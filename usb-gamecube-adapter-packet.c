#include "usb-gamecube-adapter.h"

static int gc_send(struct gc_data *dev, void *buf, size_t len)
{
	u8 *packet = dev->out.ep.data;
	unsigned long flags;
	int error;

	if (len > dev->out.ep.len)
		return -EINVAL;
	spin_lock_irqsave(&dev->out.lock, flags);
	memcpy(packet, buf, len);
	dev->out.ep.urb->transfer_buffer_length = len;

	usb_anchor_urb(dev->out.ep.urb, &dev->out.anchor);
	error = usb_submit_urb(dev->out.ep.urb, GFP_ATOMIC);
	if (error) {
		usb_unanchor_urb(dev->out.ep.urb);
		error = -EIO;
	}
	spin_unlock_irqrestore(&dev->out.lock, flags);
	return error;
}

int gc_send_rumble(struct gc_data *gdata)
{
	u8 payload[5] = { 0x11, gdata->rumbles[0], gdata->rumbles[1],
			  gdata->rumbles[2], gdata->rumbles[3] };
	return gc_send(gdata, payload, sizeof(payload));
}

int gc_send_init(struct gc_data *gdata)
{
	u8 payload[] = { 0x13 };
	return gc_send(gdata, payload, sizeof(payload));
}
