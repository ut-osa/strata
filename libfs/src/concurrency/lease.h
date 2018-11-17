#ifndef _LEASE_H
#define _LEASE_H

#include "global/types.h"
#include <unistd.h>
#include <math.h>

mlfs_time_t acquire_read_lease(uint32_t inum);
mlfs_time_t acquire_write_lease(uint32_t inum);
void release_read_lease(uint32_t inum);
void release_write_lease(uint32_t inum);

mlfs_time_t Acquire_read_lease(uint32_t inum);
mlfs_time_t Acquire_write_lease(uint32_t inum);

#endif
