#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "usb-gamecube-adapter.h"

#include "usb-gamecube-adapter.h"

/* Callers must hold gdata->odata_lock spinlock */
static int gc_queue_rumble(struct gc_data *gdata)
{
	int error;
	memcpy(gdata->odata + 1, gdata->odata_rumbles,
			 sizeof(gdata->odata_rumbles));
	gdata->irq_out_active = true;
	gdata->rumble_changed = false;
	gdata->odata[0] = 0x11;
	gdata->irq_out->transfer_buffer_length = 5;

	usb_anchor_urb(gdata->irq_out, &gdata->irq_out_anchor);
	error = usb_submit_urb(gdata->irq_out, GFP_ATOMIC);
	if (error) {
		dev_err(&gdata->intf->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, error);
		usb_unanchor_urb(gdata->irq_out);
		error = -EIO;
	}
	return error;
}

static void gc_irq_out(struct urb *urb)
{
	struct gc_data *gdata = urb->context;
	struct device *dev = &gdata->intf->dev;
	int status = urb->status;
	unsigned long flags;

	spin_lock_irqsave(&gdata->odata_lock, flags);

	switch (status) {
	case 0:
		/* success */
		gdata->irq_out_active = gdata->rumble_changed;
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		gdata->irq_out_active = false;
		break;

	default:
		dev_dbg(dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		break;
	}
	if (gdata->irq_out_active) {
		gc_queue_rumble(gdata);
	}
	spin_unlock_irqrestore(&gdata->odata_lock, flags);
}

static int gc_init_output(struct gc_data *gdata,
			 struct usb_endpoint_descriptor *irq)
{
	int error = -ENOMEM;

	init_usb_anchor(&gdata->irq_out_anchor);

	gdata->odata = usb_alloc_coherent(gdata->udev, GCC_OUT_PKT_LEN, GFP_KERNEL,
			 &gdata->odata_dma);
	if (!gdata->odata)
		return error;

	spin_lock_init(&gdata->odata_lock);

	gdata->irq_out = usb_alloc_urb(0, GFP_KERNEL);

	if (!gdata->irq_out)
		goto err_free_coherent;

	usb_fill_int_urb(gdata->irq_out, gdata->udev,
			 usb_sndintpipe(gdata->udev, irq->bEndpointAddress),
			 gdata->odata, GCC_OUT_PKT_LEN, gc_irq_out, gdata,
			 irq->bInterval);
	gdata->irq_out->transfer_dma = gdata->odata_dma;
	gdata->irq_out->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	return 0;

err_free_coherent:
	usb_free_coherent(gdata->udev, GCC_OUT_PKT_LEN, gdata->odata,
			 gdata->odata_dma);
	return error;
}

static void gc_deinit_output(struct gc_data *gdata)
{
	usb_free_urb(gdata->irq_out);
	usb_free_coherent(gdata->udev, GCC_OUT_PKT_LEN, gdata->odata,
			 gdata->odata_dma);
}

static void gc_irq_in(struct urb *urb)
{
	struct gc_data *gdata = urb->context;
	struct usb_interface *intf = gdata->intf;
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
	if (gdata->irq_in->actual_length != GCC_IN_PKT_LEN)
		dev_warn(&intf->dev, "Bad sized packet\n");
	else if (gdata->idata[0] != 0x21)
		dev_warn(&intf->dev, "Unknown opcode %d\n", gdata->idata[0]);
	else
		; // TODO: code the handler function
exit:
	error = usb_submit_urb(gdata->irq_in, GFP_ATOMIC);
	if (error)
		dev_err(&intf->dev, "controller urb failed: %d\n", error);
}

static int gc_init_input(struct gc_data *gdata,
			 struct usb_endpoint_descriptor *irq)
{
	int error = -ENOMEM;

	gdata->idata = usb_alloc_coherent(gdata->udev, GCC_IN_PKT_LEN, GFP_KERNEL,
			 &gdata->idata_dma);
	if (!gdata->idata)
		return error;

	gdata->irq_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!gdata->irq_in)
		goto err_free_coherent;

	usb_fill_int_urb(gdata->irq_in, gdata->udev,
			 usb_rcvintpipe(gdata->udev, irq->bEndpointAddress),
			 gdata->idata, GCC_IN_PKT_LEN, gc_irq_in, gdata,
			 irq->bInterval);
	gdata->irq_in->transfer_dma = gdata->idata_dma;
	gdata->irq_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	return 0;

err_free_coherent:
	usb_free_coherent(gdata->udev, GCC_IN_PKT_LEN, gdata->idata,
			 gdata->idata_dma);
	return error;

}


static void gc_deinit_input(struct gc_data *gdata)
{
	usb_free_urb(gdata->irq_in);
	usb_free_coherent(gdata->udev, GCC_IN_PKT_LEN, gdata->idata,
			 gdata->idata_dma);
}


static int gc_init_irq(struct gc_data *gdata)
{
	struct usb_endpoint_descriptor *eps[] = { NULL, NULL };
	int error;

	error = usb_find_common_endpoints(gdata->intf->cur_altsetting, NULL, NULL,
					  &eps[0], &eps[1]);
	if (error)
		return -ENODEV;
	error = gc_init_output(gdata, eps[1]);
	if (error)
		return error;
	error = gc_init_input(gdata, eps[0]);
	if (error)
		goto err_deinit_out;

	memset(gdata->odata_rumbles, 0, 4);
	gdata->rumble_changed = false;
	gdata->irq_out_active = true;
	gdata->odata[0] = 0x13;
	gdata->irq_out->transfer_buffer_length = 1;

	error = usb_submit_urb(gdata->irq_in, GFP_KERNEL);
	if (error)
		goto err_deinit_in;

	usb_anchor_urb(gdata->irq_out, &gdata->irq_out_anchor);
	error = usb_submit_urb(gdata->irq_out, GFP_ATOMIC);
	if (error) {
		dev_err(&gdata->intf->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, error);
		usb_unanchor_urb(gdata->irq_out);
		error = -EIO;
		goto err_kill_in_urb;
	}

	return 0;
err_kill_in_urb:
	usb_kill_urb(gdata->irq_in);
err_deinit_in:
	gc_deinit_input(gdata);
err_deinit_out:
	gc_deinit_output(gdata);
	return error;
}

static void gc_deinit_irq(struct gc_data *gdata)
{
	if (!usb_wait_anchor_empty_timeout(&gdata->irq_out_anchor, 5000)) {
		dev_warn(&gdata->intf->dev,
			 "timed out waiting for output URB to complete, killing\n");
		usb_kill_anchored_urbs(&gdata->irq_out_anchor);
	}
	usb_kill_urb(gdata->irq_in);
	/* Make sure we are done with presence work if it was scheduled */
	//flush_work(&xpad->work);
	// TODO: Clean worker

	gc_deinit_input(gdata);
	gc_deinit_output(gdata);
}

int gc_set_rumble_value(struct gc_data *gdata, u8 controller, u8 value)
{
	unsigned long flags;
	int error;

	value = !!value;
	if (controller > 4)
		return -EINVAL;

	spin_lock_irqsave(&gdata->odata_lock, flags);
	if (gdata->odata_rumbles[controller] == value) {
		spin_unlock_irqrestore(&gdata->odata_lock, flags);
		return 0;
	}
	gdata->odata_rumbles[controller] = value;
	gdata->rumble_changed = true;
	if (!gdata->irq_out_active) {
		error = gc_queue_rumble(gdata);
	}
	spin_unlock_irqrestore(&gdata->odata_lock, flags);
	return error;
}

static void gc_init_controllers(struct gc_data *gdata)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gdata->controllers); i++) {
		gdata->controllers[i].adapter = gdata;
		gdata->controllers[i].no = i;
		gdata->controllers[i].status = GAMECUBE_NONE;
	}
}

int gc_usb_probe(struct usb_interface *iface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(iface);
	struct gc_data *gdata;
	int error;

	gdata = kzalloc(sizeof(struct gc_data), GFP_KERNEL);
	if (!gdata)
		return -ENOMEM;
	usb_set_intfdata(iface, gdata);
	gdata->udev = udev;
	gdata->intf = iface;
	gc_init_controllers(gdata);
	error = gc_init_irq(gdata);
	if (error)
		goto err_free_devs;
	error = gc_init_attr(gdata);
	if (error)
		goto err_deinit_endpoints;
	dev_info(&iface->dev, "New device registered\n");
	return 0;
err_deinit_endpoints:
	gc_deinit_irq(gdata);
err_free_devs:
	usb_set_intfdata(iface, NULL);
	kfree(gdata);
	return error;
}

static void gc_usb_disconnect(struct usb_interface *iface)
{
	struct gc_data *gdat = usb_get_intfdata(iface);
	gc_deinit_attr(gdat);
	gc_deinit_irq(gdat);
	usb_set_intfdata(iface, NULL);
	kfree(gdat);
	return;
}

static const struct usb_device_id gc_usb_devices[] = {
	{ USB_DEVICE(USB_VENDOR_ID_NINTENDO,
		     USB_DEVICE_ID_NINTENDO_GAMECUBE_ADAPTER) },
	{}
};

MODULE_DEVICE_TABLE(usb, gc_usb_devices);

static struct usb_driver gc_usb_driver = {
	.name		= "gamecube_adapter",
	.id_table	= gc_usb_devices,
	.probe		= gc_usb_probe,
	.disconnect	= gc_usb_disconnect,
};

module_usb_driver(gc_usb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robin Milas <milas.robin@live.fr>");
MODULE_DESCRIPTION("Driver for GameCube adapter");
