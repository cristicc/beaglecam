#
# Makefile for beaglecam project.
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
CONFIG_FILE := $(OUTPUT_DIR)/.config

# Absolute path to project root directory.
ROOT_DIR := $(realpath $(CURDIR))

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

export Q KBUILD_VERBOSE

# Set default goal.
.PHONY: all
all: beaglecam

# Set serial/parallel make.
ifndef MAKE
  MAKE := make
endif

MAKE1 := $(MAKE) -j1

#
# If MAKE_JLEVEL is 0, scale the maximum concurrency with the number of CPUs.
# An additional job is used in order to keep processors busy while waiting
# on I/O. If the number of processors is not available, assume one.
#
MAKE_JLEVEL ?= 0

ifeq ($(MAKE_JLEVEL),0)
  PARALLEL_JOBS := $(shell echo $$(($$(nproc 2>/dev/null) + 1)))
else
  PARALLEL_JOBS := $(MAKE_JLEVEL)
endif

override MAKE = $(MAKE) \
  $(if $(findstring j,$(filter-out --%,$(MAKEFLAGS))),,-j$(PARALLEL_JOBS))

# Common host tools
TAR ?= tar
TAR_OPTIONS = -xf

# Silent mode requested?
QUIET := $(if $(findstring s,$(filter-out --%,$(MAKEFLAGS))),-q)

# This is extra environment we can not export ourselves (eg. because some
# packages use that variable internally, eg. uboot), so we have to
# explicitly pass it to user-supplied external hooks (eg. post-build,
# post-images)
EXTRA_ENV = \
  DOWNLOAD_DIR=$(DOWNLOAD_DIR) \
  BUILD_DIR=$(BUILD_DIR) \
  O=$(OUTPUT_DIR)

# TODO: set TARGET_CROSS
TARGET_CROSS = $(OUTPUT_DIR)/toolchain/bin/arm-buildroot-linux-uclibcgnueabihf-

# Include common makefiles.
include util/pkg-common.mk
include util/pkg-download.mk
include util/pkg-generic.mk
include util/pkg-br-target.mk

# Include project components.
include buildroot/buildroot.mk
include linux/linux.mk
include rootfs/rootfs.mk
include toolchain/toolchain.mk
include u-boot/u-boot.mk

#
# Main targets.
#

$(OUTPUT_DIR) $(BUILD_DIR):
	$(Q)mkdir -p "$@"

$(CONFIG_FILE):
	$(Q)mkdir -p "$(@D)"
# Generate a Makefile in the output directory, if using a custom output
# directory. This allows convenient use of make in the output directory.
ifeq ($(OUTOFTREE_BUILD),y)
	$(Q)$(ROOT_DIR)/util/gen-make-wrapper.sh $(ROOT_DIR) $(OUTPUT_DIR)
endif
	$(Q)touch $@

.PHONY: prepare
prepare: $(CONFIG_FILE) | $(OUTPUT_DIR) $(BUILD_DIR)

.PHONY: clean
clean:
	$(Q)rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	$(Q)rm -rf $(OUTPUT_DIR)

.PHONY: beaglecam
beaglecam: u-boot linux rootfs

.PHONY: help
help:
	@printf "%s\n" \
		'Cleaning:' \
		'  clean                  Delete all files created by build.' \
		'  distclean              Delete all non-source files (including downloads).' \
		'' \
		'Build:' \
		'  all                    Build beaglecam project.' \
		'  toolchain              Setup toolchain.' \
		'  buildroot              Setup buildroot.' \
		'  u-boot                 Build u-boot bootloader.' \
		'  linux                  Build linux kernel.' \
		'  rootfs                 Build root filesystem.'
