/*!
 * @file view_modes.cpp
 * @brief 人が目で見るモード（`--prefab-view` / `--town-view`）。検査ではない。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * **遊ぶときには 1 行も通らない。** 宣言は `app/hd2d_checks.h`。
 * 振る舞いが変わっていないことは `python tools/hd2d_verify/golden.py --check` で見る。
 */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX //!< これが無いと windows.h の min/max マクロが `std::min` を壊す
#endif
#include <windows.h> //!< `MessageBoxA`（読めなかった理由を窓で出す）
#endif

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

//! `--prefab=` の画面。カメラは固定（矢印で回せるが既定値から始まる）。
int run_prefab_view(const AppOptions &options)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
#if defined(_WIN32)
        (void)::MessageBoxA(nullptr, "SDL の初期化に失敗しました。", "Hengband HD2D", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "[hd2d] SDL の初期化に失敗しました: %s\n", SDL_GetError());
#endif
        return 1;
    }
    Window window;
    std::string err;
    if (!create_window(options, window, err)) {
        show_message(err);
        SDL_Quit();
        return 1;
    }

    TextOverlay text;
    VoxelRenderer renderer;
    if (!text.init(kTextPx, err) || !renderer.init(err)) {
        show_message(err);
        SDL_Quit();
        return 1;
    }
    /*
     * 素材見物にもポスト処理を通す（P7）。**通さないと絵が白く飛ぶ**（物のシェーダが
     * 線形の HDR を書くようになったので、トーンマップはここにしか無い）。
     * 引き継ぎ §4 の「ブルームやトーンカーブで**草の揺れが読めなくなっていないか**を
     * 並べて確かめること（`--prefab=grass --field=16` が使える）」も、この口が要る。
     */
    PostChain post;
    const PostFlags post_flags = resolve_post_flags(options);
    if (!init_post_chain(post, options, err)) {
        show_message("ポスト処理を用意できませんでした:\n" + err);
        SDL_Quit();
        return 1;
    }
    /*
     * 素材見物にも影を入れる（P6 の検証「**影が本体と一致して動くこと**」）。
     * P5 までは「形と色だけを見る手段」として影を切っていたが、動くものが入った以上、
     * **影が付いて回るかを見られる場所が要る。**世界にプレハブが出るのは P10 なので、
     * いまそれを確かめられるのはここだけである。
     */
    ShadowMap shadow;
    if (!shadow.init(kShadowMapSide, err)) {
        std::fprintf(stderr, "[hd2d] 影を用意できませんでした（影なしで続けます）: %s\n", err.c_str());
    }
    // 影を落とす先の地面。プレハブの足元に敷く。
    Prefab ground_prefab = make_box_prefab("ground", 32, 32, 2, -2, 32);
    GpuPrefab ground_gpu;
    if (!renderer.upload(ground_prefab, ground_gpu, err)) {
        show_message("地面を GPU へ載せられませんでした:\n" + err);
        SDL_Quit();
        return 1;
    }

    //! **面のディテール**（`--wear=` / `--leaf=`）。1 本だけ撮り比べる手段。
    apply_detail_options(options, renderer);

    Prefab prefab;
    if (!load_prefab(options.voxel_dir, options.prefab_name, prefab, err)) {
        show_message("プレハブを読めませんでした:\n" + err);
        SDL_Quit();
        return 1;
    }
    GpuPrefab gpu;
    if (!renderer.upload(prefab, gpu, err)) {
        show_message("プレハブを GPU へ載せられませんでした:\n" + err);
        SDL_Quit();
        return 1;
    }

    Vec3 bounds_min{};
    Vec3 bounds_max{};
    prefab_bounds(prefab, bounds_min, bounds_max);
    const Vec3 extent = bounds_max - bounds_min;
    /*
     * 地面タイルの素材（天面が z<=0）は、見物用の下敷きと同じ高さに来る。
     * そのまま重ねると z-fight で見えない（P10 で床・水・溶岩を作って気づいた）。
     * **そのマスの下敷きだけ抜き、周囲のリングは残す**。抜いた穴に素材が収まるので、
     * 「水面が床より低い」のような段差も隣との関係として見える。
     */
    const bool prefab_is_ground = bounds_max.z <= 0.05f;
    const int hole_x0 = static_cast<int>(std::floor(bounds_min.x));
    const int hole_x1 = static_cast<int>(std::ceil(bounds_max.x));
    const int hole_y0 = static_cast<int>(std::floor(bounds_min.y));
    const int hole_y1 = static_cast<int>(std::ceil(bounds_max.y));

    /*
     * --- 並べて見る（`--field=N`）---
     * 1 個だけ見ても「隣とどう見えるか」は分からない。**風がどう見えるかは
     * 隣との関係で決まる**（設計書 §8.1 が世界座標から位相を作っている理由そのもの）。
     *
     * 間隔はプレハブの大きさから決める。草（1 マス）は隙間なく、木（2.4 マス）は 3 マス。
     * 位置と大きさと色を種でわずかに散らすのは、**同じ形が整列していると
     * 揺れの違いより並びのほうが目につく**ため（§9.3 と同じ考え方の簡略版）。
     */
    const int field = std::max(1, options.field);
    /*
     * 間隔。**風で曲がるものは重ねて敷き、剛体は間を空ける。**
     * 草を 1 マス間隔で置くと株の間に地面が見えて「並べた鉢植え」になり、
     * 野原としての揺れ方が判断できない。木は逆に、重なると幹が刺さって見える。
     */
    bool prefab_has_wind = false;
    for (const auto &part : prefab.parts) {
        prefab_has_wind = prefab_has_wind || (part.motion.kind == MotionKind::Wind);
    }
    const float density = prefab_has_wind ? 0.55f : 1.20f;
    const float step = std::max(0.35f, std::max(extent.x, extent.y) * density);
    std::vector<InstanceData> field_instances;
    if (field > 1) {
        const float half = static_cast<float>(field - 1) * 0.5f;
        for (int j = 0; j < field; ++j) {
            for (int i = 0; i < field; ++i) {
                // 位置から引く決定的な擬似乱数（走査順に依存しない。§9.3）。
                auto jitter = [&](int salt) {
                    const std::uint32_t h = (static_cast<std::uint32_t>(i) * 73856093u)
                        ^ (static_cast<std::uint32_t>(j) * 19349663u) ^ (static_cast<std::uint32_t>(salt) * 83492791u);
                    return static_cast<float>((h >> 8) & 0xFFFFu) / 65535.f;
                };
                InstanceData one;
                one.x = (static_cast<float>(i) - half) * step + ((jitter(1) - 0.5f) * step * 0.35f);
                one.y = (static_cast<float>(j) - half) * step + ((jitter(2) - 0.5f) * step * 0.35f);
                const float scale = 0.82f + (jitter(3) * 0.36f);
                one.sx = one.sy = scale;
                one.sz = 0.80f + (jitter(4) * 0.45f);
                const float shade = 0.86f + (jitter(5) * 0.24f);
                one.r = shade;
                one.g = shade * (0.94f + (jitter(6) * 0.12f));
                one.b = shade * (0.92f + (jitter(7) * 0.10f));
                field_instances.push_back(one);
            }
        }
        // 並べたぶんだけ見る範囲を広げる。
        const float span = static_cast<float>(field - 1) * step * 0.5f;
        bounds_min.x -= span;
        bounds_min.y -= span;
        bounds_max.x += span;
        bounds_max.y += span;
    }

    const Vec3 center{ (bounds_min.x + bounds_max.x) * 0.5f, (bounds_min.y + bounds_max.y) * 0.5f,
        (bounds_min.z + bounds_max.z) * 0.5f };
    const Vec3 view_extent = bounds_max - bounds_min;
    const float radius = 0.5f * std::sqrt(dot(view_extent, view_extent));

    /*
     * カメラの既定値。**P2 で本物のカメラを入れるときに改めて詰める**（計画 §4-1 で
     * 人に聞くと決めてある項目）。ここは「1 個を眺める」ための値でしかない。
     */
    /*
     * **`HD2D_PREFAB_AZIMUTH`（度）で始まりの向きを決められる。**
     *
     * 既定の 0.9rad は「1 個を斜めから眺める」向きで、**世界のカメラが見る面ではない**
     * （世界のカメラは注視点の +y から見る＝`camera.cpp` の `eye()`）。素材の南面だけに
     * 意匠を持たせたもの（扉の引き手・岩に開いた口）を既定の向きで撮ると**裏側が写る**ので、
     * 出来を判断できない。世界と同じ向きで撮りたいときは `90` を渡す。
     * 矢印キーで回せるのは従来どおりで、ここは**その初期値**だけを動かす。
     */
    float azimuth = 0.9f;
    if (const char *const az = std::getenv("HD2D_PREFAB_AZIMUTH")) {
        if (az[0] != '\0') {
            azimuth = static_cast<float>(std::atof(az)) * 0.0174533f;
        }
    }
    /*
     * 並べたときは**低く構える。**揺れは水平方向なので、見下ろすほど動きが画面上で
     * 縮んで見える（真上からだと揺れがほとんど分からない）。
     */
    float elevation = (options.field > 1) ? 0.30f : 0.62f;
    float fov_x = 0.62f; //!< 約 35 度。望遠寄りにすると垂直線の傾きが減る（設計書 §4.3）

    int screen_w = options.window_w;
    int screen_h = options.window_h;
    SDL_GetWindowSize(window.window, &screen_w, &screen_h);

    // 外接球がちょうど入る距離。**縦横の狭い方**で決めないと画面外へはみ出す。
    auto framing_distance = [&](float fov, int w, int h) {
        const float half_x = fov * 0.5f;
        const float half_y = std::atan(std::tan(half_x) * static_cast<float>(h) / static_cast<float>(std::max(1, w)));
        return (radius / std::sin(std::min(half_x, half_y))) * 1.08f;
    };
    float distance = framing_distance(fov_x, screen_w, screen_h);
    Uint64 fps_mark = SDL_GetTicks64();
    int fps_frames = 0;
    double fps = 0.0;
    int frame_index = 0;
    //! `--shot=` はここまで描いてから撮る（最初の 1 枚は文字アトラスが焼けていない）。
    constexpr int kShotAtFrame = 8;

    /*
     * --- 動き（P6）---
     * **`--shot` のときは時刻を引数で固定する。**進む時刻のまま撮ると、同じコマンドで
     * 撮った 2 枚が違う絵になり、「変わったのは直したからか、撮った時刻が違うからか」が
     * 分からなくなる（比較のための撮影が比較にならない）。
     */
    bool motion_running = options.shot_path.empty();
    float motion_time = options.motion_time;
    //! 連番撮影（`--motion-frames=`）。撮った枚数。
    int captured = 0;
    const int capture_total = std::max(1, options.motion_frames);
    bool wind_on = true;
    /*
     * 風の位相の作り方（`HD2D_WIND_PHASE=0|1|2`、実行中は `P` キー）。
     * **設計書 §8.1 の答えは 0（波）。**1（同期）と 2（ばらばら）は見比べるための手段である。
     */
    WindPhase wind_phase = [] {
        const char *const raw = std::getenv("HD2D_WIND_PHASE");
        const int value = (raw != nullptr) ? std::atoi(raw) : 0;
        return static_cast<WindPhase>(std::clamp(value, 0, 2));
    }();
    float door_target = 0.f;
    float door_open = 0.f;
    Uint64 last_tick = SDL_GetTicks64();
    std::vector<Mat4> part_motions;

    /*
     * **わざと §8.2-1 を破る口。**`HD2D_BREAK_SHADOW_MOTION=1` を立てると、
     * 影のパスにだけ「動く前の姿勢」と「無風」を渡す。設計書 §8.2 が
     * 「忘れると『揺れているのに影が揺れない』」と言っているそのものを再現する。
     *
     * これが無いと、影が本体に付いて回っている絵を見ても
     * **「そもそも影が動く仕組みなど無い」場合と見分けがつかない。**
     * 検査を書いたら壊れた入力で反応することを確かめる（§14-4）の、絵の側の版である。
     */
    const bool break_shadow_motion = [] {
        const char *const raw = std::getenv("HD2D_BREAK_SHADOW_MOTION");
        return (raw != nullptr) && (raw[0] != '\0') && (raw[0] != '0');
    }();
    if (break_shadow_motion) {
        std::fprintf(stderr, "[hd2d] HD2D_BREAK_SHADOW_MOTION: **影のパスにだけ動きを渡しません**"
                             "（設計書 §8.2-1 をわざと破る口）\n");
    }
    std::vector<Mat4> still_motions;

    for (bool running = true; running;) {
        ++frame_index;
        {
            const Uint64 now = SDL_GetTicks64();
            const float dt = std::min(0.1f, static_cast<float>(now - last_tick) / 1000.f);
            last_tick = now;
            if (motion_running) {
                motion_time += dt;
            }
            if (capture_total > 1) {
                /*
                 * 連番撮影中は**時刻を実時間から切り離す。**フレームにかかった時間で
                 * 進めると、コマの間隔が撮影機の都合で揺れて GIF がぎくしゃくする。
                 */
                motion_time = options.motion_time
                    + (options.motion_span * static_cast<float>(captured) / static_cast<float>(capture_total));
            }
            // 扉は**状態そのものをなめらかに**する（`part_motion.cpp` の注記）。
            const float door_step = dt * 2.5f;
            door_open += std::clamp(door_target - door_open, -door_step, door_step);
        }
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if ((event.type == SDL_WINDOWEVENT) && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                screen_w = event.window.data1;
                screen_h = event.window.data2;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_LEFT:
                    azimuth -= 0.08f;
                    break;
                case SDLK_RIGHT:
                    azimuth += 0.08f;
                    break;
                case SDLK_UP:
                    elevation = std::min(elevation + 0.05f, 1.50f);
                    break;
                case SDLK_DOWN:
                    elevation = std::max(elevation - 0.05f, -0.20f);
                    break;
                case SDLK_EQUALS:
                case SDLK_KP_PLUS:
                    distance = std::max(distance * 0.92f, radius * 0.6f);
                    break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:
                    distance = std::min(distance * 1.08f, radius * 12.f);
                    break;
                case SDLK_LEFTBRACKET:
                    fov_x = std::max(fov_x - 0.05f, 0.15f);
                    break;
                case SDLK_RIGHTBRACKET:
                    fov_x = std::min(fov_x + 0.05f, 1.60f);
                    break;
                /*
                 * P6 の口。動きを**止めて／進めて**見られないと、
                 * 「揺れているのに影が揺れない」のような症状を目で追えない。
                 */
                case SDLK_SPACE:
                    motion_running = !motion_running;
                    break;
                case SDLK_PERIOD:
                    motion_time += 0.05f; // 1 コマ送り（止めているときに使う）
                    break;
                case SDLK_COMMA:
                    motion_time -= 0.05f;
                    break;
                case SDLK_w:
                    wind_on = !wind_on; //!< 風の入切（`flex` が効いているかを見る）
                    break;
                case SDLK_p:
                    // 位相の作り方を回す。**並べたとき（`--field=`）に見比べるための手段。**
                    wind_phase = static_cast<WindPhase>((static_cast<int>(wind_phase) + 1) % 3);
                    break;
                case SDLK_o:
                    // 状態連動（扉）。0 ⇄ 1 をなめらかに動かす（`part_motion.cpp` の注記）。
                    door_target = (door_target > 0.5f) ? 0.f : 1.f;
                    break;
                default:
                    break;
                }
            }
        }

        const Vec3 eye{ center.x + (distance * std::cos(elevation) * std::cos(azimuth)),
            center.y + (distance * std::cos(elevation) * std::sin(azimuth)),
            center.z + (distance * std::sin(elevation)) };
        const Mat4 view = look_at(eye, center, Vec3{ 0.f, 0.f, 1.f });
        const float z_near = std::max(1.f, distance - (radius * 2.f));
        const float z_far = distance + (radius * 4.f);
        const Mat4 projection = perspective_horizontal(fov_x,
            static_cast<float>(screen_w) / static_cast<float>(std::max(1, screen_h)), z_near, z_far);
        if (!post.resize(screen_w, screen_h, err)) {
            show_message("画面の FBO を作れませんでした:\n" + err);
            running = false;
            break;
        }

        // 動き（P6）。剛体は CPU で行列に、風は頂点シェーダに。
        MotionContext motion;
        motion.time = motion_time;
        motion.states.emplace_back("door", door_open);
        compose_part_motions(prefab, motion, part_motions);
        WindParams wind;
        if (wind_on) {
            wind.dir_x = 0.92f;
            wind.dir_y = 0.39f;
            wind.amplitude = 0.10f; //!< マス。0.1 マス＝3 ボクセルぶん
            wind.phase = wind_phase;
        }

        /*
         * 素材を照らす光。**影が開けた地面に落ちる向きへ置く。**
         * 真上だと影が足元に隠れ、カメラと同じ側から照らすと物の裏へ回って見えない。
         * 既定のカメラは方位 0.9rad（＝ +x +y の側）にいるので、画面の左手前へ影が伸びる
         * 「+x -y から照らす」を採る。**動く影を見るための手段なので、見えなければ意味が無い。**
         */
        SceneLighting prefab_light;
        prefab_light.sun_dir = normalize(Vec3{ 0.50f, -0.35f, 0.79f });
        prefab_light.shadow_strength = 0.85f;
        const ShadowFit fit = fit_shadow_bounds(bounds_min.x - 1.f, bounds_max.x + 3.f,
            bounds_min.y - 1.f, bounds_max.y + 3.f, bounds_max.z, prefab_light.sun_dir, shadow.side());

        // 地面。プレハブの足元を覆うだけ並べる。**地面タイルの素材のマスは抜く**（上の注記）。
        std::vector<InstanceData> ground;
        for (int gy = static_cast<int>(std::floor(bounds_min.y)) - 2; gy <= static_cast<int>(std::ceil(bounds_max.y)) + 2; ++gy) {
            for (int gx = static_cast<int>(std::floor(bounds_min.x)) - 2; gx <= static_cast<int>(std::ceil(bounds_max.x)) + 2; ++gx) {
                if (prefab_is_ground && (gx >= hole_x0) && (gx < hole_x1) && (gy >= hole_y0) && (gy < hole_y1)) {
                    continue;
                }
                InstanceData slab;
                slab.x = static_cast<float>(gx);
                slab.y = static_cast<float>(gy);
                slab.r = slab.g = slab.b = 0.62f;
                ground.push_back(slab);
            }
        }

        // (a) 影のパス。**本描画とまったく同じ動きを渡す**（§8.2-1）。
        if (shadow.ready()) {
            shadow.begin();
            renderer.begin_motion(break_shadow_motion ? WindParams{} : wind,
                break_shadow_motion ? 0.f : motion_time);
            still_motions.assign(prefab.parts.size(), Mat4::identity());
            const Mat4 *shadow_motions = break_shadow_motion ? still_motions.data() : part_motions.data();
            renderer.draw_instanced_depth(ground_gpu, fit.view_projection, ground.data(), ground.size());
            if (field > 1) {
                renderer.draw_instanced_depth(gpu, fit.view_projection, field_instances.data(),
                    field_instances.size(), shadow_motions, prefab.parts.size());
            } else {
                renderer.draw_depth_single(gpu, fit.view_projection, shadow_motions, prefab.parts.size());
            }
            shadow.end(screen_w, screen_h);
        }

        // (b) 本描画。**画面用の FBO へ**（P7）。
        post.begin_scene(0.10f, 0.11f, 0.13f);
        renderer.begin(projection * view, prefab_light, fit.view_projection, shadow.texture(), shadow.side());
        renderer.begin_motion(wind, motion_time);
        renderer.begin_cutaway(Cutaway{}); //!< 素材見物に遮蔽は無い（半径 0 ＝ 無効）
        renderer.draw_instanced(ground_gpu, ground.data(), ground.size());
        if (field > 1) {
            renderer.draw_instanced(gpu, field_instances.data(), field_instances.size(),
                part_motions.data(), part_motions.size());
        } else {
            renderer.draw(gpu, part_motions.data(), part_motions.size());
        }
        post.end_scene(screen_w, screen_h);

        // (c) ポスト処理 → 既定のフレームバッファ。**文字はこの後**（UI はぼかさない）。
        post.resolve(post_flags, PostParams{}, z_near, z_far, distance, screen_w, screen_h);

        text.begin(screen_w, screen_h);
        char buf[256]{};
        std::snprintf(buf, sizeof(buf), "prefab=%s  parts=%zu  quads=%zu  triangles=%zu  naive faces=%zu  atlas=%dx%d",
            prefab.name.c_str(), prefab.parts.size(), gpu.quad_count, gpu.triangle_count,
            gpu.naive_face_count, gpu.atlas_side, gpu.atlas_side);
        text.draw_cell(8, 8, 0, 0, buf, kHeaderColor);
        std::snprintf(buf, sizeof(buf), "size=%.1fx%.1fx%.1f cells  footprint=%zu cells  fps=%.1f",
            extent.x, extent.y, extent.z, prefab.footprint.size(), fps);
        text.draw_cell(8, 8, 0, 1, buf, kDimColor);
        std::snprintf(buf, sizeof(buf), "azimuth=%.2f elevation=%.2f distance=%.0f fov_x=%.0f deg",
            azimuth, elevation, distance, fov_x * 57.2958f);
        text.draw_cell(8, 8, 0, 2, buf, kDimColor);
        // 動き（P6）。**どのパーツがどう動く約束なのか**まで出す（.jsonc の読み違いを目で捕まえる）。
        {
            std::string motions;
            for (const auto &part : prefab.parts) {
                if (!motions.empty()) {
                    motions += " ";
                }
                motions += part.name + ":" + motion_kind_name(part.motion.kind);
                if (part.motion.kind == MotionKind::Wind) {
                    char k[32]{};
                    std::snprintf(k, sizeof(k), "(k=%.1f)", part.wind_k);
                    motions += k;
                }
            }
            static const char *const kPhaseName[3] = { "波(設計書)", "同期", "ばらばら" };
            std::snprintf(buf, sizeof(buf), "motion t=%.2fs %s  wind=%s 位相=%s  field=%dx%d(%zu)  door=%.2f   %s",
                motion_time, motion_running ? "▶" : "■", wind_on ? "on" : "off",
                kPhaseName[static_cast<int>(wind_phase)], field, field, field_instances.size(),
                door_open, motions.c_str());
            text.draw_cell(8, 8, 0, 3, buf, kDimColor);
        }
        text.draw_cell(8, 8, 0, 4,
            "[P6] 矢印=回す  +/-=寄る引く  [ ]=画角  空白=停止  , .=コマ送り  W=風  P=位相  O=扉  ESC=終了",
            kDimColor);
        text.flush();

        // 撮るのは **swap の前**（読むのは裏の面。表を読むと何が入っているか保証が無い）。
        if (!options.shot_path.empty() && (frame_index >= kShotAtFrame)) {
            if (capture_total <= 1) {
                (void)save_framebuffer_bmp(options.shot_path, screen_w, screen_h);
                running = false;
            } else {
                // 連番。`out.bmp` → `out_000.bmp`。
                const std::size_t dot = options.shot_path.find_last_of('.');
                const std::string stem = (dot == std::string::npos) ? options.shot_path : options.shot_path.substr(0, dot);
                const std::string ext = (dot == std::string::npos) ? std::string(".bmp") : options.shot_path.substr(dot);
                char suffix[16]{};
                std::snprintf(suffix, sizeof(suffix), "_%03d", captured);
                (void)save_framebuffer_bmp(stem + suffix + ext, screen_w, screen_h);
                ++captured;
                if (captured >= capture_total) {
                    std::fprintf(stderr, "[hd2d] %d 枚撮りました（%.3f 秒ぶん）: %s_000%s ...\n",
                        captured, options.motion_span, stem.c_str(), ext.c_str());
                    running = false;
                }
            }
        }
        SDL_GL_SwapWindow(window.window);

        ++fps_frames;
        const Uint64 now = SDL_GetTicks64();
        if ((now - fps_mark) >= 500) {
            fps = (static_cast<double>(fps_frames) * 1000.0) / static_cast<double>(now - fps_mark);
            fps_frames = 0;
            fps_mark = now;
        }
    }

    post.shutdown();
    shadow.shutdown();
    renderer.release(ground_gpu);
    renderer.release(gpu);
    renderer.shutdown();
    text.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    return 0;
}

/*!
 * @brief `--town-view`: **実データの町をその場で描く**（コアは起こさない）。
 *
 * @details 町ごとの意匠（`TownStyle`）を自分で見るために足した（P10 第 2 期
 * レビュー 3）。**辺境の地以外の町へ実機で行く道が無い**（`lib/save/PLAYER` は辺境の地に
 * いて、`^A` はスクリプトでは効かない＝罠 69）ので、これが無いとテルモラとモリバントの絵が
 * 1 枚も出せない。
 *
 * 見るのは `--town-check` と同じ材料（`load_town_frame` → 意味づけ → 敷地の分解）で、
 * 違いは**描くこと**だけである。検査ではないので RESULT は出さない。
 *
 * ```
 * .\HengbandHd2d.exe --town-view --town-data=lib\edit\towns\03_Morivant.txt \
 *     --town-at=120,40 --windowed=1600x900 --shot=shots\p10c\mor.bmp
 * ```
 */
int run_town_view(const AppOptions &options)
{
    const std::string path = options.town_data_path.empty()
        ? std::string("lib/edit/towns/01_Outpost_Full.txt")
        : options.town_data_path;
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
    if (!renderer.init(err) || !shadow.init(kShadowMapSide, err) || !init_post_chain(post, options, err)
        || !post.resize(options.window_w, options.window_h, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    /*
     * **面の汚し**（`render/surface_wear.h`）。ここは cfg を読まない道なので、
     * `--wear=` を打たれたときだけ掛ける（打たなければ以前の絵と 1 ビットも同じ）。
     * **町の絵はこの道でしか出せない**（罠 69）ので、撮り比べる手段がここに要る。
     */
    /*
     * **面のディテール**（`--wear=` / `--leaf=`）。ここは cfg を読まない道なので、
     * 打たれたときだけ掛ける。**町の絵はこの道でしか出せない**（罠 69）ので、
     * 撮り比べる手段がここに要る。
     */
    apply_detail_options(options, renderer);
    //! 埃（`--dust=`）。失敗しても続ける（雲と同じ扱い）。
    DustMotes town_motes;
    if (options.dust > 0.f) {
        std::string dust_err;
        if (!town_motes.init(dust_err)) {
            std::fprintf(stderr, "[hd2d] 埃を用意できませんでした: %s\n", dust_err.c_str());
        }
    }
    if (options.sepia > 0.f) {
        //! **セピアは LUT の側**なので、ここで 1 度焼く（`apply_color_options` は送るだけ）。
        std::string lut_err;
        if (!post.set_grade(sepia_grade(GradeParams{}, options.sepia), lut_err)) {
            std::fprintf(stderr, "[hd2d] セピアの LUT を焼けませんでした: %s\n", lut_err.c_str());
        }
    }

    Prefab wall_prefab = make_box_prefab("wall", 32, 32, 32, 0, 32);
    Prefab floor_prefab = make_box_prefab("floor", 32, 32, 2, -2, 32);
    GpuPrefab wall_gpu;
    GpuPrefab floor_gpu;
    PrefabLibrary library;
    if (!renderer.upload(wall_prefab, wall_gpu, err) || !renderer.upload(floor_prefab, floor_gpu, err)
        || !library.load(options.voxel_dir, err) || !library.upload_gpu(renderer, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    GameFrame frame;
    if (!load_town_frame(path, library, frame, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    //! 夜の町（街灯と窓）を見るための手段。`HD2D_FORCE_TIME=20:30` で日没後にできる。
    {
        const int forced = forced_day_minute();
        if (forced >= 0) {
            frame.lighting.day_minute = forced;
            frame.lighting.daytime = (forced >= (6 * 60)) && (forced < (18 * 60));
        }
    }
    /*
     * **別コアの町を合成フレームで見るオプション**（2026-08-18）。実機では握手の `core_name` で
     * `town_styles.jsonc` を読むが、ここはコアを起こさないのでコア名が無い——つまり
     * 幻想蛮怒の意匠（`_jin` や大門）を通す道が無かった。`HD2D_TOWN_CORE=gensoband` で
     * 表を読ませる（未設定なら従来どおりべた書きの表＝変愚の絵は 1 ボクセルも動かない）。
     */
    load_styles_for_check(options);
    FloorMeaning meaning;
    TownPlan town;
    const std::vector<std::uint8_t> entrances = town_entrance_mask(frame, library);
    if (!rebuild_floor_meaning(frame, meaning) || !rebuild_town_plan(meaning, town, nullptr, &entrances)) {
        std::fprintf(stderr, "[hd2d] 町を読めませんでした: %s\n", path.c_str());
        SDL_Quit();
        return 1;
    }
    /*
     * 見る場所。指定が無ければ**門のある敷地がいちばん混んでいる所**を選ぶ（町の外れの
     * 野原に降ろされると「何も変わっていない」に見えるので、既定が効く）。
     */
    int look_x = options.town_at_x;
    int look_y = options.town_at_y;
    if ((look_x < 0) || (look_y < 0)) {
        int best = -1;
        for (const TownSite &site : town.sites) {
            if (!site.has_gate) {
                continue;
            }
            //! `near` という名前は使えない（MSVC の古いメモリモデルのマクロ。罠 22 の仲間）。
            int crowd = 0;
            for (const TownSite &other : town.sites) {
                const int dx = other.x0 - site.x0;
                const int dy = other.y0 - site.y0;
                crowd += (((dx * dx) + (dy * dy)) < (18 * 18)) ? 1 : 0;
            }
            if (crowd > best) {
                best = crowd;
                look_x = site.gate_x;
                look_y = site.gate_y + 3;
            }
        }
    }
    if ((look_x < 0) || (look_y < 0)) {
        look_x = town.width / 2;
        look_y = town.height / 2;
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
    //! `--turn=0..7`（1 段 45°）。町の絵も回した向きで撮れるようにする（2026-09-06）。
    camera.yaw = view_turn_yaw(options.camera_turn);
    camera.target = Vec3{ static_cast<float>(look_x) + 0.5f, static_cast<float>(look_y) + 0.5f, 0.f };
    /*
     * **一人称でも撮れるようにしておく**（`--fps` / `--fps=<度>`）。
     * 見下ろしのカメラは必ず北を向いているので、南を向いた看板は**表しか写らない**。
     * 裏を確かめたい（2026-08-11 に決めた「看板は裏面もアイコンを描画して」）ときに、
     * セーブを用意して実際に町を歩く以外の道が無いのは高すぎる。
     */
    FpsMode town_fps;
    //! 見下ろしの戻り先＝`--turn=` の角（`apply()` がここから `camera.yaw` を作る）。
    town_fps.base_yaw = view_turn_yaw(options.camera_turn);
    town_fps.set_active(options.first_person);
    if (options.first_person) {
        town_fps.facing = options.first_person_yaw_deg * 0.0174533f;
        town_fps.yaw = town_fps.facing;
    }
    town_fps.apply(camera);

    TerrainMemory memory;
    memory.update(frame);
    library.update_motions(options.motion_time); //!< 撮るときは時刻を固定する（罠 14）
    const float night = lamp_night_factor(frame.lighting);
    TerrainView terrain;
    build_terrain_view(meaning, Frustum::from(camera.view_projection()), terrain, &memory, &library, &town, night);

    SceneLighting lighting = make_scene_lighting(frame.lighting, frame.floor);
    //! 覆いの町の中の自然光（本編と同じ。オレンジに振って底を持ち上げる）。
    if (terrain.covered) {
        lighting.sky_color = Vec3{ 0.88f, 0.62f, 0.40f };
        lighting.bounce_color = Vec3{ 1.00f, 0.70f, 0.46f };
        lighting.ambient_scale = std::max(lighting.ambient_scale, 1.15f);
    }
    (void)collect_point_lights(frame, camera.target, lighting);
    //! 祠のライトアップとガス灯（本編と同じ合流。覆いの町は昼も点し、近い順に枠へ足す）。
    {
        const float wish_night = terrain.covered ? std::max(night, 0.85f) : night;
        if ((wish_night > 0.02f) && !terrain.lights.empty()) {
            std::vector<const TerrainViewLight *> wishes;
            wishes.reserve(terrain.lights.size());
            for (const TerrainViewLight &wish : terrain.lights) {
                wishes.push_back(&wish);
            }
            std::sort(wishes.begin(), wishes.end(),
                [&camera](const TerrainViewLight *a, const TerrainViewLight *b) {
                    const float da = ((a->x - camera.target.x) * (a->x - camera.target.x))
                        + ((a->y - camera.target.y) * (a->y - camera.target.y));
                    const float db = ((b->x - camera.target.x) * (b->x - camera.target.x))
                        + ((b->y - camera.target.y) * (b->y - camera.target.y));
                    return da < db;
                });
            for (const TerrainViewLight *wish : wishes) {
                if (static_cast<int>(lighting.points.size()) >= kMaxPointLights) {
                    break;
                }
                PointLight point;
                point.x = wish->x;
                point.y = wish->y;
                point.z = wish->z;
                point.radius = wish->radius;
                point.r = wish->r;
                point.g = wish->g;
                point.b = wish->b;
                point.intensity = wish->intensity * wish_night;
                lighting.points.push_back(point);
            }
        }
    }
    //! 覆いの町（岩天井）では直方体を天井まで持ち上げる（本編ループと同じ理由）。
    const float town_shadow_height = terrain.covered ? std::max(kMaxPropHeight, terrain.cave_top) : kMaxPropHeight;
    const ShadowFit fit = fit_shadow(camera, lighting.sun_dir, town_shadow_height, shadow.side(), 0.f);
    /*
     * **覆いの町の穴**（2026-08-18 その3）。実機ではプレイヤの位置に開くが、ここには
     * プレイヤが居ないので**見る所（`--town-at`）に開ける**——開けないと黒い山の背しか
     * 写らず、意匠の確認という本来の用が果たせない。層 1（近くの遮蔽）＋層 2（屋根外し）
     * の二重は本編と同じ形。覆いの無い町は従来どおり穴なし（全景を見る手段）。
     */
    std::vector<Cutaway> town_cuts;
    //! 屋敷の中を見る所（デザイン4）。bbox の中の歩けるマスなら屋根の蓋を刳り抜く。
    bool manor_look = false;
    for (const auto &mbox : town.manors) {
        if ((look_x >= mbox[0]) && (look_x <= mbox[2]) && (look_y >= mbox[1]) && (look_y <= mbox[3])
            && (town.role_at(look_x, look_y) != TownRole::Manor)) {
            manor_look = true;
            break;
        }
    }
    if (terrain.covered || manor_look) {
        const Vec3 focus{ static_cast<float>(look_x) + 0.5f, static_cast<float>(look_y) + 0.5f, 0.55f };
        town_cuts.push_back(make_point_cutaway(camera, focus, options.cutaway_radius));
        //! 本編と同じ 3.0 倍（決めたこと その3「透過の範囲はいまの 1.5 倍に」）。
        Cutaway lid = make_point_cutaway(camera, focus, options.cutaway_radius * 3.0f);
        lid.roof_min_z = terrain.covered ? terrain.cave_min_z : 2.3f; //!< 内壁（2 マス）のすぐ上から
        lid.depth = 2.f;
        lid.power = 3.4f;
        town_cuts.push_back(lid);
    }
    const PostFlags post_flags = resolve_post_flags(options);
    float z_near = 0.f;
    float z_far = 0.f;
    camera.depth_range(z_near, z_far);

    std::fprintf(stderr, "[hd2d] town-view: %s\n", path.c_str());
    std::fprintf(stderr, "  %s\n", town_plan_summary(town).c_str());
    std::fprintf(stderr, "  意匠 \"%s\"／見る所 (%d,%d)／時刻 %02d:%02d\n",
        (town_style_for(town.identity.town_id).suffix[0] == '\0') ? "（辺境の地）"
                                                                 : town_style_for(town.identity.town_id).suffix,
        look_x, look_y, frame.lighting.day_minute / 60, frame.lighting.day_minute % 60);

    //! 4 枚回してから撮る（1 枚目はドライバの都合で空のことがある＝`--terrain-check` と同じ）。
    for (int i = 0; i < 4; ++i) {
        if (!post.resize(screen_w, screen_h, err)) {
            std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
            SDL_Quit();
            return 1;
        }
        shadow.begin();
        renderer.draw_instanced_depth(floor_gpu, fit.view_projection, terrain.slabs.data(), terrain.slabs.size());
        renderer.draw_instanced_depth(wall_gpu, fit.view_projection, terrain.boxes.data(), terrain.boxes.size());
        draw_library_depth(renderer, library, terrain, fit.view_projection);
        shadow.end(screen_w, screen_h);
        post.begin_scene(0.04f, 0.05f, 0.07f);
        renderer.begin(camera.view_projection(), lighting, fit.view_projection, shadow.texture(), shadow.side());
        //! 町の全景を見る手段なので**覆いの町以外は**抜かない（覆いは上の `town_cuts`）。
        renderer.begin_cutaways(town_cuts.data(), town_cuts.size());
        renderer.set_cutaway_scale(0.f); //!< 床の板は抜かない（本編と同じ）
        renderer.draw_instanced(floor_gpu, terrain.slabs.data(), terrain.slabs.size());
        renderer.set_shadow_scale(0.f);
        renderer.draw_instanced(floor_gpu, terrain.under_slabs.data(), terrain.under_slabs.size());
        renderer.set_shadow_scale(1.f);
        renderer.set_cutaway_scale(1.f);
        renderer.draw_instanced(wall_gpu, terrain.boxes.data(), terrain.boxes.size());
        draw_library_color(renderer, library, terrain);
        /*
         * 湖の霧（デザイン4。紅魔館）。実機と同じく不透明の後・`end_scene()` の前。
         * 合成フレームはコアを起こさないが、意匠の表（HD2D_TOWN_CORE）が高さを
         * 持てばマスが積まれているので、絵の確認がここでできる。
         */
        if (!terrain.mist_cells.empty() && (terrain.mist_height > 0.f)) {
            static CloudLayer town_mist;
            static bool mist_ready = false;
            if (!mist_ready) {
                std::string mist_err;
                mist_ready = town_mist.init(mist_err);
            }
            if (mist_ready) {
                SkyState mist_sky;
                mist_sky.visible = true;
                //! 合成フレームの時刻から太陽の高さを雑に引く（正午 = 真上）。
                const float t = static_cast<float>(frame.lighting.day_minute) / (24.f * 60.f);
                mist_sky.altitude = std::sin((t - 0.25f) * 2.f * 3.14159265f);
                mist_sky.horizon = lighting.sky_color;
                town_mist.draw_patch(camera.view_projection(), terrain.mist_cells,
                    terrain.mist_height, mist_sky);
            }
        }
        /*
         * **地表の霧**（デザイン8 その2。永遠亭の迷いの竹林）。空の雲の網を低い所へ
         * 敷く——合成フレームは時が止まっているので、**コマ番号を秒に推定て**流す。
         */
        if (terrain.ground_mist_height > 0.f) {
            static CloudLayer town_fog;
            static bool fog_ready = false;
            if (!fog_ready) {
                std::string fog_err;
                fog_ready = town_fog.init(fog_err);
            }
            if (fog_ready) {
                SkyState fog_sky;
                fog_sky.visible = true;
                const float t = static_cast<float>(frame.lighting.day_minute) / (24.f * 60.f);
                fog_sky.altitude = std::sin((t - 0.25f) * 2.f * 3.14159265f);
                fog_sky.horizon = lighting.sky_color;
                town_fog.draw_ground(camera.view_projection(), camera.eye(), fog_sky, 0.f,
                    terrain.ground_mist_height, terrain.ground_mist_alpha, 0.5f);
            }
        }
        /*
         * 配管の蒸気（デザイン7 その2）。合成フレームは時が止まっているので、
         * **コマ番号を秒に推定て**位相を進める（4 枚撮るうちの最後を保存するので、
         * 静止画でも噴き出しの途中が写る）。
         */
        if (!terrain.steam_jets.empty()) {
            static CloudLayer town_steam;
            static bool steam_ready = false;
            if (!steam_ready) {
                std::string steam_err;
                steam_ready = town_steam.init(steam_err);
            }
            if (steam_ready) {
                SkyState steam_sky;
                steam_sky.visible = true;
                const float t = static_cast<float>(frame.lighting.day_minute) / (24.f * 60.f);
                steam_sky.altitude = std::sin((t - 0.25f) * 2.f * 3.14159265f);
                steam_sky.horizon = lighting.sky_color;
                town_steam.draw_steam(camera.view_projection(), terrain.steam_jets, steam_sky,
                    0.7f * static_cast<float>(i));
            }
        }
        //! 後処理へ渡す値（埃の広がりも同じものから測る）。
        PostParams town_post_probe;
        apply_color_options(options, town_post_probe);
        /*
         * **空中に浮かぶ埃**（`--dust=`）。不透明の後・`end_scene()` の前
         * （線形の HDR へ加算するので、ブルームと被写界深度が後から掛かる）。
         */
        if (options.dust > 0.f) {
            DustParams town_dust;
            town_dust.amount = options.dust;
            const Vec3 dust_to_target = camera.target - camera.eye();
            //! 粒の広がりは後処理と同じ測りから（`measure_dof()`）。
            const DofView town_dof = measure_dof(town_post_probe, camera.view_projection(), camera.target.x,
                camera.target.y, dust_to_target.x, dust_to_target.y);
            town_motes.draw(camera.view_projection(), camera.eye(), lighting.sky_color,
                0.7f * static_cast<float>(i), town_dust, town_dof, camera.target, screen_h);
        }
        post.end_scene(screen_w, screen_h);
        const PostParams &town_post = town_post_probe;
        /*
         * **被写界深度に要るものを渡す**（2026-08-23）。ここは長らく視点行列も焦点も
         * 渡していなかったので、町の下見では被写界深度が 1 度も掛かっていなかった
         * （`--dof=` を打っても絵が 1 画素も変わらない）。埃のぼけ方を見る場所が要るので通した。
         */
        const Vec3 town_to_target = camera.target - camera.eye();
        post.resolve(post_flags, town_post, z_near, z_far, std::sqrt(dot(town_to_target, town_to_target)),
            screen_w, screen_h, 0, 0, camera.view_projection(), camera.target.x, camera.target.y,
            town_to_target.x, town_to_target.y);
        if (!options.shot_path.empty() && (i == 3)) {
            (void)save_framebuffer_bmp(options.shot_path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);
    }
    if (options.shot_path.empty()) {
        //! 撮らない指定なら窓を出したまま眺められるようにする（ESC か窓を閉じるまで）。
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event) != 0) {
                running = running && (event.type != SDL_QUIT)
                    && ((event.type != SDL_KEYDOWN) || (event.key.keysym.sym != SDLK_ESCAPE));
            }
            SDL_Delay(16);
        }
    }
    SDL_Quit();
    return 0;
}

} // namespace hd2d
