// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Ambarella International LP
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <linux/delay.h>

const struct pinmux_config cv5_init_pinmux[] = {
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
	pinmux_config_set_item(cv5_init_pinmux, sizeof(cv5_init_pinmux));
}

void plat_f_soc_init(void)
{
	/* NIC400 */
	writel(1, 0x20f1000008);
	writel(1, 0x20f100000c);
	writel(1, 0x20f1000010);
	writel(1, 0x20f1000014);

	writel(1, 0x20f1000018);
	writel(1, 0x20f100001c);
	writel(1, 0x20f1000020);
	writel(1, 0x20f1000024);
	writel(1, 0x20f1000028);
}

void plat_r_reset_cpu(void)
{
	rct_writel(0x068, 0xE);
	dsb();
	rct_writel(0x068, 0xF);
}

void cpu_secondary_init_r(void)
{
	writel(gd->relocaddr, 0x20f2000068);

	writel(0, 0x20f2000028);
}

void plat_device_init(void)
{
	/* USB device */
	rct_writel(0x50, 0x3006);

	setbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	mdelay(1);
	clrbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	mdelay(1);
}
