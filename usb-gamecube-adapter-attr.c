#include "usb-gamecube-adapter.h"

/* Gamecube adapter rumble device file */

static ssize_t gc_rumble_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct gc_data *gdata = dev_get_drvdata(dev);
	if (!gdata)
		return -EFAULT;
	return sprintf(buf, "%d %d %d %d\n",
		       gdata->rumbles[0], gdata->rumbles[1],
		       gdata->rumbles[2], gdata->rumbles[3]);
}

static ssize_t gc_rumble_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct gc_data *gdata;
	int res;
	if (count != 4)
	{
		return -EINVAL;
	}
	gdata = dev_get_drvdata(dev);
	if (!gdata)
		return -EFAULT;
	memcpy(gdata->rumbles, buf, 4);
	res = gc_send_rumble(gdata);
	if (res < 0)
		return res;
	return 4;
}

static DEVICE_ATTR(rumble, S_IRUGO | S_IWUSR | S_IWGRP, gc_rumble_show,
		   gc_rumble_store);

/* Gamecube controller status attribute files */

#define STATE_NORMAL 0x10
#define STATE_WAVEBIRD 0x20

static const char *str_connected = "Connected\n";
static const char *str_wr_connected = "Connected (Wireless)\n";
static const char *str_disconnected = "Disconnected\n";

static ssize_t gc_status(u8 status, char *buf)
{
	switch (status & (STATE_NORMAL | STATE_WAVEBIRD))
	{
	case STATE_NORMAL:
		memcpy(buf, str_connected, strlen(str_connected));
		return strlen(str_connected);
	case STATE_WAVEBIRD:
		memcpy(buf, str_wr_connected, strlen(str_wr_connected));
		return strlen(str_wr_connected);
	default:
		memcpy(buf, str_disconnected, strlen(str_disconnected));
		return strlen(str_disconnected);
	}
}

static ssize_t gc_show_status(struct device *dev,
			      struct device_attribute *attr,
			      char *buf, int controller_no)
{
	struct gc_data *gdata = dev_get_drvdata(dev);
	if (!gdata)
		return -EFAULT;
	return gc_status(gdata->data[1 + 9 * controller_no], buf);
}

static ssize_t gc_show_status1(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return gc_show_status(dev, attr, buf, 0);
}

static DEVICE_ATTR(status1, S_IRUGO, gc_show_status1, NULL);

static ssize_t gc_show_status2(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return gc_show_status(dev, attr, buf, 1);
}

static DEVICE_ATTR(status2, S_IRUGO, gc_show_status2, NULL);

static ssize_t gc_show_status3(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return gc_show_status(dev, attr, buf, 2);
}

static DEVICE_ATTR(status3, S_IRUGO, gc_show_status3, NULL);

static ssize_t gc_show_status4(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return gc_show_status(dev, attr, buf, 3);
}

static DEVICE_ATTR(status4, S_IRUGO, gc_show_status4, NULL);

/* Init and deinit of attributes files */

static struct attribute *gc_attrs[] = {
    &dev_attr_rumble.attr,
    &dev_attr_status1.attr,
    &dev_attr_status2.attr,
    &dev_attr_status3.attr,
    &dev_attr_status4.attr,
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