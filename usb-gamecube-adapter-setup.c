#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "usb-gamecube-adapter.h"

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

int gc_usb_probe(struct usb_interface *iface,
		 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(iface);
	struct gc_data *dev;
	int error;

	dev = kzalloc(sizeof(struct gc_data), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	usb_set_intfdata(iface, dev);
	dev->udev = udev;
	dev->intf = iface;
	error = gc_init_endpoints(dev);
	if (error)
		goto err_free_devs;
	error = gc_init_attr(dev);
	if (error)
		goto err_deinit_endpoints;
	error = gc_adapter_start_input(dev);
	if (error)
		goto err_deinit_attrs;
	dev_info(&iface->dev, "New device registered\n");
	return 0;
err_deinit_attrs:
	gc_deinit_attr(dev);
err_deinit_endpoints:
	gc_deinit_endpoints(dev);
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
	gc_deinit_endpoints(dev);
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
