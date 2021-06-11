RPMSGCAM_SITE = $(RPMSGCAM_PKGDIR)
RPMSGCAM_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-component))
