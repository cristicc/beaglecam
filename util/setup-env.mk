#
# Makefile containing various build environment related variables.
#
# Based on Buildroot's Makefile.in:
# https://git.busybox.net/buildroot/tree/package/Makefile.in
#

#
# Configure host tools.
#
ifndef HOSTAR
HOSTAR := ar
endif
ifndef HOSTAS
HOSTAS := as
endif
ifndef HOSTCC
HOSTCC := gcc
HOSTCC := $(shell which $(HOSTCC) || type -p $(HOSTCC) || echo gcc)
endif
HOSTCC_NOCCACHE := $(HOSTCC)
ifndef HOSTCXX
HOSTCXX := g++
HOSTCXX := $(shell which $(HOSTCXX) || type -p $(HOSTCXX) || echo g++)
endif
HOSTCXX_NOCCACHE := $(HOSTCXX)
ifndef HOSTCPP
HOSTCPP := cpp
endif
ifndef HOSTLD
HOSTLD := ld
endif
ifndef HOSTLN
HOSTLN := ln
endif
ifndef HOSTNM
HOSTNM := nm
endif
ifndef HOSTOBJCOPY
HOSTOBJCOPY := objcopy
endif
ifndef HOSTRANLIB
HOSTRANLIB := ranlib
endif
HOSTAR := $(shell which $(HOSTAR) || type -p $(HOSTAR) || echo ar)
HOSTAS := $(shell which $(HOSTAS) || type -p $(HOSTAS) || echo as)
HOSTCPP := $(shell which $(HOSTCPP) || type -p $(HOSTCPP) || echo cpp)
HOSTLD := $(shell which $(HOSTLD) || type -p $(HOSTLD) || echo ld)
HOSTLN := $(shell which $(HOSTLN) || type -p $(HOSTLN) || echo ln)
HOSTNM := $(shell which $(HOSTNM) || type -p $(HOSTNM) || echo nm)
HOSTOBJCOPY := $(shell which $(HOSTOBJCOPY) || type -p $(HOSTOBJCOPY) || echo objcopy)
HOSTRANLIB := $(shell which $(HOSTRANLIB) || type -p $(HOSTRANLIB) || echo ranlib)
SED := $(shell which sed || type -p sed) -i -e

export HOSTAR HOSTAS HOSTCC HOSTCXX HOSTLD
export HOSTCC_NOCCACHE HOSTCXX_NOCCACHE

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

TARGET_BUILD_FLAGS = $(call qstrip,$(PRJ_BUILD_FLAGS))

TARGET_CPPFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
TARGET_CFLAGS = $(TARGET_CPPFLAGS) $(TARGET_BUILD_FLAGS)
TARGET_CXXFLAGS = $(TARGET_CFLAGS)
TARGET_FCFLAGS = $(TARGET_BUILD_FLAGS)

TARGET_CROSS = $(call qstrip,$(PRJ_TOOLCHAIN_PATH))

# Define TARGET_xx variables for all common binutils/gcc
TARGET_AR	= $(TARGET_CROSS)ar
TARGET_AS	= $(TARGET_CROSS)as
TARGET_CC	= $(TARGET_CROSS)gcc
TARGET_CPP	= $(TARGET_CROSS)cpp
TARGET_CXX	= $(TARGET_CROSS)g++
TARGET_FC	= $(TARGET_CROSS)gfortran
TARGET_LD	= $(TARGET_CROSS)ld
TARGET_NM	= $(TARGET_CROSS)nm
TARGET_RANLIB	= $(TARGET_CROSS)ranlib
TARGET_READELF	= $(TARGET_CROSS)readelf
TARGET_OBJCOPY	= $(TARGET_CROSS)objcopy
TARGET_OBJDUMP	= $(TARGET_CROSS)objdump

ifeq ($(PRJ_STRIP),y)
  STRIP_STRIP_DEBUG := --strip-debug
  TARGET_STRIP = $(TARGET_CROSS)strip
  STRIPCMD = $(TARGET_CROSS)strip --remove-section=.comment --remove-section=.note
else
  TARGET_STRIP = /bin/true
  STRIPCMD = $(TARGET_STRIP)
endif

TAR ?= tar
TAR_OPTIONS = $(call qstrip,$(PRJ_TAR_OPTIONS)) -xf

INSTALL := $(shell which install || type -p install)
UNZIP := $(shell which unzip || type -p unzip) -q

APPLY_PATCHES = util/apply-patches.sh $(if $(QUIET),-s)

HOST_CPPFLAGS	= -I$(HOST_DIR)/include
HOST_CFLAGS	?= -O2
HOST_CFLAGS 	+= $(HOST_CPPFLAGS)
HOST_CXXFLAGS	+= $(HOST_CFLAGS)
HOST_LDFLAGS	+= -L$(HOST_DIR)/lib -Wl,-rpath,$(HOST_DIR)/lib

# Quotes are needed for spaces and all in the original PATH content.
PRJ_PATH = "$(HOST_DIR)/bin:$(HOST_DIR)/sbin:$(PATH)"

TARGET_MAKE_ENV = PATH=$(PRJ_PATH)

TARGET_CONFIGURE_OPTS = \
	$(TARGET_MAKE_ENV) \
	AR="$(TARGET_AR)" \
	AS="$(TARGET_AS)" \
	LD="$(TARGET_LD)" \
	NM="$(TARGET_NM)" \
	CC="$(TARGET_CC)" \
	GCC="$(TARGET_CC)" \
	CPP="$(TARGET_CPP)" \
	CXX="$(TARGET_CXX)" \
	FC="$(TARGET_FC)" \
	F77="$(TARGET_FC)" \
	RANLIB="$(TARGET_RANLIB)" \
	READELF="$(TARGET_READELF)" \
	STRIP="$(TARGET_STRIP)" \
	OBJCOPY="$(TARGET_OBJCOPY)" \
	OBJDUMP="$(TARGET_OBJDUMP)" \
	DEFAULT_ASSEMBLER="$(TARGET_AS)" \
	DEFAULT_LINKER="$(TARGET_LD)" \
	CPPFLAGS="$(TARGET_CPPFLAGS)" \
	CFLAGS="$(TARGET_CFLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	FCFLAGS="$(TARGET_FCFLAGS)" \
	FFLAGS="$(TARGET_FCFLAGS)"

HOST_MAKE_ENV = \
	PATH=$(PRJ_PATH) \
	PKG_CONFIG_SYSROOT_DIR="/" \
	PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1 \
	PKG_CONFIG_ALLOW_SYSTEM_LIBS=1 \
	PKG_CONFIG_LIBDIR="$(HOST_DIR)/lib/pkgconfig:$(HOST_DIR)/share/pkgconfig"

HOST_CONFIGURE_OPTS = \
	$(HOST_MAKE_ENV) \
	AR="$(HOSTAR)" \
	AS="$(HOSTAS)" \
	LD="$(HOSTLD)" \
	NM="$(HOSTNM)" \
	CC="$(HOSTCC)" \
	GCC="$(HOSTCC)" \
	CXX="$(HOSTCXX)" \
	CPP="$(HOSTCPP)" \
	OBJCOPY="$(HOSTOBJCOPY)" \
	RANLIB="$(HOSTRANLIB)" \
	CPPFLAGS="$(HOST_CPPFLAGS)" \
	CFLAGS="$(HOST_CFLAGS)" \
	CXXFLAGS="$(HOST_CXXFLAGS)" \
	LDFLAGS="$(HOST_LDFLAGS)"

# This is extra environment we can not export ourselves (eg. because some
# packages use that variable internally, eg. uboot), so we have to explicitly
# pass it to user-supplied external hooks (eg. post-build, post-images).
EXTRA_ENV = \
	PATH=$(PRJ_PATH) \
	DOWNLOAD_DIR=$(DOWNLOAD_DIR) \
	BUILD_DIR=$(BUILD_DIR) \
	O=$(OUTPUT_DIR)

ifeq ($(PRJ_SHARED_STATIC_LIBS),static)
SHARED_STATIC_LIBS_OPTS = --enable-static --disable-shared
TARGET_CFLAGS += -static
TARGET_CXXFLAGS += -static
TARGET_FCFLAGS += -static
TARGET_LDFLAGS += -static
else ifeq ($(PRJ_SHARED_STATIC_LIBS),shared)
SHARED_STATIC_LIBS_OPTS = --disable-static --enable-shared
else ifeq ($(BR2_SHARED_STATIC_LIBS),both)
SHARED_STATIC_LIBS_OPTS = --enable-static --enable-shared
endif

SHARED_STATIC_LIBS_OPTS = $(call qstrip,$(PRJ_SHARED_STATIC_LIBS_OPTS))
