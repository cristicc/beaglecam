UBOOT_VERSION = $(call qstrip,$(PRJ_UBOOT_VERSION))
UBOOT_SITE = https://ftp.denx.de/pub/u-boot
UBOOT_SOURCE = u-boot-$(UBOOT_VERSION).tar.bz2

# Call 'make all' unconditionally
UBOOT_MAKE_TARGET += all

UBOOT_MAKE_TARGET += $(call qstrip,$(PRJ_UBOOT_IMAGE_NAME))
UBOOT_BINS += $(PRJ_UBOOT_IMAGE_NAME)

# The kernel calls AArch64 'arm64', but U-Boot calls it just 'arm', so
# we have to special case it. Similar for i386/x86_64 -> x86
ifeq ($(KERNEL_ARCH),arm64)
UBOOT_ARCH = arm
else ifneq ($(filter $(KERNEL_ARCH),i386 x86_64),)
UBOOT_ARCH = x86
else
UBOOT_ARCH = $(KERNEL_ARCH)
endif

UBOOT_MAKE_OPTS += \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	ARCH=$(UBOOT_ARCH) \
	$(call qstrip,$(PRJ_UBOOT_CUSTOM_MAKEOPTS))

# Fixup inclusion of libfdt headers, which can fail in older u-boot versions
# when libfdt-devel is installed system-wide.
# The core change is equivalent to upstream commit
# e0d20dc1521e74b82dbd69be53a048847798a90a (first in v2018.03). However, the fixup
# is complicated by the fact that the underlying u-boot code changed multiple
# times in history:
# - The directory scripts/dtc/libfdt only exists since upstream commit
#   c0e032e0090d6541549b19cc47e06ccd1f302893 (first in v2017.11). For earlier
#   versions, create a dummy scripts/dtc/libfdt directory with symlinks for the
#   fdt-related files. This allows to use the same -I<path> option for both
#   cases.
# - The variable 'srctree' used to be called 'SRCTREE' before upstream commit
#   01286329b27b27eaeda045b469d41b1d9fce545a (first in v2014.04).
# - The original location for libfdt, 'lib/libfdt/', used to be simply
#   'libfdt' before upstream commit 0de71d507157c4bd4fddcd3a419140d2b986eed2
#   (first in v2010.06). Make the 'lib' part optional in the substitution to
#   handle this.
define UBOOT_FIXUP_LIBFDT_INCLUDE
	$(Q)if [ ! -d $(@D)/scripts/dtc/libfdt ]; then \
		mkdir -p $(@D)/scripts/dtc/libfdt; \
		cd $(@D)/scripts/dtc/libfdt; \
		ln -s ../../../include/fdt.h .; \
		ln -s ../../../include/libfdt*.h .; \
		ln -s ../../../lib/libfdt/libfdt_internal.h .; \
	fi
	$(Q)$(SED) \
		's%-I\ *\$$(srctree)/lib/libfdt%-I$$(srctree)/scripts/dtc/libfdt%; \
		s%-I\ *\$$(SRCTREE)\(/lib\)\?/libfdt%-I$$(SRCTREE)/scripts/dtc/libfdt%' \
		$(@D)/tools/Makefile
endef
UBOOT_POST_PATCH_HOOKS += UBOOT_FIXUP_LIBFDT_INCLUDE

UBOOT_KCONFIG_DEFCONFIG = $(call qstrip,$(PRJ_UBOOT_DEFCONFIG))
UBOOT_KCONFIG_FILE = $(call qstrip,$(PRJ_UBOOT_CUSTOM_CONFIG_FILE))

UBOOT_KCONFIG_FRAGMENT_FILES = $(call qstrip,$(PRJ_UBOOT_CONFIG_FRAGMENT_FILES))
UBOOT_KCONFIG_EDITORS = menuconfig

UBOOT_KCONFIG_OPTS = $(UBOOT_MAKE_OPTS)

UBOOT_CUSTOM_DTS_PATH = $(call qstrip,$(PRJ_UBOOT_CUSTOM_DTS_PATH))

define UBOOT_BUILD_CMDS
	$(if $(UBOOT_CUSTOM_DTS_PATH),
		cp -f $(UBOOT_CUSTOM_DTS_PATH) $(@D)/arch/$(UBOOT_ARCH)/dts/
	)
	$(TARGET_CONFIGURE_OPTS) \
		$(MAKE) -C $(@D) $(UBOOT_MAKE_OPTS) \
		$(UBOOT_MAKE_TARGET)
endef

define UBOOT_INSTALL_CMDS
	$(foreach f,$(UBOOT_BINS), \
		cp -dpf $(@D)/$(f) $(BINARIES_DIR)/
	)
	$(if $(PRJ_UBOOT_SPL_NAME),
		$(foreach f,$(call qstrip,$(PRJ_UBOOT_SPL_NAME)), \
			cp -dpf $(@D)/$(f) $(BINARIES_DIR)/
		)
	)
	$(if $(PRJ_UBOOT_UENV_PATH),
		cp -dpf $(call qstrip,$(PRJ_UBOOT_UENV_PATH)) $(BINARIES_DIR)/
	)
endef

$(eval $(kconfig-component))
