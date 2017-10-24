#include "mlfs/mlfs_user.h"
#include "global/global.h"
#include "global/util.h"
#include "concurrency/synchronization.h"
#include "fs.h"
#include "balloc.h"
#include "extents.h"
#include "io/block_io.h"

int namecmp(const char *s, const char *t)
{
	return strncmp(s, t, DIRSIZ);
}

uint8_t *get_dirent_block(struct inode *dir_inode, offset_t offset)
{
	int ret;
	struct buffer_head *io_buf;
	struct dirent_block *d_block;
	addr_t blk_no;

	mlfs_assert(dir_inode->itype == T_DIR);

	d_block = dcache_find(dir_inode->dev, dir_inode->inum, offset);

	if (d_block) 
		return d_block->dirent_array;

	// Allocate new directory array block.
	if (dir_inode->l1.addrs[offset >> g_block_size_shift] == 0) {
		handle_t handle;
		mlfs_lblk_t count = 1;

		handle.dev = dir_inode->dev;
		mlfs_ext_alloc_blocks(&handle, dir_inode, 0, 
				MLFS_GET_BLOCKS_CREATE_META, &blk_no, &count);

		dir_inode->l1.addrs[offset >> g_block_size_shift] = blk_no;
		
		// Must zero-out the new directory block.
		d_block = dcache_alloc_add(dir_inode->dev,
				dir_inode->inum, offset, NULL);

		mlfs_debug("Allocate a new directory block %lx\n", blk_no);
	} 
	// Read from storage.
	else {
		blk_no = dir_inode->l1.addrs[offset >> g_block_size_shift];
		uint8_t *data = mlfs_alloc(g_block_size_bytes);

		mlfs_assert(blk_no != 0);

		io_buf = bh_get_sync_IO(dir_inode->dev, blk_no, BH_NO_DATA_ALLOC);
		io_buf->b_size = g_block_size_bytes;
		io_buf->b_data = data;

		bh_submit_read_sync_IO(io_buf);

		mlfs_io_wait(dir_inode->dev, 1);

		d_block = dcache_alloc_add(dir_inode->dev,
				dir_inode->inum, offset, data);
	}

	return d_block->dirent_array;
}

struct mlfs_dirent *get_dirent(struct inode *dir_inode, offset_t offset)
{
	uint8_t *dirent_array;
	struct mlfs_dirent *dir_entry;

	dirent_array = get_dirent_block(dir_inode, offset);

	dir_entry = (struct mlfs_dirent *)(dirent_array + (offset % g_block_size_bytes));

	return dir_entry;
}

static inline int dblk_cmp(struct rb_node *a, struct rb_node *b)
{
	struct dirent_block *a_inode, *b_inode;
	a_inode = container_of(a, struct dirent_block, dblk_rb_node);
	b_inode = container_of(b, struct dirent_block, dblk_rb_node);

	if (a_inode->key.offset < b_inode->key.offset)
		return -1;
	else if (a_inode->key.offset > b_inode->key.offset)
		return 1;

	return 0;
}

static inline int mark_dirent_block_dirty(struct inode *inode, 
		struct dirent_block *dblk) 
{
	return rb_insert(&inode->i_dirty_dblock, &dblk->dblk_rb_node,
			dblk_cmp);
}

int persist_dirty_dirent_block(struct inode *inode)
{
	struct rb_node *node;

	for (node = rb_first(&inode->i_dirty_dblock); 
			node; node = rb_next(node)) {
		struct dirent_block *dblk = 
			rb_entry(node, struct dirent_block, dblk_rb_node);
		struct buffer_head *bh;
		bh = bh_get_sync_IO(inode->dev, inode->l1.addrs[dblk->key.offset], 
				BH_NO_DATA_ALLOC);

		bh->b_data = dblk->dirent_array;
		bh->b_size = g_block_size_bytes;
		bh->b_offset = 0;

		mlfs_write(bh);

		mlfs_io_wait(bh->b_dev, 0);

		bh_release(bh);
	}

	return 0;
}

// Search for an inode by name from a directory entry.
// dir_inode is an inode for a directory entry.
// If found, set *poff to byte offset of entry.
struct inode* dir_lookup(struct inode *dir_inode, char *name, offset_t *poff)
{
	offset_t off;
	uint32_t inum, n;
	struct mlfs_dirent *de;
	struct inode *ip;

	if (dir_inode->itype != T_DIR)
		panic("lookup for non DIR");

	ip = de_cache_find(dir_inode, name, &off);
	if (ip)
		return ip;

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (de->inum == 0) {
			de++;
			continue;
		}

		if (namecmp(name, de->name) == 0) {
			// entry matches path element
			if (poff)
				*poff = off;
			inum = de->inum;
			ip = iget(dir_inode->dev, inum);

			if (!(ip->flags & I_VALID)) {
				struct dinode dip;

				read_ondisk_inode(dir_inode->dev, inum, &dip);

				// on-disk inode must be allocated beforehand.
				mlfs_assert(dip.itype != 0);
				
				ip->_dinode = (struct dinode *)ip;
				sync_inode_from_dinode(ip, &dip);
				ip->flags |= I_VALID;
			}

			iput(ip);

			de_cache_alloc_add(dir_inode, name, ip, off); 

			return ip;
		}

		de++;
	}

	if (poff)
		*poff = 0;

	return NULL;
}

int dir_remove_entry(struct inode *dir_inode, char *name, uint32_t inum)
{
	offset_t off;
	struct mlfs_dirent *de;
	uint8_t *dirent_array;
	struct inode *ip;
	uint32_t n;

	if (dir_inode->size > g_block_size_bytes) {
		de_cache_find(dir_inode, name, &off); 

		if (off != 0) {
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off); 
			de += ((off % g_block_size_bytes) / sizeof(*de));

			if (namecmp(de->name, name) == 0) {
				mlfs_assert(inum == de->inum);
				goto dirent_found;
			}
		}
	}

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);;
	mlfs_assert(de);

	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (namecmp(de->name, name) == 0) {
			mlfs_assert(inum == de->inum);
			break;
		}

		de++;
	}

dirent_found:
	memset(de, 0, sizeof(*de));

	if (off == dir_inode->size)
		dir_inode->size -= sizeof(*de);

	bitmap_clear(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

	de_cache_del(dir_inode, name);

	mark_dirent_block_dirty(dir_inode, 
			dcache_find(dir_inode->dev, dir_inode->inum, off));

	return 0;
}

/* rename to an existing file implicitly unlink the old one */
int __attribute__ ((deprecated))
dir_change_entry(struct inode *dir_inode, char *oldname, 
		char *newname, uint32_t new_inum)
{
	struct inode *ip;
	uint8_t *dirent_array;
	struct mlfs_dirent *de;
	offset_t off;
	int ret = -ENOENT;
	uint32_t n;
	uint64_t tsc_begin, tsc_end;
	char token[DIRSIZ+8] = {0};

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	mlfs_assert(de);

	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (namecmp(de->name, oldname) == 0) {
			de_cache_del(dir_inode, oldname);
			strncpy(de->name, newname, DIRSIZ);

			if (de->inum != new_inum)
				ret = de->inum;
			else
				ret = 0;

			de->inum = new_inum;

			break;
		}

		de++;
	}

	return ret;
}

// Write a new directory entry (name, inum) into the directory inode.
int dir_add_entry(struct inode *dir_inode, char *name, uint32_t inum)
{
	offset_t off;
	struct mlfs_dirent *de;
	struct inode *ip;
	uint32_t n, next_avail_slot;

#if 0
	// Only for debugging.
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if(de->inum == inum || namecmp(name, de->name) == 0) {
			printf("name %s inum %u <-> de name %s inum %u\n", 
					name, inum, de->name, de->inum);
			panic("a There is duplicated entry\n");
		}
	}
#endif

	next_avail_slot = find_next_zero_bit(dir_inode->dirent_bitmap,
			DIRBITMAP_SIZE, 0);

	off = next_avail_slot * sizeof(*de);
	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
	de += ((off % g_block_size_bytes) / sizeof(*de));

	if (de->inum == 0) 
		goto empty_found;

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	mlfs_assert(de);

search_slot:
	// Look for an empty dirent.
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if(de->inum == 0)
			break;

		de++;
		bitmap_set(dir_inode->dirent_bitmap, off / sizeof(*de), 1);
	}

empty_found:
	// directory inode size is max offset.
	if (off + sizeof(struct mlfs_dirent) > dir_inode->size)
		dir_inode->size = off + sizeof(struct mlfs_dirent);;

	bitmap_set(dir_inode->dirent_bitmap, off / sizeof(*de), 1);

	strncpy(de->name, name, DIRSIZ);
	de->inum = inum;

	de_cache_alloc_add(dir_inode, name, 
			icache_find(dir_inode->dev, inum), off);

	mark_dirent_block_dirty(dir_inode, 
			dcache_find(dir_inode->dev, dir_inode->inum, off));

	mlfs_debug("name %s inum %d off %u\n", de->name, de->inum, off);

	return 0;
}

// Paths
// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode* namex(char *path, int nameiparent, char *name)
{
	struct inode *ip, *next;

	if (*path == '/') 
		ip = iget(g_root_dev, ROOTINO);
	else
		panic("relative path is not yet implemented\n");

	// directory walking of a given path
	while ((path = get_next_name(path, name)) != 0) {
		ilock(ip);
		if (ip->itype != T_DIR){
			iunlockput(ip);
			return 0;
		}
		if (nameiparent && *path == '\0') {
			// Stop one level early.
			iunlock(ip);
			return ip;
		}
		if ((next = dir_lookup(ip, name, 0)) == NULL) {
			iunlockput(ip);
			return 0;
		}

		iunlockput(ip);
		ip = next;
	}

	if (nameiparent) {
		iput(ip);
		return 0;
	}

	return ip;
}

struct inode* namei(char *path)
{
	char name[DIRSIZ];
	return namex(path, 0, name);
}

struct inode* nameiparent(char *path, char *name)
{
	return namex(path, 1, name);
}

/////////////////////////////////////////////////////////////////
// functions for debugging. Usually called by gdb.
// Note that I assume following functions are call when
// program is frozen by gdb so functions are not thread-safe.
//

// place holder for python gdb module to records.
void dbg_save_inode(struct inode *inode, char *name)
{
	return ;
}

// place holder for python gdb module to records.
void dbg_save_dentry(struct mlfs_dirent *de, struct inode *dir_inode)
{
	return ;
}

void dbg_dump_dir(uint8_t dev, uint32_t inum)
{
	offset_t off;
	struct mlfs_dirent *de;
	struct inode *dir_inode;
	uint32_t n;

	dir_inode = iget(dev, inum);

	if (!(dir_inode->flags & I_VALID)) {
		struct dinode dip;
		int ret;

		read_ondisk_inode(dir_inode->dev, inum, &dip);

		if(dip.itype == 0) {
			mlfs_info("inum %d does not exist\n", inum);
			return;
		}

		dir_inode->_dinode = (struct dinode *)dir_inode;
		sync_inode_from_dinode(dir_inode, &dip);
		dir_inode->flags |= I_VALID;
	}

	if (dir_inode->itype != T_DIR) {
		mlfs_info("%s\n", "lookup for non DIR");
		return;
	}

	de = (struct mlfs_dirent *)get_dirent_block(dir_inode, 0);
	for (off = 0, n = 0; off < dir_inode->size; off += sizeof(*de)) {
		if (n != (off >> g_block_size_shift)) {
			n = off >> g_block_size_shift;
			// read another directory block.
			de = (struct mlfs_dirent *)get_dirent_block(dir_inode, off);
		}

		if (de->inum == 0) {
			de++;
			continue;
		}

		mlfs_info("name %s\tinum %d offset %lu\n", de->name, de->inum, off);
		de++;
	}

	mlfs_info("%s\n", "--------------------------------");

	return;
}

// Walking through to path and snapshoting dentry and inode.
void dbg_path_walk(char *path)
{
	struct inode *inode, *next_inode;
	char name[DIRSIZ];

	if (*path != '/')
		return;

	inode = iget(g_root_dev, ROOTINO);

	dbg_save_inode(inode, "/");

	while ((path = get_next_name(path, name)) != 0) {
		next_inode = dir_lookup(inode, name, 0);

		dbg_save_inode(inode, name);

		if (next_inode == NULL)
			break;

		inode = next_inode;
	}

	return ;
}

void dbg_dump_inode(uint8_t dev, uint32_t inum)
{
	struct inode *inode;

	inode = iget(dev, inum);

	if (!(inode->flags & I_VALID)) {
		struct dinode dip;
		int ret;

		read_ondisk_inode(inode->dev, inum, &dip);

		if(dip.itype == 0) {
			mlfs_info("inum %d does not exist\n", inum);
			iput(inode);
		return;
		}

		inode->_dinode = (struct dinode *)inode;
		sync_inode_from_dinode(inode, &dip);
		inode->flags |= I_VALID;
	}

	mlfs_info("inum %d type %d size %lu flags %u\n", 
			inode->inum, inode->itype, inode->size, inode->flags);

	return;
}

void dbg_check_inode(void *data)
{
	uint32_t i;
	struct dinode *dinode;

	for (i = 0; i < IPB ; i++) {
		dinode = (struct dinode *)data + i;

		if (dinode->itype > 3)
			GDB_TRAP;
	}

	return;
}

void dbg_check_dir(void *data)
{
	uint32_t i;
	struct mlfs_dirent *de;

	for (i = 0; i < (g_block_size_bytes / sizeof(struct mlfs_dirent)) ; i++) {
		de = (struct mlfs_dirent *)data + i;

		if (de->inum == 0)
			continue;

		mlfs_info("%u (offset %lu): inum %u name %s\n", i, i * sizeof(*de), 
				de->inum, de->name);
	}

	mlfs_info("%s", "-------------------------\n");
	
	return;
}
