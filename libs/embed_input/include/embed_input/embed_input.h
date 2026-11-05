#pragma once
/*
 * embed_input: ディジタル入力のチャタリング除去とボタン操作の判定。C11、動的確保なし。
 *
 * - embed_debounce: 最大 32 点の入力 (ポートをまとめて読んだ値) を、連続して同じ値が
 *   samples 回続いたら確定する。サンプル周期 (例: 1〜5 ms) ごとに呼ぶ。
 * - embed_button: 1 つのボタンを時刻つきで更新し、押下・解放・クリック・ダブルクリック・
 *   長押し・長押し中のリピートをイベントのビットで返す。
 * 時刻は 32bit ミリ秒 (周回は考慮済み)。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 多点入力のチャタリング除去 ---------------- */
typedef struct {
    uint32_t state;   /* 確定した値 */
    uint8_t samples;  /* 確定に必要な連続回数 (1〜255) */
    uint8_t count[32];
} embed_debounce_t;

void embed_debounce_init(embed_debounce_t *d, uint8_t samples, uint32_t initial);
/* raw を 1 サンプル与え、このサンプルで確定値が変わったビットを返す */
uint32_t embed_debounce_update(embed_debounce_t *d, uint32_t raw);
/* 立ち上がり (0 -> 1) / 立ち下がり (1 -> 0) で確定したビット */
static inline uint32_t embed_debounce_rising(const embed_debounce_t *d, uint32_t changed) {
    return changed & d->state;
}
static inline uint32_t embed_debounce_falling(const embed_debounce_t *d, uint32_t changed) {
    return changed & ~d->state;
}

/* ---------------- ボタン ---------------- */
typedef struct {
    uint16_t debounce_ms;     /* この時間変化しなければ確定 (0 で即時) */
    uint16_t long_press_ms;   /* 長押しと判定する押下時間 (0 で長押しなし) */
    uint16_t double_click_ms; /* 解放から次の押下までの最大間隔 (0 でダブルクリックなし: 解放ですぐ CLICK) */
    uint16_t repeat_ms;       /* 長押し後のリピート間隔 (0 でリピートなし) */
} embed_button_config_t;

/* embed_button_update が返すイベントのビット */
#define EMBED_BUTTON_PRESSED 0x01u      /* 押下を確定した */
#define EMBED_BUTTON_RELEASED 0x02u     /* 解放を確定した */
#define EMBED_BUTTON_CLICK 0x04u        /* 短い押下 1 回 (ダブルクリックの待ちが終わった時点) */
#define EMBED_BUTTON_DOUBLE_CLICK 0x08u /* 短い押下 2 回 */
#define EMBED_BUTTON_LONG_PRESS 0x10u   /* 長押し (押している間に 1 回) */
#define EMBED_BUTTON_REPEAT 0x20u       /* 長押し中のリピート */

typedef struct {
    embed_button_config_t cfg;
    bool raw;
    uint32_t raw_since;
    bool pressed;
    uint32_t press_at;
    uint32_t release_at;
    uint32_t next_repeat;
    bool long_sent;
    bool click_pending;
} embed_button_t;

/* cfg が NULL なら既定値 (チャタリング 20 ms、長押し 800 ms、ダブルクリック 300 ms、リピートなし) */
void embed_button_init(embed_button_t *b, const embed_button_config_t *cfg, uint32_t now_ms);
/* 生の入力 (true = 押されている) と現在時刻を与え、発生したイベントのビットを返す。
 * 押している間・クリック判定待ちの間は定期的 (チャタリング時間より短い周期) に呼ぶこと。 */
uint8_t embed_button_update(embed_button_t *b, bool raw_pressed, uint32_t now_ms);
bool embed_button_is_pressed(const embed_button_t *b);
/* 押し続けている時間 (押していなければ 0) */
uint32_t embed_button_held_ms(const embed_button_t *b, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
