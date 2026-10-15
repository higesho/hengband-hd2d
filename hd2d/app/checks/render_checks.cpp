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
#include "render/model_renderer.h"
#include "model/model_asset.h"
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
     * **区域**（`HD2D_CHECK_ZONE=<意匠の id>`。2026-10-03）。Umoria の部屋の種類（`FloorZone`）を
     * 合成フレームで検分するため、自機の周り（横 ±12・縦 ±8）に 1 つ置く。実機では灯りの外が
     * 真っ黒で、部屋の種類の壁を撮れなかった。未設定なら何も置かない（従来どおり）。
     */
    if (const char *const zone_env = std::getenv("HD2D_CHECK_ZONE"); (zone_env != nullptr) && (zone_env[0] != '\0')) {
        const int style = std::atoi(zone_env);
        if (style > 0) {
            frame.floor.zones.push_back(FloorZone{ style, frame.player_gx - 12, frame.player_gy - 8,
                frame.player_gx + 12, frame.player_gy + 8 });
        }
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
        const Vec3 slab_n = screen_aligned_slab_normal(turned.yaw, turned.pitch);
        const Vec3 screen_n = turned.forward() * -1.f;
        const Vec3 slab_diff = slab_n - screen_n;
        const bool slab_ok = std::sqrt(dot(slab_diff, slab_diff)) < 1e-5f;
        turn_checks_ok = turn_checks_ok && rt_ok && or_ok && slab_ok;
        std::fprintf(stderr, "  視点回転 turn=%d: 窓 %d×%d / 往復 %s（%s）/ 向き %s（%s）/ 板正対 %s\n", turn,
            turned_window.cols, turned_window.rows, rt_ok ? "OK" : "**NG**", turn_round_trip.c_str(),
            or_ok ? "OK" : "**NG**", turn_orientation.c_str(), slab_ok ? "OK" : "**NG**");
    }
    for (const float pitch : { 0.f, 0.25f, 0.8f, 1.25f }) {
        Camera tilted = camera;
        tilted.pitch = pitch;
        const Vec3 slab_n = screen_aligned_slab_normal(tilted.yaw, tilted.pitch);
        const Vec3 screen_n = tilted.forward() * -1.f;
        const Vec3 slab_diff = slab_n - screen_n;
        const bool slab_ok = std::sqrt(dot(slab_diff, slab_diff)) < 1e-5f;
        turn_checks_ok = turn_checks_ok && slab_ok;
        std::fprintf(stderr, "  板正対 pitch=%.2f: %s\n", static_cast<double>(pitch), slab_ok ? "OK" : "**NG**");
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
            /*
             * (7-c) **照準の輪が床の上に見えること**（`target_gx/gy`）。
             *
             * 輪の列（`EntityView::rings`）は CPU の話で、「列はできているのに床の下に潜って 1 画素も
             * 出ない」を素通りさせる（高さ `kRingLift` が床の上面より下だと深度で負ける）。
             * だから**ライブラリの床を描いた上へ**輪を描き、輪の有る無しで画素が変わることを見る。
             * 本描画と同じ順（地面 → 壁 → ライブラリの床 → 輪）で、輪は深度を読んで書かない。
             */
            {
                GameFrame aimed = probe;
                aimed.target_gx = 102;
                aimed.target_gy = 44;
                EntityView ring_view;
                tracker.build(aimed, no_catalog, no_slabs, renderer, 0.f, ring_view, false, 0.f, SlabFacing{}, &glyphs);
                std::fprintf(stderr, "  (7-c) 照準の輪: 照準なし %zu 個 / 照準あり %zu 個\n",
                    ascii_view.rings.size(), ring_view.rings.size());
                if (!ascii_view.rings.empty()) {
                    glyph_fail("照準を立てていないのに輪が作られています");
                }
                const bool ring_listed = (ring_view.rings.size() == 1)
                    && (std::fabs(ring_view.rings[0].x - 102.5f) < 0.01f)
                    && (std::fabs(ring_view.rings[0].y - 44.5f) < 0.01f);
                if (!ring_listed) {
                    glyph_fail("照準のマスの中心に輪が 1 個作られていません");
                } else {
                    GroundRingRenderer ring_renderer;
                    std::string ring_err;
                    if (!ring_renderer.init(ring_err)) {
                        glyph_fail("足元のリングを用意できません: " + ring_err);
                    } else {
                        const auto shoot_ring = [&](const EntityView &view) {
                            glBindFramebuffer(GL_FRAMEBUFFER, 0);
                            glViewport(0, 0, screen_w, screen_h);
                            glClearColor(static_cast<float>(scene_clear[0]) / 255.f,
                                static_cast<float>(scene_clear[1]) / 255.f,
                                static_cast<float>(scene_clear[2]) / 255.f, 1.f);
                            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                            renderer.begin(camera.view_projection(), lighting, fit.view_projection, shadow.texture(),
                                shadow.side());
                            renderer.begin_cutaway(Cutaway{});
                            renderer.draw_instanced(floor_gpu, terrain.slabs.data(), terrain.slabs.size());
                            renderer.set_shadow_scale(0.f);
                            renderer.draw_instanced(floor_gpu, terrain.under_slabs.data(), terrain.under_slabs.size());
                            renderer.set_shadow_scale(1.f);
                            renderer.draw_instanced(wall_gpu, terrain.boxes.data(), terrain.boxes.size());
                            draw_library_color(renderer, library, terrain);
                            if (!view.rings.empty()) {
                                ring_renderer.draw(camera.view_projection(), view.rings.data(), view.rings.size());
                            }
                            return read_framebuffer(screen_w, screen_h);
                        };
                        const std::vector<unsigned char> floor_only = shoot_ring(ascii_view);
                        const std::vector<unsigned char> floor_ring = shoot_ring(ring_view);
                        const std::size_t ring_px = count_changed_pixels(floor_only, floor_ring, 0);
                        std::fprintf(stderr, "  (7-c) 照準の輪の画素（床の上）: %zu\n", ring_px);
                        if (ring_px < 100) {
                            glyph_fail("照準の輪が床の上に出ていません（変わった画素 " + std::to_string(ring_px)
                                + "。輪の高さが床の上面より下ではありませんか）");
                        }
                        ring_renderer.shutdown();
                    }
                }
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

/*
 * ============================================================================
 * `--light-range-check=出力の頭`: 光の届く範囲
 * ============================================================================
 */
namespace {

//! 検査用の地図の長方形（壁だけの地図へ彫る床）。
struct LrRect {
    int x0;
    int y0;
    int x1;
    int y1;
    bool bright; //!< 明るい部屋（`CAVE_ROOM` + `CAVE_GLOW` 相当）か
};

/*!
 * @brief 合成フレームを「壁だけ」にして、長方形を彫る。
 * @details `make_synthetic_frame()` の部屋・通路・溶岩の池は、この検査の絵に余計な光と明るさを持ち込む。
 * 暗い通路と明るい部屋を**思ったとおりの形で**置くため、いったん全部を壁にして彫り直す。
 */
void lr_carve(GameFrame &frame, const std::vector<LrRect> &rects)
{
    const auto carved = [&rects](int gx, int gy) -> const LrRect * {
        for (const LrRect &r : rects) {
            if ((gx >= r.x0) && (gx <= r.x1) && (gy >= r.y0) && (gy <= r.y1)) {
                return &r;
            }
        }
        return nullptr;
    };
    for (MapCellView &cell : frame.cells) {
        const LrRect *const rect = carved(cell.gx, cell.gy);
        if (rect == nullptr) {
            cell.feature_flags = CELL_FEAT_KNOWN | CELL_FEAT_WALL;
            cell.terrain_id = 56; // GRANITE
        } else {
            cell.feature_flags = CELL_FEAT_KNOWN | (rect->bright ? (CELL_FEAT_ROOM | CELL_FEAT_GLOWING) : 0u);
            cell.terrain_id = 1; // FLOOR
        }
    }
    const int w = frame.minimap.width;
    for (int gy = 0; gy < frame.minimap.height; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            const bool floor = (carved(gx, gy) != nullptr);
            frame.minimap.kinds[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(gx)]
                = static_cast<std::uint8_t>(floor ? MinimapKind::Floor : MinimapKind::Wall);
        }
    }
    frame.minimap.kinds[(static_cast<std::size_t>(frame.player_gy) * static_cast<std::size_t>(w))
        + static_cast<std::size_t>(frame.player_gx)]
        = static_cast<std::uint8_t>(MinimapKind::Player);
}

//! 述語が真のマスを「プレイヤの光源で照らされている」ことにする（`player_lit` を作る）。
template <typename Pred>
void lr_set_lit(GameFrame &frame, Pred &&lit)
{
    frame.lighting.player_lit.assign((frame.cells.size() + 7u) / 8u, 0u);
    for (std::size_t i = 0; i < frame.cells.size(); ++i) {
        if (lit(static_cast<int>(frame.cells[i].gx), static_cast<int>(frame.cells[i].gy))) {
            frame.lighting.player_lit[i >> 3] = static_cast<std::uint8_t>(frame.lighting.player_lit[i >> 3] | (1u << (i & 7u)));
        }
    }
}

//! 真四角の柱（3D モデルの代役。幅 0.44・高さ 0.8）。巻き方は外向きが「右手系の反時計回り」。
ModelAsset lr_make_pillar()
{
    ModelAsset asset;
    ModelPrimitive prim;
    const float hx = 0.22f;
    const float hy = 0.22f;
    const float h = 0.8f;
    const auto quad = [&prim](const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, const Vec3 &n) {
        const Vec3 corners[6] = { a, b, c, a, c, d };
        for (const Vec3 &p : corners) {
            ModelVertex v;
            v.position = p;
            v.normal = n;
            prim.vertices.push_back(v);
        }
    };
    quad(Vec3{ hx, -hy, 0.f }, Vec3{ hx, hy, 0.f }, Vec3{ hx, hy, h }, Vec3{ hx, -hy, h }, Vec3{ 1.f, 0.f, 0.f });
    quad(Vec3{ -hx, hy, 0.f }, Vec3{ -hx, -hy, 0.f }, Vec3{ -hx, -hy, h }, Vec3{ -hx, hy, h }, Vec3{ -1.f, 0.f, 0.f });
    quad(Vec3{ hx, hy, 0.f }, Vec3{ -hx, hy, 0.f }, Vec3{ -hx, hy, h }, Vec3{ hx, hy, h }, Vec3{ 0.f, 1.f, 0.f });
    quad(Vec3{ -hx, -hy, 0.f }, Vec3{ hx, -hy, 0.f }, Vec3{ hx, -hy, h }, Vec3{ -hx, -hy, h }, Vec3{ 0.f, -1.f, 0.f });
    quad(Vec3{ -hx, -hy, h }, Vec3{ hx, -hy, h }, Vec3{ hx, hy, h }, Vec3{ -hx, hy, h }, Vec3{ 0.f, 0.f, 1.f });
    prim.material.metallic = 0.f;
    prim.material.roughness = 1.f;
    prim.material.double_sided = true;
    asset.vertex_count = prim.vertices.size();
    asset.bounds_min = Vec3{ -hx, -hy, 0.f };
    asset.bounds_max = Vec3{ hx, hy, h };
    asset.primitives.push_back(std::move(prim));
    return asset;
}

//! 範囲の中の 3 x 3 画素の平均（RGB。0〜255）。`x, y` は左上原点の画素座標。
std::array<double, 3> lr_mean_rgb(const std::vector<unsigned char> &image, int w, int h, float x, float y, int half)
{
    std::array<double, 3> sum{ 0.0, 0.0, 0.0 };
    int n = 0;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            int rgb[3]{};
            pixel_at(image, w, h, static_cast<int>(std::lround(x)) + dx, static_cast<int>(std::lround(y)) + dy, rgb);
            for (int c = 0; c < 3; ++c) {
                sum[static_cast<std::size_t>(c)] += rgb[c];
            }
            ++n;
        }
    }
    for (double &v : sum) {
        v /= static_cast<double>(n);
    }
    return sum;
}

double lr_luma(const std::array<double, 3> &c)
{
    return (0.2126 * c[0]) + (0.7152 * c[1]) + (0.0722 * c[2]);
}

//! 画素の差の最大値（RGB の各成分）。
int lr_max_diff(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b)
{
    if (a.size() != b.size()) {
        return 255;
    }
    int worst = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((i & 3u) == 3u) {
            continue;
        }
        worst = std::max(worst, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
    }
    return worst;
}

} // namespace

int run_light_range_check(const AppOptions &options)
{
    int failures = 0;
    int passes = 0;
    const auto check = [&](bool ok, const std::string &what) {
        if (ok) {
            ++passes;
            std::fprintf(stderr, "  PASS %s\n", what.c_str());
        } else {
            ++failures;
            std::fprintf(stderr, "  **FAIL** %s\n", what.c_str());
        }
    };
    const auto near = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };

    /* ======================================================== (1) CPU だけで見るもの */
    {
        std::fprintf(stderr, "[hd2d] light-range-check (1) 場・揺らぎ・光源の種類（CPU）\n");
        //! 5x5 の小さな窓。(12, 22) が真ん中。
        const auto make = [](int ox, int oy, std::uint64_t turn) {
            GameFrame f;
            f.floor.kind = static_cast<int>(FloorKind::Dungeon);
            f.floor.generated_turn = turn;
            for (int y = 0; y < 5; ++y) {
                for (int x = 0; x < 5; ++x) {
                    MapCellView c{};
                    c.gx = static_cast<std::int16_t>(ox + x);
                    c.gy = static_cast<std::int16_t>(oy + y);
                    f.cells.push_back(c);
                }
            }
            f.lighting.player_lit.assign((f.cells.size() + 7u) / 8u, 0u);
            return f;
        };
        const auto set_bit = [](GameFrame &f, int gx, int gy, bool on) {
            for (std::size_t i = 0; i < f.cells.size(); ++i) {
                if ((f.cells[i].gx == gx) && (f.cells[i].gy == gy)) {
                    if (on) {
                        f.lighting.player_lit[i >> 3] = static_cast<std::uint8_t>(f.lighting.player_lit[i >> 3] | (1u << (i & 7u)));
                    } else {
                        f.lighting.player_lit[i >> 3] = static_cast<std::uint8_t>(f.lighting.player_lit[i >> 3] & ~(1u << (i & 7u)));
                    }
                }
            }
        };
        LightRangeParams params;
        params.fade_seconds = 0.25f;

        LitRangeField field;
        GameFrame f0 = make(10, 20, 100);
        field.update(f0, 0.016f, params);
        check(field.active() && (field.value_at(12, 22) == 0.f), "場: player_lit が来たら有効で、最初は全部 0");
        GameFrame f1 = f0;
        set_bit(f1, 12, 22, true);
        field.update(f1, 0.1f, params);
        check(near(field.value_at(12, 22), 0.4, 1e-4) && (field.value_at(13, 22) == 0.f),
            "場: 0.1 秒で 0.4（線形・既定 0.25 秒）。隣のマスは動かない");
        field.update(f1, 0.1f, params);
        field.update(f1, 0.1f, params);
        check(near(field.value_at(12, 22), 1.0, 1e-4), "場: 0.3 秒で 1.0 に届いて止まる");
        GameFrame f2 = f1;
        set_bit(f2, 12, 22, false);
        field.update(f2, 0.125f, params);
        check(near(field.value_at(12, 22), 0.5, 1e-4) && (field.target_at(12, 22) == 0.f), "場: 範囲を出ると 0.125 秒で 0.5 まで下がる");
        // 新しく窓に入ったマスは、その場で目標に合わせる。
        GameFrame f3 = make(11, 20, 100); // 1 マス東へずらした窓
        set_bit(f3, 12, 22, true);
        set_bit(f3, 15, 22, true); // 新しく窓に入る端のマス
        field.update(f3, 0.05f, params);
        check(near(field.value_at(15, 22), 1.0, 1e-4), "場: 新しく窓に入ったマスは目標にそのまま合う（端がふわっと光らない）");
        check(near(field.value_at(12, 22), 0.5 + 0.2, 1e-3), "場: 窓がずれても前から居るマスは続きから寄る（0.5 → 0.7）");
        // 階が変わったら全マス即座。
        GameFrame f4 = make(11, 20, 999);
        set_bit(f4, 12, 22, false);
        field.update(f4, 0.01f, params);
        check(field.value_at(12, 22) == 0.f, "場: 階が変わったら持っていた値を捨てて目標へ即座に合わせる");
        // 即座に合わせる設定。
        LightRangeParams instant = params;
        instant.fade_seconds = 0.f;
        GameFrame f5 = f4;
        set_bit(f5, 13, 23, true);
        field.update(f5, 0.001f, instant);
        check(field.value_at(13, 23) == 1.f, "場: fade 0 なら 1 回の更新で目標に合う");
        // 送らない（空）なら無効。
        GameFrame f6 = f5;
        f6.lighting.player_lit.clear();
        field.update(f6, 0.1f, params);
        check(!field.active() && (field.value_at(13, 23) == 0.f), "場: player_lit が空なら無効（従来の絵のまま）");
        // 戻った直後は即座に合う（前の値を引きずらない）。
        field.update(f5, 0.001f, params);
        check(near(field.value_at(13, 23), 1.0, 1e-4), "場: 送り直されたら全マスを即座に合わせる");
    }
    {
        // 記憶のマスの暗さ（`memory_dim` を申告したコアだけ。Rogue 系）。
        const auto make = [](int ox, int oy, std::uint64_t turn) {
            GameFrame f;
            f.floor.kind = static_cast<int>(FloorKind::Dungeon);
            f.floor.generated_turn = turn;
            for (int y = 0; y < 5; ++y) {
                for (int x = 0; x < 5; ++x) {
                    MapCellView c{};
                    c.gx = static_cast<std::int16_t>(ox + x);
                    c.gy = static_cast<std::int16_t>(oy + y);
                    c.light_level = 2; // 見えている
                    f.cells.push_back(c);
                }
            }
            f.lighting.player_lit.assign((f.cells.size() + 7u) / 8u, 0u);
            return f;
        };
        const auto set_level = [](GameFrame &f, int gx, int gy, int level) {
            for (MapCellView &c : f.cells) {
                if ((c.gx == gx) && (c.gy == gy)) {
                    c.light_level = static_cast<std::uint8_t>(level);
                }
            }
        };
        LightRangeParams params;
        params.fade_seconds = 0.25f;

        // 申告なし: light_level 1 を並べても記憶の値は 0 のまま（変愚の 1 は「視界内の暗がり」で記憶ではない）。
        {
            GameFrame f = make(10, 20, 100);
            set_level(f, 12, 22, 1);
            LitRangeField field;
            field.update(f, 0.016f, params);
            check(field.active() && !field.memory_declared() && (field.memory_at(12, 22) == 0.f)
                    && (field.memory_target_at(12, 22) == 0.f),
                "記憶のマス: 申告（memory_dim）が無ければ、light_level 1 のマスでも記憶の暗さは 0（変愚などは変わらない）");
        }
        // 申告あり: light_level 1 のマスだけ目標 1。最初のフレームは目標にそのまま合う。
        GameFrame f0 = make(10, 20, 100);
        f0.lighting.memory_dim = 1;
        set_level(f0, 12, 22, 1);
        set_level(f0, 11, 22, 1);
        set_level(f0, 13, 22, 0); // 0 は「知らないマス」。暗くしない
        LitRangeField field;
        field.update(f0, 0.016f, params);
        check(field.active() && field.memory_declared() && (field.memory_at(12, 22) == 1.f) && (field.memory_at(11, 22) == 1.f),
            "記憶のマス: 申告があれば、light_level 1 のマスが記憶（目標 1）。最初のフレームは目標にそのまま合う");
        check((field.memory_at(13, 22) == 0.f) && (field.memory_at(14, 22) == 0.f) && (field.value_at(12, 22) == 0.f),
            "記憶のマス: light_level 0（知らない）と 2（見えている）は暗くしない。光の範囲の値には混ざらない");
        // 見えるようになったら、短い時間でなめらかに戻る（線形・0.25 秒）。
        GameFrame f1 = f0;
        set_level(f1, 12, 22, 2);
        field.update(f1, 0.1f, params);
        check(near(field.memory_at(12, 22), 0.6, 1e-4) && (field.memory_target_at(12, 22) == 0.f) && (field.memory_at(11, 22) == 1.f),
            "記憶のマス: 見えるようになると 0.1 秒で 1 → 0.6（線形）。隣の記憶のマスは動かない");
        field.update(f1, 0.2f, params);
        check(field.memory_at(12, 22) == 0.f, "記憶のマス: 0.3 秒で 0 に届いて止まる");
        // また見えなくなったら暗くなっていく。
        field.update(f0, 0.125f, params);
        check(near(field.memory_at(12, 22), 0.5, 1e-4), "記憶のマス: 見えなくなると 0.125 秒で 0 → 0.5");
        // プレイヤの光に照らされているマスは見えているので、light_level が 1 でも暗くしない。
        GameFrame f2 = f0;
        f2.lighting.player_lit[(12 - 10 + (22 - 20) * 5) >> 3] = static_cast<std::uint8_t>(1u << ((12 - 10 + (22 - 20) * 5) & 7));
        LitRangeField lit_wins;
        lit_wins.update(f2, 0.016f, params);
        check((lit_wins.value_at(12, 22) == 1.f) && (lit_wins.memory_at(12, 22) == 0.f) && (lit_wins.memory_at(11, 22) == 1.f),
            "記憶のマス: 光の範囲に入っているマスは、light_level が 1 でも記憶として暗くしない");
        // 階が変わったら即座に合う。
        GameFrame f3 = make(10, 20, 777);
        f3.lighting.memory_dim = 1;
        set_level(f3, 12, 22, 1);
        field.update(f3, 0.001f, params);
        check(field.memory_at(12, 22) == 1.f, "記憶のマス: 階が変わったら持っていた値を捨てて目標へ即座に合わせる");
        // player_lit が空でも、申告があれば有効（記憶の暗さだけ働く）。空で申告もなければ無効。
        GameFrame f4 = f0;
        f4.lighting.player_lit.clear();
        LitRangeField only_memory;
        only_memory.update(f4, 0.016f, params);
        check(only_memory.active() && (only_memory.memory_at(12, 22) == 1.f) && (only_memory.value_at(12, 22) == 0.f),
            "記憶のマス: player_lit が空でも申告があれば有効で、光の範囲の値は 0");
        f4.lighting.memory_dim = 0;
        only_memory.update(f4, 0.016f, params);
        check(!only_memory.active() && (only_memory.memory_at(12, 22) == 0.f),
            "記憶のマス: player_lit が空で申告もなければ無効（従来の絵のまま）");
        // 申告が途中で現れたマスは、その場で目標に合わせる（前の値を引きずらない）。
        LitRangeField late;
        GameFrame f5 = f0;
        f5.lighting.memory_dim = 0;
        late.update(f5, 0.016f, params);
        late.update(f0, 0.016f, params);
        check(late.memory_at(12, 22) == 1.f, "記憶のマス: 申告が後から付いたときは、その場で目標に合わせる");
    }
    {
        float lo = 10.f;
        float hi = -10.f;
        float max_step = 0.f;
        float prev = light_flicker_gain(1, 0.f);
        for (int i = 1; i <= 3600; ++i) {
            const float g = light_flicker_gain(1, static_cast<float>(i) / 60.f);
            lo = std::min(lo, g);
            hi = std::max(hi, g);
            max_step = std::max(max_step, std::fabs(g - prev));
            prev = g;
        }
        std::fprintf(stderr, "  松明の揺らぎ 60 秒: %.4f 〜 %.4f（1 フレームの最大の変化 %.4f）\n", static_cast<double>(lo),
            static_cast<double>(hi), static_cast<double>(max_step));
        check((lo >= 0.9399f) && (hi <= 1.0601f) && ((hi - lo) > 0.06f), "揺らぎ: 松明は 1 ± 6% の範囲で動く（幅が 0.06 以上ある）");
        check(max_step < 0.01f, "揺らぎ: 1 フレーム（1/60 秒）の変化は 1% 未満＝ちらつかずゆっくり");
        bool others_steady = true;
        for (const int kind : { 0, 2, 3, 4, 99 }) {
            for (int i = 0; i < 100; ++i) {
                others_steady = others_steady && (light_flicker_gain(kind, static_cast<float>(i) * 0.37f) == 1.f);
            }
        }
        check(others_steady, "揺らぎ: 松明・暗黒側以外（0・2・3・4・知らない値）は揺らがない");
    }
    {
        // 暗黒側（5）は、範囲の濃さがゆっくり息をするように揺れる（縁に揺らぐ影）。松明より遅く、幅は ±12% 以内。
        float lo = 10.f;
        float hi = -10.f;
        float max_step = 0.f;
        float prev = light_flicker_gain(5, 0.f);
        for (int i = 1; i <= 3600; ++i) {
            const float g = light_flicker_gain(5, static_cast<float>(i) / 60.f);
            lo = std::min(lo, g);
            hi = std::max(hi, g);
            max_step = std::max(max_step, std::fabs(g - prev));
            prev = g;
        }
        std::fprintf(stderr, "  暗黒側の揺らぎ 60 秒: %.4f 〜 %.4f（1 フレームの最大の変化 %.4f）\n", static_cast<double>(lo),
            static_cast<double>(hi), static_cast<double>(max_step));
        check((lo >= 0.879f) && (hi <= 1.121f) && ((hi - lo) > 0.08f), "揺らぎ: 暗黒側(5)は 1 ± 12% の範囲で動く（幅が 0.08 以上ある）");
        check(max_step < 0.01f, "揺らぎ: 暗黒側(5)も 1 フレーム（1/60 秒）の変化は 1% 未満＝ゆっくり");
    }
    {
        const LightKindLook k0 = light_kind_look(0);
        const LightKindLook k1 = light_kind_look(1);
        const LightKindLook k2 = light_kind_look(2);
        const LightKindLook k3 = light_kind_look(3);
        const LightKindLook k4 = light_kind_look(4);
        const LightKindLook k9 = light_kind_look(99);
        check((k0.torch_r == 1.00f) && (k0.torch_g == 0.78f) && (k0.torch_b == 0.48f), "光源の種類: 0 は従来の暖色（1.00, 0.78, 0.48）");
        check((k9.torch_r == k0.torch_r) && (k9.torch_g == k0.torch_g) && (k9.torch_b == k0.torch_b), "光源の種類: 知らない値は 0 と同じ");
        check((k1.torch_r > k1.torch_g) && (k1.torch_g > k1.torch_b) && ((k1.torch_g / k1.torch_r) < (k0.torch_g / k0.torch_r)),
            "光源の種類: 松明は従来の暖色より橙");
        check((k2.torch_g / k2.torch_r) > (k0.torch_g / k0.torch_r) && (k2.torch_r > k2.torch_b), "光源の種類: ランタンは従来の暖色より黄");
        check((k3.torch_b >= k3.torch_r) && (k3.torch_b > k3.torch_g), "光源の種類: アーティファクトは青みのある白");
        check((std::fabs(k4.torch_r - k4.torch_b) < 0.08f) && (k4.torch_r > 0.9f), "光源の種類: それ以外は淡い白");
        check((k1.flicker > 0.f) && (k0.flicker == 0.f) && (k2.flicker == 0.f) && (k3.flicker == 0.f) && (k4.flicker == 0.f),
            "光源の種類: 光の側で揺らぐのは松明だけ");
        const LightKindLook k5 = light_kind_look(5);
        check((k5.tint_b > k5.tint_r) && (k5.tint_r < 0.8f) && (k5.torch_b > k5.torch_r),
            "光源の種類: 暗黒側(5)は冷たい色（青が赤より強い）。暖色の既定ではない");
        check(k5.flicker > 0.f, "光源の種類: 暗黒側(5)は範囲の濃さが揺れる");
    }
    {
        GameFrame f = make_synthetic_frame(23, 19, 100, 44);
        SceneLighting a = make_scene_lighting(f.lighting, f.floor);
        (void)collect_point_lights(f, Vec3{ 100.5f, 44.5f, 0.f }, a);
        check(!a.points.empty() && (a.points[0].r == 1.00f) && (a.points[0].g == 0.78f) && (a.points[0].b == 0.48f)
                && (a.points[0].intensity == 1.25f),
            "点光源: light_kind 0 の松明は従来どおり（色と強さが 1 ビットも変わらない）");
        f.lighting.light_kind = 1;
        SceneLighting b = make_scene_lighting(f.lighting, f.floor);
        (void)collect_point_lights(f, Vec3{ 100.5f, 44.5f, 0.f }, b, 1.f, nullptr, 0.95f);
        const LightKindLook k1 = light_kind_look(1);
        check(!b.points.empty() && (b.points[0].r == k1.torch_r) && (b.points[0].g == k1.torch_g)
                && (near(b.points[0].intensity, 1.25 * 0.95, 1e-5)),
            "点光源: light_kind 1 は橙で、揺らぎの倍率が強さに掛かる");
        // 暗黒側(5)は光ではないので、プレイヤの位置に暖かい点光源を置かない（半径 = 光源半径 + 1.5 の光が無い）。
        f.lighting.light_radius = 3;
        f.lighting.light_kind = 1;
        SceneLighting torch = make_scene_lighting(f.lighting, f.floor);
        (void)collect_point_lights(f, Vec3{ 100.5f, 44.5f, 0.f }, torch);
        f.lighting.light_kind = 5;
        SceneLighting dark = make_scene_lighting(f.lighting, f.floor);
        (void)collect_point_lights(f, Vec3{ 100.5f, 44.5f, 0.f }, dark);
        const auto has_player_light = [](const SceneLighting &s) {
            for (const PointLight &p : s.points) {
                if (std::fabs(p.radius - 4.5f) < 1e-4f) {
                    return true;
                }
            }
            return false;
        };
        check(has_player_light(torch) && !has_player_light(dark),
            "点光源: 暗黒側(5)ではプレイヤの位置に点光源を置かない（松明(1)では置く）");
    }

    /* ======================================================== (2) 描く検査 */
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
    BillboardRenderer sprites;
    ModelRenderer pillar_model;
    ShadowMap shadow;
    PostChain post;
    int screen_w = options.window_w;
    int screen_h = options.window_h;
    SDL_GetWindowSize(window.window, &screen_w, &screen_h);
    const ModelAsset pillar_asset = lr_make_pillar();
    if (!renderer.init(err) || !sprites.init(err) || !shadow.init(kShadowMapSide, err)
        || !init_post_chain(post, options, err) || !post.resize(screen_w, screen_h, err)
        || !pillar_model.init(pillar_asset, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    std::fprintf(stderr, "[hd2d] light-range-check (2) 描画 %dx%d\n", screen_w, screen_h);

    Prefab wall_prefab = make_box_prefab("wall", 32, 32, 32, 0, 32);
    Prefab floor_prefab = make_box_prefab("floor", 32, 32, 2, -2, 32);
    //! 実体の板（ボクセルのスラブ）の代役。幅 0.75・厚み 1/16・高さ 0.75 の立つ板。
    Prefab board_prefab = make_box_prefab("board", 24, 2, 24, 0, 32);
    GpuPrefab wall_gpu;
    GpuPrefab floor_gpu;
    GpuPrefab board_gpu;
    if (!renderer.upload(wall_prefab, wall_gpu, err) || !renderer.upload(floor_prefab, floor_gpu, err)
        || !renderer.upload(board_prefab, board_gpu, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    PrefabLibrary library;
    if (!library.load(options.voxel_dir, err) || !library.upload_gpu(renderer, err)) {
        std::fprintf(stderr, "[hd2d] light-range-check: プレハブライブラリを用意できませんでした:\n%s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    library.update_motions(0.58f); //!< 時刻は固定（罠 14）
    //! かきわりの絵（4x4 の淡い灰色）。
    GLuint sprite_atlas = 0;
    {
        std::vector<unsigned char> texels(4u * 4u * 4u, 255u);
        for (std::size_t i = 0; i < 16u; ++i) {
            texels[(i * 4u) + 0u] = 210;
            texels[(i * 4u) + 1u] = 205;
            texels[(i * 4u) + 2u] = 195;
        }
        glGenTextures(1, &sprite_atlas);
        glBindTexture(GL_TEXTURE_2D, sprite_atlas);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    Camera camera;
    camera.pitch = options.camera_pitch_deg * 0.0174533f;
    camera.fov_x = options.camera_fov_deg * 0.0174533f;
    camera.viewport_w = screen_w;
    camera.viewport_h = screen_h;
    camera.distance = distance_for_cell_px(camera, options.camera_cell_px);
    camera.yaw = view_turn_yaw(options.camera_turn);
    const ViewWindow view_full = derive_view_window(camera, kMaxPropHeight, 60.f);
    float z_near = 0.f;
    float z_far = 0.f;
    camera.depth_range(z_near, z_far);

    struct Scene {
        const char *name = "";
        bool bright = false;
        int px = 0;
        int py = 0;
        GameFrame frame;
        FloorMeaning meaning;
        TerrainMemory memory;
        TerrainView terrain;
        ShadowFit fit;
        std::vector<InstanceData> boards;
        std::vector<BillboardInstance> sprites;
        std::vector<Vec3> models;
        //! 調べるマス（床の測定点・実体のマス）。{ gx, gy }。
        std::pair<int, int> floor_lit{}, floor_dark{};
        std::pair<int, int> board_lit{}, board_dark{};
        std::pair<int, int> sprite_lit{}, sprite_dark{};
        std::pair<int, int> model_lit{}, model_dark{};
        //! 左右対称の測定点（光の半平面の検査。同じ距離で、片方だけ照らされる）。
        std::pair<int, int> mirror_lit{}, mirror_dark{};
    };
    std::vector<Scene> scenes(2);
    {
        Scene &a = scenes[0];
        a.name = "corridor"; // 暗い通路（東西に延びる幅 3 の通路）
        a.px = 101;
        a.py = 51;
        a.frame = make_synthetic_frame(view_full.cols, view_full.rows, a.px, a.py);
        lr_carve(a.frame, { LrRect{ 70, 50, 132, 52, false } });
        //! 床は北の列（y=50）で測る。南の壁が手前を隠すので、真ん中や南の列は壁の上面を測ってしまう。
        a.floor_lit = { 100, 50 };
        a.floor_dark = { 108, 50 };
        a.board_lit = { 103, 51 };
        a.board_dark = { 107, 51 };
        a.sprite_lit = { 99, 50 };
        a.sprite_dark = { 95, 52 };
        a.model_lit = { 104, 52 };
        a.model_dark = { 110, 50 };
        a.mirror_lit = { 105, 50 };
        a.mirror_dark = { 97, 50 };

        Scene &b = scenes[1];
        b.name = "room"; // 明るい部屋（広い部屋の真ん中。壁は松明の外）
        b.bright = true;
        b.px = 101;
        b.py = 50;
        b.frame = make_synthetic_frame(view_full.cols, view_full.rows, b.px, b.py);
        lr_carve(b.frame, { LrRect{ 86, 42, 116, 58, true } });
        b.floor_lit = { 102, 52 };
        b.floor_dark = { 108, 54 };
        b.board_lit = { 103, 50 };
        b.board_dark = { 108, 50 };
        b.sprite_lit = { 99, 49 };
        b.sprite_dark = { 94, 51 };
        b.model_lit = { 101, 52 };
        b.model_dark = { 101, 56 };
        b.mirror_lit = { 105, 50 };
        b.mirror_dark = { 97, 50 };
    }
    for (Scene &s : scenes) {
        s.frame.lighting.light_radius = 3;
        camera.target = Vec3{ static_cast<float>(s.px) + 0.5f, static_cast<float>(s.py) + 0.5f, 0.f };
        (void)rebuild_floor_meaning(s.frame, s.meaning);
        s.memory.update(s.frame);
        build_terrain_view(s.meaning, Frustum::from(camera.view_projection()), s.terrain, &s.memory, &library);
        SceneLighting base = make_scene_lighting(s.frame.lighting, s.frame.floor);
        s.fit = fit_shadow(camera, base.sun_dir, kMaxPropHeight, shadow.side(), 0.f);
        const auto cell_centre = [](const std::pair<int, int> &c, float z) {
            return Vec3{ static_cast<float>(c.first) + 0.5f, static_cast<float>(c.second) + 0.5f, z };
        };
        InstanceData board_lit;
        board_lit.x = static_cast<float>(s.board_lit.first) + 0.125f;
        board_lit.y = static_cast<float>(s.board_lit.second) + 0.5f - (1.f / 32.f);
        InstanceData board_dark = board_lit;
        board_dark.x = static_cast<float>(s.board_dark.first) + 0.125f;
        board_dark.y = static_cast<float>(s.board_dark.second) + 0.5f - (1.f / 32.f);
        s.boards = { board_lit, board_dark };
        for (const auto &c : { s.sprite_lit, s.sprite_dark }) {
            BillboardInstance one;
            one.x = static_cast<float>(c.first) + 0.5f;
            one.y = static_cast<float>(c.second) + 0.5f;
            one.z = 0.05f;
            one.width = 0.7f;
            one.height = 0.8f;
            one.u0 = 0.f;
            one.v0 = 0.f;
            one.u1 = 4.f;
            one.v1 = 4.f;
            s.sprites.push_back(one);
        }
        s.models = { cell_centre(s.model_lit, 0.f), cell_centre(s.model_dark, 0.f) };
    }

    const PostFlags all_off{ false, false, false, false, false };
    PostParams params;
    const std::array<double, 3> kNoColour{ 0.0, 0.0, 0.0 };

    // 描く 1 回ぶんの指定。
    struct Shot {
        int kind = 0;
        //! 0 = 場を結ばない（従来）/ 1 = 場を完全に寄せて結ぶ / 2 = 切り替えの途中（値 0.5）/ 3 = 光の半平面（左右対称の検査）
        int mode = 0;
        bool entities = true;
        bool pictures = false; //!< 後処理を全部入れる（絵）。偽なら全部切る（数える）
        float strength = 1.f;
        //! 記憶のマス: 0 = 何も付けない / 1 = 照らされない遠いマスを light_level 1（記憶）にして申告する /
        //! 2 = 同じ light_level を付けるが申告しない（変愚などの 1 と同じ形）
        int memory = 0;
        //! 記憶のマスの明るさの倍率（`LightRangeParams::memory_brightness`）。
        float memory_brightness = 0.78f;
    };
    Vec3 to_target{};
    const auto shoot = [&](Scene &s, const Shot &shot) {
        camera.target = Vec3{ static_cast<float>(s.px) + 0.5f, static_cast<float>(s.py) + 0.5f, 0.f };
        s.frame.lighting.light_kind = static_cast<std::uint8_t>(shot.kind);
        const int px = s.px;
        const int py = s.py;
        const auto circle = [px, py](int gx, int gy) {
            const double dx = static_cast<double>(gx - px);
            const double dy = static_cast<double>(gy - py);
            return ((dx * dx) + (dy * dy)) <= (3.4 * 3.4);
        };
        const auto half_plane = [px](int gx, int) { return gx >= px; };

        SceneLighting lighting = make_scene_lighting(s.frame.lighting, s.frame.floor);
        apply_lit_room_ambient(lighting, s.frame.floor, s.bright ? 1.f : 0.f);
        (void)collect_point_lights(s.frame, camera.target, lighting);

        LightRangeParams range;
        range.strength = shot.strength;
        range.fade_seconds = 0.25f;
        range.memory_brightness = shot.memory_brightness;
        LitRangeField field;
        if (shot.mode != 0) {
            GameFrame lit_frame = s.frame;
            if (shot.memory != 0) {
                //! 光の円の外は「いま見えていない」（light_level 1）、円の中は見えている（2）。
                for (MapCellView &cell : lit_frame.cells) {
                    cell.light_level = circle(cell.gx, cell.gy) ? 2 : 1;
                }
                lit_frame.lighting.memory_dim = (shot.memory == 1) ? 1 : 0;
            }
            lr_set_lit(lit_frame, [](int, int) { return false; });
            field.update(lit_frame, 0.016f, range); // 全部 0 から始める
            if (shot.mode == 3) {
                lr_set_lit(lit_frame, half_plane);
            } else {
                lr_set_lit(lit_frame, circle);
            }
            field.update(lit_frame, (shot.mode == 2) ? 0.125f : 1.0f, range);
            field.bind(lighting, range, shot.kind, 1.f);
        }

        const Scene &c = s;
        shadow.begin();
        renderer.draw_instanced_depth(floor_gpu, c.fit.view_projection, c.terrain.slabs.data(), c.terrain.slabs.size());
        renderer.draw_instanced_depth(wall_gpu, c.fit.view_projection, c.terrain.boxes.data(), c.terrain.boxes.size());
        draw_library_depth(renderer, library, c.terrain, c.fit.view_projection);
        shadow.end(screen_w, screen_h);
        post.begin_scene(0.04f, 0.05f, 0.07f);
        renderer.begin(camera.view_projection(), lighting, c.fit.view_projection, shadow.texture(), shadow.side());
        renderer.begin_cutaway(Cutaway{});
        renderer.set_cutaway_scale(0.f);
        renderer.draw_instanced(floor_gpu, c.terrain.slabs.data(), c.terrain.slabs.size());
        renderer.set_shadow_scale(0.f);
        renderer.draw_instanced(floor_gpu, c.terrain.under_slabs.data(), c.terrain.under_slabs.size());
        renderer.set_shadow_scale(1.f);
        renderer.draw_instanced(wall_gpu, c.terrain.boxes.data(), c.terrain.boxes.size());
        draw_library_color(renderer, library, c.terrain);
        if (shot.entities) {
            renderer.draw_instanced(board_gpu, c.boards.data(), c.boards.size());
            sprites.draw(camera.view_projection(), lighting, camera.azimuth(), camera.eye(), c.fit.view_projection,
                shadow.texture(), shadow.side(), sprite_atlas, 4, c.sprites.data(), c.sprites.size());
            pillar_model.draw(camera.view_projection(), Mat4::identity(), lighting, camera.eye(), c.fit.view_projection,
                shadow.texture(), shadow.side(), c.models.data(), c.models.size());
        }
        post.end_scene(screen_w, screen_h);
        to_target = camera.target - camera.eye();
        const float focus = std::sqrt(dot(to_target, to_target));
        post.resolve(shot.pictures ? PostFlags{} : all_off, params, z_near, z_far, focus, screen_w, screen_h, 0, 0,
            camera.view_projection(), camera.target.x, camera.target.y, to_target.x, to_target.y);
        field.release(); // この回のテクスチャは使い終わった
        return read_framebuffer(screen_w, screen_h);
    };

    // マスの上の測定点（画素）。
    const auto mean_at = [&](const std::vector<unsigned char> &image, const std::pair<int, int> &cell, float dx, float dy,
                             float z, int half) {
        float sx = 0.f;
        float sy = 0.f;
        if (!camera.project(Vec3{ static_cast<float>(cell.first) + dx, static_cast<float>(cell.second) + dy, z }, sx, sy)) {
            return kNoColour;
        }
        return lr_mean_rgb(image, screen_w, screen_h, sx, sy, half);
    };
    const auto fmt = [](const std::array<double, 3> &c) {
        char buf[64]{};
        std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", c[0], c[1], c[2]);
        return std::string(buf);
    };

    // 4 回回してから読む（1 枚目はドライバの都合で空のことがある）。
    for (int i = 0; i < 3; ++i) {
        (void)shoot(scenes[0], Shot{});
    }

    for (Scene &s : scenes) {
        std::fprintf(stderr, "[hd2d] light-range-check シーン \"%s\"\n", s.name);
        camera.target = Vec3{ static_cast<float>(s.px) + 0.5f, static_cast<float>(s.py) + 0.5f, 0.f };
        for (const int kind : { 0, 1, 2, 3, 4, 5 }) {
            Shot off;
            off.kind = kind;
            off.mode = 0;
            Shot on = off;
            on.mode = 1;
            const std::vector<unsigned char> img_off = shoot(s, off);
            const std::vector<unsigned char> img_on = shoot(s, on);
            const std::string tag = std::string(s.name) + " kind=" + std::to_string(kind) + ": ";
            //! 5 は暗黒側。照らされたマスは**暗くなる**（ほかの種類は明るくなる）。
            const bool dark_side = (kind == 5);

            // 床
            const auto f_lit_off = mean_at(img_off, s.floor_lit, 0.5f, 0.5f, 0.f, 3);
            const auto f_lit_on = mean_at(img_on, s.floor_lit, 0.5f, 0.5f, 0.f, 3);
            const auto f_dark_off = mean_at(img_off, s.floor_dark, 0.5f, 0.5f, 0.f, 3);
            const auto f_dark_on = mean_at(img_on, s.floor_dark, 0.5f, 0.5f, 0.f, 3);
            std::fprintf(stderr, "    %s床 照らされたマス %s → %s / そうでないマス %s → %s\n", tag.c_str(),
                fmt(f_lit_off).c_str(), fmt(f_lit_on).c_str(), fmt(f_dark_off).c_str(), fmt(f_dark_on).c_str());
            if (dark_side) {
                check(lr_luma(f_lit_off) > 3.0, tag + "暗黒側: 測定点の床が写っている（暗すぎて測れないではない）");
                check(lr_luma(f_lit_on) < (lr_luma(f_lit_off) * 0.92), tag + "暗黒側: 範囲に入ったマスの床が、場を結ぶと暗くなる");
            } else {
                check(lr_luma(f_lit_on) > (lr_luma(f_lit_off) * 1.06), tag + "照らされたマスの床が、場を結ぶと明るくなる");
            }
            check(std::fabs(lr_luma(f_dark_on) - lr_luma(f_dark_off)) < 0.01,
                tag + "照らされていないマスの床は場を結んでも変わらない");

            // 実体 3 種（板・かきわり・3D モデル）
            struct Probe {
                const char *label;
                const std::pair<int, int> *lit;
                const std::pair<int, int> *dark;
                float dx;
                float dy;
                float z;
            };
            const Probe probes[] = {
                { "板", &s.board_lit, &s.board_dark, 0.5f, 0.55f, 0.35f },
                { "かきわり", &s.sprite_lit, &s.sprite_dark, 0.5f, 0.5f, 0.4f },
                { "3Dモデル", &s.model_lit, &s.model_dark, 0.5f, 0.5f, 0.78f },
            };
            for (const Probe &p : probes) {
                const auto lit_off = mean_at(img_off, *p.lit, p.dx, p.dy, p.z, 2);
                const auto lit_on = mean_at(img_on, *p.lit, p.dx, p.dy, p.z, 2);
                const auto dark_off = mean_at(img_off, *p.dark, p.dx, p.dy, p.z, 2);
                const auto dark_on = mean_at(img_on, *p.dark, p.dx, p.dy, p.z, 2);
                std::fprintf(stderr, "    %s%s 照らされたマス %s → %s / そうでないマス %s → %s\n", tag.c_str(), p.label,
                    fmt(lit_off).c_str(), fmt(lit_on).c_str(), fmt(dark_off).c_str(), fmt(dark_on).c_str());
                check(lr_luma(lit_off) > 8.0, tag + p.label + ": 測定点に実体が写っている（暗くて測れない・写っていないではない）");
                if (dark_side) {
                    check(lr_luma(lit_on) < (lr_luma(lit_off) * 0.95), tag + p.label + ": 暗黒側: 範囲に入ったマスに立つものが、場を結ぶと暗くなる");
                } else {
                    check(lr_luma(lit_on) > (lr_luma(lit_off) * 1.04), tag + p.label + ": 照らされたマスに立つものが、場を結ぶと明るくなる");
                }
                check(std::fabs(lr_luma(dark_on) - lr_luma(dark_off)) < 0.01,
                    tag + p.label + ": 照らされていないマスに立つものは場を結んでも変わらない");
            }
        }
    }

    /* --- 色味（kind ごとの、場を結んだときの足し分） --- */
    for (Scene &s : scenes) {
        std::array<std::array<double, 3>, 6> delta{};
        for (const int kind : { 0, 1, 2, 3, 4, 5 }) {
            Shot off;
            off.kind = kind;
            Shot on = off;
            on.mode = 1;
            const auto a = mean_at(shoot(s, off), s.floor_lit, 0.5f, 0.5f, 0.f, 3);
            const auto b = mean_at(shoot(s, on), s.floor_lit, 0.5f, 0.5f, 0.f, 3);
            for (std::size_t c = 0; c < 3; ++c) {
                delta[static_cast<std::size_t>(kind)][c] = b[c] - a[c];
            }
            std::fprintf(stderr, "    %s kind=%d 足し分 R%+.1f G%+.1f B%+.1f\n", s.name, kind,
                delta[static_cast<std::size_t>(kind)][0], delta[static_cast<std::size_t>(kind)][1],
                delta[static_cast<std::size_t>(kind)][2]);
        }
        const std::string tag = std::string(s.name) + " 色味: ";
        const auto ratio = [](const std::array<double, 3> &d, std::size_t num, std::size_t den) {
            return (std::fabs(d[den]) > 1e-9) ? (d[num] / d[den]) : 99.0;
        };
        // G/R: ランタン（黄）> 従来 > 松明（橙）。B/R: アーティファクト（青白）> それ以外（淡い白）> 従来 > 松明。
        check((delta[1][0] > delta[1][1]) && (delta[1][1] > delta[1][2]) && (ratio(delta[1], 1, 0) < ratio(delta[0], 1, 0)),
            tag + "松明（1）は橙（従来の 0 より緑が弱く、赤>緑>青）");
        check((ratio(delta[2], 1, 0) > ratio(delta[0], 1, 0)) && (delta[2][0] >= delta[2][1]) && (delta[2][1] > delta[2][2]),
            tag + "ランタン（2）は黄（従来の 0 より緑が強く、青が最も弱い）");
        check((ratio(delta[3], 2, 0) > ratio(delta[4], 2, 0)) && (ratio(delta[4], 2, 0) > ratio(delta[0], 2, 0))
                && (ratio(delta[0], 2, 0) > ratio(delta[1], 2, 0)),
            tag + "青の割合: アーティファクト（3）> それ以外（4）> 従来（0）> 松明（1）");
        check((delta[4][0] > 0.0) && (ratio(delta[4], 2, 0) > 0.7) && (ratio(delta[4], 1, 0) > 0.8),
            tag + "それ以外（4）は淡い白（赤・緑・青の足し分がほぼそろう）");
        // 暗黒側(5): 赤・緑・青のすべてが減り、青の減りがいちばん小さい（冷たい色に寄る）。
        check((delta[5][0] < 0.0) && (delta[5][1] < 0.0) && (delta[5][2] <= 0.0) && (delta[5][2] > delta[5][0])
                && (delta[5][2] >= delta[5][1]),
            tag + "暗黒側（5）は暗くなり、青の減りがいちばん小さい（冷たい色）");
    }

    /* --- 切り替えの途中（時刻固定）・左右対称・場なし・強さ 0 --- */
    for (Scene &s : scenes) {
        const std::string tag = std::string(s.name) + ": ";
        Shot base;
        base.kind = 1;
        Shot full = base;
        full.mode = 1;
        Shot mid = base;
        mid.mode = 2;
        const auto img_off = shoot(s, base);
        const auto img_full = shoot(s, full);
        const auto img_mid = shoot(s, mid);
        const double l_off = lr_luma(mean_at(img_off, s.floor_lit, 0.5f, 0.5f, 0.f, 3));
        const double l_full = lr_luma(mean_at(img_full, s.floor_lit, 0.5f, 0.5f, 0.f, 3));
        const double l_mid = lr_luma(mean_at(img_mid, s.floor_lit, 0.5f, 0.5f, 0.f, 3));
        std::fprintf(stderr, "    %s切り替えの途中 輝度 なし %.1f / 途中（0.125 秒）%.1f / 完了 %.1f\n", tag.c_str(), l_off, l_mid, l_full);
        check((l_mid > (l_off + 0.5)) && (l_mid < (l_full - 0.5)), tag + "切り替えの途中は、なしと完了の間の明るさ");

        // 左右対称（同じ距離で、片方だけ照らされる）
        Shot half = base;
        half.mode = 3;
        half.entities = false;
        const auto img_half = shoot(s, half);
        const double l_lit = lr_luma(mean_at(img_half, s.mirror_lit, 0.5f, 0.5f, 0.f, 3));
        const double l_dark = lr_luma(mean_at(img_half, s.mirror_dark, 0.5f, 0.5f, 0.f, 3));
        std::fprintf(stderr, "    %s左右対称（距離 4）輝度 照らされたマス %.1f / そうでないマス %.1f\n", tag.c_str(), l_lit, l_dark);
        check(l_lit > (l_dark * 1.06), tag + "同じ距離なら、照らされたマスの床がそうでないマスより明るい");

        // 場なし（player_lit 空）・強さ 0 は、結ばない絵と画素まで同じ
        Shot zero = base;
        zero.mode = 1;
        zero.strength = 0.f;
        const auto img_zero = shoot(s, zero);
        check(lr_max_diff(img_zero, img_off) == 0, tag + "強さ 0 なら、場を結ばない絵と 1 画素も違わない");
        // 暗黒側(5)も同じ約束: 左右対称で、範囲に入った側が暗い。強さ 0 なら結ばない絵と同じ。
        {
            Shot dark_half = half;
            dark_half.kind = 5;
            Shot dark_off = base;
            dark_off.kind = 5;
            Shot dark_zero = dark_off;
            dark_zero.mode = 1;
            dark_zero.strength = 0.f;
            const auto img_dark_half = shoot(s, dark_half);
            const double d_lit = lr_luma(mean_at(img_dark_half, s.mirror_lit, 0.5f, 0.5f, 0.f, 3));
            const double d_dark = lr_luma(mean_at(img_dark_half, s.mirror_dark, 0.5f, 0.5f, 0.f, 3));
            std::fprintf(stderr, "    %s暗黒側 左右対称（距離 4）輝度 範囲のマス %.1f / 範囲の外 %.1f\n", tag.c_str(), d_lit, d_dark);
            check(d_lit < (d_dark * 0.94), tag + "暗黒側: 同じ距離なら、範囲に入った側の床が外より暗い");
            check(lr_max_diff(shoot(s, dark_zero), shoot(s, dark_off)) == 0, tag + "暗黒側: 強さ 0 なら、場を結ばない絵と 1 画素も違わない");
        }
        // 記憶のマス（Rogue 系が `memory_dim` を申告したときだけ、light_level 1 のマスを少し暗くする）。
        {
            Shot ref = base;
            ref.mode = 1; // 光の範囲あり・記憶の印なし
            Shot declared = ref;
            declared.memory = 1;
            Shot undeclared = ref;
            undeclared.memory = 2;
            Shot full = ref;
            full.memory = 1;
            full.memory_brightness = 1.f;
            Shot declared_zero = declared;
            declared_zero.strength = 0.f;
            Shot ref_zero = ref;
            ref_zero.strength = 0.f;
            const auto img_ref = shoot(s, ref);
            const auto img_declared = shoot(s, declared);
            const std::string mtag = tag + "記憶のマス: ";
            check(lr_max_diff(shoot(s, undeclared), img_ref) == 0,
                mtag + "申告が無ければ、light_level 1 のマスを並べても 1 画素も変わらない（変愚などの見た目は変わらない）");
            check(lr_max_diff(shoot(s, full), img_ref) == 0, mtag + "明るさの倍率が 1 なら、申告があっても 1 画素も変わらない");

            struct MemProbe {
                const char *label;
                const std::pair<int, int> *cell;
                float dx;
                float dy;
                float z;
                int half;
                bool visible; //!< 見えているマス（暗くしない側）か
            };
            const MemProbe mem_probes[] = {
                { "床（遠い）", &s.floor_dark, 0.5f, 0.5f, 0.f, 3, false },
                { "床（左右対称の遠い側）", &s.mirror_dark, 0.5f, 0.5f, 0.f, 3, false },
                { "板（遠い）", &s.board_dark, 0.5f, 0.55f, 0.35f, 2, false },
                { "かきわり（遠い）", &s.sprite_dark, 0.5f, 0.5f, 0.4f, 2, false },
                { "3Dモデル（遠い）", &s.model_dark, 0.5f, 0.5f, 0.78f, 2, false },
                { "床（見えている）", &s.floor_lit, 0.5f, 0.5f, 0.f, 3, true },
                { "板（見えている）", &s.board_lit, 0.5f, 0.55f, 0.35f, 2, true },
                { "かきわり（見えている）", &s.sprite_lit, 0.5f, 0.5f, 0.4f, 2, true },
                { "3Dモデル（見えている）", &s.model_lit, 0.5f, 0.5f, 0.78f, 2, true },
            };
            for (const MemProbe &p : mem_probes) {
                const double before = lr_luma(mean_at(img_ref, *p.cell, p.dx, p.dy, p.z, p.half));
                const double after = lr_luma(mean_at(img_declared, *p.cell, p.dx, p.dy, p.z, p.half));
                std::fprintf(stderr, "    %s%s 輝度 %.2f → %.2f（%.3f 倍）\n", mtag.c_str(), p.label, before, after,
                    (before > 0.0) ? (after / before) : 0.0);
                check(before > 4.0, mtag + p.label + ": 測定点が写っている（暗くて測れない・写っていないではない）");
                if (p.visible) {
                    check(std::fabs(after - before) < 0.01, mtag + p.label + ": 見えているマスは申告があっても変わらない");
                } else {
                    check((after < (before * 0.92)) && (after > (before * 0.55)),
                        mtag + p.label + ": 見えていない記憶のマスは、はっきり分かるが見えなくならない程度（0.55〜0.92 倍）に暗い");
                }
            }
            // 光の範囲の強さが 0 でも、記憶のマスの暗さは働く。見えているマスは結ばない絵と同じ。
            const auto img_declared_zero = shoot(s, declared_zero);
            const auto img_ref_zero = shoot(s, ref_zero);
            const double z_before = lr_luma(mean_at(img_ref_zero, s.floor_dark, 0.5f, 0.5f, 0.f, 3));
            const double z_after = lr_luma(mean_at(img_declared_zero, s.floor_dark, 0.5f, 0.5f, 0.f, 3));
            check(z_after < (z_before * 0.92), mtag + "光の範囲の強さが 0 でも、記憶のマスは暗くなる");
            check(std::fabs(lr_luma(mean_at(img_declared_zero, s.floor_lit, 0.5f, 0.5f, 0.f, 3))
                      - lr_luma(mean_at(img_ref_zero, s.floor_lit, 0.5f, 0.5f, 0.f, 3)))
                    < 0.01,
                mtag + "光の範囲の強さが 0 のとき、見えているマスは変わらない");
        }
        // 空の player_lit（送らないコア）: 更新しても場は有効にならず、結ばない絵と同じ
        {
            LitRangeField empty_field;
            GameFrame empty_frame = s.frame;
            empty_frame.lighting.player_lit.clear();
            empty_field.update(empty_frame, 0.1f, LightRangeParams{});
            SceneLighting probe;
            empty_field.bind(probe, LightRangeParams{}, 1, 1.f);
            check(!empty_field.active() && !probe.lit_range.enabled, tag + "player_lit が空なら、場も有効にならず bind も何も結ばない");
        }
    }

    // 暗黒側の旗は bind が運ぶ（5 だけ真。0〜4・知らない値は偽）。
    {
        Scene &s = scenes[0];
        GameFrame lit_frame = s.frame;
        lr_set_lit(lit_frame, [](int, int) { return true; });
        LitRangeField dark_field;
        dark_field.update(lit_frame, 0.016f, LightRangeParams{});
        bool flags_ok = true;
        for (const int kind : { 0, 1, 2, 3, 4, 5, 99 }) {
            SceneLighting probe;
            dark_field.bind(probe, LightRangeParams{}, kind, 1.f);
            flags_ok = flags_ok && probe.lit_range.enabled && (probe.lit_range.dark == (kind == 5));
        }
        dark_field.release();
        check(flags_ok, "bind: 暗黒側の旗（lit_range.dark）は光源の種類 5 のときだけ真");
    }

    // bind が運ぶ記憶のマスの倍率: 申告があれば LightRangeParams の値、無ければ 1。強さ 0 でも申告があれば場は有効。
    {
        Scene &s = scenes[0];
        GameFrame mem_frame = s.frame;
        for (MapCellView &cell : mem_frame.cells) {
            cell.light_level = 1;
        }
        lr_set_lit(mem_frame, [](int, int) { return false; });
        bool declared_ok = true;
        bool undeclared_ok = true;
        bool zero_ok = true;
        bool no_texture_ok = true;
        for (const float strength : { 1.f, 0.f }) {
            LightRangeParams params;
            params.strength = strength;
            params.memory_brightness = 0.7f;
            LitRangeField declared_field;
            mem_frame.lighting.memory_dim = 1;
            declared_field.update(mem_frame, 0.016f, params);
            SceneLighting with;
            declared_field.bind(with, params, 1, 1.f);
            declared_ok = declared_ok && with.lit_range.enabled && (with.lit_range.texture != 0)
                && (std::fabs(with.lit_range.memory_brightness - 0.7f) < 1e-6f)
                && (std::fabs(with.lit_range.strength - strength) < 1e-6f);
            declared_field.release();

            LitRangeField plain_field;
            mem_frame.lighting.memory_dim = 0;
            plain_field.update(mem_frame, 0.016f, params);
            SceneLighting without;
            plain_field.bind(without, params, 1, 1.f);
            undeclared_ok = undeclared_ok && (without.lit_range.memory_brightness == 1.f);
            zero_ok = zero_ok && ((strength > 0.f) ? without.lit_range.enabled : !without.lit_range.enabled);
            plain_field.release();
        }
        {
            // 倍率が 1 なら、申告があっても何も結ばない（強さ 0 のとき）。
            LightRangeParams params;
            params.strength = 0.f;
            params.memory_brightness = 1.f;
            LitRangeField field;
            mem_frame.lighting.memory_dim = 1;
            field.update(mem_frame, 0.016f, params);
            SceneLighting probe;
            field.bind(probe, params, 1, 1.f);
            no_texture_ok = !probe.lit_range.enabled;
            field.release();
        }
        check(declared_ok, "bind: 申告があれば、記憶のマスの倍率が運ばれ、光の範囲の強さが 0 でも場が有効");
        check(undeclared_ok && zero_ok, "bind: 申告が無ければ倍率は 1 で、強さ 0 なら何も結ばない（従来どおり）");
        check(no_texture_ok, "bind: 倍率が 1 で光の範囲の強さも 0 なら、申告があっても何も結ばない");
    }

    // GL のエラーが出ていない（GLES の「同じ単位に型違いのサンプラ」は、ここで GL_INVALID_OPERATION になる）。
    {
        const std::string gl_err = drain_gl_errors();
        check(gl_err.empty(), "描く間に GL エラーが出ていない" + (gl_err.empty() ? std::string() : (": " + gl_err)));
    }

    /* --- 絵を書き出す（2 つのシーン × 光源の種類 5 つ + 場なし + 切り替えの途中） --- */
    if (!options.light_range_check_path.empty()) {
        std::string stem = options.light_range_check_path;
        for (Scene &s : scenes) {
            const auto save = [&](const std::string &suffix, const Shot &shot) {
                Shot pic = shot;
                pic.pictures = true;
                (void)shoot(s, pic);
                const std::string path = stem + "_" + s.name + suffix + ".png";
                std::vector<unsigned char> pixels(static_cast<std::size_t>(screen_w) * static_cast<std::size_t>(screen_h) * 4u);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glReadPixels(0, 0, screen_w, screen_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (std::size_t i = 3; i < pixels.size(); i += 4) {
                    pixels[i] = 255;
                }
                const bool ok = save_rgba_png(path, pixels.data(), screen_w, screen_h);
                check(ok, "絵を書き出せた: " + path);
            };
            Shot none;
            none.kind = 1;
            none.mode = 0;
            save("_off", none); // 場なし（従来の絵）
            for (const int kind : { 0, 1, 2, 3, 4, 5 }) {
                Shot on;
                on.kind = kind;
                on.mode = 1;
                save("_kind" + std::to_string(kind), on);
            }
            Shot dark_none;
            dark_none.kind = 5;
            dark_none.mode = 0;
            save("_kind5_off", dark_none); // 暗黒側の、場なし（範囲の外の暗さの基準）
            Shot mid;
            mid.kind = 1;
            mid.mode = 2;
            save("_fade_mid", mid);
            Shot mem;
            mem.kind = 1;
            mem.mode = 1;
            mem.memory = 1;
            save("_memory", mem); // 記憶のマス（申告あり）。遠いマスが少し暗い
            Shot mem_undeclared = mem;
            mem_undeclared.memory = 2;
            save("_memory_undeclared", mem_undeclared); // 申告なし（光の範囲だけの絵と同じ）
        }
    }

    post.shutdown();
    shadow.shutdown();
    library.release_gpu(renderer);
    pillar_model.shutdown();
    sprites.shutdown();
    if (sprite_atlas != 0) {
        glDeleteTextures(1, &sprite_atlas);
    }
    release_lit_placeholder();
    renderer.release(wall_gpu);
    renderer.release(floor_gpu);
    renderer.release(board_gpu);
    renderer.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    std::fprintf(stderr, "[hd2d] light-range-check: PASS %d / FAIL %d\n", passes, failures);
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

} // namespace hd2d
