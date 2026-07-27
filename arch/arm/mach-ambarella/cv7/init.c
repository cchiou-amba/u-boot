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

const struct pinmux_config cv7_init_pinmux[] = {
	/* UART APB */
#ifdef CONFIG_DEBUG_UART
	{30, 1}, {31, 1},
#endif
};

void plat_f_clk_config(void)
{
	/* UART APB divider */
	rct_writel(0x038, 1);
}

void plat_f_pinmux_config(void)
{
	pinmux_config_set_item(cv7_init_pinmux, sizeof(cv7_init_pinmux));
}

static void misc_pll_init(void)
{
	writel(0x0, CLK_SI_INPUT_MODE_REG);
}
void plat_f_soc_init(void)
{
	int i;

printf("plat_f_soc_init\n");
	if (current_el() == 3) {
		/* configure all NIC400 master port to non-secure */
		for(i = 0; i < 63; i++)
			writel(1, 0x20f1000000 + 8 + i * 4);

		/* axi support security */
		writel(0, 0x20f20000d0);
		writel(0, 0x20f20000d4);
		writel(0, 0x20f20000d8);
		writel(0, 0x20f20000dc);
		writel(0, 0x20f20000e0);
printf("plat_f_soc_init2\n");
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
		writel(gd->relocaddr, 0x20f2000050);
		writel(gd->relocaddr, 0x20f2000058);
		writel(gd->relocaddr, 0x20f2000060);

		clrbits_32(0x20f2000028, (1 << 3));
		writel(gd->relocaddr, 0x20f2000050);
		writel(gd->relocaddr, 0x20f2000058);
		writel(gd->relocaddr, 0x20f2000060);

		clrbits_32(0x20f2000028,  (1 << 3) | (1 << 4) | (1 << 5));
	}
}

void plat_device_init(void)
{
	/* USB device */
	rct_writel(0x50, 0x3006);
	/*
	 * FIXME: On CV3 and cv7, this reset operation leads to usb device
	 * halt when booting from USB.
	 */
	//setbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	//mdelay(1);
	//clrbits_32(AHBSP_NS_BASE + 0x12c, 0x1);
	//mdelay(1);
}

static void dram_set_arbiter(void)
{
	writel(0x00000509, 0x3000004000);    // 0x0000 - cortex0wr
	writel(0x00000509, 0x3000004004);    // 0x0004 - cortex0rd
	writel(0x0000041d, 0x3000004008);    // 0x0008 - usb3h0
	writel(0x0000041d, 0x300000400c);    // 0x000c - pcie0
	writel(0x0000041d, 0x3000004010);    // 0x0010 - enet
	writel(0x0000041d, 0x3000004014);    // 0x0014 - periphls0
	writel(0x0000041d, 0x3000004018);    // 0x0018 - periphls1
	writel(0x0000070c, 0x300000401c);    // 0x001c - nvp0maxi
	writel(0x00000000, 0x3000004020);    // 0x0020 - gdma
	writel(0x00000428, 0x3000004024);    // 0x0024 - nvp0vmem
	writel(0x0001053a, 0x3000004028);    // 0x0028 - smemwr: allow_rw_switch_disable_cyc
	writel(0x0001053a, 0x300000402c);    // 0x002c - smemrd: allow_rw_switch_disable_cyc
	writel(0x00000f0f, 0x3000004030);    // 0x0030 - orcme0
	writel(0x00000f0f, 0x3000004034);    // 0x0034 - orcme1
	writel(0x00000f0f, 0x3000004038);    // 0x0038 - orccode
	writel(0x00000e0e, 0x300000403c);    // 0x003c - smemwrh: allow_rw_switch_disable_cyc
	writel(0x0001070e, 0x3000004040);    // 0x0040 - smemrdh: allow_rw_switch_disable_cyc

	writel(0x800, 0x3000004208);         // DRAM_THROTTLE_DLN (2048 cycles)

	writel(0x102, 0x3000004214);         // DRAM_USAGE_TARGET USB3 (0.78%)
	writel(0x102, 0x3000004218);         // DRAM_USAGE_TARGET PCIE0 (0.78%)
	writel(0x20502, 0x300000421c);       // DRAM_USAGE_TARGET ENET0 (r:0.78%, w:0.78%)
	writel(0x102, 0x3000004220);         // DRAM_USAGE_TARGET PERIPHLS0 (0.78%)
	writel(0x102, 0x3000004224);         // DRAM_USAGE_TARGET PERIPHLS1 (0.78%)
	writel(0x140, 0x3000004230);         // DRAM_USAGE_TARGET NVP0_VMEM (25.0%)
	writel(0x160, 0x3000004234);         // DRAM_USAGE_TARGET SMEM_WR (37.5%)
	writel(0x160, 0x3000004238);         // DRAM_USAGE_TARGET SMEM_RD (37.5%)

	writel(0x140283f, 0x3000004800);     // RW_SWITCHING_CNT
					     // post_rw_switch_opp_type_disable_cycles = 63
					     // post_rw_switch_opp_type_disable_cycles_if_allowed = 160
					     // post_rw_switch_opp_type_throttle_cycles = 320
					     // post_grant_opp_req_type_throttle_cycles = 0
	/* limit cortex/gdma bandwidth */
	writel(0x7f7f1f1f, 0x3000000040);    // set cortex0wr/cortex0rd request credit to 0x1f
	writel(0x7f7f7f00, 0x3000000048);    // set gdma request credit to 0
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
