// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Ambarella International LP
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <linux/bitops.h>

#define PLL_SCALER_JDIV(x)			(((x >> 4) & 0xF) + 1)
#define REF_CLK_FREQ			24000000UL

#define PLL_CORE_CTRL_OFFSET         0x0
#define PLL_CORE_CTRL2_OFFSET         0x100
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
