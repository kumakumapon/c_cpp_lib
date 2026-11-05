#include "embed_json/embed_json.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *js;
    size_t len, pos;
    embed_json_token_t *tok;
    size_t max, count;
    int depth;
} parser;

static void skip_ws(parser *p) {
    while (p->pos < p->len) {
        const char c = p->js[p->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        ++p->pos;
    }
}

static int alloc(parser *p, embed_json_type type, int parent) {
    if (p->count >= p->max)
        return EMBED_JSON_ERR_NOMEM;
    embed_json_token_t *t = &p->tok[p->count];
    t->type = type;
    t->start = (uint32_t)p->pos;
    t->end = (uint32_t)p->pos;
    t->size = 0;
    t->parent = parent;
    if (parent >= 0)
        p->tok[parent].size++;
    return (int)p->count++;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int parse_value(parser *p, int parent);

static int parse_string(parser *p, int parent) {
    ++p->pos; /* 開きの引用符 */
    const int idx = alloc(p, EMBED_JSON_STRING, parent);
    if (idx < 0)
        return idx;
    while (p->pos < p->len) {
        const unsigned char c = (unsigned char)p->js[p->pos];
        if (c == '"') {
            p->tok[idx].end = (uint32_t)p->pos++;
            return idx;
        }
        if (c < 0x20)
            return EMBED_JSON_ERR_INVALID; /* 制御文字はエスケープが必要 */
        if (c == '\\') {
            if (++p->pos >= p->len)
                return EMBED_JSON_ERR_PARTIAL;
            const char e = p->js[p->pos];
            if (e == 'u') {
                for (int k = 1; k <= 4; ++k) {
                    if (p->pos + (size_t)k >= p->len)
                        return EMBED_JSON_ERR_PARTIAL;
                    if (!is_hex(p->js[p->pos + (size_t)k]))
                        return EMBED_JSON_ERR_INVALID;
                }
                p->pos += 4;
            } else if (!strchr("\"\\/bfnrt", e) || e == '\0') {
                return EMBED_JSON_ERR_INVALID;
            }
        }
        ++p->pos;
    }
    return EMBED_JSON_ERR_PARTIAL;
}

/* -? (0 | [1-9][0-9]*) (\.[0-9]+)? ([eE][+-]?[0-9]+)? */
static int parse_number(parser *p, int parent) {
    const int idx = alloc(p, EMBED_JSON_NUMBER, parent);
    if (idx < 0)
        return idx;
    const char *s = p->js;
    size_t i = p->pos;
    if (i < p->len && s[i] == '-')
        ++i;
    if (i >= p->len)
        return EMBED_JSON_ERR_PARTIAL;
    if (s[i] == '0') {
        ++i;
    } else if (is_digit(s[i])) {
        while (i < p->len && is_digit(s[i]))
            ++i;
    } else {
        return EMBED_JSON_ERR_INVALID;
    }
    if (i < p->len && s[i] == '.') {
        ++i;
        if (i >= p->len)
            return EMBED_JSON_ERR_PARTIAL;
        if (!is_digit(s[i]))
            return EMBED_JSON_ERR_INVALID;
        while (i < p->len && is_digit(s[i]))
            ++i;
    }
    if (i < p->len && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < p->len && (s[i] == '+' || s[i] == '-'))
            ++i;
        if (i >= p->len)
            return EMBED_JSON_ERR_PARTIAL;
        if (!is_digit(s[i]))
            return EMBED_JSON_ERR_INVALID;
        while (i < p->len && is_digit(s[i]))
            ++i;
    }
    p->pos = i;
    p->tok[idx].end = (uint32_t)i;
    return idx;
}

static int parse_literal(parser *p, int parent, const char *word, embed_json_type type) {
    const size_t n = strlen(word);
    for (size_t k = 0; k < n; ++k) {
        if (p->pos + k >= p->len)
            return EMBED_JSON_ERR_PARTIAL;
        if (p->js[p->pos + k] != word[k])
            return EMBED_JSON_ERR_INVALID;
    }
    const int idx = alloc(p, type, parent);
    if (idx < 0)
        return idx;
    p->pos += n;
    p->tok[idx].end = (uint32_t)p->pos;
    return idx;
}

static int parse_container(parser *p, int parent, bool object) {
    if (++p->depth > EMBED_JSON_MAX_DEPTH)
        return EMBED_JSON_ERR_DEPTH;
    const int idx = alloc(p, object ? EMBED_JSON_OBJECT : EMBED_JSON_ARRAY, parent);
    if (idx < 0)
        return idx;
    const char close = object ? '}' : ']';
    ++p->pos;
    skip_ws(p);
    if (p->pos < p->len && p->js[p->pos] == close) {
        p->tok[idx].end = (uint32_t)++p->pos;
        --p->depth;
        return idx;
    }
    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len)
            return EMBED_JSON_ERR_PARTIAL;
        int r;
        if (object) {
            if (p->js[p->pos] != '"')
                return EMBED_JSON_ERR_INVALID; /* キーは文字列 (末尾カンマもここで不正) */
            const int key = parse_string(p, idx);
            if (key < 0)
                return key;
            skip_ws(p);
            if (p->pos >= p->len)
                return EMBED_JSON_ERR_PARTIAL;
            if (p->js[p->pos] != ':')
                return EMBED_JSON_ERR_INVALID;
            ++p->pos;
            skip_ws(p);
            r = parse_value(p, key);
        } else {
            if (p->js[p->pos] == ']')
                return EMBED_JSON_ERR_INVALID; /* 末尾カンマ */
            r = parse_value(p, idx);
        }
        if (r < 0)
            return r;
        skip_ws(p);
        if (p->pos >= p->len)
            return EMBED_JSON_ERR_PARTIAL;
        const char c = p->js[p->pos];
        if (c == ',') {
            ++p->pos;
            continue;
        }
        if (c == close) {
            p->tok[idx].end = (uint32_t)++p->pos;
            --p->depth;
            return idx;
        }
        return EMBED_JSON_ERR_INVALID;
    }
}

static int parse_value(parser *p, int parent) {
    skip_ws(p);
    if (p->pos >= p->len)
        return EMBED_JSON_ERR_PARTIAL;
    const char c = p->js[p->pos];
    switch (c) {
    case '{':
        return parse_container(p, parent, true);
    case '[':
        return parse_container(p, parent, false);
    case '"':
        return parse_string(p, parent);
    case 't':
        return parse_literal(p, parent, "true", EMBED_JSON_BOOL);
    case 'f':
        return parse_literal(p, parent, "false", EMBED_JSON_BOOL);
    case 'n':
        return parse_literal(p, parent, "null", EMBED_JSON_NULL);
    default:
        if (c == '-' || is_digit(c))
            return parse_number(p, parent);
        return EMBED_JSON_ERR_INVALID;
    }
}

int embed_json_parse(const char *js, size_t len, embed_json_token_t *tokens, size_t max_tokens) {
    if (!js || !tokens || len > UINT32_MAX)
        return EMBED_JSON_ERR_INVALID;
    parser p = {js, len, 0, tokens, max_tokens, 0, 0};
    const int r = parse_value(&p, -1);
    if (r < 0)
        return r;
    skip_ws(&p);
    if (p.pos != len)
        return EMBED_JSON_ERR_INVALID; /* 値の後ろに余分な文字 */
    return (int)p.count;
}

bool embed_json_token_eq(const char *js, const embed_json_token_t *t, const char *s) {
    if (!js || !t || !s)
        return false;
    const size_t n = strlen(s);
    return t->end - t->start == n && memcmp(js + t->start, s, n) == 0;
}

int embed_json_object_get(const char *js, const embed_json_token_t *tok, int count, int object,
                          const char *key) {
    if (!js || !tok || !key || object < 0 || object >= count || tok[object].type != EMBED_JSON_OBJECT)
        return -1;
    for (int i = object + 1; i + 1 < count && tok[i].start < tok[object].end; ++i)
        if (tok[i].parent == object && tok[i].type == EMBED_JSON_STRING &&
            embed_json_token_eq(js, &tok[i], key))
            return i + 1;
    return -1;
}

int embed_json_array_get(const embed_json_token_t *tok, int count, int array, size_t n) {
    if (!tok || array < 0 || array >= count || tok[array].type != EMBED_JSON_ARRAY)
        return -1;
    size_t seen = 0;
    for (int i = array + 1; i < count && tok[i].start < tok[array].end; ++i)
        if (tok[i].parent == array && seen++ == n)
            return i;
    return -1;
}

static int hex4(const char *s) {
    int v = 0;
    for (int k = 0; k < 4; ++k) {
        const char c = s[k];
        v = v * 16 + (is_digit(c) ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

static bool put_utf8(char *out, size_t cap, size_t *o, uint32_t cp) {
    char b[4];
    size_t n;
    if (cp < 0x80) {
        b[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    if (*o + n >= cap)
        return false;
    memcpy(out + *o, b, n);
    *o += n;
    return true;
}

bool embed_json_get_string(const char *js, const embed_json_token_t *t, char *out, size_t cap) {
    if (!js || !t || !out || cap == 0 || t->type != EMBED_JSON_STRING)
        return false;
    size_t o = 0;
    for (uint32_t i = t->start; i < t->end; ++i) {
        char c = js[i];
        if (c != '\\') {
            if (o + 1 >= cap)
                return false;
            out[o++] = c;
            continue;
        }
        c = js[++i];
        if (c == 'u') {
            uint32_t cp = (uint32_t)hex4(js + i + 1);
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) { /* サロゲートペア */
                if (i + 6 < t->end + 0u && js[i + 1] == '\\' && js[i + 2] == 'u') {
                    const uint32_t lo = (uint32_t)hex4(js + i + 3);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    } else {
                        cp = 0xFFFD;
                    }
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD; /* 対のない下位サロゲート */
            }
            if (!put_utf8(out, cap, &o, cp))
                return false;
            continue;
        }
        const char *from = "\"\\/bfnrt", *to = "\"\\/\b\f\n\r\t";
        const char *at = strchr(from, c);
        if (o + 1 >= cap || !at)
            return false;
        out[o++] = to[at - from];
    }
    out[o] = '\0';
    return true;
}

bool embed_json_get_int(const char *js, const embed_json_token_t *t, int64_t *out) {
    if (!js || !t || !out || t->type != EMBED_JSON_NUMBER)
        return false;
    uint32_t i = t->start;
    const bool neg = js[i] == '-';
    if (neg)
        ++i;
    const uint64_t limit = neg ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    uint64_t v = 0;
    for (; i < t->end; ++i) {
        const char c = js[i];
        if (!is_digit(c))
            return false; /* 小数部・指数部のある数は整数として読まない */
        const uint64_t d = (uint64_t)(c - '0');
        if (v > (limit - d) / 10u)
            return false;
        v = v * 10u + d;
    }
    *out = neg ? (v == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)v) : (int64_t)v;
    return true;
}

bool embed_json_get_double(const char *js, const embed_json_token_t *t, double *out) {
    if (!js || !t || !out || t->type != EMBED_JSON_NUMBER || t->end - t->start >= 64)
        return false;
    char buf[64];
    const size_t n = t->end - t->start;
    memcpy(buf, js + t->start, n);
    buf[n] = '\0';
    char *end = NULL;
    *out = strtod(buf, &end);
    return end == buf + n;
}

bool embed_json_get_bool(const char *js, const embed_json_token_t *t, bool *out) {
    if (!js || !t || !out || t->type != EMBED_JSON_BOOL)
        return false;
    *out = js[t->start] == 't';
    return true;
}
