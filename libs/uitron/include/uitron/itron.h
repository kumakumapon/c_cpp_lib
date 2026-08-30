#pragma once
#ifndef ITRON_H
#define ITRON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * 基本データ型 (μITRON 4.0 仕様準拠)
 * --------------------------------------------------------- */
typedef int8_t      B;      /**< 符号付き 8ビット整数 */
typedef int16_t     H;      /**< 符号付き 16ビット整数 */
typedef int32_t     W;      /**< 符号付き 32ビット整数 */
typedef int64_t     D;      /**< 符号付き 64ビット整数 */

typedef uint8_t     UB;     /**< 符号なし 8ビット整数 */
typedef uint16_t    UH;     /**< 符号なし 16ビット整数 */
typedef uint32_t    UW;     /**< 符号なし 32ビット整数 */
typedef uint64_t    UD;     /**< 符号なし 64ビット整数 */

typedef int32_t     INT;    /**< 符号付き整数 */
typedef uint32_t    UINT;   /**< 符号なし整数 */

typedef bool        BOOL;   /**< 真偽値 */
#ifndef TRUE
#define TRUE        1
#endif
#ifndef FALSE
#define FALSE       0
#endif

typedef void*       VP;     /**< 未定義型のポインタ */
typedef void        (*FP)(void); /**< 関数ポインタ */

typedef INT         ER;     /**< エラーコード */
typedef INT         ER_UINT;/**< エラーコードまたは符号なし整数 */
typedef INT         ID;     /**< オブジェクトID (1以上) */
typedef UINT        ATR;    /**< オブジェクト属性 */
typedef UINT        STAT;   /**< オブジェクト状態 */
typedef UINT        MODE;   /**< サービスコール動作モード */
typedef INT         PRI;    /**< タスク優先度 (1以上、小さい値ほど高優先度) */
typedef UINT        TMO;    /**< タイムアウト指定 (ミリ秒) */
typedef UINT        RELTIM; /**< 相対時間 (ミリ秒) */
typedef UD          SYSTIM; /**< システム時刻 (ミリ秒) */
typedef uintptr_t   VP_INT; /**< ポインタまたは整数 */

/* ---------------------------------------------------------
 * 定数定義
 * --------------------------------------------------------- */
/* タイムアウト指定 */
#define TMO_POL     ((TMO)0)        /**< ポーリング (待ちなし) */
#define TMO_FEVR    ((TMO)(~0U))    /**< 永久待ち */

/* オブジェクトID */
#define TSK_SELF    0               /**< 自タスク指定 */

/* ---------------------------------------------------------
 * エラーコード (μITRON 4.0 共通仕様)
 * --------------------------------------------------------- */
#define E_OK        0       /**< 正常終了 */
#define E_SYS       (-5)    /**< システムエラー */
#define E_NOSPT     (-9)    /**< 未サポート機能 */
#define E_RSFN      (-10)   /**< 予約機能コード */
#define E_RSATR     (-11)   /**< 予約属性 */
#define E_PAR       (-17)   /**< パラメータ不正 */
#define E_ID        (-18)   /**< 不正ID番号 */
#define E_CTX       (-25)   /**< コンテキストエラー */
#define E_MACV      (-26)   /**< メモリアクセス違反 */
#define E_OACV      (-27)   /**< オブジェクトアクセス違反 */
#define E_ILUSE     (-28)   /**< サービスコール不正使用 */
#define E_NOMEM     (-33)   /**< メモリ不足 */
#define E_NOID      (-34)   /**< ID番号不足 */
#define E_OBJ       (-41)   /**< オブジェクト状態不正 */
#define E_NOEXS     (-42)   /**< オブジェクト未生成 */
#define E_QOVR      (-43)   /**< キューイングオーバーフロー */
#define E_RLWAI     (-49)   /**< 待ち状態の強制解除 */
#define E_TMOUT     (-50)   /**< ポーリング失敗またはタイムアウト */
#define E_DLT       (-51)   /**< 待ちオブジェクトの削除 */
#define E_CLS       (-52)   /**< 待ちオブジェクトの状態変化 */
#define E_WBLK      (-57)   /**< ノンブロッキング受付 */
#define E_BOVR      (-58)   /**< バッファオーバーフロー */

#ifdef __cplusplus
}
#endif

#endif /* ITRON_H */
