#ifndef _LEASE_H
#define _LEASE_H

#include "global/types.h"
// lease time in microseconds
#define MLFS_LEASE_SEC 10
#define MLFS_LEASE_USEC 0
enum lease_action { acquire, release };
enum file_operation { mlfs_read, mlfs_write, mlfs_create, mlfs_delete, null_op};
typedef char inode_t;

struct mlfs_lease_call {
    lease_action action;
    uint32_t inum;
    file_operation operation;
    inode_t type;
};

mlfs_time_t mlfs_acquire_lease(uint32_t inum, file_operation operation, inode_t type);
void mlfs_release_lease(uint32_t inum, file_operation operation, inode_t type);

#endif
