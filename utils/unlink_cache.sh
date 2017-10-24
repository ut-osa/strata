#!/bin/bash

umount /dev/vm/lvol0 

yes | vgremove vg
#lvconvert --splitcache vg/lvol0
