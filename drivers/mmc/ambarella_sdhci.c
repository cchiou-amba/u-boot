// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

//#define DEBUG
#include <common.h>
#include <dm.h>
#include <dt-structs.h>
#include <linux/err.h>
#include <linux/libfdt.h>
#include <malloc.h>
#include <mapmem.h>
#include <mmc.h>
#include <sdhci.h>
#include <clk.h>
#include <linux/delay.h>

#include <asm/arch/soc.h>
#include <asm/arch/misc.h>

/* 400KHz is max freq for card ID etc. Use that as min */
#define EMMC_MIN_FREQ	400000

struct ambarella_sdhc_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

static void ambarella_set_clock(struct sdhci_host *host, u32 div)
{
	u32 clock;

	clock = host->mmc->clock;
	/* ToDo : Use the dts node Index, use 0 firstly */
	rct_set_sd_pll(host->index, clock);
}

static int ambarella_sdhci_execute_tuning(struct mmc *mmc, u8 opcode)
{
	u32 ctrl, stat, data_size, old_int_enable_value, temp;
	int tuning_loop_counter = 40;//SDHCI_TUNING_LOOP_COUNT;
	ulong start;
	struct sdhci_host *host = mmc->priv;

	if (mmc->bus_width == 8)
		data_size = 128;
	else
		data_size = 64;

	old_int_enable_value = sdhci_readl(host, SDHCI_HOST_CONTROL2);
	sdhci_writel(host, SDHCI_INT_ENABLE, SDHCI_INT_DATA_AVAIL);

	temp = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	temp &= ~(SDHCI_CTRL_TUNED_CLK);
	sdhci_writew(host, SDHCI_HOST_CONTROL2, temp);
	temp = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	temp |= SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, SDHCI_HOST_CONTROL2, temp);

	do {
		if (tuning_loop_counter-- == 0)
			break;

		/* wait CMD line ready */
		while ((sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CMD_INHIBIT));

		sdhci_writel(host, SDHCI_ARGUMENT, 0);
		sdhci_writew(host, SDHCI_BLOCK_SIZE, data_size);
		//sdhci_writew(host, SDHCI_BLOCK_COUNT, 0x1);
		sdhci_writew(host, SDHCI_TRANSFER_MODE, SDHCI_TRNS_READ);
		sdhci_writew(host, SDHCI_COMMAND, opcode);

		start = get_timer(0);
		while(1) {
			if(get_timer(start) > 50) {
				printf("tuning timeout\n");
				return -1;
			}
			stat = sdhci_readl(host, SDHCI_INT_STATUS);
			if(stat & SDHCI_INT_DATA_AVAIL) {
				sdhci_writel(host, SDHCI_INT_STATUS, stat);
				break;
			}
		}

		if(opcode == MMC_CMD_SEND_TUNING_BLOCK)
			udelay(10000);
		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	} while (ctrl & SDHCI_CTRL_EXEC_TUNING);

	if (tuning_loop_counter < 0) {
		ctrl &= ~SDHCI_CTRL_TUNED_CLK;
		sdhci_writew(host, SDHCI_HOST_CONTROL2, ctrl);
	}

	if (!(ctrl & SDHCI_CTRL_TUNED_CLK)) {
		printf("%s:Tuning failed ,fall back to fixed timing\n", __func__);
		//clrbitsw(sd_base + SDHCI_HOST_CONTROL2, SDHCI_CTRL_TUNED_CLK);
		//clrbitsw(sd_base + SDHCI_HOST_CONTROL2, SDHCI_CTRL_EXEC_TUNING);
		temp = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		temp &= ~SDHCI_CTRL_TUNED_CLK;
		temp &= ~SDHCI_CTRL_EXEC_TUNING;
		sdhci_writew(host, SDHCI_HOST_CONTROL2, temp);
	}

	sdhci_writel(host, SDHCI_INT_ENABLE, old_int_enable_value);

	return 0;

}


static const struct sdhci_ops ambarella_sdhci_ops = {
	.platform_execute_tuning	= &ambarella_sdhci_execute_tuning,
	.set_clock	= &ambarella_set_clock,
};

static int ambarella_sdhci_bind(struct udevice *dev)
{
	struct ambarella_sdhc_plat *plat = dev_get_platdata(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static int ambarella_sdhci_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct ambarella_sdhc_plat *plat = dev_get_platdata(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	int ret, max_frequency;

	max_frequency = dev_read_u32_default(dev, "max-frequency", 0);
	host->bus_width = dev_read_u32_default(dev, "bus-width", 4);
	ret = dev_read_s32(dev, "index", &host->index);
	if (ret < 0) {
		debug("Missing index!\n");
		host->index = 0;
	}
	debug("mmc%d bus width %d-bit \n", host->index, host->bus_width);

	host->name = dev->name;
	host->ioaddr = dev_read_addr_ptr(dev);

	host->ops = &ambarella_sdhci_ops;

	host->max_clk = max_frequency;
	host->mmc = &plat->mmc;
	host->mmc->dev = dev;
	host->mmc->priv = host;
	upriv->mmc = host->mmc;

	ret = sdhci_setup_cfg(&plat->cfg, host, 0, EMMC_MIN_FREQ);
	if (ret)
		return ret;

	ret = sdhci_probe(dev);
	if (ret)
		return ret;

	return 0;
}

static const struct udevice_id ambarella_sdhci_ids[] = {
	{ .compatible = "ambarella,sdhci" },
	{ }
};

U_BOOT_DRIVER(ambarella_sdhci_drv) = {
	.name		= "ambarella_sdhci",
	.id		= UCLASS_MMC,
	.of_match	= ambarella_sdhci_ids,
	.ops		= &sdhci_ops,
	.bind		= ambarella_sdhci_bind,
	.probe		= ambarella_sdhci_probe,
	.priv_auto_alloc_size = sizeof(struct sdhci_host),
	.platdata_auto_alloc_size = sizeof(struct ambarella_sdhc_plat),
};

