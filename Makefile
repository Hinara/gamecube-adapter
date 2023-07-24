
CONFIG_HID_GAMECUBE_CONTROLLER?=m
obj-$(CONFIG_HID_GAMECUBE_CONTROLLER)	+= usb-gamecube-adapter.o
usb-gamecube-adapter-y:= \
	usb-gamecube-adapter-attr.o \
	usb-gamecube-adapter-packet.o \
	usb-gamecube-adapter-setup.o \
	usb-gamecube-adapter-endpoints.o \

KDIR?=/lib/modules/$(shell uname -r)/build

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
help:
	$(MAKE) -C $(KDIR) M=$(PWD) help
sign:
	$(mod_sign_cmd) $('')

.PHONY: modules modules_install clean help sign
