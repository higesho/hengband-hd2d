/*! @file input_controller.h
 * @brief SDL 入力を UI とコアへ振り分ける。原作のキー配列はここでは解釈しない。
 */
#pragma once
#include "app/app_support.h"
#include "render/targeting_visual.h"
#include "ui/game_pad.h"
#include "ui/virtual_pad.h"
#include "ui/feature_menu.h"
#include "ui/floor_cutin.h"
#include "ui/click_path.h"
#include "ui/map_click.h"
#include "ui/fps_mode.h"
#include "ui/game_hud.h"
#include "frame/protocol_messages.h"
#include <functional>
#include <cstdint>

namespace hd2d {

struct InputState {
    std::string ime_edit_text;
    bool ime_prompt_open{ false };
    bool ime_dismissed{ false };
    bool hover_valid{ false };
    int hover_gx{ 0 };
    int hover_gy{ 0 };
    std::int64_t menu_finger{};
    int menu_touch_x{},menu_touch_y{},menu_touch_last_x{},menu_touch_last_y{},menu_touch_axis{};
    bool menu_touch_active{},menu_touch_scrolled{};
    //! 地図のクリック・タップの追跡（AH-58。`ui/map_click.h`）。`map_click_enabled` のコアでだけ動く。
    MapClickState map_click;
    /*!
     * Shift＋矢印の「走る」の後半（修-02）。ターゲット画面で方向を受けるコア
     * （`run_direction_via_targeting_ui`）では、走る命令を押したあとに画面が開くのを待ち、
     * 開いたら `aim_move` → `aim_confirm` で方向を答える。`active` の間だけ意味を持つ。
     */
    struct PendingRun {
        bool active{ false };
        int dx{ 0 };
        int dy{ 0 };
        int stage{ 0 };                     //!< 0 = 画面が開くのを待つ・1 = 方向を送った（確定の待ち）
        std::uint32_t since_ms{ 0 };        //!< 待ち始め（`SDL_GetTicks`）
        std::int32_t last_run_sym{ 0 };     //!< 走るを出したキー。押しっぱなしの連打（repeat）で走りを止めないための記憶
    } pending_run;
    /*!
     * 斜め移動限定（`kActionDiagonalOnly`。`ui/diagonal_only.h`）の押し具合。押している間だけ真。
     * キーは `SDL_KEYDOWN`/`SDL_KEYUP` を見て `pump_sdl_events()` が書く。パッドのボタンは `run()` が
     * 毎フレーム `GamePad::input_down` から書く。窓が焦点を失ったら全部偽に戻す。
     */
    unsigned arrows_down{ 0 };           //!< 押している矢印（`kArrowUp` 等の論理和。上＋左を作るのに要る）
    unsigned wasd_down{ 0 };             //!< 押している WASD（一人称。W＝`kArrowUp`・S＝`kArrowDown`・A＝`kArrowLeft`・D＝`kArrowRight`）
    bool diagonal_key_held{ false };     //!< 割り当てたキーを押している
    bool diagonal_pad_armed{ false };    //!< パッドの割り当てのボタンを押した縁を受けた（離すと偽。層つきの割り当てでも正しい層の押しだけ）
    bool diagonal_pad_held{ false };     //!< パッドの割り当てのボタンを押している（`diagonal_pad_armed` かつ押下中）
    bool diagonal_held() const { return diagonal_key_held || diagonal_pad_held; }
};

/*!
 * @brief ソフトキーボード（Android の IME）を出したい画面か。
 * @details コアが文字入力の中に居る（`text_input_active`）ときだけ。ただし**数量の欄（`numeric`）では出さない**
 *。数量は新しい UI でも従来の UI でも十字キーで変えられ、キーボードは画面の下半分を覆う。
 */
inline bool wants_soft_keyboard(const GameFrame &frame)
{
    return frame.text_input_active && !frame.numeric.active;
}

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
    /*!
     * いまのキー配置の種類（"original" / "rogue"。`pad_commands` と一緒に届く）。
     * "rogue" のとき、照準の画面と視点の回転は hjklyubn を方向として読む（2026-10-07）。
     */
    const std::string &pad_keymap;
    //! 握手の `core_name`。Shift＋矢印で「走る」を出すときに、コアごとの走る命令の字を引くのに要る（修-02）
    const std::string &core_name;

    //! ### UI
    FeatureMenu &feature_menu;
    FloorCutin &floor_cutin;
    HudState &hud_state;         //!< カーソル層と、掴んでいる仕切り
    ClickPath &click_path;
    /*!
     * コアが `hello_ack.features` に `"map_click"` を申告したか（AH-58）。真のとき、地図のクリック・
     * タップは `map_click` として送る（歩く・走る・見る・文脈メニューは原作の動きをコアが決める）。
     * 偽のコアでは、これまでの「クリックした所まで歩く」（`click_path`）のまま。握手のあとで決まる。
     */
    bool map_click_enabled{ false };
    TextOverlay &text;           //!< `menu_choice_at()` が字の幅を測るのに要る
    FpsMode &first_person;
    const std::vector<SubPanelKindChoice> &sub_panel_kind_choices;

    //! ### いまのフレーム
    const GameFrame &frame;
    std::span<const TargetingEntity> targeting_entities;

    InputState &state; //!< フレームをまたぐ IME・ホバーの状態

    //! ### `run()` の lambda（中身はあちら。ここからは呼ぶだけ）
    std::function<void(int)> perform_action;                    //!< 割り当てられた操作を 1 つ実行する
    std::function<bool()> fps_drives_movement;                  //!< 一人称が WASDQE を持ってよい場面か
    std::function<bool()> turn_drives_movement;                 //!< 視点回転が方向入力を回してよい場面か
    std::function<void(presentation::InputEventWire &)> turn_screen_move; //!< 画面基準の `move` を世界基準へ

    InputBatch &batch; //!< このフレームの入力結果。パッドも同じ場所へ積む。
    /*!
     * DCSS の画面の区画（タブの欄ほか。`hd2d/dcss/`）へのマウス。申告 `dcss-ui` のコアだけが渡す（空なら何もしない）。
     * 動き（`SDL_MOUSEMOTION`）と押下（`SDL_MOUSEBUTTONDOWN`）を、機能メニューが開いていないときに最初に見せる。
     * 真を返したら、その出来事は区画が食った（ほかへ回さない）。
     */
    std::function<bool(const SDL_Event &)> dcss_mouse;
    /*!
     * DCSS の窓の部品（段 U3。メニュー・説明・作成の格子ほか。`hd2d/dcss/dcss_windows.h`）への出来事。申告 `dcss-ui` のコアだけが渡す（空なら何もしない）。
     * 窓が開いている間（と、1 行の文字入力の間）は、キー・文字・マウスをここが全部食う（窓の中のキーが HD2D の別の操作に化けないため）。
     * 第 2 引数は「HD2D が自分で使うキー（機能メニュー・Alt+Enter）か」。真を返したら出来事は食われた（ほかへ回さない）。
     */
    std::function<bool(const SDL_Event &, bool)> dcss_window;
    /*!
     * DCSS の地図のマウス操作（段 U3・U4。左右のクリック・ツールチップ。`hd2d/dcss/dcss_mouse.h`）。申告 `dcss-ui` のコアだけが渡す（空なら何もしない）。
     * 動き（`SDL_MOUSEMOTION`）と押下（`SDL_MOUSEBUTTONDOWN`）を、ほかの区画（`dcss_mouse`）が食わなかったあとで見せる。引数は
     * 「マウスの位置が地図の見えているマスの上か」と「プレイヤーのマスから見たそのマスの相対」。真を返したら食った（歩く・狙うへ回さない）。
     */
    std::function<bool(const SDL_Event &, bool, int, int)> dcss_map;
    /*!
     * DCSS のタッチ（段 7。`hd2d/dcss/dcss_touch.h`）。申告 `dcss-ui` のコアだけが渡す（空なら何もしない）。指の出来事（`SDL_FINGER*`）を受けて、
     * 原作の Android 版と同じ決まりのマウスの出来事（`which` = `dcss::kTouchMouseId`）を `SDL_PushEvent` で積み直す。第 2 引数は「この指の押下をなかったことにするか」
     * （仮想パッドのボタンの近く）。渡したコアでは、仮想パッドを出している間も**パッドのボタンの外の指**は捨てず、ここへ流す
     * （`pointer_blocked_by_vpad`。決めたこと 2026-10-14 は DCSS には当てない——調整役の決定 D29）。
     */
    std::function<void(const SDL_Event &, bool)> dcss_touch;

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

/*!
 * @brief 斜め移動限定が**効く場面**か（地図の上を歩く場面。メニュー・窓・狙い・方向を聞く問い・文字入力の間は偽）。
 * @details コアが「次の命令」を待っていて（`awaiting_command`。Shift＋矢印の走りと同じ条件）、地図が見えていて、
 * 機能メニュー・カットイン・配置の編集・新 UI の窓・狙い・選択肢・数の入力・文字入力のどれも出ていないとき。
 * キー（`pump_key_down` / `pump_text_input`）とパッド（`run()` の方向の道筋）が**同じ 1 本**を見る。
 */
bool diagonal_only_applies(const InputPumpContext &ctx);

//! 押している最中で、かつ効く場面か。移動を捨てるかどうかの判定はこれで見る。
inline bool diagonal_only_active(const InputPumpContext &ctx)
{
    return ctx.state.diagonal_held() && diagonal_only_applies(ctx);
}


} // namespace hd2d
