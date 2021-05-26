#!/bin/sh
#
# Utility script to scan for attached USB storage devices and/or
# prompt the user to choose one of the available drives.
#
# Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
#

#
# Prints messages on stderr.
#
# argv: printf-like arguments
#
error() {
    printf "ERROR: " >&2
    printf "$@" >&2
    printf "\n" >&2
    return 1
}

#
# Converts a byte count to a human readable format in IEC binary notation
# (base-1024), rounded to two decimal places for anything larger than a byte.
# Switchable to padded format and base-1000 if desired.
#
# arg1: value to be converted (in bytes)
# arg2: enable padding - yes / no (default)
# arg3: base - 1000 / 1024 (default)
#
bytes_to_human() {
    awk -v bytes="${1:-0}" -v pad="${2:-no}" -v base="${3:-1024}" \
    'function human(x, pad, base) {
        if (base != 1024) base=1000
        basesuf = (base == 1024) ? "iB" : "B"

        s = "BKMGTEPYZ"
        while (x >= base && length(s) > 1) { x /= base; s = substr(s, 2) }
        s = substr(s, 1, 1)

        xf = (pad == "yes") ? ((s == "B") ? "%3d   " : "%6.2f") : ((s == "B") ? "%d" : "%.2f")
        s = (s != "B") ? (s basesuf) : ((pad == "no") ? s : ((basesuf == "iB") ? (s "  ") : (s " ")))

        return sprintf((xf " %s\n"), x, s)
    }
    BEGIN { print human(bytes, pad, base) }'
}

#
# Scans for USB removable drives and displays a space separated list
# of device names, e.g.: sda sdb
#
scan_drives() {
    grep -H "^1$" /sys/block/*/removable 2>/dev/null \
    | sed "s|/removable:.*$|/size|" \
    | xargs grep -Hv "^0$" \
    | sed "s|^/sys/block/\(.*\)/size:.*$|\1|" \
    | grep "^\(sd\)\|\(mmcblk\)\|\(nvme\)" \
    | sort | tr '\n' ' '
}

#
# Displays a human readable list of USB removable drives, e.g.:
#
# sda       28.83 GiB  CalDigit Card Reader
# sdb       14.50 GiB  Generic- USB3.0 CRW   -SD
#
# Arg1: drive/description separator string, default ' '
#
list_drives() {
    local list drive pname pval vendor name model size

    list=$(scan_drives)
    [ -n "${list}" ] || {
        printf "No USB drive found!\n" >&2
        exit 1
    }

    for drive in ${list}; do
        # Size in 512-byte blocks.
        read size < /sys/block/${drive}/size

        for pname in vendor name model; do
            [ -e /sys/block/${drive}/device/${pname} ] \
            && read pval < /sys/block/${drive}/device/${pname} \
            || unset pval
            eval ${pname}='${pval}'
        done

        printf "%-8s||%s  %s%s%s\n" "${drive}" \
            "$(bytes_to_human $((size*512)) yes)" \
            "${vendor:+${vendor} }" "${name:+${name} }" "${model}" \
        | sed "s/||/${1- }/"
    done
}

#
# Displays a USB removable drives selection dialog.
#
# The device name (e.g. sda) for the chosen drive is written
# to the standard output stream after dialog confirmation.
#
# Returns non-zero when no entry has been selected, otherwise 0.
#
select_drives() {
    list_drives >/dev/null || exit $?

    local output ret
    output=$(list_drives '\x00' | tr '\n' '\0' | xargs -0 dialog \
        --menu "Choose the USB drive to write. Any existing data will be lost!" \
        20 70 20 --stdout 2>&1)
    ret=$?

    [ -z "${NO_CLEAR}" ] && clear

    [ ${ret} -eq 0 ] && printf "%s" "${output%% *}" \
    || error "%s" "${output:-Cancelled by user}"

    return ${ret}
}

# Syntax help.
print_usage() {
    cat <<EOF
Usage:
  ${0##*/} [OPTION...]

Options:
  -h, --help    Show this help message.

  -s, --scan    Print the list of available removable USB storage devices.
                This is the default operation if no option is provided.

  -p, --prompt  Show a dialog for selecting a removable USB storage device.
                The selected device is printed to the console.

  --noclear     Do not clear the screen after dialog prompt.
EOF
}

# Main.
unset DO_SCAN DO_PROMPT NO_CLEAR

while [ $# -gt 0 ]; do
    case $1 in
    -h|--help)
        print_usage
        exit 0
        ;;

    -s|--scan)
        DO_SCAN=1
        ;;

    -p|--prompt)
        DO_PROMPT=1
        ;;

    --noclear)
        NO_CLEAR=1
        ;;

    --)
        break
        ;;

    -*)
        error "Unknown option: %s" "$1"
        print_usage
        exit 1
        ;;

    *)
        error "Unexpected argument: %s" "$1"
        print_usage
        exit 1
        ;;
    esac

    shift
done

[ -n "${DO_SCAN}${DO_PROMPT}" ] || DO_SCAN=1

if [ -n "${DO_SCAN}" ]; then
    list_drives
elif [ -n "${DO_PROMPT}" ]; then
    select_drives
fi
