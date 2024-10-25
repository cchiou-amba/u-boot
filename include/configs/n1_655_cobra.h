#ifndef __PLAT_N1_655_COBRA_H__
#define __PLAT_N1_655_COBRA_H__

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

#define DRAM_SIZE					(16ULL << 30) /* 16GB */
/*
 * Memory allocator in board_r
 */
#define CONFIG_SYS_MALLOC_LEN		(8 * SZ_1M)

/*
 * Stack top pointer.
 */
#define CONFIG_SYS_INIT_SP_ADDR		0x08000000

#define COUNTER_FREQUENCY			(50000000) /* 50MHz */

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
	"name=kernel,start=19M,size=16M,uuid=${uuid_gpt_kernel};" \
	"name=rootfs,size=512M,uuid=${uuid_gpt_rootfs}\0"
#endif /* PARTS_DEFAULT */

#ifdef CONFIG_AMBA_BOOT_SECONDARY_CLUSTER
#define MULTI_CLUSTER_SETTINGS							  \
	"init_cluster1_image=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_1_jump_addr} /multi-cluster/vmlinuz-multi\0" \
	"init_cluster1_dtb=ext4load mmc ${emmc_dev_num}:${emmc_boot_part} ${cluster_1_dtb_addr} /multi-cluster/dtb/ambarella/cluster1.dtb\0"
#else
#define MULTI_CLUSTER_SETTINGS
#endif

#ifdef CONFIG_SUPPORT_EMMC_BOOT
#define CONFIG_EXTRA_ENV_SETTINGS			   \
	"kernel_addr_r=${kernel_addr}\0"						\
	"partitions=" PARTS_DEFAULT			 \
	"init_emmc_part=gpt write mmc 0 ${partitions}\0"\
	"cpu_info= nr_cpus=4 maxcpus=4 \0"		\
	"pcie_arg= pci=nomsi,pcie_bus_safe pcie_pme=nomsi \0"	\
	"boot_emmc=setenv bootargs console=ttyS0 noinitrd root=/dev/mmcblk0p4 rw rootfstype=ext4 rootwait ${cpu_info} ${pcie_arg};"			\
	"mmc read ${kernel_addr_r} 0x9800 0x8000; booti ${kernel_addr_r} - ${fdt_addr_r} \0"	  \
	"emmc_boot_part=1\0"					   \
	"boot_target=emmc\0"
#endif

#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND							\
	"if test ${AmbaEnv_boot_mode} = nand;"			\
	"then "											\
		"for target in ${boot_target};"				\
		"do "										\
			"echo 'Trying to boot from:'${target};"	\
			"run boot_${target};"					\
		"done;"										\
	"elif test ${AmbaEnv_boot_mode} = emmc;"		\
	"then "											\
		"for target in ${boot_target};"				\
		"do "										\
			"echo 'Trying to boot from':${target};"	\
			"run boot_${target};"					\
		"done;"										\
	"elif test ${AmbaEnv_boot_mode} = spinor;"		\
	"then "											\
		"for target in ${boot_target};"				\
		"do "										\
			"echo 'Trying to boot from':${target};"	\
			"run boot_${target};"					\
		"done;"										\
	"else "											\
		"echo Unsupport ...;"						\
	"fi"

#endif /* __PLAT_N1_655_COBRA_H__ */
