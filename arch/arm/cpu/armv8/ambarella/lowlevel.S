#include <asm/macro.h>
#include <config.h>

.global lowlevel_init
lowlevel_init:
    mov	x29, lr			/* Save LR */

#if defined(CONFIG_GICV2) || defined(CONFIG_GICV3)
    branch_if_slave x0, 1f
    ldr	x0, =GICD_BASE
    bl	gic_init_secure
1:
#if defined(CONFIG_GICV3)
    ldr	x0, =GICR_BASE
    bl	gic_init_secure_percpu
#elif defined(CONFIG_GICV2)
    ldr	x0, =GICD_BASE
    ldr	x1, =GICC_BASE
    bl	gic_init_secure_percpu
#endif

#endif

#ifdef CONFIG_ARMV8_MULTIENTRY
    branch_if_master x0, x1, 2f

    /*for multi-cluster case, if cluster N core 0, should return */
    lsl x0, x0, #48
    cbz x0, 2f

    /*
    * All slaves will enter EL2 and optionally EL1.
    */
    adr	x4, lowlevel_in_el2
    ldr	x5, =ES_TO_AARCH64
    bl	armv8_switch_to_el2

    lowlevel_in_el2:
#ifdef CONFIG_ARMV8_SWITCH_TO_EL1
    adr	x4, lowlevel_in_el1
    ldr	x5, =ES_TO_AARCH64
    bl	armv8_switch_to_el1

    lowlevel_in_el1:
#endif

#endif /* CONFIG_ARMV8_MULTIENTRY */

2:
    mov	lr, x29			/* Restore LR */
    ret