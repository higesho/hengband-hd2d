/*!
 * @file run_parts.h
 * @brief `run()`（ゲームループ）から切り出した部品。
 *
 * @details コアからのフレーム・テーブルの受信と、VR の配置を担当する。
 * SDL 入力は input_controller、描画資源と共通の補助処理は app_support が担当する。
 *
 * ## 振る舞いを変えていないことの確かめ方
 *
 *     python tools/hd2d_verify/replay.py --check      ← コアを止めて 1 ビットまで
 *     python tools/hd2d_verify/playthrough.py --check ← 本物のコアで通しを
 *     python tools/hd2d_verify/golden.py --check      ← 検査モード 16 件
 *
 * 最初のものは絵の SHA-256 まで完全一致で見る。`run()` を触ったら必ず 3 つとも通すこと。
 */
#pragma once

#include "xr/xr_room.h"    //!< `xr::UiSurface`
#include "xr/xr_session.h"
#include "xr/xr_fake.h"
#include "xr/xr_math.h"
#include "app/app_clock.h"
#include "ui/game_hud.h"
#include "ui/combat_fx_view.h"
#include "render/term_colors.h"
#include "frame/frame_codec.h"
#include "frame/sound_event.h"
#include "ui/fps_mode.h"
#include "render/render_view.h" //!< `RenderView`
#include "app/app_support.h"   //!< `Camera` / `GameFrame` / `UiLayout` ほか
#include "assets/tile_catalog.h" //!< `TileCatalog`
#include "net/core_link.h"     //!< `CoreLink`
#include "ui/game_pad.h"       //!< `PadCommand` / `PadMacroFlags`
#include "ui/feature_menu.h"  //!< `SubPanelKindChoice`
#include "ui/hd2d_settings.h"  //!< `Hd2dSettings`

#include <string>
#include <vector>

namespace hd2d {

/*!
 * @brief 闘技場の観戦カメラの結果。
 * @details `active` が偽なら場内を測れなかった（追従カメラのまま）。
 */
struct ArenaWatch {
    //! 観戦カメラに切り替わったか。
    bool active{ false };
    //! 測れた場内の大きさ（マス）。`active` が偽なら 0。
    int width{ 0 };
    int height{ 0 };
};

/*!
 * @brief 闘技場の観戦カメラを当てる（設計書 §4.6）。
 *
 * @details 賭け試合ではプレイヤは端で観戦するだけなので、追従カメラでは**試合が
 * 画面の外**になる。合図は `FloorKind::Arena`（v1 §12-2。コア固有の知識を持たない）。
 * 中心と大きさはミニマップ（フロア全域）から採る。
 *
 * 当たると `camera` の注視点・見下ろし角・距離を書き換える。当たらなければ
 * `camera` は触らない。
 *
 * @param frame いまのフレーム。ミニマップとフロアの種別を見る
 * @param force `--arena-watch` で強制するか
 * @param first_person_active 一人称のときは**一人称が勝つ**（観戦へ切り替えない）
 * @param layout 画面の割り付け（場内が入る倍率を決めるのに要る）
 * @param camera 当たったときだけ書き換える
 */
ArenaWatch apply_arena_watch(const GameFrame &frame, bool force, bool first_person_active,
    const UiLayout &layout, Camera &camera);


/*!
 * @brief コアから届いた表を取り込む（v1 §8）。
 * @details `asset_manifest`（ビルボードの目録）・`pad_commands`・`macro_triggers`・
 * `sub_panel_kinds` の 4 つ。**表を足したらここにも足すこと。**
 */
void take_core_tables(CoreLink &link, TileCatalog &tile_catalog,
    std::vector<PadCommand> &pad_commands, PadMacroFlags &pad_macros,
    Hd2dSettings &settings, std::vector<SubPanelKindChoice> &sub_panel_kind_choices);

/*!
 * @brief VR の画面の板を空間へ据える（§22）。
 *
 */
void place_vr_hud_panel(bool vr_frame_ready, xr::Session &xr, const xr::Frame &vr_frame,
    const xr::HeadAnchor &vr_anchor, const xr::UiSurface &vr_fake_ui,
    const Hd2dSettings &settings, const UiLayout &layout,
    int screen_w, int screen_h, float frame_seconds,
    float &vr_panel_yaw, std::vector<xr::UiPanel> &vr_panels);

/*!
 * @brief 届いている最新のフレームを取り込む（v1 §6.4）。
 *
 */
void take_new_frame(CoreLink &link, GameFrame &frame, std::size_t &frame_bytes,
    HudState &hud_state, CombatFxView &combat_fx, Hd2dSettings &settings,
    bool &sub_kinds_synced, const std::vector<SubPanelKindChoice> &sub_panel_kind_choices,
    const int (&sent_sub_kinds)[kUiSubPanels],
    Uint64 perf_freq, double &ms_decode);

/*!
 * @brief VR の盤（世界を載せる卓）を空間へ据える（§17）。
 *
 */
void place_vr_board(bool vr_frame_ready, const xr::Frame &vr_frame,
    xr::Session &xr, Camera &camera, const ViewWindow &view_window,
    FpsMode &first_person, const Hd2dSettings &settings, const AppOptions &options,
    int screen_w, int screen_h, RenderView (&vr_views)[2],
    xr::BoardPlacement &vr_board, xr::RoomPlacement &vr_room,
    xr::HeadAnchor &vr_anchor, bool &vr_board_placed, bool &vr_board_first_person,
    int &vr_board_turn, float &vr_panel_yaw);

} // namespace hd2d
