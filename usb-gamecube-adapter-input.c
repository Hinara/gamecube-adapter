#include "usb-gamecube-adapter.h"

static const u16 gc_keys_map[] = {
	BTN_START,
	BTN_TR2,
	BTN_TR,
	BTN_TL,
	BTN_SOUTH,
	BTN_WEST,
	BTN_EAST,
	BTN_NORTH,
	BTN_DPAD_LEFT,
	BTN_DPAD_RIGHT,
	BTN_DPAD_DOWN,
	BTN_DPAD_UP,
};


static int gc_alloc_input(struct gc_data *gc_data)
{
	int i, error;

	gc_data->input = input_allocate_device();
	if (!gc_data->input)
		return -ENOMEM;
	set_bit(EV_KEY, gc_data->input->evbit);
	for (i = 0; i < GAMECUBE_KEY_COUNT; ++i)
		set_bit(gc_keys_map[i], gc_data->input->keybit);
	error = input_register_device(gc_data->input);
	if (error)
		goto err_register_device;
	return 0;
err_register_device:
	input_free_device(gc_data->input);
	return error;
}
