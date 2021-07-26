#!/bin/sh

TARGET_DIR=$1

cd ${TARGET_DIR}

cp -dR --preserve=mode,timestamps \
    ${PRJ_BINARIES_DIR}/lib ${PRJ_BINARIES_DIR}/usr .

rm -rf \
 etc lib32 media mnt opt root run tmp \
 usr/lib usr/lib32 usr/sbin usr/share \
 var \
