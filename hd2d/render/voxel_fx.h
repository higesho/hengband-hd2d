/*!
 * @file voxel_fx.h
 * @brief 攻撃の視覚効果（矢・弾・投げた物・魔法の玉・爆発・ビーム・ブレス）を**ボクセルの並び**として作る（CPU の部分）。
 *
 * ゲームからの情報（`ProjectionFx`・`MissileFx`）は
 * `render/attack_fx_view.h` が `FxSpec`（撃ち方 1 回）にして、ここへ渡す。
 *
 * ## 作りの方針
 * - **GL に触らない。**ボクセルの一覧（`FxVoxel`）・点光源（`PointLight`）・板（`FxSlab`）を返すだけ。
 *   描くのは `render/voxel_fx_renderer.h`。数で確かめる検査もこの層だけで閉じる。
 * - **状態を持たない。**`fx_evaluate(撃ち方, 経過秒)` は同じ入力に同じ出力を返す。
 *   火の粉の尾も爆発の飛び散りも、種（`seed`）と時刻から毎回計算し直す。
 *   だから検査が「時刻 0.3 秒」を直接指せるし、GIF を撮り直しても同じ絵になる。
 * - **ボクセルは軸に沿った立方体（直方体）**。矢のように向きを持つ物は、全部の立方体に
 *   同じ `yaw`/`pitch` を入れて、矢の座標系ごと回す。
 *
 * ## 種類と時間の流れ
 * | 種類 | 飛ぶ | 当たってから |
 * |---|---|---|
 * | 矢（Arrow） | `arrow_speed`（20 マス/秒）。軽い放物線 | 刺さって `arrow_stick_seconds` 残り、薄れて消える |
 * | クロスボウの矢（Bolt） | `bolt_speed`（28 マス/秒）。**まっすぐ（高さ一定）**。矢より短く太い | 刺さって `bolt_stick_seconds` 残る |
 * | 弾（Shot） | `shot_speed`（24 マス/秒）。小さな石の玉が回る | その場で消える |
 * | 投げた物・ブーメラン | `thrown_speed`（14 マス/秒）。**物の板（ボクセルのスラブ）**が面内で回る | その場で消える。ブーメランは戻りの道を戻ってくる |
 * | 魔法の玉（Orb） | `orb_speed`（12 マス/秒）。まっすぐ。属性の色（火は試作の火の玉） | ボールは爆発、ボルトは小さく弾ける |
 * | ビーム | 経路の全部のマスに光の線（始点から順に点き、短く光って消える） | 終点で小さく弾ける |
 * | ブレス | 玉は飛ばない。撃った者に近いマスから順に爆発が広がる | |
 * | 直接の爆発（Jump） | その場に爆発 | |
 * | 飛ばない魔法（Glow） | 対象の上に小さな光が浮かんで消える | |
 *
 * ## 属性ごとの見た目（スタイル）
 * 魔法の玉・爆発・ビーム・ブレス・飛ばない魔法は、属性の名前（`FxSpec::attr`）から引いた
 * **スタイル**（`render/fx_styles/fx_style.h`）の関数が作る。スタイルは系統ごとのファイル
 * （`render/fx_styles/fx_liquid.cpp` など）に登録してあり、関数が無い（`nullptr`）段階は
 * この層の汎用の見た目（`fx_generic_*`）になる。矢・弾・投げた物はスタイルを通らない。
 *
 * ## 爆発は範囲のマスに収まる
 * 爆発のボクセルは `FxSpec::cells`（ゲームが教えた効果範囲のマス）の**中だけ**に出る。1 粒ごとに
 * 「そのマスの中心から ±(0.5 − 粒の半分)」へ押し込むので、範囲が 1 マスなら爆発も 1 マスに収まる。
 * 外へ広がって見えるのは、中心からの距離（`ring`）ごとに `ring_delay` 秒ずつ遅らせて始めるため
 * （原作の描く順番。`effect/effect-processor.cpp`）。
 */
#pragma once

#include "render/lighting.h"
#include "render/math3d.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hd2d {

//! ボクセルがどの部品のものか。検査と絵合わせが数えるための印で、描き方には効かない。
enum class FxPart : std::uint8_t {
    Other = 0,
    ArrowHead,   //!< 矢じり（金属。クロスボウの矢も同じ）
    ArrowShaft,  //!< 矢柄（木）
    ArrowFletch, //!< 羽根
    ShotStone,   //!< スリングの弾の石
    ThrownBox,   //!< 投げた物の板が引けなかったときの小さな箱
    OrbCore,     //!< 魔法の玉の本体
    OrbTail,     //!< 玉のすぐ後ろの炎
    Ember,       //!< 火の粉の尾（玉・属性付きの矢弾）
    BurstMain,   //!< 爆発の粒（属性の色。火なら火の色）
    BurstSmoke,  //!< 爆発の煙
    BurstFlash,  //!< 爆発の瞬間の閃光
    BeamCore,    //!< ビームの芯
    BeamGlow,    //!< ビームの周りの光（半透明）
    GlowMote,    //!< 飛ばない魔法の光
    BrandGlow,   //!< 属性付きの矢弾の周りの光
    // ---- ここから下は属性ごとの見た目（`render/fx_styles/`）が使う。検査が数えるための印
    Particle,    //!< 粒（`fx_emit_particle`）
    Droplet,     //!< 液体の雫
    Puddle,      //!< 床の水たまり・紋様（`fx_emit_puddle`）
    Mist,        //!< 煙・霧（`fx_emit_smoke`）
    Spark,       //!< 火花
    Debris,      //!< 破片・礫・雹
    Bolt,        //!< 折れ線の光（稲妻。`fx_emit_lightning`）
    BoltGlow,    //!< 折れ線の周りの光（半透明）
    Ring,        //!< 広がる輪・縮む輪（`fx_emit_ring`）
    Shape,       //!< 文字の格子から作った形（`fx_emit_shape`）
    Sprite,      //!< 絵の板（`fx_emit_sprite`）
    Ghost,       //!< 対象のタイルの写し（`fx_emit_tile_ghost`）
    Wave,        //!< 光の波（`fx_emit_world_wave`）
};

//! ボクセル 1 個。位置は世界座標（マス。x 東・y 南・z 上）、大きさは各軸の一辺（マス）。
struct FxVoxel {
    Vec3 center{};
    Vec3 size{ 0.1f, 0.1f, 0.1f };
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    //! 不透明度。1 なら不透明（深度を書く）、未満なら半透明（奥から手前の順に混ぜる）。
    float alpha{ 1.f };
    //! 自己発光の強さ。色 × この値が、光に関係なく足される（線形の HDR）。
    float emissive{ 0.f };
    //! 立方体の向き（ラジアン）。`yaw` は z 軸まわり、`pitch` は上向きが正。0 なら軸に沿う。
    float yaw{ 0.f };
    float pitch{ 0.f };
    FxPart part{ FxPart::Other };
};

/*!
 * @brief 投げた物の板（ボクセルのスラブ）1 枚。**GL の絵はここでは作らない**（描くのは `draw_fx_slab`）。
 * @details 床のアイテムと同じ板（`SlabLibrary`）を、`item_id` から引いて描く。板は面内で `spin` だけ回る。
 * 向きは画面へ正対（`face_yaw`/`face_pitch`＝床の実体の板と同じ値）。
 */
struct FxSlab {
    std::uint16_t item_id{ 0 };
    //! 板の中心の世界座標。
    Vec3 centre{};
    //! 板の拡大率（床のアイテムと同じ 0.7）。
    float scale{ 0.7f };
    //! 板の面内の回転（ラジアン）。
    float spin{ 0.f };
    float face_yaw{ 0.f };
    float face_pitch{ 0.f };
};

struct FxStyle;

//! `FxKind::Glow` の種類。
enum class FxGlowMode : std::uint8_t {
    Target,     //!< 対象の上で効果を出す（飛ばない魔法）
    AllInSight, //!< 視界内の全員にかかる魔法（画面全体の効果にまとめる）
    Touch,      //!< 足元・周囲にだけ効く魔法
};

enum class FxKind : std::uint8_t {
    Arrow,     //!< 弓の矢（少し弧）
    Bolt,      //!< クロスボウの矢（まっすぐ）
    Shot,      //!< スリングの弾
    Thrown,    //!< 投げた物
    Boomerang, //!< 戻ってくる投擲
    Orb,       //!< 魔法の玉（ボルト・ボール）
    Beam,      //!< ビーム
    Breath,    //!< ブレス
    Burst,     //!< その場の爆発（ジャンプ）
    Glow,      //!< 飛ばない魔法の小さな光
};

//! RGB（線形）。
struct FxColour {
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
};

/*!
 * @brief 対象（そのマスにいるモンスターまたはプレイヤ）の見た目の一時的な変化。
 * @details 属性の見た目が `FxFrame::entity_mods` へ足し（`fx_mod_for()` で同じマスは 1 件にまとまる）、
 * 画面側が実体の描画（ボクセル板・字の板・プレイヤのモデル）に当てる（`render/fx_styles/fx_entity_mod.h`）。
 * 時刻から決まる値を入れるので状態は持たない。
 */
struct FxEntityMod {
    int gx{ 0 };
    int gy{ 0 };
    //! 押しつぶし（0〜1）。上下に縮み、横に少し広がる。1 で高さが 4 割になる。
    float squash{ 0.f };
    //! 小刻みな震え（マス。振幅）と、今の位相（ラジアン）。横方向に揺れる。
    float tremble{ 0.f };
    float tremble_phase{ 0.f };
    //! のけぞり（0〜1）。`away_x/away_y`（水平・正規化）の向きへ少し下がり、後ろへ傾く。
    float recoil{ 0.f };
    float away_x{ 0.f };
    float away_y{ 1.f };
    //! 浮き上がり（マス）。
    float lift{ 0.f };
    //! 灰色に染める（0〜1）。
    float grey{ 0.f };
    //! 二重にずらす：0 でなければ、同じ実体をこの量（マス）だけずらした複製をもう 1 つ描く。
    float echo_x{ 0.f };
    float echo_y{ 0.f };
};

//! 画面全体を走る光の波（`kProjAllInSight`）。`position` は 0（左端）〜 1（右端）、`width` は画面幅に対する割合。
struct FxScreenWave {
    FxColour colour{};
    float position{ 0.f };
    float width{ 0.2f };
    float alpha{ 0.f };
    //! 波の進む向き（画面上。ラジアン。0 で左から右）。
    float angle{ 0.f };
};

//! 対象のタイルの画素（左上原点・RGBA 各 8 ビット）。分解のように「対象のタイルと同じ形」が要るときに使う。
struct FxTileImage {
    int width{ 0 };
    int height{ 0 };
    std::vector<std::uint8_t> rgba;
    //! 実体の板の大きさ（マス）。画面側が分かるときは入れる。既定は 0.8 × 0.8。
    float width_cells{ 0.8f };
    float height_cells{ 0.8f };
};

/*!
 * @brief 属性ごとの色の組。**火だけは試作で決めた値をそのまま持つ**（`fx_palette_for("fire")`）。
 * 他の属性は基準の色 1 つから段階を導く（`fx_palette_from_base`）。属性ごとの見た目は後で作る。
 */
struct FxPalette {
    //! 玉：中心→中間→外縁の色と自己発光。
    FxColour orb[3]{ { 1.0f, 0.95f, 0.62f }, { 1.0f, 0.58f, 0.14f }, { 0.86f, 0.16f, 0.04f } };
    float orb_emissive[3]{ 4.2f, 2.5f, 1.3f };
    //! 爆発の粒：若い（u=0）→ 0.2 → 0.6 → 老いた（u=1）の色と自己発光。
    FxColour burst[4]{ { 1.0f, 0.74f, 0.26f }, { 1.0f, 0.64f, 0.18f }, { 0.92f, 0.26f, 0.04f }, { 0.50f, 0.05f, 0.02f } };
    float burst_emissive[4]{ 1.7f, 1.35f, 1.0f, 0.35f };
    //! 爆発の閃光（若い→老いた）。
    FxColour flash[2]{ { 1.0f, 0.96f, 0.78f }, { 1.0f, 0.70f, 0.30f } };
    //! 煙：出はじめの色と、灰色。`smoke_alpha` が 0 なら煙は出ない。
    FxColour smoke_warm{ 0.55f, 0.32f, 0.20f };
    FxColour smoke_cool{ 0.30f, 0.29f, 0.28f };
    float smoke_alpha{ 0.80f };
    //! 火の粉（尾）：若い→老いた。
    FxColour ember[2]{ { 1.0f, 0.72f, 0.22f }, { 0.80f, 0.14f, 0.03f } };
    //! 点光源の色（玉・爆発）。
    FxColour light_orb{ 1.0f, 0.60f, 0.25f };
    FxColour light_burst{ 1.0f, 0.55f, 0.20f };
    //! 火の試作の値か（検査が見分けるための印）。
    bool is_fire{ false };
    //! 名前を知っていた属性か（知らない名前は白の組になる）。
    bool known{ true };
};

//! 属性の名前（小文字）から色の組を引く。**知らない名前は白**（`known = false`）。
FxPalette fx_palette_for(const std::string &attr);

//! 基準の色 1 つから色の組を作る（火以外の属性）。`smoke_alpha` は煙の濃さ、`emissive_scale` は自己発光の倍率（闇は暗く見せるため下げる）。
FxPalette fx_palette_from_base(const FxColour &base, float smoke_alpha, float emissive_scale = 1.f);

//! 爆発の範囲のマス 1 つ。`ring` は中心（撃った者）からの距離で、この順に時間をずらして始まる。
struct FxBurstCell {
    int gx{ 0 };
    int gy{ 0 };
    int ring{ 0 };
};

//! 見た目と速さの調整値。**既定がそのまま採用値**で、定数をここ 1 か所に集めてある。
struct VoxelFxParams {
    //! 飛ぶ速さ（マス/秒）。
    float arrow_speed{ 20.f };
    float bolt_speed{ 28.f };
    float shot_speed{ 24.f };
    float thrown_speed{ 14.f };
    float boomerang_speed{ 13.f };
    float orb_speed{ 12.f };
    //! 最低限の飛ぶ時間（秒）。隣のマスへの射撃でも見えるように。
    float min_fly_seconds{ 0.10f };

    //! 弧の高さ = 距離 × `arc_per_cell`（`arc_max` で頭打ち）。矢が 1.0、弾・投げた物は `arc_scale_*` を掛ける。
    float arc_per_cell{ 0.04f };
    float arc_max{ 0.45f };
    float arc_scale_shot{ 0.6f };
    float arc_scale_thrown{ 0.8f };

    //! 矢の長さ（マス）。
    float arrow_length{ 0.8f };
    //! 当たってから、刺さって残る時間と、そのうち薄れる時間（秒）。0 で当たった瞬間に消える。
    float arrow_stick_seconds{ 0.35f };
    float arrow_fade_seconds{ 0.15f };
    float bolt_stick_seconds{ 0.28f };
    float bolt_fade_seconds{ 0.12f };

    //! 弾（石）の 1 粒の大きさと、回る速さ（ラジアン/秒）。
    float shot_voxel{ 0.06f };
    float shot_spin{ 32.f };
    //! 投げた物の板の面内の回る速さ（ラジアン/秒）。ブーメランは `boomerang_spin`。
    float thrown_spin{ 19.f };
    float boomerang_spin{ 23.f };
    //! 板の拡大率（床のアイテムと同じ）。
    float slab_scale{ 0.7f };
    //! 板が引けないときの箱の一辺。
    float thrown_box{ 0.2f };

    //! 属性付きの矢弾の光：周りの光の粒の数、点光源の半径・強さ。
    float brand_light_radius{ 2.6f };
    float brand_light_intensity{ 0.9f };

    //! 火の玉の半径（マス）と、ボクセル 1 個の一辺。ボルトは `bolt_orb_scale` を掛ける。
    float orb_radius{ 0.36f };
    float orb_voxel{ 0.11f };
    float bolt_orb_scale{ 0.8f };
    //! 火の玉のちらつきを切り替える頻度（回/秒）。
    float orb_flicker_hz{ 24.f };
    //! 火の粉を出す頻度（個/秒）と寿命（秒）。
    float ember_rate{ 48.f };
    float ember_life{ 0.5f };
    //! 飛んでいる玉の点光源（半径はマス）。
    float orb_light_radius{ 4.5f };
    float orb_light_intensity{ 2.0f };

    //! 爆発：距離が 1 増えるごとの遅れ・粒の寿命・煙の遅れと寿命（秒）。
    float ring_delay{ 0.05f };
    float fire_life{ 0.42f };
    float smoke_delay{ 0.08f };
    float smoke_life{ 0.5f };
    //! 爆発のマス 1 個あたりのボクセルの数。
    int fire_per_cell{ 5 };
    int smoke_per_cell{ 1 };
    //! 閃光の長さ（秒）。
    float flash_seconds{ 0.18f };
    /*!
     * 爆発の点光源。強さ = `burst_light_base + burst_light_per_ring × 範囲の最大の距離`、
     * 半径 = 範囲の最大の距離 + 1 + `burst_light_margin`。**範囲が狭いほど弱く小さい**。
     */
    float burst_light_base{ 3.2f };
    float burst_light_per_ring{ 1.4f };
    float burst_light_max{ 8.f };
    float burst_light_rise{ 0.03f };
    float burst_light_decay{ 0.45f };
    float burst_light_margin{ 2.5f };
    //! ボルトが当たったときの小さな弾け：粒の数・寿命（秒）。
    int pop_voxels{ 6 };
    float pop_life{ 0.22f };

    //! ブレス：撃った者からの距離が 1 増えるごとの遅れ（秒）。
    float breath_ring_delay{ 0.045f };

    //! ビーム：マス 1 つぶんの点き始めの遅れ・光っている時間・薄れる時間（秒）。
    float beam_cell_delay{ 0.016f };
    float beam_life{ 0.26f };
    float beam_fade{ 0.14f };

    //! 飛ばない魔法：光が出ている時間（秒）。
    float glow_seconds{ 0.85f };
};

/*!
 * @brief 撃ち方 1 回ぶん（フレームの `ProjectionFx` / `MissileFx` から `AttackFxView` が作る）。
 * @details 位置は世界座標（マス）。経路は `fx_set_route()` で入れる（累計の長さを一緒に作る）。
 */
struct FxSpec {
    FxKind kind{ FxKind::Arrow };
    //! 乱数の種。同じ種なら同じ絵になる。
    std::uint32_t seed{ 1 };
    //! 飛ぶ道（始点から止まる所まで。ブーメランは戻りの道もつなげてある）。
    std::vector<Vec3> route;
    //! `route` の各点までの累計の長さ（先頭は 0）。
    std::vector<float> route_cum;
    //! `route` のうち、行きの終点（折り返し）の点の番号。戻りが無ければ最後の点。
    std::size_t turn_index{ 0 };

    //! 爆発・ブレスの範囲のマス（`ring` つき）。ビームの経路のマスにも使う。
    std::vector<FxBurstCell> cells;
    //! 爆発の中心のマス（`ring = 0` の位置。ブレスは撃った者のマス）。
    int centre_gx{ 0 };
    int centre_gy{ 0 };
    //! Orb が着弾で爆発するか（ボール）。偽（ボルト）は小さく弾ける。
    bool explode{ false };
    //! 弾・矢が相手に当たったか。
    bool hit{ false };

    FxPalette palette;
    //! 属性付きの矢弾か、その属性の色の組。
    bool has_brand{ false };
    FxPalette brand;

    //! 投げた物の `item_id` と、板を使うか（引けないときは小さな箱）。
    std::uint16_t item_id{ 0 };
    bool use_slab{ false };
    float face_yaw{ 0.f };
    float face_pitch{ 0.f };

    /*!
     * @name 属性ごとの見た目が使う情報
     * @{
     */
    //! 属性の名前（小文字。`ProjectionFx::attr`）。矢弾では空。
    std::string attr;
    //! 引いたスタイル（`make_spec` が入れる。`nullptr` なら汎用）。
    const FxStyle *style{ nullptr };
    //! `FxKind::Glow` の種類。
    FxGlowMode glow_mode{ FxGlowMode::Target };
    //! 撃った者のマス（格子座標）。
    int src_gx{ 0 };
    int src_gy{ 0 };
    //! 画面の右と上（世界座標での向き。正規化）。絵や文字を画面に正対させるのに使う。評価のたびに画面側が入れる。
    Vec3 cam_right{ 1.f, 0.f, 0.f };
    Vec3 cam_up{ 0.f, 0.f, 1.f };
    //! 対象のマス（y,x の格子座標）にいる実体のタイルの画素を引く。引けなければ偽。評価のたびに画面側が入れる。
    std::function<bool(int gx, int gy, FxTileImage &out)> tile_at;
    /*! @} */
};

//! ある時刻の描くもの。
struct FxFrame {
    std::vector<FxVoxel> voxels;
    //! この時刻の点光源。`collect_point_lights()` の `extra_lights` に渡す。
    std::vector<PointLight> lights;
    //! 投げた物の板。
    std::vector<FxSlab> slabs;

    //! 飛んでいる最中の本体（矢・火の玉など）があるか。あれば位置と進む向き（正規化）。**最後に評価した 1 つ**の値。
    bool projectile_visible{ false };
    Vec3 projectile_pos{};
    Vec3 projectile_dir{ 1.f, 0.f, 0.f };
    //! 爆発が起きている最中か。起きていれば、中心から爆発のボクセルの中心までの水平距離の最大値（マス）。
    bool explosion_active{ false };
    float explosion_extent{ 0.f };
    //! 全部終わって何も残っていないか。
    bool finished{ false };

    /*!
     * @name 属性ごとの見た目が足す出力（`render/fx_styles/`）
     * @{
     */
    //! 画面の揺れの強さ（マス。カメラの注視点にこの大きさまでの揺れを足す）。同じ時刻の複数の効果は大きいほうを取る。
    float shake{ 0.f };
    //! 対象の見た目の一時的な変化。
    std::vector<FxEntityMod> entity_mods;
    //! 画面全体を走る光の波。
    std::vector<FxScreenWave> screen_waves;
    /*! @} */
};

/*!
 * @brief スタイルの関数に渡す文脈（`render/fx_styles/fx_style.h`）。
 * @details `t` は撃ってからの秒（どの段階でも同じ）。`fly` は玉が飛ぶ秒（飛ばない形は 0）、
 * `te` は着弾（飛ばない形では開始）からの秒（`t - fly`）。
 */
struct FxStyleCtx {
    const FxSpec &spec;
    const VoxelFxParams &params;
    float t{ 0.f };
    float fly{ 0.f };
    float te{ 0.f };
};

//! 経路を入れる（行きの点の並びと、戻りの点の並び。戻りは空でよい）。累計の長さも作る。
void fx_set_route(FxSpec &spec, const std::vector<Vec3> &outbound, const std::vector<Vec3> &back = {});

//! 飛んでいる時間（秒）。経路が無ければ 0。
float fx_fly_seconds(const FxSpec &spec, const VoxelFxParams &params);

//! 撃ってから全部消えるまでの秒数。
float fx_total_seconds(const FxSpec &spec, const VoxelFxParams &params);

//! 飛んでいる時間のうち、行きの分（秒）。戻りが無ければ `fx_fly_seconds` と同じ。
float fx_outbound_seconds(const FxSpec &spec, const VoxelFxParams &params);

/*!
 * @brief 飛んでいる本体の中心の位置（`s` は 0 = 撃った位置 〜 1 = 止まる位置）。弧を含む。
 * @details 矢・弾・クロスボウの矢には、矢じりが当たる所に刺さるよう、終点を手前へずらした分も含む。
 */
Vec3 fx_flight_position(const FxSpec &spec, const VoxelFxParams &params, float s);

/*!
 * @brief 撃ってから `t` 秒の描くものを `out` に**足す**（`out` は消さない）。
 * @details 撃つ前（`t < 0`）と終わった後は何も足さず、終わった後は `out.finished = true` にする。
 */
void fx_evaluate(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out);

/*!
 * @brief 原作の `distance()`（`max(|dx|,|dy|) + min(|dx|,|dy|)/2`）。爆発の距離の単位。
 * @details 整数の切り捨てまで原作と同じ。
 */
int fx_grid_distance(int dx, int dy);

/*!
 * @name 汎用の見た目（属性の色の玉・爆発・ビーム・ブレス・光）
 * @details スタイルの関数が `nullptr` のときに使われる段階の実体。**スタイルの中から呼んでよい**
 * （「火の玉の見た目に雫を足す」のように、汎用の見た目の上に重ねる使い方）。
 * 時間の長さは対応する `*_seconds` で聞く。
 * @{
 */
//! 爆発 1 回の出し方。`small` は小さな弾け（ボルトの当たり・ビームの終点）。
struct FxBurstMode {
    float ring_delay{ 0.05f };
    bool flash{ true };
    bool small{ false };
};

//! 玉の本体（飛んでいる間）。
void fx_emit_orb_body(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out);
//! 火の粉の尾。`t` は撃ってからの秒。
void fx_emit_embers(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out, const FxPalette &pal, float rate,
    float life, float fly, float size_scale);
//! 爆発（`te` は始まりからの秒）。範囲のマスの中だけに粒が出る。
void fx_emit_burst(const FxSpec &spec, const VoxelFxParams &params, float te, const FxBurstMode &mode, FxFrame &out, bool with_light);
//! 爆発の長さ（秒）。
float fx_burst_seconds(const FxSpec &spec, const VoxelFxParams &params, const FxBurstMode &mode);
//! 経路に沿った光の線。
void fx_emit_beam_line(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out);
//! 対象の上に浮かぶ小さな光。
void fx_emit_glow_mote(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out);

//! 汎用の段階。引数の意味は `render/fx_styles/fx_style.h` の `FxStyle`。
void fx_generic_flight(const FxStyleCtx &ctx, FxFrame &out);
void fx_generic_impact(const FxStyleCtx &ctx, FxFrame &out);
float fx_generic_impact_seconds(const FxSpec &spec, const VoxelFxParams &params);
void fx_generic_beam(const FxStyleCtx &ctx, FxFrame &out);
float fx_generic_beam_seconds(const FxSpec &spec, const VoxelFxParams &params);
void fx_generic_breath(const FxStyleCtx &ctx, FxFrame &out);
float fx_generic_breath_seconds(const FxSpec &spec, const VoxelFxParams &params);
void fx_generic_burst(const FxStyleCtx &ctx, FxFrame &out);
float fx_generic_burst_seconds(const FxSpec &spec, const VoxelFxParams &params);
void fx_generic_glow(const FxStyleCtx &ctx, FxFrame &out);
float fx_generic_glow_seconds(const FxSpec &spec, const VoxelFxParams &params);
/*! @} */

} // namespace hd2d
