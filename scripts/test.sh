#!/bin/bash

set -e

# For added flair
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0;0m' # No Color

if [ -z "$1" ]; then
    echo "Usage: $0 /dev/sdX"
    exit 1
fi

DEVICE=$1
MOUNT_DIR="/mnt/usb_test_msc"
TEST_FILE="test_data.bin"
RESULT_FILE="result_data.bin"

# Erasing all data from the device
echo "Erasing"
sudo dd if=/dev/zero of="$DEVICE" bs=512 count=128
sync

# Filesystem
echo "Initiating FAT12"
sudo mkfs.fat "$DEVICE"

# Mounting and transferring data
echo "Data transfer"
sudo mkdir -p "$MOUNT_DIR"
sudo mount "$DEVICE" "$MOUNT_DIR"

dd if=/dev/urandom of="$TEST_FILE" bs=512 count=64 status=none
sudo cp "$TEST_FILE" "$MOUNT_DIR/"

# Forcing actual usage
echo "Saving"
sync
sudo umount "$MOUNT_DIR"
sudo fsck.vfat -v "$DEVICE"
sudo mount "$DEVICE" "$MOUNT_DIR"

# Checking for corruption
sudo cp "$MOUNT_DIR/$TEST_FILE" "$RESULT_FILE"
if cmp -s "$TEST_FILE" "$RESULT_FILE"; then
    echo -e "${GREEN}SUCCESS!${NC}"
else
    echo -e "${RED}ERROR: File corruption occurred${NC}"
    exit 1
fi

# Cleanup
echo "Cleanup"
sudo umount "$MOUNT_DIR"
sudo rmdir "$MOUNT_DIR"
rm "$TEST_FILE" "$RESULT_FILE"