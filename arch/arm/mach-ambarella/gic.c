#include <common.h>
#include <asm/arch/soc.h>
#include <asm/arch/gic.h>
#include <asm/io.h>

static void gic_dist_init(int irq_nr)
{
	int i;

	writel(0, GICD_REG(GICD_CTLR));

	/* set type: 0: level-sensitive; 1: edge triggered */
	for (i = 32; i < irq_nr; i += 16)
		writel(0x0, GICD_REG(GICD_ICFGR) + i / 4);

	/* set priority, default 0xa0 higher than cpu priority 0xf0 */
	for (i = 32; i < irq_nr; i += 4)
		writel(0xa0a0a0a0, GICD_REG(GICD_IPRIORITYR) + i);

	/* disable all spi */
	for (i = 32; i < irq_nr; i += 32)
		writel(~0, GICD_REG(GICD_ICENABLER) + i / 8);

	/* all interrupts only send to processor 0 as default */
	for (i = 32; i < irq_nr; i += 4)
		writel(0x01010101, GICD_REG(GICD_ITARGETSR) + i);

	/* set group1 */
	for (i = 32; i < irq_nr; i += 32)
		writel(~0, GICD_REG(GICD_IGROUPR) + i / 8);

	/* 16 supported priority levels, only interrupts with higher priority
	 * than the value in this register are signaled to the processor */
	writel(0xf0, GICC_REG(GICC_PMR));

	/* Enable Grp0 Grp1 interrupts forward */
	writel(0x3, GICD_REG(GICD_CTLR));
}

void gic_percpu_init(void)
{
	u32 el, bypass, i;

	__asm__ __volatile__("mrs %0, CurrentEL" : "=r" (el));

	/*
	 * Deal with the banked PPI and SGI interrupts:
	 * - disable all PPI interrupts
	 * - enable all SGI interrupts.
	 */
	writel(0xffff0000, GICD_REG(GICD_ICENABLER));
	writel(0x0000ffff, GICD_REG(GICD_ISENABLER));

	/* set SGI and PPI to group 1 */
	writel(~0, GICD_REG(GICD_IGROUPR));

	/* 16 supported priority levels, only interrupts with higher priority
	 * than the value in this register are signaled to the processor */
	writel(0xf0, GICC_REG(GICC_PMR));

	/* Set priority for PPI and SGI interrupts */
	for (i = 0; i < 32; i += 4)
		writel(0xa0a0a0a0, GICD_REG(GICD_IPRIORITYR) + i);

	/* TODO When the signaling of FIQs or IRQs by the CPU interface is disabled,
	 * Bypass signal is not signaled to the processor */
	bypass = readl(GICC_REG(GICC_CTRL));
	bypass &= 0x1e0;
	bypass |= 0x7;
	writel(bypass, GICC_REG(GICC_CTRL));

#if defined(CONFIG_MULTI_THREAD)
	if (el == 0xc)	/* set SGI1 to group 0 */
		clrbitsl(GICD_REG(GICD_IGROUPR), 1 << SGI_INT_IRQ1);
#endif
}

void irq_init(void)
{
	int irq_nr;

	disable_interrupts();

	/* Get interrupts nr supported */
	irq_nr = readl(GICD_REG(GICD_TYPER)) & 0x1f;
	irq_nr = (irq_nr + 1) * 32;
	if (irq_nr > 1020)
		irq_nr = 1020;

	/* Distributor init (SPI)*/
	gic_dist_init(irq_nr);

	/* CPU interface init (SGI & PPI) */
	gic_percpu_init();

	enable_interrupts();
}

void master_cpu_gic_setup(void)
{
	writel(~0, GICD_REG(GICD_IGROUPR));
}