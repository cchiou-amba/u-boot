#ifndef __PLAT_CV72_COOPER_MINI_H__
#define __PLAT_CV72_COOPER_MINI_H__

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

#define DRAM_SIZE			(8ULL << 30) /* 8GB */
/*
 * Memory allocator in board_r
 */
#define CONFIG_SYS_MALLOC_LEN		(8 * SZ_1M)

/*
 * Stack top pointer.
 */
#define CONFIG_SYS_INIT_SP_ADDR		0x08000000

#define CONFIG_SYS_MAX_NAND_DEVICE	1
#define COUNTER_FREQUENCY		(50000000) /* 50MHz */

#ifdef CONFIG_SUPPORT_EMMC_BOOT
#define CONFIG_EXTRA_ENV_SETTINGS			   \
	"kernel_addr_r=${kernel_addr}\0"						\
	"pcie_arg= pci=nomsi,pcie_bus_safe pcie_pme=nomsi \0"	\
	"boot_emmc=setenv bootargs console=ttyS0 noinitrd root=/dev/mmcblk0p3 rw rootfstype=ext4 rootwait ${pcie_arg};"			\
	"mmc read ${kernel_addr_r} 0x9800 0x8000; booti ${kernel_addr_r} - ${fdt_addr_r} \0"	  \
	"boot_target=emmc\0"
#endif

#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND							\
	"if test ${AmbaEnv_boot_mode} = nand;"			\
	"then "											\
		"for target in ${boot_target};"				\
		"do "										\
			"echo Trying to boot from: ${target};"	\
			"run boot_${target};"					\
		"done;"										\
	"elif test ${AmbaEnv_boot_mode} = emmc;"		\
	"then "											\
		"for target in ${boot_target};"				\
		"do "										\
			"echo Trying to boot from: ${target};"	\
			"run boot_${target};"					\
		"done;"										\
	"else "											\
		"echo Unsupport ...;"						\
	"fi"

#endif /* __PLAT_CV72_COOPER_MINI_H__ */
