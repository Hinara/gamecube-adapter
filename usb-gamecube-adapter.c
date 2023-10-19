#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb/input.h>

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

static void gcc_input(struct gcc_data *gccdata, const u8 *keys)
{
	input_report_key(gccdata->input, BTN_A, !!(keys[1] & BIT(0)));
	input_report_key(gccdata->input, BTN_B, !!(keys[1] & BIT(1)));
	input_report_key(gccdata->input, BTN_X, !!(keys[1] & BIT(2)));
	input_report_key(gccdata->input, BTN_Y, !!(keys[1] & BIT(3)));
	input_report_key(gccdata->input, BTN_DPAD_LEFT, !!(keys[1] & BIT(4)));
	input_report_key(gccdata->input, BTN_DPAD_RIGHT, !!(keys[1] & BIT(5)));
	input_report_key(gccdata->input, BTN_DPAD_DOWN, !!(keys[1] & BIT(6)));
	input_report_key(gccdata->input, BTN_DPAD_UP, !!(keys[1] & BIT(7)));

	input_report_key(gccdata->input, BTN_START, !!(keys[2] & BIT(0)));
	input_report_key(gccdata->input, BTN_TR2, !!(keys[2] & BIT(1)));
	input_report_key(gccdata->input, BTN_TR, !!(keys[2] & BIT(2)));
	input_report_key(gccdata->input, BTN_TL, !!(keys[2] & BIT(3)));

	input_report_abs(gccdata->input, ABS_X, keys[3]);
	input_report_abs(gccdata->input, ABS_Y, keys[4] ^ 0xFF);
	input_report_abs(gccdata->input, ABS_RX, keys[5]);
	input_report_abs(gccdata->input, ABS_RY, keys[6] ^ 0xFF);
	input_report_abs(gccdata->input, ABS_Z, keys[7]);
	input_report_abs(gccdata->input, ABS_RZ, keys[8]);

	input_sync(gccdata->input);
}

static int gc_set_rumble_value(struct gc_data *gdata, u8 controller, u8 value)
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

static int gc_rumble_play(struct input_dev *dev, void *data,
			      struct ff_effect *eff)
{
	struct gcc_data *gccdata = input_get_drvdata(dev);
	u8 value;

	/*
	 * The gamecube controller  supports only a single rumble motor so if any
	 * magnitude is set to non-zero then we start the rumble motor. If both are
	 * set to zero, we stop the rumble motor.
	 */

	if (eff->u.rumble.strong_magnitude || eff->u.rumble.weak_magnitude)
		value = 1;
	else
		value = 0;
	return gc_set_rumble_value(gccdata->adapter, gccdata->no, value);
}

static u8 gc_connected_type(u8 status)
{
	u8 type = status & (GAMECUBE_WIRED | GAMECUBE_WIRELESS);
	switch (type)
	{
		case GAMECUBE_WIRED:
		case GAMECUBE_WIRELESS:
			return type;
		default:
			return 0;
	}
}

static int gc_controller_init(struct gcc_data *gccdev, u8 status)
{
	int error;

	gccdev->input = input_allocate_device();
	if (!gccdev->input)
		return -ENOMEM;

	input_set_drvdata(gccdev->input, gccdev);
	usb_to_input_id(gccdev->adapter->udev, &gccdev->input->id);
	gccdev->input->name = "Nintendo GameCube Controller";
	gccdev->input->phys = gccdev->adapter->phys;

	set_bit(EV_KEY, gccdev->input->evbit);

	set_bit(BTN_A, gccdev->input->keybit);
	set_bit(BTN_B, gccdev->input->keybit);
	set_bit(BTN_X, gccdev->input->keybit);
	set_bit(BTN_Y, gccdev->input->keybit);
	set_bit(BTN_DPAD_LEFT, gccdev->input->keybit);
	set_bit(BTN_DPAD_RIGHT, gccdev->input->keybit);
	set_bit(BTN_DPAD_DOWN, gccdev->input->keybit);
	set_bit(BTN_DPAD_UP, gccdev->input->keybit);
	set_bit(BTN_START, gccdev->input->keybit);
	set_bit(BTN_TR2, gccdev->input->keybit);
	set_bit(BTN_TR, gccdev->input->keybit);
	set_bit(BTN_TL, gccdev->input->keybit);

	set_bit(EV_ABS, gccdev->input->evbit);

	set_bit(ABS_X, gccdev->input->absbit);
	set_bit(ABS_Y, gccdev->input->absbit);
	set_bit(ABS_RX, gccdev->input->absbit);
	set_bit(ABS_RY, gccdev->input->absbit);
	set_bit(ABS_Z, gccdev->input->absbit);
	set_bit(ABS_RZ, gccdev->input->absbit);

	input_set_abs_params(gccdev->input, ABS_X, 0, 255, 16, 16);
	input_set_abs_params(gccdev->input, ABS_Y, 0, 255, 16, 16);
	input_set_abs_params(gccdev->input, ABS_RX, 0, 255, 16, 16);
	input_set_abs_params(gccdev->input, ABS_RY, 0, 255, 16, 16);
	input_set_abs_params(gccdev->input, ABS_Z, 0, 255, 16, 0);
	input_set_abs_params(gccdev->input, ABS_RZ, 0, 255, 16, 0);

	error = input_ff_create_memless(gccdev->input, NULL, gc_rumble_play);
	if (error) {
		dev_warn(&gccdev->input->dev, "Could not create ff (skipped)");
		goto gc_deinit_controller;
	}
	input_set_capability(gccdev->input, EV_FF, FF_RUMBLE);

	error = input_register_device(gccdev->input);
	if (error)
		goto gc_deinit_controller_ff;
	gccdev->enable = true;
	return 0;

gc_deinit_controller_ff:
	input_ff_destroy(gccdev->input);
gc_deinit_controller:
	input_free_device(gccdev->input);
	return error;
}

static void gc_controller_update_work(struct work_struct *work)
{
	int i;
	u8 status[4];
	bool enable[4];
	unsigned long flags;
	struct gc_data *gdata = container_of(work, struct gc_data, work);

	for (i = 0; i < 4; i++) {
		status[i] = gdata->controllers[i].status;
		enable[i] = gc_connected_type(status[i]) != 0;
	}

	for (i = 0; i < 4; i++) {
		if (enable[i] && !gdata->controllers[i].enable) {
			if (gc_controller_init(&gdata->controllers[i], status[i]) != 0)
				enable[i] = false;
		}
	}

	spin_lock_irqsave(&gdata->idata_lock, flags);
	for (i = 0; i < 4; i++) {
		swap(gdata->controllers[i].enable, enable[i]);
	}
	spin_unlock_irqrestore(&gdata->idata_lock, flags);

	for (i = 0; i < 4; i++) {
		if (enable[i] && !gdata->controllers[i].enable) {
			input_unregister_device(gdata->controllers[i].input);
		}
	}
}

static void gc_input(struct gc_data *gdata)
{
	int i;
	unsigned long flags;
	bool updated = false;

	for (i = 0; i < 4; i++) {
		updated = updated || 
			 gdata->idata[1 + 9 * i] != gdata->controllers[i].status;
		gdata->controllers[i].status = gdata->idata[1 + 9 * i];
	}
	if (updated)
		schedule_work(&gdata->work);
	spin_lock_irqsave(&gdata->idata_lock, flags);
	for (i = 0; i < 4; i++) {
		if (gdata->controllers[i].enable) {
			gcc_input(&gdata->controllers[i], &gdata->idata[1 + 9 * i]);
		}
	}
	spin_unlock_irqrestore(&gdata->idata_lock, flags);
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
		gc_input(gdata);
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

	spin_lock_init(&gdata->idata_lock);
	INIT_WORK(&gdata->work, gc_controller_update_work);
	
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
	flush_work(&gdata->work);

	gc_deinit_input(gdata);
	gc_deinit_output(gdata);
}

static void gc_init_controllers(struct gc_data *gdata)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gdata->controllers); i++) {
		gdata->controllers[i].adapter = gdata;
		gdata->controllers[i].no = i;
		gdata->controllers[i].status = GAMECUBE_NONE;
		gdata->controllers[i].enable = false;
	}
}

static struct attribute *gc_attrs[] = {
	NULL,
};

static const struct attribute_group gc_attr_group = {
	.attrs = gc_attrs,
};

static int gc_init_attr(struct gc_data *gdata)
{
	return sysfs_create_group(&gdata->intf->dev.kobj, &gc_attr_group);
}

static void gc_deinit_attr(struct gc_data *gdata)
{
	sysfs_remove_group(&gdata->intf->dev.kobj, &gc_attr_group);
}


static int gc_usb_probe(struct usb_interface *iface, const struct usb_device_id *id)
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

	usb_make_path(udev, gdata->phys, sizeof(gdata->phys));
	strlcat(gdata->phys, "/input0", sizeof(gdata->phys));

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
	int i;
	struct gc_data *gdata = usb_get_intfdata(iface);

	for (i = 0; i < 4; i++) {
		if (gdata->controllers[i].enable) {
			input_unregister_device(gdata->controllers[i].input);
		}
	}
	gc_deinit_attr(gdata);
	gc_deinit_irq(gdata);
	usb_set_intfdata(iface, NULL);
	kfree(gdata);
	return;
}

static const struct usb_device_id gc_usb_devices[] = {
	{ USB_DEVICE(USB_VENDOR_ID_NINTENDO,
		     USB_DEVICE_ID_NINTENDO_GCADAPTER) },
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
