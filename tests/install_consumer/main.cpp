#include <embed_bridge/embed_bridge.hpp>
#include <embed_fixed/embed_fixed.hpp>
#include <embed_modbus_tcp/embed_modbus_tcp.h>
#include <embed_sched/embed_sched.h>
#include <embed_selftest/embed_selftest.h>
#include <embed_cli/embed_cli.h>
#include <embed_frame/embed_frame.h>
#include <embed_input/embed_input.h>
#include <embed_json/embed_json.h>
#include <embed_nvparam/embed_nvparam.h>
#include <uart_protocol/uart_crc.h>
int main() {
    embed_sched s;
    embed_sched_init(&s, 0, nullptr, nullptr);
    uint8_t enc[8];
    size_t n = 0;
    const uint8_t raw[] = {1, 0, 2};
    char json[32];
    embed_json_writer_t w;
    embed_json_writer_init(&w, json, sizeof(json));
    embed_json_array_begin(&w);
    embed_json_int(&w, 1);
    embed_json_array_end(&w);
    if (!embed_cobs_encode(raw, 3, enc, sizeof(enc), &n) || n != 4 || !embed_json_finish(&w, nullptr))
        return 2;
    return uart_crc16_ccitt((const uint8_t *)"123456789", 9, 0xffff) == 0x29b1 &&
                   embed_q15_mul(16384, 16384) == 8192 &&
                   embed_selftest_rom("123456789", 9, 0xcbf43926)
               ? 0
               : 1;
}
