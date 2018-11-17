#include "lease.h"

mlfs_time_t acquire_read_lease(uint32_t inum)
{
    //mlfs_info("acquire_read_lease: %d\n", 0);
    mlfs_time_t expiration_time;
    mlfs_get_time(&expiration_time);
    expiration_time.tv_usec += 1000;
    return expiration_time;
}

mlfs_time_t acquire_write_lease(uint32_t inum)
{
    mlfs_time_t expiration_time;
    mlfs_get_time(&expiration_time);
    expiration_time.tv_usec += 1200;
    return expiration_time;
}

void release_write_lease(uint32_t inum)
{
     //mlfs_info("release_write_lease: %d\n", 0);
}

void 
release_read_lease(uint32_t inum)
{
    //mlfs_info("release_read_lease: %d\n", 0);
}

mlfs_time_t Acquire_read_lease(uint32_t inum)
{
    mlfs_time_t expiration_time;
    
    do
    {
        expiration_time = acquire_read_lease(inum);
        if (expiration_time.tv_sec < 0)
        {
            sleep(abs(expiration_time.tv_sec));
        }
    } while (expiration_time.tv_sec < 0);
    
    return expiration_time;
}

mlfs_time_t Acquire_write_lease(uint32_t inum)
{
    mlfs_time_t expiration_time;
    
    do
    {
        expiration_time = acquire_write_lease(inum);
        if (expiration_time.tv_sec < 0)
        {
            sleep(abs(expiration_time.tv_sec));
        }
    } while (expiration_time.tv_sec < 0);
    
    return expiration_time;
}

