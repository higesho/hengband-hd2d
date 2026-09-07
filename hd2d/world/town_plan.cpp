/*!
 * @file town_plan.cpp
 * @brief `town_plan.h` の実装。
 */
#include "world/town_plan.h"

#include "world/terrain_view.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <sstream>

namespace hd2d {

namespace {

/*!
 * @name 敷地とみなす条件（設計書 §10.1 の実測に合わせてある）
 *
 * @details Outpost の永久壁 21 成分を数えると、
 * | 何 | 大きさ | 外接矩形の充填率 |
 * |---|---|---|
 * | 世界の枠 | 198×66 の縁 | 0.04 |
 * | 町の城壁 | 71×17 / 71×14 | 0.18 / 0.20 |
 * | 店・施設の敷地 | 4×3 〜 13×4 | **0.75 〜 1.00** |
 * とはっきり分かれる。**塊か線かの違い**なので、充填率と大きさで足りる。
 *
 * @note ここは「町の中でだけ」効く（`rebuild_town_plan` が `town_id` を見る）。
 * 荒野の山を敷地と読む心配は要らない。
 * @{
 */
constexpr int kMaxSiteSpan = 26; //!< 外接矩形がこれより大きい成分は敷地としない
constexpr float kSiteFill = 0.72f; //!< 外接矩形の充填率
constexpr int kMinSiteCells = 4;
/*! @} */

//! 建物の最小の大きさ。**1 マス幅の建物は作らない**（§10.4 の「露店」へ落とす）。
constexpr int kMinHouseSpan = 2;

/*!
 * @brief 壁の切れ目を「入口」と読む上限（マス）。
 * @details テルモラの城は南面に**4 マス**の門を持つ。ここを 1 に縛っていた版は、
 * 城の正面に門も看板も立たなかった（`--town-check` の (2) が 21/25 で落ちた）。
 * 逆に大きくしすぎると**中庭が門になる**（同じ城の中庭は 11 マス幅）。
 */
constexpr int kMaxGateSpan = 4;

/*!
 * @brief やぐら（塔）を立てる塀の最小の大きさ（マス）。
 * @details 小さな石の塊まで「塀」として四隅に塔を立てると、**モリバントの噴水が
 * 円塔の林**になった。町を囲む塀は 99〜702 マスあるので、ここは十分に低くてよい。
 *
 * **12 では足りなかった。**噴水を囲む石の縁（16 マス）に円塔が 2 本立ち、噴水が
 * 小さな城のように見えた（実機の絵で気づいた）。町の塀といちばん小さな飾りの間は
 * 99 対 16 と開いているので、その間を採る。
 */
constexpr int kMinTowerCells = 40;

/*!
 * @brief 噴水の縁と読み替える範囲（噴き上げからのチェビシェフ距離・マス）。
 * @details モリバントの噴水は 7×5 マスなので、中央から 3 マスで外の縁まで届く。
 */
constexpr int kFountainRim = 3;

/*!
 * @name 岩山（P10 第 3 期。2026-08-10 に決めた）
 *
 * @details ズルの山塊（11,642 マス）と、他の 4 町でいちばん大きい非敷地の成分
 * （テルモラの世界の枠 991 マス）の間を採る。**大きさだけで見分けられる**のは、
 * 「行動可能範囲と侵入不可領域以外はコアの地図を気にしなくてよい」（2026-08-10）
 * ——つまり**巨大な侵入不可の塊は山だと読んでよい**からである。
 * 地形 id を見に行くと罠 40（間近で見たマスにしか無い）を踏む。
 * @{
 */
constexpr int kCragCells = kCragMassCells; //!< このマス数を超える成分は山（宣言は town_plan.h）
constexpr int kCragTiers = 4; //!< 麓 → 峰の段数（素材は `crag_low/mid/high/peak`）
/*!
 * @brief 看板の南、岩を麓の段へ落とすマス数（2026-08-10 に決めた）。
 * @details 山肌の入口の看板が**南の高い岩に隠れていた**。カメラは南から見下ろすので、
 * 看板の手前（南）だけは低くする。3 マスは指定した値そのもの。
 */
constexpr int kSignClearSouth = 3;
/*! @} */

/*!
 * @brief 門の見通し（同上「建物の入り口に木のオブジェクトが被らないように」）。
 * @details 印を付けるのは**門のマスとその周り 1 マス**だけ。南へどこまで空けるかは
 * 素材の高さで決まるので、置く側が決める（`terrain_view.cpp` の「見通しを塞ぐか」）。
 */
constexpr int kClearRing = 1;

//! 2 階建てにする確率（建物が広いときだけ）。
constexpr float kTwoStoreyChance = 0.34f;

/*!
 * @brief 町ごとの意匠の表（`town_style_for` が引く。宣言は `town_plan.h` の `TownStyle`）。
 *
 * @details **辺境の地の値は 1 つも動かさない**（実機で見て閉じている）。
 * 足すときはここへ 1 行と、`gen_prefabs.py` の `TOWN_STYLES` へ 1 語。
 *
 * 色の振り幅は町ごとに性格が違う。辺境と藁葺きは**焼け具合**（暖色の濃淡）で振れるが、
 * 瓦は**焼きの色**そのものが個体差なので、赤茶と青灰の間で振る。
 */
struct StyleRow {
    int town_id;
    TownStyle style;
};

/*!
 * @name 町ごとの目印の建物（2026-08-09 に決めた）
 * @details 建物の名前は町のデータ（`B:` 行）が決めていて、**同じ地形 id が町ごとに別の建物**
 * である。だから表は町ごとに持つ——辺境の地の `BUILDING_1` は村長の家で、城ではない。
 * @{
 */
//! テルモラ: 城（`b` = `BUILDING_1`。デネゴール）。
const TownLandmark kTelmoraLandmarks[] = {
    { "BUILDING_1", "castle" },
    { nullptr, nullptr },
};
//! モリバント: 城（ブリン）と、**賢者の塔**（`i`）・**トランプ魔術の塔**（`n`）。
const TownLandmark kMorivantLandmarks[] = {
    { "BUILDING_1", "castle" },
    { "BUILDING_8", "tower_wizard" },
    { "BUILDING_13", "tower_wizard" },
    { nullptr, nullptr },
};
/*! @} */

/*!
 * @name 町ごとの建物の看板（P10 第 4 期。2026-08-10 に気づいた）
 *
 * @details 中身は `lib/edit/towns/*.txt` の `B:<n>:N:<名前>` をそのまま写したもの。
 * **同じ `BUILDING_n` が町ごとに別の建物**なので、terrain id では引けない
 * （`town_plan.h` の `TownSign` に経緯）。
 *
 * **絵は建物の種類ごとに 1 つずつ**（「同じ看板を別種の建物に使わないように」と決めた）。
 * 店との重なりも避けてある——図書館は `tome`（本屋 `book` とは別）、宿屋は `bed`
 * （我が家 `house` とは別）、武器匠は `anvil`（武器屋 `sword` とは別）、
 * 寺院の建物は `chapel`（寺院の店 `holy` とは別）。
 *
 * 村長（辺境）・城（テルモラ／モリバント）・荘園（アングウィル）は**同じ `crown`** に
 * してある。名前は違うが「その町の主の館」という同じ役目で、`BUILDING_1` という
 * 同じ枠に入っている。分けたくなったら 3 行を別々の絵にすればよい。
 * @{
 */
//! 辺境の地（TOWN 1）。**`01_Outpost_Full.txt`（既定の荒野）に合わせてある。**
//! `$WILDERNESS LITE` の地図は `BUILDING_2` 以降の割り当てが違う（そちらは合わない）。
const TownSign kOutpostSigns[] = {
    { "BUILDING_0", "sign_bed" }, //!< 旅の宿『白馬亭』
    { "BUILDING_1", "sign_crown" }, //!< 村長
    { "BUILDING_13", "sign_target" }, //!< ハンター事務所
    { nullptr, nullptr },
};
//! テルモラ（TOWN 2）。
const TownSign kTelmoraSigns[] = {
    { "BUILDING_0", "sign_tome" }, //!< 図書館
    { "BUILDING_1", "sign_crown" }, //!< 城
    { "BUILDING_2", "sign_trident" }, //!< 闘技場
    { "BUILDING_3", "sign_dice" }, //!< カジノ
    { "BUILDING_4", "sign_bed" }, //!< 宿屋
    { "BUILDING_5", "sign_fang" }, //!< モンスター仙人
    { "BUILDING_6", "sign_anvil" }, //!< 武器匠
    { "BUILDING_7", "sign_helm" }, //!< 戦士の集会所
    { "BUILDING_9", "sign_ankh" }, //!< 生命魔術の塔
    { "BUILDING_10", "sign_dagger" }, //!< 盗賊のギルド
    { "BUILDING_11", "sign_bow" }, //!< アーチャーのギルド
    { "BUILDING_12", "sign_paladin" }, //!< パラディンのギルド
    { nullptr, nullptr },
};
//! モリバント（TOWN 3）。**図書館に寝台が出ていたのはここ**（気づいたこと）。
const TownSign kMorivantSigns[] = {
    { "BUILDING_0", "sign_tome" }, //!< 図書館（ミリムバー）
    { "BUILDING_1", "sign_crown" }, //!< 城（ブリン）
    { "BUILDING_3", "sign_dice" }, //!< カジノ
    { "BUILDING_4", "sign_bed" }, //!< 宿屋（ケレボール）
    { "BUILDING_5", "sign_fang" }, //!< モンスター仙人
    { "BUILDING_6", "sign_anvil" }, //!< 武器匠
    { "BUILDING_7", "sign_helm" }, //!< 戦士の集会所
    { "BUILDING_8", "sign_eye" }, //!< 賢者の塔
    { "BUILDING_9", "sign_chapel" }, //!< 寺院
    { "BUILDING_10", "sign_dagger" }, //!< 盗賊のギルド
    { "BUILDING_11", "sign_bow" }, //!< アーチャーのギルド
    { "BUILDING_12", "sign_paladin" }, //!< パラディンのギルド
    { "BUILDING_13", "sign_card" }, //!< トランプ魔術の塔
    { nullptr, nullptr },
};
//! アングウィル（TOWN 4）。並びはモリバントと同じで、名前だけ森の集落ふうに違う。
const TownSign kAngwilSigns[] = {
    { "BUILDING_0", "sign_tome" }, //!< 図書館
    { "BUILDING_1", "sign_crown" }, //!< 荘園
    { "BUILDING_3", "sign_dice" }, //!< カジノ
    { "BUILDING_4", "sign_bed" }, //!< 宿屋
    { "BUILDING_5", "sign_fang" }, //!< モンスター仙人
    { "BUILDING_6", "sign_anvil" }, //!< 武器匠
    { "BUILDING_7", "sign_helm" }, //!< 戦士の集会所
    { "BUILDING_8", "sign_eye" }, //!< 賢者の塔
    { "BUILDING_9", "sign_chapel" }, //!< 寺院
    { "BUILDING_10", "sign_dagger" }, //!< 盗賊のアジト
    { "BUILDING_11", "sign_bow" }, //!< アーチャーの酒場
    { "BUILDING_12", "sign_paladin" }, //!< パラディンの聖所
    { "BUILDING_13", "sign_card" }, //!< トランプ魔術の塔
    { nullptr, nullptr },
};
//! ズル（TOWN 5）。山肌の入口 12 箇所のうち、実データが建物を持つのはこの 5 つ。
const TownSign kZulSigns[] = {
    { "BUILDING_4", "sign_bed" }, //!< 宿屋
    { "BUILDING_8", "sign_swirl" }, //!< 仙術の塔
    { "BUILDING_14", "sign_chaos" }, //!< カオスの塔
    { "BUILDING_15", "sign_leaf" }, //!< 自然魔術の塔
    { "BUILDING_16", "sign_signpost" }, //!< 観光客案内所
    { nullptr, nullptr },
};
/*! @} */

const StyleRow kStyles[] = {
    //! テルモラ（TOWN 2）— 木骨漆喰と藁葺き。壁は生成り寄り、屋根は藁の灼け具合。
    { 2,
        { "_tel",
            {
                { 1.00f, 0.99f, 0.96f }, //!< 塗り替えたばかりの漆喰
                { 0.95f, 0.90f, 0.80f }, //!< 生成り
                { 0.90f, 0.88f, 0.82f }, //!< 灰がかった古い漆喰
                { 1.00f, 0.94f, 0.84f }, //!< 黄土を混ぜた壁
            },
            {
                { 1.00f, 1.00f, 1.00f }, //!< 葺いたばかりの藁
                { 0.88f, 0.85f, 0.80f }, //!< 灰がかって古びた藁
                { 1.08f, 1.02f, 0.88f }, //!< 日に灼けた明るい藁
                { 0.78f, 0.72f, 0.62f }, //!< 苔の乗った濃い藁
            },
            0.46f, 3, //!< **ススキを 1 マスに 3 株まで**（2026-08-09 に決めた「いまの三倍くらいに」）
            true, "", 0, true, kTelmoraLandmarks } }, //!< 地形の草も寄せる。街路樹と街灯は無し
    //! モリバント（TOWN 3）— 切石と漆喰、瓦。**明るさより色相**で振る（都の家並みは
    //! 高さも意匠も揃っているので、明暗だけで振ると同じ建物が並んで見える）。
    { 3,
        { "_mor",
            {
                { 1.00f, 1.00f, 1.00f }, //!< 白い漆喰
                { 0.96f, 0.94f, 0.88f }, //!< 生成りの漆喰
                { 0.90f, 0.91f, 0.96f }, //!< 青みの石灰
                { 1.00f, 0.95f, 0.90f }, //!< 赤みの砂岩
            },
            {
                { 1.00f, 1.00f, 1.00f }, //!< 素焼きの瓦
                { 1.08f, 0.96f, 0.88f }, //!< 焼きの強い赤瓦
                { 0.86f, 0.90f, 1.00f }, //!< 青みがかった瓦
                { 0.80f, 0.76f, 0.72f }, //!< 煤けて古い瓦
            },
            0.00f, 1, //!< **町の中の地面には草はなし**（2026-08-09 に決めた）
            true, "tree_03", 5,
            //! **種別の分からない門には何も立てない**（2026-08-09 に気づいた）。
            false, kMorivantLandmarks } }, //!< 地形の草も敷石へ。街路樹と 5 マスごとの街灯
    /*!
     * ズル（TOWN 5）— **山を刳り抜いた谷**（P10 第 3 期）。
     *
     * 材と色は辺境の地のまま（建物の敷地が 1 件しか無いので、意匠 37 個を作っても
     * 絵にほとんど出ない）。**この行が要るのは 1 つのためだけ**——山肌の入口を
     * 「三方が山の開いたマス」で拾うと、実データの店 12 箇所のほかに**谷の行き止まりの
     * 窪み 11 箇所**まで門になる。そこに石のアーチと無地の板が立つと、モリバントで
     * 撤去すると決めたのと同じ「街じゅうに並ぶ白い板」になる（罠 89）。
     * **表が看板の絵を持っている入口だけ**に門を立てる。
     */
    { 5,
        { "",
            {
                { 1.00f, 0.99f, 0.96f },
                { 0.94f, 0.88f, 0.76f },
                { 0.84f, 0.84f, 0.90f },
                { 0.98f, 0.87f, 0.78f },
            },
            {
                { 1.00f, 1.00f, 1.00f },
                { 0.84f, 0.82f, 0.80f },
                { 1.10f, 1.00f, 0.84f },
                { 0.74f, 0.66f, 0.58f },
            },
            0.22f, 1, //!< 辺境の地と同じ（谷底の草はコアの地図が持っているものをそのまま）
            false, "", 0,
            false, //!< **種別の分からない門には何も立てない**（上の注記）
            nullptr } },
};

//! 辺境の地（TOWN 1）と、意匠をまだ持たない町（アングウィル・ズル）。
const TownStyle kDefaultStyle = {
    "",
    {
        { 1.00f, 0.99f, 0.96f }, //!< 漆喰そのまま
        { 0.94f, 0.88f, 0.76f }, //!< 生成り
        { 0.84f, 0.84f, 0.90f }, //!< 灰漆喰
        { 0.98f, 0.87f, 0.78f }, //!< 赤土を混ぜた壁
    },
    {
        { 1.00f, 1.00f, 1.00f }, //!< 素材そのまま
        { 0.84f, 0.82f, 0.80f }, //!< 灰がかって古びた板
        { 1.10f, 1.00f, 0.84f }, //!< 日に灼けた明るい板
        { 0.74f, 0.66f, 0.58f }, //!< 煤けた濃い板
    },
    0.22f, 1, //!< 辺境の地。**この 2 つは変えない**（実機で見て閉じている）
    false, "", 0, //!< 地形の草はそのまま。街路樹も街灯も無し
    true, //!< 辺境の地。**この値は変えない**（実機で見て閉じている）
    nullptr, //!< 目印の建物は持たない（村長の家を城にしない）
};

/*!
 * @brief **わざと町の読みを壊す手段**（`HD2D_BREAK_TOWN`。検査の検査＝設計書 §14-4）。
 *
 * | 値 | 何を壊すか | `--town-check` のどれが落ちるべきか |
 * |---|---|---|
 * | `sites` | 塊を敷地と認めない（全部を城壁にする） | (1)(3)(5)(6) |
 * | `gate` | 入口を 1 つも立てない | (2) |
 * | `corridor` | 門から奥への筋を空けない | (3) |
 * | `paths` | 道の網を張らない（全部が草になる） | (8) |
 * | `crag` | **階段を抱えた塊を岩山にする**（2026-08-12 に捨てた規則） | (10) |
 *
 * @note `crag` は「モリバントの雑貨屋が岩山になる」不具合そのものを再現する口である。
 * あの直しは規則の**差し替え**なので、検査 (10) が本当にそれを捕まえるかは
 * 壊れた規則を走らせてみないと分からない（罠 112 の形）。
 */
const char *break_town()
{
    static const char *const value = []() -> const char * {
        const char *const env = std::getenv("HD2D_BREAK_TOWN");
        if ((env == nullptr) || (env[0] == '\0')) {
            return nullptr;
        }
        std::fprintf(stderr, "[hd2d] HD2D_BREAK_TOWN=%s: **町の読みをわざと壊します**"
                             "（検査の検査。--town-check は FAIL になるのが正しい）\n",
            env);
        return env;
    }();
    return value;
}

bool break_town_is(const char *what)
{
    const char *const value = break_town();
    return (value != nullptr) && (std::strcmp(value, what) == 0);
}

//! そのマスは永久壁（＝敷地の材料）か。ミニマップの「壁」がこの 2 つの役割に落ちる。
bool is_site_cell(const FloorMeaning &meaning, int gx, int gy)
{
    const CellRole role = meaning.role_at(gx, gy);
    return (role == CellRole::StructuralWall) || (role == CellRole::Bedrock);
}

const int kDx[4] = { 0, 0, -1, 1 };
const int kDy[4] = { -1, 1, 0, 0 };

//! 種から 0..1（`terrain_view.cpp` と同じ splitmix64 の流儀。あちらの `next01` は非公開）。
float roll(std::uint64_t &state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(z >> 40) / static_cast<float>(1u << 24);
}

/*!
 * @brief 使える所の最大矩形を探す（建物の置き場所）。
 *
 * @param avail 幅 `bw` × 高さ `bh` の真偽（敷地であり、門の筋でも前庭でもない）
 * @param seed 同じ広さの候補が複数あるときに選ぶ種
 * @return (x0, y0, x1, y1)。見つからなければ x1 < x0
 *
 * @details 「いちばん広い矩形」だけを採ると、左右対称な敷地でいつも同じ側になる。
 * **広さが 8 割以上の候補を全部集めて種で 1 つ選ぶ**ことで、同じ町の中でも
 * 建物の寄り方がばらける（§10.3「建物を左右どちらかへ寄せて置く」）。
 */
std::array<int, 4> largest_rect(const std::vector<std::uint8_t> &avail, int bw, int bh, std::uint64_t &seed)
{
    std::array<int, 4> none{ 0, 0, -1, -1 };
    if ((bw < kMinHouseSpan) || (bh < kMinHouseSpan)) {
        return none;
    }
    // 行ごとの累積和。「x0..x1 が全部使えるか」を O(1) で見る。
    std::vector<int> prefix(static_cast<std::size_t>(bw + 1) * static_cast<std::size_t>(bh), 0);
    const auto sum_at = [&prefix, bw](int x, int y) {
        return prefix[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw + 1)) + static_cast<std::size_t>(x)];
    };
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            prefix[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw + 1)) + static_cast<std::size_t>(x) + 1]
                = sum_at(x, y)
                + (avail[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bw)) + static_cast<std::size_t>(x)] ? 1 : 0);
        }
    }
    const auto row_ok = [&sum_at](int x0, int x1, int y) { return (sum_at(x1 + 1, y) - sum_at(x0, y)) == (x1 - x0 + 1); };

    //! 2 周する。1 周目でいちばんの広さを知り、2 周目で 8 割以上の候補を集める。
    int best = 0;
    std::vector<std::array<int, 4>> candidates;
    for (int pass = 0; pass < 2; ++pass) {
        const int floor_area = (pass == 0) ? 0 : std::max(kMinHouseSpan * kMinHouseSpan, (best * 4) / 5);
        for (int x0 = 0; x0 < bw; ++x0) {
            for (int x1 = x0 + kMinHouseSpan - 1; x1 < bw; ++x1) {
                int run = 0;
                for (int y = 0; y < bh; ++y) {
                    run = row_ok(x0, x1, y) ? (run + 1) : 0;
                    if (run < kMinHouseSpan) {
                        continue;
                    }
                    //! この (x0,x1) で y を下端とする矩形は、高さ `kMinHouseSpan..run` まで作れる。
                    for (int hgt = kMinHouseSpan; hgt <= run; ++hgt) {
                        const int area = (x1 - x0 + 1) * hgt;
                        if (pass == 0) {
                            best = std::max(best, area);
                        } else if ((area >= floor_area) && (candidates.size() < 256)) {
                            candidates.push_back({ x0, y - hgt + 1, x1, y });
                        }
                    }
                }
            }
        }
        if ((pass == 0) && (best == 0)) {
            return none;
        }
    }
    if (candidates.empty()) {
        return none;
    }
    const auto pick = static_cast<std::size_t>(roll(seed) * static_cast<float>(candidates.size()));
    return candidates[std::min(pick, candidates.size() - 1)];
}

} // namespace

// ------------------------------------------- データから読む町の意匠（§ヘッダの `load_town_styles`）
namespace {

//! データ 1 町ぶん。**文字列と表を自分で持つ**（`TownStyle` は `const char *` で指すだけ）。
struct LoadedTown {
    int town_id{ 0 }; //!< 0 = そのコアの**全町に効く既定**
    TownStyle style{};
    std::vector<TownLandmark> landmarks; //!< 終端（`terrain_key == nullptr`）まで入れる
    std::vector<TownSign> signs; //!< 同じく終端まで
    std::vector<RampartOverride> rampart_overrides; //!< 同じく終端（`terrain_id == 0`）まで
    std::vector<TownLandmark> manor_marks; //!< 屋敷の中の印（デザイン4）。終端まで
    std::vector<EntranceFlank> entrance_flanks; //!< 入口の前の列（デザイン10）。終端まで
    std::vector<TownPlot> plots; //!< マスで名指した塊（デザイン11）。終端まで
    std::vector<TerrainProp> terrain_props; //!< 地形の絵の差し替え（デザイン14）。終端まで
    std::vector<YardStyle> yards; //!< 前庭の小物（OUTPOST_TOWN_DESIGN §5）。終端まで
    std::vector<YardProp> yard_common; //!< 同上。終端まで
    std::vector<SitePlot> sites; //!< 入口の無い敷地の名指し（同）。終端まで
    std::vector<TurfProp> turf_props; //!< 草地の小物（同）。終端まで
    std::vector<std::array<int, 2>> brook_points; //!< 小川の折れ線（同）。終端は持たない
    std::vector<TownZone> zones; //!< マスの矩形を区画として名指す（同 §7.2）。終端まで
    std::vector<TownFeature> features; //!< 区画に属さない 1 点（同 §7.3）。終端まで
};

struct StyleData {
    /*!
     * @brief **参照が安定する容器**でなければならない。
     * @details `std::vector<std::string>` にすると、後から足したときの再配置で
     * 前に返した `c_str()` が宙に浮く（`TownStyle` は文字列を借りているだけ）。
     * `towns` も同じ理由で `deque`——`find()` が要素へのポインタを返す。
     */
    std::deque<std::string> strings;
    std::deque<LoadedTown> towns;
    /*!
     * @brief `YardStyle::props` / `SitePlot::props` が指す先。
     * @details `strings` と同じ理由（**参照が安定する容器**）で `deque` に持つ。
     * 終端（`prefab == nullptr`）はここで積む——呼び出し側は生の列を渡すだけでよい。
     */
    std::deque<std::vector<YardProp>> yard_prop_lists;
    /*!
     * @brief `TownZone::props` / `TownZone::rim_props` が指す先、および
     * `TownZone::features` が指す先。同じ理由で `deque`。
     */
    std::deque<std::vector<ZoneProp>> zone_prop_lists;
    std::deque<std::vector<ZoneFeature>> zone_feature_lists;

    const char *keep(const std::string &text)
    {
        this->strings.push_back(text);
        return this->strings.back().c_str();
    }

    //! `list` の末尾に終端を積んで控え、安定した先頭ポインタを返す（空なら `nullptr`）。
    const YardProp *keep_yard_props(std::vector<YardProp> list)
    {
        if (list.empty()) {
            return nullptr;
        }
        list.push_back(YardProp{}); //!< 終端（`prefab == nullptr`）
        this->yard_prop_lists.push_back(std::move(list));
        return this->yard_prop_lists.back().data();
    }

    //! 同上（`ZoneProp`。終端は `prefab == nullptr`）。
    const ZoneProp *keep_zone_props(std::vector<ZoneProp> list)
    {
        if (list.empty()) {
            return nullptr;
        }
        list.push_back(ZoneProp{});
        this->zone_prop_lists.push_back(std::move(list));
        return this->zone_prop_lists.back().data();
    }

    //! 同上（`ZoneFeature`。終端は `at_x < 0`）。
    const ZoneFeature *keep_zone_features(std::vector<ZoneFeature> list)
    {
        if (list.empty()) {
            return nullptr;
        }
        list.push_back(ZoneFeature{});
        this->zone_feature_lists.push_back(std::move(list));
        return this->zone_feature_lists.back().data();
    }

    const LoadedTown *find(int town_id) const
    {
        const LoadedTown *fallback = nullptr;
        for (const LoadedTown &town : this->towns) {
            if (town.town_id == town_id) {
                return &town;
            }
            if (town.town_id == 0) {
                fallback = &town;
            }
        }
        return fallback;
    }
};

StyleData &style_data()
{
    static StyleData data;
    return data;
}

//! `[[r,g,b], …]` を 4 段まで読む。**足りない段は既定のまま残す**（部分的に振れる）。
void read_tints(const nlohmann::json &node, float (&out)[4][3])
{
    if (!node.is_array()) {
        return;
    }
    for (std::size_t i = 0; (i < node.size()) && (i < 4); ++i) {
        const nlohmann::json &row = node[i];
        if (!row.is_array()) {
            continue;
        }
        for (std::size_t c = 0; (c < row.size()) && (c < 3); ++c) {
            if (row[c].is_number()) {
                out[i][c] = row[c].get<float>();
            }
        }
    }
}

/*!
 * @brief `[ "name_out", { "prefab": …, "flame": …, "glow": [r,g,b], "always": bool,
 *   "center": bool }, … ]` を読んで `YardProp` の列にする（OUTPOST_TOWN_DESIGN §5）。
 * @details 終端は積まない（`StyleData::keep_yard_props` が呼び出し側で積む）。
 */
void read_yard_props(const nlohmann::json &node, StyleData &data, std::vector<YardProp> &out)
{
    if (!node.is_array()) {
        return;
    }
    for (const nlohmann::json &row : node) {
        YardProp prop{};
        if (row.is_string()) {
            prop.prefab = data.keep(row.get<std::string>());
        } else if (row.is_object() && row.contains("prefab") && row["prefab"].is_string()) {
            prop.prefab = data.keep(row["prefab"].get<std::string>());
            if (row.contains("flame") && row["flame"].is_string()) {
                prop.flame = data.keep(row["flame"].get<std::string>());
            }
            if (row.contains("glow") && row["glow"].is_array()) {
                for (std::size_t c = 0; (c < row["glow"].size()) && (c < 3); ++c) {
                    if (row["glow"][c].is_number()) {
                        prop.glow[c] = row["glow"][c].get<float>();
                    }
                }
            }
            prop.always = row.value("always", false);
            prop.center = row.value("center", false);
        } else {
            continue;
        }
        out.push_back(prop);
    }
}

/*!
 * @brief `[ { "prefab": …, "chance": …, "center": bool }, … ]` を読んで `ZoneProp` の列に
 * する。
 * @details 終端は積まない（`StyleData::keep_zone_props` が呼び出し側で積む）。
 */
void read_zone_props(const nlohmann::json &node, StyleData &data, std::vector<ZoneProp> &out)
{
    if (!node.is_array()) {
        return;
    }
    for (const nlohmann::json &row : node) {
        if (!row.is_object() || !row.contains("prefab") || !row["prefab"].is_string()) {
            continue;
        }
        ZoneProp prop{};
        prop.prefab = data.keep(row["prefab"].get<std::string>());
        prop.chance = row.value("chance", 0.f);
        prop.center = row.value("center", false);
        if (row.contains("flame") && row["flame"].is_string()) {
            prop.flame = data.keep(row["flame"].get<std::string>());
        }
        if (row.contains("glow") && row["glow"].is_array() && (row["glow"].size() >= 3)) {
            for (int k = 0; k < 3; ++k) {
                prop.glow[k] = row["glow"][static_cast<std::size_t>(k)].get<float>();
            }
        }
        prop.always = row.value("always", false);
        out.push_back(prop);
    }
}

/*!
 * @brief `[ { "at": [x,y], "prefab": …, "center": bool }, … ]` を読んで `ZoneFeature` の列に
 * する（同 §7.2 `zones` の `features`）。終端は積まない（既定 `center` は真）。
 */
void read_zone_features(const nlohmann::json &node, StyleData &data, std::vector<ZoneFeature> &out)
{
    if (!node.is_array()) {
        return;
    }
    for (const nlohmann::json &row : node) {
        if (!row.is_object() || !row.contains("at") || !row["at"].is_array() || (row["at"].size() < 2)
            || !row.contains("prefab") || !row["prefab"].is_string()) {
            continue;
        }
        ZoneFeature feat{};
        feat.at_x = row["at"][0].get<int>();
        feat.at_y = row["at"][1].get<int>();
        if (feat.at_x < 0) {
            continue; //!< 負は並びの終わりの印なので、データには書けない
        }
        feat.prefab = data.keep(row["prefab"].get<std::string>());
        feat.center = row.value("center", true);
        if (row.contains("flame") && row["flame"].is_string()) {
            feat.flame = data.keep(row["flame"].get<std::string>());
        }
        if (row.contains("glow") && row["glow"].is_array() && (row["glow"].size() >= 3)) {
            for (int k = 0; k < 3; ++k) {
                feat.glow[k] = row["glow"][static_cast<std::size_t>(k)].get<float>();
            }
        }
        feat.always = row.value("always", false);
        out.push_back(feat);
    }
}

} // namespace

bool load_town_styles(const std::string &voxel_dir, const std::string &core_name, std::string *log)
{
    StyleData &data = style_data();
    data.strings.clear();
    data.towns.clear();
    data.yard_prop_lists.clear();
    if (core_name.empty()) {
        return false;
    }
    const std::string sep
        = (voxel_dir.empty() || (voxel_dir.back() == '/') || (voxel_dir.back() == '\\')) ? "" : "/";
    const std::string path = voxel_dir + sep + "town_styles.jsonc";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        //! **無いのは異常ではない。**変愚はべた書きの表だけで従来どおり動く。
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    nlohmann::json root;
    try {
        //! 第 4 引数 = 注釈を無視（`terrain_prefabs.jsonc` と同じ読み方）。
        root = nlohmann::json::parse(buffer.str(), nullptr, true, true);
    } catch (const std::exception &e) {
        if (log != nullptr) {
            *log = std::string("town_styles.jsonc を解釈できませんでした: ") + e.what();
        }
        return false;
    }
    if (!root.contains("cores") || !root["cores"].is_array()) {
        if (log != nullptr) {
            *log = "town_styles.jsonc に cores がありません";
        }
        return false;
    }

    /*
     * **2 度なめる。**`"id": 0` の節は「そのコアの全町に効く既定」なので、先に読んでから
     * 町ごとの節へ重ねる。こうしないと、店の看板 10 行を 14 町ぶん書き写すことになる。
     *
     * 重ね方は「**町の行を先・既定の行を後**」——引く側（`terrain_view`）は
     * **最初に当たった行**を採るので、町ごとの指定が既定に勝つ。
     */
    TownStyle base_style = kDefaultStyle;
    std::vector<TownLandmark> base_landmarks; //!< **終端を入れない**（後ろに繋ぐため）
    std::vector<TownSign> base_signs;
    std::vector<RampartOverride> base_rampart_overrides;
    //! 前庭の小物・入口の無い敷地・草地の小物・小川（OUTPOST_TOWN_DESIGN §5）。同じ理由。
    std::vector<YardStyle> base_yards;
    std::vector<YardProp> base_yard_common;
    std::vector<SitePlot> base_sites;
    std::vector<TurfProp> base_turf_props;
    std::vector<std::array<int, 2>> base_brook_points;
    //! マスの矩形の区画・区画に属さない 1 点（同 §7.2〜7.3）。同じ理由。
    std::vector<TownZone> base_zones;
    std::vector<TownFeature> base_features;
    bool has_base = false;

    int taken = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (const nlohmann::json &core : root["cores"]) {
        if (!core.is_object()) {
            continue;
        }
        /*
         * **別名**（`"aliases": ["tangband"]`）。短愚蛮怒は変愚と同じ町の地図（`D:` 行が 5 町とも
         * 同一）なのに、コア名が `tangband` なので変愚の節を 1 行も読めず、組み込みの古い意匠
         * （`kStyles`）へ落ちていた（2026-09-06 に `--town-check` の「0 節」で気づいた）。
         * 節を写すと 1,000 行が二重になるので、節の側に「この名前でも引く」と書く。
         */
        bool matches = (core.value("core", std::string()) == core_name);
        if (!matches && core.contains("aliases") && core["aliases"].is_array()) {
            for (const nlohmann::json &alias : core["aliases"]) {
                if (alias.is_string() && (alias.get<std::string>() == core_name)) {
                    matches = true;
                    break;
                }
            }
        }
        if (!matches) {
            continue;
        }
        if (!core.contains("towns") || !core["towns"].is_array()) {
            continue;
        }
        for (const nlohmann::json &entry : core["towns"]) {
            if (!entry.is_object()) {
                continue;
            }
            const int entry_id = entry.value("id", 0);
            //! 1 周目は既定（`id: 0`）だけ、2 周目は町ごとだけ。
            if ((entry_id == 0) != (pass == 0)) {
                continue;
            }
            LoadedTown town;
            //! **既定から始めて上書きする。**書いていない節は既定（無ければ辺境の地）の値が残る。
            town.style = has_base ? base_style : kDefaultStyle;
            town.town_id = entry_id;
            if (entry.contains("suffix") && entry["suffix"].is_string()) {
                town.style.suffix = data.keep(entry["suffix"].get<std::string>());
            }
            if (entry.contains("street_tree") && entry["street_tree"].is_string()) {
                town.style.street_tree = data.keep(entry["street_tree"].get<std::string>());
            }
            if (entry.contains("gate") && entry["gate"].is_string()) {
                town.style.gate = data.keep(entry["gate"].get<std::string>());
            }
            if (entry.contains("great_gate") && entry["great_gate"].is_string()) {
                town.style.great_gate = data.keep(entry["great_gate"].get<std::string>());
            }
            if (entry.contains("wall_tints")) {
                read_tints(entry["wall_tints"], town.style.wall_tints);
            }
            if (entry.contains("roof_tints")) {
                read_tints(entry["roof_tints"], town.style.roof_tints);
            }
            town.style.tuft_chance = entry.value("tuft_chance", town.style.tuft_chance);
            town.style.tuft_tries = entry.value("tuft_tries", town.style.tuft_tries);
            town.style.override_grass = entry.value("override_grass", town.style.override_grass);
            town.style.lamp_step = entry.value("lamp_step", town.style.lamp_step);
            //! 街路樹の格子（デザイン §9.6。テルモラの作り直し）と建物の階数の下限。
            //! 書かなければ 0 のまま（従来どおり）。
            town.style.street_tree_step = entry.value("street_tree_step", town.style.street_tree_step);
            town.style.min_storeys = entry.value("min_storeys", town.style.min_storeys);
            if (entry.contains("sign_base") && entry["sign_base"].is_string()) {
                town.style.sign_base = data.keep(entry["sign_base"].get<std::string>());
            }
            town.style.blank_gate_props = entry.value("blank_gate_props", town.style.blank_gate_props);
            //! 岩天井（覆いの町。2026-08-18 その3）。書かなければ覆わない。
            if (entry.contains("cave_roof") && entry["cave_roof"].is_string()) {
                town.style.cave_roof = data.keep(entry["cave_roof"].get<std::string>());
            }
            town.style.cave_roof_height = entry.value("cave_roof_height", town.style.cave_roof_height);
            town.style.cave_min_z = entry.value("cave_min_z", town.style.cave_min_z);
            //! 山塊と交差点の街灯（同 その3）。書かなければ積まない・立てない。
            if (entry.contains("cave_mound") && entry["cave_mound"].is_string()) {
                town.style.cave_mound = data.keep(entry["cave_mound"].get<std::string>());
            }
            if (entry.contains("cross_lamp") && entry["cross_lamp"].is_string()) {
                town.style.cross_lamp = data.keep(entry["cross_lamp"].get<std::string>());
            }
            //! 一直線でない壁の塊を岩山に（同 その3。人里と天狗の里）。
            town.style.crag_blob_walls = entry.value("crag_blob_walls", town.style.crag_blob_walls);
            //! 大門の切れ目の上限と両脇モード（デザイン4・2026-08-18。紅魔館の参道の開口）。
            town.style.great_gate_span = entry.value("great_gate_span", town.style.great_gate_span);
            town.style.great_gate_flank = entry.value("great_gate_flank", town.style.great_gate_flank);
            //! 深水のマスの上の霧の雲（同。紅魔館の霧の湖）。
            town.style.lake_mist_height = entry.value("lake_mist", town.style.lake_mist_height);
            //! 試作由来の目印（shop / mill）を建てない（同。「モリバンドの建物だけを配置」）。
            town.style.no_stock_landmarks
                = entry.value("no_stock_landmarks", town.style.no_stock_landmarks);
            //! 屋敷（デザイン4 第 2 段。紅魔館の本館）。
            town.style.manor_min_cells = entry.value("manor_min_cells", town.style.manor_min_cells);
            //! 切妻の屋敷・参道の大鳥居・道端の石灯籠（デザイン6・2026-08-19。博麗神社）。
            town.style.manor_roof_gable = entry.value("manor_roof_gable", town.style.manor_roof_gable);
            if (entry.contains("torii") && entry["torii"].is_string()) {
                town.style.torii_gate = data.keep(entry["torii"].get<std::string>());
            }
            town.style.path_lanterns = entry.value("path_lanterns", town.style.path_lanterns);
            //! 片流れの屋敷と屋外の中庭（デザイン7・2026-08-19。河童のバザー）。
            town.style.manor_roof_shed = entry.value("manor_roof_shed", town.style.manor_roof_shed);
            town.style.manor_open_court = entry.value("manor_open_court", town.style.manor_open_court);
            //! 町じゅうの配管（デザイン7 その2）。
            town.style.pipe_works = entry.value("pipe_works", town.style.pipe_works);
            //! 入母屋の屋敷・竹林・竹林の霧（デザイン8・2026-08-19。永遠亭）。
            town.style.manor_roof_irimoya
                = entry.value("manor_roof_irimoya", town.style.manor_roof_irimoya);
            town.style.manor_hip_break = entry.value("manor_hip_break", town.style.manor_hip_break);
            town.style.manor_roof_max = entry.value("manor_roof_max", town.style.manor_roof_max);
            if (entry.contains("grove") && entry["grove"].is_string()) {
                town.style.grove = data.keep(entry["grove"].get<std::string>());
            }
            town.style.grove_mist_height = entry.value("grove_mist", town.style.grove_mist_height);
            //! 屋敷を 2 棟＋渡り廊下に割る・桜 1 本・中庭のガラクタ（デザイン9・2026-08-19。香霖堂）。
            town.style.manor_court_split
                = entry.value("manor_court_split", town.style.manor_court_split);
            if (entry.contains("manor_annex") && entry["manor_annex"].is_string()) {
                town.style.manor_annex = data.keep(entry["manor_annex"].get<std::string>());
            }
            if (entry.contains("manor_corridor") && entry["manor_corridor"].is_string()) {
                town.style.manor_corridor = data.keep(entry["manor_corridor"].get<std::string>());
            }
            //! 屋敷の脇に 1 つだけ立つもの（デザイン9 は桜・デザイン10 は鐘楼）。
            if (entry.contains("corner_prop") && entry["corner_prop"].is_string()) {
                town.style.corner_prop = data.keep(entry["corner_prop"].get<std::string>());
            }
            if (entry.contains("court_props") && entry["court_props"].is_string()) {
                town.style.court_props = data.keep(entry["court_props"].get<std::string>());
            }
            //! 走りの上限・1 マス塀の読み替え・入口の前の列（デザイン10・2026-08-19。命蓮寺）。
            town.style.manor_run_max = entry.value("manor_run_max", town.style.manor_run_max);
            //! 屋敷のまわりの伐開（デザイン11・2026-08-20。魔法の森は地図の 60% が木）。
            town.style.manor_clearing = entry.value("manor_clearing", town.style.manor_clearing);
            //! 陸屋根の崩落（デザイン12・2026-08-20。廃洋館の抜けた屋根）。
            if (entry.contains("manor_roof_scar") && entry["manor_roof_scar"].is_string()) {
                town.style.manor_roof_scar = data.keep(entry["manor_roof_scar"].get<std::string>());
            }
            town.style.manor_roof_scar_step
                = entry.value("manor_roof_scar_step", town.style.manor_roof_scar_step);
            //! 1 マス幅の線の塊を敷地にしない（同。塀の断片が前庭に化けるのを止める）。
            town.style.no_line_sites = entry.value("no_line_sites", town.style.no_line_sites);
            town.style.sweep_turf = entry.value("sweep_turf", town.style.sweep_turf);
            if (entry.contains("lone_wall_prop") && entry["lone_wall_prop"].is_string()) {
                town.style.lone_wall_prop = data.keep(entry["lone_wall_prop"].get<std::string>());
            }
            //! 2×2 マスちょうどの塊（デザイン13。守矢神社の御柱）。
            if (entry.contains("quad_wall_prop") && entry["quad_wall_prop"].is_string()) {
                town.style.quad_wall_prop = data.keep(entry["quad_wall_prop"].get<std::string>());
            }
            //! **越屋根**（デザイン15。敷地の建物の真ん中へ 1 つ載せ、天面から煙を立てる）。
            if (entry.contains("roof_vent") && entry["roof_vent"].is_string()) {
                town.style.roof_vent = data.keep(entry["roof_vent"].get<std::string>());
            }
            if (entry.contains("entrance_flanks") && entry["entrance_flanks"].is_array()) {
                for (const nlohmann::json &row : entry["entrance_flanks"]) {
                    if (!row.is_object() || !row.contains("terrain") || !row.contains("prefab")) {
                        continue;
                    }
                    EntranceFlank flank{};
                    flank.terrain_key = data.keep(row["terrain"].get<std::string>());
                    flank.prefab = data.keep(row["prefab"].get<std::string>());
                    flank.reach = row.value("reach", 0);
                    town.entrance_flanks.push_back(flank);
                }
            }
            /*
             * **マスで名指した塊**（デザイン11・2026-08-20。魔法の森の 2 軒）。
             * 材の棟番号はここで割り当てる——データは接尾辞だけ書けばよく、
             * 置く側は棟番号でしか引かない（`TownPlan::wings`）。空いている枠
             * （1 = `manor_annex` / 2 = `manor_corridor`）へ順に入れ、同じ接尾辞は
             * 同じ枠を共有する。**3 つめの接尾辞は主の材のまま**（黙って落ちる）。
             */
            if (entry.contains("plots") && entry["plots"].is_array()) {
                for (const nlohmann::json &row : entry["plots"]) {
                    if (!row.is_object() || !row.contains("at") || !row["at"].is_array()
                        || (row["at"].size() < 2)) {
                        continue;
                    }
                    TownPlot plot{};
                    plot.at_x = row["at"][0].get<int>();
                    plot.at_y = row["at"][1].get<int>();
                    if (plot.at_x < 0) {
                        continue; //!< 負は並びの終わりの印なので、データには書けない
                    }
                    if (row.contains("mark") && row["mark"].is_string()) {
                        plot.mark = data.keep(row["mark"].get<std::string>());
                    }
                    if (row.contains("flank") && row["flank"].is_string()) {
                        plot.flank = data.keep(row["flank"].get<std::string>());
                    }
                    if (row.contains("suffix") && row["suffix"].is_string()) {
                        const std::string suffix = row["suffix"].get<std::string>();
                        const char **const slots[2]
                            = { &town.style.manor_annex, &town.style.manor_corridor };
                        for (std::uint8_t i = 0; i < 2; ++i) {
                            const char *&slot = *slots[i];
                            const bool empty = (slot == nullptr) || (slot[0] == '\0');
                            if (!empty && (suffix != slot)) {
                                continue;
                            }
                            if (empty) {
                                slot = data.keep(suffix);
                            }
                            plot.wing = static_cast<std::uint8_t>(i + 1);
                            break;
                        }
                    }
                    town.plots.push_back(plot);
                }
            }
            /*
             * **地形の絵の差し替え**（デザイン14・2026-08-20。彼岸の奈落）。
             * 共通の対応表に行の無い地形（`DARK_PIT`）を、その町でだけ絵にする口。
             * 行を共通の表へ足すと変愚のクエスト階まで動くので、ここが正しい置き場所である。
             */
            if (entry.contains("terrain_props") && entry["terrain_props"].is_array()) {
                for (const nlohmann::json &row : entry["terrain_props"]) {
                    if (!row.is_object() || (!row.contains("terrain") && !row.contains("terrain_id"))) {
                        continue;
                    }
                    TerrainProp prop{};
                    if (row["terrain"].is_string()) {
                        prop.terrain_key = data.keep(row["terrain"].get<std::string>());
                    }
                    //! 共通の表が知らない地形は**数で名指す**（`rampart_overrides` と同じ）。
                    prop.terrain_id = row.value("terrain_id", 0);
                    if (row.contains("ground") && row["ground"].is_string()) {
                        prop.ground = data.keep(row["ground"].get<std::string>());
                    }
                    if (row.contains("prop") && row["prop"].is_string()) {
                        prop.prop = data.keep(row["prop"].get<std::string>());
                    }
                    if (row.contains("wall") && row["wall"].is_string()) {
                        prop.wall = data.keep(row["wall"].get<std::string>());
                    }
                    //! **煙の口の高さ**（デザイン15。書かなければ 0 ＝立てない）。
                    prop.smoke = row.value("smoke", 0.f);
                    town.terrain_props.push_back(prop);
                }
            }
            if (entry.contains("manor_marks") && entry["manor_marks"].is_array()) {
                for (const nlohmann::json &row : entry["manor_marks"]) {
                    if (!row.is_object() || !row.contains("terrain") || !row.contains("kind")) {
                        continue;
                    }
                    TownLandmark mark{};
                    mark.terrain_key = data.keep(row["terrain"].get<std::string>());
                    mark.kind = data.keep(row["kind"].get<std::string>());
                    town.manor_marks.push_back(mark);
                }
            }
            //! 塀のマスの絵の差し替え（同。氷壁→氷塊。コア固有の id はこの表が持つ）。
            if (entry.contains("rampart_overrides") && entry["rampart_overrides"].is_array()) {
                for (const nlohmann::json &row : entry["rampart_overrides"]) {
                    if (!row.is_object() || !row.contains("terrain_id") || !row.contains("prefab")) {
                        continue;
                    }
                    RampartOverride over{};
                    over.terrain_id = row["terrain_id"].get<int>();
                    over.prefab = data.keep(row["prefab"].get<std::string>());
                    if (over.terrain_id != 0) {
                        town.rampart_overrides.push_back(over);
                    }
                }
            }

            /*
             * **前庭の小物を建物の意味で引く**（辺境の地の
             * 作り替え）。`yards` は門の地形 key で引く行の列、`yard_common` は
             * 行に当たらない敷地と `props` を使い切った後の乱択の元。
             */
            if (entry.contains("yards") && entry["yards"].is_array()) {
                for (const nlohmann::json &row : entry["yards"]) {
                    if (!row.is_object() || !row.contains("terrain") || !row["terrain"].is_string()) {
                        continue;
                    }
                    YardStyle yard{};
                    yard.terrain_key = data.keep(row["terrain"].get<std::string>());
                    std::vector<YardProp> props;
                    if (row.contains("props")) {
                        read_yard_props(row["props"], data, props);
                    }
                    yard.props = data.keep_yard_props(std::move(props));
                    if (row.contains("wall_tint") && row["wall_tint"].is_array()) {
                        for (std::size_t c = 0; (c < row["wall_tint"].size()) && (c < 3); ++c) {
                            if (row["wall_tint"][c].is_number()) {
                                yard.wall_tint[c] = row["wall_tint"][c].get<float>();
                            }
                        }
                    }
                    if (row.contains("roof_tint") && row["roof_tint"].is_array()) {
                        for (std::size_t c = 0; (c < row["roof_tint"].size()) && (c < 3); ++c) {
                            if (row["roof_tint"][c].is_number()) {
                                yard.roof_tint[c] = row["roof_tint"][c].get<float>();
                            }
                        }
                    }
                    yard.storeys = row.value("storeys", 0);
                    yard.fill = row.value("fill", 0.5f);
                    town.yards.push_back(yard);
                }
            }
            if (entry.contains("yard_common") && entry["yard_common"].is_array()) {
                read_yard_props(entry["yard_common"], data, town.yard_common);
            }
            /*
             * **入口の無い敷地をマスで名指す**（同 §5 `sites`。納屋・厩）。
             */
            if (entry.contains("sites") && entry["sites"].is_array()) {
                for (const nlohmann::json &row : entry["sites"]) {
                    if (!row.is_object() || !row.contains("at") || !row["at"].is_array()
                        || (row["at"].size() < 2)) {
                        continue;
                    }
                    SitePlot site{};
                    site.at_x = row["at"][0].get<int>();
                    site.at_y = row["at"][1].get<int>();
                    if (site.at_x < 0) {
                        continue; //!< 負は並びの終わりの印なので、データには書けない
                    }
                    if (row.contains("wing") && row["wing"].is_string()) {
                        site.wing = data.keep(row["wing"].get<std::string>());
                    }
                    std::vector<YardProp> props;
                    if (row.contains("props")) {
                        read_yard_props(row["props"], data, props);
                    }
                    site.props = data.keep_yard_props(std::move(props));
                    if (row.contains("landmark") && row["landmark"].is_string()) {
                        site.landmark = data.keep(row["landmark"].get<std::string>());
                    }
                    town.sites.push_back(site);
                }
            }
            //! 草地（`Turf`）の小物（同 §5 `turf_props`）。
            if (entry.contains("turf_props") && entry["turf_props"].is_array()) {
                for (const nlohmann::json &row : entry["turf_props"]) {
                    if (!row.is_object() || !row.contains("prefab") || !row["prefab"].is_string()) {
                        continue;
                    }
                    TurfProp prop{};
                    prop.prefab = data.keep(row["prefab"].get<std::string>());
                    prop.chance = row.value("chance", 0.f);
                    town.turf_props.push_back(prop);
                }
            }
            //! 道の縁の草・門の火（同 §5 `path_edge` / `gate_flame`）。
            if (entry.contains("path_edge") && entry["path_edge"].is_string()) {
                town.style.path_edge = data.keep(entry["path_edge"].get<std::string>());
            }
            if (entry.contains("gate_flame") && entry["gate_flame"].is_string()) {
                town.style.gate_flame = data.keep(entry["gate_flame"].get<std::string>());
            }
            //! 小川の折れ線（同 §5 `brook`。軸に沿う線分だけ——置く側が読むときに確かめる）。
            if (entry.contains("brook") && entry["brook"].is_array()) {
                for (const nlohmann::json &pt : entry["brook"]) {
                    if (!pt.is_array() || (pt.size() < 2) || !pt[0].is_number_integer()
                        || !pt[1].is_number_integer()) {
                        continue;
                    }
                    town.brook_points.push_back({ pt[0].get<int>(), pt[1].get<int>() });
                }
            }

            /*
             * **マスの矩形を区画として名指す**（同 §7.2 `zones`。辺境の地の作り替え
             * 第 2 段）。書かない町は `entry.contains("zones")` が偽のままなので、
             * この節そのものが 1 度も回らない。
             */
            if (entry.contains("zones") && entry["zones"].is_array()) {
                for (const nlohmann::json &row : entry["zones"]) {
                    if (!row.is_object() || !row.contains("rect") || !row["rect"].is_array()
                        || (row["rect"].size() < 4)) {
                        continue;
                    }
                    TownZone zone{};
                    zone.x0 = row["rect"][0].get<int>();
                    zone.y0 = row["rect"][1].get<int>();
                    zone.x1 = row["rect"][2].get<int>();
                    zone.y1 = row["rect"][3].get<int>();
                    if (zone.x1 < zone.x0) {
                        continue; //!< 並びの終わりの印（`x1 < x0`）と衝突しないよう保険
                    }
                    const std::string apply = row.value("apply_to", std::string("turf"));
                    zone.apply_to = (apply == "path") ? 1 : ((apply == "all") ? 2 : 0);
                    if (row.contains("ground") && row["ground"].is_string()) {
                        zone.ground = data.keep(row["ground"].get<std::string>());
                    }
                    if (row.contains("rim") && row["rim"].is_string()) {
                        zone.rim = data.keep(row["rim"].get<std::string>());
                    }
                    if (row.contains("kind") && row["kind"].is_string()) {
                        zone.kind = data.keep(row["kind"].get<std::string>());
                    }
                    std::vector<ZoneProp> props;
                    if (row.contains("props")) {
                        read_zone_props(row["props"], data, props);
                    }
                    zone.props = data.keep_zone_props(std::move(props));
                    std::vector<ZoneProp> rim_props;
                    if (row.contains("rim_props")) {
                        read_zone_props(row["rim_props"], data, rim_props);
                    }
                    zone.rim_props = data.keep_zone_props(std::move(rim_props));
                    std::vector<ZoneFeature> features;
                    if (row.contains("features")) {
                        read_zone_features(row["features"], data, features);
                    }
                    zone.features = data.keep_zone_features(std::move(features));
                    town.zones.push_back(zone);
                }
            }
            //! 区画に属さない 1 点（同 §7.3 `features`。門楼・大木・さらし台など）。
            if (entry.contains("features")) {
                std::vector<ZoneFeature> features;
                read_zone_features(entry["features"], data, features);
                for (const ZoneFeature &f : features) {
                    TownFeature tf{};
                    tf.at_x = f.at_x;
                    tf.at_y = f.at_y;
                    tf.prefab = f.prefab;
                    tf.flame = f.flame;
                    tf.glow[0] = f.glow[0];
                    tf.glow[1] = f.glow[1];
                    tf.glow[2] = f.glow[2];
                    tf.always = f.always;
                    town.features.push_back(tf);
                }
            }

            //! 自分の行を先に積み、既定の行を後ろへ繋ぐ（**終端はいちばん最後に 1 つだけ**）。
            if (entry.contains("landmarks") && entry["landmarks"].is_array()) {
                for (const nlohmann::json &row : entry["landmarks"]) {
                    if (!row.is_object() || !row.contains("terrain") || !row.contains("kind")) {
                        continue;
                    }
                    TownLandmark mark{};
                    mark.terrain_key = data.keep(row["terrain"].get<std::string>());
                    mark.kind = data.keep(row["kind"].get<std::string>());
                    town.landmarks.push_back(mark);
                }
            }
            if (entry.contains("signs") && entry["signs"].is_array()) {
                for (const nlohmann::json &row : entry["signs"]) {
                    if (!row.is_object() || !row.contains("terrain") || !row.contains("sign")) {
                        continue;
                    }
                    TownSign sign{};
                    sign.terrain_key = data.keep(row["terrain"].get<std::string>());
                    sign.sign = data.keep(row["sign"].get<std::string>());
                    if (row.contains("gate") && row["gate"].is_string()) {
                        sign.gate = data.keep(row["gate"].get<std::string>());
                    }
                    town.signs.push_back(sign);
                }
            }

            if (entry_id == 0) {
                /*
                 * 既定の節。控えておいて、2 周目の町ごとの節へ重ねる。
                 * **この節自身も `data.towns` へ積む**——`find()` が
                 * 「町ごとの節が無い町」の落とし先として `town_id == 0` を返すからである。
                 * **自分自身を後ろへ繋いではいけない**（同じ行が 2 度並ぶ）。
                 */
                base_style = town.style;
                base_landmarks = town.landmarks;
                base_signs = town.signs;
                base_rampart_overrides = town.rampart_overrides;
                base_yards = town.yards;
                base_yard_common = town.yard_common;
                base_sites = town.sites;
                base_turf_props = town.turf_props;
                base_brook_points = town.brook_points;
                base_zones = town.zones;
                base_features = town.features;
                has_base = true;
            } else {
                //! 既定の行を後ろへ繋ぐ（自分の行が先なので、同じ地形なら自分が勝つ）。
                town.landmarks.insert(town.landmarks.end(), base_landmarks.begin(), base_landmarks.end());
                town.signs.insert(town.signs.end(), base_signs.begin(), base_signs.end());
                town.rampart_overrides.insert(town.rampart_overrides.end(),
                    base_rampart_overrides.begin(), base_rampart_overrides.end());
                //! `yards` / `sites` / `turf_props` は自分の行が先（同じ terrain key/座標は自分が勝つ）。
                town.yards.insert(town.yards.end(), base_yards.begin(), base_yards.end());
                if (town.yard_common.empty()) {
                    town.yard_common = base_yard_common; //!< 乱択の元は町ごとに丸ごと差し替え
                }
                town.sites.insert(town.sites.end(), base_sites.begin(), base_sites.end());
                town.turf_props.insert(town.turf_props.end(), base_turf_props.begin(), base_turf_props.end());
                if (town.brook_points.empty()) {
                    town.brook_points = base_brook_points; //!< 折れ線は町ごとに丸ごと差し替え
                }
                //! `zones` / `features` も自分の行が先（同 §7.2〜7.3）。
                town.zones.insert(town.zones.end(), base_zones.begin(), base_zones.end());
                town.features.insert(town.features.end(), base_features.begin(), base_features.end());
            }
            if (!town.landmarks.empty()) {
                town.landmarks.push_back(TownLandmark{ nullptr, nullptr }); //!< 終端
            }
            if (!town.signs.empty()) {
                town.signs.push_back(TownSign{ nullptr, nullptr, nullptr }); //!< 終端
            }
            if (!town.rampart_overrides.empty()) {
                town.rampart_overrides.push_back(RampartOverride{ 0, nullptr }); //!< 終端
            }
            if (!town.manor_marks.empty()) {
                town.manor_marks.push_back(TownLandmark{ nullptr, nullptr }); //!< 終端
            }
            if (!town.entrance_flanks.empty()) {
                town.entrance_flanks.push_back(EntranceFlank{ nullptr, nullptr, 0 }); //!< 終端
            }
            if (!town.plots.empty()) {
                town.plots.push_back(TownPlot{ -1, 0, 0, nullptr, nullptr }); //!< 終端
            }
            if (!town.terrain_props.empty()) {
                town.terrain_props.push_back(
                    TerrainProp{ nullptr, 0, nullptr, nullptr, nullptr, 0.f }); //!< 終端
            }
            if (!town.yards.empty()) {
                town.yards.push_back(YardStyle{}); //!< 終端（`terrain_key == nullptr`）
            }
            if (!town.yard_common.empty()) {
                town.yard_common.push_back(YardProp{}); //!< 終端（`prefab == nullptr`）
            }
            if (!town.sites.empty()) {
                town.sites.push_back(SitePlot{ -1, 0, nullptr, nullptr, nullptr }); //!< 終端
            }
            if (!town.turf_props.empty()) {
                town.turf_props.push_back(TurfProp{ nullptr, 0.f }); //!< 終端
            }
            if (!town.zones.empty()) {
                town.zones.push_back(TownZone{}); //!< 終端（`x1 < x0`）
            }
            if (!town.features.empty()) {
                town.features.push_back(TownFeature{}); //!< 終端（`at_x < 0`）
            }

            data.towns.push_back(std::move(town));
            /*
             * **入れた後で指し直す。**`landmarks` は `LoadedTown` が持っているので、
             * 積む前の一時物の `data()` を控えると宙に浮く（`deque` は要素を動かさないので
             * 積んだ後なら安定する）。
             */
            LoadedTown &stored = data.towns.back();
            stored.style.landmarks = stored.landmarks.empty() ? nullptr : stored.landmarks.data();
            stored.style.rampart_overrides
                = stored.rampart_overrides.empty() ? nullptr : stored.rampart_overrides.data();
            stored.style.manor_marks = stored.manor_marks.empty() ? nullptr : stored.manor_marks.data();
            stored.style.entrance_flanks
                = stored.entrance_flanks.empty() ? nullptr : stored.entrance_flanks.data();
            stored.style.plots = stored.plots.empty() ? nullptr : stored.plots.data();
            stored.style.terrain_props
                = stored.terrain_props.empty() ? nullptr : stored.terrain_props.data();
            stored.style.yards = stored.yards.empty() ? nullptr : stored.yards.data();
            stored.style.yard_common = stored.yard_common.empty() ? nullptr : stored.yard_common.data();
            stored.style.sites = stored.sites.empty() ? nullptr : stored.sites.data();
            stored.style.turf_props = stored.turf_props.empty() ? nullptr : stored.turf_props.data();
            stored.style.brook_points = stored.brook_points.empty() ? nullptr : stored.brook_points.data();
            stored.style.brook_point_count = static_cast<int>(stored.brook_points.size());
            stored.style.zones = stored.zones.empty() ? nullptr : stored.zones.data();
            stored.style.features = stored.features.empty() ? nullptr : stored.features.data();
            ++taken;
        }
        }
    }
    if (log != nullptr) {
        std::ostringstream note;
        note << "town_styles.jsonc: core=" << core_name << " で " << taken
             << " 節を採りました（既定の節を含む）";
        *log = note.str();
    }
    return taken > 0;
}

const TownStyle &town_style_for(int town_id)
{
    //! **データが先。**その町の行が無ければ従来のべた書きへ落ちる（変愚は無改変で動く）。
    const LoadedTown *const found = style_data().find(town_id);
    if (found != nullptr) {
        return found->style;
    }
    for (const StyleRow &row : kStyles) {
        if (row.town_id == town_id) {
            return row.style;
        }
    }
    return kDefaultStyle;
}

const TownSign *town_signs_for(int town_id)
{
    /*
     * データにある町は**データの表だけ**を見る。べた書きへ落とすと、
     * 幻想蛮怒の `BUILDING_0`（酒場）にテルモラの本の看板が掛かる——直したかったのはそこである。
     * 表を書いていない（`signs` 節が無い）なら `nullptr` ＝無地の板。
     */
    const LoadedTown *const found = style_data().find(town_id);
    if (found != nullptr) {
        return found->signs.empty() ? nullptr : found->signs.data();
    }
    switch (town_id) {
    case 1:
        return kOutpostSigns;
    case 2:
        return kTelmoraSigns;
    case 3:
        return kMorivantSigns;
    case 4:
        return kAngwilSigns;
    case 5:
        return kZulSigns;
    default:
        //! 知らない町。**無地の板へ落とす**（間違った絵を掛けるより黙っているほうがよい）。
        return nullptr;
    }
}

TownRole TownPlan::role_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return TownRole::None;
    }
    return static_cast<TownRole>(
        this->roles[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(gx)]);
}

std::int16_t TownPlan::site_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return -1;
    }
    return this->site_of[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(gx)];
}

HouseSlice TownPlan::slice_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return HouseSlice::Middle;
    }
    return static_cast<HouseSlice>(
        this->slices[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(gx)]);
}

std::uint8_t TownPlan::wing_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)
        || this->wings.empty()) {
        return 0;
    }
    return this->wings[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width))
        + static_cast<std::size_t>(gx)];
}

std::uint8_t TownPlan::fence_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return 0;
    }
    return this->fences[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(gx)];
}

bool TownPlan::clear_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return false;
    }
    return this->clearing[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width))
               + static_cast<std::size_t>(gx)]
        != 0u;
}

bool rebuild_town_plan(const FloorMeaning &meaning, TownPlan &out, CragMemory *crags,
    const std::vector<std::uint8_t> *entrances)
{
    out = TownPlan{};
    if (!meaning.valid()) {
        return false;
    }
    /*
     * **町の中でだけ読む。**`town_id` はコアの `AngbandWorld::get_town_index()` で、
     * 荒野・地下では 0 になる（`src/floor/wild.cpp` が格子ごとに入れ直す）。
     * これを見ないと、荒野の山の塊を敷地と読んで野原に城壁が建つ。
     */
    if ((meaning.identity.kind != static_cast<int>(FloorKind::Surface)) || (meaning.identity.town_id <= 0)) {
        return false;
    }

    //! 町ごとの意匠（色の表をここから引く。素材の接尾辞は置く側が使う）。
    const TownStyle &style = town_style_for(meaning.identity.town_id);
    const int w = meaning.width;
    const int h = meaning.height;
    const auto count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    out.width = w;
    out.height = h;
    out.identity = meaning.identity;
    out.roles.assign(count, static_cast<std::uint8_t>(TownRole::None));
    out.site_of.assign(count, static_cast<std::int16_t>(-1));
    out.slices.assign(count, static_cast<std::uint8_t>(HouseSlice::Middle));
    //! 屋敷の棟（デザイン9）。**割らない町では最後まで 0 のまま**＝従来の 1 棟の読み。
    out.wings.assign(count, 0);
    out.fences.assign(count, 0);
    out.clearing.assign(count, 0);

    const auto at = [w](int x, int y) {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x);
    };

    /* ---- (1) 永久壁の連結成分（4 近傍）。§10.3 の「敷地」の材料 ---- */
    std::vector<std::int32_t> label(count, -1);
    std::vector<std::vector<std::pair<int, int>>> components;
    std::vector<std::pair<int, int>> stack;
    for (int gy = 0; gy < h; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            if ((label[at(gx, gy)] >= 0) || !is_site_cell(meaning, gx, gy)) {
                continue;
            }
            const auto id = static_cast<std::int32_t>(components.size());
            components.emplace_back();
            stack.clear();
            stack.emplace_back(gx, gy);
            label[at(gx, gy)] = id;
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                components[static_cast<std::size_t>(id)].emplace_back(cx, cy);
                for (int i = 0; i < 4; ++i) {
                    const int nx = cx + kDx[i];
                    const int ny = cy + kDy[i];
                    if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                        continue;
                    }
                    if ((label[at(nx, ny)] >= 0) || !is_site_cell(meaning, nx, ny)) {
                        continue;
                    }
                    label[at(nx, ny)] = id;
                    stack.emplace_back(nx, ny);
                }
            }
        }
    }

    /* ---- (1b) 参道の大鳥居（デザイン6・2026-08-19。博麗神社）----
     *
     * 参道の入口の両脇の**小さな壁の塊の東西の対**（間に壁が無い）を鳥居の脚と読む。
     * 博麗の実測は 2×2 マス × 2（間は床 7 マス）。これまでは敷地の条件（4 マス・充填 1.0）を
     * 満たして**ミニ民家が 2 軒**建っていた。脚のマスの役割は Rampart のまま（素材の無い
     * 環境では塀の欠片に落ちる保険）で、絵は置く側が半身の型に差し替える。
     */
    std::vector<char> torii_leg(components.size(), 0);
    if ((style.torii_gate != nullptr) && (style.torii_gate[0] != '\0')) {
        struct Pillar {
            std::size_t id;
            int x0, y0, x1, y1;
        };
        std::vector<Pillar> pillars;
        for (std::size_t id = 0; id < components.size(); ++id) {
            const auto &cells = components[id];
            if ((cells.size() < 2u) || (cells.size() > 6u)) {
                continue;
            }
            int x0 = w;
            int y0 = h;
            int x1 = -1;
            int y1 = -1;
            bool border = false;
            for (const auto &[cx, cy] : cells) {
                x0 = std::min(x0, cx);
                y0 = std::min(y0, cy);
                x1 = std::max(x1, cx);
                y1 = std::max(y1, cy);
                border = border || (cx == 0) || (cy == 0) || (cx == (w - 1)) || (cy == (h - 1));
            }
            if (border || ((x1 - x0) > 1) || ((y1 - y0) > 1)) {
                continue;
            }
            pillars.push_back({ id, x0, y0, x1, y1 });
        }
        for (std::size_t i = 0; i < pillars.size(); ++i) {
            for (std::size_t j = 0; j < pillars.size(); ++j) {
                const Pillar &a = pillars[i];
                const Pillar &b = pillars[j];
                if (torii_leg[a.id] || torii_leg[b.id] || (a.y0 != b.y0) || (a.y1 != b.y1)) {
                    continue;
                }
                const int gap = b.x0 - a.x1 - 1;
                if ((gap < 3) || (gap > 14)) {
                    continue;
                }
                //! 間に壁があれば対ではない（塀の親柱の並びを誤読しないため）。
                bool blocked = false;
                for (int cy = a.y0; (cy <= a.y1) && !blocked; ++cy) {
                    for (int cx = a.x1 + 1; cx < b.x0; ++cx) {
                        if (label[at(cx, cy)] >= 0) {
                            blocked = true;
                            break;
                        }
                    }
                }
                if (blocked) {
                    continue;
                }
                torii_leg[a.id] = 1;
                torii_leg[b.id] = 1;
                for (const std::size_t leg : { a.id, b.id }) {
                    for (const auto &[cx, cy] : components[leg]) {
                        out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::Rampart);
                        ++out.rampart_cells;
                    }
                }
                out.torii_gates.push_back({ a.x0, a.y0, a.x1, a.y1, b.x0, b.y0, b.x1, b.y1 });
                std::fprintf(stderr, "[hd2d] 大鳥居: 西脚 (%d,%d) 東脚 (%d,%d) 間 %d マス\n",
                    a.x0, a.y0, b.x0, b.y0, gap);
            }
        }
    }

    /* ---- (1b2) 御柱（デザイン13・2026-08-20。守矢神社）----
     *
     * **4 マスちょうどで外接矩形が 2×2 の塊**を柱と読む。守矢神社の地図はこれを
     * ちょうど 16 個持っていて（4 組 × 4 本）、放っておくと敷地の条件（4 マス以上・
     * 充填 1.0）を満たして**ミニ民家が 16 軒**建つ——博麗神社の大鳥居の脚が
     * 民家 2 軒になっていたのと同じ形である。
     *
     * 役割は `Rampart` のまま（素材の無い環境では塀の欠片に落ちる保険）。絵は
     * **北西のマスに 1 枚**だけ立てる（型が 2×2 マスぶんの柱を持つ＝大鳥居の脚と同じ分担）。
     */
    std::vector<char> quad_leg(components.size(), 0);
    if ((style.quad_wall_prop != nullptr) && (style.quad_wall_prop[0] != '\0')) {
        for (std::size_t id = 0; id < components.size(); ++id) {
            const auto &cells = components[id];
            if (cells.size() != 4u) {
                continue;
            }
            int x0 = w;
            int y0 = h;
            int x1 = -1;
            int y1 = -1;
            bool border = false;
            for (const auto &[cx, cy] : cells) {
                x0 = std::min(x0, cx);
                y0 = std::min(y0, cy);
                x1 = std::max(x1, cx);
                y1 = std::max(y1, cy);
                border = border || (cx == 0) || (cy == 0) || (cx == (w - 1)) || (cy == (h - 1));
            }
            if (border || ((x1 - x0) != 1) || ((y1 - y0) != 1)) {
                continue; //!< L 字や 1×4 の並びは柱ではない
            }
            quad_leg[id] = 1;
            for (const auto &[cx, cy] : cells) {
                out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::Rampart);
                ++out.rampart_cells;
            }
            out.quad_walls.push_back({ x0, y0 });
        }
        if (!out.quad_walls.empty()) {
            std::fprintf(stderr, "[hd2d] (1b2) 2×2 の柱: %zu 本（最初は (%d,%d)）\n",
                out.quad_walls.size(), out.quad_walls.front()[0], out.quad_walls.front()[1]);
        }
    }

    /* ---- (2) 成分を仕分ける（§10.3）---- */
    for (std::size_t id = 0; id < components.size(); ++id) {
        if (torii_leg[id]) {
            continue; // 鳥居の脚。役割は (1b) で塗ってある
        }
        if (quad_leg[id]) {
            continue; // 御柱。役割は (1b2) で塗ってある
        }
        const auto &cells = components[id];
        int x0 = w;
        int y0 = h;
        int x1 = -1;
        int y1 = -1;
        bool touches_border = false;
        for (const auto &[cx, cy] : cells) {
            x0 = std::min(x0, cx);
            y0 = std::min(y0, cy);
            x1 = std::max(x1, cx);
            y1 = std::max(y1, cy);
            touches_border = touches_border || (cx == 0) || (cy == 0) || (cx == (w - 1)) || (cy == (h - 1));
        }
        /*
         * **山でできた塊は建物ではない**（2026-08-11 に気づいた:「辺境マップ内の
         * イークの洞窟のオブジェクトが**家になっている**ので岩山と入り口に修正を」）。
         *
         * 辺境の地の地図には森の中に `^`（MOUNTAIN）の塊が 25 マスあり、その南の縁に
         * イークの洞窟へ降りる `>` がある（`01_Outpost_Full.txt` の y=31 x=150）。
         * 塊は 8×4・充填率 0.78 で**敷地の条件をそのまま満たす**ので、家が 1 軒建って
         * 洞窟の口を塞いでいた。
         *
         * ## 「階段を抱えているか」では見分けられなかった（2026-08-12 の直し）
         * 最初の直しは**階段に接した壁の塊を山**とした（町の店の敷地に階段は無い、という
         * 読み）。ところが**町の階段はダンジョンの口だけではない**——クエストの入口
         * （`QUEST_ENTER`）も `STAIRS` を持つので、クエストを受けている間だけ、その入口に
         * 接した敷地が丸ごと岩山に落ちた（2026-08-12 に気づいた:「モリバントの雑貨屋が
         * 岩山になっている」）。実データを 6 町 × クエスト状態で総当たりすると 14 箇所あり、
         * モリバントの雑貨屋・ブラックマーケット・錬金術師・魔法の店と、
         * **辺境の地とテルモラの城壁そのもの**（203 マス・990 マス）まで岩山になっていた。
         *
         * ## 見分けるのは「山でできているか」
         * 誤って岩山になった 14 箇所は**全件 `PERMANENT` 100%** で、イークの洞窟の露岩は
         * **`MOUNTAIN` 100%** である。並びの形では見分けられない（露岩は充填率 0.78、
         * 雑貨屋の敷地は 0.93 で、どちらも「壁の塊に 1 マスの穴」）ので、
         * **山かどうかをミニマップに運んでもらう**（`MinimapKind::Mountain`）。
         *
         * これは §9.4「ui は地形テーブルを知らない」を破らない——ここが見るのは
         * 意味づけ層の印（`FloorMeaning::mountains`）で、地形 id でも表でもない。
         */
        int mountain = 0;
        for (const auto &[cx, cy] : cells) {
            mountain += meaning.mountain_at(cx, cy) ? 1 : 0;
        }
        const int bw = x1 - x0 + 1;
        const int bh = y1 - y0 + 1;
        const float fill = static_cast<float>(cells.size()) / static_cast<float>(bw * bh);
        /*
         * `HD2D_BREAK_TOWN=crag` は**捨てた規則**（階段を抱えた塊を岩山にする）で読む。
         * 検査 (10) がこの不具合を捕まえられるかを見るための手段である。
         */
        bool hugs_stairs = false;
        if (break_town_is("crag")) {
            for (const auto &[cx, cy] : cells) {
                for (int i = 0; i < 4; ++i) {
                    hugs_stairs = hugs_stairs
                        || (meaning.role_at(cx + kDx[i], cy + kDy[i]) == CellRole::Stairs);
                }
            }
        }
        /*
         * **山が主なら岩山**（大きさは問わない。25 マスの露岩もある）。「1 マスでも山なら」に
         * すると、町の壁が山肌と地続きになっている所（ズルの `PERMANENT` 524 マス）で
         * 町ごと山に飲まれる。半分を境にすれば、ズルの山塊（山 11,117 / 石 524）は山、
         * 石の城壁に山が数マスくっついた形は城壁のままになる。
         */
        /*
         * **山でできた塊**（デザイン7・2026-08-19 に切り出した）。屋敷の読みは大きさで
         * 立つので、山の多い町（河童のバザーは山 1,853 マスが 1 つの成分）で
         * `manor_min_cells` を小さく取ると**山塊が屋敷になる**。屋敷は「山でないこと」で
         * 弾き、`kCragCells`（大きすぎる塊）の側では弾かない——紅魔館の本館は 2,476 マスで
         * その閾を超えており、そこで弾くと**館が岩山に戻る**（デザイン4 の逆行）。
         */
        const bool mountain_mass = (mountain * 2 >= static_cast<int>(cells.size()));
        const bool crag_mass = hugs_stairs || mountain_mass
            || (static_cast<int>(cells.size()) >= kCragCells);
        /*
         * **屋敷のマス数に達した塊は敷地にしない**（デザイン6・2026-08-19）。
         * 博麗神社の社殿は 277 マス 25×12・充填 0.92 で敷地の条件に収まり、**巨大な寄棟の
         * 民家**が建っていた。表が manor_min_cells を書いた町では屋敷の読みを先に立てる。
         * 紅魔館（下限 2000）の敷地は最大でも 26×26 = 676 マスなので、この変更で動く塊は無い。
         */
        const bool manor_sized = (style.manor_min_cells > 0)
            && (static_cast<int>(cells.size()) >= style.manor_min_cells);
        /*
         * **マスで名指した塊**（デザイン11・2026-08-20。魔法の森の 2 軒）。名指したマス
         * （戸口の彫り込みそのもの）が外接矩形の中にあり、その**四方のどれかがこの塊**
         * なら、マス数に関わらず屋敷として読む。
         *
         * 地形 key で引けないのは、そのマスの地形が**クエストの状態で入れ替わる**ため
         * （受注中だけ `QUEST_ENTER`・それ以外は `GRASS`）。町の地図は動かないので
         * 座標のほうが安定している。**既知が少なくて塊が欠けている段は当たらず**、
         * 従来どおりの家へ落ちる（危い側へ倒れない）。
         */
        int plot_index = -1;
        if (style.plots != nullptr) {
            for (int i = 0; style.plots[i].at_x >= 0; ++i) {
                const int px = style.plots[i].at_x;
                const int py = style.plots[i].at_y;
                if ((px < x0) || (px > x1) || (py < y0) || (py > y1)) {
                    continue;
                }
                bool touches = false;
                for (int k = 0; k < 4; ++k) {
                    const int nx = px + kDx[k];
                    const int ny = py + kDy[k];
                    touches = touches
                        || ((nx >= 0) && (ny >= 0) && (nx < w) && (ny < h)
                            && (label[at(nx, ny)] == static_cast<int>(id)));
                }
                if (touches) {
                    plot_index = i;
                    break;
                }
            }
        }
        /*
         * **1 マス幅の線は敷地ではない**（デザイン12・2026-08-20。廃洋館の南塀）。
         * 敷地の条件は線にも当たるので、切れ目で割れた 1×26 の塀が丸ごと前庭に
         * 化けていた（実測）。表が旗を立てた町でだけ弾く（既定は従来どおり）。
         */
        const bool line_wall = style.no_line_sites && ((bw <= 1) || (bh <= 1));
        const bool site = (plot_index < 0) && !touches_border && !crag_mass && !manor_sized
            && !line_wall && (bw <= kMaxSiteSpan)
            && (bh <= kMaxSiteSpan)
            && (static_cast<int>(cells.size()) >= kMinSiteCells) && (fill >= kSiteFill)
            && !break_town_is("sites");
        /*
         * **一直線でない壁の塊は岩山**（2026-08-18 その3。`TownStyle::crag_blob_walls` の註記）。
         * 内部マス（4 方向すべてが同じ成分）の数で「線か塊か」を見る——1〜2 マス幅の塀は
         * まっすぐでも L 字でも囲いの輪でも内部マスを持たない。敷地（`site`）を先に
         * 見るので、条件を満たす店の塊はこれまでどおり家が建つ。
         */
        bool blob_wall = false;
        if (style.crag_blob_walls && !site && !crag_mass) {
            int interior = 0;
            for (const auto &[cx, cy] : cells) {
                if ((cx <= 0) || (cy <= 0) || (cx >= (w - 1)) || (cy >= (h - 1))) {
                    continue;
                }
                const bool inside = (label[at(cx - 1, cy)] == static_cast<int>(id))
                    && (label[at(cx + 1, cy)] == static_cast<int>(id))
                    && (label[at(cx, cy - 1)] == static_cast<int>(id))
                    && (label[at(cx, cy + 1)] == static_cast<int>(id));
                interior += inside ? 1 : 0;
                if (interior >= 2) {
                    blob_wall = true;
                    break;
                }
            }
        }

        /*
         * **山**（P10 第 3 期。2026-08-10 に決めた）。塀にも枠にもせず、丸ごと岩山にする。
         *
         * ズルは `MOUNTAIN` が `PERMANENT` ＋ `WALL` を持つので、山が町の壁と地続きになって
         * 地図の 86% が 1 つの成分になる。従来はそれが「塀」に落ちて**山脈が丸太の塀**に
         * なっていた。**枠のマスも山にする**——地図の縁で山が切れると、そこだけ岩盤の箱が
         * 並んで「山の外」が見えてしまう。山は画面の外まで続いているのが正しい。
         *
         * **巨大な塊も山とみなす**（`kCragCells`）。山でできていることが分かれば大きさは
         * 要らないが、既知が少なくて `Mountain` の印がまだ 1 マスも来ていない段でも
         * ズルが丸太の塀に見えないよう、こちらは残してある。
         */
        /*
         * **屋敷**（デザイン4・2026-08-18。紅魔館の本館）。表がマス数の下限を書いた町でだけ、
         * 大きな壁の塊を岩山より先に屋敷として読む（紅魔館は 2,476 マスで kCragCells=2400 を
         * 超えており、この分岐が無いと岩山に飲まれる）。スライスは**マスごとの外向き**で
         * 決める——矩形でない輪郭（南面の彫り込み）にも壁と屋根が回る。
         */
        const bool manor = !site && !touches_border && (manor_sized || (plot_index >= 0))
            && !mountain_mass;
        if (manor) {
            x0 = w;
            y0 = h;
            x1 = -1;
            y1 = -1;
            for (const auto &[cx, cy] : cells) {
                x0 = std::min(x0, cx);
                y0 = std::min(y0, cy);
                x1 = std::max(x1, cx);
                y1 = std::max(y1, cy);
                out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::Manor);
                ++out.manor_cells;
            }
            for (const auto &[cx, cy] : cells) {
                const auto mine = [&](int nx, int ny) {
                    return (nx >= 0) && (ny >= 0) && (nx < w) && (ny < h)
                        && (label[at(nx, ny)] == static_cast<int>(id));
                };
                const bool n_out = !mine(cx, cy - 1);
                const bool s_out = !mine(cx, cy + 1);
                const bool w_out = !mine(cx - 1, cy);
                const bool e_out = !mine(cx + 1, cy);
                //! 9 スライスへ丸める（対向が同時に外の細い棟は、この地図には無い——
                //! 出たら n/w を勝たせる。輪郭の絵が半マスずれるだけで壊れはしない）。
                int row = 1;
                int col = 1;
                if (n_out) {
                    row = 0;
                } else if (s_out) {
                    row = 2;
                }
                if (w_out) {
                    col = 0;
                } else if (e_out) {
                    col = 2;
                }
                out.slices[at(cx, cy)] = static_cast<std::uint8_t>((row * 3) + col);
            }
            /*
             * **南から食い込む切れ込みで棟を割る**（デザイン9・2026-08-19。香霖堂）。
             *
             * 香霖堂の壁の塊は 241 マス 34×10 の 1 枚岩で、そのまま読むと横長の 1 棟に
             * なる。§4.1 の外せない 1 点は「入母屋の店舗＋白い土蔵が渡り廊下で
             * つながった 2 棟のシルエット」なので、**地図の形が既に持っている割れ目**
             * を使って 3 つに分ける——列ごとに南の縁から北へ空きを数え、深く食い込んだ
             * 列（実測 x112..114 が 4 マス。この奥が闇市と香霖堂の入口が向かい合う前庭）
             * が棟の境である。座標は 1 つも持たない。
             *
             * 端の列は屋敷の隅が欠けているだけなので、**両側に屋敷の列が残る切れ込み**
             * だけを採る。走りが複数あれば**いちばん広いもの**（棟の境は 1 つ）。
             */
            /*
             * **名指した塊の材と戸口**（デザイン11）。棟番号は塊まるごとに 1 つ
             * ——割れ目で分ける口（`manor_court_split`）とは別物で、**別々の塊に
             * 別々の材**を当てるためのものである。
             */
            if (plot_index >= 0) {
                const TownPlot &plot = style.plots[plot_index];
                if (plot.wing > 0) {
                    for (const auto &[cx, cy] : cells) {
                        out.wings[at(cx, cy)] = plot.wing;
                        ++out.wing_cells[plot.wing];
                    }
                    /*
                     * **戸口のマスにも棟番号を入れる。**そのマスは歩けるマスなので屋敷では
                     * ないが、切妻は彫り込みの頭上にも蓋を葺く（デザイン6）ので、
                     * 番号が無いと**そこだけ主の材**になる（白い洋館に赤錆の蓋が 1 枚）。
                     */
                    out.wings[at(plot.at_x, plot.at_y)] = plot.wing;
                }
                //! 戸口が**外へ開いている向き**（塊でない側。彫り込みは 1 マスなので四方で決まる）。
                int dx = 0;
                int dy = 0;
                for (int k = 0; k < 4; ++k) {
                    const int nx = plot.at_x + kDx[k];
                    const int ny = plot.at_y + kDy[k];
                    const bool mine = (nx >= 0) && (ny >= 0) && (nx < w) && (ny < h)
                        && (label[at(nx, ny)] == static_cast<int>(id));
                    if (!mine) {
                        dx = kDx[k];
                        dy = kDy[k];
                    }
                }
                if ((plot.mark != nullptr) && (plot.mark[0] != 0)) {
                    out.plot_props.push_back({ plot.at_x, plot.at_y, plot_index, 0 });
                }
                if ((plot.flank != nullptr) && (plot.flank[0] != 0)) {
                    //! 脇は**戸口の外の 1 マス**の左右（開いた向きに直交する側）。
                    for (int s = -1; s <= 1; s += 2) {
                        const int px = plot.at_x + dx + (dy * s);
                        const int py = plot.at_y + dy + (dx * s);
                        if ((px <= 0) || (py <= 0) || (px >= (w - 1)) || (py >= (h - 1))) {
                            continue;
                        }
                        if (label[at(px, py)] >= 0) {
                            continue; //!< 壁のマスには置かない
                        }
                        out.plot_props.push_back({ px, py, plot_index, 1 });
                    }
                }
                //! 戸口の前は**見通し**にする（森の天蓋に沈む。デザイン9 の穴）。
                for (int d = 0; d <= 3; ++d) {
                    for (int s = -1; s <= 1; ++s) {
                        const int cx = plot.at_x + (dx * d) + (dy * s);
                        const int cy = plot.at_y + (dy * d) + (dx * s);
                        if ((cx >= 0) && (cy >= 0) && (cx < w) && (cy < h)) {
                            out.clearing[at(cx, cy)] = 1u;
                        }
                    }
                }
                std::fprintf(stderr,
                    "[hd2d] (1e) 名指しの塊: (%d,%d) %d マス x%d..%d y%d..%d 棟 %d\n",
                    plot.at_x, plot.at_y, static_cast<int>(cells.size()), x0, x1, y0, y1,
                    static_cast<int>(plot.wing));
            }
            /*
             * **屋敷のまわりを伐り開く**（デザイン11。§4.1 ⑦「建物の周りを 1〜2 マス」）。
             * 印は「背の高いものを立てないマス」なので、**コアの地図は 1 マスも動かない**。
             */
            if (style.manor_clearing > 0) {
                const int reach = style.manor_clearing;
                for (const auto &[cx, cy] : cells) {
                    for (int oy = -reach; oy <= reach; ++oy) {
                        for (int ox = -reach; ox <= reach; ++ox) {
                            const int nx = cx + ox;
                            const int ny = cy + oy;
                            if ((nx >= 0) && (ny >= 0) && (nx < w) && (ny < h)) {
                                out.clearing[at(nx, ny)] = 1u;
                            }
                        }
                    }
                }
            }
            if (style.manor_court_split > 0) {
                const int span_w = x1 - x0 + 1;
                std::vector<int> gap(static_cast<std::size_t>(span_w), 0);
                std::vector<int> mass(static_cast<std::size_t>(span_w), 0);
                for (const auto &[cx, cy] : cells) {
                    ++mass[static_cast<std::size_t>(cx - x0)];
                }
                for (int gx = x0; gx <= x1; ++gx) {
                    int d = 0;
                    while (((y1 - d) >= y0) && (label[at(gx, y1 - d)] != static_cast<int>(id))) {
                        ++d;
                    }
                    gap[static_cast<std::size_t>(gx - x0)] = d;
                }
                int best_lo = -1;
                int best_hi = -1;
                for (int i = 0; i < span_w; ++i) {
                    if (gap[static_cast<std::size_t>(i)] < style.manor_court_split) {
                        continue;
                    }
                    int j = i;
                    while (((j + 1) < span_w) && (gap[static_cast<std::size_t>(j + 1)] >= style.manor_court_split)) {
                        ++j;
                    }
                    //! **両側に棟が残ること**（端の欠けは境ではない）。
                    bool west = false;
                    bool east = false;
                    for (int k = 0; k < i; ++k) {
                        west = west || (mass[static_cast<std::size_t>(k)] > 0);
                    }
                    for (int k = j + 1; k < span_w; ++k) {
                        east = east || (mass[static_cast<std::size_t>(k)] > 0);
                    }
                    if (west && east && ((j - i) > (best_hi - best_lo))) {
                        best_lo = i;
                        best_hi = j;
                    }
                    i = j;
                }
                if (best_lo >= 0) {
                    for (const auto &[cx, cy] : cells) {
                        const int i = cx - x0;
                        const std::uint8_t wing = (i < best_lo)
                            ? std::uint8_t{ 1 } //!< 割れ目の西＝別棟（香霖堂では白い土蔵）
                            : ((i > best_hi) ? std::uint8_t{ 0 } : std::uint8_t{ 2 });
                        out.wings[at(cx, cy)] = wing;
                        ++out.wing_cells[wing];
                    }
                    std::fprintf(stderr, "[hd2d] (1c) 屋敷を割った: x%d..%d が渡り廊下（主 %d / 別棟 %d / 廊下 %d マス）\n",
                        x0 + best_lo, x0 + best_hi, out.wing_cells[0], out.wing_cells[1],
                        out.wing_cells[2]);
                }
            }
            /*
             * **南西の外角に 1 本だけ木を立てる**（デザイン9。香霖堂の桜）。街路樹は
             * 道沿いに次々に植わるので「1 本」にならない。南面の入口を隠さない向き
             * （カメラは南から見下ろす）へ、屋敷の外 2〜4 マスを探して最初に開いたマスを採る。
             */
            if ((style.corner_prop != nullptr) && (style.corner_prop[0] != '\0')) {
                for (int d = 2; (d <= 4) && out.lone_trees.empty(); ++d) {
                    for (int e = 0; (e <= 3) && out.lone_trees.empty(); ++e) {
                        const int tx = x0 - d;
                        const int ty = y1 - e;
                        if ((tx <= 0) || (ty <= 0) || (tx >= (w - 1)) || (ty >= (h - 1))) {
                            continue;
                        }
                        if (label[at(tx, ty)] < 0) {
                            out.lone_trees.push_back({ tx, ty });
                            /*
                             * **まわりを見通しにする。**森の木は 3 マスを超え、葉は
                             * 3〜4 マスへ張り出すので、ただ 1 本立てても**天蓋の下に沈んで
                             * 1 画素も見えない**（合成フレームで実測。桜は 2.4 マス）。
                             * 門と同じ見通しの印を置くと、そのマスを覆う木が落ちて
                             * **林間の小さな空き地に桜が 1 本**という絵になる。
                             */
                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dx = -1; dx <= 1; ++dx) {
                                    const int cx = tx + dx;
                                    const int cy = ty + dy;
                                    if ((cx >= 0) && (cy >= 0) && (cx < w) && (cy < h)) {
                                        out.clearing[at(cx, cy)] = 1u;
                                    }
                                }
                            }
                            //! 1 つしか立たないので、どこに立ったかは記録に残す。
                            std::fprintf(stderr, "[hd2d] (1d) 角の 1 つ: (%d,%d)\n", tx, ty);
                        }
                    }
                }
            }
            out.manors.push_back({ x0, y0, x1, y1 });
            continue;
        }

        if (!site && (crag_mass || blob_wall)) {
            for (const auto &[cx, cy] : cells) {
                out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::Crag);
                ++out.crag_cells;
            }
            continue;
        }

        /*
         * **1 マスだけの壁の塊**（デザイン10・2026-08-19。命蓮寺の千体地蔵）。
         * 意匠が茎名を書いた町でだけ控える——役割は下の枝で `Rampart` になるので、
         * **素材の無い環境では従来どおり塀の欠片**が立つ。線の塀（何十マスも続く）とは
         * 連結成分の大きさで分かれるので、道が近いかどうかを見る必要が無い。
         */
        if (!site && (cells.size() == 1) && (style.lone_wall_prop != nullptr)
            && (style.lone_wall_prop[0] != '\0')) {
            out.lone_walls.push_back({ cells.front().first, cells.front().second });
        }
        if (!site) {
            /*
             * **「世界の枠」は地図のいちばん外のマスだけ**にする。成分ごと枠にしていた版は、
             * テルモラの城壁を丸ごと飲み込んでいた——あの町の壁は西の端（x=0）まで続いていて
             * 地図の枠と地続きなので、**町の塀が 1 マスも無い**ことになり、塀の意匠が
             * どこにも出なかった（枠 991 マス・塀 235 マスで、その 235 は東の山脈だった）。
             *
             * 辺境の地・モリバント・アングウィルの枠はちょうど外周の 524 マスなので、
             * この直しでは 1 マスも動かない（＝実機で閉じた絵は変わらない）。
             */
            std::vector<std::pair<int, int>> wall_cells;
            for (const auto &[cx, cy] : cells) {
                const bool on_frame = (cx == 0) || (cy == 0) || (cx == (w - 1)) || (cy == (h - 1));
                out.roles[at(cx, cy)] = static_cast<std::uint8_t>(on_frame ? TownRole::Border : TownRole::Rampart);
                (on_frame ? out.border_cells : out.rampart_cells) += 1;
                if (!on_frame) {
                    wall_cells.emplace_back(cx, cy);
                }
            }
            //! やぐらの位置は**塀になったマスだけ**から決める（枠まで含めると地図の四隅に立つ）。
            if (static_cast<int>(wall_cells.size()) >= kMinTowerCells) {
                x0 = w;
                y0 = h;
                x1 = -1;
                y1 = -1;
                for (const auto &[cx, cy] : wall_cells) {
                    x0 = std::min(x0, cx);
                    y0 = std::min(y0, cy);
                    x1 = std::max(x1, cx);
                    y1 = std::max(y1, cy);
                }
            }
            const auto &cells_for_tower = wall_cells;
            if (static_cast<int>(wall_cells.size()) >= kMinTowerCells) {
                /*
                 * やぐらは**外接矩形の四隅にいちばん近いマス**へ 1 つずつ。
                 * 「出隅なら櫓」にしていた版は、厚みのある塀の縁が軒並み出隅と判定されて
                 * **塀じゅうにやぐらが並んだ**（実機の絵で気づいた）。
                 *
                 * **小さな塊には立てない。**モリバントの中央には噴水があり、その石の柱
                 * （2〜3 マスの連結成分）が塀と読まれて、**噴水に円塔が林立した**
                 * （実機の絵で気づいた。辺境の地では木のやぐらだったので目立たなかった）。
                 */
                for (const auto &[qx, qy] : { std::pair<int, int>{ x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } }) {
                    int best = -1;
                    long long best_d = 0;
                    for (std::size_t k = 0; k < cells_for_tower.size(); ++k) {
                        const long long dx = cells_for_tower[k].first - qx;
                        const long long dy = cells_for_tower[k].second - qy;
                        const long long d = (dx * dx) + (dy * dy);
                        if ((best < 0) || (d < best_d)) {
                            best = static_cast<int>(k);
                            best_d = d;
                        }
                    }
                    if (best >= 0) {
                        const auto &[cx, cy] = cells_for_tower[static_cast<std::size_t>(best)];
                        out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::Tower);
                    }
                }
            }
            continue;
        }

        TownSite entry;
        entry.x0 = x0;
        entry.y0 = y0;
        entry.x1 = x1;
        entry.y1 = y1;
        entry.cells = static_cast<int>(cells.size());
        //! 敷地の種は**外接矩形の左上**から引く（`cell_seed` と同じ規則。§9.3）。
        entry.seed = cell_seed(x0, y0, meaning.identity);
        const auto site_index = static_cast<std::int16_t>(out.sites.size());
        for (const auto &[cx, cy] : cells) {
            out.site_of[at(cx, cy)] = site_index;
        }

        /*
         * ---- (3) 入口 ----
         *
         * 素朴には「3 方を敷地に囲まれた開いたマス」で足りる。店や施設の入口は壁に
         * 開けた 1 マスの穴だからである。**ただし城のような広い入口では足りない**
         * （テルモラの城は南面に 4 マスの門があり、端の 2 マスは 2 方・中の 2 マスは 1 方しか
         * 囲まれていない）。データの入口を 21/25 しか門にできず、城の正面に何も立たなかった。
         *
         * そこで**2 段で読む**。
         *   1 周目: 3 方を囲まれたマス → 門（従来どおり）
         *   2 周目: **壁の切れ目** — 外接矩形の中で、行（または列）に沿って
         *           「敷地 … 開いたマスが `kMaxGateSpan` 以下 … 敷地」と並び、かつ
         *           その開いたマスが**すべて直交方向にも敷地に接している**所
         *
         * 2 周目が中庭を拾わないのは長さで切っているからである（テルモラの城の中庭は
         * 11 マス幅なので落ちる）。直交方向の条件は「壁に開けた穴」であることの担保で、
         * これが無いと敷地の腕と腕の間の窪みを門にしてしまう。
         *
         * くぐる向きは**敷地の重心から遠ざかる側**を採る。3 方を囲まれたマスでは開いた向きが
         * 1 つしか無いので従来と同じ答えになり、広い入口でだけ意味を持つ（4 マスの門の端のマスは
         * 「街路の側」と「隣の門のマス」の 2 つが開いているので、選ばないと横を向く）。
         */
        double cog_x = 0.0;
        double cog_y = 0.0;
        for (const auto &[cx, cy] : cells) {
            cog_x += cx;
            cog_y += cy;
        }
        cog_x /= static_cast<double>(cells.size());
        cog_y /= static_cast<double>(cells.size());
        //! そのマスは「敷地の外接矩形の中にある、敷地でないマス」か。
        const auto in_box_open = [&](int gx, int gy) {
            return (gx >= x0) && (gx <= x1) && (gy >= y0) && (gy <= y1)
                && (label[at(gx, gy)] != static_cast<std::int32_t>(id));
        };
        //! くぐる向き（重心から遠ざかる、敷地でない隣）。無ければ -1。
        const auto exit_dir = [&](int gx, int gy) {
            int best = -1;
            double best_score = 0.0;
            for (int i = 0; i < 4; ++i) {
                const int nx = gx + kDx[i];
                const int ny = gy + kDy[i];
                if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                    continue;
                }
                if (label[at(nx, ny)] == static_cast<std::int32_t>(id)) {
                    continue; // 敷地。そちらへはくぐれない
                }
                const double score = (kDx[i] * (gx - cog_x)) + (kDy[i] * (gy - cog_y));
                if ((best < 0) || (score > best_score)) {
                    best = i;
                    best_score = score;
                }
            }
            return best;
        };

        std::vector<std::pair<int, int>> gate_front;
        for (int gy = y0; gy <= y1; ++gy) {
            for (int gx = x0; gx <= x1; ++gx) {
                if (!in_box_open(gx, gy)) {
                    continue; // 敷地そのもの
                }
                int walls = 0;
                for (int i = 0; i < 4; ++i) {
                    const int nx = gx + kDx[i];
                    const int ny = gy + kDy[i];
                    walls += ((nx >= 0) && (ny >= 0) && (nx < w) && (ny < h)
                                 && (label[at(nx, ny)] == static_cast<std::int32_t>(id)))
                        ? 1
                        : 0;
                }
                if ((walls < 3) || break_town_is("gate")) {
                    continue; // 3 方を囲まれていない＝1 周目では拾わない（中庭は 4 方）
                }
                gate_front.emplace_back(gx, gy);
            }
        }
        /*
         * 2 周目。**壁の切れ目**（行・列に沿った、敷地に挟まれた短い開き）を門にする。
         * 行方向と列方向を同じ手で見る（`axis` が 0 なら行を横に、1 なら列を縦に走る）。
         */
        if (!break_town_is("gate")) {
            for (int axis = 0; axis < 2; ++axis) {
                const int outer0 = (axis == 0) ? y0 : x0;
                const int outer1 = (axis == 0) ? y1 : x1;
                const int inner0 = (axis == 0) ? x0 : y0;
                const int inner1 = (axis == 0) ? x1 : y1;
                const auto cell = [axis](int outer, int inner) {
                    return (axis == 0) ? std::pair<int, int>{ inner, outer } : std::pair<int, int>{ outer, inner };
                };
                const auto is_mine = [&](int gx, int gy) {
                    return (gx >= 0) && (gy >= 0) && (gx < w) && (gy < h)
                        && (label[at(gx, gy)] == static_cast<std::int32_t>(id));
                };
                for (int outer = outer0; outer <= outer1; ++outer) {
                    int run = 0;
                    for (int inner = inner0; inner <= inner1 + 1; ++inner) {
                        const auto [cx, cy] = cell(outer, std::min(inner, inner1));
                        const bool mine = (inner <= inner1) && is_mine(cx, cy);
                        if (!mine && (inner <= inner1)) {
                            ++run;
                            continue;
                        }
                        //! 敷地に当たった。**直前の開きが敷地で挟まれていて短ければ**門である。
                        const int start = inner - run;
                        /*
                         * 拾うのは**2 マス以上**の開きだけ（1 マスは 1 周目が見ている）。さらに
                         * 「片側だけが敷地で、反対側は全部開いている」ことを求める＝**壁の面に
                         * 開けた穴**である。この条件が無いと、敷地の腕と腕の間の窪みや、
                         * 厚い壁を貫くトンネルまで門になって、辺境の地の門が 14 → 19 に増えた
                         * （実機で見て閉じた絵なので、増やしてはならない）。
                         */
                        const bool closed = (run >= 2) && (start > inner0);
                        if (closed && (run <= kMaxGateSpan)) {
                            for (int side = -1; side <= 1; side += 2) {
                                bool wall_face = true;
                                for (int k = 0; (k < run) && wall_face; ++k) {
                                    const auto [gx, gy] = cell(outer, start + k);
                                    const bool back = (axis == 0) ? is_mine(gx, gy + side) : is_mine(gx + side, gy);
                                    const bool front = (axis == 0) ? is_mine(gx, gy - side) : is_mine(gx - side, gy);
                                    wall_face = back && !front;
                                }
                                if (wall_face) {
                                    for (int k = 0; k < run; ++k) {
                                        gate_front.push_back(cell(outer, start + k));
                                    }
                                    break;
                                }
                            }
                        }
                        run = 0;
                    }
                }
            }
        }
        for (std::size_t head = 0; head < gate_front.size(); ++head) {
            const auto [gx, gy] = gate_front[head];
            if (static_cast<TownRole>(out.roles[at(gx, gy)]) == TownRole::Gate) {
                continue; // 同じマスを 2 通りで拾うことがある（1 マスの入口は両方に当たる）
            }
            const int open_dir = exit_dir(gx, gy);
            if (open_dir < 0) {
                continue; // 4 方とも敷地＝中庭。門ではない
            }
            out.roles[at(gx, gy)] = static_cast<std::uint8_t>(TownRole::Gate);
            out.site_of[at(gx, gy)] = site_index;
            /*
             * 門の姿勢をマスごとに覚えておく（`slices` を借りる）。1 = 東西にくぐる。
             * 扉の 2 姿勢（P10 レビュー 11）と同じ流儀で、意匠は 1 つ・姿勢だけ 2 つ。
             * **敷地には門が 2 つ以上あることがある**（Outpost_Lite）ので、
             * 代表の門の向きで代用してはならない。
             */
            out.slices[at(gx, gy)] = (kDx[open_dir] != 0) ? 1u : 0u;
            ++out.gate_cells;
            if (!entry.has_gate) {
                entry.has_gate = true;
                entry.gate_x = gx;
                entry.gate_y = gy;
                entry.gate_dx = kDx[open_dir];
                entry.gate_dy = kDy[open_dir];
            }
        }

        /* ---- (4) 前庭と建物（§10.3。**門からの筋は空ける**）---- */
        const int bw2 = bw;
        const int bh2 = bh;
        std::vector<std::uint8_t> avail(static_cast<std::size_t>(bw2) * static_cast<std::size_t>(bh2), 0);
        for (const auto &[cx, cy] : cells) {
            avail[(static_cast<std::size_t>(cy - y0) * static_cast<std::size_t>(bw2)) + static_cast<std::size_t>(cx - x0)] = 1;
        }
        const auto block = [&avail, bw2, bh2, x0, y0](int gx, int gy) {
            const int lx = gx - x0;
            const int ly = gy - y0;
            if ((lx < 0) || (ly < 0) || (lx >= bw2) || (ly >= bh2)) {
                return;
            }
            avail[(static_cast<std::size_t>(ly) * static_cast<std::size_t>(bw2)) + static_cast<std::size_t>(lx)] = 0;
        };
        if (entry.has_gate) {
            const int ix = -entry.gate_dx;
            const int iy = -entry.gate_dy;
            /*
             * 門からまっすぐ奥へ抜ける 1 マス幅の筋を空ける。**これが §10.3-3 の要点**で、
             * 建物が視線上に無ければ遮蔽の式が適用されない（＝門は建物に隠れない）。
             *
             * ただし**要るのは敷地が門の南に広がるときだけ**である。カメラは常に南から
             * 見下ろすので（`Camera::eye()`）、敷地が門の北にあれば建物は門の向こう側になり、
             * 何も隠さない。全部の敷地で筋を空けていた版は、敷地の 2/3 が前庭になって
             * **町が「空き地に柵を巡らせた区画」に見えた**（実機で撮って気づいた）。
             * 東西に開く門も、視線がほぼ南北なので建物は横に並ぶだけで隠さない。
             */
            if ((iy > 0) && !break_town_is("corridor")) {
                for (int step = 0; step <= std::max(bw2, bh2); ++step) {
                    block(entry.gate_x + (ix * step), entry.gate_y + (iy * step));
                }
            }
            /*
             * 門の周りの前庭。奥行きがあれば 2 マス、無ければ 1 マス（§10.3「入口の南 1〜2」）。
             * 奥行き＝門から奥へ何マス敷地が続くか。
             */
            int depth = 0;
            while (depth < std::max(bw2, bh2)) {
                const int nx = entry.gate_x + (ix * (depth + 1));
                const int ny = entry.gate_y + (iy * (depth + 1));
                if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h) || (label[at(nx, ny)] != static_cast<std::int32_t>(id))) {
                    break;
                }
                ++depth;
            }
            const int apron = ((depth >= 5) && (iy > 0)) ? 2 : 1;
            for (int dy = -apron; dy <= apron; ++dy) {
                for (int dx = -apron; dx <= apron; ++dx) {
                    block(entry.gate_x + dx, entry.gate_y + dy);
                }
            }
        }
        const std::array<int, 4> rect = largest_rect(avail, bw2, bh2, entry.seed);
        if (rect[2] >= rect[0]) {
            entry.house_x0 = x0 + rect[0];
            entry.house_y0 = y0 + rect[1];
            entry.house_x1 = x0 + rect[2];
            entry.house_y1 = y0 + rect[3];
            const int area = (entry.house_x1 - entry.house_x0 + 1) * (entry.house_y1 - entry.house_y0 + 1);
            entry.storeys = ((area >= 6) && (roll(entry.seed) < kTwoStoreyChance)) ? 2 : 1;
        } else {
            ++out.stall_sites; // 建物の入らない小さな区画＝露店（§10.4）
        }
        //! 敷地の色（形は 1 つ・色だけ振る）。**建物の矩形を決めた後**に引く（順序が種を決める）。
        //! 色の表は**町ごと**（藁と瓦では振れる方向が違う）。
        const auto wall_pick = static_cast<std::size_t>(roll(entry.seed) * 4.f);
        const auto roof_pick = static_cast<std::size_t>(roll(entry.seed) * 4.f);
        for (int k = 0; k < 3; ++k) {
            entry.wall_tint[k] = style.wall_tints[std::min<std::size_t>(wall_pick, 3)][k];
            entry.roof_tint[k] = style.roof_tints[std::min<std::size_t>(roof_pick, 3)][k];
        }

        for (const auto &[cx, cy] : cells) {
            const bool in_house = entry.has_house() && (cx >= entry.house_x0) && (cx <= entry.house_x1)
                && (cy >= entry.house_y0) && (cy <= entry.house_y1);
            if (in_house) {
                const int col = (cx == entry.house_x0) ? 0 : ((cx == entry.house_x1) ? 2 : 1);
                const int row = (cy == entry.house_y0) ? 0 : ((cy == entry.house_y1) ? 2 : 1);
                out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::House);
                out.slices[at(cx, cy)] = static_cast<std::uint8_t>((row * 3) + col);
                ++out.house_cells;
            } else {
                out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::Yard);
                ++out.yard_cells;
            }
        }
        out.sites.push_back(entry);
    }

    /*
     * ---- (2a) **岩山の記憶**（`town_plan.h` の `CragMemory`。2026-08-11 に気づいた:
     * 「イークの洞穴で洞窟から地上に戻った際に岩山が修正前の家になった」）----
     *
     * 敷地か岩山かは既知マスから読むので、**同じ町の同じ場所が、そのとき何を知っているかで
     * 別のものに読まれる**。町の地図は動かないので「町 N のこのマスは岩山」は一度分かれば
     * 永久に正しい——**岩が家になることは無い。**
     *
     * ここでやるのは 2 つ。
     * 1. 覚えているマスを岩山へ戻す（敷地の枠に取られていたら剥がす）
     * 2. いま岩山と読めたマスを覚える
     *
     * 覚えるのは**岩山だけ**。敷地・塀・道は既知が増えるほど正しくなるので、
     * 後から読み直したほうがよい。岩山だけが逆で、**既知が少ないほど家に化けやすい**
     * （小さくて充填率の高い塊は敷地の条件をそのまま満たす）。
     */
    if (crags != nullptr) {
        if (!crags->covers(meaning.identity.town_id, w, h)) {
            crags->town_id = meaning.identity.town_id;
            crags->width = w;
            crags->height = h;
            crags->cells.assign(count, 0u);
        }
        int restored = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (crags->cells[i] == 0u) {
                continue;
            }
            if (static_cast<TownRole>(out.roles[i]) == TownRole::Crag) {
                continue;
            }
            /*
             * 敷地に取られていたら剥がす。**敷地の記録（`sites` / `site_of`）からも外す**
             * ——外さないと、建物を建てる側が「ここは自分の敷地」と思ったまま屋根を載せる。
             */
            out.roles[i] = static_cast<std::uint8_t>(TownRole::Crag);
            out.site_of[i] = -1;
            ++out.crag_cells;
            ++restored;
        }
        if (restored > 0) {
            std::fprintf(stderr, "[hd2d] 町 %d: 覚えていた岩山を %d マス戻しました"
                                 "（既知が少ないと敷地に読まれるマス）\n",
                meaning.identity.town_id, restored);
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (static_cast<TownRole>(out.roles[i]) == TownRole::Crag) {
                crags->cells[i] = 1u;
            }
        }
    }

    /*
     * ---- (2b) 山の段と、山肌に開いた入口（P10 第 3 期。2026-08-10 に決めた）----
     *
     * ズルは「山を刳り抜いた谷」の町で、**店の入口が山肌に開いた 1 マスの穴**である
     * （実データの 12 箇所すべてが、三方を山に囲まれた開いたマス）。山を敷地として仕分ける
     * ことは諦めたので、入口はここで別に拾う——`is_site_cell` を通る成分ではないだけで、
     * 「三方を囲まれた開いたマス＝入口」という読み自体は (3) の 1 周目と同じである。
     */
    std::vector<std::pair<int, int>> crag_gate_exit;
    if (out.crag_cells > 0) {
        const auto is_crag = [&out, at, w, h](int gx, int gy) {
            if ((gx < 0) || (gy < 0) || (gx >= w) || (gy >= h)) {
                return true; //!< 地図の外は山の続き（縁で段が下がると山が切れて見える）
            }
            return static_cast<TownRole>(out.roles[at(gx, gy)]) == TownRole::Crag;
        };
        /*
         * 段は**山の外からの深さ**（8 近傍の幅優先）。4 近傍で測ると、斜めに切れ込んだ谷で
         * 段の縞が谷筋と平行に走って**棚田**に見える。8 近傍なら深さが等方に増えるので、
         * 谷から離れるほど素直に高くなる。
         */
        std::vector<std::int16_t> depth(count, -1);
        std::vector<int> queue;
        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                if (!is_crag(gx, gy)) {
                    continue;
                }
                bool at_edge = false;
                for (int dy = -1; (dy <= 1) && !at_edge; ++dy) {
                    for (int dx = -1; (dx <= 1) && !at_edge; ++dx) {
                        at_edge = ((dx != 0) || (dy != 0)) && !is_crag(gx + dx, gy + dy);
                    }
                }
                if (at_edge) {
                    depth[at(gx, gy)] = 0;
                    queue.push_back(static_cast<int>(at(gx, gy)));
                }
            }
        }
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int here = queue[head];
            const int hx = here % w;
            const int hy = here / w;
            const auto next_depth = static_cast<std::int16_t>(depth[static_cast<std::size_t>(here)] + 1);
            if (next_depth >= kCragTiers) {
                continue; // これ以上は段が変わらない。広げる意味が無い
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = hx + dx;
                    const int ny = hy + dy;
                    if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                        continue;
                    }
                    if (!is_crag(nx, ny) || (depth[at(nx, ny)] >= 0)) {
                        continue;
                    }
                    depth[at(nx, ny)] = next_depth;
                    queue.push_back(static_cast<int>(at(nx, ny)));
                }
            }
        }
        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                if (static_cast<TownRole>(out.roles[at(gx, gy)]) != TownRole::Crag) {
                    continue;
                }
                const std::int16_t d = depth[at(gx, gy)];
                //! 届かなかったマス（山の奥）は最上段。
                out.slices[at(gx, gy)] = static_cast<std::uint8_t>((d < 0) ? (kCragTiers - 1)
                                                                           : std::min<int>(d, kCragTiers - 1));
            }
        }
        //! 山肌に開いた入口。**三方が山で、開いているのが 1 方向だけ**のマス。
        if (!break_town_is("gate")) {
            for (int gy = 0; gy < h; ++gy) {
                for (int gx = 0; gx < w; ++gx) {
                    if ((static_cast<TownRole>(out.roles[at(gx, gy)]) != TownRole::None)
                        || is_site_cell(meaning, gx, gy)) {
                        continue;
                    }
                    /*
                     * **階段のマスには門を立てない**（2026-08-11）。イークの洞窟の `>` は
                     * 山の南の縁に開いていて「三方が山・南だけ開く」をそのまま満たすので、
                     * ここを外さないと**石の門が洞窟の口の上に建つ**。あのマスの絵は
                     * 対応表が持っている（`terrains_surface` の `DOWN_STAIR`）ので、
                     * 町の規則は手を出さずに通す。
                     */
                    if (meaning.role_at(gx, gy) == CellRole::Stairs) {
                        continue;
                    }
                    int open_dir = -1;
                    int walls = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (is_crag(gx + kDx[i], gy + kDy[i])) {
                            ++walls;
                        } else {
                            open_dir = i;
                        }
                    }
                    if ((walls < 3) || (open_dir < 0)) {
                        continue; // 谷そのもの（2 方向以上開いている）か、行き止まりの袋
                    }
                    out.roles[at(gx, gy)] = static_cast<std::uint8_t>(TownRole::Gate);
                    //! 門の姿勢は敷地の門と同じ（1 = 東西にくぐる）。`site_of` は -1 のまま。
                    out.slices[at(gx, gy)] = (kDx[open_dir] != 0) ? 1u : 0u;
                    ++out.gate_cells;
                    ++out.crag_gates;
                    const int odx = kDx[open_dir];
                    const int ody = kDy[open_dir];
                    crag_gate_exit.emplace_back(gx + odx, gy + ody);
                    /*
                     * **看板の南は低い岩に限る**（2026-08-10 に決めた:「ズルの
                     * 東西向きの建物の入り口にある看板の**南側の地形が高いため隠れて
                     * しまっている**。看板の南三マスは低い岩に限定して」）。
                     *
                     * 山肌の入口の看板は**開いている側のマス**に立つ（斜め前を先に試して、
                     * 駄目なら真ん前。`terrain_view.cpp` の門の枝）。どれが選ばれるかは
                     * 置く側が種で決めるので、**候補 3 つとも**の南を下げる。
                     * 下げるのは岩のマスだけなので、谷や道には効かない。
                     */
                    const int latx = -ody;
                    const int laty = odx;
                    for (int side = -1; side <= 1; ++side) {
                        const int sx = gx + odx + (latx * side);
                        const int sy = gy + ody + (laty * side);
                        for (int k = 1; k <= kSignClearSouth; ++k) {
                            const int ny = sy + k;
                            if ((sx < 0) || (ny < 0) || (sx >= w) || (ny >= h)) {
                                continue;
                            }
                            if (static_cast<TownRole>(out.roles[at(sx, ny)]) != TownRole::Crag) {
                                continue;
                            }
                            if (out.slices[at(sx, ny)] != 0u) {
                                out.slices[at(sx, ny)] = 0u;
                                ++out.crag_lowered;
                            }
                        }
                    }
                }
            }
        }
    }

    /*
     * ---- (4c) データの入口を必ず門にする（2026-09-06 に決めた）----
     *
     * ここまでの門は**壁の塊の形**から決めている（塊の面に開いた 1〜4 マスの窪み）。
     * ところが塊が敷地にならないと門も作られない——外接矩形の充填率が `kSiteFill` に
     * 届かない塊（中庭や刻みの多い商館）と、世界の外壁がそれである。実データでは
     * Frox のアナンバールで 13 か所・タロスで 10 か所・辺境の地で 2 か所・
     * アングウィルで 1 か所の戸口が、アーチも看板も無いただの壁として描かれていた。
     *
     * **データが入口と言っているマスは、形が何であれ門である。**印は呼ぶ側が作る
     * （地形の対応表が看板を持つマス）ので、ここは町もコアも知らないままでよい。
     *
     * 横 2 マス・縦 2 マスの入口もこれで門になる。看板は置く側が**並びの先頭に 1 枚**だけ
     * 立てるので（`terrain_view.cpp` の `head_of_run`）、2 マスでも板は 1 枚である。
     */
    if ((entrances != nullptr) && !break_town_is("data_gate")
        && (entrances->size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h))) {
        //! くぐる向きの候補。**南を先に見る**（カメラは南から見下ろすので、戸口は南が正面）。
        static const int kTryDir[4] = { 1, 3, 2, 0 }; // S / E / W / N
        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                if ((*entrances)[at(gx, gy)] == 0u) {
                    continue;
                }
                const TownRole now = static_cast<TownRole>(out.roles[at(gx, gy)]);
                if ((now == TownRole::Gate) || (now == TownRole::Border)) {
                    continue; // 既に門／世界の枠には立てない
                }
                //! 階段のマスには門を立てない（(4a) と同じ理由。口の絵は対応表が持っている）。
                if (meaning.role_at(gx, gy) == CellRole::Stairs) {
                    continue;
                }
                //! そのマスは「くぐれない側」か（壁・岩盤・建物・塀。範囲外も塞がりとみなす）。
                const auto blocked = [&](int nx, int ny) {
                    if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                        return true;
                    }
                    const CellRole role = meaning.role_at(nx, ny);
                    if ((role != CellRole::RoomFloor) && (role != CellRole::CorridorFloor)
                        && (role != CellRole::Doorway) && (role != CellRole::Stairs)) {
                        return true;
                    }
                    const TownRole side = static_cast<TownRole>(out.roles[at(nx, ny)]);
                    return (side == TownRole::House) || (side == TownRole::Manor)
                        || (side == TownRole::Rampart) || (side == TownRole::Crag)
                        || (side == TownRole::Tower) || (side == TownRole::Border);
                };
                /*
                 * くぐる向き。**相方のマス（入口の印が立っている隣）は選ばない**
                 * ——2 マスの入口では相方も「開いている隣」なので、そちらを向きにすると
                 * 門の姿勢が 90 度ずれ、看板の重複を止める判定が効かず板が 2 枚立つ
                 * （2026-09-06 に気づいた）。
                 *
                 * 1 周目は**向かいが塞がっている**ものだけを採る（＝壁の面に開いた戸口。
                 * 表と奥がはっきりする）。見つからなければ 2 周目でその条件を落とす。
                 */
                int open_dir = -1;
                for (int strict = 1; (strict >= 0) && (open_dir < 0); --strict) {
                    for (const int i : kTryDir) {
                        const int nx = gx + kDx[i];
                        const int ny = gy + kDy[i];
                        if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                            continue;
                        }
                        if ((*entrances)[at(nx, ny)] != 0u) {
                            continue; // 相方の入口。そちらは「外」ではない
                        }
                        if (blocked(nx, ny)) {
                            continue;
                        }
                        if ((strict != 0) && !blocked(gx - kDx[i], gy - kDy[i])) {
                            continue; // 向かいも開いている＝壁の面ではない
                        }
                        open_dir = i;
                        break;
                    }
                }
                if (open_dir < 0) {
                    continue; // 四方が塞がっている＝中庭の奥。門にしても入れない
                }
                out.roles[at(gx, gy)] = static_cast<std::uint8_t>(TownRole::Gate);
                /*
                 * 門の姿勢（1 = 東西にくぐる）。`site_of` は**そのマスの持ち主のまま**にする
                 * ——敷地に属していれば前庭の小物が引けるし、属していなければ -1 のままで
                 * 山肌の入口（(4a) の終わり）と同じ扱いになる。
                 */
                const std::uint8_t pose = (kDx[open_dir] != 0) ? 1u : 0u;
                out.slices[at(gx, gy)] = pose;
                /*
                 * **並びの隣が既に門なら、持ち主と姿勢をそちらへ合わせる**（決めたこと
                 * 2026-09-06「2 マスで 1 つの門になるように」）。看板の重複を止める判定は
                 * 「隣が門で持ち主が同じなら自分は先頭ではない」と見るので、持ち主が
                 * 食い違うと 2 枚目が立つ。並びの向きは**くぐる向きと直交する**
                 * （東西にくぐる門は縦に並ぶ）。
                 */
                const int run_dx = (pose != 0u) ? 0 : 1;
                const int run_dy = (pose != 0u) ? 1 : 0;
                for (int side = -1; side <= 1; side += 2) {
                    const int nx = gx + (run_dx * side);
                    const int ny = gy + (run_dy * side);
                    if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                        continue;
                    }
                    if (static_cast<TownRole>(out.roles[at(nx, ny)]) != TownRole::Gate) {
                        continue;
                    }
                    out.slices[at(gx, gy)] = out.slices[at(nx, ny)];
                    out.site_of[at(gx, gy)] = out.site_of[at(nx, ny)];
                    break;
                }
                ++out.gate_cells;
                ++out.data_gates;
            }
        }
    }

    /*
     * ---- (4b) 目印の建物（試作由来の shop / mill が世界に出るのはここ）----
     *
     * §10.3 は「入口を持たない成分 → 視線の拘束が無い。**ここが大きな建物の置き場所**」と
     * 言っている。その枠に、9 スライスで組む家ではなく**作り込んだ 1 棟**を建てる。
     * 町ごとに shop 1 棟・mill 1 棟までなので、「そこにしかない建物」として読める。
     * 敷地の走査順（左上から）で決まるので、同じ町なら毎回同じ場所に建つ。
     */
    if (!style.no_stock_landmarks) {
        struct Landmark {
            int id;
            int span_x;
            int span_y;
        };
        for (const Landmark &mark : { Landmark{ 1, 4, 4 }, Landmark{ 2, 4, 3 } }) {
            for (auto &site : out.sites) {
                if (site.has_gate || (site.landmark != 0) || !site.has_house()) {
                    continue;
                }
                if (((site.house_x1 - site.house_x0 + 1) < mark.span_x)
                    || ((site.house_y1 - site.house_y0 + 1) < mark.span_y)) {
                    continue;
                }
                site.landmark = mark.id;
                // 建物を目印の大きさへ縮め、はみ出したマスは前庭へ戻す。
                for (int gy = site.house_y0; gy <= site.house_y1; ++gy) {
                    for (int gx = site.house_x0; gx <= site.house_x1; ++gx) {
                        if ((gx < site.house_x0 + mark.span_x) && (gy < site.house_y0 + mark.span_y)) {
                            continue;
                        }
                        out.roles[at(gx, gy)] = static_cast<std::uint8_t>(TownRole::Yard);
                        --out.house_cells;
                        ++out.yard_cells;
                    }
                }
                site.house_x1 = site.house_x0 + mark.span_x - 1;
                site.house_y1 = site.house_y0 + mark.span_y - 1;
                break;
            }
        }
    }

    /*
     * ---- (4c) 噴水（2026-08-09 に決めた）----
     *
     * 「中央の池は動的な水しぶきを高く噴き上げる噴水を中央に。水しぶきが 4 本の柱に
     * たれ落ちる感じで」。
     *
     * **形だけで見分ける。**モリバントの噴水は実データで
     * 「開いたマスの四方が、どれも**1 マスだけの独立した石の塊**」という並びをしている
     * （`03_Morivant.txt` の 31〜35 行の内側の菱形）。変愚のミニマップは水を送らないので
     * （`MinimapKind::Water` は FH-09 で出来たが、細別を送るのは今のところ Frox だけ）
     * 水では引けないが、**この並びは他の町に無い**のでこれで足りる。
     * 4 つの石はそのままだと 1 マスの塀になっていた。
     */
    for (int gy = 1; gy < (h - 1); ++gy) {
        for (int gx = 1; gx < (w - 1); ++gx) {
            if (is_site_cell(meaning, gx, gy)
                || (static_cast<TownRole>(out.roles[at(gx, gy)]) != TownRole::None)) {
                continue;
            }
            bool ringed = true;
            for (int i = 0; (i < 4) && ringed; ++i) {
                const std::int32_t id = label[at(gx + kDx[i], gy + kDy[i])];
                ringed = (id >= 0) && (components[static_cast<std::size_t>(id)].size() == 1)
                    && (static_cast<TownRole>(out.roles[at(gx + kDx[i], gy + kDy[i])]) == TownRole::Rampart);
            }
            if (!ringed) {
                continue;
            }
            out.roles[at(gx, gy)] = static_cast<std::uint8_t>(TownRole::Fountain);
            out.slices[at(gx, gy)] = 1u; //!< 1 = 噴き上げ
            ++out.fountain_cells;
            for (int i = 0; i < 4; ++i) {
                const int nx = gx + kDx[i];
                const int ny = gy + kDy[i];
                out.roles[at(nx, ny)] = static_cast<std::uint8_t>(TownRole::Fountain);
                out.slices[at(nx, ny)] = 0u; //!< 0 = 柱
                ++out.fountain_cells;
                --out.rampart_cells;
            }
            /*
             * **噴水を囲む石も噴水の一部**にする（2026-08-09 に決めた:「噴水の周りが
             * 壁では違和感があるので、**低い柵と柵の内側には花壇**を設置しよう」）。
             *
             * ここを塀のまま置くと 2.2 マスの城壁が池を囲み、**噴水が小さな城に見える**
             * （実機の絵で気づいた）。噴き上げの近く（チェビシェフ 3 マス）にある
             * **小さな石の塊**を丸ごと縁に読み替える。町の塀は 99 マス以上あるので巻き込まない。
             */
            for (int ry = gy - kFountainRim; ry <= (gy + kFountainRim); ++ry) {
                for (int rx = gx - kFountainRim; rx <= (gx + kFountainRim); ++rx) {
                    if ((rx < 0) || (ry < 0) || (rx >= w) || (ry >= h)) {
                        continue;
                    }
                    const std::int32_t id = label[at(rx, ry)];
                    if ((id < 0) || (components[static_cast<std::size_t>(id)].size() >= kMinTowerCells)) {
                        continue;
                    }
                    for (const auto &[cx, cy] : components[static_cast<std::size_t>(id)]) {
                        if (static_cast<TownRole>(out.roles[at(cx, cy)]) == TownRole::Fountain) {
                            continue; // 柱として既に読んである
                        }
                        out.roles[at(cx, cy)] = static_cast<std::uint8_t>(TownRole::Fountain);
                        out.slices[at(cx, cy)] = 2u; //!< 2 = 縁（低い柵と花壇）
                        ++out.fountain_cells;
                        --out.rampart_cells;
                    }
                }
            }
        }
    }

    /*
     * ---- (5) 柵（§10.3「柵が二役を果たす」）----
     * 敷地の**外**に面した辺に立てる。建物のマスには立てない（壁がその役をする）。
     * 門の側にも立てない（そこは通る所である）。
     */
    for (int gy = 0; gy < h; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            const TownRole role = static_cast<TownRole>(out.roles[at(gx, gy)]);
            if (role == TownRole::Yard) {
                const std::int16_t mine = out.site_of[at(gx, gy)];
                std::uint8_t bits = 0;
                for (int i = 0; i < 4; ++i) {
                    const int nx = gx + kDx[i];
                    const int ny = gy + kDy[i];
                    const bool inside = (nx >= 0) && (ny >= 0) && (nx < w) && (ny < h)
                        && (out.site_of[at(nx, ny)] == mine);
                    if (inside) {
                        continue; // 同じ敷地（建物・前庭・門）。柵は要らない
                    }
                    bits |= static_cast<std::uint8_t>(1u << i); // FENCE_N/S/W/E は kDx/kDy と同じ並び
                }
                out.fences[at(gx, gy)] = bits;
                continue;
            }
            /*
             * **塀の上の胸壁**（2026-08-09 に決めた:「中世の城壁のように上を人が
             * 歩けるような形で石の柵を城壁の上にたてよう」）。柵と同じ「1 枚 1 辺」で、
             * **塀でない側**へ向いた辺にだけ立てる。門の側には立てない（通る所である）。
             *
             * マスごとに四方へ狭間を載せると、厚みのある壁の上が窪みの並びになって
             * 格子に見える（罠 71）。外を向いた辺だけに載せればその形にならない。
             */
            /*
             * 噴水の縁も同じ仕掛けで**低い柵**を持つ（2026-08-09 に決めた）。
             * 「外」は「隣が噴水でない側」。
             */
            const bool rim = (role == TownRole::Fountain)
                && (out.slices[at(gx, gy)] == 2u);
            /*
             * **屋敷の内壁**（デザイン4。「屋敷の内壁高さ2ブロック。下部が赤く塗られた
             * 板張り、上部が白の壁紙」）。屋敷の bbox の中の**歩けるマス**（＝屋内）の、
             * 屋敷の壁に面した辺へビットを立てる。板を貼るのは床のマスの側——壁のマスに
             * 貼ると 3 階の壁パネルの箱に埋まる。
             */
            if (role == TownRole::None) {
                bool inside_manor = false;
                for (const auto &box : out.manors) {
                    if ((gx >= box[0]) && (gx <= box[2]) && (gy >= box[1]) && (gy <= box[3])) {
                        inside_manor = true;
                        break;
                    }
                }
                if (inside_manor) {
                    std::uint8_t bits = 0;
                    for (int i = 0; i < 4; ++i) {
                        const int nx = gx + kDx[i];
                        const int ny = gy + kDy[i];
                        if ((nx >= 0) && (ny >= 0) && (nx < w) && (ny < h)
                            && (static_cast<TownRole>(out.roles[at(nx, ny)]) == TownRole::Manor)) {
                            bits |= static_cast<std::uint8_t>(1u << i);
                        }
                    }
                    if (bits != 0u) {
                        out.fences[at(gx, gy)] = bits;
                    }
                    continue;
                }
            }
            if ((role != TownRole::Rampart) && !rim) {
                continue;
            }
            std::uint8_t bits = 0;
            for (int i = 0; i < 4; ++i) {
                const int nx = gx + kDx[i];
                const int ny = gy + kDy[i];
                if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                    continue; // 地図の外。手を出さない
                }
                const TownRole side = static_cast<TownRole>(out.roles[at(nx, ny)]);
                const bool inside = rim
                    ? (side == TownRole::Fountain)
                    : ((side == TownRole::Rampart) || (side == TownRole::Tower)
                        || (side == TownRole::Border) || (side == TownRole::Gate));
                if (!inside) {
                    bits |= static_cast<std::uint8_t>(1u << i);
                }
            }
            out.fences[at(gx, gy)] = bits;
        }
    }

    /*
     * ---- (5b) 閉じた大門（2026-08-18 に決めた）----
     *
     * 「大門は開いて通過させる必要はないので、閉じた大門としてつくり、**南側の壁の
     * 一部に組み込んで**」。実際の出入りは塀の切れ目（街道）が受け持ち、ここで選ぶのは
     * **絵として差し替える塀のマス 1 つ**である（マスの役割は `Rampart` のまま＝数も検査も
     * 動かない。描く側がマスを見て絵だけ替える）。
     *
     * 置き場は**塀のいちばん南の並び**。並びの間に狭い切れ目（人の通る隙間）があれば
     * **その東隣**——人里では街道と堀の橋が切れ目を通るので、大門は橋のたもとに立つ。
     * 切れ目が無ければ、いちばん長い並びの中央。
     *
     * @note ミニマップの既知から読むので、南の塀をまだ見ていない段では別の行が
     * 最南になりうる（探索が進めば正しい塀へ移る。町の読み全体と同じ性質）。
     */
    if ((style.great_gate != nullptr) && (style.great_gate[0] != '\0')) {
        int south_y = -1;
        for (int gy = h - 1; (gy >= 0) && (south_y < 0); --gy) {
            for (int gx = 0; gx < w; ++gx) {
                if (static_cast<TownRole>(out.roles[at(gx, gy)]) == TownRole::Rampart) {
                    south_y = gy;
                    break;
                }
            }
        }
        if (south_y >= 0) {
            std::vector<std::pair<int, int>> runs; //!< その行の塀の連続区間 [first..second]
            /*
             * **やぐら（Tower）も並びに数える。**塔は塀の成分の隅に立つので、切れ目の
             * 両脇がちょうど塔になっていることがある（紅魔館の参道の開口がそうだった
             * ——両脇が塔に取られ、隙間が 9 マスから 11 マスに膨らんで上限を超えた）。
             * 大門に選ばれたマスがあとで塔だったら Rampart へ戻す（門が塔に勝つ）。
             */
            const auto rampartish = [&](int gx) {
                const TownRole role = static_cast<TownRole>(out.roles[at(gx, south_y)]);
                return (role == TownRole::Rampart) || (role == TownRole::Tower);
            };
            for (int gx = 0; gx < w; ++gx) {
                if (!rampartish(gx)) {
                    continue;
                }
                if (runs.empty() || (runs.back().second != (gx - 1))) {
                    runs.emplace_back(gx, gx);
                } else {
                    runs.back().second = gx;
                }
            }
            //! 並びの間の狭い隙間＝人の通る切れ目。上限は表から（既定 1〜6 マス。紅魔館の
            //! 参道の開口は 9 マスなので表が 10 を書く）。広い隙間は「別の塀」とみなす。
            const int max_gap = (style.great_gate_span > 0) ? style.great_gate_span : 6;
            for (std::size_t i = 0; (i + 1) < runs.size(); ++i) {
                const int gap = runs[i + 1].first - runs[i].second - 1;
                if ((gap >= 1) && (gap <= max_gap)) {
                    //! 両脇モード（紅魔館「b の横と、左側の塀の終端」）は西隣も差し替える。
                    if (style.great_gate_flank) {
                        out.great_gates.push_back({ runs[i].second, south_y });
                    }
                    out.great_gates.push_back({ runs[i + 1].first, south_y });
                    break;
                }
            }
            if (out.great_gates.empty() && !runs.empty()) {
                const auto longest = std::max_element(runs.begin(), runs.end(),
                    [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                        return (a.second - a.first) < (b.second - b.first);
                    });
                out.great_gates.push_back({ (longest->first + longest->second) / 2, south_y });
            }
            //! 大門のマスが塔だったら塀へ戻す（描く側の大門の分岐は Rampart 条件）。
            for (const auto &gate : out.great_gates) {
                if (static_cast<TownRole>(out.roles[at(gate[0], gate[1])]) == TownRole::Tower) {
                    out.roles[at(gate[0], gate[1])] = static_cast<std::uint8_t>(TownRole::Rampart);
                }
            }
            //! 検算（絵を見る前に判定を疑えるように）。出るのは町の読み直しのときだけ。
            std::fprintf(stderr, "[hd2d] (5b) 大門: span=%d flank=%d runs=%zu gates=%zu\n",
                style.great_gate_span, style.great_gate_flank ? 1 : 0, runs.size(),
                out.great_gates.size());
        }
    }

    /*
     * ---- (6) 通り道と草（2026-08-09 に決めた）----
     *
     * 「辺境なので地面は土の地面で、**建物と建物をつなぐ通り道は踏み固められて**
     * 地面がむき出しに。それ以外は短めの草が生えているように」。
     *
     * 「通り道」を density や見た目の勘で撒くと、道に見えない斑になる。**人が実際に
     * 歩く線**＝門と階段を結ぶ最短路の集まりにする（踏み跡＝desire line）。
     * 手順は素朴な最小木で、地点を 1 つずつ「既に道になっている所」へ最短路で繋ぐ。
     */
    {
        std::vector<std::uint8_t> open(count, 0);
        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                const CellRole role = meaning.role_at(gx, gy);
                const bool floorish = (role == CellRole::RoomFloor) || (role == CellRole::CorridorFloor)
                    || (role == CellRole::Doorway) || (role == CellRole::Stairs);
                open[at(gx, gy)] = (floorish && (static_cast<TownRole>(out.roles[at(gx, gy)]) == TownRole::None))
                    ? 1u
                    : 0u;
            }
        }
        //! 人が向かう先: 店と施設の門の**外側**、そして階段（地下への入口）。
        std::vector<std::pair<int, int>> points;
        for (const TownSite &site : out.sites) {
            if (!site.has_gate) {
                continue;
            }
            const int nx = site.gate_x + site.gate_dx;
            const int ny = site.gate_y + site.gate_dy;
            if ((nx >= 0) && (ny >= 0) && (nx < w) && (ny < h) && (open[at(nx, ny)] != 0u)) {
                points.emplace_back(nx, ny);
            }
        }
        //! 山肌に開いた入口も同じ「人が向かう先」（ズルの店はここにしか無い）。
        for (const auto &[nx, ny] : crag_gate_exit) {
            if ((nx >= 0) && (ny >= 0) && (nx < w) && (ny < h) && (open[at(nx, ny)] != 0u)) {
                points.emplace_back(nx, ny);
            }
        }
        /*
         * **大門の開口も「人が向かう先」**（デザイン4・2026-08-18。紅魔館の参道）。
         * 両脇に大門が立つ町では、正門の開口こそ人の出入り口なのに、道の網の地点が
         * 中（入口と階段）にしか無いと参道が草のまま残る。開口の中央の 1 マス北
         * （塀の内側）を地点に足す。**両脇モードの町だけ**——人里（東隣 1 つ）の
         * 網は従来どおり動かさない。
         */
        if (style.great_gate_flank && (out.great_gates.size() >= 2)) {
            const int mid_x = (out.great_gates[0][0] + out.great_gates[1][0]) / 2;
            const int mid_y = out.great_gates[0][1] - 1;
            if ((mid_x >= 0) && (mid_y >= 0) && (mid_x < w) && (mid_y < h)
                && (open[at(mid_x, mid_y)] != 0u)) {
                points.emplace_back(mid_x, mid_y);
            }
        }
        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                if ((meaning.role_at(gx, gy) == CellRole::Stairs) && (open[at(gx, gy)] != 0u)) {
                    points.emplace_back(gx, gy);
                }
            }
        }

        std::vector<std::uint8_t> road(count, 0);
        std::vector<int> parent(count, -1);
        std::vector<int> queue;
        if (!points.empty() && !break_town_is("paths")) {
            road[at(points[0].first, points[0].second)] = 1u;
            out.path_links = 1;
            for (std::size_t i = 1; i < points.size(); ++i) {
                //! 幅優先で「既に道になっているマス」へ届いたら、そこまでの道筋を道にする。
                std::fill(parent.begin(), parent.end(), -1);
                queue.clear();
                const int start = static_cast<int>(at(points[i].first, points[i].second));
                parent[static_cast<std::size_t>(start)] = start;
                queue.push_back(start);
                int hit = -1;
                for (std::size_t head = 0; (head < queue.size()) && (hit < 0); ++head) {
                    const int here = queue[head];
                    const int hx = here % w;
                    const int hy = here / w;
                    for (int d = 0; d < 4; ++d) {
                        const int nx = hx + kDx[d];
                        const int ny = hy + kDy[d];
                        if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                            continue;
                        }
                        const auto next = static_cast<int>(at(nx, ny));
                        if ((open[static_cast<std::size_t>(next)] == 0u)
                            || (parent[static_cast<std::size_t>(next)] >= 0)) {
                            continue;
                        }
                        parent[static_cast<std::size_t>(next)] = here;
                        if (road[static_cast<std::size_t>(next)] != 0u) {
                            hit = next;
                            break;
                        }
                        queue.push_back(next);
                    }
                }
                if (hit < 0) {
                    continue; // どこへも繋がらない地点（塀の向こうなど）。道にしない
                }
                ++out.path_links;
                for (int step = hit; step != parent[static_cast<std::size_t>(step)];
                     step = parent[static_cast<std::size_t>(step)]) {
                    road[static_cast<std::size_t>(step)] = 1u;
                }
                road[static_cast<std::size_t>(start)] = 1u;
            }
        }
        //! 1 マス幅の線のままだと「筋」であって「道」に見えない。左右へ 1 マスずつ広げる。
        std::vector<std::uint8_t> wide = road;
        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                if (road[at(gx, gy)] == 0u) {
                    continue;
                }
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = gx + dx;
                        const int ny = gy + dy;
                        if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h) || (open[at(nx, ny)] == 0u)) {
                            continue;
                        }
                        wide[at(nx, ny)] = 1u;
                    }
                }
            }
        }
        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                if (open[at(gx, gy)] == 0u) {
                    continue;
                }
                const bool is_path = wide[at(gx, gy)] != 0u;
                out.roles[at(gx, gy)] = static_cast<std::uint8_t>(is_path ? TownRole::Path : TownRole::Turf);
                (is_path ? out.path_cells : out.turf_cells) += 1;
            }
        }
    }

    /*
     * ---- (7) 門の見通し（P10 第 3 期。2026-08-10 に決めた）----
     *
     * 「アングウィル 建物の入り口に**木のオブジェクトが被らないように木を減らして**」。
     *
     * ここが持つのは**見えていてほしいマス**（門とその周り 1 マス）だけで、何を置かないかは
     * 置く側が決める。**南へどこまで空けるかは素材の高さで決まる**ので、ここでマスに
     * 印を付けても意味が無い——半径を 1 つ決める作りにすると、大木を除ける半径では
     * 低木まで消え、低木に合わせると大木が被ったままになる。
     *
     * **門は全部**（敷地の門も山肌の入口も）。**ダンジョンの口も同じ**（2026-08-11。
     * 「岩山と入り口に」と決めた）——辺境の地のイークの洞窟は森の真ん中にあり、
     * 7 マスの大木に囲まれて**口が 1 画素も見えなかった**（絵で確かめた）。
     * 入口は「そこへ入れることが見えていなければ意味が無い」ものなので、
     * 店の門と同じ扱いにする。
     */
    for (int gy = 0; gy < h; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            const bool is_gate = static_cast<TownRole>(out.roles[at(gx, gy)]) == TownRole::Gate;
            const bool is_stairs = meaning.role_at(gx, gy) == CellRole::Stairs;
            if (!is_gate && !is_stairs) {
                continue;
            }
            for (int dy = -kClearRing; dy <= kClearRing; ++dy) {
                for (int dx = -kClearRing; dx <= kClearRing; ++dx) {
                    const int nx = gx + dx;
                    const int ny = gy + dy;
                    if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                        continue;
                    }
                    out.clearing[at(nx, ny)] = 1u;
                }
            }
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        out.clearing_cells += (out.clearing[i] != 0u) ? 1 : 0;
    }

    /*
     * ---- (8) 入口の手前の塀を低くする（2026-08-27 に決めた）----
     *
     * 「**鉄獄の入り口が丸太の山に囲まれて見えない**ので手前の丸太は背の低い
     *   踏破不可のオブジェクトで置き換えて」。
     *
     * Frox の辺境の地（荒野を持たない遊び方の `t_lite.txt` / `t_ulite.txt`）は、
     * 鉄獄の口を**永久壁 29 マスの塚**の東面に開けている。塚は外接矩形 7×6・
     * 充填 0.69 で敷地の下限（`kSiteFill` = 0.72）にわずかに届かず、山でもなく
     * `kCragCells` にも遠いので、上の (1) で**丸ごと塀**に落ちる。辺境の意匠の塀は
     * 丸太（1.81〜1.88 マス）で、**ダンジョンの口の絵（1.31 マス）より高い**——
     * つまり口が塀に埋まって 1 画素も見えなかった。
     *
     * (7) と同じ「入口はそこへ入れることが**見えていなければ意味が無い**」の理屈だが、
     * あちらは木を**立てない**話で、ここは壁のマスそのものなので消せない（歩けない
     * ことが地図の意味である）。だから**下げる**。ここが持つのは印だけで、何を置くかは
     * 置く側が決める（`terrain_view.cpp` の塀の枝が低い岩棚を引く）。
     *
     * 届く先は**看板の手前とまったく同じ形**にしてある（`kSignClearSouth`。上の (5) の
     * 岩山の看板と揃えた）: 口とその東西隣の**南 3 マス**——3×3 の帯だけである。
     * カメラは南から見下ろすので、南が空けば口が見える。
     *
     * **八方の輪にしないのが要点である。**輪で取ると**町の出口**（塀に開いた 1 マスの
     * 切れ目。`TOWN_EXIT` も `CellRole::Stairs` に入る）で左右の塀まで下がり、
     * 変愚の辺境の地とテルモラで**閉じた絵が動いた**（実測 12 マス・8 マス）。
     * 出口は塀を貫く道なので**南が塀ではない**——帯で取れば 1 マスも当たらない。
     *
     * さらに「**塚に埋まった口だけ**」に絞る: 南に塀があり、**しかも東西のどちらかにも
     * 塀が回り込んでいる**ことを条件にする。塀の線がただ口の南を通っているだけの所
     * （変愚の辺境の地・モリバントの南の塀。実測 7 マス・3 マス）では、塀は口の
     * 「向こう側」にあって隠していない——**決着させた絵を動かさない**ために弾く
     * （`terrain_view.cpp` の胸壁の註記と同じ理屈）。
     *
     * **やぐら（`Tower`）は下げない。**あれは 40 マス以上の塀にだけ立つ数少ない目印で、
     * 口の隣に来ることがそもそも稀なうえ、下げると町の遠景が変わる。
     */
    for (int gy = 0; gy < h; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            if (meaning.role_at(gx, gy) != CellRole::Stairs) {
                continue;
            }
            const auto is_rampart = [&out, w, h](int nx, int ny) {
                if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                    return false;
                }
                const std::size_t k = (static_cast<std::size_t>(ny) * static_cast<std::size_t>(w))
                    + static_cast<std::size_t>(nx);
                return static_cast<TownRole>(out.roles[k]) == TownRole::Rampart;
            };
            //! 塚に埋まった口か（南に塀・東西のどちらかにも塀）。
            if (!is_rampart(gx, gy + 1)
                || (!is_rampart(gx - 1, gy) && !is_rampart(gx + 1, gy))) {
                continue;
            }
            const auto lower = [&out, w, h](int nx, int ny) {
                if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h)) {
                    return;
                }
                const std::size_t k = (static_cast<std::size_t>(ny) * static_cast<std::size_t>(w))
                    + static_cast<std::size_t>(nx);
                if (static_cast<TownRole>(out.roles[k]) != TownRole::Rampart) {
                    return;
                }
                if (out.slices[k] == kRampartLow) {
                    return;
                }
                out.slices[k] = kRampartLow;
                ++out.rampart_lowered;
            };
            for (int side = -1; side <= 1; ++side) {
                for (int k = 1; k <= kSignClearSouth; ++k) {
                    lower(gx + side, gy + k);
                }
            }
        }
    }
    return true;
}

std::string town_plan_summary(const TownPlan &plan)
{
    int gated = 0;
    int landmarks = 0;
    for (const TownSite &site : plan.sites) {
        gated += site.has_gate ? 1 : 0;
        landmarks += (site.landmark != 0) ? 1 : 0;
    }
    char buf[384]{};
    std::snprintf(buf, sizeof(buf),
        "town %d  %dx%d  敷地=%zu（門あり %d・露店 %d・目印 %d）門=%d（山肌 %d・データ %d）建物=%d 前庭=%d"
        " 塀=%d 枠=%d 岩山=%d（看板の手前を下げた %d）  噴水=%d 道=%d（結んだ地点 %d）草=%d 見通し=%d",
        plan.identity.town_id, plan.width, plan.height, plan.sites.size(), gated, plan.stall_sites,
        landmarks, plan.gate_cells, plan.crag_gates, plan.data_gates, plan.house_cells, plan.yard_cells,
        plan.rampart_cells, plan.border_cells, plan.crag_cells, plan.crag_lowered,
        plan.fountain_cells, plan.path_cells, plan.path_links, plan.turf_cells, plan.clearing_cells);
    //! 入口の手前で下げた塀（2026-08-27）。下げた町でだけ 1 語足す。
    if (plan.rampart_lowered > 0) {
        const std::size_t used = std::strlen(buf);
        std::snprintf(buf + used, sizeof(buf) - used, " 入口の手前の塀を下げた=%d",
            plan.rampart_lowered);
    }
    //! 閉じた大門（2026-08-18）。選ばれた町でだけ 1 語足す——絵を見る前に判定を検算できる。
    for (const auto &gate : plan.great_gates) {
        const std::size_t used = std::strlen(buf);
        std::snprintf(buf + used, sizeof(buf) - used, " 大門=(%d,%d)", gate[0], gate[1]);
    }
    //! 屋敷（デザイン4）。読まれた町でだけ出る。
    for (const auto &box : plan.manors) {
        const std::size_t used = std::strlen(buf);
        std::snprintf(buf + used, sizeof(buf) - used, " 屋敷=%dマス(%d,%d)-(%d,%d)", plan.manor_cells,
            box[0], box[1], box[2], box[3]);
    }
    //! 御柱（デザイン13）。読まれた町でだけ出る。
    if (!plan.quad_walls.empty()) {
        const std::size_t used = std::strlen(buf);
        std::snprintf(buf + used, sizeof(buf) - used, " 2x2の柱=%zu", plan.quad_walls.size());
    }
    //! 棟の割り（デザイン9）。割った町でだけ出る。
    if ((plan.wing_cells[1] + plan.wing_cells[2]) > 0) {
        const std::size_t used = std::strlen(buf);
        std::snprintf(buf + used, sizeof(buf) - used, " 棟=主%d/別棟%d/廊下%d", plan.wing_cells[0],
            plan.wing_cells[1], plan.wing_cells[2]);
    }
    return buf;
}

} // namespace hd2d
