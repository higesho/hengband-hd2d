/*!
 * @file sdl_win_audio.h
 * @brief SDL2 UI 経路向け Win32 効果音／BGM 接続（main-win sound/music 再利用）
 */
#pragma once

#include <filesystem>

namespace presentation {

//! lib 配下の xtra/sound・xtra/music を初期化し、MCI コールバック窓を用意する。
bool sdl_win_audio_init(const std::filesystem::path &lib_path);
void sdl_win_audio_shutdown();

//! SdlUiOptions を use_sound/use_music／音量表に反映する。
void sdl_win_audio_apply_options();

/*!
 * @brief Term xtra の音関連を処理する。
 * @return 処理したら 0、未対応なら 1（呼び出し側が default へ）
 */
int sdl_win_audio_term_xtra(int n, int v);

} // namespace presentation
