#
# This file implements an infrastructure to support building components via
# buildroot targets.
#
# In terms of implementation, the component specific .mk file should specify
# only the path of the buildroot defconfig file via $(PKG)_KCONFIG_FILE.
#
# Additionally, in the defconfig file, you may use BASE_DIR to refer to the
# component build directory $(PKG)_BUILDDIR.
#

#
# Generates the make targets needed to support a buildroot component.
#
# Arg1: lowercase component name
# Arg2: uppercase component name
#
define buildroot-component-helper

$(2)_MAKE					?= $$(MAKE)
$(2)_MAKE_ENV				+= BR2_DL_DIR=$$(DOWNLOAD_DIR)
$(2)_MAKE_OPTS				?=

$(2)_KCONFIG_OPTS			?= -C $$(BUILDROOT_BUILDDIR) \
	O=$$($(2)_BUILDDIR) BR2_DL_DIR=$$(DOWNLOAD_DIR) \
	$$(if $$($(2)_BR2EXT_DIR),BR2_EXTERNAL=$$($(2)_BR2EXT_DIR))

# This is a buildroot target, hence add buildroot to the list of dependencies.
$(2)_KCONFIG_DEPENDENCIES += buildroot

# Buildroot external dir.
$(2)_BR2EXT_DIR				?=

# Explicitly set these so we do not get confused by environment
# variables with the same names.
$(2)_VERSION =
$(2)_SOURCE =

# Build step. Only define it if not already defined by the package .mk file.
ifndef $(2)_BUILD_CMDS
define $(2)_BUILD_CMDS
	$$($$(PKG)_MAKE_ENV) $$($$(PKG)_MAKE) $$($$(PKG)_MAKE_OPTS) -C $$($$(PKG)_BUILDDIR)
endef
endif

# Call the kconfig package infra to generate the necessary make targets.
$(call kconfig-component-helper,$(1),$(2))

endef

# The target generator macro for buildroot target components.
buildroot-component = $(call buildroot-component-helper,$(pkgname),$(call UPPERCASE,$(pkgname)))
