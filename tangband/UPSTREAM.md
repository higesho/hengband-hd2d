# tangband/ の取り込み記録

| 項目 | 値 |
|---|---|
| 取り込み元 | https://github.com/tanguband/tangband ブランチ `tang26` |
| 取り込んだ版 | v26.0.4（`8d8e1f2cd`、2026-03-22） |
| 先方の分岐点 | 本家 hengband develop `fca19de548c823d4f03f23f1cd2b58a8fa28185c`（2026-03-02、3.0.2.2-Beta＋56） |
| 取り込み日 | 2026-08-18 |
| 方式 | 検討の定義の置き場 の T-B。**先方の木は持ち込まない**。lib データは当方 3.0.2.4 基底へ三方マージした結果、src 差分は `#ifdef TANGBAND` で当方 `src/` に同居 |

## この lib/ に入っているもの

`lib/edit/` は当方 `lib/edit/`（3.0.2.4 相当）に tangband 差分を三方マージしたもの。
`save/ apex/ bone/ data/ user/` は空（実行時生成。**変愚の `lib/` と交差しない**）。
`file/ help/ pref/ xtra/ script/ info/` は持たない——**変愚側 `lib/` を共有**する
（`presentation/bootstrap/sdl_game_bootstrap.cpp` の TANGBAND ゲートが edit/data/save/apex/bone/user だけ差し替える）。

## マージで手判断した箇所（再照合のときに見る）

| 箇所 | 判断 |
|---|---|
| MonraceDefinitions.jsonc: id 237/373/481/882 の DROP dice | 本家 3.0.2.4 の整理後の値を採用（先方は隣に DROP_GREAT を足しただけで、dice は fork 時のまま。DROP_GREAT は自動マージ済みを確認） |
| 同: id 1034 のフラグ配列 | 集合は同一。本家の複数行書式を採用 |
| 同: 終末のサーペント | **先方の id 1395 は本家が虚弱なイークに使用済み（1395〜1416 を fork 後に追加）。id 1417 に振り直して末尾へ追加**。`spawn_doomsday_serpents()`（src 側）も 1417 を指す |
| towns/01 噂を聞く | 先方は JA 0:0・EN 5:5 と不揃い（README の触れ込みは「無料」）。**両言語 0:0 に統一** |
| quests 001/034 | 先方は旧 .txt を編集、本家は fork 後に .jsonc へ改式。**.jsonc へ手移し**（001: 巻物 352/354/355・金貨 490 の床置き＋警報罠 `^`＝`["ROOM", "NO_TELEPORT_DEST"]`（004 の同一行の本家変換に合わせた）。034: 箱 339・ワンド 282/204・金貨 490 への差し替え＋配置増量） |
| lib/help の 24 ファイル | **未移植**（ゲーム進行に影響しないため後回し。持つなら本家共有をやめて分離が要る） |

## 実装状況

**T0/T1 済み（2026-08-18）。** 組み方と受け入れの記録は。
検査は `python tools/tangband/tb_smoke.py <workdir>`（起動したコアは必ず殺す作り）。

**T2 の絵も済み（同日・決めたことで色替え）。** 終末のサーペント（R1417）は
R862『混沌のサーペント』の**灰燼色替え**（輝度→ガンマ 0.78 で中間調を持ち上げ→冷灰
R×0.92/G×0.97/B×1.07。α 不変）。`tilework/sfc/R1417.png`・`tilework/64/R1417.png` を作り、
`tools/voxel/tile_to_slab.py --ids R1417` で `assets/voxel/slab/R1417.vox` に焼いた。
目録・セル索引は**生成物の表を触らず** `presentation/bridge/official_tile_table_tang.h`
（索引は kOfficialTileCount の続き番号）を TANGBAND ビルドでだけ連結する
（asset_manifest_builder.h と presentation_bridge.cpp にゲート）。変愚の目録はバイト不変。

**help も分離済み（同日）。** `tangband/lib/help`（24 本の表記替え＋URL 1 行を三方マージ）を
持ち、ブートストラップのゲートが `ANGBAND_DIR_HELP` も差し替える。スモークが
ヘルプ画面の「短愚蛮怒」表記まで見る（12/12）。

**T3（Android/Quest）も済み（同日。決めたことで多コア形）。** 単独 APK ではなく、
**:hd2d / :quest の APK にコア .so を複数載せ、起動時のコア選択画面で選ぶ**
（libmain.so＝UI、libhengcore.so＝変愚、libtangcore.so＝短愚。dlopen で
`hengband_core_entry` を引く。定義の置き場）。データは共有アセット束に
tangband/lib（edit・help）ごと入り、実行時ディレクトリは android_asset_installer.cpp が無条件に作る。
エミュレータ（x86_64）で選択画面→**変愚 3.0.2.4・短愚 26.0.4 の両起動**と
tile catalog 1934 件（R1417 入り）まで実測済み。**Quest 版は組めることまで**（実機は未確認）。
一度建てた単独モジュール :tang / :tangquest は同日撤去した。

## 英語

実行時日英切替はそのまま生きる（`tools/tangband/tb_en_check.py` で実測 5/5）。
先方由来の英語の粗——randart の『〜タン』銘・勝利日記等の "Hengband" 残り・
英語ヘルプ未改稿——は**2026-08-18 に決めたでそのまま**（先方仕様に忠実）。

**取り込みは完了（2026-08-18 に判断）。** 残る宿題は Quest 実機での被り確認のみ。

## 先方の版が上がったら

1. の台本で新しい fork 点と差分を取り直す
2. lib/edit は三方マージし直し、**上の表の手判断箇所を必ず開き直す**（規則を変えたらデータ全数の流儀）
3. src 側は `grep -rn "TANGBAND" src presentation platform` が当方ゲートの全量
