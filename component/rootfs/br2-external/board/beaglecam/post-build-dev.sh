#!/bin/sh

set -u
set -e

# Generate persistent SSH key
AUTH_KEYS_FILE=${TARGET_DIR}/root/.ssh/authorized_keys
[ -f ${AUTH_KEYS_FILE} ] || (
    umask 077
    mkdir -p ${AUTH_KEYS_FILE%/*}
    cd ${AUTH_KEYS_FILE%/*}
    umask 377
    ssh-keygen -t ecdsa -N '' -C 'BeagleCam SSH key' -f ${AUTH_KEYS_FILE}_tmp
    mv ${AUTH_KEYS_FILE}_tmp.pub ${AUTH_KEYS_FILE}
    # Private key only needed on the SSH client side
    mv ${AUTH_KEYS_FILE}_tmp beaglecam-id_ecdsa
)
