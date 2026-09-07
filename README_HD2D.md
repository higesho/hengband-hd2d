# Hengband HD2D

**変愚蛮怒（Hengband）をボクセルで立体的に描き直した派生版**である。
画面を新しく作り、**5 つのゲームコアを同じ画面で遊べる**ようにした。

作者: Higesho

> **これは非公式の派生版であり、本家 [hengband/hengband](https://github.com/hengband/hengband)
> とは無関係である。** 不具合の報告を本家へ持ち込まないこと。

---

## 何が変わったのか（上流からの変更）

Moria/Angband 使用許諾に基づく変愚蛮怒の追加条項 (2)「変更を加えた事実および変更内容に
ついて明確に記載する事」に従い、上流 **Hengband 3.0.2.3-Beta** からの変更を記す。

### 足したもの

| | 内容 |
| --- | --- |
| **ボクセルの画面**（`hd2d/`） | OpenGL 4.6 で世界をボクセルの立体として描く。影・被写界深度・ブルーム・面の汚し・昼夜・天候 |
| **画面とコアの分離**（`presentation/` `platform/`） | 画面が**別プロセスの**コアを起こし、パイプで独自のプロトコル（v1）を喋る |
| **コアを 4 本足した**（`gensoband/` `silq/` `frox/` `tangband/`） | 変愚蛮怒に加えて、短愚蛮怒・幻想蛮怒・Sil-Q・FroxComposband を同じ画面で遊べる |
| **移植** | Android・Meta Quest（ネイティブ）・VR（OpenXR） |
| **表示モードと画調** | 見下ろし／一人称ほか 4 つの視点、TRON 画調、リアルタイムモード |
| **言語** | 幻想蛮怒の英語化、Sil-Q と FroxComposband の日本語化 |

### 変えたもの

- **外部への通信を切った。** 上流のコアは異常終了したとき本家の Webhook へ報告を送る
  仕組みを持つが、派生版から本家へ送るのは筋が通らないので `DISABLE_NET` で切ってある。
  幻想蛮怒のワールドスコア送信も同様に切ってある（詳細は [LICENSE](LICENSE) §1.1）
- **上流の 2D 版（`Hengband.exe`）のビルドは外してある。** HD2D の組み立てには要らず、
  同梱していた静的な libcurl（上流版だけが使う）も外した
- 上流の CI 設定は、このツリーの構成に合わないので取り除いてある

規模: 上流を基点に 719 コミット（2026-07-25 〜 2026-09-07）。

---

## 組み立て方

### Windows

必要なもの:

- Visual Studio 2022 以降の MSBuild
- [vcpkg](https://vcpkg.io/)

```powershell
# 依存を入れる。sdl2-ttf と openal-soft は同梱の重ね三つ組を通すこと
vcpkg install sdl2:x64-windows sdl2-image:x64-windows openxr-loader:x64-windows
vcpkg install sdl2-ttf:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
vcpkg install openal-soft:x64-windows --overlay-triplets=.\tools\vcpkg\triplets
# コアは Win32 で組まれるので、コア側の依存だけ x86 で入れる
vcpkg install curl:x86-windows
```

> **重ね三つ組は飾りではない。** 素の `/O2` で組んだ `sdl2-ttf` は **x64 で字が欠ける**
> （MSVC の不具合）。`openal-soft` はいまの MSVC では素だと通らない。

```powershell
msbuild .\VisualStudio\Hengband.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

成果物はリポジトリ直下の `HengbandHd2d.exe`（画面）と `HengbandCore.exe`（コア）。

> `/p:Platform=x64` は「**画面だけ x64**」の意味である。VR（OpenXR）のために画面を
> x64 にした。コアはこの構成でも Win32 のまま組まれる——別プロセスでやり取りは
> バイト列なので混ざってよい。

### 遊ぶ

```
HengbandHd2d.exe
```

**`HengbandCore.exe` を直に起こしても動かない。** 画面がコアを子として起こす作りである。

### 組まずに遊ぶ

**組み上がった配布物のほうが早い。** 実行体・タイル画像・ボクセルの模型・音の素材を
まとめたものを、作者のページの Releases に置いてある。

- Windows: [HengbandHd2d-win.zip](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHd2d-win.zip)
- Android: [HengbandHd2d-android.apk](https://github.com/higesho/hengband-hd2d/releases/latest/download/HengbandHd2d-android.apk)

案内のページ: <https://higesho.github.io/>

### 自分で組んだときに要るもの

**このリポジトリには素材を入れていない**（タイル画像だけで 2.2GB あるため）。
自分で組んだ実行体で遊ぶには、上の `HengbandHd2d-win.zip` から
`assets/` `lib/` `tilework/` などを取り出して、実行体と同じ場所に置くこと。

ボクセルの模型（`assets/voxel/`）だけは `tools/voxel/` で作り直せる——
外部の生成 AI は要らず、Python が手続きで組み立てる。

### Android / Meta Quest

先に `tools/fetch_android_deps.ps1` で SDL2 群を展開してから、
`android/` の Gradle で組む（`android/hd2d` が Android、`android/quest` が Quest）。
組み立ての決まりは `android/hd2d/src/main/cpp/CMakeLists.txt` の冒頭に厚く書いてある。

**旧 C のコア 2 本（幻想蛮怒・FroxComposband）は文字コードの変換ツリーを通して組む**
——`tools/transcode_cp932_src.py` が CP932 と EUC のソースを UTF-8 へ写す。
CMake が自動で呼ぶので、手で走らせる必要は無い。

---

## 5 つのコア

起動すると、どのコアで遊ぶかを選ぶ画面が出る。

| コア | 素性 |
| --- | --- |
| **変愚蛮怒** | 上流 Hengband 3.0.2.3-Beta |
| **短愚蛮怒** | 変愚蛮怒の短編版（同じソースを別の設定で組む） |
| **幻想蛮怒** | 東方の意匠を持つ変愚蛮怒の派生。町とダンジョンを作り込み、英語版も用意した |
| **Sil-Q** | Sil の後継。日本語化した |
| **FroxComposband** | Composband の派生。日本語化した |

コアはそれぞれ独立した実行体で、画面とはプロトコル v1 で繋がる。
**画面はコアごとの知識を持たない**——コアが自分の名前と能力を名乗り、画面はそれに従う。

---

## 振る舞いが変わっていないことを確かめる

`tools/hd2d_verify/` に、**改修の前後で振る舞いが変わっていないかを機械で確かめる道具**が
3 つ入っている。外部の生成 AI もインターネットも要らない（Python の標準ライブラリだけ）。

```
python tools/hd2d_verify/golden.py --check       検査モード 18 個の出力を突き合わせる
python tools/hd2d_verify/playthrough.py --check  実際に遊ばせて通しの振る舞いを見る
python tools/hd2d_verify/replay.py --check       コアを止めてゲームループを 1 ビット単位で見る
```

**最初に自分の機械で基準を採ること**（`--record`）。基準には絵のハッシュが入るので、
GPU が違えば値も違う——他人の基準は当てにならない。

`replay.py` が要になっている。**コアを起こさず、採っておいた会話を 1 周に 1 通ずつ配り、
時計も決め打ちにする**ので、同じ操作なら**絵が完全に一致する**。ゲームループ
（5,800 行あった）を作り直すとき、これが唯一の証拠になる。

くわしくは各ファイルの冒頭の説明にある。何を捕まえて**何を捕まえないか**も書いてある。

---

## ソースの見取り図

| 木 | 中身 |
| --- | --- |
| `hd2d/` | ボクセルの画面（この派生版の中心） |
| `presentation/` | コア側の画づくりとプロトコル |
| `platform/` | Windows / Android の入口 |
| `src/` | 変愚蛮怒・短愚蛮怒のコア（上流由来） |
| `gensoband/` `silq/` `frox/` `tangband/` | 各コア |
| `tools/voxel/` | ボクセルの模型を手続きで組み立てる（外部の生成 AI は要らない） |
| `tools/hd2d_verify/` | 振る舞いが変わっていないかを確かめる網 |

**設計文書は含めていない。** この案件の内部向けの記録であり、受け取った人へ向けて
書かれていないため。**代わりにソースの註釈を厚くしてある**——なぜそう作ったか・
どんな罠を踏んだかは、関数やファイルの冒頭に日本語で書いてある。

**`presentation/` を触ると Windows と Android の両方に効く。** 片方だけ確かめて
済ませないこと。

---

## 上流の文書

上流 Hengband の文書はそのまま残してある（ライセンス条件 (1)「全ての著作権表記を
そのままの形で含む」のため）。

| ファイル | 中身 |
| --- | --- |
| `readme.md` | 上流 Hengband の説明（日本語） |
| `readme-eng.md` | 同（英語） |
| `readme_angband` | Angband の説明 |
| `lib/help/jlicense.txt` | 変愚蛮怒の著作権と使用許諾の全文 |
| `silq/LICENSE.md` | Sil-Q のライセンス |

---

## ライセンス

**系統ごとに条項が違う。** [LICENSE](LICENSE) を読むこと。

要点だけ:

- 大部分は **Moria/Angband 使用許諾**——教育・研究・**非営利**なら配布可。
  全ての複写に著作権表記と使用許諾文を含めること
- **Sil-Q は GPL v2 と Angband 使用許諾の選択制**。原著者は派生物でも二重の表明を
  保つことを求めている
- タイル画像は**画像生成 AI（ComfyUI / Stable Diffusion）の出力**である。
  権利の扱いは各自で確認すること

---

## 謝辞

上流の変愚蛮怒、その前身の ZAngband・Angband・Umoria・Moria、そして
幻想蛮怒・Sil-Q・FroxComposband の作者と貢献者に。
著作権表記の全文は [LICENSE](LICENSE) と `lib/help/jlicense.txt` にある。
