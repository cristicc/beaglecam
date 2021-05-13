#
# Makefile containing various utility functions used by the package
# infrastructure, or by the packages themselves.
#

# Strip quotes and then whitespaces
qstrip = $(strip $(subst ",,$(1)))

# Variables for use in Make constructs
comma := ,
empty :=
space := $(empty) $(empty)

# Needed for the foreach loops to loop over the list of hooks, so that
# each hook call is properly separated by a newline.
define sep


endef

# Case conversion macros.
#
# The caseconvert-helper creates a definition of the case conversion macro.
# After expansion by the outer $(eval ), the UPPERCASE macro is defined as:
# $(strip $(eval __tmp := $(1))  $(eval __tmp := $(subst a,A,$(__tmp))) ... )
# In other words, every letter is substituted one by one.

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

# Macro to display a message in bold type.
MESSAGE = echo "$(TERM_BOLD)=== $($(PKG)_NAME) $($(PKG)_VERSION) $(call qstrip,$(1))$(TERM_RESET)"
TERM_BOLD := $(shell tput smso 2>/dev/null)
TERM_RESET := $(shell tput rmso 2>/dev/null)

# Helper functions to determine the name of a package and its directory from
# its makefile directory. Currently used by the generic-component macro to
# automagically find where the component is located.
pkgdir = $(dir $(lastword $(MAKEFILE_LIST)))
pkgname = $(lastword $(subst /, ,$(pkgdir)))

# Sanitize macro cleans up generic strings so it can be used as a filename
# and in rules. Particularly useful for VCS version strings, that can contain
# slashes, colons (OK in filenames but not in rules), and spaces.
sanitize = $(subst $(space),_,$(subst :,_,$(subst /,_,$(strip $(1)))))

# Macro to apply patches.
APPLY_PATCHES = util/apply-patches.sh $(if $(QUIET),-s)
