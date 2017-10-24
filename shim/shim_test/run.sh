#! /bin/bash

PATH=$PATH:.
LD_PRELOAD=../libshim/libshim.so LD_LIBRARY_PATH=/home/yjkwon/project/mlfs/libfs/ ${@}
