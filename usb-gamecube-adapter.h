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

#define STATE_NORMAL	0x10
#define STATE_WAVEBIRD	0x20

#define EP_IN  0x81
#define EP_OUT 0x02

#define GCC_OUT_PKT_LEN 5
#define GCC_IN_PKT_LEN 37

enum gamecube_status {
	GAMECUBE_NONE,
	GAMECUBE_WIRED = 0x10,
	GAMECUBE_WIRELESS = 0x20,
};

struct gcc_data {
	struct gc_data *adapter;
	struct input_dev *input;
	u8 no;
	u8 status;
};

struct gc_data {
	struct usb_device *udev;
	struct usb_interface *intf;

	struct urb *irq_in;
	u8 *idata;
	dma_addr_t idata_dma;
	
	struct urb *irq_out;	
	struct usb_anchor irq_out_anchor;
	bool irq_out_active;		/* we must not use an active URB */
	u8 *odata;
	u8 odata_rumbles[4];
	bool rumble_changed;		/* if rumble need update*/
	dma_addr_t odata_dma;
	spinlock_t odata_lock;		/* output data */

	struct gcc_data controllers[4];
};

/* Device attributes */
int gc_init_attr(struct gc_data *gdata);
void gc_deinit_attr(struct gc_data *gdata);

/* IRQ */
//int gc_init_irq(struct gc_data *gdata);
//void gc_deinit_irq(struct gc_data *gdata);
int gc_set_rumble_value(struct gc_data *gdata, u8 controller, u8 value);

#endif // __GAMECUBE_ADAPTER_H_FILE
