# DCSS の地形の名寄せ（設計の材料と表の実物）

2026-10-21。12 本目のコアとして取り込む DCSS 日本語版（`..\roguelike-cores\upstream\crawl-ja`、タグ `0.34.1-ja2`。2026-10-22 に ja4 へ上げたが `rltiles`・`dungeon-feature-type.h`・`feature-data.h` は同じ。書き換えていない）の
**地形 211 定義を、HD2D の地形番号とプレハブへ寄せる表**と、新しく作る物の一覧。
決めたこと（2026-10-21）: **地形の名寄せで足りない物は新しく足す。絵は DCSS のタイルを使い、壁やオブジェクトはタイル画像をもとにボクセルのオブジェクト（プレハブ）にする。**
検討の元は と 日本語版の作業場所の `ja-tools/HD2D_STUDY.md` §3.3。

用語: **ブランチ**は DCSS の言う枝のダンジョン（迷宮・オークの坑道・獣の棲み処など。フォークの辞書の訳に合わせた名前で書く）。
**タイル板**は、タイル画像 1 枚を 1 画素 1 ボクセルで立てた板（`tools/voxel/tile_to_slab.py` の出力と同じ作り）。

読み方: 「確かめた」はコードやデータを読んで（または道具で数えて）確認したこと。「推測」「未確認」は確認していないこと。
**画面で見た確認はしていない**（この文書の設計の部分は表と設計だけで、exe は起動していない）。

**現状（2026-10-21 更新）**: 決めたこと「確かめられない絵もそのまま使う」に合わせ、表の**全行（仕分け a・b を含む）を DCSS のタイルから作ったプレハブにした**
（`tools/dcss/gen_dcss_terrain.py`・`gen_dcss_objects.py`。作り方と撮った絵は `dcss/assets/README.md` §3）。
そのため**表の仕分けは c が 218 行、a は 7 行（描かない 5・旧版互換で床扱い 2。タイルが無い）、b は 0 行**になった。
下の §4 の a・b の説明と §5 の「作り方の案」は**設計の記録**（実物と違う所は各節の先頭に書いた）。権利の扱いは §9 と `dcss/assets/README.md` §2。

| ファイル | 中身 |
| --- | --- |
| `feature_map.csv` | 仕分けの表の実物（225 行。生成物） |
| `build_feature_map.py` | 表を作る道具。**仕分けの決めはこのファイルが正本**。`--write` で作り直し、引数なしで照合、`--stats` で数 |

---

## 1. 結論（数）

| 項目 | 数 |
| --- | ---: |
| DCSS の地形の定義名 | **211**（`NUM_FEATURES` = 221 は番号の幅で、20〜29 は穴。「221 種」と呼ばれていたのは番号の幅のこと） |
| 仕分け a（変愚の既存の地形へそのまま寄せる）。**当初 33 → 今は 7**（描かない 5・旧版互換で床扱い 2。タイルが無い行だけ残る） | **7** |
| 仕分け b（既存のプレハブを色・組み合わせで流用する）。**当初 26 → 今は 0**（DCSS のタイルから作った） | **0** |
| 仕分け c（新しいプレハブを作る）。**当初 152 → 今は 204** | **204** |
| 派生の行（店の種別・条件つきのタイルなど、地形の値を持たない） | 14（全部 c） |
| 作ったプレハブ（表の c の行が指す名前の数。変種の `_01〜` は 1 と数える） | **186**（全部作成済み。ほかに扉の東西向き 10・壁 `dc_wall_catacombs`・ブランチごとの床 61 を足して合計 258 個） |
| 予約した番号 | **2000〜2220**（= 2000 + DCSS の地形の値）と **2300〜2399**（派生。使うのは 2300〜2313） |
| 共通の部分のコードに手が要るか | **要らない見込み**（データだけで足りる）。ただし §8 の 3 点は実機で確かめるか設計する |

c が多いのは、祭壇 29・入口・出口・門 85（枝 24・ポータル・地獄・アビスなど）・罠 17 が**絵ごとに別の物**で、
決めたこと（タイルをもとに作る）に従って 1 絵 1 プレハブにしたため。

---

## 2. HD2D が地形を受け取る仕組み（確かめた）

- コアはマスごとに `terrain_id`（16 ビットの整数）と `feature_flags`（`CELL_FEAT_*` のビット）を送る
  （`presentation/frame/map_cell_view.h`、`cell_feature_bits.h`。JSON の符号化も `uint16_t`）。
- 画面側は `assets/voxel/terrain_prefabs.jsonc` を **`terrain_id` で引く**（`PrefabLibrary::rule_for_terrain`。引く表は 1 つの `rule_by_id_`）。
  節は `terrains`（変愚の番号。`lib/edit/TerrainDefinitions.jsonc` と照合される）・`terrains_surface`（地上だけの上書き）・**`terrains_extra`**（変愚に無い地形。照合から外れる）。
  **別のコアの番号と当たると別の地形が立つ**ので、番号はコアごとに予約帯を分ける。
- 1 行（規則）の欄: `ground`（地面）・`structure`（マスを塞ぐ壁）・`object`（地面の上に立つ物）・`object_ew`（東西に通る所の姿勢）・`sign`（店の看板）・
  `density`・`jitter`・`emissive`（自発光）・`tint`（色の掛け算）・`light_shaft`（上空からの光の柱）。
  `tint` は地面・壁・物のどれにも掛かる（`terrain_view.cpp` の `lib_tint` 呼び出し 4 か所）。
  **規則の `emissive` は地面には全量、物には 0.22 倍で効く**（`kObjectEmissive`。5906 行付近）。物だけを光らせたくても床も光るので、
  光らせたい祭壇・火は、弱い値にするか、光る部分を別の物（炎）に分ける（立ち松明が柱と炎の 2 体なのと同じ。`dungeon_style.h` の註）。
- 規則が無い `terrain_id`、または未視認のマスは**役割の既定**（床・壁・扉・階段…）で描く。役割はコアが送る地図の種別（`MinimapKind`。床・壁・扉・階段・山・水・木・草・溶岩・雪）で決まる。
  ブランチごとの材の差し替え（`dungeon_styles.jsonc`）が効くのは**役割の既定で描くマスだけ**。
- `terrain_id` は**間近で見たマスだけ**が覚えられる（`TerrainMemory`）。フロアの識別（`FloorIdentity`）が変わると記憶は捨てられ、新しい値で上書きされる。
- 水と溶岩の判定は `CELL_FEAT_WATER`・`CELL_FEAT_LAVA` のビット、または**地面のプレハブ名に `water`・`lava` を含む**こと（`liquid_kind`）。
- `sign`（看板）を読むのは町の門のマスの処理（`terrain_view.cpp` の 4147 行付近）だけ。**DCSS に町は無いので、看板で区別する案は使えない**（確かめたのは読み出しの場所まで。町以外で看板を立てる経路は見つけていない）。

前例（どれも `terrains_extra` の予約帯。確かめた）:

| コア | 番号 | 行 | 作り方 |
| --- | --- | ---: | --- |
| TOband | `1600 + feat`（1603〜1804） | 121 | `toband/tilework/feature_tiles.csv` → `tools/toband/gen_terrain_rules.py` が `terrains_extra` の目印の間を書く。`build_mapping.py` が同じ番号をコアの表へ書く |
| Frox | `1300 + feat`（1522〜1544） | 23 | `frox/tilework/terrain_map.csv` の `voxel_id` 欄。変愚と同じ 165 種は恒等 |
| Sil-Q | `1300 + feat`（1302〜1377） | 10 | `silq/tilework/terrain_map.csv` の `voxel_id` 欄 |
| 幻想蛮怒 | `1000 + id`（逃がし先。**行は無い**） | 0 | `gb_frame.cpp` の `align_terrain_id`。別の地形の規則に当たらないための空番号 |
| Angband | `40001`（通れる瓦礫の 1 件） | 0 | `town_styles.jsonc` が `terrain_id: 40001` で受ける |

---

## 3. 番号の予約（確かめた使用状況と、決め）

| 帯 | 使っている者 | 備考 |
| --- | --- | --- |
| 0〜243 | 変愚（変愚の番号をそのまま送るコアも。短愚蛮怒、Frox の 165 種） | `lib/edit/TerrainDefinitions.jsonc` の最大は **243**（の表は「0〜238」のまま古い） |
| 1000〜1252 | 幻想蛮怒 | 行は無い |
| 1300〜1383 | Sil-Q | 表の記述。**行があるのは 1302〜1377 の 10 件** |
| 1522〜1544 | Frox | 23 件 |
| 1603〜1804 | TOband | 121 件 |
| 40001 | Angband | 1 件 |
| **2000〜2220** | **DCSS（今回の決め）** | `2000 + dungeon_feature_type の値`。a（7 行。描かない・旧版互換の床）は変愚の番号へ寄せるので使わない。c の 204 行（定義名。当初は b・c の 178 行）ぶんの規則を書く。**番号を使うのは b・c の行だけ**（空きは残す） |
| **2300〜2399** | **DCSS の派生（今回の決め）** | 地形の値を持たない行。店の種別 7（2300〜2306）、地獄の前室のアーチ（2307）、ゾットの門・宝物庫の門の別の絵（2308・2309）、ゾットの像 4（2310〜2313）。2314〜2399 は予備 |
| **2400〜2699** | **DCSS のブランチごとの床・壁・雲（段 M1。2026-10-21）** | 床 2400〜2499（原作が選ぶ床のタイルの組ごと）・壁 2500〜2649（同・壁）・雲のある床 2650〜2699。コアが原作の `t.bg`・`t.cloud` のタイル番号から選んで送る。規則と番号の表は `tools/dcss/gen_dcss_terrain_rules.py` が作る（`dcss/adapter/dcss_tile_variants.inc`・`assets/voxel/terrain_prefabs.jsonc` の DCSS の節）。**番号は定義名ではなくタイルの組から決まる**ので `feature_map.csv` には載らない |
| **3000〜14967** | **DCSS の物 × 床の組（段 4。2026-10-21）** | 扉・階段・祭壇など背景が透ける物の下の床を、そのマスの場所の床にする。番号 = 3000 + 物の位置 × 64 + (床の地形番号 − 2400)。物 187 個 × 既定以外の床 61 種 = 11,407 行。コアは原作の `t.flv.f`（そのマスの床のタイル）から選んで送る。規則と表は `tools/dcss/gen_dcss_terrain_rules.py` が作る（`dcss_tile_variants.inc` の `kDcssUnderObjects`）。`feature_map.csv` には載らない |

- `uint16_t` なので 65535 まで使える。**1805〜39999・40002〜65535 が空き**（このうち 2000〜2399 を今回使う）。2000 を選んだのは、既存の最大（TOband の 1804）から余白を取り、**次のコアのために 1805〜1999 を残す**ため。
- 番号の元は `TAG_MAJOR_VERSION==34`（`tag-version.h` の既定）の `dungeon_feature_type`。**番号は照合用で、アダプタは定義名で引く**ことにする
  （上流の `dungeon-feature-type.h` が動くと値がずれる）。アダプタは `feature_map.csv` から名前 → 番号の表を生成して持つのがよい（Umoria の `um_tile_map.generated.h` と同じ流儀）。
- `build_feature_map.py` の照合は、予約帯が `terrain_prefabs.jsonc` の既存の `id` とぶつからないことも確かめる（いまは 0 件）。

---

## 4. 仕分けの結果

### 4.1 a（当初 33 行。今は 7 行）— 変愚の既存の地形へそのまま寄せる

**設計の記録。** 下の表の行のうちタイルのある 26 行（扉・階段・木・壁・水・溶岩・床ほか）は、決めたこと（2026-10-21）で DCSS のタイルから作ったプレハブに替え、仕分けを c にした。
いま a に残るのは UNSEEN・EXPLORE_HORIZON・TRAVEL_TRAIL（描かない）と、旧版互換の UNDISCOVERED_TRAP・BADLY_SEALED_DOOR・TRAP_SHADOW・TRAP_SHADOW_DORMANT（出たら床）の 7 行。

| 寄せ先（変愚） | DCSS（行数） |
| --- | --- |
| 0 描かない | UNSEEN・EXPLORE_HORIZON・TRAVEL_TRAIL（3。地図に出ない疑似地形） |
| 1 FLOOR | FLOOR と、旧版互換の UNDISCOVERED_TRAP・BADLY_SEALED_DOOR・TRAP_SHADOW・TRAP_SHADOW_DORMANT（5） |
| 4 / 5 / 32 | OPEN_DOOR / BROKEN_DOOR / CLOSED_DOOR |
| 200 / 201 / 202 | 半透明の扉の開・壊・閉（変愚のガラスの扉） |
| 6 UP_STAIR | STONE_STAIRS_UP_I〜III・EXIT_DUNGEON（4） |
| 7 DOWN_STAIR | STONE_STAIRS_DOWN_I〜III（3） |
| 56 / 57 / 60 | ROCK_WALL / STONE_WALL / PERMAROCK_WALL（壁は役割の既定。ブランチごとの材は §6） |
| 218 / 219 | 半透明の壁・永久の半透明の壁（変愚のガラスの壁。**専用の規則が無く、通常の壁と同じ絵**） |
| 83 / 84 / 85 | DEEP_WATER・OPEN_SEA / SHALLOW_WATER / LAVA・LAVA_SEA（OPEN_SEA・LAVA_SEA は歩けない縁） |
| 96 TREE | TREE |

**a の絵は当初は変愚のプレハブだったが、今は DCSS のタイルから作った物にそろえた**（`dcss/assets/voxel/manifest.tsv` が定義名 → プレハブ名の対応。道具が読んで仕分けを c にする）。

### 4.2 b（当初 27 行。今は 0 行）— 既存のプレハブを色・組み合わせで流用する

**設計の記録。** 流用はやめ、27 行すべてを DCSS のタイルから作ったプレハブにした（扉・壁・水・縦穴・蜘蛛の巣・噴水・像・台座・アーチ・封印の階段ほか。名前は `gen_dcss_terrain.py` の `AB_NAMES`）。下の表は当初の案。

他のコアのプレハブを名前で引く（Frox の `FC_WEB` が Sil-Q の `web_sq` を使った前例と同じ。流用元を作り直すと DCSS の絵も変わる）。**見た目は未確認**（`tint` の値は案）。

| DCSS | 流用するもの |
| --- | --- |
| RUNED_DOOR・SEALED_DOOR | Sil-Q の護符の扉 `door_warded_sq_b`・`_g`（原作の色は薄い青・薄い緑） |
| RUNED_CLEAR_DOOR・SEALED_CLEAR_DOOR | `door_glass_closed` に `tint` |
| SLIMY_WALL | `wall_stone_01〜10` に緑の `tint` |
| FRIGID_WALL | Frox の氷の壁 `wall_ice_fc_01〜03` |
| TOXIC_BOG・MUD | `water_shallow` に緑の `tint`／Outpost の泥 `ground_mud_out_01〜04` |
| TRAP_SHAFT・TRAP_WEB | Sil-Q の深淵 `ground_chasm_sq_01〜03`／蜘蛛の巣 `web_sq` |
| 噴水 8 行（FOUNTAIN_BLUE・SPARKLING・BLOOD・EYES、DRY_FOUNTAIN 4） | `fountain_basin_telb`（色違い）／`fountain_pillar`（涸れた噴水） |
| GRANITE_STATUE・METAL_STATUE・ORB_DAIS | `statue_worn`／`statue_worn` に `tint`／`pedestal_agd_lb` |
| STONE_ARCH・ABANDONED_SHOP・EXPIRED_PORTAL | `arch_stone`・`rubble_pile`（床は `floor_room_01`）。地獄の前室のアーチは `tint` つきの派生 1 行 |
| SEALED_STAIRS_UP・DOWN | `stairs_up`・`stairs_down` に緑の `tint` |

### 4.3 c（当初 165 行 = 定義 152 + 派生 13。今は 218 行 = 定義 204 + 派生 14）— 新しいプレハブを作る

次の節の表。**すべて 1 枚のタイル（先頭の 1 枚）から作る**ので、元のタイル画像の種類は 186、プレハブの名前は 186。
当初は権利の一覧に載る門のタイル 4 種を仮に共通の門 `dc_gate_portal` へ寄せて 136 だったが、決定（そのまま使う）で元のタイルから作った（`dc_gate_ice_cave_portal` など。§9）。

---

## 5. 新しく作るプレハブの一覧と作り方の案

**設計の記録。** 作り方の実物（壁は約 1 ボクセルの凹凸、祭壇・噴水・像・木・店は絵を解釈して一から、床は絵に合わせた凹凸のある物、扉は開閉と東西向き）は `dcss/assets/README.md` §3.2。下の「作り方の案」と個数は当初のもの。

名前は **`dc_<種類>_<名前>`**（試作の `dc_altar_zin` と TOband の `tb_` と同じ流儀。**同じものを 2 語で呼ばない**ため、ほかの案の綴りは使わない）。
タイルは 32x32 が 138 枚、30x31 と 31x32 が各 1 枚（祭壇 ASHENZARI・石化した木）。HD2D の 1 マスは 32 ボクセルなので**1 画素 = 1 ボクセルで 1 マスに収まる**（確かめた）。

| 種類 | 行 | プレハブ | 形 | 作り方の案 |
| --- | ---: | ---: | --- | --- |
| `dc_gate_*` 入口・出口・門 | 85 | 63 | 立つ板 | 枝の入口 12・出口 12、地獄 7、アビス 3、パンデモニウム 3、宝物庫 3、ゾット 3、ポータル系ほか。**絵が正面の図**なので、浮き彫り板（試作 `dc_altar_zin` と同じ作り）にして南向き 1 枚。複数の出口が同じタイルを使う（`dc_gate_portal`・`dc_gate_return_vestibule` など）ので数が行より少ない |
| `dc_altar_*` 祭壇 | 29 | 29 | 立つ板 | 神ごとに別の絵（確かめた。台座は共通だが上が違う）。浮き彫り板。火の祭壇（マクレブ・イグニス）などの光り方は §2 の `emissive` の注意に従う（床も光る） |
| `dc_trap_*` 罠 | 17 | 15 | **床に平らな板** | 床の上の記号なので寝かせる（厚み 1〜2 ボクセル、床の高さ）。`tile_to_slab.py` は立てる板だけなので、**寝かせる版が要る**（`tools/dcss/gen_dcss_terrain.py` へ足すのが自然） |
| `dc_shop_*` 店 | 8 | 7 | 立つ板 | 店の種別ごとの店先の図。武器・防具・宝飾・書・巻物・薬・雑貨。種別は派生番号 2300〜2306 |
| `dc_statue_*`・`dc_tree_*` | 9 | 7 | 立つ板 | オークの像、ゾットの像 3（火・冬・エントロピー）、マングローブ・石化した木・悪魔の樹。**歩けない物**（§8-1） |
| `dc_wall_*` 壁 | 3 | 3 | ブロック | 金属の壁（コマ 9 枚）・水晶の壁・棘射出機。試作 `dc_wall_catacombs` の作り（4 側面に絵、天面は回して暗く、線を彫る）。変種は `_01〜` |
| `dc_floor_*` 床 | 2 | 2 | 床の板 | 塩の平原（4 枚）・装飾された床。試作 `dc_floor_rect` の作り |
| `dc_hatch_*`・`dc_transporter*`・`dc_grate`・`dc_cache_*`・`dc_rune_*` | 12 | 10 | 立つ板／平らな板 | 脱出ハッチ 2・転送装置 2（着地点は平ら）・鉄格子・隠し場所 3（果物・肉・焼き菓子）・ルーンの灯と束縛の印（平ら） |

作り方の共通:
1. **道具**（作成済み）: `tools/dcss/gen_dcss_terrain.py` が `feature_map.csv` の全行を読んで作る（物は `gen_dcss_objects.py`）。タイルの指定（`元のタイル画像`）と名前（`プレハブ案`）は表にある。
2. **色は 1 個 255 まで**（`.vox` の上限）。c の元タイルのうち 21 枚が 255 色を超える（門・祭壇ゴザーグ・金属の壁など）。試作は 96 色へ詰める。**詰めた枚は生成の最後に申告させる**（黙って変えない）。
3. **権利**: 決定（2026-10-21）で、確認できない絵も原作で使われているのでそのまま使う。道具は止まらず、使った絵を `dcss/assets/voxel/tiles_used.tsv` に記録し、来歴は `dcss/assets/tiles_used_provenance.tsv` と突き合わせる（`dcss/assets/README.md` §2）。
4. **向き**: 扉は `object_ew`（東西の姿勢）の対を持つ。DCSS の門・祭壇は**まず南向き 1 枚**（試作と同じ）にし、通路が東西のときの見え方は実機で見て決める。
5. **規則の書き方**: TOband の `gen_terrain_rules.py` と同じく、`terrains_extra` の中に目印の行（`// ---- DCSS（… が書く。手で直さない）----` と `// ---- DCSS ここまで ----`）を置き、その間を道具が丸ごと書く。1 行の例:
   ```
   { "key": "DC_ALTAR_ZIN", "id": 2103, "ground": ["floor_room_01"], "object": ["dc_altar_zin"], "density": 1.0 },
   { "key": "DC_ENTER_ORC", "id": 2070, "ground": ["floor_room_01"], "object": ["dc_gate_enter_orc"], "density": 1.0 },
   { "key": "DC_TRAP_TELEPORT", "id": 2036, "ground": ["floor_room_01"], "object": ["dc_trap_teleport"], "density": 1.0 },
   { "key": "DC_METAL_WALL", "id": 2005, "structure": ["dc_wall_metal_01", "dc_wall_metal_02"] },
   ```
   **`ground` を空にしない**（規則を持つマスは役割の既定へ落ちないので、空だと床が 1 枚も描かれない。Frox の `FC_WEB` の註と同じ）。
6. **検査**: アダプタ側の送る番号の表と、規則の `id` を**全数突き合わせる**。
   `build_feature_map.py` は、a の寄せ先が変愚の表に在ること・b・c の番号が重ならないこと・タイル名に PNG が在ること・日本語名が在ることを確かめる。
7. **軽い案との比較**: 検討文書の案（祭壇・入口・出口の 3 つにまとめ、色や看板で見分ける）なら共通の枠は数個で済むが、
   神ごと・枝ごとに違う DCSS のタイルの絵が失われる。看板は町でしか立たない（§2）ので、色の違いだけが手段になる。本案は決めたことに合わせて**絵ごとに 1 個**にした。

---

## 6. ブランチごとの床と壁（地形の番号ではなく、ダンジョンの意匠）

DCSS の床と壁の見た目は、**地形の種類ではなくブランチ（枝のダンジョン）で決まる**（`tileview.cc` の `tile_default_flv`。確かめた）。
ROCK_WALL・FLOOR が a の行（役割の既定）なのはそのため。ブランチごとの材は `assets/voxel/dungeon_styles.jsonc` に **`"core": "dcss"` の節を足し**、
ブランチを `dungeon_id`・深さを `dun_level` として送る（`FloorIdentity`）。意匠の表はコア名とダンジョン番号で引く仕組みで、コードの変更は要らない（確かめた）。

| ブランチ | 壁（原作のタイル名） | 床 |
| --- | --- | --- |
| 迷宮（ダンジョン本体）・魔術師の研究所・闘技場・肉の坩堝 | `WALL_NORMAL` | `FLOOR_NORMAL` |
| 深層 | `WALL_NORMAL` | `FLOOR_GREY_DIRT_B` |
| 宝物庫・ジグラット・バザール・宝の埋蔵庫 | `WALL_VAULT` | `FLOOR_VAULT` |
| 寺院 | `WALL_VINES` | `FLOOR_VINES` |
| エルフの大広間（旧: ドワーフの大広間・刃の広間） | `WALL_HALL` | `FLOOR_HALL`（ドワーフは `FLOOR_LIMESTONE`） |
| オークの坑道 | `WALL_ORC` | `FLOOR_ORC` |
| 獣の棲み処 | `WALL_LAIR` | `FLOOR_LAIR` |
| スライムの穴ぐら | `WALL_SLIME` | `FLOOR_SLIME` |
| 蛇穴 | `WALL_SNAKE` | `FLOOR_MOSAIC` |
| 沼 | `WALL_SWAMP` | `FLOOR_SWAMP` |
| 浅瀬 | `WALL_SHOALS` | `FLOOR_SAND` |
| 蜘蛛の穴ぐら | `WALL_SPIDER` | `FLOOR_SPIDER` |
| 地下墓地 | `ROCK_WALL_CRYPT` | `FLOOR_CRYPT` |
| 霊廟 | `WALL_UNDEAD` | `FLOOR_TOMB` |
| ゾット | `WALL_ZOT_YELLOW` | `FLOOR_TOMB` |
| 地獄の入り口（前室） | `WALL_HELL` | `FLOOR_CAGE` |
| ディース | `WALL_ZOT_CYAN` | `FLOOR_IRON` |
| ゲヘナ | `WALL_ZOT_RED` | `FLOOR_ROUGH_RED` |
| コキュートス | `WALL_ICE` | `FLOOR_FROZEN` |
| タルタロス | `WALL_COBALT_ROCK` | `FLOOR_BLACK_COBALT` |
| 死者の都／ラビリンス・試練場／下水道／納骨堂／濠城 | `WALL_CATACOMBS`／`WALL_LAB_ROCK`／`WALL_OOZING`／`WALL_SANDSTONE`／`WALL_BRICK_BROWN` | 各ブランチの専用の床 |
| 氷の洞穴・火山・塩の荒野 | `WALL_ICE_BLOCK`／`WALL_VOLCANIC`／`WALL_DESOLATION` | `FLOOR_ICE`／`FLOOR_ROUGH_RED`／`FLOOR_SALT` |
| アビス・パンデモニウム | `WALL_UNDEAD`・`WALL_BARS` を**階ごとの色**で塗る | `FLOOR_NERVES`・`FLOOR_DEMONIC` を階ごとの色で塗る |

- STONE_WALL（積み石）はブランチごとにさらに約 20 種へ上書きされる（`_apply_branch_tile_overrides`。確かめた）。金属・水晶の壁もヴォールト・深層で絵が替わる。**最初は役割の既定の壁 1 組だけ**にして、ブランチごとの差し替えは後回しでよい。
- 試作の `dc_wall_catacombs`（死者の都）・`dc_floor_rect` は、この表の 1 組に当たる。**材ごとに壁と床を 1 組ずつ作り、`dungeon_styles.jsonc` の `wall`・`room_floor` の幹の名前で引く**形になる（変種は `_01〜10`）。
- 階ごとの色（アビス・パンデモニウム）は §8-3。

---

## 7. 動く地形・条件で変わる絵

| 現象 | 原作の事実（読んだ） | 案 |
| --- | --- | --- |
| **アビスの位置ずらし** | プレイヤーが中心から離れると `abyss_area_shift()`（`abyss.cc` 1528 行）が地図の中身を中心へ寄せ直し、**`forget_map(true)` で記憶を消す**（1548 行）。座標がずれる | **ずらしのたびに `FloorIdentity`（`generated_turn` など）を変える**。画面は記憶を捨てて、送られてくる新しい値で組み直す。原作が記憶を消すので見た目の食い違いが無い（推測。実機未確認） |
| アビスの地形の入れ替え | `_update_abyss_terrain()` がプレイヤーの周りの地形を毎ターン書き換える（`abyss_morph`） | `terrain_id` が変わるだけ。`TerrainMemory` は新しい値で上書きする（確かめた）。**変わったマスの立体が再生成されるか**は実機で確かめる（未確認） |
| 浅瀬の潮 | `shoals_apply_tides`（`dgn-shoals.cc` 1001 行）が水と床を入れ替える | 同上（`terrain_id` の更新だけ） |
| 一時的な地形の変化 | `temp_change_terrain`（`terrain.cc` 2169 行。魔法の一時的な壁・氷など） | 同上 |
| ルグヌの腐敗 | `lugonu_corrupt_level`（`abyss.cc` 2322 行）が階の地形と色を替える | 同上。色は §8-3 |
| 扉・階段の開閉 | 地形の種類が替わる | 変愚の扉と同じ（確かめた） |
| ゾットの門・宝物庫の門・像・店 | タイルの選び方が**条件で変わる**（`tilepick.cc`）が、地形の種類は同じ | 派生番号（2300〜）をアダプタが条件で選んで送る（行は用意した） |

アビスの床と壁の**階ごとの色**（`env.floor_colour`・`env.rock_colour`。`_abyss_generate_new_area`〔`abyss.cc` 1630 行〕が決める）には、画面へ送る欄が無い（§8-3）。

---

## 8. 共通の部分に手が要るか

**地形の絵を出すだけなら要らない**（確かめた根拠: §2。規則の表・意匠の表・プレハブはデータで、予約帯の追加は書き足しだけ）。
次の 3 点は、アダプタと実機で確かめるか、設計が要る。

1. **歩けない置物**（STATUE・IDOL・GRATE・MALIGN_GATEWAY の 5 種）。`CELL_FEAT_WALL` を立てると壁のブロックが立つので立てず、`PASSABLE` も立てない（案）。
   役割が壁のマスに「`structure` の無い規則（床＋物）」を置いたとき、床と物が描かれるか（壁のブロックが勝たないか）は**コードの流れだけ読んで未確認**
   （`terrain_view.cpp` 5485〜5780 行。構造が無ければ置かない作りには見える）。**実機で 1 マス置いて確かめる**。
   だめなら規則側に「歩けない物」を示す欄を足す（共通の部分の変更）。
2. **部屋と通路の区別が DCSS に無い**。`CELL_FEAT_ROOM` は変愚の `CAVE_ROOM` の写しで、無いと画面は床の連なりの幅から推定する（暗い部屋で後から床が化けた前例）。
   案: アダプタが**すべての床に `ROOM` を立て**、床の材は意匠の `room_floor` 1 本にする。通路の床（`floor_rough`）は出なくなる。
3. **階ごとの色**（アビス・パンデモニウム。上の表）。送る欄が無い。案: (a) 色は無視して材だけ（`WALL_UNDEAD`・`FLOOR_NERVES` の既定の色）にする（共通の部分は無変更）、
   (b) `FloorIdentity` に色の欄を足す（**追加だけ**の変更で版は上げない。通る全コアで影響なしを確かめる）。**まず (a)** で足りるか見るのがよい。

---

## 9. 決めたことが要る点・確かめていないこと

- **a・b の昇格**: 済み（2026-10-21 の決定。表の全行を DCSS のタイルから作った。§4.1・§4.2）。
- **権利（突き合わせ済み）**: 来歴の一覧（github.com/crawl/tiles の `TILES_UNDER_UNKNOWN_LICENSE.md`）は 1,264 項目で、今の木に対応する絵は 1,012 枚、
  CC0 でないと明記された絵は 5 枚（NetHack 由来のネコ。使っていない）、作者の表記の義務が書かれた絵は 0 枚。**本案件が使った 265 枚のうち 48 枚が一覧に載る**（CC0 と確認できない）が、
  原作で実際に使われているので**そのまま使う**。門のタイル 4 種も元のタイルで作った。記録は `dcss/assets/tile_provenance_unclear.tsv`（一覧の全項目）と
  `dcss/assets/tiles_used_provenance.tsv`（使った絵ごとの区分）。CSV の `権利の注意` は、一覧に載る絵を使う行に付く。配るときの使用許諾の文書に載せる（`dcss/assets/README.md` §2）。
- **色の詰めの見映え**（元の色数が 100〜128 を超える絵はメディアンカットで詰めた。申告は道具の最後に出る）と、**浮き彫りの厚み**・**32 か 64 の粒**は見映えの判断（遊ぶ人）。
- **未確認**: ガラスの壁が不透明のブロックで「ガラスに見えない」こと（`dc_wall_clear_*` は絵の色の不透明ブロック）、歩けない置物の描画（§8-1）、アビスのずらしの実機の動き、門の向き（東西の通路）。
- 旧版のセーブ互換用の定義（TAG_MAJOR_VERSION==34 だけにある物。ENTER_DWARF・ENTER_FOREST・ENTER_BLADE と出口、PAKELLAS の祭壇、TRAP_SHADOW・DORMANT など）は、新規の階には出ないと読んだが（コードの印は `#if TAG_MAJOR_VERSION == 34`）、
  **セーブ読み込みの変換までは追っていない**。行は用意したので、出ても壊れない。
- **番号の照合用の値は上流のヘッダの順に依る**。アダプタは定義名で引くこと（§3）。
- 日本語名はフォークの辞書 `data-features.txt`（`[feature]<英名>`）から取った。**同じ英名で別の定義**は同じ日本語名になる（「ここから脱出する門」14 種・「石の下り階段」「石の上り階段」各 3 種・「涸れた噴水」4 種ほか、9 組）。

---

## 10. 使い方

```
python -u dcss/terrain/build_feature_map.py            # 照合（CSV が最新か、問題の件数）
python -u dcss/terrain/build_feature_map.py --stats    # この README の数字の出どころ
python -u dcss/terrain/build_feature_map.py --write    # 仕分けを直したら CSV を作り直す
```

CSV の列: `DCSSの定義名`・`番号`・`日本語名`・`仕分け`・`寄せ先`（a は変愚の番号。b・c は新しい番号）・`寄せ先のキー`・`元のタイル画像`（`rltiles/` からの相対）・`備考`・
`プレハブ案`・`feature_flags案`（アダプタが載せる `CELL_FEAT_*`）・`MinimapKind案`・`権利の注意`・`試作`。先頭の `#` の行が凡例。
