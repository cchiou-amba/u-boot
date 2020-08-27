#include <common.h>
#include <env.h>
#include <hang.h>
#include <serial.h>
#include <stdio_dev.h>
#include <post.h>
#include <linux/compiler.h>
#include <dm.h>
#include <errno.h>
#include <asm/arch/soc.h>
#include <asm/arch/misc.h>

#define UART_RB_OFFSET			0x00
#define UART_TH_OFFSET			0x00
#define UART_DLL_OFFSET			0x00
#define UART_IE_OFFSET			0x04
#define UART_DLH_OFFSET			0x04
#define UART_II_OFFSET			0x08
#define UART_FC_OFFSET			0x08
#define UART_LC_OFFSET			0x0c
#define UART_MC_OFFSET			0x10
#define UART_LS_OFFSET			0x14
#define UART_MS_OFFSET			0x18
#define UART_SC_OFFSET			0x1c	/* Byte */
#define UART_DMAE_OFFSET		0x28
#define UART_DMAF_OFFSET		0x40	/* DMA fifo */
#define UART_US_OFFSET			0x7c
#define UART_TFL_OFFSET			0x80
#define UART_RFL_OFFSET			0x84
#define UART_SRR_OFFSET			0x88

#define UART_LC_DLAB			0x80
#define UART_LC_BRK			0x40
#define UART_LC_EVEN_PARITY		0x10
#define UART_LC_ODD_PARITY		0x00
#define UART_LC_PEN			0x08
#define UART_LC_STOP_2BIT		0x04
#define UART_LC_STOP_1BIT		0x00
#define UART_LC_CLS_8_BITS		0x03
#define UART_LC_CLS_7_BITS		0x02
#define UART_LC_CLS_6_BITS		0x01
#define UART_LC_CLS_5_BITS		0x00
/*	quick defs */
#define	UART_LC_8N1			0x03
#define	UART_LC_7E1			0x0a

/* UART[x]_MC_REG */
#define UART_MC_SIRE			0x40
#define UART_MC_AFCE			0x20
#define UART_MC_LB			0x10
#define UART_MC_OUT2			0x08
#define UART_MC_OUT1			0x04
#define UART_MC_RTS			0x02
#define UART_MC_DTR			0x01

/* UART[x]_LS_REG */
#define UART_LS_FERR			0x80
#define UART_LS_TEMT			0x40
#define UART_LS_THRE			0x20
#define UART_LS_BI			0x10
#define UART_LS_FE			0x08
#define UART_LS_PE			0x04
#define UART_LS_OE			0x02
#define UART_LS_DR			0x01

/* UART[x]_MS_REG */
#define UART_MS_DCD			0x80
#define UART_MS_RI			0x40
#define UART_MS_DSR			0x20
#define UART_MS_CTS			0x10
#define UART_MS_DDCD			0x08
#define UART_MS_TERI			0x04
#define UART_MS_DDSR			0x02
#define UART_MS_DCTS			0x01

/* UART[x]_US_REG */
#define UART_US_RFF			0x10
#define UART_US_RFNE			0x08
#define UART_US_TFE			0x04
#define UART_US_TFNF			0x02
#define UART_US_BUSY			0x01
#define UART_DBG_CLK_DIV		1

static void __serial_putc(const char c)
{
	while (!(readb(UART_BASE + UART_LS_OFFSET) & UART_LS_TEMT));
	writeb(c, UART_BASE + UART_TH_OFFSET);
}

static int __serial_poll(void)
{
	return !!(readb(UART_BASE + UART_LS_OFFSET) & UART_LS_DR);
}

static int amb_serial_init(void)
{

	unsigned short dl;
	unsigned int clk;
	int baudrate;

	writeb(0, UART_BASE + UART_SRR_OFFSET);

#ifdef CONFIG_BAUDRATE
	baudrate = CONFIG_BAUDRATE;
#else
	baudrate = 115200
#endif
	clk = rct_get_uart_pll();
	dl = clk * 10 / baudrate / 16;
	if (dl % 10 >= 5)
		dl = (dl / 10) + 1;
	else
		dl = (dl / 10);

	writeb(UART_LC_DLAB, UART_BASE + UART_LC_OFFSET);
	writeb(dl & 0xff, UART_BASE + UART_DLL_OFFSET);
	writeb(dl >> 8, UART_BASE + UART_DLH_OFFSET);
	writeb(UART_LC_8N1, UART_BASE + UART_LC_OFFSET);

	return 0;
}

static void amb_serial_putc(const char c)
{
	if (c == '\n')
		__serial_putc('\r');
	__serial_putc(c);
}

static int amb_serial_getc(void)
{
	while (!(__serial_poll()));

	return readb(UART_BASE + UART_RB_OFFSET);
}

static int amb_serial_tstc(void)
{
	return __serial_poll();
}

static void amb_serial_setbrg(void)
{
}


static struct serial_device amb_serial_drv = {
	.name	= "amb_serial",
	.start	= amb_serial_init,
	.stop	= NULL,
	.setbrg	= amb_serial_setbrg,
	.putc	= amb_serial_putc,
	.puts	= default_serial_puts,
	.getc	= amb_serial_getc,
	.tstc	= amb_serial_tstc,
};

__weak struct serial_device *default_serial_console(void)
{
	return &amb_serial_drv;
}

__printf__(const char *s)
{
	char *c = s;

	while(*c) {
		amb_serial_putc(*c++);
	}
}
