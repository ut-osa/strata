//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

#include "global/global.h"
#include "global/types.h"
#include "filesystem/stat.h"
#include "filesystem/fs.h"
#include "filesystem/file.h"
#include "log/log.h"
#include "ds/uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _min(a, b) ({\
		__typeof__(a) _a = a;\
		__typeof__(b) _b = b;\
		_a < _b ? _a : _b; })

/* note for this posix handlers' (syscall handler) return value and errno.
 * glibc checks syscall return value in INLINE_SYSCALL macro.
 * if the return value is negative, it sets the value as errno 
 * (after turnning to positive one) and returns -1 to application.
 * Therefore, the return value must be correct -errno in posix semantic 
 */

#define SET_MLFS_FD(fd) fd + g_fd_start
#define GET_MLFS_FD(fd) fd - g_fd_start

#if 0
// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp)
{
	int off;
	struct dirent de;

	for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
		if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
			panic("isdirempty: readi");
		if(de.inum != 0)
			return 0;
	}
	return 1;
}
#endif

int mlfs_posix_open(char *path, int flags, uint16_t mode)
{
  //mlfs_info("Enter mlfs_posix_open %c\n", ' ');
  //fflush(stdout);
  struct file *f;
  struct inode *inode;
  int fd;
  extern struct mlfs_lease_struct *mlfs_lease_table;
  mlfs_time_t expiration_time = MLFS_LEASE_EXPIRATION_TIME_INITIALIZER;
  file_operation_t operation;
  inode_t type;
  int lease_ret;

  mlfs_info("start_log_tx() %c\n", ' ');
  fflush(stdout);  
  start_log_tx();

  if (flags & O_CREAT)
  {
    if (flags & O_DIRECTORY)
    {
      panic("O_DIRECTORY cannot be set with O_CREAT\n");      
    }
       
    //mlfs_info("mlfs_object_create() %c\n", ' ');
    fflush(stdout);    
    operation = mlfs_create_op;
    type = T_FILE;
    lease_ret = Acquire_lease(path, &expiration_time, operation, type);
    if (lease_ret == MLFS_LEASE_ERR)
    {
      mlfs_info("File is re-created or deleted by other processes%c", ' ');
      fflush(stdout);      
      return lease_ret;
    }

    inode = mlfs_object_create(path, T_FILE);
                
    mlfs_info("create file %s - inum %u\n", path, inode->inum);
    fflush(stdout);

    if (!inode) {
      commit_log_tx();
      //mlfs_info("inum after: %p\n", inode);
      mlfs_release_lease(path, operation, type);
      return -ENOENT;
    }
  }
  else
  {
    //mlfs_info("mlfs_posix_open %s\n", "1992");    
    //fflush(stdout);
    operation = mlfs_read_op;
    // opendir API
    if (flags & O_DIRECTORY)
    {
      // Fall through..
      // it is OK to return fd for directory. glibc allocates 
      // DIR structure and fill it with fd and result from stats. 
      // check: sysdeps/posix/opendir.c
      //mlfs_info("mlfs_posix_open %s\n", "1991");
      //fflush(stdout);      
      type = T_DIR;
      lease_ret = Acquire_lease(path, &expiration_time, operation, type);
      if (lease_ret == MLFS_LEASE_ERR) {
        mlfs_info("File is re-created or deleted by other processes%c", ' ');
        fflush(stdout);        
        return lease_ret;
      }          
    }
    else
    {
      //mlfs_info("mlfs_posix_open %s\n", "1990");
      fflush(stdout);            
      type = T_FILE;
      lease_ret = Acquire_lease(path, &expiration_time, operation, type);
      if (lease_ret == MLFS_LEASE_ERR) {
        mlfs_info("File is re-created or deleted by other processes%c", ' ');
        fflush(stdout);        
        return lease_ret;
      }
    }

    if ((inode = namei(path)) == NULL) {
      commit_log_tx();
      //mlfs_info("before return %c\n", '1');
      //fflush(stdout);      
      if (flags & O_DIRECTORY)
      {
        type = T_DIR;
        mlfs_release_lease(path, operation, type);
      }
      else
      {
        type = T_FILE;
        mlfs_release_lease(path, operation, T_FILE);
      }
      return -ENOENT;
    }

    if (inode->itype == T_DIR) {
      if (!(flags |= (O_RDONLY|O_DIRECTORY))) {
        commit_log_tx();
        //mlfs_info("before return %c\n", '2');
        //fflush(stdout);        
        mlfs_release_lease(path, operation, T_DIR);
	return -EACCES;
      }
    }
  }

  //mlfs_info("mlfs_posix_open %s\n", "1990");
  //fflush(stdout);      
  f = mlfs_file_alloc();

  if (f == NULL) {
    iunlockput(inode);
    commit_log_tx();
    //mlfs_info("before return %c\n", '3');
    //fflush(stdout);    
    mlfs_release_lease(path, operation, type);
    return -ENOMEM;
  }

  fd = f->fd;

  mlfs_info("open file %s inum %u fd %d\n", path, inode->inum, fd);
  fflush(stdout);
  commit_log_tx();

  pthread_rwlock_wrlock(&f->rwlock);

  if (flags & O_DIRECTORY) {
	mlfs_debug("directory file inum %d\n", inode->inum);
	f->type = FD_DIR;
  } else {
	f->type = FD_INODE;
  }

  f->ip = inode;
  f->readable = !(flags & O_WRONLY);
  f->writable = (flags & O_WRONLY) || (flags & O_RDWR);

  f->off = 0;

/* TODO: set inode permission based the mode 
if (mode & S_IRUSR)
	// Set read permission
if (mode & S_IWUSR)
	// Set write permission
*/

  pthread_rwlock_unlock(&f->rwlock);

  // We add the opened file's <inum, path> to the mlfs_lease_table
  struct mlfs_lease_struct *s;
  s = malloc(sizeof(struct mlfs_lease_struct));
  s->inum = f->ip->inum;
  strcpy(s->path, path);
  HASH_ADD_INT(mlfs_lease_table, inum, s);
  /* mlfs_info("before return %c\n", '4'); */
  /* fflush(stdout);   */
  mlfs_release_lease(path, operation, type);
  return SET_MLFS_FD(fd);
}

int mlfs_posix_access(char *pathname, int mode)
{
	struct inode *inode;

	if (mode != F_OK)
		panic("does not support other than F_OK\n");

	inode = namei(pathname);

	if (!inode) {
		return -ENOENT;
	}

	iput(inode);

	return 0;
}

int mlfs_posix_creat(char *path, uint16_t mode)
{
	return mlfs_posix_open(path, O_CREAT|O_RDWR, mode);
}

int mlfs_posix_read(int fd, uint8_t *buf, int count)
{
	int ret = 0;
	struct file *f;

        //mlfs_info("Before mlfs_file_read: %c\n", '1');
	f = &g_fd_table.open_files[fd];

	pthread_rwlock_rdlock(&f->rwlock);
        //mlfs_info("Before mlfs_file_read: %c\n", '2');
	mlfs_assert(f);

	if (f->ref == 0) {
		panic("file descriptor is wrong\n");
		return -EBADF;
	}

        //mlfs_info("Before mlfs_file_read: %c\n", '3');
	ret = mlfs_file_read(f, buf, count);

	pthread_rwlock_unlock(&f->rwlock);

	return ret;
}

int mlfs_posix_pread64(int fd, uint8_t *buf, int count, loff_t off)
{
	int ret = 0;
	struct file *f;

	f = &g_fd_table.open_files[fd];

	pthread_rwlock_rdlock(&f->rwlock);

	mlfs_assert(f);

	if (f->ref == 0) {
		panic("file descriptor is wrong\n");
		return -EBADF;
	}

	ret = mlfs_file_read_offset(f, buf, count, off);

	pthread_rwlock_unlock(&f->rwlock);

	return ret;
}

int mlfs_posix_write(int fd, uint8_t *buf, size_t count)
{
	int ret;
	struct file *f;
        //mlfs_info("Enter mlfs_posix_write %c\n", ' ');
	f = &g_fd_table.open_files[fd];

	pthread_rwlock_wrlock(&f->rwlock);

	mlfs_assert(f);

	if (f->ref == 0) {
		panic("file descriptor is wrong\n");
		return -EBADF;
	}
        //mlfs_info("Before mlfs_file_write %c\n", ' ');
	ret = mlfs_file_write(f, buf, count);

	pthread_rwlock_unlock(&f->rwlock);

	return ret;
}

int mlfs_posix_lseek(int fd, int64_t offset, int origin)
{
	struct file *f;
	int ret = 0;

	f = &g_fd_table.open_files[fd];

	if (f->ref == 0) {
		return -EBADF;
	}

	mlfs_assert(f);

	//lock file
	mlfs_time_t expiration_time = MLFS_LEASE_EXPIRATION_TIME_INITIALIZER; 
  Acquire_lease_inum(f->ip->inum, &expiration_time, mlfs_write_op, T_FILE);

	switch(origin) {
		case SEEK_SET:
			f->off = offset;
			break;
		case SEEK_CUR:
			f->off += offset;
			break;
		case SEEK_END:
			f->ip->size += offset;
			f->off = f->ip->size;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	//unlock file
	mlfs_release_lease_inum(f->ip->inum, mlfs_write_op, T_FILE);

	return f->off;
}

int mlfs_posix_close(int fd)
{
	struct file *f;

	f = &g_fd_table.open_files[fd];

	if (!f) {
		return -EBADF;
	}

	mlfs_debug("close file inum %u fd %d\n", f->ip->inum, f->fd);

	return mlfs_file_close(f);
}

int mlfs_posix_mkdir(char *path, mode_t mode)
{
	int ret = 0;
	struct inode *inode;

	start_log_tx();

	// return inode with holding ilock.
	inode = mlfs_object_create(path, T_DIR);

	if (!inode) {
		abort_log_tx();
		return -ENOENT;
	}

exit_mkdir:
	commit_log_tx();
	return ret;
}

int mlfs_posix_rmdir(char *path)
{
	int ret = 0;
	struct inode *dir_inode;

	start_log_tx();

	dir_inode = namei(path);

	if (!dir_inode) {
		abort_log_tx();
		return -ENOENT;
	}

	if (dir_inode->size > 0) {
		abort_log_tx();
		return -EINVAL;
	}

	mlfs_debug("%s\n", path);
	dir_remove_entry(dir_inode, path, dir_inode->inum);
	iunlockput(dir_inode);

exit_rmdir:
	commit_log_tx();
	return ret;
}

int mlfs_posix_stat(const char *filename, struct stat *stat_buf)
{
	struct inode *inode;

	inode = namei((char *)filename);

	if (!inode) {
		return -ENOENT;
	}

  mlfs_time_t expiration_time = MLFS_LEASE_EXPIRATION_TIME_INITIALIZER;
  Acquire_lease(filename, &expiration_time, mlfs_read_op, T_FILE);

	stati(inode, stat_buf);

  mlfs_release_lease(filename, mlfs_read_op, T_FILE);

	return 0;
}

int mlfs_posix_fstat(int fd, struct stat *stat_buf)
{
	struct file *f;

	f = &g_fd_table.open_files[fd];

	if (f->ref == 0) 
		return -ENOENT;

	mlfs_assert(f->ip);

	stati(f->ip, stat_buf);

	return 0;
}

#define ALLOC_IO_SIZE (64UL << 10)
int mlfs_posix_fallocate(int fd, offset_t offset, offset_t len)
{
	struct file *f;
	int ret = 0;
	size_t i, io_size;
	char falloc_buf[ALLOC_IO_SIZE];

	f = &g_fd_table.open_files[fd];

	if (f->ref == 0)
		return -EBADF;

	memset(falloc_buf, 0, ALLOC_IO_SIZE);

	mlfs_assert(f->ip);

	if (offset > f->ip->size)
		panic("does not support sparse file\n");

	f->off = offset;

	for (i = 0; i < len; i += ALLOC_IO_SIZE) {
		io_size = _min(len - i, ALLOC_IO_SIZE);

		f->off += io_size;

		ret = mlfs_file_write(f, (uint8_t *)falloc_buf, io_size);

		if (ret < 0) {
			panic("fail to do fallocate\n");
			return ret;
		}
	}

	return 0;
}

int mlfs_posix_unlink(const char *filename)
{
	int ret = 0;
	char name[DIRSIZ];
	struct inode *inode;
	struct inode *dir_inode;
        int lease_ret;
        mlfs_time_t expiration_time;
    lease_ret = Acquire_lease(filename, &expiration_time, mlfs_delete_op, T_FILE);
    if (lease_ret == MLFS_LEASE_ERR)
    {
      mlfs_info("File is re-created or deleted by other processes%c", ' ');
      fflush(stdout);      
      return lease_ret;
    }

        
	/* TODO: handle struct file deletion
	 * e.g., unlink without calling close */

	dir_inode = nameiparent((char *)filename, name);
	if (!dir_inode)
        {
          /* mlfs_info("unlink%c\n", '1'); */
          /* fflush(stdout); */
          mlfs_release_lease(filename, mlfs_delete_op, T_FILE);
	  return -ENOENT;
        }

	//inode = namei((char *)filename);
	inode = dir_lookup(dir_inode, name, NULL);

	if (!inode)
        {
          /* mlfs_info("unlink%c\n", '2'); */
          /* fflush(stdout); */
          mlfs_release_lease(filename, mlfs_delete_op, T_FILE);          
          return -ENOENT;                    
        }

	start_log_tx();
	
	// remove file from directory
	ret = dir_remove_entry(dir_inode, name, inode->inum);
	if (ret < 0) {
          /* mlfs_info("unlink%c\n", '3'); */
          /* fflush(stdout);                               */
          abort_log_tx();
          mlfs_release_lease(filename, mlfs_delete_op, T_FILE);          
	  return ret;
	}

	/* mlfs_info("unlink filename %s - inum %u\n", name, inode->inum); */
        /* fflush(stdout); */

	dlookup_del(inode->dev, filename);
	
	iput(dir_inode);
	iput(inode);

	ret = idealloc(inode);

	// write to the log for digest.
	add_to_loghdr(L_TYPE_UNLINK, inode, 0, sizeof(struct dinode), NULL, 0);  

	commit_log_tx();

        mlfs_release_lease(filename, mlfs_delete_op, T_FILE);
	return ret;
}

int mlfs_posix_truncate(const char *filename, offset_t length)
{
	struct inode *inode;

	inode = namei((char *)filename);

	if (!inode) {
		return -ENOENT;
	}

	start_log_tx();
	
	itrunc(inode, length);

	commit_log_tx();

	iput(inode);

	return 0;
}

int mlfs_posix_ftruncate(int fd, offset_t length)
{
	struct file *f;
	int ret = 0;

	f = &g_fd_table.open_files[fd];

	if (f->ref == 0) {
		return -EBADF;
	}

	start_log_tx();

	itrunc(f->ip, length);

	commit_log_tx();

	return 0;
}

int mlfs_posix_rename(char *oldpath, char *newpath)
{
	int ret = 0;
	struct inode *old_dir_inode, *new_dir_inode;
	char old_file_name[DIRSIZ], new_file_name[DIRSIZ];

	old_dir_inode = nameiparent((char *)oldpath, old_file_name);
	new_dir_inode = nameiparent((char *)newpath, new_file_name);

	mlfs_assert(old_dir_inode);
	mlfs_assert(new_dir_inode);

	if (old_dir_inode != new_dir_inode)
		panic("Only support rename in a same directory\n");

	start_log_tx();

	mlfs_assert(strlen(old_file_name) <= DIRSIZ);
	mlfs_assert(strlen(new_file_name) <= DIRSIZ);

	dlookup_del(old_dir_inode->dev, newpath);

	ret = dir_change_entry(old_dir_inode, old_file_name, new_file_name);
	if (ret < 0) {
		abort_log_tx();

		iput(old_dir_inode);
		iput(new_dir_inode);

		dlookup_del(old_dir_inode->dev, oldpath);
		return ret;
	}

	mlfs_debug("rename %s to %s\n", old_file_name, new_file_name);

	dlookup_del(old_dir_inode->dev, oldpath);

	iput(old_dir_inode);
	iput(new_dir_inode);

	commit_log_tx();
		
	return 0;
}

size_t mlfs_posix_getdents(int fd, struct linux_dirent *buf, 
		size_t nbytes, offset_t off)
{
	struct file *f;
	int bytes;

	f = &g_fd_table.open_files[fd];

	if (f->ref == 0) {
		return -EBADF;
	}

	if (f->type != FD_DIR) 
		return -EBADF;

	/* glibc compute bytes with struct linux_dirent
	 * but ip->size is is computed by struct dirent, 
	 * which is much small size than struct linux_dirent
	if (nbytes < f->ip->size) 
		return -EINVAL;
	*/
	
	if (f->off >= f->ip->size)
		return 0;

	bytes = dir_get_entry(f->ip, buf, f->off);
	f->off += bytes;

	return sizeof(struct linux_dirent);
}

int mlfs_posix_fcntl(int fd, int cmd, void *arg)
{
	struct file *f;
	int ret = 0;

	f = &g_fd_table.open_files[fd];

	if (f->ref == 0) {
		return -EBADF;
	}

	if (cmd != F_SETLK) {
		mlfs_debug("%s: cmd %d\n", __func__, cmd);
		//panic("Only support F_SETLK\n");
	}

	return 0;
}

#ifdef __cplusplus
}
#endif
