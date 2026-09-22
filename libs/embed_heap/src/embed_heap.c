#include "embed_heap/embed_heap.h"
#include <string.h>
#include <stdio.h>

#define ALIGN_UP(size, align) (((size) + ((align) - 1)) & ~((align) - 1))
#define BLOCK_HEADER_SIZE     ALIGN_UP(sizeof(embed_heap_block_t), EMBED_HEAP_DEFAULT_ALIGNMENT)
#define MIN_BLOCK_PAYLOAD     (EMBED_HEAP_DEFAULT_ALIGNMENT)

static inline void heap_enter_critical(embed_heap_t *heap) {
    if (heap && heap->lock) {
        heap->lock(heap->lock_user_data);
    }
}

static inline void heap_exit_critical(embed_heap_t *heap) {
    if (heap && heap->unlock) {
        heap->unlock(heap->lock_user_data);
    }
}

bool embed_heap_init(embed_heap_t *heap, void *buffer, size_t buffer_size) {
    if (!heap || !buffer) {
        return false;
    }

    /* アライメント調整 */
    uintptr_t buf_start = (uintptr_t)buffer;
    if (buf_start > UINTPTR_MAX - (EMBED_HEAP_DEFAULT_ALIGNMENT - 1) || buffer_size > UINTPTR_MAX - buf_start) return false;
    uintptr_t aligned_start = ALIGN_UP(buf_start, EMBED_HEAP_DEFAULT_ALIGNMENT);
    size_t align_offset = (size_t)(aligned_start - buf_start);

    if (buffer_size <= align_offset + BLOCK_HEADER_SIZE + MIN_BLOCK_PAYLOAD) {
        return false; /* サイズ不足 */
    }

    size_t usable_size = buffer_size - align_offset;

    heap->buffer = (uint8_t *)aligned_start;
    heap->buffer_size = usable_size;
    heap->lock = NULL;
    heap->unlock = NULL;
    heap->lock_user_data = NULL;
    heap->total_alloc_ops = 0;
    heap->total_free_ops = 0;

    /* 初期状態: 1つの大きな空きブロック */
    embed_heap_block_t *first = (embed_heap_block_t *)heap->buffer;
    first->magic = EMBED_HEAP_MAGIC;
    first->size = usable_size - BLOCK_HEADER_SIZE;
    first->is_free = true;
    first->prev = NULL;
    first->next = NULL;

    heap->first_block = first;
    return true;
}

void embed_heap_set_lock(embed_heap_t *heap, embed_heap_lock_fn lock, embed_heap_unlock_fn unlock, void *user_data) {
    if (heap) {
        heap->lock = lock;
        heap->unlock = unlock;
        heap->lock_user_data = user_data;
    }
}

static void *alloc_internal(embed_heap_t *heap, size_t size, size_t alignment) {
    if (!heap || !heap->first_block || size == 0) {
        return NULL;
    }

    if (alignment < EMBED_HEAP_DEFAULT_ALIGNMENT) {
        alignment = EMBED_HEAP_DEFAULT_ALIGNMENT;
    }
    
    /* alignment must be power of 2 */
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    if (size > SIZE_MAX - (EMBED_HEAP_DEFAULT_ALIGNMENT - 1)) return NULL;
    size_t needed_size = ALIGN_UP(size, EMBED_HEAP_DEFAULT_ALIGNMENT);
    if (needed_size < MIN_BLOCK_PAYLOAD) {
        needed_size = MIN_BLOCK_PAYLOAD;
    }

    embed_heap_block_t *curr = heap->first_block;
    while (curr) {
        if (curr->magic != EMBED_HEAP_MAGIC) {
            return NULL;
        }

        if (curr->is_free) {
            uintptr_t block_addr = (uintptr_t)curr;
            uintptr_t payload_addr = block_addr + BLOCK_HEADER_SIZE;
            if (payload_addr > UINTPTR_MAX - (alignment - 1)) return NULL;
            uintptr_t aligned_payload = ALIGN_UP(payload_addr, alignment);

            size_t pad_needed = 0;
            if (aligned_payload != payload_addr) {
                if (aligned_payload < payload_addr + BLOCK_HEADER_SIZE + MIN_BLOCK_PAYLOAD) {
                    if (payload_addr > UINTPTR_MAX - BLOCK_HEADER_SIZE - MIN_BLOCK_PAYLOAD ||
                        payload_addr + BLOCK_HEADER_SIZE + MIN_BLOCK_PAYLOAD > UINTPTR_MAX - (alignment - 1)) return NULL;
                    aligned_payload = ALIGN_UP(payload_addr + BLOCK_HEADER_SIZE + MIN_BLOCK_PAYLOAD, alignment);
                }
                pad_needed = (size_t)(aligned_payload - payload_addr);
            }

            if (pad_needed <= curr->size && needed_size <= curr->size - pad_needed) {
                /* アライメントパディングが必要な場合、前方に空きブロックを作成 */
                if (pad_needed > 0) {
                    embed_heap_block_t *pad_block = curr;
                    embed_heap_block_t *new_block = (embed_heap_block_t *)(aligned_payload - BLOCK_HEADER_SIZE);
                    embed_heap_block_t *orig_next = pad_block->next;
                    size_t orig_size = curr->size;

                    pad_block->size = pad_needed - BLOCK_HEADER_SIZE;
                    pad_block->is_free = true;

                    new_block->magic = EMBED_HEAP_MAGIC;
                    new_block->size = orig_size - pad_needed;
                    new_block->is_free = true;
                    new_block->prev = pad_block;
                    new_block->next = orig_next;

                    if (orig_next) {
                        orig_next->prev = new_block;
                    }
                    pad_block->next = new_block;

                    curr = new_block;
                }

                /* 分割判定 */
                size_t remaining = curr->size - needed_size;
                if (remaining >= BLOCK_HEADER_SIZE + MIN_BLOCK_PAYLOAD) {
                    embed_heap_block_t *next_block = (embed_heap_block_t *)((uint8_t *)curr + BLOCK_HEADER_SIZE + needed_size);
                    embed_heap_block_t *orig_curr_next = curr->next;

                    next_block->magic = EMBED_HEAP_MAGIC;
                    next_block->size = remaining - BLOCK_HEADER_SIZE;
                    next_block->is_free = true;
                    next_block->prev = curr;
                    next_block->next = orig_curr_next;

                    if (orig_curr_next) {
                        orig_curr_next->prev = next_block;
                    }
                    curr->next = next_block;
                    curr->size = needed_size;
                }

                curr->is_free = false;
                heap->total_alloc_ops++;
                return (void *)((uint8_t *)curr + BLOCK_HEADER_SIZE);
            }
        }
        curr = curr->next;
    }

    return NULL; /* メモリ不足 */
}

void *embed_heap_alloc(embed_heap_t *heap, size_t size) {
    heap_enter_critical(heap);
    void *ptr = alloc_internal(heap, size, EMBED_HEAP_DEFAULT_ALIGNMENT);
    heap_exit_critical(heap);
    return ptr;
}

void *embed_heap_aligned_alloc(embed_heap_t *heap, size_t size, size_t alignment) {
    heap_enter_critical(heap);
    void *ptr = alloc_internal(heap, size, alignment);
    heap_exit_critical(heap);
    return ptr;
}

void *embed_heap_calloc(embed_heap_t *heap, size_t num, size_t size) {
    size_t total = num * size;
    if (num != 0 && total / num != size) {
        return NULL; /* オーバーフロー */
    }

    void *ptr = embed_heap_alloc(heap, total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

static void free_internal(embed_heap_t *heap, void *ptr) {
    if (!heap || !ptr) {
        return;
    }

    /* ポインタ範囲チェック */
    uint8_t *p = (uint8_t *)ptr;
    if (p < heap->buffer || p >= heap->buffer + heap->buffer_size) {
        return; /* ヒープ範囲外 */
    }

    embed_heap_block_t *block = (embed_heap_block_t *)(p - BLOCK_HEADER_SIZE);
    if (block->magic != EMBED_HEAP_MAGIC || block->is_free) {
        return; /* 不正ポインタまたは二重解放 */
    }

    block->is_free = true;
    heap->total_free_ops++;

    /* 次のブロックとマージ可能ならマージ */
    if (block->next && block->next->is_free) {
        embed_heap_block_t *next = block->next;
        block->size += BLOCK_HEADER_SIZE + next->size;
        block->next = next->next;
        if (next->next) {
            next->next->prev = block;
        }
        next->magic = 0; /* 無効化 */
    }

    /* 前のブロックとマージ可能ならマージ */
    if (block->prev && block->prev->is_free) {
        embed_heap_block_t *prev = block->prev;
        prev->size += BLOCK_HEADER_SIZE + block->size;
        prev->next = block->next;
        if (block->next) {
            block->next->prev = prev;
        }
        block->magic = 0; /* 無効化 */
    }
}

void embed_heap_free(embed_heap_t *heap, void *ptr) {
    heap_enter_critical(heap);
    free_internal(heap, ptr);
    heap_exit_critical(heap);
}

void *embed_heap_realloc(embed_heap_t *heap, void *ptr, size_t new_size) {
    if (!heap || new_size > SIZE_MAX - (EMBED_HEAP_DEFAULT_ALIGNMENT - 1)) return NULL;
    if (!ptr) {
        return embed_heap_alloc(heap, new_size);
    }
    if (new_size == 0) {
        embed_heap_free(heap, ptr);
        return NULL;
    }

    heap_enter_critical(heap);

    uint8_t *p = (uint8_t *)ptr;
    if (p < heap->buffer || p >= heap->buffer + heap->buffer_size) {
        heap_exit_critical(heap);
        return NULL;
    }

    embed_heap_block_t *block = (embed_heap_block_t *)(p - BLOCK_HEADER_SIZE);
    if (block->magic != EMBED_HEAP_MAGIC || block->is_free) {
        heap_exit_critical(heap);
        return NULL;
    }

    size_t needed_size = ALIGN_UP(new_size, EMBED_HEAP_DEFAULT_ALIGNMENT);
    if (needed_size < MIN_BLOCK_PAYLOAD) {
        needed_size = MIN_BLOCK_PAYLOAD;
    }

    /* サイズ縮小の場合 */
    if (block->size >= needed_size) {
        size_t remaining = block->size - needed_size;
        if (remaining >= BLOCK_HEADER_SIZE + MIN_BLOCK_PAYLOAD) {
            embed_heap_block_t *next_block = (embed_heap_block_t *)((uint8_t *)block + BLOCK_HEADER_SIZE + needed_size);
            next_block->magic = EMBED_HEAP_MAGIC;
            next_block->size = remaining - BLOCK_HEADER_SIZE;
            next_block->is_free = true;
            next_block->prev = block;
            next_block->next = block->next;

            if (block->next) {
                block->next->prev = next_block;
            }
            block->next = next_block;
            block->size = needed_size;

            /* 後続空きブロックとマージ */
            if (next_block->next && next_block->next->is_free) {
                embed_heap_block_t *succ = next_block->next;
                next_block->size += BLOCK_HEADER_SIZE + succ->size;
                next_block->next = succ->next;
                if (succ->next) {
                    succ->next->prev = next_block;
                }
                succ->magic = 0;
            }
        }
        heap_exit_critical(heap);
        return ptr;
    }

    /* サイズ拡大: 後続ブロックが空きで結合可能かチェック */
    if (block->next && block->next->is_free) {
        size_t combined = block->size + BLOCK_HEADER_SIZE + block->next->size;
        if (combined >= needed_size) {
            embed_heap_block_t *succ = block->next;
            block->next = succ->next;
            if (succ->next) {
                succ->next->prev = block;
            }
            block->size = combined;
            succ->magic = 0;

            /* 余りがあれば再分割 */
            size_t remaining = block->size - needed_size;
            if (remaining >= BLOCK_HEADER_SIZE + MIN_BLOCK_PAYLOAD) {
                embed_heap_block_t *next_block = (embed_heap_block_t *)((uint8_t *)block + BLOCK_HEADER_SIZE + needed_size);
                next_block->magic = EMBED_HEAP_MAGIC;
                next_block->size = remaining - BLOCK_HEADER_SIZE;
                next_block->is_free = true;
                next_block->prev = block;
                next_block->next = block->next;

                if (block->next) {
                    block->next->prev = next_block;
                }
                block->next = next_block;
                block->size = needed_size;
            }

            heap_exit_critical(heap);
            return ptr;
        }
    }

    /* 別領域を確保してコピー */
    void *new_ptr = alloc_internal(heap, new_size, EMBED_HEAP_DEFAULT_ALIGNMENT);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size < new_size ? block->size : new_size);
        free_internal(heap, ptr);
    }

    heap_exit_critical(heap);
    return new_ptr;
}

bool embed_heap_get_stats(const embed_heap_t *heap, embed_heap_stats_t *stats) {
    if (!heap || !stats) {
        return false;
    }

    memset(stats, 0, sizeof(embed_heap_stats_t));
    stats->total_size = heap->buffer_size;
    stats->total_alloc_ops = heap->total_alloc_ops;
    stats->total_free_ops = heap->total_free_ops;

    const embed_heap_block_t *curr = heap->first_block;
    while (curr) {
        if (curr->magic != EMBED_HEAP_MAGIC) {
            return false;
        }

        stats->overhead_size += BLOCK_HEADER_SIZE;
        if (curr->is_free) {
            stats->free_size += curr->size;
            if (curr->size > stats->max_free_block) {
                stats->max_free_block = curr->size;
            }
        } else {
            stats->used_size += curr->size;
            stats->alloc_count++;
        }
        curr = curr->next;
    }

    return true;
}

void embed_heap_dump(const embed_heap_t *heap, embed_heap_dump_printer printer, void *user_data) {
    if (!heap || !printer) {
        return;
    }

    printer(user_data, "=== EmbedHeap Dump ===\n");
    printer(user_data, "Buffer: %p - %p (Total %zu bytes)\n",
            (void *)heap->buffer, (void *)(heap->buffer + heap->buffer_size), heap->buffer_size);

    int idx = 0;
    const embed_heap_block_t *curr = heap->first_block;
    while (curr) {
        printer(user_data, "  [%03d] Addr: %p | Header: %zu B | Payload: %6zu B | %s\n",
                idx++,
                (const void *)curr,
                BLOCK_HEADER_SIZE,
                curr->size,
                curr->is_free ? "FREE" : "USED");
        curr = curr->next;
    }
    printer(user_data, "======================\n");
}
