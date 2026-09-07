/*!
 * @file render_checks.cpp
 * @brief 描画の検査（`--terrain-check` / `--post-check` / `--cutaway-check`）。窓と GL が要る。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * **遊ぶときには 1 行も通らない。** 宣言は `app/hd2d_checks.h`。
 * 振る舞いが変わっていないことは `python tools/hd2d_verify/golden.py --check` で見る。
 */
#include "app/hd2d_checks.h"

#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "render/camera.h"
#include "render/gl_core.h"
#include "render/glyph_atlas.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/post_process.h"
#include "render/render_view.h"
#include "render/shadow_map.h"
#include "render/sky_dome.h"
#include "render/term_colors.h"
#include "render/text_overlay.h"
#include "render/voxel_renderer.h"
#include "render/billboard_renderer.h"
#include "render/ground_ring.h"
#include "render/light_shaft.h"
#include "render/cloud_layer.h"
#include "render/dust_motes.h"
#include "voxel/greedy_mesher.h"
#include "voxel/part_motion.h"
#include "voxel/prefab.h"
#include "world/dungeon_style.h"
#include "world/entity_view.h"
#include "world/floor_meaning.h"
#include "world/overlay_view.h"
#include "world/prefab_library.h"
#include "world/slab_library.h"
#include "world/terrain_memory.h"
#include "world/terrain_view.h"
#include "world/town_plan.h"
#include "ui/combat_fx_view.h"
#include "ui/feature_menu.h"
#include "ui/floor_cutin.h"
#include "ui/game_hud.h"
#include "ui/game_pad.h"
#include "ui/hd2d_settings.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/virtual_pad.h"
#include "assets/tile_catalog.h"
#include "i18n/lang.h"
#include "audio/audio_engine.h"
#include "audio/ambience_mix.h"
#include "audio/ambience_table.h"
#include "audio/sfx_catalog.h"
#include "frame/cell_feature_bits.h"
#include "frame/frame_codec.h"
#include "frame/game_frame.h"
#include "frame/minimap_snapshot.h"
#include "frame/protocol_messages.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hd2d {

using namespace hd2d::gl; //!< GL の型と関数は `hd2d::gl` に居る（gl_core.h）

int run_terrain_check(const AppOptions &options)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[hd2d] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    Window window;
    std::string err;
    if (!create_window(options, window, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    TextOverlay text;
    VoxelRenderer renderer;
    ShadowMap shadow;
    PostChain post;
    UiPaint paint; //!< `--combat-fx-check` のときだけ使う（見せ場は 2D の重ね書き）
    if (!text.init(kTextPx, err) || !renderer.init(err) || !shadow.init(kShadowMapSide, err)
        || !paint.init(err)
        || !init_post_chain(post, options, err) || !post.resize(options.window_w, options.window_h, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    Prefab wall_prefab = make_box_prefab("wall", 32, 32, 32, 0, 32);
    Prefab floor_prefab = make_box_prefab("floor", 32, 32, 2, -2, 32);
    GpuPrefab wall_gpu;
    GpuPrefab floor_gpu;
    if (!renderer.upload(wall_prefab, wall_gpu, err) || !renderer.upload(floor_prefab, floor_gpu, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }

    int screen_w = options.window_w;
    int screen_h = options.window_h;
    SDL_GetWindowSize(window.window, &screen_w, &screen_h);

    Camera camera;
    camera.pitch = options.camera_pitch_deg * 0.0174533f;
    camera.fov_x = options.camera_fov_deg * 0.0174533f;
    camera.viewport_w = screen_w;
    camera.viewport_h = screen_h;
    camera.distance = distance_for_cell_px(camera, options.camera_cell_px);
    //! `--turn=0..3`。**0 なら厳密に 0** なので従来と同じ絵。
    camera.yaw = view_turn_yaw(options.camera_turn);
    // 部屋の中に立たせる（合成マップは 16×14 間隔で部屋と通路を敷いてある）。
    camera.target = Vec3{ 100.5f, 44.5f, 0.f };
    const ViewWindow window_full = derive_view_window(camera, kMaxPropHeight, 60.f);

    const int view_w = std::max(3, window_full.cols - (options.shrink_view * 2));
    const int view_h = std::max(3, window_full.rows - (options.shrink_view * 2));
    GameFrame frame = make_synthetic_frame(view_w, view_h, 100, 44);
    /*
     * **合成フロアに N 番のダンジョンの顔をさせる**（`--dungeon=N`。P10 第 4 期）。
     * 町の `--town-view` と同じ役目で、ダンジョンごとの意匠（`dungeon_style.cpp`）を
     * 自分で見るために要る。実機でイークの洞穴やオークの洞窟へ潜るには
     * 何十手もかかるし、深い所（金鉱の 75 階）へは事実上行けない。
     * **検査の結果は変えない**（種が変わるので配置は変わるが、見るものは同じ）。
     */
    if (options.synthetic_dungeon_id > 0) {
        frame.floor.dungeon_id = options.synthetic_dungeon_id;
    }
    /*
     * **階**（`--dun-level=N`）。`make_synthetic_frame` の既定は 5 階である。
     * 階を動かせないと**帯（`levels`）を持つ意匠**を検分できない
     */
    if (options.synthetic_dun_level >= 0) {
        frame.floor.dun_level = options.synthetic_dun_level;
    }
    /*
     * **別コアのダンジョンを合成フレームで見るオプション**（同 §2.5・V4）。ここはコアを起こさないので
     * コア名が無い——`HD2D_STYLE_CORE=gensoband`（旧 `HD2D_TOWN_CORE` も可）で表を読ませる。
     * 未設定なら従来どおりべた書きの表＝変愚の絵は 1 ボクセルも動かない。
     */
    load_styles_for_check(options);
    /*
     * `--shrink-view` は「コアへ狭い窓しか頼まなかった」を再現する検査である。
     * 意味づけ層はフロア全域を読むので、**ミニマップの側を窓の外で未知に戻す**ことで
     * 「届いていない」状況を作る（そうしないと窓を狭めても地形が減らない）。
     */
    if (options.shrink_view > 0) {
        const int ox = 100 - (view_w / 2);
        const int oy = 44 - (view_h / 2);
        for (int y = 0; y < frame.minimap.height; ++y) {
            for (int x = 0; x < frame.minimap.width; ++x) {
                if ((x < ox) || (x >= (ox + view_w)) || (y < oy) || (y >= (oy + view_h))) {
                    frame.minimap.kinds[(static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.minimap.width))
                        + static_cast<std::size_t>(x)]
                        = static_cast<std::uint8_t>(MinimapKind::Unknown);
                }
            }
        }
    }
    FloorMeaning meaning;
    (void)rebuild_floor_meaning(frame, meaning);
    /*
     * プレハブライブラリ（P10）。**この検査では必須**である。assets/ は追跡されているので
     * 読めない＝木が壊れている。読めないまま仮の箱に落ちて PASS すると、本番と違う絵を
     * 検査することになる（罠 28「検査は本番と違う viewport で描いていたので素通り」と同種）。
     */
    PrefabLibrary library;
    TerrainMemory memory;
    if (!library.load(options.voxel_dir, err) || !library.upload_gpu(renderer, err)) {
        std::fprintf(stderr, "[hd2d] terrain-check: プレハブライブラリを用意できませんでした:\n%s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    memory.update(frame);
    library.update_motions(0.58f); //!< 検査は時刻を固定する（罠 14。動きで絵が毎回変わると比較にならない）
    TerrainView terrain;
    build_terrain_view(meaning, Frustum::from(camera.view_projection()), terrain, &memory, &library);

    /*
     * (0') **同じ入れ物へ組み直しても数が増えないこと**（2026-08-23。気づいたこと
     * 「時間経過で光の差し込みが重なっていくのか白飛びしてしまう」）。
     *
     * `build_terrain_view()` は**毎フレーム**呼ばれ、出力の入れ物は使い回される。
     * 頭で空にし忘れた列は積み増され、**同じマスの光を何十枚も重ねて描く**
     * ——加算なので白へ飛ぶ。配列も際限なく伸びる。
     *
     * 目で見つけるのは難しい（1 フレームでは正しく、何十秒か経ってから壊れる）ので、
     * **2 回組んで数を突き合わせる**。列を足した人がここを通れば必ず捕まる。
     */
    bool rebuild_ok = true;
    {
        /*
         * **「前のフレームの残り」を自分で仕込む。**
         *
         * 2 回組んで数を突き合わせるだけでは**空振りする**——この合成の階には
         * 陽だまりも霧も蒸気も 1 マスも無いので、どの列も 0 のまま「増えていない」に見える
         * （最初そう書いて、実際に 0 が並んだ）。**列が空のままでは、空にし忘れを
         * 見つけようがない。**
         *
         * だから使い回しの入れ物へ**わざと 1 件ずつ置いてから**組み直す。
         * 頭で空にしていれば消え、忘れていれば残る。列の中身が 0 件でも効く。
         */
        TerrainView fresh; //!< 新しい入れ物＝必ず正しい数
        build_terrain_view(meaning, Frustum::from(camera.view_projection()), fresh, &memory, &library);

        terrain.slabs.push_back(InstanceData{});
        terrain.boxes.push_back(InstanceData{});
        terrain.lights.push_back(TerrainViewLight{});
        terrain.light_shafts.push_back(LightShaftInstance{});
        terrain.mist_cells.push_back({ 0, 0 });
        terrain.steam_jets.push_back({ 0.f, 0.f, 0.f });
        if (!terrain.lib.empty()) {
            terrain.lib[0].push_back(InstanceData{});
        }

        //! **同じ入れ物へ**もう一度組む（新しい入れ物では積み増しが起きない）。
        build_terrain_view(meaning, Frustum::from(camera.view_projection()), terrain, &memory, &library);

        struct Counted {
            const char *name;
            std::size_t want;
            std::size_t got;
        };
        const Counted rows[] = {
            { "slabs", fresh.slabs.size(), terrain.slabs.size() },
            { "boxes", fresh.boxes.size(), terrain.boxes.size() },
            { "lights", fresh.lights.size(), terrain.lights.size() },
            { "light_shafts", fresh.light_shafts.size(), terrain.light_shafts.size() },
            { "mist_cells", fresh.mist_cells.size(), terrain.mist_cells.size() },
            { "steam_jets", fresh.steam_jets.size(), terrain.steam_jets.size() },
            { "lib[0]", fresh.lib.empty() ? 0U : fresh.lib[0].size(),
                terrain.lib.empty() ? 0U : terrain.lib[0].size() },
        };
        for (const auto &row : rows) {
            if (row.want == row.got) {
                continue;
            }
            std::fprintf(stderr,
                "  **FAIL** 組み直し: %s に前の残りが %zu 件ぶん生きています（%zu のはずが %zu）"
                "——`build_terrain_view` の頭で空にしていません\n",
                row.name, row.got - row.want, row.want, row.got);
            rebuild_ok = false;
        }
        std::fprintf(stderr, "  (0') 組み直し: 残りを 7 列へ仕込んで組み直し → %s\n",
            rebuild_ok ? "全部消えた" : "残った");
    }

    /*
     * **未探知を真っ黒な塊で埋める**（2026-08-11 に決めた。一人称かつ地下のときだけ）。
     * 見るのは 3 つ:
     *   1. 切のとき ＝ **何も置かない**（従来。見えていない地形を捏造しない）
     *   2. 入のとき ＝ **箱が増える**（未探知のぶん）
     *   3. その箱が**真っ黒**であること（色を持たせると「知らない所」に見えない）
     * 2 だけだと「何か置いた」までしか言えず、灰色の壁でも通ってしまう。
     */
    bool dark_unknown_ok = true;
    {
        /*
         * **合成フロアは全マス探知済み**（`known=13068`）なので、そのままでは
         * 未探知が 1 マスも無く、この検査は空振りする。**未探知を自分で作る。**
         * 罠 64（検査の絵に無い条件は検出できない）そのものなので、
         * 「増えなかった」を PASS にしないこと。
         */
        FloorMeaning fogged = meaning;
        int made_unknown = 0;
        for (int gy = 0; gy < fogged.height; ++gy) {
            for (int gx = 0; gx < fogged.width; ++gx) {
                //! 縞に抜く（塊で抜くと視錐台の外だけが未探知になりかねない）。
                if (((gx + gy) % 7) != 0) {
                    continue;
                }
                const auto at = (static_cast<std::size_t>(gy) * static_cast<std::size_t>(fogged.width))
                    + static_cast<std::size_t>(gx);
                fogged.roles[at] = static_cast<std::uint8_t>(CellRole::Unknown);
                ++made_unknown;
            }
        }
        TerrainView plain;
        build_terrain_view(fogged, Frustum::from(camera.view_projection()), plain, &memory, &library);
        TerrainView dark;
        build_terrain_view(fogged, Frustum::from(camera.view_projection()), dark, &memory, &library, nullptr, 0.f,
            0.f, true);
        const std::size_t added
            = (dark.boxes.size() > plain.boxes.size()) ? (dark.boxes.size() - plain.boxes.size()) : 0U;
        std::fprintf(stderr, "  未探知の黒塊: 未探知を %d マス作った\n", made_unknown);
        std::size_t black = 0;
        for (const auto &box : dark.boxes) {
            if ((box.r == 0.f) && (box.g == 0.f) && (box.b == 0.f)) {
                ++black;
            }
        }
        if (added == 0) {
            std::fprintf(stderr, "  **未探知の黒塊: 箱が 1 つも増えていません"
                                 "（この合成フロアに未探知が無い ＝ 検査になっていない）**\n");
            dark_unknown_ok = false;
        } else if (black != added) {
            std::fprintf(stderr, "  **未探知の黒塊: 増えた %zu 個のうち真っ黒なのは %zu 個です**\n", added, black);
            dark_unknown_ok = false;
        }
        std::fprintf(stderr, "  未探知の黒塊: 切=%zu 個 → 入=%zu 個（増えた %zu 個）\n",
            plain.boxes.size(), dark.boxes.size(), added);
    }

    /*
     * **一人称の 2 段目と天井**（2026-08-11 に決めた）。見るのは 4 つ:
     *   1. 2 段目だけ入 → z=1 ちょうどの基本ブロックが増え、z=2 には何も居ない
     *   2. 天井だけ入 → 同じく z=1（壁 1 段の天面に載る）に増え、z=2 には居ない
     *   3. 両方入 → z=1（2 段目）と z=2（天井。壁 2 段の天面へ持ち上がる）の両方に居る
     *   4. **切のときの置き場所が 1 ビットも動かない**（床の板が同一 ＝ 種が別系統である
     *      ことの検査。本流の乱数列から引いていると、切り替えるたびに世界の絵が変わる）
     * 1〜3 の z を見るのは「増えた」だけだと積む高さの取り違え（天井が壁にめり込む等）を
     * 検出できないため。z=1 / z=2 ちょうどに置くのは他の経路に無い。
     */
    bool fps_layers_ok = true;
    {
        const auto count_at_z = [](const TerrainView &view, float z) {
            std::size_t n = 0;
            for (const auto &bucket : view.lib) {
                for (const auto &inst : bucket) {
                    if (inst.z == z) {
                        ++n;
                    }
                }
            }
            for (const auto &box : view.boxes) {
                if (box.z == z) {
                    ++n;
                }
            }
            return n;
        };
        const auto slabs_equal = [](const TerrainView &a, const TerrainView &b) {
            return (a.slabs.size() == b.slabs.size())
                && ((a.slabs.empty())
                    || (std::memcmp(a.slabs.data(), b.slabs.data(), a.slabs.size() * sizeof(InstanceData)) == 0));
        };
        const Frustum frustum = Frustum::from(camera.view_projection());
        TerrainView base;
        build_terrain_view(meaning, frustum, base, &memory, &library);
        TerrainView upper;
        build_terrain_view(meaning, frustum, upper, &memory, &library, nullptr, 0.f, 0.f, false, nullptr, true, false);
        TerrainView ceiling;
        build_terrain_view(meaning, frustum, ceiling, &memory, &library, nullptr, 0.f, 0.f, false, nullptr, false, true);
        TerrainView both;
        build_terrain_view(meaning, frustum, both, &memory, &library, nullptr, 0.f, 0.f, false, nullptr, true, true);
        const std::size_t upper_z1 = count_at_z(upper, 1.f);
        const std::size_t ceiling_z1 = count_at_z(ceiling, 1.f);
        const std::size_t both_z1 = count_at_z(both, 1.f);
        const std::size_t both_z2 = count_at_z(both, 2.f);
        if (count_at_z(base, 1.f) != 0) {
            std::fprintf(stderr, "  **一人称の層: 切なのに z=1 に何か居ます（前提が崩れています）**\n");
            fps_layers_ok = false;
        }
        if ((upper_z1 == 0) || (count_at_z(upper, 2.f) != 0)) {
            std::fprintf(stderr, "  **一人称の層: 2 段目だけ入で z=1 に %zu 個 / z=2 に %zu 個**\n",
                upper_z1, count_at_z(upper, 2.f));
            fps_layers_ok = false;
        }
        if ((ceiling_z1 == 0) || (count_at_z(ceiling, 2.f) != 0)) {
            std::fprintf(stderr, "  **一人称の層: 天井だけ入で z=1 に %zu 個 / z=2 に %zu 個**\n",
                ceiling_z1, count_at_z(ceiling, 2.f));
            fps_layers_ok = false;
        }
        if ((both_z1 == 0) || (both_z2 == 0)) {
            std::fprintf(stderr, "  **一人称の層: 両方入で z=1 に %zu 個 / z=2 に %zu 個（天井が持ち上がっていない）**\n",
                both_z1, both_z2);
            fps_layers_ok = false;
        }
        if (!slabs_equal(base, both) || (base.prop_count != both.prop_count)) {
            std::fprintf(stderr, "  **一人称の層: 入にしたら既存の置き場所まで動きました（種が本流に混ざっています）**\n");
            fps_layers_ok = false;
        }
        std::fprintf(stderr, "  一人称の層: 2 段目 z=1 %zu 個 / 天井のみ z=1 %zu 個 / 両方 z=1 %zu + z=2 %zu 個\n",
            upper_z1, ceiling_z1, both_z1, both_z2);
    }

    /*
     * 四隅の欠けを数えるための背景色（P2 ④）。
     *
     * **P7 で 2 つになった。**画面用の FBO を消す色（`scene_clear`。線形の HDR）と、
     * それがトーンマップを通って既定のフレームバッファに出たときの色（`clear`）である。
     * 数えるのは後者で、`glReadPixels` が読むのはそちらだからである。
     *
     * ここは**この検査がポスト処理を切って走る理由**でもある。ビネットは四隅を暗くするので、
     * 掛けたまま数えると「背景色と一致する画素」が 0 になり、**欠けを検出できない検査**に化ける。
     * `--terrain-check` は幾何の検査であって見え方の検査ではない。見え方は `--post-check`。
     */
    const unsigned char scene_clear[3] = { 10, 13, 18 };
    unsigned char clear[3]{};
    for (int c = 0; c < 3; ++c) {
        const float linear = static_cast<float>(scene_clear[c]) / 255.f;
        clear[c] = static_cast<unsigned char>(std::lround(hd2d_tonemap_cpu(linear) * 255.f));
    }
    const PostFlags terrain_post{ false, false, false, false, false };
    std::string corner_report;
    double gap_ratio = 0.0;
    std::string round_trip;
    const bool round_trip_ok = camera_round_trip_check(camera, window_full, round_trip);
    std::string orientation;
    const bool orientation_ok = camera_orientation_check(camera, orientation);

    /*
     * **4 方位ぶんの往復検査と向きの検査**。
     * `--turn=` で撮る絵は 1 方位ぶんだけなので、当たり判定の検査はここでまとめて 4 通り回す
     * ——「クリック移動がずれる」は仕様違反（VOXEL 設計書 §4.5）であり、
     * 回した先で一度でも崩れたら回転そのものを出せない。
     *
     * 向きの検査を混ぜてあるのは、往復検査が**鏡像を捕まえられない**からである
     * （投影と逆写像が互いに合ってさえいれば通る。`camera.h` の注記）。
     * 90° の回転は左右を入れ替える種類の間違いを起こしやすいので、両方要る。
     */
    bool turn_checks_ok = true;
    for (int turn = 0; turn < kViewTurnCount; ++turn) {
        Camera turned = camera;
        turned.yaw = view_turn_yaw(turn);
        const ViewWindow turned_window = derive_view_window(turned, kMaxPropHeight, 60.f);
        std::string turn_round_trip;
        std::string turn_orientation;
        const bool rt_ok = camera_round_trip_check(turned, turned_window, turn_round_trip);
        const bool or_ok = camera_orientation_check(turned, turn_orientation);
        turn_checks_ok = turn_checks_ok && rt_ok && or_ok;
        std::fprintf(stderr, "  視点回転 turn=%d: 窓 %d×%d / 往復 %s（%s）/ 向き %s（%s）\n", turn,
            turned_window.cols, turned_window.rows, rt_ok ? "OK" : "**NG**", turn_round_trip.c_str(),
            or_ok ? "OK" : "**NG**", turn_orientation.c_str());
    }
    /*
     * **移動の回転の規約**（`rotate_screen_delta`）も同じ所で見る。表は `camera.h` の
     * doc と同じもので、ここが崩れると「画面の上へ押したのに横へ歩く」になる。
     * 純関数なので窓も GL も要らない——**検査の検査**として、わざと 1 行変えれば必ず落ちる。
     */
    {
        struct TurnExpect {
            int turn;
            int fwd_dx;
            int fwd_dy;
            int right_dx;
            int right_dy;
        };
        static const TurnExpect kExpect[kViewTurnCount] = {
            { 0, 0, -1, 1, 0 }, //!< 北を見る: 奥＝北・右＝東
            { 1, 1, -1, 1, 1 }, //!< 北東を見る: 奥＝北東・右＝南東
            { 2, 1, 0, 0, 1 }, //!< 東を見る: 奥＝東・右＝南
            { 3, 1, 1, -1, 1 }, //!< 南東を見る: 奥＝南東・右＝南西
            { 4, 0, 1, -1, 0 }, //!< 南を見る: 奥＝南・右＝西
            { 5, -1, 1, -1, -1 }, //!< 南西を見る: 奥＝南西・右＝北西
            { 6, -1, 0, 0, -1 }, //!< 西を見る: 奥＝西・右＝北
            { 7, -1, -1, 1, -1 }, //!< 北西を見る: 奥＝北西・右＝北東
        };
        for (const auto &e : kExpect) {
            int fdx = 0;
            int fdy = -1;
            rotate_screen_delta(e.turn, fdx, fdy);
            int rdx = 1;
            int rdy = 0;
            rotate_screen_delta(e.turn, rdx, rdy);
            const bool ok = (fdx == e.fwd_dx) && (fdy == e.fwd_dy) && (rdx == e.right_dx) && (rdy == e.right_dy);
            turn_checks_ok = turn_checks_ok && ok;
            std::fprintf(stderr, "  移動の回転 turn=%d: 奥(0,-1)->(%+d,%+d) 右(1,0)->(%+d,%+d) %s\n", e.turn, fdx, fdy,
                rdx, rdy, ok ? "OK" : "**NG**");
        }
    }

    /*
     * 光と影（P5）。合成フレームは地下（`FloorKind::Dungeon`）なので、
     * `make_scene_lighting()` は「上からやや南寄りの弱い方向光」を返す（`lighting.h` の注記）。
     *
     * `--shrink-shadow=N` は**検査の検査**（設計書 §14-4）。影の直方体を狭めれば
     * 画面の端が必ずはみ出すはずで、はみ出さなければ「覆えているかを見られない検査」だった
     * ということになる。`--shrink-view` と同じ考え方である。
     */
    SceneLighting lighting = make_scene_lighting(frame.lighting, frame.floor);
    const int dropped_lights = collect_point_lights(frame, camera.target, lighting);
    const ShadowFit fit = fit_shadow(camera, lighting.sun_dir, kMaxPropHeight, shadow.side(),
        static_cast<float>(options.shrink_shadow));
    std::string shadow_report;
    const bool shadow_ok = shadow_fit_covers_view(fit, camera, kMaxPropHeight, shadow_report);

    // 数フレーム回して安定してから読む（1 枚目はドライバの都合で空のことがある）。
    float z_near = 0.f;
    float z_far = 0.f;
    camera.depth_range(z_near, z_far);
    for (int i = 0; i < 4; ++i) {
        if (!post.resize(screen_w, screen_h, err)) {
            std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
            SDL_Quit();
            return 1;
        }
        /*
         * 影も込みで描く。**四隅の欠けを数える検査に影が混じらないよう**、
         * 消去色そのものは影では作られない（影は暗くするだけで、下塗り色にはならない）。
         */
        shadow.begin();
        renderer.draw_instanced_depth(floor_gpu, fit.view_projection, terrain.slabs.data(), terrain.slabs.size());
        renderer.draw_instanced_depth(wall_gpu, fit.view_projection, terrain.boxes.data(), terrain.boxes.size());
        draw_library_depth(renderer, library, terrain, fit.view_projection); //!< P10。本描画と同じ行列
        shadow.end(screen_w, screen_h);
        post.begin_scene(static_cast<float>(scene_clear[0]) / 255.f, static_cast<float>(scene_clear[1]) / 255.f,
            static_cast<float>(scene_clear[2]) / 255.f);
        renderer.begin(camera.view_projection(), lighting, fit.view_projection, shadow.texture(), shadow.side());
        renderer.begin_cutaway(Cutaway{}); //!< 幾何の検査なので抜かない
        renderer.draw_instanced(floor_gpu, terrain.slabs.data(), terrain.slabs.size());
        /*
         * 壁の下の地面（P7）。**影のパスへは渡さない**（`TerrainView::under_slabs` の注記）し、
         * **影も受けない**（P10 レビュー 10。壁の真下なので受けると必ず真っ黒になる）。
         */
        renderer.set_shadow_scale(0.f);
        renderer.draw_instanced(floor_gpu, terrain.under_slabs.data(), terrain.under_slabs.size());
        renderer.set_shadow_scale(1.f);
        renderer.draw_instanced(wall_gpu, terrain.boxes.data(), terrain.boxes.size());
        draw_library_color(renderer, library, terrain); //!< P10。ライブラリのバケット（地面 → 立つもの）
        post.end_scene(screen_w, screen_h);
        post.resolve(terrain_post, PostParams{}, z_near, z_far, camera.distance, screen_w, screen_h);
        gap_ratio = count_corner_gaps(screen_w, screen_h, clear, 96, corner_report);
        /*
         * 戦闘の見せ場（`--combat-fx-check`）。**四隅の欠けを数えた後に重ねる**
         * ——先に重ねると、被弾の全面フラッシュが消去色を塗り替えて欠けが 0 に見える。
         */
        if (options.combat_fx_check && (i == 3)) {
            overlay_combat_fx(paint, camera, screen_w, screen_h, 100.5f, 44.5f);
        }
        if (!options.shot_path.empty() && (i == 3)) {
            (void)save_framebuffer_bmp(options.shot_path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);
    }

    /*
     * (7'') **上空から差し込む光の柱**（2026-08-22 に決めた。陽だまり）。
     *
     * 陽だまりは Sil-Q の地形なので、この合成の階には 1 マスも無い。**柱を自分で立てて**
     * 「加算で薄く重なること」「後ろのものを消さないこと」を画素で見る。
     *
     * 見るのは 2 つ:
     *   1. **描くと画が変わる**（柱が出ている）
     *   2. **明るくなる方向にしか変わらない**（加算なので、どの画素も暗くならない）。
     *      α 混合で書いてしまうと床の色を殺すので、ここで捕まる。
     */
    bool shaft_ok = true;
    {
        LightShaftRenderer shafts;
        std::string shaft_err;
        if (!shafts.init(shaft_err)) {
            std::fprintf(stderr, "  (7'') 光の柱: 作れません（%s）\n", shaft_err.c_str());
            shaft_ok = false;
        } else {
            /*
             * **絵は濃さを上げて撮る。** 陽だまりの既定（`terrain_prefabs.jsonc` の
             * `alpha` 0.18）は「薄め」の指示どおりで、この検査の階（溶岩で明るい部屋）では
             * 形が読めない。**画素で見るのは既定の濃さ**（下の 2 回）、
             * **人が形を見るのは濃い版**（`--shot=` の 2 枚）と分けてある。
             */
            const auto shoot_shaft = [&](bool draw_shaft, float alpha = 0.f) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, screen_w, screen_h);
                glClearColor(static_cast<float>(clear[0]) / 255.f, static_cast<float>(clear[1]) / 255.f,
                    static_cast<float>(clear[2]) / 255.f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                renderer.begin(camera.view_projection(), lighting, fit.view_projection, shadow.texture(),
                    shadow.side());
                renderer.begin_cutaway(Cutaway{}); //!< 幾何の検査なので抜かない
                renderer.draw_instanced(floor_gpu, terrain.slabs.data(), terrain.slabs.size());
                renderer.draw_instanced(wall_gpu, terrain.boxes.data(), terrain.boxes.size());
                draw_library_color(renderer, library, terrain);
                if (draw_shaft) {
                    //! 自機のまわりに 3 本（1 本だと「たまたま」を見分けられない）。
                    std::vector<LightShaftInstance> beams;
                    for (int n = 0; n < 3; ++n) {
                        LightShaftInstance one;
                        one.x = 100.5f + static_cast<float>(n) * 1.5f;
                        one.y = 44.5f;
                        one.z = 0.f;
                        if (alpha > 0.f) {
                            one.a = alpha;
                        }
                        beams.push_back(one);
                    }
                    shafts.draw(camera.view_projection(), camera.azimuth(), beams.data(), beams.size());
                }
                return read_framebuffer(screen_w, screen_h);
            };
            const std::vector<unsigned char> without = shoot_shaft(false);
            const std::vector<unsigned char> with = shoot_shaft(true);
            std::size_t changed = 0;
            std::size_t darker = 0;
            if (without.size() == with.size()) {
                for (std::size_t at = 0; at + 3u <= without.size(); at += 4u) {
                    bool diff = false;
                    for (int k = 0; k < 3; ++k) {
                        if (with[at + static_cast<std::size_t>(k)] != without[at + static_cast<std::size_t>(k)]) {
                            diff = true;
                        }
                        if (with[at + static_cast<std::size_t>(k)] < without[at + static_cast<std::size_t>(k)]) {
                            ++darker;
                        }
                    }
                    if (diff) {
                        ++changed;
                    }
                }
            }
            std::fprintf(stderr, "  (7'') 光の柱: 変わった画素 %zu / 暗くなった成分 %zu\n",
                changed, darker);
            if (changed == 0) {
                std::fprintf(stderr, "  **FAIL** 光の柱: 描いても画が 1 画素も変わりません\n");
                shaft_ok = false;
            }
            if (darker != 0) {
                std::fprintf(stderr,
                    "  **FAIL** 光の柱: 暗くなった画素があります（加算になっていません）\n");
                shaft_ok = false;
            }
            if (!options.shot_path.empty()) {
                //! **有り／無しの 2 枚**を書く（人が並べて見ないと濃さは決められない）。
                for (const bool on : { false, true }) {
                    (void)shoot_shaft(on, 0.75f);
                    std::string path = options.shot_path;
                    const std::size_t dot = path.find_last_of('.');
                    const std::string tag = on ? "_shaft_on" : "_shaft_off";
                    path = (dot == std::string::npos) ? (path + tag)
                                                      : (path.substr(0, dot) + tag + path.substr(dot));
                    (void)save_framebuffer_bmp(path, screen_w, screen_h);
                }
            }
            SDL_GL_SwapWindow(window.window);
        }
        shafts.shutdown();
    }

    /*
     * (8) **画調**。同じ幾何を 3 回描いて突き合わせる。
     *
     * | 回 | 何を掛けるか | 何を見るか |
     * |---|---|---|
     * | A | 何も掛けない（`set_look` を 1 度も呼ばない） | 基準 |
     * | B | **標準の画調**を全部通す（`set_look` ＋ 光 ＋ 後処理 ＋ LUT） | **A と 1 バイトも違わないこと** |
     * | C | TRON | A と違うこと・**A より暗いこと**・明るい線の画素があること |
     *
     * B が要点である。「切ったときの絵が 1 ビットも変わらない」は必守制約 4 そのもので、
     * *掛ける道を通した上で* 同じでなければ意味が無い（`enabled = 0` を目で読むだけでは、
     * 光や後処理の側の取りこぼしを捕まえられない）。
     *
     * 後処理は**全部入り**で回す。TRON の線は「1 を超えた値がブルームで滲む」ことで
     * できているので、切って比べると画調の要が検査から抜ける（上の (2) の欠け数えとは
     * 目的が違うので、消去色の一致は見ない）。
     */
    bool look_ok = true;
    {
        const auto look_fail = [&look_ok](const std::string &why) {
            std::fprintf(stderr, "  **NG** (8) 画調: %s\n", why.c_str());
            look_ok = false;
        };
        /*
         * **どの回もまったく同じ手順で描く。**違うのは画調だけにしないと比較が意味を持たない
         * ——一度、`under_slabs` を落とした写しで「字を切った絵」を作ってしまい、
         * 「字が 33 万画素出ている」という**嘘の数字**が出た（§8-8）。写しを作らないこと。
         */
        /*
         * **TRON はマス 1 つにつき立方体 1 つ**（§11）。地形の組み立てからして別物なので、
         * 検査もそちらを描かなければ「実際に出る絵」を見たことにならない。
         */
        TerrainView tron_terrain;
        build_terrain_view(meaning, Frustum::from(camera.view_projection()), tron_terrain, &memory, &library,
            nullptr, 0.f, 0.f, false, nullptr, false, false, true);
        const auto render_once
            = [&](SceneLookKind kind, bool apply, bool face_glyph, std::vector<unsigned char> &out) {
            const TerrainView &geom = (kind == SceneLookKind::Tron) ? tron_terrain : terrain;
            SceneLighting lit = lighting;
            PostParams params;
            if (apply) {
                apply_look_to_lighting(kind, false, lit);
                apply_look_to_post(kind, false, params);
                LookParams lp = make_look_params(kind);
                lp.face_glyph = face_glyph;
                renderer.set_look(lp);
                std::string lut_err;
                (void)post.set_grade(look_grade(kind), lut_err);
            }
            shadow.begin();
            renderer.draw_instanced_depth(floor_gpu, fit.view_projection, geom.slabs.data(), geom.slabs.size());
            renderer.draw_instanced_depth(wall_gpu, fit.view_projection, geom.boxes.data(), geom.boxes.size());
            draw_library_depth(renderer, library, geom, fit.view_projection);
            shadow.end(screen_w, screen_h);
            const Vec3 bg = apply
                ? look_clear_color(kind, false,
                    Vec3{ static_cast<float>(scene_clear[0]) / 255.f, static_cast<float>(scene_clear[1]) / 255.f,
                        static_cast<float>(scene_clear[2]) / 255.f })
                : Vec3{ static_cast<float>(scene_clear[0]) / 255.f, static_cast<float>(scene_clear[1]) / 255.f,
                      static_cast<float>(scene_clear[2]) / 255.f };
            post.begin_scene(bg.x, bg.y, bg.z);
            renderer.begin(camera.view_projection(), lit, fit.view_projection, shadow.texture(), shadow.side());
            renderer.begin_cutaway(Cutaway{});
            renderer.draw_instanced(floor_gpu, geom.slabs.data(), geom.slabs.size());
            renderer.set_shadow_scale(0.f);
            renderer.draw_instanced(floor_gpu, geom.under_slabs.data(), geom.under_slabs.size());
            renderer.set_shadow_scale(1.f);
            renderer.draw_instanced(wall_gpu, geom.boxes.data(), geom.boxes.size());
            draw_library_color(renderer, library, geom);
            post.end_scene(screen_w, screen_h);
            post.resolve(PostFlags{}, params, z_near, z_far, camera.distance, screen_w, screen_h);
            out.assign(static_cast<std::size_t>(screen_w) * static_cast<std::size_t>(screen_h) * 4, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glReadPixels(0, 0, screen_w, screen_h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        };
        /*
         * 平均の明るさ・「線らしい画素」（どれかの成分が 200 超）・**「沈んだ面」**
         * （どれも 24 未満）の割合。
         *
         * **明るさの平均で比べてはいけない。**TRON は幾何そのものが違う（マスごとの立方体＝
         * 縁が桁違いに多い）ので、平均は標準より高くなって当たり前である。実際に
         * 「TRON のほうが明るい」で落ちた——**検査の前提のほうが古かった**。
         * 見るべきは「面が黒いガラスになっているか」で、それは沈んだ画素の割合に出る。
         */
        const auto measure = [](const std::vector<unsigned char> &px, double &mean, double &bright, double &dark) {
            long long sum = 0;
            long long hot = 0;
            long long sunk = 0;
            const std::size_t count = px.size() / 4;
            for (std::size_t i = 0; i < count; ++i) {
                const int r = px[(i * 4) + 0];
                const int g = px[(i * 4) + 1];
                const int b = px[(i * 4) + 2];
                sum += (r + g + b);
                const int peak = std::max({ r, g, b });
                if (peak > 200) {
                    ++hot;
                }
                if (peak < 24) {
                    ++sunk;
                }
            }
            mean = (count > 0) ? (static_cast<double>(sum) / static_cast<double>(count * 3)) : 0.0;
            bright = (count > 0) ? (static_cast<double>(hot) / static_cast<double>(count)) : 0.0;
            dark = (count > 0) ? (static_cast<double>(sunk) / static_cast<double>(count)) : 0.0;
        };

        if (make_look_params(SceneLookKind::Standard).enabled != 0) {
            look_fail("`make_look_params(Standard)` が `enabled = 0` を返していません");
        }
        /*
         * **面のアスキー文字の材**（§10）。合成フレームは記号を持たないので、
         * 2D アスキーの検査（(2''')）と同じ割り当てを入れてから記憶へ流す。
         * ここまで用意して初めて「字が出ているか」を画素で数えられる。
         */
        GlyphAtlas look_font;
        TerrainMemory look_memory;
        GLuint look_cells = 0;
        {
            GameFrame lettered = frame;
            for (auto &cell : lettered.cells) {
                cell.feature_flags |= CELL_FEAT_KNOWN;
                if (cell.ascii_fallback != '\0') {
                    continue;
                }
                const bool wall = (cell.feature_flags & CELL_FEAT_WALL) != 0u;
                cell.ascii_fallback = wall ? '#' : '.';
                cell.fg_color = wall ? 2 : 9;
            }
            look_memory.update(lettered);
            std::string font_err;
            if (!look_font.init(GlyphAtlas::kDefaultPx, font_err) || !look_font.bake_sheet(font_err)) {
                look_fail("文字シートを用意できません: " + font_err);
            } else if (!look_memory.valid()) {
                look_fail("記憶が空です（合成フレームのマスを読めていない）");
            } else {
                std::vector<std::uint8_t> rgba(
                    static_cast<std::size_t>(look_memory.width) * static_cast<std::size_t>(look_memory.height) * 4u,
                    0u);
                std::size_t lettered_cells = 0;
                for (std::size_t i = 0; i < look_memory.ascii.size(); ++i) {
                    if (look_memory.ascii[i] == 0u) {
                        continue;
                    }
                    const RgbColor rgb = term_color_to_rgb(look_memory.fg[i]);
                    rgba[(i * 4u) + 0u] = look_memory.ascii[i];
                    rgba[(i * 4u) + 1u] = rgb.r;
                    rgba[(i * 4u) + 2u] = rgb.g;
                    rgba[(i * 4u) + 3u] = rgb.b;
                    ++lettered_cells;
                }
                if (lettered_cells == 0) {
                    look_fail("記号を覚えたマスが 1 つもありません");
                }
                glGenTextures(1, &look_cells);
                glBindTexture(GL_TEXTURE_2D, look_cells);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8UI), look_memory.width,
                    look_memory.height, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, rgba.data());
                glBindTexture(GL_TEXTURE_2D, 0);
                LookGlyphSource src;
                src.sheet = look_font.sheet_texture();
                src.cells = look_cells;
                src.width = look_memory.width;
                src.height = look_memory.height;
                renderer.set_look_glyphs(src);
            }
        }
        std::vector<unsigned char> base_px;
        std::vector<unsigned char> std_px;
        std::vector<unsigned char> tron_px;
        render_once(SceneLookKind::Standard, false, true, base_px);
        render_once(SceneLookKind::Standard, true, true, std_px);
        render_once(SceneLookKind::Tron, true, true, tron_px);

        if (base_px != std_px) {
            std::size_t differ = 0;
            for (std::size_t i = 0; (i < base_px.size()) && (i < std_px.size()); ++i) {
                if (base_px[i] != std_px[i]) {
                    ++differ;
                }
            }
            char buf[160]{};
            std::snprintf(buf, sizeof(buf),
                "標準の画調を通したら絵が変わりました（%zu バイト違う）。必守制約「切ったら 1 ビットも変わらない」違反です",
                differ);
            look_fail(buf);
        }
        double base_mean = 0.0;
        double base_bright = 0.0;
        double base_dark = 0.0;
        double tron_mean = 0.0;
        double tron_bright = 0.0;
        double tron_dark = 0.0;
        measure(base_px, base_mean, base_bright, base_dark);
        measure(tron_px, tron_mean, tron_bright, tron_dark);
        if (base_px == tron_px) {
            look_fail("TRON にしても絵が変わりませんでした（画調が 1 つも届いていない）");
        }
        if (tron_dark <= base_dark) {
            char buf[192]{};
            std::snprintf(buf, sizeof(buf),
                "TRON の面が沈んでいません（沈んだ画素 標準 %.1f%% / TRON %.1f%%）。面は黒いガラスのはずです",
                base_dark * 100.0, tron_dark * 100.0);
            look_fail(buf);
        }
        if (tron_bright <= 0.0) {
            look_fail("TRON に明るい線の画素が 1 つもありません（線が出ていない）");
        }
        /*
         * **立方体だけになっているか**（§11。「全てシンプルな立方体」と決めた
         * 「小物オブジェクトは不要」）。ライブラリのプレハブが 1 つでも残っていたら、
         * 既存のブロック（レンガの壁・木・建物）がそのまま出てしまう。
         */
        {
            std::size_t lib_instances = 0;
            for (const auto &bucket : tron_terrain.lib) {
                lib_instances += bucket.size();
            }
            if (lib_instances > 0) {
                look_fail("TRON の地形にライブラリのプレハブが残っています（立方体だけになっていない）");
            }
            if (tron_terrain.prop_count != 0) {
                char buf[128]{};
                std::snprintf(buf, sizeof(buf), "TRON の地形に小物が %d 個あります（指示は「不要」）",
                    tron_terrain.prop_count);
                look_fail(buf);
            }
            std::fprintf(stderr, "  画調: TRON の地形 箱 %zu / 板 %zu / 小物 %d / ライブラリの置き場所 %zu\n",
                tron_terrain.boxes.size(), tron_terrain.slabs.size(), tron_terrain.prop_count, lib_instances);
        }
        std::fprintf(stderr,
            "  画調: 標準=基準と一致 %s / 沈んだ面 %.1f%%→%.1f%% / 線の画素 %.2f%%→%.2f%% / 平均 %.1f→%.1f%s\n",
            (base_px == std_px) ? "OK" : "**NG**", base_dark * 100.0, tron_dark * 100.0, base_bright * 100.0,
            tron_bright * 100.0, base_mean, tron_mean, look_ok ? "" : "  **NG**");
        /*
         * **面の字が本当に出ているか。**字を切って**同じ `render_once` で**描き直し、
         * 絵が変わることを見る。線と黒い面だけでも「TRON らしく」見えてしまうので、
         * 字が 1 枚も出ていないことに気づける道が要る。
         */
        {
            std::vector<unsigned char> no_glyph_px;
            render_once(SceneLookKind::Tron, true, false, no_glyph_px);
            std::size_t glyph_pixels = 0;
            /*
             * **出た場所も言う。**数だけだと「画面のどこかで何かが変わった」までしか
             * 分からず、字がマスの面に乗っているのか、まったく別の所が動いたのかを
             * 見分けられない（実際にそこで一度誤診した。§8-8）。
             */
            int min_x = screen_w;
            int max_x = -1;
            int min_y = screen_h;
            int max_y = -1;
            for (int y = 0; y < screen_h; ++y) {
                for (int x = 0; x < screen_w; ++x) {
                    const std::size_t i
                        = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(screen_w))
                              + static_cast<std::size_t>(x))
                        * 4u;
                    if ((tron_px[i] == no_glyph_px[i]) && (tron_px[i + 1] == no_glyph_px[i + 1])
                        && (tron_px[i + 2] == no_glyph_px[i + 2])) {
                        continue;
                    }
                    ++glyph_pixels;
                    min_x = std::min(min_x, x);
                    max_x = std::max(max_x, x);
                    min_y = std::min(min_y, y);
                    max_y = std::max(max_y, y);
                }
            }
            if (glyph_pixels == 0) {
                look_fail("面のアスキー文字が 1 画素も出ていません（字を切っても絵が同じ）");
            }
            std::fprintf(stderr, "  画調: 面の字 %zu 画素（x %d..%d / y %d..%d。**左下原点**）\n",
                glyph_pixels, min_x, max_x, min_y, max_y);
        }

        /*
         * **絵も残す。**数字だけでは「線が出た」としか言えず、TRON に見えるかは目でしか
         * 判らない（記憶 `hengband-hd2d-verification-trap`）。`--shot=` があれば
         * 標準の絵の隣へ `_tron.bmp` を置く。
         *
         * **撮る前に「字あり」で描き直す。**直前の描画は上の比較で字を切ったほうなので、
         * そのまま読むと**字の無い絵が残る**。一度これで「字が出ていない」と誤診した（§8-9）。
         */
        if (!options.shot_path.empty()) {
            render_once(SceneLookKind::Tron, true, true, tron_px);
            //! `x.bmp` の隣へ `x_tron.bmp`。拡張子が無ければ末尾へ足す（`--shot=` の書式は自由）。
            std::string tron_path = options.shot_path;
            const auto dot = tron_path.find_last_of('.');
            const auto slash = tron_path.find_last_of("/" "\\");
            if ((dot != std::string::npos) && ((slash == std::string::npos) || (dot > slash))) {
                tron_path.insert(dot, "_tron");
            } else {
                tron_path += "_tron.bmp";
            }
            (void)save_framebuffer_bmp(tron_path, screen_w, screen_h);
        }
        //! 後片付け。**次の検査へ画調を持ち越さない。**
        renderer.set_look(make_look_params(SceneLookKind::Standard));
        renderer.set_look_glyphs(LookGlyphSource{});
        if (look_cells != 0) {
            glDeleteTextures(1, &look_cells);
        }
        look_font.shutdown();
        std::string lut_err;
        (void)post.set_grade(GradeParams{}, lut_err);
    }

    /*
     * (7) **アスキー実体**（こう決めた——「きゃら、モンスター、
     * アイテムはアスキーを板にしたもの」）。
     *
     * ここで閉じられるのが要点である——**字はコアの目録（`TileCatalog`）を引かない**ので、
     * コアを起こさずに `EntityTracker::build` のアスキーの枝をそのまま通せる
     * （板の実体はタイルの絵が要るので、この検査ではどうにもならない。§17.3）。
     *
     * 見るのは 5 つ:
     *   1. アスキーでは `glyphs` が埋まり `slabs` が空（**排他**であること）
     *   2. 板では逆（アスキーの枝が本線を汚していないこと）
     *   3. 作った矩形がアトラスの中にあり、幅と高さが正であること
     *   4. 色が**コアの 16 色そのもの**であること（`TermPalette` を引けていること）
     *   5. **タイルの無い実体も出る**こと（字は目録に依らない＝アスキーの利点そのもの）
     */
    bool glyph_ok = true;
    {
        /*
         * 失敗の伝え方は**この関数の流儀**に合わせる（bool を畳んで最後の `all_ok` へ入れる）。
         * `--cutaway-check` の `fail()` のような仕掛けはここには無い。
         */
        const auto glyph_fail = [&glyph_ok](const std::string &why) {
            std::fprintf(stderr, "  **NG** (7) アスキー実体: %s\n", why.c_str());
            glyph_ok = false;
        };
        GlyphAtlas glyphs;
        std::string glyph_err;
        if (!glyphs.init(GlyphAtlas::kDefaultPx, glyph_err)) {
            glyph_fail( "アトラスを用意できません: " + glyph_err);
        } else {
            /*
             * 実体を 3 体置く。**タイル索引を持たせない 1 体を混ぜる**のが 5 番目の検査で、
             * 板のときは消えていた実体である。
             */
            GameFrame probe = make_synthetic_frame(view_w, view_h, 100, 44);
            struct Placed {
                int gx;
                int gy;
                char ch;
                std::uint8_t color;
                bool monster;
                bool object;
                bool player;
                std::uint16_t tile;
            };
            const Placed placed[] = {
                { 100, 44, '@', 1, false, false, true, 1 }, //!< 自分（白）
                { 101, 44, 'k', 5, true, false, false, 1 }, //!< 敵（緑）
                { 102, 44, '!', 11, false, true, false, 0 }, //!< 道具（黄）。**タイル索引 0**
            };
            for (const Placed &it : placed) {
                for (auto &cell : probe.cells) {
                    if ((cell.gx != it.gx) || (cell.gy != it.gy)) {
                        continue;
                    }
                    cell.feature_flags |= CELL_FEAT_KNOWN;
                    if (it.player) {
                        cell.feature_flags |= CELL_FEAT_PLAYER;
                    }
                    cell.monster_id = it.monster ? 42u : 0u;
                    cell.monster_slot = it.monster ? 7u : 0u;
                    cell.object_id = it.object ? 99u : 0u;
                    cell.ascii_fallback = it.ch;
                    cell.fg_color = it.color;
                    cell.tile_index = it.tile;
                    break;
                }
            }

            EntityTracker tracker;
            TileCatalog no_catalog; //!< アスキーでは引かれない（引いたら 5 番目の検査が落ちる）
            SlabLibrary no_slabs;
            EntityView ascii_view;
            tracker.build(probe, no_catalog, no_slabs, renderer, 0.f, ascii_view, false, 0.f, SlabFacing{}, &glyphs);
            EntityView slab_view;
            tracker.build(probe, no_catalog, no_slabs, renderer, 0.f, slab_view, false, 0.f, SlabFacing{}, nullptr);

            std::fprintf(stderr, "  (7) アスキー実体: 字 %zu 枚 / 板 %zu 枚（板の道では 目録なしで %zu 枚）\n",
                ascii_view.glyphs.size(), ascii_view.slabs.size(), slab_view.slabs.size());
            //! 1・2: 排他であること。
            if (ascii_view.glyphs.size() != 3) {
                glyph_fail( "3 体置いたのに字が " + std::to_string(ascii_view.glyphs.size()) + " 枚です");
            }
            if (!ascii_view.slabs.empty()) {
                glyph_fail( "アスキーなのに板も作られています（表現が排他になっていません）");
            }
            if (!slab_view.glyphs.empty()) {
                glyph_fail( "板なのに字も作られています（表現が排他になっていません）");
            }
            //! 5: **タイルの無い実体も出る**（板の道では目録が無いので 1 枚も出ないのが正しい）。
            if (!slab_view.slabs.empty()) {
                glyph_fail( "目録が無いのに板が作られました（この検査の前提が崩れています）");
            }
            /*
             * (7-b) **板の道の字の受け皿**（
             * 決めたこと F5）。板の道で目録に無い実体は、受け皿を渡せば
             * `ascii_fallback` の字の板で出る。目録が空なので 3 体とも字になるのが正しい。
             * 受け皿を渡さない道（上の `slab_view`）が空のままなのは 2・5 で見ている
             * ——切れば従来どおり、が入切の約束である。
             */
            EntityView fb_view;
            tracker.build(probe, no_catalog, no_slabs, renderer, 0.f, fb_view, false, 0.f, SlabFacing{},
                nullptr, 0.8f, &glyphs);
            std::fprintf(stderr, "  (7-b) 字の受け皿: 板の道で目録なし 3 体 → 字 %zu 枚\n",
                fb_view.glyphs.size());
            if (fb_view.glyphs.size() != 3) {
                glyph_fail("受け皿を渡したのに字が " + std::to_string(fb_view.glyphs.size())
                    + " 枚です（3 枚のはず）");
            }
            if (!fb_view.slabs.empty()) {
                glyph_fail("受け皿の道で板も作られています（目録が空なのに）");
            }
            //! 3・4: 矩形と色。
            const int side = glyphs.side();
            for (std::size_t i = 0; i < ascii_view.glyphs.size(); ++i) {
                const BillboardInstance &b = ascii_view.glyphs[i];
                const bool rect_ok = (b.u1 > b.u0) && (b.v1 > b.v0) && (b.u0 >= 0.f) && (b.v0 >= 0.f)
                    && (b.u1 <= static_cast<float>(side)) && (b.v1 <= static_cast<float>(side));
                const bool size_ok = (b.width > 0.01f) && (b.height > 0.01f);
                if (!rect_ok || !size_ok) {
                    glyph_fail(
                        "字 " + std::to_string(i) + " の矩形か寸法が壊れています（uv "
                            + std::to_string(b.u0) + "," + std::to_string(b.v0) + ".."
                            + std::to_string(b.u1) + "," + std::to_string(b.v1) + " / "
                            + std::to_string(b.width) + "x" + std::to_string(b.height) + "）");
                }
            }
            /*
             * 色は**置いた順ではなく座標で照らし合わせる**（`frame.cells` の並びに依らない）。
             * ここを添字で見ると、並びが変わったときに黙って別の色を比べる検査になる。
             */
            for (const Placed &it : placed) {
                const RgbColor want = term_color_to_rgb(it.color);
                bool found = false;
                for (const BillboardInstance &b : ascii_view.glyphs) {
                    if ((std::fabs(b.x - (static_cast<float>(it.gx) + 0.5f)) > 0.01f)
                        || (std::fabs(b.y - (static_cast<float>(it.gy) + 0.5f)) > 0.01f)) {
                        continue;
                    }
                    found = true;
                    const bool color_ok = (std::fabs(b.r - (static_cast<float>(want.r) / 255.f)) < 0.01f)
                        && (std::fabs(b.g - (static_cast<float>(want.g) / 255.f)) < 0.01f)
                        && (std::fabs(b.b - (static_cast<float>(want.b) / 255.f)) < 0.01f);
                    if (!color_ok) {
                        glyph_fail(
                            std::string("'") + it.ch + "' の色がコアの色表と違います");
                    }
                    break;
                }
                if (!found) {
                    glyph_fail( std::string("'") + it.ch + "' の字が置かれていません");
                }
            }
            /*
             * **本当に画素が出るか。**ここまでは全部 CPU の話で、
             * 「列は正しいのに 1 画素も描かれない」を素通りさせてしまう。
             * 板を 1 回流して、実体の居る辺りの画素が背景から変わることを見る。
             */
            BillboardRenderer boards;
            std::string board_err;
            if (!boards.init(board_err)) {
                glyph_fail( "ビルボードを用意できません: " + board_err);
            } else {
                /*!
                 * @param with_glyphs 字を重ねるか（**数えるための対**を作る）。
                 * @param with_terrain 地形も描くか。
                 *
                 * @details 数えるときは地形を描かない（背景が一様だと差がそのまま字の画素になる）。
                 * **人に見せる絵では地形を描く**——縁取りが要るかどうか・大きさが適切かどうかは、
                 * 明るい床の上に置いてみないと判断できない（設計書 §14-2）。
                 */
                const auto shoot_glyphs = [&](bool with_glyphs, bool with_terrain = false) {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, screen_w, screen_h);
                    glClearColor(static_cast<float>(scene_clear[0]) / 255.f,
                        static_cast<float>(scene_clear[1]) / 255.f,
                        static_cast<float>(scene_clear[2]) / 255.f, 1.f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    if (with_terrain) {
                        //! 本描画と**同じ順**（地面 → 壁の下の地面 → 壁 → ライブラリ）。抜きは掛けない。
                        renderer.begin(camera.view_projection(), lighting, fit.view_projection, shadow.texture(),
                            shadow.side());
                        renderer.begin_cutaway(Cutaway{});
                        renderer.draw_instanced(floor_gpu, terrain.slabs.data(), terrain.slabs.size());
                        renderer.set_shadow_scale(0.f);
                        renderer.draw_instanced(floor_gpu, terrain.under_slabs.data(), terrain.under_slabs.size());
                        renderer.set_shadow_scale(1.f);
                        renderer.draw_instanced(wall_gpu, terrain.boxes.data(), terrain.boxes.size());
                        draw_library_color(renderer, library, terrain);
                    }
                    if (with_glyphs) {
                        boards.draw(camera.view_projection(), lighting, camera.azimuth(), camera.eye(),
                            fit.view_projection, 0, shadow.side(), glyphs.texture(), glyphs.side(),
                            ascii_view.glyphs.data(), ascii_view.glyphs.size(), nullptr, /*fullbright=*/true);
                    }
                    return read_framebuffer(screen_w, screen_h);
                };
                const std::vector<unsigned char> without = shoot_glyphs(false);
                const std::vector<unsigned char> with = shoot_glyphs(true);
                const std::size_t drawn = count_changed_pixels(without, with, 0);
                std::fprintf(stderr, "  (7) アスキー実体の画素: %zu\n", drawn);
                if (drawn == 0) {
                    glyph_fail( "字の列はできているのに 1 画素も描かれていません");
                }
                if (!options.shot_path.empty()) {
                    //! **地形の上に重ねて**撮る（読みやすさを見るための絵）。
                    (void)shoot_glyphs(true, true);
                    std::string path = options.shot_path;
                    const std::size_t dot_at = path.find_last_of('.');
                    path = (dot_at == std::string::npos) ? (path + "_glyphs")
                                                         : (path.substr(0, dot_at) + "_glyphs" + path.substr(dot_at));
                    (void)save_framebuffer_bmp(path, screen_w, screen_h);
                }
                SDL_GL_SwapWindow(window.window);
                boards.shutdown();
            }
            glyphs.shutdown();
        }
    }

    std::fprintf(stderr, "[hd2d] terrain-check viewport=%dx%d cell=%.0fpx pitch=%.0f fov_x=%.0f dist=%.1f\n",
        screen_w, screen_h, options.camera_cell_px, camera.pitch * 57.2958f, camera.fov_x * 57.2958f,
        camera.distance);
    std::fprintf(stderr, "  derived view = %dx%d (N%.1f S%.1f W%.1f E%.1f)  requested = %dx%d (shrink=%d)\n",
        window_full.cols, window_full.rows, window_full.north, window_full.south,
        window_full.west, window_full.east, view_w, view_h, options.shrink_view);
    std::fprintf(stderr, "  %s\n", floor_meaning_summary(meaning).c_str());
    std::fprintf(stderr, "  boxes=%zu slabs=%zu props=%d lib=%d  cells considered=%d culled=%d drawn=%d\n",
        terrain.boxes.size(), terrain.slabs.size(), terrain.prop_count, terrain.lib_instances,
        terrain.considered_cells, terrain.culled_cells, terrain.drawn_cells);
    std::fprintf(stderr, "  round-trip: %s (%s)\n", round_trip_ok ? "OK" : "FAILED", round_trip.c_str());
    std::fprintf(stderr, "  orientation: %s (%s)\n", orientation_ok ? "OK" : "FAILED", orientation.c_str());
    std::fprintf(stderr, "  corner gaps (clear colour counted): %.3f%%  [%s]\n", gap_ratio * 100.0, corner_report.c_str());
    std::fprintf(stderr, "  shadow fit: %s (%s) shrink=%d  map=%dx%d\n", shadow_ok ? "OK" : "FAILED",
        shadow_report.c_str(), options.shrink_shadow, shadow.side(), shadow.side());
    std::fprintf(stderr, "  lighting: sun=(%.2f,%.2f,%.2f) ambient=%.2f shadow_strength=%.2f points=%zu (dropped=%d)\n",
        lighting.sun_dir.x, lighting.sun_dir.y, lighting.sun_dir.z, lighting.ambient_scale,
        lighting.shadow_strength, lighting.points.size(), dropped_lights);

    post.shutdown();
    shadow.shutdown();
    library.release_gpu(renderer);
    renderer.release(wall_gpu);
    renderer.release(floor_gpu);
    renderer.shutdown();
    text.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();

    const bool gaps_ok = gap_ratio < 0.0005; // 0.05% 未満なら「欠けなし」
    const bool all_ok = round_trip_ok && gaps_ok && orientation_ok && shadow_ok && dark_unknown_ok && fps_layers_ok
        && turn_checks_ok && glyph_ok && look_ok && shaft_ok && rebuild_ok;
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", all_ok ? "PASS" : "FAIL");
    return all_ok ? 0 : 1;
}

/*!
 * @brief `--post-check`: ポスト処理が**本当に効いているか**を画素で確かめる。
 *
 * @details 見るのは 5 つ。
 * | # | 何を | なぜ |
 * |---|---|---|
 * | 1 | GPU のトーンマップ ＝ CPU の `hd2d_tonemap_cpu()` | 式が 2 か所にある。片方だけ直す事故を毎回捕まえる |
 * | 2 | 全部切れば 2 回目も 1 回目と**完全に同じ** | パスの間に状態が漏れていないこと |
 * | 3 | 効果を 1 つ入れると画素が**変わる** | 繋ぎ忘れは「変わらない」として出る |
 * | 4 | ビネットは**四隅だけ**暗くする | 「何かが変わった」では効果の取り違えを捕まえられない |
 * | 5 | カラーグレーディングの結果が CPU で引いた LUT と一致 | 3D テクスチャの軸の並び・巻き方まで見る |
 *
 * **検査の検査**は `HD2D_BREAK_POST=all`（(3) が落ちる）。
 */
int run_post_check(const AppOptions &options)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[hd2d] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    Window window;
    std::string err;
    if (!create_window(options, window, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    VoxelRenderer renderer;
    ShadowMap shadow;
    PostChain post;
    int screen_w = options.window_w;
    int screen_h = options.window_h;
    SDL_GetWindowSize(window.window, &screen_w, &screen_h);
    if (!renderer.init(err) || !shadow.init(kShadowMapSide, err) || !init_post_chain(post, options, err)
        || !post.resize(screen_w, screen_h, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }

    int failures = 0;
    const auto fail = [&failures](const char *what, const std::string &detail) {
        std::fprintf(stderr, "  **FAIL** %s: %s\n", what, detail.c_str());
        ++failures;
    };

    const PostFlags all_off{ false, false, false, false, false };
    //! **検査は既定値で通す。**撮るときだけ `--dof=` に従って書き換える（下の `--shot`）。
    PostParams params;
    float z_near = 1.f;
    float z_far = 100.f;

    /* --- (1) GPU のトーンマップ ＝ CPU の式 -------------------------------- */
    {
        std::string worst;
        int worst_delta = 0;
        for (const float v : { 0.02f, 0.10f, 0.35f, 0.75f, 1.40f, 3.00f }) {
            post.begin_scene(v, v, v); //!< 幾何を描かない。消去色そのものが線形の HDR
            post.end_scene(screen_w, screen_h);
            post.resolve(all_off, params, z_near, z_far, 10.f, screen_w, screen_h);
            const std::vector<unsigned char> image = read_framebuffer(screen_w, screen_h);
            int rgb[3]{};
            pixel_at(image, screen_w, screen_h, screen_w / 2, screen_h / 2, rgb);
            const int expected = static_cast<int>(std::lround(hd2d_tonemap_cpu(v) * 255.f));
            const int delta = std::abs(rgb[0] - expected);
            if (delta > worst_delta) {
                char buf[128]{};
                std::snprintf(buf, sizeof(buf), "HDR %.2f → GPU %d / CPU %d", v, rgb[0], expected);
                worst = buf;
                worst_delta = delta;
            }
        }
        std::fprintf(stderr, "  (1) トーンマップ GPU=CPU: 最大のずれ %d/255%s%s\n", worst_delta,
            worst.empty() ? "" : "  ", worst.c_str());
        if (worst_delta > 1) {
            fail("(1) トーンマップ", "GPU の hd2d_tonemap() と CPU の hd2d_tonemap_cpu() が食い違っています");
        }
    }

    /* --- 実際の絵を 1 枚だけ描く（以降は同じ FBO を掛け直すだけ） ---------- */
    Prefab wall_prefab = make_box_prefab("wall", 32, 32, 32, 0, 32);
    Prefab floor_prefab = make_box_prefab("floor", 32, 32, 2, -2, 32);
    GpuPrefab wall_gpu;
    GpuPrefab floor_gpu;
    if (!renderer.upload(wall_prefab, wall_gpu, err) || !renderer.upload(floor_prefab, floor_gpu, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    Camera camera;
    camera.pitch = options.camera_pitch_deg * 0.0174533f;
    camera.fov_x = options.camera_fov_deg * 0.0174533f;
    camera.viewport_w = screen_w;
    camera.viewport_h = screen_h;
    camera.distance = distance_for_cell_px(camera, options.camera_cell_px);
    camera.target = Vec3{ 100.5f, 44.5f, 0.f };
    camera.depth_range(z_near, z_far);
    const ViewWindow view_full = derive_view_window(camera, kMaxPropHeight, 60.f);
    /*
     * **2 つの絵で見る。**引き継ぎ §4 は「ブルームは溶岩と松明で効く」と言っているが、
     * 明るさの出どころは地下（松明 1 灯）と地上（正午の太陽）でまるで違う。
     * 片方だけで閾値を決めると、もう片方で「効かない」か「全面が光る」になる。
     */
    struct Scene {
        const char *name;
        GameFrame frame;
        FloorMeaning meaning;
        TerrainMemory memory; //!< 地形の細別（P10。合成フレームの terrain_id を吸う）
        TerrainView terrain;
        std::vector<InstanceData> emissive; //!< 光る床（P7）
        SceneLighting lighting;
        ShadowFit fit;
    };
    /*
     * プレハブライブラリ（P10）。terrain-check と同じく**この検査でも必須**。
     * ブルームの相手（溶岩の亀裂・松明の炎）がライブラリの側に移ったので、ライブラリなしで走ると
     * 「明るい画素の出どころ」が本番と別物になる。
     */
    PrefabLibrary library;
    if (!library.load(options.voxel_dir, err) || !library.upload_gpu(renderer, err)) {
        std::fprintf(stderr, "[hd2d] post-check: プレハブライブラリを用意できませんでした:\n%s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    library.update_motions(0.58f); //!< 時刻は固定（罠 14）
    std::vector<Scene> scenes(2);
    scenes[0].name = "地下＋松明";
    scenes[0].frame = make_synthetic_frame(view_full.cols, view_full.rows, 100, 44);
    scenes[1].name = "地上の町・正午";
    scenes[1].frame = make_entrance_frame(view_full.cols, view_full.rows, 100, 44);
    /*
     * 地下の南（画面の手前）と西（画面の左）を**未踏破**にする（P10 レビュー 9 追記）。
     * 実機は未踏のマスを描かないので、手前のブロックの下辺と縦辺は
     * 「描写外との境」になる。全面が地形の絵ではこの境が一度も検査を通らず、
     * 輪郭だけ鮮明に残る欠陥（実機のスクリーンショット）を見逃した。
     * 部屋（gy 44〜48）は残し、その南の壁と西隣の部屋を境の外へ出す。
     */
    {
        const auto to_unknown = [&](int gx, int gy) { return (gy >= 49) || (gx <= 92); };
        for (auto &cell : scenes[0].frame.cells) {
            if (to_unknown(cell.gx, cell.gy)) {
                cell.feature_flags = 0;
                cell.terrain_id = 0;
            }
        }
        for (int gy = 0; gy < scenes[0].frame.minimap.height; ++gy) {
            for (int gx = 0; gx < scenes[0].frame.minimap.width; ++gx) {
                if (!to_unknown(gx, gy)) {
                    continue;
                }
                scenes[0].frame.minimap.kinds[(static_cast<std::size_t>(gy)
                                                  * static_cast<std::size_t>(scenes[0].frame.minimap.width))
                    + static_cast<std::size_t>(gx)]
                    = static_cast<std::uint8_t>(MinimapKind::Unknown);
            }
        }
    }
    for (auto &scene : scenes) {
        (void)rebuild_floor_meaning(scene.frame, scene.meaning);
        scene.memory.update(scene.frame);
        build_terrain_view(scene.meaning, Frustum::from(camera.view_projection()), scene.terrain,
            &scene.memory, &library);
        scene.lighting = make_scene_lighting(scene.frame.lighting, scene.frame.floor);
        (void)collect_point_lights(scene.frame, camera.target, scene.lighting);
        collect_emissive_slabs(scene.frame, scene.emissive, /*skip_lava=*/true);
        scene.fit = fit_shadow(camera, scene.lighting.sun_dir, kMaxPropHeight, shadow.side(), 0.f);
    }
    //! いま描いている絵。検査 (2)(4)(5) は明るさに依らないので地下（0 番）で足りる。
    std::size_t scene_index = 0;
    const auto draw_scene = [&]() {
        const Scene &scene = scenes[scene_index];
        shadow.begin();
        renderer.draw_instanced_depth(floor_gpu, scene.fit.view_projection,
            scene.terrain.slabs.data(), scene.terrain.slabs.size());
        renderer.draw_instanced_depth(wall_gpu, scene.fit.view_projection,
            scene.terrain.boxes.data(), scene.terrain.boxes.size());
        draw_library_depth(renderer, library, scene.terrain, scene.fit.view_projection);
        shadow.end(screen_w, screen_h);
        post.begin_scene(0.04f, 0.05f, 0.07f);
        renderer.begin(camera.view_projection(), scene.lighting, scene.fit.view_projection,
            shadow.texture(), shadow.side());
        renderer.begin_cutaway(Cutaway{});
        renderer.set_cutaway_scale(0.f);
        renderer.draw_instanced(floor_gpu, scene.terrain.slabs.data(), scene.terrain.slabs.size());
        renderer.set_shadow_scale(0.f); //!< 抜いた底面は影を受けない（P10 レビュー 10）
        renderer.draw_instanced(floor_gpu, scene.terrain.under_slabs.data(), scene.terrain.under_slabs.size());
        renderer.set_shadow_scale(1.f);
        renderer.draw_instanced(wall_gpu, scene.terrain.boxes.data(), scene.terrain.boxes.size());
        draw_library_color(renderer, library, scene.terrain);
        renderer.set_cutaway_scale(0.f);
        if (!scene.emissive.empty()) {
            renderer.draw_instanced(floor_gpu, scene.emissive.data(), scene.emissive.size());
        }
        post.end_scene(screen_w, screen_h);
    };
    //! 焦点は視点から注視点までの**実距離**（`run()` と同じ式。ここが違うと別物を検査する）。
    const Vec3 to_target = camera.target - camera.eye();
    const float focus = std::sqrt(dot(to_target, to_target));
    const auto shot = [&](const PostFlags &flags) {
        draw_scene();
        //! 本編と同じく**キャラの位置とカメラの前方向**を基準にする（P10 レビュー 8）。
        post.resolve(flags, params, z_near, z_far, focus, screen_w, screen_h, 0, 0,
            camera.view_projection(), camera.target.x, camera.target.y, to_target.x, to_target.y);
        return read_framebuffer(screen_w, screen_h);
    };

    const std::vector<unsigned char> base = shot(all_off);
    //! ブルームを試す絵（いちばん明るい方）。(0) が決める。
    std::size_t bloom_scene = 0;

    /*
     * --- (0) 明るさの下見 ---
     * **これを出さずに閾値を決めると「効いていない」と「明るい所が無い」を取り違える。**
     * 実際 P7 で一度取り違えた（閾値 1.10 は、どちらの絵でも一度も届かない値だった）。
     * トーンマップは可逆（`x = -ln(1-v)/1.25`）なので、画面の値から元の HDR に戻せる。
     */
    {
        const auto brightest_hdr = [](const std::vector<unsigned char> &image) {
            int brightest = 0;
            for (std::size_t i = 0; i < image.size(); i += 4) {
                brightest = std::max({ brightest, static_cast<int>(image[i]), static_cast<int>(image[i + 1]),
                    static_cast<int>(image[i + 2]) });
            }
            const float v = std::min(static_cast<float>(brightest) / 255.f, 0.998f);
            return std::pair<int, float>{ brightest, -std::log(1.f - v) / 1.25f };
        };
        bool any_over_threshold = false;
        bool hazed = false;
        float best_peak = -1.f;
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            scene_index = i;
            const std::vector<unsigned char> plain = shot(all_off);
            const std::vector<unsigned char> bloomed = shot(PostFlags{ false, false, true, false, false });
            const auto [peak, hdr] = brightest_hdr(plain);
            const double ratio = static_cast<double>(count_changed_pixels(plain, bloomed, 1))
                / (static_cast<double>(screen_w) * static_cast<double>(screen_h));
            std::fprintf(stderr, "  (0) %-16s いちばん明るい画素 %3d/255 → HDR %.2f  ブルームが変えた画素 %.1f%%\n",
                scenes[i].name, peak, hdr, ratio * 100.0);
            any_over_threshold = any_over_threshold || (hdr > params.bloom_threshold);
            // **にじみが画面を覆っていないか。**膝を広げすぎると全面が白む（P7 で 1 度やった）。
            hazed = hazed || (ratio > 0.45);
            if (hdr > best_peak) {
                best_peak = hdr;
                bloom_scene = i;
            }
        }
        scene_index = 0;
        std::fprintf(stderr, "      ブルームの閾値 %.2f / 膝 %.2f / 強さ %.2f\n",
            params.bloom_threshold, params.bloom_knee, params.bloom_intensity);
        if (!any_over_threshold) {
            fail("(0) ブルームの閾値", "どちらの絵も閾値に届いていません（この設定ではブルームは一度も効きません）");
        }
        if (hazed) {
            fail("(0) ブルームの膝", "画面の半分近くがにじんでいます（膝が広すぎます。閾値 − 膝 が効き始めです）");
        }
    }

    /* --- (2) 全部切れば何度やっても同じ ------------------------------------ */
    {
        const std::vector<unsigned char> again = shot(all_off);
        const std::size_t changed = count_changed_pixels(base, again, 0);
        std::fprintf(stderr, "  (2) 全部切って 2 回: 違う画素 %zu\n", changed);
        if (changed != 0) {
            fail("(2) 再現性", "同じ入力で 2 回描いて絵が違います（パスの間で状態が漏れています）");
        }
    }

    /* --- (3)(4) 効果を 1 つずつ ------------------------------------------- */
    const double total = static_cast<double>(screen_w) * static_cast<double>(screen_h);
    struct Single {
        const char *name;
        PostFlags flags;
    };
    const Single singles[5] = {
        { "fog", PostFlags{ true, false, false, false, false } },
        { "dof", PostFlags{ false, true, false, false, false } },
        { "bloom", PostFlags{ false, false, true, false, false } },
        { "grade", PostFlags{ false, false, false, true, false } },
        { "vignette", PostFlags{ false, false, false, false, true } },
    };
    std::vector<unsigned char> grade_image;
    for (const auto &single : singles) {
        // ブルームだけは**いちばん明るい絵**で見る（暗い絵で「効かない」のは正しい振る舞い）。
        const bool is_bloom = (std::string(single.name) == "bloom");
        scene_index = is_bloom ? bloom_scene : 0;
        const std::vector<unsigned char> reference = is_bloom ? shot(all_off) : base;
        const std::vector<unsigned char> image = shot(single.flags);
        const std::size_t changed = count_changed_pixels(reference, image, 1);
        const double ratio = static_cast<double>(changed) / total;
        std::fprintf(stderr, "  (3) %-8s だけ入れる: 変わった画素 %.1f%%%s\n", single.name, ratio * 100.0,
            is_bloom ? "（いちばん明るい絵で）" : "");
        // **1% は「効いている」と言える下限。**0 だと繋ぎ忘れ、極小だと丸め誤差と区別できない。
        if (ratio < 0.01) {
            fail("(3) 効果が効いていない", std::string(single.name) + " を入れても絵がほとんど変わりません");
        }
        if (std::string(single.name) == "vignette") {
            // (4) ビネットは**四隅だけ**。中心が動いたら、掛けているのはビネットではない。
            int centre_a[3]{};
            int centre_b[3]{};
            int corner_a[3]{};
            int corner_b[3]{};
            pixel_at(reference, screen_w, screen_h, screen_w / 2, screen_h / 2, centre_a);
            pixel_at(image, screen_w, screen_h, screen_w / 2, screen_h / 2, centre_b);
            pixel_at(reference, screen_w, screen_h, 4, 4, corner_a);
            pixel_at(image, screen_w, screen_h, 4, 4, corner_b);
            const int centre_delta = std::abs(centre_a[1] - centre_b[1]);
            const int corner_delta = corner_a[1] - corner_b[1];
            std::fprintf(stderr, "      (4) 中心のずれ %d / 左上の落ち %d\n", centre_delta, corner_delta);
            if (centre_delta > 1) {
                fail("(4) ビネット", "画面の中心まで暗くなっています");
            }
            if (corner_delta <= 0) {
                fail("(4) ビネット", "四隅が暗くなっていません");
            }
        }
        if (std::string(single.name) == "grade") {
            grade_image = image;
        }
        if (std::string(single.name) == "bloom") {
            // ブルームは**足す**もの。全体の明るさが下がったら符号を間違えている。
            long long sum_a = 0;
            long long sum_b = 0;
            for (std::size_t i = 0; i < reference.size(); i += 4) {
                sum_a += reference[i] + reference[i + 1] + reference[i + 2];
                sum_b += image[i] + image[i + 1] + image[i + 2];
            }
            std::fprintf(stderr, "      明るさの合計 %lld → %lld\n", sum_a, sum_b);
            if (sum_b <= sum_a) {
                fail("(3) ブルーム", "足しているはずなのに画面が明るくなっていません");
            }
        }
    }

    /* --- (5) カラーグレーディングが CPU で引いた LUT と一致するか ---------- */
    if (!grade_image.empty()) {
        std::vector<unsigned char> lut;
        bake_lut(GradeParams{}, kLutSize, lut);
        int worst = 0;
        std::string worst_where;
        // 画面の 7x7 の格子で見る（1 点だと、たまたま合う色を引く）。
        for (int j = 1; j < 8; ++j) {
            for (int i = 1; i < 8; ++i) {
                const int x = (screen_w * i) / 9;
                const int y = (screen_h * j) / 9;
                int before[3]{};
                int after[3]{};
                pixel_at(base, screen_w, screen_h, x, y, before);
                pixel_at(grade_image, screen_w, screen_h, x, y, after);
                const float in[3] = { static_cast<float>(before[0]) / 255.f,
                    static_cast<float>(before[1]) / 255.f, static_cast<float>(before[2]) / 255.f };
                float expected[3]{};
                sample_lut_cpu(lut, kLutSize, in, expected);
                for (int c = 0; c < 3; ++c) {
                    const int delta = std::abs(after[c] - static_cast<int>(std::lround(expected[c] * 255.f)));
                    if (delta > worst) {
                        worst = delta;
                        char buf[160]{};
                        std::snprintf(buf, sizeof(buf), "(%d,%d) 成分%d: GPU %d / CPU %d（入力 %d）",
                            x, y, c, after[c], static_cast<int>(std::lround(expected[c] * 255.f)), before[c]);
                        worst_where = buf;
                    }
                }
            }
        }
        std::fprintf(stderr, "  (5) LUT GPU=CPU: 最大のずれ %d/255  %s\n", worst, worst_where.c_str());
        /*
         * 3/255 まで許す。**GPU は float で LUT を引き、CPU は 8bit へ落とした値から引く**ので、
         * 入力の量子化ぶん（0.5/255 × LUT の傾き）と 3D テクスチャの補間の丸めが乗る。
         */
        if (worst > 3) {
            fail("(5) LUT", "GPU の 3D テクスチャと CPU で焼いた表が食い違っています（軸の並びを疑うこと）");
        }
    }

    /* --- (6) コスト ------------------------------------------------------- */
    {
        /*
         * **`glFinish` で挟んで測る。**付けないと `SDL_GL_SwapWindow` に待ちが吸われ、
         * 「速い」ではなく「見えていない」を読むことになる（設計書 §14-9。P5 で 1 度踏んだ）。
         * ここは検査なので常に同期する（実プレイでは `HD2D_GPU_SYNC=1` が要る）。
         */
        const Uint64 freq = SDL_GetPerformanceFrequency();
        const auto time_resolve = [&](const PostFlags &flags) {
            draw_scene();
            glFinish();
            constexpr int kRepeat = 30;
            const Uint64 start = SDL_GetPerformanceCounter();
            for (int i = 0; i < kRepeat; ++i) {
                post.resolve(flags, params, z_near, z_far, focus, screen_w, screen_h);
            }
            glFinish();
            return (static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0
                       / static_cast<double>(freq))
                / kRepeat;
        };
        scene_index = 0;
        const double ms_off = time_resolve(all_off);
        const double ms_all = time_resolve(PostFlags{});
        std::fprintf(stderr, "  (6) 合成のコスト %dx%d: 全部切って %.3fms / 全部入れて %.3fms"
                             "（差 %.3fms）\n",
            screen_w, screen_h, ms_off, ms_all, ms_all - ms_off);
    }

    if (!options.shot_path.empty()) {
        /*
         * **`--post=` で言われたとおりに撮る。**ここを全部入りに決め打ちしていたせいで、
         * 効果を 1 つずつ切って撮った 12 枚が全部同じ絵になった（P7 で実際にやった）。
         * 比べるための撮影が比べられないのは、撮っていないのと同じである（§14-14 と同じ罠）。
         */
        /*
         * **`--dof=` に従って撮る**（P10 レビュー 5）。検査そのものは既定の強さで通し、
         * 撮るときだけ言われた強さにする。強さ違いを並べられないと、
         * 「TiltShift 風に見えるか」を人が判断できない（§14「並べる手段が無い変更は承認できない」）。
         */
        params.dof_strength = options.dof_strength;
        scene_index = 0;
        (void)shot(resolve_post_flags(options));
        (void)save_framebuffer_bmp(options.shot_path, screen_w, screen_h);
        /*
         * **地上の絵も撮る**（P10 レビュー 3）。この検査は 2 つの絵を持っているのに
         * 撮れるのは地下だけで、街路や草地の見えを確かめる手段がどこにも無かった。
         * 名前は `<shot>` の拡張子の前に `_surface` を挟む。
         */
        const std::size_t dot = options.shot_path.find_last_of('.');
        const std::string surface_path = (dot == std::string::npos)
            ? (options.shot_path + "_surface.bmp")
            : (options.shot_path.substr(0, dot) + "_surface" + options.shot_path.substr(dot));
        scene_index = 1;
        (void)shot(resolve_post_flags(options));
        (void)save_framebuffer_bmp(surface_path, screen_w, screen_h);
    }

    post.shutdown();
    shadow.shutdown();
    library.release_gpu(renderer);
    renderer.release(wall_gpu);
    renderer.release(floor_gpu);
    renderer.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

/*!
 * @brief `--cutaway-check`: 遮蔽建物のフェードが**本当に入口を見せるか**を画素で確かめる。
 *
 * @details プレイヤのマスに**赤い目印の箱**を立て、その画素を数える。
 * 赤いのは、壁がほぼ無彩色なので `r > 1.6·g かつ r > 1.6·b` で確実に分けられるからである
 * （光の当たり方が変わっても、色相までは変わらない）。
 *
 * | # | 何を | |
 * |---|---|---|
 * | 1 | 建物の高さ 1 マス（いまの仮の地形） | **参考値。**この高さでは遮蔽が弱い |
 * | 2 | 建物の高さ 2.5 マス（P10 で置く実物の高さ） | **こちらが本番。**切れば見えず、入れれば見える |
 * | 3 | 抜いた穴から**背景**が見えていないか | 床の板を抜くと地面に穴が開く |
 *
 * **検査の検査**は `--shrink-cutaway=N`（半径を N 画素狭める）。十分に狭めれば
 * 見えなくなるはずで、見えたままなら「見えていないことを検出できない検査」である。
 */
int run_cutaway_check(const AppOptions &options)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[hd2d] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    Window window;
    std::string err;
    if (!create_window(options, window, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    VoxelRenderer renderer;
    ShadowMap shadow;
    PostChain post;
    int screen_w = options.window_w;
    int screen_h = options.window_h;
    SDL_GetWindowSize(window.window, &screen_w, &screen_h);
    if (!renderer.init(err) || !shadow.init(kShadowMapSide, err) || !init_post_chain(post, options, err)
        || !post.resize(screen_w, screen_h, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }

    Prefab wall_prefab = make_box_prefab("wall", 32, 32, 32, 0, 32);
    Prefab floor_prefab = make_box_prefab("floor", 32, 32, 2, -2, 32);
    //! 入口に立つ目印。**プレイヤの代わり**（合成フレームには実体が居ない）。
    Prefab marker_prefab = make_box_prefab("marker", 14, 14, 29, 0, 32);
    GpuPrefab wall_gpu;
    GpuPrefab floor_gpu;
    GpuPrefab marker_gpu;
    if (!renderer.upload(wall_prefab, wall_gpu, err) || !renderer.upload(floor_prefab, floor_gpu, err)
        || !renderer.upload(marker_prefab, marker_gpu, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }

    Camera camera;
    camera.pitch = options.camera_pitch_deg * 0.0174533f;
    camera.fov_x = options.camera_fov_deg * 0.0174533f;
    camera.viewport_w = screen_w;
    camera.viewport_h = screen_h;
    camera.distance = distance_for_cell_px(camera, options.camera_cell_px);
    camera.target = Vec3{ 100.5f, 44.5f, 0.f };
    float z_near = 0.f;
    float z_far = 0.f;
    camera.depth_range(z_near, z_far);
    const ViewWindow view_full = derive_view_window(camera, kMaxPropHeight, 60.f);
    GameFrame frame = make_entrance_frame(view_full.cols, view_full.rows, 100, 44);
    FloorMeaning meaning;
    (void)rebuild_floor_meaning(frame, meaning);
    TerrainView terrain;
    build_terrain_view(meaning, Frustum::from(camera.view_projection()), terrain);
    SceneLighting lighting = make_scene_lighting(frame.lighting, frame.floor);
    (void)collect_point_lights(frame, camera.target, lighting);

    std::vector<InstanceData> marker(1);
    marker[0].x = camera.target.x - (14.f / 64.f);
    marker[0].y = camera.target.y - (14.f / 64.f);
    marker[0].z = 0.f;
    marker[0].r = 1.20f;
    marker[0].g = 0.10f;
    marker[0].b = 0.10f;

    const unsigned char scene_clear[3] = { 10, 13, 18 };
    unsigned char clear[3]{};
    for (int c = 0; c < 3; ++c) {
        clear[c] = static_cast<unsigned char>(
            std::lround(hd2d_tonemap_cpu(static_cast<float>(scene_clear[c]) / 255.f) * 255.f));
    }
    const PostFlags check_post{ false, false, false, false, false }; //!< 色を見るので掛けない

    /*!
     * 1 枚描いて「赤い目印の画素」と「抜いた穴から見えた背景の画素」を数える。
     * @param height 建物の高さ（マス）。1 = いまの仮の地形 / 2.5 = P10 で置く実物。
     * @param radius カットアウェイの半径（画素）。0 で無効。
     */
    //! 最後に測った絵（(4) が「抜いた底面」の画素を読むのに使う）。
    std::vector<unsigned char> last_image;
    const auto measure_cuts = [&](float height, const std::vector<Cutaway> &cuts, const Cutaway &disc,
                                  const PostFlags &flags, std::size_t &red_out, std::size_t &hole_out) {
        std::vector<InstanceData> boxes = terrain.boxes;
        for (auto &box : boxes) {
            box.sz = height;
        }
        const float prop_height = std::max(1.f, height);
        const ShadowFit fit = fit_shadow(camera, lighting.sun_dir, prop_height, shadow.side(), 0.f);
        shadow.begin();
        renderer.draw_instanced_depth(floor_gpu, fit.view_projection, terrain.slabs.data(), terrain.slabs.size());
        renderer.draw_instanced_depth(wall_gpu, fit.view_projection, boxes.data(), boxes.size());
        renderer.draw_instanced_depth(marker_gpu, fit.view_projection, marker.data(), marker.size());
        shadow.end(screen_w, screen_h);

        post.begin_scene(static_cast<float>(scene_clear[0]) / 255.f, static_cast<float>(scene_clear[1]) / 255.f,
            static_cast<float>(scene_clear[2]) / 255.f);
        renderer.begin(camera.view_projection(), lighting, fit.view_projection, shadow.texture(), shadow.side());
        renderer.begin_cutaways(cuts.data(), cuts.size());
        renderer.set_cutaway_scale(0.f); //!< 床の板は抜かない
        renderer.draw_instanced(floor_gpu, terrain.slabs.data(), terrain.slabs.size());
        /*
         * 壁の下の地面（P7）。**これが無いと抜いた所から背景が見える**（この検査の (3)）。
         * **影は受けない**（P10 レビュー 10。この検査の (4) が読めることを見る）。
         */
        renderer.set_shadow_scale(0.f);
        renderer.draw_instanced(floor_gpu, terrain.under_slabs.data(), terrain.under_slabs.size());
        renderer.set_shadow_scale(1.f);
        renderer.set_cutaway_scale(1.f); //!< 建物は抜く
        renderer.draw_instanced(wall_gpu, boxes.data(), boxes.size());
        renderer.set_cutaway_scale(0.f); //!< **見せたい当人は抜かない**（実機のビルボードも掛からない）
        renderer.draw_instanced(marker_gpu, marker.data(), marker.size());
        post.end_scene(screen_w, screen_h);
        const Vec3 to_target = camera.target - camera.eye();
        post.resolve(flags, PostParams{}, z_near, z_far, std::sqrt(dot(to_target, to_target)),
            screen_w, screen_h);

        const std::vector<unsigned char> image = read_framebuffer(screen_w, screen_h);
        last_image = image; //!< (4) が後から読む
        red_out = 0;
        hole_out = 0;
        for (int y = 0; y < screen_h; ++y) {
            for (int x = 0; x < screen_w; ++x) {
                const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(screen_w))
                                              + static_cast<std::size_t>(x))
                    * 4;
                const int r = image[index];
                const int g = image[index + 1];
                const int b = image[index + 2];
                if ((r > 40) && (static_cast<float>(r) > (static_cast<float>(g) * 1.6f))
                    && (static_cast<float>(r) > (static_cast<float>(b) * 1.6f))) {
                    ++red_out;
                }
                const float dx = static_cast<float>(x) - disc.centre_x;
                const float dy = static_cast<float>(y) - disc.centre_y;
                if (((dx * dx) + (dy * dy)) > (disc.radius * disc.radius)) {
                    continue;
                }
                if ((std::abs(r - static_cast<int>(clear[0])) <= 1) && (std::abs(g - static_cast<int>(clear[1])) <= 1)
                    && (std::abs(b - static_cast<int>(clear[2])) <= 1)) {
                    ++hole_out;
                }
            }
        }
        SDL_GL_SwapWindow(window.window);
    };

    /*!
     * 従来の呼び方（プレイヤ 1 人ぶんの穴）。**穴の並びを渡す形へ変えた**（P10 第 4 期で
     * 抜く所が複数になったため）ので、ここで 1 個の並びに包んでいる。
     */
    const auto measure = [&](float height, float radius, const PostFlags &flags,
                             std::size_t &red_out, std::size_t &hole_out) {
        std::vector<Cutaway> cuts;
        if (radius > 0.f) {
            cuts.push_back(make_player_cutaway(camera, frame, radius));
        }
        measure_cuts(height, cuts, make_player_cutaway(camera, frame, std::max(radius, 1.f)), flags,
            red_out, hole_out);
    };

    const float radius = std::max(0.f, options.cutaway_radius - static_cast<float>(options.shrink_cutaway));
    int failures = 0;
    const auto fail = [&failures](const char *what, const std::string &detail) {
        std::fprintf(stderr, "  **FAIL** %s: %s\n", what, detail.c_str());
        ++failures;
    };

    std::fprintf(stderr, "[hd2d] cutaway-check viewport=%dx%d pitch=%.0f cell=%.0fpx  半径=%.0fpx (shrink=%d)\n",
        screen_w, screen_h, camera.pitch * 57.2958f, options.camera_cell_px, radius, options.shrink_cutaway);
    std::fprintf(stderr, "  建物 %zu マス（プレイヤの南 1〜5 マス・幅 13）  街路 %zu マス\n",
        terrain.boxes.size(), terrain.slabs.size());

    std::size_t low_off = 0;
    std::size_t low_on = 0;
    std::size_t ignored = 0;
    measure(1.f, 0.f, check_post, low_off, ignored);
    measure(1.f, radius, check_post, low_on, ignored);
    /*
     * **参考値。**いま世界に置いている壁は 1 マスの高さしかなく、設計書 §4.4 の
     * 遮蔽長 `f·H/(h−H)` は 12.9×1/13.4 ＝ 約 0.96 マスにしかならない。
     * つまり入口のマスの**足元だけ**が隠れ、上半分は見えている。
     * 遮蔽が本当に問題になるのは、建物が実物の高さを持つ P10 からである。
     */
    std::fprintf(stderr, "  (1) 建物 1.0 マス（いまの仮の地形・参考）: 目印の画素 切=%zu → 入=%zu\n",
        low_off, low_on);

    std::size_t tall_off = 0;
    std::size_t tall_on = 0;
    std::size_t hole_off = 0;
    std::size_t hole_on = 0;
    measure(2.5f, 0.f, check_post, tall_off, hole_off);
    measure(2.5f, radius, check_post, tall_on, hole_on);
    std::fprintf(stderr, "  (2) 建物 2.5 マス（P10 で置く高さ・本番）: 目印の画素 切=%zu → 入=%zu\n",
        tall_off, tall_on);
    if (tall_off > 60) {
        fail("(2) 前提", "カットアウェイを切っても目印が見えています（この配置では遮蔽が起きていません）");
    }
    if (tall_on < 300) {
        fail("(2) カットアウェイ", "入れても入口のプレイヤが見えません");
    }
    if (tall_on < (tall_off * 4)) {
        fail("(2) カットアウェイ", "入れても見える量がほとんど増えていません");
    }

    const double hole_ratio = (radius > 0.f)
        ? (static_cast<double>(hole_on) / (3.14159265 * static_cast<double>(radius) * static_cast<double>(radius)))
        : 0.0;
    std::fprintf(stderr, "  (3) 抜いた穴から見えた背景: 切=%zu → 入=%zu 画素（円の %.2f%%）\n",
        hole_off, hole_on, hole_ratio * 100.0);
    /* --- (4) 抜いた底面が読めるか（P10 レビュー 10） ----------------------- */
    {
        /*
         * **抜いた所に「何があったか」の色が出ていること。**底面（`under_slabs`）は
         * 壁の真下にあるので、壁自身の影を受けると必ず真っ黒になる。ふだんは壁に隠れて
         * いて誰も困らないが、抜いた瞬間だけ症状が出る——という見つけにくい形だった
         * （2026-08-09 に気づいた:「手前の面の影が黒く残っていて底面にかぶり判別しづらい」）。
         *
         * 狙うのは**プレイヤの 1 マス南**（＝プレイヤを隠している壁）の底面。位置は
         * 決め打ちの画素ではなく**投影して求める**（カメラの既定値が変わっても追随する）。
         * 直前の `measure(2.5, radius, check_post)` の絵をそのまま読む。
         */
        float sx = 0.f;
        float sy = 0.f;
        const bool in_front = camera.project(Vec3{ camera.target.x, camera.target.y + 1.f, 0.02f }, sx, sy);
        //! `Camera::project` は左上原点・`read_framebuffer` は左下原点（罠 24 と同じ換算）。
        const int cx = static_cast<int>(std::lround(sx));
        const int cy = static_cast<int>(std::lround(static_cast<float>(screen_h) - sy));
        long long sum = 0;
        int samples = 0;
        for (int dy = -6; dy <= 6; ++dy) {
            for (int dx = -12; dx <= 12; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if ((x < 0) || (y < 0) || (x >= screen_w) || (y >= screen_h)) {
                    continue;
                }
                const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(screen_w))
                                              + static_cast<std::size_t>(x))
                    * 4;
                sum += std::max(last_image[index], std::max(last_image[index + 1], last_image[index + 2]));
                ++samples;
            }
        }
        const int mean = (samples > 0) ? static_cast<int>(sum / samples) : 0;
        std::fprintf(stderr, "  (4) 抜いた底面の明るさ（1 マス南・画素 %d,%d の周り）: %d/255\n", cx, cy, mean);
        if (!in_front || (samples == 0)) {
            fail("(4) 底面", "1 マス南のマスが画面に出ていません（この検査の前提が崩れています）");
        } else if (mean < 70) {
            /*
             * **70 は「影で沈んでいない」の下限。**影を受けたままだと 45 前後、
             * 受けなければ 130 前後になる（実測）。真ん中に取ってある。
             */
            fail("(4) 底面", "抜いた底面が暗く沈んでいます（壁自身の影を受けていないか確かめること）");
        }
    }

    /*
     * (5) **壁に隠れた実体のぶんも抜けるか**（P10 第 4 期。2026-08-10 に気づいた:
     * 「視界の範囲内にあるモンスター、アイテムが壁ブロック等の 1 マス北にある場合
     * （隠れて見えない場合）は透過して見えるように」）。
     *
     * (2) はプレイヤ 1 人ぶんの穴しか通していないので、**この項目が無いと
     * 「実体のぶんは 1 度も抜いていない」検査**になる（罠 64 と同じ形）。
     * 目印を 2 つ置き、`append_entity_cutaways()` に穴を作らせて、両方が見えることを見る。
     * **穴が 2 つ**なので、シェーダの配列と `begin_cutaways()` もここで初めて通る。
     */
    {
        const std::vector<InstanceData> saved = marker;
        marker.push_back(marker[0]);
        marker[1].x += 3.f; //!< 同じ建物の裏の、3 マス東
        //! 目印の位置に実体（板）があることにする。高さ 1 マス・幅 1 マス。
        EntityView probe;
        for (const InstanceData &at : marker) {
            SlabInstance b{};
            b.cx = at.x + (14.f / 64.f);
            b.cy = at.y + (14.f / 64.f);
            b.inst.z = 0.f;
            b.height = 1.f;
            probe.slabs.push_back(b);
        }
        /*
         * **遮蔽の高さを、描く高さに合わせる。**この検査は描くときだけ壁を 2.5 マスへ
         * 上げていて（`measure_cuts` の `box.sz = height`）、`TerrainView` に記録された
         * 高さは仮の地形のままの 1.0 である。そのままだと `line_of_sight_blocked()` が
         * 「見えている」と答えて穴が 1 つもできない（最初にそう落ちた）。
         * **絵と判定の前提を揃えるのは検査の側の仕事。**
         */
        TerrainView tall = terrain;
        for (float &top : tall.occluder_z) {
            if (top > 0.f) {
                top = 2.5f;
            }
        }
        std::vector<Cutaway> cuts;
        append_entity_cutaways(camera, tall, probe, radius * 0.42f, cuts);
        std::size_t hidden_off = 0;
        std::size_t hidden_on = 0;
        std::size_t skip = 0;
        const Cutaway disc = make_player_cutaway(camera, frame, std::max(radius, 1.f));
        measure_cuts(2.5f, {}, disc, check_post, hidden_off, skip);
        measure_cuts(2.5f, cuts, disc, check_post, hidden_on, skip);
        std::fprintf(stderr, "  (5) 壁の裏の実体 2 体: 穴 %zu 個 / 目印の画素 切=%zu → 入=%zu\n",
            cuts.size(), hidden_off, hidden_on);
        if (cuts.size() != 2) {
            fail("(5) 実体の穴", "隠れている 2 体に対して穴が 2 個できていません（遮蔽の判定を確かめること）");
        } else if (hidden_off != 0) {
            fail("(5) 実体の穴", "抜く前から見えています（この検査の前提が崩れています）");
        } else if (hidden_on == 0) {
            fail("(5) 実体の穴", "抜いても実体が 1 画素も見えません");
        }
        marker = saved;
    }

    /*
     * (6) **半平面が 4 方位で回るか**（実機で見つけた（2026-08-19）:「通常以外の方向の場合の
     * 透過の判定がされなかった。おそらく南側のみ透過というのが効いてしまっている」）。
     *
     * ここは**画素を数えない**。見たいのは `Cutaway` の半平面の式そのもので、
     * シェーダの `dot(v_world.xy, face.xy) > face.z` を CPU で当てれば足りる
     * ——窓も GL も要らないぶん、落ちたときに原因が 1 か所に絞れる。
     *
     * 見るのは 3 通り。**「真横」を入れてあるのが要点**で、初版の「南だけ」は
     * これを通すために足された条件だった（2026-08-11 に決めた「真横のオブジェクトを
     * 透過しない」）。回転に対応させるついでにこれを壊すと、退行に気づけない。
     */
    {
        //! シェーダの 1 行と同じ式。**ここを他の書き方にしたら検査の意味が無くなる。**
        const auto dissolved = [](const Cutaway &cut, float wx, float wy) {
            return ((wx * cut.face_x) + (wy * cut.face_y)) > cut.face_min;
        };
        for (int turn = 0; turn < kViewTurnCount; ++turn) {
            Camera turned = camera;
            turned.yaw = view_turn_yaw(turn);
            const Cutaway cut = make_player_cutaway(turned, frame, std::max(radius, 1.f), true);
            if (cut.radius <= 0.f) {
                fail("(6) 半平面", "穴が作れませんでした（プレイヤがカメラの後ろに居ます）");
                continue;
            }
            const float cx = static_cast<float>(frame.player_gx) + 0.5f;
            const float cy = static_cast<float>(frame.player_gy) + 0.5f;
            /*
             * 「カメラへ向かう向き」は**カメラの幾何から独立に**出す（`eye()` と対象の差）。
             *
             * **`cut.face_*` から取ってはいけない。**最初にそう書いて、
             * 「常に南」へわざと戻しても検査が通ってしまった——対象から出した値で
             * 対象を検査すれば、何を入れても自己一致するのは当然である。
             * ここは `Camera::eye()`（`yaw` から組まれる、カットアウェイとは別の道）を使う。
             *
             * 真横はそれを水平面内で 90° 回したもの。**世界の東西南北で書かないこと**
             * ——それをやったのが今回直した不具合である。
             */
            const Vec3 eye_at = turned.eye();
            float to_cam_x = eye_at.x - cx;
            float to_cam_y = eye_at.y - cy;
            const float to_cam_len = std::sqrt((to_cam_x * to_cam_x) + (to_cam_y * to_cam_y));
            if (to_cam_len < 1e-4f) {
                fail("(6) 半平面", "カメラが対象の真上に居ます（この検査の前提が崩れています）");
                continue;
            }
            to_cam_x /= to_cam_len;
            to_cam_y /= to_cam_len;
            const float side_x = -to_cam_y;
            const float side_y = to_cam_x;

            const bool near_ok = dissolved(cut, cx + to_cam_x, cy + to_cam_y);
            const bool far_ok = !dissolved(cut, cx - to_cam_x, cy - to_cam_y);
            const bool side_ok = !dissolved(cut, cx + side_x, cy + side_y);
            const bool side2_ok = !dissolved(cut, cx - side_x, cy - side_y);
            std::fprintf(stderr,
                "  (6) 半平面 turn=%d 法線(%+.2f,%+.2f): カメラ側 %s / 奥 %s / 真横 %s %s\n", turn,
                static_cast<double>(cut.face_x), static_cast<double>(cut.face_y), near_ok ? "抜く" : "**抜かない**",
                far_ok ? "残す" : "**抜けた**", side_ok ? "残す" : "**抜けた**", side2_ok ? "残す" : "**抜けた**");
            if (!near_ok) {
                fail("(6) 半平面", "カメラと対象の間の 1 マスが抜けません（回した先で透過しない不具合）");
            }
            if (!far_ok) {
                fail("(6) 半平面", "対象より奥の 1 マスまで抜けています");
            }
            if (!side_ok || !side2_ok) {
                fail("(6) 半平面", "真横の 1 マスが抜けています（2026-08-11 の指示に反します）");
            }
        }
        /*
         * **半平面を掛けない穴は素通しであること**（既定 −1e9 の意味）。
         * ここが崩れると屋根外し（層 2）と検査 (1)〜(5) が黙って別のものになる。
         */
        const Cutaway plain = make_player_cutaway(camera, frame, std::max(radius, 1.f), false);
        const bool plain_ok = dissolved(plain, 0.f, 0.f) && dissolved(plain, 1e6f, -1e6f);
        std::fprintf(stderr, "  (6) 半平面を掛けない穴: %s\n", plain_ok ? "素通し OK" : "**素通しになっていません**");
        if (!plain_ok) {
            fail("(6) 半平面", "掛けない指定の穴で半平面が効いています（既定値が壊れています）");
        }
    }

    if (!options.shot_path.empty()) {
        /*
         * **撮るときは `--post=` に従う。**数えるときは色で目印を分けるので効果を切るが、
         * 人に見せる絵はふだんの見え方でなければ意味が無い。
         */
        std::size_t ignore_a = 0;
        std::size_t ignore_b = 0;
        measure(2.5f, radius, resolve_post_flags(options), ignore_a, ignore_b);
        (void)save_framebuffer_bmp(options.shot_path, screen_w, screen_h);
    }

    post.shutdown();
    shadow.shutdown();
    renderer.release(wall_gpu);
    renderer.release(floor_gpu);
    renderer.release(marker_gpu);
    renderer.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

} // namespace hd2d
