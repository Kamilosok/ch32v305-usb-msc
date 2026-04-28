#! /bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 /dev/sdX"
    exit 1
fi

DEVICE=$1

sudo dd if=fs/smallest_fat12.img of="$DEVICE" bs=512 conv=notrunc
sync