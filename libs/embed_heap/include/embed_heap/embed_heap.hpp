#pragma once
#include "embed_heap.h"

#include <cstddef>
#include <new>
#include <utility>
#include <type_traits>
#include <iostream>

namespace embed_heap {

/**
 * @brief C++ RAII / オブジェクト指向スタイルのヒープマネージャクラス
 */
class Heap {
public:
    Heap() : initialized_(false) {
        heap_.buffer = nullptr;
        heap_.buffer_size = 0;
        heap_.first_block = nullptr;
        heap_.lock = nullptr;
        heap_.unlock = nullptr;
        heap_.lock_user_data = nullptr;
        heap_.total_alloc_ops = 0;
        heap_.total_free_ops = 0;
    }

    Heap(void *buffer, size_t size) : Heap() {
        init(buffer, size);
    }

    /**
     * @brief バッファ配列から自動サイズ取得して初期化
     */
    template <size_t N>
    explicit Heap(uint8_t (&buffer)[N]) : Heap() {
        init(buffer, N);
    }

    ~Heap() = default;

    // Non-copyable
    Heap(const Heap &) = delete;
    Heap &operator=(const Heap &) = delete;

    bool init(void *buffer, size_t size) {
        initialized_ = embed_heap_init(&heap_, buffer, size);
        return initialized_;
    }

    void set_lock(embed_heap_lock_fn lock, embed_heap_unlock_fn unlock, void *user_data = nullptr) {
        embed_heap_set_lock(&heap_, lock, unlock, user_data);
    }

    void *alloc(size_t size) {
        return embed_heap_alloc(&heap_, size);
    }

    void *aligned_alloc(size_t size, size_t alignment) {
        return embed_heap_aligned_alloc(&heap_, size, alignment);
    }

    void *calloc(size_t num, size_t size) {
        return embed_heap_calloc(&heap_, num, size);
    }

    void *realloc(void *ptr, size_t new_size) {
        return embed_heap_realloc(&heap_, ptr, new_size);
    }

    void free(void *ptr) {
        embed_heap_free(&heap_, ptr);
    }

    /**
     * @brief オブジェクトを構築して割り当て (newの代用)
     */
    template <typename T, typename... Args>
    T *create(Args &&...args) {
        void *mem = aligned_alloc(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    /**
     * @brief オブジェクトのデストラクタを呼び出して解放 (deleteの代用)
     */
    template <typename T>
    void destroy(T *ptr) {
        if (ptr) {
            ptr->~T();
            free(ptr);
        }
    }

    embed_heap_stats_t get_stats() const {
        embed_heap_stats_t stats{};
        embed_heap_get_stats(&heap_, &stats);
        return stats;
    }

    embed_heap_t *raw_handle() { return &heap_; }
    const embed_heap_t *raw_handle() const { return &heap_; }
    bool is_initialized() const { return initialized_; }

private:
    embed_heap_t heap_;
    bool initialized_;
};

/**
 * @brief C++ STLコンテナ (std::vector, std::list等) で使えるアロケータ
 */
template <typename T>
class Allocator {
public:
    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = Allocator<U>;
    };

    explicit Allocator(Heap &heap) noexcept : heap_(&heap) {}
    explicit Allocator(embed_heap_t *raw_heap) noexcept : heap_raw_(raw_heap), heap_(nullptr) {}

    template <typename U>
    Allocator(const Allocator<U> &other) noexcept
        : heap_(other.heap_), heap_raw_(other.heap_raw_) {}

    T *allocate(std::size_t n) {
        size_t bytes = n * sizeof(T);
        void *p = nullptr;
        if (heap_) {
            p = heap_->aligned_alloc(bytes, alignof(T));
        } else if (heap_raw_) {
            p = embed_heap_aligned_alloc(heap_raw_, bytes, alignof(T));
        }
        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<T *>(p);
    }

    void deallocate(T *p, std::size_t) noexcept {
        if (heap_) {
            heap_->free(p);
        } else if (heap_raw_) {
            embed_heap_free(heap_raw_, p);
        }
    }

    template <typename U>
    bool operator==(const Allocator<U> &rhs) const noexcept {
        return (heap_ == rhs.heap_) && (heap_raw_ == rhs.heap_raw_);
    }

    template <typename U>
    bool operator!=(const Allocator<U> &rhs) const noexcept {
        return !(*this == rhs);
    }

    Heap *heap_{nullptr};
    embed_heap_t *heap_raw_{nullptr};
};

} // namespace embed_heap
