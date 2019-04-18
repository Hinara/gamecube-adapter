#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "usb-gamecube-adapter.h"

/* Send and receive functions */

static int gamecube_adapter_snd(struct usb_device *udev, __u8 *buffer,
			    size_t count)
{
	__u8 *buf;
	int cnt;
	int ret;

	buf = kmemdup(buffer, count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	ret = usb_interrupt_msg(udev, usb_sndintpipe(udev, EP_OUT), buf, count, &cnt, 0);
	kfree(buf);
	return cnt;
}

static int gamecube_adapter_rcv(struct usb_device *udev, __u8 *buffer,
			    size_t count)
{
	__u8 *buf;
	int cnt;
	int ret;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	ret = usb_interrupt_msg(udev, usb_rcvintpipe(udev, EP_IN),
				buf, count, &cnt, 0);
	memcpy(buffer, buf, cnt);
	kfree(buf);
	return cnt;
}

/* Send utils functions */

static int gamecube_send_rumble(struct gamecube_data *gdata)
{
	u8 payload[5] = { 0x11, gdata->rumbles[0], gdata->rumbles[1],
			  gdata->rumbles[2], gdata->rumbles[3] };
	return gamecube_adapter_snd(gdata->udev, payload, sizeof(payload));
}

static int gamecube_send_init(struct gamecube_data *gdata)
{
	u8 payload[1] = { 0x13 };
	return gamecube_adapter_snd(gdata->udev, payload, sizeof(payload));
}

static int gamecube_adapter_handle(struct gamecube_data *gdata)
{
	u8 buf[37];
	int res;

	res = gamecube_adapter_rcv(gdata->udev, buf, sizeof(buf));
	if (res < 0) {
		return res;
	}
	if (res < 1) {
		return -EINVAL;
	}
	if (buf[0] != 0x21) {
		pr_warn("Unknown opcode %d\n", buf[0]);
	} else if (res != 37) {
		pr_warn("Invalid packet size\n");
	} else {

	}
	return res;
}

static void gamecube_worker(struct work_struct *work)
{
	struct gamecube_data *gdata = container_of(work, struct gamecube_data,
						   worker);
	int err;
	err = gamecube_send_init(gdata);
	if (err < 0) {
		pr_warn("Init Error %d", err);
		gdata->halt = 1;
	}
	while (!gdata->halt) {
		err = gamecube_adapter_handle(gdata);
		if (err < 0) {
			pr_warn("Error %d", err);
		}
	}
}

static void gamecube_schedule(struct gamecube_data *gdata)
{
	schedule_work(&gdata->worker);
}

/* Constructor and destructor of gamecube_data */

static struct gamecube_data *gamecube_create(struct usb_interface *intf)
{
	struct gamecube_data *gdata;

	gdata = kzalloc(sizeof(*gdata), GFP_KERNEL);
	if (!gdata)
		return NULL;
	gdata->udev = interface_to_usbdev(intf);
	dev_set_drvdata(&intf->dev, gdata);
	INIT_WORK(&gdata->worker, gamecube_worker);
	return gdata;
}

static void gamecube_destroy(struct gamecube_data *gdata)
{
	kfree(gdata);
}

/* Gamecube adapter rumble device file */

static ssize_t gamecube_rumble_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct gamecube_data *gdata = dev_get_drvdata(dev);
	return sprintf(buf, "%d %d %d %d\n", gdata->rumbles[0], gdata->rumbles[1], gdata->rumbles[2], gdata->rumbles[3]);
}

static ssize_t gamecube_rumble_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gamecube_data *gdata;
	int res;
	if (count != 4) {
		return -EINVAL;
	}
	gdata = dev_get_drvdata(dev);
	memcpy(gdata->rumbles, buf, 4);
	res = gamecube_send_rumble(gdata);
	if (res < 0)
		return res;
	return 4;
}

static DEVICE_ATTR(rumble, S_IRUGO | S_IWUSR | S_IWGRP, gamecube_rumble_show,
		   gamecube_rumble_store);

/* USB Driver functions */

static int gamecube_adapter_usb_probe(struct usb_interface *intf,
				      const struct usb_device_id *id)
{
	struct gamecube_data *gdata;
	int ret;

	gdata = gamecube_create(intf);
	if (!gdata) {
		dev_err(&intf->dev, "Can't alloc device\n");
		return -ENOMEM;
	}

	ret = device_create_file(&intf->dev, &dev_attr_rumble);
	if (ret) {
		dev_err(&intf->dev, "cannot create sysfs attribute\n");
		goto err;
	}
	gamecube_schedule(gdata);
	dev_info(&intf->dev, "New device registered\n");
	return 0;
err:
	kfree(gdata);
	return ret;
}

static void gamecube_adapter_usb_disconnect(struct usb_interface *intf)
{
	struct gamecube_data *gdata = dev_get_drvdata(&intf->dev);

	gdata->halt = 1;
	cancel_work_sync(&gdata->worker);
	pr_info("Adapter %d now disconnected\n",
		intf->cur_altsetting->desc.bInterfaceNumber);
	dev_info(&intf->dev, "Device removed\n");
	gamecube_destroy(gdata);
	device_remove_file(&intf->dev, &dev_attr_rumble);
}

static const struct usb_device_id gamecube_adapter_usb_devices[] = {

	{ USB_DEVICE(USB_VENDOR_ID_NINTENDO,
		USB_DEVICE_ID_NINTENDO_GAMECUBE_ADAPTER) },
	{ }
};

MODULE_DEVICE_TABLE(usb, gamecube_adapter_usb_devices);

static struct usb_driver gamecube_adapter_usb_driver = {
	.name = "gamecube_adapter",
	.id_table = gamecube_adapter_usb_devices,
	.probe = gamecube_adapter_usb_probe,
	.disconnect = gamecube_adapter_usb_disconnect
};

module_usb_driver(gamecube_adapter_usb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robin Milas <milas.robin@live.fr>");
MODULE_DESCRIPTION("Driver for Nintendo Gamecube adapter peripherals");
