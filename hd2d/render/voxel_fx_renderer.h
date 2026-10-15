/*!
 * @file voxel_fx_renderer.h
 * @brief 攻撃の視覚効果のボクセル（`FxVoxel`）を、1 回の呼び出しでまとめて描く。
 *
 * ## 何を描くか
 * 小さな立方体を多数。位置・大きさ・色・不透明度・自己発光・向き（`yaw`/`pitch`）を
 * インスタンスごとに持つ。立方体の頂点は 24 個の固定のメッシュ 1 つで、使い回す。
 *
 * ## 光
 * 地形と**同じ式**（`render/lighting.h` の `kLightingGlsl`）で方向光・環境光・点光源・影を受ける。
 * そこへ自己発光（色 × `emissive`）を、光に関係なく足す。トーンマップは掛けない
 * （線形の HDR を書く。ブルームが掴むのは自己発光の強い部分）。
 *
 * ## 深度
 * **読む。**不透明なものは書き、半透明なもの（煙・薄れる矢・火の粉の終わり）は書かない。
 * 半透明は奥から手前の順に混ぜるので、描く前に CPU で距離を測って並べる。
 *
 * ## GLES
 * 他のシェーダと同じく `#version 460 core` で書き、`compile_program` が読み替える。
 * サンプラは光の式が持つ 2 つ（影・光の届く範囲の場）だけで、`upload_lighting()` が
 * 使わないときも身代わりを結ぶ。**単位は影が 2、場が 5**（`VoxelRenderer` と同じ）。
 */
#pragma once

#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/voxel_fx.h"
#include "render/voxel_renderer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hd2d {

class VoxelFxRenderer {
public:
    bool init(std::string &err);
    void shutdown();
    bool ready() const { return this->program_ != 0; }

    /*!
     * @brief 描く。**世界を描いた後・後処理の前**（線形の HDR の FBO が張られている間）に呼ぶ。
     * @param eye カメラの位置（半透明を並べる基準）
     * @param lighting 地形と同じものを渡す（点光源に `collect_point_lights()` の `extra_lights` が入っていること）
     * @return 描いたボクセルの数（0 なら何もしていない）
     */
    std::size_t draw(const Mat4 &view_projection, const Vec3 &eye, const SceneLighting &lighting,
        const Mat4 &light_view_projection, gl::GLuint shadow_texture, int shadow_side, const FxVoxel *voxels,
        std::size_t count);

private:
    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint cube_vbo_{ 0 };
    gl::GLuint cube_ebo_{ 0 };
    gl::GLuint instance_vbo_{ 0 };
    std::size_t instance_capacity_{ 0 };
    gl::GLint loc_view_projection_{ -1 };
    LightUniforms light_loc_;
    std::vector<float> scratch_;
};

/*!
 * @brief 投げた物の板 1 枚ぶんの、GPU 側の持ち物（`SlabLibrary` が持っているもの）。
 * @details `minimum`・`maximum` は板のプレハブ空間（マス）の外接の箱。回す中心（板の中心）を決めるのに使う。
 */
struct FxSlabSource {
    GpuPrefab *gpu{ nullptr };
    Vec3 minimum{};
    Vec3 maximum{};
};

//! `item_id` から板を引く（床のアイテムを描くのと同じ道。引けなければ偽）。
using FxSlabResolver = std::function<bool(std::uint16_t item_id, FxSlabSource &out)>;

/*!
 * @brief 投げた物の板（`FxFrame::slabs`）を描く。**床のアイテムと同じ `VoxelRenderer` で、同じ光・同じ板**。
 * @details 板は中心を軸に、面内で `spin` だけ回し、画面へ正対（`face_yaw`/`face_pitch`）させる。
 * 回転は**パーツの動きの行列**（プレハブ空間・拡大の前）で与えるので、`InstanceData` の向きは使わない。
 * `renderer.begin()` を済ませた後（世界を描く最中）に呼ぶ。影の地図へは書かない。
 * @return 描いた板の数。
 */
std::size_t draw_fx_slabs(VoxelRenderer &renderer, const std::vector<FxSlab> &slabs, const FxSlabResolver &resolve);

} // namespace hd2d
