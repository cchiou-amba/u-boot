#include <common.h>
#include <command.h>
#include <env.h>
#include <i2c.h>
#include <linux/delay.h>
#include "eeprom.h"

static struct eeprom {
	char pcba_ver[5]; /* PCBA_Version */
} e;

static int has_been_read = 0;

char *get_pcba_version(void)
{
	return e.pcba_ver;
}

#define EEPROM_SIZE	(2048)
static int read_eeprom(void)
{
	char data[EEPROM_SIZE] = {0};
	char *p = data;
	struct udevice *dev;
	char key[32], value[48];
	int n, ret;

	if (has_been_read)
		return 0;

	ret = i2c_get_chip_for_busnum(CONFIG_SYS_EEPROM_BUS_NUM,
				      CONFIG_SYS_I2C_EEPROM_ADDR, 2, &dev);

	if (!ret) {
		for (int i = 0; i < sizeof(data); i += 32)
			ret = dm_i2c_read(dev, i, (void *)(data + i), 32);
	}
	has_been_read = (ret == 0) ? 1 : 0;

	while (*p) {
		ret = sscanf(p, "%[^:]: %[^\r\n]%n", key, value, &n);
		if (ret == 2) {
			if (strcmp(key, "PCBA_Version") == 0) {
				strcpy(e.pcba_ver, value);
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
	if (read_eeprom()) {
		printf("EEPROM read failed, continue booting...\n");
	}

	return 0;
}

int do_mac(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	printf("Not supported\n");

	return 0;
}
