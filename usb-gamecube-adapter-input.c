#include <linux/input.h>
#include "usb-gamecube-adapter.h"

void gcc_input(struct gcc_data *gccdata, const u8 *keys)
{
   //u8 bt1 = gccdata->adapter->data[1 + 9 * gccdata->no + 1];
   //u8 bt2 = gccdata->adapter->data[1 + 9 * gccdata->no + 2];

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
static u8 connected_type(u8 status)
{
   u8 type = status & (STATE_NORMAL | STATE_WAVEBIRD);
   switch (type)
   {
      case STATE_NORMAL:
      case STATE_WAVEBIRD:
         return type;
      default:
         return 0;
   }
}

int gcc_setup_keys(struct gcc_data *gccdev)
{
	u8 status = gccdev->adapter->idata[1 + 9 * gccdev->no];
	u8 type = connected_type(status);
	bool extra_power = ((status & 0x04) != 0);

   set_bit(EV_KEY, gccdev->input->evbit);

   set_bit(BTN_NORTH, gccdev->input->keybit);
   set_bit(BTN_SOUTH, gccdev->input->keybit);
   set_bit(BTN_EAST, gccdev->input->keybit);
   set_bit(BTN_WEST, gccdev->input->keybit);
   set_bit(BTN_START, gccdev->input->keybit);
   set_bit(BTN_DPAD_UP, gccdev->input->keybit);
   set_bit(BTN_DPAD_DOWN, gccdev->input->keybit);
   set_bit(BTN_DPAD_LEFT, gccdev->input->keybit);
   set_bit(BTN_DPAD_RIGHT, gccdev->input->keybit);
   set_bit(BTN_TL, gccdev->input->keybit);
   set_bit(BTN_TR, gccdev->input->keybit);
   set_bit(BTN_TR2, gccdev->input->keybit);

   set_bit(EV_ABS, gccdev->input->evbit);

   set_bit(ABS_X, gccdev->input->absbit);
   set_bit(ABS_Y, gccdev->input->absbit);
   set_bit(ABS_RX, gccdev->input->absbit);
   set_bit(ABS_RY, gccdev->input->absbit);
   set_bit(ABS_Z, gccdev->input->absbit);
   set_bit(ABS_RZ, gccdev->input->absbit);
   

   
   if (type == GAMECUBE_WIRED && extra_power) {
    	input_set_capability(gccdev->input, EV_FF, FF_RUMBLE);
		return input_ff_create_memless(gccdev->input, NULL, gc_rumble_play);
   }
   return 0;
}