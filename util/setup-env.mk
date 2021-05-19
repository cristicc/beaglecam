#
# Makefile containing various build environment related variables.
#
# Based on Buildroot's Makefile.in:
# https://git.busybox.net/buildroot/tree/package/Makefile.in
#

ifndef MAKE
  MAKE := make
endif
ifndef HOSTMAKE
  HOSTMAKE = $(MAKE)
endif
HOSTMAKE := $(shell which $(HOSTMAKE) || type -p $(HOSTMAKE) || echo make)

#
# Configure build jobs.
#
# If PRJ_JLEVEL is 0, scale the maximum concurrency with the number of CPUs.
# An extra job is used in order to keep processors busy while waiting on I/O.
#
ifeq ($(PRJ_JLEVEL),0)
  PARALLEL_JOBS := $(shell echo $$(($$(nproc 2>/dev/null) + 1)))
else
  PARALLEL_JOBS := $(PRJ_JLEVEL)
endif

MAKE1 := $(HOSTMAKE) -j1
override MAKE = $(HOSTMAKE) \
	$(if $(findstring j,$(filter-out --%,$(MAKEFLAGS))),,-j$(PARALLEL_JOBS))

#
# Setup target architecture, cross-compiler and build utilities.
#
ARCH := $(call qstrip,$(PRJ_ARCH))

KERNEL_ARCH := $(shell echo "$(ARCH)" | sed -e "s/-.*//" \
	-e s/i.86/i386/ -e s/sun4u/sparc64/ \
	-e s/arcle/arc/ \
	-e s/arceb/arc/ \
	-e s/arm.*/arm/ -e s/sa110/arm/ \
	-e s/aarch64.*/arm64/ \
	-e s/nds32.*/nds32/ \
	-e s/or1k/openrisc/ \
	-e s/parisc64/parisc/ \
	-e s/powerpc64.*/powerpc/ \
	-e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
	-e s/riscv.*/riscv/ \
	-e s/sh.*/sh/ \
	-e s/s390x/s390/ \
	-e s/microblazeel/microblaze/)

MKIMAGE ?= mkimage
# mkimage supports arm blackfin m68k microblaze mips mips64 nios2 powerpc ppc sh sparc sparc64 x86
# KERNEL_ARCH can be arm64 arc arm blackfin m68k microblaze mips nios2 powerpc sh sparc i386 x86_64 xtensa
# For arm64, arc, xtensa we'll just keep KERNEL_ARCH
# For mips64, we'll just keep mips
# For i386 and x86_64, we need to convert
ifeq ($(KERNEL_ARCH),x86_64)
  MKIMAGE_ARCH = x86
else ifeq ($(KERNEL_ARCH),i386)
  MKIMAGE_ARCH = x86
else
  MKIMAGE_ARCH = $(KERNEL_ARCH)
endif

TARGET_CROSS = $(call qstrip,$(PRJ_TOOLCHAIN_PATH))

ifeq ($(PRJ_STRIP),y)
  STRIP_STRIP_DEBUG := --strip-debug
  TARGET_STRIP = $(TARGET_CROSS)strip
  STRIPCMD = $(TARGET_CROSS)strip --remove-section=.comment --remove-section=.note
else
  TARGET_STRIP = /bin/true
  STRIPCMD = $(TARGET_STRIP)
endif

# Common host utilities.
TAR ?= tar
TAR_OPTIONS = $(call qstrip,$(PRJ_TAR_OPTIONS)) -xf

SED := $(shell which sed || type -p sed) -i -e

INSTALL := $(shell which install || type -p install)
UNZIP := $(shell which unzip || type -p unzip) -q

APPLY_PATCHES = util/apply-patches.sh $(if $(QUIET),-s)

# This is extra environment we can not export ourselves (eg. because some
# packages use that variable internally, eg. uboot), so we have to explicitly
# pass it to user-supplied external hooks (eg. post-build, post-images).
EXTRA_ENV = \
	DOWNLOAD_DIR=$(DOWNLOAD_DIR) \
	BUILD_DIR=$(BUILD_DIR) \
	O=$(OUTPUT_DIR)
