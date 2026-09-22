# embed_crc

C11・動的確保なしのCRC/チェックサム。`EMBED_CRC_TABLES=OFF`（既定）はビット演算、
`ON` は読み取り専用テーブルを使います。CCITTとXMODEMのテーブルは共有します。
UART/Modbusの既存CRC関数はこのライブラリへ委譲し、ワイヤ互換を維持します。

| 種類 | 初期値 | xor-out | `123456789` |
| --- | --- | --- | --- |
| CRC-8 SAE-J1850 | FF | FF | 4B |
| CRC-16 CCITT-FALSE | FFFF | 0 | 29B1 |
| CRC-16 Modbus | FFFF | 0 | 4B37 |
| CRC-16 XMODEM | 0 | 0 | 31C3 |
| CRC-32 IEEE | FFFFFFFF | FFFFFFFF | CBF43926 |
| CRC-32 Castagnoli | FFFFFFFF | FFFFFFFF | E3069283 |

```c
uint32_t state = embed_crc_init(EMBED_CRC32_IEEE);
state = embed_crc_update(EMBED_CRC32_IEEE, state, "1234", 4);
state = embed_crc_update(EMBED_CRC32_IEEE, state, "56789", 5);
uint32_t value = embed_crc_finish(EMBED_CRC32_IEEE, state);
```

`embed_crc` は一括処理。更新APIにはfinalize前の状態を渡します。
Fletcher-16は初期値0、Adler-32は初期値1で `embed_fletcher16` / `embed_adler32` を
繰り返し呼び出します。長さ0ではNULLを許容します。NULLかつ非0長は状態を変更しません。
CRC種別には上記の列挙値のみを指定してください。
