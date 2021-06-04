#!/bin/sh
#
# Utility script to run BeagleCam remote commands over SSH.
#
# Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
#

set -e

RUN_DIR=$(dirname "$(readlink -fn "$0")")

PRJ_OUTPUT_DIR=${RUN_DIR}/../output/dev
PRJ_BINARIES_DIR=${PRJ_OUTPUT_DIR}/binaries
ROOTFS_BUILD_DIR=${PRJ_OUTPUT_DIR}/build/rootfs

BEAGLECAM_STATIC_IPS="10.0.0.100 10.0.1.100"

# Run a remote command over SSH.
ssh_cmd() {
    ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=3 \
        root@${BEAGLECAM_REMOTE_IP} "$@"
}

# Test SSH access using the persistent ECDSA key.
test_ssh() {
    printf "\nTesting SSH access\n"

    ssh -q -o BatchMode=yes -o ConnectTimeout=3 root@${BEAGLECAM_REMOTE_IP} true || {
        ssh-add ${ROOTFS_BUILD_DIR}/target/root/.ssh/beaglecam-id_ecdsa
        ssh-keygen -f ${HOME}/.ssh/known_hosts -R ${BEAGLECAM_REMOTE_IP}
    }

    ssh_cmd "uname -a"
}

# Update the specified software components.
update_components() {
    local components files
    local remote_boot_mnt

    components=$*
    remote_boot_mnt=/mnt/boot

    while [ $# -gt 0 ]; do
        case $1 in
        uenv)
            files="${files} ${PRJ_BINARIES_DIR}/uEnv.txt ${PRJ_BINARIES_DIR}/uEnv-falcon.txt"
            ;;
        uboot)
            files="${files} ${PRJ_BINARIES_DIR}/MLO ${PRJ_BINARIES_DIR}/u-boot.img"
            ;;
        kernel)
            files="${files} ${PRJ_BINARIES_DIR}/zImage ${PRJ_BINARIES_DIR}/am335x-boneblack-lcd43.dtb"
            ;;
        rootfs)
            files="${files} ${PRJ_BINARIES_DIR}/rootfs.cpio"
            ;;
        *)
            printf "\nUnknown component: %s\n" "$1"
            print_usage
            return 1
        esac

        shift
    done

    printf "\nUpdating components: %s\n" "${components}"

    ssh_cmd "mkdir -p ${remote_boot_mnt} && mount -t vfat /dev/mmcblk0p1 ${remote_boot_mnt}" \
    && scp ${files} root@${BEAGLECAM_REMOTE_IP}:${remote_boot_mnt}/ \
    && ssh_cmd "sync; umount ${remote_boot_mnt} && reboot" \
    && echo "Rebooting ${BEAGLECAM_REMOTE_IP}.."
}

# Autodetect device IP.
autodetect_ip() {
    local ip
    printf "Autodetecting device IP: "

    for ip in ${BEAGLECAM_STATIC_IPS}; do
        ping -q -c 1 -W 4 ${ip} >/dev/null 2>&1 && {
            BEAGLECAM_REMOTE_IP=${ip}
            printf "%s\n" ${ip}
            break
        }
    done

    [ -n "${BEAGLECAM_REMOTE_IP}" ] || {
        printf "FAILED\n"
        return 1
    }
}

# Syntax help.
print_usage() {
    cat <<EOF
Usage:
  ${0##*/} [OPTION...] [COMMAND [ARG]..]

Options:
  -h, --help    Show this help message.

  -t, --target  Target device hostname or IP address. By default it is
                autodetected using a set of preconfigured IPs.

  -u, --update COMPONENT[,COMPONENT..]
                Perform a software upgrade of the given component(s):
                uenv, uboot, kernel, rootfs. If no component is specified,
                "all" will be considered. Note the device will reboot after
                the operation is completed.
EOF
}

# Main.
unset BEAGLECAM_REMOTE_IP UPDATE_COMPONENTS

while [ $# -gt 0 ]; do
    case $1 in
    -h|--help)
        print_usage
        exit 0
        ;;

    -t|--target)
        shift
        BEAGLECAM_REMOTE_IP=$1
        ;;

    -u|--update)
        shift
        UPDATE_COMPONENTS=${UPDATE_COMPONENTS},$1
        ;;

    -*)
        printf "Unknown option: %s\n" "$1"
        print_usage
        exit 1
        ;;

    *)
        break
        ;;
    esac

    shift
done

[ -z "${BEAGLECAM_REMOTE_IP}" ] && { autodetect_ip || exit $?; }

test_ssh || exit $?

[ -n "${UPDATE_COMPONENTS}" ] && {
    update_components $(echo ${UPDATE_COMPONENTS} | tr ',' ' ')
    exit $?
}

[ -z "$*" ] || {
    printf "\nExecuting remote cmd: %s\n" "$*"
    ssh_cmd "$@"
}
