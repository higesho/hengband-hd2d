/*!
 * @file greedy_mesher.cpp
 * @brief `greedy_mesher.h` の実装。
 */
#include "voxel/greedy_mesher.h"

#include <algorithm>
#include <array>

namespace hd2d {

namespace {

//! まとめた四角形 1 枚（アトラスへ詰める前の姿）。
struct Quad {
    int axis{ 0 }; //!< 面の法線の軸（0=x 1=y 2=z）
    int sign{ 1 }; //!< +1 / -1
    int slice{ 0 }; //!< 法線の軸に沿った層（ボクセルの座標）
    int p0{ 0 }; //!< 高さ方向の始まり
    int q0{ 0 }; //!< 幅方向の始まり
    int h{ 1 }; //!< 高さ（p 方向のマス数）
    int w{ 1 }; //!< 幅（q 方向のマス数）
    int atlas_x{ 0 };
    int atlas_y{ 0 };
};

/*!
 * @brief 軸 `a` に対する「高さの軸 p」と「幅の軸 q」。
 *
 * @details **ここが検算の要である。**まとめる順序（どちらの軸を先に伸ばすか）で
 * 四角形の数が変わるので、`tools/voxel_probe/greedy.py` と同じ取り方をしなければならない。
 * あちらは `np.moveaxis(vol, axis, 0)` で法線の軸を先頭へ持ってくるだけなので、
 * **残りの 2 軸は元の順序のまま**になり、行 = 小さい方の軸・列 = 大きい方の軸になる。
 * 幅（＝先に伸ばす方）は列、つまり**残った 2 軸のうち番号の大きい方**。
 *
 * | 法線の軸 | 行 p（高さ） | 列 q（幅） |
 * |---|---|---|
 * | 0 (x) | 1 (y) | 2 (z) |
 * | 1 (y) | 0 (x) | 2 (z) |
 * | 2 (z) | 0 (x) | 1 (y) |
 */
void plane_axes(int a, int &p, int &q)
{
    p = (a == 0) ? 1 : 0;
    q = (a == 2) ? 1 : 2;
}

/*!
 * @brief 角の並び (q, p) → (w,0) → (w,h) → (0,h) が作る法線が `+e_a` を向くか。
 * @details `E_q × E_p` の符号。a=0 で -1、a=1 で +1、a=2 で -1 になる。
 * 表裏を取り違えると背面カリングで消えるので、ここは式で決めて実物で確かめる。
 */
int winding_sign(int a)
{
    return (a == 1) ? +1 : -1;
}

//! 棚詰め（shelf packing）。高い四角形から順に詰める。
bool pack_atlas(std::vector<Quad> &quads, int side)
{
    int pen_x = 0;
    int pen_y = 0;
    int shelf_h = 0;
    for (auto &quad : quads) {
        if ((quad.w > side) || (quad.h > side)) {
            return false;
        }
        if ((pen_x + quad.w) > side) {
            pen_x = 0;
            pen_y += shelf_h;
            shelf_h = 0;
        }
        if ((pen_y + quad.h) > side) {
            return false;
        }
        quad.atlas_x = pen_x;
        quad.atlas_y = pen_y;
        pen_x += quad.w;
        shelf_h = std::max(shelf_h, quad.h);
    }
    return true;
}

} // namespace

bool downsample_model(const VoxModel &model, int level, VoxModel &out, std::string &err)
{
    if (level < 0) {
        err = "LOD の段が負です";
        return false;
    }
    if (level == 0) {
        out = model;
        return true;
    }
    const int step = 1 << level;
    out.size[0] = std::max(1, (model.size[0] + step - 1) / step);
    out.size[1] = std::max(1, (model.size[1] + step - 1) / step);
    out.size[2] = std::max(1, (model.size[2] + step - 1) / step);
    out.voxels.assign(static_cast<std::size_t>(out.size[0]) * static_cast<std::size_t>(out.size[1])
            * static_cast<std::size_t>(out.size[2]),
        0);

    const int block = step * step * step;
    std::array<int, 256> histogram{};
    for (int z = 0; z < out.size[2]; ++z) {
        for (int y = 0; y < out.size[1]; ++y) {
            for (int x = 0; x < out.size[0]; ++x) {
                histogram.fill(0);
                int filled = 0;
                for (int dz = 0; dz < step; ++dz) {
                    for (int dy = 0; dy < step; ++dy) {
                        for (int dx = 0; dx < step; ++dx) {
                            const int sx = (x * step) + dx;
                            const int sy = (y * step) + dy;
                            const int sz = (z * step) + dz;
                            if ((sx >= model.size[0]) || (sy >= model.size[1]) || (sz >= model.size[2])) {
                                continue;
                            }
                            const std::uint8_t index = model.at(sx, sy, sz);
                            if (index != 0) {
                                ++filled;
                                ++histogram[index];
                            }
                        }
                    }
                }
                if ((filled * 2) < block) {
                    continue; // 半分未満は空（疎な形を膨らませない）
                }
                int best = 0;
                int best_count = 0;
                for (int i = 1; i < 256; ++i) {
                    if (histogram[static_cast<std::size_t>(i)] > best_count) {
                        best_count = histogram[static_cast<std::size_t>(i)];
                        best = i;
                    }
                }
                out.voxels[static_cast<std::size_t>(x)
                    + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.size[0]))
                    + (static_cast<std::size_t>(z) * static_cast<std::size_t>(out.size[0]) * static_cast<std::size_t>(out.size[1]))]
                    = static_cast<std::uint8_t>(best);
            }
        }
    }
    return true;
}

bool mesh_model(const VoxModel &model, VoxelMesh &out, std::string &err, bool hide_unseen_from_above)
{
    const int size[3] = { model.size[0], model.size[1], model.size[2] };
    if ((size[0] <= 0) || (size[1] <= 0) || (size[2] <= 0)) {
        err = "模型の寸法が不正です";
        return false;
    }

    auto solid = [&model, &size](int x, int y, int z) -> bool {
        if ((x < 0) || (y < 0) || (z < 0) || (x >= size[0]) || (y >= size[1]) || (z >= size[2])) {
            return false;
        }
        return model.at(x, y, z) != 0;
    };

    /*
     * **空から届く空間**（`hide_unseen_from_above`。ヘッダの説明）。
     * いちばん上の面の空のボクセルを種にして、空だけを 6 方向へ塗り広げる。
     * 旗が立っていなければ表そのものを作らない（費用は 0）。
     */
    std::vector<std::uint8_t> lit;
    auto lit_at = [&lit, &size](int x, int y, int z) -> bool {
        return lit[(static_cast<std::size_t>(z) * static_cast<std::size_t>(size[0])
                       * static_cast<std::size_t>(size[1]))
            + (static_cast<std::size_t>(y) * static_cast<std::size_t>(size[0]))
            + static_cast<std::size_t>(x)]
            != 0;
    };
    if (hide_unseen_from_above) {
        const std::size_t count = static_cast<std::size_t>(size[0]) * static_cast<std::size_t>(size[1])
            * static_cast<std::size_t>(size[2]);
        lit.assign(count, std::uint8_t{ 0 });
        std::vector<std::array<int, 3>> stack;
        auto seed = [&](int x, int y, int z) {
            if (solid(x, y, z) || lit_at(x, y, z)) {
                return;
            }
            lit[(static_cast<std::size_t>(z) * static_cast<std::size_t>(size[0])
                    * static_cast<std::size_t>(size[1]))
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(size[0]))
                + static_cast<std::size_t>(x)]
                = 1;
            stack.push_back({ x, y, z });
        };
        for (int y = 0; y < size[1]; ++y) {
            for (int x = 0; x < size[0]; ++x) {
                seed(x, y, size[2] - 1);
            }
        }
        static const int kStep[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
            { 0, 0, -1 } };
        while (!stack.empty()) {
            const std::array<int, 3> here = stack.back();
            stack.pop_back();
            for (const auto &d : kStep) {
                const int nx = here[0] + d[0];
                const int ny = here[1] + d[1];
                const int nz = here[2] + d[2];
                if ((nx < 0) || (ny < 0) || (nz < 0) || (nx >= size[0]) || (ny >= size[1]) || (nz >= size[2])) {
                    continue;
                }
                seed(nx, ny, nz);
            }
        }
    }
    auto index_at = [&model](int x, int y, int z) -> std::uint8_t { return model.at(x, y, z); };

    std::vector<Quad> quads;
    std::size_t naive = 0;
    std::vector<std::uint8_t> mask;

    for (int a = 0; a < 3; ++a) {
        int p = 0;
        int q = 0;
        plane_axes(a, p, q);
        const int height = size[p];
        const int width = size[q];
        mask.resize(static_cast<std::size_t>(height) * static_cast<std::size_t>(width));

        for (int sign_i = 0; sign_i < 2; ++sign_i) {
            const int sign = (sign_i == 0) ? +1 : -1;
            for (int k = 0; k < size[a]; ++k) {
                std::fill(mask.begin(), mask.end(), std::uint8_t{ 0 });
                for (int j = 0; j < height; ++j) {
                    for (int i = 0; i < width; ++i) {
                        int here[3]{};
                        here[a] = k;
                        here[p] = j;
                        here[q] = i;
                        if (!solid(here[0], here[1], here[2])) {
                            continue;
                        }
                        int outward[3] = { here[0], here[1], here[2] };
                        outward[a] += sign;
                        if (solid(outward[0], outward[1], outward[2])) {
                            continue; // 隣が詰まっている＝この面は見えない
                        }
                        if (hide_unseen_from_above) {
                            const bool inside = (outward[0] >= 0) && (outward[1] >= 0) && (outward[2] >= 0)
                                && (outward[0] < size[0]) && (outward[1] < size[1]) && (outward[2] < size[2]);
                            if (!inside) {
                                //! 箱の外。**上（+z）だけ残す**——横は隣のマス、下は地面の下である。
                                if (!((a == 2) && (sign > 0))) {
                                    continue;
                                }
                            } else if (!lit_at(outward[0], outward[1], outward[2])) {
                                continue; //!< 空から届かない空間に向いている＝見えない
                            }
                        }
                        mask[(static_cast<std::size_t>(j) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(i)] = 1;
                        ++naive;
                    }
                }

                // greedy.py と同じ走査: 行ごとに幅を伸ばし、そのあと高さを伸ばす。
                for (int j = 0; j < height; ++j) {
                    int i = 0;
                    while (i < width) {
                        const std::size_t base = (static_cast<std::size_t>(j) * static_cast<std::size_t>(width));
                        if (mask[base + static_cast<std::size_t>(i)] == 0) {
                            ++i;
                            continue;
                        }
                        int w = 1;
                        while (((i + w) < width) && (mask[base + static_cast<std::size_t>(i + w)] != 0)) {
                            ++w;
                        }
                        int h = 1;
                        for (;;) {
                            if ((j + h) >= height) {
                                break;
                            }
                            const std::size_t row = (static_cast<std::size_t>(j + h) * static_cast<std::size_t>(width));
                            bool full = true;
                            for (int d = 0; d < w; ++d) {
                                if (mask[row + static_cast<std::size_t>(i + d)] == 0) {
                                    full = false;
                                    break;
                                }
                            }
                            if (!full) {
                                break;
                            }
                            ++h;
                        }
                        for (int dj = 0; dj < h; ++dj) {
                            const std::size_t row = (static_cast<std::size_t>(j + dj) * static_cast<std::size_t>(width));
                            for (int di = 0; di < w; ++di) {
                                mask[row + static_cast<std::size_t>(i + di)] = 0;
                            }
                        }
                        Quad quad;
                        quad.axis = a;
                        quad.sign = sign;
                        quad.slice = k;
                        quad.p0 = j;
                        quad.q0 = i;
                        quad.h = h;
                        quad.w = w;
                        quads.push_back(quad);
                        i += w;
                    }
                }
            }
        }
    }

    out.quad_count = quads.size();
    out.naive_face_count = naive;
    if (quads.empty()) {
        out.atlas_w = out.atlas_h = 0;
        return true;
    }

    // --- アトラスへ詰める（高い四角形から。棚の隙間を減らす） ---
    std::vector<std::size_t> order(quads.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(),
        [&quads](std::size_t l, std::size_t r) { return quads[l].h > quads[r].h; });
    std::vector<Quad> sorted;
    sorted.reserve(quads.size());
    for (const std::size_t i : order) {
        sorted.push_back(quads[i]);
    }

    int side = 64;
    while (!pack_atlas(sorted, side)) {
        side *= 2;
        if (side > 8192) {
            err = "面アトラスに入り切りません（8192×8192 を超えました）";
            return false;
        }
    }
    out.atlas_w = side;
    out.atlas_h = side;
    out.atlas.assign(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 2, 0);

    // --- 頂点とアトラスの中身 ---
    out.vertices.clear();
    out.indices.clear();
    out.vertices.reserve(sorted.size() * 4);
    out.indices.reserve(sorted.size() * 6);

    for (const auto &quad : sorted) {
        int p = 0;
        int q = 0;
        plane_axes(quad.axis, p, q);

        // アトラス: テクセル (atlas_x + di, atlas_y + dj) が面 (q0 + di, p0 + dj) を持つ。
        for (int dj = 0; dj < quad.h; ++dj) {
            for (int di = 0; di < quad.w; ++di) {
                int here[3]{};
                here[quad.axis] = quad.slice;
                here[p] = quad.p0 + dj;
                here[q] = quad.q0 + di;

                // AO: 面の外側の隣（空）の、面と平行な 8 近傍がどれだけ詰まっているか。
                int outward[3] = { here[0], here[1], here[2] };
                outward[quad.axis] += quad.sign;
                int occluders = 0;
                for (int dp = -1; dp <= 1; ++dp) {
                    for (int dq = -1; dq <= 1; ++dq) {
                        if ((dp == 0) && (dq == 0)) {
                            continue;
                        }
                        int probe[3] = { outward[0], outward[1], outward[2] };
                        probe[p] += dp;
                        probe[q] += dq;
                        if (solid(probe[0], probe[1], probe[2])) {
                            ++occluders;
                        }
                    }
                }
                const float ao = 1.f - (0.55f * (static_cast<float>(occluders) / 8.f));
                const std::size_t texel = ((static_cast<std::size_t>(quad.atlas_y + dj) * static_cast<std::size_t>(side))
                                              + static_cast<std::size_t>(quad.atlas_x + di))
                    * 2;
                out.atlas[texel + 0] = index_at(here[0], here[1], here[2]);
                out.atlas[texel + 1] = static_cast<std::uint8_t>(ao * 255.f);
            }
        }

        // 頂点。角は (q, p) で (0,0) → (w,0) → (w,h) → (0,h)。
        const float plane = static_cast<float>(quad.slice) + ((quad.sign > 0) ? 1.f : 0.f);
        const std::array<std::array<int, 2>, 4> corners = { { { 0, 0 }, { quad.w, 0 }, { quad.w, quad.h }, { 0, quad.h } } };
        VoxelVertex built[4]{};
        for (int c = 0; c < 4; ++c) {
            float position[3]{};
            position[quad.axis] = plane;
            position[q] = static_cast<float>(quad.q0 + corners[static_cast<std::size_t>(c)][0]);
            position[p] = static_cast<float>(quad.p0 + corners[static_cast<std::size_t>(c)][1]);
            VoxelVertex vertex;
            vertex.px = position[0];
            vertex.py = position[1];
            vertex.pz = position[2];
            vertex.nx = (quad.axis == 0) ? static_cast<float>(quad.sign) : 0.f;
            vertex.ny = (quad.axis == 1) ? static_cast<float>(quad.sign) : 0.f;
            vertex.nz = (quad.axis == 2) ? static_cast<float>(quad.sign) : 0.f;
            vertex.u = static_cast<float>(quad.atlas_x + corners[static_cast<std::size_t>(c)][0]);
            vertex.v = static_cast<float>(quad.atlas_y + corners[static_cast<std::size_t>(c)][1]);
            /*
             * 風のしなやかさ（§7.1-3「自動＝そのパーツの底面からの高さ」）。
             * **模型の高さで正規化する**ので、背の低い草も背の高い木も根元 0・先端 1 になる。
             * 二乗するのは、根元近くをもっと固くするため（線形だと草の株元が滑って見える）。
             */
            const float height = (model.size[2] > 1) ? (position[2] / static_cast<float>(model.size[2] - 1)) : 0.f;
            const float clamped = (height < 0.f) ? 0.f : ((height > 1.f) ? 1.f : height);
            vertex.flex = clamped * clamped;
            /*
             * 四角形の縁までの距離（テクセル。`VoxelVertex::e0` の注記）。
             * 角の局所座標そのものと、幅・高さからの引き算。**4 本とも 1 次式**なので、
             * 補間した値が四角形の中のどこでも正しい距離になる。
             */
            const float lu = static_cast<float>(corners[static_cast<std::size_t>(c)][0]);
            const float lv = static_cast<float>(corners[static_cast<std::size_t>(c)][1]);
            vertex.e0 = lu;
            vertex.e1 = static_cast<float>(quad.w) - lu;
            vertex.e2 = lv;
            vertex.e3 = static_cast<float>(quad.h) - lv;
            built[c] = vertex;
        }

        const auto first = static_cast<std::uint32_t>(out.vertices.size());
        // 角の並びが作る法線が外向きと逆なら、並びをひっくり返す（背面カリングで消えないように）。
        if (winding_sign(quad.axis) == quad.sign) {
            for (const auto &vertex : built) {
                out.vertices.push_back(vertex);
            }
        } else {
            for (int c = 3; c >= 0; --c) {
                out.vertices.push_back(built[c]);
            }
        }
        out.indices.push_back(first + 0);
        out.indices.push_back(first + 1);
        out.indices.push_back(first + 2);
        out.indices.push_back(first + 0);
        out.indices.push_back(first + 2);
        out.indices.push_back(first + 3);
    }
    return true;
}

} // namespace hd2d
