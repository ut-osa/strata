#! /bin/bash

rm -rf a.strace
#./run.sh ./filebench.mlfs -f fileserver_big_strata.f
./run.sh ./filebench.mlfs -f fileserver_strata.f
#./run.sh strace -ff ./filebench.mlfs -f varmail_mlfs.f 2> a.strace
