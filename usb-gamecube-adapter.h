#ifndef __HID_GAMECUBE_ADAPTER_H_FILE
#define __HID_GAMECUBE_ADAPTER_H_FILE

#include <linux/device.h>
#include <linux/usb.h>

/*
 * EP_OUT Output USB pipe of the gamecube adapter
 *
 * EP_IN Input USB pipe of the gamecube adapter
 */
#define EP_OUT 0x02
#define EP_IN 0x81

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

struct gamecube_data {
	struct usb_device	*udev;
	u8			rumbles[4];
	struct work_struct	worker;
	bool			halt;
};

#endif