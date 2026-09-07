# FroxComposband の取り込み記録

このディレクトリは **FroxComposband の写し**である。基準は先方のリポジトリで、
ここは「取り込んだ時点の複製」でしかない。**`frox/src/` は 1 バイトも改変しない**
。

| 項目 | 値 |
|---|---|
| 取り込み元 | https://github.com/Gliktch/froxcomposband |
| コミット | `c7641efca8caf6069aa9a71a4b5c8547a64013be`（`main` の HEAD） |
| コミット日 | 2026-08-21（件名 `Development: Prepare angband.live staging build`） |
| `git describe` | `v7.3.2-1-gc7641efc` |
| 先方版 | **7.3.pipari.2**（`src/defines.h:19-22` の `VER_MAJOR` / `VER_MINOR` / `VER_PATCH` / `VER_EXTRA`。`src/Makefile.src:8` の `VERSION` と一致） |
| 取り込み日 | 2026-08-24 |
| 文字コード | **`src/` と `lib/edit/` は非 ASCII バイト 0**（機械で全数確認）。ほかに 4 本だけ非 ASCII を持つ（下の「非 ASCII を持つ 4 本」） |
| ライセンス | **旧 Angband ライセンス**。LICENSE ファイルは先方に無く、条項は `frox/src/angband.h:3-8` にある（James E. Wilson, 1989。「教育・研究・非営利の目的なら複製と配布を認める。ただしこの著作権表示と条項を写しに含めること」） |
| 上流の状況 | **現役**（直近 12 か月で 298 コミット・最新は取り込みの 3 日前）。FrogComposband（2023-08-02 で停止）の 298 コミット先で、Frog の HEAD は Frox の祖先である |

## 取り込んだもの

| 先方 | ここ | 備考 |
|---|---|---|
| `src/*.c` `src/*.h` | `frox/src/` | ルート直下のみ。**212 本の `.c` と 52 本の `.h`** |
| `lib/edit/` | `frox/lib/edit/` | 実体の定義とクエストの記述（117 件） |
| `lib/file/` | `frox/lib/file/` | 死亡記事の雛形・名前の表など（71 件） |
| `lib/help/` | `frox/lib/help/` | マニュアル（65 件）。**ここだけは写しのままでは終わらない**——先方の `init_help_files()` が版の食い違いを見て書き足すので、取り込み時に 1 度走らせた結果ごと置く（下の「`lib/help/` の扱い」） |
| `lib/pref/` | `frox/lib/pref/` | 色・キー・自動拾いの既定（45 件） |

`lib/save` `lib/data` `lib/apex` `lib/bone` `lib/user` は**空で作る**
（`init_file_paths()` は道を組むだけで作りはしない）。中身は追跡しない。

## 取り込まなかったもの

| 除外 | 理由 |
|---|---|
| `.git/` `.github/` `.gitattributes` `.gitignore` | 写しであって作業木ではない。上流追随は取り込み直しで行う |
| `lib/xtra/font/` `graf/` `sound/` `music/` | **出所が書かれていない**（設計 §1 制約 5）。当方は自前の絵とフォントを使う |
| `lib/info/` `lib/script/` | 中身は `Makefile` と `delete.me` だけ |
| `src/cocoa/` `src/main-cocoa.m` `src/*.ico` `src/*.bmp` `src/froxcomposband.rc` | macOS と資源ファイル。当方は組まない |
| `src/Makefile*` `src/build/` `src/changes` `src/autoconf.h.in` `src/bigsmov*` | 先方のビルド系。当方は `VisualStudio/FroxCore/FroxCore.vcxproj` で組む |
| `configure` `configure.ac` `acinclude.m4` `m4/` `mk/` `build/` `scripts/` `autogen.sh` `install-sh` `config.*` | 同上 |
| `my-windows/` `play/` `display/` `design/` | 先方の配布物・作業場。**ビルド済みバイナリは取り込まないし実行もしない**（設計 §1 制約 6） |

## 非 ASCII を持つ 4 本（**`src/` と `lib/edit/` には 1 バイトも無い**）

設計 §13.2 は 2 本と書いていたが、**実際は 4 本**である（2026-08-24 に実測。設計へ追記済み）。
どれも註釈か旧版の残骸で、M0 の動作には掛からない。

| ファイル | 符号 | 中身 |
|---|---|---|
| `lib/file/monfear.txt` | **UTF-8** | 註釈行（`#` で始まる）。Frox 期に足された日本語の但し書き |
| `lib/file/book-0_jp.txt` | **EUC-JP** | Hengband 時代の日本語のガイド。先方のコードから参照されない残骸 |
| `lib/pref/picktype.prf` | **EUC-JP** | 自動拾いの書式説明（註釈） |
| `lib/pref/proxy.prf` | **EUC-JP** | 同上（4 行の註釈） |

M1（日本語化）の受け持ちは。**`frox/lib/edit/` は
1 バイトも変えず、`frox/lib-ja/edit/` を別に作る**（設計 §12 の 4）。

## ここで足したもの（先方には無い）

| 追加 | 理由 |
|---|---|
| `adapter/` | 新規コード。写しではない（設計 §3）。**UTF-8 BOM 付きで書く** |
| `tilework/` | 地形の名寄せ表（設計 §4.2）。写しではない |
| `assets/` | コア選択画面の絵 1 枚 |
| `lib/{save,data,apex,bone,user}/.keepalive` | 上記のとおり空の道を作るため。中身は `.gitignore` で外す |

## `lib/help/` の扱い（設計 §3.5 とその 2026-08-24 追記）

**`ALLOW_SPOILERS` は落とせない。** `src/z-config.h:185` が無条件に定義しており、
消える枝は `ANGBAND_LITE` を定義したときだけで、それは `ALLOW_WIZARD` などを
道連れにする（設計 §1 制約 7 に触る）。`frox/src` を直す道は制約 1 で塞がっている。

**代わりに先方の仕掛けをそのまま使う。** `init_help_files()`（`src/init2.c:1163`）は
無条件には書かない——**`lib/edit/help_upd.txt` の `V:` 行が版と食い違うときだけ**
`generate_spoilers()` を呼び、済んだらその `help_upd.txt` を今の版で書き直す。
上流は `help_upd.txt` を配っていない（生成物なので追跡していない）ので、
**初回の 1 度だけ書き戻る**。

| いつ | 何をする |
|---|---|
| 取り込み時 | **1 度だけ走らせて `frox/lib/help/` と `frox/lib/edit/help_upd.txt` を作り、両方コミットする** |
| 以後 | `V:7.3.pipari.2` が一致するので**二度と書かない**（受け入れ §9 の 9） |
| 上流の版が上がったら | `help_upd.txt` は上流に無いので上書きされない。**初回に 1 度書き戻るので、そのときも作り直してコミットし、この記録に日付を書く** |

つまり `frox/lib/help/` と `frox/lib/edit/help_upd.txt` は**写しではなく作成物**である。
ほかの `lib/edit/*.txt` は 1 バイトも変えない（設計 §12 の 4）。

## 上流を取り込み直すとき

1. 先方を clone し、上の「取り込んだもの」だけを上書きで写す。
2. `git diff` で **`frox/src/` に差分が出ないこと**を確かめる
   （出たら上流の変更である。当方の手当ては `frox/adapter/` と vcxproj の側にしか無い）。
3. MSVC の穴が増えていないか見る（設計 §3.7。いまは 2 つ——`z-doc.c` の `/FIwindows.h` と
   `strncasecmp` の `fc_compat.c`）。**増えていても `frox/src` は直さず、設計 §3.7 へ足す。**
4. `VisualStudio/FroxCore/FroxCore.vcxproj` の `ClCompile` 一覧に増減した `.c` を反映する。
5. `FroxCore.exe --selftest` で地形 188 種の全数照合を通す（設計 §4.2）。
6. 本書の表（コミット・版・日付）を更新する。
