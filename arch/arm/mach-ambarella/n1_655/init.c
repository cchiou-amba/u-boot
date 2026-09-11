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

/* boot/usbstrap/n1_655/n1_655.c */
static const struct pinmux_config init_pinmux[] = {
	/* UART APB */
#ifdef CONFIG_DEBUG_UART
	{44, 1}, {45, 1},
#endif
};

void plat_f_clk_config(void)
{
	/* UART APB divider */
	rct_writel(0x038, 1);
}

void plat_f_pinmux_config(void)
{
	pinmux_config_set_item(init_pinmux, sizeof(init_pinmux));
}

static void misc_pll_init(void)
{
	writel(0x0, CLK_SI_INPUT_MODE_REG);
}

void plat_f_soc_init(void)
{
	if (current_el() == 3) {
		/* NIC400 */
		for (ulong i = 0; i < 64U; i++) {
			writel(1, (0xfff1000008U + (i * 4)));
		}

		writel(0, 0xfff30000d0);
		writel(0, 0xfff30000d4);
		writel(0, 0xfff30000d8);
		writel(0, 0xfff30000dc);
		writel(0, 0xfff30000e0);
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
		writel(gd->relocaddr, 0xfff3000058);
		writel(gd->relocaddr, 0xfff3000060);

		clrbits_32(0xfff3000028,  (1 << 3) | (1 << 4) | (1 << 5));
	}
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

/* boot/amboot/src/bld/soc_fixup.c */
static void dram_set_arbiter(void)
{
	writel(0x00000509, 0xff08004000);    // 0x0000 - cortex0wr
	writel(0x00000509, 0xff08004004);    // 0x0004 - cortex0rd
	writel(0x00000509, 0xff08004008);    // 0x0008 - cortex1wr
	writel(0x00000509, 0xff0800400c);    // 0x000c - cortex1rd
	writel(0x00000509, 0xff08004010);    // 0x0010 - cortexr52wr
	writel(0x00000509, 0xff08004014);    // 0x0014 - cortexr52rd
	writel(0x0000041d, 0xff08004018);    // 0x0018 - usb3h0
	writel(0x0000041d, 0xff0800401c);    // 0x001c - pcie0wr
	writel(0x0000041d, 0xff08004020);    // 0x0020 - pcie1rd
	writel(0x00000408, 0xff08004024);    // 0x0024 - gpu
	writel(0x0000041d, 0xff08004028);    // 0x0028 - enet
	writel(0x0000041d, 0xff0800402c);    // 0x002c - periphls0
	writel(0x0000041d, 0xff08004030);    // 0x0030 - periphls1
	writel(0x0000070c, 0xff08004034);    // 0x0034 - swmaxi
	writel(0x0000070c, 0xff08004038);    // 0x0038 - hsm
	writel(0x0000060b, 0xff0800403c);    // 0x003c - nvp0maxi
	writel(0x0000060b, 0xff08004040);    // 0x0040 - fexmaxi
	writel(0x00000000, 0xff08004044);    // 0x0044 - gdma
	writel(0x00000428, 0xff08004048);    // 0x0048 - nvp0vmem
	writel(0x00000428, 0xff0800404c);    // 0x004c - fexdma
	writel(0x0001053a, 0xff08004050);    // 0x0050 - smemwr: allow_rw_switch_disable_cyc
	writel(0x0001053a, 0xff08004054);    // 0x0054 - smemrd: allow_rw_switch_disable_cyc
	writel(0x00000f0f, 0xff08004058);    // 0x0058 - orcme0
	writel(0x00000f0f, 0xff0800405c);    // 0x005c - orccode
	writel(0x0000070e, 0xff08004060);    // 0x0060 - ecchmpw
	writel(0x0000070e, 0xff08004064);    // 0x0064 - ecchevc
	writel(0x0000070e, 0xff08004068);    // 0x0068 - ecchrmf
	writel(0x00000e0e, 0xff0800406c);    // 0x006c - smemwrhi: disable allow_rw_switch_disable_cyc
	writel(0x0001070e, 0xff08004070);    // 0x0070 - smemrdhi: allow_rw_switch_disable_cyc
	writel(0x00000f0f, 0xff08004074);    // 0x0074 - ecchmpwhi
	writel(0x00000f0f, 0xff08004078);    // 0x0078 - ecchevchi
	writel(0x00000f0f, 0xff0800407c);    // 0x007c - ecchrmfhi

	writel(0x800, 0xff08004214);        // DRAM_THROTTLE_DLN (2048 cycles)

	writel(0x103, 0xff08004230);        // DRAM_USAGE_TARGET USB3 (1.17%)
	writel(0x108, 0xff08004234);        // DRAM_USAGE_TARGET PCIEWR (3.125%)
	writel(0x108, 0xff08004238);        // DRAM_USAGE_TARGET PCIERD (3.125)
	writel(0x20502, 0xff08004240);      // DRAM_USAGE_TARGET ENET0 (r:0.78%, w:0.78%)
	writel(0x103, 0xff08004244);        // DRAM_USAGE_TARGET PERIPHLS0 (1.17%)
	writel(0x103, 0xff08004248);        // DRAM_USAGE_TARGET PERIPHLS1 (1.17%)
	writel(0x160, 0xff08004260);        // DRAM_USAGE_TARGET NVP0_VMEM (37.5%)
	writel(0x104, 0xff08004264);        // DRAM_USAGE_TARGET FEXDMA (1.56%)
	writel(0x150, 0xff08004268);        // DRAM_USAGE_TARGET SMEM_WR (31.25%)
	writel(0x150, 0xff0800426c);        // DRAM_USAGE_TARGET SMEM_RD (31.25%)

	writel(0x140283f, 0xff08004800);    // RW_SWITCHING_CNT
					    // post_rw_switch_opp_type_disable_cycles = 63
					    // post_rw_switch_opp_type_disable_cycles_if_allowed = 160
					    // post_rw_switch_opp_type_throttle_cycles = 320
					    // post_grant_opp_req_type_throttle_cycles = 0
}

static void shm_setup(void)
{
	unsigned long shmem_base = 0xff00000000UL;
	u32 i = 0;
	for (i = 0; i < 12; ++ i) {
		writeb(0x05, shmem_base + 0x1e000 + i);
	}
	for (i = 0; i < 32; i += 4) {
		writel(0x20202020, shmem_base + 0x1e160 + i);
	}
}

void soc_fixup(void)
{
	if (current_el() == 3) {
		u32 core_freq = get_core_bus_freq_hz();

		if (POC_PERIPHERAL_CLK_MODE) {
			if (core_freq < 466000000)
				setbits_32(SYS_CONFIG_REG, POC_PERIPHERAL_CLK_MODE);
			else
				clrbits_32(SYS_CONFIG_REG, POC_PERIPHERAL_CLK_MODE);
		}

		dram_set_arbiter();
		shm_setup();
	}
}

int arch_cpu_init(void)
{
	soc_fixup();

	return 0;
}
