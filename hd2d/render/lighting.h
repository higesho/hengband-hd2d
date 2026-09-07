/*!
 * @file lighting.h
 * @brief 光の状態 — 方向光・環境光・点光源（P5 ①②③）。
 *
 * プロトコルは §12-4・§12-5、
 * 段取りは P5。
 *
 * ## ここに全部集める理由
 * 光の式が**ボクセルの陰影とビルボードの陰影で違うと、同じ場所に立つ壁と人物の
 * 明るさが食い違う**。P4 まではビルボードだけ「一定の明るさ」だったので、影が入ると
 * その食い違いが目に見えるようになる。
 *
 * したがって
 *
 * | | |
 * |---|---|
 * | 式 | `kLightingGlsl`（**文字列 1 本**。両方のフラグメントシェーダが連結して使う） |
 * | 値 | `SceneLighting`（1 フレーム 1 個。両方のプログラムへ同じものを送る） |
 * | 送り出し | `LightUniforms`（プログラムごとに場所を 1 回だけ引く） |
 *
 * ## 決めたこと（設計書に無いので、ここに理由ごと書く）
 *
 * **地下にも方向光を置く。**設計書 §13 は方向光 1 灯としか言っていないが、地下には
 * 太陽が無い。それでも完了条件は「**接地影が落ち**、暗いダンジョンで松明が丸く効く」
 * である。点光源に影を落とさせるには 6 面のシャドウマップが要り、P5 の割に合わない。
 * そこで**地下では「上からやや南寄りの弱い方向光」**を置き、これを接地影の光源にする。
 * 物理ではなく約束である。明るさは地上の 1/4 ほどにして、主役は松明に譲る。
 *
 * **点光源は影を落とさない。**上と同じ理由（コスト）。松明の丸い減衰は落ちるが、
 * 松明が壁の向こうを照らす。ダンジョンでは視界そのものがコア側で切られていて
 * 壁の向こうのマスは描かれないので、実害は「角のすぐ裏が明るい」程度に留まる。
 */
#pragma once

#include "frame/game_frame.h"
#include "render/gl_core.h"
#include "render/math3d.h"

#include <vector>

namespace hd2d {

struct Camera;
struct FloorMeaning;

//! 同時に送れる点光源の数。シェーダの配列長と**必ず一致させる**（`kLightingGlsl`）。
constexpr int kMaxPointLights = 16;

//! 点光源 1 つ。半径はマス単位で、**そこで完全に 0 になる**（コアの光源半径に合わせる）。
struct PointLight {
    float x{};
    float y{};
    float z{};
    float radius{ 1.f };
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    float intensity{ 1.f };
};

//! 1 フレーム分の光。**ボクセルにもビルボードにも同じものを渡す。**
struct SceneLighting {
    //! 方向光の向き。**面から光源へ**（シェーダの `dot(n, l)` がそのまま使える向き）。
    Vec3 sun_dir{ 0.40f, 0.52f, 0.80f };
    Vec3 key_color{ 1.30f, 1.18f, 0.96f };
    Vec3 sky_color{ 0.60f, 0.68f, 0.86f };
    Vec3 bounce_color{ 0.86f, 0.74f, 0.58f };
    //! 環境光の倍率。夜と地下で落とす。
    float ambient_scale{ 1.f };
    //! 影の濃さ（0 = 影を落とさない / 1 = シャドウマップのまま）。
    float shadow_strength{ 1.f };
    std::vector<PointLight> points;
};

/*!
 * @brief 時刻とフロアの種別から、太陽と環境光を決める（§12-4）。
 * @details 設計書は「昼夜は**単項目あたりの効果が最大**」と言っている。2 値ではなく
 * 分（`LightingState::day_minute`）から連続に作るので、夕暮れが階段状に飛ばない。
 */
SceneLighting make_scene_lighting(const LightingState &state, const FloorIdentity &floor);

/*!
 * @brief **明るい部屋に居るあいだ、地下の環境光を上げる**（2026-08-20 に決めた）。
 *
 * @param mix 0 = 通路や暗い部屋のまま / 1 = 明るい部屋。**呼ぶ側がなめらかに動かす**
 *            （部屋の出入りで明るさが 1 フレームで飛ばないように）
 *
 * @details コアは「このマスは自分で明るい」を `CAVE_GLOW`（画面側の `CELL_FEAT_GLOWING`）で
 * 言ってくる。**これは光源ではない**——`CELL_FEAT_GLOW`（店の灯り・溶岩）と違い、
 * 明るい部屋のマスすべてに立つ印である。だから点光源や自発光を置いてはいけない
 * （置くと部屋じゅうが電球になって床が白く飛ぶ。Sil-Q の M0 検証で実際にそうなった）。
 * 正しい写し方は**環境光**で、ここがその 1 か所である。
 *
 * 方向光（`key_color`）には触らない——上げると陰影が消えて立体が平たくなる。
 * 上げるのは底（`ambient_scale`）と、空と照り返しの色だけ。地上には掛けない
 * （あちらの明るさは時刻が決めており、`CAVE_GLOW` は日向の印になってしまう）。
 */
void apply_lit_room_ambient(SceneLighting &lighting, const FloorIdentity &floor, float mix);

/*!
 * @brief @ の居るマスが「明るい部屋」か（`CELL_FEAT_GLOWING`）。
 * @return 立っていれば 1.0、そうでなければ 0.0。@ が見つからなければ 0.0
 * @details `apply_lit_room_ambient` の `mix` の目標値。**なめらかに動かすのは呼ぶ側**。
 */
float lit_room_target(const GameFrame &frame);

/*!
 * @brief フレームから点光源を集める（§12-5）。
 *
 * @param camera_target 注視点。**近い順に採るための基準**（`kMaxPointLights` で溢れるため）。
 * @param[in,out] out `points` を詰める（既存の中身は捨てる）。
 *
 * @details 拾うのは 3 つ。
 * | 元 | 色 | 半径 |
 * |---|---|---|
 * | プレイヤの松明（`LightingState::light_radius`） | 暖色 | 光源半径そのまま |
 * | 溶岩（`CELL_FEAT_LAVA`） | 橙 | 2.5 マス |
 * | 発光地形（`CELL_FEAT_GLOW`） | 淡い暖色 | 3.0 マス |
 *
 * 町の窓（§13）は**まだ拾えない**。建物の中に光源があるという情報がプロトコルに無く、
 * §10 の敷地の意味づけ自体が未着手だからである（引き継ぎ §5-5）。
 *
 * @note 溢れたぶんは**黙って捨てない**。捨てた数を戻り値で返す。
 * @return 溢れて捨てた光源の数。
 *
 * @param player_pos プレイヤの**なめらかな位置**（マス。`nullptr` ならマスの中心）。
 * @details 松明だけはここを見る。マスの中心（整数 + 0.5）に置くと、
 * **絵はなめらかに流れているのに光だけが 1 マスずつ飛ぶ**——一人称ではそれが
 * 明暗のちらつきになり、酔いに直結する（2026-08-14 に気づいた。
 *）。溶岩と発光地形は地形なのでマスのままでよい。
 */
int collect_point_lights(const GameFrame &frame, const Vec3 &camera_target, SceneLighting &out,
    float glow_scale = 1.f, const Vec3 *player_pos = nullptr);

/*!
 * @brief 光の式（GLSL）。**両方のフラグメントシェーダがこれを連結して使う。**
 * @details `#version` 行は含まない（連結する側が先頭に置く）。
 * @note **トーンマップは入っていない**（P7 で `kTonemapGlsl` へ出た）。物のシェーダは
 * 線形の HDR を書き、掛けるのは合成の 1 か所だけである。理由は `lighting.cpp` の注記。
 */
extern const char *const kLightingGlsl;

/*!
 * @brief トーンマップだけの GLSL。**合成（`post_process.cpp`）だけが使う。**
 * @details ブルームの閾値抽出がトーンマップ前の値を見る必要があるため、P7 で
 * `kLightingGlsl` から分けた。`#version` 行は含まない。
 */
extern const char *const kTonemapGlsl;

/*!
 * @brief `hd2d_tonemap()` と**同じ式**を CPU で（1 成分）。
 * @details 検査が「画面のこの画素はいくつになるはず」を計算するのに要る。
 * 式が 2 か所にあるのは危ないので、`--post-check` の (2) が GPU の結果と突き合わせる。
 */
float hd2d_tonemap_cpu(float lit);

//! 光のユニフォームの場所。プログラムごとに 1 回だけ引く。
struct LightUniforms {
    gl::GLint light_dir{ -1 };
    gl::GLint key_color{ -1 };
    gl::GLint sky_color{ -1 };
    gl::GLint bounce_color{ -1 };
    gl::GLint ambient_scale{ -1 };
    gl::GLint shadow_strength{ -1 };
    gl::GLint point_count{ -1 };
    gl::GLint point_pos_radius{ -1 };
    gl::GLint point_color{ -1 };
    gl::GLint light_view_projection{ -1 };
    gl::GLint shadow_map{ -1 };
    gl::GLint shadow_texel{ -1 };

    //! `program` から場所を引く。**リンク後に 1 回だけ。**
    void locate(gl::GLuint program);
};

/*!
 * @brief 光を送る。**`glUseProgram` 済みであること。**
 * @param shadow_unit シャドウマップを結ぶテクスチャ単位。
 * @param shadow_texture 影の深度テクスチャ。0 なら影を無効化する（`shadow_strength = 0`）。
 * @param shadow_side 影のテクスチャの辺（PCF の刻み幅に使う）。
 */
void upload_lighting(const LightUniforms &loc, const SceneLighting &lighting, const Mat4 &light_view_projection,
    gl::GLuint shadow_texture, int shadow_side, int shadow_unit);

/*!
 * @brief 影の身代わりテクスチャを捨てる。**GL の文脈を消す前に必ず呼ぶ。**
 * @details 身代わりは描画の途中で作られ、**関数の中の静的な変数**で持っている。
 * `run()` はコアを起こし直すたびに出入りし、そのたびに GL の文脈を作り直すので、
 * 捨てないと**死んだ文脈の名前が次の回へ残る**——結ぶたびに
 * `GL_INVALID_OPERATION: Texture name does not refer to a texture object generated by OpenGL`
 * が出る（2026-08-25 の実機で 1 回につき 1 万件を超えた。SH-36）。
 * 捨てたあとは、次に要ったときにその文脈で作り直される。
 */
void release_shadow_placeholder();

} // namespace hd2d
