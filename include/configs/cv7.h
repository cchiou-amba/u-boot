#ifndef __PLAT_CV7_H__
#define __PLAT_CV7_H__

#include <linux/sizes.h>
#include <asm/arch/soc.h>

//#define DEBUG
#define CONFIG_ARMV8_SWITCH_TO_EL1
/*
 * Default load address:
 *	U-boot command script
 *	Kernel image
 *	Firmware
 */
#define CONFIG_SYS_LOAD_ADDR		0x10000000

#define DRAM_SIZE			(8ULL << 30)
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


/*
 * GMAC
 */
#define CONFIG_DW_GMAC_DEFAULT_DMA_PBL  (8)
#define CONFIG_DW_ALTDESCRIPTOR

/*
 * GIC
 */
//#define CONFIG_GICV2
//#define GICD_BASE			0x20F3001000
//#define GICC_BASE			0x20F3002000

/*
 *
 */
#ifndef PARTS_DEFAULT
/* Define the default GPT table for eMMC */
#define PARTS_DEFAULT \
        /* Linux partitions */ \
        "uuid_disk=${uuid_gpt_disk};" \
        "name=kernel,start=${size_start},size=${size_kernel},uuid=${uuid_gpt_kernel};" \
        "name=rootfs,size=${size_rootfs},uuid=${uuid_gpt_rootfs}\0"
#endif /* PARTS_DEFAULT */

#define EXTRA_ENV_COMMON_SETTINGS        \
    "serial#=Ambarella CV7\0"            \
    "init_sd_gpio=md 0x20e4014000;"      \
        "mw 0x20e4014004 0x1;"          \
        "mw 0x20e4014028 0x1;"          \
        "mw 0x20e4014000 0x1;"          \
        "mw 0x20e401402C 0x1;"           \
        "mmc rescan;"                    \
        "mmc dev ${sd_dev_num}\0"        \
    "sd_dev_num=1\0"                     \
    "sd_boot_part=1\0"                   \
    "fdt_addr_r=0x1000\0"                \
    "fdt_high=0xffffffffffffffff\0"      \
    "extlinux_addr_r=0x0\0"              \
    "kernel_addr_r=" __stringify(CFG_KERNEL_LOAD_ADDR) "\0" \
    "kernel_comp_addr_r=0x1000000\0"     \
    "kernel_comp_size=0x1000000\0"       \
    "boot_sd_extlinux=run init_sd_gpio;" \
        "sysboot mmc ${sd_dev_num}:${sd_boot_part} any "\
        "${extlinux_addr_r} /extlinux/extlinux.conf\0"


#ifdef CONFIG_SUPPORT_EMMC_BOOT
#define CONFIG_EXTRA_ENV_SETTINGS               \
        "partitions=" PARTS_DEFAULT             \
	"boot_emmc=setenv bootargs console=ttyS0 noinitrd root=/dev/mmcblk0p2 rw rootfstype=ext4 init=/linuxrc rootwait;"            \
	"mmc read ${kernel_addr} ${mmc_read_blk} ${mmc_read_cnt};"	\
	"booti ${kernel_addr} - ${fdtaddr} \0"      \
    EXTRA_ENV_COMMON_SETTINGS                   \
    "size_start=2M\0"                           \
    "size_kernel=256M\0"                        \
    "size_rootfs=2G\0"                          \
    "mmc_read_blk=0x1000\0"                     \
    "mmc_read_cnt=0x8000\0"                     \
    "init_emmc_part=gpt write mmc 0 ${partitions}\0"\
    "init_emmc=mmc bootbus 0 2 1 0;"                \
        "mmc partconf 0 0 1 0;"                     \
        "mmc rst-function 0 1\0"                    \
    "mmc_dev_num=0\0"                               \
    "mmc_boot_part=1\0"                             \
    "boot_emmc_extlinux=mmc rescan;"                \
        "mmc dev ${mmc_dev_num};"                   \
        "sysboot mmc ${mmc_dev_num}:${mmc_boot_part} any "\
        "${extlinux_addr_r} /extlinux/extlinux.conf\0"    \
    "boot_target=sd_extlinux emmc_extlinux emmc\0"


#else
#define CONFIG_EXTRA_ENV_SETTINGS						\
	"bootargs_nand= ubi.mtd=rootfs rootfstype=ubifs rw root=ubi0:rootfs init=/linuxrc \0"	\
	"boot_nand=setenv bootargs "						\
		"console=${console},115200 earlycon loglevel=8 ${bootargs_nand} ${mtdparts}; "		\
		"nand read ${kernel_addr} kernel; "				\
		"booti ${kernel_addr} - ${fdtaddr} \0"                                   \
    EXTRA_ENV_COMMON_SETTINGS \
    "boot_target=nand\0"


#endif

#if defined(CONFIG_BOOTCOMMAND)
#undef CONFIG_BOOTCOMMAND
#endif
#define CONFIG_BOOTCOMMAND							\
    "if test ${AmbaEnv_boot_mode} = nand;"          \
    "then "                                         \
        "for target in ${boot_target};"             \
        "do "                                       \
            "echo 'Tring to boot from:'${target};"  \
            "run boot_${target};"                   \
        "done;"                                     \
    "elif test ${AmbaEnv_boot_mode} = emmc;"        \
    "then "                                         \
        "for target in ${boot_target};"             \
        "do "                                       \
            "echo 'Tring to boot from':${target};"  \
            "run boot_${target};"                   \
        "done;"                                     \
    "elif test ${AmbaEnv_boot_mode} = spinor;"      \
    "then "                                         \
        "echo spinor boot;"                         \
    "else "                                         \
        "echo Unsupport ...;"                       \
    "fi"

#endif
