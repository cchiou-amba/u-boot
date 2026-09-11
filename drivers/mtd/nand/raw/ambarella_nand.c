// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */
//#define DEBUG

#include <common.h>
#include <cpu_func.h>
#include <linux/mtd/rawnand.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/bitrev.h>
#include <linux/bch.h>
#include <linux/bug.h>
#include <malloc.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <memalign.h>
#include <nand.h>
#include <fdtdec.h>
#include <dm.h>
#include <env.h>
#include <dm/of_access.h>
#include <regmap.h>
#include <syscon.h>
#include <dm/pinctrl.h>

#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include "ambarella_nand.h"

DECLARE_GLOBAL_DATA_PTR;

#define NAND_CMD_TIMEOUT		1000
#define AMBARELLA_NAND_DMA_BUFFER_SIZE	8192

struct ambarella_nand_host {
	struct nand_chip		chip;
	struct nand_hw_control controller;

	struct udevice			*dev;
	void __iomem			*regbase;
	struct regmap			*reg_rct;

	u32				ecc_bits;
	/* bch enabled or not by POC */
	bool				bch_enabled;
	bool				page_4k;
	bool				enable_wp;
	bool				is_spinand;
	bool				sck_mode3;

	/* used for software BCH */
	struct bch_control		*bch;
	u8				soft_bch_extra_size;

	dma_addr_t			dmaaddr;
	u8				*dmabuf;
	int				dma_bufpos;
	u32				int_sts;
	u32				ecc_rpt_sts;
	u32				ecc_rpt_sts2;

	/* saved page_addr during CMD_SEQIN */
	int				seqin_page_addr;

	/* Operation parameters for nand controller register */
	int				err_code;
	u32				control_reg;
	u32				timing[6];

};
/* ==========================================================================*/
/*  "Micron MT29F2G01ABBGD_256MB_PG2K_1_8V" */
/**
 * timing parameter in ns
 */
#define NAND_TCLH               4
#define NAND_TCLL               4
#define NAND_TCS                30
#define NAND_TCLQV              14 /* PS: experience value */

#define NAND_TCHSL              4
#define NAND_TSLCH              4
#define NAND_TCHSH              4
#define NAND_TSHCH              4

#define NAND_THHQX              16 /* same as NAND_TCLQV? */
#define NAND_TWPS               20
#define NAND_TWPH               100

#define NAND_TCHHL              0
#define NAND_TCHHH              0
#define NAND_THLCH              0
#define NAND_THHCH              0

#define NAND_TIMING0    (NAND_TCLH << 24 | NAND_TCLL << 16 | NAND_TCS << 8 | NAND_TCLQV)
#define NAND_TIMING1    (NAND_TCHSL << 24 | NAND_TSLCH << 16 | NAND_TCHSH << 8 | NAND_TSHCH)
#define NAND_TIMING2    (NAND_THHQX << 16 | NAND_TWPS << 8 | NAND_TWPH)
#define NAND_TIMING3    (NAND_TCHHL << 24 | NAND_TCHHH << 16 | NAND_THLCH << 8 | NAND_THHCH)
/* ==========================================================================*/
#define FLASH_TIMING_MIN(x, offs) flash_timing(0, x, offs)
#define FLASH_TIMING_MAX(x, offs) flash_timing(1, x, offs)

static inline int flash_timing(int minmax, int val, int offs)
{
	u32 clk, x;
	int n, r;

	val = (val >> offs) & 0xff;

	/* to avoid overflow, divid clk by 1000000 first */
	clk = get_nand_freq_hz() / 1000000;

	x = val * clk;
	n = x / 1000;
	r = x % 1000;

	if (r != 0)
		n++;

	if (minmax)
		n--;

	return (n < 1 ? 0 : (n-1)) << offs;
}

static int ambarella_nand_init_timings(struct ambarella_nand_host *host)
{
	u32 timing0 = NAND_TIMING0;
	u32 timing1 = NAND_TIMING1;
	u32 timing2 = NAND_TIMING2;

	u32 clk = get_nand_freq_hz() / 1000000;
	printf("nand clk is %dMhz \n", clk);
	/* timing 0 */
	writel(FLASH_TIMING_MIN(timing0, 24)	|
				FLASH_TIMING_MIN(timing0, 16)	|
				FLASH_TIMING_MIN(timing0, 8)	|
				FLASH_TIMING_MAX(timing0, 0),
				host->regbase + SPINAND_TIMING0_OFFSET);
	/* timing 1 */
	writel(FLASH_TIMING_MIN(timing1, 24)	|
				FLASH_TIMING_MIN(timing1, 16)	|
				FLASH_TIMING_MIN(timing1, 8)	|
				FLASH_TIMING_MIN(timing1, 0),
				host->regbase + SPINAND_TIMING1_OFFSET);

	/* timing 2 */
	writel(FLASH_TIMING_MAX(timing2, 16)	|
			    FLASH_TIMING_MIN(timing2, 8)	|
			    FLASH_TIMING_MIN(timing2, 0),
				host->regbase + SPINAND_TIMING2_OFFSET);


	return 0;
}

static int amb_ecc6_ooblayout_ecc_lp(struct mtd_info *mtd, int section,
		struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 16) + 6;
	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int amb_ecc6_ooblayout_free_lp(struct mtd_info *mtd, int section,
		struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 16) + 1;
	oobregion->length = 5;

	return 0;
}

static const struct mtd_ooblayout_ops amb_ecc6_lp_ooblayout_ops = {
	.ecc = amb_ecc6_ooblayout_ecc_lp,
	.rfree = amb_ecc6_ooblayout_free_lp,
};

static int amb_ecc8_ooblayout_ecc_lp(struct mtd_info *mtd, int section,
		struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 32) + 19;
	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int amb_ecc8_ooblayout_free_lp(struct mtd_info *mtd, int section,
		struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 32) + 2;;
	oobregion->length = 17;

	return 0;
}

static const struct mtd_ooblayout_ops amb_ecc8_lp_ooblayout_ops = {
	.ecc = amb_ecc8_ooblayout_ecc_lp,
	.rfree = amb_ecc8_ooblayout_free_lp,
};

static u32 to_native_cmd(struct ambarella_nand_host *host, u32 cmd)
{
	u32 native_cmd, is_spinand = host->is_spinand;

	switch (cmd) {
		case NAND_CMD_RESET:
			native_cmd = is_spinand ? NAND_AMB_CC_RESET : NAND_AMB_CMD_RESET;
			break;
		case NAND_CMD_READID:
			native_cmd = NAND_AMB_CC_READID;
			break;
		case NAND_CMD_STATUS:
			native_cmd = is_spinand ? NAND_AMB_CC_READSTATUS : NAND_AMB_CMD_READSTATUS;
			break;
		case NAND_CMD_SET_FEATURES:
			native_cmd = NAND_AMB_CC_SETFEATURE;
			break;
		case NAND_CMD_GET_FEATURES:
			native_cmd = NAND_AMB_CC_GETFEATURE;
			break;
		case NAND_CMD_ERASE1:
			native_cmd = NAND_AMB_CC_ERASE;
			break;
		case NAND_CMD_READOOB:
		case NAND_CMD_READ0:
			native_cmd = is_spinand ? NAND_AMB_CC_READ : NAND_AMB_CMD_READ;
			break;
		case NAND_CMD_PAGEPROG:
			native_cmd = is_spinand ? NAND_AMB_CC_PROGRAM : NAND_AMB_CMD_PROGRAM;
			break;
		case NAND_CMD_PARAM:
			native_cmd = NAND_AMB_CC_READ_PARAM;
			break;
		default:
			dev_err(host->dev, "Unknown command: %d\n", cmd);
			BUG();
			break;
	}

	return native_cmd;
}

static int count_zero_bits(u8 *buf, int size, int max_bits)
{
	int i, zero_bits = 0;

	for (i = 0; i < size; i++) {
		zero_bits += hweight8(~buf[i]);
		if (zero_bits > max_bits)
			break;
	}

	return zero_bits;
}

static int nand_bch_check_blank_page(struct ambarella_nand_host *host)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info	*mtd = nand_to_mtd(chip);
	int eccsteps = chip->ecc.steps, zero_bits = 0, zeroflip = 0, oob_subset;
	u8 *bufpos, *bsp;
	u32 i;

	bufpos = host->dmabuf;
	bsp = host->dmabuf + mtd->writesize;
	oob_subset = mtd->oobsize / eccsteps;

	for (i = 0; i < eccsteps; i++) {
		zero_bits = count_zero_bits(bufpos, chip->ecc.size, chip->ecc.strength);
		if (zero_bits > chip->ecc.strength)
			return -1;

		if (zero_bits)
			zeroflip = 1;

		zero_bits += count_zero_bits(bsp, oob_subset, chip->ecc.strength);
		if (zero_bits > chip->ecc.strength)
			return -1;

		bufpos += chip->ecc.size;
		bsp += oob_subset;
	}

	if (zeroflip)
		memset(host->dmabuf, 0xff, mtd->writesize);

	return 0;
}

static void ambarella_nand_setup_dma(struct ambarella_nand_host *host, u32 cmd)
{
	struct mtd_info *mtd = nand_to_mtd(&host->chip);
	u32 dmaaddr, fdma_ctrl;

	/* Setup FDMA engine transfer */
	dmaaddr = host->dmaaddr;
	writel(dmaaddr, host->regbase + FDMA_MN_MEM_ADDR_OFFSET);
	dmaaddr = host->dmaaddr + mtd->writesize;
	writel(dmaaddr, host->regbase + FDMA_SP_MEM_ADDR_OFFSET);

	if(NAND_CMD_CMD(cmd) == NAND_AMB_CMD_READ)
		invalidate_dcache_range((ulong)host->dmaaddr,
				(ulong)host->dmaaddr + ROUND(mtd->writesize + mtd->oobsize, ARCH_DMA_MINALIGN));
	else
		flush_dcache_range((ulong)host->dmaaddr,
				(ulong)host->dmaaddr + ROUND(mtd->writesize + mtd->oobsize, ARCH_DMA_MINALIGN));

	fdma_ctrl = (cmd == NAND_AMB_CMD_READ) ? FDMA_CTRL_WRITE_MEM : FDMA_CTRL_READ_MEM;
	fdma_ctrl |= FDMA_CTRL_ENABLE | FDMA_CTRL_BLK_SIZE_512B;
	fdma_ctrl |= mtd->writesize + mtd->oobsize;
	writel(fdma_ctrl, host->regbase + FDMA_MN_CTRL_OFFSET);
}

static void ambarella_nand_readid(struct ambarella_nand_host *host, u32 page_addr)
{
	u32 val;

	/* disable BCH if using soft ecc */
	val = readl(host->regbase + FIO_CTRL_OFFSET);
	val &= ~FIO_CTRL_ECC_BCH_ENABLE;
	writel(val, host->regbase + FIO_CTRL_OFFSET);

	val = NAND_CC_WORD_CMD1VAL0(NAND_CMD_READID);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(page_addr, host->regbase + NAND_COPY_ADDR_OFFSET);
	writel(0x0, host->regbase + NAND_CP_ADDR_H_OFFSET);

	val = NAND_CC_DATA_CYCLE(8) | NAND_CC_RW_READ | NAND_CC_WAIT_TWHR |
		NAND_CC_ADDR_CYCLE(1) | NAND_CC_CMD1(1) |
		NAND_CC_ADDR_SRC(0) | NAND_CC_TERMINATE_CE;
	writel(val, host->regbase + NAND_CC_OFFSET);
}

#define ONFI_PARAM_SIZE (256)
static void ambarella_nand_cc_read_param(struct ambarella_nand_host *host, u32 page_addr)
{
	u32 dmaaddr, fdma_ctrl, val;

	/* disable BCH if using soft ecc */
	val = readl(host->regbase + FIO_CTRL_OFFSET);
	val &= ~FIO_CTRL_ECC_BCH_ENABLE;
	writel(val, host->regbase + FIO_CTRL_OFFSET);

	/* Setup FDMA engine transfer */
	dmaaddr = host->dmaaddr;
	writel(dmaaddr, host->regbase + FDMA_MN_MEM_ADDR_OFFSET);

	fdma_ctrl = FDMA_CTRL_ENABLE | FDMA_CTRL_WRITE_MEM |
		FDMA_CTRL_BLK_SIZE_256B | ONFI_PARAM_SIZE;
	writel(fdma_ctrl, host->regbase + FDMA_MN_CTRL_OFFSET);

	val = NAND_CC_WORD_CMD1VAL0(NAND_CMD_PARAM);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(page_addr, host->regbase + NAND_COPY_ADDR_OFFSET);
	writel(0x0, host->regbase + NAND_CP_ADDR_H_OFFSET);

	val = NAND_CC_RW_READ | NAND_CC_DATA_SRC_DMA | NAND_CC_WAIT_RB |
		NAND_CC_ADDR_CYCLE(1) | NAND_CC_CMD1(1) | NAND_CC_ADDR_SRC(0);
	writel(val, host->regbase + NAND_CC_OFFSET);
}

static void ambarella_nand_cc_reset(struct ambarella_nand_host *host)
{
	u32 val;

	val = NAND_CC_WORD_CMD1VAL0(SPINAND_CMD_RESET);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(0x0, host->regbase + SPINAND_CC2_OFFSET);
	writel(0x0, host->regbase + SPINAND_CC1_OFFSET);
}

static void ambarella_nand_cc_readid(struct ambarella_nand_host *host)
{
	u32 val;

	val = NAND_CC_WORD_CMD1VAL0(SPINAND_CMD_READ_ID);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(0x0, host->regbase + NAND_COPY_ADDR_OFFSET);
	writel(0x0, host->regbase + NAND_CP_ADDR_H_OFFSET);

	writel(0x0, host->regbase + SPINAND_CC2_OFFSET);

	val = SPINAND_CC_DATA_CYCLE(4) | SPINAND_CC_RW_READ |
		SPINAND_CC_ADDR_CYCLE(1);
	writel(val, host->regbase + SPINAND_CC1_OFFSET);
}

static void ambarella_nand_cc_readstatus(struct ambarella_nand_host *host)
{
	u32 val;

	val = NAND_CC_WORD_CMD1VAL0(SPINAND_CMD_GET_FEATURE);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(0xC0, host->regbase + NAND_COPY_ADDR_OFFSET);
	writel(0x0, host->regbase + NAND_CP_ADDR_H_OFFSET);

	writel(0x0, host->regbase + SPINAND_CC2_OFFSET);

	val = SPINAND_CC_DATA_CYCLE(1) | SPINAND_CC_RW_READ | SPINAND_CC_ADDR_CYCLE(1);
	writel(val, host->regbase + SPINAND_CC1_OFFSET);
}

static void ambarella_nand_cc_setfeature(struct ambarella_nand_host *host,
		u8 feature_addr, u8 value)
{
	u32 val;

	writel(feature_addr, host->regbase + NAND_COPY_ADDR_OFFSET);
	writel(0x0, host->regbase + NAND_CP_ADDR_H_OFFSET);

	val = NAND_CC_WORD_CMD1VAL0(SPINAND_CMD_SET_FEATURE);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(SPINAND_ERR_PATTERN, host->regbase + SPINAND_ERR_PATTERN_OFFSET);
	writel(SPINAND_DONE_PATTERN, host->regbase + SPINAND_DONE_PATTERN_OFFSET);

	writel(value, host->regbase + NAND_CC_DAT0_OFFSET);

	writel(0, host->regbase + SPINAND_CC2_OFFSET);

	val = SPINAND_CC1_AUTO_WE | SPINAND_CC_AUTO_STSCHK |
		SPINAND_CC_RW_WRITE | SPINAND_CC_ADDR_CYCLE(1);
	writel(val, host->regbase + SPINAND_CC1_OFFSET);
}

static void ambarella_nand_cc_erase(struct ambarella_nand_host *host, u32 page_addr)
{
	u32 val;

	if (host->is_spinand) {
		/* Note: spinand use page number as address for block erase. */
		writel(page_addr, host->regbase + NAND_COPY_ADDR_OFFSET);
		writel(0x0, host->regbase + NAND_CP_ADDR_H_OFFSET);

		val = NAND_CC_WORD_CMD1VAL0(SPINAND_CMD_BLK_ERASE);
		writel(val, host->regbase + NAND_CC_WORD_OFFSET);

		val = SPINAND_ERR_PATTERN;
		writel(val, host->regbase + SPINAND_ERR_PATTERN_OFFSET);
		val = SPINAND_DONE_PATTERN;
		writel(val, host->regbase + SPINAND_DONE_PATTERN_OFFSET);

		writel(0x0, host->regbase + SPINAND_CC2_OFFSET);

		val = SPINAND_CC1_AUTO_WE | SPINAND_CC_AUTO_STSCHK |
			SPINAND_CC_ADDR_CYCLE(3);
		writel(val, host->regbase + SPINAND_CC1_OFFSET);
	} else {
		val = NAND_CC_WORD_CMD1VAL0(0x60) | NAND_CC_WORD_CMD2VAL0(0xD0);
		writel(val, host->regbase + NAND_CC_WORD_OFFSET);

		val = NAND_CC_DATA_CYCLE(5) | NAND_CC_WAIT_RB |
			NAND_CC_CMD2(1) | NAND_CC_ADDR_CYCLE(3) |
			NAND_CC_CMD1(1) | NAND_CC_ADDR_SRC(1) |
			NAND_CC_TERMINATE_CE;
		writel(val, host->regbase + NAND_CC_OFFSET);
	}
}

static void ambarella_nand_cc_read(struct ambarella_nand_host *host, u32 page_addr)
{
	u32 val;

	val = NAND_CC_WORD_CMD1VAL0(SPINAND_CMD_PAGE_READ) |
		NAND_CC_WORD_CMD2VAL0(SPINAND_CMD_READ_CACHE_X4);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(SPINAND_ERR_PATTERN, host->regbase + SPINAND_ERR_PATTERN_OFFSET);
	writel(SPINAND_DONE_PATTERN, host->regbase + SPINAND_DONE_PATTERN_OFFSET);

	val = SPINAND_CC2_ENABLE | SPINAND_CC_DATA_SRC_DMA |
		SPINAND_CC_DUMMY_DATA_NUM(1) | SPINAND_CC_ADDR_CYCLE(2) |
		SPINAND_CC_ADDR_SRC(2) | SPINAND_CC_RW_READ | SPINAND_LANE_NUM(4);
	writel(val, host->regbase + SPINAND_CC2_OFFSET);

	val = SPINAND_CC_AUTO_STSCHK | SPINAND_CC_DATA_SRC_DMA |
		SPINAND_CC_ADDR_SRC(1) | SPINAND_CC_ADDR_CYCLE(3);
	writel(val, host->regbase + SPINAND_CC1_OFFSET);
}

static void ambarella_nand_cc_write(struct ambarella_nand_host *host, u32 page_addr)
{
	u32 val;

	val = NAND_CC_WORD_CMD1VAL0(SPINAND_CMD_PROG_LOAD_X4) |
		NAND_CC_WORD_CMD2VAL0(SPINAND_CMD_PROG_EXEC);
	writel(val, host->regbase + NAND_CC_WORD_OFFSET);

	writel(SPINAND_ERR_PATTERN, host->regbase + SPINAND_ERR_PATTERN_OFFSET);
	writel(SPINAND_DONE_PATTERN, host->regbase + SPINAND_DONE_PATTERN_OFFSET);

	val = SPINAND_CC_AUTO_STSCHK | SPINAND_CC2_ENABLE | SPINAND_CC_DATA_SRC_DMA |
		SPINAND_CC_ADDR_CYCLE(3) | SPINAND_CC_ADDR_SRC(1);
	writel(val, host->regbase + SPINAND_CC2_OFFSET);

	val = SPINAND_CC1_AUTO_WE | SPINAND_CC_DATA_SRC_DMA |
		SPINAND_CC_ADDR_CYCLE(2) | SPINAND_CC_ADDR_SRC(2) |
		SPINAND_CC_RW_WRITE | SPINAND_LANE_NUM(4);
	writel(val, host->regbase + SPINAND_CC1_OFFSET);
}

static void nand_wait_cmd_done(struct ambarella_nand_host *host, u32 cmd)
{
	u32 rval;
	u32 start;

	//if (!NAND_CMD_IS_CC(cmd))
	//	writel(addr | cmd, NAND_CMD_REG);

	start = get_timer(0);
	while(1) {
		rval = readl(host->regbase + FIO_RAW_INT_STATUS_OFFSET);

		if (rval & (FIO_INT_ECC_RPT_UNCORR |
					FIO_INT_ECC_RPT_THRESH |
					FIO_INT_SND_LOOP_TIMEOUT |
					FIO_INT_OPERATION_DONE)) {

			host->int_sts = rval;
			host->ecc_rpt_sts =
				readl(host->regbase + FIO_ECC_RPT_STATUS_OFFSET);
			host->ecc_rpt_sts2 =
				readl(host->regbase + FIO_ECC_RPT_STATUS2_OFFSET);
			break;
		}

		if (get_timer(start) >= NAND_CMD_TIMEOUT) {
			printf("nand cmd timeout: 0x%02x\n", cmd);
			while(1);
		}
	}

	BUG_ON(rval & FIO_INT_SND_LOOP_TIMEOUT);

	writel(rval, host->regbase + FIO_RAW_INT_STATUS_OFFSET);
}


static int ambarella_nand_issue_cmd(struct ambarella_nand_host *host,
		u32 cmd, u32 page_addr)
{
	struct mtd_info *mtd = nand_to_mtd(&host->chip);
	u64 addr64 = (u64)page_addr * mtd->writesize;
	u32 native_cmd = to_native_cmd(host, cmd);
	u32 nand_ctrl, nand_cmd;
	int rval = 0;

	host->int_sts = 0;

	if (NAND_CMD_CMD(native_cmd) == NAND_AMB_CMD_READ ||
			NAND_CMD_CMD(native_cmd) == NAND_AMB_CMD_PROGRAM )
		ambarella_nand_setup_dma(host, NAND_CMD_CMD(native_cmd));

	nand_ctrl = host->control_reg | NAND_CTRL_A33_32(addr64 >> 32);
	nand_cmd = (u32)addr64 | NAND_AMB_CMD(native_cmd);
	writel(nand_ctrl, host->regbase + NAND_CTRL_OFFSET);
	writel(nand_cmd, host->regbase + NAND_CMD_OFFSET);

	switch (native_cmd) {
		case NAND_AMB_CC_RESET:
			ambarella_nand_cc_reset(host);
			break;
		case NAND_AMB_CC_READID:
			if (host->is_spinand)
				ambarella_nand_cc_readid(host);
			else
				ambarella_nand_readid(host, page_addr);
			break;
		case NAND_AMB_CC_READSTATUS:
			ambarella_nand_cc_readstatus(host);
			break;
		case NAND_AMB_CC_SETFEATURE:
			ambarella_nand_cc_setfeature(host, page_addr, 0x00);
			break;
		case NAND_AMB_CC_GETFEATURE:
			break;
		case NAND_AMB_CC_ERASE:
			ambarella_nand_cc_erase(host, page_addr);
			break;
		case NAND_AMB_CC_READ:
			ambarella_nand_cc_read(host, page_addr);
			break;
		case NAND_AMB_CC_PROGRAM:
			ambarella_nand_cc_write(host, page_addr);
			break;
		case NAND_AMB_CC_READ_PARAM:
			ambarella_nand_cc_read_param(host, page_addr);
			break;
	}

	/* now waiting for command completed */
#if 0
	timeout = wait_event_timeout(host->wq, host->int_sts, 1 * HZ);
	if (timeout <= 0) {
		rval = -EBUSY;
		dev_err(host->dev, "cmd=0x%x timeout\n", native_cmd);
	}
#endif

	nand_wait_cmd_done(host, native_cmd);

	/* avoid to flush previous error info */
	if (host->err_code == 0)
		host->err_code = rval;

	return rval;
}


/* ==========================================================================*/
static uint8_t ambarella_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_host *host = nand_get_controller_data(chip);
	uint8_t *data;

	data = host->dmabuf + host->dma_bufpos;
	host->dma_bufpos++;

	return *data;
}

static u16 ambarella_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_host *host = nand_get_controller_data(chip);
	u16 *data;

	data = (u16 *)(host->dmabuf + host->dma_bufpos);
	host->dma_bufpos += 2;

	return *data;
}

static void ambarella_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_host *host = nand_get_controller_data(chip);

	BUG_ON((host->dma_bufpos + len) > AMBARELLA_NAND_DMA_BUFFER_SIZE);

	memcpy(buf, host->dmabuf + host->dma_bufpos, len);
	host->dma_bufpos += len;
}

static void ambarella_nand_write_buf(struct mtd_info *mtd,
		const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_host *host = nand_get_controller_data(chip);

	BUG_ON((host->dma_bufpos + len) > AMBARELLA_NAND_DMA_BUFFER_SIZE);

	memcpy(host->dmabuf + host->dma_bufpos, buf, len);

	host->dma_bufpos += len;
}

static void ambarella_nand_select_chip(struct mtd_info *mtd, int chip)
{
	if (chip > 0)
		dev_err(host->dev, "Multi-Chip isn't supported yet.\n");
}

static void ambarella_nand_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{
}

static int ambarella_nand_dev_ready(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);

	return (chip->read_byte(mtd) & NAND_STATUS_READY) ? 1 : 0;
}

static int ambarella_nand_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct ambarella_nand_host *host = nand_get_controller_data(chip);
	int status = 0;

	/*
	 * ambarella nand controller has waited for the command completion,
	 * but still need to check the nand chip's status
	 */
	if (host->err_code) {
		status = NAND_STATUS_FAIL;
	} else {
		chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);
		status = chip->read_byte(mtd);
	}

	return status;
}

static void ambarella_nand_cmdfunc(struct mtd_info *mtd, unsigned cmd,
		int column, int page_addr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_host *host = nand_get_controller_data(chip);
	u32 val, *id, fio_ctr_bak = 0;

	host->err_code = 0;

	switch(cmd) {
		case NAND_CMD_ERASE2:
			break;

		case NAND_CMD_SEQIN:
			host->dma_bufpos = column;
			host->seqin_page_addr = page_addr;
			break;

		case NAND_CMD_READID:
			fio_ctr_bak = readl(host->regbase + FIO_CTRL_OFFSET);
			host->dma_bufpos = 0;
			ambarella_nand_issue_cmd(host, cmd, column);
			break;
		case NAND_CMD_PARAM:
			host->dma_bufpos = 0;
			if(!host->is_spinand) {
				fio_ctr_bak = readl(host->regbase + FIO_CTRL_OFFSET);
				ambarella_nand_issue_cmd(host, cmd, column);
			}
			break;
		case NAND_CMD_STATUS:
			host->dma_bufpos = 0;
			ambarella_nand_issue_cmd(host, cmd, 0);
			break;

		case NAND_CMD_RESET:
			host->dma_bufpos = 0;
			ambarella_nand_issue_cmd(host, cmd, 0);
			if (host->is_spinand) {
				mdelay(2);
				/* unlock all blocks */
				ambarella_nand_issue_cmd(host, NAND_CMD_SET_FEATURES, 0xA0);
			}
			break;

		case NAND_CMD_READOOB:
		case NAND_CMD_READ0:
			host->dma_bufpos = (cmd == NAND_CMD_READ0) ? column : mtd->writesize;
			ambarella_nand_issue_cmd(host, cmd, page_addr);
			break;

		case NAND_CMD_PAGEPROG:
			page_addr = host->seqin_page_addr;
		case NAND_CMD_ERASE1:
			ambarella_nand_issue_cmd(host, cmd, page_addr);
			break;

		default:
			dev_err(host->dev, "%s: 0x%x, %d, %d\n",
					__func__, cmd, column, page_addr);
			BUG();
			break;
	}

	switch(cmd) {
		case NAND_CMD_READID:
			id = (u32 *)host->dmabuf;

			val = readl(host->regbase + NAND_CC_DAT0_OFFSET);
			*id = val;

			val = readl(host->regbase + NAND_CC_DAT1_OFFSET);
			host->dmabuf[4] = (unsigned char)(val & 0xff);

			writel(fio_ctr_bak, host->regbase + FIO_CTRL_OFFSET);
			break;

		case NAND_CMD_STATUS:
			if (host->is_spinand) {
				/*
				 * no matter the device is Write Enable or not, we can
				 * always send the Write Enable Command automatically
				 * prior to PROGRAM or ERASE command.
				 */
				val = readl(host->regbase + NAND_CC_DAT0_OFFSET);
				val &= 0x000000FF;
				host->dmabuf[0] = NAND_STATUS_WP;
				if (!(val & 0x1))
					host->dmabuf[0] |= NAND_STATUS_READY;
				if (val & 0x2c)
					host->dmabuf[0] |= NAND_STATUS_FAIL;
			} else {
				val = readl(host->regbase + NAND_STATUS_OFFSET);
				host->dmabuf[0] = (unsigned char)val;
			}
			break;

		case NAND_CMD_READOOB:
		case NAND_CMD_READ0:

			if (host->int_sts & FIO_INT_ECC_RPT_UNCORR) {
				int count = 0;
				count = nand_bch_check_blank_page(host);
				if (count < 0) {
					mtd->ecc_stats.failed++;
					dev_err(host->dev,
							"BCH corrected failed in block[%d]!\n",
							FIO_ECC_RPT_UNCORR_BLK_ADDR(host->ecc_rpt_sts2));
				}
			} else if (host->int_sts & FIO_INT_ECC_RPT_THRESH) {
				val = FIO_ECC_RPT_MAX_ERR_NUM(host->ecc_rpt_sts);
				mtd->ecc_stats.corrected += val;
				dev_info(host->dev, "BCH correct [%d]bit in block[%d]\n",
						val, FIO_ECC_RPT_BLK_ADDR(host->ecc_rpt_sts));
			}
			break;

		case NAND_CMD_PARAM:
			writel(fio_ctr_bak, host->regbase + FIO_CTRL_OFFSET);
			break;
	}
}

static void ambarella_nand_hwctl(struct mtd_info *mtd, int mode)
{

}

static int ambarella_nand_calculate_ecc(struct mtd_info *mtd,
		const u_char *buf, u_char *code)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_host *host = nand_get_controller_data(chip);

	memset(code, 0xff, host->chip.ecc.bytes);

	return 0;
}

static int ambarella_nand_correct_data(struct mtd_info *mtd, u_char *buf,
		u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_host *host = nand_get_controller_data(chip);
	int rval = 0;

	/*
	 * if we use hardware ecc, any errors include DMA error and FIO DMA
	 * error, we consider it as a ecc error which will tell the caller the
	 * read is failed. We have distinguish all the errors, but the
	 * nand_read_ecc only check the return value by this function.
	 */
	rval = host->err_code;

	return rval;
}

static int ambarella_nand_write_oob_std(struct mtd_info *mtd,
		struct nand_chip *chip, int page)
{
	uint8_t *ecc_calc = chip->buffers->ecccalc;
	int i, status, eccsteps;

	/*
	 * Our nand controller will write the generated ECC code into spare
	 * area automatically, so we should mark the ECC code which located
	 * in the eccpos.
	 */
#if 1
	eccsteps = chip->ecc.steps;
	for (i = 0; eccsteps; eccsteps--, i += chip->ecc.bytes) {
		chip->ecc.calculate(mtd, NULL, &ecc_calc[i]);
		status = mtd_ooblayout_set_eccbytes(mtd, ecc_calc,
				chip->oob_poi, 0, chip->ecc.total);
		if (status)
			return status;
	}
#else
	for (i = 0; i < chip->ecc.total; i++)
		chip->oob_poi[chip->ecc.layout->eccpos[i]] = 0xFF;
#endif	

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

static void ambarella_nand_init_hw(struct ambarella_nand_host *host)
{
	u32 val;

	/* Reset FIO FIFO and then exit random read mode */
	val = readl(host->regbase + FIO_CTRL_OFFSET);
	val |= FIO_CTRL_RANDOM_READ;
	writel(val, host->regbase + FIO_CTRL_OFFSET);
	/* wait for some time to make sure FIO FIFO reset is done */
	mdelay(3);
	val &= ~FIO_CTRL_RANDOM_READ;
	writel(val, host->regbase + FIO_CTRL_OFFSET);

	/* always use 5 cycles to read ID */
	val = readl(host->regbase + NAND_EXT_CTRL_OFFSET);
	val |= NAND_EXT_CTRL_I5;
	if (host->page_4k)
		val |= NAND_EXT_CTRL_4K_PAGE;
	else
		val &= ~NAND_EXT_CTRL_4K_PAGE;
	writel(val, host->regbase + NAND_EXT_CTRL_OFFSET);

	/* always enable dual-space mode if BCH is enabled by POC */
	if (host->bch_enabled) {
		if (host->ecc_bits == 6)
			val = FDMA_DSM_MAIN_JP_SIZE_512B | FDMA_DSM_SPARE_JP_SIZE_16B;
		else
			val = FDMA_DSM_MAIN_JP_SIZE_512B | FDMA_DSM_SPARE_JP_SIZE_32B;
	} else {
		if (host->page_4k)
			val = FDMA_DSM_MAIN_JP_SIZE_4KB | FDMA_DSM_SPARE_JP_SIZE_128B;
		else
			val = FDMA_DSM_MAIN_JP_SIZE_2KB | FDMA_DSM_SPARE_JP_SIZE_64B;

		if (host->ecc_bits == 8)
			val += 0x1;
	}
	writel(val, host->regbase + FDMA_DSM_CTRL_OFFSET);

	/* disable BCH if using soft ecc */
	val = readl(host->regbase + FIO_CTRL_OFFSET);
	val |= FIO_CTRL_RDERR_STOP | FIO_CTRL_SKIP_BLANK_ECC;
	if (!host->bch_enabled)
		val &= ~FIO_CTRL_ECC_BCH_ENABLE;
	else
		val |= FIO_CTRL_ECC_BCH_ENABLE;
	writel(val, host->regbase + FIO_CTRL_OFFSET);

	if (host->is_spinand) {
		val = readl(host->regbase + FIO_CTRL2_OFFSET);
		val |= FIO_CTRL2_SPINAND;
		writel(val, host->regbase + FIO_CTRL2_OFFSET);

		val = readl(host->regbase + SPINAND_CTRL_OFFSET);

		if (host->sck_mode3)
			val |= SPINAND_CTRL_SCKMODE_3;

		val &= ~SPINAND_CTRL_PS_SEL_MASK;
		val |= SPINAND_CTRL_PS_SEL_6;

		writel(val, host->regbase + SPINAND_CTRL_OFFSET);
	}

	//ambarella_nand_set_timing(host);

	/* setup min number of correctable bits that not trigger irq. */
	val = FIO_ECC_RPT_ERR_NUM_TH(host->ecc_bits);
	writel(val, host->regbase + FIO_ECC_RPT_CFG_OFFSET);

	/* clear and enable nand irq */
	val = readl(host->regbase + FIO_RAW_INT_STATUS_OFFSET);
	writel(val, host->regbase + FIO_RAW_INT_STATUS_OFFSET);
	val = FIO_INT_OPERATION_DONE | FIO_INT_SND_LOOP_TIMEOUT |
		FIO_INT_ECC_RPT_UNCORR | FIO_INT_ECC_RPT_THRESH | FIO_INT_AXI_BUS_ERR;
	writel(val, host->regbase + FIO_INT_ENABLE_OFFSET);
}

static int ambarella_nand_config_flash(struct ambarella_nand_host *host)
{
	int					rval = 0;

	/*
	 * Calculate row address cycyle according to whether the page number
	 * of the nand is greater than 65536.
	 */
	if ((host->chip.chip_shift - host->chip.page_shift) > 16)
		host->control_reg |= NAND_CTRL_P3;
	else
		host->control_reg &= ~NAND_CTRL_P3;

	host->control_reg &= ~NAND_CTRL_SIZE_8G;
	switch (host->chip.chipsize) {
		case 8 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_64M;
			break;
		case 16 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_128M;
			break;
		case 32 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_256M;
			break;
		case 64 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_512M;
			break;
		case 128 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_1G;
			break;
		case 256 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_2G;
			break;
		case 512 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_4G;
			break;
		case 1024 * 1024 * 1024:
			host->control_reg |= NAND_CTRL_SIZE_8G;
			break;
		default:
			dev_err(host->dev,
					"Unexpected NAND flash chipsize %lld. Aborting\n",
					host->chip.chipsize);
			rval = -ENXIO;
			break;
	}

	return rval;
}

static void ambarella_nand_init_chip(struct ambarella_nand_host *host,
		struct udevice *dev)
{
	struct nand_chip *chip = &host->chip;
	u32 poc = rct_system_config();

#if 1
	host->page_4k = (poc & SYS_CONFIG_NAND_PAGE_SIZE) ? false : true;
	host->sck_mode3 = (poc & SYS_CONFIG_NAND_SCKMODE) ? true : false;
	host->bch_enabled = (poc & SYS_CONFIG_NAND_ECC_BCH_EN) ? true : false;
	host->is_spinand = dev_read_bool(dev, "amb,spinand-only") ? true :
		(poc & SYS_CONFIG_NAND_SPINAND) ? true : false;

	/*
	 * if ecc is generated by software, the ecc bits num will
	 * be defined in FDT.
	 */
	host->ecc_bits = (poc & SYS_CONFIG_NAND_ECC_SPARE_2X) ? 8 : 6;
#else
	host->page_4k = false;
	host->sck_mode3 = false;
	host->bch_enabled = true;
	host->is_spinand = false;
	host->ecc_bits = 6;
#endif
	debug("%s\n", host->is_spinand ? "spinand" : "nand");

	ambarella_nand_init_hw(host);
	/*
	 * Always use P3 and I5 to support all NAND,
	 * but we will adjust page cycles after read ID from NAND.
	 */
	host->control_reg = NAND_CTRL_P3 | NAND_CTRL_SIZE_8G;
	host->control_reg |= host->enable_wp ? NAND_CTRL_WP : 0;

	chip->chip_delay = 0;
	chip->controller = &host->controller;
	chip->read_byte = ambarella_nand_read_byte;
	chip->read_word = ambarella_nand_read_word;
	chip->write_buf = ambarella_nand_write_buf;
	chip->read_buf = ambarella_nand_read_buf;
	chip->select_chip = ambarella_nand_select_chip;
	chip->cmd_ctrl = ambarella_nand_cmd_ctrl;
	chip->dev_ready = ambarella_nand_dev_ready;
	chip->waitfunc = ambarella_nand_waitfunc;
	chip->cmdfunc = ambarella_nand_cmdfunc;
	//chip->onfi_set_features = nand_onfi_get_set_features_notsupp;
	//chip->onfi_get_features = nand_onfi_get_set_features_notsupp;
	/* Disable subpage writes as we do not provide ecc->hwctl */
	chip->options |= NAND_NO_SUBPAGE_WRITE;

	nand_set_flash_node(chip, dev->node);
}

static struct nand_ecclayout nand_oob;
static int ambarella_nand_init_chipecc(struct ambarella_nand_host *host)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info	*mtd = nand_to_mtd(chip);
	int i, j, steps;

	/* sanity check */
	BUG_ON(mtd->writesize != 2048 && mtd->writesize != 4096);
	BUG_ON(host->ecc_bits != 6 && host->ecc_bits != 8);
	BUG_ON(host->ecc_bits == 8 && mtd->oobsize < 128);

	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.strength = host->ecc_bits;

	switch (host->ecc_bits) {
		case 8:
			chip->ecc.size = 512;
			chip->ecc.bytes = 13;
			//chip->ecc.layout = &amb_oobinfo_2048_dsm_ecc8;
			steps = mtd->writesize / chip->ecc.size;
			nand_oob.eccbytes = chip->ecc.bytes * steps;
			for (i = 0; i <  steps; i++)
				for (j = 0; j < chip->ecc.bytes; j++)
					nand_oob.eccpos[i * chip->ecc.bytes + j] = 2 + 17 + j + 32 * i;
			for (i = 0; i <  steps; i++) {
				nand_oob.oobfree[i].offset = 2 + 32 * i;
				nand_oob.oobfree[i].length = 17;
			}
			chip->ecc.layout = &nand_oob;
			host->soft_bch_extra_size = 19;
			mtd_set_ooblayout(mtd, &amb_ecc8_lp_ooblayout_ops);
			break;
		case 6:
			chip->ecc.size = 512;
			chip->ecc.bytes = 10;
			//chip->ecc.layout = &amb_oobinfo_2048_dsm_ecc6;
			steps = mtd->writesize / chip->ecc.size;
			nand_oob.eccbytes = chip->ecc.bytes * steps;
			for (i = 0; i <  steps; i++)
				for (j = 0; j < chip->ecc.bytes; j++)
					nand_oob.eccpos[i * chip->ecc.bytes + j] = 1 + 5 + j + 16 * i;
			for (i = 0; i <  steps; i++) {
				nand_oob.oobfree[i].offset = 1 + 16 * i;
				nand_oob.oobfree[i].length = 5;
			}
			chip->ecc.layout = &nand_oob;
			host->soft_bch_extra_size = 6;
			mtd_set_ooblayout(mtd, &amb_ecc6_lp_ooblayout_ops);
			break;
	}
	chip->ecc.hwctl = ambarella_nand_hwctl;
	chip->ecc.calculate = ambarella_nand_calculate_ecc;
	chip->ecc.correct = ambarella_nand_correct_data;
	chip->ecc.write_oob = ambarella_nand_write_oob_std;

	return 0;
}
#if 0
static void ambarella_nand_set_sdr_timing(struct ambarella_nand_host *host)
{
	if (host->is_spinand) {
		//TO DO
#if 0
		host->timing[0] = NAND_TIMING0;
		host->timing[1] = NAND_TIMING1;
		host->timing[2] = NAND_TIMING2;

		writel(FLASH_TIMING_MIN(host->timing[0], 24)	|
				FLASH_TIMING_MIN(host->timing[0], 16)	|
				FLASH_TIMING_MIN(host->timing[0], 8)	|
				FLASH_TIMING_MAX(host->timing[0], 0), host->regbase + SPINAND_TIMING0_OFFSET);

		writel(FLASH_TIMING_MIN(host->timing[1], 24)	|
				FLASH_TIMING_MIN(host->timing[1], 16)	|
				FLASH_TIMING_MIN(host->timing[1], 8)	|
				FLASH_TIMING_MIN(host->timing[1], 0), host->regbase + SPINAND_TIMING1_OFFSET);

		writel(FLASH_TIMING_MAX(host->timing[2], 16)	|
				FLASH_TIMING_MIN(host->timing[2], 8)	|
				FLASH_TIMING_MIN(host->timing[2], 0), host->regbase + SPINAND_TIMING2_OFFSET);
#endif
	} else {
		/* Setup flash timing register */
		debug("time 0-0x%x,1-0x%x,2-0x%x,3-0x%x,4-0x%x,5-0x%x,6-%x",
				host->timing[0], host->timing[1], host->timing[2],
				host->timing[3], host->timing[4], host->timing[5], host->timing[6]);
		writel(FLASH_TIMING_MIN(host->timing[0], 24)	|
				FLASH_TIMING_MIN(host->timing[0], 16)	|
				FLASH_TIMING_MIN(host->timing[0], 8)		|
				FLASH_TIMING_MIN(host->timing[0], 0), host->regbase + NAND_TIMING0_OFFSET);

		writel(FLASH_TIMING_MIN(host->timing[1], 24)	|
				FLASH_TIMING_MIN(host->timing[1], 16)	|
				FLASH_TIMING_MIN(host->timing[1], 8)		|
				FLASH_TIMING_MIN(host->timing[1], 0), host->regbase + NAND_TIMING1_OFFSET);

		writel(FLASH_TIMING_MIN(host->timing[2], 24)	|
				FLASH_TIMING_MIN(host->timing[2], 16)	|
				FLASH_TIMING_MAX(host->timing[2], 8)		|
				FLASH_TIMING_MIN(host->timing[2], 0), host->regbase + NAND_TIMING2_OFFSET);

		writel(FLASH_TIMING_MIN(host->timing[3], 24)	|
				FLASH_TIMING_MIN(host->timing[3], 16)	|
				FLASH_TIMING_MAX(host->timing[3], 8)		|
				FLASH_TIMING_MAX(host->timing[3], 0), host->regbase + NAND_TIMING3_OFFSET);

		writel(FLASH_TIMING_MIN(host->timing[4], 24)	|
				FLASH_TIMING_MIN(host->timing[4], 16)	|
				FLASH_TIMING_MIN(host->timing[4], 8)		|
				FLASH_TIMING_MIN(host->timing[4], 0), host->regbase + NAND_TIMING4_OFFSET);

		writel(FLASH_TIMING_MIN(host->timing[5], 16)	|
				FLASH_TIMING_MAX(host->timing[5], 8)		|
				FLASH_TIMING_MIN(host->timing[5], 0), host->regbase + NAND_TIMING5_OFFSET);

		writel(FLASH_TIMING_MIN(host->timing[6], 16)	|
				FLASH_TIMING_MIN(host->timing[6], 8)		|
				FLASH_TIMING_MAX(host->timing[6], 0), host->regbase + NAND_TIMING6_OFFSET);
	}
}

static void ambarella_nand_init_timings(struct ambarella_nand_host *host)
{
	int mode;
	struct nand_chip *chip = &host->chip;

	const amba_nand_sdr_timings_t *amba_nand_sdr_timing;

	mode = onfi_get_async_timing_mode(chip);
	if (mode == ONFI_TIMING_MODE_UNKNOWN) {
		//TO DO
#if 0
		ntypes = ARRAY_SIZE(builtin_flash_types);

		chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);

		id = chip->read_byte(mtd);
		id |= chip->read_byte(mtd) << 0x8;

		for (i = 0; i < ntypes; i++) {
			f = &builtin_flash_types[i];

			if (f->chip_id == id)
				break;
		}

		if (i == ntypes) {
			dev_err(&info->pdev->dev, "Error: timings not found\n");
			return -EINVAL;
		}

		pxa3xx_nand_set_timing(host, f->timing);

		if (f->flash_width == 16) {
			info->reg_ndcr |= NDCR_DWIDTH_M;
			chip->options |= NAND_BUSWIDTH_16;
		}

		info->reg_ndcr |= (f->dfc_width == 16) ? NDCR_DWIDTH_C : 0;
#endif
	} else {
		mode = fls(mode) - 1;
		if (mode < 0)
			mode = 0;

		amba_nand_sdr_timing = &amba_onfi_sdr_timings[mode].timings.sdr;
		host->timing[0] = amba_nand_sdr_timing->timing0;
		host->timing[1] = amba_nand_sdr_timing->timing1;
		host->timing[2] = amba_nand_sdr_timing->timing2;
		host->timing[3] = amba_nand_sdr_timing->timing3;
		host->timing[4] = amba_nand_sdr_timing->timing4;
		host->timing[5] = amba_nand_sdr_timing->timing5;
		host->timing[6] = amba_nand_sdr_timing->timing6;


		ambarella_nand_set_sdr_timing(host);
	}
}
#endif

static int ambarella_nand_probe(struct udevice *dev)
{
	struct ambarella_nand_host *host = dev_get_priv(dev);
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = &chip->mtd;
	int ret;

	/* Get resources */
	host->regbase = (void *)dev_read_addr(dev);
	host->dev = dev;

	/* Enable the clock */

	/* allocate dma buffer */
	host->dmabuf = memalign(ARCH_DMA_MINALIGN, AMBARELLA_NAND_DMA_BUFFER_SIZE);
	if (!host->dmabuf) {
		printf("%s: Aligned buffer alloc failed!!!\n",
				__func__);
		return -ENOMEM;
	}
	host->dmaaddr = (dma_addr_t)host->dmabuf;

	/* Reset */
	ambarella_nand_init_chip(host, dev);

	/* defualt is spinand */
	if (host->is_spinand)
		ret = pinctrl_select_state(dev, "default");
	else
		ret = pinctrl_select_state(dev, "nand");
	if (ret)
		pr_err("%s: select pinctrl error.\n", dev->name);

	nand_set_controller_data(chip, host);

	/* Scan to find existence of the device */
	ret = nand_scan_ident(mtd, CONFIG_SYS_NAND_MAX_CHIPS, NULL);
	if (ret) {
		pr_err("%s: nand scan ident err %d\n",dev->name, ret);
		return ret;
	}

	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	/* it must be SLC nand, because the spinand id parsing could */
	/* be considered as MLC nand, we set it as SLC mandatory */
	if (host->is_spinand)
		chip->bits_per_cell = 1;

	ret = ambarella_nand_init_chipecc(host);
	if (ret) {
		pr_err("%s: init chip err %d\n", dev->name, ret);
		goto exit1;
	}

	ret = ambarella_nand_config_flash(host);
	if (ret) {
		pr_err("%s: config flash err %d\n", dev->name, ret);
		goto exit1;
	}

	ambarella_nand_init_timings(host);

	/* Scan the device to fill MTD data-structures */
	ret = nand_scan_tail(mtd);
	if (ret) {
		pr_err("%s: scan tail err %d\n", dev->name, ret);
		goto exit1;
	}

	ret = nand_register(0, mtd);
	if (ret) {
		dev_err(dev, "Failed to register MTD: %d\n", ret);
		goto exit1;
	}

	debug("%s: Probe done.\n", dev->name);

	return 0;

exit1:
	free(host->dmabuf);
	return ret;
}

static const struct udevice_id ambarella_nand_of_match[] = {
	{ .compatible = "ambarella,nand" },
	{ /* Sentinel */ }
};

U_BOOT_DRIVER(ambarella_nand) = {
	.name = "ambarella_nand",
	.id = UCLASS_MTD,
	.of_match = ambarella_nand_of_match,
	.probe = ambarella_nand_probe,
	.priv_auto_alloc_size = sizeof(struct ambarella_nand_host),
};

void board_nand_init(void)
{
	struct udevice *dev;
	int ret;

	if (rct_system_boot_from() != SYS_CONFIG_BOOT_NAND)
		return ;

	ret = uclass_get_device_by_driver(UCLASS_MTD,
			DM_GET_DRIVER(ambarella_nand),
			&dev);
	if (ret && ret != -ENODEV)
		pr_err("Initialize ambarella NAND controller error %d\n",
				ret);
}
