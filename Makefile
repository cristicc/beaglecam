#
# Makefile for beaglecam project.
#
# Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
#

# Delete default rules to improve performance.
.SUFFIXES:

# Absolute path to project root directory.
ROOT_DIR := $(CURDIR)

# Build artifacts directories.
OUTPUT_DIR := $(ROOT_DIR)/output
ROOTFS_BUILD_DIR := $(OUTPUT_DIR)/rootfs

# Buildroot setup.
BUILDROOT_VERSION_DEFAULT = 2021.02.1
BUILDROOT_DIR := $(OUTPUT_DIR)/buildroot
BUILDROOT_EXTERNAL_DIR := $(ROOT_DIR)/buildroot/external
BUILDROOT_PATCHES_DIR := $(ROOT_DIR)/buildroot/patches

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
