// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Ambarella Reset driver for Cadence PHY.
 *
 * Copyright (C) 2018-2028, Ambarella, Inc.
 */
#define DEBUG 1
#include <common.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <reset-uclass.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <regmap.h>
#include <linux/delay.h>
#include <dm/device_compat.h>
#include <dm/of_access.h>
#include <dm/devres.h>
#include <dm/ofnode.h>
#include <syscon.h>

/* USB32_PMA_CTRL_REG */
#define USB32_PMA_CMN_REFCLK_DIG_DIV_4		BIT(12)
#define USB32_PMA_CMN_REFCLK_DIG_DIV_MASK	GENMASK(12, 11)

/* USB32C_CTRL_REG */
#define USB32C_SOFT_RESET			BIT(0)

/* USB32P_CTRL_REG */
#define USB32P_PHY_RESET			BIT(0)
#define USB32P_APB_RESET			BIT(1)

/* PCIE_PMA_CTRL_REG */
#define PCIE_PMA_CMN_REFCLK_DIG_DIV_4		BIT(21)
#define PCIE_PMA_CMN_REFCLK_DIG_DIV_MASK	GENMASK(21, 20)

/* PCIEP_CTRL_REG - Stupid register definition for PCIe! */
#define PCIEP_APB_RESET(id)			BIT((id) * 2)
#define PCIEP_PHY_RESET(id)			BIT(((id) * 2) + 1)

/* PCIEC_CTRL1_REG */
#define PCIEC_CONFIG_EN				BIT(25)
#define PCIEC_LINK_TRAIN_EN			BIT(22)
#define PCIEC_MODE_SELECT_RP			BIT(21)
#define PCIEC_APB_CORE_RATIO_4			(4 << 9)
#define PCIEC_APB_CORE_RATIO_MASK		GENMASK(13, 9)
#define PCIEC_MISC_RESET			GENMASK(7, 0)
#define PCIEC_GEN_RESET				GENMASK(18, 17)
#define PCIEC_LANE_RESET			GENMASK(20, 19)
#define LANE_COUNT_X4 BIT(20)
#define LANE_COUNT_X2 BIT(19)
#define LANE_COUNT_X1 0

#define GEN3 BIT(18)
#define GEN2 BIT(17)
#define GEN1 0

#define PCI_TPVPERL_DELAY_MS    100

enum {
	PMA_CTRL_REG = 0,
	P_CTRL_REG,
	NUM_REG,
};

struct amba_phyrst {
	struct reset_ctl rst;
	struct regmap *regmap;
	struct regmap *c_regmap;
	u32 offset[NUM_REG];
	u32 c_offset;
	u32 phy_id;
	void *data;
	struct device_node *np;
};

#define msleep(a)	udelay(a * 1000)

struct amba_phyrst_of_data {
	int (*init)(struct amba_phyrst *, struct device_node *);
	const struct reset_ops *ops;
	struct amba_phyrst *phyrst;
};

enum {
	CDNS_PHY_RESET = 0,
	CDNS_PHY_LINK_RESET,	/* Not used, as we are single-link PHY */
	CDNS_PHY_NR_RESETS,
};

static int amba_phyrst_pcie_assert(struct reset_ctl *rst)
{
	struct amba_phyrst_of_data *priv = dev_get_priv(rst->dev);
	struct amba_phyrst *phyrst = priv->phyrst;
	u32 phy_id = phyrst->phy_id;

	switch (rst->id) {
	case CDNS_PHY_LINK_RESET:
		break;
	case CDNS_PHY_RESET:
		regmap_update_bits(phyrst->c_regmap, phyrst->c_offset, PCIEC_GEN_RESET, GEN3);
		regmap_update_bits(phyrst->c_regmap, phyrst->c_offset, PCIEC_LANE_RESET, LANE_COUNT_X4);
		regmap_update_bits(phyrst->c_regmap, phyrst->c_offset, PCIEC_LINK_TRAIN_EN, 0x0);
		regmap_update_bits(phyrst->c_regmap, phyrst->c_offset, PCIEC_MISC_RESET, PCIEC_MISC_RESET);
		regmap_update_bits(phyrst->regmap, phyrst->offset[P_CTRL_REG], PCIEP_PHY_RESET(phy_id), PCIEP_PHY_RESET(phy_id));
		break;
	}

	return 0;
}

static int amba_phyrst_pcie_deassert(struct reset_ctl *rst)
{
	struct amba_phyrst_of_data *priv = dev_get_priv(rst->dev);
	struct amba_phyrst *phyrst = priv->phyrst;
	u32 phy_id = phyrst->phy_id;

	switch (rst->id) {
	case CDNS_PHY_LINK_RESET:
		break;
	case CDNS_PHY_RESET:
		regmap_update_bits(phyrst->regmap, phyrst->offset[P_CTRL_REG], PCIEP_PHY_RESET(phy_id), 0x0);
		/**
		 * "Power Sequencing and Reset Signal Timings" table in
		 * PCI EXPRESS CARD ELECTROMECHANICAL SPECIFICATION, REV. 3.0
		 * indicates PERST# should be deasserted after minimum of 100us
		 * once REFCLK is stable. The REFCLK to the connector in RC
		 * mode is selected while enabling the PHY. So deassert PERST#
		 * after 100 us.
		 * PCI EXPRESS CARD ELECTROMECHANICAL SPECIFICATION, REV. 3.0
		 * indicates PERST# should be deasserted after minimum of 100ms
		 * after power rails achieve specified operating limits and
		 * within this period reference clock should also become stable.
		 */
		msleep(PCI_TPVPERL_DELAY_MS);
		regmap_update_bits(phyrst->c_regmap, phyrst->c_offset, PCIEC_MISC_RESET, 0x0);
		regmap_update_bits(phyrst->c_regmap, phyrst->c_offset, PCIEC_LINK_TRAIN_EN, PCIEC_LINK_TRAIN_EN);
		break;
	}

	return 0;
}

static int amba_phyrst_usb32_assert(struct reset_ctl *rst)
{
	struct amba_phyrst_of_data *priv = dev_get_priv(rst->dev);
	struct amba_phyrst *phyrst = priv->phyrst;

	switch (rst->id) {
	case CDNS_PHY_LINK_RESET:
		break;
	case CDNS_PHY_RESET:
		regmap_update_bits(phyrst->regmap, phyrst->offset[P_CTRL_REG], USB32P_PHY_RESET, USB32P_PHY_RESET);
		break;
	}

	return 0;
}

static int amba_phyrst_usb32_deassert(struct reset_ctl *rst)
{
	struct amba_phyrst_of_data *priv = dev_get_priv(rst->dev);
	struct amba_phyrst *phyrst = priv->phyrst;

	switch (rst->id) {
	case CDNS_PHY_LINK_RESET:
		break;
	case CDNS_PHY_RESET:
		regmap_update_bits(phyrst->regmap, phyrst->offset[P_CTRL_REG], USB32P_PHY_RESET, 0x0);
		regmap_update_bits(phyrst->c_regmap, phyrst->c_offset, USB32C_SOFT_RESET, 0x0);
		break;
	}

	return 0;
}

static const struct reset_ops amba_phyrst_pcie_ops = {
	.rst_assert   = amba_phyrst_pcie_assert,
	.rst_deassert = amba_phyrst_pcie_deassert,
};

static const struct reset_ops amba_phyrst_usb32_ops = {
	.rst_assert   = amba_phyrst_usb32_assert,
	.rst_deassert = amba_phyrst_usb32_deassert,
};

static int amba_phyrst_pcie_init(struct amba_phyrst *phyrst, struct device_node *np)
{
	return 0;
}

static int amba_phyrst_usb32_init(struct amba_phyrst *phyrst, struct device_node *np)
{
	/* PHY reference clock is fixed at 100Mhz */
	regmap_update_bits(phyrst->regmap, phyrst->offset[PMA_CTRL_REG],
					USB32_PMA_CMN_REFCLK_DIG_DIV_MASK,
					USB32_PMA_CMN_REFCLK_DIG_DIV_4);

	/* Release USB32 PHY APB reset to allow access to PCS/PMA registers */
	regmap_update_bits(phyrst->regmap, phyrst->offset[P_CTRL_REG], USB32P_APB_RESET, 0x0);

	return 0;
}

static int amba_cdns_reset_assert(struct reset_ctl *rst)
{
	struct amba_phyrst_of_data *priv = dev_get_priv(rst->dev);

	return priv->ops->rst_assert(rst);
}

static int amba_cdns_reset_deassert(struct reset_ctl *rst)
{
	struct amba_phyrst_of_data *priv = dev_get_priv(rst->dev);

	return priv->ops->rst_deassert(rst);
}

static int amba_cdns_reset_free(struct reset_ctl *rst)
{
	return 0;
}

static int amba_cdns_reset_request(struct reset_ctl *rst)
{
	return 0;
}

static const struct reset_ops amba_cdns_reset_ops = {
	.request = amba_cdns_reset_request,
	.rfree = amba_cdns_reset_free,
	.rst_assert = amba_cdns_reset_assert,
	.rst_deassert = amba_cdns_reset_deassert,
};


static const struct udevice_id amba_phyrst_dt_ids[] = {
	{ .compatible = "ambarella,usb32-phyrst"},
	{ .compatible = "ambarella,pcie-phyrst"},
	{ /* sentinel */ }
};

static int amba_phyrst_probe(struct udevice *dev)
{
	struct amba_phyrst_of_data *data = dev_get_priv(dev);
	struct amba_phyrst *phyrst;
	struct device_node *np = (struct device_node *)ofnode_to_np(dev->node);
	struct ofnode_phandle_args args;
	int ret;

	if (device_is_compatible(dev, "ambarella,usb32-phyrst")) {
		data->init = amba_phyrst_usb32_init;
		data->ops = &amba_phyrst_usb32_ops;
	} else if (device_is_compatible(dev, "ambarella,pcie-phyrst")) {
		data->init = amba_phyrst_pcie_init;
		data->ops = &amba_phyrst_pcie_ops;
	}

	phyrst = devm_kzalloc(dev, sizeof(*phyrst), GFP_KERNEL);
	if(!phyrst)
		return -ENOMEM;

	data->phyrst = phyrst;
	phyrst->data = (void *)data;
	phyrst->np = np;

	phyrst->regmap = syscon_regmap_lookup_by_phandle(dev, "amb,scr-regmap");

	ret = dev_read_phandle_with_args(dev, "amb,scr-regmap", NULL, 2, 0, &args);
	if (ret) {
		printf("%s: dev_read_phandle_with_args: err=%d\n",
				__func__, ret);
		return ret;
	}
	phyrst->offset[PMA_CTRL_REG] = args.args[0];
	phyrst->offset[P_CTRL_REG] = args.args[1];
	if (IS_ERR(phyrst->regmap)) {
		dev_err(dev, "regmap lookup failed.\n");
		return PTR_ERR(phyrst->regmap);
	}

	debug("PMA_CTRL_REG:0x%x, P_CTRL_REG:0x%x\n", phyrst->offset[PMA_CTRL_REG],
		phyrst->offset[P_CTRL_REG]);

	phyrst->c_regmap = syscon_regmap_lookup_by_phandle(dev, "amb,c-scr-regmap");
	ret = dev_read_phandle_with_args(dev, "amb,c-scr-regmap", NULL, 1, 0, &args);
	if (ret) {
		printf("%s: dev_read_phandle_with_args: err=%d\n",
				__func__, ret);
		return ret;
	}
	phyrst->c_offset = args.args[0];

	debug("phyrst->c_offset:0x%x\n", phyrst->c_offset);

	if (IS_ERR(phyrst->c_regmap)) {
		dev_err(dev, "controller regmap lookup failed.\n");
		return PTR_ERR(phyrst->c_regmap);
	}

	ofnode_read_u32(dev_ofnode(dev), "amb,usb32-phy-id", &phyrst->phy_id);

	data->init(phyrst, np);

	return 0;
}

U_BOOT_DRIVER(stm32_rcc_reset) = {
	.name			= "amba-phyrst",
	.id			= UCLASS_RESET,
	.of_match		= amba_phyrst_dt_ids,
	.probe			= amba_phyrst_probe,
	.priv_auto_alloc_size	= sizeof(struct amba_phyrst_of_data),
	.ops			= &amba_cdns_reset_ops,
};
