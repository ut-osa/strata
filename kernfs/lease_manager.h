#ifndef _LEASE_MANAGER_H
#define _LEASE_MANAGER_H

#include <sys/time.h>
#include "ds/uthash.h"
#include "global/types.h"
#include "concurrency/lease.h"

void log_time(const char* name, mlfs_time_t *val);

void init_lease_interval();
struct mlfs_lease {
    mlfs_hash_t hh;
    uint32_t inum; /* key */
    mlfs_time_t read;
    mlfs_time_t write;
};
typedef struct mlfs_lease mlfs_lease_t;

#endif
