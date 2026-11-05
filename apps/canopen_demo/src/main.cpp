// CANopen (CiA 301) スレーブノードのデモ。
// 仮想 CAN バス上で「マスタ」がサーボドライブ (Node 5) を設定・運転する流れを再現する:
//   1. ブートアップ受信 → SDO で識別情報とデバイス名 (分割転送) を読む
//   2. SDO で TPDO1 のマッピングを設定 (無効化 → マッピング → 有効化: マスタの標準手順)
//   3. NMT Start → SYNC ごとに TPDO1 (位置 + ステータス) を受信
//   4. RPDO1 で目標速度を送る (書き込みフックが範囲外を拒否)
//   5. マスタの Heartbeat が途絶 → ノードが EMCY を出して安全停止
#include "embed_canopen/embed_canopen.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
const uint8_t kNode = 5;
const uint8_t kMaster = 1;

void print_frame(const char *dir, const can_frame_t &f) {
    std::printf("  %s %03X [%u]", dir, (unsigned)f.id, (unsigned)f.len);
    for (int i = 0; i < f.len; ++i)
        std::printf(" %02X", f.data[i]);
    std::printf("\n");
}

can_frame_t make(uint32_t id, std::initializer_list<uint8_t> bytes) {
    can_frame_t f{};
    f.id = id;
    f.len = (uint8_t)bytes.size();
    std::memcpy(f.data, bytes.begin(), bytes.size());
    return f;
}

/* ドライブ (スレーブ) 側のアプリケーション */
struct Drive {
    embed_canopen::Node node{kNode, "ServoDrive-CiA402"};
    int32_t actual_position = 0;
    uint16_t statusword = 0x0237; // Operation enabled
    int16_t target_velocity = 0;
    bool safe_stop = false;

    static uint32_t check_velocity(const canopen_od_entry_t *, const void *data, size_t, void *) {
        int16_t v;
        std::memcpy(&v, data, sizeof(v));
        return (v < -3000 || v > 3000) ? CANOPEN_SDO_ABORT_VALUE_RANGE : 0;
    }

    Drive() {
        node.add_variable(0x6064, 0x00, actual_position, OD_ACCESS_RO);
        node.add_variable(0x6041, 0x00, statusword, OD_ACCESS_RO);
        node.add_variable(0x60FF, 0x00, target_velocity, OD_ACCESS_RW);
        canopen_od_set_write_hook(&node.raw_handle()->od, 0x60FF, 0, check_velocity, nullptr);
        node.config_rpdo(1, {{0x60FF, 0, 16}}, 254);
        node.add_heartbeat_consumer(kMaster, 300);
        canopen_node_callbacks_t cb{};
        cb.on_heartbeat_timeout = [](canopen_node_t *, uint8_t id, void *user) {
            std::printf("  [drive] master %u heartbeat lost -> safe stop\n", (unsigned)id);
            auto *d = static_cast<Drive *>(user);
            d->safe_stop = true;
            d->target_velocity = 0;
        };
        canopen_node_set_callbacks(node.raw_handle(), &cb, this);
    }

    void control_step(uint32_t ms) { // 速度指令に従って位置を進める
        actual_position += target_velocity * (int32_t)ms / 1000;
    }
};

/* 仮想バス: ドライブが出したフレームを集める */
struct Bus {
    Drive &drive;
    std::vector<can_frame_t> from_drive;
    void master_send(const can_frame_t &f) {
        print_frame("M->", f);
        drive.node.handle(f, [this](const can_frame_t &tx) { deliver(tx); });
    }
    void tick(uint32_t ms) {
        drive.control_step(ms);
        drive.node.tick(ms, [this](const can_frame_t &tx) { deliver(tx); });
    }
    void deliver(const can_frame_t &tx) {
        print_frame("<-D", tx);
        from_drive.push_back(tx);
    }
    /* SDO 要求を送り、応答を返す */
    can_frame_t sdo(std::initializer_list<uint8_t> req) {
        from_drive.clear();
        master_send(make(0x600 + kNode, req));
        return from_drive.empty() ? can_frame_t{} : from_drive.back();
    }
};

uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
} // namespace

int main() {
    std::printf("=====================================================\n");
    std::printf(" CANopen (CiA 301) slave demo - embed_canopen\n");
    std::printf("=====================================================\n");
    Drive drive;
    Bus bus{drive, {}};
    int failures = 0;

    std::printf("\n[1] boot-up and identity\n");
    bus.tick(0); // 送信キューにあるブートアップを出す
    can_frame_t r = bus.sdo({0x40, 0x18, 0x10, 0x01, 0, 0, 0, 0}); // 0x1018.1 Vendor ID
    std::printf("  vendor id = 0x%08X\n", (unsigned)le32(r.data + 4));
    r = bus.sdo({0x40, 0x08, 0x10, 0x00, 0, 0, 0, 0}); // 0x1008 デバイス名 (分割転送)
    const uint32_t name_len = le32(r.data + 4);
    char name[64] = {};
    uint8_t toggle = 0;
    for (uint32_t got = 0; got < name_len && got < sizeof(name) - 1;) {
        r = bus.sdo({(uint8_t)(0x60 | (toggle << 4)), 0, 0, 0, 0, 0, 0, 0});
        const uint32_t k = 7u - ((r.data[0] >> 1) & 7u);
        std::memcpy(name + got, r.data + 1, k);
        got += k;
        toggle ^= 1;
    }
    std::printf("  device name = \"%s\" (%u bytes, segmented)\n", name, (unsigned)name_len);
    failures += std::strcmp(name, "ServoDrive-CiA402") != 0;

    std::printf("\n[2] master maps TPDO1 = {0x6064 position, 0x6041 statusword}, every SYNC\n");
    bus.sdo({0x23, 0x00, 0x18, 0x01, 0x85, 0x01, 0x00, 0x80}); // COB-ID 無効化
    bus.sdo({0x2F, 0x00, 0x1A, 0x00, 0, 0, 0, 0});             // マッピング数 0
    bus.sdo({0x23, 0x00, 0x1A, 0x01, 0x20, 0x00, 0x64, 0x60}); // 6064.00, 32bit
    bus.sdo({0x23, 0x00, 0x1A, 0x02, 0x10, 0x00, 0x41, 0x60}); // 6041.00, 16bit
    bus.sdo({0x2F, 0x00, 0x1A, 0x00, 2, 0, 0, 0});             // マッピング数 2
    bus.sdo({0x2F, 0x00, 0x18, 0x02, 1, 0, 0, 0});             // 送信種別: SYNC ごと
    r = bus.sdo({0x23, 0x00, 0x18, 0x01, 0x85, 0x01, 0x00, 0x00}); // 有効化
    failures += r.data[0] != 0x60;

    std::printf("\n[3] NMT start, master heartbeat, SYNC cycle with velocity command\n");
    bus.master_send(make(0x000, {0x01, kNode}));
    bus.master_send(make(0x700 + kMaster, {0x05})); // マスタの Heartbeat
    bus.master_send(make(0x205, {0xF4, 0x01}));      // RPDO1: 目標速度 500
    std::printf("  target velocity = %d\n", drive.target_velocity);
    for (int cycle = 0; cycle < 3; ++cycle) {
        bus.tick(100);
        bus.master_send(make(0x700 + kMaster, {0x05}));
        bus.from_drive.clear();
        bus.master_send(make(0x080, {}));
        for (const auto &f : bus.from_drive)
            if (f.id == 0x185)
                std::printf("  TPDO1: position=%d status=0x%04X\n", (int)le32(f.data),
                            (unsigned)(f.data[4] | (f.data[5] << 8)));
    }
    failures += drive.actual_position != 150;

    std::printf("\n[4] out-of-range velocity over SDO is rejected by the write hook\n");
    r = bus.sdo({0x2B, 0xFF, 0x60, 0x00, 0x70, 0x17, 0, 0}); // 6000
    std::printf("  abort code = 0x%08X (value range exceeded)\n", (unsigned)le32(r.data + 4));
    failures += le32(r.data + 4) != CANOPEN_SDO_ABORT_VALUE_RANGE;

    std::printf("\n[5] master heartbeat stops -> EMCY 0x8130 and safe stop\n");
    for (int i = 0; i < 4; ++i)
        bus.tick(100);
    std::printf("  safe_stop=%s velocity=%d error_register=0x%02X\n", drive.safe_stop ? "yes" : "no",
                drive.target_velocity, drive.node.raw_handle()->error_register);
    failures += !drive.safe_stop;

    std::printf("\n%s\n", failures ? "demo FAILED" : "demo completed successfully");
    return failures ? 1 : 0;
}
