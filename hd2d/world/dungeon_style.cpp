/*!
 * @file dungeon_style.cpp
 * @brief ダンジョンごとの意匠の表（P10 第 4 期）。
 *
 * 下調べは 、案は。
 * **`id` はコアの `DungeonDefinitions.jsonc` の id** で、`FloorIdentity::dungeon_id`
 * にそのまま入っている（`presentation_bridge.cpp` が `FloorType::dungeon_id` を写す）。
 */

#include "world/dungeon_style.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <fstream>
#include <sstream>

namespace hd2d {

namespace {

//! 何も差し替えない意匠（＝今までの絵）。知らないダンジョンはこれになる。
constexpr DungeonStyle kDefaultStyle{
    /* room_floor_stem  */ nullptr,
    /* corridor_floor…  */ nullptr,
    /* wall_stem        */ nullptr,
    /* bedrock_stem     */ nullptr,
    /* dead_end_pile    */ nullptr,
    /* wall_side        */ { nullptr, nullptr, nullptr },
    /* room_edge_pillar */ nullptr,
    /* fire_body        */ nullptr,
    /* fire_flame       */ nullptr,
    /* fire_needs_lit   */ true,
    /* room_floor       */ { nullptr, nullptr, nullptr },
    /* prop_terrains    */ { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
};

struct StyleRow {
    int dungeon_id;
    DungeonStyle style;
};

/*
 * **イークとオークの差は「大きさ」で付ける**（2026-08-10 に決めた:
 * 「オークの洞窟はイークの洞窟より大きなベッド」）。同じ「ベッド」を 2 つ持たせて、
 * オーク（`nest_hide`・径 0.81 マス）をイーク（`nest_straw`・径 0.47 マス）の
 * 倍以上にしてある。並べて見ることは無いが、深さ 1–13 → 10–23 と続く順路なので
 * 記憶と比べられる。
 *
 * 壁はどちらも岩肌（`wall_rock`）。**同じ材でよい**——どちらも掘り抜いた穴であって、
 * 差を作るのは中に置いてあるものの側である。
 */
constexpr StyleRow kRows[] = {
    // ---- 2: イークの洞穴（深さ 1–13・通路率 8・初心者・`y` のみ）----
    // 「ベッドの藁束や食料としている小動物の骨、生えているキノコ」（決めたこと）。
    { 2,
        {
            /*
             * **材ごと替える 1 本目**（2026-08-10 に決めた。意匠を持たせるのは
             * 迷宮・城・金鉱・イークの洞穴の 4 本）。手掘りの穴なので、
             * 部屋の床の敷石も通路の踏み固めた土も切石の壁も全部いらない。
             * 岩盤だけは「掘っていない地山」として壁と作り分けてある。
             */
            /* room_floor_stem  */ "floor_cave",
            /* corridor_floor…  */ "floor_dug",
            /* wall_stem        */ "wall_rock",
            /* bedrock_stem     */ "bedrock_raw",
            //! 行き止まりは瓦礫のまま。**茸は既定が濃く出る**ので触らない（指示の「生えているキノコ」）。
            /* dead_end_pile    */ nullptr,
            //! 壁際にベッド。イークは通路の隅で寝ている。
            /* wall_side        */ { "nest_straw", "bones_small", "stalagmite" },
            //! **切石の柱は立たない。**手掘りの穴なので掘り残しの岩にする。
            /* room_edge_pillar */ "stalagmite",
            //! 立ち松明の代わりに**消し炭の焚き火跡**。イークが鉄の燭台を据えるのは不自然。
            /* fire_body        */ "ash_pit",
            //! 火は消えている（`""`）。だから「明るい部屋だけ」の縛りも外す。
            /* fire_flame       */ "",
            /* fire_needs_lit   */ false,
            //! 樽と木箱は出さない（イークは桶を組まない）。食べかすと茸とベッド。
            /* room_floor       */ { "bones_small", "mushroom_brown", "nest_straw" },
        } },
    // ---- 3: オークの洞窟（深さ 10–23・砂地と山脈壁・`o O T`・最大）----
    // 「イークの洞窟より大きなベッド、壊れた道具、ゴミの山等」（決めたこと）。
    { 3,
        {
            /*
             * **オークは壁だけ**（意匠を持たせる 4 本に入っていない）。床を替えないのは
             * 予算の話だけではない——オークの洞窟の床はコアの側が**砂地 70 / 草 30**
             * なので、間近で見たマスは表が土と草を敷く。役割の既定へ落ちるのは
             * 「まだ間近で見ていないマス」だけである（イークの床は 100% が `FLOOR` で、
             * 細別が無いので既定がそのまま出る＝敷石になっていた）。
             */
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ "wall_rock",
            /* bedrock_stem     */ nullptr,
            //! 行き止まりはゴミの捨て場。**瓦礫と違って石でない物が混ざる**のが読み。
            /* dead_end_pile    */ "refuse_heap",
            /* wall_side        */ { "broken_tools", "bones", "refuse_heap" },
            /*
             * **縄張りの印**（P10 第 6 期）。イークが「掘り残しの岩」＝自然物なのに対し、
             * オークは**置いた物**を立てる。同じ「柱を出さない」でも、そこで 2 本が分かれる。
             */
            /* room_edge_pillar */ "war_stake",
            /*
             * **オークは火を焚く**（イークの消し炭と対）。ただし既定の松明は使えない
             * ——`torch_flame` は受け皿に合わせて z=30 から作ってあるので、地面の焚き火に
             * 載せると炎が 1 マス上に浮く（罠 86 を 3 度目に踏みかけた所）。低い炎を作った。
             * **「明るい部屋」の縛りは外す**：焚き火は部屋の明るさの理由の側であって、
             * 明るいと確定した部屋にしか焚けない道理は無い（`ash_pit` と同じ扱い）。
             */
            /* fire_body        */ "cook_fire",
            /* fire_flame       */ "cook_flame",
            /* fire_needs_lit   */ false,
            //! 大きなベッドが主役。樽と木箱を落として、巣・ゴミ・骨にする。
            /* room_floor       */ { "nest_hide", "refuse_heap", "bones" },
        } },
    // ---- 4: 迷宮（深さ 10–18・迷路・最小・忘却。主は『迷宮のミノタウロス』）----
    //
    // > 迷宮のイメージはミノタウロスの迷宮や Wizardry の四角い迷宮のイメージ。
    // > 小物とはずれるが**壁や床を石ブロックの壁、石壁で他と差別化する**
    // （2026-08-10 に決めた）
    //
    // **小物は 1 つも差し替えていない。**ここは材で語るダンジョンで、
    // 切石の柱も立ち松明も「人が造った迷路」として理屈が合っている。
    { 4,
        {
            //! **部屋と通路で床を分けない**（決めたこと）。同じ石畳を両方へ敷く
            //! ——どこまでが部屋か分からないことが「迷う」の読みになる。
            /* room_floor_stem  */ "floor_lab",
            /* corridor_floor…  */ "floor_lab",
            /* wall_stem        */ "wall_lab",
            /* bedrock_stem     */ "bedrock_lab",
            //! 燃え尽きた松明の燃え殻。**先客が居た**ことだけを言う。
            /* dead_end_pile    */ "torch_stub",
            /* wall_side        */ { "pack_lost", "torch_stub", "rubble_pile" },
            //! **牛頭の柱頭**（主が『迷宮のミノタウロス』）。切石の柱は人が造った迷路に合う。
            /* room_edge_pillar */ "pillar_bull",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            //! 先人の荷物（放り出された背嚢と白骨）。`FORGET` の迷宮で「戻れなかった者」を言う。
            /* room_floor       */ { "pack_lost", "crate", "bones" },
        } },
    // ---- 1: 鉄獄（1–127・本線・制限なし。名前は「鉄の牢獄」） ----
    // **基準に据えたいなら、この 1 行を消せば元の絵に戻る。**
    // 材は替えない（本線の見た目は変えない）。小物だけ 2 枠。
    { 1,
        {
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ nullptr,
            /* bedrock_stem     */ nullptr,
            /* dead_end_pile    */ nullptr,
            /* wall_side        */ { "chain_rusted", "bones", "rubble_pile" },
            /* room_edge_pillar */ nullptr,
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "weapon_broken", "crate", "chain_rusted" },
        } },
    // ---- 5: 竜の住みか（60–72・砂地・溶岩の川・`d D`） ----
    // 巣と宝と骸。**大きさで竜を言う**（卵殻 0.6 マス・鱗 1 枚 0.4 マス・肋骨がマスをまたぐ）。
    { 5,
        {
            /*
             * **壁と岩盤だけ**（P10 第 6 期。2026-08-11）。床はコアが砂地 100 を敷くので
             * 材を作っても出ない（罠 113）。壁は 90% が花崗岩なので既定が効く。
             * 形は替えない——ここも掘り抜いた穴で、違うのは**炎に灼けている**ことだけ。
             */
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ "wall_lair",
            /* bedrock_stem     */ "bedrock_lair",
            /* dead_end_pile    */ "eggshell_dragon",
            /* wall_side        */ { "claw_rock", "bones_huge", "rubble_pile" },
            /* room_edge_pillar */ "stalagmite",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "coin_hoard", "scale_shed", "bones_huge" },
        } },
    // ---- 6: 墓地（40–70・水の床・アンデッドと非生物。主はヴェクナ） ----
    // **墓石 1 つで墓地になる。**
    { 6,
        {
            /*
             * **納骨堂**（P10 第 6 期。2026-08-11）。床 85／壁 75 が既定なので、
             * B〜G のなかでは材の効きがいちばん大きい 1 本である。
             * **城（12）と正面から分かれる**——あちらは磨いた白大理石、こちらは
             * 同じ「人が積んだ石」でありながら苔と水染みで傷んでいる。
             */
            /* room_floor_stem  */ "floor_tomb",
            /* corridor_floor…  */ "floor_crypt",
            /* wall_stem        */ "wall_tomb",
            /* bedrock_stem     */ "bedrock_tomb",
            /* dead_end_pile    */ "grave_broken",
            //! 枯れた花（案 §4）。**束ねてある**ので野の枯れ草ではなく供物に読める。
            /* wall_side        */ { "gravestone", "bones", "flowers_dead" },
            /* room_edge_pillar */ "sarcophagus",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "grave_open", "gravestone", "bones" },
        } },
    // ---- 7: 森（15–32・木の壁・扉なし。主はシェロブ＝闇の蜘蛛） ----
    // 名前・主・小物が一直線に揃う唯一のダンジョン。
    // **切石の柱は立たない**（`NO_DOORS` の森に柱は無い）。松明も出さない——
    // 木の壁の中で炎を焚いているのは筋が通らない。
    { 7,
        {
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ nullptr,
            /* bedrock_stem     */ nullptr,
            /* dead_end_pile    */ "spider_web",
            /* wall_side        */ { "log_fallen", "cocoon", "fern" },
            /* room_edge_pillar */ "fern",
            /* fire_body        */ "",
            /* fire_flame       */ "",
            /* fire_needs_lit   */ true,
            //! 人骨をやめて**鹿の骨**へ（P10 第 6 期。案 §4 の「鹿の骨」）。
            //! 獣の森に人骨が転がっているのは物語が違う——そこは墓地（6）の役目である。
            /* room_floor       */ { "fairy_ring", "bones_deer", "log_fallen" },
        } },
    // ---- 8: 火山（50–60・溶岩の床・炎免疫と飛行のみ） ----
    // **自然の火**。地獄（9）の人造の責め具と対にする。
    { 8,
        {
            /*
             * **壁と岩盤だけ**（P10 第 6 期）。床は砂地 40／溶岩 60 で全部が細別なので
             * 材を作っても出ない（罠 113）。壁は 90% が花崗岩。スコリア＝気泡だらけの
             * 噴出岩で、**赤い割れ目は彫った溝の底にだけ**——面を赤くすると溶岩の壁になり、
             * 地獄（9・壁まで溶岩）と見分けが付かなくなる。
             */
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ "wall_scoria",
            /* bedrock_stem     */ "bedrock_scoria",
            /* dead_end_pile    */ "volcanic_bomb",
            /* wall_side        */ { "fumarole", "sulphur_crust", "charred_wood" },
            //! **暗闇の洞窟（19）の黒い結晶を借りていた**のをやめた（P10 第 6 期）。
            //! あちらは「光を吸う」ための小さな結晶で、部屋の縁に立つ高さを持っていない。
            /* room_edge_pillar */ "pillar_obsidian",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "charred_wood", "volcanic_bomb", "sulphur_crust" },
        } },
    // ---- 9: 地獄（666–696・壁まで溶岩・炎免疫かつ邪悪・要勝利） ----
    // **人造の責め具**。火山（8）の自然と対にする。
    { 9,
        {
            /*
             * **岩盤だけ**（P10 第 6 期）。壁は深い溶岩 80／暗い穴 20、床も溶岩なので、
             * 既定へ落ちるのは岩盤（＝まだ掘っていない厚い壁の中身）しか無い。
             * ただし通路率 0・最大の広さなので、**画面のかなりを岩盤が占める**
             * ——1 役割 10 個で効きの大きい所である。
             */
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ nullptr,
            /* bedrock_stem     */ "bedrock_ember",
            /* dead_end_pile    */ "bones_hung",
            /* wall_side        */ { "iron_stake", "bones_huge", "chain_rusted" },
            /* room_edge_pillar */ "skull_pillar",
            /*
             * **溶けた鉄の池**（案 §4「溶けた鉄の池」。P10 第 6 期）。
             * ④ 部屋の中の枠には置けない——あちらは自発光を 0 で渡すので、
             * 「光らない橙色の塊」になる（光茸で踏んだ穴＝罠 106）。火の枠に載せて
             * 湯面（`iron_pool_glow`）へ自発光を受けさせる。**炎ではないので揺らさない。**
             * 「明るい部屋」の縛りも外す：溶けた鉄は部屋の明るさと関係なく在る。
             */
            /* fire_body        */ "iron_pool",
            /* fire_flame       */ "iron_pool_glow",
            /* fire_needs_lit   */ false,
            /* room_floor       */ { "bones_hung", "iron_stake", "bones_huge" },
        } },
    // ---- 10: 天界（555–585・永久壁・善のみ・要勝利） ----
    // **勝つまで入口が現れない**（`wild.cpp` の `is_winner`）ので、実機で見るには
    // `--dungeon=10` が要る。
    { 10,
        {
            /*
             * **床だけ**（P10 第 6 期）。壁は永久壁 100（`wall_perm` ＝ 磨いた黒曜石）で、
             * **黒い壁と白い床の対比がそのまま天界の絵**になっている。白は城の大理石と
             * 分けたいので、ここは真珠層（青く冷たい白）。通路率 1000 なので、
             * いちばん長く目に入るのは回廊の床（`floor_nacre`）である。
             */
            /* room_floor_stem  */ "floor_halo",
            /* corridor_floor…  */ "floor_nacre",
            /* wall_stem        */ nullptr,
            /* bedrock_stem     */ nullptr,
            /* dead_end_pile    */ "cloud_puff",
            /* wall_side        */ { "column_white", "cloud_puff", "statue_winged" },
            /* room_edge_pillar */ "column_white",
            //! **鉄の立ち松明を降ろした**（P10 第 6 期）。永久壁と真珠層の床へ錆びうる鉄を
            //! 立てるのはちぐはぐである（森とイークで切石の柱を降ろしたのと同じ理屈＝案 §7）。
            //! 炎は既定のまま（`candle_white` の受けは z=27〜31 で `torch_flame` に合う）。
            /* fire_body        */ "candle_white",
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            //! 竪琴（案 §4）。翼の像は壁際の枠に居るので、部屋の中では別のものを引く。
            /* room_floor       */ { "light_font", "harp", "cloud_puff" },
        } },
    // ---- 11: 水没した遺跡（55–75・壁の 80% が深い水。主はヨルムンガンド） ----
    // **水は「揺れるもの」で言う**。海藻を風で揺らすと水中の揺らぎに読める。
    { 11,
        {
            /*
             * **床だけ**（P10 第 6 期）。壁は深い水 80 なので材を作っても出ない（罠 113）。
             * 床は 80% が既定。**水そのものではなく「底に溜まったもの」で水を言う**
             * ——小物で海藻を揺らしたのと同じ手である。
             */
            /* room_floor_stem  */ "floor_silt",
            /* corridor_floor…  */ "floor_shell",
            /* wall_stem        */ nullptr,
            /* bedrock_stem     */ nullptr,
            /* dead_end_pile    */ "column_fallen",
            //! 珊瑚（案 §4）。海藻が**揺れ**で水を言うのに対し、こちらは**枝分かれの形**で言う。
            /* wall_side        */ { "kelp", "coral", "wreck_rib" },
            /* room_edge_pillar */ "statue_worn",
            /*
             * **火を出さない**（P10 第 6 期）。壁の 80% が深い水・床の 20% が浅い水の
             * 遺跡で立ち松明が燃えているのは筋が通らない（森で松明を降ろしたのと同じ）。
             * `""` は「置かない」で、`nullptr`（既定のまま）とは別物である。
             */
            /* fire_body        */ "",
            /* fire_flame       */ "",
            /* fire_needs_lit   */ true,
            //! 沈んだ積荷（案 §4「貝と壺」）。難破船の肋材が船の骨格なら、こちらは中身。
            /* room_floor       */ { "amphora", "kelp", "wreck_rib" },
        } },
    // ---- 13: ルルイエ（80–96・水の床・旧支配者の恐怖。主は偉大なるクトゥルフ） ----
    // **「傾いている」が唯一にして最大の読み。**
    { 13,
        {
            /*
             * **巨石**（P10 第 6 期）。壁は 100% が花崗岩、床も 50% が既定なので、
             * 墓地（6）と並んで材の効きが大きい 1 本である。
             * **傾けるのは目地であってマスではない**——マスが直方体であることは通れる／
             * 通れないの判別なので崩さない（2026-08-09 に決めた）。継ぎ目が水平でも
             * 垂直でもない積み方なら、箱を保ったまま「角度が合わない」を言える。
             * 部屋は巨石 1〜2 枚、通路は柱状節理の輪切り——**石の寸法を大きく違える**のが、
             * 人の寸法で造られていないことの読みになる。
             */
            /* room_floor_stem  */ "floor_cyclo",
            /* corridor_floor…  */ "floor_basalt",
            /* wall_stem        */ "wall_cyclo",
            /* bedrock_stem     */ "bedrock_cyclo",
            /* dead_end_pile    */ "stone_tilted",
            //! 緑の粘液（案 §4）。**壁から垂れている**ので、苔（面に貼る）とも分かれる。
            /* wall_side        */ { "tentacle_carve", "slime_green", "column_fallen" },
            //! わずかに傾いた柱（案 §4）。倒れる向きは変種ごとに替えてある。
            /* room_edge_pillar */ "pillar_tilted",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "glyph_slab", "stone_tilted", "tentacle_carve" },
        } },
    // ---- 14: 山（25–50・山脈壁・草の床・扉なし。主はソロンドール＝大鷲王） ----
    // **新規はほとんど要らなかった**——第 3 期の `alpine`（風で揺れる）と `crag` が
    // そのまま使える。切石の柱は立たない（屋外に近い）。
    { 14,
        {
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ nullptr,
            /* bedrock_stem     */ nullptr,
            //! 落石（案 §4「落石・雪だまり」。P10 第 6 期）。雪だまりは壁際の枠に居るので、
            //! 行き止まりは**落ちてきた石**にする——瓦礫との差は「1 個が大きい」こと。
            /* dead_end_pile    */ "rockfall",
            /* wall_side        */ { "alpine_01", "crag_low_01", "snow_drift" },
            /* room_edge_pillar */ "cairn",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "nest_eagle", "cairn", "alpine_02" },
        } },
    // ---- 16: 反魔法の洞窟（40–50・魔法禁止） ----
    // 反攻撃（17）と**対**。こちらは**石と印**。深さも素材も同じ 2 本なので、
    // 材は替えず（`nullptr`）小物だけで割る——材が同じほうが小物の差が立つ。
    // **印は彫ってあるが光らない。**うっかり自発光を乗せると意味が反転する。
    { 16,
        {
            /*
             * **材は 17 と共有する**（P10 第 5 期。2026-08-11）。案の §6-4 が
             * 「対にするので材が同じほうが小物の差が立つ」と書いていたのは
             * **2 本の間の差**の話である。1 組の材を 2 本で共有すれば、対であることは
             * 崩れないまま鉄獄（＝既定の花崗岩）とは分かれる。床は替えない
             * ——磨いた一枚岩で床まで囲うと部屋が箱に見える。
             */
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ "wall_ward",
            /* bedrock_stem     */ "bedrock_ward",
            /* dead_end_pile    */ "staff_snapped",
            /* wall_side        */ { "ward_stone", "staff_snapped", "rubble_pile" },
            //! **逆さに埋めた剣の輪**（案 §3.6。P10 第 6 期）。封じの石柱は壁際の枠に居る。
            //! 武器としてではなく**封じの杭**として地に刺さっているのが読みで、
            //! 輪（`ward_ring`）が水平の結界なら、こちらは垂直に立つ結界である。
            /* room_edge_pillar */ "sword_inverted",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "ward_ring", "staff_snapped", "ward_stone" },
        } },
    // ---- 17: 反攻撃の洞窟（40–50・殴打禁止・硝子室） ----
    // 反魔法（16）と**対**。こちらは**刃と硝子**。
    { 17,
        {
            //! **16 とまったく同じ材**（意図的に 1 文字も違えていない。上の注記を見よ）。
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ "wall_ward",
            /* bedrock_stem     */ "bedrock_ward",
            /* dead_end_pile    */ "blades_heap",
            /* wall_side        */ { "blade_embedded", "blades_heap", "weapon_broken" },
            /* room_edge_pillar */ "pillar_glass",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "stone_floating", "blades_heap", "weapon_broken" },
        } },
    // ---- 18: カメレオン洞（30–45・カメレオンのみ・最低 30 体） ----
    // **「同じものが 1 つだけ違う」が擬態の読み。**新規は 2 つで済んだ。
    { 18,
        {
            /*
             * **材で擬態を言う**（P10 第 5 期。2026-08-11）。岩・苔・砂が 1 マスの中で
             * 混じり、どれでもない面になる。**10 枚のうち 1 枚だけ違う**のが擬態の読みで、
             * `floor_mimic_10` は色相がずれ、`wall_mimic_07` は**瞳を持つ**。
             * 形は洞窟のまま（掘り抜いた穴であることは変わらない）。
             */
            /* room_floor_stem  */ "floor_mimic",
            /* corridor_floor…  */ "floor_scute",
            /* wall_stem        */ "wall_mimic",
            /* bedrock_stem     */ "bedrock_scute",
            /* dead_end_pile    */ "slough",
            /* wall_side        */ { "stalagmite", "slough", "rubble_pile" },
            /* room_edge_pillar */ "stalagmite",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /* room_floor       */ { "egg_cluster", "slough", "mushroom_brown" },
        } },
    // ---- 19: 暗闇の洞窟（55–72・暗闇・小。主はヌル） ----
    // **置くものより置かないもので語る。**`DARKNESS` はコアの側で `GLOW` を潰すので
    // （`grid.cpp:74`）、明るい部屋が 1 つも立たず**立ち松明は自動で 0 本**になる。
    // 光茸だけが光源になるので、部屋の中の枠でも茸を引く。
    { 19,
        {
            /*
             * **材が光を吸う**（P10 第 5 期。2026-08-11）。ただし**黒にはしない**
             * ——コアが部屋の `GLOW` を潰しているので、材まで黒くすると暗さが二重に
             * 掛かって形が消える。彩度と明度を落とし、**マスの縁だけ**光を返させてある。
             * 形は洞窟のまま（`wall_rock` / `bedrock_raw` の色替え）。
             */
            /* room_floor_stem  */ "floor_murk",
            /* corridor_floor…  */ "floor_soot",
            /* wall_stem        */ "wall_murk",
            /* bedrock_stem     */ "bedrock_murk",
            /* dead_end_pile    */ "torch_stub",
            /* wall_side        */ { "crystal_black", "torch_stub", "bones" },
            //! **柱に刺さったまま消えた松明**（案 §3.9。P10 第 6 期）。
            //! このダンジョンでは立ち松明が 1 本も立たない（`DARKNESS` が `GLOW` を潰す）ので、
            //! **燃えていない松明を柱として置く**ことで「昔は灯っていた」を絵にする。
            /* room_edge_pillar */ "pillar_snuffed",
            /* fire_body        */ nullptr,
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            /*
             * **光茸は部屋の枠へ置けない。**④ の `emit_prop` は自発光を 0 で渡すので、
             * ここへ `mushroom_glow` を入れると「光らない毒々しい青緑の塊」になる
             * （実機の絵で見つけた）。光茸が光るのは①の枠だけ——そちらは
             * `emit_prop(..., 0.12f, 0.34f, 0.26f)` と明示的に自発光を乗せている。
             */
            /* room_floor       */ { "crystal_black", "torch_stub", "bones" },
        } },
    // ---- 20: ガラスの城（40–60・ガラスの床と壁・透明なものと光と闇） ----
    // 材は表（`terrains`）が `GLASS_FLOOR` / `GLASS_WALL` を持っているので触らない。
    // **ボクセルは不透明前提**なので「透ける」は稜を明るくして匂わせるだけ（既存の宿題）。
    { 20,
        {
            /* room_floor_stem  */ nullptr,
            /* corridor_floor…  */ nullptr,
            /* wall_stem        */ nullptr,
            /* bedrock_stem     */ nullptr,
            /* dead_end_pile    */ "glass_shards",
            //! **城（12）の鉄の燭台を借りていた**のをやめた（P10 第 6 期）。硝子の城に鉄は無い。
            /* wall_side        */ { "glass_shards", "glass_sconce", "pillar_glass" },
            /* room_edge_pillar */ "pillar_glass",
            //! 火の枠も硝子の燭台で受ける（受けは z=26〜31 なので既定の炎がそのまま載る）。
            /* fire_body        */ "glass_sconce",
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            //! プリズム（案 §4）。柱が**稜の明るさ**で透けを言うのに対し、こちらは**色**で言う。
            /* room_floor       */ { "glass_table", "prism", "pillar_glass" },
        } },
    // ---- 12: 城（深さ 20–65・洞窟なし・帳・闘技場。主は皇帝『レイザーク』）----
    // **人が造った建物。**洞窟（イーク）と正面から対にする——あちらは掘った穴。
    // 材はモリバント（町の `_mor`）の系統を引く。同じ石工の仕事に見えるほうがよい。
    { 12,
        {
            /* room_floor_stem  */ "floor_cas",
            //! 広間は大判の正方形、廊下は細長い石。**板目が走る**ことが通り道の読み。
            /* corridor_floor…  */ "floor_hall",
            /* wall_stem        */ "wall_cas",
            //! 城に「地山」は無い（`NO_CAVE`）。岩盤に来るのは**分厚い壁の中身**である。
            /* bedrock_stem     */ "bedrock_cas",
            //! 空の鎧が壁際に立っている。行き止まりが「飾ってある廊下の突き当り」になる。
            /* dead_end_pile    */ "armour_stand",
            //! 織物の垂れ幕（`CURTAIN` を持つので理屈が合う）と壁の燭台。
            /* wall_side        */ { "tapestry", "armour_stand", "sconce" },
            //! 柱は切石のまま（既定の `pillar_stone`）。**人が造った建物なので合っている。**
            /* room_edge_pillar */ nullptr,
            //! 火は燭台で受ける。**炎は既定の `torch_flame`** をそのまま乗せる（自発光は枠が渡す）。
            /* fire_body        */ "sconce",
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            //! 椅子（案 §3.4「長机と椅子」。P10 第 6 期）。空の鎧は行き止まりと壁際に居るので、
            //! 部屋の中では長机の相手を引く。
            /* room_floor       */ { "table_long", "chair", "carpet" },
        } },
    // ---- 15: 金鉱（深さ 75–80・最小・通路率 0・壁の 40% が財宝の鉱脈・`$` のみ）----
    // **壁がもう鉱脈**なので、意匠は「人が掘った跡」を足す側に回る。
    { 15,
        {
            //! 切羽（掘っている面）は掘り屑と窪み、運び道は均されて粒が多い。
            /* room_floor_stem  */ "floor_mine",
            /* corridor_floor…  */ "floor_drift",
            /* wall_stem        */ "wall_mine",
            /* bedrock_stem     */ "bedrock_mine",
            //! 掘りかけの切羽（つるはしが岩に刺さったまま）。行き止まり＝**掘っている先端**。
            /* dead_end_pile    */ "pick_stuck",
            //! **支保工が主役。**2 本の柱と梁が通路に並ぶだけで坑道になる。
            /* wall_side        */ { "pit_prop", "ore_heap", "rubble_pile" },
            //! 坑道に切石の柱は立たない（イークと同じ理屈）。支保工は壁際の枠が持つ。
            /* room_edge_pillar */ "stalagmite",
            //! 立ち松明ではなく**カンテラ**。坑夫が持ち込む灯りである（炎は既定を乗せる）。
            /* fire_body        */ "miner_lamp",
            /* fire_flame       */ nullptr,
            /* fire_needs_lit   */ true,
            //! **鉱車**（案 §3.5。町の `cart` を流用する。P10 第 6 期）。掘りかけの切羽は
            //! 行き止まりに居るので、部屋の中では「運び出す側」を引く。新規 0 個。
            /* room_floor       */ { "ore_heap", "cart", "pit_prop" },
        } },
};

} // namespace

// ------------------------------------- データから読むダンジョンの意匠（`load_dungeon_styles`）
namespace {

/*!
 * @brief データ 1 本ぶん。**文字列を自分で持つ**（`DungeonStyle` は `const char *` で指すだけ）。
 *
 * @details `level_min < 0` が「帯なし」。帯つきの節は `dun_level` が
 * `[level_min, level_max]` に入るときだけ当たる（設計 §2.2 の 4 段目）。
 */
struct LoadedDungeon {
    int dungeon_id{ 0 }; //!< 0 = そのコアの**全ダンジョンに効く既定**
    int level_min{ -1 };
    int level_max{ -1 };
    DungeonStyle style{};
};

struct StyleData {
    /*!
     * @brief **参照が安定する容器**でなければならない。
     * @details `std::vector<std::string>` にすると、後から足したときの再配置で
     * 前に返した `c_str()` が宙に浮く（`DungeonStyle` は文字列を借りているだけ）。
     * `rows` も同じ理由で `deque`——引く側は要素への参照を返す。
     */
    std::deque<std::string> strings;
    std::deque<LoadedDungeon> rows;

    const char *keep(const std::string &text)
    {
        this->strings.push_back(text);
        return this->strings.back().c_str();
    }

    /*!
     * @brief §2.2 の選び方。**帯つき ＞ 帯なし ＞ `id: 0`**。
     * @return 当たった節。**節が 1 つも無ければ `nullptr`**（＝べた書きの表へ落ちてよい）
     */
    const LoadedDungeon *find(int dungeon_id, int dun_level) const
    {
        const LoadedDungeon *plain = nullptr;
        const LoadedDungeon *base = nullptr;
        for (const LoadedDungeon &row : this->rows) {
            if (row.dungeon_id == 0) {
                base = &row;
                continue;
            }
            if (row.dungeon_id != dungeon_id) {
                continue;
            }
            if (row.level_min < 0) {
                plain = &row;
                continue;
            }
            if ((dun_level >= row.level_min) && (dun_level <= row.level_max)) {
                return &row; //!< 帯が合ったら最優先（D5 の浅間 51〜55）
            }
        }
        return (plain != nullptr) ? plain : base;
    }
};

StyleData &style_data()
{
    static StyleData data;
    return data;
}

/*!
 * @brief 文字列の 3 値をそのまま写す（`DungeonStyle` の約束）。
 * @details **キーを書かない＝土台のまま**（何もしない）／`""` ＝ 置かない／名前 ＝ 引く。
 * `""` を `nullptr` に潰してはいけない——意図が逆になる（火を消す指定が消える）。
 */
void read_stem(const nlohmann::json &entry, const char *key, StyleData &data, const char *&slot)
{
    if (!entry.contains(key) || !entry[key].is_string()) {
        return;
    }
    slot = data.keep(entry[key].get<std::string>());
}

//! `[a, b, c]` の 3 つ組。**書いた添字だけ**差し替わる（`wall_side` / `room_props`）。
void read_trio(const nlohmann::json &entry, const char *key, StyleData &data, const char *(&slot)[3])
{
    if (!entry.contains(key) || !entry[key].is_array()) {
        return;
    }
    const nlohmann::json &node = entry[key];
    for (std::size_t i = 0; (i < node.size()) && (i < 3); ++i) {
        if (node[i].is_string()) {
            slot[i] = data.keep(node[i].get<std::string>());
        }
    }
}

/*!
 * @brief **撒いてよい地形の key の並び**（D7 の `prop_terrains`）。
 * @details 三つ組と違って**書いたら丸ごと入れ替える**——「土台の草地に帯が花を足す」
 * のような積み方は要らないし、帯で**消したい**（穢土の層では撒かない）ことがある。
 * `[]` と書けば空になる＝従来どおり「役割の既定のマスだけ」。
 */
void read_terrains(const nlohmann::json &entry, const char *key, StyleData &data,
    const char *(&slot)[6])
{
    if (!entry.contains(key) || !entry[key].is_array()) {
        return;
    }
    for (auto &cell : slot) {
        cell = nullptr;
    }
    const nlohmann::json &node = entry[key];
    std::size_t at = 0;
    for (std::size_t i = 0; (i < node.size()) && (at < 6); ++i) {
        if (node[i].is_string()) {
            slot[at++] = data.keep(node[i].get<std::string>());
        }
    }
}

//! 1 節ぶんを土台の上へ重ねる（§2.2 の「キー単位の重ね塗り」）。
void overlay(const nlohmann::json &entry, StyleData &data, DungeonStyle &style)
{
    read_stem(entry, "room_floor", data, style.room_floor_stem);
    read_stem(entry, "corridor_floor", data, style.corridor_floor_stem);
    read_stem(entry, "wall", data, style.wall_stem);
    read_stem(entry, "bedrock", data, style.bedrock_stem);
    read_stem(entry, "dead_end", data, style.dead_end_pile);
    read_trio(entry, "wall_side", data, style.wall_side);
    read_stem(entry, "pillar", data, style.room_edge_pillar);
    read_stem(entry, "fire_body", data, style.fire_body);
    read_stem(entry, "fire_flame", data, style.fire_flame);
    style.fire_needs_lit = entry.value("fire_needs_lit", style.fire_needs_lit);
    /*
     * **④ 部屋の中は `room_props`** である（設計 §3）。`room_floor` のほうは
     * 「材の部屋の床」に取ってあるので、同じ名前を 2 つの意味で使わない。
     */
    read_trio(entry, "room_props", data, style.room_floor);
    read_terrains(entry, "prop_terrains", data, style.prop_terrains);
}

} // namespace

bool load_dungeon_styles(const std::string &voxel_dir, const std::string &core_name, std::string *log)
{
    StyleData &data = style_data();
    data.strings.clear();
    data.rows.clear();
    if (core_name.empty()) {
        return false;
    }
    const std::string sep
        = (voxel_dir.empty() || (voxel_dir.back() == '/') || (voxel_dir.back() == '\\')) ? "" : "/";
    const std::string path = voxel_dir + sep + "dungeon_styles.jsonc";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        //! **無いのは異常ではない。**変愚はべた書きの表だけで従来どおり動く。
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    nlohmann::json root;
    try {
        //! 第 4 引数 = 注釈を無視（`town_styles.jsonc` と同じ読み方）。
        root = nlohmann::json::parse(buffer.str(), nullptr, true, true);
    } catch (const std::exception &e) {
        if (log != nullptr) {
            *log = std::string("dungeon_styles.jsonc を解釈できませんでした: ") + e.what();
        }
        return false;
    }
    if (!root.contains("cores") || !root["cores"].is_array()) {
        if (log != nullptr) {
            *log = "dungeon_styles.jsonc に cores がありません";
        }
        return false;
    }

    /*
     * **3 度なめる**（§2.2 の重ね塗りを読み込みのときに畳んでしまう）。
     *   1 周目 … `id: 0` ＝ そのコアの既定。土台になる
     *   2 周目 … `id` だけの節。土台の上へ重ねる
     *   3 周目 … `id` ＋ `levels` の節。**同じ id の帯なしの節**（無ければ土台）の上へ重ねる
     * 引く側（`find`）は帯 ＞ 帯なし ＞ 既定の順に選ぶだけでよくなる。
     */
    DungeonStyle base_style = kDefaultStyle;
    int taken = 0;
    for (int pass = 0; pass < 3; ++pass) {
        for (const nlohmann::json &core : root["cores"]) {
            if (!core.is_object() || (core.value("core", std::string()) != core_name)) {
                continue;
            }
            if (!core.contains("dungeons") || !core["dungeons"].is_array()) {
                continue;
            }
            for (const nlohmann::json &entry : core["dungeons"]) {
                if (!entry.is_object()) {
                    continue;
                }
                const int entry_id = entry.value("id", 0);
                const bool banded = entry.contains("levels") && entry["levels"].is_array()
                    && (entry["levels"].size() >= 2);
                const int want = (entry_id == 0) ? 0 : (banded ? 2 : 1);
                if (want != pass) {
                    continue;
                }
                LoadedDungeon row;
                row.dungeon_id = entry_id;
                row.style = base_style;
                if (banded) {
                    row.level_min = entry["levels"][0].get<int>();
                    row.level_max = entry["levels"][1].get<int>();
                    //! 帯は**同じ id の帯なしの節**の続きである（浅間の全域 → 穢土の層）。
                    for (const LoadedDungeon &plain : data.rows) {
                        if ((plain.dungeon_id == entry_id) && (plain.level_min < 0)) {
                            row.style = plain.style;
                            break;
                        }
                    }
                }
                overlay(entry, data, row.style);
                if (entry_id == 0) {
                    /*
                     * 既定の節。**この節自身も積む**——`find()` が「節を書いていない
                     * ダンジョン」の落とし先として `dungeon_id == 0` を返すからである。
                     */
                    base_style = row.style;
                }
                data.rows.push_back(row);
                ++taken;
            }
        }
    }
    if (log != nullptr) {
        std::ostringstream note;
        note << "dungeon_styles.jsonc: core=" << core_name << " で " << taken
             << " 節を採りました（既定の節を含む）";
        *log = note.str();
    }
    return taken > 0;
}

const DungeonStyle &dungeon_style_for(int dungeon_id, int dun_level)
{
    /*
     * **データが先**（§2.2 の 1 段目）。そのコア名の節が 1 つでも在れば、
     * 知らないダンジョンも `id: 0` の既定へ落ちる——**べた書きの表 `kRows` へは落ちない**。
     * これが「変愚の意匠が別コアの 16 本に誤って当たる」を 1 節で絶つ仕掛けである。
     */
    if (const LoadedDungeon *const found = style_data().find(dungeon_id, dun_level)) {
        return found->style;
    }
    for (const auto &row : kRows) {
        if (row.dungeon_id == dungeon_id) {
            return row.style;
        }
    }
    return kDefaultStyle;
}

} // namespace hd2d
