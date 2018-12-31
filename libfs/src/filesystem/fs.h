#ifndef _FS_H_
#define _FS_H_

#include "global/global.h"
#include "global/types.h"
#include "global/defs.h"
#include "log/log.h"
#include "filesystem/stat.h"
#include "filesystem/shared.h"
#include "global/mem.h"
#include "global/ncx_slab.h"
#include "filesystem/extents.h"
#include "filesystem/extents_bh.h"
#include "ds/uthash.h"
#include "ds/khash.h"

#ifdef __cplusplus
extern "C" {
#endif

// libmlfs Disk layout:
// [ boot block | sb block | inode blocks | free bitmap | data blocks | log blocks ]
// [ inode block | free bitmap | data blocks | log blocks ] is a block group.
// If data blocks is full, then file system will allocate a new block group.
// Block group expension is not implemented yet.

// directory entry cache
struct dirent_data {
	mlfs_hash_t hh;
	char name[DIRSIZ]; // key
	struct inode *inode;
	offset_t offset;
};

/* A bug note. UThash has a weird bug that
 * if offset is uint64_t type, it cannot find data
 * It is OK to use 32 bit because the offset does not 
 * overflow 32 bit */
typedef struct dcache_key {
	uint32_t inum;
	uint32_t offset; // offset of directory inode / 4096.
} dcache_key_t;

// dirent array block (4KB) cache
struct dirent_block {
	dcache_key_t key; 
	mlfs_hash_t hash_handle;
	uint8_t dirent_array[g_block_size_bytes];
	addr_t log_addr;
	uint32_t log_version;
};

typedef struct fcache_key {
	offset_t offset;
} fcache_key_t;

struct fcache_block {
	offset_t key;
	mlfs_hash_t hash_handle;
	uint32_t inum;
	addr_t log_addr;		// block # of update log
	uint8_t invalidate;
	uint32_t log_version;
	uint8_t is_data_cached;
	uint8_t *data;
	struct list_head l;	// entry for global list
};

struct cache_copy_list {
	uint8_t *dst_buffer;
	uint8_t *cached_data;
	uint32_t size;
	struct list_head l;
};

struct dlookup_data {
	mlfs_hash_t hh;
	char path[MAX_PATH];	// key: canonical path
	struct inode *inode;
};

typedef struct bmap_request {
	// input 
	offset_t start_offset;
	uint32_t blk_count;
	// output
	addr_t block_no;
	uint32_t blk_count_found;
	uint8_t dev;
} bmap_req_t;

// statistics
typedef struct mlfs_libfs_stats {
	uint64_t digest_wait_tsc;
	uint32_t digest_wait_nr;
	uint64_t l0_search_tsc;
	uint32_t l0_search_nr;
	uint64_t tree_search_tsc;
	uint32_t tree_search_nr;
	uint64_t log_write_tsc;
	uint64_t loghdr_write_tsc;
	uint32_t log_write_nr;
	uint64_t log_commit_tsc;
	uint32_t log_commit_nr;
	uint64_t read_data_tsc;
	uint32_t read_data_nr;
	uint64_t dir_search_tsc;
	uint32_t dir_search_nr_hit;
	uint32_t dir_search_nr_miss;
	uint32_t dir_search_nr_notfound;
	uint64_t ialloc_tsc;
	uint32_t ialloc_nr;
	uint64_t tmp_tsc;
	uint64_t bcache_search_tsc;
	uint32_t bcache_search_nr;
} libfs_stat_t;

extern struct lru g_fcache_head;

extern libfs_stat_t g_perf_stats;
extern uint8_t enable_perf_stats;

extern pthread_rwlock_t *icache_rwlock;
extern pthread_rwlock_t *dcache_rwlock;
extern pthread_rwlock_t *dlookup_rwlock;
extern pthread_rwlock_t *invalidate_rwlock;
extern pthread_rwlock_t *g_fcache_rwlock;

extern struct dirent_block *dirent_hash[g_n_devices + 1];
extern struct inode *inode_hash[g_n_devices + 1];
extern struct dlookup_data *dlookup_hash[g_n_devices + 1];

static inline struct inode *icache_find(uint8_t dev, uint32_t inum)
{
	struct inode *inode;

	mlfs_assert(dev == g_root_dev);

	pthread_rwlock_rdlock(icache_rwlock);

	HASH_FIND(hash_handle, inode_hash[dev], &inum,
        		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);

	return inode;
}

static inline struct inode *icache_alloc_add(uint8_t dev, uint32_t inum)
{
	struct inode *inode;
	pthread_rwlockattr_t rwlattr;

	mlfs_assert(dev == g_root_dev);

	inode = (struct inode *)mlfs_zalloc(sizeof(*inode));

	if (!inode)
		panic("Fail to allocate inode\n");

	inode->dev = dev;
	inode->inum = inum;
	inode->i_ref = 1;

	inode->_dinode = (struct dinode *)inode;

#if 0
	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	pthread_rwlock_init(&inode->fcache_rwlock, &rwlattr);
	inode->fcache = NULL;
	inode->n_fcache_entries = 0;

#ifdef KLIB_HASH
	mlfs_info("allocate hash %u\n", inode->inum);
	inode->fcache_hash = kh_init(fcache);
#endif

	INIT_LIST_HEAD(&inode->i_slru_head);
	
	pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	inode->de_cache = NULL;
#endif

	//pthread_rwlock_wrlock(icache_rwlock);

	HASH_ADD(hash_handle, inode_hash[dev], inum,
	 		sizeof(uint32_t), inode);

	//pthread_rwlock_unlock(icache_rwlock);

	return inode;
}

static inline struct inode *icache_add(struct inode *inode)
{
	uint32_t inum = inode->inum;

	pthread_mutex_init(&inode->i_mutex, NULL);
	
	pthread_rwlock_wrlock(icache_rwlock);

	HASH_ADD(hash_handle, inode_hash[inode->dev], inum,
	 		sizeof(uint32_t), inode);

	pthread_rwlock_unlock(icache_rwlock);

	return inode;
}

static inline int icache_del(struct inode *ip)
{
	pthread_rwlock_wrlock(icache_rwlock);

	HASH_DELETE(hash_handle, inode_hash[ip->dev], ip);

	pthread_rwlock_unlock(icache_rwlock);

	return 0;
}

#ifdef KLIB_HASH
static inline struct fcache_block *fcache_find(struct inode *inode, offset_t key)
{
	khiter_t k;
	struct fcache_block *fc_block = NULL;

	pthread_rwlock_rdlock(&inode->fcache_rwlock);

	k = kh_get(fcache, inode->fcache_hash, key);
	if (k == kh_end(inode->fcache_hash)) {
		pthread_rwlock_unlock(&inode->fcache_rwlock);
		return NULL;
	}

	fc_block = kh_value(inode->fcache_hash, k);

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline struct fcache_block *fcache_alloc_add(struct inode *inode, 
		offset_t key, addr_t log_addr)
{
	struct fcache_block *fc_block;
	khiter_t k;
	int ret;

	fc_block = (struct fcache_block *)mlfs_zalloc(sizeof(*fc_block));
	if (!fc_block)
		panic("Fail to allocate fcache block\n");

	fc_block->key = key;
	fc_block->log_addr = log_addr;
	fc_block->invalidate = 0;
	fc_block->is_data_cached = 0;
	fc_block->inum = inode->inum;
	inode->n_fcache_entries++;
	INIT_LIST_HEAD(&fc_block->l);

	pthread_rwlock_wrlock(&inode->fcache_rwlock);
	
	k = kh_put(fcache, inode->fcache_hash, key, &ret);
	if (ret < 0)
		panic("fail to insert fcache value");
	/*
	else if (!ret) {
		kh_del(fcache, inode->fcache_hash, k);
		k = kh_put(fcache, inode->fcache_hash, key, &ret);
	}
	*/

	kh_value(inode->fcache_hash, k) = fc_block;
	//mlfs_info("add key %u @ inode %u\n", key, inode->inum);

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline int fcache_del(struct inode *inode, 
		struct fcache_block *fc_block)
{
	khiter_t k;
	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	k = kh_get(fcache, inode->fcache_hash, fc_block->key);

	if (kh_exist(inode->fcache_hash, k)) {
		kh_del(fcache, inode->fcache_hash, k);
		inode->n_fcache_entries--;
	}

	/*
	if (k != kh_end(inode->fcache_hash)) {
		kh_del(fcache, inode->fcache_hash, k);
		inode->n_fcache_entries--;
		mlfs_debug("del key %u @ inode %u\n", fc_block->key, inode->inum);
	}
	*/

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return 0;
}

static inline int fcache_del_all(struct inode *inode)
{
	khiter_t k;
	struct fcache_block *fc_block;

	for (k = kh_begin(inode->fcache_hash); 
			k != kh_end(inode->fcache_hash); k++) {
		if (kh_exist(inode->fcache_hash, k)) {
			fc_block = kh_value(inode->fcache_hash, k);

			if (fc_block->is_data_cached) {
				list_del(&fc_block->l);
				mlfs_free(fc_block->data);
			}
			//mlfs_free(fc_block);
		}
	}

	mlfs_debug("destroy hash %u\n", inode->inum);
	kh_destroy(fcache, inode->fcache_hash);
	return 0;
}
// UTHash version
#else
static inline struct fcache_block *fcache_find(struct inode *inode, offset_t key)
{
	struct fcache_block *fc_block = NULL;

	pthread_rwlock_rdlock(&inode->fcache_rwlock);

	HASH_FIND(hash_handle, inode->fcache, &key,
        		sizeof(offset_t), fc_block);
	
	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline struct fcache_block *fcache_alloc_add(struct inode *inode, 
		offset_t key, addr_t log_addr)
{
	struct fcache_block *fc_block;

	fc_block = (struct fcache_block *)mlfs_zalloc(sizeof(*fc_block));
	if (!fc_block)
		panic("Fail to allocate fcache block\n");

	fc_block->key = key;
	fc_block->inum = inode->inum;
	fc_block->log_addr = log_addr;
	fc_block->invalidate = 0;
	fc_block->is_data_cached = 0;
	inode->n_fcache_entries++;
	INIT_LIST_HEAD(&fc_block->l);

	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	HASH_ADD(hash_handle, inode->fcache, key,
	 		sizeof(offset_t), fc_block);

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return fc_block;
}

static inline int fcache_del(struct inode *inode, 
		struct fcache_block *fc_block)
{
	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	HASH_DELETE(hash_handle, inode->fcache, fc_block);
	inode->n_fcache_entries--;

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return 0;
}

static inline int fcache_del_all(struct inode *inode)
{
	struct fcache_block *item, *tmp;

	pthread_rwlock_wrlock(&inode->fcache_rwlock);

	HASH_ITER(hash_handle, inode->fcache, item, tmp) {
		HASH_DELETE(hash_handle, inode->fcache, item);
		if (item->is_data_cached) {
			list_del(&item->l);
			mlfs_free(item->data);
		}
		mlfs_free(item);
	}
	HASH_CLEAR(hash_handle, inode->fcache);

	inode->n_fcache_entries = 0;

	pthread_rwlock_unlock(&inode->fcache_rwlock);

	return 0;
}
#endif

static inline struct inode *de_cache_find(struct inode *dir_inode, 
		const char *_name, offset_t *offset)
{
	struct dirent_data *dirent_data;

	HASH_FIND_STR(dir_inode->de_cache, _name, dirent_data);

	if (dirent_data) {
		*offset = dirent_data->offset;
		return dirent_data->inode;
	} else {
		*offset = 0;
		return NULL;
	}
}

static inline struct inode *de_cache_alloc_add(struct inode *dir_inode, 
		const char *name, struct inode *inode, offset_t _offset)
{
	struct dirent_data *_dirent_data;

	_dirent_data = (struct dirent_data *)mlfs_zalloc(sizeof(*_dirent_data));
	if (!_dirent_data)
		panic("Fail to allocate dirent data\n");

	strcpy(_dirent_data->name, name);

	_dirent_data->inode = inode;
	_dirent_data->offset = _offset;

	pthread_spin_lock(&dir_inode->de_cache_spinlock);

	HASH_ADD_STR(dir_inode->de_cache, name, _dirent_data);

	dir_inode->n_de_cache_entry++;

	pthread_spin_unlock(&dir_inode->de_cache_spinlock);

	return dir_inode;
}

static inline int de_cache_del(struct inode *dir_inode, const char *_name)
{
	struct dirent_data *dirent_data;

	HASH_FIND_STR(dir_inode->de_cache, _name, dirent_data);
	if (dirent_data) {
		pthread_spin_lock(&dir_inode->de_cache_spinlock);
		HASH_DEL(dir_inode->de_cache, dirent_data);
		dir_inode->n_de_cache_entry--;
		pthread_spin_unlock(&dir_inode->de_cache_spinlock);
	}

	return 0;
}

static inline struct inode *dlookup_find(uint8_t dev, const char *path)
{
	struct dlookup_data *_dlookup_data;

	pthread_rwlock_rdlock(dlookup_rwlock);

	HASH_FIND_STR(dlookup_hash[dev], path, _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);

	if (!_dlookup_data)
		return NULL;
	else
		return _dlookup_data->inode;
}

static inline struct inode *dlookup_alloc_add(uint8_t dev, 
		struct inode *inode, const char *_path)
{
	struct dlookup_data *_dlookup_data;

	_dlookup_data = (struct dlookup_data *)mlfs_zalloc(sizeof(*_dlookup_data));
	if (!_dlookup_data)
		panic("Fail to allocate dlookup data\n");

	strcpy(_dlookup_data->path, _path);

	_dlookup_data->inode = inode;

	pthread_rwlock_wrlock(dlookup_rwlock);

	HASH_ADD_STR(dlookup_hash[dev], path, _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);

	return inode;
}

static inline int dlookup_del(uint8_t dev, const char *path)
{
	struct dlookup_data *_dlookup_data;

	pthread_rwlock_wrlock(dlookup_rwlock);

	HASH_FIND_STR(dlookup_hash[dev], path, _dlookup_data);
	if (_dlookup_data)
		HASH_DEL(dlookup_hash[dev], _dlookup_data);

	pthread_rwlock_unlock(dlookup_rwlock);

	return 0;
}

// global variables
extern uint8_t fs_dev_id;
extern struct disk_superblock *disk_sb;
extern struct super_block *sb[g_n_devices + 1];

//forward declaration
struct fs_stat;

void shared_slab_init(uint8_t shm_slab_index);

void read_superblock(uint8_t dev);
void read_root_inode(uint8_t dev);

int read_ondisk_inode(uint8_t dev, uint32_t inum, struct dinode *dip);
int sync_inode_ext_tree(uint8_t dev, struct inode *inode);
struct inode* icreate(uint8_t dev, uint8_t type);
struct inode* ialloc(uint8_t dev, uint32_t inum, struct dinode *dip);
int idealloc(struct inode *inode);
struct inode* idup(struct inode*);
struct inode* iget(uint8_t dev, uint32_t inum);
void ilock(struct inode*);
void iput(struct inode*);
void iunlock(struct inode*);
void iunlockput(struct inode*);
void iupdate(struct inode*);
int itrunc(struct inode *inode, offset_t length);
int bmap(struct inode *ip, struct bmap_request *bmap_req);

int dir_check_entry_fast(struct inode *dir_inode);
struct inode* dir_lookup(struct inode*, char*, offset_t *);
int dir_get_entry(struct inode *dir_inode, struct linux_dirent *buf, offset_t off);
int dir_add_entry(struct inode *inode, char *name, uint32_t inum);
int dir_remove_entry(struct inode *inode,char *name, uint32_t inum);
int dir_change_entry(struct inode *dir_inode, char *oldname, char *newname);
int namecmp(const char*, const char*);
struct inode* namei(char*);
struct inode* nameiparent(char*, char*);
int readi_unopt(struct inode*, uint8_t *, offset_t, uint32_t);
int readi(struct inode*, uint8_t *, offset_t, uint32_t);
void stati(struct inode*, struct stat *);
int add_to_log(struct inode*, uint8_t*, offset_t, uint32_t);
int check_log_invalidation(struct fcache_block *_fcache_block);
uint8_t *get_dirent_block(struct inode *dir_inode, offset_t offset);
void show_libfs_stats(const char *title);
void reset_libfs_stats();

//APIs for debugging.
uint32_t dbg_get_iblkno(uint32_t inum);
void dbg_dump_inode(uint8_t dev, uint32_t inum);
void dbg_check_inode(void *data);
void dbg_check_dir(void *data);
void dbg_dir_dump(uint8_t dev, uint32_t inum);
void dbg_path_walk(char *path);

// mempool slab for libfs
extern ncx_slab_pool_t *mlfs_slab_pool;
// mempool on top of shared memory
extern ncx_slab_pool_t *mlfs_slab_pool_shared;
extern uint8_t shm_slab_index;

extern pthread_rwlock_t *shm_slab_rwlock; 
extern pthread_rwlock_t *shm_lru_rwlock; 

extern uint64_t *bandwidth_consumption;

// Inodes per block.
#define IPB           (g_block_size_bytes / sizeof(struct dinode))

// Block containing inode i
/*
#define IBLOCK(i, disk_sb)  ((i/IPB) + disk_sb.inode_start)
*/
static inline addr_t get_inode_block(uint8_t dev, uint32_t inum)
{
	return (inum / IPB) + disk_sb[dev].inode_start;
}

// Bitmap bits per block
#define BPB           (g_block_size_bytes*8)

// Block of free map containing bit for block b
#define BBLOCK(b, disk_sb) (b/BPB + disk_sb.bmap_start)

#ifdef __cplusplus
}
#endif

#endif
