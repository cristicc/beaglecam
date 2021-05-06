#!/bin/sh

TARGET_DIR=$1

cd $TARGET_DIR
rm -rf \
 etc root tmp var media mnt opt run \
 lib lib32 \
 sbin \
 usr/lib usr/lib32 usr/sbin usr/share \

