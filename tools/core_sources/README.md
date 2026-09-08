# 原作を入力として外部にコアのソースを再構成する

原作本文はこのツールのパッチに再収録しない。固定版の原作を入力し、範囲コピー・文字変換・明示された追加内容で出力する。再構成後の全ファイルをSHA-256で検証する。

## 配置

- `manifest.json`: 原作の固定コミット、入力/出力ファイルのハッシュ、各定義のハッシュ。
- `recipes/*.json`: 入力バッファの参照、コピー位置と長さ、追加内容、必要な出力形式。
- `reconstruction.py` / `reconstruction_recipe.py`: Pythonでの生成と再構成。
- `hd2d/app/source_reconstruction.cpp`: 製品と検査で使う同じ形式のC++実装。

原作のGitリポジトリは既定で `../roguelike-cores/upstream/`、再構成先は `../roguelike-cores/build-sources/`。原作の作業ツリーは変更せず、固定コミットのオブジェクトを読む。原作の自動取得はしない。

## 準備・検証・ビルド

```powershell
python tools/core_sources/prepare.py
python tools/core_sources/prepare.py --verify
python tools/core_sources/audit.py
./tools/core_sources/Build-Windows.ps1 -Python python
```

別の場所は `--upstream` / `--output`、または `HENGBAND_UPSTREAM_ROOT` / `HENGBAND_CORE_SOURCE_ROOT` で指定する。
既存の出力が現在の定義と一致しない場合は上書きせず、空の新しい出力先を指定する。
Visual Studioでは `HengbandCoreSourceRoot`、Android CMakeでは `HB_CORE_SOURCE_ROOT` を変更できる。
UI単体は原作不要。Androidは `HENGBAND_BUILD_CORES=OFF` でUIだけをビルドできる。

短愚のゲーム内容は従来どおり共通の変愚側ソースをTANGBAND指定でビルドして保持する。利用者向けのZIPインポートでは短愚のZIPに加え、共通部分を供給する変愚の固定版ZIPも必要。

## 変更の記録

```powershell
python tools/core_sources/capture.py --source-root D:/build/edited-sources
python tools/core_sources/capture.py --source-root D:/build/edited-sources --record
python tools/core_sources/prepare.py --output D:/build/verified-sources
```

`--record`なしは確認のみ。原文入りのunified diffやbinary patchは生成しない。
出力は元ファイルへの参照と追加内容に分かれる。追加内容が自作であるかは自動認定しないので、内容・出所をレビューする。
記録後は新しい出力先へ再構成してビルド・回帰検査を行う。原作の保管場所を編集対象にしない。

## 検査

`test_reconstruction.py --driver <reconstruction_driver.exe> --report <結果.json>` で範囲、ハッシュ、パス、文字変換、出力形式を確認する。
`audit_recipe.py <定義.json> <原作入力ディレクトリ> --report <結果.json>` は再構成と、まとまった原作の行・引用文字列の追加データへの混入を検査する。これは機械的な梱包検査であり著作権の判定ではない。
入力ディレクトリは `<archive-id>/<入力パス>` の配置とする。

旧`patches/*.patch`とschema 1は現行経路で使用しない。旧キットは新UIでの新規インポートに使わず再生成する。
既に登録した実行ファイルとセーブはこの形式変更で削除しない。
