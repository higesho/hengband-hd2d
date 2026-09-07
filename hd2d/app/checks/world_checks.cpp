/*!
 * @file world_checks.cpp
 * @brief 世界と町の検査（`--world-check` / `--town-check`）。**窓も GL も要らない。**
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
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hd2d {

/*!
 * @brief `--world-check`: 種と配置の決定性（設計書 §9.3）。**窓も GL も要らない。**
 *
 * @details 見るのは 3 つ:
 * 1. **同じ階・同じ座標なら種が一致する**（何度引いても同じ）
 * 2. **カメラを動かしても配置が変わらない** — 別の場所から見た 2 回の結果を、
 *    両方に入っているマスについて突き合わせる
 * 3. **フロアが変われば必ず変わる** — これが無いと「いつも同じ値を返す壊れた種」でも 1・2 は通る
 *
 * 3 が**検査の検査**にあたる（設計書 §14-4）。
 */
int run_world_check(const AppOptions &options)
{
    const GameFrame frame = make_synthetic_frame(23, 19, 100, 44);
    FloorMeaning meaning;
    if (!rebuild_floor_meaning(frame, meaning)) {
        std::fprintf(stderr, "[hd2d] 意味づけを作れませんでした\n");
        return 1;
    }
    std::fprintf(stderr, "[hd2d] world-check: %s\n", floor_meaning_summary(meaning).c_str());

    int failures = 0;

    // (1) 同じ引数なら同じ種。
    {
        int mismatches = 0;
        for (int gy = 0; gy < meaning.height; ++gy) {
            for (int gx = 0; gx < meaning.width; ++gx) {
                if (cell_seed(gx, gy, meaning.identity) != cell_seed(gx, gy, meaning.identity)) {
                    ++mismatches;
                }
            }
        }
        std::fprintf(stderr, "  (1) 同じ座標で種が一致: mismatches=%d\n", mismatches);
        failures += (mismatches != 0) ? 1 : 0;
    }

    // (2) カメラを動かしても配置が変わらない。
    {
        Camera camera;
        camera.pitch = options.camera_pitch_deg * 0.0174533f;
        camera.fov_x = options.camera_fov_deg * 0.0174533f;
        camera.viewport_w = 1600;
        camera.viewport_h = 900;
        camera.distance = distance_for_cell_px(camera, options.camera_cell_px);

        /*
         * ライブラリと記憶も通す（P10）。**本番と同じ経路**で決定性を見る（罠 28 の教訓）。
         * ライブラリが読めない場合はここで FAIL する（assets/ は追跡されているので、読めない＝
         * 木が壊れている）。
         */
        PrefabLibrary library;
        TerrainMemory memory;
        std::string lib_err;
        const bool lib_ok = library.load(options.voxel_dir, lib_err);
        if (!lib_ok) {
            std::fprintf(stderr, "  (2) **ライブラリを読めませんでした**: %s\n", lib_err.c_str());
            ++failures;
        } else {
            memory.update(frame);
        }
        const PrefabLibrary *lib_ptr = lib_ok ? &library : nullptr;
        const TerrainMemory *mem_ptr = lib_ok ? &memory : nullptr;

        // 部屋の中に立たせる（合成マップは 16×14 間隔で部屋と通路を敷いてある）。
        camera.target = Vec3{ 100.5f, 44.5f, 0.f };
        TerrainView a;
        build_terrain_view(meaning, Frustum::from(camera.view_projection()), a, mem_ptr, lib_ptr);
        camera.target = Vec3{ 104.5f, 47.5f, 0.f };
        TerrainView b;
        build_terrain_view(meaning, Frustum::from(camera.view_projection()), b, mem_ptr, lib_ptr);

        // 両方に入っている置き場所は、**1 ビットも違ってはならない**。
        auto key = [](const std::string &group, const InstanceData &i) {
            return group + ":" + std::to_string(i.x) + "/" + std::to_string(i.y) + "/" + std::to_string(i.z) + "/"
                + std::to_string(i.sx) + "/" + std::to_string(i.sz) + "/" + std::to_string(i.r) + "/"
                + std::to_string(i.g) + "/" + std::to_string(i.b) + "/" + std::to_string(i.er);
        };
        std::set<std::string> in_a;
        for (const auto &i : a.boxes) {
            in_a.insert(key("box", i));
        }
        for (const auto &i : a.slabs) {
            in_a.insert(key("slab", i));
        }
        for (std::size_t bucket = 0; bucket < a.lib.size(); ++bucket) {
            for (const auto &i : a.lib[bucket]) {
                in_a.insert(key(std::to_string(bucket), i));
            }
        }

        /*
         * b 側の置き場所のうち、**1 回目でも確かに作られていた**ものだけを比べる。
         *
         * **置き場所の座標から「作ったマス」は逆に引けない**（罠）。装飾はマスの隅へ
         * `kPropCornerOffset`（0.28）＋ゆらぎだけ寄り、立ちものは `entry.anchor_*` の
         * ぶん原点がずれるので、**instance の座標を床関数に掛けたマスは隣のマスになりうる**。
         * 実際、(138,4) が撒いた小物が y = 3.81 に落ち、(138,3) の持ち物に見えていた。
         *
         * そこで**周り 2 マスぶんが全部 1 回目の視錐台に入っていること**を条件にする。
         * どのマスが作ったのであれ 1 回目も走査しているので、そこで見つからなければ
         * **本当に置き方が変わった**ことになる。際のマスは数えないが、それでよい
         * （間引きはカメラで変わってよい。守りたいのは「同じマスには同じものが立つ」）。
         *
         * 高さは 1.9 で見る。`build_terrain_view` は 7.5（ライブラリつき）で間引くので、
         * **こちらが低いぶんには安全側**（1.9 で交われば 7.5 でも必ず交わる）。
         *
         * この抜けは**画角を広げるまで表に出なかった**（2026-08-11 に決めた で
         * 40° → 70°。際のマスが一気に増えて、隣のマスの持ち物と取り違える確率が上がった）。
         */
        Camera at_a = camera;
        at_a.target = Vec3{ 100.5f, 44.5f, 0.f };
        const Frustum frustum_a = Frustum::from(at_a.view_projection());
        const auto neighbourhood_seen = [&frustum_a](const InstanceData &i) {
            const int gx = static_cast<int>(std::floor(i.x));
            const int gy = static_cast<int>(std::floor(i.y));
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const Vec3 lo{ static_cast<float>(gx + dx), static_cast<float>(gy + dy), -0.1f };
                    const Vec3 hi{ static_cast<float>(gx + dx) + 1.f, static_cast<float>(gy + dy) + 1.f, 1.9f };
                    if (!frustum_a.intersects(lo, hi)) {
                        return false;
                    }
                }
            }
            return true;
        };

        int shared = 0;
        int drifted = 0;
        auto check = [&](const std::string &group, const std::vector<InstanceData> &list) {
            for (const auto &i : list) {
                if (!neighbourhood_seen(i)) {
                    continue; // 1 回目には（作ったマスが）写っていなかったかもしれない
                }
                ++shared;
                if (in_a.find(key(group, i)) == in_a.end()) {
                    ++drifted;
                    //! **中身を出す**（数だけだと直しようが無い）。1 件見れば原因は分かる。
                    if (drifted <= 3) {
                        std::fprintf(stderr, "  (2) ずれ: %s\n", key(group, i).c_str());
                    }
                }
            }
        };
        check("box", b.boxes);
        check("slab", b.slabs);
        for (std::size_t bucket = 0; bucket < b.lib.size(); ++bucket) {
            check(std::to_string(bucket), b.lib[bucket]);
        }
        std::fprintf(stderr, "  (2) カメラを動かしても同じ: shared=%d drifted=%d "
                             "(1 回目 boxes=%zu slabs=%zu lib=%d / 2 回目 boxes=%zu slabs=%zu lib=%d)\n",
            shared, drifted, a.boxes.size(), a.slabs.size(), a.lib_instances,
            b.boxes.size(), b.slabs.size(), b.lib_instances);
        failures += (drifted != 0) ? 1 : 0;
        if (shared == 0) {
            std::fprintf(stderr, "  (2) **共通のマスが 0 だった。検査になっていない**\n");
            ++failures;
        }
        if (lib_ok && (a.lib_instances == 0)) {
            std::fprintf(stderr, "  (2) **ライブラリはあるのにプレハブが 1 つも置かれていない。検査になっていない**\n");
            ++failures;
        }
    }

    // (3) **検査の検査**: フロアが変われば必ず変わる。
    {
        FloorIdentity other = meaning.identity;
        other.generated_turn += 1;
        int same = 0;
        int total = 0;
        for (int gy = 0; gy < meaning.height; ++gy) {
            for (int gx = 0; gx < meaning.width; ++gx) {
                ++total;
                if (cell_seed(gx, gy, meaning.identity) == cell_seed(gx, gy, other)) {
                    ++same;
                }
            }
        }
        std::fprintf(stderr, "  (3) 別のフロアなら種が変わる: same=%d / %d\n", same, total);
        // 64bit のハッシュなので偶然の一致はまず起きない。1 つでも一致したら混ぜ方が悪い。
        failures += (same != 0) ? 1 : 0;

        int same_neighbour = 0;
        for (int gy = 0; gy < meaning.height; ++gy) {
            for (int gx = 1; gx < meaning.width; ++gx) {
                if (cell_seed(gx, gy, meaning.identity) == cell_seed(gx - 1, gy, meaning.identity)) {
                    ++same_neighbour;
                }
            }
        }
        std::fprintf(stderr, "  (3) 隣のマスと種が違う: same=%d\n", same_neighbour);
        failures += (same_neighbour != 0) ? 1 : 0;
    }

    /*
     * (4) **昼夜**（P5 ④・設計書 §12-4）。GL は要らないので、決定性の検査と一緒にここで見る。
     *
     * 見るのは「絵として正しいか」ではなく、**外の世界と噛み合っているか**である。
     * §14-2 の教訓（互いに辻褄が合う検査は系ごと間違っていても通る）がここでも効く:
     * 太陽の高度と方位を自分の式どうしで突き合わせても、東西が逆でも通ってしまう。
     */
    {
        FloorIdentity surface;
        surface.kind = static_cast<int>(FloorKind::Surface);
        const auto sun_at = [&](int hour) {
            LightingState state;
            state.day_minute = hour * 60;
            state.daytime = (hour >= 6) && (hour < 18);
            return make_scene_lighting(state, surface);
        };

        const SceneLighting dawn = sun_at(6);
        const SceneLighting noon = sun_at(12);
        const SceneLighting dusk = sun_at(18);
        const SceneLighting night = sun_at(0);

        struct Rule {
            const char *what;
            bool ok;
        };
        // **この世界は x = 東・y = 南。**朝日は東から差すので、面から光源へ向かう向きは +x。
        const Rule rules[] = {
            { "朝 6 時は東（+x）から", dawn.sun_dir.x > 0.5f },
            { "夕 18 時は西（-x）から", dusk.sun_dir.x < -0.5f },
            { "正午がいちばん高い", (noon.sun_dir.z > dawn.sun_dir.z) && (noon.sun_dir.z > dusk.sun_dir.z) },
            { "正午は真上に近い", noon.sun_dir.z > 0.9f },
            { "正午は影が濃い", noon.shadow_strength > 0.9f },
            // 夜も**薄い月影**は残す（0 にすると平板になる）。ただし昼よりは必ず薄い。
            { "真夜中の影は薄い", (night.shadow_strength > 0.05f) && (night.shadow_strength < 0.5f) },
            { "真夜中の光源は空にある", night.sun_dir.z > 0.5f },
            { "真夜中は環境光が落ちる", night.ambient_scale < noon.ambient_scale },
            // 地下は時刻に依らない（太陽が無い）。**地上と同じ値を返していたらここで落ちる。**
            { "地下は時刻で変わらない",
                make_scene_lighting(LightingState{ 0, false, 0 }, meaning.identity).sun_dir.z
                    == make_scene_lighting(LightingState{ 720, true, 0 }, meaning.identity).sun_dir.z },
        };
        int broken = 0;
        for (const Rule &rule : rules) {
            if (!rule.ok) {
                std::fprintf(stderr, "  (4) **%s** が成り立っていない\n", rule.what);
                ++broken;
            }
        }
        std::fprintf(stderr, "  (4) 昼夜: %d 件中 %d 件が成立"
                             " (6時 x=%.2f z=%.2f / 12時 z=%.2f / 18時 x=%.2f / 0時 影=%.2f)\n",
            static_cast<int>(std::size(rules)), static_cast<int>(std::size(rules)) - broken,
            dawn.sun_dir.x, dawn.sun_dir.z, noon.sun_dir.z, dusk.sun_dir.x, night.shadow_strength);
        failures += (broken != 0) ? 1 : 0;
    }

    // (5) 点光源の収集（P5 ②・設計書 §12-5）。**溢れたら黙って捨てないこと**を見る。
    {
        SceneLighting lighting = make_scene_lighting(frame.lighting, frame.floor);
        const int dropped = collect_point_lights(frame, Vec3{ 100.5f, 44.5f, 0.f }, lighting);
        const bool has_torch = !lighting.points.empty() && (lighting.points[0].radius > 1.f);
        std::fprintf(stderr, "  (5) 点光源: %zu 個 (捨てた %d)  松明 %s\n",
            lighting.points.size(), dropped, has_torch ? "あり" : "**なし**");
        failures += has_torch ? 0 : 1;

        /*
         * 検査の検査: 灯りを持っていなければ松明は置かれない。
         * **マスを空にしてから見る。**合成フレームには溶岩の池も入っているので
         * （P7 で足した。自発光の相手が要るため）、そのままだと「松明は消えたが
         * 溶岩が残っている」を「消えていない」と読んでしまう。
         */
        GameFrame dark = frame;
        dark.cells.clear();
        dark.lighting.light_radius = 0;
        SceneLighting dark_lighting = make_scene_lighting(dark.lighting, dark.floor);
        (void)collect_point_lights(dark, Vec3{ 100.5f, 44.5f, 0.f }, dark_lighting);
        std::fprintf(stderr, "  (5) 光源半径 0 なら松明なし: %zu 個\n", dark_lighting.points.size());
        failures += dark_lighting.points.empty() ? 0 : 1;

        // 上限で溢れたら**捨てた数が返る**（黙って消えない）。
        GameFrame crowded = frame;
        crowded.cells.clear();
        for (int i = 0; i < kMaxPointLights + 5; ++i) {
            MapCellView cell{};
            cell.gx = static_cast<int16_t>(100 + i);
            cell.gy = 44;
            cell.feature_flags = static_cast<uint16_t>(CELL_FEAT_KNOWN | CELL_FEAT_LAVA);
            crowded.cells.push_back(cell);
        }
        SceneLighting crowded_lighting = make_scene_lighting(crowded.lighting, crowded.floor);
        const int crowded_dropped = collect_point_lights(crowded, Vec3{ 100.5f, 44.5f, 0.f }, crowded_lighting);
        std::fprintf(stderr, "  (5) 溢れたぶんを報告する: %zu 個 / 捨てた %d\n",
            crowded_lighting.points.size(), crowded_dropped);
        failures += ((crowded_dropped == 6) && (static_cast<int>(crowded_lighting.points.size()) == kMaxPointLights))
            ? 0
            : 1;
    }

    /*
     * (6) **視線の遮り**（P10 レビュー 4）。カットアウェイを出すかどうかの根拠なので、
     * 「壁越しなら遮られ、開けていれば遮られない」を確かめる。
     *
     * **検査の検査は「遮蔽の高さを全部 0 にしたら必ず通る」**である。これが無いと、
     * 常に true を返す壊れた判定でも (a) だけは通ってしまう（§14-4）。
     */
    {
        Camera camera;
        camera.pitch = options.camera_pitch_deg * 0.0174533f;
        camera.fov_x = options.camera_fov_deg * 0.0174533f;
        camera.viewport_w = 1600;
        camera.viewport_h = 900;
        camera.distance = distance_for_cell_px(camera, options.camera_cell_px);
        camera.target = Vec3{ 100.5f, 44.5f, 0.f };

        PrefabLibrary library;
        TerrainMemory memory;
        std::string lib_err;
        const bool lib_ok = library.load(options.voxel_dir, lib_err);
        if (lib_ok) {
            memory.update(frame);
        }
        TerrainView view;
        build_terrain_view(meaning, Frustum::from(camera.view_projection()), view,
            lib_ok ? &memory : nullptr, lib_ok ? &library : nullptr);

        /*
         * (a) **置く側が遮蔽の高さを記録しているか。**壁のマスには高さが立ち、
         * 部屋の床は 0 のままでなければならない。
         *
         * **視錐台の外は 0 のまま**なので、ここは「カメラに写っているマス」で見る
         * （最初は画面外の南の壁で試して、カリングのせいで 0 を拾った）。
         */
        int walls_with_height = 0;
        int floors_with_height = 0;
        for (int gy = 0; gy < meaning.height; ++gy) {
            for (int gx = 0; gx < meaning.width; ++gx) {
                const float top = view.occluder_at(gx, gy);
                const CellRole role = meaning.role_at(gx, gy);
                if ((role == CellRole::StructuralWall) || (role == CellRole::Bedrock)) {
                    walls_with_height += (top > 0.5f) ? 1 : 0;
                } else if ((role == CellRole::RoomFloor) || (role == CellRole::CorridorFloor)) {
                    floors_with_height += (top > 0.5f) ? 1 : 0;
                }
            }
        }
        /*
         * **床に遮蔽があってもよい。**背の高い装飾（柱は 1.7 マス）や木は床の上に立つので、
         * そのマスは視線を遮る（P10 レビュー 6。こう気づいた——「オブジェクトが視界を
         * 遮るかどうかにしてくれ」）。見るのは「**置いた覚えの無いマスに書いていないか**」で、
         * 上限は装飾の数（1 つの装飾が複数マスを覆うので、そのぶんの余裕を見る）。
         */
        //! 1 つの装飾が覆えるマスは多くて 4×4（木でも 4 マス角）。それを超えたら書きすぎ。
        const int prop_budget = std::max(4, view.prop_count * 16);
        std::fprintf(stderr, "  (6) 遮蔽の記録: 高さのある壁 %d マス / 床の上に立つもの %d マス（上限 %d）\n",
            walls_with_height, floors_with_height, prop_budget);
        failures += (walls_with_height > 0) ? 0 : 1;
        failures += (floors_with_height <= prop_budget) ? 0 : 1;

        /*
         * (b) **判定そのもの。**手で作った小さな地形で確かめる（世界のカリングに
         * 左右されない）。カメラは (0.5, 0.5, 4) から (5, 5) のプレイヤを見下ろす。
         */
        TerrainView probe;
        probe.occluder_w = 8;
        probe.occluder_h = 8;
        probe.occluder_z.assign(64, 0.f);
        const Vec3 eye{ 0.5f, 0.5f, 4.0f };
        const bool open_ok = !line_of_sight_blocked(probe, eye, 5, 5);
        probe.occluder_z[(3 * 8) + 3] = 3.0f; // 視線の途中に高い壁を 1 マス
        const bool blocked_ok = line_of_sight_blocked(probe, eye, 5, 5);
        std::fprintf(stderr, "  (6) 視線: 開けていれば通る=%s / 高い壁があれば遮られる=%s\n",
            open_ok ? "OK" : "**FAILED**", blocked_ok ? "OK" : "**FAILED**");
        failures += open_ok ? 0 : 1;
        failures += blocked_ok ? 0 : 1;

        /*
         * (c) **足元だけが隠れていても遮蔽**（P10 レビュー 5。気づいたこと
         * 「頭部の一部が見えることで視線が通っている扱いになる」）。
         *
         * 腰へのレイがぎりぎり越え、足元へのレイだけが当たる高さを選ぶ。
         * カメラ (0.5,0.5,4) → プレイヤ (5.5,5.5) の直線で、遮る壁は (3,3) の中央
         * （道のり 5/7 手前 ≒ t=0.50）。そこでの視線の高さは
         * 足元 = 4 + (0.08-4)×0.50 ≒ 2.04 / 腰 ≒ 2.28 なので、**その間**に天面を置く。
         */
        probe.occluder_z[(3 * 8) + 3] = 2.15f;
        const bool feet_ok = line_of_sight_blocked(probe, eye, 5, 5);
        std::fprintf(stderr, "  (6) 足元だけ隠れる高さ（頭は見えている）: %s\n",
            feet_ok ? "遮られる" : "**通ってしまう**");
        failures += feet_ok ? 0 : 1;

        /*
         * (d) **検査の検査。**同じ位置に**低い**ものを置いたら通らなければならない。
         * これが無いと「何か置いてあれば必ず遮られる」壊れた判定でも (b)(c) は通る。
         */
        probe.occluder_z[(3 * 8) + 3] = 0.2f;
        const bool low_ok = !line_of_sight_blocked(probe, eye, 5, 5);
        std::fprintf(stderr, "  (6) 検査の検査（同じ所に低いものだけ）: %s\n",
            low_ok ? "通る" : "**まだ遮られる**");
        failures += low_ok ? 0 : 1;
    }

    /*
     * (7) 対応表と `lib/edit/TerrainDefinitions.jsonc` の突き合わせ（P10・設計書 §9.4）。
     * key の不在・id の食い違いは**表の書き間違いなので FAIL**。表に無い id は
     * 役割の既定で描かれるだけなので**一覧を出すだけ**（素材を足すときの TODO 列になる）。
     * 検査の検査: `HD2D_BREAK_TERRAIN_MAP=prefab`（ライブラリの読みが落ちる）/ `=id`（突き合わせが落ちる）。
     */
    {
        PrefabLibrary library;
        std::string lib_err;
        if (!library.load(options.voxel_dir, lib_err)) {
            std::fprintf(stderr, "  (6) **ライブラリを読めませんでした**: %s\n", lib_err.c_str());
            ++failures;
        } else {
            std::string report;
            if (!library.cross_check("lib/edit/TerrainDefinitions.jsonc", report, lib_err)) {
                std::fprintf(stderr, "  (6) **対応表の突き合わせに失敗**: %s\n", lib_err.c_str());
                ++failures;
            } else {
                std::fprintf(stderr, "  (6) 対応表: %d 個  %s\n", library.count(), report.c_str());
            }

            /*
             * (7) **扉は通る向きに合わせて姿勢を変える**（P10 レビュー 11。決めたこと
             * 2026-08-09:「扉の向きは東西に通過の場合は 90 度回転して設置して」）。
             *
             * 同じ扉を「南北に通る所」と「東西に通る所」の 2 か所へ置いて、
             * **選ばれるプレハブが変わること**を見る。姿勢を選ぶ規則は絵を見ないと
             * 分からないものではないので、GL 無しでここまで確かめられる。
             *
             * 検査の検査は `HD2D_BREAK_DOOR_TURN=1`（向きを見ずに基の姿勢で押し通す）。
             */
            if (library.ready()) {
                //! 十字の通路を 1 本ずつ持つ小さなフロアを組み、真ん中に扉を置く。
                const auto door_pick_at = [&](bool east_west, int door_terrain_id) {
                    GameFrame frame = make_synthetic_frame(23, 19, 100, 44);
                    const int w = frame.minimap.width;
                    //! 全部岩盤にしてから、通路を 1 本だけ通す（周りは必ず壁になる）。
                    frame.minimap.kinds.assign(frame.minimap.kinds.size(),
                        static_cast<std::uint8_t>(MinimapKind::Wall));
                    for (int t = -6; t <= 6; ++t) {
                        const int gx = east_west ? (100 + t) : 100;
                        const int gy = east_west ? 44 : (44 + t);
                        frame.minimap.kinds[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(w))
                            + static_cast<std::size_t>(gx)]
                            = static_cast<std::uint8_t>((t == 0) ? MinimapKind::Door : MinimapKind::Floor);
                    }
                    for (auto &cell : frame.cells) {
                        cell.feature_flags = CELL_FEAT_KNOWN;
                        const int dx = cell.gx - 100;
                        const int dy = cell.gy - 44;
                        const bool on_line = east_west ? ((dy == 0) && (std::abs(dx) <= 6))
                                                       : ((dx == 0) && (std::abs(dy) <= 6));
                        //! 真ん中のマスだけ扉（渡された id）、通路は床、残りは花崗岩。
                        if ((dx == 0) && (dy == 0)) {
                            cell.terrain_id = static_cast<std::uint16_t>(door_terrain_id);
                        } else if (on_line) {
                            cell.terrain_id = 1;
                        } else {
                            cell.terrain_id = 56;
                            cell.feature_flags |= CELL_FEAT_WALL;
                        }
                    }
                    FloorMeaning local;
                    (void)rebuild_floor_meaning(frame, local);
                    TerrainMemory memory;
                    memory.update(frame);
                    Camera cam;
                    cam.pitch = options.camera_pitch_deg * 0.0174533f;
                    cam.fov_x = options.camera_fov_deg * 0.0174533f;
                    cam.viewport_w = 1600;
                    cam.viewport_h = 900;
                    cam.distance = distance_for_cell_px(cam, options.camera_cell_px);
                    cam.target = Vec3{ 100.5f, 44.5f, 0.f };
                    TerrainView view;
                    build_terrain_view(local, Frustum::from(cam.view_projection()), view, &memory, &library);
                    /*
                     * 真ん中のマスに**立っているもの**の名前を拾う。
                     * 地面（`is_ground`）は除く——床タイルも同じマスへ積まれているので、
                     * 名前の頭で選り分けると素材の名前に縛られる（帳は `door` で始まらない）。
                     */
                    std::string found;
                    for (std::size_t bucket = 0; bucket < view.lib.size(); ++bucket) {
                        const LibraryEntry &entry = library.entry(static_cast<int>(bucket));
                        if (entry.is_ground) {
                            continue;
                        }
                        for (const auto &at : view.lib[bucket]) {
                            if ((static_cast<int>(std::floor(at.x)) == 100)
                                && (static_cast<int>(std::floor(at.y)) == 44)) {
                                found = entry.name;
                            }
                        }
                    }
                    return found;
                };
                /*
                 * **扉は 1 種類ではない**（2026-08-11 に気づいた:「ガラス扉のオブジェクトが
                 * 東西方向に設置される際向きが南北のままになる」）。コアの扉は樫・硝子・帳の
                 * 3 系統あり、**表に行が無いものは `doorway` の既定へ落ちる**。既定が姿勢の対を
                 * 持っていなかったので、そこへ落ちた扉だけ南北のまま立っていた。
                 *
                 * ここは 3 系統に**既定そのもの**（id 0 = 細別が届いていないマス）を足した 4 件を
                 * 同じ物差しで見る。1 件ずつ見ていたら同じ穴をまた開ける。
                 */
                struct DoorCase {
                    const char *what;
                    int terrain_id;
                };
                static constexpr DoorCase kDoorCases[] = {
                    { "樫の扉", 32 }, //!< CLOSED_DOOR
                    { "ガラスの扉", 202 }, //!< CLOSED_GLASS_DOOR
                    { "帳", 221 }, //!< CLOSED_CURTAIN
                    { "既定の口", 0 }, //!< 細別がまだ届いていないマス（roles の doorway）
                };
                for (const DoorCase &door : kDoorCases) {
                    const std::string ns = door_pick_at(false, door.terrain_id);
                    const std::string ew = door_pick_at(true, door.terrain_id);
                    std::fprintf(stderr, "  (7) 扉の姿勢[%s]: 南北=\"%s\" / 東西=\"%s\"\n",
                        door.what, ns.c_str(), ew.c_str());
                    if (ns.empty() || ew.empty()) {
                        std::fprintf(stderr, "  (7) **%s が置かれていない。検査になっていない**\n", door.what);
                        ++failures;
                    } else if (ns == ew) {
                        std::fprintf(stderr, "  (7) **%s は東西に通る所でも同じ姿勢のままです**"
                                             "（通る向きを見ていません）\n",
                            door.what);
                        ++failures;
                    } else if (ew != (ns + "_ew")) {
                        std::fprintf(stderr, "  (7) **%s の東西の姿勢が対になっていません**"
                                             "（\"%s\" の対は \"%s_ew\"）\n",
                            door.what, ns.c_str(), ns.c_str());
                        ++failures;
                    }
                }
            }

            /*
             * ---- (8) **小物は一度置いたら書き換わらない**（2026-08-11 に決めた:
             * 「ダンジョンの小物は一度生成されたら書き換わらないように」）----
             *
             * 装飾層は**並びの意味づけ**（行き止まり・壁際・部屋の縁）から置く物を決めるので、
             * 探索が進んで既知マスが増えると**枝そのものが動く**。種はマスとフロアだけで
             * 決まるが、それは「同じ枝を通ったとき」の話でしかない。
             *
             * ここは行き止まりの通路を 1 本引き、**先を見せる前と後**で組み直して、
             * ロック（`PropLatch`）を渡したときだけ**前に置いたものが 1 つも消えない**ことを見る。
             * ロックを渡さない側が変わることも併せて確かめる（変わらなければ検査になっていない）。
             */
            {
                //! 幅 1 の通路を `known` マスぶんだけ見せたフレームを作る。
                const auto corridor_frame = [](int known) {
                    GameFrame f = make_synthetic_frame(23, 19, 100, 44);
                    const int w = f.minimap.width;
                    f.minimap.kinds.assign(f.minimap.kinds.size(), static_cast<std::uint8_t>(MinimapKind::Unknown));
                    for (int t = 0; t < known; ++t) {
                        const int gx = 100 + t;
                        //! 通路と、その南北の壁だけを見せる（幅 1 であることが分かる形）。
                        for (int dy = -1; dy <= 1; ++dy) {
                            const std::size_t at = (static_cast<std::size_t>(44 + dy) * static_cast<std::size_t>(w))
                                + static_cast<std::size_t>(gx);
                            f.minimap.kinds[at] = static_cast<std::uint8_t>(
                                (dy == 0) ? MinimapKind::Floor : MinimapKind::Wall);
                        }
                    }
                    for (auto &cell : f.cells) {
                        cell.feature_flags = 0;
                        cell.terrain_id = 0;
                    }
                    return f;
                };
                //! いま置かれているものを (ライブラリの索引, 位置) の集合として採る。
                const auto snapshot = [&library](const TerrainView &view) {
                    std::set<std::string> out_set;
                    for (std::size_t bucket = 0; bucket < view.lib.size(); ++bucket) {
                        for (const auto &at : view.lib[bucket]) {
                            char buf[128]{};
                            std::snprintf(buf, sizeof(buf), "%s@%.3f,%.3f",
                                library.entry(static_cast<int>(bucket)).name.c_str(), at.x, at.y);
                            out_set.insert(buf);
                        }
                    }
                    return out_set;
                };
                Camera cam;
                cam.pitch = options.camera_pitch_deg * 0.0174533f;
                cam.fov_x = options.camera_fov_deg * 0.0174533f;
                cam.viewport_w = 1600;
                cam.viewport_h = 900;
                cam.distance = distance_for_cell_px(cam, options.camera_cell_px);
                cam.target = Vec3{ 106.5f, 44.5f, 0.f };
                const Frustum frustum = Frustum::from(cam.view_projection());

                //! ロックあり / ロックなしで、同じ「見せる前 → 見せた後」を通す。
                const auto run = [&](bool use_latch) {
                    PropLatch latch;
                    FloorMeaning m1;
                    const GameFrame f1 = corridor_frame(6);
                    (void)rebuild_floor_meaning(f1, m1);
                    latch.follow(f1.floor);
                    TerrainView v1;
                    build_terrain_view(m1, frustum, v1, nullptr, &library, nullptr, 0.f, 0.f, false,
                        use_latch ? &latch : nullptr);
                    const auto before = snapshot(v1);

                    FloorMeaning m2;
                    const GameFrame f2 = corridor_frame(14); //!< 先が見えた（行き止まりではなくなる）
                    (void)rebuild_floor_meaning(f2, m2);
                    latch.follow(f2.floor);
                    TerrainView v2;
                    build_terrain_view(m2, frustum, v2, nullptr, &library, nullptr, 0.f, 0.f, false,
                        use_latch ? &latch : nullptr);
                    const auto after = snapshot(v2);

                    int lost = 0;
                    for (const auto &one : before) {
                        lost += (after.find(one) == after.end()) ? 1 : 0;
                    }
                    return std::pair<int, std::size_t>{ lost, before.size() };
                };
                const auto locked = run(true);
                const auto loose = run(false);
                std::fprintf(stderr, "  (8) 小物の錠: 先を見せる前 %zu 個 / 錠あり 消えた %d 個"
                                     " / 錠なし 消えた %d 個\n",
                    locked.second, locked.first, loose.first);
                if (locked.second == 0) {
                    std::fprintf(stderr, "  (8) **置かれたものが 1 つも無い。検査になっていない**\n");
                    ++failures;
                }
                if (locked.first != 0) {
                    std::fprintf(stderr, "  (8) **錠を掛けたのに小物が書き換わりました**\n");
                    ++failures;
                }
                if (loose.first == 0) {
                    std::fprintf(stderr, "  (8) **錠なしでも変わらない。検査になっていない**"
                                         "（この地形では枝が動かない）\n");
                    ++failures;
                }
            }
        }
    }

    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

/*!
 * @brief `--town-check`: **実データの町**で敷地の分解を確かめる（設計書 §10）。窓も GL も不要。
 *
 * @details 見るのは 7 つ。
 * | # | 何を | なぜ |
 * |---|---|---|
 * | 1 | 敷地・城壁・世界の枠に分かれる | §10.3 の読みそのもの。1 つに潰れていたら意味づけが効いていない |
 * | 2 | **データの入口が全部「門」になる** | 実データの記号（店・施設）が正。取りこぼしは絵に穴が開く |
 * | 3 | **門から奥への筋に建物が無い** | §10.3-3。ここが埋まると入口が建物に隠れる |
 * | 4 | 柵は**敷地の外**を向いた辺だけ | 内側に立つと敷地が檻に見える |
 * | 5 | 建物は 2×2 以上・敷地からはみ出さない | 1 マス幅の家を作らない（§10.4 の「露店」へ落とす） |
 * | 6 | ライブラリの素材が**実際に置かれる** | 名前の引き忘れ（罠 36）はここでしか出ない |
 * | 7 | **町の種が訪れ直しで変わらない** | `town_id` を足した理由そのもの（§12-2） |
 *
 * **検査の検査**は `HD2D_BREAK_TOWN`（`sites` / `gate` / `corridor`）。
 */
int run_town_check(const AppOptions &options)
{
    const std::string path = options.town_data_path.empty()
        ? std::string("lib/edit/towns/01_Outpost_Full.txt")
        : options.town_data_path;
    /*
     * ライブラリを先に読む。町のデータの記号（`F:1:GENERAL_STORE`）を `terrain_id` へ翻訳するのに
     * 表の (key, id) が要る（`PrefabLibrary::terrain_id_for_key`）。
     */
    PrefabLibrary library;
    std::string err;
    if (!library.load(options.voxel_dir, err)) {
        std::fprintf(stderr, "[hd2d] **ライブラリを読めませんでした**: %s\n", err.c_str());
        return 1;
    }
    GameFrame frame;
    if (!load_town_frame(path, library, frame, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        return 1;
    }
    /*
     * **別コアの町を検査へ通す手段**（デザイン7・2026-08-19）。`--town-view` には
     * 2026-08-18 に足してあったが、こちらはコア名が無いまま変愚の表で読んでいたので、
     * 幻想蛮怒の読み方（屋敷・中庭・大鳥居）が 1 つも立たない絵を検分していた。
     */
    load_styles_for_check(options);
    FloorMeaning meaning;
    if (!rebuild_floor_meaning(frame, meaning)) {
        std::fprintf(stderr, "[hd2d] 意味づけを作れませんでした\n");
        return 1;
    }
    TownPlan town;
    const std::vector<std::uint8_t> entrances = town_entrance_mask(frame, library);
    if (!rebuild_town_plan(meaning, town, nullptr, &entrances)) {
        std::fprintf(stderr, "[hd2d] **町を読めませんでした**（town_id=%d kind=%d）\n",
            meaning.identity.town_id, meaning.identity.kind);
        return 1;
    }
    std::fprintf(stderr, "[hd2d] town-check: %s\n", path.c_str());
    std::fprintf(stderr, "  %s\n", town_plan_summary(town).c_str());

    /*
     * 読んだ結果を絵で出す。**数字だけでは「どう分解されたか」が分からない**（実際、
     * 前庭が広すぎることに気づいたのは実機の絵を見てからだった）。
     * `#`=城壁 `+`=世界の枠 `H`=建物 `M`=目印の建物 `,`=前庭 `G`=門
     */
    {
        std::fprintf(stderr,
            "  ---- 読んだ町（#=塀 L=下げた塀 O=2x2の柱 T=やぐら +=枠 H=建物 M=目印 Y=屋敷 ,=前庭 G=門"
            " ==通り道 空白=草）----\n");
        for (int gy = 0; gy < town.height; ++gy) {
            std::string line;
            bool any = false;
            for (int gx = 0; gx < town.width; ++gx) {
                char glyph = ' ';
                switch (town.role_at(gx, gy)) {
                case TownRole::Tower:
                    glyph = 'T';
                    break;
                case TownRole::Rampart:
                    //! **入口の手前で下げた塀**（2026-08-27）。役割は塀のままなので字で分ける。
                    glyph = (static_cast<std::uint8_t>(town.slice_at(gx, gy)) == kRampartLow) ? 'L'
                                                                                             : '#';
                    //! 御柱（デザイン13。役割は塀のままなので、ここで見分けて字を替える）。
                    for (const auto &spot : town.quad_walls) {
                        if ((gx >= spot[0]) && (gx <= (spot[0] + 1)) && (gy >= spot[1])
                            && (gy <= (spot[1] + 1))) {
                            glyph = 'O';
                            break;
                        }
                    }
                    break;
                case TownRole::Fountain: {
                    //! 0 = 柱 / 1 = 噴き上げ / 2 = 縁（低い柵と花壇）
                    const auto kind = static_cast<std::uint8_t>(town.slice_at(gx, gy));
                    glyph = (kind == 1u) ? '*' : ((kind == 0u) ? 'o' : '~');
                    break;
                }
                case TownRole::Border:
                    glyph = '+';
                    break;
                case TownRole::Crag:
                    //! 岩山。**段（山の外からの深さ）を数字で**出す（1 = 麓 … 4 = 峰）。
                    glyph = static_cast<char>('1'
                        + std::min<int>(static_cast<int>(town.slice_at(gx, gy)), 3));
                    break;
                case TownRole::House: {
                    const std::int16_t si = town.site_at(gx, gy);
                    glyph = ((si >= 0) && (town.sites[static_cast<std::size_t>(si)].landmark != 0)) ? 'M' : 'H';
                    break;
                }
                case TownRole::Manor:
                    /*
                     * **屋敷**（デザイン12・2026-08-20 に足した）。それまで空白＝草と
                     * 同じ字で出ていたので、792 マスの館が**地図の上では何も無い**ように
                     * 見えた（廃洋館の検分で気づいた）。屋敷は幻想蛮怒の 6 町で主役なので、
                     * 塀と見分けの付く字を当てる。
                     */
                    glyph = 'Y';
                    break;
                case TownRole::Yard:
                    glyph = ',';
                    break;
                case TownRole::Gate:
                    glyph = 'G';
                    break;
                case TownRole::Path:
                    glyph = '=';
                    break;
                case TownRole::Turf:
                case TownRole::None:
                default:
                    break;
                }
                any = any || (glyph != ' ') || (glyph == '+');
                line += glyph;
            }
            //! 世界の枠と岩山だけの行は出さない（198×66 の全部を流すと読めない）。
            if (any && (line.find_first_not_of(" +1234") != std::string::npos)) {
                std::fprintf(stderr, "  %2d %s\n", gy, line.c_str());
            }
        }
    }

    int failures = 0;

    /*
     * **敷地の無い町がある**（`town_plan.h` の `TownRole::Crag`）。ズルは山を刳り抜いた
     * 谷で、店も施設も**山肌に開いた穴**である。ここが 0 件でも壊れてはいない
     * ——(1) が「敷地の並び」と「山肌の入口」の 2 通りで見ているのと同じ話で、
     * 建物を要求する検査（(3)・(5)・(6) の家の素材）はそちらへ通さない。
     *
     * 第 3 期の時点では**溶岩湖の中の山（96 マス・全部 `MOUNTAIN`）が敷地 1 件に読まれて**
     * いて、そこに家が建っていたので検査が通っていた。山を山と読むようになった
     * （2026-08-12）ので、ズルの敷地は 0 件が正しい。
     */
    const bool valley_town = town.sites.empty() && (town.crag_gates > 0);
    if (valley_town) {
        std::fprintf(stderr, "  ---- 敷地を持たない町（山肌の入口 %d）。建物の検査は飛ばします ----\n",
            town.crag_gates);
    }

    // (1) 敷地・城壁・世界の枠に分かれているか。
    {
        int gated = 0;
        for (const TownSite &site : town.sites) {
            gated += site.has_gate ? 1 : 0;
        }
        std::fprintf(stderr, "  (1) 敷地 %zu 件（門あり %d）／城壁 %d マス／世界の枠 %d マス"
                             "／岩山 %d マス（山肌の入口 %d）\n",
            town.sites.size(), gated, town.rampart_cells, town.border_cells, town.crag_cells,
            town.crag_gates);
        /*
         * **町には 2 つの形がある**（P10 第 3 期。2026-08-10 に決めた）。
         * ふつうの町は「敷地の並び」だが、ズルは**山を刳り抜いた谷**で、店の入口は
         * 山肌に開いた穴である（敷地は 1 件しか無い）。どちらも「侵入不可のマスが
         * 1 種類に潰れていない」ことを見ているので、判定を 2 通りにする。
         */
        const bool as_sites = (town.sites.size() >= 10) && (gated >= 8) && (town.rampart_cells > 0)
            && (town.border_cells > 0);
        const bool as_crag = (town.crag_cells > 0) && (town.crag_gates >= 8);
        if (!as_sites && !as_crag) {
            std::fprintf(stderr, "  (1) **敷地の仕分けが成立していません**"
                                 "（永久壁が 1 種類に潰れているか、読めていない）\n");
            ++failures;
        }
    }

    /*
     * (2) **実データの入口が全部「門」になっているか。**
     * 正は町のデータの記号（店 `0`〜`9`・施設 `a`〜`p`）で、こちらの読みではない。
     * 「開いている隣を向きとする」規則は `tools/voxel/town_entrances.py` と同じ。
     */
    {
        std::FILE *fp = std::fopen(path.c_str(), "rb");
        std::vector<std::string> rows;
        if (fp != nullptr) {
            char line[1024];
            while (std::fgets(line, sizeof(line), fp) != nullptr) {
                std::string text(line);
                while (!text.empty() && ((text.back() == '\n') || (text.back() == '\r'))) {
                    text.pop_back();
                }
                if (text.rfind("D:", 0) == 0) {
                    rows.push_back(text.substr(2));
                }
            }
            std::fclose(fp);
        }
        const std::string entrance_glyphs = "0123456789abcdefghijklmnop";
        int total = 0;
        int as_gate = 0;
        std::string missed;
        for (int gy = 0; gy < static_cast<int>(rows.size()); ++gy) {
            const std::string &row = rows[static_cast<std::size_t>(gy)];
            for (int gx = 0; gx < static_cast<int>(row.size()); ++gx) {
                if (entrance_glyphs.find(row[static_cast<std::size_t>(gx)]) == std::string::npos) {
                    continue;
                }
                ++total;
                if (town.role_at(gx, gy) == TownRole::Gate) {
                    ++as_gate;
                } else if (missed.size() < 120) {
                    char buf[32]{};
                    std::snprintf(buf, sizeof(buf), " %c(%d,%d)", row[static_cast<std::size_t>(gx)], gx, gy);
                    missed += buf;
                }
            }
        }
        std::fprintf(stderr, "  (2) データの入口 %d 箇所のうち門になったもの %d 箇所%s%s\n", total, as_gate,
            missed.empty() ? "" : "  取りこぼし:", missed.c_str());
        if ((total == 0) || (as_gate * 10 < total * 9)) {
            std::fprintf(stderr, "  (2) **入口の 9 割を門にできていません**\n");
            ++failures;
        }
    }

    /*
     * (3) **門から奥への筋に建物が無い**（§10.3-3「建物を左右どちらかへ寄せて置く」）。
     * ここが埋まると、実物の高さを持った建物が入口を隠す。
     *
     * 見るのは**敷地が門の南に広がる敷地だけ**である。カメラは常に南から見下ろすので、
     * 敷地が門の北にあれば建物は門の向こう側になり、そもそも何も隠さない
     * （全部の敷地で筋を空けていた版は、敷地の 2/3 が前庭になって町が空き地に見えた）。
     */
    {
        int checked = 0;
        int blocked = 0;
        for (const TownSite &site : town.sites) {
            if (!site.has_gate || !site.has_house() || (site.gate_dy >= 0)) {
                continue; // gate_dy < 0 ＝ 開いているのが北側 ＝ 敷地は門の南
            }
            ++checked;
            for (int step = 1; step <= std::max(site.width(), site.height()); ++step) {
                const int gx = site.gate_x - (site.gate_dx * step);
                const int gy = site.gate_y - (site.gate_dy * step);
                if ((gx < site.x0) || (gx > site.x1) || (gy < site.y0) || (gy > site.y1)) {
                    break;
                }
                if (town.role_at(gx, gy) == TownRole::House) {
                    ++blocked;
                    break;
                }
            }
        }
        std::fprintf(stderr, "  (3) 門から奥への筋（南に広がる敷地）: 調べた %d 件 / 建物が塞いでいる %d 件\n",
            checked, blocked);
        if ((checked == 0) && !valley_town) {
            std::fprintf(stderr, "  (3) **南へ広がる敷地が 1 件も無い。検査になっていない**\n");
            ++failures;
        }
        failures += (blocked == 0) ? 0 : 1;
    }

    /*
     * (4) **辺ごとの飾りは外を向いた辺だけ。**
     *
     * `fences` は 2 つの役目を持つ（P10 第 2 期レビュー 4）。前庭（`Yard`）では柵、
     * 塀（`Rampart`）では**上に載せる胸壁**である。どちらも「外」の意味が違うので分けて数える。
     * 一緒くたに数えていた版は、胸壁を足した瞬間に辺境の地まで落ちた
     * （塀のマスは敷地を持たないので `site_at` が -1 どうしで一致してしまう）。
     */
    {
        int rails = 0;
        int inward = 0;
        int merlons = 0;
        int merlon_inward = 0;
        const int dx[4] = { 0, 0, -1, 1 };
        const int dy[4] = { -1, 1, 0, 0 };
        for (int gy = 0; gy < town.height; ++gy) {
            for (int gx = 0; gx < town.width; ++gx) {
                const TownRole role = town.role_at(gx, gy);
                const std::uint8_t bits = town.fence_at(gx, gy);
                for (int i = 0; i < 4; ++i) {
                    if ((bits & (1u << i)) == 0u) {
                        continue;
                    }
                    if (role == TownRole::Rampart) {
                        ++merlons;
                        //! 胸壁は「隣が塀でない辺」だけ。塀どうしの間に立っていたら壊れている。
                        const TownRole side = town.role_at(gx + dx[i], gy + dy[i]);
                        if ((side == TownRole::Rampart) || (side == TownRole::Tower)
                            || (side == TownRole::Border) || (side == TownRole::Gate)) {
                            ++merlon_inward;
                        }
                        continue;
                    }
                    if (role == TownRole::Fountain) {
                        //! 噴水の縁の低い柵。「隣が噴水でない辺」だけ（同じ理屈）。
                        ++merlons;
                        if (town.role_at(gx + dx[i], gy + dy[i]) == TownRole::Fountain) {
                            ++merlon_inward;
                        }
                        continue;
                    }
                    ++rails;
                    if (town.site_at(gx + dx[i], gy + dy[i]) == town.site_at(gx, gy)) {
                        ++inward;
                    }
                }
            }
        }
        std::fprintf(stderr, "  (4) 胸壁 %d 枚 / 塀どうしの間に立ったもの %d 枚\n", merlons, merlon_inward);
        failures += (merlon_inward == 0) ? 0 : 1;
        std::fprintf(stderr, "  (4) 柵 %d 枚 / 敷地の内側を向いたもの %d 枚\n", rails, inward);
        //! 柵は前庭のものなので、敷地を持たない町（ズル）には 1 枚も無いのが正しい。
        failures += ((rails > 0) || valley_town) ? 0 : 1;
        failures += (inward == 0) ? 0 : 1;
    }

    // (5) 建物は 2×2 以上で敷地からはみ出さない。
    {
        int bad = 0;
        int houses = 0;
        for (const TownSite &site : town.sites) {
            if (!site.has_house()) {
                continue;
            }
            ++houses;
            const int hw = site.house_x1 - site.house_x0 + 1;
            const int hh = site.house_y1 - site.house_y0 + 1;
            const bool inside = (site.house_x0 >= site.x0) && (site.house_x1 <= site.x1)
                && (site.house_y0 >= site.y0) && (site.house_y1 <= site.y1);
            if ((hw < 2) || (hh < 2) || !inside) {
                ++bad;
            }
        }
        std::fprintf(stderr, "  (5) 建物 %d 棟 / 形の合わないもの %d 棟（露店 %d 件）\n", houses, bad,
            town.stall_sites);
        failures += ((houses > 0) || valley_town) ? 0 : 1;
        failures += (bad == 0) ? 0 : 1;
    }

    /*
     * (6) **ライブラリの素材が実際に置かれるか。**名前で引く素材は表の規則から参照されないので、
     * `extra` への載せ忘れ（罠 36・67）はここでしか出ない。町を丸ごと視錐台に入れて数える。
     */
    {
        {
            TerrainMemory memory;
            memory.update(frame);
            //! 町の全域を写す視錐台（引いて全部入れる。カリングで数え落とさないため）。
            Camera camera;
            camera.pitch = 70.f * 0.0174533f;
            camera.fov_x = 120.f * 0.0174533f;
            camera.viewport_w = 1600;
            camera.viewport_h = 900;
            camera.distance = 260.f;
            camera.target = Vec3{ static_cast<float>(town.width) * 0.5f, static_cast<float>(town.height) * 0.5f, 0.f };
            TerrainView view;
            build_terrain_view(meaning, Frustum::from(camera.view_projection()), view, &memory, &library, &town, 1.f);

            /*
             * 置かれるはずの素材。**町ごとの意匠を通した名前で見る**（P10 第 2 期レビュー 3）。
             * 基の名前で決め打ちにすると、テルモラとモリバントでは辺境の地の素材が
             * 置かれないのは当たり前なので**必ず落ちる**うえ、肝心の「その町の素材が
             * 本当に置かれたか」を 1 度も見ないままになる。
             *
             * `styled` が真の行は「茎 ＋ 接尾辞」、`variant` が真の行はさらに `_01` が付く
             * （`palisade` → `palisade_mor_01`）。門と灯りは意匠を持たない。
             */
            /*
             * `need` は**その町がその素材を要るか**（P10 第 3 期）。ズルには塀が 1 マスも
             * 無い（山を刳り抜いた谷なので、囲うものが山そのものである）ので、
             * 丸太の塀とやぐらが置かれないのは**正しい**。逆に岩山と高山植物は
             * 山のある町でだけ要る。全部を一律に求めると、町の形の違いが FAIL に化ける。
             */
            enum class Need {
                Always,
                Rampart, //!< 塀のある町だけ
                /*!
                 * @brief **敷地のある町だけ**（2026-08-12）。
                 * @details ズルは山を刳り抜いた谷で、店も施設も山肌に開いた穴なので
                 * **敷地が 1 件も無い**（`valley_town`）。家の壁・屋根・柵・前庭・窓の灯りは
                 * 置く場所がそもそも無いので、一律に求めると町の形の違いが FAIL に化ける
                 * （塀とやぐらを `Rampart` で外してあるのとまったく同じ話）。
                 */
                Site,
                Crag, //!< 岩山のある町だけ
                /*!
                 * @brief **山塊**のある町だけ（2026-08-11）。
                 * @details 峰（`crag_peak`）は山の外から 4 段めなので、露岩には出ない。
                 * 辺境の地のイークの洞窟は 25 マスの露岩で、深さは 2 段までしか無い
                 * （`town_plan.cpp` の `crag_mass`）。ここを `Crag` のままにしていたら
                 * **「峰が置かれていない」で FAIL した**——正しく置かれていないのではなく、
                 * 置く場所がそもそも無い。
                 */
                CragMass,
                /*!
                 * @brief **入口の手前の塀を下げた町だけ**（2026-08-27）。
                 * @details 低い岩棚（`crag_ledge`）が立つのは、塞がれたダンジョンの口の
                 * 南 3×3 に塀があった町だけである（`TownPlan::rampart_lowered`）。
                 * 変愚の 5 町では 1 マスも当たらない（実測）ので、一律に求めると必ず落ちる。
                 */
                RampartLow,
            };
            struct Wanted {
                const char *stem;
                //! 変種を持つ素材か（`_01` を付けて 1 枚だけ見る）。
                bool variant;
                Need need;
            };
            const Wanted wanted_rows[] = {
                { "house_wall_se", false, Need::Site },
                { "house_roof_se", false, Need::Site },
                { "fence_n", false, Need::Site },
                { "yard_ground", false, Need::Site },
                { "arch_stone", false, Need::Always },
                { "house_light", false, Need::Site },
                { "palisade", true, Need::Rampart },
                { "watchtower", false, Need::Rampart },
                { "ground_path", true, Need::Always },
                { "ground_turf", true, Need::Always },
                //! 岩山は**意匠を持つ町もある**（旧地獄街道・紅魔館・河童のバザー・偽天棚）。
                //! 下の名前の決め方が「意匠つきがライブラリにあればそちら」なので、旗は立てておく。
                { "crag_low", true, Need::Crag },
                { "crag_peak", true, Need::CragMass },
                { "alpine", true, Need::Crag },
                //! 入口の手前の低い岩棚（2026-08-27）。下げた町でだけ立つ。
                { "crag_ledge", true, Need::RampartLow },
            };
            const TownStyle &style = town_style_for(town.identity.town_id);
            std::vector<std::string> wanted;
            for (const Wanted &row : wanted_rows) {
                if ((row.need == Need::Rampart) && (town.rampart_cells == 0)) {
                    continue;
                }
                if ((row.need == Need::Site) && valley_town) {
                    continue;
                }
                if ((row.need == Need::Crag) && (town.crag_cells == 0)) {
                    continue;
                }
                if ((row.need == Need::CragMass) && (town.crag_cells < kCragMassCells)) {
                    continue;
                }
                if ((row.need == Need::RampartLow) && (town.rampart_lowered == 0)) {
                    continue;
                }
                /*
                 * **名前は置く側と同じ落ち方で決める**（デザイン15・2026-08-20）。
                 * `styled()` も `gather()` も「意匠つきがライブラリにあればそちら、無ければ基の
                 * 名前」なので、検査だけ意匠つきを決め打ちすると**意匠を持たない町で
                 * 「置かれていない」に化ける**（逆に決め打ちを基の名前にすると、
                 * 意匠つきが立っている町でその基の名前が余って FAIL する——偽天棚の
                 * 岩山がそれだった）。
                 */
                const std::string tail = row.variant ? "_01" : "";
                std::string name = std::string(row.stem) + style.suffix + tail;
                if (library.find(name) < 0) {
                    name = std::string(row.stem) + tail;
                }
                /*
                 * **門は表が名指せる**（`TownStyle::gate`。辺境の地の `arch_out`・幻想蛮怒の `arch_kido`）。
                 * `arch_stone` を決め打ちにすると、門を替えた町で「置かれていない」に化ける
                 * （2026-09-05 に辺境の地で踏んだ）。表が門を持ち、ライブラリにあればそちらを数える。
                 */
                if ((std::strcmp(row.stem, "arch_stone") == 0) && (style.gate != nullptr) && (style.gate[0] != '\0')
                    && (library.find(style.gate) >= 0)) {
                    name = style.gate;
                }
                wanted.push_back(name);
            }
            std::fprintf(stderr, "  (6) 意匠 \"%s\"（town %d）\n",
                (style.suffix[0] == '\0') ? "（辺境の地）" : style.suffix, town.identity.town_id);
            std::string missing;
            for (const std::string &name : wanted) {
                const int index = library.find(name);
                const bool placed = (index >= 0) && !view.lib[static_cast<std::size_t>(index)].empty();
                if (!placed) {
                    missing += " ";
                    missing += name;
                }
            }
            /*
             * **店の種別ごとに違う看板が出る**（2026-08-09 に決めた「建物の内容が
             * わかる絵を組もう」）。実データの記号 → `terrain_id` → 表の `sign` を
             * 通しているので、ここが 1 種類しか出なければ対応が切れている。
             */
            int sign_kinds = 0;
            std::string sign_names;
            for (int i = 0; i < library.count(); ++i) {
                const std::string &name = library.entry(i).name;
                if ((name.rfind("sign_", 0) != 0) || view.lib[static_cast<std::size_t>(i)].empty()) {
                    continue;
                }
                ++sign_kinds;
                sign_names += " " + name;
            }
            std::fprintf(stderr, "  (6) 看板の絵 %d 種:%s\n", sign_kinds, sign_names.c_str());
            if (sign_kinds < 4) {
                std::fprintf(stderr, "  (6) **店ごとの看板になっていません**"
                                     "（表の sign か terrain_id の対応が切れている）\n");
                ++failures;
            }
            /*
             * **看板は門をふさがない。**板は常に南を向くので向きはマスで吸収する
             * （南北に開く門は横へ、東西に開く門は 1 つ北へ）。門のマスそのものに
             * 立ってしまうと入口が塞がって見えるので、そこだけは機械で見る。
             */
            int on_gate = 0;
            int signs = 0;
            for (int i = 0; i < library.count(); ++i) {
                if (library.entry(i).name.rfind("sign_", 0) != 0) {
                    continue;
                }
                for (const auto &at : view.lib[static_cast<std::size_t>(i)]) {
                    ++signs;
                    on_gate += (town.role_at(static_cast<int>(std::floor(at.x)),
                                    static_cast<int>(std::floor(at.y)))
                                   == TownRole::Gate)
                        ? 1
                        : 0;
                }
            }
            std::fprintf(stderr, "  (6) 看板 %d 枚 / 門のマスに立ったもの %d 枚\n", signs, on_gate);
            failures += ((signs > 0) && (on_gate == 0)) ? 0 : 1;
            int landmarks = 0;
            for (const char *const name : { "shop", "mill" }) {
                const int index = library.find(name);
                landmarks += ((index >= 0) && !view.lib[static_cast<std::size_t>(index)].empty()) ? 1 : 0;
            }
            std::fprintf(stderr, "  (6) 置き場所 %d 個（目印の建物 %d 棟）%s%s\n", view.lib_instances, landmarks,
                missing.empty() ? "" : "  **置かれなかった素材:**", missing.c_str());
            failures += missing.empty() ? 0 : 1;

            /*
             * (6b) **夜だけ光る。**昼のフレームで自発光が出ていたら、夜の度合いが
             * 効いていない（＝窓が昼も点いている）。
             */
            TerrainView day;
            build_terrain_view(meaning, Frustum::from(camera.view_projection()), day, &memory, &library, &town, 0.f);
            /*
             * 数えるのは**町の灯り**だけ（`lamps` が真のときは `house_light` などの
             * 灯りのプレハブに限る）。全部の自発光を数えていた版はテルモラで落ちた——
             * 町の北東に溶岩が 6 マスあり、**溶岩は昼も光るのが正しい**。
             * 「昼に光っていたら壊れている」のは町の窓と街灯の話である（罠 72）。
             */
            const auto count_glow = [&library](const TerrainView &v, bool lamps_only) {
                int n = 0;
                for (std::size_t i = 0; i < v.lib.size(); ++i) {
                    if (lamps_only) {
                        const std::string &name = library.entry(static_cast<int>(i)).name;
                        if ((name != "house_light") && (name != "lamp_flame")) {
                            continue;
                        }
                    }
                    for (const auto &at : v.lib[i]) {
                        n += ((at.er > 0.01f) || (at.eg > 0.01f) || (at.eb > 0.01f)) ? 1 : 0;
                    }
                }
                return n;
            };
            const int night_glow = count_glow(view, false);
            const int day_glow = count_glow(day, true);
            std::fprintf(stderr, "  (6) 灯り: 夜 %d 個 / 昼の町の灯り %d 個\n", night_glow, day_glow);
            failures += (night_glow > 0) ? 0 : 1;
            failures += (day_glow == 0) ? 0 : 1;
        }
    }

    /*
     * (7) **町は訪れ直しても同じ姿でなければならない**（`FloorIdentity::town_id` を
     * 足した理由。§12-2）。町のフロアは出入りのたびに作り直されて `generated_turn` が
     * 変わるので、地上では turn を種に混ぜていない。
     * **町が違えば必ず変わる**ことも一緒に見る（混ぜていなければ 2 つ目で落ちる）。
     */
    {
        FloorIdentity again = meaning.identity;
        again.generated_turn += 4242; // 町から出て戻った
        FloorIdentity other = meaning.identity;
        other.town_id += 1; // 別の町
        int drifted = 0;
        int same_other = 0;
        for (int gy = 0; gy < meaning.height; ++gy) {
            for (int gx = 0; gx < meaning.width; ++gx) {
                drifted += (cell_seed(gx, gy, meaning.identity) != cell_seed(gx, gy, again)) ? 1 : 0;
                same_other += (cell_seed(gx, gy, meaning.identity) == cell_seed(gx, gy, other)) ? 1 : 0;
            }
        }
        std::fprintf(stderr, "  (7) 訪れ直しで変わった種 %d 個 / 別の町と同じ種 %d 個\n", drifted, same_other);
        failures += (drifted == 0) ? 0 : 1;
        failures += (same_other == 0) ? 0 : 1;
    }

    /*
     * (8) **通り道が門と門を繋いでいる**（2026-08-09 に決めた:「建物と建物をつなぐ
     * 通り道は踏み固められて地面がむき出しに。それ以外は短めの草」）。
     *
     * 見るのは 3 つ: 道と草が両方あること・**どの門の前も道**であること
     * （繋がっていない道は道ではない）・道が町を埋め尽くしていないこと。
     * 検査の検査は `HD2D_BREAK_TOWN=paths`（道を張らない＝全部が草になる）。
     */
    {
        int gates = 0;
        int reached = 0;
        for (const TownSite &site : town.sites) {
            if (!site.has_gate) {
                continue;
            }
            ++gates;
            if (town.role_at(site.gate_x + site.gate_dx, site.gate_y + site.gate_dy) == TownRole::Path) {
                ++reached;
            }
        }
        const int open_cells = town.path_cells + town.turf_cells;
        const float share = (open_cells > 0) ? (static_cast<float>(town.path_cells) / static_cast<float>(open_cells))
                                             : 0.f;
        std::fprintf(stderr, "  (8) 通り道 %d マス / 草 %d マス（開けたマスの %.0f%%）門の前が道 %d/%d\n",
            town.path_cells, town.turf_cells, share * 100.f, reached, gates);
        if ((town.path_cells == 0) || (town.turf_cells == 0)) {
            std::fprintf(stderr, "  (8) **道と草の塗り分けが成立していません**\n");
            ++failures;
        }
        if (reached < gates) {
            std::fprintf(stderr, "  (8) **道の通っていない門があります**（繋がっていない道は道ではない）\n");
            ++failures;
        }
        if (share > 0.5f) {
            std::fprintf(stderr, "  (8) **道が広がりすぎです**（草が主でなくなっている）\n");
            ++failures;
        }
    }

    /*
     * ---- (9) **戻ってきた直後**（既知が少ないとき）でも読み方が変わらないこと ----
     *
     * 2026-08-11 に気づいた:「イークの洞穴で洞窟から地上に戻った際に岩山が
     * 修正前の家になった」。**セーブを読んだ直後は正しい**（岩山 25 マス）ので、
     * 違うのは**そのときに何を知っているか**である。ダンジョンから上がると地上の階は
     * 作り直され、既知はプレイヤの周りの小さな円だけになる（実機の絵のミニマップが
     * ほとんど青＝未探知だった）。
     *
     * ここは**その状態を作って**、階段のまわりの塊が家ではなく岩山のままかを見る。
     * ここまでの検査は全部「地図を全部知っている」前提で、**実機の最初の 1 秒を
     * 1 度も通っていなかった**（罠 112・124 の 3 度目）。
     */
    {
        /*
         * **山に開いた**階段のマスを探す（＝ダンジョンの口）。
         *
         * ここを「最初に見つかった階段」にしていた版は、クエストの入口を階段として
         * 数えるようにした時点（2026-08-12）で意味を失った——モリバントの雑貨屋の
         * 壁に開いたクエストの入口を拾って、**その敷地に家が建っているのを FAIL に
         * していた**（あそこは家が建っていて正しい）。見たいのは「山に開いた口の
         * まわりに家が建たないこと」なので、口の側で絞る。
         */
        int sx = -1;
        int sy = -1;
        for (int gy = 0; (gy < meaning.height) && (sx < 0); ++gy) {
            for (int gx = 0; gx < meaning.width; ++gx) {
                if (meaning.role_at(gx, gy) != CellRole::Stairs) {
                    continue;
                }
                bool in_rock = false;
                for (const auto &[dx, dy] : { std::pair<int, int>{ 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } }) {
                    in_rock = in_rock || meaning.mountain_at(gx + dx, gy + dy);
                }
                if (in_rock) {
                    sx = gx;
                    sy = gy;
                    break;
                }
            }
        }
        if (sx < 0) {
            std::fprintf(stderr, "  (9) 山に開いた階段がこの町のデータに無いので飛ばします\n");
        } else {
            const int crag_full = town.crag_cells;
            //! 半径 `r` の外を未探知にしたフレームを作って読み直す。
            //! `near` / `far` は `windows.h` の遺物マクロなので変数名に使えない（罠 7 の親戚）。
            const auto plan_with_radius = [&](int r, TownPlan &out_plan) {
                GameFrame just_arrived = frame;
                for (int gy = 0; gy < just_arrived.minimap.height; ++gy) {
                    for (int gx = 0; gx < just_arrived.minimap.width; ++gx) {
                        if ((std::abs(gx - sx) <= r) && (std::abs(gy - sy) <= r)) {
                            continue;
                        }
                        just_arrived.minimap.kinds[(static_cast<std::size_t>(gy)
                                                       * static_cast<std::size_t>(just_arrived.minimap.width))
                            + static_cast<std::size_t>(gx)]
                            = static_cast<std::uint8_t>(MinimapKind::Unknown);
                    }
                }
                FloorMeaning arrived_meaning;
                (void)rebuild_floor_meaning(just_arrived, arrived_meaning);
                //! **記憶は渡さない。**素の読み方が壊れていないかを見る所である。
                return rebuild_town_plan(arrived_meaning, out_plan);
            };
            for (const int r : { 2, 3, 5, 8 }) {
                TownPlan near_plan;
                if (!plan_with_radius(r, near_plan)) {
                    std::fprintf(stderr, "  (9) 半径 %d: 町を読めませんでした\n", r);
                    ++failures;
                    continue;
                }
                //! 階段のまわり 1 マスに建物・前庭・門が来ていないか（＝家が建っていないか）。
                int built = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const TownRole role = near_plan.role_at(sx + dx, sy + dy);
                        built += ((role == TownRole::House) || (role == TownRole::Yard)) ? 1 : 0;
                    }
                }
                std::fprintf(stderr, "  (9) 半径 %d マスだけ既知: 岩山 %d マス / 階段のまわりの建物 %d マス\n",
                    r, near_plan.crag_cells, built);
                if (built > 0) {
                    std::fprintf(stderr, "  (9) **ダンジョンの口に家が建ちました**"
                                         "（既知が少ないと読み方が変わる）\n");
                    ++failures;
                }
            }
            std::fprintf(stderr, "  (9) 全部知っているとき: 岩山 %d マス\n", crag_full);
        }
    }

    /*
     * ---- (10) **岩山になったマスは、データが山だと言っているマスだけ** ----
     *
     * 2026-08-12 に気づいた:「イークの洞窟が家になるバグを潰したが、その影響で
     * モリバントの雑貨屋が岩山になっている」。前の直しは「階段を抱えた壁の塊は山」と
     * 読んでいて、**クエストの入口も階段である**ことを見落としていた
     * （`town_plan.cpp` の `crag_mass` に経緯）。
     *
     * ここが見るのは規則そのものである——**岩山と読んだマスの地形は山か。**
     * 材料は `load_town_frame` が実データから組んだミニマップ（`MinimapKind::Mountain`）で、
     * 町の読み方が使ったものと同じものを別の向きから突き合わせる。
     *
     * @note **山塊の町（ズル）は除く。**あの町は「地図の縁で山が切れると山の外が見える」
     * ので、枠の石マス 524 マスをわざと山に含めている（`TownRole::Crag` の注記）。
     */
    {
        if (town.crag_cells >= kCragMassCells) {
            std::fprintf(stderr, "  (10) 山塊の町（岩山 %d マス）なので飛ばします"
                                 "（枠の石マスをわざと山に含めている）\n",
                town.crag_cells);
        } else {
            int not_mountain = 0;
            std::string where;
            for (int gy = 0; gy < town.height; ++gy) {
                for (int gx = 0; gx < town.width; ++gx) {
                    if (town.role_at(gx, gy) != TownRole::Crag) {
                        continue;
                    }
                    if (frame.minimap.at(gx, gy) == MinimapKind::Mountain) {
                        continue;
                    }
                    ++not_mountain;
                    if (where.size() < 100) {
                        char buf[24]{};
                        std::snprintf(buf, sizeof(buf), " (%d,%d)", gx, gy);
                        where += buf;
                    }
                }
            }
            std::fprintf(stderr, "  (10) 岩山 %d マス / データが山でないもの %d マス%s\n",
                town.crag_cells, not_mountain, where.c_str());
            if (not_mountain > 0) {
                std::fprintf(stderr, "  (10) **山でない塊を岩山にしています**"
                                     "（クエストの入口に接した敷地や城壁が岩山に化けていないか）\n");
                ++failures;
            }
        }
    }

    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

} // namespace hd2d
