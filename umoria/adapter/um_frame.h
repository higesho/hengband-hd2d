#pragma once

#include "frame/game_frame.h"
#include "frame/modern_window.h"

#include <vector>

namespace umoria {

//! 端末に地図が出ている（メニュー・店・ヘルプなどで覆われていない）なら真。
bool map_screen_active();
//! 地図・ミニマップ・階・HUD・状態列・メッセージを状態から埋める。キャラができてから呼ぶ。
void fill_world_frame(GameFrame &frame);
//! 持ち物（selection 0）と装備（selection 1）の行。画面側の持ち物一覧と、アイテムを選ぶ問いの窓で使う。
std::vector<ModernRow> owned_rows();
//! 溜まっている魔法・投射物の効果（`GameFrame::projections`・`missiles`）を汲んで空にする。`settled` は命令待ちにいるか。
void fill_attack_fx(GameFrame &frame, bool settled);
//! `UmoriaCore.exe --selftest`。状態のアイコンの集め方を、未鑑定の装備・空腹・日英の名前まで全数で照合する。0 なら全部合った。
int status_effects_selftest();
//! 同じく `--selftest` の攻撃の効果の分。魔法・矢弾の記録を、盲目・未識別・暗いマス・見えない相手まで数える。0 なら全部合った。
int attack_fx_selftest();

} // namespace umoria
