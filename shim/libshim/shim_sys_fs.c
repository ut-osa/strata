#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <mlfs/mlfs_interface.h>
#include <posix/posix_interface.h>
#include <global/types.h>

#include "interfaces.h"
#include "shim_types.h"
#include "shim_syscall_macro.h"
#include "shim_sys_fs.h"


/* System call ABIs
 * syscall number : %eax
 * parameter sequence: %rdi, %rsi, %rdx, %rcx, %r8, %r9
 * Kernel destroys %rcx, %r11
 * return : %rax
 */

#define PATH_BUF_SIZE 4095
#define MLFS_PREFIX (char *)"/mlfs"

#ifdef __cplusplus
extern "C" {
#endif

static int collapse_name(const char *input, char *_output)
{
	char *output = _output;

	while(1) {
		/* Detect a . or .. component */
		if (input[0] == '.') {
			if (input[1] == '.' && input[2] == '/') {
				/* A .. component */
				if (output == _output)
					return -1;
				input += 2;
				while (*(++input) == '/');
				while(--output != _output && *(output - 1) != '/');
				continue;
			} else if (input[1] == '/') {
				/* A . component */
				input += 1;
				while (*(++input) == '/');
				continue;
			}
		}

		/* Copy from here up until the first char of the next component */
		while(1) {
			*output++ = *input++;
			if (*input == '/') {
				*output++ = '/';
				/* Consume any extraneous separators */
				while (*(++input) == '/');
				break;
			} else if (*input == 0) {
				*output = 0;
				return output - _output;
			}
		}
	}
}

int shim_do_open(char *filename, int flags, mode_t mode)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(filename, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		ret = mlfs_posix_open(filename, flags, mode);

		if (!check_mlfs_fd(ret)) {
			printf("incorrect fd %d: file %s\n", ret, filename);
		}

		syscall_trace(__func__, ret, 3, filename, flags, mode);
			
		return ret;
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%esi;"
		"mov %3, %%edx;"
		"mov %4, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(filename), "r"(flags), "r"(mode), "r"(__NR_open)
		:"rax", "rdi", "rsi", "rdx"
		);

	return ret;
}

int shim_do_openat(int dfd, const char *filename, int flags, mode_t mode)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(filename, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		//printf("%s -> MLFS \n", path_buf);

		if (dfd != AT_FDCWD) {
			fprintf(stderr, "Only support AT_FDCWD\n");
			exit(-1);
		}

		ret = mlfs_posix_open((char *)filename, flags, mode);

		if (!check_mlfs_fd(ret)) {
			printf("incorrect fd %d: file %s\n", ret, filename);
		}

		syscall_trace(__func__, ret, 4, filename, dfd, flags, mode);
			
		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%edx;"
		"mov %4, %%r10d;"
		"mov %5, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(dfd),"r"(filename), "r"(flags), "r"(mode), "r"(__NR_openat)
		:"rax", "rdi", "rsi", "rdx", "r10"
		);

	return ret;
}

int shim_do_creat(char *filename, mode_t mode)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(filename, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		ret = mlfs_posix_creat(filename, mode);

		if (!check_mlfs_fd(ret)) {
			printf("incorrect fd %d\n", ret);
		}
			
		syscall_trace(__func__, ret, 2, filename, mode);

		return ret;
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%esi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(filename), "r"(mode), "r"(__NR_creat)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

size_t shim_do_read(int fd, void *buf, size_t count)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_read(get_mlfs_fd(fd), buf, count);
		syscall_trace(__func__, ret, 3, fd, buf, count);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%rdx;"
		"mov %4, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "m"(buf), "r"(count), "r"(__NR_read)
		:"rax", "rdi", "rsi", "rdx"
		);

	return ret;
}

size_t shim_do_pread64(int fd, void *buf, size_t count, loff_t off)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_pread64(get_mlfs_fd(fd), buf, count, off);
		syscall_trace(__func__, ret, 4, fd, buf, count, off);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%rdx;"
		"mov %4, %%r10;"
		"mov %5, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "m"(buf), "r"(count), "r"(off), "r"(__NR_pread64)
		:"rax", "rdi", "rsi", "rdx", "r10"
		);

	return ret;
}

size_t shim_do_write(int fd, void *buf, size_t count)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_write(get_mlfs_fd(fd), buf, count);
		syscall_trace(__func__, ret, 3, fd, buf, count);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%rdx;"
		"mov %4, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "m"(buf), "r"(count), "r"(__NR_write)
		:"rax", "rdi", "rsi", "rdx"
		);

	return ret;
}

size_t shim_do_pwrite64(int fd, void *buf, size_t count, loff_t off)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		/*
		ret = mlfs_posix_pwrite64(get_mlfs_fd(fd), buf, count, off);
		syscall_trace(__func__, ret, 4, fd, buf, count, off);

		return ret;
		*/
		printf("%s: does not support yet\n", __func__);
		exit(-1);
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%rdx;"
		"mov %4, %%r10;"
		"mov %5, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "m"(buf), "r"(count),"r"(off), "r"(__NR_pwrite64)
		:"rax", "rdi", "rsi", "rdx", "r10"
		);

	return ret;
}

int shim_do_close(int fd)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_close(get_mlfs_fd(fd));
		syscall_trace(__func__, ret, 1, fd);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "r"(__NR_close)
		:"rax", "rdi"
		);

	return ret;
}

int shim_do_lseek(int fd, off_t offset, int origin)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_lseek(get_mlfs_fd(fd), offset, origin);
		syscall_trace(__func__, ret, 3, fd, offset, origin);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%edx;"
		"mov %4, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "r"(offset), "r"(origin), "r"(__NR_lseek)
		:"rax", "rdi", "rsi", "rdx"
		);

	return ret;
}

int shim_do_mkdir(void *path, mode_t mode)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];

	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name((char *)path, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		;
	} else {
		//printf("%s: go to mlfs\n", path_buf);
		ret = mlfs_posix_mkdir(path_buf, mode);
		syscall_trace(__func__, ret, 2, path, mode);

		return ret;
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%esi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(path), "r"(mode), "r"(__NR_mkdir)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_rmdir(const char *path)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];

	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(path, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		;
	} else {
		ret = mlfs_posix_rmdir((char *)path);
		return ret;
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(path), "r"(__NR_rmdir)
		:"rax", "rdi"
		);

	return ret;
}

int shim_do_rename(char *oldname, char *newname)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(oldname, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		ret = mlfs_posix_rename(oldname, newname);
		syscall_trace(__func__, ret, 2, oldname, newname);

		return ret;
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(oldname), "m"(newname), "r"(__NR_rename)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_fallocate(int fd, int mode, off_t offset, off_t len)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_fallocate(get_mlfs_fd(fd), offset, len);
		syscall_trace(__func__, ret, 4, fd, mode, offset, len);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%esi;"
		"mov %3, %%rdx;"
		"mov %4, %%r10;"
		"mov %5, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "r"(mode), "r"(offset), "r"(len), "r"(__NR_fallocate)
		:"rax", "rdi", "rsi", "rdx", "r10"
		);

	return ret;
}

int shim_do_stat(const char *filename, struct stat *statbuf)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(filename, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		ret = mlfs_posix_stat(filename, statbuf);
		syscall_trace(__func__, ret, 2, filename, statbuf);

		return ret;
	}
			
	asm("mov %1, %%rdi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(filename), "m"(statbuf), "r"(__NR_stat)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_lstat(const char *filename, struct stat *statbuf)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(filename, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		// Symlink does not implemented yet
		// so stat and lstat is identical now.
		ret = mlfs_posix_stat(filename, statbuf);
		syscall_trace(__func__, ret, 2, filename, statbuf);

		return ret;
	}
			
	asm("mov %1, %%rdi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(filename), "m"(statbuf), "r"(__NR_lstat)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_fstat(int fd, struct stat *statbuf)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_fstat(get_mlfs_fd(fd), statbuf);
		syscall_trace(__func__, ret, 2, fd, statbuf);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "m"(statbuf), "r"(__NR_fstat)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_truncate(const char *filename, off_t length)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(filename, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		ret = mlfs_posix_truncate(filename, length);
		syscall_trace(__func__, ret, 2, filename, length);

		return ret;
	}
			
	asm("mov %1, %%rdi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(filename), "r"(length), "r"(__NR_truncate)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_ftruncate(int fd, off_t length)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_ftruncate(get_mlfs_fd(fd), length);
		syscall_trace(__func__, ret, 2, fd, length);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "r"(length), "r"(__NR_ftruncate)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_unlink(const char *path)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];

	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(path, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		;
	} else {
		ret = mlfs_posix_unlink(path);
		syscall_trace(__func__, ret, 1, path);
		return ret;
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(path), "r"(__NR_unlink)
		:"rax", "rdi"
		);

	return ret;
}

int shim_do_symlink(const char *target, const char *linkpath)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(target, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		printf("%s\n", target);
		printf("symlink: do not support yet\n");
		exit(-1);
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(target), "m"(linkpath), "r"(__NR_symlink)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_access(const char *pathname, int mode)
{
	int ret;
	char path_buf[PATH_BUF_SIZE];
	
	memset(path_buf, 0, PATH_BUF_SIZE);
	collapse_name(pathname, path_buf);

	if (strncmp(path_buf, MLFS_PREFIX, 5) != 0){
		//printf("%s : will not go to libfs\n", path_buf);
		;
		// fall through
	} else {
		ret = mlfs_posix_access((char *)pathname, mode);
		syscall_trace(__func__, ret, 2, pathname, mode);
		
		return ret;
	}
			
	asm("mov %1, %%rdi;"
		"mov %2, %%esi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(pathname), "r"(mode), "r"(__NR_access)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

int shim_do_fsync(int fd)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		// libfs has quick persistency guarantee. 
		// fsync is nop.
		return 0;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "r"(__NR_fsync)
		:"rax", "rdi"
		);

	return ret;
}

int shim_do_fdatasync(int fd)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		// fdatasync is nop.
		syscall_trace(__func__, 0, 1, fd);

		return 0;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "r"(__NR_fdatasync)
		:"rax", "rdi"
		);

	return ret;
}

int shim_do_sync(void)
{
	int ret;

	printf("sync: do not support yet\n");
	exit(-1);

	asm("mov %1, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(__NR_sync)
		:"rax" 
		);

	return ret;
}

int shim_do_fcntl(int fd, int cmd, void *arg)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_fcntl(get_mlfs_fd(fd), cmd, arg);
		syscall_trace(__func__, ret, 3, fd, cmd, arg);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%esi;"
		"mov %3, %%rdx;"
		"mov %4, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "r"(cmd), "m"(arg), "r"(__NR_fcntl)
		:"rax", "rdi", "rsi", "rdx"
		);

	return ret;
}

void* shim_do_mmap(void *addr, size_t length, int prot, 
		int flags, int fd, off_t offset)
{
	void* ret;

	if (check_mlfs_fd(fd)) {
		printf("mmap: not implemented\n");
		exit(-1);
	}

	asm("mov %1, %%rdi;"
		"mov %2, %%rsi;"
		"mov %3, %%edx;"
		"mov %4, %%r10d;"
		"mov %5, %%r8d;"
		"mov %6, %%r9;"
		"mov %7, %%eax;"
		"syscall;\n\t"
		"mov %%rax, %0;\n\t"
		:"=r"(ret)
		:"m"(addr), "r"(length), "r"(prot), "r"(flags), "r"(fd), "r"(offset), "r"(__NR_mmap)
		:"rax", "rdi", "rsi", "rdx", "r10", "r8", "r9"
		);

	return ret;
}

int shim_do_munmap(void *addr, size_t length)
{
	int ret;

	asm("mov %1, %%rdi;"
		"mov %2, %%rsi;"
		"mov %3, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"m"(addr), "r"(length), "r"(__NR_munmap)
		:"rax", "rdi", "rsi"
		);

	return ret;
}

size_t shim_do_getdents(int fd, struct linux_dirent *buf, size_t count)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		ret = mlfs_posix_getdents(get_mlfs_fd(fd), buf, count);

		syscall_trace(__func__, ret, 3, fd, buf, count);

		return ret;
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%rdx;"
		"mov %4, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "m"(buf), "r"(count), "r"(__NR_getdents)
		:"rax", "rdi", "rsi", "rdx", "r10"
		);

	return ret;
}

size_t shim_do_getdents64(int fd, struct linux_dirent64 *buf, size_t count)
{
	int ret;

	if (check_mlfs_fd(fd)) {
		printf("getdent64 is not supported\n");
		exit(-1);
	}

	asm("mov %1, %%edi;"
		"mov %2, %%rsi;"
		"mov %3, %%rdx;"
		"mov %4, %%eax;"
		"syscall;\n\t"
		"mov %%eax, %0;\n\t"
		:"=r"(ret)
		:"r"(fd), "m"(buf), "r"(count), "r"(__NR_getdents)
		:"rax", "rdi", "rsi", "rdx", "r10"
		);

	return ret;
}

#ifdef __cplusplus
}
#endif
