# 変愚蛮怒HD（Hengband HD）

[日本語](#日本語) | [English](#english)

---

## 日本語

変愚蛮怒（Hengband）を 3D 表示にした派生版です。画面部分を新しく作り、
変愚蛮怒・短愚蛮怒・幻想蛮怒・Sil-Q・FroxComposband・Angband・Umoria・Rogue 3.6・Rogue 5.4・RogueClone2 の 10 本を
同じ画面で遊べるようにしました。

作者: Higesho

非公式の派生版です。Hengband の公式リポジトリ [hengband/hengband](https://github.com/hengband/hengband)
とは関係がありませんので、不具合があってもそちらには報告しないでください。

### ダウンロード

ビルド済みのものを Releases に置いています。ボクセルモデルや音などの素材も入っています（タイル画像は入っていません）。

- Windows: [HengbandHD-win.zip](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHD-win.zip)
- Android: [HengbandHD-android.apk](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHD-android.apk)
- Linux (x86_64): [HengbandHD-linux-x86_64.tar.gz](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHD-linux-x86_64.tar.gz)

紹介ページ: <https://higesho.github.io/>

### 遊び方

- **Windows**: zip を展開して `HengbandHd2d.exe` を起動してください。最初に、どのゲームで遊ぶかを選ぶ画面が出ます。
  64 ビットの Windows と OpenGL 4.6 が使えるグラフィックス、Microsoft Visual C++ 再頒布可能パッケージ（x64）が要ります。
  ゲーム本体（コア）の exe は `cores\` の中にあります。画面側から起動される作りなので、単体では起動しません。
- **Linux**: tar.gz を展開して `./HengbandHd2d.sh` を起動してください。OpenGL 4.5 が使える環境が要ります。詳しくは展開した中の `README-linux.txt` を見てください。
- **Android**: apk をインストールしてください（arm64-v8a）。

現在の配布物は 10 本のコアを組み込み済みで、原作の ZIP を取り込んでコンパイルする機能（`tools/core_import/`）は
ソースにはありますが、配布物には含めていません。

### 10 本のゲーム

| ゲーム | 説明 |
| --- | --- |
| 変愚蛮怒 | Hengband 3.0.2.5-Beta です |
| 短愚蛮怒 | 変愚蛮怒の短編版です（同じソースを別の設定でビルドしています） |
| 幻想蛮怒 | 東方 Project をモチーフにした変愚蛮怒の派生版です。町とダンジョンを作り込み、英語版も用意しました |
| Sil-Q | Sil の後継です。日本語化しました |
| FroxComposband | Composband の派生版です。日本語化しました |
| Angband | Angband 4.2.6 です。日本語表示に対応しました |
| Umoria | Umoria 5.7 系です。日本語化しました |
| Rogue 3.6 / Rogue 5.4 | 古典の Rogue です。英語表示にも対応しました |
| RogueClone2 | Rogue のクローン（RogueClone2）です。日本語版をもとにしています |

それぞれ独立した実行ファイルで、画面とは独自のプロトコルで通信します。
画面側はゲームごとの固有の知識を持たず、コアが自分の名前と機能を伝え、画面がそれに合わせます。

### 元の Hengband からの変更点

Moria/Angband 使用許諾の条件に従い、Hengband 3.0.2.3-Beta からの変更点を書きます。

追加したもの:

- 3D 表示（`hd2d/`）。OpenGL 4.6 で、影・被写界深度・ブルーム・昼夜と天候があります
- 画面とコアを別プロセスに分け、パイプで通信するようにしました（`presentation/` `platform/`）
- 9 つのゲームを追加しました（`gensoband/` `silq/` `frox/` `tangband/` `angband/` `umoria/` `rogue2/` `rogue36/` `rogue54/` `rogue_legacy/`）
- Android、Meta Quest、VR（OpenXR）に移植しました
- 視点モード 4 種類、TRON 風の表示、リアルタイムモード
- 幻想蛮怒の英語版、Sil-Q と FroxComposband の日本語版

変更したもの:

- 外部への通信をなくしました。元の Hengband にはクラッシュ時に開発チームのサーバーへ報告を送る機能が
  ありますが、この派生版では無効にしています（`DISABLE_NET`）。幻想蛮怒のワールドスコア送信も同様です。
  詳しくは [LICENSE](LICENSE) の §1.1 をご覧ください
- 元の 2D 版（`Hengband.exe`）のビルドと、それだけが使っていた libcurl を外しました
- 元の CI 設定を外しました

原作ソースを外部へ分離し、対応する原作 ZIP の検証・コンパイル・登録を画面から行う機能を追加しました。

### ビルド

#### 原作の用意

原作のソースとゲームデータはこのリポジトリに含めません。固定した版を外部に用意します。
次の表のリポジトリを `../roguelike-cores/upstream/<名前>/` に clone し、固定のコミットに合わせてください
（detached HEAD で構いません。場所は `--upstream` か環境変数 `HENGBAND_UPSTREAM_ROOT` で変えられます）。
コミットは `tools/core_sources/manifest.json` にも書いてあります。

| 名前 | 取得元 | 固定のコミット |
| --- | --- | --- |
| `hengband` | <https://github.com/hengband/hengband>（変愚蛮怒 3.0.2.5-Beta） | `18b10d6ea5fd234c119053e0a209c90aede21fa4`。短愚蛮怒のために同じリポジトリの `43106a70d7057dc6c506bac96b0888ecdaddb4b8`（3.0.2.4-Beta）も要ります |
| `gensoband` | <https://github.com/asamayim/gensoband> | `fc4418078a62db9fae2915c8b6ab24a063609093` |
| `silq` | <https://github.com/sil-quirk/sil-q> | `d0355bf97e873ba10e6b364aa127bc5ebbbd05f1` |
| `frox` | <https://github.com/Gliktch/froxcomposband> | `c7641efca8caf6069aa9a71a4b5c8547a64013be` |
| `angband` | <https://github.com/angband/angband>（4.2.6） | `f3082213b73f3e463e3d0d60bff4b00462beae6e` |
| `umoria` | <https://github.com/dungeons-of-moria/umoria>（5.7.15） | `3bf8abc4f76812810e59d738492c7b61fb8f1bb6` |
| `rogue2` | <https://github.com/suzukiiichiro/Rogue2.Official>（RogueClone2） | `895a468ab632c7ba05d88b706ac1db478dffbbe8` |
| `rogue36` | usotopr/newrogue3.6（Rogue 3.6.4） | `570c8825d867ee4da1f03f9fd5309bb9c454a26e` |
| `rogue54` | Davidslv/rogue（Rogue 5.4.4） | `f4653c2a2ee6981a73abe9dfda055134285e1e79` |

用意したら、次の 2 つを実行します。

```powershell
python tools/core_sources/prepare.py
python tools/core_sources/prepare_original_data.py
```

- `prepare.py` は、固定版の原作とこのリポジトリのレシピ（`tools/core_sources/recipes/`）から、改変後のソースを
  `../roguelike-cores/build-sources/` に再構成し、全ファイルを SHA-256 で検証します。
  詳しくは [原作ソースの準備](tools/core_sources/README.md) を参照してください。
- `prepare_original_data.py` は、組み立てと配布物が読む**原作のゲームデータ**を固定版から写します。
  Angband の `lib/{gamedata,customize,screens,help}`（`angband/lib/` の下）と、RogueClone2 の日本語のメッセージ表
  （原作の `mesg_J` を `rogue2/mesg` へ）です。これらはこのリポジトリに含めておらず、`.gitignore` で追跡の対象から外してあります。
  写していない場合、`tools/rogue2/build_windows.py` は自動で写し、`tools/angband/build_windows.py` も自動で原作から写します。
  配布物を作る `tools/package/` と `tools/build_android_hd2d_assets.py` は、写していなければ止まります。

#### Windows

Visual Studio の C++ 開発環境と [vcpkg](https://vcpkg.io/) が必要です。プロジェクトが指定する MSVC ツールセットと Windows SDK を用意してください。

```powershell
vcpkg install sdl2:x64-windows sdl2-image:x64-windows openxr-loader:x64-windows
vcpkg install sdl2-ttf:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
vcpkg install openal-soft:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
```

`sdl2-ttf` と `openal-soft` は、`tools/vcpkg/triplets` にあるトリプレット（vcpkg のビルド設定）を
指定して入れてください。標準の設定のままだと、`sdl2-ttf` は x64 で文字が欠けることがあり
（MSVC の最適化の問題です）、`openal-soft` はビルドが通りません。

```powershell
msbuild .\VisualStudio\Hengband.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

リポジトリ直下に `HengbandHd2d.exe` と、変愚蛮怒・短愚蛮怒・幻想蛮怒・Sil-Q・FroxComposband の exe ができます。

`/p:Platform=x64` は画面側だけの指定で、コアは Win32 でビルドされます。
別プロセスなので混在しても問題ありません。画面側を x64 にしているのは VR 対応のためです。

UI だけなら `VisualStudio/HengbandHd2d/HengbandHd2d.vcxproj` を指定してビルドでき、原作の準備は不要です。

Angband・Umoria・Rogue 系の 5 本（`AngbandCore.exe` `UmoriaCore.exe` `Rogue2Core.exe` `Rogue36Core.exe` `Rogue54Core.exe`）は
Visual Studio の解には入っていません。`tools/angband/build_windows.py` `tools/umoria/build_windows.py`
`tools/rogue_legacy/build_windows.py` `tools/rogue2/build_windows.py` で組みます。
同梱コンパイラー（LLVM-MinGW。版と SHA-256 は `tools/core_import/toolchains.json`）を `core-import/toolchain/` に用意してください。

#### 動かすために要る素材

画像や音などの素材はこのリポジトリに含めていません。現在の配布物にタイル画像は入っておらず、
店・所持品・品物選択のアイコンはボクセル板で表示します。

自分でビルドしたものを動かすには、Releases の zip を展開し、その中の `HengbandHd2d.exe` と `cores\` の中のコアの exe を
自分でビルドしたものに置き換えるのが簡単です（`assets\` `lib\` と、ゲームごとのフォルダー、`cores\angband\` などのデータはそのまま使います）。

ボクセルモデル（`assets/voxel/`）は `tools/voxel/` の Python スクリプトで生成できます。

配布物と同じ構成を自分で作るには `tools/package/Build-Hd2dPackage.ps1`（Windows）、
`tools/package/build_linux_package.py`（Linux）、`tools/package/Build-Hd2dApk.ps1`（Android）を使います。
先に上の「原作の用意」を済ませてください。

#### UI から原作 ZIP を取り込む

ソースには、コア選択画面の「コアをインポート」から対応版の原作 ZIP を選び、ビルド・登録する機能があります
（現在の配布物には含めていません）。対応版・手順・制約は [インポート仕様](tools/core_import/README.md) を参照してください。

#### Linux

`platform/linux/CMakeLists.txt` で組みます。Ubuntu 26.04 x86_64 で確認しています。
先に「原作の用意」の 2 つのコマンドを実行してください。

#### Android / Meta Quest

`tools/fetch_android_deps.ps1` で SDL2 を取得してから、`android/` で Gradle を実行します
（`android/hd2d` が Android、`android/quest` が Quest です）。
詳しい手順は `android/hd2d/src/main/cpp/CMakeLists.txt` の先頭に書いてあります。

幻想蛮怒のソース（と幻想蛮怒・Sil-Q のアダプタ）は Shift_JIS の文字列を前提にしているため、
ビルド時に `tools/transcode_cp932_src.py` で文字列リテラルをバイト列に書き換えたコピー
（外部の作業用ソース内の `android-sjis/`）を作ってビルドします。CMake が自動で実行します。

### 動作確認用のスクリプト

`tools/hd2d_verify/` に、変更の前後で動作が変わっていないかを調べるスクリプトが 3 つあります。
Python の標準ライブラリだけで動きます。

```
python tools/hd2d_verify/golden.py --check       検査モード 16 件の出力を比較します
python tools/hd2d_verify/replay.py --check       記録した通信を再生し、画面が一致するか確かめます
python tools/hd2d_verify/playthrough.py --check  実際にコアを起動して、通しの動作を確かめます
```

先に `--record` で自分の環境の基準を作ってください。画面のハッシュを比較するので、GPU が違うと結果も変わります。

`replay.py` は、記録しておいたコアとの通信を固定の時刻で再生するので、同じ操作なら画面が完全に一致します。
詳しくは各スクリプトの先頭のコメントをご覧ください。

### ディレクトリ構成

| ディレクトリ | 内容 |
| --- | --- |
| `hd2d/` | 3D 表示の画面 |
| `presentation/` | コア側の描画データの作成とプロトコル |
| `platform/` | Windows / Android / Linux の起動処理 |
| `tools/core_sources/` | 外部原作の版固定、必要なパッチ、ソースとデータの準備 |
| `gensoband/` `silq/` `frox/` `tangband/` `angband/` `umoria/` `rogue2/` `rogue36/` `rogue54/` `rogue_legacy/` | コア別アダプター・翻訳・ゲームデータ。原作の src と原作のゲームデータは外部で管理 |
| `tools/targeting/` | 原作ソースへ狙いの表示などの呼び出しを足す書き換え |
| `tools/voxel/` | ボクセルモデルの生成 |
| `tools/hd2d_verify/` | 動作確認用のスクリプト |

設計資料は含めていません。ソースのコメントに日本語で説明を書いています。

`presentation/` は Windows と Android の両方で使うので、変更したときは両方で確認してください。

### 元の Hengband のドキュメント

元の Hengband のドキュメントは、使用許諾の条件に従ってそのまま残しています。

| ファイル | 内容 |
| --- | --- |
| `readme-hengband.md` | Hengband の説明（日本語） |
| `readme-eng.md` | Hengband の説明（英語） |
| `readme_angband` | Angband の説明 |
| `lib/help/jlicense.txt` | 変愚蛮怒の著作権と使用許諾の全文 |
| `silq/LICENSE.md` | Sil-Q のライセンス |
| `angband/LICENSE.md` | Angband のライセンス |

### ライセンス

系統ごとに違います。[LICENSE](LICENSE) と [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) をご覧ください。

- 変愚蛮怒・短愚蛮怒・幻想蛮怒・FroxComposband、画面、共通コード、アダプターは Moria/Angband 使用許諾です。非営利での配布は可能で、著作権表記と使用許諾文を含める必要があります
- Sil-Q は GPL v2 と Angband 使用許諾のデュアルライセンスです
- Angband は GPL v2 で配布します（原作は GPL v2 と Angband 使用許諾の二重許諾で、GPL v2 を選んでいます）
- Umoria は GPL v3 です
- Rogue 3.6 / Rogue 5.4 は BSD 3 条項です
- RogueClone2 は原作者の条件（営利目的で販売しない・資料を添付する・制限を加えない）に従います
- GPL のコア（Angband・Umoria）の対応するソースの入手のしかたは [LICENSE](LICENSE) の §8 に書いてあります。
  上流の固定版（上の「原作の用意」の表）と、このリポジトリのレシピ・アダプターから、`tools/core_sources/prepare.py` が再構成します
- 許諾の違うコアが同じ配布物に入ることについて、事実を [LICENSE](LICENSE) の §7 に書いてあります
- タイル画像は画像生成 AI（Stable Diffusion）で作ったものです（配布していません）。利用にあたっては各自でご確認ください

### 謝辞

変愚蛮怒と、その元になった ZAngband・Angband・Umoria・Moria、
そして幻想蛮怒・Sil-Q・FroxComposband・Rogue・RogueClone2 の作者と貢献者の皆さんに感謝します。

---

## English

A fork of Hengband with a 3D display. The screen side was written from scratch,
and ten games can be played on it: Hengband, Tangband, Gensoband, Sil-Q, FroxComposband,
Angband, Umoria, Rogue 3.6, Rogue 5.4 and RogueClone2.

Author: Higesho

This is an unofficial fork and is not related to the official
[hengband/hengband](https://github.com/hengband/hengband) repository. Please do not report bugs there.

### Download

Prebuilt packages are on the Releases page. They include the voxel models and sounds (the tile images are not included).

- Windows: [HengbandHD-win.zip](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHD-win.zip)
- Android: [HengbandHD-android.apk](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHD-android.apk)
- Linux (x86_64): [HengbandHD-linux-x86_64.tar.gz](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHD-linux-x86_64.tar.gz)

Project page: <https://higesho.github.io/>

### How to play

- **Windows**: unzip the package and run `HengbandHd2d.exe`. You will be asked which game to play.
  It needs 64-bit Windows, a graphics device that supports OpenGL 4.6, and the Microsoft Visual C++ Redistributable (x64).
  The game core executables are in `cores\`. They are started by the screen side, so they do not run on their own.
- **Linux**: unpack the tar.gz and run `./HengbandHd2d.sh`. It needs OpenGL 4.5. See `README-linux.txt` inside the package.
- **Android**: install the apk (arm64-v8a).

The current packages contain the ten prebuilt cores. The feature that imports and compiles upstream source ZIPs
(`tools/core_import/`) is in the source but is not included in the packages.

### The ten games

| Game | Description |
| --- | --- |
| Hengband | Hengband 3.0.2.5-Beta |
| Tangband | A short version of Hengband (the same source built with different settings) |
| Gensoband | A Touhou Project themed fork of Hengband. Towns and dungeons were redesigned, and an English version was added |
| Sil-Q | A successor of Sil, translated into Japanese |
| FroxComposband | A fork of Composband, translated into Japanese |
| Angband | Angband 4.2.6, with Japanese display support |
| Umoria | Umoria 5.7 series, translated into Japanese |
| Rogue 3.6 / Rogue 5.4 | The classic Rogue, also with English display |
| RogueClone2 | A Rogue clone (RogueClone2), based on its Japanese edition |

Each game is a separate executable and talks to the screen through a small protocol.
The screen side has no game-specific knowledge; each core reports its name and features, and the screen adapts.

### Changes from the original Hengband

As required by the Moria/Angband license, this section lists the changes from Hengband 3.0.2.3-Beta.

Added:

- A 3D display (`hd2d/`) using OpenGL 4.6, with shadows, depth of field, bloom, day/night and weather
- The screen and the core now run as separate processes and talk over a pipe (`presentation/` `platform/`)
- Nine more games (`gensoband/` `silq/` `frox/` `tangband/` `angband/` `umoria/` `rogue2/` `rogue36/` `rogue54/` `rogue_legacy/`)
- Ports to Android, Meta Quest and VR (OpenXR)
- Four view modes, a TRON-style look, and a real-time mode
- An English version of Gensoband, and Japanese versions of Sil-Q and FroxComposband

Changed:

- All outbound network access was removed. The original Hengband sends a crash report to the developers' server;
  this fork disables it (`DISABLE_NET`). Gensoband's world score upload is disabled too.
  See §1.1 of [LICENSE](LICENSE)
- The original 2D build (`Hengband.exe`) and the libcurl it used were removed
- The original CI configuration was removed

Upstream sources are now maintained outside this repository. The UI can validate, compile and register supported source ZIPs.

### Building

#### Original sources

The upstream sources and the upstream game data are not in this repository. Prepare the pinned versions outside it.
Clone the repositories in the table below into `../roguelike-cores/upstream/<name>/` and check out the pinned commits
(a detached HEAD is fine; use `--upstream` or the environment variable `HENGBAND_UPSTREAM_ROOT` for another place).
The commits are also in `tools/core_sources/manifest.json`.

| Name | Source | Pinned commit |
| --- | --- | --- |
| `hengband` | <https://github.com/hengband/hengband> (Hengband 3.0.2.5-Beta) | `18b10d6ea5fd234c119053e0a209c90aede21fa4`. Tangband also needs `43106a70d7057dc6c506bac96b0888ecdaddb4b8` (3.0.2.4-Beta) of the same repository |
| `gensoband` | <https://github.com/asamayim/gensoband> | `fc4418078a62db9fae2915c8b6ab24a063609093` |
| `silq` | <https://github.com/sil-quirk/sil-q> | `d0355bf97e873ba10e6b364aa127bc5ebbbd05f1` |
| `frox` | <https://github.com/Gliktch/froxcomposband> | `c7641efca8caf6069aa9a71a4b5c8547a64013be` |
| `angband` | <https://github.com/angband/angband> (4.2.6) | `f3082213b73f3e463e3d0d60bff4b00462beae6e` |
| `umoria` | <https://github.com/dungeons-of-moria/umoria> (5.7.15) | `3bf8abc4f76812810e59d738492c7b61fb8f1bb6` |
| `rogue2` | <https://github.com/suzukiiichiro/Rogue2.Official> (RogueClone2) | `895a468ab632c7ba05d88b706ac1db478dffbbe8` |
| `rogue36` | usotopr/newrogue3.6 (Rogue 3.6.4) | `570c8825d867ee4da1f03f9fd5309bb9c454a26e` |
| `rogue54` | Davidslv/rogue (Rogue 5.4.4) | `f4653c2a2ee6981a73abe9dfda055134285e1e79` |

Then run these two commands:

```powershell
python tools/core_sources/prepare.py
python tools/core_sources/prepare_original_data.py
```

- `prepare.py` reconstructs the modified sources under `../roguelike-cores/build-sources/` from the pinned upstream
  and this repository's recipes (`tools/core_sources/recipes/`), and verifies every file with SHA-256.
  See [source preparation](tools/core_sources/README.md).
- `prepare_original_data.py` copies the **upstream game data** that the builds and packages read, from the pinned commits:
  Angband's `lib/{gamedata,customize,screens,help}` (into `angband/lib/`) and RogueClone2's Japanese message table
  (upstream `mesg_J` to `rogue2/mesg`). They are not in this repository and are excluded by `.gitignore`.
  If they are missing, `tools/rogue2/build_windows.py` copies them automatically, and `tools/angband/build_windows.py` copies the Angband data from the upstream working tree.
  The packaging scripts (`tools/package/` and `tools/build_android_hd2d_assets.py`) stop when they are missing.

#### Windows

You need Visual Studio with C++ support, the MSVC toolset and Windows SDK selected by the projects, and [vcpkg](https://vcpkg.io/).

```powershell
vcpkg install sdl2:x64-windows sdl2-image:x64-windows openxr-loader:x64-windows
vcpkg install sdl2-ttf:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
vcpkg install openal-soft:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
```

Install `sdl2-ttf` and `openal-soft` with the triplets in `tools/vcpkg/triplets`.
With the default triplets, `sdl2-ttf` drops glyphs on x64 (an MSVC optimizer issue) and `openal-soft` fails to build.

```powershell
msbuild .\VisualStudio\Hengband.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

`HengbandHd2d.exe` and the executables of Hengband, Tangband, Gensoband, Sil-Q and FroxComposband are placed in the repository root.

`/p:Platform=x64` applies to the screen side only; the cores are built as Win32.
They are separate processes, so mixing them is fine. The screen is x64 for VR support.

The standalone `VisualStudio/HengbandHd2d/HengbandHd2d.vcxproj` UI build does not require any core sources.

The Angband, Umoria and Rogue cores (`AngbandCore.exe`, `UmoriaCore.exe`, `Rogue2Core.exe`, `Rogue36Core.exe`, `Rogue54Core.exe`)
are not in the Visual Studio solution. Build them with `tools/angband/build_windows.py`, `tools/umoria/build_windows.py`,
`tools/rogue_legacy/build_windows.py` and `tools/rogue2/build_windows.py`. They need the bundled compiler
(LLVM-MinGW; version and SHA-256 are in `tools/core_import/toolchains.json`) in `core-import/toolchain/`.

#### Assets needed to run

Images and sounds are not in this repository. The current packages do not include tile images;
shop, inventory and item-selection icons are drawn with the voxel board assets.

The easy way to run your own build is to unzip the release and replace `HengbandHd2d.exe` and the core executables in `cores\`
with your own build (keep `assets\`, `lib\`, the per-game folders and the data in `cores\angband\` as they are).

The voxel models (`assets/voxel/`) can be generated with the Python scripts in `tools/voxel/`.

To make a package with the same layout as the release, use `tools/package/Build-Hd2dPackage.ps1` (Windows),
`tools/package/build_linux_package.py` (Linux) or `tools/package/Build-Hd2dApk.ps1` (Android).
Do the "Original sources" step first.

#### Import source ZIPs from the UI

The source has a feature that builds and registers a supported original source ZIP through **Import core** on the core selection screen
(it is not included in the current packages). See [source import](tools/core_import/README.md) for supported versions and scope.

#### Linux

Build with `platform/linux/CMakeLists.txt`. It has been checked on Ubuntu 26.04 x86_64.
Run the two commands of "Original sources" first.

#### Android / Meta Quest

Run `tools/fetch_android_deps.ps1` to fetch SDL2, then run Gradle in `android/`
(`android/hd2d` for Android, `android/quest` for Quest).
The details are at the top of `android/hd2d/src/main/cpp/CMakeLists.txt`.

The Gensoband source (and the Gensoband and Sil-Q adapters) expect Shift_JIS strings, so at build time
`tools/transcode_cp932_src.py` writes a copy with the string literals rewritten as byte escapes
(the external prepared source directory, `android-sjis/`), and that copy is built. CMake runs it automatically.

### Verification scripts

`tools/hd2d_verify/` contains three scripts that check whether behaviour changed between builds.
They only need the Python standard library.

```
python tools/hd2d_verify/golden.py --check       Compare the output of 16 check modes
python tools/hd2d_verify/replay.py --check       Replay a recorded session and compare the screen
python tools/hd2d_verify/playthrough.py --check  Start a real core and check the whole flow
```

Run `--record` first to create a baseline on your own machine. The scripts compare screen hashes, so results differ between GPUs.

`replay.py` replays a recorded conversation with the core at a fixed clock, so the same input produces an identical screen.
See the comment at the top of each script for details.

### Directory layout

| Directory | Contents |
| --- | --- |
| `hd2d/` | The 3D screen |
| `presentation/` | Core-side rendering data and the protocol |
| `platform/` | Entry points for Windows, Android and Linux |
| `tools/core_sources/` | Pinned external sources, patches, and preparation of the sources and data |
| `gensoband/` `silq/` `frox/` `tangband/` `angband/` `umoria/` `rogue2/` `rogue36/` `rogue54/` `rogue_legacy/` | Adapters, translations and game data; the upstream src and upstream game data are external |
| `tools/targeting/` | Rewrites that add calls (target display and so on) to the upstream sources |
| `tools/voxel/` | Voxel model generation |
| `tools/hd2d_verify/` | Verification scripts |

Design documents are not included. The source comments (in Japanese) explain the code.

`presentation/` is shared by Windows and Android, so please check both when you change it.

### Original Hengband documents

The documents of the original Hengband are kept as they were, as the license requires.

| File | Contents |
| --- | --- |
| `readme-hengband.md` | Hengband readme (Japanese) |
| `readme-eng.md` | Hengband readme (English) |
| `readme_angband` | Angband readme |
| `lib/help/jlicense.txt` | Full text of the Hengband copyright and license |
| `silq/LICENSE.md` | Sil-Q license |
| `angband/LICENSE.md` | Angband license |

### License

Different parts are under different licenses. See [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

- Hengband, Tangband, Gensoband and FroxComposband, the screen, the shared code and the adapters are under the Moria/Angband license. Non-commercial distribution is allowed,
  and all copies must include the copyright notice and the license text
- Sil-Q is dual-licensed under GPL v2 and the Angband license
- Angband is distributed under GPL v2 (the upstream dual license is GPL v2 or the Angband license; GPL v2 is chosen)
- Umoria is under GPL v3
- Rogue 3.6 and Rogue 5.4 are under the BSD 3-clause license
- RogueClone2 follows its author's conditions (no sale for profit, attach the documents, no further restrictions)
- How to get the corresponding source of the GPL cores (Angband and Umoria) is in §8 of [LICENSE](LICENSE):
  the pinned upstream versions (the table in "Original sources") plus this repository's recipes and adapters,
  reconstructed by `tools/core_sources/prepare.py`
- The facts about cores under different licenses in one package are in §7 of [LICENSE](LICENSE)
- The tile images were generated with an image generation AI (Stable Diffusion) and are not distributed. Please check the terms for your own use

### Acknowledgements

Thanks to the authors and contributors of Hengband and its ancestors ZAngband, Angband, Umoria and Moria,
and of Gensoband, Sil-Q, FroxComposband, Rogue and RogueClone2.

### Source reconstruction / ソース再構成

原作本文入りのパッチを、ハッシュ確認済みの入力への範囲参照・文字変換・追加内容を持つschema 2へ変更しました。開発用とUIのインポートで同じ復元形式を使います。旧キットは再生成が必要です。

Source reconstruction now uses schema 2: verified input ranges, encoding transformations and explicit additions. Regenerate legacy import kits for the updated UI. Tangband requires both the pinned Tangband and Hengband source ZIPs. Existing registered cores and saves are retained. See [import instructions](tools/core_import/README.md).
