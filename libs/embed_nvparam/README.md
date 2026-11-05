# embed_nvparam

設定値・校正値をフラッシュに保存する。**書き込み中の電源断に耐えます**。C11、動的確保なし。

## 仕組み

2 つのバンク (消去単位) を交互に使います。

1. 古い方のバンクを消去する
2. データを書く (書き込み単位まで 0xFF で埋める)
3. ヘッダ (マジック・版・長さ・通し番号・CRC-32) を書く ← ここで確定
4. 読み戻して CRC を照合する

読み出し時は、CRC が正しい記録のうち通し番号の新しい方を使います (32bit の周回も考慮)。
どの時点で電源が切れても、「直前に保存した値」か「その前の値」に戻り、壊れた値は使いません。
最新の記録がビット化けした場合も、1 つ前の記録に戻ります。

```
バンク: | magic "NVP1" | version | length | seq | CRC-32 | data ... | 0xFF 埋め |
          ^ ヘッダは最後に書く (16 バイト。書き込み単位まで 0xFF で埋める)
```

## 使い方

```c
typedef struct {
    float gain;
    int16_t offset;
    uint8_t mode;
} config_t;

static const embed_nvparam_flash_t flash = {
    .read = flash_read, .write = flash_program, .erase = flash_erase_pages, .ctx = NULL,
    .bank_addr = {0x0807F000, 0x0807F800},     /* 例: 2KB ページ × 2 */
    .bank_size = 0x800,
    .write_align = 8,                           /* STM32L4 のダブルワード書き込み */
};

embed_nvparam_t nv;
config_t cfg = {1.0f, 0, 0};                    /* 既定値 */
embed_nvparam_init(&nv, &flash, 1 /* データ構造の版 */);
switch (embed_nvparam_load(&nv, &cfg, sizeof(cfg))) {
case EMBED_NVPARAM_OK:      break;              /* 保存値を読めた */
case EMBED_NVPARAM_EMPTY:   break;              /* 初回起動: 既定値のまま */
case EMBED_NVPARAM_VERSION: migrate(&nv, &cfg); break;
default:                    break;              /* SIZE / IO: 既定値のまま */
}
...
cfg.gain = 1.25f;
if (embed_nvparam_save(&nv, &cfg, sizeof(cfg)) != EMBED_NVPARAM_OK)
    report_error();                             /* 失敗しても前の記録は残っている */
```

### 版が変わったときの移行

`embed_nvparam_peek` で保存されている版と長さを調べ、`embed_nvparam_load_raw` で古い形式のまま読み出して
変換し、新しい版で `save` してください。

## 契約

- `load` は版と長さが一致したときだけ `data` を書き換えます。それ以外は既定値がそのまま残ります。
- `write` には `write_align` の倍数のアドレス・長さだけを渡します。
- `save` が `EMBED_NVPARAM_IO` を返しても、電源断のタイミングによっては新しい記録が完成している場合が
  あります (ヘッダの埋め草を書く途中など)。次の `load` はどちらか一方の完全な記録を返します。
- 保存のたびに 1 バンクを消去します。フラッシュの書き換え寿命 (例: 1 万回) を考え、頻繁に変わる値
  (カウンタなど) の保存は間隔を空けてください。
- 同時に呼ばないでください (利用側で排他)。

## テスト

`test_embed_nvparam` はフラッシュのシミュレータ (消去は 0xFF、書き込みは 1→0 のみ、書き込み単位の検査、
途中での電源断) を使い、保存の全バイト操作の時点で電源を切って再起動し、毎回「前の値」か「新しい値」に
戻ること、切り替わりが一方向であることを、書き込み単位 1・8・32 バイトで確かめます。
ビット化けからの復帰、版・長さの不一致と移行、通し番号の周回、書き込み不良の検出も確認します。
