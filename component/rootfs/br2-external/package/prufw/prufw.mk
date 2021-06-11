################################################################################
#
# prufw
#
################################################################################

PRUFW_SITE = $(PRUFW_PKGDIR)/src
PRUFW_SITE_METHOD = local
PRUFW_LIBTOOL_PATCH = NO
PRUFW_DEPENDENCIES = host-ti-cgt-pru host-pru-software-support

define PRUFW_BUILD_CMDS
	$(MAKE) PRU_CGT=$(TI_CGT_PRU_INSTALLDIR) -C $(@D)
endef

define PRUFW_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0644 -D $(@D)/gen/prufw1.out $(TARGET_DIR)/lib/firmware/am335x-pru1-fw
endef

$(eval $(generic-package))
