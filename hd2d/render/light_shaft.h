/*!
 * @file light_shaft.h
 * @brief 上空から差し込む**光**（陽だまり。2026-08-22 に決めた）。
 *
 * ## 何のためのものか
 * 2026-08-22 に決めた「陽だまりには上空から差し込む光の表現を。
 * 薄めで半透明の光の差し込みを」。同日の作り直し:
 * 「天使の階段はマスの幅いっぱいで平行四辺形に落ちる光で。マスから 2 ブロックくらいは
 * 光で満ちていて、そこから上は左右を残して中央が減水していく感じに」。
 *
 * 陽だまり（Sil-Q の `patch of sunlight`）は初版で**わざと柱を立てなかった**——
 * ボクセルは不透明なので（`voxel_renderer.cpp` に混色の引数が無い）、柱を立てると
 * 陽の筋ではなく**白い角柱**がマスを塞ぐ。差し込む光は床の輪と `emissive` だけで見せていた
 * （`assets/voxel/terrain_prefabs.jsonc` の `SQ_SUNLIGHT` の註記）。
 *
 * **その判断はボクセルで作る限り正しい。** ここはボクセルを使わない——
 * 半透明の面を 1 枚立てて**加算で混ぜる**。加算なので後ろのものを消さず、
 * 薄く重なるほど明るくなる。光の表現としてはこちらが素直である。
 *
 * ## 形（2026-08-22 に決めた の作り直し）
 *
 * ```
 *    |       |      <- 上端: 左右だけが残り、そこで 0 へ
 *    |   .   |
 *    |  ...  |      <- 上へ行くほど中央が薄くなる（「減水」）
 *    |#######|      <- 下 `full`（既定 2 ブロック）までは**満ちている**
 *    +-------+      <- 床。マスの幅いっぱい。全体が `slant` で傾き、上ほど少し狭い
 * ```
 *
 * - **マスの幅いっぱい**（`radius` 0.5 ＝ 1 マス）。細い筋を並べる形は 1 度作って捨てた
 *   ——「マスの幅いっぱいで平行四辺形に」という指示がその答えである
 * - **斜めに落ちる**。上端を水平にずらす（`slant`）。垂直に立てると差し込みではなく柵に見える
 * - **上へ行くほど少しだけ狭い**（`taper`）。完全な平行四辺形は柵の板に見えた
 *   （2026-08-22 に決めた「上まで完全に平行はやめて。少しだけ狭めてみて」）
 * - **下 `full` ブロックまでは満ちている**（濃さが一様）。ここが「光の中」である
 * - **そこから上は中央から抜ける**。左右の縁は残るので、上へ行くほど 2 本の筋に見える。
 *   いちばん上は α が 0 まで落ちる（**切り口を見せない**）
 * - 面は**軸拘束のビルボード**（上は世界の +Z 固定、横はカメラの方位へ正対）。
 *   字の板（`glyph_atlas`）と同じ流儀で、見下ろし・一人称・視点回転・VR のどれでも
 *   追加の向き管理が要らない。**カメラを向くので「横から見たら消える」が起きない**
 * - **加算で混ぜる**（`GL_SRC_ALPHA, GL_ONE`）。α 混合だと霧に見え、
 *   後ろの床の色を殺してしまう
 * - **深度は読むが書かない。** 壁の向こうの光は隠れてほしいが、
 *   光が後ろのものを隠してはいけない
 *
 * ## 光を「浴びている床」は別の仕掛けである
 * 床の側の明るさは対応表の `emissive` が持っている（`SQ_SUNLIGHT`）。
 * ここが足すのは**空間に浮かぶ光**だけ。片方だけにすると
 * 「光っている床の上に何も無い」か「宙に浮いた光の下が暗い」になる。
 */
#pragma once

#include "render/gl_core.h"
#include "render/math3d.h"

#include <cstddef>
#include <string>

namespace hd2d {

//! 差し込み 1 マスぶん。位置は**床の中心**（マスの中心・床の高さ）。
struct LightShaftInstance {
    float x{};
    float y{};
    float z{};
    //! 幅の半分（マス単位）。**マスの形に合わせる**ので既定は 0.5（＝1 マスいっぱい）。
    float radius{ 0.5f };
    float height{ 3.4f }; //!< 立ち上がる高さ（マス単位）
    //! **満ちている高さ**（マス単位）。ここまでは濃さが一様で、上は中央から抜ける。
    float full{ 2.f };
    //! 上端の水平のずれ（マス単位）。**斜めに落とす**ぶん。
    float slant_x{ 0.95f };
    float slant_y{ 0.30f };
    /*!
     * @brief 上端の幅（床での幅に対する比）。**1 未満で上へ行くほど狭くなる。**
     * @details 2026-08-22 に決めた「上まで完全に平行はやめて。少しだけ狭めてみて」。
     * 完全な平行四辺形は柵の板に見えたので、少しだけすぼめる。
     * **`slant_x` `slant_y` の直後に置くこと**——3 つまとめて `vec3` で採っている。
     */
    float taper{ 0.82f };
    float r{ 1.f };
    float g{ 0.96f };
    float b{ 0.80f };
    float a{ 0.22f }; //!< 濃さ。**薄めにする**（決めたこと）
};

class LightShaftRenderer {
public:
    bool init(std::string &err);
    void shutdown();
    bool ready() const { return this->program_ != 0; }

    /*!
     * @brief 描く。**不透明な世界を全部描いた後**に呼ぶこと。
     * @param azimuth カメラの方位（ラジアン）。面をこちらへ正対させる。
     */
    void draw(const Mat4 &view_projection, float azimuth, const LightShaftInstance *instances,
        std::size_t count);

private:
    void upload_instances(const LightShaftInstance *instances, std::size_t count);

    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLuint instance_vbo_{ 0 };
    std::size_t instance_capacity_{ 0 };
    gl::GLint loc_view_projection_{ -1 };
    gl::GLint loc_right_{ -1 };
};

} // namespace hd2d
