// to use O_DIRECT flag
//
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <libpmem.h>

#include "global/global.h"
#include "global/util.h"
#include "mlfs/mlfs_user.h"
#include "storage/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

// Set CPU frequency correctly
#define _CPUFREQ 2600LLU /* MHz */

#define NS2CYCLE(__ns) (((__ns) * _CPUFREQ) / 1000)
#define CYCLE2NS(__cycles) (((__cycles) * 1000) / _CPUFREQ)

#define BANDWIDTH_MONITOR_NS 10000
#define SEC_TO_NS(x) (x * 1000000000UL)

#define ENABLE_PERF_MODEL
#define ENABLE_BANDWIDTH_MODEL

// performance parameters
/* SCM read extra latency than DRAM */
uint32_t SCM_EXTRA_READ_LATENCY_NS = 150;
// We assume WBARRIER LATENCY is 0 since write back queue can hide this even in 
// power failure.
// https://software.intel.com/en-us/blogs/2016/09/12/deprecate-pcommit-instruction
uint32_t SCM_WBARRIER_LATENCY_NS = 0;

/* SCM write bandwidth */
uint32_t SCM_BANDWIDTH_MB = 8000;
/* DRAM system peak bandwidth */
uint32_t DRAM_BANDWIDTH_MB = 63000;

uint64_t *bandwidth_consumption;
static uint64_t monitor_start = 0, monitor_end = 0, now = 0;

pthread_mutex_t mlfs_nvm_mutex;

static inline void PERSISTENT_BARRIER(void)
{
	asm volatile ("sfence\n" : : );
}

///////////////////////////////////////////////////////

static uint8_t *dax_addr[g_n_devices + 1];
static size_t mapped_len[g_n_devices + 1];

static inline void emulate_latency_ns(int ns)
{
	uint64_t cycles, start, stop;

	start = asm_rdtscp();
	cycles = NS2CYCLE(ns);
	//printf("cycles %lu\n", cycles);

	do {
		/* RDTSC doesn't necessarily wait for previous instructions to complete
		 * so a serializing instruction is usually used to ensure previous
		 * instructions have completed. However, in our case this is a desirable
		 * property since we want to overlap the latency we emulate with the
		 * actual latency of the emulated instruction.
		 */
		stop = asm_rdtscp();
	} while (stop - start < cycles);
}

#if 0 //Aerie.
static void perfmodel_add_delay(int read, size_t size)
{
#ifdef ENABLE_PERF_MODEL
    uint32_t extra_latency;
#endif

#ifdef ENABLE_PERF_MODEL
	if (read) {
		extra_latency = SCM_EXTRA_READ_LATENCY_NS;
	} else {
#ifdef ENABLE_BANDWIDTH_MODEL
		// Due to the writeback cache, write does not have latency
		// but it has bandwidth limit.
		// The following is emulated delay when bandwidth is full
		extra_latency = (int)size * 
			(1 - (float)(((float) SCM_BANDWIDTH_MB)/1000) /
			 (((float)DRAM_BANDWIDTH_MB)/1000)) / (((float)SCM_BANDWIDTH_MB)/1000);
#else
		//No write delay.
		extra_latency = 0;
#endif
	}

    emulate_latency_ns(extra_latency);
#endif

    return;
}
#else // based on https://engineering.purdue.edu/~yiying/nvmmstudy-msst15.pdf
static void perfmodel_add_delay(int read, size_t size)
{
	static int warning = 0;
#ifdef ENABLE_PERF_MODEL
	uint32_t extra_latency;
	uint32_t do_bandwidth_delay;

	// Only allowed for mkfs.
	if (!bandwidth_consumption) {
		if (!warning)  {
			printf("\033[31m WARNING: Bandwidth tracking variable is not set."
					" Running program must be mkfs \033[0m\n");
			warning = 1;
		}
		return ;
	}

	now = asm_rdtscp();

	if (now >= monitor_end) {
		monitor_start = now;
		monitor_end = monitor_start + NS2CYCLE(BANDWIDTH_MONITOR_NS);
		*bandwidth_consumption = 0;
	} 

	if (__sync_add_and_fetch(bandwidth_consumption, size) >=
			((SCM_BANDWIDTH_MB << 20) / (SEC_TO_NS(1UL) / BANDWIDTH_MONITOR_NS)))
		do_bandwidth_delay = 1;
	else
		do_bandwidth_delay = 0;

	if (read) {
		extra_latency = SCM_EXTRA_READ_LATENCY_NS;
	} else
		extra_latency = SCM_WBARRIER_LATENCY_NS;

	// bandwidth delay for both read and write.
	if (do_bandwidth_delay) {
		// Due to the writeback cache, write does not have latency
		// but it has bandwidth limit.
		// The following is emulated delay when bandwidth is full
		extra_latency += (int)size *
			(1 - (float)(((float) SCM_BANDWIDTH_MB)/1000) /
			 (((float)DRAM_BANDWIDTH_MB)/1000)) / (((float)SCM_BANDWIDTH_MB)/1000);
		pthread_mutex_lock(&mlfs_nvm_mutex);
		emulate_latency_ns(extra_latency);
		pthread_mutex_unlock(&mlfs_nvm_mutex);
	} else
		emulate_latency_ns(extra_latency);

#endif
	return;
}
#endif

uint8_t *dax_init(uint8_t dev, char *dev_path)
{
	int fd;
	int is_pmem;
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&mlfs_nvm_mutex, &attr);

	fd = open(dev_path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "cannot open dax device %s\n", dev_path);
		exit(-1);
	}

	dax_addr[dev] = (uint8_t *)mmap(NULL, dev_size[dev], PROT_READ | PROT_WRITE, 
			MAP_SHARED| MAP_POPULATE, fd, 0);

	if (dax_addr[dev] == MAP_FAILED) {
		perror("cannot map file system file");
		exit(-1);
	}

	// FIXME: for some reason, when mmap the Linux dev-dax, dax_addr is not accessible
	// up to the max dev_size (last 550 MB is not accessible).
	dev_size[dev] -= (550 << 20);

	printf("dev-dax engine is initialized: dev_path %s size %lu MB\n", 
			dev_path, dev_size[dev] >> 20);

	return dax_addr[dev];
}

int dax_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	memmove(buf, dax_addr[dev] + (blockno * g_block_size_bytes), io_size);

	perfmodel_add_delay(1, io_size);

	//mlfs_debug("read block number %d\n", blockno);

	return io_size;
}

int dax_read_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset, 
		uint32_t io_size)
{
	//copy and flush data to pmem.
	memmove(buf, dax_addr[dev] + (blockno * g_block_size_bytes) + offset, 
			io_size);

	perfmodel_add_delay(1, io_size);
	
	/*
	mlfs_debug("read block number %lu, address %lu size %u\n", 
			blockno, (blockno * g_block_size_bytes) + offset, io_size);
	*/

	return io_size;
}

/* optimization: instead of calling sfence every write,
 * dax_write just does memory copy. When libmlfs commits transaction,
 * it call dax_commit to drain changes (like pmem_memmove_persist) */
int dax_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	addr_t addr = (addr_t)dax_addr[dev] + (blockno << g_block_size_shift);

	//copy and flush data to pmem.
	pmem_memmove_persist((void *)addr, buf, io_size);
	PERSISTENT_BARRIER();

	//memmove(dax_addr[dev] + (blockno * g_block_size_bytes), buf, io_size);
	perfmodel_add_delay(0, io_size);

	mlfs_muffled("write block number %lu, address %lu size %u\n", 
			blockno, (blockno * g_block_size_bytes), io_size);

	return io_size;
}

int dax_write_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset, 
		uint32_t io_size)
{
	addr_t addr = (addr_t)dax_addr[dev] + (blockno << g_block_size_shift) + offset;

	//copy and flush data to pmem.
	pmem_memmove_persist((void *)addr, buf, io_size);
	PERSISTENT_BARRIER();

	//memmove(dax_addr[dev] + (blockno * g_block_size_bytes) + offset, buf, io_size);
	perfmodel_add_delay(0, io_size);

	mlfs_muffled("write block number %lu, address %lu size %u\n", 
			blockno, (blockno * g_block_size_bytes) + offset, io_size);

	return io_size;
}

int dax_commit(uint8_t dev)
{
	return 0;
}

int dax_erase(uint8_t dev, addr_t blockno, uint32_t io_size)
{
	memset(dax_addr[dev] + (blockno * g_block_size_bytes), 0, io_size);

	perfmodel_add_delay(0, io_size);
}

void dax_exit(uint8_t dev)
{
	munmap(dax_addr[dev], dev_size[dev]);

	return;
}

#ifdef __cplusplus
}
#endif
