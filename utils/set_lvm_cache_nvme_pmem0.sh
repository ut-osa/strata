#!/bin/bash

NVM_SIZE=34G
SSD_SIZE=350G

vgcreate vg /dev/nvme0n1 /dev/pmem0
lvcreate -n cache0 -L $NVM_SIZE vg /dev/pmem0
lvcreate -n cache0meta -L 300M vg /dev/pmem0
lvcreate -n lvol0 -L $SSD_SIZE vg /dev/nvme0n1

lvconvert --type cache-pool --poolmetadata vg/cache0meta vg/cache0
lvconvert --type cache --cachepool vg/cache0 --cachemode writeback vg/lvol0

#lvconvert --type cache --cachepool vg/cache0 vg/lvol0

echo "/dev/vg/lvol0 is available. You can format filesystem on the device"
