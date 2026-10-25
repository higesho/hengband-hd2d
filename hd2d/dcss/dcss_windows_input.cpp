/*!
 * @file dcss_windows_input.cpp
 * @brief DCSS の窓の部品の入力（キー・マウス）。ウェブタイル版のブラウザ側（`menu.js`・`ui-layouts.js`）が受け持っていた分を再現する。
 *
 * ブラウザ側は、メニューの強調の移動・頁送り・捲りを自分で行い、サーバ（コア）へは `menu_hover`・`menu_scroll`・
 * `formatted_scroller_scroll` で知らせ、そのほかのキーはそのまま `key` で送る。ここも同じにする。
 */
#include "dcss/dcss_windows.h"

#include "dcss/dcss_windows_internal.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <climits>

namespace hd2d::dcss {

namespace {

using json = nlohmann::json;

//! 原作 `cio.h` の特殊キーの符号（`CK_*`）。
constexpr int kCkDelete = -255;
constexpr int kCkUp = -254;
constexpr int kCkDown = -253;
constexpr int kCkLeft = -252;
constexpr int kCkRight = -251;
constexpr int kCkInsert = -250;
constexpr int kCkHome = -249;
constexpr int kCkEnd = -248;
constexpr int kCkPgUp = -246;
constexpr int kCkPgDn = -245;
constexpr int kCkShiftOffset = -243 - (-254); // CK_SHIFT_UP - CK_UP
constexpr int kCkCtrlOffset = -232 - (-254); // CK_CTRL_UP - CK_UP
constexpr int kCkShiftTab = -233;
constexpr int kCkCtrlTab = -222;

bool menu_has_custom_dash(const std::string &tag)
{
    return (tag == "inventory") || (tag == "stash") || (tag == "actions") || (tag == "macros") || (tag == "macro_mapping") || (tag == "use_item");
}

//! 窓を閉じるクリックか: 窓の枠の外の左クリック、または右クリック。
//! （原作: ウェブ版は枠の外のクリックで Esc を送る（`ui.js` の `popup_clickoutside_handler`）。タイル版の右クリックは `CK_MOUSE_CMD` で `CMD_MENU_EXIT`。）
bool popup_close_click(const RectPx &frame, int button, int x, int y)
{
    if (button == SDL_BUTTON_RIGHT) {
        return true;
    }
    return (button == SDL_BUTTON_LEFT) && !frame.empty() && !frame.contains(x, y);
}

//! 画面の押下の位置を窓の座標のまま使う。
struct Pt {
    int x{ 0 };
    int y{ 0 };
};

} // namespace

// ================================================================== 送る行

void DcssWindows::send_line(const std::string &line)
{
    // 画面の側で積んだ窓（ゲームパッドの割り当ての画面）の操作は、コアへ送らず受け手へ渡す。
    if (this->local_handler_) {
        const DcssWindow *top = this->stack_.top();
        if ((top != nullptr) && (top->kind == DcssWindow::Kind::Menu) && (top->menu.tag.rfind("hd2d_pad", 0) == 0)) {
            this->local_handler_(line);
            return;
        }
    }
    this->outgoing_.push_back(line);
}

namespace {

//! 1 行入力の画面側の控え（原作の行編集のうち、字の追加・消去だけを写す）。
void edit_typed(std::string &typed, bool &replace_on_first_key, int keycode)
{
    if ((keycode == 8) || (keycode == 127)) {
        while (!typed.empty() && ((static_cast<unsigned char>(typed.back()) & 0xC0) == 0x80)) {
            typed.pop_back();
        }
        if (!typed.empty()) {
            typed.pop_back();
        }
        replace_on_first_key = false;
    } else if (keycode == 21) {
        typed.clear();
        replace_on_first_key = false;
    } else if (keycode >= 32) {
        if (replace_on_first_key) {
            typed.clear();
            replace_on_first_key = false;
        }
        typed += dcss_encode_utf8(static_cast<char32_t>(keycode));
    }
}

} // namespace

void DcssWindows::send_key(int keycode)
{
    if (keycode == kToggleKeyboardKey) {
        // ゲームメニューに足した「画面上のキーボードの切り替え」。コアへは送らず、画面のキーボードを出し入れしてメニューを閉じる。
        if (this->keyboard_handler_) {
            this->keyboard_handler_();
        }
        this->send_key(27);
        return;
    }
    if (this->stack_.name_prompt() && this->stack_.name_overwrite()) {
        // 同じ名前のセーブの上書きの確認（原作 `_choose_name` の `overwrite_prompt`）。コアは「Y」を待っていて、ほかのキーは確認をやめるだけで食う。
        // 原作の Enter は名前の確定のボタンが先に食い、確認の窓（はい/いいえ）は十字キーでしか出ない。画面は自分の「はい・いいえ」で答えを受け、コアへは Y か n だけを送る。
        // Y・y = はい。N・n・Esc・Enter = いいえ（Enter が既定。取り消しは名前の問いへ戻るだけ）。ほかのキーは無視する。
        if ((keycode == 'Y') || (keycode == 'y')) {
            this->stack_.clear_name_overwrite();
            this->send_line("{\"msg\":\"key\",\"keycode\":89}");
        } else if ((keycode == 'N') || (keycode == 'n') || (keycode == 27) || (keycode == 13)) {
            this->stack_.clear_name_overwrite();
            this->send_line("{\"msg\":\"key\",\"keycode\":110}");
        }
        return;
    }
    this->send_line("{\"msg\":\"key\",\"keycode\":" + std::to_string(keycode) + "}");
    DcssTextInput &in = this->stack_.text_input();
    if (this->stack_.name_prompt() && (keycode == 13) && !this->stack_.name_prompt_random() && !in.typed.empty()) {
        // 名前の確定。同じ名前のセーブが在ればコアは上書きの確認を待つ（画面には届かない）ので、在るかをアダプタへ問い合わせる（返事は `DcssWindowStack::on_name_check`）。
        json q;
        q["msg"] = "hd2d_name_check";
        q["name"] = in.typed;
        this->send_line(q.dump(-1, ' ', false, json::error_handler_t::replace));
    }
    if (this->stack_.name_prompt()) {
        // 名前の問い: 「*」はコアがランダムな名前を入れる（字は画面に来ない）。それ以外は打った字を覚える。
        if (keycode == '*') {
            in.typed.clear();
            this->stack_.set_name_prompt_random(true);
        } else if ((keycode >= 32) || (keycode == 8) || (keycode == 127) || (keycode == 21)) {
            if (this->stack_.name_prompt_random()) {
                in.typed.clear();
                this->stack_.set_name_prompt_random(false);
            }
            edit_typed(in.typed, in.replace_on_first_key, keycode);
        }
    } else if (in.active) {
        edit_typed(in.typed, in.replace_on_first_key, keycode);
    } else if (DcssWindow *top = this->stack_.top(); (top != nullptr) && (top->kind == DcssWindow::Kind::Ui) && (top->type == "msgwin-get-line")) {
        bool replace = false;
        edit_typed(top->typed, replace, keycode);
    }
}

void DcssWindows::send_text(const std::string &text)
{
    json m;
    m["msg"] = "text_input";
    m["text"] = text;
    this->send_line(m.dump(-1, ' ', false, json::error_handler_t::replace));
}

void DcssWindows::send_hover(int index, bool mouse)
{
    this->send_line("{\"msg\":\"menu_hover\",\"hover\":" + std::to_string(index) + ",\"mouse\":" + (mouse ? "true" : "false") + "}");
}

void DcssWindows::send_scroll_if_changed(DcssWindow &w)
{
    if (w.kind == DcssWindow::Kind::Menu) {
        if ((w.first_visible != w.last_sent_first) || (w.last_visible != w.last_sent_hover)) {
            w.last_sent_first = w.first_visible;
            w.last_sent_hover = w.last_visible;
            this->send_line("{\"msg\":\"menu_scroll\",\"first\":" + std::to_string(w.first_visible) + ",\"last\":" + std::to_string(w.last_visible)
                + ",\"hover\":" + std::to_string(w.menu.last_hovered) + "}");
        }
    }
}

// ================================================================== メニューの操作

void DcssWindows::menu_update_visible(DcssWindow &w, const View &v)
{
    const auto &entries = v.menu.entries;
    const int n = static_cast<int>(entries.size());
    const int top = w.scroll_y;
    const int bottom = w.scroll_y + v.viewport.h;
    int first = 0;
    int last = std::max(0, n - 1);
    int i = 0;
    for (; i < n; ++i) {
        if ((entries[static_cast<std::size_t>(i)].y + 1) >= top) {
            first = i;
            break;
        }
    }
    for (; i < n; ++i) {
        const DcssMenuEntryLayout &e = entries[static_cast<std::size_t>(i)];
        if ((e.y + e.height - 1) >= bottom) {
            break;
        }
        last = i;
    }
    w.first_visible = first;
    w.last_visible = last;
}

int DcssWindows::menu_next_hoverable(const DcssWindow &w, bool reverse, int starting_point, bool start_at_starting_point) const
{
    const DcssMenuData &menu = w.menu;
    const int n = static_cast<int>(menu.items.size());
    int items_tried = 0;
    int max_items = 0;
    if ((menu.flags & kMfWrap) != 0) {
        max_items = n;
    } else if (reverse) {
        max_items = menu.last_hovered;
    } else {
        max_items = n - std::max(starting_point, 0);
    }
    if (start_at_starting_point && (n > 0)) {
        max_items = std::max(max_items, 1);
    }
    if (max_items <= 0) {
        return -1;
    }
    int new_hover = starting_point;
    if (reverse && (new_hover < 0)) {
        new_hover = 0;
    }
    bool found = false;
    if (!start_at_starting_point) {
        new_hover += reverse ? -1 : 1;
    }
    while (items_tried < max_items) {
        ++items_tried;
        if ((menu.flags & kMfWrap) != 0) {
            new_hover = (new_hover + n) % n;
        }
        new_hover = std::max(0, std::min(new_hover, n - 1));
        if (dcss_menu_item_selectable(menu, new_hover)) {
            found = true;
            break;
        }
        new_hover += reverse ? -1 : 1;
    }
    return found ? new_hover : -1;
}

void DcssWindows::menu_set_hovered(DcssWindow &w, int index, bool snap, bool from_mouse)
{
    DcssMenuData &menu = w.menu;
    if (index >= static_cast<int>(menu.items.size())) {
        index = std::max(0, static_cast<int>(menu.items.size()) - 1);
    }
    if ((index == menu.last_hovered) && ((index < 0) || dcss_menu_item_selectable(menu, index))) {
        return;
    }
    if ((index < 0) || dcss_menu_item_selectable(menu, index)) {
        menu.last_hovered = index;
        if ((menu.last_hovered >= 0) && snap) {
            this->menu_snap_in_page(w, menu.last_hovered);
        }
        this->send_hover(menu.last_hovered, from_mouse);
    }
}

void DcssWindows::menu_cycle_hover(DcssWindow &w, bool reverse)
{
    const int next = this->menu_next_hoverable(w, reverse, w.menu.last_hovered, false);
    if (next != -1) {
        this->menu_set_hovered(w, next, true, false);
    }
}

void DcssWindows::menu_scroll_to_item(DcssWindow &w, int index)
{
    View *v = this->view_of(w);
    if ((v == nullptr) || w.menu.items.empty() || v->menu.entries.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(w.menu.items.size()) - 1);
    if ((index > 0) && (w.menu.items[static_cast<std::size_t>(index - 1)].level < 2)) {
        --index; // 直前の見出しも見えるように
    }
    if (index == w.first_visible) {
        return;
    }
    const int max_scroll = std::max(0, v->menu_total - v->viewport.h);
    w.scroll_y = std::clamp(v->menu.entries[static_cast<std::size_t>(index)].y, 0, max_scroll);
    this->menu_update_visible(w, *v);
}

void DcssWindows::menu_scroll_bottom_to_item(DcssWindow &w, int index)
{
    View *v = this->view_of(w);
    if ((v == nullptr) || w.menu.items.empty() || v->menu.entries.empty() || (w.last_visible == index)) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(w.menu.items.size()) - 1);
    const DcssMenuEntryLayout &e = v->menu.entries[static_cast<std::size_t>(index)];
    const int max_scroll = std::max(0, v->menu_total - v->viewport.h);
    w.scroll_y = std::clamp(e.y + e.height - v->viewport.h, 0, max_scroll);
    this->menu_update_visible(w, *v);
}

void DcssWindows::menu_snap_in_page(DcssWindow &w, int index)
{
    if ((index < 0) || (index >= static_cast<int>(w.menu.items.size()))) {
        return;
    }
    if (index <= w.first_visible) {
        this->menu_scroll_to_item(w, index);
    } else if (index >= w.last_visible) {
        this->menu_scroll_bottom_to_item(w, index);
    }
}

void DcssWindows::menu_line_step(DcssWindow &w, int dir)
{
    const int n = static_cast<int>(w.menu.items.size());
    if (n <= 0) {
        return;
    }
    if (dir > 0) {
        int next = w.first_visible + 1;
        if (w.menu.items[static_cast<std::size_t>(w.first_visible)].level < 2) {
            ++next;
        }
        if (next >= n) {
            next = n - 1;
        }
        this->menu_scroll_to_item(w, next);
    } else {
        this->menu_scroll_to_item(w, std::max(0, w.first_visible - 1));
    }
    // 強調が見えている範囲から外れたら寄せる。
    if (w.menu.last_hovered >= 0) {
        if (w.menu.last_hovered < w.first_visible) {
            this->menu_set_hovered(w, w.first_visible, false, false);
        } else if (w.menu.last_hovered > w.last_visible) {
            this->menu_set_hovered(w, w.last_visible, false, false);
        }
    }
}

void DcssWindows::menu_paging(DcssWindow &w, bool up)
{
    DcssMenuData &menu = w.menu;
    const int n = static_cast<int>(menu.items.size());
    if (n == 0) {
        return;
    }
    if (((menu.flags & kMfArrowsSelect) != 0) && (menu.last_hovered < 0)) {
        menu.last_hovered = w.first_visible;
    }
    const bool adjust_hover = (menu.last_hovered >= 0);
    // 見えている範囲の中での強調の位置（見出しは数えない）。
    int relative_hover = 0;
    if (menu.last_hovered >= 0) {
        if (w.last_visible == menu.last_hovered) {
            relative_hover = -1;
        } else {
            relative_hover = menu.last_hovered - w.first_visible;
            int headers = 0;
            for (int i = 0; i <= relative_hover; ++i) {
                const int at = w.first_visible + i;
                if ((at < n) && (menu.items[static_cast<std::size_t>(at)].level < 2)) {
                    ++headers;
                }
            }
            relative_hover = std::max(0, relative_hover - headers);
        }
    }
    if (up) {
        if (w.first_visible == 0) {
            relative_hover = 0;
        } else {
            this->menu_scroll_bottom_to_item(w, w.first_visible);
        }
    } else {
        if (w.last_visible == (n - 1)) {
            relative_hover = -1;
        } else {
            this->menu_scroll_to_item(w, w.last_visible);
        }
    }
    if (adjust_hover) {
        int hover_target = 0;
        if (relative_hover >= 0) {
            for (int i = 0; (i <= relative_hover) && ((w.first_visible + i) <= w.last_visible); ++i) {
                if (menu.items[static_cast<std::size_t>(w.first_visible + i)].level < 2) {
                    ++relative_hover;
                }
            }
            hover_target = w.first_visible + relative_hover;
        } else {
            hover_target = w.last_visible + relative_hover + 1;
        }
        hover_target = std::clamp(hover_target, w.first_visible, w.last_visible);
        this->menu_set_hovered(w, this->menu_next_hoverable(w, false, hover_target, true), true, false);
    }
}

bool DcssWindows::menu_handle_key(DcssWindow &w, const SDL_Event &event)
{
    View *v = this->view_of(w);
    if (v == nullptr) {
        return false;
    }
    const SDL_Keycode sym = event.key.keysym.sym;
    const Uint16 mod = event.key.keysym.mod;
    if ((mod & (KMOD_ALT | KMOD_CTRL)) != 0) {
        return false;
    }
    DcssMenuData &menu = w.menu;
    const bool shift = (mod & KMOD_SHIFT) != 0;
    const bool arrows_select = (menu.flags & kMfArrowsSelect) != 0;
    const int n = static_cast<int>(menu.items.size());
    switch (sym) {
    case SDLK_KP_MINUS:
        if (menu_has_custom_dash(menu.tag)) {
            return false;
        }
        this->menu_paging(w, true);
        return true;
    case SDLK_PAGEUP:
        this->menu_paging(w, true);
        return true;
    case SDLK_KP_PLUS:
    case SDLK_PAGEDOWN:
        this->menu_paging(w, false);
        return true;
    case SDLK_END:
        this->menu_scroll_bottom_to_item(w, n - 1);
        if ((n > 0) && arrows_select) {
            this->menu_set_hovered(w, this->menu_next_hoverable(w, true, n - 1, true), true, false);
        }
        return true;
    case SDLK_HOME:
        this->menu_scroll_to_item(w, 0);
        if (arrows_select) {
            this->menu_set_hovered(w, this->menu_next_hoverable(w, false, 0, true), true, false);
        }
        return true;
    case SDLK_UP:
        if (arrows_select && !shift) {
            this->menu_cycle_hover(w, true);
        } else {
            this->menu_line_step(w, -1);
        }
        return true;
    case SDLK_DOWN:
        if (arrows_select && !shift) {
            this->menu_cycle_hover(w, false);
        } else {
            this->menu_line_step(w, 1);
        }
        return true;
    case SDLK_LEFT:
        if (shift) {
            this->menu_line_step(w, -1);
            return true;
        }
        return false;
    case SDLK_RIGHT:
        if (shift) {
            this->menu_line_step(w, 1);
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool DcssWindows::menu_handle_char(DcssWindow &w, int ch)
{
    DcssMenuData &menu = w.menu;
    if ((ch == ' ') && ((menu.flags & kMfMultiSelect) != 0) && ((menu.flags & kMfArrowsSelect) != 0)) {
        this->send_key('.');
        return true;
    }
    switch (ch) {
    case '-':
        if (menu_has_custom_dash(menu.tag)) {
            return false;
        }
        [[fallthrough]];
    case '<':
        if ((ch == '<') && (menu.tag == "travel")) {
            return false;
        }
        [[fallthrough]];
    case ';':
        this->menu_paging(w, true);
        return true;
    case '>':
        if (menu.tag == "travel") {
            return false;
        }
        [[fallthrough]];
    case '+':
    case ' ':
        this->menu_paging(w, false);
        return true;
    default:
        return false;
    }
}

void DcssWindows::menu_click_item(DcssWindow &w, int index, bool left)
{
    DcssMenuData &menu = w.menu;
    if (!dcss_menu_item_selectable(menu, index)) {
        return;
    }
    const DcssMenuItem &item = menu.items[static_cast<std::size_t>(index)];
    if ((menu.flags & kMfArrowsSelect) != 0) {
        this->menu_set_hovered(w, index, true, false);
        if ((menu.flags & kMfSingleSelect) != 0) {
            if (left) {
                this->send_key(13);
            } else {
                this->send_text("'");
            }
        } else if ((menu.flags & kMfMultiSelect) != 0) {
            if (left) {
                // ja5 のネメレクスの Stack Five（札を入れ替える複数選択のメニュー）は、項目の数字のキー（'1'〜'9'）で札を入れ替える。
                // ウェブ版の動き（空白 = 強調の項目の選択の切り替え）では 2 回押すと 2 枚とも選択済みになるだけで入れ替わらないので、
                // 数字のキーを持つ項目は、そのキーを送る（強調はすでに項目へ移してある）。
                const bool digit_key = !item.hotkeys.empty() && (item.hotkeys.front() >= '1') && (item.hotkeys.front() <= '9');
                this->send_key(digit_key ? item.hotkeys.front() : 32);
            } else {
                this->send_text("'");
            }
        }
    } else if (!item.hotkeys.empty() && left) {
        this->send_key(item.hotkeys.front());
    }
}

void DcssWindows::scroll_by(DcssWindow &w, int delta)
{
    View *v = this->view_of(w);
    if (v == nullptr) {
        return;
    }
    if (w.kind == DcssWindow::Kind::Menu) {
        const int max_scroll = std::max(0, v->menu_total - v->viewport.h);
        w.scroll_y = std::clamp(w.scroll_y + delta, 0, max_scroll);
        this->menu_update_visible(w, *v);
        return;
    }
    const int max_scroll = std::max(0, v->body_total - v->body_viewport.h);
    const int before = w.scroll_y;
    w.scroll_y = std::clamp(w.scroll_y + delta, 0, max_scroll);
    if ((w.scroll_y != before) && (w.kind == DcssWindow::Kind::Ui) && (w.type == "formatted-scroller") && (v->line_h > 0)) {
        this->send_line("{\"msg\":\"formatted_scroller_scroll\",\"scroll\":" + std::to_string((w.scroll_y + (v->line_h / 2)) / v->line_h) + "}");
    }
}

// ================================================================== 出来事

bool DcssWindows::on_event(const SDL_Event &event, bool reserved_key)
{
    switch (event.type) {
    case SDL_KEYDOWN:
        return this->on_key_down(event, reserved_key);
    case SDL_TEXTINPUT:
        return this->on_text(event);
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEWHEEL:
        return this->on_mouse(event);
    default:
        return false;
    }
}

namespace {

//! SDL のキーから、原作の符号（`CK_*` や文字コード）を出す。0 なら符号が無い（食うだけで送らない）。
int translate_key(const SDL_KeyboardEvent &key)
{
    const SDL_Keycode sym = key.keysym.sym;
    const Uint16 mod = key.keysym.mod;
    const bool shift = (mod & KMOD_SHIFT) != 0;
    const bool ctrl = (mod & KMOD_CTRL) != 0;
    const bool num = (mod & KMOD_NUM) != 0;
    const auto arrow = [&](int base) { return ctrl ? (base + kCkCtrlOffset) : (shift ? (base + kCkShiftOffset) : base); };
    switch (sym) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return 13;
    case SDLK_ESCAPE:
        return 27;
    case SDLK_BACKSPACE:
        return 8;
    case SDLK_TAB:
        return ctrl ? kCkCtrlTab : (shift ? kCkShiftTab : 9);
    case SDLK_DELETE:
        return kCkDelete;
    case SDLK_UP:
        return arrow(kCkUp);
    case SDLK_DOWN:
        return arrow(kCkDown);
    case SDLK_LEFT:
        return arrow(kCkLeft);
    case SDLK_RIGHT:
        return arrow(kCkRight);
    case SDLK_INSERT:
        return arrow(kCkInsert);
    case SDLK_HOME:
        return arrow(kCkHome);
    case SDLK_END:
        return arrow(kCkEnd);
    case SDLK_PAGEUP:
        return arrow(kCkPgUp);
    case SDLK_PAGEDOWN:
        return arrow(kCkPgDn);
    case SDLK_KP_8:
        return num ? 0 : arrow(kCkUp);
    case SDLK_KP_2:
        return num ? 0 : arrow(kCkDown);
    case SDLK_KP_4:
        return num ? 0 : arrow(kCkLeft);
    case SDLK_KP_6:
        return num ? 0 : arrow(kCkRight);
    default:
        break;
    }
    if (ctrl && (sym >= SDLK_a) && (sym <= SDLK_z)) {
        return static_cast<int>(sym - SDLK_a) + 1;
    }
    return 0;
}

} // namespace

bool DcssWindows::on_key_down(const SDL_Event &event, bool reserved_key)
{
    if (!this->captures_keys() || reserved_key) {
        return false;
    }
    const SDL_Keycode sym = event.key.keysym.sym;
    const Uint16 mod = event.key.keysym.mod;
    if (((sym >= SDLK_F1) && (sym <= SDLK_F12)) || ((mod & (KMOD_ALT | KMOD_GUI)) != 0)) {
        return false;
    }
    DcssWindow *top = this->stack_.top();
    if (top != nullptr) {
        if (top->kind == DcssWindow::Kind::Menu) {
            if (this->menu_handle_key(*top, event)) {
                return true;
            }
        } else if (top->kind == DcssWindow::Kind::Ui) {
            View *v = this->view_of(*top);
            const bool plain = (mod & (KMOD_CTRL | KMOD_SHIFT)) == 0;
            if (plain && ((sym == SDLK_LEFT) || (sym == SDLK_RIGHT))) {
                // ja5: 左右は説明の窓の下の操作の行のカーソル（品物・地形）か、引用の面の切り替え（説明）。コアも同じ動きをするが、
                // ウェブへは状態が来ないので画面が同じ動きを覚える（キーはコアへも送る）。怪物・神の画面の面は、コアが `ui-state` の `pane` で送ってくる。
                const int step = (sym == SDLK_RIGHT) ? 1 : -1;
                if (((top->type == "describe-item") || (top->type == "describe-feature-wide")) && (top->bar_count > 0)) {
                    top->bar_cursor = (top->bar_cursor + step + top->bar_count) % top->bar_count;
                } else if (top->type == "describe-generic") {
                    const json q = top->data.contains("quote") ? top->data["quote"] : json();
                    if (q.is_string() && !q.get<std::string>().empty()) {
                        top->pane = (top->pane == 0) ? 1 : 0;
                        top->scroll_y = 0;
                    }
                }
            }
            // 作り始めのおまかせの組み合わせの問い（`newgame-random-combo`）は捲る中身が無く、ja5 から十字キーを「はい・引き直す・やめる」のメニューを開くキーに使う。
            // 画面で捲らず、そのままコアへ送る。
            if ((v != nullptr) && plain && (v->body_total > 0) && (top->type != "newgame-random-combo")) {
                const int page = std::max(v->line_h, ((v->body_viewport.h - 24) / std::max(1, v->line_h)) * v->line_h);
                switch (sym) {
                case SDLK_PAGEUP:
                    this->scroll_by(*top, -page);
                    return true;
                case SDLK_PAGEDOWN:
                    this->scroll_by(*top, page);
                    return true;
                case SDLK_UP:
                    this->scroll_by(*top, -v->line_h);
                    return true;
                case SDLK_DOWN:
                    this->scroll_by(*top, v->line_h);
                    return true;
                case SDLK_END:
                    this->scroll_by(*top, INT_MAX / 2);
                    return true;
                case SDLK_HOME: {
                    const bool help = (top->type == "formatted-scroller") && (top->data.value("tag", std::string()) == "help");
                    if (!help) {
                        this->scroll_by(*top, -INT_MAX / 2);
                        return true;
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }
    }
    const int code = translate_key(event.key);
    if (code != 0) {
        this->send_key(code);
    }
    return true;
}

bool DcssWindows::on_text(const SDL_Event &event)
{
    if (!this->captures_keys()) {
        return false;
    }
    const std::string text = event.text.text;
    if (text.empty()) {
        return true;
    }
    DcssWindow *top = this->stack_.top();
    const std::vector<char32_t> cps = dcss_decode_utf8(text);
    for (const char32_t cp : cps) {
        const int ch = static_cast<int>(cp);
        if ((top != nullptr) && (ch > 0) && (ch < 128)) {
            if (top->kind == DcssWindow::Kind::Menu) {
                if (this->menu_handle_char(*top, ch)) {
                    continue;
                }
            } else if (top->kind == DcssWindow::Kind::Ui) {
                View *v = this->view_of(*top);
                if ((v != nullptr) && (v->body_total > 0) && !(top->type == "describe-feature-wide" && ((ch == '<') || (ch == '>')))) {
                    const int page = std::max(v->line_h, ((v->body_viewport.h - 24) / std::max(1, v->line_h)) * v->line_h);
                    if ((ch == ' ') || (ch == '>') || (ch == '+') || (ch == '\'')) {
                        this->scroll_by(*top, page);
                        continue;
                    }
                    if ((ch == '-') || (ch == '<') || (ch == ';')) {
                        this->scroll_by(*top, -page);
                        continue;
                    }
                }
                // 面の切り替え（説明の窓の `!`・`^`）。キーはコアへも送る（神の画面のコアの状態が変わる）。
                if (((ch == '!') || (ch == '^')) && (top->type == "describe-generic")) {
                    top->pane = (top->pane == 0) ? 1 : 0; // 説明 ⇔ 引用の 2 面（コアも 1 から引いて切り替える）
                    top->scroll_y = 0;
                } else if (((ch == '!') || (ch == '^')) && ((top->type == "describe-monster") || (top->type == "describe-god"))) {
                    top->pane = (top->pane + 1) % 4; // 面の数は描画側が丸める。正しい面はコアが `ui-state` の `pane` で送ってくる
                    top->scroll_y = 0;
                }
            }
        }
        this->send_key(ch);
    }
    return true;
}

bool DcssWindows::on_mouse(const SDL_Event &event)
{
    if (this->stack_.empty()) {
        return this->stack_.name_prompt() ? this->on_name_mouse(event) : false;
    }
    DcssWindow *top = this->stack_.top();
    View *v = this->view_of(*top);
    if (event.type == SDL_MOUSEMOTION) {
        this->mouse_x_ = event.motion.x;
        this->mouse_y_ = event.motion.y;
    }
    if (v == nullptr) {
        return true;
    }
    if (top->kind == DcssWindow::Kind::Menu) {
        const RectPx vp = v->viewport;
        const auto entry_at = [&](int px, int py) -> int {
            if (!vp.contains(px, py)) {
                return -1;
            }
            const int ly = (py - vp.y) + top->scroll_y;
            const int lx = px - vp.x;
            const int col_width = std::max(1, vp.w / std::max(1, v->menu.num_columns));
            const int col = std::min(v->menu.num_columns - 1, lx / col_width);
            for (std::size_t i = 0; i < v->menu.entries.size(); ++i) {
                const DcssMenuEntryLayout &e = v->menu.entries[i];
                if (e.heading) {
                    continue;
                }
                if ((e.column == col) && (ly >= e.y) && (ly < (e.y + e.height))) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        if (event.type == SDL_MOUSEMOTION) {
            const int idx = entry_at(event.motion.x, event.motion.y);
            if (idx >= 0) {
                this->menu_set_hovered(*top, idx, false, true);
            } else if (((top->menu.flags & kMfArrowsSelect) == 0) && (top->menu.last_hovered >= 0)) {
                this->menu_set_hovered(*top, -1, false, true);
            }
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.which == SDL_TOUCH_MOUSEID) {
                return true;
            }
            const int idx = entry_at(event.button.x, event.button.y);
            if (idx >= 0) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    this->menu_click_item(*top, idx, true);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    this->menu_click_item(*top, idx, false);
                }
            } else if (popup_close_click(v->geo.frame, event.button.button, event.button.x, event.button.y)) {
                // 窓の外の左クリックと、項目の無い所の右クリックで窓を閉じる（原作: ウェブ版は枠の外のクリックで Esc、タイル版は右クリック = `CK_MOUSE_CMD`）。
                this->send_key(27);
            }
        } else if (event.type == SDL_MOUSEWHEEL) {
            if (event.wheel.y != 0) {
                this->scroll_by(*top, -event.wheel.y * 20);
            }
        }
        return true;
    }
    if (event.type == SDL_MOUSEWHEEL) {
        if (event.wheel.y != 0) {
            this->scroll_by(*top, -event.wheel.y * 20);
        }
        return true;
    }
    if (top->kind == DcssWindow::Kind::Crt) {
        // 文字画面（開始メニュー）の項目。タイル版と同じく、マウスを動かすとその項目が選択され、押すと決まる
        // （`OuterMenu` の `on_mousemove_event` で選択を移し、`MenuButton` が押されて `activate`）。
        // 選択中の項目はコアが覚えているので、今の選択から対象の項目までの距離だけ上下キーを送り、決めるときは Enter を送る。
        const auto current_focus = [&]() {
            const std::uint32_t now = SDL_GetTicks();
            if ((top->crt_pending_focus >= 0) && ((now - top->crt_pending_at) < 400)) {
                return top->crt_pending_focus;
            }
            int focus = -1;
            for (const View::Hit &h : v->hits) {
                if ((h.kind == 2) && (h.hotkey == 1)) {
                    focus = h.index;
                }
            }
            return focus;
        };
        const auto move_focus_to = [&](int index) {
            const int focus = current_focus();
            if ((focus < 0) || (focus == index)) {
                return;
            }
            const int delta = index - focus;
            for (int i = 0; i < std::abs(delta); ++i) {
                this->send_key((delta > 0) ? kCkDown : kCkUp);
            }
            top->crt_pending_focus = index;
            top->crt_pending_at = SDL_GetTicks();
        };
        if (event.type == SDL_MOUSEMOTION) {
            for (const View::Hit &h : v->hits) {
                if ((h.kind == 2) && h.rect.contains(event.motion.x, event.motion.y)) {
                    move_focus_to(h.index);
                    break;
                }
            }
        } else if (event.type == SDL_MOUSEWHEEL) {
            // セーブデータの欄の捲り
            if ((event.wheel.y != 0) && (v->body_total > v->body_viewport.h) && v->body_viewport.contains(this->mouse_x_, this->mouse_y_)) {
                this->scroll_by(*top, -event.wheel.y * 20);
            }
        } else if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.which != SDL_TOUCH_MOUSEID) && (event.button.button == SDL_BUTTON_LEFT)) {
            for (const View::Hit &h : v->hits) {
                if ((h.kind == 2) && h.rect.contains(event.button.x, event.button.y)) {
                    move_focus_to(h.index);
                    this->send_key(13);
                    break;
                }
            }
        }
        return true;
    }
    if ((event.type == SDL_MOUSEMOTION) && (top->type != "newgame-choice")) {
        // 説明の下の操作の文の強調
        int hovered = -1;
        for (const View::Hit &h : v->hits) {
            if ((h.kind == 3) && h.rect.contains(event.motion.x, event.motion.y)) {
                hovered = h.index;
                break;
            }
        }
        top->hover_action = hovered;
    }
    if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.which != SDL_TOUCH_MOUSEID)) {
        // 作成の格子のボタン・説明の下の操作
        for (const View::Hit &h : v->hits) {
            if (h.rect.contains(event.button.x, event.button.y) && ((h.kind == 1) || (h.kind == 3))) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    this->send_key(h.hotkey);
                    return true;
                }
                if (h.kind == 3) {
                    return true;
                }
                break;
            }
        }
        if ((top->type != "progress-bar") && popup_close_click(v->geo.frame, event.button.button, event.button.x, event.button.y)) {
            this->send_key(27);
            return true;
        }
        if ((top->type == "formatted-scroller") && (top->data.value("easy_exit", false)) && (event.button.button == SDL_BUTTON_LEFT)) {
            this->send_key(27);
        }
    }
    if ((event.type == SDL_MOUSEMOTION) && (top->type == "newgame-choice")) {
        int hovered = -1;
        for (const View::Hit &h : v->hits) {
            if ((h.kind == 1) && h.rect.contains(event.motion.x, event.motion.y)) {
                hovered = h.index;
                if (top->focus_hotkey != h.hotkey) {
                    top->focus_hotkey = h.hotkey;
                    json m;
                    m["msg"] = "outer_menu_focus";
                    m["menu_id"] = top->state.value("menu_id", std::string());
                    m["hotkey"] = h.hotkey;
                    if (!top->data.is_null() && top->data.contains("main-items")) {
                        // メニューの名前は、ボタンの属する側（main-items / sub-items）の `menu_id`
                        const auto &side = (h.kind == 1 && h.index >= 100000) ? top->data["sub-items"] : top->data["main-items"];
                        m["menu_id"] = side.value("menu_id", std::string());
                    }
                    this->send_line(m.dump(-1, ' ', false, json::error_handler_t::replace));
                }
                break;
            }
        }
        top->hover_button = hovered;
    }
    return true;
}

} // namespace hd2d::dcss
