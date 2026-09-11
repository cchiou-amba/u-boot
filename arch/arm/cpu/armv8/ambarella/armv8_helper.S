/**
 * bld/arm64/armv8_switch.S
 *
 * History:
 *    2015/11/20 - [Jorney Tu] AARCH64
 *
 * Copyright (c) 2026 Ambarella International LP
 *
 * License Identifier: AMBARELLA-2-Clause
 *
 */

#include <asm/arch/cortex.h>
#include <asm/arch/gic.h>
#include <asm/arch/soc.h>
#include <asm/macro.h>

	.global __switch_to_el2
	.global __switch_to_el1
	.global __switch_el1t_to_el1h
	.global __secondary_gic_init
	.global __disable_mmu

	.global  el3_common_cpu_init
	.global amboot_psci_exit
	.global amboot_power_down_wfi
	.global Writer
	.global __cortex_a53_errata
	.global __cortex_a76_errata
	.global __delay

el3_common_cpu_init:
	ldr	x0, =0x30d00800
	msr	sctlr_el1, x0
	msr	actlr_el1, xzr

	ldr	x0, =0x30080000000
	msr	hcr_el2, x0
	ldr	x0, =0x33ff
	msr	cptr_el2, x0

	ldr	x0, =0x3
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr
	msr	vttbr_el2, xzr
	ldr	x0, =0x6
	msr	mdcr_el2, x0
	msr	hstr_el2, xzr
	msr	cnthp_ctl_el2, xzr

	mrs	x0, midr_el1
	msr	vpidr_el2, x0
	mrs	x0, mpidr_el1
	msr	vmpidr_el2, x0
	ret

amboot_psci_exit:
#if defined(AMBOOT_HANDLE_SMC_CALLING)
	bl	el3_common_cpu_init
	smc_restore_and_exit
#endif
	b	.

amboot_power_down_wfi:
	dsb	sy
	wfi
	b	.


__switch_to_el2:
	/* Non-secure EL0/EL1 | HVC | 64bit EL2 */
	mov	x0, #0x531
	msr	scr_el3, x0

	/* Disable coprocessor traps to EL3 */
	msr	cptr_el3, xzr
	/* Disable coprocessor traps to EL2 */
	mov	x0, #0x33ff
	msr	cptr_el2, x0

	/* clear counter-timer virtual offset */
	msr	cntvoff_el2, xzr

	ldr	x0, =0x30C50830
	msr	sctlr_el2, x0

	/* Migrate SP and VBAR */
	mov	x0, sp
	msr	sp_el2, x0
	mrs	x0, vbar_el3
	msr	vbar_el2, x0

	mov	x0, #0x3c9
	msr	spsr_el3, x0

#if defined(CONFIG_ARMV8_SWITCH_TO_EL1)
	msr	elr_el3, x30
#else
	mov	x0, x28       /* dtb address */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	msr	elr_el3, x29  /* x29 - kernel entry*/
#endif
	eret


__switch_to_el1:
	/* Enable EL1 access to timers */
	mrs	x0, cnthctl_el2
	orr	x0, x0, #0x3
	msr	cnthctl_el2, x0
	/* clear counter-timer virtual offset */
	msr	cntvoff_el2, xzr

	/* Initilize MIDR/MPIDR registers */
	mrs	x0, midr_el1
	msr	vpidr_el2, x0
	mrs	x0, mpidr_el1
	msr	vmpidr_el2, x0

	/* Disable coprocessor traps to EL2 */
	mov	x0, #0x33ff
	msr	cptr_el2, x0
	/* Disable AArch32 coprocessor traps to EL2 */
	msr	hstr_el2, xzr

	/* Enable FP/SIMD at EL1 */
	mov	x0, #(0x3 << 20)
	msr	cpacr_el1, x0

	ldr	x0, =0x30d00800
	msr	sctlr_el1, x0

	/* Migrate SP and VBAR */
	mov	x0, sp
	msr	sp_el1, x0
	mrs	x0, vbar_el2
	msr	vbar_el1, x0

#ifdef CONFIG_ARMV8_AARCH32
	/*AArch32 EL1 */
	mov	x0, #(0x1 << 29)
	mov	x1, #0x1d3
#else
	/* AArch64 EL1 */
	mov	x0, #(0x5 << 29)
	mov	x1, #0x3c5
#endif
	msr	hcr_el2, x0
	msr	spsr_el2, x1

#ifdef CONFIG_ARMV8_AARCH32
	mov	x0, xzr
	mov	x1, xzr
	mov	x2, x28			/* dtb address */
	mov	x3, xzr
#else
	mov	x0, x28			/* dtb address */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
#endif
	msr	elr_el2, x29
	eret


__switch_el1t_to_el1h:
#ifdef CONFIG_ARMV8_AARCH32
	ldr	x0, =SMCCC_SIP_CALL(AMBA_SIP_SWITCH_TO_AARCH32, AMBA_SIP_AARCH32_KERNEL)
	mov	x1, x29			/* kernel entry */
	mov	x2, x28			/* dtb address */
	mov	x3, xzr
	mov	x4, xzr
	smc	#0
	/* never return here */
	b	.
#else
	mov	x0, x28			/* dtb address */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	mov	x20, #0x3c5
#endif
	msr	spsr_el1, x20
	msr	elr_el1, x29
	eret

__secondary_gic_init:
	ldr	x0, =GICD_BASE
	mov	w1, 0xffffffff
	str	w1, [x0, GICD_IGROUPR]
	ldr	x0, =GICC_BASE
	mov	w1, #0x1e7
	str	w1, [x0, GICC_CTRL]
	mov	w1, #0xf0
	str	w1, [x0, GICC_PMR]
	ret


__disable_mmu:
	switch_el x1, 3f, 2f, 1f
1:	mrs	x0, sctlr_el1
	bic	x0, x0, #0x1
	msr	sctlr_el1, x0
	b	4f
2:	b	.
3:	mrs	x0, sctlr_el3
	bic	x0, x0, #0x1
	msr	sctlr_el3, x0
4:	ret


/*-----------------------------------------------------------------------------
 * Name:	Writer
 * Purpose:	Performs sequential write into memory, as fast as possible.
 * Params:
 *	x0 = address
 *	x1 = length, multiple of 256
 *	x2 = loop
 *-----------------------------------------------------------------------------*/
Writer:
	bic	x1, x1, #0x7f
	mov	x4, x0
	mov	x5, x1

	/*
	 * x4 = temp address
	 * x5 = temp length
	 */

.L01:
	mov	x0, x4
	mov	x1, x5

.L11:
	stp x3, x6, [x0]
	stp x3, x6, [x0, #16]
	stp x3, x6, [x0, #32]
	stp x3, x6, [x0, #48]
	stp x3, x6, [x0, #64]
	stp x3, x6, [x0, #80]
	stp x3, x6, [x0, #96]
	stp x3, x6, [x0, #112]
	stp x3, x6, [x0, #128]
	stp x3, x6, [x0, #144]
	stp x3, x6, [x0, #160]
	stp x3, x6, [x0, #176]
	stp x3, x6, [x0, #192]
	stp x3, x6, [x0, #208]
	stp x3, x6, [x0, #224]
	stp x3, x6, [x0, #240]
	add x0, x0, #256

	sub	x1, x1, #256
	cbnz	x1, .L11

	sub	x2, x2, #1
	cbnz	x2, .L01

	ret

/*
 * void __cortex_a53_errata(void)
 */
__cortex_a53_errata:
#ifdef CONFIG_SMP
	mrs	x0, S3_1_c15_c2_0	/* CPUACTLR */
	orr	x0, x0, #(1 << 44)	/* Errata 855873 */
	orr	x0, x0, #(1 << 29)	/* Errata 859372 */
	msr	S3_1_c15_c2_0, x0
#endif
	ret

/*
 * void __cortex_a76_errata(void)
 * CV5_a76_and_dsu version: r4p1
*/
__cortex_a76_errata:
apply_errata_a76_1946160_wa:
	mov	x0, #3
	msr	S3_6_C15_C8_0, x0
	ldr	x0, =0x10E3900002
	msr	S3_6_C15_C8_2, x0
	ldr	x0, =0x10FFF00083
	msr	S3_6_C15_C8_3, x0
	ldr	x0, =0x2001003FF
	msr	S3_6_C15_C8_1, x0

	mov	x0, #4
	msr	S3_6_C15_C8_0, x0
	ldr	x0, =0x10E3800082
	msr	S3_6_C15_C8_2, x0
	ldr	x0, =0x10FFF00083
	msr	S3_6_C15_C8_3, x0
	ldr	x0, =0x2001003FF
	msr	S3_6_C15_C8_1, x0

	mov	x0, #5
	msr	S3_6_C15_C8_0, x0
	ldr	x0, =0x10E3800200
	msr	S3_6_C15_C8_2, x0
	ldr	x0, =0x10FFF003E0
	msr	S3_6_C15_C8_3, x0
	ldr	x0, =0x2001003FF
	msr	S3_6_C15_C8_1, x0

	isb
	ret

/*
 * void __delay
 * delay some time
*/
__delay:
    ldr x0, =0xFFFFF
    ldr x1, =0x0

.L02:
    sub x0, x0, #1
    cmp x0, x1
    bne .L02

    ret
