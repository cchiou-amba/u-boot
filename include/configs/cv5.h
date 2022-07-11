#ifndef __PLAT_S6LM_H__
#define __PLAT_S6LM_H__

#include <linux/sizes.h>
#include <asm/arch/soc.h>
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


#ifndef CONFIG_FASTBOOT_FLASH_NAND
#define CONFIG_EXTRA_ENV_SETTINGS               \
        "partitions=" PARTS_DEFAULT "\0"

#else
#define CONFIG_EXTRA_ENV_SETTINGS						\
	"serial#=Ambarella CV5\0"						\
	"bootargs_nand= ubi.mtd=rootfs rootfstype=ubifs rw root=ubi0:rootfs \0"	\
	"boot_nand=setenv bootargs "						\
		"console=${console} ${bootargs_nand} ${mtdparts}; "		\
		"nand read ${kernel_addr} kernel; "				\
		"booti ${kernel_addr} - ${fdtaddr} \0"
#endif

#define CONFIG_BOOTCOMMAND							\
	"if test ${AmbaEnv:boot_mode} = nand;"					\
		"then "								\
			"run boot_nand;"					\
		"elif test ${AmbaEnv:boot_mode} = mmc;"				\
		"then "								\
			"echo eMMC boot;"					\
		"elif test ${AmbaEnv:boot_mode} = spinor;"			\
		"then "								\
			"echo spinor boot;"					\
		"else "								\
			"echo Unsupport ...;"					\
	"fi"									\

#endif
