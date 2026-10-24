#pragma once
#ifndef UITRON_OS_H
#define UITRON_OS_H

#include "itron.h"
#include "kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief μITRON OS コアの最大管理オブジェクト数設定
 */
#ifndef UITRON_MAX_TSKID
#define UITRON_MAX_TSKID 16
#endif

#ifndef UITRON_MAX_SEMID
#define UITRON_MAX_SEMID 16
#endif

#ifndef UITRON_MAX_FLGID
#define UITRON_MAX_FLGID 16
#endif

#ifndef UITRON_MAX_MBXID
#define UITRON_MAX_MBXID 16
#endif

#ifndef UITRON_MAX_MPFID
#define UITRON_MAX_MPFID 16
#endif

#ifndef UITRON_MAX_PRI
#define UITRON_MAX_PRI   16
#endif

/**
 * @brief μITRON OS を初期化します。
 */
void uitron_os_init(void);

/**
 * @brief μITRON OS をリセット/シャットダウンします。
 */
void uitron_os_shutdown(void);

/**
 * @brief システムタイマのTickを進めます (タイマ割込みハンドラから呼び出し)。
 * @param ms 経過ミリ秒 (通常 1)
 */
void uitron_os_tick(RELTIM ms);

/**
 * @brief 現在実行状態 (RUNNING) のタスクIDを取得します。
 */
ID uitron_os_get_current_task(void);

/**
 * @brief スケジューラを実行し、最高優先度のREADYタスクをディスパッチします。
 */
void uitron_os_schedule(void);

/**
 * @brief タスクのエントリ関数を呼び出してステップ実行します (シミュレーション/コルーチン実行用)。
 */
void uitron_os_run_step(void);

/**
 * @brief 待ちの結果を取得します (本ライブラリ独自の拡張)。
 *
 * ブロッキング系サービスコール (wai_sem, twai_flg, rcv_mbx, get_mpf, slp_tsk, dly_tsk など) は、
 * 呼出元を中断できないため待ちを登録して E_WBLK を返します。待ちが解除された後の次のステップで、
 * 本関数により μITRON 本来の戻り値に相当する結果を得られます。
 *
 * 結果は、そのタスクが最後に待ちへ入った回のものです。待ちを登録せずに戻ったサービスコール
 * (即時成立・ポーリング失敗) の結果は含みません。E_WBLK が返った後に読んでください。
 *
 * @param tskid 対象タスク (TSK_SELF で呼出元)
 * @retval E_OK     待ち条件が成立した (資源獲得・メッセージ受信・起床・dly_tsk の時間経過など)
 * @retval E_TMOUT  タイムアウトした
 * @retval E_RLWAI  rel_wai / irel_wai で強制解除された
 * @retval E_DLT    待っていたオブジェクトが削除された
 * @retval E_WBLK   まだ待ち中 (待ち状態・二重待ち状態)
 * @retval E_NOEXS  タスクが存在しない
 * @retval E_CTX    TSK_SELF をタスク外 (実行中のタスクがない状態) から指定した
 */
ER uitron_wait_result(ID tskid);

#ifdef __cplusplus
}
#endif

#endif /* UITRON_OS_H */
