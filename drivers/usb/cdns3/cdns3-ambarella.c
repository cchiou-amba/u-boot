

#include <common.h>
#include <asm-generic/io.h>
#include <clk.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/usb/otg.h>
#include <malloc.h>
#include "core.h"
#include <syscon.h>
#include <regmap.h>
#include <asm/arch-ambarella/scratchpad.h>

#define USB32C_CTRL_OFFSET		0x16c
#define USB32C_RESET_MASK		(0x1)
#define USB32C_MODE_STRAP_MASK		(0x6)
#define USB32C_MODE_STRAP_SHIFT		(1)
#define USB32C_OTGSESS_VALID_MASK	BIT(3)

#define USBC_CTRL_OFFSET		0x12c
#define USBC_HOST_OCP_MASK		BIT(1)
#define USBC_HOST_OCP_SHIFT		(1)

#define USB_SIDEBAND_REG_OFFSET		0x94

#define USBP_TXPREEMPAMP_MASK		GENMASK(13, 12)
#define USBP_TXRISETUNE0_MASK		GENMASK(21, 20)
#define USBP_TXVREFTUNE0_MASK		GENMASK(25, 22)

#define USBP_TXPREEMPAMP_SHIFT		(12)
#define USBP_TXRISETUNE0_SHIFT		(20)
#define USBP_TXVREFTUNE0_SHIFT		(22)

/* Modestrap modes */
enum modestrap_mode {
	MODE_STRAP_MODE_NONE,
	MODE_STRAP_MODE_HOST,
	MODE_STRAP_MODE_PERIPHERAL
};

static unsigned int mode_strap = MODE_STRAP_MODE_HOST;

struct cdns_amba {
	struct device *dev;
	struct regmap	*scr_reg;
	u32 ovrcur_pol_inv;
	u32 usbp_ctrl_offset;
	u32 usbp_ctrl2_offset;
	u32 usbp_tx_tune[3];
};

static int cdns_amba_probe(struct udevice *dev)
{
	struct cdns_amba *data = dev_get_platdata(dev);
	int ret;
	u32 val, ovrcur_pol;
	struct ofnode_phandle_args args;

	data->scr_reg = syscon_regmap_lookup_by_phandle(dev, "amb,scr-regmap");
	if (IS_ERR(data->scr_reg)) {
		dev_err(dev, "no scr regmap!\n");
		return PTR_ERR(data->scr_reg);
	}

	ret = dev_read_phandle_with_args(dev, "amb,scr-regmap", NULL, 2, 0, &args);
	if (ret) {
		printf("%s: dev_read_phandle_with_args: err=%d\n",
				__func__, ret);
		return ret;
	}

	data->usbp_ctrl_offset = args.args[0];
	data->usbp_ctrl2_offset = args.args[1];

	/* Get USB HS PHY tunning setting if necessary */
	ret = ofnode_read_u32(dev->node, "amb,tx-preemphasis", &data->usbp_tx_tune[0]);
	if (ret < 0) {
		regmap_read(data->scr_reg, data->usbp_ctrl2_offset, &val);
		data->usbp_tx_tune[0] = (val & USBP_TXPREEMPAMP_MASK) >> USBP_TXPREEMPAMP_SHIFT;
	} else
		regmap_update_bits(data->scr_reg, data->usbp_ctrl2_offset, USBP_TXPREEMPAMP_MASK,
						data->usbp_tx_tune[0] << USBP_TXPREEMPAMP_SHIFT);

	ret = ofnode_read_u32(dev->node, "amb,tx-risetune", &data->usbp_tx_tune[1]);
	if (ret < 0) {
		regmap_read(data->scr_reg, data->usbp_ctrl_offset, &val);
		data->usbp_tx_tune[1] = (val & USBP_TXRISETUNE0_MASK) >> USBP_TXRISETUNE0_SHIFT;
	} else
		regmap_update_bits(data->scr_reg, data->usbp_ctrl_offset, USBP_TXRISETUNE0_MASK,
						data->usbp_tx_tune[1] << USBP_TXRISETUNE0_SHIFT);

	ret = ofnode_read_u32(dev->node, "amb,tx-vreftune", &data->usbp_tx_tune[2]);
	if (ret < 0) {

		regmap_read(data->scr_reg, data->usbp_ctrl_offset, &val);
		data->usbp_tx_tune[2] = (val & USBP_TXVREFTUNE0_MASK) >> USBP_TXVREFTUNE0_SHIFT;
	} else
		regmap_update_bits(data->scr_reg, data->usbp_ctrl_offset, USBP_TXVREFTUNE0_MASK,
						data->usbp_tx_tune[2] << USBP_TXVREFTUNE0_SHIFT);

	/* Set default mode (mode_strap) to be actived after power on reset */
	regmap_update_bits(data->scr_reg, USB32C_CTRL_OFFSET,
			USB32C_MODE_STRAP_MASK, mode_strap << USB32C_MODE_STRAP_SHIFT);

#ifndef CONFIG_PHY_CADENCE_TORRENT
	regmap_write(data->scr_reg, USB32C_CTRL_OFFSET, 0x0000101a);
#endif

#if (CHIP_REV == CV75) || (CHIP_REV == CV72) || (CHIP_REV == CV3AD685) || (CHIP_REV == N1_655)
	/* Default OCP is low with bit1 = 1; while bit1 = 0, ocp high */
	ret = ofnode_read_u32(dev->node, "amb,ocp-polarity", &ovrcur_pol);
	if (ret < 0)
		ovrcur_pol = 0;
	data->ovrcur_pol_inv = !ovrcur_pol;
	regmap_update_bits(data->scr_reg, USBC_CTRL_OFFSET,
		USBC_HOST_OCP_MASK, data->ovrcur_pol_inv << USBC_HOST_OCP_SHIFT);
#endif

	return 0;
}

static int cdns_amba_remove(struct udevice *dev)
{
	return 0;
}

static const struct udevice_id cdns_amba_of_match[] = {
	{ .compatible = "ambarella,cdns-usb3", },
	{},
};

U_BOOT_DRIVER(cdns_amba) = {
	.name = "cdns-amba",
	.id = UCLASS_NOP,
	.of_match = cdns_amba_of_match,
	.bind = cdns3_bind,
	.probe = cdns_amba_probe,
	.remove = cdns_amba_remove,
	.platdata_auto_alloc_size = sizeof(struct cdns_amba),
	.flags = DM_FLAG_OS_PREPARE,
};

