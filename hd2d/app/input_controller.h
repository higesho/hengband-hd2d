/*! @file input_controller.h
 * @brief SDL 入力を UI とコアへ振り分ける。原作のキー配列はここでは解釈しない。
 */
#pragma once
#include "app/app_support.h"
#include "ui/game_pad.h"
#include "ui/virtual_pad.h"
#include "ui/feature_menu.h"
#include "ui/floor_cutin.h"
#include "ui/click_path.h"
#include "ui/fps_mode.h"
#include "ui/game_hud.h"
#include "frame/protocol_messages.h"
#include <functional>

namespace hd2d {

struct InputState {
    std::string ime_edit_text;
    bool ime_prompt_open{ false };
    bool ime_dismissed{ false };
    bool hover_valid{ false };
    int hover_gx{ 0 };
    int hover_gy{ 0 };
};

// フレームごとに作成する。SDL とパッドによるメニューの二重開閉を共通で抑止する。
struct InputBatch {
    presentation::InputEventsMessage input;
    bool want_quit{ false };
    char swallow_char{ '\0' };
    bool swallow_next_text{ false };
    bool menu_toggled_this_pump{ false };
};

/*!
 * @brief SDL の出来事を捌く（`pump_sdl_events()`）のに要る `run()` の状態。
 *
 * @details **持ち主は `run()` のまま**で、設定や描画への依存は参照で渡す。1 巡ごとに `run()` が
 * 組み立てて渡す（`perform_action` などの lambda が 1 巡の局所変数を捕まえているので、
 * 環の外で 1 回だけ組むことはできない）。
 *
 * IME・ホバーは InputState、フレーム内の入力結果は InputBatch が所有する。
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

    InputState &state; //!< フレームをまたぐ IME・ホバーの状態

    //! ### `run()` の lambda（中身はあちら。ここからは呼ぶだけ）
    std::function<void(int)> perform_action;                    //!< 割り当てられた操作を 1 つ実行する
    std::function<bool()> fps_drives_movement;                  //!< 一人称が WASDQE を持ってよい場面か
    std::function<bool()> turn_drives_movement;                 //!< 視点回転が方向入力を回してよい場面か
    std::function<void(presentation::InputEventWire &)> turn_screen_move; //!< 画面基準の `move` を世界基準へ

    InputBatch &batch; //!< このフレームの入力結果。パッドも同じ場所へ積む。

};

/*!
 * @brief SDL の出来事を全部捌く（(a) 入力 → `input_event`。v1 §8.3）。
 *
 * @details キー・マウス・ホイール・IME・窓・指をこの順で見て、UI が食うものは食い、
 * 残りを `ctx.batch.input` へ積む。**到着順を崩さない。**中身の順番と理由は本体の註釈にある。
 * パッドは別（`run()` の (a')）だが、同じ `ctx.batch.input` へ積む。
 * キーの 2 枝（`SDL_KEYDOWN` / `SDL_TEXTINPUT`）は長いので `input_controller.cpp` の中で
 * `pump_key_down()` / `pump_text_input()` に分けてある。
 */
void pump_sdl_events(InputPumpContext &ctx);


} // namespace hd2d
