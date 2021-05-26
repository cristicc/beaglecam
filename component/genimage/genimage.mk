GENIMAGE_VERSION = $(call qstrip,$(PRJ_GENIMAGE_VERSION))
GENIMAGE_SOURCE = genimage-$(GENIMAGE_VERSION).tar.xz
GENIMAGE_SITE = https://github.com/pengutronix/genimage/releases/download/v$(GENIMAGE_VERSION)
GENIMAGE_DEPENDENCIES = libconfuse

$(eval $(host-autotools-component))
