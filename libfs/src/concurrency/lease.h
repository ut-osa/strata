#ifndef _LEASE_H
#define _LEASE_H

#include "global/types.h"
// lease time in microseconds
#define MLFS_LEASE_SEC 0
#define MLFS_LEASE_USEC 999999

mlfs_time_t acquire_read_lease(uint32_t inum);
mlfs_time_t acquire_write_lease(uint32_t inum);
void release_read_lease(uint32_t inum);
void release_write_lease(uint32_t inum);


#endif
