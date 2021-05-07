#!/bin/sh
#
# Utility script to setup and build project components.
#
# Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
#

SCRIPT_DIR=$(dirname "$(readlink -mn "$0")")
OUTPUT_DIR=${SCRIPT_DIR}/output
ROOTFS_BUILD_DIR=${OUTPUT_DIR}/rootfs
BUILDROOT_DIR=${OUTPUT_DIR}/buildroot
BUILDROOT_EXTERNAL_DIR=${SCRIPT_DIR}/buildroot/external
BUILDROOT_PATCHES_DIR=${SCRIPT_DIR}/buildroot/patches
BUILDROOT_VERSION_DEFAULT=2021.02.1

#
# Helper to print messages on stderr.
#
# argv: printf-like arguments
#
error() {
    printf "ERROR: " >&2
    printf "$@" >&2
    printf "\n" >&2
    return 1
}

patch_buildroot_cmd() {
    local p

    printf "Patching buildroot..\n"
    for p in $(cd "${BUILDROOT_PATCHES_DIR}"; ls *.patch 2>/dev/null); do
        printf " > %s\n" "${p}"
        patch -p1 -E -N -t -d "${BUILDROOT_DIR}" \
            -i "${BUILDROOT_PATCHES_DIR}/${p}" || exit 1
    done
}

setup_buildroot_cmd() {
    local brver=${1:-${BUILDROOT_VERSION_DEFAULT}}

    printf "Installing buildroot v%s..\n" "$brver"
    mkdir -p "${OUTPUT_DIR}" && (
        cd "${OUTPUT_DIR}" \
        && curl -O https://buildroot.org/downloads/buildroot-${brver}.tar.bz2 \
        && tar -xf buildroot-${brver}.tar.bz2 \
        && ln -s buildroot-${brver} buildroot
    ) && patch_buildroot_cmd
}

build_toolchain_cmd() {
    printf "Building toolchain not implemented!\n"
    return 1
}

config_rootfs_cmd() {
    printf "Configuring rootfs..\n"

    mkdir -p "${ROOTFS_BUILD_DIR}" \
    && make BR2_EXTERNAL="${BUILDROOT_EXTERNAL_DIR}" O="${ROOTFS_BUILD_DIR}" \
        -C "${BUILDROOT_DIR}" defconfig beaglecam_defconfig
}

build_rootfs_cmd() {
    printf "Building rootfs..\n"

    make -C "${ROOTFS_BUILD_DIR}"
}

build_kernel_cmd() {
    printf "Building kernel not implemented!\n"
    return 1
}

# Parse and execute commands.
process_cmd() {
    local cmd_func ret

    [ -n "$1" ] || {
        error "Missing command."
        return 2
    }

    cmd_func=${1}_cmd
    command -v ${cmd_func} >/dev/null || {
        error "Invalid command: %s" "$1"
        return 3
    }

    shift
    ${cmd_func} "$@" && printf "OK\n" && return 0

    ret=$?
    printf "FAILED\n"
    return $?
}

#
# Syntax help.
#
print_usage() {
    cat <<EOF
Usage:
  ${0##*/} [OPTION] command [arguments...]

Options:
  -h, --help                        Show this help message.

Commands:
  setup_buildroot [VERSION]         Downloads and unpacks buildroot. If VERSION is not
                                    provided, ${BUILDROOT_VERSION_DEFAULT} will be used.
  build_rootfs [clean]              Builds the root filesystem. Use 'clean' to perform
                                    a fresh build.
EOF
}

#
# Main.
#
while [ $# -gt 0 ]; do
    case $1 in
    # Normal option processing.
    -h|--help)
        print_usage
        exit 0
        ;;

    --)
        break
        ;;

    --*)
        error "Unknown (long) option: %s"
        print_usage
        exit 1
        ;;

    -?)
        error "Unknown (short) option: %s" "$1"
        print_usage
        exit 1
        ;;

    # Split apart combined short options.
    -*)
        split=$1
        shift
        set -- $(echo "$split" | cut -c 2- | sed 's/./-& /g') "$@"
        continue
        ;;

    # Done with options.
    *)
        break
        ;;
    esac

    shift
done

# Process command
process_cmd "$@"
