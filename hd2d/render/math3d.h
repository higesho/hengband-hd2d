/*!
 * @file math3d.h
 * @brief 3D の最小限（ベクトルと 4×4 行列）。ヘッダだけ。
 *
 * @details 外部の数学ライブラリは入れない。
 * 要るのは視点行列・透視投影・掛け算だけで、そのために依存を 1 つ増やす理由が無い。
 *
 * 行列は**列優先**（GLSL と同じ並び）で持つ。`m[col * 4 + row]` である。
 * `glUniformMatrix4fv` に `transpose = GL_FALSE` でそのまま渡せる。
 */
#pragma once

#include <cmath>

namespace hd2d {

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

inline Vec3 operator+(const Vec3 &a, const Vec3 &b) { return Vec3{ a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(const Vec3 &a, const Vec3 &b) { return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(const Vec3 &a, float s) { return Vec3{ a.x * s, a.y * s, a.z * s }; }
inline float dot(const Vec3 &a, const Vec3 &b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return Vec3{ (a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x) };
}
inline Vec3 normalize(const Vec3 &a)
{
    const float len = std::sqrt(dot(a, a));
    return (len > 0.f) ? (a * (1.f / len)) : a;
}

//! 列優先の 4×4。
struct Mat4 {
    float m[16]{};

    static Mat4 identity()
    {
        Mat4 out;
        out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.f;
        return out;
    }
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b)
{
    Mat4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[(k * 4) + row] * b.m[(col * 4) + k];
            }
            out.m[(col * 4) + row] = sum;
        }
    }
    return out;
}

/*!
 * @brief 一般の 4×4 逆行列（余因子展開）。**P10 レビュー 7 で足した。**
 * @details 使うのは**画素からワールド座標へ戻す**ため（被写界深度が「キャラからの
 * 水平距離」で効くのに要る）。1 フレームに 1 回しか呼ばないので、素直な実装でよい。
 * @note 行列が特異なら単位行列を返す（黙って NaN を撒かない）。
 */
inline Mat4 inverse(const Mat4 &a)
{
    const float *const m = a.m;
    float inv[16]{};

    inv[0] = (m[5] * m[10] * m[15]) - (m[5] * m[11] * m[14]) - (m[9] * m[6] * m[15])
        + (m[9] * m[7] * m[14]) + (m[13] * m[6] * m[11]) - (m[13] * m[7] * m[10]);
    inv[4] = (-m[4] * m[10] * m[15]) + (m[4] * m[11] * m[14]) + (m[8] * m[6] * m[15])
        - (m[8] * m[7] * m[14]) - (m[12] * m[6] * m[11]) + (m[12] * m[7] * m[10]);
    inv[8] = (m[4] * m[9] * m[15]) - (m[4] * m[11] * m[13]) - (m[8] * m[5] * m[15])
        + (m[8] * m[7] * m[13]) + (m[12] * m[5] * m[11]) - (m[12] * m[7] * m[9]);
    inv[12] = (-m[4] * m[9] * m[14]) + (m[4] * m[10] * m[13]) + (m[8] * m[5] * m[14])
        - (m[8] * m[6] * m[13]) - (m[12] * m[5] * m[10]) + (m[12] * m[6] * m[9]);
    inv[1] = (-m[1] * m[10] * m[15]) + (m[1] * m[11] * m[14]) + (m[9] * m[2] * m[15])
        - (m[9] * m[3] * m[14]) - (m[13] * m[2] * m[11]) + (m[13] * m[3] * m[10]);
    inv[5] = (m[0] * m[10] * m[15]) - (m[0] * m[11] * m[14]) - (m[8] * m[2] * m[15])
        + (m[8] * m[3] * m[14]) + (m[12] * m[2] * m[11]) - (m[12] * m[3] * m[10]);
    inv[9] = (-m[0] * m[9] * m[15]) + (m[0] * m[11] * m[13]) + (m[8] * m[1] * m[15])
        - (m[8] * m[3] * m[13]) - (m[12] * m[1] * m[11]) + (m[12] * m[3] * m[9]);
    inv[13] = (m[0] * m[9] * m[14]) - (m[0] * m[10] * m[13]) - (m[8] * m[1] * m[14])
        + (m[8] * m[2] * m[13]) + (m[12] * m[1] * m[10]) - (m[12] * m[2] * m[9]);
    inv[2] = (m[1] * m[6] * m[15]) - (m[1] * m[7] * m[14]) - (m[5] * m[2] * m[15])
        + (m[5] * m[3] * m[14]) + (m[13] * m[2] * m[7]) - (m[13] * m[3] * m[6]);
    inv[6] = (-m[0] * m[6] * m[15]) + (m[0] * m[7] * m[14]) + (m[4] * m[2] * m[15])
        - (m[4] * m[3] * m[14]) - (m[12] * m[2] * m[7]) + (m[12] * m[3] * m[6]);
    inv[10] = (m[0] * m[5] * m[15]) - (m[0] * m[7] * m[13]) - (m[4] * m[1] * m[15])
        + (m[4] * m[3] * m[13]) + (m[12] * m[1] * m[7]) - (m[12] * m[3] * m[5]);
    inv[14] = (-m[0] * m[5] * m[14]) + (m[0] * m[6] * m[13]) + (m[4] * m[1] * m[14])
        - (m[4] * m[2] * m[13]) - (m[12] * m[1] * m[6]) + (m[12] * m[2] * m[5]);
    inv[3] = (-m[1] * m[6] * m[11]) + (m[1] * m[7] * m[10]) + (m[5] * m[2] * m[11])
        - (m[5] * m[3] * m[10]) - (m[9] * m[2] * m[7]) + (m[9] * m[3] * m[6]);
    inv[7] = (m[0] * m[6] * m[11]) - (m[0] * m[7] * m[10]) - (m[4] * m[2] * m[11])
        + (m[4] * m[3] * m[10]) + (m[8] * m[2] * m[7]) - (m[8] * m[3] * m[6]);
    inv[11] = (-m[0] * m[5] * m[11]) + (m[0] * m[7] * m[9]) + (m[4] * m[1] * m[11])
        - (m[4] * m[3] * m[9]) - (m[8] * m[1] * m[7]) + (m[8] * m[3] * m[5]);
    inv[15] = (m[0] * m[5] * m[10]) - (m[0] * m[6] * m[9]) - (m[4] * m[1] * m[10])
        + (m[4] * m[2] * m[9]) + (m[8] * m[1] * m[6]) - (m[8] * m[2] * m[5]);

    const float det = (m[0] * inv[0]) + (m[1] * inv[4]) + (m[2] * inv[8]) + (m[3] * inv[12]);
    if ((det > -1e-12f) && (det < 1e-12f)) {
        return Mat4::identity();
    }
    Mat4 out;
    const float k = 1.f / det;
    for (int i = 0; i < 16; ++i) {
        out.m[i] = inv[i] * k;
    }
    return out;
}

inline Mat4 translation(const Vec3 &t)
{
    Mat4 out = Mat4::identity();
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
}

inline Mat4 scaling(float s)
{
    Mat4 out = Mat4::identity();
    out.m[0] = out.m[5] = out.m[10] = s;
    return out;
}

/*!
 * @brief 視点行列。**この世界は左手系**（x 東・y 南・z 上）であることに注意。
 *
 * @details コアの格子は x が東・y が**南**なので、z を上に取ると
 * 「東 × 南 = 下」となり**左手系**である。ここで教科書どおりの右手系の式
 * （`右 = 前 × 上`）を使うと**東西が入れ替わった鏡像**になる。
 *
 * **これは往復検査（画素 → マス → 画素）では絶対に捕まらない。**投影も逆写像も同じ
 * 行列から作るので、世界が丸ごと鏡でも辻褄が合ってしまう。実際 P4 の作業中に
 * `camera_orientation_check()`（東が画面の右に出るか）を足して初めて見つかった。
 *
 * したがって右方向は `上 × 前` で取る。この行列は行列式が負（＝鏡映を含む）なので、
 * **描画側は表面を時計回りとして扱うこと**（`glFrontFace(GL_CW)`）。
 */
inline Mat4 look_at(const Vec3 &eye, const Vec3 &target, const Vec3 &up)
{
    const Vec3 f = normalize(target - eye); // 前
    const Vec3 s = normalize(cross(up, f)); // 右（**左手系なので `上 × 前`**）
    const Vec3 u = cross(f, s); // 実際の上

    Mat4 out = Mat4::identity();
    out.m[0] = s.x;
    out.m[4] = s.y;
    out.m[8] = s.z;
    out.m[1] = u.x;
    out.m[5] = u.y;
    out.m[9] = u.z;
    out.m[2] = -f.x;
    out.m[6] = -f.y;
    out.m[10] = -f.z;
    out.m[12] = -dot(s, eye);
    out.m[13] = -dot(u, eye);
    out.m[14] = dot(f, eye);
    return out;
}

/*!
 * @brief 平行投影。**方向光のシャドウマップ用**（P5 ①）。
 *
 * @details 方向光には収束点が無いので、視錐台ではなく直方体で切り取る。
 * `look_at` と組で使う前提なので、視点空間は **-z が前**（`perspective_horizontal` と同じ約束）。
 *
 * @note `look_at` はこの世界が左手系であるために鏡映を含むが、**それで構わない**。
 * 影の書き込みと読み出しに同じ行列を使う限り、鏡像であることは打ち消し合う。
 * （鏡像かどうかが効くのは「外の世界と合っているか」を見るときだけ。§14-2 の教訓。）
 */
inline Mat4 ortho(float left, float right, float bottom, float top, float z_near, float z_far)
{
    Mat4 out = Mat4::identity();
    out.m[0] = 2.f / (right - left);
    out.m[5] = 2.f / (top - bottom);
    out.m[10] = -2.f / (z_far - z_near);
    out.m[12] = -(right + left) / (right - left);
    out.m[13] = -(top + bottom) / (top - bottom);
    out.m[14] = -(z_far + z_near) / (z_far - z_near);
    return out;
}

/*!
 * @brief 透視投影（**水平**画角で受ける）。
 * @details 設計書 §4.3 の媒介変数が「水平画角」なので、垂直に直さず素直に横で持つ。
 * 深度は GL 既定の [-1, 1]。
 * @param aspect **横 ÷ 縦**（`width / height`）。逆に渡すと縦に潰れる。
 */
inline Mat4 perspective_horizontal(float fov_x_rad, float aspect, float z_near, float z_far)
{
    const float tan_half = std::tan(fov_x_rad * 0.5f);
    Mat4 out;
    out.m[0] = 1.f / tan_half;
    out.m[5] = aspect / tan_half;
    out.m[10] = -(z_far + z_near) / (z_far - z_near);
    out.m[11] = -1.f;
    out.m[14] = -(2.f * z_far * z_near) / (z_far - z_near);
    return out;
}

} // namespace hd2d
