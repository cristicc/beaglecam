#
# Makefile containing various utility macros and helper variables.
#
# Based on Buildroot's common utilities:
# https://git.busybox.net/buildroot/tree/support/misc/utils.mk
# https://git.busybox.net/buildroot/tree/package/pkg-utils.mk
#

# Strip quotes and then whitespaces
qstrip = $(strip $(subst ",,$(1)))

# Variables for use in Make constructs
comma := ,
empty :=
space := $(empty) $(empty)

# make 4.3:
# https://lwn.net/Articles/810071/
# Number signs (#) appearing inside a macro reference or function invocation
#   no longer introduce comments and should not be escaped with backslashes:
#   thus a call such as:
#     foo := $(shell echo '#')
#   is legal.  Previously the number sign needed to be escaped, for example:
#     foo := $(shell echo '\#')
#   Now this latter will resolve to "\#".  If you want to write makefiles
#   portable to both versions, assign the number sign to a variable:
#     H := \#
#     foo := $(shell echo '$H')
SHARP_SIGN := \#

# Case conversion macros. This is inspired by the 'up' macro from gmsl
# (http://gmsl.sf.net). It is optimised very heavily because these macros
# are used a lot. It is about 5 times faster than forking a shell and tr.
#
# The caseconvert-helper creates a definition of the case conversion macro.
# After expansion by the outer $(eval ), the UPPERCASE macro is defined as:
# $(strip $(eval __tmp := $(1))  $(eval __tmp := $(subst a,A,$(__tmp))) ... )
# In other words, every letter is substituted one by one.
#
# The caseconvert-helper allows us to create this definition out of the
# [FROM] and [TO] lists, so we don't need to write down every substition
# manually. The uses of $ and $$ quoting are chosen in order to do as
# much expansion as possible up-front.
#
# Note that it would be possible to conceive a slightly more optimal
# implementation that avoids the use of __tmp, but that would be even
# more unreadable and is not worth the effort.

[FROM] := a b c d e f g h i j k l m n o p q r s t u v w x y z - .
[TO]   := A B C D E F G H I J K L M N O P Q R S T U V W X Y Z _ _

define caseconvert-helper
$(1) = $$(strip \
	$$(eval __tmp := $$(1))\
	$(foreach c, $(2),\
		$$(eval __tmp := $$(subst $(firstword $(subst :, ,$c)),$(lastword $(subst :, ,$c)),$$(__tmp))))\
	$$(__tmp))
endef

$(eval $(call caseconvert-helper,UPPERCASE,$(join $(addsuffix :,$([FROM])),$([TO]))))
$(eval $(call caseconvert-helper,LOWERCASE,$(join $(addsuffix :,$([TO])),$([FROM]))))

# Reverse the orders of words in a list. Again, inspired by the gmsl
# 'reverse' macro.
reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1))) $(firstword $(1)))

# Sanitize macro cleans up generic strings so it can be used as a filename
# and in rules. Particularly useful for VCS version strings, that can contain
# slashes, colons (OK in filenames but not in rules), and spaces.
sanitize = $(subst $(space),_,$(subst :,_,$(subst /,_,$(strip $(1)))))

# Macro to display a message in bold type.
MESSAGE = echo "$(TERM_BOLD)=== $($(PKG)_NAME) $($(PKG)_VERSION) $(call qstrip,$(1))$(TERM_RESET)"
TERM_BOLD := $(shell tput smso 2>/dev/null)
TERM_RESET := $(shell tput rmso 2>/dev/null)

# Utility functions for 'find'
# findfileclauses(filelist) => -name 'X' -o -name 'Y'
findfileclauses = $(call notfirstword,$(patsubst %,-o -name '%',$(1)))
# finddirclauses(base, dirlist) => -path 'base/dirX' -o -path 'base/dirY'
finddirclauses = $(call notfirstword,$(patsubst %,-o -path '$(1)/%',$(2)))

# Miscellaneous utility functions
# notfirstword(wordlist): returns all but the first word in wordlist
notfirstword = $(wordlist 2,$(words $(1)),$(1))

# Needed for the foreach loops to loop over the list of hooks, so that
# each hook call is properly separated by a newline.
define sep


endef

#
# Manipulation of .config files based on the Kconfig infrastructure.
# Used by the BusyBox package, the Linux kernel package, and more.
#

# KCONFIG_DOT_CONFIG ([file])
# Returns the path to the .config file that should be used, which will
# be $(1) if provided, or the current package .config file otherwise.
KCONFIG_DOT_CONFIG = $(strip \
	$(if $(strip $(1)), $(1), \
		$($(PKG)_BUILDDIR)/$($(PKG)_KCONFIG_DOTCONFIG) \
	) \
)

# KCONFIG_MUNGE_DOT_CONFIG (option, newline [, file])
define KCONFIG_MUNGE_DOT_CONFIG
	$(SED) "/\\<$(strip $(1))\\>/d" $(call KCONFIG_DOT_CONFIG,$(3))
	echo '$(strip $(2))' >> $(call KCONFIG_DOT_CONFIG,$(3))
endef

# KCONFIG_ENABLE_OPT (option [, file])
KCONFIG_ENABLE_OPT  = $(call KCONFIG_MUNGE_DOT_CONFIG, $(1), $(1)=y, $(2))
# KCONFIG_SET_OPT (option, value [, file])
KCONFIG_SET_OPT     = $(call KCONFIG_MUNGE_DOT_CONFIG, $(1), $(1)=$(2), $(3))
# KCONFIG_DISABLE_OPT  (option [, file])
KCONFIG_DISABLE_OPT = $(call KCONFIG_MUNGE_DOT_CONFIG, $(1), $(SHARP_SIGN) $(1) is not set, $(2))

# Helper functions to determine the name of a package and its directory from
# its makefile directory. Currently used by the generic-component macro to
# automagically find where the component is located.
pkgdir = $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
pkgname = $(lastword $(subst /, ,$(pkgdir)))
