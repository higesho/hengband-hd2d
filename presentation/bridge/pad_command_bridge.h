/*!
 * @file pad_command_bridge.h
 * @brief パッド割り当てコマンド表の充填口（実体は `presentation/bridge/pad_command_table.cpp`）。
 *
 * ここだけがコアの `menu_info` / `keymap_actions_map` を知る。呼び出しは
 * `run_sdl_game` の **`init_angband` の後**。
 */
#pragma once

namespace presentation {

//! `menu_info` を平坦化して `pad_command_table()` を充填し、キーマップ逆引きを登録する。
void register_pad_command_table();

} // namespace presentation
