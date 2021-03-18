#! /bin/bash

if [ $# -ne 1 ]; then
  echo Usage: $0 device_mount_point >&2
  exit 1
fi

PATH=$PATH:$HOME/cross-compiler64/bin/

./build.sh &&
echo Waiting for SD card &&
while test \! -f $1/kernel8.img
do
	sleep 1
done &&
echo Copying to SD card &&
cp sdfat/kernel8.img $1/kernel8.img &&
sync &&
umount $1 &&
echo Done
