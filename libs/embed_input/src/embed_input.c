#include "embed_input/embed_input.h"
#include <string.h>

void embed_debounce_init(embed_debounce_t *d, uint8_t samples, uint32_t initial) {
    if (!d)
        return;
    memset(d, 0, sizeof(*d));
    d->samples = samples ? samples : 1;
    d->state = initial;
}

uint32_t embed_debounce_update(embed_debounce_t *d, uint32_t raw) {
    if (!d)
        return 0;
    uint32_t changed = 0;
    uint32_t diff = raw ^ d->state;
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t bit = 1u << i;
        if (!(diff & bit)) {
            d->count[i] = 0; /* 確定値と同じに戻ったらやり直し */
            continue;
        }
        if (++d->count[i] >= d->samples) {
            d->count[i] = 0;
            d->state ^= bit;
            changed |= bit;
        }
    }
    return changed;
}

void embed_button_init(embed_button_t *b, const embed_button_config_t *cfg, uint32_t now_ms) {
    if (!b)
        return;
    memset(b, 0, sizeof(*b));
    if (cfg) {
        b->cfg = *cfg;
    } else {
        b->cfg.debounce_ms = 20;
        b->cfg.long_press_ms = 800;
        b->cfg.double_click_ms = 300;
        b->cfg.repeat_ms = 0;
    }
    b->raw_since = now_ms;
}

static bool elapsed(uint32_t now, uint32_t since, uint32_t span) {
    return (uint32_t)(now - since) >= span;
}

uint8_t embed_button_update(embed_button_t *b, bool raw, uint32_t now) {
    if (!b)
        return 0;
    uint8_t ev = 0;
    if (raw != b->raw) {
        b->raw = raw;
        b->raw_since = now;
    }
    /* 確定: 生の値が debounce_ms 変わらなかった */
    if (b->raw != b->pressed && elapsed(now, b->raw_since, b->cfg.debounce_ms)) {
        b->pressed = b->raw;
        if (b->pressed) {
            ev |= EMBED_BUTTON_PRESSED;
            b->press_at = now;
            b->long_sent = false;
        } else {
            ev |= EMBED_BUTTON_RELEASED;
            if (!b->long_sent) {
                if (b->cfg.double_click_ms == 0) {
                    ev |= EMBED_BUTTON_CLICK;
                } else if (b->click_pending) {
                    ev |= EMBED_BUTTON_DOUBLE_CLICK;
                    b->click_pending = false;
                } else {
                    b->click_pending = true;
                    b->release_at = now;
                }
            }
        }
    }
    if (b->pressed) {
        if (b->cfg.long_press_ms && !b->long_sent && elapsed(now, b->press_at, b->cfg.long_press_ms)) {
            b->long_sent = true;
            ev |= EMBED_BUTTON_LONG_PRESS;
            b->next_repeat = b->press_at + b->cfg.long_press_ms + b->cfg.repeat_ms;
            if (b->click_pending) { /* クリックの後の長押し: 先のクリックを確定させる */
                b->click_pending = false;
                ev |= EMBED_BUTTON_CLICK;
            }
        } else if (b->long_sent && b->cfg.repeat_ms && (int32_t)(now - b->next_repeat) >= 0) {
            ev |= EMBED_BUTTON_REPEAT;
            b->next_repeat += b->cfg.repeat_ms;
            if ((int32_t)(now - b->next_repeat) >= 0) /* 呼び出しが遅れても溜め込まない */
                b->next_repeat = now + b->cfg.repeat_ms;
        }
    } else if (b->click_pending && elapsed(now, b->release_at, b->cfg.double_click_ms)) {
        b->click_pending = false;
        ev |= EMBED_BUTTON_CLICK;
    }
    return ev;
}

bool embed_button_is_pressed(const embed_button_t *b) { return b && b->pressed; }

uint32_t embed_button_held_ms(const embed_button_t *b, uint32_t now_ms) {
    return b && b->pressed ? now_ms - b->press_at : 0;
}
