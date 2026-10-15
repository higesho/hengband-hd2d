/*!
 * @file entity_fx_mod.h
 * @brief 攻撃の視覚効果が掛ける「対象の見た目の一時的な変化」（`FxEntityMod`）を、実体の描画に当てる。
 *
 * 属性の見た目は `fx_mod_for(frame, gx, gy)`（`render/fx_styles/fx_parts.h`）で
 * `FxFrame::entity_mods` に変化を足す。ここはそれを**実体の描画データ**に当てる側で、`EntityTracker::build()` の後・描画の前に呼ぶ。
 *
 * | 変化 | 当たり方 |
 * |---|---|
 * | 押しつぶし | ボクセル板・字の板・モデルとも、高さが縮み、横が少し広がる（足元は動かない） |
 * | 震え | 水平に小刻みにずれる |
 * | のけぞり | `away_x/away_y` の向きへ少し下がり、ボクセル板は後ろへ傾く（字の板とモデルは下がるだけ） |
 * | 浮き上がり | 高さが上がる |
 * | 灰色 | 暗くして灰色の自己発光を足す（色相を落とす近似。モデルは対象外） |
 * | 二重 | ずらした複製をもう 1 つ描く（ボクセル板と字の板。モデルは対象外） |
 *
 * 実体は毎フレーム作り直すので、ここで書き換えても次のフレームには残らない（状態を持たない）。
 */
#pragma once

#include "render/math3d.h"
#include "render/voxel_fx.h"
#include "world/entity_view.h"

#include <vector>

namespace hd2d {

//! `FxEntityMod` から計算した当て方（描画の種類によらない共通の値）。
struct FxModTransform {
    float dx{ 0.f };
    float dy{ 0.f };
    float dz{ 0.f };
    //! 水平方向・高さ方向の拡大率。
    float scale_xy{ 1.f };
    float scale_z{ 1.f };
    //! ボクセル板の後ろへの傾き（ラジアン）。
    float pitch{ 0.f };
    //! 色に掛ける値（1 で変化なし）と、足す自己発光（灰色）。
    float tint{ 1.f };
    float glow{ 0.f };
};

FxModTransform fx_mod_transform(const FxEntityMod &mod);

//! マス (gx,gy) の変化を探す。無ければ `nullptr`。
const FxEntityMod *fx_find_mod(const std::vector<FxEntityMod> &mods, int gx, int gy);

//! ボクセル板と字の板に当てる。変化が空なら何もしない。
void fx_apply_entity_mods(EntityView &view, const std::vector<FxEntityMod> &mods);

//! プレイヤのモデルの置き方（足元 `foot` に変化を当てた行列）。
Mat4 fx_mod_model_matrix(const FxEntityMod &mod, const Vec3 &foot);

} // namespace hd2d
