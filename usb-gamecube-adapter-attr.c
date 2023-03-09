#include "usb-gamecube-adapter.h"

/* Gamecube adapter rumble device file */

static ssize_t gc_rumble_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct gc_data *gdata = dev_get_drvdata(dev);
	if (!gdata)
		return -EFAULT;
	return sprintf(buf, "%d %d %d %d\n", gdata->rumbles[0],
		       gdata->rumbles[1], gdata->rumbles[2], gdata->rumbles[3]);
}

static ssize_t gc_rumble_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct gc_data *gdata;
	int res;
	if (count != sizeof(gdata->rumbles)) {
		return -EINVAL;
	}
	gdata = dev_get_drvdata(dev);
	if (!gdata)
		return -EFAULT;
	memcpy(gdata->rumbles, buf, sizeof(gdata->rumbles));
	res = gc_send_rumble(gdata);
	if (res < 0)
		return res;
	return 4;
}

static DEVICE_ATTR(rumble, S_IRUGO | S_IWUSR | S_IWGRP, gc_rumble_show,
		   gc_rumble_store);

/* Gamecube controller status attribute files */

static const char *gc_none_name = "Not connected";
static const char *gc_wired_name = "Nintendo GameCube Controller";
static const char *gc_wireless_name = "Nintendo GameCube Wavebird Controller";
static const char *gc_unknown_name = "Unknown Controller";

static const char *gc_getname(u8 status)
{
	switch (status & (STATE_NORMAL | STATE_WAVEBIRD)) {
	case STATE_NONE:
		return gc_none_name;
	case STATE_NORMAL:
		return gc_wired_name;
	case STATE_WAVEBIRD:
		return gc_wireless_name;
	default:
		return gc_unknown_name;
	}
}

static ssize_t gc_show_status_g(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gc_data *gdata = dev_get_drvdata(dev);
	if (!gdata)
		return -EFAULT;
	return sprintf(buf, "Port 1: %s\nPort 2: %s\nPort 3: %s\nPort 4: %s\n",
		gc_getname(gdata->data[1 + 9 * 0]),
		gc_getname(gdata->data[1 + 9 * 1]),
		gc_getname(gdata->data[1 + 9 * 2]),
		gc_getname(gdata->data[1 + 9 * 3]));
}

static DEVICE_ATTR(status, S_IRUGO, gc_show_status_g, NULL);


/* Init and deinit of attributes files */

static struct attribute *gc_attrs[] = {
	&dev_attr_rumble.attr,
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group gc_attr_group = {
	.attrs = gc_attrs,
};

int gc_init_attr(struct gc_data *gdata)
{
	return sysfs_create_group(&gdata->intf->dev.kobj, &gc_attr_group);
}

void gc_deinit_attr(struct gc_data *gdata)
{
	sysfs_remove_group(&gdata->intf->dev.kobj, &gc_attr_group);
}
