# embed_heap (組み込み向け静的ヒープメモリアロケータ)

`embed_heap` は、OSヒープや標準Cライブラリの `malloc` / `free` が利用できない組み込みベアメタル環境や特定メモリプール向けに設計された、軽量・高機能な静的ヒープメモリ管理ライブラリです。

---

## 📑 目次
1. [背景と特徴](#-背景と特徴)
2. [アーキテクチャと内部仕様](#-アーキテクチャと内部仕様)
3. [C API リファレンス](#-c-api-リファレンス)
4. [C++ API / STL アロケータ](#-c-api--stl-アロケータ)
5. [実践的な使い方・ユースケース](#-実践的な使い方ユースケース)
6. [安全性・排他制御設計](#-安全性排他制御設計)
7. [計算量とオーバーヘッド](#-計算量とオーバーヘッド)

---

## 🎯 背景と特徴

### 組み込み環境における課題
- **OS/ヒープ非サポート**: ベアメタル環境や一部のRTOS環境では動的ヒープが使えない、または使用が推奨されない。
- **特定セクションへのメモリ集約**: SRAM、高速TCM (Tightly Coupled Memory)、外部SDRAMなどの特定領域のみを動的に切り出して使いたい。
- **メモリの断片化 (フラグメンテーション)**: 頻繁な確保・解放によるメモリ枯渇。
- **ハードウェア境界制約**: DMA転送や特定ペリフェラルが要求するアライメント（16/32/64バイト境界など）。

### `embed_heap` の解決策
- **ゼロ依存**: 標準 `malloc`/`free` やOS APIに一切依存せず、ユーザーが渡した静的バッファ（配列など）のみで動作。
- **即時マージ (Immediate Coalescing)**: メモリ解放時、前後の空きブロックを即座に結合して外部フラグメンテーションを強力に抑制。
- **任意のアライメント対応**: 2のべき乗アライメント境界を厳密に保証する `aligned_alloc` を標準装備。
- **二重解放・不正アクセス保護**: マジックナンバーとポインタ範囲検査による防御。
- **C++20 STL対応**: カスタムアロケータを提供し、`std::vector` などのSTLコンテナを静的ヒープ上で安全に動作可能。

---

## 🏗️ アーキテクチャと内部仕様

### 1. メモリレイアウト構造

初期化時、渡されたバッファ全体を1つの大きな「空きブロック」として管理します。割り当てと解放が行われるにつれて、ブロックが分割・マージされます。

```text
+-----------------------------------------------------------------------------------------+
|                                    ユーザー提供バッファ                                 |
+-------------------+-------------------+-------------------+-------------------------+
|  Block 0 (USED)   |  Block 1 (USED)   |  Block 2 (FREE)   |  Block 3 (FREE)         |
+---------+---------+---------+---------+---------+---------+---------+---------------+
| Header  | Payload | Header  | Payload | Header  | Payload | Header  | Payload       |
+---------+---------+---------+---------+---------+---------+---------+---------------+
```

### 2. ブロックヘッダ構造 (`embed_heap_block_t`)

各メモリブロックは、先頭に以下のメタデータヘッダを持ちます：

```c
typedef struct embed_heap_block {
    uint32_t magic;                 /* 破損・二重解放検知用 (0x45484541: "EHEA") */
    size_t size;                    /* ペイロード部分のサイズ (バイト) */
    bool is_free;                   /* 空き状態フラグ (true: 空き, false: 使用中) */
    struct embed_heap_block *prev;  /* 前の物理ブロックへのポインタ */
    struct embed_heap_block *next;  /* 次の物理ブロックへのポインタ */
} embed_heap_block_t;
```

### 3. アルゴリズム詳細

#### 割り当て (Allocation - First Fit + Split)
1. ヒープ先頭ブロックから順に、要求サイズ（+ アライメントパディング）を満たす最初の空きブロックを探索します。
2. ブロックの余剰サイズが `ブロックヘッダサイズ + 最小ペイロードサイズ` 以上ある場合、ブロックを **[使用中ブロック]** と **[新たな空きブロック]** の2つに分割 (Split) します。
3. ユーザーにはヘッダ直後のペイロード領域のアドレスを返却します。

#### 解放 (Free + Immediate Coalescing)
1. 渡されたポインタのアドレス妥当性（ヒープ範囲内、ヘッダのマジックナンバー、既解放フラグ）を検証します。
2. `is_free = true` に設定します。
3. **後方結合**: 次のブロックが存在し、かつ空きブロックであれば、1つのブロックに統合します。
4. **前方結合**: 前のブロックが存在し、かつ空きブロックであれば、前のブロックに統合します。

#### サイズ変更 (Realloc)
- **縮小**: 余剰サイズが十分であれば後続部分を新たな空きブロックとして切り離し、後続ブロックとマージします。
- **拡大 (インプレース)**: 次のブロックが空き状態で、合算サイズが要求を満たす場合、その場で拡張します（データのコピーが発生しません）。
- **拡大 (移動)**: インプレース拡張ができない場合、新規領域を確保してデータを `memcpy` し、旧領域を解放します。

---

## 📖 C API リファレンス

### 初期化・設定

#### `bool embed_heap_init(embed_heap_t *heap, void *buffer, size_t buffer_size)`
- **説明**: ヒープ管理構造体を初期化し、指定バッファを管理下に置きます。
- **引数**:
  - `heap`: ヒープ管理構造体へのポインタ
  - `buffer`: メモリプールの先頭アドレス（静的配列や特定のメモリアドレス）
  - `buffer_size`: バッファの総バイト数
- **戻り値**: 成功時 `true`、引数不正やサイズ不足時は `false`

#### `void embed_heap_set_lock(embed_heap_t *heap, embed_heap_lock_fn lock, embed_heap_unlock_fn unlock, void *user_data)`
- **説明**: マルチスレッドや割り込みコンテキストからの保護用コールバックを設定します。

---

### メモリ操作

#### `void *embed_heap_alloc(embed_heap_t *heap, size_t size)`
- **説明**: 指定サイズのメモリを割り当てます（デフォルトアライメント: `sizeof(void*)`）。
- **戻り値**: 割り当てられた領域の先頭ポインタ。メモリ不足時は `NULL`。

#### `void *embed_heap_aligned_alloc(embed_heap_t *heap, size_t size, size_t alignment)`
- **説明**: 指定した境界アライメントを満たすアドレスでメモリを割り当てます。
- **引数**:
  - `alignment`: 2のべき乗（4, 8, 16, 32, 64 等）

#### `void *embed_heap_calloc(embed_heap_t *heap, size_t num, size_t size)`
- **説明**: `num * size` バイトを割り当て、全ビットを `0` クリアして返します。

#### `void *embed_heap_realloc(embed_heap_t *heap, void *ptr, size_t new_size)`
- **説明**: 既存の割り当て領域のサイズを変更します。

#### `void embed_heap_free(embed_heap_t *heap, void *ptr)`
- **説明**: 領域を解放し、隣接する空き領域と自動マージします。`ptr` が `NULL` の場合は何もしません。

---

### 統計・デバッグ

#### `bool embed_heap_get_stats(const embed_heap_t *heap, embed_heap_stats_t *stats)`
- **説明**: ヒープの利用状況（使用中サイズ、空きサイズ、最大連続空きブロック、割り当て数など）を取得します。

#### `void embed_heap_dump(const embed_heap_t *heap, embed_heap_dump_printer printer, void *user_data)`
- **説明**: 全ブロックの状態（アドレス、サイズ、FREE/USED）を一覧ダンプします。

---

## ⚡ C++ API / STL アロケータ

C++ 環境では、型安全なオブジェクト生成 (`create` / `destroy`) および STL コンテナ用アロケータ (`embed_heap::Allocator<T>`) を提供します。

```cpp
#include "embed_heap/embed_heap.hpp"
#include <vector>

static uint8_t s_pool[8192];

void cpp_demo() {
    embed_heap::Heap heap(s_pool);

    // 1. オブジェクトの動的生成 (new/deleteの代用)
    struct SensorData {
        int id;
        float value;
        SensorData(int i, float v) : id(i), value(v) {}
    };

    SensorData *sensor = heap.create<SensorData>(1, 24.5f);
    heap.destroy(sensor);

    // 2. STLコンテナとの連携 (std::vector)
    using MyVector = std::vector<int, embed_heap::Allocator<int>>;
    embed_heap::Allocator<int> alloc(heap);

    MyVector vec(alloc);
    vec.push_back(10);
    vec.push_back(20);
    // スコープ終了時にメモリは自動的にヒープへ返却されます
}
```

---

## 💡 実践的な使い方・ユースケース

### ユースケース 1: ベアメタル環境での静的バッファヒープ

```c
#include "embed_heap/embed_heap.h"

// リンカスクリプトやBSSに配置された静的配列
static uint8_t s_system_heap[16 * 1024]; // 16 KB
static embed_heap_t g_heap;

void system_init(void) {
    embed_heap_init(&g_heap, s_system_heap, sizeof(s_system_heap));
}

void process_packet(size_t packet_size) {
    uint8_t *packet = (uint8_t *)embed_heap_alloc(&g_heap, packet_size);
    if (!packet) {
        // メモリ不足エラー処理
        return;
    }

    // パケット処理...

    embed_heap_free(&g_heap, packet);
}
```

---

### ユースケース 2: DMA 転送バッファの 32 バイトアライメント確保

```c
// DMAコントローラが32バイト境界を要求する場合
void *dma_buffer = embed_heap_aligned_alloc(&g_heap, 1024, 32);
if (dma_buffer) {
    // ((uintptr_t)dma_buffer % 32) == 0 が保証される
    start_dma_transfer(dma_buffer, 1024);
    
    // 転送完了後
    embed_heap_free(&g_heap, dma_buffer);
}
```

---

### ユースケース 3: FreeRTOS でのスレッドセーフ化

```c
#include "FreeRTOS.h"
#include "semphr.h"
#include "embed_heap/embed_heap.h"

static SemaphoreHandle_t s_heap_mutex;

static void heap_lock(void *user_data) {
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)user_data;
    xSemaphoreTake(mutex, portMAX_DELAY);
}

static void heap_unlock(void *user_data) {
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)user_data;
    xSemaphoreGive(mutex);
}

void setup_thread_safe_heap(embed_heap_t *heap, void *buf, size_t size) {
    s_heap_mutex = xSemaphoreCreateMutex();
    embed_heap_init(heap, buf, size);
    embed_heap_set_lock(heap, heap_lock, heap_unlock, (void *)s_heap_mutex);
}
```

---

## 🔒 安全性・排他制御設計

1. **二重解放 (Double Free) の防止**:
   - 解放処理時にブロックヘッダの `is_free` をチェックし、既に解放されているブロックに対する二重解放を無視・安全に弾きます。
2. **範囲外・不正ポインタの検出**:
   - `embed_heap_free` に渡されたポインタがヒープの管理領域 `[buffer, buffer + buffer_size)` 内にあるかを検証します。
   - ブロック先頭の `magic` (`0x45484541`) が一致しない場合、ヒープ破損または不正ポインタと判定して処理を中断します。
3. **クリティカルセクション保護**:
   - すべてのパブリック操作 (`alloc`, `aligned_alloc`, `calloc`, `realloc`, `free`) のエントリ/エグジットでロック関数が呼び出されます。

---

## 📊 計算量とオーバーヘッド

| 項目 | 値 / 計算量 | 備考 |
| :--- | :--- | :--- |
| **ヘッダオーバーヘッド** | 32 バイト / ブロック (64bit環境)<br>20 バイト / ブロック (32bit環境) | アライメント調整によりプラットフォーム依存 |
| **最小ブロックサイズ** | ヘッダサイズ + アライメントサイズ | 過度な細分化を防止 |
| **Allocation 時間計算量** | $O(N)$ (最悪) | $N$ は全ブロック数。First-Fit探索 |
| **Free 時間計算量** | $O(1)$ (即時マージ) | 双方向リンクリストによる前後ブロックへの直接アクセス |
| **Realloc 時間計算量** | $O(1)$ (インプレース) / $O(N)$ (移動) | 隣接空き領域がある場合はコピーなしで拡張 |
