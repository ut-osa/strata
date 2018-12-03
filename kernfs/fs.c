#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "mlfs/mlfs_user.h"
#include "global/global.h"
#include "global/util.h"
#include "global/defs.h"
#include "kernfs_interface.h"
#include "fs.h"
#include "io/block_io.h"
#include "storage/storage.h"
#include "extents.h"
#include "extents_bh.h"
#include "balloc.h"
#include "slru.h"
#include "migrate.h"
#include "thpool.h"
#include "lease_server.h"
#include "lease_manager.h"

#define _min(a, b) ({\
        __typeof__(a) _a = a;\
        __typeof__(b) _b = b;\
        _a < _b ? _a : _b; })

#define NOCREATE 0
#define CREATE 1

extern mlfs_mutex_t lease_lock;
extern mlfs_lease_t* mlfs_lease_global;

int shm_fd = 0;
uint8_t *shm_base;

/*void mlfs_get_time(mlfs_time_t *a) {}*/

pthread_spinlock_t icache_spinlock;
pthread_spinlock_t dcache_spinlock;

struct inode *inode_hash[g_n_devices + 1];
struct dirent_block *dirent_hash[g_n_devices + 1];

struct disk_superblock disk_sb[g_n_devices];
struct super_block *sb[g_n_devices];

ncx_slab_pool_t *mlfs_slab_pool;
ncx_slab_pool_t *mlfs_slab_pool_shared;

uint8_t g_log_dev = 0;
uint8_t g_ssd_dev = 0;
uint8_t g_hdd_dev = 0;

kernfs_stats_t g_perf_stats;
uint8_t enable_perf_stats;

uint16_t *inode_version_table;
threadpool thread_pool;
threadpool thread_pool_ssd;
#ifdef FCONCURRENT
threadpool file_digest_thread_pool;
#endif

int digest_unlink(uint8_t from_dev, uint8_t to_dev, uint32_t inum);

#define NTYPE_I 1
#define NTYPE_D 2
#define NTYPE_F 3
#define NTYPE_U 4

typedef struct replay_node_key
{
    uint32_t inum;
    uint16_t ver;
} replay_key_t;

/* It is important to place node_type right after struct list_head
 * since digest_log_from_replay_list compute node_type like this:
 * node_type = &list + sizeof(struct list_head)
 */
typedef struct inode_replay {
    replay_key_t key;
    addr_t blknr;
    mlfs_hash_t hh;
    struct list_head list;
    uint8_t node_type;
    uint8_t create;
} i_replay_t;

typedef struct d_replay_key {
    uint32_t inum;
    uint16_t ver;
    uint8_t type;
} d_replay_key_t;

typedef struct directory_replay {
    d_replay_key_t key;
    int n;
    uint32_t dir_inum;
    uint32_t dir_size;
    addr_t blknr;
    mlfs_hash_t hh;
    struct list_head list;
    uint8_t node_type;
} d_replay_t;

typedef struct block_list {
    struct list_head list;
    uint32_t n;
    addr_t blknr;
} f_blklist_t;

typedef struct file_io_vector {
    offset_t hash_key;
    struct list_head list;
    offset_t offset;
    uint32_t length;
    addr_t blknr;
    uint32_t n_list;
    struct list_head iov_blk_list;
    mlfs_hash_t hh;
} f_iovec_t;

typedef struct file_replay {
    replay_key_t key;
    struct list_head iovec_list;
    f_iovec_t *iovec_hash;
    mlfs_hash_t hh;
    struct list_head list;
    uint8_t node_type;
} f_replay_t;

typedef struct unlink_replay {
    replay_key_t key;
    mlfs_hash_t hh;
    struct list_head list;
    uint8_t node_type;
} u_replay_t;

struct replay_list {
    i_replay_t *i_digest_hash;
    d_replay_t *d_digest_hash;
    f_replay_t *f_digest_hash;
    u_replay_t *u_digest_hash;
    struct list_head head;
};

struct digest_arg {
    int sock_fd;
    struct sockaddr_un cli_addr;
    char msg[MAX_SOCK_BUF];
};

struct f_digest_worker_arg {
    uint8_t from_dev;
    uint8_t to_dev;
    f_replay_t *f_item;
};


void show_kernfs_stats(void)
{
    float clock_speed_mhz = get_cpu_clock_speed();
    uint64_t n_digest = g_perf_stats.n_digest == 0 ? 1.0 : g_perf_stats.n_digest;

    printf("\n");
    //printf("CPU clock : %.3f MHz\n", clock_speed_mhz);
    printf("----------------------- kernfs statistics\n");
    printf("digest       : %.3f ms\n",
            g_perf_stats.digest_time_tsc / (clock_speed_mhz * 1000.0));
    printf("- replay     : %.3f ms\n",
            g_perf_stats.replay_time_tsc / (clock_speed_mhz * 1000.0));
    printf("- apply      : %.3f ms\n",
            g_perf_stats.apply_time_tsc / (clock_speed_mhz * 1000.0));
    printf("-- inode digest : %.3f ms\n",
            g_perf_stats.digest_inode_tsc / (clock_speed_mhz * 1000.0));
    printf("-- dir digest   : %.3f ms\n",
            g_perf_stats.digest_dir_tsc / (clock_speed_mhz * 1000.0));
    printf("-- file digest  : %.3f ms\n",
            g_perf_stats.digest_file_tsc / (clock_speed_mhz * 1000.0));
    printf("n_digest        : %lu\n", g_perf_stats.n_digest);
    printf("n_digest_skipped: %lu (%.1f %%)\n",
            g_perf_stats.n_digest_skipped,
            ((float)g_perf_stats.n_digest_skipped * 100.0) / (float)n_digest);
    printf("path search    : %.3f ms\n",
            g_perf_stats.path_search_tsc / (clock_speed_mhz * 1000.0));
    printf("total migrated : %lu MB\n", g_perf_stats.total_migrated_mb);
    printf("--------------------------------------\n");
}

void show_storage_stats(void)
{
    printf("------------------------------ storage stats\n");
    printf("NVM - %.2f %% used (%.2f / %.2f GB) - # of used blocks %lu\n",
            (100.0 * (float)sb[g_root_dev]->used_blocks) /
            (float)disk_sb[g_root_dev].ndatablocks,
            (float)(sb[g_root_dev]->used_blocks << g_block_size_shift) / (1 << 30),
            (float)(disk_sb[g_root_dev].ndatablocks << g_block_size_shift) / (1 << 30),
            sb[g_root_dev]->used_blocks);

#ifdef USE_SSD
    printf("SSD - %.2f %% used (%.2f / %.2f GB) - # of used blocks %lu\n",
            (100.0 * (float)sb[g_ssd_dev]->used_blocks) /
            (float)disk_sb[g_ssd_dev].ndatablocks,
            (float)(sb[g_ssd_dev]->used_blocks << g_block_size_shift) / (1 << 30),
            (float)(disk_sb[g_ssd_dev].ndatablocks << g_block_size_shift) / (1 << 30),
            sb[g_ssd_dev]->used_blocks);
#endif

#ifdef USE_HDD
    printf("HDD - %.2f %% used (%.2f / %.2f GB) - # of used blocks %lu\n",
            (100.0 * (float)sb[g_hdd_dev]->used_blocks) /
            (float)disk_sb[g_hdd_dev].ndatablocks,
            (float)(sb[g_hdd_dev]->used_blocks << g_block_size_shift) / (1 << 30),
            (float)(disk_sb[g_hdd_dev].ndatablocks << g_block_size_shift) / (1 << 30),
            sb[g_hdd_dev]->used_blocks);
#endif

    mlfs_info("--- lru_list count %lu, %lu, %lu\n",
            g_lru[g_root_dev].n, g_lru[g_ssd_dev].n, g_lru[g_hdd_dev].n);
}

loghdr_meta_t *read_log_header(uint8_t from_dev, addr_t hdr_addr)
{
    int ret, i;
    loghdr_t *_loghdr;
    loghdr_meta_t *loghdr_meta;

    loghdr_meta = (loghdr_meta_t *)mlfs_zalloc(sizeof(loghdr_meta_t));
    if (!loghdr_meta)
        panic("cannot allocate logheader\n");

    INIT_LIST_HEAD(&loghdr_meta->link);

    /* optimization: instead of reading log header block to kernel's memory,
     * buffer head points to memory address for log header block.
     */
    _loghdr = (loghdr_t *)(g_bdev[from_dev]->map_base_addr +
            (hdr_addr << g_block_size_shift));

    loghdr_meta->loghdr = _loghdr;
    loghdr_meta->hdr_blkno = hdr_addr;
    loghdr_meta->is_hdr_allocated = 1;

    mlfs_debug("%s", "--------------------------------\n");
    mlfs_debug("%d\n", _loghdr->n);
    mlfs_debug("next loghdr %lx\n", _loghdr->next_loghdr_blkno);
    mlfs_debug("inuse %x\n", _loghdr->inuse);

    /*
       for (i = 0; i < _loghdr->n; i++) {
       mlfs_debug("types %d blocks %lx\n",
       _loghdr->type[i], _loghdr->blocks[i]);
       }
       */

    return loghdr_meta;
}

// FIXME: to_dev is now used. change APIs
int digest_inode(uint8_t from_dev, uint8_t to_dev,
        uint32_t inum, addr_t blknr)
{
    struct buffer_head *bh;
    struct dinode *src_dinode;
    struct inode *inode;
    int ret;

    bh = bh_get_sync_IO(from_dev, blknr, BH_NO_DATA_ALLOC);
    bh->b_size = sizeof(struct dinode);
    bh->b_data = mlfs_alloc(bh->b_size);

    bh_submit_read_sync_IO(bh);
    mlfs_io_wait(from_dev, 1);

    src_dinode = (struct dinode *)bh->b_data;

    mlfs_assert(src_dinode->dev == g_root_dev);

    mlfs_debug("[INODE] dev %u type %u inum %u size %lu\n",
            src_dinode->dev, src_dinode->itype, inum, src_dinode->size);

    // Inode exists only in NVM layer.
    to_dev = g_root_dev;

    inode = icache_find(to_dev, inum);

    if (!inode)
        inode = ialloc(to_dev, src_dinode->itype, inum);

    mlfs_assert(inode);

    if (inode->flags & I_DELETING) {
        // reuse deleting inode.
        // digest_unlink cleaned up old contents already.

        inode->flags &= ~I_DELETING;
        inode->flags |= I_VALID;
        inode->itype = src_dinode->itype;
        inode->i_sb = sb;
        inode->i_generation = 0;
        inode->i_data_dirty = 0;
    }

    if (inode->itype == T_FILE) {
        struct mlfs_extent_header *ihdr;
        handle_t handle = {.dev = to_dev};

        ihdr = ext_inode_hdr(&handle, inode);

        // The first creation of dinode of file
        if (ihdr->eh_magic != MLFS_EXT_MAGIC) {
            mlfs_ext_tree_init(&handle, inode);

            // For testing purpose, those data are hard-coded.
            inode->i_writeback = NULL;
            memset(inode->i_uuid, 0xCC, sizeof(inode->i_uuid));
            inode->i_csum = mlfs_crc32c(~0, inode->i_uuid, sizeof(inode->i_uuid));
            inode->i_csum =
                mlfs_crc32c(inode->i_csum, &inode->inum, sizeof(inode->inum));
            inode->i_csum = mlfs_crc32c(inode->i_csum, &inode->i_generation,
                    sizeof(inode->i_generation));
        }

        // ftruncate case (shrink length)
        if (src_dinode->size < inode->size) {
            handle_t handle;
            handle.dev = src_dinode->dev;

            ret = mlfs_ext_truncate(&handle, inode,
                    (src_dinode->size) >> g_block_size_shift,
                    ((inode->size) >> g_block_size_shift) - 1);

            mlfs_assert(!ret);
        }
    }

    inode->size = src_dinode->size;

    mlfs_debug("[INODE] (%d->%d) inode inum %u type %d, size %lu\n",
            from_dev, to_dev, inode->inum, inode->itype, inode->size);

    mlfs_mark_inode_dirty(inode);

    clear_buffer_uptodate(bh);

    mlfs_free(bh->b_data);
    bh_release(bh);

    return 0;
}

/* n : nth entry in the log header.
 * type : digest type.
 * dir_inum : the inode number of parent directory
 * _dirent_inum : the inode number of directory to be digested.
 * blknr: the number of block that contatins the directory entry.
 */
int digest_directory(uint8_t from_dev, uint8_t to_dev, int n, uint8_t type,
        uint32_t dir_inum, uint32_t dir_size, offset_t _dirent_inum, addr_t blknr)
{
    struct inode *dir_inode;
    struct dinode *dinode;
    struct dirent *de;
    char loghdr_ext[2048], *name;
    uint32_t dirent_inum;
    struct buffer_head *bh_dir, *bh;
    uint8_t *dirent_array;
    uint8_t found = 0;
    int ret;

    to_dev = g_root_dev;

    bh = bh_get_sync_IO(from_dev, blknr, BH_NO_DATA_ALLOC);
    bh->b_size = g_block_size_bytes;
    bh->b_data = mlfs_alloc(bh->b_size);

    bh_submit_read_sync_IO(bh);
    mlfs_io_wait(from_dev, 1);

    name = (char *)bh->b_data + sizeof(struct logheader);
    memmove(loghdr_ext, name, _min(strlen(name), 2048));

    /* Ugly tokenizing. To see the token format,
     * check dir_add_entry in libfs */
    name = strtok(loghdr_ext, "|");
    while (name != NULL) {
        if (name[0] == '0' + n) {
            name++;
            found = 1;
            break;
        }
        name = strtok(NULL, "|");
    }

    if (found == 0)
        return -EEXIST;

    name = strtok(name, "@");
    dirent_inum = strtoul(strtok(NULL, "@"), NULL, 10);
    mlfs_assert(dirent_inum == (uint32_t)_dirent_inum);

    mlfs_debug("[DIR] %s, name %s (parent inum %d) inum %d\n",
            type == L_TYPE_DIR_ADD ? "ADD" :
            type == L_TYPE_DIR_DEL ? "DEL" : "RENAME",
            name, dir_inum, dirent_inum);

    dir_inode = icache_find(to_dev, dir_inum);
    if (!dir_inode)
        dir_inode = ialloc(to_dev, T_DIR, dir_inum);

    // Update dirent array block. Possibly, a new directory block could be allocated
    // during directory walk (in get_dirent_block()).
    if (type == L_TYPE_DIR_ADD)
        dir_add_entry(dir_inode, name, dirent_inum);
    else if (type == L_TYPE_DIR_RENAME) {
        struct inode *old_inode;

        old_inode = dir_lookup(dir_inode, name, NULL);

        // rename to the same file name implies unlink the old file.
        if (old_inode) {
            dir_remove_entry(dir_inode, name, old_inode->inum);
            digest_unlink(from_dev, to_dev, old_inode->inum);
        }

        dir_add_entry(dir_inode, name, dirent_inum);
    } else if (type == L_TYPE_DIR_DEL)
        dir_remove_entry(dir_inode, name, dirent_inum);
    else
        panic("unsupported type\n");

    mlfs_debug("dir_inode size %lu\n", dir_inode->size);

    mlfs_debug("[DIR] (%d->%d) inode inum %u size %lu\n",
            from_dev, dir_inode->dev,
            dir_inode->inum, dir_inode->size);

    mlfs_mark_inode_dirty(dir_inode);

    clear_buffer_uptodate(bh);

    mlfs_free(bh->b_data);
    bh_release(bh);

    return 0;
}

int digest_file(uint8_t from_dev, uint8_t to_dev, uint32_t file_inum,
        offset_t offset, uint32_t length, addr_t blknr)
{
    int ret;
    uint32_t offset_in_block = 0;
    struct inode *file_inode;
    struct buffer_head *bh_data, *bh;
    uint8_t *data;
    struct mlfs_ext_path *path = NULL;
    struct mlfs_map_blocks map;
    uint32_t nr_blocks = 0, nr_digested_blocks = 0;
    offset_t cur_offset;
    handle_t handle = {.dev = to_dev};

    mlfs_debug("[FILE] (%d->%d) inum %d offset %lu(0x%lx) length %u\n",
            from_dev, to_dev, file_inum, offset, offset, length);

    if (length < g_block_size_bytes)
        nr_blocks = 1;
    else {
        nr_blocks = (length >> g_block_size_shift);

        if (length % g_block_size_bytes != 0)
            nr_blocks++;
    }

    mlfs_assert(nr_blocks > 0);

    if ((from_dev == g_ssd_dev) || (from_dev == g_hdd_dev))
        panic("does not support this case yet\n");

    // Storage to storage copy.
    // FIXME: this does not work if migrating block from SSD to NVM.
    data = g_bdev[from_dev]->map_base_addr + (blknr << g_block_size_shift);

    file_inode = icache_find(g_root_dev, file_inum);
    if (!file_inode) {
        struct dinode dip;
        file_inode = icache_alloc_add(g_root_dev, file_inum);

        read_ondisk_inode(g_root_dev, file_inum, &dip);
        mlfs_assert(dip.itype != 0);

        sync_inode_from_dinode(file_inode, &dip);

        file_inode->i_sb = sb;

        mlfs_assert(dip.dev != 0);
    }

    mlfs_assert(file_inode->dev != 0);

    // update file inode length and mtime.
    if (file_inode->size < offset + length) {
        /* Inode size should be synchronized among other layers.
         * So, update both inodes */
        file_inode->size = offset + length;

        mlfs_mark_inode_dirty(file_inode);
    }

    nr_digested_blocks = 0;
    cur_offset = offset;
    offset_in_block = offset % g_block_size_bytes;

    // case 1. a single block writing: small size (< 4KB)
    // or a heading block of unaligned starting offset.
    if ((length < g_block_size_bytes) || offset_in_block != 0) {
        int _len = _min(length, (uint32_t)g_block_size_bytes - offset_in_block);

        map.m_lblk = (cur_offset >> g_block_size_shift);
        map.m_pblk = 0;
        map.m_len = 1;
        map.m_flags = 0;

        ret = mlfs_ext_get_blocks(&handle, file_inode, &map,
                MLFS_GET_BLOCKS_CREATE);

        mlfs_assert(ret == 1);

        bh_data = bh_get_sync_IO(to_dev, map.m_pblk, BH_NO_DATA_ALLOC);

        mlfs_assert(bh_data);

        bh_data->b_data = data + offset_in_block;
        bh_data->b_size = _len;
        bh_data->b_offset = offset_in_block;

#ifdef MIGRATION
        lru_key_t k = {
            .dev = to_dev,
            .block = map.m_pblk,
        };
        lru_val_t v = {
            .inum = file_inum,
            .lblock = map.m_lblk,
        };
        update_slru_list_from_digest(to_dev, k, v);
#endif
        //mlfs_debug("File data : %s\n", bh_data->b_data);

        ret = mlfs_write(bh_data);
        mlfs_assert(!ret);

        bh_release(bh_data);

        mlfs_debug("inum %d, offset %lu len %u (dev %d:%lu) -> (dev %d:%lu)\n",
                file_inode->inum, cur_offset, _len,
                from_dev, blknr, to_dev, map.m_pblk);

        nr_digested_blocks++;
        cur_offset += _len;
        data += _len;
    }

    // case 2. multiple trial of block writing.
    // when extent tree has holes in a certain offset (due to data migration),
    // an extent is split at the hole. Kernfs should call mlfs_ext_get_blocks()
    // with setting m_lblk to the offset having a the hole to fill it.
    while (nr_digested_blocks < nr_blocks) {
        int nr_block_get = 0, i;

        mlfs_assert((cur_offset % g_block_size_bytes) == 0);

        map.m_lblk = (cur_offset >> g_block_size_shift);
        map.m_pblk = 0;
        map.m_len = nr_blocks - nr_digested_blocks;
        map.m_flags = 0;

        // find block address of offset and update extent tree
        if (to_dev == g_ssd_dev || to_dev == g_hdd_dev) {
            //make kernelFS do log-structured update.
            //map.m_flags |= MLFS_MAP_LOG_ALLOC;
            nr_block_get = mlfs_ext_get_blocks(&handle, file_inode, &map,
                    MLFS_GET_BLOCKS_CREATE_DATA);
        } else {
            nr_block_get = mlfs_ext_get_blocks(&handle, file_inode, &map,
                    MLFS_GET_BLOCKS_CREATE_DATA);
        }

        mlfs_assert(map.m_pblk != 0);

        mlfs_assert(nr_block_get <= (nr_blocks - nr_digested_blocks));
        mlfs_assert(nr_block_get > 0);

        nr_digested_blocks += nr_block_get;

        // update data block
        bh_data = bh_get_sync_IO(to_dev, map.m_pblk, BH_NO_DATA_ALLOC);

        bh_data->b_data = data;
        bh_data->b_size = nr_block_get * g_block_size_bytes;
        bh_data->b_offset = 0;

#ifdef MIGRATION
        for (i = 0; i < nr_block_get; i++) {
            lru_key_t k = {
                .dev = to_dev,
                .block = map.m_pblk + i,
            };
            lru_val_t v = {
                .inum = file_inum,
                .lblock = map.m_lblk + i,
            };
            update_slru_list_from_digest(to_dev, k, v);
        }
#endif

        //mlfs_debug("File data : %s\n", bh_data->b_data);

        ret = mlfs_write(bh_data);
        mlfs_assert(!ret);
        clear_buffer_uptodate(bh_data);
        bh_release(bh_data);

        if (0) {
            struct buffer_head *bh;
            uint8_t tmp_buf[4096];
            bh = bh_get_sync_IO(to_dev, map.m_pblk, BH_NO_DATA_ALLOC);

            bh->b_data = tmp_buf;
            bh->b_size = g_block_size_bytes;
            bh_submit_read_sync_IO(bh);
            mlfs_io_wait(bh->b_dev, 1);

            GDB_TRAP;

            bh_release(bh);
        }

        mlfs_debug("inum %d, offset %lu len %u (dev %d:%lu) -> (dev %d:%lu)\n",
                file_inode->inum, cur_offset, bh_data->b_size,
                from_dev, blknr, to_dev, map.m_pblk);

        cur_offset += nr_block_get * g_block_size_bytes;
        data += nr_block_get * g_block_size_bytes;
    }

    mlfs_assert(nr_blocks == nr_digested_blocks);

    if (file_inode->size < offset + length)
        file_inode->size = offset + length;

    return 0;
}

//FIXME: this function is not synchronized with up-to-date
//changes. Refer digest_file to update this function.
int digest_file_iovec(uint8_t from_dev, uint8_t to_dev,
        uint32_t file_inum, f_iovec_t *iovec)
{
    int ret;
    uint32_t offset_in_block = 0;
    struct inode *file_inode;
    struct buffer_head *bh_data, *bh;
    uint8_t *data;
    struct mlfs_ext_path *path = NULL;
    struct mlfs_map_blocks map;
    uint32_t nr_blocks = 0, nr_digested_blocks = 0;
    offset_t cur_offset;
    uint32_t length = iovec->length;
    offset_t offset = iovec->offset;
    f_blklist_t *_blk_list;
    handle_t handle = {.dev = to_dev};

    mlfs_debug("[FILE] (%d->%d) inum %d offset %lu(0x%lx) length %u\n",
            from_dev, to_dev, file_inum, offset, offset, length);

    if (length < g_block_size_bytes)
        nr_blocks = 1;
    else {
        nr_blocks = (length >> g_block_size_shift);

        if (length % g_block_size_bytes != 0)
            nr_blocks++;
    }

    mlfs_assert(nr_blocks > 0);

    if (from_dev == g_ssd_dev)
        panic("does not support this case\n");

    file_inode = icache_find(to_dev, file_inum);
    if (!file_inode) {
        struct dinode dip;
        file_inode = icache_alloc_add(to_dev, file_inum);

        read_ondisk_inode(to_dev, file_inum, &dip);
        mlfs_assert(dip.itype != 0);
        mlfs_assert(dip.dev != 0);

        sync_inode_from_dinode(file_inode, &dip);

        file_inode->i_sb = sb;

    }

    mlfs_assert(file_inode->dev != 0);

#ifdef USE_SSD
    // update file inode length and mtime.
    if (file_inode->size < offset + length) {
        /* Inode size should be synchronized among NVM and SSD layer.
         * So, update both inodes */
        uint8_t sync_dev = 3 - to_dev;
        handle_t handle;
        struct inode *sync_file_inode = icache_find(sync_dev, file_inum);
        if (!sync_file_inode) {
            struct dinode dip;
            sync_file_inode = icache_alloc_add(sync_dev, file_inum);

            read_ondisk_inode(sync_dev, file_inum, &dip);

            mlfs_assert(dip.itype != 0);
            sync_inode_from_dinode(sync_file_inode, &dip);

            file_inode->i_sb = sb;
        }

        file_inode->size = offset + length;
        sync_file_inode->size = file_inode->size;

        handle.dev = to_dev;
        mlfs_mark_inode_dirty(file_inode);

        handle.dev = sync_dev;
        mlfs_mark_inode_dirty(sync_file_inode);
    }
#endif

    nr_digested_blocks = 0;
    cur_offset = offset;
    offset_in_block = offset % g_block_size_bytes;

    _blk_list = list_first_entry(&iovec->iov_blk_list, f_blklist_t, list);

    // case 1. a single block writing: small size (< 4KB)
    // or a heading block of unaligned starting offset.
    if ((length < g_block_size_bytes) || offset_in_block != 0) {
        int _len = _min(length, (uint32_t)g_block_size_bytes - offset_in_block);

        //mlfs_assert(_blk_list->n == 1);
        data = g_bdev[from_dev]->map_base_addr + (_blk_list->blknr << g_block_size_shift);

        map.m_lblk = (cur_offset >> g_block_size_shift);
        map.m_pblk = 0;
        map.m_len = 1;

        ret = mlfs_ext_get_blocks(&handle, file_inode, &map,
                MLFS_GET_BLOCKS_CREATE);

        mlfs_assert(ret == 1);

        bh_data = bh_get_sync_IO(to_dev, map.m_pblk, BH_NO_DATA_ALLOC);

        bh_data->b_data = data + offset_in_block;
        bh_data->b_size = _len;
        bh_data->b_offset = offset_in_block;

        //mlfs_debug("File data : %s\n", bh_data->b_data);

        ret = mlfs_write(bh_data);
        mlfs_assert(!ret);
        clear_buffer_uptodate(bh_data);
        bh_release(bh_data);

        mlfs_debug("inum %d, offset %lu (dev %d:%lx) -> (dev %d:%lx)\n",
                file_inode->inum, cur_offset, from_dev,
                blknr, to_dev, map.m_pblk);

        nr_digested_blocks++;
        cur_offset += _len;
        data += _len;
    }

    // case 2. multiple trial of block writing.
    // when extent tree has holes in a certain offset (due to data migration),
    // an extent is split at the hole. Kernfs should call mlfs_ext_get_blocks()
    // with setting m_lblk to the offset having a the hole to fill it.
    while (nr_digested_blocks < nr_blocks) {
        int nr_block_get = 0, i, j;
        int rand_val;

        mlfs_assert((cur_offset % g_block_size_bytes) == 0);

        map.m_lblk = (cur_offset >> g_block_size_shift);
        map.m_pblk = 0;
        map.m_len = min(nr_blocks - nr_digested_blocks, (1 << 15));

        // find block address of offset and update extent tree
        nr_block_get = mlfs_ext_get_blocks(&handle, file_inode, &map,
                MLFS_GET_BLOCKS_CREATE);

        mlfs_assert(map.m_pblk != 0);

        mlfs_assert(nr_block_get > 0);

        nr_digested_blocks += nr_block_get;

        if (_blk_list->n > nr_block_get) {
            data = g_bdev[from_dev]->map_base_addr +
                (_blk_list->blknr << g_block_size_shift);

            mlfs_debug("inum %d, offset %lu len %u (dev %d:%lu) -> (dev %d:%lu)\n",
                    file_inode->inum, cur_offset, _blk_list->n << g_block_size_shift,
                    from_dev, _blk_list->blknr, to_dev, map.m_pblk);

            bh_data = bh_get_sync_IO(to_dev, map.m_pblk, BH_NO_DATA_ALLOC);

            bh_data->b_data = data;
            bh_data->b_size = (_blk_list->n << g_block_size_shift);
            bh_data->b_offset = 0;

            //mlfs_debug("File data : %s\n", bh_data->b_data);

            ret = mlfs_write(bh_data);
            mlfs_assert(!ret);
            clear_buffer_uptodate(bh_data);
            bh_release(bh_data);

            cur_offset += (nr_block_get << g_block_size_shift);

            _blk_list->n -= nr_block_get;
            _blk_list->blknr += nr_block_get;

            continue;
        }

        for (i = 0, j = _blk_list->n; j <= nr_block_get;
                i += _blk_list->n, j += _blk_list->n) {
            data = g_bdev[from_dev]->map_base_addr +
                (_blk_list->blknr << g_block_size_shift);

            mlfs_debug("inum %d, offset %lu len %u (dev %d:%lu) -> (dev %d:%lu)\n",
                    file_inode->inum, cur_offset, _blk_list->n << g_block_size_shift,
                    from_dev, _blk_list->blknr, to_dev, map.m_pblk + i);

            // update data block
            bh_data = bh_get_sync_IO(to_dev, map.m_pblk + i, BH_NO_DATA_ALLOC);

            bh_data->b_data = data;
            bh_data->b_blocknr = map.m_pblk + i;
            bh_data->b_size = (_blk_list->n << g_block_size_shift);
            bh_data->b_offset = 0;

            //mlfs_debug("File data : %s\n", bh_data->b_data);

            ret = mlfs_write(bh_data);
            mlfs_assert(!ret);
            clear_buffer_uptodate(bh_data);
            bh_release(bh_data);

            cur_offset += (_blk_list->n << g_block_size_shift);

            _blk_list = list_next_entry(_blk_list, list);

            if (&_blk_list->list == &iovec->iov_blk_list)
                break;
        }
    }

    mlfs_assert(nr_blocks == nr_digested_blocks);

    if (file_inode->size < offset + length)
        file_inode->size = offset + length;

    return 0;
}

int digest_unlink(uint8_t from_dev, uint8_t to_dev, uint32_t inum)
{
    struct buffer_head *bh;
    struct inode *inode;
    struct dinode dinode;
    int ret = 0;
    mlfs_debug("unlink: to_dev = %d\n", to_dev);
    mlfs_debug("unlink: g_root_dev = %d\n", g_root_dev);
    mlfs_assert(to_dev == g_root_dev);

    mlfs_debug("[UNLINK] (%d->%d) inum %d\n", from_dev, to_dev, inum);

    inode = icache_find(to_dev, inum);
    mlfs_assert(inode);

    mlfs_assert(!(inode->flags & I_DELETING));

    if (inode->itype == T_FILE) {
        if (inode->size > 0) {
            handle_t handle = {.dev = to_dev};
            mlfs_lblk_t end = (inode->size) >> g_block_size_shift;

            ret = mlfs_ext_truncate(&handle, inode, 0, end == 0 ? end : end - 1);
            mlfs_assert(!ret);
        }
    } else if (inode->itype == T_DIR) {
        ;
    } else {
        panic("unsupported inode type\n");
    }

    memset(inode->_dinode, 0, sizeof(struct dinode));

    inode->dev = to_dev;
    inode->flags = 0;
    inode->flags |= I_DELETING;
    inode->size = 0;
    inode->itype = 0;

    mlfs_mark_inode_dirty(inode);

    return 0;
}

static void digest_each_log_entries(uint8_t from_dev, loghdr_meta_t *loghdr_meta)
{
    int i, ret;
    loghdr_t *loghdr;
    uint16_t nr_entries;
    uint64_t tsc_begin;

    nr_entries = loghdr_meta->loghdr->n;
    loghdr = loghdr_meta->loghdr;

    for (i = 0; i < nr_entries; i++) {
        if (enable_perf_stats)
            g_perf_stats.n_digest++;

        // parse log entries on types.
        switch(loghdr->type[i]) {
            case L_TYPE_INODE_CREATE:
                // ftruncate is handled by this case.
            case L_TYPE_INODE_UPDATE: {
                                          if (enable_perf_stats)
                                              tsc_begin = asm_rdtscp();

                                          ret = digest_inode(from_dev,
                                                  g_root_dev,
                                                  loghdr->inode_no[i],
                                                  loghdr->blocks[i]);
                                          mlfs_assert(!ret);

                                          if (enable_perf_stats)
                                              g_perf_stats.digest_inode_tsc +=
                                                  asm_rdtscp() - tsc_begin;
                                          break;
                                      }
            case L_TYPE_DIR_ADD:
            case L_TYPE_DIR_RENAME:
            case L_TYPE_DIR_DEL: {
                                     if (enable_perf_stats)
                                         tsc_begin = asm_rdtscp();

                                     ret = digest_directory(from_dev,
                                             g_root_dev,
                                             i,
                                             loghdr->type[i],
                                             loghdr->inode_no[i],
                                             loghdr->length[i],
                                             loghdr->data[i],
                                             loghdr_meta->hdr_blkno);
                                     mlfs_assert(!ret);

                                     if (enable_perf_stats)
                                         g_perf_stats.digest_dir_tsc +=
                                             asm_rdtscp() - tsc_begin;
                                     break;
                                 }
            case L_TYPE_FILE: {
                                  uint8_t dest_dev = g_root_dev;
                                  int rand_val;
                                  lru_key_t k;

                                  if (enable_perf_stats)
                                      tsc_begin = asm_rdtscp();
#ifdef USE_SSD
                                  // for NVM bypassing test
                                  //dest_dev = g_ssd_dev;
#endif
                                  ret = digest_file(from_dev,
                                          dest_dev,
                                          loghdr->inode_no[i],
                                          loghdr->data[i],
                                          loghdr->length[i],
                                          loghdr->blocks[i]);
                                  mlfs_assert(!ret);

                                  if (enable_perf_stats)
                                      g_perf_stats.digest_file_tsc +=
                                          asm_rdtscp() - tsc_begin;
                                  break;
                              }
            case L_TYPE_UNLINK: {
                                    if (enable_perf_stats)
                                        tsc_begin = asm_rdtscp();

                                    ret = digest_unlink(from_dev,
                                            g_root_dev,
                                            loghdr->inode_no[i]);
                                    mlfs_assert(!ret);

                                    if (enable_perf_stats)
                                        g_perf_stats.digest_inode_tsc +=
                                            asm_rdtscp() - tsc_begin;
                                    break;
                                }
            default: {
                         printf("%s: digest type %d\n", __func__, loghdr->type[i]);
                         panic("unsupported type of operation\n");
                         break;
                     }
        }
    }
}

static void digest_replay_and_optimize(uint8_t from_dev,
        loghdr_meta_t *loghdr_meta, struct replay_list *replay_list)
{
    int i, ret;
    loghdr_t *loghdr;
    uint16_t nr_entries;

    nr_entries = loghdr_meta->loghdr->n;
    loghdr = loghdr_meta->loghdr;

    for (i = 0; i < nr_entries; i++) {
        switch(loghdr->type[i]) {
            case L_TYPE_INODE_CREATE:
            case L_TYPE_INODE_UPDATE: {
                                          i_replay_t search, *item;
                                          memset(&search, 0, sizeof(i_replay_t));

                                          search.key.inum = loghdr->inode_no[i];

                                          if (loghdr->type[i] == L_TYPE_INODE_CREATE)
                                              inode_version_table[search.key.inum]++;
                                          search.key.ver = inode_version_table[search.key.inum];

                                          HASH_FIND(hh, replay_list->i_digest_hash, &search.key,
                                                  sizeof(replay_key_t), item);
                                          if (!item) {
                                              item = (i_replay_t *)mlfs_zalloc(sizeof(i_replay_t));
                                              item->key = search.key;
                                              item->node_type = NTYPE_I;
                                              list_add_tail(&item->list, &replay_list->head);

                                              // tag the inode coalecing starts from inode creation.
                                              // This is crucial information to decide whether
                                              // unlink can skip or not.
                                              if (loghdr->type[i] == L_TYPE_INODE_CREATE)
                                                  item->create = 1;
                                              else
                                                  item->create = 0;

                                              HASH_ADD(hh, replay_list->i_digest_hash, key,
                                                      sizeof(replay_key_t), item);
                                              mlfs_debug("[INODE] inum %u (ver %u) - create %d\n",
                                                      item->key.inum, item->key.ver, item->create);
                                          }
                                          // move blknr to point the up-to-date inode snapshot in the log.
                                          item->blknr = loghdr->blocks[i];
                                          if (enable_perf_stats)
                                              g_perf_stats.n_digest++;
                                          break;
                                      }
            case L_TYPE_DIR_DEL:
            case L_TYPE_DIR_RENAME:
            case L_TYPE_DIR_ADD: {
                                     d_replay_t search, *item;
                                     // search must be zeroed at the beginning.
                                     memset(&search, 0, sizeof(d_replay_t));
                                     search.key.inum = loghdr->data[i];
                                     search.key.type = loghdr->type[i];
                                     search.key.ver = inode_version_table[loghdr->data[i]];

                                     HASH_FIND(hh, replay_list->d_digest_hash, &search.key,
                                             sizeof(d_replay_key_t), item);
                                     if (!item) {
                                         item = (d_replay_t *)mlfs_zalloc(sizeof(d_replay_t));
                                         item->key = search.key;
                                         HASH_ADD(hh, replay_list->d_digest_hash, key,
                                                 sizeof(d_replay_key_t), item);
                                         item->node_type = NTYPE_D;
                                         list_add_tail(&item->list, &replay_list->head);
                                         mlfs_debug("[ DIR ] inum %u (ver %u) - %s\n",
                                                 item->key.inum,
                                                 item->key.ver,
                                                 loghdr->type[i] == L_TYPE_DIR_ADD ? "ADD" :
                                                 loghdr->type[i] == L_TYPE_DIR_DEL ? "DEL" :
                                                 "RENAME");
                                     }
                                     item->n = i;
                                     item->dir_inum = loghdr->inode_no[i];
                                     item->dir_size = loghdr->length[i];
                                     item->blknr = loghdr_meta->hdr_blkno;
                                     if (enable_perf_stats)
                                         g_perf_stats.n_digest++;
                                     break;
                                 }
            case L_TYPE_FILE: {
                                  f_replay_t search, *item;
                                  f_iovec_t *f_iovec;
                                  f_blklist_t *_blk_list;
                                  lru_key_t k;
                                  offset_t iovec_key;
                                  int found = 0;

                                  memset(&search, 0, sizeof(f_replay_t));
                                  search.key.inum = loghdr->inode_no[i];
                                  search.key.ver = inode_version_table[loghdr->inode_no[i]];

                                  HASH_FIND(hh, replay_list->f_digest_hash, &search.key,
                                          sizeof(replay_key_t), item);
                                  if (!item) {
                                      item = (f_replay_t *)mlfs_zalloc(sizeof(f_replay_t));
                                      item->key = search.key;

                                      HASH_ADD(hh, replay_list->f_digest_hash, key,
                                              sizeof(replay_key_t), item);

                                      INIT_LIST_HEAD(&item->iovec_list);
                                      item->node_type = NTYPE_F;
                                      item->iovec_hash = NULL;
                                      list_add_tail(&item->list, &replay_list->head);
                                  }

#ifndef EXPERIMENTAL
#ifdef IOMERGE
                                  // IO data is merged if the same offset found.
                                  // Reduce amount IO when IO data has locality such as Zipf dist.
                                  // FIXME: currently iomerge works correctly when IO size is
                                  // 4 KB and aligned.
                                  iovec_key = ALIGN_FLOOR(loghdr->data[i], g_block_size_bytes);

                                  if (loghdr->data[i] % g_block_size_bytes !=0 ||
                                          loghdr->length[i] != g_block_size_bytes)
                                      panic("IO merge is not support current IO pattern\n");

                                  HASH_FIND(hh, item->iovec_hash,
                                          &iovec_key, sizeof(offset_t), f_iovec);

                                  if (f_iovec &&
                                          (f_iovec->length == loghdr->length[i])) {
                                      f_iovec->offset = iovec_key;
                                      f_iovec->blknr = loghdr->blocks[i];
                                      // TODO: merge data from loghdr->blocks to f_iovec buffer.
                                      found = 1;
                                  }

                                  if (!found) {
                                      f_iovec = (f_iovec_t *)mlfs_zalloc(sizeof(f_iovec_t));
                                      f_iovec->length = loghdr->length[i];
                                      f_iovec->offset = loghdr->data[i];
                                      f_iovec->blknr = loghdr->blocks[i];
                                      INIT_LIST_HEAD(&f_iovec->list);
                                      list_add_tail(&f_iovec->list, &item->iovec_list);

                                      f_iovec->hash_key = iovec_key;
                                      HASH_ADD(hh, item->iovec_hash, hash_key,
                                              sizeof(offset_t), f_iovec);
                                  }
#else
                                  f_iovec = (f_iovec_t *)mlfs_zalloc(sizeof(f_iovec_t));
                                  f_iovec->length = loghdr->length[i];
                                  f_iovec->offset = loghdr->data[i];
                                  f_iovec->blknr = loghdr->blocks[i];
                                  INIT_LIST_HEAD(&f_iovec->list);
                                  list_add_tail(&f_iovec->list, &item->iovec_list);
#endif  //IOMERGE

#else //EXPERIMENTAL
                                  // Experimental feature: merge contiguous small writes to
                                  // a single write one.
                                  mlfs_debug("new log block %lu\n", loghdr->blocks[i]);
                                  _blk_list = (f_blklist_t *)mlfs_zalloc(sizeof(f_blklist_t));
                                  INIT_LIST_HEAD(&_blk_list->list);

                                  // FIXME: Now only support 4K aligned write.
                                  _blk_list->n = (loghdr->length[i] >> g_block_size_shift);
                                  _blk_list->blknr = loghdr->blocks[i];

                                  if (!list_empty(&item->iovec_list)) {
                                      f_iovec = list_last_entry(&item->iovec_list, f_iovec_t, list);

                                      // Find the case where io_vector can be coalesced.
                                      if (f_iovec->offset + f_iovec->length == loghdr->data[i]) {
                                          f_iovec->length += loghdr->length[i];
                                          f_iovec->n_list++;

                                          mlfs_debug("block is merged %u\n", _blk_list->blknr);
                                          list_add_tail(&_blk_list->list, &f_iovec->iov_blk_list);
                                      } else {
                                          mlfs_debug("new f_iovec %lu\n",  loghdr->data[i]);
                                          // cannot coalesce io_vector. allocate new one.
                                          f_iovec = (f_iovec_t *)mlfs_zalloc(sizeof(f_iovec_t));
                                          f_iovec->length = loghdr->length[i];
                                          f_iovec->offset = loghdr->data[i];
                                          f_iovec->blknr = loghdr->blocks[i];
                                          INIT_LIST_HEAD(&f_iovec->list);
                                          INIT_LIST_HEAD(&f_iovec->iov_blk_list);

                                          list_add_tail(&_blk_list->list, &f_iovec->iov_blk_list);
                                          list_add_tail(&f_iovec->list, &item->iovec_list);
                                      }
                                  } else {
                                      f_iovec = (f_iovec_t *)mlfs_zalloc(sizeof(f_iovec_t));
                                      f_iovec->length = loghdr->length[i];
                                      f_iovec->offset = loghdr->data[i];
                                      f_iovec->blknr = loghdr->blocks[i];
                                      INIT_LIST_HEAD(&f_iovec->list);
                                      INIT_LIST_HEAD(&f_iovec->iov_blk_list);

                                      list_add_tail(&_blk_list->list, &f_iovec->iov_blk_list);
                                      list_add_tail(&f_iovec->list, &item->iovec_list);
                                  }
#endif

                                  if (enable_perf_stats)
                                      g_perf_stats.n_digest++;
                                  break;
                              }
            case L_TYPE_UNLINK: {
                                    // Got it! Kernfs can skip digest of related items.
                                    // clean-up inode, directory, file digest operations for the inode.
                                    uint32_t inum = loghdr->inode_no[i];
                                    i_replay_t i_search, *i_item;
                                    d_replay_t d_search, *d_item;
                                    f_replay_t f_search, *f_item;
                                    u_replay_t u_search, *u_item;
                                    //d_replay_key_t d_key;
                                    f_iovec_t *f_iovec, *tmp;

                                    replay_key_t key = {
                                        .inum = loghdr->inode_no[i],
                                        .ver = inode_version_table[loghdr->inode_no[i]],
                                    };

                                    // This is required for structure key in UThash.
                                    memset(&i_search, 0, sizeof(i_replay_t));
                                    memset(&d_search, 0, sizeof(d_replay_t));
                                    memset(&f_search, 0, sizeof(f_replay_t));
                                    memset(&u_search, 0, sizeof(u_replay_t));

                                    mlfs_debug("%s\n", "-------------------------------");

                                    // check inode digest info can skip.
                                    i_search.key.inum = key.inum;
                                    i_search.key.ver = key.ver;
                                    HASH_FIND(hh, replay_list->i_digest_hash, &i_search.key,
                                            sizeof(replay_key_t), i_item);

                                    if (i_item && i_item->create) {
                                        mlfs_debug("[INODE] inum %u (ver %u) --> SKIP\n",
                                                i_item->key.inum, i_item->key.ver);
                                        // the unlink can skip and erase related i_items
                                        HASH_DEL(replay_list->i_digest_hash, i_item);
                                        list_del(&i_item->list);
                                        mlfs_free(i_item);
                                        if (enable_perf_stats)
                                            g_perf_stats.n_digest_skipped++;
                                    } else {
                                        // the unlink must be applied. create a new unlink item.
                                        u_item = (u_replay_t *)mlfs_zalloc(sizeof(u_replay_t));
                                        u_item->key = key;
                                        u_item->node_type = NTYPE_U;
                                        HASH_ADD(hh, replay_list->u_digest_hash, key,
                                                sizeof(replay_key_t), u_item);
                                        list_add_tail(&u_item->list, &replay_list->head);
                                        mlfs_debug("[ULINK] inum %u (ver %u)\n",
                                                u_item->key.inum, u_item->key.ver);
                                        if (enable_perf_stats)
                                            g_perf_stats.n_digest++;
                                    }

#if 0
                                    HASH_FIND(hh, replay_list->u_digest_hash, &key,
                                            sizeof(replay_key_t), u_item);
                                    if (u_item) {
                                        // previous unlink can skip.
                                        mlfs_debug("[ULINK] inum %u (ver %u) --> SKIP\n",
                                                u_item->key.inum, u_item->key.ver);
                                        HASH_DEL(replay_list->u_digest_hash, u_item);
                                        list_del(&u_item->list);
                                        mlfs_free(u_item);
                                    }
#endif

                                    // check directory digest info to skip.
                                    d_search.key.inum = inum;
                                    d_search.key.ver = key.ver;
                                    d_search.key.type = L_TYPE_DIR_ADD;

                                    HASH_FIND(hh, replay_list->d_digest_hash, &d_search.key,
                                            sizeof(d_replay_key_t), d_item);

                                    if (d_item) {
                                        mlfs_debug("[ DIR ] inum %u (ver %u) - ADD --> SKIP\n",
                                                d_item->key.inum, d_item->key.ver);
                                        HASH_DEL(replay_list->d_digest_hash, d_item);
                                        list_del(&d_item->list);
                                        mlfs_free(d_item);
                                        if (enable_perf_stats)
                                            g_perf_stats.n_digest_skipped++;
                                    }

                                    d_search.key.inum = inum;
                                    d_search.key.ver = key.ver;
                                    d_search.key.type = L_TYPE_DIR_RENAME;

                                    HASH_FIND(hh, replay_list->d_digest_hash, &d_search.key,
                                            sizeof(d_replay_key_t), d_item);

                                    if (d_item) {
                                        mlfs_debug("[ DIR ] inum %u (ver %u) - RENAME --> SKIP\n",
                                                d_item->key.inum, d_item->key.ver);
                                        HASH_DEL(replay_list->d_digest_hash, d_item);
                                        list_del(&d_item->list);
                                        mlfs_free(d_item);
                                        if (enable_perf_stats)
                                            g_perf_stats.n_digest_skipped++;
                                    }

                                    // unlink digest must happens before directory delete digest.
                                    memset(&d_search, 0, sizeof(d_replay_t));
                                    d_search.key.inum = inum;
                                    d_search.key.ver = key.ver;
                                    d_search.key.type = L_TYPE_DIR_DEL;

                                    HASH_FIND(hh, replay_list->d_digest_hash, &d_search.key,
                                            sizeof(d_replay_key_t), d_item);

                                    if (d_item && d_item->key.ver != 0 ) {
                                        mlfs_debug("[ DIR ] inum %u (ver %u) - DEL --> SKIP\n",
                                                d_item->key.inum, d_item->key.ver);
                                        HASH_DEL(replay_list->d_digest_hash, d_item);
                                        list_del(&d_item->list);
                                        mlfs_free(d_item);

                                        if (enable_perf_stats)
                                            g_perf_stats.n_digest_skipped++;
                                    }

                                    // delete file digest info.
                                    f_search.key.inum = key.inum;
                                    f_search.key.ver = key.ver;

                                    HASH_FIND(hh, replay_list->f_digest_hash, &f_search.key,
                                            sizeof(replay_key_t), f_item);

                                    if (f_item) {
                                        list_for_each_entry_safe(f_iovec, tmp,
                                                &f_item->iovec_list, list) {
                                            list_del(&f_iovec->list);
                                            mlfs_free(f_iovec);

                                            if (enable_perf_stats)
                                                g_perf_stats.n_digest_skipped++;
                                        }

                                        HASH_DEL(replay_list->f_digest_hash, f_item);
                                        list_del(&f_item->list);
                                        mlfs_free(f_item);
                                    }

                                    mlfs_debug("%s\n", "-------------------------------");
                                    break;
                                }
            default: {
                         printf("%s: digest type %d\n", __func__, loghdr->type[i]);
                         panic("unsupported type of operation\n");
                         break;
                     }
        }
    }
}

#ifdef FCONCURRENT
static void file_digest_worker(void *arg)
{
    struct f_digest_worker_arg *_arg = (struct f_digest_worker_arg *)arg;
    f_iovec_t *f_iovec, *iovec_tmp;
    f_replay_t *f_item;
    lru_key_t k;

    f_item = _arg->f_item;

    list_for_each_entry_safe(f_iovec, iovec_tmp,
            &f_item->iovec_list, list) {
        digest_file(_arg->from_dev, _arg->to_dev,
                f_item->key.inum, f_iovec->offset,
                f_iovec->length, f_iovec->blknr);
        mlfs_free(f_iovec);
    }

    mlfs_free(_arg);
}
#endif

// digest must be applied in order since unlink and creation cannot commute.
static void digest_log_from_replay_list(uint8_t from_dev, struct replay_list *replay_list)
{
    struct list_head *l, *tmp;
    uint8_t *node_type;
    f_iovec_t *f_iovec, *iovec_tmp;
    uint64_t tsc_begin;

    list_for_each_safe(l, tmp, &replay_list->head) {
        node_type = (uint8_t *)l + sizeof(struct list_head);
        mlfs_assert(*node_type < 5);

        switch(*node_type) {
            case NTYPE_I: {
                              i_replay_t *i_item;
                              i_item = (i_replay_t *)container_of(l, i_replay_t, list);

                              if (enable_perf_stats)
                                  tsc_begin = asm_rdtscp();

                              digest_inode(from_dev, g_root_dev, i_item->key.inum, i_item->blknr);

                              HASH_DEL(replay_list->i_digest_hash, i_item);
                              list_del(l);
                              mlfs_free(i_item);

                              if (enable_perf_stats)
                                  g_perf_stats.digest_inode_tsc += asm_rdtscp() - tsc_begin;
                              break;
                          }
            case NTYPE_D: {
                              d_replay_t *d_item;
                              d_item = (d_replay_t *)container_of(l, d_replay_t, list);

                              if (enable_perf_stats)
                                  tsc_begin = asm_rdtscp();

                              digest_directory(from_dev, g_root_dev, d_item->n,
                                      d_item->key.type, d_item->dir_inum, d_item->dir_size,
                                      d_item->key.inum, d_item->blknr);

                              HASH_DEL(replay_list->d_digest_hash, d_item);
                              list_del(l);
                              mlfs_free(d_item);

                              if (enable_perf_stats)
                                  g_perf_stats.digest_dir_tsc += asm_rdtscp() - tsc_begin;
                              break;
                          }
            case NTYPE_F: {
                              uint8_t dest_dev = g_root_dev;
                              f_replay_t *f_item, *t;
                              f_item = (f_replay_t *)container_of(l, f_replay_t, list);
                              lru_key_t k;

                              if (enable_perf_stats)
                                  tsc_begin = asm_rdtscp();

#ifdef FCONCURRENT
                              HASH_ITER(hh, replay_list->f_digest_hash, f_item, t) {
                                  struct f_digest_worker_arg *arg;

                                  // Digest worker thread will free the arg.
                                  arg = (struct f_digest_worker_arg *)mlfs_alloc(
                                          sizeof(struct f_digest_worker_arg));

                                  arg->from_dev = from_dev;
                                  arg->to_dev = g_root_dev;
                                  arg->f_item = f_item;

                                  thpool_add_work(file_digest_thread_pool,
                                          file_digest_worker, (void *)arg);
                              }

                              //if (thpool_num_threads_working(file_digest_thread_pool))
                              thpool_wait(file_digest_thread_pool);

                              HASH_ITER(hh, replay_list->f_digest_hash, f_item, t) {
                                  HASH_DEL(replay_list->f_digest_hash, f_item);
                                  mlfs_free(f_item);
                              }
#else
                              list_for_each_entry_safe(f_iovec, iovec_tmp,
                                      &f_item->iovec_list, list) {

#ifndef EXPERIMENTAL
                                  digest_file(from_dev, dest_dev,
                                          f_item->key.inum, f_iovec->offset,
                                          f_iovec->length, f_iovec->blknr);
                                  mlfs_free(f_iovec);
#else
                                  digest_file_iovec(from_dev, dest_dev,
                                          f_item->key.inum, f_iovec);
#endif //EXPERIMETNAL
                                  if (dest_dev == g_ssd_dev)
                                      mlfs_io_wait(g_ssd_dev, 0);
                              }

                              HASH_DEL(replay_list->f_digest_hash, f_item);
                              mlfs_free(f_item);
#endif //FCONCURRENT

                              list_del(l);

                              if (enable_perf_stats)
                                  g_perf_stats.digest_file_tsc += asm_rdtscp() - tsc_begin;
                              break;
                          }
            case NTYPE_U: {
                              u_replay_t *u_item;
                              u_item = (u_replay_t *)container_of(l, u_replay_t, list);

                              if (enable_perf_stats)
                                  tsc_begin = asm_rdtscp();

                              digest_unlink(from_dev, g_root_dev, u_item->key.inum);

                              HASH_DEL(replay_list->u_digest_hash, u_item);
                              list_del(l);
                              mlfs_free(u_item);

                              if (enable_perf_stats)
                                  g_perf_stats.digest_inode_tsc += asm_rdtscp() - tsc_begin;
                              break;
                          }
            default:
                          panic("unsupported node type!\n");
        }
    }

    HASH_CLEAR(hh, replay_list->i_digest_hash);
    HASH_CLEAR(hh, replay_list->d_digest_hash);
    HASH_CLEAR(hh, replay_list->f_digest_hash);
    HASH_CLEAR(hh, replay_list->u_digest_hash);

#if 0
    i_replay_t *i_item, *i_tmp;
    d_replay_t *d_item, *d_tmp;
    f_replay_t *f_item, *f_tmp;
    f_iovec_t *f_iovec, *iovec_tmp;

    HASH_ITER(hh, replay_list->i_digest_hash, i_item, i_tmp) {
        digest_inode(from_dev, g_root_dev, i_item->inum, i_item->blknr);
        mlfs_free(i_item);
    }

    HASH_ITER(hh, replay_list->d_digest_hash, d_item, d_tmp) {
        digest_directory(from_dev, g_root_dev, d_item->n, d_item->key.type,
                d_item->dir_inum, d_item->dir_size, d_item->key.inum, d_item->blknr);
        mlfs_free(d_item);
    }

    HASH_ITER(hh, replay_list->f_digest_hash, f_item, f_tmp) {
        list_for_each_entry_safe(f_iovec, iovec_tmp, &f_item->iovec_list, list) {
            digest_file(from_dev, g_root_dev, f_item->inum, f_iovec->offset,
                    f_iovec->length, f_iovec->blknr);
            mlfs_free(f_iovec);
        }
        mlfs_free(f_item);
    }
#endif
}

static int persist_dirty_objects_nvm(void)
{
    struct rb_node *node;

    // flush extent tree changes
    sync_all_buffers(g_bdev[g_root_dev]);

    // save dirty inodes
    for (node = rb_first(&sb[g_root_dev]->s_dirty_root);
            node; node = rb_next(node)) {
        struct inode *ip = rb_entry(node, struct inode, i_rb_node);
        mlfs_debug("[dev %d] write dirty inode %d size %lu\n",
                ip->dev, ip->inum, ip->size);
        rb_erase(&ip->i_rb_node, &get_inode_sb(g_root_dev, ip)->s_dirty_root);
        write_ondisk_inode(g_root_dev, ip);

        if (ip->itype == T_DIR)
            persist_dirty_dirent_block(ip);
    }

    // save block allocation bitmap
    store_all_bitmap(g_root_dev, sb[g_root_dev]->s_blk_bitmap);

    return 0;
}

static int persist_dirty_objects_ssd(void)
{
    struct rb_node *node;

    sync_all_buffers(g_bdev[g_ssd_dev]);

    store_all_bitmap(g_ssd_dev, sb[g_ssd_dev]->s_blk_bitmap);

    return 0;
}

static int persist_dirty_objects_hdd(void)
{
    struct rb_node *node;

    sync_all_buffers(g_bdev[g_hdd_dev]);

    store_all_bitmap(g_hdd_dev, sb[g_hdd_dev]->s_blk_bitmap);

    return 0;
}

static int digest_logs(uint8_t from_dev, int n_hdrs,
        addr_t *loghdr_to_digest, int *rotated)
{
    loghdr_meta_t *loghdr_meta;
    int i, n_digest;
    uint64_t tsc_begin;
    static addr_t previous_loghdr_blk;
    struct replay_list replay_list = {
        .i_digest_hash = NULL,
        .d_digest_hash = NULL,
        .f_digest_hash = NULL,
        .u_digest_hash = NULL,
    };

    INIT_LIST_HEAD(&replay_list.head);

    memset(inode_version_table, 0, sizeof(uint16_t) * NINODES);

    // digest log entries
    for (i = 0 ; i < n_hdrs; i++) {
        loghdr_meta = read_log_header(from_dev, *loghdr_to_digest);

        if (loghdr_meta->loghdr->inuse != LH_COMMIT_MAGIC) {
            mlfs_assert(loghdr_meta->loghdr->inuse == 0);
            mlfs_free(loghdr_meta);
            break;
        }

#ifdef DIGEST_OPT
        if (enable_perf_stats)
            tsc_begin = asm_rdtscp();
        digest_replay_and_optimize(from_dev, loghdr_meta, &replay_list);
        if (enable_perf_stats)
            g_perf_stats.replay_time_tsc += asm_rdtscp() - tsc_begin;
#else
        digest_each_log_entries(from_dev, loghdr_meta);
#endif

        // rotated when next_loghdr_blkno jumps to beginning of the log.
        // FIXME: instead of this condition, it would be better if
        // *loghdr_to_digest > the lost block of application log.
        if (*loghdr_to_digest > loghdr_meta->loghdr->next_loghdr_blkno) {
            mlfs_debug("loghdr_to_digest %lu, next header %lu\n",
                    *loghdr_to_digest, loghdr_meta->loghdr->next_loghdr_blkno);
            *rotated = 1;
        }

        *loghdr_to_digest = loghdr_meta->loghdr->next_loghdr_blkno;

        previous_loghdr_blk = loghdr_meta->hdr_blkno;

        mlfs_free(loghdr_meta);
    }

#ifdef DIGEST_OPT
    if (enable_perf_stats)
        tsc_begin = asm_rdtscp();
    digest_log_from_replay_list(from_dev, &replay_list);
    if (enable_perf_stats)
        g_perf_stats.apply_time_tsc += asm_rdtscp() - tsc_begin;
#endif

    n_digest = i;

    if (0) {
        ncx_slab_stat_t slab_stat;
        ncx_slab_stat(mlfs_slab_pool, &slab_stat);
    }

    return n_digest;
}


void reset_lease() {
    mlfs_lease_t *lease;
    mlfs_mutex_lock(&lease_lock);
    for(lease = mlfs_lease_global; lease != NULL; lease = (mlfs_lease_t* )lease->hh.next) {
        lease->last_op_stat = unknown;
    }
    mlfs_mutex_unlock(&lease_lock);
}
static void handle_digest_request(void *arg)
{
    uint32_t dev_id;
    int sock_fd;
    struct digest_arg *digest_arg;
    char *buf, response[MAX_SOCK_BUF];
    char cmd_header[12];
    int rotated = 0;
    int lru_updated = 0;
    addr_t digest_blkno, end_blkno;
    uint32_t digest_count;
    addr_t next_hdr_of_digested_hdr;

    memset(cmd_header, 0, 12);

    digest_arg = (struct digest_arg *)arg;

    sock_fd = digest_arg->sock_fd;
    buf = digest_arg->msg;

    // parsing digest request
    sscanf(buf, "|%s |%d|%u|%lu|%lu|",
            cmd_header, &dev_id, &digest_count, &digest_blkno, &end_blkno);

    mlfs_info("%s\n", cmd_header);
    if (strcmp(cmd_header, "digest") == 0) {
        mlfs_info("digest command: dev_id %u, digest_blkno %lx, digest_count %u\n",
                dev_id, digest_blkno, digest_count);

        if (enable_perf_stats) {
            g_perf_stats.digest_time_tsc = asm_rdtscp();
            g_perf_stats.path_search_tsc = 0;
            g_perf_stats.replay_time_tsc = 0;
            g_perf_stats.apply_time_tsc= 0;
            g_perf_stats.digest_dir_tsc = 0;
            g_perf_stats.digest_file_tsc = 0;
            g_perf_stats.digest_inode_tsc = 0;
            g_perf_stats.n_digest_skipped = 0;
            g_perf_stats.n_digest = 0;
        }

        digest_count = digest_logs(dev_id, digest_count, &digest_blkno, &rotated);

        if (enable_perf_stats)
            g_perf_stats.digest_time_tsc =
                (asm_rdtscp() - g_perf_stats.digest_time_tsc);

        mlfs_debug("-- Total used block %d\n",
                bitmap_weight((uint64_t *)sb[g_root_dev].s_blk_bitmap->bitmap,
                    sb[g_root_dev].ondisk->ndatablocks));

        memset(response, 0, MAX_SOCK_BUF);
        sprintf(response, "|ACK |%d|%lu|%d|%d|",
                digest_count, digest_blkno, rotated, lru_updated);
        mlfs_info("Write %s to libfs\n", response);

        persist_dirty_objects_nvm();
#ifdef USE_SSD
        persist_dirty_objects_ssd();
#endif
#ifdef USE_HDD
        persist_dirty_objects_hdd();
#endif

        sendto(sock_fd, response, MAX_SOCK_BUF, 0,
                (struct sockaddr *)&digest_arg->cli_addr, sizeof(struct sockaddr_un));

        reset_lease();
        show_storage_stats();

        if (enable_perf_stats)
            show_kernfs_stats();
    } else if (strcmp(cmd_header, "lru") == 0) {
        // only used for debugging.
        if (0) {
            lru_node_t *l;
            list_for_each_entry(l, &lru_heads[g_log_dev], list)
                mlfs_info("%u - %lu\n",
                        l->val.inum, l->val.lblock);
        }
    } else {
        panic("invalid command\n");
    }

    mlfs_free(arg);
}

#define MAX_EVENTS 4
static void wait_for_event(void)
{
    int sock_fd, epfd, flags, n, ret;
    struct sockaddr_un addr;
    struct epoll_event epev = {0};
    struct epoll_event *events;
    int i;

    if ((sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
        panic ("socket error");

    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SRV_SOCK_PATH, sizeof(addr.sun_path));

    unlink(SRV_SOCK_PATH);

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind error:");
        panic("bind error");
    }

    // make it non-blocking
    flags = fcntl(sock_fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    ret = fcntl(sock_fd, F_SETFL, flags);
    if (ret < 0)
        panic("fail to set non-blocking mode\n");

    epfd = epoll_create(1);
    epev.data.fd = sock_fd;
    epev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLET;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, sock_fd, &epev);
    if (ret < 0)
        panic("fail to connect epoll fd\n");

    events = mlfs_zalloc(sizeof(struct epoll_event) * MAX_EVENTS);

    while(1) {
        n = epoll_wait(epfd, events, MAX_EVENTS, -1);

        if (n < 0 && errno != EINTR)
            panic("epoll has error\n");

        for (i = 0; i < n; i++) {
            /*
               if ((events[i].events & EPOLLERR) ||
               (events[i].events & EPOLLHUP) ||
               (!(events[i].events & EPOLLIN)))
               {
               fprintf (stderr, "epoll error\n");
               continue;
               }
               */

            if ((events[i].events & EPOLLIN) &&
                    events[i].data.fd == sock_fd) {
                int ret;
                char buf[MAX_SOCK_BUF];
                char cmd_header[12];
                uint32_t dev_id;
                uint32_t digest_count;
                addr_t digest_blkno, end_blkno;
                struct sockaddr_un cli_addr;
                socklen_t len = sizeof(struct sockaddr_un);
                struct digest_arg *digest_arg;

                ret = recvfrom(sock_fd, buf, MAX_SOCK_BUF, 0,
                        (struct sockaddr *)&cli_addr, &len);

                // When clients hang up, the recvfrom returns 0 (EOF).
                if (ret == 0)
                    continue;

                mlfs_info("GET: %s\n", buf);

                memset(cmd_header, 0, 12);
                sscanf(buf, "|%s |%d|%u|%lu|%lu|",
                        cmd_header, &dev_id, &digest_count, &digest_blkno, &end_blkno);

                digest_arg = (struct digest_arg *)mlfs_alloc(sizeof(struct digest_arg));
                digest_arg->sock_fd = sock_fd;
                digest_arg->cli_addr = cli_addr;
                memmove(digest_arg->msg, buf, MAX_SOCK_BUF);

#ifdef CONCURRENT
                if (dev_id == 4)
                    thpool_add_work(thread_pool, handle_digest_request, (void *)digest_arg);
                else
                    //thpool_add_work(thread_pool_ssd, handle_digest_request, (void *)digest_arg);
                    thpool_add_work(thread_pool, handle_digest_request, (void *)digest_arg);
#else
                handle_digest_request((void *)digest_arg);
#endif

#ifdef MIGRATION
                /*
                   thpool_wait(thread_pool);
                   thpool_wait(thread_pool_ssd);
                   */

                //try_writeback_blocks();
                try_migrate_blocks(g_root_dev, g_ssd_dev, 0, 0);
                //try_migrate_blocks(g_root_dev, g_hdd_dev, 0);
#endif
            } else {
                mlfs_info("%s\n", "Huh?");
            }
        }
    }

    close(epfd);
}

void shutdown_fs(void)
{
    printf("Finalize FS\n");
    mlfs_mutex_destroy(&lease_lock);
    device_shutdown();
    return ;
}

#ifdef USE_SLAB
void mlfs_slab_init(uint64_t pool_size)
{
    uint8_t *pool_space;

    // Transparent huge page allocation.
    pool_space = (uint8_t *)mmap(0, pool_size, PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);

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

static void shared_memory_init(void)
{
    int ret;

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
        panic("cannot create shared memory\n");

    ret = ftruncate(shm_fd, SHM_SIZE);
    if (ret == -1)
        panic("cannot ftruncate shared memory\n");

    shm_base = (uint8_t *)mmap(SHM_START_ADDR,
            SHM_SIZE + 4096,
            PROT_READ| PROT_WRITE,
            MAP_SHARED | MAP_POPULATE | MAP_FIXED,
            shm_fd, 0);

    printf("shm mmap base %p\n", shm_base);
    if (shm_base == MAP_FAILED)
        panic("cannot map shared memory\n");

    // the first 4 KB is reserved.
    mlfs_slab_pool_shared = (ncx_slab_pool_t *)(shm_base + 4096);
    mlfs_slab_pool_shared->addr = shm_base + 4096;
    mlfs_slab_pool_shared->min_shift = 3;
    mlfs_slab_pool_shared->end = shm_base + SHM_SIZE - 4096;

    ncx_slab_init(mlfs_slab_pool_shared);

    bandwidth_consumption = (uint64_t *)shm_base;
    lru_heads = (struct list_head *)shm_base + 128;

    return;
}

void init_device_lru_list(void)
{
    int i;

    for (i = 1; i < g_n_devices + 1; i++) {
        memset(&g_lru[i], 0, sizeof(struct lru));
        INIT_LIST_HEAD(&g_lru[i].lru_head);
        g_lru_hash[i] = NULL;
    }

    return;
}

void locks_init(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&block_bitmap_mutex, &attr);
}

void init_fs(void)
{
    int i;
    const char *perf_profile;

    g_ssd_dev = 2;
    g_hdd_dev = 3;
    g_log_dev = 4;

#ifdef USE_SLAB
    mlfs_slab_init(3UL << 30);
#endif

    device_init();

    init_device_lru_list();

    shared_memory_init();
    cache_init(g_root_dev);

    locks_init();

    for (i = 0; i < g_n_devices; i++)
        sb[i] = mlfs_zalloc(sizeof(struct super_block));

    read_superblock(g_root_dev);
    read_root_inode(g_root_dev);
    balloc_init(g_root_dev, sb[g_root_dev]);

#ifdef USE_SSD
    read_superblock(g_ssd_dev);
    balloc_init(g_ssd_dev, sb[g_ssd_dev]);
#endif

#ifdef USE_HDD
    read_superblock(g_hdd_dev);
    balloc_init(g_hdd_dev, sb[g_hdd_dev]);
#endif

    memset(&g_perf_stats, 0, sizeof(kernfs_stats_t));

    inode_version_table =
        (uint16_t *)mlfs_zalloc(sizeof(uint16_t) * NINODES);

    perf_profile = getenv("MLFS_PROFILE");

    if (perf_profile)
        enable_perf_stats = 1;
    else
        enable_perf_stats = 0;

    mlfs_debug("%s\n", "LIBFS is initialized");

    thread_pool = thpool_init(1);
    // A fixed thread for using SPDK.
    thread_pool_ssd = thpool_init(1);
#ifdef FCONCURRENT
    file_digest_thread_pool = thpool_init(8);
#endif
    // lease_server
    init_lease_global();
    if (mlfs_mutex_init(&lease_lock) != 0) {
        panic("Lease mutex initialization failed");
    }

    threadpool lease_server_thread_pool  = thpool_init(1);
    thpool_add_work(lease_server_thread_pool, run_lease_server, NULL);

    wait_for_event();
}

void cache_init(uint8_t dev)
{
    int i;

    for (i = 1; i < g_n_devices + 1; i++) {
        inode_hash[i] = NULL;
        dirent_hash[i] = NULL;
    }

    pthread_spin_init(&icache_spinlock, PTHREAD_PROCESS_SHARED);
    pthread_spin_init(&dcache_spinlock, PTHREAD_PROCESS_SHARED);
}

void read_superblock(uint8_t dev)
{
    int ret;
    struct buffer_head *bh;

    bh = bh_get_sync_IO(dev, 1, BH_NO_DATA_ALLOC);
    bh->b_size = g_block_size_bytes;
    bh->b_data = mlfs_zalloc(g_block_size_bytes);

    bh_submit_read_sync_IO(bh);
    mlfs_io_wait(dev, 1);

    if (!bh)
        panic("cannot read superblock\n");

    mlfs_debug("size of superblock %ld\n", sizeof(struct disk_superblock));

    memmove(&disk_sb[dev], bh->b_data, sizeof(struct disk_superblock));

    mlfs_info("superblock: size %lu nblocks %lu ninodes %u\n"
            "[inode start %lu bmap start %lu datablock start %lu log start %lu]\n",
            disk_sb[dev].size,
            disk_sb[dev].ndatablocks,
            disk_sb[dev].ninodes,
            disk_sb[dev].inode_start,
            disk_sb[dev].bmap_start,
            disk_sb[dev].datablock_start,
            disk_sb[dev].log_start);

    sb[dev]->ondisk = &disk_sb[dev];

    sb[dev]->s_dirty_root = RB_ROOT;
    sb[dev]->last_block_allocated = 0;

    // The partition is GC unit (1 GB) in SSD.
    // disk_sb[dev].size : total # of blocks
    sb[dev]->n_partition = disk_sb[dev].size >> 18;

    if (disk_sb[dev].size % (1 << 18))
        sb[dev]->n_partition++;

    //single partitioned allocation, used for debugging.
    mlfs_info("dev %u: # of segment %u\n", dev, sb[dev]->n_partition);
    sb[dev]->num_blocks = disk_sb[dev].ndatablocks;
    sb[dev]->reserved_blocks = disk_sb[dev].datablock_start;
    sb[dev]->s_bdev = g_bdev[dev];

    mlfs_free(bh->b_data);
    bh_release(bh);
}
