/* mlfs file system interface
 *
 * Implement file-based operations with file descriptor and
 * struct file.
 */
#include "filesystem/file.h"
#include "concurrency/synchronization.h"
#include "filesystem/fs.h"
#include "filesystem/stat.h"
#include "global/global.h"
#include "global/util.h"
#include "log/log.h"
#include "mlfs/mlfs_user.h"

#include <pthread.h>

struct open_file_table g_fd_table;

void mlfs_file_init(void)
{
    pthread_spin_init(&g_fd_table.lock, PTHREAD_PROCESS_SHARED);
}

static addr_t mlfs_update_get_fcache(struct inode* inode, offset_t offset,
    addr_t log_addr)
{
    offset_t key;
    struct fcache_block* fc_block;

    key = (offset >> g_block_size_shift);

    fc_block = fcache_find(inode, key);

    if (fc_block)
        return fc_block->log_addr;

    fc_block = fcache_alloc_add(inode, key, log_addr);

    if (!fc_block)
        return 0;

    return fc_block->log_addr;
}

// Allocate a file structure.
/* FIXME: the ftable implementation is too naive. need to
 * improve way to allocate struct file */
struct file* mlfs_file_alloc(void)
{
    int i = 0;
    struct file* f;
    pthread_rwlockattr_t rwlattr;

    pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

    pthread_spin_lock(&g_fd_table.lock);

    for (i = 0, f = &g_fd_table.open_files[0]; i < g_max_open_files; i++, f++) {
        if (f->ref == 0) {
            memset(f, 0, sizeof(*f));
            f->ref = 1;
            f->fd = i;
            pthread_rwlock_init(&f->rwlock, &rwlattr);

            pthread_spin_unlock(&g_fd_table.lock);

            return f;
        }
    }

    pthread_spin_unlock(&g_fd_table.lock);
    return 0;
}

// Increment ref count for file f.
struct file* mlfs_file_dup(struct file* f)
{

    panic("not supported\n");

    pthread_rwlock_wrlock(&f->rwlock);

    if (f->ref < 1)
        panic("filedup");
    f->ref++;

    pthread_rwlock_unlock(&f->rwlock);

    return f;
}

int mlfs_file_close(struct file* f)
{
    struct file ff;

    mlfs_assert(f->ref > 0);

    pthread_rwlock_wrlock(&f->rwlock);

    if (--f->ref > 0) {
        pthread_rwlock_unlock(&f->rwlock);
        return 0;
    }

    ff = *f;

    f->ref = 0;
    f->type = FD_NONE;

    pthread_rwlock_unlock(&f->rwlock);

    if (ff.type == FD_INODE)
        iput(ff.ip);

    return 0;
}

// Get metadata about file f.
int mlfs_file_stat(struct file* f, struct stat* st)
{
    if (f->type == FD_INODE) {
        ilock(f->ip);
        stati(f->ip, st);
        iunlock(f->ip);
        return 0;
    }
    return -1;
}

// Read from file f.
ssize_t mlfs_file_read(struct file* f, uint8_t* buf, size_t n)
{
    ssize_t r = 0;

    if (f->readable == 0)
        return -EPERM;

    if (f->type == FD_INODE) {
        // lock the inode that associated with the file
        ilock(f->ip);

        // off(set) represents the last read position of the file
        if (f->off >= f->ip->size) {
            iunlock(f->ip);
            return 0;
        }

        // TODO: we revoke lease when
        // 1. the timeout or
        // 2. there is a write lease request pending
        mlfs_info("Before readi: %c\n", ' ');
        r = readi(f->ip, buf, f->off, n);

        if (r < 0)
            panic("read error\n");

        f->off += r;

        iunlock(f->ip);
        return r;
    }

    panic("mlfs_file_read\n");

    return -1;
}

int mlfs_file_read_offset(struct file* f, uint8_t* buf, size_t n,
    offset_t off)
{
    int r;

    if (f->readable == 0)
        return -EPERM;

    if (f->type == FD_INODE) {
        ilock(f->ip);

        if (f->off >= f->ip->size) {
            iunlock(f->ip);
            return 0;
        }

        r = readi(f->ip, buf, off, n);

        if (r < 0)
            panic("read error\n");

        iunlock(f->ip);
        return r;
    }

    panic("mlfs_file_read_offset\n");

    return -1;
}

// Write to file f.
int mlfs_file_write(struct file* f, uint8_t* buf, size_t n)
{
    int r, lease_ret;
    uint32_t max_io_size = (128 << 20);
    offset_t i = 0, file_size;
    uint32_t io_size = 0;
    offset_t size_aligned, size_prepended, size_appended;
    offset_t offset_aligned, offset_start, offset_end;
    char* data;

    if (f->writable == 0)
        return -EPERM;
    /*
  // PIPE is not supported
  if(f->type == FD_PIPE)
          return pipewrite(f->pipe, buf, n);
  */

    if (f->type == FD_INODE) {
        /*            a     b     c
     *      (d) ----------------- (e)   (io data)
     *       |      |      |      |     (4K alignment)
     *           offset
     *           aligned
     *
     *	a: size_prepended
     *	b: size_aligned
     *	c: size_appended
     *	d: offset_start
     *	e: offset_end
     */

        mlfs_debug("%s\n", "+++ start transaction");

        // Try to acquire the write lease
        mlfs_time_t expiration_time = MLFS_LEASE_EXPIRATION_TIME_INITIALIZER;
        lease_ret = Acquire_lease_inum(f->ip->inum, &expiration_time, mlfs_write_op,
            T_FILE);
        if (lease_ret == MLFS_LEASE_ERR) {
            mlfs_info("File is re-created or deleted by other processes%c", ' ');
            return -ENOENT;
        }

        start_log_tx();

        offset_start = f->off;
        offset_end = f->off + n;

        offset_aligned = ALIGN(offset_start, g_block_size_bytes);

        /* when IO size is less than 4KB. */
        if (offset_end < offset_aligned) {
            size_prepended = n;
            size_aligned = 0;
            size_appended = 0;
        } else {
            // compute portion of prepended unaligned write
            if (offset_start < offset_aligned) {
                size_prepended = offset_aligned - offset_start;
            } else
                size_prepended = 0;

            mlfs_assert(size_prepended < g_block_size_bytes);

            // compute portion of appended unaligned write
            size_appended = ALIGN(offset_end, g_block_size_bytes) - offset_end;
            if (size_appended > 0)
                size_appended = g_block_size_bytes - size_appended;

            size_aligned = n - size_prepended - size_appended;
        }

        // add preprended portion to log
        if (size_prepended > 0) {
            ilock(f->ip);

            r = add_to_log(f->ip, buf, f->off, size_prepended);

            iunlock(f->ip);

            mlfs_assert(r > 0);

            f->off += r;

            i += r;
        }

        // add aligned portion to log
        while (i < n - size_appended) {
            mlfs_assert((f->off % g_block_size_bytes) == 0);

            io_size = n - size_appended - i;

            if (io_size > max_io_size)
                io_size = max_io_size;

            /* add_to_log updates inode block pointers */
            ilock(f->ip);
            /* do not copy user buffer to page cache */

            /* add buffer to log header */
            if ((r = add_to_log(f->ip, buf + i, f->off, io_size)) > 0)
                f->off += r;

            iunlock(f->ip);

            if (r < 0)
                break;

            if (r != io_size)
                panic("short filewrite");

            i += r;
        }

        // add appended portion to log
        if (size_appended > 0) {
            ilock(f->ip);

            r = add_to_log(f->ip, buf + i, f->off, size_appended);

            iunlock(f->ip);

            mlfs_assert(r > 0);

            f->off += r;

            i += r;
        }

        /* Optimization: writing inode to log does not require
     * for write append or update. Kernfs can see the length in inode
     * by looking up offset in a logheader and also mtime in a logheader */
        // iupdate(f->ip);

        commit_log_tx();

        mlfs_debug("%s\n", "--- end transaction");

        mlfs_time_t current_time;
        mlfs_get_time(&current_time);
        if (timercmp(&current_time, &expiration_time, >) == 0) {
            // We release the write lease if we finish our work before the expiration_time
            mlfs_release_lease_inum(f->ip->inum, mlfs_write_op, T_FILE);
        }

        return i == n ? n : -1;
    }

    panic("filewrite");

    return -1;
}

// supporting type : T_FILE, T_DIR
struct inode* mlfs_object_create(char* path, unsigned short type)
{
  
    int lease_ret;
    offset_t offset;
    struct inode *inode = NULL, *parent_inode = NULL;
    char name[DIRSIZ];
    uint64_t tsc_begin, tsc_end;

    mlfs_info("Enter mlfs_object_create %c\n", ' ');
    fflush(stdout);
    /* this sets value of name */
    if ((parent_inode = nameiparent(path, name)) == 0)
    {
        mlfs_info("mlfs_object_create %c\n", '1');      
        return NULL;
    }
    
    ilock(parent_inode);

    if (dir_check_entry_fast(parent_inode)) {
        inode = dir_lookup(parent_inode, name, &offset);

        if (inode) {
            iunlockput(parent_inode);

            if (inode->itype != type)
                inode->itype = type;

            mlfs_get_time(&inode->ctime);
            inode->atime = inode->mtime = inode->ctime;
            mlfs_info("mlfs_object_create %p\n", inode);
            mlfs_info("mlfs_object_create %d\n", inode->inum);            
            return inode;
        }
    }

    if (enable_perf_stats)
        tsc_begin = asm_rdtscp();

    // create new inode
    inode = icreate(parent_inode->dev, type);
    if (!inode)
        panic("cannot create inode");
    /*mlfs_debug("ENABLE PER STATS: %d", enable_perf_stats);*/
    if (enable_perf_stats) {
        /*mlfs_debug("logging inode allocation: %d", enable_perf_stats);*/
        tsc_end = asm_rdtscp();
        g_perf_stats.ialloc_tsc += (tsc_end - tsc_begin);
        g_perf_stats.ialloc_nr++;
    }

    inode->itype = type;
    inode->nlink = 1;

    add_to_loghdr(L_TYPE_INODE_CREATE, inode, 0, sizeof(struct dinode), NULL, 0);

    mlfs_debug("create %s - inum %u\n", path, inode->inum);

    if (type == T_DIR) {
        // Add "." and ".." to direntry
        // this link up is for ".."
        // To avoid cyclic ref count, do not bump link for "."
        parent_inode->nlink++;

#if 0
		if (dir_add_entry(inode, ".", inode->inum) < 0)
			panic("cannot add . entry");

		if (dir_add_entry(inode, "..", parent_inode->inum) < 0)
			panic("cannot add .. entry");
#endif

        iupdate(parent_inode);
    }

    if (dir_add_entry(parent_inode, name, inode->inum) < 0)
        panic("cannot add entry");

    iunlockput(parent_inode);

    if (!dlookup_find(g_root_dev, path))
        dlookup_alloc_add(g_root_dev, inode, path);

    return inode;
}
