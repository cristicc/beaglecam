#
# This file implements an infrastructure that eases development of
# package .mk files for autotools packages. It should be used for all
# packages that use the autotools as their build system.
#
# In terms of implementation, this autotools infrastructure requires
# the .mk file to only specify metadata information about the
# package: name, version, download URL, etc.
#
# We still allow the package .mk file to override what the different
# steps are doing, if needed. For example, if <PKG>_BUILD_CMDS is
# already defined, it is used as the list of commands to perform to
# build the package, instead of the default autotools behaviour. The
# package can also define some post operation hooks.
#
# Based on Buildroot's autotools package infrastructure:
# https://git.busybox.net/buildroot/tree/package/pkg-autotools.mk
#

#
# Utility function to upgrade config.sub and config.guess files
#
# argument 1 : directory into which config.guess and config.sub need
# to be updated. Note that config.sub and config.guess are searched
# recursively in this directory.
#
define CONFIG_UPDATE
	for file in config.guess config.sub; do \
		for i in $$(find $(1) -name $$file); do \
			cp support/gnuconfig/$$file $$i; \
		done; \
	done
endef

# This function generates the ac_cv_file_<foo> value for a given
# filename. This is needed to convince configure script doing
# AC_CHECK_FILE() tests that the file actually exists, since such
# tests cannot be done in a cross-compilation context. This function
# takes as argument the path of the file. An example usage is:
#
#  FOOBAR_CONF_ENV = \
#	$(call AUTOCONF_AC_CHECK_FILE_VAL,/dev/random)=yes
AUTOCONF_AC_CHECK_FILE_VAL = ac_cv_file_$(subst -,_,$(subst /,_,$(subst .,_,$(1))))

#
# Hook to update config.sub and config.guess if needed
#
define UPDATE_CONFIG_HOOK
	@$(call MESSAGE,"Updating config.sub and config.guess")
	$(call CONFIG_UPDATE,$(@D))
endef

#
# Hook to patch libtool to make it work properly for cross-compilation
#
define LIBTOOL_PATCH_HOOK
	@$(call MESSAGE,"Patching libtool")
	$(Q)for i in `find $($(PKG)_DIR) -name ltmain.sh`; do \
		ltmain_version=`sed -n '/^[ \t]*VERSION=/{s/^[ \t]*VERSION=//;p;q;}' $$i | \
		sed -e 's/\([0-9]*\.[0-9]*\).*/\1/' -e 's/\"//'`; \
		ltmain_patchlevel=`sed -n '/^[ \t]*VERSION=/{s/^[ \t]*VERSION=//;p;q;}' $$i | \
		sed -e 's/\([0-9]*\.[0-9]*\.*\)\([0-9]*\).*/\2/' -e 's/\"//'`; \
		if test $${ltmain_version} = '1.5'; then \
			patch -i support/libtool/buildroot-libtool-v1.5.patch $${i}; \
		elif test $${ltmain_version} = "2.2"; then\
			patch -i support/libtool/buildroot-libtool-v2.2.patch $${i}; \
		elif test $${ltmain_version} = "2.4"; then\
			if test $${ltmain_patchlevel:-0} -gt 2; then\
				patch -i support/libtool/buildroot-libtool-v2.4.4.patch $${i}; \
			else \
				patch -i support/libtool/buildroot-libtool-v2.4.patch $${i}; \
			fi \
		fi \
	done
endef

#
# Hook to gettextize the package if needed
#
define GETTEXTIZE_HOOK
	@$(call MESSAGE,"Gettextizing")
	$(Q)cd $($(PKG)_SRCDIR) && $(GETTEXTIZE) $($(PKG)_GETTEXTIZE_OPTS)
endef

#
# Hook to autoreconf the package if needed
#
define AUTORECONF_HOOK
	@$(call MESSAGE,"Autoreconfiguring")
	$(Q)cd $($(PKG)_SRCDIR) && $($(PKG)_AUTORECONF_ENV) $(AUTORECONF) $($(PKG)_AUTORECONF_OPTS)
endef

#
# Generates the make targets needed to support an autotools package.
#
# Arg1: lowercase component name
# Arg2: uppercase component name
# Arg3: build type (target or host)
#
define autotools-component-helper

$(2)_LIBTOOL_PATCH			?= YES
$(2)_MAKE					?= $$(MAKE)
$(2)_AUTORECONF				?= NO
$(2)_GETTEXTIZE				?= NO

$(2)_CONF_ENV				?=
$(2)_CONF_OPTS				?=
$(2)_MAKE_ENV				?=
$(2)_MAKE_OPTS				?=

$(2)_BUILD_TYPE				= $(call UPPERCASE,$(3))

ifeq ($(3),host)
$(2)_INSTALL_OPTS			?= DESTDIR=$$(HOST_DIR) install
$(2)_ADD_TOOLCHAIN_DEPENDENCY ?= NO
else
$(2)_INSTALL_OPTS			?= DESTDIR=$$(BINARIES_DIR) install
endif

# Configure step. Only define it if not already defined by the package .mk file.
ifndef $(2)_CONFIGURE_CMDS
define $(2)_CONFIGURE_CMDS
	(cd $$($$(PKG)_SRCDIR) && rm -rf config.cache && \
	$$($$($$(PKG)_BUILD_TYPE)_CONFIGURE_OPTS) \
	$$($$(PKG)_CONF_ENV) \
	CONFIG_SITE=/dev/null \
	./configure \
		--prefix=/usr \
		--exec-prefix=/usr \
		--sysconfdir=/etc \
		--localstatedir=/var \
		--program-prefix="" \
		--disable-gtk-doc \
		--disable-gtk-doc-html \
		--disable-doc \
		--disable-docs \
		--disable-documentation \
		--with-xmlto=no \
		--with-fop=no \
		$$(if $$($$(PKG)_OVERRIDE_SRCDIR),,--disable-dependency-tracking) \
		--enable-ipv6 \
		$$(NLS_OPTS) \
		$$(SHARED_STATIC_LIBS_OPTS) \
		$$(QUIET) $$($$(PKG)_CONF_OPTS) \
	)
endef
endif

#$(2)_POST_PATCH_HOOKS += UPDATE_CONFIG_HOOK

ifeq ($$($(2)_AUTORECONF),YES)

# This has to come before autoreconf
ifeq ($$($(2)_GETTEXTIZE),YES)
$(2)_PRE_CONFIGURE_HOOKS += GETTEXTIZE_HOOK
endif
$(2)_PRE_CONFIGURE_HOOKS += AUTORECONF_HOOK
# default values are not evaluated yet, so don't rely on this defaulting to YES
ifneq ($$($(2)_LIBTOOL_PATCH),NO)
$(2)_PRE_CONFIGURE_HOOKS += LIBTOOL_PATCH_HOOK
endif

else # ! AUTORECONF = YES

# default values are not evaluated yet, so don't rely on this defaulting to YES
ifneq ($$($(2)_LIBTOOL_PATCH),NO)
$(2)_POST_PATCH_HOOKS += LIBTOOL_PATCH_HOOK
endif

endif

# Build step. Only define it if not already defined by the package .mk file.
ifndef $(2)_BUILD_CMDS
define $(2)_BUILD_CMDS
	$$($$($$(PKG)_BUILD_TYPE)_MAKE_ENV) $$($$(PKG)_MAKE_ENV) $$($$(PKG)_MAKE) $$($$(PKG)_MAKE_OPTS) -C $$($$(PKG)_SRCDIR)
endef
endif

# Install step. Only define it if not already defined by the package .mk file.
ifndef $(2)_INSTALL_CMDS
define $(2)_INSTALL_CMDS
	$$($$($$(PKG)_BUILD_TYPE)_MAKE_ENV) $$($$(PKG)_MAKE_ENV) $$($$(PKG)_MAKE) $$($$(PKG)_INSTALL_OPTS) -C $$($$(PKG)_SRCDIR)
endef
endif

# Call the generic package infrastructure to generate the necessary make targets
$(call generic-component-helper,$(1),$(2))

endef

# The target generator macro for autotools components.
autotools-component = $(call autotools-component-helper,$(pkgname),$(call UPPERCASE,$(pkgname)),target)
host-autotools-component = $(call autotools-component-helper,$(pkgname),$(call UPPERCASE,$(pkgname)),host)
