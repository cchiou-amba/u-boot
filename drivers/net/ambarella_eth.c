// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

/*
 * ambarella ethernet IP driver for U-Boot
 */
//#define DEBUG
#include <common.h>
#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <miiphy.h>
#include <malloc.h>
#include <pci.h>
#include <reset.h>
#include <linux/delay.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <power/regulator.h>
#include <dm/pinctrl.h>
#include "ambarella_eth.h"

#define ETH_SUPPORT_AHB_MDIO (1)
#define AHBSP_GMII_ADDR_REG (0x20e00240a4)
#define AHBSP_GMII_DATA_REG (0x20e00240a0)

#if (ETH_SUPPORT_AHB_MDIO == 0)
static int ambhw_mdio_read(struct mii_dev *bus, int addr, int devad, int reg)
{
	struct amb_eth_dev *priv = dev_get_priv((struct udevice *)bus->priv);
	struct amb_mac_regs *mac_p = priv->mac_regs_p;
	ulong start;
	u16 miiaddr;
	int timeout = CONFIG_MDIO_TIMEOUT;

	miiaddr = ((addr << MIIADDRSHIFT) & MII_ADDRMSK) |
		((reg << MIIREGSHIFT) & MII_REGMSK);

	writel(miiaddr | MII_CLKRANGE_150_250M | MII_BUSY, &mac_p->miiaddr);

	start = get_timer(0);
	while (get_timer(start) < timeout) {
		if (!(readl(&mac_p->miiaddr) & MII_BUSY))
			return readl(&mac_p->miidata);
		udelay(10);
	};

	debug("addr0x%x devad0x%x reg 0x%n", addr, devad, reg);
	return -ETIMEDOUT;
}

static int ambhw_mdio_write(struct mii_dev *bus, int addr, int devad, int reg,
		u16 val)
{
	struct amb_eth_dev *priv = dev_get_priv((struct udevice *)bus->priv);
	struct amb_mac_regs *mac_p = priv->mac_regs_p;

	ulong start;
	u16 miiaddr;
	int ret = -ETIMEDOUT, timeout = CONFIG_MDIO_TIMEOUT;

	debug("addr0x%x devad0x%x reg 0x%n", addr, devad, reg);
	writel(val, &mac_p->miidata);
	miiaddr = ((addr << MIIADDRSHIFT) & MII_ADDRMSK) |
		((reg << MIIREGSHIFT) & MII_REGMSK) | MII_WRITE;

	writel(miiaddr | MII_CLKRANGE_150_250M | MII_BUSY, &mac_p->miiaddr);

	start = get_timer(0);
	while (get_timer(start) < timeout) {
		if (!(readl(&mac_p->miiaddr) & MII_BUSY)) {
			ret = 0;
			break;
		}
		udelay(10);
	};

	return ret;
}

#else
static int ambahb_mdio_read(struct mii_dev *bus, int addr, int devad, int reg)
{
	ulong start;
	unsigned int regval;
	int timeout = CONFIG_MDIO_TIMEOUT;

	regval = addr << 11;
	regval |= reg << 6;
	/* clock divider always set to 4 */
	regval |= 4 << 2;
	/* GMII read */
	regval |= 0 << 1;
	regval |= 1 << 0;
	writel(regval, (void *)AHBSP_GMII_ADDR_REG);

	debug("%s addr 0x%x devad 0x%x reg 0x%x \n", __func__, addr, devad, reg);
	start = get_timer(0);
	while (get_timer(start) < timeout) {
		if (!(readl((void *)AHBSP_GMII_ADDR_REG) & 0x1)){
			regval = readl((void *)AHBSP_GMII_DATA_REG);
			debug("%s addr 0x%x devad 0x%x reg 0x%x regval 0x%x \n", __func__,
					addr, devad, reg, regval);
					return regval;
		}
		udelay(10);
	};

	return -ETIMEDOUT;
}

static int ambahb_mdio_write(struct mii_dev *bus, int addr, int devad, int reg,
		u16 val)
{
	ulong start;
	unsigned int regval;
	int ret = -ETIMEDOUT, timeout = CONFIG_MDIO_TIMEOUT;

	regval = addr << 11;
	regval |= reg << 6;
	/* clock divider always set to 4 */
	regval |= 4 << 2;
	/* GMII write */
	regval |= 1 << 1;
	regval |= 1 << 0;

	writel(val, (void *)AHBSP_GMII_DATA_REG);
	writel(regval, (void *)AHBSP_GMII_ADDR_REG);

	start = get_timer(0);
	while (get_timer(start) < timeout) {
		if (!(readl((void *)AHBSP_GMII_ADDR_REG) & 0x1)) {
			ret = 0;
			break;
		}
		udelay(10);
	};

	return ret;
}

#endif

static int amb_mdio_init(const char *name, void *priv)
{
	struct mii_dev *bus = mdio_alloc();

	if (!bus) {
		printf("Failed to allocate MDIO bus\n");
		return -ENOMEM;
	}

#if (ETH_SUPPORT_AHB_MDIO == 0)
	bus->read = ambhw_mdio_read;
	bus->write = ambhw_mdio_write;
	snprintf(bus->name, sizeof(bus->name), "%s", name);
#else
	bus->read = ambahb_mdio_read;
	bus->write = ambahb_mdio_write;
	snprintf(bus->name, sizeof(bus->name), "%s", name);
#endif

	bus->priv = priv;

	return mdio_register(bus);
}

static void tx_descs_init(struct amb_eth_dev *priv)
{
	struct amb_dma_regs *dma_p = priv->dma_regs_p;
	struct dmamacdescr *desc_table_p = &priv->tx_mac_descrtable[0];
	char *txbuffs = &priv->txbuffs[0];
	struct dmamacdescr *desc_p;
	u32 idx;

	for (idx = 0; idx < CONFIG_TX_DESCR_NUM; idx++) {
		desc_p = &desc_table_p[idx];
		desc_p->dmamac_addr = (ulong)&txbuffs[idx * CONFIG_ETH_BUFSIZE];
		desc_p->dmamac_next = (ulong)&desc_table_p[idx + 1];

#if defined(CONFIG_DW_ALTDESCRIPTOR)
		desc_p->txrx_status &= ~(DESC_TXSTS_TXINT | DESC_TXSTS_TXLAST |
				DESC_TXSTS_TXFIRST | DESC_TXSTS_TXCRCDIS |
				DESC_TXSTS_TXCHECKINSCTRL |
				DESC_TXSTS_TXRINGEND | DESC_TXSTS_TXPADDIS);

		desc_p->txrx_status |= DESC_TXSTS_TXCHAIN;
		desc_p->dmamac_cntl = 0;
		desc_p->txrx_status &= ~(DESC_TXSTS_MSK | DESC_TXSTS_OWNBYDMA);
#else
		desc_p->dmamac_cntl = DESC_TXCTRL_TXCHAIN;
		desc_p->txrx_status = 0;
#endif
	}

	/* Correcting the last pointer of the chain */
	desc_p->dmamac_next = (ulong)&desc_table_p[0];

	/* Flush all Tx buffer descriptors at once */
	flush_dcache_range((ulong)priv->tx_mac_descrtable,
			(ulong)priv->tx_mac_descrtable +
			sizeof(priv->tx_mac_descrtable));

	writel((ulong)&desc_table_p[0], &dma_p->txdesclistaddr);
	priv->tx_currdescnum = 0;
}

static void rx_descs_init(struct amb_eth_dev *priv)
{
	struct amb_dma_regs *dma_p = priv->dma_regs_p;
	struct dmamacdescr *desc_table_p = &priv->rx_mac_descrtable[0];
	char *rxbuffs = &priv->rxbuffs[0];
	struct dmamacdescr *desc_p;
	u32 idx;

	/* Before passing buffers to GMAC we need to make sure zeros
	 * written there right after "priv" structure allocation were
	 * flushed into RAM.
	 * Otherwise there's a chance to get some of them flushed in RAM when
	 * GMAC is already pushing data to RAM via DMA. This way incoming from
	 * GMAC data will be corrupted. */
	flush_dcache_range((ulong)rxbuffs, (ulong)rxbuffs + RX_TOTAL_BUFSIZE);

	for (idx = 0; idx < CONFIG_RX_DESCR_NUM; idx++) {
		desc_p = &desc_table_p[idx];
		desc_p->dmamac_addr = (ulong)&rxbuffs[idx * CONFIG_ETH_BUFSIZE];
		desc_p->dmamac_next = (ulong)&desc_table_p[idx + 1];

		desc_p->dmamac_cntl =
			(MAC_MAX_FRAME_SZ & DESC_RXCTRL_SIZE1MASK) |
			DESC_RXCTRL_RXCHAIN;

		desc_p->txrx_status = DESC_RXSTS_OWNBYDMA;
	}

	/* Correcting the last pointer of the chain */
	desc_p->dmamac_next = (ulong)&desc_table_p[0];

	/* Flush all Rx buffer descriptors at once */
	flush_dcache_range((ulong)priv->rx_mac_descrtable,
			(ulong)priv->rx_mac_descrtable +
			sizeof(priv->rx_mac_descrtable));

	writel((ulong)&desc_table_p[0], &dma_p->rxdesclistaddr);
	priv->rx_currdescnum = 0;
}

static int amb_write_hwaddr(struct amb_eth_dev *priv, u8 *mac_id)
{
	struct amb_mac_regs *mac_p = priv->mac_regs_p;
	u32 macid_lo, macid_hi;

	macid_lo = mac_id[0] + (mac_id[1] << 8) + (mac_id[2] << 16) +
		(mac_id[3] << 24);
	macid_hi = mac_id[4] + (mac_id[5] << 8);

	writel(macid_hi, &mac_p->macaddr0hi);
	writel(macid_lo, &mac_p->macaddr0lo);

	return 0;
}

static int amb_adjust_link(struct amb_eth_dev *priv, struct amb_mac_regs *mac_p,
		struct phy_device *phydev)
{
	u32 conf = readl(&mac_p->conf) | FRAMEBURSTENABLE | DISABLERXOWN;

	if (!phydev->link) {
		printf("%s: No link.\n", phydev->dev->name);
		return 0;
	}

	if (phydev->speed != 1000)
		conf |= MII_PORTSELECT;
	else
		conf &= ~MII_PORTSELECT;

	if (phydev->speed == 100)
		conf |= FES_100;

	if (phydev->duplex)
		conf |= FULLDPLXMODE;

	writel(conf, &mac_p->conf);

	printf("Speed: %d, %s duplex%s\n", phydev->speed,
			(phydev->duplex) ? "full" : "half",
			(phydev->port == PORT_FIBRE) ? ", fiber mode" : "");

	return 0;
}

static void ambarella_eth_phy_clock_init(void)
{
	/* rct USE_INTERNAL_GTX_CLK */
	writel(0x00, (void *)0x20ED0802B0);
	/* scratchpad AHBSP_NON_SEC_CTRL_REG bit31 enet_gtx_clk_pol */
	setbits_32((void *)0x20E0024060, 0x80000000);
	/* rct ENET_CLK_SRC_SEL_REG  */
	setbits_32((void *)0x20ED0806B8, 0x1);
	/* rct AHB_MISC_REG bit5 Controls direction of xx_enet_clk_rx*/
	setbits_32((void *)0x20ED08021C, 0x20);
	//printf("%s 0x%x\n", __func__, *(unsigned int *)0x20E0024060);
}

int ambarella_eth_init(struct amb_eth_dev *priv, u8 *enetaddr)
{
	struct amb_mac_regs *mac_p = priv->mac_regs_p;
	struct amb_dma_regs *dma_p = priv->dma_regs_p;
	unsigned int start;
	int ret;

	ambarella_eth_phy_clock_init();
	writel(readl(&dma_p->busmode) | DMAMAC_SRST, &dma_p->busmode);

	/*
	 * When a MII PHY is used, we must set the PS bit for the DMA
	 * reset to succeed.
	 */
	if (priv->phydev->interface == PHY_INTERFACE_MODE_MII)
		writel(readl(&mac_p->conf) | MII_PORTSELECT, &mac_p->conf);
	else
		writel(readl(&mac_p->conf) & ~MII_PORTSELECT, &mac_p->conf);

	start = get_timer(0);
	while (readl(&dma_p->busmode) & DMAMAC_SRST) {
		if (get_timer(start) >= CONFIG_MACRESET_TIMEOUT) {
			printf("DMA reset timeout\n");
			return -ETIMEDOUT;
		}

		mdelay(100);
	};

	/*
	 * Soft reset above clears HW address registers.
	 * So we have to set it here once again.
	 */
	amb_write_hwaddr(priv, enetaddr);

	rx_descs_init(priv);
	tx_descs_init(priv);

	writel(FIXEDBURST | PRIORXTX_41 | DMA_PBL, &dma_p->busmode);

#ifndef CONFIG_DW_MAC_FORCE_THRESHOLD_MODE
	writel(readl(&dma_p->opmode) | FLUSHTXFIFO | STOREFORWARD,
			&dma_p->opmode);
#else
	writel(readl(&dma_p->opmode) | FLUSHTXFIFO,
			&dma_p->opmode);
#endif

	writel(readl(&dma_p->opmode) | RXSTART | TXSTART, &dma_p->opmode);


	/* Start up the PHY */
	ret = phy_startup(priv->phydev);
	if (ret) {
		printf("Could not initialize PHY %s\n",
				priv->phydev->dev->name);
		return ret;
	}

	ret = amb_adjust_link(priv, mac_p, priv->phydev);
	if (ret)
		return ret;

	return 0;
}

int ambarella_eth_enable(struct amb_eth_dev *priv)
{
	struct amb_mac_regs *mac_p = priv->mac_regs_p;

	if (!priv->phydev->link)
		return -EIO;

	writel(readl(&mac_p->conf) | RXENABLE | TXENABLE, &mac_p->conf);

	return 0;
}

#define ETH_ZLEN	60

static int amb_phy_init(struct amb_eth_dev *priv, void *dev)
{
	struct phy_device *phydev;
	int phy_addr = -1, ret;

#ifdef CONFIG_PHY_ADDR
	phy_addr = CONFIG_PHY_ADDR;
#endif

	phydev = phy_connect(priv->bus, phy_addr, dev, priv->interface);
	if (!phydev)
		return -ENODEV;

	phydev->supported &= PHY_GBIT_FEATURES;
	if (priv->max_speed) {
		ret = phy_set_supported(phydev, priv->max_speed);
		if (ret)
			return ret;
	}
	phydev->advertising = phydev->supported;

	priv->phydev = phydev;
	phy_config(phydev);

	return 0;
}

static int ambarella_eth_start(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_platdata(dev);
	struct amb_eth_dev *priv = dev_get_priv(dev);
	int ret;

	ret = ambarella_eth_init(priv, pdata->enetaddr);
	if (ret)
		return ret;
	ret = ambarella_eth_enable(priv);
	if (ret)
		return ret;

	return 0;
}

int ambarella_eth_send(struct udevice *dev, void *packet, int length)
{
	struct amb_eth_dev *priv = dev_get_priv(dev);
	struct amb_dma_regs *dma_p = priv->dma_regs_p;
	u32 desc_num = priv->tx_currdescnum;
	struct dmamacdescr *desc_p = &priv->tx_mac_descrtable[desc_num];
	ulong desc_start = (ulong)desc_p;
	ulong desc_end = desc_start +
		roundup(sizeof(*desc_p), ARCH_DMA_MINALIGN);
	ulong data_start = desc_p->dmamac_addr;
	ulong data_end = data_start + roundup(length, ARCH_DMA_MINALIGN);
	debug("%s %d 0x%x \n",__func__, __LINE__, length);
	/*
	 * Strictly we only need to invalidate the "txrx_status" field
	 * for the following check, but on some platforms we cannot
	 * invalidate only 4 bytes, so we flush the entire descriptor,
	 * which is 16 bytes in total. This is safe because the
	 * individual descriptors in the array are each aligned to
	 * ARCH_DMA_MINALIGN and padded appropriately.
	 */
	invalidate_dcache_range(desc_start, desc_end);

	/* Check if the descriptor is owned by CPU */
	if (desc_p->txrx_status & DESC_TXSTS_OWNBYDMA) {
		printf("CPU not owner of tx frame\n");
		return -EPERM;
	}

	memcpy((void *)data_start, packet, length);
	if (length < ETH_ZLEN) {
		memset(&((char *)data_start)[length], 0, ETH_ZLEN - length);
		length = ETH_ZLEN;
	}

	/* Flush data to be sent */
	flush_dcache_range(data_start, data_end);

#if defined(CONFIG_DW_ALTDESCRIPTOR)
	desc_p->txrx_status |= DESC_TXSTS_TXFIRST | DESC_TXSTS_TXLAST;
	desc_p->dmamac_cntl = (desc_p->dmamac_cntl & ~DESC_TXCTRL_SIZE1MASK) |
		((length << DESC_TXCTRL_SIZE1SHFT) &
		 DESC_TXCTRL_SIZE1MASK);

	desc_p->txrx_status &= ~(DESC_TXSTS_MSK);
	desc_p->txrx_status |= DESC_TXSTS_OWNBYDMA;
#else
	desc_p->dmamac_cntl = (desc_p->dmamac_cntl & ~DESC_TXCTRL_SIZE1MASK) |
		((length << DESC_TXCTRL_SIZE1SHFT) &
		 DESC_TXCTRL_SIZE1MASK) | DESC_TXCTRL_TXLAST |
		DESC_TXCTRL_TXFIRST;

	desc_p->txrx_status = DESC_TXSTS_OWNBYDMA;
#endif

	/* Flush modified buffer descriptor */
	flush_dcache_range(desc_start, desc_end);

	/* Test the wrap-around condition. */
	if (++desc_num >= CONFIG_TX_DESCR_NUM)
		desc_num = 0;

	priv->tx_currdescnum = desc_num;

	/* Start the transmission */
	writel(POLL_DATA, &dma_p->txpolldemand);

	return 0;
}

int ambarella_eth_recv(struct udevice *dev, int flags, uchar **packetp)
{
	struct amb_eth_dev *priv = dev_get_priv(dev);
	u32 status, desc_num = priv->rx_currdescnum;
	struct dmamacdescr *desc_p = &priv->rx_mac_descrtable[desc_num];
	int length = -EAGAIN;
	ulong desc_start = (ulong)desc_p;
	ulong desc_end = desc_start +
		roundup(sizeof(*desc_p), ARCH_DMA_MINALIGN);
	ulong data_start = desc_p->dmamac_addr;
	ulong data_end;

	/* Invalidate entire buffer descriptor */
	invalidate_dcache_range(desc_start, desc_end);

	status = desc_p->txrx_status;

	/* Check  if the owner is the CPU */
	if (!(status & DESC_RXSTS_OWNBYDMA)) {

		length = (status & DESC_RXSTS_FRMLENMSK) >>
			DESC_RXSTS_FRMLENSHFT;

		/* Invalidate received data */
		data_end = data_start + roundup(length, ARCH_DMA_MINALIGN);
		invalidate_dcache_range(data_start, data_end);
		*packetp = (uchar *)(ulong)desc_p->dmamac_addr;
	}

	return length;
}

int ambarella_eth_free_pkt(struct udevice *dev, uchar *packet, int length)
{
	struct amb_eth_dev *priv = dev_get_priv(dev);

	u32 desc_num = priv->rx_currdescnum;
	struct dmamacdescr *desc_p = &priv->rx_mac_descrtable[desc_num];
	ulong desc_start = (ulong)desc_p;
	ulong desc_end = desc_start +
		roundup(sizeof(*desc_p), ARCH_DMA_MINALIGN);

	/*
	 * Make the current descriptor valid again and go to
	 * the next one
	 */
	desc_p->txrx_status |= DESC_RXSTS_OWNBYDMA;

	/* Flush only status field - others weren't changed */
	flush_dcache_range(desc_start, desc_end);

	/* Test the wrap-around condition. */
	if (++desc_num >= CONFIG_RX_DESCR_NUM)
		desc_num = 0;
	priv->rx_currdescnum = desc_num;

	return 0;
}

void ambarella_eth_stop(struct udevice *dev)
{
	struct amb_eth_dev *priv = dev_get_priv(dev);
	struct amb_mac_regs *mac_p = priv->mac_regs_p;
	struct amb_dma_regs *dma_p = priv->dma_regs_p;

	writel(readl(&mac_p->conf) & ~(RXENABLE | TXENABLE), &mac_p->conf);
	writel(readl(&dma_p->opmode) & ~(RXSTART | TXSTART), &dma_p->opmode);

	phy_shutdown(priv->phydev);
}

int ambarella_eth_write_hwaddr(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_platdata(dev);
	struct amb_eth_dev *priv = dev_get_priv(dev);

	return amb_write_hwaddr(priv, pdata->enetaddr);
}

static int ambarella_eth_bind(struct udevice *dev)
{
	return 0;
}

int ambarella_eth_probe(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_platdata(dev);
	struct amb_eth_dev *priv = dev_get_priv(dev);
	ulong iobase = pdata->iobase;
	ulong ioaddr;
	int ret, err;

#if 0
	ret = reset_get_bulk(dev, &reset_bulk);
	if (ret)
		dev_warn(dev, "Can't get reset: %d\n", ret);
	else
		reset_deassert_bulk(&reset_bulk);
#endif

	debug("%s, iobase=0x%lx, priv=%p\n", __func__, iobase, priv);
	ioaddr = iobase;
	priv->mac_regs_p = (struct amb_mac_regs *)ioaddr;
	priv->dma_regs_p = (struct amb_dma_regs *)(ioaddr + ETH_DMA_BASE_OFFSET);
	priv->interface = pdata->phy_interface;
	priv->max_speed = pdata->max_speed;

	ret = amb_mdio_init(dev->name, dev);
	if (ret) {
		err = ret;
		goto mdio_err;
	}
	priv->bus = miiphy_get_dev_by_name(dev->name);

	ret = amb_phy_init(priv, dev);
	debug("%s, ret=%d\n", __func__, ret);
	if (!ret)
		return 0;

	/* continue here for cleanup if no PHY found */
	err = ret;
	mdio_unregister(priv->bus);
	mdio_free(priv->bus);
mdio_err:

	return err;
}

static int ambarella_eth_remove(struct udevice *dev)
{
	struct amb_eth_dev *priv = dev_get_priv(dev);

	free(priv->phydev);
	mdio_unregister(priv->bus);
	mdio_free(priv->bus);

	return 0;
}

const struct eth_ops ambarella_eth_ops = {
	.start			= ambarella_eth_start,
	.send			= ambarella_eth_send,
	.recv			= ambarella_eth_recv,
	.free_pkt		= ambarella_eth_free_pkt,
	.stop			= ambarella_eth_stop,
	.write_hwaddr		= ambarella_eth_write_hwaddr,
};

int ambarella_eth_of_to_plat(struct udevice *dev)
{
	struct amb_eth_pdata *amb_pdata = dev_get_platdata(dev);
	struct eth_pdata *pdata = &amb_pdata->eth_pdata;
	const char *phy_mode;
	int ret = 0;

	pdata->iobase = dev_read_addr(dev);
	pdata->phy_interface = -1;
	phy_mode = dev_read_string(dev, "phy-mode");
	if (phy_mode)
		pdata->phy_interface = phy_get_interface_by_name(phy_mode);
	if (pdata->phy_interface == -1) {
		debug("%s: Invalid PHY interface '%s'\n", __func__, phy_mode);
		return -EINVAL;
	}

	pdata->max_speed = dev_read_u32_default(dev, "max-speed", 0);

	return ret;
}

static const struct udevice_id ambarella_eth_ids[] = {
	{ .compatible = "ambarella,eth" },
	{ }
};

U_BOOT_DRIVER(eth_ambarella) = {
	.name	= "eth_ambarella",
	.id	= UCLASS_ETH,
	.of_match = ambarella_eth_ids,
	.ofdata_to_platdata = ambarella_eth_of_to_plat,
	.bind	= ambarella_eth_bind,
	.probe	= ambarella_eth_probe,
	.remove	= ambarella_eth_remove,
	.ops	= &ambarella_eth_ops,
	.priv_auto_alloc_size = sizeof(struct amb_eth_dev),
	.platdata_auto_alloc_size = sizeof(struct amb_eth_pdata),
	.flags = DM_FLAG_ALLOC_PRIV_DMA,
};

