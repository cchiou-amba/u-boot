// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

//#define DEBUG

#include <common.h>
#include <command.h>
#include <config.h>
#include <cpu_func.h>
#include <net.h>
#include <dm.h>
#include <malloc.h>
#include <regmap.h>
#include <syscon.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/dma-direction.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <asm/arch/soc.h>


#include "ambarella_udc.h"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

//setup and data descriptor must be 16-byte aligned
#define DESC_ENT_ALIGN roundup(ARCH_DMA_MINALIGN, 16)
//data buffer pointer must be 8-byte aligned
#define DATA_BUF_ALIGN roundup(ARCH_DMA_MINALIGN, 8)

static const char		gadget_name[] = "ambarella_udc";
//static const char 		ep0name [] = "ep0";

static const char *amb_ep_string[] = {
	"ep0in", "ep1in", "ep2in", "ep3in",
	"ep4in", "ep5in", "ep6in", "ep7in",
	"ep8in", "ep9in", "ep10in", "ep11in",
	"ep12in", "ep13in", "ep14in", "ep15in",

	"ep0out", "ep1out", "ep2out", "ep3out",
	"ep4out", "ep5out", "ep6out", "ep7out",
	"ep8out", "ep9out", "ep10out", "ep11out",
	"ep12out", "ep13out", "ep14out", "ep15out"
};

static int ambarella_udc_get_frame(struct usb_gadget *_gadget);
static void ambarella_ep_nuke(struct ambarella_ep *ep, int status);

static inline struct ambarella_udc *to_ambarella_udc(struct usb_gadget *gadget)
{
	return container_of(gadget, struct ambarella_udc, gadget);
}

static inline struct ambarella_ep *to_ambarella_ep(struct usb_ep *ep)
{
	return container_of(ep, struct ambarella_ep, ep);
}

static inline struct ambarella_request *to_ambarella_req(struct usb_request *req)
{
	return container_of(req, struct ambarella_request, req);
}

static void ambarella_ep_fifo_flush(struct ambarella_ep *ep)
{
	struct ambarella_udc *udc = ep->udc;
	if(ep->dir == USB_DIR_IN)  /* Empty Tx FIFO */
		setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_FLUSH);
	else { 			  /* Empty RX FIFO */
		if (!(readl(udc->base + USB_DEV_STS_REG) & USB_DEV_RXFIFO_EMPTY_STS)) {
			int retry_count = 1000;

			setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_NAK);
			setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_FLUSH_RXFIFO);
			while(!(readl(udc->base + USB_DEV_STS_REG) & USB_DEV_RXFIFO_EMPTY_STS)) {
				if (retry_count-- < 0) {
					printk (KERN_ERR "%s: failed", __func__);
					break;
				}
				udelay(5);
			}
			clrbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_NAK);
		}
	}
}

/*
 * Name: ambarella_flush_fifo
 * Description:
 *	 Empty Tx/Rx FIFO of DMA
 */
static void ambarella_udc_fifo_flush(struct ambarella_udc *udc)
{
	struct ambarella_ep *ep;
	u32 ep_id;

	for (ep_id = 0; ep_id < EP_NUM_MAX; ep_id++) {
		ep = &udc->ep[ep_id];
		if(ep->ep.desc == NULL && !IS_EP0(ep))
			continue;
		ambarella_ep_fifo_flush(ep);
	}
}

static void ambarella_disable_all_intr(struct ambarella_udc *udc)
{
	/* device interrupt mask register */
	writel(0x0000007f, udc->base + USB_DEV_INTR_MSK_REG);
	writel(0xffffffff, udc->base + USB_DEV_EP_INTR_MSK_REG);
	writel(0x0000007f, udc->base + USB_DEV_INTR_REG);
	writel(0xffffffff, udc->base + USB_DEV_EP_INTR_REG);
}

static struct ambarella_data_desc *ambarella_get_last_desc(struct ambarella_ep *ep)
{
	struct ambarella_data_desc *desc = ep->data_desc;
	int retry_count = 1000;

	while(desc && (desc->status & USB_DMA_LAST) == 0) {
		if (retry_count-- < 0) {
			printk(KERN_ERR "Can't find the last descriptor\n");
			break;
		}

		if (desc->last_aux == 1)
			break;

		desc = desc->next_desc_virt;
	};

	return desc;
}

static u32 ambarella_check_bna_error (struct ambarella_ep *ep, u32 ep_status)
{
	u32 retval = 0;
	struct ambarella_udc *udc = ep->udc;

	/* Error: Buffer Not Available */
	if (ep_status & USB_EP_BUF_NOT_AVAIL) {
		printk(KERN_DEBUG "[USB]:BNA error in %s\n", ep->ep.name);
		writel(USB_EP_BUF_NOT_AVAIL, udc->base + ep->ep_reg.sts_reg);
		retval = 1;
	}

	return retval;
}

static u32 ambarella_check_he_error (struct ambarella_ep *ep, u32 ep_status)
{
	u32 retval = 0;
	struct ambarella_udc *udc = ep->udc;

	/* Error: Host Error */
	if (ep_status & USB_EP_HOST_ERR) {
		printk(KERN_ERR "[USB]:HE error in %s\n", ep->ep.name);
		writel(USB_EP_HOST_ERR, udc->base + ep->ep_reg.sts_reg);
		retval = 1;
	}

	return retval;
}

static u32 ambarella_check_dma_error (struct ambarella_ep *ep)
{
	u32	retval = 0, sts_tmp1, sts_tmp2;

	if(ep->last_data_desc){
		sts_tmp1 = ep->last_data_desc->status & USB_DMA_BUF_STS;
		sts_tmp2 = ep->last_data_desc->status & USB_DMA_RXTX_STS;
		if ((sts_tmp1 != USB_DMA_BUF_DMA_DONE) || (sts_tmp2 != USB_DMA_RXTX_SUCC)){
			printk(KERN_DEBUG "%s: DMA failed\n", ep->ep.name);
			retval = 1;
		}
	}

	return retval;
}

/*
 * Name: init_setup_descriptor
 * Description:
 *  Config the setup packet to specific endpoint register
 */
static void init_setup_descriptor(struct ambarella_udc *udc)
{
	struct ambarella_ep *ep = &udc->ep[CTRL_OUT];

	udc->setup_buf->status 	= USB_DMA_BUF_HOST_RDY;
	udc->setup_buf->reserved = 0xffffffff;
	udc->setup_buf->data0	= 0xffffffff;
	udc->setup_buf->data1	= 0xffffffff;
	flush_dcache_range((unsigned long)udc->setup_buf,
		(unsigned long)udc->setup_buf + sizeof(struct ambarella_setup_desc));

	writel(udc->setup_addr | udc->dma_fix,
			udc->base + ep->ep_reg.setup_buf_ptr_reg);
}

static int init_null_pkt_desc(struct ambarella_udc *udc)
{
	//udc->dummy_desc = dma_pool_alloc(udc->desc_dma_pool, GFP_KERNEL, &udc->dummy_desc_addr);
	udc->dummy_desc = memalign(DESC_ENT_ALIGN, sizeof(struct ambarella_setup_desc));
	udc->dummy_desc_addr = (dma_addr_t)udc->dummy_desc;
	if(udc->dummy_desc == NULL) {
		printk(KERN_ERR "No memory to DMA\n");
		return -ENOMEM;
	}

	udc->dummy_desc->data_ptr = udc->dummy_desc_addr | udc->dma_fix;
	udc->dummy_desc->reserved = 0xffffffff;
	udc->dummy_desc->next_desc_ptr = udc->dummy_desc_addr | udc->dma_fix;
	udc->dummy_desc->status = USB_DMA_BUF_HOST_RDY | USB_DMA_LAST;

	return 0;
}

static void init_ep0(struct ambarella_udc *udc)
{
	struct ambarella_ep_reg *ep_reg;

	ep_reg = &udc->ep[CTRL_IN].ep_reg;
	writel(USB_EP_TYPE_CTRL, udc->base + ep_reg->ctrl_reg);
	writel(USB_TXFIFO_DEPTH_CTRLIN, udc->base + ep_reg->buf_sz_reg);
	writel(USB_EP_CTRLIN_MAX_PKT_SZ, udc->base + ep_reg->max_pkt_sz_reg);
	udc->ep[CTRL_IN].ctrl_sts_phase = 0;

	ep_reg = &udc->ep[CTRL_OUT].ep_reg;
	writel(USB_EP_TYPE_CTRL, udc->base + ep_reg->ctrl_reg);
	writel(USB_EP_CTRL_MAX_PKT_SZ, udc->base + ep_reg->max_pkt_sz_reg);
	init_setup_descriptor(udc);
	udc->ep[CTRL_OUT].ctrl_sts_phase = 0;

	/* This should be set after gadget->bind */
	udc->ep[CTRL_OUT].ep.driver_data = udc->ep[CTRL_IN].ep.driver_data;

	/* FIXME: For A5S, this bit must be set,
	  * or USB_UDC_REG can't be read or write */
	setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_REMOTE_WAKEUP);

	/* setup ep0 CSR. Note: ep0-in and ep0-out share the same CSR reg */
	clrbits_32(udc->base + USB_UDC_REG(CTRL_IN), 0x7ff << 19);
	setbits_32(udc->base + USB_UDC_REG(CTRL_IN), USB_EP_CTRL_MAX_PKT_SZ << 19);
	/* the direction bit should not take effect for ep0 */
	setbits_32(udc->base + USB_UDC_REG(CTRL_IN), 0x1 << 4);
}

static void ambarella_regaddr_map(struct ambarella_udc *udc)
{
	u32 i;

	for(i = 0; i < EP_NUM_MAX; i++) {
		struct ambarella_ep *ep;
		ep = &udc->ep[i];
		if(ep->dir == USB_DIR_IN){
			ep->ep_reg.ctrl_reg = USB_EP_IN_CTRL_REG(ep->id);
			ep->ep_reg.sts_reg = USB_EP_IN_STS_REG(ep->id);
			ep->ep_reg.buf_sz_reg = USB_EP_IN_BUF_SZ_REG(ep->id);
			ep->ep_reg.max_pkt_sz_reg =
				USB_EP_IN_MAX_PKT_SZ_REG(ep->id);
			ep->ep_reg.dat_desc_ptr_reg =
				USB_EP_IN_DAT_DESC_PTR_REG(ep->id);
		} else {
			ep->ep_reg.ctrl_reg = USB_EP_OUT_CTRL_REG(ep->id - 16);
			ep->ep_reg.sts_reg = USB_EP_OUT_STS_REG(ep->id - 16);
			ep->ep_reg.buf_sz_reg =
				USB_EP_OUT_PKT_FRM_NUM_REG(ep->id - 16);
			ep->ep_reg.max_pkt_sz_reg =
				USB_EP_OUT_MAX_PKT_SZ_REG(ep->id - 16);
			ep->ep_reg.dat_desc_ptr_reg =
				USB_EP_OUT_DAT_DESC_PTR_REG(ep->id - 16);
		}

		ep->ep_reg.setup_buf_ptr_reg = (i == CTRL_OUT) ?
			USB_EP_OUT_SETUP_BUF_PTR_REG(ep->id - 16) : 0;
	}
}


/*
 * ambarella_udc_reinit
 */
static void ambarella_udc_reinit(struct ambarella_udc *udc)
{
	u32 i;

	/* device/ep0 records init */
	INIT_LIST_HEAD (&udc->gadget.ep_list);
	INIT_LIST_HEAD (&udc->gadget.ep0->ep_list);
	udc->auto_ack_0_pkt = 0;
	udc->remote_wakeup_en = 0;

	for (i = 0; i < EP_NUM_MAX; i++) {
		struct ambarella_ep *ep = &udc->ep[i];

		if (!IS_EP0(ep)){
			list_add_tail (&ep->ep.ep_list, &udc->gadget.ep_list);
		}

		ep->udc = udc;
		ep->halted = 0;
		ep->data_desc = NULL;
		ep->last_data_desc = NULL;
		INIT_LIST_HEAD (&ep->queue);
	}
}

static void ambarella_patch_iso_desc(struct ambarella_udc *udc,
	struct ambarella_request * req, struct ambarella_ep *ep, int frame_fix)
{
	struct ambarella_data_desc *data_desc;
	u32 current_frame, max_packet_num, i, j;

	current_frame = ambarella_udc_get_frame(&udc->gadget);
	data_desc = req->data_desc;

	/* according to USB2.0 spec, each microframe can send 3 packets at most */
	for (i = 0; i < req->active_desc_count; i += j) {
		if ((req->active_desc_count - i) >= ISO_MAX_PACKET)
			max_packet_num = ISO_MAX_PACKET;
		else
			max_packet_num = req->active_desc_count - i;

		for (j = 0; j < max_packet_num; j++) {
			data_desc->status |= ((current_frame + ep->frame_offset + frame_fix) << 16);
			data_desc->status |= (max_packet_num << 14);
			data_desc->status &= ~(0xf0000000);
			data_desc->status |= USB_DMA_BUF_HOST_RDY | USB_DMA_LAST;
			data_desc = data_desc->next_desc_virt;
			flush_dcache_range((unsigned long)data_desc,
				(unsigned long)data_desc + sizeof(struct ambarella_data_desc));
		}
	}
}


static void ambarella_set_tx_dma(struct ambarella_ep *ep,
	struct ambarella_request * req, int frame_fix)
{
	struct ambarella_udc *udc = ep->udc;
	struct ambarella_ep_reg *ep_reg = &ep->ep_reg;

	if (IS_ISO_IN_EP(ep))
		ambarella_patch_iso_desc(udc, req, ep, frame_fix);

	ep->data_desc = req->data_desc;
	flush_dcache_range((unsigned long)req->data_desc,
		(unsigned long)req->data_desc + sizeof(struct ambarella_data_desc));
	writel(req->data_desc_addr | udc->dma_fix,
			udc->base + ep_reg->dat_desc_ptr_reg);
	/* set Poll command to transfer data to Tx FIFO */
	setbits_32(udc->base + ep_reg->ctrl_reg, USB_EP_POLL_DEMAND);
	ep->dma_going = 1;
}


static void ambarella_set_ep_nak(struct ambarella_ep *ep)
{
	struct ambarella_udc *udc = ep->udc;
	setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_SET_NAK);
}

static void ambarella_clr_ep_nak(struct ambarella_ep *ep)
{
	struct ambarella_ep_reg *ep_reg = &ep->ep_reg;
	struct ambarella_udc *udc = ep->udc;

	setbits_32(udc->base + ep_reg->ctrl_reg, USB_EP_CLR_NAK);
	if (readl(udc->base + ep_reg->ctrl_reg) & USB_EP_NAK_STS) {
		/* can't clear NAK, let somebody clear it after Rx DMA is done. */
		ep->need_cnak = 1;
	}else{
		ep->need_cnak = 0;
	}
}

static void ambarella_enable_rx_dma(struct ambarella_ep *ep)
{
	struct ambarella_udc *udc = ep->udc;
	ep->dma_going = 1;
	setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_RCV_DMA_EN);
}

static void ambarella_set_rx_dma(struct ambarella_ep *ep,
	struct ambarella_request * req)
{
	struct ambarella_ep_reg *ep_reg = &ep->ep_reg;
	struct ambarella_udc *udc = ep->udc;
	u32 i;

	if(req){
		ep->data_desc = req->data_desc;
		flush_dcache_range((unsigned long)req->data_desc,
			(unsigned long)req->data_desc + sizeof(struct ambarella_data_desc));
		writel(req->data_desc_addr | udc->dma_fix,
				udc->base + ep_reg->dat_desc_ptr_reg);
	} else {
		/* receive zero-length-packet */
		udc->dummy_desc->status = USB_DMA_BUF_HOST_RDY | USB_DMA_LAST;
		flush_dcache_range((unsigned long)udc->dummy_desc_addr,
			(unsigned long)udc->dummy_desc_addr + sizeof(struct ambarella_data_desc));
		writel(udc->dummy_desc_addr | udc->dma_fix,
				udc->base + ep_reg->dat_desc_ptr_reg);
	}

	/* enable dma completion interrupt for next RX data */
	clrbits_32(udc->base + USB_DEV_EP_INTR_MSK_REG, 1 << ep->id);
	/* re-enable DMA read */
	ambarella_enable_rx_dma(ep);

	if (readl(udc->base + USB_DEV_STS_REG) & USB_DEV_RXFIFO_EMPTY_STS) {
		/* clear NAK for TX dma */
		for (i = 0; i < EP_NUM_MAX; i++) {
			struct ambarella_ep *_ep = &udc->ep[i];
			if (_ep->need_cnak == 1)
				ambarella_clr_ep_nak(_ep);
		}
	}

	/* clear NAK */
	ambarella_clr_ep_nak(ep);
}

static int ambarella_handle_ep_stall(struct ambarella_ep *ep, u32 ep_status)
{
	int ret = 0;
	struct ambarella_udc *udc = ep->udc;

	if (ep_status & USB_EP_RCV_CLR_STALL) {
		setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_CLR_NAK);
		clrbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_STALL);
		setbits_32(udc->base + ep->ep_reg.sts_reg, USB_EP_RCV_CLR_STALL);
		ret = 1;
	}

	if (ep_status & USB_EP_RCV_SET_STALL) {
		setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_STALL);
		if (ep->dir == USB_DIR_IN)
			setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_FLUSH);
		setbits_32(udc->base + ep->ep_reg.sts_reg, USB_EP_RCV_SET_STALL);
		ret = 1;
	}

	mdelay(1);
	return ret;
}

static void ambarella_handle_request_packet(struct ambarella_udc *udc)
{
	struct usb_ctrlrequest *crq;
	int ret;

	ambarella_ep_nuke(&udc->ep[CTRL_OUT], -EPROTO);  // Todo

	/* read out setup packet */
	udc->setup[0] = udc->setup_buf->data0;
	udc->setup[1] = udc->setup_buf->data1;
	/* reinitialize setup packet */
	init_setup_descriptor(udc);

	crq = (struct usb_ctrlrequest *) &udc->setup[0];

	debug("bRequestType = 0x%02x, bRequest = 0x%02x, "
		"wValue = 0x%04x, wIndex = 0x%04x, wLength = 0x%04x\n",
		crq->bRequestType, crq->bRequest, crq->wValue, crq->wIndex,
		crq->wLength);

	if (crq->bRequestType == 0 || crq->bRequestType == 0xff)
	{
		setbits_32(udc->base + udc->ep[CTRL_IN].ep_reg.ctrl_reg, USB_EP_STALL | USB_EP_FLUSH);
		ambarella_enable_rx_dma(&udc->ep[CTRL_OUT]);
		ambarella_clr_ep_nak(&udc->ep[CTRL_OUT]);
		return ;
	}

	if((crq->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD){
		switch(crq->bRequest)
		{
		case USB_REQ_GET_STATUS:
		case USB_REQ_SET_ADDRESS:
		case USB_REQ_CLEAR_FEATURE:
		case USB_REQ_SET_FEATURE:
			pr_err("%s: This bRequest is not implemented!\n"
				"\tbRequestType = 0x%02x, bRequest = 0x%02x,\n"
				"\twValue = 0x%04x, wIndex = 0x%04x, wLength = 0x%04x\n",
				__func__,
				crq->bRequestType, crq->bRequest, crq->wValue, crq->wIndex,
				crq->wLength);
		}
	}

	if (crq->bRequestType & USB_DIR_IN)
		udc->gadget.ep0 = &udc->ep[CTRL_IN].ep;

	else
		udc->gadget.ep0 = &udc->ep[CTRL_OUT].ep;

	spin_unlock(&udc->lock);
	if(udc->driver && udc->driver->setup)
		ret = udc->driver->setup(&udc->gadget, crq);
	spin_lock(&udc->lock);
	if (ret < 0) {
		pr_err("%s: SETUP request failed (%d)\n", __func__, ret);
		setbits_32(udc->base + udc->ep[CTRL_IN].ep_reg.ctrl_reg, USB_EP_STALL | USB_EP_FLUSH);
		/* Re-enable Rx DMA to receive next setup packet */
		ambarella_enable_rx_dma(&udc->ep[CTRL_OUT]);
		ambarella_clr_ep_nak(&udc->ep[CTRL_OUT]);
	}

	return;

}

static void ambarella_udc_done(struct ambarella_ep *ep,
		struct ambarella_request *req, int status)
{
	unsigned halted_tmp, need_queue = 0;
	struct ambarella_request *next_req;
	struct ambarella_udc *udc = ep->udc;

	halted_tmp = ep->halted;

	list_del_init(&req->queue);

	if(!list_empty(&ep->queue) && !ep->halted && !ep->cancel_transfer){
		need_queue = 1;
	} else if (!IS_EP0(ep) && (ep->dir == USB_DIR_IN) && !ep->cancel_transfer) {
		/* ep->ep.desc = NULL when ep disabled */
		if (!ep->ep.desc || IS_ISO_IN_EP(ep))
			ambarella_set_ep_nak(ep);
		setbits_32(udc->base + USB_DEV_EP_INTR_MSK_REG, 1 << ep->id);
	}

	if (likely (req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	if (likely(!req->use_aux_buf)) {
		invalidate_dcache_range((unsigned long)req->req.buf, (unsigned long)req->req.length);
	} else {
		invalidate_dcache_range((unsigned long)req->buf_aux, (unsigned long)req->buf_aux + req->req.length);
	}

	if (req->use_aux_buf && ep->dir != USB_DIR_IN)
		memcpy(req->req.buf, req->buf_aux, req->req.actual);

	ep->data_desc = NULL;
	ep->last_data_desc = NULL;

	ep->halted = 1;
	spin_unlock(&ep->udc->lock);
	if(req->req.complete)
		req->req.complete(&ep->ep, &req->req);
	spin_lock(&ep->udc->lock);
	ep->halted = halted_tmp;

	if(need_queue){
		next_req = list_first_entry (&ep->queue,
			 struct ambarella_request, queue);

		switch(ep->dir) {
		case USB_DIR_IN:
			/* no need to wait for IN-token for ISO transfer */
			if (IS_ISO_IN_EP(ep)) {
				writel(USB_EP_IN_PKT, udc->base + ep->ep_reg.sts_reg);
				ambarella_set_tx_dma(ep, next_req, 1);
			}
			ambarella_clr_ep_nak(ep);
			break;
		case USB_DIR_OUT:
			ambarella_set_rx_dma(ep, next_req);
			break;
		default:
			return;
		}
	}
}

static void ambarella_handle_data_in(struct ambarella_ep *ep)
{
	struct ambarella_request	*req = NULL;
	struct ambarella_udc *udc = ep->udc;

	/* get request */
	if (list_empty(&ep->queue)) {
		printk(KERN_DEBUG "%s: req NULL\n", __func__);
		return ;
	}

	req = list_first_entry(&ep->queue, struct ambarella_request,queue);

	/* If error happened, issue the request again */
	if (ambarella_check_dma_error(ep))
		req->req.status = -EPROTO;
	else {
		/* No error happened, so all the data has been sent to host */
		req->req.actual = req->req.length;
	}

	ambarella_udc_done(ep, req, 0);

	if(ep->id == CTRL_IN){
		/* For STATUS-OUT stage */
		udc->ep[CTRL_OUT].ctrl_sts_phase = 1;
		ambarella_set_rx_dma(&udc->ep[CTRL_OUT], NULL);
	}
}

static int ambarella_handle_data_out(struct ambarella_ep *ep)
{
	struct ambarella_request	*req = NULL;
	struct ambarella_udc *udc = ep->udc;
	u32 recv_size = 0, req_size;

	/* get request */
	if (list_empty(&ep->queue)) {
		printk(KERN_DEBUG "%s: req NULL\n", __func__);
		return -EINVAL;
	}

	req = list_first_entry(&ep->queue, struct ambarella_request,queue);

	/* If error happened, issue the request again */
	if (ambarella_check_dma_error(ep))
		req->req.status = -EPROTO;

	recv_size = ep->last_data_desc->status & USB_DMA_RXTX_BYTES;
	if (!recv_size && req->req.length == UDC_DMA_MAXPACKET) {
		/* on 64k packets the RXBYTES field is zero */
		recv_size = UDC_DMA_MAXPACKET;
	}

	req_size = req->req.length - req->req.actual;
	if (recv_size > req_size) {
		if ((req_size % ep->ep.maxpacket) != 0)
			req->req.status = -EOVERFLOW;
		recv_size = req_size;
	}

	req->req.actual += recv_size;

	ambarella_udc_done(ep, req, 0);

	if(ep->id == CTRL_OUT) {
		/* For STATUS-IN stage */
		ambarella_clr_ep_nak(&udc->ep[CTRL_IN]);
		/* Re-enable Rx DMA to receive next setup packet */
		ambarella_enable_rx_dma(ep);
		ep->dma_going = 0;
	}

	return 0;
}

static void ambarella_ep_nuke(struct ambarella_ep *ep, int status)
{
	while (!list_empty (&ep->queue)) {
		struct ambarella_request *req;
		req = list_first_entry(&ep->queue,
			struct ambarella_request, queue);
		ambarella_udc_done(ep, req, status);
	}
}

static int ambarella_udc_ep_enable(struct usb_ep *_ep,
				 const struct usb_endpoint_descriptor *desc)
{
	struct ambarella_udc	*udc;
	struct ambarella_ep	*ep = to_ambarella_ep(_ep);
	u32			max_packet, tmp, type, idx = 0;
	unsigned long		flags;

	/* Sanity check  */
	if (!_ep || !desc || IS_EP0(ep)
		|| desc->bDescriptorType != USB_DT_ENDPOINT) {
		printk("%s ep %d is inval \n",__func__,ep->id);
		return -EINVAL;
	}
	udc = ep->udc;
	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	max_packet = usb_endpoint_maxp(desc) & 0x7ff;

	spin_lock_irqsave(&udc->lock, flags);
	ep->ep.maxpacket = max_packet;
	ep->ep.desc = desc;
	ep->halted = 0;
	ep->data_desc = NULL;
	ep->last_data_desc = NULL;
	ep->ctrl_sts_phase = 0;
	ep->dma_going = 0;
	ep->cancel_transfer = 0;
	ep->frame_offset =  (1 << (desc->bInterval - 1));

	if(ep->dir == USB_DIR_IN){
		idx = ep->id;
	} else {
		idx = ep->id - CTRL_OUT_UDC_IDX;
	}

	/* setup CSR */
	clrbits_32(udc->base + USB_UDC_REG(idx), 0x3fffffff);
	tmp = (desc->bEndpointAddress & 0xf) << 0;
	tmp |= (desc->bEndpointAddress >> 7) << 4;
	tmp |= (desc->bmAttributes & 0x3) << 5;
	tmp |= udc->cur_config << 7;
	tmp |= udc->cur_intf << 11;
	tmp |= udc->cur_alt << 15;
	tmp |= max_packet << 19;
	setbits_32(udc->base + USB_UDC_REG(idx), tmp);

	type = (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) << 4;
	writel(type | USB_EP_SET_NAK, udc->base + ep->ep_reg.ctrl_reg);

	if(ep->dir == USB_DIR_IN) {
		/* NOTE: total IN fifo size must be less than 528 * 4B */
		tmp = max_packet / 4;
#if 0
		if (IS_ISO_IN_EP(ep))
			tmp *= max_packet > 1024 ? 1 : max_packet > 512 ? 2 : 3;
		else
#endif
			tmp *= 2;
		writel(tmp, udc->base + ep->ep_reg.buf_sz_reg);
	}
	writel(max_packet, udc->base + ep->ep_reg.max_pkt_sz_reg);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int ambarella_udc_ep_disable(struct usb_ep *_ep)
{
	struct ambarella_ep *ep = to_ambarella_ep(_ep);
	struct ambarella_udc *udc = ep->udc;
	unsigned long flags;

	if (!_ep || !ep->ep.desc) {
		printk("%s not enabled\n",
			_ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->udc->lock, flags);

	ep->ep.desc = NULL;
	ep->halted = 1;
	ambarella_ep_nuke(ep, -ESHUTDOWN);

	ambarella_set_ep_nak(ep);

	if(ep->dir == USB_DIR_IN){
		/* clear DMA poll demand bit */
		clrbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_POLL_DEMAND);
		/* clear status register */
		setbits_32(udc->base + ep->ep_reg.sts_reg, USB_EP_IN_PKT);
		/* flush the fifo */
		setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_FLUSH);
	}

	/* disable irqs */
	setbits_32(udc->base + USB_DEV_EP_INTR_MSK_REG, 1 << ep->id);

	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return 0;
}

static struct usb_request *
ambarella_udc_alloc_request(struct usb_ep *_ep, gfp_t mem_flags)
{
	struct ambarella_request *req;

	if (!_ep)
		return NULL;

	req = kzalloc (sizeof(struct ambarella_request), mem_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD (&req->queue);
	req->desc_count = 0;

	req->buf_aux = NULL;
	req->dma_aux = DMA_ADDR_INVALID;
	req->use_aux_buf = 0;

	return &req->req;
}

static void ambarella_free_descriptor(struct ambarella_ep *ep,
	struct ambarella_request *req)
{
	struct ambarella_data_desc *data_desc, *next_data_desc;
	//struct dma_pool *desc_dma_pool = ep->udc->desc_dma_pool;
	int i;

	next_data_desc = req->data_desc;
	for(i = 0; i < req->desc_count; i++){
		data_desc = next_data_desc;
		data_desc->status = USB_DMA_BUF_HOST_BUSY | USB_DMA_LAST;
		data_desc->data_ptr = 0xffffffff;
		data_desc->next_desc_ptr = 0;
		data_desc->last_aux = 1;
		next_data_desc = data_desc->next_desc_virt;
		//dma_pool_free(desc_dma_pool, data_desc, data_desc->cur_desc_addr);
		invalidate_dcache_range((unsigned long)data_desc, (unsigned long)data_desc + sizeof(struct ambarella_data_desc));
		free(data_desc);
	}

	req->desc_count = 0;
	req->data_desc = NULL;
	req->data_desc_addr = 0;
}


static void
ambarella_udc_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct ambarella_ep	*ep = to_ambarella_ep(_ep);
	struct ambarella_request	*req = to_ambarella_req(_req);

	if (!ep || !_req)
		return;

	if(req->desc_count > 0)
		ambarella_free_descriptor(ep, req);

	if (req->buf_aux) {
		kfree(req->buf_aux);
		req->buf_aux = NULL;
	}

	WARN_ON (!list_empty (&req->queue));
	kfree(req);
}

static int ambarella_reuse_descriptor(struct ambarella_ep *ep,
	struct ambarella_request *req, u32 desc_count, dma_addr_t start_address)
{
	struct ambarella_udc *udc = ep->udc;
	struct ambarella_data_desc *data_desc, *next_data_desc;
	u32 data_transmit, rest_bytes, i;
	dma_addr_t buf_dma_address;

	next_data_desc = req->data_desc;
	for(i = 0; i < desc_count; i++){
		rest_bytes = req->req.length - i * ep->ep.maxpacket;
		if(ep->dir == USB_DIR_IN)
			data_transmit = rest_bytes < ep->ep.maxpacket ?
				rest_bytes : ep->ep.maxpacket;
		else
			data_transmit = 0;

		data_desc = next_data_desc;
		data_desc->status = USB_DMA_BUF_HOST_RDY | data_transmit;
		data_desc->reserved = 0xffffffff;
		buf_dma_address = start_address + i * ep->ep.maxpacket;
		data_desc->data_ptr = buf_dma_address | udc->dma_fix;
		data_desc->last_aux = 0;

		next_data_desc = data_desc->next_desc_virt;
		flush_dcache_range((unsigned long)data_desc, (unsigned long)data_desc + sizeof(struct ambarella_data_desc));
	}

	/* Patch last one. */
	data_desc->status |= USB_DMA_LAST;
	data_desc->last_aux = 1;
	flush_dcache_range((unsigned long)data_desc, (unsigned long)data_desc + sizeof(struct ambarella_data_desc));

	return 0;
}

static int ambarella_prepare_descriptor(struct ambarella_ep *ep,
	struct ambarella_request *req, gfp_t gfp)
{
	struct ambarella_udc *udc = ep->udc;
	struct ambarella_data_desc *data_desc = NULL;
	struct ambarella_data_desc *prev_data_desc = NULL;
	dma_addr_t desc_phys, start_address, buf_dma_address;
	u32 desc_count, data_transmit, rest_bytes, i;

	if (likely(!req->use_aux_buf))
		start_address = (dma_addr_t)req->req.buf;//start_address = req->req.dma;
	else
		start_address = (dma_addr_t)req->buf_aux;//start_address = req->dma_aux;

	desc_count = (req->req.length + ep->ep.maxpacket - 1) / ep->ep.maxpacket;
	if(req->req.zero && (req->req.length % ep->ep.maxpacket == 0))
		desc_count++;
	if(desc_count == 0)
		desc_count = 1;

	req->active_desc_count = desc_count;

	if (desc_count <= req->desc_count) {
		ambarella_reuse_descriptor(ep, req, desc_count, start_address);
		return 0;
	}

	if(req->desc_count > 0)
		ambarella_free_descriptor(ep, req);

	req->desc_count = desc_count;

	for(i = 0; i < desc_count; i++){
		rest_bytes = req->req.length - i * ep->ep.maxpacket;
		if(ep->dir == USB_DIR_IN)
			data_transmit = rest_bytes < ep->ep.maxpacket ?
				rest_bytes : ep->ep.maxpacket;
		else
			data_transmit = 0;

		//data_desc = dma_pool_alloc(udc->desc_dma_pool, gfp, &desc_phys);
		data_desc = memalign(DESC_ENT_ALIGN, sizeof(struct ambarella_setup_desc));
		desc_phys = (dma_addr_t)data_desc;
		
		if (!data_desc) {
			req->desc_count = i;
			if(req->desc_count > 0)
				ambarella_free_descriptor(ep, req);
			return -ENOMEM;
		}

		data_desc->status = USB_DMA_BUF_HOST_RDY | data_transmit;
		data_desc->reserved = 0xffffffff;
		buf_dma_address = start_address + i * ep->ep.maxpacket;
		data_desc->data_ptr = buf_dma_address | udc->dma_fix;
		data_desc->next_desc_ptr = 0;
		data_desc->rsvd1 = 0xffffffff;
		data_desc->last_aux = 0;
		data_desc->cur_desc_addr = desc_phys;

		if(prev_data_desc){
			prev_data_desc->next_desc_ptr = desc_phys | udc->dma_fix;
			prev_data_desc->next_desc_virt = data_desc;
		}

		prev_data_desc = data_desc;

		if(i == 0){
			req->data_desc = data_desc;
			req->data_desc_addr = desc_phys;
		}
		flush_dcache_range((unsigned long)data_desc, (unsigned long)data_desc + sizeof(struct ambarella_data_desc));
	}

	/* Patch last one */
	data_desc->status |= USB_DMA_LAST;
	data_desc->next_desc_ptr = 0;
	data_desc->next_desc_virt = NULL;
	data_desc->last_aux = 1;
	flush_dcache_range((unsigned long)data_desc, (unsigned long)data_desc + sizeof(struct ambarella_data_desc));

	return 0;
}


static int ambarella_udc_queue(struct usb_ep *_ep, struct usb_request *_req,
		gfp_t gfp_flags)
{
	struct ambarella_request	*req = NULL;
	struct ambarella_ep	*ep = NULL;
	struct ambarella_udc	*udc;
	unsigned long flags;
	unsigned int length;
	int i;

	if (unlikely (!_ep)) {
		pr_err("%s: _ep is NULL\n", __func__);
		return -EINVAL;
	}

	ep = to_ambarella_ep(_ep);
	udc = ep->udc;

	for(i = 0; i < EP_NUM_MAX; i++) {
		struct ambarella_ep *endp;
		endp = &udc->ep[i];

		if (endp != NULL && endp->dma_going) //Check for any ongoing DMA
			break;

		// If no other onging DMA and the current req is for ISO, enable the DMA
		if((i == EP_NUM_MAX - 1) && IS_ISO_IN_EP(ep)) {
			setbits_32(udc->base + USB_DEV_CTRL_REG,
					USB_DEV_RCV_DMA_EN | USB_DEV_TRN_DMA_EN);
		}
	}

	if (unlikely (!ep->ep.desc && !IS_EP0(ep))) {
		pr_err("%s: %s, invalid args\n", __func__, _ep->name);
		return -EINVAL;
	}

#if 0
	if( unlikely( !udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)){
		printk("%s: %01d %01d\n", _ep->name,
			!udc->driver, udc->gadget.speed==USB_SPEED_UNKNOWN);
		return -ESHUTDOWN;
	}
#endif

	if (unlikely(!_req )) {
		pr_err("%s: %s, _req is NULL\n", __func__, _ep->name);
		return -EINVAL;
	}

	req = to_ambarella_req(_req);
	if (unlikely(!req->req.complete || !req->req.buf
				|| !list_empty(&req->queue))) {
		pr_err("%s: %s, %01d %01d %01d\n", __func__, _ep->name,
			!_req->complete, !_req->buf, !list_empty(&req->queue));

		return -EINVAL;
	}

	if(IS_EP0(ep) && (udc->auto_ack_0_pkt == 1)){
		/* It's status stage in setup packet. And A2/A3 will
		  * automatically send the zero-length packet to Host */
		udc->auto_ack_0_pkt = 0;
		req->req.actual = 0;
		if(req->req.complete)
			req->req.complete(&ep->ep, &req->req);
		return 0;
	}

	/* check whether USB is suspended */
	if(readl(udc->base + USB_DEV_STS_REG) & USB_DEV_SUSP_STS){
		pr_err("%s: UDC is suspended!\n", __func__);
		return -ESHUTDOWN;
	}

	length = roundup(req->req.length, ARCH_DMA_MINALIGN);
	if (unlikely((unsigned long)req->req.buf & 0x7)) {
		req->use_aux_buf = 1;

		if (req->buf_aux == NULL) {
			//req->buf_aux = kmalloc(UDC_DMA_MAXPACKET, GFP_ATOMIC);
			req->buf_aux = memalign(DATA_BUF_ALIGN, length);
			if (req->buf_aux == NULL)
				return -ENOMEM;
			req->dma_aux = (dma_addr_t)req->buf_aux;
		}

		if (ep->dir == USB_DIR_IN)
			memcpy(req->buf_aux, req->req.buf, req->req.length);
		req->dma_aux = (dma_addr_t)req->buf_aux;
		flush_dcache_range((unsigned long)req->buf_aux, (unsigned long)req->buf_aux + length);
	} else {
		req->use_aux_buf = 0;
		flush_dcache_range((unsigned long)req->req.buf, (unsigned long)req->req.buf + length);
	}

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	ambarella_prepare_descriptor(ep, req, gfp_flags);

	/* disable IRQ handler's bottom-half  */
	spin_lock_irqsave(&udc->lock, flags);

	/* kickstart this i/o queue? */
	//if (list_empty(&ep->queue) && !ep->halted) {
	if (list_empty(&ep->queue)) {
		/* when the data length in ctrl-out transfer is zero, we just
		  * need to implement the STATUS-IN stage. But we don't
		  * implement the case that the data length in ctrl-in transfer
		  * is zero. */
		if(req->req.length == 0) {
			if(ep->id == CTRL_OUT) {
				ambarella_udc_done(ep, req, 0);
				/* told UDC the configuration is done, and to ack HOST */
				//setbitsl(USB_DEV_CTRL_REG, USB_DEV_CSR_DONE);
				//udelay(150);
				/* For STATUS-IN stage */
				ambarella_clr_ep_nak(&udc->ep[CTRL_IN]);
				/* Re-enable Rx DMA to receive next setup packet */
				ambarella_set_rx_dma(ep, NULL);
				ep->dma_going = 0;
				goto finish;
			} else if (ep->id == CTRL_IN) {
				//printk("the data length of ctrl-in is zero\n");
				//BUG();
			}
		}

		if (ep->dir == USB_DIR_IN) {
			/* no need to wait for IN-token for ISO transfer */
#if 0
			if (IS_ISO_IN_EP(ep)) {
				writel(udc->base + ep->ep_reg.sts_reg, USB_EP_IN_PKT);
				ambarella_set_tx_dma(ep, req);
			}
#endif
			/* enable dma completion interrupt for current TX data */
			clrbits_32(udc->base + USB_DEV_EP_INTR_MSK_REG, 1 << ep->id);
			ambarella_clr_ep_nak(ep);
		} else {
			ambarella_set_rx_dma(ep, req);
		}
	}

	list_add_tail(&req->queue, &ep->queue);

finish:
	/* enable IRQ handler's bottom-half  */
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int ambarella_udc_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct ambarella_ep *ep = to_ambarella_ep(_ep);
	struct ambarella_udc *udc = ep->udc;
	struct ambarella_request *req;
	unsigned long flags;
	unsigned halted;

	if (!ep->udc->driver)
		return -ESHUTDOWN;

	if (!_ep || !_req)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);

	/* make sure the request is actually queued on this endpoint */
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EINVAL;
	}

	halted = ep->halted;
	ep->halted = 1;

	/* request in processing */
	if((ep->data_desc == req->data_desc) && (ep->dma_going == 1)) {
		if (ep->dir == USB_DIR_IN)
			ep->cancel_transfer = 1;
		else {
			u32 tmp, desc_status;
			/* stop potential receive DMA */
			tmp = readl(udc->base + USB_DEV_CTRL_REG);
			clrbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_RCV_DMA_EN);

			/* cancel transfer later in ISR if descriptor was touched. */
			desc_status = req->data_desc->status;
			if (desc_status != USB_DMA_BUF_HOST_RDY)
				ep->cancel_transfer = 1;

			writel(tmp, udc->base + USB_DEV_CTRL_REG);
		}
	}

	ambarella_udc_done(ep, req, -ECONNRESET);

	ep->halted = halted;
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return 0;
}

/*
 * ambarella_udc_set_halt
 */
static int ambarella_udc_set_halt(struct usb_ep *_ep, int value)
{
	struct ambarella_ep *ep = to_ambarella_ep(_ep);
	struct ambarella_udc *udc = ep->udc;
	unsigned long flags;

	if (unlikely (!_ep || (!ep->ep.desc && !IS_EP0(ep)))) {
		pr_err("%s: %s, -EINVAL 1\n", __func__,_ep->name);
		return -EINVAL;
	}
	if (!ep->udc->driver || ep->udc->gadget.speed == USB_SPEED_UNKNOWN){
		pr_err("%s: %s, -ESHUTDOWN\n", __func__, _ep->name);
		return -ESHUTDOWN;
	}
	/* isochronous transfer never halts because there is no handshake
	 * to report a halt condition */
	if (ep->ep.desc /* not ep0 */ && IS_ISO_IN_EP(ep)) {
		pr_err("%s: %s, -EINVAL 2\n", __func__, _ep->name);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->udc->lock, flags);

	/* set/clear, then synch memory views with the device */
	if (value) { /* stall endpoint */
		if (ep->dir == USB_DIR_IN) {
			setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_STALL | USB_EP_FLUSH);
		} else {
			int retry_count = 10000;
			/* Wait Rx-FIFO to be empty */
			while(!(readl(udc->base + USB_DEV_STS_REG) & USB_DEV_RXFIFO_EMPTY_STS)){
				if (retry_count-- < 0) {
					printk(KERN_ERR"[USB] stall_endpoint:failed");
					break;
				}
			}
			setbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_STALL);
		}
	} else { /* clear stall endpoint */
		clrbits_32(udc->base + ep->ep_reg.ctrl_reg, USB_EP_STALL);
	}

	ep->halted = !!value;

	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return 0;
}

static const struct usb_ep_ops ambarella_ep_ops = {
	.enable		= ambarella_udc_ep_enable,
	.disable	= ambarella_udc_ep_disable,

	.alloc_request	= ambarella_udc_alloc_request,
	.free_request	= ambarella_udc_free_request,

	.queue		= ambarella_udc_queue,
	.dequeue	= ambarella_udc_dequeue,

	.set_halt	= ambarella_udc_set_halt,
	/* fifo ops not implemented */
};

/*------------------------- usb_gadget_ops ----------------------------------*/

static int ambarella_udc_get_frame(struct usb_gadget *_gadget)
{
	struct ambarella_udc *udc = to_ambarella_udc(_gadget);
	return (readl(udc->base + USB_DEV_STS_REG) >> 18) & 0x7ff;
}

static int ambarella_udc_wakeup(struct usb_gadget *_gadget)
{
	struct ambarella_udc *udc = to_ambarella_udc(_gadget);
	u32 tmp;

	tmp = readl(udc->base + USB_DEV_CFG_REG);
	/* Remote wakeup feature not enabled by host */
	if ((!udc->remote_wakeup_en) || (!(tmp & USB_DEV_REMOTE_WAKEUP_EN)))
		return -ENOTSUPP;

	tmp = readl(udc->base + USB_DEV_STS_REG);
	/* not suspended? */
	if (!(tmp & USB_DEV_SUSP_STS))
		return 0;

	/* trigger force resume */
	setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_REMOTE_WAKEUP);

	return 0;
}

static void ambarella_udc_enable(struct ambarella_udc *udc)
{
	if (udc->udc_is_enabled)
		return;

	udc->udc_is_enabled = 1;

	/* Disable Tx and Rx DMA */
	clrbits_32(udc->base + USB_DEV_CTRL_REG,
		USB_DEV_RCV_DMA_EN | USB_DEV_TRN_DMA_EN);

	/* flush all of dma fifo */
	ambarella_udc_fifo_flush(udc);

	/* initialize ep0 register */
	init_ep0(udc);

	/* enable ep0 interrupt. */
	clrbits_32(udc->base + USB_DEV_EP_INTR_MSK_REG,
		USB_DEV_MSK_EP0_IN | USB_DEV_MSK_EP0_OUT);

	/* enable Tx and Rx DMA */
	setbits_32(udc->base + USB_DEV_CTRL_REG,
		USB_DEV_RCV_DMA_EN | USB_DEV_TRN_DMA_EN);

	/* enable device interrupt:
	 * Set_Configure, Set_Interface, Speed Enumerate Complete, Reset */
	clrbits_32(udc->base + USB_DEV_INTR_MSK_REG,
			USB_DEV_MSK_SET_CFG |
			USB_DEV_MSK_SET_INTF |
			USB_DEV_MSK_SPD_ENUM_CMPL |
			USB_DEV_MSK_RESET);
}

static void ambarella_udc_disable(struct ambarella_udc *udc)
{
	/* Disable all interrupts and Clear the interrupt registers */
	ambarella_disable_all_intr(udc);

	/* Disable Tx and Rx DMA */
	clrbits_32(udc->base + USB_DEV_CTRL_REG,
			USB_DEV_RCV_DMA_EN | USB_DEV_TRN_DMA_EN);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->udc_is_enabled = 0;
}

static void ambarella_stop_activity(struct ambarella_udc *udc)
{
	//struct usb_gadget_driver *driver = udc->driver;
	struct ambarella_ep *ep;
	u32  i;

	/* Disable Tx and Rx DMA */
	clrbits_32(udc->base + USB_DEV_CTRL_REG,
			USB_DEV_RCV_DMA_EN | USB_DEV_TRN_DMA_EN);

	//if (udc->gadget.speed == USB_SPEED_UNKNOWN)
	//	udc->driver = NULL;
	//udc->gadget.speed = USB_SPEED_UNKNOWN;

	for (i = 0; i < EP_NUM_MAX; i++) {
		ep = &udc->ep[i];

		if(ep->ep.desc == NULL && !IS_EP0(ep))
			continue;

		ambarella_set_ep_nak(ep);

		ep->halted = 1;
		ambarella_ep_nuke(ep, -ESHUTDOWN);
	}

	//tasklet_schedule(&udc->disconnect_tasklet);

	ambarella_udc_reinit(udc);
}

static int ambarella_udc_set_pullup(struct ambarella_udc *udc, int is_on)
{
	if (is_on) {
		ambarella_udc_enable(udc);
		/* reconnect to host */
		clrbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_SOFT_DISCON);
	} else {
		if (udc->gadget.speed != USB_SPEED_UNKNOWN)
			ambarella_stop_activity(udc);
		/* disconnect to host */
		setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_SOFT_DISCON);
		ambarella_udc_disable(udc);
	}

	return 0;
}

static int ambarella_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct ambarella_udc *udc = to_ambarella_udc(gadget);

	ambarella_udc_set_pullup(udc, is_on);

	return 0;
}

static int ambarella_udc_start(struct usb_gadget *gadget,
			struct usb_gadget_driver *driver)
{
	struct ambarella_udc *udc = to_ambarella_udc(gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	/* Hook the driver */
	//driver->driver.bus = NULL;
	udc->driver = driver;
	/* Enable udc */
	ambarella_udc_enable(udc);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

#if 0
static int ambarella_udc_stop(struct usb_gadget *gadget)
{
	struct ambarella_udc *udc = to_ambarella_udc(gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	ambarella_stop_activity(udc);
	ambarella_udc_disable(udc);
	udc->driver = NULL;

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}
#endif

static const struct usb_gadget_ops ambarella_ops = {
	.get_frame		= ambarella_udc_get_frame,
	.wakeup			= ambarella_udc_wakeup,
	.pullup			= ambarella_udc_pullup,
	//.vbus_session		= ambarella_udc_vbus_session,
	/*.set_selfpowered: Always selfpowered */
	.udc_start		= ambarella_udc_start,
	//.udc_stop		= ambarella_udc_stop,
};


static void ambarella_init_gadget(struct ambarella_udc *udc)
{
	struct ambarella_ep *ep;
	u32 i;

	udc->gadget.ops = &ambarella_ops;
	udc->gadget.name = gadget_name;
	udc->gadget.max_speed = USB_SPEED_HIGH;
	udc->gadget.ep0 = &udc->ep[CTRL_IN].ep;

	/* set basic ep parameters */
	for (i = 0; i < EP_NUM_MAX; i++) {
		ep = &udc->ep[i];
		ep->ep.name = amb_ep_string[i];
		ep->id = i;
		ep->ep.ops = &ambarella_ep_ops;
		ep->ep.maxpacket = (unsigned short) ~0;
		ep->ep.maxpacket_limit = (unsigned short) ~0;

		if (i < EP_IN_NUM) {
			ep->ep.caps.dir_in = true;
			ep->dir = USB_DIR_IN;
		} else {
			ep->ep.caps.dir_out = true;
			ep->dir = USB_DIR_OUT;
		}

		if (i == CTRL_IN || i == CTRL_OUT){
			ep->ep.caps.type_control = true;
		}else{
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_int = true;
		}
	}

	udc->ep[CTRL_IN].ep.maxpacket = USB_EP_CTRL_MAX_PKT_SZ;
	udc->ep[CTRL_OUT].ep.maxpacket = USB_EP_CTRL_MAX_PKT_SZ;

	return;
}

static void ambarella_init_usb(struct ambarella_udc *udc)
{
	u32 value;

	/* disconnect to host */
	setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_SOFT_DISCON);
	/* disable all interrupts */
	ambarella_disable_all_intr(udc);
	/* disable Tx and Rx DMA */
	clrbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_RCV_DMA_EN | USB_DEV_TRN_DMA_EN);
	/* flush dma fifo, may used in AMboot */
	ambarella_udc_fifo_flush(udc);

	/* device config register */
	value = USB_DEV_SPD_HI |
		USB_DEV_SELF_POWER |
		USB_DEV_PHY_8BIT |
		USB_DEV_UTMI_DIR_BI |
		USB_DEV_HALT_ACK |
		USB_DEV_SET_DESC_STALL |
		USB_DEV_DDR |
		USB_DEV_CSR_PRG_EN |
		USB_DEV_REMOTE_WAKEUP_EN;

	setbits_32(udc->base + USB_DEV_CFG_REG, value);

	/* device control register */
	value = USB_DEV_DESC_UPD_PYL |
		USB_DEV_LITTLE_ENDN |
		USB_DEV_DMA_MD;

	setbits_32(udc->base + USB_DEV_CTRL_REG, value);

	// udelay(200); // FIXME: how long to wait is the best?
}

static void usb_phy_enable(struct ambarella_udc *udc)
{
#if 0
	regmap_update_bits(udc->rct_regmap, ANA_PWR_OFFSET, 0x3 << 1, 0x3);
	regmap_update_bits(udc->rct_regmap, ANA_PWR_OFFSET, 0x3 << 12, 0x3);
	mdelay(1);

	regmap_update_bits(udc->rct_regmap, USBC_CTRL_OFFSET, 0x1 << 1, 0x1);
	mdelay(1);
	regmap_update_bits(udc->rct_regmap, USBC_CTRL_OFFSET, 0x1 << 1, 0x0);
	mdelay(1);
#endif
}

static void ambarella_udc_reset(struct ambarella_udc *udc)
{
	if (udc->scr_reg) {
		/*
		 * On CV3 and cv72, this reset operation leads to usb device
		 * halt when booting from USB.
		 */
		//regmap_update_bits(udc->scr_reg, UDC_SOFT_RESET_OFFSET, UDC_SOFT_RESET_MASK, UDC_SOFT_RESET_MASK);
		//mdelay(1);
		//regmap_update_bits(udc->scr_reg, UDC_SOFT_RESET_OFFSET, UDC_SOFT_RESET_MASK, 0x0);
		//mdelay(1);
	}
};
static int ambarella_udc_probe(struct udevice *dev)
{
	struct ambarella_udc *udc = dev_get_priv(dev);
	int retval;

	usb_phy_enable(udc);
	mdelay(1);			/* 'delay' is more stable ? */
	setbits_32(udc->base + USB_DEV_CTRL_REG, 1 << 10);
	setbits_32(udc->base + USB_DEV_CFG_REG, 1 << 2);
	ambarella_udc_reset(udc);

	ambarella_init_gadget(udc);
	ambarella_udc_reinit(udc);
	ambarella_regaddr_map(udc);

	/*initial usb hardware, and set soft disconnect */
	ambarella_init_usb(udc);

	//udc->setup_buf = dma_pool_alloc(udc->desc_dma_pool, GFP_KERNEL,&udc->setup_addr);
	udc->setup_buf = memalign(DESC_ENT_ALIGN, sizeof(struct ambarella_setup_desc));
	udc->setup_addr = (dma_addr_t)udc->setup_buf;
	if(udc->setup_buf == NULL) {
		printk(KERN_ERR "No memory to DMA\n");
		return -ENOMEM;
	}
	//invalidate_dcache_range((unsigned long)udc->setup_buf,
	//	(unsigned long)udc->setup_buf + sizeof(struct ambarella_setup_desc));

	retval = init_null_pkt_desc(udc);
	if(retval){
		return retval;
	}
	debug("%s Probe done\n", dev->name);

	retval = usb_add_gadget_udc((struct device *)dev, &udc->gadget);

	return 0;
}

/*
 * Name: device_interrupt
 * Description:
 *	Process related device interrupt
 */
static void udc_device_interrupt(struct ambarella_udc *udc, u32 int_value)
{
	/* case 1. Get set_config or set_interface request from host */
	if (int_value & (USB_DEV_SET_CFG | USB_DEV_SET_INTF)) {
		struct usb_ctrlrequest crq;
		u32 i, ret, csr_config;

		if(int_value & USB_DEV_SET_CFG) {
			/* Ack the SC interrupt */
			writel(USB_DEV_SET_CFG, udc->base + USB_DEV_INTR_REG);
			udc->cur_config = (u16)(readl(udc->base + USB_DEV_STS_REG) & USB_DEV_CFG_NUM);

			crq.bRequestType = 0x00;
			crq.bRequest = USB_REQ_SET_CONFIGURATION;
			crq.wValue = cpu_to_le16(udc->cur_config);
			crq.wIndex = 0x0000;
			crq.wLength = 0x0000;
		} else if(int_value & USB_DEV_SET_INTF){
			/* Ack the SI interrupt */
			writel(USB_DEV_SET_INTF, udc->base + USB_DEV_INTR_REG);
			udc->cur_intf = (readl(udc->base + USB_DEV_STS_REG) & USB_DEV_INTF_NUM) >> 4;
			udc->cur_alt = (readl(udc->base + USB_DEV_STS_REG) & USB_DEV_ALT_SET) >> 8;

			crq.bRequestType = 0x01;
			crq.bRequest = USB_REQ_SET_INTERFACE;
			crq.wValue = cpu_to_le16(udc->cur_alt);
			crq.wIndex = cpu_to_le16(udc->cur_intf);
			crq.wLength = 0x0000;
		}

		for (i = 0; i < EP_NUM_MAX; i++){
			udc->ep[i].halted = 0;
			clrbits_32(udc->base + udc->ep[i].ep_reg.ctrl_reg, USB_EP_STALL);
		}

		/* setup ep0 CSR. Note: ep0-in and ep0-out share the same CSR reg */
		csr_config = (udc->cur_config << 7) | (udc->cur_intf << 11) |
			(udc->cur_alt << 15);
		clrbits_32(udc->base + USB_UDC_REG(CTRL_IN), 0xfff << 7);
		setbits_32(udc->base + USB_UDC_REG(CTRL_IN), csr_config);

		udc->auto_ack_0_pkt = 1;
		ambarella_ep_nuke(&udc->ep[CTRL_OUT], -EPROTO);
		spin_unlock(&udc->lock);
		ret = udc->driver->setup(&udc->gadget, &crq);
		spin_lock(&udc->lock);
		if(ret < 0)
			printk(KERN_ERR "set config failed. (%d)\n", ret);

		/* told UDC the configuration is done, and to ack HOST
		 * UDC has to ack the host quickly, or Host will think failed,
		 * do don't add much debug message when receive SC/SI irq.*/
		setbits_32(udc->base + USB_DEV_CTRL_REG, USB_DEV_CSR_DONE);
		udelay(150);
		//usb_gadget_set_state(&udc->gadget, USB_STATE_CONFIGURED);
		udc->gadget.state = USB_STATE_CONFIGURED;
		schedule_work(&udc->uevent_work);
	}

	/* case 2. Get reset Interrupt */
	else if (int_value & USB_DEV_RESET) {

		debug("USB reset IRQ\n");
		writel(USB_DEV_RESET, udc->base + USB_DEV_INTR_REG);
#if 0
		ambarella_disable_all_intr(udc);

		if (udc->host_suspended && udc->driver && udc->driver->resume){
			spin_unlock(&udc->lock);
			if(udc->driver->resume)
				udc->driver->resume(&udc->gadget);
			spin_lock(&udc->lock);
			udc->host_suspended = 0;
		}

		ambarella_stop_activity(udc);

		//udc->gadget.speed = USB_SPEED_UNKNOWN;
		udc->auto_ack_0_pkt = 0;
		udc->remote_wakeup_en = 0;

		udc->udc_is_enabled = 0;
		ambarella_udc_enable(udc);

		//usb_gadget_set_state(&udc->gadget, USB_STATE_ATTACHED);
		udc->gadget.state = USB_STATE_ATTACHED;
		schedule_work(&udc->uevent_work);
#if 0
		/* enable suspend interrupt */
		clrbitsl(udc->base + USB_DEV_INTR_MSK_REG, UDC_INTR_MSK_US);
#endif
#endif
	}

	/* case 3. Get suspend Interrupt */
	else if (int_value & USB_DEV_SUSP) {

		pr_err("%s: USB suspend IRQ\n", __func__);

		writel(USB_DEV_SUSP, udc->base + USB_DEV_INTR_REG);

		if (udc->driver->suspend) {
			udc->host_suspended = 1;
			spin_unlock(&udc->lock);
			if(udc->driver->suspend)
				udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	/* case 4. enumeration complete */
	else if(int_value & USB_DEV_ENUM_CMPL) {
		u32 	value = 0;

		/* Ack the CMPL interrupt */
		writel(USB_DEV_ENUM_CMPL, udc->base + USB_DEV_INTR_REG);

		value = readl(udc->base + USB_DEV_STS_REG) & USB_DEV_ENUM_SPD;

		if(value == USB_DEV_ENUM_SPD_HI) {  /* high speed */

			debug("enumeration IRQ - "
					"High-speed bus detected\n");
			udc->gadget.speed = USB_SPEED_HIGH;
		} else if (value == USB_DEV_ENUM_SPD_FU) { /* full speed */

			debug("enumeration IRQ - "
					"Full-speed bus detected\n");
			udc->gadget.speed = USB_SPEED_FULL;
		} else {
			printk(KERN_ERR "Not supported speed!"
					"USB_DEV_STS_REG = 0x%x\n", value);
			udc->gadget.speed = USB_SPEED_UNKNOWN;

		}
	} /* ENUM COMPLETE */
	else {
		printk(KERN_ERR "Unknown Interrupt:0x%08x\n", int_value);
		/* Ack the Unknown interrupt */
		writel(int_value, udc->base + USB_DEV_INTR_REG);
	}
}

/*
 * Name: udc_epin_interrupt
 * Description:
 *	Process IN(CTRL or BULK) endpoint interrupt
 */
static void udc_epin_interrupt(struct ambarella_udc *udc, u32 ep_id)
{
	u32 ep_status = 0;
	struct ambarella_ep *ep = &udc->ep[ep_id];
	struct ambarella_request *req;

	ep_status = readl(udc->base + ep->ep_reg.sts_reg);

	/* TxFIFO is empty, but we've not used this bit, so just ignored simply. */
	if (ep_status == USB_EP_TXFIFO_EMPTY) {
		writel(ep_status, udc->base + ep->ep_reg.sts_reg);
		return;
	}

	debug("%s: ep_status = 0x%08x\n", ep->ep.name, ep_status);

	if (ambarella_handle_ep_stall(ep, ep_status))
		return;

	if (ambarella_check_bna_error(ep, ep_status)
			|| ambarella_check_he_error(ep, ep_status)) {
		struct ambarella_request	*req = NULL;
		ep->dma_going = 0;
		ep->cancel_transfer = 0;
		ep->need_cnak = 0;
		if (!list_empty(&ep->queue)) {
			req = list_first_entry(&ep->queue, struct ambarella_request,queue);
			req->req.status = -EPROTO;
			ambarella_udc_done(ep, req, 0);
		}
		return;
	}

	if (ep_status & USB_EP_TRN_DMA_CMPL) {
		/* write dummy desc to try to avoid BNA error */
		ep->udc->dummy_desc->status =
			USB_DMA_BUF_HOST_RDY | USB_DMA_LAST;
		flush_dcache_range((unsigned long)udc->dummy_desc_addr, 
			(unsigned long)udc->dummy_desc_addr + sizeof(struct ambarella_data_desc));
		writel(udc->dummy_desc_addr | udc->dma_fix,
				udc->base + ep->ep_reg.dat_desc_ptr_reg);

		if(ep->halted || ep->dma_going == 0 || ep->cancel_transfer == 1
				|| list_empty(&ep->queue)) {
			ep_status &= (USB_EP_IN_PKT | USB_EP_TRN_DMA_CMPL);
			writel(ep_status, udc->base + ep->ep_reg.sts_reg);
			ep->dma_going = 0;
			ep->cancel_transfer = 0;
			return;
		}

		ep->dma_going = 0;
		ep->cancel_transfer = 0;
		ep->need_cnak = 0;

		ep->last_data_desc = ambarella_get_last_desc(ep);
		if(ep->last_data_desc == NULL){
			printk(KERN_ERR "%s: last_data_desc is NULL\n", ep->ep.name);
			BUG();
			return;
		}
		ambarella_handle_data_in(&udc->ep[ep_id]);
	} else if(ep_status & USB_EP_IN_PKT) {
#if 0
		if (IS_ISO_IN_EP(ep))
			goto finish;
#endif

		if(!ep->halted && !ep->cancel_transfer && !list_empty(&ep->queue)){
			req = list_first_entry(&ep->queue,
				struct ambarella_request, queue);
			ambarella_set_tx_dma(ep, req, 0);
		} else if (ep->dma_going == 0 || ep->halted || ep->cancel_transfer) {
			ambarella_set_ep_nak(ep);
		}
		ep->cancel_transfer = 0;
	} else if (ep_status != 0){
		pr_err("%s: %s, Unknown int source(0x%08x)\n", __func__,
			ep->ep.name, ep_status);
		writel(ep_status, udc->base + ep->ep_reg.sts_reg);
		return;
	}

#if 0
finish:
#endif
	if (ep_status != 0) {
		ep_status &= (USB_EP_IN_PKT | USB_EP_TRN_DMA_CMPL | USB_EP_TXFIFO_EMPTY);
//		ep_status &= (USB_EP_IN_PKT | USB_EP_TRN_DMA_CMPL | USB_EP_RCV_CLR_STALL);
		writel(ep_status, udc->base + ep->ep_reg.sts_reg);
	}
}


/*
 * Name: udc_epout_interrupt
 * Description:
 *	Process OUT endpoint interrupt
 */
static void udc_epout_interrupt(struct ambarella_udc *udc, u32 ep_id)
{
	struct ambarella_ep *ep = &udc->ep[ep_id];
	u32 desc_status, ep_status, i;

	/* check the status bits for what kind of packets in */
	ep_status = readl(udc->base + ep->ep_reg.sts_reg);

	if (ambarella_handle_ep_stall(ep, ep_status))
		return;


	if(ep_id == CTRL_OUT) {
		/* Cope with setup-data packet  */
		if((ep_status & USB_EP_OUT_PKT_MSK) == USB_EP_SETUP_PKT){
			writel(USB_EP_SETUP_PKT, udc->base + ep->ep_reg.sts_reg);
			ep->ctrl_sts_phase = 0;
			ep->dma_going = 0;
			ambarella_handle_request_packet(udc);
		}
	}

	/* Cope with normal data packet  */
	if((ep_status & USB_EP_OUT_PKT_MSK) == USB_EP_OUT_PKT) {
		writel(USB_EP_OUT_PKT, udc->base + ep->ep_reg.sts_reg);
		ep->dma_going = 0;

		/* Just cope with the zero-length packet */
		if(ep->ctrl_sts_phase == 1) {
			ep->ctrl_sts_phase = 0;
			ambarella_enable_rx_dma(ep);
			ep->dma_going = 0;
			return;
		}

		if(ep->halted || ep->cancel_transfer || list_empty(&ep->queue)) {
			writel(ep_status, udc->base + ep->ep_reg.sts_reg);
			return;
		}

		if (ambarella_check_bna_error(ep, ep_status))
			return;

		if(ambarella_check_he_error(ep, ep_status) && !list_empty(&ep->queue)) {
			struct ambarella_request *req = NULL;
			req = list_first_entry(&ep->queue,
				struct ambarella_request, queue);
			req->req.status = -EPROTO;
			ambarella_udc_done(ep, req, 0);
			return;
		}

		ep->last_data_desc = ambarella_get_last_desc(ep);
		if(ep->last_data_desc == NULL){
			pr_err("%s: %s, last_data_desc is NULL\n", __func__, ep->ep.name);
			BUG();
			return;
		}

		if(ep_id != CTRL_OUT){
			desc_status = ep->last_data_desc->status;
			/* received data */
			if((desc_status & USB_DMA_BUF_STS) == USB_DMA_BUF_DMA_DONE) {
				setbits_32(udc->base + USB_DEV_EP_INTR_MSK_REG, 1 << ep_id);
				ambarella_set_ep_nak(ep);
			}
		}

		ambarella_handle_data_out(ep);

		/* clear NAK for TX dma */
		if (readl(udc->base + USB_DEV_STS_REG) & USB_DEV_RXFIFO_EMPTY_STS) {
			for (i = 0; i < EP_NUM_MAX; i++) {
				struct ambarella_ep *_ep = &udc->ep[i];
				if (_ep->need_cnak == 1)
					ambarella_clr_ep_nak(_ep);
			}
		}
	}

	return;
}


int dm_usb_gadget_handle_interrupts(struct udevice *dev)
{
	struct ambarella_udc *udc = dev_get_priv(dev);
	u32 value, handled = 0, i, ep_irq;

	/* 1. check if device interrupt */
	value = readl(udc->base + USB_DEV_INTR_REG);
	if(value) {

		debug("device int value = 0x%x\n", value);

		handled = 1;
		udc_device_interrupt(udc, value);

	}
	/* 2. check if endpoint interrupt */
	value = readl(udc->base + USB_DEV_EP_INTR_REG);
	if(value) {
		debug("endpoint value = 0x%x\n", value);
		handled = 1;

		for(i = 0; i < EP_NUM_MAX; i++){
			ep_irq = 1 << i;
			if (!(value & ep_irq))
				continue;

			/* ack the endpoint interrupt */
			writel(ep_irq, udc->base + USB_DEV_EP_INTR_REG);

			/* irq for out ep ? */
			if (i >= EP_IN_NUM)
				udc_epout_interrupt(udc, i);
			else
				udc_epin_interrupt(udc, i);

			value &= ~ep_irq;
			if(value == 0)
				break;
		}
	}

	return handled;
}

#if 0
int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	int ret;

	if (!driver)
		return -EINVAL;
	if (!driver->bind || !driver->setup || !driver->disconnect)
		return -EINVAL;
	if (driver->speed != USB_SPEED_FULL && driver->speed != USB_SPEED_HIGH)
		return -EINVAL;

	ret = driver->bind(&udc_priv.gadget);
	if (ret) {
		debug("driver->bind() returned %d\n", ret);
		return ret;
	}
	udc_priv.driver = driver;

	return 0;
}

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	driver->disconnect(&udc_priv.gadget);
	driver->unbind(&udc_priv.gadget);
	udc_priv.driver = NULL;

	ambarella_udc_set_pullup(&udc_priv, 0);

	return 0;
}
#endif

static int __ofdata_to_platdata(struct udevice *dev)
{
	struct ambarella_udc *platdata = dev_get_priv(dev);

	platdata->base = (void *)dev_read_addr(dev);
	platdata->scr_reg = syscon_regmap_lookup_by_phandle(dev,
			"amb,scr-regmap");
	return 0;
}

static const struct udevice_id ambarella_udc_ids[] = {
	{ .compatible = "ambarella,udc" },
	{},
};

U_BOOT_DRIVER(ambarella_udc) = {
	.name	= "Ambarella udc",
	.id	= UCLASS_USB_GADGET_GENERIC,
	.of_match = ambarella_udc_ids,
	.probe = ambarella_udc_probe,
	.remove = NULL,
	.ofdata_to_platdata = __ofdata_to_platdata,
	.priv_auto_alloc_size = sizeof(struct ambarella_udc),
};
