// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Ambarella International LP
 */

#include <command.h>
#include <common.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#define PLL_SCALER_JDIV(x)			(((x >> 4) & 0xF) + 1)
#define REF_CLK_FREQ			24000000UL

#define PLL_CORE_CTRL_OFFSET         0x0
#define PLL_CORE_CTRL2_OFFSET         0x100

#define PLL_SD_CTRL_OFFSET		0x4AC
#define PLL_SD_FRAC_OFFSET		0x4B0
#define PLL_SD_CTRL2_OFFSET		0x4B4
#define PLL_SD_CTRL3_OFFSET		0x4B8

#define SCALER_SD0_OFFSET		0x00C
#define SCALER_SD1_OFFSET		0x430
#define SCALER_SD2_OFFSET		0x434

#define SCALER_SD0_REG			RCT_REG(SCALER_SD0_OFFSET)
#define SCALER_SD1_REG			RCT_REG(SCALER_SD1_OFFSET)
#define SCALER_SD2_REG			RCT_REG(SCALER_SD2_OFFSET)

#define SCALER_SD_REG(id)		((id == 0) ? SCALER_SD0_REG : \
					 (id == 1) ? SCALER_SD1_REG : SCALER_SD2_REG)
/* ==========================================================================*/
void rct_writel(unsigned long reg, unsigned int val)
{
	writel(val, RCT_BASE + reg);
}

unsigned int rct_readl(unsigned long reg)
{
	return readl(RCT_BASE + reg);
}

int rct_system_config(void)
{
	return rct_readl(SYS_CONFIG_OFFSET);
}

int rct_system_boot_from(void)
{
	return rct_system_config() & SYS_CONFIG_BOOT_MASK;
}

#if 0
u32 rct_get_emmc_poc(void)
{
	u32 poc = rct_system_config();
	u32 ret_val = RCT_BOOT_EMMC_AUTO;

	if (poc & SYS_CONFIG_MMC_HS)
		ret_val |= RCT_BOOT_EMMC_HS;

	if (poc & SYS_CONFIG_MMC_DDR)
		ret_val |= RCT_BOOT_EMMC_DDR;

	if (poc & SYS_CONFIG_MMC_4BIT)
		ret_val |= RCT_BOOT_EMMC_4BIT;

	if (poc & SYS_CONFIG_MMC_8BIT)
		ret_val |= RCT_BOOT_EMMC_8BIT;

	if (poc & SYS_CONFIG_MMC_SDXC)
		ret_val |= RCT_BOOT_EMMC_SDXC;

	return ret_val;
}
#endif
/* ==========================================================================*/
static u64 rct_get_integer_pll_freq(u32 ctrl, u32 ctrl2, u32 pres, u32 posts)
{
	u32 ctrl2_8, ctrl2_9, ctrl2_11, ctrl2_12;
	u32 intp, sout, sdiv;
	u64 fvco, freq;

	if(pres == 0 || posts == 0) {
		printf("pll divider is zero \n");
		while(1);
	}

	if (ctrl & 0x20)
		return 0;

	if (ctrl & 0x00200004) {
		intp = REF_CLK_FREQ;
		intp /= pres;
		intp /= posts;
		return intp;
	}

	ctrl2_12 = ((ctrl2 >> 12) & 0x1);
	ctrl2_11 = ((ctrl2 >> 11) & 0x1) + 1;
	ctrl2_9 = ((ctrl2 >> 9) & 0x1) + 1;
	ctrl2_8 = ((ctrl2 >> 8) & 0x1) + 1;

	intp = ((ctrl >> 24) & 0x7f) + 1;
	sout = ((ctrl >> 16) & 0xf) + 1;
	sdiv = ((ctrl >> 12) & 0xf) + 1;

	fvco = REF_CLK_FREQ * ctrl2_8 * ctrl2_9 * sdiv * intp;

	if (ctrl2_12)
		freq = fvco;
	else
		freq = fvco / ctrl2_8 / ctrl2_11 / sout;

	return freq / posts;
}

/* ==========================================================================*/
u32 get_core_bus_freq_hz(void)
{
	return rct_get_integer_pll_freq(rct_readl(PLL_CORE_CTRL_OFFSET), rct_readl(PLL_CORE_CTRL2_OFFSET), 1, 1);
}

u32 get_ahb_bus_freq_hz(void)
{
	return (get_core_bus_freq_hz() >> 1);
}

u32 get_apb_bus_freq_hz(void)
{
	return get_ahb_bus_freq_hz() >> 1;
}

/* ==========================================================================*/
void rct_set_sd_pll(int slot, u32 freq_hz)
{
	u32 parent_rate, divider, rval;

	parent_rate = rct_get_integer_pll_freq(rct_readl(PLL_SD_CTRL_OFFSET),
							rct_readl(PLL_SD_CTRL2_OFFSET), 1, 1);
	divider = (parent_rate + freq_hz - 1) / freq_hz;
	rval = min(divider, 0xffffu);
	writel(rval, SCALER_SD_REG(slot));
}

u32 get_sd_freq_hz(int slot)
{
	return rct_get_integer_pll_freq(rct_readl(PLL_SD_CTRL_OFFSET),
							rct_readl(PLL_SD_CTRL2_OFFSET), 1, readl(SCALER_SD_REG(slot)));
}
