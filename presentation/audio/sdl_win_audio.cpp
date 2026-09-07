/*!
 * @file sdl_win_audio.cpp
 * @brief SDL2 UI 経路の効果音／BGM（waveOut + MCI、既存 main-win 実装を利用）
 */
#include "audio/sdl_win_audio.h"

#include "audio/sound_event_queue.h"
#include "frame/sdl_ui_options.h"

#include "game-option/runtime-arguments.h"
#include "game-option/special-options.h"
#include "main-win/main-win-mci.h"
#include "main-win/main-win-music.h"
#include "main-win/main-win-sound.h"
#include "main/sound-definitions-table.h"
#include "main/sound-of-music.h"
#include "util/enum-converter.h"
#include "system/player-type-definition.h"
#include "term/z-term.h"
#include "util/angband-files.h"
#include "world/world.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdio>

extern PlayerType *p_ptr;

namespace presentation {
namespace {

HWND g_mci_hwnd = nullptr;
bool g_inited = false;
const wchar_t *kMciClass = L"HengbandSdlMciNotify";

LRESULT CALLBACK mci_wnd_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
    if (msg == MM_MCINOTIFY) {
        const auto &opts = sdl_ui_options();
        const int vol = main_win_music::VOLUME_TABLE[static_cast<size_t>(
            (std::max)(0, (std::min)(opts.music_volume_index, SdlUiOptions::kVolumeLevels - 1)))];
        main_win_music::on_mci_notify(w_param, static_cast<LONG>(l_param), vol);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w_param, l_param);
}

bool create_mci_window()
{
    if (g_mci_hwnd != nullptr) {
        return true;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = mci_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMciClass;
    RegisterClassW(&wc);
    g_mci_hwnd = CreateWindowExW(
        0, kMciClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (g_mci_hwnd == nullptr) {
        std::fprintf(stderr, "[sdl-audio] CreateWindowEx MCI notify failed\n");
        return false;
    }
    setup_mci(g_mci_hwnd);
    return true;
}

void destroy_mci_window()
{
    if (g_mci_hwnd != nullptr) {
        DestroyWindow(g_mci_hwnd);
        g_mci_hwnd = nullptr;
    }
}

} // namespace

bool sdl_win_audio_init(const std::filesystem::path &lib_path)
{
    if (g_inited) {
        return true;
    }

    ANGBAND_DIR_XTRA_SOUND = path_build(lib_path / "xtra", "sound");
    ANGBAND_DIR_XTRA_MUSIC = path_build(lib_path / "xtra", "music");

    load_sound_prefs();
    main_win_music::load_music_prefs();
    create_mci_window();

    load_sdl_ui_options();
    sdl_win_audio_apply_options();

    g_inited = true;
    std::fprintf(stderr, "[sdl-audio] init sound=%s music=%s\n",
        ANGBAND_DIR_XTRA_SOUND.string().c_str(), ANGBAND_DIR_XTRA_MUSIC.string().c_str());
    return true;
}

void sdl_win_audio_shutdown()
{
    if (!g_inited) {
        return;
    }
    main_win_music::stop_music();
    finalize_sound();
    destroy_mci_window();
    g_inited = false;
}

void sdl_win_audio_apply_options()
{
    const auto &opts = sdl_ui_options();
    use_sound = opts.sound_enabled;
    use_music = opts.music_enabled;
    arg_sound = opts.sound_enabled;
    arg_music = opts.music_enabled;

    const int sidx = (std::max)(0, (std::min)(opts.sound_volume_index, SdlUiOptions::kVolumeLevels - 1));
    const int midx = (std::max)(0, (std::min)(opts.music_volume_index, SdlUiOptions::kVolumeLevels - 1));
    arg_sound_volume_table_index = sidx;
    arg_music_volume_table_index = midx;

    if (!use_music) {
        main_win_music::stop_music();
        return;
    }

    main_win_music::set_music_volume(main_win_music::VOLUME_TABLE[static_cast<size_t>(midx)]);
    // Quest/Dungeon 表とフロアが揃ってから選曲（起動直後の空 map rbegin アサート回避と同系統）。
    if (p_ptr != nullptr && AngbandWorld::get_instance().character_generated) {
        select_floor_music(p_ptr);
    }
}

int sdl_win_audio_term_xtra(int n, int v)
{
    switch (n) {
    case TERM_XTRA_SOUND: {
        if (!use_sound) {
            return 0;
        }
        /*
         * **画面側が鳴らすなら、ここでは鳴らさず書き留める**。
         * 位置は分からない（`TERM_XTRA_SOUND` は番号しか運ばない）ので聞き手のマスを入れる
         * ——画面側では頭で鳴る。マスを持つ音は変種ごとに足していく。
         */
        if (presentation::sound_events_wanted()) {
            const auto sk = i2enum<SoundKind>(v);
            const auto it = sound_names.find(sk);
            if (it != sound_names.end()) {
                const int py = (p_ptr != nullptr) ? p_ptr->y : 0;
                const int px = (p_ptr != nullptr) ? p_ptr->x : 0;
                presentation::push_sound_event(it->second, py, px);
            }
            return 0;
        }
        const int sidx = (std::max)(0, (std::min)(arg_sound_volume_table_index, SdlUiOptions::kVolumeLevels - 1));
        return play_sound(v, SOUND_VOLUME_TABLE[static_cast<size_t>(sidx)]);
    }
    case TERM_XTRA_MUSIC_BASIC:
    case TERM_XTRA_MUSIC_DUNGEON:
    case TERM_XTRA_MUSIC_QUEST:
    case TERM_XTRA_MUSIC_TOWN:
    case TERM_XTRA_MUSIC_MONSTER:
        if (!use_music) {
            return 1;
        }
        return main_win_music::play_music(n, v) ? 0 : 1;
    case TERM_XTRA_MUSIC_MUTE:
        main_win_music::stop_music();
        return 0;
    case TERM_XTRA_SCENE:
        if (!use_music) {
            return 1;
        }
        main_win_music::play_music_scene(v);
        return 0;
    case TERM_XTRA_NOISE:
        // ベル等。任意。現状 no-op で可。
        return 0;
    default:
        return 1;
    }
}

} // namespace presentation
