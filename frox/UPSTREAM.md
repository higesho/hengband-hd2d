# FroxComposband の取り込み元

> **2026-09-08: 原作ソースを外部管理へ分離しました。** 接続コード・翻訳・ゲームデータはここに残し、
> 原作の `src/` は `tools/core_sources/prepare.py` が外部に構成します。
> 版とパッチの正本は `tools/core_sources/manifest.json` と `patches/`、手順は `tools/core_sources/README.md`。
> 以下は取り込み時の範囲と出自の記録です。旧 `src/` の場所を現在のビルド入力として使わないでください。


This directory is a copy of the FroxComposband source and data. `frox/src/` is unmodified.
See the table below for the origin, and the last section for how to update it.

このディレクトリは FroxComposband のソースとデータの写しです。`frox/src/` には手を加えていません。

| 項目 | 内容 |
| --- | --- |
| 取り込み元 | <https://github.com/Gliktch/froxcomposband> |
| コミット | `c7641efca8caf6069aa9a71a4b5c8547a64013be`（2026-08-21、`main` の先頭。`git describe` は `v7.3.2-1-gc7641efc`） |
| バージョン | 7.3.pipari.2（`src/defines.h` の `VER_MAJOR` / `VER_MINOR` / `VER_PATCH` / `VER_EXTRA`） |
| 取り込んだ日 | 2026-08-24 |
| 文字コード | `src/` と `lib/edit/` は ASCII です。`lib/file/` と `lib/pref/` に UTF-8 と EUC-JP のファイルが 4 件あります（下記） |
| ライセンス | Angband 使用許諾です。LICENSE ファイルはなく、条項は `frox/src/angband.h` の先頭にあります |

## 取り込んだもの

| 元 | ここ | 備考 |
| --- | --- | --- |
| `src/*.c` `src/*.h` | `frox/src/` | 直下のみ（`.c` 212 件、`.h` 52 件） |
| `lib/edit/` | `frox/lib/edit/` | ゲームデータとクエスト（117 件） |
| `lib/file/` | `frox/lib/file/` | 死亡記事の雛形や名前の表など（71 件） |
| `lib/help/` | `frox/lib/help/` | ヘルプ（65 件）。下の「`lib/help/` について」を参照してください |
| `lib/pref/` | `frox/lib/pref/` | 色、キー、自動拾いの既定値（45 件） |

`lib/save` `lib/data` `lib/apex` `lib/bone` `lib/user` は空のディレクトリとして作っています
（プログラムは起動時にこれらを作らないためです）。中身は追跡しません。

## 取り込んでいないもの

- `.git/` `.github/` `.gitattributes` `.gitignore`
- `lib/xtra/` のフォント、画像、音、音楽 … 出所が書かれていないため。この派生版では自前のものを使います
- `lib/info/` `lib/script/` … 中身が `Makefile` と `delete.me` だけのため
- macOS 用のファイルとリソース（`src/cocoa/`、アイコンなど）
- 元のビルド設定（`Makefile`、`configure` など）… こちらは `VisualStudio/FroxCore/FroxCore.vcxproj` でビルドします
- `my-windows/` `play/` `display/` `design/` … 元の配布物と作業用のファイルです。ビルド済みの実行ファイルは取り込みません

## ASCII でないファイル 4 件

| ファイル | 文字コード | 内容 |
| --- | --- | --- |
| `lib/file/monfear.txt` | UTF-8 | コメント行（`#` で始まる）に日本語があります |
| `lib/file/book-0_jp.txt` | EUC-JP | Hengband 時代の日本語のガイドです。プログラムからは参照されません |
| `lib/pref/picktype.prf` | EUC-JP | 自動拾いの書式の説明（コメント） |
| `lib/pref/proxy.prf` | EUC-JP | 同上 |

日本語化のデータは `frox/lang/ja/` にあります。`frox/lib/edit/` は変えていません。

## こちらで追加したもの

| 追加 | 理由 |
| --- | --- |
| `adapter/` | この派生版のためのコードです。写しではありません。UTF-8（BOM 付き）で書いています |
| `lang/` | 日本語化のデータです |
| `lib/{save,data,apex,bone,user}/.keepalive` | 空のディレクトリを作るためです。中身は追跡しません |

## `lib/help/` について

FroxComposband は起動時に `lib/edit/help_upd.txt` に書かれたバージョンを見て、
食い違っていればヘルプの一部（スポイラー）を生成し直します（`src/init2.c` の
`init_help_files()`）。元のリポジトリは `help_upd.txt` を配っていないので、取り込んだときに
1 度起動して生成した結果を `frox/lib/help/` と `frox/lib/edit/help_upd.txt` に入れてあります。
新しい版を取り込んだときも、同じように 1 度生成してからコミットしてください。

## 新しい版を取り込むとき

1. 元のリポジトリを clone し、上の「取り込んだもの」だけを上書きコピーします
2. `git diff` で `frox/src/` に差分が出ないことを確認します（出た場合は元の変更です。
   こちらの手当ては `frox/adapter/` と vcxproj にしかありません）
3. MSVC でビルドできない箇所が増えていないか確認します。いまは 2 か所を adapter 側で
   回避しています（`z-doc.c` の `/FIwindows.h` と、`strncasecmp` のための `fc_compat.c`）
4. `VisualStudio/FroxCore/FroxCore.vcxproj` の `ClCompile` の一覧に増減した `.c` を反映します
5. `FroxCore.exe --selftest` で地形 188 種の対応を確認します
6. この文書の表を更新します
