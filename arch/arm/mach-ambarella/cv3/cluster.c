/**
 * bld/loader.c
 *
 * History:
 *    2022/07/05 - [Cao Rongrong] created file
 *
 * Copyright (c) 2026 Ambarella International LP
 *
 * License Identifier: AMBARELLA-2-Clause
 *
 */
#include <common.h>
#include <env.h>
#include <dm.h>
#include <command.h>
#include <image.h>
#include <asm/arch/cortex.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <linux/bug.h>
#include <asm/system.h>
#include <fdt_support.h>

#define PTR_CAST(x)              ((void *)(unsigned long)(x))

extern void _clean_d_cache_range(void *addr, unsigned int size);
extern void _clean_flush_all_cache(void);

/* cluster0 has its own stack */
u8 cluster_stack[CORTEX_CLUSTER_NUM - 1][CORTEX_CLUSTER_STACK_SIZE]
				__attribute__ ((aligned(32), section(".bss.noinit")));

unsigned long long memparse(const char *ptr, char **retptr)
{
	char *endptr;	/* local pointer to end of parsed string */
	unsigned long long size = 0;

	size = simple_strtoull(ptr, &endptr, 0);

	switch (*endptr) {
	case 'G':
	case 'g':
		size <<= 10;
	case 'M':
	case 'm':
		size <<= 10;
	case 'K':
	case 'k':
		size <<= 10;
		endptr++;
	default:
		break;
	}

	if (retptr)
		*retptr = endptr;

	return size;
}

static int fdt_update_cpux(void *fdt, int verbose)
{
	int rval = 0;

#if defined(CONFIG_AMBA_BOOT_SECONDARY_CORTEX)
	uintptr_t cpux_jump;
	int offset, cpu;

	if (current_el() != 3)
		return rval;

	cpux_jump = (uintptr_t)secondary_cortex_jump;

	offset = fdt_path_offset(fdt, "/psci");
	if (offset >= 0 && (rval = fdt_del_node(fdt, offset)) < 0)
		return rval;

	offset = fdt_path_offset(fdt, "/cpus");
	if (offset < 0)
		return offset;

	for (cpu = 0, offset = fdt_first_subnode(fdt, offset); offset >= 0;
				offset = fdt_next_subnode(fdt, offset), cpu++) {
		const char *device_type = fdt_getprop(fdt, offset, "device_type", NULL);
		if (!device_type || strncmp(device_type, "cpu", 3))
			continue;

		rval = fdt_setprop_u64(fdt, offset, "cpu-release-addr",
					cpux_jump + cpu * sizeof(uintptr_t));
		if (rval < 0)
			break;

		/* enable-method is only used by ARMv8 chips in aarch64 mode */
		rval = fdt_setprop_string(fdt, offset,
					"enable-method", "spin-table");
		if (rval < 0)
			break;
	}

	if (verbose)
		printf("cpux_jump: 0x%08lx\n", cpux_jump);
#endif

	return rval;
}

static int fdt_update_cluster_cpux(void *fdt, u32 cluster_id, int verbose)
{
	int rval = 0;

#if defined(CONFIG_AMBA_BOOT_SECONDARY_CORTEX)
	const int *prop;
	uintptr_t cpux_jump;
	int offset, value, cpu;

	if (current_el() != 3)
		return 0;

	cpux_jump = (uintptr_t)secondary_cortex_jump;
	cpux_jump += cluster_id * CORTEX_CORE_MAX_NUM * sizeof(uintptr_t);

	offset = fdt_path_offset(fdt, "/psci");
	if (offset >= 0 && (rval = fdt_del_node(fdt, offset)) < 0)
		return rval;

	offset = fdt_path_offset(fdt, "/cpus");
	if (offset < 0)
		return offset;

	for (cpu = 0, offset = fdt_first_subnode(fdt, offset); offset >= 0;
				offset = fdt_next_subnode(fdt, offset), cpu++) {
		const char *device_type = fdt_getprop(fdt, offset, "device_type", NULL);
		if (!device_type || strncmp(device_type, "cpu", 3))
			continue;

		prop = fdt_getprop(fdt, offset, "reg", NULL);
		if (prop == NULL)
			break;

		value = fdt32_to_cpu(*prop);
		value |= cluster_id << 16;

		rval = fdt_setprop_u32(fdt, offset, "reg", value);
		if (rval < 0)
			break;

		rval = fdt_setprop_u64(fdt, offset, "cpu-release-addr",
					cpux_jump + cpu * sizeof(uintptr_t));
		if (rval < 0)
			break;

		rval = fdt_setprop_string(fdt, offset, "enable-method", "spin-table");
		if (rval < 0)
			break;
	}

	if (verbose)
		printf("cpux_jump: 0x%08lx\n", cpux_jump);
#endif

	return rval;
}

static int fdt_update_cluster_tags(u32 cluster_id, uintptr_t jump_addr,
	uintptr_t initrd2_start, uintptr_t initrd2_size, int verbose)
{
	uintptr_t kernelp, kernels = 0UL, fdt_addr = 0UL;
	void *fdt;
	const char *cmdline;
	int offset, val[4], rval;

	// fdt = PTR_CAST(jump_addr + SIZE_1MB * 64);
	switch(cluster_id) {
		case 1:
		case 2:
		case 3: {
			char node[64] = {0};
			sprintf(node, "cluster_%d_dtb_addr", cluster_id);
			strict_strtoul(env_get(node), 16, &fdt_addr);
		}break;
		default: {
			printf("Wrong cluster id %d......\n", cluster_id);
			BUG();
		}break;
	}
	fdt = PTR_CAST(fdt_addr);

	offset = fdt_path_offset(fdt, "/chosen");
	if (offset < 0) {
		rval = offset;
		pr_err("libfdt chosen node error: %s\n", fdt_strerror(rval));
		goto fdt_update_tags_exit;
	}

	if ((initrd2_start != 0x0) && (initrd2_size != 0x0)) {
		rval = fdt_setprop_u32(fdt, offset, "linux,initrd-start",
					initrd2_start);
		if (rval < 0) {
			pr_err("libfdt linux,initrd-start error: %s\n", fdt_strerror(rval));
			goto fdt_update_tags_exit;
		}

		rval = fdt_setprop_u32(fdt, offset, "linux,initrd-end",
					initrd2_start + initrd2_size);
		if (rval < 0) {
			pr_err("libfdt linux,initrd-end error: %s\n", fdt_strerror(rval));
			goto fdt_update_tags_exit;
		}
	}

	kernelp = jump_addr & (~SIZE_1MB_MASK);

	cmdline = fdt_getprop(fdt, offset, "bootargs", NULL);
	if (cmdline) {
		char *str, *s = "mem=";

		// str = strnstr(cmdline, s, strlen(cmdline));
		str = strstr(cmdline, s);
		if (str){
			kernels = memparse(str + strlen(s), NULL);
			if (verbose) {
				printf("kernelp: 0x%08lx kernels: 0x%08lx\n", kernelp, kernels);
				printf("dtbp: 0x%08lx\n", (uintptr_t)fdt);
				printf("initrd2_start: 0x%08lx initrd2_size: 0x%08lx\n", initrd2_start, initrd2_size);
			}

			BUG_ON(kernelp < DRAM_START_ADDR);
			BUG_ON(kernels > DRAM_SIZE || kernels == 0);

			offset = fdt_node_offset_by_prop_value(fdt, -1, "device_type", "memory", 7);
			if (offset < 0) {
				rval = offset;
				pr_err("libfdt memory node error: %s\n", fdt_strerror(rval));
				goto fdt_update_tags_exit;
			}

			val[0] = cpu_to_fdt32((u64)kernelp >> 32);
			val[1] = cpu_to_fdt32((u32)kernelp);
			val[2] = cpu_to_fdt32((u64)kernels >> 32);
			val[3] = cpu_to_fdt32((u32)kernels);

			rval = fdt_setprop(fdt, offset, "reg", val, sizeof(val));
			if (rval < 0) {
				pr_err("libfdt setup memory error: %s\n", fdt_strerror(rval));
				goto fdt_update_tags_exit;
			}
		}
	}

	rval = fdt_update_cluster_cpux(fdt, cluster_id, verbose);
	if (rval < 0) {
		pr_err("fdt_update_cluster_cpux: %s\n", fdt_strerror(rval));
		goto fdt_update_tags_exit;
	}

#if defined(SYSTEM_COUNTER_IS_BROKEN)
	offset = fdt_node_offset_by_compatible(fdt, -1, "arm,armv8-timer");
	if (offset >= 0) {
		rval = fdt_setprop_u32(fdt, offset, "clock-frequency", get_apb_bus_freq_hz());
		if (rval < 0) {
			pr_err("libfdt clock-frequency error: %s\n", fdt_strerror(rval));
			goto fdt_update_tags_exit;
		}

		/* intentionally make armv8-timer driver probe failed */
		fdt_delprop(fdt, offset, "interrupts");
	}
#endif

	fdt_fixup_ethernet(fdt);

	_clean_d_cache_range(fdt, fdt_totalsize(fdt));

fdt_update_tags_exit:
	return rval;
}

void second_cluster_dcache_invalid_notify(u32 cluster_id)
{
	uint32_t syncflg, mask;
	syncflg = readl(AHBSP_DATA3_REG);
	mask = 0x3 << (cluster_id * 4);
	syncflg |= mask;
	writel(syncflg, AHBSP_DATA3_REG);
}
void wait_second_cluster_invalid_dcache_done(u32 cluster_id)
{
	uint32_t syncflg, mask;
	do {
		syncflg = readl(AHBSP_DATA3_REG);
		mask = 0x3 << (cluster_id * 4);
	} while ((syncflg & mask) != mask);
}
void second_cluster_image_load_notify(u32 cluster_id)
{
	uint32_t syncflg, mask;
	mask = 0xC << (cluster_id * 4);
	syncflg = readl(AHBSP_DATA3_REG);
	syncflg |= mask;
	writel(syncflg, AHBSP_DATA3_REG);
}
void wait_second_cluster_image_load_done(u32 cluster_id)
{
	u32 syncflg, mask;
	mask = 0xF;
	do {
		syncflg = readl(AHBSP_DATA3_REG);
		syncflg >>= (cluster_id * 4);
	} while ((syncflg & mask) != mask);
}

int boot_cluster(int boot_multi_cluster, int verbose)
{
	uintptr_t jump_addr = 0, fdt_addr = 0, rmd_start = 0, rmd_size = 0;
	int rval;
	void* fdt;
	char *cmd_prefix = NULL;
	u32 cluster_id;

	if (boot_multi_cluster == 1) {
		strict_strtoul(env_get("fdtaddr"), 16, &fdt_addr);
	} else {
		strict_strtoul(env_get("fdt_addr_r"), 16, &fdt_addr);
	}
	fdt = PTR_CAST(fdt_addr);
	rval = fdt_update_cpux(fdt, verbose);
	if(rval){
		printf("fdt_update_cpux fail.\n");
	}

	switch(boot_multi_cluster) {
		case -1: cmd_prefix = NULL;      break; /* No need to boot multi-cluster */
		case  0: cmd_prefix = "";        break; /* Boot special multi-cluster */
		case  1: cmd_prefix = "emmc_";   break; /* Boot emmc Kernel Image */
		case  2: cmd_prefix = "lychee_"; break; /* Boot Lychee Kernel Image */
		default: cmd_prefix = "emmc_";   break;
	}
	if (NULL == cmd_prefix) {
		/* Reset cluster 0 RAM_SIZE to DRAM_SIZE when single cluster boots */
		if (!rval) {
			extern int update_fdt_memory_size(void *blob, u64 ram_start, u64 ram_size);
			rval = update_fdt_memory_size(fdt, 0, gd->ram_size);
		}
		return rval;
	}

	for (cluster_id = 1; cluster_id < CORTEX_CLUSTER_NUM; cluster_id++) {
		char load_cluster_img_cmd[64] = {0};
		char load_cluster_dtb_cmd[64] = {0};
		sprintf(load_cluster_img_cmd, "%sinit_cluster%d_image", cmd_prefix, cluster_id);
		sprintf(load_cluster_dtb_cmd, "%sinit_cluster%d_dtb", cmd_prefix, cluster_id);
		switch(cluster_id) {
			case 1:
			case 2:
			case 3: {
				char cluster_jump_addr[64] = {0};
				char cluster_dtbs_addr[64] = {0};
				sprintf(cluster_jump_addr, "cluster_%d_jump_addr", cluster_id);
				strict_strtoul(env_get(cluster_jump_addr), 16, &jump_addr);
				sprintf(cluster_dtbs_addr, "cluster_%d_dtb_addr", cluster_id);
				strict_strtoul(env_get(cluster_dtbs_addr), 16, &fdt_addr);
			} break;
			default:
				printf("Wrong cluster ID: %u\n", cluster_id);
				BUG();
			break;
		}

		printf("Boot cluster %d...\n",cluster_id);
		writel(CORTEX_RVBAR_ADDR(gd->relocaddr), CORTEX_RVBARADDR0_REG + (cluster_id << 4));
		writel(CORTEX_RVBAR_ADDR(gd->relocaddr), CORTEX_RVBARADDR1_REG + (cluster_id << 4));
		writel(CORTEX_RVBAR_ADDR(gd->relocaddr), CORTEX_RVBARADDR2_REG + (cluster_id << 4));
		writel(CORTEX_RVBAR_ADDR(gd->relocaddr), CORTEX_RVBARADDR3_REG + (cluster_id << 4));

		clrbits_32(CORTEX_RESET_REG, CORTEX_RESET_MASK(cluster_id));

		wait_second_cluster_invalid_dcache_done(cluster_id);
		secondary_cortex_jump[cluster_id * CORTEX_CORE_MAX_NUM + 0] = jump_addr;
		_clean_d_cache_range(secondary_cortex_jump + cluster_id * CORTEX_CORE_MAX_NUM,
												 sizeof(uintptr_t));
		printf("Cluster%d loading    DTB to 0x%lx: ", cluster_id, fdt_addr);
		run_command(env_get(load_cluster_dtb_cmd), 0);

		printf("Cluster%d loading Kernel to 0x%lx: ", cluster_id, jump_addr);
		run_command(env_get(load_cluster_img_cmd), 0);

		rval = fdt_update_cluster_tags(cluster_id, jump_addr, rmd_start, rmd_size, verbose);
		if (rval < 0) {
			printf("Failed to update cluster%d FDT\n", cluster_id);
			return rval;
		}
		_clean_flush_all_cache();
		second_cluster_image_load_notify(cluster_id);
	}

	return 0;
}

