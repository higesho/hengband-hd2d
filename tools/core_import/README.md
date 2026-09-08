# UI から原作ソース ZIP をインポートする

Windows x64 と Android 13 以降の arm64 が対象。コンパイラーは同梱し、プレイヤー側で Python・Git・Visual Studio・Termux を導入する必要はない。
Windows は実際の UI 操作とゲーム動作を検証した。Android は APK、5 コアのクロスビルド、エミュレーターでの ZIP 選択と画面復帰まで検証し、端末内ビルド・生成したコアの起動は ARM 実機確認待ち。

## プレイヤーの操作

1. コア選択画面の「コアをインポート」を押す。Windows は F8 または ZIP のドラッグ＆ドロップでも開ける。
2. 対象のコアと対応版を確認し、「ZIP追加」で原作ソースのZIPを選択する。短愚は短愚と変愚の2つを追加する。「選択解除」で選び直せる。
3. 「ビルド」を押す。進捗を表示し、ビルド中はキャンセルできる。
4. 完了後に戻ると、取り込んだコアを選んで起動できる。

コアがゼロ・1 個でも選択画面を表示する。失敗・キャンセルで以前のコア登録やセーブを変更しない。
インポート完了後のゲーム起動にはコンパイラーも原作 ZIP も不要。
Android のファイル選択は Storage Access Framework を使い、広範囲のストレージ権限を求めない。
ゲームデータ・タイル・翻訳は UI の配布物を利用する。

## 対応するソース

ファイル名ではなく内容で検証する。下表のコミットに対応する未改変のソースを使う。
ZIP 内の先頭フォルダー名は任意。改行と UTF-8 BOM の違いは正規化する。

| コア | 対応版 | 固定コミットのソース |
|---|---|---|
| hengband | 3.0.2.4-Beta | [ZIP](https://github.com/hengband/hengband/archive/43106a70d7057dc6c506bac96b0888ecdaddb4b8.zip) |
| tangband | 26.0.4 | [ZIP](https://github.com/tanguband/tangband/archive/8d8e1f2cd74fe467a153d7dbdb0b2c5ed0ab72e2.zip) |
| gensoband | 2.1.6 | [ZIP](https://github.com/asamayim/gensoband/archive/fc4418078a62db9fae2915c8b6ab24a063609093.zip) |
| silq | 1.5.1.0-beta2 | [ZIP](https://github.com/sil-quirk/sil-q/archive/d0355bf97e873ba10e6b364aa127bc5ebbbd05f1.zip) |
| frox | 7.3.pipari.2 | [ZIP](https://github.com/Gliktch/froxcomposband/archive/c7641efca8caf6069aa9a71a4b5c8547a64013be.zip) |

短愚は表にある短愚と変愚の固定版ZIPを両方使う。選択順序は任意。共通ソースを入力からコピー・変換して再構成し、TANGBAND指定でビルドする。
任意の別版への自動対応、改造版、元のゲーム EXE の取り込みは対象外。

## インポート処理

- ZIPは最大5個、選択したZIPの合計512 MiB以下。各ZIPの展開量は1 GiB以下、再構成する全ファイルも合計1 GiB以下、1ファイル64 MiB以下。通常の ZIP の store / deflate に対応する。
- パス逸脱・絶対パス・重複・リンク・CRC 不一致を拒否する。暗号化・分割・ZIP64 は対象外。
- 原作の必要ファイル、範囲コピー・変換後のファイル、接続 SDK を SHA-256 で照合する。
- ZIP 内の Makefile・シェル・実行ファイルは実行しない。同梱したコンパイル定義とコンパイラーだけを使用する。
- 別プロセスでビルドし、UI を止めない。Windows は最大 4、Android は最大 2 プロセスでコンパイルする。
- コンパイラー失敗・中止・保存失敗では登録を切り替えない。成功時は別世代へ生成物を置き、登録ファイルを原子的に更新する。
- 登録済みの同名コアを優先する。選択画面ではコアを起動せず、プレイヤーが選んだ時点で起動する。
- 作業用ソース・オブジェクトは終了後に掃除し、ログは保存する。プレイヤーが選んだ元の ZIP は変更しない。

## 保存先と実行

登録と生成物は SDL_GetPrefPath("Hengband", "HD2D") 配下の `core-import/` に置く。
Windows の通常の保存先は `%APPDATA%/Hengband/HD2D/core-import/`。`HENGBAND_IMPORT_HOME` で変更できる。

- `registry.json`: コアごとの現在の実行ファイル・版・ハッシュ。
- `installed/<コア>-<世代>/`: 生成した EXE / SO。以前の世代は保持する。
- `work/<コア>-<世代>/build.log`: ビルドの記録。

Windows は別プロセスとして実行し、既存のパイプ通信を使う。データの基準は UI が `HENGBAND_DATA_ROOT` で渡す。
Android はアプリ内部の生成した SO を読み込み、従来と同じコアスレッド・通信を使う。コンパイラーは APK の nativeLibraryDir に格納し、書き込み可能な作業フォルダーからコンパイラーを実行しない。

## 同梱コンパイラー

- Windows: LLVM-MinGW UCRT 20260826。x86 のコアを生成する。コンパイルに必要なバイナリー、DLL、ヘッダー、ライブラリと許諾だけを同梱する。
- Android: lzhiyong/termux-ndk r29 の arm64 ホスト版 Clang 21 / LLD と sysroot。リンク起動ヘルパーは本リポジトリのコード。
- 取得元・固定ハッシュ: `tools/core_import/toolchains.json`。
- 許諾: `THIRD-PARTY-NOTICES.txt` とキット内の LICENSE / NOTICE / COPYING を保持する。

Windows UI 自体の従来の実行環境（OpenGL、VC++ ランタイム等）は従来どおり。

## 開発者がキットを作る手順

Windows の既存開発環境で実行する。原作は UI の外に保持し、事前に `tools/core_sources/prepare.py` で準備する。

```powershell
python tools/core_import/package_kit.py --platform windows
```

Android 分も作る場合は、まず Gradle の `:hd2d:externalNativeBuildPreview` で 5 コアのコンパイル定義と SDL ライブラリを用意してから実行する（原作準備と既存の SDK 設定が必要）。

```powershell
# android/ で実行。コンパイル定義を作る段階ではアセットの生成は不要。
gradle :hd2d:externalNativeBuildPreview -PhengbandAbi=arm64-v8a -PhengbandBuildCores=true
# リポジトリ直下へ戻って実行。
python tools/core_import/package_kit.py --platform all
```

生成先は `core-import/`、`android/core-import-kit/`、`android/core-import-jni/`。Git 管理しない。
開発者向けツールであり、プレイヤーのインポート時には実行しない。SDK 更新時は UI を終了し、キットを再生成する。

## コアを含まない配布物

```powershell
./tools/package/Build-Hd2dPackage.ps1 -WithoutCores
./tools/package/Build-Hd2dApk.ps1 -WithoutCores
```

`-WithoutCores` を省略すると従来どおりコアも同梱する。
コアなし版でも、同梱コンパイラー、接続 SDK、ゲームデータ、翻訳、描画資産は必要。


## 検証

共通処理の検査入口は `tools/core_import/driver.cpp`（CMakeLists.txt でビルド可能）。
`test_import.py` が不正入力とキャンセル時の登録保持を確認する。
`tools/hd2d_verify/test_core_import.py` で実 UI の取り込みを検査できる。
Windowsでは5コアの生成・登録とゲーム動作を検証済み。Android ARM64端末内でのコンパイルと生成したSOの起動は実機確認待ち。

## 配布キットを作る開発者向けの手順

原作を準備してから、固定ハッシュのコンパイラーと接続SDKを生成する。遊ぶ人が実行する手順ではない。

```powershell
python tools/core_sources/prepare.py
python tools/core_import/package_kit.py --platform windows
./tools/package/Build-Hd2dPackage.ps1 -WithoutCores
```

Androidは先に5コアをビルドしてコンパイル定義を生成する。`package_kit.py --platform android --ndk <NDKの場所>` でキットを作り、`Build-Hd2dApk.ps1 -WithoutCores` を実行する。SDKやコンパイラーの生成物はGitへ追加しない。

## 再構成定義の更新

schema 2は原作ZIP内の不変入力への範囲参照、CP932等の変換、明示された追加内容でソースを復元する。旧schema 1のキットは新UIで使用できないため、開発者はキットを再生成する。登録済みの実行ファイルとセーブは保持する。原作や追加コードの使用許諾を変更するものではない。
