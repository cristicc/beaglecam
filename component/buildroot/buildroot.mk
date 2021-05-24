BUILDROOT_VERSION = $(call qstrip,$(PRJ_BUILDROOT_VERSION))
BUILDROOT_SOURCE = buildroot-$(BUILDROOT_VERSION).tar.bz2
BUILDROOT_SITE = https://buildroot.org/downloads

# Toolchain is not necessary since we are not building anything here.
BUILDROOT_ADD_TOOLCHAIN_DEPENDENCY = NO

$(eval $(generic-component))
