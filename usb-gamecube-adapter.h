#ifndef __GAMECUBE_ADAPTER_H_FILE
#define __GAMECUBE_ADAPTER_H_FILE

#include <linux/device.h>
#include <linux/usb.h>
#include <linux/input.h>

/*
 * USB_VENDOR_ID_NINTENDO Nintendo USB vendor ID
 *
 * USB_DEVICE_ID_NINTENDO_GAMECUBE_ADAPTER Gamecube adapter device ID
 */
#ifndef USB_VENDOR_ID_NINTENDO
#define USB_VENDOR_ID_NINTENDO			0x057e
#endif
#define USB_DEVICE_ID_NINTENDO_GAMECUBE_ADAPTER	0x0337

#define STATE_NONE	0x00
#define STATE_NORMAL	0x10
#define STATE_WAVEBIRD	0x20

enum gc_keys {
   GAMECUBE_KEY_START,
   GAMECUBE_KEY_TR2,
   GAMECUBE_KEY_TR,
   GAMECUBE_KEY_TL,
   GAMECUBE_KEY_SOUTH,
   GAMECUBE_KEY_WEST,
   GAMECUBE_KEY_EAST,
   GAMECUBE_KEY_NORTH,
   GAMECUBE_KEY_DPAD_LEFT,
   GAMECUBE_KEY_DPAD_RIGHT,
   GAMECUBE_KEY_DPAD_DOWN,
   GAMECUBE_KEY_DPAD_UP,
   GAMECUBE_KEY_COUNT
};


struct gc_ep {
	size_t		len;
	u8		*data;
	dma_addr_t	dma;
	struct urb	*urb;
};

struct gc_out_ep {
	struct gc_ep		ep;
	struct usb_anchor	anchor;
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
	struct input_dev	*input;
};

/* Packets */
int gc_send_rumble(struct gc_data *gdata);
int gc_send_init(struct gc_data *gdata);

/* Display */
void gc_display_state(struct gc_data *dev);

/* Device attributes */
int gc_init_attr(struct gc_data *gdata);
void gc_deinit_attr(struct gc_data *gdata);

/* Endpoints */
int gc_init_endpoints(struct gc_data *dev);
void gc_deinit_endpoints(struct gc_data *dev);

#endif // __GAMECUBE_ADAPTER_H_FILE
