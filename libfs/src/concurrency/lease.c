#include "lease.h"

struct mlfs_lease_struct *mlfs_lease_table = NULL;

mlfs_time_t mlfs_acquire_lease(const char* path, file_operation_t operation, inode_t type)
{
  //mlfs_info("mlfs_acquire_lease: %d\n", 0);
  mlfs_time_t expiration_time;
  mlfs_get_time(&expiration_time);
  expiration_time.tv_usec += 1000;
  return expiration_time;
}

void mlfs_release_lease(const char* path, file_operation_t operation, inode_t type)
{
  //mlfs_info("mlfs_release_lease: %d\n", 0);
}

int Acquire_lease(const char* path, mlfs_time_t* expiration_time, file_operation_t operation, inode_t type)
{
  int ret = MLFS_LEASE_OK;

  mlfs_time_t current_time;
  mlfs_get_time(&current_time);
  current_time.tv_usec += MLFS_LEASE_RENEW_THRESHOLD;
	if (timercmp(&current_time, expiration_time, <) == 0 || ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0))
	{
		  // Acquire lease if it is time to renewal or it is our first time to try to get a lease
		  do
      {
        *expiration_time = mlfs_acquire_lease(path, operation, type);

        if ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0)
        {
          // This means we hit the error
          ret = MLFS_LEASE_ERR;
        }

        if ((*expiration_time).tv_sec < 0)
        {
            ret = MLFS_LEASE_GIVE_UP;
            sleep(abs((*expiration_time).tv_sec));
        }
      } while ((*expiration_time).tv_sec < 0);
	}

  return ret;
}

