BUILDROOT_VERSION = 2021.02.1
BUILDROOT_SOURCE = buildroot-$(BUILDROOT_VERSION).tar.bz2
BUILDROOT_SITE = https://buildroot.org/downloads

$(eval $(generic-component))
