#ifndef _KERN_FS_INTERFACE_
#define _KERN_FS_INTERFACE_
#define KERNFSPIDPATH "/tmp/kernfs.pid"

void init_fs(void);
void reset_kernfs_stats(void);
void show_kernfs_stats(void);
void shutdown_fs(void);
#endif
