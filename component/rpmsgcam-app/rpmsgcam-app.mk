RPMSGCAM_APP_SITE = $(RPMSGCAM_APP_PKGDIR)/src
RPMSGCAM_APP_SITE_METHOD = local

define RPMSGCAM_APP_BUILD_CMDS
	$(TARGET_CONFIGURE_OPTS) $(MAKE) -C $(@D)
endef

define RPMSGCAM_APP_INSTALL_CMDS
	$(INSTALL) -m 0755 -D $(@D)/rpmsgcam-app $(BINARIES_DIR)/usr/bin/rpmsgcam-app
endef

$(eval $(generic-component))
