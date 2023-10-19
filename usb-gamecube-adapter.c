#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include "usb-gamecube-adapter.h"

static void gc_hid_send_rumble(struct work_struct *work)
{
	struct gc_data *gdata = container_of(work, struct gc_data, rumble_work);
	unsigned long flags;

	if (!gdata->hdev->ll_driver->output_report)
		return;

	gdata->rumbles_buffer[0] = 0x11;
	spin_unlock_irqrestore(&gdata->rumble_lock, flags);
	memcpy(gdata->rumbles_buffer + 1, gdata->rumbles, GCC_OUT_PKT_LEN - 1);
	gdata->rumble_changed = 0;
	spin_unlock_irqrestore(&gdata->rumble_lock, flags);

	hid_hw_output_report(gdata->hdev, gdata->rumbles_buffer, GCC_OUT_PKT_LEN);
}

static int gc_set_rumble_value(struct gc_data *gdata, u8 controller, u8 value)
{
	unsigned long flags;
	int error = 0;

	value = !!value;
	if (controller > 4)
		return -EINVAL;

	spin_lock_irqsave(&gdata->rumble_lock, flags);
	if (gdata->rumbles[controller] != value) {
		gdata->rumbles[controller] = value;
		gdata->rumble_changed = true;
		schedule_work(&gdata->rumble_work);
	}
	spin_unlock_irqrestore(&gdata->rumble_lock, flags);
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
	gccdev->input->dev.parent = &gccdev->adapter->hdev->dev;
	gccdev->input->id.bustype = gccdev->adapter->hdev->bus;
	gccdev->input->id.vendor = gccdev->adapter->hdev->vendor;
	gccdev->input->id.product = gccdev->adapter->hdev->product;
	gccdev->input->id.version = gccdev->adapter->hdev->version;
	gccdev->input->name = GCADAPTER_NAME;

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
	struct gc_data *gdata = container_of(work, struct gc_data, update_work);

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

	spin_lock_irqsave(&gdata->update_lock, flags);
	for (i = 0; i < 4; i++) {
		swap(gdata->controllers[i].enable, enable[i]);
	}
	spin_unlock_irqrestore(&gdata->update_lock, flags);

	for (i = 0; i < 4; i++) {
		if (enable[i] && !gdata->controllers[i].enable) {
			input_unregister_device(gdata->controllers[i].input);
		}
	}
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

static void gc_input(struct gc_data *gdata, u8 *raw_data)
{
	int i;
	unsigned long flags;
	bool updated = false;

	for (i = 0; i < 4; i++) {
		updated = updated || 
			 raw_data[1 + 9 * i] != gdata->controllers[i].status;
		gdata->controllers[i].status = raw_data[1 + 9 * i];
	}
	if (updated)
		schedule_work(&gdata->update_work);
	spin_lock_irqsave(&gdata->update_lock, flags);
	for (i = 0; i < 4; i++) {
		if (gdata->controllers[i].enable) {
			gcc_input(&gdata->controllers[i], &raw_data[1 + 9 * i]);
		}
	}
	spin_unlock_irqrestore(&gdata->update_lock, flags);
}

static int gc_hid_event(struct hid_device *hdev, struct hid_report *report,
							u8 *raw_data, int size)
{
	struct gc_data *gdata = hid_get_drvdata(hdev);

	if (size != GCC_IN_PKT_LEN) {
		hid_warn(hdev, "Bad sized packet\n");
		return -EINVAL;
	} else if (raw_data[0] != 0x21) {
		hid_warn(hdev, "Unknown opcode %d\n", raw_data[0]);
		return -EINVAL;
	} else {
		gc_input(gdata, raw_data);
	}
	return 0;
}

static void gc_destroy_adapter(struct hid_device *hdev)

{
	struct gc_data *gdata = hid_get_drvdata(hdev);
	int i = 0;

	flush_work(&gdata->rumble_work);
	flush_work(&gdata->update_work);
	for (i = 0; i < 4; i++) {
		if (gdata->controllers[i].enable) {
			input_unregister_device(gdata->controllers[i].input);
		}
	}
	kfree(gdata);
}

static struct gc_data *gc_init_adapter(struct hid_device *hdev)
{
	struct gc_data *gdata;
	int i;

	gdata = kzalloc(sizeof(struct gc_data), GFP_KERNEL);
	if (!gdata)
		return NULL;
	gdata->hdev = hdev;

	gdata->rumbles_buffer = kzalloc(sizeof(GCC_OUT_PKT_LEN), GFP_KERNEL);
	if (!gdata->rumbles_buffer) {
		kfree(gdata);
		return NULL;
	}

	hid_set_drvdata(hdev, gdata);

	for (i = 0; i < ARRAY_SIZE(gdata->controllers); i++) {
		gdata->controllers[i].adapter = gdata;
		gdata->controllers[i].no = i;
		gdata->controllers[i].status = GAMECUBE_NONE;
		gdata->controllers[i].enable = false;
	}

	spin_lock_init(&gdata->update_lock);
	INIT_WORK(&gdata->update_work, gc_controller_update_work);

	spin_lock_init(&gdata->rumble_lock);
	INIT_WORK(&gdata->rumble_work, gc_hid_send_rumble);
	return gdata;
}

static int gc_hid_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	int ret;
	struct gc_data *gdata;

	gdata = gc_init_adapter(hdev);
	if (!gdata)
		return ret;
	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "HID parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DRIVER);
	if (ret) {
		hid_err(hdev, "HW start failed\n");
		goto err;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "cannot start hardware I/O\n");
		goto err_stop;
	}

	gdata->rumbles_buffer[0] = 0x13;
	ret = hid_hw_output_report(gdata->hdev, gdata->rumbles_buffer, 1);
	hid_err(hdev, "WHAT %d\n", gdata->hdev->ll_driver->max_buffer_size);
	hid_err(hdev, "WHAT %lx\n", gdata->hdev->ll_driver->output_report);
	if (ret) {
		hid_err(hdev, "cannot sent init packet\n");
		goto err_close;
	}
	hid_info(hdev, "New device registered\n");

err_close:
	hid_hw_close(hdev);
err_stop:
	hid_hw_stop(hdev);
err:
	gc_destroy_adapter(hdev);
	return ret;
}

static void gc_hid_remove(struct hid_device *hdev)
{
	gc_destroy_adapter(hdev);
	return;
}

static const struct hid_device_id gc_hid_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_GCADAPTER) },
	{ }
};

MODULE_DEVICE_TABLE(hid, gc_hid_devices);

static struct hid_driver gc_hid_driver = {
	.name = "gamecube_adapter",
	.id_table = gc_hid_devices,
	.probe = gc_hid_probe,
	.remove	= gc_hid_remove,
	.raw_event = gc_hid_event,
};

module_hid_driver(gc_hid_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robin Milas <milas.robin@live.fr>");
MODULE_DESCRIPTION("Driver for GameCube adapter");
