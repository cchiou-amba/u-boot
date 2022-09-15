#ifndef __PLAT_S6LM_H__
#define __PLAT_S6LM_H__

#include <linux/sizes.h>
#include <asm/arch/soc.h>

#define CONFIG_ARMV8_SWITCH_TO_EL1
/*
 * Default load address:
 *	U-boot command script
 *	Kernel image
 *	Firmware
 */
#define CONFIG_SYS_LOAD_ADDR		0x10000000

#define DRAM_SIZE					(8ULL << 30)
/*
 * Memory allocator in board_r
 */
#define CONFIG_SYS_MALLOC_LEN		(8 * SZ_1M)

/*
 * Stack top pointer.
 */
#define CONFIG_SYS_INIT_SP_ADDR		0x08000000

#define CONFIG_SYS_MAX_NAND_DEVICE	1
#define COUNTER_FREQUENCY		0x2faf080


/*
 * GMAC
 */
#define CONFIG_DW_GMAC_DEFAULT_DMA_PBL  (8)
#define CONFIG_DW_ALTDESCRIPTOR

/*
 *
 */
#ifndef PARTS_DEFAULT
/* Define the default GPT table for eMMC */
#define PARTS_DEFAULT \
        /* Linux partitions */ \
        "uuid_disk=${uuid_gpt_disk};" \
        "name=uboot,start=1M,size=1M,uuid=${uuid_gpt_uboot};" \
        "name=kernel,size=20M,uuid=${uuid_gpt_kernel};" \
        "name=rootfs,size=512M,uuid=${uuid_gpt_rootfs}\0"
#endif /* PARTS_DEFAULT */


#ifdef CONFIG_SUPPORT_EMMC_BOOT
#define CONFIG_EXTRA_ENV_SETTINGS               \
        "partitions=" PARTS_DEFAULT 	\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi fw_devlink=permissive \0"	\
	"boot_emmc=setenv bootargs console=ttyS0 noinitrd root=/dev/mmcblk0p3 rw rootfstype=ext4 init=/linuxrc rootwait ${pcie_arg};"	\
	"mmc read ${kernel_addr} 0x1000 0x8000;"	\
	"booti ${kernel_addr} - ${fdtaddr} \0"

#else
#define CONFIG_EXTRA_ENV_SETTINGS						\
	"serial#=Ambarella CV5\0"						\
	"bootargs_nand= ubi.mtd=rootfs rootfstype=ubifs rw root=ubi0:rootfs init=/linuxrc \0"	\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi fw_devlink=permissive \0" \
	"boot_nand=setenv bootargs "						\
		"console=${console} ${bootargs_nand} ${pcie_arg} ${mtdparts}; "		\
		"nand read ${kernel_addr} kernel; "				\
		"booti ${kernel_addr} - ${fdtaddr} \0"                                   \
		"init_sd_gpio=md 0x20e4016000;"                                          \
		        "mw 0x20e4016004 0x10;"                                          \
		        "mw 0x20e4016028 0x10;"                                          \
		        "mw 0x20e4016000 0x10;"                                          \
		        "mw 0x20e401602C 0x1;"                                           \
		        "mmc rescan;"                                                    \
		        "mmc dev 1 \0"                                                   \
		"bootargs_sd=root=/dev/mmcblk1p2 rw rootfstype=ext4 init=/sbin/init \0"  \
		"boot_sd=run init_sd_gpio;"                                              \
		        "setenv bootargs console=${console} ${bootargs_sd} ${pcie_arg};" \
		        "ext4load mmc 1:1 ${kernel_addr} Image;"                         \
		        "booti ${kernel_addr} - ${fdtaddr} \0"
#endif

#define CONFIG_BOOTCOMMAND							\
	"if test ${AmbaEnv_boot_mode} = nand;"					\
		"then "								\
			"run boot_nand;"					\
		"elif test ${AmbaEnv_boot_mode} = emmc;"				\
		"then "								\
			"echo eMMC boot;"					\
			"run boot_emmc;"					\
		"elif test ${AmbaEnv_boot_mode} = spinor;"			\
		"then "								\
			"echo spinor boot;"					\
		"else "								\
			"echo Unsupport ...;"					\
	"fi"									\

#endif
