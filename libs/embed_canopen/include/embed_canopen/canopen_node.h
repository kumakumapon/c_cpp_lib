#pragma once
#ifndef CANOPEN_NODE_H
#define CANOPEN_NODE_H

#include "canopen_types.h"
#include "canopen_od.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct canopen_node canopen_node_t;

/** アプリケーションへの通知 (すべて NULL 可) */
typedef struct {
    /** NMT 状態が変わった */
    void (*on_nmt_change)(canopen_node_t *node, canopen_nmt_state_t state, void *user);
    /** Reset Node / Reset Communication を受けた。必要ならここで通信パラメータを既定値へ戻す */
    void (*on_nmt_reset)(canopen_node_t *node, canopen_nmt_cmd_t cmd, void *user);
    /** SYNC を受けた (Operational / Pre-Operational) */
    void (*on_sync)(canopen_node_t *node, void *user);
    /** RPDO の値を OD へ反映した (pdo_num は 1〜4) */
    void (*on_rpdo)(canopen_node_t *node, uint8_t pdo_num, void *user);
    /** 監視中ノードの Heartbeat が途絶えた */
    void (*on_heartbeat_timeout)(canopen_node_t *node, uint8_t remote_id, void *user);
} canopen_node_callbacks_t;

/** PDO マッピングの 1 項目 (バイト単位のマッピングのみ: bit_length は 8/16/32/64) */
typedef struct {
    uint16_t index;
    uint8_t subindex;
    uint8_t bit_length;
} canopen_pdo_map_entry_t;

/** PDO の通信・マッピングパラメータと実行時状態 (内部用。API / SDO 経由で変更すること) */
typedef struct {
    uint32_t cob_id;            /**< bit31 = 1 で無効。下位 11bit が CAN ID */
    uint8_t transmission_type;  /**< 0: 非周期同期, 1..240: n 回目の SYNC ごと, 254/255: イベント */
    uint16_t inhibit_100us;     /**< TPDO の送信禁止時間 (100 µs 単位) */
    uint16_t event_timer_ms;    /**< TPDO のイベントタイマ (0 で無効) */
    uint8_t map_count;
    uint32_t map[CANOPEN_PDO_MAX_MAP]; /**< index<<16 | subindex<<8 | bit_length */
    /* 実行時 */
    uint32_t timer_ms;
    uint32_t since_tx_ms;
    uint8_t sync_count;
    bool pending;
    bool has_last;
    uint8_t last[CANOPEN_MAX_CAN_DATA_LEN];
    uint8_t rx_data[CANOPEN_MAX_CAN_DATA_LEN];
    bool rx_pending;
} canopen_pdo_t;

struct canopen_node {
    uint8_t node_id;
    canopen_nmt_state_t nmt_state;
    canopen_od_t od;

    /* 標準通信パラメータ (OD に登録済み) */
    uint32_t device_type;           /**< 0x1000: Device Type */
    uint8_t  error_register;        /**< 0x1001: Error Register */
    uint32_t cob_id_sync;           /**< 0x1005: COB-ID SYNC */
    char     device_name[32];       /**< 0x1008: Manufacturer Device Name */
    uint32_t vendor_id;             /**< 0x1018.1: Vendor ID */
    uint32_t product_code;          /**< 0x1018.2: Product Code */
    uint32_t revision_number;       /**< 0x1018.3: Revision Number */
    uint32_t serial_number;         /**< 0x1018.4: Serial Number */
    uint16_t heartbeat_producer_ms; /**< 0x1017: Producer Heartbeat Time [ms] */
    uint32_t heartbeat_timer_ms;

    /* 0x1003: Pre-defined error field (新しいものが sub1) */
    uint8_t error_count;
    uint32_t error_history[CANOPEN_ERROR_HISTORY];

    /* 0x1016: Consumer heartbeat time (node id << 16 | 監視時間 ms) */
    uint32_t hb_consumer[CANOPEN_MAX_HB_CONSUMERS];
    struct {
        uint32_t elapsed_ms;
        canopen_nmt_state_t state;
        bool seen;
        bool timed_out;
    } hb_remote[CANOPEN_MAX_HB_CONSUMERS];

    canopen_pdo_t tpdo[CANOPEN_MAX_TPDO];
    canopen_pdo_t rpdo[CANOPEN_MAX_RPDO];

    /* SDO サーバ */
    struct {
        uint8_t state;
        uint16_t index;
        uint8_t subindex;
        uint8_t toggle;
        bool size_indicated;
        size_t size;
        size_t offset;
        uint32_t idle_ms;
        uint8_t buf[CANOPEN_SDO_BUFFER_SIZE];
    } sdo;
    uint32_t sdo_timeout_ms;        /**< 分割転送の無通信タイムアウト (既定 1000 ms、0 で無効) */

    /* 送信キュー */
    can_frame_t txq[CANOPEN_TX_QUEUE_SIZE];
    uint8_t txq_head;
    uint8_t txq_count;
    uint32_t tx_overflows;          /**< キュー満杯で捨てたフレーム数 */

    canopen_node_callbacks_t callbacks;
    void *callback_user;
};

/**
 * @brief CANopen ノードを初期化し、標準通信ODエントリ (0x1000〜0x1018) を構成します。
 *
 * CiA 301 に従いブートアップメッセージ (0x700 + NodeID, データ 0x00) を送信キューに積み、
 * Pre-Operational へ移行します。
 * @param node_id ノードID (1〜127。範囲外は 1)
 */
void canopen_node_init(canopen_node_t *node, uint8_t node_id, const char *device_name);

void canopen_node_set_callbacks(canopen_node_t *node, const canopen_node_callbacks_t *callbacks,
                                void *user);

/**
 * @brief 受信したCANフレームを処理します。
 *
 * 生成されたフレーム (SDO 応答、SYNC による TPDO など) は送信キューに積まれ、
 * 先頭の 1 フレームを tx_frame に取り出して true を返します。
 * 残りは canopen_node_pop_tx で取り出してください。
 */
bool canopen_node_process_frame(canopen_node_t *node, const can_frame_t *rx_frame, can_frame_t *tx_frame);

/**
 * @brief 時間を進めます (Heartbeat、TPDO のイベントタイマ・変化検出、SDO タイムアウト、
 *        Heartbeat 監視)。生成フレームの扱いは process_frame と同じです。
 */
bool canopen_node_timer_tick(canopen_node_t *node, uint32_t elapsed_ms, can_frame_t *tx_frame);

/** 送信キューから 1 フレーム取り出します。空なら false。 */
bool canopen_node_pop_tx(canopen_node_t *node, can_frame_t *tx_frame);
size_t canopen_node_tx_pending(const canopen_node_t *node);

/**
 * @brief TPDO1 の COB-ID で生データのフレームを作ります (マッピングを使わない簡易送信)。
 *        Operational 以外では false。
 */
bool canopen_node_send_tpdo1(const canopen_node_t *node, const uint8_t *data, uint8_t len, can_frame_t *tx_frame);

/* ---- PDO ---- */
/**
 * @brief TPDO を構成して有効化します (SDO で 0x1800/0x1A00 を書くのと同じ検査)。
 * @param pdo_num 1〜4
 * @param transmission_type 0 / 1..240 / 254 / 255
 * @return マッピング不正 (オブジェクト不在・読めない・長さ不一致・64bit 超) なら false (PDO は無効のまま)
 */
bool canopen_node_config_tpdo(canopen_node_t *node, uint8_t pdo_num,
                              const canopen_pdo_map_entry_t *map, uint8_t count,
                              uint8_t transmission_type, uint16_t event_timer_ms);
/** RPDO を構成して有効化します。0..240 は次の SYNC で反映、254/255 は受信時に即反映。 */
bool canopen_node_config_rpdo(canopen_node_t *node, uint8_t pdo_num,
                              const canopen_pdo_map_entry_t *map, uint8_t count,
                              uint8_t transmission_type);
/** TPDO の送信を要求します (0: 次の SYNC、254/255: 禁止時間が過ぎていれば即時)。 */
bool canopen_node_trigger_tpdo(canopen_node_t *node, uint8_t pdo_num);
/** TPDO の送信禁止時間を設定します (100 µs 単位。PDO が無効なときだけ)。 */
bool canopen_node_set_tpdo_inhibit(canopen_node_t *node, uint8_t pdo_num, uint16_t inhibit_100us);

/* ---- EMCY / エラー ---- */
/**
 * @brief EMCY を送信し、エラーレジスタ (0x1001) と履歴 (0x1003) を更新します。
 * @param error_code CANOPEN_EMCY_*。0 はエラーリセット (レジスタを err_reg_bits に戻す)
 * @param manufacturer 5 バイトのメーカー固有データ (NULL なら 0)
 * @return Stopped では送信できず false
 */
bool canopen_node_emcy(canopen_node_t *node, uint16_t error_code, uint8_t err_reg_bits,
                       const uint8_t *manufacturer);

/* ---- Heartbeat 監視 (0x1016) ---- */
/** remote_id を timeout_ms で監視します (最初の Heartbeat を受けてから監視開始)。 */
bool canopen_node_add_heartbeat_consumer(canopen_node_t *node, uint8_t remote_id,
                                         uint16_t timeout_ms);
/** 監視中ノードの最後の NMT 状態。未受信・未登録なら false。 */
bool canopen_node_get_remote_state(const canopen_node_t *node, uint8_t remote_id,
                                   canopen_nmt_state_t *state);

/* ---- ローカルからの SDO 相当アクセス (CANopen のバイト順 = リトルエンディアン) ---- */
/** 通信プロファイルの仮想オブジェクト (0x1003, 0x1016, 0x1400〜0x1A03) を含めて読む。0 またはアボートコード */
uint32_t canopen_node_sdo_read(canopen_node_t *node, uint16_t index, uint8_t subindex,
                               uint8_t *buf, size_t cap, size_t *len);
uint32_t canopen_node_sdo_write(canopen_node_t *node, uint16_t index, uint8_t subindex,
                                const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_NODE_H */
