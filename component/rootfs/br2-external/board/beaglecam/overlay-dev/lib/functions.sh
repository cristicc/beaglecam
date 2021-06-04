#!/bin/sh
#
# Setup configuration and utilities.
#

#
# Utility to get the value of a kernel parameter.
# arg1: param name
#
get_kern_param() {
    local value
    value=" $(cat /proc/cmdline) "
    value=${value##* ${1}=}
    value=${value%% *}
    printf "%s" "${value}"
}
