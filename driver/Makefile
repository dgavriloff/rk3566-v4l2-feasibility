# SPDX-License-Identifier: GPL-2.0-only
#
# In-tree Makefile for rockchip-rkvenc
# Place in drivers/media/platform/rockchip/rkvenc/
#

obj-$(CONFIG_VIDEO_ROCKCHIP_RKVENC) += rkvenc.o
rkvenc-objs := rkvenc_drv.o rkvenc_v4l2.o rkvenc_h264.o

#
# Out-of-tree build support:
#   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
#
# When building out-of-tree without Kconfig, force module build:
ifndef CONFIG_VIDEO_ROCKCHIP_RKVENC
ifneq ($(KERNELRELEASE),)
obj-m := rkvenc.o
rkvenc-objs := rkvenc_drv.o rkvenc_v4l2.o rkvenc_h264.o
endif
endif

# Standalone build target (invoked without KERNELRELEASE)
ifeq ($(KERNELRELEASE),)
KDIR ?= /lib/modules/$(shell uname -r)/build

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: modules modules_install clean
endif
