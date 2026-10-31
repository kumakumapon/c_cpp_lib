# コントリビューションガイド

Issue や Pull Request を歓迎します。大きな変更は、実装前に Issue で方針を相談してください。

## 開発環境

- CMake 3.20 以上
- C11 / C++20 に対応したコンパイラ（MSVC、GCC、Clang）

## ビルドとテスト

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

テストは Debug と Release の両方で通る必要があります（検査は `NDEBUG` でも有効です）。
CI では次を検証しており、PR はすべて成功する必要があります。

- Linux / Windows、Debug / Release
- CRC のテーブル方式（`-DEMBED_CRC_TABLES=ON/OFF`）
- 共有ライブラリ（`-DBUILD_SHARED_LIBS=ON`）
- ASan / UBSan
- `cmake --install` 後の外部プロジェクトからの利用

## コーディング規約

- C ライブラリは C11、C++ は C++20 で書きます。
- 組み込み向けのため、ライブラリ本体では動的メモリ確保を避けてください。
- 外部から受け取る入力（バッファ、長さ）は必ず境界を検査し、失敗は戻り値で返してください。
- 整形は `.clang-format`（LLVM ベース、インデント4、100桁）に従います。
  静的解析は `.clang-tidy`（`bugprone-*`、`cert-*`）を使います。
- 警告は `target_set_warnings(<ターゲット名>)` で共通設定を適用してください。

## テストの書き方

新しいロジックには、規格や解析解に基づくテストを書いてください。

- 期待値は、規格書のバイト列・規定動作、または手計算した解析解にします。
  ライブラリ自身の encode / decode を往復させるだけのテストは、仕様を押さえたことになりません(補助にとどめます)。
- 正常系を 1 回通すだけでなく、境界値(`<` と `<=` の境目)と異常系も確認します。
- 追加したテストが実装の誤りを検出できるかは、変異テストで確かめられます。
  手順と最低ラインは [tools/mutation/README.md](tools/mutation/README.md) を参照してください。

## ライブラリやアプリの追加

手順は [README](README.md) の「新しいライブラリの追加方法」「新しいアプリケーションの追加方法」を
参照してください。新しいライブラリには次を含めてください。

- `libs/<名前>/README.md`（用途、API の要点、制約）
- `libs/<名前>/tests/` の単体テスト（`embed_test` を使用）。境界値と不正入力のテストを含めます。

## Pull Request

1. `main` からブランチを作成します（例: `feature/<内容>`、`fix/<内容>`、`docs/<内容>`）。
2. 変更とテストをコミットします。バグ修正には再現するテストを追加してください。
3. Pull Request を作成し、変更の目的とテスト方法を書きます。
4. CI がすべて成功したらレビューを依頼してください。

コミットメッセージは `feat:`、`fix:`、`docs:`、`test:` などの接頭辞で始めます。

## セキュリティ上の問題

脆弱性は公開の Issue に書かず、[SECURITY.md](SECURITY.md) の手順で報告してください。

## ライセンス

コントリビューションは、このリポジトリと同じ [MIT License](LICENSE) で公開されることに同意したものとみなします。
