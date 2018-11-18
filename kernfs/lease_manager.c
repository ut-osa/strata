#include "lease_manager.h"
#include "assert.h"
#include "global/defs.h"
#include <stdlib.h>
mlfs_lease_t* mlfs_lease_global = NULL;
mlfs_time_t lease_interval;

void init_lease_interval()
{
    lease_interval.tv_sec = MLFS_LEASE_SEC;
    lease_interval.tv_usec = MLFS_LEASE_USEC;
}

void log_time(const char* name, mlfs_time_t *val) {
   printf("%s: %ld.%06ld\n", name, val->tv_sec, val->tv_usec);
}

mlfs_lease_t* insert_or_create_lease(uint32_t inum)
{
    mlfs_lease_t* lease;
    HASH_FIND_INT(mlfs_lease_global, &inum, lease);
    if (lease == NULL) {
        lease = (mlfs_lease_t*)malloc(sizeof *lease);
        lease->inum = inum;
        timerclear(&lease->read);
        timerclear(&lease->write);
        HASH_ADD_INT(mlfs_lease_global, inum, lease);
    }
    return lease;
}

mlfs_time_t acquire_read_lease(uint32_t inum)
{
    mlfs_lease_t* lease = insert_or_create_lease(inum);
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
    if (timercmp(&current_time, &lease->write, <)) {
        // if write lease has not expire, return its expire time to let the client try again
        mlfs_time_t expire_time = current_time;
        expire_time.tv_sec = -expire_time.tv_sec;
        return expire_time;
    } else {
        // read can proceed, return updated expire time to client
        timeradd(&current_time, &lease_interval, &lease->read);
        return lease->read;
    }
}

mlfs_time_t acquire_write_lease(uint32_t inum)
{
    mlfs_lease_t* lease = insert_or_create_lease(inum);
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
    mlfs_time_t expire_time = timercmp(&lease->read, &lease->write, <) ? lease->write : lease->read;
    if (timercmp(&current_time, &expire_time, <)) {
        // if write/read lease has not expire, return its expire time to let the client try again
        expire_time.tv_sec = -expire_time.tv_sec;
        return expire_time;
    } else {
        // write can proceed, return updated expire time to client
        timeradd(&current_time, &lease_interval, &lease->write);
        return lease->write;
    }
}

void release_read_lease(uint32_t inum)
{
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
    mlfs_lease_t* lease = insert_or_create_lease(inum);
#ifdef DEBUG
    // check that the client must have acquired the lease before
    assert(lease->read.tv_sec != 0 && lease->read.tv_usec != 0);
#endif
    lease->read = current_time;
}

void release_write_lease(uint32_t inum)
{
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
    mlfs_lease_t* lease = insert_or_create_lease(inum);
#ifdef DEBUG
    // check that the client must have acquired the lease before
    assert(lease->write.tv_sec != 0 && lease->write.tv_usec != 0);
#endif
    lease->write = current_time;
}
