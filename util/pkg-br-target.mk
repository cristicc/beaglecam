#
# This file implements an infrastructure to support building complex components
# via buildroot targets.
#
# In terms of implementation, the component specific .mk file should specify
# only the config file name that resides in a br2-external tree.
#

#
# Generates the make targets needed to build a buildroot component.
#
# Arg1: lowercase component name
# Arg2: uppercase component name
#
define br-component-helper

# Explicitly set these so we do not get confused by environment
# variables with the same names.
$(2)_VERSION =
$(2)_SOURCE =

# This is a buildroot target, hence add buildroot to the list of dependencies
# and mark the package as not being dependant on toolchain.
$(2)_DEPENDENCIES += buildroot
$(2)_ADD_TOOLCHAIN_DEPENDENCY = NO

# Configure step. Only define it if not already defined by the package .mk file.
ifndef $(2)_CONFIGURE_CMDS
# Configure package for target
define $(2)_CONFIGURE_CMDS
	$$(MAKE1) BR2_EXTERNAL=$$($$(PKG)_PKGDIR) O=$$($$(PKG)_BUILDDIR) \
		BR2_DL_DIR=$$(DOWNLOAD_DIR) \
		-C $$(BUILDROOT_SRCDIR) $$($$(PKG)_CONFIG)
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
br-component = $(call br-component-helper,$(pkgname),$(call UPPERCASE,$(pkgname)))
