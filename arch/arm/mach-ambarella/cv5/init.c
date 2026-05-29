// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <asm/io.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>
#include <linux/delay.h>

const struct pinmux_config cv5_init_pinmux[] = {
	/* UART APB */
#ifdef CONFIG_DEBUG_UART
	{10, 1}, {11, 1},
#endif
};

void plat_f_clk_config(void)
{
	/* UART APB divider */
	rct_writel(0x038, 1);
}

void plat_f_pinmux_config(void)
{
	pinmux_config_set_item(cv5_init_pinmux, sizeof(cv5_init_pinmux));
}

void plat_f_soc_init(void)
{
	/* NIC400 */
	writel(1, 0x20f1000008);
	writel(1, 0x20f100000c);
	writel(1, 0x20f1000010);
	writel(1, 0x20f1000014);

	writel(1, 0x20f1000018);
	writel(1, 0x20f100001c);
	writel(1, 0x20f1000020);
	writel(1, 0x20f1000024);
	writel(1, 0x20f1000028);
}

void plat_r_reset_cpu(void)
{
	clrbits_32(0x20f2000028, (1 << 17) | (1 << 18) | (1 << 19));
	mdelay(1);
	setbits_32(0x20f2000028, (1 << 17) | (1 << 18) | (1 << 19));
	rct_writel(0x068, 0xE);
	dsb();
	rct_writel(0x068, 0xF);
}

void cpu_secondary_init_r(void)
{
	writel((gd->relocaddr >> 8), 0x20f2000068);

	clrbits_32(0x20f2000028, (1 << 17));
}

void plat_device_init(void)
{
	/* USB device */
	rct_writel(0x50, 0x3006);

	setbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	mdelay(1);
	clrbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	mdelay(1);
}

#ifndef CONFIG_AARCH64_TRUSTZONE
static void dram_set_arbiter(void)
{
	writel(0x002d0069, DRAMC_DRAM_BASE + 0x110);   // 0: AXI0/Cortex
	writel(0x002d0069, DRAMC_DRAM_BASE + 0x114);   // 1: AXI1/Cortex
	writel(0x002d0007, DRAMC_DRAM_BASE + 0x118);   // 2: OrcL2 Cache
	writel(0x002d0049, DRAMC_DRAM_BASE + 0x11c);   // 3: USB3
	writel(0x002d0049, DRAMC_DRAM_BASE + 0x120);   // 4: PCIE
	writel(0x002d004a, DRAMC_DRAM_BASE + 0x124);   // 5: ENET0
	writel(0x002d004a, DRAMC_DRAM_BASE + 0x128);   // 6: ENET1
	writel(0x002d004a, DRAMC_DRAM_BASE + 0x12c);   // 7: Flash DMA (FDMA)
	writel(0x002d004a, DRAMC_DRAM_BASE + 0x130);   // 8: SDAXI0
	writel(0x002d004a, DRAMC_DRAM_BASE + 0x134);   // 9: SDAXI1
	writel(0x002d004a, DRAMC_DRAM_BASE + 0x138);   // 10: SDAHB0
	writel(0x002d0009, DRAMC_DRAM_BASE + 0x13c);   // 11: USB2_device
	writel(0x002d0009, DRAMC_DRAM_BASE + 0x140);   // 12: ARM_DMA0/AHB
	writel(0x002d0009, DRAMC_DRAM_BASE + 0x144);   // 13: ARM_DMA1/AHB
	writel(0x002d0009, DRAMC_DRAM_BASE + 0x148);   // 14: CANC0
	writel(0x002d0003, DRAMC_DRAM_BASE + 0x14c);   // 15: GDMA
	writel(0x002d000b, DRAMC_DRAM_BASE + 0x150);   // 16: OrcMe0
	writel(0x002d000b, DRAMC_DRAM_BASE + 0x154);   // 17: OrcCode0
	writel(0x002d000b, DRAMC_DRAM_BASE + 0x158);   // 18: OrcMe1
	writel(0x002d000b, DRAMC_DRAM_BASE + 0x15c);   // 19: OrcCode1
	writel(0x002d0007, DRAMC_DRAM_BASE + 0x160);   // 20: OrcVp
	writel(0x103e1108, DRAMC_DRAM_BASE + 0x164);   // 21: SMEM_WR: Granularity = 2KB
	writel(0x103e1108, DRAMC_DRAM_BASE + 0x168);   // 22: SMEM_RD: Granularity = 2KB
	writel(0x002d0057, DRAMC_DRAM_BASE + 0x16c);   // 23: VMEM0
	writel(0x002d0005, DRAMC_DRAM_BASE + 0x170);   // 24: DBSE
	writel(0x103f220f, DRAMC_DRAM_BASE + 0x190);   // SMEM_WR hi_priority: Granularity = 2KB
	writel(0x103f220f, DRAMC_DRAM_BASE + 0x194);   // SMEM_RD hi_priority: Granularity = 2KB
	writel(0x800, DRAMC_DRAM_BASE + 0x198);        // DRAM_THROTTLE_DLN (2048 cycles)
	writel(0x103, DRAMC_DRAM_BASE + 0x1a8);        // DRAM_USAGE_TARGET USB3 (1.17%)
	writel(0x108, DRAMC_DRAM_BASE + 0x1ac);        // DRAM_USAGE_TARGET PCIE (3.12%)
	writel(0x20703, DRAMC_DRAM_BASE + 0x1b0);      // DRAM_USAGE_TARGET ENET0 (r:1.17%, w:1.17%)
	writel(0x20703, DRAMC_DRAM_BASE + 0x1b4);      // DRAM_USAGE_TARGET ENET1 (r:1.17%, w:1.17%)
	writel(0x103, DRAMC_DRAM_BASE + 0x1b8);        // DRAM_USAGE_TARGET FDMA (1.17%)
	writel(0x103, DRAMC_DRAM_BASE + 0x1bc);        // DRAM_USAGE_TARGET SDAXI0 (1.17%)
	writel(0x103, DRAMC_DRAM_BASE + 0x1c0);        // DRAM_USAGE_TARGET SDAXI1 (1.17%)
	writel(0x103, DRAMC_DRAM_BASE + 0x1c4);        // DRAM_USAGE_TARGET SDAHB0 (1.17%)
	writel(0x170, DRAMC_DRAM_BASE + 0x1f0);        // DRAM_USAGE_TARGET SMEM_WR (43.75%)
	writel(0x170, DRAMC_DRAM_BASE + 0x1f4);        // DRAM_USAGE_TARGET SMEM_RD (43.75%)
	writel(0x130, DRAMC_DRAM_BASE + 0x1f8);        // DRAM_USAGE_TARGET VMEM0 (18.5%)
	writel(0x01400C20, DRAMC_DRAM_BASE + 0x21c);   // rw throttle mandatory = 32
	writel(0x01401030, DRAMC_DRAM_BASE + 0x220);   // bank throttle mandatory = 48
	writel(0x88, DRAMC_DRAM_BASE + 0x224);         // DIE_BG_THROTTLE: different_die/bg_disable_cycles = 8
}
#else
static void dram_set_arbiter(void)
{
}
#endif

static void misc_pll_init(void)
{
	writel(0x0, CLK_SI_INPUT_MODE_REG);
}

void soc_fixup(void)
{
	/* ATF will set DRAM arbiter and update sysconfig if it's used */

#ifndef CONFIG_AARCH64_TRUSTZONE
	u32 core_freq = get_core_bus_freq_hz();

	if (POC_PERIPHERAL_CLK_MODE) {
		if (core_freq < 466000000)
			setbits_32(SYS_CONFIG_REG, POC_PERIPHERAL_CLK_MODE);
		else
			clrbits_32(SYS_CONFIG_REG, POC_PERIPHERAL_CLK_MODE);
	}

	if (POC_ORC_CLK_MODE) {
		if (core_freq < 500000000)
			clrbits_32(SYS_CONFIG_REG, POC_ORC_CLK_MODE);
		else
			setbits_32(SYS_CONFIG_REG, POC_ORC_CLK_MODE);
	}
#endif
	misc_pll_init();

	dram_set_arbiter();
}

int arch_cpu_init(void)
{
	soc_fixup();

	return 0;
}
