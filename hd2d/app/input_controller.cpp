/*! @file input_controller.cpp
 * @brief SDL のイベントを UI の操作またはコアへの入力へ変換する。
 */
#include "app/input_controller.h"
#include "app/test_keyboard.h"
#include "app/app_clock.h"
#include "ui/key_binds.h"
#include "ui/ui_cursor.h"
#include "render/gl_core.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace hd2d::gl;

namespace hd2d {

namespace {

/* ================================================================ 入力の翻訳 */

/*!
 * @brief `SDL_Keymod` → 割り当て表の修飾（`ui/key_binds.h` の `kMod*`）。
 * @details **左右をまとめる。**`KMOD_LSHIFT` をそのまま覚えると、右 Shift で割り当てた人が
 * 左 Shift で反応しないことになる。NumLock・CapsLock は「押した修飾」ではないので落とす。
 */
int mods_from_sdl(Uint16 mod)
{
    int mods = 0;
    if ((mod & KMOD_SHIFT) != 0) {
        mods |= kModShift;
    }
    if ((mod & KMOD_CTRL) != 0) {
        mods |= kModCtrl;
    }
    if ((mod & KMOD_ALT) != 0) {
        mods |= kModAlt;
    }
    return mods;
}

/*!
 * @brief UI のカーソルが解釈する向き（P8）。**斜めは渡さない。**
 * @details 斜めを渡すと、店で `1`（南西）を押したときに選択肢が動いてしまい、
 * 「文字キーでも選べる」という従来の道が塞がる。カーソルは上下左右と決定だけを食う。
 */
CursorNav cursor_nav_from_key(const SDL_KeyboardEvent &key)
{
    const bool numlock = (key.keysym.mod & KMOD_NUM) != 0;
    switch (key.keysym.sym) {
    case SDLK_UP:
        return CursorNav::Up;
    case SDLK_DOWN:
        return CursorNav::Down;
    case SDLK_LEFT:
        return CursorNav::Left;
    case SDLK_RIGHT:
        return CursorNav::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return CursorNav::Confirm;
    case SDLK_KP_8:
        return numlock ? CursorNav::None : CursorNav::Up;
    case SDLK_KP_2:
        return numlock ? CursorNav::None : CursorNav::Down;
    case SDLK_KP_4:
        return numlock ? CursorNav::None : CursorNav::Left;
    case SDLK_KP_6:
        return numlock ? CursorNav::None : CursorNav::Right;
    default:
        return CursorNav::None;
    }
}


/*!
 * @brief テンキーの数字（`1`〜`9`。`5` は除く）を**画面基準**の (dx,dy) にする。
 * @return 数字でなければ偽（dx,dy は触らない）。
 * @details 並びはテンキーそのもの——`7 8 9` が上の段、`1 2 3` が下の段。
 */
bool digit_to_screen_delta(char c, int &dx, int &dy)
{
    switch (c) {
    case '1':
        dx = -1;
        dy = 1;
        return true;
    case '2':
        dx = 0;
        dy = 1;
        return true;
    case '3':
        dx = 1;
        dy = 1;
        return true;
    case '4':
        dx = -1;
        dy = 0;
        return true;
    case '6':
        dx = 1;
        dy = 0;
        return true;
    case '7':
        dx = -1;
        dy = -1;
        return true;
    case '8':
        dx = 0;
        dy = -1;
        return true;
    case '9':
        dx = 1;
        dy = -1;
        return true;
    default:
        return false;
    }
}

//! 上の逆。8 近傍でなければ `'\0'`。
char screen_delta_to_digit(int dx, int dy)
{
    if ((dx == -1) && (dy == 1)) {
        return '1';
    }
    if ((dx == 0) && (dy == 1)) {
        return '2';
    }
    if ((dx == 1) && (dy == 1)) {
        return '3';
    }
    if ((dx == -1) && (dy == 0)) {
        return '4';
    }
    if ((dx == 1) && (dy == 0)) {
        return '6';
    }
    if ((dx == -1) && (dy == -1)) {
        return '7';
    }
    if ((dx == 0) && (dy == -1)) {
        return '8';
    }
    if ((dx == 1) && (dy == -1)) {
        return '9';
    }
    return '\0';
}

/*!
 * @brief SDL のキー入力を `input_event`（v1 §8.3）へ翻訳する。
 *
 * @details **`keys`（生のバイト列）ではなく `input_event` を送る。** 抽象イベントなら
 * 「北へ 1 歩」がコアの側でキー配列に合わせて解決されるので、こちらがテンキーの数字や
 * ローグライク配列を知らずに済む（`presentation/bridge/input_event_adapter.cpp`）。
 *
 * @note 印字文字は `SDL_TEXTINPUT` から採る（Shift や記号の解決を SDL に任せられる）。
 * `SDL_KEYDOWN` で拾うのは**印字にならないキーと Ctrl 併用**だけ。両方で拾うと 2 回入る。
 */
bool translate_key_down(const SDL_KeyboardEvent &key, presentation::InputEventWire &out)
{
    const SDL_Keycode sym = key.keysym.sym;
    const bool ctrl = (key.keysym.mod & KMOD_CTRL) != 0;
    const bool shift = (key.keysym.mod & KMOD_SHIFT) != 0;
    const bool alt = (key.keysym.mod & KMOD_ALT) != 0;
    const bool numlock = (key.keysym.mod & KMOD_NUM) != 0;

    auto move = [&out](int dx, int dy) {
        out.e = "move";
        out.dx = dx;
        out.dy = dy;
        return true;
    };

    switch (sym) {
    case SDLK_UP:
        return move(0, -1);
    case SDLK_DOWN:
        return move(0, 1);
    case SDLK_LEFT:
        return move(-1, 0);
    case SDLK_RIGHT:
        return move(1, 0);
    case SDLK_ESCAPE:
        out.e = "cancel";
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        out.e = "confirm";
        return true;
    case SDLK_TAB:
        out.e = "key";
        out.name = "tab";
        return true;
    case SDLK_BACKSPACE:
        out.e = "key";
        out.name = "backspace";
        return true;
    case SDLK_DELETE:
        out.e = "key";
        out.name = "delete";
        return true;
    default:
        break;
    }

    // テンキー。NumLock が入っているときは `SDL_TEXTINPUT` から数字が来るので触らない
    // （両方で拾うと 1 打鍵で 2 歩動く）。
    if (!numlock) {
        switch (sym) {
        case SDLK_KP_1:
            return move(-1, 1);
        case SDLK_KP_2:
            return move(0, 1);
        case SDLK_KP_3:
            return move(1, 1);
        case SDLK_KP_4:
            return move(-1, 0);
        case SDLK_KP_5:
            return move(0, 0);
        case SDLK_KP_6:
            return move(1, 0);
        case SDLK_KP_7:
            return move(-1, -1);
        case SDLK_KP_8:
            return move(0, -1);
        case SDLK_KP_9:
            return move(1, -1);
        default:
            break;
        }
    }

    if ((sym >= SDLK_F1) && (sym <= SDLK_F12)) {
        out.e = "fkey";
        out.n = (sym - SDLK_F1) + 1;
        out.ctrl = ctrl;
        out.shift = shift;
        out.alt = alt;
        return true;
    }

    // Ctrl + 英字。`SDL_TEXTINPUT` は制御文字を出さないのでここでしか拾えない。
    if (ctrl && (sym >= SDLK_a) && (sym <= SDLK_z)) {
        out.e = "key";
        out.chr = std::string(1, static_cast<char>(sym));
        out.ctrl = true;
        return true;
    }
    return false;
}

} // namespace

namespace {

/*!
 * @brief `SDL_TEXTINPUT`（印字文字と IME の確定）を捌く。`pump_sdl_events()` の一部。
 *
 * @details 確定文字を処理し、UI が消費した打鍵の文字を抑止する。非 ASCII は自由文字入力中だけ送る。
 */
void pump_text_input(InputPumpContext &ctx, const SDL_Event &event)
{
    ctx.state.ime_edit_text.clear(); //!< 確定した。未確定の表示は消す
    if (ctx.feature_menu.is_open()) {
        return; // メニューを開いている間は印字文字もコアへ流さない
    }
    if (ctx.batch.swallow_next_text
        || ((ctx.batch.swallow_char != '\0') && (event.text.text[0] == ctx.batch.swallow_char)
            && (event.text.text[1] == '\0'))) {
        //! 直前の KEYDOWN で UI が食った 1 文字。**その 1 個だけ**落とす。
        ctx.batch.swallow_next_text = false;
        ctx.batch.swallow_char = '\0';
        return;
    }
    ctx.batch.swallow_char = '\0';
    /*
     * **日本語などの非 ASCII は `text` で送る**（実装方針 2026-08-13）。
     *
     * コアは 2 バイト文字を受けられる——`askfor` は 1 バイト目が `iskanji`
     * なら `inkey_base` で次の 1 バイトをそのまま取る（`asking-player.cpp`）。
     * 受け取れなかったのは**ここが捨てていた**からで、コアの制約ではない。
     *
     * 送るのは **UTF-8 のまま**。系の文字コード（Windows は SJIS・Android は
     * EUC）へ直すのはコア側の仕事である（`input_event_adapter.cpp`）——
     * 「ui は文字コードを知らない」という分担は、地形テーブルと同じ約束。
     *
     * **自由文字入力の最中だけ**にする。コマンドを待っている所へ 2 バイト文字を
     * 流すと、コアはそれを 2 つのコマンドとして読む（`iskanji` の判定は
     * 文字入力の中にしか無い）。外では今までどおり黙って捨てる。
     */
    bool non_ascii = false;
    for (const char *p = event.text.text; *p != '\0'; ++p) {
        const auto byte = static_cast<unsigned char>(*p);
        non_ascii = non_ascii || (byte < 0x20) || (byte > 0x7E);
    }
    if (non_ascii && ctx.frame.text_input_active) {
        presentation::InputEventWire wire;
        wire.e = "text";
        wire.text = event.text.text;
        ctx.batch.input.events.push_back(wire);
        return;
    }
    for (const char *p = event.text.text; *p != '\0'; ++p) {
        const auto byte = static_cast<unsigned char>(*p);
        if ((byte < 0x20) || (byte > 0x7E)) {
            continue; // 印字 ASCII だけ（コアのキューは 1 バイトのキーを取る）
        }
        /*
         * **数字キーの移動も視点回転ぶん回す**（不具合の確認 2026-09-06）。
         *
         * 矢印とテンキー（NumLock 切）は `translate_key_down` が `move` にするので
         * `turn_screen_move` を通る。ところが **NumLock が入っているテンキーと
         * 上段の数字は文字として届く**ので、ここを素通りして素の向きでコアへ行っていた。
         *
         * 直すのは**向きだけ**——コアが受け取るのは今までどおり数字なので、
         * 走る・止まる・回数の意味は 1 つも変わらない。回してよい場面かどうかは
         * `turn_drives_movement()` が持つ（メニュー・店・選択肢・数の入力では回さない）。
         */
        char chr = static_cast<char>(byte);
        int digit_dx = 0;
        int digit_dy = 0;
        if (ctx.turn_drives_movement() && digit_to_screen_delta(chr, digit_dx, digit_dy)) {
            rotate_screen_delta(ctx.camera_turn, digit_dx, digit_dy);
            const char turned = screen_delta_to_digit(digit_dx, digit_dy);
            if (turned != '\0') {
                chr = turned;
            }
        }
        presentation::InputEventWire wire;
        wire.e = "key";
        wire.chr = std::string(1, chr);
        ctx.batch.input.events.push_back(wire);
    }
    return;
}

/*!
 * @brief `SDL_KEYDOWN` を捌く。`pump_sdl_events()` の一部で、いちばん長い枝。
 *
 * @details UI の優先順位に従って処理し、消費したイベントはコアへ送らない。
 * 見る順は、配置の編集画面 → Alt+Enter → カットイン → 機能メニュー → 一人称の WASDQE →
 * 割り当て → カーソル層 → 文字編集の矢印 → Enter のコマンドメニュー → コアへ。
 * それぞれの理由は中の註釈にある。
 *
 * @param fps_owns_move_keys 一人称が WASDQE を持っているか（`ctx.fps_drives_movement()`。
 * 出来事ごとに `pump_sdl_events()` が判じ直す——同じ 1 巡の中でメニューが開くことがある）
 */
void pump_key_down(InputPumpContext &ctx, const SDL_Event &event, bool fps_owns_move_keys)
{
    /*
     * **UI が先に食うキー**（P8）。既定の綴りは既存 UI と同じ
     * （F10 = 機能メニュー・Alt+Enter = 画面モード）ので、利用者の記憶がそのまま効く。
     * 割り当ては機能メニューの「操作の割り当て」で変えられる。
     * ここで `return` する＝**コアへは流さない**（誤コマンド禁止）。
     */
    const SDL_Keycode sym = event.key.keysym.sym;
    const int key_mods = mods_from_sdl(event.key.keysym.mod);

    /*
     * ボタン配置の編集画面（実装方針 2026-08-12）。**すべてのキーを食う**
     * （下にはメニューと地図があり、通すと見えない画面への誤操作になる）。
     * 抜ける道はタッチの「戻る」と、ここの ESC / Enter / Android の戻るキー。
     */
    if (ctx.vpad.editor_open()) {
        if ((sym == SDLK_ESCAPE) || (sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER)
            || (sym == SDLK_AC_BACK)) {
            ctx.vpad.close_editor();
        }
        ctx.batch.swallow_next_text = true;
        return;
    }

    // Alt+Enter で全画面 ⇔ ウインドウ（Windows の慣習。コアは受け取らない）。
    if ((sym == SDLK_RETURN) && ((event.key.keysym.mod & KMOD_ALT) != 0)) {
        ctx.settings.windowed = !ctx.settings.windowed;
        return;
    }
    /*
     * カットインの間は**すべてのキーを演出が食う**（P10。`ui/floor_cutin.h`）。
     * 進めるのは決定キー（Enter / Space）だけで、ほかのキーは**捨てる**
     * ——幕の向こうは見えていないので、通せば誤コマンドになる。
     * **機能メニューより先**に見る（幕が出ている間はメニューも開かせない。
     * 開けてしまうと、いちばん上に描く幕の下にメニューが隠れる）。
     */
    if (ctx.floor_cutin.blocks_input()) {
        if ((sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER) || (sym == SDLK_SPACE)) {
            (void)ctx.floor_cutin.confirm();
        }
        ctx.batch.swallow_next_text = true; //!< 続く `SDL_TEXTINPUT` も落とす
        return;
    }
    /*
     * 機能メニューを開いている間は**すべてのキーをメニューが食う**。
     * 誤コマンド禁止（メニューの中で押した矢印がゲームの中で歩いてはいけない）。
     */
    if (ctx.feature_menu.is_open()) {
        /*
         * 割り当ての変更モード。**十字も Enter も割り当てたいキーでありうる**ので、
         * メニューの操作より先に見る（`handle_bind_key` が予約キーを弾く）。
         */
        if (ctx.feature_menu.waiting_for_key()) {
            if (ctx.feature_menu.handle_bind_key(static_cast<int>(sym), key_mods, ctx.settings)) {
                return;
            }
        }
        if (ctx.settings.key_binds.action_for(static_cast<int>(sym), key_mods) == kActionFeatureMenu) {
            /*
             * 同じキーで閉じる。**ただしキーのリピートでは閉じない**
             * （関連する実装 §15 の 2・罠 Q-13）。ここと
             * 開く側（`perform_action` の `kActionFeatureMenu`）は同じキーで
             * 交互に踏まれるので、リピートを通すと **F10 を押しっぱなしに
             * するだけでメニューが点滅する**（PC で再現する。Quest の
             * 「一瞬開いて閉じる」と同じ形）。開閉は物理的な押し直しでだけ動かす。
             */
            if ((event.key.repeat == 0) && !ctx.batch.menu_toggled_this_pump) {
                ctx.batch.menu_toggled_this_pump = true;
                ctx.feature_menu.close();
            }
            return;
        }
        MenuNav nav = MenuNav::None;
        switch (sym) {
        case SDLK_UP:
        case SDLK_KP_8:
            nav = MenuNav::Up;
            break;
        case SDLK_DOWN:
        case SDLK_KP_2:
            nav = MenuNav::Down;
            break;
        case SDLK_LEFT:
        case SDLK_KP_4:
            nav = MenuNav::Left;
            break;
        case SDLK_RIGHT:
        case SDLK_KP_6:
            nav = MenuNav::Right;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            nav = MenuNav::Confirm;
            break;
        case SDLK_ESCAPE:
            nav = MenuNav::Cancel;
            break;
        default:
            break;
        }
        ctx.feature_menu.handle(nav, ctx.settings);
        return;
    }
    /*
     * 一人称（おまけ。`ui/fps_mode.h`）の操作。**横取りするのは WASDQE と
     * 右 Shift だけ**で、ほかのキーは今までどおりコアのコマンドとして働く
     * （実装方針 2026-08-11「WASDQE 以外は FPS モードでも使えるように」）。
     *
     * **方向キーとテンキーは一人称でも見下ろしの絶対方位のまま**（↑＝北固定）。
     * これは手つかずではなく**仕様**である——確認のうえ利用者が決めた
     * （2026-08-12「これはこのままの方がいいな。直さないでおく」）。
     * 向き基準で歩きたければ WASD／左スティック（`direction_delta`）を使う。
     * **将来「画面の向きと合っていない」と直したくなってもここを読むこと。**
     *
     * 食わない場合:
     *   - 選択肢・はい／いいえ・数値入力・機能メニューが出ている間
     *     （店で品物の a・s が選べなくなる）
     *   - Ctrl / Alt / GUI / Shift 付き
     *     （コアでは大文字と Ctrl 付きが別のコマンドなので、奪うとその操作が消える）
     */
    if (fps_owns_move_keys && (sym == SDLK_RSHIFT)) {
        //! 一人称の間の「メニュー」。開いた先の操作はカーソル＋Enter＋ESC のまま。
        //! ここも開閉なので**リピートでは動かさない**（§15 の 2・罠 Q-13）。
        if ((event.key.repeat == 0) && !ctx.batch.menu_toggled_this_pump) {
            ctx.batch.menu_toggled_this_pump = true;
            ctx.feature_menu.set_sub_panel_kinds(ctx.sub_panel_kind_choices);
            ctx.feature_menu.set_pad_state(ctx.pad_commands, ctx.pad.connected() || ctx.settings.vpad.show,
                ctx.pad.connected() ? ctx.pad.name() : std::string("バーチャルパッド"));
            ctx.feature_menu.open(ctx.settings);
            ctx.pad.forget_hold();
        }
        return;
    }
    if (fps_owns_move_keys
        && ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI | KMOD_SHIFT)) == 0)) {
        int forward = 0;
        int strafe = 0;
        bool eaten = true;
        switch (sym) {
        case SDLK_w:
            forward = 1;
            break;
        case SDLK_s:
            forward = -1;
            break;
        case SDLK_a:
            strafe = -1;
            break;
        case SDLK_d:
            strafe = 1;
            break;
        case SDLK_q:
            //! 向きだけ。**ターンは消費しない。**押している間ずっと回る（`turn_hold_*`）。
            if (event.key.repeat == 0) {
                ctx.first_person.turn_hold_begin(-1);
            }
            break;
        case SDLK_e:
            if (event.key.repeat == 0) {
                ctx.first_person.turn_hold_begin(1);
            }
            break;
        default:
            eaten = false;
            break;
        }
        if (eaten) {
            int dx = 0;
            int dy = 0;
            if (ctx.first_person.direction_delta(forward, strafe, dx, dy)) {
                ctx.click_path.cancel(); //!< 自分で歩き始めたら経路は捨てる
                presentation::InputEventWire wire;
                wire.e = "move";
                wire.dx = dx;
                wire.dy = dy;
                ctx.batch.input.events.push_back(wire);
            }
            /*
             * **続く `SDL_TEXTINPUT` の 1 文字を落とす札を立てる。**
             * KEYDOWN を食っただけでは印字文字が別のイベントでコアへ届く
             * （これを見落として Q が「飲む」を開いていた。動作確認 2026-08-11）。
             */
            ctx.batch.swallow_char = static_cast<char>(sym);
            return;
        }
    }
    /*
     * **割り当てられた操作**（`ui/key_binds.h`）。UI 自身の操作もコアのコマンドも
     * 同じ表から引く。ここで食ったキーはコアへ流さない（二重に効かない）。
     *
     * **一人称の WASDQE より後。**一人称の間の WASDQE はその場の移動であり、
     * 同じキーに割り当てた操作より優先する（§7.1.1 の「横取りするのは WASDQE」）。
     * 先に置くと、`a` に何かを割り当てた瞬間に一人称の左横歩きが消える。
     */
    {
        const int action = ctx.settings.key_binds.action_for(static_cast<int>(sym), key_mods);
        if (action != kActionNone) {
            /*
             * **機能メニューの開閉だけはキーのリピートで動かさない**
             * （関連する実装 §15 の 2・罠 Q-13。閉じる側は上）。
             * ほかの操作は今までどおりリピートを通す——押しっぱなしで
             * 進むのが自然なもの（コアのコマンド）まで一律に止めると、
             * 「押し続けても 1 回しか効かない」という別の不具合になる。
             */
            if ((action != kActionFeatureMenu) || (event.key.repeat == 0)) {
                ctx.perform_action(action);
            }
            /*
             * **続く `SDL_TEXTINPUT` の 1 文字を落とす札を立てる。**
             * KEYDOWN を食っただけでは印字文字が別のイベントでコアへ届く
             * （一人称の WASDQE で実際に踏んだ穴。動作確認 2026-08-11）。
             * Ctrl / Alt 併用は印字にならないので札を立てない（立てると
             * 次に打った文字を巻き添えにする）。
             */
            if ((sym >= 0x20) && (sym <= 0x7E) && ((key_mods & (kModCtrl | kModAlt)) == 0)) {
                ctx.batch.swallow_next_text = true;
            }
            return;
        }
    }
    /*
     * **カーソルが先に食う**（P8。選択肢・はい／いいえ・数値入力）。
     * 食われなかったものだけが従来どおりコアへ行く。ここを後回しにすると
     * 「個数を聞かれている最中の Enter」が店のコマンドとして走る。
     *
     * ## `pre_game_menu` で切ってはいけない（2026-08-11 に切って 3 つ壊した）
     * 「作成画面はコアがカーソルを持つ」と誤診して層ごと切った版は、
     * **矢印がコアへ素通りして数字キーに化けた**（`translate_key_down` の
     * 移動イベント → アダプタが `6` などへ）。`[Y/n]` は y/n 以外のキーを
     * 「はい」と読むので、**→ を押すと勝手に決定**し、プロンプトの枠も
     * 動かなくなった（動作確認 2026-08-11）。作成画面の [Y/n] も
     * ロール確認も**この層が持つ**（K-24 がそのために作った）。
     * 本当の不具合はカーソルの初期位置だった（`ui_cursor.cpp` の `sync`）。
     */
    if (ctx.hud_state.cursors.handle(cursor_nav_from_key(event.key), ctx.batch.input)) {
        return;
    }
    /*
     * **文字を編集している画面の矢印**（`ui/ui_cursor.h` の
     * `handle_text_edit_arrows`）。エディタは矢印を `SKEY_*` としてしか
     * 受け取れず、この線に SKEY は無い——そのうえ `move` はアダプタで
     * `'4'` `'8'` になるので、**押すたびに本文へ数字が入っていた**。
     * カーソル層の**後**に置くこと（数値入力・はい／いいえ・
     * 自動拾いエディタの ESC メニューは向こうのものである）。
     */
    if (handle_text_edit_arrows(ctx.frame, cursor_nav_from_key(event.key), ctx.batch.input)) {
        return;
    }
    /*
     * **Enter も A と同じ**（すぐ下のパッドの枝と同じ理屈）。変愚では
     * `\r` を受けたコアが通常メニューを開くので、旗を立てないあちらでは
     * ここは素通りする——キーボードの手触りも変わらない。
     */
    if (((sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER))
        && ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_GUI)) == 0)
        && confirm_opens_command_menu(ctx.frame)) {
        ctx.perform_action(kActionCommandMenu);
        return;
    }
    presentation::InputEventWire wire;
    if (translate_key_down(event.key, wire)) {
        //! 矢印・テンキーは**画面基準**。視点回転ぶんを当ててから積む（設計書 罠 1）。
        ctx.turn_screen_move(wire);
        ctx.batch.input.events.push_back(wire);
    }
    return;
}

} // namespace

/*!
 * @brief SDL の出来事を全部捌く（(a) 入力）。仕様は `run_parts.h`。
 * @details 本体は `run()` に在ったときの字面のまま。外の名前に `ctx.` が付いただけで、
 * 到着順に処理する。UI が消費した入力は同じイベントからコアへ再送しない。
 */
void pump_sdl_events(InputPumpContext &ctx)
{
    test_keyboard().poll("game");
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            ctx.batch.want_quit = true;
            continue;
        }
        if ((event.type == SDL_WINDOWEVENT) && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
            ctx.screen_w = event.window.data1;
            ctx.screen_h = event.window.data2;
            glViewport(0, 0, ctx.screen_w, ctx.screen_h);
            continue;
        }
        if ((event.type == SDL_WINDOWEVENT) && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
            //! **「握ったまま」を忘れる**（`game_pad.h` の約束 1・2）。
            ctx.pad.forget_hold();
            ctx.vpad.forget_hold(); //!< 指の掴みも捨てる（FINGERUP が来ない道があるため）
            continue;
        }
        /*
         * バーチャルパッド（実装方針 2026-08-12）。**マウスの分岐より先**に見る
         * ——ボタンに落ちた指（とそのマウス写し）はここが食い、タップ移動や
         * 一人称の視界に化けさせない。ボタンの外はいっさい触らず従来の道へ落とす。
         * 配置の編集画面が開いている間はポインタの出来事を全部ここが食う。
         */
        if (ctx.vpad.on_event(event, ctx.settings.vpad, ctx.usable_area(), ctx.screen_w, ctx.screen_h)) {
            continue;
        }
#if defined(__ANDROID__)
        /*
         * **タップしたらソフトキーボードを出し直す**（実装方針 2026-08-13）。
         * 戻るキーで閉じても**コアはまだ入力を待っている**ので、戻る道が要る。
         * バーチャルパッドより後・ほかの指の道より先に見る（パッドのボタンを
         * 押したつもりの指がキーボードを呼ばないように）。
         *
         * Android はタッチのマウス写しを切ってある（`hd2d_entry_android.cpp`）ので、
         * 指の出来事はマウスではなくここへ来る。
         *
         * **当たりは「パッド以外の画面ぜんぶ」**である。初版は入力行（`prompt_bar`）
         * だけにしていて、**名前入力で 1 度も効かなかった**（エミュレータで実測）。
         * `prompt_bar` は遊んでいる最中の `[y/n]` の行で、名前入力・生い立ちの編集・
         * 自動拾い／マクロのエディタは**全画面のターミナル**として描かれる
         * ——つまり文字入力の場面はどれも入力行があそこに無い。画面ごとに
         * 入力行の在り処を当てにいくより、**外れない所を当たりにする**ほうが正しい。
         *
         * 広げても誤爆しない。Android では文字入力の間にタップで起きることが
         * ほかに無い（タップ移動はマウス写しの道なので端から無い）。
         */
        if ((event.type == SDL_FINGERDOWN) && ctx.state.ime_prompt_open && ctx.state.ime_dismissed) {
            SDL_StartTextInput();
            ctx.state.ime_dismissed = false;
            continue;
        }
#endif
        if (ctx.pad.on_event(event)) {
            continue; // パッドの出来事はパッドが食う
        }
        /*
         * 一人称（おまけ）が**この 6 つのキーだけ**を持っているか。
         *
         * 実装方針は 2 段階で動いた。**いまは後者が正**:
         *   1.（2026-08-11）「元のキーボード操作は OFF にして WASDQE のみ有効に」
         *   2.（同日・訂正）「**WASDQE 以外は FPS モードでも使えるように**」
         * つまり一人称は WASDQE（＋右 Shift のメニュー）だけを横取りし、
         * ほかのキーは今までどおりコアのコマンドとして働く。
         *
         * 1 の発端は **Q が「飲む」を開く**ことだった。原因は「全部を食っていなかった」
         * ことではなく、**`SDL_TEXTINPUT` を塞いでいなかった**ことである（KEYDOWN を
         * 食っても印字文字は別のイベントで届く）。**塞ぐ場所は 2 つある**と覚える。
         * いまは `swallow_char` で「いま食った 1 文字」だけを落としている。
         *
         * メニュー（機能メニュー・コアの Term の写し・選択肢・数値入力）が出ている間は
         * 横取りもしない。一人称のまま店へ入る／罠の確認を聞かれることは普通に起きるので、
         * そこで WASD を食うと品物の a・s が選べない。判定は `fps_drives_movement()`
         * 1 か所に置いてある（パッドの方と食い違わせない）。
         */
        const bool fps_owns_move_keys = ctx.fps_drives_movement();
        /*
         * 一人称のマウス（実装方針 2026-08-11）:
         *   左クリック          決定
         *   右クリック          取消
         *   右ドラッグ          視点の自由移動（離した所で最寄りの 8 方位へ吸着）
         *   ホイール            画角 90〜120 度
         *
         * **キーと違って選択肢が出ていても横取りする。**キーの方は「店で品物の a・s が
         * 選べなくなる」ので譲っているが、マウスにはその衝突が無い。むしろ選択肢や
         * はい／いいえこそ決定・取消を押したい場面なので、ここで譲ると使えない。
         */
        const bool fps_owns_mouse = ctx.first_person.active && !ctx.feature_menu.is_open();
        /*
         * **視界を回すのは地図が見えているときだけ。**メニューが覆っている間も回すと、
         * 店から出た瞬間に景色が明後日を向いている。ボタン（決定・取消）は覆われていても
         * 効かせたいので、条件を分けてある。
         */
        const bool fps_owns_look = fps_owns_mouse && frame_shows_map(ctx.frame);
        /*
         * IME の未確定文字列。**コアへは送らない**（確定するまでは文字ではない）。
         * 覚えておいて、こちらで入力欄の桁に描く（`ime_edit_text` の注記）。
         */
        if (event.type == SDL_TEXTEDITING) {
            /*
             * **1 度だけ出す。**「候補は出るのに打っている字が見えない」の相談は、
             * この行が出ているかどうかで**こちらへ届いていない**のか
             * **描けていない**のかが分かれる（毎回出すと変換のたびに埋まる）。
             */
            static bool told = false;
            if (!told && (event.edit.text[0] != '\0')) {
                told = true;
                std::fprintf(stderr, "[hd2d] IME の未確定文字列を受け取りました（こちらで描きます）\n");
            }
            ctx.state.ime_edit_text = event.edit.text;
            continue;
        }
        if (event.type == SDL_TEXTINPUT) {
            pump_text_input(ctx, event); //!< 中身は上の関数
            continue;
        }
        /*
         * カメラの手直し（計画 §4-1 の「実物を見て決める」項目）。
         * **マウスだけを使う**（キーはコアへ転送しているので衝突する）。
         *   ホイール          … 1 マスの見かけの大きさ（＝距離）
         *   Shift + ホイール  … 見下ろし角
         *   Ctrl  + ホイール  … 水平画角
         * いまの値は画面に出ているので、気に入った値をそのまま `--camera=` に書ける。
         */
        if (event.type == SDL_MOUSEWHEEL) {
            const SDL_Keymod mod = SDL_GetModState();
            const auto step = static_cast<float>(event.wheel.y);
            if (fps_owns_mouse) {
                /*
                 * 一人称のホイールは**画角**（実装方針 2026-08-11・90〜120 度）。
                 * 見下ろしの「1 マスの大きさ」は一人称では意味を持たない（カメラは
                 * マスの中に立っていて距離が無い）ので、同じ回し方を当てても何も動かない。
                 */
                ctx.first_person.adjust_fov(event.wheel.y);
                ctx.settings.fps_fov_deg = ctx.first_person.fov_deg; //!< cfg へ残す
                continue;
            }
            //! **設定へ書く**（P8）。カメラへ直に書くと機能メニューの数字とずれ、cfg にも残らない。
            if ((mod & KMOD_SHIFT) != 0) {
                ctx.settings.camera_pitch_deg = std::clamp(ctx.settings.camera_pitch_deg + step, 10.f, 85.f);
            } else if ((mod & KMOD_CTRL) != 0) {
                ctx.settings.camera_fov_deg = std::clamp(ctx.settings.camera_fov_deg + step, 10.f, 80.f);
            } else {
                ctx.settings.camera_cell_px = std::clamp(
                    ctx.settings.camera_cell_px * ((step > 0.f) ? 1.08f : 0.926f), 24.f, 400.f);
            }
            continue;
        }
        /*
         * 一人称のマウス（実装方針 2026-08-11・**改訂**）。
         * 「右クリックをマウススライドではなく、マウススライド単体で視界移動に」
         * ＝ **ボタンを押さずに動かすだけで視界が動く。**吸着も無い。
         * したがって右ボタンは**取消だけ**を意味する。
         */
        if (fps_owns_look && (event.type == SDL_MOUSEMOTION) && (ctx.hud_state.grabbed_grip < 0)) {
            //! 相対量（`xrel`/`yrel`）で回す。相対マウスモード中は座標が来ないので必須。
            ctx.first_person.look(event.motion.xrel, event.motion.yrel);
            continue;
        }
        if (fps_owns_mouse && (event.type == SDL_MOUSEBUTTONDOWN)
            && (event.button.button == SDL_BUTTON_RIGHT)) {
            presentation::InputEventWire wire;
            wire.e = "cancel";
            ctx.batch.input.events.push_back(wire);
            continue;
        }
        if (fps_owns_mouse && (event.type == SDL_MOUSEBUTTONDOWN)
            && (event.button.button == SDL_BUTTON_MIDDLE)) {
            /*
             * ホイールの押し込み ＝ **視界を水平に戻す**（実装方針 2026-08-11）。
             * これだけは割り当ての表に無い（マウスのボタンは表に並べていない）ので、
             * ここで直に見ている。同じ操作はコントローラー R3 とキーの割り当てからも出せる。
             */
            ctx.first_person.level_view();
            continue;
        }
        if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_RIGHT)) {
            continue; //!< 右ボタンは押した所で効かせた。離しは捨てる
        }
        if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_LEFT)) {
            //! 掴みを離す。**cfg へ書くのはここから先**（動かすたびにディスクを叩かない）。
            ctx.hud_state.grabbed_grip = -1;
            continue;
        }
        if ((event.type == SDL_MOUSEMOTION) && (ctx.hud_state.grabbed_grip >= 0)) {
            //! 掴んでいる仕切りを動かす。次のフレームの `compute()` が新しい形で組み直す。
            (void)drag_grip(ctx.layout, ctx.hud_state.grabbed_grip, event.motion.x, event.motion.y,
                ctx.settings.sub_split);
            continue;
        }
        if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_LEFT)) {
            /*
             * 下段の境界を掴む（P8。既存 UI の K-31 相当）。**地図より先に見る**
             * ので、境界の上をクリックしても歩き出さない。
             */
            if (!ctx.feature_menu.is_open()) {
                const int grip = grip_at(ctx.layout, event.button.x, event.button.y);
                if (grip >= 0) {
                    ctx.hud_state.grabbed_grip = grip;
                    ctx.click_path.cancel();
                    continue;
                }
            }
            /*
             * Term の写しに出ている**選択肢へのクリック**（P5 その2・2026-08-19。
             * 不具合の確認「セーブデータ選択が文字指定でしか選択できない」）。
             * 当たりは描画と同じ計算（`menu_choice_at`）なので見えている枠と
             * ずれない。送るのはカーソル層の決定と同じ「その選択肢の 1 キー」
             * ——幻想蛮怒のセーブ選択だけでなく、選択肢が立つ画面すべて
             * （変愚の店・建物の一覧）で効く。
             */
            if (!frame_shows_map(ctx.frame)) {
                const int hit = menu_choice_at(ctx.text, ctx.layout, ctx.frame, event.button.x, event.button.y);
                if (hit >= 0) {
                    const MenuChoice &choice = ctx.frame.menu_choices[static_cast<std::size_t>(hit)];
                    presentation::InputEventWire wire;
                    if (choice.key == '\r') {
                        wire.e = "confirm";
                    } else if (choice.key == 0x1B) {
                        wire.e = "cancel";
                    } else {
                        wire.e = "key";
                        wire.chr = std::string(1, static_cast<char>(choice.key));
                    }
                    ctx.batch.input.events.push_back(wire);
                    ctx.click_path.cancel();
                    continue;
                }
                if (!fps_owns_mouse) {
                    //! 写しの外れをクリックしても歩き出さない（下の unproject へ流さない）。
                    continue;
                }
            }
            /*
             * 一人称の左クリックは**決定**（実装方針 2026-08-11）。
             * 一人称では地面を指しても「そこへ歩く」が読めない（見えているのは
             * 目の高さの壁で、床の 1 マスを狙って押せる作りになっていない）ので、
             * クリック移動は見下ろしのときだけの操作にする。
             * **下段の境界を掴む道より後**に置く（掴みが決定に食われない）。
             */
            if (fps_owns_mouse) {
                ctx.click_path.cancel();
                if (!ctx.hud_state.cursors.handle(CursorNav::Confirm, ctx.batch.input)) {
                    presentation::InputEventWire wire;
                    wire.e = "confirm";
                    ctx.batch.input.events.push_back(wire);
                }
                continue;
            }
            /*
             * クリックした所まで歩く（P8）。**隣なら 1 歩、遠ければ経路**。
             * 経路は既知のマスの上だけを通す（見ていない所を通り抜ける経路は引かない）。
             */
            ctx.click_path.cancel();
            if (ctx.feature_menu.is_open() || !ctx.layout.scene.contains(event.button.x, event.button.y)) {
                continue;
            }
            Vec3 ground{};
            const float local_x = static_cast<float>(event.button.x - ctx.layout.scene.x) + 0.5f;
            const float local_y = static_cast<float>(event.button.y - ctx.layout.scene.y) + 0.5f;
            if (!ctx.camera.unproject_to_plane(local_x, local_y, 0.f, ground)) {
                continue; // 地平線より上
            }
            const int gx = static_cast<int>(std::floor(ground.x));
            const int gy = static_cast<int>(std::floor(ground.y));
            const int dx = gx - ctx.frame.player_gx;
            const int dy = gy - ctx.frame.player_gy;
            if ((dx == 0) && (dy == 0)) {
                continue;
            }
            if ((std::abs(dx) <= 1) && (std::abs(dy) <= 1)) {
                presentation::InputEventWire wire;
                wire.e = "move";
                wire.dx = dx;
                wire.dy = dy;
                ctx.batch.input.events.push_back(wire);
                continue;
            }
            (void)ctx.click_path.begin(ctx.frame.minimap, ctx.frame.player_gx, ctx.frame.player_gy, gx, gy);
            continue;
        }
        if (event.type == SDL_MOUSEMOTION) {
            /*
             * ヒットテスト（P2 ④）。画素の**中心**を渡す（往復検査と同じ約束）。
             * **`scene.x/y` を引く**（P8）。カメラは 3D の矩形の中の座標しか知らない。
             * ここを忘れると `Split` で「描いた絵と当たり判定が食い違う」（必守制約 3）。
             */
            const float local_x = static_cast<float>(event.motion.x - ctx.layout.scene.x) + 0.5f;
            const float local_y = static_cast<float>(event.motion.y - ctx.layout.scene.y) + 0.5f;
            Vec3 ground{};
            ctx.state.hover_valid = ctx.layout.scene.contains(event.motion.x, event.motion.y)
                && ctx.camera.unproject_to_plane(local_x, local_y, 0.f, ground);
            if (ctx.state.hover_valid) {
                ctx.state.hover_gx = static_cast<int>(std::floor(ground.x));
                ctx.state.hover_gy = static_cast<int>(std::floor(ground.y));
            }
            continue;
        }
        if (event.type == SDL_KEYUP) {
            //! 一人称の向き替えは**離した所で刻みへ吸い付く**（`FpsMode::turn_hold_end`）。
            if (event.key.keysym.sym == SDLK_q) {
                ctx.first_person.turn_hold_end(-1);
            } else if (event.key.keysym.sym == SDLK_e) {
                ctx.first_person.turn_hold_end(1);
            }
            continue; //!< コアは KEYUP を使わない（印字は TEXTINPUT から採っている）
        }
        if (event.type == SDL_KEYDOWN) {
            pump_key_down(ctx, event, fps_owns_move_keys); //!< 中身は上の関数（長いので分けた）
            continue;
        }
    }
}

} // namespace hd2d
