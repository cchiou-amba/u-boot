// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */
#include <common.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <linux/bitops.h>

extern const struct pinmux_config early_init_pin[];

static int __set_pinmux(unsigned short pin, unsigned short alt)
{
	int i, bank, boff;
	unsigned long reg;

	if (pin >= MAX_GPIO_NUM || alt > 5)
		return -1;

	bank = pin / 32;
	boff = pin % 32;

	for (i = 0; i < 3; i++) {

		reg = IOMUX_BASE + 0xC * bank + 0x4 * i;

		if (alt & BIT(i))
			setbits_32(reg, 1 << boff);
		else
			clrbits_32(reg, 1 << boff);
	}

	writel(0x1, IOMUX_BASE + 0xF0);
	writel(0x0, IOMUX_BASE + 0xF0);

	return 0;
}

int pinmux_config_set_item(const struct pinmux_config *item, int n_item)
{
	int i, rval;

	for (i = 0; i < n_item; i++, item++) {
		rval = __set_pinmux(item->pin, item->alt);
		if (rval)
			return rval;
	}

	return 0;
}
