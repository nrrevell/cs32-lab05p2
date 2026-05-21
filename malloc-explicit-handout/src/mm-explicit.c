/*
 * mm-explicit.c - The best malloc package EVAR!
 *
 * TODO (bug): Uh..this is an implicit list???
 */

#include <stdint.h>
#include "mm.h"
#include "memlib.h"
#include <string.h>
#include <stdio.h>

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);
static const size_t MIN_FREE_BLOCK_SIZE = 4 * sizeof(size_t);

/** The layout of each block allocated on the heap */
typedef struct block_t block_t;
    struct block_t {
        size_t header;
        union {
            struct {
                block_t *next;
                block_t *prev;
            };
        int8_t payload[0];
        };        
    };
/** The first and last blocks on the heap */
static block_t *mm_heap_first = NULL;
static block_t *mm_heap_last = NULL;
static block_t *free_list_head = NULL;
static bool isInFreeList(block_t *block) {
    block_t *traverse = free_list_head;
    while (traverse) {
        if (traverse == block) {
            return true;
        }
        traverse = traverse->next;
    }
    return false;
}
void list_add(block_t *block) {
    if (isInFreeList(block)) return;
    if (!block) return;

    block->next = free_list_head;
    block->prev = NULL;

    if (free_list_head) {
        free_list_head->prev = block;
    }

    free_list_head = block;
}

void list_remove(block_t *block) {
    if (block == NULL) {
        return;
    }

    if (block->prev != NULL) {
        block->prev->next = block->next;
    } else {
        free_list_head = block->next;
    }

    if (block->next != NULL) {
        block->next->prev = block->prev;
    }

    block->next = NULL;
    block->prev = NULL;
}

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}
/** Extracts a block's size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}
static void set_footer(block_t *block, size_t size, bool allocated) {
    size_t *footer = (size_t *)((char *)block + size - sizeof(size_t));
    *footer = size | allocated;
}
/*static size_t *get_footer(block_t *block) {
    return (size_t *)((char *)block + get_size(block) - sizeof(size_t));
}*/
/** Set's a block's header with the given size and allocation state */
static void set_header(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
    set_footer(block, size, is_allocated);
}
static block_t* get_prev_block(block_t *block) {
    if ((char *)block <= (char *)mm_heap_first){
        return NULL;
    } 
    size_t *prev_footer = (size_t *)((char *)block - sizeof(size_t));
    size_t prev_size = *prev_footer & ~1;

    return (block_t *)((char *)block - prev_size);
}

/** Extracts a block's allocation state from its header */
static bool is_allocated(block_t *block) {
    return block->header & 1;
}




/**
 * Finds the first free block in the heap with at least the given size.
 * If no block is large enough, returns NULL.
 */
static block_t *find_fit(size_t size) {
    // Traverse the blocks in the heap using the implicit list
    block_t *traverse = free_list_head;
    while(traverse != NULL) {
        // If the block is free and large enough for the allocation, return it
        if (!is_allocated(traverse) && get_size(traverse) >= size) {
            return traverse;
        }
        traverse = traverse->next;
    }
    return NULL;
}

/** Gets the header corresponding to a given payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - offsetof(block_t, payload);
}


/**
 * mm_init - Initializes the allocator state
 */
bool mm_init(void) {
    // We want the first payload to start at ALIGNMENT bytes from the start of the heap
    void *padding = mem_sbrk(ALIGNMENT - sizeof(size_t));
    if (padding == (void *) -1) {
        return false;
    }

    // Initialize the heap with no blocks
    mm_heap_first = NULL;
    mm_heap_last = NULL;
    free_list_head = NULL;
    return true;
}
/**
 * mm_malloc - Allocates a block with the given size
 */
void *mm_malloc(size_t size) {
    // The block must have enough space for a header and be 16-byte aligned
    size_t asize = round_up(2 * sizeof(size_t) + size, ALIGNMENT);

    // If there is a large enough free block, use it
    block_t *block = find_fit(asize);
    if (block != NULL) {
        size_t block_size = get_size(block);
        size_t remainder_size = block_size - asize;

        list_remove(block);

        if (remainder_size >= MIN_FREE_BLOCK_SIZE) {
            block_t *remainder = (block_t *)((char *)block + asize);

            set_header(remainder, remainder_size, false);
            remainder->next = NULL;
            remainder->prev = NULL;

            if (block == mm_heap_last) {
                mm_heap_last = remainder;
            }

            list_add(remainder);
            block_size = asize;
        }

        // Use the whole free block when the remainder would be too small to track.
        set_header(block, block_size, true);
        return block->payload;        
    }

    // Otherwise, a new block needs to be allocated at the end of the heap
    block = mem_sbrk(asize);
    if (block == (void *) -1) {
        return NULL;
    }

    // Update mm_heap_first and mm_heap_last since we extended the heap
    if (mm_heap_first == NULL) {
        mm_heap_first = block;
    }
    mm_heap_last = block;

    // Initialize the block with the allocated size
    set_header(block, asize, true);
    return block->payload;
}

/**
 * mm_free - Releases a block to be reused for future allocations
 */
void mm_free(void *ptr) {
    if (!ptr) return;

    block_t *block = block_from_payload(ptr);
    size_t size = get_size(block);
    char *heap_end = (char *)mem_heap_hi() + 1;

    block_t *prev = get_prev_block(block);
    block_t *next = (block_t *)((char *)block + size);

    bool prev_valid = prev && (char *)prev >= (char *)mm_heap_first;
    bool next_valid = (char *)next < heap_end;

    bool prev_free = prev_valid && !is_allocated(prev);
    bool next_free = next_valid && !is_allocated(next);

    if (next_free) {
        if (isInFreeList(next)) list_remove(next);
        size += get_size(next);
    }

    if (prev_free) {
        if (isInFreeList(prev)) list_remove(prev);
        size += get_size(prev);
        block = prev;
    }

    if ((char *)block + size == heap_end) {
        mm_heap_last = block;
    }

    set_header(block, size, false);
    list_add(block);
}

/**
 * mm_realloc - Change the size of the block by mm_mallocing a new block,
 *      copying its data, and mm_freeing the old block.
 */
void *mm_realloc(void *old_ptr, size_t size) {
    if (old_ptr == NULL) {
        return mm_malloc(size);
    }
    if (size == 0) {
        mm_free(old_ptr);
        return NULL;
    }   

    void* new_ptr = mm_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    block_t *old_block = block_from_payload(old_ptr);
    size_t old_payload_size = get_size(old_block) - 2 * sizeof(size_t);
    if (old_payload_size < size) {
        size = old_payload_size;
    }
    memcpy(new_ptr, old_ptr, size);
    mm_free(old_ptr);
    return new_ptr;
}

/**
 * mm_calloc - Allocate the block and set it to zero.
 */
void *mm_calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    if (nmemb > (SIZE_MAX / size)) {
        return NULL;
    }
    size_t total = nmemb * size;
    void* new_ptr = mm_malloc(total);
    if (new_ptr == NULL) {
        return NULL;
    }
    memset(new_ptr, 0, total);
    return new_ptr;
}

/**
 * mm_checkheap - So simple, it doesn't need a checker!
 */
void mm_checkheap(void) {

}
