ifeq ($(KERNELRELEASE),)
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

install:
	install -m 0644 st7735r.ko /lib/modules/$(shell uname -r)/kernel/drivers/gpu/drm/tiny/
	rmmod st7735r || true
	modprobe st7735r
	
.PHONY: modules modules_install install clean

else
	obj-m := st7735r.o
endif