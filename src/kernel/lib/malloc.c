/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Simple first-fit allocator serving malloc/free for the kernel.
 */
#include <lux/memory.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define KERNEL_HEAP_SIZE (64 * 1024)
#define ALIGNMENT 8u

struct block_header {
    size_t size; /* bytes in payload portion */
    struct block_header *next;
    struct block_header *prev;
    bool free;
};

typedef struct block_header block_header_t;

#define MIN_SPLIT (sizeof(block_header_t) + ALIGNMENT)

static uint8_t kernel_heap[KERNEL_HEAP_SIZE] __attribute__((aligned(ALIGNMENT)));
static block_header_t *heap_head;
static bool heap_ready;

static size_t align_up(size_t size)
{
    const size_t mask = ALIGNMENT - 1u;
    return (size + mask) & ~mask;
}

static bool pointer_in_heap(const void *ptr)
{
    uintptr_t start = (uintptr_t)kernel_heap;
    uintptr_t end = start + KERNEL_HEAP_SIZE;
    uintptr_t addr = (uintptr_t)ptr;
    return addr > start && addr < end;
}

static void split_block(block_header_t *block, size_t payload_size)
{
    if (payload_size >= block->size) {
        return;
    }

    size_t remaining = block->size - payload_size;
    if (remaining <= MIN_SPLIT) {
        return;
    }

    uint8_t *payload = (uint8_t *)(block + 1);
    block_header_t *new_block = (block_header_t *)(payload + payload_size);

    new_block->size = remaining - sizeof(block_header_t);
    new_block->free = true;
    new_block->prev = block;
    new_block->next = block->next;
    if (new_block->next) {
        new_block->next->prev = new_block;
    }

    block->size = payload_size;
    block->next = new_block;
}

static void coalesce(block_header_t *block)
{
    if (block->next && block->next->free) {
        block_header_t *next = block->next;
        block->size += sizeof(block_header_t) + next->size;
        block->next = next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    if (block->prev && block->prev->free) {
        block_header_t *prev = block->prev;
        prev->size += sizeof(block_header_t) + block->size;
        prev->next = block->next;
        if (block->next) {
            block->next->prev = prev;
        }
        block = prev;
    }
}

static block_header_t *find_block(size_t size)
{
    block_header_t *current = heap_head;
    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return 0;
}

void heap_init(void)
{
    if (heap_ready) {
        return;
    }

    heap_head = (block_header_t *)kernel_heap;
    heap_head->size = KERNEL_HEAP_SIZE - sizeof(block_header_t);
    heap_head->next = 0;
    heap_head->prev = 0;
    heap_head->free = true;
    heap_ready = true;
}

void *malloc(size_t size)
{
    if (!size) {
        return 0;
    }

    if (!heap_ready) {
        heap_init();
    }

    size_t aligned = align_up(size);
    block_header_t *block = find_block(aligned);
    if (!block) {
        return 0;
    }

    split_block(block, aligned);
    block->free = false;
    return (void *)(block + 1);
}

void free(void *ptr)
{
    if (!ptr || !heap_ready || !pointer_in_heap(ptr)) {
        return;
    }

    block_header_t *block = ((block_header_t *)ptr) - 1;
    if (block->free) {
        return;
    }

    block->free = true;
    coalesce(block);
}

/**
 * Allocate and zero-initialize an array of `count` elements each of `size` bytes.
 *
 * @param count Number of elements to allocate.
 * @param size  Size in bytes of each element.
 * @returns Pointer to the allocated, zeroed memory on success; `NULL` if `count` or `size` is zero, if `count * size` would overflow, or if allocation fails.
 */
void *calloc(size_t count, size_t size)
{
    if (!count || !size) {
        return 0;
    }

    size_t total = count * size;
    if (count != 0 && total / count != size) {
        return 0;
    }

    void *ptr = malloc(total);
    if (!ptr) {
        return 0;
    }

    memset(ptr, 0, total);
    return ptr;
}

/**
 * Populate heap usage statistics for the kernel heap.
 *
 * Fills the provided heap_stats structure with:
 * - total_bytes: total payload bytes available in the heap
 * - used_bytes: sum of payload bytes in allocated blocks
 * - free_bytes: sum of payload bytes in free blocks
 * - largest_free_block: size of the largest free payload block
 * - allocation_count: number of allocated blocks
 * - free_block_count: number of free blocks
 *
 * If the heap is not initialized, the function returns baseline values that represent
 * a single free block spanning the entire heap payload.
 *
 * @param stats Pointer to a heap_stats structure to populate; must not be NULL.
 * @returns true on success, false if `stats` is NULL.
 */
bool heap_get_stats(struct heap_stats *stats)
{
    if (!stats) {
        return false;
    }

    size_t total_payload = KERNEL_HEAP_SIZE - sizeof(block_header_t);

    if (!heap_ready || !heap_head) {
        stats->total_bytes = total_payload;
        stats->used_bytes = 0;
        stats->free_bytes = total_payload;
        stats->largest_free_block = total_payload;
        stats->allocation_count = 0;
        stats->free_block_count = 1;
        return true;
    }

    size_t used = 0;
    size_t free = 0;
    size_t largest_free = 0;
    size_t allocated_blocks = 0;
    size_t free_blocks = 0;

    block_header_t *current = heap_head;
    while (current) {
        if (current->free) {
            free += current->size;
            ++free_blocks;
            if (current->size > largest_free) {
                largest_free = current->size;
            }
        } else {
            used += current->size;
            ++allocated_blocks;
        }
        current = current->next;
    }

    stats->total_bytes = total_payload;
    stats->used_bytes = used;
    stats->free_bytes = free;
    stats->largest_free_block = largest_free;
    stats->allocation_count = allocated_blocks;
    stats->free_block_count = free_blocks;
    return true;
}