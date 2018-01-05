#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include "filesystem/fs.h"
#include "io/block_io.h"
#include "global/global.h"
#include "storage/storage.h"
#include "mlfs/kerncompat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define g_root_dev 1
#define g_ssd_dev 2
#define g_hdd_dev 3
#define g_log_dev 4

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define dbg_printf
//#define dbg_printf printf

// In-kernel fs Disk layout:
// [ sb block | inode blocks | free bitmap | data blocks ]
// [ inode block | free bitmap | data blocks ] is a block group.
// If data blocks is full, then file system will allocate a new block group.

int dev_id, fsfd;
struct disk_superblock ondisk_sb;
char zeroes[g_block_size_bytes];
addr_t freeinode = 1;
addr_t freeblock;

void balloc(int);
void wsect(addr_t, uint8_t *buf);
void rsect(addr_t sec, uint8_t *buf);
void write_inode(uint32_t inum, struct dinode *dip);
void read_inode(uint8_t dev, uint32_t inum, struct dinode *dip);
uint32_t mkfs_ialloc(uint8_t dev, uint16_t type);
void iappend(uint8_t dev, uint32_t inum, void *p, int n);
void mkfs_read_superblock(uint8_t dev, struct disk_superblock *disk_sb);

// Inodes per block.
#define IPB           (g_block_size_bytes / sizeof(struct dinode))
// Block containing inode i
static inline addr_t mkfs_get_inode_block(uint32_t inum, addr_t inode_start)
{
	return (inum /IPB) + inode_start;
}

typedef enum {NON, NVM, SPDK, HDD, FS} storage_mode_t;

storage_mode_t storage_mode;

#if 0
#ifdef __cplusplus
static struct storage_operations spdk_storage_ops = {
	spdk_init,
	spdk_read,
	NULL,
	spdk_write,
	NULL,
	spdk_erase,
	NULL,
	spdk_wait_io,
	spdk_exit
};

static struct storage_operations nvm_storage_ops = {
	dax_init,
	dax_read,
	NULL,
	dax_write,
	NULL,
	dax_erase,
	dax_commit,
	NULL,
	dax_exit
};

struct storage_operations storage_hdd = {
	hdd_init,
	hdd_read,
	NULL,
	hdd_write,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	hdd_exit,
};
#else
static struct storage_operations spdk_storage_ops = {
	.init = spdk_init,
	.read = spdk_read,
	.write = spdk_write,
	.erase  = spdk_erase,
	.commit = NULL,
	.wait_io = spdk_wait_io,
	.exit = spdk_exit,
};

static struct storage_operations nvm_storage_ops = {
	.init = dax_init,
	.read = dax_read,
	.erase = dax_erase,
	.write = dax_write,
	.commit = dax_commit,
	.wait_io = NULL,
	.exit = dax_exit,
};

struct storage_operations storage_hdd = {
	.init = hdd_init,
	.read = hdd_read,
	.read_unaligned = NULL,
	.write = hdd_write,
	.write_unaligned = NULL,
	.commit = NULL,
	.wait_io = NULL,
	.erase = NULL,
	.readahead = NULL,
	.exit = hdd_exit,
};
#endif
#endif

#define xshort(x) x
#define xint(x) x

int main(int argc, char *argv[])
{
	int i, cc, fd;
	uint32_t rootino, mlfs_dir_ino;
	addr_t off;
	struct mlfs_dirent de;
	uint8_t buf[g_block_size_bytes];
	struct dinode din;
	uint32_t nbitmap;
	int ninodeblocks = NINODES / IPB + 1;
	uint64_t file_size_bytes;
	uint64_t file_size_blks, log_size_blks;
	uint64_t nlog, ndatablocks;
	unsigned int nmeta;    // Number of meta blocks (boot, sb, inode, bitmap)

	static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

	if (argc < 2) {
		fprintf(stderr, "Usage: mkfs dev_id\n");
		exit(1);
	}

	dev_id = atoi(argv[1]);
	printf("dev_id %d\n", dev_id);

	if (dev_id == 1) {
		printf("Formating fs with DEVDAX\n");
		storage_mode = NVM;
	} else if (dev_id == 2) {
		printf("Formating fs with SPDK\n");
		storage_mode = SPDK;
		//storage_mode = NVM;
	} else if (dev_id == 3) {
		printf("Formating fs with HDD\n");
		storage_mode = HDD;
	} else {
		printf("Formating fs with DEVDAX\n");
		storage_mode = NVM;
	}

	// Bypass dev-dax mmap problem: use actual device size - 550 MB.
	file_size_bytes = dev_size[dev_id] - (550 << 20);
	file_size_blks = file_size_bytes >> g_block_size_shift;
	log_size_blks = file_size_blks - (1UL * (1 << 10));
	nbitmap = file_size_blks / (g_block_size_bytes * 8) + 1;

	printf("Ondisk inode size = %lu\n", sizeof(struct dinode));

	/* all invariants check */
	if (g_block_size_bytes % sizeof(struct dinode) != 0) {
		printf("dinode size (%lu) is not power of 2\n",
				sizeof(struct dinode));
		exit(-1);
	}

	//mlfs_assert((g_block_size_bytes % sizeof(struct dirent)) == 0);
	if (g_block_size_bytes % sizeof(struct mlfs_dirent) != 0) {
		printf("dirent (size %lu) should be power of 2\n",
				sizeof(struct mlfs_dirent));
		exit(-1);
	}

	/* nmeta = Empty block + Superblock + inode block + block allocation bitmap block */
	if (dev_id == g_root_dev) {
		nmeta = 2 + ninodeblocks + nbitmap;
		ndatablocks = file_size_blks - nmeta;
		nlog = 0;
	}
	// SSD and HDD case.
	else if (dev_id <= g_hdd_dev) {
		nmeta = 2 + ninodeblocks + nbitmap;
		ndatablocks = file_size_blks - nmeta;
		nlog = 0;
	}
	// Per-application log.
	else {
		nmeta = 2 + ninodeblocks + nbitmap;
		ndatablocks = 0;
		nlog = log_size_blks;
	}

	// Fill superblock data
	ondisk_sb.size = file_size_blks;
	ondisk_sb.ndatablocks = ndatablocks;
	ondisk_sb.ninodes = NINODES;
	ondisk_sb.nlog = nlog;
	ondisk_sb.inode_start = 2;
	ondisk_sb.bmap_start = 2 + ninodeblocks;
	ondisk_sb.datablock_start = nmeta;
	ondisk_sb.log_start = ondisk_sb.datablock_start + ndatablocks;

	assert(sizeof(ondisk_sb) <= g_block_size_bytes);

	printf("Creating file system\n");
	printf("size of superblock %ld\n", sizeof(ondisk_sb));
	printf("----------------------------------------------------------------\n");
	printf("nmeta %d (boot 1, super 1, inode blocks %u, bitmap blocks %u) \n"
			"[ inode start %lu, bmap start %lu, datablock start %lu, log start %lu ] \n"
			": data blocks %lu log blocks %lu -- total %lu (%lu MB)\n",
			nmeta,
			ninodeblocks,
			nbitmap,
			ondisk_sb.inode_start,
			ondisk_sb.bmap_start,
			ondisk_sb.datablock_start,
			ondisk_sb.log_start,
			ndatablocks,
			nlog,
			file_size_blks,
			(file_size_blks * g_block_size_bytes) >> 20);
	printf("----------------------------------------------------------------\n");

	if (storage_mode == NVM)
		storage_dax.init(dev_id, g_dev_path[dev_id]);
	else if (storage_mode == SPDK) {
		storage_spdk.init(dev_id, NULL);
	} else if (storage_mode == HDD) {
		storage_hdd.init(dev_id, g_dev_path[dev_id]);
	} else {
		fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
		if(fsfd < 0){
			perror(argv[1]);
			exit(1);
		}
	}

	freeblock = nmeta;     // the first free block that we can allocate

	memset(zeroes, 0, g_block_size_bytes);
#if 1
	if (storage_mode == SPDK) {
		//for(i = 0; i < file_size_blks; i += (1 << 10)) {
		for (i = 0; i <  ondisk_sb.datablock_start; i += (1 << 10)) {
			//spdk_storage_ops.erase(dev_id, i, g_block_size_bytes * (1 << 10));
			storage_spdk.erase(dev_id, i, g_block_size_bytes * (1 << 10));
		}
	} else if (storage_mode == HDD) {
		for(i = 0; i < ondisk_sb.datablock_start; i++)
			wsect(i, (uint8_t *)zeroes);
	} else {
		for(i = 0; i < file_size_blks - 1; i++)
			wsect(i, (uint8_t *)zeroes);
	}
#else
	if (storage_mode == SPDK) {
		for( i = 0; i <  ondisk_sb.datablock_start; i += (1 << 10)) {
			storage_spdk.erase(dev_id, i, g_block_size_bytes * (1 << 10));
		}
	} else {
		for(i = 0; i < ondisk_sb.datablock_start; i++)
			wsect(i, (uint8_t *)zeroes);
	}
#endif

	memset(buf, 0, sizeof(buf));
	printf("== Write superblock\n");
	memmove(buf, &ondisk_sb, sizeof(ondisk_sb));
	wsect(1, buf);

	// Create / directory
	rootino = mkfs_ialloc(dev_id, T_DIR);
	printf("== create / directory\n");
	printf("root inode(inum = %u) at block address %lx\n",
			rootino, mkfs_get_inode_block(rootino, ondisk_sb.inode_start));
	assert(rootino == ROOTINO);

	bzero(&de, sizeof(de));
	de.inum = xshort(rootino);
	strcpy(de.name, ".");
	iappend(dev_id, rootino, &de, sizeof(de));

	bzero(&de, sizeof(de));
	de.inum = xshort(rootino);
	strcpy(de.name, "..");
	iappend(dev_id, rootino, &de, sizeof(de));

	// Create /mlfs directory
	mlfs_dir_ino = mkfs_ialloc(dev_id, T_DIR);
	printf("== create /mlfs directory\n");
	printf("/mlfs inode(inum = %u) at block address %lx\n",
			rootino, mkfs_get_inode_block(rootino, ondisk_sb.inode_start));

	bzero(&de, sizeof(de));
	de.inum = xshort(mlfs_dir_ino);
	strcpy(de.name, ".");
	iappend(dev_id, mlfs_dir_ino, &de, sizeof(de));

	bzero(&de, sizeof(de));
	de.inum = xshort(rootino);
	strcpy(de.name, "..");
	iappend(dev_id, mlfs_dir_ino, &de, sizeof(de));

	// append /mlfs to / directory.
	bzero(&de, sizeof(de));
	de.inum = xshort(mlfs_dir_ino);
	strcpy(de.name, "mlfs");
	iappend(dev_id, rootino, &de, sizeof(de));

	// clean /mlfs directory
	read_inode(dev_id, mlfs_dir_ino, &din);
	wsect(din.l1_addrs[0], (uint8_t *)zeroes);

#if 0
	for(i = 3; i < argc; i++){
		assert(index(argv[i], '/') == 0);

		if((fd = open(argv[i], 0)) < 0){
			perror(argv[i]);
			exit(1);
		}

		// Skip leading _ in name when writing to file system.
		// The binaries are named _rm, _cat, etc. to keep the
		// build operating system from trying to execute them
		// in place of system binaries like rm and cat.
		if(argv[i][0] == '_')
			++argv[i];

		inum = mkfs_ialloc(T_FILE);

		bzero(&de, sizeof(de));
		de.inum = xshort(inum);
		strncpy(de.name, argv[i], DIRSIZ);
		iappend(rootino, &de, sizeof(de));

		while((cc = read(fd, buf, sizeof(buf))) > 0)
			iappend(inum, buf, cc);

		close(fd);
	}
#endif

	// fix size of root inode dir
	read_inode(dev_id, rootino, &din);
	din.size = xint(din.size);
	write_inode(rootino, &din);

	// update block allocation bitmap;
	balloc(freeblock);

	if (storage_mode == NVM)
		storage_dax.commit(dev_id);
	else if (storage_mode == SPDK) {
		storage_spdk.wait_io(dev_id, 0);
		storage_spdk.wait_io(dev_id, 1);
	}
	else if (storage_mode == HDD)
		storage_hdd.commit(dev_id);

	exit(0);
}

void wsect(addr_t sec, uint8_t *buf)
{
	if (storage_mode == NVM) {
		storage_dax.write(dev_id, buf, sec, g_block_size_bytes);
	} else if (storage_mode == SPDK) {
		storage_spdk.write(dev_id, buf, sec, g_block_size_bytes);
		storage_spdk.wait_io(dev_id, 0);
	} else if (storage_mode == HDD) {
		storage_hdd.write(dev_id, buf, sec, g_block_size_bytes);
	} else if (storage_mode == FS) {
		if(lseek(fsfd, sec * g_block_size_bytes, 0) != sec * g_block_size_bytes) {
			perror("lseek");
			exit(1);
		}

		if(write(fsfd, buf, g_block_size_bytes) != g_block_size_bytes) {
			perror("write");
			exit(1);
		}
	}
}

void write_inode(uint32_t inum, struct dinode *ip)
{
	uint8_t buf[g_block_size_bytes];
	addr_t inode_block;
	struct dinode *dip;

	inode_block = mkfs_get_inode_block(inum, ondisk_sb.inode_start);
	rsect(inode_block, buf);

	dip = ((struct dinode*)buf) + (inum % IPB);
	*dip = *ip;

	dbg_printf("%s: inode %u (addr %u) type %u\n",
			__func__, inum, inode_block, ip->itype);

	wsect(inode_block, buf);
}

void read_inode(uint8_t dev_id, uint32_t inum, struct dinode *ip)
{
	uint8_t buf[g_block_size_bytes];
	addr_t bn;
	struct dinode *dip;

	//bn = IBLOCK(inum, ondisk_sb);
	bn = mkfs_get_inode_block(inum, ondisk_sb.inode_start);
	rsect(bn, buf);

	dip = ((struct dinode*)buf) + (inum % IPB);
	*ip = *dip;
}

void rsect(addr_t sec, uint8_t *buf)
{
	if (storage_mode == NVM)
		storage_dax.read(dev_id, buf, sec, g_block_size_bytes);
	else if (storage_mode == SPDK) {
		storage_spdk.read(dev_id, buf, sec, g_block_size_bytes);
		storage_spdk.wait_io(dev_id, 1);
	} else if (storage_mode == HDD) {
		storage_hdd.read(dev_id, buf, sec, g_block_size_bytes);
	} else if (storage_mode == FS) {
		if(lseek(fsfd, sec * g_block_size_bytes, 0) != sec * g_block_size_bytes){
			perror("lseek");
			exit(1);
		}

		if(read(fsfd, buf, g_block_size_bytes) != g_block_size_bytes) {
			perror("read");
			exit(1);
		}
	}
}

uint32_t mkfs_ialloc(uint8_t dev, uint16_t type)
{
	uint32_t inum = freeinode++;
	struct dinode din;

	bzero(&din, sizeof(din));
	din.dev = dev;
	din.itype = xshort(type);
	din.nlink = xshort(1);
	din.size = xint(0);

	memset(din.l1_addrs, 0, sizeof(addr_t) * (NDIRECT + 1));
	write_inode(inum, &din);

	return inum;
}

void balloc(int used)
{
	uint8_t buf[g_block_size_bytes];
	int i;

	dbg_printf("balloc: first %d blocks have been allocated\n", used);
	assert(used < g_block_size_bytes*8);
	bzero(buf, g_block_size_bytes);
	for(i = 0; i < used; i++){
		buf[i/8] = buf[i/8] | (0x1 << (i%8));
	}
	dbg_printf("balloc: write bitmap block at sector %lx\n", ondisk_sb.bmap_start);
	wsect(ondisk_sb.bmap_start, buf);
}

#define _min(a, b) ((a) < (b) ? (a) : (b))

// append new data (xp) to an inode (inum)
// 1. allocate new block
// 2. update inode pointers for data block (dinode.addrs) - in-memory
// 3. write data blocks - on-disk
// 4. update inode blocks - on-disk
void iappend(uint8_t dev, uint32_t inum, void *xp, int n)
{
    char *p = (char*)xp;
    addr_t fbn, off, n1;
    struct dinode din;
    uint8_t buf[g_block_size_bytes];
    addr_t indirect[NINDIRECT];
    addr_t block_address;

    read_inode(dev, inum, &din);
    off = xint(din.size);

    /* TODO: fix this with extent */
    dbg_printf("append inum %d at off %ld sz %d\n", inum, off, n);
    while(n > 0) {
        // compute the number of block
        fbn = off / g_block_size_bytes;
        assert(fbn < MAXFILE);
        if(fbn < NDIRECT) {
            if(xint(din.l1_addrs[fbn]) == 0) {
                // sequential allocation for freeblock
                din.l1_addrs[fbn] = xint(freeblock++);
            }
            block_address = xint(din.l1_addrs[fbn]);
        }
        else {
            printf("Size over NDIRECT does not support yet\n");
        }

        n1 = _min(n, (fbn + 1) * g_block_size_bytes - off);
        rsect(block_address, buf);
        //update block (buf) with data pointer (p)
        bcopy(p, buf + off - (fbn * g_block_size_bytes), n1);
        wsect(block_address, buf);
        //printf("%s: %u %d\n", __func__, block_address, inum);

        n -= n1;
        off += n1;
        p += n1;
    }

    din.size = xint(off);
    write_inode(inum, &din);
}

#ifdef __cplusplus
}
#endif
