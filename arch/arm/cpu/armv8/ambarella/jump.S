/**
 * bld/arm64/jump.S
 *
 * History:
 *    2015/11/17 - [Jorney Tu] AARCH64
 *
 * Copyright (c) 2016 Ambarella International LP
 *
 * This file and its contents ("Software") are protected by intellectual
 * property rights including, without limitation, U.S. and/or foreign
 * copyrights. This Software is also the confidential and proprietary
 * information of Ambarella International LP and its licensors. You may not use, reproduce,
 * disclose, distribute, modify, or otherwise prepare derivative works of this
 * Software or any portion thereof except pursuant to a signed license agreement
 * or nondisclosure agreement with Ambarella International LP or its authorized affiliates.
 * In the absence of such an agreement, you agree to promptly notify and return
 * this Software to Ambarella International LP
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
 * MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL AMBARELLA INTERNATIONAL LP OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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

    /*Delay  */
    bl __delay
    
	msr	daifset, #0x3		/* disable interrupt */
	bl	_clean_flush_all_cache
	bl	_disable_icache
	bl	_disable_dcache
	bl	__disable_mmu

	switch_el x1, 3f, 2f, 1f
1:	/* make sure CPU is in EL1h mode */
	b	__switch_el1t_to_el1h	/* jump to Kernel, entry is stored in x29 */
	/* never run here */
2:	b	.
3:	bl	master_cpu_gic_setup	/* EL3 route setup */
	bl	__switch_to_el2
	b	__switch_to_el1		/* jump to Kernel, entry is stored in x29 */

