/*!
 * @file key_binds.cpp
 * @brief `key_binds.h` の実装。**SDL を知っているのはここだけ**（綴りの解決に要る）。
 */
#include "ui/key_binds.h"

#include "i18n/lang.h"

#include <SDL2/SDL.h>

#include <cstdio>

namespace hd2d {

const std::vector<int> &ui_action_ids()
{
    static const std::vector<int> ids{ kActionFeatureMenu, kActionLayoutCycle, kActionSubsToggle, kActionFpsToggle,
        kActionFpsLevelView, kActionVrRecenter, kActionTurnLeft, kActionTurnRight,
        kActionMainPanelToggle, kActionCommandMenu };
    return ids;
}

const char *ui_action_label(int action)
{
    switch (action) {
    case kActionFeatureMenu:
        return i18n::tr("hd2d.app.hd2d-app.open-the-feature-menu");
    case kActionLayoutCycle:
        return i18n::tr("hd2d.ui.key-binds.cycle-the-layout");
    case kActionSubsToggle:
        return i18n::tr("hd2d.ui.key-binds.open-or-close-the-sub-panels");
    case kActionFpsToggle:
        return i18n::tr("hd2d.ui.key-binds.toggle-the-first-person-view");
    case kActionFpsLevelView:
        return i18n::tr("hd2d.ui.key-binds.bring-the-first-person-view-back-to");
    case kActionVrRecenter:
        return i18n::tr("hd2d.ui.key-binds.vr-put-the-board-and-the-panels-back");
    case kActionTurnLeft:
        return i18n::tr("hd2d.ui.key-binds.turn-the-view-90-degrees-left");
    case kActionTurnRight:
        return i18n::tr("hd2d.ui.key-binds.turn-the-view-90-degrees-right");
    case kActionMainPanelToggle:
        return i18n::tr("hd2d.ui.key-binds.switch-the-main-panel-3d-ascii");
    case kActionCommandMenu:
        return i18n::tr("hd2d.ui.key-binds.open-the-command-menu");
    default:
        return "";
    }
}

bool key_is_reserved(int keycode)
{
    switch (keycode) {
    /*
     * 選ぶ・決定・取消。**これを割り当てられるようにすると割り当て画面から出られなくなる。**
     * テンキーの方向も入れる（NumLock を切っていると方向として使われる道が
     * `hd2d_app.cpp` の `cursor_nav_from_key` にある）。
     */
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFT:
    case SDLK_RIGHT:
    case SDLK_KP_2:
    case SDLK_KP_4:
    case SDLK_KP_6:
    case SDLK_KP_8:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_ESCAPE:
        return true;
    default:
        return false;
    }
}

bool key_is_escape(int keycode)
{
    return keycode == SDLK_ESCAPE;
}

KeyBindEntry KeyBinds::find(int action) const
{
    for (const auto &entry : this->entries) {
        if (entry.action == action) {
            return entry;
        }
    }
    return KeyBindEntry{ action, 0, 0 };
}

int KeyBinds::action_for(int keycode, int mods) const
{
    if (keycode == 0) {
        return kActionNone;
    }
    for (const auto &entry : this->entries) {
        if ((entry.keycode == keycode) && (entry.mods == mods)) {
            return entry.action;
        }
    }
    return kActionNone;
}

bool KeyBinds::assign(int action, int keycode, int mods)
{
    if ((action == kActionNone) || (keycode == 0) || key_is_reserved(keycode)) {
        return false;
    }
    //! **先客から外す。**同じキーが 2 つの操作を出す表を作らない。
    for (auto it = this->entries.begin(); it != this->entries.end();) {
        if ((it->keycode == keycode) && (it->mods == mods) && (it->action != action)) {
            it = this->entries.erase(it);
        } else {
            ++it;
        }
    }
    for (auto &entry : this->entries) {
        if (entry.action == action) {
            entry.keycode = keycode;
            entry.mods = mods;
            return true;
        }
    }
    this->entries.push_back(KeyBindEntry{ action, keycode, mods });
    return true;
}

void KeyBinds::clear(int action)
{
    for (auto it = this->entries.begin(); it != this->entries.end();) {
        it = (it->action == action) ? this->entries.erase(it) : (it + 1);
    }
}

std::string KeyBinds::to_line() const
{
    std::string line;
    for (const auto &entry : this->entries) {
        if ((entry.action == kActionNone) || (entry.keycode == 0)) {
            continue;
        }
        if (!line.empty()) {
            line += ",";
        }
        line += std::to_string(entry.action) + ":" + std::to_string(entry.keycode) + ":" + std::to_string(entry.mods);
    }
    return line;
}

void KeyBinds::parse(const std::string &line)
{
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t comma = line.find(',', at);
        const std::string item = line.substr(at, (comma == std::string::npos) ? std::string::npos : (comma - at));
        int action = 0;
        int keycode = 0;
        int mods = 0;
        //! 修飾は後から足した欄なので、**2 つしか無い古い cfg も読めるように**しておく。
        const int got = std::sscanf(item.c_str(), "%d:%d:%d", &action, &keycode, &mods);
        if ((got >= 2) && (action != kActionNone) && (keycode != 0) && !key_is_reserved(keycode)) {
            (void)this->assign(action, keycode, (got >= 3) ? mods : 0);
        }
        // 読めない綴りは黙って飛ばす（版が進んだ cfg でも落ちない）
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
}

KeyBinds default_key_binds()
{
    KeyBinds binds;
    //! 既存 UI と同じ綴り（F10 = 機能メニュー）。利用者の記憶がそのまま効く。
    (void)binds.assign(kActionFeatureMenu, SDLK_F10, 0);
    (void)binds.assign(kActionLayoutCycle, SDLK_F9, 0);
    (void)binds.assign(kActionSubsToggle, SDLK_F8, 0);
    (void)binds.assign(kActionFpsToggle, SDLK_F7, 0);
    //! 見下ろしの 90° 視点回転。F7 の隣へ続ける。
    (void)binds.assign(kActionTurnLeft, SDLK_F5, 0);
    (void)binds.assign(kActionTurnRight, SDLK_F6, 0);
    //! メインパネルの 3D ／ 2D アスキー。
    (void)binds.assign(kActionMainPanelToggle, SDLK_F4, 0);
    return binds;
}

std::string key_display_name(int keycode, int mods)
{
    if (keycode == 0) {
        return std::string();
    }
    const char *name = SDL_GetKeyName(static_cast<SDL_Keycode>(keycode));
    if ((name == nullptr) || (name[0] == '\0')) {
        return std::string();
    }
    std::string text;
    //! 並びは Windows の慣習（Ctrl → Alt → Shift）。
    if ((mods & kModCtrl) != 0) {
        text += "Ctrl+";
    }
    if ((mods & kModAlt) != 0) {
        text += "Alt+";
    }
    if ((mods & kModShift) != 0) {
        text += "Shift+";
    }
    text += name;
    return text;
}

int key_from_fkey(int fkey)
{
    if ((fkey < 1) || (fkey > 12)) {
        return 0;
    }
    //! SDL の F1〜F12 は連番（`SDL_scancode.h`）。1 つずつ書くより取り違えが起きない。
    return static_cast<int>(SDLK_F1) + (fkey - 1);
}

std::string core_key_sequence_name(const std::vector<int> &keys)
{
    std::string text;
    for (const int key : keys) {
        if ((key <= 0) || (key > 255)) {
            continue;
        }
        if (key == 0x1B) {
            text += "ESC";
        } else if (key == 0x0D) {
            text += "Enter";
        } else if (key == ' ') {
            text += "Space";
        } else if ((key >= 0x21) && (key <= 0x7E)) {
            text += static_cast<char>(key);
        } else {
            //! 制御文字（Ctrl 併用）。コアは `^A` の形で書くので合わせる。
            text += "^";
            text += static_cast<char>('@' + key);
        }
    }
    return text;
}

} // namespace hd2d
