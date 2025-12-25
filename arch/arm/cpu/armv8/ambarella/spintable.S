#include <asm/arch/cortex.h>

.globl amba_secondary_cortex_wait
amba_secondary_cortex_wait:
    	mrs	x9, mpidr_el1
	ubfx	x0, x9, #0, #8
	tbz	x9, #24, 1f		/* MT bit */
	ubfx	x0, x9, #8, #8
1:
	cbnz	x0, 2f
    	ret

2:
	lsl	x0, x0, #3
	wfe
	isb
	dsb	sy
	adr	x1, secondary_cortex_jump
#if defined(CONFIG_AMBA_BOOT_SECONDARY_CLUSTER)
	ubfx	x2, x9, #16, #8		/* get cluster id */
	mov	x3, #(CORTEX_CORE_MAX_NUM * 8)
	madd	x1, x2, x3, x1
#endif
	ldr	x29, [x1, x0]
	cbz	x29, amba_secondary_cortex_wait
	ldr	x2, =0xdeadbeafdeadbeaf
	cmp	x29, x2
	beq	amba_secondary_cortex_wait

	bl	__secondary_gic_init
    	br	x29

/*************************************************/
.align 3
.globl secondary_cortex_jump
secondary_cortex_jump:
.rept CORTEX_CLUSTER_NUM * CORTEX_CORE_MAX_NUM
	.quad	0xdeadbeafdeadbeaf
.endr
