#ifndef _FS_H_
#define _FS_H_

#include "global/global.h"
#include "global/types.h"
#include "global/defs.h"
#include "global/mem.h"
#include "global/ncx_slab.h"
#include "ds/uthash.h"
#include "ds/rbtree.h"
#include "shared.h"
#include "concurrency/synchronization.h"

#ifdef __cplusplus
extern "C" {
#endif

// libmlfs Disk layout:
// [ boot block | sb block | inode blocks | free bitmap | data blocks | log blocks ]
// [ inode block | free bitmap | data blocks | log blocks ] is a block group.
// If data blocks is full, then file system will allocate a new block group.
// Block group expension is not implemented yet.

typedef struct mlfs_kernfs_stats {
	uint64_t digest_time_tsc; 
	uint64_t path_search_tsc;
	uint64_t replay_time_tsc;
	uint64_t apply_time_tsc;
	uint64_t digest_dir_tsc;
	uint64_t digest_inode_tsc;
	uint64_t digest_file_tsc;
	uint64_t n_digest;
	uint64_t n_digest_skipped;
	uint64_t total_migrated_mb;
} kernfs_stats_t;

extern struct disk_superblock disk_sb[g_n_devices];
extern struct super_block *sb[g_n_devices];
extern kernfs_stats_t g_perf_stats;
extern uint8_t enable_perf_stats;

// Inodes per block.
#define IPB           (g_block_size_bytes / sizeof(struct dinode))

// directory entry cache
struct dirent_data {
	mlfs_hash_t hash_handle;
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
	struct rb_node dblk_rb_node;
	uint8_t dirent_array[g_block_size_bytes];
};

struct mlfs_range_node *mlfs_alloc_blocknode(struct super_block *sb);
struct mlfs_range_node *mlfs_alloc_inode_node(struct super_block *sb);

extern pthread_spinlock_t icache_spinlock;
extern pthread_spinlock_t dcache_spinlock;

extern struct dirent_block *dirent_hash[g_n_devices + 1];
extern struct inode *inode_hash[g_n_devices + 1];

static inline struct inode *icache_find(uint8_t dev, uint32_t inum)
{
	struct inode *inode;

	mlfs_assert(dev == g_root_dev);

	pthread_spin_lock(&icache_spinlock);

	HASH_FIND(hash_handle, inode_hash[dev], &inum,
        		sizeof(uint32_t), inode);

	pthread_spin_unlock(&icache_spinlock);

	return inode;
}

static inline struct inode *icache_alloc_add(uint8_t dev, uint32_t inum)
{
	struct inode *inode;

#ifdef __cplusplus
	inode = static_cast<struct inode *>(mlfs_zalloc(sizeof(*inode)));
#else
	inode = mlfs_zalloc(sizeof(*inode));
#endif

	if (!inode)
		panic("Fail to allocate inode\n");

	mlfs_assert(dev == g_root_dev);

	inode->dev = dev;
	inode->inum = inum;
	inode->i_ref = 1;
	//inode->filter = NULL;
	inode->flags = 0;
	inode->i_dirty_dblock = RB_ROOT;
	inode->_dinode = (struct dinode *)inode;

	pthread_spin_init(&inode->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	inode->de_cache = NULL;

	//pthread_spin_init(&inode->i_spinlock, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&inode->i_mutex, NULL);

	INIT_LIST_HEAD(&inode->i_slru_head);
	
	pthread_spin_lock(&icache_spinlock);

	HASH_ADD(hash_handle, inode_hash[dev], inum,
	 		sizeof(uint32_t), inode);

	pthread_spin_unlock(&icache_spinlock);

	return inode;
}

static inline struct inode *icache_add(struct inode *inode)
{
	uint32_t inum = inode->inum;

	pthread_mutex_init(&inode->i_mutex, NULL);
	
	pthread_spin_lock(&icache_spinlock);

	HASH_ADD(hash_handle, inode_hash[inode->dev], inum,
	 		sizeof(uint32_t), inode);

	pthread_spin_unlock(&icache_spinlock);

	return inode;
}

static inline int icache_del(struct inode *ip)
{
	pthread_spin_lock(&icache_spinlock);

	HASH_DELETE(hash_handle, inode_hash[ip->dev], ip);

	pthread_spin_unlock(&icache_spinlock);

	return 0;
}

static inline 
__attribute__((optimize("-Ofast")))
struct dirent_block *dcache_find(uint8_t dev, 
		uint32_t inum, offset_t _offset)
{
	struct dirent_block *dir_block;
	dcache_key_t key = {
		.inum = inum,
		.offset = (_offset >> g_block_size_shift),
	};

	pthread_spin_lock(&dcache_spinlock);

	HASH_FIND(hash_handle, dirent_hash[dev], &key,
        		sizeof(dcache_key_t), dir_block);

	pthread_spin_unlock(&dcache_spinlock);

	return dir_block;
}

static inline 
__attribute__((optimize("-Ofast")))
struct dirent_block *dcache_alloc_add(uint8_t dev, 
		uint32_t inum, offset_t offset, uint8_t *data)
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

	pthread_spin_lock(&dcache_spinlock);

	HASH_ADD(hash_handle, dirent_hash[dev], key,
	 		sizeof(dcache_key_t), dir_block);

	pthread_spin_unlock(&dcache_spinlock);

	return dir_block;
}

static inline struct inode *de_cache_find(struct inode *dir_inode, 
		const char *name, offset_t *offset)
{
	struct dirent_data *dirent_data;

	HASH_FIND(hash_handle, dir_inode->de_cache, name,
        		sizeof(strlen(name)), dirent_data);

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

	HASH_ADD(hash_handle, dir_inode->de_cache, name,
	 		strlen(name), _dirent_data);

	pthread_spin_unlock(&dir_inode->de_cache_spinlock);

	return dir_inode;
}

static inline int de_cache_del(struct inode *dir_inode, const char *name)
{
	struct dirent_data *dirent_data;

	HASH_FIND(hash_handle, dir_inode->de_cache, name,
        		sizeof(strlen(name)), dirent_data);
	if (dirent_data) {
		pthread_spin_lock(&dir_inode->de_cache_spinlock);
		HASH_DELETE(hash_handle, dir_inode->de_cache, dirent_data);
		pthread_spin_unlock(&dir_inode->de_cache_spinlock);
	}

	return 0;
}

//forward declaration
struct fs_stat;

//APIs
#ifdef USE_SLAB
void mlfs_slab_init(uint64_t pool_size);
#endif
void read_superblock(uint8_t dev);
void read_root_inode(uint8_t dev);
int read_ondisk_inode(uint8_t dev, uint32_t inum, struct dinode *dip);
int write_ondisk_inode(uint8_t dev, struct inode *ip);
int dir_add_entry(struct inode*, char*, uint32_t);
int dir_change_entry(struct inode *dir_inode, char *oldname, 
		char *newname, uint32_t new_inum);
int dir_remove_entry(struct inode *dir_inode, char *name, uint32_t inum);
uint8_t *get_dirent_block(struct inode *dir_inode, offset_t offset);
struct inode* dir_lookup(struct inode*, char*, offset_t*);
struct inode* ialloc(uint8_t, uint8_t, uint32_t);
struct inode* idup(struct inode*);
void cache_init(uint8_t dev);
void ilock(struct inode*);
void iput(struct inode*);
void iunlock(struct inode*);
void iunlockput(struct inode*);
void iupdate(struct inode*);
int  namecmp(const char*, const char*);
struct inode* namei(char*);
struct inode* nameiparent(char*, char*);
addr_t readi(struct inode*, char*, offset_t, addr_t);
void stati(struct inode*, struct fs_stat*);
int bmap(uint8_t mode, struct inode *ip, offset_t offset, addr_t *block_no);
void itrunc(struct inode*);
struct inode* iget(uint8_t dev, uint32_t inum);
int mlfs_mark_inode_dirty(struct inode *inode);
int persist_dirty_dirent_block(struct inode *inode);
int persist_dirty_object(void);
int digest_file(uint8_t from_dev, uint8_t to_dev, uint32_t file_inum, 
		offset_t offset, uint32_t length, addr_t blknr);
void show_storage_stats(void);

//APIs for debugging.
uint32_t dbg_get_iblkno(uint32_t inum);
void dbg_dump_inode(uint8_t dev, uint32_t inum);
void dbg_check_inode(void *data);
void dbg_check_dir(void *data);
struct inode* dbg_dir_lookup(struct inode *dir_inode,
		char *name, uint32_t *poff);
void dbg_path_walk(char *path);

extern uint8_t g_ssd_dev;
extern uint8_t g_log_dev;
extern uint8_t g_hdd_dev;

// Block containing inode i
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
