#include "filesystem/fs.h"
#include "io/block_io.h"
#include "log/log.h"

int namecmp(const char *s, const char *t)
{
	return strncmp(s, t, DIRSIZ);
}

static inline int dcache_del(uint8_t dev, struct dirent_block *dir_block)
{
	pthread_rwlock_rdlock(dcache_rwlock);

	HASH_DELETE(hash_handle, dirent_hash[dev], dir_block);

	pthread_rwlock_unlock(dcache_rwlock);

	return 0;
}

static inline struct dirent_block *dcache_find(uint8_t dev, 
		uint32_t inum, offset_t _offset, struct fs_log *fs_log)
{
	struct dirent_block *dir_block;
	dcache_key_t key = {
		.inum = inum,
		.offset = (_offset >> g_block_size_shift),
	};

	pthread_rwlock_rdlock(dcache_rwlock);

	HASH_FIND(hash_handle, dirent_hash[dev], &key,
        		sizeof(dcache_key_t), dir_block);

	pthread_rwlock_unlock(dcache_rwlock);

	return dir_block;
}

static inline struct dirent_block *dcache_alloc_add(uint8_t dev, uint32_t inum, 
		offset_t offset, uint8_t *data, addr_t log_addr, struct fs_log *fs_log)
{
	struct dirent_block *dir_block;

	dir_block = (struct dirent_block *)mlfs_zalloc(sizeof(*dir_block));
	if (!dir_block)
		panic("Fail to allocate dirent block\n");

	dir_block->key.inum = inum;
	dir_block->key.offset = (offset >> g_block_size_shift);

	if (data)
		memmove(dir_block->dirent_array, data, g_block_size_bytes);
	else
		memset(dir_block->dirent_array, 0, g_block_size_bytes);

	dir_block->log_addr = log_addr;
	dir_block->log_version = fs_log->avail_version;

	mlfs_debug("add (DIR): inum %u offset %lu version %u -> addr %lu\n", 
			inum, offset, dir_block->log_version, log_addr);

	pthread_rwlock_wrlock(dcache_rwlock);

	HASH_ADD(hash_handle, dirent_hash[dev], key,
	 		sizeof(dcache_key_t), dir_block);

	pthread_rwlock_unlock(dcache_rwlock);

	return dir_block;
}

uint8_t *get_dirent_block(struct inode *dir_inode, offset_t offset)
{
	int ret;
	struct buffer_head *bh;
	struct dirent_block *d_block;
	addr_t block_no;
	// Currently, assuming directory structures are stored in NVM.
	uint8_t dev = g_root_dev;

	mlfs_assert(dir_inode->itype == T_DIR);
	mlfs_assert(offset <= dir_inode->size + sizeof(struct mlfs_dirent));

	d_block = dcache_find(dev, dir_inode->inum, offset, g_fs_log);

	if (d_block) 
		return d_block->dirent_array;

	if (dir_inode->size == 0) {
		// directory is empty
		if (!(dir_inode->dinode_flags & DI_VALID))
			panic("dir_inode is not synchronized with on-disk inode\n");

		d_block = dcache_alloc_add(dev, dir_inode->inum, 0, NULL, 0, g_fs_log);
	} else {
		bmap_req_t bmap_req = {
			.dev = dev,
			.start_offset = offset,
			.blk_count = 1,
		};

		// get block address
		ret = bmap(dir_inode, &bmap_req);
		mlfs_assert(ret == 0);

		// requested directory block is not allocated in kernfs.
		if (bmap_req.blk_count_found == 0) {
			d_block = dcache_alloc_add(dev, dir_inode->inum, offset, NULL, 0, g_fs_log);
			mlfs_assert(d_block);
		} else {
			uint8_t *data = mlfs_alloc(g_block_size_bytes);
			bh = bh_get_sync_IO(dir_inode->dev, bmap_req.block_no, BH_NO_DATA_ALLOC);

			bh->b_size = g_block_size_bytes;
			bh->b_data = data;

			bh_submit_read_sync_IO(bh);

			mlfs_io_wait(dir_inode->dev, 1);

			d_block = dcache_alloc_add(dev, dir_inode->inum, 
					offset, data, bmap_req.block_no, g_fs_log);

			mlfs_assert(d_block);
		}
	}

	return d_block->dirent_array;
}

struct mlfs_dirent *get_dirent(struct inode *dir_inode, offset_t offset)
{
	struct mlfs_dirent *dir_entry;
	uint8_t *dirent_array;

	dirent_array = get_dirent_block(dir_inode, offset);

	dir_entry = (struct mlfs_dirent *)(dirent_array + (offset % g_block_size_bytes));

	return dir_entry;
}


int dir_check_entry_fast(struct inode *dir_inode) 
{
	// Avoid brute force search of directory blocks.
	// de_cache has caching of all files in the directory.
	// So cache missing means there is no file in the directory.
	if ((dir_inode->n_de_cache_entry == 
				bitmap_weight(dir_inode->dirent_bitmap, DIRBITMAP_SIZE)) &&
			dir_inode->n_de_cache_entry > 2) {
		mlfs_debug("search skipped %d %d\n", dir_inode->n_de_cache_entry,
				bitmap_weight(dir_inode->dirent_bitmap, DIRBITMAP_SIZE));
		return 0;
	}
	
	return 1;
}

// Search for an inode by name from directory entries.
// dir_inode is an inode for the directory entries.
// If found, set *poff to byte offset of entry.
// dir_lookup increase i_ref by iget. caller must call iput in proper code path.
struct inode* dir_lookup(struct inode *dir_inode, char *name, offset_t *poff)
{
	offset_t off = 0;
	uint32_t inum, n;
	struct mlfs_dirent *de;
	struct inode *ip;
	uint64_t tsc_begin, tsc_end;

	if (dir_inode->itype != T_DIR)
		panic("lookup for non DIR");

	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();

	if (poff)
		off = *poff;

	ip = de_cache_find(dir_inode, name, &off);
	if (ip) {
		if (poff)
			*poff = off;

		if (enable_perf_stats) {
			g_perf_stats.dir_search_nr_hit++;
		}

		return ip;
	}

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (de->inum == 0) {
			de++;
			continue;
		}

		if (namecmp(name, de->name) == 0) {
			// entry matches path element
			if (poff) {
				*poff = off;
				mlfs_assert(*poff <= dir_inode->size);
			}
			inum = de->inum;
			ip = iget(dir_inode->dev, inum);

			mlfs_assert(ip);

			if (!(ip->flags & I_VALID)) {
				struct dinode dip;

				read_ondisk_inode(dir_inode->dev, inum, &dip);
				// or icache search?

				mlfs_assert(dip.itype != 0);

				ip->i_sb = sb;
				ip->_dinode = (struct dinode *)ip;
				sync_inode_from_dinode(ip, &dip);
				ip->flags |= I_VALID;

				panic("Not a valid call path!\n");
			}

			iput(ip);

			de_cache_alloc_add(dir_inode, name, ip, off); 

			if (enable_perf_stats) {
				tsc_end = asm_rdtscp();
				g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
				g_perf_stats.dir_search_nr_miss++;
			}

			return ip;
		}

		de++;
	}

	if (poff)
		*poff = 0;

	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		g_perf_stats.dir_search_nr_notfound++;
	}

	return NULL;
}

/* linux_dirent must be identical to gblic kernel_dirent
 * defined in sysdeps/unix/sysv/linux/getdents.c */
int dir_get_entry(struct inode *dir_inode, struct linux_dirent *buf, offset_t off)
{
	struct inode *ip;
	uint8_t *dirent_array;
	struct mlfs_dirent *de;

	de = get_dirent(dir_inode, off);

	mlfs_assert(de);

	buf->d_ino = de->inum;
	buf->d_off = (off/sizeof(*de)) * sizeof(struct linux_dirent);
	buf->d_reclen = sizeof(struct linux_dirent);
	memmove(buf->d_name, de->name, DIRSIZ);

	return sizeof(struct mlfs_dirent);
}
	
/* Workflows when renaming to existing one (newname exists in the directory).
 * Libfs: if it finds existing file, it makes unlink request to previous inode.
 *        UNLINK of previous newname, but does not make unlink log entry.
 *        DIR_DEL of oldname
 *        DIR_RENAME of newname (inode number is the same as oldname).
 *
 * Kernfs: when it gets,
 *        DIR_DEL of oldname : delete oldname in the directory
 *        DIR_RENAME of newname: it deletes an existing newname and add newname
 *        to directory (inode number is different). 
 *        If there exist a newname, kernfs unlink it while digesting DIR_RENAME.
 */
int dir_change_entry(struct inode *dir_inode, char *oldname, char *newname)
{
	struct inode *ip;
	uint8_t *dirent_array;
	struct mlfs_dirent *de;
	offset_t off = 0;
	int ret = -ENOENT;
	uint32_t n;
	uint64_t tsc_begin, tsc_end;
	char token[DIRSIZ+8];

	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();

	// handle the case rename to a existing file.
	if ((ip = dir_lookup(dir_inode, newname, &off)) != NULL)  {
		de = get_dirent(dir_inode, off);

		mlfs_assert(strcmp(de->name, newname) == 0);

		bitmap_clear(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

		memset(de, 0, sizeof(*de));

		de_cache_del(dir_inode, de->name);

		iput(ip);

		idealloc(ip);
	}

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	mlfs_assert(de);

	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (namecmp(de->name, oldname) == 0) {
			/*
			if (add_to_log(dir_inode, dirent_array, 0, dir_inode->size) 
					!= dir_inode->size)
				panic("cannot write to log");
			iupdate(dir_inode);
			*/
			
			de_cache_del(dir_inode, oldname);
			memset(token, 0, DIRSIZ+8);
			sprintf(token, "%s@%d", oldname, de->inum);
			mlfs_assert(strlen(token) < DIRSIZ+8);
			add_to_loghdr(L_TYPE_DIR_DEL, dir_inode, de->inum, 
					dir_inode->size, token, strlen(token));

			// remove previous newname if exist.
			de_cache_del(dir_inode, newname);

			strncpy(de->name, newname, DIRSIZ);
			memset(token, 0, DIRSIZ+8);
			sprintf(token, "%s@%d", newname, de->inum);
			mlfs_assert(strlen(token) < DIRSIZ+8);
			add_to_loghdr(L_TYPE_DIR_RENAME, dir_inode, de->inum, 
					dir_inode->size, token, strlen(token));
			ret = 0;

			break;
		}

		de++;
	}

	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		g_perf_stats.dir_search_nr_miss++;
	}

	return ret;
}

int dir_remove_entry(struct inode *dir_inode, char *name, uint32_t inum)
{
	offset_t off = 0;
	struct mlfs_dirent *de;
	struct inode *ip;
	char token[DIRSIZ+8] = {0};
	uint32_t n;
	uint64_t tsc_begin, tsc_end;

	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();

	if (dir_inode->size > g_block_size_bytes) {
		de_cache_find(dir_inode, name, &off); 

		if (off != 0) {
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off); 
			de += ((off % g_block_size_bytes) / sizeof(*de));

			 if (namecmp(de->name, name) == 0)
				 goto dirent_found;
		}
	}

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	mlfs_assert(de);

	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (namecmp(de->name, name) == 0) {
			mlfs_assert(inum == de->inum);
			break;
		}

		de++;
	}

dirent_found:
	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		//g_perf_stats.dir_search_nr++;
	}

	if (dir_inode->size == off)
		dir_inode->size -= sizeof(*de);

	bitmap_clear(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

	sprintf(token, "%s@%d", name, inum);

	mlfs_assert(de->inum != 0);

	add_to_loghdr(L_TYPE_DIR_DEL, dir_inode, de->inum, 
			dir_inode->size, token, strlen(token));

	memset(de, 0, sizeof(*de));

	de_cache_del(dir_inode, name);

	mlfs_debug("remove file %s from directory\n", name);

	/* unoptimized code.

	   if (add_to_log(dir_inode, dirent_array, 0, dir_inode->size) 
	   != dir_inode->size)
	   panic("cannot write to log");

	   iupdate(dir_inode);
   */

	return 0;
}

// Write a new directory entry (name, inum) into the directory inode.
int dir_add_entry(struct inode *dir_inode, char *name, uint32_t inum)
{
	offset_t off = 0;
	struct mlfs_dirent *de;
	struct inode *ip;
	char token[DIRSIZ+8] = {0};
	uint32_t n, next_avail_slot;
	uint64_t tsc_begin, tsc_end;

	// Check that name is not present.
	/*
	if ((ip = dir_lookup(dir_inode, name, 0)) != NULL) {
		iput(ip);
		return -EEXIST;
	}
	*/

	if (enable_perf_stats)
		tsc_begin = asm_rdtscp();

	next_avail_slot = find_next_zero_bit(dir_inode->dirent_bitmap,
			DIRBITMAP_SIZE, 0);

	off = next_avail_slot * sizeof(*de);
	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
	de += ((off % g_block_size_bytes) / sizeof(*de));

	if (de->inum == 0) 
		goto empty_found;

search_slot:
	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	mlfs_assert(de);

	// brute-force search of empty dirent slot.
	for (off = 0, n = 0; off < dir_inode->size + sizeof(*de); off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (de->inum == 0)
			break;

		de++;
		bitmap_set(dir_inode->dirent_bitmap, off / sizeof(*de), 1);
	}

empty_found:
	bitmap_set(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

	if (enable_perf_stats) {
		tsc_end = asm_rdtscp();
		g_perf_stats.dir_search_tsc += (tsc_end - tsc_begin);
		//g_perf_stats.dir_search_nr++;
	}

	strncpy(de->name, name, DIRSIZ);
	de->inum = inum;

	//dir_inode->size += sizeof(struct mlfs_dirent);
	// directory inode size is a max offset.
	if (off + sizeof(struct mlfs_dirent) > dir_inode->size)  
		dir_inode->size = off + sizeof(struct mlfs_dirent);

	de_cache_alloc_add(dir_inode, name, 
			icache_find(dir_inode->dev, inum), off);

	mlfs_get_time(&dir_inode->mtime);

	mlfs_debug("name %s inum %u off %lu\n", name, inum, off);

	memset(token, 0, DIRSIZ+8);
	sprintf(token, "%s@%d", name, inum);

	add_to_loghdr(L_TYPE_DIR_ADD, dir_inode, inum, 
			dir_inode->size, token, strlen(token));

	/*
	if (add_to_log(dir_inode, dirent_array, 0, dir_inode->size) 
			!= dir_inode->size)
		panic("cannot write to log");
	*/

	// optimization: It is OK to skip logging dir_inode
	// Kernfs can update mtime (from logheader) and size (from dirent_array).
	// iupdate(dir_inode);

	return 0;
}

// Paths
// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode* namex(char *path, int parent, char *name)
{
	struct inode *ip, *next;

	if (*path == '/') 
		ip = iget(g_root_dev, ROOTINO);
	else
		//ip = idup(proc->cwd);
		panic("relative path is not yet implemented\n");

	// directory walking of a given path
	while ((path = get_next_name(path, name)) != 0) {
		ilock(ip);
		if (ip->itype != T_DIR){
			iunlockput(ip);
			return NULL;
		}
		if (parent && *path == '\0') {
			// Stop one level early.
			iunlock(ip);
			return ip;
		}
		if ((next = dir_lookup(ip, name, 0)) == NULL) {
			iunlockput(ip);
			return NULL;
		}

		iunlockput(ip);
		ip = next;
	}

	if (parent) {
		iput(ip);
		return NULL;
	}

	mlfs_debug("inum %u - refcount %d\n", ip->inum, ip->i_ref);
	return ip;
}

struct inode* namei(char *path)
{
#if 0 // This is for debugging.
	struct inode *inode, *_inode;
	char name[DIRSIZ];

	_inode = dlookup_find(g_root_dev, path); 

	if (!_inode) {
		inode = namex(path, 0, name);
		if (inode)
			dlookup_alloc_add(g_root_dev, inode, path);
	} else {
		inode = namex(path, 0, name);
		mlfs_assert(inode == _inode);
	}

	return inode;
#else
	struct inode *inode;
	char name[DIRSIZ];

	inode = dlookup_find(g_root_dev, path); 

	if (inode && (inode->flags & I_DELETING)) 
		return NULL;

	if (!inode) {
		inode = namex(path, 0, name);
		if (inode)
			dlookup_alloc_add(g_root_dev, inode, path);
	} else {
		;
	}

	return inode;
#endif
}

struct inode* nameiparent(char *path, char *name)
{
#if 0 // This is for debugging.
	struct inode *inode, *_inode;
	char parent_path[MAX_PATH];

	get_parent_path(path, parent_path);

	_inode = dlookup_find(g_root_dev, parent_path); 

	if (!_inode) {
		inode = namex(path, 1, name);
		if (inode)
			dlookup_alloc_add(g_root_dev, inode, parent_path);
		_inode = inode;
	} else {
		inode = namex(path, 1, name);
		mlfs_assert(inode == _inode);
	}

	return inode;
#else
	struct inode *inode;
	char parent_path[MAX_PATH];

	get_parent_path(path, parent_path, name);

	inode = dlookup_find(g_root_dev, parent_path); 

	if (inode && (inode->flags & I_DELETING)) 
		return NULL;

	if (!inode) {
		inode = namex(path, 1, name);
		if (inode)
			dlookup_alloc_add(g_root_dev, inode, parent_path);
	} else {
		;
	}

	return inode;
#endif
}

/////////////////////////////////////////////////////////////////
// functions for debugging. Usually called by gdb.
// Note that I assume following functions are call when
// program is frozen by gdb so functions are not thread-safe.
//

// place holder for python gdb module to records.
void dbg_save_inode(struct inode *inode, char *name)
{
	return ;
}

// place holder for python gdb module to records.
void dbg_save_dentry(struct mlfs_dirent *de, struct inode *dir_inode)
{
	return ;
}

void dbg_dump_dir(uint8_t dev, uint32_t inum)
{
	offset_t off;
	struct mlfs_dirent *de;
	struct inode *dir_inode;
	uint32_t n;

	dir_inode = iget(dev, inum);

	if (!(dir_inode->flags & I_VALID)) {
		struct dinode dip;
		int ret;

		read_ondisk_inode(dir_inode->dev, inum, &dip);

		if(dip.itype == 0) {
			mlfs_info("inum %d does not exist\n", inum);
			return;
		}

		dir_inode->_dinode = (struct dinode *)dir_inode;
		sync_inode_from_dinode(dir_inode, &dip);
		dir_inode->flags |= I_VALID;
	}

	if (dir_inode->itype != T_DIR) {
		mlfs_info("%s\n", "lookup for non DIR");
		//iput(dir_inode);
		return;
	}

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (de->inum == 0) {
			de++;
			continue;
		}

		mlfs_info("name %s\tinum %d\toff %lu\n", de->name, de->inum, off);
		de++;
	}

	mlfs_info("%s\n", "--------------------------------");

	//iput(dir_inode);

	return;
}

// Walking through to path and snapshoting dentry and inode.
void dbg_path_walk(char *path)
{
	struct inode *inode, *next_inode;
	char name[DIRSIZ];

	if (*path != '/')
		return;

	inode = iget(g_root_dev, ROOTINO);

	dbg_save_inode(inode, (char *)"/");

	while ((path = get_next_name(path, name)) != 0) {
		next_inode = dir_lookup(inode, name, 0);

		dbg_save_inode(inode, name);

		if (next_inode == NULL)
			break;

		inode = next_inode;
	}

	return ;
}

void dbg_dump_inode(uint8_t dev, uint32_t inum)
{
	struct inode *inode;

	inode = icache_find(dev, inum);

	if (!inode) {
		mlfs_info("cannot find inode %u\n", inum);
		return;
	}

	if (!(inode->flags & I_VALID)) {
		struct dinode dip;
		int ret;

		read_ondisk_inode(inode->dev, inum, &dip);

		if(dip.itype == 0) {
			mlfs_info("inum %d does not exist\n", inum);
			//iput(inode);
			return;
		}

		inode->_dinode = (struct dinode *)inode;
		sync_inode_from_dinode(inode, &dip);
		inode->flags |= I_VALID;
	}

	mlfs_info("inum %d type %d size %lu flags %u\n",
			inode->inum, inode->itype, inode->size, inode->flags);

	//iput(inode);
	return;
}

void dbg_check_inode(void *data)
{
	uint32_t i;
	struct dinode *dinode;

	for (i = 0; i < IPB ; i++) {
		dinode = (struct dinode *)data + i;

		if (dinode->itype > 3)
			GDB_TRAP;
	}

	return;
}

void dbg_check_dir(void *data)
{
	uint32_t i;
	struct mlfs_dirent *de;

	for (i = 0; i < (g_block_size_bytes / sizeof(struct mlfs_dirent)) ; i++) {
		de = (struct mlfs_dirent *)data + i;

		if (de->inum == 0)
			continue;

		mlfs_info("%u (offset %lu): %s - inum %d\n", 
				i, i * sizeof(*de), de->name, de->inum);
	}
	
	return;
}
