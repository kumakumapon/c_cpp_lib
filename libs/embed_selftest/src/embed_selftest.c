#include "embed_selftest/embed_selftest.h"
#include <string.h>
bool embed_selftest_march_c(volatile uint32_t *ram, size_t n) {
    if (!ram || !n)
        return false;
    for (size_t i = 0; i < n; ++i)
        ram[i] = 0;
    for (size_t i = 0; i < n; ++i) {
        if (ram[i] != 0)
            return false;
        ram[i] = UINT32_MAX;
    }
    for (size_t i = 0; i < n; ++i) {
        if (ram[i] != UINT32_MAX)
            return false;
        ram[i] = 0;
    }
    for (size_t i = n; i-- > 0;) {
        if (ram[i] != 0)
            return false;
        ram[i] = UINT32_MAX;
    }
    for (size_t i = n; i-- > 0;) {
        if (ram[i] != UINT32_MAX)
            return false;
        ram[i] = 0;
    }
    for (size_t i = 0; i < n; ++i)
        if (ram[i] != 0)
            return false;
    return true;
}
bool embed_selftest_rom(const void *rom, size_t n, uint32_t expected) {
    return rom && n && embed_crc(EMBED_CRC32_IEEE, rom, n) == expected;
}

static uint32_t ram_read(void *ctx, size_t i) { return ((volatile uint32_t *)ctx)[i]; }
static void ram_write(void *ctx, size_t i, uint32_t v) { ((volatile uint32_t *)ctx)[i] = v; }
void embed_selftest_ram_io(embed_selftest_mem_io *io, volatile uint32_t *ram) {
    if (!io)
        return;
    io->read = ram_read;
    io->write = ram_write;
    io->ctx = (void *)(uintptr_t)ram;
}
static bool io_valid(const embed_selftest_mem_io *io) { return io && io->read && io->write; }
static bool fail_at(size_t *out, size_t index) {
    if (out)
        *out = index;
    return false;
}

/* March C-: {⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)} */
bool embed_selftest_march_c_io(const embed_selftest_mem_io *io, size_t n, size_t *fail_index) {
    if (!io_valid(io) || !n)
        return false;
    const uint32_t zero = 0, one = UINT32_MAX;
    for (size_t i = 0; i < n; ++i)
        io->write(io->ctx, i, zero);
    for (size_t i = 0; i < n; ++i) {
        if (io->read(io->ctx, i) != zero)
            return fail_at(fail_index, i);
        io->write(io->ctx, i, one);
    }
    for (size_t i = 0; i < n; ++i) {
        if (io->read(io->ctx, i) != one)
            return fail_at(fail_index, i);
        io->write(io->ctx, i, zero);
    }
    for (size_t i = n; i-- > 0;) {
        if (io->read(io->ctx, i) != zero)
            return fail_at(fail_index, i);
        io->write(io->ctx, i, one);
    }
    for (size_t i = n; i-- > 0;) {
        if (io->read(io->ctx, i) != one)
            return fail_at(fail_index, i);
        io->write(io->ctx, i, zero);
    }
    for (size_t i = 0; i < n; ++i)
        if (io->read(io->ctx, i) != zero)
            return fail_at(fail_index, i);
    return true;
}

bool embed_selftest_data_bus(const embed_selftest_mem_io *io, size_t index, uint32_t *failed) {
    if (!io_valid(io))
        return false;
    for (uint32_t pattern = 1; pattern != 0; pattern <<= 1) {
        io->write(io->ctx, index, pattern);
        if (io->read(io->ctx, index) != pattern) {
            if (failed)
                *failed = pattern;
            return false;
        }
    }
    return true;
}

bool embed_selftest_address_bus(const embed_selftest_mem_io *io, size_t n, size_t *fail_index) {
    if (!io_valid(io) || n < 2)
        return false;
    const uint32_t pattern = 0xAAAAAAAAu, anti = 0x55555555u;
    for (size_t off = 1; off < n; off <<= 1)
        io->write(io->ctx, off, pattern);
    /* 固着 (High): 0 番地への書き込みが他の 2^k 番地に現れないこと */
    io->write(io->ctx, 0, anti);
    for (size_t off = 1; off < n; off <<= 1)
        if (io->read(io->ctx, off) != pattern)
            return fail_at(fail_index, off);
    io->write(io->ctx, 0, pattern);
    /* 固着 (Low)・短絡: 2^k 番地への書き込みが 0 番地や他の 2^j 番地に現れないこと */
    for (size_t test = 1; test < n; test <<= 1) {
        io->write(io->ctx, test, anti);
        if (io->read(io->ctx, 0) != pattern)
            return fail_at(fail_index, test);
        for (size_t off = 1; off < n; off <<= 1)
            if (off != test && io->read(io->ctx, off) != pattern)
                return fail_at(fail_index, off);
        io->write(io->ctx, test, pattern);
    }
    return true;
}

bool embed_selftest_rom_begin(embed_selftest_rom_ctx *ctx, const void *rom, size_t len,
                              uint32_t expected) {
    if (!ctx || !rom || !len)
        return false;
    ctx->rom = (const uint8_t *)rom;
    ctx->len = len;
    ctx->pos = 0;
    ctx->state = embed_crc_init(EMBED_CRC32_IEEE);
    ctx->expected = expected;
    return true;
}

embed_selftest_status embed_selftest_rom_step(embed_selftest_rom_ctx *ctx, size_t chunk) {
    if (!ctx || !ctx->rom || !ctx->len || !chunk)
        return EMBED_SELFTEST_FAIL;
    size_t n = ctx->len - ctx->pos < chunk ? ctx->len - ctx->pos : chunk;
    ctx->state = embed_crc_update(EMBED_CRC32_IEEE, ctx->state, ctx->rom + ctx->pos, n);
    ctx->pos += n;
    if (ctx->pos < ctx->len)
        return EMBED_SELFTEST_BUSY;
    const uint32_t crc = embed_crc_finish(EMBED_CRC32_IEEE, ctx->state);
    ctx->pos = 0;
    ctx->state = embed_crc_init(EMBED_CRC32_IEEE);
    return crc == ctx->expected ? EMBED_SELFTEST_PASS : EMBED_SELFTEST_FAIL;
}
void embed_selftest_stack_fill(void *p, size_t n, uint8_t marker) {
    if (p)
        memset(p, marker, n);
}
size_t embed_selftest_stack_unused(const void *p, size_t n, uint8_t marker, bool down) {
    if (!p)
        return 0;
    const uint8_t *b = (const uint8_t *)p;
    size_t i = 0;
    while (i < n && b[down ? i : n - 1 - i] == marker)
        ++i;
    return i;
}
bool embed_selftest_cpu(embed_selftest_cpu_fn fn, void *user) { return fn && fn(user); }
bool embed_selftest_clock(uint64_t measured, uint64_t expected, uint32_t ppm) {
    if (!expected || ppm > 1000000)
        return false;
    uint64_t delta = measured > expected ? measured - expected : expected - measured;
    uint64_t allowance = (expected / 1000000) * ppm + ((expected % 1000000) * ppm) / 1000000;
    return delta <= allowance;
}
bool embed_watchdog_init(embed_watchdog *w, uint64_t now, uint64_t min, uint64_t max,
                         void (*feed)(void *), void *user) {
    if (!w || !feed || !max || min > max)
        return false;
    *w = (embed_watchdog){now, min, max, feed, user, false};
    return true;
}
bool embed_watchdog_poll(embed_watchdog *w, uint64_t now, bool healthy) {
    if (!w || !w->feed || w->fault)
        return false;
    if (!healthy || now < w->last || now - w->last > w->max_interval) {
        w->fault = true;
        return false;
    }
    if (now - w->last < w->min_interval)
        return false;
    w->feed(w->user);
    w->last = now;
    return true;
}
