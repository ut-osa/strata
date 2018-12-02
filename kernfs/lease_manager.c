#include "lease_manager.h"
#include "assert.h"
#include "global/defs.h"
#include "filesystem/shared.h"
#include "concurrency/synchronization.h"
#include <stdlib.h>
#include <pthread.h>
mlfs_lease_t *mlfs_lease_global = NULL;
mlfs_mutex_t lease_lock;

mlfs_time_t lease_interval;
mlfs_time_t lease_error;

void init_lease_global()
{
    // global initialization of lease setup
    // works the same if called multiple times
    lease_interval.tv_sec = MLFS_LEASE_SEC;
    lease_interval.tv_usec = MLFS_LEASE_USEC;
    timerclear(&lease_error);
}

bool has_undeleted_subdir(mlfs_lease_t *lease)
{
    const char *path = lease->path;
    // if there is undeleted files that the process is unaware of, they should be delete / create the file
    mlfs_lease_t *it;
    int len = strlen(path);
    for (it = mlfs_lease_global; it != NULL; it = (mlfs_lease_t *)(it->hh.next))
    {
        if (it != lease && strlen(it->path) > len && strncmp(path, it->path, len) == 0 && it->last_op_stat != deleted)
        {
            return true;
        }
    }
    return false;
}

void log_time(const char *name, mlfs_time_t *val)
{
    printf("%s: %ld.%06ld\n", name, val->tv_sec, val->tv_usec);
}
mlfs_lease_t *get_lease(const char *path)
{
    mlfs_lease_t *lease;
    HASH_FIND_STR(mlfs_lease_global, path, lease);
    return lease;
}
mlfs_lease_t *get_or_create_lease(const char *path)
{
    mlfs_lease_t *lease;
    lease = get_lease(path);
    if (lease == NULL)
    {
        lease = (mlfs_lease_t *)malloc(sizeof *lease);
        lease->path = path;
        timerclear(&lease->read);
        timerclear(&lease->write);
        lease->last_op_stat = unknown;
        HASH_ADD_KEYPTR(hh, mlfs_lease_global, path, strlen(path), lease);
    }
    return lease;
}

mlfs_time_t acquire_read_lease(mlfs_lease_t *lease)
{
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
    if (timercmp(&current_time, &lease->write, <))
    {
        // if write lease has not expire, return its expire time to let the client try again
        mlfs_time_t expire_time = current_time;
        expire_time.tv_sec = -expire_time.tv_sec;
        return expire_time;
    }
    else
    {
        // read can proceed, return updated expire time to client
        timeradd(&current_time, &lease_interval, &lease->read);
        return lease->read;
    }
}

mlfs_time_t acquire_write_lease(mlfs_lease_t *lease)
{
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
    mlfs_time_t expire_time = timercmp(&lease->read, &lease->write, <) ? lease->write : lease->read;
    if (timercmp(&current_time, &expire_time, <))
    {
        // if write/read lease has not expire, return its expire time to let the client try again
        expire_time.tv_sec = -expire_time.tv_sec;
        return expire_time;
    }
    else
    {
        // write can proceed, return updated expire time to client
        timeradd(&current_time, &lease_interval, &lease->write);
        return lease->write;
    }
}

void release_read_lease(mlfs_lease_t *lease)
{
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
#ifdef DEBUG
    // check that the client must have acquired the lease before
    assert(lease->read.tv_sec != 0 && lease->read.tv_usec != 0);
#endif
    lease->read = current_time;
}

void release_write_lease(mlfs_lease_t *lease)
{
    mlfs_time_t current_time;
    assert(!gettimeofday(&current_time, NULL));
#ifdef DEBUG
    // check that the client must have acquired the lease before
    assert(lease->write.tv_sec != 0 && lease->write.tv_usec != 0);
#endif
    lease->write = current_time;
}

mlfs_time_t lease_acquire(const char *path, file_operation_t operation, inode_t type, pid_t client)
{
   // mlfs_info("Try to lock%c\n", ' ');
    mlfs_mutex_lock(&lease_lock);
    // mlfs_info("Lock successfully%c\n", ' ');
    mlfs_lease_t *lease = get_or_create_lease(path);
    mlfs_time_t ret;
    path_stat_t forbidden_stat;
    switch (operation)
    {
case mlfs_read_op:
        if (lease->last_op_client != client && lease->last_op_stat != unknown)
            ret = lease_error;
        else
            ret = acquire_read_lease(lease);
        break;
    case mlfs_write_op:
        // client can always write to a file
        // mlfs_info("Try to unlock%c\n", ' ');
        mlfs_mutex_unlock(&lease_lock);
        // mlfs_info("Unlock successfully%c\n", ' ');
        return acquire_write_lease(lease);
        break;
    case mlfs_create_op:
    case mlfs_delete_op:
        forbidden_stat = operation == mlfs_create_op ? created : deleted;
        // if the file type is directory we should check if there are files created by other
        // process that they are unaware of
        if (type == T_DIR && has_undeleted_subdir(lease)) {
            //mlfs_info("Try to unlock%c\n", ' ');
            mlfs_mutex_unlock(&lease_lock);
            //mlfs_info("Unlock successfully%c\n", ' ');
            return lease_error;
        }
        if (lease->last_op_client != client && lease->last_op_stat == forbidden_stat)
            ret = lease_error;
        else
            ret = acquire_write_lease(lease);
        break;
    default:
        ret = lease_error;
        break;
    }
    // mlfs_info("Try to unlock%c\n", ' ');
    mlfs_mutex_unlock(&lease_lock);
    // mlfs_info("Unlock successfully%c\n", ' ');
    return ret;
}
void lease_release(const char *path, file_operation_t operation, inode_t type, pid_t client)
{
    // mlfs_info("Try to lock%c\n", ' ');
    mlfs_mutex_lock(&lease_lock);
    // mlfs_info("Lock successfully%c\n", ' ');
    mlfs_lease_t *lease;
    lease = get_lease(path);
    if (lease == NULL)
    {
#ifdef DEBUG
        // check that the client must have acquired the lease before
        assert(false);
#endif
        // mlfs_info("Try to unlock%c\n", ' ');
        mlfs_mutex_unlock(&lease_lock);
        // mlfs_info("Unlock successfully%c\n", ' ');
        return;
    }
    switch (operation)
    {
    case mlfs_read_op:
        release_read_lease(lease);
        break;
    case mlfs_write_op:
        release_write_lease(lease);
        break;
    case mlfs_create_op:
        release_write_lease(lease);
        lease->last_op_stat = created;
        lease->last_op_client = client;
        break;
    case mlfs_delete_op:
        release_write_lease(lease);
        lease->last_op_stat = deleted;
        lease->last_op_client = client;
        break;
    default:
        break;
    }
    // mlfs_info("Try to unlock%c\n", ' ');
    mlfs_mutex_unlock(&lease_lock);
    // mlfs_info("Unlock sucessfully%c\n", ' ');
}
