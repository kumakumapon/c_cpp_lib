#include <embed_bridge/embed_bridge.hpp>
#include <embed_fixed/embed_fixed.hpp>
#include <embed_selftest/embed_selftest.h>
#include <embed_modbus_tcp/embed_modbus_tcp.h>
#include <embed_sched/embed_sched.h>
#include <uart_protocol/uart_crc.h>
int main() {embed_sched s;embed_sched_init(&s,0,nullptr,nullptr);return uart_crc16_ccitt((const uint8_t*)"123456789",9,0xffff)==0x29b1 && embed_q15_mul(16384,16384)==8192 && embed_selftest_rom("123456789",9,0xcbf43926) ? 0 : 1;}
