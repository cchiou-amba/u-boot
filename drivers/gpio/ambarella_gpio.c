// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <dm.h>
#include <log.h>
#include <fdtdec.h>
#include <malloc.h>
#include <linux/bug.h>
#include <dm/of.h>
#include <asm/io.h>
#include <asm/gpio.h>

#define GPIO_DATA_OFFSET                0x00
#define GPIO_DIR_OFFSET                 0x04
#define GPIO_IS_OFFSET                  0x08
#define GPIO_IBE_OFFSET                 0x0c
#define GPIO_IEV_OFFSET                 0x10
#define GPIO_IE_OFFSET                  0x14
#define GPIO_AFSEL_OFFSET               0x18
#define GPIO_RIS_OFFSET                 0x1c
#define GPIO_MIS_OFFSET                 0x20
#define GPIO_IC_OFFSET                  0x24
#define GPIO_MASK_OFFSET                0x28
#define GPIO_ENABLE_OFFSET              0x2c

#define GPIO_HIGH			1
#define GPIO_LOW			0

#define GPIO_FUNC_SW_INPUT		0
#define GPIO_FUNC_SW_OUTPUT		1
#define GPIO_FUNC_HW			2

#define GPIO_MAX_BANK_NUM		8
#define GPIO_MAX_PIN_NUM		(GPIO_MAX_BANK_NUM * 32)

#define IOMUX_CTRL_SET_OFFSET           0xf0
#define IOMUX_REG_OFFSET(b, n)          (((b) * 0xc) + ((n) * 4))

#define PINID_TO_BANK(p)        ((p) >> 5)
#define PINID_TO_OFFSET(p)      ((p) & 0x1f)

static int ambarella_gpio_set_value(struct udevice *dev, unsigned gpio, int value);

struct ambarella_gpio_priv {
	phys_addr_t gpio_base[GPIO_MAX_BANK_NUM];
	phys_addr_t iomux_base;
	unsigned int bank_num;
};

static int ambarella_gpio_set_direction(struct udevice *dev, unsigned gpio, bool input)
{
	uint32_t bank, offset, i;
	uintptr_t base;
	struct ambarella_gpio_priv *priv = dev_get_priv(dev);

	bank = PINID_TO_BANK(gpio);
	offset = PINID_TO_OFFSET(gpio);
	base = priv->gpio_base[bank];

	if (input)
		clrbits_32(base + GPIO_DIR_OFFSET, BIT(offset));
	else
		setbits_32(base + GPIO_DIR_OFFSET, BIT(offset));

	clrbits_32(base + GPIO_AFSEL_OFFSET, BIT(offset));
	setbits_32(base + GPIO_MASK_OFFSET, BIT(offset));

	/* Configure iomux to make sure the pin in GPIO mode */
	for (i = 0; i < 3; i++)
		clrbits_32(priv->iomux_base + IOMUX_REG_OFFSET(bank, i), BIT(offset));

	writel(0x1, priv->iomux_base + IOMUX_CTRL_SET_OFFSET);
	writel(0x0, priv->iomux_base + IOMUX_CTRL_SET_OFFSET);

	return 0;
}

static int ambarella_gpio_direction_input(struct udevice *dev, unsigned gpio)
{
	ambarella_gpio_set_direction(dev, gpio, 1);

	return 0;
}

static int ambarella_gpio_direction_output(struct udevice *dev, unsigned gpio,
		int value)
{
	ambarella_gpio_set_direction(dev, gpio, 0);
	ambarella_gpio_set_value(dev, gpio, value);

	return 0;
}

static int ambarella_gpio_get_value(struct udevice *dev, unsigned gpio)
{
	uint32_t bank, offset;
	uintptr_t base;
	struct ambarella_gpio_priv *priv = dev_get_priv(dev);

	bank = PINID_TO_BANK(gpio);
	offset = PINID_TO_OFFSET(gpio);
	base = priv->gpio_base[bank];

	return !!(readl(base + GPIO_DATA_OFFSET) & BIT(offset));
}

static int ambarella_gpio_set_value(struct udevice *dev, unsigned gpio, int value)
{
	uint32_t bank, offset;
	uintptr_t base;
	struct ambarella_gpio_priv *priv = dev_get_priv(dev);

	bank = PINID_TO_BANK(gpio);
	offset = PINID_TO_OFFSET(gpio);
	base = priv->gpio_base[bank];

	if (value == 0)
		clrbits_32(base + GPIO_DATA_OFFSET, BIT(offset));
	else
		setbits_32(base + GPIO_DATA_OFFSET, BIT(offset));

	return 0;
}

static int ambarella_gpio_get_function(struct udevice *dev, unsigned int gpio)
{
	uint32_t bank, offset;
	uintptr_t base;
	struct ambarella_gpio_priv *priv = dev_get_priv(dev);
	int i, ret, af_num = 0;;

	bank = PINID_TO_BANK(gpio);
	offset = PINID_TO_OFFSET(gpio);
	base = priv->gpio_base[bank];

	for (i = 0; i < 3; i++) {
		if (readl(priv->iomux_base + IOMUX_REG_OFFSET(bank, i)) & BIT(offset))
			af_num |= 0x1 << i;
	}

	if (af_num == 0) {
		if (readl(base + GPIO_DIR_OFFSET) & BIT(offset))
			ret = GPIOF_OUTPUT;
		else
			ret = GPIOF_INPUT;
	} else {
		ret = GPIOF_FUNC;
	}

	return ret;
}

static const struct dm_gpio_ops ambarella_gpio_ops = {
	.direction_input	= ambarella_gpio_direction_input,
	.direction_output	= ambarella_gpio_direction_output,
	.get_value		= ambarella_gpio_get_value,
	.set_value		= ambarella_gpio_set_value,
	.get_function		= ambarella_gpio_get_function,
};

static int ambarella_gpio_ofdata_to_platdata(struct udevice *dev)
{
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	int ret;
	struct ofnode_phandle_args args;

	ret = dev_read_phandle_with_args(dev, "gpio-ranges", NULL, 3, 0, &args);
	if (ret) {
		printf("%s: dev_read_phandle_with_args: err=%d\n",
				__func__, ret);
		return ret;
	}
	uc_priv->gpio_count = args.args[2];
	debug("arg0 is 0x%x, arg1 is 0x%x, arg2 is 0x%x\n", args.args[0], args.args[1], args.args[2]);

	return 0;
}

static int ambarella_gpio_probe(struct udevice *dev)
{
	struct ambarella_gpio_priv *priv = dev_get_priv(dev);
	int node, i, index;

	node = fdt_node_offset_by_compatible(gd->fdt_blob, -1, "ambarella,pinctrl");
	if (node == -FDT_ERR_NOTFOUND) {
		printf("not found ambarella,pinctrl node, please check it\n");
		return -1;
	}

	index = fdt_stringlist_search(gd->fdt_blob, node,
				      "reg-names", "iomux");
	priv->bank_num = index;
	priv->iomux_base = ofnode_get_addr_index(offset_to_ofnode(node), index);

	for(i = 0; i < priv->bank_num; i++)
		priv->gpio_base[i] = ofnode_get_addr_index(offset_to_ofnode(node), i);

	/* enable all bank gpio */
	for(i = 0; i < priv->bank_num; i++)
		writel(0x1, priv->gpio_base[i] + GPIO_ENABLE_OFFSET);

	return 0;
}

static int ambarella_gpio_remove(struct udevice *dev)
{
	return 0;
}

static const struct udevice_id ambarella_gpio_ids[] = {
	{ .compatible = "ambarella,gpio" },
	{ }
};

U_BOOT_DRIVER(ambarella_gpio) = {
	.name	= "ambarella_gpio",
	.id	= UCLASS_GPIO,
	.of_match = ambarella_gpio_ids,
	.ofdata_to_platdata = ambarella_gpio_ofdata_to_platdata,
	.probe	= ambarella_gpio_probe,
	.remove	= ambarella_gpio_remove,
	.ops	= &ambarella_gpio_ops,
	.priv_auto_alloc_size = sizeof(struct ambarella_gpio_priv),
};

