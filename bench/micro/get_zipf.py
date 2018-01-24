#! /usr/bin/python

import sys
import numpy as np

io_size = 4 # in KB

def gen_zipf(file_size_mb):
    s = np.random.zipf(2, (long(file_size_mb) * 1024) / io_size)
    write_ratio = float(sys.argv[2])

    for l in s:
        print l * io_size * 1024
        op = np.random.random_sample()
        print "write" if op < write_ratio else "read"

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print "usage: %s <file size in MB> <write ratio eg. 0.2>" % sys.argv[0]
        exit()

    gen_zipf(sys.argv[1])
