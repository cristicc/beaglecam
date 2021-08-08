#!/bin/sh
#
# Utility script to flash SD cards for booting BeagleBone SBC.
#
# Usage: ./prepare-sd-card.sh /path/to/disk/image/file
#
# Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
#

# Retrieve script path
SCRIPT_DIR=$(dirname "$(readlink -mn "$0")")

#
# Prints a space separated list of disk partitions.
#
# arg1: device name
#
get_disk_parts() {
    [ -n "$1" ] || {
        printf "No device name specified.\n" >&2
        return 1
    }

    grep -Hv "^0$" /sys/block/$1/*/size 2>/dev/null \
    | grep -v '/mmcblk[0-9]boot[0-9]/' \
    | sed "s|^/sys/block/${1}/\(.*\)/size:.*$|\1|" | tr '\n' ' '
}

#
# Flash disk.
#
# arg1: device name
# arg2: disk image file path
#
flash_disk() {
    local dev_path

    dev_path=/dev/$1
    [ -b ${dev_path} ] || {
        printf "ERROR: %s is not a block device!\n" "${dev_path}" >&2
        return 1
    }

    local parts part
    parts=$(get_disk_parts $1)

    for part in ${parts}; do
        part=/dev/${part}
        mount | grep -q "^${part}\b" && {
            printf "Unmounting %s\n" "${part}"
            sudo umount ${part} || {
                printf "Aborting\n" >&2
                return 1
            }
        }
    done

    printf "Please wait while writing '%s' to '%s'\n" "$2" "${dev_path}"

    sudo dd if="$2" of=${dev_path} bs=4M status=progress oflag=sync \
    && printf "Done.\n"
}

#
# Parse args.
#
[ -n "$1" ] || {
    printf "Please provide the SD card image file path!\n" >&2
    exit 1
}

[ -r "$1" ] || {
    printf "File '$1' doesn't exist or is not readable!\n" >&2
    exit 1
}

# Prompt user to select the USB storage device.
CHOICE=$(${SCRIPT_DIR}/scan-usb.sh --prompt --noclear 2>&1)
RES=$?

clear

[ ${RES} -eq 0 ] || {
    printf "%s\n" "${CHOICE}" >&2
    exit ${RES}
}

[ -n "${CHOICE}" ] || {
    printf "Please choose a USB drive!\n" >&2
    exit 1
}

flash_disk "${CHOICE}" "$1"
