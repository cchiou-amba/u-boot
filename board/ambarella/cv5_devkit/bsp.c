// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <cpu_func.h>
#include <fdt_support.h>
#include <init.h>
#include <log.h>
#include <errno.h>
#include <malloc.h>
#include <netdev.h>
#include <asm/io.h>
#include <dm.h>
#include <env.h>
#include <usb.h>
#include <linux/delay.h>
#include <asm-generic/gpio.h>
#include <asm/gpio.h>
#include <asm/arch/periph.h>
#include <asm/arch/pinmux.h>
#include <asm/armv8/mmu.h>
#include <asm/arch/misc.h>


int dram_init(void)
{
	int rval;

	rval = plat_f_dram_init();
	if (rval)
		gd->ram_size = 0x40000000;

	return 0;
}

/*
 * board_r stage.
 */
int board_init(void)
{
	printf("...\n");

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

	return 0;
}
