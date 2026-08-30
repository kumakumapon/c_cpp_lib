#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>

#include "embed_heap/embed_heap.h"
#include "embed_heap/embed_heap.hpp"

// 組み込み環境を想定し、BSSセクションや特定セクションに静的に配置された大きなメモリ領域
static uint8_t s_embed_memory_pool[4096];

static void dump_stats(const embed_heap_t *heap) {
    embed_heap_stats_t stats{};
    embed_heap_get_stats(heap, &stats);
    std::printf("  [Stats] Total: %zu B | Used: %zu B | Free: %zu B | MaxBlock: %zu B | Allocs: %zu\n",
                stats.total_size, stats.used_size, stats.free_size, stats.max_free_block, stats.alloc_count);
}

static void print_callback(void *, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " Embedded Heap Allocator (embed_heap) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // ---------------------------------------------------------
    // 1. C API デモ
    // ---------------------------------------------------------
    std::cout << "\n[1] Initializing embed_heap with 4096 bytes static buffer...\n";
    embed_heap_t heap;
    if (!embed_heap_init(&heap, s_embed_memory_pool, sizeof(s_embed_memory_pool))) {
        std::cerr << "Failed to initialize heap!" << std::endl;
        return 1;
    }
    dump_stats(&heap);

    std::cout << "\n[2] Allocating dynamic buffers (128B, 256B, 64B with 32B alignment)...\n";
    void *buf1 = embed_heap_alloc(&heap, 128);
    void *buf2 = embed_heap_alloc(&heap, 256);
    void *buf3 = embed_heap_aligned_alloc(&heap, 64, 32);

    std::printf("  buf1 (128B)            : %p\n", buf1);
    std::printf("  buf2 (256B)            : %p\n", buf2);
    std::printf("  buf3 (64B, align 32)   : %p (Address %% 32 = %llu)\n", 
                buf3, (unsigned long long)((uintptr_t)buf3 % 32));
    dump_stats(&heap);

    std::cout << "\n[3] Dumping heap memory blocks:\n";
    embed_heap_dump(&heap, print_callback, nullptr);

    std::cout << "\n[4] Freeing middle buffer (buf2: 256B)...\n";
    embed_heap_free(&heap, buf2);
    dump_stats(&heap);

    std::cout << "\n[5] Freeing remaining buffers (buf1, buf3) -> Coalescing all blocks...\n";
    embed_heap_free(&heap, buf1);
    embed_heap_free(&heap, buf3);
    dump_stats(&heap);

    // ---------------------------------------------------------
    // 2. C++ API / STL Allocator デモ
    // ---------------------------------------------------------
    std::cout << "\n[6] C++ Heap & std::vector with Custom Allocator Demo...\n";
    {
        embed_heap::Heap cpp_heap(s_embed_memory_pool);
        using MyVec = std::vector<int, embed_heap::Allocator<int>>;
        embed_heap::Allocator<int> my_alloc(cpp_heap);

        MyVec numbers(my_alloc);
        for (int i = 1; i <= 10; ++i) {
            numbers.push_back(i * 10);
        }

        std::cout << "  std::vector elements: ";
        for (int val : numbers) {
            std::cout << val << " ";
        }
        std::cout << std::endl;

        auto cpp_stats = cpp_heap.get_stats();
        std::cout << "  Used size during vector lifecycle: " << cpp_stats.used_size << " bytes\n";
    }

    std::cout << "  After std::vector destruction:\n";
    dump_stats(&heap);

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " Demo completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
