/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Minimal Unix-like filesystem backed by an ATA PIO block device.
 */
#include <lux/fs.h>
#include <lux/ata.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LUXFS_MAGIC            0x4C555846u /* "LUXF" */
#define LUXFS_VERSION          1u
#define LUXFS_START_LBA        2048u
#define LUXFS_TOTAL_SECTORS    4096u
#define LUXFS_MAX_INODES       128u
#define LUXFS_DIRECT_BLOCKS    8u
#define LUXFS_MAX_PATH_DEPTH   8u
#define LUXFS_INVALID_BLOCK    0xFFFFFFFFu

#define LUXFS_SUPER_BLOCK          0u
#define LUXFS_INODE_BITMAP_BLOCK   1u
#define LUXFS_BLOCK_BITMAP_BLOCK   2u
#define LUXFS_INODE_TABLE_START    3u

struct luxfs_superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t start_lba;
    uint32_t total_sectors;
    uint32_t inode_table_start;
    uint32_t inode_count;
    uint32_t data_block_start;
    uint32_t data_block_count;
    uint32_t root_inode;
};

enum {
    LUXFS_NODE_FREE = 0,
    LUXFS_NODE_DIR  = 1,
    LUXFS_NODE_FILE = 2,
};

struct luxfs_inode {
    uint8_t type;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t size;
    uint32_t parent;
    uint32_t direct[LUXFS_DIRECT_BLOCKS];
    uint32_t reserved_tail[4];
};

struct luxfs_dir_record {
    uint32_t inode;
    char name[FS_NAME_MAX];
};

#define LUXFS_INODES_PER_BLOCK (ATA_SECTOR_SIZE / sizeof(struct luxfs_inode))
#define LUXFS_INODE_TABLE_BLOCKS ((LUXFS_MAX_INODES + LUXFS_INODES_PER_BLOCK - 1u) / LUXFS_INODES_PER_BLOCK)
#define LUXFS_DATA_BLOCK_START (LUXFS_INODE_TABLE_START + LUXFS_INODE_TABLE_BLOCKS)
#define LUXFS_DATA_BLOCK_COUNT (LUXFS_TOTAL_SECTORS - LUXFS_DATA_BLOCK_START)
#define LUXFS_INODE_BITMAP_BYTES ((LUXFS_MAX_INODES + 7u) / 8u)
#define LUXFS_BLOCK_BITMAP_BYTES ((LUXFS_DATA_BLOCK_COUNT + 7u) / 8u)

struct luxfs_state {
    bool mounted;
    struct luxfs_superblock super;
    struct luxfs_inode inodes[LUXFS_MAX_INODES];
    uint8_t inode_bitmap[LUXFS_INODE_BITMAP_BYTES];
    uint8_t block_bitmap[LUXFS_BLOCK_BITMAP_BYTES];
};

static struct luxfs_state g_fs;

struct dir_find_ctx {
    const char *target;
    uint32_t *result;
    bool found;
};

struct dir_emit_ctx {
    fs_dir_iter_cb cb;
    void *user_data;
};

/**
 * Copy a filename into a fixed-size buffer with truncation and NUL termination.
 *
 * If `dst` is NULL the function does nothing. If `src` is NULL the destination
 * is set to an empty string. Otherwise up to FS_NAME_MAX-1 bytes from `src`
 * are copied into `dst` and a terminating NUL is written.
 *
 * @param dst Destination buffer (must be at least FS_NAME_MAX bytes long).
 * @param src Source NUL-terminated string to copy, or NULL to write an empty name.
 */
static void luxfs_copy_name(char *dst, const char *src)
{
    if (!dst) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t len = strlen(src);
    if (len >= FS_NAME_MAX) {
        len = FS_NAME_MAX - 1u;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/**
 * Extract the basename component from a filesystem path into out_name.
 *
 * Copies the final path segment (the name after the last '/') into out_name,
 * handling empty or root paths by producing "/". Trailing slashes are ignored.
 * The result is truncated to FS_NAME_MAX - 1 bytes and always NUL-terminated.
 *
 * @param path Source path string; may be NULL or empty.
 * @param out_name Destination buffer that receives the basename; if NULL no action is taken.
 */
static void luxfs_basename(const char *path, char *out_name)
{
    if (!out_name) {
        return;
    }

    if (!path || !*path) {
        luxfs_copy_name(out_name, "/");
        return;
    }

    const char *start = path;
    const char *end = path + strlen(path);
    while (end > start && *(end - 1) == '/') {
        --end;
    }
    if (end == start) {
        luxfs_copy_name(out_name, "/");
        return;
    }

    const char *cursor = end;
    while (cursor > start && *(cursor - 1) != '/') {
        --cursor;
    }

    size_t len = (size_t)(end - cursor);
    if (len >= FS_NAME_MAX) {
        len = FS_NAME_MAX - 1u;
    }
    memcpy(out_name, cursor, len);
    out_name[len] = '\0';
}

/**
 * Compare a directory record's name against a target and record a match.
 *
 * When the record name equals the target stored in the provided context, sets
 * the context's `found` flag and, if `result` is non-NULL, writes the matched
 * inode index to `*result`.
 *
 * @param record Directory record to compare.
 * @param ctx_ptr Pointer to a `struct dir_find_ctx` containing `target`,
 *                optional `result`, and `found` fields.
 * @returns `false` to stop iteration when a match is found or the context is
 *          invalid; `true` to continue iteration otherwise.
 */
static bool luxfs_dir_find_cb(const struct luxfs_dir_record *record, void *ctx_ptr)
{
    struct dir_find_ctx *ctx = (struct dir_find_ctx *)ctx_ptr;
    if (!ctx || !ctx->target) {
        return true;
    }

    if (strcmp(record->name, ctx->target) == 0) {
        if (ctx->result) {
            *ctx->result = record->inode;
        }
        ctx->found = true;
        return false;
    }
    return true;
}

/**
 * Emit a directory entry to the provided callback for a valid directory record.
 *
 * If `ctx_ptr` or its callback is NULL, or if the record's inode index is out of
 * range or refers to a free inode, the function skips emitting and continues.
 *
 * @param record Pointer to the directory record to consider.
 * @param ctx_ptr Pointer to a `struct dir_emit_ctx` containing the callback and user data.
 * @returns `true` to indicate iteration should continue.
 */
static bool luxfs_dir_emit_cb(const struct luxfs_dir_record *record, void *ctx_ptr)
{
    struct dir_emit_ctx *ctx = (struct dir_emit_ctx *)ctx_ptr;
    if (!ctx || !ctx->cb) {
        return true;
    }
    if (record->inode >= LUXFS_MAX_INODES) {
        return true;
    }

    const struct luxfs_inode *child = &g_fs.inodes[record->inode];
    if (child->type == LUXFS_NODE_FREE) {
        return true;
    }

    struct fs_dirent entry;
    memset(&entry, 0, sizeof(entry));
    entry.is_dir = (child->type == LUXFS_NODE_DIR);
    entry.size = child->size;
    memcpy(entry.name, record->name, FS_NAME_MAX);
    ctx->cb(&entry, ctx->user_data);
    return true;
}

/**
 * Selects the smaller of two 32-bit unsigned integers.
 * @param a First value to compare.
 * @param b Second value to compare.
 * @returns The smaller of `a` and `b`.
 */
static inline uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

/**
 * Check whether a specific bit is set in a byte bitmap.
 * @param bitmap Pointer to the bitmap bytes.
 * @param index Zero-based bit index to test.
 * @returns `true` if the bit at `index` is 1, `false` otherwise.
 */
static inline bool bitmap_test(const uint8_t *bitmap, uint32_t index)
{
    return (bitmap[index / 8u] >> (index % 8u)) & 0x1u;
}

/**
 * Set or clear a bit at a given bit index within a byte-array bitmap.
 * @param bitmap Byte-array representing the bitmap where bit indices start at 0.
 * @param index Bit index to modify (0 = first bit of bitmap[0]).
 * @param value `true` to set the bit, `false` to clear the bit.
 */
static inline void bitmap_set(uint8_t *bitmap, uint32_t index, bool value)
{
    uint32_t byte = index / 8u;
    uint8_t mask = (uint8_t)(1u << (index % 8u));
    if (value) {
        bitmap[byte] |= mask;
    } else {
        bitmap[byte] &= (uint8_t)~mask;
    }
}

/**
 * Read a filesystem block from disk into a buffer.
 * @param block Logical block index relative to the filesystem start.
 * @param buffer Destination buffer; must be at least LUXFS_BLOCK_SIZE bytes.
 * @returns `true` if the block was read successfully, `false` otherwise.
 */
static bool disk_read_block(uint32_t block, void *buffer)
{
    return ata_pio_read(LUXFS_START_LBA + block, 1, buffer);
}

/**
 * Write a filesystem block to the underlying ATA device at the specified block index.
 *
 * @param block Block index within the filesystem (0 = first filesystem block).
 * @param buffer Pointer to a block-sized buffer containing the data to write.
 * @returns `true` if the block was written successfully, `false` otherwise.
 */
static bool disk_write_block(uint32_t block, const void *buffer)
{
    return ata_pio_write(LUXFS_START_LBA + block, 1, buffer);
}

/**
 * Read a filesystem data block into the provided buffer.
 * @param index Data block index within the filesystem data region (0 .. LUXFS_DATA_BLOCK_COUNT - 1).
 * @param buffer Pointer to a buffer at least LUXFS_BLOCK_SIZE bytes in size that will receive the block data.
 * @returns `true` if the block was successfully read from disk, `false` otherwise.
 */
static bool disk_read_data_block(uint32_t index, void *buffer)
{
    if (index >= LUXFS_DATA_BLOCK_COUNT) {
        return false;
    }
    return ata_pio_read(LUXFS_START_LBA + LUXFS_DATA_BLOCK_START + index, 1, buffer);
}

/**
 * Write a filesystem data block to disk.
 *
 * @param index Index of the data block within the filesystem data region.
 * @param buffer Pointer to a block-sized buffer containing the data to write.
 * @returns `true` if the block was written successfully, `false` otherwise.
 */
static bool disk_write_data_block(uint32_t index, const void *buffer)
{
    if (index >= LUXFS_DATA_BLOCK_COUNT) {
        return false;
    }
    return ata_pio_write(LUXFS_START_LBA + LUXFS_DATA_BLOCK_START + index, 1, buffer);
}

/**
 * Persist the in-memory filesystem superblock to its on-disk location.
 *
 * @returns `true` if the superblock was written successfully, `false` otherwise.
 */
static bool luxfs_flush_superblock(void)
{
    uint8_t buffer[ATA_SECTOR_SIZE];
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, &g_fs.super, sizeof(g_fs.super));
    return disk_write_block(LUXFS_SUPER_BLOCK, buffer);
}

/**
 * Writes the in-memory inode bitmap to the inode bitmap block on disk.
 *
 * @returns `true` if the inode bitmap was successfully written, `false` otherwise.
 */
static bool luxfs_flush_inode_bitmap(void)
{
    uint8_t buffer[ATA_SECTOR_SIZE];
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, g_fs.inode_bitmap, sizeof(g_fs.inode_bitmap));
    return disk_write_block(LUXFS_INODE_BITMAP_BLOCK, buffer);
}

/**
 * Write the in-memory block allocation bitmap to its on-disk bitmap block.
 *
 * @returns `true` if the bitmap was written to disk successfully, `false` otherwise.
 */
static bool luxfs_flush_block_bitmap(void)
{
    uint8_t buffer[ATA_SECTOR_SIZE];
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, g_fs.block_bitmap, sizeof(g_fs.block_bitmap));
    return disk_write_block(LUXFS_BLOCK_BITMAP_BLOCK, buffer);
}

/**
 * Flushes the inode-table block identified by block_index to disk.
 *
 * Writes the in-memory inodes that belong to the specified inode-table block
 * to the corresponding on-disk inode-table block.
 *
 * @param block_index Index of the inode-table block to flush (0..LUXFS_INODE_TABLE_BLOCKS-1).
 * @returns `true` if the block was written successfully, `false` if block_index is out of range or the write failed.
 */
static bool luxfs_flush_inode_block(uint32_t block_index)
{
    if (block_index >= LUXFS_INODE_TABLE_BLOCKS) {
        return false;
    }

    uint8_t buffer[ATA_SECTOR_SIZE];
    memset(buffer, 0, sizeof(buffer));

    uint32_t start = block_index * LUXFS_INODES_PER_BLOCK;
    uint32_t count = min_u32(LUXFS_INODES_PER_BLOCK, LUXFS_MAX_INODES - start);
    memcpy(buffer, &g_fs.inodes[start], count * sizeof(struct luxfs_inode));
    return disk_write_block(LUXFS_INODE_TABLE_START + block_index, buffer);
}

/**
 * Flushes the disk block that contains the specified inode index from the in-memory inode table.
 * @param inode_index Index of the inode whose containing inode-table block should be flushed.
 * @returns `true` if the containing inode-table block was written successfully; `false` if `inode_index` is out of range or the write failed.
 */
static bool luxfs_flush_inode(uint32_t inode_index)
{
    if (inode_index >= LUXFS_MAX_INODES) {
        return false;
    }

    uint32_t block_index = inode_index / LUXFS_INODES_PER_BLOCK;
    return luxfs_flush_inode_block(block_index);
}

/**
 * Load filesystem metadata from disk into the in-memory filesystem state.
 *
 * Reads the on-disk superblock, inode bitmap, block bitmap, and inode table blocks
 * and populates the global in-memory luxfs state (g_fs).
 *
 * @returns `true` if all metadata blocks were read and loaded successfully, `false` otherwise.
 */
static bool luxfs_load_metadata(void)
{
    uint8_t buffer[ATA_SECTOR_SIZE];

    if (!disk_read_block(LUXFS_SUPER_BLOCK, buffer)) {
        return false;
    }
    memcpy(&g_fs.super, buffer, sizeof(g_fs.super));

    if (!disk_read_block(LUXFS_INODE_BITMAP_BLOCK, buffer)) {
        return false;
    }
    memcpy(g_fs.inode_bitmap, buffer, sizeof(g_fs.inode_bitmap));

    if (!disk_read_block(LUXFS_BLOCK_BITMAP_BLOCK, buffer)) {
        return false;
    }
    memcpy(g_fs.block_bitmap, buffer, sizeof(g_fs.block_bitmap));

    for (uint32_t block = 0; block < LUXFS_INODE_TABLE_BLOCKS; ++block) {
        if (!disk_read_block(LUXFS_INODE_TABLE_START + block, buffer)) {
            return false;
        }
        uint32_t start = block * LUXFS_INODES_PER_BLOCK;
        uint32_t count = min_u32(LUXFS_INODES_PER_BLOCK, LUXFS_MAX_INODES - start);
        memcpy(&g_fs.inodes[start], buffer, count * sizeof(struct luxfs_inode));
    }

    return true;
}

/**
 * Reset an inode to the free/empty state.
 *
 * Clears type to free, sets size and parent to zero, and marks all direct
 * block pointers as invalid.
 *
 * @param inode Inode to clear; must be a valid pointer to a luxfs_inode.
 */
static void luxfs_inode_clear(struct luxfs_inode *inode)
{
    inode->type = LUXFS_NODE_FREE;
    inode->size = 0;
    inode->parent = 0;
    for (uint32_t i = 0; i < LUXFS_DIRECT_BLOCKS; ++i) {
        inode->direct[i] = LUXFS_INVALID_BLOCK;
    }
}

/**
 * Initialize and write a fresh LUXFS filesystem image to disk and set it as mounted.
 *
 * Formats in-memory filesystem structures (superblock, inode table, bitmaps, root inode),
 * persists the superblock, inode bitmap, block bitmap and all inode table blocks to disk,
 * and marks the filesystem as mounted on success.
 *
 * @returns `true` if formatting and all on-disk writes succeeded and the filesystem was marked mounted, `false` if any disk write or flush failed. 
 */
static bool luxfs_format(void)
{
    memset(&g_fs, 0, sizeof(g_fs));
    g_fs.super.magic = LUXFS_MAGIC;
    g_fs.super.version = LUXFS_VERSION;
    g_fs.super.block_size = ATA_SECTOR_SIZE;
    g_fs.super.start_lba = LUXFS_START_LBA;
    g_fs.super.total_sectors = LUXFS_TOTAL_SECTORS;
    g_fs.super.inode_table_start = LUXFS_INODE_TABLE_START;
    g_fs.super.inode_count = LUXFS_MAX_INODES;
    g_fs.super.data_block_start = LUXFS_DATA_BLOCK_START;
    g_fs.super.data_block_count = LUXFS_DATA_BLOCK_COUNT;
    g_fs.super.root_inode = 0;

    for (uint32_t i = 0; i < LUXFS_MAX_INODES; ++i) {
        luxfs_inode_clear(&g_fs.inodes[i]);
    }

    bitmap_set(g_fs.inode_bitmap, 0, true);
    struct luxfs_inode *root = &g_fs.inodes[0];
    root->type = LUXFS_NODE_DIR;
    root->parent = 0;

    if (!luxfs_flush_superblock()) {
        return false;
    }
    if (!luxfs_flush_inode_bitmap()) {
        return false;
    }
    if (!luxfs_flush_block_bitmap()) {
        return false;
    }
    for (uint32_t block = 0; block < LUXFS_INODE_TABLE_BLOCKS; ++block) {
        if (!luxfs_flush_inode_block(block)) {
            return false;
        }
    }

    g_fs.mounted = true;
    return true;
}

/**
 * Validate the in-memory superblock against the filesystem's expected constants.
 *
 * @returns `true` if all critical superblock fields (magic, version, block size,
 *          layout start/count fields and inode counts) match the expected
 *          LUXFS constants, `false` otherwise.
 */
static bool luxfs_validate_superblock(void)
{
    if (g_fs.super.magic != LUXFS_MAGIC) {
        return false;
    }
    if (g_fs.super.version != LUXFS_VERSION) {
        return false;
    }
    if (g_fs.super.block_size != ATA_SECTOR_SIZE) {
        return false;
    }
    if (g_fs.super.start_lba != LUXFS_START_LBA) {
        return false;
    }
    if (g_fs.super.total_sectors != LUXFS_TOTAL_SECTORS) {
        return false;
    }
    if (g_fs.super.inode_table_start != LUXFS_INODE_TABLE_START) {
        return false;
    }
    if (g_fs.super.inode_count != LUXFS_MAX_INODES) {
        return false;
    }
    if (g_fs.super.data_block_start != LUXFS_DATA_BLOCK_START) {
        return false;
    }
    if (g_fs.super.data_block_count != LUXFS_DATA_BLOCK_COUNT) {
        return false;
    }
    return true;
}

/**
 * Allocate a free inode, initialize it with the given type and parent, and persist inode metadata.
 *
 * @param type Inode type (one of LUXFS_NODE_FREE, LUXFS_NODE_DIR, LUXFS_NODE_FILE).
 * @param parent Index of the parent inode to set on the allocated inode.
 * @param out_index Pointer that receives the allocated inode index on success.
 * @returns `true` if a free inode was found, initialized, and its metadata flushed to disk; `false` otherwise.
 */
static bool luxfs_alloc_inode(uint8_t type, uint32_t parent, uint32_t *out_index)
{
    for (uint32_t i = 0; i < LUXFS_MAX_INODES; ++i) {
        if (!bitmap_test(g_fs.inode_bitmap, i)) {
            struct luxfs_inode *inode = &g_fs.inodes[i];
            luxfs_inode_clear(inode);
            inode->type = type;
            inode->parent = parent;
            bitmap_set(g_fs.inode_bitmap, i, true);
            if (!luxfs_flush_inode_bitmap()) {
                return false;
            }
            if (!luxfs_flush_inode(i)) {
                return false;
            }
            *out_index = i;
            return true;
        }
    }
    return false;
}

/**
 * Allocate the next free data block and persist the allocation in the block bitmap.
 *
 * Sets `*out_index` to the index of the allocated data block when successful and ensures the block bitmap is flushed to disk.
 *
 * @param out_index Pointer to receive the allocated data block index; unchanged on failure.
 * @returns `true` if a free data block was found, marked allocated, and the bitmap flushed; `false` otherwise.
 */
static bool luxfs_alloc_block(uint32_t *out_index)
{
    for (uint32_t i = 0; i < LUXFS_DATA_BLOCK_COUNT; ++i) {
        if (!bitmap_test(g_fs.block_bitmap, i)) {
            bitmap_set(g_fs.block_bitmap, i, true);
            if (!luxfs_flush_block_bitmap()) {
                return false;
            }
            *out_index = i;
            return true;
        }
    }
    return false;
}

/**
 * Release a data block and persist the block allocation bitmap.
 *
 * @param index Index of the data block to free (0-based, must be less than LUXFS_DATA_BLOCK_COUNT).
 * @returns `true` if the block was already free or was freed and the block bitmap was successfully written to disk; `false` if `index` is out of range or persistence of the bitmap failed.
 */
static bool luxfs_free_block(uint32_t index)
{
    if (index >= LUXFS_DATA_BLOCK_COUNT) {
        return false;
    }

    if (!bitmap_test(g_fs.block_bitmap, index)) {
        return true;
    }

    bitmap_set(g_fs.block_bitmap, index, false);
    return luxfs_flush_block_bitmap();
}

/**
 * Release and clear all data blocks referenced by an inode and reset its size.
 *
 * Frees each allocated direct data block referenced by `inode`, sets each direct
 * pointer to `LUXFS_INVALID_BLOCK`, and sets `inode->size` to 0.
 *
 * @param inode Inode whose data blocks should be released.
 * @returns `true` if all allocated blocks were freed successfully, `false` if
 * any block free operation failed or if `inode` is NULL.
 */
static bool luxfs_release_inode_blocks(struct luxfs_inode *inode)
{
    if (!inode) {
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0; i < LUXFS_DIRECT_BLOCKS; ++i) {
        if (inode->direct[i] != LUXFS_INVALID_BLOCK) {
            if (!luxfs_free_block(inode->direct[i])) {
                ok = false;
            }
            inode->direct[i] = LUXFS_INVALID_BLOCK;
        }
    }
    inode->size = 0;
    return ok;
}

/**
 * Mark an inode as free and persist the change to disk.
 *
 * Releases any data blocks held by the inode, resets the inode to the free state,
 * clears its bit in the inode bitmap, and flushes the inode and inode bitmap to disk.
 *
 * @param index Index of the inode to free; ignored if out of valid range.
 */
static void luxfs_mark_inode_free(uint32_t index)
{
    if (index >= LUXFS_MAX_INODES) {
        return;
    }

    struct luxfs_inode *inode = &g_fs.inodes[index];
    luxfs_release_inode_blocks(inode);
    luxfs_inode_clear(inode);
    bitmap_set(g_fs.inode_bitmap, index, false);
    luxfs_flush_inode(index);
    luxfs_flush_inode_bitmap();
}

/**
 * Iterate directory entries of a directory inode and invoke a callback for each entry.
 *
 * Iterates the serialized directory records stored in the directory inode's direct data blocks,
 * invoking `callback` for each complete `luxfs_dir_record`. Iteration stops early if the
 * callback returns `false`.
 *
 * @param dir_index Index of the directory inode to iterate.
 * @param callback Function called for each directory record; receives a pointer to the record
 *                 and the `ctx` pointer. If the callback returns `false`, iteration stops.
 * @param ctx Opaque user-provided pointer forwarded to the callback.
 * @returns `true` if iteration completed successfully or was stopped by the callback,
 *          `false` on error (invalid inode, I/O failure, invalid block layout) or if the
 *          final directory record is incomplete (partial trailing record).
 */
static bool luxfs_dir_iterate(uint32_t dir_index, bool (*callback)(const struct luxfs_dir_record *, void *), void *ctx)
{
    if (dir_index >= LUXFS_MAX_INODES) {
        return false;
    }

    const struct luxfs_inode *dir = &g_fs.inodes[dir_index];
    if (dir->type != LUXFS_NODE_DIR) {
        return false;
    }

    size_t processed = 0;
    size_t offset = 0;
    uint8_t block_buffer[ATA_SECTOR_SIZE];
    struct luxfs_dir_record record;
    size_t record_progress = 0;

    while (processed < dir->size) {
        uint32_t block_idx = (uint32_t)(offset / ATA_SECTOR_SIZE);
        size_t block_offset = offset % ATA_SECTOR_SIZE;
        if (block_idx >= LUXFS_DIRECT_BLOCKS) {
            return false;
        }
        uint32_t data_block = dir->direct[block_idx];
        if (data_block == LUXFS_INVALID_BLOCK) {
            return false;
        }
        if (!disk_read_data_block(data_block, block_buffer)) {
            return false;
        }

        size_t chunk = ATA_SECTOR_SIZE - block_offset;
        size_t remaining_bytes = dir->size - processed;
        if (chunk > remaining_bytes) {
            chunk = remaining_bytes;
        }

        size_t consumed = 0;
        while (consumed < chunk) {
            size_t copy = sizeof(struct luxfs_dir_record) - record_progress;
            if (copy > (chunk - consumed)) {
                copy = chunk - consumed;
            }

            memcpy(((uint8_t *)&record) + record_progress,
                   block_buffer + block_offset + consumed,
                   copy);

            record_progress += copy;
            consumed += copy;
            processed += copy;

            if (record_progress == sizeof(struct luxfs_dir_record)) {
                if (!callback(&record, ctx)) {
                    return true;
                }
                record_progress = 0;
            }
        }

        offset += chunk;
    }

    return record_progress == 0;
}

/**
 * Search a directory for an entry by name and obtain its inode index.
 *
 * @param dir_index Index of the directory inode to search.
 * @param name NUL-terminated name of the entry to find.
 * @param inode_index Pointer to a uint32_t that will be set to the found inode index.
 * @returns `true` if an entry with the given name was found and `*inode_index` was set, `false` otherwise.
 */
static bool luxfs_dir_find(uint32_t dir_index, const char *name, uint32_t *inode_index)
{
    if (!name || !inode_index) {
        return false;
    }

    struct dir_find_ctx ctx = {
        .target = name,
        .result = inode_index,
        .found = false
    };

    if (!luxfs_dir_iterate(dir_index, luxfs_dir_find_cb, &ctx)) {
        return false;
    }

    return ctx.found;
}

/**
 * Append a directory entry to the directory represented by the given inode.
 *
 * Appends `record` to the directory's data (growing the directory and persisting
 * changes to disk), allocating data blocks and updating the inode as needed.
 *
 * @param dir_index Index of the directory inode to append into.
 * @param record Pointer to the directory record to append; its contents are copied.
 * @returns `true` if the record was appended and the inode flushed to disk, `false` on error.
 */
static bool luxfs_dir_append_record(uint32_t dir_index, const struct luxfs_dir_record *record)
{
    if (dir_index >= LUXFS_MAX_INODES) {
        return false;
    }

    struct luxfs_inode *dir = &g_fs.inodes[dir_index];
    if (dir->type != LUXFS_NODE_DIR) {
        return false;
    }

    size_t offset = dir->size;
    size_t remaining = sizeof(*record);
    const uint8_t *src = (const uint8_t *)record;
    uint8_t block_buffer[ATA_SECTOR_SIZE];

    while (remaining) {
        uint32_t block_idx = (uint32_t)(offset / ATA_SECTOR_SIZE);
        size_t block_offset = offset % ATA_SECTOR_SIZE;
        if (block_idx >= LUXFS_DIRECT_BLOCKS) {
            return false;
        }

        if (dir->direct[block_idx] == LUXFS_INVALID_BLOCK) {
            uint32_t new_block;
            if (!luxfs_alloc_block(&new_block)) {
                return false;
            }
            uint8_t zero[ATA_SECTOR_SIZE];
            memset(zero, 0, sizeof(zero));
            if (!disk_write_data_block(new_block, zero)) {
                return false;
            }
            dir->direct[block_idx] = new_block;
        }

        if (!disk_read_data_block(dir->direct[block_idx], block_buffer)) {
            return false;
        }

        size_t chunk = ATA_SECTOR_SIZE - block_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        memcpy(block_buffer + block_offset, src, chunk);
        if (!disk_write_data_block(dir->direct[block_idx], block_buffer)) {
            return false;
        }

        src += chunk;
        remaining -= chunk;
        offset += chunk;
    }

    dir->size += sizeof(*record);
    return luxfs_flush_inode(dir_index);
}

/**
 * Split a filesystem path into its path components.
 *
 * Tokenizes `path` by '/' characters into `components`, omitting empty segments
 * and single-dot ("." ) segments, and writes the number of resulting components
 * to `*count`.
 *
 * @param path NUL-terminated path to tokenize.
 * @param components Caller-provided array to receive components; each element
 *        must be at least FS_NAME_MAX bytes. Only the first `*count` entries
 *        are written.
 * @param count Output pointer that receives the number of components written.
 *        On failure the value pointed to is not modified.
 * @returns `true` if tokenization succeeded, `false` on invalid input,
 *          when the path exceeds LUXFS_MAX_PATH_DEPTH, or when a component
 *          name would exceed FS_NAME_MAX - 1 characters.
 */
static bool luxfs_tokenize_path(const char *path, char components[][FS_NAME_MAX], size_t *count)
{
    if (!path || !count) {
        return false;
    }

    size_t depth = 0;
    size_t i = 0;

    while (path[i]) {
        while (path[i] == '/') {
            ++i;
        }
        if (!path[i]) {
            break;
        }
        if (depth >= LUXFS_MAX_PATH_DEPTH) {
            return false;
        }

        size_t len = 0;
        while (path[i] && path[i] != '/') {
            if (len >= FS_NAME_MAX - 1u) {
                return false;
            }
            components[depth][len++] = path[i++];
        }
        components[depth][len] = '\0';

        if (len == 1 && components[depth][0] == '.') {
            continue;
        }
        if (len == 0) {
            continue;
        }

        ++depth;
    }

    *count = depth;
    return true;
}

/**
 * Resolve a filesystem path to its corresponding inode index.
 *
 * Parses the path into components, traverses directories (handling the ".." parent
 * component), and stores the final inode index in `inode_index` on success.
 *
 * @param path NUL-terminated filesystem path to resolve.
 * @param inode_index Pointer to where the resolved inode index will be written.
 * @returns `true` if the path was successfully resolved and `inode_index` set, `false` otherwise.
 */
static bool luxfs_resolve(const char *path, uint32_t *inode_index)
{
    if (!inode_index) {
        return false;
    }

    char components[LUXFS_MAX_PATH_DEPTH][FS_NAME_MAX];
    size_t depth = 0;
    if (!luxfs_tokenize_path(path, components, &depth)) {
        return false;
    }

    uint32_t current = g_fs.super.root_inode;
    for (size_t i = 0; i < depth; ++i) {
        if (components[i][0] == '.' && components[i][1] == '.' && components[i][2] == '\0') {
            current = g_fs.inodes[current].parent;
            continue;
        }
        uint32_t child = 0;
        if (!luxfs_dir_find(current, components[i], &child)) {
            return false;
        }
        current = child;
    }

    *inode_index = current;
    return true;
}

/**
 * Resolve the parent directory inode for a filesystem path and extract the final path component (leaf).
 *
 * Tokenizes `path`, navigates components from the filesystem root while handling ".." to move to parent
 * directories, and sets `parent_inode` to the inode index of the parent directory containing the leaf.
 * The final path component is copied into `leaf` and NUL-terminated.
 *
 * @param path NUL-terminated filesystem path to resolve.
 * @param parent_inode Pointer to receive the parent directory inode index; must be non-NULL.
 * @param leaf Buffer to receive the final path component (basename); must be at least FS_NAME_MAX bytes.
 * @returns `true` if the parent directory was successfully resolved and `leaf` was set, `false` otherwise.
 */
static bool luxfs_resolve_parent(const char *path, uint32_t *parent_inode, char *leaf)
{
    if (!parent_inode || !leaf) {
        return false;
    }

    char components[LUXFS_MAX_PATH_DEPTH][FS_NAME_MAX];
    size_t depth = 0;
    if (!luxfs_tokenize_path(path, components, &depth)) {
        return false;
    }

    if (depth == 0) {
        return false;
    }

    memcpy(leaf, components[depth - 1u], FS_NAME_MAX);
    leaf[FS_NAME_MAX - 1u] = '\0';

    uint32_t current = g_fs.super.root_inode;
    for (size_t i = 0; i + 1u < depth; ++i) {
        if (components[i][0] == '.' && components[i][1] == '.' && components[i][2] == '\0') {
            current = g_fs.inodes[current].parent;
            continue;
        }
        uint32_t child = 0;
        if (!luxfs_dir_find(current, components[i], &child)) {
            return false;
        }
        struct luxfs_inode *inode = &g_fs.inodes[child];
        if (inode->type != LUXFS_NODE_DIR) {
            return false;
        }
        current = child;
    }

    *parent_inode = current;
    return true;
}

/**
 * Mounts and initializes the LUXFS filesystem and the underlying ATA PIO device.
 *
 * Attempts to ensure the ATA PIO subsystem is ready, load on-disk filesystem
 * metadata, and recover by formatting the filesystem if metadata is missing or
 * invalid. Safe to call repeatedly; returns success if the filesystem is already mounted.
 *
 * @returns `true` if the filesystem is mounted and ready, `false` otherwise.
 */
bool fs_mount(void)
{
    if (g_fs.mounted) {
        return true;
    }

    if (!ata_pio_ready()) {
        if (!ata_pio_init()) {
            return false;
        }
    }

    if (ata_pio_total_sectors() < (LUXFS_START_LBA + LUXFS_TOTAL_SECTORS)) {
        return false;
    }

    if (!luxfs_load_metadata()) {
        if (!luxfs_format()) {
            return false;
        }
        return true;
    }

    if (!luxfs_validate_superblock()) {
        if (!luxfs_format()) {
            return false;
        }
        return true;
    }

    g_fs.mounted = true;
    return true;
}

/**
 * Report whether the filesystem is currently mounted.
 *
 * @returns `true` if the filesystem is mounted, `false` otherwise.
 */
bool fs_ready(void)
{
    return g_fs.mounted;
}

/**
 * Ensure a regular file exists at the given filesystem path, creating it if necessary.
 *
 * Does not create intermediate directories. Rejects empty leaf names and the special
 * names "." and "..". Fails if an existing entry at the path is not a regular file
 * or if the filesystem is not mounted.
 *
 * @param path Filesystem path for the file to ensure.
 * @returns `true` if the file exists or was created successfully, `false` otherwise.
 */
bool fs_touch(const char *path)
{
    if (!fs_ready() || !path) {
        return false;
    }

    uint32_t existing = 0;
    if (luxfs_resolve(path, &existing)) {
        struct luxfs_inode *inode = &g_fs.inodes[existing];
        return inode->type == LUXFS_NODE_FILE;
    }

    uint32_t parent = 0;
    char leaf[FS_NAME_MAX];
    if (!luxfs_resolve_parent(path, &parent, leaf)) {
        return false;
    }

    if (!leaf[0] || (leaf[0] == '.' && (leaf[1] == '\0' || (leaf[1] == '.' && leaf[2] == '\0')))) {
        return false;
    }

    if (luxfs_dir_find(parent, leaf, &existing)) {
        return true;
    }

    uint32_t inode_index = 0;
    if (!luxfs_alloc_inode(LUXFS_NODE_FILE, parent, &inode_index)) {
        return false;
    }

    struct luxfs_dir_record record;
    record.inode = inode_index;
    memset(record.name, 0, sizeof(record.name));
    luxfs_copy_name(record.name, leaf);

    if (!luxfs_dir_append_record(parent, &record)) {
        luxfs_mark_inode_free(inode_index);
        return false;
    }

    return true;
}

/**
 * Create a new directory at the specified filesystem path.
 *
 * Fails if the filesystem is not ready, the path is invalid, the parent
 * directory cannot be resolved, or an entry with the same name already
 * exists. Intermediate directories are not created automatically.
 *
 * @param path Filesystem path of the directory to create.
 * @returns `true` if the directory was created successfully, `false` otherwise.
 */
bool fs_mkdir(const char *path)
{
    if (!fs_ready() || !path) {
        return false;
    }

    uint32_t existing = 0;
    if (luxfs_resolve(path, &existing)) {
        return false;
    }

    uint32_t parent = 0;
    char leaf[FS_NAME_MAX];
    if (!luxfs_resolve_parent(path, &parent, leaf)) {
        return false;
    }

    if (!leaf[0] || (leaf[0] == '.' && (leaf[1] == '\0' || (leaf[1] == '.' && leaf[2] == '\0')))) {
        return false;
    }

    if (luxfs_dir_find(parent, leaf, &existing)) {
        return false;
    }

    uint32_t inode_index = 0;
    if (!luxfs_alloc_inode(LUXFS_NODE_DIR, parent, &inode_index)) {
        return false;
    }

    struct luxfs_dir_record record;
    memset(&record, 0, sizeof(record));
    record.inode = inode_index;
    luxfs_copy_name(record.name, leaf);

    if (!luxfs_dir_append_record(parent, &record)) {
        luxfs_mark_inode_free(inode_index);
        return false;
    }

    return true;
}

/**
 * List the directory entries for a filesystem path or emit a single entry for a file.
 *
 * If `path` refers to a directory, invokes `cb` for each directory entry found.
 * If `path` refers to a file, invokes `cb` once with that file's dirent.
 *
 * @param path Path to list; if NULL the root directory is used.
 * @param cb Callback invoked for each emitted dirent; may be NULL to perform existence check only.
 * @param user_data Opaque pointer forwarded to `cb`.
 * @returns `true` if the path was successfully resolved and listing/emission completed, `false` on error (e.g., filesystem not mounted, path not found, or other failure).
 */
bool fs_list(const char *path, fs_dir_iter_cb cb, void *user_data)
{
    if (!fs_ready()) {
        return false;
    }

    const char *resolved_path = path ? path : "/";

    uint32_t inode_index = 0;
    if (!luxfs_resolve(resolved_path, &inode_index)) {
        return false;
    }

    const struct luxfs_inode *node = &g_fs.inodes[inode_index];
    if (node->type == LUXFS_NODE_FILE) {
        if (cb) {
            struct fs_dirent entry;
            memset(&entry, 0, sizeof(entry));
            entry.is_dir = false;
            entry.size = node->size;
            luxfs_basename(resolved_path, entry.name);
            cb(&entry, user_data);
        }
        return true;
    }

    if (!cb) {
        return true;
    }

    struct dir_emit_ctx ctx = {
        .cb = cb,
        .user_data = user_data
    };

    return luxfs_dir_iterate(inode_index, luxfs_dir_emit_cb, &ctx);
}

/**
 * Retrieve filesystem metadata for a given path.
 *
 * Populates the provided fs_stat structure with the node type (directory flag)
 * and size for the inode identified by the resolved path.
 *
 * @param path Path to the file or directory to stat.
 * @param out_stats Pointer to an fs_stat structure to receive the results.
 * @returns `true` if the path was resolved and out_stats was populated,
 *          `false` if the filesystem is not mounted, arguments are invalid,
 *          or the path does not exist.
 */
bool fs_stat_path(const char *path, struct fs_stat *out_stats)
{
    if (!fs_ready() || !path || !out_stats) {
        return false;
    }

    uint32_t inode_index = 0;
    if (!luxfs_resolve(path, &inode_index)) {
        return false;
    }

    const struct luxfs_inode *inode = &g_fs.inodes[inode_index];
    out_stats->is_dir = (inode->type == LUXFS_NODE_DIR);
    out_stats->size = inode->size;
    return true;
}

/**
 * Read data from a file at the given path into a caller buffer starting at a byte offset.
 *
 * Reads up to `length` bytes from the file denoted by `path`, beginning at `offset`, and copies
 * the data into `buffer`. If `bytes_read` is non-NULL it will be set to the number of bytes
 * actually copied. If `offset` is greater than or equal to the file size, no bytes are read and
 * `*bytes_read` (if provided) is set to 0 while the call still succeeds. The call fails if the
 * path does not resolve to a regular file, inputs are invalid, or underlying disk I/O fails.
 *
 * @param path Path to the target file.
 * @param offset Byte offset within the file from which to start reading.
 * @param buffer Destination buffer to receive the read data.
 * @param length Maximum number of bytes to read into `buffer`.
 * @param bytes_read Optional output that receives the number of bytes actually read.
 * @returns `true` on success, `false` on failure.
 */
bool fs_read(const char *path, size_t offset, void *buffer, size_t length, size_t *bytes_read)
{
    if (!fs_ready() || !path || !buffer) {
        return false;
    }

    uint32_t inode_index = 0;
    if (!luxfs_resolve(path, &inode_index)) {
        return false;
    }

    struct luxfs_inode *inode = &g_fs.inodes[inode_index];
    if (inode->type != LUXFS_NODE_FILE) {
        return false;
    }

    if (offset >= inode->size) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return true;
    }

    size_t total = 0;
    size_t remaining = inode->size - offset;
    if (remaining > length) {
        remaining = length;
    }

    uint8_t block_buffer[ATA_SECTOR_SIZE];

    while (remaining) {
        uint32_t block_idx = (uint32_t)(offset / ATA_SECTOR_SIZE);
        size_t block_offset = offset % ATA_SECTOR_SIZE;
        if (block_idx >= LUXFS_DIRECT_BLOCKS) {
            break;
        }
        uint32_t data_block = inode->direct[block_idx];
        if (data_block == LUXFS_INVALID_BLOCK) {
            break;
        }
        if (!disk_read_data_block(data_block, block_buffer)) {
            return false;
        }

        size_t chunk = ATA_SECTOR_SIZE - block_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        memcpy((uint8_t *)buffer + total, block_buffer + block_offset, chunk);

        total += chunk;
        remaining -= chunk;
        offset += chunk;
    }

    if (bytes_read) {
        *bytes_read = total;
    }
    return true;
}

/**
 * Write data to a file at the given path, optionally truncating the file before writing.
 *
 * If `truncate` is true the file's existing data blocks are released and size is reset to zero
 * before writing. The write is limited to LUXFS_DIRECT_BLOCKS * ATA_SECTOR_SIZE bytes and will
 * fail if `offset` or `length` would exceed that maximum. The function persists inode metadata
 * on success.
 *
 * @param path Null-terminated path to the target file.
 * @param offset Byte offset within the file at which to begin writing.
 * @param buffer Pointer to the source data to write; may be NULL only when `length` is zero.
 * @param length Number of bytes to write from `buffer`.
 * @param truncate If true, discard the file's existing contents before writing.
 * @returns `true` if the data and inode metadata were written and flushed successfully;
 *          `false` on failure (not mounted, invalid arguments, path not found, target not a file,
 *          offset/length exceed limits, allocation or I/O errors).
 */
bool fs_write(const char *path, size_t offset, const void *buffer, size_t length, bool truncate)
{
    if (!fs_ready() || !path) {
        return false;
    }

    if (length && !buffer) {
        return false;
    }

    const size_t max_size = (size_t)LUXFS_DIRECT_BLOCKS * ATA_SECTOR_SIZE;
    if (offset > max_size) {
        return false;
    }
    if (length > (max_size - offset)) {
        return false;
    }

    uint32_t inode_index = 0;
    if (!luxfs_resolve(path, &inode_index)) {
        return false;
    }

    struct luxfs_inode *inode = &g_fs.inodes[inode_index];
    if (inode->type != LUXFS_NODE_FILE) {
        return false;
    }

    if (!truncate && offset > inode->size) {
        return false;
    }

    if (truncate) {
        if (!luxfs_release_inode_blocks(inode)) {
            return false;
        }
        inode->size = 0;
    }

    if (!length) {
        return luxfs_flush_inode(inode_index);
    }

    const uint8_t *src = (const uint8_t *)buffer;
    size_t total_written = 0;
    size_t write_offset = offset;
    uint8_t block_buffer[ATA_SECTOR_SIZE];

    while (total_written < length) {
        uint32_t block_idx = (uint32_t)(write_offset / ATA_SECTOR_SIZE);
        size_t block_offset = write_offset % ATA_SECTOR_SIZE;
        if (block_idx >= LUXFS_DIRECT_BLOCKS) {
            return false;
        }

        bool new_block = false;
        if (inode->direct[block_idx] == LUXFS_INVALID_BLOCK) {
            uint32_t new_block_index = 0;
            if (!luxfs_alloc_block(&new_block_index)) {
                return false;
            }
            inode->direct[block_idx] = new_block_index;
            memset(block_buffer, 0, sizeof(block_buffer));
            new_block = true;
        }

        if (!new_block) {
            if (!disk_read_data_block(inode->direct[block_idx], block_buffer)) {
                return false;
            }
        }

        size_t chunk = ATA_SECTOR_SIZE - block_offset;
        size_t remaining = length - total_written;
        if (chunk > remaining) {
            chunk = remaining;
        }

        memcpy(block_buffer + block_offset, src + total_written, chunk);

        if (!disk_write_data_block(inode->direct[block_idx], block_buffer)) {
            return false;
        }

        total_written += chunk;
        write_offset += chunk;
    }

    if (write_offset > inode->size) {
        inode->size = write_offset;
    }

    return luxfs_flush_inode(inode_index);
}