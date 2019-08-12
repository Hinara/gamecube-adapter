#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
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
	if (dev->in.urb->actual_length != sizeof(dev->data)) {
		pr_warn("Bad sized packet\n");
	} else if (dev->in.data[0] != 0x21) {
		pr_warn("Unknown opcode %d\n", dev->in.data[0]);
	} else {
		memcpy(dev->data, dev->in.data, sizeof(dev->data));
	}
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

int gc_adapter_start_input(struct gc_data *dev)
{
	int error = usb_submit_urb(dev->in.urb, GFP_KERNEL);
	if (error)
		return error;
	error = gc_send_init(dev);
	if (error) {
		usb_kill_urb(dev->in.urb);
		return error;
	}
	return 0;
}

static int gc_alloc_ep(struct gc_data *dev, size_t len, struct gc_ep *ep)
{
	int error;

	ep->len = len;
	ep->data = usb_alloc_coherent(dev->udev, ep->len,
					 GFP_KERNEL, &ep->dma);
	if (!ep->data)
		return -ENOMEM;

	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb) {
		error = -ENOMEM;
		goto err_free_coherent;
	}
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
			 ep->data, ep->len,
			 controller_irq_in, dev, irq->bInterval);
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
			 ep->data, ep->len,
			 controller_irq_out, dev, irq->bInterval);
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

static void gc_deinit_in_ep(struct gc_data *dev)
{
	return gc_deinit_ep(dev, &dev->in);
}

static void gc_deinit_out_ep(struct gc_data *dev)
{
	return gc_deinit_ep(dev, &dev->out.ep);
}


int gc_usb_probe(struct usb_interface *iface,
		 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(iface);
	struct ep_irq_pair eps = { NULL, NULL };
	struct gc_data *dev;
	int error;

	error = usb_find_common_endpoints(iface->cur_altsetting, NULL, NULL, &eps.in, &eps.out);
	if (error || eps.out->wMaxPacketSize != 5 || eps.in->wMaxPacketSize != 37)
		return -ENODEV;
	dev = kzalloc(sizeof(struct gc_data), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	usb_set_intfdata(iface, dev);
	dev->udev = udev;
	dev->intf = iface;
	error = gc_init_in_ep(dev, eps.in);
	if (error)
		goto err_free_devs;
	error = gc_init_out_ep(dev, eps.out);
	if (error)
		goto err_free_in_ep;
	error = gc_init_attr(dev);
	if (error)
		goto err_free_out_ep;
	error = gc_adapter_start_input(dev);
	if (error)
		goto err_deinit_attrs;
	dev_info(&iface->dev, "New device registered\n");
	return 0;
err_deinit_attrs:
	gc_deinit_attr(dev);
err_free_out_ep:
	gc_deinit_out_ep(dev);
err_free_in_ep:
	gc_deinit_in_ep(dev);
err_free_devs:
	usb_set_intfdata(iface, NULL);
	kfree(dev);
	return error;
}

static void gc_usb_disconnect(struct usb_interface *iface)
{
	struct gc_data *dev = usb_get_intfdata(iface);
	usb_kill_urb(dev->in.urb);
	usb_kill_urb(dev->out.ep.urb);
	usb_set_intfdata(iface, NULL);
	gc_deinit_attr(dev);
	gc_deinit_out_ep(dev);
	gc_deinit_in_ep(dev);
	kfree(dev);
	return;
}

static const struct usb_device_id gc_usb_devices[] = {

	{ USB_DEVICE(USB_VENDOR_ID_NINTENDO,
		USB_DEVICE_ID_NINTENDO_GAMECUBE_ADAPTER) },
	{ }
};

MODULE_DEVICE_TABLE(usb, gc_usb_devices);

static struct usb_driver gc_usb_driver = {
	.name = "gamecube_adapter",
	.id_table = gc_usb_devices,
	.probe = gc_usb_probe,
	.disconnect = gc_usb_disconnect,
};

module_usb_driver(gc_usb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robin Milas <milas.robin@live.fr>");
MODULE_DESCRIPTION("Driver for GameCube adapter");
