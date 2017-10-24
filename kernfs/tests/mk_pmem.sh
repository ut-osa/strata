#! /bin/bash

user=$(whoami)
pmem_root=./../pmem_storage/

sudo mkfs.ext4 /dev/pmem0
mkdir -p ./pmem
sudo mount -t ext4 -o dax /dev/pmem0 $pmem_root
sudo chown -R $user $pmem_root
