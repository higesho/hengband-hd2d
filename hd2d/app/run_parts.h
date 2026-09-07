/*!
 * @file run_parts.h
 * @brief `run()`（ゲームループ）から切り出した部品。
 *
 * @details `hd2d/app/hd2d_app.cpp` の `run()` は 5,831 行あり、そのうち 4,128 行が
 * `for (;;)` の中である。**ローカル変数が 156 個**あって全部が環をまたぐので、
 * 素直に半分に割ると 156 個の受け渡しになる——それは分割ではなく作り直しになる。
 *
 * そこで、**外の変数を数個しか触らない塊**だけをここへ出す。目安は「触る変数が
 * 10 個以下」。それを超えるものは、まとめ役の作りを考えてからにする——
 * SDL の出来事を捌く塊（33 個）は、参照の束 `InputPumpContext` を組んでから出した。
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
#include "ui/virtual_pad.h"   //!< `VirtualPad`
#include "ui/floor_cutin.h"   //!< `FloorCutin`
#include "ui/click_path.h"    //!< `ClickPath`
#include "render/text_overlay.h" //!< `TextOverlay`
#include "frame/protocol_messages.h" //!< `presentation::InputEventsMessage`

#include <functional>
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

/*!
 * @brief SDL の出来事を捌く（`pump_sdl_events()`）のに要る `run()` の状態。
 *
 * @details **持ち主は `run()` のまま**で、ここは参照の束である。1 巡ごとに `run()` が
 * 組み立てて渡す（`perform_action` などの lambda が 1 巡の局所変数を捕まえているので、
 * 環の外で 1 回だけ組むことはできない）。
 *
 * 触るものは 33 個ある。**引数に並べると呼び出しが読めなくなる**ので束にした。
 * 仕分けは下の見出しのとおり——窓・設定と視点・入力装置・UI・フレーム・IME・
 * ホバー・`run()` の lambda・この 1 巡の受け皿。
 *
 * 書き換えるものは非 const の参照、読むだけのものは const の参照にしてある
 * （`camera_turn` は読むだけだが、同じ 1 巡の中で `perform_action` が回すので
 * 値で写してはいけない）。
 *
 * `run()` の lambda は `std::function` で受ける。**参照で受けてはいけない**——
 * lambda から `std::function` への変換は一時オブジェクトを作るので、構造体を
 * 組んだ文が終わった時点で消える。
 */
struct InputPumpContext {
    //! ### 窓。大きさは `SDL_WINDOWEVENT_SIZE_CHANGED` で書き換わる
    int &screen_w;
    int &screen_h;
    //! いま使える範囲（切り欠きと IME を避けたもの）。バーチャルパッドの当たりに要る
    std::function<RectPx()> usable_area;

    //! ### 設定と視点
    Hd2dSettings &settings;      //!< ホイールでカメラの値を、Alt+Enter で窓の形を書く
    const UiLayout &layout;      //!< 仕切りと 3D の矩形の当たり
    const Camera &camera;        //!< クリック位置を地面へ落とす
    const int &camera_turn;      //!< 見下ろしの視点回転（数字キーの向きを回すのに要る）

    //! ### 入力装置
    GamePad &pad;
    VirtualPad &vpad;
    const std::vector<PadCommand> &pad_commands; //!< 機能メニューを開くときに渡す

    //! ### UI
    FeatureMenu &feature_menu;
    FloorCutin &floor_cutin;
    HudState &hud_state;         //!< カーソル層と、掴んでいる仕切り
    ClickPath &click_path;
    TextOverlay &text;           //!< `menu_choice_at()` が字の幅を測るのに要る
    FpsMode &first_person;
    const std::vector<SubPanelKindChoice> &sub_panel_kind_choices;

    //! ### いまのフレーム
    const GameFrame &frame;

    //! ### IME
#if defined(__ANDROID__)
    //! 2 つとも Android だけ（`run()` の宣言と同じ条件。ソフトキーボードの出し直しに要る）。
    const bool &ime_prompt_open;
    bool &ime_dismissed;
#endif
    std::string &ime_edit_text;  //!< 未確定文字列（こちらで描く）

    //! ### マウスが指しているマス
    bool &hover_valid;
    int &hover_gx;
    int &hover_gy;

    //! ### `run()` の lambda（中身はあちら。ここからは呼ぶだけ）
    std::function<void(int)> perform_action;                    //!< 割り当てられた操作を 1 つ実行する
    std::function<bool()> fps_drives_movement;                  //!< 一人称が WASDQE を持ってよい場面か
    std::function<bool()> turn_drives_movement;                 //!< 視点回転が方向入力を回してよい場面か
    std::function<void(presentation::InputEventWire &)> turn_screen_move; //!< 画面基準の `move` を世界基準へ

    //! ### この 1 巡の受け皿（`run()` が 1 巡ごとに作り直す。パッドの道もここへ積む）
    presentation::InputEventsMessage &input;
    bool &want_quit;
    char &swallow_char;          //!< KEYDOWN で食った 1 文字。続く TEXTINPUT をこの 1 個だけ落とす
    bool &swallow_next_text;     //!< 次の TEXTINPUT を 1 個落とす札
    bool &menu_toggled_this_pump; //!< 機能メニューの開閉は 1 巡 1 回
};

/*!
 * @brief SDL の出来事を全部捌く（(a) 入力 → `input_event`。v1 §8.3）。
 *
 * @details キー・マウス・ホイール・IME・窓・指をこの順で見て、UI が食うものは食い、
 * 残りを `ctx.input` へ積む。**到着順を崩さない。**中身の順番と理由は本体の註釈にある。
 * パッドは別（`run()` の (a')）だが、同じ `ctx.input` へ積む。
 * キーの 2 枝（`SDL_KEYDOWN` / `SDL_TEXTINPUT`）は長いので `run_parts.cpp` の中で
 * `pump_key_down()` / `pump_text_input()` に分けてある。
 */
void pump_sdl_events(InputPumpContext &ctx);

} // namespace hd2d
