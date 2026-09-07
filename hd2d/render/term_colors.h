/*!
 * @file term_colors.h
 * @brief `TERM_COLOR`（0〜15）→ RGB。
 *
 * @details 表は**コアから毎フレーム届く**（`GameFrame::term_palette`）。
 * `&`（カラーの設定）と pref の `V:` 行はコアの `angband_color_table` を書き換えるので、
 * ここで写しをべた書きに持つと**利用者が色を変えても 1 ドットも変わらない**。
 * 実際に長らくそうなっていた（2026-08-14 に直した）。
 *
 * 引く側（`ui_paint` / `game_hud` / `hd2d_app`）は `GameFrame` を持っていないことも多いので、
 * **プロセスに 1 つの表**として置く。フレームを取り込んだところで `set_term_palette` を
 * 1 回呼ぶ（`hd2d_app.cpp` の `take_latest_frame` の直後）。届く前は `TermPalette` の
 * 既定＝コアの初期値なので、そのまま描いても違わない。
 *
 * ボクセル HD2D では最終的に色は素材（パレット）とビルボードが持つので、この表は
 * **テキスト表示と、デバッグ表示のためだけ**にある。
 */
#pragma once

#include "frame/game_frame.h"

#include <cstdint>

namespace hd2d {

struct RgbColor {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
};

//! プロセスに 1 つの色表。`set_term_palette` が入れ替える。
inline TermPalette &active_term_palette()
{
    static TermPalette palette{}; //!< 既定＝コアの初期値（`game_frame.h`）
    return palette;
}

//! コアから届いた色表を取り込む。**フレームを取り込んだら必ず呼ぶこと。**
inline void set_term_palette(const TermPalette &palette)
{
    active_term_palette() = palette;
}

//! 下位 4bit に丸めて引く（コアは上位ビットに別の意味を載せることがある）。
inline RgbColor term_color_to_rgb(std::uint8_t color_index)
{
    const auto &entry = active_term_palette().rgb[color_index & 0x0F];
    return RgbColor{ entry[0], entry[1], entry[2] };
}

} // namespace hd2d
