ROOTFS_BR2EXT_DIR = $(ROOTFS_PKGDIR)/br2-external
ROOTFS_KCONFIG_FILE = $(ROOTFS_BR2EXT_DIR)/configs/$(call qstrip,$(PRJ_ROOTFS_DEFCONFIG))_defconfig
ROOTFS_DEPENDENCIES = linux rpmsgcam-drv rpmsgcam-app

ROOTFS_MAKE_ENV = \
	PRJ_TOOLCHAIN_PATH=$(patsubst %/bin/,%,$(dir $(call qstrip,$(PRJ_TOOLCHAIN_PATH)))) \
	PRJ_TOOLCHAIN_PREFIX=$(patsubst %-,%,$(notdir $(call qstrip,$(PRJ_TOOLCHAIN_PATH))))

define ROOTFS_KCONFIG_FIXUP_CMDS
	$(call KCONFIG_ENABLE_OPT,BR2_TOOLCHAIN_EXTERNAL)
	$(call KCONFIG_ENABLE_OPT,BR2_TOOLCHAIN_EXTERNAL_CUSTOM)
	$(call KCONFIG_SET_OPT,BR2_TOOLCHAIN_EXTERNAL_PATH,"$${PRJ_TOOLCHAIN_PATH}")
	$(call KCONFIG_SET_OPT,BR2_TOOLCHAIN_EXTERNAL_CUSTOM_PREFIX,"$${PRJ_TOOLCHAIN_PREFIX}")
endef

#
# Since external content (files) might be added to the target image via
# BR2_ROOTFS_POST_FAKEROOT_SCRIPT, let's make sure the external binaries
# are stripped (if PRJ_STRIP is set).
#
define STRIP_EXTERNAL_BINARIES
	@$(STRIP_FIND_CMD) | xargs -0 $(STRIPCMD) 2>/dev/null || true
endef
ROOTFS_PRE_BUILD_HOOKS += STRIP_EXTERNAL_BINARIES

$(eval $(buildroot-component))
