// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */
//#define DEBUG

#include <common.h>
#include <cpu_func.h>
#include <linux/mtd/spi-nor.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/bitrev.h>
#include <linux/bug.h>
#include <malloc.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <memalign.h>
#include <fdtdec.h>
#include <dm.h>
#include <env.h>
#include <dm/of_access.h>
#include <regmap.h>
#include <syscon.h>
#include <dm/pinctrl.h>
#include <linux/dma-direction.h>
#include <spi_flash.h>

#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include "ambarella-spinor.h"
#include "ambarella_dma.h"

/*TODO, use dm_dma to manager dma controller*/
#define AMBA_DMA0_OFFSET		0xFFE0020000
#define AMBA_DMA_FUNC_SEL		0xE02C
#define AMBA_DMA_CH0_CONTROL_OFFSET	0x300
#define AMBA_DMA_CH0_SRC_ADDR_OFFSET	0x304
#define AMBA_DMA_CH0_DST_ADDR_OFFSET	0x308
#define AMBA_DMA_CH0_STATUS_OFFSET	0x30C
#define AMBA_DMA_CH1_CONTROL_OFFSET	0x310
#define AMBA_DMA_CH1_SRC_ADDR_OFFSET	0x314
#define AMBA_DMA_CH1_DST_ADDR_OFFSET	0x318
#define AMBA_DMA_CH1_STATUS_OFFSET	0x31C

#define SPINOR_CMD_TIMEOUT		1000
#define SPINOR_BLK_DMA_SIZE		32
#define AMBARELLA_SPINOR_DMA_BUFFER_SIZE 8192

struct spinor_ctrl {
	u32 len;
	u8 *buf;
	u8 lane;
	u8 is_dtr : 1;
	u8 is_read : 1; /* following are only avaiable for data */
	u8 is_io : 1;
	u8 is_dma : 1;
};

struct ambarella_spinor {
	struct spi_nor	*chip;
	struct udevice	*dev;
	void __iomem	*regbase;
	void __iomem	*dmabase;

	dma_addr_t	dmaaddr;
	u8		*dmabuf;
};

extern int spi_flash_mtd_register(struct spi_flash *flash);

#if !CONFIG_IS_ENABLED(SYS_DCACHE_OFF)
static void amba_spinor_invalidate_cache(struct ambarella_spinor *amba_spinor, u32 len)
{
	u32 data_start = amba_spinor->dmaaddr;
	u32 data_end = data_start + roundup(len, ARCH_DMA_MINALIGN);

	invalidate_dcache_range(data_start, data_end);
}

static void amba_spinor_flush_cache(struct ambarella_spinor *amba_spinor, u32 len)
{
	u32 data_start = amba_spinor->dmaaddr;
	u32 data_end = data_start + roundup(len, ARCH_DMA_MINALIGN);

	flush_dcache_range(data_start, data_end);
}
#else
static void amba_spinor_invalidate_cache(struct ambarella_spinor *amba_spinor, u32 len){}
static void amba_spinor_flush_cache(struct ambarella_spinor *amba_spinor, u32 len){}
#endif

static int amba_spinor_dma_transfer(struct ambarella_spinor *amba_spinor,
		u32 len, enum dma_data_direction dir)
{
	int ret = 0;
	u32 control = 0;
	u32 src_addr = 0;
	u32 dst_addr = 0;
	u32 rxtx_enable = 0;

	debug("dma tr(%#.2x): len:%x\n", dir, len);

	if (dir == DMA_FROM_DEVICE){
		//firstly invalidate data cache
		amba_spinor_invalidate_cache(amba_spinor, len);

		/* Set dma0 ch1  control + src + dest */
		control = DMA_CHANX_CTR_EN | DMA_CHANX_CTR_WM | DMA_CHANX_CTR_NI |
			DMA_CHANX_CTR_BLK_32B | DMA_CHANX_CTR_TS_4B | len;
		src_addr = (u32)(uintptr_t)amba_spinor->regbase + SPINOR_RXDATA_OFFSET;
		dst_addr = amba_spinor->dmaaddr;
		rxtx_enable = SPINOR_DMACTRL_RXEN;
		writel(src_addr, amba_spinor->dmabase + AMBA_DMA_CH1_SRC_ADDR_OFFSET);
		writel(dst_addr, amba_spinor->dmabase + AMBA_DMA_CH1_DST_ADDR_OFFSET);
		writel(control, amba_spinor->dmabase + AMBA_DMA_CH1_CONTROL_OFFSET);
	} else {
		//firstly flush data cache
		amba_spinor_flush_cache(amba_spinor, len);

		/* Set dma0 ch0  control + src + dest */
		control = DMA_CHANX_CTR_EN | DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI |
			DMA_CHANX_CTR_BLK_32B | DMA_CHANX_CTR_TS_4B | len;
		src_addr = amba_spinor->dmaaddr;
		dst_addr = (u32)(uintptr_t)amba_spinor->regbase + SPINOR_TXDATA_OFFSET;
		rxtx_enable = SPINOR_DMACTRL_TXEN;
		writel(src_addr, amba_spinor->dmabase + AMBA_DMA_CH0_SRC_ADDR_OFFSET);
		writel(dst_addr, amba_spinor->dmabase + AMBA_DMA_CH0_DST_ADDR_OFFSET);
		writel(control, amba_spinor->dmabase + AMBA_DMA_CH0_CONTROL_OFFSET);
	}

	debug("setting DMA control reg val = %x\n", control);
	debug("ch0 src_addr reg val = %x\n", readl(amba_spinor->dmabase + AMBA_DMA_CH0_SRC_ADDR_OFFSET));
	debug("ch0 dest_addr reg val = %x\n", readl(amba_spinor->dmabase + AMBA_DMA_CH0_DST_ADDR_OFFSET));
	debug("ch1 src_addr reg val = %x\n", readl(amba_spinor->dmabase + AMBA_DMA_CH1_SRC_ADDR_OFFSET));
	debug("ch1 dest_addr reg val = %x\n", readl(amba_spinor->dmabase + AMBA_DMA_CH1_DST_ADDR_OFFSET));
	writel(rxtx_enable, amba_spinor->regbase + SPINOR_DMACTRL_OFFSET);

	return ret;
}

static int amba_spinor_send_cmd(struct ambarella_spinor *amba_spinor,
		struct spinor_ctrl *cmd, struct spinor_ctrl *addr,
		struct spinor_ctrl *data, struct spinor_ctrl *dummy)
{
	u32 reg_length = 0, reg_ctrl = 0, val = 0, i = 0, done = 0;

	/* setup basic info */
	if (cmd != NULL) {
		reg_length |= SPINOR_LENGTH_CMD(cmd->len);

		reg_ctrl |= cmd->is_dtr ? SPINOR_CTRL_CMDDTR : 0;
		switch(cmd->lane) {
		case 8:
			reg_ctrl |= SPINOR_CTRL_CMD8LANE;
			break;
		case 4:
			reg_ctrl |= SPINOR_CTRL_CMD4LANE;
			break;
		case 2:
			reg_ctrl |= SPINOR_CTRL_CMD2LANE;
			break;
		case 1:
		default:
			reg_ctrl |= SPINOR_CTRL_CMD1LANE;
			break;
		}
	} else {
		return -EINVAL;
	}

	if (addr != NULL) {
		reg_length |= SPINOR_LENGTH_ADDR(addr->len);

		reg_ctrl |= addr->is_dtr ? SPINOR_CTRL_ADDRDTR : 0;
		switch(addr->lane) {
		case 4:
			reg_ctrl |= SPINOR_CTRL_ADDR4LANE;
			break;
		case 2:
			reg_ctrl |= SPINOR_CTRL_ADDR2LANE;
			break;
		case 1:
		default:
			reg_ctrl |= SPINOR_CTRL_ADDR1LANE;
			break;
		}
	}

	if (data != NULL) {
		if (data->len > SPINOR_MAX_DATA_LENGTH) {
			dev_err(amba_spinor->dev, "spinor: data length is too large.\n");
			return -ENOMEM;
		}

		debug("data->len = 0x%x\n", data->len);
		reg_length |= SPINOR_LENGTH_DATA(data->len);

		reg_ctrl |= data->is_dtr ? SPINOR_CTRL_DATADTR : 0;
		switch(data->lane) {
		case 8:
			reg_ctrl |= SPINOR_CTRL_DATA8LANE;
			break;
		case 4:
			reg_ctrl |= SPINOR_CTRL_DATA4LANE;
			break;
		case 2:
			reg_ctrl |= SPINOR_CTRL_DATA2LANE;
			break;
		case 1:
		default:
			reg_ctrl |= SPINOR_CTRL_DATA1LANE;
			break;
		}

		if (data->is_read)
			reg_ctrl |= SPINOR_CTRL_RDEN;
		else
			reg_ctrl |= SPINOR_CTRL_WREN;

		if (!data->is_io)
			reg_ctrl |= SPINOR_CTRL_RXLANE_TXRX;

		if (data->is_dma && data->is_read && data->len < SPINOR_BLK_DMA_SIZE)
			data->is_dma = 0;
	}

	if (dummy != NULL) {
		reg_length |= SPINOR_LENGTH_DUMMY(dummy->len);
		reg_ctrl |= dummy->is_dtr ? SPINOR_CTRL_DUMMYDTR : 0;
	}

	debug("reg_length = 0x%x\n", reg_length);
	writel(reg_length, amba_spinor->regbase + SPINOR_LENGTH_OFFSET);
	writel(reg_ctrl, amba_spinor->regbase + SPINOR_CTRL_OFFSET);

	/* setup cmd id */
	val = 0;
	for (i = 0; i < cmd->len; i++)
		val |= (cmd->buf[i] << (i << 3));
	writel(val, amba_spinor->regbase + SPINOR_CMD_OFFSET);

	/* setup address */
	if (addr) {
		val = 0;
		for (i = 0; i < addr->len; i++) {
			if (i >= 4)
				break;
			val |= (addr->buf[i] << (i << 3));
		}
		writel(val, amba_spinor->regbase + SPINOR_ADDRLO_OFFSET);

		debug("addrlo = 0x%x\t", val);
		val = 0;
		for (; i < addr->len; i++)
			val |= (addr->buf[i] << ((i - 4) << 3));
		writel(val, amba_spinor->regbase + SPINOR_ADDRHI_OFFSET);
		debug("addrhi = 0x%x\n", val);
	}
	/* set dmactrl usage 0 as default */
	writel(0, amba_spinor->regbase + SPINOR_DMACTRL_OFFSET);

	/* setup dma if data phase is existed and dma is required.
	 * Note: for READ, dma will just transfer the length multiple of
	 * 32Bytes, the residual data in FIFO need to be read manually. */
	if (data != NULL && data->is_dma) {
		if (data->is_read) {
			amba_spinor_dma_transfer(amba_spinor, data->len, DMA_FROM_DEVICE);
		} else {
			amba_spinor_dma_transfer(amba_spinor, data->len, DMA_TO_DEVICE);
		}
	} else if (data != NULL && !data->is_read) {
		if (data->len > 0x100) {
			dev_err(amba_spinor->dev, "spinor: tx length exceeds fifo size.\n");
			return -1;
		}
		for (i = 0; i < data->len; i++) {
			writeb(data->buf[i], amba_spinor->regbase + SPINOR_TXDATA_OFFSET);
		}
	}

	/* start tx/rx transaction */
	writel(0x1, amba_spinor->regbase + SPINOR_START_OFFSET);

	while (data != NULL && data->is_dma) {
		/* get dma data transfer done, and spi-nor data transfer done */
		if(data->is_read)
			val = readl(amba_spinor->dmabase + AMBA_DMA_CH1_STATUS_OFFSET) & DMA_CHANX_STA_DN;
		else
			val = readl(amba_spinor->dmabase + AMBA_DMA_CH0_STATUS_OFFSET) & DMA_CHANX_STA_DN;

		debug("dma status reg: %x, default = %x\n", val, DMA_CHANX_STA_DN);
		if (val == DMA_CHANX_STA_DN) {
			done = readl(amba_spinor->regbase + SPINOR_RAWINTR_OFFSET) & SPINOR_INTR_DATALENREACH;
			if (done == SPINOR_INTR_DATALENREACH) {
				/* clear dma status reg and spinor intr reg */
				writel(0, amba_spinor->dmabase + AMBA_DMA_CH0_STATUS_OFFSET);
				writel(0, amba_spinor->dmabase + AMBA_DMA_CH1_STATUS_OFFSET);
				writel(SPINOR_INTR_ALL, amba_spinor->regbase + SPINOR_CLRINTR_OFFSET);
				break;
			}
		} else
			debug("dma data transfer not finish.\n");
	}

	if (data->is_read){
		//read data need invalidate data from dcache to ram
		amba_spinor_invalidate_cache(amba_spinor, data->len);
	}

	/* for cmd data done */
	while(!done) {
		done = readl(amba_spinor->regbase + SPINOR_RAWINTR_OFFSET) & SPINOR_INTR_DATALENREACH;
	}

	writel(SPINOR_INTR_ALL, amba_spinor->regbase + SPINOR_CLRINTR_OFFSET);

	return 0;
}

static ssize_t amba_spinor_read(struct spi_nor *nor, loff_t from, size_t len,
			      u_char *buf)
{
	struct ambarella_spinor *amba_spinor = nor->priv;
	struct spinor_ctrl cmd;
	struct spinor_ctrl addr;
	struct spinor_ctrl data;
	struct spinor_ctrl dummy;
	int ret;
	int i;
	u32 tail, bulk, dma_blk;
	u8 cmd_id, is_dma, data_lane;
	size_t offset = 0;
	size_t remain;
	loff_t trans_addr;

	dev_dbg(amba_spinor->dev, "read(%#.2x): buf:%p from:%#.8x len:%#zx\n",
			nor->read_opcode, buf, (u32)from, len);

	dma_blk = len / AMBARELLA_SPINOR_DMA_BUFFER_SIZE;
	bulk = len / SPINOR_BLK_DMA_SIZE;

	memset(&cmd, 0, sizeof(cmd));
	memset(&addr, 0, sizeof(addr));
	memset(&data, 0, sizeof(data));
	memset(&dummy, 0, sizeof(dummy));

	cmd_id = nor->read_opcode;
	cmd.buf = &cmd_id;
	cmd.len = 1;
	cmd.lane = 1;

	addr.buf = (u8*)&from;
	addr.len = nor->addr_width;
	addr.lane = 1;
	addr.is_dtr = 0;

	is_dma = bulk ? 1 : 0;

	data.buf = buf;
	data_lane = spi_nor_get_protocol_data_nbits(nor->read_proto);
	data.lane = max_t(u8, data_lane, 2);
	data.is_dtr = 0;
	data.is_read = 1;
	data.is_io = (data_lane == 1) ? 0 : 1;
	data.is_dma = is_dma;

	dummy.len = nor->read_dummy;
	dummy.is_dtr = 0;

	if (dma_blk) {
		for (i = 0; i < dma_blk; i++) {
			debug("index = %d,dma_blk = %d\n", i, dma_blk);
			offset = i * AMBARELLA_SPINOR_DMA_BUFFER_SIZE;
			trans_addr = from + offset;
			addr.buf = (u8*)&trans_addr;
			data.len = AMBARELLA_SPINOR_DMA_BUFFER_SIZE;

			ret = amba_spinor_send_cmd(amba_spinor, &cmd, &addr, &data, &dummy);
			if (ret)
				return ret;

			memcpy(buf + offset, amba_spinor->dmabuf, AMBARELLA_SPINOR_DMA_BUFFER_SIZE);
		}
	}

	offset = dma_blk * AMBARELLA_SPINOR_DMA_BUFFER_SIZE;
	remain = len - offset;

	tail = remain % SPINOR_BLK_DMA_SIZE;
	bulk = remain / SPINOR_BLK_DMA_SIZE;

	if (remain) {
		if (bulk) {
			trans_addr = from + offset;
			addr.buf = (u8*)&trans_addr;
			data.len = remain - tail;
			data.is_dma = 1;

			ret = amba_spinor_send_cmd(amba_spinor, &cmd, &addr, &data, &dummy);

			if (ret)
				return ret;

			if (bulk)
				memcpy(buf + offset, amba_spinor->dmabuf, remain-tail);

		}
		offset += bulk * SPINOR_BLK_DMA_SIZE;

		if (tail) {
			trans_addr = from + offset;
			addr.buf = (u8*)&trans_addr;
			data.len = tail;
			data.is_dma = 0;

			ret = amba_spinor_send_cmd(amba_spinor, &cmd, &addr, &data, &dummy);
			if (ret)
				return ret;

			for (i = 0; i < tail; i++){
				*(buf+offset+i) = readb(amba_spinor->regbase + SPINOR_RXDATA_OFFSET);
			}
		}
	}
	return len;
}

/* write function need to consider the page bound issue or not */
/* mtd layer should consider and handle the page bound issue   */
/* So it may could ignore this */
static ssize_t amba_spinor_write(struct spi_nor *nor, loff_t to, size_t len,
			       const u_char *buf)
{
	struct ambarella_spinor *amba_spinor = nor->priv;
	struct spinor_ctrl cmd;
	struct spinor_ctrl addr;
	struct spinor_ctrl data;

	int ret, i;
	u32 tail = 0;
	u8 cmd_id;

	dev_dbg(amba_spinor->dev, "write(%#.2x): buf:%p to:%#.8x len:%#zx\n",
		nor->program_opcode, buf, (u32)to, len);

	i = to % (nor->page_size);
	if ((i + len) > nor->page_size)
		dev_err(amba_spinor->dev, "write data exceed the page bound \n");
	BUG_ON((i + len) > nor->page_size);

	tail = len % SPINOR_BLK_DMA_SIZE;

	memset(&cmd, 0, sizeof(cmd));
	memset(&addr, 0, sizeof(addr));
	memset(&data, 0, sizeof(data));

	cmd_id = nor->program_opcode;
	cmd.buf = &cmd_id;
	cmd.len = 1;
	cmd.lane = 1;

	addr.buf = (u8*)&to;
	addr.len = nor->addr_width;
	addr.lane = 1;
	addr.is_dtr = 0;

	data.buf = (void *)buf;
	data.len = len;
	data.lane = 1;
	data.is_dtr = 0;
	data.is_read = 0;
	data.is_io = 0;
	data.is_dma = tail ? 0 : 1;

	if (tail == 0)
		memcpy(amba_spinor->dmabuf, buf, len);

	ret = amba_spinor_send_cmd(amba_spinor, &cmd, &addr, &data, NULL);
	if (ret)
		return ret;

	return len;
}

static int amba_spinor_erase(struct spi_nor *nor, loff_t offs)
{
	struct ambarella_spinor *amba_spinor = nor->priv;
	struct spinor_ctrl cmd;
	struct spinor_ctrl addr;
	u8 cmd_id;
	int ret;

	dev_dbg(amba_spinor->dev, "erase(%#.2x): to:%#.8x \n",
		nor->erase_opcode, (u32)offs);

	cmd_id = nor->erase_opcode;
	cmd.buf = &cmd_id;
	cmd.len = 1;
	cmd.lane = 1;
	cmd.is_dtr = 0;

	addr.buf = (u8 *)&offs;
	addr.len = nor->addr_width;
	addr.lane = 1;
	addr.is_dtr = 0;

	ret = amba_spinor_send_cmd(amba_spinor, &cmd, &addr, NULL, NULL);
	return ret;
}

static int amba_spinor_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct ambarella_spinor *amba_spinor = nor->priv;
	struct spinor_ctrl cmd, data;
	int i, rval;

	dev_dbg(amba_spinor->dev, "read_reg(%#.2x): buf:%p len:%d\n", opcode, buf, len);

	cmd.buf = &opcode;
	cmd.len = 1;
	cmd.lane = 1;
	cmd.is_dtr = 0;

	data.buf = buf;
	data.len = len;
	data.lane = 2;
	data.is_read = 1;
	data.is_io = 0;
	data.is_dtr = 0;
	data.is_dma = 0;

	rval = amba_spinor_send_cmd(amba_spinor, &cmd, NULL, &data, NULL);
	if (rval < 0)
		return rval;

	for (i = 0; i < len; i++) {
		buf[i] = readb(amba_spinor->regbase + SPINOR_RXDATA_OFFSET);
		debug("buf[%d] = %02x\n", i, buf[i]);
	}

	return 0;
}

static int amba_spinor_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct ambarella_spinor *amba_spinor = nor->priv;
	struct spinor_ctrl cmd, data;
	int rval;

	dev_dbg(amba_spinor->dev, "write_reg(%#.2x): buf:%p len:%d\n", opcode, buf, len);

	cmd.buf = &opcode;
	cmd.len = 1;
	cmd.lane = 1;
	cmd.is_dtr = 0;

	data.buf = (void *)buf;
	data.len = len;
	data.lane = 1;
	data.is_dtr = 0;
	data.is_read = 0;
	data.is_io = 0;
	data.is_dma = 0;

	rval = amba_spinor_send_cmd(amba_spinor, &cmd, NULL, &data, NULL);
	if (rval < 0)
		return rval;

	return 0;
}

static void amba_spinor_init(struct ambarella_spinor *amba_spinor)
{
	u32 rval;

	writel(SPINOR_INTR_ALL, amba_spinor->regbase + SPINOR_INTRMASK_OFFSET);

	writel(SPINOR_INTR_ALL,	amba_spinor->regbase + SPINOR_CLRINTR_OFFSET);

	/* reset the FIFO */
	writel(0x1, amba_spinor->regbase + SPINOR_TXFIFORST_OFFSET);
	writel(0x1, amba_spinor->regbase + SPINOR_RXFIFORST_OFFSET);

	/* after reset fifo, the 0x28 will become 0x10,
	 * so , read REG200 times to clear the 0x28,  this is a bug in hardware
	 */
	while (readl(amba_spinor->regbase + SPINOR_RXFIFOLV_OFFSET) != 0) {
		rval = readl(amba_spinor->regbase + SPINOR_RXDATA_OFFSET);
	}

	writel(31, amba_spinor->regbase + SPINOR_TXFIFOTHLV_OFFSET);
	writel(31, amba_spinor->regbase + SPINOR_RXFIFOTHLV_OFFSET);
}

static int amba_spinor_setup_dma(struct ambarella_spinor *amba_spinor)
{
	u32 val = 0;

	/* We can only either use DMA for both TX and RX or not use it at all */
	/* Set DMA0 ch0 tx, ch1 rx */
	val = 5 | (6 << 8);
	writel(val, amba_spinor->dmabase + AMBA_DMA_FUNC_SEL);

	val = readl(amba_spinor->dmabase + AMBA_DMA_FUNC_SEL);

	debug("DMA0 FUNC ADDR val = %08x\n", val);
	return 0;
}

static int ambarella_spinor_probe(struct udevice *dev)
{
	struct ambarella_spinor *host = dev_get_priv(dev);
	struct spi_nor *chip = dev_get_uclass_priv(dev);
	int ret;

	/* Get resources */
	host->regbase = (void *)dev_read_addr(dev);
	host->dmabase = (void *)AMBA_DMA0_OFFSET;
	host->chip = chip;
	chip->dev = dev;
	chip->priv = (void *)host;

	/* TODO setting clk, use the bst default clk now */

	/* allocate dma buffer */
	host->dmabuf = memalign(ARCH_DMA_MINALIGN, AMBARELLA_SPINOR_DMA_BUFFER_SIZE);
	if (!host->dmabuf) {
		debug("%s: Aligned buffer alloc failed!!!\n",
				__func__);
		return -ENOMEM;
	}
	host->dmaaddr = (dma_addr_t)host->dmabuf;

	/* defualt is spinor */
	ret = pinctrl_select_state(dev, "default");
	if (ret)
		pr_err("%s: select pinctrl error.\n", dev->name);

	amba_spinor_init(host);
	amba_spinor_setup_dma(host);

	chip->read = amba_spinor_read;
	chip->write = amba_spinor_write;
	chip->erase = amba_spinor_erase;
	chip->read_reg = amba_spinor_read_reg;
	chip->write_reg = amba_spinor_write_reg;

	ret = spi_nor_scan(chip);
	if(ret)
		goto exit1;

	if(CONFIG_IS_ENABLED(SPI_FLASH_MTD)) {
		ret = spi_flash_mtd_register(chip);
		if (ret) {
			printf("spi_flash_mtd_register failed: %d\n", ret);
			goto exit1;
		}
		debug("spi_flash_mtd_register.\n");
	}

	printf("%s: Probe done.\n", dev->name);

	return 0;

exit1:
	free(host->dmabuf);
	return ret;
}

static int ambarella_spinor_read(struct udevice *dev, u32 offset, size_t len, void *buf)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return log_ret(mtd->_read(mtd, offset, len, &retlen, buf));
}

static int ambarella_spinor_write(struct udevice *dev, u32 offset, size_t len, const void *buf)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return mtd->_write(mtd, offset, len, &retlen, buf);
}

static int ambarella_spinor_erase(struct udevice *dev, u32 offset, size_t len)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	struct erase_info instr;

	if (offset % mtd->erasesize || len % mtd->erasesize) {
		debug("SF: Erase offset/length not multiple of erase size\n");
		return -EINVAL;
	}

	memset(&instr, 0, sizeof(instr));
	instr.addr = offset;
	instr.len = len;

	return mtd->_erase(mtd, &instr);
}

static const struct dm_spi_flash_ops ambarella_spinor_ops = {
	.read = ambarella_spinor_read,
	.write = ambarella_spinor_write,
	.erase = ambarella_spinor_erase,
};

static const struct udevice_id ambarella_spinor_of_match[] = {
	{ .compatible = "ambarella,spinor" },
	{ /* Sentinel */ }
};

U_BOOT_DRIVER(ambarella_spinor) = {
	.name = "ambarella_spinor",
	.id = UCLASS_SPI_FLASH,
	//.id = UCLASS_MTD,
	.of_match = ambarella_spinor_of_match,
	.probe = ambarella_spinor_probe,
	.priv_auto_alloc_size = sizeof(struct ambarella_spinor),
	.ops = &ambarella_spinor_ops,
	.flags = DM_FLAG_PRE_RELOC,
};

int flash_init(void)
{
	struct udevice *dev;
	int ret;
	int size = 0;

//	if (rct_system_boot_from() != SYS_CONFIG_BOOT_SPINOR)
//		return 0;

	debug("begin flash_init.\n");
	ret = uclass_get_device_by_driver(UCLASS_SPI_FLASH,
			DM_GET_DRIVER(ambarella_spinor),
			&dev);
	if (ret && ret != -ENODEV)
		debug("Initialize ambarella spinor controller error %d\n", ret);

	return size;
}
