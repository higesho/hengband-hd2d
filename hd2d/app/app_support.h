/*!
 * @file app_support.h
 * @brief `run()` と検査モードの**両方**が使う土台。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * 検査モードを `hd2d/app/checks/` へ分けたとき、遊ぶ経路と検査の経路の
 * **両方から呼ばれているもの**がここへ残った。
 *
 * ここに置く目安は「窓・GL・画面の保存のように、どちらの経路でも要る土台か」。
 * 検査どうしでしか使わないものは `app/checks/check_support.h` のほう。
 */
#pragma once

#include "app/hd2d_app.h"        //!< `AppOptions`
#include "render/camera.h"       //!< `Camera` / `ViewWindow`
#include "render/math3d.h"       //!< `Vec3` / `Mat4`
#include "render/post_process.h" //!< `PostChain` / `PostFlags`
#include "render/text_overlay.h" //!< `TextOverlay` / `TextColor`
#include "render/voxel_renderer.h" //!< `VoxelRenderer` / `Cutaway` / `InstanceData`
#include "world/entity_view.h"   //!< `EntityView`
#include "world/prefab_library.h" //!< `PrefabLibrary`
#include "world/terrain_view.h"  //!< `TerrainView`
#include "ui/hd2d_settings.h"    //!< `Hd2dSettings`
#include "ui/ui_layout.h"        //!< `UiLayout`
#include "frame/game_frame.h"    //!< `GameFrame` / `LightingState`（名前空間に入っていない）
#include "frame/protocol_messages.h" //!< `presentation::UiStateMessage`

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
typedef void *SDL_GLContext;

namespace hd2d {

/*! @brief 窓と GL コンテキストの対。 */
struct Window {
    SDL_Window *window{ nullptr };
    SDL_GLContext context{ nullptr };
};

/*!
 * @brief GL 4.6 core のコンテキストを持った窓を作る。
 * @details **代替経路は用意しない。** 4.6 が取れない機械ではこの実行体は成立しない。
 */
bool create_window(const AppOptions &options, Window &out, std::string &err);

/*! @brief 注視点で 1 マスが `cell_px` に見える視線距離を求める。 */
float distance_for_cell_px(const Camera &camera, float cell_px);

/*! @brief いまのフレームバッファを BMP で保存する（検査の証拠と `--shot`）。 */
bool save_framebuffer_bmp(const std::string &path, int width, int height);

/*! @brief 町の「入口のマス」を立てた面（プレハブの門を数えるのに使う）。 */
std::vector<std::uint8_t> town_entrance_mask(const GameFrame &frame, const PrefabLibrary &library);


/* --------------------------------------------------- 共有の定数 */

//! 文字の高さ（画素）。
inline constexpr int kTextPx = 16;

//! コアへ要求する可視窓の上限（フロアの最大寸法。これ以上もらっても描く先が無い）。
inline constexpr int kMaxViewW = 198;
inline constexpr int kMaxViewH = 66;

/*!
 * @brief シャドウマップの一辺（P5 ①）。
 * @details 既定のカメラの可視範囲は差し渡し 40〜60 マスなので、2048 なら 1 マス 34〜51 テクセル
 * ＝ボクセル 1 個あたり 1 テクセル強になる。これ以上増やしても、影の元になる形が
 * ボクセルの刻みしか持っていないので効かない。
 */
inline constexpr int kShadowMapSide = 2048;

/*!
 * @brief 立ち上がるものの高さ（マス）。可視窓と影の直方体の両方で使う。
 * @details `derive_view_window()` の呼び出しと**同じ値でなければならない**。
 * 食い違うと「窓には入っているが影の地図には入っていない」マスが出る。
 */
inline constexpr float kMaxPropHeight = 1.f;

//! 字の色。見出しと、控えめな説明。
inline const TextColor kHeaderColor{ 1.f, 0.85f, 0.35f, 1.f };
inline const TextColor kDimColor{ 0.55f, 0.60f, 0.70f, 1.f };

/* --------------------------------------------------- 時刻・光 */

/*! @brief `HD2D_DAY_MINUTE` で時刻を固定する（検査を昼夜で揺らさないため）。-1 なら固定しない。 */
int forced_day_minute();

/*! @brief 夜のあいだだけ灯りを点ける係数（0=昼・1=夜）。 */
float lamp_night_factor(const LightingState &light);

/* --------------------------------------------------- 後処理 */

/*! @brief 起動の引数から後処理の段の on/off を決める。 */
PostFlags resolve_post_flags(const AppOptions &options);

/*! @brief 後処理の連なりを組む（LUT の読み込みまで）。 */
bool init_post_chain(PostChain &post, const AppOptions &options, std::string &err);

/* --------------------------------------------------- 字と画面 */

/*! @brief Term の色番号を描画の色へ。 */
TextColor from_term_color(std::uint8_t index, float alpha = 1.f);

/*! @brief 画面の下に 1 行だけ知らせを出す。 */
void show_message(const std::string &text);

/*! @brief いまの画面で決定キーがコマンドメニューを開くか。 */
bool confirm_opens_command_menu(const GameFrame &frame);

/*! @brief フレームを字だけで描く（2D の見え方。`--ascii-map`）。 */
void draw_frame_as_text(TextOverlay &text, const GameFrame &frame, const std::string &title_line,
    int screen_w, int screen_h, double fps, std::size_t frame_bytes, bool ascii_map);

/*! @brief 画面の状態をコアへ知らせる電文を組む。 */
presentation::UiStateMessage build_ui_state(
    const ViewWindow &window, const UiLayout &layout, const Hd2dSettings &settings, bool screen_audio);

/* --------------------------------------------------- 立体を描く */

/*! @brief 溶岩・発光地形の「面として光る」床を集める。 */
void collect_emissive_slabs(const GameFrame &frame, std::vector<InstanceData> &out,
    bool skip_lava = false, float glow_scale = 1.f);

/*! @brief プレハブライブラリを影のパスへ描く。 */
void draw_library_depth(VoxelRenderer &renderer, PrefabLibrary &library, const TerrainView &terrain,
    const Mat4 &light_view_projection);

/*! @brief プレハブライブラリを色のパスへ描く。 */
void draw_library_color(VoxelRenderer &renderer, PrefabLibrary &library, const TerrainView &terrain);

/* --------------------------------------------------- 切り欠き（手前を刈る） */

/*! @brief 世界の点が画面の奥行きでどこに来るか。 */
float window_depth_of(const Camera &camera, const Vec3 &world);

/*! @brief 切り欠きの向き（カメラの正面を平面に落としたもの）。 */
void cutaway_face_dir(const Camera &camera, float &face_x, float &face_y);

/*! @brief 1 点のまわりを刈る切り欠きを作る。 */
Cutaway make_point_cutaway(const Camera &camera, const Vec3 &at, float radius,
    bool camera_side_only = false);

/*! @brief 人物のまわりを刈る切り欠きを作る。 */
Cutaway make_player_cutaway(const Camera &camera, const GameFrame &frame, float radius,
    bool camera_side_only = false);

/*! @brief 実体のまわりの切り欠きを足す。 */
void append_entity_cutaways(const Camera &camera, const TerrainView &terrain,
    const EntityView &entities, float radius, std::vector<Cutaway> &out,
    bool camera_side_only = false);
} // namespace hd2d
