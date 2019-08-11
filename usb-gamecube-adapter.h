#ifndef __GAMECUBE_ADAPTER_H_FILE
#define __GAMECUBE_ADAPTER_H_FILE

#include <linux/device.h>
#include <linux/usb.h>

/*
 * USB_VENDOR_ID_NINTENDO Nintendo USB vendor ID
 *
 * USB_DEVICE_ID_NINTENDO_GAMECUBE_ADAPTER Gamecube adapter device ID
 */
#ifndef USB_VENDOR_ID_NINTENDO
#define USB_VENDOR_ID_NINTENDO			0x057e
#endif
#define USB_DEVICE_ID_NINTENDO_GAMECUBE_ADAPTER	0x0337

enum gamecube_status {
	GAMECUBE_NONE,
	GAMECUBE_WIRED,
	GAMECUBE_WIRELESS
};

struct ep_irq_pair {
	struct usb_endpoint_descriptor *in;
	struct usb_endpoint_descriptor *out;
};

struct gc_ep {
	size_t			len;
	u8			*data;
	dma_addr_t		dma;
	struct urb		*urb;
};

struct gc_out_ep {
	struct gc_ep		ep;
	struct usb_anchor 	anchor;
	spinlock_t		lock;
};

struct gc_data {
	struct usb_device	*udev;
	struct usb_interface	*intf;
	struct gc_ep		in;
	struct gc_out_ep	out;
	u8			rumbles[4];
	u8			data[37];
	bool			halt;
};

/* Packets */
int gc_send_rumble(struct gc_data *gdata);
int gc_send_init(struct gc_data *gdata);

/* Display */
void gc_display_state(struct gc_data *dev);

/* Device attributes */
int gc_init_attr(struct gc_data *gdata);
void gc_deinit_attr(struct gc_data *gdata);

#endif // __GAMECUBE_ADAPTER_H_FILE