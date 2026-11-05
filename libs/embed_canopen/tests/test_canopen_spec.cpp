// CiA 301 のフレーム配置でノードの振る舞いを固定する。期待値のバイト列は規格の定義
// (SDO のコマンド指定子・アボートコード、EMCY・Heartbeat・PDO の COB-ID とリトルエンディアン) から作る。
#include "embed_canopen/canopen_node.h"
#include "embed_canopen/embed_canopen.hpp"
#include "embed_test/embed_test.hpp"
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
can_frame_t frame(uint32_t id, std::initializer_list<uint8_t> bytes) {
    can_frame_t f{};
    f.id = id;
    f.len = (uint8_t)bytes.size();
    size_t i = 0;
    for (uint8_t b : bytes)
        f.data[i++] = b;
    return f;
}
std::vector<can_frame_t> drain(canopen_node_t &n) {
    std::vector<can_frame_t> out;
    can_frame_t f{};
    while (canopen_node_pop_tx(&n, &f))
        out.push_back(f);
    return out;
}
/* フレームを渡し、生成されたフレームを全部返す */
std::vector<can_frame_t> feed(canopen_node_t &n, const can_frame_t &rx) {
    std::vector<can_frame_t> out;
    can_frame_t f{};
    if (canopen_node_process_frame(&n, &rx, &f)) {
        out.push_back(f);
        auto rest = drain(n);
        out.insert(out.end(), rest.begin(), rest.end());
    }
    return out;
}
std::vector<can_frame_t> tick(canopen_node_t &n, uint32_t ms) {
    std::vector<can_frame_t> out;
    can_frame_t f{};
    if (canopen_node_timer_tick(&n, ms, &f)) {
        out.push_back(f);
        auto rest = drain(n);
        out.insert(out.end(), rest.begin(), rest.end());
    }
    return out;
}
/* SDO 要求 (8 バイト) を送り、応答を 1 つ返す */
can_frame_t sdo(canopen_node_t &n, std::initializer_list<uint8_t> bytes) {
    auto out = feed(n, frame(0x600 + n.node_id, bytes));
    can_frame_t none{};
    if (!CHECK_EQ(out.size(), 1u))
        return none;
    CHECK_EQ(out[0].id, 0x580u + n.node_id);
    CHECK_EQ(out[0].len, 8u);
    return out[0];
}
bool bytes_are(const can_frame_t &f, std::initializer_list<uint8_t> expect) {
    if (f.len != expect.size())
        return CHECK_EQ((size_t)f.len, expect.size());
    std::vector<uint8_t> e(expect);
    return CHECK_MEM_EQ(f.data, e.data(), e.size());
}
void abort_is(const can_frame_t &f, uint16_t idx, uint8_t sub, uint32_t code) {
    bytes_are(f, {0x80, (uint8_t)idx, (uint8_t)(idx >> 8), sub, (uint8_t)code, (uint8_t)(code >> 8),
                  (uint8_t)(code >> 16), (uint8_t)(code >> 24)});
}
void fresh(canopen_node_t &n, uint8_t id = 5, const char *name = "ServoDrive-CiA402") {
    canopen_node_init(&n, id, name);
    drain(n); // ブートアップ
}
void start(canopen_node_t &n) { feed(n, frame(0x000, {0x01, n.node_id})); }
} // namespace

/* ================= NMT ================= */

TEST_CASE(bootup_message_after_init) {
    canopen_node_t n;
    canopen_node_init(&n, 5, "x");
    CHECK_EQ(n.nmt_state, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
    auto out = drain(n);
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].id, 0x705u);
    bytes_are(out[0], {0x00});
    canopen_node_t bad;
    canopen_node_init(&bad, 0, nullptr); // 範囲外の ID は 1
    CHECK_EQ(bad.node_id, 1u);
    canopen_node_init(&bad, 128, nullptr);
    CHECK_EQ(bad.node_id, 1u);
}

namespace {
struct NmtLog {
    std::vector<canopen_nmt_state_t> states;
    std::vector<canopen_nmt_cmd_t> resets;
};
} // namespace

TEST_CASE(nmt_state_machine) {
    canopen_node_t n;
    fresh(n);
    NmtLog log;
    canopen_node_callbacks_t cb{};
    cb.on_nmt_change = [](canopen_node_t *, canopen_nmt_state_t s, void *u) {
        static_cast<NmtLog *>(u)->states.push_back(s);
    };
    cb.on_nmt_reset = [](canopen_node_t *, canopen_nmt_cmd_t c, void *u) {
        static_cast<NmtLog *>(u)->resets.push_back(c);
    };
    canopen_node_set_callbacks(&n, &cb, &log);
    feed(n, frame(0x000, {0x01, 0x06})); // 他ノード宛て
    CHECK_EQ(n.nmt_state, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
    feed(n, frame(0x000, {0x01, 0x05, 0x00})); // DLC 3 は不正
    CHECK_EQ(n.nmt_state, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
    feed(n, frame(0x000, {0x01, 0x00})); // 全ノード宛て
    CHECK_EQ(n.nmt_state, CANOPEN_NMT_STATE_OPERATIONAL);
    feed(n, frame(0x000, {0x02, 0x05}));
    CHECK_EQ(n.nmt_state, CANOPEN_NMT_STATE_STOPPED);
    feed(n, frame(0x000, {0x80, 0x05}));
    CHECK_EQ(n.nmt_state, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
    feed(n, frame(0x000, {0x80, 0x05})); // 同じ状態: 通知しない
    auto out = feed(n, frame(0x000, {0x82, 0x05})); // Reset Communication
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].id, 0x705u);
    bytes_are(out[0], {0x00}); // ブートアップ
    CHECK_EQ(n.nmt_state, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
    // リセット後は Initialising を経て Pre-Operational に入るので、最後にもう一度通知される
    const canopen_nmt_state_t expect[] = {CANOPEN_NMT_STATE_OPERATIONAL, CANOPEN_NMT_STATE_STOPPED,
                                          CANOPEN_NMT_STATE_PRE_OPERATIONAL,
                                          CANOPEN_NMT_STATE_PRE_OPERATIONAL};
    REQUIRE_EQ(log.states.size(), 4u);
    for (size_t i = 0; i < 4; ++i)
        CHECK_EQ(log.states[i], expect[i]);
    REQUIRE_EQ(log.resets.size(), 1u);
    CHECK_EQ(log.resets[0], CANOPEN_NMT_CMD_RESET_COMM);
    // Reset Node はエラー状態も消す
    canopen_node_emcy(&n, CANOPEN_EMCY_GENERIC, 0, nullptr);
    drain(n);
    CHECK_EQ(n.error_register, CANOPEN_ERR_REG_GENERIC);
    feed(n, frame(0x000, {0x81, 0x05}));
    CHECK_EQ(n.error_register, 0u);
    CHECK_EQ(n.error_count, 0u);
}

TEST_CASE(stopped_node_ignores_sdo_but_keeps_heartbeat) {
    canopen_node_t n;
    fresh(n);
    feed(n, frame(0x000, {0x02, 0x05}));
    CHECK(feed(n, frame(0x605, {0x40, 0x00, 0x10, 0x00, 0, 0, 0, 0})).empty());
    auto hb = tick(n, 1000);
    REQUIRE_EQ(hb.size(), 1u);
    CHECK_EQ(hb[0].id, 0x705u);
    bytes_are(hb[0], {0x04});
    CHECK(!canopen_node_emcy(&n, CANOPEN_EMCY_GENERIC, 0, nullptr)); // Stopped では送らない
    CHECK_EQ(canopen_node_tx_pending(&n), 0u);
}

TEST_CASE(heartbeat_producer_period_and_phase) {
    canopen_node_t n;
    fresh(n);
    CHECK(tick(n, 999).empty());
    auto hb = tick(n, 1);
    REQUIRE_EQ(hb.size(), 1u);
    bytes_are(hb[0], {0x7F});
    hb = tick(n, 2500); // 遅れても 1 回だけ。位相は保つ (残り 500)
    CHECK_EQ(hb.size(), 1u);
    CHECK(tick(n, 499).empty());
    CHECK_EQ(tick(n, 1).size(), 1u);
    // SDO で 0x1017 を 0 にすると止まる
    sdo(n, {0x2B, 0x17, 0x10, 0x00, 0x00, 0x00, 0, 0});
    CHECK(tick(n, 100000).empty());
}

/* ================= SDO ================= */

TEST_CASE(sdo_expedited_upload_layouts) {
    canopen_node_t n;
    fresh(n);
    // UINT32 0x1000 = 0x00020192: cs = 0x43 (n = 0)
    bytes_are(sdo(n, {0x40, 0x00, 0x10, 0x00, 0, 0, 0, 0}),
              {0x43, 0x00, 0x10, 0x00, 0x92, 0x01, 0x02, 0x00});
    // UINT16 0x1017 = 1000: cs = 0x4B (n = 2)
    bytes_are(sdo(n, {0x40, 0x17, 0x10, 0x00, 0, 0, 0, 0}),
              {0x4B, 0x17, 0x10, 0x00, 0xE8, 0x03, 0x00, 0x00});
    // UINT8 0x1001 = 0: cs = 0x4F (n = 3)
    bytes_are(sdo(n, {0x40, 0x01, 0x10, 0x00, 0, 0, 0, 0}),
              {0x4F, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00});
}

TEST_CASE(sdo_expedited_download_and_length_checks) {
    canopen_node_t n;
    fresh(n);
    bytes_are(sdo(n, {0x2B, 0x17, 0x10, 0x00, 0xF4, 0x01, 0x00, 0x00}),
              {0x60, 0x17, 0x10, 0x00, 0, 0, 0, 0});
    CHECK_EQ(n.heartbeat_producer_ms, 500u);
    // サイズ指定なし (e=1, s=0): オブジェクト本来の 2 バイトを使う
    sdo(n, {0x22, 0x17, 0x10, 0x00, 0x2C, 0x01, 0xFF, 0xFF});
    CHECK_EQ(n.heartbeat_producer_ms, 300u);
    // UINT16 に 4 バイト: 長すぎる
    abort_is(sdo(n, {0x23, 0x17, 0x10, 0x00, 1, 2, 3, 4}), 0x1017, 0, CANOPEN_SDO_ABORT_LENGTH_HIGH);
    // UINT32 に 2 バイト: 短すぎる (旧版は下位 2 バイトだけ書き換えていた)
    abort_is(sdo(n, {0x2B, 0x05, 0x10, 0x00, 0x81, 0x00, 0, 0}), 0x1005, 0,
             CANOPEN_SDO_ABORT_LENGTH_LOW);
    CHECK_EQ(n.cob_id_sync, 0x80u);
    CHECK_EQ(n.heartbeat_producer_ms, 300u);
}

TEST_CASE(sdo_abort_codes) {
    canopen_node_t n;
    fresh(n);
    uint32_t wo = 0;
    canopen_od_add_entry(&n.od, 0x2100, 0, OD_TYPE_UINT32, OD_ACCESS_WO, &wo, 4);
    abort_is(sdo(n, {0x40, 0x00, 0x20, 0x00, 0, 0, 0, 0}), 0x2000, 0, CANOPEN_SDO_ABORT_NO_OBJECT);
    abort_is(sdo(n, {0x40, 0x18, 0x10, 0x09, 0, 0, 0, 0}), 0x1018, 9, CANOPEN_SDO_ABORT_NO_SUBINDEX);
    abort_is(sdo(n, {0x23, 0x00, 0x10, 0x00, 1, 2, 3, 4}), 0x1000, 0, CANOPEN_SDO_ABORT_READ_ONLY);
    abort_is(sdo(n, {0x40, 0x00, 0x21, 0x00, 0, 0, 0, 0}), 0x2100, 0, CANOPEN_SDO_ABORT_WRITE_ONLY);
    abort_is(sdo(n, {0xE0, 0x00, 0x10, 0x00, 0, 0, 0, 0}), 0x1000, 0, CANOPEN_SDO_ABORT_CS);
    abort_is(sdo(n, {0xA0, 0x00, 0x10, 0x00, 0, 0, 0, 0}), 0x1000, 0, CANOPEN_SDO_ABORT_CS); // ブロック
    // 転送していないときのセグメント要求
    CHECK_EQ(sdo(n, {0x60, 0, 0, 0, 0, 0, 0, 0}).data[0], 0x80u);
    // DLC が 8 でない SDO は無視
    CHECK(feed(n, frame(0x605, {0x40, 0x00, 0x10, 0x00})).empty());
    // 拡張フレーム・リモートフレームは無視
    can_frame_t ext = frame(0x605, {0x40, 0x00, 0x10, 0x00, 0, 0, 0, 0});
    ext.is_extended = true;
    CHECK(feed(n, ext).empty());
    ext.is_extended = false;
    ext.is_rtr = true;
    CHECK(feed(n, ext).empty());
    // 他ノード宛て
    CHECK(feed(n, frame(0x606, {0x40, 0x00, 0x10, 0x00, 0, 0, 0, 0})).empty());
}

TEST_CASE(sdo_segmented_upload_of_device_name) {
    canopen_node_t n;
    fresh(n); // "ServoDrive-CiA402" = 17 バイト
    bytes_are(sdo(n, {0x40, 0x08, 0x10, 0x00, 0, 0, 0, 0}), {0x41, 0x08, 0x10, 0x00, 17, 0, 0, 0});
    bytes_are(sdo(n, {0x60, 0, 0, 0, 0, 0, 0, 0}), {0x00, 'S', 'e', 'r', 'v', 'o', 'D', 'r'});
    bytes_are(sdo(n, {0x70, 0, 0, 0, 0, 0, 0, 0}), {0x10, 'i', 'v', 'e', '-', 'C', 'i', 'A'});
    // 最後: 3 バイト、n = 4, c = 1 -> 0x09
    bytes_are(sdo(n, {0x60, 0, 0, 0, 0, 0, 0, 0}), {0x09, '4', '0', '2', 0, 0, 0, 0});
    CHECK_EQ(n.sdo.state, 0u);
    // トグル誤り
    sdo(n, {0x40, 0x08, 0x10, 0x00, 0, 0, 0, 0});
    abort_is(sdo(n, {0x70, 0, 0, 0, 0, 0, 0, 0}), 0x1008, 0, CANOPEN_SDO_ABORT_TOGGLE);
    // 空文字列は分割型 (サイズ 0) で 1 セグメント
    canopen_node_t e;
    fresh(e, 5, "");
    bytes_are(sdo(e, {0x40, 0x08, 0x10, 0x00, 0, 0, 0, 0}), {0x41, 0x08, 0x10, 0x00, 0, 0, 0, 0});
    bytes_are(sdo(e, {0x60, 0, 0, 0, 0, 0, 0, 0}), {0x0F, 0, 0, 0, 0, 0, 0, 0});
}

TEST_CASE(sdo_segmented_download_to_string) {
    canopen_node_t n;
    fresh(n);
    char text[20];
    std::memset(text, 'x', sizeof(text));
    canopen_od_add_entry(&n.od, 0x2000, 0, OD_TYPE_VISIBLE_STRING, OD_ACCESS_RW, text, sizeof(text));
    bytes_are(sdo(n, {0x21, 0x00, 0x20, 0x00, 11, 0, 0, 0}), {0x60, 0x00, 0x20, 0x00, 0, 0, 0, 0});
    bytes_are(sdo(n, {0x00, 'H', 'e', 'l', 'l', 'o', ' ', 'W'}), {0x20, 0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(text[0], 'x'); // 最後のセグメントまで OD は変わらない
    // t = 1, 4 バイト (n = 3), c = 1 -> 0x17
    bytes_are(sdo(n, {0x17, 'o', 'r', 'l', 'd', 0, 0, 0}), {0x30, 0, 0, 0, 0, 0, 0, 0});
    CHECK_STR_EQ(text, "Hello World");
    CHECK_EQ(text[19], '\0'); // 残りは 0 埋め
    // 宣言サイズより多く送る
    sdo(n, {0x21, 0x00, 0x20, 0x00, 3, 0, 0, 0});
    abort_is(sdo(n, {0x01, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}), 0x2000, 0,
             CANOPEN_SDO_ABORT_LENGTH_HIGH);
    // 宣言サイズより少なく終える
    sdo(n, {0x21, 0x00, 0x20, 0x00, 5, 0, 0, 0});
    abort_is(sdo(n, {0x09, 'a', 'b', 'c', 0, 0, 0, 0}), 0x2000, 0, CANOPEN_SDO_ABORT_TYPE_MISMATCH);
    CHECK_STR_EQ(text, "Hello World");
    // 格納領域より大きいサイズは開始時に拒否
    abort_is(sdo(n, {0x21, 0x00, 0x20, 0x00, 21, 0, 0, 0}), 0x2000, 0,
             CANOPEN_SDO_ABORT_LENGTH_HIGH);
    // RO への分割ダウンロードも開始時に拒否
    abort_is(sdo(n, {0x21, 0x08, 0x10, 0x00, 3, 0, 0, 0}), 0x1008, 0, CANOPEN_SDO_ABORT_READ_ONLY);
    // サイズ指定なし (s = 0)
    sdo(n, {0x20, 0x00, 0x20, 0x00, 0, 0, 0, 0});
    sdo(n, {0x0B, 'O', 'K', 0, 0, 0, 0, 0}); // 2 バイト, n = 5, c = 1
    CHECK_STR_EQ(text, "OK");
    // 途中のトグル誤り
    sdo(n, {0x21, 0x00, 0x20, 0x00, 14, 0, 0, 0});
    abort_is(sdo(n, {0x10, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}), 0x2000, 0, CANOPEN_SDO_ABORT_TOGGLE);
}

TEST_CASE(sdo_timeout_and_client_abort) {
    canopen_node_t n;
    fresh(n);
    n.heartbeat_producer_ms = 0;
    sdo(n, {0x40, 0x08, 0x10, 0x00, 0, 0, 0, 0}); // 分割アップロード開始
    CHECK(tick(n, 999).empty());
    auto out = tick(n, 1);
    REQUIRE_EQ(out.size(), 1u);
    abort_is(out[0], 0x1008, 0, CANOPEN_SDO_ABORT_TIMEOUT);
    // クライアントのアボートには応答しない
    sdo(n, {0x40, 0x08, 0x10, 0x00, 0, 0, 0, 0});
    CHECK(feed(n, frame(0x605, {0x80, 0x08, 0x10, 0x00, 0, 0, 0, 0x08})).empty());
    CHECK_EQ(n.sdo.state, 0u);
    CHECK(tick(n, 5000).empty());
}

namespace {
uint32_t limit_100(const canopen_od_entry_t *, const void *data, size_t, void *calls) {
    ++*static_cast<int *>(calls);
    uint16_t v;
    std::memcpy(&v, data, 2);
    return v > 100 ? CANOPEN_SDO_ABORT_VALUE_HIGH : 0;
}
} // namespace

TEST_CASE(write_hook_validates_sdo_and_local_writes) {
    canopen_node_t n;
    fresh(n);
    uint16_t speed = 10;
    int calls = 0;
    canopen_od_add_entry(&n.od, 0x2200, 0, OD_TYPE_UINT16, OD_ACCESS_RW, &speed, 2);
    REQUIRE(canopen_od_set_write_hook(&n.od, 0x2200, 0, limit_100, &calls));
    abort_is(sdo(n, {0x2B, 0x00, 0x22, 0x00, 101, 0, 0, 0}), 0x2200, 0, CANOPEN_SDO_ABORT_VALUE_HIGH);
    CHECK_EQ(speed, 10u);
    sdo(n, {0x2B, 0x00, 0x22, 0x00, 100, 0, 0, 0});
    CHECK_EQ(speed, 100u);
    const uint16_t big = 500;
    CHECK(!canopen_od_write(&n.od, 0x2200, 0, &big, 2));
    CHECK_EQ(calls, 3);
    CHECK(!canopen_od_set_write_hook(&n.od, 0x2201, 0, limit_100, &calls));
}

/* ================= PDO ================= */

TEST_CASE(tpdo_sync_cyclic_mapping) {
    canopen_node_t n;
    fresh(n);
    int32_t pos = -2;                // FE FF FF FF
    uint16_t status = 0x1237;        // 37 12
    canopen_od_add_entry(&n.od, 0x6064, 0, OD_TYPE_INT32, OD_ACCESS_RO, &pos, 4);
    canopen_od_add_entry(&n.od, 0x6041, 0, OD_TYPE_UINT16, OD_ACCESS_RO, &status, 2);
    const canopen_pdo_map_entry_t map[] = {{0x6064, 0, 32}, {0x6041, 0, 16}};
    REQUIRE(canopen_node_config_tpdo(&n, 1, map, 2, 1, 0));
    CHECK(feed(n, frame(0x080, {})).empty()); // Pre-Operational では PDO を送らない
    start(n);
    auto out = feed(n, frame(0x080, {}));
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].id, 0x185u);
    bytes_are(out[0], {0xFE, 0xFF, 0xFF, 0xFF, 0x37, 0x12});
    // 3 回目の SYNC ごと
    REQUIRE(canopen_node_config_tpdo(&n, 2, map, 1, 3, 0));
    int sent2 = 0;
    for (int i = 0; i < 9; ++i)
        for (auto &f : feed(n, frame(0x080, {i == 0 ? (uint8_t)1 : (uint8_t)2})))
            sent2 += f.id == 0x285u ? 1 : 0;
    CHECK_EQ(sent2, 3);
    // SYNC の COB-ID は 0x1005 で変えられる
    sdo(n, {0x23, 0x05, 0x10, 0x00, 0x81, 0x00, 0x00, 0x00});
    CHECK(feed(n, frame(0x080, {})).empty());
    CHECK_EQ(feed(n, frame(0x081, {})).size(), 1u);
    // 旧 API の生データ送信は TPDO1 の COB-ID を使う
    can_frame_t raw{};
    REQUIRE(canopen_node_send_tpdo1(&n, (const uint8_t *)"\x01\x02", 2, &raw));
    CHECK_EQ(raw.id, 0x185u);
}

TEST_CASE(tpdo_event_timer_change_of_state_and_inhibit) {
    canopen_node_t n;
    fresh(n);
    n.heartbeat_producer_ms = 0;
    uint8_t in = 1;
    canopen_od_add_entry(&n.od, 0x6000, 1, OD_TYPE_UINT8, OD_ACCESS_RO, &in, 1);
    const canopen_pdo_map_entry_t map[] = {{0x6000, 1, 8}};
    REQUIRE(canopen_node_config_tpdo(&n, 1, map, 1, 254, 100));
    start(n);
    auto out = tick(n, 1); // Operational に入ったら一度送る
    REQUIRE_EQ(out.size(), 1u);
    bytes_are(out[0], {0x01});
    CHECK(tick(n, 99).empty());      // タイマは送信した時点から数える
    CHECK_EQ(tick(n, 1).size(), 1u); // イベントタイマ 100 ms
    in = 2;
    out = tick(n, 1); // 値の変化
    REQUIRE_EQ(out.size(), 1u);
    bytes_are(out[0], {0x02});
    // 送信禁止時間 10 ms (= 100 x 100 µs) は無効化してから設定する
    CHECK(!canopen_node_set_tpdo_inhibit(&n, 1, 100));
    sdo(n, {0x23, 0x00, 0x18, 0x01, 0x85, 0x01, 0x00, 0x80}); // 無効化
    REQUIRE(canopen_node_set_tpdo_inhibit(&n, 1, 100));
    sdo(n, {0x23, 0x00, 0x18, 0x01, 0x85, 0x01, 0x00, 0x00}); // 有効化
    in = 3;
    CHECK_EQ(tick(n, 1).size(), 1u);
    in = 4;
    CHECK(tick(n, 5).empty()); // 禁止時間中は保留
    CHECK(tick(n, 4).empty());
    out = tick(n, 1); // 10 ms 経過
    REQUIRE_EQ(out.size(), 1u);
    bytes_are(out[0], {0x04});
}

TEST_CASE(tpdo_trigger_acyclic_sync) {
    canopen_node_t n;
    fresh(n);
    uint16_t v = 0xBEEF;
    canopen_od_add_entry(&n.od, 0x2000, 0, OD_TYPE_UINT16, OD_ACCESS_RW, &v, 2);
    const canopen_pdo_map_entry_t map[] = {{0x2000, 0, 16}};
    REQUIRE(canopen_node_config_tpdo(&n, 1, map, 1, 0, 0));
    start(n);
    CHECK(feed(n, frame(0x080, {})).empty()); // トリガーなし
    REQUIRE(canopen_node_trigger_tpdo(&n, 1));
    CHECK_EQ(canopen_node_tx_pending(&n), 0u); // 次の SYNC まで待つ
    auto out = feed(n, frame(0x080, {}));
    REQUIRE_EQ(out.size(), 1u);
    bytes_are(out[0], {0xEF, 0xBE});
    CHECK(feed(n, frame(0x080, {})).empty());
    CHECK(!canopen_node_trigger_tpdo(&n, 2)); // 無効な PDO
    CHECK(!canopen_node_trigger_tpdo(&n, 5));
}

namespace {
struct RpdoLog {
    std::vector<uint8_t> nums;
};
} // namespace

TEST_CASE(rpdo_applies_mapped_values) {
    canopen_node_t n;
    fresh(n);
    uint16_t control = 0;
    uint8_t mode = 0;
    canopen_od_add_entry(&n.od, 0x6040, 0, OD_TYPE_UINT16, OD_ACCESS_RW, &control, 2);
    canopen_od_add_entry(&n.od, 0x6060, 0, OD_TYPE_UINT8, OD_ACCESS_RW, &mode, 1);
    RpdoLog log;
    canopen_node_callbacks_t cb{};
    cb.on_rpdo = [](canopen_node_t *, uint8_t num, void *u) { static_cast<RpdoLog *>(u)->nums.push_back(num); };
    canopen_node_set_callbacks(&n, &cb, &log);
    const canopen_pdo_map_entry_t map[] = {{0x6040, 0, 16}, {0x6060, 0, 8}};
    REQUIRE(canopen_node_config_rpdo(&n, 1, map, 2, 254));
    feed(n, frame(0x205, {0x0F, 0x00, 0x01})); // Pre-Operational では反映しない
    CHECK_EQ(control, 0u);
    start(n);
    CHECK(feed(n, frame(0x205, {0x0F, 0x00, 0x01, 0xAA})).empty()); // 長い分は無視して反映
    CHECK_EQ(control, 0x000Fu);
    CHECK_EQ(mode, 1u);
    REQUIRE_EQ(log.nums.size(), 1u);
    CHECK_EQ(log.nums[0], 1u);
    // 短すぎる RPDO は反映せず EMCY 0x8210
    auto out = feed(n, frame(0x205, {0x06, 0x00}));
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].id, 0x085u);
    bytes_are(out[0], {0x10, 0x82, 0x11, 0, 0, 0, 0, 0});
    CHECK_EQ(control, 0x000Fu);
    // 同期型 RPDO は次の SYNC で反映
    REQUIRE(canopen_node_config_rpdo(&n, 2, map, 1, 1));
    feed(n, frame(0x305, {0x07, 0x00}));
    CHECK_EQ(control, 0x000Fu);
    feed(n, frame(0x080, {}));
    CHECK_EQ(control, 0x0007u);
}

TEST_CASE(pdo_configuration_over_sdo_like_a_master) {
    canopen_node_t n;
    fresh(n);
    int32_t pos = 0x01020304;
    uint8_t ro8 = 0;
    char str[4] = "abc";
    canopen_od_add_entry(&n.od, 0x6064, 0, OD_TYPE_INT32, OD_ACCESS_RO, &pos, 4);
    canopen_od_add_entry(&n.od, 0x2001, 0, OD_TYPE_UINT8, OD_ACCESS_RO, &ro8, 1);
    canopen_od_add_entry(&n.od, 0x2002, 0, OD_TYPE_VISIBLE_STRING, OD_ACCESS_RO, str, 4);
    // 既定: COB-ID 0x80000185 (無効)、送信種別 254、最大サブインデックス 5
    bytes_are(sdo(n, {0x40, 0x00, 0x18, 0x01, 0, 0, 0, 0}), {0x43, 0x00, 0x18, 0x01, 0x85, 0x01, 0x00, 0x80});
    bytes_are(sdo(n, {0x40, 0x00, 0x18, 0x00, 0, 0, 0, 0}), {0x4F, 0x00, 0x18, 0x00, 5, 0, 0, 0});
    bytes_are(sdo(n, {0x40, 0x00, 0x14, 0x00, 0, 0, 0, 0}), {0x4F, 0x00, 0x14, 0x00, 2, 0, 0, 0});
    abort_is(sdo(n, {0x40, 0x00, 0x18, 0x04, 0, 0, 0, 0}), 0x1800, 4, CANOPEN_SDO_ABORT_NO_SUBINDEX);
    // マッピング: 不在 / 長さ違い / 文字列 / RPDO に RO
    abort_is(sdo(n, {0x23, 0x00, 0x1A, 0x01, 0x20, 0x00, 0x99, 0x20}), 0x1A00, 1, CANOPEN_SDO_ABORT_NO_OBJECT);
    abort_is(sdo(n, {0x23, 0x00, 0x1A, 0x01, 0x10, 0x00, 0x64, 0x60}), 0x1A00, 1, CANOPEN_SDO_ABORT_NOT_MAPPABLE);
    abort_is(sdo(n, {0x23, 0x00, 0x1A, 0x01, 0x20, 0x00, 0x02, 0x20}), 0x1A00, 1, CANOPEN_SDO_ABORT_NOT_MAPPABLE);
    abort_is(sdo(n, {0x23, 0x00, 0x16, 0x01, 0x08, 0x00, 0x01, 0x20}), 0x1600, 1, CANOPEN_SDO_ABORT_NOT_MAPPABLE);
    // 正しい手順: 0x1A00.1 = 6064 00 20, 0x1A00.0 = 1, 0x1800.2 = 1, 0x1800.1 = 0x185 (有効)
    sdo(n, {0x23, 0x00, 0x1A, 0x01, 0x20, 0x00, 0x64, 0x60});
    sdo(n, {0x2F, 0x00, 0x1A, 0x00, 1, 0, 0, 0});
    abort_is(sdo(n, {0x23, 0x00, 0x1A, 0x02, 0x20, 0x00, 0x64, 0x60}), 0x1A00, 2, CANOPEN_SDO_ABORT_UNSUPPORTED);
    // 64bit を超えるマッピング
    sdo(n, {0x2F, 0x00, 0x1A, 0x00, 0, 0, 0, 0});
    for (uint8_t s = 2; s <= 3; ++s)
        sdo(n, {0x23, 0x00, 0x1A, s, 0x20, 0x00, 0x64, 0x60});
    abort_is(sdo(n, {0x2F, 0x00, 0x1A, 0x00, 3, 0, 0, 0}), 0x1A00, 0, CANOPEN_SDO_ABORT_PDO_LENGTH);
    abort_is(sdo(n, {0x2F, 0x00, 0x1A, 0x00, 9, 0, 0, 0}), 0x1A00, 0, CANOPEN_SDO_ABORT_VALUE_HIGH);
    sdo(n, {0x2F, 0x00, 0x1A, 0x00, 1, 0, 0, 0});
    abort_is(sdo(n, {0x2F, 0x00, 0x18, 0x02, 241, 0, 0, 0}), 0x1800, 2, CANOPEN_SDO_ABORT_VALUE_RANGE);
    sdo(n, {0x2F, 0x00, 0x18, 0x02, 1, 0, 0, 0});
    abort_is(sdo(n, {0x23, 0x00, 0x18, 0x01, 0x85, 0x01, 0x00, 0x20}), 0x1800, 1, CANOPEN_SDO_ABORT_VALUE_RANGE); // 29bit
    bytes_are(sdo(n, {0x23, 0x00, 0x18, 0x01, 0x85, 0x01, 0x00, 0x00}), {0x60, 0x00, 0x18, 0x01, 0, 0, 0, 0});
    // 有効な間はマッピングも CAN ID も変えられない
    abort_is(sdo(n, {0x2F, 0x00, 0x1A, 0x00, 0, 0, 0, 0}), 0x1A00, 0, CANOPEN_SDO_ABORT_DEVICE_STATE);
    abort_is(sdo(n, {0x23, 0x00, 0x18, 0x01, 0x86, 0x01, 0x00, 0x00}), 0x1800, 1, CANOPEN_SDO_ABORT_VALUE_RANGE);
    abort_is(sdo(n, {0x2F, 0x00, 0x18, 0x00, 5, 0, 0, 0}), 0x1800, 0, CANOPEN_SDO_ABORT_READ_ONLY);
    start(n);
    auto out = feed(n, frame(0x080, {}));
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].id, 0x185u);
    bytes_are(out[0], {0x04, 0x03, 0x02, 0x01});
    // 読み戻し
    bytes_are(sdo(n, {0x40, 0x00, 0x1A, 0x01, 0, 0, 0, 0}), {0x43, 0x00, 0x1A, 0x01, 0x20, 0x00, 0x64, 0x60});
    // API でも不正なマッピングは拒否
    const canopen_pdo_map_entry_t bad[] = {{0x6064, 0, 16}};
    CHECK(!canopen_node_config_tpdo(&n, 2, bad, 1, 1, 0));
    const canopen_pdo_map_entry_t ok[] = {{0x6064, 0, 32}};
    CHECK(!canopen_node_config_tpdo(&n, 2, ok, 1, 250, 0)); // 送信種別
    CHECK(!canopen_node_config_tpdo(&n, 0, ok, 1, 1, 0));
    CHECK(!canopen_node_config_tpdo(&n, 5, ok, 1, 1, 0));
}

/* ================= EMCY・Heartbeat 監視 ================= */

TEST_CASE(emcy_frame_error_register_and_history) {
    canopen_node_t n;
    fresh(n);
    const uint8_t manuf[5] = {1, 2, 3, 4, 5};
    REQUIRE(canopen_node_emcy(&n, 0x4210, CANOPEN_ERR_REG_TEMPERATURE, manuf));
    auto out = drain(n);
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].id, 0x085u);
    bytes_are(out[0], {0x10, 0x42, 0x09, 1, 2, 3, 4, 5}); // レジスタ = 温度 | 汎用
    canopen_node_emcy(&n, 0x3210, CANOPEN_ERR_REG_VOLTAGE, nullptr);
    drain(n);
    bytes_are(sdo(n, {0x40, 0x03, 0x10, 0x00, 0, 0, 0, 0}), {0x4F, 0x03, 0x10, 0x00, 2, 0, 0, 0});
    bytes_are(sdo(n, {0x40, 0x03, 0x10, 0x01, 0, 0, 0, 0}), {0x43, 0x03, 0x10, 0x01, 0x10, 0x32, 0, 0}); // 新しい順
    bytes_are(sdo(n, {0x40, 0x03, 0x10, 0x02, 0, 0, 0, 0}), {0x43, 0x03, 0x10, 0x02, 0x10, 0x42, 0, 0});
    abort_is(sdo(n, {0x40, 0x03, 0x10, 0x03, 0, 0, 0, 0}), 0x1003, 3, CANOPEN_SDO_ABORT_NO_DATA);
    abort_is(sdo(n, {0x40, 0x03, 0x10, 0x05, 0, 0, 0, 0}), 0x1003, 5, CANOPEN_SDO_ABORT_NO_SUBINDEX);
    bytes_are(sdo(n, {0x40, 0x01, 0x10, 0x00, 0, 0, 0, 0}), {0x4F, 0x01, 0x10, 0x00, 0x0D, 0, 0, 0});
    abort_is(sdo(n, {0x2F, 0x03, 0x10, 0x00, 1, 0, 0, 0}), 0x1003, 0, CANOPEN_SDO_ABORT_VALUE_RANGE);
    sdo(n, {0x2F, 0x03, 0x10, 0x00, 0, 0, 0, 0}); // 0 を書くと消去
    CHECK_EQ(n.error_count, 0u);
    // エラーリセット
    REQUIRE(canopen_node_emcy(&n, CANOPEN_EMCY_NO_ERROR, 0, nullptr));
    out = drain(n);
    REQUIRE_EQ(out.size(), 1u);
    bytes_are(out[0], {0, 0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(n.error_register, 0u);
    // 履歴は最大 CANOPEN_ERROR_HISTORY 件
    for (int i = 0; i < 6; ++i)
        canopen_node_emcy(&n, (uint16_t)(0x5000 + i), 0, nullptr);
    CHECK_EQ(n.error_count, (uint8_t)CANOPEN_ERROR_HISTORY);
    CHECK_EQ(n.error_history[0], 0x5005u);
}

TEST_CASE(heartbeat_consumer_detects_loss) {
    canopen_node_t n;
    fresh(n);
    n.heartbeat_producer_ms = 0;
    std::vector<uint8_t> lost;
    canopen_node_callbacks_t cb{};
    cb.on_heartbeat_timeout = [](canopen_node_t *, uint8_t id, void *u) {
        static_cast<std::vector<uint8_t> *>(u)->push_back(id);
    };
    canopen_node_set_callbacks(&n, &cb, &lost);
    REQUIRE(canopen_node_add_heartbeat_consumer(&n, 9, 100));
    bytes_are(sdo(n, {0x40, 0x16, 0x10, 0x01, 0, 0, 0, 0}), {0x43, 0x16, 0x10, 0x01, 0x64, 0x00, 0x09, 0x00});
    CHECK(tick(n, 1000).empty()); // 最初の Heartbeat までは監視しない
    canopen_nmt_state_t st{};
    CHECK(!canopen_node_get_remote_state(&n, 9, &st));
    feed(n, frame(0x709, {0x05}));
    REQUIRE(canopen_node_get_remote_state(&n, 9, &st));
    CHECK_EQ(st, CANOPEN_NMT_STATE_OPERATIONAL);
    CHECK(tick(n, 99).empty());
    auto out = tick(n, 1);
    REQUIRE_EQ(out.size(), 1u);
    bytes_are(out[0], {0x30, 0x81, 0x11, 0, 0, 0, 0, 0}); // EMCY 0x8130
    REQUIRE_EQ(lost.size(), 1u);
    CHECK_EQ(lost[0], 9u);
    CHECK(tick(n, 1000).empty()); // 通知は 1 回
    feed(n, frame(0x709, {0x7F})); // 復帰したら再び監視
    CHECK(tick(n, 100).size() == 1u);
    // 同じノードを 2 回登録できない
    abort_is(sdo(n, {0x23, 0x16, 0x10, 0x02, 0x64, 0x00, 0x09, 0x00}), 0x1016, 2,
             CANOPEN_SDO_ABORT_PARAM_INCOMPAT);
    CHECK(!canopen_node_add_heartbeat_consumer(&n, 0, 100));
    CHECK(!canopen_node_add_heartbeat_consumer(&n, 9, 0));
    CHECK(canopen_node_add_heartbeat_consumer(&n, 9, 200)); // 既存の更新
    for (uint8_t id = 10; id < 13; ++id)
        CHECK(canopen_node_add_heartbeat_consumer(&n, id, 100));
    CHECK(!canopen_node_add_heartbeat_consumer(&n, 20, 100)); // 満杯
}

TEST_CASE(tx_queue_overflow_is_counted) {
    canopen_node_t n;
    fresh(n);
    can_frame_t req = frame(0x605, {0x40, 0x00, 0x10, 0x00, 0, 0, 0, 0});
    can_frame_t out{};
    // process_frame は生成したフレームを 1 つ取り出して返すので溜まらない
    for (int i = 0; i < CANOPEN_TX_QUEUE_SIZE + 3; ++i)
        CHECK(canopen_node_process_frame(&n, &req, &out));
    CHECK_EQ(n.tx_overflows, 0u);
    CHECK_EQ(canopen_node_tx_pending(&n), 0u);
    // 取り出さずに積み続けると、容量を超えた分は捨てて数える
    for (int i = 0; i < CANOPEN_TX_QUEUE_SIZE + 3; ++i)
        canopen_node_emcy(&n, CANOPEN_EMCY_GENERIC, 0, nullptr);
    CHECK_EQ(canopen_node_tx_pending(&n), (size_t)CANOPEN_TX_QUEUE_SIZE);
    CHECK_EQ(n.tx_overflows, 3u);
    CHECK(!canopen_node_pop_tx(nullptr, &out));
    CHECK(!canopen_node_process_frame(&n, nullptr, &out));
    CHECK_EQ(canopen_node_tx_pending(nullptr), 0u);
}

TEST_CASE(od_checked_access_semantics) {
    canopen_od_t od;
    canopen_od_init(&od);
    uint32_t u32 = 0;
    char name[8] = "abc";
    REQUIRE(canopen_od_add_entry(&od, 0x2000, 1, OD_TYPE_UINT32, OD_ACCESS_RW, &u32, 4));
    REQUIRE(canopen_od_add_entry(&od, 0x2001, 0, OD_TYPE_VISIBLE_STRING, OD_ACCESS_RW, name, 8));
    const uint16_t two = 7;
    CHECK_EQ(canopen_od_write_checked(&od, 0x2000, 1, &two, 2), CANOPEN_SDO_ABORT_LENGTH_LOW);
    const uint64_t eight = 7;
    CHECK_EQ(canopen_od_write_checked(&od, 0x2000, 1, &eight, 8), CANOPEN_SDO_ABORT_LENGTH_HIGH);
    CHECK_EQ(canopen_od_write_checked(&od, 0x2000, 2, &eight, 4), CANOPEN_SDO_ABORT_NO_SUBINDEX);
    CHECK_EQ(canopen_od_write_checked(&od, 0x3000, 0, &eight, 4), CANOPEN_SDO_ABORT_NO_OBJECT);
    CHECK_EQ(u32, 0u);
    char out[8];
    size_t len = 0;
    CHECK_EQ(canopen_od_read_checked(&od, 0x2001, 0, out, sizeof(out), &len), 0u);
    CHECK_EQ(len, 3u); // 最初の NUL まで
    CHECK_EQ(canopen_od_read_checked(&od, 0x2001, 0, out, 2, &len), CANOPEN_SDO_ABORT_OUT_OF_MEMORY);
    CHECK_EQ(canopen_od_write_checked(&od, 0x2001, 0, "xy", 2), 0u);
    CHECK_STR_EQ(name, "xy");
    CHECK_EQ(name[7], '\0');
    CHECK_EQ(canopen_od_write_checked(&od, 0x2001, 0, "123456789", 9), CANOPEN_SDO_ABORT_LENGTH_HIGH);
    // 同じ index/subindex の再登録は置き換え
    uint32_t other = 5;
    REQUIRE(canopen_od_add_entry(&od, 0x2000, 1, OD_TYPE_UINT32, OD_ACCESS_RO, &other, 4));
    CHECK_EQ(od.count, 2u);
    CHECK_EQ(canopen_od_write_checked(&od, 0x2000, 1, &u32, 4), CANOPEN_SDO_ABORT_READ_ONLY);
    CHECK(!canopen_od_add_entry(&od, 0x2002, 0, OD_TYPE_UINT8, OD_ACCESS_RW, nullptr, 1));
    CHECK(!canopen_od_add_entry(&od, 0x2002, 0, OD_TYPE_UINT8, OD_ACCESS_RW, &u32, 0));
}

TEST_CASE(cpp_wrapper_pdo_and_handle) {
    embed_canopen::Node node(7, "Cpp");
    uint32_t counter = 0x11223344;
    REQUIRE(node.add_variable(0x2000, 0, counter, OD_ACCESS_RO));
    REQUIRE(node.config_tpdo(1, {{0x2000, 0, 32}}, 1));
    std::vector<can_frame_t> sent;
    auto send = [&sent](const can_frame_t &f) { sent.push_back(f); };
    node.handle(frame(0x000, {0x01, 7}), send);
    CHECK_EQ(sent.size(), 1u); // ブートアップ (未取り出しだった分)
    sent.clear();
    node.handle(frame(0x080, {}), send);
    REQUIRE_EQ(sent.size(), 1u);
    CHECK_EQ(sent[0].id, 0x187u);
    bytes_are(sent[0], {0x44, 0x33, 0x22, 0x11});
    double gain = 1.5;
    CHECK(node.add_variable(0x2100, 0, gain));
    char label[10] = "drive";
    CHECK(node.add_string(0x2101, 0, label));
    CHECK(node.emcy(CANOPEN_EMCY_SOFTWARE));
    node.tick(1, send);
    CHECK(sent.size() >= 2u);
}

TEST_CASE(pdo_virtual_object_sizes_and_subindexes) {
    canopen_node_t n;
    fresh(n);
    // 通信パラメータ: sub0=最大サブインデックス(TPDO 5 / RPDO 2)、sub2=1 バイト、sub3/sub5=2 バイト
    bytes_are(sdo(n, {0x40, 0x00, 0x18, 0x02, 0, 0, 0, 0}), {0x4F, 0x00, 0x18, 0x02, 254, 0, 0, 0});
    bytes_are(sdo(n, {0x40, 0x00, 0x18, 0x03, 0, 0, 0, 0}), {0x4B, 0x00, 0x18, 0x03, 0, 0, 0, 0});
    bytes_are(sdo(n, {0x40, 0x00, 0x18, 0x05, 0, 0, 0, 0}), {0x4B, 0x00, 0x18, 0x05, 0, 0, 0, 0});
    abort_is(sdo(n, {0x40, 0x00, 0x18, 0x04, 0, 0, 0, 0}), 0x1800, 4, CANOPEN_SDO_ABORT_NO_SUBINDEX);
    abort_is(sdo(n, {0x40, 0x00, 0x14, 0x03, 0, 0, 0, 0}), 0x1400, 3, CANOPEN_SDO_ABORT_NO_SUBINDEX); // RPDO に禁止時間はない
    abort_is(sdo(n, {0x40, 0x00, 0x14, 0x05, 0, 0, 0, 0}), 0x1400, 5, CANOPEN_SDO_ABORT_NO_SUBINDEX);
    // 4 本目までのインデックスが有効、5 本目は存在しない
    bytes_are(sdo(n, {0x40, 0x03, 0x18, 0x02, 0, 0, 0, 0}), {0x4F, 0x03, 0x18, 0x02, 254, 0, 0, 0});
    abort_is(sdo(n, {0x40, 0x04, 0x18, 0x02, 0, 0, 0, 0}), 0x1804, 2, CANOPEN_SDO_ABORT_NO_OBJECT);
    // マッピングのサブインデックスは 8 まで
    bytes_are(sdo(n, {0x40, 0x00, 0x1A, 0x08, 0, 0, 0, 0}), {0x43, 0x00, 0x1A, 0x08, 0, 0, 0, 0});
    abort_is(sdo(n, {0x40, 0x00, 0x1A, 0x09, 0, 0, 0, 0}), 0x1A00, 9, CANOPEN_SDO_ABORT_NO_SUBINDEX);
    abort_is(sdo(n, {0x23, 0x00, 0x1A, 0x09, 0, 0, 0, 0}), 0x1A00, 9, CANOPEN_SDO_ABORT_NO_SUBINDEX);
    // 通信パラメータの書き込み長
    abort_is(sdo(n, {0x2B, 0x00, 0x18, 0x02, 1, 0, 0, 0}), 0x1800, 2, CANOPEN_SDO_ABORT_LENGTH_HIGH);
    abort_is(sdo(n, {0x2F, 0x00, 0x18, 0x03, 1, 0, 0, 0}), 0x1800, 3, CANOPEN_SDO_ABORT_LENGTH_LOW);
    abort_is(sdo(n, {0x2F, 0x00, 0x18, 0x05, 1, 0, 0, 0}), 0x1800, 5, CANOPEN_SDO_ABORT_LENGTH_LOW);
    // イベントタイマは有効なままでも変更できる
    sdo(n, {0x2B, 0x00, 0x18, 0x05, 0x64, 0x00, 0, 0});
    CHECK_EQ(n.tpdo[0].event_timer_ms, 100u);
    // 起動時にイベント種別・マッピングありの TPDO を設定して Operational へ入るまで何も送らない
    uint8_t v = 1;
    canopen_od_add_entry(&n.od, 0x2000, 0, OD_TYPE_UINT8, OD_ACCESS_RW, &v, 1);
    const canopen_pdo_map_entry_t map[] = {{0x2000, 0, 8}};
    REQUIRE(canopen_node_config_tpdo(&n, 4, map, 1, 255, 0));
    CHECK_EQ(canopen_node_tx_pending(&n), 0u);
    // 64bit ちょうど (8 バイト) は可、9 バイト目は不可
    uint64_t q[2] = {0x0102030405060708ull, 0};
    canopen_od_add_entry(&n.od, 0x2100, 0, OD_TYPE_UINT64, OD_ACCESS_RW, &q[0], 8);
    canopen_od_add_entry(&n.od, 0x2101, 0, OD_TYPE_UINT8, OD_ACCESS_RW, &v, 1);
    const canopen_pdo_map_entry_t full[] = {{0x2100, 0, 64}};
    const canopen_pdo_map_entry_t over[] = {{0x2100, 0, 64}, {0x2101, 0, 8}};
    CHECK(canopen_node_config_tpdo(&n, 2, full, 1, 1, 0));
    CHECK(!canopen_node_config_tpdo(&n, 3, over, 2, 1, 0));
    start(n);
    auto out = feed(n, frame(0x080, {}));
    bool saw = false;
    for (auto &f : out)
        if (f.id == 0x285) {
            bytes_are(f, {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01});
            saw = true;
        }
    CHECK(saw);
}

TEST_CASE(sdo_size_less_expedited_and_od_capacity) {
    canopen_node_t n;
    fresh(n);
    uint8_t one = 0;
    uint16_t two = 0;
    canopen_od_add_entry(&n.od, 0x2000, 0, OD_TYPE_UINT8, OD_ACCESS_RW, &one, 1);
    canopen_od_add_entry(&n.od, 0x2001, 0, OD_TYPE_UINT16, OD_ACCESS_RW, &two, 2);
    // サイズ指定なし: 1 バイトと 2 バイトのオブジェクトは本来の長さで書く
    sdo(n, {0x22, 0x00, 0x20, 0x00, 0x7B, 0xFF, 0xFF, 0xFF});
    CHECK_EQ(one, 0x7Bu);
    sdo(n, {0x22, 0x01, 0x20, 0x00, 0x34, 0x12, 0xFF, 0xFF});
    CHECK_EQ(two, 0x1234u);
    // 加速型 n ビットの全値: n=3 は 1 バイト、n=2 は 2 バイト
    sdo(n, {0x2F, 0x00, 0x20, 0x00, 0x05, 0, 0, 0});
    CHECK_EQ(one, 5u);
    sdo(n, {0x2B, 0x01, 0x20, 0x00, 0x06, 0x00, 0, 0});
    CHECK_EQ(two, 6u);
    // 既存の 9 件 + 追加で OD の上限 (CANOPEN_MAX_OD_ENTRIES) ちょうどまで入る
    canopen_od_t od;
    canopen_od_init(&od);
    uint8_t cell[CANOPEN_MAX_OD_ENTRIES + 1];
    for (int i = 0; i < CANOPEN_MAX_OD_ENTRIES; ++i)
        CHECK(canopen_od_add_entry(&od, 0x3000, (uint8_t)i, OD_TYPE_UINT8, OD_ACCESS_RW, &cell[i], 1));
    CHECK(!canopen_od_add_entry(&od, 0x3001, 0, OD_TYPE_UINT8, OD_ACCESS_RW, &cell[0], 1));
    CHECK(canopen_od_add_entry(&od, 0x3000, 0, OD_TYPE_UINT16, OD_ACCESS_RW, &cell[0], 1)); // 置き換えは可
    CHECK(canopen_od_find_entry(&od, 0x3000, CANOPEN_MAX_OD_ENTRIES - 1) != nullptr);
    CHECK(canopen_od_find_entry(&od, 0x3000, CANOPEN_MAX_OD_ENTRIES) == nullptr);
    // 読み出しバッファ境界
    uint32_t dev = 0;
    size_t len = 0;
    uint8_t buf[4];
    CHECK_EQ(canopen_node_sdo_read(&n, 0x1000, 0, buf, 4, &len), 0u);
    CHECK_EQ(canopen_node_sdo_read(&n, 0x1000, 0, buf, 3, &len), CANOPEN_SDO_ABORT_OUT_OF_MEMORY);
    CHECK_EQ(canopen_node_sdo_read(&n, 0x1000, 0, nullptr, 4, &len), CANOPEN_SDO_ABORT_GENERAL);
    CHECK_EQ(canopen_node_sdo_write(&n, 0x2000, 0, nullptr, 1), CANOPEN_SDO_ABORT_GENERAL);
    CHECK_EQ(canopen_node_sdo_write(nullptr, 0x2000, 0, buf, 1), CANOPEN_SDO_ABORT_GENERAL);
    (void)dev;
    // デバイス名は NUL を含まない長さ (31 文字まで)
    canopen_node_t nm;
    canopen_node_init(&nm, 3, "0123456789012345678901234567890123456789");
    drain(nm);
    CHECK_EQ(strlen(nm.device_name), 31u);
    size_t nlen = 0;
    uint8_t big[64];
    CHECK_EQ(canopen_node_sdo_read(&nm, 0x1008, 0, big, sizeof(big), &nlen), 0u);
    CHECK_EQ(nlen, 31u);
}

EMBED_TEST_MAIN()
