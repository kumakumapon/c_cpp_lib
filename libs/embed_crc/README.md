# embed_crc

C11・動的確保なしのCRC/チェックサム。`EMBED_CRC_TABLES=OFF`（既定）はビット演算、
`ON` は読み取り専用テーブル（約5.6KB、同じ多項式・方向のアルゴリズムで共有）を使います。
UART/Modbusの既存CRC関数はこのライブラリへ委譲し、ワイヤ互換を維持します。

| 種類 (`embed_crc_kind`) | カタログ名 | 主な用途 | 幅 | 多項式 | 初期値 | 反転 | xor-out | `123456789` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `EMBED_CRC8_J1850` | CRC-8/SAE-J1850 | 車載 | 8 | 1D | FF | - | FF | 4B |
| `EMBED_CRC8_SMBUS` | CRC-8/SMBUS | SMBus PEC | 8 | 07 | 00 | - | 00 | F4 |
| `EMBED_CRC8_MAXIM` | CRC-8/MAXIM-DOW | 1-Wire (DS18B20等) | 8 | 31 | 00 | ✓ | 00 | A1 |
| `EMBED_CRC8_NRSC5` | CRC-8/NRSC-5 | Sensirion SHT3x/SCD4x等 | 8 | 31 | FF | - | 00 | F7 |
| `EMBED_CRC16_CCITT` | CRC-16/IBM-3740 | CCITT-FALSE | 16 | 1021 | FFFF | - | 0000 | 29B1 |
| `EMBED_CRC16_XMODEM` | CRC-16/XMODEM | XMODEM | 16 | 1021 | 0000 | - | 0000 | 31C3 |
| `EMBED_CRC16_KERMIT` | CRC-16/KERMIT | CCITT (反転) | 16 | 1021 | 0000 | ✓ | 0000 | 2189 |
| `EMBED_CRC16_X25` | CRC-16/IBM-SDLC | X.25, HDLC, PPP | 16 | 1021 | FFFF | ✓ | FFFF | 906E |
| `EMBED_CRC16_MODBUS` | CRC-16/MODBUS | Modbus RTU | 16 | 8005 | FFFF | ✓ | 0000 | 4B37 |
| `EMBED_CRC32_IEEE` | CRC-32/ISO-HDLC | Ethernet, zip, PNG | 32 | 04C11DB7 | FFFFFFFF | ✓ | FFFFFFFF | CBF43926 |
| `EMBED_CRC32C` | CRC-32/ISCSI | iSCSI, ext4 | 32 | 1EDC6F41 | FFFFFFFF | ✓ | FFFFFFFF | E3069283 |
| `EMBED_CRC32_MPEG2` | CRC-32/MPEG-2 | STM32等のハードウェアCRC | 32 | 04C11DB7 | FFFFFFFF | - | 00000000 | 0376E6E7 |

```c
/* 一括 */
uint32_t crc = embed_crc(EMBED_CRC32_IEEE, data, len);

/* ストリーミング: update には finish 前の状態を渡す */
uint32_t state = embed_crc_init(EMBED_CRC32_IEEE);
state = embed_crc_update(EMBED_CRC32_IEEE, state, "1234", 4);
state = embed_crc_update(EMBED_CRC32_IEEE, state, "56789", 5);
uint32_t value = embed_crc_finish(EMBED_CRC32_IEEE, state);

/* フレーム末尾への付加と検査 (反転方式はリトルエンディアン、非反転方式はビッグエンディアン) */
size_t n = embed_crc_append(EMBED_CRC16_MODBUS, embed_crc(EMBED_CRC16_MODBUS, adu, len),
                            adu + len, cap - len);
bool ok = embed_crc_verify(EMBED_CRC16_MODBUS, adu, len + n);

/* センサの CRC 検査 (SHT3x: 2 バイトのデータ + CRC) */
bool valid = embed_crc(EMBED_CRC8_NRSC5, rx, 2) == rx[2];
```

## 任意パラメータ

列挙にないアルゴリズムは `embed_crc_params_t`（幅1〜32、正規形の多項式、`refin == refout`）で計算できます。
常にビット演算で、テーブル設定に影響されません。

```c
const embed_crc_params_t crc7_mmc = {.width = 7, .reflect = false, .poly = 0x09, .init = 0, .xorout = 0};
uint32_t crc7 = embed_crc_params_compute(&crc7_mmc, cmd, 5); /* SD カードのコマンド CRC */
```

`embed_crc_get_params` で列挙値のパラメータを取り出せます。

## チェックサム

Fletcher-16は初期値0、Adler-32は初期値1で `embed_fletcher16` / `embed_adler32` を
繰り返し呼び出します。Adler-32はzlibと同じく5552バイトごとに剰余を取ります。

## 契約

- 長さ0ではNULLを許容します。NULLかつ非0長は状態を変更しません。
- 不正な種別は `init` / `finish` / `embed_crc` が0、`update` が状態をそのまま返します。
- 列挙値の順序はABIの一部です。新しい種別は末尾に追加します。

## テスト

`test_crc_spec` は CRC RevEng カタログの check / residue 値、Sensirion・Maxim AN27・Modbus・RFC 3720 の
実例、全分割点でのストリーミング、全ビット反転の検出を確認します。
`test_crc_spec_alt` は同じテストをライブラリと逆のテーブル設定でビルドし、両方式を毎回検証します。
テーブルは `tools/gen_crc_tables.py` で生成します。
