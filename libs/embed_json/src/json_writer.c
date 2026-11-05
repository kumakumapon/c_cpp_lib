#include "embed_json/embed_json.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { LV_OBJECT = 1u, LV_HAS_ITEMS = 2u, LV_AFTER_KEY = 4u };

static void append(embed_json_writer_t *w, const char *s, size_t n) {
    if (!w->ok)
        return;
    if (n >= w->cap - w->len) { /* 終端の NUL の分を残す */
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

void embed_json_writer_init(embed_json_writer_t *w, char *buf, size_t cap) {
    if (!w)
        return;
    memset(w, 0, sizeof(*w));
    w->buf = buf;
    w->cap = buf ? cap : 0;
    w->ok = buf && cap > 0;
    if (w->ok)
        buf[0] = '\0';
}

/* 値を書く前の区切りと構造の検査 */
static bool before_value(embed_json_writer_t *w) {
    if (!w || !w->ok)
        return false;
    if (w->depth == 0) {
        if (w->root_done) { /* ルートの値は 1 つだけ */
            w->ok = false;
            return false;
        }
        w->root_done = true;
        return true;
    }
    uint8_t *lv = &w->level[w->depth - 1];
    if (*lv & LV_OBJECT) {
        if (!(*lv & LV_AFTER_KEY)) { /* オブジェクトの中でキーのない値 */
            w->ok = false;
            return false;
        }
        *lv = (uint8_t)(*lv & ~LV_AFTER_KEY);
        return true;
    }
    if (*lv & LV_HAS_ITEMS)
        append(w, ",", 1);
    *lv |= LV_HAS_ITEMS;
    return w->ok;
}

static void write_escaped(embed_json_writer_t *w, const char *s, size_t n) {
    append(w, "\"", 1);
    size_t run = 0; /* エスケープ不要な連続部分はまとめて書く */
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char u[7];
        switch (c) {
        case '"': esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\b': esc = "\\b"; break;
        case '\f': esc = "\\f"; break;
        case '\n': esc = "\\n"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        default:
            if (c < 0x20) {
                snprintf(u, sizeof(u), "\\u%04x", c);
                esc = u;
            }
            break;
        }
        if (esc) {
            append(w, s + i - run, run);
            run = 0;
            append(w, esc, strlen(esc));
        } else {
            ++run;
        }
    }
    append(w, s + n - run, run);
    append(w, "\"", 1);
}

static bool begin_container(embed_json_writer_t *w, bool object) {
    if (!before_value(w))
        return false;
    if (w->depth >= EMBED_JSON_MAX_DEPTH) {
        w->ok = false;
        return false;
    }
    w->level[w->depth++] = object ? LV_OBJECT : 0u;
    append(w, object ? "{" : "[", 1);
    return w->ok;
}

static bool end_container(embed_json_writer_t *w, bool object) {
    if (!w || !w->ok || w->depth == 0)
        goto bad;
    const uint8_t lv = w->level[w->depth - 1];
    if (((lv & LV_OBJECT) != 0) != object || (lv & LV_AFTER_KEY))
        goto bad; /* 種類の不一致、または値のないキー */
    w->depth--;
    append(w, object ? "}" : "]", 1);
    return w->ok;
bad:
    if (w)
        w->ok = false;
    return false;
}

bool embed_json_object_begin(embed_json_writer_t *w) { return begin_container(w, true); }
bool embed_json_object_end(embed_json_writer_t *w) { return end_container(w, true); }
bool embed_json_array_begin(embed_json_writer_t *w) { return begin_container(w, false); }
bool embed_json_array_end(embed_json_writer_t *w) { return end_container(w, false); }

bool embed_json_key(embed_json_writer_t *w, const char *key) {
    if (!w || !w->ok || !key || w->depth == 0)
        goto bad;
    uint8_t *lv = &w->level[w->depth - 1];
    if (!(*lv & LV_OBJECT) || (*lv & LV_AFTER_KEY))
        goto bad;
    if (*lv & LV_HAS_ITEMS)
        append(w, ",", 1);
    *lv |= LV_HAS_ITEMS | LV_AFTER_KEY;
    write_escaped(w, key, strlen(key));
    append(w, ":", 1);
    return w->ok;
bad:
    if (w)
        w->ok = false;
    return false;
}

bool embed_json_string_n(embed_json_writer_t *w, const char *s, size_t len) {
    if (!s && len) {
        if (w)
            w->ok = false;
        return false;
    }
    if (!before_value(w))
        return false;
    write_escaped(w, s ? s : "", len);
    return w->ok;
}

bool embed_json_string(embed_json_writer_t *w, const char *s) {
    if (!s)
        return embed_json_null(w);
    return embed_json_string_n(w, s, strlen(s));
}

static bool literal(embed_json_writer_t *w, const char *text) {
    if (!before_value(w))
        return false;
    append(w, text, strlen(text));
    return w->ok;
}

bool embed_json_int(embed_json_writer_t *w, int64_t v) {
    char t[24];
    snprintf(t, sizeof(t), "%lld", (long long)v);
    return literal(w, t);
}

bool embed_json_uint(embed_json_writer_t *w, uint64_t v) {
    char t[24];
    snprintf(t, sizeof(t), "%llu", (unsigned long long)v);
    return literal(w, t);
}

bool embed_json_double(embed_json_writer_t *w, double v) {
    if (!isfinite(v))
        return literal(w, "null"); /* JSON に NaN / Inf はない */
    char t[32];
    snprintf(t, sizeof(t), "%.15g", v);
    return literal(w, t);
}

bool embed_json_fixed(embed_json_writer_t *w, double v, int decimals) {
    if (!isfinite(v))
        return literal(w, "null");
    if (decimals < 0)
        decimals = 0;
    if (decimals > 9)
        decimals = 9;
    if (fabs(v) >= 1e15) /* %f で桁が多くなりすぎる値は有効数字表記にする */
        return embed_json_double(w, v);
    char t[40];
    snprintf(t, sizeof(t), "%.*f", decimals, v);
    if (strcmp(t, "-0") == 0 || (t[0] == '-' && strspn(t + 1, "0.") == strlen(t + 1)))
        memmove(t, t + 1, strlen(t)); /* -0.00 は 0.00 にする */
    return literal(w, t);
}

bool embed_json_bool(embed_json_writer_t *w, bool v) { return literal(w, v ? "true" : "false"); }
bool embed_json_null(embed_json_writer_t *w) { return literal(w, "null"); }

bool embed_json_kv_string(embed_json_writer_t *w, const char *k, const char *v) {
    return embed_json_key(w, k) && embed_json_string(w, v);
}
bool embed_json_kv_int(embed_json_writer_t *w, const char *k, int64_t v) {
    return embed_json_key(w, k) && embed_json_int(w, v);
}
bool embed_json_kv_double(embed_json_writer_t *w, const char *k, double v) {
    return embed_json_key(w, k) && embed_json_double(w, v);
}
bool embed_json_kv_fixed(embed_json_writer_t *w, const char *k, double v, int decimals) {
    return embed_json_key(w, k) && embed_json_fixed(w, v, decimals);
}
bool embed_json_kv_bool(embed_json_writer_t *w, const char *k, bool v) {
    return embed_json_key(w, k) && embed_json_bool(w, v);
}

const char *embed_json_finish(embed_json_writer_t *w, size_t *len) {
    if (!w || !w->ok || w->depth != 0 || !w->root_done)
        return NULL;
    if (len)
        *len = w->len;
    return w->buf;
}
