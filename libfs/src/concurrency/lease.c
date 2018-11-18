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

void Acquire_lease(uint32_t inum, mlfs_time_t* expiration_time, char type)
{
  if (type != 'r' && type != 'w')
  {
    panic("unknown type");
  }

  // First time to try to get a read lease
  if ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0)
  {
    if (type == 'r')
    {
      *expiration_time = acquire_read_lease(inum);
    }
    else if (type == 'w')
    {
      *expiration_time = acquire_write_lease(inum);
    }
    return;
  }

  mlfs_time_t current_time;
  mlfs_get_time(&current_time);
  current_time.tv_usec += MLFS_LEASE_RENEW_THRESHOLD;
	if (timercmp(&current_time, expiration_time, <) == 0)
	{
		  // Re-acquire lease if it is time to renewal
		  do
      {
        if (type == 'r')
        {
          *expiration_time = acquire_read_lease(inum);
        }
        else if (type == 'w')
        {
          *expiration_time = acquire_write_lease(inum);
        }

        if ((*expiration_time).tv_sec < 0)
        {
            sleep(abs((*expiration_time).tv_sec));
        }
      } while ((*expiration_time).tv_sec < 0);
	}
}

