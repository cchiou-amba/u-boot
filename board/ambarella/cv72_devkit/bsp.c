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
#include <asm/sections.h>
#include <asm/system.h>

__weak void plat_f_pinmux_config(void) { }
__weak void plat_f_clk_config(void) { }
__weak void plat_f_debug_init(void) { }
__weak void plat_f_soc_init(void){ }
__weak void plat_r_reset_cpu(void) { }
__weak void plat_f_early_print_init(void) { }
__weak void plat_device_init(void) { }

int dram_init(void)
{
	int rval;

	rval = plat_f_dram_init();
	if (rval)
		gd->ram_size = 0x40000000;

	return 0;
}

/*
 * board_r stage, place the platform hardare init here
 * call the hardware init after mmu enable at the current stage.
 * else mmu enable stage2 trans. but mmu_el1 no init will hang-up
 * under the secure-boot-mode-case.
 */
int board_init(void)
{
	if (current_el() != 3) {
		plat_f_clk_config();
		plat_f_pinmux_config();
		plat_f_early_print_init();
		plat_f_soc_init();
		plat_device_init();
	}

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

#if (CFG_DTB_LOAD_ADDR > 0)
	gd->fdt_blob = (void *)CFG_DTB_LOAD_ADDR;
#endif
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
