obj-m +=	usb-gamecube-adapter.o

usb-gamecube-adapter-objs:= \
	usb-gamecube-adapter-attr.o \
	usb-gamecube-adapter-packet.o \
	usb-gamecube-adapter-setup.o \

modules = $(obj-m:.o=.ko)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
up:
	sudo insmod $(modules)
down:
	sudo rmmod $(modules)

du:
	make down
	make up

.PHONY: all clean up down
