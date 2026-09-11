#ifndef __PLAT_N1_COOPER_MAX_H__
#define __PLAT_N1_COOPER_MAX_H__

#include <linux/sizes.h>
#include <asm/arch/soc.h>

//#define CONFIG_ARMV8_SWITCH_TO_EL1
/*
 * Default load address:
 *	U-boot command script
 *	Kernel image
 *	Firmware
 */
#define CONFIG_SYS_LOAD_ADDR		0x10000000

#define DRAM_SIZE					(64ULL << 30)
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

/* EEPROM */
#define CONFIG_ID_EEPROM
#define CONFIG_SYS_I2C_EEPROM_ADDR      0x50
#define CONFIG_SYS_EEPROM_BUS_NUM       3

/*
 *
 */
#ifndef PARTS_DEFAULT
/* Define the default GPT table for eMMC */
#define PARTS_DEFAULT \
        /* Linux partitions */ \
        "uuid_disk=${uuid_gpt_disk};" \
        "name=bst,start=0x0,size=128K,uuid=${uuid_gpt_bst};" \
        "name=bld,size=4M,uuid=${uuid_gpt_bld};" \
        "name=kernel,start=${size_start},size=${size_kernel},uuid=${uuid_gpt_kernel};" \
        "name=rootfs,size=${size_rootfs},uuid=${uuid_gpt_rootfs}\0"
#endif /* PARTS_DEFAULT */

#ifdef CONFIG_AMBA_BOOT_SECONDARY_CLUSTER
/* dtbver is set by function get_cluster_image_type() in common.c */
#define MULTI_CLUSTER_SETTINGS                              \
    "dtbver=\0" \
    "lychee_init_cluster1_image=unzip ${cluster_kernel_addr_r} ${cluster_1_jump_addr}\0" \
    "lychee_init_cluster2_image=unzip ${cluster_kernel_addr_r} ${cluster_2_jump_addr}\0" \
    "lychee_init_cluster3_image=unzip ${cluster_kernel_addr_r} ${cluster_3_jump_addr}\0" \
    "lychee_init_cluster1_dtb=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_1_dtb_addr} /multi-cluster/dtb/ambarella/${dtbver}/cluster1.dtb\0" \
    "lychee_init_cluster2_dtb=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_2_dtb_addr} /multi-cluster/dtb/ambarella/${dtbver}/cluster2.dtb\0" \
    "lychee_init_cluster3_dtb=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_3_dtb_addr} /multi-cluster/dtb/ambarella/${dtbver}/cluster3.dtb\0" \
    "init_cluster1_image=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_1_jump_addr} /multi-cluster/vmlinuz-multi\0" \
    "init_cluster2_image=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_2_jump_addr} /multi-cluster/vmlinuz-multi\0" \
    "init_cluster3_image=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_3_jump_addr} /multi-cluster/vmlinuz-multi\0" \
    "init_cluster1_dtb=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_1_dtb_addr} /multi-cluster/dtb/ambarella/cluster1.dtb\0" \
    "init_cluster2_dtb=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_2_dtb_addr} /multi-cluster/dtb/ambarella/cluster2.dtb\0" \
    "init_cluster3_dtb=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_3_dtb_addr} /multi-cluster/dtb/ambarella/cluster3.dtb\0" \
    "emmc_init_cluster1_image=mmc read ${cluster_1_jump_addr} 0x9800 0x8000\0" \
    "emmc_init_cluster2_image=mmc read ${cluster_2_jump_addr} 0x9800 0x8000\0" \
    "emmc_init_cluster3_image=mmc read ${cluster_3_jump_addr} 0x9800 0x8000\0" \
    "emmc_init_cluster1_dtb=mmc read ${cluster_1_dtb_addr} 0x5800 0x80\0" \
    "emmc_init_cluster2_dtb=mmc read ${cluster_2_dtb_addr} 0x5880 0x80\0" \
    "emmc_init_cluster3_dtb=mmc read ${cluster_3_dtb_addr} 0x5900 0x80\0"
#else
#define MULTI_CLUSTER_SETTINGS
#endif

#define EXTRA_ENV_COMMON_SETTINGS                               \
    "reset_shm=mw.b 0xff0001fdef 0x4;"                          \
        "mw.q 0xff0001fdf0 0x0404040404040404;"                 \
        "mw.q 0xff0001fdf8 0x0404040404040404;"                 \
        "mw.q 0xff0001fe60 0x1010101010101010;"                 \
        "mw.q 0xff0001fe68 0x1010101010101010;"                 \
        "mw.q 0xff0001fe70 0x1010101010101010;"                 \
        "mw.q 0xff0001fe78 0x1010101010101010\0"                \
    "print_shm_reg=md.b 0xff0001fdef 0x11;"                     \
        "md.q 0xff0001fe60 0x4\0"                               \
    "sd_dev_num=1\0"                                            \
    "sd_boot_part=1\0"                                          \
    "iso_dev_num=1\0"                                           \
    "iso_boot_part=1\0"                                         \
    "usb_dev_num=0\0"                                           \
    "usb_boot_part=1\0"                                         \
    "emmc_dev_num=0\0"                                          \
    "fdt_addr_r=0x200000\0"                                     \
    "extlinux_addr_r=0x401000\0"                                \
    "ramdisk_addr_r=0x8000000\0"                                \
    "kernel_addr_r=0x400000\0"                                  \
    "kernel_comp_addr_r=0x2800000\0"                            \
    "kernel_comp_size=0x2800000\0"                              \
    MULTI_CLUSTER_SETTINGS                                      \
    "fdt_high=0xffffffffffffffff\0"                             \
    "sd_extlinux_file=/extlinux/extlinux.conf\0"                \
    "iso_extlinux_file=/EFI/BOOT/live/extlinux/extlinux.conf\0" \
    "usb_extlinux_file=/EFI/BOOT/live/extlinux/extlinux.conf\0" \
    "emmc_extlinux_file=/extlinux/extlinux.conf\0"              \
    "boot_sd_extlinux=run print_shm_reg;"                       \
    "mmc rescan;"                                               \
    "sysboot mmc ${sd_dev_num}:${sd_boot_part} any "            \
    "${extlinux_addr_r} ${sd_extlinux_file}\0"                  \
    "boot_usb_extlinux=run print_shm_reg;"                      \
    "usb start;"                                                \
    "sysboot usb ${usb_dev_num}:${usb_boot_part} any "          \
    "${extlinux_addr_r} ${usb_extlinux_file}\0"                 \
    "boot_iso_extlinux=run print_shm_reg;"                      \
    "mmc rescan;"                                               \
    "sysboot mmc ${iso_dev_num}:${iso_boot_part} any "          \
    "${extlinux_addr_r} ${iso_extlinux_file}\0"                 \
    "boot_emmc_extlinux=run print_shm_reg;"                     \
    "mmc rescan;"                                               \
    "sysboot mmc ${emmc_dev_num}:${emmc_boot_part} any "        \
    "${extlinux_addr_r} ${emmc_extlinux_file}\0"                \
    "usbdl=mw.l 0xffed080034 0x000e442e; sleep 3; reset;\0"

#ifdef CONFIG_SUPPORT_EMMC_BOOT
#define CONFIG_EXTRA_ENV_SETTINGS				\
	"partitions=" PARTS_DEFAULT				\
	"cpu_info= nr_cpus=4 maxcpus=4 \0"			\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi \0"	\
	"ip_arg= ip=172.20.1.1::172.20.1.1:255.255.255.0:Ambarella:blazenet@01:off ip=172.20.2.1::172.20.2.1:255.255.255.0:Ambarella:blazenet@02:off ip=172.20.3.1::172.20.3.1:255.255.255.0:Ambarella:blazenet@03:off \0" \
	"boot_emmc= setenv bootargs console=ttyS0 noinitrd root=/dev/mmcblk0p4 rw rootfstype=ext4 rootwait ${cpu_info} ${pcie_arg} ${ip_arg} multi-cluster-emmc;" \
	"mmc read ${kernel_addr} 0x9800 0x8000;"		\
	"booti ${kernel_addr} - ${fdtaddr} \0"			\
	EXTRA_ENV_COMMON_SETTINGS				\
	"size_start=19M\0"					\
	"size_kernel=16M\0"					\
	"size_rootfs=512M\0"					\
	"emmc_boot_part=1\0"					\
	"boot_target=usb_extlinux iso_extlinux sd_extlinux emmc_extlinux emmc\0"

#elif CONFIG_AMBARELLA_SPINOR
#define CONFIG_EXTRA_ENV_SETTINGS						\
	"bootargs_spinor= root=/dev/mtdblock3 rw rootfstype=jffs2 earlycon \0"	\
	"cpu_info= nr_cpus=4 maxcpus=4 \0"		\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi \0" \
	"boot_spinor=setenv bootargs console=${console} ${bootargs_spinor} ${cpu_info} ${pcie_arg} ${mtdparts}; "		\
	"mtd read kernel ${kernel_addr}; "		\
	"booti ${kernel_addr} - ${fdtaddr} \0"	\
	EXTRA_ENV_COMMON_SETTINGS				\
	"emmc_boot_part=1\0"                    \
	"boot_target=usb_extlinux iso_extlinux sd_extlinux emmc_extlinux spinor\0"
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
        "for target in ${boot_target};"             \
        "do "                                       \
            "echo 'Tring to boot from':${target};"  \
            "run boot_${target};"                   \
        "done;"                                     \
    "else "                                         \
        "echo Unsupport ...;"                       \
    "fi"

#endif /* __PLAT_N1_COOPER_MAX_H__ */
