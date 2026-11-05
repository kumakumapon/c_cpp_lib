// 実際の TCP ソケットで動く Modbus TCP のデモ (Linux / macOS / Windows)。
//
//   modbus_tcp_demo                 127.0.0.1 の空きポートでサーバを起動し、同じプロセスの
//                                   クライアントから読み書きして結果を検査する (CI 用)
//   modbus_tcp_demo --serve [port]  サーバとして待ち受ける (既定 5020)。mbpoll などで試せる
//                                   例: mbpoll -a 1 -r 1 -c 4 -p 5020 127.0.0.1
//
// ソケットの扱いはアプリ側、embed_modbus_tcp はフレーミング・MBAP・トランザクション管理だけを担う。
#include "embed_modbus/modbus_master.h"
#include "embed_modbus_tcp/embed_modbus_tcp.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kInvalid = INVALID_SOCKET;
static void close_sock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using sock_t = int;
static const sock_t kInvalid = -1;
static void close_sock(sock_t s) { close(s); }
#endif

namespace {
/* ---------------- サーバ側の装置モデル ---------------- */
struct Device {
    std::mutex lock; // 複数の接続から同じレジスタに触れるので直列化する
    uint16_t holding[16] = {};
    uint16_t input[4] = {231, 650, 1013, 0}; // 温度 23.1 ℃ / 湿度 65.0 % / 気圧 1013 hPa / 状態
};

modbus_exception_t read_holding(uint16_t addr, uint16_t count, uint16_t *dst, void *user) {
    auto *d = static_cast<Device *>(user);
    if ((size_t)addr + count > 16)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    std::lock_guard<std::mutex> g(d->lock);
    std::memcpy(dst, d->holding + addr, count * sizeof(uint16_t));
    return MODBUS_EX_NONE;
}
modbus_exception_t write_holding(uint16_t addr, uint16_t count, const uint16_t *src, void *user) {
    auto *d = static_cast<Device *>(user);
    if ((size_t)addr + count > 16)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    std::lock_guard<std::mutex> g(d->lock);
    std::memcpy(d->holding + addr, src, count * sizeof(uint16_t));
    return MODBUS_EX_NONE;
}
modbus_exception_t read_input(uint16_t addr, uint16_t count, uint16_t *dst, void *user) {
    auto *d = static_cast<Device *>(user);
    if ((size_t)addr + count > 4)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    std::lock_guard<std::mutex> g(d->lock);
    std::memcpy(dst, d->input + addr, count * sizeof(uint16_t));
    return MODBUS_EX_NONE;
}

void send_all(const uint8_t *data, size_t len, void *user) {
    const sock_t s = *static_cast<sock_t *>(user);
    while (len) {
        const int n = (int)send(s, (const char *)data, (int)len, 0);
        if (n <= 0)
            return;
        data += n;
        len -= (size_t)n;
    }
}

/* 1 接続を処理する。不正な MBAP を受けたら切断する (状態はソケットごと) */
void serve_connection(sock_t client, const modbus_slave_t *slave) {
    embed_modbus_tcp_connection conn{};
    uint8_t buf[512];
    for (;;) {
        const int n = (int)recv(client, (char *)buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        if (!embed_modbus_tcp_feed(&conn, slave, buf, (size_t)n, send_all, &client)) {
            std::printf("[server] malformed MBAP, closing connection\n");
            break;
        }
    }
    close_sock(client);
}

sock_t listen_on(const char *addr, uint16_t port, uint16_t *bound) {
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid)
        return kInvalid;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, addr, &sa.sin_addr);
    if (bind(s, (sockaddr *)&sa, sizeof(sa)) != 0 || listen(s, 4) != 0) {
        close_sock(s);
        return kInvalid;
    }
    socklen_t len = sizeof(sa);
    getsockname(s, (sockaddr *)&sa, &len);
    *bound = ntohs(sa.sin_port);
    return s;
}

/* ---------------- クライアント側 ---------------- */
uint32_t now_ms() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Client {
    sock_t sock = kInvalid;
    embed_modbus_tcp_connection conn{};
    embed_modbus_transactions transactions{};
    uint8_t unit = EMBED_MODBUS_TCP_UNIT_DIRECT;
    uint8_t last_adu[EMBED_MODBUS_TCP_MAX_ADU];
    size_t last_len = 0;

    static void on_adu(const uint8_t *adu, size_t len, void *user) {
        auto *c = static_cast<Client *>(user);
        std::memcpy(c->last_adu, adu, len);
        c->last_len = len;
    }

    /* 要求 PDU を送り、応答 PDU を out へ。戻り値は parse_response の結果 */
    modbus_status_t transact(const uint8_t *pdu, size_t pdu_len, uint8_t *out, size_t *out_len) {
        uint16_t tid = 0;
        if (!embed_modbus_transaction_begin_at(&transactions, now_ms(), &tid))
            return MODBUS_ERR_BUFFER_OVERFLOW;
        uint8_t adu[EMBED_MODBUS_TCP_MAX_ADU];
        const size_t n = embed_modbus_tcp_build_request(tid, unit, pdu, pdu_len, adu, sizeof(adu));
        send_all(adu, n, &sock);
        last_len = 0;
        uint8_t buf[512];
        while (last_len == 0) {
            const int r = (int)recv(sock, (char *)buf, sizeof(buf), 0);
            if (r <= 0) { // 受信タイムアウトまたは切断
                embed_modbus_transaction_expire(&transactions, now_ms(), 0, nullptr, 0);
                return MODBUS_ERR_TIMEOUT;
            }
            if (!embed_modbus_tcp_frame(&conn, buf, (size_t)r, on_adu, this))
                return MODBUS_ERR_INVALID_RESPONSE;
        }
        const uint8_t *rpdu = nullptr;
        size_t rlen = 0;
        const modbus_status_t st =
            embed_modbus_tcp_parse_response(last_adu, last_len, tid, unit, &rpdu, &rlen);
        embed_modbus_transaction_end(&transactions, tid);
        if (st == MODBUS_OK) {
            std::memcpy(out, rpdu, rlen);
            *out_len = rlen;
        }
        return st;
    }
};

sock_t connect_to(const char *addr, uint16_t port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid)
        return kInvalid;
#ifdef _WIN32
    DWORD tv = 2000;
#else
    timeval tv{2, 0};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)); // CI で固まらないように
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, addr, &sa.sin_addr);
    if (connect(s, (sockaddr *)&sa, sizeof(sa)) != 0) {
        close_sock(s);
        return kInvalid;
    }
    return s;
}

int run_self_test(Device &device, const modbus_slave_t &slave) {
    uint16_t port = 0;
    const sock_t listener = listen_on("127.0.0.1", 0, &port);
    if (listener == kInvalid) {
        std::printf("cannot listen on 127.0.0.1\n");
        return 1;
    }
    std::printf("[server] listening on 127.0.0.1:%u\n", (unsigned)port);
    std::thread server([listener, &slave] {
        const sock_t c = accept(listener, nullptr, nullptr);
        if (c != kInvalid)
            serve_connection(c, &slave);
    });

    Client client;
    client.sock = connect_to("127.0.0.1", port);
    int failures = 0;
    if (client.sock == kInvalid) {
        std::printf("cannot connect\n");
        close_sock(listener);
        server.join();
        return 1;
    }
    uint8_t pdu[MODBUS_MAX_PDU_LENGTH], resp[MODBUS_MAX_PDU_LENGTH];
    size_t pdu_len = 0, resp_len = 0;

    // FC16: 設定値を書く
    const uint16_t setpoints[3] = {215, 1500, 1};
    modbus_master_req_write_multiple_registers(0, 3, setpoints, pdu, &pdu_len);
    modbus_status_t st = client.transact(pdu, pdu_len, resp, &resp_len);
    st = st == MODBUS_OK ? modbus_master_parse_write_resp(resp, resp_len, MODBUS_FC_WRITE_MULTIPLE_REGISTERS, 0) : st;
    std::printf("[client] FC16 write 3 setpoints      -> %s\n", st == MODBUS_OK ? "OK" : "FAILED");
    failures += st != MODBUS_OK;

    // FC03: 読み戻す
    uint16_t back[3] = {};
    modbus_master_req_read_holding_registers(0, 3, pdu, &pdu_len);
    st = client.transact(pdu, pdu_len, resp, &resp_len);
    st = st == MODBUS_OK ? modbus_master_parse_read_registers_resp(resp, resp_len, back, 3) : st;
    std::printf("[client] FC03 read back              -> %u %u %u\n", back[0], back[1], back[2]);
    failures += st != MODBUS_OK || back[0] != 215 || back[1] != 1500 || back[2] != 1;

    // FC04: センサ値
    uint16_t sensors[3] = {};
    modbus_master_req_read_input_registers(0, 3, pdu, &pdu_len);
    st = client.transact(pdu, pdu_len, resp, &resp_len);
    st = st == MODBUS_OK ? modbus_master_parse_read_registers_resp(resp, resp_len, sensors, 3) : st;
    std::printf("[client] FC04 sensors                -> %.1f C, %.1f %%, %u hPa\n", sensors[0] / 10.0,
                sensors[1] / 10.0, sensors[2]);
    failures += st != MODBUS_OK || sensors[2] != 1013;

    // 範囲外アドレス: 例外応答 0x83 / 0x02
    modbus_master_req_read_holding_registers(14, 4, pdu, &pdu_len);
    st = client.transact(pdu, pdu_len, resp, &resp_len);
    const bool exception = st == MODBUS_OK && resp[0] == 0x83 && resp[1] == MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    std::printf("[client] FC03 out of range           -> exception 0x%02X code %u\n", resp[0], resp[1]);
    failures += !exception;

    std::printf("[client] pending transactions: %zu\n",
                embed_modbus_transaction_pending(&client.transactions));
    {
        std::lock_guard<std::mutex> g(device.lock);
        failures += device.holding[1] != 1500;
    }
    close_sock(client.sock);
    server.join();
    close_sock(listener);
    std::printf("%s\n", failures ? "demo FAILED" : "demo completed successfully");
    return failures ? 1 : 0;
}

int run_server(const modbus_slave_t &slave, uint16_t port) {
    uint16_t bound = 0;
    const sock_t listener = listen_on("0.0.0.0", port, &bound);
    if (listener == kInvalid) {
        std::printf("cannot listen on port %u\n", (unsigned)port);
        return 1;
    }
    std::printf("Modbus TCP server on port %u (unit 1 or 255). Ctrl-C to stop.\n", (unsigned)bound);
    std::printf("  holding registers 0..15 (read/write), input registers 0..3 (sensors)\n");
    for (;;) {
        const sock_t c = accept(listener, nullptr, nullptr);
        if (c == kInvalid)
            continue;
        std::thread([c, &slave] { serve_connection(c, &slave); }).detach();
    }
}
} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 1;
#endif
    static Device device;
    modbus_slave_callbacks_t cb{};
    cb.read_holding_registers = read_holding;
    cb.write_holding_registers = write_holding;
    cb.read_input_registers = read_input;
    modbus_slave_t slave{};
    modbus_slave_init(&slave, 1, &cb, &device);

    int rc;
    if (argc > 1 && std::strcmp(argv[1], "--serve") == 0)
        rc = run_server(slave, (uint16_t)(argc > 2 ? std::atoi(argv[2]) : 5020));
    else if (argc > 1)
        rc = (std::printf("usage: %s [--serve [port]]\n", argv[0]), 2);
    else
        rc = run_self_test(device, slave);
#ifdef _WIN32
    WSACleanup();
#endif
    return rc;
}
