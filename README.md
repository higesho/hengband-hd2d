# Hengband HD2D

[日本語](#日本語) | [English](#english)

---

## 日本語

変愚蛮怒（Hengband）を 3D 表示にした派生版です。画面部分を新しく作り、
変愚蛮怒・短愚蛮怒・幻想蛮怒・Sil-Q・FroxComposband の 5 つを同じ画面で遊べるようにしました。

作者: Higesho

非公式の派生版です。Hengband の公式リポジトリ [hengband/hengband](https://github.com/hengband/hengband)
とは関係がありませんので、不具合があってもそちらには報告しないでください。

### ダウンロード

ビルド済みのものを Releases に置いています。画像や音などの素材も入っています。

- Windows: [HengbandHd2d-win.zip](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHd2d-win.zip)
- Android: [HengbandHd2d-android.apk](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHd2d-android.apk)

紹介ページ: <https://higesho.github.io/>

### 遊び方

zip を展開して `HengbandHd2d.exe` を起動してください。最初に、どのゲームで遊ぶかを選ぶ画面が出ます。

`HengbandCore.exe` などのゲーム本体（コア）は画面側から起動される作りになっているため、単体では起動しません。

### 5 つのゲーム

| ゲーム | 説明 |
| --- | --- |
| 変愚蛮怒 | Hengband 3.0.2.3-Beta です |
| 短愚蛮怒 | 変愚蛮怒の短編版です（同じソースを別の設定でビルドしています） |
| 幻想蛮怒 | 東方 Project をモチーフにした変愚蛮怒の派生版です。町とダンジョンを作り込み、英語版も用意しました |
| Sil-Q | Sil の後継です。日本語化しました |
| FroxComposband | Composband の派生版です。日本語化しました |

それぞれ独立した実行ファイルで、画面とは独自のプロトコルで通信します。
画面側はゲームごとの固有の知識を持たず、コアが自分の名前と機能を伝え、画面がそれに合わせます。

### 元の Hengband からの変更点

Moria/Angband 使用許諾の条件に従い、Hengband 3.0.2.3-Beta からの変更点を書きます。

追加したもの:

- 3D 表示（`hd2d/`）。OpenGL 4.6 で、影・被写界深度・ブルーム・昼夜と天候があります
- 画面とコアを別プロセスに分け、パイプで通信するようにしました（`presentation/` `platform/`）
- 4 つのゲームを追加しました（`gensoband/` `silq/` `frox/` `tangband/`）
- Android、Meta Quest、VR（OpenXR）に移植しました
- 視点モード 4 種類、TRON 風の表示、リアルタイムモード
- 幻想蛮怒の英語版、Sil-Q と FroxComposband の日本語版

変更したもの:

- 外部への通信をなくしました。元の Hengband にはクラッシュ時に開発チームのサーバーへ報告を送る機能が
  ありますが、この派生版では無効にしています（`DISABLE_NET`）。幻想蛮怒のワールドスコア送信も同様です。
  詳しくは [LICENSE](LICENSE) の §1.1 をご覧ください
- 元の 2D 版（`Hengband.exe`）のビルドと、それだけが使っていた libcurl を外しました
- 元の CI 設定を外しました

元の Hengband から 719 コミット（2026-07-25 〜 2026-09-07）の変更があります。

### ビルド

#### Windows

Visual Studio 2022 以降と [vcpkg](https://vcpkg.io/) が必要です。

```powershell
vcpkg install sdl2:x64-windows sdl2-image:x64-windows openxr-loader:x64-windows
vcpkg install sdl2-ttf:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
vcpkg install openal-soft:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
vcpkg install curl:x86-windows
```

`sdl2-ttf` と `openal-soft` は、`tools/vcpkg/triplets` にあるトリプレット（vcpkg のビルド設定）を
指定して入れてください。標準の設定のままだと、`sdl2-ttf` は x64 で文字が欠けることがあり
（MSVC の最適化の問題です）、`openal-soft` はビルドが通りません。
`curl` はコア側が使うもので、コアは Win32 でビルドされるため x86 で入れます。

```powershell
msbuild .\VisualStudio\Hengband.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

リポジトリ直下に `HengbandHd2d.exe` と各コアの exe ができます。

`/p:Platform=x64` は画面側だけの指定で、コアは Win32 でビルドされます。
別プロセスなので混在しても問題ありません。画面側を x64 にしているのは VR 対応のためです。

#### 素材について

画像や音などの素材はこのリポジトリに含めていません（タイル画像だけで約 2.2GB あるためです）。
自分でビルドしたものを動かすには、Releases の zip から `assets/` `lib/` `tilework/` を取り出して、
exe と同じ場所に置いてください。

ボクセルモデル（`assets/voxel/`）は `tools/voxel/` の Python スクリプトで生成できます。

#### Android / Meta Quest

`tools/fetch_android_deps.ps1` で SDL2 を取得してから、`android/` で Gradle を実行します
（`android/hd2d` が Android、`android/quest` が Quest です）。
詳しい手順は `android/hd2d/src/main/cpp/CMakeLists.txt` の先頭に書いてあります。

幻想蛮怒のソース（と幻想蛮怒・Sil-Q のアダプタ）は Shift_JIS の文字列を前提にしているため、
ビルド時に `tools/transcode_cp932_src.py` で文字列リテラルをバイト列に書き換えたコピー
（`android/build-src-sjis/`）を作ってビルドします。CMake が自動で実行します。

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
| `platform/` | Windows / Android の起動処理 |
| `src/` | 変愚蛮怒と短愚蛮怒のコア（元の Hengband のもの） |
| `gensoband/` `silq/` `frox/` `tangband/` | 各コア |
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

### ライセンス

系統ごとに違います。[LICENSE](LICENSE) をご覧ください。

- 大部分は Moria/Angband 使用許諾です。非営利での配布は可能で、著作権表記と使用許諾文を含める必要があります
- Sil-Q は GPL v2 と Angband 使用許諾のデュアルライセンスです
- タイル画像は画像生成 AI（Stable Diffusion）で作ったものです。利用にあたっては各自でご確認ください

### 謝辞

変愚蛮怒と、その元になった ZAngband・Angband・Umoria・Moria、
そして幻想蛮怒・Sil-Q・FroxComposband の作者と貢献者の皆さんに感謝します。

---

## English

A fork of Hengband with a 3D display. The screen side was written from scratch,
and five games can be played on it: Hengband, Tangband, Gensoband, Sil-Q, and FroxComposband.

Author: Higesho

This is an unofficial fork and is not related to the official
[hengband/hengband](https://github.com/hengband/hengband) repository. Please do not report bugs there.

### Download

Prebuilt packages are on the Releases page. They include the images and sounds.

- Windows: [HengbandHd2d-win.zip](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHd2d-win.zip)
- Android: [HengbandHd2d-android.apk](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHd2d-android.apk)

Project page: <https://higesho.github.io/>

### How to play

Unzip the package and run `HengbandHd2d.exe`. You will be asked which game to play.

The game cores such as `HengbandCore.exe` are started by the screen side, so they do not run on their own.

### The five games

| Game | Description |
| --- | --- |
| Hengband | Hengband 3.0.2.3-Beta |
| Tangband | A short version of Hengband (the same source built with different settings) |
| Gensoband | A Touhou Project themed fork of Hengband. Towns and dungeons were redesigned, and an English version was added |
| Sil-Q | A successor of Sil, translated into Japanese |
| FroxComposband | A fork of Composband, translated into Japanese |

Each game is a separate executable and talks to the screen through a small protocol.
The screen side has no game-specific knowledge; each core reports its name and features, and the screen adapts.

### Changes from the original Hengband

As required by the Moria/Angband license, this section lists the changes from Hengband 3.0.2.3-Beta.

Added:

- A 3D display (`hd2d/`) using OpenGL 4.6, with shadows, depth of field, bloom, day/night and weather
- The screen and the core now run as separate processes and talk over a pipe (`presentation/` `platform/`)
- Four more games (`gensoband/` `silq/` `frox/` `tangband/`)
- Ports to Android, Meta Quest and VR (OpenXR)
- Four view modes, a TRON-style look, and a real-time mode
- An English version of Gensoband, and Japanese versions of Sil-Q and FroxComposband

Changed:

- All outbound network access was removed. The original Hengband sends a crash report to the developers' server;
  this fork disables it (`DISABLE_NET`). Gensoband's world score upload is disabled too.
  See §1.1 of [LICENSE](LICENSE)
- The original 2D build (`Hengband.exe`) and the libcurl it used were removed
- The original CI configuration was removed

There are 719 commits on top of the original (2026-07-25 to 2026-09-07).

### Building

#### Windows

You need Visual Studio 2022 or later and [vcpkg](https://vcpkg.io/).

```powershell
vcpkg install sdl2:x64-windows sdl2-image:x64-windows openxr-loader:x64-windows
vcpkg install sdl2-ttf:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
vcpkg install openal-soft:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
vcpkg install curl:x86-windows
```

Install `sdl2-ttf` and `openal-soft` with the triplets in `tools/vcpkg/triplets`.
With the default triplets, `sdl2-ttf` drops glyphs on x64 (an MSVC optimizer issue) and `openal-soft` fails to build.
`curl` is used by the cores, which are built as Win32, so it is installed for x86.

```powershell
msbuild .\VisualStudio\Hengband.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

`HengbandHd2d.exe` and the core executables are placed in the repository root.

`/p:Platform=x64` applies to the screen side only; the cores are built as Win32.
They are separate processes, so mixing them is fine. The screen is x64 for VR support.

#### Assets

Images and sounds are not in this repository (the tile images alone are about 2.2 GB).
To run your own build, take `assets/`, `lib/` and `tilework/` from the release zip and put them next to the executables.

The voxel models (`assets/voxel/`) can be generated with the Python scripts in `tools/voxel/`.

#### Android / Meta Quest

Run `tools/fetch_android_deps.ps1` to fetch SDL2, then run Gradle in `android/`
(`android/hd2d` for Android, `android/quest` for Quest).
The details are at the top of `android/hd2d/src/main/cpp/CMakeLists.txt`.

The Gensoband source (and the Gensoband and Sil-Q adapters) expect Shift_JIS strings, so at build time
`tools/transcode_cp932_src.py` writes a copy with the string literals rewritten as byte escapes
(`android/build-src-sjis/`), and that copy is built. CMake runs it automatically.

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
| `platform/` | Entry points for Windows and Android |
| `src/` | The Hengband and Tangband cores (from the original Hengband) |
| `gensoband/` `silq/` `frox/` `tangband/` | The other cores |
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

### License

Different parts are under different licenses. See [LICENSE](LICENSE).

- Most of the code is under the Moria/Angband license. Non-commercial distribution is allowed,
  and all copies must include the copyright notice and the license text
- Sil-Q is dual-licensed under GPL v2 and the Angband license
- The tile images were generated with an image generation AI (Stable Diffusion). Please check the terms for your own use

### Acknowledgements

Thanks to the authors and contributors of Hengband and its ancestors ZAngband, Angband, Umoria and Moria,
and of Gensoband, Sil-Q and FroxComposband.
