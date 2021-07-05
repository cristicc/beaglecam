RPMSGCAM_DRV_SITE = $(RPMSGCAM_DRV_PKGDIR)/src
RPMSGCAM_DRV_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-component))
