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

#include <cstdint>
#include <vector>

namespace hd2d {

struct Camera;
struct FloorMeaning;

//! 同時に送れる点光源の数。シェーダの配列長と**必ず一致させる**（`kLightingGlsl`）。
constexpr int kMaxPointLights = 16;

/*!
 * @brief 「照らされ具合の場」(`u_lit_field`) を結ぶテクスチャ単位の既定。
 * @details ボクセル（0 面アトラス・1 パレット・2 影・3/4 画調の字）、ビルボード（0 絵・1 影・3/4 画調の字）、
 * 3D モデル（0 絵・1 影・2 遮蔽）のどれとも重ならない 5 番。
 */
constexpr int kLitFieldUnit = 5;

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

/*!
 * @brief **光の届く範囲**の調整値（`hd2d.cfg` の `light_range_pct` / `light_range_fade_ms` /
 *        `torch_flicker`。 ）。
 */
struct LightRangeParams {
    //! 強さ（1.0 = 既定）。0 で範囲の強調を出さない。
    float strength{ 1.f };
    //! 照らされ具合が目標へ寄るまでの時間（秒）。0 で即座に合わせる。
    float fade_seconds{ 0.25f };
    //! 松明（`light_kind` 1）の揺らぎを出すか。
    bool flicker{ true };
    /*!
     * @brief **いま見えていない記憶のマス**の明るさの倍率（0〜1。1 で暗くしない）。
     * @details コアが `LightingState::memory_dim` を申告したときだけ効く（Rogue 系。決めたこと 2026-10-11
     * 「見えなくなるほどにはしない。少し暗くするだけ」）。既定の 0.78 は絵を見て決めた
     * （床と壁の形・模様が十分読め、見えている床と並べてはっきり分かる暗さ）。
     */
    float memory_brightness{ 0.78f };
};

/*!
 * @brief 光源の種類（`LightingState::light_kind`）ごとの見え方。
 * @details 知らない値・0 は「従来どおり」（点光源は暖色のまま・揺らがない）。
 */
struct LightKindLook {
    //! 点光源（プレイヤの光）の色。
    float torch_r{ 1.00f };
    float torch_g{ 0.78f };
    float torch_b{ 0.48f };
    //! 照らされたマスに足す色味（範囲の強調）。
    float tint_r{ 1.00f };
    float tint_g{ 0.80f };
    float tint_b{ 0.52f };
    //! 揺らぎの幅（強さに対する割合。0 なら揺らがない）。
    float flicker{ 0.f };
    //! 揺らぎの速さ（1 = 松明。小さいほどゆっくり）。暗黒側は息をするようにゆっくり。
    float flicker_speed{ 1.f };
    /*!
     * @brief 暗黒側の光か（`light_kind` 5）。
     * @details 真なら、範囲に入ったマスを**暗く冷たい色にする**（掛け算で落とす。光を足さない）。
     * プレイヤの位置に暖かい点光源も置かない。
     */
    bool dark{ false };
};
LightKindLook light_kind_look(int light_kind);

/*!
 * @brief 光源の強さに掛ける揺らぎ（1 ± 数%）。松明（1）と暗黒側（5。ゆっくり）だけが揺らぎ、ほかは常に 1。
 * @param seconds 時刻（秒）。**呼ぶ側が持つ時計**を渡す（検査は固定の時刻を渡す）。
 * @details 周期の違う 3 つの波を重ねて、ゆっくり不規則に変える。
 */
float light_flicker_gain(int light_kind, float seconds);

/*!
 * @brief シェーダへ渡す「照らされ具合の場」（`LitRangeField::bind()` が詰める）。
 * @details `enabled` が偽なら強さ 0 で送る＝今の絵と 1 画素も変わらない。
 */
struct LitRangeView {
    bool enabled{ false };
    //! マスごとの照らされ具合（`GL_RG8`・1 マス 1 テクセル。R = 光の届く範囲、G = 記憶のマスの暗さ）。**場の持ち主が作って消す。**
    gl::GLuint texture{ 0 };
    //! テクスチャの左上のマス座標と、幅・高さ（マス）。
    float origin_x{ 0.f };
    float origin_y{ 0.f };
    float width{ 1.f };
    float height{ 1.f };
    //! 色味（光源の種類）。
    float r{ 1.f };
    float g{ 0.8f };
    float b{ 0.5f };
    //! 強さ（設定 × 揺らぎ）。
    float strength{ 0.f };
    //! 暗黒側か（真なら明るくせず、暗く冷たい色へ寄せる）。
    bool dark{ false };
    //! 記憶のマスの明るさの倍率（シェーダへは `u_mem_dark` = 1 − 倍率）。1 なら暗くしない（申告のないコアは常に 1）。
    float memory_brightness{ 1.f };
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
    //! 光の届く範囲（プレイヤの光源で照らされているマス）。既定は無効＝従来どおり。
    LitRangeView lit_range;
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
 * @brief **町の夜を明るくする**（町の表の `night_light`。Angband の町。決めたこと 2026-09-24
 *        「夜間は暗すぎるので、もうすこし周辺光を明るくし、視認性を上げよう」）。
 *
 * @param night_light 1.0 なら何もしない（従来と 1 ビットも変わらない）。1.6 で月明かり・空・
 *                    照り返しが真夜中に 1.6 倍、環境光の底も持ち上がる
 *
 * @details `make_scene_lighting()` と同じ時角から日没後の割合（高度 0〜−0.15 で 0→1）を出し、
 * その割合で効かせる——**昼は変えず、夕方から夜へ連続につなぐ**。呼ぶ側は「町の中に居て、
 * その町の表に値がある」ときだけ呼ぶ（`TerrainView::night_light`）。地下には掛けない。
 */
void apply_town_night_light(SceneLighting &lighting, const LightingState &state, float night_light);

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
/*!
 * @param torch_gain プレイヤの光の強さに掛ける倍率（`light_flicker_gain()`。既定 1 = 揺らがない）。
 * @details 松明の色は `LightingState::light_kind` で変わる（`light_kind_look()`。0 は従来の暖色）。
 */
/*!
 * @param extra_lights 呼び出し側が足す点光源（攻撃の視覚効果の火の玉・爆発。`FxFrame::lights`）。
 *        `nullptr` か空なら**従来と 1 ビットも変わらない**。
 * @details 足す光は**枠を先に確保する**（最大 `kMaxExtraPointLights` 個。溢れたらそれ以降は捨てる）。
 * 溢れて削るのは地形の光のほうで、近い順に残す。爆発の光が遠くの溶岩に押し出されて消えると、
 * 絵の主役が暗くなるため。
 */
int collect_point_lights(const GameFrame &frame, const Vec3 &camera_target, SceneLighting &out,
    float glow_scale = 1.f, const Vec3 *player_pos = nullptr, float torch_gain = 1.f,
    const std::vector<PointLight> *extra_lights = nullptr);

//! `collect_point_lights()` の `extra_lights` が確保する枠の上限。`kMaxPointLights` の内数。
constexpr int kMaxExtraPointLights = 4;

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
    //! 光の届く範囲（`u_lit_field` / `u_lit_rect` / `u_lit_tint`）。
    gl::GLint lit_field{ -1 };
    gl::GLint lit_rect{ -1 };
    gl::GLint lit_tint{ -1 };
    //! 暗黒側のとき 1（`u_lit_dark`）。
    gl::GLint lit_dark{ -1 };
    //! 記憶のマスを暗くする割合（`u_mem_dark`。1 − 明るさの倍率）。0 で素通り。
    gl::GLint mem_dark{ -1 };

    //! `program` から場所を引く。**リンク後に 1 回だけ。**
    void locate(gl::GLuint program);
};

/*!
 * @brief 光を送る。**`glUseProgram` 済みであること。**
 * @param shadow_unit シャドウマップを結ぶテクスチャ単位。
 * @param shadow_texture 影の深度テクスチャ。0 なら影を無効化する（`shadow_strength = 0`）。
 * @param shadow_side 影のテクスチャの辺（PCF の刻み幅に使う）。
 * @param lit_unit 「照らされ具合の場」を結ぶテクスチャ単位。**他のサンプラと同じ単位にしないこと**
 *        （GLES の罠）。場が無いときは 1x1 の身代わりを結ぶ。
 */
void upload_lighting(const LightUniforms &loc, const SceneLighting &lighting, const Mat4 &light_view_projection,
    gl::GLuint shadow_texture, int shadow_side, int shadow_unit, int lit_unit = kLitFieldUnit);

/*!
 * @brief **プレイヤの光源で照らされているマス**の、マスごとの照らされ具合（0〜1）。
 *
 * @details `LightingState::player_lit`（ビット列）を目標にして、**短い時間でなめらかに寄せる**
 * （既定 0.25 秒。`LightRangeParams::fade_seconds`）。範囲に入った・出たマスだけが動く。
 *
 * | 場面 | 扱い |
 * |---|---|
 * | `player_lit` が空 | 送っていない。場は無効（`active() == false`）で、絵は従来どおり |
 * | 階・町が変わった | 持っていた値を捨て、全マスを目標に**即座に合わせる** |
 * | 新しく窓に入ったマス | その場で目標に合わせる（歩いて窓が動くたびに端がふわっと光らない） |
 * | 窓の中で目標が変わったマス | 1 秒に `1 / fade_seconds` の速さで寄せる（線形） |
 *
 * 持つのは可視窓（`GameFrame::cells`）の外接矩形ぶんだけ。座標は地図の格子座標 `gx, gy`。
 *
 * CPU の部分（`update` / `value_at`）は GL を使わない。GPU への載せ替え（`bind`）だけが文脈を要る。
 */
class LitRangeField {
public:
    /*!
     * @brief フレームを取り込み、`dt_seconds` ぶん目標へ寄せる。
     * @param dt_seconds 前回からの経過（実時計の秒）。負・巨大な値は丸める。
     */
    void update(const GameFrame &frame, float dt_seconds, const LightRangeParams &params);

    //! 値を全部捨てる（次の `update` で全マスが即座に合う）。
    void reset();

    //! 場が有効か（`player_lit` が来ていて、窓が有る）。
    bool active() const { return this->active_; }

    //! マス `(gx, gy)` の照らされ具合（0〜1）。窓の外・無効のときは 0。
    float value_at(int gx, int gy) const;

    //! マス `(gx, gy)` の目標（0 か 1）。窓の外・無効のときは 0。
    float target_at(int gx, int gy) const;

    //! 記憶のマスの暗さの度合い（0〜1。`memory_dim` を申告したコアの `light_level` 1 のマスが目標 1）。窓の外・無効・申告なしは 0。
    float memory_at(int gx, int gy) const;

    //! マス `(gx, gy)` の記憶の目標（0 か 1）。
    float memory_target_at(int gx, int gy) const;

    //! `memory_dim` が申告されているか（直近の `update`）。
    bool memory_declared() const { return this->memory_declared_; }

    int origin_x() const { return this->origin_x_; }
    int origin_y() const { return this->origin_y_; }
    int width() const { return this->width_; }
    int height() const { return this->height_; }

    /*!
     * @brief テクスチャへ載せて、`out.lit_range` を詰める。**GL の文脈が要る。**
     * @param light_kind 色味を決める（`LightingState::light_kind`）。
     * @param gain 強さに掛ける倍率（`light_flicker_gain()`）。
     * @details 場が無効・強さ 0 のときは `out.lit_range.enabled = false` にして何もしない。
     */
    void bind(SceneLighting &out, const LightRangeParams &params, int light_kind, float gain);

    //! テクスチャを捨てる。**GL の文脈を消す前に必ず呼ぶ**（`release_shadow_placeholder()` と同じ理由）。
    void release();

private:
    bool active_{ false };
    bool have_previous_{ false };
    std::uint64_t floor_key_{ 0 };
    int origin_x_{ 0 };
    int origin_y_{ 0 };
    int width_{ 0 };
    int height_{ 0 };
    //! 現在値（0〜1）と、目標（0 / 1）。窓の外接矩形ぶん（行優先）。
    std::vector<float> values_;
    std::vector<std::uint8_t> targets_;
    //! 記憶のマスの現在値（0〜1）と目標（0 / 1）。`values_` と同じ並び。申告が無いときは空。
    std::vector<float> mem_values_;
    std::vector<std::uint8_t> mem_targets_;
    bool memory_declared_{ false };
    //! そのマスが今のフレームの窓に在るか。
    std::vector<std::uint8_t> present_;
    std::vector<std::uint8_t> bytes_;
    gl::GLuint texture_{ 0 };
    int texture_w_{ 0 };
    int texture_h_{ 0 };
    bool dirty_{ true };
};

/*!
 * @brief 「照らされ具合の場」の身代わり（1x1・値 0）を捨てる。**GL の文脈を消す前に必ず呼ぶ。**
 * @details 場が無いとき `u_lit_field` に結ぶ（結ばないと既定の単位 0 と相乗りして Adreno が描画を捨てる）。
 */
void release_lit_placeholder();

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
