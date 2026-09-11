/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * include/asm-arm/macro.h
 *
 * Copyright (C) 2009 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 */

#ifndef __ASM_ARM_MACRO_H__
#define __ASM_ARM_MACRO_H__

#ifdef CONFIG_ARM64
#include <asm/system.h>
#endif

#ifdef __ASSEMBLY__

/*
 * These macros provide a convenient way to write 8, 16 and 32 bit data
 * to any address.
 * Registers r4 and r5 are used, any data in these registers are
 * overwritten by the macros.
 * The macros are valid for any ARM architecture, they do not implement
 * any memory barriers so caution is recommended when using these when the
 * caches are enabled or on a multi-core system.
 */

.macro	write32, addr, data
	ldr	r4, =\addr
	ldr	r5, =\data
	str	r5, [r4]
.endm

.macro	write16, addr, data
	ldr	r4, =\addr
	ldrh	r5, =\data
	strh	r5, [r4]
.endm

.macro	write8, addr, data
	ldr	r4, =\addr
	ldrb	r5, =\data
	strb	r5, [r4]
.endm

/*
 * This macro generates a loop that can be used for delays in the code.
 * Register r4 is used, any data in this register is overwritten by the
 * macro.
 * The macro is valid for any ARM architeture. The actual time spent in the
 * loop will vary from CPU to CPU though.
 */

.macro	wait_timer, time
	ldr	r4, =\time
1:
	nop
	subs	r4, r4, #1
	bcs	1b
.endm

#ifdef CONFIG_ARM64
/*
 * Register aliases.
 */
lr	.req	x30

/*
 * Branch according to exception level
 */
.macro	switch_el, xreg, el3_label, el2_label, el1_label
	mrs	\xreg, CurrentEL
	cmp	\xreg, 0xc
	b.eq	\el3_label
	cmp	\xreg, 0x8
	b.eq	\el2_label
	cmp	\xreg, 0x4
	b.eq	\el1_label
.endm

/*
 * Branch if current processor is a Cortex-A57 core.
 */
.macro	branch_if_a57_core, xreg, a57_label
	mrs	\xreg, midr_el1
	lsr	\xreg, \xreg, #4
	and	\xreg, \xreg, #0x00000FFF
	cmp	\xreg, #0xD07		/* Cortex-A57 MPCore processor. */
	b.eq	\a57_label
.endm

/*
 * Branch if current processor is a Cortex-A53 core.
 */
.macro	branch_if_a53_core, xreg, a53_label
	mrs	\xreg, midr_el1
	lsr	\xreg, \xreg, #4
	and	\xreg, \xreg, #0x00000FFF
	cmp	\xreg, #0xD03		/* Cortex-A53 MPCore processor. */
	b.eq	\a53_label
.endm

/*
 * Branch if current processor is a slave,
 * choose processor with all zero affinity value as the master.
 */
.macro	branch_if_slave, xreg, slave_label
#ifdef CONFIG_ARMV8_MULTIENTRY
	/* NOTE: MPIDR handling will be erroneous on multi-cluster machines */
	mrs	\xreg, mpidr_el1
	tst	\xreg, #0xff		/* Test Affinity 0 */
	b.ne	\slave_label
	lsr	\xreg, \xreg, #8
	tst	\xreg, #0xff		/* Test Affinity 1 */
	b.ne	\slave_label
	lsr	\xreg, \xreg, #8
	tst	\xreg, #0xff		/* Test Affinity 2 */
	b.ne	\slave_label
	lsr	\xreg, \xreg, #16
	tst	\xreg, #0xff		/* Test Affinity 3 */
	b.ne	\slave_label
#endif
.endm

/*
 * Branch if current processor is a master,
 * choose processor with all zero affinity value as the master.
 */
.macro	branch_if_master, xreg1, xreg2, master_label
#ifdef CONFIG_ARMV8_MULTIENTRY
	/* NOTE: MPIDR handling will be erroneous on multi-cluster machines */
	mrs	\xreg1, mpidr_el1
	lsr	\xreg2, \xreg1, #32
	lsl	\xreg2, \xreg2, #32
	lsl	\xreg1, \xreg1, #40
	lsr	\xreg1, \xreg1, #40
	orr	\xreg1, \xreg1, \xreg2
	cbz	\xreg1, \master_label
#else
	b 	\master_label
#endif
.endm

/*
 * Switch from EL3 to EL2 for ARMv8
 * @ep:     kernel entry point
 * @flag:   The execution state flag for lower exception
 *          level, ES_TO_AARCH64 or ES_TO_AARCH32
 * @tmp:    temporary register
 *
 * For loading 32-bit OS, x1 is machine nr and x2 is ftaddr.
 * For loading 64-bit OS, x0 is physical address to the FDT blob.
 * They will be passed to the guest.
 */
.macro armv8_switch_to_el2_m, ep, flag, tmp
	msr	cptr_el3, xzr		/* Disable coprocessor traps to EL3 */
	mov	\tmp, #CPTR_EL2_RES1
	msr	cptr_el2, \tmp		/* Disable coprocessor traps to EL2 */

	/* Initialize Generic Timers */
	msr	cntvoff_el2, xzr

	/* Initialize SCTLR_EL2
	 *
	 * setting RES1 bits (29,28,23,22,18,16,11,5,4) to 1
	 * and RES0 bits (31,30,27,26,24,21,20,17,15-13,10-6) +
	 * EE,WXN,I,SA,C,A,M to 0
	 */
	ldr	\tmp, =(SCTLR_EL2_RES1 | SCTLR_EL2_EE_LE |\
			SCTLR_EL2_WXN_DIS | SCTLR_EL2_ICACHE_DIS |\
			SCTLR_EL2_SA_DIS | SCTLR_EL2_DCACHE_DIS |\
			SCTLR_EL2_ALIGN_DIS | SCTLR_EL2_MMU_DIS)
	msr	sctlr_el2, \tmp

	mov	\tmp, sp
	msr	sp_el2, \tmp		/* Migrate SP */
	mrs	\tmp, vbar_el3
	msr	vbar_el2, \tmp		/* Migrate VBAR */

	/* Check switch to AArch64 EL2 or AArch32 Hypervisor mode */
	cmp	\flag, #ES_TO_AARCH32
	b.eq	1f

	/*
	 * The next lower exception level is AArch64, 64bit EL2 | HCE |
	 * RES1 (Bits[5:4]) | Non-secure EL0/EL1.
	 * and the SMD depends on requirements.
	 */
#ifdef CONFIG_ARMV8_PSCI
	ldr	\tmp, =(SCR_EL3_RW_AARCH64 | SCR_EL3_HCE_EN |\
			SCR_EL3_RES1 | SCR_EL3_NS_EN)
#else
	ldr	\tmp, =(SCR_EL3_RW_AARCH64 | SCR_EL3_HCE_EN |\
			SCR_EL3_SMD_DIS | SCR_EL3_RES1 |\
			SCR_EL3_NS_EN)
#endif

#ifdef CONFIG_ARMV8_EA_EL3_FIRST
	orr	\tmp, \tmp, #SCR_EL3_EA_EN
#endif
	msr	scr_el3, \tmp

	/* Return to the EL2_SP2 mode from EL3 */
	ldr	\tmp, =(SPSR_EL_DEBUG_MASK | SPSR_EL_SERR_MASK |\
			SPSR_EL_IRQ_MASK | SPSR_EL_FIQ_MASK |\
			SPSR_EL_M_AARCH64 | SPSR_EL_M_EL2H)
	msr	spsr_el3, \tmp
	msr	elr_el3, \ep
	/* Clear mdcr_el3 and spsr_el1 which have unknown reset value, cv7 need this  */
	msr	spsr_el2, xzr
	msr	spsr_el1, xzr
	msr	mdcr_el3, xzr
	eret

1:
	/*
	 * The next lower exception level is AArch32, 32bit EL2 | HCE |
	 * SMD | RES1 (Bits[5:4]) | Non-secure EL0/EL1.
	 */
	ldr	\tmp, =(SCR_EL3_RW_AARCH32 | SCR_EL3_HCE_EN |\
			SCR_EL3_SMD_DIS | SCR_EL3_RES1 |\
			SCR_EL3_NS_EN)
	msr	scr_el3, \tmp

	/* Return to AArch32 Hypervisor mode */
	ldr     \tmp, =(SPSR_EL_END_LE | SPSR_EL_ASYN_MASK |\
			SPSR_EL_IRQ_MASK | SPSR_EL_FIQ_MASK |\
			SPSR_EL_T_A32 | SPSR_EL_M_AARCH32 |\
			SPSR_EL_M_HYP)
	msr	spsr_el3, \tmp
	msr     elr_el3, \ep
	eret
.endm

/*
 * Switch from EL2 to EL1 for ARMv8
 * @ep:     kernel entry point
 * @flag:   The execution state flag for lower exception
 *          level, ES_TO_AARCH64 or ES_TO_AARCH32
 * @tmp:    temporary register
 *
 * For loading 32-bit OS, x1 is machine nr and x2 is ftaddr.
 * For loading 64-bit OS, x0 is physical address to the FDT blob.
 * They will be passed to the guest.
 */
.macro armv8_switch_to_el1_m, ep, flag, tmp
	/* Initialize Generic Timers */
	mrs	\tmp, cnthctl_el2
	/* Enable EL1 access to timers */
	orr	\tmp, \tmp, #(CNTHCTL_EL2_EL1PCEN_EN |\
		CNTHCTL_EL2_EL1PCTEN_EN)
	msr	cnthctl_el2, \tmp
	msr	cntvoff_el2, xzr

	/* Initilize MPID/MPIDR registers */
	mrs	\tmp, midr_el1
	msr	vpidr_el2, \tmp
	mrs	\tmp, mpidr_el1
	msr	vmpidr_el2, \tmp

	/* Disable coprocessor traps */
	mov	\tmp, #CPTR_EL2_RES1
	msr	cptr_el2, \tmp		/* Disable coprocessor traps to EL2 */
	msr	hstr_el2, xzr		/* Disable coprocessor traps to EL2 */
	mov	\tmp, #CPACR_EL1_FPEN_EN
	msr	cpacr_el1, \tmp		/* Enable FP/SIMD at EL1 */

	/* SCTLR_EL1 initialization
	 *
	 * setting RES1 bits (29,28,23,22,20,11) to 1
	 * and RES0 bits (31,30,27,21,17,13,10,6) +
	 * UCI,EE,EOE,WXN,nTWE,nTWI,UCT,DZE,I,UMA,SED,ITD,
	 * CP15BEN,SA0,SA,C,A,M to 0
	 */
	ldr	\tmp, =(SCTLR_EL1_RES1 | SCTLR_EL1_UCI_DIS |\
			SCTLR_EL1_EE_LE | SCTLR_EL1_WXN_DIS |\
			SCTLR_EL1_NTWE_DIS | SCTLR_EL1_NTWI_DIS |\
			SCTLR_EL1_UCT_DIS | SCTLR_EL1_DZE_DIS |\
			SCTLR_EL1_ICACHE_DIS | SCTLR_EL1_UMA_DIS |\
			SCTLR_EL1_SED_EN | SCTLR_EL1_ITD_EN |\
			SCTLR_EL1_CP15BEN_DIS | SCTLR_EL1_SA0_DIS |\
			SCTLR_EL1_SA_DIS | SCTLR_EL1_DCACHE_DIS |\
			SCTLR_EL1_ALIGN_DIS | SCTLR_EL1_MMU_DIS)
	msr	sctlr_el1, \tmp

	mov	\tmp, sp
	msr	sp_el1, \tmp		/* Migrate SP */
	mrs	\tmp, vbar_el2
	msr	vbar_el1, \tmp		/* Migrate VBAR */

	/* Check switch to AArch64 EL1 or AArch32 Supervisor mode */
	cmp	\flag, #ES_TO_AARCH32
	b.eq	1f

	/* Initialize HCR_EL2 */
	ldr	\tmp, =(HCR_EL2_RW_AARCH64 | HCR_EL2_HCD_DIS)
	msr	hcr_el2, \tmp

	/* Return to the EL1_SP1 mode from EL2 */
	ldr	\tmp, =(SPSR_EL_DEBUG_MASK | SPSR_EL_SERR_MASK |\
			SPSR_EL_IRQ_MASK | SPSR_EL_FIQ_MASK |\
			SPSR_EL_M_AARCH64 | SPSR_EL_M_EL1H)
	msr	spsr_el2, \tmp
	msr     elr_el2, \ep
	eret

1:
	/* Initialize HCR_EL2 */
	ldr	\tmp, =(HCR_EL2_RW_AARCH32 | HCR_EL2_HCD_DIS)
	msr	hcr_el2, \tmp

	/* Return to AArch32 Supervisor mode from EL2 */
	ldr	\tmp, =(SPSR_EL_END_LE | SPSR_EL_ASYN_MASK |\
			SPSR_EL_IRQ_MASK | SPSR_EL_FIQ_MASK |\
			SPSR_EL_T_A32 | SPSR_EL_M_AARCH32 |\
			SPSR_EL_M_SVC)
	msr     spsr_el2, \tmp
	msr     elr_el2, \ep
	eret
.endm

#if defined(CONFIG_GICV3)
.macro gic_wait_for_interrupt_m xreg1
0 :	wfi
	mrs     \xreg1, ICC_IAR1_EL1
	msr     ICC_EOIR1_EL1, \xreg1
	cbnz    \xreg1, 0b
.endm
#elif defined(CONFIG_GICV2)
.macro gic_wait_for_interrupt_m xreg1, wreg2
0 :	wfi
	ldr     \wreg2, [\xreg1, GICC_AIAR]
	str     \wreg2, [\xreg1, GICC_AEOIR]
	and	\wreg2, \wreg2, #0x3ff
	cbnz    \wreg2, 0b
.endm
#endif

.macro	operate_all_cache, op, lvl, type, line, way, set, \
				wayp, wayv, cp_val, dc_val, tmp
	dsb	sy

	mov	\lvl, #0		/* lvl = level variable */

/* loop_level */
1:
	mrs	\cp_val, clidr_el1	/* read clidr_el1 to check Ctype */
	lsl	\tmp, \lvl, #1
	add	\tmp, \tmp, \lvl	/* tmp = lvl << 1 + lvl = lvl * 3 */
	lsr	\type, \cp_val, \tmp
	and	\type, \type, #7	/* Cache type */
	cbz	\type, 5f		/* return if no cache, no need to check upper layer */
	cmp	\type, #2
	b.lt	4f			/* skip if icache */

	/*---------- START Specified Level Operation -----------*/

	lsl	\tmp, \lvl, #1
	msr	csselr_el1, \tmp	/* Selects the current Cache level */
	isb				/* make sure csselr_el1 has taken effect */

	mrs	\cp_val, ccsidr_el1
	and	\line, \cp_val, #7
	add	\line, \line, #4	/* line = log2(cache line size) */
	lsr	\way, \cp_val, #3
	and	\way, \way, #0x3ff	/* way = ways - 1 */
	clz	\wayp, \way		/* wayp = way position in DC ISW/CSW/CISW */
	sub	\wayp, \wayp, #32	/* wayp = 64 - 32 - A = 32 - log2(ways) */
	lsr	\set, \cp_val, #13
	and	\set, \set, #0x7fff	/* set = sets - 1 */

/* loop_set */
2:
	mov	\wayv, \way		/* wayv = way variable */

/* loop_way */
3:
	lsl	\dc_val, \wayv, \wayp
	orr	\dc_val, \dc_val, \lvl, lsl #1
	lsl	\tmp, \set, \line
	orr	\dc_val, \dc_val, \tmp
	dc	\op, \dc_val

	subs	\wayv, \wayv, #1
	b.ge	3b			/* back to loop_way */
	subs	\set, \set, #1
	b.ge	2b			/* back to loop_set */

	/*---------- END Specified Level Operation -----------*/
4:
	add	\lvl, \lvl, #1		/* increment cache level */
	b	1b			/* back to loop_level */

5:
	mov	\tmp, #0
	msr	csselr_el1, \tmp	/* restore csselr_el1 */
	dsb	sy
	isb
.endm

.macro	invalidate_all_cache, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, \
				tmp7, tmp8, tmp9
	operate_all_cache isw, \tmp0, \tmp1, \tmp2, \tmp3, \tmp4, \
				 \tmp5, \tmp6, \tmp7, \tmp8, \tmp9
.endm

.macro	clean_all_cache, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, \
				tmp7, tmp8, tmp9
	operate_all_cache csw, \tmp0, \tmp1, \tmp2, \tmp3, \tmp4, \
				\tmp5, \tmp6, \tmp7, \tmp8, \tmp9
.endm

.macro	invcln_all_cache, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, \
				tmp7, tmp8, tmp9
	operate_all_cache cisw, \tmp0, \tmp1, \tmp2, \tmp3, \tmp4, \
				\tmp5, \tmp6, \tmp7, \tmp8, \tmp9
.endm

.macro	invalidate_all_tlb
	armv8_switch_el x1, 1f, 2f, 3f
1:	tlbi	vmalle1
	b	0f
2:	b	.
3:	tlbi	alle3is
0:	dsb	ish
	isb
.endm

.macro  clean_invalidate_dcache, ops, addr, end, tmp0, tmp1

	mrs	\tmp1, ctr_el0
	lsr	\tmp1, \tmp1, #16
	and	\tmp1, \tmp1, #0xf
	mov	\tmp0, #4
	lsl	\tmp0, \tmp0, \tmp1		/* cache line size */

	sub	\tmp1, \tmp0, #1
	bic	\addr, \addr, \tmp1
1: 	dc	\ops, \addr
	add	\addr, \addr, \tmp0
	cmp	\addr, \end
	b.lo	1b
	dsb	sy
.endm

.macro  invalidate_icache, ops, addr, end, tmp0, tmp1

	mrs	\tmp1, ctr_el0
	and	\tmp1, \tmp1, #0xf
	mov	\tmp0, #4
	lsl	\tmp0, \tmp0, \tmp1		/* cache line size */

	sub	\tmp1, \tmp0, #1
	bic	\addr, \addr, \tmp1
1: 	ic	\ops, \addr
	add	\addr, \addr, \tmp0
	cmp	\addr, \end
	b.lo	1b
	dsb	ish
	isb
.endm

#endif /* CONFIG_ARM64 */

#endif /* __ASSEMBLY__ */
#endif /* __ASM_ARM_MACRO_H__ */
