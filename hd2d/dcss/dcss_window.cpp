/*!
 * @file dcss_window.cpp
 * @brief `dcss_window.h` の実装。
 */
#include "dcss/dcss_window.h"

#include <algorithm>
#include <cstdlib>

namespace hd2d::dcss {

namespace {

using json = nlohmann::json;

std::int64_t jint(const json &o, const char *key, std::int64_t fallback = 0)
{
    const auto it = o.find(key);
    if ((it == o.end()) || it->is_null()) {
        return fallback;
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? 1 : 0;
    }
    if (it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    if (it->is_number_unsigned()) {
        return static_cast<std::int64_t>(it->get<std::uint64_t>());
    }
    if (it->is_number_float()) {
        return static_cast<std::int64_t>(it->get<double>());
    }
    return fallback;
}

std::string jstr(const json &o, const char *key)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

//! 不正な UTF-8 を置換文字に直す（nlohmann は不正な字で例外を投げる）。
std::string sanitize(const std::string &in)
{
    const std::vector<char32_t> chars = dcss_decode_utf8(in);
    std::string out;
    out.reserve(in.size());
    for (const char32_t c : chars) {
        out += dcss_encode_utf8(c);
    }
    return out;
}

std::vector<DcssTileRef> parse_tiles(const json &arr)
{
    std::vector<DcssTileRef> out;
    if (!arr.is_array()) {
        return out;
    }
    for (const json &t : arr) {
        if (!t.is_object()) {
            continue;
        }
        DcssTileRef r;
        r.t = static_cast<int>(jint(t, "t"));
        r.tex = static_cast<int>(jint(t, "tex"));
        r.ymax = static_cast<int>(jint(t, "ymax"));
        out.push_back(r);
    }
    return out;
}

DcssMenuItem parse_item(const json &it)
{
    DcssMenuItem item;
    item.placeholder = false;
    if (it.is_string()) {
        item.text = it.get<std::string>();
        return item;
    }
    if (!it.is_object()) {
        return item;
    }
    item.text = jstr(it, "text");
    item.quantity = static_cast<int>(jint(it, "q"));
    item.colour = it.contains("colour") ? static_cast<int>(jint(it, "colour", kLightGrey)) & 0x0F : kLightGrey;
    item.level = static_cast<int>(jint(it, "level", 2));
    if (it.contains("hotkeys") && it["hotkeys"].is_array()) {
        for (const json &h : it["hotkeys"]) {
            if (h.is_number()) {
                item.hotkeys.push_back(h.get<int>());
            }
        }
    }
    if (it.contains("tiles")) {
        item.tiles = parse_tiles(it["tiles"]);
    }
    return item;
}

void apply_title(DcssMenuData &menu, const json &m)
{
    const auto t = m.find("title");
    if ((t != m.end()) && t->is_object()) {
        menu.title = jstr(*t, "text");
    }
}

} // namespace

DcssStyledLine dcss_parse_window_text(std::string_view text, int default_colour, int base_colour)
{
    const int base = (base_colour < 0) ? default_colour : base_colour;
    DcssStyledLine out;
    std::vector<int> stack;
    int current = default_colour;
    const std::vector<char32_t> chars = dcss_decode_utf8(text);
    const auto name_to_colour = [](const std::string &name) {
        if (name == "h") {
            return static_cast<int>(kYellow);
        }
        if (name == "w") {
            return static_cast<int>(kWhite);
        }
        return dcss_colour_from_name(name, -1);
    };
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const char32_t c = chars[i];
        if (c != U'<') {
            out.push_back({ c, static_cast<std::uint8_t>(current) });
            continue;
        }
        if (((i + 1) >= chars.size())) {
            out.push_back({ c, static_cast<std::uint8_t>(current) });
            continue;
        }
        if (chars[i + 1] == U'<') {
            out.push_back({ U'<', static_cast<std::uint8_t>(current) });
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        std::string name;
        bool closing = false;
        if ((j < chars.size()) && (chars[j] == U'/')) {
            closing = true;
            ++j;
        }
        while ((j < chars.size()) && (chars[j] != U'>')) {
            name += dcss_encode_utf8(chars[j]);
            ++j;
        }
        if ((j >= chars.size()) || name.empty()) {
            out.push_back({ c, static_cast<std::uint8_t>(current) });
            continue;
        }
        if (name.compare(0, 3, "bg:") == 0) {
            i = j; // 背景色は使わない
            continue;
        }
        const int col = name_to_colour(name);
        if (closing) {
            if (!stack.empty() && ((col == stack.back()) || (col < 0))) {
                stack.pop_back();
            }
            current = stack.empty() ? base : stack.back();
        } else if (col >= 0) {
            stack.push_back(col);
            current = col;
        } else {
            // 読めないタグは原作どおり明るい赤で字として残す。
            const std::string literal = "<" + name + ">";
            for (const char32_t lc : dcss_decode_utf8(literal)) {
                out.push_back({ lc, static_cast<std::uint8_t>(kLightRed) });
            }
        }
        i = j;
    }
    return out;
}

std::string dcss_strip_window_text(std::string_view text)
{
    const DcssStyledLine line = dcss_parse_window_text(text, kLightGrey);
    std::string out;
    for (const DcssStyledChar &c : line) {
        out += dcss_encode_utf8(c.ch);
    }
    return out;
}

std::vector<DcssStyledLine> dcss_split_styled_lines(const DcssStyledLine &line)
{
    std::vector<DcssStyledLine> out(1);
    for (const DcssStyledChar &c : line) {
        if (c.ch == U'\n') {
            out.emplace_back();
        } else {
            out.back().push_back(c);
        }
    }
    return out;
}

bool DcssWindowStack::game_over() const
{
    for (const DcssWindow &w : this->windows_) {
        if ((w.kind == DcssWindow::Kind::Ui) && (w.type == "game-over")) {
            return true;
        }
    }
    return false;
}

DcssWindow *DcssWindowStack::top_menu()
{
    for (auto it = this->windows_.rbegin(); it != this->windows_.rend(); ++it) {
        if (it->kind == DcssWindow::Kind::Menu) {
            return &*it;
        }
    }
    return nullptr;
}

DcssWindow *DcssWindowStack::top_ui(const std::string &type)
{
    for (auto it = this->windows_.rbegin(); it != this->windows_.rend(); ++it) {
        if ((it->kind == DcssWindow::Kind::Ui) && (type.empty() || (it->type == type))) {
            return &*it;
        }
    }
    return nullptr;
}

namespace {

//! 書式のタグ（`<brown>`・`</w>` など）を除いた字。
std::string strip_format_tags(const std::string &text)
{
    std::string out;
    bool in_tag = false;
    for (const char ch : text) {
        if (ch == '<') {
            in_tag = true;
        } else if ((ch == '>') && in_tag) {
            in_tag = false;
        } else if (!in_tag) {
            out += ch;
        }
    }
    return out;
}

//! 作成の最後の窓の題（「ようこそ、名もなきノールの戦士。」「Welcome, nameless Gnoll Fighter.」）から「ノールの戦士」を取る。取れなければ空。
std::string job_from_newgame_title(const std::string &title)
{
    const auto cut = [&](const char *prefix, const char *suffix) -> std::string {
        const std::string pre = prefix;
        const std::string suf = suffix;
        const std::size_t a = title.find(pre);
        if (a == std::string::npos) {
            return std::string();
        }
        std::string rest = title.substr(a + pre.size());
        const std::size_t b = rest.rfind(suf);
        if (b != std::string::npos) {
            rest.erase(b);
        }
        return rest;
    };
    std::string job = cut("名もなき", "。");
    if (job.empty()) {
        job = cut("nameless ", ".");
    }
    return job;
}

} // namespace

void DcssWindowStack::set_pregame(bool pregame)
{
    this->pregame_ = pregame;
    if (!pregame && this->name_prompt_) {
        this->name_prompt_ = false; // ゲームの画面が来た（名前が決まった）
        this->name_random_ = false;
        this->input_ = DcssTextInput{};
    }
    if (!pregame) {
        this->name_resume_ = false;
        this->name_overwrite_ = false;
    }
}

void DcssWindowStack::on_name_check(const std::string &name, bool exists)
{
    const auto trim = [](const std::string &t) {
        const std::size_t b = t.find_first_not_of(' ');
        return (b == std::string::npos) ? std::string() : t.substr(b, t.find_last_not_of(' ') - b + 1);
    };
    // 返事が来たとき、まだ同じ名前の問いが出ているときだけ（ゲームが始まった・名前を打ち直した後の遅い返事は捨てる）
    if (this->name_prompt_ && exists && !this->name_overwrite_ && !this->name_random_ && (trim(name) == trim(this->input_.typed))) {
        this->name_overwrite_ = true;
        ++this->version_;
    }
}

void DcssWindowStack::clear_name_overwrite()
{
    if (this->name_overwrite_) {
        this->name_overwrite_ = false;
        ++this->version_;
    }
}

bool DcssWindowStack::ingest_line(const std::string &raw)
{
    if (raw.empty() || (raw[0] == '*')) {
        return false;
    }
    const json m = json::parse(sanitize(raw), nullptr, false);
    if (m.is_discarded() || !m.is_object()) {
        return false;
    }
    const std::string kind = jstr(m, "msg");
    const bool input_fresh = this->input_fresh_;
    this->input_fresh_ = false;
    if (this->name_prompt_ && ((kind == "menu") || (kind == "ui-push"))) {
        // ja5: 名前の問いの最中の十字（カーソルのキー）で、同じ名前のセーブの上書きを訊くはい/いいえのメニュー（`tag` = `prompt`）が出る。
        // それは作成の窓ではなく、閉じれば名前の問いへ戻る（コアの名前の入力欄はそのまま）。覚えておいて戻す。
        const bool question = (kind == "menu") && (jstr(m, "tag") == "prompt");
        if (question) {
            this->name_resume_ = true;
            this->name_saved_input_ = this->input_;
            this->name_saved_random_ = this->name_random_;
        }
        this->name_prompt_ = false; // 作成の窓へ戻った（名前の問いで Esc を押した）
        this->name_random_ = false;
        this->name_overwrite_ = false;
        this->input_ = DcssTextInput{};
    } else if (this->name_resume_ && ((kind == "ui-push") || ((kind == "menu") && (jstr(m, "tag") != "prompt")))) {
        this->name_resume_ = false; // 確認ではなく作成の窓へ戻った
    }
    if (kind == "menu") {
        this->on_menu(m);
    } else if (kind == "update_menu") {
        this->on_update_menu(m);
    } else if (kind == "update_menu_items") {
        this->on_update_menu_items(m);
    } else if (kind == "menu_scroll") {
        this->on_menu_scroll(m);
    } else if (kind == "close_menu") {
        this->pop_menu_family();
        if (this->name_resume_ && this->pregame_ && this->windows_.empty()) {
            // 上書きの確認が閉じた（いいえ）。名前の問いへ戻る
            this->name_resume_ = false;
            this->name_prompt_ = true;
            this->name_random_ = this->name_saved_random_;
            this->input_ = this->name_saved_input_;
        }
    } else if (kind == "close_all_menus") {
        const std::size_t before = this->windows_.size();
        this->windows_.erase(std::remove_if(this->windows_.begin(), this->windows_.end(),
                                 [](const DcssWindow &w) { return w.kind != DcssWindow::Kind::Ui; }),
            this->windows_.end());
        if (this->windows_.size() != before) {
            ++this->close_serial_;
        }
    } else if (kind == "ui-push") {
        this->on_ui_push(m);
    } else if (kind == "ui-pop") {
        this->pop_ui();
    } else if (kind == "ui-state") {
        this->on_ui_state(m);
    } else if (kind == "txt") {
        this->on_txt(m);
    } else if (kind == "init_input") {
        DcssTextInput in;
        in.active = true;
        in.prompt = jstr(m, "prompt");
        in.tag = jstr(m, "tag");
        in.prefill = jstr(m, "prefill");
        in.typed = in.prefill;
        in.replace_on_first_key = (jint(m, "select_prefill") != 0);
        in.maxlen = static_cast<int>(jint(m, "maxlen"));
        this->input_ = std::move(in);
        this->input_fresh_ = (jstr(m, "type") == "messages");
    } else if (kind == "close_input") {
        this->input_ = DcssTextInput{};
        if (input_fresh && this->pregame_ && this->windows_.empty() && !this->name_prompt_) {
            // 名前の問い（`name_prompt()` の注記）。字は窓の部品が打った分を覚える。
            this->name_prompt_ = true;
            this->name_random_ = false;
            this->name_job_ = job_from_newgame_title(this->last_newgame_title_);
            this->input_.active = true;
            this->input_.maxlen = 30; // `MAX_NAME_LENGTH`
        }
    } else {
        return false;
    }
    ++this->version_;
    return true;
}

void DcssWindowStack::on_menu(const json &m)
{
    DcssWindow w;
    w.generation = ++this->generation_;
    w.centred = (jint(m, "ui-centred") != 0);
    if (jstr(m, "type") == "crt") {
        w.kind = DcssWindow::Kind::Crt;
        for (std::string &l : this->crt_lines_) {
            l.clear();
        }
        this->windows_.push_back(std::move(w));
        return;
    }
    w.kind = DcssWindow::Kind::Menu;
    DcssMenuData &menu = w.menu;
    menu.tag = jstr(m, "tag");
    menu.flags = static_cast<int>(jint(m, "flags"));
    apply_title(menu, m);
    menu.more = jstr(m, "more");
    menu.alt_more = jstr(m, "alt_more");
    menu.last_hovered = static_cast<int>(jint(m, "last_hovered", -1));
    menu.jump_to = static_cast<int>(jint(m, "jump_to"));
    menu.total_items = static_cast<int>(jint(m, "total_items"));
    menu.replace = (jint(m, "replace") != 0);
    const int chunk_start = static_cast<int>(jint(m, "chunk_start"));
    const auto items = m.find("items");
    const int given = ((items != m.end()) && items->is_array()) ? static_cast<int>(items->size()) : 0;
    menu.total_items = std::max(menu.total_items, chunk_start + given);
    menu.items.assign(static_cast<std::size_t>(menu.total_items), DcssMenuItem{});
    for (int i = 0; i < given; ++i) {
        menu.items[static_cast<std::size_t>(chunk_start + i)] = parse_item((*items)[static_cast<std::size_t>(i)]);
    }
    if (this->keyboard_item_ && (menu.tag == "game_menu") && (chunk_start == 0) && (given == menu.total_items)) {
        // 「/ 情報を調べる」の次に足す（原作の Android 版の並び）。
        for (std::size_t i = 0; i < menu.items.size(); ++i) {
            const std::vector<int> &keys = menu.items[i].hotkeys;
            if (std::find(keys.begin(), keys.end(), static_cast<int>('/')) != keys.end()) {
                DcssMenuItem extra;
                extra.placeholder = false;
                extra.text = " F12 - " + this->keyboard_label_;
                extra.hotkeys = { kToggleKeyboardKey };
                extra.level = 2;
                extra.tiles.push_back(DcssTileRef{ 9550, 5, 0 }); // TILEG_CMD_KEYBOARD
                menu.items.insert(menu.items.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(extra));
                menu.total_items = static_cast<int>(menu.items.size());
                break;
            }
        }
    }
    if (menu.replace) {
        if (DcssWindow *top = this->top_menu()) {
            const std::size_t index = static_cast<std::size_t>(top - this->windows_.data());
            this->windows_.erase(this->windows_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    this->windows_.push_back(std::move(w));
}

void DcssWindowStack::on_update_menu(const json &m)
{
    DcssWindow *w = this->top_menu();
    if (w == nullptr) {
        return;
    }
    DcssMenuData &menu = w->menu;
    if (m.contains("last_hovered")) {
        menu.last_hovered = static_cast<int>(jint(m, "last_hovered", -1));
    }
    if (m.contains("title")) {
        apply_title(menu, m);
    }
    if (m.contains("more")) {
        menu.more = jstr(m, "more");
    }
    if (m.contains("alt_more")) {
        menu.alt_more = jstr(m, "alt_more");
    }
    if (m.contains("total_items")) {
        menu.total_items = static_cast<int>(jint(m, "total_items"));
        menu.items.resize(static_cast<std::size_t>(std::max(0, menu.total_items)));
    }
}

void DcssWindowStack::on_update_menu_items(const json &m)
{
    DcssWindow *w = this->top_menu();
    if (w == nullptr) {
        return;
    }
    DcssMenuData &menu = w->menu;
    const int start = static_cast<int>(std::max<std::int64_t>(0, jint(m, "chunk_start")));
    const auto items = m.find("items");
    if ((items == m.end()) || !items->is_array()) {
        return;
    }
    for (std::size_t i = 0; i < items->size(); ++i) {
        const std::size_t at = static_cast<std::size_t>(start) + i;
        if (at >= menu.items.size()) {
            menu.items.resize(at + 1);
            menu.total_items = static_cast<int>(menu.items.size());
        }
        menu.items[at] = parse_item((*items)[i]);
    }
}

void DcssWindowStack::on_menu_scroll(const json &m)
{
    DcssWindow *w = this->top_menu();
    if ((w == nullptr) || (jint(m, "force") == 0)) {
        return;
    }
    w->menu.jump_to = static_cast<int>(jint(m, "first"));
    w->first_visible = w->menu.jump_to;
    w->scroll_y = -1; // 描画側が項目の位置から決め直す
    if (m.contains("last_hovered")) {
        w->menu.last_hovered = static_cast<int>(jint(m, "last_hovered", -1));
    }
}

void DcssWindowStack::on_ui_push(const json &m)
{
    DcssWindow w;
    w.kind = DcssWindow::Kind::Ui;
    w.type = jstr(m, "type");
    w.data = m;
    w.state = json::object();
    w.generation = ++this->generation_;
    w.centred = (jint(m, "ui-centred") != 0);
    if ((w.type == "newgame-choice") || (w.type == "newgame-random-combo")) {
        this->creation_started_ = true;
        if ((w.type == "newgame-choice") && m.contains("title") && m["title"].is_string()) {
            this->last_newgame_title_ = strip_format_tags(m["title"].get<std::string>());
        }
    }
    this->windows_.push_back(std::move(w));
}

void DcssWindowStack::on_ui_state(const json &m)
{
    DcssWindow *w = this->top_ui(jstr(m, "type"));
    if (w == nullptr) {
        return;
    }
    for (auto it = m.begin(); it != m.end(); ++it) {
        if ((it.key() == "msg") || (it.key() == "type")) {
            continue;
        }
        w->state[it.key()] = it.value();
    }
    if (w->type == "newgame-choice" && m.contains("button_focus")) {
        w->focus_hotkey = static_cast<int>(jint(m, "button_focus"));
    }
    if (m.contains("pane")) {
        w->pane = static_cast<int>(jint(m, "pane"));
    }
}

void DcssWindowStack::on_txt(const json &m)
{
    const std::string id = jstr(m, "id");
    if (!id.empty() && (id != "menu_txt")) {
        return;
    }
    if (jint(m, "clear") != 0) {
        for (std::string &l : this->crt_lines_) {
            l.clear();
        }
    }
    const auto lines = m.find("lines");
    if ((lines == m.end()) || !lines->is_object()) {
        return;
    }
    for (auto it = lines->begin(); it != lines->end(); ++it) {
        const int row = std::atoi(it.key().c_str());
        if ((row >= 0) && (row < static_cast<int>(this->crt_lines_.size())) && it.value().is_string()) {
            this->crt_lines_[static_cast<std::size_t>(row)] = it.value().get<std::string>();
        }
    }
}

void DcssWindowStack::pop_menu_family()
{
    for (auto it = this->windows_.rbegin(); it != this->windows_.rend(); ++it) {
        if (it->kind != DcssWindow::Kind::Ui) {
            this->windows_.erase(std::next(it).base());
            ++this->close_serial_;
            return;
        }
    }
}

void DcssWindowStack::pop_ui()
{
    for (auto it = this->windows_.rbegin(); it != this->windows_.rend(); ++it) {
        if (it->kind == DcssWindow::Kind::Ui) {
            this->windows_.erase(std::next(it).base());
            ++this->close_serial_;
            return;
        }
    }
}

bool dcss_parse_startup(const nlohmann::json &m, DcssStartupData &out)
{
    using json = nlohmann::json;
    if (!m.is_object() || !m.contains("modes") || !m["modes"].is_array() || !m.contains("saves") || !m["saves"].is_array()) {
        return false;
    }
    const auto str = [](const json &o, const char *k) { return (o.contains(k) && o[k].is_string()) ? o[k].get<std::string>() : std::string(); };
    DcssStartupData d;
    if (m.contains("prompts") && m["prompts"].is_object()) {
        d.p_name = str(m["prompts"], "name");
        d.p_modes = str(m["prompts"], "modes");
        d.p_saves = str(m["prompts"], "saves");
        d.p_new_game = str(m["prompts"], "new_game");
    }
    for (const json &e : m["modes"]) {
        DcssStartupMode mode;
        mode.id = e.value("id", 0);
        mode.label = str(e, "label");
        mode.desc = str(e, "desc");
        mode.tile = e.value("tile", 0);
        d.modes.push_back(std::move(mode));
    }
    for (const json &e : m["saves"]) {
        DcssStartupSave save;
        save.name = str(e, "name");
        save.label = str(e, "label");
        save.wiz = e.value("wiz", false);
        save.loadable = e.value("loadable", true);
        if (e.contains("doll") && e["doll"].is_array()) {
            for (const json &part : e["doll"]) {
                if (part.is_array() && (part.size() >= 2) && part[0].is_number() && part[1].is_number()) {
                    save.doll.emplace_back(part[0].get<int>(), part[1].get<int>());
                }
            }
        }
        d.saves.push_back(std::move(save));
    }
    d.valid = !d.modes.empty();
    out = std::move(d);
    return out.valid;
}

} // namespace hd2d::dcss
