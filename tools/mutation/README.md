# 変異テスト

`libs/<lib>/src/*.c` の演算子を 1 か所ずつ壊し(`==`↔`!=`、`<`↔`<=`、`>`↔`>=`、`&&`↔`||`、`+`↔`-`)、
そのライブラリのテストが失敗するかを数えます。

    検出率 = テストが失敗した変異 / 有効な変異

コンパイルできない変異と、許可リストの同値変異は数えません。

```sh
python tools/mutation/mutate.py --list                       # 対象ライブラリ
python tools/mutation/mutate.py embed_pid                    # 1 ライブラリ (生き残った変異を表示)
python tools/mutation/mutate.py embed_pid -v --jobs 8        # 検出された変異も表示、8 並列
python tools/mutation/mutate.py --all --check                # 全ライブラリ。baseline.json を下回れば失敗
python tools/mutation/mutate.py embed_pid --update-baseline  # 検出率を baseline.json に記録
```

- 作業ツリーは書き換えません。git 管理下のファイルを一時ディレクトリへコピーし、その上で測定します。
- 変異の抽出は `--seed`(既定固定)と `--count`(既定 80)で決まるので、同じソースなら再現できます。
- 要件は Python 3.9 以上、CMake、C/C++ コンパイラです。Windows では Visual Studio の構成でも動きますが遅いので、
  WSL / Linux での実行を勧めます。

## 最低ライン (`baseline.json`)

ライブラリごとの検出率を記録しています。`--check` は、記録より `--tolerance`(既定 5 ポイント)を超えて
下回ると失敗します。ソースを変えると抽出される 80 件が入れ替わるため、許容差を設けています。
テストを強化して検出率が上がったら、`--update-baseline` で最低ラインを引き上げてください。

## 同値変異 (`equivalent.json`)

壊してもプログラムの振る舞いが変わらない変異(例: 連続な点で `<` と `<=` が同じ結果になる)は、
テストでは検出できません。生き残った変異を確認し、本当に同値と判断できるものだけ、理由を添えて登録します。

```json
{"file": "libs/embed_motion/src/motion_profile.c",
 "line": "} else if (t < t3) {",
 "change": "< -> <=",
 "reason": "t == t3 で加速度・速度・位置が連続なので、どちらの区間で評価しても同じ値になる"}
```

`line` は前後の空白を除いたソース行の全文で、行番号ではありません(行がずれても有効です)。
生き残ったからといって安易に登録せず、まずテストで検出できないか考えてください。

## CI

`.github/workflows/mutation.yml` が夜間と手動(`workflow_dispatch`)で、ライブラリごとに `--check` を実行します。
PR では実行しません(時間がかかるため)。
