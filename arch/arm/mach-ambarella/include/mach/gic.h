
/**
 * gic.h
 *
 * History:
 *    2015/12/1 - Jorney(qtu@ambarella.com) created file
 *
 * Copyright (c) 2026 Ambarella International LP
 *
 * License Identifier: AMBARELLA-2-Clause
 *
 */
#ifndef __MACH_GIC_H
#define __MACH_GIC_H

#include "soc.h"

/* ==========================================================================*/

#define GICD_REG(x)			(GICD_BASE + (x))
#define GICC_REG(x)			(GICC_BASE + (x))


/* GIC distribute */
#define GICD_CTLR			0x0000
#define GICD_TYPER			0x0004
#define GICD_IIDR			0x0008
#define GICD_STATUSR			0x0010
#define GICD_SETSPI_NSR			0x0040
#define GICD_CLRSPI_NSR			0x0048
#define GICD_SETSPI_SR			0x0050
#define GICD_CLRSPI_SR			0x0058
#define GICD_SEIR			0x0068
#define GICD_IGROUPR			0x0080
#define GICD_ISENABLER			0x0100
#define GICD_ICENABLER			0x0180
#define GICD_ISPENDR			0x0200
#define GICD_ICPENDR			0x0280
#define GICD_ISACTIVER			0x0300
#define GICD_ICACTIVER			0x0380
#define GICD_IPRIORITYR			0x0400
#define GICD_ICFGR			0x0C00
#define GICD_IGRPMODR			0x0D00
#define GICD_NSACR			0x0E00
#define GICD_IROUTER			0x6000
#define GICD_IDREGS			0xFFD0
#define GICD_PIDR2			0xFFE8
#define GICD_ITARGETSR			0x0800
#define GICD_SGIR			0x0F00
#define GICD_CPENDSGIR			0x0F10
#define GICD_SPENDSGIR			0x0F20

/* GIC cpu interface */
#define GICC_CTRL			0x00
#define GICC_PMR			0x04
#define GICC_BPR			0x08
#define GICC_IAR			0x0c
#define GICC_EOIR			0x10
#define GICC_RPR			0x14
#define GICC_HPPIR			0x18
#define GICC_ABPR			0x1c
#define GICC_AIAR			0x20
#define GICC_AEOIR			0x24
#define GICC_AHPPIR			0x28
#define GICC_APRN			0xd0
#define GICC_IIDR			0xfc


#define CTLR_ENABLE_G0_SHIFT		0
#define CTLR_ENABLE_G0_MASK		(0x1)
#define CTLR_ENABLE_G0_BIT		BIT(CTLR_ENABLE_G0_SHIFT)

#define CTLR_ENABLE_G1_SHIFT		1
#define CTLR_ENABLE_G1_MASK		(0x1)
#define CTLR_ENABLE_G1_BIT		BIT(CTLR_ENABLE_G1_SHIFT)

#define IRQ_BYP_DIS_GRP1		BIT(8)
#define FIQ_BYP_DIS_GRP1		BIT(7)
#define IRQ_BYP_DIS_GRP0		BIT(6)
#define FIQ_BYP_DIS_GRP0		BIT(5)

/* ==========================================================================*/
#define ICC_CTLR_EL3			S3_6_C12_C12_4
#define ICC_SRE_EL3			S3_6_C12_C12_5
#define ICC_IGRPEN1_EL3			S3_6_C12_C12_7

/* ==========================================================================*/
#define SGI_INT_VEC(x)			(x)
#define PPI_INT_VEC(x)			(x)
#define SPI_INT_VEC(x)			((x) + 32)

/* ==========================================================================*/
#define SGI_INT_IRQ0			SGI_INT_VEC(0)
#define SGI_INT_IRQ1			SGI_INT_VEC(1)
#define VIRT_TIMER_IRQ27		PPI_INT_VEC(27)


/* ==========================================================================*/
#ifndef __ASSEMBLY__
extern void master_cpu_gic_setup(void);

static inline void enable_interrupts(void)
{
	__asm__ __volatile__("msr daifclr, #0x3":::"memory");
}

static inline void disable_interrupts(void)
{
	__asm__ __volatile__("msr daifset, #0x3":::"memory");
}
#endif
#endif
