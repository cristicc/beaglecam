#
# This file implements an infrastructure to support building generic
# components via dedicated *.mk files.
#
# Based on Buildroot's generic package infrastructure:
# https://git.busybox.net/buildroot/tree/package/pkg-generic.mk
#

#
# Implicit targets producing a stamp file for each step of a component build.
#

# Retrieve the archive
$(BUILD_DIR)/%/.stamp_downloaded:
	$(foreach hook,$($(PKG)_PRE_DOWNLOAD_HOOKS),$(call $(hook))$(sep))
# Only show the download message if it isn't already downloaded
	$(Q)for p in $($(PKG)_ALL_DOWNLOADS); do \
		[ -e $($(PKG)_DL_DIR)/$$(basename $$p) ] || { \
			$(call MESSAGE,"Downloading"); break; \
		}; \
	done
	$(foreach p,$($(PKG)_ALL_DOWNLOADS),$(call DOWNLOAD,$(p),$(PKG))$(sep))
	$(foreach hook,$($(PKG)_POST_DOWNLOAD_HOOKS),$(call $(hook))$(sep))
	$(Q)mkdir -p $(@D)
	$(Q)touch $@

# Unpack the archive
$(BUILD_DIR)/%/.stamp_extracted:
	@$(call MESSAGE,"Extracting")
	$(foreach hook,$($(PKG)_PRE_EXTRACT_HOOKS),$(call $(hook))$(sep))
	$(Q)mkdir -p $(@D)
	$($(PKG)_EXTRACT_CMDS)
# some packages have messed up permissions inside
	$(Q)chmod -R +rw $(@D)
	$(foreach hook,$($(PKG)_POST_EXTRACT_HOOKS),$(call $(hook))$(sep))
	$(Q)touch $@

# Rsync the source directory if the <pkg>_OVERRIDE_SRCDIR feature is used.
$(BUILD_DIR)/%/.stamp_rsynced:
	@$(call MESSAGE,"Syncing from source dir $(SRCDIR)")
	@mkdir -p $(@D)
	$(foreach hook,$($(PKG)_PRE_RSYNC_HOOKS),$(call $(hook))$(sep))
	@test -d $(SRCDIR) || (echo "ERROR: $(SRCDIR) does not exist" ; exit 1)
	rsync -au --chmod=u=rwX,go=rX $($(PKG)_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS) $(RSYNC_VCS_EXCLUSIONS) $(call qstrip,$(SRCDIR))/ $(@D)
	$(foreach hook,$($(PKG)_POST_RSYNC_HOOKS),$(call $(hook))$(sep))
	$(Q)touch $@

# Patch
$(BUILD_DIR)/%/.stamp_patched: PATCH_BASE_DIRS = $(PKGDIR) $(PKGDIR)/patches
$(BUILD_DIR)/%/.stamp_patched:
	@$(call MESSAGE,"Patching")
	$(foreach hook,$($(PKG)_PRE_PATCH_HOOKS),$(call $(hook))$(sep))
	$(foreach p,$($(PKG)_PATCH),$(APPLY_PATCHES) $(@D) $($(PKG)_DL_DIR) $(notdir $(p))$(sep))
	$(Q)( \
	for D in $(PATCH_BASE_DIRS); do \
	  if test -d $${D}; then \
	    if test -d $${D}/$($(PKG)_VERSION); then \
	      $(APPLY_PATCHES) $(@D) $${D}/$($(PKG)_VERSION) \*.patch || exit 1; \
	    else \
	      $(APPLY_PATCHES) $(@D) $${D} \*.patch || exit 1; \
	    fi; \
	  fi; \
	done; \
	)
	$(foreach hook,$($(PKG)_POST_PATCH_HOOKS),$(call $(hook))$(sep))
	$(Q)touch $@

# Configure
$(BUILD_DIR)/%/.stamp_configured:
	@$(call MESSAGE,"Configuring")
	$(foreach hook,$($(PKG)_PRE_CONFIGURE_HOOKS),$(call $(hook))$(sep))
	$($(PKG)_CONFIGURE_CMDS)
	$(foreach hook,$($(PKG)_POST_CONFIGURE_HOOKS),$(call $(hook))$(sep))
	$(Q)touch $@

# Build
$(BUILD_DIR)/%/.stamp_built::
	@$(call MESSAGE,"Building")
	$(foreach hook,$($(PKG)_PRE_BUILD_HOOKS),$(call $(hook))$(sep))
	+$($(PKG)_BUILD_CMDS)
	$(foreach hook,$($(PKG)_POST_BUILD_HOOKS),$(call $(hook))$(sep))
	$(Q)touch $@

# Install
$(BUILD_DIR)/%/.stamp_installed:
	@$(call MESSAGE,"Installing to binaries directory")
	$(foreach hook,$($(PKG)_PRE_INSTALL_HOOKS),$(call $(hook))$(sep))
	+$($(PKG)_INSTALL_CMDS)
	$(foreach hook,$($(PKG)_POST_INSTALL_HOOKS),$(call $(hook))$(sep))
	$(Q)touch $@

# Remove package sources
$(BUILD_DIR)/%/.stamp_dircleaned:
	rm -Rf $(@D)

#
# Generates the make targets needed to support a generic component.
#
# Arg1: lowercase component name
# Arg2: uppercase component name
#
define generic-component-helper

# Ensure the package is only declared once, i.e. do not accept that a
# package be re-defined by a br2-external tree.
ifneq ($(call strip,$(filter $(1),$(PACKAGES_ALL))),)
$$(error Package '$(1)' defined a second time in '$(pkgdir)'; \
	previous definition was in '$$($(2)_PKGDIR)')
endif
PACKAGES_ALL += $(1)

# Define component-specific variables.
$(2)_NAME			= $(1)
$(2)_PKGDIR			= $(pkgdir)

$(2)_DL_VERSION		:= $$(strip $$($(2)_VERSION))
$(2)_VERSION		:= $$(call sanitize,$$($(2)_DL_VERSION))

$(2)_BASENAME		= $$(if $$($(2)_VERSION),$(1)-$$($(2)_VERSION),$(1))
$(2)_DL_SUBDIR		?= $$($(2)_NAME)
$(2)_DL_DIR			= $$(DOWNLOAD_DIR)/$$($(2)_DL_SUBDIR)
$(2)_DIR			= $$(BUILD_DIR)/$$($(2)_BASENAME)

ifeq ($$($(2)_STRIP_COMPONENTS),)
  $(2)_STRIP_COMPONENTS = 1
endif

$(2)_SRCDIR			= $$($(2)_DIR)/$$($(2)_SUBDIR)
$(2)_BUILDDIR		?= $$($(2)_SRCDIR)

ifneq ($$($(2)_OVERRIDE_SRCDIR),)
  $(2)_VERSION = custom
endif

ifndef $(2)_SITE_METHOD
  # Try automatic detection using the scheme part of the URI
  $(2)_SITE_METHOD = $$(call geturischeme,$$($(2)_SITE))
endif

ifeq ($$($(2)_SITE_METHOD),local)
  ifeq ($$($(2)_OVERRIDE_SRCDIR),)
    $(2)_OVERRIDE_SRCDIR = $$($(2)_SITE)
  endif
  ifeq ($$($(2)_OVERRIDE_SRCDIR),)
    $$(error $(1) has local site method, but "$(2)_SITE" is not defined)
  endif
endif

$(2)_ALL_DOWNLOADS	= \
	$$(if $$($(2)_SOURCE),$$($(2)_SITE_METHOD)+$$($(2)_SITE)/$$($(2)_SOURCE)) \
	$$(foreach p,$$($(2)_PATCH) $$($(2)_EXTRA_DOWNLOADS),\
		$$(if $$(findstring ://,$$(p)),$$(p),\
			$$($(2)_SITE_METHOD)+$$($(2)_SITE)/$$(p)))

ifneq ($(1),toolchain)
# When a target package is a toolchain dependency set this variable to 'NO'
# so the 'toolchain' dependency is not added to prevent a circular dependency.
$(2)_ADD_TOOLCHAIN_DEPENDENCY ?= YES

ifeq ($$($(2)_ADD_TOOLCHAIN_DEPENDENCY),YES)
  $(2)_DEPENDENCIES += toolchain
endif
endif

# Eliminate duplicates in dependencies
$(2)_FINAL_DEPENDENCIES			= $$(sort $$($(2)_DEPENDENCIES))
$(2)_FINAL_DOWNLOAD_DEPENDENCIES = $$(sort $$($(2)_DOWNLOAD_DEPENDENCIES))
$(2)_FINAL_EXTRACT_DEPENDENCIES	= $$(sort $$($(2)_EXTRACT_DEPENDENCIES))
$(2)_FINAL_PATCH_DEPENDENCIES	= $$(sort $$($(2)_PATCH_DEPENDENCIES))

$(2)_FINAL_ALL_DEPENDENCIES 	= \
	$$(sort \
		$$($(2)_FINAL_DEPENDENCIES) \
		$$($(2)_FINAL_DOWNLOAD_DEPENDENCIES) \
		$$($(2)_FINAL_EXTRACT_DEPENDENCIES) \
		$$($(2)_FINAL_PATCH_DEPENDENCIES))

$(2)_FINAL_RECURSIVE_DEPENDENCIES = $$(sort \
	$$(if $$(filter undefined,$$(origin $(2)_FINAL_RECURSIVE_DEPENDENCIES__X)), \
		$$(eval $(2)_FINAL_RECURSIVE_DEPENDENCIES__X := \
			$$(foreach p, \
				$$($(2)_FINAL_ALL_DEPENDENCIES), \
				$$(p) \
				$$($$(call UPPERCASE,$$(p))_FINAL_RECURSIVE_DEPENDENCIES) \
			) \
		) \
	) \
	$$($(2)_FINAL_RECURSIVE_DEPENDENCIES__X))

$(2)_FINAL_RECURSIVE_RDEPENDENCIES = $$(sort \
	$$(if $$(filter undefined,$$(origin $(2)_FINAL_RECURSIVE_RDEPENDENCIES__X)), \
		$$(eval $(2)_FINAL_RECURSIVE_RDEPENDENCIES__X := \
			$$(foreach p, \
				$$($(2)_RDEPENDENCIES), \
				$$(p) \
				$$($$(call UPPERCASE,$$(p))_FINAL_RECURSIVE_RDEPENDENCIES) \
			) \
		) \
	) \
	$$($(2)_FINAL_RECURSIVE_RDEPENDENCIES__X))

# Define sub-target stamps
$(2)_TARGET_INSTALL 			= $$($(2)_DIR)/.stamp_installed
$(2)_TARGET_BUILD 				= $$($(2)_DIR)/.stamp_built
$(2)_TARGET_CONFIGURE			= $$($(2)_DIR)/.stamp_configured
$(2)_TARGET_RSYNC				= $$($(2)_DIR)/.stamp_rsynced
$(2)_TARGET_PATCH				= $$($(2)_DIR)/.stamp_patched
$(2)_TARGET_EXTRACT				= $$($(2)_DIR)/.stamp_extracted
$(2)_TARGET_SOURCE				= $$($(2)_DIR)/.stamp_downloaded
$(2)_TARGET_DIRCLEAN			= $$($(2)_DIR)/.stamp_dircleaned

# Default extract command
$(2)_EXTRACT_CMDS ?= \
	$$(if $$($(2)_SOURCE),$$(TAR) \
		--strip-components=$$($(2)_STRIP_COMPONENTS) -C $$($(2)_DIR) \
		$$(foreach x,$$($(2)_EXCLUDES),--exclude='$$(x)' ) \
		$$(TAR_OPTIONS) $$($(2)_DL_DIR)/$$($(2)_SOURCE))

# Pre/post-steps hooks
$(2)_PRE_DOWNLOAD_HOOKS			?=
$(2)_POST_DOWNLOAD_HOOKS		?=
$(2)_PRE_EXTRACT_HOOKS			?=
$(2)_POST_EXTRACT_HOOKS			?=
$(2)_PRE_RSYNC_HOOKS			?=
$(2)_POST_RSYNC_HOOKS			?=
$(2)_PRE_PATCH_HOOKS			?=
$(2)_POST_PATCH_HOOKS			?=
$(2)_PRE_CONFIGURE_HOOKS		?=
$(2)_POST_CONFIGURE_HOOKS		?=
$(2)_PRE_BUILD_HOOKS			?=
$(2)_POST_BUILD_HOOKS			?=
$(2)_PRE_INSTALL_HOOKS			?=
$(2)_POST_INSTALL_HOOKS			?=

# Human-friendly targets and target sequencing
$(1):							$(1)-install
$(1)-install:					$$($(2)_TARGET_INSTALL)
$$($(2)_TARGET_INSTALL):		$$($(2)_TARGET_BUILD)

$(1)-build:						$$($(2)_TARGET_BUILD)
$$($(2)_TARGET_BUILD):			$$($(2)_TARGET_CONFIGURE)

$(1)-configure:					$$($(2)_TARGET_CONFIGURE)
$$($(2)_TARGET_CONFIGURE):		| prepare $$($(2)_FINAL_DEPENDENCIES)

ifeq ($$(strip $$($(2)_SITE)$$($(2)_SOURCE)),)
# In case of packages without source code, assuming a br-target step sequence:
# - build dir creation
# - depends
#
$$($(2)_TARGET_CONFIGURE):		| $$($(2)_DIR)

$$($(2)_DIR):
	$$(Q)mkdir -p $$@

$(1)-depends:					$$($(2)_FINAL_DEPENDENCIES)

$(1)-patch:
$(1)-extract:
$(1)-source:
$(1)-external-deps:

else ifneq ($$($(2)_OVERRIDE_SRCDIR),)
# In the package override case, the sequence of steps is:
#  source, by rsyncing
#  depends
#  configure

# Use an order-only dependency so the "<pkg>-clean-for-rebuild" rule
# can remove the stamp file without triggering the configure step.
$$($(2)_TARGET_CONFIGURE):		| $$($(2)_TARGET_RSYNC)

$(1)-depends:					$$($(2)_FINAL_DEPENDENCIES)

$(1)-patch:						$(1)-rsync
$(1)-extract:					$(1)-rsync

$(1)-rsync:						$$($(2)_TARGET_RSYNC)

$(1)-source:

$(1)-external-deps:
	@echo "file://$$($(2)_OVERRIDE_SRCDIR)"

else
# In the normal case (no package override), the sequence of steps is:
# - source (by downloading)
# - depends
# - extract
# - patch
# - configure
#
$$($(2)_TARGET_CONFIGURE):		$$($(2)_TARGET_PATCH)

$(1)-patch:						$$($(2)_TARGET_PATCH)
$$($(2)_TARGET_PATCH):			$$($(2)_TARGET_EXTRACT)
# Order-only dependency
$$($(2)_TARGET_PATCH):  		| $$(patsubst %,%-patch,$$($(2)_FINAL_PATCH_DEPENDENCIES))

$(1)-extract:					$$($(2)_TARGET_EXTRACT)
$$($(2)_TARGET_EXTRACT):		$$($(2)_TARGET_SOURCE)
$$($(2)_TARGET_EXTRACT):		| $$($(2)_FINAL_EXTRACT_DEPENDENCIES)

$(1)-depends:					$$($(2)_FINAL_ALL_DEPENDENCIES)

$(1)-source:					$$($(2)_TARGET_SOURCE)
$$($(2)_TARGET_SOURCE):			| $$($(2)_FINAL_DOWNLOAD_DEPENDENCIES)

$(1)-external-deps:
	@for p in $$($(2)_SOURCE)	$$($(2)_PATCH) $$($(2)_EXTRA_DOWNLOADS) ; do \
		echo $$$$(basename $$$$p) ; \
	done
endif

$(1)-show-version:
	@echo $$($(2)_VERSION)

$(1)-show-depends:
	@echo $$($(2)_FINAL_ALL_DEPENDENCIES)

$(1)-show-recursive-depends:
	@echo $$($(2)_FINAL_RECURSIVE_DEPENDENCIES)

$(1)-show-recursive-rdepends:
	@echo $$($(2)_FINAL_RECURSIVE_RDEPENDENCIES)

$(1)-show-build-order:			$$(patsubst %,%-show-build-order,$$($(2)_FINAL_ALL_DEPENDENCIES))
	@:
	$$(info $(1))

$(1)-dirclean:					$$($(2)_TARGET_DIRCLEAN)

$(1)-clean-for-reinstall:
ifneq ($$($(2)_OVERRIDE_SRCDIR),)
	rm -f $$($(2)_TARGET_RSYNC)
endif
	rm -f $$($(2)_TARGET_INSTALL)

$(1)-reinstall:					$(1)-clean-for-reinstall $(1)

$(1)-reinstall-all:				$$(patsubst %,%-clean-for-reinstall,$$($(2)_FINAL_ALL_DEPENDENCIES)) $(1)-reinstall

$(1)-clean-for-rebuild:			$(1)-clean-for-reinstall
	rm -f $$($(2)_TARGET_BUILD)

$(1)-rebuild:					$(1)-clean-for-rebuild $(1)

$(1)-rebuild-all:				$$(patsubst %,%-clean-for-rebuild,$$($(2)_FINAL_ALL_DEPENDENCIES)) $(1)-rebuild

$(1)-clean-for-reconfigure: 	$(1)-clean-for-rebuild
	rm -f $$($(2)_TARGET_CONFIGURE)

$(1)-reconfigure:				$(1)-clean-for-reconfigure $(1)

$(1)-reconfigure-all:			$$(patsubst %,%-clean-for-reconfigure,$$($(2)_FINAL_ALL_DEPENDENCIES)) $(1)-reconfigure

# Define target local variables.
$$($(2)_TARGET_INSTALL):		PKG=$(2)
$$($(2)_TARGET_BUILD):			PKG=$(2)
$$($(2)_TARGET_CONFIGURE):		PKG=$(2)
$$($(2)_TARGET_CONFIGURE):		NAME=$(1)
$$($(2)_TARGET_RSYNC):			SRCDIR=$$($(2)_OVERRIDE_SRCDIR)
$$($(2)_TARGET_RSYNC):			PKG=$(2)
$$($(2)_TARGET_PATCH):			PKG=$(2)
$$($(2)_TARGET_PATCH):			PKGDIR=$(pkgdir)
$$($(2)_TARGET_EXTRACT):		PKG=$(2)
$$($(2)_TARGET_SOURCE):			PKG=$(2)
$$($(2)_TARGET_SOURCE):			PKGDIR=$(pkgdir)
$$($(2)_TARGET_DIRCLEAN):		PKG=$(2)
$$($(2)_TARGET_DIRCLEAN):		NAME=$(1)

# Register package as a reverse-dependencies of all its dependencies.
$$(eval $$(foreach p,$$($(2)_FINAL_ALL_DEPENDENCIES),\
	$$(call UPPERCASE,$$(p))_RDEPENDENCIES += $(1)$$(sep)))

ifneq ($$($(2)_LINUX_CONFIG_FIXUPS),)
PACKAGES_LINUX_CONFIG_FIXUPS += $$($(2)_LINUX_CONFIG_FIXUPS)$$(sep)
endif
TARGET_FINALIZE_HOOKS += $$($(2)_TARGET_FINALIZE_HOOKS)

# Ensure all virtual targets are PHONY. Listed alphabetically.
.PHONY:	$(1) \
	$(1)-build \
	$(1)-clean-for-rebuild \
	$(1)-clean-for-reconfigure \
	$(1)-clean-for-reinstall \
	$(1)-configure \
	$(1)-depends \
	$(1)-dirclean \
	$(1)-external-deps \
	$(1)-extract \
	$(1)-install \
	$(1)-patch \
	$(1)-rebuild \
	$(1)-rebuild-all \
	$(1)-reconfigure \
	$(1)-reconfigure-all \
	$(1)-reinstall \
	$(1)-reinstall-all \
	$(1)-rsync \
	$(1)-show-depends \
	$(1)-show-recursive-depends \
	$(1)-show-recursive-rdepends \
	$(1)-show-version \
	$(1)-source

ifneq ($$($(2)_SOURCE),)
ifeq ($$($(2)_SITE),)
  $$(error $(2)_SITE cannot be empty when $(2)_SOURCE is not)
endif
endif

ifeq ($$(patsubst %/,ERROR,$$($(2)_SITE)),ERROR)
  $$(error $(2)_SITE ($$($(2)_SITE)) cannot have a trailing slash)
endif

endef # generic-component-helper

# The target generator macro for generic components.
generic-component = $(call generic-component-helper,$(pkgname),$(call UPPERCASE,$(pkgname)))
