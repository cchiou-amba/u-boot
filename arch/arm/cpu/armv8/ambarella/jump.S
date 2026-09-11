/**
 * bld/arm64/jump.S
 *
 * History:
 *    2015/11/17 - [Jorney Tu] AARCH64
 *
 * Copyright (c) 2026 Ambarella International LP
 *
 * License Identifier: AMBARELLA-2-Clause
 *
 */

#include <asm/macro.h>
#include <asm/arch/gic.h>
.text

/*
 * Jump to the kernel
 * x0 - jump address
 * x1 - dtb address
 */
.globl	jump_to_kernel
jump_to_kernel:
	mov	x29, x0
	mov	x28, x1

	msr	daifset, #0x3		/* disable interrupt */
	bl	_clean_flush_all_cache
	bl	_disable_icache
	bl	_disable_dcache
	bl	__disable_mmu
	isb sy /* Instruction Synchornization Barrier */
	dsb sy /* Data Synchronization Barrier */

	switch_el x1, 3f, 2f, 1f
1:	/* make sure CPU is in EL1h mode */
	b	__switch_el1t_to_el1h	/* jump to Kernel, entry is stored in x29 */
	/* never run here */
2:	b	.
3:	bl	master_cpu_gic_setup	/* EL3 route setup */
	bl	__switch_to_el2
	b	__switch_to_el1		/* jump to Kernel, entry is stored in x29 */

