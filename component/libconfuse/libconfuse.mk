LIBCONFUSE_VERSION = $(call qstrip,$(PRJ_LIBCONFUSE_VERSION))
LIBCONFUSE_SOURCE = confuse-$(LIBCONFUSE_VERSION).tar.xz
LIBCONFUSE_SITE = https://github.com/martinh/libconfuse/releases/download/v$(LIBCONFUSE_VERSION)
LIBCONFUSE_CONF_OPTS = --disable-rpath
LIBCONFUSE_LIBTOOL_PATCH = NO

$(eval $(host-autotools-component))
