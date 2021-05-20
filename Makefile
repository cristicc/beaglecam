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

# Absolute path to project root directory.
ROOT_DIR := $(realpath $(CURDIR))

# Project configuration files.
PRJ_DEFCONFIG := $(ROOT_DIR)/prj.config
PRJ_DOTCONFIG := $(OUTPUT_DIR)/.config

# Set default goal.
.PHONY: all
all:

# List of targets for which PRJ_DOTCONFIG doesn't need to be read in.
noconfig_targets := clean distclean help

#
# Some global targets do not trigger a build, but are used to collect metadata,
# or do various checks. When such targets are triggered, some packages should
# not do their configuration sanity checks. Provide them a BR_BUILDING variable
# set to 'y' when we're actually building so they should do their sanity checks.
#
# We're building in two situations: when MAKECMDGOALS is empty (default target
# is to build), or when MAKECMDGOALS contains something not in nobuild_targets.
#
nobuild_targets := $(noconfig_targets) \
	configure reconfigure %-show-depends %-show-version

ifeq ($(MAKECMDGOALS),)
  BR_BUILDING = y
else ifneq ($(filter-out $(nobuild_targets),$(MAKECMDGOALS)),)
  BR_BUILDING = y
endif

# Pull in the user's configuration file. Note that make will attempt to rebuild
# the included file if it doesn't exist. If it is successfully rebuilt, make
# will re-execute itself to read the new version.
ifeq ($(filter $(noconfig_targets),$(MAKECMDGOALS)),)
-include $(PRJ_DOTCONFIG)
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

.PHONY: update-config
update-config:
	cp -a $(PRJ_DOTCONFIG) $(PRJ_DEFCONFIG)

# Setup build environment.
include util/setup-env.mk

# Include package build infrastructure.
include util/pkg-download.mk
include util/pkg-generic.mk
include util/pkg-br-target.mk
include util/pkg-kconfig.mk

# Include project components.
include $(sort $(wildcard component/*/*.mk))

else # $(PRJ_HAVE_DOT_CONFIG)

.PHONY: update-config
all update-config:
	$(error Invalid or missing project configuration; try "make reconfig")
	@exit 1

endif # $(PRJ_HAVE_DOT_CONFIG)

$(BUILD_DIR) $(BINARIES_DIR):
	$(Q)mkdir -p "$@"

$(PRJ_DOTCONFIG): | $(BUILD_DIR) $(BINARIES_DIR)
	$(Q)mkdir -p "$(@D)"
# Generate a Makefile in the output directory, if using a custom output
# directory. This allows convenient use of make in the output directory.
ifeq ($(OUTOFTREE_BUILD),y)
	$(Q)$(ROOT_DIR)/util/gen-make-wrapper.sh $(ROOT_DIR) $(OUTPUT_DIR)
endif
	cp -a $(PRJ_DEFCONFIG) $@

.PHONY: configure
configure: $(PRJ_DOTCONFIG)
	@:

.PHONY: reconfigure
reconfigure:
	rm -f $(PRJ_DOTCONFIG)

.PHONY: clean
clean:
	$(Q)rm -rf $(BUILD_DIR) $(BINARIES_DIR)

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
		'Common targets:' \
		'  configure              Configure project using "$(notdir $(PRJ_DEFCONFIG))" (handled automatically).' \
		'  reconfigure            Force project configure step.' \
		'  update-config          Copy build "$(notdir $(PRJ_DOTCONFIG))" back to "$(notdir $(PRJ_DEFCONFIG))".' \
		'  all                    Build project.' \
		'  clean                  Delete all files created by build.' \
		'  distclean              Delete all non-source files (including downloads).' \
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
		'  GPKG-rebuild           Redo the build step.' \
		'  GPKG-reinstall         Redo the install step.' \
		'' \
		'Kconfig package build targets:' \
		'  KPKG-menuconfig        Call KPKG menuconfig target.' \
		'  KPKG-update-config     Copy KPKG config back to the source config file.' \
		'  KPKG-update-defconfig  Copy KPKG defconfig back to the source config file.' \
		'  KPKG-savedefconfig     Create KPKG defconfig without updating source config file.' \
		'  KPKG-diff-config       Show diff between current config and the source config file.' \
		''
