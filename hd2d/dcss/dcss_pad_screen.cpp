/*!
 * @file dcss_pad_screen.cpp
 * @brief `dcss_pad_screen.h` の実装。
 */
#include "dcss/dcss_pad_screen.h"

#include <algorithm>

namespace hd2d::dcss {

namespace {

constexpr const char *kTagBindings = "hd2d_pad_bindings";
constexpr const char *kTagGroups = "hd2d_pad_groups";
constexpr const char *kTagCommands = "hd2d_pad_commands";
constexpr const char *kTagConfirm = "hd2d_pad_confirm";

//! 原作 `Menu(MF_SINGLESELECT | MF_ALLOW_FORMATTING | MF_ARROWS_SELECT | MF_WRAP)`。
constexpr int kMenuFlags = kMfSingleSelect | kMfAllowFormatting | kMfArrowsSelect | kMfWrap;

nlohmann::json item_row(const std::string &text, int hotkey, bool yellow, bool indent = false)
{
    nlohmann::json it;
    it["text"] = indent ? ("     " + text) : text; // 原作の `pad_choice_entry` は行の頭に空白 5 つ
    it["colour"] = yellow ? static_cast<int>(kYellow) : static_cast<int>(kLightGrey);
    it["level"] = 2;
    it["hotkeys"] = nlohmann::json::array({ hotkey });
    return it;
}

nlohmann::json subtitle_row(const std::string &text)
{
    nlohmann::json it;
    it["text"] = text;
    it["colour"] = static_cast<int>(kBlue); // 原作の見出し（MEL_SUBTITLE）の青
    it["level"] = 1;
    return it;
}

//! 左を `width` 桁にそろえる（字は等幅。枠の綴りはすべて半角）。
std::string pad_right(const std::string &s, std::size_t width)
{
    return (s.size() >= width) ? s : (s + std::string(width - s.size(), ' '));
}

} // namespace

DcssPadScreen::DcssPadScreen(DcssPad &pad, DcssWindows &windows)
    : pad_(pad)
    , windows_(windows)
{
}

//! 書式つきの字へ入れるため `<` を逃がす（原作 `escape_tags`）。
std::string DcssPadScreen::esc(const std::string &s) const
{
    std::string out;
    for (const char c : s) {
        out += c;
        if (c == '<') {
            out += '<';
        }
    }
    return out;
}

bool DcssPadScreen::top_is_ours() const
{
    if (this->levels_.empty()) {
        return false;
    }
    const DcssWindow *top = this->windows_.stack().top();
    return (top != nullptr) && (top->kind == DcssWindow::Kind::Menu) && (top->menu.tag.rfind("hd2d_pad", 0) == 0);
}

bool DcssPadScreen::active() const
{
    return this->top_is_ours();
}

bool DcssPadScreen::capturing() const
{
    return this->top_is_ours() && (this->levels_.size() == 1) && (this->levels_[0].kind == Kind::Bindings);
}

void DcssPadScreen::inject_menu(const std::string &tag, const std::string &title, const std::string &more, const nlohmann::json &items, int hover, bool replace)
{
    nlohmann::json m;
    m["msg"] = "menu";
    m["tag"] = tag;
    m["flags"] = kMenuFlags;
    m["title"] = nlohmann::json{ { "text", title } };
    m["more"] = more;
    m["alt_more"] = more;
    m["last_hovered"] = hover;
    m["total_items"] = static_cast<int>(items.size());
    m["items"] = items;
    if (replace) {
        m["replace"] = 1;
    }
    (void)this->windows_.stack().ingest_line(m.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

bool DcssPadScreen::open()
{
    if (!this->pad_.table_ready() || this->top_is_ours()) {
        return false;
    }
    this->levels_.clear();
    this->pending_keys_.clear();
    this->status_.clear();
    this->push_level(Kind::Bindings, -1, -1, -1);
    return true;
}

void DcssPadScreen::push_level(Kind kind, int slot, int group, int hover_slot)
{
    Level level;
    level.kind = kind;
    level.slot = slot;
    level.group = group;
    nlohmann::json items = nlohmann::json::array();
    int hover = -1;
    std::string title;
    std::string more;
    const char *tag = kTagBindings;
    const std::string help_choose = this->tr("[<w>上</w>/<w>下</w>] で選ぶ、[<w>A</w>/<w>Enter</w>] で", "[<w>Up</w>/<w>Down</w>] choose, [<w>A</w>/<w>Enter</w>] ");
    const auto add_item = [&](const nlohmann::json &it, RowAction action) {
        items.push_back(it);
        level.rows.push_back(action);
    };

    if (kind == Kind::Bindings) {
        tag = kTagBindings;
        title = this->tr("ゲームパッドの割り当て", "Gamepad bindings");
        for (int mods = 0; mods < kPadLayerCount; ++mods) {
            add_item(subtitle_row(this->esc(this->pad_.layer_name(mods))), RowAction{});
            for (int i = 0; i < kPadInputCount; ++i) {
                const PadInput b = static_cast<PadInput>(i);
                if (!dcss_pad_slot_valid(b, mods) || dcss_pad_slot_fixed(b, mods)) {
                    continue; // 単独押しの A・B・L3・R3 は固定（下の案内に書く）
                }
                const int row_slot = dcss_pad_slot(b, mods);
                const int cmd = this->pad_.command(row_slot);
                const std::string what = (cmd == 0) ? this->tr("(割り当てなし: マクロのキー)", "(no command: macro key)") : this->pad_.label_with_key(cmd);
                add_item(item_row(this->esc("Pad-" + dcss_pad_slot_name(row_slot) + " - " + what), kPadKeyPadBase - row_slot, this->pad_.overridden(row_slot)),
                    RowAction{ Row::OpenSlot, row_slot });
                if (row_slot == hover_slot) {
                    hover = static_cast<int>(items.size()) - 1;
                }
            }
        }
        add_item(subtitle_row(" "), RowAction{});
        add_item(item_row(this->tr("この画面での変更をすべて取り消す", "Undo all changes made in this screen"), kPadKeyPadBase - 100, false, true), RowAction{ Row::UndoAll, 0 });
        more = this->status_.empty() ? std::string() : (this->esc(this->status_) + "\n");
        more += this->tr("[<w>上</w>/<w>下</w>] で選ぶ、[<w>A</w>/<w>Enter</w>] で変える、[<w>B</w>/<w>Esc</w>] で閉じる。パッドのボタンや同時押しを押しても、その枠を変えられる。",
            "[<w>Up</w>/<w>Down</w>] choose, [<w>A</w>/<w>Enter</w>] change, [<w>B</w>/<w>Esc</w>] close. Press a gamepad button or chord to change it.");
        more += "\n";
        more += this->tr("A(決定)、B(取消)、L3(左クリック)、R3(右クリック)の単独押しは固定。右スティックでマウスのカーソルが動く。",
            "A (confirm), B (cancel), L3 (left click) and R3 (right click) alone are fixed; the right stick moves the mouse cursor.");
        more += "\n";
        more += this->tr("<yellow>黄色</yellow>の行はこの画面で変えたもので、設定 dcss_pad_binds(hd2d-DcssCore.cfg)に保存される。",
            "<yellow>Yellow</yellow> rows were changed here: they are saved to dcss_pad_binds in the settings file (hd2d-DcssCore.cfg).");
    } else if (kind == Kind::Groups) {
        tag = kTagGroups;
        const int current = this->pad_.command(slot);
        title = this->tr("%s に割り当てるコマンド(今: %s)", "Command for %s (now: %s)");
        const std::string slot_name = dcss_pad_slot_name(slot);
        const std::string now = (current == 0) ? this->tr("(割り当てなし: マクロのキー)", "(no command: macro key)") : this->pad_.label_with_key(current);
        title.replace(title.find("%s"), 2, slot_name);
        title.replace(title.find("%s"), 2, this->esc(now));
        add_item(item_row(this->esc(this->pad_.japanese() ? ("割り当てなし(マクロのキー Pad-" + slot_name + " を出す)")
                                                    : ("No command (send the macro key Pad-" + slot_name + ")")),
                     1000, current == 0, true),
            RowAction{ Row::SetNone, 0 });
        if (current == 0) {
            hover = 0;
        }
        const int def = this->pad_.default_command(slot);
        const std::string def_text = (def == 0) ? this->tr("(割り当てなし: マクロのキー)", "(no command: macro key)") : this->pad_.label_with_key(def);
        add_item(item_row(this->esc(this->pad_.japanese() ? ("元に戻す(既定のまま: " + def_text + ")") : ("Reset (use the default: " + def_text + ")")), 1001, false, true),
            RowAction{ Row::SetDefault, 0 });
        add_item(subtitle_row(this->tr("コマンドの分類", "Command categories")), RowAction{});
        const std::vector<DcssPadGroup> groups = this->pad_.groups();
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            const DcssPadGroup &g = groups[gi];
            const bool has_current = std::find(g.ids.begin(), g.ids.end(), current) != g.ids.end();
            add_item(item_row(this->esc(g.name + " (" + std::to_string(g.ids.size()) + ")"), 1100 + static_cast<int>(gi), has_current, true),
                RowAction{ Row::OpenGroup, static_cast<int>(gi) });
            if (has_current) {
                hover = static_cast<int>(items.size()) - 1;
            }
        }
        more = this->tr("[<w>上</w>/<w>下</w>] で選ぶ、[<w>A</w>/<w>Enter</w>] で開く・割り当てる、[<w>B</w>/<w>Esc</w>] で戻る",
            "[<w>Up</w>/<w>Down</w>] choose, [<w>A</w>/<w>Enter</w>] open or assign, [<w>B</w>/<w>Esc</w>] go back");
    } else if (kind == Kind::Commands) {
        tag = kTagCommands;
        const int current = this->pad_.command(slot);
        const std::vector<DcssPadGroup> groups = this->pad_.groups();
        const DcssPadGroup empty;
        const DcssPadGroup &g = (group >= 0 && group < static_cast<int>(groups.size())) ? groups[static_cast<std::size_t>(group)] : empty;
        title = this->tr("%s に割り当てるコマンド: %s", "Command for %s: %s");
        title.replace(title.find("%s"), 2, dcss_pad_slot_name(slot));
        title.replace(title.find("%s"), 2, this->esc(g.name));
        for (std::size_t i = 0; i < g.ids.size(); ++i) {
            const int id = g.ids[i];
            add_item(item_row(this->esc(this->pad_.label_with_key(id)), 2000 + static_cast<int>(i), id == current, true), RowAction{ Row::SetCommand, id });
            if (id == current) {
                hover = static_cast<int>(i);
            }
        }
        more = help_choose + this->tr("割り当てる、[<w>B</w>/<w>Esc</w>] で戻る", "assign, [<w>B</w>/<w>Esc</w>] go back");
    } else {
        tag = kTagConfirm;
        title = this->tr("この画面での変更をすべて取り消しますか?", "Really undo all changes made in this screen?");
        add_item(item_row(this->tr("はい", "Yes"), 3000, false), RowAction{ Row::Yes, 0 });
        add_item(item_row(this->tr("いいえ", "No"), 3001, false), RowAction{ Row::No, 0 });
        hover = 1;
        more = help_choose + this->tr("答える", "answer");
    }
    if (hover < 0) {
        for (std::size_t i = 0; i < level.rows.size(); ++i) {
            if (level.rows[i].kind != Row::None) {
                hover = static_cast<int>(i);
                break;
            }
        }
    }
    this->levels_.push_back(level);
    this->inject_menu(tag, title, more, items, hover, false);
}

//! 1 枚目を作り直す（変えた枠の表示・黄色を反映する）。いちばん上の 1 枚目を置き換える。
void DcssPadScreen::refill_bindings(int hover_slot)
{
    if (this->levels_.empty() || (this->levels_.back().kind != Kind::Bindings)) {
        return;
    }
    this->levels_.pop_back();
    // 置き換えは「いちばん上のメニューを消して積む」ので、1 枚目をいったん閉じてから積み直す。
    (void)this->windows_.stack().ingest_line("{\"msg\":\"close_menu\"}");
    this->push_level(Kind::Bindings, -1, -1, hover_slot);
}

void DcssPadScreen::pop_level()
{
    if (this->levels_.empty()) {
        return;
    }
    (void)this->windows_.stack().ingest_line("{\"msg\":\"close_menu\"}");
    this->levels_.pop_back();
}

void DcssPadScreen::on_slot(int slot)
{
    if (!this->capturing()) {
        return;
    }
    const PadInput b = dcss_pad_slot_button(slot);
    const int mods = dcss_pad_slot_mods(slot);
    if (!dcss_pad_slot_valid(b, mods) || dcss_pad_slot_fixed(b, mods)) {
        return;
    }
    this->push_level(Kind::Groups, slot, -1, -1);
}

void DcssPadScreen::on_line(const std::string &line)
{
    const nlohmann::json m = nlohmann::json::parse(line, nullptr, false);
    if (m.is_discarded() || !m.is_object() || (m.value("msg", std::string()) != "key")) {
        return;
    }
    this->pending_keys_.push_back(m.value("keycode", 0));
}

void DcssPadScreen::activate(int row)
{
    if (this->levels_.empty()) {
        return;
    }
    const Level level = this->levels_.back();
    if ((row < 0) || (row >= static_cast<int>(level.rows.size()))) {
        return;
    }
    const RowAction action = level.rows[static_cast<std::size_t>(row)];
    switch (action.kind) {
    case Row::OpenSlot:
        this->push_level(Kind::Groups, action.value, -1, -1);
        break;
    case Row::UndoAll:
        if (this->pad_.has_overrides()) {
            this->push_level(Kind::Confirm, -1, -1, -1);
        }
        break;
    case Row::Yes:
        this->pad_.clear_overrides();
        this->status_ = this->tr("この画面での変更をすべて取り消した。", "All changes made in this screen were undone.");
        this->pop_level(); // 確認
        this->refill_bindings(-1);
        break;
    case Row::No:
        this->pop_level();
        break;
    case Row::SetNone:
    case Row::SetDefault:
    case Row::SetCommand: {
        const int slot = level.slot;
        const int value = (action.kind == Row::SetNone) ? 0 : ((action.kind == Row::SetDefault) ? kPadUseDefault : action.value);
        (void)this->pad_.set_command(slot, value);
        const int now = this->pad_.command(slot);
        this->status_ = dcss_pad_slot_name(slot) + ": " + ((now == 0) ? this->tr("(割り当てなし: マクロのキー)", "(no command: macro key)") : this->pad_.label_with_key(now));
        if (action.kind == Row::SetCommand) {
            this->pop_level(); // 分類の中のコマンド
        }
        this->pop_level(); // コマンドの分類
        this->refill_bindings(slot);
        break;
    }
    case Row::OpenGroup:
        this->push_level(Kind::Commands, level.slot, action.value, -1);
        break;
    case Row::None:
        break;
    }
}

void DcssPadScreen::service()
{
    if (this->pending_keys_.empty()) {
        return;
    }
    std::vector<int> keys;
    keys.swap(this->pending_keys_);
    for (const int key : keys) {
        if (!this->top_is_ours()) {
            this->levels_.clear(); // コアが窓を閉じた・別の窓が積まれた
            return;
        }
        if (key == 27) {
            this->pop_level();
        } else if (key == 13) {
            const DcssWindow *top = this->windows_.stack().top();
            this->activate((top != nullptr) ? top->menu.last_hovered : -1);
        }
    }
}

} // namespace hd2d::dcss
