// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Ambarella International LP
 */

//#define DEBUG

#include <common.h>
#include <dm.h>
#include <log.h>
#include <dm/pinctrl.h>
#include <linux/bitops.h>
#include <asm/io.h>

#define IOMUX_CTRL_OFFSET	0xF0
#define PIN_ID(x)		((x) & 0xFFF)
#define PIN_ALT(x)		(((x) >> 12) & 0xF)

struct ambarella_pinctrl_priv {
	phys_addr_t iomux_base;
};

static void __pinctrl_config(struct udevice *dev, u32 pin, u32 alt)
{
	int i, bank, boff;
	unsigned long reg;
	struct ambarella_pinctrl_priv *priv = dev_get_priv(dev);

	bank = pin / 32;
	boff = pin % 32;

	for (i = 0; i < 3; i++) {

		reg =  priv->iomux_base + 0xC * bank + 0x4 * i;

		if (alt & BIT(i))
			setbits_32(reg, 1 << boff);
		else
			clrbits_32(reg, 1 << boff);
	}

	writel(0x1, priv->iomux_base + IOMUX_CTRL_OFFSET);
	writel(0x0, priv->iomux_base + IOMUX_CTRL_OFFSET);

}

static void __pinctrl_set_state(struct udevice *dev, int offset)
{
	const void *fdt = gd->fdt_blob;
	u32 pinmux[32];
	int len;

	len = fdtdec_get_int_array_count(fdt, offset, "amb,pinmux-ids",
			pinmux, ARRAY_SIZE(pinmux));

	if (len) {
		u32 pin_id, pin_alt;

		for (int i; i < len; i++) {
			pin_id = PIN_ID(pinmux[i]);
			pin_alt = PIN_ALT(pinmux[i]);
			debug("\tPin%d: Alt%d\n", pin_id, pin_alt);
			__pinctrl_config(dev, pin_id, pin_alt);
		}
	}
}

static int ambarella_pinctrl_set_state(struct udevice *dev, struct udevice *config)
{
	debug("%s:\n",config->name);

	__pinctrl_set_state(dev, dev_of_offset(config));

	return 0;
}

static int ambarella_pinctrl_probe(struct udevice *dev)
{
	struct ambarella_pinctrl_priv *priv;

	priv = dev_get_priv(dev);
	if (!priv)
		return -EINVAL;

	priv->iomux_base = dev_read_addr_name(dev, "iomux");
	if (priv->iomux_base == FDT_ADDR_T_NONE)
		return -ENOTSUPP;

	debug("%s Probe done.\n", dev->name);

	return 0;
}

const struct pinctrl_ops ambarella_pinctrl_ops = {
	.set_state			= ambarella_pinctrl_set_state,
};

static const struct udevice_id ambarella_pinctrl_ids[] = {
	{ .compatible = "ambarella,pinctrl" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(pinctrl_ambarella) = {
	.name		= "pinctrl_ambarella",
	.id		= UCLASS_PINCTRL,
	.of_match	= ambarella_pinctrl_ids,
	.priv_auto_alloc_size = sizeof(struct ambarella_pinctrl_priv),
	.ops		= &ambarella_pinctrl_ops,
	.probe		= ambarella_pinctrl_probe,
	.flags		= DM_FLAG_PRE_RELOC,
};
