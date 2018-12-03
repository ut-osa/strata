#ifndef _LEASE_H
#define _LEASE_H

#include <unistd.h>
#include <math.h>
#include "global/types.h"
#include "global/defs.h"
#include "filesystem/stat.h"
#include "filesystem/shared.h"
#include "ds/uthash.h"
#include "mlfs/mlfs_interface.h"

#define MLFS_LEASE_SEC 2               /* Time length of Lease */
#define MLFS_LEASE_USEC 0
#define MLFS_LEASE_RENEW_THRESHOLD 0   /* the threhold value to decide when to send renewal request to the kernfs */
#define MLFS_LEASE_PERCENT 30          /* percent parameter for `make_digest_request_sync` */
#define MLFS_LEASE_ERR -1              /* Indicates we hit the error case we try to acquire lease */ 
#define MLFS_LEASE_GIVE_UP 1           /* Indicates we are failed to renewal lease (i.e., at one point, we give up the lease before reacquire) */
#define MLFS_LEASE_OK 0                /* Nothing happens during the renewal */
#define MLFS_LEASE_POLL_TIME 50000     /* Determine the polling speed for trying to acquire lease again (us). Determined empirically */

// lease_client
#define data_size 4102
#define LEASE_CLIENT_NAME "/tmp/mlfs-lease.client"
#define LEASE_SERVER_NAME "/tmp/mlfs-lease.server"

#define MLFS_LEASE_EXPIRATION_TIME_INITIALIZER { (0, 0) }
enum lease_action { acquire = 0, release = 1 };
enum file_operation { mlfs_read_op = 0, mlfs_write_op = 1, mlfs_create_op = 2, mlfs_delete_op = 3, null_op};
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
void mlfs_release_lease_inum(uint32_t inum, file_operation_t operation, inode_t type);
int Acquire_lease(const char* path, mlfs_time_t* expiration_time, file_operation_t operation, inode_t type);
int Acquire_lease_inum(uint32_t inum, mlfs_time_t* expiration_time, file_operation_t operation, inode_t type);
void init_lease_client();
void shutdown_sock();
void lease_sleep(struct timeval expiration_time);
#endif
