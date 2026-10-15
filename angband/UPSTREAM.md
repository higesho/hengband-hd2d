# Angband の取り込み元

このディレクトリ（`angband/`）は Angband 4.2.6 コア（`AngbandCore.exe`）の**接続コードと
訳・自前のデータ**だけを置く。原作の `src/` も、原作のゲームデータ（`lib/gamedata` など）も
置かない。ソースは `python tools/core_sources/prepare.py` が `../roguelike-cores/upstream/angband`
の固定コミットから `../roguelike-cores/build-sources/angband/patched-src` へ再構成する
（追跡しない出力）。ゲームデータは `python tools/core_sources/prepare_original_data.py` が
同じ固定コミットから `angband/lib/{gamedata,customize,screens,help}` へ写す
（`.gitignore` で追跡の対象から外してある）。

| 項目 | 内容 |
| --- | --- |
| 取り込み元 | <https://github.com/angband/angband> |
| タグ | `4.2.6` |
| コミット | `f3082213b73f3e463e3d0d60bff4b00462beae6e`（2025-12-15） |
| 取得先 | `../roguelike-cores/upstream/angband`（full clone。detached HEAD） |
| 取り込んだ日 | 2026-09-23（S1〜S3 を通して育てている） |
| 文字コード | UTF-8（英語の範囲では ASCII と同じ） |
| ライセンス | 原作は GPL v2 または Angband ライセンスの選択制（`angband/LICENSE.md` は原作の `copying.rst` の写し）。この派生版では GPL v2 を選んで配る（ルートの `LICENSE` §3） |


## 取り込んだもの

| 元 | ここ | 備考 |
| --- | --- | --- |
| `src/*.c` `src/*.h`（直下 332 ファイルを丸ごと写す。組むのは `main-*.c` 12 本・`snd-sdl.c`・`wiz-stats.c`・`main.c` を除いた 149 本） | `../roguelike-cores/build-sources/angband/patched-src/`（追跡しない。`prepare_sources.py` が作る） | `main.c` 自体は組まない（`an_bootstrap.c` が起動列を写す）。`src/borg/`・`src/tests/` はサブディレクトリなので最初から対象外 |
| `lib/gamedata/*` | `angband/lib/gamedata/`（追跡しない。`prepare_original_data.py` が写す） | 45 ファイル（Makefile を除く） |
| `lib/customize/*` | `angband/lib/customize/`（同上） | 11 ファイル |
| `lib/screens/*` | `angband/lib/screens/`（同上） | 4 ファイル |
| `lib/help/*` | `angband/lib/help/`（同上） | 5 ファイル |
| | `angband/LICENSE.md` | 書式だけ Markdown 化。本文は変えていない |

## 取り込んでいないもの

- `.git/`・`src/borg/`（Borg。`ALLOW_BORG` は立てない）・`src/tests/`
- `lib/tiles`・`lib/fonts`・`lib/sounds`・`lib/icons`
  （設計 §1 制約 4。Shockbolt のタイルは改変に許可が要り、商用利用は明示の許諾が要る）
- `lib/mangband`（ネットワーク版 MAngband 用）
- CMake / `src/Makefile.nmake` などの原作のビルド設定
  （こちらは `tools/angband/build_windows.py` が同梱の LLVM-MinGW clang で組む）
- 元のリリースに含まれるビルド済みの実行ファイル

## 原作への手入れ（置換の表。`prepare_sources.py` が当てる）

設計 §4 の A1・A2 と、§8.5 T2 の A3。`ui-menu.c` に 2 か所——`display_menu_row()` が行を描いた直後に
`an_menu_row_drawn()` を、`menu_refresh()` の開始と終了に `an_menu_begin()` /
`an_menu_end()` を呼ぶよう差し込む（弱い既定の空関数つき。実体は `angband/adapter/
an_bootstrap.c`）。各置換は**ちょうど 1 回当たることを確かめてから**適用され、
当たらなければ `prepare_sources.py` が止まる。

**A3**（2026-09-23 追加）: `cmd-core.c` の `cmd_get_target()` の先頭に
`an_note_target_command(cmd)` を 1 行差し込む（弱い既定の空関数つき。実体は
`angband/adapter/an_targeting.c`）。`get_aim_dir_hook`（T1）が呼ばれる**前**に、
いまの `struct command *` をアダプタへ渡すだけ——アダプタはそれを使って
`cmd_get_arg_item(cmd, "item", &obj)` で「投げる/撃つ/使う」で**既に選ばれている**
アイテムを引き、`object_effect(obj)` の `struct effect` からボルト・ビーム・ボールの
予測（射線・範囲）を組む（T2）。

**A4**（2026-09-23 追加）: `ui-target.c` の `target_set_interactive()` の先頭で、アダプタの
`an_target_set_interactive_hook()` を呼ぶ（弱い既定は「何もせず原作の処理へ進む」。実体は
`angband/adapter/an_targeting.c`）。原作の「目標を決める」（`*`。`textui_target()`）と
「見る」（`l`。`do_cmd_look()`）はどちらもこの関数だけを通るので、1 か所で両方を新 UI の
目標選択へ寄せられる（設計 §8.5 T4・§8.5.1）。

**A5**（2026-09-24 追加。2026-09-24 に「手で描いた地図を読む」形へ改めた——遊ぶ人の
判断。原作の乱数まかせの町づくりは、店が区画いっぱいに建って北向きの入口が
建物の陰に隠れ、庭も取れないため）: `gen-cave.c` の `town_gen()` の「保存済みの町が
無い」枝（`if (!c_old) { ... } `）で、次の 3 手を挟む（弱い既定つき。実体は
`angband/adapter/an_town_rng.c` と `angband/adapter/an_town_map.c`）。

1. `an_town_rng_begin()` — `z-rand.c` の乱数の状態一式（`Rand_quick`・`Rand_value`・
   `state_i`・`STATE[]`・`z0`/`z1`/`z2`）を退避する。
2. `an_town_build_from_file(c_new, p)` を試す。**真を返せばそれで町ができている**
   （`angband/lib/hd2d/town.txt` を読んで地形を敷き、`player_place()` まで
   済ませる）。**偽を返したときだけ**、原作の乱数任せの `town_gen_layout(c_new, p)`
   がそのまま動く（ファイルが無い・読めない・書式が壊れているときの安全な逃げ道）。
3. `an_town_rng_end()` — 退避した状態をそのまま書き戻す。

**町の地図をどちらの方法で作っても、ほかの乱数の流れ（キャラクターの能力値・
モンスター・アイテム・店の品ぞろえ・ダンジョンなど）は 1 歩も変えない。**
地図のファイルの書式（1 文字 1 マス・66×22・地形の字の割り当て）は
`angband/adapter/an_town_map.c` の先頭コメントが正本。

**`angband/lib/hd2d/town.txt` は `gamedata`/`customize`/`screens`/`help` の外に
置く**（2026-09-24 の見直し）。`tools/angband/prepare_sources.py` の
`copy_runtime_data()` はその 4 つだけを実行のたびに `shutil.rmtree` してから
原作の中身で埋め直すので、そこへ置くと準備をやり直すたびに消える。読み先は
`an_lib_root()`（`an_bootstrap.c`。`an_bootstrap()` に渡された
`<exe_dir>/angband/lib/` そのもの）から組み、原作の `ANGBAND_DIR_GAMEDATA` には
頼らない。

**A6**（2026-09-24 追加。——階の作り方ごとの意匠）:
`generate.c` の `choose_profile()` が返す直前に `an_cave_profile_chosen(p, profile)` を
1 行差し込む（弱い既定の空関数つき。実体は `angband/adapter/an_profile.c`）。
原作は階の作り方（cave profile。classic / modified / moria / labyrinth / cavern / lair /
gauntlet / hard centre）を `dun->profile` に置くだけで**階にもセーブにも残さない**ので、
アダプタがここで受け取り、`(階を作った turn, 作り方の番号 101〜108)` を
メモリの表と `ANGBAND_DIR_USER/hd2d_profile_<セーブ名>.txt` へ記録する。画面へは
`frame.floor.dungeon_id` として送る（`an_frame.cpp`。`an_get_floor_info()` が `cave->turn` で引く）。
セーブを読み込み直した階は原作が `cave->turn` を戻すので、同じ鍵で引ける。
**選択そのものは 1 ビットも変えない**（観察するだけ）。

原作への変更は A1〜A6 の 6 つで、これ以外は無い。

## 足したもの（このリポジトリ側）

| 追加 | 理由 |
| --- | --- |
| `angband/adapter/an_*.{c,cpp,h}` | この派生版のためのコード。写しではない |
| `angband/adapter/an_wiz_stats_stub.c` | `wiz-stats.c`（3,329 行、既定 OFF）を持ち込まなかったことで欠ける 4 記号（`stats_are_enabled` ほか）の穴埋め。原作がその無効版として持っている `#else /* USE_STATS */` ブロック（`wiz-stats.c:3076-3093`）の写し（関数の中身は変えていない。ヘッダ註記・include はこの adapter の流儀） |
| `angband/adapter/an_text_hooks.c` | `text_mbcs_hook` ほか 4 つの UTF-8 変換フックの穴埋め。持ち込んだ 149 本に `main-win.c` は含まれないが、そこにある `Term_mbstowcs_win` ほか（`main-win.c:2359-2443`）の中身は Windows API 呼び出しだけで `main-win.c` 固有の構造体に依存しないため、そのままここへ写した（原作は編集していない。差すのは `game-input.h` のフックと同じ、関数ポインタ経由） |
| `angband/lib/hd2d/town.txt` | 手で描いた町の地図（この派生版のデータ。原作のデータではない） |
| `angband/lib/user/.gitkeep` | 空の目印。実際にコアが書き込む `save`・`user`・`panic`・`scores`・`archive` は `portable::data_dir()/angband/` の下（設計 §2） |

## 更新するとき

1. `../roguelike-cores/upstream/angband` を新しいタグへ進め、このファイルと
   `tools/angband/prepare_sources.py` の `COMMIT` を書き換える。
2. `python tools/angband/prepare_sources.py` を実行し、A1・A2 の置換が
   引き続き 1 回だけ当たることを確かめる（当たらなければ upstream 側の該当箇所が
   変わったということなので、パッチの anchor 文字列を当て直す）。
3. `python tools/angband/build_windows.py` でビルドし、
   `python tools/angband/protocol_smoke.py` を通す。
