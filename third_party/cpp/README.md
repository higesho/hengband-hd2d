# UI の C++ 依存

UI を原作コアの `src/` に依存させないため、従来参照していた汎用ライブラリをここに分けた。

- `include/nlohmann/`: JSON for Modern C++。各ヘッダーの著作権・MIT ライセンス表記を保持する。
- `include/fmt/` と `fmt/format.cc`: fmt。各ファイルの著作権・ライセンス表記を保持する。

分離起点 72e5ef68e の `src/external-lib/` からバイト列を変えずにコピーした。
ここにゲームのルールや原作の型は含めない。コアのビルドは外部の作業用ソースに含まれる依存を使う。

追加: `include/picosha2/` は ZIP インポートの SHA-256 検証用。MIT ライセンスをヘッダーに保持。
取得元は https://github.com/okdshin/PicoSHA2 のコミット `27fcf6979298949e8a462e16d09a0351c18fcaf2`。
