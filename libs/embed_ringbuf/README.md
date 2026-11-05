# embed_ringbuf

UARTから独立したC11のバイトリングバッファ。呼び出し側の配列を使用し、確保は行いません。
容量2以上で初期化し、有効容量は配列長-1です。満杯ならputはfalse、writeは書けた長さを返します。
空ならgetはfalse、readは読めた長さを返します。容量不足・NULLでは書き込みを拒否します。
折り返しは比較と減算だけで行い、除算は使いません。複数バイト操作は最大2回の `memcpy` です。

```c
uint8_t storage[128];
embed_ringbuf_t rb;
embed_ringbuf_init(&rb, storage, sizeof(storage));
embed_ringbuf_put(&rb, 42);
uint8_t value;
embed_ringbuf_get(&rb, &value);
```

## API

| 区分 | 関数 | 内容 |
| --- | --- | --- |
| 生産者 | `put` / `write` | 1バイト / 入るだけ書く |
| 生産者 | `write_linear` + `write_commit` | 連続空き領域へ直接書く (DMA受信・ゼロコピー) |
| 消費者 | `get` / `read` | 1バイト / ある分だけ読む |
| 消費者 | `peek` / `peek_bytes` | 消費せずに読む (ヘッダの先読みなど) |
| 消費者 | `skip` | 先頭を捨てる |
| 消費者 | `find` | 区切り文字の位置を探す (行単位の受信など) |
| 消費者 | `read_linear` + `read_commit` | 連続格納領域から直接送る (DMA送信・ゼロコピー) |
| 状態 | `available` / `free_space` / `capacity` / `is_empty` / `is_full` | |
| 管理 | `init` / `clear` | 生産者・消費者の双方が止まっているときだけ |

### DMA での使い方

```c
/* UART 受信 DMA: 連続空き領域へ転送し、完了割込みで確定する */
uint8_t *dst;
size_t span = embed_ringbuf_write_linear(&rx, &dst);
HAL_UART_Receive_DMA(&huart1, dst, span);
/* ... 完了 (または IDLE 検出) 割込みで ... */
embed_ringbuf_write_commit(&rx, received);

/* UART 送信 DMA: 連続格納領域をそのまま送り、完了後に消費する */
const uint8_t *src;
size_t n = embed_ringbuf_read_linear(&tx, &src);
HAL_UART_Transmit_DMA(&huart1, src, n);
/* ... 完了割込みで ... */
embed_ringbuf_read_commit(&tx, n);
```

格納データが末尾で折り返している場合、`*_linear` は折り返し点までの長さを返します。
確定後にもう一度呼ぶと残りの領域が得られます。

## 並行性 (SPSC)

生産者 (put / write / write_commit) と消費者 (get / read / skip / read_commit) がそれぞれ
1つずつなら、ロックなしで ISR とメインループ、または2スレッド間で使えます。
生産者は `head` だけ、消費者は `tail` だけを更新し、データの読み書きと位置の更新の間に
メモリバリア (GCC/Clang の `__atomic_thread_fence`、MSVC のバリア、C11 `atomic_thread_fence`) を置きます。
`volatile` だけでは、バッファへの書き込みが `head` の更新より後ろへ並べ替えられる可能性がありました。

- 生産者・消費者が複数ある場合は、それぞれの側で排他してください。
- `size_t` を1命令で読み書きできない 8bit MCU では、位置の読み書きの間に割込み禁止が必要です。
- `clear` / `init` は両側が止まっているときだけ呼んでください。

既存 `uart_ringbuf_*` は互換マクロと型別名として引き続き使用できます（再コンパイルが必要）。
