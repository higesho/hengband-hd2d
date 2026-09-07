/*!
 * @file asset_checks.cpp
 * @brief プレハブと材質の検査（`--prefab-check=` / `--material-check`）。窓も GL も要らない。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * **遊ぶときには 1 行も通らない**——`run()` の頭で検査のモードを見て、
 * そのまま返る経路にしか無い。分けたのは本体が 17,981 行あって読めなかったため。
 *
 * 宣言は `app/hd2d_checks.h`。振る舞いが変わっていないことは
 * `python tools/hd2d_verify/golden.py --check` で見る。
 */
#include "app/hd2d_checks.h"

#include "render/surface_wear.h"
#include "voxel/greedy_mesher.h"
#include "voxel/prefab.h"
#include "world/prefab_library.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hd2d {

/*!
 * @brief `--material-check`: **ライブラリぜんぶ**の材質の推定を数える（GL 不要）。
 *
 * @details 材質は**パレットの色から**引いている（`render/surface_wear.h`）。
 * 色はライブラリじゅうで使い回されているので、**同じ色が別の材に使われていれば必ず外れる**
 * （2026-08-23 に気づいた:「幻想や Sil-Q では意図しない箇所に石や土としての
 * 汚れが入っていないか」）。当てずっぽうで答えないための手段である。
 *
 * 出すのは 2 つ。
 *
 * 1. ライブラリぜんぶの内訳（材質ごとのボクセル数）
 * 2. **`default` が半分を超えるプレハブ**——材質が推定できていない＝弱い汚しだけが
 *    掛かっているもの。意匠だけの色（`reg_local`）で作った町や洞がここへ出る
 *
 * 引くのは描き手と同じ `material_for_color` そのもの（写さない）。
 */
int run_material_check(const AppOptions &options)
{
    PrefabLibrary library;
    std::string err;
    if (!library.load(options.voxel_dir, err)) {
        std::fprintf(stderr, "[hd2d] ライブラリを読めませんでした: %s\n", err.c_str());
        return 1;
    }
    std::array<std::size_t, static_cast<std::size_t>(MaterialClass::Count)> total{};
    //! **宣言で引けたプレハブ／色から当てたプレハブ**（2026-08-23 に決めた でこちらが正になった）。
    std::size_t declared_books = 0;
    struct Weak {
        std::string name;
        std::size_t unknown;
        std::size_t voxels;
    };
    std::vector<Weak> weak;
    for (int i = 0; i < library.count(); ++i) {
        const LibraryEntry &entry = library.entry(i);
        std::array<std::size_t, static_cast<std::size_t>(MaterialClass::Count)> tally{};
        std::size_t voxels = 0;
        if (entry.prefab.palette_material_declared) {
            ++declared_books;
        }
        for (const auto &part : entry.prefab.parts) {
            const VoxModel &model = entry.prefab.vox.models[static_cast<std::size_t>(part.model_index)];
            for (const std::uint8_t index : model.voxels) {
                if (index == 0u) {
                    continue;
                }
                //! **宣言が正**。無いものだけ色から当てる（`upload_meshed` と同じ順）。
                std::size_t mat = entry.prefab.palette_material[index];
                if (!entry.prefab.palette_material_declared) {
                    const std::uint8_t *rgb = entry.prefab.vox.palette[index];
                    mat = static_cast<std::size_t>(material_for_color(rgb[0], rgb[1], rgb[2]));
                }
                ++tally[mat];
                ++voxels;
            }
        }
        for (std::size_t k = 0; k < tally.size(); ++k) {
            total[k] += tally[k];
        }
        const std::size_t unknown = tally[static_cast<std::size_t>(MaterialClass::Default)];
        if ((voxels > 0) && ((unknown * 2) > voxels)) {
            weak.push_back(Weak{ entry.name, unknown, voxels });
        }
    }
    std::size_t all = 0;
    for (const std::size_t n : total) {
        all += n;
    }
    std::fprintf(stderr, "[hd2d] material-check: プレハブ %d 個・ボクセル %zu 個\n", library.count(), all);
    std::fprintf(stderr, "[hd2d]   材質を宣言しているプレハブ: %zu / %d（残りは色から当てる予備の道）\n",
        declared_books, library.count());
    for (std::size_t k = 0; k < total.size(); ++k) {
        if (all == 0) {
            break;
        }
        std::fprintf(stderr, "[hd2d]   %-8s %10zu (%.1f%%)\n", material_class_name(static_cast<MaterialClass>(k)),
            total[k], (100.0 * static_cast<double>(total[k])) / static_cast<double>(all));
    }
    /*
     * **材質を推定できない色を多い順に出す。**「どの色を埋めれば効くか」が分からないと、
     * 144 件の名前を端から眺めることになる（実際に 1 度そうなった）。
     */
    {
        std::map<std::uint32_t, std::size_t> unknown_colors;
        for (int i = 0; i < library.count(); ++i) {
            const LibraryEntry &entry = library.entry(i);
            for (const auto &part : entry.prefab.parts) {
                const VoxModel &model = entry.prefab.vox.models[static_cast<std::size_t>(part.model_index)];
                for (const std::uint8_t index : model.voxels) {
                    if (index == 0u) {
                        continue;
                    }
                    const std::uint8_t *rgb = entry.prefab.vox.palette[index];
                    const std::size_t mat = entry.prefab.palette_material_declared
                        ? static_cast<std::size_t>(entry.prefab.palette_material[index])
                        : static_cast<std::size_t>(material_for_color(rgb[0], rgb[1], rgb[2]));
                    if (mat != static_cast<std::size_t>(MaterialClass::Default)) {
                        continue;
                    }
                    const std::uint32_t key = (static_cast<std::uint32_t>(rgb[0]) << 16)
                        | (static_cast<std::uint32_t>(rgb[1]) << 8) | static_cast<std::uint32_t>(rgb[2]);
                    unknown_colors[key] += 1;
                }
            }
        }
        std::vector<std::pair<std::uint32_t, std::size_t>> sorted_colors(unknown_colors.begin(), unknown_colors.end());
        std::sort(sorted_colors.begin(), sorted_colors.end(),
            [](const auto &l, const auto &r) { return l.second > r.second; });
        std::fprintf(stderr, "[hd2d]   材質を推定できない色: %zu 色（多い順に 15 色）\n", sorted_colors.size());
        const std::size_t top = (sorted_colors.size() < 15) ? sorted_colors.size() : 15;
        for (std::size_t k = 0; k < top; ++k) {
            std::fprintf(stderr, "[hd2d]     #%06X  %zu\n", sorted_colors[k].first, sorted_colors[k].second);
        }
    }
    std::sort(weak.begin(), weak.end(), [](const Weak &l, const Weak &r) { return l.unknown > r.unknown; });
    std::fprintf(stderr, "[hd2d]   材質を推定できないプレハブ: %zu 個（default が半分超）\n", weak.size());
    const std::size_t show = (weak.size() < 20) ? weak.size() : 20;
    for (std::size_t k = 0; k < show; ++k) {
        std::fprintf(stderr, "[hd2d]     %-28s default %zu / %zu\n", weak[k].name.c_str(), weak[k].unknown, weak[k].voxels);
    }
    return 0;
}

/*!
 * @brief 窓を出さずにメッシュ化だけして数を出す（`--prefab-check=`）。
 * @details **`tools/voxel_probe/greedy.py` の第 3 列と突き合わせるための手段**である
 * （計画 P1 の検証）。GL を触らないので CI からも回せる。
 * 検査（footprint・ピボット・親子の環）もここを通る。
 */
int run_prefab_check(const AppOptions &options)
{
    Prefab prefab;
    std::string err;
    if (!load_prefab(options.voxel_dir, options.prefab_name, prefab, err)) {
        std::fprintf(stderr, "[hd2d] プレハブを読めませんでした: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "[hd2d] prefab \"%s\": parts=%zu footprint=%zu cells voxels_per_cell=%d\n",
        prefab.name.c_str(), prefab.parts.size(), prefab.footprint.size(), prefab.voxels_per_cell);

    /*
     * **材質の内訳**（`render/surface_wear.h`）。汚しと木の葉は材質で掛け方を変えるので、
     * 「この模型のこの色が何に見なされているか」を出す手段が要る。
     *
     * **描く手順は写さない**（記憶 `hengband-check-must-not-copy-the-draw`）——
     * 引くのは描き手と同じ `material_for_color` そのものである。ここで写した表を
     * 別に持つと、片方だけ直したときに検査が嘘をつく。
     */
    {
        std::array<std::size_t, static_cast<std::size_t>(MaterialClass::Count)> tally{};
        std::size_t counted = 0;
        for (const auto &part : prefab.parts) {
            const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
            for (const std::uint8_t index : model.voxels) {
                if (index == 0u) {
                    continue;
                }
                //! **宣言が正**（`.jsonc` の `palette`）。無いものだけ色から当てる。
                std::size_t mat = prefab.palette_material[index];
                if (!prefab.palette_material_declared) {
                    const std::uint8_t *rgb = prefab.vox.palette[index];
                    mat = static_cast<std::size_t>(material_for_color(rgb[0], rgb[1], rgb[2]));
                }
                ++tally[mat];
                ++counted;
            }
        }
        std::fprintf(stderr, "[hd2d]   材質（ボクセル %zu 個・%s）:", counted,
            prefab.palette_material_declared ? "宣言" : "色から");
        for (std::size_t i = 0; i < tally.size(); ++i) {
            if (tally[i] == 0) {
                continue;
            }
            std::fprintf(stderr, " %s=%zu", material_class_name(static_cast<MaterialClass>(i)), tally[i]);
        }
        std::fprintf(stderr, "\n");
    }

    std::size_t total_quads = 0;
    std::size_t total_naive = 0;
    for (const auto &part : prefab.parts) {
        const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        VoxelMesh mesh;
        //! **描くときと同じ旗で数える**（記憶「検査で描画手順を写さない」）。
        if (!mesh_model(model, mesh, err, prefab.hide_unseen_from_above)) {
            std::fprintf(stderr, "[hd2d] メッシュにできませんでした（%s）: %s\n", part.name.c_str(), err.c_str());
            return 1;
        }
        total_quads += mesh.quad_count;
        total_naive += mesh.naive_face_count;
        std::fprintf(stderr, "  %-8s %3dx%3dx%3d  naive=%-8zu quads=%-8zu triangles=%-8zu atlas=%dx%d\n",
            part.name.c_str(), model.size[0], model.size[1], model.size[2],
            mesh.naive_face_count, mesh.quad_count, mesh.triangle_count(), mesh.atlas_w, mesh.atlas_h);

        // LOD 段の実測（設計書 §6「32 → 16 → 8」／§16-4）。**間引いて数え直すだけ。**
        for (int level = 1; level <= 2; ++level) {
            VoxModel small;
            if (!downsample_model(model, level, small, err)) {
                std::fprintf(stderr, "[hd2d] LOD %d を作れませんでした: %s\n", level, err.c_str());
                return 1;
            }
            VoxelMesh lod;
            if (!mesh_model(small, lod, err, prefab.hide_unseen_from_above)) {
                std::fprintf(stderr, "[hd2d] LOD %d をメッシュにできませんでした: %s\n", level, err.c_str());
                return 1;
            }
            std::fprintf(stderr, "    LOD%d  %3dx%3dx%3d  quads=%-8zu triangles=%-8zu (%.1f%% of LOD0)\n",
                level, small.size[0], small.size[1], small.size[2], lod.quad_count, lod.triangle_count(),
                (100.0 * static_cast<double>(lod.quad_count)) / static_cast<double>(std::max<std::size_t>(1, mesh.quad_count)));
        }
    }
    std::fprintf(stderr, "[hd2d] TOTAL naive=%zu quads=%zu triangles=%zu\n", total_naive, total_quads, total_quads * 2);
    return 0;
}

} // namespace hd2d
