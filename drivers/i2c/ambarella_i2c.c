// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 *
 */

//#define DEBUG
#include <common.h>
#include <dm.h>
#include <log.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <dm/root.h>
#include <i2c.h>
#include <fdtdec.h>
#include <mapmem.h>
#include <wait_bit.h>
#include <clk.h>

#include <asm/arch/soc.h>
#include <asm/arch/misc.h>

DECLARE_GLOBAL_DATA_PTR;
/* ==========================================================================*/

#define IDC_ENR_OFFSET			0x00
#define IDC_CTRL_OFFSET			0x04
#define IDC_DATA_OFFSET			0x08
#define IDC_STS_OFFSET			0x0c
#define IDC_PSLL_OFFSET			0x10
#define IDC_PSLH_OFFSET			0x14
#define IDC_FMCTRL_OFFSET		0x18
#define IDC_FMDATA_OFFSET		0x1c
#define IDC_PSHS_OFFSET			0x20
#define IDC_DUTYCYCLE_OFFSET		0x24
#define IDC_STRETCHSCL_OFFSET		0x28

#define IDC_ENR_REG_ENABLE		(0x01)
#define IDC_ENR_REG_DISABLE		(0x00)

#define IDC_CTRL_HSMODE			(0x10)
#define IDC_CTRL_STOP			(0x08)
#define IDC_CTRL_START			(0x04)
#define IDC_CTRL_IF			(0x02)
#define IDC_CTRL_ACK			(0x01)
#define IDC_CTRL_CLS			(0x00)

#define IDC_STS_FIFO_EMP		(0x04)
#define IDC_STS_FIFO_FUL		(0x02)

#define IDC_FIFO_BUF_SIZE		(63)

#define IDC_FMCTRL_HSMODE		(0x10)
#define IDC_FMCTRL_STOP			(0x08)
#define IDC_FMCTRL_START		(0x04)
#define IDC_FMCTRL_IF			(0x02)

/* ==========================================================================*/
#define I2C_TIMEOUT_MS                100
#define CONFIG_I2C_AMBARELLA_RETRIES		(3)
#define AMBARELLA_I2C_STOP_WAIT_INTERVAL_US (10)
#define AMBARELLA_I2C_STOP_WAIT_TIME_US (5 * AMBARELLA_I2C_STOP_WAIT_INTERVAL_US)

enum ambarella_i2c_state {
	AMBA_I2C_STATE_IDLE,
	AMBA_I2C_STATE_START,
	AMBA_I2C_STATE_START_TEN,
	AMBA_I2C_STATE_START_NEW,
	AMBA_I2C_STATE_READ,
	AMBA_I2C_STATE_READ_STOP,
	AMBA_I2C_STATE_WRITE,
	AMBA_I2C_STATE_WRITE_WAIT_ACK,
	AMBA_I2C_STATE_BULK_WRITE,
	AMBA_I2C_STATE_NO_ACK,
	AMBA_I2C_STATE_ERROR,
	AMBA_I2C_STATE_HS_MODE
};

enum ambarella_i2c_hw_state {
	AMBA_I2C_HW_STATE_IDLE,
	AMBA_I2C_HW_STATE_START_COMMAND,
	AMBA_I2C_HW_STATE_TX_DATA,
	AMBA_I2C_HW_STATE_RX_ACK,
	AMBA_I2C_HW_STATE_COMMAND_FINISH,
	AMBA_I2C_HW_STATE_STOP_COMMAND,
	AMBA_I2C_HW_STATE_RX_DATA,
	AMBA_I2C_HW_STATE_TX_ACK
};

struct ambarella_i2c_dev_info {
	void __iomem 			*regbase;
	enum ambarella_i2c_state		state;
	struct udevice			*dev;

	u32					clk_limit;
	u32					hs_clk_limit;
	u32					bulk_num;
	u32					duty_cycle;
	u32					stretch_scl;
	u32					turbo_mode;
	u32					master_code;
	bool					hs_mode;
	bool					hsmode_enter;

	struct i2c_msg				*msgs;
	u16					msg_num;
	u16					msg_addr;
	unsigned int				msg_index;
};

/* ======================================================================= */
static enum ambarella_i2c_hw_state ambarella_i2c_get_hw_state(struct ambarella_i2c_dev_info *pinfo)
{
	u32 status_reg;

	status_reg = readl(pinfo->regbase + IDC_STS_OFFSET);

	return (status_reg >> 4) & 0xF;
}

static inline void ambarella_i2c_set_clk(struct ambarella_i2c_dev_info *pinfo)
{
	unsigned int				apb_clk;
	u32					idc_prescale;

	apb_clk = get_apb_bus_freq_hz();

	writel_relaxed(IDC_ENR_REG_DISABLE, pinfo->regbase + IDC_ENR_OFFSET);

	idc_prescale =( ((apb_clk / pinfo->clk_limit) - 2)/(4 + pinfo->duty_cycle)) - 1;

	debug("apb_clk[%dHz]\n", apb_clk);
	debug("idc_prescale[%d]\n", idc_prescale);
	debug("duty_cycle[%d]\n", pinfo->duty_cycle);
	debug("clk[%dHz]\n",
		(apb_clk / ((idc_prescale + 1) << 2)));

	writeb_relaxed(idc_prescale, pinfo->regbase + IDC_PSLL_OFFSET);
	writeb_relaxed(idc_prescale >> 8, pinfo->regbase + IDC_PSLH_OFFSET);

	writeb_relaxed(pinfo->duty_cycle, pinfo->regbase + IDC_DUTYCYCLE_OFFSET);
	writeb_relaxed(pinfo->stretch_scl, pinfo->regbase + IDC_STRETCHSCL_OFFSET);

	writel_relaxed(IDC_ENR_REG_ENABLE, pinfo->regbase + IDC_ENR_OFFSET);
}

static inline void ambarella_i2c_set_hs_clk(struct ambarella_i2c_dev_info *pinfo)
{
	unsigned int				apb_clk;
	u32					idc_prescale;

	apb_clk = get_apb_bus_freq_hz();

	writel_relaxed(IDC_ENR_REG_DISABLE, pinfo->regbase + IDC_ENR_OFFSET);

	idc_prescale = (apb_clk / (6 * pinfo->hs_clk_limit)) - (4 / 3);

	debug("apb_clk[%dHz]\n", apb_clk);
	debug("idc_prescale[%d]\n", idc_prescale);

	writeb_relaxed(idc_prescale, pinfo->regbase + IDC_PSHS_OFFSET);

	writel_relaxed(IDC_ENR_REG_ENABLE, pinfo->regbase + IDC_ENR_OFFSET);
}

static inline void ambarella_i2c_hw_init(struct ambarella_i2c_dev_info *pinfo)
{
	ambarella_i2c_set_clk(pinfo);

	if (pinfo->hs_mode)
		ambarella_i2c_set_hs_clk(pinfo);

	pinfo->msgs = NULL;
	pinfo->msg_num = 0;
	pinfo->state = AMBA_I2C_STATE_IDLE;
}

static inline void ambarella_i2c_send_master_code(
	struct ambarella_i2c_dev_info *pinfo)
{
	writeb_relaxed(pinfo->master_code, pinfo->regbase + IDC_DATA_OFFSET);
	writel_relaxed(IDC_CTRL_START, pinfo->regbase + IDC_CTRL_OFFSET);
}

static inline void ambarella_i2c_start_single_msg(
	struct ambarella_i2c_dev_info *pinfo)
{
	__u32 hs_mode;

	if (pinfo->msgs->flags & I2C_M_TEN) {
		pinfo->state = AMBA_I2C_STATE_START_TEN;
		writeb_relaxed((0xf0 | ((pinfo->msg_addr >> 8) & 0x07)),
					pinfo->regbase + IDC_DATA_OFFSET);
	} else {
		pinfo->state = AMBA_I2C_STATE_START;
		writeb_relaxed(pinfo->msg_addr, pinfo->regbase + IDC_DATA_OFFSET);
	}

	hs_mode = (pinfo->hs_mode) ? (IDC_CTRL_HSMODE) : (0U);
	writel_relaxed(IDC_CTRL_START | hs_mode, pinfo->regbase + IDC_CTRL_OFFSET);
}

static inline void ambarella_i2c_bulk_write(
	struct ambarella_i2c_dev_info *pinfo,
	__u32 fifosize)
{
	while (fifosize--) {
		writeb_relaxed(pinfo->msgs->buf[pinfo->msg_index++],
				pinfo->regbase + IDC_FMDATA_OFFSET);
		if (pinfo->msg_index >= pinfo->msgs->len)
			break;
	};

	/* the last fifo data MUST be STOP+IF */
	writel_relaxed(IDC_FMCTRL_IF | IDC_FMCTRL_STOP,
		pinfo->regbase + IDC_FMCTRL_OFFSET);
}

static inline void ambarella_i2c_start_bulk_msg_write(
	struct ambarella_i2c_dev_info *pinfo)
{
	__u32				fifosize = IDC_FIFO_BUF_SIZE;
	__u32				hs_mode;

	pinfo->state = AMBA_I2C_STATE_BULK_WRITE;

	writel_relaxed(0, pinfo->regbase + IDC_CTRL_OFFSET);

	hs_mode = (pinfo->hs_mode) ? (IDC_FMCTRL_HSMODE) : (0U);
	writel_relaxed(IDC_FMCTRL_START | hs_mode, pinfo->regbase + IDC_FMCTRL_OFFSET);

	if (pinfo->msgs->flags & I2C_M_TEN) {
		writeb_relaxed((0xf0 | ((pinfo->msg_addr >> 8) & 0x07)),
				pinfo->regbase + IDC_FMDATA_OFFSET);
		fifosize--;
	}
	writeb_relaxed(pinfo->msg_addr, pinfo->regbase + IDC_FMDATA_OFFSET);
	fifosize -= 2;

	ambarella_i2c_bulk_write(pinfo, fifosize);
}

static inline void ambarella_i2c_start_current_msg(
	struct ambarella_i2c_dev_info *pinfo)
{
	pinfo->msg_index = 0;
	pinfo->msg_addr = (pinfo->msgs->addr << 1);

	if (pinfo->msgs->flags & I2C_M_RD)
		pinfo->msg_addr |= 1;

	if (pinfo->msgs->flags & I2C_M_REV_DIR_ADDR)
		pinfo->msg_addr ^= 1;

	if (pinfo->msgs->flags & I2C_M_RD) {
		ambarella_i2c_start_single_msg(pinfo);
	} else if (pinfo->turbo_mode) {
		ambarella_i2c_start_bulk_msg_write(pinfo);
	} else {
		ambarella_i2c_start_single_msg(pinfo);
	}

	debug("msg_addr[0x%x], len[0x%x] \n",
		pinfo->msg_addr, pinfo->msgs->len);
	udelay(1000);
}

static inline void ambarella_i2c_stop(
	struct ambarella_i2c_dev_info *pinfo,
	enum ambarella_i2c_state state,
	__u32 *pack_control)
{
	if(state != AMBA_I2C_STATE_IDLE) {
		*pack_control |= IDC_CTRL_ACK;
	}

	pinfo->state = state;
	pinfo->msgs = NULL;
	pinfo->msg_num = 0;

	*pack_control |= IDC_CTRL_STOP;

	if (pinfo->hs_mode)
		*pack_control &= ~IDC_CTRL_HSMODE;

	if (pinfo->state == AMBA_I2C_STATE_IDLE ||
		pinfo->state == AMBA_I2C_STATE_NO_ACK) {
	} else
		udelay(10);
}

static inline __u32 ambarella_i2c_check_ack(
	struct ambarella_i2c_dev_info *pinfo,
	__u32 *pack_control,
	__u32 retry_counter)
{
	__u32				retVal = IDC_CTRL_ACK;

ambarella_i2c_check_ack_enter:
	if (unlikely((*pack_control) & IDC_CTRL_ACK)) {
		if (pinfo->msgs->flags & I2C_M_IGNORE_NAK)
			goto ambarella_i2c_check_ack_exit;

		if ((pinfo->msgs->flags & I2C_M_RD) &&
			(pinfo->msgs->flags & I2C_M_NO_RD_ACK))
			goto ambarella_i2c_check_ack_exit;

		if (retry_counter--) {
			udelay(100);
			*pack_control = readl_relaxed(pinfo->regbase + IDC_CTRL_OFFSET);
			goto ambarella_i2c_check_ack_enter;
		}
		retVal = 0;
		*pack_control = 0;
		ambarella_i2c_stop(pinfo,
			AMBA_I2C_STATE_NO_ACK, pack_control);
	}

ambarella_i2c_check_ack_exit:
	return retVal;
}

/* Wait for an interrupt */
static void ambarella_i2c_wait(struct ambarella_i2c_dev_info *pinfo)
{
	u32				status_reg;
	u32				control_reg;
	u32				ack_control = IDC_CTRL_CLS;

	status_reg = readl(pinfo->regbase + IDC_STS_OFFSET);
	control_reg = readl(pinfo->regbase + IDC_CTRL_OFFSET);

	if (pinfo->hs_mode)
		ack_control |= IDC_CTRL_HSMODE;

	debug("state[0x%x]\n", pinfo->state);
	debug("status_reg[0x%x]\n", status_reg);
	debug("control_reg[0x%x]\n", control_reg);

	switch (pinfo->state) {
	case AMBA_I2C_STATE_START:
		if (ambarella_i2c_check_ack(pinfo, &control_reg,
			1) == IDC_CTRL_ACK) {
			if (pinfo->msgs->flags & I2C_M_RD) {
				if (pinfo->msgs->len == 1)
					ack_control |= IDC_CTRL_ACK;
				pinfo->state = AMBA_I2C_STATE_READ;
			} else {
				pinfo->state = AMBA_I2C_STATE_WRITE;
				goto amba_i2c_irq_write;
			}
		} else {
			ack_control = control_reg;
		}
		break;
	case AMBA_I2C_STATE_START_TEN:
		pinfo->state = AMBA_I2C_STATE_START;
		writeb(pinfo->msg_addr, pinfo->regbase + IDC_DATA_OFFSET);
		break;
	case AMBA_I2C_STATE_START_NEW:
amba_i2c_irq_start_new:
		ambarella_i2c_start_current_msg(pinfo);
		goto amba_i2c_irq_exit;
		break;
	case AMBA_I2C_STATE_READ_STOP:
		pinfo->msgs->buf[pinfo->msg_index] =
			readb(pinfo->regbase + IDC_DATA_OFFSET);
		pinfo->msg_index++;
amba_i2c_irq_read_stop:
		ambarella_i2c_stop(pinfo, AMBA_I2C_STATE_IDLE, &ack_control);
		break;
	case AMBA_I2C_STATE_READ:
		pinfo->msgs->buf[pinfo->msg_index] =
			readb(pinfo->regbase + IDC_DATA_OFFSET);
		pinfo->msg_index++;

		if (pinfo->msg_index >= pinfo->msgs->len - 1) {
			if (pinfo->msg_num > 1) {
				pinfo->msgs++;
				pinfo->state = AMBA_I2C_STATE_START_NEW;
				pinfo->msg_num--;
			} else {
				if (pinfo->msg_index > pinfo->msgs->len - 1) {
					goto amba_i2c_irq_read_stop;
				} else {
					pinfo->state = AMBA_I2C_STATE_READ_STOP;
					ack_control |= IDC_CTRL_ACK;
				}
			}
		}
		break;
	case AMBA_I2C_STATE_WRITE:
amba_i2c_irq_write:
		pinfo->state = AMBA_I2C_STATE_WRITE_WAIT_ACK;
		if(pinfo->msgs->len)
			writeb(pinfo->msgs->buf[pinfo->msg_index],
				pinfo->regbase + IDC_DATA_OFFSET);
		break;
	case AMBA_I2C_STATE_WRITE_WAIT_ACK:
		if (ambarella_i2c_check_ack(pinfo, &control_reg,
			1) == IDC_CTRL_ACK) {
			pinfo->state = AMBA_I2C_STATE_WRITE;
			pinfo->msg_index++;

			if (pinfo->msg_index >= pinfo->msgs->len) {
				if (pinfo->msg_num > 1) {
					pinfo->msgs++;
					pinfo->state = AMBA_I2C_STATE_START_NEW;
					pinfo->msg_num--;
					goto amba_i2c_irq_start_new;
				}
				ambarella_i2c_stop(pinfo,
					AMBA_I2C_STATE_IDLE, &ack_control);
			} else {
				goto amba_i2c_irq_write;
			}
		} else {
			ack_control = control_reg;
		}
		break;
	case AMBA_I2C_STATE_BULK_WRITE:
		while (((status_reg & 0xF0) != 0x50) && ((status_reg & 0xF0) != 0x00)) {
			cpu_relax();
			status_reg = readl(pinfo->regbase + IDC_STS_OFFSET);
		};
		if (pinfo->msg_num > 1) {
			pinfo->msgs++;
			pinfo->state = AMBA_I2C_STATE_START_NEW;
			pinfo->msg_num--;
			goto amba_i2c_irq_start_new;
		}
		ambarella_i2c_stop(pinfo, AMBA_I2C_STATE_IDLE, &ack_control);
		goto amba_i2c_irq_exit;
	case AMBA_I2C_STATE_HS_MODE:
		pinfo->hsmode_enter = false;
		//wake_up(&pinfo->msg_wait);
		goto amba_i2c_irq_exit;
	default:
		printf("ambarella_i2c_irq in wrong state[0x%x]\n",
			pinfo->state);
		printf("status_reg[0x%x]\n", status_reg);
		printf("control_reg[0x%x]\n", control_reg);
		ack_control = IDC_CTRL_STOP | IDC_CTRL_ACK;
		pinfo->state = AMBA_I2C_STATE_ERROR;
		break;
	}

	writel(ack_control, pinfo->regbase + IDC_CTRL_OFFSET);

amba_i2c_irq_exit:
	return;
}

static int ambarella_i2c_probe(struct udevice *dev)
{
	struct ambarella_i2c_dev_info *pinfo = dev_get_priv(dev);

	ambarella_i2c_hw_init(pinfo);

	return 0;
}

static int ambarella_i2c_xfer(struct udevice *bus, struct i2c_msg *msg,
			 int nmsgs)
{
	struct ambarella_i2c_dev_info *pinfo = dev_get_priv(bus);
	int	errorCode = -EPERM;
	int retryCount;
	int	hw_state;
	int	wait_stop_retry;
	int	i;
	ulong start_time;
	u32 timeout = 0;

	/* check data length for FIFO mode */
	if (unlikely(pinfo->turbo_mode)) {
		pinfo->msgs = msg;
		pinfo->msg_num = nmsgs;
		for (i = 0 ; i < pinfo->msg_num; i++) {
			if ((!(pinfo->msgs->flags & I2C_M_RD)) &&
				(pinfo->msgs->len > IDC_FIFO_BUF_SIZE - 2)) {
				printf("Turbo(FIFO) mode can only support <= "
					"%d bytes writing, but message[%d]: %d bytes applied!\n",
					IDC_FIFO_BUF_SIZE - 2, i, pinfo->msgs->len);

				return -EPERM;
			}
			pinfo->msgs++;
		}
	}

	for (retryCount = 0; retryCount <= CONFIG_I2C_AMBARELLA_RETRIES; retryCount++) {
		errorCode = 0;
		timeout = 0;

		if (pinfo->hs_mode) {
			pinfo->state = AMBA_I2C_STATE_HS_MODE;
			pinfo->hsmode_enter = true;
			ambarella_i2c_send_master_code(pinfo);

			start_time = get_timer(0);
			do {
				udelay(100);
				ambarella_i2c_wait(pinfo);

				if (get_timer(start_time) > 1000) {
					debug("i2c: hsmode timeout\n");
					timeout = 1;
					break;
				}
			}while (pinfo->hsmode_enter == true);

			if (timeout) {
				pinfo->state = AMBA_I2C_STATE_NO_ACK;
			}
			//debug("enter hs mode %ld jiffies left.\n", timeout);
			pinfo->hsmode_enter = false;
		}

		if (pinfo->state != AMBA_I2C_STATE_IDLE)
			ambarella_i2c_hw_init(pinfo);
		pinfo->msgs = msg;
		pinfo->msg_num = nmsgs;

		ambarella_i2c_start_current_msg(pinfo);

		start_time = get_timer(0);
		do {
			ambarella_i2c_wait(pinfo);

			if (get_timer(start_time) > I2C_TIMEOUT_MS) {
				debug("i2c: timeout\n");
				timeout = 1;
				break;
			}
			udelay(100);
		} while (pinfo->msg_num != 0);

		/* We need to check HW state to ensure the controller has already entered idle indeed, or if slave device
		 * holds the SCL due to busy, i2c gpio muxer might turn off bus before the STOP is really sent out. */
		wait_stop_retry = AMBARELLA_I2C_STOP_WAIT_TIME_US / AMBARELLA_I2C_STOP_WAIT_INTERVAL_US;
		while (wait_stop_retry--) {
			hw_state = ambarella_i2c_get_hw_state(pinfo);
			if (hw_state == AMBA_I2C_HW_STATE_IDLE)
				break;

			udelay(AMBARELLA_I2C_STOP_WAIT_INTERVAL_US);
		}

		if (hw_state != AMBA_I2C_HW_STATE_IDLE)
			debug("Xfer exits with non-idle hw state %d.\n", hw_state);

		if (timeout) {
			pinfo->state = AMBA_I2C_STATE_NO_ACK;
		}
		//debug("%ld jiffies left.\n", timeout);

		if (pinfo->state != AMBA_I2C_STATE_IDLE) {
			errorCode = -EBUSY;
		} else {
			break;
		}
	}

	if (errorCode) {
		if (pinfo->state == AMBA_I2C_STATE_NO_ACK) {
			debug("No ACK from address 0x%x, %d:%d!\n",
				pinfo->msg_addr, pinfo->msg_num,
				pinfo->msg_index);
		}
		return errorCode;
	}

	return 0;
}


static int ambarella_i2c_ofdata_to_platdata(struct udevice *dev)
{
	int ret;
	struct ambarella_i2c_dev_info *pinfo = dev_get_priv(dev);

	pinfo->regbase = (void *)dev_read_addr(dev);
	if (!pinfo->regbase)
		return -ENOMEM;

	pinfo->dev = dev;
	pinfo->clk_limit = dev_read_u32_default(dev, "clock-frequency", 100000);
	ret = dev_read_u32(dev, "amb,duty-cycle", &pinfo->duty_cycle);
	if (ret < 0) {
		debug("Missing duty-cycle, assuming 1:1!\n");
		pinfo->duty_cycle = 0;
	}

	if (pinfo->duty_cycle > 2)
		pinfo->duty_cycle = 2;

	ret = dev_read_u32(dev, "amb,stretch-scl", &pinfo->stretch_scl);
	if (ret < 0) {
		debug("Missing stretch-scl, assuming 1:1!\n");
		pinfo->stretch_scl = 1;
	}

	pinfo->turbo_mode = 0;
	pinfo->hs_mode = 0;

	return 0;
}

static const struct dm_i2c_ops ambarella_i2c_ops = {
	.xfer = ambarella_i2c_xfer,
	//.set_bus_speed = ambarella_i2c_set_bus_speed,
};

static const struct udevice_id ambarella_i2c_of_match[] = {
	{ .compatible = "ambarella,i2c" },
	{ /* end of table */ }
};

U_BOOT_DRIVER(ambarella_i2c) = {
	.name = "ambarella_i2c",
	.id = UCLASS_I2C,
	.of_match = ambarella_i2c_of_match,
	.probe = ambarella_i2c_probe,
	.ofdata_to_platdata = ambarella_i2c_ofdata_to_platdata,
	.priv_auto_alloc_size = sizeof(struct ambarella_i2c_dev_info),
	.ops = &ambarella_i2c_ops,
};
