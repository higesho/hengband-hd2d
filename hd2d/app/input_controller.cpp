/*! @file input_controller.cpp
 * @brief SDL のイベントを UI の操作またはコアへの入力へ変換する。
 */
#include "app/input_controller.h"
#include "app/test_keyboard.h"
#include "app/ui_probe.h"
#include "app/app_clock.h"
#include "ui/key_binds.h"
#include "ui/ui_cursor.h"
#include "ui/menu_scale.h"
#include "render/gl_core.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using namespace hd2d::gl;

namespace hd2d {

namespace {

//! ホイールの位置。実マウスの位置を読む。試験入力（`{"wheel":{...,"at":[x,y]}}`）が位置を指定したときだけそれを使う。
void wheel_pointer(int &x, int &y)
{
    SDL_GetMouseState(&x, &y);
    (void)ui_probe().wheel_at(x, y);
}

/*!
 * @brief 画面の点（窓の座標）が指すダンジョンのマス。立体の絵の上でも 2D アスキーでも同じ問いに答える。
 *
 * @details 立体では、立っている実体（モンスター・アイテムの絵）を先に見る——背の高いものは足元より
 * 上の点を指されることが多く、地面へ落とすと 1 マス奥へずれる。当たる実体が無ければ地面（高さ 0）へ
 * 逆写像する。視点の回転・傾き・距離はカメラが持っているので、ここでは何も知らなくてよい
 * （回した先でも、絵に見えているマスがそのまま返る）。2D アスキーは字の格子から求める。
 *
 * 狙う・見る（`select_target_at()`）と地図のクリック（AH-58 の `begin_mouse_map_click()`）が
 * **同じ関数を通る**。別々に書くと、片方だけ回転の後でずれる。
 *
 * @return 地図の矩形の外・地平線より上・字の格子の外なら偽。
 */
bool pick_map_cell(const InputPumpContext &ctx, int screen_x, int screen_y, TargetingPoint &point) {
    if(!ctx.layout.scene.contains(screen_x,screen_y))return false;
    if(ctx.settings.main_panel==Hd2dSettings::MainPanel::Ascii)
        return ctx.hud_state.targeting.map_projection.pick(screen_x,screen_y,point);
    const float x=float(screen_x-ctx.layout.scene.x)+.5f,y=float(screen_y-ctx.layout.scene.y)+.5f;
    if(pick_targeting_entity(ctx.camera.eye(),ctx.camera.ray_direction(x,y),ctx.targeting_entities,point))return true;
    Vec3 ground;
    if(!ctx.camera.unproject_to_plane(x,y,0.f,ground))return false;
    point={int(std::floor(ground.x)),int(std::floor(ground.y))};
    return true;
}

void select_target_at(InputPumpContext &ctx,int screen_x,int screen_y) {
    ctx.click_path.cancel();
    if(ctx.hud_state.targeting.click_toolbar(screen_x,screen_y,ctx.batch.input))return;
    TargetingPoint point;
    if(pick_map_cell(ctx,screen_x,screen_y,point))ctx.hud_state.targeting.select(point,ctx.batch.input);
}

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
 * @brief ローグライクのキー配置の方向の文字（`hjklyubn`。大文字も同じ向き）をテンキーの数字にする。
 * @return 方向の文字でなければ `'\0'`。
 * @details 変愚系・Angband・Sil-Q・Umoria・Rogue 系のローグライクのキー配置は、どれも vi と同じ
 * `y k u / h . l / b j n` の並びで、大文字は「走る」（2026-10-07）。
 */
char rogue_letter_to_digit(char c)
{
    if ((c >= 'A') && (c <= 'Z')) {
        c = static_cast<char>(c - 'A' + 'a');
    }
    switch (c) {
    case 'b': return '1';
    case 'j': return '2';
    case 'n': return '3';
    case 'h': return '4';
    case 'l': return '6';
    case 'y': return '7';
    case 'k': return '8';
    case 'u': return '9';
    default: return '\0';
    }
}

//! 上の逆（小文字）。8 近傍の数字でなければ `'\0'`。
char digit_to_rogue_letter(char digit)
{
    switch (digit) {
    case '1': return 'b';
    case '2': return 'j';
    case '3': return 'n';
    case '4': return 'h';
    case '6': return 'l';
    case '7': return 'y';
    case '8': return 'k';
    case '9': return 'u';
    default: return '\0';
    }
}

/*!
 * @brief ローグライクのキー配置の方向の文字を、見下ろしの視点回転ぶん回す（大文字・小文字は保つ）。
 *
 * @details 矢印・テンキー・数字キーは視点回転ぶん回して送っている（`turn_screen_move` と
 * `pump_text_input` の数字の枝）。ローグライクのキー配置の `hjkl` も同じく**画面基準**で
 * 歩けるようにする（2026-10-07。回さないと、90° 回した画面で `k` が画面の横へ歩く）。
 *
 * 回してよいのは、**コアがコマンドを待っている**ときだけ（`frame.awaiting_command`）。
 * 持ち物の選択では `h` `j` が持ち物の記号なので、回すと別の物を選んでしまう。
 */
void turn_rogue_letter(const InputPumpContext &ctx, char &c)
{
    if ((ctx.pad_keymap != "rogue") || !ctx.frame.awaiting_command || !ctx.turn_drives_movement()) {
        return;
    }
    const char digit = rogue_letter_to_digit(c);
    int dx = 0;
    int dy = 0;
    if ((digit == '\0') || !digit_to_screen_delta(digit, dx, dy)) {
        return;
    }
    rotate_screen_delta(ctx.camera_turn, dx, dy);
    const char letter = digit_to_rogue_letter(screen_delta_to_digit(dx, dy));
    if (letter == '\0') {
        return;
    }
    c = ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(letter - 'a' + 'A') : letter;
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

/* Preserve the physical identity of keys while the native macro/custom-key
 * editor is recording a trigger.  `pref-win.prf` defines these in the same
 * Win32/DirectInput PC/AT notation used by the original frontend.  Ordinary
 * input must keep using semantic move/key events; this raw sequence is only
 * valid while `capture_keys` is active. */
bool translate_captured_special_key(const SDL_KeyboardEvent &key, presentation::InputEventWire &out)
{
    int scan = 0;
    bool keypad = false;
    switch (key.keysym.sym) {
    case SDLK_BACKSPACE: scan = 0x0E; break;
    case SDLK_TAB: scan = 0x0F; break;
    case SDLK_HOME: scan = 0x47; break;
    case SDLK_UP: scan = 0x48; break;
    case SDLK_PAGEUP: scan = 0x49; break;
    case SDLK_LEFT: scan = 0x4B; break;
    case SDLK_RIGHT: scan = 0x4D; break;
    case SDLK_END: scan = 0x4F; break;
    case SDLK_DOWN: scan = 0x50; break;
    case SDLK_PAGEDOWN: scan = 0x51; break;
    case SDLK_INSERT: scan = 0x52; break;
    case SDLK_DELETE: scan = 0x53; break;
    case SDLK_KP_DIVIDE: scan = 0x35; keypad = true; break;
    case SDLK_KP_MULTIPLY: scan = 0x37; keypad = true; break;
    case SDLK_KP_7: scan = 0x47; keypad = true; break;
    case SDLK_KP_8: scan = 0x48; keypad = true; break;
    case SDLK_KP_9: scan = 0x49; keypad = true; break;
    case SDLK_KP_MINUS: scan = 0x4A; keypad = true; break;
    case SDLK_KP_4: scan = 0x4B; keypad = true; break;
    case SDLK_KP_5: scan = 0x4C; keypad = true; break;
    case SDLK_KP_6: scan = 0x4D; keypad = true; break;
    case SDLK_KP_PLUS: scan = 0x4E; keypad = true; break;
    case SDLK_KP_1: scan = 0x4F; keypad = true; break;
    case SDLK_KP_2: scan = 0x50; keypad = true; break;
    case SDLK_KP_3: scan = 0x51; keypad = true; break;
    case SDLK_KP_0: scan = 0x52; keypad = true; break;
    case SDLK_KP_PERIOD: scan = 0x53; keypad = true; break;
    default: return false;
    }
    static constexpr char hex[] = "0123456789ABCDEF";
    out = {};
    out.e = "keyseq";
    out.bytes.push_back(31);
    if ((key.keysym.mod & KMOD_CTRL) != 0) out.bytes.push_back('C');
    if ((key.keysym.mod & KMOD_SHIFT) != 0) out.bytes.push_back('S');
    if ((key.keysym.mod & KMOD_ALT) != 0) out.bytes.push_back('A');
    out.bytes.push_back('x');
    if (keypad) out.bytes.push_back('K');
    out.bytes.push_back(hex[(scan >> 4) & 0x0F]);
    out.bytes.push_back(hex[scan & 0x0F]);
    out.bytes.push_back(13);
    return true;
}

} // namespace

namespace {

/* ================================================================ 地図のクリック・タップ（AH-58） */

//! 検査用の出力先（`HD2D_CLICK_TRACE=<パス>`）。立っていなければ空。起動後に 1 度だけ読む。
const std::string &click_trace_path()
{
    static const std::string path = [] {
        const char *const env = std::getenv("HD2D_CLICK_TRACE");
        return std::string((env != nullptr) ? env : "");
    }();
    return path;
}

//! UTF-8 の文字列から `std::filesystem::path` へ（`u8path` は C++20 で非推奨）。
std::filesystem::path click_trace_file(const std::string &utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

//! 検査用: 押した・送った・捨てたを 1 行ずつ `<パス>.log` へ足す。
void click_trace_event(const nlohmann::json &entry)
{
    const std::string &path = click_trace_path();
    if (path.empty()) {
        return;
    }
    std::ofstream out(click_trace_file(path + ".log"), std::ios::binary | std::ios::app);
    out << entry.dump() << '\n';
}

void trace_map_click(const InputPumpContext &ctx, const char *event, const char *source, const MapClick *click,
    const char *why)
{
    if (click_trace_path().empty()) {
        return;
    }
    nlohmann::json entry{ { "ev", event }, { "src", source }, { "tick", clock::ticks() },
        { "frame_id", ctx.frame.frame_id } };
    if (click != nullptr) {
        entry["x"] = click->gx;
        entry["y"] = click->gy;
        entry["button"] = (click->button == MapClickButton::Right) ? "right" : "left";
        entry["shift"] = click->mods.shift;
        entry["ctrl"] = click->mods.ctrl;
        entry["alt"] = click->mods.alt;
        entry["screen_x"] = click->sx;
        entry["screen_y"] = click->sy;
    }
    if (why != nullptr) {
        entry["why"] = why;
    }
    click_trace_event(entry);
}

//! いま押されている修飾キー。押した瞬間に読む（`map_click` の `mods`）。
MapClickMods map_click_mods()
{
    const int mods = mods_from_sdl(static_cast<Uint16>(SDL_GetModState()));
    MapClickMods out;
    out.shift = (mods & kModShift) != 0;
    out.ctrl = (mods & kModCtrl) != 0;
    out.alt = (mods & kModAlt) != 0;
    return out;
}

/*!
 * @brief 地図のクリックを始めてよい画面でない理由（`nullptr` なら始めてよい）。
 * @details 画面側の状態（機能メニュー・カットイン・配置の編集・新 UI の窓・目標選び・一人称）と、
 * 最新のフレームの姿（`map_click_frame_block_reason()`）を続けて見る。
 */
const char *map_click_ui_block_reason(const InputPumpContext &ctx)
{
    if (ctx.first_person.active) {
        return "first person"; //!< 左クリックは決定・右クリックは取消（一人称のマウス）
    }
    if (ctx.feature_menu.is_open()) {
        return "feature menu";
    }
    if (ctx.floor_cutin.blocks_input()) {
        return "cut-in";
    }
    if (ctx.vpad.editor_open()) {
        return "pad editor";
    }
    if (ctx.hud_state.modern.active()) {
        return "window open";
    }
    if (ctx.hud_state.targeting.active()) {
        return "targeting";
    }
    return map_click_frame_block_reason(ctx.frame);
}

/*!
 * @brief 組み上がったクリックを送る。**送ってよい場面かをここでもう一度見る**（押している間に画面が変わりうる）。
 * @details 送るのは `input_event` の `map_click` 1 つ。返事は無く、効果は次のフレームに出る。
 * 送ったら次のフレームが届くまで（または `MapClickGate::kReplyTimeoutMs`）続けて送らない。
 */
void dispatch_map_click(InputPumpContext &ctx, const MapClick &click, const char *source)
{
    const std::uint32_t now = clock::ticks();
    const char *why = map_click_ui_block_reason(ctx);
    if ((why == nullptr) && !map_click_cell_valid(ctx.frame, click.gx, click.gy)) {
        why = "cell outside the floor";
    }
    if ((why == nullptr) && !ctx.state.map_click.gate.ready(ctx.frame.frame_id, now)) {
        why = "waiting for the reply to the previous click";
    }
    if (why != nullptr) {
        trace_map_click(ctx, "dropped", source, &click, why);
        return;
    }
    push_map_click(ctx.batch.input, click);
    ctx.state.map_click.gate.sent(ctx.frame.frame_id, now);
    ctx.click_path.cancel(); //!< 自分で歩く道（コアが持たないコア向け）は使わない
    if ((click.button == MapClickButton::Right) && !click.mods.shift && !click.mods.ctrl && !click.mods.alt) {
        //! 修飾なしの右クリック（長押しを含む）はコアが文脈メニューを開く。押した点の近くへ出すため覚える。
        ctx.state.map_click.anchor_valid = true;
        ctx.state.map_click.anchor_x = click.sx;
        ctx.state.map_click.anchor_y = click.sy;
    }
    trace_map_click(ctx, "sent", source, &click, nullptr);
}

//! 実マウスのボタンを押した。押した所のマスと修飾をここで決め、離した所で送る。
void begin_mouse_map_click(InputPumpContext &ctx, Uint8 button, int x, int y)
{
    const MapClickButton which = (button == SDL_BUTTON_RIGHT) ? MapClickButton::Right : MapClickButton::Left;
    TargetingPoint cell{};
    const char *why = map_click_ui_block_reason(ctx);
    if ((why == nullptr) && !pick_map_cell(ctx, x, y, cell)) {
        why = "no cell under the pointer";
    }
    MapClick click;
    click.button = which;
    click.gx = cell.x;
    click.gy = cell.y;
    click.mods = map_click_mods();
    click.sx = x;
    click.sy = y;
    if (why != nullptr) {
        trace_map_click(ctx, "ignored", "mouse (press)", nullptr, why);
    }
    if (why == nullptr) {
        trace_map_click(ctx, "press", "mouse", &click, nullptr);
    }
    (void)ctx.state.map_click.gesture.press(
        MapClickGesture::Source::Mouse, button, x, y, click, clock::ticks(), why == nullptr);
}

//! 実マウスのボタンを離した。動かさずに離していれば、押した所のマスへ送る。
void end_mouse_map_click(InputPumpContext &ctx, Uint8 button, int x, int y)
{
    MapClick click;
    if (ctx.state.map_click.gesture.release(MapClickGesture::Source::Mouse, button, x, y, clock::ticks(), click)) {
        dispatch_map_click(ctx, click, "mouse");
    } else {
        trace_map_click(ctx, "release", "mouse", nullptr, "not a click (dragged, or no press was followed)");
    }
}

/*!
 * @brief バーチャルパッドを出している間、パッドの外へのポインタの出来事を捨てるか（修-12）。
 * @details 呼ぶのは `VirtualPad::on_event()` が食わなかった後。指（`SDL_FINGER*`）・マウスのボタン・
 * ホイール・マウスの移動が対象。移動も捨てるのは、一人称では押さずに動かすだけで視界が回り、
 * メニューではホバーの強調が出て、押せない部品が押せるように見えるため。
 * 編集画面が開いている間は `on_event()` が全部食うので、ここへは来ない。
 */
bool pointer_blocked_by_vpad(const InputPumpContext &ctx, const SDL_Event &event)
{
    if (!ctx.settings.vpad.show) {
        return false;
    }
    switch (event.type) {
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
    case SDL_MOUSEMOTION:
        return true;
    default:
        return false;
    }
}

/*!
 * @brief 指の出来事（`SDL_FINGER*`）から地図のタップを組む。短いタップ＝左・長押し＝右。
 * @details 長押しの時間は `pump_sdl_events()` の最後の `poll()` が見る。
 * 指がパッドのボタンの上や近くに触れたときは**タップにしない**——ボタンを押し損ねた指が、
 * その足元のマスへの移動になってしまう（AH-58。ボタンの当たりは `VirtualPad::on_event()` が
 * すでに食っているので、ここへ来るのは外れた指だけ）。
 */
void pump_map_click_finger(InputPumpContext &ctx, const SDL_Event &event)
{
    const int x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.screen_w));
    const int y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.screen_h));
    const std::int64_t finger = static_cast<std::int64_t>(event.tfinger.fingerId);
    MapClickGesture &gesture = ctx.state.map_click.gesture;
    const std::uint32_t now = clock::ticks();
    switch (event.type) {
    case SDL_FINGERDOWN: {
        TargetingPoint cell{};
        const char *why = nullptr;
        if (!ctx.layout.scene.contains(x, y)) {
            why = "outside the map";
        } else if (ctx.vpad.near_control(static_cast<float>(x), static_cast<float>(y), ctx.settings.vpad,
                       ctx.usable_area())) {
            why = "near a pad button";
        } else {
            why = map_click_ui_block_reason(ctx);
        }
        if ((why == nullptr) && !pick_map_cell(ctx, x, y, cell)) {
            why = "no cell under the finger";
        }
        MapClick click;
        click.button = MapClickButton::Left;
        click.gx = cell.x;
        click.gy = cell.y;
        click.mods = map_click_mods();
        click.sx = x;
        click.sy = y;
        if (why != nullptr) {
            trace_map_click(ctx, "ignored", "touch (press)", nullptr, why);
        }
        if (why == nullptr) {
            trace_map_click(ctx, "press", "touch", &click, nullptr);
        }
        (void)gesture.press(MapClickGesture::Source::Touch, finger, x, y, click, now, why == nullptr);
        break;
    }
    case SDL_FINGERMOTION:
        gesture.move(MapClickGesture::Source::Touch, finger, x, y);
        break;
    case SDL_FINGERUP: {
        MapClick click;
        if (gesture.release(MapClickGesture::Source::Touch, finger, x, y, now, click)) {
            dispatch_map_click(ctx, click, "touch");
        } else {
            trace_map_click(ctx, "release", "touch", nullptr, "not a tap (dragged, long press already sent, or two fingers)");
        }
        break;
    }
    default:
        break;
    }
}

/*!
 * @brief 検査用: 窓・カメラ・フレームの姿と、マスの画面座標を `<パス>` へ書く（`HD2D_CLICK_TRACE`）。
 * @details 実窓の検査（`tools/angband/ah58_window_click.py`）が「このマスを押す」ために、拾い方の
 * **逆**（マス → 画面の点）をここから読む。カメラは直前に描いたもの——ここに書いた点を押せば
 * `pick_map_cell()` がそのマスへ戻す。環境変数が無ければ何もしない（遊ぶときの動きは変わらない）。
 * 書くのは変わったときだけ（40 ms に 1 度まで）。読み手が書きかけを見ないよう、別名で書いて差し替える。
 */
void write_click_snapshot(const InputPumpContext &ctx)
{
    const std::string &path = click_trace_path();
    if (path.empty()) {
        return;
    }
    static Uint32 last_at = 0;
    static std::string last_text;
    const Uint32 now = clock::ticks();
    if ((last_at != 0) && ((now - last_at) < 40)) {
        return;
    }
    last_at = now;
    const RectPx scene = ctx.layout.scene;
    const bool ascii = ctx.settings.main_panel == Hd2dSettings::MainPanel::Ascii;
    const TargetingMapProjection &grid = ctx.hud_state.targeting.map_projection;
    nlohmann::json cells = nlohmann::json::array();
    constexpr int kRadius = 16;
    for (int gy = ctx.frame.player_gy - kRadius; gy <= ctx.frame.player_gy + kRadius; ++gy) {
        for (int gx = ctx.frame.player_gx - kRadius; gx <= ctx.frame.player_gx + kRadius; ++gx) {
            int sx = 0;
            int sy = 0;
            if (ascii) {
                if ((grid.cell_w <= 0) || (grid.cell_h <= 0)) {
                    continue;
                }
                const int col = gx - grid.cam_x + (grid.cols / 2);
                const int row = gy - grid.cam_y + (grid.rows / 2);
                if ((col < 0) || (row < 0) || (col >= grid.cols) || (row >= grid.rows)) {
                    continue;
                }
                sx = grid.x + (col * grid.cell_w) + (grid.cell_w / 2);
                sy = grid.y + (row * grid.cell_h) + (grid.cell_h / 2);
            } else {
                float px = 0.f;
                float py = 0.f;
                if (!ctx.camera.project(Vec3{ static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, 0.f }, px, py)) {
                    continue;
                }
                sx = static_cast<int>(std::lround(px - 0.5f)) + scene.x;
                sy = static_cast<int>(std::lround(py - 0.5f)) + scene.y;
            }
            if (scene.contains(sx, sy)) {
                cells.push_back({ gx, gy, sx, sy });
            }
        }
    }
    nlohmann::json entities = nlohmann::json::array();
    if (!ascii) {
        for (const TargetingEntity &body : ctx.targeting_entities) {
            float px = 0.f;
            float py = 0.f;
            if ((body.id == -1)
                || !ctx.camera.project(Vec3{ body.x, body.y, body.z + (body.height * 0.5f) }, px, py)) {
                continue;
            }
            const int sx = static_cast<int>(std::lround(px - 0.5f)) + scene.x;
            const int sy = static_cast<int>(std::lround(py - 0.5f)) + scene.y;
            if (scene.contains(sx, sy)) {
                entities.push_back({ body.gx, body.gy, sx, sy, body.id });
            }
        }
    }
    const char *const block = ctx.map_click_enabled ? map_click_ui_block_reason(ctx) : "core does not declare map_click";
    nlohmann::json snapshot{ { "window", { ctx.screen_w, ctx.screen_h } },
        { "scene", { scene.x, scene.y, scene.w, scene.h } }, { "frame_id", ctx.frame.frame_id },
        { "player", { ctx.frame.player_gx, ctx.frame.player_gy } }, { "awaiting_command", ctx.frame.awaiting_command },
        { "menu_open", ctx.frame.menu_open }, { "targeting", ctx.frame.targeting.active },
        { "block", (block != nullptr) ? block : "" }, { "main_panel", ascii ? "ascii" : "hd2d" },
        { "camera_turn", ctx.camera_turn }, { "yaw", ctx.camera.yaw }, { "pitch", ctx.camera.pitch },
        { "fov", ctx.camera.fov_x }, { "first_person", ctx.first_person.active },
        { "modern_active", ctx.hud_state.modern.active() }, { "modern_stage", ctx.frame.modern.stage },
        { "modern_rows", ctx.frame.modern.rows.size() }, { "term_lines", ctx.frame.menu_term_lines.size() },
        { "menu_over_map", ctx.frame.menu_over_map }, { "cells", cells }, { "entities", entities } };
    const std::string text = snapshot.dump();
    if (text == last_text) {
        return;
    }
    last_text = text;
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(click_trace_file(temporary), std::ios::binary | std::ios::trunc);
        out << text;
    }
    std::error_code ec;
    std::filesystem::rename(click_trace_file(temporary), click_trace_file(path), ec);
}

/*!
 * @brief `SDL_TEXTINPUT`（印字文字と IME の確定）を捌く。`pump_sdl_events()` の一部。
 *
 * @details 確定文字を処理し、UI が消費した打鍵の文字を抑止する。非 ASCII は自由文字入力中だけ送る。
 */
void pump_text_input(InputPumpContext &ctx, const SDL_Event &event)
{
    ctx.state.ime_edit_text.clear(); //!< 確定した。未確定の表示は消す
    if (ctx.batch.swallow_next_text
        || ((ctx.batch.swallow_char != '\0') && (event.text.text[0] == ctx.batch.swallow_char)
            && (event.text.text[1] == '\0'))) {
        //! 直前の KEYDOWN で UI が食った 1 文字。**その 1 個だけ**落とす。
        ctx.batch.swallow_next_text = false;
        ctx.batch.swallow_char = '\0';
        return;
    }
    ctx.batch.swallow_char = '\0';
    if (ctx.feature_menu.is_open()) {
        if(!ctx.feature_menu.waiting_for_key()&&event.text.text[0]&&event.text.text[1]=='\0'&&static_cast<unsigned char>(event.text.text[0])<128)
            ctx.feature_menu.handle_native_key(static_cast<unsigned char>(event.text.text[0]),ctx.settings);
        return; // Never send a menu shortcut to the underlying game as well.
    }
    if(ctx.hud_state.targeting.active()) {
        if(event.text.text[0]&&event.text.text[1]=='\0') {
            /*
             * ローグライクのキー配置では `hjklyubn`（大文字も）が方向（2026-10-07）。原作の照準も
             * 方向を変愚系は `get_keymap_dir()`、Angband は `target_dir()` でキー配置から読む。
             * 追加操作と重なる字（Angband の `k`）は、原作のローグライクのキー配置でも方向が勝つ
             * （「無視する」は ^D へ移る。`angband/adapter/an_targeting.c`）。
             */
            const char digit=(ctx.pad_keymap=="rogue")?rogue_letter_to_digit(event.text.text[0]):'\0';
            int dx=0,dy=0;
            if(digit!='\0'&&digit_to_screen_delta(digit,dx,dy)) {
                presentation::InputEventWire move;move.e="move";move.dx=dx;move.dy=dy;
                ctx.turn_screen_move(move);
                ctx.hud_state.targeting.move(5+move.dx-3*move.dy,ctx.batch.input);
                return;
            }
            //! AH-54: 見る・狙うの追加操作（`<` `>` `x` `p` `m` `g` `k`）は原作のキーのまま打てる。
            ctx.hud_state.targeting.extra_key(static_cast<unsigned char>(event.text.text[0]),ctx.batch.input);
        }
        return;
    }
    if(ctx.hud_state.modern.active()) {ctx.hud_state.modern.text(event.text.text);return;}
    /*
     * The modern UI already has one consolidated inventory/equipment view.
     * Let the traditional direct `i` / `e` shortcuts open that same view
     * instead of falling back to each core's passive full-screen text list.
     *
     * Check the advertised command table before consuming the key so imported
     * or future cores remain free to assign `i`/`e` to something else.
     */
    if(ctx.settings.modern_ui&&ctx.frame.awaiting_command&&!ctx.frame.pre_game_menu
        &&event.text.text[0]&&event.text.text[1]=='\0'
        &&(event.text.text[0]=='i'||event.text.text[0]=='e')) {
        const char key=event.text.text[0];
        const bool inventory_command=std::any_of(ctx.pad_commands.begin(),ctx.pad_commands.end(),[&](const auto &command){return command.command==key;});
        if(inventory_command) {
            //! メニューを 1 度も開いていないとコマンドのタブが空なので、先に組む（窓の分類タブを出すため）。
            ctx.feature_menu.set_pad_commands(ctx.pad_commands);
            auto categories=ctx.feature_menu.command_tab_labels();
            int inventory_index=0;
            for(size_t i=0;i<categories.size();++i)if(categories[i]=="Inventory"||categories[i]==reinterpret_cast<const char*>(u8"持ち物一覧"))inventory_index=int(i);
            ctx.hud_state.modern.open_inventory(ctx.batch.input,std::move(categories),inventory_index);
            return;
        }
    }
    if(ctx.settings.modern_ui && nothing_to_confirm(ctx.frame)
        && event.text.text[0] && event.text.text[1]=='\0'
        && ctx.frame.command_menu_keys.find(event.text.text[0])!=std::string::npos) {
        ctx.perform_action(kActionCommandMenu);return;
    }

    /*
     * **日本語などの非 ASCII は `text` で送る**。
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
    if ((non_ascii || ctx.frame.player_name_input_active) && ctx.frame.text_input_active) {
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
         * **数字キーの移動も視点回転ぶん回す**。
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
        /*
         * **数字を回すのは標準のキー配置のときだけ**（2026-10-07）。ローグライクのキー配置では
         * 数字が「回数の指定」になるコアがある（Umoria・Rogue 3.6/5.4・RogueClone2）。回すと
         * `12` 回のつもりが `14` 回になる。ローグライクで回すのは方向の文字（下の `turn_rogue_letter`）。
         */
        if ((ctx.pad_keymap != "rogue") && ctx.turn_drives_movement() && digit_to_screen_delta(chr, digit_dx, digit_dy)) {
            rotate_screen_delta(ctx.camera_turn, digit_dx, digit_dy);
            const char turned = screen_delta_to_digit(digit_dx, digit_dy);
            if (turned != '\0') {
                chr = turned;
            }
        }
        turn_rogue_letter(ctx, chr);
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
     * （F10 = 機能メニュー・Alt+Enter = 画面モード）ので、遊ぶ人の記憶がそのまま効く。
     * 割り当ては機能メニューの「操作の割り当て」で変えられる。
     * ここで `return` する＝**コアへは流さない**（誤コマンド禁止）。
     */
    const SDL_Keycode sym = event.key.keysym.sym;
    const int key_mods = mods_from_sdl(event.key.keysym.mod);

    /*
     * ボタン配置の編集画面。**すべてのキーを食う**
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
             * （の 2・罠 Q-13）。ここと
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
        if((event.key.keysym.mod&KMOD_CTRL)&&sym>=SDLK_a&&sym<=SDLK_z) {
            if(!event.key.repeat)ctx.feature_menu.handle_native_key(static_cast<int>(sym)&31,ctx.settings);return;
        }
        if((sym==SDLK_ESCAPE||sym==SDLK_RETURN||sym==SDLK_KP_ENTER||sym==SDLK_BACKSPACE)
            &&ctx.feature_menu.handle_native_key(sym==SDLK_ESCAPE?27:(sym==SDLK_BACKSPACE?8:13),ctx.settings))return;
        if(sym==SDLK_TAB && event.key.repeat==0) {ctx.feature_menu.handle(MenuNav::TogglePane,ctx.settings);return;}
        if(ctx.feature_menu.tab_navigation_active()) {
            if(event.key.repeat && (sym==SDLK_RETURN || sym==SDLK_KP_ENTER))return;
            if(sym==SDLK_LSHIFT && event.key.repeat==0) {ctx.feature_menu.handle(MenuNav::PreviousTab,ctx.settings);return;}
            if(sym==SDLK_RSHIFT && event.key.repeat==0) {ctx.feature_menu.handle(MenuNav::NextTab,ctx.settings);return;}
            if(sym==SDLK_F2) {ctx.feature_menu.handle(MenuNav::Categories,ctx.settings);return;}
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
     * （決めたこと 2026-08-11「WASDQE 以外は FPS モードでも使えるように」）。
     *
     * **方向キーとテンキーは一人称でも見下ろしの絶対方位のまま**（↑＝北固定）。
     * これは手つかずではなく**仕様**である——確認のうえ遊ぶ人が決めた
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
    if(ctx.hud_state.targeting.active()&&!ctx.feature_menu.is_open()) {
        if(sym==SDLK_RETURN||sym==SDLK_KP_ENTER||sym==SDLK_SPACE) {
            if(!event.key.repeat)ctx.hud_state.targeting.confirm(ctx.batch.input);
        } else if(sym==SDLK_ESCAPE) {
            if(!event.key.repeat)ctx.hud_state.targeting.cancel(ctx.batch.input);
        } else if(sym==SDLK_F4) {
            if(!event.key.repeat)ctx.hud_state.targeting.toggle_extras(); //!< 追加操作の一覧（AH-54）
        } else if(ctx.hud_state.targeting.extras_open()&&(sym==SDLK_UP||sym==SDLK_DOWN||sym==SDLK_KP_8||sym==SDLK_KP_2||sym==SDLK_8||sym==SDLK_2)) {
            //! 一覧が開いている間の上下は一覧のカーソル（カメラの向きへの読み替えをしない）。
            ctx.hud_state.targeting.step_extras((sym==SDLK_UP||sym==SDLK_KP_8||sym==SDLK_8)?-1:1);
        } else if(sym==SDLK_TAB||sym==SDLK_PAGEUP||sym==SDLK_PAGEDOWN) {
            ctx.hud_state.targeting.cycle(sym==SDLK_PAGEUP||(event.key.keysym.mod&KMOD_SHIFT)?-1:1,ctx.batch.input);
        } else if((event.key.keysym.mod&KMOD_CTRL)&&sym>=SDLK_a&&sym<=SDLK_z) {
            //! Ctrl 付きの追加操作（Angband のローグライクのキー配置では「無視する」が ^D）。
            //! Ctrl 併用は `SDL_TEXTINPUT` を出さないので、ここでしか拾えない（2026-10-07）。
            if(!event.key.repeat)ctx.hud_state.targeting.extra_key(static_cast<int>(sym)&31,ctx.batch.input);
        } else if(sym==SDLK_i) {
            // 「詳細」（設計文書 section 8.5.1）。モード "look" 専用 --
            // TargetingUi::recall() がそれ以外のモードでは無視する。
            if(!event.key.repeat)ctx.hud_state.targeting.recall(ctx.batch.input);
        } else {
            int direction=0;
            switch(sym) {
            case SDLK_LEFT:case SDLK_KP_4:case SDLK_4:direction=4;break;
            case SDLK_RIGHT:case SDLK_KP_6:case SDLK_6:direction=6;break;
            case SDLK_UP:case SDLK_KP_8:case SDLK_8:direction=8;break;
            case SDLK_DOWN:case SDLK_KP_2:case SDLK_2:direction=2;break;
            case SDLK_KP_1:case SDLK_1:direction=1;break;
            case SDLK_KP_3:case SDLK_3:direction=3;break;
            case SDLK_KP_7:case SDLK_7:direction=7;break;
            case SDLK_KP_9:case SDLK_9:direction=9;break;
            }
            if(direction) {
                presentation::InputEventWire move;move.e="move";
                digit_to_screen_delta(char('0'+direction),move.dx,move.dy);
                ctx.turn_screen_move(move);
                ctx.hud_state.targeting.move(5+move.dx-3*move.dy,ctx.batch.input);
            }
        }
        return;
    }
    if (fps_owns_move_keys && (sym == SDLK_RSHIFT)) {
        //! 一人称の間の「メニュー」。開いた先の操作はカーソル＋Enter＋ESC のまま。
        //! ここも開閉なので**リピートでは動かさない**（§15 の 2・罠 Q-13）。
        if ((event.key.repeat == 0) && !ctx.batch.menu_toggled_this_pump) {
            ctx.batch.menu_toggled_this_pump = true;
            ctx.feature_menu.set_sub_panel_kinds(ctx.sub_panel_kind_choices);
            ctx.feature_menu.set_pad_state(ctx.pad_commands, ctx.pad.connected() || ctx.settings.vpad.show,
                ctx.pad.connected() ? ctx.pad.name() : std::string("バーチャルパッド"));
            ctx.feature_menu.open_features(ctx.settings);
            ctx.pad.forget_hold();
        }
        return;
    }
    if (fps_owns_move_keys && !ctx.frame.menu_open && !ctx.hud_state.modern.active()
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
             * （これを見落として Q が「飲む」を開いていた。気づいたこと 2026-08-11）。
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
        if (action != kActionNone && !ctx.frame.menu_open && !ctx.frame.numeric.active && ctx.frame.prompt.choices.empty() && !ctx.hud_state.modern.active()) {
            /*
             * **機能メニューの開閉だけはキーのリピートで動かさない**
             * （の 2・罠 Q-13。閉じる側は上）。
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
             * （一人称の WASDQE で実際に踏んだ穴。気づいたこと 2026-08-11）。
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
     * 動かなくなった。作成画面の [Y/n] も
     * ロール確認も**この層が持つ**（K-24 がそのために作った）。
     * 本当の不具合はカーソルの初期位置だった（`ui_cursor.cpp` の `sync`）。
     */
    if(ctx.hud_state.modern.captures_keys()) {
        presentation::InputEventWire wire;
        if(!event.key.repeat&&(translate_captured_special_key(event.key,wire)||translate_key_down(event.key,wire)))
            ctx.batch.input.events.push_back(std::move(wire));
        return;
    }
    if(ctx.hud_state.modern.active()) {
        if(event.key.repeat && (sym==SDLK_RETURN || sym==SDLK_KP_ENTER || sym==SDLK_ESCAPE))return;
        if((event.key.keysym.mod&KMOD_CTRL)&&sym>=SDLK_a&&sym<=SDLK_z) {
            if(ctx.hud_state.modern.text_mode()||ctx.frame.numeric.active)ctx.hud_state.modern.edit_text_key(static_cast<int>(sym)&31);
            else if(!event.key.repeat)ctx.hud_state.modern.handle_native_key(static_cast<int>(sym)&31,ctx.batch.input);
            return;
        }
        if(sym==SDLK_ESCAPE)ctx.hud_state.modern.cancel(ctx.batch.input);
        else if(sym==SDLK_BACKSPACE)ctx.hud_state.modern.backspace();
        else if(sym==SDLK_DELETE)ctx.hud_state.modern.edit_text_key(127);
        else if(sym==SDLK_HOME)ctx.hud_state.modern.edit_text_boundary(false);
        else if(sym==SDLK_END)ctx.hud_state.modern.edit_text_boundary(true);
        else if((sym==SDLK_LSHIFT||sym==SDLK_RSHIFT)&&!event.key.repeat){if(ctx.hud_state.modern.item_window()||ctx.hud_state.modern.parent_tab_navigation_active())ctx.hud_state.modern.tab(sym==SDLK_LSHIFT?-1:1);}
        else if(sym==SDLK_TAB){if(ctx.hud_state.modern.text_mode())ctx.hud_state.modern.edit_text_key(9);else if(!event.key.repeat)ctx.hud_state.modern.toggle_details();}
        else if(sym==SDLK_F3){if(ctx.hud_state.modern.document_mode())ctx.hud_state.modern.search_document((event.key.keysym.mod&KMOD_SHIFT)!=0,ctx.batch.input);else ctx.hud_state.modern.show_help(ctx.batch.input);}
        else if(sym==SDLK_F4)ctx.hud_state.modern.toggle_commands();
        else if(sym==SDLK_PAGEUP)ctx.hud_state.modern.page(-1,ctx.batch.input);
        else if(sym==SDLK_PAGEDOWN)ctx.hud_state.modern.page(1,ctx.batch.input);
        else if((event.key.keysym.mod&KMOD_SHIFT)&&(sym==SDLK_LEFT||sym==SDLK_RIGHT)&&!ctx.hud_state.modern.text_mode()&&!ctx.frame.numeric.active)ctx.hud_state.modern.scroll_horizontal(sym==SDLK_LEFT?-1:1);
        else ctx.hud_state.modern.handle(cursor_nav_from_key(event.key),ctx.batch.input);
        return;
    }
    /*
     * 「設定」タブへ移った直後は、機能メニューが閉じて原作の設定画面がまだ届いていない。
     * この間の Shift は受ける所が無く捨てられていた（2026-09-23 気づいたこと「右 Shift が
     * 右タブに遷移できないことがある」）。設定の画面が届いたら回すよう預ける。
     */
    if((sym==SDLK_LSHIFT||sym==SDLK_RSHIFT)&&!event.key.repeat&&ctx.hud_state.modern.awaiting_parent_child()){ctx.hud_state.modern.tab(sym==SDLK_LSHIFT?-1:1);return;}
    if(ctx.settings.modern_ui&&!ctx.frame.menu_term_lines.empty()&&!ctx.frame.text_input_active
        && (sym==SDLK_PAGEUP||sym==SDLK_PAGEDOWN)) {
        ctx.hud_state.native_scroll_row+=sym==SDLK_PAGEUP?-8:8;return;
    }
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
     * **Enter も A と同じ、決定の専任**（すぐ下がパッドの
     * 枝と同じ理屈）。以前はここでコマンドメニューを開いていたが、いまは
     * F1 とパッド Start だけが開く——**確認する相手が無いとき、Enter は何もしない**
     * （コアへも送らない。変愚のように `\r` を受けて自分の通常メニューを開くコアへ
     * `\r` を渡すと、二重の入口になってしまう）。旗を立てないコアでは
     * `nothing_to_confirm()` が常に偽なので、今までどおり `\r` がそのままコアへ届く
     * ——**あちらの動きは 1 ビットも変わらない**。
     */
    if (((sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER))
        && ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_GUI)) == 0)
        && nothing_to_confirm(ctx.frame)) {
        return;
    }
    presentation::InputEventWire wire;
    if (translate_key_down(event.key, wire)) {
        //! 矢印・テンキーは**画面基準**。視点回転ぶんを当ててから積む（設計書 罠 1）。
        ctx.turn_screen_move(wire);
        //! ローグライクのキー配置の Ctrl＋方向の文字（その方向へ働きかける・掘る）も同じく回す。
        if ((wire.e == "key") && wire.ctrl && (wire.chr.size() == 1)) {
            turn_rogue_letter(ctx, wire.chr[0]);
        }
        /*
         * Shift＋矢印（テンキー）は「走る＋その方向」（修-02。原作の Shift＋方向と同じ）。
         * プロトコルは変えず、パッドの「走る」と同じ道（解決済みのキー列）で走る命令を押す。
         * キー配置ごとの走るキー（`.` / `,` など）はコアが届ける `pad_commands` の逆引きに任せる。
         * 方向は、普通のキーで読むコア（Umoria・Rogue 系）には続けて `move` で送り、ターゲット画面で
         * 受けるコアには、画面が開いてから `aim_move`・`aim_confirm` で答える（`advance_pending_run`）。
         * **コアがコマンドを待っているときだけ**——方向を訊かれている最中や持ち物の選択では、今までどおり
         * `move` 1 つのまま（走る命令を差し込むと `.`＝「目標」などに化ける）。Ctrl・Alt 付きは対象外。
         * メニューのタブ移動の Shift と窓の横スクロールの Shift＋左右は、ここより前で食われるので届かない。
         */
        const bool shift_move = (wire.e == "move") && ((wire.dx != 0) || (wire.dy != 0))
            && ((event.key.keysym.mod & KMOD_SHIFT) != 0) && ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) == 0);
        if (shift_move && (event.key.repeat != 0) && (ctx.state.pending_run.last_run_sym == static_cast<std::int32_t>(sym))) {
            return; // 押しっぱなしの連打は捨てる（走っている最中に次のキーが届くと、原作は走りを止める）
        }
        if (event.key.repeat == 0) {
            ctx.state.pending_run.last_run_sym = 0;
        }
        if (shift_move && ctx.frame.awaiting_command && !ctx.state.pending_run.active
            && emit_run_command(ctx.core_name, ctx.pad_commands, ctx.batch.input)) {
            ctx.state.pending_run.last_run_sym = static_cast<std::int32_t>(sym);
            if (run_direction_via_targeting_ui(ctx.core_name)) {
                ctx.state.pending_run.active = true;
                ctx.state.pending_run.dx = wire.dx;
                ctx.state.pending_run.dy = wire.dy;
                ctx.state.pending_run.stage = 0;
                ctx.state.pending_run.since_ms = SDL_GetTicks();
            } else {
                ctx.batch.input.events.push_back(wire);
            }
            return;
        }
        ctx.batch.input.events.push_back(wire);
    }
    return;
}

/*!
 * @brief Shift＋矢印の「走る」の後半（修-02）。ターゲット画面で方向を受けるコア用。
 * @details 走る命令を押すと、コアは方向を訊くターゲット画面（`direction8`）を開く。この画面は生の
 * `move` を読まず、`aim_move`（向きを決める）→ `aim_confirm`（確定）で答える。どちらもコアが届けた
 * 画面の番号（revision・request_id）を付けて送るので、走る命令と同じ束には積めない。そこで、画面が
 * 開くのを毎フレーム見て、開いたら向きを送り、コアが受けて確定できるようになったら確定する。
 * 画面が一定の時間（`kWaitMs`）開かないときは、普通のキーで方向を読むコアとみなして `move` を送る。
 */
void advance_pending_run(InputPumpContext &ctx)
{
    auto &run = ctx.state.pending_run;
    if (!run.active) {
        return;
    }
    constexpr std::uint32_t kWaitMs = 600;
    const std::uint32_t now = SDL_GetTicks();
    auto &aim = ctx.hud_state.targeting;
    if (aim.active() && (aim.state().mode == "direction8")) {
        if (run.stage == 0) {
            if (!aim.pending()) {
                aim.move(5 + run.dx - 3 * run.dy, ctx.batch.input); // 向きの番号（テンキーの配置）
                run.stage = 1;
                run.since_ms = now;
            }
            return;
        }
        if (!aim.pending() && aim.state().can_confirm) {
            aim.confirm(ctx.batch.input);
            run.active = false;
        } else if (now - run.since_ms > 2 * kWaitMs) {
            run.active = false; // 確定できないまま止まった（向きの先が壁など）。ターゲット画面はそのまま残す
        }
        return;
    }
    if (run.stage == 1) {
        run.active = false; // 画面が閉じた（コアが先に受けた）
        return;
    }
    if (now - run.since_ms > kWaitMs) {
        presentation::InputEventWire move;
        move.e = "move";
        move.dx = run.dx;
        move.dy = run.dy;
        ctx.batch.input.events.push_back(move);
        run.active = false;
    }
}

} // namespace

/*!
 * @brief SDL の出来事を全部捌く（(a) 入力）。仕様は `run_parts.h`。
 * @details 本体は `run()` に在ったときの字面のまま。外の名前に `ctx.` が付いただけで、
 * 到着順に処理する。UI が消費した入力は同じイベントからコアへ再送しない。
 */
void pump_sdl_events(InputPumpContext &ctx)
{
    test_keyboard().poll(ctx.floor_cutin.blocks_input() ? "floor-cutin" : "game");
    advance_pending_run(ctx);
    write_click_snapshot(ctx);
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
            ctx.state.menu_touch_active=false;
            ctx.state.map_click.gesture.reset(); //!< 押したまま焦点が移ると、離す出来事は届かない
            ctx.state.map_click.gate.reset();
            continue;
        }
        /*
         * バーチャルパッド。**マウスの分岐より先**に見る
         * ——ボタンに落ちた指（とそのマウス写し）はここが食い、タップ移動や
         * 一人称の視界に化けさせない。ボタンの外はいっさい触らず従来の道へ落とす。
         * 配置の編集画面が開いている間はポインタの出来事を全部ここが食う。
         */
        if (ctx.vpad.on_event(event, ctx.settings.vpad, ctx.usable_area(), ctx.screen_w, ctx.screen_h)) {
            continue;
        }
        /*
         * **バーチャルパッドを出している間は、パッドの部品への操作だけを受け付ける**
         * （決めたこと 2026-10-14「バーチャルパッドが出てる場合はタッチ操作は出来なくてよい」
         * 「正確にはバーチャルパッドの操作のみを受け付けるようにして」。修-12）。
         * 上の `on_event()` が食わなかった指・マウスのボタン・ホイールは、パッドの外への操作なので
         * ここで捨てる（地図のタップ・窓のタブ・メニューの行・一人称の視界などへ流さない）。
         * キーボードと実物のゲームパッドはそのまま。パッドを出していないとき（実物のゲームパッドだけ
         * 使っているときを含む）は何も変わらず、マウスと共存する。
         * マウスの移動も捨てる（一人称では押さずに動かすだけで視界が回る。ホバーの強調も出さない）。
         * パッドを出す・隠す切り替えはメニューの項目で、パッドの A・十字とキーボードで操作できる
         * ので、捨てても戻れなくなることはない。
         */
        if (pointer_blocked_by_vpad(ctx, event)) {
#if defined(__ANDROID__)
            /*
             * 例外：ソフトキーボードを閉じたあとの「タップで出し直す」道は残す。
             * ここで捨てると、戻るキーでキーボードを閉じた後に文字入力へ戻れなくなる。
             * ゲームの操作には何もならない（キーボードが出るだけ）。
             */
            if ((event.type == SDL_FINGERDOWN) && ctx.state.ime_prompt_open && ctx.state.ime_dismissed) {
                SDL_StartTextInput();
                ctx.state.ime_dismissed = false;
            }
#endif
            continue;
        }
        if(ctx.hud_state.targeting.active()&&!ctx.feature_menu.is_open()
            &&(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP)) {
            if(event.type==SDL_FINGERDOWN)select_target_at(ctx,int(event.tfinger.x*ctx.screen_w),int(event.tfinger.y*ctx.screen_h));
            continue;
        }
#if defined(__ANDROID__)
        /*
         * **タップしたらソフトキーボードを出し直す**。
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
        if((ctx.hud_state.modern.active()||(ctx.settings.modern_ui&&ctx.feature_menu.is_open()&&!ctx.feature_menu.showing_bindings()))
            &&(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP)) {
            const int x=int(event.tfinger.x*ctx.screen_w),y=int(event.tfinger.y*ctx.screen_h);
            auto &s=ctx.state;
            if(event.type==SDL_FINGERDOWN&&!s.menu_touch_active) {
                s.menu_finger=event.tfinger.fingerId;s.menu_touch_active=true;s.menu_touch_scrolled=false;
                s.menu_touch_x=s.menu_touch_last_x=x;s.menu_touch_y=s.menu_touch_last_y=y;s.menu_touch_axis=0;
            } else if(s.menu_touch_active&&s.menu_finger==event.tfinger.fingerId) {
                const int step=std::max(12,int(24*menu_font_scale(ctx.settings.modern_font_level)));
                const int dx=x-s.menu_touch_last_x,dy=y-s.menu_touch_last_y;
                if(event.type==SDL_FINGERMOTION&&std::max(std::abs(dx),std::abs(dy))>=step) {
                    if(!s.menu_touch_axis)s.menu_touch_axis=std::abs(dx)>std::abs(dy)?2:1;
                    const int delta=s.menu_touch_axis==2?dx:dy;
                    if(std::abs(delta)>=step) {
                        if(ctx.feature_menu.is_open()){if(s.menu_touch_axis==1)ctx.feature_menu.scroll_at(s.menu_touch_x,s.menu_touch_y,delta<0?1:-1,ctx.settings);}
                        else ctx.hud_state.modern.scroll_at(ctx.layout,s.menu_touch_x,s.menu_touch_y,delta<0?1:-1,ctx.batch.input,s.menu_touch_axis==2);
                        s.menu_touch_last_x=x;s.menu_touch_last_y=y;s.menu_touch_scrolled=true;
                    }
                } else if(event.type==SDL_FINGERUP) {
                    if(!s.menu_touch_scrolled&&std::abs(x-s.menu_touch_x)<step&&std::abs(y-s.menu_touch_y)<step) {
                        if(ctx.feature_menu.is_open())ctx.feature_menu.click_row(x,y,ctx.settings);
                        else ctx.hud_state.modern.click(ctx.layout,x,y,ctx.batch.input);
                    }
                    s.menu_touch_active=false;
                }
            }
            continue;
        }
        if (ctx.pad.on_event(event)) {
            continue; // パッドの出来事はパッドが食う
        }
        /*
         * 一人称（おまけ）が**この 6 つのキーだけ**を持っているか。
         *
         * 決めたことは 2 段階で動いた。**いまは後者が正**:
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
         * 一人称のマウス:
         *   左クリック          決定
         *   右クリック          取消
         *   右ドラッグ          視点の自由移動（離した所で最寄りの 8 方位へ吸着）
         *   ホイール            画角 90〜120 度
         *
         * **キーと違って選択肢が出ていても横取りする。**キーの方は「店で品物の a・s が
         * 選べなくなる」ので譲っているが、マウスにはその衝突が無い。むしろ選択肢や
         * はい／いいえこそ決定・取消を押したい場面なので、ここで譲ると使えない。
         */
        const bool fps_owns_mouse = ctx.first_person.active && !ctx.feature_menu.is_open() && !ctx.hud_state.modern.active() && !ctx.hud_state.targeting.active();
        /*
         * **視界を回すのは地図が見えているときだけ。**メニューが覆っている間も回すと、
         * 店から出た瞬間に景色が明後日を向いている。ボタン（決定・取消）は覆われていても
         * 効かせたいので、条件を分けてある。
         */
        const bool fps_owns_look = fps_owns_mouse && frame_shows_map(ctx.frame);
        /*
         * 地図のタップ・長押し（AH-58）。ここまで来る指は、パッドのボタン・目標選び・新 UI の窓・
         * ゲームパッドのどれも取らなかったもの。**申告したコアだけ**（`map_click_enabled`）で、
         * 申告しないコアでは指の出来事は今までどおりここで何にもならない。
         * 一人称では指は視界の操作に使うので、地図のタップにしない（`pump_map_click_finger()` が見る）。
         */
        if (ctx.map_click_enabled
            && ((event.type == SDL_FINGERDOWN) || (event.type == SDL_FINGERMOTION) || (event.type == SDL_FINGERUP))) {
            pump_map_click_finger(ctx, event);
            continue;
        }
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
        if(event.type==SDL_MOUSEBUTTONDOWN&&ctx.feature_menu.is_open()) {
            if(event.button.which==SDL_TOUCH_MOUSEID)continue;
            if(event.button.button==SDL_BUTTON_LEFT)ctx.feature_menu.click_row(event.button.x,event.button.y,ctx.settings);
            else if(event.button.button==SDL_BUTTON_RIGHT)ctx.feature_menu.handle(MenuNav::Cancel,ctx.settings);
            continue;
        }
        if(event.type==SDL_MOUSEWHEEL&&ctx.feature_menu.is_open()) {
            int x=0,y=0;wheel_pointer(x,y);
            if(event.wheel.y)ctx.feature_menu.scroll_at(x,y,event.wheel.y>0?-1:1,ctx.settings);
            continue;
        }
        if(ctx.hud_state.targeting.active()&&!ctx.feature_menu.is_open()) {
            if(event.type==SDL_MOUSEBUTTONDOWN) {
                ctx.click_path.cancel();
                if(event.button.button==SDL_BUTTON_RIGHT)ctx.hud_state.targeting.cancel(ctx.batch.input);
                else if(event.button.button==SDL_BUTTON_LEFT&&event.button.which!=SDL_TOUCH_MOUSEID)
                    select_target_at(ctx,event.button.x,event.button.y);
                continue;
            }
            if(event.type==SDL_MOUSEWHEEL)continue;
        }
        if(ctx.hud_state.modern.active()&&!ctx.feature_menu.is_open()) {
            if(event.type==SDL_MOUSEWHEEL){int x=0,y=0;wheel_pointer(x,y);if(event.wheel.y)ctx.hud_state.modern.scroll_at(ctx.layout,x,y,event.wheel.y>0?-1:1,ctx.batch.input,(SDL_GetModState()&KMOD_SHIFT)!=0);continue;}
            if(event.type==SDL_MOUSEBUTTONDOWN){
                if(event.button.which==SDL_TOUCH_MOUSEID)continue;
                if(event.button.button==SDL_BUTTON_LEFT)ctx.hud_state.modern.click(ctx.layout,event.button.x,event.button.y,ctx.batch.input);
                else if(event.button.button==SDL_BUTTON_RIGHT)ctx.hud_state.modern.cancel(ctx.batch.input);
                continue;
            }
        }
        if(event.type==SDL_MOUSEWHEEL&&ctx.settings.modern_ui&&!ctx.feature_menu.is_open()
            &&!ctx.hud_state.modern.active()&&!ctx.frame.menu_term_lines.empty()) {
            if(SDL_GetModState()&KMOD_SHIFT)ctx.hud_state.native_scroll_col-=event.wheel.y*4;
            else ctx.hud_state.native_scroll_row-=event.wheel.y*3;
            continue;
        }
        if (event.type == SDL_MOUSEWHEEL) {
            const SDL_Keymod mod = SDL_GetModState();
            const auto step = static_cast<float>(event.wheel.y);
            if (fps_owns_mouse) {
                /*
                 * 一人称のホイールは**画角**（決めたこと 2026-08-11・90〜120 度）。
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
         * 一人称のマウス（決めたこと 2026-08-11・**改訂**）。
         * 「右クリックをマウススライドではなく、マウススライド単体で視界移動に」
         * ＝ **ボタンを押さずに動かすだけで視界が動く。**吸着も無い。
         * したがって右ボタンは**取消だけ**を意味する。
         */
        if (ctx.map_click_enabled && (event.type == SDL_MOUSEMOTION) && (event.motion.which != SDL_TOUCH_MOUSEID)) {
            //! 押したまま大きく動いたらドラッグ（クリックにしない）。押していなければ何も起きない。
            ctx.state.map_click.gesture.move(MapClickGesture::Source::Mouse, SDL_BUTTON_LEFT, event.motion.x, event.motion.y);
            ctx.state.map_click.gesture.move(MapClickGesture::Source::Mouse, SDL_BUTTON_RIGHT, event.motion.x, event.motion.y);
        }
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
             * ホイールの押し込み ＝ **視界を水平に戻す**。
             * これだけは割り当ての表に無い（マウスのボタンは表に並べていない）ので、
             * ここで直に見ている。同じ操作はコントローラー R3 とキーの割り当てからも出せる。
             */
            ctx.first_person.level_view();
            continue;
        }
        if (ctx.map_click_enabled && (event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_RIGHT)) {
            //! ここへ来る右ボタンは、メニュー・目標選び・一人称が取らなかったもの（上でそれぞれ `continue` 済み）。
            if ((event.button.which != SDL_TOUCH_MOUSEID) && ctx.layout.scene.contains(event.button.x, event.button.y)) {
                begin_mouse_map_click(ctx, event.button.button, event.button.x, event.button.y);
            }
            continue;
        }
        if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_RIGHT)) {
            if (ctx.map_click_enabled && (event.button.which != SDL_TOUCH_MOUSEID)) {
                end_mouse_map_click(ctx, event.button.button, event.button.x, event.button.y);
            }
            continue; //!< 右ボタンは押した所で効かせた（申告したコアは離した所で送る）。離しは捨てる
        }
        if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_LEFT)) {
            //! 掴みを離す。**cfg へ書くのはここから先**（動かすたびにディスクを叩かない）。
            ctx.hud_state.grabbed_grip = -1;
            if (ctx.map_click_enabled && (event.button.which != SDL_TOUCH_MOUSEID)) {
                end_mouse_map_click(ctx, event.button.button, event.button.x, event.button.y);
            }
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
             * 気づいたこと「セーブデータ選択が文字指定でしか選択できない」）。
             * 当たりは描画と同じ計算（`menu_choice_at`）なので見えている枠と
             * ずれない。送るのはカーソル層の決定と同じ「その選択肢の 1 キー」
             * ——幻想蛮怒のセーブ選択だけでなく、選択肢が立つ画面すべて
             * （変愚の店・建物の一覧）で効く。
             */
            if (!frame_shows_map(ctx.frame)) {
                const int hit = menu_choice_at(ctx.text, ctx.layout, ctx.frame, event.button.x, event.button.y,ctx.settings.window_theme(),&ctx.hud_state);
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
             * 一人称の左クリックは**決定**。
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
            if (ctx.map_click_enabled) {
                /*
                 * 地図のクリック（AH-58）。**歩くかどうかはコアが原作の規則で決める**ので、ここでは
                 * 経路を引かない。押した所のマスと修飾を覚え、動かさずに離したときに送る。
                 * 指のマウス写し（`SDL_TOUCH_MOUSEID`）は捨てる——指は `SDL_FINGER*` から
                 * 別に組む（同じタップを 2 回送らないため）。
                 */
                if (event.button.which != SDL_TOUCH_MOUSEID) {
                    begin_mouse_map_click(ctx, event.button.button, event.button.x, event.button.y);
                }
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
    if (ctx.map_click_enabled) {
        //! 指を動かさずに押し続けて時間が来たら、離すのを待たずに右クリックを送る（長押し）。
        MapClick click;
        if (ctx.state.map_click.gesture.poll(clock::ticks(), click)) {
            dispatch_map_click(ctx, click, "touch (long press)");
        }
    }
}

} // namespace hd2d
