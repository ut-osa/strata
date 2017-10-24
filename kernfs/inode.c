// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /var/log/kern.log for hierarchical namespace.

#include "mlfs/mlfs_user.h"
#include "global/global.h"
#include "concurrency/synchronization.h"
#include "io/block_io.h"
#include "fs.h"
#include "kernfs_interface.h"
#include "extents_bh.h"

void read_root_inode(uint8_t dev_id)
{
	struct dinode _dinode;
	struct inode *_inode;

	read_ondisk_inode(dev_id, ROOTINO, &_dinode);
	_dinode.dev = dev_id;

	mlfs_assert(_dinode.itype == T_DIR);

	_inode = icache_alloc_add(dev_id, ROOTINO);

	_inode->_dinode = (struct dinode *)_inode;
	sync_inode_from_dinode(_inode, &_dinode);
	_inode->flags |= I_VALID;
}

int read_ondisk_inode(uint8_t dev, uint32_t inum, struct dinode *dip)
{
	int ret;
	struct buffer_head *bh;
	addr_t inode_block;

	inode_block = get_inode_block(dev, inum);
	bh = bh_get_sync_IO(dev, inode_block, BH_NO_DATA_ALLOC);

	if (dev == g_root_dev) {

		bh->b_size = sizeof(struct dinode);
		bh->b_data = (uint8_t *)dip;
		bh->b_offset = sizeof(struct dinode) * (inum % IPB);
		bh_submit_read_sync_IO(bh);
		mlfs_io_wait(dev, 1);
	} else {
		bh->b_size = g_block_size_bytes;
		bh->b_data = mlfs_zalloc(g_block_size_bytes);

		bh_submit_read_sync_IO(bh);
		mlfs_io_wait(dev, 1);

		memmove(dip, (struct dinode*)bh->b_data + (inum % IPB), sizeof(struct dinode));

		mlfs_free(bh->b_data);
	}

	return 0;
}

int write_ondisk_inode(uint8_t dev, struct inode *ip)
{
	int ret;
	struct dinode *dip = ip->_dinode;
	struct buffer_head *bh;
	addr_t inode_block;
	uint8_t _dev = dev;

	inode_block = get_inode_block(dev, ip->inum);
	bh = bh_get_sync_IO(dev, inode_block, BH_NO_DATA_ALLOC);

	if (dev == g_root_dev) {

		bh->b_size = sizeof(struct dinode);
		bh->b_data = (uint8_t *)dip;
		bh->b_offset = sizeof(struct dinode) * (ip->inum % IPB);
		ret = mlfs_write(bh);
		mlfs_io_wait(dev, 0);
	} else {
		bh->b_size = g_block_size_bytes;
		bh->b_data = mlfs_zalloc(g_block_size_bytes);

		// read-modify-write of inode block 
		bh_submit_read_sync_IO(bh);
		mlfs_io_wait(dev, 1);

		memmove((struct dinode*)bh->b_data + (ip->inum % IPB), 
				dip, sizeof(struct dinode));

		ret = mlfs_write(bh);
		mlfs_io_wait(dev, 0);

		mlfs_free(bh->b_data);
	}

	return ret;
}

void iupdate(struct inode *ip)
{
}

// Allocate a new inode with the given type on device dev.
// A free inode has a type of zero.
struct inode* ialloc(uint8_t dev, uint8_t type, uint32_t inode_nr)
{
    uint32_t inum;
	int ret;
    struct buffer_head *bp;
    struct dinode dip;
    struct inode *ip;

	// allocate with empty inode number
	if (inode_nr == 0) {
		for (inum = 1; inum < disk_sb[dev].ninodes; inum++) {
			read_ondisk_inode(dev, inum, &dip);

			if (dip.itype == 0) {  // a free inode
				memset(&dip, 0, sizeof(struct dinode));
				dip.itype = type;
				dip.dev = dev;

				/* add in-memory inode to icache */
				ip = iget(dev, inum);
				ip->_dinode = (struct dinode *)ip;
				sync_inode_from_dinode(ip, &dip);
				ip->flags |= I_VALID;

				ip->i_sb = sb;
				ip->i_generation = 0;
				ip->i_data_dirty = 0;
				ip->nlink = 1;

				return ip;
			}

		}
	} else {
		ip = icache_find(dev, inode_nr);

		if (!ip)
			ip = iget(dev, inode_nr);
		else 
			ip->i_ref++;

		ip->_dinode = (struct dinode *)ip;

		if (!(ip->flags & I_VALID)) {
			read_ondisk_inode(dev, inode_nr, &dip);

			if (dip.itype == 0) {
				memset(&dip, 0, sizeof(struct dinode));
				dip.itype = type;
				dip.dev = dev;
			}

			sync_inode_from_dinode(ip, &dip);
			ip->flags |= I_VALID;
		}

		ip->i_sb = sb;
		ip->i_generation = 0;
		ip->i_data_dirty = 0;
		ip->nlink = 1;

		return ip;
	}

    panic("ialloc: no inodes");

    return NULL;
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
struct inode* iget(uint8_t dev, uint32_t inum)
{
	struct inode *ip;

	ip = icache_find(dev, inum);

	if (ip) {
		ip->i_ref++;
		return ip;
	}

	ip = icache_alloc_add(dev, inum);

	return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode* idup(struct inode *ip)
{
	ip->i_ref++;
	return ip;
}

// Lock the given inode; set iflag of I_BUSY and I_VALID
// Reads the inode from disk if necessary.
void ilock(struct inode *ip)
{
}

// Unlock the given inode.
void iunlock(struct inode *ip)
{
}

/* iput does not deallocate inode. it just drops reference count. 
 * An inode is explicitly deallocated by ideallc() 
 */
void iput(struct inode *ip)
{
	ip->i_ref--;
}

int idealloc(struct inode *inode)
{
	struct dinode dip;

	mlfs_assert(inode->i_ref < 2);
	mlfs_assert(inode->flags & I_DELETING);

	if (inode->i_ref == 1 && 
			(inode->flags & I_VALID) && 
			inode->nlink == 0) {
		if (inode->flags & I_BUSY)
			panic("Inode must not be busy!");

		inode->flags &= ~I_BUSY;
	}

	read_ondisk_inode(inode->dev, inode->inum, &dip);

	mlfs_assert(inode->itype == dip.itype);

	ilock(inode);
	inode->size = 0;
	/* After persisting the inode, libfs moves it to
	 * deleted inode hash table in persist_log_inode() */
	inode->itype = 0;

	iunlock(inode);

	mlfs_debug("delete %u\n", inode->inum);

	return 0;
}

// Common idiom: unlock, then put.
void iunlockput(struct inode *ip)
{
	iunlock(ip);
	iput(ip);
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
void itrunc(struct inode *ip)
{
	panic("truncate is not supported\n");
}
