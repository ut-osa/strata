#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "mlfs/mlfs_user.h"
#include "log/log.h"
#include "concurrency/thread.h"
#include "filesystem/fs.h"
#include "filesystem/slru.h"
#include "io/block_io.h"
#include "global/mem.h"
#include "global/util.h"
#include "mlfs/mlfs_interface.h"
#include "storage/storage.h"

/**
 A system call should call start_log_tx()/commit_log_tx() to mark
 its start and end. Usually start_log_tx() just increments
 the count of in-progress FS system calls and returns.

 Log appends are synchronous.
 For crash consistency, log blocks are persisted first and
 then log header is perstisted in commit_log_tx().
 Digesting happens as a unit of a log block group
 (each logheader for each group).
 n in the log header indicates # of live log blocks.
 After digesting all log blocks in a group, the digest thread
 unsets inuse bit, indicating the group  can be garbage-collected.
 Note that the log header must be less than
 a block size for crash consistency.

 On-disk format of log area
 [ log superblock | log header | log blocks | log header | log blocks ... ]
 [ log header | log blocks ] is a transaction made by a system call.

 Each logheader describes a log block group created by
 a single transaction. Different write syscalls use different log group.
 start_log_tx() start a new log group and commit_log_tx()
 serializes writing multiple log groups to log area.
 */

struct fs_log *g_fs_log;
struct log_superblock *g_log_sb;

// for communication with kernel fs.
int g_sock_fd;
static struct sockaddr_un g_srv_addr, g_addr;

static void read_log_superblock(struct log_superblock *log_sb);
static void write_log_superblock(struct log_superblock *log_sb);
static void commit_log(void);
static void digest_log(void);

pthread_mutex_t *g_log_mutex_shared;

//pthread_t is unsigned long
static unsigned long digest_thread_id;
//Thread entry point
void *digest_thread(void *arg);

void init_log(int dev)
{
	int ret;
	int volatile done = 0;
	pthread_mutexattr_t attr;

	if (sizeof(struct logheader) > g_block_size_bytes) {
		printf("log header size %lu block size %lu\n",
				sizeof(struct logheader), g_block_size_bytes);
		panic("initlog: too big logheader");
	}

	g_fs_log = (struct fs_log *)mlfs_zalloc(sizeof(struct fs_log));
	g_log_sb = (struct log_superblock *)mlfs_zalloc(sizeof(struct log_superblock));

	g_fs_log->log_sb_blk = disk_sb[dev].log_start;
	g_fs_log->size = disk_sb[dev].nlog;
	g_fs_log->dev = dev;
	g_fs_log->nloghdr = 0;

	ret = pipe(g_fs_log->digest_fd);
	if (ret < 0) 
		panic("cannot create pipe for digest\n");

	read_log_superblock(g_log_sb);

	g_fs_log->log_sb = g_log_sb;

	// Assuming all logs are digested by recovery.
	g_fs_log->next_avail_header = disk_sb[dev].log_start + 1; // +1: log superblock
	g_fs_log->next_avail = g_fs_log->next_avail_header + 1; 
	g_fs_log->start_blk = disk_sb[dev].log_start + 1;

	mlfs_debug("end of the log %lx\n", g_fs_log->start_blk + g_fs_log->size);

	g_log_sb->start_digest = g_fs_log->next_avail_header;

	write_log_superblock(g_log_sb);

	atomic_init(&g_log_sb->n_digest, 0);

	//g_fs_log->outstanding = 0;
	g_fs_log->start_version = g_fs_log->avail_version = 0;

	pthread_spin_init(&g_fs_log->log_lock, PTHREAD_PROCESS_SHARED);
	
	// g_log_mutex_shared is shared mutex between parent and child.
	g_log_mutex_shared = (pthread_mutex_t *)mlfs_zalloc(sizeof(pthread_mutex_t));
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(g_log_mutex_shared, &attr);

	g_fs_log->shared_log_lock = (pthread_mutex_t *)mlfs_zalloc(sizeof(pthread_mutex_t));
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(g_fs_log->shared_log_lock, &attr);

	digest_thread_id = mlfs_create_thread(digest_thread, &done);

	// enable/disable statistics for log
	enable_perf_stats = 0;

	/* wait until the digest thread get started */
	while(!done);
}

void shutdown_log(void)
{
	// wait until the digest_thead finishes job.
	if (g_fs_log->digesting) {
		mlfs_info("%s", "[L] Wait finishing on-going digest\n");
		wait_on_digesting();
	}

	if (atomic_load(&g_fs_log->log_sb->n_digest)) {
		mlfs_info("%s", "[L] Digesting remaining log data\n");
		while(make_digest_request_async(100) != -EBUSY);
		m_barrier();
		wait_on_digesting();
	}
}

static void read_log_superblock(struct log_superblock *log_sb)
{
	int ret;
	struct buffer_head *bh;

	bh = bh_get_sync_IO(g_fs_log->dev, g_fs_log->log_sb_blk, 
			BH_NO_DATA_ALLOC);

	bh->b_size = sizeof(struct log_superblock);
	bh->b_data = (uint8_t *)log_sb;

	bh_submit_read_sync_IO(bh);

	bh_release(bh);

	return;
}

static void write_log_superblock(struct log_superblock *log_sb)
{
	int ret;
	struct buffer_head *bh;

	bh = bh_get_sync_IO(g_fs_log->dev, g_fs_log->log_sb_blk, 
			BH_NO_DATA_ALLOC);

	bh->b_size = sizeof(struct log_superblock);
	bh->b_data = (uint8_t *)log_sb;

	mlfs_write(bh);

	bh_release(bh);

	return;
}

static loghdr_t *read_log_header(uint16_t dev, addr_t blkno)
{
	int ret;
	struct buffer_head *bh;
	loghdr_t *hdr_data = mlfs_alloc(sizeof(struct logheader));

	bh = bh_get_sync_IO(g_fs_log->dev, blkno, BH_NO_DATA_ALLOC);
	bh->b_size = sizeof(struct logheader);
	bh->b_data = (uint8_t *)hdr_data;

	bh_submit_read_sync_IO(bh);

	return hdr_data;
}

inline addr_t log_alloc(uint32_t nr_blocks)
{
	int ret;

	/* g_fs_log->start_blk : header
	 * g_fs_log->next_avail : tail 
	 *
	 * There are two cases:
	 *
	 * <log_begin ....... log_start .......  next_avail .... log_end>
	 *	      digested data      available data       empty space
	 *
	 *	                    (ver 10)              (ver 9)
	 * <log_begin ........ next_avail .......... log_start ...... log_end>
	 *	       available data       digested data       available data
	 *
	 */
	//mlfs_assert(g_fs_log->avail_version - g_fs_log->start_version < 2);

	// Log is getting full. make asynchronous digest request.
	if (!g_fs_log->digesting) {
		addr_t nr_used_blk = 0;
		if (g_fs_log->avail_version == g_fs_log->start_version) {
			mlfs_assert(g_fs_log->next_avail >= g_fs_log->start_blk);
			nr_used_blk = g_fs_log->next_avail - g_fs_log->start_blk; 
		} else {
			nr_used_blk = (g_fs_log->size - g_fs_log->start_blk);
			nr_used_blk += (g_fs_log->next_avail - g_fs_log->log_sb_blk);
		}

		// The 30% is ad-hoc parameter: In genernal, 30% ~ 40% shows good performance
		// in all workloads
		if (nr_used_blk > ((30 * g_fs_log->size) / 100)) {

			// digest 90% of log.
			while(make_digest_request_async(100) != -EBUSY)
			mlfs_info("%s", "[L] log is getting full. asynchronous digest!\n");
		}
	}

	// next_avail reaches the end of log. 
	//if (g_fs_log->next_avail + nr_blocks > g_fs_log->log_sb_blk + g_fs_log->size) {
	if (g_fs_log->next_avail + nr_blocks > g_fs_log->size) {
		g_fs_log->next_avail = g_fs_log->log_sb_blk + 1;

		atomic_add(&g_fs_log->avail_version, 1);

		mlfs_debug("-- log tail is rotated: new start %lu\n", g_fs_log->next_avail);
	}

	addr_t next_log_blk = 
		__sync_fetch_and_add(&g_fs_log->next_avail, nr_blocks);

	// This has many policy questions.
	// Current implmentation is very converative.
	// Pondering the way of optimization.
retry:
	if (g_fs_log->avail_version > g_fs_log->start_version) {
		if (g_fs_log->start_blk - g_fs_log->next_avail
				< (g_fs_log->size/ 5)) {
			mlfs_info("%s", "\x1B[31m [L] synchronous digest request and wait! \x1B[0m\n");
			while (make_digest_request_async(95) != -EBUSY);

			m_barrier();
			wait_on_digesting();
		}
	}

	if (g_fs_log->avail_version > g_fs_log->start_version) {
		if (g_fs_log->next_avail > g_fs_log->start_blk) 
			goto retry;
	}

	if (0) {
		int i;
		for (i = 0; i < nr_blocks; i++) {
			mlfs_info("alloc %lu\n", next_log_blk + i);
		}
	}

	return next_log_blk;
}

// allocate logheader meta and attach logheader.
static inline struct logheader_meta *loghd_alloc(struct logheader *lh)
{
	struct logheader_meta *loghdr_meta;

	loghdr_meta = (struct logheader_meta *)
		mlfs_zalloc(sizeof(struct logheader_meta));

	if (!loghdr_meta)
		panic("cannot allocate logheader_meta");

	INIT_LIST_HEAD(&loghdr_meta->link);

	loghdr_meta->loghdr = lh;

	return loghdr_meta;
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void persist_log_header(struct logheader_meta *loghdr_meta,
		addr_t hdr_blkno)
{
	struct logheader *loghdr = loghdr_meta->loghdr;
	struct buffer_head *io_bh;
	int i;
	uint64_t start_tsc;

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	io_bh = bh_get_sync_IO(g_fs_log->dev, hdr_blkno, BH_NO_DATA_ALLOC);

	if (enable_perf_stats) {
		g_perf_stats.bcache_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.bcache_search_nr++;
	}
	//pthread_spin_lock(&io_bh->b_spinlock);

	mlfs_get_time(&loghdr->mtime);
	io_bh->b_data = (uint8_t *)loghdr;
	io_bh->b_size = sizeof(struct logheader);

	mlfs_write(io_bh);

	mlfs_debug("pid %u [log header] inuse %d blkno %lu next_hdr_blockno %lu\n", 
			getpid(),
			loghdr->inuse, io_bh->b_blocknr, 
			loghdr->next_loghdr_blkno);

	if (loghdr_meta->ext_used) {
		io_bh->b_data = loghdr_meta->loghdr_ext;
		io_bh->b_size = loghdr_meta->ext_used;
		io_bh->b_offset = sizeof(struct logheader);
		mlfs_write(io_bh);
	}

	bh_release(io_bh);

	//pthread_spin_unlock(&io_bh->b_spinlock);
}

// called at the start of each FS system call.
void start_log_tx(void)
{
	struct logheader_meta *loghdr_meta;

#ifndef CONCURRENT
	pthread_mutex_lock(g_log_mutex_shared);
	g_fs_log->outstanding++;

	mlfs_debug("start log_tx %u\n", g_fs_log->outstanding);
	mlfs_assert(g_fs_log->outstanding == 1);
#endif

	loghdr_meta = get_loghdr_meta();
	memset(loghdr_meta, 0, sizeof(struct logheader_meta));

	if (!loghdr_meta)
		panic("cannot locate logheader_meta\n");

	loghdr_meta->hdr_blkno = 0;
	INIT_LIST_HEAD(&loghdr_meta->link);

	/*
	if (g_fs_log->outstanding == 0 ) {
		mlfs_debug("outstanding %d\n", g_fs_log->outstanding);
		panic("outstanding\n");
	}
	*/
}

void abort_log_tx(void)
{
	struct logheader_meta *loghdr_meta;

	loghdr_meta = get_loghdr_meta();

	if (loghdr_meta->is_hdr_allocated)
		mlfs_free(loghdr_meta->loghdr);

#ifndef CONCURRENT
	pthread_mutex_unlock(g_log_mutex_shared);
	g_fs_log->outstanding--;
#endif

	return;
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void commit_log_tx(void)
{
	int do_commit = 0;

	/*
	if(g_fs_log->outstanding > 0) {
		do_commit = 1;
	} else {
		panic("commit when no outstanding tx\n");
	}
	*/

	do_commit = 1;

	if(do_commit) {
		uint64_t tsc_begin;
		struct logheader_meta *loghdr_meta;

		if (enable_perf_stats)
			tsc_begin = asm_rdtscp();

		commit_log();

		loghdr_meta = get_loghdr_meta();

		if (loghdr_meta->is_hdr_allocated)
			mlfs_free(loghdr_meta->loghdr);
		
#ifndef CONCURRENT
		g_fs_log->outstanding--;
		pthread_mutex_unlock(g_log_mutex_shared);
		mlfs_debug("commit log_tx %u\n", g_fs_log->outstanding);
#endif
		if (enable_perf_stats) {
			g_perf_stats.log_commit_tsc += (asm_rdtscp() - tsc_begin);
			g_perf_stats.log_commit_nr++;
		}
	} else {
		panic("it has a race condition\n");
	}
}

static int persist_log_inode(struct logheader_meta *loghdr_meta, uint32_t idx)
{
	struct inode *ip;
	addr_t logblk_no;
	uint32_t nr_logblocks = 0;
	struct buffer_head *log_bh;
	struct logheader *loghdr = loghdr_meta->loghdr;
	uint64_t start_tsc;

	logblk_no = loghdr_meta->log_blocks + loghdr_meta->pos;
	loghdr_meta->pos++;

	mlfs_assert(loghdr_meta->pos <= loghdr_meta->nr_log_blocks);

	if (enable_perf_stats)
		start_tsc = asm_rdtscp();

	log_bh = bh_get_sync_IO(g_fs_log->dev, logblk_no, BH_NO_DATA_ALLOC);

	if (enable_perf_stats) {
		g_perf_stats.bcache_search_tsc += (asm_rdtscp() - start_tsc);
		g_perf_stats.bcache_search_nr++;
	}

	loghdr->blocks[idx] = logblk_no;

	ip = icache_find(g_root_dev, loghdr->inode_no[idx]);
	mlfs_assert(ip);

	log_bh->b_data = (uint8_t *)ip->_dinode;
	log_bh->b_size = sizeof(struct dinode);
	log_bh->b_offset = 0;

	if (ip->flags & I_DELETING) {
		// icache_del(ip);
		// ideleted_add(ip);
		ip->flags &= ~I_VALID;
		bitmap_clear(sb[ip->dev]->s_inode_bitmap, ip->inum, 1);
	}

	nr_logblocks = 1;

	mlfs_assert(log_bh->b_blocknr < g_fs_log->next_avail);
	mlfs_assert(log_bh->b_dev == g_fs_log->dev);

	mlfs_debug("inum %u offset %lu @ blockno %lx\n",
				loghdr->inode_no[idx], loghdr->data[idx], logblk_no);

	mlfs_write(log_bh);

	bh_release(log_bh);

	//mlfs_assert((log_bh->b_blocknr + nr_logblocks) == g_fs_log->next_avail);

	return 0;
}

static int persist_log_directory_unopt(struct logheader_meta *loghdr_meta, uint32_t idx)
{
	struct inode *dir_ip;
	uint8_t *dirent_array;
	addr_t logblk_no;
	uint32_t nr_logblocks = 0;
	struct buffer_head *log_bh;
	struct logheader *loghdr = loghdr_meta->loghdr;

	logblk_no = log_alloc(1);
	loghdr->blocks[idx] = logblk_no;

	log_bh = bh_get_sync_IO(g_fs_log->dev, logblk_no, BH_NO_DATA_ALLOC);

	dir_ip = icache_find(g_root_dev, loghdr->inode_no[idx]);
	mlfs_assert(dir_ip);

	dirent_array = get_dirent_block(dir_ip, 0);
	mlfs_assert(dirent_array);

	//pthread_spin_lock(&log_bh->b_spinlock);

	log_bh->b_data = dirent_array;
	log_bh->b_size = g_block_size_bytes;

	nr_logblocks = 1;

	mlfs_assert(log_bh->b_blocknr < g_fs_log->next_avail);
	mlfs_assert(log_bh->b_dev == g_fs_log->dev);

	mlfs_debug("inum %u offset %lu @ blockno %lx\n",
				loghdr->inode_no[idx], loghdr->data[idx], logblk_no);

	// write data to log synchronously
	mlfs_write(log_bh);  

	bh_release(log_bh);

	//pthread_spin_unlock(&log_bh->b_spinlock);

	mlfs_assert((log_bh->b_blocknr + nr_logblocks) == g_fs_log->next_avail);

	return 0;
}

/* This is a critical path for write performance.
 * Stay optimized and need to be careful when modifying it */
static int persist_log_file(struct logheader_meta *loghdr_meta, 
		uint32_t idx, uint8_t n_iovec)
{
	uint32_t k, l, size;
	offset_t key;
	struct fcache_block *fc_block;
	addr_t logblk_no;
	uint32_t nr_logblocks = 0;
	struct buffer_head *log_bh;
	struct logheader *loghdr = loghdr_meta->loghdr;
	uint32_t io_size;
	struct inode *inode;
	lru_key_t lru_entry;
	uint64_t start_tsc, start_tsc_tmp;
	int ret;

	inode = icache_find(g_root_dev, loghdr->inode_no[idx]);

	mlfs_assert(inode);

	size = loghdr_meta->io_vec[n_iovec].size;

	// Handling small write (< 4KB).
	if (size < g_block_size_bytes) {
		// fc_block invalidation and coalescing.
		// 1. find fc_block -> if not exist, allocate fc_block and perform writes
		// -> if exist, fc_block may or may not be valid.
		// 2. if fc_block is valid, then do coalescing.
		// 3. if fc_block is not valid, then skip coalescing and update fc_block.

		uint32_t offset_in_block;

		key = (loghdr->data[idx] >> g_block_size_shift);
		offset_in_block = (loghdr->data[idx] % g_block_size_bytes);

		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		fc_block = fcache_find(inode, key);

		if (enable_perf_stats) {
			g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.l0_search_nr++;
		}

		logblk_no = loghdr_meta->log_blocks + loghdr_meta->pos;
		loghdr_meta->pos++;

		if (fc_block) {
			ret = check_log_invalidation(fc_block);
			// fc_block is invalid. update it
			if (ret) {
				fc_block->log_version = g_fs_log->avail_version;
				fc_block->log_addr = logblk_no;
			}
			// fc_block is valid
			else {
				if (fc_block->log_addr)  {
					logblk_no = fc_block->log_addr;
					fc_block->log_version = g_fs_log->avail_version;
					mlfs_debug("write is coalesced %lu @ %lu\n", loghdr->data[idx], logblk_no);
				}
			}
		}

		if (!fc_block) {
			mlfs_assert(loghdr_meta->pos <= loghdr_meta->nr_log_blocks);

			fc_block = fcache_alloc_add(inode, key, logblk_no);
			fc_block->log_version = g_fs_log->avail_version;
		} 
		
		if (enable_perf_stats)
			start_tsc = asm_rdtscp();

		log_bh = bh_get_sync_IO(g_fs_log->dev, logblk_no, BH_NO_DATA_ALLOC);

		if (enable_perf_stats) {
			g_perf_stats.bcache_search_tsc += (asm_rdtscp() - start_tsc);
			g_perf_stats.bcache_search_nr++;
		}

		// the logblk_no could be either a new block or existing one (patching case).
		loghdr->blocks[idx] = logblk_no;

		// case 1. the IO fits into one block.
		if (offset_in_block + size <= g_block_size_bytes)
			io_size = size;
		// case 2. the IO incurs two blocks write (unaligned).
		else 
			panic("do not support this case yet\n");

		log_bh->b_data = loghdr_meta->io_vec[n_iovec].base;
		log_bh->b_size = io_size;
		log_bh->b_offset = offset_in_block;

		mlfs_assert(log_bh->b_dev == g_fs_log->dev);

		mlfs_debug("inum %u offset %lu @ blockno %lx (partial io_size=%u)\n",
				loghdr->inode_no[idx], loghdr->data[idx], logblk_no, io_size);

		mlfs_write(log_bh); 

		bh_release(log_bh);
	} 
	// Handling large (possibly multi-block) write.
	else {
		offset_t cur_offset;

		if (enable_perf_stats)
			start_tsc_tmp = asm_rdtscp();

		cur_offset = loghdr->data[idx];

		/* logheader of multi-block is always 4K aligned.
		 * It is guaranteed by mlfs_file_write() */
		mlfs_assert((loghdr->data[idx] % g_block_size_bytes) == 0);
		mlfs_assert((size % g_block_size_bytes) == 0);

		nr_logblocks = size >> g_block_size_shift; 

		mlfs_assert(nr_logblocks > 0);

		logblk_no = loghdr_meta->log_blocks + loghdr_meta->pos;
		loghdr_meta->pos += nr_logblocks;

		mlfs_assert(loghdr_meta->pos <= loghdr_meta->nr_log_blocks);

		log_bh = bh_get_sync_IO(g_fs_log->dev, logblk_no, BH_NO_DATA_ALLOC);

		log_bh->b_data = loghdr_meta->io_vec[n_iovec].base;
		log_bh->b_size = size;
		log_bh->b_offset = 0;

		loghdr->blocks[idx] = logblk_no;

		// Update log address hash table.
		// This is performance bottleneck of sequential write.
		for (k = 0, l = 0; l < size; l += g_block_size_bytes, k++) {
			key = (cur_offset + l) >> g_block_size_shift;

			mlfs_assert(logblk_no);

			if (enable_perf_stats)
				start_tsc = asm_rdtscp();

			fc_block = fcache_find(inode, key);

			if (enable_perf_stats) {
				g_perf_stats.l0_search_tsc += (asm_rdtscp() - start_tsc);
				g_perf_stats.l0_search_nr++;
			}

			if (!fc_block) {
				fc_block = fcache_alloc_add(inode, key, logblk_no + k);
				fc_block->log_version = g_fs_log->avail_version;
			} else {
				fc_block->log_version = g_fs_log->avail_version;
				fc_block->log_addr = logblk_no + k;
			}
		}

		mlfs_debug("inum %u offset %lu size %u @ blockno %lx (aligned)\n",
				loghdr->inode_no[idx], cur_offset, size, logblk_no);

		mlfs_write(log_bh);

		bh_release(log_bh);

		if (enable_perf_stats) {
			g_perf_stats.tmp_tsc += (asm_rdtscp() - start_tsc_tmp);
		}
	}

	return 0;
}

static uint32_t compute_log_blocks(struct logheader_meta *loghdr_meta)
{
	struct logheader *loghdr = loghdr_meta->loghdr; 
	uint8_t type, n_iovec; 
	uint32_t nr_log_blocks = 0;
	int i;

	for (i = 0, n_iovec = 0; i < loghdr->n; i++) {
		type = loghdr->type[i];

		switch(type) {
			case L_TYPE_UNLINK:
			case L_TYPE_INODE_CREATE:
			case L_TYPE_INODE_UPDATE: {
				nr_log_blocks++;
				break;
			} 
			case L_TYPE_DIR_ADD: {
				break;
			}
			case L_TYPE_DIR_RENAME: {
				break;
			}
			case L_TYPE_DIR_DEL: {
				break;
			}
			case L_TYPE_FILE: {
				uint32_t size;
				size = loghdr_meta->io_vec[n_iovec].size;

				if (size < g_block_size_bytes)
					nr_log_blocks++;
				else
					nr_log_blocks += 
						(size >> g_block_size_shift);
				n_iovec++;
				break;
			}
			default: {
				panic("unsupported log type\n");
				break;
			}
		}
	}

	return nr_log_blocks;
}

// Copy modified blocks from cache to log.
static void persist_log_blocks(struct logheader_meta *loghdr_meta)
{ 
	struct logheader *loghdr = loghdr_meta->loghdr; 
	uint32_t i, nr_logblocks = 0; 
	uint8_t type, n_iovec; 
	addr_t logblk_no; 

	//mlfs_assert(hdr_blkno >= g_fs_log->start);

	for (i = 0, n_iovec = 0; i < loghdr->n; i++) {
		type = loghdr->type[i];

		switch(type) {
			case L_TYPE_UNLINK:
			case L_TYPE_INODE_CREATE:
			case L_TYPE_INODE_UPDATE: {
				persist_log_inode(loghdr_meta, i);
				break;
			} 
				/* Directory information is piggy-backed in 
				 * log header */
			case L_TYPE_DIR_ADD: {
				break;
			}
			case L_TYPE_DIR_RENAME: {
				break;
			}
			case L_TYPE_DIR_DEL: {
				break;
			}
			case L_TYPE_FILE: {
				persist_log_file(loghdr_meta, i, n_iovec);
				n_iovec++;
				break;
			}
			default: {
				panic("unsupported log type\n");
				break;
			}
		}
	}
}

static void commit_log(void)
{
	struct logheader_meta *loghdr_meta;
	struct logheader *loghdr;
	uint64_t tsc_begin, tsc_end;

	// loghdr_meta is stored in TLS.
	loghdr_meta = get_loghdr_meta();
	mlfs_assert(loghdr_meta);

	/* There was no log update during transaction */
	if (!loghdr_meta->is_hdr_allocated)
		return;

	mlfs_assert(loghdr_meta->loghdr);
	loghdr = loghdr_meta->loghdr;

	if (loghdr->n <= 0)
		panic("empty log header\n");

	if (loghdr->n > 0) {
		uint32_t nr_log_blocks;

		// Pre-compute required log blocks for atomic append.
		nr_log_blocks = compute_log_blocks(loghdr_meta);
		nr_log_blocks++; // +1 for a next log header block;

		pthread_mutex_lock(g_fs_log->shared_log_lock);

		// atomic log allocation.
		loghdr_meta->log_blocks = log_alloc(nr_log_blocks);
		loghdr_meta->nr_log_blocks = nr_log_blocks;
		// loghdr_meta->pos = 0 is used for log header block.
		loghdr_meta->pos = 1;

		loghdr_meta->hdr_blkno = g_fs_log->next_avail_header;
		g_fs_log->next_avail_header = loghdr_meta->log_blocks + loghdr_meta->nr_log_blocks;

		loghdr->next_loghdr_blkno = g_fs_log->next_avail_header;
		loghdr->inuse = LH_COMMIT_MAGIC;

		pthread_mutex_unlock(g_fs_log->shared_log_lock);

		mlfs_debug("pid %u [commit] log block %lu nr_log_blocks %u\n",
				getpid(), loghdr_meta->log_blocks, loghdr_meta->nr_log_blocks);
		mlfs_debug("pid %u [commit] current header %lu next header %lu\n", 
				getpid(), loghdr_meta->hdr_blkno, g_fs_log->next_avail_header);

		if (enable_perf_stats)
			tsc_begin = asm_rdtscp();

		/* Crash consistent order: log blocks write
		 * is followed by log header write */
		persist_log_blocks(loghdr_meta);

		if (enable_perf_stats) {
			tsc_end = asm_rdtscp();
			g_perf_stats.log_write_tsc += (tsc_end - tsc_begin);
		}

#if 0
		if(loghdr->next_loghdr_blkno != g_fs_log->next_avail_header) {
			printf("loghdr_blkno %lu, next_avail %lu\n",
					loghdr->next_loghdr_blkno, g_fs_log->next_avail_header);
			panic("loghdr->next_loghdr_blkno is tainted\n");
		}
#endif

		if (enable_perf_stats)
			tsc_begin = asm_rdtscp();

		// Write log header to log area (real commit)
		persist_log_header(loghdr_meta, loghdr_meta->hdr_blkno);

		if (enable_perf_stats) {
			tsc_end = asm_rdtscp();
			g_perf_stats.loghdr_write_tsc += (tsc_end - tsc_begin);
		}

		atomic_fetch_add(&g_log_sb->n_digest, 1);

		mlfs_assert(loghdr_meta->loghdr->next_loghdr_blkno
				>= g_fs_log->log_sb_blk);
	}
}

void add_to_loghdr(uint8_t type, struct inode *inode, offset_t data, 
		uint32_t length, void *extra, uint16_t extra_len)
{
	uint32_t i;
	struct logheader *loghdr;
	struct logheader_meta *loghdr_meta;

	loghdr_meta = get_loghdr_meta();

	mlfs_assert(loghdr_meta);

	if (!loghdr_meta->is_hdr_allocated) {
		loghdr = (struct logheader *)mlfs_zalloc(sizeof(*loghdr));

		loghdr_meta->loghdr = loghdr;
		loghdr_meta->is_hdr_allocated = 1;
	}

	loghdr = loghdr_meta->loghdr;

	if (loghdr->n >= g_fs_log->size)
		panic("too big a transaction for log");

	/*
		 if (g_fs_log->outstanding < 1)
		 panic("add_to_loghdr: outside of trans");
		 */

	i = loghdr->n;

	if (i >= g_max_blocks_per_operation)
		panic("log header is too small\n");

	loghdr->type[i] = type;
	loghdr->inode_no[i] = inode->inum;

	if (type == L_TYPE_FILE) 
		// offset in file.
		loghdr->data[i] = (offset_t)data;
	else if (type == L_TYPE_DIR_ADD ||
			type == L_TYPE_DIR_RENAME ||
			type == L_TYPE_DIR_DEL) {
		// dirent inode number.
		loghdr->data[i] = (uint32_t)data;
	} else
		loghdr->data[i] = 0;

	loghdr->length[i] = length;
	loghdr->n++;

	if (extra_len) {
		uint16_t ext_used = loghdr_meta->ext_used;

		loghdr_meta->loghdr_ext[ext_used] =  '0' + i;
		ext_used++;
		memmove(&loghdr_meta->loghdr_ext[ext_used], extra, extra_len);
		ext_used += extra_len;
		strncat((char *)&loghdr_meta->loghdr_ext[ext_used], "|", 1);
		ext_used++;
		loghdr_meta->loghdr_ext[ext_used] = '\0';
		loghdr_meta->ext_used = ext_used;

		mlfs_assert(ext_used <= 2048);
	}

	/*
		 if (type != L_TYPE_FILE)
		 mlfs_debug("[loghdr-add] dev %u, type %u inum %u data %lu\n",
		 inode->dev, type, inode->inum, data);
		 */
}

////////////////////////////////////////////////////////////////////
// Libmlfs digest thread.

void wait_on_digesting()
{
	uint64_t tsc_begin, tsc_end;
	if (enable_perf_stats) 
		tsc_begin = asm_rdtsc();

	while(g_fs_log->digesting)
		cpu_relax();

	if (enable_perf_stats) {
		tsc_end = asm_rdtsc();
		g_perf_stats.digest_wait_tsc += (tsc_end - tsc_begin);
		g_perf_stats.digest_wait_nr++;
	}
}

int make_digest_request_async(int percent)
{
	char cmd_buf[MAX_CMD_BUF] = {0};
	int ret = 0;

	sprintf(cmd_buf, "|digest |%d|", percent);

	if (!g_fs_log->digesting && atomic_load(&g_log_sb->n_digest) > 0) {
		set_digesting();
		mlfs_debug("Send digest command: %s\n", cmd_buf);
		ret = write(g_fs_log->digest_fd[1], cmd_buf, MAX_CMD_BUF);
		return 0;
	} else
		return -EBUSY;
}

uint32_t make_digest_request_sync(int percent)
{
	int ret, i;
	char cmd[MAX_SOCK_BUF];
	uint32_t digest_count = 0, n_digest;
	loghdr_t *loghdr;
	addr_t loghdr_blkno = g_fs_log->start_blk;
	struct inode *ip;

	g_log_sb->start_digest = g_fs_log->start_blk;
	write_log_superblock(g_log_sb);

	n_digest = atomic_load(&g_log_sb->n_digest);

	g_fs_log->n_digest_req = (percent * n_digest) / 100;
	socklen_t len = sizeof(struct sockaddr_un);
	sprintf(cmd, "|digest |%d|%u|%lu|%lu|",
			g_fs_log->dev, g_fs_log->n_digest_req, g_log_sb->start_digest, 0UL);

	mlfs_info("%s\n", cmd);

	// send digest command
	ret = sendto(g_sock_fd, cmd, MAX_SOCK_BUF, 0, 
			(struct sockaddr *)&g_srv_addr, len);

	return n_digest;
}

static void cleanup_lru_list(int lru_updated)
{
	lru_node_t *node, *tmp;
	int i = 0;

	pthread_rwlock_wrlock(shm_lru_rwlock);

	list_for_each_entry_safe_reverse(node, tmp, &lru_heads[g_log_dev], list) {
		HASH_DEL(lru_hash, node);
		list_del(&node->list);
		mlfs_free_shared(node);
	}

	pthread_rwlock_unlock(shm_lru_rwlock);
}

void handle_digest_response(char *ack_cmd)
{
	char ack[10] = {0};
	addr_t next_hdr_of_digested_hdr;
	int n_digested, rotated, lru_updated;
	struct inode *inode, *tmp;

	sscanf(ack_cmd, "|%s |%d|%lu|%d|%d|", ack, &n_digested, 
			&next_hdr_of_digested_hdr, &rotated, &lru_updated);

	if (g_fs_log->n_digest_req == n_digested)  {
		mlfs_info("%s", "digest is done correctly\n");
		mlfs_info("%s", "-----------------------------------\n");
	} else {
		mlfs_printf("[D] digest is done insufficiently: req %u | done %u\n",
				g_fs_log->n_digest_req, n_digested);
		panic("Digest was incorrect!\n");
	}

	mlfs_debug("g_fs_log->start_blk %lx, next_hdr_of_digested_hdr %lx\n",
			g_fs_log->start_blk, next_hdr_of_digested_hdr);

	if (rotated) {
		g_fs_log->start_version++;
		mlfs_debug("g_fs_log start_version = %d\n", g_fs_log->start_version);
	}

	// change start_blk
	g_fs_log->start_blk = next_hdr_of_digested_hdr;
	g_log_sb->start_digest = next_hdr_of_digested_hdr;

	// adjust g_log_sb->n_digest properly
	atomic_fetch_sub(&g_log_sb->n_digest, n_digested);

	//Start cleanup process after digest is done.

	//cleanup_lru_list(lru_updated);

	// TODO: optimize this. Now sync all inodes in the inode_hash.
	// As the optimization, Kernfs sends inodes lists (via shared memory),
	// and Libfs syncs inodes based on the list.
	HASH_ITER(hash_handle, inode_hash[g_root_dev], inode, tmp) {
		if (!(inode->flags & I_DELETING)) {
			if (inode->itype == T_FILE) 
				sync_inode_ext_tree(g_root_dev, inode);
			else if(inode->itype == T_DIR)
				;
			else
				panic("unsupported inode type\n");
		}
	}

	// persist log superblock.
	write_log_superblock(g_log_sb);

	//xchg_8(&g_fs_log->digesting, 0);
	clear_digesting();

	if (enable_perf_stats) 
		show_libfs_stats("digest response");
}

#define EVENT_COUNT 2
void *digest_thread(void *arg)
{
	int epfd, kernfs_epfd, ret, n, flags;
	char buf[MAX_SOCK_BUF] = {0}, cmd_buf[MAX_CMD_BUF] = {0};
	struct epoll_event kernfs_epev = {0}, epev[EVENT_COUNT] = {0};
	struct sockaddr_un srv_addr;

	// setup server address
	memset(&g_srv_addr, 0, sizeof(g_addr));
	g_srv_addr.sun_family = AF_UNIX;
	strncpy(g_srv_addr.sun_path, SRV_SOCK_PATH, sizeof(g_srv_addr.sun_path));

	// Create domain socket for sending digest command to kernfs
	if ((g_sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
		panic("fail to create socket\n");

	memset(&g_addr, 0, sizeof(g_addr));
	g_addr.sun_family = AF_UNIX;
	snprintf(g_addr.sun_path, sizeof(g_addr.sun_path), 
			"/tmp/mlfs_cli.%ld", (long) getpid());

	unlink(g_addr.sun_path);

	if (bind(g_sock_fd, (struct sockaddr *)&g_addr, 
				sizeof(struct sockaddr_un)) == -1)
		panic("bind error\n");

	flags = fcntl(g_sock_fd, F_GETFL, 0);
	flags |= O_NONBLOCK;
	ret = fcntl(g_sock_fd, F_SETFL, flags);
	if (ret < 0)
		panic("fail to set non-blocking mode\n");

	// epoll for socket: communication with kernfs
	kernfs_epfd = epoll_create(1);
	if (kernfs_epfd < 0)
		panic("cannot create epoll fd\n");

	kernfs_epev.data.fd = g_sock_fd;
	kernfs_epev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
	ret = epoll_ctl(kernfs_epfd, EPOLL_CTL_ADD, g_sock_fd, 
			&kernfs_epev);
	if (ret < 0)
		panic("fail to connect epoll fd\n");

	// epoll for pipe and kernfs
	epfd = epoll_create(1);
	if (epfd < 0)
		panic("cannot create epoll fd\n");

	// waiting for digest command
	epev[0].data.fd = g_fs_log->digest_fd[0];
	epev[0].events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, 
			g_fs_log->digest_fd[0], &epev[0]);
	if (ret < 0)
		panic("fail to connect epoll fd\n");

	// waiting for kernfs message
	epev[1].data.fd = g_sock_fd;
	epev[1].events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, g_sock_fd, &epev[1]);
	if (ret < 0)
		panic("fail to connect epoll fd\n");

	*((int*)arg) = 1;

	mlfs_debug("%s\n", "digest thread starts");

	while(1) {
		int i;
		n = epoll_wait(epfd, epev, EVENT_COUNT, -1);

		if (n < 0 && errno != EINTR)
			panic("epoll wait problem: digest completion\n");

		for (i = 0; i < n; i++) {
			int _fd = epev[i].data.fd;
			socklen_t len = sizeof(struct sockaddr_un);

			if (_fd == g_fs_log->digest_fd[0]) {
				mlfs_debug("digest_pipe: event %d\n", epev[i].events);
				ret = read(_fd, cmd_buf, MAX_CMD_BUF);
				if (ret == 0)
					continue;

				mlfs_info("[D] cmd: %s\n", cmd_buf);

				// digest command
				if (cmd_buf[1] == 'd') {
					char cmd_header[10];
					int percent, j;
					uint32_t n_digest, digest_count = 0;
					addr_t loghdr_blkno;
					lru_node_t *node, *tmp;
					offset_t key;

					sscanf(cmd_buf, "|%s |%d|", cmd_header, &percent);
					n_digest = make_digest_request_sync(percent);

					loghdr_blkno = g_log_sb->start_digest;

#if 0
					// This has performance impact. I leave the code to improve later.
					// Build invalidate list before waiting ack from kernel FS.
					while (1) {
						loghdr_t *loghdr;
						struct fcache_block *fc_block;
						struct inode *inode;

						//mlfs_debug("read log header %lx\n", loghdr_blkno);
						loghdr = read_log_header(g_fs_log->dev, loghdr_blkno);

						if (loghdr->inuse != LH_COMMIT_MAGIC) {
							mlfs_printf("%s\n", "warning: early finish"
									" by meeting uncommited block\n");
							break;
						}

						for (j = 0; j < loghdr->n; j++) {
							if (loghdr->type[j] == L_TYPE_FILE) {
								//For inode resync from g_root_dev after digesting is done.
								inode = icache_find(g_root_dev, loghdr->inode_no[j]);
								mlfs_assert(inode);
								inode->flags |= I_RESYNC;

								key = (loghdr->data[j] >> g_block_size_shift);

								fc_block = fcache_find(inode, key);

								// the log block will be reclaimed so mark it as invalidated entry.
								if (fc_block)
									fc_block->invalidate = 1;

							}
						}

						digest_count++;
						loghdr_blkno = loghdr->next_loghdr_blkno;

						if (digest_count == n_digest)
							break;
					}
#endif
					// Waiting for ACK of digest from kernfs.
					ret = epoll_wait(kernfs_epfd, &kernfs_epev, 1, -1); 
					if (ret >= 0) {
						ret = recvfrom(g_sock_fd, buf, MAX_SOCK_BUF, 0, 
								(struct sockaddr *)&srv_addr, &len);

						mlfs_info("received %s\n", buf);

						handle_digest_response(buf);
					} 
				}
			} else if (_fd == g_sock_fd) {
				mlfs_debug("kernfs: event %d\n", epev[i].events);
				ret = recvfrom(g_sock_fd, buf, MAX_SOCK_BUF, 0, 
						(struct sockaddr *)&srv_addr, &len);
				if (ret == 0)
					continue;

				mlfs_debug("kernfs cmd: %s\n", buf);
			}
		} 
	}
}
