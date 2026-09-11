// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <env.h>
#include <dm.h>
#include <asm/gpio.h>
#include <asm/arch/misc.h>
#include <linux/delay.h>
#include <spi_flash.h>

int dram_init(void)
{
	int rval;

	rval = plat_f_dram_init();
	if (rval)
		gd->ram_size = DRAM_SIZE;

	return 0;
}

/*
 * board_r stage.
 */
int board_init(void)
{
	gpio_request(107, "sdmmc1_pwr"); /* default 3.3V */
	gpio_request(109, "sdmmc2_pwr");

	gpio_direction_output(107, 0);
	gpio_direction_output(109, 0);
	mdelay(10);
	gpio_direction_output(107, 1);
	gpio_direction_output(109, 1);

	return 0;
}

#define CONFIG_BUF_SIZE (2048)
/*
 * Get mac address from EEPROM via R52
*/
static int get_mac_addr(void)
{
	struct spi_flash *flash;
	uint8_t config_data[CONFIG_BUF_SIZE];
	char mac_str[18];
	int ret;

	/* Read SPI NOR Flash */
	flash = spi_flash_probe(0, 0, 0, 0);
	if (!flash) {
		printf("SPINOR flash not available\n");
		return -1;
	}

	ret = spi_flash_read(flash, 0x0, CONFIG_BUF_SIZE, config_data);
	if (ret < 0) {
		printf("Failed to read SPINOR flash: %d\n", ret);
		return ret;
	}

	//printf("Successfully read %d bytes from SPINOR flash\n", CONFIG_BUF_SIZE);

	if(!eth_get_mac_from_eeprom((char *)config_data, "MAC0:", mac_str)){
		env_set("ethaddr", mac_str);
		printf("Set ethaddr environment variable to: %s\n", mac_str);
	}

	if(!eth_get_mac_from_eeprom((char *)config_data, "MAC1:", mac_str)){
		env_set("eth1addr", mac_str);
		printf("Set eth1addr environment variable to: %s\n", mac_str);
	}

	return 0;
}

int board_fit_config_name_match(const char *name)
{
	/*
	 * select configuration 0 by default.
	 */

	return 0;
}

static int __init_usb_gadget(void)
{
	int rval;
	struct udevice *dev;

	rval = uclass_get_device(UCLASS_USB_GADGET_GENERIC, 0, &dev);
	if (rval) {
		pr_err("%s: Cannot find USB device\n", __func__);
		return rval;
	}
	return 0;
}

int board_late_init(void)
{
	int rval;

	rval = env_set_hex("fdtaddr", (ulong)gd->fdt_blob);
	if (rval) {
		printf("set fdtaddr env error.\n");
		return rval;
	}

	plat_r_board_late_init();

#ifdef CONFIG_USB_GADGET
	rval = __init_usb_gadget();
	if (rval)
		return rval;
#endif
	get_mac_addr();

	return 0;
}
