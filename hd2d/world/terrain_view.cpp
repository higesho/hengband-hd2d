/*!
 * @file terrain_view.cpp
 * @brief `terrain_view.h` の実装。
 */
#include "world/terrain_view.h"

#include "frame/cell_feature_bits.h"
#include "frame/game_frame.h"
#include "render/surface_wear.h" //!< 箱の材質を**宣言する**（色から当てさせない）
#include "world/dungeon_style.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace hd2d {

namespace {

//! splitmix64。1 ワードを掻き混ぜる。
std::uint64_t mix64(std::uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

//! 種から 0..1 の実数を取り出して種を進める（**同じ順で呼べば同じ列**）。
float next01(std::uint64_t &state)
{
    state = mix64(state);
    return static_cast<float>(state >> 40) / static_cast<float>(1u << 24);
}

//! `lo`..`hi` の実数。
float next_range(std::uint64_t &state, float lo, float hi)
{
    return lo + (next01(state) * (hi - lo));
}

struct Rgb {
    float r;
    float g;
    float b;
};

/*
 * 仮の色。**地形テーブルは引かない**（§9.4「ui のコードは地形を知らない」）。
 * ここは役割ごとの色で、P10 で「terrain_id → プレハブ集合」の対応表へ置き換わる。
 */
constexpr Rgb kRoomFloorA{ 0.62f, 0.58f, 0.52f }; //!< 加工された石畳
constexpr Rgb kRoomFloorB{ 0.55f, 0.51f, 0.46f };
constexpr Rgb kCorridorFloor{ 0.40f, 0.36f, 0.31f }; //!< 掘りっぱなしの床
constexpr Rgb kStructuralWall{ 0.70f, 0.67f, 0.60f }; //!< 石積み
constexpr Rgb kBedrock{ 0.44f, 0.41f, 0.37f }; //!< 岩盤
constexpr Rgb kDoorway{ 0.52f, 0.36f, 0.22f };
constexpr Rgb kStairs{ 0.78f, 0.70f, 0.35f };
constexpr Rgb kProp{ 0.46f, 0.43f, 0.38f };
constexpr Rgb kPillar{ 0.74f, 0.71f, 0.64f };
/*!
 * @name 壁の下に敷く地面（P7 → P10 レビュー 3 で 2 色に分けた）
 *
 * @details **カットアウェイが抜いたときにだけ見える面**である。2026-08-09 に決めた:
 * 「透過されたブロックは底面を表示し、行動可能範囲が茶系の地面、灰色の壁、
 * 岩はさらに黒に近い灰色で判別がつくように」。
 *
 * つまりこの底面は**そこが何だったかを言う印**であって、地面の続きではない。
 * | 抜いた先 | 色 | 読み |
 * |---|---|---|
 * | 床（素材そのもの） | 茶 | **歩ける** |
 * | 構造壁 | 灰 | 壁 |
 * | 岩盤・鉱脈 | 黒に近い灰 | 掘るしかない岩 |
 *
 * **P7 の教訓は残る**（0.26 の一様な暗さにしたら黒い染みに見えた）。岩を暗くするのは
 * 「床との差」を作るためで、黒潰れさせるためではない。
 * @{
 */
constexpr Rgb kUnderWall{ 0.46f, 0.45f, 0.47f }; //!< 構造壁の下。**灰色**（茶の床と分ける）
constexpr Rgb kUnderBedrock{ 0.20f, 0.19f, 0.21f }; //!< 岩盤の下。**黒に近い灰**
/*! @} */

/*!
 * @brief マスごとの**色**の散らし方の倍率（`HD2D_TERRAIN_VARIANCE`）。
 *
 * @details **0.25 で確定させた値**（2026-08-08）。**勝手に動かさないこと。**
 *
 * この世界のマスは 1 つずつ別のインスタンスで、**面が丸ごと 1 色に塗られる**。
 * 散らし幅が小さくても**境目は硬い段差**になるので、広い床や壁では**格子状の線**に見える
 * （「ブロックの影とブロックに線が入る」に気づいたの正体がこれだった）。
 * 1.0 / 0.5 / 0.25 / 0 を並べて見てもらい、**マスの単調さを消しつつ線が目立たない**
 * ところとして 0.25 を選んでいる。
 *
 * **高さの散らしには効かせない。**岩盤の凸凹は「線」ではなく荒々しさの表現で、
 * 別の話である（一緒に消すと洞窟が平らな箱になる）。
 *
 * 0 = 完全に平ら / 1 = P3〜P7 の見え。**並べて比べ直すための手段**として残してある
 * （値を触りたくなったら、同じ 4 枚を撮って見せ直すこと）。
 *
 * @note **P10 でまた見直す。**いまの箱は仮の形で、本物のボクセルプレハブが入ると
 * 素材そのものに模様が出る。そのとき「マスごとに色を散らす」必要が残るかは分からない。
 */
constexpr float kTerrainVarianceDefault = 0.25f;

/*!
 * @brief 装飾をマスの中心からどれだけ隅へ寄せるか（マス単位）。
 * @details **中央には置かない**（2026-08-09 に決めた）。0.28 はプレハブの絵の中心が
 * マスの角から 0.22 マスの所に来る値で、隣のマスへはみ出さずに「隅に寄っている」と読める。
 */
constexpr float kPropCornerOffset = 0.28f;

/*!
 * @brief そのマスは**東西に通る**か（＝南北が壁で塞がっているか）。P10 レビュー 11。
 *
 * @details 扉は「通る向き」を持つ数少ない素材である。素材の基の姿勢は**南北に通る**形
 * （板が東西に張り、方立てが東西の端に立つ）なので、東西の通路にそのまま置くと
 * **壁と平行に扉が立って通れないように見える**（2026-08-09 に決めた:
 * 「扉の向きは東西に通過の場合は 90 度回転して設置して」）。
 *
 * 見るのは**通れる側ではなく壁の側**である。扉の板は壁の並びの上に張るものなので、
 * 「どちらの隣が壁か」が板の向きをそのまま決める。通れる側で判定すると、
 * 四方が開けた部屋の入口（両方通れる）で答えが定まらない。
 *
 * 未探知（`Unknown`）は壁に数えない。分からないときは**基の姿勢のまま**にする
 * （推定で見た目を決めると、情報が増えたときに絵が化ける。罠 40）。
 */
bool passage_runs_east_west(const FloorMeaning &meaning, int gx, int gy)
{
    /*
     * **わざと向きを見ない口**（検査の検査。P10 レビュー 11）。`HD2D_BREAK_DOOR_TURN=1` で
     * 常に「南北に通る」と答える＝基の姿勢で押し通す。`--world-check` の (7) は
     * これで **FAIL しなければならない。**
     */
    static const bool broken = []() {
        const char *const env = std::getenv("HD2D_BREAK_DOOR_TURN");
        const bool on = (env != nullptr) && (env[0] != '\0') && (env[0] != '0');
        if (on) {
            std::fprintf(stderr, "[hd2d] HD2D_BREAK_DOOR_TURN: **扉の向きを見ません**"
                                 "（検査の検査。--world-check は FAIL になるのが正しい）\n");
        }
        return on;
    }();
    if (broken) {
        return false;
    }
    const auto walled = [&meaning](int x, int y) {
        const CellRole role = meaning.role_at(x, y);
        return (role == CellRole::StructuralWall) || (role == CellRole::Bedrock);
    };
    const bool ns_wall = walled(gx, gy - 1) && walled(gx, gy + 1);
    const bool ew_wall = walled(gx - 1, gy) && walled(gx + 1, gy);
    return ns_wall && !ew_wall;
}

float terrain_variance()
{
    static const float scale = [] {
        const char *const value = std::getenv("HD2D_TERRAIN_VARIANCE");
        if ((value == nullptr) || (value[0] == '\0')) {
            return kTerrainVarianceDefault;
        }
        return std::clamp(static_cast<float>(std::atof(value)), 0.f, 1.f);
    }();
    return scale;
}

//! 色を少しだけ揺らす（§7.1-4「インスタンスごとにパレットを少しずらす」の素朴版）。
void tint(InstanceData &instance, const Rgb &base, std::uint64_t &state, float amount = 0.06f)
{
    //! **種は必ず進める**（弱めても配置の決定性が変わらないように）。
    const float raw = next_range(state, -1.f, 1.f);
    const float shift = 1.f + (raw * amount * terrain_variance());
    instance.r = base.r * shift;
    instance.g = base.g * shift;
    instance.b = base.b * shift;
}

/*!
 * @brief `hd2d::YardProp` を索引へ翻訳したもの。
 * @details 表は名前（文字列）しか持たない——ライブラリに無い名前は `prefab < 0` になり、
 * 置く側は黙って飛ばす（`styled()` と同じ「無ければ何もしない」流儀。ただしここは
 * 接尾辞つきの完全な名前を**そのまま**引くので、意匠の接尾辞は掛けない）。
 */
struct YardPropRuntime {
    int prefab{ -1 };
    int flame{ -1 };
    float glow[3]{ 0.f, 0.f, 0.f };
    bool always{ false };
    bool center{ false };
};

//! 敷地 1 件ぶんの前庭の計画（`yards` の門つき敷地、または `sites` の名指し）。
struct SiteYardRuntime {
    std::vector<YardPropRuntime> props; //!< 建物に近い順に消費する
    float fill{ 0.f }; //!< 使い切った後、残りのマスへ乱択する割合（0 なら乱択しない）
    bool use_common_fallback{ false }; //!< 乱択の元に `yard_common` も混ぜるか
};

//! `TownStyle::turf_props` の 1 行を索引へ翻訳したもの。
struct TurfPropRuntime {
    std::vector<int> variants;
    float chance{ 0.f };
};

//! `hd2d::ZoneProp` を索引へ翻訳したもの。
struct ZonePropRuntime {
    int prefab{ -1 }; //!< 単数（変種が無いとき）
    std::vector<int> variants; //!< `<名前>_01..`（あれば種で乱択）
    float chance{ 0.f };
    bool center{ false };
    int flame{ -1 }; //!< 火の相方（-1 = 無し）
    float glow[3]{ 0.f, 0.f, 0.f };
    bool always{ false };
};

//! `hd2d::ZoneFeature` を索引へ翻訳したもの（同 §7.2）。
struct ZoneFeatureRuntime {
    int at_x{ -1 };
    int at_y{ 0 };
    int prefab{ -1 };
    bool center{ true };
    int flame{ -1 }; //!< 火の相方（-1 = 無し）
    float glow[3]{ 0.f, 0.f, 0.f };
    bool always{ false };
};

//! `hd2d::TownZone` を索引へ翻訳したもの（同 §7.2）。
struct ZoneRuntime {
    int x0{ 0 };
    int y0{ 0 };
    int x1{ -1 };
    int y1{ 0 };
    int apply_to{ 0 }; //!< 0 = turf / 1 = path / 2 = all（`TownZone::apply_to` と同じ数）
    std::vector<int> ground; //!< 変種。空なら地面は差し替えない
    //! 道の向きで選ぶ地面（`<ground>_ew` / `_ns` / `_x` の変種。空なら `ground` だけ）。
    std::vector<int> ground_ew;
    std::vector<int> ground_ns;
    std::vector<int> ground_x;
    //! 道の脇の縁（`<ground>_edge_n/_s/_w/_e`。名前は**草地のある側**）。空なら `ground`。
    std::vector<int> ground_edge[4];
    int rim[4]{ -1, -1, -1, -1 }; //!< N / S / W / E（`TownFence` と同じ並び）
    std::vector<ZonePropRuntime> props;
    std::vector<ZonePropRuntime> rim_props;
    std::vector<ZoneFeatureRuntime> features;
};

//! `hd2d::TownFeature`（区画に属さない 1 点。同 §7.3）を索引へ翻訳したもの。
struct TownFeatureRuntime {
    int at_x{ -1 };
    int at_y{ 0 };
    int prefab{ -1 };
    int flame{ -1 }; //!< 火の相方（-1 = 無し）
    float glow[3]{ 0.f, 0.f, 0.f };
    bool always{ false };
};

} // namespace

std::uint64_t cell_seed(int gx, int gy, const FloorIdentity &floor)
{
    // 絶対格子座標とフロア識別子だけを混ぜる。**視界にも走査順にも依存させない。**
    std::uint64_t h = mix64(static_cast<std::uint64_t>(static_cast<std::int64_t>(gx)) * 0x9E3779B97F4A7C15ull);
    h ^= mix64(static_cast<std::uint64_t>(static_cast<std::int64_t>(gy)) * 0xC2B2AE3D27D4EB4Full);
    /*
     * **地上では `generated_turn` を混ぜない**（P10 第 2 期）。地上のフロアは町を出入り
     * するたびに作り直されて turn が変わるので、混ぜると**町へ戻るたびに建物の意匠が
     * 入れ替わる**。町は「同じ場所」なので、それでは困る。
     *
     * 代わりに町の番号（`town_id`。§12-2。同じ段でプロトコルへ足した）を混ぜる。
     * 町ごとに違う姿になり、同じ町なら何度訪れても同じ姿になる。
     * 地下は従来どおり turn を混ぜる（作り直された階は別物であるべき）。
     */
    const bool surface = floor.kind == static_cast<int>(FloorKind::Surface);
    h ^= surface ? mix64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(floor.town_id))
                * 0x27220A95E2F1B4C7ull)
                 : mix64(floor.generated_turn * 0x165667B19E3779F9ull);
    h ^= mix64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(floor.dungeon_id)) << 32)
        ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(floor.dun_level))
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(floor.kind)) << 16));
    return mix64(h);
}

void PropLatch::follow(const FloorIdentity &floor)
{
    if (this->valid && same_floor(this->identity, floor)) {
        return;
    }
    this->cells.clear();
    this->identity = floor;
    this->valid = true;
}


namespace {
//! `CellRole` の数（`Stairs` までの種類。添字の配列を切るのに使う）。
constexpr std::size_t kCellRoleCount = 8;
struct PropSet {
    int rubble{ -1 };
    int bones{ -1 };
    int stalagmite{ -1 };
    int mushroom_glow{ -1 };
    int mushroom_brown{ -1 };
    int pillar{ -1 };
    int torch_stand{ -1 };
    int torch_flame{ -1 };
    int barrel{ -1 };
    int crate{ -1 };
    /*!
     * @name 枠ごとの差し替え（P10 第 4 期。ダンジョンごとの小物）
     * @details 上の 10 個が**素材の名前**、ここから下が**置き場所（枠）**である。
     * 既定では枠が上の素材を指しているだけだが、`DungeonStyle` が名前を持っていれば
     * そのダンジョンだけ差し替わる。**確率と置き方は変えない**——種類だけ替える。
     *
     * 枠を分けたのは、同じ素材が複数の枠から引かれているからである（`rubble` は
     * 行き止まり・通路の壁際・町の草地の 3 か所）。素材の側を差し替えると
     * **町の草地にゴミの山が撒かれる**。
     * @{
     */
    int dead_end_pile{ -1 }; //!< ① 行き止まりに必ず置く塚
    int wall_side[3]{ -1, -1, -1 }; //!< ② 通路の壁際（40 / 30 / 30）
    int room_edge_pillar{ -1 }; //!< ③ 部屋の縁の柱
    int fire_body{ -1 }; //!< ③ 火の本体（松明の柱／焚き火跡）
    int fire_flame{ -1 }; //!< ③ 炎。**-1 なら炎を置かない**＝消えている火
    bool fire_needs_lit{ true }; //!< ③ 火を「明るい部屋」に限るか
    int room_floor[3]{ -1, -1, -1 }; //!< ④ 部屋の中（40 / 35 / 25）
    /*! @} */
    /*!
     * @name 役割の既定の材（ダンジョンの意匠。空なら既定のまま）
     * @details 添字は `CellRole`。**役割の既定で描くマスにだけ**効かせる
     * ——地形の細別を持つマスは表のほうが正なので触らない（§9.4）。
     * @{
     */
    std::vector<int> mat_ground[kCellRoleCount]; //!< 床（`RoomFloor` / `CorridorFloor`）
    std::vector<int> mat_structure[kCellRoleCount]; //!< 塞ぐもの（`StructuralWall` / `Bedrock`）
    /*! @} */
    /*!
     * @brief **小物を撒いてよい地形 id**（利用者の決定 D7。2026-08-21）。空なら従来どおり。
     * @details 下の装飾層は**役割の既定で描いたマスにだけ**回している（地形の細別を持つ
     * マスに撒くと「溶岩の上に木箱が浮く」）。だが**草地と木と水しか無いダンジョン**では
     * それだと枠が 1 度も回らない——幻想蛮怒の無縁塚は細別が 99.4%、魔法の森深部は
     * 100% で、意匠の小物が 1 つも置かれていなかった（全フロア 14,000 マスの実測）。
     *
     * **撒いてよい地形を意匠が名指す**ことで、草地と花だけ緩められる。
     * 木にも水にも溶岩にも撒かないので、過去の失敗は踏まない。
     */
    std::vector<int> prop_terrains;
};

PropSet resolve_terrain_props(const FloorMeaning &meaning, const PrefabLibrary *library, bool use_lib)
{
    PropSet props;
    if (use_lib) {
        props.rubble = library->find("rubble_pile");
        props.bones = library->find("bones");
        props.stalagmite = library->find("stalagmite");
        props.mushroom_glow = library->find("mushroom_glow");
        props.mushroom_brown = library->find("mushroom_brown");
        props.pillar = library->find("pillar_stone");
        props.torch_stand = library->find("torch_stand");
        props.torch_flame = library->find("torch_flame");
        props.barrel = library->find("barrel");
        props.crate = library->find("crate");

        /*
         * 枠の既定。**ここまでが今までの絵**（枠が素材をそのまま指している）。
         */
        props.dead_end_pile = props.rubble;
        props.wall_side[0] = props.rubble;
        props.wall_side[1] = props.bones;
        props.wall_side[2] = props.stalagmite;
        props.room_edge_pillar = props.pillar;
        props.fire_body = props.torch_stand;
        props.fire_flame = props.torch_flame;
        props.room_floor[0] = props.barrel;
        props.room_floor[1] = props.crate;
        props.room_floor[2] = props.bones;

        /*
         * ダンジョンごとの意匠（P10 第 4 期。実装方針 2026-08-10
         * 「同質なダンジョンは名前の意味で小物に差を付けよう」）。
         *
         * **町の `TownStyle` とまったく同じ流儀**——引く名前を替えるだけで、
         * ライブラリに無ければ既定へ落ちる。表は `dungeon_style.cpp`。
         *
         * **地下だけ**に効かせる。地上（町・荒野）の枠は町の側が持っているし、
         * 荒野の `dungeon_id` は 0（＝既定）なので実害は無いが、
         * 「町の値を読むとき全部に `use_town` が要る」（罠 87）の裏返しで、
         * ダンジョンの値も**ダンジョンでだけ**読む形にしておく。
         */
        if (meaning.identity.kind == static_cast<int>(FloorKind::Dungeon)) {
            //! 階（`dun_level`）も渡す。**帯（`levels`）を持つ意匠**がこれで引ける
            //! （関連する実装 D5。浅間浄穢山の 51〜55 階だけ別の材）。
            const DungeonStyle &dstyle
                = dungeon_style_for(meaning.identity.dungeon_id, meaning.identity.dun_level);
            //! `nullptr` = 既定のまま / `""` = 置かない / 名前 = 引く（無ければ既定へ落ちる）。
            const auto swap = [library](const char *name, int fallback) {
                if (name == nullptr) {
                    return fallback;
                }
                if (name[0] == '\0') {
                    return -1;
                }
                const int index = library->find(name);
                return (index >= 0) ? index : fallback;
            };
            props.dead_end_pile = swap(dstyle.dead_end_pile, props.dead_end_pile);
            for (int i = 0; i < 3; ++i) {
                props.wall_side[i] = swap(dstyle.wall_side[i], props.wall_side[i]);
                props.room_floor[i] = swap(dstyle.room_floor[i], props.room_floor[i]);
            }
            props.room_edge_pillar = swap(dstyle.room_edge_pillar, props.room_edge_pillar);
            props.fire_body = swap(dstyle.fire_body, props.fire_body);
            props.fire_flame = swap(dstyle.fire_flame, props.fire_flame);
            props.fire_needs_lit = dstyle.fire_needs_lit;
            /*
             * 役割の既定の材（床・壁・岩盤）。**1 枚も見つからなければ空のまま**で、
             * 既定の材のまま描かれる（素材を作っていないダンジョンは今までの絵で動く）。
             */
            const auto gather_material = [library](const char *stem, std::vector<int> &into) {
                if (stem == nullptr) {
                    return;
                }
                for (int i = 1; i <= 10; ++i) {
                    char tail[8]{};
                    std::snprintf(tail, sizeof(tail), "_%02d", i);
                    const int index = library->find(std::string(stem) + tail);
                    if (index >= 0) {
                        into.push_back(index);
                    }
                }
            };
            gather_material(dstyle.room_floor_stem,
                props.mat_ground[static_cast<std::size_t>(CellRole::RoomFloor)]);
            gather_material(dstyle.corridor_floor_stem,
                props.mat_ground[static_cast<std::size_t>(CellRole::CorridorFloor)]);
            gather_material(dstyle.wall_stem,
                props.mat_structure[static_cast<std::size_t>(CellRole::StructuralWall)]);
            gather_material(dstyle.bedrock_stem,
                props.mat_structure[static_cast<std::size_t>(CellRole::Bedrock)]);
            /*
             * **撒いてよい地形**（D7）。key → id は共通の対応表に聞く（町の `TerrainProp`
             * と同じ流儀で、コードは地形の番号を持たないままでいられる）。
             * **表が知らない key は黙って落とす**——ライブラリの欠けでフレームを落とさない。
             */
            for (const char *const key : dstyle.prop_terrains) {
                if ((key == nullptr) || (key[0] == '\0')) {
                    continue;
                }
                const int id = library->terrain_id_for_key(key);
                if (id > 0) {
                    props.prop_terrains.push_back(id);
                }
            }
        }
    }

    return props;
}
} // namespace

void build_terrain_view(const FloorMeaning &meaning, const Frustum &frustum, TerrainView &out,
    const TerrainMemory *memory, const PrefabLibrary *library, const TownPlan *town, float night, float wall_inset,
    bool black_unknown, PropLatch *latch, bool fps_wall_upper, bool fps_ceiling, bool blocks_only)
{
    out.slabs.clear();
    out.under_slabs.clear();
    out.boxes.clear();
    out.lights.clear();
    /*
     * **「リクエスト」の 3 本も毎回空にする**（2026-08-23。気づいたこと
     * 「時間経過で光の差し込みが重なっていくのか白飛びしてしまう」）。
     *
     * この関数は**毎フレーム**呼ばれる。空にし忘れるとマスごとのリクエストが積み増され、
     * 同じマスの光を何十枚も重ねて描くことになる——**加算なので白へ飛ぶ**し、
     * 配列も際限なく伸びる。
     *
     * **`light_shafts` を足した日に踏んだが、`mist_cells` と `steam_jets` も
     * 同じ穴だった**（前からある。あちらが目に見えて壊れなかったのは、霧が同じマスを
     * 重ねても絵が変わらず、蒸気には `kSteamPuffBudget` の頭打ちがあったからで、
     * **配列が伸び続けるのは同じ**）。3 本まとめてここで空にする。
     */
    out.light_shafts.clear();
    out.mist_cells.clear();
    out.steam_jets.clear();
    out.considered_cells = 0;
    out.culled_cells = 0;
    out.drawn_cells = 0;
    out.prop_count = 0;
    out.lib_instances = 0;
    //! 視線の遮り（P10 レビュー 4）。フロア全域ぶん確保して 0 で埋め直す。
    out.occluder_w = meaning.width;
    out.occluder_h = meaning.height;
    out.occluder_z.assign(
        static_cast<std::size_t>(std::max(0, meaning.width)) * static_cast<std::size_t>(std::max(0, meaning.height)),
        0.f);
    /*
     * **マス 1 つにつき立方体 1 つ**。
     * ライブラリをここで切ると、壁も地面も町の建物も**素の箱と板**（P3 の見え）へ落ちる
     * ——「アスキー地図が言っているものだけを立方体で建てる」がそのまま手に入る。
     * 装飾層と岩盤の高さの散らしは下の 3 か所で別に止める（`blocks_only` を検索すること）。
     */
    const bool use_lib = (library != nullptr) && library->ready() && !blocks_only;
    if (static_cast<int>(out.lib.size()) != (use_lib ? library->count() : 0)) {
        out.lib.assign(use_lib ? static_cast<std::size_t>(library->count()) : 0, {});
    } else {
        for (auto &bucket : out.lib) {
            bucket.clear();
        }
    }
    if (!meaning.valid()) {
        return;
    }
    out.slabs.reserve(2048);
    out.boxes.reserve(2048);

    /*
     * 装飾（marks から置くもの）のプレハブ索引。**表ではなくコードが選ぶ**
     * （「行き止まりに瓦礫」は地形ではなく並びの意味づけなので、§9.4 の表の管轄外）。
     * 無い名前は -1 になり、その装飾は置かれないだけ（ライブラリの欠けで落とさない）。
     */
    PropSet props;
    /*
     * 町の素材（P10 第 2 期・§10）。装飾（`PropSet`）と同じ理由で**コードが名前で引く**：
     * 敷地の分解は地形の細別ではなく**並びの意味づけ**なので、§9.4 の表の管轄外である。
     * 名前は `terrain_prefabs.jsonc` の `extra` に載せてある（載せないとライブラリが読まない＝罠 36）。
     */
    struct TownSet {
        int wall[9]{};
        int roof[9]{};
        int fence[4]{}; //!< N / S / W / E（`TownFence` のビットと同じ並び）
        int parapet[4]{}; //!< 塀の上の胸壁。**外を向いた辺だけ**（同上の並び。無ければ -1）
        int fountain_pillar{ -1 };
        int fountain_jet{ -1 };
        int castle_keep[9]{}; //!< 天守（6 マス。9 スライス）
        int castle_wall[4]{}; //!< 城壁（3 マス。N / S / W / E の 1 辺）
        int gate_pier{ -1 }; //!< 城門の脇柱。門の並びの両端
        int gate_lintel{ -1 }; //!< 城門の梁（南北にくぐる）
        int gate_lintel_ew{ -1 }; //!< 同（東西にくぐる）
        int street_tree{ -1 }; //!< 街路樹（道に面したマスへ。意匠が名前を持つ）
        int lamp_post{ -1 }; //!< 街灯の柱。火は `lamp_flame`（自発光を分けるため別体＝罠 39）
        int lamp_flame{ -1 };
        std::vector<int> grass_ground; //!< 地形の「草地」。**町の地面へ寄せる判定にだけ**使う
        std::vector<int> palisade; //!< 丸太の塀（変種）
        /*!
         * @brief 塀の内側のマス（石の土台だけ。デザイン5・2026-08-19）。
         * @details これをライブラリに持つ意匠（いまは紅魔館 `_koma` だけ）は、柵を帯の
         * **外周 1 列**に絞り、内側のマスをこの土台にする。無い意匠は従来どおり全マス柵。
         */
        std::vector<int> palisade_base;
        /*!
         * @brief **ダンジョンの口の手前の低い岩棚**（2026-08-27 に決めた）。
         * @details 塀のマスのうち `town_plan` が `kRampartLow` を立てたものへ、塀の代わりに
         * 置く。丸太の塀は 1.81 マスで**口の絵（1.31 マス）より高い**ため、鉄獄の口が
         * 埋まっていた。ライブラリに 1 枚も無い環境では**従来どおりの塀**に落ちる。
         */
        std::vector<int> crag_ledge;
        /*!
         * @brief 岩山（P10 第 3 期）。添字は**山の外からの深さ 0〜3**（麓 → 峰）。
         * @details 段は `town_plan.cpp` が `slices` に入れてある。素の意匠は持たない
         * ——山は町の意匠ではなく地形——だったが、**覆いの町**（岩天井。2026-08-18 その3）
         * だけは例外である: 刳り抜いた山の内壁は「外から見上げる山」と別物なので、
         * 接尾辞つき（`crag_low_kyu` など）がライブラリに**あれば**そちらを使う。無い意匠は
         * 従来どおり素の岩山のまま（`gather` の落ち方）。
         */
        std::vector<int> crag[4];
        //! 岩天井（覆いの町の全マスの頭上に敷く蓋。変種。空なら覆わない）。
        std::vector<int> cave_roof;
        //! 岩天井の上の山塊（岩屑 / 裾 / 中 / 高 / 峰。値ノイズで段を選ぶ。空なら積まない）。
        std::vector<int> cave_mound[5];
        //! 交差点の街灯とその火（覆いの町の道筋を照らす。無ければ -1）。
        int cross_lamp{ -1 };
        int cross_lamp_flame{ -1 };
        std::vector<int> alpine; //!< 高山植物。岩山の**天面へ**まばらに載せる
        std::vector<int> path; //!< 踏み固められた通り道の地面（変種）
        std::vector<int> path_ew; //!< 東西に通る道の轍（無ければ `path`）
        std::vector<int> path_ns;
        std::vector<int> path_x; //!< 交差点
        std::vector<int> turf; //!< 短めの草の地面（変種）
        std::vector<int> water; //!< 水の地面。**あぜ道を出す縁の判定にだけ**使う
        int water_deep{ -1 }; //!< 深水の板。**湖の霧のマスの判定にだけ**使う（デザイン4）
        int light{ -1 };
        int ground{ -1 };
        int well{ -1 };
        int cart{ -1 };
        int flowerbed{ -1 };
        int haystack{ -1 };
        int tower{ -1 };
        int arch{ -1 };
        int arch_ew{ -1 };
        //! 南の塀に組み込む**閉じた大門**（2026-08-18。意匠が名前を持つ町だけ。無ければ -1）。
        int great_gate{ -1 };
        //! 石灯籠（2026-08-18。祠の柱の欠片の角に立てる。ライブラリに無ければ -1）。
        int stone_lantern{ -1 };
        //! 石灯籠の灯（火袋の外に貼る発光板。夜だけ自発光で置く）。
        int stone_lantern_flame{ -1 };
        int sign_plain{ -1 }; //!< 無地の板（店の種別がまだ分からない門）
        int sign_base{ -1 }; //!< 看板の台（`sign_base`。看板をその高さだけ持ち上げる）
        //! **見た目だけの玄関**（2026-08-18）。意匠が持たない町では -1 ＝置かない。
        int entrance{ -1 };
        /*!
         * @brief 建物の看板（P10 第 4 期）。`(terrain_id, プレハブ索引)` の並び。
         * @details **`BUILDING_n` は町ごとに別の建物**なので、対応表（terrain id で引く）
         * では表せない。表は `town_plan.cpp` の `town_signs_for()` が町ごとに持つ。
         */
        struct GateSign {
            int terrain_id;
            int sign;
            //! その入口に立てる門（`TownSign::gate`）。**-1 なら意匠の既定**。
            int gate;
            int gate_ew;
        };
        std::vector<GateSign> building_signs;
        int tuft{ -1 }; //!< 草の茂み（町の草地に撒く小物。風で揺れる `grass` を流用）
        int landmark[3]{ -1, -1, -1 }; //!< 添字は `TownSite::landmark`
        /*!
         * @brief 塀のマスの絵の差し替え（デザイン4・2026-08-18。紅魔館の氷壁→氷塊）。
         * @details `(terrain_id, 変種の索引列)` の並び。コア固有の id は表
         * （`town_styles.jsonc` の `rampart_overrides`）が持つ。
         */
        std::vector<std::pair<int, std::vector<int>>> rampart_overrides;
        /*!
         * @name 屋敷（デザイン4 第 2 段。紅魔館の本館）
         * @details 壁は 3 階一体・屋根はひさし出し（どちらも 9 スライス）。内壁は
         * 屋内床のマスの辺に貼る「1 枚 1 辺」。marks は屋内床の地形 id で引く印
         * （螺旋階段・本の机）。towers は屋根の上に立てる 1 枚型（時計塔ほか）。
         * @{
         */
        std::vector<int> manor_wall[9]; //!< 変種（窓なし ×2・窓 1・窓 2）。マスの種で選ぶ
        int manor_roof[9]{ -1, -1, -1, -1, -1, -1, -1, -1, -1 };
        int manor_innerwall[4]{ -1, -1, -1, -1 };
        //! 屋内の床（板張り。デザイン5・2026-08-19）。ライブラリに無い意匠は -1 ＝地形のまま。
        int manor_floor{ -1 };
        std::vector<std::pair<int, int>> manor_marks; //!< (terrain_id, プレハブ索引)
        std::vector<std::array<int, 3>> manor_towers; //!< {gx, gy, プレハブ索引}
        /*!
         * @name 切妻の屋敷（デザイン6・2026-08-19。博麗神社の拝殿）
         * @details 斜面 2 姿勢＋軒のマス 2 姿勢＋箱棟＋妻壁 4 面。置く側が
         * 「外接矩形の北端・南端までの距離の小さいほう」を段にして積む。
         * @{
         */
        bool manor_gable{ false };
        /*!
         * @brief **片流れの屋敷**（デザイン7・2026-08-19。河童のバザーの工房の長屋）。
         * @details 斜面は 4 姿勢（軒の向き = N / S / W / E）。段は「外側の面までの距離」で、
         * 棟は持たない。切妻と同じ配列を使う（両立しない意匠なので枠を分けない）。
         */
        bool manor_shed{ false };
        /*!
         * 切妻: 0 北半分 / 1 南半分。**変種の集合**（デザイン11 で単数から改めた）。
         * 初版は 1 個ずつしか引かず、`_kir`（霧雨魔法店。変種 2 枚で登録）が
         * **1 個も当たらずに切妻そのものが立たなかった**——§4.1 の外せない 1 点
         * 「急勾配の尖った洋風屋根」が出ていなかった。屋根裏の窓（`_02`）も
         * これで初めて使われる。**単数で登録した意匠（`_hak`）も拾う**。
         */
        std::vector<int> gable_slope[2];
        std::vector<int> gable_eave[2]; //!< 段 0（軒のマス）用。軒先 8 vox つき
        /*!
         * @brief 片流れの斜面と軒のマス（姿勢 N / S / W / E ごとに**変種**）。
         * @details 姿勢ごとに 1 個だと、錆の斑がマスの格子にぴたりと揃って**同じ模様の
         * 連鎖**に見えた（合成フレームで実測。長屋は 20 マスも続く）。変種をマスの種で選ぶ。
         */
        std::vector<int> shed_slope[4];
        std::vector<int> shed_eave[4];
        int gable_ridge{ -1 };
        int gable_wall[4]{ -1, -1, -1, -1 }; //!< 妻壁（N / S / W / E）。sz で伸ばす
        /*!
         * @brief **入母屋の屋敷**（デザイン8・2026-08-19。永遠亭の大屋敷）。
         * @details 斜面と軒のマスは片流れと同じ 4 姿勢の変種を使い（`shed_slope` /
         * `shed_eave`）、棟は**通る向きが 2 通り**なので東西の `gable_ridge` に
         * 南北の `ridge_ns` を足す。妻壁（`gable_wall`）は破風として段差を塞ぐ。
         */
        bool manor_irimoya{ false };
        int ridge_ns{ -1 }; //!< 棟が南北へ通るマス用（入母屋だけ）
        int roof_top{ -1 }; //!< 段の上限に達したマスの平らな大棟（入母屋だけ）
        /*!
         * @brief **陸屋根の崩落**（デザイン12・2026-08-20。廃洋館）。2×2 マスの塊で抜く。
         * @details 表が茎名を書いた町でだけ集める。空なら陸屋根は無傷のまま。
         */
        std::vector<int> roof_scar;
        /*!
         * @brief **棟ごとの材**（デザイン9・2026-08-19。香霖堂の店舗・土蔵・渡り廊下）。
         *
         * @details 1 つの壁の塊を 3 つに読み分けた（`TownPlan::wings`）ので、材も
         * 棟ごとに引く。0 番は**主**＝これまでどおり `style.suffix` で引いたものと
         * 1 個も違わない（割っていない町では 0 番しか使われない）。
         * **無い棟の材は主へ落ちる**ので、旗だけ立った環境でも絵は出る。
         */
        struct ManorWingSet {
            std::vector<int> wall[9];
            std::vector<int> slope[4];
            std::vector<int> eave[4];
            //! **切妻の斜面と軒**（デザイン11・2026-08-20）。N / S の 2 姿勢だけ持つ。
            std::vector<int> gable_slope[2];
            std::vector<int> gable_eave[2];
            int gable[4]{ -1, -1, -1, -1 };
            int ridge{ -1 };
            int ridge_ns{ -1 };
            int roof_top{ -1 };
        };
        ManorWingSet manor_wing[3];
        /*! @} */
        //! 屋敷の脇の 1 つ（デザイン9 は桜・デザイン10 は鐘楼）と、中庭のガラクタ。
        int corner_prop{ -1 };
        //! 越屋根（デザイン15。煙出し。無ければ -1 ＝載せない）。
        int roof_vent{ -1 };
        std::vector<int> court_props;
        //! **1 マスだけの塀**の読み替え（デザイン10。命蓮寺の千体地蔵）。
        std::vector<int> lone_wall;
        //! **2×2 マスちょうどの塊**の読み替え（デザイン13。守矢神社の御柱）。
        std::vector<int> quad_wall;
        //! 入口の前に列で立てるもの（デザイン10。{terrain_id, 変種の集合, 届くマス数}）。
        struct FlankRow {
            int terrain_id;
            std::vector<int> variants;
            int reach;
        };
        std::vector<FlankRow> entrance_flanks;
        /*!
         * @brief **地形の絵の差し替え**（デザイン14・2026-08-20。彼岸の奈落）。
         * @details 共通の対応表に行の無い地形（`DARK_PIT`）を、その町でだけ絵にする。
         * 当たったマスは**意匠の草も街路樹も撒かない**——そこは表が持っているマスである。
         */
        struct TerrainPropRow {
            int terrain_id;
            std::vector<int> ground;
            std::vector<int> prop;
            //! **隣が違う地形の辺にだけ**立てる壁（N / S / W / E）。-1 なら立てない。
            int wall[4]{ -1, -1, -1, -1 };
            //! そのマスから煙を立てる高さ（マス。0 なら立てない。デザイン15）。
            float smoke{ 0.f };
        };
        std::vector<TerrainPropRow> terrain_props;
        //! **竹林**（デザイン8。木のマスに立てる素材と、木かどうかを見る terrain id）。
        std::vector<int> grove;
        int tree_terrain_id{ 0 };
        //! 参道の大鳥居の半身（デザイン6。torii_gates を持つ町だけ。無ければ塀の欠片のまま）。
        int torii_l{ -1 };
        int torii_r{ -1 };
        /*!
         * @name 配管（デザイン7 その2・2026-08-19。河童のバザー）
         * @details 本管は行と列に走らせるので**マスいっぱい**の型で、隣と繋がって線になる。
         * 壁の管は「屋敷が何側か」の 4 姿勢。立ち上がりは横引きと寸法を揃えてある。
         * @{
         */
        std::vector<int> pipe_run[2]; //!< 0 = 南北 / 1 = 東西
        std::vector<int> pipe_cross;
        std::vector<int> pipe_wall[4]; //!< N / S / W / E（屋敷のある側）
        std::vector<int> pipe_riser[4];
        /*! @} */
        /*!
         * @name 辺境の地の作り替え
         * @{
         */
        //! `yard_common`（行に当たらない門つき敷地、および `yards` の乱択の元）。
        std::vector<YardPropRuntime> yard_common;
        //! 草地（`Turf`）へ撒く小物（`turf_props`）。
        std::vector<TurfPropRuntime> turf_props;
        int path_edge{ -1 }; //!< 道の縁の草（`path_edge`）
        int gate_flame{ -1 }; //!< 門の灯（`gate_flame`。街灯の火と同じ流儀）
        /*!
         * @brief 小川（`brook`）のマスの向き。添字は `gy * width + gx`。
         * 0=南北 / 1=東西 / 2=bend_ne / 3=bend_nw / 4=bend_se / 5=bend_sw / 0xFF=小川でない。
         */
        std::vector<std::uint8_t> brook_kind;
        int brook_ns{ -1 };
        int brook_ew{ -1 };
        int brook_bend[4]{ -1, -1, -1, -1 }; //!< ne / nw / se / sw
        int brook_plank_ns{ -1 };
        int brook_plank_ew{ -1 };
        int brook_culvert{ -1 };
        //! 堀を渡る板の橋（自動。**町の表に `suffix` があるときだけ** `styled()` で引く）。
        int bridge_ns{ -1 };
        int bridge_ew{ -1 };
        /*! @} */
        /*!
         * @name 辺境の地の作り替え第 2 段（〜7.3。表を書いた
         * 町だけで効く）
         * @{
         */
        std::vector<ZoneRuntime> zones; //!< マスの矩形の区画（`zones`）
        std::vector<TownFeatureRuntime> features; //!< 区画に属さない 1 点（町の節直下の `features`）
        /*! @} */
    } townset;
    const bool use_town = use_lib && (town != nullptr) && town->valid();
    //! 町ごとの意匠。**地下では `town` が null** なので既定（辺境の地）を指す（罠 87）。
    const TownStyle &style = town_style_for(use_town ? town->identity.town_id : 0);
    //! 敷地ごとの目印（0 = 無し / 1 = 城 / 2 = `site_mark_prefab` の建物）。
    std::vector<std::uint8_t> site_mark;
    std::vector<int> site_mark_prefab;
    /*!
     * @name 辺境の地の作り替え — 敷地ごとの上書き
     * @details 表を書いていない町では全部が空/既定のままなので、House/Yard の描画は
     * 1 ボクセルも動かない。
     * @{
     */
    std::vector<SiteYardRuntime> site_yard; //!< 敷地ごとの前庭の計画
    std::vector<std::uint8_t> site_yard_active; //!< `site_yard[i]` が有効か
    std::vector<int> yard_rank; //!< 添字 `gy*width+gx`。前庭のマスの「建物に近い順」。-1 = 対象外
    std::vector<std::array<int, 9>> site_wall_override; //!< `sites` の `wing` が指す壁（-1 = 既定）
    std::vector<std::array<int, 9>> site_roof_override;
    std::vector<int> site_entrance_override; //!< -1 = 既定の玄関
    std::vector<std::uint8_t> site_wall_tint_has;
    std::vector<std::array<float, 3>> site_wall_tint_override;
    std::vector<std::uint8_t> site_roof_tint_has;
    std::vector<std::array<float, 3>> site_roof_tint_override;
    std::vector<int> site_storeys_override; //!< 0 = 既定のまま
    /*! @} */
    /*!
     * @name 辺境の地の作り替え第 2 段 — マスの矩形の区画
     * @details `townset.zones` が空のままの町（表を書いていない町）では
     * `zone_of` が全マス -1 のままなので、後段の描画は 1 ボクセルも動かない。
     * @{
     */
    std::vector<int> zone_of; //!< 添字 `gy*width+gx`。属する区画（`townset.zones` の添字）。-1 = 無し
    //! 区画の外周の柵の辺（`TownFence` と同じビット）。隣が `Path` の辺は立てない
    std::vector<std::uint8_t> zone_fence;
    //! そのマスは区画の外周（`rim_props` の対象）か。
    std::vector<std::uint8_t> zone_rim;
    /*! @} */
    //! **敷地を持たない入口**の目印（{gx, gy, 目印の索引, 門の索引, 目印の dx, dy}。
    //! 人里の龍神像の祠。門は表の `gate` 行が名指す（鳥居）。-1 なら従来の絵のまま。
    //! 目印は入口の**開いた側の隣**へ立てる（デザイン6 で北固定をやめた——博麗の
    //! 「神社の裏」は南へ開くので、北固定だと桜が館の壁の中に立つ）。
    std::vector<std::array<int, 6>> lone_marks;
    //! 祠の柱の欠片の読み替え（{gx, gy, 0=花壇 / 1=石灯籠}。下の走査で拾う）。
    std::vector<std::array<int, 3>> lone_props;
    /*!
     * **屋敷の入口の見通し**（デザイン9・2026-08-19。香霖堂）。1 = ここを隠す木は立てない。
     *
     * 門（`TownRole::Gate`）の前は `town_plan` が既に空けている（`clearing`）が、
     * **屋敷に穿たれた入口**（`manor_marks`）はそこを通らない——役割は歩けるマスのままで、
     * 印であることは地形 id を見て初めて分かるからである。香霖堂は森の中の店で、
     * 前庭の南に大木が並び、**店の口が 1 画素も見えなかった**（合成フレームで実測）。
     * 「入口はそこへ入れることが見えていなければ意味が無い」（罠 89 の直しの理屈）を
     * 屋敷の入口にも通す。**印を持たない町では 1 マスも立たない。**
     */
    std::vector<std::uint8_t> mark_clear;
    /*!
     * **入口の前に立てる列**（デザイン10。命蓮寺の赤い幟）。{gx, gy, プレハブ索引}。
     * 入口のマスから開いた向きへ進みながら、その**左右 1 マス**へ置く。
     */
    std::vector<std::array<int, 3>> flank_props;
    if (use_town) {
        /*
         * 町ごとの意匠（2026-08-09 に決めた「街ごとの雰囲気の変化」）。
         *
         * **骨組みは変えない。**引く名前に接尾辞を足すだけで、ライブラリに無ければ基の名前へ落ちる
         * （＝素材を作っていない町は辺境の地の見た目のまま動く）。どの町がどの接尾辞かは
         * `town_plan.cpp` の表が持つ（`TownStyle`）。
         */
        const auto styled = [library, &style](const std::string &stem) {
            if (style.suffix[0] != '\0') {
                const int index = library->find(stem + style.suffix);
                if (index >= 0) {
                    return index;
                }
            }
            return library->find(stem);
        };
        //! 変種のある素材は「名前 + 連番」で集める。**無ければ黙って短くなるだけ**。
        const auto gather_into = [library](const std::string &stem, std::vector<int> &into) {
            for (int i = 1; i <= 10; ++i) {
                char tail[8]{};
                std::snprintf(tail, sizeof(tail), "_%02d", i);
                const int index = library->find(stem + tail);
                if (index >= 0) {
                    into.push_back(index);
                }
            }
        };
        //! 意匠つきを先に集め、1 枚も無ければ基の名前で集め直す（**混ぜない**）。
        const auto gather = [&gather_into, &style](const std::string &stem, std::vector<int> &into) {
            if (style.suffix[0] != '\0') {
                gather_into(stem + style.suffix, into);
                if (!into.empty()) {
                    return;
                }
            }
            gather_into(stem, into);
        };
        /*
         * **棟ごとの材**（デザイン9・2026-08-19。香霖堂）。棟の接尾辞 → 町の接尾辞 →
         * 基の名前の順に落ちる。棟の接尾辞が空なら**町の意匠そのまま**なので、
         * 割っていない町の絵は 1 ボクセルも動かない。
         */
        const auto gather_wing = [&gather_into, &style](const std::string &stem, const char *wing,
                                     std::vector<int> &into) {
            if ((wing != nullptr) && (wing[0] != '\0')) {
                gather_into(stem + wing, into);
                if (!into.empty()) {
                    return;
                }
            }
            if (style.suffix[0] != '\0') {
                gather_into(stem + style.suffix, into);
                if (!into.empty()) {
                    return;
                }
            }
            gather_into(stem, into);
        };
        /*
         * **単数でも変種でも拾う**（デザイン11・2026-08-20）。切妻の屋根は意匠ごとに
         * 登録の仕方が割れている——博麗（`_hak`）は 1 個、霧雨（`_kir`）は変種 2 枚。
         * 単数で引く手段しか無かったので**霧雨の切妻が 1 個も当たらず**、屋根が
         * ひさし出しに落ちていた。棟 → 町 → 基の順に落ちるのは他の口と同じ。
         */
        const auto gather_any = [library, &gather_into, &style](const std::string &stem,
                                    const char *wing, std::vector<int> &into) {
            const char *const tries[3] = { wing, style.suffix, "" };
            for (const char *const sfx : tries) {
                if (sfx == nullptr) {
                    continue;
                }
                const std::string full = stem + sfx;
                gather_into(full, into);
                if (!into.empty()) {
                    return;
                }
                const int single = library->find(full);
                if (single >= 0) {
                    into.push_back(single);
                    return;
                }
            }
        };
        const auto styled_wing = [library, &style](const std::string &stem, const char *wing) {
            if ((wing != nullptr) && (wing[0] != '\0')) {
                const int index = library->find(stem + wing);
                if (index >= 0) {
                    return index;
                }
            }
            if (style.suffix[0] != '\0') {
                const int index = library->find(stem + style.suffix);
                if (index >= 0) {
                    return index;
                }
            }
            return library->find(stem);
        };

        static const char *const kSliceNames[9] = { "nw", "n", "ne", "w", "mid", "e", "sw", "s", "se" };
        for (int i = 0; i < 9; ++i) {
            townset.wall[i] = styled(std::string("house_wall_") + kSliceNames[i]);
            townset.roof[i] = styled(std::string("house_roof_") + kSliceNames[i]);
        }
        for (int i = 0; i < 9; ++i) {
            townset.castle_keep[i] = library->find(std::string("castle_keep_") + kSliceNames[i]);
        }
        static const char *const kCastleWallNames[4] = { "castle_wall_n", "castle_wall_s",
            "castle_wall_w", "castle_wall_e" };
        for (int i = 0; i < 4; ++i) {
            townset.castle_wall[i] = library->find(kCastleWallNames[i]);
        }
        townset.gate_pier = library->find("gate_pier");
        townset.gate_lintel = library->find("gate_lintel");
        townset.gate_lintel_ew = library->find("gate_lintel_ew");
        static const char *const kFenceNames[4] = { "fence_n", "fence_s", "fence_w", "fence_e" };
        static const char *const kParapetNames[4] = { "parapet_n", "parapet_s", "parapet_w", "parapet_e" };
        for (int i = 0; i < 4; ++i) {
            townset.fence[i] = styled(kFenceNames[i]);
            /*
             * 胸壁は**その意匠が持っていれば載る**（モリバントだけ）。基の名前へは落とさない
             * ——辺境の地の丸太の塀に石の狭間が載ると、決着させた絵が変わってしまう。
             */
            townset.parapet[i] = (style.suffix[0] != '\0')
                ? library->find(std::string(kParapetNames[i]) + style.suffix)
                : -1;
        }
        //! 噴水は**意匠を持たない**（4 つの柱に囲まれた水マスという並びはその町にしか無い）。
        townset.fountain_pillar = library->find("fountain_pillar");
        //! 噴き上げは意匠つきがあればそちら（モリバントの `fountain_jet_mor`＝水滴が順に点いて動く）。
        townset.fountain_jet = styled("fountain_jet");
        gather("palisade", townset.palisade);
        gather("palisade_base", townset.palisade_base);
        //! 口の手前の低い岩棚（2026-08-27）。岩なので**意匠を持たない**（岩山と同じ扱い）。
        gather("crag_ledge", townset.crag_ledge);
        /*
         * 岩山。素の意匠は持たない（山は地形）が、**覆いの町**（岩天井）だけは
         * 刳り抜いた山の内壁として接尾辞つきを使う——ライブラリに無い意匠は従来どおり
         * 素の岩山のままなので、既存の 3 意匠は 1 ボクセルも動かない（townset の註記）。
         */
        static const char *const kCragTierNames[4] = { "crag_low", "crag_mid", "crag_high", "crag_peak" };
        for (int i = 0; i < 4; ++i) {
            gather(kCragTierNames[i], townset.crag[i]);
        }
        //! 岩天井（2026-08-18 その3）。名前は意匠の表が持ち、変種は地面と同じ流儀で集める。
        if ((style.cave_roof != nullptr) && (style.cave_roof[0] != '\0')) {
            gather_into(style.cave_roof, townset.cave_roof);
        }
        out.covered = !townset.cave_roof.empty();
        out.cave_min_z = (style.cave_min_z > 0.f) ? style.cave_min_z : 3.1f;
        //! 山塊（2026-08-18 その3）。5 段を変種つきで集める（無い段は素通り＝積まれない）。
        //! base は岩屑の最低段——ノイズの低い所も含めて全マスに敷き、平らな天面を消す。
        //! peak はそびえる頂（同「思い切りがたらないぞ」——蓋の上 7.7 マスまで届く）。
        if (out.covered && (style.cave_mound != nullptr) && (style.cave_mound[0] != '\0')) {
            static const char *const kMoundTiers[5] = { "_base", "_low", "_mid", "_high", "_peak" };
            for (int i = 0; i < 5; ++i) {
                gather_into(std::string(style.cave_mound) + kMoundTiers[i], townset.cave_mound[i]);
            }
        }
        //! 交差点の街灯（同 その3）。火は「茎 + _flame」。
        if (out.covered && (style.cross_lamp != nullptr) && (style.cross_lamp[0] != '\0')) {
            townset.cross_lamp = library->find(style.cross_lamp);
            townset.cross_lamp_flame = library->find(std::string(style.cross_lamp) + "_flame");
        }
        //! 影の直方体をここまで持ち上げる（蓋の厚み＋山塊の頭まで）。
        if (out.covered) {
            const float roof_z = (style.cave_roof_height > 0.f) ? style.cave_roof_height : 6.f;
            float crown = 0.75f;
            for (const auto &tier_set : townset.cave_mound) {
                for (const int index : tier_set) {
                    //! 山塊は蓋へ 0.5 マス埋め、狙いの高さへの伸縮（最大 1.45 倍）まで見ておく。
                    crown = std::max(crown, 0.5f + (library->entry(index).top_z * 1.45f));
                }
            }
            out.cave_top = roof_z + crown;
        } else {
            out.cave_top = 0.f;
        }
        /*
         * 高山植物は**意匠つきを先に見る**（デザイン15・2026-08-20）。偽天棚の岩は
         * 高山帯の稜線そのもので、そこに咲くのは駒草である（§4.5 ②「高山植物の女王」）。
         * `gather` はライブラリに接尾辞つきが無ければ基の名前へ落ちるので、**他の町の岩山は
         * 1 ボクセルも動かない**。
         */
        gather("alpine", townset.alpine);
        gather("ground_path", townset.path);
        gather("ground_turf", townset.turf);
        //! 道の向きで選ぶ轍（`ground_path_ew_<意匠>_NN` など。無ければ空＝従来どおり）。
        gather("ground_path_ew", townset.path_ew);
        gather("ground_path_ns", townset.path_ns);
        gather("ground_path_x", townset.path_x);
        for (const char *const name : { "water_deep", "water_shallow" }) {
            const int index = library->find(name);
            if (index >= 0) {
                townset.water.push_back(index);
            }
        }
        townset.water_deep = library->find("water_deep");
        //! 湖の霧（デザイン4）。高さは意匠の表から。マスは下の地形ループで積む。
        out.mist_height = (style.lake_mist_height > 0.f) ? style.lake_mist_height : 0.f;
        //! 「町の中の地面には草はなし」を守るために、**草地の素材を名前で覚えておく**（§9.4）。
        gather_into("ground_grass", townset.grass_ground);
        townset.street_tree = ((style.street_tree != nullptr) && (style.street_tree[0] != '\0'))
            ? library->find(style.street_tree)
            : -1;
        /*
         * **竹林**（デザイン8・2026-08-19。永遠亭の迷いの竹林）。町の木のマスで
         * 対応表の木の代わりに立てる素材。どのマスが木かは**対応表に聞く**——`TREE` の
         * terrain id を表から引いて覚えるだけで、コードは 96 という数を持たない。
         */
        if ((style.grove != nullptr) && (style.grove[0] != '\0')) {
            gather_into(style.grove, townset.grove);
        }
        if (!townset.grove.empty()) {
            townset.tree_terrain_id = library->terrain_id_for_key("TREE");
        }
        /*
         * **桜 1 本と中庭のガラクタ**（デザイン9・2026-08-19。香霖堂）。木は
         * `town_plan` が屋敷の南西の外角で 1 マス選んである。ガラクタは屋敷の中庭
         * （bbox の中の歩けるマス）へマスの種で撒く。**どちらも表が名前を書いた町だけ**。
         */
        //! **越屋根**（デザイン15。煙出し。敷地 1 件につき 1 つ、建物の真ん中の屋根へ）。
        townset.roof_vent = ((style.roof_vent != nullptr) && (style.roof_vent[0] != '\0'))
            ? library->find(style.roof_vent)
            : -1;
        townset.corner_prop = ((style.corner_prop != nullptr) && (style.corner_prop[0] != '\0'))
            ? library->find(style.corner_prop)
            : -1;
        if ((style.court_props != nullptr) && (style.court_props[0] != '\0')) {
            gather_into(style.court_props, townset.court_props);
        }
        /*
         * **1 マスだけの塀の読み替えと、入口の前の列**（デザイン10・2026-08-19。命蓮寺）。
         * どちらも表が名前を書いた町でだけ集める——ライブラリに無ければ集合が空のままで、
         * 置く側が丸ごと黙る（従来どおりの塀の欠片と、飾りなしの入口になる）。
         */
        if ((style.lone_wall_prop != nullptr) && (style.lone_wall_prop[0] != '\0')) {
            gather_into(style.lone_wall_prop, townset.lone_wall);
        }
        //! **2×2 マスの柱**（デザイン13。守矢神社の御柱）。同じ流儀で集める。
        if ((style.quad_wall_prop != nullptr) && (style.quad_wall_prop[0] != '\0')) {
            gather_into(style.quad_wall_prop, townset.quad_wall);
        }
        for (const EntranceFlank *row = style.entrance_flanks;
             (row != nullptr) && (row->terrain_key != nullptr); ++row) {
            const int id = library->terrain_id_for_key(row->terrain_key);
            if ((id <= 0) || (row->prefab == nullptr) || (row->prefab[0] == '\0')) {
                continue;
            }
            std::vector<int> variants;
            gather_into(row->prefab, variants);
            if (!variants.empty()) {
                townset.entrance_flanks.push_back(
                    { id, std::move(variants), (row->reach > 0) ? row->reach : 4 });
            }
        }
        /*
         * **地形の絵の差し替え**（デザイン14）。地形は key で書く（id は版で動く）ので、
         * 翻訳はライブラリに任せる——`landmarks` と同じ流儀である。**ライブラリに絵が 1 枚も無い行は
         * 入れない**（入れると「差し替える」とだけ決まって何も置かれないマスになる）。
         */
        for (const TerrainProp *row = style.terrain_props;
             (row != nullptr) && ((row->terrain_key != nullptr) || (row->terrain_id != 0)); ++row) {
            //! key で引けなければ**表が書いた数**をそのまま使う（共通の表に行の無い地形）。
            const int by_key = (row->terrain_key != nullptr)
                ? library->terrain_id_for_key(row->terrain_key)
                : 0;
            const int id = (by_key > 0) ? by_key : row->terrain_id;
            if (id <= 0) {
                continue;
            }
            //! **単数でも変種でも拾う**（`gather_any` と同じ理由。デザイン11 の穴）。
            const auto collect = [library, &gather_into](const char *stem, std::vector<int> &into) {
                if ((stem == nullptr) || (stem[0] == '\0')) {
                    return;
                }
                gather_into(stem, into);
                if (into.empty()) {
                    const int single = library->find(stem);
                    if (single >= 0) {
                        into.push_back(single);
                    }
                }
            };
            decltype(townset)::TerrainPropRow entry{};
            entry.terrain_id = id;
            entry.smoke = row->smoke;
            collect(row->ground, entry.ground);
            collect(row->prop, entry.prop);
            if ((row->wall != nullptr) && (row->wall[0] != '\0')) {
                static const char *const kSideTail[4] = { "_n", "_s", "_w", "_e" };
                for (int k = 0; k < 4; ++k) {
                    //! 姿勢は**茎と接尾辞の間**（`naraku_wall` + `_n` + `_hig`）。
                    //! ライブラリに意匠つきが無ければ素の名前へ落ちる（`styled` の流儀）。
                    entry.wall[k] = styled(std::string(row->wall) + kSideTail[k]);
                }
            }
            if (!entry.ground.empty() || !entry.prop.empty()) {
                townset.terrain_props.push_back(std::move(entry));
            }
        }
        //! **地表の霧**（デザイン8 その2）。マスの集合は要らない——空の雲の網を
        //! そのまま低い所へ敷くので、ここが渡すのは高さと濃さだけである。
        out.ground_mist_height = std::max(0.f, style.grove_mist_height);
        /*
         * 街灯も意匠で引く（2026-08-18。旧地獄街道の提灯掛け）。ライブラリに無い意匠は
         * 基の鉄の街灯へ落ちるので、既存の 3 意匠は 1 ボクセルも動かない。
         */
        townset.lamp_post = (style.lamp_step > 0) ? styled("lamp_post") : -1;
        townset.lamp_flame = (style.lamp_step > 0) ? styled("lamp_flame") : -1;
        /*
         * 窓の灯り。**意匠で替わる**（2026-08-18 に気づいた:「障子の中に光る枠（変愚の窓）が
         * 違和感。障子自体の白い部分を光らせることはできるか？」）。
         * 自発光はインスタンス単位なので**光る物は壁と別体**のままにして、その形を
         * 障子に合わせた（`house_light_jin`）。ライブラリに無い意匠は基の窓へ落ちる＝変愚は従来どおり。
         */
        townset.light = styled("house_light");
        /*
         * 見た目だけの玄関（2026-08-18 に決めた:「機能のない建物にも見た目としてだけ
         * 玄関をつけたい」）。**基の名前は作っていない**ので、意匠を持たない町では
         * `find` が -1 を返して**何も置かれない**（変愚は従来どおり）。
         */
        townset.entrance = styled("house_entrance");
        townset.ground = styled("yard_ground");
        townset.well = library->find("well");
        townset.cart = library->find("cart");
        townset.flowerbed = library->find("flowerbed");
        townset.haystack = library->find("haystack");
        townset.tower = styled("watchtower");
        /*
         * 門（2026-08-17。こう決めた——「門と看板を意匠から外す規則は変愚だけの話。
         * 幻想では替えていく」）。
         *
         * **変愚は従来どおり石のアーチ**——意匠が名前を持たなければ `arch_stone` へ落ちるので、
         * 既存の 3 意匠は 1 ボクセルも動かない。幻想蛮怒はデータが `arch_kido`（建物）と
         * `arch_noren`（店）を指す（第 3 案）。
         *
         * **看板は意匠で替えない**——`sign_*` は町ごとの表（`TownSign`）が terrain key で
         * 掛け替えるので、そちらへ書けば足りる（幻想蛮怒の漢字 1 文字の板もこの道で入る）。
         */
        const std::string gate_base = ((style.gate != nullptr) && (style.gate[0] != '\0'))
            ? std::string(style.gate)
            : std::string("arch_stone");
        townset.arch = library->find(gate_base);
        townset.arch_ew = library->find(gate_base + "_ew");
        //! 閉じた大門（2026-08-18）。どのマスに立つかは `town_plan` が選んである。
        townset.great_gate = ((style.great_gate != nullptr) && (style.great_gate[0] != '\0'))
            ? library->find(style.great_gate)
            : -1;
        /*
         * 配管（デザイン7 その2）。表が旗を立てた町でだけ集める——ライブラリに無い町では
         * 集合が空のままで、下の置き場が丸ごと黙る（素材と表の両方が揃った町だけ配管が走る）。
         */
        if (style.pipe_works) {
            gather("pipe_run_ns", townset.pipe_run[0]);
            gather("pipe_run_ew", townset.pipe_run[1]);
            gather("pipe_cross", townset.pipe_cross);
            static const char *const kPipeSides[4] = { "n", "s", "w", "e" };
            for (int i = 0; i < 4; ++i) {
                gather(std::string("pipe_wall_") + kPipeSides[i], townset.pipe_wall[i]);
                gather(std::string("pipe_riser_") + kPipeSides[i], townset.pipe_riser[i]);
            }
        }
        //! 石灯籠（祠の柱の欠片の角。`lone_props` を持つ町でしか置かれない）。
        townset.stone_lantern = library->find("stone_lantern");
        townset.stone_lantern_flame = library->find("stone_lantern_flame");
        townset.sign_plain = library->find("sign_plain");
        //! 看板の台（`sign_base`。テルモラ §9.13）。表に無い町は -1 のまま＝従来どおり。
        townset.sign_base = ((style.sign_base != nullptr) && (style.sign_base[0] != '\0'))
            ? library->find(style.sign_base)
            : -1;
        //! 参道の大鳥居（デザイン6）。対の読みは town_plan（(1b)）が済ませてある。
        if (!town->torii_gates.empty() && (style.torii_gate != nullptr)
            && (style.torii_gate[0] != '\0')) {
            townset.torii_l = library->find(std::string(style.torii_gate) + "_l");
            townset.torii_r = library->find(std::string(style.torii_gate) + "_r");
        }
        //! 塀のマスの絵の差し替え（デザイン4。紅魔館の氷壁→氷塊）。変種は地面と同じ流儀。
        for (const RampartOverride *over = style.rampart_overrides;
             (over != nullptr) && (over->terrain_id != 0); ++over) {
            if ((over->prefab == nullptr) || (over->prefab[0] == '\0')) {
                continue;
            }
            std::vector<int> variants;
            gather_into(over->prefab, variants);
            if (!variants.empty()) {
                townset.rampart_overrides.emplace_back(over->terrain_id, std::move(variants));
            }
        }
        /*
         * 屋敷（デザイン4 第 2 段）。表がマス数を書いた町でだけ `TownRole::Manor` が立つ。
         * 素材は接尾辞で引く——ライブラリに無い町では読みだけ立って絵は塀に落ちる（下の case）。
         */
        if (!town->manors.empty()) {
            for (int i = 0; i < 9; ++i) {
                gather(std::string("manor_wall_") + kSliceNames[i], townset.manor_wall[i]);
                townset.manor_roof[i] = styled(std::string("manor_roof_") + kSliceNames[i]);
            }
            static const char *const kInnerNames[4] = {
                "manor_innerwall_n", "manor_innerwall_s", "manor_innerwall_w", "manor_innerwall_e"
            };
            for (int i = 0; i < 4; ++i) {
                townset.manor_innerwall[i] = styled(kInnerNames[i]);
            }
            //! 屋内の床（板張り。デザイン5）。素の名前はライブラリに無いので koma だけに効く。
            townset.manor_floor = styled("manor_floor");
            /*
             * 切妻の屋根（デザイン6・2026-08-19。博麗神社）。斜面の 2 姿勢が揃った町だけ
             * 切妻モードに入る——ライブラリに無い意匠（紅魔館）は従来のひさし出しのまま。
             */
            if (style.manor_roof_gable) {
                gather_any("manor_roofslope_n", nullptr, townset.gable_slope[0]);
                gather_any("manor_roofslope_s", nullptr, townset.gable_slope[1]);
                gather_any("manor_roofeave_n", nullptr, townset.gable_eave[0]);
                gather_any("manor_roofeave_s", nullptr, townset.gable_eave[1]);
                townset.gable_ridge = styled("manor_ridge");
                static const char *const kGableNames[4] = {
                    "manor_gable_n", "manor_gable_s", "manor_gable_w", "manor_gable_e"
                };
                for (int i = 0; i < 4; ++i) {
                    townset.gable_wall[i] = styled(kGableNames[i]);
                }
                townset.manor_gable
                    = !townset.gable_slope[0].empty() && !townset.gable_slope[1].empty();
            }
            /*
             * 片流れの屋根（デザイン7・2026-08-19。河童のバザー）。斜面は 4 姿勢で、
             * 軒の向きは「外接矩形の中心から遠ざかる向き」——**帯の外へ軒が落ち、
             * 中庭へ高い面が向く**。切妻と同じ妻壁（`manor_gable_*`）を段の埋めに使う。
             */
            if (style.manor_roof_shed && !townset.manor_gable) {
                static const char *const kShedSides[4] = { "n", "s", "w", "e" };
                for (int i = 0; i < 4; ++i) {
                    gather(std::string("manor_roofslope_") + kShedSides[i], townset.shed_slope[i]);
                    gather(std::string("manor_roofeave_") + kShedSides[i], townset.shed_eave[i]);
                    townset.gable_wall[i] = styled(std::string("manor_gable_") + kShedSides[i]);
                }
                townset.manor_shed = !townset.shed_slope[0].empty() && !townset.shed_slope[1].empty()
                    && !townset.shed_slope[2].empty() && !townset.shed_slope[3].empty();
            }
            /*
             * 入母屋の屋根（デザイン8・2026-08-19。永遠亭）。斜面と軒は片流れと同じ
             * 4 姿勢の変種を借り、棟は**東西と南北の 2 本**を持つ（矩形でない平面では
             * 棟の向きが所によって変わる）。片流れとは両立しない（あちらが勝つ）。
             */
            if (style.manor_roof_irimoya && !townset.manor_gable && !townset.manor_shed) {
                static const char *const kSides[4] = { "n", "s", "w", "e" };
                for (int i = 0; i < 4; ++i) {
                    gather(std::string("manor_roofslope_") + kSides[i], townset.shed_slope[i]);
                    gather(std::string("manor_roofeave_") + kSides[i], townset.shed_eave[i]);
                    townset.gable_wall[i] = styled(std::string("manor_gable_") + kSides[i]);
                }
                townset.gable_ridge = styled("manor_ridge");
                townset.ridge_ns = styled("manor_ridge_ns");
                townset.roof_top = styled("manor_rooftop");
                /*
                 * **崩落**（デザイン12・2026-08-20。廃洋館の抜けた屋根）。表が茎名を
                 * 書いた町でだけ集める——ライブラリに無ければ集合が空のままで、下の置き場が
                 * 丸ごと黙る（陸屋根は無傷のまま＝既存の 3 町は 1 ボクセルも動かない）。
                 */
                if ((style.manor_roof_scar != nullptr) && (style.manor_roof_scar[0] != '\0')) {
                    gather_into(style.manor_roof_scar, townset.roof_scar);
                }
                townset.manor_irimoya = !townset.shed_slope[0].empty()
                    && !townset.shed_slope[1].empty() && !townset.shed_slope[2].empty()
                    && !townset.shed_slope[3].empty();
            }
            /*
             * **棟ごとの材**（デザイン9・2026-08-19。香霖堂の店舗・土蔵・渡り廊下）。
             * 0 番（主）は上で集めたものと同じ名前で引き直すだけなので、割っていない
             * 町では 1 個も変わらない。**壁は棟が割れていなくても引く**——置く側は
             * 常にこの表から引くようにして、分岐を 1 つに保つ。
             */
            {
                const char *const kWings[3] = { nullptr, style.manor_annex, style.manor_corridor };
                static const char *const kSides[4] = { "n", "s", "w", "e" };
                for (int wing = 0; wing < 3; ++wing) {
                    auto &set = townset.manor_wing[wing];
                    for (int i = 0; i < 9; ++i) {
                        gather_wing(std::string("manor_wall_") + kSliceNames[i], kWings[wing],
                            set.wall[i]);
                    }
                    /*
                     * **切妻も棟ごとに引く**（デザイン11・2026-08-20。魔法の森の 2 軒）。
                     * 初版（デザイン9）は入母屋の香霖堂しか棟を割らなかったので、
                     * 屋根は入母屋のときだけ集めていた。**別々の塊に別々の材**を当てる
                     * ようになると、切妻の町でも屋根が主の材のまま残ってしまう
                     * ——壁だけ白い洋館で屋根が店の赤錆、という継ぎ接ぎになる。
                     */
                    if (townset.manor_gable) {
                        gather_any("manor_roofslope_n", kWings[wing], set.gable_slope[0]);
                        gather_any("manor_roofslope_s", kWings[wing], set.gable_slope[1]);
                        gather_any("manor_roofeave_n", kWings[wing], set.gable_eave[0]);
                        gather_any("manor_roofeave_s", kWings[wing], set.gable_eave[1]);
                        set.ridge = styled_wing("manor_ridge", kWings[wing]);
                        for (int i = 0; i < 4; ++i) {
                            set.gable[i]
                                = styled_wing(std::string("manor_gable_") + kSides[i], kWings[wing]);
                        }
                        continue;
                    }
                    if (!townset.manor_irimoya) {
                        continue;
                    }
                    for (int i = 0; i < 4; ++i) {
                        gather_wing(std::string("manor_roofslope_") + kSides[i], kWings[wing],
                            set.slope[i]);
                        gather_wing(std::string("manor_roofeave_") + kSides[i], kWings[wing],
                            set.eave[i]);
                        set.gable[i] = styled_wing(std::string("manor_gable_") + kSides[i], kWings[wing]);
                    }
                    set.ridge = styled_wing("manor_ridge", kWings[wing]);
                    set.ridge_ns = styled_wing("manor_ridge_ns", kWings[wing]);
                    set.roof_top = styled_wing("manor_rooftop", kWings[wing]);
                }
            }
            //! 屋内の印（螺旋階段・本の机）。id への翻訳はライブラリに任せる（看板と同じ流儀）。
            for (const TownLandmark *row = style.manor_marks;
                 (row != nullptr) && (row->terrain_key != nullptr); ++row) {
                const int id = library->terrain_id_for_key(row->terrain_key);
                const int index = library->find(row->kind);
                if ((id > 0) && (index >= 0)) {
                    townset.manor_marks.emplace_back(id, index);
                }
            }
            /*
             * 塔の置き場（「中に入れるスペースの上に巨大な時計塔、その左右に少し
             * 小ぶりな塔。屋敷のりょうよくにも大きな塔を」）。
             * 時計塔は**迷宮の入口（Stairs）の真北の屋敷マス**——「中に入れるスペース」は
             * 入口の凹みで、その上に建てるのが指示の読みである。小塔は左右 5 マス、
             * 翼塔は外接矩形の東西端の内側 2 マス。**屋敷マスでない置き場は捨てる**。
             */
            const int clock = library->find("tower_clock" + std::string(style.suffix));
            const int minor = library->find("tower_minor" + std::string(style.suffix));
            const int wing = library->find("tower_wing" + std::string(style.suffix));
            const auto &box = town->manors.front();
            int stair_x = -1;
            int stair_y = -1;
            /*
             * 1 周目は **ENTRANCE の地形 id**（迷宮の入口そのもの）。Stairs 役割で探すと、
             * クエストの入口（合成フレームは受注中の姿で読む＝隠し通路の `s` も階段）が
             * 先にヒットして時計塔が東の外れへ立った（実測 (109,34)）。id が見えないマス
             * しか無いときだけ 2 周目（Stairs 役割）へ落ちる。
             */
            const int entrance_id = library->terrain_id_for_key("ENTRANCE");
            if ((entrance_id > 0) && (memory != nullptr)) {
                for (int gy = box[1]; (gy <= box[3]) && (stair_x < 0); ++gy) {
                    for (int gx = box[0]; gx <= box[2]; ++gx) {
                        if ((memory->id_at(gx, gy) == static_cast<std::uint16_t>(entrance_id))
                            && (town->role_at(gx, gy) != TownRole::Manor)) {
                            stair_x = gx;
                            stair_y = gy;
                            break;
                        }
                    }
                }
            }
            for (int gy = box[1]; (gy <= box[3]) && (stair_x < 0); ++gy) {
                for (int gx = box[0]; gx <= box[2]; ++gx) {
                    if ((meaning.role_at(gx, gy) == CellRole::Stairs)
                        && (town->role_at(gx, gy) != TownRole::Manor)) {
                        stair_x = gx;
                        stair_y = gy;
                        break;
                    }
                }
            }
            /*
             * 時計塔は**参道の正面**（気づいたこと デザイン4:「時計塔の位置はレンガの
             * 参道の正面に」）——大門の開口の中心線の上、屋内（彫り込み）のマスの重心の
             * **蓋の上**に立てる。屋内マスにも置けるようにする（蓋の place 側が照合する）。
             * 大門が無い町は迷宮入口の真北（従来）へ落ちる。
             */
            const auto add_tower = [&](int gx, int gy, int prefab) {
                if (prefab < 0) {
                    return;
                }
                const bool inside_box
                    = (gx >= box[0]) && (gx <= box[2]) && (gy >= box[1]) && (gy <= box[3]);
                if ((town->role_at(gx, gy) == TownRole::Manor) || inside_box) {
                    townset.manor_towers.push_back({ gx, gy, prefab });
                }
            };
            int face_x = -1;
            int face_y = -1;
            if (town->great_gates.size() >= 2) {
                face_x = (town->great_gates[0][0] + town->great_gates[1][0]) / 2;
                int sum = 0;
                int n = 0;
                for (int gy = box[1]; gy <= box[3]; ++gy) {
                    if (town->role_at(face_x, gy) != TownRole::Manor) {
                        sum += gy;
                        ++n;
                    }
                }
                if (n > 0) {
                    face_y = sum / n;
                }
            }
            if ((face_x < 0) || (face_y < 0)) {
                face_x = stair_x;
                face_y = stair_y - 1;
            }
            if (face_x >= 0) {
                add_tower(face_x, face_y, clock);
                add_tower(face_x - 5, face_y, minor);
                add_tower(face_x + 5, face_y, minor);
            }
            const int mid_y = (box[1] + box[3]) / 2;
            add_tower(box[0] + 2, mid_y, wing);
            add_tower(box[2] - 2, mid_y, wing);
            //! 検算（絵を見る前に塔の数と置き場を疑えるように）。読み直しのときだけ出る。
            std::fprintf(stderr,
                "[hd2d] 屋敷: bbox=(%d,%d)-(%d,%d) 階段=(%d,%d) 塔=%zu 本（clock=%d minor=%d wing=%d）\n",
                box[0], box[1], box[2], box[3], stair_x, stair_y, townset.manor_towers.size(), clock,
                minor, wing);
        }
        /*
         * 建物の看板（P10 第 4 期。2026-08-10 に気づいた「モリバンドの看板で図書館と
         * 宿屋が逆」）。**terrain key → id の翻訳はライブラリに任せる**（目印の建物と同じ流儀）。
         * ライブラリに絵が無い行は**入れない**——入れると `-1` を掛けて板が消える。
         */
        for (const TownSign *row = town_signs_for(town->identity.town_id);
             (row != nullptr) && (row->terrain_key != nullptr); ++row) {
            const int id = library->terrain_id_for_key(row->terrain_key);
            const int index = library->find(row->sign);
            if ((id > 0) && (index >= 0)) {
                /*
                 * 門も表が名指せる（2026-08-17 の第 3 案。店は暖簾・建物は木戸）。
                 * **書いていない行は -1** ＝意匠の既定（変愚のべた書きの表はすべてこれ）。
                 */
                int gate = -1;
                int gate_ew = -1;
                if ((row->gate != nullptr) && (row->gate[0] != '\0')) {
                    gate = library->find(row->gate);
                    gate_ew = library->find(std::string(row->gate) + "_ew");
                }
                townset.building_signs.push_back({ id, index, gate, gate_ew });
            }
        }
        //! 草は意匠で差し替わる（テルモラは背の高いススキ＝`grass_tel`）。
        townset.tuft = styled("grass");
        //! **意匠つきがあれば使う**（OUTPOST_TOWN_DESIGN §5 の 9。無ければ従来どおり基の名前）。
        townset.landmark[1] = styled("shop");
        townset.landmark[2] = styled("mill");
        /*
         * **辺境の地の作り替え**。表に書いていない町では
         * `style.yard_common` などが `nullptr`/空のままなので、ここは全部 -1 か空で終わり
         * ——後段の描画は 1 ボクセルも動かない。
         */
        {
            const auto resolve_yard_props = [library](const YardProp *rows) {
                std::vector<YardPropRuntime> out_props;
                for (const YardProp *row = rows; (row != nullptr) && (row->prefab != nullptr); ++row) {
                    YardPropRuntime rt{};
                    rt.prefab = library->find(row->prefab);
                    if (rt.prefab < 0) {
                        continue; //!< プレハブが無い名前は黙って飛ばす
                    }
                    rt.flame = ((row->flame != nullptr) && (row->flame[0] != '\0'))
                        ? library->find(row->flame)
                        : -1;
                    rt.glow[0] = row->glow[0];
                    rt.glow[1] = row->glow[1];
                    rt.glow[2] = row->glow[2];
                    rt.always = row->always;
                    rt.center = row->center;
                    out_props.push_back(rt);
                }
                return out_props;
            };
            townset.yard_common = resolve_yard_props(style.yard_common);
            //! 草地の小物（`turf_props`）。変種があれば集め、無ければ単数を拾う。
            for (const TurfProp *row = style.turf_props; (row != nullptr) && (row->prefab != nullptr); ++row) {
                TurfPropRuntime rt{};
                gather_into(row->prefab, rt.variants);
                if (rt.variants.empty()) {
                    const int single = library->find(row->prefab);
                    if (single >= 0) {
                        rt.variants.push_back(single);
                    }
                }
                if (!rt.variants.empty()) {
                    rt.chance = row->chance;
                    townset.turf_props.push_back(rt);
                }
            }
            townset.path_edge = ((style.path_edge != nullptr) && (style.path_edge[0] != '\0'))
                ? library->find(style.path_edge)
                : -1;
            townset.gate_flame = ((style.gate_flame != nullptr) && (style.gate_flame[0] != '\0'))
                ? library->find(style.gate_flame)
                : -1;
            //! 小川（`brook`）。役割で絵を選ぶので、名前だけ先に引いておく。
            townset.brook_ns = styled("brook_ns");
            townset.brook_ew = styled("brook_ew");
            townset.brook_bend[0] = styled("brook_bend_ne");
            townset.brook_bend[1] = styled("brook_bend_nw");
            townset.brook_bend[2] = styled("brook_bend_se");
            townset.brook_bend[3] = styled("brook_bend_sw");
            townset.brook_plank_ns = styled("brook_plank_ns");
            townset.brook_plank_ew = styled("brook_plank_ew");
            townset.brook_culvert = styled("brook_culvert");
            /*
             * **橋は自動**（同 §5 の橋）。堀を渡る歩けるマスに絵を出すだけの飾りなので、
             * 意匠が接尾辞を持つ町だけで試す——`suffix` が空の町（意匠を持たない町）まで
             * 基の名前へ落ちて橋が生えると、辺境の作り替え以外の町の絵が動く。
             */
            if (style.suffix[0] != '\0') {
                townset.bridge_ns = styled("ground_bridge_ns");
                townset.bridge_ew = styled("ground_bridge_ew");
            }
            /*
             * 折れ線を歩き、マスごとの向き（`brook_kind`）を前計算する
             * （OUTPOST_TOWN_DESIGN §5「軸に沿う線分だけ」）。
             */
            if (use_town && (style.brook_points != nullptr) && (style.brook_point_count >= 2)) {
                std::vector<std::array<int, 2>> path;
                const auto push_cell = [&path](int x, int y) {
                    if (path.empty() || (path.back()[0] != x) || (path.back()[1] != y)) {
                        path.push_back({ x, y });
                    }
                };
                for (int v = 0; (v + 1) < style.brook_point_count; ++v) {
                    const int x0 = style.brook_points[v][0];
                    const int y0 = style.brook_points[v][1];
                    const int x1 = style.brook_points[v + 1][0];
                    const int y1 = style.brook_points[v + 1][1];
                    if (x0 == x1) {
                        const int step = (y1 >= y0) ? 1 : -1;
                        for (int y = y0; y != (y1 + step); y += step) {
                            push_cell(x0, y);
                        }
                    } else if (y0 == y1) {
                        const int step = (x1 >= x0) ? 1 : -1;
                        for (int x = x0; x != (x1 + step); x += step) {
                            push_cell(x, y0);
                        }
                    }
                    //! 軸に沿わない線分は無視する（設計書「軸に沿う線分だけ」）。
                }
                if (path.size() >= 2) {
                    townset.brook_kind.assign(static_cast<std::size_t>(town->width)
                            * static_cast<std::size_t>(town->height),
                        0xFFu);
                    const auto dir_bit = [](int ox, int oy) -> int {
                        if ((ox == 0) && (oy == -1)) {
                            return 1; // N
                        }
                        if ((ox == 0) && (oy == 1)) {
                            return 2; // S
                        }
                        if ((ox == -1) && (oy == 0)) {
                            return 4; // W
                        }
                        if ((ox == 1) && (oy == 0)) {
                            return 8; // E
                        }
                        return 0;
                    };
                    const auto axis_both = [](int ox, int) -> int { return (ox != 0) ? (4 | 8) : (1 | 2); };
                    for (std::size_t i = 0; i < path.size(); ++i) {
                        const int gx = path[i][0];
                        const int gy = path[i][1];
                        if ((gx < 0) || (gy < 0) || (gx >= town->width) || (gy >= town->height)) {
                            continue;
                        }
                        int dirs = 0;
                        if (i > 0) {
                            dirs |= dir_bit(path[i - 1][0] - gx, path[i - 1][1] - gy);
                        } else if (path.size() > 1) {
                            dirs |= axis_both(path[1][0] - gx, path[1][1] - gy);
                        }
                        if ((i + 1) < path.size()) {
                            dirs |= dir_bit(path[i + 1][0] - gx, path[i + 1][1] - gy);
                        } else if (i > 0) {
                            dirs |= axis_both(path[i - 1][0] - gx, path[i - 1][1] - gy);
                        }
                        std::uint8_t kind = 0xFFu;
                        if (dirs == (1 | 2)) {
                            kind = 0; // NS
                        } else if (dirs == (4 | 8)) {
                            kind = 1; // EW
                        } else if (dirs == (1 | 8)) {
                            kind = 2; // bend NE
                        } else if (dirs == (1 | 4)) {
                            kind = 3; // bend NW
                        } else if (dirs == (2 | 8)) {
                            kind = 4; // bend SE
                        } else if (dirs == (2 | 4)) {
                            kind = 5; // bend SW
                        }
                        if (kind != 0xFFu) {
                            townset.brook_kind[(static_cast<std::size_t>(gy)
                                                    * static_cast<std::size_t>(town->width))
                                + static_cast<std::size_t>(gx)]
                                = kind;
                        }
                    }
                }
            }
        }

        /*
         * **マスの矩形を区画として名指す**（〜7.3。辺境の地の
         * 作り替え第 2 段）。表に書いていない町では `style.zones` / `style.features` が
         * `nullptr` のままなので、`townset.zones` は空・`zone_of` は全マス -1 のまま
         * ——後段の描画は 1 ボクセルも動かない。
         */
        for (const TownZone *zrow = style.zones; (zrow != nullptr) && (zrow->x1 >= zrow->x0); ++zrow) {
            ZoneRuntime zone{};
            zone.x0 = zrow->x0;
            zone.y0 = zrow->y0;
            zone.x1 = zrow->x1;
            zone.y1 = zrow->y1;
            zone.apply_to = zrow->apply_to;
            if ((zrow->ground != nullptr) && (zrow->ground[0] != '\0')) {
                gather_into(zrow->ground, zone.ground);
                if (zone.ground.empty()) {
                    const int single = library->find(zrow->ground);
                    if (single >= 0) {
                        zone.ground.push_back(single);
                    }
                }
            }
            if ((zrow->ground != nullptr) && (zrow->ground[0] != '\0')) {
                //! 轍を道の向きで選ぶ（2026-09-05 に決めた「轍は 1 マスに囚われず長くうねる」）。
                gather_into(std::string(zrow->ground) + "_ew", zone.ground_ew);
                gather_into(std::string(zrow->ground) + "_ns", zone.ground_ns);
                gather_into(std::string(zrow->ground) + "_x", zone.ground_x);
                static constexpr const char *kEdgeSides[4] = { "_edge_n", "_edge_s", "_edge_w", "_edge_e" };
                for (int side = 0; side < 4; ++side) {
                    gather_into(std::string(zrow->ground) + kEdgeSides[side], zone.ground_edge[side]);
                }
            }
            if ((zrow->rim != nullptr) && (zrow->rim[0] != '\0')) {
                //! `<rim>_n/_s/_w/_e`。**そのまま引く名前**（意匠の接尾辞は掛けない。
                //! `wattle_fence_out` のように呼び出し側が完全な名前を書く）。
                static const char *const kRimSuffix[4] = { "_n", "_s", "_w", "_e" };
                for (int i = 0; i < 4; ++i) {
                    zone.rim[i] = library->find(std::string(zrow->rim) + kRimSuffix[i]);
                }
            }
            const auto resolve_zone_props
                = [library, &gather_into](const ZoneProp *rows, std::vector<ZonePropRuntime> &out_list) {
                      for (const ZoneProp *row = rows; (row != nullptr) && (row->prefab != nullptr); ++row) {
                          ZonePropRuntime entry{};
                          //! **変種（`_01..`）があれば集める**（2026-09-05。`apple_tree_out_01` の類が 1 本も出なかった）。
                          gather_into(row->prefab, entry.variants);
                          entry.prefab = library->find(row->prefab);
                          if ((entry.prefab < 0) && entry.variants.empty()) {
                              continue; //!< プレハブが無い名前は黙って飛ばす
                          }
                          entry.chance = row->chance;
                          entry.center = row->center;
                          if ((row->flame != nullptr) && (row->flame[0] != '\0')) {
                              entry.flame = library->find(row->flame);
                          }
                          entry.glow[0] = row->glow[0];
                          entry.glow[1] = row->glow[1];
                          entry.glow[2] = row->glow[2];
                          entry.always = row->always;
                          out_list.push_back(entry);
                      }
                  };
            resolve_zone_props(zrow->props, zone.props);
            resolve_zone_props(zrow->rim_props, zone.rim_props);
            for (const ZoneFeature *frow = zrow->features; (frow != nullptr) && (frow->at_x >= 0); ++frow) {
                const int idx = library->find(frow->prefab);
                if (idx < 0) {
                    continue;
                }
                ZoneFeatureRuntime zf{};
                zf.at_x = frow->at_x;
                zf.at_y = frow->at_y;
                zf.prefab = idx;
                zf.center = frow->center;
                if ((frow->flame != nullptr) && (frow->flame[0] != '\0')) {
                    zf.flame = library->find(frow->flame);
                }
                zf.glow[0] = frow->glow[0];
                zf.glow[1] = frow->glow[1];
                zf.glow[2] = frow->glow[2];
                zf.always = frow->always;
                zone.features.push_back(zf);
            }
            townset.zones.push_back(std::move(zone));
        }
        //! 区画に属さない 1 点（同 §7.3。門楼・大木・さらし台など）。
        for (const TownFeature *frow = style.features; (frow != nullptr) && (frow->at_x >= 0); ++frow) {
            const int idx = library->find(frow->prefab);
            if (idx < 0) {
                continue;
            }
            TownFeatureRuntime tf{};
            tf.at_x = frow->at_x;
            tf.at_y = frow->at_y;
            tf.prefab = idx;
            if ((frow->flame != nullptr) && (frow->flame[0] != '\0')) {
                tf.flame = library->find(frow->flame);
            }
            tf.glow[0] = frow->glow[0];
            tf.glow[1] = frow->glow[1];
            tf.glow[2] = frow->glow[2];
            tf.always = frow->always;
            townset.features.push_back(tf);
        }
        /*
         * 区画の幾何（対象のマス・外周の柵の辺）は**地形の細別（`memory`）に依らず**
         * 先に決まる（`TownPlan::role_at` は握手直後の意味づけだけで決まる）。水のマスの
         * 除外は地面を差し替える側（後段の `town_ground`）で行う——そちらは間近で見た
         * マスの規則を要るので、この場では判定できない。
         */
        zone_of.assign(
            static_cast<std::size_t>(town->width) * static_cast<std::size_t>(town->height), -1);
        zone_fence.assign(zone_of.size(), 0u);
        zone_rim.assign(zone_of.size(), 0u);
        if (!townset.zones.empty()) {
            const auto zone_role_ok = [](TownRole r, int apply_to) {
                switch (apply_to) {
                case 1: // "path"
                    return r == TownRole::Path;
                case 2: // "all"
                    return (r == TownRole::None) || (r == TownRole::Path) || (r == TownRole::Turf);
                default: // "turf"（既定）
                    return (r == TownRole::None) || (r == TownRole::Turf);
                }
            };
            for (std::size_t zi = 0; zi < townset.zones.size(); ++zi) {
                const ZoneRuntime &zone = townset.zones[zi];
                for (int gy = zone.y0; gy <= zone.y1; ++gy) {
                    for (int gx = zone.x0; gx <= zone.x1; ++gx) {
                        if ((gx < 0) || (gy < 0) || (gx >= town->width) || (gy >= town->height)) {
                            continue;
                        }
                        if (!zone_role_ok(town->role_at(gx, gy), zone.apply_to)) {
                            continue; //!< 敷地・門・塀・前庭など対象外の役割
                        }
                        zone_of[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(town->width))
                            + static_cast<std::size_t>(gx)]
                            = static_cast<int>(zi);
                    }
                }
            }
            //! N / S / W / E。`TownFence` と同じビット並び（1 / 2 / 4 / 8）。
            static constexpr int kZoneDx[4] = { 0, 0, -1, 1 };
            static constexpr int kZoneDy[4] = { -1, 1, 0, 0 };
            for (int gy = 0; gy < town->height; ++gy) {
                for (int gx = 0; gx < town->width; ++gx) {
                    const std::size_t idx = (static_cast<std::size_t>(gy)
                                                 * static_cast<std::size_t>(town->width))
                        + static_cast<std::size_t>(gx);
                    const int zi = zone_of[idx];
                    if (zi < 0) {
                        continue;
                    }
                    std::uint8_t bits = 0u;
                    bool rim = false;
                    for (int k = 0; k < 4; ++k) {
                        const int nx = gx + kZoneDx[k];
                        const int ny = gy + kZoneDy[k];
                        const bool in_bounds = (nx >= 0) && (ny >= 0) && (nx < town->width)
                            && (ny < town->height);
                        const bool same_zone = in_bounds
                            && (zone_of[(static_cast<std::size_t>(ny)
                                            * static_cast<std::size_t>(town->width))
                                    + static_cast<std::size_t>(nx)]
                                == zi);
                        if (same_zone) {
                            continue; //!< 対象のマス同士。辺は立たない
                        }
                        rim = true; //!< 矩形の外、または対象外のマスと接する＝外周
                        //! **隣が道の辺には立てない**（道が通る所は柵を開ける）。
                        const TownRole nrole = in_bounds ? town->role_at(nx, ny) : TownRole::None;
                        if (nrole != TownRole::Path) {
                            bits |= static_cast<std::uint8_t>(1u << k);
                        }
                    }
                    zone_fence[idx] = bits;
                    zone_rim[idx] = rim ? 1u : 0u;
                }
            }
        }

        /*
         * **目印の建物**（2026-08-09 に決めた。城・賢者の塔・トランプの塔）。
         *
         * どの敷地が城でどれが塔かは、**敷地の入口に立っている地形**で決まる。
         * 地形 id は版で動くので、意匠が持つのは key の文字列だけで、id への翻訳はライブラリに任せる
         * （`terrain_id_for_key`）。**敷地ごとに 1 度だけ**引いて、置く側は添字で見る。
         */
        site_mark.assign(town->sites.size(), 0u);
        site_mark_prefab.assign(town->sites.size(), -1);
        if ((style.landmarks != nullptr) && (memory != nullptr)) {
            for (std::size_t i = 0; i < town->sites.size(); ++i) {
                const TownSite &site = town->sites[i];
                if (!site.has_gate) {
                    continue;
                }
                const std::uint16_t id = memory->id_at(site.gate_x, site.gate_y);
                if (id == 0) {
                    continue; // まだ間近で見ていない入口。目印は建てない
                }
                for (const TownLandmark *row = style.landmarks; row->terrain_key != nullptr; ++row) {
                    if (library->terrain_id_for_key(row->terrain_key) != static_cast<int>(id)) {
                        continue;
                    }
                    if (std::strcmp(row->kind, "castle") == 0) {
                        site_mark[i] = 1u;
                    } else {
                        const int index = library->find(row->kind);
                        if (index >= 0) {
                            site_mark[i] = 2u;
                            site_mark_prefab[i] = index;
                        }
                    }
                    break;
                }
            }
        }
        /*
         * **前庭の小物を建物の意味で引く**（`yards`/`sites`。
         * 辺境の地の作り替え）。敷地ごとに 1 つの計画（`SiteYardRuntime`）を作り、
         * 前庭のマスは**建物に近い順**へ並べ替えて `yard_rank` に控える——マスの走査順は
         * ラスタ順なので、順位表を先に作らないと「近い順」が決定的にならない。
         *
         * **表を書いていない町ではここが 1 度も何も積まない**——`site_yard_active` が
         * 全部偽のままなので、後段の Yard/House は従来の乱択に落ちる。
         */
        site_yard.assign(town->sites.size(), SiteYardRuntime{});
        site_yard_active.assign(town->sites.size(), 0u);
        site_wall_override.assign(town->sites.size(), { -1, -1, -1, -1, -1, -1, -1, -1, -1 });
        site_roof_override.assign(town->sites.size(), { -1, -1, -1, -1, -1, -1, -1, -1, -1 });
        site_entrance_override.assign(town->sites.size(), -1);
        site_wall_tint_has.assign(town->sites.size(), 0u);
        site_wall_tint_override.assign(town->sites.size(), { 1.f, 1.f, 1.f });
        site_roof_tint_has.assign(town->sites.size(), 0u);
        site_roof_tint_override.assign(town->sites.size(), { 1.f, 1.f, 1.f });
        site_storeys_override.assign(town->sites.size(), 0);
        yard_rank.assign(
            static_cast<std::size_t>(town->width) * static_cast<std::size_t>(town->height), -1);
        const auto resolve_yard_props_owned = [library](const YardProp *rows) {
            std::vector<YardPropRuntime> out_props;
            for (const YardProp *row = rows; (row != nullptr) && (row->prefab != nullptr); ++row) {
                YardPropRuntime rt{};
                rt.prefab = library->find(row->prefab);
                if (rt.prefab < 0) {
                    continue; //!< プレハブが無い名前は黙って飛ばす
                }
                rt.flame = ((row->flame != nullptr) && (row->flame[0] != '\0'))
                    ? library->find(row->flame)
                    : -1;
                rt.glow[0] = row->glow[0];
                rt.glow[1] = row->glow[1];
                rt.glow[2] = row->glow[2];
                rt.always = row->always;
                rt.center = row->center;
                out_props.push_back(rt);
            }
            return out_props;
        };
        //! `yards`：門の地形 key が当たった敷地に計画を立てる。
        if ((style.yards != nullptr) && (memory != nullptr)) {
            for (std::size_t i = 0; i < town->sites.size(); ++i) {
                const TownSite &site = town->sites[i];
                if (!site.has_gate) {
                    continue;
                }
                const std::uint16_t id = memory->id_at(site.gate_x, site.gate_y);
                if (id == 0) {
                    continue;
                }
                for (const YardStyle *row = style.yards; row->terrain_key != nullptr; ++row) {
                    if (library->terrain_id_for_key(row->terrain_key) != static_cast<int>(id)) {
                        continue;
                    }
                    SiteYardRuntime plan{};
                    plan.props = resolve_yard_props_owned(row->props);
                    plan.fill = row->fill;
                    plan.use_common_fallback = true;
                    site_yard[i] = std::move(plan);
                    site_yard_active[i] = 1u;
                    if (row->wall_tint[0] >= 0.f) {
                        site_wall_tint_override[i] = { row->wall_tint[0], row->wall_tint[1], row->wall_tint[2] };
                        site_wall_tint_has[i] = 1u;
                    }
                    if (row->roof_tint[0] >= 0.f) {
                        site_roof_tint_override[i] = { row->roof_tint[0], row->roof_tint[1], row->roof_tint[2] };
                        site_roof_tint_has[i] = 1u;
                    }
                    if (row->storeys > 0) {
                        site_storeys_override[i] = row->storeys;
                    }
                    break;
                }
            }
        }
        //! `sites`：入口の無い敷地をマスで名指す。
        for (const SitePlot *row = style.sites; (row != nullptr) && (row->at_x >= 0); ++row) {
            const std::int16_t idx = town->site_at(row->at_x, row->at_y);
            if ((idx < 0) || (static_cast<std::size_t>(idx) >= town->sites.size())) {
                continue; //!< 座標が合わなければ黙って飛ばす
            }
            const std::size_t i = static_cast<std::size_t>(idx);
            if ((row->wing != nullptr) && (row->wing[0] != '\0')) {
                for (int k = 0; k < 9; ++k) {
                    site_wall_override[i][static_cast<std::size_t>(k)]
                        = styled_wing(std::string("house_wall_") + kSliceNames[k], row->wing);
                    site_roof_override[i][static_cast<std::size_t>(k)]
                        = styled_wing(std::string("house_roof_") + kSliceNames[k], row->wing);
                }
                site_entrance_override[i] = styled_wing("house_entrance", row->wing);
            }
            if ((row->landmark != nullptr) && (row->landmark[0] != '\0')) {
                const int index = library->find(row->landmark);
                if (index >= 0) {
                    site_mark[i] = 2u;
                    site_mark_prefab[i] = index;
                }
            }
            if (row->props != nullptr) {
                SiteYardRuntime plan{};
                plan.props = resolve_yard_props_owned(row->props);
                plan.fill = 0.f;
                plan.use_common_fallback = false;
                site_yard[i] = std::move(plan);
                site_yard_active[i] = 1u;
            }
        }
        //! ランク表。**前庭のマスを敷地ごとに集め、建物に近い順へ並べる。**
        for (std::size_t i = 0; i < town->sites.size(); ++i) {
            if (site_yard_active[i] == 0u) {
                continue;
            }
            const TownSite &site = town->sites[i];
            std::vector<std::pair<int, std::array<int, 2>>> ranked; // {距離, {gx,gy}}
            for (int gy = site.y0; gy <= site.y1; ++gy) {
                for (int gx = site.x0; gx <= site.x1; ++gx) {
                    if (town->role_at(gx, gy) != TownRole::Yard) {
                        continue;
                    }
                    if (town->site_at(gx, gy) != static_cast<std::int16_t>(i)) {
                        continue;
                    }
                    //! 門の隣とクリアリングは飛ばす（既存の流儀。ランクにも入れない）。
                    bool next_to_gate = (town->role_at(gx, gy - 1) == TownRole::Gate);
                    for (int k = 0; k < 4; ++k) {
                        const int nx = gx + ((k == 2) ? -1 : ((k == 3) ? 1 : 0));
                        const int ny = gy + ((k == 0) ? -1 : ((k == 1) ? 1 : 0));
                        next_to_gate = next_to_gate
                            || ((town->role_at(nx, ny) == TownRole::Gate)
                                && (town->site_at(nx, ny) == static_cast<std::int16_t>(i)));
                    }
                    if (next_to_gate) {
                        continue;
                    }
                    if ((site_mark[i] == 2u) && !site.has_house()) {
                        continue;
                    }
                    int dist = 0;
                    if (site.has_house()) {
                        const int dx = std::max({ site.house_x0 - gx, 0, gx - site.house_x1 });
                        const int dy = std::max({ site.house_y0 - gy, 0, gy - site.house_y1 });
                        dist = std::max(dx, dy);
                    } else {
                        const int cx = (site.x0 + site.x1) / 2;
                        const int cy = (site.y0 + site.y1) / 2;
                        dist = std::abs(gx - cx) + std::abs(gy - cy);
                    }
                    ranked.push_back({ dist, { gx, gy } });
                }
            }
            std::stable_sort(ranked.begin(), ranked.end(),
                [](const auto &a, const auto &b) { return a.first < b.first; });
            for (std::size_t r = 0; r < ranked.size(); ++r) {
                const auto &cell = ranked[r].second;
                yard_rank[(static_cast<std::size_t>(cell[1]) * static_cast<std::size_t>(town->width))
                    + static_cast<std::size_t>(cell[0])]
                    = static_cast<int>(r);
            }
        }
        /*
         * **敷地を持たない入口の目印**（2026-08-18。人里の龍神像で見つけた穴）。
         *
         * 幻想蛮怒の龍神像（`BUILDING_4`）は 4 本の柱だけの吹き放しの祠で、柱の欠片
         * （3 マスの L 字 × 4）が敷地の条件（`kMinSiteCells`）を満たさない——だから表に
         * 書いた目印が**どこにも立たなかった**（合成フレームで確認）。山肌の入口が
         * 「敷地を持たない門」であるのと同じ形の例外として、**入口のマスそのもの**から
         * 目印を引き、**1 マス北（祠の内側）**へ建てる。北なのはカメラが南から見るからで、
         * 入口のマスに立てるとプレイヤの実体と重なる。
         *
         * 敷地の目印（上）と違って建物は置き換えない（置き換える建物が無い）。
         */
        if (((style.landmarks != nullptr) || !townset.building_signs.empty()) && (memory != nullptr)) {
            /*
             * 開けたマスは (6) が通り道か草に塗ってしまうので、「置ける」は None ではなく
             * **{None, Path, Turf} のどれか**である（None 限定にしたら祠の中が全部
             * 弾かれて 1 件も拾えなかった。実測）。
             */
            const auto open_role = [this_town = town](int gx, int gy) {
                const TownRole role = this_town->role_at(gx, gy);
                return (role == TownRole::None) || (role == TownRole::Path) || (role == TownRole::Turf);
            };
            for (int gy = 1; gy < town->height; ++gy) {
                for (int gx = 0; gx < town->width; ++gx) {
                    if ((town->site_at(gx, gy) >= 0) || !open_role(gx, gy)) {
                        continue;
                    }
                    const std::uint16_t id = memory->id_at(gx, gy);
                    if (id == 0) {
                        continue;
                    }
                    //! 目印（表の landmarks 行）。**無くてもよい**——門だけの入口もある
                    //! （デザイン6。博麗の縁側 BUILDING_3 と池のほとり BUILDING_4）。
                    int mark_index = -1;
                    const char *mark_note = nullptr;
                    for (const TownLandmark *row = style.landmarks;
                         (row != nullptr) && (row->terrain_key != nullptr); ++row) {
                        if (library->terrain_id_for_key(row->terrain_key) != static_cast<int>(id)
                            || (std::strcmp(row->kind, "castle") == 0)) {
                            continue;
                        }
                        mark_index = library->find(row->kind);
                        mark_note = row->kind;
                        break;
                    }
                    /*
                     * 入口のマスに立てる門（2026-08-18。「素木の鳥居に」と決めた）。
                     * 町ごとの表の `gate` 行が名指す——**表に無ければ -1** ＝従来どおり
                     * 対応表の絵（変愚の石のアーチ）が立つ。
                     */
                    int gate = -1;
                    for (const auto &sign_row : townset.building_signs) {
                        if (sign_row.terrain_id == static_cast<int>(id)) {
                            gate = sign_row.gate;
                            break;
                        }
                    }
                    if ((mark_index < 0) && (gate < 0)) {
                        continue;
                    }
                    /*
                     * 目印の立ち位置は**開いた側の隣**（北 → 南 → 東 → 西の順で最初に
                     * 開いた向き。人里の祠は北が開くので従来の「1 マス北」と同じ結果）。
                     * どの向きも塞がっていたら目印は諦めて門だけ立てる。
                     */
                    int mdx = 0;
                    int mdy = 0;
                    if (mark_index >= 0) {
                        static constexpr int kOff[4][2] = { { 0, -1 }, { 0, 1 }, { 1, 0 }, { -1, 0 } };
                        bool placed = false;
                        for (const auto &off : kOff) {
                            if (open_role(gx + off[0], gy + off[1])) {
                                mdx = off[0];
                                mdy = off[1];
                                placed = true;
                                break;
                            }
                        }
                        if (!placed) {
                            mark_index = -1;
                        }
                    }
                    if ((mark_index < 0) && (gate < 0)) {
                        continue;
                    }
                    lone_marks.push_back({ gx, gy, mark_index, gate, mdx, mdy });
                    //! 出るのは町の読み直しのときだけ（数は 1 桁）。絵を見る前の検算用。
                    if (mark_note != nullptr) {
                        std::fprintf(stderr, "[hd2d] 敷地なし入口の目印: (%d,%d) → %s（%+d,%+d）\n",
                            gx, gy, mark_note, mdx, mdy);
                    }
                }
            }
        }
        /*
         * **屋敷の入口の前を空ける**（デザイン9）。印のマスそのものと、その周り 2 マス。
         * 印かどうかは地形 id で見る（置く側と同じ引き方）ので、**表が `manor_marks` を
         * 書いた町だけ**が対象になる。
         */
        if (!townset.manor_marks.empty() && (memory != nullptr) && !town->manors.empty()) {
            constexpr int kMarkRing = 2;
            mark_clear.assign(
                static_cast<std::size_t>(town->width) * static_cast<std::size_t>(town->height), 0u);
            for (int gy = 0; gy < town->height; ++gy) {
                for (int gx = 0; gx < town->width; ++gx) {
                    const TownRole role = town->role_at(gx, gy);
                    if ((role != TownRole::None) && (role != TownRole::Path)
                        && (role != TownRole::Turf)) {
                        continue;
                    }
                    const std::uint16_t id = memory->id_at(gx, gy);
                    if (id == 0) {
                        continue;
                    }
                    bool is_mark = false;
                    for (const auto &mark : townset.manor_marks) {
                        is_mark = is_mark || (mark.first == static_cast<int>(id));
                    }
                    if (!is_mark) {
                        continue;
                    }
                    for (int dy = -kMarkRing; dy <= kMarkRing; ++dy) {
                        for (int dx = -kMarkRing; dx <= kMarkRing; ++dx) {
                            const int nx = gx + dx;
                            const int ny = gy + dy;
                            if ((nx < 0) || (ny < 0) || (nx >= town->width) || (ny >= town->height)) {
                                continue;
                            }
                            mark_clear[(static_cast<std::size_t>(ny)
                                           * static_cast<std::size_t>(town->width))
                                + static_cast<std::size_t>(nx)]
                                = 1u;
                        }
                    }
                }
            }
        }
        /*
         * **入口の前に幟を並べる**（デザイン10・2026-08-19。命蓮寺）。§4.2 の外せない
         * 1 点は「灰の瓦屋根と**赤い幟の列**」で、街路樹の口では道じゅうに散ってしまう。
         * 入口のマス（地形 id が表の行に当たるマス）から**開いた向きへ進みながら、その左右
         * 1 マス**へ置く——参道の両脇に列が並ぶ。**表が名指した地形だけ**なので、
         * 店の暖簾の前には 1 本も立たない。
         */
        if (!townset.entrance_flanks.empty() && (memory != nullptr)) {
            const auto open_here = [this_town = town](int gx, int gy) {
                const TownRole role = this_town->role_at(gx, gy);
                return (role == TownRole::None) || (role == TownRole::Path) || (role == TownRole::Turf);
            };
            for (int gy = 0; gy < town->height; ++gy) {
                for (int gx = 0; gx < town->width; ++gx) {
                    const std::uint16_t id = memory->id_at(gx, gy);
                    if (id == 0) {
                        continue;
                    }
                    const decltype(townset)::FlankRow *row = nullptr;
                    for (const auto &candidate : townset.entrance_flanks) {
                        if (candidate.terrain_id == static_cast<int>(id)) {
                            row = &candidate;
                            break;
                        }
                    }
                    if (row == nullptr) {
                        continue;
                    }
                    //! 開いている向き（北 → 南 → 西 → 東の順で最初に開いたほう）。
                    static constexpr int kOut[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
                    int ox = 0;
                    int oy = 0;
                    for (const auto &off : kOut) {
                        if (open_here(gx + off[0], gy + off[1])) {
                            ox = off[0];
                            oy = off[1];
                            break;
                        }
                    }
                    if ((ox == 0) && (oy == 0)) {
                        continue;
                    }
                    //! 直交する向き（列の左右）。
                    const int sx = -oy;
                    const int sy = ox;
                    for (int step = 1; step <= row->reach; ++step) {
                        for (int side = -1; side <= 1; side += 2) {
                            const int px = gx + (ox * step) + (sx * side);
                            const int py = gy + (oy * step) + (sy * side);
                            if (!open_here(px, py)) {
                                continue;
                            }
                            const std::size_t pick = (static_cast<std::size_t>(step)
                                                         + static_cast<std::size_t>(side + 1))
                                % row->variants.size();
                            flank_props.push_back({ px, py, row->variants[pick] });
                        }
                    }
                }
            }
        }
        /*
         * **名指した塊の戸口と脇**（デザイン11・2026-08-20。魔法の森の 2 軒）。
         * どのマスかは読み取り側が拾ってある（`TownPlan::plot_props`）ので、ここは
         * 絵を引くだけ——戸口は 1 個、脇は変種の茎名（`<茎>_01..`）である。
         * 置き場は入口の前の列と同じ器（`flank_props`）へ入れる。
         */
        if (!town->plot_props.empty() && (style.plots != nullptr)) {
            for (const auto &prop : town->plot_props) {
                const TownPlot &plot = style.plots[prop[2]];
                const char *const stem = (prop[3] == 0) ? plot.mark : plot.flank;
                if ((stem == nullptr) || (stem[0] == 0)) {
                    continue;
                }
                int index = library->find(stem);
                if (index < 0) {
                    std::vector<int> variants;
                    gather_into(std::string(stem), variants);
                    if (variants.empty()) {
                        continue;
                    }
                    const auto pick = static_cast<std::size_t>(prop[0] + prop[1]) % variants.size();
                    index = variants[pick];
                }
                flank_props.push_back({ prop[0], prop[1], index });
            }
        }
        /*
         * **祠の柱の欠片を灯籠と花壇に読み替える**（2026-08-18 に決めた:「L 字の
         * ブロックはせっかくの龍神像を隠してしまってるので、腕の先の 1・2 は花壇、
         * 角の 3 は高さ 2 ブロックの石灯籠にしよう」）。
         *
         * 対象は**目印の入口の近く（チェビシェフ 3 マス）にある、敷地にならない小さな
         * 塀の塊**だけ。1 マス塀の絵（土塀の塊）は像の腰より高く、四方から像を囲んで
         * 隠していた。角（塊の中で隣が 2 つのマス）＝石灯籠、腕の先（隣 1 つ以下）＝花壇。
         * 塊が 5 マスを超えたら**本物の塀**なので手を出さない。
         */
        //! 大鳥居の脚のマスか（(1b) の対）。**灯籠・花壇の読み替えから除外する**——
        //! gate だけの目印（デザイン6 の木戸）が脚の近くに立つと、祠の柱の欠片の走査が
        //! 脚 4 マス（≤5 マス）を灯籠に読み替えて、鳥居の半身が出なくなった（実測）。
        const auto is_torii_leg = [this_town = town](int x, int y) {
            for (const auto &tg : this_town->torii_gates) {
                if (((x >= tg[0]) && (x <= tg[2]) && (y >= tg[1]) && (y <= tg[3]))
                    || ((x >= tg[4]) && (x <= tg[6]) && (y >= tg[5]) && (y <= tg[7]))) {
                    return true;
                }
            }
            return false;
        };
        /*
         * **道端の石灯籠**（デザイン6・2026-08-19。博麗神社の参道）。
         * **1〜2 マスの孤立した塀の欠片**を石灯籠に読み替える。人里の祠の読み替え（下）は
         * 目印の入口の近くだけだが、博麗の参道の灯籠（実測 (96,15)・(102,15)・(100,20)）は
         * 入口から遠い——表の旗（`path_lanterns`）を立てた町でだけ拾う。
         * 「道の近く」の条件は付けない——参道の上半分は道の網（Path 役割）に入らず、
         * 灯籠 3 つのうち 2 つが漏れた（実測）。旗は町ごとの opt-in なのでこれで足りる。
         * **祠の走査より先に置く**——読み替えは先に入ったほうが勝つので、木戸だけの目印
         * （神霊廟出張所）の近くの灯籠が「腕の先＝花壇」に化けない（(100,20) で実測）。
         * 大鳥居の脚（2×2 = 4 マス）は is_torii_leg と大きさの両方で外れる。
         */
        if (style.path_lanterns) {
            std::vector<std::array<int, 2>> lamp_seen;
            const auto seen_at = [&lamp_seen](int x, int y) {
                for (const auto &cell : lamp_seen) {
                    if ((cell[0] == x) && (cell[1] == y)) {
                        return true;
                    }
                }
                return false;
            };
            for (int gy = 0; gy < town->height; ++gy) {
                for (int gx = 0; gx < town->width; ++gx) {
                    if ((town->role_at(gx, gy) != TownRole::Rampart) || seen_at(gx, gy)
                        || is_torii_leg(gx, gy)) {
                        continue;
                    }
                    /*
                     * 塊を**最後まで**流してから大きさを見る（途中で打ち切ると、残りのマスが
                     * 次の種から「小さな塊」に見えて灯籠に化ける——鳥居の脚の 4 マスが
                     * 3+1 に割れて余りが灯籠になった実測の穴）。上限は保険。
                     */
                    std::vector<std::array<int, 2>> cluster{ { gx, gy } };
                    lamp_seen.push_back({ gx, gy });
                    bool big = false;
                    for (std::size_t head = 0; (head < cluster.size()) && !big; ++head) {
                        static constexpr int kD[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
                        for (const auto &d : kD) {
                            const int nx = cluster[head][0] + d[0];
                            const int ny = cluster[head][1] + d[1];
                            if ((town->role_at(nx, ny) != TownRole::Rampart) || seen_at(nx, ny)
                                || is_torii_leg(nx, ny)) {
                                continue;
                            }
                            cluster.push_back({ nx, ny });
                            lamp_seen.push_back({ nx, ny });
                            if (cluster.size() > 12u) {
                                big = true; // 明らかに本物の塀。数えるだけ無駄
                                break;
                            }
                        }
                    }
                    if (big || (cluster.size() > 2u)) {
                        continue; // 本物の塀。塀のまま
                    }
                    for (const auto &cell : cluster) {
                        lone_props.push_back({ cell[0], cell[1], 1 });
                    }
                }
            }
        }
        if (!lone_marks.empty()) {
            std::vector<std::array<int, 2>> flooded;
            const auto visited = [&flooded](int x, int y) {
                for (const auto &cell : flooded) {
                    if ((cell[0] == x) && (cell[1] == y)) {
                        return true;
                    }
                }
                return false;
            };
            for (const auto &mark : lone_marks) {
                for (int dy = -3; dy <= 3; ++dy) {
                    for (int dx = -3; dx <= 3; ++dx) {
                        const int sx = mark[0] + dx;
                        const int sy = mark[1] + dy;
                        if ((town->role_at(sx, sy) != TownRole::Rampart) || visited(sx, sy)
                            || is_torii_leg(sx, sy)) {
                            continue;
                        }
                        std::vector<std::array<int, 2>> cluster{ { sx, sy } };
                        flooded.push_back({ sx, sy });
                        bool big = false;
                        for (std::size_t head = 0; (head < cluster.size()) && !big; ++head) {
                            static constexpr int kD[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
                            for (const auto &d : kD) {
                                const int nx = cluster[head][0] + d[0];
                                const int ny = cluster[head][1] + d[1];
                                if ((town->role_at(nx, ny) != TownRole::Rampart) || visited(nx, ny)
                                    || is_torii_leg(nx, ny)) {
                                    continue;
                                }
                                cluster.push_back({ nx, ny });
                                flooded.push_back({ nx, ny });
                                if (cluster.size() > 5) {
                                    big = true;
                                    break;
                                }
                            }
                        }
                        if (big) {
                            continue; // 本物の塀に繋がっている。塀のまま
                        }
                        for (const auto &cell : cluster) {
                            int degree = 0;
                            for (const auto &other : cluster) {
                                const int dist = std::abs(cell[0] - other[0]) + std::abs(cell[1] - other[1]);
                                degree += (dist == 1) ? 1 : 0;
                            }
                            lone_props.push_back({ cell[0], cell[1], (degree >= 2) ? 1 : 0 });
                        }
                    }
                }
            }
        }
    }

    props = resolve_terrain_props(meaning, library, use_lib);

    /*
     * ライブラリ素材のマスごとの色むら（2026-08-09 に決めた「ダンジョンブロック全てに
     * 色むらが欲しい」）。仮の箱の世界の倍率 0.25（罠 29 の「線」対策）をそのまま掛けると
     * ±1〜3% でほぼ見えない。素材には目地や石割りの模様があり、境目の段差は模様に紛れる
     * ので、こちらは振れ幅を上げる（0.25 × 3 = 0.75 が既定＝下の amount のおよそ 3/4）。
     * `HD2D_TERRAIN_VARIANCE` は引き続き倍率の指定（0 で真っ平ら・1 で 3 倍）。
     * 明暗（common）に加えて、わずかな暖冷（warm。r と b を逆へ）も混ぜる。
     *
     * **上げすぎると「まだら」になる。**4 倍・構造 ±11% で試した版は、岩盤の塊ごとに
     * 明るさが飛んで洞窟が斑に見えた（レビュー第 1 回で撮って気づいた）。
     */
    const float mottle = terrain_variance() * 3.f;
    auto lib_tint = [mottle](InstanceData &instance, const TerrainRule &rule, std::uint64_t &state, float amount) {
        const float common = next_range(state, -1.f, 1.f) * amount * mottle;
        const float warm = next_range(state, -1.f, 1.f) * amount * 0.35f * mottle;
        instance.r = rule.tint[0] * (1.f + common + warm);
        instance.g = rule.tint[1] * (1.f + common);
        instance.b = rule.tint[2] * (1.f + common - warm);
    };
    /*!
     * 集合の**何枚目**を選ぶか。種は必ず 1 回進める（集合の大きさに依存させない）。
     * 枚目で返すのは、姿勢の対（`object` と `object_ew`）で**同じ枚を引く**ため
     * （P10 レビュー 11。対の枚数が揃っていることは `parse_rule` が確かめている）。
     */
    auto choose_slot = [](const std::vector<int> &set, std::uint64_t &state) {
        const float pick = next01(state);
        const int at = static_cast<int>(pick * static_cast<float>(set.size()));
        return static_cast<std::size_t>(std::min(at, static_cast<int>(set.size()) - 1));
    };
    //! 集合から種で 1 つ選ぶ。
    auto choose = [&choose_slot](const std::vector<int> &set, std::uint64_t &state) {
        return set[choose_slot(set, state)];
    };
    //! そのマスの「視線を遮る高さ」を更新する（高いほうを採る）。
    auto note_occluder = [&out](int gx, int gy, float top) {
        if ((gx < 0) || (gy < 0) || (gx >= out.occluder_w) || (gy >= out.occluder_h)) {
            return;
        }
        float &slot = out.occluder_z[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(out.occluder_w))
            + static_cast<std::size_t>(gx)];
        slot = std::max(slot, top);
    };
    /*!
     * 置いたプレハブが**実際に覆うマス全部**へ高さを記録する（P10 レビュー 6）。
     * footprint（当たり判定）ではなくボクセルの広がりを使うのが要点で、
     * 木は幹 1 マスの footprint に対して葉が 3〜4 マスに張り出している。
     */
    auto note_cover = [&note_occluder](const LibraryEntry &entry, const InstanceData &at, float top) {
        const int base_x = static_cast<int>(std::floor(at.x));
        const int base_y = static_cast<int>(std::floor(at.y));
        for (int cy = entry.cover_y0; cy <= entry.cover_y1; ++cy) {
            for (int cx = entry.cover_x0; cx <= entry.cover_x1; ++cx) {
                note_occluder(base_x + cx, base_y + cy, top);
            }
        }
    };
    /*!
     * 町の素材を 1 つ置く（P10 第 2 期）。ライブラリに無ければ**置かないだけ**（-1 で素通り）。
     * @param cover_top 0 より大きければ、その高さで視線の遮りを記録する。
     * @details anchor を引くのも `lib_instances` を数えるのも他の経路と同じ。
     */
    auto place_town = [&out, &note_cover, library, use_lib](int index, float fx, float fy, float z,
                          const float rgb[3], float cover_top = 0.f, float er = 0.f, float eg = 0.f,
                          float eb = 0.f) {
        if (!use_lib || (index < 0)) {
            return;
        }
        const LibraryEntry &entry = library->entry(index);
        InstanceData inst;
        inst.x = fx - entry.anchor_x;
        inst.y = fy - entry.anchor_y;
        inst.z = z;
        inst.r = rgb[0];
        inst.g = rgb[1];
        inst.b = rgb[2];
        inst.er = er;
        inst.eg = eg;
        inst.eb = eb;
        out.lib[static_cast<std::size_t>(index)].push_back(inst);
        ++out.lib_instances;
        if (cover_top > 0.f) {
            note_cover(entry, inst, cover_top);
        }
    };
    //! そのプレハブの天面（マス）。ライブラリに無ければ 0。**高さを直に書かない**ための手段。
    auto top_of = [library, use_lib](int index) {
        return (use_lib && (index >= 0)) ? library->entry(index).top_z : 0.f;
    };

    /*
     * 岩盤・プレハブはここまで高くなりうる。視錐台の判定にはこの高さを使う
     * （低く見積もると天面が消える）。木（3 マス）が入るライブラリの経路では高くとる。
     * **一人称の 2 段目・天井（後述）が入るときは天井の天面（z=3）まで持ち上げる**
     * ——仮の箱の経路の 1.9 のままだと、画面の縁で天井だけ欠ける。
     * @note 木の張り出し（footprint の外の枝葉）までは見ていない。画面の縁で
     * 中心マスが外れると木が丸ごと消える。P10 の積み残し（プレハブの届く範囲での判定）。
     */
    const bool fps_layers = fps_wall_upper || fps_ceiling;
    //! 覆いの町は山塊の頭まで（岩天井 6 ＋ 峰 3.2。低いままだと画面の縁で山だけ欠ける）。
    const float kMaxHeight = std::max({ use_lib ? 7.5f : 1.9f, fps_layers ? 3.2f : 0.f,
        out.covered ? (out.cave_top + 0.4f) : 0.f });
    //! 地上（町・荒野）か。街路の見た目をダンジョンの通路と分けるのに使う（`roles_surface`）。
    const bool surface_floor = meaning.identity.kind == static_cast<int>(FloorKind::Surface);
    /*
     * 立ち上がるものの根元を地面より少し下へ埋める。
     *
     * **これが無いと、床を少し下げたマスの脇に 1 画素の隙間が空いて背景が見える。**
     * 通路の床は種で最大 0.03 マス沈めているのに、壁の箱は z = 0 から始まっていたため、
     * その差のぶんだけ**壁の根元の裏側**が浅い角度から見えていた。四隅の欠け検査が
     * 見下ろし 42°／画角 44°／1 マス 70px で 0.45% を出して見つけた
     * （既定のカメラ 46°/40°/110px では 0.000% のままで、出なかった）。
     * 埋める深さは床の沈み込み（0.03）より十分深くとる。
     */
    constexpr float kFootingDepth = 0.08f;

    /*!
     * 斜め抜けの隙間（一人称。`ui/fps_mode.h` の `kFpsWallInset`）。
     *
     * 人間の指示「斜め移動が可能になっている場合はブロックがぴったりとせず隙間が空くように」。
     * **痩せさせるのは「隣がブロックでない辺」だけ**である。全周を痩せさせてはならない:
     * 一直線に続く壁が 1 マスごとに割れて柵のように見えるうえ、隣り合う岩盤の間から
     * 背景が抜ける（`CellRole::Bedrock` の doc コメントに、以前それで四隅の欠け検査が
     * 0.45% を出した経緯が書いてある）。開いている辺だけなら、角で接する 2 つの間にだけ
     * `2 × wall_inset` の隙間が開く。
     *
     * `wall_inset == 0`（本線）のときは**何も触らない**ので、置き場所は 1 ビットも変わらない。
     */
    const auto inset_solid = [&meaning](int x, int y) {
        const CellRole r = meaning.role_at(x, y);
        return (r == CellRole::StructuralWall) || (r == CellRole::Bedrock);
    };
    const auto apply_wall_inset = [wall_inset, &inset_solid](int gx, int gy, InstanceData &inst) {
        if (wall_inset <= 0.f) {
            return;
        }
        const float w = inset_solid(gx - 1, gy) ? 0.f : wall_inset;
        const float e = inset_solid(gx + 1, gy) ? 0.f : wall_inset;
        const float n = inset_solid(gx, gy - 1) ? 0.f : wall_inset;
        const float s = inset_solid(gx, gy + 1) ? 0.f : wall_inset;
        // 拡大率は**インスタンスの原点**まわりに掛かるので、先に原点を内側へ寄せる。
        inst.x += w;
        inst.y += n;
        inst.sx *= 1.f - w - e;
        inst.sy *= 1.f - n - s;
    };

    /*!
     * 一人称の 2 段目と天井（2026-08-11 に決めた）。くわしくはヘッダの説明にある。
     *
     * ## 材は「そのダンジョンの基本ブロック」1 系統だけ
     * 意匠の壁（`DungeonStyle::wall_stem`）→ 役割の既定の壁（花崗岩）の順で引き、
     * **マスの細別（鉱脈・石英…）は見ない**。指示が「全てダンジョンの基本ブロックで埋めて」
     * なので、鉱脈のマスの上も基本ブロックになる。
     *
     * ## 種は本流と別系統（`^ kFpsLayerSalt`）
     * 本流の `seed` から引くと乱数の消費数が入切で変わり、**一人称に切り替えた瞬間に
     * 小物と色むらが入れ替わる**。マスの座標から別の列を起こせば、本流は 1 ビットも動かない。
     */
    constexpr std::uint64_t kFpsLayerSalt = 0x2B10C85E11A9ull;
    //! 壁の天面（＝天井の底）。2 段目が入なら z=2、切なら z=1。
    const float fps_wall_top = fps_wall_upper ? 2.f : 1.f;
    //! 基本ブロックの材。意匠の壁 → 役割の既定の壁。**どちらも無ければ仮の箱**（tint も既定）。
    const std::vector<int> *fps_layer_set = nullptr;
    const TerrainRule *fps_layer_rule = nullptr;
    if (use_lib && fps_layers) {
        fps_layer_rule = library->rule_for_role(CellRole::StructuralWall, surface_floor);
        const std::vector<int> &styled_wall = props.mat_structure[static_cast<std::size_t>(CellRole::StructuralWall)];
        if (!styled_wall.empty()) {
            fps_layer_set = &styled_wall;
        } else if ((fps_layer_rule != nullptr) && !fps_layer_rule->structure.empty()) {
            fps_layer_set = &fps_layer_rule->structure;
        }
    }
    //! 色むらの基準。役割の既定が無いライブラリでも tint 1,1,1 で置けるように別に持つ。
    static const TerrainRule kFpsLayerPlainRule{};
    /*!
     * 基本ブロックを 1 つ置く。`z` が底、高さはちょうど 1 マスへ揃える
     * （岩盤系の材は天面が 1.0 でないことがあるので `sz` で正す）。
     * @param solid 壁の並び（壁・岩盤・扉の上）か。真なら斜め抜けの隙間も同じだけ痩せる
     * ——1 段目だけ痩せて 2 段目が張り出すと、庇のようになって隙間が読めない。
     */
    const auto emit_fps_block = [&](int gx, int gy, float fx, float fy, float z, bool solid) {
        std::uint64_t layer_seed = mix64(cell_seed(gx, gy, meaning.identity) ^ kFpsLayerSalt);
        if (fps_layer_set != nullptr) {
            const int pick = choose(*fps_layer_set, layer_seed);
            const LibraryEntry &entry = library->entry(pick);
            InstanceData inst;
            inst.x = fx - entry.anchor_x;
            inst.y = fy - entry.anchor_y;
            inst.z = z;
            if (entry.top_z > 0.01f) {
                inst.sz = 1.f / entry.top_z;
            }
            lib_tint(inst, (fps_layer_rule != nullptr) ? *fps_layer_rule : kFpsLayerPlainRule, layer_seed, 0.085f);
            if (solid) {
                apply_wall_inset(gx, gy, inst);
            }
            out.lib[static_cast<std::size_t>(pick)].push_back(inst);
            ++out.lib_instances;
        } else {
            InstanceData box;
            box.x = fx;
            box.y = fy;
            box.z = z;
            tint(box, kStructuralWall, layer_seed, 0.04f);
            if (solid) {
                apply_wall_inset(gx, gy, box);
            }
            out.boxes.push_back(box);
        }
    };
    /*!
     * そのマスのぶんの 2 段目／天井を置く。**ライブラリの経路と仮の箱の経路の両方から呼ぶ**
     * （どちらの経路でも絵の約束は同じ——壁の並びは 2 段・開けたマスの上は天井）。
     *
     * どちらの層も視線を遮る高さ（`note_occluder`）には**入れない**。目の高さ（0.55 マス）の
     * 視線は 1 段目が既に遮っていて、2 段目で答えが変わるマスは無い。天井を入れると
     * 全マスが「遮られている」ことになり、実体のカットアウェイが誤爆する。
     *
     * 扉のマスは、2 段目が入ならまぐさ石（z=1〜2・壁あつかい）で塞がる。**切のときは
     * 天井の続き**として塞ぐ——扉の上だけ開けたままだと、そこから背景が抜ける。
     */
    const auto emit_fps_layers = [&](int gx, int gy, float fx, float fy, CellRole role) {
        if (!fps_layers) {
            return;
        }
        const bool wall_line = (role == CellRole::StructuralWall) || (role == CellRole::Bedrock)
            || (role == CellRole::Doorway);
        if (wall_line) {
            if (fps_wall_upper) {
                emit_fps_block(gx, gy, fx, fy, 1.f, true);
            } else if ((role == CellRole::Doorway) && fps_ceiling) {
                emit_fps_block(gx, gy, fx, fy, fps_wall_top, false);
            }
            return;
        }
        if (fps_ceiling) {
            emit_fps_block(gx, gy, fx, fy, fps_wall_top, false);
        }
    };

    /*
     * **覆いの町では灯りを昼も点す**（2026-08-18 その3）。岩天井の下に太陽は届かない
     * ——時刻がいつでも、格子窓・軒下の提灯・街灯は点いているのが正しい
     * （原作の旧地獄街道も「通りには火の灯った吊り提灯」が昼のステージに並ぶ）。
     * 覆いの無い町は従来どおり `night` そのまま＝ 1 ビットも変わらない。
     */
    const float night_eff = out.covered ? std::max(night, 0.85f) : night;
    //! 岩天井を敷く高さ（マス）。覆いの町でだけ使う。
    const float cave_roof_z = (style.cave_roof_height > 0.f) ? style.cave_roof_height : 6.f;
    /*
     * **山の起伏の値ノイズ**（2026-08-18 その3「複数ブロックを組み合わせたゴツゴツとした
     * 凹凸のある大きな岩山を」）。格子の角にマスの種と同じ流儀で乱数を置き、間を
     * 滑らかに補間する（3t²−2t³。線形だと格子の折り目が稜線に見える）。**マスとフロアだけ**
     * から決まるので、カメラにも走査順にも依存しない（§9.3 と同じ約束）。
     * 8 マス（山の骨格）と 3 マス（肩の荒れ）の**2 オクターブを重ねる**——1 枚だけだと
     * なだらかな丘の連なりで、「ダイナミックな凹凸」（同 その3 の指摘）にならない。
     */
    const auto mound_noise_at = [&meaning](int gx, int gy, int step, std::uint64_t salt) -> float {
        const auto corner = [&meaning, step, salt](int cx, int cy) {
            std::uint64_t s = cell_seed(cx * step, cy * step, meaning.identity) ^ salt;
            return next01(s);
        };
        const int bx = gx / step;
        const int by = gy / step;
        const float tx = static_cast<float>(gx - (bx * step)) / static_cast<float>(step);
        const float ty = static_cast<float>(gy - (by * step)) / static_cast<float>(step);
        const float sx = tx * tx * (3.f - (2.f * tx));
        const float sy = ty * ty * (3.f - (2.f * ty));
        const float a = corner(bx, by);
        const float b = corner(bx + 1, by);
        const float c = corner(bx, by + 1);
        const float d = corner(bx + 1, by + 1);
        return (((a * (1.f - sx)) + (b * sx)) * (1.f - sy)) + (((c * (1.f - sx)) + (d * sx)) * sy);
    };
    /*
     * 蓋の上の**狙いの高さ**（マス）。値ノイズは真ん中に寄る分布なので、素のまま使うと
     * 「どこも中くらいの高さのガレ場」になる（実写で確認——同 その3 の指摘の二の舞）。
     * **窓で切って対比を作る**: 骨格（12 マス）の 0.40 以下は谷（岩屑の低地）・0.64 以上は
     * 山塊の頂に張り付け、間の 0.24 幅（約 3 マス）が崖の斜面になる。肩の荒れ（3 マス）は
     * **峰ほど強く**掛ける（谷まで荒らすと大きな構造がまた消える）。
     * 振幅は谷 0.35 〜 峰 7 マス超（同 その3「もっとダイナミックに。思い切りがたらないぞ」
     * ——4 マスでは足らなかった。峰の段 5.5 マス × 伸縮 1.45 まで使い切る）。
     */
    const auto mound_target = [&mound_noise_at](int gx, int gy) -> float {
        const float bone = mound_noise_at(gx, gy, 12, 0xa24baed4963ee407ull);
        const float shoulder = mound_noise_at(gx, gy, 3, 0x94d049bb133111ebull);
        const float windowed = std::min(1.f, std::max(0.f, (bone - 0.40f) / 0.24f));
        const float shaped = std::pow(windowed, 1.15f);
        return 0.35f + (7.2f * shaped * (0.70f + (0.60f * shoulder)));
    };

    for (int gy = 0; gy < meaning.height; ++gy) {
        for (int gx = 0; gx < meaning.width; ++gx) {
            CellRole role = meaning.role_at(gx, gy);
            if (role == CellRole::Unknown) {
                if (!black_unknown) {
                    continue; // 未探知。**何も置かない**（見えていない地形を捏造しない）
                }
                /*
                 * **一人称のときだけ、真っ黒な塊で埋める**（2026-08-11 に決めた）。
                 *
                 * 何も置かないと、目の高さからは**そこだけ世界の外が抜けて見える**。
                 * 未探知は「向こうが見えない闇」であって穴ではない。
                 *
                 * **地形を捏造してはいない**ことに注意。置くのは「まだ知らない」という
                 * 一様な黒であって、壁でも床でもない（色で見分けがつく）。
                 * 探知が進めば本当の地形に置き換わる。
                 */
                const Vec3 dark_lo{ static_cast<float>(gx), static_cast<float>(gy), -0.1f };
                const Vec3 dark_hi{ static_cast<float>(gx) + 1.f, static_cast<float>(gy) + 1.f, kMaxHeight };
                if (!frustum.intersects(dark_lo, dark_hi)) {
                    ++out.culled_cells;
                    continue;
                }
                InstanceData dark;
                dark.x = static_cast<float>(gx);
                dark.y = static_cast<float>(gy);
                dark.z = -kFootingDepth;
                /*
                 * 壁と同じ高さ。低いと未探知の向こうに既知の壁の頭が覗く。
                 * **壁が 2 段（`fps_wall_upper`）ならこちらも 2 段**——1 段のままだと、
                 * 未探知の黒の上（z=1〜2）から既知の壁の 2 段目と天井の裏が覗く。
                 */
                dark.sz = (fps_wall_upper ? 2.f : 1.f) + kFootingDepth;
                /*
                 * **真っ黒**。インスタンスの色は光に掛かるので、0 なら何を当てても黒のまま。
                 * 自発光も 0（ブルームに拾わせない）。
                 */
                dark.r = 0.f;
                dark.g = 0.f;
                dark.b = 0.f;
                /*
                 * **痩せさせない。**一人称の隙間（`wall_inset`）は「斜めに抜けられる」ことを
                 * 見せるためのもので、未探知は抜けられるかどうかが分からない所である。
                 * 痩せさせると隙間から世界の外が覗く（＝穴にしないという目的が崩れる）。
                 */
                out.boxes.push_back(dark);
                note_occluder(gx, gy, fps_wall_upper ? 2.f : 1.f);
                ++out.drawn_cells;
                continue;
            }
            /*
             * 部屋か通路かは**コアの `CAVE_ROOM` を正**とする（`CELL_FEAT_ROOM`。P10 レビュー）。
             * 意味づけ層の「床の連なりの幅」からの推定は、暗い部屋を歩いた直後に既知領域が
             * 細いせいで「通路」と誤読し、探索が進むと部屋へ再分類されて**床の見た目が後から
             * 化ける**（実機で見つけた:「一歩南のマスに乗ると通常の地面に戻る」）。
             * 間近で見たマスは記憶のビットで確定し、見ていないマスだけ推定に頼る。
             */
            if ((memory != nullptr) && ((role == CellRole::RoomFloor) || (role == CellRole::CorridorFloor))) {
                const std::uint16_t seen_flags = memory->flags_at(gx, gy);
                if ((seen_flags & CELL_FEAT_KNOWN) != 0u) {
                    role = ((seen_flags & CELL_FEAT_ROOM) != 0u) ? CellRole::RoomFloor : CellRole::CorridorFloor;
                }
            }
            ++out.considered_cells;

            const Vec3 lo{ static_cast<float>(gx), static_cast<float>(gy), -0.1f };
            const Vec3 hi{ static_cast<float>(gx) + 1.f, static_cast<float>(gy) + 1.f, kMaxHeight };
            if (!frustum.intersects(lo, hi)) {
                ++out.culled_cells;
                continue;
            }
            ++out.drawn_cells;

            std::uint64_t seed = cell_seed(gx, gy, meaning.identity);
            const auto fx = static_cast<float>(gx);
            const auto fy = static_cast<float>(gy);
            const std::uint8_t mark = meaning.mark_at(gx, gy);

            /*
             * **岩天井**（2026-08-18 その3「街全体を巨大な黒い岩山で覆い、屋根として
             * しまいたい」）。役割を問わず**全マス**の頭上に蓋を敷く——通りも家並みも
             * 岩山も水も、同じ山の中にある。穴を開けるのは描く側の屋根外しの層
             * （`make_roof_cutaway`。`TerrainView::covered` が合図）。
             * 遮蔽（`cover_top`）には**入れない**——入れると全マスが「遮られている」に
             * なり、実体の穴が枠（12）を食い尽くす（terrain_view.h の註記）。
             */
            if (out.covered) {
                /*
                 * 変種は **4×4 マスのまとまり**で選ぶ——変種ごとの天面の高さの差（1 ボクセル）が
                 * 数マスおきの緩い段丘になる。マスごとに独立に選んだ初版は、全面が市松の
                 * 砂利に見えた（実写で確認）。種は別に切る（下のマスの種の消費を乱さない）。
                 */
                std::uint64_t lid_seed = cell_seed(gx & ~3, gy & ~3, meaning.identity) ^ 0x9e3779b97f4a7c15ull;
                static constexpr float kLidPlain[3] = { 1.f, 1.f, 1.f };
                place_town(choose(townset.cave_roof, lid_seed), fx, fy, cave_roof_z, kLidPlain, 0.f);
                /*
                 * **山の起伏**（2026-08-18 その3）。値ノイズの高い所に裾→峰の塊を積む。
                 * 段は 3 つで、閾を段ごとに離してあるので山裾が峰を囲む（岩山の
                 * 「麓→峰の段」と同じ読み）。塊は蓋へ 0.5 マス埋める——蓋の変種の
                 * 高さ差（1 ボクセル）を吸収し、継ぎ目から下が抜けない。
                 */
                /*
                 * ノイズの低い所も**岩屑（base）で埋める**——平らな天面を残さない（同 その3）。
                 * 高さは**狙いの高さへの連続の伸縮**で作る（同 その3 の指摘「同じ高さに並んで
                 * 相対的に平らに見える。もっとダイナミックに」——段の量子化だけだと段の中が
                 * 同じ高さに揃う）。狙い（谷 0.5 〜 峰 4 マス超）に一番近い段の素材を選び、
                 * z の伸縮で狙いに合わせる。隣のマスは狙いも近いので、山肌は連続の斜面になる。
                 */
                const float target = mound_target(gx, gy);
                const int mound_tier = (target < 1.05f) ? 0
                    : ((target < 1.75f) ? 1
                          : ((target < 2.85f) ? 2 : ((target < 4.30f) ? 3 : 4)));
                if (!townset.cave_mound[mound_tier].empty()) {
                    std::uint64_t mound_seed = seed ^ 0x517cc1b727220a95ull;
                    const int mound_pick = choose(townset.cave_mound[mound_tier], mound_seed);
                    if (mound_pick >= 0) {
                        const LibraryEntry &entry = library->entry(mound_pick);
                        const float native = std::max(0.5f, entry.top_z);
                        InstanceData inst;
                        inst.x = fx - entry.anchor_x;
                        inst.y = fy - entry.anchor_y;
                        inst.z = cave_roof_z + 0.5f; //!< 蓋へ 0.5 マス埋める（変種の高さ差の吸収）
                        inst.sz = std::min(1.45f, std::max(0.5f, target / native));
                        out.lib[static_cast<std::size_t>(mound_pick)].push_back(inst);
                        ++out.lib_instances;
                    }
                }
                /*
                 * **交差点のガス灯**（2026-08-18 その3「街中はまだ暗いので、道の交差点毎に
                 * ガス灯のような灯りを立てよう」）。道のマスで直交の隣に道が 3 つ以上＝
                 * 交差点。2×2 に固まる所は西・北・北西を見て 1 本に間引く。
                 * 道の真ん中を塞がないよう隅へ寄せる（街路樹の註記と同じ理由）。
                 * **本当の明かりは点光源のリクエスト**——自発光は自分しか光らせない
                 * （龍神像のライトアップと同じ判断）。リクエストの消費側が覆いの町では
                 * 昼も点し、近い順に枠（16）へ足す。
                 */
                //! 覆いの町は必ず use_town（covered は意匠の表からしか立たない）。
                if ((townset.cross_lamp >= 0) && (town->role_at(gx, gy) == TownRole::Path)) {
                    const auto crossing = [this_town = town](int x, int y) {
                        if (this_town->role_at(x, y) != TownRole::Path) {
                            return false;
                        }
                        const int arms = ((this_town->role_at(x - 1, y) == TownRole::Path) ? 1 : 0)
                            + ((this_town->role_at(x + 1, y) == TownRole::Path) ? 1 : 0)
                            + ((this_town->role_at(x, y - 1) == TownRole::Path) ? 1 : 0)
                            + ((this_town->role_at(x, y + 1) == TownRole::Path) ? 1 : 0);
                        return arms >= 3;
                    };
                    if (crossing(gx, gy) && !crossing(gx - 1, gy) && !crossing(gx, gy - 1)
                        && !crossing(gx - 1, gy - 1)) {
                        std::uint64_t lamp_seed = seed ^ 0x2545f4914f6cdd1dull;
                        const float ox = (next01(lamp_seed) < 0.5f) ? -0.30f : 0.30f;
                        const float oy = (next01(lamp_seed) < 0.5f) ? -0.30f : 0.30f;
                        place_town(townset.cross_lamp, fx + ox, fy + oy, -kFootingDepth, kLidPlain,
                            top_of(townset.cross_lamp));
                        if ((townset.cross_lamp_flame >= 0) && (night_eff > 0.02f)) {
                            const float glow = night_eff * 2.1f;
                            place_town(townset.cross_lamp_flame, fx + ox, fy + oy, -kFootingDepth,
                                kLidPlain, 0.f, glow, glow * 0.80f, glow * 0.50f);
                        }
                        TerrainViewLight wish;
                        wish.x = fx + ox + 0.5f;
                        wish.y = fy + oy + 0.5f;
                        wish.z = 1.6f; // 火屋の高さ
                        wish.radius = 4.5f;
                        wish.r = 1.f;
                        wish.g = 0.82f;
                        wish.b = 0.55f;
                        wish.intensity = 1.5f;
                        out.lights.push_back(wish);
                        ++out.prop_count;
                    }
                }
            }
            /*
             * ---- **町じゅうの配管**（デザイン7 その2・2026-08-19。河童のバザー）----
             *
             * 決めたこと:「工房と市の地面と壁に**やけくそって程に**配管を置こう。
             * 焼き込みではなく、ボクセルでパイプをそこかしこに」。
             *
             * 置き場は 2 つ。
             * | 何 | どこ |
             * |---|---|
             * | 本管の網 | 屋敷の近くの開けたマス。**行と列をハッシュで選ぶ**ので、
             *   選ばれた行（列）は端から端まで 1 本の管が通る（マスごとの乱数だと切れ切れになる） |
             * | 壁の管 | **屋敷に接したマス**。屋敷のある側の横引きを必ず置き、
             *   1/3 で立ち上がりと弁に差し替える（横引きの寸法は同じなので線は切れない） |
             *
             * **屋敷の近くだけ**に絞るのは、この地図が森 5,308 マス・草 9,488 マスで、
             * 全域に敷くと森の中まで管が走るため（町の絵ではなくなる）。
             */
            if (use_town && style.pipe_works && !townset.pipe_run[0].empty()) {
                static constexpr float kPipePlain[3] = { 1.f, 1.f, 1.f };
                const TownRole here = town->role_at(gx, gy);
                const bool open_cell = (here == TownRole::None) || (here == TownRole::Path)
                    || (here == TownRole::Turf);
                //! 屋敷から 5 マスまで（市庭と、建物どうしの間の通りが入る広さ）。
                constexpr int kPipeReach = 5;
                bool near_manor = false;
                for (const auto &mbox : town->manors) {
                    if ((gx >= (mbox[0] - kPipeReach)) && (gx <= (mbox[2] + kPipeReach))
                        && (gy >= (mbox[1] - kPipeReach)) && (gy <= (mbox[3] + kPipeReach))) {
                        near_manor = true;
                        break;
                    }
                }
                if (open_cell && near_manor) {
                    //! そのマスに接している屋敷の向き（N / S / W / E の順に最初の 1 つ）。
                    static constexpr int kPipeDirs[4][2] = {
                        { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 }
                    };
                    int wall_side = -1;
                    for (int i = 0; i < 4; ++i) {
                        if (town->role_at(gx + kPipeDirs[i][0], gy + kPipeDirs[i][1])
                            == TownRole::Manor) {
                            wall_side = i;
                            break;
                        }
                    }
                    std::uint64_t pipe_seed = seed ^ 0x9E3779B97F4A7C15ull;
                    if ((wall_side >= 0) && !townset.pipe_wall[wall_side].empty()) {
                        //! 壁に添う管。1/3 は立ち上がりと弁（＝蒸気の口）へ差し替える。
                        const bool riser = (next01(pipe_seed) < 0.34f)
                            && !townset.pipe_riser[wall_side].empty();
                        const std::vector<int> &set = riser ? townset.pipe_riser[wall_side]
                                                            : townset.pipe_wall[wall_side];
                        const int pick = choose(set, pipe_seed);
                        place_town(pick, fx, fy, 0.f, kPipePlain, 0.f);
                        ++out.prop_count;
                        //! **噴くのは半分ほど**（全部の口から噴くと町が霧に沈む＝実測）。
                        if (riser && (next01(pipe_seed) < 0.55f)) {
                            //! 噴気の口（型の中の高さ 40〜44 vox ＝ 1.3 マス）。
                            const float ox = (wall_side == 2) ? 0.80f
                                : ((wall_side == 3) ? 0.20f : 0.50f);
                            const float oy = (wall_side == 0) ? 0.80f
                                : ((wall_side == 1) ? 0.20f : 0.50f);
                            out.steam_jets.push_back({ fx + ox, fy + oy, 1.32f });
                        }
                    } else {
                        /*
                         * 本管の網。**行と列をハッシュで選ぶ**——選ばれた行は端から端まで
                         * 管が通る。3 行に 1 本・4 列に 1 本ほどで、市庭を何本もの管が横切る
                         * （「やけくそって程に」と決めた——初版の 5 行 / 6 列では疎かった）。
                         */
                        const bool ew = ((mix64(static_cast<std::uint64_t>(gy) ^ 0xB1Eull) % 3ull) == 0ull);
                        const bool ns = ((mix64(static_cast<std::uint64_t>(gx) ^ 0xC0DEull) % 4ull) == 0ull);
                        if (ew && ns && !townset.pipe_cross.empty()) {
                            const int pick = choose(townset.pipe_cross, pipe_seed);
                            place_town(pick, fx, fy, 0.f, kPipePlain, 0.f);
                            ++out.prop_count;
                            //! 継ぎ手の噴気管の口（型の 48 vox ＝ 1.5 マス）。
                            if (next01(pipe_seed) < 0.60f) {
                                out.steam_jets.push_back({ fx + 0.5f, fy + 0.5f, 1.52f });
                            }
                        } else if (ew || ns) {
                            const std::vector<int> &set = townset.pipe_run[ew ? 1 : 0];
                            const int pick = choose(set, pipe_seed);
                            place_town(pick, fx, fy, 0.f, kPipePlain, 0.f);
                            ++out.prop_count;
                        }
                    }
                }
            }

            /*
             * **壁の下にも地面を敷く**（P7 で足した）。設計書 §9.2 の地面層は
             * 「各マスに 1 枚の板」で、壁のマスだけ抜けていたのは実装の落ち度である。
             * 壁が立っている限り完全に隠れるので P6 までは誰も困らなかったが、
             * **カットアウェイが壁を抜いた瞬間に、そこだけ背景が見えた**
             * （`--cutaway-check` の (3) が円の 9.2% を出して見つけた）。
             *
             * 影のパスには渡さない（壁の真下は光が当たらないので、影は 1 テクセルも変わらない）。
             * 描くのも色のパスだけなので、別の配列に分けてある。
             */
            const TownRole town_role = use_town ? town->role_at(gx, gy) : TownRole::None;
            /*
             * 祠の柱の欠片（灯籠か花壇に読み替えるマス）か。前庭と同じく z=0 に土間を
             * 敷くので、このマスも下敷きを敷かない（同じ高さと z-fight する）。
             */
            int shrine_prop_kind = -1;
            for (const auto &prop : lone_props) {
                if ((prop[0] == gx) && (prop[1] == gy)) {
                    shrine_prop_kind = prop[2];
                    break;
                }
            }
            //! 大鳥居の脚のマスか（デザイン6）。足元に開けた地面を z=0 で敷くので、
            //! 祠の柱の欠片と同じく下敷きを敷かない（同じ高さと z-fight する）。
            bool torii_leg_here = false;
            if (use_town) {
                for (const auto &tg : town->torii_gates) {
                    if (((gx >= tg[0]) && (gx <= tg[2]) && (gy >= tg[1]) && (gy <= tg[3]))
                        || ((gx >= tg[4]) && (gx <= tg[6]) && (gy >= tg[5]) && (gy <= tg[7]))) {
                        torii_leg_here = true;
                        break;
                    }
                }
            }
            //! 御柱のマスか（デザイン13）。足元に開けた地面を z=0 で敷くので、
            //! 大鳥居の脚と同じく下敷きを敷かない（同じ高さと z-fight する）。
            if (use_town && !town->quad_walls.empty()) {
                for (const auto &spot : town->quad_walls) {
                    if ((gx >= spot[0]) && (gx <= (spot[0] + 1)) && (gy >= spot[1])
                        && (gy <= (spot[1] + 1))) {
                        torii_leg_here = true;
                        break;
                    }
                }
            }
            //! このマスが祠の入口で、表が門（鳥居）を名指しているか。>= 0 なら対応表の
            //! 既定の物（変愚の石のアーチ）は**置かない**（下の通常経路が見る）。
            int lone_gate_here = -1;
            /*
             * **屋敷の屋内か**（デザイン4 第 2 段。紅魔館の前庭の彫り込み）。
             * bbox の中の歩けるマスは館の中——頭上に屋根の蓋を敷き、館の壁に面した辺へ
             * 内壁を貼り、地形 id が表の印（螺旋階段・本の机）に当たればそれを置く。
             */
            bool manor_inside = false;
            int manor_mark_here = -1;
            /*
             * 屋内は**歩けるマスだけ**（None / Path / Turf）。bbox 内には内塀（Rampart）も
             * あり、そこまで屋内に数えると **(5) が胸壁用に立てた fences ビットを内壁と
             * 誤読して、館の正面に赤板張りの帯が出た**（実測）。
             */
            const bool walkable_role = (town_role == TownRole::None) || (town_role == TownRole::Path)
                || (town_role == TownRole::Turf);
            if (use_town && !town->manors.empty() && walkable_role) {
                for (const auto &mbox : town->manors) {
                    if ((gx >= mbox[0]) && (gx <= mbox[2]) && (gy >= mbox[1]) && (gy <= mbox[3])) {
                        manor_inside = true;
                        break;
                    }
                }
                /*
                 * さらに**幅 20 マス以下の彫り込みだけ**に絞る（東西へ歩けるマスを辿り、
                 * 両側とも館の壁に当たること）。紅魔館の南面には**全長 100 マスのテラス**
                 * （幅 2 行の歩ける帯）が bbox の中にあり、そこまで屋内に数えると
                 * 北辺の内壁が館の正面いっぱいに立って赤白の縞に見えた（実測）。
                 * 前庭の彫り込み（幅 15）は残る。
                 */
                if (manor_inside) {
                    int width_walked = 0;
                    bool hit_w = false;
                    bool hit_e = false;
                    for (int d = 1; d <= 21; ++d) {
                        const TownRole r = town->role_at(gx - d, gy);
                        if (r == TownRole::Manor) {
                            hit_w = true;
                            break;
                        }
                        if ((r != TownRole::None) && (r != TownRole::Path) && (r != TownRole::Turf)) {
                            break;
                        }
                        ++width_walked;
                    }
                    for (int d = 1; (d <= 21) && hit_w; ++d) {
                        const TownRole r = town->role_at(gx + d, gy);
                        if (r == TownRole::Manor) {
                            hit_e = true;
                            break;
                        }
                        if ((r != TownRole::None) && (r != TownRole::Path) && (r != TownRole::Turf)) {
                            break;
                        }
                        ++width_walked;
                    }
                    manor_inside = hit_w && hit_e && (width_walked <= 20);
                }
                if (manor_inside && !townset.manor_marks.empty() && (memory != nullptr)) {
                    const std::uint16_t id = memory->id_at(gx, gy);
                    if (id != 0) {
                        for (const auto &mark : townset.manor_marks) {
                            if (mark.first == static_cast<int>(id)) {
                                manor_mark_here = mark.second;
                                break;
                            }
                        }
                    }
                }
            }
            /*
             * 町のマスのうち**前庭だけは下敷きを敷かない。**前庭には砂利の地面
             * （`yard_ground`）を天面 z=0 で敷くので、同じ高さの下敷きと z-fight する。
             * 前庭には抜くべき壁が立っていないので、下敷きの役目（カットアウェイが
             * 抜いた先を見せる）も無い。
             */
            if (((role == CellRole::StructuralWall) || (role == CellRole::Bedrock))
                && (town_role != TownRole::Yard) && (shrine_prop_kind < 0) && !torii_leg_here) {
                InstanceData under;
                under.x = fx;
                under.y = fy;
                under.z = 0.f;
                //! **岩盤は黒に近い灰**（決めたこと。抜いたとき床・壁・岩を色で見分ける）。
                tint(under, (role == CellRole::Bedrock) ? kUnderBedrock : kUnderWall, seed, 0.05f);
                /*
                 * **覆いの町では沈める**（2026-08-18 に気づいた その3:「通常の透過する
                 * ときの手前のブロックの床面を描画するルールが天井透過にも効いてないか？」）。
                 * 下敷きは**影を受けない**約束（描く側が set_shadow_scale(0) で描く——層 1 の
                 * 小さな抜きで「そこが何だったか」の色を読ませるため）なので、岩天井の下でも
                 * 太陽の明るさのまま描かれ、屋根外しの穴の中に**日向の床が浮いて見えた**。
                 * 山の中に日は差さない——覆いの町では色そのものを洞窟の暗さへ落とす。
                 */
                if (out.covered) {
                    //! 0.22 では穴の中でまだ 1 段浮いた（実写）。洞窟の底の闇まで沈める。
                    under.r *= 0.10f;
                    under.g *= 0.10f;
                    under.b *= 0.12f;
                }
                out.under_slabs.push_back(under);
            }

            /*
             * 敷地を持たない入口の目印（上の走査で拾ったもの。人里の龍神像）。
             * マスの役割は `None` のままなので、地面は下の従来の経路がそのまま敷く。
             * 目印だけを**入口の 1 マス北**（祠の内側）に立てる。
             */
            if (!lone_marks.empty()) {
                for (const auto &mark : lone_marks) {
                    if ((mark[0] == gx) && (mark[1] == gy)) {
                        static constexpr float kMarkPlain[3] = { 1.f, 1.f, 1.f };
                        //! 目印は**開いた側の隣**へ（拾う側が向きを決めてある。-1 なら無し）。
                        if (mark[2] >= 0) {
                            place_town(mark[2], fx + static_cast<float>(mark[4]),
                                fy + static_cast<float>(mark[5]), -kFootingDepth, kMarkPlain,
                                top_of(mark[2]));
                            /*
                             * **夜のライトアップ**（2026-08-18 に決めた「龍神像を夜は
                             * 下からライトアップさせることは可能？」）。目印の足元の南
                             * （カメラ側）に暖色の点光源を 2 灯。低い位置から照らすので
                             * **距離減衰で裾が明るく頭へ向かって消える**。桜（デザイン6）にも
                             * そのまま効く——夜桜のライトアップになる。
                             * 夜の度合いは光を組み立てる側（`hd2d_app`）が掛ける。
                             */
                            const float mx = fx + static_cast<float>(mark[4]);
                            const float my = fy + static_cast<float>(mark[5]);
                            //! 灯の置き場は**目印の 1 マス南**（カメラ側の足元）。人里の龍神像は
                            //! 目印が入口の 1 マス北なので、従来の (fx, fy+0.2) と同じ座標になる。
                            out.lights.push_back({ mx + 0.5f - 0.9f, my + 1.2f, 0.08f, 3.6f,
                                1.00f, 0.86f, 0.62f, 1.7f });
                            out.lights.push_back({ mx + 0.5f + 0.9f, my + 1.2f, 0.08f, 3.6f,
                                1.00f, 0.86f, 0.62f, 1.7f });
                        }
                        /*
                         * 入口の門（2026-08-18。「素木の鳥居にしよう」と決めた）。
                         * 表の `gate` 行が名指したものを**入口のマスそのもの**に立てる
                         * （鳥居は開いた枠なので、くぐるマスに立てて絵が塞がらない。
                         * 敷地の門のアーチと同じ扱い）。対応表の既定（石のアーチ）は
                         * `lone_gate_here` を見た通常経路が置かずに済ませる。
                         */
                        if (mark[3] >= 0) {
                            place_town(mark[3], fx, fy, -kFootingDepth, kMarkPlain, top_of(mark[3]));
                            lone_gate_here = mark[3];
                        }
                        break;
                    }
                }
            }

            /*
             * **区画に属さない 1 点**（`features`。門楼・
             * 大木・さらし台など）。置く先の役割は問わない（`Rampart`/`Tower` でもよい
             * ——門楼は街道が塀を抜けるマスに立てる）ので、下の `town_structure` の
             * 判定より前に置く。2 マス幅の素材は `at` のマスを西端として置く
             * （プレハブの原点はマスの隅なので、そのまま置けば東隣へはみ出す）。
             * 天面の広い素材（大木など）は既存の木と同じく `top_of` を `cover_top` に
             * 渡すので、見通しの刈り込みにも載る。
             */
            if (!townset.features.empty()) {
                static constexpr float kFeaturePlain[3] = { 1.f, 1.f, 1.f };
                for (const auto &feat : townset.features) {
                    if ((feat.at_x == gx) && (feat.at_y == gy)) {
                        place_town(feat.prefab, fx, fy, -kFootingDepth, kFeaturePlain, top_of(feat.prefab));
                        //! 火の相方（灯り）。夜、`always` なら昼も。街灯の火と同じ流儀。
                        if (feat.flame >= 0) {
                            const float strength = feat.always ? 1.f : night_eff;
                            if (strength > 0.02f) {
                                place_town(feat.flame, fx, fy, -kFootingDepth, kFeaturePlain, 0.f,
                                    feat.glow[0] * 2.2f * strength, feat.glow[1] * 2.2f * strength,
                                    feat.glow[2] * 2.2f * strength);
                            }
                        }
                        ++out.prop_count;
                        break;
                    }
                }
            }

            /*
             * ---- 町（P10 第 2 期・設計書 §10）------------------------------------
             * 敷地の分解は `town_plan.cpp` が済ませてある。ここは**置くだけ**。
             * `Border`（世界の枠）は町の扱いにしないので、下の従来の経路へ落ちる。
             */
            const bool town_structure = (town_role == TownRole::Rampart) || (town_role == TownRole::Tower)
                || (town_role == TownRole::Gate) || (town_role == TownRole::Yard)
                || (town_role == TownRole::House) || (town_role == TownRole::Fountain)
                || (town_role == TownRole::Crag) || (town_role == TownRole::Manor);
            if (use_town && town_structure) {
                static constexpr float kPlain[3] = { 1.f, 1.f, 1.f };
                const std::int16_t site_index = town->site_at(gx, gy);
                const TownSite *const site = ((site_index >= 0)
                                                 && (site_index < static_cast<std::int16_t>(town->sites.size())))
                    ? &town->sites[static_cast<std::size_t>(site_index)]
                    : nullptr;
                //! この敷地の目印（0 = 無し / 1 = 城 / 2 = 名前のプレハブ）。
                const std::uint8_t mark = ((site != nullptr) && (site_index < static_cast<std::int16_t>(site_mark.size())))
                    ? site_mark[static_cast<std::size_t>(site_index)]
                    : 0u;
                const bool castle = (mark == 1u) && (townset.castle_keep[0] >= 0);

                switch (town_role) {
                case TownRole::Rampart:
                case TownRole::Tower: {
                    /*
                     * 町を囲む塀（§10.3「外周に接する成分 → 城壁・櫓」）。**辺境なので石積み
                     * ではなく丸太**（2026-08-09 に決めた:「町を囲む塀は丸太を組み合わせた
                     * 塀に。四隅には木造のやぐらを」）。
                     *
                     * 丸太は向きを持たないので、厚み 1 マスでも 3 マスでもそのまま並べられる。
                     * どのマスがやぐらかは `town_plan.cpp` が決める（成分ごとに四隅の 4 つだけ）。
                     */
                    /*
                     * **小川が塀をくぐる樋**（OUTPOST_TOWN_DESIGN §5 `brook`）。塀の絵の
                     * 代わりに `brook_culvert` を置く——役割は塀のまま（通れないマス）。
                     * **表が折れ線を書いていない町ではここが 1 度も通らない**
                     * （`townset.brook_kind` が空のまま）。
                     */
                    if (!townset.brook_kind.empty()) {
                        const std::size_t brook_idx = (static_cast<std::size_t>(gy)
                                                           * static_cast<std::size_t>(town->width))
                            + static_cast<std::size_t>(gx);
                        if ((townset.brook_kind[brook_idx] != 0xFFu) && (townset.brook_culvert >= 0)) {
                            place_town(townset.brook_culvert, fx, fy, -kFootingDepth, kPlain,
                                top_of(townset.brook_culvert));
                            break;
                        }
                    }
                    /*
                     * **祠の柱の欠片**（2026-08-18 に決めた）。塀の絵の代わりに
                     * **角＝石灯籠・腕の先＝花壇**を土間の上へ置く。1 マス塀の塊は像の腰より
                     * 高く、四方から龍神像を囲んで隠していた。役割は塀のまま（通れないマス）。
                     */
                    if (shrine_prop_kind >= 0) {
                        place_town(townset.ground, fx, fy, 0.f, kPlain);
                        const int pick = (shrine_prop_kind == 1) ? townset.stone_lantern
                                                                 : townset.flowerbed;
                        place_town(pick, fx, fy, -kFootingDepth, kPlain, top_of(pick));
                        /*
                         * **火袋の灯**（2026-08-18 に決めた「石灯籠は夜光らせる事は
                         * 可能？」）。街灯（`lamp_flame`）と同じ流儀——自発光はインスタンス
                         * 単位（罠 39）なので光る板を別体に割り、夜だけ自発光を乗せて置く。
                         * 板は火袋の面の 1 ボクセル外に貼ってあるので、昼の窓と z-fight しない。
                         */
                        if ((shrine_prop_kind == 1) && (townset.stone_lantern_flame >= 0)
                            && (night_eff > 0.02f)) {
                            const float glow = night_eff * 2.1f;
                            place_town(townset.stone_lantern_flame, fx, fy, -kFootingDepth, kPlain,
                                0.f, glow, glow * 0.74f, glow * 0.36f);
                        }
                        break;
                    }
                    /*
                     * **1 マスだけの塀の塊**（デザイン10・2026-08-19。命蓮寺の千体地蔵）。
                     * どのマスかは `town_plan` が連結成分の大きさで選んである。塀の絵の
                     * 代わりに石仏を置き、足元は境内の地面で受ける（土塀の下敷きは敷かない）。
                     * **素材がライブラリに無ければ集合が空**＝この枝を素通りして従来の塀になる。
                     */
                    if (!townset.lone_wall.empty() && !town->lone_walls.empty()) {
                        bool lone_here = false;
                        for (const auto &spot : town->lone_walls) {
                            if ((spot[0] == gx) && (spot[1] == gy)) {
                                lone_here = true;
                                break;
                            }
                        }
                        if (lone_here) {
                            if (!townset.turf.empty()) {
                                place_town(choose(townset.turf, seed), fx, fy, 0.f, kPlain);
                            }
                            const int pick = choose(townset.lone_wall, seed);
                            place_town(pick, fx, fy, -kFootingDepth, kPlain, top_of(pick));
                            break;
                        }
                    }
                    /*
                     * **御柱**（デザイン13・2026-08-20。守矢神社）。2×2 マスちょうどの塊は
                     * `town_plan` の (1b2) が控えてある。**北西のマスに 1 枚**だけ 2×2 マスぶんの
                     * 型を立て、残り 3 マスは足元の地面だけ敷く（大鳥居の脚と同じ分担）。
                     * 素材がライブラリに無ければ集合が空＝この枝を素通りして従来の塀になる。
                     */
                    if (!townset.quad_wall.empty() && !town->quad_walls.empty()) {
                        bool quad_here = false;
                        bool quad_head = false;
                        for (const auto &spot : town->quad_walls) {
                            if ((gx >= spot[0]) && (gx <= (spot[0] + 1)) && (gy >= spot[1])
                                && (gy <= (spot[1] + 1))) {
                                quad_here = true;
                                quad_head = (gx == spot[0]) && (gy == spot[1]);
                                break;
                            }
                        }
                        if (quad_here) {
                            if (!townset.turf.empty()) {
                                place_town(choose(townset.turf, seed), fx, fy, 0.f, kPlain);
                            }
                            if (quad_head) {
                                const int pick = choose(townset.quad_wall, seed);
                                place_town(pick, fx, fy, -kFootingDepth, kPlain, top_of(pick));
                            }
                            break;
                        }
                    }
                    /*
                     * **参道の大鳥居の脚**（デザイン6・2026-08-19。博麗神社）。
                     * 対の読みは `town_plan` の (1b)。半身の型（柱＋狛犬＋笠木 4.5 マス）を
                     * **各脚の北西のマスに 1 枚**だけ置き、脚の他のマスは足元の地面だけ敷く
                     * （型が 2×2 マスぶんの柱を持っている）。素材がライブラリに無ければ従来どおり
                     * 塀の欠片へ落ちる（下の経路）。
                     */
                    if (!town->torii_gates.empty()) {
                        bool torii_done = false;
                        for (const auto &tg : town->torii_gates) {
                            const bool west_leg = (gx >= tg[0]) && (gx <= tg[2]) && (gy >= tg[1])
                                && (gy <= tg[3]);
                            const bool east_leg = (gx >= tg[4]) && (gx <= tg[6]) && (gy >= tg[5])
                                && (gy <= tg[7]);
                            if (!west_leg && !east_leg) {
                                continue;
                            }
                            const int half = west_leg ? townset.torii_l : townset.torii_r;
                            if (half < 0) {
                                break; // 素材なし。塀の欠片のまま（保険）
                            }
                            //! 足元は参道の脇の開けた地面（塀の下敷きはこのマスでは敷かない）。
                            if (!townset.turf.empty()) {
                                place_town(choose(townset.turf, seed), fx, fy, 0.f, kPlain);
                            }
                            if ((gx == (west_leg ? tg[0] : tg[4])) && (gy == (west_leg ? tg[1] : tg[5]))) {
                                place_town(half, fx, fy, -kFootingDepth, kPlain, top_of(half));
                            }
                            torii_done = true;
                            break;
                        }
                        if (torii_done) {
                            break;
                        }
                    }
                    /*
                     * **閉じた大門**（2026-08-18 に決めた「南側の壁の一部に組み込んで」）。
                     * どのマスかは `town_plan` の (5b) が選ぶ（塀の最南の並びの、切れ目の東隣）。
                     * 素材の色をそのまま見せる（石像と同じ流儀。土塀の色むらは受けない）。
                     * 役割は塀のままなので、胸壁や数の検査はここを通らない側で従来どおり動く。
                     */
                    if ((town_role == TownRole::Rampart) && (townset.great_gate >= 0)
                        && !town->great_gates.empty()) {
                        bool is_gate = false;
                        for (const auto &gate : town->great_gates) {
                            if ((gx == gate[0]) && (gy == gate[1])) {
                                is_gate = true;
                                break;
                            }
                        }
                        if (is_gate) {
                            place_town(townset.great_gate, fx, fy, -kFootingDepth, kPlain,
                                top_of(townset.great_gate));
                            break;
                        }
                    }
                    /*
                     * **ダンジョンの口の手前は低い岩棚**（2026-08-27 に決めた:
                     * 「鉄獄の入り口が丸太の山に囲まれて見えないので**手前の丸太は
                     *   背の低い踏破不可のオブジェクト**で置き換えて」）。
                     *
                     * どのマスかは `town_plan` が `slices` に `kRampartLow` で入れてある
                     * （岩山の段と同じ分担）。**塀のままより先に見る**——氷壁の差し替えは
                     * 飾りだが、こちらは「入口が見えるか」なので優先する。
                     * ライブラリに岩棚が 1 枚も無い環境では従来どおりの塀に落ちる。
                     */
                    if ((town_role == TownRole::Rampart) && !townset.crag_ledge.empty()
                        && (static_cast<std::uint8_t>(town->slice_at(gx, gy)) == kRampartLow)) {
                        const int ledge = choose(townset.crag_ledge, seed);
                        place_town(ledge, fx, fy, -kFootingDepth, kPlain, top_of(ledge));
                        break;
                    }
                    /*
                     * **塀のマスの絵の差し替え**（デザイン4・2026-08-18。紅魔館の氷壁）。
                     * コア固有の地形 id は表（town_styles.jsonc）が持ち、ここは id を
                     * 突き合わせて茎名の変種を置くだけ。id は間近で見たマスにしか無い
                     * （罠 40）ので、見るまでは塀の絵で立つ——16 マスの飾りなので許容。
                     */
                    if ((town_role == TownRole::Rampart) && !townset.rampart_overrides.empty()
                        && (memory != nullptr)) {
                        const std::uint16_t id = memory->id_at(gx, gy);
                        int over_pick = -1;
                        for (const auto &over : townset.rampart_overrides) {
                            if ((id != 0) && (over.first == static_cast<int>(id))
                                && !over.second.empty()) {
                                over_pick = choose(over.second, seed);
                                break;
                            }
                        }
                        if (over_pick >= 0) {
                            place_town(over_pick, fx, fy, -kFootingDepth, kPlain, top_of(over_pick));
                            break;
                        }
                    }
                    /*
                     * 覆いの町では櫓を立てない（2026-08-18 その3）——見張り台は空の下の
                     * 建物で、岩天井の下では頭を天井へ埋めるだけになる。角のマスも岩肌で埋める。
                     */
                    /*
                     * **柵は帯の外周 1 列だけ**（2026-08-19 に決めた デザイン5:
                     * 「建物を囲む柵は外周1マスだけ土台の上に　柵内側は土台だけに」）。
                     * 地図の塀は 2〜4 マス幅の帯で、全マスに鉄柵を立てると棒の列が重なって
                     * 館を隠した。外周の判定は**館の重心から遠ざかる向きの隣が塀でない**
                     * こと——東西・南北それぞれの外向きを見るので、コの字に回る帯でも
                     * 館に背を向けた 1 列だけが柵になる。土台だけの絵（`palisade_base`）を
                     * ライブラリに持つ意匠だけがこの読みに入る（変愚の塀は従来どおり全マス柵）。
                     */
                    bool rampart_base_only = false;
                    if ((town_role == TownRole::Rampart) && !townset.palisade_base.empty()
                        && !town->manors.empty()) {
                        const auto &mbox = town->manors.front();
                        const int dx = gx - ((mbox[0] + mbox[2]) / 2);
                        const int dy = gy - ((mbox[1] + mbox[3]) / 2);
                        bool outer_rim = false;
                        if (dx != 0) {
                            const TownRole r = town->role_at(gx + ((dx > 0) ? 1 : -1), gy);
                            outer_rim = (r != TownRole::Rampart) && (r != TownRole::Tower);
                        }
                        if (!outer_rim && (dy != 0)) {
                            const TownRole r = town->role_at(gx, gy + ((dy > 0) ? 1 : -1));
                            outer_rim = (r != TownRole::Rampart) && (r != TownRole::Tower);
                        }
                        rampart_base_only = !outer_rim;
                    }
                    const int pick = ((town_role == TownRole::Tower) && !out.covered)
                        ? townset.tower
                        : (rampart_base_only
                                ? choose(townset.palisade_base, seed)
                                : (townset.palisade.empty() ? -1 : choose(townset.palisade, seed)));
                    const float top = top_of(pick);
                    float rgb[3]{ 1.f, 1.f, 1.f };
                    const float shade = 1.f + (next_range(seed, -1.f, 1.f) * 0.06f * mottle);
                    rgb[0] = rgb[1] = rgb[2] = shade;
                    //! 根元を埋めるのは壁と同じ（罠 4）。天面は素材の高さのまま。
                    place_town(pick, fx, fy, -kFootingDepth, rgb, top);
                    /*
                     * **塀の上の胸壁**（2026-08-09 に決めた。モリバントだけ）。
                     * 柵と同じ「1 枚 1 辺」で、`town_plan` が立てたビットの辺にだけ載せる
                     * （マスごとに四方へ載せると格子に見える＝罠 71）。
                     * やぐらには載せない（円塔は自分で狭間を持っている）。
                     */
                    if (town_role == TownRole::Rampart) {
                        const std::uint8_t edges = town->fence_at(gx, gy);
                        for (int i = 0; i < 4; ++i) {
                            if (((edges & (1u << i)) != 0u) && (townset.parapet[i] >= 0)) {
                                //! 塀の天面へ載せる。**根元を埋めた分を戻す**（罠 4 の裏）。
                                place_town(townset.parapet[i], fx, fy, top - kFootingDepth, rgb,
                                    top + top_of(townset.parapet[i]));
                            }
                        }
                    }
                    break;
                }
                case TownRole::Crag: {
                    /*
                     * **岩山**（P10 第 3 期。2026-08-10 に決めた:「ズル 山が丸太の塀に
                     * なっているのを複数のマスにわたって表現された岩山にして。岩山には
                     * ちらほらと高山植物が生えているように」）。
                     *
                     * 段（山の外からの深さ）は `town_plan` が `slices` に入れてある。
                     * 麓 1.4 マスから峰 4.9 マスまで 4 段なので、谷から見ると**3〜4 マスかけて
                     * 壁がせり上がる**。1 マスで立ち上がると、材が岩でも塀に見える。
                     */
                    const auto tier = std::min<std::size_t>(
                        static_cast<std::size_t>(town->slice_at(gx, gy)), 3);
                    const std::vector<int> &set = townset.crag[tier];
                    const int pick = set.empty() ? -1 : choose(set, seed);
                    const float top = top_of(pick);
                    float rgb[3]{ 1.f, 1.f, 1.f };
                    const float shade = 1.f + (next_range(seed, -1.f, 1.f) * 0.05f * mottle);
                    rgb[0] = rgb[1] = rgb[2] = shade;
                    place_town(pick, fx, fy, -kFootingDepth, rgb, top);
                    /*
                     * 高山植物は**天面へ**。「ちらほら」なので 8 マスに 1 株ほど。
                     * 岩を植えた分だけ根元を埋めてあるので、載せる高さもその分だけ下げる
                     * （罠 4 の裏。胸壁と同じ手当て）。
                     */
                    //! 覆いの町では生やさない（天面は岩天井に接して見えないし、日も差さない）。
                    if (!out.covered && !townset.alpine.empty() && (next01(seed) < 0.13f) && (top > 0.f)) {
                        const float plant_x = next_range(seed, -0.24f, 0.24f);
                        const float plant_y = next_range(seed, -0.24f, 0.24f);
                        place_town(choose(townset.alpine, seed), fx + plant_x, fy + plant_y,
                            top - kFootingDepth, kPlain);
                        ++out.prop_count;
                    }
                    break;
                }
                case TownRole::Fountain: {
                    /*
                     * 噴水（2026-08-09 に決めた:「中央の池は動的な水しぶきを高く噴き上げる
                     * 噴水を中央に。水しぶきが 4 本の柱にたれ落ちる感じで」）。
                     *
                     * どのマスが何かは `town_plan` が `slices` に入れてある
                     * （0 = 柱 / 1 = 噴き上げ / 2 = 縁）。
                     */
                    const auto kind = static_cast<std::uint8_t>(town->slice_at(gx, gy));
                    if (kind == 0u) {
                        const int pillar = townset.fountain_pillar;
                        place_town(pillar, fx, fy, -kFootingDepth, kPlain, top_of(pillar));
                        break;
                    }
                    if (kind == 1u) {
                        //! **噴き上げは 3×3 マス**なので、中央のマスから 1 つ北西へずらして置く。
                        const int spout = townset.fountain_jet;
                        place_town(spout, fx - 1.f, fy - 1.f, 0.f, kPlain, top_of(spout));
                        break;
                    }
                    /*
                     * 縁（2026-08-09 に決めた:「噴水の周りが壁では違和感があるので、
                     * **低い柵と柵の内側には花壇**を設置しよう」）。
                     * 柵は前庭と同じ素材（意匠つき）で、**外を向いた辺だけ**（罠 71）。
                     */
                    place_town(townset.ground, fx, fy, 0.f, kPlain);
                    const std::uint8_t rails = town->fence_at(gx, gy);
                    for (int i = 0; i < 4; ++i) {
                        if ((rails & (1u << i)) != 0u) {
                            place_town(townset.fence[i], fx, fy, -kFootingDepth, kPlain);
                        }
                    }
                    //! 花壇は**マスの中央**へ。ここは通れないマスなので、寄せる理由が無い。
                    place_town(townset.flowerbed, fx, fy, -kFootingDepth, kPlain);
                    ++out.prop_count;
                    break;
                }
                case TownRole::Gate: {
                    //! 門の前は人が集まるので**踏み固められた土**（通り道と同じ地面）。
                    if (!townset.path.empty()) {
                        place_town(choose(townset.path, seed), fx, fy, 0.f, kPlain);
                    }
                    const bool ew = static_cast<std::uint8_t>(town->slice_at(gx, gy)) != 0u;
                    /*
                     * **城門**（2026-08-09 に決めた:「城の入り口だけは看板を取りやめて城門に」
                     * 「入り口は一つの大きな門で組もう」）。
                     *
                     * マスごとにアーチを立てると、幅のある入口で**同じ門が並ぶ**。
                     * 「一つの大きな門」は**並びの両端に脇柱・間に梁**で作る。
                     * 看板は立てない（城に店の看板は要らない）。
                     */
                    if (castle && (townset.gate_pier >= 0)) {
                        const int run_dx = ew ? 0 : 1;
                        const int run_dy = ew ? 1 : 0;
                        const auto same_gate = [&](int dx, int dy) {
                            return (town->role_at(gx + dx, gy + dy) == TownRole::Gate)
                                && (town->site_at(gx + dx, gy + dy) == site_index);
                        };
                        const bool end = !same_gate(-run_dx, -run_dy) || !same_gate(run_dx, run_dy);
                        if (end) {
                            place_town(townset.gate_pier, fx, fy, -kFootingDepth, kPlain,
                                top_of(townset.gate_pier));
                        } else {
                            const int beam = ew ? townset.gate_lintel_ew : townset.gate_lintel;
                            place_town(beam, fx, fy, -kFootingDepth, kPlain, top_of(beam));
                        }
                        break;
                    }
                    /*
                     * 店の看板（2026-08-09 に決めた。**1 マス幅**へ縮めた）。
                     *
                     * どの絵かは**表が持つ**（`TerrainRule::sign` と `sign_ew`。§9.4 を
                     * ここでも守る）。まだ間近で見ていない門は種別が分からないので無地の板に
                     * なる——「遠くの看板は読めない」ので、絵として辻褄も合う。
                     *
                     * **板は常に南を向く**（2026-08-09 に決めた:「建物の看板は常に
                     * 南向に」）。姿勢の対は持たず、**向きはマスをずらして吸収する**。
                     *
                     * | 門の開く向き | 看板のマス |
                     * |---|---|
                     * | 南北（＝北側に入口がある建物も含む） | 門の**横**（左右のマス。種で選ぶ） |
                     * | **東西** | 門の**1 つ北**のマス |
                     */
                    /*
                     * 看板は**入口 1 つにつき 1 枚**（P10 第 2 期レビュー 3）。広い入口
                     * （テルモラの城は 4 マス）でマスごとに出すと、同じ絵が 4 枚並ぶうえ、
                     * 板の立ち場所が**隣の門のマス**になって入口を塞ぐ。
                     * 並びの先頭（南北に開く門なら西端、東西に開く門なら北端）だけが出す。
                     */
                    const int run_dx = ew ? 0 : -1;
                    const int run_dy = ew ? -1 : 0;
                    const bool head_of_run = (town->role_at(gx + run_dx, gy + run_dy) != TownRole::Gate)
                        || (town->site_at(gx + run_dx, gy + run_dy) != site_index);
                    if (!head_of_run) {
                        break;
                    }
                    int sign_pick = townset.sign_plain;
                    bool real_entrance = false;
                    /*
                     * **入口ごとの門**（2026-08-17 の第 3 案「店は暖簾、建物と種別不明の門は木戸」）。
                     * どちらを立てるかは**町ごとの表がその入口の地形に対して名指す**ので、
                     * **UI は「店とは何か」を知らない**（知識はデータの側にある）。
                     * -1 のままなら意匠の既定（変愚では石のアーチ）。
                     */
                    int gate_pick = -1;
                    int gate_pick_ew = -1;
                    if (memory != nullptr) {
                        const std::uint16_t detail_id = memory->id_at(gx, gy);
                        const TerrainRule *const detail = (detail_id != 0)
                            ? library->rule_for_terrain(detail_id, surface_floor)
                            : nullptr;
                        if ((detail != nullptr) && !detail->sign.empty()) {
                            sign_pick = choose(detail->sign, seed);
                            real_entrance = true;
                        }
                        /*
                         * **建物は町ごとの表が勝つ**（P10 第 4 期）。`BUILDING_n` は
                         * 町ごとに別の建物なので、terrain id で引く対応表には
                         * 正しい絵を書けない（対応表の側は無地の板にしてある）。
                         */
                        for (const auto &row : townset.building_signs) {
                            if (row.terrain_id == static_cast<int>(detail_id)) {
                                sign_pick = row.sign;
                                real_entrance = true;
                                if (row.gate >= 0) {
                                    gate_pick = row.gate;
                                    gate_pick_ew = (row.gate_ew >= 0) ? row.gate_ew : row.gate;
                                }
                                break;
                            }
                        }
                    }
                    /*
                     * **種別の分からない門には何も立てない**意匠がある（気づいたこと
                     * 2026-08-09:「ほかの場所にも小物として使っている箇所があるから撤去して」）。
                     *
                     * 門は**並びの形**から読むので、実データに店の無い所も門になる。そこへ
                     * 絵の無い板と石のアーチを立てると、石の都では**街じゅうに白い板とアーチが
                     * 並ぶ飾り**に見えた。表が看板の絵を持っている門だけに立てる。
                     */
                    if (!real_entrance && !style.blank_gate_props) {
                        break;
                    }
                    /*
                     * 自立した門（§10.3-1「入口に裏が無くなる」）。**くぐる向きに合わせる**
                     * ——扉の 2 姿勢（P10 レビュー 11）と同じ流儀で、意匠は 1 つ・姿勢だけ 2 つ。
                     * **看板と同じく入口 1 つにつき 1 基**（並びの先頭だけが出す）。
                     */
                    //! 表が名指した門があればそれ、無ければ意匠の既定。
                    const int arch = (gate_pick >= 0) ? (ew ? gate_pick_ew : gate_pick)
                                                      : (ew ? townset.arch_ew : townset.arch);
                    place_town(arch, fx, fy, -kFootingDepth, kPlain, top_of(arch));
                    /*
                     * **門の灯**（OUTPOST_TOWN_DESIGN §5 `gate_flame`）。街灯の火
                     * （`lamp_flame`）と同じ流儀——自発光はインスタンス単位（罠 39）なので
                     * 別体を夜だけ重ねる。表に書いていない町では `gate_flame < 0` のまま
                     * なので、この枝は 1 度も通らない。
                     */
                    if ((townset.gate_flame >= 0) && (night_eff > 0.02f)) {
                        const float glow = night_eff * 2.1f;
                        place_town(townset.gate_flame, fx, fy, -kFootingDepth, kPlain, 0.f, glow,
                            glow * 0.74f, glow * 0.42f);
                    }
                    int sign_x = gx;
                    int sign_y = gy - 1; //!< 東西に開く門は「入口の 1 マス北」
                    /*
                     * **山肌に開いた入口は別**（P10 第 3 期。2026-08-10 に決めた で
                     * ズルの山を岩山にしたら出た）。敷地の門は隣が前庭なので板を置けるが、
                     * 山肌の穴は**三方が岩**で、そこへ置くと**板が崖に埋まる**（実機の絵で
                     * 見つけた。柱が岩を貫いて板だけが山の中腹に浮いていた）。
                     *
                     * 板は常に南を向くので**開いている側のマスへ出せば読める**。真正面は
                     * 穴を塞ぐので、**斜め前**を先に試して、駄目なら真ん前へ落とす。
                     */
                    const bool in_cliff = (site == nullptr);
                    if (in_cliff) {
                        //! 開いている隣（山肌の入口は 1 方向しか開いていない）。
                        int odx = 0;
                        int ody = 0;
                        for (int i = 0; i < 4; ++i) {
                            const int nx = gx + ((i == 2) ? -1 : ((i == 3) ? 1 : 0));
                            const int ny = gy + ((i == 0) ? -1 : ((i == 1) ? 1 : 0));
                            if (town->role_at(nx, ny) != TownRole::Crag) {
                                odx = nx - gx;
                                ody = ny - gy;
                                break;
                            }
                        }
                        //! 谷底（歩ける所）か。岩・建物・柵のマスには立てない。
                        const auto open_cell = [town](int nx, int ny) {
                            const TownRole side = town->role_at(nx, ny);
                            return (side == TownRole::None) || (side == TownRole::Path)
                                || (side == TownRole::Turf);
                        };
                        const int latx = -ody; //!< くぐる向きに直交する向き
                        const int laty = odx;
                        const int first = (next01(seed) < 0.5f) ? 1 : -1;
                        sign_x = gx + odx;
                        sign_y = gy + ody;
                        for (int attempt = 0; attempt < 2; ++attempt) {
                            const int side = (attempt == 0) ? first : -first;
                            const int cx = gx + odx + (latx * side);
                            const int cy = gy + ody + (laty * side);
                            if (open_cell(cx, cy)) {
                                sign_x = cx;
                                sign_y = cy;
                                break;
                            }
                        }
                    } else if (!ew) {
                        //! 南北に開く門は**門の横**。種で左右を選び、片方が使えなければ逆へ。
                        const int first = (next01(seed) < 0.5f) ? 1 : -1;
                        for (int attempt = 0; attempt < 2; ++attempt) {
                            const int dir = (attempt == 0) ? first : -first;
                            if (town->role_at(gx + dir, gy) != TownRole::Gate) {
                                sign_x = gx + dir;
                                sign_y = gy;
                                break;
                            }
                        }
                    } else {
                        /*
                         * **東西にくぐる門の北が壁のことがある**（2026-09-06）。縦に並ぶ入口の
                         * 先頭は、その北が建物や城壁になりやすい（タロスの宮殿は縦 4 マス）。
                         * 北が塞がっていれば南を試す。どちらも駄目なら従来どおり北へ置く
                         * ——**看板は 1 枚に保つ**（置き場所を探して 2 枚にはしない）。
                         */
                        const auto sign_ok = [town](int nx, int ny) {
                            const TownRole side = town->role_at(nx, ny);
                            return (side == TownRole::None) || (side == TownRole::Path)
                                || (side == TownRole::Turf) || (side == TownRole::Yard);
                        };
                        if (!sign_ok(gx, gy - 1) && sign_ok(gx, gy + 1)) {
                            sign_y = gy + 1;
                        }
                    }
                    /*
                     * **看板の台**（`sign_base`。テルモラ §9.13）。台を根元に沈めて置き、看板は
                     * 台の見えている高さ（`top_of` から沈めたぶんを引いた所）へ載せる。
                     */
                    float sign_lift = 0.f;
                    if (townset.sign_base >= 0) {
                        place_town(townset.sign_base, static_cast<float>(sign_x), static_cast<float>(sign_y),
                            -kFootingDepth, kPlain, top_of(townset.sign_base));
                        sign_lift = std::max(0.f, top_of(townset.sign_base) - kFootingDepth);
                    }
                    place_town(sign_pick, static_cast<float>(sign_x), static_cast<float>(sign_y),
                        sign_lift - kFootingDepth, kPlain, sign_lift + top_of(sign_pick));
                    break;
                }
                case TownRole::Yard: {
                    /*
                     * **小川が前庭を横切る**（OUTPOST_TOWN_DESIGN §5 `brook`）。地面を
                     * 差し替えて、柵は従来どおり出すが**小物は置かない**（設計書「前庭の
                     * マスに当たったら前庭の地面の代わりに敷く（小物も置かない）」）。
                     */
                    int brook_ground = -1;
                    if (!townset.brook_kind.empty()) {
                        const std::size_t brook_idx = (static_cast<std::size_t>(gy)
                                                           * static_cast<std::size_t>(town->width))
                            + static_cast<std::size_t>(gx);
                        switch (townset.brook_kind[brook_idx]) {
                        case 0:
                            brook_ground = townset.brook_ns;
                            break;
                        case 1:
                            brook_ground = townset.brook_ew;
                            break;
                        case 2:
                            brook_ground = townset.brook_bend[0];
                            break;
                        case 3:
                            brook_ground = townset.brook_bend[1];
                            break;
                        case 4:
                            brook_ground = townset.brook_bend[2];
                            break;
                        case 5:
                            brook_ground = townset.brook_bend[3];
                            break;
                        default:
                            break;
                        }
                    }
                    //! 前庭の地面（§10.5「街路と明確に違う地面」）。小川があればそちらへ差し替える。
                    place_town((brook_ground >= 0) ? brook_ground : townset.ground, fx, fy, 0.f, kPlain);
                    /*
                     * **建物の入らなかった敷地（露店）の目印**（デザイン6・2026-08-19）。
                     * 博麗の守矢神社分社（3×3 マス・門が塊の中）は、門の前庭の彫り込みで
                     * 家の矩形が取れず露店に落ちる——表の目印（`bunsha_hak`）は House のマスで
                     * しか置かれないので、**どこにも建たなかった**（実測。柵の庭だけが出た）。
                     * 露店の敷地では外接矩形の北西のマスに目印を建てる。柵はそのまま
                     * ——分社を囲う垣になる。
                     */
                    if ((mark == 2u) && (site != nullptr) && !site->has_house()
                        && (gx == site->x0) && (gy == site->y0)
                        && (site_mark_prefab[static_cast<std::size_t>(site_index)] >= 0)) {
                        const int pick = site_mark_prefab[static_cast<std::size_t>(site_index)];
                        place_town(pick, fx, fy, -kFootingDepth, kPlain, top_of(pick));
                    }
                    /*
                     * 柵（§10.3-2）。**敷地の外に面した辺だけ**。1 マスに何辺でも立つ。
                     * **城の敷地では、同じ辺へ 3 マスの城壁**を立てる（決めたこと
                     * 2026-08-09「石組みで巨大な城を組んで」）。枠は柵と同じなので、
                     * 置く側は素材を差し替えるだけでよい。
                     */
                    const std::uint8_t bits = town->fence_at(gx, gy);
                    for (int i = 0; i < 4; ++i) {
                        if ((bits & (1u << i)) == 0u) {
                            continue;
                        }
                        const int rail = (castle && (townset.castle_wall[i] >= 0))
                            ? townset.castle_wall[i]
                            : townset.fence[i];
                        place_town(rail, fx, fy, -kFootingDepth, kPlain,
                            castle ? top_of(rail) : 0.f);
                    }
                    //! **差し替えたマスには小物を置かない**（設計書 §5 `brook`）。
                    if (brook_ground >= 0) {
                        break;
                    }
                    /*
                     * **門の隣のマスには何も置かない**（2026-08-09 に決めた:
                     * 「建物入り口周囲にある街灯のこものは無しに」）。かつてはここに街灯を
                     * 立てていたが、いまは**看板の立ち場所**でもある（南北に開く門は横、
                     * 東西に開く門は 1 つ北）。空けておくことが看板の見えやすさに直結する。
                     */
                    bool next_to_gate = (town->role_at(gx, gy - 1) == TownRole::Gate);
                    for (int i = 0; i < 4; ++i) {
                        const int nx = gx + ((i == 2) ? -1 : ((i == 3) ? 1 : 0));
                        const int ny = gy + ((i == 0) ? -1 : ((i == 1) ? 1 : 0));
                        next_to_gate = next_to_gate
                            || ((town->role_at(nx, ny) == TownRole::Gate) && (town->site_at(nx, ny) == site_index));
                    }
                    //! 目印の敷地（分社）の庭に樽や干し草は撒かない（境内の格が壊れる）。
                    //! **建物の入らなかった敷地だけ**——変愚の目印つき敷地（賢者の塔など）は
                    //! 家があるので従来どおり（1 ボクセルも動かさない約束）。
                    if ((mark == 2u) && (site != nullptr) && !site->has_house()) {
                        break;
                    }
                    /*
                     * **前庭の小物を建物の意味で引く**（OUTPOST_TOWN_DESIGN §5 `yards`/
                     * `sites`）。表が門の地形／敷地の座標を名指した敷地だけがここを通る。
                     * データの無い町は `has_yard_plan` が常に偽なので、従来の 42% 乱択が
                     * 1 ビットも動かない（下の `next01` 呼び出し順も従来のまま）。
                     */
                    const bool has_yard_plan = (site_index >= 0)
                        && (static_cast<std::size_t>(site_index) < site_yard_active.size())
                        && (site_yard_active[static_cast<std::size_t>(site_index)] != 0u);
                    if (next_to_gate || (!has_yard_plan && (next01(seed) >= 0.42f))) {
                        break; // 空き。**全部のマスを埋めない**（前庭は「余白」でもある）
                    }
                    //! 装飾は**マスの四隅へ寄せる**（2026-08-09 に決めた。中央は禁止）。
                    const float corner_pick = next01(seed);
                    const float corner_x = (corner_pick < 0.5f) ? -kPropCornerOffset : kPropCornerOffset;
                    const float corner_y = (std::fmod(corner_pick * 4.f, 2.f) < 1.f) ? -kPropCornerOffset
                                                                                     : kPropCornerOffset;
                    const auto place_yard_prop = [&](const YardPropRuntime &p) {
                        if (p.prefab < 0) {
                            return;
                        }
                        const float px = p.center ? 0.f : corner_x;
                        const float py = p.center ? 0.f : corner_y;
                        place_town(p.prefab, fx + px, fy + py, -kFootingDepth, kPlain, top_of(p.prefab));
                        ++out.prop_count;
                        if ((p.flame >= 0) && (p.always || (night_eff > 0.02f))) {
                            const float k = p.always ? 1.f : night_eff;
                            place_town(p.flame, fx + px, fy + py, -kFootingDepth, kPlain, 0.f,
                                p.glow[0] * k * 2.1f, p.glow[1] * k * 2.1f, p.glow[2] * k * 2.1f);
                        }
                    };
                    if (has_yard_plan) {
                        const SiteYardRuntime &plan = site_yard[static_cast<std::size_t>(site_index)];
                        const std::size_t cell_idx = (static_cast<std::size_t>(gy)
                                                          * static_cast<std::size_t>(town->width))
                            + static_cast<std::size_t>(gx);
                        const int rank = (cell_idx < yard_rank.size()) ? yard_rank[cell_idx] : -1;
                        if ((rank >= 0) && (static_cast<std::size_t>(rank) < plan.props.size())) {
                            place_yard_prop(plan.props[static_cast<std::size_t>(rank)]);
                            break;
                        }
                        //! 使い切った後は `fill` の割合で乱択（`sites` は既定 0 = 乱択しない）。
                        if ((plan.fill > 0.f) && (next01(seed) < plan.fill)) {
                            std::vector<const YardPropRuntime *> pool;
                            for (const auto &p : plan.props) {
                                pool.push_back(&p);
                            }
                            if (plan.use_common_fallback) {
                                for (const auto &p : townset.yard_common) {
                                    pool.push_back(&p);
                                }
                            }
                            if (!pool.empty()) {
                                const std::size_t pick = static_cast<std::size_t>(
                                    next01(seed) * static_cast<float>(pool.size()));
                                place_yard_prop(*pool[std::min(pick, pool.size() - 1)]);
                            }
                        }
                        break;
                    }
                    if (!townset.yard_common.empty()) {
                        //! **行に当たらない門つき敷地**（設計書 §5 `yard_common`）。井戸・樽…の代わり。
                        const std::size_t pick = static_cast<std::size_t>(
                            next01(seed) * static_cast<float>(townset.yard_common.size()));
                        place_yard_prop(
                            townset.yard_common[std::min(pick, townset.yard_common.size() - 1)]);
                        break;
                    }
                    //! **従来どおり**（表が無い町。井戸・花壇・干し草・荷車・樽の 5 択）。
                    const float which = next01(seed);
                    const int prop = (which < 0.22f) ? townset.well
                                                     : ((which < 0.46f) ? townset.flowerbed
                                                                        : ((which < 0.66f) ? townset.haystack
                                                                                           : ((which < 0.84f) ? townset.cart
                                                                                                              : props.barrel)));
                    place_town(prop, fx + corner_x, fy + corner_y, -kFootingDepth, kPlain);
                    ++out.prop_count;
                    break;
                }
                case TownRole::House: {
                    if (site == nullptr) {
                        break;
                    }
                    /*
                     * **目印の建物**（2026-08-09 に決めた）。城なら天守（6 マス）を
                     * 家と同じ 9 スライスで組み、塔なら建物の矩形の左上へ 1 棟だけ置く。
                     */
                    if (castle) {
                        const auto slice = static_cast<std::size_t>(town->slice_at(gx, gy));
                        const int keep = townset.castle_keep[std::min<std::size_t>(slice, 8)];
                        place_town(keep, fx, fy, -kFootingDepth, site->wall_tint, top_of(keep));
                        break;
                    }
                    if ((mark == 2u) && (site_mark_prefab[static_cast<std::size_t>(site_index)] >= 0)) {
                        if ((gx == site->house_x0) && (gy == site->house_y0)) {
                            const int pick = site_mark_prefab[static_cast<std::size_t>(site_index)];
                            place_town(pick, fx, fy, -kFootingDepth, kPlain, top_of(pick));
                        }
                        break;
                    }
                    if (site->landmark != 0) {
                        /*
                         * 目印の建物（試作由来の shop / mill）。**矩形の左上のマスにだけ**置く
                         * （素材そのものが 4×4 / 4×3 マスぶんの大きさを持っている）。
                         */
                        if ((gx == site->house_x0) && (gy == site->house_y0)) {
                            const int pick = townset.landmark[site->landmark];
                            place_town(pick, fx, fy, -kFootingDepth, kPlain, top_of(pick));
                        }
                        break;
                    }
                    const auto slice = static_cast<std::size_t>(town->slice_at(gx, gy));
                    /*
                     * **敷地ごとの上書き**（OUTPOST_TOWN_DESIGN §5 `yards`/`sites`）。
                     * `site_index` が範囲外、または表がその敷地を名指していなければ
                     * 全部が既定（`townset.wall/roof/entrance` と `site->wall_tint/roof_tint/
                     * storeys`）のまま——**表の無い町は 1 ボクセルも動かない**。
                     */
                    const std::size_t sidx = (site_index >= 0) ? static_cast<std::size_t>(site_index)
                                                                : site_wall_override.size();
                    const std::size_t slice_at = std::min<std::size_t>(slice, 8);
                    const bool has_wall_override
                        = (sidx < site_wall_override.size()) && (site_wall_override[sidx][slice_at] >= 0);
                    const bool has_roof_override
                        = (sidx < site_roof_override.size()) && (site_roof_override[sidx][slice_at] >= 0);
                    const int wall = has_wall_override ? site_wall_override[sidx][slice_at]
                                                        : townset.wall[slice_at];
                    const int roof = has_roof_override ? site_roof_override[sidx][slice_at]
                                                        : townset.roof[slice_at];
                    const int entrance_idx = ((sidx < site_entrance_override.size())
                                                  && (site_entrance_override[sidx] >= 0))
                        ? site_entrance_override[sidx]
                        : townset.entrance;
                    const float *const wall_tint = ((sidx < site_wall_tint_has.size())
                                                        && (site_wall_tint_has[sidx] != 0u))
                        ? site_wall_tint_override[sidx].data()
                        : site->wall_tint;
                    const float *const roof_tint = ((sidx < site_roof_tint_has.size())
                                                        && (site_roof_tint_has[sidx] != 0u))
                        ? site_roof_tint_override[sidx].data()
                        : site->roof_tint;
                    /*
                     * **建物の階数の下限**（デザイン §9.6。テルモラの作り直し「ずっしり」）。
                     * `yards`/`sites` が明示した階数（上書き）は**そのまま優先**——下限とは
                     * 突き合わせない。明示が無いときだけ `site->storeys`（敷地の乱択）と
                     * `min_storeys` の大きいほうを採る。0（既定）なら従来どおり。
                     */
                    const int storeys = ((sidx < site_storeys_override.size())
                                            && (site_storeys_override[sidx] > 0))
                        ? site_storeys_override[sidx]
                        : std::max(site->storeys, style.min_storeys);
                    const float wall_h = top_of(wall);
                    /*
                     * 壁を階数ぶん積む。**1 枚目だけ根元を埋める**（罠 4）。素材の高さは
                     * 保ちたいので、埋めたぶんを sz で戻す（`structure` と同じ手当て）。
                     */
                    for (int storey = 0; storey < storeys; ++storey) {
                        const bool first = (storey == 0);
                        InstanceData inst;
                        const LibraryEntry *entry = nullptr;
                        if (use_lib && (wall >= 0)) {
                            entry = &library->entry(wall);
                            inst.x = fx - entry->anchor_x;
                            inst.y = fy - entry->anchor_y;
                            inst.z = (wall_h * static_cast<float>(storey)) - (first ? kFootingDepth : 0.f);
                            if (first && (wall_h > 0.01f)) {
                                inst.sz = (wall_h + kFootingDepth) / wall_h;
                            }
                            inst.r = wall_tint[0];
                            inst.g = wall_tint[1];
                            inst.b = wall_tint[2];
                            out.lib[static_cast<std::size_t>(wall)].push_back(inst);
                            ++out.lib_instances;
                        }
                        //! 窓の灯り。**南に面した列だけ**（北面はその建物自身に隠れて見えない）。
                        if ((night_eff > 0.02f) && (slice >= 6) && (townset.light >= 0)) {
                            const float glow = night_eff * 2.3f;
                            place_town(townset.light, fx, fy,
                                (wall_h * static_cast<float>(storey)) + 0.40f, kPlain, 0.f, glow,
                                glow * 0.78f, glow * 0.42f);
                        }
                        /*
                         * **見た目だけの玄関**（2026-08-18 に決めた）。町のデータに入口を
                         * 持たない敷地は、窓と屋根だけの閉じた箱になって「人が住んでいる家」に
                         * 見えない。そこで南面（カメラの正面）の中ほどへ引き戸を 1 つ立てる。
                         *
                         * **敷地に 1 つだけ**（マスごとに立てると戸が横一列に並ぶ）。1 階だけ。
                         * 入口を持つ敷地には立てない——あちらは本物の門と看板が立つので、
                         * 玄関が 2 つあるように見える。
                         */
                        if ((storey == 0) && (entrance_idx >= 0) && !site->has_gate
                            && (gy == site->house_y1)
                            && (gx == ((site->house_x0 + site->house_x1) / 2))) {
                            place_town(entrance_idx, fx, fy, 0.f, wall_tint, 0.f);
                        }
                    }
                    const float top = (wall_h * static_cast<float>(storeys)) + top_of(roof);
                    place_town(roof, fx, fy, wall_h * static_cast<float>(storeys), roof_tint, top);
                    /*
                     * **越屋根と煙**（デザイン15・2026-08-20。偽天棚。§4.5 ④「煙が偽天棚の
                     * 署名」）。載せるのは**建物の矩形の真ん中のマス 1 つだけ**である
                     * ——9 スライスの `Middle` に載せると 5×5 の家で 9 つ並んで煙突の林になる。
                     * 煙はプレハブでは描けない（半透明は雲の描き手だけ）ので、型の天面の
                     * 座標を蒸気の口として積む（河童のバザーの配管と同じ道）。
                     */
                    if ((townset.roof_vent >= 0)
                        && (gx == ((site->house_x0 + site->house_x1) / 2))
                        && (gy == ((site->house_y0 + site->house_y1) / 2))) {
                        const float crown = top + top_of(townset.roof_vent);
                        place_town(townset.roof_vent, fx, fy, top, roof_tint, crown);
                        out.steam_jets.push_back({ fx, fy, crown });
                    }
                    break;
                }
                case TownRole::Manor: {
                    /*
                     * **屋敷**（デザイン4 第 2 段。紅魔館の本館）。壁は 3 階一体・屋根は
                     * ひさし出し。スライスは `town_plan` がマスごとの外向きで入れてある。
                     * ライブラリに意匠が無ければ塀に落とす（表だけ立った環境の保険）。
                     */
                    const auto slice = static_cast<std::size_t>(town->slice_at(gx, gy));
                    /*
                     * **棟ごとの材**（デザイン9・2026-08-19。香霖堂）。1 つの壁の塊が
                     * 店舗・土蔵・渡り廊下に読み分けられている（`TownPlan::wings`）ので、
                     * 壁も屋根もこの番号で引く。割っていない町は 0 番しか来ないので、
                     * 引く先はこれまでと同じ材である。
                     */
                    const auto wing = std::min<std::size_t>(town->wing_at(gx, gy), 2);
                    const auto &wingset = townset.manor_wing[wing];
                    const std::vector<int> &wset = wingset.wall[std::min<std::size_t>(slice, 8)];
                    int wall = wset.empty() ? -1 : choose(wset, seed);
                    /*
                     * **ベイの律動**（デザイン6 その3。こう気づいた——「本殿の正面が障子で
                     * 埋め尽くされた」）。切妻の屋敷では壁の変種を乱数でなく**列**で選ぶ——
                     * 板壁・蔀戸・板壁・連子の 4 マス周期が、社寺の「間ごとのベイ」として並ぶ。
                     * 乱数のままだと窓の変種が固まって出る列があり、一人称（北面を正面から
                     * 見る）で面じゅうが格子に読めた。紅魔館（乱数で「窓少なめ」を振る設計）は
                     * 切妻でないので従来どおり。
                     */
                    if ((townset.manor_gable || townset.manor_irimoya) && !wset.empty()) {
                        static constexpr std::size_t kBayPattern[4] = { 0, 1, 0, 2 };
                        /*
                         * **面の走る向きで数える軸を替える**（デザイン8）。切妻の拝殿は
                         * 南北の面しか無いので列（gx）でよかったが、入母屋の大屋敷は
                         * 東西の面も長い——縦の面（スライスの列が中でない）で gx を使うと、
                         * 面に沿って**同じ変種が延々と続く**（律動にならない）。
                         */
                        const int axis = ((slice % 3) != 1) ? gy : gx;
                        const std::size_t idx = kBayPattern[static_cast<std::size_t>(axis) % 4];
                        wall = wset[std::min(idx, wset.size() - 1)];
                    }
                    if (wall < 0) {
                        const int pick = townset.palisade.empty() ? -1 : choose(townset.palisade, seed);
                        place_town(pick, fx, fy, -kFootingDepth, kPlain, top_of(pick));
                        break;
                    }
                    const float wall_h = top_of(wall);
                    /*
                     * **切妻の屋敷**（デザイン6・2026-08-19。博麗神社の拝殿）。
                     * 段 = 外接矩形の北端・南端までの距離の小さいほう。斜面の型
                     * （top_z = 1 段の上がり）を段 × 上がりの高さに積み、北半分と南半分の
                     * 出会う線（dn = ds - 1 の並びの南縁）へ箱棟をまたがせる。
                     * 妻（東西端）と、向拝の裏の段差（北面の段 > 0）には 1 マスの妻壁を
                     * **sz で段の高さへ伸ばして**貼る（cave_mound の z 伸縮と同じ流儀）。
                     */
                    /*
                     * **片流れの屋敷**（デザイン7・2026-08-19。河童のバザーの工房の長屋）。
                     * 段 = **外側の面までの距離**（1 マス目が段 0 ＝軒）。外向きは外接矩形の
                     * 中心から遠ざかる 2 方向のうち、**外へ抜けるまでが短いほう**——
                     * 中庭を囲む 3 マス幅の帯なら、外の 1 列が軒・中庭側の 1 列がいちばん高く、
                     * 高い面は妻壁（`manor_gable_*` を sz で伸ばす）で塞がって
                     * **市庭に向いた 1 枚の壁**になる。棟は無い（片流れなので）。
                     */
                    if (townset.manor_shed) {
                        const std::array<int, 4> *mybox = nullptr;
                        for (const auto &mbox : town->manors) {
                            if ((gx >= mbox[0]) && (gx <= mbox[2]) && (gy >= mbox[1])
                                && (gy <= mbox[3])) {
                                mybox = &mbox;
                                break;
                            }
                        }
                        if (mybox == nullptr) {
                            place_town(wall, fx, fy, -kFootingDepth, kPlain, wall_h);
                            break;
                        }
                        static constexpr int kShedDirs[4][2] = {
                            { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } // N / S / W / E
                        };
                        //! **深さの上限 4 マス。**厚い塊（南辺の 15×4）でも屋根が伸び続けない。
                        constexpr int kShedMaxDepth = 4;
                        const float cx = static_cast<float>((*mybox)[0] + (*mybox)[2]) * 0.5f;
                        const float cy = static_cast<float>((*mybox)[1] + (*mybox)[3]) * 0.5f;
                        const int outward[2] = { (static_cast<float>(gy) <= cy) ? 0 : 1,
                            (static_cast<float>(gx) <= cx) ? 2 : 3 };
                        int dir = outward[0];
                        int depth = kShedMaxDepth + 1;
                        for (const int cand : outward) {
                            int d = 1;
                            while ((d <= kShedMaxDepth)
                                && (town->role_at(gx + (kShedDirs[cand][0] * d),
                                        gy + (kShedDirs[cand][1] * d))
                                    == TownRole::Manor)) {
                                ++d;
                            }
                            if (d < depth) {
                                depth = d;
                                dir = cand;
                            }
                        }
                        const int level = std::min(depth - 1, 3);
                        const int slope = choose(townset.shed_slope[dir], seed);
                        const float rise = top_of(slope);
                        const float ridge_top = wall_h + (rise * static_cast<float>(level + 1));
                        place_town(wall, fx, fy, -kFootingDepth, kPlain, ridge_top);
                        const int piece = ((level == 0) && !townset.shed_eave[dir].empty())
                            ? choose(townset.shed_eave[dir], seed)
                            : slope;
                        /*
                         * **マスごとに色を少し振る**（§7.1-4 の町版）。斜面は姿勢ごとに 1 個
                         * しか無いので、素のまま並べると錆の斑が**格子に揃って**見えた
                         * （合成フレームで実測）。明るさ ±7% と赤みの差で、同じ型でも
                         * 「一枚ずつ葺いたトタン」に読める。
                         */
                        float tin[3]{ 1.f, 1.f, 1.f };
                        const float lum = 1.f + (next_range(seed, -1.f, 1.f) * 0.07f);
                        const float warm = next_range(seed, -1.f, 1.f) * 0.05f;
                        tin[0] = lum + warm;
                        tin[1] = lum;
                        tin[2] = lum - warm;
                        place_town(piece, fx, fy, wall_h + (rise * static_cast<float>(level)),
                            tin, 0.f);
                        //! 段の分だけ開いた縦の面を塞ぐ（外に面した辺だけ。切妻と同じ流儀）。
                        if (level > 0) {
                            for (int i = 0; i < 4; ++i) {
                                if (town->role_at(gx + kShedDirs[i][0], gy + kShedDirs[i][1])
                                    == TownRole::Manor) {
                                    continue;
                                }
                                const int filler = townset.gable_wall[i];
                                if (filler < 0) {
                                    continue;
                                }
                                const LibraryEntry &fe = library->entry(filler);
                                InstanceData inst;
                                inst.x = fx - fe.anchor_x;
                                inst.y = fy - fe.anchor_y;
                                inst.z = wall_h;
                                if (fe.top_z > 0.01f) {
                                    inst.sz = (rise * static_cast<float>(level)) / fe.top_z;
                                }
                                out.lib[static_cast<std::size_t>(filler)].push_back(inst);
                                ++out.lib_instances;
                            }
                        }
                        break;
                    }
                    /*
                     * **入母屋の屋敷**（デザイン8・2026-08-19。永遠亭の大屋敷）。
                     * 切妻は「外接矩形の北端・南端までの距離」を段にするので**矩形の
                     * 平面にしか効かない**——永遠亭は長い北棟から東西 2 つの棟が南へ
                     * 下りる形（960 マス・外接 95×21・充填 0.48）で、外接矩形で測ると
                     * 棟がどこにも通らない。
                     *
                     * そこで**マスごとの距離変換**で組む。4 方向へ「屋敷が続くマス数」を
                     * 数え、短いほうの軸を勾配に、長いほうの軸に棟を通す。段は距離
                     * そのものなので、棟の位置も軒の位置も平面の形から決まる。
                     * **妻側は `manor_hip_break` 段まで隅棟で下ろし、その上は距離で
                     * 縛らない**——ここで開く垂直の面が入母屋の破風である。
                     */
                    if (townset.manor_irimoya) {
                        static constexpr int kIriDirs[4][2] = {
                            { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } // N / S / W / E
                        };
                        /*
                         * **走りの上限。**長い棟（54 マス）を端まで数えても意味が無い。
                         * ただし**奥行きの深い平面では 8 では足りない**（デザイン10・
                         * 命蓮寺の本堂は 21 マス）——中央の行が揃って頭打ちになり、
                         * 棟が何本も並んだ幅広の台になる。表が書いた町だけ深く数える。
                         */
                        const int kMaxRun = (style.manor_run_max > 0) ? style.manor_run_max : 8;
                        constexpr int kFar = 99;
                        const int brk = (style.manor_hip_break > 0) ? style.manor_hip_break : 2;
                        //! 段の上限（0 なら無し）。**上限に達したマスは平らな大棟**で受ける。
                        const int cap = (style.manor_roof_max > 0) ? style.manor_roof_max : kMaxRun;
                        /*
                         * **走りは棟の境で止める**（デザイン9・2026-08-19。香霖堂）。
                         * 距離変換を塊ぜんぶで回すと、店舗と土蔵と渡り廊下に**1 枚の
                         * 大屋根**が架かって「2 棟が渡り廊下でつながった形」が消える。
                         * 棟が変われば別の建物なので、そこで屋敷が切れたものとして数える
                         * ——境のマスは自動的に軒（段 0）になり、棟ごとに屋根が閉じる。
                         * 割っていない町では棟が全部 0 なので、条件は常に真＝従来どおり。
                         */
                        const auto same_manor = [town](int sx, int sy, std::uint8_t wing) {
                            return (town->role_at(sx, sy) == TownRole::Manor)
                                && (town->wing_at(sx, sy) == wing);
                        };
                        const auto here_wing = static_cast<std::uint8_t>(wing);
                        const auto solve = [&](int sx, int sy, int run[4], int &out_level,
                                               int &out_pose, bool *out_flat = nullptr) {
                            const std::uint8_t my_wing = town->wing_at(sx, sy);
                            for (int i = 0; i < 4; ++i) {
                                int d = 0;
                                while ((d < kMaxRun)
                                    && same_manor(sx + (kIriDirs[i][0] * (d + 1)),
                                        sy + (kIriDirs[i][1] * (d + 1)), my_wing)) {
                                    ++d;
                                }
                                run[i] = d;
                            }
                            const bool ridge_ew = (std::min(run[0], run[1]) <= std::min(run[2], run[3]));
                            int eff[4] = { run[0], run[1], run[2], run[3] };
                            for (int i = 0; i < 2; ++i) {
                                const int k = ridge_ew ? (2 + i) : i;
                                if (eff[k] >= brk) {
                                    eff[k] = kFar; //!< 破風。ここから上は距離で縛らない
                                }
                            }
                            out_pose = 0;
                            for (int i = 1; i < 4; ++i) {
                                if (eff[i] < eff[out_pose]) {
                                    out_pose = i;
                                }
                            }
                            out_level = eff[out_pose];
                            if (out_flat != nullptr) {
                                *out_flat = (out_level > cap);
                            }
                            out_level = std::min(out_level, cap);
                            return ridge_ew;
                        };
                        int run[4]{};
                        int level = 0;
                        int pose = 0;
                        bool flat = false;
                        const bool ridge_ew = solve(gx, gy, run, level, pose, &flat);
                        const int slope = choose(wingset.slope[pose], seed);
                        const float rise = top_of(slope);
                        const float ridge_top = wall_h + (rise * static_cast<float>(level + 1));
                        place_town(wall, fx, fy, -kFootingDepth, kPlain, ridge_top);
                        /*
                         * **上限に達したマスは平らな大棟**（デザイン8）。距離変換は
                         * 「平面の厚い所ほど屋根が高い」ので、棟と棟が出会う所で段が
                         * 際限なく伸び、**階段状の塊**に見えた（合成フレームで実測）。
                         * 上限で頭打ちにすると翼ごとの棟の高さが揃い、頭打ちになった
                         * 面は幅の広い大棟として読める。**斜面を同じ段で並べてはならない**
                         * ——1 マスごとに上がる型なので、平らな所へ並べると鋸歯になる。
                         */
                        int piece = flat
                            ? ((wingset.roof_top >= 0) ? wingset.roof_top : slope)
                            : (((level == 0) && !wingset.eave[pose].empty())
                                    ? choose(wingset.eave[pose], seed)
                                    : slope);
                        /*
                         * **陸屋根の崩落**（デザイン12・2026-08-20。廃洋館）。
                         * §4.3 の外せない 1 点「上辺が欠けた非対称シルエット」は、
                         * 平らな大棟を**塊で抜く**ことで出る。
                         *
                         * **マスごとに散らしてはならない。**変種の 1 枚を穴にすると
                         * マスの種が独立に選ぶので数十個の穴が方眼に開く（罠 38）。
                         * 種は **2×2 マスで切り**（岩天井の変種と同じ流儀）、塊ごと
                         * 同じ変種を当てる——4〜6 か所のまとまった崩落になる。
                         *
                         * **陸屋根の縁（段が上限に達した斜面の輪）も対象にする。**
                         * 平らな所だけを抜くと穴は屋根の内側にしか開かず、
                         * **輪郭（上辺）は 1 か所も欠けない**（合成フレームで実測）。
                         * 縁を跨いだ塊が欠けて初めて、§4.3 の「上辺が欠けた非対称
                         * シルエット」になる。
                         */
                        if ((flat || (level >= cap)) && !townset.roof_scar.empty()) {
                            const int step = (style.manor_roof_scar_step > 0)
                                ? style.manor_roof_scar_step
                                : 12;
                            std::uint64_t scar_seed
                                = cell_seed(gx & ~1, gy & ~1, meaning.identity) ^ 0x6a09e667f3bcc909ull;
                            if ((scar_seed % static_cast<std::uint64_t>(step)) == 0) {
                                piece = choose(townset.roof_scar, scar_seed);
                            }
                        }
                        //! マスごとに色を少し振る（片流れと同じ理由。檜皮は 1 枚ずつ葺くもの）。
                        float shade[3]{ 1.f, 1.f, 1.f };
                        const float lum = 1.f + (next_range(seed, -1.f, 1.f) * 0.06f);
                        const float warm = next_range(seed, -1.f, 1.f) * 0.04f;
                        shade[0] = lum + warm;
                        shade[1] = lum;
                        shade[2] = lum - warm;
                        place_town(piece, fx, fy, wall_h + (rise * static_cast<float>(level)),
                            shade, 0.f);
                        /*
                         * **段の差で開いた縦の面を塞ぐ**（＝破風と、棟の高さが変わる所の
                         * 壁）。勾配の軸では 1 段の差は斜面そのものが覆うので、**2 段以上
                         * 開いたときだけ**。直交の軸は 1 段でも塞ぐ（そこが妻である）。
                         */
                        for (int i = 0; i < 4; ++i) {
                            const int nx = gx + kIriDirs[i][0];
                            const int ny = gy + kIriDirs[i][1];
                            int nl = 0;
                            int npose = pose;
                            /*
                             * **棟の違う隣は「外」と同じ**（デザイン9）。あちらは別の
                             * 建物で、壁の高さも屋根の段も揃っていない——隣の段で埋めを
                             * 縮めると、店舗と土蔵の合わせ目に**斜めの隙間**が開く。
                             */
                            if (same_manor(nx, ny, here_wing)) {
                                int nrun[4]{};
                                solve(nx, ny, nrun, nl, npose);
                            }
                            /*
                             * 勾配の軸で 1 段の差が斜面に覆われるのは、**隣も同じ軸へ
                             * 下りているとき**だけである。棟の向きが変わる境（永遠亭では
                             * 北棟と渡り廊下の角）では、同じ 1 段でも面が合わずに
                             * **暗い隙間**が開く（合成フレームで実測）。軸が違う隣は
                             * 直交の側と同じ扱いにして、1 段でも塞ぐ。
                             */
                            const bool same_axis = ((npose / 2) == (pose / 2));
                            const bool along = same_axis && (ridge_ew ? (i < 2) : (i >= 2));
                            if ((level - nl) <= (along ? 1 : 0)) {
                                continue;
                            }
                            const int filler = wingset.gable[i];
                            if (filler < 0) {
                                continue;
                            }
                            const LibraryEntry &fe = library->entry(filler);
                            InstanceData inst;
                            inst.x = fx - fe.anchor_x;
                            inst.y = fy - fe.anchor_y;
                            inst.z = wall_h + (rise * static_cast<float>(nl));
                            if (fe.top_z > 0.01f) {
                                inst.sz = (rise * static_cast<float>(level - nl)) / fe.top_z;
                            }
                            out.lib[static_cast<std::size_t>(filler)].push_back(inst);
                            ++out.lib_instances;
                        }
                        /*
                         * **棟**。両側の走りが等しい（＝勾配が出会う）マスにだけ載せる。
                         * 破風の裾（段が距離で縛られているマス）には載せない——あそこは
                         * 隅棟で下りる面で、棟の通る所ではない。
                         */
                        const int a = ridge_ew ? run[0] : run[2];
                        const int b = ridge_ew ? run[1] : run[3];
                        const int ridge = ridge_ew ? wingset.ridge : wingset.ridge_ns;
                        if ((ridge >= 0) && !flat && (level == std::min(a, b))) {
                            if (a == b) {
                                //! 奇数の奥行き。棟はマスの中央。
                                place_town(ridge, ridge_ew ? fx : (fx + 0.5f),
                                    ridge_ew ? (fy + 0.5f) : fy, ridge_top, kPlain, 0.f);
                            } else if (a == (b - 1)) {
                                //! 偶数の奥行き。棟は隣との境にまたがる。
                                place_town(ridge, ridge_ew ? fx : (fx + 1.f),
                                    ridge_ew ? (fy + 1.f) : fy, ridge_top, kPlain, 0.f);
                            }
                        }
                        break;
                    }
                    if (townset.manor_gable) {
                        const std::array<int, 4> *mybox = nullptr;
                        for (const auto &mbox : town->manors) {
                            if ((gx >= mbox[0]) && (gx <= mbox[2]) && (gy >= mbox[1])
                                && (gy <= mbox[3])) {
                                mybox = &mbox;
                                break;
                            }
                        }
                        if (mybox == nullptr) {
                            place_town(wall, fx, fy, -kFootingDepth, kPlain, wall_h);
                            break;
                        }
                        const int dn = gy - (*mybox)[1];
                        const int ds = (*mybox)[3] - gy;
                        const int level = std::min(dn, ds);
                        const int half = (dn <= ds) ? 0 : 1;
                        //! **屋根も棟ごと**（デザイン11）。割っていない町は主の材に落ちる。
                        const std::vector<int> &gset = wingset.gable_slope[half].empty()
                            ? townset.gable_slope[half]
                            : wingset.gable_slope[half];
                        const std::vector<int> &eset = wingset.gable_eave[half].empty()
                            ? townset.gable_eave[half]
                            : wingset.gable_eave[half];
                        const int gslope = gset.empty() ? -1 : choose(gset, seed);
                        const int geave = eset.empty() ? -1 : choose(eset, seed);
                        const float rise = top_of(gslope);
                        const float ridge_top = wall_h + (rise * static_cast<float>(level + 1));
                        place_town(wall, fx, fy, -kFootingDepth, kPlain, ridge_top);
                        const int piece = ((level == 0) && (geave >= 0)) ? geave : gslope;
                        place_town(piece, fx, fy, wall_h + (rise * static_cast<float>(level)),
                            kPlain, 0.f);
                        if (level > 0) {
                            static constexpr int kGableDirs[4][2] = {
                                { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } // N / S / W / E
                            };
                            for (int i = 0; i < 4; ++i) {
                                if (town->role_at(gx + kGableDirs[i][0], gy + kGableDirs[i][1])
                                    == TownRole::Manor) {
                                    continue;
                                }
                                const int filler = (wingset.gable[i] >= 0)
                                    ? wingset.gable[i]
                                    : townset.gable_wall[i];
                                if (filler < 0) {
                                    continue;
                                }
                                const LibraryEntry &fe = library->entry(filler);
                                InstanceData inst;
                                inst.x = fx - fe.anchor_x;
                                inst.y = fy - fe.anchor_y;
                                inst.z = wall_h;
                                if (fe.top_z > 0.01f) {
                                    inst.sz = (rise * static_cast<float>(level)) / fe.top_z;
                                }
                                out.lib[static_cast<std::size_t>(filler)].push_back(inst);
                                ++out.lib_instances;
                            }
                        }
                        const int gridge = (wingset.ridge >= 0) ? wingset.ridge : townset.gable_ridge;
                        if (gridge >= 0) {
                            if (dn == (ds - 1)) {
                                //! 偶数奥行き。棟は南隣との境（fy+1 のマスの北縁）にまたがる。
                                place_town(gridge, fx, fy + 1.f, ridge_top, kPlain, 0.f);
                            } else if (dn == ds) {
                                //! 奇数奥行き。棟はマスの中央。
                                place_town(gridge, fx, fy + 0.5f, ridge_top, kPlain, 0.f);
                            }
                        }
                        break;
                    }
                    const int roof = townset.manor_roof[std::min<std::size_t>(slice, 8)];
                    const float top = wall_h + ((roof >= 0) ? top_of(roof) : 0.f);
                    place_town(wall, fx, fy, -kFootingDepth, kPlain, top);
                    if (roof >= 0) {
                        place_town(roof, fx, fy, wall_h, kPlain, 0.f);
                    }
                    //! 塔（時計塔・小塔・翼塔）。屋根の上に 0.35 マス埋めて立てる。
                    for (const auto &tw : townset.manor_towers) {
                        if ((tw[0] == gx) && (tw[1] == gy)) {
                            place_town(tw[2], fx, fy, top - 0.35f, kPlain, 0.f);
                        }
                    }
                    break;
                }
                case TownRole::None:
                case TownRole::Border:
                default:
                    break;
                }
                continue; // このマスは町の規則で描けた
            }

            /*
             * ---- 本物のプレハブ（P10）----
             * 選ぶ順は **地形の細別（記憶）→ 役割の既定**。細別は間近で見たマスにしか
             * 無いので、見ていないマスは役割で描く（ミニマップの粗さのまま。捏造しない）。
             */
            if (use_lib) {
                const TerrainRule *rule = nullptr;
                if (memory != nullptr) {
                    const std::uint16_t detail_id = memory->id_at(gx, gy);
                    if (detail_id != 0) {
                        rule = library->rule_for_terrain(detail_id, surface_floor);
                    }
                }
                if ((rule == nullptr) || !rule->any()) {
                    rule = library->rule_for_role(role, surface_floor);
                }
                /*
                 * **町の地面は道と草で塗り分ける**（2026-08-09 に決めた:「辺境なので
                 * 地面は土の地面で、建物と建物をつなぐ通り道は踏み固められて地面がむき出しに。
                 * それ以外は短めの草が」）。
                 *
                 * 効かせるのは**役割の既定で描くマスだけ**である。草・水・木のように細別を
                 * 持つマスはそちらが正なので触らない（表が持つ知識を上書きしない）。
                 *
                 * 水際は「あぜ道」にする（同上「縁にはあぜ道のような草と踏み固められた
                 * 地面で」）。**水かどうかは表に聞く**——隣のマスの規則が水の素材を指しているか
                 * を見るだけで、コードは地形 id を知らないままでいられる（§9.4）。
                 */
                /*
                 * **地形の絵の差し替え**（デザイン14・2026-08-20。彼岸の奈落 96 マス）。
                 * 共通の対応表に行の無い地形（`DARK_PIT`）を町ごとの表で受ける。
                 * **当たったマスは表が丸ごと持つ**——意匠の草も街路樹も撒かない。
                 * 地形 id は間近で見たマスにしか無い（罠 40）ので、見るまでは従来の絵で立つ。
                 */
                const decltype(townset)::TerrainPropRow *terrain_prop = nullptr;
                if (use_town && !townset.terrain_props.empty() && (memory != nullptr)) {
                    const std::uint16_t here_id = memory->id_at(gx, gy);
                    for (const auto &row : townset.terrain_props) {
                        if (row.terrain_id == static_cast<int>(here_id)) {
                            terrain_prop = &row;
                            break;
                        }
                    }
                }
                /*
                 * **小川が通るマス**（OUTPOST_TOWN_DESIGN §5 `brook`）。差し替えたマスには
                 * 草・小物・道の縁を撒かない——`terrain_prop` と同じ「表が丸ごと持つマス」の
                 * 扱い。表が折れ線を書いていない町では常に偽（`townset.brook_kind` が空）。
                 */
                const bool brook_here = use_town && !townset.brook_kind.empty()
                    && (townset.brook_kind[(static_cast<std::size_t>(gy)
                                                * static_cast<std::size_t>(town->width))
                            + static_cast<std::size_t>(gx)]
                        != 0xFFu);
                const bool plain_ground = (rule == library->rule_for_role(role, surface_floor));
                /*
                 * **町の中では地形の草地も町の地面へ寄せる**（意匠が求めるときだけ）。
                 * 「町の中の地面には草はなし」（モリバント）と「道以外の平地はススキ野」
                 * （テルモラ）は、どちらも同じ話——2026-08-09 に決めた。
                 *
                 * **草かどうかは表に聞く**（そのマスの規則が `ground_grass_*` を指しているか）。
                 * コードは地形 id を知らないままでいられる（§9.4）。水と溶岩には効かない。
                 */
                const bool grassy_detail = use_town && !plain_ground && (rule != nullptr)
                    && style.override_grass
                    && std::any_of(townset.grass_ground.begin(), townset.grass_ground.end(),
                        [rule](int index) {
                            return std::find(rule->ground.begin(), rule->ground.end(), index) != rule->ground.end();
                        });
                //! 道の向き。隣の道（`TownRole::Path`）が東西だけなら 1、南北だけなら 2、両方なら 3、無ければ 0。
                const auto road_dir = [&](int cx, int cy) -> int {
                    if (town == nullptr) {
                        return 0;
                    }
                    const bool w = town->role_at(cx - 1, cy) == TownRole::Path;
                    const bool e = town->role_at(cx + 1, cy) == TownRole::Path;
                    const bool n = town->role_at(cx, cy - 1) == TownRole::Path;
                    const bool s = town->role_at(cx, cy + 1) == TownRole::Path;
                    return ((w || e) && (n || s)) ? 3 : ((w || e) ? 1 : ((n || s) ? 2 : 0));
                };
                const int town_ground = [&]() -> int {
                    if (!use_town) {
                        return -1;
                    }
                    const bool walkable_role = (town_role == TownRole::Path)
                        || (town_role == TownRole::Turf) || (town_role == TownRole::None);
                    if (walkable_role) {
                        /*
                         * **小川**（OUTPOST_TOWN_DESIGN §5 `brook`）。地形の細別より先に効く
                         * ——表が折れ線を書いた町だけ `townset.brook_kind` が埋まる。
                         */
                        if (!townset.brook_kind.empty()) {
                            const std::size_t brook_idx = (static_cast<std::size_t>(gy)
                                                               * static_cast<std::size_t>(town->width))
                                + static_cast<std::size_t>(gx);
                            const std::uint8_t bk = townset.brook_kind[brook_idx];
                            if (bk != 0xFFu) {
                                int pick = -1;
                                if (town_role == TownRole::Path) {
                                    pick = (bk == 1) ? townset.brook_plank_ew : townset.brook_plank_ns;
                                } else {
                                    switch (bk) {
                                    case 0:
                                        pick = townset.brook_ns;
                                        break;
                                    case 1:
                                        pick = townset.brook_ew;
                                        break;
                                    case 2:
                                        pick = townset.brook_bend[0];
                                        break;
                                    case 3:
                                        pick = townset.brook_bend[1];
                                        break;
                                    case 4:
                                        pick = townset.brook_bend[2];
                                        break;
                                    case 5:
                                        pick = townset.brook_bend[3];
                                        break;
                                    default:
                                        break;
                                    }
                                }
                                if (pick >= 0) {
                                    return pick;
                                }
                            }
                        }
                        /*
                         * **区画の地面**。
                         * 水のマス（規則が `townset.water` を指すマス）は差し替えない——
                         * 池の区画（`water_pond_out`）は「ここを池にする」意図なので、
                         * **既に水のマスだけ**を除く（brook/橋と同じ「絵で見る」判定）。
                         */
                        if (!townset.zones.empty() && !zone_of.empty()) {
                            const int zi = zone_of[(static_cast<std::size_t>(gy)
                                                        * static_cast<std::size_t>(town->width))
                                + static_cast<std::size_t>(gx)];
                            if ((zi >= 0)
                                && !townset.zones[static_cast<std::size_t>(zi)].ground.empty()) {
                                const std::uint16_t id = (memory != nullptr) ? memory->id_at(gx, gy) : 0;
                                const TerrainRule *const here
                                    = (id != 0) ? library->rule_for_terrain(id, surface_floor) : nullptr;
                                bool zone_is_water = false;
                                if (here != nullptr) {
                                    for (const int water : townset.water) {
                                        if (std::find(here->ground.begin(), here->ground.end(), water)
                                            != here->ground.end()) {
                                            zone_is_water = true;
                                            break;
                                        }
                                    }
                                }
                                if (!zone_is_water) {
                                    const auto &zn = townset.zones[static_cast<std::size_t>(zi)];
                                    if (town_role == TownRole::Path) {
                                        const int dir = road_dir(gx, gy);
                                        const std::vector<int> *set = nullptr;
                                        if ((dir == 1) && !zn.ground_ew.empty()) {
                                            set = &zn.ground_ew;
                                        } else if ((dir == 2) && !zn.ground_ns.empty()) {
                                            set = &zn.ground_ns;
                                        } else if ((dir == 3) && !zn.ground_x.empty()) {
                                            set = &zn.ground_x;
                                        }
                                        if (set != nullptr) {
                                            return choose(*set, seed);
                                        }
                                    }
                                    /*
                                     * **縁は区画の矩形の位置で選ぶ**（2026-09-05 に決めた「3 マス幅の上下に
                                     * 緑がきて中央に向けて茶色。中央のマスは黒めの茶色」）。北の列は `_edge_n`
                                     * （草地が北）、南の列は `_edge_s`、東西の端の列は `_edge_w`/`_edge_e`、
                                     * 中は素の泥。道の並びに頼ると、道でない所で縁が途切れる。
                                     */
                                    {
                                        int edge = -1;
                                        if ((zn.y1 > zn.y0) && (gy == zn.y0)) {
                                            edge = 0;
                                        } else if ((zn.y1 > zn.y0) && (gy == zn.y1)) {
                                            edge = 1;
                                        } else if ((zn.x1 > zn.x0) && (gx == zn.x0)) {
                                            edge = 2;
                                        } else if ((zn.x1 > zn.x0) && (gx == zn.x1)) {
                                            edge = 3;
                                        }
                                        if ((edge >= 0) && !zn.ground_edge[edge].empty()) {
                                            return choose(zn.ground_edge[edge], seed);
                                        }
                                    }
                                    return choose(zn.ground, seed);
                                }
                            }
                        }
                        /*
                         * **橋は自動**（同 §5 の橋）。向かい合う 2 辺の隣が水なら板の橋を敷く。
                         * `townset.bridge_ns/ew` は意匠が `suffix` を持つ町だけで引いてある
                         * （資材が無ければ -1 のままなので、この枝は何もしない）。
                         */
                        if ((townset.bridge_ns >= 0) || (townset.bridge_ew >= 0)) {
                            const auto is_water_side = [&](int nx, int ny) {
                                const std::uint16_t id = (memory != nullptr) ? memory->id_at(nx, ny) : 0;
                                const TerrainRule *const near = (id != 0)
                                    ? library->rule_for_terrain(id, surface_floor)
                                    : nullptr;
                                if (near == nullptr) {
                                    return false;
                                }
                                for (const int water : townset.water) {
                                    if (std::find(near->ground.begin(), near->ground.end(), water)
                                        != near->ground.end()) {
                                        return true;
                                    }
                                }
                                return false;
                            };
                            /*
                             * **自分のマスが水なら橋ではない。** 堀は 2 マス幅なので、水のマスも
                             * 「向かい合う 2 辺が水」を満たす——除かないと堀に沿って板張りが並ぶ
                             * （2026-09-05 に実際にそうなった）。
                             */
                            const bool self_water = is_water_side(gx, gy);
                            const bool bridge_ns = !self_water && is_water_side(gx, gy - 1) && is_water_side(gx, gy + 1);
                            const bool bridge_ew = !self_water && is_water_side(gx - 1, gy) && is_water_side(gx + 1, gy);
                            if (bridge_ns && (townset.bridge_ns >= 0)) {
                                return townset.bridge_ns;
                            }
                            if (bridge_ew && (townset.bridge_ew >= 0)) {
                                return townset.bridge_ew;
                            }
                        }
                    }
                    if ((!plain_ground && !grassy_detail) || townset.path.empty()
                        || townset.turf.empty()) {
                        return -1;
                    }
                    if ((town_role != TownRole::Path) && (town_role != TownRole::Turf)) {
                        return -1;
                    }
                    bool by_water = false;
                    for (int i = 0; (i < 4) && !by_water; ++i) {
                        const int nx = gx + ((i == 2) ? -1 : ((i == 3) ? 1 : 0));
                        const int ny = gy + ((i == 0) ? -1 : ((i == 1) ? 1 : 0));
                        const std::uint16_t id = (memory != nullptr) ? memory->id_at(nx, ny) : 0;
                        const TerrainRule *const near = (id != 0) ? library->rule_for_terrain(id, surface_floor)
                                                                  : nullptr;
                        if (near == nullptr) {
                            continue;
                        }
                        for (const int water : townset.water) {
                            by_water = by_water
                                || (std::find(near->ground.begin(), near->ground.end(), water) != near->ground.end());
                        }
                    }
                    //! あぜ道は**草と踏み跡が混じる**（どちらか一色にすると人工的に見える）。
                    const bool packed = (town_role == TownRole::Path) || (by_water && (next01(seed) < 0.55f));
                    if ((town_role == TownRole::Path) && !townset.path_ew.empty()) {
                        //! 轍は道の向きで選ぶ。轍の位置はマスの縁で揃えてあるので**マスをまたいで続く**。
                        const int dir = road_dir(gx, gy);
                        if ((dir == 1) && !townset.path_ew.empty()) {
                            return choose(townset.path_ew, seed);
                        }
                        if ((dir == 2) && !townset.path_ns.empty()) {
                            return choose(townset.path_ns, seed);
                        }
                        if (!townset.path_x.empty()) {
                            return choose(townset.path_x, seed);
                        }
                    }
                    return choose(packed ? townset.path : townset.turf, seed);
                }();
                //! 町の地面で塗り替えたマスは、装飾の側でも「素の地面」として扱う。
                const bool town_ground_used = town_ground >= 0;

                /*
                 * **区画の柵・小物・区画内の 1 点**（辺境の地の
                 * 作り替え第 2 段）。地面は上の `town_ground` が既に決めている——ここは
                 * 柵と小物だけ。表を書いていない町では `townset.zones` が空のままなので、
                 * この節は 1 度も回らない。
                 */
                const int zone_here_idx = (!townset.zones.empty() && !zone_of.empty())
                    ? zone_of[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(town->width))
                        + static_cast<std::size_t>(gx)]
                    : -1;
                //! **小川が通るマスは区画より先に効く**（brook が丸ごと持つマス。設計書 §5）。
                const bool zone_here = (zone_here_idx >= 0) && !brook_here;
                if (zone_here) {
                    static constexpr float kZonePlain[3] = { 1.f, 1.f, 1.f };
                    const ZoneRuntime &zone = townset.zones[static_cast<std::size_t>(zone_here_idx)];
                    const std::size_t zone_cell = (static_cast<std::size_t>(gy)
                                                       * static_cast<std::size_t>(town->width))
                        + static_cast<std::size_t>(gx);
                    //! 柵。**外周の辺だけ**（隣が道の辺は `zone_fence` が既に落としてある）。
                    const std::uint8_t fbits = zone_fence[zone_cell];
                    for (int i = 0; i < 4; ++i) {
                        if (((fbits & (1u << i)) == 0u) || (zone.rim[i] < 0)) {
                            continue;
                        }
                        place_town(zone.rim[i], fx, fy, -kFootingDepth, kZonePlain);
                    }
                    //! マスの隅（または中央）を引く。**呼ぶたびに新しく引く**——同じマスに
                    //! `props` と `rim_props` の両方が当たっても、隅が重ならないように。
                    const auto zone_corner = [&]() -> std::pair<float, float> {
                        const float pick = next01(seed);
                        const float cx = (pick < 0.5f) ? -kPropCornerOffset : kPropCornerOffset;
                        const float cy = (std::fmod(pick * 4.f, 2.f) < 1.f) ? -kPropCornerOffset
                                                                             : kPropCornerOffset;
                        return { cx, cy };
                    };
                    //! 列の先頭から順に `chance` を当て、**最初に当たった 1 つ**だけ置く。
                    const auto try_zone_props = [&](const std::vector<ZonePropRuntime> &list) {
                        for (const auto &p : list) {
                            if (next01(seed) >= p.chance) {
                                continue;
                            }
                            float px = 0.f;
                            float py = 0.f;
                            if (!p.center) {
                                const auto corner = zone_corner();
                                px = corner.first;
                                py = corner.second;
                            }
                            const int pick = p.variants.empty() ? p.prefab : choose(p.variants, seed);
                            place_town(pick, fx + px, fy + py, -kFootingDepth, kZonePlain, top_of(pick));
                            if (p.flame >= 0) {
                                const float strength = p.always ? 1.f : night_eff;
                                if (strength > 0.02f) {
                                    place_town(p.flame, fx + px, fy + py, -kFootingDepth, kZonePlain, 0.f,
                                        p.glow[0] * 2.2f * strength, p.glow[1] * 2.2f * strength,
                                        p.glow[2] * 2.2f * strength);
                                }
                            }
                            ++out.prop_count;
                            return true;
                        }
                        return false;
                    };
                    if (!zone.props.empty()) {
                        try_zone_props(zone.props);
                    }
                    //! 外周のマスだけ、同じ流儀で**さらに**撒く。
                    if ((zone_rim[zone_cell] != 0u) && !zone.rim_props.empty()) {
                        try_zone_props(zone.rim_props);
                    }
                    //! 区画の中の名指したマス（`features`）。ここは None/Path/Turf しか
                    //! 来ないので、設計書の「役割は問わない（`Rampart` 以外）」は自然に満たす。
                    for (const auto &feat : zone.features) {
                        if ((feat.at_x != gx) || (feat.at_y != gy)) {
                            continue;
                        }
                        float px = 0.f;
                        float py = 0.f;
                        if (!feat.center) {
                            const auto corner = zone_corner();
                            px = corner.first;
                            py = corner.second;
                        }
                        place_town(feat.prefab, fx + px, fy + py, -kFootingDepth, kZonePlain,
                            top_of(feat.prefab));
                        if (feat.flame >= 0) {
                            const float strength = feat.always ? 1.f : night_eff;
                            if (strength > 0.02f) {
                                place_town(feat.flame, fx + px, fy + py, -kFootingDepth, kZonePlain, 0.f,
                                    feat.glow[0] * 2.2f * strength, feat.glow[1] * 2.2f * strength,
                                    feat.glow[2] * 2.2f * strength);
                            }
                        }
                        ++out.prop_count;
                    }
                }

                if ((rule != nullptr) && (rule->any() || (town_ground >= 0))) {
                    bool structure_placed = false;
                    bool object_placed = false;
                    /*
                     * **ダンジョンの意匠で役割の材を差し替える**（P10 第 4 期。
                     * 2026-08-10 に決めた「イークの床を含む意匠」）。
                     *
                     * `plain_ground`（＝このマスを役割の既定で描いている）が真のときだけ。
                     * 地形の細別を持つマスは表のほうが正なので触らない（§9.4）。
                     * だから洞窟の床を指定しても、その洞窟にある池や溶岩は池と溶岩のまま。
                     */
                    const std::size_t role_slot = static_cast<std::size_t>(role);
                    const std::vector<int> *const styled_ground = (plain_ground
                                                                      && (role_slot < kCellRoleCount)
                                                                      && !props.mat_ground[role_slot].empty())
                        ? &props.mat_ground[role_slot]
                        : nullptr;
                    const std::vector<int> *const styled_structure = (plain_ground
                                                                         && (role_slot < kCellRoleCount)
                                                                         && !props.mat_structure[role_slot].empty())
                        ? &props.mat_structure[role_slot]
                        : nullptr;
                    /*
                     * **床ごと置き換える印**（`stairs_down`。デザイン5 その3——紅魔館の
                     * 迷宮入口は「建物内の階段」なので下り階段の絵）。天面が地表そのもの
                     * （is_ground）の印を置くマスに地面も敷くと、同じ高さの面が重なって
                     * ちらつく。このマスの床は印に任せる。
                     */
                    const bool mark_owns_ground
                        = (manor_mark_here >= 0) && library->entry(manor_mark_here).is_ground;
                    if (((town_ground >= 0) || !rule->ground.empty()) && !mark_owns_ground) {
                        /*
                         * **屋敷の屋内の床は板張り**（2026-08-19 に決めた デザイン5:
                         * 「建物内の床は草ではおかしいので板張りの床に」）。屋内と判定した
                         * マスは地形の草より先に板の床で受ける。ライブラリに無い意匠は従来どおり。
                         */
                        const int pick
                            = ((terrain_prop != nullptr) && !terrain_prop->ground.empty())
                            ? choose(terrain_prop->ground, seed)
                            : ((manor_inside && !style.manor_open_court && (townset.manor_floor >= 0))
                                    ? townset.manor_floor
                                    : ((town_ground >= 0)
                                            ? town_ground
                                            : choose((styled_ground != nullptr) ? *styled_ground
                                                                                : rule->ground,
                                                  seed)));
                        const LibraryEntry &entry = library->entry(pick);
                        InstanceData ground;
                        ground.x = fx - entry.anchor_x;
                        ground.y = fy - entry.anchor_y;
                        ground.z = 0.f;
                        lib_tint(ground, *rule, seed, 0.09f);
                        ground.er = rule->emissive[0];
                        ground.eg = rule->emissive[1];
                        ground.eb = rule->emissive[2];
                        out.lib[static_cast<std::size_t>(pick)].push_back(ground);
                        ++out.lib_instances;
                        /*
                         * **湖の霧**（デザイン4・2026-08-18。紅魔館「霧の湖は高さ2ブロックの
                         * あたりに半透明にした雲をうかべよう」）。意匠が高さを持つ町で、
                         * 深水のマス（あぜ道の縁と同じ「絵で見る」判定）だけリクエストを積む。
                         * 描くのは `CloudLayer::draw_patch`——ここはマスを集めるだけ。
                         */
                        if (use_town && (style.lake_mist_height > 0.f)
                            && (townset.water_deep >= 0) && (pick == townset.water_deep)) {
                            out.mist_cells.push_back({ gx, gy });
                        }
                        /*
                         * **上空から差し込む光の柱**（2026-08-22 に決めた。陽だまり）。
                         * 規則が `light_shaft` を持つマスだけ。**マスの中心に 1 本**で、
                         * 散らしはしない——陽だまりは「1 マスに 1 つの穴から」の絵である。
                         * 描くのは `LightShaftRenderer`（`hd2d_app.cpp`）。
                         */
                        if (rule->shaft_alpha > 0.f) {
                            LightShaftInstance shaft;
                            shaft.x = fx + 0.5f;
                            shaft.y = fy + 0.5f;
                            shaft.z = 0.f;
                            shaft.radius = rule->shaft_radius;
                            shaft.height = rule->shaft_height;
                            shaft.full = rule->shaft_full;
                            shaft.slant_x = rule->shaft_slant[0];
                            shaft.slant_y = rule->shaft_slant[1];
                            shaft.taper = rule->shaft_taper;
                            shaft.r = rule->shaft_color[0];
                            shaft.g = rule->shaft_color[1];
                            shaft.b = rule->shaft_color[2];
                            shaft.a = rule->shaft_alpha;
                            out.light_shafts.push_back(shaft);
                        }
                    }
                    /*
                     * **屋敷の屋内**（デザイン4 第 2 段）。頭上に屋根の蓋・館の壁に面した
                     * 辺に内壁・表の印（螺旋階段・本の机）。印のマスでは対応表の既定の絵
                     * （石のアーチ・階段の絵）を置かない（`manor_mark_here` を下の枝が見る）。
                     */
                    if (manor_inside) {
                        static constexpr float kManorPlain[3] = { 1.f, 1.f, 1.f };
                        /*
                         * **中庭は屋外**（デザイン7・2026-08-19。河童のバザーの市庭）。
                         * 蓋も内壁も敷かない——市庭は草と水と露店の場所で、屋内にすると
                         * 板の間の広間になる。**印（入口の顔）は屋外でも立てる。**
                         */
                        if (!style.manor_open_court) {
                        const int lid = townset.manor_roof[4];
                        const int wall_ref
                            = townset.manor_wall[4].empty() ? -1 : townset.manor_wall[4].front();
                        if (townset.manor_gable && (wall_ref >= 0)) {
                            /*
                             * 切妻の蓋（デザイン6）。彫り込みのマスの頭上にも、外のマスと同じ
                             * 段の斜面を敷く——博麗の拝殿の入口（北面の 1 マスの彫り込み）は
                             * 向拝の屋根がそのまま続いて見える。
                             */
                            const std::array<int, 4> *mybox = nullptr;
                            for (const auto &mbox : town->manors) {
                                if ((gx >= mbox[0]) && (gx <= mbox[2]) && (gy >= mbox[1])
                                    && (gy <= mbox[3])) {
                                    mybox = &mbox;
                                    break;
                                }
                            }
                            if (mybox != nullptr) {
                                const int dn = gy - (*mybox)[1];
                                const int ds = (*mybox)[3] - gy;
                                const int level = std::min(dn, ds);
                                const int half = (dn <= ds) ? 0 : 1;
                                /*
                                 * **彫り込みの蓋も棟の材で葺く**（デザイン11）。マスそのものは
                                 * 屋敷ではない（歩けるマス）ので、読み取り側が戸口のマスにも
                                 * 棟番号を入れてある。入っていない町は 0＝主のままである。
                                 */
                                const auto &lidset = townset.manor_wing[std::min<std::size_t>(
                                    town->wing_at(gx, gy), 2)];
                                const std::vector<int> &gset = lidset.gable_slope[half].empty()
                                    ? townset.gable_slope[half]
                                    : lidset.gable_slope[half];
                                const std::vector<int> &eset = lidset.gable_eave[half].empty()
                                    ? townset.gable_eave[half]
                                    : lidset.gable_eave[half];
                                const int gslope = gset.empty() ? -1 : choose(gset, seed);
                                const int geave = eset.empty() ? -1 : choose(eset, seed);
                                const float rise = top_of(gslope);
                                const int piece = ((level == 0) && (geave >= 0)) ? geave : gslope;
                                place_town(piece, fx, fy,
                                    top_of(wall_ref) + (rise * static_cast<float>(level)),
                                    kManorPlain, 0.f);
                                const int gridge
                                    = (lidset.ridge >= 0) ? lidset.ridge : townset.gable_ridge;
                                if ((gridge >= 0) && (dn == (ds - 1))) {
                                    place_town(gridge, fx, fy + 1.f,
                                        top_of(wall_ref) + (rise * static_cast<float>(level + 1)),
                                        kManorPlain, 0.f);
                                }
                            }
                        } else if ((lid >= 0) && (wall_ref >= 0)) {
                            place_town(lid, fx, fy, top_of(wall_ref), kManorPlain, 0.f);
                            //! 塔（時計塔は参道の正面＝彫り込みの蓋の上に立つ）。
                            for (const auto &tw : townset.manor_towers) {
                                if ((tw[0] == gx) && (tw[1] == gy)) {
                                    place_town(tw[2], fx, fy,
                                        top_of(wall_ref) + top_of(lid) - 0.35f, kManorPlain, 0.f);
                                }
                            }
                        }
                        //! 内壁（屋内の判定そのものが「幅 20 マス以下の彫り込み」に絞ってある）。
                        const std::uint8_t bits = town->fence_at(gx, gy);
                        for (int i = 0; i < 4; ++i) {
                            if (((bits & (1u << i)) != 0u) && (townset.manor_innerwall[i] >= 0)) {
                                place_town(townset.manor_innerwall[i], fx, fy, -kFootingDepth,
                                    kManorPlain, 0.f);
                            }
                        }
                        } //!< `manor_open_court` の閉じ（中庭は屋外）
                        if (manor_mark_here >= 0) {
                            /*
                             * 横 2 マスの印（本の机）が入口の並び（書店と図書館が隣接）で
                             * 重ならないよう、**西隣も印ならこのマスでは置かない**
                             * （西の 1 枚が両方の入口を覆う）。
                             */
                            /*
                             * **1 マスの印には効かせない**（デザイン14・2026-08-20。彼岸の階）。
                             * この飛ばしは「横 2 マスの机が隣り合う入口で重なる」ための備えで、
                             * 1 マスの印（朱の丸柱）を並べたい所では**西端の 1 本しか立たない**
                             * ——4 マスの向拝が柱 1 本になった（合成フレームで実測）。
                             */
                            const bool wide_mark
                                = library->entry(manor_mark_here).cover_x1
                                > library->entry(manor_mark_here).cover_x0;
                            bool skip_mark = false;
                            if (wide_mark && (memory != nullptr)) {
                                const std::uint16_t west_id = memory->id_at(gx - 1, gy);
                                for (const auto &mark : townset.manor_marks) {
                                    if ((mark.first == static_cast<int>(west_id))
                                        && (mark.second == manor_mark_here)) {
                                        skip_mark = true;
                                        break;
                                    }
                                }
                            }
                            if (!skip_mark) {
                                /*
                                 * 床ごと置き換える印（`stairs_down`）は**沈めない**——
                                 * 天面が地表そのものなので、根元埋め（罠 4）を掛けると
                                 * 縁が 0.08 マス下がって板張りの床と段差が出る。
                                 */
                                const bool ground_mark = library->entry(manor_mark_here).is_ground;
                                place_town(manor_mark_here, fx, fy,
                                    ground_mark ? 0.f : -kFootingDepth, kManorPlain,
                                    top_of(manor_mark_here));
                            }
                        }
                    }
                    /*
                     * **桜 1 本**（デザイン9・2026-08-19。香霖堂。§4.1 ⑤「近くに桜の木が
                     * 一本——春に霖之助が一人で花見をする」）。どのマスかは `town_plan` が
                     * 屋敷の南西の外角で選んである。**対応表の物より先に立てる**ので、
                     * 木のマスでも草のマスでも同じ 1 本になる（竹林と同じ差し替えの流儀）。
                     */
                    static constexpr float kLonePlain[3] = { 1.f, 1.f, 1.f };
                    /*
                     * **入口の前の列**（デザイン10。命蓮寺の赤い幟）。どのマスかは上の
                     * 走査が拾ってある。**草も小物も置かない**（列の足元は空けておく）。
                     */
                    bool flank_here = false;
                    for (const auto &prop : flank_props) {
                        if ((prop[0] != gx) || (prop[1] != gy)) {
                            continue;
                        }
                        place_town(prop[2], fx, fy, -kFootingDepth, kLonePlain, top_of(prop[2]));
                        flank_here = true;
                        break;
                    }
                    bool lone_tree_here = false;
                    if (use_town && (townset.corner_prop >= 0)) {
                        for (const auto &spot : town->lone_trees) {
                            if ((spot[0] != gx) || (spot[1] != gy)) {
                                continue;
                            }
                            place_town(townset.corner_prop, fx, fy, -kFootingDepth, kLonePlain,
                                top_of(townset.corner_prop));
                            lone_tree_here = true;
                            break;
                        }
                    }
                    /*
                     * **戸口の外のガラクタ**（デザイン9。§4.1 ⑥）。霖之助は外の世界の
                     * 道具を拾い集める道具屋で、店先に積まれた山がその記号である。
                     * 屋敷の中庭（＝入口の前の彫り込み）のマスへ、マスの種で 3 つに 1 つ。
                     */
                    if (manor_inside && !townset.court_props.empty() && !lone_tree_here
                        && (manor_mark_here < 0) && (next01(seed) < 0.34f)) {
                        const int pick = choose(townset.court_props, seed);
                        place_town(pick, fx + next_range(seed, -0.18f, 0.18f),
                            fy + next_range(seed, -0.18f, 0.18f), -kFootingDepth, kLonePlain, 0.f);
                    }
                    if (!rule->structure.empty() && (manor_mark_here < 0)) {
                        const int pick = choose(
                            (styled_structure != nullptr) ? *styled_structure : rule->structure, seed);
                        const LibraryEntry &entry = library->entry(pick);
                        InstanceData solid;
                        solid.x = fx - entry.anchor_x;
                        solid.y = fy - entry.anchor_y;
                        // 根元を地面へ埋め、天面の高さは保つ（罠 4。壁の高さの揃いは「作られた」感）。
                        solid.z = -kFootingDepth;
                        if (entry.top_z > 0.01f) {
                            solid.sz = (entry.top_z + kFootingDepth) / entry.top_z;
                        }
                        lib_tint(solid, *rule, seed, 0.085f);
                        /*
                         * 斜め抜けの隙間（一人称）。**1 マスに収まる素材だけ**が対象。
                         * 木のように複数マスへ張り出すプレハブへ掛けると、原点まわりの
                         * 拡大率なので葉の広がりごと歪む。壁と岩盤は 1 マスなので条件で弾ける。
                         */
                        const bool single_cell = (entry.cover_x0 == 0) && (entry.cover_x1 == 0)
                            && (entry.cover_y0 == 0) && (entry.cover_y1 == 0);
                        if (single_cell
                            && ((role == CellRole::StructuralWall) || (role == CellRole::Bedrock))) {
                            apply_wall_inset(gx, gy, solid);
                        }
                        out.lib[static_cast<std::size_t>(pick)].push_back(solid);
                        ++out.lib_instances;
                        note_cover(entry, solid, entry.top_z); //!< 視線を遮る（P10 レビュー 4・6）
                        structure_placed = true;
                        if ((role != CellRole::StructuralWall) && (role != CellRole::Bedrock)) {
                            // 床あつかいの役割に細別が壁を建てた（山など）。下敷きをここで足す。
                            InstanceData under;
                            under.x = fx;
                            under.y = fy;
                            under.z = 0.f;
                            tint(under, kUnderBedrock, seed, 0.05f); //!< 山・鉱脈なので岩の側
                            out.under_slabs.push_back(under);
                        }
                    }
                    //! 祠の入口に門（鳥居）を立てたマスでは、対応表の既定の物（石のアーチ）は
                    //! 置かない（2026-08-18。差し替えの片割れ。`lone_gate_here` は上の枝が立てる）。
                    //! 屋敷の印（本の机・入口）のマスも同じ（`manor_mark_here`）。**屋内の床でも
                    //! 置かない**（デザイン5——板張りの床から草の茂みが生えていた。入口の絵は
                    //! 印（`manor_marks` の `dungeon_entrance`）として立つので消えない）。
                    /*
                     * **掃き清めた境内**（デザイン10。命蓮寺）。表が旗を立てた町では、
                     * 道と草地のマスに**対応表の草を 1 本も生やさない**——`tuft_chance` は
                     * 意匠が撒く草の話で、地形の表が持っている草はそれとは別に生える。
                     * 木のマスは対象外（境内の外の林は残る）。
                     */
                    const bool swept = style.sweep_turf
                        && ((town_role == TownRole::Turf) || (town_role == TownRole::Path));
                    /*
                     * **穴の縁**（デザイン14 その2。彼岸の奈落）。四方のうち**隣が違う
                     * 地形の辺**にだけ 1 枚立てる。型の中に縁を彫ると 96 マスぜんぶに縁が
                     * 立って黒い格子になる（実測）——穴の内側は 1 枚も立たないので、
                     * 96 マスが 1 つの穴として繋がる。
                     */
                    if ((terrain_prop != nullptr) && (memory != nullptr)) {
                        static constexpr int kSideDir[4][2] = {
                            { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } // N / S / W / E
                        };
                        static constexpr float kEdgePlain[3] = { 1.f, 1.f, 1.f };
                        for (int k = 0; k < 4; ++k) {
                            if (terrain_prop->wall[k] < 0) {
                                continue;
                            }
                            const std::uint16_t nid
                                = memory->id_at(gx + kSideDir[k][0], gy + kSideDir[k][1]);
                            if (static_cast<int>(nid) == terrain_prop->terrain_id) {
                                continue; //!< 隣も同じ穴。ここに縁は要らない
                            }
                            place_town(terrain_prop->wall[k], fx, fy, 0.f, kEdgePlain);
                        }
                    }
                    //! 差し替えのマスに載せるもの（デザイン14。書いていなければ何も載らない）。
                    if ((terrain_prop != nullptr) && !terrain_prop->prop.empty()) {
                        const int pick = choose(terrain_prop->prop, seed);
                        const LibraryEntry &entry = library->entry(pick);
                        InstanceData object;
                        object.x = fx - entry.anchor_x;
                        object.y = fy - entry.anchor_y;
                        object.z = entry.is_ground ? 0.f : -kFootingDepth;
                        out.lib[static_cast<std::size_t>(pick)].push_back(object);
                        ++out.lib_instances;
                        note_cover(entry, object, entry.top_z);
                        object_placed = true;
                    }
                    /*
                     * **そのマスから立ちのぼる煙**（デザイン15。偽天棚の坑口）。表が高さを
                     * 書いた行でだけ、口の座標を積む（`prop` の有無とは独立——地面だけ
                     * 替えて煙を出すマスもありうる）。
                     */
                    if ((terrain_prop != nullptr) && (terrain_prop->smoke > 0.f)) {
                        out.steam_jets.push_back({ fx, fy, terrain_prop->smoke });
                    }
                    //! 差し替えが**物を書いたときだけ**表の物を止める（地面だけ替える行は、
                    //! 表の物＝地獄の入口の階段をそのまま立てる）。
                    const bool prop_owns_cell = (terrain_prop != nullptr) && !terrain_prop->prop.empty();
                    /*
                     * **木のマスか**（デザイン8 の竹林。id は間近で見たマスにしか無い）。
                     * ここまで上げてあるのは下の `swept` の例外に要るからである
                     * ——`sweep_turf` は「**対応表の草**を生やさない」旗であって、
                     * 林を伐る旗ではない（デザイン15。偽天棚は両方を使う最初の町）。
                     */
                    const bool tree_here = (townset.tree_terrain_id > 0) && (memory != nullptr)
                        && (static_cast<int>(memory->id_at(gx, gy)) == townset.tree_terrain_id);
                    const bool grove_here = tree_here && !townset.grove.empty();
                    if (!rule->object.empty() && !prop_owns_cell && (lone_gate_here < 0)
                        && (manor_mark_here < 0)
                        && !lone_tree_here && !flank_here && (!swept || grove_here)
                        && !(manor_inside && (townset.manor_floor >= 0))
                        && (next01(seed) < rule->density)) {
                        const std::size_t slot = choose_slot(rule->object, seed);
                        /*
                         * **通る向きに合わせて姿勢を選ぶ**（P10 レビュー 11。いまは扉だけ）。
                         * 対が無い素材（樽・木箱・木…）は `object_ew` が空なので素通りする。
                         * 引く枚は同じ（`slot`）なので、変種のばらけ方は姿勢で変わらない。
                         */
                        const bool turn = !rule->object_ew.empty() && passage_runs_east_west(meaning, gx, gy);
                        /*
                         * **竹林**（デザイン8・2026-08-19。永遠亭）。意匠が竹の茎名を
                         * 持つ町では、木のマスだけ対応表の木を竹に差し替える。木かどうかは
                         * **対応表から引いた `TREE` の id** で見る（コードは 96 を持たない）。
                         */
                        int pick = turn ? rule->object_ew[slot] : rule->object[slot];
                        if (grove_here) {
                            pick = choose(townset.grove, seed);
                        }
                        const LibraryEntry &entry = library->entry(pick);
                        InstanceData object;
                        const float jx = (rule->jitter > 0.f) ? next_range(seed, -rule->jitter, rule->jitter) : 0.f;
                        const float jy = (rule->jitter > 0.f) ? next_range(seed, -rule->jitter, rule->jitter) : 0.f;
                        object.x = fx - entry.anchor_x + jx;
                        object.y = fy - entry.anchor_y + jy;
                        object.z = entry.is_ground ? 0.f : -kFootingDepth;
                        lib_tint(object, *rule, seed, 0.07f);
                        /*
                         * **その地形が光るなら、その地形が置くものも光る**（P10 第 3 期。
                         * 2026-08-10 に決めた の溶岩の泡）。泡は熔けた溶岩そのもので、
                         * ガスは溶岩に照らされている。まったく光らせないと、
                         * **明るい溶岩の上に暗い泡が浮く**（自発光はインスタンス単位＝罠 39）。
                         *
                         * ただし**地面と同じ強さでは光らせない**。溶岩の地面は 2.10 で
                         * 白く飛んでいるので、同じ値を乗せると泡が面に溶けて**形が消える**
                         * （実機の絵で確かめた）。控えめにすると、明るい面の上で泡が
                         * 濃い橙の塊として立ち上がり、暗い所でもちゃんと灯る。
                         *
                         * 効くのは表が `emissive` と `object` を両方持つ規則だけで、
                         * いまは溶岩の 2 行しかない。
                         */
                        constexpr float kObjectEmissive = 0.22f;
                        object.er = rule->emissive[0] * kObjectEmissive;
                        object.eg = rule->emissive[1] * kObjectEmissive;
                        object.eb = rule->emissive[2] * kObjectEmissive;
                        /*
                         * **門の見通しに被る木は立てない**（P10 第 3 期。決めたこと
                         * 2026-08-10:「アングウィル 建物の入り口に木のオブジェクトが
                         * 被らないように木を減らして」）。
                         *
                         * 見るのは幹のマスではなく**その木が実際に覆うマス**である
                         * （`cover_*`。木は幹 1 マスに対して葉が 3〜4 マスへ張り出す）。
                         * さらに**北へ「その木の高さ」マスぶん伸ばして**突き合わせる。
                         * カメラは南から見下ろすので、南に立つ木は自分の高さのぶんだけ
                         * 北の入口を隠す——設計書 §4.4 の遮蔽の式で、いまのカメラは
                         * 水平距離 ≒ 視点の高さ なので**隠す深さはほぼ木の高さに等しい**。
                         *
                         * こうすると**大木ほど遠くから除かれ、低木は近くに残る**ので、
                         * 入口の前が「切り開いた回廊」ではなく**すぼまった林間の空き地**
                         * になる。半径を 1 つ決める作りでは、大木を除ける半径だと低木まで
                         * 消えて森が禿げる。
                         *
                         * 背の低いもの（草・石）は残す。入口を隠さないうえ、ここで消すと
                         * 入口の周りだけ地面がのっぺりする。
                         *
                         * **種は引ききってから捨てる。**ここまでの `next01` を飛ばすと
                         * 以降のマスの絵が全部ずれるので、決着させた町の絵が動く。
                         */
                        /*
                         * **入口そのものは見通しの規則から外す**（2026-08-11）。
                         * 空けたいマスには入口のマスも入っている（`town_plan.cpp` の (7)）ので、
                         * これを外さないと**ダンジョンの口が自分自身を隠すものとして
                         * 消える**——実際にイークの洞窟が岩の隙間だけになった（絵で見つけた）。
                         * ここで消したいのは「口を隠す木」であって、口ではない。
                         */
                        const bool is_entrance_cell = (role == CellRole::Stairs);
                        const bool hides_gate = use_town && !is_entrance_cell && (entry.top_z >= 1.f) && [&]() {
                            const int base_x = gx - static_cast<int>(entry.anchor_x);
                            const int base_y = gy - static_cast<int>(entry.anchor_y);
                            //! 南から見て何マス北まで隠すか（＝木の高さ。8 マスで頭打ち）。
                            const int reach = std::min(8, static_cast<int>(std::ceil(entry.top_z)));
                            for (int cy = entry.cover_y0 - reach; cy <= entry.cover_y1; ++cy) {
                                for (int cx = entry.cover_x0; cx <= entry.cover_x1; ++cx) {
                                    if (town->clear_at(base_x + cx, base_y + cy)) {
                                        return true;
                                    }
                                    //! 屋敷の入口の前（デザイン9）。持たない町では空のまま。
                                    const int mx = base_x + cx;
                                    const int my = base_y + cy;
                                    if (!mark_clear.empty() && (mx >= 0) && (my >= 0)
                                        && (mx < town->width) && (my < town->height)
                                        && (mark_clear[(static_cast<std::size_t>(my)
                                                           * static_cast<std::size_t>(town->width))
                                                + static_cast<std::size_t>(mx)]
                                            != 0u)) {
                                        return true;
                                    }
                                }
                            }
                            return false;
                        }();
                        if (!hides_gate) {
                            out.lib[static_cast<std::size_t>(pick)].push_back(object);
                            ++out.lib_instances;
                            /*
                             * 立つものも視線を遮る（木・門・階段）。**低いものは数えない。**
                             * 0.5 マス未満のものは見下ろす視線を実質さえぎらないので、
                             * これを遮蔽に数えると「草の脇に立っただけで全部が透ける」ことになる。
                             *
                             * **覆うのは footprint ではなく実体の広がり**（P10 レビュー 6）。
                             * 木の footprint は幹の 1 マスだが、葉は 3〜4 マスに張り出していて
                             * 視線はそこで遮られる（気づいたこと）。`LibraryEntry::cover_*` の注記。
                             */
                            if (entry.top_z >= 0.5f) {
                                note_cover(entry, object, entry.top_z);
                            }
                        }
                        //! 立てなかったマスにも**代わりの小物は撒かない**（そこは空けたい所である）。
                        object_placed = true;
                    }

                    /*
                     * ---- 装飾層（§9.2）。並びの意味（marks）から置く ----
                     * **役割の既定で描いたマスに限る。**地形の細別があるマス（溶岩・水・
                     * 草・店先など）に撒くと、溶岩の上に木箱が浮く（実際に出た）。
                     */
                    const bool open_floor = (role == CellRole::RoomFloor) || (role == CellRole::CorridorFloor);
                    /*
                     * **意匠が「撒いてよい」と挙げた地形**（決めたこと D7。2026-08-21）。
                     * 上の但し書きのとおり細別のマスには撒かないのが既定だが、**草地と木と水
                     * しか無いダンジョンでは枠が 1 度も回らない**（無縁塚 99.4% / 魔法の森
                     * 100% が細別。全フロア 14,000 マスの実測）。表が名指した地形だけ緩める。
                     * **確率も置き方も変えない**——変わるのは「どのマスで枠が回るか」だけ。
                     */
                    const bool styled_terrain = !props.prop_terrains.empty() && (memory != nullptr)
                        && (std::find(props.prop_terrains.begin(), props.prop_terrains.end(),
                                static_cast<int>(memory->id_at(gx, gy)))
                            != props.prop_terrains.end());
                    const bool plain_cell = plain_ground || town_ground_used || styled_terrain;
                    /*
                     * **入口の南のマスには小物を置かない**（2026-08-09 に決めた:
                     * 「入り口タイルの南には小物を置かずに看板が見えやすいように」）。
                     * カメラは南から見下ろすので、ここに何か立つと看板と門の前に被る。
                     */
                    //! ダンジョンの口の南も同じ（2026-08-11。木を退けた跡に樽が立つのは惜しい）。
                    const bool front_of_gate = (use_town && (town->role_at(gx, gy - 1) == TownRole::Gate))
                        || (meaning.role_at(gx, gy - 1) == CellRole::Stairs);
                    if (open_floor && plain_cell && !front_of_gate && !structure_placed && !object_placed) {
                        const bool in_dungeon = meaning.identity.kind == static_cast<int>(FloorKind::Dungeon);
                        /*
                         * **小物のロック**（`terrain_view.h` の `PropLatch`。2026-08-11 に決めた:
                         * 「ダンジョンの小物は一度生成されたら書き換わらないように」）。
                         *
                         * 掛けるのは**地下だけ**。町の街灯は夜の度合いで明るさが変わるので、
                         * 置いたときの自発光を覚えると日が暮れても灯らない。
                         */
                        const bool latch_on = (latch != nullptr) && in_dungeon;
                        const int latch_key = (gy * meaning.width) + gx;
                        //! 置いたものをそのまま置き直す（種も枝も引き直さない）。
                        const auto replay = [&](const std::vector<LatchedProp> &kept) {
                            for (const LatchedProp &kp : kept) {
                                if ((kp.prefab < 0) || (kp.prefab >= static_cast<int>(out.lib.size()))) {
                                    continue;
                                }
                                if (kp.cover_top > 0.f) {
                                    note_cover(library->entry(kp.prefab), kp.instance, kp.cover_top);
                                }
                                out.lib[static_cast<std::size_t>(kp.prefab)].push_back(kp.instance);
                                ++out.lib_instances;
                                ++out.prop_count;
                            }
                        };
                        if (latch_on) {
                            const auto kept = latch->cells.find(latch_key);
                            if (kept != latch->cells.end()) {
                                replay(kept->second);
                                continue; // このマスは覚えたとおりに置けた
                            }
                        }
                        //! このマスで置いたもの（ロックを掛けるときだけ溜める）。
                        std::vector<LatchedProp> latched;
                        /*
                         * 装飾は**マスの中央に置かない。必ず四隅のどれかへ寄せる**
                         * （2026-08-09 に決めた:「マスの中央には配置禁止で、必ず四隅に
                         * 寄せることでフレーバーと認識できるように」）。
                         *
                         * 中央に置くと「そのマスの主役」に見え、拾える物や通せんぼと
                         * 見分けが付かない。隅に寄っているものは背景の飾りとして読める。
                         * `jitter` は隅の位置からの**ゆらぎ**の量で、中央へは戻さない。
                         */
                        const float corner_pick = next01(seed);
                        const float corner_x = (corner_pick < 0.5f) ? -kPropCornerOffset : kPropCornerOffset;
                        const float corner_y = (std::fmod(corner_pick * 4.f, 2.f) < 1.f) ? -kPropCornerOffset
                                                                                         : kPropCornerOffset;
                        /*
                         * **通路の装飾は半分の大きさまで**（2026-08-09 に決めた:
                         * 「通路の地面の小物は今の半分くらいのサイズを上限に。大きいと
                         * 透過したときに見づらい」）。通路は幅 1 マスで、そこに等身大の
                         * 瓦礫や石筍が立つと、透過して見ようとしている当人を隠してしまう。
                         * 部屋は広いので従来の大きさのままにする。
                         */
                        const float prop_scale = (role == CellRole::CorridorFloor) ? 0.5f : 1.f;
                        /*!
                         * 隅を指定して 1 つ置く。既定の隅で置く `emit_prop` と、
                         * **同じマスに何株も置く**草（`tuft_tries`）が共有する。
                         */
                        auto emit_prop_at = [&](int prefab_index, float at_x, float at_y, float jitter,
                                               float er = 0.f, float eg = 0.f, float eb = 0.f) {
                            if (prefab_index < 0) {
                                return; // その装飾の素材がライブラリに無い。置かないだけ
                            }
                            const LibraryEntry &entry = library->entry(prefab_index);
                            InstanceData prop;
                            const float wobble = std::min(jitter, kPropCornerOffset * 0.5f);
                            prop.x = fx - entry.anchor_x + at_x + next_range(seed, -wobble, wobble);
                            prop.y = fy - entry.anchor_y + at_y + next_range(seed, -wobble, wobble);
                            prop.z = -kFootingDepth;
                            prop.sx = prop.sy = prop.sz = prop_scale;
                            const float shift = 1.f + (next_range(seed, -1.f, 1.f) * 0.07f * mottle);
                            prop.r = prop.g = prop.b = shift;
                            prop.er = er;
                            prop.eg = eg;
                            prop.eb = eb;
                            /*
                             * 装飾も背が高ければ視線を遮る（柱は 1.7 マスある）。
                             * **縮めた実効の高さで見る**（通路の装飾は半分の大きさ）。
                             */
                            const float effective_top = entry.top_z * prop_scale;
                            if (effective_top >= 0.5f) {
                                note_cover(entry, prop, effective_top);
                            }
                            out.lib[static_cast<std::size_t>(prefab_index)].push_back(prop);
                            ++out.lib_instances;
                            ++out.prop_count;
                            //! ロックを掛けるマスなら、置いたものをそのまま控える（次からはこれを置き直す）。
                            if (latch_on) {
                                latched.push_back(LatchedProp{ prefab_index, prop,
                                    (effective_top >= 0.5f) ? effective_top : 0.f });
                            }
                        };
                        auto emit_prop = [&emit_prop_at, corner_x, corner_y](int prefab_index, float jitter,
                                             float er, float eg, float eb) {
                            emit_prop_at(prefab_index, corner_x, corner_y, jitter, er, eg, eb);
                        };
                        /*
                         * **街路樹と街灯**（2026-08-09 に決めた:「街路樹と一定間隔で
                         * 立てられた街灯」）。どちらも**道に face したマス**にだけ立てる
                         * ——道の上に立てると通りをふさいで見えるし、野原の真ん中に立つと
                         * 「街路」樹に見えない。街灯は `lamp_step` の格子で**間隔を揃える**。
                         */
                        /*
                         * **`town` は地下では null。**意匠を引くのは町の中だけにする
                         * （ここは地上と地下で共通の装飾層で、`use_town` を見ずに
                         * `town->identity` を触った版は `--world-check` が落ちた）。
                         */
                        const TownStyle &street_style = style;
                        static constexpr float kWhite[3] = { 1.f, 1.f, 1.f };
                        bool street_placed = false;
                        /*
                         * **地面が道にも当たる区画（`apply_to: "all"`）の中では街路樹も街灯も
                         * 立てない**（テルモラ §9.13。2026-09-06 に決めた「通りの中に街路樹、
                         * 街灯、小物は撤去で」）。広場や大通りは区画の地面で一続きにしてあるので、
                         * 道に face した Turf のマスがその内側にも生まれる。縁石と同じ判定。
                         */
                        const bool zone_swallows_street = (zone_here_idx >= 0)
                            && (townset.zones[static_cast<std::size_t>(zone_here_idx)].apply_to == 2);
                        if (use_town && (town_role == TownRole::Turf) && !zone_swallows_street
                            && ((townset.street_tree >= 0) || (townset.lamp_post >= 0))) {
                            /*
                             * 道の**どちら側に face しているか**で、間隔を測る軸を選ぶ。
                             * 南北の隣が道なら通りは東西に走っているので `gx` で数える。
                             * 両方の軸を「`step` の倍数」で縛った版は交差点にしか立たず、
                             * 町全体で 11 本しか灯らなかった（実機の絵で気づいた）。
                             */
                            //! **差し替えのマスには街灯も街路樹も立てない**（デザイン14）。
                            bool by_path = false;
                            bool along_x = false;
                            for (int i = 0; (i < 4) && !by_path && (terrain_prop == nullptr); ++i) {
                                const int nx = gx + ((i == 2) ? -1 : ((i == 3) ? 1 : 0));
                                const int ny = gy + ((i == 0) ? -1 : ((i == 1) ? 1 : 0));
                                if (town->role_at(nx, ny) != TownRole::Path) {
                                    continue;
                                }
                                by_path = true;
                                along_x = (i < 2); //!< 北か南が道＝通りは東西に走る
                            }
                            const int step = street_style.lamp_step;
                            const bool on_grid = (step > 0) && (((along_x ? gx : gy) % step) == 0);
                            /*
                             * **街路樹を格子で等間隔に**（デザイン §9.6。テルモラの「秩序と
                             * 威厳」への作り直し）。`street_tree_step` が正なら、街灯の格子の
                             * ちょうど中間（`step/2` だけずらした剰余）へ乱択せず立てる。
                             * 0（既定）なら従来どおり——街灯の無い場所へ 30% の乱択で立てる
                             * （テルモラ以前の全町はこの経路のまま。1 ボクセルも動かない）。
                             */
                            const int tree_step = street_style.street_tree_step;
                            const bool on_tree_grid = (tree_step > 0)
                                && (((along_x ? gx : gy) % tree_step) == (tree_step / 2));
                            if (by_path && on_grid && (townset.lamp_post >= 0)) {
                                //! 街灯。**火は別体**（自発光はインスタンス単位＝罠 39）。
                                place_town(townset.lamp_post, fx + corner_x, fy + corner_y,
                                    -kFootingDepth, kWhite, top_of(townset.lamp_post));
                                if (night_eff > 0.02f) {
                                    const float glow = night_eff * 2.1f;
                                    place_town(townset.lamp_flame, fx + corner_x, fy + corner_y,
                                        -kFootingDepth, kWhite, 0.f, glow, glow * 0.74f, glow * 0.36f);
                                }
                                ++out.prop_count;
                                street_placed = true; // 街灯のマスには草も小物も置かない
                            } else if (by_path && !on_grid && (townset.street_tree >= 0) && (tree_step > 0)
                                && on_tree_grid) {
                                place_town(townset.street_tree, fx + corner_x, fy + corner_y,
                                    -kFootingDepth, kWhite, top_of(townset.street_tree));
                                ++out.prop_count;
                                street_placed = true;
                            } else if (by_path && !on_grid && (townset.street_tree >= 0) && (tree_step <= 0)
                                && (next01(seed) < 0.30f)) {
                                place_town(townset.street_tree, fx + corner_x, fy + corner_y,
                                    -kFootingDepth, kWhite, top_of(townset.street_tree));
                                ++out.prop_count;
                                street_placed = true;
                            }
                        }
                        /*
                         * **道の縁の草**（OUTPOST_TOWN_DESIGN §5 `path_edge`）。`Turf` の
                         * マスの各辺で、隣が `Path` ならその辺へ寄せて置く（1 マスに何辺でも）。
                         * 表に書いていない町では `townset.path_edge < 0` のままなので、
                         * この枝は 1 度も通らない。
                         *
                         * **`zones` の `ground` が道にも当たる区画（`apply_to: "all"`）の中では
                         * 置かない**（テルモラの直し第 2 回 §9.9 #2。こう気づいた——「縁石が広場の
                         * 中に格子を描く」——区画の中は道も区画の地面に置き換わっているので、
                         * 縁石を足すと区画の中にマス目が浮く）。この Turf のマス自身が属する区画を
                         * 見る——`apply_to: "all"` の区画は Path のマスも同じ区画に属するので、
                         * 隣が Path でもここが縁ではなく区画の内側だと分かる。
                         */
                        const bool zone_swallows_edge = (zone_here_idx >= 0)
                            && (townset.zones[static_cast<std::size_t>(zone_here_idx)].apply_to == 2);
                        if (use_town && (town_role == TownRole::Turf) && !brook_here
                            && (townset.path_edge >= 0) && !zone_swallows_edge) {
                            static constexpr int kEdgeDir[4][2] = {
                                { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } // N / S / W / E
                            };
                            constexpr float kEdgeOffset = 0.38f;
                            static constexpr float kEdgePlain[3] = { 1.f, 1.f, 1.f };
                            for (const auto &dir : kEdgeDir) {
                                if (town->role_at(gx + dir[0], gy + dir[1]) != TownRole::Path) {
                                    continue;
                                }
                                place_town(townset.path_edge, fx + (dir[0] * kEdgeOffset),
                                    fy + (dir[1] * kEdgeOffset), -kFootingDepth, kEdgePlain);
                            }
                        }
                        /*
                         * 町の草地の小物（2026-08-09 に決めた:「タイルに沿うものと、
                         * **沿わない小物としてのオブジェクトがたくさんあっていい**」）。
                         * 草の茂みと小石を薄く撒く。道の上には撒かない（踏み跡なので）。
                         */
                        if (street_placed || (terrain_prop != nullptr) || brook_here || zone_here) {
                            //! 街路樹・街灯を立てたマスには重ねない。**差し替えのマスにも撒かない**
                            //! （デザイン14。奈落の底に彼岸花が生えていては話が合わない。
                            //! 小川も同じ——OUTPOST_TOWN_DESIGN §5。区画のマスも同じ理由
                            //! （§7.2。区画は自分の `props` で決着済み——一般の草地の小物を
                            //! 重ねると畑の畝に雑草が生える）。
                        } else if (town_role == TownRole::Turf) {
                            /*
                             * 草の濃さは**町ごと**（`TownStyle::tuft_chance` と `tuft_tries`）。
                             * テルモラは2026-08-09 に決めたで「道以外の平地には背の高めの
                             * ススキが風の波に揺らされている」「もっと沢山。**いまの三倍**くらいに」。
                             * 割合は 1 を超えられないので、**1 マスに何株も生やす**ことで濃くする。
                             * モリバントは「町の中の地面には草はなし」なので割合が 0。
                             */
                            const TownStyle &grass_style = street_style; //!< 同じ町の意匠
                            const float grass_chance = grass_style.tuft_chance;
                            const float which = next01(seed);
                            if (which < grass_chance) {
                                emit_prop(townset.tuft, 0.16f, 0.f, 0.f, 0.f);
                            } else if (which < (grass_chance + 0.06f)) {
                                emit_prop(props.rubble, 0.16f, 0.f, 0.f, 0.f);
                            }
                            /*
                             * 2 株目から。**`tuft_tries` が 1 なら 1 度も回らない**ので、
                             * 辺境の地では種の消費が 1 ビットも変わらない（絵が動かない）。
                             * 隅は株ごとに引き直す（同じ隅に重ねると 1 株に見える）。
                             */
                            for (int extra = 1; extra < grass_style.tuft_tries; ++extra) {
                                if (next01(seed) >= grass_chance) {
                                    continue;
                                }
                                const float pick = next01(seed);
                                const float ex = (pick < 0.5f) ? -kPropCornerOffset : kPropCornerOffset;
                                const float ey = (std::fmod(pick * 4.f, 2.f) < 1.f) ? -kPropCornerOffset
                                                                                    : kPropCornerOffset;
                                emit_prop_at(townset.tuft, ex, ey, 0.13f);
                            }
                            /*
                             * **草地の小物**（OUTPOST_TOWN_DESIGN §5 `turf_props`）。低木・花・
                             * 切り株・岩・きのこなど、株（`grass`）とは別に町ごとの表で撒く。
                             * 表が空の町ではここが 1 度も回らない。
                             */
                            for (const auto &tp : townset.turf_props) {
                                if (tp.variants.empty()) {
                                    continue;
                                }
                                if (next01(seed) >= tp.chance) {
                                    continue;
                                }
                                const float pick = next01(seed);
                                const float tx = (pick < 0.5f) ? -kPropCornerOffset : kPropCornerOffset;
                                const float ty = (std::fmod(pick * 4.f, 2.f) < 1.f) ? -kPropCornerOffset
                                                                                    : kPropCornerOffset;
                                emit_prop_at(choose(tp.variants, seed), tx, ty, 0.13f);
                            }
                        } else if ((mark & MARK_DEAD_END) != 0) {
                            emit_prop(props.dead_end_pile, 0.10f, 0.f, 0.f, 0.f);
                            if (next01(seed) < 0.45f) {
                                const bool glow = in_dungeon && (next01(seed) < 0.6f);
                                if (glow) {
                                    // 光茸。ほのかな自発光（ブルームは掴まない程度）。
                                    emit_prop(props.mushroom_glow, 0.16f, 0.12f, 0.34f, 0.26f);
                                } else {
                                    emit_prop(props.mushroom_brown, 0.16f, 0.f, 0.f, 0.f);
                                }
                            }
                        } else if (((mark & MARK_WALL_SIDE) != 0) && (role == CellRole::CorridorFloor)
                            && (next01(seed) < 0.30f)) {
                            const float which = next01(seed);
                            if (which < 0.40f) {
                                emit_prop(props.wall_side[0], 0.15f, 0.f, 0.f, 0.f);
                            } else if (which < 0.70f) {
                                emit_prop(props.wall_side[1], 0.15f, 0.f, 0.f, 0.f);
                            } else {
                                emit_prop(props.wall_side[2], 0.15f, 0.f, 0.f, 0.f);
                            }
                        } else if ((mark & MARK_ROOM_EDGE) != 0) {
                            const float which = next01(seed);
                            if (which < 0.10f) {
                                emit_prop(props.room_edge_pillar, 0.f, 0.f, 0.f, 0.f);
                            } else if (which < 0.165f) {
                                /*
                                 * 立ち松明。**明るいと確定した部屋にだけ置く**（決めたこと
                                 * 2026-08-09。暗い部屋で松明が燃えていたら部屋が暗いことと
                                 * 矛盾する）。「明るい」はコアの CAVE_GLOW（`CELL_FEAT_GLOWING`。
                                 * プレイヤの灯りは含まない）で、間近で見て覚えたマスにしか無い。
                                 *
                                 * **炎だけ別のプレハブ**にしてある。自発光はインスタンス単位で
                                 * しか渡せないので、柱と一緒だと鉄柱まで光ってしまう
                                 * （P10 で最初にそうなった）。1 を超える値はブルームの相手
                                 * （P7 の宿題「地上で 1 画素も効かない」への答えがこれ）。
                                 *
                                 * **消えている火は「明るい部屋」を要らない**（P10 第 4 期）。
                                 * イークの洞穴の焚き火跡（`ash_pit`）は燃えていないので、
                                 * 明るさと矛盾しない——縛りを掛けたままだと浅い階でしか出ない。
                                 */
                                const std::uint16_t seen_flags = (memory != nullptr) ? memory->flags_at(gx, gy) : 0;
                                const bool lit_room = ((seen_flags & CELL_FEAT_ROOM) != 0u)
                                    && ((seen_flags & CELL_FEAT_GLOWING) != 0u);
                                if (lit_room || !props.fire_needs_lit) {
                                    emit_prop(props.fire_body, 0.f, 0.f, 0.f, 0.f);
                                    emit_prop(props.fire_flame, 0.f, 1.35f, 0.85f, 0.30f);
                                }
                            }
                        } else if ((role == CellRole::RoomFloor) && (next01(seed) < 0.045f)) {
                            const float which = next01(seed);
                            if (which < 0.40f) {
                                emit_prop(props.room_floor[0], 0.20f, 0.f, 0.f, 0.f);
                            } else if (which < 0.75f) {
                                emit_prop(props.room_floor[1], 0.20f, 0.f, 0.f, 0.f);
                            } else {
                                emit_prop(props.room_floor[2], 0.20f, 0.f, 0.f, 0.f);
                            }
                        }
                        /*
                         * **置いたマスだけ覚える。**何も置かなかったマスを覚えると、まだ細別が
                         * 届いていない段で「何も無い」と決まってしまい、近づいても
                         * 木も溶岩の泡も出なくなる（`terrain_view.h` の `PropLatch` の注記）。
                         */
                        if (latch_on && !latched.empty()) {
                            latch->cells.emplace(latch_key, std::move(latched));
                        }
                    }
                    emit_fps_layers(gx, gy, fx, fy, role); //!< 一人称の 2 段目と天井（別系統の種）
                    continue; // このマスはライブラリで描けた。仮の箱と板は出さない
                }
            }

            switch (role) {
            case CellRole::StructuralWall: {
                // **加工された壁。**高さは 1.0 ちょうどで、揃っていることが「作られた」感になる。
                InstanceData box;
                box.x = fx;
                box.y = fy;
                box.z = -kFootingDepth;
                box.sz = 1.f + kFootingDepth;
                tint(box, kStructuralWall, seed, 0.04f);
                apply_wall_inset(gx, gy, box);
                out.boxes.push_back(box);
                note_occluder(gx, gy, 1.f);
                break;
            }
            case CellRole::Bedrock: {
                /*
                 * **掘りっぱなしの岩塊。**高さを種で散らす。
                 *
                 * **平面方向は縮めない。**最初は食い込み（`sx = 1 - inset`）も入れていたが、
                 * 隣り合う岩盤の間に隙間ができ、そこから背景が見えた。四隅の欠け検査が
                 * 見下ろし 42°／画角 44°／1 マス 70px で 0.45% を出して見つけた
                 * （既定のカメラでは 0.000% のまま出なかった）。
                 * 荒々しさは**高さの段差と色**で出す。
                 */
                InstanceData box;
                box.x = fx;
                box.y = fy;
                box.z = -kFootingDepth;
                /*
                 * **立方体のときは散らさない**（`blocks_only`）。高さがまちまちだと
                 * 「シンプルな立方体」に見えない（2026-08-19 に決めた）。
                 * 種は**引いてから捨てる**——引かないと以降の乱数列がずれて、
                 * 画調を切り替えるたびに色むらまで変わる。
                 */
                const float rough = next_range(seed, 1.05f, 1.55f);
                box.sz = (blocks_only ? 1.f : rough) + kFootingDepth;
                tint(box, kBedrock, seed, 0.10f);
                apply_wall_inset(gx, gy, box);
                out.boxes.push_back(box);
                note_occluder(gx, gy, box.sz - kFootingDepth);
                break;
            }
            case CellRole::RoomFloor:
            case CellRole::CorridorFloor:
            case CellRole::Doorway:
            case CellRole::Stairs: {
                InstanceData slab;
                slab.x = fx;
                slab.y = fy;
                slab.z = 0.f;
                if (role == CellRole::RoomFloor) {
                    /*
                     * 市松の意匠。**部屋の床は加工されている**ことを絵で言う。
                     * `HD2D_TERRAIN_VARIANCE` で 2 色の差も詰める（切り分けの口）。
                     */
                    const float mix = terrain_variance();
                    const Rgb checker{ kRoomFloorB.r + ((kRoomFloorA.r - kRoomFloorB.r) * mix),
                        kRoomFloorB.g + ((kRoomFloorA.g - kRoomFloorB.g) * mix),
                        kRoomFloorB.b + ((kRoomFloorA.b - kRoomFloorB.b) * mix) };
                    tint(slab, (((gx + gy) & 1) != 0) ? checker : kRoomFloorB, seed, 0.03f);
                } else if (role == CellRole::CorridorFloor) {
                    slab.z = next_range(seed, -0.03f, 0.f); // わずかに凹凸
                    tint(slab, kCorridorFloor, seed, 0.10f);
                } else if (role == CellRole::Doorway) {
                    tint(slab, kDoorway, seed, 0.05f);
                } else {
                    tint(slab, kStairs, seed, 0.05f);
                }
                out.slabs.push_back(slab);

                if (role == CellRole::Stairs) {
                    // 階段は目印の箱を 1 つ載せる（P3 では形を作り込まない）。
                    InstanceData box;
                    box.x = fx + 0.25f;
                    box.y = fy + 0.25f;
                    box.z = -kFootingDepth;
                    box.sx = 0.5f;
                    box.sy = 0.5f;
                    box.sz = 0.28f + kFootingDepth;
                    tint(box, kStairs, seed, 0.05f);
                    out.boxes.push_back(box);
                    ++out.prop_count;
                    break;
                }
                if (role == CellRole::Doorway) {
                    // 扉は左右の門柱だけ立てる（§10.3 の「自立した門」の最も素朴な形）。
                    if (blocks_only) {
                        break; //!< 小物は 1 つも置かない（2026-08-19 に決めた）
                    }
                    for (int side = 0; side < 2; ++side) {
                        InstanceData post;
                        post.x = fx + ((side == 0) ? 0.02f : 0.84f);
                        post.y = fy + 0.36f;
                        post.z = -kFootingDepth;
                        post.sx = 0.14f;
                        post.sy = 0.28f;
                        post.sz = 1.1f + kFootingDepth;
                        tint(post, kStructuralWall, seed, 0.04f);
                        out.boxes.push_back(post);
                        ++out.prop_count;
                    }
                    break;
                }

                /* ---- 装飾層（§9.2）。**当たり判定に関係しない。密度で撒く** ---- */
                /*
                 * **`blocks_only` では 1 つも置かない**（2026-08-19 に決めた
                 * 「小物オブジェクトは不要」）。マスに立方体が 1 つ建つだけの世界にする。
                 */
                if (blocks_only) {
                    break;
                }
                if ((mark & MARK_DEAD_END) != 0) {
                    // 行き止まりには瓦礫（§9.1）。
                    const int rubble = 2 + static_cast<int>(next_range(seed, 0.f, 2.99f));
                    for (int i = 0; i < rubble; ++i) {
                        InstanceData prop;
                        const float size = next_range(seed, 0.10f, 0.26f);
                        prop.x = fx + next_range(seed, 0.05f, 0.95f - size);
                        prop.y = fy + next_range(seed, 0.05f, 0.95f - size);
                        prop.z = -kFootingDepth;
                        prop.sx = size;
                        prop.sy = size;
                        prop.sz = (size * next_range(seed, 0.5f, 1.1f)) + kFootingDepth;
                        tint(prop, kProp, seed, 0.12f);
                        out.boxes.push_back(prop);
                        ++out.prop_count;
                    }
                } else if (((mark & MARK_WALL_SIDE) != 0) && (role == CellRole::CorridorFloor)
                    && (next01(seed) < 0.30f)) {
                    // 通路の壁際に小さな落石。
                    InstanceData prop;
                    const float size = next_range(seed, 0.08f, 0.20f);
                    prop.x = fx + next_range(seed, 0.05f, 0.95f - size);
                    prop.y = fy + next_range(seed, 0.05f, 0.95f - size);
                    prop.z = -kFootingDepth;
                    prop.sx = size;
                    prop.sy = size;
                    prop.sz = (size * next_range(seed, 0.4f, 0.9f)) + kFootingDepth;
                    tint(prop, kProp, seed, 0.12f);
                    out.boxes.push_back(prop);
                    ++out.prop_count;
                } else if (((mark & MARK_ROOM_EDGE) != 0) && (next01(seed) < 0.10f)) {
                    // 部屋の縁にときどき柱（§9.1「部屋＝加工された石積み・柱」）。
                    InstanceData pillar;
                    pillar.x = fx + 0.36f;
                    pillar.y = fy + 0.36f;
                    pillar.z = -kFootingDepth;
                    pillar.sx = 0.28f;
                    pillar.sy = 0.28f;
                    pillar.sz = next_range(seed, 1.5f, 1.8f) + kFootingDepth;
                    tint(pillar, kPillar, seed, 0.05f);
                    out.boxes.push_back(pillar);
                    ++out.prop_count;
                }
                break;
            }
            case CellRole::Unknown:
            default:
                break;
            }
            emit_fps_layers(gx, gy, fx, fy, role); //!< 一人称の 2 段目と天井（ライブラリの経路と同じ約束）
        }
    }
}

namespace {

//! 1 本ぶんの視線。`target_z` はプレイヤ側の高さ（マス）。
bool ray_blocked(const TerrainView &view, const Vec3 &eye, float tx, float ty, float target_z,
    int player_gx, int player_gy)
{
    const float dx = tx - eye.x;
    const float dy = ty - eye.y;
    const float dz = target_z - eye.z;
    const float span = std::sqrt((dx * dx) + (dy * dy));
    if (span < 0.001f) {
        return false; // 真上から見ている。遮るものは無い
    }
    /*
     * 0.25 マス刻みで歩く。マスの対角を突っ切る場合でも取りこぼさない細かさで、
     * かつ視界（30 マス程度）でも 120 歩ほどにしかならない。
     */
    const int steps = static_cast<int>(span * 4.f);
    int last_gx = -1;
    int last_gy = -1;
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int gx = static_cast<int>(std::floor(eye.x + (dx * t)));
        const int gy = static_cast<int>(std::floor(eye.y + (dy * t)));
        if ((gx == last_gx) && (gy == last_gy)) {
            continue;
        }
        last_gx = gx;
        last_gy = gy;
        if ((gx == player_gx) && (gy == player_gy)) {
            continue; // プレイヤ自身のマス
        }
        const float ray_z = eye.z + (dz * t);
        if (view.occluder_at(gx, gy) > ray_z) {
            return true;
        }
    }
    return false;
}

} // namespace

/*!
 * @details **プレイヤは点ではない。**2026-08-09 に気づいた:「ブロックで隠れているが
 * 頭部の一部が見えることで視線が通っている扱いになるのか、透過が発生していない」。
 *
 * 腰（0.55）1 本だけで見ていたのが原因である。手前の壁が下半身を隠していても、
 * 腰へのレイが壁の天面をわずかに越えていれば「通っている」になっていた。
 * **足元・腰・肩の 3 本を引き、1 本でも遮られたら遮蔽とする。**
 * 遊ぶ側にとって「一部でも隠れている」は「見えていない」のと同じで、
 * どこが隠れているかを気にして立ち位置を直させるいわれは無い。
 *
 * マスの中を辿るのは DDA で、**カメラのマスとプレイヤのマスは見ない**
 * （自分の足元の床や、カメラが埋まっている岩で常に遮蔽になってしまう）。
 */
bool line_of_sight_blocked(const TerrainView &view, const Vec3 &eye, int player_gx, int player_gy)
{
    if (view.occluder_z.empty()) {
        return false;
    }
    const float tx = static_cast<float>(player_gx) + 0.5f;
    const float ty = static_cast<float>(player_gy) + 0.5f;
    //! 足元・腰・肩。**上から順に見ない**（低いほうが遮られやすいので、早く決まる）。
    for (const float target_z : { 0.08f, 0.55f, 0.95f }) {
        if (ray_blocked(view, eye, tx, ty, target_z, player_gx, player_gy)) {
            return true;
        }
    }
    return false;
}

Prefab make_box_prefab(const std::string &name, int sx, int sy, int sz, int origin_z, int voxels_per_cell)
{
    Prefab prefab;
    prefab.name = name;
    prefab.voxels_per_cell = voxels_per_cell;

    VoxModel model;
    model.size[0] = sx;
    model.size[1] = sy;
    model.size[2] = sz;
    model.voxels.assign(static_cast<std::size_t>(sx) * static_cast<std::size_t>(sy) * static_cast<std::size_t>(sz), 1);
    prefab.vox.models.push_back(std::move(model));
    // 索引 1 = 白。実際の色はインスタンスごとの色が掛かる。
    prefab.vox.palette[1][0] = 255;
    prefab.vox.palette[1][1] = 255;
    prefab.vox.palette[1][2] = 255;
    prefab.vox.palette[1][3] = 255;
    /*
     * **材質は宣言する**（2026-08-23。「パレットの色で汚しを入れるのをやめよう」と決めた）。
     *
     * ここは地形の床・壁・地面の下敷きで、**色はインスタンスごとの色で決まる**（白 × 色）。
     * つまり色を見ても材は分からない——白は「まだ色が付いていない」という意味しか持たない。
     * 石にしておくのは、床・壁・舗装がこの箱で描かれるためである。
     *
     * @note 地形の種類（草地・水辺）ごとに分けるには**インスタンスに 1 本足す**必要がある。
     * いまは分けていないので、草地のマスにも石の汚れ方が掛かる（弱い斑なので目には出にくい）。
     */
    prefab.palette_material[1] = static_cast<std::uint8_t>(MaterialClass::Stone);
    prefab.palette_material_declared = true;

    VoxPart vox_part;
    vox_part.name = "main";
    vox_part.model_index = 0;
    vox_part.origin[2] = origin_z;
    prefab.vox.parts.push_back(vox_part);

    PrefabPart part;
    part.name = "main";
    part.voxels = "main";
    part.model_index = 0;
    part.offset[2] = static_cast<float>(origin_z);
    prefab.parts.push_back(part);
    return prefab;
}

} // namespace hd2d
