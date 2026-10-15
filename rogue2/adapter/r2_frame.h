#pragma once

#include "frame/game_frame.h"

#include <cstdint>
#include <vector>

namespace rogue2 {

GameFrame capture_frame(std::uint64_t frame_id);
std::vector<ModernRow> inventory_rows(unsigned int category_mask = 0);
int semantic_selftest();
//! 溜まっている魔法・投射物の効果（`GameFrame::projections`・`missiles`）を汲んで空にする。フレームを送ると決まった所で呼ぶ。
void fill_attack_fx(GameFrame &frame);
//! `--selftest` の攻撃の効果の分。モンスターの炎・表の整合を、ゲームの状態が要らない部分で数える。0 なら全部合った。
int attack_fx_selftest();

}
