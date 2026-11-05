# embed_sched

C11、動的確保なしの64スロット・32タイマのタイマホイールと、最大16タスクの協調スケジューラ。
呼び出し側が64bit単調時刻と時刻単位（例:1ms）を決めます。

```c
embed_sched scheduler;
embed_sched_init(&scheduler, 0, clock_ms, user);
embed_sched_start(&scheduler, 0, 10, 10, sample_sensor, user);
embed_sched_add_task(&scheduler, background_work, user, 2);
/* スーパーループ */
embed_sched_run_once(&scheduler);
```

startのidは0〜31、delay>=1、period=0はワンショット。startは同IDを置換します。
cancelは未起動でも安全です。挿入・取消は双方向リストでO(1)。advanceは経過tick数と
各バケットのタイマ数に比例し、全32タイマを毎tick走査しません。期限切れの実行自体はO(k)です。
大きな時間ジャンプは経過tickと周期分を追いつくまで処理するため、定期的にpollしてください。

run_onceは時計を読み、期限の来たタイマを実行し、次のタスクを1つラウンドロビン実行します。
タスクはブロックせず戻る必要があります。遅れて処理したタイマと、budgetを超えたタスクを
`missed` に加算します。時計逆行はfalse、uint64_tの時刻周回はサポートしません。
周期再登録のオーバーフロー時はそのタイマを停止します。
タイマコールバック内のstart/cancelは可能ですが、initやadvance/run_onceの再入は禁止です。
並行呼び出しには外部同期が必要です。

## 問い合わせ・tickless スリープ

```c
uint64_t wake;
if (embed_sched_next_due(&scheduler, &wake)) {
    /* wake (絶対時刻) まで低消費電力モードで眠る。RTC/LPTIM の比較値に設定する */
}
uint64_t left;
embed_sched_remaining(&scheduler, 0, &left); /* タイマ 0 の期限までの tick */
embed_sched_is_active(&scheduler, 0);
embed_sched_remove_task(&scheduler, background_work, user);
```

- `next_due` は起動中の全タイマを走査します (O(タイマ数))。
- 目標時刻までが64 tickを超えるadvanceでは、次の期限の直前まで一気に進めます。
  長時間スリープ後の追いつきでもtickごとの空回りはしません (期限の来たタイマは全て、正しい時刻で実行)。
- `remove_task` はタスク・タイマのコールバック内からも呼べます。ラウンドロビンの順序は保たれます。
- 統計は `late_timers` (目標時刻より前が期限だったタイマ)、`task_overruns` (budget超過)、
  `missed` (両者の合計、互換用) に分かれています。budgetちょうどは超過に数えません。
