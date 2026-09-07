/*!
 * @file device_checks.cpp
 * @brief 粒と VR の検査（`--slab-ladder-check` / `--vr-check`）。VR は実機が要る。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * 振る舞いが変わっていないことは `python tools/hd2d_verify/golden.py --check` で見る。
 */
#include "app/hd2d_checks.h"

#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "render/camera.h"
#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/voxel_renderer.h"
#include "voxel/greedy_mesher.h"
#include "voxel/prefab.h"
#include "world/dungeon_style.h"
#include "world/floor_meaning.h"
#include "world/prefab_library.h"
#include "world/terrain_view.h"
#include "world/town_plan.h"
#include "frame/cell_feature_bits.h"
#include "frame/game_frame.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "xr/xr_session.h"
#include "xr/xr_math.h"
#if defined(HD2D_EDITOR)
#include "edit/prefab_writer.h" //!< 書く側はエディタ側にしか入らない（Android は丸ごと外す）
#endif
#include <filesystem>
#include <fstream>

namespace hd2d {

using namespace hd2d::gl; //!< GL の型と関数は `hd2d::gl` に居る（gl_core.h）

#if defined(HD2D_EDITOR)
int run_slab_ladder_check(const AppOptions &options)
{
    (void)options;
    const char *const temp = std::getenv("TEMP");
    const std::string dir = (temp != nullptr) ? (std::string(temp) + "/hd2d_slab_ladder") : std::string("hd2d_slab_ladder");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::fprintf(stderr, "[hd2d] 実体の板の粒: %s を通す（置き場 %s）\n",
        voxels_per_cell_ladder_text().c_str(), dir.c_str());
    bool ok = true;
    for (const int side : kVoxelsPerCellLadder) {
        const int depth = std::max(1, side / 16); //!< 板の厚み。`tools/voxel/tile_to_slab.py` と同じ規則
        VoxModel model;
        model.size[0] = side;
        model.size[1] = depth;
        model.size[2] = side;
        model.voxels.assign(static_cast<std::size_t>(side) * static_cast<std::size_t>(depth)
                * static_cast<std::size_t>(side),
            0);
        std::size_t filled = 0;
        for (int z = 0; z < side; ++z) {
            for (int x = 0; x < side; ++x) {
                // 外周の枠と斜めの筋。**平らな一枚板にしない**（メッシャが四角形を割る所を通す）
                const bool frame = (x == 0) || (z == 0) || (x == side - 1) || (z == side - 1);
                const bool stripe = ((x + z) % 8) < 3;
                if (!frame && !stripe) {
                    continue;
                }
                for (int y = 0; y < depth; ++y) {
                    model.voxels[static_cast<std::size_t>(x) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(side))
                        + (static_cast<std::size_t>(z) * static_cast<std::size_t>(side) * static_cast<std::size_t>(depth))]
                        = static_cast<std::uint8_t>(1 + ((x + z) % 200));
                    ++filled;
                }
            }
        }

        Prefab prefab;
        prefab.name = "ladder" + std::to_string(side);
        prefab.voxels_per_cell = side;
        prefab.vox.models.push_back(model);
        for (int i = 1; i < 256; ++i) {
            prefab.vox.palette[i][0] = static_cast<std::uint8_t>(i);
            prefab.vox.palette[i][1] = static_cast<std::uint8_t>(255 - i);
            prefab.vox.palette[i][2] = 128;
            prefab.vox.palette[i][3] = 255;
        }
        PrefabPart part;
        part.name = "main";
        part.voxels = "main";
        part.model_index = 0;
        part.parent = -1;
        part.grounded = true;
        part.wind_k = 0.f;
        prefab.parts.push_back(part);

        SaveReport report;
        std::string err;
        if (!save_prefab(dir, prefab.name, prefab, report, err)) {
            std::fprintf(stderr, "  NG %4d: 書けませんでした: %s\n", side, err.c_str());
            ok = false;
            continue;
        }
        Prefab back;
        if (!load_prefab(dir, prefab.name, back, err)) {
            std::fprintf(stderr, "  NG %4d: 読み戻せませんでした: %s\n", side, err.c_str());
            ok = false;
            continue;
        }
        const VoxModel &got = back.vox.models[static_cast<std::size_t>(back.parts[0].model_index)];
        const std::size_t got_filled = static_cast<std::size_t>(
            std::count_if(got.voxels.begin(), got.voxels.end(), [](std::uint8_t v) { return v != 0; }));
        if ((back.voxels_per_cell != side) || (got.size[0] != side) || (got.size[1] != depth)
            || (got.size[2] != side) || (got_filled != filled)) {
            std::fprintf(stderr, "  NG %4d: 読み戻しが違います（粒 %d / %dx%dx%d / 埋まり %zu、期待 %zu）\n",
                side, back.voxels_per_cell, got.size[0], got.size[1], got.size[2], got_filled, filled);
            ok = false;
            continue;
        }
        VoxelMesh mesh;
        if (!mesh_model(got, mesh, err)) {
            std::fprintf(stderr, "  NG %4d: メッシュにできません: %s\n", side, err.c_str());
            ok = false;
            continue;
        }
        VoxModel lod;
        VoxelMesh lod_mesh;
        if (!downsample_model(got, 1, lod, err) || !mesh_model(lod, lod_mesh, err)) {
            std::fprintf(stderr, "  NG %4d: LOD を作れません: %s\n", side, err.c_str());
            ok = false;
            continue;
        }
        std::fprintf(stderr, "  OK %4d: %dx%dx%d ボクセル %zu / 四角形 %zu（LOD1 %zu）/ %s / %.0fKB\n",
            side, got.size[0], got.size[1], got.size[2], got_filled, mesh.quad_count, lod_mesh.quad_count,
            (side > kVoxClassicMaxSide) ? "XYZ2" : "XYZI", static_cast<double>(report.vox_bytes) / 1024.0);
    }

    /*
     * **折り返しを黙って通さないこと**の確認。`XYZI` のまま SIZE だけ 512 と書いた物を
     * 作って、読み込みが**失敗する**ことを見る（成功したら、あの絵の重なりが復活している）。
     */
    {
        const std::string bad = dir + "/folded.vox";
        std::string src;
        {
            std::ifstream in(dir + "/ladder512.vox", std::ios::binary);
            src.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const std::size_t at = src.find("XYZ2");
        if ((at == std::string::npos) || src.empty()) {
            std::fprintf(stderr, "  NG 折り返しの検査: 512 の板に XYZ2 がありません\n");
            ok = false;
        } else {
            src.replace(at, 4, "XYZI"); // 中身は 16 ビットのまま＝壊れたファイル
            std::ofstream out(bad, std::ios::binary);
            out.write(src.data(), static_cast<std::streamsize>(src.size()));
            out.close();
            VoxFile dummy;
            std::string err;
            if (load_vox_file(bad, dummy, err)) {
                std::fprintf(stderr, "  NG 折り返しの検査: 256 超を XYZI で読んでしまいました\n");
                ok = false;
            } else {
                std::fprintf(stderr, "  OK 折り返しの検査: 断って止まりました（%s）\n", err.c_str());
            }
        }
    }
    std::fprintf(stderr, "[hd2d] 粒の受け入れ: %s\n", ok ? "OK" : "NG");
    return ok ? 0 : 1;
}
#endif // HD2D_EDITOR（書く側は `hd2d/edit/prefab_writer.cpp`。リリース用には入らない）

/*!
 * @brief VR の検査（`--vr-check`）。**コアを起こさない。**
 * @details 窓と GL だけ作って OpenXR のセッションを立て、両眼を色で塗って回す。
 * 見るのは 3 つ:
 * 1. **どのランタイムに繋がったか**（起動行の `XR_RUNTIME`。設計書 罠 1）
 * 2. セッションがどこまで進んだか（IDLE → READY → … → FOCUSED）
 * 3. `xrWaitFrame` の間隔（歩調をランタイムが握れているか。設計書 §4）
 *
 * HMD が無い機械でも (1) までは必ず出る。SteamVR の null ドライバなら (2) が
 * FOCUSED に達し、(3) も出る（設計書 §13）。
 *
 * **左右で色を変えてある。** 片目だけ映っている・左右が入れ替わっている、を
 * HMD を被った人がすぐ言えるようにするため。
 */
int run_vr_check(const AppOptions &options)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[hd2d] SDL の初期化に失敗しました: %s\n", SDL_GetError());
        return 1;
    }
    Window window;
    std::string err;
    if (!create_window(options, window, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    //! **歩調は `xrWaitFrame` が握る。**vsync と二重に待つと即座に半分へ落ちる（罠 5）。
    SDL_GL_SetSwapInterval(0);

    xr::Session xr;
    if (!xr.init(err)) {
        std::fprintf(stderr, "[hd2d] vr-check: %s\n", err.c_str());
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        std::fprintf(stderr, "[hd2d] RESULT: FAIL\n");
        return 1;
    }

    int drawn = 0;
    double wait_min = 1e9;
    double wait_max = 0.0;
    double wait_sum = 0.0;
    const double perf_freq = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 last_wait = SDL_GetPerformanceCounter();
    /*
     * **数えるのは「描いた数」であって回した数ではない。**セッションが起きるまでの
     * 空回りを数に入れると、ランタイムの機嫌しだいで 1 枚も描かないうちに数え終わる
     * （実際に踏んだ。SteamVR は温まっているかどうかで READY まで数秒ぶれる）。
     * 代わりに全体の時間で頭打ちにする。
     */
    const Uint64 started_at = SDL_GetTicks64();
    constexpr Uint64 kVrCheckTimeoutMs = 60000;
    bool quit_requested = false;
    while (!quit_requested && (drawn < options.vr_check_frames)
        && ((SDL_GetTicks64() - started_at) < kVrCheckTimeoutMs)) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                quit_requested = true;
            }
        }
        xr::Frame frame;
        if (!xr.begin_frame(frame)) {
            SDL_Delay(2); //!< まだ寝ている（IDLE）。ここは待ってよい
            continue;
        }
        {
            const Uint64 now = SDL_GetPerformanceCounter();
            const double ms = static_cast<double>(now - last_wait) * 1000.0 / perf_freq;
            last_wait = now;
            if (drawn > 0) { //!< 1 回目は「起きるまでの待ち」が混ざるので数えない
                wait_min = std::min(wait_min, ms);
                wait_max = std::max(wait_max, ms);
                wait_sum += ms;
            }
        }
        for (int eye = 0; eye < frame.view_count; ++eye) {
            if (!xr.bind_eye(eye)) {
                continue;
            }
            //! 左は青・右は緑。**左右の取り違えがすぐ分かる色にする。**
            if (eye == 0) {
                glClearColor(0.05f, 0.10f, 0.35f, 1.f);
            } else {
                glClearColor(0.05f, 0.30f, 0.12f, 1.f);
            }
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        int mirror_w = 0;
        int mirror_h = 0;
        SDL_GL_GetDrawableSize(window.window, &mirror_w, &mirror_h);
        /*
         * 鏡窓には**左右を並べて**出す（`--shot=` で 1 枚に両眼が写るように）。
         * HMD が無い機械でも「左が青・右が緑」を絵として残せる。
         */
        xr.blit_mirror(0, 0, 0, mirror_w / 2, mirror_h);
        xr.blit_mirror(1, mirror_w / 2, 0, mirror_w, mirror_h);
        xr.end_frame();
        //! **`--shot=` は swap の直前**（フラットの主ループと同じ位置）。
        if (!options.shot_path.empty() && (drawn == std::max(1, options.shot_after_frames))) {
            (void)save_framebuffer_bmp(options.shot_path, mirror_w, mirror_h);
        }
        SDL_GL_SwapWindow(window.window);
        ++drawn;
    }

    std::fprintf(stderr, "[hd2d] vr-check: runtime=%s\n",
        xr.runtime_name().empty() ? "(unknown)" : xr.runtime_name().c_str());
    {
        std::string states;
        for (const auto &name : xr.state_log()) {
            states += (states.empty() ? "" : " -> ") + name;
        }
        std::fprintf(stderr, "[hd2d] vr-check: states  %s\n", states.empty() ? "(none)" : states.c_str());
    }
    std::fprintf(stderr, "[hd2d] vr-check: drawn %d frames\n", drawn);
    if (drawn > 1) {
        std::fprintf(stderr, "[hd2d] vr-check: xrWaitFrame 間隔 min %.2f / avg %.2f / max %.2f ms\n",
            wait_min, wait_sum / static_cast<double>(drawn - 1), wait_max);
    }
    /*
     * **合格は「FOCUSED まで行って絵を出した」まで。**繋がっただけでは通さない
     * （設計書 §12 の M0 完了条件）。HMD が無ければここは落ちる——それが正しい。
     *
     * 見るのは `focus_reached`（一度でも達したか）であって、いまの状態ではない。
     * セッションはまとめるとき `FOCUSED → … → EXITING` と降りてくるので、
     * 終わり際の値を見ると「達しなかった」に化ける。
     */
    const bool ok = xr.focus_reached() && (drawn > 0);
    xr.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace hd2d
