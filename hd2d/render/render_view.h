/*!
 * @file render_view.h
 * @brief 「1 回ぶんの視点」——シーンを描くのに要る、視点まわりの値だけを束ねたもの。
 *
 *
 * ## なぜ `Camera` ではなくこれを渡すのか
 * フラットは `Camera` から視点行列を作れるが、**VR の視点は `Camera` から作れない**——
 * 位置も画角もランタイムが決めるからである。そこでシーンを描く側は
 * 「どこから・どの向きに・どんな画角で・どの大きさへ」だけを受け取ることにして、
 * フラットは 1 個・VR は左右で 2 個作って**同じ関数へ回す**。
 *
 * `Camera` 構造体そのものには触らない（可視窓の導出・クリック移動の逆写像・
 * 被写界深度の軸は今までどおり `Camera` が持つ）。
 */
#pragma once

#include "render/math3d.h"

namespace hd2d {

struct RenderView {
    //! 視点行列（世界 → 視点）。
    Mat4 view{ Mat4::identity() };
    //! 射影行列。VR では**非対称**（`xr::projection_from_fov`）。
    Mat4 projection{ Mat4::identity() };
    //! 目の位置（世界座標・マス）。鏡面や減衰の計算がここを読む。
    Vec3 eye{ 0.f, 0.f, 0.f };
    /*!
     * @brief かきわり（ビルボード）を向ける水平角（ラジアン）。
     * @details **VR では左右の目で同じ値を渡すこと。**目ごとに別の角を向けると
     * 板が目ごとにねじれて立体視が壊れる。
     */
    float azimuth{ 0.f };
    //! 描き先の大きさ（画素）。
    int width{ 1 };
    int height{ 1 };
    //! 深度範囲。
    float z_near{ 0.1f };
    float z_far{ 100.f };
    /*!
     * @name VR の部屋と卓
     *
     * @details VR には**世界（マス）ではない物**が 2 つある——利用者のまわりの
     * 暗い部屋と、ジオラマを載せる卓である。どちらもステージ空間（メートル）に
     * 直に置かれるので、盤の写像（`world_to_stage`）を掛けない視点行列が要る。
     * フラットでは `stage_valid` が偽のまま使われない。
     * @{
     */
    Mat4 stage_view{ Mat4::identity() };
    bool stage_valid{ false };
    /*! @} */

    Mat4 view_projection() const { return this->projection * this->view; }
};

} // namespace hd2d
