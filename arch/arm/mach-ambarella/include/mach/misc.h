#ifndef __MACH_MISC_H__
#define __MACH_MISC_H__

struct pinmux_config {
	unsigned short pin;
	unsigned short alt;
};

/* ------------------------------------ */

void rct_writel(unsigned long reg, unsigned int val);
unsigned int rct_readl(unsigned long reg);
int rct_system_boot_from(void);
int rct_system_config(void);

int pinmux_config_set_item(const struct pinmux_config *item,
		int n_item);

void plat_r_board_late_init(void);
void plat_r_reset_cpu(void);
int plat_f_dram_init(void);

u32 get_core_bus_freq_hz(void);
u32 get_ahb_bus_freq_hz(void);
u32 get_apb_bus_freq_hz(void);

void rct_set_sd_pll(int slot, u32 freq_hz);
u32 get_sd_freq_hz(int slot);

u32 get_nand_freq_hz(void);

int eth_get_mac_from_eeprom(char *eeprom_buf, const char *str, char *mac_str);
int read_eeprom(int bus_addr, int dev_addr);

#endif
