#! /bin/bash

PATH=$PATH:.
export LD_LIBRARY_PATH=../lib/nvml/src/nondebug/:../build

LD_PRELOAD=../lib/jemalloc-4.5.0/lib/libjemalloc.so.2 ${@}
#LD_PRELOAD=../../shim/libshim/libshim.so:../lib/jemalloc-4.5.0/lib/libjemalloc.so.2 MLFS_PROFILE=1 ${@}
