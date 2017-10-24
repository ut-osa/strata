#ifndef _SHIM_SYSCALL_H_
#define _SHIM_SYSCALL_H_

#include <global/types.h>
#include "shim_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if 0
#include "shim_syscall_macro.h"
// Create declarations
#define DECLARE_SYSCALL_STUB(name, args ...) \
	SHIM_ARG_TYPE __shim_##name(args);

/*
#define DECLARE_SYSCALL_HANDLER(name, func, ret_type, args ...) \
	ret_type func(args);
*/

#define DECLARE_SHIM_SYSCALL(name, n, func, ret_type, args ...) \
	DECLARE_SYSCALL_STUB(name, SHIM_PROTO_ARGS_##n) 
	//DECLARE_SYSCALL_HANDLER(name, func, ret_type, args ...)

#include "define_shim_syscalls.h"
//#undef DECLARE_SHIM_SYSCALL
#endif

// TODO: Create macro to generate the declarations
// for system call interpose macro
long __shim_open(long __arg1, long __arg2, long __arg3);
long __shim_openat(long __arg1, long __arg2, long __arg3, long __arg4);
long __shim_creat(long __arg1, long __arg2);
long __shim_close(long __arg1);
long __shim_read(long __arg1, long __arg2, long __arg3);
long __shim_pread64(long __arg1, long __arg2, long __arg3, long __arg4);
long __shim_write(long __arg1, long __arg2, long __arg3);
long __shim_pwrite64(long __arg1, long __arg2, long __arg3, long __arg4);
long __shim_lseek(long __arg1, long __arg2, long __arg3);
long __shim_mkdir(long __arg1, long __arg2);
long __shim_rmdir(long __arg1);
long __shim_rename(long __arg1, long __arg2);
long __shim_fallocate(long __arg1, long __arg2, long __arg3, long __arg4);
long __shim_stat(long __arg1, long __arg2);
long __shim_fstat(long __arg1, long __arg2);
long __shim_lstat(long __arg1, long __arg2);
long __shim_truncate(long __arg1, long __arg2);
long __shim_ftruncate(long __arg1, long __arg2);
long __shim_unlink(long __arg1);
long __shim_symlink(long __arg1, long __arg2);
long __shim_access(long __arg1, long __arg2);
long __shim_sync(void);
long __shim_fsync(long __arg1);
long __shim_fdatasync(long __arg1);
long __shim_fcntl(long __arg1, long __arg2, long __arg3);
long __shim_mmap(long __arg1, long __arg2, long __arg3, long __arg4, 
				long __arg5, long __arg6);
long __shim_munmap(long __arg1, long __arg2);
long __shim_getdents(long __arg1, long __arg2, long __arg3);
long __shim_getdents64(long __arg1, long __arg2, long __arg3);

// Actual system call handlers.
int shim_do_open(const char * file, int flags, mode_t mode);
int shim_do_openat(int dfd, const char *filename, int flags, mode_t mode);
int shim_do_creat(const char * file, mode_t mode);
size_t shim_do_read(int fd, void *buf, size_t count);
size_t shim_do_pread64(int fd, void *buf, size_t count, loff_t pos);
size_t shim_do_write(int fd, const void *buf, size_t count);
size_t shim_do_pwrite64(int fd, const void *buf, size_t count, loff_t pos);
int shim_do_mkdir(void *buf, mode_t count);
int shim_do_rmdir(const char *pathname);
int shim_do_rename(char *oldname, char *newname);
int shim_do_lseek(int fd, off_t offset, int origin);
int shim_do_close(int fd);
int shim_do_fallocate(int fd, int mode, off_t offset, off_t len);
int shim_do_posix_fallocate(int fd, off_t offset, off_t len);
int shim_do_stat(const char *filename, struct stat *statbuf);
int shim_do_lstat(const char *filename, struct stat *statbuf);
int shim_do_fstat(int fd, struct stat *statbuf);
int shim_do_truncate(const char *filename, off_t length);
int shim_do_ftruncate(int fd, off_t length);
int shim_do_unlink(const char *filename);
int shim_do_symlink(const char *target, const char *linkpath);
int shim_do_access(const char *pathname, int mode);
int shim_do_fsync(int fd);
int shim_do_sync(void);
int shim_do_fdatasync(int fd);
// follow format of glibc do_fcntl at sysdeps/unix/sysv/linux/fcntl.c
int shim_do_fcntl(int fd, int cmd, void *arg);
void *shim_do_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int shim_do_munmap(void *addr, size_t length);
size_t shim_do_getdents(int fd, struct linux_dirent *buf, size_t count);
size_t shim_do_getdents64(int fd, struct linux_dirent64 *buf, size_t count);

#ifdef __cplusplus
}
#endif

#endif
