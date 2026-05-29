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
#include <asm/system.h>

const struct pinmux_config cv72_init_pinmux[] = {
	/* UART APB */
#ifdef CONFIG_DEBUG_UART
	{31, 1}, {32, 1},
#endif
};

void plat_f_clk_config(void)
{
	/* UART APB divider */
	rct_writel(0x038, 1);
}

void plat_f_pinmux_config(void)
{
	pinmux_config_set_item(cv72_init_pinmux, sizeof(cv72_init_pinmux));
}

static void misc_pll_init(void)
{
	writel(0x0, CLK_SI_INPUT_MODE_REG);
}

void plat_f_soc_init(void)
{
	int i;

	if (current_el() == 3) {
		/* configure all NIC400 master port to non-secure */
		for(i = 0; i < 63; i++)
			writel(1, 0xfff1000000 + 8 + i * 4);

		/* axi support security */
		writel(0, 0xfff3000090);
		writel(0, 0xfff3000094);
		writel(0, 0xfff3000098);
		writel(0, 0xfff30000a0);
	}

	misc_pll_init();
}

void plat_r_reset_cpu(void)
{
	rct_writel(0x068, 0xE);
	dsb();
	rct_writel(0x068, 0xF);
}

void cpu_secondary_init_r(void)
{
	if (current_el() == 3) {
		writel(gd->relocaddr, 0xfff3000050);
		clrbits_32(0xfff3000028, (1 << 3));
		asm volatile("sev");	/* wakeup the secondary-core in bst */
	}
}

void plat_device_init(void)
{
	/* USB device */
	rct_writel(0x50, 0x3006);
	/*
	 * FIXME: On CV3 and cv72, this reset operation leads to usb device
	 * halt when booting from USB.
	 */
	//setbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	//mdelay(1);
	//clrbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	//mdelay(1);
}

static void dram_set_arbiter(void)
{
	writel(0x00000509, 0xff08004000);    // 0x0000 - cortex0wr
	writel(0x00000509, 0xff08004004);    // 0x0004 - cortex0rd
	writel(0x0000041d, 0xff08004008);    // 0x0010 - usb3h0
	writel(0x0000041d, 0xff0800400c);    // 0x0014 - pcie0
	writel(0x0000041d, 0xff08004010);    // 0x0020 - enet
	writel(0x0000041d, 0xff08004014);    // 0x0024 - periphls0
	writel(0x0000041d, 0xff08004018);    // 0x0028 - periphls1
	writel(0x0000070c, 0xff0800401c);    // 0x008c - swmaxi
	writel(0x0000070c, 0xff08004020);    // 0x0090 - hsm
	writel(0x0000060b, 0xff08004024);    // 0x003c - nvp0maxi
	writel(0x00000000, 0xff08004028);    // 0x002c - gdma
	writel(0x00000428, 0xff0800402c);    // 0x0040 - nvp0vmem
	writel(0x0001953a, 0xff08004030);    // 0x0094 - smemwr: allow_rw_switch_disable_cyc
	writel(0x0001953a, 0xff08004034);    // 0x0098 - smemrd: allow_rw_switch_disable_cyc
	writel(0x00000f0f, 0xff08004038);    // 0x0030 - orcme0
	writel(0x00000f0f, 0xff0800403c);    // 0x0038 - orccode
	writel(0x0001970e, 0xff08004040);    // 0x009c - smemwrh: allow_rw_switch_disable_cyc
	writel(0x0001970e, 0xff08004044);    // 0x00a0 - smemrdh: allow_rw_switch_disable_cyc

	writel(0x800, 0xff08004208);        // DRAM_THROTTLE_DLN (2048 cycles)

	writel(0x102, 0xff08004214);        // DRAM_USAGE_TARGET USB3 (0.78%)
	writel(0x108, 0xff08004218);        // DRAM_USAGE_TARGET PCIE0 (3.125%)
	writel(0x20502, 0xff0800421c);      // DRAM_USAGE_TARGET ENET0 (r:0.78%, w:0.78%)
	writel(0x102, 0xff08004220);        // DRAM_USAGE_TARGET PERIPHLS0 (0.78%)
	writel(0x102, 0xff08004224);        // DRAM_USAGE_TARGET PERIPHLS1 (0.78%)
	writel(0x140, 0xff08004230);        // DRAM_USAGE_TARGET NVP0_VMEM (25.0%)
	writel(0x150, 0xff0800423c);        // DRAM_USAGE_TARGET SMEM_WR (31.25%)
	writel(0x150, 0xff08004240);        // DRAM_USAGE_TARGET SMEM_RD (31.25%)

	writel(0x140283f, 0xff08004800);    // RW_SWITCHING_CNT
					    // post_rw_switch_opp_type_disable_cycles = 63
					    // post_rw_switch_opp_type_disable_cycles_if_allowed = 160
					    // post_rw_switch_opp_type_throttle_cycles = 320
					    // post_grant_opp_req_type_throttle_cycles = 0
	/* limit cortex/gdma bandwidth */
	writel(0x7f7f0101, 0xff08000040);    // set cortex0wr/cortex0rd request credit to 1
	writel(0x7f007f7f, 0xff08000048);    // set gdma request credit to 0
}

static void soc_fixup(void)
{
	if (current_el() == 3) {

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

		dram_set_arbiter();
	}
}

int arch_cpu_init(void)
{
	soc_fixup();

	return 0;
}
