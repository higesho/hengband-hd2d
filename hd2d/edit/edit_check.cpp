/*!
 * @file edit_check.cpp
 * @brief `edit_check.h` の実装。
 */
#include "edit/edit_check.h"

#include "edit/prefab_edit.h"
#include "edit/prefab_writer.h"
#include "voxel/greedy_mesher.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hd2d {

namespace {

int g_failures = 0;

void report(bool ok, const std::string &what)
{
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  **FAIL** %s\n", what.c_str());
    }
}

/*!
 * @brief 失敗の理由（`err`）も添えて報告する。
 * @details `report(f(..., err), "…" + err)` と**書いてはならない**。引数の評価順は
 * 決まっておらず（MSVC は右から左）、文字列の連結が `f` より先に走ると**前の検査の
 * `err` が印字される**。ここでは `err` を参照で受け、本体（全引数の評価後）で読む。
 */
void report_op(bool ok, const std::string &what, const std::string &err)
{
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  **FAIL** %s: %s\n", what.c_str(), err.c_str());
    }
}

std::string read_all(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

//! 検査で使う「1 マスまるごと詰まった箱」（32³ ボクセル）。
Prefab make_full_cube(const std::string &name)
{
    Prefab prefab = make_new_prefab(name);
    VoxModel &model = prefab.vox.models[0];
    model.size[0] = model.size[1] = model.size[2] = 32;
    model.voxels.assign(32 * 32 * 32, 1);
    return prefab;
}

} // namespace

int run_edit_check(const AppOptions &options)
{
    g_failures = 0;
    const char *const break_raw = std::getenv("HD2D_BREAK_EDIT");
    const std::string break_mode = (break_raw != nullptr) ? break_raw : "";
    if (!break_mode.empty()) {
        std::fprintf(stderr, "[hd2d] HD2D_BREAK_EDIT=%s: **わざと壊して回します**（FAIL になるのが正しい）\n",
            break_mode.c_str());
    }

    const std::string scratch = "shots/edit_check";
    std::error_code fs_err;
    std::filesystem::create_directories(scratch, fs_err);
    if (fs_err) {
        std::fprintf(stderr, "[hd2d] 作業場を作れませんでした: %s\n", scratch.c_str());
        return 1;
    }

    // --- (0) 相手の一覧。素材が増えても検査がついて行くように、決め打ちにしない ---
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(options.voxel_dir, fs_err)) {
        if (entry.path().extension() != ".jsonc") {
            continue;
        }
        /*
         * 相方の `.vox` が無い `.jsonc` はプレハブではない。P10 からこの置き場には
         * 対応表（`terrain_prefabs.jsonc`。設計書 §9.4「素材の隣に置く」）も住んでいる。
         */
        std::filesystem::path vox = entry.path();
        vox.replace_extension(".vox");
        if (!std::filesystem::exists(vox)) {
            continue;
        }
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    std::fprintf(stderr, "[hd2d] edit-check  voxel-dir=%s  プレハブ %zu 個  作業場=%s\n",
        options.voxel_dir.c_str(), names.size(), scratch.c_str());
    if (names.empty()) {
        std::fprintf(stderr, "  **FAIL** プレハブが 1 つもありません\n");
        std::fprintf(stderr, "[hd2d] RESULT: FAIL\n");
        return 1;
    }

    /* ========================================== (1)(2) 往復と正準（全プレハブ） */
    for (const auto &name : names) {
        Prefab original;
        std::string err;
        if (!load_prefab(options.voxel_dir, name, original, err)) {
            report(false, "(1) " + name + " を読めませんでした: " + err);
            continue;
        }
        Prefab copy = original;
        SaveReport save_report;
        if (!save_prefab(scratch, name, copy, save_report, err)) {
            report(false, "(1) " + name + " の保存（書く→読み戻す→照合）が通りません: " + err);
            continue;
        }
        // 健全な素材なら footprint の作り直しは何も変えないはず（宣言と実体の一致は読めた時点で保証済み）。
        std::string why;
        report(prefabs_equivalent(original, copy, why), "(1) " + name + ": footprint の作り直しが中身を変えました: " + why);

        const std::string vox_path = scratch + "/" + name + ".vox";
        const std::string jsonc_path = scratch + "/" + name + ".jsonc";
        const std::string vox_first = read_all(vox_path);
        const std::string jsonc_first = read_all(jsonc_path);
        if (!save_prefab(scratch, name, copy, save_report, err)) {
            report(false, "(2) " + name + " の 2 回目の保存が通りません: " + err);
            continue;
        }
        report(read_all(vox_path) == vox_first, "(2) " + name + ": .vox が 2 回目で違うバイト列になりました");
        report(read_all(jsonc_path) == jsonc_first, "(2) " + name + ": .jsonc が 2 回目で違うバイト列になりました");
        std::fprintf(stderr, "  (1)(2) %-6s 往復と正準: OK (vox=%zu B, jsonc=%zu B, footprint=%zu マス)\n",
            name.c_str(), save_report.vox_bytes, save_report.jsonc_bytes, save_report.footprint_cells);
    }

    /* ==================================================== (3) 編集操作の一連 */
    {
        const std::string base = (std::find(names.begin(), names.end(), "shop") != names.end()) ? "shop" : names.front();
        Prefab prefab;
        std::string err;
        if (!load_prefab(options.voxel_dir, base, prefab, err)) {
            report(false, "(3) " + base + " を読めませんでした: " + err);
        } else {
            std::string log;
            normalize_for_edit(prefab, log);
            if (!log.empty()) {
                std::fprintf(stderr, "  (3) 正規化: %s", log.c_str());
            }
            const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(prefab.parts[0].model_index)];

            // 接地している（z = 0 の）ボクセルを 1 つ選ぶ。
            int gx = -1;
            int gy = -1;
            std::uint8_t ground_color = 0;
            for (int y = 0; (gx < 0) && (y < model.size[1]); ++y) {
                for (int x = 0; (gx < 0) && (x < model.size[0]); ++x) {
                    if (model.at(x, y, 0) != 0) {
                        gx = x;
                        gy = y;
                        ground_color = model.at(x, y, 0);
                    }
                }
            }
            report(gx >= 0, "(3) " + base + " に接地ボクセルがありません（検査の前提が崩れています）");

            // 色を塗る → 消す → 戻す。
            report_op(set_voxel(prefab, 0, gx, gy, 0, 7, err), "(3) 色付けに失敗", err);
            report(get_voxel(prefab, 0, gx, gy, 0) == 7, "(3) 塗った色が読めません");
            report_op(set_voxel(prefab, 0, gx, gy, 0, 0, err), "(3) 削除に失敗", err);
            report(get_voxel(prefab, 0, gx, gy, 0) == 0, "(3) 消したのに残っています");
            report_op(set_voxel(prefab, 0, gx, gy, 0, ground_color, err), "(3) 戻しに失敗", err);

            // 負の側へ育てる（ローカル座標がずれ、offset が動き、プレハブ空間では何も動かない）。
            const int old_size_x = model.size[0];
            const float old_offset_x = prefab.parts[0].offset[0];
            int shift[3] = {};
            report_op(set_voxel(prefab, 0, -1, gy, 0, ground_color, err, shift), "(3) 負の側への成長に失敗", err);
            report(shift[0] == 1, "(3) 成長のずれが返りません（shift.x=" + std::to_string(shift[0]) + "）");
            {
                const VoxModel &grown = prefab.vox.models[static_cast<std::size_t>(prefab.parts[0].model_index)];
                report(grown.size[0] == old_size_x + 1, "(3) 育った後の大きさが合いません");
                report(std::abs(prefab.parts[0].offset[0] - (old_offset_x - 1.f)) < 0.01f, "(3) 育った後の offset が合いません");
                report(get_voxel(prefab, 0, 0, gy, 0) == ground_color, "(3) 置いたボクセルがずれた位置にありません");
                report(get_voxel(prefab, 0, gx + 1, gy, 0) == ground_color, "(3) 既存のボクセルがずれていません");
            }

            // 正の側へ育てる。**別のマスに接地させる**（保存時の footprint 作り直しが働く相手）。
            {
                const VoxModel &now = prefab.vox.models[static_cast<std::size_t>(prefab.parts[0].model_index)];
                const int px = now.size[0]; // いまの範囲のすぐ外
                report_op(set_voxel(prefab, 0, px, gy, 0, ground_color, err), "(3) 正の側への成長に失敗", err);
                report(get_voxel(prefab, 0, px, gy, 0) == ground_color, "(3) 正の側へ置けていません");
            }

            // 上限（.vox の一辺 256）を超える成長は断られる。
            report(!set_voxel(prefab, 0, 300, gy, 0, ground_color, err), "(3) 一辺 256 を超える成長が断られません");

            // 分割: 接地ボクセル 1 個を新しいパーツへ。
            const std::size_t parts_before = prefab.parts.size();
            const int box[3] = { 0, gy, 0 };
            const int split_index = split_part(prefab, 0, box, box, "chip", err);
            report_op(split_index == static_cast<int>(parts_before), "(3) 分割に失敗", err);
            if (split_index >= 0) {
                report(get_voxel(prefab, 0, 0, gy, 0) == 0, "(3) 分割の元にボクセルが残っています");
                report(get_voxel(prefab, split_index, 0, 0, 0) == ground_color, "(3) 分割先にボクセルが移っていません");
                report(prefab.parts[static_cast<std::size_t>(split_index)].parent == 0, "(3) 分割先の親が元のパーツではありません");
                // 名前の検査: 使えない字・重複は断られる。
                report(!rename_part(prefab, split_index, "日本語", err), "(3) 使えない字の名前が断られません");
                report(!rename_part(prefab, split_index, prefab.parts[0].name, err), "(3) 重複する名前が断られません");
                report_op(rename_part(prefab, split_index, "chip2", err), "(3) まともな名前の変更に失敗", err);
                // 循環の検査: 元 ← 分割先 の親付けは循環になる。
                report(!set_parent(prefab, 0, split_index, err), "(3) 循環する親付けが断られません");
                // 移動（ピボットはパーツに付いて動く）。
                prefab.parts[static_cast<std::size_t>(split_index)].has_pivot = true;
                prefab.parts[static_cast<std::size_t>(split_index)].pivot[0] = prefab.parts[static_cast<std::size_t>(split_index)].offset[0];
                prefab.parts[static_cast<std::size_t>(split_index)].pivot[1] = prefab.parts[static_cast<std::size_t>(split_index)].offset[1];
                prefab.parts[static_cast<std::size_t>(split_index)].pivot[2] = prefab.parts[static_cast<std::size_t>(split_index)].offset[2];
                const float pivot_before = prefab.parts[static_cast<std::size_t>(split_index)].pivot[0];
                report_op(move_part(prefab, split_index, 3, 0, 0, err), "(3) 移動に失敗", err);
                report(std::abs(prefab.parts[static_cast<std::size_t>(split_index)].pivot[0] - (pivot_before + 3.f)) < 0.01f,
                    "(3) 移動でピボットが付いて来ません");
                report_op(delete_part(prefab, split_index, err), "(3) パーツの削除に失敗", err);
                report(prefab.parts.size() == parts_before, "(3) 削除後のパーツ数が合いません");
            }

            // パレットを 1 色だけ変える（保存 → 読み戻しで残ることは save_prefab の照合が見る）。
            prefab.vox.palette[5][0] = 210;
            prefab.vox.palette[5][1] = 32;
            prefab.vox.palette[5][2] = 96;
            prefab.vox.palette[5][3] = 255;

            // メッシュ化が通ること（編集で壊れた模型は絵に出る前にここで捕まえる）。
            for (const auto &part : prefab.parts) {
                VoxelMesh mesh;
                report_op(mesh_model(prefab.vox.models[static_cast<std::size_t>(part.model_index)], mesh, err),
                    "(3) 編集後のメッシュ化に失敗（" + part.name + "）", err);
            }

            // 保存（footprint の作り直し込み）→ 読み戻し照合。
            SaveReport save_report;
            report_op(save_prefab(scratch, "edited", prefab, save_report, err), "(3) 編集後の保存が通りません", err);
            std::fprintf(stderr, "  (3) 編集操作の一連: %s（footprint %zu マス%s）\n",
                (g_failures == 0) ? "OK" : "問題あり", save_report.footprint_cells,
                save_report.footprint_refreshed ? "・作り直し済み" : "・**作り直していない**");
        }
    }

    /* ====================================================== (4) レイ判定 */
    {
        Prefab cube = make_full_cube("ray");
        std::string err;

        // 真上から: いちばん上の層に、上向きの面で当たる。
        {
            Ray ray;
            ray.origin = Vec3{ 0.5f, 0.5f, 5.f };
            ray.dir = Vec3{ 0.f, 0.f, -1.f };
            VoxelHit hit;
            const bool ok = raycast_prefab(cube, ray, 100.f, hit);
            report(ok && (hit.voxel[2] == 31) && (hit.normal[2] == 1),
                "(4) 真上からのレイが上の面に当たりません");
        }
        // 西から: x = 0 の層に、西向きの面で当たる。
        {
            Ray ray;
            ray.origin = Vec3{ -3.f, 0.5f, 0.5f };
            ray.dir = Vec3{ 1.f, 0.f, 0.f };
            VoxelHit hit;
            const bool ok = raycast_prefab(cube, ray, 100.f, hit);
            report(ok && (hit.voxel[0] == 0) && (hit.normal[0] == -1) && (std::abs(hit.t - 3.f) < 0.01f),
                "(4) 西からのレイが西の面に当たりません");
            // 検査の検査: 届く前に打ち切れば外れるはず。
            VoxelHit cut;
            report(!raycast_prefab(cube, ray, 2.f, cut), "(4) max_t の打ち切りが効いていません");
        }
        // 外すべきものは外す（箱の横を通り過ぎる）。
        {
            Ray ray;
            ray.origin = Vec3{ -3.f, 5.5f, 0.5f };
            ray.dir = Vec3{ 1.f, 0.f, 0.f };
            VoxelHit hit;
            report(!raycast_prefab(cube, ray, 100.f, hit), "(4) 外れるはずのレイが当たっています");
        }
        // offset が付いても同じ（ボクセル空間への変換が正しいか）。
        {
            Prefab shifted = cube;
            shifted.parts[0].offset[0] = 32.f; // 1 マス東へ
            Ray ray;
            ray.origin = Vec3{ 1.5f, 0.5f, 5.f };
            ray.dir = Vec3{ 0.f, 0.f, -1.f };
            VoxelHit hit;
            const bool ok = raycast_prefab(shifted, ray, 100.f, hit);
            report(ok && (hit.voxel[2] == 31), "(4) offset 付きのパーツに当たりません");
            Ray miss;
            miss.origin = Vec3{ 0.5f, 0.5f, 5.f };
            miss.dir = Vec3{ 0.f, 0.f, -1.f };
            report(!raycast_prefab(shifted, miss, 100.f, hit), "(4) offset 付きで外れるはずのレイが当たっています");
        }
        // 2 パーツなら手前が勝つ。
        {
            Prefab two = cube;
            two.vox.models.push_back(two.vox.models[0]);
            PrefabPart far_part; // ※ `far` は windows.h の古いマクロと衝突する（引き継ぎ §3 罠 22）
            far_part.name = "b";
            far_part.voxels = "b";
            far_part.model_index = 1;
            far_part.offset[0] = 64.f;
            two.parts.push_back(far_part);
            Ray from_west;
            from_west.origin = Vec3{ -3.f, 0.5f, 0.5f };
            from_west.dir = Vec3{ 1.f, 0.f, 0.f };
            VoxelHit hit;
            report(raycast_prefab(two, from_west, 100.f, hit) && (hit.part == 0), "(4) 西からのレイで手前のパーツが勝ちません");
            Ray from_east;
            from_east.origin = Vec3{ 6.f, 0.5f, 0.5f };
            from_east.dir = Vec3{ -1.f, 0.f, 0.f };
            report(raycast_prefab(two, from_east, 100.f, hit) && (hit.part == 1), "(4) 東からのレイで手前のパーツが勝ちません");
        }
        // 地面との交点。
        {
            Ray ray;
            ray.origin = Vec3{ 0.f, 0.f, 2.f };
            ray.dir = normalize(Vec3{ 1.f, 0.f, -1.f });
            Vec3 point;
            report(raycast_ground(ray, 0.f, point) && (std::abs(point.x - 2.f) < 0.01f), "(4) 地面との交点が合いません");
            Ray level;
            level.origin = Vec3{ 0.f, 0.f, 2.f };
            level.dir = Vec3{ 1.f, 0.f, 0.f };
            report(!raycast_ground(level, 0.f, point), "(4) 水平なレイが地面に当たったことになっています");
        }
        // 投影との往復: 画素 → レイ → 当たり。カメラへ向いた角のボクセルなら厳密に戻る。
        {
            EditorCameraState camera;
            camera.center = Vec3{ 0.5f, 0.5f, 0.5f };
            camera.azimuth = 0.9f;
            camera.elevation = 0.62f;
            camera.distance = 6.f;
            camera.fov_x = 0.62f;
            camera.screen_w = 1600;
            camera.screen_h = 900;
            const Vec3 corner_centre{ 31.5f / 32.f, 31.5f / 32.f, 31.5f / 32.f };
            float px = 0.f;
            float py = 0.f;
            report(camera_project(camera, corner_centre, px, py), "(4) 投影がカメラの後ろ扱いになっています");
            const Ray ray = camera_ray(camera, px, py);
            VoxelHit hit;
            const bool ok = raycast_prefab(cube, ray, 100.f, hit);
            report(ok && (hit.voxel[0] == 31) && (hit.voxel[1] == 31) && (hit.voxel[2] == 31),
                "(4) 画素 → レイ → 当たりの往復が角のボクセルへ戻りません（当たり: "
                    + (ok ? (std::to_string(hit.voxel[0]) + "," + std::to_string(hit.voxel[1]) + ","
                          + std::to_string(hit.voxel[2]))
                          : std::string("なし"))
                    + "）");
        }
        (void)err;
        std::fprintf(stderr, "  (4) レイ判定: %s\n", (g_failures == 0) ? "OK" : "問題あり");
    }

    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (g_failures == 0) ? "PASS" : "FAIL");
    return (g_failures == 0) ? 0 : 1;
}

} // namespace hd2d
