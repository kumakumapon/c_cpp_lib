# embed_frame

バイトストリーム (UART / USB CDC / TCP) 上でパケットの境界を作るフレーミング。C11、動的確保なし。

| 方式 | 区切り | オーバーヘッド | 用途 |
| --- | --- | --- | --- |
| COBS | `0x00` | 最大 ceil(n/254) バイト (約 0.4%) | 最悪ケースが小さい。バイナリ全般 |
| SLIP (RFC 1055) | `0xC0` | 最悪 2 倍 (`0xC0` / `0xDB` をエスケープ) | 既存ツール・機器との互換 |

どちらも区切りバイトがデータ中に現れないので、受信側は途中から聞き始めても次の区切りで同期できます。
CRC が必要なら `embed_crc_append` でペイロードに付けてから符号化してください。

## COBS

```c
uint8_t payload[64], wire[EMBED_COBS_MAX_ENCODED(64) + 1];
size_t n;
embed_cobs_encode(payload, len, wire, sizeof(wire) - 1, &n);
wire[n++] = EMBED_COBS_DELIMITER;           /* 区切りを付けて送る */
uart_write(wire, n);

/* 受信: 1 バイトずつ (ISR が積んだリングバッファから) */
static uint8_t rx_buf[EMBED_COBS_MAX_ENCODED(64)];
static embed_cobs_decoder_t dec;
embed_cobs_decoder_init(&dec, rx_buf, sizeof(rx_buf));
const uint8_t *frame;
size_t frame_len;
switch (embed_cobs_decoder_feed(&dec, byte, &frame, &frame_len)) {
case EMBED_FRAME_COMPLETE: handle(frame, frame_len); break;  /* frame は次の feed まで有効 */
case EMBED_FRAME_ERROR:    break;                            /* 不正・長すぎ: そのフレームは捨てた */
case EMBED_FRAME_IN_PROGRESS: break;
}
```

- `embed_cobs_decode` は `dst == src` のその場復号ができます (復号器もそうしています)。
- 254 バイトの非 0 ブロックで終わるデータには余分なコードバイトを付けない正準形で符号化します。
- 区切りの連続 (空フレーム) は無視します。長すぎるフレームは次の区切りまで読み捨ててエラー1回です。

## SLIP

```c
uint8_t wire[EMBED_SLIP_MAX_ENCODED(64)];
embed_slip_encode(payload, len, wire, sizeof(wire), &n);     /* 先頭と末尾に END を付ける */

embed_slip_decoder_t dec;
embed_slip_decoder_init(&dec, rx_buf, sizeof(rx_buf));
embed_slip_decoder_feed(&dec, byte, &frame, &frame_len);     /* 戻り値は COBS と同じ */
```

未定義のエスケープ (`0xDB` の後が `0xDC` / `0xDD` 以外)、ESC 直後の END、バッファ超過はエラーとして
そのフレームを捨てます。

## テスト

`test_embed_frame` は Cheshire & Baker / Wikipedia の COBS 符号化例 (254・255 バイトの境界を含む)、
RFC 1055 のエスケープ、ストリーム復号の同期・回復、乱数データの往復を確認します。
