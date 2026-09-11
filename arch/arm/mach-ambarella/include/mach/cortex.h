
#ifndef __MACH__CORTEX_H__
#define __MACH__CORTEX_H__

#include "soc.h"

/*****************************************************/
#define CORTEX_RVBAR_ADDR(x)            ((x) >> 8)

#define AXI_CFG_OFFSET			0x0000
#define AXI_CFG_BASE			(AXI_BASE + AXI_CFG_OFFSET)
#define AXI_CFG_REG(x)			(AXI_CFG_BASE + (x))

/****************************************************/
#define CORTEX_CORE_MAX_NUM		4

#if defined(CONFIG_ARCH_AMBARELLA_CV3)
#define CORTEX_CLUSTER_NUM		4
#elif defined(CONFIG_ARCH_AMBARELLA_CV3AD685)
#define CORTEX_CLUSTER_NUM		3
#elif defined(CONFIG_ARCH_AMBARELLA_N1_655)
#define CORTEX_CLUSTER_NUM		2
#elif defined(CONFIG_ARCH_AMBARELLA_CV3AD635)
#define CORTEX_CLUSTER_NUM		1
#endif

#define CORTEX_CLUSTER_STACK_SIZE	0x800

/****************************************************/
#define SIZE_1KB		(1 * 1024)
#define SIZE_1KB_MASK		(SIZE_1KB - 1)
#define SIZE_1MB		(1024 * 1024)
#define SIZE_1MB_MASK		(SIZE_1MB - 1)

/****************************************************/
#if defined(CONFIG_ARCH_AMBARELLA_CV22) || defined(CONFIG_ARCH_AMBARELLA_CV3) || defined(CONFIG_ARCH_AMBARELLA_CV3AD635) \
|| defined(CONFIG_ARCH_AMBARELLA_CV25)
#define AXI_SYS_TIMER_INDEPENDENT	0
#define AXI_SYS_TIMER_DIVISOR		16 /* see AXI_CFG_REG(0x14) */
#else
#define AXI_SYS_TIMER_INDEPENDENT	1
#define AXI_SYS_TIMER_DIVISOR		12 /* see SCALER_SYS_CNT_POST_REG */
#endif

/****************************************************/
#define CORTEX_RESET_OFFSET		0x28
#define CORTEX_RESET_REG		AXI_CFG_REG(CORTEX_RESET_OFFSET)

#if defined(CONFIG_ARCH_AMBARELLA_CV3)
#define CORTEX_RESET_MASK(id)		((0xf << ((id) * 7 + 3)))
#elif defined(CONFIG_ARCH_AMBARELLA_CV3AD685) || defined(CONFIG_ARCH_AMBARELLA_N1_655)
#define CORTEX_RESET_MASK(id)		((0x1f << ((id) * 7 + 2)))
#endif

#if defined(CONFIG_ARCH_AMBARELLA_CV3)
#define CORTEX_RVBARADDR0_OFFSET	0x48
#define CORTEX_RVBARADDR1_OFFSET	0x4c
#define CORTEX_RVBARADDR2_OFFSET	0x50
#define CORTEX_RVBARADDR3_OFFSET	0x54
#elif defined(CONFIG_ARCH_AMBARELLA_CV72) || defined(CONFIG_ARCH_AMBARELLA_CV3AD685) \
|| defined(CONFIG_ARCH_AMBARELLA_CV75) || defined(CONFIG_ARCH_AMBARELLA_N1_655) \
|| defined(CONFIG_ARCH_AMBARELLA_CV7)
#define CORTEX_RVBARADDR0_OFFSET	0x48	/* 64-bit */
#define CORTEX_RVBARADDR1_OFFSET	0x50	/* 64-bit */
#define CORTEX_RVBARADDR2_OFFSET	0x58	/* 64-bit */
#define CORTEX_RVBARADDR3_OFFSET	0x60	/* 64-bit */
#else
#define CORTEX_RVBARADDR0_OFFSET	0x64
#define CORTEX_RVBARADDR1_OFFSET	0x68
#define CORTEX_RVBARADDR2_OFFSET	0x6c
#define CORTEX_RVBARADDR3_OFFSET	0x70
#endif
#define CORTEX_RVBARADDR0_REG		AXI_CFG_REG(CORTEX_RVBARADDR0_OFFSET)
#define CORTEX_RVBARADDR1_REG		AXI_CFG_REG(CORTEX_RVBARADDR1_OFFSET)
#define CORTEX_RVBARADDR2_REG		AXI_CFG_REG(CORTEX_RVBARADDR2_OFFSET)
#define CORTEX_RVBARADDR3_REG		AXI_CFG_REG(CORTEX_RVBARADDR3_OFFSET)

#if (CHIP_REV == S6LM) || (CHIP_REV == CV2) || (CHIP_REV == CV22) || \
	(CHIP_REV == CV25) || (CHIP_REV == CV28) || (CHIP_REV == CV5)
#define AMBARELLA_SUPPORT_AST 0
#elif (CHIP_REV == N1) || (CHIP_REV == CV72) || (CHIP_REV == CV3AD685) || \
	(CHIP_REV == CV75) || (CHIP_REV == N1_655) || (CHIP_REV == CV7)
#define AMBARELLA_SUPPORT_AST 1
#else
#define AMBARELLA_SUPPORT_AST 2
#endif

/****************************************************/
#ifndef __ASSEMBLY__
/* ==========================================================================*/

/* used for Cortex boot Cortex */
extern u64 secondary_cortex_jump[];
// extern void bld_prepare_secondary_cortex(void);
// extern void bld_boot_secondary_cortex(void);
// extern void bld_reset_secondary_cpumask(u32 mask);

/* ==========================================================================*/
#endif
#endif

