# uitron (μITRON 4.0 仕様準拠 RTOS API ライブラリ)

`uitron` は、日本の組み込みシステムで標準的に使用されている **μITRON 4.0 仕様** に準拠したリアルタイムOS (RTOS) APIライブラリです。

組み込みベアメタル環境への移植や、ホスト環境（PC/CI環境）でのRTOSアプリケーションのテスト・シミュレーションを容易に行うことができます。

---

## 📑 目次
1. [特徴とメリット](#-特徴とメリット)
2. [サポートしているAPI仕様](#-サポートしているapi仕様)
3. [各機能の解説](#-各機能の解説)
   - [タスク管理機能](#1-タスク管理機能)
   - [タスク付属同期機能](#2-タスク付属同期機能)
   - [セマフォ (Semaphore)](#3-セマフォ-semaphore)
   - [イベントフラグ (Event Flag)](#4-イベントフラグ-event-flag)
   - [メールボックス (Mailbox)](#5-メールボックス-mailbox)
   - [固定長メモリプール (Fixed-size Memory Pool)](#6-固定長メモリプール-fixed-size-memory-pool)
   - [システム時刻・時間管理](#7-システム時刻時間管理)
   - [システム状態管理](#8-システム状態管理)
4. [実践的なサンプルコード](#-実践的なサンプルコード)

---

## 🎯 特徴とメリット

- **μITRON 4.0 標準準拠**: `cre_tsk`, `wai_sem`, `set_flg`, `snd_mbx`, `get_mpf` 等の標準サービスコール名をそのまま利用可能。
- **ゼロ動的メモリ割り当て対応**: 全オブジェクト制御ブロック（TCB, SCB, FCB, MCB, PCB）を静的配列で管理し、`malloc` が使えない環境でも完全動作。
- **優先度ベーススケジューリング**: 1（最高優先度）〜 16（最低優先度）のプリエンプティブ/協調スケジューリング。
- **Tick駆動のタイマー管理**: `uitron_os_tick()` または `isig_tim()` をタイマ割込みから呼ぶことで、タイムアウトや遅延 (`dly_tsk`) を制御。

---

## 📋 サポートしているAPI仕様

| カテゴリ | サービスコール (API) |
| :--- | :--- |
| **タスク管理** | `cre_tsk`, `del_tsk`, `act_tsk`, `iact_tsk`, `can_act`, `ext_tsk`, `exd_tsk`, `ter_tsk`, `chg_pri`, `get_pri`, `ref_tsk` |
| **タスク付属同期** | `slp_tsk`, `tslp_tsk`, `wup_tsk`, `iwup_tsk`, `can_wup`, `rel_wai`, `irel_wai`, `sus_tsk`, `rsm_tsk`, `dly_tsk` |
| **セマフォ** | `cre_sem`, `del_sem`, `sig_sem`, `isig_sem`, `wai_sem`, `pol_sem`, `twai_sem`, `ref_sem` |
| **イベントフラグ** | `cre_flg`, `del_flg`, `set_flg`, `iset_flg`, `clr_flg`, `wai_flg`, `pol_flg`, `twai_flg`, `ref_flg` |
| **メールボックス** | `cre_mbx`, `del_mbx`, `snd_mbx`, `isnd_mbx`, `rcv_mbx`, `prcv_mbx`, `trcv_mbx`, `ref_mbx` |
| **固定長メモリプール** | `cre_mpf`, `del_mpf`, `get_mpf`, `pget_mpf`, `tget_mpf`, `rel_mpf`, `ref_mpf` |
| **システム時刻** | `set_tim`, `get_tim`, `isig_tim` |
| **システム状態管理** | `loc_cpu`, `unl_cpu`, `dis_dsp`, `ena_dsp`, `sns_ctx`, `sns_loc`, `sns_dsp`, `sns_dpn` |

> **ブロッキング系サービスコールの注意**: 本ライブラリはホスト/協調実行向けで、呼出元の実行を中断できません。`wai_sem`・`twai_*`・`slp_tsk`・`dly_tsk` など、条件が即時に成立しない待ち系は、タスクを待ち状態へ登録して **`E_WBLK`** を返します(成功 `E_OK` ではありません)。解除は `sig_sem`/`set_flg`/`wup_tsk`/`uitron_os_tick` 等で行われ、状態は `ref_tsk` で確認します。`ext_tsk`/`ter_tsk` は、起動要求がキューイングされていれば再起動します。

### 実行モデル (協調ステップ実行)

タスクのエントリ関数は「1ステップ」で、`uitron_os_run_step()` から繰り返し呼ばれます。
ブロッキング系サービスコールで `E_WBLK` が返ったらその場で `return` し、待ちが解除された後の
次のステップで続きを実行します(フェーズ変数を持つ状態機械として書きます)。

- サービスコールの呼出元は、エントリ関数の実行中は常にそのタスク自身です。
  より高優先度のタスクを起こした後(例: `snd_mbx` の直後)に `dly_tsk` や `ext_tsk` を呼んでも、
  待ちや終了の対象は呼び出したタスクです。実際のディスパッチは次のステップ境界で行われます。
- 待ち行列の順序は `TA_TFIFO` なら待ちに入った順、`TA_TPRI` なら優先度順(同一優先度は待ちに入った順)です。
- 待ち解除時に結果を書き込む出力先(`wai_flg` の `p_flgptn`、`rcv_mbx` の `ppk_msg`、`get_mpf` の `p_blk`)は、
  待ちが解除されるまで有効でなければなりません。ローカル変数ではなく、静的変数などを渡してください。
- 待ちの結果(成立・タイムアウト・強制解除・オブジェクト削除)は戻り値として得られません。
  出力先が更新されたか、`ref_tsk` の状態で判断してください。

### テスト

| 実行ファイル | 内容 |
| :--- | :--- |
| `test_uitron` | 各サービスコールの戻り値・状態遷移の単体テスト(タスクの本体は実行しない) |
| `test_uitron_scenarios` | 複数タスクが協調動作するシナリオテスト。周期実行、センサ→処理→ログのパイプライン(メモリプール枯渇のバックプレッシャー付き)、セマフォの待ち行列順序、イベントフラグの合流とタイムアウト、割込みからのタスクチェーン、優先度制御、起動要求のキューイング、サスペンド・強制終了・オブジェクト削除 |

シナリオテストは `tests/uitron_sim.hpp` のヘルパーで 1 tick = 1 ms として時間を進め、
「どのタスクが・いつ・どの順序で・何回」動いたかをイベントログで検証します。

---

## 🔍 各機能の解説

### 1. タスク管理機能
タスクの生成・起動・終了・優先度変更を行います。

```c
T_CTSK ctsk = {
    .tskatr = TA_HLNG | TA_ACT, // 生成時即座に起動
    .itskpri = 5,               // 起動時優先度 5
    .task = my_task_func,       // エントリ関数
    .stksz = 1024,              // スタックサイズ
};
cre_tsk(1, &ctsk);
```

### 2. セマフォ (Semaphore)
共有リソースの排他制御や計数同期を行います。

```c
T_CSEM csem = {
    .sematr = TA_TFIFO,
    .isemcnt = 1, // バイナリセマフォ (排他用)
    .maxsem = 1,
};
cre_sem(1, &csem);

wai_sem(1); // 獲得
// クリティカルセクション処理...
sig_sem(1); // 返却
```

### 3. イベントフラグ (Event Flag)
複数ビットのフラグによるタスク間通知・複合条件待ちを行います。

- **`TWF_ANDW`**: 指定した全ビットがセットされるまで待つ
- **`TWF_ORW`**: 指定したビットのいずれかがセットされるまで待つ
- **`TWF_CLR`**: 条件成立時にフラグをクリアする

```c
T_CFLG cflg = {
    .flgatr = TA_WMUL,
    .iflgptn = 0x00,
};
cre_flg(1, &cflg);

// 送信側
set_flg(1, 0x01 | 0x02);

// 受信側 (0x01 と 0x02 の両方が揃ったらクリアして解除)
UINT ptn;
wai_flg(1, 0x03, TWF_ANDW | TWF_CLR, &ptn);
```

### 4. メールボックス (Mailbox)
メッセージポインタをキューイングしてタスク間でデータ受送信を行います。

```c
typedef struct {
    T_MSG head; // 先頭にT_MSGヘッダを含める
    int sensor_val;
} MyMessage;

MyMessage msg;
msg.sensor_val = 42;
snd_mbx(1, &msg.head);

// 受信
T_MSG *p_head;
rcv_mbx(1, &p_head);
MyMessage *recv = (MyMessage *)p_head;
```

### 5. 固定長メモリプール (Fixed-size Memory Pool)
メモリの断片化（フラグメンテーション）を起こさずに、固定サイズのブロックを $O(1)$ で高速に貸出・返却します。

```c
static uint8_t s_pool[64 * 8]; // 64バイト × 8ブロック
T_CMPF cmpf = {
    .mpfatr = TA_TFIFO,
    .blkcnt = 8,
    .blksz = 64,
    .mpf = s_pool, // 静的バッファ指定
};
cre_mpf(1, &cmpf);

void *blk = NULL;
get_mpf(1, &blk); // 獲得
// 使用...
rel_mpf(1, blk);  // 返却
```

---

## 💡 実践的なサンプルコード

```c
#include "uitron/itron.h"
#include "uitron/kernel.h"
#include "uitron/uitron_os.h"

void app_main(void) {
    // 1. カーネル初期化
    uitron_os_init();

    // 2. オブジェクト生成
    T_CSEM csem = { .sematr = TA_TFIFO, .isemcnt = 1, .maxsem = 1 };
    cre_sem(1, &csem);

    // 3. セマフォ操作
    if (pol_sem(1) == E_OK) {
        // リソース保護区間
        sig_sem(1);
    }
}
```
