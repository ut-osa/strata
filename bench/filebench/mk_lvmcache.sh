#! /bin/bash

user=$(whoami)

if [ ! -L /dev/vg/lvol0 ]; then
	echo "LVM cache device does not exist"
	exit -1
fi

mkdir -p ./lvm_cache
#sudo mount /dev/vg/lvol0 ./lvm_cache
sudo mount -o sync /dev/vg/lvol0 ./lvm_cache
sudo chown $user ./lvm_cache
