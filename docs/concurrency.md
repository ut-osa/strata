# Concurrency

This document describes the current status and limitations of concurrent file
operations in Strata.

## SPDK Concurrency

Below we discuss the performance characteristics and scalability of SPDK
running with multiple threads and qpairs per process. This feature is enabled
by compiling both kernfs and libfs with `-DCONCURRENT`.

The current implementation (located in `strata/libfs/src/storage/spdk/async.c`)
    allocates half the available qpairs (or 1 qpair per processor core,
            whichever is smaller) for the libfs process and half the available
    qpairs for the kernfs process. qpairs cannot be modified concurrently, so
    mutual exclusion is maintained using `pthread_mutex_t`'s. Threads then use
    the first available qpair to queue their work in - completions are
    processed by locking each qpair and processing completions one qpair at a
    time.

In the current simple implementation, we cannot run filebench
(`strata/bench/filebench`) - filebench spawns two processes along with kernfs,
    which tries to allocate too many qpairs. The easy solution is to add a flag
    to `libspdk_init` to allow a process to specify the number of qpairs that
    it wants, but this isn't a scalable practice.

For the tests below, all digest optimizations were disabled (`-DDIGEST_OPT`
        disabled), and NVRAM was bypassed so that logs were written straight to
the SSD (kernfs/fs.c, line 984 - uncomment the line `dest_dev = g_ssd_dev;` to
        allow for NVM bypass testing).

### Concurrent Reads

For the following measurements, `iobench` was run with overall IO/thread of
1GB. To perform this measurement, run the following command:

``` strata/bench/micro $ sudo ./run.sh iobench sr 1G 16K $NUM_THREADS ```

![Graph of throughput from iobench running in sequential read mode.][sr]

We see a read throughput increase of approximately 40% scaling from one to two
threads. We were unable to measure throughput for more than two threads  due to
seemingly deterministic segmentation violations triggered in another part of
the Strata stack - it seems that there's a race on inode block list, but this
issue has yet to be fully investigated. This may be an issue with thread-safety
in other parts of the Strata pipeline.

### Concurrent Writes

For the following measurements, `iobench` was run with overall IO/thread of
4GB. To perform this measurement, run the following command:

``` strata/bench/micro $ sudo ./run.sh iobench sw 4G 16K $NUM_THREADS ```

![Graph of throughput from iobench running in sequential write mode.][sw]

There is no positive scaling on write throughput — it seems that there's a
bottleneck somewhere. Based on the log from kernfs, we suspect that the
bottleneck may be in the handling of digest requests (which is done
        synchronously in a single thread, and must be done synchronously).
There should be some investigation effort here to see if any other parts of the
kernfs pipeline can be optimized or made to run concurrently.

[sr]: img/iobench_sr_with_IO_size_of_16K.png
[sw]: img/iobench_sw_with_IO_size_of_16K.png
