/* SPDX-License-Identifier: GPL-2.0+
 *
 * Copyright (C) 2020 Ambarella International LP
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
	u32 value, burst_size;
	int offset;

	/*
	 * Some Socs don't have this node in device tree,
	 * return OK if the compatible is not found
	 */
	offset = fdt_node_offset_by_compatible(fdt, -1, compatible);
	if (offset < 0)
		return 0;

	(void)value; /* avoid gcc warning "unused variable" */
	value = readl(DRAM_REG(REG_DRAM_MODE));
	burst_size = DRAM_BURST_SIZE(value);

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

/*
 * This function is called right before the kernel is booted. "blob" is the
 * device tree that will be passed to the kernel.
 */
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

	return 0;
}
