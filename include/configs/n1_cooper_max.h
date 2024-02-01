#include "cv3.h"

#undef CONFIG_EXTRA_ENV_SETTINGS

#if CONFIG_AMBARELLA_SPINOR
#define CONFIG_EXTRA_ENV_SETTINGS						\
	"bootargs_spinor= root=/dev/mtdblock3 rw rootfstype=jffs2 earlycon \0"	\
	"cpu_info= nr_cpus=4 maxcpus=4 \0"		\
	"pcie_arg= pci=nomsi,pcie_bus_perf pcie_pme=nomsi fw_devlink=permissive \0" \
	"boot_spinor=setenv bootargs console=${console} ${bootargs_spinor} ${cpu_info} ${pcie_arg} ${mtdparts}; "		\
	"mtd read kernel ${kernel_addr}; "		\
	"booti ${kernel_addr} - ${fdtaddr} \0"	\
	EXTRA_ENV_COMMON_SETTINGS	\
	"boot_target=sd_extlinux spinor nand\0"
#endif
