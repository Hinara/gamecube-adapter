obj-m +=	usb-gamecube-adapter.o

modules = $(obj-m:.o=.ko)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
up:
	sudo insmod $(modules)
down:
	sudo rmmod $(modules)

.PHONY: all clean up down
