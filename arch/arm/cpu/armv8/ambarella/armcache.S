/**
 * bld/arm64/armcache.S
 *
 * History:
 *    2015/11/20 - [Jorney Tu] AARCH64
 *
 * Copyright (c) 2026 Ambarella International LP
 *
 * License Identifier: AMBARELLA-2-Clause
 *
 */
#include <asm/macro.h>

.globl	_enable_icache
_enable_icache:
	switch_el x1, 3f, 2f, 1f
1:	mrs	x0, sctlr_el1
	orr	x0, x0, #0x1000
	msr	sctlr_el1, x0
	ret
2:	b	.
3:	mrs	x0, sctlr_el3
	orr	x0, x0, #0x1000
	msr	sctlr_el3, x0
	ret

.globl	_disable_icache
_disable_icache:
	switch_el x1, 3f, 2f, 1f
1:	mrs	x0, sctlr_el1
	bic	x0, x0, #0x1000
	msr	sctlr_el1, x0
	ret
2:	b	.
3:	mrs	x0, sctlr_el3
	bic	x0, x0, #0x1000
	msr	sctlr_el3, x0
	ret

.globl	_enable_dcache
_enable_dcache:
	switch_el x1, 3f, 2f, 1f
1:	mrs	x0, sctlr_el1
	orr	x0, x0, #0x4
	msr	sctlr_el1, x0
	dsb	ish
	isb
	ret
2:	b	.
3:	mrs	x0, sctlr_el3
	orr	x0, x0, #0x4
	msr	sctlr_el3, x0
	dsb	ish
	isb
	ret

.globl	_drain_write_buffer
_drain_write_buffer:
	ret

.globl	_disable_dcache
_disable_dcache:
	switch_el  x1, 3f, 2f, 1f
1:	mrs	x0, sctlr_el1
	bic	x0, x0, #0x4
	msr	sctlr_el1, x0
	ret
2:	b	.
3:	mrs	x0, sctlr_el3
	bic	x0, x0, #0x4
	msr	sctlr_el3, x0
	ret

.globl disable_mmu
disable_mmu:
	switch_el  x1, 3f, 2f, 1f
1:	mrs	x0, sctlr_el1
	bic	x0, x0, #0x1		/* disable MMU */
	msr	sctlr_el1, x0
	ret
2:	b	.
3:	mrs	x0, sctlr_el3
	bic	x0, x0, #0x1		/* disable MMU */
	msr	sctlr_el3, x0
	ret

.globl	_clean_flush_d_cache
_clean_flush_d_cache:
	stp     x8, x9, [sp, #-16]!
	invcln_all_cache x0, x1, x2, x3, x4, x5, x6, x7, x8, x9
	ldp     x8, x9, [sp], #16
	ret

.global _clean_flush_all_cache
_clean_flush_all_cache:
	/* invalid all icache */
	ic	ialluis
	isb	sy
	/* invalid all dcache */
	stp     x8, x9, [sp, #-16]!
	invcln_all_cache x0, x1, x2, x3, x4, x5, x6, x7, x8, x9
	ldp     x8, x9, [sp], #16
	ret

.global _flush_d_cache
_flush_d_cache:
	invalidate_all_cache x0, x1, x2, x3, x4, x5, x6, x7, x8, x9
	ret

.global _clean_d_cache
_clean_d_cache:
	clean_all_cache x0, x1, x2, x3, x4, x5, x6, x7, x8, x9
	ret

.globl _clean_d_cache_range
_clean_d_cache_range:
	add x1, x1, x0
	clean_invalidate_dcache cvac, x0, x1, x2, x3
	ret

.globl _flush_d_cache_range
_flush_d_cache_range:
	add x1, x1, x0
	clean_invalidate_dcache ivac, x0, x1, x2, x3
	ret

.globl _clean_flush_d_cache_range
_clean_flush_d_cache_range:
	add x1, x1, x0
	clean_invalidate_dcache  civac, x0, x1, x2, x3
	ret

.globl __invalid_icache_range
__invalid_icache_range:
	add x1, x1, x0
	invalidate_icache ivau, x0, x1, x2, x3
	ret

.globl flush_all_dcache
flush_all_dcache:
	/* cache flush flow, reference from armv8 trm */
	mrs x0, clidr_el1
	and w3, w0, #0x07000000
	lsr w3, w3, #23
	cbz w3, flushdone
	mov w10, #0
	mov w8, #1
loop1:
	add w2, w10, w10, lsr #1
	lsr w1, w0, w2
	and w1, w1, #0x7
	cmp w1, #2
	b.lt skip
	msr csselr_el1, x10
	isb
	mrs x1, ccsidr_el1
	and w2, w1, #7
	add w2, w2, #4
	ubfx w4, w1, #3, #10
	clz w5, w4
	lsl w9, w4, w5
	lsl w16, w8, w5
loop2:
	ubfx w7, w1, #13, #15
	lsl w7, w7, w2
	lsl w17, w8, w2
loop3:
	orr w11, w10, w9
	orr w11, w11, w7
	dc  isw, x11		/* isw -- invalid the dcache by set/way */
	subs w7, w7, w17
	b.ge loop3
	subs x9, x9, x16
	b.ge loop2
skip:	add w10, w10, #2
	cmp w3, w10
	dsb ish
	b.gt loop1
flushdone:
	ret
