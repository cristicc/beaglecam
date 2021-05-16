TOOLCHAIN_KCONFIG_FILE = $(call qstrip,$(PRJ_TOOLCHAIN_CUSTOM_CONFIG_FILE))

$(eval $(buildroot-component))
