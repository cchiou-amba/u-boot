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
	gpio_request(107, "sdmmc1_pwr"); /* default 3.3V */
	gpio_request(109, "sdmmc2_pwr");

	gpio_direction_output(107, 0);
	gpio_direction_output(109, 0);
	mdelay(10);
	gpio_direction_output(107, 1);
	gpio_direction_output(109, 1);

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
