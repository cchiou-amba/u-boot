/* SPDX-License-Identifier: GPL-2.0+
 *
 * Copyright (C) 2020 Ambarella International LP
 */
#include <common.h>
#include <env.h>
#include <dm/device.h>
#include <asm/armv8/mmu.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>

#include <fdt.h>
#include <linux/libfdt.h>

static const char *u_boot_cfg = "/u-boot_cfg";

static struct mm_region mach_mem_map[] = {
	{
		.virt = DRAM_SPACE_START,
		.phys = DRAM_SPACE_START,
		.size = DRAM_SPACE_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			PTE_BLOCK_INNER_SHARE
	},
	{
		.virt = DEVICE_SPACE_START,
		.phys = DEVICE_SPACE_START,
		.size = DEVICE_SPACE_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			PTE_BLOCK_NON_SHARE |
			PTE_BLOCK_PXN | PTE_BLOCK_UXN
	},
	{
		/* List terminator */
		0,
	}
};

struct mm_region *mem_map = mach_mem_map;

static void env_set_poc_info(void)
{
	int rval = 0, boot;
	const char *env = "AmbaEnv:boot_mode";

	env_set_hex("AmbaEnv:poc", rct_system_config());

	boot = rct_system_boot_from();

	switch(boot) {
	case SYS_CONFIG_BOOT_SPINOR:
		rval = env_set(env, "spinor");
		break;

	case SYS_CONFIG_BOOT_NAND:
		rval = env_set(env, "nand");
		break;

	case SYS_CONFIG_BOOT_EMMC:
		rval = env_set(env, "emmc");
		break;

	default:
		rval = -ENOTSUPP;
		break;
	}

	if (rval)
		printf("Platform: init board error %d\n", rval);
}

static void env_set_cfg_info(void)
{

	const void *fdt = gd->fdt_blob;
	const void *prop;
	int offset;

	offset = fdt_path_offset(fdt, u_boot_cfg);
	if (offset < 0) {
		return ;
	}

	prop = fdt_getprop(fdt, offset, "kernel-addr", NULL);
	if (prop) {
		char str[11];
		sprintf(str, "0x%x", fdt32_to_cpu(*(fdt32_t*)prop));
		env_set("kernel_addr", str);
	}

	prop = fdt_getprop(fdt, offset, "console", NULL);
	if (prop)
		env_set("console", prop);
	else
		env_set("console", "ttyS0");

	prop = fdt_getprop(fdt, offset, "mtdids", NULL);
	if (prop)
		env_set("mtdids", prop);

	prop = fdt_getprop(fdt, offset, "mtdparts", NULL);
	if (prop)
		env_set("mtdparts", prop);
}

ulong board_get_usable_ram_top(ulong total_size)
{
	const void *fdt = gd->fdt_blob;
	const void *prop;
	int offset;

	offset = fdt_path_offset(fdt, u_boot_cfg);
	if (offset < 0)
		return gd->ram_top;

	prop = fdt_getprop(fdt, offset, "reloc-top", NULL);
	if (prop)
		return fdt32_to_cpu(*(fdt32_t*)prop);

	return gd->ram_top;
}


int plat_f_dram_init(void)
{
	const void *fdt = gd->fdt_blob;
	const void *prop;
	int offset;

	offset = fdt_path_offset(fdt, u_boot_cfg);
	if (offset < 0)
		return -1;

	prop = fdt_getprop(fdt, offset, "ram-size", NULL);
	if (prop) {
		gd->ram_size = fdt32_to_cpu(*(fdt32_t*)prop);

		mach_mem_map[0].size = gd->ram_size;
		return 0;
	}

	return -1;
}

/*
 *
 */

__weak void plat_f_pinmux_config(void) { }
__weak void plat_f_clk_config(void) { }
__weak void plat_f_debug_init(void) { }
__weak void plat_f_soc_init(void){ }
__weak void plat_r_reset_cpu(void) { }
__weak void plat_f_early_print_init(void) { }
__weak void plat_device_init(void) { }

void plat_r_board_late_init(void)
{
	env_set_poc_info();
	env_set_cfg_info();
}


void reset_cpu(ulong addr)
{
	plat_r_reset_cpu();

	while(1)
		__asm__ volatile("wfe");
}

int dram_init_banksize(void)
{
#if defined(CONFIG_NR_DRAM_BANKS)
	gd->bd->bi_dram[0].start = 0;
	gd->bd->bi_dram[0].size = gd->ram_size;
#endif
	return 0;
}

int board_early_init_f(void)
{
	plat_f_clk_config();
	plat_f_pinmux_config();
	plat_f_soc_init();
	plat_f_early_print_init();

	plat_device_init();

	return 0;
}
