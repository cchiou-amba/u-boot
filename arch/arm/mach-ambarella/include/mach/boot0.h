// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Ambarella International LP
 */

/* BOOT0 header information */

	b	reset
#if 0
	.space	0x44
#else
	.space  0x4c
	.quad	_end - _start + 0x20000
#endif
