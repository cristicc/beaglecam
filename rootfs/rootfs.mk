TODO: move buildroot/external to ./buildroot

ROOTFS_CONFIG = initram_defconfig

$(eval $(buildroot-component))
