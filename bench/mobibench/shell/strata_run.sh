#! /bin/bash

PATH=$PATH:.

STRATA=../../..

export LD_LIBRARY_PATH=$STRATA/libfs/lib/nvml/src/nondebug/:$STRATA/libfs/build

LD_PRELOAD=$STRATA/shim/libshim/libshim.so:$STRATA/libfs/lib/jemalloc-4.5.0/lib/libjemalloc.so.2 ${@}
#LD_PRELOAD=../../shim/libshim/libshim.so:../lib/jemalloc-4.5.0/lib/libjemalloc.so.2 MLFS_PROFILE=1 ${@}
