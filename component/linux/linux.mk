LINUX_VERSION = $(call qstrip,$(PRJ_LINUX_KERNEL_VERSION))
LINUX_SITE = https://cdn.kernel.org/pub/linux/kernel/v$(firstword $(subst ., ,$(LINUX_VERSION))).x
LINUX_SOURCE = linux-$(LINUX_VERSION).tar.xz

# Starting with 4.18, the kconfig in the kernel calls the
# cross-compiler to check its capabilities. So we need the
# toolchain before we can call the configurators.
LINUX_KCONFIG_DEPENDENCIES += toolchain

LINUX_KERNEL_APPENDED_INITRAMFS = $(call qstrip,$(PRJ_LINUX_KERNEL_APPENDED_INITRAMFS))

LINUX_MAKE_ENV = \
	PRJ_LINUX_KERNEL_APPENDED_INITRAMFS=$(LINUX_KERNEL_APPENDED_INITRAMFS)

# We don't want to run depmod after installing the kernel. It's done in a
# target-finalize hook, to encompass modules installed by packages.
LINUX_MAKE_FLAGS = \
	ARCH=$(KERNEL_ARCH) \
	INSTALL_MOD_PATH=$(BINARIES_DIR) \
	CROSS_COMPILE="$(TARGET_CROSS)"

# gcc-8 started warning about function aliases that have a
# non-matching prototype.  This seems rather useful in general, but it
# causes tons of warnings in the Linux kernel, where we rely on
# abusing those aliases for system call entry points, in order to
# sanitize the arguments passed from user space in registers.
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82435
LINUX_MAKE_ENV += KCFLAGS=-Wno-attribute-alias

# Get the real Linux version, which tells us where kernel modules are
# going to be installed in the target filesystem.
# Filter out 'w' from MAKEFLAGS, to workaround a bug in make 4.1 (#13141)
LINUX_VERSION_PROBED = `MAKEFLAGS='$(filter-out w,$(MAKEFLAGS))' $(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) --no-print-directory -s kernelrelease 2>/dev/null`

LINUX_DTS_NAME += $(call qstrip,$(PRJ_LINUX_KERNEL_INTREE_DTS_NAME))

# We keep only the .dts files, so that the user can specify both .dts
# and .dtsi files in BR2_LINUX_KERNEL_CUSTOM_DTS_PATH. Both will be
# copied to arch/<arch>/boot/dts, but only the .dts files will
# actually be generated as .dtb.
LINUX_DTS_NAME += $(basename $(filter %.dts,$(notdir $(call qstrip,$(PRJ_LINUX_KERNEL_CUSTOM_DTS_PATH)))))

LINUX_DTBS = $(addsuffix .dtb,$(LINUX_DTS_NAME))

LINUX_IMAGE_NAME = $(call qstrip,$(PRJ_LINUX_IMAGE_NAME))
LINUX_TARGET_NAME = $(LINUX_IMAGE_NAME)

LINUX_IMAGE_NAME_EXTRA = $(call qstrip,$(PRJ_LINUX_IMAGE_NAME_EXTRA))
LINUX_TARGET_NAME += $(LINUX_IMAGE_NAME_EXTRA)

LINUX_KERNEL_UIMAGE_LOADADDR = $(call qstrip,$(PRJ_LINUX_KERNEL_UIMAGE_LOADADDR))
ifneq ($(LINUX_IMAGE_NAME),uImage)
  ifeq ($(PRJ_LINUX_KERNEL_APPENDED_UIMAGE),)
  LINUX_KERNEL_UIMAGE_LOADADDR =
  endif
endif
ifneq ($(LINUX_KERNEL_UIMAGE_LOADADDR),)
LINUX_MAKE_FLAGS += LOADADDR="$(LINUX_KERNEL_UIMAGE_LOADADDR)"
endif

# Compute the arch path, since i386 and x86_64 are in arch/x86 and not
# in arch/$(KERNEL_ARCH). Even if the kernel creates symbolic links
# for bzImage, arch/i386 and arch/x86_64 do not exist when copying the
# defconfig file.
ifeq ($(KERNEL_ARCH),i386)
LINUX_ARCH_PATH = $(LINUX_DIR)/arch/x86
else ifeq ($(KERNEL_ARCH),x86_64)
LINUX_ARCH_PATH = $(LINUX_DIR)/arch/x86
else
LINUX_ARCH_PATH = $(LINUX_DIR)/arch/$(KERNEL_ARCH)
endif

LINUX_IMAGE_PATH = $(LINUX_ARCH_PATH)/boot/$(LINUX_IMAGE_NAME)

LINUX_KCONFIG_DEFCONFIG = $(call qstrip,$(PRJ_LINUX_KERNEL_DEFCONFIG))
LINUX_KCONFIG_FILE = $(call qstrip,$(PRJ_LINUX_KERNEL_CUSTOM_CONFIG_FILE))

LINUX_KCONFIG_FRAGMENT_FILES = $(call qstrip,$(PRJ_LINUX_KERNEL_CONFIG_FRAGMENT_FILES))
LINUX_KCONFIG_EDITORS = menuconfig

LINUX_KCONFIG_OPTS = $(LINUX_MAKE_FLAGS)

# If no package has yet set it, set it from the Kconfig option
LINUX_NEEDS_MODULES ?= $(PRJ_LINUX_NEEDS_MODULES)

define LINUX_KCONFIG_FIXUP_CMDS
	$(if $(LINUX_NEEDS_MODULES),
		$(call KCONFIG_ENABLE_OPT,CONFIG_MODULES))
	# As the kernel gets compiled before root filesystems are built, we create a
	# fake cpio file. It'll be replaced later by the real cpio archive, and the
	# kernel will be rebuilt using the linux-rebuild-with-initramfs target.
	$(if $(LINUX_KERNEL_APPENDED_INITRAMFS),
		mkdir -p $(dir $(LINUX_KERNEL_APPENDED_INITRAMFS))
		touch $(LINUX_KERNEL_APPENDED_INITRAMFS)
		$(call KCONFIG_SET_OPT,CONFIG_INITRAMFS_SOURCE,"$${PRJ_LINUX_KERNEL_APPENDED_INITRAMFS}")
		$(call KCONFIG_SET_OPT,CONFIG_INITRAMFS_ROOT_UID,0)
		$(call KCONFIG_SET_OPT,CONFIG_INITRAMFS_ROOT_GID,0))
	$(call KCONFIG_DISABLE_OPT,CONFIG_GCC_PLUGINS)
	$(PACKAGES_LINUX_CONFIG_FIXUPS)
endef

ifneq ($(LINUX_DTS_NAME),)
define LINUX_BUILD_DTB
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(@D) $(LINUX_DTBS)
endef
ifeq ($(PRJ_LINUX_KERNEL_APPENDED_DTB),)
define LINUX_INSTALL_DTB
	# dtbs moved from arch/<ARCH>/boot to arch/<ARCH>/boot/dts since 3.8-rc1
	$(foreach dtb,$(LINUX_DTBS), \
		$(INSTALL) -m 0644 -D \
			$(or $(wildcard $(LINUX_ARCH_PATH)/boot/dts/$(dtb)),$(LINUX_ARCH_PATH)/boot/$(dtb)) \
			$(1)/$(if $(PRJ_LINUX_KERNEL_DTB_KEEP_DIRNAME),$(dtb),$(notdir $(dtb)))
	)
endef
endif
endif

ifeq ($(PRJ_LINUX_KERNEL_APPENDED_DTB),y)
# dtbs moved from arch/$ARCH/boot to arch/$ARCH/boot/dts since 3.8-rc1
define LINUX_APPEND_DTB
	(cd $(LINUX_ARCH_PATH)/boot; \
		for dtb in $(LINUX_DTS_NAME); do \
			if test -e $${dtb}.dtb ; then \
				dtbpath=$${dtb}.dtb ; \
			else \
				dtbpath=dts/$${dtb}.dtb ; \
			fi ; \
			cat zImage $${dtbpath} > zImage.$${dtb} || exit 1; \
		done)
endef
ifeq ($(PRJ_LINUX_KERNEL_APPENDED_UIMAGE),y)
# We need to generate a new u-boot image that takes into
# account the extra-size added by the device tree at the end
# of the image. To do so, we first need to retrieve both load
# address and entry point for the kernel from the already
# generate uboot image before using mkimage -l.
LINUX_APPEND_DTB += ; \
	MKIMAGE_ARGS=`$(MKIMAGE) -l $(LINUX_IMAGE_PATH) |\
	sed -n -e 's/Image Name:[ ]*\(.*\)/-n \1/p' -e 's/Load Address:/-a/p' -e 's/Entry Point:/-e/p'`; \
	for dtb in $(LINUX_DTS_NAME); do \
		$(MKIMAGE) -A $(MKIMAGE_ARCH) -O linux \
			-T kernel -C none $${MKIMAGE_ARGS} \
			-d $(LINUX_ARCH_PATH)/boot/zImage.$${dtb} $(LINUX_IMAGE_PATH).$${dtb}; \
	done
endif
endif

# Compilation. We make sure the kernel gets rebuilt when the
# configuration has changed. We call the 'all' and
# '$(LINUX_TARGET_NAME)' targets separately because calling them in
# the same $(MAKE) invocation has shown to cause parallel build
# issues.
# The call to disable gcc-plugins is a stop-gap measure:
#   http://lists.busybox.net/pipermail/buildroot/2020-May/282727.html
define LINUX_BUILD_CMDS
	$(call KCONFIG_DISABLE_OPT,CONFIG_GCC_PLUGINS)
	$(foreach dts,$(call qstrip,$(PRJ_LINUX_KERNEL_CUSTOM_DTS_PATH)), \
		cp -f $(dts) $(LINUX_ARCH_PATH)/boot/dts/
	)
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(@D) all
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(@D) $(LINUX_TARGET_NAME)
	$(LINUX_BUILD_DTB)
	$(LINUX_APPEND_DTB)
endef

ifeq ($(PRJ_LINUX_KERNEL_APPENDED_DTB),y)
# When a DTB was appended, install the potential several images with
# appended DTBs.
define LINUX_INSTALL_IMAGE
	mkdir -p $(1)
	cp $(LINUX_ARCH_PATH)/boot/$(LINUX_IMAGE_NAME).* $(1)
endef
else
# Otherwise, just install the unique image generated by the kernel
# build process.
define LINUX_INSTALL_IMAGE
	$(INSTALL) -m 0644 -D $(LINUX_IMAGE_PATH) $(1)/$(notdir $(LINUX_IMAGE_NAME))
endef
endif

ifneq ($(LINUX_IMAGE_NAME_EXTRA),)
define LINUX_INSTALL_IMAGE_EXTRA
	$(foreach img,$(LINUX_IMAGE_NAME_EXTRA), \
		$(INSTALL) -m 0644 -D $(LINUX_ARCH_PATH)/boot/$(img) $(1) \
	)
endef
endif

ifeq ($(PRJ_STRIP),y)
LINUX_MAKE_FLAGS += INSTALL_MOD_STRIP=1
endif

# Install modules and remove symbolic links pointing to build
# directories, not relevant on the target.
define LINUX_INSTALL_MODULES
	if grep -q "CONFIG_MODULES=y" $(@D)/.config; then \
		$(LINUX_MAKE_ENV) $(MAKE1) $(LINUX_MAKE_FLAGS) -C $(@D) modules_install; \
		rm -f $(BINARIES_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/build ; \
		rm -f $(BINARIES_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/source ; \
	fi
endef

# Run depmod in a target-finalize hook, to encompass modules installed by
# packages.
define LINUX_RUN_DEPMOD
	if test -d $(BINARIES_DIR)/lib/modules/$(LINUX_VERSION_PROBED) \
		&& grep -q "CONFIG_MODULES=y" $(LINUX_DIR)/.config; then \
		depmod -a -b $(BINARIES_DIR) $(LINUX_VERSION_PROBED); \
	fi
endef

define LINUX_INSTALL_CMDS
	$(call LINUX_INSTALL_IMAGE,$(BINARIES_DIR))
	$(call LINUX_INSTALL_IMAGE_EXTRA,$(BINARIES_DIR))
	$(call LINUX_INSTALL_DTB,$(BINARIES_DIR))
	$(LINUX_INSTALL_MODULES)
	$(LINUX_RUN_DEPMOD)
endef

$(eval $(kconfig-component))

# Support for rebuilding the kernel after the cpio archive has
# been generated.
ifneq ($(LINUX_KERNEL_APPENDED_INITRAMFS),)
LINUX_TARGET_REBUILD_WITH_INITRAMFS = $(LINUX_DIR)/.stamp_rebuild-with-initramfs
$(LINUX_TARGET_REBUILD_WITH_INITRAMFS): $(LINUX_TARGET_INSTALL) $(LINUX_KERNEL_APPENDED_INITRAMFS)
	@$(call MESSAGE,"Rebuilding kernel with initramfs")
	# Build the kernel.
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) $(LINUX_TARGET_NAME)
	$(LINUX_APPEND_DTB)
	# Copy the kernel image(s) to its(their) final destination
	$(call LINUX_INSTALL_IMAGE,$(BINARIES_DIR))
	$(Q)touch $@

.PHONY: linux-rebuild-with-initramfs
linux-rebuild-with-initramfs: $(LINUX_TARGET_REBUILD_WITH_INITRAMFS)
endif
