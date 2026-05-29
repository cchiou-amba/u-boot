// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <linux/delay.h>
#include <env.h>
#include <dm.h>
#include <usb.h>
#include <i2c.h>
#include <asm/gpio.h>
#include <asm/arch/misc.h>
#include "../common/eeprom.h"

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
	struct udevice *dev;

	/* SDMMC Power-up */
	gpio_request(144, "sdmmc0_rst");
	gpio_direction_output(144, 0);
	mdelay(10);
	gpio_direction_output(144, 1);
	mdelay(10);

	/* TF Card */
	i2c_get_chip_for_busnum(1, 0x0a, 1, &dev);
	dm_i2c_reg_write(dev, 0x8c, 0x8b);

	/* V110: SD Card, V120: WLAN + BT */
	i2c_get_chip_for_busnum(0, 0x0a, 1, &dev);
	dm_i2c_reg_write(dev, 0x8c, 0x8b);

	/* PCIE */
	gpio_request(17,  "pcie0_1_clk");	/* PCIE0_1 CLK */
	gpio_request(139, "usb_pcie2_clk");	/* USB & PCIE2 CLK */
	gpio_request(27,  "pcie0_pwr");		/* PCIE0 PWR */
	gpio_request(26,  "pcie1_pwr");		/* PCIE1 PWR */
	gpio_request(141, "pcie2_pwr");		/* PCIE2 PWR */

	/* PCIE Slot0&1 */
	gpio_direction_output(17, 0);
	gpio_direction_output(27, 1);
	mdelay(10);
	gpio_direction_output(26, 1);
	mdelay(10);

	/* PCIE Slot2 */
	gpio_direction_output(139, 0);
	gpio_direction_output(141, 1);

	/* IDC1 & LT9611 */
	gpio_request(49, "lt9611_pwr");
	gpio_direction_output(49, 1);

	/* USB */
	gpio_request(20, "usb_id");
	gpio_direction_output(20, 1);

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

	rval = env_set_hex("fdtaddr", (ulong)gd->fdt_blob);
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

	if (strcmp(get_pcba_version(), "v120") == 0) {
		/* Power-off GPIO active low, default high */
		gpio_request(14, "poweroff");
		gpio_direction_output(14, 1);
	}

	return 0;
}
