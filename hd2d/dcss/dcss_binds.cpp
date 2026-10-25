/*!
 * @file dcss_binds.cpp
 * @brief DCSS の「操作の割り当て」（`dcss_binds.h`）の実装。D33。
 */
#include "dcss/dcss_binds.h"

#include "ui/key_binds.h"

#include <algorithm>

namespace hd2d::dcss {

namespace {

//! 値の欄の「割り当てなし」（`feature_menu.cpp` の `kUnassigned` と同じ字）。
const char *const kNone = "—";

} // namespace

int DcssBinds::ui_function(int action)
{
    switch (action) {
    case kActionFeatureMenu:
        return kPadCmdHd2dMenu;
    case kActionTurnLeft:
        return kPadCmdTurnLeft;
    case kActionTurnRight:
        return kPadCmdTurnRight;
    default:
        return 0;
    }
}

void DcssBinds::ingest_keymap(const nlohmann::json &m)
{
    const auto maps = m.find("maps");
    if ((maps == m.end()) || !maps->is_array()) {
        return;
    }
    std::vector<KeyEntry> next;
    for (const nlohmann::json &e : *maps) {
        if (!e.is_array() || (e.size() < 3) || !e[0].is_number_integer() || !e[1].is_string() || !e[2].is_number_integer()) {
            continue;
        }
        KeyEntry k;
        k.key = e[0].get<int>();
        k.name = e[1].get<std::string>();
        k.cmd = e[2].get<int>();
        next.push_back(std::move(k));
    }
    this->keymaps_ = std::move(next);
}

std::vector<std::string> DcssBinds::take_outgoing()
{
    std::vector<std::string> out;
    out.swap(this->outgoing_);
    return out;
}

void DcssBinds::on_open()
{
    this->outgoing_.push_back("{\"msg\":\"hd2d_keymap\",\"op\":\"list\"}");
}

std::vector<ExternalFixedRow> DcssBinds::fixed_rows() const
{
    return {
        { this->tr("選ぶ・進む", "Choose / move"), this->tr("矢印・テンキー", "Arrows / numpad"), this->tr("十字 / 左スティック", "D-pad / left stick") },
        { this->tr("決定", "Confirm"), "Enter", "A" },
        { this->tr("取消", "Cancel"), "ESC", "B" },
        { this->tr("左クリック（疑似カーソル）", "Left click (pointer)"), kNone, "L3" },
        { this->tr("右クリック（疑似カーソル）", "Right click (pointer)"), kNone, "R3" },
        { this->tr("疑似カーソルを動かす", "Move the pointer"), kNone, this->tr("右スティック", "Right stick") },
    };
}

bool DcssBinds::hides_ui_action(int action) const
{
    //! コマンドメニューは無い（D32）。サブパネルの開閉も、サブパネルがこのコアの画面に無いので出さない（D33）。
    return (action == kActionCommandMenu) || (action == kActionSubsToggle);
}

std::vector<ExternalBindRow> DcssBinds::rows() const
{
    std::vector<ExternalBindRow> out;
    auto heading = [&out](const std::string &text) {
        ExternalBindRow r;
        r.label = text;
        out.push_back(std::move(r));
    };
    heading(this->tr("― 原作の割り当ての画面", "― The original's binding screens"));
    {
        ExternalBindRow r;
        r.id = kRowOpenPadScreen;
        r.label = this->tr("ゲームパッドの割り当ての画面を開く", "Open the gamepad bindings screen");
        r.open_row = true;
        out.push_back(r);
        ExternalBindRow k;
        k.id = kRowOpenKeymap;
        k.label = this->tr("キーマップ・マクロの画面を開く（原作の ~）", "Open the keymaps and macros screen (the original's ~)");
        k.open_row = true;
        out.push_back(k);
    }
    if (!this->pad_.table_ready()) {
        heading(this->tr("― ゲームのコマンドの一覧は、ゲームが始まると届きます", "― The command list arrives once the game has started"));
        return out;
    }
    for (const DcssPadGroup &g : this->pad_.groups()) {
        std::vector<ExternalBindRow> in_group;
        for (const int id : g.ids) {
            if ((id < 0) && (id != kPadCmdBindings)) {
                continue; //!< 機能メニュー・視点を回すは「この画面まわり」の行にある
            }
            ExternalBindRow r;
            r.id = id;
            r.label = this->pad_.label(id);
            r.no_key = (id < 0); //!< 画面の機能は原作のキーを持たない
            in_group.push_back(std::move(r));
        }
        if (in_group.empty()) {
            continue;
        }
        heading("― " + g.name);
        out.insert(out.end(), in_group.begin(), in_group.end());
    }
    return out;
}

std::string DcssBinds::slots_text(int cmd) const
{
    std::string text;
    int shown = 0;
    int total = 0;
    for (int slot = 0; slot < kPadSlotCount; ++slot) {
        if (this->pad_.command(slot) != cmd) {
            continue;
        }
        const std::string name = dcss_pad_slot_name(slot);
        if (name.empty()) {
            continue;
        }
        ++total;
        if (shown < 2) {
            text += (shown == 0 ? "" : ", ") + name;
            ++shown;
        }
    }
    if (total > shown) {
        text += " +" + std::to_string(total - shown);
    }
    return text.empty() ? std::string(kNone) : text;
}

std::string DcssBinds::cell(int id, int col) const
{
    if ((id == kRowOpenPadScreen) || (id == kRowOpenKeymap)) {
        return (col == 0) ? this->tr("決定で開く", "Enter to open") : std::string();
    }
    if (col == 1) {
        return this->slots_text(id);
    }
    if (id < 0) {
        return std::string();
    }
    std::string text;
    for (const DcssPadCommand &c : this->pad_.commands()) {
        if ((c.id == id) && !c.key.empty()) {
            text = c.key;
            break;
        }
    }
    for (const KeyEntry &k : this->keymaps_) {
        if (k.cmd == id) {
            text += (text.empty() ? "" : ", ") + k.name;
        }
    }
    return text.empty() ? std::string(kNone) : text;
}

bool DcssBinds::ui_has_pad(int action) const
{
    return ui_function(action) != 0;
}

std::string DcssBinds::ui_pad_cell(int action) const
{
    const int fn = ui_function(action);
    return (fn == 0) ? std::string() : this->slots_text(fn);
}

std::string DcssBinds::assign_key(int id, int key)
{
    if ((id <= 0) || (key <= 0x20) || (key > 0x10ffff)) {
        return this->tr("このキーは割り当てられません", "This key cannot be bound");
    }
    for (const DcssPadCommand &c : this->pad_.commands()) {
        if ((c.id == id) && (c.key.size() == 1) && (static_cast<unsigned char>(c.key[0]) == key)) {
            return this->tr("すでにこのコマンドの既定のキーです", "That is already this command's default key");
        }
    }
    std::string name(1, static_cast<char>(key));
    if (key < 0x20) {
        name = std::string("^") + static_cast<char>('A' + key - 1);
    }
    this->keymaps_.erase(std::remove_if(this->keymaps_.begin(), this->keymaps_.end(), [key](const KeyEntry &k) { return k.key == key; }), this->keymaps_.end());
    this->keymaps_.push_back({ key, name, id });
    this->outgoing_.push_back("{\"msg\":\"hd2d_keymap\",\"op\":\"set\",\"key\":" + std::to_string(key) + ",\"cmd\":" + std::to_string(id) + "}");
    return std::string();
}

std::string DcssBinds::assign_slot(int cmd, PadInput input, int mods)
{
    const int slot = dcss_pad_slot(input, mods);
    if (!dcss_pad_slot_valid(input, mods)) {
        return this->tr("は割り当てられません（LB・RB は押しながらの層でだけ使えます）", " cannot be bound (LB and RB only work as the held layer)");
    }
    if (dcss_pad_slot_fixed(input, mods)) {
        return this->tr("は単独では変えられません（A = 決定・B = 取消・L3・R3 = クリック）", " cannot be changed on its own (A = confirm, B = cancel, L3/R3 = click)");
    }
    if (!this->pad_.set_command(slot, cmd)) {
        return this->tr("は割り当てられません", " cannot be bound");
    }
    return std::string();
}

std::string DcssBinds::assign_pad(int id, PadInput input, int mods)
{
    return this->assign_slot(id, input, mods);
}

std::string DcssBinds::assign_ui_pad(int action, PadInput input, int mods)
{
    const int fn = ui_function(action);
    if (fn == 0) {
        return this->tr("は割り当てられません", " cannot be bound");
    }
    return this->assign_slot(fn, input, mods);
}

void DcssBinds::reset_pad(int cmd)
{
    //! 遊ぶ人が変えた枠のうち、このコマンドに関わる物（このコマンドを割り当てた枠・既定でこのコマンドだった枠）を既定へ戻す。
    for (int slot = 0; slot < kPadSlotCount; ++slot) {
        if (this->pad_.overridden(slot) && ((this->pad_.command(slot) == cmd) || (this->pad_.default_command(slot) == cmd))) {
            (void)this->pad_.set_command(slot, kPadUseDefault);
        }
    }
}

void DcssBinds::clear(int id, int col)
{
    if (col == 1) {
        this->reset_pad(id);
        return;
    }
    //! 足したキーを、割り当てる前の動きへ戻す（コアが `K:X → X` にする）。
    std::vector<int> keys;
    for (const KeyEntry &k : this->keymaps_) {
        if (k.cmd == id) {
            keys.push_back(k.key);
        }
    }
    for (const int key : keys) {
        this->outgoing_.push_back("{\"msg\":\"hd2d_keymap\",\"op\":\"clear\",\"key\":" + std::to_string(key) + "}");
    }
    this->keymaps_.erase(std::remove_if(this->keymaps_.begin(), this->keymaps_.end(), [id](const KeyEntry &k) { return k.cmd == id; }), this->keymaps_.end());
}

void DcssBinds::clear_ui_pad(int action)
{
    const int fn = ui_function(action);
    if (fn != 0) {
        this->reset_pad(fn);
    }
}

bool DcssBinds::open_screen(int id)
{
    if (id == kRowOpenPadScreen) {
        return this->open_pad_screen_ && this->open_pad_screen_();
    }
    if (id == kRowOpenKeymap) {
        //! 原作の `~`（`CMD_MACRO_MENU`）。キーマップを通らないコマンドのキーで送る（割り当てを変えた人にも同じ画面が出る）。
        const int cmd = this->pad_.id_for_name("CMD_MACRO_MENU");
        this->outgoing_.push_back("{\"msg\":\"key\",\"keycode\":" + (cmd > 0 ? std::to_string(-cmd) : std::string("126")) + "}");
        return true;
    }
    return false;
}

} // namespace hd2d::dcss
