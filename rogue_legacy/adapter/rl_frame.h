#pragma once

#include "frame/game_frame.h"

#include <cstdint>

namespace rogue_legacy {

GameFrame capture_frame(std::uint64_t frame_id, const char *core_name);
//! フレームの組み立てが持つ「自キャラが 1 度でも見えたか」「ゲームが終わった画面を見たか」を捨てる（新しいゲームの頭で呼ぶ）。
void reset_frame_state();
std::uint16_t object_tile_for_glyph(std::uint32_t glyph);
//! 溜まっている魔法・投射物の効果（`GameFrame::projections`・`missiles`）を汲んで空にする。フレームを送ると決まった所で呼ぶ。
void fill_attack_fx(GameFrame &frame);
//! `--selftest-fx`：攻撃の効果の記録を、ゲームの状態が要らない部分で数える。0 なら全部合った。
int attack_fx_selftest();

} // namespace rogue_legacy
