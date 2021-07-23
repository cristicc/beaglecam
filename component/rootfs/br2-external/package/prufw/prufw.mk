################################################################################
#
# prufw
#
################################################################################

PRUFW_SITE = $(PRUFW_PKGDIR)/src
PRUFW_SITE_METHOD = local
PRUFW_LIBTOOL_PATCH = NO
PRUFW_DEPENDENCIES = host-ti-cgt-pru host-pru-software-support

ifeq ($(BR2_PACKAGE_PRUFW_LED_DIAG),y)
PRUFW_CFLAGS += -DLED_DIAG_ENABLED
endif

define PRUFW_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) PRU_CGT=$(TI_CGT_PRU_INSTALLDIR) -C $(@D) \
		CFLAGS="$(PRUFW_CFLAGS)"
endef

define PRUFW_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0644 -D $(@D)/gen/prufw0.out $(TARGET_DIR)/lib/firmware/am335x-pru0-fw
	$(INSTALL) -m 0644 -D $(@D)/gen/prufw1.out $(TARGET_DIR)/lib/firmware/am335x-pru1-fw
endef

$(eval $(generic-package))
