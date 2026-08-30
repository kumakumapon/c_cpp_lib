#pragma once
#ifndef EMBED_HEAP_H
#define EMBED_HEAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief アライメントのデフォルト値 (CPUアーキテクチャ依存、通常4または8バイト)
 */
#ifndef EMBED_HEAP_DEFAULT_ALIGNMENT
#define EMBED_HEAP_DEFAULT_ALIGNMENT (sizeof(void*))
#endif

/**
 * @brief デバッグ用マジックナンバー
 */
#define EMBED_HEAP_MAGIC 0x45484541 /* "EHEA" */

/**
 * @brief 排他制御用コールバック関数型
 */
typedef void (*embed_heap_lock_fn)(void *user_data);
typedef void (*embed_heap_unlock_fn)(void *user_data);

/**
 * @brief ヒープ統計情報
 */
typedef struct {
    size_t total_size;       /**< 管理領域全体のサイズ (バイト) */
    size_t used_size;        /**< 現在割り当て中のユーザーデータサイズ (バイト) */
    size_t free_size;        /**< 現在の空き合計サイズ (バイト) */
    size_t overhead_size;    /**< 管理ヘッダやアライメントによるオーバーヘッド (バイト) */
    size_t max_free_block;   /**< 現在確保可能な最大の連続空きブロックサイズ (バイト) */
    size_t alloc_count;      /**< 現在のアロケーション数 */
    size_t total_alloc_ops;  /**< 累計アロケーション実行回数 */
    size_t total_free_ops;   /**< 累計解放実行回数 */
} embed_heap_stats_t;

/**
 * @brief ブロックヘッダ構造体 (内部管理用)
 */
typedef struct embed_heap_block {
    uint32_t magic;                 /**< 破損検知用マジックナンバー */
    size_t size;                    /**< このブロックのデータ部サイズ (アライメント含む) */
    bool is_free;                   /**< 空きブロックかどうか */
    struct embed_heap_block *prev;  /**< 前の物理ブロック */
    struct embed_heap_block *next;  /**< 次の物理ブロック */
} embed_heap_block_t;

/**
 * @brief ヒープ管理構造体
 */
typedef struct {
    uint8_t *buffer;                /**< 管理対象のバッファ先頭 */
    size_t buffer_size;             /**< バッファの総バイト数 */
    embed_heap_block_t *first_block;/**< 最初のブロック */
    
    /* 排他制御 */
    embed_heap_lock_fn lock;
    embed_heap_unlock_fn unlock;
    void *lock_user_data;

    /* 統計 */
    size_t total_alloc_ops;
    size_t total_free_ops;
} embed_heap_t;

/**
 * @brief ヒープを初期化します。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param buffer メモリ領域の先頭ポインタ (静的配列やSRAM領域など)
 * @param buffer_size bufferのバイトサイズ
 * @return true 初期化成功
 * @return false パラメータ不正またはサイズ不足
 */
bool embed_heap_init(embed_heap_t *heap, void *buffer, size_t buffer_size);

/**
 * @brief スレッドセーフ/割り込み保護用のロックコールバックを設定します。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param lock ロック取得関数
 * @param unlock ロック解除関数
 * @param user_data コールバックに渡されるユーザーポインタ
 */
void embed_heap_set_lock(embed_heap_t *heap, embed_heap_lock_fn lock, embed_heap_unlock_fn unlock, void *user_data);

/**
 * @brief メモリを割り当てます。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param size 要求バイト数
 * @return void* 割り当てられたメモリの先頭ポインタ (失敗時はNULL)
 */
void *embed_heap_alloc(embed_heap_t *heap, size_t size);

/**
 * @brief アライメントを指定してメモリを割り当てます。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param size 要求バイト数
 * @param alignment アライメント境界 (2のべき乗: 4, 8, 16, 32, 64 等)
 * @return void* 割り当てられたメモリの先頭ポインタ (失敗時はNULL)
 */
void *embed_heap_aligned_alloc(embed_heap_t *heap, size_t size, size_t alignment);

/**
 * @brief 0初期化されたメモリを割り当てます。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param num 要素数
 * @param size 1要素あたりのバイト数
 * @return void* 割り当てられたメモリの先頭ポインタ (失敗時はNULL)
 */
void *embed_heap_calloc(embed_heap_t *heap, size_t num, size_t size);

/**
 * @brief 割り当て済みメモリのサイズを変更します。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param ptr 既存のポインタ (NULLの場合はallocと同等)
 * @param new_size 新しい要求バイト数 (0の場合はfreeと同等)
 * @return void* 新しい領域の先頭ポインタ (失敗時はNULL、元の領域は維持)
 */
void *embed_heap_realloc(embed_heap_t *heap, void *ptr, size_t new_size);

/**
 * @brief メモリを解放し、隣接する空きブロックと統合します。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param ptr 解放するメモリのポインタ (NULLの場合は何もしない)
 */
void embed_heap_free(embed_heap_t *heap, void *ptr);

/**
 * @brief 現在のヒープ統計情報を取得します。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param stats 統計情報を格納する構造体ポインタ
 * @return true 取得成功
 * @return false 引数不正
 */
bool embed_heap_get_stats(const embed_heap_t *heap, embed_heap_stats_t *stats);

/**
 * @brief デバッグ用コールバック型
 */
typedef void (*embed_heap_dump_printer)(void *user_data, const char *fmt, ...);

/**
 * @brief ヒープ内部のブロック状態をダンプ出力します。
 * 
 * @param heap ヒープ管理構造体へのポインタ
 * @param printer 出力用関数ポインタ (printf等)
 * @param user_data printerに渡すユーザーポインタ
 */
void embed_heap_dump(const embed_heap_t *heap, embed_heap_dump_printer printer, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* EMBED_HEAP_H */
