// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <asm/io.h>
#include <asm/spin_table.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>

static const struct pinmux_config cv22_init_pin[] = {
	/* UART APB */
#ifdef CONFIG_DEBUG_UART
	{39, 1}, {40, 1},
#endif
};

void plat_f_clk_config(void)
{
	/* UART APB divider */
	rct_writel(0x038, 1);
}

void plat_f_pinmux_config(void)
{
	pinmux_config_set_item(cv22_init_pin, sizeof(cv22_init_pin));
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

