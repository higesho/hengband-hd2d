/*!
 * @file attack_fx_overlay.h
 * @brief 攻撃の視覚効果のうち、3D の外（画面）に描くもの：視界内の全員にかかる魔法の光の波。
 *
 * の共通の約束と §7。波は `FxFrame::screen_waves`
 * （`fx_add_screen_wave`）に属性の見た目が足し、画面側が**ポスト処理のあと・戦闘の見せ場（被弾のフラッシュ）と同じ段**で描く。
 * 地図の矩形（`scene`）の中だけに描く（サブパネルの数字を染めない。被弾のフラッシュと同じ約束）。
 */
#pragma once

#include "render/voxel_fx.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"

#include <vector>

namespace hd2d {

//! 光の波を `scene` の中に描く。波が空なら何もしない。
void draw_attack_fx_waves(UiPaint &paint, const std::vector<FxScreenWave> &waves, const RectPx &scene);

} // namespace hd2d
