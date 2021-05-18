ROOTFS_BR2EXT_DIR = $(ROOTFS_PKGDIR)/br2-external
ROOTFS_KCONFIG_FILE = $(ROOTFS_BR2EXT_DIR)/configs/$(call qstrip,$(PRJ_ROOTFS_DEFCONFIG))_defconfig

$(eval $(buildroot-component))
