#ifndef _SHIM_SYS_FS_
#define _SHIM_SYS_FS_

#define FD_START 1000000 //should be consistent with 
						 //FD_START in libfs/param.h

static inline int check_mlfs_fd(int fd)
{
	if (fd >= FD_START)
		return 1;
	else
		return 0;
}

static inline int get_mlfs_fd(int fd)
{
	if (fd >= FD_START)
		return fd - FD_START;
	else
		return fd;
}

#endif
