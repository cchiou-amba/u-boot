/* SPDX-License-Identifier: GPL-2.0+
 *
 * Copyright (C) 2026 Ambarella International LP
 */
#include <command.h>
#include <common.h>
#include <env.h>
#include <dm/device.h>
#include <asm/armv8/mmu.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <asm/io.h>
#include <linux/bitops.h>

#include <fdt.h>
#include <linux/libfdt.h>
#include <asm/system.h>
#include <version.h>

//#include <config.h>
#if 0
#if !defined(IDSP_RAM_START)

#if defined(IDSP_PRIVATE_SIZE_MB) && (DRAM_SIZE > (1ULL << 32))
#define IDSP_RAM_START          (1ULL << 32)
#elif defined(IDSP_PRIVATE_SIZE_MB) && (DRAM_SIZE <= (1ULL << 32))
#define IDSP_RAM_START          ((1ULL << 32) - (IDSP_PRIVATE_SIZE_MB << 20))
#else
#error                          "IDSP_RAM_START is not specified"
#endif

#if 0
#define IDSP_SHARED_START       (IDSP_RAM_START - (IDSP_SHARED_SIZE_MB << 20))
#define CV_SHARED_START         (IDSP_SHARED_START - (CV_SHARED_SIZE_MB << 20))
#define CV_RAM_START            (CV_SHARED_START - (CV_PRIVATE_SIZE_MB << 20))

#if ((IDSP_PRIVATE_SIZE_MB + IDSP_SHARED_SIZE_MB + CV_SHARED_SIZE_MB) > 4096)
#error                          "Memory resource is overflow"
#endif
#endif
#endif /* IDSP_RAM_START */
#endif

static inline uintptr_t get_idsp_memory_size(void)
{
        uintptr_t size;

        size = DRAM_SIZE + DRAM_START_ADDR - IDSP_RAM_START;

        return size;
}

static int fdt_update_dram_burst_size(void *fdt)
{
	const char *compatible = "ambarella,ddrc";
	u32 burst_size;
	int offset;

	/*
	 * Some Socs don't have this node in device tree,
	 * return OK if the compatible is not found
	 */
	offset = fdt_node_offset_by_compatible(fdt, -1, compatible);
	if (offset < 0)
		return 0;

	burst_size = DRAM_BURST_SIZE(0);
	return fdt_setprop_u32(fdt, offset, "burst-size", burst_size);
}

static int fdt_update_memory(void *blob,
	uintptr_t iav_start, uintptr_t iav_size,
	uintptr_t frame_buf_start, uintptr_t frame_buf_size)
{
	int offset, len, rval = -1;
	u32 val[4];

	if (blob == NULL)
		goto fdt_update_memory_exit;

	offset = fdt_node_offset_by_prop_value(blob, -1, "device_type", "memory", 7);
	if (offset < 0) {
		rval = offset;
		goto fdt_update_memory_exit;
	}

	if (fdt_getprop(blob, offset, "reg", &len) == NULL)
		goto fdt_update_memory_exit;
#if 0
	if (len == sizeof(u32) * 2) {
		//val[0] = cpu_to_fdt32((u32)kernel_start);
		//val[1] = cpu_to_fdt32((u32)kernel_size);
		val[0] = cpu_to_fdt32(0x0120000);
		val[1] = cpu_to_fdt32(0x1000000);
	} else {
		val[0] = cpu_to_fdt32(0x0);
		val[1] = cpu_to_fdt32(0x0120000);
		val[2] = cpu_to_fdt32(0x0);
		val[3] = cpu_to_fdt32(0x1000000);
	}
	rval = fdt_setprop(blob, offset, "reg", val, len);
	if (rval < 0)
		goto fdt_update_memory_exit;
#endif
	/* create a new node "/iavmem" (offset 0 is root level) */
	offset = fdt_add_subnode(blob, 0, "iavmem");
	if (offset < 0)
		goto fdt_update_memory_exit;

	rval = fdt_setprop_string(blob, offset, "device_type", "iavmem");
	if (rval < 0)
		goto fdt_update_memory_exit;

	if (len == sizeof(u32) * 2) {
		val[0] = cpu_to_fdt32((u32)iav_start);
		val[1] = cpu_to_fdt32((u32)iav_size);
	} else {
		val[0] = cpu_to_fdt32((u64)iav_start >> 32);
		val[1] = cpu_to_fdt32((u32)iav_start);
		val[2] = cpu_to_fdt32((u64)iav_size >> 32);
		val[3] = cpu_to_fdt32((u32)iav_size);
	}
	rval = fdt_setprop(blob, offset, "reg", val, len);
	if (rval < 0)
		goto fdt_update_memory_exit;

	/* create a new node "/fbmem" (offset 0 is root level) */
	offset = fdt_add_subnode(blob, 0, "fbmem");
	if (offset < 0)
		goto fdt_update_memory_exit;

	rval = fdt_setprop_string(blob, offset, "device_type", "fbmem");
	if (rval < 0)
		goto fdt_update_memory_exit;

	if (len == sizeof(u32) * 2) {
		val[0] = cpu_to_fdt32((u32)frame_buf_start);
		val[1] = cpu_to_fdt32((u32)frame_buf_size);
	} else {
		val[0] = cpu_to_fdt32((u64)frame_buf_start >> 32);
		val[1] = cpu_to_fdt32((u32)frame_buf_start);
		val[2] = cpu_to_fdt32((u64)frame_buf_size >> 32);
		val[3] = cpu_to_fdt32((u32)frame_buf_size);
	}
	rval = fdt_setprop(blob, offset, "reg", val, len);
	if (rval < 0)
		goto fdt_update_memory_exit;

fdt_update_memory_exit:
	return rval;
}

#if !defined(CONFIG_AARCH64_TRUSTZONE) && defined(CONFIG_ARCH_AMBARELLA_CV5)
void fdt_setup_att_regmap(void *fdt)
{

#define PAGE_ENTRY_SHIFT		(14)
#define PAGE_ENTRY_TOTAL		(1 << 14)
#define PAGE_ENTRY_MASK			(PAGE_ENTRY_TOTAL - 1)
#define PAGE_ATTR_RO			(1 << 15)

	int i, j, offset, len, count, verbose;
	const char *compatible = "ambarella,att-regmap";
	const unsigned int *prop;
	unsigned int bitmap, page_size, regval;
	unsigned int entry_start, entry_count, page_start;
	unsigned long dram_size;

	struct segment_regmap {
		unsigned long virt_addr;
		unsigned long phys_addr;
		unsigned long size;
	} regmap[8];

	if (current_el() < 3)
		return;

	offset = fdt_node_offset_by_compatible(fdt, -1, compatible);
	if (offset < 0)
		return ;

	prop = fdt_getprop(fdt, offset, "amb,att-debug", &len);
	if (!prop)
		verbose = 0;
	else
		verbose = 1;

	prop = fdt_getprop(fdt, offset, "dram-size", &len);
	if (!prop || len < 0) {
		printf("%s: get property 'dram-size' error\n", __func__);
		return ;
	}

	dram_size = ((unsigned long)fdt32_to_cpu(prop[0]) << 32) | fdt32_to_cpu(prop[1]);
	page_size = (unsigned int)(dram_size >> PAGE_ENTRY_SHIFT);

	prop = fdt_getprop(fdt, offset, "client-bitmap", &len);
	if (!prop || len < 0) {
		printf("%s: get property 'client-bitmap' error\n", __func__);
		return ;
	}
	bitmap = fdt32_to_cpu(prop[0]);

	prop = fdt_getprop(fdt, offset, "segment-regmap", &len);
	if (!prop || len < 0){
		printf("%s: get property 'segment-regmap' error\n", __func__);
		return ;
	}

	for (i = 0; i < len / 24; i ++) {
		regmap[i].virt_addr = ((unsigned long)fdt32_to_cpu(prop[i * 6 + 0]) << 32);
		regmap[i].virt_addr |= fdt32_to_cpu(prop[i * 6 + 1]);
		regmap[i].phys_addr = ((unsigned long)fdt32_to_cpu(prop[i * 6 + 2]) << 32);
		regmap[i].phys_addr |= fdt32_to_cpu(prop[i * 6 + 3]);
		regmap[i].size = ((unsigned long)fdt32_to_cpu(prop[i * 6 + 4]) << 32);
		regmap[i].size |= fdt32_to_cpu(prop[i * 6 + 5]);

		if (!regmap[i].size || regmap[i].size < page_size){
			printf("%s: Invalid regmap size 0x%lx\n", __func__, regmap[i].size);
			return;
		}
	}

	count = i;

	for (i = 0; verbose && (i < count); i++) {
		printf("vaddr 0x%lx, paddr 0x%lx, size 0x%lx\n",
				regmap[i].virt_addr, regmap[i].phys_addr, regmap[i].size);
	}

	for (i = 0; i < 32; i++) {
		if (!(bitmap & (1 << i)))
			continue;

		writel(0, DRAMC_DRAM_BASE + 0x40c + 0x4 * i);
		writel(PAGE_ENTRY_MASK, DRAMC_DRAM_BASE + 0x48c + 0x4 * i);
	}

	regval = PAGE_ATTR_RO | PAGE_ENTRY_MASK;
	regval |=regval << 16;
	for (i = 0; i < PAGE_ENTRY_TOTAL; i += 2)
		writel(regval, DRAMC_DRAM_BASE + 0x10000 + i * 2);

	for (i = 0; i < count; i++) {
		entry_start = regmap[i].virt_addr / page_size;
		entry_count = regmap[i].size / page_size;
		page_start = regmap[i].phys_addr / page_size;

		for (j = entry_start; j < entry_start + entry_count; j += 2) {
			regval = page_start++;
			regval |= page_start++ << 16;
			writel(regval, DRAMC_DRAM_BASE + 0x10000 + j * 2);
		}
	}

	writel(bitmap, DRAMC_DRAM_BASE + 0x400);
}
#else
void fdt_setup_att_regmap(void *fdt)
{
}
#endif

/*
 * This function is called right before the kernel is booted. "blob" is the
 * device tree that will be passed to the kernel.
 */
#if defined(CONFIG_ARCH_AMBARELLA_CV3) || defined(CONFIG_ARCH_AMBARELLA_N1_655) || defined(CONFIG_ARCH_AMBARELLA_CV22) || defined(CONFIG_ARCH_AMBARELLA_CV25)
int ft_system_setup(void *blob, struct bd_info *bd)
{
	int rval;
	rval = fdt_setprop_string(blob, 0, "uboot_version", U_BOOT_VERSION_STRING);
	if (rval < 0) {
		printf("fdt_setprop_string: %s\n", fdt_strerror(rval));
		return rval;
	}
	return 0;
}
#else
int ft_system_setup(void *blob, struct bd_info *bd)
{
	uintptr_t iavp, iavs, fbp, fbs;
	int rval;

	iavp = IDSP_RAM_START;
	iavs = get_idsp_memory_size();
	fbp = IDSP_RAM_START - FRAMEBUFFER_SIZE;
	fbs = FRAMEBUFFER_SIZE;

	{
		printf("iavp: 0x%08lx iavs: 0x%08lx\n", iavp, iavs);
		printf("fbp: 0x%08lx fbs: 0x%08lx\n", fbp, fbs);
		printf("dtbp: 0x%08lx\n", (uintptr_t)blob);
	}

	rval = fdt_update_memory(blob, iavp, iavs, fbp, fbs);
	if (rval < 0) {
		printf("fdt_update_memory: %s\n", fdt_strerror(rval));
		return rval;
	}

	rval = fdt_update_dram_burst_size(blob);
	if (rval < 0) {
		printf("fdt update dram burst size: %s\n", fdt_strerror(rval));
		return rval;
	}

	fdt_setup_att_regmap(blob);

	return 0;
}
#endif
