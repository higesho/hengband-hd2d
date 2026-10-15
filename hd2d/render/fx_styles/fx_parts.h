/*!
 * @file fx_parts.h
 * @brief 属性ごとの見た目が共通に使う部品（粒・水たまり・煙・稲妻・輪・画面の揺れ・文字の格子の形・絵の板・タイルの写し）。
 *
 * **どの部品も状態を持たない**（`voxel_fx.h` の方針）。
 * 「いつ出るか」を `born`（撃ってからの秒）で渡し、`t`（撃ってからの秒）を渡すと、その時刻の姿を
 * `FxFrame::voxels` に**足す**。同じ入力に同じ出力を返すので、検査が時刻を直に指せる。
 *
 * | 部品 | 使い方の一言 |
 * |---|---|
 * | `FxParticle` / `fx_emit_particle` | 重力・空気の抵抗・寿命・色と大きさの変化つきの粒 1 つ。床に当たったら止まる／消える |
 * | `FxSpray` / `fx_emit_spray` | 粒をまとめて吹き出す（雫・火花・礫・雹） |
 * | `FxPuddle` / `fx_emit_puddle` | 床の平たいボクセルの溜まり。広がって、少しずつ消える |
 * | `FxSmoke` / `fx_emit_smoke` | 上へ昇って広がって薄れる煙・霧 |
 * | `FxLightning` / `fx_emit_lightning` | 始点と終点の間のジグザグの光の線。毎回形が変わる |
 * | `FxRing` / `fx_emit_ring` | 広がる輪・縮む輪（衝撃波・波紋・時計の輪） |
 * | `fx_add_shake` | 画面を揺らす（強さと長さ。cfg の `fx_screen_shake` で切れる） |
 * | `fx_mod_for` | 対象のマスの実体に、押しつぶし・震え・のけぞり・浮き上がり・灰色・二重を掛ける |
 * | `fx_emit_tile_ghost` | 対象のタイルと同じ形の板を、色を変えて粒に分けて崩す（分解） |
 * | `FxShape` / `fx_emit_shape` | 文字の格子（`"..##.."` の行）から小さなボクセルの形を作る（矢印・剣・手・時計・岩…） |
 * | `fx_emit_sprite` | `assets/fx/*.png` の絵を 3D の位置に出す（画面に正対。位置・大きさ・色・透明度・回転を時間で動かせる） |
 * | `fx_add_screen_wave` | 画面の端から端へ走る光の波（視界内の全員にかかる魔法） |
 *
 * 座標は世界座標（マス。x 東・y 南・z 上）。マス (gx,gy) の中心は (gx+0.5, gy+0.5)。
 */
#pragma once

#include "render/voxel_fx.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace hd2d {

/* ============================================================ 基本 */

//! 0 以上 1 未満の擬似乱数。同じ入力に同じ出力。`std::rand` は使わない。
float fx_rand(std::uint32_t seed, std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0);
float fx_clamp01(float v);
float fx_lerp(float a, float b, float t);
FxColour fx_mix(const FxColour &a, const FxColour &b, float t);
FxColour fx_scale(const FxColour &a, float k);
//! `t` が `t0` 以下で 0、`t1` 以上で 1 のなめらかな立ち上がり。
float fx_ramp(float t, float t0, float t1);
//! 立ち上がり `attack` 秒 → 保持 `hold` 秒 → 減衰 `release` 秒の包絡（0〜1）。`t` は始まりからの秒。
float fx_envelope(float t, float attack, float hold, float release);
//! ローカルの向き（+x が前）を世界の向きへ。`FxVoxel::yaw/pitch` と同じ式。
Vec3 fx_rotate_local(const Vec3 &v, float yaw, float pitch);
//! 向き `d` の `yaw`・`pitch`（`FxVoxel` に入れる値）。
void fx_direction_angles(const Vec3 &d, float &yaw, float &pitch);
//! マス (gx,gy) の中心の世界座標（高さ `z`）。
Vec3 fx_cell_centre(int gx, int gy, float z = 0.f);
//! 対象（着弾点）のマスの中心。`z` は高さ（胸の高さは 0.5）。
Vec3 fx_target_centre(const FxSpec &spec, float z = 0.5f);
//! 撃った者のマスの中心。
Vec3 fx_source_centre(const FxSpec &spec, float z = 0.5f);
//! 画面（カメラ）のほうを向く単位ベクトル（`cam_right` × `cam_up` の逆）。絵を対象の表面に浮かせるのに使う。
Vec3 fx_toward_camera(const FxSpec &spec);

/* ============================================================ 粒 */

//! 床（`floor_z`）に当たったときの動き。
enum class FxFloor : std::uint8_t {
    None,   //!< 床を見ない
    Stop,   //!< 当たった所で止まり、残りの寿命をそこで過ごす
    Vanish, //!< 当たったら消える
};

/*!
 * @brief 粒 1 つ。位置は `pos + vel × 年齢` に重力と空気の抵抗を足した式で決まる。
 * @details `gravity` は z 方向の加速度（マス/秒²）。負で落ち、正で昇る（煙）。`drag` は空気の抵抗（1/秒。
 * 速さが `e^(-drag × 年齢)` で落ちる）。色・大きさ・自己発光（`emissive` が 0 より大きければ光る）・
 * 不透明度は、寿命の始め（`*0`）から終わり（`*1`）へ直線で変わる。
 */
struct FxParticle {
    Vec3 pos{};
    Vec3 vel{};
    float gravity{ -6.f };
    float drag{ 0.f };
    float born{ 0.f };
    float life{ 0.5f };
    FxColour colour0{ 1.f, 1.f, 1.f };
    FxColour colour1{ 1.f, 1.f, 1.f };
    float size0{ 0.06f };
    float size1{ 0.02f };
    float emissive0{ 0.f };
    float emissive1{ 0.f };
    float alpha0{ 1.f };
    float alpha1{ 0.f };
    FxFloor floor{ FxFloor::None };
    float floor_z{ 0.f };
    FxPart part{ FxPart::Particle };
};

//! 粒の位置（年齢 `age` 秒）。床に当たっていれば `*hit` に真、`*hit_age` に当たった年齢を入れる。
Vec3 fx_particle_position(const FxParticle &p, float age, bool *hit = nullptr, float *hit_age = nullptr);
//! `t`（撃ってからの秒）の粒を足す。寿命の外か、床に当たって消えたなら足さず偽を返す。
bool fx_emit_particle(const FxParticle &p, float t, FxFrame &out);

/*!
 * @brief 粒をまとめて吹き出す。1 粒ごとの向き・速さ・寿命は `seed` と番号から決まる。
 * @details `duration` が 0 なら全部 `born` に出る。0 より大きければ `born` から `duration` 秒の間に等間隔で出る。
 * `cone` は `dir` を中心にした広がり（半角。ラジアン。π 以上で全方向）。
 */
struct FxSpray {
    Vec3 origin{};
    Vec3 dir{ 0.f, 0.f, 1.f };
    float cone{ 0.5f };
    //! 出る位置のばらつき（水平。マス）。
    float scatter{ 0.f };
    float speed_min{ 1.f };
    float speed_max{ 2.f };
    float gravity{ -6.f };
    float drag{ 0.f };
    float born{ 0.f };
    float duration{ 0.f };
    float life_min{ 0.3f };
    float life_max{ 0.6f };
    int count{ 12 };
    FxColour colour0{ 1.f, 1.f, 1.f };
    FxColour colour1{ 1.f, 1.f, 1.f };
    float size0{ 0.06f };
    float size1{ 0.02f };
    float emissive0{ 0.f };
    float emissive1{ 0.f };
    float alpha0{ 1.f };
    float alpha1{ 0.f };
    FxFloor floor{ FxFloor::None };
    float floor_z{ 0.f };
    FxPart part{ FxPart::Particle };
    std::uint32_t seed{ 1 };
};

//! 吹き出した粒のうち、`t` で見えているものを足す。足した数を返す。
int fx_emit_spray(const FxSpray &s, float t, FxFrame &out);

/* ============================================================ 床の溜まり・煙 */

/*!
 * @brief 床の平たいボクセルの溜まり（水たまり）。`born` から `spread` 秒で `radius` まで広がり、`hold` 秒そのまま、
 * `fade` 秒で薄れて消える。輪郭は `ragged` でゆがむ。
 */
struct FxPuddle {
    float cx{ 0.f };
    float cy{ 0.f };
    float z{ 0.f };
    float radius{ 0.3f };
    float born{ 0.f };
    float spread{ 0.25f };
    float hold{ 0.6f };
    float fade{ 0.5f };
    FxColour colour{ 1.f, 1.f, 1.f };
    float emissive{ 0.f };
    float alpha{ 0.85f };
    float voxel{ 0.07f };
    float thickness{ 0.02f };
    float ragged{ 0.35f };
    std::uint32_t seed{ 1 };
    FxPart part{ FxPart::Puddle };
};
//! 足したボクセルの数を返す。
int fx_emit_puddle(const FxPuddle &p, float t, FxFrame &out);

/*!
 * @brief 煙・霧。`born` から `duration` 秒の間に `count` 個の塊が出て、それぞれ `life` 秒かけて上へ昇り、
 * 大きくなり（`grow` 倍）、薄れる。`radius` は出る位置の水平のばらつき。
 */
struct FxSmoke {
    Vec3 origin{};
    float radius{ 0.2f };
    float born{ 0.f };
    float duration{ 0.4f };
    float life{ 0.8f };
    int count{ 6 };
    float rise{ 0.6f };
    float size{ 0.14f };
    float grow{ 1.8f };
    FxColour colour{ 0.7f, 0.7f, 0.7f };
    FxColour colour_end{ 0.5f, 0.5f, 0.5f };
    float alpha{ 0.6f };
    float emissive{ 0.f };
    //! 水平に流れる量（マス/秒）。
    float drift_x{ 0.f };
    float drift_y{ 0.f };
    std::uint32_t seed{ 1 };
    FxPart part{ FxPart::Mist };
};
//! 足した塊の数を返す。
int fx_emit_smoke(const FxSmoke &s, float t, FxFrame &out);

/* ============================================================ 折れ線の光（稲妻） */

//! 始点 `a` と終点 `b` の間をジグザグに折った点の並び（両端を含む）。`frame` が変わると形が変わる。
std::vector<Vec3> fx_zigzag_points(const Vec3 &a, const Vec3 &b, int segments, float jitter, std::uint32_t seed, std::uint32_t frame);

/*!
 * @brief 稲妻 1 本。始点から終点へ、毎秒 `flicker_hz` 回形を変えながら光る。芯（細く明るい）と周りの光（太く半透明）で描く。
 * @details `branches` は途中から横へ分かれる枝の数。束ねたいときは `seed` を変えて何本も呼ぶ。
 */
struct FxLightning {
    Vec3 a{};
    Vec3 b{};
    int segments{ 8 };
    float jitter{ 0.25f };
    float core{ 0.06f };
    float glow{ 0.2f };
    FxColour core_colour{ 1.f, 1.f, 1.f };
    FxColour glow_colour{ 0.6f, 0.7f, 1.f };
    float emissive{ 3.2f };
    float glow_alpha{ 0.35f };
    float alpha{ 1.f };
    int branches{ 1 };
    float flicker_hz{ 30.f };
    std::uint32_t seed{ 1 };
};
//! `t`（形を決める時刻。秒）の稲妻を足す。足したボクセルの数を返す。
int fx_emit_lightning(const FxLightning &l, float t, FxFrame &out);

/* ============================================================ 輪 */

/*!
 * @brief 輪。`born` から `life` 秒で半径が `r0` から `r1` へ変わる（`r0 < r1` で広がる、`r0 > r1` で縮む）。
 * @details 平面は `normal`（既定は水平＝法線が上）。画面に正対した輪が欲しいときは `fx_toward_camera(spec)` を渡す。
 * 不透明度は `alpha` から始まり、`fade_out` が真なら終わりに向けて 0 になる。
 */
struct FxRing {
    Vec3 centre{};
    Vec3 normal{ 0.f, 0.f, 1.f };
    float r0{ 0.1f };
    float r1{ 1.f };
    float born{ 0.f };
    float life{ 0.4f };
    float thickness{ 0.05f };
    int segments{ 24 };
    FxColour colour{ 1.f, 1.f, 1.f };
    float emissive{ 2.f };
    float alpha{ 0.9f };
    bool fade_out{ true };
    //! 輪の中の回転（ラジアン）。破線にしたいときは `dash` を 2 以上にする（2 なら 1 つおきに抜く）。
    float spin{ 0.f };
    int dash{ 1 };
    std::uint32_t seed{ 1 };
    FxPart part{ FxPart::Ring };
};
//! 足したボクセルの数を返す。
int fx_emit_ring(const FxRing &r, float t, FxFrame &out);

/* ============================================================ 画面の揺れ・光の波 */

/*!
 * @brief 画面の揺れの強さの時間変化。`start` から `seconds` 秒の間、最初に `strength` で、指数で弱まる。範囲の外は 0。
 * @details 結果は `fx_add_shake()` に渡す。強さの単位はマス（注視点の振れ幅）。0.05〜0.15 が目安。
 */
float fx_shake_envelope(float t, float start, float seconds, float strength);
//! 画面の揺れを足す（同じ時刻の複数の効果は大きいほうが残る）。
void fx_add_shake(FxFrame &out, float strength);
//! 画面側がカメラの注視点に足す揺れ（水平 2 方向。`z` は 0）。`time_ms` から作るので同じ時刻に同じ値。
Vec3 fx_shake_offset(float strength, std::uint32_t time_ms);

//! 画面全体を走る光の波 1 本を足す。`position` は 0（左端）〜 1（右端）。
void fx_add_screen_wave(FxFrame &out, const FxColour &colour, float position, float width, float alpha, float angle = 0.f);
/*!
 * @brief 視界内の全員にかかる魔法の既定の波：`seconds` 秒かけて画面の左から右へ、色 `colour` の光が走る。
 * @details 属性の `glow` で `spec.glow_mode == FxGlowMode::AllInSight` のとき呼ぶ。
 */
void fx_emit_all_sight_wave(const FxStyleCtx &ctx, FxFrame &out, const FxColour &colour, float seconds = 0.8f);

/* ============================================================ 対象の見た目の変化 */

/*!
 * @brief 対象のマス (gx,gy) の実体の変化を取り出す（無ければ作る）。返した参照に値を入れる。
 * @details 同じマスに複数の効果が掛かったときは 1 件にまとまるので、`squash = std::max(m.squash, …)` のように
 * 足し込む。時刻から決まる値（震えの位相など）を入れること。実体への適用は画面側（`world/entity_fx_mod.h`）。
 */
FxEntityMod &fx_mod_for(FxFrame &out, int gx, int gy);

/* ============================================================ 対象のタイルの写し */

/*!
 * @brief 対象のマスの実体のタイルと同じ形の板を、粒（ボクセル）に分けて出し、崩して落とす。
 * @details タイルの画素は `spec.tile_at(gx, gy, …)` で引く。引けなければ無地の四角で代用する。
 * 板は `born` から `hold` 秒そのままで、`crumble` 秒かけて粒が上から順に外れて落ち、床で止まって薄れる。
 * 対象そのものは崩さない（実体の描画は変わらない）。板は画面に正対し、対象の表面に少し浮かせる。
 */
struct FxGhost {
    int gx{ 0 };
    int gy{ 0 };
    //! 板の色。`tint` が 1 なら単色、0 ならタイルの色のまま。
    FxColour colour{ 0.55f, 0.55f, 0.58f };
    float tint{ 1.f };
    //! 横の粒の数（縦は縦横比で決まる）。
    int grid{ 16 };
    //! 板を対象の中心から画面のほうへ浮かせる量（マス）。実体の板は薄いので 0.1 で表面に重なる。厚みのある実体（検査の柱）へ重ねるなら大きくする。
    float offset{ 0.1f };
    float born{ 0.f };
    float hold{ 0.15f };
    float crumble{ 0.5f };
    float fall_life{ 0.5f };
    //! 粒の自己発光。暗い場所でも板の形が読めるよう、少し光らせる。
    float emissive{ 0.3f };
    std::uint32_t seed{ 1 };
};
//! 足した粒の数を返す。
int fx_emit_tile_ghost(const FxSpec &spec, const FxGhost &g, float t, FxFrame &out);

/* ============================================================ 文字の格子から作る形 */

/*!
 * @brief 文字の格子（`"..##.."` の行の並び。上の行から）から作る小さな形。空きは `.` と空白。他の字は 1 マスのボクセル。
 * @details 矢印・剣・手・時計・岩・どくろのような小さな形を、絵を描かずにコードの中で定義できる。
 */
struct FxShape {
    int width{ 0 };
    int height{ 0 };
    std::vector<std::string> rows;
};
FxShape fx_shape(std::initializer_list<const char *> rows);

//! 形の字ごとの色。`keys` の i 番目の字は `colours[i]`、無い字は `base`。
struct FxShapeColours {
    FxColour base{ 1.f, 1.f, 1.f };
    std::string keys;
    std::vector<FxColour> colours;
};

/*!
 * @brief 形の置き方。`right`・`up` が形の平面（画面に正対させるなら `spec.cam_right`・`spec.cam_up`）、`cell` は格子 1 つの一辺（マス）。
 * `layers` が 2 以上なら奥行きに厚みを持たせる（`depth` は 1 層の厚み）。
 */
struct FxShapePlace {
    Vec3 centre{};
    Vec3 right{ 1.f, 0.f, 0.f };
    Vec3 up{ 0.f, 0.f, 1.f };
    float cell{ 0.05f };
    float depth{ 0.05f };
    int layers{ 1 };
    float scale{ 1.f };
    //! 平面の中の回転（ラジアン）。
    float rotate{ 0.f };
    float alpha{ 1.f };
    float emissive{ 1.5f };
    FxPart part{ FxPart::Shape };
};
//! 形を置く。足したボクセルの数を返す。
int fx_emit_shape(const FxShape &shape, const FxShapeColours &colours, const FxShapePlace &place, FxFrame &out);

/* ============================================================ 絵の板 */

/*!
 * @brief 小さな絵（`assets/fx/<名前>.png`）を 3D の位置に出す。
 * @details 絵を `resolution` 以下の画素数に縮め、不透明な画素 1 つにつき小さなボクセル 1 つで出す
 * （ボクセルの世界と同じ質感になり、GL の新しい部品を要らない）。画面に正対する板になる。
 * 絵は `fx_sprite_set_dir()` で決めた場所から読み、読めなければ何も足さず 0 を返す。
 */
struct FxSprite {
    //! 名前（拡張子なし。`skull` なら `assets/fx/skull.png`）。
    const char *name{ "skull" };
    Vec3 centre{};
    //! 絵の長いほうの辺の長さ（マス）。
    float size{ 0.5f };
    //! 板の中の回転（ラジアン）。
    float rotate{ 0.f };
    FxColour tint{ 1.f, 1.f, 1.f };
    float alpha{ 1.f };
    float emissive{ 0.8f };
    //! 縮めたあとの画素の数（長いほうの辺）。大きいほど細かく、ボクセルの数が増える。
    int resolution{ 20 };
};
//! 絵のある場所（`.../assets/fx`）。起動時に画面側が決める。
void fx_sprite_set_dir(const std::string &dir);
//! 絵のある場所を返す。
const std::string &fx_sprite_dir();
//! 絵の板を足す。`spec` から画面の向きを取る。足したボクセルの数（絵が読めなければ 0）を返す。
int fx_emit_sprite(const FxSpec &spec, const FxSprite &sprite, FxFrame &out);
//! 絵 1 枚を読めるか（検査用）。
bool fx_sprite_available(const char *name);
//! PNG を読んで RGBA（左上原点）にする。読めなければ偽。
bool fx_load_png(const std::string &path, FxTileImage &out);

} // namespace hd2d
