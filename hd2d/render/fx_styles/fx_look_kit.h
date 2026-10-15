/*!
 * @file fx_look_kit.h
 * @brief 変愚に無い属性（Frox・幻想蛮怒）の見た目を、表（`Entry`）と「見た目 1 つ」の関数で作るための共通の枠。
 *
 * `fx_frox.cpp`（`fc_` で始まる名前）と `fx_gensoband.cpp`（`gb_` で始まる名前）が使う。呼び分けは `fx_spirit.cpp` と同じ作りで、
 * 属性ごとに「見た目 1 つ」（`LookFn`。対象のマス・始まりからの秒・大きさから絵を作る）を書き、撃ち方の形ごとに次のように使う。
 * - 飛ばない魔法・足元だけ：対象のマスに見た目を 1 つ。視界内の全員は属性の色の光の波（`allsight` を結べば専用の絵）。
 * - ボルト・ボール：飛んでいる間は属性の色の玉（`flight` を結べば専用の姿）。着弾で見た目（ボールは範囲のマスごとに、
 *   中心は全部、周りは小さな版を距離ぶん遅らせて。マスが多いとき・ブレスは靄だけ）。
 * - ビーム：経路に光の線（`beam` を結べば専用の絵）、終点に見た目。直接（ジャンプ）はボールの着弾と同じ。
 *
 * **原作より情報を増やさない**（効いたか・耐えたかで絵を変えない）。関数は状態を持たない（`voxel_fx.h` の方針）。
 */
#pragma once

#include "render/fx_styles/fx_parts.h"
#include "render/fx_styles/fx_style.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace hd2d {
namespace looks {

constexpr float kPi = 3.14159265f;

struct Entry;

//! 見た目 1 つの文脈。`te` は始まりからの秒、`scale` は大きさ（範囲の周りのマスの小さな版は 0.62）。
struct Look {
    const FxSpec &spec;
    const Entry &e;
    int gx;
    int gy;
    Vec3 foot;  //!< マスの中心（高さ 0）
    Vec3 front; //!< 体の前（画面のほうへ少し出した位置。高さ 0）。壁や厚みのある実体に隠れないよう、体の中心に出す粒はここへ置く
    float te;
    float scale;
    bool mini; //!< 範囲の周りのマスに出す小さな版（数を減らす）
    std::uint32_t seed;
};

using LookFn = void (*)(const Look &, FxFrame &);
//! 飛んでいる間の姿（ボルト・ボール）。
using FlightFn = void (*)(const FxStyleCtx &, const Entry &, FxFrame &);
//! ビームの光の線の代わり。`ctx.t` は撃ってからの秒、`t_end` は終点に着く秒。
using BeamFn = void (*)(const FxStyleCtx &, const Entry &, float t_end, FxFrame &);
//! 視界内の全員にかかる魔法の絵（`ctx.t` は撃ってからの秒）。
using AllSightFn = void (*)(const FxStyleCtx &, const Entry &, FxFrame &);

enum EntryFlag : unsigned {
    kNone = 0,
    //! ビームの光の線を引かない（`beam` が全部描く）。
    kNoBeamLine = 1u << 0,
    //! 範囲の周りのマスも、小さな版ではなく同じ大きさで出す（範囲が小さいときだけ。大地震など）。
    kWholeArea = 1u << 1,
    //! 範囲のマスが多くても靄に落とさない（画面全体の絵）。
    kNoMistFallback = 1u << 2,
};

struct Entry {
    const char *attr;
    //! 汎用の色（玉・光の波・靄）。
    FxColour base;
    float smoke;
    float emissive;
    //! 見た目の第 2・第 3 の色。
    FxColour tint;
    FxColour tint2;
    LookFn look;
    //! 見た目 1 つの長さ（秒）。この間に全部消える。
    float seconds;
    int level;
    FlightFn flight;
    BeamFn beam;
    //! `beam` の動きの長さ（秒。終点に着いたあとの分を含めた、光の線が出ている長さ）。
    float beam_seconds;
    AllSightFn allsight;
    float allsight_seconds;
    unsigned flags;
};

//! 表を登録表に足し、呼び分けの関数を結ぶ。`family` は `registry.set_family()` で決めてから呼ぶ。
void register_entries(FxStyleRegistry &registry, const Entry *entries, std::size_t count);

/* ------------------------------------------------------------ 小さな道具（見た目の関数が使う） */

Look make_look(const FxSpec &spec, const Entry &e, int gx, int gy, float te, float scale, bool mini, std::uint32_t seed, float forward = 0.22f);
int count_of(const Look &L, int n);
//! 立方体 1 つ。
void put(FxFrame &out, const Vec3 &pos, float size, const FxColour &c, float emissive, float alpha, FxPart part = FxPart::Particle);
//! 向きのある細長い立方体（長さ `len`、太さ `w`）。向きは進む向き `dir`（単位ベクトル）。
void put_bar(FxFrame &out, const Vec3 &centre, const Vec3 &dir, float len, float w, const FxColour &c, float emissive, float alpha,
    FxPart part = FxPart::Particle);
//! 画面の右へ `x`、上へ `y` のずれ（世界座標）。
Vec3 on_screen(const FxSpec &spec, float x, float y);
void add_light(FxFrame &out, const Vec3 &p, float radius, const FxColour &c, float intensity);
float ease_out(float u);
FxColour lerp_colour(const FxColour &a, const FxColour &b, float t);
Vec3 unit_or(const Vec3 &v, const Vec3 &fallback);
//! 文字の格子の形を、画面に正対させて置く。`keys`/`colours` は字ごとの色（無い字は `base`）。
void glyph(const Look &L, FxFrame &out, const FxShape &shape, const Vec3 &centre, float cell, float alpha, const FxColour &base, float emissive = 1.6f, int layers = 1,
    float rotate = 0.f, const std::string &keys = std::string(), std::initializer_list<FxColour> colours = {});
//! 輪。`flat` が真なら床に水平、偽なら画面に正対。
void ring_at(const Look &L, FxFrame &out, bool flat, const Vec3 &centre, float r0, float r1, float born, float life, float thickness, const FxColour &c,
    float emissive = 1.8f, float alpha = 0.9f, int segments = 22, int dash = 1, float spin = 0.f);
//! 範囲のマスの靄（マスが多いとき・ブレス）。
void cell_mist(const FxSpec &spec, const Entry &e, int gx, int gy, int index, float te, FxFrame &out);
//! 経路の上の位置と進む向き（`s` は 0〜1）。
struct FlightPose {
    Vec3 pos;
    Vec3 dir;
    float s;
};
FlightPose flight_pose(const FxStyleCtx &ctx);
//! 飛んでいる玉の印（検査と画面が見る）。
void mark_projectile(FxFrame &out, const Vec3 &pos, const Vec3 &dir);
//! 属性の色の小さな玉（既定の飛んでいる姿）。`big` は大きさの倍率。
void small_orb(const FxStyleCtx &ctx, const Entry &e, float big, FxFrame &out);
//! 床の高さ（z）にある、画面の外へ出ない範囲の中心（床の汚れ・紋様に使う）。
Vec3 floor_point(const Look &L, float z = 0.03f);
//! 床の平たい点の列（線の一部）。
void floor_dots(FxFrame &out, const Vec3 &a, const Vec3 &b, float fraction, float step, const FxColour &c, float emissive, float alpha);
//! 折れ線に沿った太い線（ビームの代わりに使う）。`a`→`b` を `w` の太さで、`alpha`・`emissive` で描く。
void beam_bar(FxFrame &out, const Vec3 &a, const Vec3 &b, float w, const FxColour &c, float emissive, float alpha, float visible = 1.f,
    FxPart part = FxPart::BeamCore);
//! ビームの経路の最初と最後（無ければ撃った者と対象）。
void beam_ends(const FxSpec &spec, Vec3 &from, Vec3 &to);
//! 経路の長さ（マス）。
float beam_length(const FxSpec &spec);

//! 撃った者から対象への水平の向き（実体の「のけぞり」・押し出しに使う）。同じマスなら下向き。
void facing_away(const Look &L, float &ax, float &ay);
//! 画面を揺らす（範囲の周りの小さな版では揺らさない）。
void shake_for(const Look &L, FxFrame &out, float start, float seconds, float strength);
//! 垂直の柱（立方体を積む）。`z0`〜`z1`、一辺 `size`。上へ行くほど薄れる（`fade_top` が 0 ならそのまま）。
void vertical_column(FxFrame &out, float x, float y, float z0, float z1, float size, const FxColour &c, float emissive, float alpha, float fade_top = 0.f,
    FxPart part = FxPart::Wave);
//! 点 `a`〜`b` を結ぶ線上に小さな立方体を等間隔に並べる（細い線。向きを持たないので太さが一定）。
void dotted_line(FxFrame &out, const Vec3 &a, const Vec3 &b, float step, float size, const FxColour &c, float emissive, float alpha, float visible = 1.f,
    FxPart part = FxPart::Particle);

} // namespace looks
} // namespace hd2d
