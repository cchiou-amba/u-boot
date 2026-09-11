// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <env.h>
#include <dm.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/arch/misc.h>
#include <linux/delay.h>

int dram_init(void)
{
	int rval;

	rval = plat_f_dram_init();
	if (rval)
		gd->ram_size = DRAM_SIZE;

	return 0;
}

/* Sync the setting with FreeRTOS board setting */
#define SHARED_MEM_BASE     	0x40000000  // 1GB
#define SHARED_MEM_MAGIC_OFFSET 0x800  // 2KB
#define SHARED_MEM_MAGIC_VALUE  0x12345678
#define SHARED_MEM_FAILED_VALUE  0x0
#define MAX_TIMEOUT_MS      	2000  // 2s timeout
#define CHECK_INTERVAL_MS   	2
static int check_shared_memory(void)
{
	volatile u32 *magic_addr;
	u32 magic_value;
	u32 timeout_ms = 0;
	u32 retry_count = 0;
	u32 last_value = 0;

	magic_addr = (volatile u32 *)(SHARED_MEM_BASE + SHARED_MEM_MAGIC_OFFSET);

	while (timeout_ms < MAX_TIMEOUT_MS) {
		magic_value = readl(magic_addr);

		if (magic_value == SHARED_MEM_MAGIC_VALUE) {
			return 0;
		}

		if (magic_value == SHARED_MEM_FAILED_VALUE) {
			return -1;
		}

		if (magic_value != last_value) {
			last_value = magic_value;
		}

		retry_count++;
		timeout_ms += CHECK_INTERVAL_MS;

		mdelay(CHECK_INTERVAL_MS);
	}

	return -1;
}

static int read_shared_memory_data(void *buffer, size_t size)
{
	volatile void *src;
	void *dst;

	if (check_shared_memory() != 0) {
		return -1;
	}

	if (!buffer || size == 0)
		return -1;

	src = (volatile void *)(SHARED_MEM_BASE);
	dst = (void *)buffer;

	memcpy(dst, src, size);

	return 0;
}

#define EEPROM_SIZE (2048)
/*
 * Get mac address from EEPROM via R52
*/
static int get_mac_addr(void)
{
	uint8_t eeprom_data[EEPROM_SIZE];
	char mac_str[18];
	int ret;

	ret = read_shared_memory_data(eeprom_data, sizeof(eeprom_data));
	if (ret)
		printf("Get mac address failed \n");
	else {
		if(!eth_get_mac_from_eeprom((char *)eeprom_data, "MAC0:", mac_str)){
			env_set("ethaddr", mac_str);
			printf("Set ethaddr environment variable to: %s\n", mac_str);
		}

		if(!eth_get_mac_from_eeprom((char *)eeprom_data, "MAC1:", mac_str)){
			env_set("eth1addr", mac_str);
			printf("Set eth1ddr environment variable to: %s\n", mac_str);
		}
	}
	return ret;
}
/*
 * board_r stage.
 */
int board_init(void)
{
	/* Sys Peripheral 3.3V */
	gpio_request(51, "sys peripheral 3v3");
	gpio_direction_output(51, 1);

    /********* SerDes *********/

	/* MAX96712-A Power */
	gpio_request(30, "max96712-A power");
	gpio_direction_output(30, 1);
	/* MAX96712-B Power */
	gpio_request(54, "max96712-B power");
	gpio_direction_output(54, 1);

    /********* USB *********/

	/* USB SWITCH */
	gpio_request(3, "usb_switch");
	gpio_direction_output(3, 1); //0: usb work as type-c 1:usb work as hub

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

	/*
	 * Specify the device-tree for Linux kernel
	 */
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
	//get_mac_addr();

	return 0;
}
