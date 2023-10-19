#ifndef __GAMECUBE_ADAPTER_H_FILE
#define __GAMECUBE_ADAPTER_H_FILE

#include <linux/device.h>
#include <linux/hid.h>

/*
 * USB_VENDOR_ID_NINTENDO Nintendo USB vendor ID
 *
 * USB_DEVICE_ID_NINTENDO_GCADAPTER Gamecube adapter device ID
 */
#ifndef USB_VENDOR_ID_NINTENDO
#define USB_VENDOR_ID_NINTENDO			0x057e
#endif
#define USB_DEVICE_ID_NINTENDO_GCADAPTER	0x0337

#define GCADAPTER_NAME "Nintendo GameCube Controller Adapter"

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
	bool enable;
};

struct gc_data {
	struct hid_device *hdev;

	/* Status */
	struct gcc_data controllers[4];
	spinlock_t update_lock;
	struct work_struct update_work;	/* send rumble packets */

	/* Rumbles */
	u8 rumbles[4];
	bool rumble_changed;
	u8 *rumbles_buffer;
	spinlock_t rumble_lock;
	struct work_struct rumble_work;	/* create/delete controller input files */
};

#endif // __GAMECUBE_ADAPTER_H_FILE
