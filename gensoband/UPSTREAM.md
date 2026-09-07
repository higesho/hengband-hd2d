# 幻想蛮怒（Gensoband）の取り込み元

This directory is a copy of the Gensoband source and data. See the table below for the
origin, and the last section for how to update it.

このディレクトリは幻想蛮怒のソースとデータの写しです。元は下のリポジトリで、
原則として手を加えていません。

| 項目 | 内容 |
| --- | --- |
| 取り込み元 | <https://github.com/asamayim/gensoband> |
| コミット | `fc4418078a62db9fae2915c8b6ab24a063609093`（2026-02-28、v2.1.6） |
| バージョン | v2.1.6（`src/defines.h` の `H_VER_MAJOR` / `H_VER_MINOR` / `H_VER_PATCH`） |
| 取り込んだ日 | 2026-08-17 |
| 文字コード | ソースも `lib/` も Shift_JIS です（元のビルド設定 `WINDOWS;JP;SJIS` に合わせています） |

## 取り込んだもの

| 元 | ここ |
| --- | --- |
| `src/` | `gensoband/src/` |
| `lib/` | `gensoband/lib/` |
| `README.md`、`change.txt`、`change2.txt` | `gensoband/` の直下 |

## 取り込んでいないもの

- `.git/`（写しなので履歴は持ちません。更新するときは取り込み直します）
- `Gensoband.exe`（元のリポジトリに入っているビルド済みの実行ファイル）

## こちらで追加したもの

| 追加 | 理由 |
| --- | --- |
| `adapter/` | この派生版のためのコードです。写しではありません。UTF-8（BOM 付き）で書いています。BOM を外すと MSVC が Shift_JIS として読んでしまいます |
| `lib/save/.gitkeep`、`lib/data/.gitkeep` | 元のリポジトリには `save/` と `data/` がありません。起動時に `lib/` の下のディレクトリを確認する処理と、`edit/*.txt` から作る `data/*_j.raw` の書き込み先として必要です。中身は追跡しません |

`lib/apex/` `lib/bone/` `lib/info/` `lib/script/` `lib/user/` には元のリポジトリと同じ
`delete.me` を置いて、空のディレクトリを保っています。

## 追跡しているもの

写し（`src/`、`lib/edit/`、`lib/file/` など）は追跡しています。取り込み直したときの差分を
git で見るためです。実行時に作られるもの（`lib/save/`、`lib/apex/*.raw`、`lib/user/`、
`lib/bone/`、`lib/data/*.raw`、`*.INI`）はリポジトリ直下の `.gitignore` で除いています。

## 新しい版を取り込むとき

1. 元のリポジトリを clone し、`src/`、`lib/` と 3 つの文書を上書きコピーします
   （`.git` と `.exe` は入れません）
2. `git diff` で、`//GB:` の印が付いた行が消えていないか確認します。いまのところ
   `//GB:` の変更はありません
3. `VisualStudio/GensobandCore/GensobandCore.vcxproj` の `ClCompile` の一覧に、増減した
   `.c` を反映します（`main.c`、`main-win.c`、`maid-x11.c`、`readdib.c` は除きます）
4. この文書の表（コミット、バージョン、日付）を更新します
