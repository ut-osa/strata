#! /bin/bash

PATH=$PATH:.
SRC_ROOT=../../
export LD_LIBRARY_PATH=$SRC_ROOT/libfs/lib/nvml/src/nondebug/:$SRC_ROOT/libfs/build:/usr/local/lib/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/

$SRC_ROOT/libfs/bin/mkfs.mlfs $@
