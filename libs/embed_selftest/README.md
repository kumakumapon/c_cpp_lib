# embed_selftest

起動時・周期診断向けのC11ユーティリティ。診断の実施時期と故障時の安全停止は利用側で決めます。

| API | 契約 |
| --- | --- |
| `embed_selftest_march_c` | 32bit RAMのMarch C-。破壊的検査。成功時は0を残す |
| `embed_selftest_rom` | IEEE CRC-32を期待値と比較 |
| `embed_selftest_cpu` | 対象CPUのレジスタ検査フックを呼ぶ。未設定ならfalse |
| `embed_selftest_stack_fill/unused` | 未使用領域のマーカーと上下方向のウォーターマーク測定 |
| `embed_selftest_clock` | 独立した基準で測定した値を期待値±ppmで検査 |
| `embed_watchdog_init/poll` | 最小/最大間隔内かつhealthyの場合のみ給餌。故障はラッチ |

RAMテストには専用・整列済み・排他所有の領域を渡してください。使用中のスタックや
ヒープへ実行できません。スタック塗りつぶしも、対象タスク開始前の未使用部分だけに行います。
マーカーと偶然同じ値の書き込みはウォーターマークで検出できません。
ウォッチドッグの早すぎるpollは給餌を待機し、期限超過・時計逆行・healthy=falseはfaultを保持します。
復帰には利用側の故障対応後に再初期化が必要です。

CPUレジスタの実検査にはレジスタの退避・復元を含む機種別アセンブリが必要です。
ホスト上のフックテストはCPU故障検出率を検証しません。本モジュール自体はIEC 61508 /
ISO 13849の認証品ではなく、認証・診断カバレッジを保証しません。

## 抽象メモリでの検査 (外付けメモリ・ボード立ち上げ)

`embed_selftest_mem_io` に読み書き関数を渡すと、SPI SRAM / FRAM などドライバ経由のメモリにも
同じ検査を実行できます。内蔵 RAM は `embed_selftest_ram_io` で包みます。

| API | 検出する故障 |
| --- | --- |
| `embed_selftest_march_c_io` | セル固着・遷移故障・アドレスデコーダ故障・語間の結合故障 (失敗位置を返す) |
| `embed_selftest_data_bus` | データ線の固着・短絡 (1語に walking-one) |
| `embed_selftest_address_bus` | アドレス線の固着・短絡 (2の累乗オフセット) |

```c
embed_selftest_mem_io io = {spi_sram_read32, spi_sram_write32, &sram_dev};
size_t where;
if (!embed_selftest_data_bus(&io, 0, NULL) ||
    !embed_selftest_address_bus(&io, SRAM_WORDS, &where) ||
    !embed_selftest_march_c_io(&io, SRAM_WORDS, &where)) {
    enter_safe_state();
}
```

全0/全1の背景データで行う March C- は、同じ語の中のビット間短絡を検出できません。
データバス検査と組み合わせてください（`test_selftest_spec` でこの差を確認しています）。
すべて破壊的な検査です。

## ROM の周期検査

起動時の一括検査に加えて、実行中に CPU 時間を分けて ROM の CRC を少しずつ計算できます。

```c
static embed_selftest_rom_ctx rom;
embed_selftest_rom_begin(&rom, __flash_start, FLASH_SIZE, expected_crc32);
/* 周期タスク (例: 10 ms ごとに 256 バイト) */
switch (embed_selftest_rom_step(&rom, 256)) {
case EMBED_SELFTEST_BUSY: break;            /* 計算途中 */
case EMBED_SELFTEST_PASS: break;            /* 1 周一致。次の呼び出しから先頭に戻る */
case EMBED_SELFTEST_FAIL: enter_safe_state(); break;
}
```

## テスト

`test_selftest_spec` は、固着・遷移・結合・アドレスデコーダ・データ線短絡・アドレス線固着を注入した
仮想メモリで各検査の検出能力を確認し、時計の ppm 判定とウォッチドッグ窓の境界を固定します。
