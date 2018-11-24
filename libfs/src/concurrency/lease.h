#ifndef _LEASE_H
#define _LEASE_H

#include <unistd.h>
#include <math.h>
#include "global/types.h"
#include "global/defs.h"
#include "filesystem/stat.h"
#include "ds/uthash.h"

// lease time in microseconds
#define MLFS_LEASE_SEC 10
#define MLFS_LEASE_USEC 0
#define MLFS_LEASE_RENEW_THRESHOLD 10  /* the threhold value to decide when to send renewal request to the kernfs */

#define MLFS_LEASE_ERR -1              /* Indicates we hit the error case we try to acquire lease */ 
#define MLFS_LEASE_GIVE_UP 1           /* Indicates we are failed to renewal lease (i.e., at one point, we give up the lease before reacquire) */
#define MLFS_LEASE_OK 0                /* Nothing happens during the renewal */

#define MLFS_LEASE_EXPIRATION_TIME_INITIALIZER { (0, 0) }
enum lease_action { acquire, release };
enum file_operation { mlfs_read_op, mlfs_write_op, mlfs_create_op, mlfs_delete_op, null_op};
typedef enum file_operation file_operation_t;
typedef enum lease_action lease_action_t;
typedef char inode_t; // can be T_FILE or T_DIR

struct mlfs_lease_call {
    lease_action_t action;
    const char* path;
    file_operation_t operation;
    inode_t type;
};

/*
 * We store the <inum, path> mapping for the lease use
 */
struct mlfs_lease_struct {
  int inum;          /* we'll use this field as key */
  char path[4097];
  UT_hash_handle hh; /* makes this structure hashable */
};

mlfs_time_t mlfs_acquire_lease(const char* path, file_operation_t operation, inode_t type);
void mlfs_release_lease(const char* path, file_operation_t operation, inode_t type);
int Acquire_lease(const char* path, mlfs_time_t* expiration_time, file_operation_t operation, inode_t type);


#endif
