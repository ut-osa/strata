#include "shim_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
long if_call_defined (long int sys_no)
{
    return shim_table[sys_no] != 0;
}
*/
DEFINE_SHIM_SYSCALL (open, 3, shim_do_open, int, char *, file, int, flags,
                     mode_t, mode)

DEFINE_SHIM_SYSCALL (openat, 4, shim_do_openat, int, int, dfd,
		                     const char *, filename, int, flags, int, mode)

DEFINE_SHIM_SYSCALL (creat, 2, shim_do_creat, int, char *, file, mode_t, mode)

DEFINE_SHIM_SYSCALL (read, 3, shim_do_read, size_t, int, fd, void *, buf,
                     size_t, count)

DEFINE_SHIM_SYSCALL (pread64, 4, shim_do_pread64, size_t, int, fd, char *, buf,
                     size_t, count, loff_t, pos)

DEFINE_SHIM_SYSCALL (write, 3, shim_do_write, size_t, int, fd, void *,
                     buf, size_t, count)

DEFINE_SHIM_SYSCALL (pwrite64, 4, shim_do_pwrite64, size_t, int, fd, char *,
                     buf,  size_t, count, loff_t, pos)

DEFINE_SHIM_SYSCALL (close, 1, shim_do_close, int, int, fd)

DEFINE_SHIM_SYSCALL (lseek, 3, shim_do_lseek, off_t, int, fd, off_t, offset,
                     int, origin)

DEFINE_SHIM_SYSCALL (mkdir, 2, shim_do_mkdir, int, char *, pathname, int, mode)

DEFINE_SHIM_SYSCALL (rmdir, 1, shim_do_rmdir, int, const char *, pathname)

DEFINE_SHIM_SYSCALL (rename, 2, shim_do_rename, int, char *, newname, char *, oldname)

DEFINE_SHIM_SYSCALL (fallocate, 4, shim_do_fallocate, int, int, fd, int, mode, 
					off_t, offset, off_t, len)

DEFINE_SHIM_SYSCALL (stat, 2, shim_do_stat, int, const char *, file,
                     struct stat *, statbuf)

DEFINE_SHIM_SYSCALL (fstat, 2, shim_do_fstat, int, int, fd,
                     struct stat *, statbuf)

DEFINE_SHIM_SYSCALL (lstat, 2, shim_do_lstat, int, const char *, file,
                     struct stat *, statbuf)

DEFINE_SHIM_SYSCALL (truncate, 2, shim_do_truncate, int, const char *, filename,
                     off_t, length)
	
DEFINE_SHIM_SYSCALL (ftruncate, 2, shim_do_ftruncate, int, int, fd, off_t, length)

DEFINE_SHIM_SYSCALL (unlink, 1, shim_do_unlink, int, const char *, filename)

DEFINE_SHIM_SYSCALL (symlink, 2, shim_do_symlink, int, const char *, target, 
					const char *, linkpath)

DEFINE_SHIM_SYSCALL (access, 2, shim_do_access, int, const char *, pathname,
                    int, mode)

DEFINE_SHIM_SYSCALL (fsync, 1, shim_do_fsync, int, int, fd)

DEFINE_SHIM_SYSCALL (fdatasync, 1, shim_do_fdatasync, int, int, fd)

DEFINE_SHIM_SYSCALL (sync, 0, shim_do_sync, int)

DEFINE_SHIM_SYSCALL (fcntl, 3, shim_do_fcntl, int, int, fd, int, cmd, void *, arg)

DEFINE_SHIM_SYSCALL (mmap, 6, shim_do_mmap, void *, void *, addr, size_t, length, int, prot, 
					int, flags, int, fd, off_t, offset)

DEFINE_SHIM_SYSCALL (munmap, 2, shim_do_munmap, int, void *, addr, size_t, length)

DEFINE_SHIM_SYSCALL (getdents, 3, shim_do_getdents, size_t, int, fd,
		struct linux_dirent *, buf, size_t, count)

DEFINE_SHIM_SYSCALL (getdents64, 3, shim_do_getdents64, size_t, int, fd,
		struct linux_dirent64 *, buf, size_t, count)

/*
DEFINE_SHIM_SYSCALL (readv, 3, shim_do_readv, ssize_t, int, fd,
                     const struct iovec *, vec, int, vlen)

DEFINE_SHIM_SYSCALL (writev, 3, shim_do_writev, ssize_t, int, fd,
                     const struct iovec *, vec, int, vlen)
DEFINE_SHIM_SYSCALL (getcwd, 2, shim_do_getcwd, int, char *, buf, size_t, size)

DEFINE_SHIM_SYSCALL (chdir, 1, shim_do_chdir, int, const char *, filename)

DEFINE_SHIM_SYSCALL (fchdir, 1, shim_do_fchdir, int, int, fd)
DEFINE_SHIM_SYSCALL (symlink, 2, shim_do_symlink, int, const char *, old, const char *, new)

DEFINE_SHIM_SYSCALL (readlink, 3, shim_do_readlink, int, const char *, path,
char *, buf, int, bufsize)

DEFINE_SHIM_SYSCALL (chmod, 2, shim_do_chmod, int, const char *, filename,
mode_t, mode)

DEFINE_SHIM_SYSCALL (fchmod, 2, shim_do_fchmod, int, int, fd, mode_t, mode)

DEFINE_SHIM_SYSCALL (chown, 3, shim_do_chown, int, const char *, filename,
uid_t, user, gid_t, group)

DEFINE_SHIM_SYSCALL (fchown, 3, shim_do_fchown, int, int, fd, uid_t, user, gid_t, group)

DEFINE_SHIM_SYSCALL (lchown, 3, shim_do_lchown, int, const char *, filename,
uid_t, user, gid_t, group)

DEFINE_SHIM_SYSCALL (umask, 1, shim_do_umask, mode_t, mode_t, mask)
*/

// umask, dup, fcntl, ioctl

#ifdef __cplusplus
}
#endif
