#ifndef __PLAT_CV3_H__
#define __PLAT_CV3_H__

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

#define DRAM_SIZE					(32ULL << 30)
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
        "name=kernel,start=${size_start},size=${size_kernel},uuid=${uuid_gpt_kernel};" \
        "name=rootfs,size=${size_rootfs},uuid=${uuid_gpt_rootfs}\0"
#endif /* PARTS_DEFAULT */

#ifdef CONFIG_AMBA_BOOT_SECONDARY_CLUSTER
#define MULTI_CLUSTER_SETTINGS                              \
    "cluster_1_jump_addr=0x500000000\0"                     \
    "cluster_2_jump_addr=0x600000000\0"                     \
    "cluster_3_jump_addr=0x700000000\0"                     \
    "cluster_1_dtb_addr=0x504000000\0"                      \
    "cluster_2_dtb_addr=0x604000000\0"                      \
    "cluster_3_dtb_addr=0x704000000\0"                      \
    "init_cluster1_image=ext4load mmc ${mmc_dev_num}:${mmc_boot_part} ${cluster_1_jump_addr} /vmlinuz-multi\0" \
    "init_cluster2_image=ext4load mmc ${mmc_dev_num}:${mmc_boot_part} ${cluster_2_jump_addr} /vmlinuz-multi\0" \
    "init_cluster3_image=ext4load mmc ${mmc_dev_num}:${mmc_boot_part} ${cluster_3_jump_addr} /vmlinuz-multi\0" \
    "init_cluster1_dtb=ext4load mmc ${mmc_dev_num}:${mmc_boot_part} ${cluster_1_dtb_addr} /dtb/ambarella/cluster1.dtb\0" \
    "init_cluster3_dtb=ext4load mmc ${mmc_dev_num}:${mmc_boot_part} ${cluster_3_dtb_addr} /dtb/ambarella/cluster3.dtb\0" \
    "init_cluster2_dtb=ext4load mmc ${mmc_dev_num}:${mmc_boot_part} ${cluster_2_dtb_addr} /dtb/ambarella/cluster2.dtb\0"
#else
#define MULTI_CLUSTER_SETTINGS
#endif

#define EXTRA_ENV_COMMON_SETTINGS                               \
    "serial#=Ambarella CV3\0"                                   \
    "reset_shm=mw.b 0xff0001fdef 0x4;"                          \
        "mw.q 0xff0001fdf0 0x0404040404040404;"                 \
        "mw.q 0xff0001fdf8 0x0404040404040404;"                 \
        "mw.q 0xff0001fe60 0x1010101010101010;"                 \
        "mw.q 0xff0001fe68 0x1010101010101010;"                 \
        "mw.q 0xff0001fe70 0x1010101010101010;"                 \
        "mw.q 0xff0001fe78 0x1010101010101010\0"                \
    "print_shm_reg=md.b 0xff0001fdef 0x11;"                     \
        "md.q 0xff0001fe60 0x4\0"                               \
    "set_usb3_gpio=gpio clear 33\0"                             \
    "init_usb3_gpio=md 0xffe4014000;"                           \
        "mw 0xffe4014004 0x2;"                                  \
        "mw 0xffe4014028 0x2;"                                  \
        "mw 0xffe4014000 0x0;"                                  \
        "mw 0xffe401402C 0x1;"                                  \
        "md 0xffe4014000\0"                                     \
    "init_sd_gpio=md 0xffe4017000;"                             \
        "mw 0xffe4017004 0x10000;"                              \
        "mw 0xffe4017028 0x10000;"                              \
        "mw 0xffe4017000 0x10000;"                              \
        "mw 0xffe401702C 0xffffffff;"                           \
        "mmc rescan;"                                           \
        "mmc dev ${sd_dev_num}\0"                               \
    "sd_dev_num=0\0"                                            \
    "sd_boot_part=1\0"                                          \
    "iso_dev_num=0\0"                                           \
    "iso_boot_part=1\0"                                         \
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
    "boot_iso_extlinux=run print_shm_reg;"                      \
    "run set_usb3_gpio; run init_sd_gpio;"                      \
    "sysboot mmc ${iso_dev_num}:${iso_boot_part} any "          \
    "${extlinux_addr_r} ${iso_extlinux_file}\0"                 \
    "boot_sd_extlinux=run print_shm_reg;"                       \
    "run set_usb3_gpio; run init_sd_gpio;"                      \
    "run init_cluster1_image; run init_cluster2_image;"         \
    "run init_cluster3_image; run init_cluster1_dtb;"           \
    "run init_cluster2_dtb; run init_cluster3_dtb;"             \
    "sysboot mmc ${sd_dev_num}:${sd_boot_part} any "            \
    "${extlinux_addr_r} ${sd_extlinux_file}\0"


#ifdef CONFIG_SUPPORT_EMMC_BOOT
#define CONFIG_EXTRA_ENV_SETTINGS               \
        "partitions=" PARTS_DEFAULT             \
	"cpu_info= nr_cpus=4 maxcpus=4 \0"		\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi fw_devlink=permissive \0"	\
	"boot_emmc=setenv bootargs console=ttyS0 noinitrd root=/dev/mmcblk0p2 rw rootfstype=ext4 init=/linuxrc rootwait ${cpu_info} ${pcie_arg};"            \
	"mmc read ${kernel_addr} 0x1000 0x8000;"	\
	"booti ${kernel_addr} - ${fdtaddr} \0"      \
    EXTRA_ENV_COMMON_SETTINGS                   \
    "size_start=1M\0"                           \
    "size_kernel=256M\0"                        \
    "size_rootfs=29G\0"                         \
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
    "boot_target=iso_extlinux sd_extlinux emmc_extlinux emmc\0"

#elif CONFIG_AMBARELLA_SPINOR
#define CONFIG_EXTRA_ENV_SETTINGS						\
	"bootargs_spinor= rootfstype=squashfs root=/dev/mtdblock4 init=/linuxrc \0"	\
	"cpu_info= nr_cpus=4 maxcpus=4 \0"		\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi fw_devlink=permissive \0" \
	"boot_spinor=setenv bootargs console=${console} ${bootargs_spinor} ${cpu_info} ${pcie_arg} ${mtdparts}; "		\
	"mtd read kernel ${kernel_addr_r}; "		\
	"mtd read dtb ${fdt_addr_r}; "			\
	"booti ${kernel_addr_r} - ${fdt_addr_r} \0"                                   \
    EXTRA_ENV_COMMON_SETTINGS \
    "boot_target=iso_extlinux sd_extlinux spinor\0"

#else
#define CONFIG_EXTRA_ENV_SETTINGS						\
	"bootargs_nand= ubi.mtd=rootfs rootfstype=ubifs rw root=ubi0:rootfs init=/linuxrc \0"	\
	"cpu_info= nr_cpus=4 maxcpus=4 \0"		\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi fw_devlink=permissive \0" \
	"boot_nand=setenv bootargs "						\
		"console=${console} ${bootargs_nand} ${cpu_info} ${pcie_arg} ${mtdparts}; " \
		"nand read ${kernel_addr} kernel; "				\
		"booti ${kernel_addr} - ${fdtaddr} \0"                                   \
    EXTRA_ENV_COMMON_SETTINGS \
    "boot_target=spinor sd_extlinux\0"

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

#endif
