#include <rte_config.h>
#include <rte_malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "common.h"
#include "sync.h"

/**********************************
 *
 *      QPAIR FUNCTIONS
 *
 **********************************/
#ifdef CONCURRENT
static int acquire_qpair(size_t n_ios) {
	struct ns_entry *ns_entry = g_namespaces;
  static volatile int try_me_first = 0;

  if (n_ios > Q_DEPTH) return -EFBIG;

  int f = try_me_first;

  for (int i = f; i < ns_entry->nqpairs + f; ++i) {
    int idx = i % ns_entry->nqpairs;

    int r = pthread_mutex_trylock(ns_entry->qtexs[idx]);
    if (r == 0) {
      try_me_first = (idx + 1) % ns_entry->nqpairs;
      return idx;
    } else if (r != EBUSY) {
      return -r;
    } // else, keep checking for an available qpair.
  }

  return -EBUSY;
}

static int release_qpair(int qpair) {
  return pthread_mutex_unlock(g_namespaces->qtexs[qpair]);
}
#endif

unsigned int spdk_sync_get_n_lbas(void) {
  return libspdk_get_n_lbas();
}

static void spdk_sync_io_read_callback(void *arg,
		const struct spdk_nvme_cpl *completion) {
	struct spdk_sync_io *io = arg;
	memcpy(io->guest_buffer, io->buffer, io->size);
	//spdk_free(io->buffer);
	rte_free(io->buffer);

	if(io->user_cb)
	  (*(io->user_cb))(io->user_arg);

	io->done = 1;
}

static void spdk_sync_io_write_callback(
		void *arg, const struct spdk_nvme_cpl *completion) {
	struct spdk_sync_io *io = arg;
	//spdk_free(io->buffer);
	rte_free(io->buffer);

	if(io->user_cb)
	  (*(io->user_cb))(io->user_arg);

	io->done = 1;
}

int spdk_sync_io_init(void) { return libspdk_init(); }

void spdk_sync_io_exit(void) { libspdk_exit(); }

// TODO: incoming buffer is DPDK (hugepage) or regular? See comment on
// spdk_block_io_write
// will save one copy operation if we use DPDK
int spdk_sync_io_read(uint8_t *guest_buffer, uint32_t blockno, uint32_t io_size,
		void(* cb)(void*), void* arg)
{

  int n_blocks = ceil(io_size/(double)BLOCK_SIZE);
  int idx = 0;
  int rc = -1;

	//printf("SPDK: reading block\t%d-%d\n", blockno, blockno+n_blocks-1);

	struct spdk_sync_io io = {
		.user_arg = arg,
		.user_cb = cb,
		.done = 0,
		//.buffer = spdk_zmalloc(n_blocks * BLOCK_SIZE, 0x1000, NULL),
		.buffer = rte_zmalloc(NULL, n_blocks * BLOCK_SIZE, 0x1000),
		.guest_buffer = guest_buffer,
		.size = io_size
	};

	struct ns_entry *ns_entry = g_namespaces;

#ifdef CONCURRENT
  while ((idx = acquire_qpair(n_blocks)) == -EBUSY);
#endif

	if (spdk_nvme_ns_cmd_read(
				ns_entry->ns, ns_entry->qpairs[idx], io.buffer,
        blockno, /* LBA start */
				n_blocks, /* number of LBAs */
				spdk_sync_io_read_callback, (void *)&io, 0) != 0) {
		fprintf(stderr, "starting read I/O failed\n");
		goto end;
	}

	while (!io.done) {
		spdk_nvme_qpair_process_completions(ns_entry->qpairs[idx], 0);
	}

  rc = 0;

end:
#ifdef CONCURRENT
  release_qpair(idx);
#endif

	return rc;
}

// TODO: Youngjin: was the data that needs to be written allocated using rte
// (DPDK)?
// if not, we need to allocate with it and copy (SPDK requirement), which is
// what this is doing for now
int spdk_sync_io_write(uint8_t *guest_buffer, uint32_t blockno, uint32_t io_size,
	void(* cb)(void*), void* arg)
{
  int n_blocks = ceil(io_size/(double)BLOCK_SIZE);
  int rc = -1, idx = 0;

	//printf("SPDK: writing block\t%d-%d\n", blockno, blockno+n_blocks-1);

	struct spdk_sync_io io = {
		.user_arg = arg,
		.user_cb = cb,
		.done = 0,
		//.buffer = spdk_zmalloc(n_blocks * BLOCK_SIZE, 0x1000, NULL),
		.buffer = rte_zmalloc(NULL, n_blocks * BLOCK_SIZE, 0x1000),
		.guest_buffer = guest_buffer,
		.size = io_size};

	memcpy(io.buffer, io.guest_buffer, io_size);

	struct ns_entry *ns_entry = g_namespaces;

#ifdef CONCURRENT
  while ((idx = acquire_qpair(n_blocks)) == -EBUSY);
#endif

	if (spdk_nvme_ns_cmd_write(
				ns_entry->ns, ns_entry->qpairs[idx], io.buffer,
        blockno, /* LBA start */
				n_blocks, /* number of LBAs */
				spdk_sync_io_write_callback, (void *)&io, 0) != 0) {
		fprintf(stderr, "starting write I/O failed\n");
    goto end;
	}

	while (!io.done) {
		spdk_nvme_qpair_process_completions(ns_entry->qpairs[idx], 0);
	}

  rc = 0;

end:

#if CONCURRENT
  release_qpair(idx);
#endif

	return rc;
}

