#!/bin/sh

TARGET_DIR=$1

cd ${TARGET_DIR}

cp -dR --preserve=mode,timestamps ${PRJ_BINARIES_DIR}/lib .

sed -i "s|@OS_VER@|${PRJ_VERSION}|" \
    usr/lib/os-release \
    etc/banner

rm -rf \
    etc/issue \
    root/.ssh/beaglecam-id_ecdsa \
    usr/share/ffmpeg
