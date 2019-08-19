#! /bin/bash

PATH=$PATH:.

STRATA=../..
LIBFS=$STRATA/libfs

export LD_LIBRARY_PATH=$LIBFS/lib/nvml/src/nondebug/:$LIBFS/build:$LIBFS/src/storage/spdk/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/

LD_PRELOAD=$STRATA/shim/libshim/libshim.so:$LIBFS/lib/jemalloc-4.5.0/lib/libjemalloc.so.2 ${@}
#LD_PRELOAD=../../shim/libshim/libshim.so:../lib/jemalloc-4.5.0/lib/libjemalloc.so.2 MLFS_PROFILE=1 ${@}
