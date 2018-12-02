#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "mlfs/mlfs_user.h"
#include "global/global.h"
#include "concurrency/synchronization.h"
#include "concurrency/thread.h"
#include "filesystem/fs.h"
#include "io/block_io.h"
#include "filesystem/file.h"
#include "log/log.h"
#include "mlfs/mlfs_interface.h"
#include "ds/bitmap.h"
#include "filesystem/slru.h"
#include "concurrency/lease.h"

#define _min(a, b) ({\
		__typeof__(a) _a = a;\
		__typeof__(b) _b = b;\
		_a < _b ? _a : _b; })

int log_fd = 0;
int shm_fd = 0;


struct disk_superblock *disk_sb;
struct super_block *sb[g_n_devices + 1];
ncx_slab_pool_t *mlfs_slab_pool;
ncx_slab_pool_t *mlfs_slab_pool_shared;
uint8_t *shm_base;
uint8_t shm_slab_index = 0;

uint8_t g_log_dev = 0;
uint8_t g_ssd_dev = 0;
uint8_t g_hdd_dev = 0;

uint8_t initialized = 0;

// statistics
uint8_t enable_perf_stats;

struct lru g_fcache_head;

pthread_rwlock_t *icache_rwlock;
pthread_rwlock_t *dcache_rwlock;
pthread_rwlock_t *dlookup_rwlock;
pthread_rwlock_t *invalidate_rwlock;
pthread_rwlock_t *g_fcache_rwlock;

pthread_rwlock_t *shm_slab_rwlock; 
pthread_rwlock_t *shm_lru_rwlock; 

struct inode *inode_hash[g_n_devices + 1];
struct dirent_block *dirent_hash[g_n_devices + 1];
struct dlookup_data *dlookup_hash[g_n_devices + 1];

libfs_stat_t g_perf_stats;
float clock_speed_mhz;

static inline float tsc_to_ms(uint64_t tsc) 
{
	return (float)tsc / (clock_speed_mhz * 1000.0);
}

void show_libfs_stats(void)
{
	printf("\n");
	printf("----------------------- libfs statistics\n");
	// For some reason, floating point operation causes segfault in filebench 
	// worker thread.
	printf("wait on digest    : %.3f ms\n", tsc_to_ms(g_perf_stats.digest_wait_tsc));
	printf("inode allocation  : %.3f ms\n", tsc_to_ms(g_perf_stats.ialloc_tsc));
	printf("bcache search     : %.3f ms\n", tsc_to_ms(g_perf_stats.bcache_search_tsc));
	printf("search l0 tree    : %.3f ms\n", tsc_to_ms(g_perf_stats.l0_search_tsc));
	printf("search lsm tree   : %.3f ms\n", tsc_to_ms(g_perf_stats.tree_search_tsc));
	printf("log commit        : %.3f ms\n", tsc_to_ms(g_perf_stats.log_commit_tsc));
	printf("  log writes      : %.3f ms\n", tsc_to_ms(g_perf_stats.log_write_tsc));
	printf("  loghdr writes   : %.3f ms\n", tsc_to_ms(g_perf_stats.loghdr_write_tsc));
	printf("read data blocks  : %.3f ms\n", tsc_to_ms(g_perf_stats.read_data_tsc));
	printf("directory search  : %.3f ms\n", tsc_to_ms(g_perf_stats.dir_search_tsc));
	printf("temp_debug        : %.3f ms\n", tsc_to_ms(g_perf_stats.tmp_tsc));
	/*
	printf("wait on digest  (tsc)  : %lu \n", g_perf_stats.digest_wait_tsc);
	printf("inode allocation (tsc) : %lu \n", g_perf_stats.ialloc_tsc);
	printf("bcache search (tsc)    : %lu \n", g_perf_stats.bcache_search_tsc);
	printf("search l0 tree  (tsc)  : %lu \n", g_perf_stats.l0_search_tsc);
	printf("search lsm tree (tsc)  : %lu \n", g_perf_stats.tree_search_tsc);
	printf("log commit (tsc)       : %lu \n", g_perf_stats.log_commit_tsc);
	printf("  log writes (tsc)     : %lu \n", g_perf_stats.log_write_tsc);
	printf("  loghdr writes (tsc)  : %lu \n", g_perf_stats.loghdr_write_tsc);
	printf("read data blocks (tsc) : %lu \n", g_perf_stats.read_data_tsc);
	printf("directory search (tsc) : %lu \n", g_perf_stats.dir_search_tsc);
	printf("temp_debug (tsc)       : %lu \n", g_perf_stats.tmp_tsc);
	*/
#if 0
	printf("wait on digest (nr)   : %lu \n", g_perf_stats.digest_wait_nr);
	printf("search lsm tree (nr)  : %lu \n", g_perf_stats.tree_search_nr);
	printf("log writes (nr)       : %lu \n", g_perf_stats.log_write_nr);
	printf("read data blocks (nr) : %lu \n", g_perf_stats.read_data_nr);
	printf("directory search hit  (nr) : %lu \n", g_perf_stats.dir_search_nr_hit);
	printf("directory search miss (nr) : %lu \n", g_perf_stats.dir_search_nr_miss);
	printf("directory search notfound (nr) : %lu \n", g_perf_stats.dir_search_nr_notfound);
#endif
	printf("--------------------------------------\n");
}

void shutdown_fs(void)
{
	int ret;
	int _enable_perf_stats = enable_perf_stats;

	if (!initialized)
		return ;

	fflush(stdout);
	fflush(stderr);

	enable_perf_stats = 1;

	shutdown_log();

	enable_perf_stats = _enable_perf_stats;

	if (enable_perf_stats) 
		show_libfs_stats();

	/*
	ret = munmap(mlfs_slab_pool_shared, SHM_SIZE);
	if (ret == -1)
		panic("cannot unmap shared memory\n");

	ret = close(shm_fd);
	if (ret == -1)
		panic("cannot close shared memory\n");
	*/

    // lease client
    shutdown_sock();
	return ;
}

#ifdef USE_SLAB
void mlfs_slab_init(uint64_t pool_size) 
{
	uint8_t *pool_space;

	// MAP_SHARED is used to share memory in case of fork.
	pool_space = (uint8_t *)mmap(0, pool_size, PROT_READ|PROT_WRITE, 
			MAP_SHARED|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);

	mlfs_assert(pool_space);

	if (madvise(pool_space, pool_size, MADV_HUGEPAGE) < 0) 
		panic("cannot do madvise for huge page\n");

	mlfs_slab_pool = (ncx_slab_pool_t *)pool_space;
	mlfs_slab_pool->addr = pool_space;
	mlfs_slab_pool->min_shift = 3;
	mlfs_slab_pool->end = pool_space + pool_size;

	ncx_slab_init(mlfs_slab_pool);
}
#endif

void debug_init(void)
{
#ifdef MLFS_LOG
	log_fd = open(LOG_PATH, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
#endif
}

void shared_slab_init(uint8_t _shm_slab_index)
{
	/* TODO: make the following statment work */
	/* shared memory is used for 2 slab regions.
	 * At the beginning, The first region is used for allocating lru list.
	 * After libfs makes a digest request or lru update request, libfs must free 
	 * a current lru list and start build new one. Instead of iterating lru list, 
	 * Libfs reintialize slab to the second region and initialize head of lru.
	 * This is because kernel FS might be still absorbing the LRU list 
	 * in the first region.(kernel FS sends ack of digest and starts absoring 
	 * the LRU list to reduce digest wait time.)
	 * Likewise, the second region is toggle to the first region 
	 * when it needs to build a new list.
	 */
	mlfs_slab_pool_shared = (ncx_slab_pool_t *)(shm_base + 4096);

	mlfs_slab_pool_shared->addr = (shm_base + 4096) + _shm_slab_index * (SHM_SIZE / 2);
	mlfs_slab_pool_shared->min_shift = 3;
	mlfs_slab_pool_shared->end = mlfs_slab_pool_shared->addr + (SHM_SIZE / 2);

	ncx_slab_init(mlfs_slab_pool_shared);
}

static void shared_memory_init(void)
{
	shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
	if (shm_fd == -1) 
		panic("cannot open shared memory\n");

	// the first 4096 is reserved for lru_head array.
	shm_base = (uint8_t *)mmap(SHM_START_ADDR, 
			SHM_SIZE + 4096, 
			PROT_READ | PROT_WRITE, 
			MAP_SHARED | MAP_FIXED, 
			shm_fd, 0);
	if (shm_base == MAP_FAILED) 
		panic("cannot map shared memory\n");

	shm_slab_index = 0;
	shared_slab_init(shm_slab_index);

	bandwidth_consumption = (uint64_t *)shm_base;

	lru_heads = (struct list_head *)shm_base + 128;

	INIT_LIST_HEAD(&lru_heads[g_log_dev]);
}

static void cache_init(void)
{
	int i;
	
	for (i = 1; i < g_n_devices + 1; i++) {
		inode_hash[i] = NULL;
		dirent_hash[i] = NULL;
		dlookup_hash[i] = NULL;
	}

	lru_hash = NULL;

	INIT_LIST_HEAD(&g_fcache_head.lru_head);
	g_fcache_head.n = 0;
}

static void locks_init(void)
{
	pthread_rwlockattr_t rwlattr;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	icache_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
	dcache_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
	dlookup_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
	invalidate_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));
	g_fcache_rwlock = (pthread_rwlock_t *)mlfs_zalloc(sizeof(pthread_rwlock_t));

	shm_slab_rwlock = (pthread_rwlock_t *)mlfs_alloc(sizeof(pthread_rwlock_t));
	shm_lru_rwlock = (pthread_rwlock_t *)mlfs_alloc(sizeof(pthread_rwlock_t));

	pthread_rwlock_init(icache_rwlock, &rwlattr);
	pthread_rwlock_init(dcache_rwlock, &rwlattr);
	pthread_rwlock_init(dlookup_rwlock, &rwlattr);
	pthread_rwlock_init(invalidate_rwlock, &rwlattr);
	pthread_rwlock_init(g_fcache_rwlock, &rwlattr);

	pthread_rwlock_init(shm_slab_rwlock, &rwlattr);
	pthread_rwlock_init(shm_lru_rwlock, &rwlattr);
}

void init_fs(void)
{
#ifdef USE_SLAB
	unsigned long memsize_gb = 4;
#endif

	if (!initialized) {
		const char *perf_profile;
		const char *device_id;
		uint8_t dev_id;
		int i;

		device_id = getenv("DEV_ID");

		// TODO: range check.
		if (device_id)
			dev_id = atoi(device_id);
		else
			dev_id = 4;

#ifdef USE_SLAB
		mlfs_slab_init(memsize_gb << 30); 
#endif
		g_ssd_dev = 2;
		g_hdd_dev = 3;
		g_log_dev = dev_id;

		// This is allocated from slab, which is shared
		// between parent and child processes.
		disk_sb = (struct disk_superblock *)mlfs_zalloc(
				sizeof(struct disk_superblock) * (g_n_devices + 1));

		for (i = 0; i < g_n_devices + 1; i++) 
			sb[i] = (struct super_block *)mlfs_zalloc(sizeof(struct super_block));

		device_init();

		debug_init();

		cache_init();

		shared_memory_init();

		locks_init();

		read_superblock(g_root_dev);
#ifdef USE_SSD
		read_superblock(g_ssd_dev);
#endif
#ifdef USE_HDD
		read_superblock(g_hdd_dev);
#endif
		read_superblock(g_log_dev);

		mlfs_file_init();

		init_log(g_log_dev);

		// read root inode in NVM 
		read_root_inode(g_root_dev);

		mlfs_info("LibFS is initialized with id %d\n", g_log_dev);

		initialized = 1;

		perf_profile = getenv("MLFS_PROFILE");
                mlfs_info("perf_profile: %d\n", perf_profile);

		if (perf_profile)
			enable_perf_stats = 1;
		else
			enable_perf_stats = 0;

		memset(&g_perf_stats, 0, sizeof(libfs_stat_t));

		clock_speed_mhz = get_cpu_clock_speed();

        // lease client
        init_lease_client();
	}
}

///////////////////////////////////////////////////////////////////////
// Physical block management

void read_superblock(uint8_t dev)
{
	uint32_t inum;
	int ret;
	struct buffer_head *bh;
	struct dinode dip;

	// 1 is superblock address
	bh = bh_get_sync_IO(dev, 1, BH_NO_DATA_ALLOC);

	bh->b_size = g_block_size_bytes;
	bh->b_data = mlfs_zalloc(g_block_size_bytes);

	bh_submit_read_sync_IO(bh);
	mlfs_io_wait(dev, 1);

	if (!bh)
		panic("cannot read superblock\n");

	memmove(&disk_sb[dev], bh->b_data, sizeof(struct disk_superblock));

	mlfs_debug("[dev %d] superblock: size %u nblocks %u ninodes %u "
			"inodestart %lx bmap start %lx datablock_start %lx\n",
			dev,
			disk_sb[dev].size, 
			disk_sb[dev].ndatablocks, 
			disk_sb[dev].ninodes,
			disk_sb[dev].inode_start, 
			disk_sb[dev].bmap_start, 
			disk_sb[dev].datablock_start);

	sb[dev]->ondisk = &disk_sb[dev];

	sb[dev]->s_inode_bitmap = (unsigned long *)
		mlfs_zalloc(BITS_TO_LONGS(disk_sb[dev].ninodes));

	if (dev == g_root_dev) {
		// setup inode allocation bitmap.
		for (inum = 1; inum < disk_sb[dev].ninodes; inum++) {
			read_ondisk_inode(dev, inum, &dip);

			if (dip.itype != 0)
				bitmap_set(sb[dev]->s_inode_bitmap, inum, 1);
		}
	}

	mlfs_free(bh->b_data);
	bh_release(bh);
}

void read_root_inode(uint8_t dev_id)
{
	struct dinode _dinode;
	struct inode *ip;

	read_ondisk_inode(dev_id, ROOTINO, &_dinode);
	_dinode.dev = dev_id;
	mlfs_debug("root inode block %lx size %lu\n",
			IBLOCK(ROOTINO, disk_sb[dev_id]), dip->size);

	mlfs_assert(_dinode.itype == T_DIR);

	ip = ialloc(dev_id, ROOTINO, &_dinode);
}

int read_ondisk_inode(uint8_t dev, uint32_t inum, struct dinode *dip)
{
	int ret;
	struct buffer_head *bh;
	uint8_t _dev = dev;
	addr_t inode_block;

	mlfs_assert(dev == g_root_dev);

	inode_block = get_inode_block(dev, inum);
	bh = bh_get_sync_IO(dev, inode_block, BH_NO_DATA_ALLOC);

	if (dev == g_root_dev) {
		bh->b_size = sizeof(struct dinode);
		bh->b_data = (uint8_t *)dip;
		bh->b_offset = sizeof(struct dinode) * (inum % IPB);
		bh_submit_read_sync_IO(bh);
		mlfs_io_wait(dev, 1);
	} else {
		panic("This code path is deprecated\n");

		bh->b_size = g_block_size_bytes;
		bh->b_data = mlfs_zalloc(g_block_size_bytes);

		bh_submit_read_sync_IO(bh);
		mlfs_io_wait(dev, 1);

		memmove(dip, (struct dinode*)bh->b_data + (inum % IPB), sizeof(struct dinode));

		mlfs_free(bh->b_data);
	}

	return 0;
}

int sync_inode_ext_tree(uint8_t dev, struct inode *inode) 
{
	//if (inode->flags & I_RESYNC) {
		struct buffer_head *bh;
		struct dinode dinode;

		mlfs_assert(dev == g_root_dev);

		read_ondisk_inode(dev, inode->inum, &dinode);

		pthread_mutex_lock(&inode->i_mutex);
		memmove(inode->l1.addrs, dinode.l1_addrs, sizeof(addr_t) * (NDIRECT + 1));
#ifdef USE_SSD
		memmove(inode->l2.addrs, dinode.l2_addrs, sizeof(addr_t) * (NDIRECT + 1));
#endif
#ifdef USE_HDD
		memmove(inode->l3.addrs, dinode.l3_addrs, sizeof(addr_t) * (NDIRECT + 1));
#endif
		pthread_mutex_unlock(&inode->i_mutex);
		
		/*
		if (inode->itype == T_DIR)
			mlfs_info("resync inode (DIR) %u is done\n", inode->inum);
		else 
			mlfs_info("resync inode %u is done\n", inode->inum);
		*/
	//}
	
	inode->flags &= ~I_RESYNC;
	return 0;
}

// Allocate "in-memory" inode. 
// on-disk inode is created by icreate
struct inode* ialloc(uint8_t dev, uint32_t inum, struct dinode *dip)
{
	int ret;
	struct inode *ip;
	pthread_rwlockattr_t rwlattr;

	mlfs_assert(dev == g_root_dev);
	
	ip = icache_find(dev, inum);
	if (!ip) 
		ip = icache_alloc_add(dev, inum);

	ip->_dinode = (struct dinode *)ip;

	if (ip->flags & I_DELETING) {
		// There is the case where unlink in the update log is not yet digested. 
		// Then, ondisk inode does contain a stale information. 
		// So, skip syncing with ondisk inode.
		memset(ip->_dinode, 0, sizeof(struct dinode));
		ip->dev = dev;
		ip->inum = inum;
		mlfs_debug("reuse inode - inum %u\n", inum);
	} else {
		sync_inode_from_dinode(ip, dip);
		mlfs_debug("get inode - inum %u\n", inum);
	}

	ip->flags = 0;
	ip->flags |= I_VALID;
	ip->i_ref = 1;
	ip->n_de_cache_entry = 0;
	ip->i_dirty_dblock = RB_ROOT;
	ip->i_sb = sb;

	pthread_rwlockattr_setpshared(&rwlattr, PTHREAD_PROCESS_SHARED);

	pthread_rwlock_init(&ip->fcache_rwlock, &rwlattr);
	ip->fcache = NULL;
	ip->n_fcache_entries = 0;

#ifdef KLIB_HASH
	mlfs_debug("allocate hash %u\n", ip->inum);
	ip->fcache_hash = kh_init(fcache);
#endif

	ip->de_cache = NULL;
	pthread_spin_init(&ip->de_cache_spinlock, PTHREAD_PROCESS_SHARED);
	
	INIT_LIST_HEAD(&ip->i_slru_head);
	
	pthread_mutex_init(&ip->i_mutex, NULL);

	bitmap_set(sb[dev]->s_inode_bitmap, inum, 1);

	return ip;
}

// Allocate a new inode with the given type on device dev.
// A free inode has a type of zero.
struct inode* icreate(uint8_t dev, uint8_t type)
{
	uint32_t inum;
	int ret;
	struct dinode dip;
	struct inode *ip;
	pthread_rwlockattr_t rwlattr;

	// FIXME: hard coded. used for testing multiple applications.
	if (g_log_dev == 4)
		inum = find_next_zero_bit(sb[dev]->s_inode_bitmap, 
				sb[dev]->ondisk->ninodes, 1);
	else
		inum = find_next_zero_bit(sb[dev]->s_inode_bitmap, 
				sb[dev]->ondisk->ninodes, NINODES/2);

	read_ondisk_inode(dev, inum, &dip);

	// Clean (in-memory in block cache) ondisk inode.
	// At this point, storage and in-memory state diverges.
	// Libfs does not write dip directly, and kernFS will
	// update the dip on storage when digesting the inode.
	setup_ondisk_inode(&dip, dev, type);

	ip = ialloc(dev, inum, &dip);

	return ip;
}

/* Inode (in-memory) cannot be freed at this point.
 * If the inode is freed, libfs will read on-disk inode from
 * read-only area. This cause a problem since the on-disk deletion 
 * is not applied yet in kernfs (before digest).
 * idealloc() marks the inode as deleted (I_DELETING). The inode is
 * removed from icache when digesting the inode.
 * from icache. libfs will free the in-memory inode after digesting
 * a log of deleting the inode.
 */
int idealloc(struct inode *inode)
{
	struct inode *_inode;
	lru_node_t *l, *tmp;

	mlfs_assert(inode->i_ref < 2);

	if (inode->i_ref == 1 && 
			(inode->flags & I_VALID) && 
			inode->nlink == 0) {
		if (inode->flags & I_BUSY)
			panic("Inode must not be busy!");

		/*
		   if (inode->size > 0)
		   itrunc(inode, 0);
		*/

		inode->flags &= ~I_BUSY;
	}

	ilock(inode);
	inode->size = 0;
	/* After persisting the inode, libfs moves it to
	 * deleted inode hash table in persist_log_inode() */
	inode->flags |= I_DELETING;
	inode->itype = 0;

	/* delete inode data (log) pointers */
	fcache_del_all(inode);

	pthread_spin_destroy(&inode->de_cache_spinlock);
	pthread_mutex_destroy(&inode->i_mutex);
	pthread_rwlock_destroy(&inode->fcache_rwlock);

	iunlock(inode);

	mlfs_debug("dealloc inum %u\n", inode->inum);

#if 0 // TODO: do this in parallel by assigning a background thread.
	list_for_each_entry_safe(l, tmp, &inode->i_slru_head, list) { 
		HASH_DEL(lru_hash_head, l);
		list_del(&l->list);
		mlfs_free_shared(l);
	}
#endif

	return 0;
}

// Copy a modified in-memory inode to log.
void iupdate(struct inode *ip)
{
	mlfs_assert(!(ip->flags & I_DELETING));

	if (!(ip->dinode_flags & DI_VALID))
		panic("embedded _dinode is invalid\n");

	if (ip->_dinode != (struct dinode *)ip)
		panic("_dinode pointer is incorrect\n");

	mlfs_get_time(&ip->mtime);
	ip->atime = ip->mtime; 

	add_to_loghdr(L_TYPE_INODE_UPDATE, ip, 0, 
			sizeof(struct dinode), NULL, 0);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
struct inode* iget(uint8_t dev, uint32_t inum)
{
	struct inode *ip;

	ip = icache_find(dev, inum);

	if (ip) {
		if ((ip->flags & I_VALID) && (ip->flags & I_DELETING))
			return NULL;

		pthread_mutex_lock(&ip->i_mutex);
		ip->i_ref++;
		pthread_mutex_unlock(&ip->i_mutex);
	} else {
		struct dinode dip;
		// allocate new in-memory inode
		mlfs_debug("allocate new inode by iget %u\n", inum);
		read_ondisk_inode(dev, inum, &dip);

		ip = ialloc(dev, inum, &dip);
	}

	return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode* idup(struct inode *ip)
{
	panic("does not support idup yet\n");

	return ip;
}

void ilock(struct inode *ip)
{
	pthread_mutex_lock(&ip->i_mutex);
	ip->flags |= I_BUSY;
}

void iunlock(struct inode *ip)
{
	pthread_mutex_unlock(&ip->i_mutex);
	ip->flags &= ~I_BUSY;
}

/* iput does not deallocate inode. it just drops reference count. 
 * An inode is explicitly deallocated by ideallc() 
 */
void iput(struct inode *ip)
{
	pthread_mutex_lock(&ip->i_mutex);

	mlfs_muffled("iput num %u ref %u nlink %u\n", 
			ip->inum, ip->ref, ip->nlink);

	ip->i_ref--;

	pthread_mutex_unlock(&ip->i_mutex);
}

// Common idiom: unlock, then put.
void iunlockput(struct inode *ip)
{
	iunlock(ip);
	iput(ip);
}

/* Get block addresses from extent trees.
 * return = 0, if all requested offsets are found.
 * return = -EAGAIN, if not all blocks are found.
 * 
 */
int bmap(struct inode *ip, struct bmap_request *bmap_req)
{
	int ret = 0;
	handle_t handle;
	offset_t offset = bmap_req->start_offset;

	if (ip->itype == T_DIR) {
		bmap_req->block_no = ip->l1.addrs[(offset >> g_block_size_shift)];
		bmap_req->blk_count_found = 1;
		bmap_req->dev = ip->dev;

		return 0;
	}  
	/*
	if (ip->itype == T_DIR) {
		handle.dev = g_root_dev;
		struct mlfs_map_blocks map;

		map.m_lblk = (offset >> g_block_size_shift);
		map.m_len = bmap_req->blk_count;

		ret = mlfs_ext_get_blocks(&handle, ip, &map, 0);

		if (ret == bmap_req->blk_count)
			bmap_req->blk_count_found = ret;
		else
			bmap_req->blk_count_found = 0;

		return 0;
	} 
	*/
	else if (ip->itype == T_FILE) {
		struct mlfs_map_blocks map;

		map.m_lblk = (offset >> g_block_size_shift);
		map.m_len = bmap_req->blk_count;
		map.m_flags = 0;

		// get block address from extent tree.
		mlfs_assert(ip->dev == g_root_dev);

		// L1 search
		handle.dev = g_root_dev;
		ret = mlfs_ext_get_blocks(&handle, ip, &map, 0);

		// all blocks are found in the L1 tree
		if (ret != 0) {
			bmap_req->blk_count_found = ret;
			bmap_req->dev = g_root_dev;
			bmap_req->block_no = map.m_pblk;

			if (ret == bmap_req->blk_count) {
				mlfs_debug("[dev %d] Get all offset %lx: blockno %lx from NVM\n", 
						g_root_dev, offset, map.m_pblk);
				return 0;
			} else {
				mlfs_debug("[dev %d] Get partial offset %lx: blockno %lx from NVM\n", 
						g_root_dev, offset, map.m_pblk);
				return -EAGAIN;
			}
		} 

		// L2 search
#ifdef USE_SSD
		if (ret == 0) {
			struct inode *l2_ip;
			struct dinode l2_dip;

			l2_ip = ip;

			mlfs_assert(l2_ip);

			if (!(l2_ip->dinode_flags & DI_VALID)) {
				read_ondisk_inode(g_ssd_dev, ip->inum, &l2_dip);

				pthread_mutex_lock(&l2_ip->i_mutex);

				l2_ip->_dinode = (struct dinode *)l2_ip;
				sync_inode_from_dinode(l2_ip, &l2_dip);
				l2_ip->flags |= I_VALID;

				pthread_mutex_unlock(&l2_ip->i_mutex);
			}

			map.m_lblk = (offset >> g_block_size_shift);
			map.m_len = bmap_req->blk_count;
			map.m_flags = 0;

			handle.dev = g_ssd_dev;
			ret = mlfs_ext_get_blocks(&handle, l2_ip, &map, 0);

			mlfs_debug("search l2 tree: ret %d\n", ret);

#ifndef USE_HDD
			/* No blocks are found in all trees */
			if (ret == 0)
				return -EIO;
#else
			/* To L3 tree search */
			if (ret == 0)
				goto L3_search;
#endif
			bmap_req->blk_count_found = ret;
			bmap_req->dev = g_ssd_dev;
			bmap_req->block_no = map.m_pblk;

			mlfs_debug("[dev %d] Get offset %lu: blockno %lu from SSD\n", 
					g_ssd_dev, offset, map.m_pblk);

			if (ret != bmap_req->blk_count) 
				return -EAGAIN;
			else
				return 0;
		} 
#endif
		// L3 search
#ifdef USE_HDD
L3_search:
		if (ret == 0) {
			struct inode *l3_ip;
			struct dinode l3_dip;

			l3_ip = ip;

			if (!(l3_ip->dinode_flags & DI_VALID)) {
				read_ondisk_inode(g_hdd_dev, ip->inum, &l3_dip);

				pthread_mutex_lock(&l3_ip->i_mutex);

				l3_ip->_dinode = (struct dinode *)l3_ip;
				sync_inode_from_dinode(l3_ip, &l3_dip);
				l3_ip->flags |= I_VALID;

				pthread_mutex_unlock(&l3_ip->i_mutex);
			}

			map.m_lblk = (offset >> g_block_size_shift);
			map.m_len = bmap_req->blk_count;
			map.m_flags = 0;

			handle.dev = g_hdd_dev;
			ret = mlfs_ext_get_blocks(&handle, l3_ip, &map, 0);

			mlfs_debug("search l3 tree: ret %d\n", ret);

			pthread_mutex_lock(&l3_ip->i_mutex);
			// iput() without deleting
			ip->i_ref--;
			pthread_mutex_unlock(&l3_ip->i_mutex);

			/* No blocks are found in all trees */
			if (ret == 0)
				return -EIO;

			bmap_req->blk_count_found = ret;
			bmap_req->dev = l3_ip->dev;
			bmap_req->block_no = map.m_pblk;

			mlfs_debug("[dev %d] Get offset %lx: blockno %lx from SSD\n", 
					g_ssd_dev, offset, map.m_pblk);

			if (ret != bmap_req->blk_count) 
				return -EAGAIN;
			else
				return 0;
		} 
#endif
	} 

	return -EIO;
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
int itrunc(struct inode *ip, offset_t length)
{
	int ret = 0;
	struct buffer_head *bp;
	offset_t size;
	struct fcache_block *fc_block;
	offset_t key;

	mlfs_assert(ip);
	mlfs_assert(ip->itype == T_FILE);

	if (length == 0) {
		fcache_del_all(ip);
	} else if (length < ip->size) {
		/* invalidate all data pointers for log block.
		 * If libfs only takes care of zero trucate case, 
		 * dropping entire hash table is OK. 
		 * It considers non-zero truncate */
		for (size = ip->size; size > 0; size -= g_block_size_bytes) {
			key = (size >> g_block_size_shift);

			fc_block = fcache_find(ip, key);
			if (fc_block) {
				if (fc_block->is_data_cached)
					list_del(&fc_block->l);
				fcache_del(ip, fc_block);
				mlfs_free(fc_block);
			}
		}
	} 

	pthread_mutex_lock(&ip->i_mutex);

	ip->size = length;

	pthread_mutex_unlock(&ip->i_mutex);

	mlfs_get_time(&ip->mtime);

	iupdate(ip);

	return ret;
}

void stati(struct inode *ip, struct stat *st)
{
	mlfs_assert(ip);

	st->st_dev = ip->dev;
	st->st_ino = ip->inum;
	st->st_mode = 0;
	st->st_nlink = ip->nlink;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size = ip->size;
	st->st_blksize = g_block_size_bytes;
	// This could be incorrect if there is file holes.
	st->st_blocks = ip->size / 512;

	st->st_mtime = (time_t)ip->mtime.tv_sec;
	st->st_ctime = (time_t)ip->ctime.tv_sec;
	st->st_atime = (time_t)ip->atime.tv_sec;

}

// TODO: Now, eviction is simply discarding. Extend this function
// to evict data to the update log.
static void evict_read_cache(struct inode *inode, uint32_t n_entries_to_evict)
{
	uint32_t i = 0;
	struct fcache_block *_fcache_block, *tmp;

	list_for_each_entry_safe_reverse(_fcache_block, tmp, 
			&g_fcache_head.lru_head, l) {
		if (i > n_entries_to_evict) 
			break;

		if (_fcache_block->is_data_cached) {
			mlfs_free(_fcache_block->data);

			//if (!_fcache_block->log_addr) {
				list_del(&_fcache_block->l);
				fcache_del(inode, _fcache_block);
				mlfs_free(_fcache_block);
			//}

			g_fcache_head.n--;
			i++;
		}
	}
}

// Note that read cache does not copying data (parameter) to _fcache_block->data.
// Instead, _fcache_block->data points memory in data. 
static struct fcache_block *add_to_read_cache(struct inode *inode, 
		offset_t off, uint8_t *data)
{
	struct fcache_block *_fcache_block;

	_fcache_block = fcache_find(inode, (off >> g_block_size_shift));

	if (!_fcache_block) {
		_fcache_block = fcache_alloc_add(inode, (off >> g_block_size_shift), 0); 
		g_fcache_head.n++;
	} else {
		mlfs_assert(_fcache_block->is_data_cached == 0);
	}

	_fcache_block->is_data_cached = 1;
	_fcache_block->data = data;

	list_move(&_fcache_block->l, &g_fcache_head.lru_head); 

	if (g_fcache_head.n > g_max_read_cache_blocks) {
		evict_read_cache(inode, g_fcache_head.n - g_max_read_cache_blocks);
	}

	return _fcache_block;
}

int check_log_invalidation(struct fcache_block *_fcache_block)
{
	int ret = 0;
	int version_diff = g_fs_log->avail_version - _fcache_block->log_version;

	mlfs_assert(version_diff >= 0);

	// fcache must be used for either data cache or log address caching.
	// mlfs_assert((_fcache_block->is_data_cached && _fcache_block->log_addr) == 0);

	pthread_rwlock_wrlock(invalidate_rwlock);

	// fcache is used for log address.
	if ((version_diff > 1) || 
			(version_diff == 1 && 
			 _fcache_block->log_addr < g_fs_log->next_avail_header)) {
		mlfs_debug("invalidate: inum %u offset %lu -> addr %lu\n", 
				ip->inum, _off, _fcache_block->log_addr);
		_fcache_block->log_addr = 0;

		// Delete fcache_block when it is not used for read cache.
		if (!_fcache_block->is_data_cached) 
			ret = 1;
		else
			ret = 0;
	}

	pthread_rwlock_unlock(invalidate_rwlock);

	return ret;
}

int do_unaligned_read(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size, mlfs_time_t* expiration_time)
{
	int io_done = 0, ret, lease_ret;
	offset_t key, off_aligned;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	struct list_head io_list_log;
	bmap_req_t bmap_req;

	INIT_LIST_HEAD(&io_list_log);

	mlfs_assert(io_size < g_block_size_bytes);

	key = (off >> g_block_size_shift);

	off_aligned = ALIGN_FLOOR(off, g_block_size_bytes);

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	_fcache_block = fcache_find(ip, key);

	if (enable_perf_stats) {
		g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.l0_search_nr++;
	}

	if (_fcache_block) {
		ret = check_log_invalidation(_fcache_block);
		if (ret) {
			fcache_del(ip, _fcache_block);
			mlfs_free(_fcache_block);
			_fcache_block = NULL;
		}
	}	

  lease_ret = Acquire_lease_inum(ip->inum, expiration_time, mlfs_read_op, T_FILE);
  if (lease_ret == MLFS_LEASE_ERR)
  {
    mlfs_info("File is re-created or deleted by other processes%c\n", ' ');
    return -ENOENT;
  }
  if (lease_ret == MLFS_LEASE_GIVE_UP)
  {
    // We need to invalidate read cache
    // However, in the lease development, we are only working with NVM
    // NVM has no read cache (e.g., else of `if (bmap_req.dev == g_root_dev)` code path never get hit)
    // Thus, here, we have no-op
  }

	if (_fcache_block) {
		// read cache hit
		if (_fcache_block->is_data_cached) {
			memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
			list_move(&_fcache_block->l, &g_fcache_head.lru_head);

			return io_size;
		} 
		// the update log search
		else if (_fcache_block->log_addr) {
			addr_t block_no = _fcache_block->log_addr;

			mlfs_debug("GET from cache: blockno %lx offset %lu(0x%lx) size %lu\n", 
					block_no, off, off, io_size);

			bh = bh_get_sync_IO(g_fs_log->dev, block_no, BH_NO_DATA_ALLOC);

			bh->b_offset = off - off_aligned;
			bh->b_data = dst;
			bh->b_size = io_size;

			list_add_tail(&bh->b_io_list, &io_list_log);
		}
	}

	// global shared area search
	bmap_req.start_offset = off_aligned;
	bmap_req.blk_count_found = 0;
	bmap_req.blk_count = 1;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Get block address from shared area.
	ret = bmap(ip, &bmap_req);

	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

  lease_ret = Acquire_lease_inum(ip->inum, expiration_time, mlfs_read_op, T_FILE);
  if (lease_ret == MLFS_LEASE_ERR)
  {
    mlfs_info("File is re-created or deleted by other processes %c\n", ' ');
    return -ENOENT;
  }

	if (ret == -EIO)
		goto do_io_unaligned;

	bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
	bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

	// NVM case: no read caching.
	if (bmap_req.dev == g_root_dev) {
              mlfs_info("Enter NVM case %c\n", '1');
		bh->b_offset = off - off_aligned;
		bh->b_data = dst;
		bh->b_size = io_size;

		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	} 
	// SSD and HDD cache: do read caching.
	else {
                        mlfs_info("Enter NVM case %c\n", '2');
		mlfs_assert(_fcache_block == NULL);

#if 0
		// TODO: Move block-level readahead to read cache
		if (bh->b_dev == g_ssd_dev)
			mlfs_readahead(g_ssd_dev, bh->b_blocknr, (128 << 10));
#endif

		bh->b_data = mlfs_alloc(bmap_req.blk_count_found << g_block_size_shift);
		bh->b_size = g_block_size_bytes;
		bh->b_offset = 0;

		_fcache_block = add_to_read_cache(ip, off_aligned, bh->b_data);

		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		bh_submit_read_sync_IO(bh);

		mlfs_io_wait(g_ssd_dev, 1);

		if (enable_perf_stats) {
			g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.read_data_nr++;
		}

		bh_release(bh);

		// copying cached data to user buffer
		memmove(dst, _fcache_block->data + (off - off_aligned), io_size);
	}

  lease_ret = Acquire_lease_inum(ip->inum, expiration_time, mlfs_read_op, T_FILE);
  if (lease_ret == MLFS_LEASE_ERR)
  {
    mlfs_info("File is re-created or deleted by other processes %c\n", ' ');
    return -ENOENT;
  }

do_io_unaligned:
	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Patch data from log (L0) if up-to-date blocks are in the update log.
	// This is required when partial updates are in the update log.
	list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	}

	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}

	return io_size;
}

int do_aligned_read(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size, mlfs_time_t* expiration_time)
{
	int io_to_be_done = 0, ret, i, lease_ret;
	offset_t key, _off, pos;
	struct fcache_block *_fcache_block;
	uint64_t start_tsc;
	struct buffer_head *bh, *_bh;
	struct list_head io_list, io_list_log;
	uint32_t bitmap_size = (io_size >> g_block_size_shift), bitmap_pos;
	struct cache_copy_list copy_list[bitmap_size];
	bmap_req_t bmap_req;
  extern struct mlfs_lease_struct* mlfs_lease_table;

	DECLARE_BITMAP(io_bitmap, bitmap_size);

	bitmap_set(io_bitmap, 0, bitmap_size);

	memset(copy_list, 0, sizeof(struct cache_copy_list) * bitmap_size); 

	INIT_LIST_HEAD(&io_list);
	INIT_LIST_HEAD(&io_list_log);

	mlfs_assert(io_size % g_block_size_bytes == 0);

	for (pos = 0, _off = off; pos < io_size; 
			pos += g_block_size_bytes, _off += g_block_size_bytes) {
		key = (_off >> g_block_size_shift);

		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		_fcache_block = fcache_find(ip, key);

		if (enable_perf_stats) {
			g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.l0_search_nr++;
		}

		if (_fcache_block) {
			ret = check_log_invalidation(_fcache_block);
			if (ret) {
				fcache_del(ip, _fcache_block);
				mlfs_free(_fcache_block);
				_fcache_block = NULL;
			}
		}	


    lease_ret = Acquire_lease_inum(ip->inum, expiration_time, mlfs_read_op, T_FILE);
    if (lease_ret == MLFS_LEASE_ERR)
    {
      panic("File is re-created or deleted by other processes");
      return -ENOENT;
    }
    if (lease_ret == MLFS_LEASE_GIVE_UP)
    {
      // We need to invalidate read cache
      // However, in the lease development, we are only working with NVM
      // NVM has no read cache (e.g., else of `if (bmap_req.dev == g_root_dev)` code path never get hit)
      // Thus, here, we have no-op
    }

		if (_fcache_block) {
			// read cache hit
			if (_fcache_block->is_data_cached) {
				copy_list[pos >> g_block_size_shift].dst_buffer = dst + pos;
				copy_list[pos >> g_block_size_shift].cached_data = _fcache_block->data;
				copy_list[pos >> g_block_size_shift].size = g_block_size_bytes;

				// move the fcache entry to head of LRU
				list_move(&_fcache_block->l, &g_fcache_head.lru_head);

				bitmap_clear(io_bitmap, (pos >> g_block_size_shift), 1);
				io_to_be_done++;

				mlfs_debug("read cache hit: offset %lu(0x%lx) size %u\n", 
							off, off, io_size);
			} 
			// the update log search
			else if (_fcache_block->log_addr) {
				addr_t block_no = _fcache_block->log_addr;

				mlfs_debug("GET from update log: blockno %lx offset %lu(0x%lx) size %lu\n", 
						block_no, off, off, io_size);

				bh = bh_get_sync_IO(g_fs_log->dev, block_no, BH_NO_DATA_ALLOC);

				bh->b_offset = 0;
				bh->b_data = dst + pos;
				bh->b_size = g_block_size_bytes;

				list_add_tail(&bh->b_io_list, &io_list_log);
				bitmap_clear(io_bitmap, (pos >> g_block_size_shift), 1);
				io_to_be_done++;
			}
		}
	}

	// All data come from the update log.
	if (bitmap_weight(io_bitmap, bitmap_size) == 0)  {
		list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
			bh_submit_read_sync_IO(bh);
			bh_release(bh);
		}
		return io_size;
	}

do_global_search:
	_off = off + (find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift);
	pos = find_first_bit(io_bitmap, bitmap_size) << g_block_size_shift;
	bitmap_pos = find_first_bit(io_bitmap, bitmap_size);
	
	// global shared area search
	bmap_req.start_offset = _off;
	bmap_req.blk_count = 
		find_next_zero_bit(io_bitmap, bitmap_size, bitmap_pos) - bitmap_pos;
	bmap_req.dev = 0;
	bmap_req.block_no = 0;
	bmap_req.blk_count_found = 0;

  lease_ret = Acquire_lease_inum(ip->inum, expiration_time, mlfs_read_op, T_FILE);
  if (lease_ret == MLFS_LEASE_ERR)
  {
      panic("File is re-created or deleted by other processes");
      return -ENOENT;
  }

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Get block address from shared area.
	ret = bmap(ip, &bmap_req);

	if (enable_perf_stats) {
		g_perf_stats.tree_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.tree_search_nr++;
	}

	if (ret == -EIO) {
		if (bmap_req.blk_count_found != bmap_req.blk_count) {
			//panic("could not found blocks in any storage layers\n");
			mlfs_debug("inum %u - count not find block in any storage layer\n", 
					ip->inum);
		}
		goto do_io_aligned;
	}

	// NVM case: no read caching.
	if (bmap_req.dev == g_root_dev) {
              mlfs_info("Enter NVM case %c\n", '3');          
		bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no, BH_NO_DATA_ALLOC);
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);
		bh->b_offset = 0;
		bh->b_data = dst + pos;
		bh->b_size = (bmap_req.blk_count_found << g_block_size_shift);

		list_add_tail(&bh->b_io_list, &io_list);
	} 
	// SSD and HDD cache: do read caching.
	else {
                        mlfs_info("Enter NVM case %c\n", '4');          
		offset_t cur, l;

#if 0
		// TODO: block-level read_ahead to read cache.
		if (bh->b_dev == g_ssd_dev)
			mlfs_readahead(g_ssd_dev, bh->b_blocknr, (256 << 10));
#endif

		 /* The read cache is managed by 4 KB block.
		 * For large IO size (e.g., 256 KB), we have two design options
		 * 1. To make a large IO request to SSD. But, in this case, libfs
		 *    must copy IO data to read cache for each 4 KB block.
		 * 2. To make 4 KB requests for the large IO. This case does not
		 *    need memory copy; SPDK could make read request with the 
		 *    read cache block.
		 * Currently, I implement it with option 2
		 */

		// register IO memory to read cache for each 4 KB blocks.
		// When bh finishes IO, the IO data will be in the read cache.
		for (cur = _off, l = 0; l < bmap_req.blk_count_found; 
				cur += g_block_size_bytes, l++) {
			bh = bh_get_sync_IO(bmap_req.dev, bmap_req.block_no + l, BH_NO_DATA_ALLOC);
			bh->b_data = mlfs_alloc(g_block_size_bytes);
			bh->b_size = g_block_size_bytes;
			bh->b_offset = 0;

			_fcache_block = add_to_read_cache(ip, cur, bh->b_data);

			copy_list[l].dst_buffer = dst + pos;
			copy_list[l].cached_data = _fcache_block->data;
			copy_list[l].size = g_block_size_bytes;
		}

		list_add_tail(&bh->b_io_list, &io_list);
	}

  lease_ret = Acquire_lease_inum(ip->inum, expiration_time, mlfs_read_op, T_FILE);
  if (lease_ret == MLFS_LEASE_ERR)
  {
    panic("File is re-created or deleted by other processes");
    return -ENOENT;
  }

	/* EAGAIN happens in two cases:
	 * 1. A size of extent is smaller than bmap_req.blk_count. In this 
	 * case, subsequent bmap call starts finding blocks in next extent.
	 * 2. A requested offset is not in the L1 tree. In this case,
	 * subsequent bmap call starts finding blocks in other lsm tree.
	 */
	if (ret == -EAGAIN) {
		bitmap_clear(io_bitmap, bitmap_pos, bmap_req.blk_count_found);
		io_to_be_done += bmap_req.blk_count_found;

		goto do_global_search;
	} else {
		bitmap_clear(io_bitmap, bitmap_pos, bmap_req.blk_count_found);
		io_to_be_done += bmap_req.blk_count_found;

		//mlfs_assert(bitmap_weight(io_bitmap, bitmap_size) == 0);
		if (bitmap_weight(io_bitmap, bitmap_size) != 0) {
			goto do_global_search;
		}
	}

	mlfs_assert(io_to_be_done == (io_size >> g_block_size_shift));

do_io_aligned:
	//mlfs_assert(bitmap_weight(io_bitmap, bitmap_size) == 0);

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	// Read data from L1 ~ trees
	list_for_each_entry_safe(bh, _bh, &io_list, b_io_list) {
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	}

	mlfs_io_wait(g_ssd_dev, 1);
	// At this point, read cache entries are filled with data.

  lease_ret = Acquire_lease_inum(ip->inum, expiration_time, mlfs_read_op, T_FILE);
  if (lease_ret == MLFS_LEASE_ERR)
  {
    panic("File is re-created or deleted by other processes");
    return -ENOENT;
  }

	// copying read cache data to user buffer.
	for (i = 0 ; i < bitmap_size; i++) {
		if (copy_list[i].dst_buffer != NULL) {
			memmove(copy_list[i].dst_buffer, copy_list[i].cached_data,
					copy_list[i].size);

			if (copy_list[i].dst_buffer + copy_list[i].size > dst + io_size)
				panic("read request overruns the user buffer\n");
		}
	}

	// Patch data from log (L0) if up-to-date blocks are in the update log.
	// This is required when partial updates are in the update log.
	list_for_each_entry_safe(bh, _bh, &io_list_log, b_io_list) {
		bh_submit_read_sync_IO(bh);
		bh_release(bh);
	}

	if (enable_perf_stats) {
		g_perf_stats.read_data_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.read_data_nr++;
	}

	return io_size;
}

/**
 * 
 * @param ip
 * @param dst
 * @param off The current offset of the file
 * @param io_size
 * @return the number of bytes have read
 */
int readi(struct inode *ip, uint8_t *dst, offset_t off, uint32_t io_size)
{
  //mlfs_info("readi: %d\n", 0);

	int ret = 0, lease_ret;
	uint8_t *_dst;
	offset_t _off, offset_end, offset_aligned, offset_small = 0;
	offset_t size_aligned = 0, size_prepended = 0, size_appended = 0, size_small = 0;
	int io_done;

	mlfs_assert(off < ip->size);

	// Try to acquire the read lease
  mlfs_time_t expiration_time = MLFS_LEASE_EXPIRATION_TIME_INITIALIZER;
  lease_ret = Acquire_lease_inum(ip->inum, &expiration_time, mlfs_read_op, T_FILE);
  if (lease_ret == MLFS_LEASE_ERR)
  {
    mlfs_info("File is re-created or deleted by other processes%c\n", ' ');
    return -ENOENT;
  }

#ifdef LEASE_TIMEOUT_TEST
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  select(0, NULL, NULL, NULL, &tv);
#endif
  
	if (off + io_size > ip->size)
		io_size = ip->size - off;

	_dst = dst;
	_off = off;

	// new offset after read io_size
	offset_end = off + io_size;
	// The following examples help to understand the ALIGN function
	// (the closest upper bound of the multiples of block size)
	// off: 0       offset_aligned: 0
	// off: 20      offset_aligned: 4096
	// off: 4096    offset_aligned: 4096
	// off: 5043    offset_aligned: 8192
	offset_aligned = ALIGN(off, g_block_size_bytes);

	// aligned read.
	// Example:
	// off: 4096  offset_aligned: 4096  offset_end: 8192
	if ((offset_aligned == off) &&
		(offset_end == ALIGN(offset_end, g_block_size_bytes))) {
		// io_size: 4096 => size_aligned: 4096
		size_aligned = io_size;
	} 
	// unaligned read.
	else {
		// Example:
		// 1. off: 4096  offset_aligned: 4096  io_size: 1000
		// 2. off: 200   offset_end: 300       offset_aligned: 4096
		if ((offset_aligned == off && io_size < g_block_size_bytes) ||
				(offset_end < offset_aligned)) 
		{
			// The following examples to help to understand the ALIGN_FLOOR function
			// (the closest lower bound of the multiples of block size)
			// off: 0       =>: 0
			// off: 20      =>: 0
			// off: 4096    =>: 4096
			// off: 5043    =>: 4096
			offset_small = off - ALIGN_FLOOR(off, g_block_size_bytes);
			size_small = io_size;
		} else 
		{
			// Example:
			// off: 20	offset_aligned: 4096 io_size: 5000
			if (off < offset_aligned) 
			{
				// size_prepended: 4076
				size_prepended = offset_aligned - off;
			} 
			else
			{
				size_prepended = 0;
			}

			// offset_end: 20 + 5000 = 5020 => size_appended: 8192 - 5020 = 3172
			size_appended = ALIGN(offset_end, g_block_size_bytes) - offset_end;
			
			if (size_appended > 0) 
			{
				// size_appended: 4096 - 3172 = 924
				size_appended = g_block_size_bytes - size_appended;
			}

			// size_aligned = 5000 - 4076 - 924 = 0
			size_aligned = io_size - size_prepended - size_appended; 
		}
	}

	if (size_small) {
		io_done = do_unaligned_read(ip, _dst, _off, size_small, &expiration_time);

		mlfs_assert(size_small == io_done);

		_dst += io_done;
		_off += io_done;
		ret += io_done;
	}

	if (size_prepended) {
		io_done = do_unaligned_read(ip, _dst, _off, size_prepended, &expiration_time);

		mlfs_assert(size_prepended == io_done);

		_dst += io_done;
		_off += io_done;
		ret += io_done;
	}

	if (size_aligned) {
		io_done = do_aligned_read(ip, _dst, _off, size_aligned, &expiration_time);

		mlfs_assert(size_aligned == io_done);

		_dst += io_done;
		_off += io_done;
		ret += io_done;
	}

	if (size_appended) {
		io_done = do_unaligned_read(ip, _dst, _off, size_appended, &expiration_time);

		mlfs_assert(size_appended == io_done);

		_dst += io_done;
		_off += io_done;
		ret += io_done;
	}

        mlfs_release_lease_inum(ip->inum, mlfs_read_op, T_FILE);

	return ret;
}

// Write data to log
// add_to_log should handle the logging for both directory and file.
// 1. allocate blocks for log
// 2. add to log_header
int add_to_log(struct inode *ip, uint8_t *data, offset_t off, uint32_t size)
{
	offset_t total;
	uint32_t io_size, nr_iovec = 0;
	addr_t block_no;
	struct logheader_meta *loghdr_meta;

	mlfs_assert(ip != NULL);

	loghdr_meta = get_loghdr_meta();
	mlfs_assert(loghdr_meta);

	mlfs_assert(off + size > off);

	/*
	if (ip->itype == T_DIR) {
		add_to_loghdr(L_TYPE_DIR, ip, off, size);
		//dbg_check_dir(io_buf->data);
	}  
	*/

	if (ip->itype == T_FILE) {
		nr_iovec = loghdr_meta->nr_iovec;
		loghdr_meta->io_vec[nr_iovec].base = data;
		loghdr_meta->io_vec[nr_iovec].size = size;
		loghdr_meta->nr_iovec++;

		mlfs_assert(loghdr_meta->nr_iovec <= 9);
		add_to_loghdr(L_TYPE_FILE, ip, off, size, NULL, 0);
	} else
		panic("unknown inode type\n");

	if (size > 0 && (off + size) > ip->size) 
		ip->size = off + size;

	return size;
}
