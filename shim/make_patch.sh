#! /bin/bash

diff -urN --recursive --no-dereference -x "tags" -x "cscope.*" glibc-2.19.org/ glibc-2.19 > a.patch
