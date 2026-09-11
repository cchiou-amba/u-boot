#ifndef __MACH_SOC_CV5_H__
#define __MACH_SOC_CV5_H__

#define DRAM_SPACE_START	0x0000000000
#define DRAM_SPACE_SIZE		0x1000000000
#define DEVICE_SPACE_START	0x1000000000
#define DEVICE_SPACE_SIZE	0x2000000000

#define AHB_BASE		0x20E0000000
#define APB_BASE		0x20E4000000
#define AXI_BASE		0x20F2000000

#define UARTD_BASE		0x20E4000000		/* UART DBG */
#define IOMUX_BASE		0x20E4010000
#define RCT_BASE		0x20ED080000
#define AHBSP_NS_BASE		0x20E0024000

/*
 * GIC
 */
#define CONFIG_GICV2
#define GICD_BASE		0x20F3001000
#define GICC_BASE		0x20F3002000

#define MAX_GPIO_NUM		139

/*
 * RCT register
 */

#define FIO_RESET_OFFSET		0x074
#define FIO_RESET_FIO_RST		0x00000008
#define FIO_RESET_CF_RST		0x00000004
#define FIO_RESET_XD_RST		0x00000002
#define FIO_RESET_FLASH_RST		0x00000001

#define SYS_CONFIG_OFFSET		0x034
#define SYS_CONFIG_NAND_SPINAND		0xffffffff /* not used, spinand only */
#define SYS_CONFIG_NAND_SCKMODE		0x00040000
#define SYS_CONFIG_NAND_4K_FIFO		0xffffffff /* not used */
#define SYS_CONFIG_NAND_8K_FIFO		0x00100000 /* not used */
#define SYS_CONFIG_NAND_PAGE_SIZE	0x00020000
#define SYS_CONFIG_NAND_READ_CONFIRM	0xffffffff /* not used */
#define SYS_CONFIG_NAND_ECC_BCH_EN	0x00008000
#define SYS_CONFIG_NAND_ECC_SPARE_2X	0x00004000

#define SYS_CONFIG_BOOT_SPINOR		(0b00 << 4)
#define SYS_CONFIG_BOOT_NAND		(0b01 << 4)
#define SYS_CONFIG_BOOT_EMMC		(0b10 << 4)
#define SYS_CONFIG_BOOT_RSVD		(0b11 << 4)
#define SYS_CONFIG_BOOT_MASK		(0b11 << 4)

#define RCT_REG(x)			(RCT_BASE + (x))
#define SYS_CONFIG_REG                  RCT_REG(SYS_CONFIG_OFFSET)

#define CLK_SI_INPUT_MODE_OFFSET      0xBC
#define CLK_SI_INPUT_MODE_REG         RCT_REG(CLK_SI_INPUT_MODE_OFFSET)

#define POC_PERIPHERAL_CLK_MODE         0x04000000
#define POC_ORC_CLK_MODE                0x00000000

/* DRAM ctrl register */
#define DRAMC_PHYS_BASE                         0x1000000000
#define DRAM_DRAM_OFFSET                        0x00000
#define DRAMC_DRAM_BASE                         (DRAMC_PHYS_BASE + DRAM_DRAM_OFFSET)
#define DRAMC_DDRC_BASE                         (DRAMC_PHYS_BASE + DRAM_DDRC_OFFSET)

#define DRAM_REG(x)				(DRAMC_DRAM_BASE + (x))
/* Dram registers offset*/
#define REG_DRAM_MODE				0x000

#define DRAM_BURST_SIZE(x)			64

#define IDSP_RAM_START          (1ULL << 32)
#define FRAMEBUFFER_SIZE		0

#define DRAM_START_ADDR         DRAM_SPACE_START

#endif
