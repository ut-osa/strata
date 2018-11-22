#ifndef _LEASE_H
#define _LEASE_H

#include <unistd.h>
#include <math.h>
#include "global/types.h"
#include "global/defs.h"
#include "filesystem/stat.h"

// lease time in microseconds
#define MLFS_LEASE_SEC 0
#define MLFS_LEASE_USEC 999999
#define MLFS_LEASE_RENEW_THRESHOLD 10  /* the threhold value to decide when to send renewal request to the kernfs */

#define MLFS_LEASE_ERR -1              /* Indicates we hit the error case we try to acquire lease */ 
#define MLFS_LEASE_GIVE_UP 1           /* Indicates we are failed to renewal lease (i.e., at one point, we give up the lease before reacquire) */
#define MLFS_LEASE_OK 0                /* Nothing happens during the renewal */

#define MLFS_LEASE_EXPIRATION_TIME_INITIALIZER { (0, 0) }

mlfs_time_t acquire_read_lease(uint32_t inum);
mlfs_time_t acquire_write_lease(uint32_t inum);
void release_read_lease(uint32_t inum);
void release_write_lease(uint32_t inum);

int Acquire_lease(uint32_t inum, mlfs_time_t* expiration_time, char type);

#endif
