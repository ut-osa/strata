#include "lease.h"

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

