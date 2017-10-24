#! /bin/bash

mkdir -p ./ramfs

sudo mount -t tmpfs -o size=6G tmpfs ./ramfs
