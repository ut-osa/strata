#ifndef _LOG_H_
#define _LOG_H_

#include "concurrency/synchronization.h"
#include "filesystem/shared.h"
#include "filesystem/fs.h"
#include "io/block_io.h"
#include "global/global.h"
#include "global/types.h"
#include "global/util.h"
#include "ds/list.h"
#include "ds/stdatomic.h"

#include <pthread.h>

// On-disk metadata of log area
struct log_superblock {
	// block number of the first undigested logheader.
	addr_t start_digest;
	// # of loghdr to digest
	atomic_uint n_digest;
	
	addr_t loghdr_expect_to_digest;
};

// In-memory metadata for log area.
// Log format
// log_sb(sb_blknr)|..garbages..|log data(start_blknr ~ next_avail - 1)|unused area...
// can garbage collect from sb_blknr to start_blknr
struct fs_log {
	struct log_superblock *log_sb;
	uint8_t dev;
	// superblock number of log area (the first block).
	addr_t log_sb_blk;
	addr_t start_blk;
	// next available log data blockno 
	addr_t next_avail;
	// next available log header blockno 
	addr_t next_avail_header;
	// size of log as # of block.
	addr_t size;

	// how many transactions are executing.
	uint8_t outstanding;

	// digesting, please wait.
	uint8_t digesting;

	uint32_t start_version;
	uint32_t avail_version;
	uint32_t n_digest_req;

	// pipe fd to make digest request.
	int digest_fd[2];
	// # of logheaders in the lh_list.
	uint32_t nloghdr;

	// used for threads
	pthread_spinlock_t log_lock;
	// used for parent and child processes.
	pthread_mutex_t *shared_log_lock;
};

//forward declaration
struct inode;

extern struct fs_log *g_fs_log;

void init_log(int dev);
void add_to_loghdr(uint8_t type, struct inode *inode, offset_t data, 
		uint32_t length, void *extra, uint16_t extra_len);
void start_log_tx(void);
void abort_log_tx(void);
void commit_log_tx(void);

static inline void set_digesting(void)
{
	while (1) {
		//if (!xchg_8(&g_fs_log->digesting, 1)) 
		if (!cmpxchg(&g_fs_log->digesting, 0, 1)) 
			return;

		while (g_fs_log->digesting) 
			cpu_relax();
	}
}

static inline void clear_digesting(void)
{
	while (1) {
		//if (!xchg_8(&g_fs_log->digesting, 1)) 
		if (cmpxchg(&g_fs_log->digesting, 1, 0)) 
			return;

		while (g_fs_log->digesting) 
			cpu_relax();
	}
}

addr_t log_alloc(uint32_t nr_logblock);
void shutdown_log(void);

#endif
