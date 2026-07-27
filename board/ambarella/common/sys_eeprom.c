#include <common.h>
#include <command.h>
#include <env.h>
#include <i2c.h>
#include <linux/delay.h>
#include "eeprom.h"

static struct eeprom {
	char pcba_ver[5];	/* PCBA_Version */
	char board_rev[16];	/* BOARD_REVISION */
	char soc[16];		/* SOC_NAME */
	char manufacturer[32];	/* MANUFACTURER */
	char lot_nr[12];	/* LOT_NUMBER */
} e = {0};

static int has_been_read = 0;

char *get_pcba_version(void)
{
	return e.pcba_ver;
}

static void show_eeprom(void)
{
	printf("PCBA_VERSION: %s\n", e.pcba_ver);
	printf("BOARD_REVISION: %s\n", e.board_rev);
	printf("SOC_NAME: %s\n", e.soc);
	printf("MANUFACTURER: %s\n", e.manufacturer);
	printf("LOT_NUMBER: %s\n", e.lot_nr);
}

#ifndef CONFIG_SYS_EEPROM_BUS_NUM
#define CONFIG_SYS_EEPROM_BUS_NUM	0xff
#endif
#ifndef CONFIG_SYS_I2C_EEPROM_ADDR
#define CONFIG_SYS_I2C_EEPROM_ADDR	0xff
#endif
__attribute__((weak)) int __read_eeprom(void *buffer, int size)
{
	struct udevice *dev;
	int ret;

	ret = i2c_get_chip_for_busnum(CONFIG_SYS_EEPROM_BUS_NUM,
		CONFIG_SYS_I2C_EEPROM_ADDR, 2, &dev);

	if (!ret) {
		for (int i = 0; i < size; i += 512) {
			ret |= dm_i2c_read(dev, i, buffer + i, 512);
		}
	}

	return ret;
}

#define EEPROM_SIZE	(2048)
static int read_eeprom(void)
{
	char data[EEPROM_SIZE] = {0};
	char *p = data;
	char key[32], value[48];
	int n, ret;

	if (has_been_read)
		return 0;

	ret = __read_eeprom(data, sizeof(data));

	has_been_read = (ret == 0) ? 1 : 0;

	while (*p) {
		ret = sscanf(p, "%[^:]: %[^\r\n]%n", key, value, &n);
		if (ret == 2) {
			if (strcmp(key, "PCBA_Version") == 0) {
				strncpy(e.pcba_ver, value, sizeof(e.pcba_ver) - 1);
				if (e.pcba_ver[0] == 'V') e.pcba_ver[0] = 'v';
			} else if (strcmp(key, "BOARD_REVISION") == 0) {
				strncpy(e.board_rev, value, sizeof(e.board_rev) - 1);
			} else if (strcmp(key, "SOC_NAME") == 0) {
				strncpy(e.soc, value, sizeof(e.soc) - 1);
			} else if (strcmp(key, "MANUFACTURER") == 0) {
				strncpy(e.manufacturer, value, sizeof(e.manufacturer) - 1);
			} else if (strcmp(key, "LOT_NUMBER") == 0) {
				strncpy(e.lot_nr, value, sizeof(e.lot_nr) - 1);
			} else if (strcmp(key, "MAC0") == 0) {
				env_set("ethaddr", value);
			} else if (strcmp(key, "MAC1") == 0) {
				env_set("eth1addr", value);
			} else if (strcmp(key, "MAC2") == 0) {
				env_set("eth2addr", value);
			} else if (strcmp(key, "MAC3") == 0) {
				env_set("eth3addr", value);
			}

			for (p += n; *p == '\r' || *p == '\n'; p++) {}
		} else {
			break;
		}
	}

	return 0;
}

int mac_read_from_eeprom(void)
{
	char serial_num[48];

	if (read_eeprom()) {
		printf("EEPROM read failed, continue booting...\n");
	}

	/* serial#=Ambarella N1-655 v110 H1234567890 */
	sprintf(serial_num, "Ambarella %s %s %s",
		e.soc, (e.board_rev[0] != '\0') ? e.board_rev : e.pcba_ver, e.lot_nr);
	env_set("serial#", serial_num);

	return 0;
}

int do_mac(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc == 1) {
		show_eeprom();
		return 0;
	}

	return 0;
}
