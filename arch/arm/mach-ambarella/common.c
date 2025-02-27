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
#include <asm/arch/cortex.h>

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
int get_cluster_image_type(const char *boot_args)
{
	int ret = -1;
	do {
		char *type   = NULL;
		char *needle = NULL;
		if (!boot_args) {
			break;
		}

		needle = strstr(boot_args, "multi-cluster");
		if (!needle) {
			break;
		}

		type = strstr(needle, "emmc");
		if (NULL != type) {
			ret = 1; /* multi-cluster-emmc, indicates loading Image from EMMC partition */
			break;
		}

		type = strstr(needle, "lychee");
		if (NULL != type) {
			ret = 2; /* multi-cluster-lychee, indicates loading Lychee Kernel */
			break;
		}

		ret = 0; /* multi-cluster, indicates loading special multi-cluster Kernel */
	}while(0);

	return ret;
}

static void env_set_clusters_mem_info(void){
	const void *fdt = gd->fdt_blob;
	const unsigned int *tmp;
	int offset;

	offset = fdt_path_offset(fdt, clusters_mem);
	if (offset < 0) {
		return ;
	}

	for (int i = 1; i < CORTEX_CLUSTER_NUM; ++ i) {
		char cluster_name[16] = {0};
		char jmp_addr_str[32] = {0};
		char dtb_addr_str[32] = {0};
		sprintf(cluster_name, "cluster_%d", i);
		sprintf(jmp_addr_str, "cluster_%d_jump_addr", i);
		sprintf(dtb_addr_str, "cluster_%d_dtb_addr",  i);

		tmp = fdt_getprop(fdt, offset, cluster_name, NULL);
		if (tmp) {
			if (env_get(jmp_addr_str) == NULL) {
				unsigned long ram_addr     = ((unsigned long)fdt32_to_cpu(tmp[0]) << 32) | fdt32_to_cpu(tmp[1]);
				unsigned long ram_size     = ((unsigned long)fdt32_to_cpu(tmp[2]) << 32) | fdt32_to_cpu(tmp[3]);
				unsigned long dtb_start    = ram_addr;
				unsigned long kernel_start = dtb_start + SIZE_1MB;
				char jmp_addr_value[32] = {0};
				char dtb_addr_value[32] = {0};
				if (i == 1) { /* Set initramfs highest address to CLUSTER1 RAM START */
				    char initrd_high_value[32] = {0};
				    sprintf(initrd_high_value, "0x%lx", ram_addr);
				    env_set("initrd_high", initrd_high_value);
				}

				sprintf(jmp_addr_value, "0x%lx", kernel_start);
				env_set(jmp_addr_str, jmp_addr_value);

				sprintf(dtb_addr_value, "0x%lx", dtb_start);
				env_set(dtb_addr_str, dtb_addr_value);

				if (i == 1) { /* use cluster 1 memory as culster_kernel_addr_r */
					char ldr_addr_value[32]  = {0};
					unsigned long load_start = kernel_start + (SIZE_1MB * 64);

					/* This address is for loading compressed kernel image */
					sprintf(ldr_addr_value, "0x%lx", load_start);
					env_set("cluster_kernel_addr_r", ldr_addr_value);

					printf("cluster_kernel_addr_r 0x%lX\n", load_start);
				}
				printf("Set cluster%d: RAM@0x%lX, SIZE:0x%lX, DTB@0x%lX, KERNEL@0x%lX\n",
							 i, ram_addr, ram_size, dtb_start, kernel_start);
			}
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
