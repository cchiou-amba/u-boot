// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2006 Freescale Semiconductor
 * York Sun (yorksun@freescale.com)
 */

#include <common.h>
#include <command.h>

extern int do_mac(struct cmd_tbl *cmdtp, int flag, int argc,
		  char *const argv[]);

U_BOOT_CMD(
	mac, 3, 1,  do_mac,
	"display and program the persistent MAC address",
	"[show]            - display active ethaddr\n"
	"mac set <address> - validate, set, and persist ethaddr to boot0\n"
	"mac random        - generate, set, and persist random ethaddr to boot0"
);
