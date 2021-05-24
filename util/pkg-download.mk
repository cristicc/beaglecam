#
# Macros to handle downloads from HTTP servers, FTP servers,
# Git repositories, Subversion repositories, and SCP servers.
#
# Based on Buildroot's package infrastructure download support:
# https://git.busybox.net/buildroot/tree/package/pkg-download.mk
#

# Download method commands
export WGET := wget --passive-ftp -nd -t 3
export SVN := svn --non-interactive
export GIT := git
export SCP := scp
export LOCALFILES := cp

# Version of the format of the archives we generate in the corresponding
# download backend:
BR_FMT_VERSION_git = -br1
BR_FMT_VERSION_svn = -br1

DL_WRAPPER = util/download/dl-wrapper.sh

#
# URI scheme helper functions
# Example URIs:
# * http://www.example.com/dir/file
# * scp://www.example.com:dir/file (with domainseparator :)
#
# geturischeme: http
geturischeme = $(firstword $(subst ://, ,$(call qstrip,$(1))))
# getschemeplusuri: git|parameter+http://example.com
getschemeplusuri = $(call geturischeme,$(1))$(if $(2),\|$(2))+$(1)
# stripurischeme: www.example.com/dir/file
stripurischeme = $(lastword $(subst ://, ,$(call qstrip,$(1))))
# domain: www.example.com
domain = $(firstword $(subst $(call domainseparator,$(2)), ,$(call stripurischeme,$(1))))
# notdomain: dir/file
notdomain = $(patsubst $(call domain,$(1),$(2))$(call domainseparator,$(2))%,%,$(call stripurischeme,$(1)))
#
# default domainseparator is /, specify alternative value as first argument
domainseparator = $(if $(1),$(1),/)

# github(user,package,version): returns site of GitHub repository
github = https://github.com/$(1)/$(2)/archive/$(3)

# Expressly do not check hashes for those files
# Exported variables default to immediately expanded in some versions of
# make, but we need it to be recursively-epxanded, so explicitly assign it.
export BR_NO_CHECK_HASH_FOR =

################################################################################
# DOWNLOAD_URIS - List the candidates URIs where to get the package from:
# * Download site
#
# Argument 1 is the source location
# Argument 2 is the upper-case package name
#
################################################################################

DOWNLOAD_URIS += \
	$(patsubst %/,%,$(dir $(call qstrip,$(1))))

################################################################################
# DOWNLOAD -- Download helper. Will call DL_WRAPPER which will try to download
# source from the list returned by DOWNLOAD_URIS.
#
# Argument 1 is the source location
# Argument 2 is the upper-case package name
#
################################################################################

define DOWNLOAD
	$(Q)mkdir -p $($(2)_DL_DIR)
	$(Q)$(EXTRA_ENV) $($(2)_DL_ENV) \
		flock $($(2)_DL_DIR)/.lock $(DL_WRAPPER) \
		-c '$($(2)_DL_VERSION)' \
		-d '$($(2)_DL_DIR)' \
		-f '$(notdir $(1))' \
		-H '$($(2)_HASH_FILE)' \
		-n '$($(2)_BASENAME)' \
		-N '$($(2)_NAME)' \
		-o '$($(2)_DL_DIR)/$(notdir $(1))' \
		$(if $($(2)_GIT_SUBMODULES),-r) \
		$(foreach uri,$(call DOWNLOAD_URIS,$(1),$(2)),-u $(uri)) \
		$(QUIET) \
		-- \
		$($(2)_DL_OPTS)
endef
