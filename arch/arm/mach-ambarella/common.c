/* SPDX-License-Identifier: GPL-2.0+
 *
 * Copyright (C) 2026 Ambarella International LP
 */
#include <common.h>
#include <env.h>
#include <dm/device.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/armv8/mmu.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <asm/arch/cortex.h>
#include <asm/arch-ambarella/flexfw.h>
#include <asm/arch-ambarella/key_alg.h>
#include <asm/arch-ambarella/scratchpad.h>

#include <i2c.h>
#include <dm/uclass.h>

#include <fdt.h>
#include <fdt_support.h>
#include <linux/libfdt.h>
#include <linux/delay.h>
#include <linux/string.h>

extern void _clean_d_cache(void);
extern void _clean_d_cache_range(void *addr, unsigned int size);

static const char *u_boot_cfg = "/u-boot_cfg";
#if defined(CONFIG_AMBA_BOOT_SECONDARY_CORTEX)
static const char *clusters_mem = "/memory";
#endif

__weak void plat_f_pinmux_config(void) { }
__weak void plat_f_clk_config(void) { }
__weak void plat_f_debug_init(void) { }
__weak void plat_f_soc_init(void){ }
__weak void plat_r_reset_cpu(void) { }
__weak void plat_f_early_print_init(void) { }
__weak void plat_device_init(void) { }

#ifdef CONFIG_DM_I2C
static void check_i2c_config(void)
{
	printf("I2C DM support: enabled\n");

#ifdef CONFIG_SYS_I2C_SPEED
	printf("Default I2C speed: %d Hz\n", CONFIG_SYS_I2C_SPEED);
#endif

#ifdef CONFIG_SYS_I2C_SLAVE
	printf("Default I2C slave address: 0x%02x\n", CONFIG_SYS_I2C_SLAVE);
#endif
}
#else
static void check_i2c_config(void)
{
	printf("Warning: I2C DM support not enabled!\n");
}
#endif

static void list_i2c_buses(void)
{
	struct udevice *bus;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_I2C, &uc);
	if (ret) {
		printf("Failed to get I2C uclass: %d\n", ret);
		return;
	}

	//printf("Available I2C buses:\n");
	uclass_foreach_dev(bus, uc) {
	//printf("  Bus %d: %s\n", bus->seq, bus->name);
	}
}

int eth_get_mac_from_eeprom(char *eeprom_buf, const char *str, char *mac_str)
{
    char *found_pos, *mac_start;
    int i, mac_len = 0;

    if (!eeprom_buf || !mac_str) {
        printf("Invalid parameters\n");
        return -1;
    }

    found_pos = strstr(eeprom_buf, str);
    if (!found_pos) {
        printf("%s: not found in EEPROM data\n", str);
        return -1;
    }

    /* jump the "MACx:" string and found the start positon */
    mac_start = found_pos + 5;

    /* jumper TAB and space */
    while (*mac_start && (*mac_start == ' ' || *mac_start == '\t')) {
        mac_start++;
    }

    for (i = 0; i < 17; i++) {
        if (mac_start[i] == '\0' || mac_start[i] == ' ' ||
            mac_start[i] == ',' || mac_start[i] == '\n' ||
            mac_start[i] == '\r' || mac_start[i] == '\t') {
            break;
        }
        mac_str[i] = mac_start[i];
        mac_len++;
    }

    /* MAC format xx:xx:xx:xx:xx:xx */
    if (mac_len != 17) {
        printf("Invalid MAC address length: %d (expected 17)\n", mac_len);
        return -1;
    }

    mac_str[17] = '\0';

    printf("Extracted MAC: %s\n", mac_str);
    return 0;
}

#define EEPROM_SIZE (512*4)
int read_eeprom(int bus_addr, int dev_addr)
{
	struct udevice *bus, *dev;
	uint8_t eeprom_data[EEPROM_SIZE];  /* 增加到2048字节 */
	uint16_t eeprom_addr = 0x00;
	int ret, i;
	char mac_str[18];


	//printf("=== I2C Configuration Check ===\n");
	check_i2c_config();
	list_i2c_buses();

	//printf("Initializing I2C[%d] EEPROM read...\n", bus_addr);

	ret = uclass_get_device_by_seq(UCLASS_I2C, bus_addr, &bus);
	if (ret) {
		printf("Failed to get I2C[%d] bus: %d\n", bus_addr, ret);
		return ret;
	}

	//printf("I2C[%d] bus found\n", bus_addr);

	/* find device on I2C bus */
	ret = dm_i2c_probe(bus, dev_addr, 0, &dev);
	if (ret) {
		printf("Failed to probe EEPROM at address 0x%02x on I2C[%d]: %d\n", dev_addr, bus_addr, ret);
		return ret;
	}

	//printf("EEPROM found at I2C[%d] address 0x%02x\n", bus_addr, dev_addr);

	/* set i2c chip address length with 2 bytes */
	ret = i2c_set_chip_offset_len(dev, 2);
	if (ret) {
		printf("Failed to set EEPROM offset length to 2 bytes: %d\n", ret);
		return ret;
	}

	memset(eeprom_data, 0, sizeof(eeprom_data));

	for (i = 0; i < sizeof(eeprom_data); i += 32) {
		int read_size = min(32, (int)(sizeof(eeprom_data) - i));

		ret = dm_i2c_read(dev, eeprom_addr + i, &eeprom_data[i], read_size);
		if (ret) {
			printf("Failed to read EEPROM data at offset 0x%02x: %d\n",
			       eeprom_addr + i, ret);
			break;
		}

		udelay(1000);
	}
#if 0
	if (ret) {
		printf("EEPROM read incomplete, continuing with partial data...\n");
	} else {
		printf("EEPROM data read successfully\n");
	}
#endif
	if(!eth_get_mac_from_eeprom((char *)eeprom_data, "MAC0:", mac_str)){
		env_set("ethaddr", mac_str);
		printf("Set ethaddr environment variable to: %s\n", mac_str);
	}

	if(!eth_get_mac_from_eeprom((char *)eeprom_data, "MAC1:", mac_str)){
		env_set("eth1addr", mac_str);
		printf("Set eth1ddr environment variable to: %s\n", mac_str);
	}

	return 0;
}


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
int update_fdt_memory_size(void *blob, u64 ram_start, u64 ram_size)
{
	int ret = 0;
#if defined(CONFIG_OF_LIBFDT)
	int bank;
	u64 start[CONFIG_NR_DRAM_BANKS];
	u64 size[CONFIG_NR_DRAM_BANKS];

	for (bank = 0; bank < CONFIG_NR_DRAM_BANKS; bank++) {
		start[bank] = gd->bd->bi_dram[bank].start;
		size[bank]  = gd->bd->bi_dram[bank].size;
	}
	start[0] = ram_start;
	size[0]  = ram_size;
	ret = fdt_fixup_memory_banks(blob, start, size, CONFIG_NR_DRAM_BANKS);
#endif
	return ret;
}

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
			int i = 0;
			char buf[32] = {0};
			char dtbver[16] = {0};

			ret = 2; /* multi-cluster-lychee, indicates loading Lychee Kernel */

			/* Parse version */
			while((i < (sizeof(buf) - 1)) && (type[i] != ' ')) {
				buf[i] = type[i];
				++ i;
			}
			if ((i >= (sizeof(buf) - 1)) && (type[i] != ' ')) {
				/* Section is too large */
				break;
			}
			/* Trying to get multi-cluster dtb version string */
			type = strstr(buf, "-");
			if (NULL != type) {
				int maj, min;
				needle = strstr(&type[1], "-");
				if (NULL != needle) {
					if (0 == strncasecmp(&needle[1], "tz", 2)) {
						printf("Detected TrustZone Multi-Cluster DTB\n");
						env_set("trustzone", "-tz");
						needle[0] = '\0';
					}
				}
				if (2 == sscanf(&type[1], "%d.%d", &maj, &min)) {
					snprintf(dtbver, sizeof(dtbver), "%d.%d", maj, min);
					printf("Detected Multi-Cluster DTB for kernel-%s\n", dtbver);
					env_set("dtbver", dtbver);
				}
			}
			break;
		}

		ret = 0; /* multi-cluster, indicates loading special multi-cluster Kernel */
	}while(0);

	return ret;
}

static void env_set_clusters_mem_info(void)
{
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
				unsigned long dtb_start    = ram_addr + SIZE_1MB;
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
#if (AMBARELLA_SUPPORT_AST == 1)
		gd->ram_size -= 4096;
#endif
		mach_mem_map[0].size = gd->ram_size;
		return 0;
	}

	return -1;
}

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
	unsigned long kernel_addr = 0;
	const char *kernel_addr_str = env_get("kernel_addr_r");

	if (kernel_addr_str != NULL)
		kernel_addr = simple_strtoull(kernel_addr_str, NULL, 16);

#if defined(CFG_AARCH64_TRUSTZONE)
	if (kernel_addr == 0) {
		__asm__ volatile("b .");
	}
#endif
	/* the memory u-boot can pass to kernel */
	gd->bd->bi_dram[0].start = CONFIG_SYS_TEXT_BASE;
	gd->bd->bi_dram[0].size = gd->ram_size - CONFIG_SYS_TEXT_BASE;
#endif
	return 0;
}

int board_early_init_f(void)
{
	if (current_el() == 3) {
		plat_f_clk_config();
		plat_f_pinmux_config();
		plat_f_soc_init();
		plat_f_early_print_init();
		plat_device_init();
	}

	return 0;
}

int ambarella_is_secure_boot(void)
{
#if defined(AMBARELLA_CV2)
	return !!(readl(RCT_REG(SYS_CONFIG_OFFSET)) & SYS_CONFIG_SECURE_BOOT);
#else
	return (readl(SECSP_BOOT_STS_REG) & 1);
#endif
}

int flexible_image_handle(void *buf)
{
	uint32_t bin_length = 0;
	uint32_t bin_offset = 0;
	struct fw_image_header *img_hdr = buf;

	if (img_hdr->magic != IMAGE_HEADER_MAGIC) {
		return 0;
	}

	/* flexible format: use the first one ?? */
	bin_length = img_hdr->bin[0].bin_length;
	bin_offset = img_hdr->bin[0].bin_offset;
	memmove(buf, buf + bin_offset, bin_length);
	_clean_d_cache_range(buf, bin_length);

	/* verify image if possible */
	return auth_verify_image(buf, bin_length);
}

void board_cleanup_before_linux(void)
{
	_clean_d_cache();
}

void *board_fdt_blob_setup(void)
{
	void *fdt_blob = NULL;

#if (CFG_DTB_LOAD_ADDR > 0)
	fdt_blob = (void *)CFG_DTB_LOAD_ADDR;
#else
	/* FDT is at end of image */
	fdt_blob = (ulong *)&_end;
#endif
	return fdt_blob;
}
