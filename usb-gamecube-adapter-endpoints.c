#include "usb-gamecube-adapter.h"

static void controller_irq_in(struct urb *urb)
{
	struct gc_data *dev = urb->context;
	struct usb_interface *intf = dev->intf;
	int error;

	switch (urb->status) {
	case 0:
		break;
	case -EOVERFLOW:
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dev_dbg(&intf->dev, "controller urb shutting down: %d\n",
			urb->status);
		return;
	default:
		dev_dbg(&intf->dev, "controller urb status: %d\n", urb->status);
		goto exit;
	}
	if (dev->in.urb->actual_length != sizeof(dev->data))
		dev_warn(&intf->dev, "Bad sized packet\n");
	else if (dev->in.data[0] != 0x21)
		dev_warn(&intf->dev, "Unknown opcode %d\n", dev->in.data[0]);
	else
		memcpy(dev->data, dev->in.data, sizeof(dev->data));
exit:
	error = usb_submit_urb(dev->in.urb, GFP_ATOMIC);
	if (error)
		dev_err(&intf->dev, "controller urb failed: %d\n", error);
}

void controller_irq_out(struct urb *urb)
{
	/*	struct gc_data *dev = urb->context;
	struct usb_interface *intf = dev->intf;
	int error = 0;

	printk(KERN_DEBUG " URB Status %d\n", urb->status);
	switch (urb->status) {
	case 0:
		break;
	case -EOVERFLOW:
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dev_dbg(&intf->dev, "controller urb shutting down: %d\n",
			urb->status);
		return;
	default:
		dev_dbg(&intf->dev, "controller urb status: %d\n", urb->status);
		goto exit;
	}
exit:
	//error = usb_submit_urb(dev->irq_out, GFP_ATOMIC);
	//gamecube_send_init(dev);
	if (error)
		dev_err(&intf->dev, "controller urb failed: %d\n", error);
*/
}

static int gc_alloc_ep(struct gc_data *dev, size_t len, struct gc_ep *ep)
{
	int error = -ENOMEM;

	ep->len = len;
	ep->data = usb_alloc_coherent(dev->udev, ep->len, GFP_KERNEL, &ep->dma);
	if (!ep->data)
		return error;
	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb)
		goto err_free_coherent;
	return 0;

err_free_coherent:
	usb_free_coherent(dev->udev, ep->len, ep->data, ep->dma);
	return error;
}

static int gc_init_in_ep(struct gc_data *dev,
			 struct usb_endpoint_descriptor *irq)
{
	struct gc_ep *ep = &dev->in;
	int error = gc_alloc_ep(dev, irq->wMaxPacketSize, ep);

	if (error)
		return error;
	usb_fill_int_urb(ep->urb, dev->udev,
			 usb_rcvintpipe(dev->udev, irq->bEndpointAddress),
			 ep->data, ep->len, controller_irq_in, dev,
			 irq->bInterval);
	ep->urb->transfer_dma = ep->dma;
	ep->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	return 0;
}

static int gc_init_out_ep(struct gc_data *dev,
			  struct usb_endpoint_descriptor *irq)
{
	struct gc_out_ep *out = &dev->out;
	struct gc_ep *ep = &out->ep;
	int error = gc_alloc_ep(dev, irq->wMaxPacketSize, ep);

	if (error)
		return error;
	usb_fill_int_urb(ep->urb, dev->udev,
			 usb_sndintpipe(dev->udev, irq->bEndpointAddress),
			 ep->data, ep->len, controller_irq_out, dev,
			 irq->bInterval);
	ep->urb->transfer_dma = ep->dma;
	ep->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	init_usb_anchor(&out->anchor);
	spin_lock_init(&out->lock);
	return 0;
}

static void gc_deinit_ep(struct gc_data *dev, struct gc_ep *ep)
{
	usb_free_urb(ep->urb);
	usb_free_coherent(dev->udev, ep->len, ep->data, ep->dma);
}

int gc_init_endpoints(struct gc_data *dev)
{
	struct usb_endpoint_descriptor *eps[] = { NULL, NULL };
	int error;

	error = usb_find_common_endpoints(dev->intf->cur_altsetting, NULL, NULL,
					  &eps[0], &eps[1]);
	if (error || eps[0]->wMaxPacketSize != 37 ||
	    eps[1]->wMaxPacketSize != 5)
		return -ENODEV;
	error = gc_init_out_ep(dev, eps[1]);
	if (error)
		return error;
	error = gc_init_in_ep(dev, eps[0]);
	if (error)
		goto err_deinit_out;
	return 0;
err_deinit_out:
	gc_deinit_ep(dev, &dev->out.ep);
	return error;
}

void gc_deinit_endpoints(struct gc_data *dev)
{
	gc_deinit_ep(dev, &dev->in);
	gc_deinit_ep(dev, &dev->out.ep);
}
