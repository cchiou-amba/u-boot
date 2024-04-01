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
#if defined(CONFIG_AMBA_BOOT_SECONDARY_CORTEX)
static const char *clusters_mem = "/memory";
#endif

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
	const char *env = "AmbaEnv_boot_mode";

	env_set_hex("AmbaEnv_poc", rct_system_config());

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

#if defined(CONFIG_AMBA_BOOT_SECONDARY_CORTEX)
static void env_set_clusters_mem_info(void){
	const void *fdt = gd->fdt_blob;
	const unsigned int *tmp;
	int offset;

	offset = fdt_path_offset(fdt, clusters_mem);
	if (offset < 0) {
		return ;
	}

	tmp = fdt_getprop(fdt, offset, "cluster_3", NULL);
	if (tmp){
		if (env_get("cluster_3_jump_addr") == NULL ){
			unsigned long cluster3_kernel_addr = ((unsigned long)fdt32_to_cpu(tmp[0]) << 32) | fdt32_to_cpu(tmp[1]);
			unsigned long cluster3_ram_size = ((unsigned long)fdt32_to_cpu(tmp[2]) << 32) | fdt32_to_cpu(tmp[3]);
			printf("set cluster3 ram...ram start is 0x%lx, size is 0x%lx.\n", cluster3_kernel_addr, cluster3_ram_size);
			char kernel_addr_str[11];
		    sprintf(kernel_addr_str, "0x%lx", cluster3_kernel_addr);
			env_set("cluster_3_jump_addr", kernel_addr_str);

			unsigned long addr = cluster3_kernel_addr + 0x4000000;
			char dtb_addr[11];
			sprintf(dtb_addr, "0x%lx", addr);
			env_set("cluster_3_dtb_addr", dtb_addr);
		}
	}

	tmp = fdt_getprop(fdt, offset, "cluster_2", NULL);
	if (tmp){
		if (env_get("cluster_2_jump_addr") == NULL ){
			unsigned long cluster2_kernel_addr = ((unsigned long)fdt32_to_cpu(tmp[0]) << 32) | fdt32_to_cpu(tmp[1]);
			unsigned long cluster2_ram_size = ((unsigned long)fdt32_to_cpu(tmp[2]) << 32) | fdt32_to_cpu(tmp[3]);
			printf("set cluster2 ram...ram start is 0x%lx, size is 0x%lx.\n", cluster2_kernel_addr, cluster2_ram_size);
			char kernel_addr_str[11];
			sprintf(kernel_addr_str, "0x%lx", cluster2_kernel_addr);
			env_set("cluster_2_jump_addr", kernel_addr_str);

			unsigned long addr = cluster2_kernel_addr + 0x4000000;
			char dtb_addr[11];
			sprintf(dtb_addr, "0x%lx", addr);
			env_set("cluster_2_dtb_addr", dtb_addr);
		}
	}

	tmp = fdt_getprop(fdt, offset, "cluster_1", NULL);
	if (tmp){
		if (env_get("cluster_1_jump_addr") == NULL ){
			unsigned long cluster1_kernel_addr = ((unsigned long)fdt32_to_cpu(tmp[0]) << 32) | fdt32_to_cpu(tmp[1]);
			unsigned long cluster1_ram_size = ((unsigned long)fdt32_to_cpu(tmp[2]) << 32) | fdt32_to_cpu(tmp[3]);
			printf("set cluster1 ram...ram start is 0x%lx, size is 0x%lx.\n", cluster1_kernel_addr, cluster1_ram_size);
			char kernel_addr_str[11];
			sprintf(kernel_addr_str, "0x%lx", cluster1_kernel_addr);
			env_set("cluster_1_jump_addr", kernel_addr_str);

			unsigned long addr = cluster1_kernel_addr + 0x4000000;
			char dtb_addr[11];
			sprintf(dtb_addr, "0x%lx", addr);
			env_set("cluster_1_dtb_addr", dtb_addr);
		}
	}
}
#endif

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
	const unsigned int *prop;
	int offset;

	offset = fdt_path_offset(fdt, u_boot_cfg);
	if (offset < 0)
		return -1;

	prop = fdt_getprop(fdt, offset, "ram-size", NULL);
	if (prop) {
		//gd->ram_size = DRAM_SIZE;
		gd->ram_size = ((unsigned long)fdt32_to_cpu(prop[0]) << 32) | fdt32_to_cpu(prop[1]);
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
#if defined(CONFIG_AMBA_BOOT_SECONDARY_CORTEX)
	env_set_clusters_mem_info();
#endif
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
