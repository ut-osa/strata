#include "storage/storage.h"

struct block_device *g_bdev[g_n_devices + 1];

#if 1 
char *g_dev_path[] = {
	(char *)"unused",
	(char *)"/dev/dax0.0",
	(char *)"PCIe-SSD",
	(char *)"/backup/mlfs_hdd",
	(char *)"/dev/dax1.0",
	(char *)"/dev/dax2.0",
};
#else
char *g_dev_path[] = {
	(char *)"unused",
	(char *)"/dev/dax0.0",
	(char *)"/dev/dax1.0",
	(char *)"/backup/mlfs_hdd",
	(char *)"/dev/dax2.0",
};
#endif

#ifdef __cplusplus
struct storage_operations storage_dax = {
	dax_init,
	dax_read,
	dax_read_unaligned,
	dax_write,
	dax_write_unaligned,
	dax_erase,
	dax_commit,
	NULL,
	NULL,
	dax_exit,
};

struct storage_operations storage_spdk = {
	spdk_init,
	spdk_read,
	NULL,
	spdk_write,
	NULL,
	spdk_erase,
	NULL,
	spdk_wait_io,
	spdk_readahead,
	spdk_exit,
};

struct storage_operations storage_hdd = {
	hdd_init,
	hdd_read,
	NULL,
	hdd_write,
	NULL,
	NULL,
	hdd_commit,
	NULL,
	NULL,
	hdd_exit,
};

#else
struct storage_operations storage_dax = {
	.init = dax_init,
	.read = dax_read,
	.read_unaligned = dax_read_unaligned,
	.write = dax_write,
	.write_unaligned = dax_write_unaligned,
	.commit = dax_commit,
	.wait_io = NULL,
	.erase = dax_erase,
	.readahead = NULL,
	.exit = dax_exit,
};

struct storage_operations storage_spdk = {
	.init = spdk_init,
	.read = spdk_read,
	.read_unaligned = NULL,
	.write = spdk_write,
	.write_unaligned = NULL,
	.commit = NULL,
	.wait_io = spdk_wait_io,
	.erase = spdk_erase,
	.readahead = spdk_readahead,
	.exit = spdk_exit,
};

struct storage_operations storage_hdd = {
	.init = hdd_init,
	.read = hdd_read,
	.read_unaligned = NULL,
	.write = hdd_write,
	.write_unaligned = NULL,
	.commit = hdd_commit,
	.wait_io = NULL,
	.erase = NULL,
	.readahead = NULL,
	.exit = hdd_exit,
};

#endif
