#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>

#include "embed_heap/embed_heap.h"
#include "embed_heap/embed_heap.hpp"

// 8KBの静的メモリ領域を定義
static uint8_t g_memory_pool[8192];

void test_c_api_basic() {
    std::cout << "[Test] Running C API basic alloc/free..." << std::endl;

    embed_heap_t heap;
    bool ok = embed_heap_init(&heap, g_memory_pool, sizeof(g_memory_pool));
    assert(ok);

    embed_heap_stats_t stats{};
    embed_heap_get_stats(&heap, &stats);
    assert(stats.alloc_count == 0);
    assert(stats.used_size == 0);
    assert(stats.free_size > 0);

    // 1. 割り当て
    void *p1 = embed_heap_alloc(&heap, 128);
    assert(p1 != nullptr);
    std::memset(p1, 0xAA, 128);

    void *p2 = embed_heap_alloc(&heap, 256);
    assert(p2 != nullptr);
    std::memset(p2, 0xBB, 256);

    embed_heap_get_stats(&heap, &stats);
    assert(stats.alloc_count == 2);

    // 2. 解放 & マージ
    embed_heap_free(&heap, p1);
    embed_heap_get_stats(&heap, &stats);
    assert(stats.alloc_count == 1);

    embed_heap_free(&heap, p2);
    embed_heap_get_stats(&heap, &stats);
    assert(stats.alloc_count == 0);
    assert(stats.used_size == 0);

    std::cout << "[Test] C API basic alloc/free passed!" << std::endl;
}

void test_c_api_calloc_realloc() {
    std::cout << "[Test] Running C API calloc/realloc..." << std::endl;

    embed_heap_t heap;
    embed_heap_init(&heap, g_memory_pool, sizeof(g_memory_pool));

    // calloc
    uint8_t *p = (uint8_t *)embed_heap_calloc(&heap, 64, sizeof(uint8_t));
    assert(p != nullptr);
    for (size_t i = 0; i < 64; ++i) {
        assert(p[i] == 0);
        p[i] = (uint8_t)(i + 1);
    }

    // realloc (拡大)
    p = (uint8_t *)embed_heap_realloc(&heap, p, 128);
    assert(p != nullptr);
    for (size_t i = 0; i < 64; ++i) {
        assert(p[i] == (uint8_t)(i + 1));
    }

    // realloc (縮小)
    p = (uint8_t *)embed_heap_realloc(&heap, p, 32);
    assert(p != nullptr);
    for (size_t i = 0; i < 32; ++i) {
        assert(p[i] == (uint8_t)(i + 1));
    }

    embed_heap_free(&heap, p);
    std::cout << "[Test] C API calloc/realloc passed!" << std::endl;
}

void test_c_api_aligned_alloc() {
    std::cout << "[Test] Running C API aligned_alloc..." << std::endl;

    embed_heap_t heap;
    embed_heap_init(&heap, g_memory_pool, sizeof(g_memory_pool));

    size_t alignments[] = {8, 16, 32, 64, 128};
    for (size_t align : alignments) {
        void *p = embed_heap_aligned_alloc(&heap, 100, align);
        assert(p != nullptr);
        assert(((uintptr_t)p % align) == 0);
        embed_heap_free(&heap, p);
    }

    std::cout << "[Test] C API aligned_alloc passed!" << std::endl;
}

struct TestObject {
    int x;
    double y;
    TestObject(int a, double b) : x(a), y(b) {}
    ~TestObject() { x = 0; y = 0.0; }
};

void test_cpp_heap_and_allocator() {
    std::cout << "[Test] Running C++ Heap & Allocator..." << std::endl;

    embed_heap::Heap heap(g_memory_pool);
    assert(heap.is_initialized());

    // C++ create / destroy
    TestObject *obj = heap.create<TestObject>(42, 3.1415);
    assert(obj != nullptr);
    assert(obj->x == 42);
    assert(obj->y == 3.1415);
    heap.destroy(obj);

    // std::vector with custom allocator
    using MyVector = std::vector<int, embed_heap::Allocator<int>>;
    embed_heap::Allocator<int> alloc(heap);

    {
        MyVector vec(alloc);
        for (int i = 0; i < 100; ++i) {
            vec.push_back(i * 2);
        }

        assert(vec.size() == 100);
        for (int i = 0; i < 100; ++i) {
            assert(vec[i] == i * 2);
        }
    }

    auto stats = heap.get_stats();
    assert(stats.alloc_count == 0);
    assert(stats.used_size == 0);

    std::cout << "[Test] C++ Heap & Allocator passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_heap test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_c_api_basic();
    test_c_api_calloc_realloc();
    test_c_api_aligned_alloc();
    test_cpp_heap_and_allocator();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_heap tests PASSED successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
