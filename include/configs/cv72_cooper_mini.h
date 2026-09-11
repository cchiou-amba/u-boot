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
#define BOOT_SD_EXTLINUX                                      \
  "sd_dev_num=1\0"                                            \
  "sd_boot_part=1\0"                                          \
  "sd_extlinux_file=/extlinux/extlinux.conf\0"                \
  "boot_sd_extlinux=mmc rescan;"                              \
  "sysboot mmc ${sd_dev_num}:${sd_boot_part} any "            \
  "${extlinux_addr_r} ${sd_extlinux_file}\0"

#define BOOT_ISO_EXTLINUX                                     \
  "iso_dev_num=1\0"                                           \
  "iso_boot_part=1\0"                                         \
  "iso_extlinux_file=/EFI/BOOT/live/extlinux/extlinux.conf\0" \
  "boot_iso_extlinux=mmc rescan;"                             \
  "sysboot mmc ${iso_dev_num}:${iso_boot_part} any "          \
  "${extlinux_addr_r} ${iso_extlinux_file}\0"

#define BOOT_EMMC_EXTLINUX                                    \
  "emmc_dev_num=0\0"                                          \
  "emmc_boot_part=1\0"                                        \
  "emmc_extlinux_file=/extlinux/extlinux.conf\0"              \
  "boot_emmc_extlinux=mmc rescan;"                            \
  "sysboot mmc ${emmc_dev_num}:${emmc_boot_part} any "        \
  "${extlinux_addr_r} ${emmc_extlinux_file}\0"

#define BOOT_EMMC                                             \
  "pcie_arg= pci=nomsi,pcie_bus_safe pcie_pme=nomsi \0"       \
  "boot_emmc=setenv bootargs console=ttyS0 noinitrd root=/dev/mmcblk0p3 rw rootfstype=ext4 rootwait ${pcie_arg};" \
  "mmc read ${kernel_addr} 0x9800 0x8000; booti ${kernel_addr} - ${fdtaddr} \0"

#define CONFIG_EXTRA_ENV_SETTINGS                             \
  "serial#=Ambarella CV72 Cooper Mini\0"                      \
  "fdt_addr_r=0x200000\0"                                     \
  "extlinux_addr_r=0x401000\0"                                \
  "ramdisk_addr_r=0x8000000\0"                                \
  "kernel_addr_r=0x400000\0"                                  \
  "kernel_comp_addr_r=0x2800000\0"                            \
  "kernel_comp_size=0x2800000\0"                              \
  "boot_target=iso_extlinux sd_extlinux emmc_extlinux emmc\0" \
  BOOT_EMMC                                                   \
  BOOT_SD_EXTLINUX                                            \
  BOOT_ISO_EXTLINUX                                           \
  BOOT_EMMC_EXTLINUX                                          \
  "usbdl=mw.l 0xffed080034 0x0213042e; sleep 3; reset;\0"
#endif

#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND                    \
  "if test ${AmbaEnv_boot_mode} = nand;"      \
  "then "                                     \
    "for target in ${boot_target};"           \
    "do "                                     \
      "echo Trying to boot from: ${target};"  \
      "run boot_${target};"                   \
    "done;"                                   \
  "elif test ${AmbaEnv_boot_mode} = emmc;"    \
  "then "                                     \
    "for target in ${boot_target};"           \
    "do "                                     \
      "echo Trying to boot from: ${target};"  \
      "run boot_${target};"                   \
    "done;"                                   \
  "else "                                     \
    "echo Unsupport ...;"                     \
  "fi"

#endif /* __PLAT_CV72_COOPER_MINI_H__ */
