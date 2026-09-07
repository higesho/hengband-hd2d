/*!
 * @file sky_dome.h
 * @brief 地上の空 — 天球（ドーム）に 3 層を重ねる（2026-08-11 に決めた）
 *
 * ## 何を作るか
 * 「ゲーム空間をドーム型にして、ドームに空と太陽と月と星をちりばめたテクスチャを貼り、
 * 時間に応じて回転させる」。層は 3 つで、**動き方がそれぞれ違う**のが要点である。
 *
 * | 層 | 中身 | 動き |
 * |---|---|---|
 * | 1（下） | 空の色 | 動かない。**時刻で色が変わる**（青 → 橙 → 紺 → 黒） |
 * | 2 | 星 | **北の空の一点（天の北極）を中心に回る**。1 日で 1 周 |
 * | 3（上） | 太陽・月 | **東から昇って西へ沈む**。月は太陽の反対側 |
 *
 * 3 層とも**同じドームの 1 パス**で描く。層ごとにドームを描き直すと、
 * 半透明の重ね順と深度の扱いが 3 通りに増えるわりに、絵は変わらない。
 *
 * ## 太陽の位置は `render/lighting.cpp` と**同じ式**から引く
 * 別々に持つと、影の伸びる向きと空の太陽の位置が食い違う（絵として即座に嘘に見える）。
 * `sky_state_from()` は `make_scene_lighting()` と同じ `day_minute → 時角 → 高度` を通る。
 *
 * ただし**高度の頭打ち（0.85）は掛けない**。あれは「正午に光源が真上へ来ると影が物の
 * 真下に隠れて立体感が消える」ことへの対処で、**光の都合**である。空に描く太陽まで
 * 頭打ちにすると、正午でも太陽が天頂に来ない絵になる。ずれるのは正午前後のわずかで、
 * 影の向きの食い違いとしては見えない。
 *
 * ## 地下では描かない
 * `FloorKind::Surface` 以外は `SkyState::visible == false`。空が要らないだけでなく、
 * 岩盤の外に空が見えると「地下」という前提が壊れる。
 *
 * ## 深度を書かない・**不透明を全部描いた後**に描く（2026-08-17。§15.5）
 * 以前は「`begin_scene()` の直後・地形より前に、深度テストも切って」描いていた。
 * つまり空は**毎フレーム全画素を塗る下敷き**で、そのあと地形・実体・部屋がその上を
 * 塗り直していた。ジオラマでは部屋の壁と卓が視界を覆うので、**ほぼ全画素が無駄塗り**になる
 *
 * いまは**不透明なもの（VR の部屋・地形・実体）を全部描いた後・半透明（雲）より前**に描く。
 * 頂点シェーダで z を**遠クリップ際へ貼り付け**、深度テスト LEQUAL・深度書き込みなしで
 * 「まだ誰も塗っていない画素」だけを通す。**絵は 1 画素も変わらない**——不透明の描き手は
 * 全員が深度を書くので、通る画素の集合は「空が後から上書きされなかった所」と同じである。
 *
 * 半透明（雲）より**前**なのは今までどおりで、混ざり先に空が要るため。
 * 順が換わったのは「不透明との前後」だけである。**半透明の描き手をこの前へ足すときは、
 * 空より後ろへ置くこと**（前に置くと、混ざり先が空ではなく消去色になって絵が変わる）。
 */
#pragma once

#include "frame/game_frame.h"
#include "render/gl_core.h"
#include "render/math3d.h"

#include <string>

namespace hd2d {

//! 天球の半径（マス）。**一人称と見下ろしの両方**で近クリップと遠クリップの間に入る値。
inline constexpr float kSkyDomeRadius = 50.f;

/*!
 * @brief 天の北極の傾き（度。地平線から測る）。
 * @details 星がこの軸のまわりを回る。0 だと真北の地平線が中心、90 だと天頂が中心で
 * 「北の空を中心に回る」に見えない。50° は北天の日周運動らしく見える折衷。
 */
inline constexpr float kSkyPoleAltitudeDeg = 50.f;

//! 1 フレーム分の空。**数を作るのはここだけ**（描く側は解釈しない）。
struct SkyState {
    //! 地上か。偽なら `SkyDome::draw()` は何もしない。
    bool visible{ false };
    //! 太陽の高度の正弦（−1 〜 +1。正午で +1）。色の補間の主材料。
    float altitude{ 0.f };
    //! 太陽の向き（単位ベクトル・世界座標。x = 東・y = 南・z = 上）。
    Vec3 sun_dir{ 0.f, 0.f, 1.f };
    //! 月の向き。太陽の**ちょうど反対側**（満月の配置）。
    Vec3 moon_dir{ 0.f, 0.f, -1.f };
    //! 星の回転角（ラジアン）。天の北極まわり。1 日で 1 周。
    float star_angle{ 0.f };
    //! 星の濃さ（0 = 昼で見えない / 1 = 真夜中）。
    float star_amount{ 0.f };
    //! 天頂の色（線形 HDR）。
    Vec3 zenith{ 0.05f, 0.09f, 0.22f };
    //! 地平線の色。
    Vec3 horizon{ 0.35f, 0.45f, 0.62f };
    //! 地平線より下（遠くの地面のかすみ）。
    Vec3 ground{ 0.06f, 0.06f, 0.07f };
};

/*!
 * @brief 時刻とフロアの種別から空の状態を作る。
 * @details `make_scene_lighting()`（`render/lighting.cpp`）と**同じ時角**を通る。
 * 片方だけ式を直すと、影の向きと空の太陽が食い違う。
 */
SkyState sky_state_from(const LightingState &state, const FloorIdentity &floor);

/*!
 * @brief 天球。テクスチャ（星・太陽・月）は `init()` が**その場で焼く**（絵の資産を持たない）。
 * @details 配布物を増やさずに済むこと、`tools/` の生成器を 1 本増やさずに済むことが理由。
 * 星は種を固定した疑似乱数なので、**起動のたびに同じ星空**になる。
 */
class SkyDome {
public:
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief 描く。**不透明なものを全部描いた後・雲より前**に呼ぶこと（上の注記）。
     * @param eye カメラ位置（天球はここを中心に置く）。
     * @details 深度テスト LEQUAL・深度書き込みなしで描き、**戻すところまで**面倒を見る。
     */
    void draw(const Mat4 &view_projection, const Vec3 &eye, const SkyState &sky);

private:
    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLsizei vertex_count_{ 0 };
    gl::GLuint star_tex_{ 0 };
    gl::GLuint sun_tex_{ 0 };
    gl::GLuint moon_tex_{ 0 };

    struct Uniforms {
        gl::GLint view_projection{ -1 };
        gl::GLint eye{ -1 };
        gl::GLint radius{ -1 };
        gl::GLint zenith{ -1 };
        gl::GLint horizon{ -1 };
        gl::GLint ground{ -1 };
        gl::GLint sun_dir{ -1 };
        gl::GLint moon_dir{ -1 };
        gl::GLint sun_tan_x{ -1 };
        gl::GLint sun_tan_y{ -1 };
        gl::GLint moon_tan_x{ -1 };
        gl::GLint moon_tan_y{ -1 };
        gl::GLint star_row0{ -1 };
        gl::GLint star_row1{ -1 };
        gl::GLint star_row2{ -1 };
        gl::GLint star_amount{ -1 };
        gl::GLint sun_glow{ -1 };
        gl::GLint stars{ -1 };
        gl::GLint sun{ -1 };
        gl::GLint moon{ -1 };
    } loc_;
};

} // namespace hd2d
