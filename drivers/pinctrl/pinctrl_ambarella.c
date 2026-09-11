// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <dm.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <dm/lists.h>
#include <dm/pinctrl.h>
#include <linux/compat.h>

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

struct ambarella_pinctrl_priv {
	phys_addr_t gpio_base[GPIO_MAX_BANK_NUM];
	phys_addr_t iomux_base;
	unsigned int bank_num;
};

#define PINID_TO_BANK(p)        ((p) >> 5)
#define PINID_TO_OFFSET(p)      ((p) & 0x1f)

static char pin_name[PINNAME_SIZE];

/* the order should be the same as enum gpio_func_t */
static const char * const pinmux_mode[] = {
	"gpio input",
	"gpio output",
	"analog",
	"unknown",
	"alt function",
};

static const char *ambarella_pinctrl_get_pin_name(struct udevice *dev,
					      unsigned int selector)
{
	snprintf(pin_name, PINNAME_SIZE, "gpio%d", selector);

	return pin_name;
}

static int ambarella_pinctrl_get_pins_count(struct udevice *dev)
{
	ofnode node;
	int ret, count;
	struct fdtdec_phandle_args args;

	ret = -1;
	dev_for_each_subnode(node, dev) {
		if (ofnode_read_bool(node, "gpio-controller")) {
			ret = 0;
			break;
		}
	}

	if (ret) {
		printf("not found gpio-controller, please add it\n");
		return ret;
	}

	ret = fdtdec_parse_phandle_with_args(gd->fdt_blob, ofnode_to_offset(node), "gpio-ranges",
					     NULL, 3, 0, &args);
	count = args.args[2];
	debug("%s %d\n", __func__, count);

	return count;
}

static int ambarella_pinctrl_get_pin_muxing(struct udevice *dev,
					unsigned int selector,
					char *buf,
					int size)
{
	uint32_t bank, offset, i;
	uintptr_t base;
	int mode, af_num = 0;
	struct ambarella_pinctrl_priv *priv = dev_get_priv(dev);

	bank = PINID_TO_BANK(selector);
	offset = PINID_TO_OFFSET(selector);
	base = priv->gpio_base[bank];

	for (i = 0; i < 3; i++) {
		if (readl(priv->iomux_base + IOMUX_REG_OFFSET(bank, i)) & BIT(offset))
			af_num |= 0x1 << i;
	}

	if (af_num == 0) {
		if (readl(base + GPIO_DIR_OFFSET) & BIT(offset))
			mode = GPIOF_OUTPUT;
		else
			mode = GPIOF_INPUT;
	} else {
		mode = GPIOF_FUNC;
	}

	switch (mode) {
	case GPIOF_UNKNOWN:
		/* should never happen */
		return -EINVAL;
	case GPIOF_UNUSED:
	case GPIOF_OUTPUT:
	case GPIOF_INPUT:
		snprintf(buf, size, "%s", pinmux_mode[mode]);
		break;
	case GPIOF_FUNC:
		snprintf(buf, size, "%s %d", pinmux_mode[mode], af_num);
		break;
	default:
		break;
	}

	return 0;
}

static int gpio_cfg_mode(struct udevice *dev, int gpio, int mode, int alt_func)
{
	u32 bank, offset, data, i;
	unsigned long regbase;
	struct ambarella_pinctrl_priv *priv = dev_get_priv(dev);

	bank = PINID_TO_BANK(gpio);
	offset = PINID_TO_OFFSET(gpio);
	regbase = priv->gpio_base[bank];

	if (mode == GPIO_FUNC_HW) {
		setbits_32(regbase + GPIO_AFSEL_OFFSET, BIT(offset));
		clrbits_32(regbase + GPIO_MASK_OFFSET, BIT(offset));
	} else {
		if (mode == GPIO_FUNC_SW_INPUT)
			clrbits_32(regbase + GPIO_DIR_OFFSET, BIT(offset));
		else
			setbits_32(regbase + GPIO_DIR_OFFSET, BIT(offset));
		clrbits_32(regbase + GPIO_AFSEL_OFFSET, BIT(offset));
		setbits_32(regbase + GPIO_MASK_OFFSET, BIT(offset));
	}

	for (i = 0; i < 3; i++) {
		data = readl(priv->iomux_base + IOMUX_REG_OFFSET(bank, i));
		data &= (~(0x1 << offset));
		data |= (((alt_func >> i) & 0x1) << offset);
		writel(data, priv->iomux_base + IOMUX_REG_OFFSET(bank, i));
	}

	writel(0x1, priv->iomux_base + IOMUX_CTRL_SET_OFFSET);
	writel(0x0, priv->iomux_base + IOMUX_CTRL_SET_OFFSET);

	return 0;
}

static int gpio_pin_mode(struct udevice *dev, int gpio, int alt_func)
{
	int mode;

	mode = alt_func == 0 ? GPIO_FUNC_SW_INPUT : GPIO_FUNC_HW;

	debug("gpio %d, mode %d, alt func %d\n", gpio, mode, alt_func);
	return gpio_cfg_mode(dev, gpio, mode, alt_func);
}


static int ambarella_pinctrl_config(struct udevice *dev, int node)
{
	const struct fdt_property *prop;
	u32 *pin_data;
	int npins, size, i;

	prop = fdt_getprop(gd->fdt_blob, node, "amb,pinmux-ids", &size);
	if (!prop) {
		printf("No amb,pinmux-ids property in node\n");
		return -EINVAL;
	}
	pin_data = kzalloc(size, 0);
	if (!pin_data)
		return -ENOMEM;

	if (fdtdec_get_int_array(gd->fdt_blob, node, "amb,pinmux-ids",
				 pin_data, size >> 2)) {
		printf("Error reading amb,pinmux-ids data.\n");
		kfree(pin_data);
		return -EINVAL;
	}

	npins = size / 4;
	for (i = 0; i < npins; i++)
		gpio_pin_mode(dev, pin_data[i] & 0xfff, pin_data[i] >> 12);

	kfree(pin_data);

#if 0
	prop = fdt_getprop(gd->fdt_blob, node, "amb,pinconf-ids", &size);
	if (!prop) {
		//printf("No amb,pinconf-ids property in node\n");
		/* normally, this property may be not exist */
		return 0;
	}
	pin_data = kzalloc(size, 0);
	if (!pin_data)
		return -ENOMEM;

	if (fdtdec_get_int_array(gd->fdt_blob, node, "amb,pinconf-ids",
				 pin_data, size >> 2)) {
		printf("Error reading mb,pinconf-ids data.\n");
		kfree(pin_data);
		return -EINVAL;
	}

	npins = size / pin_size;
	for (i = 0; i < npins; i++)
		gpio_pin_config(pin_data[i] & 0xffff, pin_data[i] >> 16);

	 kfree(pin_data);
#endif

	return 0;
}

static int ambarella_pinctrl_bind(struct udevice *dev)
{
	ofnode node;
	const char *name;
	int ret;

	dev_for_each_subnode(node, dev) {
		ofnode_get_property(node, "gpio-controller", &ret);
		if (ret < 0)
			continue;
		/* Get the name of each gpio node */
		name = ofnode_get_name(node);
		if (!name)
			return -EINVAL;

		/* Bind each gpio node */
		ret = device_bind_driver_to_node(dev, "ambarella_gpio",
						 name, node, NULL);
		if (ret)
			return ret;

		debug("%s: bind %s\n", __func__, name);
		/* assume just one gpio conroller */
		break;
	}

	return 0;
}

#if CONFIG_IS_ENABLED(PINCTRL_FULL)
static int ambarella_pinctrl_set_state(struct udevice *dev, struct udevice *config)
{
	return ambarella_pinctrl_config(dev, dev_of_offset(config));
}
#else /* PINCTRL_FULL */
static int ambarella_pinctrl_set_state_simple(struct udevice *dev,
					  struct udevice *periph)
{
	const void *fdt = gd->fdt_blob;
	const fdt32_t *list;
	uint32_t phandle;
	int config_node;
	int size, i, ret;

	list = fdt_getprop(fdt, dev_of_offset(periph), "pinctrl-0", &size);
	if (!list)
		return -EINVAL;

	debug("%s: periph->name = %s\n", __func__, periph->name);

	size /= sizeof(*list);
	for (i = 0; i < size; i++) {
		phandle = fdt32_to_cpu(*list++);

		config_node = fdt_node_offset_by_phandle(fdt, phandle);
		if (config_node < 0) {
			pr_err("prop pinctrl-0 index %d invalid phandle\n", i);
			return -EINVAL;
		}

		ret = ambarella_pinctrl_config(dev, config_node);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

const struct pinctrl_ops ambarella_pinctrl_ops = {
#if CONFIG_IS_ENABLED(PINCTRL_FULL)
	.set_state		= ambarella_pinctrl_set_state,
#else /* PINCTRL_FULL */
	.set_state_simple	= ambarella_pinctrl_set_state_simple,
#endif /* PINCTRL_FULL */
	.get_pin_name		= ambarella_pinctrl_get_pin_name,
	.get_pins_count		= ambarella_pinctrl_get_pins_count,
	.get_pin_muxing		= ambarella_pinctrl_get_pin_muxing,
};

static int ambarella_pinctrl_probe(struct udevice *dev)
{
	int i, index;
	struct ambarella_pinctrl_priv *priv = dev_get_priv(dev);

	/* reg-names is gpio0, gpio1 ... gpioN, iomux */
	index = fdt_stringlist_search(gd->fdt_blob, dev_of_offset(dev),
				      "reg-names", "iomux");
	priv->bank_num = index;

	for(i = 0; i < priv->bank_num; i++) {
		priv->gpio_base[i] = devfdt_get_addr_index(dev, i);
	}

	priv->iomux_base = devfdt_get_addr_name(dev, "iomux");

	return 0;
}

static const struct udevice_id ambarella_pinctrl_match[] = {
	{ .compatible = "ambarella,pinctrl" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(ambarella_pinctrl) = {
	.name = "ambarella_pinctrl",
	.id = UCLASS_PINCTRL,
	.of_match = ambarella_pinctrl_match,
	.ops = &ambarella_pinctrl_ops,
	.bind = ambarella_pinctrl_bind,
	.probe = ambarella_pinctrl_probe,
	.priv_auto_alloc_size = sizeof(struct ambarella_pinctrl_priv),
	.flags		= DM_FLAG_PRE_RELOC,
};

