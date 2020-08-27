#ifndef __PLAT_CV25_H__
#define __PLAT_CV25_H__

#include <linux/sizes.h>
/*
 * Default load address:
 *	U-boot command script
 *	Kernel image
 *	Firmware
 */
#define CONFIG_SYS_LOAD_ADDR		0x18000000

/*
 * Memory allocator in board_r
 */
#define CONFIG_SYS_MALLOC_LEN		(8 * SZ_1M)

/*
 * Stack top pointer.
 */
#define CONFIG_SYS_INIT_SP_ADDR		0x10000000

#define CONFIG_SYS_MAX_NAND_DEVICE	1
#define COUNTER_FREQUENCY		(1008000000 / 16)

/*
 * GIC
 */
#define CONFIG_GICV2
#define GICD_BASE			0xF3001000
#define GICC_BASE			0xF3002000

/*
 *
 */

#define CONFIG_EXTRA_ENV_SETTINGS						\
	"bootargs_ubi= ubi.mtd=rootfs rootfstype=ubifs rw root=ubi0:rootfs \0"	\
	"bootargs_squashfs= root=/dev/mtdblock3 ro rootfstype=squashfs\0"	\
	"boot_nand= mtd read kernel ${kernel_addr}; "				\
		"setenv bootargs "						\
		"console=${console} ${bootargs_ubi} mtdparts=${mtdparts}; "	\
		"booti ${kernel_addr} - ${fdtaddr} \0"


#define CONFIG_BOOTCOMMAND							\
	"if test ${Ambarella@BOOT} = nand;"					\
		"then "								\
			"run boot_nand;"					\
		"elif test ${Ambarella@BOOT} = mmc;"				\
		"then "								\
			"echo TODO mmc boot;"					\
		"elif test ${Ambarella@BOOT} = spinor;"				\
		"then "								\
			"echo TODO spinor boot;"				\
		"else "								\
			"echo Unsupport ...;"					\
	"fi"									\

#endif
