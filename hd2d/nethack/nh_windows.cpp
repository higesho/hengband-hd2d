/*!
 * @file nh_windows.cpp
 * @brief `nh_windows.h` の実装のうち、要求の取り込み・出来事・返事（描画は `nh_windows_draw.cpp`）。
 */
#include "nethack/nh_windows.h"

#include "render/gl_program.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace hd2d::nethack {

namespace {

using nlohmann::json;

std::string str_of(const json &o, const char *key)
{
    const auto it = o.find(key);
    return ((it != o.end()) && it->is_string()) ? it->get<std::string>() : std::string();
}

int int_of(const json &o, const char *key, int fallback)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_number()) {
        return fallback;
    }
    return it->is_number_integer() ? static_cast<int>(it->get<long long>()) : static_cast<int>(it->get<double>());
}

std::string read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string reply_json(int r, const char *a)
{
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = r;
    out["a"] = a;
    return out.dump();
}

//! 原作の GUI 版は、矢印キーを移動のキー（`hjklyubn`）へ直してコアへ渡す（`number_pad` が 0 のとき）。方向を聞く問いで使う。
int direction_key_of(SDL_Keycode sym)
{
    switch (sym) {
    case SDLK_UP:
    case SDLK_KP_8: return 'k';
    case SDLK_DOWN:
    case SDLK_KP_2: return 'j';
    case SDLK_LEFT:
    case SDLK_KP_4: return 'h';
    case SDLK_RIGHT:
    case SDLK_KP_6: return 'l';
    case SDLK_HOME:
    case SDLK_KP_7: return 'y';
    case SDLK_PAGEUP:
    case SDLK_KP_9: return 'u';
    case SDLK_END:
    case SDLK_KP_1: return 'b';
    case SDLK_PAGEDOWN:
    case SDLK_KP_3: return 'n';
    default: return 0;
    }
}

} // namespace

void NhWindows::set_tile_dir(const std::string &dir)
{
    this->tile_dir_ = dir;
}

bool NhWindows::load_tiles()
{
    if (this->tile_dir_.empty()) {
        return false;
    }
    if (this->tile_dir_ == this->tile_dir_loaded_) {
        return this->tiles_ready_;
    }
    this->tile_dir_loaded_ = this->tile_dir_; // 失敗しても同じ場所は読み直さない
    this->tiles_ready_ = false;
    this->tile_image_.unload();
    const std::string json_text = read_file(this->tile_dir_ + "/tiles.json");
    std::string err;
    if (json_text.empty() || !nh_tiles_parse(json_text, this->tile_table_, err)) {
        std::fprintf(stderr, "[hd2d] NetHack のタイルの一覧を読めません（絵なしで字だけ出します）: %s/tiles.json %s\n", this->tile_dir_.c_str(),
            err.c_str());
        return false;
    }
    if (!this->tile_image_.load(this->tile_dir_ + "/tiles.png", err)) {
        std::fprintf(stderr, "[hd2d] NetHack のタイルの絵を読めません（絵なしで字だけ出します）: %s\n", err.c_str());
        return false;
    }
    // 絵は拡大して貼るので、最近傍にする（読み込みの既定は線形補間）。
    {
        using namespace hd2d::gl;
        glBindTexture(GL_TEXTURE_2D, this->tile_image_.texture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    this->tiles_ready_ = true;
    std::fprintf(stderr, "[hd2d] NetHack のタイルの絵を読みました: %s (%dx%d, 絵 %d 個)\n", this->tile_dir_.c_str(), this->tile_image_.width(),
        this->tile_image_.height(), this->tile_table_.tile_count);
    return true;
}

void NhWindows::shutdown()
{
    this->tile_image_.unload();
    this->tiles_ready_ = false;
    this->tile_dir_loaded_.clear();
}

void NhWindows::sync(const NhModel &model)
{
    if (model.invent_version() != this->invent_serial_) {
        // 持ち物の常設の窓の中身（返事は要らない）。窓だけ作られて中身がまだ無いときは空にする。
        this->invent_serial_ = model.invent_version();
        if (!this->invent_.load(model.invent_payload())) {
            this->invent_ = NhMenu{};
        }
    }
    const NhModel::Request *request = model.request();
    if (request == nullptr) {
        if (this->kind_ != Kind::None) {
            this->close(); // 要求が閉じられた（コアが終わった）
        }
        return;
    }
    if (request->serial == this->seen_serial_) {
        return;
    }
    this->seen_serial_ = request->serial;
    this->open_request(*request);
}

void NhWindows::open_request(const NhModel::Request &request)
{
    this->close();
    this->request_ = request.r;
    this->hits_.clear();
    this->menu_desired_w_ = 0;
    this->desired_serial_ = 0;
    this->field_.clear();
    this->text_top_ = 0;
    this->text_wrapped_w_ = -1;
    this->ext_pick_ = -1;
    this->ext_top_ = 0;
    this->menu_btn_focus_ = -1;
    this->swallow_space_ = false;
    const json m = json::parse(request.payload, nullptr, false);
    if (m.is_discarded() || !m.is_object()) {
        return;
    }
    const std::string kind = request.kind;
    if (kind == "menu") {
        if (this->menu_.load(request.payload)) {
            this->kind_ = Kind::Menu;
        }
    } else if (kind == "text" || kind == "rip") {
        this->text_lines_.clear();
        const auto lines = m.find("lines");
        if ((lines != m.end()) && lines->is_array()) {
            for (const json &e : *lines) {
                if (e.is_object()) {
                    this->text_lines_.push_back(TextLine{ str_of(e, "text"), int_of(e, "attr", 0) });
                }
            }
        }
        if (kind == "rip") {
            this->rip_.name = str_of(m, "name");
            this->rip_.killer = str_of(m, "killer");
            this->rip_.year = int_of(m, "year", 0);
            const auto gold = m.find("gold");
            this->rip_.gold = ((gold != m.end()) && gold->is_number()) ? static_cast<long>(gold->get<double>()) : 0;
            this->kind_ = Kind::Rip;
        } else {
            this->kind_ = Kind::Text;
        }
    } else if (kind == "select") {
        if (this->select_.load(request.payload)) {
            this->kind_ = Kind::Select;
            if (this->select_.pick_complete()) {
                // 設定ファイルで職業・種族・性別・属性が全部決まっている。原作の GUI 版は窓を出さずに始める。
                this->reply(this->select_.play_json());
            }
        }
    } else if (kind == "ask") {
        const std::string ask_kind = str_of(m, "kind");
        this->query_ = str_of(m, "query");
        this->choices_ = str_of(m, "choices");
        const std::string def = str_of(m, "def");
        this->def_ = def.empty() ? '\0' : def[0];
        if (ask_kind == "yn") {
            this->kind_ = Kind::Yn;
        } else if (ask_kind == "name") {
            this->field_.set_max_bytes(31);
            this->kind_ = Kind::Name;
        } else if (ask_kind == "ext_cmd") {
            this->field_.set_max_bytes(63);
            this->ext_cmds_.clear();
            const auto cmds = m.find("cmds");
            if ((cmds != m.end()) && cmds->is_array()) {
                for (const json &c : *cmds) {
                    if (c.is_string()) {
                        this->ext_cmds_.push_back(c.get<std::string>());
                    }
                }
            }
            this->kind_ = Kind::ExtCmd;
        } else {
            this->field_.set_max_bytes(255);
            this->kind_ = Kind::Line;
        }
        if ((this->kind_ == Kind::Name) || (this->kind_ == Kind::Line)) {
            this->field_.set_max_bytes((this->kind_ == Kind::Name) ? 31 : 255);
        }
    }
}

void NhWindows::close()
{
    this->kind_ = Kind::None;
    this->caret_valid_ = false;
    this->pending_tiles_.clear();
}

void NhWindows::reply(const std::string &json_text, const std::string &echo, int echo_attr)
{
    this->outcome_.replies.push_back(json_text);
    if (!echo.empty()) {
        this->outcome_.echo = echo;
        this->outcome_.echo_attr = echo_attr;
    }
    this->close();
}

NhOutcome NhWindows::take_outcome()
{
    NhOutcome out;
    std::swap(out, this->outcome_);
    return out;
}

bool NhWindows::wants_text_input() const
{
    return (this->kind_ == Kind::Line) || (this->kind_ == Kind::Name) || (this->kind_ == Kind::ExtCmd)
        || ((this->kind_ == Kind::Menu) && this->menu_.searching());
}

bool NhWindows::text_input_rect(SDL_Rect &rect) const
{
    if (!this->caret_valid_ || !this->wants_text_input()) {
        return false;
    }
    rect = this->caret_rect_;
    return true;
}

void NhWindows::set_message_caret(int x, int y, int h)
{
    this->caret_rect_ = SDL_Rect{ x, y, std::max(1, h), std::max(1, h) };
    this->caret_valid_ = true;
}

bool NhWindows::message_row(std::string &text, bool &input_mode) const
{
    if (this->kind_ == Kind::Yn) {
        text = nh_yn_prompt_text(this->query_, this->choices_, this->def_) + " ";
        input_mode = false;
        return true;
    }
    if (this->kind_ == Kind::Line) {
        text = this->query_ + " " + this->field_.text() + this->field_.preedit();
        input_mode = true;
        return true;
    }
    return false;
}

std::string NhWindows::key_hint(bool ja) const
{
    switch (this->kind_) {
    case Kind::Menu:
        if (this->menu_.how() == 0) {
            return ja ? "Space/Enter/Esc:閉じる  " : "Space/Enter/Esc:Close  ";
        }
        return ja ? "文字:選ぶ  Enter:決定  Space:次ページ  Esc:取消  " : "Letter:Pick  Enter:OK  Space:Next page  Esc:Cancel  ";
    case Kind::Text:
    case Kind::Rip:
        return ja ? "Space:次ページ/閉じる  Esc:閉じる  " : "Space:Next/Close  Esc:Close  ";
    case Kind::Yn:
        return ja ? "答えの文字  Esc:取消  " : "Answer key  Esc:Cancel  ";
    case Kind::Line:
    case Kind::Name:
        return ja ? "Enter:決定  Esc:取消  " : "Enter:OK  Esc:Cancel  ";
    case Kind::ExtCmd:
        return ja ? "Tab:補完  ↑↓:選ぶ  Enter:決定  Esc:取消  " : "Tab:Complete  Up/Down:Pick  Enter:OK  Esc:Cancel  ";
    case Kind::Select:
        return ja ? "Tab:移動  ↑↓:選ぶ  Enter:開始  Esc:終了  " : "Tab:Move  Up/Down:Pick  Enter:Play  Esc:Quit  ";
    case Kind::None:
        break;
    }
    return {};
}

// ---- 出来事 ----

void NhWindows::finish_menu(NhMenuResult result)
{
    if (result == NhMenuResult::Submit) {
        this->reply(this->menu_.submit_json());
    } else if (result == NhMenuResult::Cancel) {
        this->reply(this->menu_.cancel_json());
    }
}

void NhWindows::text_scroll(int rows)
{
    const int total = static_cast<int>(this->text_wrapped_.size());
    this->text_top_ = std::clamp(this->text_top_ + rows, 0, std::max(0, total - this->text_page_));
}

std::vector<int> NhWindows::yn_answer_keys() const
{
    std::vector<int> keys;
    if (this->kind_ != Kind::Yn) {
        return keys;
    }
    const auto add = [&](int c) {
        if ((c > 0x20) && (c < 0x7F) && (std::find(keys.begin(), keys.end(), c) == keys.end())) {
            keys.push_back(c);
        }
    };
    if (!this->choices_.empty()) {
        for (const char c : this->choices_) {
            if (c == '\033') {
                break; // ここから後は原作の隠れた答え
            }
            add(static_cast<unsigned char>(c));
        }
        return keys;
    }
    // 問いの文の最後の [ ... ]。`or` の語は飛ばし、`a-e` は広げる
    const std::size_t open = this->query_.rfind('[');
    const std::size_t close = (open == std::string::npos) ? std::string::npos : this->query_.find(']', open);
    if ((open == std::string::npos) || (close == std::string::npos)) {
        return keys;
    }
    const std::string inside = this->query_.substr(open + 1, close - open - 1);
    for (std::size_t i = 0; i < inside.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(inside[i]);
        if ((c == 'o') && (i + 1 < inside.size()) && (inside[i + 1] == 'r') && ((i == 0) || (inside[i - 1] == ' '))
            && ((i + 2 >= inside.size()) || (inside[i + 2] == ' '))) {
            ++i; // 「or」
            continue;
        }
        if ((c == '-') && !keys.empty() && (i + 1 < inside.size())) {
            const int from = keys.back();
            const int to = static_cast<unsigned char>(inside[i + 1]);
            for (int k = from + 1; k <= to; ++k) {
                add(k);
            }
            ++i;
            continue;
        }
        if (c >= 0x80) {
            break; // 日本語の文に入った（括弧の中は字だけのはず）
        }
        add(c);
    }
    return keys;
}

void NhWindows::send_yn(int key)
{
    char result = 0;
    if (!nh_resolve_yn(key, this->choices_, this->def_, result)) {
        return; // 選べない字は捨てる（原作は `nhbell`）
    }
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = this->request_;
    out["a"] = "yn";
    out["key"] = key;
    std::string echo = nh_yn_prompt_text(this->query_, this->choices_, this->def_);
    if ((result >= 0x21) && (result < 0x7F)) {
        echo += std::string(" ") + result;
    }
    this->reply(out.dump(), echo, kAtrBold);
}

void NhWindows::submit_text_field()
{
    std::string text = this->field_.text();
    std::string echo;
    if (this->kind_ == Kind::ExtCmd) {
        const std::vector<int> matches = nh_ext_matches(this->ext_cmds_, text);
        if ((this->ext_pick_ >= 0) && (this->ext_pick_ < static_cast<int>(matches.size()))) {
            text = this->ext_cmds_[static_cast<std::size_t>(matches[static_cast<std::size_t>(this->ext_pick_)])];
        }
    } else if (this->kind_ == Kind::Line) {
        echo = this->query_ + " " + text;
    }
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = this->request_;
    out["a"] = "text";
    out["text"] = text;
    this->reply(out.dump(-1, ' ', false, json::error_handler_t::replace), echo, kAtrBold);
}

void NhWindows::cancel_request()
{
    switch (this->kind_) {
    case Kind::Menu:
        this->finish_menu(NhMenuResult::Cancel);
        break;
    case Kind::Text:
    case Kind::Rip:
        this->reply(reply_json(this->request_, "dismiss"));
        break;
    case Kind::Yn:
        this->send_yn(0x1B);
        break;
    case Kind::Line:
    case Kind::Name:
    case Kind::ExtCmd:
        this->reply(reply_json(this->request_, "cancel"));
        break;
    case Kind::Select:
        this->reply(this->select_.quit_json());
        break;
    case Kind::None:
        break;
    }
}

void NhWindows::on_text(const std::string &utf8)
{
    switch (this->kind_) {
    case Kind::Menu:
        if (this->menu_.searching()) {
            this->menu_.on_search_text(utf8);
            return;
        }
        for (const char c : utf8) {
            if (!this->active()) {
                break;
            }
            if (c == ' ') {
                this->finish_menu(this->menu_.on_key(NhMenuKey::Space));
            } else if ((static_cast<unsigned char>(c) >= 0x21) && (static_cast<unsigned char>(c) < 0x7F)) {
                this->finish_menu(this->menu_.on_char(c));
            }
        }
        break;
    case Kind::Text:
        for (const char c : utf8) {
            switch (c) {
            case ' ':
                if (this->text_top_ + this->text_page_ >= static_cast<int>(this->text_wrapped_.size())) {
                    this->reply(reply_json(this->request_, "dismiss"));
                    return;
                }
                this->text_scroll(this->text_page_);
                break;
            case '>': this->text_scroll(this->text_page_); break;
            case '<': this->text_scroll(-this->text_page_); break;
            case '^': this->text_top_ = 0; break;
            case '|': this->text_scroll(1 << 20); break;
            default: break;
            }
        }
        break;
    case Kind::Rip:
        if (utf8 == " ") {
            this->reply(reply_json(this->request_, "dismiss"));
        }
        break;
    case Kind::Yn: {
        const unsigned char c = utf8.empty() ? 0 : static_cast<unsigned char>(utf8[0]);
        if ((c >= 0x20) && (c < 0x7F)) {
            this->send_yn(c);
        }
        break;
    }
    case Kind::Line:
    case Kind::Name:
    case Kind::ExtCmd:
        this->field_.insert(utf8);
        this->field_.set_preedit(std::string());
        this->ext_pick_ = -1;
        break;
    case Kind::Select:
        if (utf8 == " ") {
            switch (this->select_.focus()) {
            case NhSelFocus::Play: this->reply(this->select_.play_json()); break;
            case NhSelFocus::Random: this->reply(this->select_.random_json()); break;
            case NhSelFocus::Quit: this->reply(this->select_.quit_json()); break;
            default: break;
            }
        }
        break;
    case Kind::None:
        break;
    }
}

bool NhWindows::on_pad(NativePad pad)
{
    if (this->kind_ == Kind::Menu) {
        if (pad == NativePad::B) {
            this->finish_menu(this->menu_.on_key(NhMenuKey::Escape));
            return true;
        }
        if (this->menu_btn_focus_ < 0) {
            switch (pad) {
            case NativePad::Up:
                this->finish_menu(this->menu_.on_key(NhMenuKey::Up));
                break;
            case NativePad::Down: {
                // 一番下の行（それ以上動かない・送れない）からさらに下なら、下のボタンへ
                const int cursor = this->menu_.cursor();
                const int top = this->menu_.top();
                this->finish_menu(this->menu_.on_key(NhMenuKey::Down));
                if ((this->kind_ == Kind::Menu) && (this->menu_.cursor() == cursor) && (this->menu_.top() == top)) {
                    this->menu_btn_focus_ = 0;
                }
                break;
            }
            case NativePad::Left:
                this->finish_menu(this->menu_.on_key(NhMenuKey::PageUp));
                break;
            case NativePad::Right:
                this->finish_menu(this->menu_.on_key(NhMenuKey::PageDown));
                break;
            case NativePad::A:
                if (this->menu_.how() == 0) {
                    this->finish_menu(NhMenuResult::Submit); // 見るだけのメニュー
                } else {
                    this->finish_menu(this->menu_.click_row(this->menu_.cursor(), false)); // クリック 1 回と同じ（印を付ける）
                }
                break;
            default:
                break;
            }
            return true;
        }
        switch (pad) {
        case NativePad::Up:
            this->menu_btn_focus_ = -1;
            break;
        case NativePad::Left:
            this->menu_btn_focus_ = 0;
            break;
        case NativePad::Right:
            this->menu_btn_focus_ = 1;
            break;
        case NativePad::A:
            if (this->menu_btn_focus_ == 0) {
                this->finish_menu(NhMenuResult::Submit);
            } else {
                this->cancel_request();
            }
            break;
        default:
            break;
        }
        return true;
    }
    // ほかの窓はキーと同じ（矢印・Enter・Esc）
    SDL_KeyboardEvent key{};
    key.type = SDL_KEYDOWN;
    key.state = SDL_PRESSED;
    switch (pad) {
    case NativePad::Up: key.keysym.sym = SDLK_UP; break;
    case NativePad::Down: key.keysym.sym = SDLK_DOWN; break;
    case NativePad::Left: key.keysym.sym = SDLK_LEFT; break;
    case NativePad::Right: key.keysym.sym = SDLK_RIGHT; break;
    case NativePad::A: key.keysym.sym = SDLK_RETURN; break;
    case NativePad::B: key.keysym.sym = SDLK_ESCAPE; break;
    }
    this->on_keydown(key);
    return true;
}

void NhWindows::on_keydown(const SDL_KeyboardEvent &key)
{
    const SDL_Keycode sym = key.keysym.sym;
    const bool shift = (key.keysym.mod & KMOD_SHIFT) != 0;
    if ((key.keysym.mod & KMOD_CTRL) != 0) {
        return; // 窓が開いている間の Ctrl の組み合わせは何もしない（コマンドとして漏らさない）
    }
    const bool is_space = (sym == SDLK_SPACE);
    if (is_space) {
        this->swallow_space_ = true; // 続く `SDL_TEXTINPUT` の空白は落とす
        this->on_text(" ");
        return;
    }
    const bool enter = (sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER);
    const bool escape = (sym == SDLK_ESCAPE) || (sym == SDLK_AC_BACK);
    switch (this->kind_) {
    case Kind::Menu: {
        NhMenuKey k;
        if (escape) {
            k = NhMenuKey::Escape;
        } else if (enter) {
            k = NhMenuKey::Enter;
        } else if (sym == SDLK_UP) {
            k = NhMenuKey::Up;
        } else if (sym == SDLK_DOWN) {
            k = NhMenuKey::Down;
        } else if (sym == SDLK_PAGEUP) {
            k = NhMenuKey::PageUp;
        } else if (sym == SDLK_PAGEDOWN) {
            k = NhMenuKey::PageDown;
        } else if (sym == SDLK_HOME) {
            k = NhMenuKey::Home;
        } else if (sym == SDLK_END) {
            k = NhMenuKey::End;
        } else if (sym == SDLK_BACKSPACE) {
            k = NhMenuKey::Backspace;
        } else {
            return;
        }
        this->finish_menu(this->menu_.on_key(k));
        break;
    }
    case Kind::Text:
        if (escape || enter) {
            this->reply(reply_json(this->request_, "dismiss"));
        } else if (sym == SDLK_UP) {
            this->text_scroll(-1);
        } else if (sym == SDLK_DOWN) {
            this->text_scroll(1);
        } else if (sym == SDLK_PAGEUP) {
            this->text_scroll(-this->text_page_);
        } else if (sym == SDLK_PAGEDOWN) {
            this->text_scroll(this->text_page_);
        } else if (sym == SDLK_HOME) {
            this->text_top_ = 0;
        } else if (sym == SDLK_END) {
            this->text_scroll(1 << 20);
        }
        break;
    case Kind::Rip:
        if (escape || enter) {
            this->reply(reply_json(this->request_, "dismiss"));
        }
        break;
    case Kind::Yn:
        if (escape) {
            this->send_yn(0x1B);
        } else if (enter) {
            this->send_yn('\r');
        } else if (this->choices_.empty()) {
            const int dir = direction_key_of(sym);
            if (dir != 0) {
                this->send_yn(dir);
            }
        }
        break;
    case Kind::Line:
    case Kind::Name:
        if (escape) {
            this->reply(reply_json(this->request_, "cancel"));
        } else if (enter) {
            this->submit_text_field();
        } else if (sym == SDLK_BACKSPACE) {
            (void)this->field_.backspace();
        }
        break;
    case Kind::ExtCmd: {
        const std::vector<int> matches = nh_ext_matches(this->ext_cmds_, this->field_.text());
        if (escape) {
            this->reply(reply_json(this->request_, "cancel"));
        } else if (enter) {
            this->submit_text_field();
        } else if (sym == SDLK_BACKSPACE) {
            (void)this->field_.backspace();
            this->ext_pick_ = -1;
        } else if (sym == SDLK_TAB) {
            // 補完: 1 つに決まればその名前、いくつか残るなら共通の前方まで。
            const std::string common = nh_ext_common_prefix(this->ext_cmds_, matches);
            if (common.size() > this->field_.text().size()) {
                this->field_.clear();
                this->field_.insert(common);
            }
            this->ext_pick_ = -1;
        } else if (sym == SDLK_DOWN) {
            if (!matches.empty()) {
                this->ext_pick_ = std::min(this->ext_pick_ + 1, static_cast<int>(matches.size()) - 1);
            }
        } else if (sym == SDLK_UP) {
            if (!matches.empty()) {
                this->ext_pick_ = std::max(this->ext_pick_ - 1, 0);
            }
        }
        break;
    }
    case Kind::Select:
        if (escape) {
            this->reply(this->select_.quit_json());
        } else if (enter) {
            switch (this->select_.focus()) {
            case NhSelFocus::Random: this->reply(this->select_.random_json()); break;
            case NhSelFocus::Quit: this->reply(this->select_.quit_json()); break;
            default: this->reply(this->select_.play_json()); break;
            }
        } else if (sym == SDLK_TAB) {
            this->select_.move_focus(shift ? -1 : 1);
        } else if (sym == SDLK_UP) {
            this->select_.move_choice(-1);
        } else if (sym == SDLK_DOWN) {
            this->select_.move_choice(1);
        } else if ((sym == SDLK_LEFT) || (sym == SDLK_RIGHT)) {
            const int step = (sym == SDLK_RIGHT) ? 1 : -1;
            const NhSelFocus f = this->select_.focus();
            if ((f == NhSelFocus::Align) || (f == NhSelFocus::Gender)) {
                this->select_.move_choice(step);
            } else {
                this->select_.move_focus(step);
            }
        }
        break;
    case Kind::None:
        break;
    }
}

bool NhWindows::on_mouse(const SDL_Event &event)
{
    if (event.type == SDL_MOUSEWHEEL) {
        const int rows = (event.wheel.y > 0) ? -3 : 3;
        if (this->kind_ == Kind::Menu) {
            this->menu_.scroll(rows);
        } else if ((this->kind_ == Kind::Text) || (this->kind_ == Kind::Rip)) {
            this->text_scroll(rows);
        } else if (this->kind_ == Kind::ExtCmd) {
            this->ext_top_ = std::max(0, this->ext_top_ + rows);
        }
        return true;
    }
    if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_RIGHT)) {
        // 右クリックは取り消し（Esc と同じ）。名前とキャラクタ作成は、うっかりでゲームを終えてしまうので除く。
        if ((this->kind_ != Kind::Name) && (this->kind_ != Kind::Select)) {
            this->cancel_request();
        }
        return true;
    }
    if ((event.type != SDL_MOUSEBUTTONDOWN) || (event.button.button != SDL_BUTTON_LEFT)) {
        return true;
    }
    const int mx = event.button.x;
    const int my = event.button.y;
    const bool dbl = event.button.clicks >= 2;
    for (auto it = this->hits_.rbegin(); it != this->hits_.rend(); ++it) {
        if (!it->rect.contains(mx, my)) {
            continue;
        }
        switch (it->type) {
        case Hit::MenuRow:
            this->finish_menu(this->menu_.click_row(it->a, dbl));
            break;
        case Hit::Role:
            (void)this->select_.choose_role(it->a);
            this->select_.set_focus(NhSelFocus::Roles);
            break;
        case Hit::Race:
            (void)this->select_.choose_race(it->a);
            this->select_.set_focus(NhSelFocus::Races);
            break;
        case Hit::Align:
            (void)this->select_.choose_align(it->a);
            this->select_.set_focus(NhSelFocus::Align);
            break;
        case Hit::Gender:
            (void)this->select_.choose_gender(it->a);
            this->select_.set_focus(NhSelFocus::Gender);
            break;
        case Hit::Play:
            this->reply(this->select_.play_json());
            break;
        case Hit::Random:
            this->reply(this->select_.random_json());
            break;
        case Hit::Quit:
            this->reply(this->select_.quit_json());
            break;
        case Hit::Ok:
            if (this->kind_ == Kind::Menu) {
                this->finish_menu(NhMenuResult::Submit);
            } else if (this->kind_ == Kind::Yn) {
                this->send_yn(this->def_ != 0 ? this->def_ : '\r');
            } else {
                this->submit_text_field();
            }
            break;
        case Hit::Cancel:
            this->cancel_request();
            break;
        case Hit::Close:
            this->reply(reply_json(this->request_, "dismiss"));
            break;
        case Hit::ExtRow: {
            this->ext_pick_ = it->a;
            if (dbl) {
                this->submit_text_field();
            }
            break;
        }
        case Hit::MenuScroll:
            break;
        }
        return true;
    }
    return true;
}

bool NhWindows::on_event(const SDL_Event &event, bool reserved_key)
{
    // 空白のキーで窓を閉じた直後に続く `SDL_TEXTINPUT` の空白は、コマンドとして漏らさない。
    if ((event.type == SDL_TEXTINPUT) && this->swallow_space_) {
        this->swallow_space_ = false;
        if ((event.text.text[0] == ' ') && (event.text.text[1] == '\0')) {
            return true;
        }
    }
    if (this->kind_ == Kind::None) {
        if (event.type == SDL_KEYDOWN) {
            this->swallow_space_ = false;
        }
        return false;
    }
    switch (event.type) {
    case SDL_TEXTEDITING:
        if (this->wants_text_input()) {
            this->field_.set_preedit(event.edit.text);
        }
        return true;
    case SDL_TEXTINPUT:
        this->on_text(event.text.text);
        return true;
    case SDL_KEYDOWN:
        if (reserved_key) {
            return false;
        }
        this->swallow_space_ = false;
        this->on_keydown(event.key);
        return true;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        return this->on_mouse(event);
    default:
        return false;
    }
}

} // namespace hd2d::nethack
