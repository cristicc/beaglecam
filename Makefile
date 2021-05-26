#
# Makefile for BeagleCam project
#
# Inspired from Buildroot's make infrastructure:
# https://git.busybox.net/buildroot/tree/Makefile
#
# Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
#

# Delete default rules to improve performance.
.SUFFIXES:

# Use POSIX shell.
SHELL = /bin/sh

# Set O variable if not already done on the command line;
ifneq ("$(origin O)", "command line")
  O := $(CURDIR)/output
endif

# Remove any trailing '/.' added by the makefile wrapper installed in the $(O)
# directory. Also remove any trailing '/' set via the command line.
override O := $(patsubst %/,%,$(patsubst %.,%,$(O)))

# Absolute path to project output directory.
# Make sure $(O) actually exists before calling realpath on it, otherwise
# we get empty value.
OUTPUT_DIR := $(shell mkdir -p $(O) >/dev/null 2>&1)$(realpath $(O))
$(if $(OUTPUT_DIR),, $(error output directory "$(OUTPUT_DIR)" does not exist))

# Set common paths.
BUILD_DIR := $(OUTPUT_DIR)/build
DOWNLOAD_DIR := $(OUTPUT_DIR)/downloads
BINARIES_DIR := $(OUTPUT_DIR)/binaries
HOST_DIR := $(OUTPUT_DIR)/host

# Absolute path to project root directory.
ROOT_DIR := $(realpath $(CURDIR))

# Project configuration file.
PRJ_CONFIG := $(ROOT_DIR)/prj.config

# Set default goal.
.PHONY: all
all:

# List of targets for which PRJ_CONFIG doesn't need to be read in.
noconfig_targets := prepare clean distclean help

#
# Some global targets do not trigger a build, but are used to collect metadata,
# or do various checks. When such targets are triggered, some packages should
# not do their configuration sanity checks. Provide them a BR_BUILDING variable
# set to 'y' when we're actually building so they should do their sanity checks.
#
# We're building in two situations: when MAKECMDGOALS is empty (default target
# is to build), or when MAKECMDGOALS contains something not in nobuild_targets.
#
nobuild_targets := $(noconfig_targets) %-show-depends %-show-version

ifeq ($(MAKECMDGOALS),)
  BR_BUILDING = y
else ifneq ($(filter-out $(nobuild_targets),$(MAKECMDGOALS)),)
  BR_BUILDING = y
endif

# Pull in the user's configuration file. Note that make will attempt to rebuild
# the included file if it doesn't exist. If it is successfully rebuilt, make
# will re-execute itself to read the new version.
ifeq ($(filter $(noconfig_targets),$(MAKECMDGOALS)),)
-include $(PRJ_CONFIG)
endif

# Avoid passing O=<dir> option for out-of-tree builds when calling make
# recursively to build project components.
MAKEOVERRIDES :=

# Check if building in-tree or out-of-tree.
ifeq ($(OUTPUT_DIR),$(ROOT_DIR)/output)
  OUTOFTREE_BUILD =
else
  OUTOFTREE_BUILD = y
endif

# Use 'make V=1' to see the all commands.
ifeq ("$(origin V)", "command line")
  KBUILD_VERBOSE = $(V)
endif
ifndef KBUILD_VERBOSE
  KBUILD_VERBOSE = 0
endif

ifeq ($(KBUILD_VERBOSE),1)
  Q =
  ifndef VERBOSE
    VERBOSE = 1
  endif
  export VERBOSE
else
  Q = @
endif

# kconfig uses CONFIG_SHELL
CONFIG_SHELL := $(SHELL)

export SHELL CONFIG_SHELL Q KBUILD_VERBOSE

#
# Differentiate make targets based on the availability of project config.
#
ifeq ($(PRJ_HAVE_DOT_CONFIG),y)

# Include various utility macros and variables.
include util/helpers.mk

# List of all available packages.
PACKAGES_ALL :=

# Silent mode requested?
QUIET := $(if $(findstring s,$(filter-out --%,$(MAKEFLAGS))),-q)

# When stripping, obey to BR2_STRIP_EXCLUDE_DIRS and
# BR2_STRIP_EXCLUDE_FILES
STRIP_FIND_COMMON_CMD = \
	find $(BINARIES_DIR) \
	$(if $(call qstrip,$(PRJ_STRIP_EXCLUDE_DIRS)), \
		\( $(call finddirclauses,$(BINARIES_DIR),$(call qstrip,$(PRJ_STRIP_EXCLUDE_DIRS))) \) \
		-prune -o \
	) \
	$(if $(call qstrip,$(PRJ_STRIP_EXCLUDE_FILES)), \
		-not \( $(call findfileclauses,$(call qstrip,$(PRJ_STRIP_EXCLUDE_FILES))) \) )

# Regular stripping for everything, except libpthread, ld-*.so and
# kernel modules:
# - libpthread.so: a non-stripped libpthread shared library is needed for
#   proper debugging of pthread programs using gdb.
# - ld.so: a non-stripped dynamic linker library is needed for valgrind
# - kernel modules (*.ko): do not function properly when stripped like normal
#   applications and libraries. Normally kernel modules are already excluded
#   by the executable permission check, so the explicit exclusion is only
#   done for kernel modules with incorrect permissions.
STRIP_FIND_CMD = \
	$(STRIP_FIND_COMMON_CMD) \
	-type f \( -perm /111 -o -name '*.so*' \) \
	-not \( $(call findfileclauses,libpthread*.so* ld-*.so* *.ko) \) \
	-print0

# Setup build environment.
include util/setup-env.mk

# Include package build infrastructure.
include util/pkg-download.mk
include util/pkg-generic.mk
include util/pkg-autotools.mk
include util/pkg-br-target.mk
include util/pkg-kconfig.mk

# Include project components.
include $(sort $(wildcard component/*/*.mk))

#
# Generate project build targets faking a generic package.
#
define STRIP_PROJECT_BINARIES
	@$(call MESSAGE,"Stripping binaries")
	$(Q)$(STRIP_FIND_CMD) | xargs -0 $(STRIPCMD) 2>/dev/null || true
endef
PRJ_POST_INSTALL_HOOKS += STRIP_PROJECT_BINARIES

GENIMAGE_CFG = $(call qstrip,$(PRJ_GENIMAGE_CONFIG_FILE))

ifneq ($(GENIMAGE_CFG),)
define GEN_BOOTABLE_SDCARD_IMAGE
	@$(call MESSAGE,"Generating bootable SD card image")
	$(Q)mkdir -p $(@D)/root.tmp $(@D)/genimage.tmp
	$(Q)rm -rf $(@D)/genimage.tmp/*
	$(EXTRA_ENV) genimage \
		--rootpath $(@D)/root.tmp \
		--tmppath $(@D)/genimage.tmp \
		--inputpath $(BINARIES_DIR) \
		--outputpath $(BINARIES_DIR) \
		--config $(GENIMAGE_CFG)
endef
PRJ_POST_INSTALL_HOOKS += GEN_BOOTABLE_SDCARD_IMAGE
PRJ_DEPENDENCIES += genimage
endif

pkgdir = $(ROOT_DIR)
$(eval $(call generic-component-helper,prj,PRJ))

ifeq ($(PRJ_LINUX_KERNEL_APPENDED_INITRAMFS),y)
$(PRJ_TARGET_BUILD): linux-rebuild-with-initramfs
endif

#
# Main make targets.
#
.PHONY: configure
configure: prj-configure

.PHONY: reconfigure
reconfigure: prj-reconfigure-all

.PHONY: build
build: prj-build

.PHONY: rebuild
rebuild: prj-rebuild-all

all: prj

else # $(PRJ_HAVE_DOT_CONFIG)

all:
	$(error Invalid or missing project configuration; try "make reconfig")
	@exit 1

endif # $(PRJ_HAVE_DOT_CONFIG)

$(BUILD_DIR) $(BINARIES_DIR):
	$(Q)mkdir -p $@

$(HOST_DIR)/usr:
	$(Q)mkdir -p $(@D)
	$(Q)ln -snf . $@

$(OUTPUT_DIR)/.stamp_prepared: | $(BUILD_DIR) $(BINARIES_DIR) $(HOST_DIR)/usr
# Generate a Makefile in the output directory, if using a custom output
# directory. This allows convenient use of make in the output directory.
ifeq ($(OUTOFTREE_BUILD),y)
	$(Q)$(ROOT_DIR)/util/gen-make-wrapper.sh $(ROOT_DIR) $(OUTPUT_DIR)
endif
	$(Q)touch $@

.PHONY: prepare
prepare: $(OUTPUT_DIR)/.stamp_prepared

.PHONY: clean
clean:
	$(Q)rm -rf $(BUILD_DIR) $(BINARIES_DIR) $(HOST_DIR) $(OUTPUT_DIR)/.stamp_prepared

.PHONY: distclean
distclean: clean
	$(Q)rm -rf $(OUTPUT_DIR)

.PHONY: help
help:
	@printf "%s\n" \
		'Options:' \
		'  V=0|1                  0 => quiet build (default), 1 => verbose build' \
		'  O=DIR                  Create all output artifacts in DIR'. \
		'' \
		'Main targets:' \
		'  prepare                Create build output directories and Makefile wrapper.' \
		'  all                    Build project.' \
		'  clean                  Delete all files created by build.' \
		'  distclean              Delete all non-source files (including downloads).' \
		'  reconfigure            Rebuild all project components from the configure step.' \
		'  rebuild                Rebuild all project components.' \
		'' \
		'Generic package build targets:' \
		'  GPKG                   Build GPKG and all its dependencies.' \
		'  GPKG-extract           Extract GPKG sources.' \
		'  GPKG-patch             Apply patches to GPKG.' \
		'  GPKG-depends           Build GPKG dependencies.' \
		'  GPKG-configure         Build GPKG up to the configure step.' \
		'  GPKG-build             Build GPKG up to the build step.' \
		'  GPKG-show-depends      List packages on which GPKG depends.' \
		'  GPKG-show-recursive-depends' \
		'                         Recursively list packages on which GPKG depends.' \
		'  GPKG-show-recursive-rdepends' \
		'                         Recursively list packages which have GPKG as a dependency.' \
		'  GPKG-dirclean          Remove GPKG build directory.' \
		'  GPKG-reconfigure       Restart the build from the configure step.' \
		'  GPKG-reconfigure-all   Restart the build from the configure step for all deps.' \
		'  GPKG-rebuild           Redo the build step.' \
		'  GPKG-rebuild-all       Redo the build step for all dependencies.' \
		'  GPKG-reinstall         Redo the install step.' \
		'' \
		'Kconfig package build targets:' \
		'  KPKG-menuconfig        Call KPKG menuconfig target.' \
		'  KPKG-update-config     Copy KPKG config back to the source config file.' \
		'  KPKG-update-defconfig  Copy KPKG defconfig back to the source config file.' \
		'  KPKG-savedefconfig     Create KPKG defconfig without updating source config file.' \
		'  KPKG-diff-config       Show diff between current config and the source config file.' \
		''
