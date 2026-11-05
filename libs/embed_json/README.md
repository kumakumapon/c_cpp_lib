# embed_json

MQTT / HTTP のペイロード向けの小さな JSON (RFC 8259) ライブラリ。C11、動的確保なし。

## 書き出し

```c
char buf[128];
embed_json_writer_t w;
embed_json_writer_init(&w, buf, sizeof(buf));
embed_json_object_begin(&w);
embed_json_kv_string(&w, "device", "pump-01");
embed_json_kv_fixed(&w, "temp", temperature, 1);      /* 小数 1 桁: 23.5 */
embed_json_kv_bool(&w, "running", running);
embed_json_key(&w, "alarms");
embed_json_array_begin(&w);
embed_json_int(&w, 17);
embed_json_array_end(&w);
embed_json_object_end(&w);

size_t len;
const char *json = embed_json_finish(&w, &len);       /* 失敗なら NULL */
if (json)
    mqtt_client_publish(&mqtt, "plant/pump-01", json, len, MQTT_QOS_0, false);
```

- カンマ・コロンは自動で入ります。文字列は `" \ \b \f \n \r \t` と制御文字 (`\u00XX`) をエスケープします。
- NaN / Inf は JSON にないので `null` を書きます。`embed_json_fixed` の `-0.00` は `0.00` にします。
- 容量不足、オブジェクト内のキーなしの値、値のないキー、種類の違う閉じ方、閉じ忘れ、ルートの値が 2 つ
  のいずれかで `ok = false` になり、`embed_json_finish` は NULL を返します (壊れた JSON を送らないため)。

## 解析

文字列をコピーせず、トークン (型・範囲・子の数・親) の配列にします。

```c
embed_json_token_t t[32];
int n = embed_json_parse(payload, payload_len, t, 32);   /* NUL 終端でなくてよい */
if (n < 0) return;                                       /* NOMEM / INVALID / PARTIAL / DEPTH */
int sp = embed_json_object_get(payload, t, n, 0, "setpoint");
double setpoint;
if (sp >= 0 && embed_json_get_double(payload, &t[sp], &setpoint))
    apply_setpoint(setpoint);

int mode = embed_json_object_get(payload, t, n, 0, "mode");
char text[16];
if (mode >= 0 && embed_json_get_string(payload, &t[mode], text, sizeof(text)))  /* エスケープ解除 */
    set_mode(text);
```

- 文法は厳密です: 末尾カンマ、引用符のないキー、先頭 0 の数値 (`01`)、`.5` / `1.` / `+1`、
  未定義のエスケープ、文字列中の生の制御文字、値の後ろの余分な文字は `EMBED_JSON_ERR_INVALID`。
- 入力が途中で終わっていれば `EMBED_JSON_ERR_PARTIAL` (受信を待って再解析できます)。
- `embed_json_get_int` は小数部・指数部のない数だけを int64 の範囲で読みます。
- `embed_json_get_string` は `\uXXXX` を UTF-8 に変換します (サロゲートペア対応、不正な対は U+FFFD)。
- 入れ子の深さの上限は `EMBED_JSON_MAX_DEPTH` (既定 16) で、再帰の深さもこれで抑えます。

## テスト

`test_embed_json` は書き出した文字列そのもの、エスケープ、数値の表現、構造の誤用と容量不足の検出、
RFC 8259 の文法の受理・拒否 (22 例)、トークン数と深さの上限、サロゲートペア、int64 の境界、往復を確認します。
