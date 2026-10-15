/*!
 * @file fx_spirit.cpp
 * @brief 呪い・死・心・状態変化・地形・特殊の系統の見た目（〜§5.8）。
 *
 * | 節 | 属性 | 見た目 |
 * |---|---|---|
 * | §5.4 | `cause_1`〜`cause_4`・`wounds` | どくろが浮かんで消える。段階が上がるほど大きく、色を濃く |
 * | | `hand_doom` | 対象の上に黒い大きな手が現れ、握りつぶす |
 * | | `hypodynamia` | 対象から赤い粒が、撃った者へ流れて吸い込まれる |
 * | §5.5 | `psi`・`mind_blast`・`brain_smash` | 対象の頭の上に桃色の波紋の輪が縮んでいく |
 * | | `psi_drain`・`drain_mana` | 生命吸収と同じ流れ。色は桃色と青 |
 * | | `confusion`・`old_conf` | 対象の頭の上を星が回る |
 * | | `charm`・`charm_living`・`control_*`・`domination` | 桃色の光の輪が対象を包み、締まって消える |
 * | §5.6 | `old_sleep` | 青白い粒が対象の上から静かに降る |
 * | | `old_slow` | 下向きの矢印が上から下りてくる |
 * | | `old_speed` | 上向きの矢印が下から上がる |
 * | | `old_heal`・`star_heal` | 緑の光の粒が足元から昇る |
 * | | `old_poly` | 白い煙がぽんと出る |
 * | | `old_clone` | 対象が一瞬二重になってずれる |
 * | | `turn_*` | 対象の周りに黒い影が震えながらまとわりつく |
 * | | `away_*` | 対象のタイルから光の粒がほどけて散る（対象そのものは消さない） |
 * | | `stasis`・`stasis_evil` | 対象が灰色に固まり、周りに時計の輪が出る |
 * | | `disp_*` | 視界全体なら光の波。1 体なら属性の色の閃光 |
 * | §5.7 | `kill_wall`・`make_wall`・`stone_wall` | 土煙と石くず |
 * | | `kill_door` | 木片が飛び散る |
 * | | `make_rune_protection` | 床に光る紋様が描かれて定着する |
 * | | `kill_trap`・`make_trap`・`make_door`・`jam_door`・`make_tree` | 小さな土煙 |
 * | §5.8 | `capture` | **原作どおり**、玉が飛んで当たるところまで。当たった所には成否に関係なく同じ光の輪 |
 * | | `anim_dead` | 床から緑の霊気が昇る |
 * | | `photo` | 画面全体が一瞬白く光る |
 * | | `engetsu` | 三日月の形の光の刃が弧を描いて走る |
 * | | `telekinesis` | 対象が浮き上がって揺れる |
 * | | `identify`・`genocide`・`e_genocide`・`hungry` | 小さな光の輪か、属性の色の靄 |
 *
 * **原作より情報を増やさない**（§5 の約束）：効いたか・耐えたか・成功したかで絵を変えない。
 *
 * ## 作り
 * 属性ごとに「見た目の種類」（`Kind`）と段階・色を表（`kEntries`）で持つ。見た目 1 つ（`Look`）は
 * 「対象のマス・始まりからの秒・大きさ」から絵を作る関数で、撃ち方の形ごとに次のように使う。
 * - 飛ばない魔法（隠れた投射・足元だけ）：対象のマスに `Look` を 1 つ。視界内の全員なら属性の色の光の波。
 * - ボルト・ボール：飛んでいる間は属性の色の小さな玉。着弾で `Look`（ボールは範囲のマスごとに、
 *   中心は全部、周りは小さな版を距離ぶん遅らせて。マスが多いとき・ブレスは靄だけ）。
 * - ビーム：経路に属性の色の光の線、終点に `Look`。ジャンプ（直接）はボールの着弾と同じ。
 */
#include "render/fx_styles/fx_parts.h"
#include "render/fx_styles/fx_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265f;

//! 見た目の種類。
enum class Kind : std::uint8_t {
    Generic,   //!< 汎用（見た目の指定が無い属性）
    Skull,     //!< どくろが浮かんで消える（`level` 1〜4）
    Hand,      //!< 黒い手が握りつぶす
    Drain,     //!< 対象から撃った者へ粒が流れる
    Ripple,    //!< 頭の上で桃色の輪が縮む（`level` は輪の数 2〜4）
    Stars,     //!< 頭の上を星が回る（`level` 2 は気絶の速い版）
    Wrap,      //!< 光の輪が包んで締まる（`level` は輪の数）
    Sleep,     //!< 青白い粒が静かに降る
    ArrowDown, //!< 下向きの矢印
    ArrowUp,   //!< 上向きの矢印
    Heal,      //!< 緑の粒が足元から昇る（`level` 2 は豪華）
    Poly,      //!< 白い煙がぽんと出る
    Clone,     //!< 一瞬二重にずれる
    Fear,      //!< 黒い影が震えながらまとわりつく
    Unravel,   //!< タイルから光の粒がほどける
    Stasis,    //!< 灰色に固まり、周りに時計の輪（`level` 1 は邪悪の版）
    Flash,     //!< 属性の色の閃光（`level` 1 は十字つき）
    Dust,      //!< 土煙（`level` 1 小・2 森・3 溶解・4 生成・5 石の壁）
    Splinters, //!< 木片が飛び散る
    Rune,      //!< 床に紋様が描かれる
    Capture,   //!< 玉が飛んで当たり、光の輪
    Rise,      //!< 床から緑の霊気が昇る
    Photo,     //!< 画面全体が白く光る
    Crescent,  //!< 三日月の光の刃が弧を描く
    Lift,      //!< 浮き上がって揺れる
    Identify,  //!< 小さな光の輪
    Mist,      //!< 属性の色の靄と輪（`level` 0 空腹・1 抹殺・2 超抹殺）
};

struct Entry {
    const char *attr;
    //! 汎用の色（玉・光の波・靄）。
    FxColour base;
    float smoke;
    float emissive;
    Kind kind;
    int level;
    //! 見た目の第 2 の色（どくろの色・粒の終わりの色・影の縁取りなど）。
    FxColour tint;
};

//! このファイルが担当する属性。
const Entry kEntries[] = {
    // §5.4 呪い・死
    { "cause_1", { 0.55f, 0.15f, 0.25f }, 0.40f, 0.9f, Kind::Skull, 1, { 0.95f, 0.88f, 1.00f } },
    { "cause_2", { 0.50f, 0.12f, 0.22f }, 0.45f, 0.9f, Kind::Skull, 2, { 1.00f, 0.62f, 0.68f } },
    { "cause_3", { 0.45f, 0.08f, 0.20f }, 0.50f, 0.8f, Kind::Skull, 3, { 1.00f, 0.34f, 0.42f } },
    { "cause_4", { 0.40f, 0.05f, 0.18f }, 0.55f, 0.8f, Kind::Skull, 4, { 0.85f, 0.10f, 0.22f } },
    { "wounds", { 0.70f, 0.10f, 0.12f }, 0.40f, 0.9f, Kind::Skull, 2, { 1.00f, 0.20f, 0.16f } },
    { "hand_doom", { 0.18f, 0.06f, 0.24f }, 0.60f, 0.6f, Kind::Hand, 0, { 0.60f, 0.22f, 0.85f } },
    { "hypodynamia", { 0.90f, 0.15f, 0.20f }, 0.30f, 1.0f, Kind::Drain, 0, { 1.00f, 0.42f, 0.34f } },
    // §5.5 心
    { "psi", { 1.00f, 0.60f, 0.80f }, 0.10f, 1.0f, Kind::Ripple, 2, { 1.00f, 0.80f, 0.92f } },
    { "mind_blast", { 1.00f, 0.60f, 0.80f }, 0.10f, 1.0f, Kind::Ripple, 3, { 1.00f, 0.80f, 0.92f } },
    { "brain_smash", { 1.00f, 0.55f, 0.75f }, 0.10f, 1.0f, Kind::Ripple, 4, { 1.00f, 0.70f, 0.90f } },
    { "psi_drain", { 1.00f, 0.55f, 0.80f }, 0.10f, 1.0f, Kind::Drain, 0, { 1.00f, 0.85f, 0.95f } },
    { "drain_mana", { 0.40f, 0.50f, 1.00f }, 0.10f, 1.0f, Kind::Drain, 0, { 0.75f, 0.88f, 1.00f } },
    { "confusion", { 0.90f, 0.80f, 0.30f }, 0.10f, 1.0f, Kind::Stars, 1, { 1.00f, 0.92f, 0.45f } },
    { "old_conf", { 0.90f, 0.80f, 0.30f }, 0.10f, 1.0f, Kind::Stars, 1, { 1.00f, 0.75f, 0.35f } },
    { "charm", { 1.00f, 0.65f, 0.85f }, 0.05f, 1.0f, Kind::Wrap, 3, { 1.00f, 0.85f, 0.95f } },
    { "charm_living", { 1.00f, 0.65f, 0.85f }, 0.05f, 1.0f, Kind::Wrap, 3, { 1.00f, 0.70f, 0.75f } },
    { "control_undead", { 0.95f, 0.60f, 0.85f }, 0.05f, 1.0f, Kind::Wrap, 4, { 0.75f, 0.60f, 1.00f } },
    { "control_animal", { 0.95f, 0.65f, 0.80f }, 0.05f, 1.0f, Kind::Wrap, 4, { 1.00f, 0.80f, 0.60f } },
    { "control_demon", { 0.95f, 0.55f, 0.85f }, 0.05f, 1.0f, Kind::Wrap, 4, { 1.00f, 0.45f, 0.65f } },
    { "domination", { 0.95f, 0.50f, 0.85f }, 0.05f, 1.0f, Kind::Wrap, 5, { 1.00f, 0.75f, 1.00f } },
    // §5.6 状態変化
    { "old_sleep", { 0.70f, 0.80f, 1.00f }, 0.10f, 1.0f, Kind::Sleep, 0, { 0.55f, 0.65f, 1.00f } },
    { "old_slow", { 0.55f, 0.60f, 0.70f }, 0.10f, 0.9f, Kind::ArrowDown, 0, { 0.50f, 0.58f, 0.78f } },
    { "old_speed", { 1.00f, 0.90f, 0.40f }, 0.05f, 1.1f, Kind::ArrowUp, 0, { 1.00f, 0.95f, 0.65f } },
    { "old_heal", { 0.40f, 1.00f, 0.50f }, 0.05f, 1.1f, Kind::Heal, 1, { 0.80f, 1.00f, 0.88f } },
    { "star_heal", { 0.40f, 1.00f, 0.55f }, 0.05f, 1.1f, Kind::Heal, 2, { 0.90f, 1.00f, 0.75f } },
    { "old_poly", { 0.95f, 0.95f, 0.95f }, 0.50f, 1.0f, Kind::Poly, 0, { 0.70f, 0.70f, 0.76f } },
    { "old_clone", { 0.80f, 0.90f, 1.00f }, 0.05f, 1.0f, Kind::Clone, 0, { 0.60f, 0.85f, 1.00f } },
    { "turn_undead", { 0.25f, 0.20f, 0.30f }, 0.30f, 0.6f, Kind::Fear, 0, { 0.30f, 0.45f, 0.80f } },
    { "turn_evil", { 0.25f, 0.20f, 0.30f }, 0.30f, 0.6f, Kind::Fear, 1, { 0.55f, 0.25f, 0.80f } },
    { "turn_all", { 0.25f, 0.20f, 0.30f }, 0.30f, 0.6f, Kind::Fear, 2, { 0.45f, 0.35f, 0.55f } },
    { "away_undead", { 1.00f, 1.00f, 0.80f }, 0.05f, 1.1f, Kind::Unravel, 0, { 0.85f, 0.95f, 1.00f } },
    { "away_evil", { 1.00f, 1.00f, 0.80f }, 0.05f, 1.1f, Kind::Unravel, 1, { 1.00f, 0.92f, 0.78f } },
    { "away_all", { 1.00f, 1.00f, 0.80f }, 0.05f, 1.1f, Kind::Unravel, 2, { 1.00f, 1.00f, 0.88f } },
    { "stasis", { 0.60f, 0.60f, 0.65f }, 0.10f, 0.9f, Kind::Stasis, 0, { 0.78f, 0.82f, 0.92f } },
    { "stasis_evil", { 0.60f, 0.60f, 0.65f }, 0.10f, 0.9f, Kind::Stasis, 1, { 0.72f, 0.58f, 0.90f } },
    { "disp_undead", { 1.00f, 0.95f, 0.70f }, 0.05f, 1.2f, Kind::Flash, 0, { 0.85f, 0.95f, 1.00f } },
    { "disp_evil", { 1.00f, 0.95f, 0.70f }, 0.05f, 1.2f, Kind::Flash, 0, { 1.00f, 0.92f, 0.70f } },
    { "disp_all", { 1.00f, 0.95f, 0.70f }, 0.05f, 1.2f, Kind::Flash, 0, { 1.00f, 1.00f, 0.88f } },
    { "disp_demon", { 1.00f, 0.82f, 0.60f }, 0.05f, 1.2f, Kind::Flash, 0, { 1.00f, 0.75f, 0.50f } },
    { "disp_living", { 0.85f, 1.00f, 0.80f }, 0.05f, 1.2f, Kind::Flash, 0, { 0.70f, 1.00f, 0.70f } },
    { "disp_good", { 0.85f, 0.90f, 1.00f }, 0.05f, 1.2f, Kind::Flash, 0, { 0.70f, 0.78f, 1.00f } },
    { "stun", { 0.90f, 0.85f, 0.40f }, 0.10f, 1.0f, Kind::Stars, 2, { 1.00f, 1.00f, 0.70f } },
    { "crusade", { 1.00f, 0.95f, 0.70f }, 0.05f, 1.2f, Kind::Flash, 1, { 1.00f, 0.85f, 0.45f } },
    // §5.7 地形
    { "kill_wall", { 0.55f, 0.50f, 0.40f }, 0.70f, 0.8f, Kind::Dust, 3, { 0.38f, 0.34f, 0.28f } },
    { "make_wall", { 0.55f, 0.50f, 0.40f }, 0.70f, 0.8f, Kind::Dust, 4, { 0.38f, 0.34f, 0.28f } },
    { "stone_wall", { 0.52f, 0.52f, 0.56f }, 0.70f, 0.8f, Kind::Dust, 5, { 0.36f, 0.36f, 0.40f } },
    { "kill_door", { 0.60f, 0.40f, 0.20f }, 0.40f, 0.9f, Kind::Splinters, 0, { 0.36f, 0.22f, 0.10f } },
    { "make_door", { 0.60f, 0.40f, 0.20f }, 0.40f, 0.9f, Kind::Dust, 1, { 0.42f, 0.30f, 0.16f } },
    { "jam_door", { 0.60f, 0.40f, 0.20f }, 0.40f, 0.9f, Kind::Dust, 1, { 0.42f, 0.30f, 0.16f } },
    { "kill_trap", { 0.55f, 0.50f, 0.40f }, 0.50f, 0.8f, Kind::Dust, 1, { 0.38f, 0.34f, 0.28f } },
    { "make_trap", { 0.55f, 0.50f, 0.40f }, 0.50f, 0.8f, Kind::Dust, 1, { 0.38f, 0.34f, 0.28f } },
    { "make_tree", { 0.30f, 0.70f, 0.30f }, 0.30f, 0.9f, Kind::Dust, 2, { 0.22f, 0.45f, 0.18f } },
    { "make_rune_protection", { 0.80f, 0.90f, 1.00f }, 0.05f, 1.2f, Kind::Rune, 0, { 0.55f, 0.75f, 1.00f } },
    // §5.8 特殊
    { "capture", { 0.90f, 0.80f, 0.50f }, 0.10f, 1.0f, Kind::Capture, 0, { 1.00f, 0.95f, 0.80f } },
    { "anim_dead", { 0.40f, 0.90f, 0.50f }, 0.30f, 1.0f, Kind::Rise, 0, { 0.15f, 0.50f, 0.28f } },
    { "photo", { 1.00f, 1.00f, 1.00f }, 0.00f, 1.2f, Kind::Photo, 0, { 1.00f, 1.00f, 1.00f } },
    { "engetsu", { 1.00f, 0.95f, 0.60f }, 0.05f, 1.2f, Kind::Crescent, 0, { 1.00f, 1.00f, 0.92f } },
    { "telekinesis", { 0.70f, 0.80f, 1.00f }, 0.10f, 1.0f, Kind::Lift, 0, { 0.88f, 0.93f, 1.00f } },
    { "identify", { 0.90f, 0.90f, 1.00f }, 0.05f, 1.0f, Kind::Identify, 0, { 1.00f, 1.00f, 0.85f } },
    { "genocide", { 0.60f, 0.10f, 0.10f }, 0.40f, 0.9f, Kind::Mist, 1, { 0.30f, 0.04f, 0.06f } },
    { "e_genocide", { 0.60f, 0.10f, 0.10f }, 0.40f, 0.9f, Kind::Mist, 2, { 0.35f, 0.05f, 0.22f } },
    { "hungry", { 0.70f, 0.50f, 0.30f }, 0.30f, 0.9f, Kind::Mist, 0, { 0.40f, 0.28f, 0.16f } },
    // 原作が属性の名前として運ぶだけで見た目の指定が無いもの（汎用の見た目）
    { "none", { 0.90f, 0.90f, 0.90f }, 0.10f, 1.0f, Kind::Generic, 0, { 0.9f, 0.9f, 0.9f } },
    { "debug", { 0.90f, 0.90f, 0.90f }, 0.10f, 1.0f, Kind::Generic, 0, { 0.9f, 0.9f, 0.9f } },
    { "attack", { 0.90f, 0.90f, 0.90f }, 0.10f, 1.0f, Kind::Generic, 0, { 0.9f, 0.9f, 0.9f } },
    { "player_shoot", { 0.90f, 0.90f, 0.90f }, 0.10f, 1.0f, Kind::Generic, 0, { 0.9f, 0.9f, 0.9f } },
    { "player_melee", { 0.90f, 0.90f, 0.90f }, 0.10f, 1.0f, Kind::Generic, 0, { 0.9f, 0.9f, 0.9f } },
    { "monster_shoot", { 0.90f, 0.90f, 0.90f }, 0.10f, 1.0f, Kind::Generic, 0, { 0.9f, 0.9f, 0.9f } },
    { "monster_melee", { 0.90f, 0.90f, 0.90f }, 0.10f, 1.0f, Kind::Generic, 0, { 0.9f, 0.9f, 0.9f } },
};

const Entry *entry_of(const FxSpec &spec)
{
    for (const Entry &e : kEntries) {
        if (spec.attr == e.attr) {
            return &e;
        }
    }
    return nullptr;
}

/* ============================================================ 小さな道具 */

//! 見た目 1 つ。`te` は始まりからの秒。`scale` は大きさ（周りのマスの小さな版は 0.62）。
struct Look {
    const FxSpec &spec;
    const Entry &e;
    int gx;
    int gy;
    Vec3 foot; //!< マスの中心（高さ 0）
    //! 体の前（画面のほうへ少し出した位置。高さ 0）。厚みのある実体や壁に隠れないよう、体の中心に出す煙や粒はここへ置く。
    Vec3 front;
    float te;
    float scale;
    bool mini; //!< 範囲の周りのマスに出す小さな版（数を減らす）
    std::uint32_t seed;
};

Look make_look(const FxSpec &spec, const Entry &e, int gx, int gy, float te, float scale, bool mini, std::uint32_t seed, float forward = 0.22f)
{
    const Vec3 toward = fx_toward_camera(spec);
    Vec3 flat{ toward.x, toward.y, 0.f };
    const float len = std::sqrt(dot(flat, flat));
    flat = (len < 1e-4f) ? Vec3{ 0.f, 1.f, 0.f } : (flat * (1.f / len));
    const Vec3 foot = fx_cell_centre(gx, gy, 0.f);
    return Look{ spec, e, gx, gy, foot, foot + (flat * forward), te, scale, mini, seed };
}

int count_of(const Look &L, int n)
{
    return L.mini ? std::max(2, n / 4) : n;
}

float srgb_to_linear(float c)
{
    return std::pow(std::max(c, 0.f), 2.2f);
}

FxColour lerp_colour(const FxColour &a, const FxColour &b, float t)
{
    return fx_mix(a, b, fx_clamp01(t));
}

//! 立方体 1 つ。
void put(FxFrame &out, const Vec3 &pos, float size, const FxColour &c, float emissive, float alpha, FxPart part = FxPart::Particle)
{
    if (alpha <= 0.004f) {
        return;
    }
    FxVoxel v;
    v.center = pos;
    v.size = Vec3{ size, size, size };
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.emissive = emissive;
    v.alpha = std::min(alpha, 1.f);
    v.part = part;
    out.voxels.push_back(v);
}

//! 向きのある細長い立方体（長さ `len`、太さ `w`）。向きは進む向き `dir`（単位ベクトル）。
void put_bar(FxFrame &out, const Vec3 &centre, const Vec3 &dir, float len, float w, const FxColour &c, float emissive, float alpha,
    FxPart part = FxPart::Particle)
{
    if (alpha <= 0.004f) {
        return;
    }
    FxVoxel v;
    v.center = centre;
    v.size = Vec3{ len, w, w };
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.emissive = emissive;
    v.alpha = std::min(alpha, 1.f);
    fx_direction_angles(dir, v.yaw, v.pitch);
    v.part = part;
    out.voxels.push_back(v);
}

Vec3 on_screen(const FxSpec &spec, float x, float y)
{
    return (spec.cam_right * x) + (spec.cam_up * y);
}

void add_light(FxFrame &out, const Vec3 &p, float radius, const FxColour &c, float intensity)
{
    if (intensity <= 0.01f) {
        return;
    }
    PointLight l;
    l.x = p.x;
    l.y = p.y;
    l.z = p.z;
    l.radius = radius;
    l.r = c.r;
    l.g = c.g;
    l.b = c.b;
    l.intensity = intensity;
    out.lights.push_back(l);
}

float ease_out(float u)
{
    u = fx_clamp01(u);
    return 1.f - ((1.f - u) * (1.f - u));
}

//! 文字の格子（行の並び）から形を作る。`#` は本体、`o` は縁。
FxShape shape_of(const std::vector<std::string> &rows)
{
    FxShape s;
    s.rows = rows;
    s.height = static_cast<int>(rows.size());
    for (const std::string &r : rows) {
        s.width = std::max(s.width, static_cast<int>(r.size()));
    }
    return s;
}

//! `#` のうち、隣に空きがあるものを `o`（縁）にする。
void mark_outline(std::vector<std::string> &g)
{
    const std::vector<std::string> src = g;
    const int h = static_cast<int>(src.size());
    for (int y = 0; y < h; ++y) {
        const int w = static_cast<int>(src[static_cast<std::size_t>(y)].size());
        for (int x = 0; x < w; ++x) {
            if (src[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != '#') {
                continue;
            }
            const auto empty_at = [&](int xx, int yy) {
                return (yy < 0) || (yy >= h) || (xx < 0) || (xx >= static_cast<int>(src[static_cast<std::size_t>(yy)].size()))
                    || (src[static_cast<std::size_t>(yy)][static_cast<std::size_t>(xx)] == '.');
            };
            if (empty_at(x - 1, y) || empty_at(x + 1, y) || empty_at(x, y - 1) || empty_at(x, y + 1)) {
                g[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 'o';
            }
        }
    }
}

void fill_rect(std::vector<std::string> &g, int x0, int y0, int x1, int y1, char ch)
{
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if ((y >= 0) && (y < static_cast<int>(g.size())) && (x >= 0) && (x < static_cast<int>(g[static_cast<std::size_t>(y)].size()))) {
                g[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = ch;
            }
        }
    }
}

//! 開いた手（指を上へ）と握った手。
const FxShape &hand_shape(bool fist)
{
    static const FxShape open = [] {
        std::vector<std::string> g(17, std::string(19, '.'));
        const int lens[4] = { 6, 8, 9, 7 };
        const int palm_top = 9;
        fill_rect(g, 3, palm_top, 17, 16, '#');
        for (int i = 0; i < 4; ++i) {
            const int x0 = 3 + (i * 4);
            fill_rect(g, x0, palm_top - lens[i], x0 + 2, palm_top, '#');
        }
        fill_rect(g, 1, 11, 3, 14, '#'); //!< 親指
        fill_rect(g, 0, 9, 2, 12, '#');
        fill_rect(g, 3, 16, 4, 16, '.');
        fill_rect(g, 16, 16, 17, 16, '.');
        mark_outline(g);
        return shape_of(g);
    }();
    static const FxShape closed = [] {
        std::vector<std::string> g(11, std::string(13, '.'));
        fill_rect(g, 1, 3, 11, 9, '#');
        fill_rect(g, 1, 1, 2, 3, '#');
        fill_rect(g, 4, 1, 5, 3, '#');
        fill_rect(g, 7, 1, 8, 3, '#');
        fill_rect(g, 10, 1, 11, 3, '#');
        fill_rect(g, 1, 9, 1, 9, '.');
        fill_rect(g, 11, 9, 11, 9, '.');
        fill_rect(g, 0, 4, 0, 7, '#');
        mark_outline(g);
        for (int x : { 3, 6, 9 }) {
            fill_rect(g, x, 3, x, 5, 'o');
        }
        fill_rect(g, 2, 6, 9, 6, 'o');
        return shape_of(g);
    }();
    return fist ? closed : open;
}

//! 三日月（開いた側が +x。画面の右）。
const FxShape &crescent_shape()
{
    static const FxShape s = [] {
        std::vector<std::string> g(15, std::string(15, '.'));
        for (int y = 0; y < 15; ++y) {
            for (int x = 0; x < 15; ++x) {
                const float dx = static_cast<float>(x) - 7.f;
                const float dy = static_cast<float>(y) - 7.f;
                const float ex = static_cast<float>(x) - 11.2f;
                if (((dx * dx) + (dy * dy) <= 7.3f * 7.3f) && ((ex * ex) + (dy * dy) > 6.1f * 6.1f)) {
                    g[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '#';
                }
            }
        }
        mark_outline(g);
        return shape_of(g);
    }();
    return s;
}

const FxShape &star_shape()
{
    static const FxShape s = fx_shape({ "..#..", ".###.", "#####", ".###.", ".#.#." });
    return s;
}

const FxShape &arrow_down_shape()
{
    static const FxShape s = fx_shape({ "...###...", "...###...", "...###...", "...###...", "#########", ".#######.", "..#####..", "...###...", "....#...." });
    return s;
}

const FxShape &arrow_up_shape()
{
    static const FxShape s = fx_shape({ "....#....", "...###...", "..#####..", ".#######.", "#########", "...###...", "...###...", "...###...", "...###..." });
    return s;
}

const FxShape &cross_shape()
{
    static const FxShape s = fx_shape({ "..#..", "..#..", "#####", "..#..", "..#..", "..#..", "..#.." });
    return s;
}

const FxShape &skull_shape()
{
    static const FxShape s = fx_shape({ ".#####.", "#######", "##...##", "##.#.##", "#######", ".##.##.", ".#####.", ".#.#.#." });
    return s;
}

/* ============================================================ 見た目の種類ごとの絵 */

void look_skull(const Look &L, FxFrame &out)
{
    static const float sizes[] = { 0.f, 0.40f, 0.52f, 0.68f, 0.92f };
    const int lv = std::min(std::max(L.e.level, 1), 4);
    const float alpha = fx_envelope(L.te, 0.25f, 0.5f, 0.35f);
    const float size = sizes[lv] * L.scale;
    const float u = fx_clamp01(L.te / 1.1f);
    const Vec3 c{ L.foot.x, L.foot.y, 0.95f + (size * 0.5f) + (0.3f * u * L.scale) };
    const float emissive = 0.45f + (0.2f * static_cast<float>(lv));
    if (alpha > 0.01f) {
        FxSprite sprite;
        sprite.name = "skull";
        sprite.centre = c;
        sprite.size = size;
        sprite.tint = L.e.tint;
        sprite.alpha = alpha;
        sprite.emissive = emissive;
        sprite.resolution = L.mini ? 9 : (12 + (lv * 3));
        if (fx_emit_sprite(L.spec, sprite, out) == 0) {
            // 絵が読めない配布版でも出す（文字の格子のどくろ）。
            FxShapePlace place;
            place.centre = c;
            place.right = L.spec.cam_right;
            place.up = L.spec.cam_up;
            place.cell = size / 7.f;
            place.alpha = alpha;
            place.emissive = emissive;
            FxShapeColours colours;
            colours.base = fx_mix(L.e.tint, FxColour{ 0.9f, 0.85f, 0.7f }, 0.35f);
            (void)fx_emit_shape(skull_shape(), colours, place, out);
        }
    }
    // どくろの下から立つ暗い靄（段階が上がるほど多く）。
    FxSmoke smoke;
    smoke.origin = Vec3{ L.foot.x, L.foot.y, 0.45f };
    smoke.radius = 0.2f * L.scale;
    smoke.born = 0.f;
    smoke.duration = 0.45f;
    smoke.life = 0.8f;
    smoke.count = count_of(L, 2 + (lv * 2));
    smoke.rise = 0.35f;
    smoke.size = 0.10f * L.scale;
    smoke.grow = 1.8f;
    smoke.colour = fx_scale(L.e.tint, 0.28f);
    smoke.colour_end = FxColour{ 0.04f, 0.02f, 0.05f };
    smoke.alpha = 0.45f;
    smoke.emissive = 0.2f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, L.te, out);
}

void look_hand(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float clench = fx_ramp(te, 0.62f, 0.85f);
    const bool fist = L.mini || (te >= 0.66f); //!< 周りのマスは握った手だけ（数を抑える）
    const float appear = fx_ramp(te, 0.f, 0.22f);
    const float release = 1.f - fx_ramp(te, 1.05f, 1.45f);
    const float alpha = appear * release;
    const float z_open = fx_lerp(2.2f, 1.35f, ease_out(te / 0.38f));
    const float z = fx_lerp(z_open, 0.78f, clench);
    FxShapePlace place;
    place.right = L.spec.cam_right;
    place.up = L.spec.cam_up;
    place.centre = Vec3{ L.foot.x, L.foot.y, z };
    place.cell = 0.062f;
    place.scale = L.scale * fx_lerp(1.f, 0.62f, clench);
    place.layers = L.mini ? 1 : 3;
    place.depth = 0.045f;
    place.alpha = alpha;
    place.emissive = 0.55f;
    // 握る間の震え。
    if (fist && (te < 1.05f)) {
        place.centre.x += std::sin(te * 130.f) * 0.012f;
    }
    FxShapeColours colours;
    colours.base = FxColour{ 0.045f, 0.02f, 0.07f };
    colours.keys = "o";
    colours.colours = { fx_scale(L.e.tint, 0.8f) };
    if (alpha > 0.01f) {
        (void)fx_emit_shape(hand_shape(fist), colours, place, out);
    }
    // 握りつぶされる対象。
    FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
    const float squash = 0.55f * clench * (1.f - fx_ramp(te, 1.0f, 1.3f));
    mod.squash = std::max(mod.squash, squash);
    if (squash > 0.02f) {
        mod.tremble = std::max(mod.tremble, 0.02f);
        mod.tremble_phase = te * 120.f;
    }
    // 握ったあとに立つ黒い煙。
    FxSmoke smoke;
    smoke.origin = Vec3{ L.foot.x, L.foot.y, 0.5f };
    smoke.radius = 0.22f;
    smoke.born = 0.7f;
    smoke.duration = 0.15f;
    smoke.life = 0.6f;
    smoke.count = count_of(L, 8);
    smoke.rise = 0.5f;
    smoke.size = 0.13f * L.scale;
    smoke.grow = 2.f;
    smoke.colour = FxColour{ 0.12f, 0.04f, 0.18f };
    smoke.colour_end = FxColour{ 0.03f, 0.01f, 0.05f };
    smoke.alpha = 0.55f;
    smoke.emissive = 0.15f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, te, out);
}

//! 撃った者への流れ（生命吸収・精神吸収・魔力吸収）。
void look_drain(const Look &L, FxFrame &out)
{
    const int n = count_of(L, 34);
    const Vec3 dst = fx_source_centre(L.spec, 0.6f);
    const float flight = 0.5f;
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = 0.45f * static_cast<float>(i) / static_cast<float>(n);
        const float age = L.te - born;
        if ((age < 0.f) || (age >= flight)) {
            continue;
        }
        const float u = age / flight;
        const float phi = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float rr = (0.08f + (0.17f * fx_rand(L.seed, id, 2u))) * L.scale;
        const Vec3 start{ L.foot.x + (std::cos(phi) * rr), L.foot.y + (std::sin(phi) * rr), (0.12f + (0.65f * fx_rand(L.seed, id, 3u))) * L.scale };
        const Vec3 d = dst - start;
        Vec3 side = cross(d, Vec3{ 0.f, 0.f, 1.f });
        side = (dot(side, side) < 1e-6f) ? Vec3{ 1.f, 0.f, 0.f } : normalize(side);
        const float bend = ((fx_rand(L.seed, id, 4u) * 2.f) - 1.f) * 0.5f;
        const auto at = [&](float k) {
            const float e = std::pow(fx_clamp01(k), 1.7f);
            Vec3 p = start + (d * e);
            p = p + (side * (bend * std::sin(kPi * fx_clamp01(k))));
            p.z += 0.25f * std::sin(kPi * fx_clamp01(k));
            return p;
        };
        const FxColour col = lerp_colour(L.e.base, L.e.tint, u);
        const float fade = 1.f - fx_ramp(u, 0.82f, 1.f);
        const float size = 0.095f * (1.f - (0.45f * u)) * L.scale;
        put(out, at(u), size, col, 2.2f, fade);
        put(out, at(u - 0.09f), size * 0.7f, fx_scale(col, 0.7f), 1.3f, 0.45f * fade);
    }
    // 吸い込む側の脈動。
    for (int k = 0; k < (L.mini ? 1 : 3); ++k) {
        FxRing ring;
        ring.centre = dst;
        ring.normal = fx_toward_camera(L.spec);
        ring.r0 = 0.06f;
        ring.r1 = 0.42f;
        ring.born = 0.55f + (0.15f * static_cast<float>(k));
        ring.life = 0.24f;
        ring.thickness = 0.03f;
        ring.segments = 18;
        ring.colour = L.e.tint;
        ring.emissive = 2.f;
        ring.alpha = 0.8f;
        (void)fx_emit_ring(ring, L.te, out);
    }
}

void look_ripple(const Look &L, FxFrame &out)
{
    const int n = L.mini ? 1 : std::max(2, L.e.level);
    const Vec3 centre{ L.foot.x, L.foot.y, (L.mini ? 0.8f : 1.05f) };
    for (int i = 0; i < n; ++i) {
        FxRing ring;
        ring.centre = centre;
        ring.normal = fx_toward_camera(L.spec);
        ring.r0 = 0.68f * L.scale;
        ring.r1 = 0.07f;
        ring.born = 0.13f * static_cast<float>(i);
        ring.life = 0.5f;
        ring.thickness = (L.e.level >= 4 ? 0.045f : 0.032f);
        ring.segments = 26;
        ring.colour = (i % 2 == 0) ? L.e.base : L.e.tint;
        ring.emissive = 1.45f;
        ring.alpha = 0.95f;
        ring.dash = (L.e.level == 3) ? 2 : 1;
        ring.spin = (L.e.level == 3) ? (static_cast<float>(i) * 0.5f) : 0.f;
        ring.fade_out = true;
        (void)fx_emit_ring(ring, L.te, out);
    }
    if ((L.e.level >= 4) && !L.mini) {
        // 脳を砕く：輪へ向かって内側へ吸い込まれる粒。
        for (int i = 0; i < 12; ++i) {
            const std::uint32_t id = static_cast<std::uint32_t>(i);
            const float born = 0.04f * static_cast<float>(i % 6);
            const float age = L.te - born;
            if ((age < 0.f) || (age >= 0.5f)) {
                continue;
            }
            const float u = age / 0.5f;
            const float ang = (2.f * kPi * static_cast<float>(i) / 12.f) + (0.4f * fx_rand(L.seed, id));
            const float r = fx_lerp(0.75f, 0.06f, u * u);
            put(out, centre + on_screen(L.spec, std::cos(ang) * r, std::sin(ang) * r), 0.045f, L.e.tint, 2.f, 1.f - u);
        }
    }
}

void look_stars(const Look &L, FxFrame &out)
{
    const bool stun = (L.e.level >= 2);
    const int n = count_of(L, stun ? 6 : 5);
    const float alpha = fx_envelope(L.te, 0.12f, 0.65f, 0.25f);
    if (alpha <= 0.01f) {
        return;
    }
    const float radius = 0.33f * L.scale;
    for (int i = 0; i < n; ++i) {
        const float ang = (2.f * kPi * static_cast<float>(i) / static_cast<float>(n)) + (L.te * (stun ? 8.5f : 5.5f));
        FxShapePlace place;
        place.right = L.spec.cam_right;
        place.up = L.spec.cam_up;
        place.centre = Vec3{ L.foot.x + (std::cos(ang) * radius), L.foot.y + (std::sin(ang) * radius * 0.85f),
            (L.mini ? 0.85f : 1.05f) + (0.045f * std::sin((L.te * 9.f) + static_cast<float>(i))) };
        place.cell = 0.03f * L.scale;
        place.alpha = alpha;
        place.emissive = 2.6f;
        FxShapeColours colours;
        colours.base = (i % 2 == 0) ? L.e.base : L.e.tint;
        (void)fx_emit_shape(star_shape(), colours, place, out);
    }
    if (stun) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.025f * alpha);
        mod.tremble_phase = L.te * 100.f;
    }
}

void look_wrap(const Look &L, FxFrame &out)
{
    const int n = L.mini ? 2 : std::max(3, L.e.level);
    for (int i = 0; i < n; ++i) {
        FxRing ring;
        const float k = static_cast<float>(i) / static_cast<float>(std::max(n - 1, 1));
        ring.centre = Vec3{ L.foot.x, L.foot.y, (0.08f + (0.8f * k)) * L.scale };
        ring.normal = Vec3{ 0.f, 0.f, 1.f };
        ring.r0 = 0.62f * L.scale;
        ring.r1 = 0.13f;
        ring.born = 0.07f * static_cast<float>(i);
        ring.life = 0.56f;
        ring.thickness = 0.034f;
        ring.segments = 24;
        ring.colour = (i % 2 == 0) ? L.e.base : L.e.tint;
        ring.emissive = 1.5f;
        ring.alpha = 0.95f;
        ring.spin = 0.7f * static_cast<float>(i);
        (void)fx_emit_ring(ring, L.te, out);
    }
    // 締まりきったところの小さな光の散り。
    FxSpray spray;
    spray.origin = Vec3{ L.foot.x, L.foot.y, 0.45f * L.scale };
    spray.cone = kPi;
    spray.speed_min = 0.3f;
    spray.speed_max = 0.8f;
    spray.gravity = 0.f;
    spray.drag = 3.f;
    spray.born = 0.55f;
    spray.life_min = 0.18f;
    spray.life_max = 0.3f;
    spray.count = count_of(L, 10);
    spray.colour0 = L.e.tint;
    spray.colour1 = L.e.base;
    spray.size0 = 0.045f * L.scale;
    spray.size1 = 0.01f;
    spray.emissive0 = 2.4f;
    spray.emissive1 = 0.5f;
    spray.seed = L.seed;
    (void)fx_emit_spray(spray, L.te, out);
}

void look_sleep(const Look &L, FxFrame &out)
{
    FxSpray spray;
    spray.origin = Vec3{ L.front.x, L.front.y, 1.5f * L.scale };
    spray.dir = Vec3{ 0.f, 0.f, -1.f };
    spray.cone = 0.3f;
    spray.scatter = 0.3f * L.scale;
    spray.speed_min = 0.2f;
    spray.speed_max = 0.45f;
    spray.gravity = -0.35f;
    spray.drag = 0.3f;
    spray.born = 0.f;
    spray.duration = 0.7f;
    spray.life_min = 0.8f;
    spray.life_max = 1.1f;
    spray.count = count_of(L, 34);
    spray.colour0 = L.e.base;
    spray.colour1 = L.e.tint;
    spray.size0 = 0.075f * L.scale;
    spray.size1 = 0.04f;
    spray.emissive0 = 2.0f;
    spray.emissive1 = 0.6f;
    spray.alpha0 = 0.95f; //!< 静かに現れ、静かに薄れて消える
    spray.alpha1 = 0.f;
    spray.seed = L.seed;
    (void)fx_emit_spray(spray, L.te, out);
    // 頭の上のやわらかな光。
    const float glow = fx_envelope(L.te, 0.25f, 0.6f, 0.5f);
    if (!L.mini && (glow > 0.02f)) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 1.2f }, 2.6f, L.e.base, 0.9f * glow);
    }
}

void look_arrow(const Look &L, FxFrame &out, bool down)
{
    const int n = L.mini ? 1 : (down ? 2 : 3);
    for (int i = 0; i < n; ++i) {
        const float born = (down ? 0.2f : 0.1f) * static_cast<float>(i);
        const float life = down ? 0.62f : 0.5f;
        const float age = L.te - born;
        if ((age < 0.f) || (age >= life)) {
            continue;
        }
        const float u = age / life;
        FxShapePlace place;
        place.right = L.spec.cam_right;
        place.up = L.spec.cam_up;
        const float z0 = down ? 1.95f : 0.05f;
        const float z1 = down ? 0.95f : 1.25f;
        place.centre = Vec3{ L.front.x, L.front.y, fx_lerp(z0, z1, down ? u : ease_out(u)) * L.scale } + (L.spec.cam_right * (((i == 1) ? -0.16f : ((i == 2) ? 0.16f : 0.f)) * L.scale));
        place.cell = (down ? 0.05f : 0.04f) * L.scale * ((i == 0) ? 1.f : 0.8f);
        place.alpha = fx_envelope(age, 0.12f, life - 0.3f, 0.18f);
        place.emissive = 1.5f;
        FxShapeColours colours;
        colours.base = (i == 0) ? L.e.base : L.e.tint;
        if (place.alpha > 0.01f) {
            (void)fx_emit_shape(down ? arrow_down_shape() : arrow_up_shape(), colours, place, out);
        }
    }
    if (!down) {
        // 速さの火の粉。
        FxSpray spray;
        spray.origin = Vec3{ L.front.x, L.front.y, 0.1f };
        spray.dir = Vec3{ 0.f, 0.f, 1.f };
        spray.cone = 0.3f;
        spray.scatter = 0.25f * L.scale;
        spray.speed_min = 1.2f;
        spray.speed_max = 2.2f;
        spray.gravity = 0.f;
        spray.drag = 1.5f;
        spray.born = 0.05f;
        spray.duration = 0.3f;
        spray.life_min = 0.25f;
        spray.life_max = 0.4f;
        spray.count = count_of(L, 12);
        spray.colour0 = L.e.tint;
        spray.colour1 = L.e.base;
        spray.size0 = 0.04f;
        spray.size1 = 0.012f;
        spray.emissive0 = 2.4f;
        spray.emissive1 = 0.8f;
        spray.seed = L.seed;
        (void)fx_emit_spray(spray, L.te, out);
    }
}

void look_heal(const Look &L, FxFrame &out)
{
    const bool star = (L.e.level >= 2);
    FxSpray spray;
    spray.origin = Vec3{ L.front.x, L.front.y, 0.03f };
    spray.dir = Vec3{ 0.f, 0.f, 1.f };
    spray.cone = 0.28f;
    spray.scatter = 0.3f * L.scale;
    spray.speed_min = 0.45f;
    spray.speed_max = 1.0f;
    spray.gravity = 0.5f;
    spray.drag = 0.4f;
    spray.born = 0.f;
    spray.duration = 0.7f;
    spray.life_min = 0.8f;
    spray.life_max = 1.1f;
    spray.count = count_of(L, star ? 70 : 44);
    spray.colour0 = L.e.base;
    spray.colour1 = L.e.tint;
    spray.size0 = (star ? 0.085f : 0.07f) * L.scale;
    spray.size1 = 0.015f;
    spray.emissive0 = 2.2f;
    spray.emissive1 = 0.8f;
    spray.seed = L.seed;
    (void)fx_emit_spray(spray, L.te, out);
    for (int k = 0; k < (star && !L.mini ? 2 : 1); ++k) {
        FxRing ring;
        ring.centre = Vec3{ L.foot.x, L.foot.y, 0.03f };
        ring.r0 = 0.12f;
        ring.r1 = star ? 0.62f : 0.46f;
        ring.born = 0.2f * static_cast<float>(k);
        ring.life = 0.55f;
        ring.thickness = 0.03f;
        ring.segments = 22;
        ring.colour = L.e.base;
        ring.emissive = 1.8f;
        ring.alpha = 0.8f;
        (void)fx_emit_ring(ring, L.te, out);
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.6f }, 3.f, L.e.base, (star ? 1.8f : 1.2f) * fx_envelope(L.te, 0.15f, 0.5f, 0.6f));
    }
}

void look_poly(const Look &L, FxFrame &out)
{
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, 0.5f * L.scale };
    smoke.radius = 0.16f * L.scale;
    smoke.born = 0.f;
    smoke.duration = 0.1f;
    smoke.life = 0.7f;
    smoke.count = count_of(L, 13);
    smoke.rise = 0.3f;
    smoke.size = 0.2f * L.scale;
    smoke.grow = 2.4f;
    smoke.colour = L.e.base;
    smoke.colour_end = L.e.tint;
    smoke.alpha = 0.85f;
    smoke.emissive = 0.6f;
    smoke.drift_x = 0.f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, L.te, out);
    FxSpray spray;
    spray.origin = Vec3{ L.front.x, L.front.y, 0.5f * L.scale };
    spray.cone = kPi;
    spray.speed_min = 1.f;
    spray.speed_max = 2.2f;
    spray.gravity = -1.f;
    spray.drag = 3.f;
    spray.born = 0.f;
    spray.life_min = 0.2f;
    spray.life_max = 0.35f;
    spray.count = count_of(L, 10);
    spray.colour0 = FxColour{ 1.f, 1.f, 1.f };
    spray.colour1 = L.e.base;
    spray.size0 = 0.05f;
    spray.size1 = 0.015f;
    spray.emissive0 = 2.4f;
    spray.emissive1 = 0.4f;
    spray.seed = L.seed + 7u;
    (void)fx_emit_spray(spray, L.te, out);
    // ぽんと弾ける一瞬の閃光。
    if (L.te < 0.06f) {
        put(out, Vec3{ L.front.x, L.front.y, 0.5f * L.scale }, ((0.2f * (1.f - (L.te / 0.06f))) + 0.08f) * L.scale, FxColour{ 1.f, 1.f, 1.f }, 2.4f, 0.8f);
    }
}

void look_clone(const Look &L, FxFrame &out)
{
    FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
    const float env = fx_envelope(L.te, 0.08f, 0.22f, 0.14f);
    mod.echo_x = std::max(mod.echo_x, 0.3f * env * L.scale);
    mod.echo_y = std::max(mod.echo_y, 0.04f * env * L.scale);
    // 二重に見える合図：ずれる 2 つの像の間を、細かな光が走る。
    const int n = count_of(L, 10);
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = 0.05f + (0.18f * fx_rand(L.seed, id, 1u));
        const float age = L.te - born;
        if ((age < 0.f) || (age >= 0.25f)) {
            continue;
        }
        const float u = age / 0.25f;
        const float h = (0.08f + (0.7f * fx_rand(L.seed, id, 2u))) * L.scale;
        const float x = fx_lerp(-0.05f, 0.3f, u);
        put(out, Vec3{ L.front.x, L.front.y, h } + (L.spec.cam_right * x), 0.05f, (i % 2 == 0) ? L.e.base : L.e.tint, 2.f, 1.f - u);
    }
}

void look_fear(const Look &L, FxFrame &out)
{
    const float alpha = fx_envelope(L.te, 0.15f, 0.7f, 0.3f);
    if (alpha <= 0.01f) {
        return;
    }
    const int n = count_of(L, 12);
    const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(L.te * 25.f));
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float ang = (2.f * kPi * static_cast<float>(i) / static_cast<float>(n)) + (L.te * 1.6f) + (fx_rand(L.seed, id, 1u) * 0.8f);
        const float z = (0.06f + (0.82f * fx_rand(L.seed, id, 2u))) * L.scale;
        const float r = (0.25f + (0.05f * fx_rand(L.seed, id, 3u))) * L.scale;
        const float jx = (fx_rand(L.seed, id, frame, 4u) - 0.5f) * 0.05f;
        const float jy = (fx_rand(L.seed, id, frame, 5u) - 0.5f) * 0.05f;
        const float jz = (fx_rand(L.seed, id, frame, 6u) - 0.5f) * 0.05f;
        const Vec3 p{ L.foot.x + (std::cos(ang) * r) + jx, L.foot.y + (std::sin(ang) * r * 0.9f) + jy, z + jz };
        const float size = (0.11f + (0.07f * fx_rand(L.seed, id, 7u))) * L.scale;
        // 縁取り（淡く光る）と、黒い影。
        put(out, p, size * 1.45f, L.e.tint, 0.55f, 0.35f * alpha);
        put(out, p, size, FxColour{ 0.02f, 0.012f, 0.03f }, 0.f, 0.85f * alpha);
    }
    FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
    mod.tremble = std::max(mod.tremble, 0.03f * alpha);
    mod.tremble_phase = L.te * 110.f;
}

void look_unravel(const Look &L, FxFrame &out)
{
    FxTileImage img;
    const bool have = L.spec.tile_at && L.spec.tile_at(L.gx, L.gy, img) && (img.width > 0) && (img.height > 0)
        && (img.rgba.size() >= (static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4u));
    if (!have && L.mini) {
        return; //!< 何もいないマスからはほどけない
    }
    const int n = L.mini ? 6 : 16;
    const int m = L.mini ? 7 : 20;
    const float wc = have ? img.width_cells : 0.8f;
    const float hc = have ? img.height_cells : 0.8f;
    const Vec3 toward = fx_toward_camera(L.spec) * 0.1f;
    const float cell = (wc / static_cast<float>(n)) * 1.15f;
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < n; ++i) {
            const float fx = ((static_cast<float>(i) + 0.5f) / static_cast<float>(n)) - 0.5f;
            const float fz = (static_cast<float>(m - j) - 0.5f) / static_cast<float>(m);
            FxColour src{ 0.9f, 0.9f, 0.9f };
            if (have) {
                const int px = std::min(img.width - 1, ((i * 2) + 1) * img.width / (n * 2));
                const int py = std::min(img.height - 1, ((j * 2) + 1) * img.height / (m * 2));
                const std::size_t k = ((static_cast<std::size_t>(py) * static_cast<std::size_t>(img.width)) + static_cast<std::size_t>(px)) * 4u;
                if (img.rgba[k + 3] < 100) {
                    continue;
                }
                src = FxColour{ srgb_to_linear(static_cast<float>(img.rgba[k]) / 255.f), srgb_to_linear(static_cast<float>(img.rgba[k + 1]) / 255.f),
                    srgb_to_linear(static_cast<float>(img.rgba[k + 2]) / 255.f) };
            } else {
                const float ex = fx / 0.4f;
                const float ez = (fz - 0.5f) / 0.5f;
                if (((ex * ex) + (ez * ez)) > 1.f) {
                    continue;
                }
            }
            const std::uint32_t id = static_cast<std::uint32_t>((j * 32) + i);
            // 上から先にほどける。
            const float trigger = (0.4f * (1.f - fz)) + (0.15f * fx_rand(L.seed, id, 1u));
            FxParticle p;
            p.pos = L.foot + toward + (L.spec.cam_right * (fx * wc)) + Vec3{ 0.f, 0.f, (fz * hc) + 0.02f };
            p.vel = Vec3{ (fx_rand(L.seed, id, 2u) - 0.5f) * 0.4f, (fx_rand(L.seed, id, 3u) - 0.5f) * 0.4f, 0.25f + (0.45f * fx_rand(L.seed, id, 4u)) }
                + (L.spec.cam_right * (fx * 0.9f));
            p.gravity = 0.45f;
            p.drag = 0.9f;
            p.born = trigger;
            p.life = 0.65f + (0.4f * fx_rand(L.seed, id, 5u));
            p.colour0 = fx_mix(src, L.e.base, 0.65f);
            p.colour1 = L.e.tint;
            p.size0 = cell;
            p.size1 = 0.012f;
            p.emissive0 = 3.f;
            p.emissive1 = 0.8f;
            p.alpha0 = 1.f;
            p.alpha1 = 0.f;
            (void)fx_emit_particle(p, L.te, out);
        }
    }
}

void look_stasis(const Look &L, FxFrame &out)
{
    const float te = L.te;
    FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
    mod.grey = std::max(mod.grey, 0.88f * fx_envelope(te, 0.22f, 0.85f, 0.4f));
    const float alpha = fx_envelope(te, 0.12f, 1.0f, 0.38f);
    if (alpha <= 0.01f) {
        return;
    }
    const FxColour silver = L.e.tint;
    const Vec3 c{ L.foot.x, L.foot.y, 0.45f * L.scale };
    const Vec3 normal = fx_toward_camera(L.spec);
    const float r = 0.62f * L.scale;
    // 文字盤の輪。
    FxRing ring;
    ring.centre = c;
    ring.normal = normal;
    ring.r0 = r;
    ring.r1 = r;
    ring.born = 0.f;
    ring.life = 1.6f;
    ring.thickness = 0.028f;
    ring.segments = L.mini ? 14 : 28;
    ring.colour = silver;
    ring.emissive = 1.6f;
    ring.alpha = alpha;
    ring.fade_out = false;
    (void)fx_emit_ring(ring, te, out);
    if (L.mini) {
        return;
    }
    // 12 の目盛り。
    for (int i = 0; i < 12; ++i) {
        const float ang = 2.f * kPi * static_cast<float>(i) / 12.f;
        const float rr = r * ((i % 3 == 0) ? 0.84f : 0.92f);
        put(out, c + on_screen(L.spec, std::sin(ang) * rr, std::cos(ang) * rr), (i % 3 == 0) ? 0.055f : 0.035f, silver, 1.8f, alpha);
    }
    // 針：勢いよく回って、だんだん遅くなって止まる。
    const float spin = ease_out(te / 0.85f) * 2.f * kPi * 1.3f;
    const float hands[2] = { spin, spin / 12.f };
    const float lens[2] = { 0.46f * L.scale, 0.3f * L.scale };
    for (int h = 0; h < 2; ++h) {
        const int steps = static_cast<int>(lens[h] / 0.06f);
        for (int k = 1; k <= steps; ++k) {
            const float rr = static_cast<float>(k) * 0.06f;
            put(out, c + on_screen(L.spec, std::sin(hands[h]) * rr, std::cos(hands[h]) * rr), (h == 0) ? 0.035f : 0.045f, silver, 1.8f, alpha);
        }
    }
    put(out, c, 0.06f, silver, 2.f, alpha);
}

void look_flash(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 c{ L.front.x, L.front.y, 0.5f * L.scale };
    if (te < 0.3f) {
        const float u = te / 0.3f;
        put(out, c, ((0.45f * (1.f - u)) + 0.08f) * L.scale, fx_mix(FxColour{ 1.f, 1.f, 1.f }, L.e.base, 0.4f), 4.f, 1.f - u);
    }
    const int n = L.mini ? 5 : 8;
    if (te < 0.4f) {
        for (int i = 0; i < n; ++i) {
            const float ang = (2.f * kPi * static_cast<float>(i) / static_cast<float>(n)) + 0.2f;
            const Vec3 d = on_screen(L.spec, std::cos(ang), std::sin(ang));
            const float u = te / 0.4f;
            const float r = fx_lerp(0.1f, 0.78f * L.scale, ease_out(te / 0.35f));
            const float len = 0.32f * L.scale * (1.f - u);
            put_bar(out, c + (d * (r + (len * 0.5f))), d, len, 0.04f * L.scale, (i % 2 == 0) ? L.e.base : L.e.tint, 3.f, 1.f - (u * u));
        }
    }
    FxRing ring;
    ring.centre = c;
    ring.normal = fx_toward_camera(L.spec);
    ring.r0 = 0.1f;
    ring.r1 = 0.88f * L.scale;
    ring.life = 0.4f;
    ring.thickness = 0.03f;
    ring.segments = 24;
    ring.colour = L.e.base;
    ring.emissive = 2.6f;
    ring.alpha = 0.9f;
    (void)fx_emit_ring(ring, te, out);
    if (!L.mini) {
        add_light(out, c, 4.2f, L.e.base, 3.2f * fx_envelope(te, 0.04f, 0.1f, 0.36f));
    }
    if ((L.e.level >= 1) && !L.mini) {
        // 聖戦：頭の上へ十字が昇る。
        FxShapePlace place;
        place.right = L.spec.cam_right;
        place.up = L.spec.cam_up;
        place.centre = Vec3{ L.foot.x, L.foot.y, 1.15f + (0.25f * ease_out(te / 0.5f)) };
        place.cell = 0.04f;
        place.alpha = fx_envelope(te, 0.1f, 0.15f, 0.3f);
        place.emissive = 2.4f;
        FxShapeColours colours;
        colours.base = L.e.tint;
        if (place.alpha > 0.01f) {
            (void)fx_emit_shape(cross_shape(), colours, place, out);
        }
    }
}

void look_dust(const Look &L, FxFrame &out)
{
    const int lv = L.e.level;
    const bool big = (lv >= 3);
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, (lv == 4) ? 0.05f : 0.2f };
    smoke.radius = (big ? 0.32f : 0.2f) * L.scale;
    smoke.born = (lv == 4 || lv == 5) ? 0.12f : 0.f;
    smoke.duration = big ? 0.35f : 0.2f;
    smoke.life = big ? 0.9f : 0.7f;
    smoke.count = count_of(L, big ? 12 : 5);
    smoke.rise = big ? 0.55f : 0.35f;
    smoke.size = (big ? 0.22f : 0.15f) * L.scale;
    smoke.grow = 2.f;
    smoke.colour = fx_mix(L.e.base, FxColour{ 1.f, 1.f, 1.f }, 0.15f);
    smoke.colour_end = L.e.base;
    smoke.alpha = 0.75f;
    smoke.emissive = 0.75f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, L.te, out);
    if (lv == 2) {
        // 森：木の葉が数枚舞う。
        FxSpray leaves;
        leaves.origin = Vec3{ L.front.x, L.front.y, 0.15f };
        leaves.dir = Vec3{ 0.f, 0.f, 1.f };
        leaves.cone = 1.f;
        leaves.speed_min = 0.5f;
        leaves.speed_max = 1.1f;
        leaves.gravity = -1.6f;
        leaves.drag = 0.8f;
        leaves.life_min = 0.5f;
        leaves.life_max = 0.8f;
        leaves.count = count_of(L, 8);
        leaves.colour0 = L.e.base;
        leaves.colour1 = L.e.tint;
        leaves.size0 = 0.05f;
        leaves.size1 = 0.03f;
        leaves.emissive0 = 0.5f;
        leaves.alpha1 = 0.f;
        leaves.part = FxPart::Debris;
        leaves.seed = L.seed + 3u;
        (void)fx_emit_spray(leaves, L.te, out);
    }
    if (big) {
        FxSpray debris;
        const bool falls = (lv == 4) || (lv == 5); //!< 壁を作る：上から石が落ちて積もる
        debris.origin = Vec3{ L.front.x, L.front.y, falls ? 1.1f : 0.5f };
        debris.dir = falls ? Vec3{ 0.f, 0.f, -1.f } : Vec3{ 0.f, 0.f, 1.f };
        debris.cone = falls ? 0.4f : 1.4f;
        debris.scatter = falls ? 0.3f : 0.15f;
        debris.speed_min = falls ? 0.2f : 0.8f;
        debris.speed_max = falls ? 0.8f : 2.2f;
        debris.gravity = -9.f;
        debris.born = falls ? 0.f : 0.03f;
        debris.duration = falls ? 0.25f : 0.1f;
        debris.life_min = 0.45f;
        debris.life_max = 0.8f;
        debris.count = count_of(L, 16);
        debris.colour0 = fx_mix(L.e.base, FxColour{ 1.f, 1.f, 1.f }, 0.1f);
        debris.colour1 = L.e.base;
        debris.size0 = 0.08f;
        debris.size1 = 0.06f;
        debris.emissive0 = 0.5f;
        debris.alpha0 = 1.f;
        debris.alpha1 = 0.f;
        debris.floor = FxFloor::Stop;
        debris.floor_z = 0.f;
        debris.part = FxPart::Debris;
        debris.seed = L.seed + 5u;
        (void)fx_emit_spray(debris, L.te, out);
    }
}

void look_splinters(const Look &L, FxFrame &out)
{
    const int n = count_of(L, 20);
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float life = 0.9f;
        const float age = L.te - (0.02f * fx_rand(L.seed, id, 9u));
        if ((age < 0.f) || (age >= life)) {
            continue;
        }
        const float ang = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float speed = 1.4f + (1.8f * fx_rand(L.seed, id, 2u));
        const float up = 1.2f + (2.2f * fx_rand(L.seed, id, 3u));
        Vec3 p = Vec3{ L.front.x, L.front.y, 0.45f * L.scale };
        p.x += std::cos(ang) * speed * 0.5f * age;
        p.y += std::sin(ang) * speed * 0.5f * age;
        p.z += (up * age) - (4.5f * 2.f * age * age);
        const float len = 0.13f + (0.1f * fx_rand(L.seed, id, 4u));
        const float w = 0.035f;
        if (p.z <= 0.02f) {
            p.z = 0.02f;
        }
        FxVoxel v;
        v.center = p;
        v.size = Vec3{ len, w, w * 0.8f };
        const FxColour col = fx_mix(L.e.base, L.e.tint, fx_rand(L.seed, id, 5u));
        v.r = col.r;
        v.g = col.g;
        v.b = col.b;
        v.emissive = 0.9f;
        v.alpha = 1.f - fx_ramp(age, 0.6f, life);
        v.yaw = (2.f * kPi * fx_rand(L.seed, id, 6u)) + (age * 9.f * (fx_rand(L.seed, id, 7u) - 0.5f));
        v.pitch = (age < 0.4f) ? (age * 12.f * (fx_rand(L.seed, id, 8u) - 0.5f)) : 0.f;
        v.part = FxPart::Debris;
        out.voxels.push_back(v);
    }
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, 0.3f };
    smoke.radius = 0.18f * L.scale;
    smoke.duration = 0.15f;
    smoke.life = 0.55f;
    smoke.count = count_of(L, 5);
    smoke.rise = 0.3f;
    smoke.size = 0.11f * L.scale;
    smoke.grow = 1.8f;
    smoke.colour = FxColour{ 0.7f, 0.55f, 0.35f };
    smoke.colour_end = FxColour{ 0.5f, 0.42f, 0.32f };
    smoke.emissive = 0.6f;
    smoke.alpha = 0.55f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, L.te, out);
}

//! 床の平たい点の列（線の一部）。
void floor_dots(FxFrame &out, const Vec3 &a, const Vec3 &b, float fraction, float step, const FxColour &c, float emissive, float alpha)
{
    const Vec3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    const int n = std::max(1, static_cast<int>(len / step));
    const int shown = static_cast<int>(std::ceil(fx_clamp01(fraction) * static_cast<float>(n)));
    for (int i = 0; i <= shown && i <= n; ++i) {
        FxVoxel v;
        v.center = a + (d * (static_cast<float>(i) / static_cast<float>(n)));
        v.size = Vec3{ step * 1.1f, step * 1.1f, 0.02f };
        v.r = c.r;
        v.g = c.g;
        v.b = c.b;
        v.emissive = emissive;
        v.alpha = std::min(alpha, 1.f);
        v.part = FxPart::Puddle;
        out.voxels.push_back(v);
    }
}

void look_rune(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float draw_ring = fx_ramp(te, 0.f, 0.45f);
    const float draw_star = fx_ramp(te, 0.25f, 0.7f);
    const float alpha = fx_envelope(te, 0.f, 1.15f, 0.5f);
    if (alpha <= 0.01f) {
        return;
    }
    const float flicker = 1.f + (0.18f * std::sin(te * 24.f));
    const float R = 0.42f * L.scale;
    const float step = L.mini ? 0.08f : 0.055f;
    const Vec3 c{ L.foot.x, L.foot.y, 0.03f };
    const int segs = L.mini ? 20 : 44;
    const int shown = static_cast<int>(std::ceil(draw_ring * static_cast<float>(segs)));
    for (int i = 0; i < shown; ++i) {
        const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(segs);
        FxVoxel v;
        v.center = c + Vec3{ std::cos(a) * R, std::sin(a) * R, 0.f };
        v.size = Vec3{ step * 1.1f, step * 1.1f, 0.02f };
        v.r = L.e.base.r;
        v.g = L.e.base.g;
        v.b = L.e.base.b;
        v.emissive = 2.f * flicker;
        v.alpha = alpha;
        v.part = FxPart::Puddle;
        out.voxels.push_back(v);
    }
    if (!L.mini) {
        // 六芒星：2 つの三角形。
        for (int tri = 0; tri < 2; ++tri) {
            for (int k = 0; k < 3; ++k) {
                const float a0 = (2.f * kPi * static_cast<float>(k) / 3.f) + (tri * kPi / 3.f) + (kPi / 6.f);
                const float a1 = (2.f * kPi * static_cast<float>(k + 1) / 3.f) + (tri * kPi / 3.f) + (kPi / 6.f);
                const float Rs = R * 0.96f;
                floor_dots(out, c + Vec3{ std::cos(a0) * Rs, std::sin(a0) * Rs, 0.f }, c + Vec3{ std::cos(a1) * Rs, std::sin(a1) * Rs, 0.f }, draw_star, step,
                    L.e.tint, 2.2f * flicker, alpha);
            }
        }
        // 中心の点と明かり。
        FxVoxel v;
        v.center = c;
        v.size = Vec3{ 0.09f, 0.09f, 0.03f };
        v.r = 1.f;
        v.g = 1.f;
        v.b = 1.f;
        v.emissive = 3.f * flicker;
        v.alpha = alpha * draw_star;
        v.part = FxPart::Puddle;
        out.voxels.push_back(v);
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.4f }, 3.2f, L.e.base, 1.6f * alpha * flicker);
    }
}

void look_capture(const Look &L, FxFrame &out)
{
    const Vec3 c{ L.foot.x, L.foot.y, 0.45f * L.scale };
    FxRing flat;
    flat.centre = Vec3{ L.foot.x, L.foot.y, 0.05f };
    flat.r0 = 0.1f;
    flat.r1 = 0.72f * L.scale;
    flat.life = 0.45f;
    flat.thickness = 0.035f;
    flat.segments = 24;
    flat.colour = L.e.base;
    flat.emissive = 2.f;
    flat.alpha = 0.9f;
    (void)fx_emit_ring(flat, L.te, out);
    FxRing up = flat;
    up.centre = c;
    up.normal = fx_toward_camera(L.spec);
    up.r1 = 0.62f * L.scale;
    up.colour = L.e.tint;
    up.born = 0.05f;
    (void)fx_emit_ring(up, L.te, out);
    if (!L.mini) {
        add_light(out, c, 3.f, L.e.base, 1.6f * fx_envelope(L.te, 0.03f, 0.1f, 0.3f));
    }
}

void look_rise(const Look &L, FxFrame &out)
{
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, 0.05f };
    smoke.radius = 0.22f * L.scale;
    smoke.born = 0.f;
    smoke.duration = 0.7f;
    smoke.life = 1.0f;
    smoke.count = count_of(L, 14);
    smoke.rise = 1.0f;
    smoke.size = 0.19f * L.scale;
    smoke.grow = 1.5f;
    smoke.colour = L.e.base;
    smoke.colour_end = fx_mix(L.e.tint, L.e.base, 0.5f);
    smoke.alpha = 0.75f;
    smoke.emissive = 1.7f;
    smoke.drift_x = 0.1f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, L.te, out);
    FxSpray spray;
    spray.origin = Vec3{ L.front.x, L.front.y, 0.05f };
    spray.dir = Vec3{ 0.f, 0.f, 1.f };
    spray.cone = 0.2f;
    spray.scatter = 0.25f * L.scale;
    spray.speed_min = 0.8f;
    spray.speed_max = 1.5f;
    spray.gravity = 0.f;
    spray.drag = 1.f;
    spray.born = 0.05f;
    spray.duration = 0.65f;
    spray.life_min = 0.5f;
    spray.life_max = 0.8f;
    spray.count = count_of(L, 18);
    spray.colour0 = FxColour{ 0.6f, 1.f, 0.7f };
    spray.colour1 = L.e.tint;
    spray.size0 = 0.05f;
    spray.size1 = 0.015f;
    spray.emissive0 = 2.4f;
    spray.emissive1 = 0.6f;
    spray.seed = L.seed + 9u;
    (void)fx_emit_spray(spray, L.te, out);
    FxRing ring;
    ring.centre = Vec3{ L.foot.x, L.foot.y, 0.03f };
    ring.r0 = 0.55f * L.scale;
    ring.r1 = 0.2f;
    ring.life = 0.6f;
    ring.thickness = 0.03f;
    ring.segments = 22;
    ring.colour = L.e.base;
    ring.emissive = 1.8f;
    ring.alpha = 0.8f;
    (void)fx_emit_ring(ring, L.te, out);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.5f }, 3.f, L.e.base, 1.2f * fx_envelope(L.te, 0.2f, 0.6f, 0.7f));
    }
}

//! 画面全体の白い閃光（写真）。
void photo_flash(float te, FxFrame &out)
{
    const float env = fx_envelope(te, 0.03f, 0.07f, 0.35f);
    if (env > 0.01f) {
        // 幅の違う 3 本を重ねて、帯のむらを目立たなくする。
        fx_add_screen_wave(out, FxColour{ 1.f, 1.f, 1.f }, 0.5f, 3.1f, 0.5f * env, 0.f);
        fx_add_screen_wave(out, FxColour{ 1.f, 1.f, 1.f }, 0.5f, 4.3f, 0.5f * env, 0.f);
        fx_add_screen_wave(out, FxColour{ 1.f, 1.f, 1.f }, 0.5f, 6.1f, 0.5f * env, 0.f);
    }
}

void look_photo(const Look &L, FxFrame &out)
{
    if (L.mini) {
        return;
    }
    photo_flash(L.te, out);
    add_light(out, Vec3{ L.foot.x, L.foot.y, 1.2f }, 7.f, FxColour{ 1.f, 1.f, 1.f }, 5.f * fx_envelope(L.te, 0.03f, 0.07f, 0.3f));
}

//! 三日月の刃を 1 枚（進む向き `dir`、画面上の向きに合わせる）。
void crescent_blade(const FxSpec &spec, const Vec3 &pos, const Vec3 &dir, float alpha, float scale, int layers, const FxColour &c, FxFrame &out)
{
    if (alpha <= 0.01f) {
        return;
    }
    const float sx = dot(dir, spec.cam_right);
    const float sy = dot(dir, spec.cam_up);
    FxShapePlace place;
    place.centre = pos;
    place.right = spec.cam_right;
    place.up = spec.cam_up;
    place.cell = 0.042f * scale;
    place.layers = layers;
    place.depth = 0.04f;
    place.rotate = std::atan2(sy, sx) + kPi; //!< 開いた側（角）が後ろへ流れる
    place.alpha = alpha;
    place.emissive = 2.6f;
    FxShapeColours colours;
    colours.base = c;
    colours.keys = "o";
    colours.colours = { FxColour{ 1.f, 1.f, 0.95f } };
    (void)fx_emit_shape(crescent_shape(), colours, place, out);
}

//! 弧を描いて走る刃の位置と進む向き。`u` は 0（撃った者）〜 1（対象）。
void crescent_pose(const Vec3 &from, const Vec3 &to, float u, float arc, Vec3 &pos, Vec3 &dir)
{
    const auto at = [&](float k) {
        const Vec3 d = to - from;
        Vec3 side = cross(d, Vec3{ 0.f, 0.f, 1.f });
        side = (dot(side, side) < 1e-6f) ? Vec3{ 0.f, 1.f, 0.f } : normalize(side);
        k = fx_clamp01(k);
        Vec3 p = from + (d * k) + (side * (arc * std::sin(kPi * k)));
        p.z += 0.12f * std::sin(kPi * k);
        return p;
    };
    pos = at(u);
    const Vec3 ahead = at(u + 0.03f) - at(u - 0.03f);
    dir = (dot(ahead, ahead) < 1e-8f) ? Vec3{ 1.f, 0.f, 0.f } : normalize(ahead);
}

//! 刃が走る：本体と、残像を 4 枚。
void crescent_travel(const FxSpec &spec, const Vec3 &from, const Vec3 &to, float u, float arc, const FxColour &base, FxFrame &out)
{
    if ((u < 0.f) || (u > 1.f)) {
        return;
    }
    for (int k = 4; k >= 0; --k) {
        const float uu = u - (0.05f * static_cast<float>(k));
        if (uu < 0.f) {
            continue;
        }
        Vec3 p;
        Vec3 d;
        crescent_pose(from, to, uu, arc, p, d);
        crescent_blade(spec, p, d, (k == 0) ? 1.f : (0.5f / static_cast<float>(k)), 1.f - (0.06f * static_cast<float>(k)), (k == 0) ? 2 : 1,
            (k == 0) ? base : fx_scale(base, 0.8f), out);
    }
}

//! 刃が通ったあとの切り口（斜めの光る線）。
void look_slash(const Look &L, FxFrame &out)
{
    const float te = L.te;
    if (te < 0.25f) {
        const float u = te / 0.25f;
        const Vec3 c{ L.foot.x, L.foot.y, 0.5f * L.scale };
        const Vec3 d = normalize(on_screen(L.spec, 1.f, 0.55f));
        const float len = 0.95f * L.scale * ease_out(u * 2.f);
        put_bar(out, c, d, len, 0.045f * L.scale, FxColour{ 1.f, 1.f, 0.9f }, 3.4f, 1.f - u);
        put_bar(out, c, d, len * 1.05f, 0.1f * L.scale, L.e.base, 1.6f, 0.4f * (1.f - u));
        if (!L.mini) {
            add_light(out, c, 3.f, L.e.base, 1.8f * (1.f - u));
        }
    }
    FxSpray spray;
    spray.origin = Vec3{ L.foot.x, L.foot.y, 0.5f * L.scale };
    spray.cone = kPi;
    spray.speed_min = 0.6f;
    spray.speed_max = 1.4f;
    spray.gravity = -2.f;
    spray.drag = 2.f;
    spray.life_min = 0.15f;
    spray.life_max = 0.3f;
    spray.count = count_of(L, 10);
    spray.colour0 = FxColour{ 1.f, 1.f, 0.9f };
    spray.colour1 = L.e.base;
    spray.size0 = 0.04f;
    spray.size1 = 0.01f;
    spray.emissive0 = 2.6f;
    spray.emissive1 = 0.6f;
    spray.seed = L.seed;
    (void)fx_emit_spray(spray, te, out);
}

void look_lift(const Look &L, FxFrame &out)
{
    const float te = L.te;
    FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
    const float env = fx_envelope(te, 0.3f, 0.8f, 0.35f);
    const float bob = (te > 0.3f) ? (0.05f * std::sin((te - 0.3f) * 9.f) * fx_envelope(te - 0.3f, 0.1f, 0.6f, 0.3f)) : 0.f;
    mod.lift = std::max(mod.lift, (0.38f * env + bob) * L.scale);
    mod.tremble = std::max(mod.tremble, 0.012f * env);
    mod.tremble_phase = te * 40.f;
    const int n = count_of(L, 10);
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float ang = (2.f * kPi * static_cast<float>(i) / static_cast<float>(n)) + (te * 2.2f);
        const float r = 0.3f * L.scale;
        const float z = 0.15f + (0.6f * fx_rand(L.seed, id, 1u)) + (0.1f * std::sin((te * 6.f) + static_cast<float>(i)));
        put(out, Vec3{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), z * L.scale }, 0.04f, (i % 2 == 0) ? L.e.base : L.e.tint, 2.2f,
            0.9f * env);
    }
    FxRing ring;
    ring.centre = Vec3{ L.foot.x, L.foot.y, 0.03f };
    ring.r0 = 0.42f * L.scale;
    ring.r1 = 0.2f;
    ring.born = 0.15f;
    ring.life = 0.9f;
    ring.thickness = 0.025f;
    ring.segments = 20;
    ring.colour = L.e.base;
    ring.emissive = 1.4f;
    ring.alpha = 0.55f;
    ring.fade_out = true;
    (void)fx_emit_ring(ring, te, out);
}

void look_identify(const Look &L, FxFrame &out)
{
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    for (int k = 0; k < (L.mini ? 1 : 2); ++k) {
        FxRing ring;
        ring.centre = c;
        ring.normal = fx_toward_camera(L.spec);
        ring.r0 = 0.1f;
        ring.r1 = (k == 0) ? 0.56f * L.scale : 0.4f * L.scale;
        ring.born = 0.15f * static_cast<float>(k);
        ring.life = 0.45f;
        ring.thickness = 0.028f;
        ring.segments = 22;
        ring.colour = (k == 0) ? L.e.base : L.e.tint;
        ring.emissive = 1.7f;
        ring.alpha = 0.9f;
        (void)fx_emit_ring(ring, L.te, out);
    }
    const int n = count_of(L, 8);
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = 0.1f + (0.2f * fx_rand(L.seed, id, 1u));
        const float age = L.te - born;
        if ((age < 0.f) || (age >= 0.3f)) {
            continue;
        }
        const float u = age / 0.3f;
        const float ang = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float r = 0.2f + (0.3f * u);
        put(out, c + on_screen(L.spec, std::cos(ang) * r, std::sin(ang) * r), 0.035f, L.e.tint, 2.4f, 1.f - u);
    }
}

void look_mist(const Look &L, FxFrame &out)
{
    const int lv = L.e.level;
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, 0.15f };
    smoke.radius = ((lv >= 2) ? 0.34f : 0.26f) * L.scale;
    smoke.born = 0.f;
    smoke.duration = 0.4f;
    smoke.life = 0.85f;
    smoke.count = count_of(L, (lv >= 2) ? 14 : 10);
    smoke.rise = 0.6f;
    smoke.size = 0.19f * L.scale;
    smoke.grow = 1.7f;
    smoke.colour = L.e.base;
    smoke.colour_end = fx_mix(L.e.tint, L.e.base, 0.4f);
    smoke.alpha = 0.75f;
    smoke.emissive = 1.2f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, L.te, out);
    FxRing ring;
    ring.centre = Vec3{ L.foot.x, L.foot.y, 0.04f };
    ring.r0 = 0.12f;
    ring.r1 = ((lv >= 2) ? 0.7f : 0.52f) * L.scale;
    ring.life = 0.55f;
    ring.thickness = 0.03f;
    ring.segments = 22;
    ring.colour = L.e.base;
    ring.emissive = 1.6f;
    ring.alpha = 0.75f;
    (void)fx_emit_ring(ring, L.te, out);
}

/* ============================================================ 見た目の呼び分け */

//! 見た目 1 つの長さ（秒）。この間に全部消える。
float look_seconds(const Entry &e)
{
    switch (e.kind) {
    case Kind::Skull:
        return 1.35f;
    case Kind::Hand:
        return 1.5f;
    case Kind::Drain:
        return 1.25f;
    case Kind::Ripple:
        return 0.5f + (0.13f * static_cast<float>(std::max(e.level, 2) - 1)) + 0.05f;
    case Kind::Stars:
        return 1.05f;
    case Kind::Wrap:
        return 0.56f + (0.07f * static_cast<float>(std::max(e.level, 3) - 1)) + 0.05f;
    case Kind::Sleep:
        return 1.85f;
    case Kind::ArrowDown:
        return 0.85f;
    case Kind::ArrowUp:
        return 0.8f;
    case Kind::Heal:
        return 1.85f;
    case Kind::Poly:
        return 0.85f;
    case Kind::Clone:
        return 0.5f;
    case Kind::Fear:
        return 1.15f;
    case Kind::Unravel:
        return 1.65f;
    case Kind::Stasis:
        return 1.55f;
    case Kind::Flash:
        return 0.55f;
    case Kind::Dust:
        return 1.45f;
    case Kind::Splinters:
        return 1.0f;
    case Kind::Rune:
        return 1.7f;
    case Kind::Capture:
        return 0.55f;
    case Kind::Rise:
        return 2.0f;
    case Kind::Photo:
        return 0.5f;
    case Kind::Crescent:
        return 0.3f;
    case Kind::Lift:
        return 1.55f;
    case Kind::Identify:
        return 0.65f;
    case Kind::Mist:
        return 1.3f;
    case Kind::Generic:
        break;
    }
    return 0.85f;
}

void emit_look(const Look &L, FxFrame &out)
{
    switch (L.e.kind) {
    case Kind::Skull:
        look_skull(L, out);
        break;
    case Kind::Hand:
        look_hand(L, out);
        break;
    case Kind::Drain:
        look_drain(L, out);
        break;
    case Kind::Ripple:
        look_ripple(L, out);
        break;
    case Kind::Stars:
        look_stars(L, out);
        break;
    case Kind::Wrap:
        look_wrap(L, out);
        break;
    case Kind::Sleep:
        look_sleep(L, out);
        break;
    case Kind::ArrowDown:
        look_arrow(L, out, true);
        break;
    case Kind::ArrowUp:
        look_arrow(L, out, false);
        break;
    case Kind::Heal:
        look_heal(L, out);
        break;
    case Kind::Poly:
        look_poly(L, out);
        break;
    case Kind::Clone:
        look_clone(L, out);
        break;
    case Kind::Fear:
        look_fear(L, out);
        break;
    case Kind::Unravel:
        look_unravel(L, out);
        break;
    case Kind::Stasis:
        look_stasis(L, out);
        break;
    case Kind::Flash:
        look_flash(L, out);
        break;
    case Kind::Dust:
        look_dust(L, out);
        break;
    case Kind::Splinters:
        look_splinters(L, out);
        break;
    case Kind::Rune:
        look_rune(L, out);
        break;
    case Kind::Capture:
        look_capture(L, out);
        break;
    case Kind::Rise:
        look_rise(L, out);
        break;
    case Kind::Photo:
        look_photo(L, out);
        break;
    case Kind::Crescent:
        look_slash(L, out);
        break;
    case Kind::Lift:
        look_lift(L, out);
        break;
    case Kind::Identify:
        look_identify(L, out);
        break;
    case Kind::Mist:
        look_mist(L, out);
        break;
    case Kind::Generic:
        break;
    }
}

//! 範囲のマスの靄（マスが多いとき・ブレス）。
void cell_mist(const FxSpec &spec, const Entry &e, int gx, int gy, int index, float te, FxFrame &out)
{
    FxSmoke smoke;
    smoke.origin = fx_cell_centre(gx, gy, 0.1f);
    smoke.radius = 0.22f;
    smoke.born = 0.f;
    smoke.duration = 0.2f;
    smoke.life = 0.7f;
    smoke.count = 2;
    smoke.rise = 0.4f;
    smoke.size = 0.15f;
    smoke.grow = 1.5f;
    smoke.colour = e.base;
    smoke.colour_end = e.tint;
    smoke.alpha = 0.55f;
    smoke.emissive = 0.9f;
    smoke.seed = spec.seed + static_cast<std::uint32_t>(index * 131);
    (void)fx_emit_smoke(smoke, te, out);
}

//! 範囲の見た目の長さ：マスが多いときは靄（寿命 0.9 秒）が残る分を見る。
float area_look_seconds(const FxSpec &spec, const Entry &e)
{
    const bool mist = (spec.cells.size() > 25u) && (e.kind != Kind::Photo);
    return std::max(look_seconds(e), mist ? 0.95f : 0.f);
}

float max_ring(const FxSpec &spec)
{
    int r = 0;
    for (const FxBurstCell &c : spec.cells) {
        r = std::max(r, c.ring);
    }
    return static_cast<float>(r);
}

//! 範囲のマス全部（ボール・ジャンプ・ブレス）。中心は全部、周りは小さな版を距離ぶん遅らせる。
void emit_area(const FxStyleCtx &ctx, const Entry &e, float te, float ring_delay, bool centre_full, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    if (spec.cells.empty()) {
        emit_look(make_look(spec, e, spec.centre_gx, spec.centre_gy, te, 1.f, false, spec.seed), out);
        return;
    }
    const bool many = spec.cells.size() > 25u;
    int index = 0;
    for (const FxBurstCell &cell : spec.cells) {
        ++index;
        const float tc = te - (static_cast<float>(cell.ring) * ring_delay);
        if (tc < 0.f) {
            continue;
        }
        const bool centre = centre_full && (cell.ring == 0);
        if (many && !centre) {
            if (e.kind != Kind::Photo) {
                cell_mist(spec, e, cell.gx, cell.gy, index, tc, out);
            }
            continue;
        }
        emit_look(make_look(spec, e, cell.gx, cell.gy, tc, centre ? 1.f : 0.62f, !centre, spec.seed + static_cast<std::uint32_t>(index * 977)), out);
    }
}

Look target_look(const FxStyleCtx &ctx, const Entry &e, float te)
{
    return make_look(ctx.spec, e, ctx.spec.centre_gx, ctx.spec.centre_gy, te, 1.f, false, ctx.spec.seed);
}

FxColour wave_colour(const Entry &e)
{
    return fx_mix(e.base, FxColour{ 1.f, 1.f, 1.f }, 0.12f);
}

/* ============================================================ 段階の関数 */

//! 飛んでいる間：属性の色の小さな玉。`big` は捕獲の玉。
void small_orb(const FxStyleCtx &ctx, const Entry &e, float big, FxFrame &out)
{
    const float fly = std::max(ctx.fly, 1e-4f);
    const float s = fx_clamp01(ctx.t / fly);
    const Vec3 pos = fx_flight_position(ctx.spec, ctx.params, s);
    const Vec3 prev = fx_flight_position(ctx.spec, ctx.params, std::max(0.f, s - 0.02f));
    Vec3 dir = pos - prev;
    dir = (dot(dir, dir) < 1e-8f) ? Vec3{ 1.f, 0.f, 0.f } : normalize(dir);
    const FxColour core = fx_mix(FxColour{ 1.f, 1.f, 1.f }, e.base, 0.45f);
    put(out, pos, 0.14f * big, core, 3.6f, 1.f, FxPart::OrbCore);
    const float o = 0.085f * big;
    const Vec3 offs[6] = { { o, 0.f, 0.f }, { -o, 0.f, 0.f }, { 0.f, o, 0.f }, { 0.f, -o, 0.f }, { 0.f, 0.f, o }, { 0.f, 0.f, -o } };
    for (const Vec3 &d : offs) {
        put(out, pos + d, 0.085f * big, e.base, 2.2f, 0.9f, FxPart::OrbCore);
    }
    // 尾。
    for (int k = 1; k <= 8; ++k) {
        const float tt = ctx.t - (0.02f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / fly));
        const float f = 1.f - (static_cast<float>(k) / 9.f);
        put(out, p, 0.09f * big * f, e.base, 1.6f * f, 0.6f * f, FxPart::OrbTail);
    }
    add_light(out, pos, 2.6f, e.base, 1.2f);
    out.projectile_visible = true;
    out.projectile_pos = pos;
    out.projectile_dir = dir;
}

void spirit_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        fx_generic_flight(ctx, out);
        return;
    }
    if (e->kind == Kind::Crescent) {
        const float fly = std::max(ctx.fly, 1e-4f);
        crescent_travel(ctx.spec, fx_source_centre(ctx.spec, 0.6f), fx_target_centre(ctx.spec, 0.5f), ctx.t / fly, 0.5f, e->base, out);
        const Vec3 pos = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(ctx.t / fly));
        out.projectile_visible = true;
        out.projectile_pos = pos;
        out.projectile_dir = Vec3{ 1.f, 0.f, 0.f };
        return;
    }
    small_orb(ctx, *e, (e->kind == Kind::Capture) ? 1.5f : 1.f, out);
}

void spirit_impact(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        fx_generic_impact(ctx, out);
        return;
    }
    if (ctx.spec.explode) {
        emit_area(ctx, *e, ctx.te, ctx.params.ring_delay, true, out);
    } else {
        const Look L = target_look(ctx, *e, ctx.te);
        emit_look(L, out);
    }
}

float spirit_impact_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        return fx_generic_impact_seconds(spec, params);
    }
    return (spec.explode ? area_look_seconds(spec, *e) : look_seconds(*e)) + (spec.explode ? (max_ring(spec) * params.ring_delay) : 0.f);
}

//! ビームの終点に見た目が始まる秒。
float beam_end_time(const FxSpec &spec, const VoxelFxParams &params)
{
    const float segments = static_cast<float>((spec.route.size() > 1) ? (spec.route.size() - 1) : 0);
    return segments * params.beam_cell_delay;
}

void spirit_beam(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        fx_generic_beam(ctx, out);
        return;
    }
    const float t_end = beam_end_time(ctx.spec, ctx.params);
    if (e->kind == Kind::Crescent) {
        // 光の線は引かず、刃が経路を走る。
        const Vec3 from = ctx.spec.route.empty() ? fx_source_centre(ctx.spec, 0.6f) : ctx.spec.route.front();
        const Vec3 to = ctx.spec.route.empty() ? fx_target_centre(ctx.spec, 0.6f) : ctx.spec.route.back();
        const float travel = std::max(0.3f, t_end + 0.1f);
        crescent_travel(ctx.spec, from, to, ctx.t / travel, 0.5f, e->base, out);
        const Look L = target_look(ctx, *e, ctx.t - travel);
        if (L.te >= 0.f) {
            emit_look(L, out);
        }
        return;
    }
    if (e->kind != Kind::Photo) {
        fx_emit_beam_line(ctx.spec, ctx.params, ctx.t, out);
    }
    // 地形を変えるビーム：経路のマスに土煙を順に立てる。
    if ((e->kind == Kind::Dust) || (e->kind == Kind::Splinters)) {
        for (std::size_t i = 1; (i + 1) < ctx.spec.route.size(); ++i) {
            const float tc = ctx.t - (static_cast<float>(i) * ctx.params.beam_cell_delay) - 0.02f;
            if (tc >= 0.f) {
                cell_mist(ctx.spec, *e, static_cast<int>(std::floor(ctx.spec.route[i].x)), static_cast<int>(std::floor(ctx.spec.route[i].y)),
                    static_cast<int>(i), tc, out);
            }
        }
    }
    const float te = ctx.t - t_end;
    if (te >= 0.f) {
        const Look L = target_look(ctx, *e, te);
        emit_look(L, out);
    }
}

float spirit_beam_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        return fx_generic_beam_seconds(spec, params);
    }
    const float t_end = beam_end_time(spec, params);
    const float tail = ((e->kind == Kind::Dust) || (e->kind == Kind::Splinters)) ? 0.95f : 0.f;
    if (e->kind == Kind::Crescent) {
        return std::max(0.3f, t_end + 0.1f) + look_seconds(*e);
    }
    return t_end + std::max(look_seconds(*e), std::max(params.beam_life, tail));
}

void spirit_breath(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        fx_generic_breath(ctx, out);
        return;
    }
    emit_area(ctx, *e, ctx.t, ctx.params.breath_ring_delay, false, out);
}

float spirit_breath_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        return fx_generic_breath_seconds(spec, params);
    }
    return area_look_seconds(spec, *e) + (max_ring(spec) * params.breath_ring_delay);
}

void spirit_burst(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        fx_generic_burst(ctx, out);
        return;
    }
    emit_area(ctx, *e, ctx.t, ctx.params.ring_delay, true, out);
}

float spirit_burst_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        return fx_generic_burst_seconds(spec, params);
    }
    return area_look_seconds(spec, *e) + (max_ring(spec) * params.ring_delay);
}

//! 飛ばない魔法・視界内の全員・足元だけ。
void spirit_glow(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        fx_generic_glow(ctx, out);
        return;
    }
    if (ctx.spec.glow_mode == FxGlowMode::AllInSight) {
        // 視界内の全員：属性の色の光の波（写真は画面全体の白い閃光）。
        if (e->kind == Kind::Photo) {
            photo_flash(ctx.t, out);
        } else {
            fx_emit_all_sight_wave(ctx, out, wave_colour(*e));
        }
        return;
    }
    if (e->kind == Kind::Crescent) {
        const float travel = 0.4f;
        crescent_travel(ctx.spec, fx_source_centre(ctx.spec, 0.6f), fx_target_centre(ctx.spec, 0.5f), ctx.t / travel, 0.5f, e->base, out);
        const Look L = target_look(ctx, *e, ctx.t - travel);
        if (L.te >= 0.f) {
            emit_look(L, out);
        }
        return;
    }
    const Look L = target_look(ctx, *e, ctx.t);
    emit_look(L, out);
}

float spirit_glow_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if ((e == nullptr) || (e->kind == Kind::Generic)) {
        return fx_generic_glow_seconds(spec, params);
    }
    if (spec.glow_mode == FxGlowMode::AllInSight) {
        return (e->kind == Kind::Photo) ? 0.5f : 1.05f;
    }
    if (e->kind == Kind::Crescent) {
        return 0.4f + look_seconds(*e);
    }
    return look_seconds(*e);
}

} // namespace

void fx_register_spirit(FxStyleRegistry &registry)
{
    for (const Entry &e : kEntries) {
        FxStyle &s = registry.add(e.attr, e.base, e.smoke, e.emissive);
        s.flight = spirit_flight;
        s.impact = spirit_impact;
        s.impact_seconds = spirit_impact_seconds;
        s.beam = spirit_beam;
        s.beam_seconds = spirit_beam_seconds;
        s.breath = spirit_breath;
        s.breath_seconds = spirit_breath_seconds;
        s.burst = spirit_burst;
        s.burst_seconds = spirit_burst_seconds;
        s.glow = spirit_glow;
        s.glow_seconds = spirit_glow_seconds;
    }
}

} // namespace hd2d
