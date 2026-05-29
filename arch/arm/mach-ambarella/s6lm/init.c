// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <linux/delay.h>

const struct pinmux_config s6lm_init_pinmux[] = {
	/* UART APB */
#ifdef CONFIG_DEBUG_UART
	{10, 1}, {11, 1},
#endif
};

void plat_f_clk_config(void)
{
	/* UART APB divider */
	rct_writel(0x038, 1);
}

void plat_f_pinmux_config(void)
{
	pinmux_config_set_item(s6lm_init_pinmux, sizeof(s6lm_init_pinmux));
}

void plat_f_soc_init(void)
{
	writel(1, 0xf1000008); /* Non-Secure AHB */
	writel(1, 0xf100000c); /* Secure AHB */
	writel(1, 0xf1000010); /* AXI Config */
	writel(1, 0xf1000014); /* GIC */

	writel(0, 0xf2000090);
	writel(0, 0xf2000094);
	writel(0, 0xf2000098);
}

void plat_r_reset_cpu(void)
{
	rct_writel(0x068, 0xE);
	dsb();
	rct_writel(0x068, 0xF);
}

void cpu_secondary_init_r(void)
{
	writel(gd->relocaddr, 0xf2000068);
	writel(gd->relocaddr, 0xf200006C);
	writel(gd->relocaddr, 0xf2000070);

	writel(0, 0xf2000028);
}

void plat_device_init(void)
{ 
	/* USB device */
	rct_writel(0x50, 0x3006);
	rct_writel(0x2cc, 0x2);
	mdelay(1);
	rct_writel(0x2cc, 0x0);
	mdelay(1);
}
