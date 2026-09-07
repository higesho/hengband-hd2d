/*!
 * @file frustum.h
 * @brief 視錐台の 6 平面と直方体の交差判定（P3 ⑤ の視錐台カリング）。ヘッダだけ。
 *
 * @details 視点射影行列から平面を取り出す標準的なやり方（行の和と差）。
 * **保守的**（見えていないものを「見える」と言うことはあるが、見えているものを
 * 「見えない」とは言わない）ので、これで消えたら本当に画面の外にある。
 */
#pragma once

#include "render/math3d.h"

#include <cmath>

namespace hd2d {

struct Frustum {
    //! `a*x + b*y + c*z + d >= 0` が内側。
    float plane[6][4]{};

    //! 列優先の視点射影行列から作る。
    static Frustum from(const Mat4 &vp)
    {
        auto row = [&vp](int r, int c) { return vp.m[(c * 4) + r]; };
        Frustum out;
        const int order[6][2] = { { 0, +1 }, { 0, -1 }, { 1, +1 }, { 1, -1 }, { 2, +1 }, { 2, -1 } };
        for (int i = 0; i < 6; ++i) {
            const int axis = order[i][0];
            const float sign = static_cast<float>(order[i][1]);
            for (int c = 0; c < 4; ++c) {
                out.plane[i][c] = row(3, c) + (sign * row(axis, c));
            }
            const float length = std::sqrt((out.plane[i][0] * out.plane[i][0])
                + (out.plane[i][1] * out.plane[i][1]) + (out.plane[i][2] * out.plane[i][2]));
            if (length > 1e-8f) {
                for (int c = 0; c < 4; ++c) {
                    out.plane[i][c] /= length;
                }
            }
        }
        return out;
    }

    /*!
     * @brief **何も切らない**視錐台。
     * @details 平面が全部 0 だと `intersects` の判定式が常に `0 >= 0` になり、どの直方体も
     * 通る。VR で要る——頭はどこからでも盤を覗けるので、**平らな画面のカメラの視錐台で
     * 切ると、覗き込んだ先が抜けている**。
     * 既定構築でも同じ形になるが、「切らない」を明示したくて名前を付けた。
     */
    static Frustum everything() { return Frustum{}; }

    /*!
     * @brief 水平な矩形で切る（**VR の卓**。2026-08-14 に決めた）。
     * @param min_x,min_y,max_x,max_y 世界（マス）の範囲。
     * @details 「テーブルの範囲がマップの描画範囲として、テーブルからはみ出ると
     * マップは消えてほしい」。上下は切らない（4 面だけ使い、残り 2 面は 0 のまま
     * ＝常に内側）。ジオラマでは**視錐台の代わりにこれを渡す**——頭はどこからでも
     * 覗けるので、平らなカメラの視錐台では切れない（罠 24）。
     */
    static Frustum slab_xy(float min_x, float min_y, float max_x, float max_y)
    {
        Frustum out;
        //! `a*x + b*y + c*z + d >= 0` が内側。x ≧ min / x ≦ max / y ≧ min / y ≦ max。
        out.plane[0][0] = 1.f;
        out.plane[0][3] = -min_x;
        out.plane[1][0] = -1.f;
        out.plane[1][3] = max_x;
        out.plane[2][1] = 1.f;
        out.plane[2][3] = -min_y;
        out.plane[3][1] = -1.f;
        out.plane[3][3] = max_y;
        return out;
    }

    //! 軸に沿った直方体が視錐台と交わるか（触れていれば true）。
    bool intersects(const Vec3 &min_corner, const Vec3 &max_corner) const
    {
        for (const auto &p : this->plane) {
            // 平面の法線から見て「最も内側にある角」が外なら、直方体は丸ごと外。
            const float x = (p[0] >= 0.f) ? max_corner.x : min_corner.x;
            const float y = (p[1] >= 0.f) ? max_corner.y : min_corner.y;
            const float z = (p[2] >= 0.f) ? max_corner.z : min_corner.z;
            if (((p[0] * x) + (p[1] * y) + (p[2] * z) + p[3]) < 0.f) {
                return false;
            }
        }
        return true;
    }
};

} // namespace hd2d
