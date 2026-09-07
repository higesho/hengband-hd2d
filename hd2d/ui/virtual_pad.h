/*!
 * @file virtual_pad.h
 * @brief バーチャルパッド — 画面のボタンを指で押して、物理パッドと同じ出来事を出す。
 *
 * ## 出自
 * 2026-08-12 に決めた「バーチャルパッドは VirtualPad.jpg を参考に作成」
 * 「デフォルトは ON。HD2D メニューで設定できるように」。Android HD2D 版の
 * AH-01（バーチャルパッドが無い）の受け皿である。旧 UI の `ui/input/virtual_pad.*` は
 * 旧スタック（IRenderer）に結線されているため**参照だけ**して作り直した（必守制約 6）。
 *
 * ## 作りの要点 — 物理パッドと同じ入力
 * 押されたボタンは `PadInput`、左スティック（LS）は `poll_direction`、
 * 右スティック（RS）は −1〜+1 の連続値。**`GamePad` と同じ形で返す**ので、
 * 呼び出し側（`hd2d_app.cpp`）は物理パッドの経路へ合流させるだけでよい。
 * 専用の経路を作ると、メニュー操作・割り当て・一人称のどれかが必ず片方だけ直される。
 *
 * | 画面のボタン | 出来事 |
 * |---|---|
 * | A / B / X / Y | `PadInput::A / B / X / Y` |
 * | L1 / L2 / L3 | `LeftShoulder / LeftTrigger / LeftStick` |
 * | R1 / R2 / R3 | `RightShoulder / RightTrigger / RightStick` |
 * | START / SELECT | `Start / Back`（SELECT ＝ 機能メニュー） |
 * | LS（左の大円） | 8 方向の移動（リピートは物理パッドと同じ 260/90ms） |
 * | RS（右の大円） | 視点の連続値（`right_stick_x/y`） |
 *
 * ## タッチと合成マウスの取り合い
 * SDL は既定で**最初の指**をマウスへ写す（`which == SDL_TOUCH_MOUSEID`）。
 * パッドのボタンに落ちた指をそのまま通すと、同じタップが「クリック移動」にも化ける。
 * そこで**ボタンに当たった指のマウス写しは、その指を離すまでこちらが飲み込む**。
 * ボタンの外に落ちた指は一切触らない（タップ移動・境界の掴みは従来どおり）。
 *
 * ## 配置の編集（ボタン配置）
 * 黒背景にパッドだけを出し、タッチで選択 → ドラッグで移動・2 本指で拡大縮小。
 * 中央に「デフォルトに戻す」を置く（2026-08-12 に決めた）。編集中は
 * ゲームへの入力を全部止める（機能メニューと同じ扱い）。
 */
#pragma once

#include "ui/game_pad.h" //!< PadInput（出来事の綴りを共有する）
#include "ui/ui_layout.h" //!< RectPx（置ける範囲。切り欠きを避けた矩形が来る）

#include <cstdint>
#include <string>
#include <vector>

union SDL_Event;

namespace hd2d {

class UiPaint;
class TextOverlay;

/*!
 * @brief 画面に出すコントロール。**並びは設定画面の並び**（2026-08-12 に決めた:
 * A B X Y R1 R2 R3 L1 L2 L3 START SELECT RS LS）。cfg にもこの綴りで書く。
 */
enum class VpadControl : int {
    A,
    B,
    X,
    Y,
    R1,
    R2,
    R3,
    L1,
    L2,
    L3,
    Start,
    Select,
    RS, //!< 右の大円 ＝ 視点スティック
    LS, //!< 左の大円 ＝ 移動スティック
    Count,
};

constexpr int kVpadControlCount = static_cast<int>(VpadControl::Count);

//! 画面と cfg に出す綴り（`A` `R1` `START` `LS` …）。
const char *vpad_control_name(VpadControl control);

//! ボタンなら対応する `PadInput`。スティック（LS/RS）は `PadInput::Count`。
PadInput vpad_control_input(VpadControl control);

/*!
 * @brief 1 コントロールの置き場所の上書き。
 * @details **負の座標は「既定のまま」**（既定の置き場所は画面の実寸から毎回導くので、
 * 数を写して持つと解像度が変わったときに古い画面の位置へ化ける）。
 */
struct VpadSlot {
    float xf{ -1.f }; //!< 中心 x（画面幅に対する 0〜1）
    float yf{ -1.f }; //!< 中心 y（画面高に対する 0〜1）
    float scale{ 1.f }; //!< 大きさの倍率（0.5〜2.0）

    bool overridden() const { return (this->xf >= 0.f) && (this->yf >= 0.f); }
};

/*!
 * @name 縦持ち・横持ち（2026-08-12 に決めた「バーチャルパッドの設定は縦と横で
 * 分けて設定できるように」＝**配置と大きさだけ**を分ける。表示 ON/OFF・濃度・
 * ボタンごとの表示は縦横で共通のまま——回しただけで切ったはずのパッドが出ると驚く）。
 *
 * @details **どちらかは置ける範囲の形が決める**（`vpad_orient_of()`）。
 * 「いま縦か」を別に持って回すと、描く側と当たり判定とで食い違う道ができる
 * （必守制約 3）。範囲は描画・入力・編集の全員が同じものを受け取っているので、
 * そこから毎回出せば必ず揃う。
 * @{
 */
constexpr int kVpadOrientCount = 2;
constexpr int kVpadLandscape = 0;
constexpr int kVpadPortrait = 1;
/*! @} */

//! 置ける範囲の形から縦横を決める。**縦長なら `kVpadPortrait`**（正方形は横扱い）。
int vpad_orient_of(const RectPx &area);
//! 記録と画面に出す綴り（`land` / `port`）。
const char *vpad_orient_name(int orient);

/*!
 * @brief バーチャルパッドの設定（`hd2d.cfg` に残る側）。
 * @details 実行時の状態（どの指がどのボタンを掴んでいるか）は `VirtualPad` が持ち、
 * ここには**利用者が変えられるもの**だけを置く。
 */
struct VirtualPadSettings {
    /*!
     * @brief パッドを出すか。**Android の既定は入**（2026-08-12 に決めた）。
     * @details Windows の既定は切（マウスとキーボードがある）。メニューにも並べない
     * （`feature_menu.h` の「この実行体に無いものは入れない」）が、cfg の
     * `vpad_show=1` を手で書けばタッチ画面の Windows でも使える（開発の確認もこの道）。
     */
    bool show{
#if defined(__ANDROID__)
        true
#else
        false
#endif
    };
    //! ボタンごとの表示。**既定は全部入**。
    bool visible[kVpadControlCount];
    /*!
     * @brief 表示濃度（2026-08-12 に決めた）。**1.0 ＝ 既定の濃さ**の倍率。
     * @details 0.25（かすか）〜 2.0（くっきり）。地図が透けてほしい人と、ボタンを
     * はっきり見たい人の両方がいる。編集画面には効かせない（編集は常にくっきり）。
     */
    float opacity{ 1.f };
    /*!
     * @brief 置き場所と大きさの上書き（既定のままなら `overridden() == false`）。
     * @details **縦横で 2 組持つ**（添字は `kVpadLandscape` / `kVpadPortrait`）。
     * 既定の置き場所も 2 組あり（`virtual_pad.cpp` の `kDefaultsLand` / `kDefaultsPort`）、
     * どちらを見るかは置ける範囲の形が決める。
     */
    VpadSlot slot[kVpadOrientCount][kVpadControlCount]{};

    VirtualPadSettings()
    {
        for (bool &v : this->visible) {
            v = true;
        }
    }

    //! 配置だけを既定へ戻す（編集画面の「デフォルトに戻す」）。**いま向いている側だけ**。
    void reset_layout(int orient);
    //! 表示・ボタン表示・配置の全部を既定へ戻す（メニューの「デフォルトに戻す」）。**配置は縦横とも**。
    void reset_all();

    //! `A:1,B:0,…` の形へ（cfg に書く。14 個全部書く）。
    std::string visible_line() const;
    //! 同じ形から読む。知らない綴りは黙って飛ばす。
    void parse_visible(const std::string &line);
    //! `A:0.712,0.866,1.00;LS:…` の形へ（**上書きしたぶんだけ**。無ければ空文字）。
    std::string layout_line(int orient) const;
    //! 同じ形から読む。**まず既定へ戻してから**入れる（消した上書きが残らないように）。
    void parse_layout(int orient, const std::string &line);

    bool differs_from(const VirtualPadSettings &other) const;
};

/*! @name 表示濃度の範囲（メニューと cfg の読みの両方が見る） @{ */
inline constexpr float kVpadOpacityMin = 0.25f;
inline constexpr float kVpadOpacityMax = 2.f;
/*! @} */

class VirtualPad {
public:
    /*!
     * @brief SDL のイベントを 1 つ渡す。パッドが食ったら true（コアにもマウスにも流さない）。
     * @param area パッドを置ける範囲（**切り欠きを避けた矩形**。窓いっぱいとは限らない）。
     *   **描くときと同じものを渡すこと**——ここがずれると「見えている所を押しても入らない」。
     * @param window_w,window_h **窓の実寸**。`area` とは別に要る——指の座標は
     *   窓に対する 0〜1 で来る（`SDL_TouchFingerEvent`）ので、画素へ直すのに窓の大きさが要る。
     * @details 指（`SDL_FINGER*`）と、その指の**マウス写し**（`SDL_TOUCH_MOUSEID`）を扱う。
     * 実マウスの左ボタンも同じ道へ入れてある（タッチ画面の Windows と開発時の確認のため）。
     * 編集画面が開いている間は**ポインタの出来事を全部食う**。
     */
    bool on_event(const SDL_Event &event, VirtualPadSettings &settings, const RectPx &area,
        int window_w, int window_h);

    //! 押し 1 回ぶんを取り出す。無ければ false（`GamePad::take_pressed` と同じ口）。
    bool take_pressed(PadPress &out);
    //! いま押されている修飾（L1 / R1 を指で押さえている間）。
    int chord_mask() const { return this->chord_.mask(); }

    /*!
     * @brief LS の方向を 1 回ぶん取り出す（初回押下とリピート。`GamePad::poll_direction` と同じ口）。
     * @details リピートの間隔・入れ替え時の扱いは物理パッドと同じ
     * （初回 260ms・以降 90ms・向きを替えても間隔を置く）。
     */
    bool poll_direction(std::uint32_t now_ms, int &dx, int &dy);

    //! RS の傾き（−1〜+1）。触れていなければ 0（`GamePad` と同じ口）。
    float right_stick_x() const { return this->rs_out_x_; }
    float right_stick_y() const { return this->rs_out_y_; }

    //! 掴んでいる指・押している状態を全部忘れる（焦点を失ったとき）。
    void forget_hold();

    /*! @name 配置の編集画面（メニューの「ボタン配置」から開く） @{ */
    bool editor_open() const { return this->editor_open_; }
    void open_editor();
    void close_editor();
    /*! @} */

    /*!
     * @brief 遊んでいる画面のパッドを描く。`settings.show` が切なら何も描かない。
     * @param area 置ける範囲（`on_event` と**同じもの**を渡すこと）。
     * @param binds ボタンへの割り当て（`settings.pad`）。
     * @param commands コアのコマンド表（名前の出どころ。握手の前は空でよい）。
     * @details `paint`（下敷き）と `text`（ラベル）に**積むだけ**。流すのは呼び出し側
     * （下敷き → 文字の順は `ui_paint.h` の約束）。
     *
     * **ボタンに出すのは「割り当てられている操作の名前」**（2026-08-12 に決めた）。
     * `A` `R1` という綴りは押す前に意味が分からないので、`決定` `階段を降りる` と出す。
     * 割り当てが無いボタンだけ綴りのまま（空にすると「出ていない」と見分けがつかない）。
     */
    void draw(UiPaint &paint, TextOverlay &text, const VirtualPadSettings &settings, const RectPx &area,
        const PadMacroFlags &macros, int chord_mask,
        const PadBinds &binds, const std::vector<PadCommand> &commands) const;

    /*!
     * @brief 編集画面を描く（黒背景・全ボタン・中央の「デフォルトに戻す」「戻る」）。
     * @details **本体の flush の後に積んで、もう一度 flush してもらう**（いちばん上に
     * 出すため。`UiPaint::flush` は流したら空になるので、同じ物を 2 度は流さない）。
     */
    void draw_editor(UiPaint &paint, TextOverlay &text, const VirtualPadSettings &settings, const RectPx &area) const;

private:
    //! 実マウスを指として扱うときの番号（Android の実指は 0 起点の非負なので衝突しない）。
    static constexpr std::int64_t kMouseFinger = -0x4D6F7573; // 'Mous'

    /*!
     * @brief 掴んでいる指 1 本。
     * @details **空きは `active` で見る。番号の番人値は使わない**——Android の
     * `SDL_FingerID` は **0** で（実測）、「0 = 空き」にすると本物の指と見分けが
     * つかなくなる。実際にそうしてしまい、FINGERUP で誰も離せず LS が
     * 押しっぱなしになった（タイトルのカーソルが回り続けた事故の真因）。
     */
    struct Grab {
        bool active{ false };
        std::int64_t finger{ 0 };
        int control{ -1 };
        float x{ 0.f }; //!< いまの位置（画素）
        float y{ 0.f };
    };
    static constexpr int kMaxGrabs = 8;

    //! 指を落とす・動かす・離す（タッチと実マウスの両方がここへ来る）。
    bool pointer_down(std::int64_t finger, float x, float y, VirtualPadSettings &settings, const RectPx &area);
    bool pointer_move(std::int64_t finger, float x, float y, VirtualPadSettings &settings, const RectPx &area);
    bool pointer_up(std::int64_t finger, VirtualPadSettings &settings, const RectPx &area);

    Grab *find_grab(std::int64_t finger);
    Grab *free_grab();
    //! この座標に居るコントロール。無ければ -1。`editor` では非表示のボタンにも当たる。
    int control_at(float x, float y, const VirtualPadSettings &settings, const RectPx &area, bool editor) const;
    //! スティックの傾きを更新する（LS は 8 方向のラッチ・RS は連続値）。
    void update_stick(int control, float x, float y, const VirtualPadSettings &settings, const RectPx &area);
    void release_stick(int control);

    Grab grabs_[kMaxGrabs]{};
    std::vector<PadPress> pressed_;
    /*!
     * @brief 同時押しの状態機械。**実機のパッドと同じもの**（`ui/game_pad.h`）。
     * @details 板の上では L1／R1 を指で押さえたまま別のボタンを叩く形になる。
     * 指は複数本届く（`Grab` を指ごとに持っている）ので、実機と同じ約束で成立する。
     */
    PadChord chord_;

    //! LS のラッチ（-1/0/+1）とリピート（`GamePad` と同じ意味）。
    int ls_dx_{ 0 };
    int ls_dy_{ 0 };
    int held_dx_{ 0 };
    int held_dy_{ 0 };
    std::uint32_t next_repeat_ms_{ 0 };
    /*!
     * @brief **短いタップの 1 歩**。倒して離すまでが 1 フレームに収まると、`poll_direction`
     * が見る前にラッチが中立へ戻って 1 歩も出ない（エミュレータの `input tap` で実測。
     * 実機でも端を素早く叩く操作は同じ道を通る）。離した瞬間に「まだ 1 歩も出していない
     * 向き」をここへ置き、次の `poll_direction` が 1 歩だけ出す。
     */
    int tap_dx_{ 0 };
    int tap_dy_{ 0 };

    float rs_out_x_{ 0.f };
    float rs_out_y_{ 0.f };

    //! ボタンに落ちた指のマウス写しを飲み込んでいる最中か（離しで解く）。
    bool touch_mouse_swallow_{ false };

    /*! @name 編集画面 @{ */
    bool editor_open_{ false };
    int selected_{ -1 };
    //! ドラッグ中の指と、掴んだ点と中心のずれ（画素）。有無は `active` で見る（上の注記と同じ理由）。
    bool drag_active_{ false };
    std::int64_t drag_finger_{ 0 };
    float drag_off_x_{ 0.f };
    float drag_off_y_{ 0.f };
    //! ピンチ（2 本目の指）。`pinch_dist0_` は掴んだ瞬間の 2 点間距離。
    bool pinch_active_{ false };
    std::int64_t pinch_finger_{ 0 };
    float pinch_dist0_{ 0.f };
    float pinch_scale0_{ 1.f };
    /*! @} */
};

} // namespace hd2d
