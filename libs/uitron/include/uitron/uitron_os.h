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

#ifdef __cplusplus
}
#endif

#endif /* UITRON_OS_H */
