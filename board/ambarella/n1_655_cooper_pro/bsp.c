// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2024 Ambarella International LP
 */

#include <env.h>
#include <dm.h>
#include <asm/gpio.h>
#include <asm/arch/misc.h>
#include <linux/delay.h>

int dram_init(void)
{
	int rval;

	rval = plat_f_dram_init();
	if (rval)
		gd->ram_size = DRAM_SIZE;

	return 0;
}

/*
 * board_r stage.
 */
int board_init(void)
{
    /* WL_PWR */
	gpio_request(20, "wl_pwr_on");
	gpio_direction_output(20, 1);
	mdelay(2);

	/* WL_REG */
	gpio_request(102, "wl_reg_on");
	gpio_direction_output(102, 1);

	/* BT_REG */
	gpio_request(103, "bt_reg_on");
	gpio_direction_output(103, 1);

	/* USB SWITCH */
	gpio_request(3, "usb_switch");
	gpio_direction_output(3, 0); //0: usb work as type-c 1:usb work as hub

	return 0;
}

int board_fit_config_name_match(const char *name)
{
	/*
	 * select configuration 0 by default.
	 */

	return 0;
}

static int __init_usb_gadget(void)
{
	int rval;
	struct udevice *dev;

	rval = uclass_get_device(UCLASS_USB_GADGET_GENERIC, 0, &dev);
	if (rval) {
		pr_err("%s: Cannot find USB device\n", __func__);
		return rval;
	}
	return 0;
}

int board_late_init(void)
{
	int rval;
	/*
	 * Specify the device-tree for Linux kernel
	 */

	rval = env_set_hex("fdt_addr_r", (ulong)gd->fdt_blob);
	if (rval) {
		printf("set fdtaddr env error.\n");
		return rval;
	}

	plat_r_board_late_init();

#ifdef CONFIG_USB_GADGET
	rval = __init_usb_gadget();
	if (rval)
		return rval;
#endif

	return 0;
}
