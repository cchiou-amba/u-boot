#include <common.h>
#include <command.h>
#include <env.h>
#include <i2c.h>
#include <net.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/errno.h>
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

static int parse_and_validate_mac(const char *str, uchar *enetaddr)
{
	int i;

	if (!str) {
		printf("Error: NULL MAC address string.\n");
		return -EINVAL;
	}

	if (strlen(str) != 17) {
		printf("Error: malformed MAC address '%s' (expected 17 chars: XX:XX:XX:XX:XX:XX).\n", str);
		return -EINVAL;
	}

	for (i = 0; i < 17; i++) {
		if ((i % 3) == 2) {
			if (str[i] != ':') {
				printf("Error: malformed delimiter '%c' at position %d (expected ':').\n", str[i], i);
				return -EINVAL;
			}
		} else {
			if (!isxdigit(str[i])) {
				printf("Error: non-hex digit '%c' at position %d.\n", str[i], i);
				return -EINVAL;
			}
		}
	}

	string_to_enetaddr(str, enetaddr);

	if (is_zero_ethaddr(enetaddr)) {
		printf("Error: all-zero MAC address '%s' is rejected.\n", str);
		return -EINVAL;
	}

	if (is_broadcast_ethaddr(enetaddr)) {
		printf("Error: broadcast MAC address '%s' is rejected.\n", str);
		return -EINVAL;
	}

	if (is_multicast_ethaddr(enetaddr)) {
		printf("Error: multicast MAC address '%s' is rejected.\n", str);
		return -EINVAL;
	}

	if (!is_valid_ethaddr(enetaddr)) {
		printf("Error: invalid MAC address '%s'.\n", str);
		return -EINVAL;
	}

	return 0;
}

int do_mac(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc == 1 || (argc == 2 && strcmp(argv[1], "show") == 0)) {
		const char *addr = env_get("ethaddr");
		if (addr && *addr)
			printf("Active ethaddr: %s\n", addr);
		else
			printf("Active ethaddr: not set\n");
		return CMD_RET_SUCCESS;
	}

	if (argc == 2 && strcmp(argv[1], "random") == 0) {
		uchar enetaddr[6];
		char formatted[18];
		int ret;

		net_random_ethaddr(enetaddr);
		snprintf(formatted, sizeof(formatted), "%02x:%02x:%02x:%02x:%02x:%02x",
			 enetaddr[0], enetaddr[1], enetaddr[2],
			 enetaddr[3], enetaddr[4], enetaddr[5]);

		ret = env_set("ethaddr", formatted);
		if (ret) {
			printf("Error: failed to set ethaddr in environment (%d).\n", ret);
			return CMD_RET_FAILURE;
		}

		ret = env_save();
		if (ret) {
			printf("Error: failed to persist ethaddr to boot0 environment (%d)!\n", ret);
			return CMD_RET_FAILURE;
		}

		printf("Generated and saved persistent ethaddr: %s\n", formatted);
		return CMD_RET_SUCCESS;
	}

	if (argc == 3 && strcmp(argv[1], "set") == 0) {
		uchar enetaddr[6];
		char formatted[18];
		int ret;

		if (parse_and_validate_mac(argv[2], enetaddr) != 0)
			return CMD_RET_FAILURE;

		snprintf(formatted, sizeof(formatted), "%02x:%02x:%02x:%02x:%02x:%02x",
			 enetaddr[0], enetaddr[1], enetaddr[2],
			 enetaddr[3], enetaddr[4], enetaddr[5]);

		ret = env_set("ethaddr", formatted);
		if (ret) {
			printf("Error: failed to set ethaddr in environment (%d).\n", ret);
			return CMD_RET_FAILURE;
		}

		ret = env_save();
		if (ret) {
			printf("Error: failed to persist ethaddr to boot0 environment (%d)!\n", ret);
			return CMD_RET_FAILURE;
		}

		printf("Persistent ethaddr set and saved: %s\n", formatted);
		return CMD_RET_SUCCESS;
	}

	return CMD_RET_USAGE;
}

int ambarella_board_mac_init(void)
{
	uchar enetaddr[6];
	char formatted[18];
	const char *env_mac;
	int ret;

	env_mac = env_get("ethaddr");
	if (env_mac && *env_mac) {
		string_to_enetaddr(env_mac, enetaddr);
		if (is_valid_ethaddr(enetaddr)) {
			/* Already valid and persistent */
			return 0;
		}
		printf("Warning: existing ethaddr '%s' is invalid.\n", env_mac);
	}

	/* Absent or invalid: generate one random locally administered unicast address */
	net_random_ethaddr(enetaddr);
	snprintf(formatted, sizeof(formatted), "%02x:%02x:%02x:%02x:%02x:%02x",
		 enetaddr[0], enetaddr[1], enetaddr[2],
		 enetaddr[3], enetaddr[4], enetaddr[5]);

	printf("Generated one-time persistent MAC address: %s\n", formatted);
	ret = env_set("ethaddr", formatted);
	if (ret) {
		printf("ERROR: Failed to set ethaddr in environment (%d)!\n", ret);
		return ret;
	}

	ret = env_save();
	if (ret) {
		printf("ERROR: Failed to persist ethaddr to boot0 environment (%d)!\n", ret);
		return ret;
	}

	printf("Saved persistent ethaddr to boot0 environment.\n");
	return 0;
}
