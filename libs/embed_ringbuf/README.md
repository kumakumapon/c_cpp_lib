# embed_ringbuf

UARTから独立したC11のバイトリングバッファ。呼び出し側の配列を使用し、確保は行いません。
容量2以上で初期化し、有効容量は配列長-1です。満杯ならputはfalse、writeは書けた長さを返します。
空ならgetはfalse、readは読めた長さを返します。容量不足・NULLでは書き込みを拒否します。

2の冪の容量はマスクで折り返し、それ以外は条件分岐で折り返します。除算は使いません。
既存 `uart_ringbuf_*` は互換マクロと型別名として引き続き使用できます（再コンパイルが必要）。

```c
uint8_t storage[128];
embed_ringbuf_t rb;
embed_ringbuf_init(&rb, storage, sizeof(storage));
embed_ringbuf_put(&rb, 42);
uint8_t value;
embed_ringbuf_get(&rb, &value);
```

並行呼び出しには呼び出し側で同期が必要です。`volatile` はC/C++メモリモデル上の
スレッド同期を保証しません。ISRから利用する場合も対象MCUの原子性・メモリバリア・
クリティカルセクションを設計してください。clear/initは排他的に実行します。
