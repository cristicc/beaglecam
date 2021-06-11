#
# This file implements an infrastructure to support building out-of-tree
# Linux kernel modules.
#
# The kernel-module infrastructure requires the packages that use it to also
# use another package infrastructure. kernel-module only defines post-build
# and post-install hooks. This allows the package to build both kernel
# modules and/or user-space components (with any of the other *-package
# infra).
#
# Based on Buildroot's kernel-module infrastructure:
# https://git.busybox.net/buildroot/tree/package/pkg-kernel-module.mk
#

#
# Generates the make targets needed to support building a kernel module.
#
# Arg1: lowercase component name
# Arg2: uppercase component name
#
define kernel-module-helper

# Ensure the kernel will support modules
LINUX_NEEDS_MODULES = y

# The kernel must be built first.
$(2)_DEPENDENCIES += linux

# This is only defined in some infrastructures (e.g. autotools, cmake),
# but not in others (e.g. generic). So define it here as well.
$(2)_MAKE ?= $$(MAKE)

# If not specified, consider the source of the kernel module to be at
# the root of the package.
$(2)_MODULE_SUBDIRS ?= .

# Build the kernel module(s)
# Force PWD for those packages that want to use it to find their
# includes and other support files (Booo!)
define $(2)_KERNEL_MODULES_BUILD
	@$$(call MESSAGE,"Building kernel module(s)")
	$$(foreach d,$$($(2)_MODULE_SUBDIRS), \
		$$(LINUX_MAKE_ENV) $$($$(PKG)_MAKE) \
			-C $$(LINUX_DIR) \
			$$(LINUX_MAKE_FLAGS) \
			$$($(2)_MODULE_MAKE_OPTS) \
			PWD=$$(@D)/$$(d) \
			M=$$(@D)/$$(d) \
			modules$$(sep))
endef
$(2)_POST_BUILD_HOOKS += $(2)_KERNEL_MODULES_BUILD

# Install the kernel module(s)
# Force PWD for those packages that want to use it to find their
# includes and other support files (Booo!)
define $(2)_KERNEL_MODULES_INSTALL
	@$$(call MESSAGE,"Installing kernel module(s)")
	$$(foreach d,$$($(2)_MODULE_SUBDIRS), \
		$$(LINUX_MAKE_ENV) $$($$(PKG)_MAKE) \
			-C $$(LINUX_DIR) \
			$$(LINUX_MAKE_FLAGS) \
			$$($(2)_MODULE_MAKE_OPTS) \
			PWD=$$(@D)/$$(d) \
			M=$$(@D)/$$(d) \
			modules_install$$(sep))
endef
$(2)_POST_INSTALL_HOOKS += $(2)_KERNEL_MODULES_INSTALL

endef

# The target generator macro for kernel module packages
kernel-module = $(call kernel-module-helper,$(pkgname),$(call UPPERCASE,$(pkgname)))
