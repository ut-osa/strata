#ifndef _LEASE_MANAGER_H
#define _LEASE_MANAGER_H

#include "concurrency/lease.h"
#include "ds/uthash.h"
#include "global/types.h"
#include <sys/time.h>
enum path_stat
{
    unknown,
    created,
    deleted
};
typedef enum path_stat path_stat_t;
void log_time(const char *name, mlfs_time_t *val);
void init_lease_global();

struct mlfs_lease
{
    mlfs_hash_t hh;
    const char *path; /* key */
    mlfs_time_t read;
    mlfs_time_t write;
    path_stat_t last_op_stat;
    pid_t last_op_client;
};
typedef struct mlfs_lease mlfs_lease_t;

mlfs_time_t lease_acquire(const char *path, file_operation_t operation, inode_t type, pid_t client);
void lease_release(const char *path, file_operation_t operation, inode_t type, pid_t client);

#endif
