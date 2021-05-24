#
# This file implements an infrastructure to support building complex components
# via buildroot targets.
#
# In terms of implementation, the component specific .mk file should specify
# only the name of the defconfig file via $(PKG)_CONFIG.
#
# If it resides in an br2-external tree specified via $(PKG)_BR2EXT_DIR),
# only  the basename shall be provided (i.e. with the 'br2-ext/configs/' prefix
# removed). Otherwise, use the path relative to the package source directory.
#
# Additionally, in the config file, you may use BASE_DIR to refer to the
# component build directory $(PKG)_BUILDDIR.
#

#
# Generates the make targets needed to support a buildroot component.
#
# Arg1: lowercase component name
# Arg2: uppercase component name
#
define buildroot-component-helper

# Explicitly set these so we do not get confused by environment
# variables with the same names.
$(2)_VERSION =
$(2)_SOURCE =

# This is a buildroot target, hence add buildroot to the list of dependencies.
$(2)_DEPENDENCIES += buildroot

# Configure step. Only define it if not already defined by the package .mk file.
ifndef $(2)_CONFIGURE_CMDS
# Configure package for target
define $(2)_CONFIGURE_CMDS
	$$(MAKE1) \
		$$(if $$($$(PKG)_BR2EXT_DIR),BR2_EXTERNAL=$$($$(PKG)_BR2EXT_DIR)) \
		O=$$($$(PKG)_BUILDDIR) \
		BR2_DL_DIR=$$(DOWNLOAD_DIR) \
		-C $$(BUILDROOT_SRCDIR) \
		$$(if $$($$(PKG)_BR2EXT_DIR),,defconfig BR2_DEFCONFIG=$$($$(PKG)_PKGDIR))$$($$(PKG)_CONFIG)
endef
endif

# Build step. Only define it if not already defined by the package .mk file.
ifndef $(2)_BUILD_CMDS
define $(2)_BUILD_CMDS
	$$(MAKE1) -C $$($$(PKG)_BUILDDIR)
endef
endif

# Call the generic package infra to generate the necessary make targets.
$(call generic-component-helper,$(1),$(2))

endef

# The target generator macro for buildroot target components.
buildroot-component = $(call buildroot-component-helper,$(pkgname),$(call UPPERCASE,$(pkgname)))
