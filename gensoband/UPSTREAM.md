# gensoband の取り込み記録

このディレクトリは**幻想蛮怒（Gensoband）の写し**である。基準は先方のリポジトリで、
ここは「取り込んだ時点の複製」でしかない。**改変は原則しない**。

| 項目 | 値 |
|---|---|
| 取り込み元 | https://github.com/asamayim/gensoband |
| コミット | `fc4418078a62db9fae2915c8b6ab24a063609093` |
| コミット日 | 2026-02-28 20:22:24 +0900（件名 `v2.1.6`） |
| 先方版 | **v2.1.6**（`src/defines.h` の `H_VER_MAJOR/MINOR/PATCH` = 2/1/6） |
| 取り込み日 | 2026-08-17 |
| 文字コード | ソース・`lib/` とも **SJIS**（先方のビルド定義 `WINDOWS;JP;SJIS` に従う） |

## 取り込んだもの

| 先方 | ここ |
|---|---|
| `src/` | `gensoband/src/` |
| `lib/` | `gensoband/lib/` |
| `README.md` `change.txt` `change2.txt` | `gensoband/` 直下 |

## **取り込まなかったもの**

| 除外 | 理由 |
|---|---|
| `.git/` | 写しであって作業木ではない。上流追随は取り込み直しで行う |
| `Gensoband.exe`（先方同梱のビルド済みバイナリ） | 設計 §1 制約 8。**取り込まない・実行しない** |

## ここで足したもの（先方には無い）

| 追加 | 理由 |
|---|---|
| `lib/save/.gitkeep` | 先方の clone に `save/` が無い。旧変愚の `init_stuff` 相当が `lib` 配下の存在を検査する。セーブの実体は追跡しない（`.gitignore`） |
| `lib/data/.gitkeep` | `init_info()` が `edit/*.txt` を焼いた `data/*_j.raw` の**書き込み先**。無いと raw が作れない。raw 自体は追跡しない |
| `adapter/` | 新規コード。写しではない（設計 §3）。**こちらだけ UTF-8（BOM 付き）**で、BOM を落とすと MSVC が SJIS として読んで化ける。写し側は SJIS のまま |

`lib/apex/` `lib/bone/` `lib/info/` `lib/script/` `lib/user/` は先方が `delete.me` を
置いているので、そのまま写して空ディレクトリが保たれている。

## 追跡の方針

写し本体（`src/` `lib/edit/` `lib/file/` ほか）は**追跡する**。上流を取り込み直したときの
差分を git で見るため。実行時に生まれるもの（`lib/save/` `lib/apex/*.raw` `lib/user/`
`lib/bone/` `lib/data/*.raw` `*.INI`）だけをリポジトリ直下の `.gitignore` で外す。

## 上流を取り込み直すとき

1. 先方を clone し、`src/` `lib/` と 3 つの文書を上書きで写す（`.git` と `.exe` は入れない）。
2. `git diff` で `//GB:` 印の付いた行が消えていないか見る（消えたら当て直す）。
   **本書を書いた時点で `//GB:` 改変は 1 行も無い。**
3. `VisualStudio/GensobandCore/GensobandCore.vcxproj` の `ClCompile` 一覧に、
   増減した `.c` を反映する（除外は `main.c` / `main-win.c` / `maid-x11.c` / `readdib.c`）。
4. 本書の表（コミット・版・日付）を更新する。
