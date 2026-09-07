# Sil-Q の取り込み記録

このディレクトリは **Sil-Q の写し**である。基準は先方のリポジトリで、ここは
「取り込んだ時点の複製」でしかない。**改変は原則しない**。

| 項目 | 値 |
|---|---|
| 取り込み元 | https://github.com/sil-quirk/sil-q |
| コミット | `d0355bf97e873ba10e6b364aa127bc5ebbbd05f1`（既定枝の HEAD） |
| コミット日 | 2026-08-05（件名 `Update changelog`） |
| 先方版 | **1.5.1.0-beta2**（`src/defines.h` の `VERSION_STRING`） |
| 取り込み日 | 2026-08-20 |
| 文字コード | **ASCII**（`src/` は main-win.c と types.h に各 1 バイトだけ非 ASCII・`lib/edit/` は全 0） |
| ライセンス | GPL v2 または Angband ライセンスの選択制。`silq/LICENSE.md` を写してある |

## 取り込んだもの

| 先方 | ここ | 備考 |
|---|---|---|
| `src/*.c` `src/*.h` | `silq/src/` | ルート直下のみ。`cocoa/` `unix2dos/` `unix2doslib/` は入れない |
| `lib/edit/*.txt` | `silq/lib/edit/` | 実体の定義（13 本） |
| `lib/pref/*` | `silq/lib/pref/` | 色・キーの既定 |
| `lib/xtra/tutorial` | `silq/lib/xtra/` | チュートリアルのセーブ（データ。媒体ではない） |
| `lib/docs/` の PDF 2 本 | `silq/lang/ja/help/manual.en.txt` | **本文だけを抜いたもの**（P6。下の註） |
| `LICENSE.md` | `silq/LICENSE.md` | **必ず残す**（先方が「派生でも残すのが良い作法」と明記） |

## **取り込まなかったもの**

| 除外 | 理由 |
|---|---|
| `.git/` | 写しであって作業木ではない。上流追随は取り込み直しで行う |
| `lib/xtra/font/` `lib/xtra/sound/` `lib/xtra/graf/` | **ライセンスが本体と別**（設計 §1 制約 5）。フォントは各々の表記、音は freeware、タイル絵は Microchasm 作。当方は使わない |
| `lib/docs/` の PDF そのもの・変更履歴・スクリーンショット | 5.2MB の文書と画像。**PDF は置かず、本文だけをテキストに抜いて置く**（下の「マニュアル」）。変更履歴と画像は要るときに上流を見る |
| `src/cocoa/` `src/main-cocoa.m` `Sil.xcodeproj` `src/*.icns` `src/Sil.ico` `src/sil.rc` | macOS / 資源ファイル。当方は組まない |
| `msvc2022/` `CMakeLists.txt` `bin/` `silg` `silx` `mise.toml` `pyproject.toml` | 先方のビルド系。当方は `VisualStudio/SilCore/SilCore.vcxproj` で組む |
| 先方 release のビルド済みバイナリ | 設計 §1 制約 9。**取り込まない・実行しない** |

## マニュアル（`lib/help` は**先方に無い**）

**`lib/help/` は上流に 1 ファイルも無い。** `ANGBAND_DIR_HELP` は
`init2.c` で解放されるだけで一度も組まれず、先方の `main-win.c:4025` でも
`validate_dir(ANGBAND_DIR_HELP)` はコメントアウトされている。
遊びながら開く `?` の画は `files.c:3003` の `do_cmd_help()` が **C で描く 3 枚**である。

詳しい規則は `lib/docs/` の PDF 2 本にある。**2 本で 1 組**で、Sil-Q 版は差分だけを
書いており、頭に「まず Sil 1.3 のマニュアルを読め」と明記している。

| 本 | 頁 | 抜いた字数 |
|---|---:|---:|
| `Sil 1.3 Manual.pdf` | 37 | 60,800 |
| `Sil-Q 1.4.2 Manual.pdf` | 28 | 34,081 |

日本語化の P6でこの 2 本を取り込み、1 本の
日本語のマニュアルへ編み直した。**PDF そのものは置かない**（5MB あり、画面に出せない）。
取り込み直すときは:

    python tools/silq/sq_import_manual.py           # 上流から落として抽出
    python tools/silq/sq_import_manual.py --pdf-dir DIR

抽出には `pypdf` が要る（機体の Python には入っていない。`--target` で当てる）。
出来上がる `silq/lang/ja/help/manual.en.txt` は**訳の出典**であって配布物ではない。
ライセンスは本体と同じ（GPL v2 または Angband ライセンス。`lib/docs/` に別条件の
表記は無い）。

## ここで足したもの（先方には無い）

| 追加 | 理由 |
|---|---|
| `lib/{data,apex,save,user}/.keepalive` | `init_file_paths()` はこれらの道を組むだけで作りはしない。`data/` は `edit/*.txt` を焼いた `*.raw` の書き込み先で、無いと raw が作れない。実体は追跡しない（`.gitignore`） |
| `adapter/` | 新規コード。写しではない（設計 §3）。**ASCII で書く**（§3.1） |
| `tilework/` | 地形の代表タイル表（設計 §5）。写しではない |

## 追跡の方針

写し本体（`src/` `lib/edit/` `lib/pref/` `LICENSE.md`）は**追跡する**。上流を取り込み直した
ときの差分を git で見るため。実行時に生まれるもの（`lib/save/` `lib/data/*.raw` `lib/apex/`
`lib/user/`）だけをリポジトリ直下の `.gitignore` で外す。

## 上流を取り込み直すとき

1. 先方を clone し、上の「取り込んだもの」だけを上書きで写す。
2. `git diff` で `//SQ:` 印の付いた行が消えていないか見る（消えたら当て直す）。
   **本書を書いた時点で `//SQ:` 改変は 1 行も無い。**
3. `VisualStudio/SilCore/SilCore.vcxproj` の `ClCompile` 一覧に増減した `.c` を反映する
   （除外は設計 §7 の列挙）。
4. 本書の表（コミット・版・日付）を更新する。
