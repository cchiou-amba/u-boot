// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

/* BOOT0 header information */
	b	reset
#if 0
	.space	0x44
#else
	.space  0x4c
	.word	_end - _start + 0x20000		// 0x20000 is reserved for dtb
	.word	CONFIG_SYS_TEXT_BASE
	.quad 	0x414D424F4F54
#endif
