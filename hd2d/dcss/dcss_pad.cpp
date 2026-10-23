/*!
 * @file dcss_pad.cpp
 * @brief DCSS のゲームパッド（段 5）。原作の日本語版のタイル版の割り当てと動きを画面の側で再現する。
 * @details 設計は `dcss_pad.h` と。手本は原作の `gamepad.cc`・`gamepad-menu.cc`・`windowmanager-sdl.cc`。
 */
#include "dcss/dcss_pad.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace hd2d::dcss {

namespace {

struct DcssPadGroupName {
    const char *en;
    const char *ja;
};
struct DcssPadCmdName {
    const char *name;
    int group;
    const char *en;
    const char *ja;
};
struct DcssPadLayerName {
    const char *en;
    const char *ja;
};
#include "dcss/dcss_pad_cmds.inc"

//! 枠の綴りの中のボタンの名前（原作 `pad_button_name`）。`PadInput` の並びと同じ。
constexpr const char *kButtonNames[kPadInputCount] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "L3", "R3" };

//! 画面の機能の名前（保存の綴りと表示名）。
struct Hd2dFunction {
    int id;
    const char *save;
    const char *ja;
    const char *en;
};
constexpr Hd2dFunction kHd2dFunctions[] = {
    { kPadCmdHd2dMenu, "HD2D_MENU", "機能メニュー(設定・言語・終了)", "Feature menu (settings, language, quit)" },
    { kPadCmdTurnLeft, "HD2D_TURN_LEFT", "視点を左へ回す", "Turn the view left" },
    { kPadCmdTurnRight, "HD2D_TURN_RIGHT", "視点を右へ回す", "Turn the view right" },
    { kPadCmdBindings, "CMD_GAMEPAD_MENU", "ゲームパッドの割り当て", "Gamepad bindings" },
};

//! 既定の割り当て。前半は原作の `gamepad.cc`、後半は画面の側の機能（原作が使っていない同時押しの層 LB+RB だけ）。
struct DefaultBind {
    PadInput button;
    int mods;
    const char *name;
};
constexpr DefaultBind kDefaults[] = {
    { PadInput::X, 0, "CMD_EXPLORE" }, // 自動探索
    { PadInput::Y, 0, "CMD_AUTOFIGHT" }, // 自動戦闘
    { PadInput::LeftShoulder, 0, "CMD_GO_DOWNSTAIRS" }, // 階段を下りる
    { PadInput::RightShoulder, 0, "CMD_GO_UPSTAIRS" }, // 階段を上る
    { PadInput::LeftTrigger, 0, "CMD_DISPLAY_INVENTORY" }, // 持ち物
    { PadInput::RightTrigger, 0, "CMD_FIRE" }, // 射撃
    { PadInput::Back, 0, "CMD_GAME_MENU" }, // ゲームメニュー
    { PadInput::Start, 0, "CMD_CAST_SPELL" }, // 呪文を唱える
    { PadInput::Back, 3, "HD2D_MENU" }, // LB+RB+Back: HD2D の機能メニュー
    { PadInput::Start, 3, "CMD_GAMEPAD_MENU" }, // LB+RB+Start: 割り当ての画面
    { PadInput::LeftTrigger, 3, "HD2D_TURN_LEFT" }, // LB+RB+LT: 視点を左へ
    { PadInput::RightTrigger, 3, "HD2D_TURN_RIGHT" }, // LB+RB+RT: 視点を右へ
};

std::string lowercase(std::string s)
{
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

const Hd2dFunction *function_by_id(int id)
{
    for (const Hd2dFunction &f : kHd2dFunctions) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

const DcssPadCmdName *cmd_name_entry(const std::string &name)
{
    for (const DcssPadCmdName &e : kDcssPadCmdNames) {
        if (name == e.name) {
            return &e;
        }
    }
    return nullptr;
}

//! 仮想パッド用の番号（コアのコマンドの番号と重ならない正の数）。
constexpr int kVpadFunctionBase = 90000;

} // namespace

// ================================================================== 枠

bool dcss_pad_slot_valid(PadInput button, int mods)
{
    const int b = static_cast<int>(button);
    if ((b < 0) || (b >= kPadInputCount) || (mods < 0) || (mods >= kPadLayerCount)) {
        return false;
    }
    return (mods == 0) || ((button != PadInput::LeftShoulder) && (button != PadInput::RightShoulder));
}

bool dcss_pad_slot_fixed(PadInput button, int mods)
{
    return (mods == 0) && ((button == PadInput::A) || (button == PadInput::B) || (button == PadInput::LeftStick) || (button == PadInput::RightStick));
}

std::string dcss_pad_slot_name(int slot)
{
    if ((slot < 0) || (slot >= kPadSlotCount)) {
        return std::string();
    }
    const PadInput b = dcss_pad_slot_button(slot);
    const int mods = dcss_pad_slot_mods(slot);
    if (!dcss_pad_slot_valid(b, mods)) {
        return std::string();
    }
    std::string name;
    if ((mods & kPadModCtrl) != 0) {
        name += "LB+";
    }
    if ((mods & kPadModAlt) != 0) {
        name += "RB+";
    }
    return name + kButtonNames[static_cast<int>(b)];
}

int dcss_pad_parse_slot(const std::string &text)
{
    std::string rest = text;
    int mods = 0;
    for (std::size_t plus = rest.find('+'); plus != std::string::npos; plus = rest.find('+')) {
        const std::string head = lowercase(rest.substr(0, plus));
        const int bit = (head == "lb") ? kPadModCtrl : ((head == "rb") ? kPadModAlt : 0);
        if ((bit == 0) || ((mods & bit) != 0)) {
            return -1;
        }
        mods |= bit;
        rest = rest.substr(plus + 1);
    }
    const std::string name = lowercase(rest);
    for (int i = 0; i < kPadInputCount; ++i) {
        if (name == lowercase(kButtonNames[i])) {
            return dcss_pad_slot_valid(static_cast<PadInput>(i), mods) ? dcss_pad_slot(static_cast<PadInput>(i), mods) : -1;
        }
    }
    return -1;
}

// ================================================================== コマンドの表

bool DcssPad::ingest_table(const nlohmann::json &message)
{
    const auto cmds = message.find("cmds");
    if ((cmds == message.end()) || !cmds->is_array()) {
        return false;
    }
    this->commands_.clear();
    this->ids_by_name_.clear();
    for (const nlohmann::json &row : *cmds) {
        if (!row.is_array() || (row.size() < 2) || !row[0].is_number_integer() || !row[1].is_string()) {
            continue;
        }
        DcssPadCommand c;
        c.id = row[0].get<int>();
        c.name = row[1].get<std::string>();
        if ((row.size() > 2) && row[2].is_string()) {
            c.key = row[2].get<std::string>();
        }
        this->ids_by_name_[c.name] = c.id;
        this->commands_.push_back(std::move(c));
    }
    this->apply_pending();
    return !this->commands_.empty();
}

int DcssPad::id_for_name(const std::string &name) const
{
    const auto it = this->ids_by_name_.find(name);
    return (it == this->ids_by_name_.end()) ? 0 : it->second;
}

std::string DcssPad::name_for_id(int id) const
{
    for (const DcssPadCommand &c : this->commands_) {
        if (c.id == id) {
            return c.name;
        }
    }
    return std::string();
}

// ================================================================== 名前

std::string DcssPad::label(int id) const
{
    if (id == 0) {
        return this->tr("割り当てなし", "No command");
    }
    if (const Hd2dFunction *f = function_by_id(id)) {
        return this->japanese_ ? f->ja : f->en;
    }
    const std::string name = this->name_for_id(id);
    if (const DcssPadCmdName *e = cmd_name_entry(name)) {
        return this->japanese_ ? e->ja : e->en;
    }
    // 表に無いコマンド（本家で増えたときなど）は `CMD_` の名前から作る。
    std::string text = name.empty() ? ("#" + std::to_string(id)) : name;
    if (text.rfind("CMD_", 0) == 0) {
        text = text.substr(4);
    }
    for (char &c : text) {
        c = (c == '_') ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string DcssPad::label_with_key(int id) const
{
    std::string text = this->label(id);
    for (const DcssPadCommand &c : this->commands_) {
        if ((c.id == id) && !c.key.empty()) {
            text += " (" + c.key + ")";
            break;
        }
    }
    return text;
}

std::vector<DcssPadGroup> DcssPad::groups() const
{
    std::vector<DcssPadGroup> out;
    if (this->commands_.empty()) {
        return out;
    }
    constexpr int kGroupCount = static_cast<int>(sizeof(kDcssPadGroups) / sizeof(kDcssPadGroups[0]));
    out.resize(static_cast<std::size_t>(kGroupCount));
    for (int i = 0; i < kGroupCount; ++i) {
        out[static_cast<std::size_t>(i)].name = this->japanese_ ? kDcssPadGroups[i].ja : kDcssPadGroups[i].en;
    }
    // 分類の並びは原作の表（`gamepad-menu.cc`）。表に無いコマンドは最後の「その他のコマンド」へ。
    std::vector<bool> listed(this->commands_.size(), false);
    for (const DcssPadCmdName &e : kDcssPadCmdNames) {
        for (std::size_t i = 0; i < this->commands_.size(); ++i) {
            if (this->commands_[i].name == e.name) {
                out[static_cast<std::size_t>(e.group)].ids.push_back(this->commands_[i].id);
                listed[i] = true;
                break;
            }
        }
    }
    DcssPadGroup others;
    others.name = this->tr("その他のコマンド(内部の名前)", "All other commands");
    for (std::size_t i = 0; i < this->commands_.size(); ++i) {
        if (!listed[i]) {
            others.ids.push_back(this->commands_[i].id);
        }
    }
    if (!others.ids.empty()) {
        out.push_back(std::move(others));
    }
    DcssPadGroup screen;
    screen.name = this->tr("画面の機能(原作にない)", "Screen functions (not in the original)");
    for (const Hd2dFunction &f : kHd2dFunctions) {
        screen.ids.push_back(f.id);
    }
    out.push_back(std::move(screen));
    out.erase(std::remove_if(out.begin(), out.end(), [](const DcssPadGroup &g) { return g.ids.empty(); }), out.end());
    return out;
}

std::string DcssPad::layer_name(int mods) const
{
    if ((mods < 0) || (mods >= kPadLayerCount)) {
        return std::string();
    }
    return this->japanese_ ? kDcssPadLayers[mods].ja : kDcssPadLayers[mods].en;
}

// ================================================================== 割り当て

int DcssPad::default_command(int slot) const
{
    for (const DefaultBind &d : kDefaults) {
        if (dcss_pad_slot(d.button, d.mods) != slot) {
            continue;
        }
        int value = 0;
        return this->value_from_name(d.name, value) ? value : 0;
    }
    return 0;
}

int DcssPad::command(int slot) const
{
    const auto it = this->user_.find(slot);
    if (it != this->user_.end()) {
        return it->second;
    }
    return this->default_command(slot);
}

bool DcssPad::set_command(int slot, int cmd)
{
    if ((slot < 0) || (slot >= kPadSlotCount)) {
        return false;
    }
    const PadInput b = dcss_pad_slot_button(slot);
    const int mods = dcss_pad_slot_mods(slot);
    if (!dcss_pad_slot_valid(b, mods) || dcss_pad_slot_fixed(b, mods)) {
        return false;
    }
    if (cmd == kPadUseDefault) {
        this->user_.erase(slot);
    } else {
        this->user_[slot] = cmd;
    }
    this->changed_ = true;
    return true;
}

bool DcssPad::value_from_name(const std::string &name, int &value) const
{
    if (lowercase(name) == "none") {
        value = 0;
        return true;
    }
    for (const Hd2dFunction &f : kHd2dFunctions) {
        if (name == f.save) {
            value = f.id;
            return true;
        }
    }
    const int id = this->id_for_name(name);
    if (id == 0) {
        return false;
    }
    value = id;
    return true;
}

std::string DcssPad::value_name(int value) const
{
    if (value == 0) {
        return "none";
    }
    if (const Hd2dFunction *f = function_by_id(value)) {
        return f->save;
    }
    return this->name_for_id(value);
}

std::string DcssPad::save_line() const
{
    std::string out;
    const auto add = [&out](const std::string &slot, const std::string &name) {
        if (name.empty()) {
            return;
        }
        if (!out.empty()) {
            out += ',';
        }
        out += slot + ':' + name;
    };
    for (const auto &entry : this->user_) {
        add(dcss_pad_slot_name(entry.first), this->value_name(entry.second));
    }
    for (const auto &entry : this->pending_) { // 表が届く前に読んだ分（名前が引けるまで失わない）
        add(dcss_pad_slot_name(entry.first), entry.second);
    }
    return out;
}

void DcssPad::load_line(const std::string &line)
{
    this->user_.clear();
    this->pending_.clear();
    std::size_t from = 0;
    while (from <= line.size()) {
        const std::size_t comma = line.find(',', from);
        const std::string item = line.substr(from, (comma == std::string::npos) ? std::string::npos : (comma - from));
        from = (comma == std::string::npos) ? (line.size() + 1) : (comma + 1);
        const std::size_t colon = item.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const int slot = dcss_pad_parse_slot(item.substr(0, colon));
        if ((slot < 0) || dcss_pad_slot_fixed(dcss_pad_slot_button(slot), dcss_pad_slot_mods(slot))) {
            continue;
        }
        const std::string name = item.substr(colon + 1);
        int value = 0;
        if (this->value_from_name(name, value)) {
            this->user_[slot] = value;
        } else if (!name.empty()) {
            this->pending_.emplace_back(slot, name);
        }
    }
    this->apply_pending();
}

void DcssPad::apply_pending()
{
    if (this->pending_.empty() || this->commands_.empty()) {
        return;
    }
    for (const auto &entry : this->pending_) {
        int value = 0;
        if (this->value_from_name(entry.second, value)) {
            this->user_[entry.first] = value;
        }
    }
    this->pending_.clear();
}

// ================================================================== 入力

namespace {

//! 方向のキー（原作 `_pad_direction_key`）。矢印キーとテンキー（NumLock 無し）と同じキー。
int direction_key(int dx, int dy)
{
    static const int keys[3][3] = {
        { kPadKeyHome, kPadKeyUp, kPadKeyPgUp },
        { kPadKeyLeft, 0, kPadKeyRight },
        { kPadKeyEnd, kPadKeyDown, kPadKeyPgDn },
    };
    if ((dx < -1) || (dx > 1) || (dy < -1) || (dy > 1)) {
        return 0;
    }
    return keys[dy + 1][dx + 1];
}

int direction_sym(int dx, int dy)
{
    if (dy < 0) {
        return (dx == 0) ? SDLK_UP : ((dx < 0) ? SDLK_HOME : SDLK_PAGEUP);
    }
    if (dy > 0) {
        return (dx == 0) ? SDLK_DOWN : ((dx < 0) ? SDLK_END : SDLK_PAGEDOWN);
    }
    return (dx < 0) ? SDLK_LEFT : SDLK_RIGHT;
}

DcssPadAction core_key(int key)
{
    DcssPadAction a;
    a.kind = DcssPadAction::Kind::CoreKey;
    a.key = key;
    return a;
}

} // namespace

DcssPadScene DcssPad::scene_for(const DcssUiModel &model, bool windows_open)
{
    if (windows_open) {
        return DcssPadScene::Window;
    }
    if (model.map_screen()) {
        return DcssPadScene::Target;
    }
    switch (model.input_mode()) {
    case kInputCommand:
        return DcssPadScene::Command;
    case kInputTarget:
    case kInputTargetDir:
    case kInputTargetPath:
        return DcssPadScene::Target;
    default:
        return DcssPadScene::Other;
    }
}

std::vector<DcssPadAction> DcssPad::on_press(const PadPress &press, DcssPadScene scene, bool capture) const
{
    std::vector<DcssPadAction> out;
    const PadInput b = press.input;
    const int mods = press.mods;
    if (!dcss_pad_slot_valid(b, mods)) {
        return out;
    }
    // 単独の L3・R3: クリック（固定）。
    if ((mods == 0) && ((b == PadInput::LeftStick) || (b == PadInput::RightStick))) {
        DcssPadAction a;
        a.kind = DcssPadAction::Kind::Click;
        a.button = (b == PadInput::LeftStick) ? 1 : 3;
        out.push_back(a);
        return out;
    }
    // 単独の A・B: 決定・取消（固定）。コマンド待ちでは何も出さない（Enter・Esc を出すと「そのコマンドは存在しません」が出るだけ）。
    if ((mods == 0) && ((b == PadInput::A) || (b == PadInput::B))) {
        const bool confirm = (b == PadInput::A);
        if (scene == DcssPadScene::Command) {
            out.push_back(core_key(kPadKeyNoKey));
        } else if (scene == DcssPadScene::Window) {
            DcssPadAction a;
            a.kind = DcssPadAction::Kind::WindowKey;
            a.sym = confirm ? SDLK_RETURN : SDLK_ESCAPE;
            out.push_back(a);
        } else {
            out.push_back(core_key(confirm ? kPadKeyEnter : kPadKeyEscape));
        }
        return out;
    }
    const int slot = dcss_pad_slot(b, mods);
    // 割り当ての画面が開いている間は、押した枠を開く。
    if (capture) {
        DcssPadAction a;
        a.kind = DcssPadAction::Kind::SlotCapture;
        a.slot = slot;
        out.push_back(a);
        return out;
    }
    const int cmd = this->command(slot);
    // 画面の機能（どの場面でも働く）。
    if (cmd == kPadCmdHd2dMenu) {
        DcssPadAction a;
        a.kind = DcssPadAction::Kind::FeatureMenu;
        out.push_back(a);
        return out;
    }
    if ((cmd == kPadCmdTurnLeft) || (cmd == kPadCmdTurnRight)) {
        DcssPadAction a;
        a.kind = DcssPadAction::Kind::TurnView;
        a.turn = (cmd == kPadCmdTurnLeft) ? -1 : 1;
        out.push_back(a);
        return out;
    }
    // 割り当ての画面を開く（原作の CMD_GAMEPAD_MENU）。コマンド待ちのときだけ。
    if (cmd == kPadCmdBindings) {
        if (scene == DcssPadScene::Command) {
            DcssPadAction a;
            a.kind = DcssPadAction::Kind::OpenBindings;
            out.push_back(a);
        } else if (scene != DcssPadScene::Window) {
            out.push_back(core_key(kPadKeyNoKey));
        }
        return out;
    }
    if (cmd > 0) {
        // キーマップも bindkey も通らない、コマンドそのもの。コマンドを待っていない場面では何もしない（無視するキー）。
        if (scene == DcssPadScene::Command) {
            out.push_back(core_key(-cmd));
        } else if (scene != DcssPadScene::Window) {
            out.push_back(core_key(kPadKeyNoKey));
        }
        return out;
    }
    // 割り当ての無い枠は、その枠のキーを出す（マクロ・キーマップのトリガー。登録が無ければゲームが無視する）。
    if (scene != DcssPadScene::Window) {
        out.push_back(core_key(kPadKeyPadBase - slot));
    }
    return out;
}

DcssPadAction DcssPad::on_direction(int dx, int dy, DcssPadScene scene) const
{
    DcssPadAction a;
    if ((dx == 0) && (dy == 0)) {
        return a;
    }
    if (scene == DcssPadScene::Window) {
        // メニューの中では斜めの Home・PgUp などが「先頭へ」「1 ページ」になるので、縦の成分だけを出す。
        if ((dx != 0) && (dy != 0)) {
            dx = 0;
        }
        a.kind = DcssPadAction::Kind::WindowKey;
        a.sym = direction_sym(dx, dy);
        return a;
    }
    const int key = direction_key(dx, dy);
    if (key == 0) {
        return a;
    }
    return core_key(key);
}

// ================================================================== 疑似カーソル

bool DcssPad::cursor_motion(std::uint32_t now_ms, bool focused, int raw_x, int raw_y, int &dx, int &dy)
{
    dx = 0;
    dy = 0;
    const double x = raw_x;
    const double y = raw_y;
    const bool active = ((x * x) + (y * y)) > (static_cast<double>(kCursorDeadzone) * kCursorDeadzone);
    if (!focused || !active) {
        this->cursor_moving_ = false;
        this->cursor_frac_x_ = 0;
        this->cursor_frac_y_ = 0;
        return false;
    }
    if (!this->cursor_moving_) {
        // 倒し始め。時刻だけ覚え、次の刻みから動かす。
        this->cursor_moving_ = true;
        this->cursor_last_ = now_ms;
        return false;
    }
    // 待ちが長引いても（重い処理のあとなど）まとめて大きく飛ばない。
    const std::uint32_t dt = std::min<std::uint32_t>(now_ms - this->cursor_last_, 100);
    this->cursor_last_ = now_ms;
    const double mag = std::sqrt((x * x) + (y * y));
    const double t = (std::min(mag, 32767.0) - kCursorDeadzone) / (32767.0 - kCursorDeadzone);
    // 2 乗の曲線: 浅く倒すと細かく、倒し切ると速く動く。
    const double speed = kCursorMinSpeed + ((kCursorMaxSpeed - kCursorMinSpeed) * t * t);
    const double step = speed * dt / 1000.0;
    this->cursor_frac_x_ += (x / mag) * step;
    this->cursor_frac_y_ += (y / mag) * step;
    dx = static_cast<int>(this->cursor_frac_x_);
    dy = static_cast<int>(this->cursor_frac_y_);
    this->cursor_frac_x_ -= dx;
    this->cursor_frac_y_ -= dy;
    return (dx != 0) || (dy != 0);
}

bool DcssPad::cursor_position(int &x, int &y) const
{
    if (!this->cursor_known_) {
        return false;
    }
    x = this->cursor_x_;
    y = this->cursor_y_;
    return true;
}

void DcssPad::clamp_cursor(int screen_w, int screen_h)
{
    this->cursor_x_ = std::clamp(this->cursor_x_, 0, std::max(0, screen_w - 1));
    this->cursor_y_ = std::clamp(this->cursor_y_, 0, std::max(0, screen_h - 1));
}

void DcssPad::move_cursor(int dx, int dy, int screen_w, int screen_h)
{
    if (!this->cursor_known_) {
        this->cursor_known_ = true;
        this->cursor_x_ = screen_w / 2;
        this->cursor_y_ = screen_h / 2;
    }
    this->cursor_x_ += dx;
    this->cursor_y_ += dy;
    this->clamp_cursor(screen_w, screen_h);
    this->cursor_pad_active_ = true;
}

void DcssPad::observe_mouse(int x, int y)
{
    if (!this->real_known_) {
        this->real_known_ = true;
        this->real_x_ = x;
        this->real_y_ = y;
        if (!this->cursor_known_) {
            this->cursor_known_ = true;
            this->cursor_x_ = x;
            this->cursor_y_ = y;
        }
        return;
    }
    if ((x != this->real_x_) || (y != this->real_y_)) {
        this->real_x_ = x;
        this->real_y_ = y;
        this->note_real_mouse(x, y);
    }
}

void DcssPad::note_real_mouse(int x, int y)
{
    this->cursor_known_ = true;
    this->cursor_x_ = x;
    this->cursor_y_ = y;
    this->cursor_pad_active_ = false;
}

// ================================================================== 下の操作の案内

std::vector<std::string> DcssPad::guide_items(DcssPadScene scene, int chord_mask, bool bindings_screen) const
{
    std::vector<std::string> items;
    const auto add = [&items](const std::string &key, const std::string &what) { items.push_back(key + ":" + what); };
    const std::string stick = this->tr("右スティック", "Right stick");
    if (bindings_screen) {
        add(this->tr("十字", "D-pad"), this->tr("選ぶ", "choose"));
        add("A", this->tr("決定", "confirm"));
        add("B", this->tr("戻る・閉じる", "back"));
        add(this->tr("ボタン", "Button"), this->tr("押すとその枠を開く", "opens that slot"));
        return items;
    }
    if (chord_mask != 0) {
        // 修飾を押している間は、その層の割り当てを出す（同時押しは覚えていないと使えない）。割り当ての無い枠はまとめて 1 項目。
        bool any_free = false;
        for (int i = 0; i < kPadInputCount; ++i) {
            const PadInput b = static_cast<PadInput>(i);
            if (!dcss_pad_slot_valid(b, chord_mask)) {
                continue;
            }
            const int slot = dcss_pad_slot(b, chord_mask);
            const int cmd = this->command(slot);
            if (cmd == 0) {
                any_free = true;
                continue;
            }
            add(dcss_pad_slot_name(slot), this->label(cmd));
        }
        if (any_free) {
            add(this->tr("ほか", "others"), this->tr("割り当てなし(マクロのキー)", "no command (macro key)"));
        }
        return items;
    }
    switch (scene) {
    case DcssPadScene::Window:
        add(this->tr("十字", "D-pad"), this->tr("選ぶ", "choose"));
        add("A", this->tr("決定", "confirm"));
        add("B", this->tr("閉じる・戻る", "close"));
        add(stick, this->tr("カーソル", "cursor"));
        add("L3/R3", this->tr("左・右クリック", "left/right click"));
        return items;
    case DcssPadScene::Target:
        add(this->tr("十字", "D-pad"), this->tr("カーソルを動かす", "move the cursor"));
        add("A", this->tr("決定", "confirm"));
        add("B", this->tr("取消", "cancel"));
        add(stick, this->tr("カーソル", "cursor"));
        add("L3/R3", this->tr("左・右クリック", "left/right click"));
        return items;
    case DcssPadScene::Other:
        add(this->tr("十字", "D-pad"), this->tr("カーソル", "cursor"));
        add("A", this->tr("決定", "confirm"));
        add("B", this->tr("取消", "cancel"));
        return items;
    case DcssPadScene::Command:
        break;
    }
    add(this->tr("十字", "D-pad"), this->tr("移動", "move"));
    add("A/B", this->tr("メニュー・狙いで決定/取消", "confirm/cancel in menus"));
    static const PadInput order[] = { PadInput::X, PadInput::Y, PadInput::LeftShoulder, PadInput::RightShoulder, PadInput::LeftTrigger,
        PadInput::RightTrigger, PadInput::Back, PadInput::Start };
    for (const PadInput b : order) {
        const int slot = dcss_pad_slot(b, 0);
        const int cmd = this->command(slot);
        add(dcss_pad_slot_name(slot), (cmd == 0) ? this->tr("割り当てなし", "none") : this->label(cmd));
    }
    add("L3/R3", this->tr("左・右クリック", "left/right click"));
    add(stick, this->tr("カーソル", "cursor"));
    return items;
}

// ================================================================== 仮想パッド

void DcssPad::fill_vpad_view(PadBinds &binds, std::vector<PadCommand> &commands) const
{
    commands.clear();
    // 単独の L3・R3（固定。左・右クリック）にも名前を出す（A・B・Back は仮想パッドが「決定」「取消」「メニュー」と出す）。
    const auto add_fixed = [&](PadInput b, int id, const std::string &name) {
        binds.command[0][static_cast<int>(b)] = id;
        PadCommand c;
        c.id = id;
        c.label_utf8 = name;
        commands.push_back(std::move(c));
    };
    for (int mods = 0; mods < kPadLayerCount; ++mods) {
        for (int i = 0; i < kPadInputCount; ++i) {
            binds.command[mods][i] = 0;
            const PadInput b = static_cast<PadInput>(i);
            if (!dcss_pad_slot_valid(b, mods) || dcss_pad_slot_fixed(b, mods)) {
                continue;
            }
            const int cmd = this->command(dcss_pad_slot(b, mods));
            if (cmd == 0) {
                continue;
            }
            // 仮想パッドは負の番号を「この exe の操作」として読むので、画面の機能は正の番号に直す。
            int shown = cmd;
            if (cmd < 0) {
                shown = kVpadFunctionBase + (-cmd);
            }
            binds.command[mods][i] = shown;
            const bool known = std::any_of(commands.begin(), commands.end(), [shown](const PadCommand &c) { return c.id == shown; });
            if (!known) {
                PadCommand c;
                c.id = shown;
                c.label_utf8 = this->label(cmd);
                commands.push_back(std::move(c));
            }
        }
    }
    add_fixed(PadInput::LeftStick, kVpadFunctionBase + 10, this->tr("左クリック", "Left click"));
    add_fixed(PadInput::RightStick, kVpadFunctionBase + 11, this->tr("右クリック", "Right click"));
}

} // namespace hd2d::dcss
