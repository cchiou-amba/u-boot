// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Ambarella International LP
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <linux/bitops.h>

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
