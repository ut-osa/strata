#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "mlfs/mlfs_user.h"
#include "global/global.h"
#include "storage/spdk/async.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t *spdk_init(uint8_t dev, char *dev_path)
{
	spdk_async_io_init();
}

int spdk_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	int ret;

	if (io_size < g_block_size_bytes) {
    fprintf(stderr, "io_size = %d, g_block_size_bytes = %lu\n", io_size,
        g_block_size_bytes);
		panic("cannot support IO size less than 4 KB\n");
  }

ISSUE_READ:
	ret = spdk_async_io_read((unsigned char *)buf, blockno, io_size,
			            NULL, NULL);

	if (ret == -1 && errno == EBUSY) {
    // (iangneal): In the multithreaded case, someone else may have done our
    // completions.
		//while(!spdk_process_completions(1));
    spdk_wait_completions(1);
    goto ISSUE_READ;
	}

	if (ret == -1 && errno == EFBIG)
		panic("[SPDK] total io divided by io unit is larger than the command queue\n");

	mlfs_assert(ret == io_size);

	return ret;
}

int spdk_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size)
{
	int ret;

	if (io_size < g_block_size_bytes)
		panic("cannot support IO size less than 4 KB\n");

ISSUE_WRITE:
	ret = spdk_async_io_write((unsigned char *)buf, blockno, io_size,
			            NULL, NULL);

	if (ret == -1 && errno == EBUSY) {
		//while(!spdk_process_completions(0));
    spdk_wait_completions(0);
    goto ISSUE_WRITE;
	}

	if (ret == -1 && errno == EFBIG)
		panic("[SPDK] total io divided by io unit is larger than the command queue\n");

  mlfs_assert(ret == io_size);

	return ret;
}

int spdk_erase(uint8_t dev, addr_t blockno, uint32_t io_size)
{
	spdk_io_trim(blockno, io_size);
	return 0;
}

int spdk_readahead(uint8_t dev, addr_t blockno, uint32_t io_size)
{
	spdk_async_readahead((uint32_t)blockno, io_size);
}

int spdk_wait_io(uint8_t dev, int read)
{
	spdk_wait_completions(read);

	return 0;
}

void spdk_exit(uint8_t dev)
{
	spdk_async_io_exit();
	return;
}

#ifdef __cplusplus
}
#endif
