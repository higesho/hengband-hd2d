/*!
 * @file check_support.h
 * @brief **検査どうし**で使い回す作り物。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 *
 * `app/app_support.h` との違いは**誰が使うか**である。あちらは `run()`（遊ぶ経路）も
 * 呼ぶ土台、こちらは**検査からしか呼ばれない**もの。遊ぶ経路から呼ばれていないので、
 * ここを直しても遊びには効かない——逆に言えば、ここの間違いは
 * `tools/hd2d_verify/golden.py` が捕まえられる（検査の出力に必ず出る）。
 */
#pragma once

#include "app/hd2d_app.h"          //!< `AppOptions`
#include "render/math3d.h"         //!< `Vec3`
#include "render/post_process.h"   //!< `PostParams`
#include "render/voxel_renderer.h" //!< `VoxelRenderer`
#include "render/camera.h"         //!< `Camera`
#include "voxel/prefab.h"          //!< `Prefab`
#include "ui/ui_layout.h"          //!< `RectPx`
#include "ui/ui_paint.h"           //!< `UiPaint`
#include "frame/game_frame.h"      //!< `GameFrame`（名前空間に入っていない）

#include <cstddef>
#include <string>
#include <vector>

namespace hd2d {

class PrefabLibrary; //!< `world/prefab_library.h`

/*!
 * @brief 意匠の表をどのコアのものとして読むか（環境変数 `HD2D_STYLE_CORE`）。
 * @details 検査はコアを起こさないので、コアの申告から決められない。
 */
std::string style_core_env();

/*! @brief 検査のために意匠の表（町・洞）を読み込む。 */
void load_styles_for_check(const AppOptions &options);

/*! @brief 保存した町のフレームを読む（`--town-check` と `--town-view`）。 */
bool load_town_frame(const std::string &path, const PrefabLibrary &library,
    GameFrame &frame, std::string &err);

/*!
 * @brief 検査用の合成フレームを作る（床と壁が市松に混ざったフロア）。
 * @details 実プレイに頼ると「セーブの中身しだいで通ったり落ちたりする検査」になる。
 */
GameFrame make_synthetic_frame(int view_w, int view_h, int player_gx, int player_gy);


/* --------------------------------------------------- プレハブと設定 */

/*! @brief プレハブの外接する箱を求める。 */
void prefab_bounds(const Prefab &prefab, Vec3 &min_out, Vec3 &max_out);

/*! @brief 起動の引数から色まわりの後処理の値を当てる。 */
void apply_color_options(const AppOptions &options, PostParams &params);

/*! @brief 起動の引数から描き込みの細かさを当てる。 */
void apply_detail_options(const AppOptions &options, VoxelRenderer &renderer);

/* --------------------------------------------------- 画素を数える */

/*! @brief いまのフレームバッファを読み出す（RGB。上下は GL のまま）。 */
std::vector<unsigned char> read_framebuffer(int width, int height);

/*! @brief 2 枚のあいだで変わった画素を数える。 */
std::size_t count_changed_pixels(const std::vector<unsigned char> &a,
    const std::vector<unsigned char> &b, int tolerance);

/*! @brief 2 枚のあいだで変わった画素を、決めた枠の中だけ数える。 */
std::size_t count_changed_pixels(const std::vector<unsigned char> &a,
    const std::vector<unsigned char> &b, int width, int height, const RectPx &area);

/*! @brief 枠の中で「塗られている」画素を数える。 */
std::size_t count_painted_pixels(const std::vector<unsigned char> &image, int width, int height,
    const RectPx &area, const unsigned char clear[3]);

/*! @brief 画面の四隅に地が見えている割合（画が寄り切っているかの目安）。 */
double count_corner_gaps(int width, int height, const unsigned char clear[3],
    int corner_px, std::string &report);

/*! @brief 1 画素の色を採る。 */
void pixel_at(const std::vector<unsigned char> &image, int width, int height, int x, int y, int rgb[3]);

/*! @brief 2 つの枠の重なりの面積。 */
long long rect_overlap_area(const RectPx &a, const RectPx &b);

/* --------------------------------------------------- 作り物 */

/*! @brief 町の入口を含む作り物のフレーム。 */
GameFrame make_entrance_frame(int view_w, int view_h, int player_gx, int player_gy);

/*! @brief 作り物のフレームに画面まわり（持ち物・状態）を詰める。 */
void fill_synthetic_ui(GameFrame &frame);

/*! @brief 戦いの見せ物を重ねて描く（`--combat-fx-check`）。 */
void overlay_combat_fx(UiPaint &paint, const Camera &camera, int screen_w, int screen_h,
    float cx, float cy);

/*! @brief LUT を CPU 側で引く（GPU の結果と突き合わせる）。 */
void sample_lut_cpu(const std::vector<unsigned char> &lut, int size, const float in[3], float out[3]);

/*! @brief 無音の WAV を書く（音の道筋の検査に使う）。 */
bool write_silent_wav(const std::string &path, std::size_t frames);
} // namespace hd2d
