# embed_input

ディジタル入力のチャタリング除去とボタン操作の判定。C11、動的確保なし。時刻は 32bit ミリ秒 (周回対応)。

## 多点入力のチャタリング除去 (`embed_debounce`)

最大 32 点の入力をポートからまとめて読み、同じ値が `samples` 回続いたビットだけを確定します。
PLC のディジタル入力やリミットスイッチ向けです。

```c
static embed_debounce_t din;
embed_debounce_init(&din, 4 /* 回 */, read_port());       /* 起動時の値で初期化 */

void every_2ms(void) {                                       /* 4 回 × 2 ms = 8 ms で確定 */
    uint32_t changed = embed_debounce_update(&din, read_port());
    if (embed_debounce_rising(&din, changed) & LIMIT_SWITCH_BIT)
        stop_axis();
}
```

## ボタン (`embed_button`)

| イベント | 発生する時点 |
| --- | --- |
| `PRESSED` / `RELEASED` | 生の入力が `debounce_ms` 変わらなかった時点 |
| `CLICK` | 短い押下の後、`double_click_ms` 以内に次の押下がなかった時点 (`double_click_ms = 0` なら解放と同時) |
| `DOUBLE_CLICK` | 2 回目の短い押下の解放時 |
| `LONG_PRESS` | 押下確定から `long_press_ms` 経過した時点 (押している間に 1 回。解放してもクリックにしない) |
| `REPEAT` | 長押しの後 `repeat_ms` ごと (数値の早送りなど) |

```c
static embed_button_t btn;
static const embed_button_config_t cfg = {.debounce_ms = 20, .long_press_ms = 800,
                                          .double_click_ms = 300, .repeat_ms = 100};
embed_button_init(&btn, &cfg, millis());

void every_5ms(void) {
    uint8_t ev = embed_button_update(&btn, gpio_read(BTN_PIN) == 0 /* 押下で Low */, millis());
    if (ev & EMBED_BUTTON_CLICK)        next_page();
    if (ev & EMBED_BUTTON_DOUBLE_CLICK) toggle_mode();
    if (ev & EMBED_BUTTON_LONG_PRESS)   enter_settings();
    if (ev & EMBED_BUTTON_REPEAT)       value++;
}
```

- 1 回の呼び出しで複数のイベントが同時に立つことがあります (例: `RELEASED | CLICK`)。
- クリックの直後に長押しすると、長押しの時点で先のクリックも `CLICK` として報告します。
- 押している間とクリック判定待ちの間は、チャタリング時間より短い周期で呼んでください。
- 呼び出しが遅れてもリピートを溜め込みません。

## テスト

`test_embed_input` は 1 ms ごとの時間軸で、チャタリングからの確定時刻、クリック・ダブルクリック・
長押し・リピートの発生時刻と回数、時刻の周回、多点入力のグリッチ除去と立ち上がり・立ち下がりを確認します。
