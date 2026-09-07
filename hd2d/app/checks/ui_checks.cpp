/*!
 * @file ui_checks.cpp
 * @brief 画面まわりの検査（`--ui-check`）。窓と GL が要る。
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
#include "ui/click_path.h" //!< `minimap_walkable()`
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

/*!
 * @brief UI の検査（`--ui-check`）。**コアを起こさない。**
 *
 * @details 見るのは 2 つある。
 * 1. **矩形**（3 つの作り × 3 つの窓の大きさ）: 3D の矩形が空でないこと・
 *    どの板も窓からはみ出さないこと・**板どうしが重ならない**こと・
 *    `Split` では 3D の矩形とパネルが重ならないこと
 * 2. **画素**: 空でないパネルが**実際に描かれた**こと
 *
 * 1 だけでは足りない。矩形の計算が正しくても `draw_game_ui()` が何も描かなければ
 * 画面は真っ黒で、それを検出できない検査は無いより悪い（計画 §0-2）。
 * 逆に 2 だけでも足りない（`Full` は全部が重なっていても画素は出る）。
 */
int run_ui_check(const AppOptions &options)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[hd2d] SDL の初期化に失敗しました。\n");
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
    UiPaint paint;
    //! カットインの板を絵に差し替えたときの貼り先（(8) が渡す。いまは 1 枚も無い）。
    UiImagePainter images;
    if (!text.init(kTextPx, err) || !paint.init(err) || !images.init(err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    int screen_w = options.window_w;
    int screen_h = options.window_h;
    /*
     * **窓の大きさと描画面の大きさは同じとは限らない**（`SDL_WINDOW_ALLOW_HIGHDPI`）。
     * GL が塗るのは描画面のほうなので、窓の大きさで配置を組むと**右と下に空白が残る**。
     * ここは両方を測って食い違いを出す（実機で踏んだ症状の出どころ候補）。
     */
    if (options.ui_check_fullscreen) {
        (void)SDL_SetWindowFullscreen(window.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        //! 大きさが落ち着くまでイベントを回す（`SDL_SetWindowFullscreen` は即座には反映されない）。
        for (int i = 0; i < 60; ++i) {
            SDL_Event event;
            while (SDL_PollEvent(&event) != 0) {
                // 読み捨て（ここで見たいのは大きさだけ）
            }
            SDL_Delay(8);
        }
    }
    int window_w = 0;
    int window_h = 0;
    int drawable_w = 0;
    int drawable_h = 0;
    SDL_GetWindowSize(window.window, &window_w, &window_h);
    SDL_GL_GetDrawableSize(window.window, &drawable_w, &drawable_h);
    screen_w = drawable_w;
    screen_h = drawable_h;

    GameFrame frame = make_synthetic_frame(23, 19, 100, 44);
    fill_synthetic_ui(frame);

    int failures = 0;
    const auto fail = [&failures](const std::string &what) {
        std::fprintf(stderr, "  **FAIL** %s\n", what.c_str());
        ++failures;
    };

    const LayoutMode modes[] = { LayoutMode::Full, LayoutMode::Hybrid, LayoutMode::Split, LayoutMode::Tall };
    /*!
     * 窓の大きさは実測のものを混ぜる（記憶 `hengband-capture-is-2560x1080`）。
     * **縦長も 1 つ入れる**（2026-08-12 に決めた で縦持ちの作りが増えた。
     * 横長だけで回していると、縦でだけ潰れる矩形をこの検査は一度も通さない）。
     */
    const int sizes[][2] = { { screen_w, screen_h }, { 1280, 720 }, { 2560, 1080 }, { 1080, 1920 } };

    std::fprintf(stderr, "[hd2d] ui-check  文字のマス %dx%d  3D を広げる %dpx%s\n",
        text.cell_w(), text.cell_h(), options.grow_scene, options.ui_check_fullscreen ? "  （全画面）" : "");
    std::fprintf(stderr, "  窓 %dx%d / 描画面 %dx%d%s\n", window_w, window_h, drawable_w, drawable_h,
        ((window_w == drawable_w) && (window_h == drawable_h)) ? "" : "  **食い違っています**");
    if ((window_w != drawable_w) || (window_h != drawable_h)) {
        //! 食い違ったまま窓の大きさで組むと、画面の右と下に空白が残る。
        fail("窓の大きさと描画面の大きさが違います（配置は描画面で組むこと）");
    }

    /* ---------------------------------------------------- (1) 矩形 */
    for (const LayoutMode mode : modes) {
        for (const bool subs_open : { false, true }) {
            for (const auto &size : sizes) {
                const UiLayout layout = UiLayout::compute(mode, size[0], size[1],
                    text.cell_w(), text.cell_h(), subs_open);
                const std::string tag = std::string(layout_mode_name(mode)) + (subs_open ? "+subs" : "")
                    + " " + std::to_string(size[0]) + "x" + std::to_string(size[1]);
                const RectPx window_rect{ 0, 0, size[0], size[1] };

                RectPx scene = layout.scene;
                if (options.grow_scene > 0) {
                    scene = scene.inset(-options.grow_scene); // **検査の検査**（広げる）
                }
                if (scene.empty()) {
                    fail(tag + ": 3D の矩形が空です");
                }

                // パネルの一覧。**ここに挙げたものは互いに重なってはいけない。**
                struct Named {
                    const char *name;
                    RectPx rect;
                    /*!
                     * @brief 3D の上に**乗せるのが仕様**の板か。
                     * @details ミニマップは 3 つの作りとも地図の隅に重ねる（そこに置くのが
                     * いちばん読みやすいので）。「重なってはいけない」に混ぜると
                     * `Split` で必ず落ちる検査になる。**パネルどうしの重なりは別に見る。**
                     */
                    bool over_scene;
                };
                std::vector<Named> panels;
                panels.push_back(Named{ "status_col", layout.status_col, false });
                panels.push_back(Named{ "hud", layout.hud, true });
                panels.push_back(Named{ "message_bar", layout.message_bar, false });
                panels.push_back(Named{ "prompt_bar", layout.prompt_bar, true });
                panels.push_back(Named{ "bottom_bar", layout.bottom_bar, false });
                panels.push_back(Named{ "minimap", layout.minimap, true });
                for (int i = 0; i < kUiSubPanels; ++i) {
                    panels.push_back(Named{ "sub", layout.sub[i], false });
                }
                for (const auto &panel : panels) {
                    if (panel.rect.empty()) {
                        continue;
                    }
                    if (rect_overlap_area(panel.rect, window_rect)
                        != (static_cast<long long>(panel.rect.w) * static_cast<long long>(panel.rect.h))) {
                        fail(tag + ": " + panel.name + " が窓からはみ出しています");
                    }
                }
                for (std::size_t a = 0; a < panels.size(); ++a) {
                    for (std::size_t b = a + 1; b < panels.size(); ++b) {
                        const long long overlap = rect_overlap_area(panels[a].rect, panels[b].rect);
                        if (overlap > 0) {
                            fail(tag + ": " + panels[a].name + " と " + panels[b].name + " が重なっています（"
                                + std::to_string(overlap) + " 画素）");
                        }
                    }
                }
                /*
                 * 重ならない作り（`Split` / `Tall`）は 3D の矩形もパネルと重なってはいけない。
                 * `Full` / `Hybrid` は**重ねるのが仕様**（3D が窓いっぱいで、パネルはその上に乗る）
                 * なので、ここを一律に見ると常に落ちる検査になる。
                 * **作りの名前ではなく `sub_overlaid` で分ける**（作りが増えるたびに
                 * 綴りを足して回ると、足し忘れた作りだけが検査を素通りする）。
                 */
                if (!layout.sub_overlaid) {
                    for (const auto &panel : panels) {
                        if (panel.over_scene) {
                            continue; // 乗せるのが仕様のもの（ミニマップ・ゲージ）
                        }
                        const long long overlap = rect_overlap_area(scene, panel.rect);
                        if (overlap > 0) {
                            fail(tag + ": 3D の矩形と " + panel.name + " が重なっています（"
                                + std::to_string(overlap) + " 画素）");
                        }
                    }
                }
                if (rect_overlap_area(scene, window_rect)
                    != (static_cast<long long>(scene.w) * static_cast<long long>(scene.h))) {
                    fail(tag + ": 3D の矩形が窓からはみ出しています");
                }
                /*
                 * **ミニマップが地図の真ん中に被らないこと。**カメラは自機を追うので、
                 * 地図の枠の中心＝**自機の居る所**である。ここが隠れると「自分が見えない」
                 * ——実機の縦持ちで踏んだ症状（2026-08-12。地図の枠が短く狭い縦では
                 * 同じ式でも枠の 6 割を覆っていた）。重なりの規則では防げない
                 * （ミニマップは地図に重ねるのが仕様なので `over_scene` で除外されている）。
                 */
                if (!layout.minimap.empty()
                    && layout.minimap.contains(scene.x + (scene.w / 2), scene.y + (scene.h / 2))) {
                    fail(tag + ": ミニマップが地図の中心（自機の居る所）に被っています");
                }
                //! @note ここは既定（右上）で回している。5 つの位置は (1'') で別に掃く
                //! ——**画面中央は「被るのが仕様」**なので、この規則をそのまま当てられない。

                /*
                 * **重ならない作り（`Split` / `Tall`）は画面を隙間なく敷き詰めること。**
                 * 「重ならない」と「隙間が無い」は**別の話**である。重なりだけを見ていると、
                 * 右と下に空白が残る配置が素通りする（実機で踏んだ症状）。
                 * 重なる作りは 3D が窓いっぱいなので、この規則の対象ではない。
                 * @note ここは `reserve_pad` を渡していない（＝パッドに場所を譲っていない）
                 * ので `Tall` も画面いっぱいを覆う。譲った状態は下の (1') で別に見る。
                 */
                if (!layout.sub_overlaid) {
                    long long covered = 0;
                    covered += static_cast<long long>(scene.w) * static_cast<long long>(scene.h);
                    for (const auto &panel : panels) {
                        if (panel.over_scene) {
                            continue; // 3D の上に乗せるもの（二重に数えない）
                        }
                        covered += static_cast<long long>(panel.rect.w) * static_cast<long long>(panel.rect.h);
                    }
                    const long long whole = static_cast<long long>(size[0]) * static_cast<long long>(size[1]);
                    if (covered != whole) {
                        fail(tag + ": 画面を敷き詰めていません（覆えたのは " + std::to_string(covered) + " / "
                            + std::to_string(whole) + " 画素。差 " + std::to_string(whole - covered) + "）");
                    }
                }
            }
        }
    }
    std::fprintf(stderr, "  (1) 矩形: %d 通り（%zu つの作り × 開閉 × %zu の大きさ）\n",
        static_cast<int>(std::size(modes)) * 2 * static_cast<int>(std::size(sizes)), std::size(modes),
        std::size(sizes));

    /* ------------------------- (1'') ミニマップの表示位置と表示サイズ（2026-08-19 に決めた） */
    {
        /*
         * 指示は「表示位置（右上、右下、左上、左下、画面中央）／表示サイズ／表示縮尺／
         * 表示濃度／方向表示」。このうち**レイアウトに効くのは位置とサイズ**なので、
         * ここで 5 つ × 3 段の大きさを掃く。縮尺・濃度・方向表示は描く側だけの話で、
         * 矩形が動かないため (2) の画素の検査で見る。
         *
         * 見るのは 3 つ:
         *   1. **窓からはみ出さない**（大きくしても）
         *   2. **地図（`scene`）の中に収まる**——ミニマップは地図に重ねるものなので、
         *      枠の外へ出たらパネルの下に潜って読めない
         *   3. **選んだ隅に居る**（右上を選んだのに左に出る、が起きない）
         *
         * **「地図の中心に被らない」は掛けない。**`Centre` は被るのが仕様である
         * （中央を選べるようにと決めたので、被りは意図された結果）。
         */
        struct CornerCase {
            MinimapCorner corner;
            const char *name;
            bool right;
            bool bottom;
        };
        const CornerCase corners[] = {
            { MinimapCorner::TopRight, "右上", true, false },
            { MinimapCorner::BottomRight, "右下", true, true },
            { MinimapCorner::TopLeft, "左上", false, false },
            { MinimapCorner::BottomLeft, "左下", false, true },
            { MinimapCorner::Centre, "画面中央", false, false },
        };
        for (const LayoutMode mode : { LayoutMode::Full, LayoutMode::Hybrid, LayoutMode::Split, LayoutMode::Tall }) {
            for (const int pct : { 50, 100, 200 }) {
                for (const CornerCase &cc : corners) {
                    const UiLayout probe = UiLayout::compute(mode, 1600, 900, text.cell_w(), text.cell_h(), true,
                        SubSplit{}, 0, 0, false, false, cc.corner, pct);
                    const std::string tag = std::string("(1'') ") + layout_mode_name(mode) + " "
                        + std::to_string(pct) + "% " + cc.name;
                    if (probe.minimap.empty()) {
                        //! 置けないほど狭いときは**置かない**のが従来からの約束（黙って重ねない）。
                        continue;
                    }
                    const RectPx win{ 0, 0, 1600, 900 };
                    const long long area
                        = static_cast<long long>(probe.minimap.w) * static_cast<long long>(probe.minimap.h);
                    if (rect_overlap_area(probe.minimap, win) != area) {
                        fail(tag + ": ミニマップが窓からはみ出しています");
                    }
                    if (rect_overlap_area(probe.minimap, probe.scene) != area) {
                        fail(tag + ": ミニマップが 3D の枠からはみ出しています");
                    }
                    /*
                     * **置いてよい領域から出ていないこと。**重なる作りでは、常時出るものと
                     * 開いたサブパネルの外側だけが使える（`UiLayout::minimap_area`）。
                     * ここを `scene` で見るとパネルの下へ潜っても通ってしまう。
                     */
                    if (rect_overlap_area(probe.minimap, probe.minimap_area) != area) {
                        fail(tag + ": ミニマップが置いてよい領域からはみ出しています");
                    }
                    /*
                     * **選んだ隅に居るか。**枠の中心が、地図の枠の中心より右か左か・
                     * 下か上かで見る（画素の余白まで当てにいくと、マスの端数で落ちる検査になる）。
                     */
                    /*
                     * **基準は「置いてよい領域」**（`minimap_area`）であって窓でも `scene` でもない。
                     * 最初に `scene` で書いて、サブパネルを開いた形で軒並み落ちた
                     * ——落ちていたのは実装ではなく検査の基準だった。
                     */
                    const RectPx &area_rect = probe.minimap_area;
                    const int mid_x = probe.minimap.x + (probe.minimap.w / 2);
                    const int mid_y = probe.minimap.y + (probe.minimap.h / 2);
                    const int area_mid_x = area_rect.x + (area_rect.w / 2);
                    const int area_mid_y = area_rect.y + (area_rect.h / 2);
                    if (cc.corner == MinimapCorner::Centre) {
                        /*
                         * **メインマップのど真ん中に居ること**（2026-08-19 に決めた）。
                         * 基準は `scene` の中心。許すずれはマス 1 つぶんだけ——「だいたい中央」では
                         * 指示を満たしていない。**位置は動かさず大きさで合わせる**実装なので、
                         * サブパネルを開いていても中心はここから動かないはずである。
                         */
                        const int scene_mid_x = probe.scene.x + (probe.scene.w / 2);
                        const int scene_mid_y = probe.scene.y + (probe.scene.h / 2);
                        const int slack = std::max(2, text.cell_w());
                        if ((std::abs(mid_x - scene_mid_x) > slack) || (std::abs(mid_y - scene_mid_y) > slack)) {
                            fail(tag + ": 画面中央を選んだのにメインマップのど真ん中に居ません（中心 "
                                + std::to_string(mid_x) + "," + std::to_string(mid_y) + " / 地図の中心 "
                                + std::to_string(scene_mid_x) + "," + std::to_string(scene_mid_y) + "）");
                        }
                        (void)area_mid_x;
                        (void)area_mid_y;
                    } else {
                        /*
                         * **枠が領域をほぼ埋めているときは左右・上下を問わない。**
                         * 寄せる余地が無いので、どちら寄りかを聞いても意味が無い
                         * （200% で実際に埋まる）。
                         */
                        const bool fills_x = (probe.minimap.w * 2) > area_rect.w;
                        const bool fills_y = (probe.minimap.h * 2) > area_rect.h;
                        if (!fills_x && (cc.right != (mid_x > area_mid_x))) {
                            fail(tag + ": 左右が選んだ側と違います");
                        }
                        /*
                         * 上下は**ゲージ避けの押し下げ**（`place_minimap` の後半）で
                         * 動くことがあるので、下を選んだときだけ「中心より上に居ない」を見る。
                         */
                        if (!fills_y && cc.bottom && (mid_y <= area_mid_y)) {
                            fail(tag + ": 下を選んだのに上に出ています");
                        }
                    }
                }
            }
        }
        /*
         * **サイズが実際に効くこと**（検査の検査に近い一手）。50% と 200% で
         * 枠の面積が変わらなければ、設定が繋がっていないということである。
         */
        const UiLayout small = UiLayout::compute(LayoutMode::Full, 1600, 900, text.cell_w(), text.cell_h(), true,
            SubSplit{}, 0, 0, false, false, MinimapCorner::TopRight, 50);
        const UiLayout big = UiLayout::compute(LayoutMode::Full, 1600, 900, text.cell_w(), text.cell_h(), true,
            SubSplit{}, 0, 0, false, false, MinimapCorner::TopRight, 200);
        std::fprintf(stderr, "  (1'') 表示サイズ 50%%=%dx%d / 200%%=%dx%d\n", small.minimap.w, small.minimap.h,
            big.minimap.w, big.minimap.h);
        if ((small.minimap.w >= big.minimap.w) || small.minimap.empty() || big.minimap.empty()) {
            fail("(1'') 表示サイズが効いていません（50% と 200% で枠が同じ）");
        }
        /*
         * **既定は従来と同じ枠であること。**位置とサイズを足したことで、
         * 何も設定していない人の絵が動いてはいけない（必守制約 4）。
         */
        const UiLayout legacy = UiLayout::compute(LayoutMode::Full, 1600, 900, text.cell_w(), text.cell_h(), true);
        const UiLayout defaulted = UiLayout::compute(LayoutMode::Full, 1600, 900, text.cell_w(), text.cell_h(), true,
            SubSplit{}, 0, 0, false, false, MinimapCorner::TopRight, 100);
        if ((legacy.minimap.x != defaulted.minimap.x) || (legacy.minimap.y != defaulted.minimap.y)
            || (legacy.minimap.w != defaulted.minimap.w) || (legacy.minimap.h != defaulted.minimap.h)) {
            fail("(1'') 既定（右上・100%）が従来の枠と違います");
        }
    }

    /* ------------------------------------ (1') 縦持ちでパッドに場所を譲る */
    {
        /*
         * 2026-08-12 に決めた「右サブパネル無し・メインマップと下サブパネル 3 つの構成を
         * 追加。Android 縦持ちはこれをデフォルトに」＋参考画像 `VirtualPad_V.jpg`。
         * 確かめるのは 3 つ:
         *   1. 縦持ちの `Tall` では**下がパッドのぶん空く**（`content` が画面より低い）
         *   2. その空いた所へ**パネルが食み出さない**（食み出すとパッドの下に隠れて読めない）
         *   3. **横持ちでは空かない**（横のパッドは画面いっぱいに広がるので、譲る意味が無い）
         */
        const UiLayout port = UiLayout::compute(LayoutMode::Tall, 1080, 1920, text.cell_w(), text.cell_h(),
            false, SubSplit{}, 0, 0, true);
        if (port.content.h >= 1920) {
            fail("縦持ちの Tall: バーチャルパッドのための場所が空いていません");
        }
        const RectPx boxes[] = { port.scene, port.status_col, port.sub[0], port.sub[1], port.sub[2],
            port.bottom_bar };
        for (const RectPx &r : boxes) {
            if (!r.empty() && ((r.y + r.h) > (port.content.y + port.content.h))) {
                fail("縦持ちの Tall: パネルがバーチャルパッドの領域へ食み出しています");
            }
        }
        for (int i = 0; i < kSubRightMax; ++i) {
            if (!port.sub[static_cast<std::size_t>(kSubRightSlot + i)].empty()) {
                fail("縦持ちの Tall: 右のサブパネルが空になっていません");
            }
        }
        const UiLayout land = UiLayout::compute(LayoutMode::Tall, 1920, 1080, text.cell_w(), text.cell_h(),
            false, SubSplit{}, 0, 0, true);
        if (land.content.h != 1080) {
            fail("横持ちの Tall: パッドのために場所を譲ってはいけません");
        }
        /*
         * ミニマップの大きさ（2026-08-12 に決めた「Android 版縦持ちのみ枠を今の 2/3 に」）。
         * **数で見る**——「小さくなった」ではなく「**縦は横の 2/3**」であること。
         * 同じ枠の形で比べないと意味が無いので、**同じ 1080×1920 を横向きに寝かせた
         * 1920×1080 ではなく**、縦の枠から `place_minimap` が出す値を、
         * 縮めない場合の理屈値と突き合わせる。
         */
        const int port_side = port.minimap.w;
        const int free_side = std::min(port.scene.w, port.scene.h) * 2 / 3;
        const int want = std::clamp(((free_side * 2) / 3), (text.cell_h() * 16 * 2) / 3,
            (text.cell_h() * 36 * 2) / 3);
        if (!port.minimap.empty() && (port_side != want)) {
            fail("縦持ちのミニマップが 2/3 になっていません（" + std::to_string(port_side) + " ≠ "
                + std::to_string(want) + "）");
        }
        std::fprintf(stderr, "  (1') 縦持ちの場所取り: 絵に使える高さ %d/1920（横持ちは %d/1080）"
                             " / ミニマップ %dx%d（地図 %dx%d の中）\n",
            port.content.h, land.content.h, port.minimap.w, port.minimap.h, port.scene.w, port.scene.h);
    }

    /* ---------------------------------------------------- (2) 画素 */
    const unsigned char clear[3] = { 5, 5, 8 };
    for (const LayoutMode mode : modes) {
        const bool subs_open = (mode != LayoutMode::Split);
        HudState state;
        state.subs_open = subs_open;
        /*
         * **バーチャルパッドを出した状態で組む**（2026-08-12 に決めた で縦持ちの
         * `Tall` は下をパッドに譲る）。譲るのは「`Tall` かつ縦向き」だけなので、
         * 横長の窓で回すぶんには今までと同じ矩形になる。
         */
        const UiLayout layout = UiLayout::compute(mode, screen_w, screen_h,
            text.cell_w(), text.cell_h(), subs_open, SubSplit{}, 0, 0, true);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screen_w, screen_h);
        glClearColor(static_cast<float>(clear[0]) / 255.f, static_cast<float>(clear[1]) / 255.f,
            static_cast<float>(clear[2]) / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        /*
         * **本番と同じ状態にしてから描く。**実際の駆動ループでは、UI を描く直前に
         * ポスト処理が走り、`glViewport` を 3D の矩形に設定したまま戻さない。
         * ここで窓いっぱいに張り直してから描いていたせいで、
         * 「UI が丸ごと 3D の矩形へ押し込まれる」不具合を**この検査は一度も通さなかった**
         * （実機で見つけた）。わざと 3D の矩形を張ってから描く。
         */
        glViewport(layout.scene.x, screen_h - layout.scene.y - layout.scene.h, layout.scene.w, layout.scene.h);
        paint.begin(screen_w, screen_h);
        text.begin(screen_w, screen_h);
        draw_game_ui(paint, text, layout, frame, state);
        /*
         * **契約そのものを見る。**「何か描かれたか」だけを数えると、文字側が
         * viewport を張り直すぶんで下敷き側の取り違えが隠れる（実際に隠れた）。
         * `flush()` は自分で窓いっぱいを張る、というのがこの 2 つの約束である。
         */
        const auto viewport_is_whole = [&](const char *who) {
            GLint viewport[4]{};
            glGetIntegerv(GL_VIEWPORT, viewport);
            if ((viewport[0] != 0) || (viewport[1] != 0) || (viewport[2] != screen_w) || (viewport[3] != screen_h)) {
                fail(std::string(who) + ": flush() が viewport を窓いっぱいに張っていません（"
                    + std::to_string(viewport[0]) + "," + std::to_string(viewport[1]) + ","
                    + std::to_string(viewport[2]) + "," + std::to_string(viewport[3]) + "）");
            }
        };
        paint.flush();
        viewport_is_whole("UiPaint");
        //! 次の `flush()` が「前が張った」に頼っていないことを見るため、**わざと崩してから**呼ぶ。
        glViewport(layout.scene.x, screen_h - layout.scene.y - layout.scene.h, layout.scene.w, layout.scene.h);
        text.flush();
        viewport_is_whole("TextOverlay");
        const std::vector<unsigned char> image = read_framebuffer(screen_w, screen_h);

        struct Named {
            const char *name;
            RectPx rect;
        };
        std::vector<Named> want;
        want.push_back(Named{ "status_col", layout.status_col });
        want.push_back(Named{ "hud", layout.hud });
        want.push_back(Named{ "message_bar", layout.message_bar });
        want.push_back(Named{ "prompt_bar", layout.prompt_bar });
        want.push_back(Named{ "bottom_bar", layout.bottom_bar });
        want.push_back(Named{ "minimap", layout.minimap });
        for (int i = 0; i < kUiSubPanels; ++i) {
            want.push_back(Named{ "sub", layout.sub[i] });
        }
        std::size_t total = 0;
        for (const auto &area : want) {
            if (area.rect.empty()) {
                continue;
            }
            const std::size_t painted = count_painted_pixels(image, screen_w, screen_h, area.rect, clear);
            total += painted;
            if (painted == 0) {
                fail(std::string(layout_mode_name(mode)) + ": " + area.name + " に何も描かれていません");
            }
        }
        std::fprintf(stderr, "  (2) %-6s 描かれた画素 %zu\n", layout_mode_name(mode), total);
        /*
         * `--shot=` を付けたら**作りごとに 1 枚ずつ**書き出す（`<shot>_full.bmp` など）。
         * 3 つのうちどれを既定にするかは絵を見ないと決められない、というのがこの段の要点なので、
         * **並べて見る手段が無いと話が始まらない**（P7 の `--post=` と同じ考え方）。
         */
        if (!options.shot_path.empty()) {
            /*
             * **絵にはバーチャルパッドも重ねる**（2026-08-12 に決めた の縦持ちは
             * 「画面構成 ＋ パッドの既定配置」で 1 組。片方だけ見ても詰められない）。
             * **画素を数え終わってから**描く——先に重ねると、パッドの下になったパネルが
             * 「何か描かれている」ことになって、空の板を見逃す。
             */
            VirtualPadSettings vps;
            vps.show = true;
            VirtualPad vpad_preview;
            const RectPx whole{ 0, 0, screen_w, screen_h };
            paint.begin(screen_w, screen_h);
            text.begin(screen_w, screen_h);
            //! 割り当ても登録も空でよい（名前が引けないボタンは綴りのまま出る＝配置は見える）。
            vpad_preview.draw(paint, text, vps, whole, PadMacroFlags{}, 0, PadBinds{}, std::vector<PadCommand>{});
            paint.flush();
            text.flush();
            std::string path = options.shot_path;
            const std::size_t dot = path.find_last_of('.');
            const std::string suffix = std::string("_") + layout_mode_name(mode);
            path = (dot == std::string::npos) ? (path + suffix) : (path.substr(0, dot) + suffix + path.substr(dot));
            (void)save_framebuffer_bmp(path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);
    }

    /* ------------------------- (2-b) HUD の `SP` の名札はコアが決める（SH-08 / W5） */
    {
        /*
         * **画面は「魔力」も「声」も知らない**（必守制約 1）。名札はコアが
         * `hud.sp_label` で運び、空なら画面の既定 `SP` に落ちる。
         *
         * 見るのは 2 つ:
         *
         * 1. **空の名札が、`"SP"` を送ったときと 1 画素も違わないこと。**
         *    これが「変愚・幻想蛮怒・短愚蛮怒の画は変わらない」の証拠である
         *    （名札の桁も測り直しているので、そこがずれたら画がずれる）。
         * 2. **名札を変えると画が変わること。** 変わらなければ運んだ値が
         *    どこにも届いていない＝ベタ書きのままである。
         */
        const UiLayout gauge_layout = UiLayout::compute(LayoutMode::Full, screen_w, screen_h,
            text.cell_w(), text.cell_h(), true, SubSplit{}, 0, 0, true);
        const auto shoot_hud = [&](const std::string &label) {
            GameFrame f = frame;
            f.hud.sp_label = label;
            HudState st;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, screen_w, screen_h);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            paint.begin(screen_w, screen_h);
            text.begin(screen_w, screen_h);
            draw_game_ui(paint, text, gauge_layout, f, st);
            paint.flush();
            text.flush();
            return read_framebuffer(screen_w, screen_h);
        };
        const std::vector<unsigned char> empty_label = shoot_hud(std::string());
        const std::vector<unsigned char> spelled_sp = shoot_hud("SP");
        const std::vector<unsigned char> voice = shoot_hud("Voice");
        const std::size_t same = count_changed_pixels(empty_label, spelled_sp,
            screen_w, screen_h, gauge_layout.hud);
        const std::size_t moved = count_changed_pixels(empty_label, voice,
            screen_w, screen_h, gauge_layout.hud);
        std::fprintf(stderr, "  (2-b) SP の名札: 空と\"SP\"の差 %zu 画素 / \"Voice\"との差 %zu 画素\n",
            same, moved);
        if (same != 0) {
            fail("(2-b) HUD: 名札が空のときの画が `SP` と違います"
                 "（名札を送らないコアの画が変わってしまいます。SH-08）");
        }
        if (moved == 0) {
            fail("(2-b) HUD: 名札を変えても画が変わりません（ベタ書きのままです。SH-08）");
        }
        if (!options.shot_path.empty()) {
            //! 人が見て確かめる用（Sil-Q が送ってくる `Voice` の画）。ほかの段と同じ流儀。
            (void)shoot_hud("Voice");
            std::string path = options.shot_path;
            const std::size_t dot = path.find_last_of('.');
            path = (dot == std::string::npos) ? (path + "_splabel")
                                              : (path.substr(0, dot) + "_splabel" + path.substr(dot));
            (void)save_framebuffer_bmp(path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);
    }

    /* ------------------------- (2'') ミニマップの自分（一人称は向きのある三角） */
    {
        /*
         * 2026-08-11 に決めた「FPS モードの時のみ、ミニマップのキャラクターを
         * 輝点ではなく三角にし、向いている方向に三角のさきを向くように」。
         *
         * 見るのは 2 つ:
         *   1. 一人称のとき、輝点のときと**絵が変わる**（三角の道を通っている）
         *   2. **方位を変えたら絵も変わる**（向きが効いている）
         * 1 だけだと「向きに関係なく同じ三角」でも通ってしまう。
         */
        const GameFrame walk = make_synthetic_frame(23, 19, 100, 44);
        const UiLayout mini_layout = UiLayout::compute(LayoutMode::Hybrid, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false);
        const auto shoot = [&](bool first_person, float facing) {
            HudState state;
            state.first_person = first_person;
            state.first_person_facing = facing;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, screen_w, screen_h);
            glClearColor(static_cast<float>(clear[0]) / 255.f, static_cast<float>(clear[1]) / 255.f,
                static_cast<float>(clear[2]) / 255.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            paint.begin(screen_w, screen_h);
            text.begin(screen_w, screen_h);
            draw_game_ui(paint, text, mini_layout, walk, state);
            paint.flush();
            text.flush();
            return read_framebuffer(screen_w, screen_h);
        };
        const std::vector<unsigned char> dot = shoot(false, 0.f);
        const std::vector<unsigned char> north = shoot(true, 0.f);
        const std::vector<unsigned char> east = shoot(true, 1.5707963f); //!< 東（時計回りに 90°）
        const RectPx &mini = mini_layout.minimap;
        const std::size_t vs_dot = count_changed_pixels(dot, north, screen_w, screen_h, mini);
        const std::size_t vs_turn = count_changed_pixels(north, east, screen_w, screen_h, mini);
        if (vs_dot == 0) {
            fail("ミニマップ: 一人称でも輝点のままです（三角になっていません）");
        }
        if (vs_turn == 0) {
            fail("ミニマップ: 方位を変えても三角の向きが変わりません");
        }
        /*
         * **360 度まわること**（2026-08-11 に決めた「FPS モードのミニマップの三角は
         * 360 度回転で表示させたい」）。8 方位へ丸めた実装でも上の 2 つは通ってしまうので、
         * **刻みの間の角**（22.5° ずらした 16 方位）で 1 周ぶん撮り、
         * **どれも隣と違う絵になる**ことを見る。丸めがあると隣同士が同じ絵になる。
         */
        {
            constexpr int kSteps = 16;
            std::vector<std::vector<unsigned char>> turned;
            turned.reserve(kSteps);
            for (int i = 0; i < kSteps; ++i) {
                //! 22.5° ずらして始める（0/90/180/270 のような「乗っている」角を避ける）。
                const float facing = ((static_cast<float>(i) + 0.5f) * 6.2831853f) / static_cast<float>(kSteps);
                turned.push_back(shoot(true, facing));
            }
            int same_pairs = 0;
            for (int i = 0; i < kSteps; ++i) {
                const int next = (i + 1) % kSteps;
                if (count_changed_pixels(turned[static_cast<std::size_t>(i)],
                        turned[static_cast<std::size_t>(next)], screen_w, screen_h, mini)
                    == 0) {
                    ++same_pairs;
                }
            }
            if (same_pairs > 0) {
                fail("ミニマップ: 三角が 360 度まわっていません（16 方位のうち "
                    + std::to_string(same_pairs) + " 組が隣と同じ絵です。刻みへ丸めていませんか）");
            }
        }
        std::fprintf(stderr, "  (2'') ミニマップの自分: 輝点→三角 %zu 画素 / 北→東 %zu 画素 / 16 方位すべて別の絵\n",
            vs_dot, vs_turn);
        if (!options.shot_path.empty()) {
            /*
             * **刻みに乗らない角**で 1 枚（`_minimap`）。0/90/180/270 で撮ると、
             * 8 方位へ丸めた実装でも同じ絵になってしまい、目で見る意味が薄い。
             */
            (void)shoot(true, 0.9f); //!< 約 51.6°（北東と東の間）
            std::string path = options.shot_path;
            const std::size_t dot_at = path.find_last_of('.');
            path = (dot_at == std::string::npos) ? (path + "_minimap")
                                                 : (path.substr(0, dot_at) + "_minimap" + path.substr(dot_at));
            (void)save_framebuffer_bmp(path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);
    }

    /* ------------------------- (2''') 2D のアスキー地図（オリジナルモード。§6） */
    {
        /*
         * 2026-08-19 に決めた（・改訂）「あくまでもメインパネルに 2D でアスキー表示するのみで」。
         *
         * いちばん見たいのは **`cam` を窓の中心として扱えているか**である。
         * プロトコルの可視窓は注視点を中心にした対称な矩形で、`cam` は左上ではない
         * （`presentation_bridge.cpp` の `ox = px - view_w/2`）。ここを取り違えると
         * 自機が隅に出て地図の半分が枠の外へ行く——`draw_frame_as_text` が P0 で実際に踏んだ穴で、
         * **絵は出ているので「動いている」ように見えてしまう**種類の間違いである。
         *
         * そこで**マス 1 つだけ**を既知にして描き、塗られた画素の外接矩形の中心が
         * 枠の中心に来ることを見る。1 マスなら位置がそのまま読める。
         */
        const UiLayout ascii_layout = UiLayout::compute(LayoutMode::Hybrid, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false);
        GameFrame one = make_synthetic_frame(23, 19, 100, 44);
        for (auto &cell : one.cells) {
            //! **全部未踏破にする**（`make_synthetic_frame` は地形を既知で作る）。
            cell.feature_flags &= static_cast<std::uint16_t>(~CELL_FEAT_KNOWN);
            cell.ascii_fallback = '\0';
            cell.bg_color = 0;
        }
        for (auto &cell : one.cells) {
            if ((cell.gx != one.cam_x) || (cell.gy != one.cam_y)) {
                continue;
            }
            cell.feature_flags |= CELL_FEAT_KNOWN;
            cell.ascii_fallback = '@';
            cell.fg_color = 1; //!< 白
            break;
        }

        const auto shoot_ascii = [&](const GameFrame &f, bool draw_map) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, screen_w, screen_h);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            paint.begin(screen_w, screen_h);
            text.begin(screen_w, screen_h);
            if (draw_map) {
                (void)draw_ascii_map_panel(paint, text, ascii_layout.scene, f);
            }
            paint.flush();
            text.flush();
            return read_framebuffer(screen_w, screen_h);
        };

        /*
         * 下敷き（黒）だけの絵を基準にする。**下敷きを描いた絵と比べる**ので、
         * 差はそのまま「字と背景色の画素」になる。
         */
        GameFrame blank = one;
        for (auto &cell : blank.cells) {
            cell.feature_flags &= static_cast<std::uint16_t>(~CELL_FEAT_KNOWN);
        }
        const std::vector<unsigned char> base = shoot_ascii(blank, true);
        const std::vector<unsigned char> one_cell = shoot_ascii(one, true);

        //! 塗られた画素の外接矩形（`count_changed_pixels` は数だけなので、ここで自分で採る）。
        int min_x = screen_w;
        int min_y = screen_h;
        int max_x = -1;
        int max_y = -1;
        std::size_t painted = 0;
        if (base.size() == one_cell.size()) {
            for (int y = 0; y < screen_h; ++y) {
                for (int x = 0; x < screen_w; ++x) {
                    const std::size_t at = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(screen_w))
                                               + static_cast<std::size_t>(x))
                        * 4u;
                    if ((base[at] == one_cell[at]) && (base[at + 1u] == one_cell[at + 1u])
                        && (base[at + 2u] == one_cell[at + 2u])) {
                        continue;
                    }
                    ++painted;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                }
            }
        }
        if (painted == 0) {
            fail("(2''') 2D アスキー: 既知のマスが 1 つあるのに 1 画素も描かれていません");
        } else {
            /*
             * `read_framebuffer` は **GL の並び（下が 0）**で返る。枠の座標は左上原点なので、
             * 比べる前に上下を返す。ここを忘れると「上下だけずれている」検査になる。
             */
            const int mid_x = (min_x + max_x) / 2;
            const int mid_y_gl = (min_y + max_y) / 2;
            const int mid_y = screen_h - 1 - mid_y_gl;
            const int want_x = ascii_layout.scene.x + (ascii_layout.scene.w / 2);
            const int want_y = ascii_layout.scene.y + (ascii_layout.scene.h / 2);
            //! マス 1 つぶんまで許す（枠の端数を両側へ等分しているので半マスずれることがある）。
            const int slack_x = std::max(2, text.cell_w() * 2);
            const int slack_y = std::max(2, text.cell_h() * 2);
            std::fprintf(stderr, "  (2''') 2D アスキー: %zu 画素 / 字の中心 %d,%d（枠の中心 %d,%d）\n",
                painted, mid_x, mid_y, want_x, want_y);
            if ((std::abs(mid_x - want_x) > slack_x) || (std::abs(mid_y - want_y) > slack_y)) {
                fail("(2''') 2D アスキー: cam のマスが枠の中心に来ていません"
                     "（`cam` を窓の左上と取り違えていませんか）");
            }
        }

        /*
         * **未踏破は描かない。**`blank`（全部未踏破）で字が出ていたら、
         * 「見ていない所まで見えている」ことになる。
         */
        const std::vector<unsigned char> nothing = shoot_ascii(blank, false);
        if (count_changed_pixels(base, nothing, screen_w, screen_h, ascii_layout.scene) != 0) {
            fail("(2''') 2D アスキー: 未踏破だけの地図で何かが描かれています");
        }

        /*
         * **(2'''-a) 暗い床が黒に潰れないこと**（SH-29。W4）。
         *
         * 枠の下敷きは黒なので、色番号 0（`TERM_DARK`）の字は**黒地に黒**になり消える。
         * Sil-Q は暗い床・盲目のマスをまさにその色で送ってくる（`TERM_DARK + TERM_SHADE`
         * の上位ビットがアダプタの 16 色畳みで落ちる）ので、**記憶している廊下が
         * 地図から消えていた**。
         *
         * **同じ関数を色だけ変えて 2 回呼ぶ**（描き方を写さない）。色 0 の 1 マスが
         * 色 9 の 1 マスと同じくらい塗られていれば直っている。
         */
        {
            GameFrame dark = one;
            for (auto &cell : dark.cells) {
                if ((cell.feature_flags & CELL_FEAT_KNOWN) == 0u) {
                    continue;
                }
                cell.ascii_fallback = '.';
                cell.fg_color = 0; //!< TERM_DARK。**ここが黒に潰れていた**
            }
            GameFrame lit = dark;
            for (auto &cell : lit.cells) {
                if ((cell.feature_flags & CELL_FEAT_KNOWN) != 0u) {
                    cell.fg_color = 9; //!< 比べる相手（ふつうの灰）
                }
            }
            const std::size_t dark_px = count_changed_pixels(base, shoot_ascii(dark, true),
                screen_w, screen_h, ascii_layout.scene);
            const std::size_t lit_px = count_changed_pixels(base, shoot_ascii(lit, true),
                screen_w, screen_h, ascii_layout.scene);
            std::fprintf(stderr, "  (2'''-a) 暗い床: 色 0 で %zu 画素 / 色 9 で %zu 画素\n",
                dark_px, lit_px);
            if (dark_px == 0) {
                fail("(2'''-a) 2D アスキー: TERM_DARK の床が 1 画素も出ていません"
                     "（黒地に黒。SH-29）");
            } else if ((lit_px > 0) && ((dark_px * 2u) < lit_px)) {
                //! 同じ字なので画素数はほぼ同じはず。半分を切るなら塗り残している。
                fail("(2'''-a) 2D アスキー: TERM_DARK の床が薄すぎます（読めません）");
            }
        }

        /*
         * **(2'''-b) 照準のマスが出ること**（SH-30。W4）。
         *
         * 照準は重ね書きではなく `target_gx/gy` で直に届く。
         * **旗で呼び分ける**——同じフレームの照準を外した版と比べ、枠のぶんだけ
         * 画素が増えることを見る。増えなければ受け皿が無い。
         */
        {
            GameFrame aimed = one;
            aimed.target_gx = static_cast<std::int16_t>(one.cam_x);
            aimed.target_gy = static_cast<std::int16_t>(one.cam_y);
            GameFrame idle = one;
            idle.target_gx = -1;
            idle.target_gy = -1;
            const std::size_t idle_px = count_changed_pixels(base, shoot_ascii(idle, true),
                screen_w, screen_h, ascii_layout.scene);
            const std::size_t aim_px = count_changed_pixels(base, shoot_ascii(aimed, true),
                screen_w, screen_h, ascii_layout.scene);
            std::fprintf(stderr, "  (2'''-b) 照準: 無しで %zu 画素 / 有りで %zu 画素\n",
                idle_px, aim_px);
            if (aim_px <= idle_px) {
                fail("(2'''-b) 2D アスキー: 照準を立てても画が変わりません（SH-30）");
            }
            //! **枠の外のマスでは出ないこと**（画面いっぱいに塗っていないことの裏取り）。
            GameFrame far_away = one;
            far_away.target_gx = static_cast<std::int16_t>(one.cam_x + 9000);
            far_away.target_gy = static_cast<std::int16_t>(one.cam_y + 9000);
            const std::size_t far_px = count_changed_pixels(base, shoot_ascii(far_away, true),
                screen_w, screen_h, ascii_layout.scene);
            if (far_px != idle_px) {
                fail("(2'''-b) 2D アスキー: 枠の外の照準まで描いています");
            }
        }

        if (!options.shot_path.empty()) {
            //! 人に見せる絵は**実データらしい形**で（合成フレームの地形を字にして全部既知にする）。
            GameFrame shown = make_synthetic_frame(23, 19, 100, 44);
            for (auto &cell : shown.cells) {
                cell.feature_flags |= CELL_FEAT_KNOWN;
                if (cell.ascii_fallback != '\0') {
                    continue;
                }
                //! 検査の絵のための割り当て（コアの記号ではない。実データでは Bridge が入れる）。
                const bool wall = (cell.feature_flags & CELL_FEAT_WALL) != 0u;
                cell.ascii_fallback = wall ? '#' : '.';
                cell.fg_color = wall ? 2 : 9;
            }
            for (auto &cell : shown.cells) {
                if ((cell.gx != shown.player_gx) || (cell.gy != shown.player_gy)) {
                    continue;
                }
                cell.ascii_fallback = '@';
                cell.fg_color = 1;
                break;
            }
            /*
             * W4 の 2 つを**絵でも見せる**。左半分の床を `TERM_DARK` にして
             * 「消えていないこと」を、@ の 3 マス右へ照準を置いて「枠が出ること」を出す。
             */
            for (auto &cell : shown.cells) {
                const bool wall = (cell.feature_flags & CELL_FEAT_WALL) != 0u;
                if (!wall && (cell.gx < shown.cam_x)) {
                    cell.fg_color = 0; //!< TERM_DARK（SH-29）
                }
            }
            shown.target_gx = static_cast<std::int16_t>(shown.player_gx + 3);
            shown.target_gy = static_cast<std::int16_t>(shown.player_gy);
            HudState st;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, screen_w, screen_h);
            glClearColor(0.02f, 0.02f, 0.03f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            paint.begin(screen_w, screen_h);
            text.begin(screen_w, screen_h);
            (void)draw_ascii_map_panel(paint, text, ascii_layout.scene, shown);
            draw_game_ui(paint, text, ascii_layout, shown, st);
            paint.flush();
            text.flush();
            std::string path = options.shot_path;
            const std::size_t dot_at = path.find_last_of('.');
            path = (dot_at == std::string::npos) ? (path + "_ascii2d")
                                                 : (path.substr(0, dot_at) + "_ascii2d" + path.substr(dot_at));
            (void)save_framebuffer_bmp(path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);
    }

    /* ---------------------------------- (2') Term の写しと選択の枠 */
    {
        /*
         * 店の画面（コアの Term をそのまま写す画面）。**カーソルの枠が画素になること**を見る。
         * ここを見ないと、枠の位置をバイトではなく Term の列で計算していても気づけない
         * （全角があると必ずずれる）。
         */
        GameFrame shop = make_synthetic_frame(23, 19, 100, 44);
        shop.menu_term_lines.push_back(TermMirrorLine{ "  よろずや", 11, 0, 0, 0 });
        shop.menu_term_lines.push_back(TermMirrorLine{ "", 1, 1, 0, 0 });
        shop.menu_term_lines.push_back(TermMirrorLine{ "  a) 商品を見る      b) 買う", 1, 2, 0, 0 });
        shop.menu_term_lines.push_back(TermMirrorLine{ "  c) 売る            d) 店を出る", 1, 3, 0, 0 });
        shop.menu_choices.push_back(MenuChoice{ 2, 2, 20, 2, 1, 'a' });
        shop.menu_choices.push_back(MenuChoice{ 2, 23, 8, 23, 1, 'b' });
        shop.menu_choices.push_back(MenuChoice{ 3, 2, 12, 2, 1, 'c' });
        shop.menu_choices.push_back(MenuChoice{ 3, 23, 14, 23, 1, 'd' });

        HudState state;
        presentation::InputEventsMessage sink;
        state.cursors.sync(shop);
        (void)state.cursors.handle(CursorNav::Right, sink); //!< 2 番目を選んだ状態にする
        const UiLayout layout = UiLayout::compute(LayoutMode::Hybrid, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screen_w, screen_h);
        glClearColor(static_cast<float>(clear[0]) / 255.f, static_cast<float>(clear[1]) / 255.f,
            static_cast<float>(clear[2]) / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        paint.begin(screen_w, screen_h);
        text.begin(screen_w, screen_h);
        draw_game_ui(paint, text, layout, shop, state);
        paint.flush();
        text.flush();
        const std::vector<unsigned char> image = read_framebuffer(screen_w, screen_h);

        //! 選んでいるのは 2 番目（`b) 買う`）。**その行の中だけ**を数える。
        const RectPx body = layout.term_overlay.inset(std::max(2, text.cell_w() / 2));
        const RectPx row{ body.x, body.y + (2 * text.cell_h()), body.w, text.cell_h() };
        const std::size_t painted = count_painted_pixels(image, screen_w, screen_h, row, clear);
        if (painted == 0) {
            fail("Term の写し: 選択肢の行に何も描かれていません");
        }
        /*
         * **小窓の大きさ**（2026-08-11 に決めた「通常メニューも枠が全画面近くまで
         * 担っていた。建物メニューも」）。確かめるのは 2 つで、**どちらも外すと使えなくなる**:
         *   1. コアの Term（80×24 固定）が**1 桁も切れない**こと。切れると店の品書きが読めない
         *   2. 地図の枠が足りている作り（`Split`）では、その**枠の中に収まる**こと
         * 窓が小さくて 1 が満たせないときは窓基準へ戻すので、そのときは 2 を求めない。
         */
        for (const auto &size : { std::pair<int, int>{ screen_w, screen_h },
                 std::pair<int, int>{ 1280, 720 }, std::pair<int, int>{ 800, 600 } }) {
            for (const LayoutMode probe_mode : { LayoutMode::Split, LayoutMode::Hybrid, LayoutMode::Full }) {
                const UiLayout probe = UiLayout::compute(probe_mode, size.first, size.second,
                    text.cell_w(), text.cell_h(), false);
                const RectPx &over = probe.term_overlay;
                const RectPx inner = panel_body(over, probe.cell_w);
                const int cols = inner.w / probe.cell_w;
                const int rows = inner.h / probe.cell_h;
                char buf[192]{};
                if ((cols < 80) || (rows < 24)) {
                    std::snprintf(buf, sizeof(buf),
                        "Term の写し: %dx%d %s でメニューの小窓が %dx%d 桁しかありません（80x24 が要る）",
                        size.first, size.second, layout_mode_name(probe_mode), cols, rows);
                    fail(buf);
                }
                const RectPx &map = probe.scene;
                //! 枠の中へ収めた枝を通ったときだけ、はみ出していないことを求める。
                const bool fitted = !map.empty() && (over.x >= map.x) && (over.y >= map.y)
                    && ((over.x + over.w) <= (map.x + map.w)) && ((over.y + over.h) <= (map.y + map.h));
                const bool room = !map.empty() && (map.w >= over.w) && (map.h >= over.h);
                if (room && !fitted) {
                    std::snprintf(buf, sizeof(buf),
                        "Term の写し: %dx%d %s で小窓 (%d,%d %dx%d) が地図の枠 (%d,%d %dx%d) からはみ出しています",
                        size.first, size.second, layout_mode_name(probe_mode), over.x, over.y, over.w, over.h,
                        map.x, map.y, map.w, map.h);
                    fail(buf);
                }
            }
        }
        std::fprintf(stderr, "  (2') Term の写し（店）+ 選択の枠: 行の画素 %zu / 小窓 %dx%d（80x24 が入る）\n",
            painted, layout.term_overlay.w, layout.term_overlay.h);
        if (!options.shot_path.empty()) {
            std::string path = options.shot_path;
            const std::size_t dot = path.find_last_of('.');
            path = (dot == std::string::npos) ? (path + "_term") : (path.substr(0, dot) + "_term" + path.substr(dot));
            (void)save_framebuffer_bmp(path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);
    }

    /* ------------------------------------------------ (3) カーソル操作 */
    {
        /*
         * 窓も GL も要らない部分。**期待する値を書き切る**（「動いた」ではなく「どこへ動いた」）。
         * 動いたかどうかだけを見る検査は、逆向きに動いても通る。
         */
        GameFrame shop = make_synthetic_frame(23, 19, 100, 44);
        shop.menu_term_lines.push_back(TermMirrorLine{ "  a) 武器を買う      b) 防具を買う", 1, 0, 0, 0 });
        shop.menu_term_lines.push_back(TermMirrorLine{ "  c) 道具を買う      d) 出る", 1, 1, 0, 0 });
        shop.menu_choices.push_back(MenuChoice{ 0, 2, 12, 2, 1, 'a' });
        shop.menu_choices.push_back(MenuChoice{ 0, 21, 12, 21, 1, 'b' });
        shop.menu_choices.push_back(MenuChoice{ 1, 2, 12, 2, 1, 'c' });
        shop.menu_choices.push_back(MenuChoice{ 1, 21, 6, 21, 1, 'd' });

        UiCursors cursors;
        cursors.sync(shop);
        if (cursors.choice_index() != 0) {
            fail("選択肢: 取り込んだ直後が先頭になっていません");
        }
        presentation::InputEventsMessage sink;
        (void)cursors.handle(CursorNav::Right, sink);
        if (cursors.choice_index() != 1) {
            fail("選択肢: → で隣へ動きません");
        }
        //! 上下は「隣の**行**の、いちばん桁が近い選択肢」へ（2 列・3 列に並ぶため）。
        (void)cursors.handle(CursorNav::Down, sink);
        if (cursors.choice_index() != 3) {
            fail("選択肢: ↓ で下の行の同じ桁へ行きません（添字 ±1 になっていませんか）");
        }
        (void)cursors.handle(CursorNav::Right, sink);
        if (cursors.choice_index() != 0) {
            fail("選択肢: 端で先頭へ回りません");
        }
        if (!sink.events.empty()) {
            fail("選択肢: 動かしただけでコアへ送っています");
        }
        if (!cursors.handle(CursorNav::Confirm, sink)) {
            fail("選択肢: 決定を食っていません");
        }
        if ((sink.events.size() != 1) || (sink.events[0].e != "key") || (sink.events[0].chr != "a")) {
            fail("選択肢: 決定で送るのは選んだ 1 キーだけであるべきです");
        }

        //! **画面が変わったら頭へ戻る。**戻らないと別の一覧で前の位置が選ばれている。
        (void)cursors.handle(CursorNav::Right, sink);
        GameFrame other = shop;
        other.menu_choices.clear();
        other.menu_choices.push_back(MenuChoice{ 0, 2, 8, 2, 1, 'x' });
        other.menu_choices.push_back(MenuChoice{ 0, 12, 8, 12, 1, 'y' });
        cursors.sync(other);
        if (cursors.choice_index() != 0) {
            fail("選択肢: 別の画面になってもカーソルが残っています");
        }

        /*
         * --- 能力値のロール確認（K-24 の 3 択。2026-08-11 の直し）---
         *
         * `['r' 次の数値, 'h' 生い立ちを表示, Enter この数値に決定]` は **Enter も選択肢**
         * として拾われる（拾わないとパッドから決定できない）。ここで初期カーソルが
         * 先頭の `'r'` だと、画面が「Enter この数値に決定」と言っているのに
         * **Enter がカーソル層で `'r'`（振り直し）へ翻訳され、何度押しても決定できない**
         * （2026-08-11 に気づいた「新規で開始する際、名前選択の手前で決定ができない」）。
         *
         * **Enter の選択肢はその画面の既定の動作**なので、初期カーソルはそこへ置く。
         */
        GameFrame roll = make_synthetic_frame(23, 19, 100, 44);
        roll.menu_term_lines.push_back(
            TermMirrorLine{ "['r' 次の数値, 'h' 生い立ちを表示, Enter この数値に決定]", 1, 0, 0, 0 });
        roll.menu_choices.push_back(MenuChoice{ 0, 1, 10, 2, 1, 'r' });
        roll.menu_choices.push_back(MenuChoice{ 0, 15, 14, 16, 1, 'h' });
        roll.menu_choices.push_back(MenuChoice{ 0, 33, 20, 33, 5, '\r' });
        UiCursors roller;
        roller.sync(roll);
        if (roller.choice_index() != 2) {
            fail("ロール確認: 初期カーソルが Enter（この数値に決定）に居ません"
                 "（素の Enter が振り直しに化けます）");
        }
        /*
         * 決定は **`confirm` イベント**であること。`key` の `chr='\r'` で送ると、
         * 復号側の検証（印字文字 0x20〜0x7E のみ）が**黙って捨てて何も起きない**
         * （2026-08-11 に気づいた「Enter を押しても動かない。ESC で次に遷移する」）。
         * 念のため**符号化 → 復号の往復**まで通して「捨てられないこと」も見る。
         */
        sink.events.clear();
        if (!roller.handle(CursorNav::Confirm, sink) || (sink.events.size() != 1)
            || (sink.events[0].e != "confirm")) {
            fail("ロール確認: 決定が confirm になっていません（chr='\\r' は復号で捨てられます）");
        }
        {
            const std::string wire_text = presentation::encode_input_events(sink);
            presentation::InputEventsMessage decoded;
            std::string codec_err;
            if (!presentation::decode_input_events(wire_text, decoded, codec_err)
                || (decoded.events.size() != 1) || (decoded.events[0].e != "confirm")) {
                fail("ロール確認: 決定が復号を通りません（コアに届かない決定は決定ではない）");
            }
        }
        //! 振り直したい人は矢印で `'r'` へ動かしてから決定する（パッドも同じ道）。
        sink.events.clear();
        (void)roller.handle(CursorNav::Left, sink);
        (void)roller.handle(CursorNav::Left, sink);
        if (roller.choice_index() != 0) {
            fail("ロール確認: ← で 'r' へ動きません");
        }
        sink.events.clear();
        if (!roller.handle(CursorNav::Confirm, sink) || (sink.events.size() != 1)
            || (sink.events[0].chr != "r")) {
            fail("ロール確認: 'r' を選んで決定しても振り直しになりません");
        }

        // --- はい／いいえ ---
        GameFrame ask = make_synthetic_frame(23, 19, 100, 44);
        ask.prompt.text_utf8 = "本当に飲みますか? [y/n]";
        ask.prompt.line_index = -1;
        ask.prompt.choices.push_back(PromptChoice{ "はい", 'y', 12, 1 });
        ask.prompt.choices.push_back(PromptChoice{ "いいえ", 'n', 14, 1 });
        UiCursors answer;
        answer.sync(ask);
        sink.events.clear();
        (void)answer.handle(CursorNav::Right, sink);
        if (answer.prompt_index() != 1) {
            fail("はい／いいえ: → で「いいえ」へ動きません");
        }
        if (!answer.handle(CursorNav::Confirm, sink)) {
            fail("はい／いいえ: 決定を食っていません");
        }
        /*
         * コアの `input_check_strict()` は `'\r'` を撥ねる。**答えの 1 文字へ翻訳**
         * されていなければ、決定で先へ進めない画面ができる。
         */
        if ((sink.events.size() != 1) || (sink.events[0].e != "answer") || (sink.events[0].value != "no")) {
            fail("はい／いいえ: 決定が answer=no になっていません");
        }

        // --- 数値入力 ---
        GameFrame count = make_synthetic_frame(23, 19, 100, 44);
        count.numeric.active = true;
        count.numeric.prompt_utf8 = "いくつですか (1-99): ";
        count.numeric.min = 1;
        count.numeric.max = 99;
        count.numeric.value = 5;
        count.numeric.digits = 2;
        count.numeric.text_len = 1;
        count.numeric.line_index = -1;
        UiCursors number;
        number.sync(count);
        sink.events.clear();
        (void)number.handle(CursorNav::Up, sink);
        if ((sink.events.size() != 1) || (sink.events[0].e != "set_number") || (sink.events[0].number != 6)
            || (sink.events[0].digits != 2)) {
            fail("数値入力: ↑ が 1 の位を +1 して 0 詰め 2 桁で送っていません");
        }
        (void)number.handle(CursorNav::Left, sink);
        if (number.numeric_place() != 1) {
            fail("数値入力: ← で 10 の位へ行きません");
        }
        sink.events.clear();
        (void)number.handle(CursorNav::Up, sink);
        if ((sink.events.size() != 1) || (sink.events[0].number != 16)) {
            fail("数値入力: 10 の位の ↑ が +10 になっていません");
        }
        sink.events.clear();
        for (int i = 0; i < 20; ++i) {
            (void)number.handle(CursorNav::Up, sink);
        }
        if (number.numeric().value > count.numeric.max) {
            fail("数値入力: 上限を超えました");
        }

        /*
         * **決定で確定できること**（P10 レビュー 12。気づいたこと
         * 「店の数量指定で数量の入力と確定が出来ず先に進まない」）。
         * コアの `askfor` は Enter で確定するので、そのまま `confirm` を送る。
         */
        sink.events.clear();
        if (!number.handle(CursorNav::Confirm, sink)) {
            fail("数値入力: 決定を食っていません（背後の店の選択肢に化けます）");
        }
        if ((sink.events.size() != 1) || (sink.events[0].e != "confirm")) {
            fail("数値入力: 決定が confirm になっていません（個数が確定しません）");
        }

        /*
         * **数値入力は選択肢より手前。**店で個数を聞かれている間も背後の店画面は
         * ミラーに残るので、選択肢も「出ている」。ここで先に食わないと、
         * 個数の決定が「選んでいる店コマンドの 1 キー」に化ける。
         *
         * **上下だけでなく決定も試すこと。**レビュー 12 まで上下しか通しておらず、
         * 「決定だけが素通りして店コマンドに化ける」という肝心の壊れ方を
         * この検査は 1 度も見ていなかった（罠 68）。
         */
        GameFrame both = shop;
        both.numeric = count.numeric;
        UiCursors mixed;
        mixed.sync(both);
        sink.events.clear();
        (void)mixed.handle(CursorNav::Up, sink);
        if ((sink.events.size() != 1) || (sink.events[0].e != "set_number")) {
            fail("数値入力: 選択肢より先に食っていません（店の個数入力が壊れます）");
        }
        sink.events.clear();
        (void)mixed.handle(CursorNav::Confirm, sink);
        if ((sink.events.size() != 1) || (sink.events[0].e != "confirm")) {
            fail("数値入力: 店の選択肢が出ていると決定が店コマンドに化けます（先に進めません）");
        }

        /*
         * **数字キーと ESC は食わない**（コアの `askfor` がそのまま受け取る）。
         * ここを食うと個数を打ち込めなくなる。`CursorNav::None` で来るので、
         * 何も積まずに false が返るのが正しい。
         */
        sink.events.clear();
        if (mixed.handle(CursorNav::None, sink) || !sink.events.empty()) {
            fail("数値入力: 文字キー・ESC まで食っています（個数を打ち込めません）");
        }

        /*
         * **品物の選択（`(商品:a-l, ESCで中断) どの品物が欲しいんだい?`）は数値ではない。**
         * ここで数値入力と誤判定すると、a〜l のキーが数値の桁移動に化ける。
         * 判定はコア側（`parse_numeric_input_line`）の仕事だが、
         * **数値でないフレームでは決定が選択肢へ行く**ことをこちらでも押さえておく。
         */
        GameFrame picking = shop;
        picking.prompt.text_utf8 = "(商品:a-l, ESCで中断) どの品物が欲しいんだい? ";
        picking.prompt.line_index = -1;
        UiCursors pick;
        pick.sync(picking);
        sink.events.clear();
        (void)pick.handle(CursorNav::Confirm, sink);
        if ((sink.events.size() != 1) || (sink.events[0].e != "key") || (sink.events[0].chr != "a")) {
            fail("品物の選択: 決定で選んだ 1 キーが送られていません");
        }

        /*
         * --- 文字を編集している画面の矢印（`handle_text_edit_arrows`）---
         *
         * コアのエディタ（生い立ち・自動拾い）は矢印を `SKEY_*` としてしか受け取れず、
         * この線に SKEY は無い。しかも `move` はアダプタで `'4'` `'8'` になるので、
         * **矢印を押すたびに本文へ数字が入っていた**（気づいたこと）。
         * 両エディタが元から持っている Ctrl-B/N/P/F へ翻訳して送る。
         */
        {
            GameFrame editing = make_synthetic_frame(23, 19, 100, 44);
            editing.text_input_active = true; //!< コアの `TextInputScope`（askfor と 2 つのエディタ）
            UiCursors editor;
            editor.sync(editing);
            struct TextEditMove {
                CursorNav nav;
                const char *chr;
                const char *name;
            };
            static const TextEditMove kTextEditMoves[] = {
                { CursorNav::Up, "p", "↑" },
                { CursorNav::Down, "n", "↓" },
                { CursorNav::Left, "b", "←" },
                { CursorNav::Right, "f", "→" },
            };
            for (const auto &move : kTextEditMoves) {
                sink.events.clear();
                //! 編集画面には選択肢が無いので、カーソル層は食い残す（食ったらこの層に届かない）。
                if (editor.handle(move.nav, sink) || !sink.events.empty()) {
                    fail(std::string("文字の編集: ") + move.name + " をカーソル層が食っています");
                }
                sink.events.clear();
                if (!handle_text_edit_arrows(editing, move.nav, sink) || (sink.events.size() != 1)) {
                    fail(std::string("文字の編集: ") + move.name + " を食っていません（本文に数字が入ります）");
                }
                /*
                 * **符号化 → 復号まで通す。**`chr` は印字文字しか通らないので、
                 * 制御キーを送るには「印字文字 ＋ `ctrl`」でなければならない
                 * （コア側のアダプタが `c & 0x1F` で KTRL にする）。ロール確認の Enter が
                 * 黙って捨てられていたのと同じ穴なので、往復で押さえる。
                 */
                const std::string wire_text = presentation::encode_input_events(sink);
                presentation::InputEventsMessage decoded;
                std::string codec_err;
                if (!presentation::decode_input_events(wire_text, decoded, codec_err)
                    || (decoded.events.size() != 1) || (decoded.events[0].e != "key")
                    || (decoded.events[0].chr != move.chr) || !decoded.events[0].ctrl) {
                    fail(std::string("文字の編集: ") + move.name + " が Ctrl-" + move.chr
                        + " として復号を通りません（コアのエディタは動きません）");
                }
            }
            //! 決定と文字キーは翻訳しない（Enter で確定・字はそのまま本文へ）。
            sink.events.clear();
            if (handle_text_edit_arrows(editing, CursorNav::Confirm, sink)
                || handle_text_edit_arrows(editing, CursorNav::None, sink) || !sink.events.empty()) {
                fail("文字の編集: 決定や文字キーまで食っています（Enter で終われません）");
            }
            //! 札が立っていない画面（ふつうに歩いている最中）では 1 件も食わない。
            GameFrame walking = make_synthetic_frame(23, 19, 100, 44);
            sink.events.clear();
            if (handle_text_edit_arrows(walking, CursorNav::Up, sink) || !sink.events.empty()) {
                fail("文字の編集: 文字入力でない画面の矢印まで食っています（歩けません）");
            }

            /*
             * **打っている行が帯に出ること**（2026-08-15 に決めた「入力中の表示も欲しい」）。
             * 銘の刻印は地図の上の `askfor` なので Term の写しが 1 行も開かず、
             * bridge が行 0 を `prompt.text_utf8` へ載せる（`fill_row0_prompts`）。
             * **選択肢は 1 つも無い**——ここを条件にすると、打っている行が出なくなって
             * 「打っても画面が何も変わらない」に戻る。
             */
            GameFrame typing = make_synthetic_frame(23, 19, 100, 44);
            typing.text_input_active = true;
            typing.prompt.text_utf8 = "銘: !k";
            typing.prompt.line_index = -1;
            if (!prompt_bar_shows_text(typing)) {
                fail("文字の編集: 打っている行が帯に出ません（打っても画面が変わりません）");
            }
            //! Term の写しに出ている行（名前入力・エディタ）は帯に出さない（二重になる）。
            GameFrame mirrored = typing;
            mirrored.prompt.line_index = 3;
            if (prompt_bar_shows_text(mirrored)) {
                fail("文字の編集: 写しに出ている行まで帯に出しています（同じ行が 2 か所に出ます）");
            }
            if (prompt_bar_shows_text(walking)) {
                fail("文字の編集: 何も聞かれていないのに帯が出ています");
            }
        }
        /*
         * **メニューが出ている間は「向いている方角へ」を効かせない**（2026-08-11 に気づいた
         * 「FPS モードでメニューに入った際、カーソルの移動が FPS で向いている方向に
         * あわせて上下左右がいれかわってしまう」）。
         *
         * ここで見るのは `frame_shows_map()` である。**コアの Term の写しが出ている画面は
         * `UiCursors` を埋めないことがある**ので、カーソルの有無だけを見ていると素通りする
         * （実際に素通りしていた）。判定そのものは `hd2d_app.cpp` の
         * `fps_drives_movement()` にあるが、その材料がここで壊れていないことを押さえる。
         */
        {
            GameFrame walking = make_synthetic_frame(23, 19, 100, 44);
            if (!frame_shows_map(walking)) {
                fail("一人称: 地図の画面が「メニューが出ている」と判定されました");
            }
            GameFrame in_menu = walking;
            in_menu.menu_term_lines.push_back(TermMirrorLine{ "  a) 商品を見る", 1, 0, 0, 0 });
            if (frame_shows_map(in_menu)) {
                fail("一人称: Term の写し（店・建物・通常メニュー）が出ているのに地図の画面と判定されました");
            }
            /*
             * 向きの翻訳そのものが「効けば入れ替わる」ことも押さえておく。
             * 南（180°）を向いていれば、前（画面の上）は**南**＝ `dy = +1` になる。
             * この翻訳がメニューへ漏れると ↑ が「下の行」になる。
             */
            FpsMode facing_south;
            facing_south.set_active(true);
            facing_south.facing = 3.14159265f;
            int dx = 0;
            int dy = 0;
            if (!facing_south.direction_delta(1, 0, dx, dy) || (dx != 0) || (dy != 1)) {
                fail("一人称: 南を向いて前へ進む向きが南になりません");
            }
        }
        std::fprintf(stderr, "  (3) カーソル: 選択肢・はい／いいえ・数値入力（決定つき）"
                             " / メニュー中は向きを翻訳しない"
                             " / 文字の編集は矢印を Ctrl-B/N/P/F へ・打っている行を帯に出す\n");
    }

    /* ---------------------------------------- (4) 機能メニューと設定の保存 */
    {
        Hd2dSettings settings;
        FeatureMenu menu;
        if (menu.is_open()) {
            fail("機能メニュー: 開く前から開いています");
        }
        menu.open(settings);
        if (!menu.is_open()) {
            fail("機能メニュー: 開きません");
        }
        /*
         * **分類の組み立てそのものを先に突く**（2026-08-19）。項目を `page_of()` へ
         * 書き忘れると、その項目はどこにも並ばず元の節が空になる——画面は普通に描け、
         * はみ出しも画素も通るので、**人が全部の節を開くまで誰も気づけない**
         * （組み替えのときカメラの 5 つを落とし、この検査が無かったので通った）。
         */
        if (const auto note = FeatureMenu::structure_report(); !note.empty()) {
            char buf[256]{};
            std::snprintf(buf, sizeof(buf), "機能メニュー: %s", note.c_str());
            fail(buf);
        }
        /*
         * **カーソルは見出しで合わせる**（番号で数えない）。番号だと、項目を 1 つ足しただけで
         * 検査が別の項目を触り、直したはずのものが落ちる（実際に落ちた）。
         */
        if (!menu.focus_item(i18n::tr("hd2d.app.hd2d-app.camera-pitch"))) {
            fail("機能メニュー: 「見下ろし角」が見つかりません");
        }
        const float pitch_before = settings.camera_pitch_deg;
        menu.handle(MenuNav::Right, settings);
        if (settings.camera_pitch_deg <= pitch_before) {
            fail("機能メニュー: → でカメラの見下ろし角が増えません");
        }
        /*
         * **4 つを順に回る**（どれかを既定にして他を捨てない、が決めたこと）ので、
         * → を 4 回で元へ戻るはずである（`Tall` を足したので 3 から 4 になった。
         * 2026-08-12 に決めた）。
         */
        if (!menu.focus_item(i18n::tr("hd2d.app.hd2d-app.layout"))) {
            fail("機能メニュー: 「画面の作り」が見つかりません");
        }
        //! 既定では横向き扱い（`set_orientation` を呼んでいない）なので `layout` が動く。
        const LayoutMode layout_before = settings.layout;
        menu.handle(MenuNav::Right, settings);
        if (settings.layout == layout_before) {
            fail("機能メニュー: → で画面の作りが変わりません");
        }
        for (int i = 0; i < 3; ++i) {
            menu.handle(MenuNav::Right, settings);
        }
        if (settings.layout != layout_before) {
            fail("機能メニュー: 画面の作りが 4 つで一巡していません");
        }
        /*
         * **縦向きの側は別に持っている**（同 2026-08-12）。縦へ切り替えてから回すと
         * 縦だけが動き、横は動かないはず。ここが落ちると「回したら横の設定まで変わる」
         * ——利用者から見れば「片方を直すともう片方が壊れる」になる。
         */
        {
            const LayoutMode land_before = settings.layout;
            const LayoutMode port_before = settings.layout_portrait;
            menu.set_orientation(true);
            menu.handle(MenuNav::Right, settings);
            if (settings.layout_portrait == port_before) {
                fail("機能メニュー: 縦向きで → を押しても縦の画面の作りが変わりません");
            }
            if (settings.layout != land_before) {
                fail("機能メニュー: 縦向きの操作で横の画面の作りまで変わりました");
            }
            settings.layout_portrait = port_before;
            menu.set_orientation(false);
        }

        /*
         * **字がパネルからはみ出していないか**を、読める言語すべてで確かめる
         * （実機で見た様子 2026-08-15。英語にしたら値が `Full-screen 3D (Landsca` で切れた）。
         * `text.draw()` は入り切らない字を黙って積まないので、溢れても落ちない——
         * **人が画面を見るまで気づけない**。だから機械で突く。
         * 言語を増やしたら、この検査が勝手にその言語も見る。
         */
        {
            const std::string lang_before(i18n::current());
            /*
             * 窓は**いちばん狭くなりうる形**で見る。広い窓で収まっても、
             * 縮めた途端に切れるのでは直したことにならない。
             */
            const UiLayout probe_layout = UiLayout::compute(
                LayoutMode::Split, 1024, 600, text.cell_w(), text.cell_h(), false);
            for (const auto &info : i18n::available()) {
                i18n::set_language(info.code);
                menu.close();
                menu.open(settings);
                /*
                 * **入口・分類・小節を全部見る。**前は入口から入れる分類だけを、それも
                 * 「↓ を n 回」で数えて回っていた——小節（効果・ミニマップ・出すパネル・
                 * ボタン表示設定）は誰も見ておらず、入口へ 1 行足すだけで見る先がずれた。
                 * `focus_page()` で名指しする（`focus_item()` と同じ理由）。
                 */
                for (int page_index = 0; page_index < FeatureMenu::page_count(); ++page_index) {
                    menu.close();
                    menu.open(settings);
                    if (!menu.focus_page(page_index)) {
                        fail("機能メニュー: 画面を出せません");
                    }
                    const auto note = menu.overflow_report(text, probe_layout, settings);
                    if (!note.empty()) {
                        char buf[320]{};
                        std::snprintf(buf, sizeof(buf), "機能メニュー[%s]: %s", info.code, note.c_str());
                        fail(buf);
                    }
                }
            }
            i18n::set_language(lang_before);
            menu.close();
            menu.open(settings);
        }

        /*
         * 操作の割り当て（2026-08-11 に決めた）。確かめるのは 4 つ:
         *   1. 決定で変更モードに入る
         *   2. 押したキーがその操作の割り当てになる
         *   3. **同じキーが 2 つの操作に乗らない**（先客から外れる）
         *   4. ESC で割り当てが消える／15 秒で変更モードを降りる
         * どれが落ちても「割り当てたのに効かない」「割り当て画面から出られない」になる。
         */
        menu.open(settings);
        menu.update(1000);
        if (!menu.focus_item(i18n::tr("hd2d.app.hd2d-app.open-the-feature-menu"))) {
            fail("操作の割り当て: 「機能メニューを開く」が見つかりません");
        }
        menu.handle(MenuNav::Confirm, settings);
        if (!menu.waiting_for_key()) {
            fail("操作の割り当て: 変更モードに入りません");
        }
        //! 予約キー（十字）は受け取らず、**変更モードのまま**であること。
        (void)menu.handle_bind_key(SDLK_UP, 0, settings);
        if (!menu.waiting_for_key()) {
            fail("操作の割り当て: 予約キー（↑）で変更モードを降りてしまいました");
        }
        const KeyBindEntry cycle_key = settings.key_binds.find(kActionLayoutCycle);
        if (!menu.handle_bind_key(cycle_key.keycode, cycle_key.mods, settings)) {
            fail("操作の割り当て: 押したキーを受け取りません");
        }
        if (settings.key_binds.find(kActionFeatureMenu).keycode != cycle_key.keycode) {
            fail("操作の割り当て: 割り当てが反映されていません");
        }
        if (settings.key_binds.find(kActionLayoutCycle).keycode == cycle_key.keycode) {
            fail("操作の割り当て: 同じキーが 2 つの操作に割り当たりました");
        }
        //! ESC は**取消ではなく消去**（決めたこと）。
        menu.handle(MenuNav::Confirm, settings);
        if (!menu.handle_bind_key(SDLK_ESCAPE, 0, settings)) {
            fail("操作の割り当て: ESC を受け取りません");
        }
        if (settings.key_binds.find(kActionFeatureMenu).keycode != 0) {
            fail("操作の割り当て: ESC で割り当てが消えません");
        }
        //! 15 秒未入力で降りること。
        menu.handle(MenuNav::Confirm, settings);
        if (!menu.waiting_for_key()) {
            fail("操作の割り当て: 変更モードに入り直せません");
        }
        menu.update(1000 + kBindWaitTimeoutMs - 1);
        if (!menu.waiting_for_key()) {
            fail("操作の割り当て: 15 秒より前に変更モードを降りました");
        }
        menu.update(1000 + kBindWaitTimeoutMs);
        if (menu.waiting_for_key()) {
            fail("操作の割り当て: 15 秒たっても変更モードを降りません");
        }
        //! 使える状態へ戻しておく（この後の往復検査と描画が続く）。
        settings.key_binds = default_key_binds();

        /*
         * 「終了」は**無い**こと（2026-08-11 に決めた「HD2D 用メニューの終了は削除して。
         * 通常の終了のみで終わらせる」）。以前は「選べて伝わる」ことを見ていた検査なので、
         * 消しただけだと「戻ってきた」ことに気づけない。**無いことを見続ける。**
         */
        menu.open(settings);
        if (menu.focus_item(i18n::tr("hd2d.app.hd2d-app.quit"))) {
            fail("機能メニュー: 削除したはずの「終了」がまだ並んでいます");
        }
        menu.close();

        /*
         * 設定の往復。**書いて読んで一致する**こと。
         * ここが落ちると、機能メニューで詰めた値が次の起動で静かに消える。
         */
        const std::string cfg_path = "hd2d_ui_check.cfg";
        settings.camera_pitch_deg = 51.f;
        settings.camera_cell_px = 125.f;
        settings.layout = LayoutMode::Split;
        settings.layout_portrait = LayoutMode::Tall;
        settings.post.bloom = false;
        settings.cutaway_radius = 0.f;
        /*
         * 音（2026-08-21 に決めた）。**音楽と効果音でわざと違う値**を入れる——
         * 同じ値だと「片方しか書いていない」を見逃す（バーチャルパッドの縦横と同じ理由）。
         */
        settings.bgm_mode = Hd2dSettings::BgmMode::Ambience;
        settings.music_volume = 3;
        settings.sound_enabled = true;
        settings.sound_volume = 7;
        /*
         * **縦横で別の配置**（2026-08-12 に決めた）が往復すること。同じ値を入れると
         * 「片方しか書いていない」を見逃すので、**わざと違う数**を入れる。
         */
        settings.vpad.slot[kVpadLandscape][static_cast<int>(VpadControl::A)] = VpadSlot{ 0.11f, 0.22f, 1.30f };
        settings.vpad.slot[kVpadPortrait][static_cast<int>(VpadControl::A)] = VpadSlot{ 0.77f, 0.88f, 0.70f };
        if (!save_settings(cfg_path, settings)) {
            fail("設定の保存: 書けません");
        }
        Hd2dSettings reloaded;
        if (!load_settings(cfg_path, reloaded)) {
            fail("設定の保存: 読み戻せません");
        }
        if (reloaded.differs_from(settings)) {
            fail("設定の保存: 書いた値と読んだ値が違います");
        }
        (void)std::remove(cfg_path.c_str());

        /*
         * 音（2026-08-21 に決めた）。**つまみがプロトコルまで届くこと。**
         * ここが落ちると「メニューの値だけ変わって何も起きない」——リアルタイム進行で
         * 一度踏んだ穴と同じで、画面はまったく正常に見える。見るのは 3 つ:
         *   1. 段の 10 がいちばん大きい音（＝添字 0）へ、1 がいちばん小さい音（＝添字 9）へ
         *   2. **入のまま 0 まで絞ったら「切」として届く**（プロトコルに 0% の段が無い）
         *   3. 切ったら音量に関わらず「切」
         */
        {
            const ViewWindow probe_window{ 40, 20, 0.f, 0.f, 0.f, 0.f };
            const UiLayout probe_layout = UiLayout::compute(
                LayoutMode::Split, 1280, 720, text.cell_w(), text.cell_h(), false);
            Hd2dSettings probe = settings;
            probe.bgm_mode = Hd2dSettings::BgmMode::Music;
            probe.music_volume = Hd2dSettings::kVolumeMax;
            probe.sound_enabled = true;
            probe.sound_volume = 1;
            presentation::UiStateMessage audio = build_ui_state(probe_window, probe_layout, probe, true);
            if (!audio.music_on || (audio.music_volume_index != 0)) {
                fail("音: 音楽を最大にしても最大音量で届きません");
            }
            //! **環境音のときはコアの曲を止める**（両方鳴らさない。§3.1）。
            probe.bgm_mode = Hd2dSettings::BgmMode::Ambience;
            if (build_ui_state(probe_window, probe_layout, probe, true).music_on) {
                fail("音: 環境音のときにコアの曲を止めていません");
            }
            probe.bgm_mode = Hd2dSettings::BgmMode::Music;
            if (!audio.sound_on || (audio.sound_volume_index != (Hd2dSettings::kVolumeMax - 1))) {
                fail("音: 効果音を 1 にしても最小音量で届きません");
            }
            probe.music_volume = 0;
            probe.sound_enabled = false;
            audio = build_ui_state(probe_window, probe_layout, probe, true);
            if (audio.music_on) {
                fail("音: 音量 0 なのに音楽が「入」で届きます");
            }
            if (audio.sound_on) {
                fail("音: 切ったのに効果音が「入」で届きます");
            }
        }

        /*
         * 環境音。見るのは 3 つ:
         *   1. **表の引き**が条件どおりに効くこと（表は検査が用意する。データ file とは分ける）
         *   2. 配ってある `assets/audio/ambience.jsonc` が**読めて空でない**こと
         *   3. 音の装置が開けること（**鳴らさずに**源が立つところまで）
         * ここが落ちると「メニューで環境音を選んでも何も起きない」になる。
         */
        {
            using namespace audio;
            AmbienceTable table;
            std::vector<AmbienceRule> rules;
            AmbienceRule wild;
            wild.wild = true;
            wild.bed = ""; //!< **空は「ここで鳴らさない」の明示**。後ろの広い規則へ落ちないこと
            rules.push_back(wild);
            AmbienceRule night_town;
            night_town.floor_kind = static_cast<int>(FloorKind::Surface);
            night_town.night = true;
            night_town.bed = "town_night";
            rules.push_back(night_town);
            AmbienceRule surface;
            surface.floor_kind = static_cast<int>(FloorKind::Surface);
            surface.bed = "town_day";
            rules.push_back(surface);
            AmbienceRule shallow;
            shallow.floor_kind = static_cast<int>(FloorKind::Dungeon);
            shallow.depth_max = 20;
            shallow.bed = "cave_shallow";
            rules.push_back(shallow);
            table.set_rules(rules);

            AmbienceScene scene;
            scene.floor_kind = static_cast<int>(FloorKind::Surface);
            scene.day_minute = 12 * 60;
            if (table.bed_for(scene) != "town_day") {
                fail("環境音: 昼の地上で town_day になりません");
            }
            scene.day_minute = 23 * 60;
            if (table.bed_for(scene) != "town_night") {
                fail("環境音: 夜の地上で town_night になりません");
            }
            scene.wild = true;
            if (!table.bed_for(scene).empty()) {
                fail("環境音: 広域マップは鳴らさないはずです");
            }
            scene = AmbienceScene{};
            scene.floor_kind = static_cast<int>(FloorKind::Dungeon);
            scene.depth = 5;
            if (table.bed_for(scene) != "cave_shallow") {
                fail("環境音: 浅い階で cave_shallow になりません");
            }
            scene.depth = 50;
            if (!table.bed_for(scene).empty()) {
                fail("環境音: 表に無い深さで何かを鳴らそうとしています");
            }
            //! 時刻が分からない（負）ときは**昼**に落ちること（夜の音を既定にしない）。
            if (is_night(-1)) {
                fail("環境音: 時刻が分からないのに夜と判断しています");
            }

            /*
             * ---- 時刻の 4 帯----
             *
             * **夜の境目はコアの `is_daytime()`（06:00〜18:00 が日中）と同じ**でなければ
             * ならない。ずれると**画は昼なのに音は夜**という食い違いが出る。
             * ここは 1 分ずつ動かして境目を全部踏む。
             */
            {
                const struct {
                    int minute;
                    TimeBand want;
                    const char *note;
                } cases[] = {
                    { -1, TimeBand::Day, "時刻が分からない" },
                    { 0, TimeBand::Night, "00:00" },
                    { (6 * 60) - 1, TimeBand::Night, "05:59" },
                    { 6 * 60, TimeBand::Dawn, "06:00（コアの日中の始まり）" },
                    { (9 * 60) - 1, TimeBand::Dawn, "08:59" },
                    { 9 * 60, TimeBand::Day, "09:00" },
                    { (15 * 60) - 1, TimeBand::Day, "14:59" },
                    { 15 * 60, TimeBand::Dusk, "15:00" },
                    { (18 * 60) - 1, TimeBand::Dusk, "17:59" },
                    { 18 * 60, TimeBand::Night, "18:00（コアの日中の終わり）" },
                };
                for (const auto &c : cases) {
                    if (time_band(c.minute) != c.want) {
                        fail((std::string("環境音: 時刻の帯が違う — ") + c.note).c_str());
                    }
                }
                //! 帯の境目が `is_night()` と食い違っていないこと（同じ 1 か所から出す約束）。
                for (int m = 0; m < 24 * 60; m += 7) {
                    const bool band_says_night = (time_band(m) == TimeBand::Night);
                    if (band_says_night != is_night(m)) {
                        fail("環境音: 時刻の帯と昼夜の判定が食い違っています");
                    }
                }
            }

            /*
             * ---- 足元の材料（§8.1）----
             * **壁を数に入れてはいけない。**洞窟は壁が半分を超えるので、混ぜると常に壁が勝つ。
             */
            {
                AmbienceSurroundings none;
                if (dominant_ground(none) != GroundKind::Unknown) {
                    fail("環境音: 数えていないのに材料を決めています");
                }
                AmbienceSurroundings cave;
                cave.radius = 12;
                cave.counted = 400;
                cave.wall = 160; //!< 壁だらけ。材料はどれも 0
                if (dominant_ground(cave) != GroundKind::Stone) {
                    fail("環境音: 洞窟が石になりません（壁を材料に数えている疑い）");
                }
                AmbienceSurroundings field;
                field.radius = 12;
                field.counted = 400;
                field.grass = 130;
                field.tree = 28;
                if (dominant_ground(field) != GroundKind::Grass) {
                    fail("環境音: 草原が草になりません");
                }
                AmbienceSurroundings forest;
                forest.radius = 12;
                forest.counted = 400;
                forest.tree = 150;
                forest.grass = 30;
                if (dominant_ground(forest) != GroundKind::Forest) {
                    fail("環境音: 森が木になりません");
                }
            }

            /*
             * ---- 重ねる層（§8.2）----
             */
            {
                AmbienceSurroundings cave;
                cave.radius = 12;
                cave.counted = 400;
                cave.wall = 200;
                cave.water = 40;
                const auto indoor = decide_layers(cave, TimeBand::Day, false);
                bool has_life = false;
                bool has_enclosed = false;
                bool has_water = false;
                for (const auto &layer : indoor) {
                    if (layer.name.rfind("layer_life_", 0) == 0) {
                        has_life = true;
                    }
                    if (layer.name == "layer_enclosed") {
                        has_enclosed = true;
                    }
                    if (layer.name == "layer_water") {
                        has_water = true;
                    }
                }
                //! **洞窟で鳥や虫を鳴らさない。**空の下だけの層である。
                if (has_life) {
                    fail("環境音: 地下なのに生き物の層が乗っています");
                }
                if (!has_enclosed) {
                    fail("環境音: 壁だらけなのに閉塞の層が乗りません");
                }
                if (!has_water) {
                    fail("環境音: 水が見えているのに水の層が乗りません");
                }
                AmbienceSurroundings meadow;
                meadow.radius = 12;
                meadow.counted = 400;
                meadow.grass = 140;
                const auto night = decide_layers(meadow, TimeBand::Night, true);
                bool life_night = false;
                for (const auto &layer : night) {
                    if (layer.name == "layer_life_night") {
                        life_night = true;
                    }
                }
                if (!life_night) {
                    fail("環境音: 夜の草原で夜の生き物の層が乗りません");
                }
                //! 数えていなければ層は 1 本も出ない（ベッドだけに落ちる）。
                AmbienceSurroundings unknown;
                if (!decide_layers(unknown, TimeBand::Day, true).empty()) {
                    fail("環境音: 数えていないのに層を鳴らそうとしています");
                }
            }

            /*
             * ---- 切り替えの寸断を作らない（§8.4。決めたこと）----
             *
             * > マスの移動毎に切り替えると寸断で不自然になるので、切り替わりはタイムラグを
             * > 許容しフェードアウトとフェードインを完了させながら変化させていく。
             *
             * **音の装置を開かずに突く。**`AudioEngine` は装置が無ければ何もしないので、
             * ここで見ているのは「段取り役がいつ動くと決めたか」だけである。
             */
            {
                AudioEngine silent; //!< `open()` していない＝鳴らない
                AmbienceDirector director;
                director.configure(1.5f, 1.2f);
                const std::vector<AmbienceLayerGain> no_layers;
                //! 1 歩ごとに要求が来ても、落ち着き待ちの間は動かない。
                for (int i = 0; i < 10; ++i) {
                    director.update(0.1f, "field_grass", no_layers, silent, "assets/audio/bed");
                }
                if (!director.bed().empty()) {
                    fail("環境音: 落ち着き待ちの前にベッドを動かしています");
                }
                director.update(0.6f, "field_grass", no_layers, silent, "assets/audio/bed");
                if (director.bed() != "field_grass") {
                    fail("環境音: 落ち着いてもベッドが動きません");
                }
                if (!director.switching()) {
                    fail("環境音: 入れ替えたのに入れ替え中になっていません");
                }
                //! **入れ替えの最中は次へ行かない。**ここが「寸断」を止めている本体。
                for (int i = 0; i < 8; ++i) {
                    director.update(0.1f, "field_forest", no_layers, silent, "assets/audio/bed");
                }
                if (director.bed() != "field_grass") {
                    fail("環境音: 入れ替えの最中なのに次のベッドへ移りました");
                }
                //! 入れ替えが終わり、かつ落ち着いてから移る。
                for (int i = 0; i < 20; ++i) {
                    director.update(0.1f, "field_forest", no_layers, silent, "assets/audio/bed");
                }
                if (director.bed() != "field_forest") {
                    fail("環境音: 入れ替えが終わっても次のベッドへ移りません");
                }
                //! 境目を行ったり来たりしても動かない（落ち着き待ちが効いている）。
                AmbienceDirector flapping;
                flapping.configure(1.5f, 1.2f);
                for (int i = 0; i < 40; ++i) {
                    const char *want = ((i % 2) == 0) ? "field_grass" : "field_forest";
                    flapping.update(0.1f, want, no_layers, silent, "assets/audio/bed");
                }
                if (!flapping.bed().empty()) {
                    fail("環境音: 境目で揺れているのにベッドが動きました");
                }
            }

            //! 配ってある表そのもの。**読めて空でない**こと（`assets/` は追跡されている）。
            AmbienceTable shipped;
            std::string table_log;
            if (!shipped.load("assets/audio", &table_log)) {
                fail((std::string("環境音: ") + table_log).c_str());
            }
            if (shipped.rule_count() == 0) {
                fail("環境音: assets/audio/ambience.jsonc に規則が 1 本もありません");
            }

            /*
             * 音の装置。**鳴らさない**——音量を 0 にしてから源を立て、
             * 「立った」ところまでを見る（机の前で音が出ると検査が嫌われる。
             * `GB_AUDIO_SILENT` と同じ考え）。
             */
            AudioEngine engine;
            std::string audio_log;
            if (!engine.open(&audio_log)) {
                fail((std::string("音: 装置を開けません — ") + audio_log).c_str());
            }
            engine.set_bed_gain(0.f);
            std::vector<short> silence(4410, 0); //!< 0.1 秒ぶん。**中身は無音**
            if (!engine.play_test_pcm(silence.data(), silence.size(), 44100)) {
                fail("音: 源を立てられません（装置は開いたのに鳴らせない）");
            }
            engine.update(0.016f);
            /*
             * **ファイルから鳴らす道**も通す（`play_test_pcm` は器の中だけで、
             * wav の読み込みと形の揃え直しを 1 度も踏まない）。無音の wav を書いて消す。
             */
            {
                const std::string probe_dir = ".";
                const std::string probe_path = "hd2d_audio_check.wav";
                if (!write_silent_wav(probe_path, 4410)) {
                    fail("音: 検査用の wav を書けません");
                }
                const bool played = engine.play_bed("hd2d_audio_check", probe_dir);
                (void)std::remove(probe_path.c_str());
                if (!played) {
                    fail("音: wav からベッドを鳴らせません");
                }
                engine.update(0.016f);
            }
            /*
             * **配ってあるベッドそのもの**を 1 本ずつ読む（`tools/gen_audio_assets.py` が作る）。
             *
             * 上の検査は自分で書いた無音の wav なので、**素材の形が違っていても通る**。
             * 素材は手続き合成でも生成 AI でも差し替わりうるので、
             * 「置いたものが実際に読めるか」はここでしか分からない。
             * 素材が 1 つも無い木でも組めるよう、**在るものだけ**見る。
             */
            {
                const std::filesystem::path bed_dir = std::filesystem::path("assets") / "audio" / "bed";
                std::error_code ec;
                int checked = 0;
                if (std::filesystem::is_directory(bed_dir, ec)) {
                    for (const auto &entry : std::filesystem::directory_iterator(bed_dir, ec)) {
                        if (!entry.is_regular_file() || (entry.path().extension() != ".wav")) {
                            continue;
                        }
                        const std::string stem = entry.path().stem().string();
                        if (!engine.play_bed(stem, bed_dir.string())) {
                            fail((std::string("音: ベッド ") + stem + " を読めません（形が違うか壊れています）").c_str());
                        }
                        engine.update(0.016f);
                        ++checked;
                    }
                }
                std::printf("[ui-check] ベッドの素材: %d 本を読み込み\n", checked);
            }
            engine.close();
            /*
             * ---- 定位----
             *
             * **左右の取り違えは耳でしか気づけない**——しかも気づくのは実機で遊んでいる
             * ときになる。だから世界 → 聞き手の変換を 1 か所に閉じて、ここで向きを固定する。
             * 北（-y）を向いて立ち、東（+x）で音が鳴ったら**右から聞こえる**こと。
             */
            {
                const ListenerOffset east = to_listener_space(1.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, 1.f);
                if (east.right <= 0.f) {
                    fail("音: 北を向いているのに東の音が右から来ません（左右が逆）");
                }
                const ListenerOffset ahead = to_listener_space(0.f, -1.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, 1.f);
                if (ahead.forward <= 0.f) {
                    fail("音: 正面の音が前から来ません");
                }
                if (std::fabs(ahead.right) > 0.001f) {
                    fail("音: 正面の音が左右に偏っています");
                }
                //! 見る向きを東（+x）へ回したら、**同じ東の音は正面**になること。
                const ListenerOffset turned = to_listener_space(1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f);
                if ((turned.forward <= 0.f) || (std::fabs(turned.right) > 0.001f)) {
                    fail("音: 向きを変えても音の向きが付いてきません");
                }
            }

            /*
             * ---- 減衰と、回折の曲がり角（§4.1）----
             * **道のりで減衰させること**。直線距離だと、壁 1 枚隔てた敵が「すぐ隣」で鳴る。
             */
            {
                if (distance_gain(0) < 0.999f) {
                    fail("音: 足元の音が小さくなっています");
                }
                if (distance_gain(20) >= distance_gain(5)) {
                    fail("音: 遠い音のほうが大きく鳴ります");
                }
                SoundEvent event;
                event.y = 10;
                event.x = 20;
                //! 曲がり角が入っていれば**そちらの向き**。道のりはそのまま音量へ。
                event.heard_y = 4;
                event.heard_x = 5;
                event.path = 30;
                const SoundPlacement placed = place_sound(event, 5.5f, 5.5f);
                if ((std::fabs(placed.dx - (5.f - 5.5f)) > 0.001f)
                    || (std::fabs(placed.dy - (4.f - 5.5f)) > 0.001f)) {
                    fail("音: 曲がり角ではなく実際のマスから鳴らそうとしています");
                }
                if (placed.steps != 30) {
                    fail("音: 道のりを音量に使っていません");
                }
                //! 載っていなければ実際のマスとチェビシェフ距離へ落ちること（コア名で分岐しない）。
                SoundEvent bare;
                bare.y = 10;
                bare.x = 20;
                const SoundPlacement fallback = place_sound(bare, 5.5f, 5.5f);
                if (std::fabs(fallback.dx - 14.5f) > 0.001f) {
                    fail("音: 曲がり角が無いときに実際のマスへ落ちていません");
                }
                if (fallback.steps != 15) {
                    fail("音: 曲がり角が無いときの歩数がチェビシェフ距離になっていません");
                }
            }

            /*
             * ---- 効果音の目録（§2.1）----
             * 名前で引けること。**候補が複数なら回る**こと（同じ音が続くと機械的に聞こえる）。
             */
            {
                SfxCatalog probe;
                probe.set_entry("hit", { "a.wav", "b.wav" });
                if (probe.path_for("hit", 0) == probe.path_for("hit", 1)) {
                    fail("音: 候補が複数あるのに同じ wav ばかり選んでいます");
                }
                if (!probe.path_for("no_such_sound", 0).empty()) {
                    fail("音: 知らない名前に道を返しています");
                }
                SfxCatalog shipped_sfx;
                std::string sfx_log;
                if (!shipped_sfx.load("assets/audio", &sfx_log)) {
                    fail((std::string("音: ") + sfx_log).c_str());
                }
                if (shipped_sfx.name_count() == 0) {
                    fail("音: assets/audio/sfx.jsonc に音が 1 つもありません");
                }
                std::fprintf(stderr, "  (4''') 音の目録: %zu 種 / 素材は %s\n",
                    shipped_sfx.name_count(), shipped_sfx.sound_dir().c_str());
            }

            std::fprintf(stderr, "  (4'') 音: 表 %zu 本 / %s\n", shipped.rule_count(), audio_log.c_str());
        }

        /*
         * つまみそのものも回す。**決めたことは「0〜10」**なので、その両端で止まること
         * まで見る（片端だけ見ていると、上限を 1 段間違えても気づけない）。
         */
        {
            menu.close();
            menu.open(settings);
            if (!menu.focus_item(i18n::tr("hd2d.ui.feature-menu.sound-effects-volume"))) {
                fail("機能メニュー: 「効果音の音量」が見つかりません");
            }
            Hd2dSettings knob = settings;
            knob.sound_volume = 5;
            menu.handle(MenuNav::Right, knob);
            if (knob.sound_volume != 6) {
                fail("音: → で効果音の音量が上がりません");
            }
            for (int i = 0; i < (Hd2dSettings::kVolumeMax + 4); ++i) {
                menu.handle(MenuNav::Left, knob);
            }
            if (knob.sound_volume != 0) {
                fail("音: ← が 0（無音）で止まりません");
            }
            for (int i = 0; i < (Hd2dSettings::kVolumeMax + 4); ++i) {
                menu.handle(MenuNav::Right, knob);
            }
            if (knob.sound_volume != Hd2dSettings::kVolumeMax) {
                fail("音: → が上限（10）で止まりません");
            }
            menu.close();
        }

        // 描く（画素になること）。
        HudState state;
        const UiLayout layout = UiLayout::compute(LayoutMode::Hybrid, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false);
        FeatureMenu shown;
        shown.open(settings);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screen_w, screen_h);
        glClearColor(static_cast<float>(clear[0]) / 255.f, static_cast<float>(clear[1]) / 255.f,
            static_cast<float>(clear[2]) / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        paint.begin(screen_w, screen_h);
        text.begin(screen_w, screen_h);
        draw_game_ui(paint, text, layout, frame, state);
        shown.draw(paint, text, layout, settings);
        paint.flush();
        text.flush();
        const std::vector<unsigned char> image = read_framebuffer(screen_w, screen_h);
        const RectPx centre{ screen_w / 3, screen_h / 3, screen_w / 3, screen_h / 3 };
        const std::size_t painted = count_painted_pixels(image, screen_w, screen_h, centre, clear);
        if (painted == 0) {
            fail("機能メニュー: 画面の真ん中に何も描かれていません");
        }
        std::fprintf(stderr, "  (4) 機能メニュー: 中央の画素 %zu / 設定の往復 OK\n", painted);
        if (!options.shot_path.empty()) {
            std::string path = options.shot_path;
            const std::size_t dot = path.find_last_of('.');
            path = (dot == std::string::npos) ? (path + "_menu") : (path.substr(0, dot) + "_menu" + path.substr(dot));
            (void)save_framebuffer_bmp(path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);

        /*
         * (4') 操作の割り当ての一覧（2026-08-11 に決めた）。
         * **窓に入らない数の行を渡して描く。**行が窓を溢れたときに落ちない・
         * カーソルが窓の外へ出ないことを、実際に画素にして確かめる
         * （溢れる形の検査を通さないと、スクロールの入っていない版でも PASS する）。
         */
        std::vector<PadCommand> many;
        for (int i = 0; i < 80; ++i) {
            char label[64]{};
            std::snprintf(label, sizeof(label), "検査のコマンド %d", i + 1);
            many.push_back(PadCommand{ i + 1, 'a' + (i % 26), (i < 40) ? "移動" : "魔法", label,
                { 'a' + (i % 26) } });
        }
        shown.set_pad_state(many, false, std::string());
        shown.open(settings);
        shown.update(2000);
        /*
         * 探す先は**この検査が自分で作った行**であって画面の文言ではない。
         * `tr()` を通すと、上の `snprintf` はリテラルのままなので言語を変えた途端に
         * 食い違う。**同じリテラルから組み立てて突き合わせる。**
         */
        char want_label[64]{};
        std::snprintf(want_label, sizeof(want_label), "検査のコマンド %d", 70);
        if (!shown.focus_item(want_label)) {
            fail("操作の割り当て: 一覧の行へカーソルを合わせられません");
        }
        //! 右の列（コントローラー）を選んで、変更モードの案内まで出た状態を描く。
        shown.handle(MenuNav::Right, settings);
        shown.handle(MenuNav::Confirm, settings);
        if (!shown.waiting_for_key()) {
            fail("操作の割り当て: 一覧から変更モードに入れません");
        }
        /*
         * 最下段の「全ボタンの割り当て」（2026-08-11 に決めた）も同じ絵で確かめる。
         * **13 入力ぶんは 1 行に収まらない**ので、折り返しが効いていることを画素で見る。
         */
        HudState bind_state = state;
        //! 検査用の合成なので、マクロは 1 つも乗っていないことにする（`PadMacroFlags{}`）。
        bind_state.controller_binds = build_pad_bind_items(settings.pad, many, PadMacroFlags{}, 0);
        /*
         * **`Split` で描く。**既定の作りであり、**地図の枠が窓と別にある唯一の作り**なので、
         * 「メインのマップパネルに収まる」（2026-08-11 に決めた）を確かめられるのはここだけ。
         * `Full` / `Hybrid` では `scene` が窓そのものなので、収まっていても何も言えない。
         */
        const UiLayout bind_layout = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        paint.begin(screen_w, screen_h);
        text.begin(screen_w, screen_h);
        draw_game_ui(paint, text, bind_layout, frame, bind_state);
        shown.draw(paint, text, bind_layout, settings);
        paint.flush();
        text.flush();
        const std::vector<unsigned char> bind_image = read_framebuffer(screen_w, screen_h);
        //! **下の帯**（一覧の末尾と操作説明）に字があること。詰め方を間違えると空になる。
        const RectPx lower{ screen_w / 4, (screen_h * 3) / 4, screen_w / 2, screen_h / 8 };
        const std::size_t bind_painted = count_painted_pixels(bind_image, screen_w, screen_h, lower, clear);
        if (bind_painted == 0) {
            fail("操作の割り当て: 一覧の下側に何も描かれていません");
        }
        /*
         * **地図の枠からはみ出していないこと**（2026-08-11 に決めた）。
         * 画素で追うと状態列やミニマップの字と見分けがつかないので、**描くのに使った矩形**
         * （`panel_rect()`。`draw()` と同じ所から出る）で押さえる。
         * 窓の大きさを変えても効くように、3 通りで確かめる。
         */
        const RectPx &map_rect = bind_layout.scene;
        if (map_rect.empty()) {
            fail("操作の割り当て: Split の地図の矩形が空です");
        }
        for (const auto &size : { std::pair<int, int>{ screen_w, screen_h },
                 std::pair<int, int>{ 1280, 720 }, std::pair<int, int>{ 1024, 600 } }) {
            const UiLayout probe = UiLayout::compute(LayoutMode::Split, size.first, size.second,
                text.cell_w(), text.cell_h(), false);
            const RectPx panel = shown.panel_rect(probe);
            const RectPx &map = probe.scene;
            if ((panel.x < map.x) || (panel.y < map.y) || ((panel.x + panel.w) > (map.x + map.w))
                || ((panel.y + panel.h) > (map.y + map.h))) {
                char buf[192]{};
                std::snprintf(buf, sizeof(buf),
                    "操作の割り当て: %dx%d でパネル (%d,%d %dx%d) が地図の枠 (%d,%d %dx%d) からはみ出しています",
                    size.first, size.second, panel.x, panel.y, panel.w, panel.h, map.x, map.y, map.w, map.h);
                fail(buf);
            }
        }
        std::fprintf(stderr, "  (4') 操作の割り当て: 80 件を溢れさせて Split で描画"
                             "（地図の枠 %dx%d に収まる・下側の画素 %zu）\n",
            map_rect.w, map_rect.h, bind_painted);
        if (!options.shot_path.empty()) {
            std::string path = options.shot_path;
            const std::size_t dot = path.find_last_of('.');
            path = (dot == std::string::npos) ? (path + "_binds") : (path.substr(0, dot) + "_binds" + path.substr(dot));
            (void)save_framebuffer_bmp(path, screen_w, screen_h);
        }
        SDL_GL_SwapWindow(window.window);

        /*
         * (4''''') **決定でコマンドメニューが開く条件**（2026-08-23 に決めた
         * 「A に、決定と同じく既定でコマンドメニューを置きたい」）。
         *
         * 規則は `confirm_opens_command_menu()` の 1 か所で、パッドの A と
         * キーボードの Enter が同じそこを見る。ここで押さえるのは 3 つ:
         *   1. **命令を待っているときだけ開く**（コアが `awaiting_command` を立てたとき）
         *   2. **`-more-` や `y/n` の待ちでは開かない**（旗が降りている＝決定のまま流す）
         *   3. **文字を打っている最中は開かない**（打ち終わりの Enter を奪わない）
         *
         * 2 は「旗を立てないコア（変愚・短愚・幻想）では今までどおり」も兼ねている
         * ——あちらは `\r` を受けたコア自身が通常メニューを開く。
         */
        {
            GameFrame waiting{};
            waiting.awaiting_command = true;
            if (!confirm_opens_command_menu(waiting)) {
                fail("決定の行き先: 命令を待っているのにコマンドメニューが開きません");
            }
            GameFrame busy{};
            busy.awaiting_command = false;
            if (confirm_opens_command_menu(busy)) {
                fail("決定の行き先: 命令待ちでないのにコマンドメニューが開きます"
                     "（`-more-` が進まなくなります）");
            }
            GameFrame typing{};
            typing.awaiting_command = true;
            typing.text_input_active = true;
            if (confirm_opens_command_menu(typing)) {
                fail("決定の行き先: 文字を打っている最中にコマンドメニューが開きます");
            }
            std::fprintf(stderr, "  (4''''') 決定の行き先: 命令待ちだけで開く"
                                 "（`-more-`・文字入力では開かない）\n");
        }

        /*
         * (4'') **コマンドメニュー**（2026-08-22 に決めた）。確かめるのは 4 つ:
         *   1. 分類の見出しには止まらない（開いた所で選べる行に乗っている）
         *   2. 決定で**その行の `id`** が要求として立つ
         *   3. **出せない命令（`keys` が空）は選べない**——押しても何も起きない行を作らない
         *   4. 溢れる数を渡しても描ける
         *
         * 一覧はコアから届くので、ここでは**合成の表**を渡す。上の `many` を流用せず
         * 別に組むのは、こちらは「出せない命令」を混ぜたいからである。
         */
        {
            std::vector<PadCommand> menu_cmds;
            for (int i = 0; i < 60; ++i) {
                char label[64]{};
                std::snprintf(label, sizeof(label), "検査の命令 %d", i + 1);
                //! **3 の倍数は `keys` を空にする**（＝いまの配列では出せない命令）。
                std::vector<int> keys;
                if ((i % 3) != 0) {
                    keys.push_back('a' + (i % 26));
                }
                menu_cmds.push_back(PadCommand{ i + 1, 'a' + (i % 26),
                    (i < 20) ? "行動" : ((i < 40) ? "道具" : "情報"), label, keys });
            }
            /*
             * **4 つ目の分類だけ小分類を持たせる**（2026-08-23 に決めた
             * 「行動メニューをグループ化しもう 1 階層深くして」）。
             * 上の 3 分類は小分類を持たないままにしてある——**混ざるのが正しい姿**で、
             * 変愚のように 1 つも送らないコアは 2 段のままでなければならない。
             */
            for (int i = 60; i < 72; ++i) {
                char label[64]{};
                std::snprintf(label, sizeof(label), "検査の深い命令 %d", i + 1);
                std::vector<int> keys;
                if ((i % 3) != 0) {
                    keys.push_back('a' + (i % 26));
                }
                PadCommand deep{ i + 1, 'a' + (i % 26), "小分類つき", label, keys };
                deep.subgroup_utf8 = (i < 66) ? "小分類 A" : "小分類 B";
                menu_cmds.push_back(std::move(deep));
            }
            FeatureMenu cm;
            cm.set_pad_state(menu_cmds, false, std::string());
            cm.open_commands(settings);
            cm.update(2000);
            /*
             * 1. **開いた所は分類の一覧**（1 段目）。決定しても命令は要求されず、
             *    分類へ潜るだけであること。**ここが 2 段構えの要点**である
             *    （2026-08-22 に決めた「変愚のメニューのように各グループ毎に階層化して」）。
             */
            cm.handle(MenuNav::Confirm, settings);
            if (cm.take_command_request() != 0) {
                fail("コマンドメニュー: 分類の行で命令が走りました（階層になっていません）");
            }
            /*
             * 1'. **盤の綴りが名札に添えてある**（2026-08-23 に決めた。追補 ⑦
             * 「割り当たっているキーを全部出す」。例は「拾う（`,`）」）。
             *
             * 合成の表は `keys = {'a' + (i % 26)}` なので、「行動」の中の 2 件目
             * （`id=2`・`i=1`）は `検査の命令 2 (b)` になっていなければならない。
             * **出せない命令には綴りを出さない**（`id=1` は `keys` が空）。
             *
             * 綴りは決め打ちしないのが要点なので、**綴りが出ていること**と
             * **出せない行には出ていないこと**の両方を見る。
             */
            {
                const std::vector<std::string> labels = cm.command_labels();
                const auto has = [&labels](const std::string &want) {
                    return std::find(labels.begin(), labels.end(), want) != labels.end();
                };
                if (!has("検査の命令 2 (b)")) {
                    fail("コマンドメニュー: 名札に盤の綴りが出ていません（「検査の命令 2 (b)」のはず）");
                }
                if (!has("検査の命令 3 (c)")) {
                    fail("コマンドメニュー: 綴りが 1 件だけしか出ていません（「検査の命令 3 (c)」のはず）");
                }
                /*
                 * 出せない行は**名札＋「出せません」だけ**であること。
                 * 括弧の有無では見ない——「出せません」の字そのものに括弧が入っている
                 * （`assets/lang/*.json`。英語は ` (not available)`）ので、
                 * 綴りと見分けがつかない。
                 */
                const std::string absent
                    = std::string("検査の命令 1") + i18n::tr("hd2d.ui.feature-menu.not-available-on-this-core");
                if (!has(absent)) {
                    fail("コマンドメニュー: 出せない命令にも綴りが出ています（id=1 は keys が空）");
                }
            }
            //! 2. 潜った先では決定でその命令が要求されること。
            cm.handle(MenuNav::Confirm, settings);
            const int first = cm.take_command_request();
            if (first <= 0) {
                fail("コマンドメニュー: 分類の中で決定しても何も要求されません");
            }
            //! 2 回目に読むと下りていること（1 回きりの約束）。
            if (cm.take_command_request() != 0) {
                fail("コマンドメニュー: 要求が下りません（読んでも残っています）");
            }
            /*
             * 3. **分類の中を下へ動かして、止まった行が全部「出せる命令」であること。**
             * `id` は 1 始まりで `keys` が空なのは `id % 3 == 1`（`i % 3 == 0`）なので、
             * 止まった `id` にその余りが出たら、選べない行に乗っている。
             *
             * **同じ分類の中に留まること**も見る（`i < 20` が「行動」なので、
             * 潜った先で出る `id` は 1〜20 の中でなければならない）。
             */
            int seen = 0;
            for (int step = 0; step < 12; ++step) {
                cm.handle(MenuNav::Down, settings);
                cm.handle(MenuNav::Confirm, settings);
                const int id = cm.take_command_request();
                if (id <= 0) {
                    fail("コマンドメニュー: 動かした先の決定で何も要求されません");
                    break;
                }
                if ((id % 3) == 1) {
                    char buf[160]{};
                    std::snprintf(buf, sizeof(buf),
                        "コマンドメニュー: 出せない命令（id=%d）にカーソルが止まりました", id);
                    fail(buf);
                    break;
                }
                if (id > 20) {
                    char buf[160]{};
                    std::snprintf(buf, sizeof(buf),
                        "コマンドメニュー: 分類「行動」の中に別の分類の命令（id=%d）が出ました", id);
                    fail(buf);
                    break;
                }
                ++seen;
            }
            //! 4. **戻るで 1 段だけ戻ること**（入口まで飛ばない）。戻ったら分類の行に居る。
            cm.handle(MenuNav::Cancel, settings);
            cm.handle(MenuNav::Confirm, settings);
            if (cm.take_command_request() != 0) {
                fail("コマンドメニュー: 戻るで分類の一覧へ戻っていません（命令が走りました）");
            }
            if (!cm.is_open()) {
                fail("コマンドメニュー: 戻るでパネルごと閉じてしまいました（1 段だけ戻ること）");
            }
            /*
             * 5. **2 列であること**（2026-08-23 に決めた）。分類は 4 つあり、
             *    行優先に置くので `↓` は**隣のマスではなく 1 段下**——0 番「行動」から
             *    ↓ で 2 番「情報」（id 41〜60）へ動く。1 列のままなら 1 番「道具」
             *    （id 21〜40）に入るので、入った先の `id` で見分けられる。
             */
            cm.handle(MenuNav::Cancel, settings); //!< 「行動」から分類の一覧へ
            cm.handle(MenuNav::Down, settings);
            cm.handle(MenuNav::Confirm, settings);
            cm.handle(MenuNav::Confirm, settings);
            {
                const int id = cm.take_command_request();
                if ((id < 41) || (id > 60)) {
                    char buf[192]{};
                    std::snprintf(buf, sizeof(buf),
                        "コマンドメニュー: ↓ が 1 段（2 件）進んでいません（入った分類の id=%d。"
                        "2 列なら 41〜60 の「情報」のはず）",
                        id);
                    fail(buf);
                }
            }
            //! 5'. `→` は**同じ段の隣のマス**（2 番「情報」→ 3 番「小分類つき」）。
            cm.handle(MenuNav::Cancel, settings);
            cm.handle(MenuNav::Right, settings);
            cm.handle(MenuNav::Confirm, settings);
            /*
             * 6. **3 段目があること**（「もう 1 階層深くして」と決めた）。
             *    小分類を持つ分類では、潜っても**まだ命令が走らない**。
             */
            cm.handle(MenuNav::Confirm, settings);
            if (cm.take_command_request() != 0) {
                fail("コマンドメニュー: 小分類の行で命令が走りました（3 段目になっていません）");
            }
            cm.handle(MenuNav::Confirm, settings);
            {
                const int id = cm.take_command_request();
                if ((id < 61) || (id > 72)) {
                    char buf[192]{};
                    std::snprintf(buf, sizeof(buf),
                        "コマンドメニュー: 小分類の中で走った命令が違います（id=%d。61〜72 のはず）", id);
                    fail(buf);
                }
            }
            //! 6'. **戻るは 1 段ずつ**（3 段目 → 2 段目 → 1 段目）。パネルはまだ開いている。
            cm.handle(MenuNav::Cancel, settings);
            cm.handle(MenuNav::Cancel, settings);
            if (!cm.is_open()) {
                fail("コマンドメニュー: 3 段目から 2 回戻っただけでパネルが閉じました");
            }
            /*
             * 7. **直に開いたパネルは、いちばん上で取消すと閉じること**（2026-08-23 に気づいた
             *    「メニューをキャンセルで抜けた際に機能メニューが立ち上がってしまう」）。
             *    ここが入口（機能メニュー）へ落ちると、押していない設定の画面が出てくる。
             */
            cm.handle(MenuNav::Cancel, settings);
            if (cm.is_open()) {
                fail("コマンドメニュー: 直に開いたパネルを取消しても閉じません（機能メニューが立ち上がります）");
            }
            /*
             * 7'. **入口から潜って来たときは入口へ戻る**（閉じない）。同じ取消で
             *     行き先が変わるのが要点なので、両方見ておく。
             */
            {
                FeatureMenu from_root;
                from_root.set_pad_state(menu_cmds, false, std::string());
                //! 入口を開くとカーソルは最初の節（＝`コマンド …`）に乗っている。
                from_root.open(settings);
                from_root.handle(MenuNav::Confirm, settings);
                from_root.handle(MenuNav::Confirm, settings); //!< 分類へ潜る（1 段目 → 2 段目）
                if (from_root.take_command_request() != 0) {
                    fail("コマンドメニュー: 入口から潜った先が分類の一覧ではありません");
                }
                from_root.handle(MenuNav::Cancel, settings); //!< 2 段目 → 1 段目
                from_root.handle(MenuNav::Cancel, settings); //!< 1 段目 → 入口
                if (!from_root.is_open()) {
                    fail("コマンドメニュー: 入口から潜ったのに、取消でパネルごと閉じました");
                }
            }
            //! 描くのは中身のある段で（溢れる数を送れること）。もう一度開いて「行動」へ潜る。
            cm.open_commands(settings);
            cm.handle(MenuNav::Confirm, settings);
            //! 4. 溢れる数でも描けること（下側に字が出る）。
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            paint.begin(screen_w, screen_h);
            text.begin(screen_w, screen_h);
            cm.draw(paint, text, bind_layout, settings);
            paint.flush();
            text.flush();
            const std::vector<unsigned char> cm_image = read_framebuffer(screen_w, screen_h);
            /*
             * **パネルそのものの下側**を見る（`lower` のような窓の座標ではない）。
             * このパネルは地図の枠の中に載るので、窓の下 1/4 を見ても何も無い——
             * すぐ上の (4') が `lower` で通っているのは、あちらが `draw_game_ui()` を
             * 先に描いていて**下の帯の字を数えていた**からで、一覧そのものは見ていない。
             * ここは**描くのに使った矩形から採る**（`panel_rect()`。必守制約 3 と同じ考え）。
             */
            const RectPx cm_box = cm.panel_rect(bind_layout);
            if (cm_box.empty()) {
                fail("コマンドメニュー: パネルの矩形が空です");
            }
            const RectPx cm_lower{ cm_box.x, cm_box.y + ((cm_box.h * 3) / 4), cm_box.w, cm_box.h / 4 };
            const std::size_t cm_painted = count_painted_pixels(cm_image, screen_w, screen_h, cm_lower, clear);
            if (cm_painted == 0) {
                fail("コマンドメニュー: 一覧の下側（パネルの下 1/4）に何も描かれていません");
            }
            //! **地図の枠からはみ出さないこと**（一覧のパネルは 2 つとも同じ約束。(4') と同じ形）。
            if ((cm_box.x < map_rect.x) || (cm_box.y < map_rect.y)
                || ((cm_box.x + cm_box.w) > (map_rect.x + map_rect.w))
                || ((cm_box.y + cm_box.h) > (map_rect.y + map_rect.h))) {
                fail("コマンドメニュー: パネルが地図の枠からはみ出しています");
            }
            std::fprintf(stderr, "  (4'') コマンドメニュー: 4 分類 72 件（うち 24 件は出せない）を"
                                 "2 列・最大 3 段で（分類の中で止まった行 %d / パネル %dx%d の下 1/4 の画素 %zu）\n",
                seen, cm_box.w, cm_box.h, cm_painted);
            if (!options.shot_path.empty()) {
                //! **段ごとに撮る**（階層になったので、1 枚だけでは形が分からない）。
                const auto shoot_menu = [&](const char *tag) {
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    paint.begin(screen_w, screen_h);
                    text.begin(screen_w, screen_h);
                    cm.draw(paint, text, bind_layout, settings);
                    paint.flush();
                    text.flush();
                    std::string path = options.shot_path;
                    const std::size_t dot = path.find_last_of('.');
                    path = (dot == std::string::npos) ? (path + tag)
                                                      : (path.substr(0, dot) + tag + path.substr(dot));
                    (void)save_framebuffer_bmp(path, screen_w, screen_h);
                };
                /*
                 * **1 段目へ戻してから撮る。** 受け入れ 4 の最後の決定でまた潜っているので、
                 * ここで戻さないと 2 枚とも同じ画（分類の中）になる（実際にそうなった）。
                 */
                cm.handle(MenuNav::Cancel, settings);
                shoot_menu("_commands"); //!< 1 段目（分類の一覧）
                cm.handle(MenuNav::Confirm, settings); //!< もう一度潜って中身も撮る
                (void)cm.take_command_request();
                shoot_menu("_commands_in");
                /*
                 * 3 段目も撮る（2026-08-23）。1 段目へ戻り、↓ → で 4 つ目の分類
                 * 「小分類つき」へ寄って、2 回潜る。**2 列の動きそのものの絵**でもある。
                 */
                cm.handle(MenuNav::Cancel, settings);
                cm.handle(MenuNav::Down, settings);
                cm.handle(MenuNav::Right, settings);
                cm.handle(MenuNav::Confirm, settings); //!< 2 段目（小分類の一覧）
                shoot_menu("_commands_sub");
                cm.handle(MenuNav::Confirm, settings); //!< 3 段目（小分類の中の命令）
                (void)cm.take_command_request();
                shoot_menu("_commands_deep");
            }
            SDL_GL_SwapWindow(window.window);
        }
    }

    /* ------------------------------------------ (5) クリックした所まで歩く */
    {
        /*
         * 合成フロアの中で歩ける 2 点を選び、**1 歩ずつしか積まないこと**と
         * **ずれたら捨てること**を確かめる。まとめて積む作りだと、途中で戦闘が
         * 割り込んだ瞬間に残りが別の場面のコマンドとして走る。
         */
        GameFrame walk = make_synthetic_frame(23, 19, 100, 44);
        /*
         * **プレイヤと同じ部屋の中**から、いちばん遠い歩けるマスを目的地にする。
         * 合成フロアの部屋と通路は繋がっていない（`make_synthetic_frame` は
         * 「部屋と通路が別に見えるか」を確かめるための形で、迷路として繋げてはいない）。
         * 遠くの通路を目的地にすると「届かない」が正しい答えになり、検査にならない。
         */
        int goal_gx = -1;
        int goal_gy = -1;
        int goal_reach = 1; //!< 隣（1 マス）は別の道を通るので 2 マス以上を選ぶ
        for (int y = walk.player_gy - 4; y <= (walk.player_gy + 4); ++y) {
            for (int x = walk.player_gx - 5; x <= (walk.player_gx + 5); ++x) {
                if (!minimap_walkable(walk.minimap, x, y)) {
                    continue;
                }
                // **`far` という名前は使えない**（MSVC の古いメモリモデルのマクロが残っている）。
                const int reach = std::max(std::abs(x - walk.player_gx), std::abs(y - walk.player_gy));
                if (reach > goal_reach) {
                    goal_reach = reach;
                    goal_gx = x;
                    goal_gy = y;
                }
            }
        }
        if (goal_gx < 0) {
            fail("クリック移動: 合成フロアに歩ける目的地が見つかりません");
        } else {
            ClickPath path;
            if (!path.begin(walk.minimap, walk.player_gx, walk.player_gy, goal_gx, goal_gy)) {
                fail("クリック移動: 経路を引けません");
            }
            const std::size_t length = path.remaining();
            if (length == 0) {
                fail("クリック移動: 経路が空です");
            }

            /*
             * 目的地まで歩かせる。**1 回の `advance` で積むのは高々 1 件。**
             * `advance` は「直前の 1 歩を実座標で確認 → 次の 1 歩を積む」を 1 回でやるので、
             * 積まずに戻ることがある（最後の 1 歩を確認した回）。そのときは
             * `active()` が偽になっているはずで、**偽でないのに積まないなら止まっている**。
             */
            int steps = 0;
            while (path.active() && (steps < 400)) {
                presentation::InputEventsMessage out;
                path.advance(walk, out);
                if (out.events.size() > 1) {
                    fail("クリック移動: 1 回に 2 歩以上積んでいます");
                    break;
                }
                if (out.events.empty()) {
                    if (path.active()) {
                        fail("クリック移動: 進みません");
                    }
                    break;
                }
                walk.player_gx += out.events[0].dx;
                walk.player_gy += out.events[0].dy;
                ++steps;
            }
            if ((walk.player_gx != goal_gx) || (walk.player_gy != goal_gy)) {
                fail("クリック移動: 目的地に着きません");
            }

            //! **ずれたら捨てる**（壁バンプ・戦闘割込）。
            GameFrame bumped = make_synthetic_frame(23, 19, 100, 44);
            ClickPath dropped;
            (void)dropped.begin(bumped.minimap, bumped.player_gx, bumped.player_gy, goal_gx, goal_gy);
            presentation::InputEventsMessage first;
            dropped.advance(bumped, first);
            bumped.player_gx += 7; // 転移した（積んだ 1 歩とは無関係な場所）
            presentation::InputEventsMessage after;
            dropped.advance(bumped, after);
            if (dropped.active()) {
                fail("クリック移動: 実座標がずれても経路を捨てていません");
            }

            //! **メニューの最中は積まない**（選択が勝手に動く）。
            GameFrame in_menu = make_synthetic_frame(23, 19, 100, 44);
            in_menu.menu_open = true;
            ClickPath halted;
            (void)halted.begin(in_menu.minimap, in_menu.player_gx, in_menu.player_gy, goal_gx, goal_gy);
            presentation::InputEventsMessage none;
            halted.advance(in_menu, none);
            if (!none.events.empty() || halted.active()) {
                fail("クリック移動: メニューの最中に方向を積んでいます");
            }

            //! 未踏破・壁は目的地にしない。
            ClickPath nowhere;
            if (nowhere.begin(walk.minimap, walk.player_gx, walk.player_gy, 0, 0)) {
                fail("クリック移動: 通れない所へ経路を引きました");
            }
            std::fprintf(stderr, "  (5) クリック移動: %zu 歩の経路を %d 回に分けて積みました\n", length, steps);
        }
    }

    /* -------------------------------------- (6) 下段の境界を掴んで動かす */
    {
        /*
         * **描いた印の場所で掴めること**（`grip_at()` が見る矩形と `draw_game_ui()` が
         * 印を描く矩形は同じもの）と、**動かした結果がレイアウトへ効くこと**を見る。
         * 掴めるように見えて掴めない所ができるのは、必守制約 3（描いた絵と当たり判定を
         * 食い違わせない）の親戚である。
         */
        SubSplit split{};
        UiLayout layout = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false, split);

        /*
         * **既定は下段 4・右列 3**（2026-08-23 に決めた「サブパネルは画像の状態を
         * デフォルトにして」）。**幅と高さそのものを見る**——枚数だけ見ていると、
         * 仕切りの既定を戻したときに素通りする。
         */
        int bottom_total = 0;
        for (int i = 0; i < kSubBottomMax; ++i) {
            const RectPx &r = layout.sub[static_cast<std::size_t>(kSubBottomSlot + i)];
            if (r.empty()) {
                fail("サブパネル: 既定なのに下段 " + std::to_string(i + 1) + " 枚目が出ていません");
            }
            bottom_total += r.w;
        }
        int right_total = 0;
        for (int i = 0; i < kSubRightMax; ++i) {
            const RectPx &r = layout.sub[static_cast<std::size_t>(kSubRightSlot + i)];
            if (r.empty()) {
                fail("サブパネル: 既定なのに右列 " + std::to_string(i + 1) + " 枚目が出ていません");
            }
            right_total += r.h;
        }
//! 下段は **4 / 4 / 5 / 7**（2026-08-25 に決めた）。
        {
            const double want[kSubBottomMax] = { 0.20, 0.20, 0.25, 0.35 };
            for (int i = 0; (i < kSubBottomMax) && (bottom_total > 0); ++i) {
                const double share
                    = static_cast<double>(layout.sub[static_cast<std::size_t>(kSubBottomSlot + i)].w)
                    / static_cast<double>(bottom_total);
                if (std::abs(share - want[i]) > 0.01) {
                    fail("下段の既定が 4/4/5/7 ではありません（" + std::to_string(i + 1) + " 枚目 "
                        + std::to_string(share) + "）");
                }
            }
        }
        //! 右列は **6 / 6 / 8**（＝ 0.30 / 0.30 / 0.40。2026-08-25 に決めた）。
        {
            const double want[kSubRightMax] = { 0.30, 0.30, 0.40 };
            for (int i = 0; (i < kSubRightMax) && (right_total > 0); ++i) {
                const double share
                    = static_cast<double>(layout.sub[static_cast<std::size_t>(kSubRightSlot + i)].h)
                    / static_cast<double>(right_total);
                if (std::abs(share - want[i]) > 0.01) {
                    fail("右列の既定が 6/6/8 ではありません（" + std::to_string(i + 1) + " 枚目 "
                        + std::to_string(share) + "）");
                }
            }
        }
        /*
         * **中身の既定はコアごと**（同じ指示）。番号の意味がコアで違うので、
         * Sil-Q に変愚の番号を出すと**描画関数の無い番号**になって空の枠が並ぶ。
         * 画のとおりの並びであることを名指しで押さえる。
         */
        {
            const int want_silq[kUiSubPanels] = { 7, 9, 4, 5, 2, 1, 0 };
            const int want_heng[kUiSubPanels] = { 6, 8, 4, 1, 3, 0, 0 };
            //! 2026-09-01 に足した 2 つ（手元の設定を作った）。
            const int want_genso[kUiSubPanels] = { 6, 8, 1, 3, 3, 0, -1 };
            const int want_frox[kUiSubPanels] = { 6, 8, 1, 3, 1, 0, -1 };
            int got[kUiSubPanels] = {};
            default_sub_panel_kinds("silq", got);
            for (int i = 0; i < kUiSubPanels; ++i) {
                if (got[i] != want_silq[i]) {
                    fail("サブパネルの既定（Sil-Q）が画と違います: 枠 " + std::to_string(i)
                        + " が " + std::to_string(got[i]) + "（" + std::to_string(want_silq[i]) + " のはず）");
                    break;
                }
            }
            default_sub_panel_kinds("hengband", got);
            for (int i = 0; i < kUiSubPanels; ++i) {
                if (got[i] != want_heng[i]) {
                    fail("サブパネルの既定（変愚）が変わっています: 枠 " + std::to_string(i)
                        + " が " + std::to_string(got[i]));
                    break;
                }
            }
            /*
             * **同じ数が 2 か所にある**（`Hd2dSettings` の初期値と、この関数の変愚の枝）。
             * コアが申告する前のフレームは初期値を使うので消せない——食い違ったら言う。
             */
            const Hd2dSettings fresh;
            for (int i = 0; i < kUiSubPanels; ++i) {
                if (fresh.sub_panel_kind[i] != want_heng[i]) {
                    fail("Hd2dSettings の初期値が default_sub_panel_kinds(変愚) と違います: 枠 "
                        + std::to_string(i));
                    break;
                }
            }
            /*
             * **幻想蛮怒と Frox も名指しで押さえる**（2026-09-01）。
             * どれも「手元の設定」なので、勝手に動いたら気づけるようにする。
             * **短愚は変愚と同じ**（cfg を持っていない）。
             */
            const struct {
                const char *core;
                const int *want;
            } kWantKinds[] = { { "gensoband", want_genso }, { "frox", want_frox },
                { "tangband", want_heng } };
            for (const auto &row : kWantKinds) {
                default_sub_panel_kinds(row.core, got);
                for (int i = 0; i < kUiSubPanels; ++i) {
                    if (got[i] != row.want[i]) {
                        fail(std::string("サブパネルの既定（") + row.core + "）が変わっています: 枠 "
                            + std::to_string(i) + " が " + std::to_string(got[i]));
                        break;
                    }
                }
            }
            /*
             * **枚数の既定**（`default_sub_split`）。右列は Sil-Q だけ 3 枚である。
             */
            {
                const struct {
                    const char *core;
                    int bottom;
                    int right;
                    int right_h20_0;
                } kWantSplit[] = {
                    { "hengband", 3, 2, 8 }, //!< 右列の上を厚く（手元の設定）
                    { "tangband", 3, 2, 8 }, //!< 変愚に合わせる（同じ指示）
                    { "gensoband", 3, 2, 6 },
                    { "frox", 4, 2, 6 },
                    { "silq", 4, 3, 6 },
                };
                for (const auto &want : kWantSplit) {
                    SubSplit split;
                    default_sub_split(want.core, split);
                    if ((split.bottom_count != want.bottom) || (split.right_count != want.right)
                        || (split.right_h20[0] != want.right_h20_0)) {
                        fail(std::string("枚数の既定（") + want.core + "）が違います: "
                            + std::to_string(split.bottom_count) + ","
                            + std::to_string(split.right_count) + " / 仕切り "
                            + std::to_string(split.right_h20[0]));
                    }
                }
            }


            /*
             * **コアごとのファイルの名前**（SH-39。2026-08-26 に決めた
             * 「完全にコア毎の記述になるようにして」）。ここが崩れると、
             * 別のコアで遊んだときに前のコアの設定を書き換えてしまう。
             */
            if (settings_path_for_core("SilCore.exe") != "hd2d-SilCore.cfg") {
                fail("コアごとの cfg の名前が違います: " + settings_path_for_core("SilCore.exe"));
            }
            if (settings_path_for_core("bin/HengbandCore.exe") != "hd2d-HengbandCore.cfg") {
                fail("コアごとの cfg の名前が道を落としていません: "
                    + settings_path_for_core("bin/HengbandCore.exe"));
            }
            if (settings_path_for_core("") != std::string(default_settings_path())) {
                fail("コアが空のときは hd2d.cfg のはずです");
            }
            if (settings_path_for_core("SilCore.exe") == settings_path_for_core("FroxCore.exe")) {
                fail("別のコアなのに同じ cfg を指しています");
            }

            /*
             * **切り欠きのぶんのずらしが、全部の矩形に効くこと**（2026-08-26 に気づいた
             * 「Android だけど、右パネルの下二段が左にズレる」）。
             *
             * `offset_rects()` が矩形を手で並べていたので、枠が 5 枚から 7 枚へ増えたときに
             * **右列の下 2 枚と仕切りの 3〜5 本目が並びから漏れ**、そこだけずれていた。
             * **切り欠きのある機体でしか出ない**ので、手元では 1 度も見えない
             * ——だから検査で押さえる。
             */
            for (const LayoutMode probe : { LayoutMode::Split, LayoutMode::Hybrid, LayoutMode::Full,
                     LayoutMode::Tall }) {
                constexpr int kDx = 37;
                constexpr int kDy = 21;
                const UiLayout at0 = UiLayout::compute(probe, screen_w, screen_h, text.cell_w(),
                    text.cell_h(), true, split);
                const UiLayout moved = UiLayout::compute(probe, screen_w, screen_h, text.cell_w(),
                    text.cell_h(), true, split, kDx, kDy);
                const auto same_shift = [&](const RectPx &a, const RectPx &b, const char *what) {
                    if (a.empty() != b.empty()) {
                        fail(std::string("ずらしで矩形が消えました: ") + what);
                        return;
                    }
                    if (a.empty()) {
                        return;
                    }
                    if (((b.x - a.x) != kDx) || ((b.y - a.y) != kDy) || (b.w != a.w) || (b.h != a.h)) {
                        fail(std::string("切り欠きのずらしが効いていません: ") + what + "（"
                            + std::to_string(b.x - a.x) + "," + std::to_string(b.y - a.y) + "）");
                    }
                };
                same_shift(at0.scene, moved.scene, "地図");
                same_shift(at0.status_col, moved.status_col, "状態列");
                same_shift(at0.bottom_bar, moved.bottom_bar, "最下段の帯");
                same_shift(at0.minimap, moved.minimap, "ミニマップ");
                for (int i = 0; i < kUiSubPanels; ++i) {
                    same_shift(at0.sub[static_cast<std::size_t>(i)], moved.sub[static_cast<std::size_t>(i)],
                        ("サブパネル " + std::to_string(i)).c_str());
                }
                for (int i = 0; i < kSubGripCount; ++i) {
                    same_shift(at0.grip[static_cast<std::size_t>(i)], moved.grip[static_cast<std::size_t>(i)],
                        ("仕切り " + std::to_string(i)).c_str());
                }
            }
        }

        //! 下段ぜんぶの幅（仕切りを動かしても変わらないはずの数）。
        const int left_w = bottom_total;

        //! 出ている仕切りは**印の真ん中で掴める**こと（描いた絵と当たり判定は同じ矩形）。
        const auto check_grips = [&](const UiLayout &lay, const char *what) {
            for (int i = 0; i < kSubGripCount; ++i) {
                const RectPx &grip = lay.grip[static_cast<std::size_t>(i)];
                if (grip.empty()) {
                    continue;
                }
                if (grip_at(lay, grip.x + (grip.w / 2), grip.y + (grip.h / 2)) != i) {
                    fail(std::string(what) + ": 仕切り " + std::to_string(i) + " の印の真ん中で掴めません");
                }
            }
        };
        check_grips(layout, "既定");
        for (int i = 0; i < 2; ++i) {
            if (layout.grip[static_cast<std::size_t>(kSubGripBottom + i)].empty()) {
                fail("下段の仕切り " + std::to_string(i) + ": 掴む帯がありません");
            }
        }
        if (layout.grip[kSubGripRight].empty()) {
            fail("右列の仕切り: 掴む帯がありません");
        }
        //! パネルの真ん中は仕切りではない（ここで掴めると、地図やパネルが押せなくなる）。
        if (grip_at(layout, layout.sub[1].x + (layout.sub[1].w / 2), layout.sub[1].y + 4) >= 0) {
            fail("仕切り: パネルの真ん中で掴めてしまいます");
        }

        //! 左の仕切りを右へ動かす → 左が広く、中央が狭くなる。
        const int before0 = layout.sub[0].w;
        if (!drag_grip(layout, kSubGripBottom, layout.sub[0].x + ((left_w * 2) / 5), layout.sub[0].y + 4, split)) {
            fail("下段の仕切り 0: 動かしても幅が変わりません");
        }
        layout = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false, split);
        if (layout.sub[0].w <= before0) {
            fail("下段の仕切り 0: 右へ動かしたのに左が広がっていません");
        }
        {
            int moved_total = 0;
            for (int i = 0; i < kSubBottomMax; ++i) {
                moved_total += layout.sub[static_cast<std::size_t>(kSubBottomSlot + i)].w;
            }
            if (moved_total != left_w) {
                fail("下段の仕切り 0: 動かしたら下段の合計が変わりました（隙間か重なりができます）");
            }
        }

        //! **潰さない。**端まで押しても、どの枚も残る。
        for (int i = 0; i < 40; ++i) {
            (void)drag_grip(layout, kSubGripBottom, layout.sub[0].x + left_w, layout.sub[0].y + 4, split);
        }
        layout = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false, split);
        for (int i = 1; i < kSubBottomMax; ++i) {
            if (layout.sub[static_cast<std::size_t>(kSubBottomSlot + i)].empty()) {
                fail("下段の仕切り 0: 端まで押したらパネル " + std::to_string(i + 1) + " が潰れました");
            }
        }

        /*
         * **右列の仕切りも動く**（2026-08-19 に決めた「パネルの仕切り位置も今の形で」）。
         * 前は `kRightSplitYRatio` の決め打ちで、掴む帯すら無かった。
         */
        {
            SubSplit rs{};
            UiLayout r = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
                text.cell_w(), text.cell_h(), false, rs);
            //! 右列ぜんぶの高さ（既定が 3 枚になったので、2 枚ぶんでは足りない）。
            int col_h = 0;
            for (int i = 0; i < kSubRightMax; ++i) {
                col_h += r.sub[static_cast<std::size_t>(kSubRightSlot + i)].h;
            }
            const int before = r.sub[kSubRightSlot].h;
            if (!drag_grip(r, kSubGripRight, r.sub[kSubRightSlot].x + 4,
                    r.sub[kSubRightSlot].y + ((col_h * 3) / 4), rs)) {
                fail("右列の仕切り: 動かしても高さが変わりません");
            }
            r = UiLayout::compute(LayoutMode::Split, screen_w, screen_h, text.cell_w(), text.cell_h(), false, rs);
            if (r.sub[kSubRightSlot].h <= before) {
                fail("右列の仕切り: 下へ動かしたのに上が高くなっていません");
            }
            {
                int moved_h = 0;
                for (int i = 0; i < kSubRightMax; ++i) {
                    moved_h += r.sub[static_cast<std::size_t>(kSubRightSlot + i)].h;
                }
                if (moved_h != col_h) {
                    fail("右列の仕切り: 動かしたら右列の合計が変わりました");
                }
            }
            for (int i = 0; i < 40; ++i) {
                (void)drag_grip(r, kSubGripRight, r.sub[kSubRightSlot].x + 4,
                    r.sub[kSubRightSlot].y + (col_h * 2), rs);
            }
            r = UiLayout::compute(LayoutMode::Split, screen_w, screen_h, text.cell_w(), text.cell_h(), false, rs);
            for (int i = 1; i < kSubRightMax; ++i) {
                if (r.sub[static_cast<std::size_t>(kSubRightSlot + i)].empty()) {
                    fail("右列の仕切り: 端まで押したらパネル " + std::to_string(i + 1) + " が潰れました");
                }
            }
        }

        /*
         * **枚数を目いっぱいにしても割り付けが成立する**こと（下段 4・右 3 ＝ 7 枚）。
         * 合計が食い違うと 1px の隙間や重なりが残り、パネルの縁が二重に見える。
         */
        {
            SubSplit full{};
            full.bottom_count = kSubBottomMax;
            full.right_count = kSubRightMax;
            const UiLayout wide = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
                text.cell_w(), text.cell_h(), false, full);
            int sum_w = 0;
            for (int i = 0; i < kSubBottomMax; ++i) {
                const RectPx &r = wide.sub[static_cast<std::size_t>(kSubBottomSlot + i)];
                if (r.empty()) {
                    fail("下段 " + std::to_string(i + 1) + " 枚目が出ていません（下段 4 枚）");
                }
                sum_w += r.w;
            }
            int sum_h = 0;
            for (int i = 0; i < kSubRightMax; ++i) {
                const RectPx &r = wide.sub[static_cast<std::size_t>(kSubRightSlot + i)];
                if (r.empty()) {
                    fail("右列 " + std::to_string(i + 1) + " 枚目が出ていません（右列 3 枚）");
                }
                sum_h += r.h;
            }
            //! 右列の合計は**使える高さ**（地図 ＋ 下段）と同じであること。
            if (sum_h != (wide.scene.h + wide.sub[0].h)) {
                fail("右列 3 枚: 高さの合計が使える高さと合いません");
            }
            check_grips(wide, "下段 4・右 3");
            for (int i = 0; i < kSubGripCount; ++i) {
                if (wide.grip[static_cast<std::size_t>(i)].empty()) {
                    fail("下段 4・右 3: 仕切り " + std::to_string(i) + " の帯がありません");
                }
            }
            std::fprintf(stderr,
                "  (6) サブパネル: 既定 下段 4（4/4/5/7）＋右 3（6/6/8）・中身はコアごとに覚える / "
                "最大 下段 4＋右 3 で合計 %d px・%d px / 仕切りは %d 本とも掴める\n",
                sum_w, sum_h, kSubGripCount);
        }
    }

    /* -------------------------------- (6') メッセージの折り返し（2026-08-21 に気づいた） */
    {
        /*
         * 「メッセージがパネル全体に表示せず半分くらいで切れる」の直しを機械で押さえる。
         * 見るのは 3 つ:
         *   1. **1 字も落ちない**（折った行を繋ぐと元の文になる）
         *   2. **どの行も枠に収まる**（測って幅以下）
         *   3. **行数が足りないときに残るのは新しいほう**（古い行から落とす）
         *
         * 検査の検査は `HD2D_BREAK_MSGWRAP=cut`: 直す前の作り（1 行に切り捨てる）を
         * 使わせる。1 と 3 が落ちるはずで、落ちなければこの節は何も見ていない。
         */
        const char *const wrap_break_env = std::getenv("HD2D_BREAK_MSGWRAP");
        const bool wrap_break = (wrap_break_env != nullptr) && (std::string(wrap_break_env) == "cut");
        if (wrap_break) {
            std::fprintf(stderr, "[hd2d] HD2D_BREAK_MSGWRAP=cut: **わざと壊します**"
                                 "（--ui-check は FAIL になるのが正しい）\n");
        }

        std::vector<MessageEvent> messages;
        const char *const kSamples[] = {
            "みじかい。",
            "あなたは「イークの洞穴」の奥深くで、名も知れぬ何かの気配をはっきりと感じ取った。",
            "You enter a maze of down staircases and the air grows colder around you.",
        };
        for (const char *sample : kSamples) {
            MessageEvent m{};
            m.seq = static_cast<std::uint32_t>(messages.size());
            m.color = 1;
            m.text_utf8 = sample;
            messages.push_back(std::move(m));
        }
        //! 枠は**わざと狭く**（20 桁ぶん）。この幅なら長い 2 本は必ず折れる。
        const int wrap_w = text.cell_w() * 20;

        //! 直す前の作り（切り捨て）。壊し口から呼ぶためにここへ置く。
        const auto truncating_lines = [&](int rows) {
            std::vector<SubPanelLine> out;
            const auto count = std::min<std::size_t>(messages.size(), static_cast<std::size_t>(rows));
            for (std::size_t i = messages.size() - count; i < messages.size(); ++i) {
                std::string cut = messages[i].text_utf8;
                cut.resize(text.fit_bytes(cut, wrap_w)); //!< 入らないぶんは**捨てる**
                out.push_back(SubPanelLine{ std::move(cut), messages[i].color });
            }
            return out;
        };
        const auto lines_for = [&](int rows) {
            return wrap_break ? truncating_lines(rows) : recent_message_lines(text, messages, wrap_w, rows);
        };

        // (1) 1 字も落ちない（行数はたっぷり与える）。
        {
            const std::vector<SubPanelLine> lines = lines_for(64);
            std::string joined;
            for (const auto &line : lines) {
                joined += line.text_utf8;
            }
            std::string whole;
            for (const auto &m : messages) {
                whole += m.text_utf8;
            }
            if (joined != whole) {
                fail("メッセージの折り返し: **字が落ちています**（折った行を繋いでも元の文になりません）");
            }
            // (2) どの行も枠に収まる。
            for (const auto &line : lines) {
                if (text.measure(line.text_utf8) > wrap_w) {
                    fail("メッセージの折り返し: 行が枠からはみ出しています（" + line.text_utf8 + "）");
                }
            }
        }

        // (3) 行数が足りないときに残るのは**新しいほう**。
        {
            const std::vector<SubPanelLine> few = lines_for(2);
            if (few.size() != 2) {
                fail("メッセージの折り返し: 行数の上限が効いていません（"
                    + std::to_string(few.size()) + " 行）");
            } else {
                const std::vector<SubPanelLine> all = lines_for(64);
                if (all.size() < 2 || few[0].text_utf8 != all[all.size() - 2].text_utf8
                    || few[1].text_utf8 != all.back().text_utf8) {
                    fail("メッセージの折り返し: **落としたのが新しいほう**です（古い行から落とすこと）");
                }
            }
        }
        std::fprintf(stderr, "  (6') メッセージ: %d 画素の枠へ折り返して 1 字も落とさない / "
                             "行が足りなければ古いほうから落とす\n",
            wrap_w);
    }

    /* ------------------------------------------ (7) パッドの割り当てと背面の絵 */
    {
        /*
         * パッド本体（開く・押す・倒す）は実物が要るので、ここで見るのは
         * **実物が無くても確かめられるところ**である: 綴りの往復・割り当ての往復・
         * 割り当てからコアへの出来事への変換。
         */
        for (int i = 0; i < kPadInputCount; ++i) {
            const auto input = static_cast<PadInput>(i);
            PadInput parsed = PadInput::Count;
            if (!parse_pad_input(pad_input_name(input), parsed) || (parsed != input)) {
                fail(std::string("パッド: 綴り「") + pad_input_name(input) + "」が往復しません");
            }
        }
        /*
         * 一人称（`ui/fps_mode.h`）。実物のマウスは要らない所だけ見る。
         *
         * **いちばん大事なのは「戻ったら北を向く」**（2026-08-11 に気づいた
         * 「FPS モードから HD2D に戻る際、FPS モードの向きに合わせて HD2D 描画の向きが
         * 変わってしまう」）。`Camera` は使い回しの値なので、`apply()` が非活性のときに
         * 素通りすると回した方位が残る。
         */
        {
            Camera fps_camera;
            FpsMode fps;
            fps.set_active(true);
            fps.turn_analog(1.f, 1.f); //!< 右スティックで半端な角まで回す
            fps.apply(fps_camera);
            if (!fps_camera.first_person || (std::fabs(fps_camera.yaw) < 0.01f)) {
                fail("一人称: 方位が動いていません（この後の検査が意味を持ちません）");
            }
            fps.set_active(false);
            fps.apply(fps_camera);
            if (fps_camera.first_person || (fps_camera.yaw != 0.f)) {
                fail("一人称: 抜けたのに見下ろしのカメラが北を向いていません");
            }
            /*
             * マウスの視点移動（2026-08-11 に決めた・**改訂**）。
             * 「マウススライド単体で視界移動」＝ **ボタンは要らない**。
             * 「吸着は無しに」＝ **45° の刻みに乗らない角のまま止まる**。
             */
            fps.set_active(true);
            fps.look(200, 0);
            const float step = kFpsYawStepDeg * 0.0174533f;
            if (std::fabs(fps.facing) < 0.01f) {
                fail("一人称: ボタンを押さないマウス移動で視界が回りません");
            }
            if (std::fabs(std::round(fps.facing / step) - (fps.facing / step)) < 0.001f) {
                fail("一人称: 吸着を止めたはずが 8 方位へ乗っています（値を選び直してください）");
            }
            /*
             * 上下（2026-08-11 に決めた「仰角、下角も見れるように」）。
             * **切のときは動かない**こと、限界で丸まること、水平へ戻せることを見る。
             */
            fps.vertical_look = false;
            fps.look(0, 200);
            if (fps.pitch_deg != kFpsPitchDeg) {
                fail("一人称: 上下視点を切っているのに伏角が動きました");
            }
            fps.vertical_look = true;
            fps.look(0, 20);
            if (fps.pitch_deg <= kFpsPitchDeg) {
                fail("一人称: マウスを下へ動かしても下を向きません");
            }
            fps.look(0, 100000);
            if (fps.pitch_deg != kFpsPitchMaxDownDeg) {
                fail("一人称: 俯角が 60 度で止まりません");
            }
            fps.look(0, -100000);
            if (fps.pitch_deg != -kFpsPitchMaxUpDeg) {
                fail("一人称: 仰角が 60 度で止まりません");
            }
            //! 「視界を水平に戻す」（中クリック・R3）。**方位は触らない。**
            const float facing_before = fps.facing;
            fps.level_view();
            if (fps.pitch_deg != kFpsPitchDeg) {
                fail("一人称: 視界を水平に戻せません");
            }
            if (fps.facing != facing_before) {
                fail("一人称: 視界を水平に戻したら方位まで変わりました");
            }
            //! 切に変えた瞬間に中立へ戻ること（戻す手がマウスに無くなるため）。
            fps.look(0, 50);
            fps.vertical_look = false;
            fps.advance(0.016f);
            if (fps.pitch_deg != kFpsPitchDeg) {
                fail("一人称: 上下視点を切っても伏角が中立へ戻りません");
            }
            //! ホイールの画角は 90〜120 度で丸まること。
            fps.adjust_fov(-20);
            if (fps.fov_deg != kFpsFovMinDeg) {
                fail("一人称: 画角の下限が 90 度になっていません");
            }
            fps.adjust_fov(20);
            if (fps.fov_deg != kFpsFovMaxDeg) {
                fail("一人称: 画角の上限が 120 度になっていません");
            }
        }
        /*
         * 既定のパッド割り当て（2026-08-11 に決めた）。**`id` ではなく
         * Angband のコマンド 1 文字で引く**こと（`id` は表の並び順で動く）。
         */
        {
            std::vector<PadCommand> table;
            table.push_back(PadCommand{ 7, 'g', "移動", "拾う", { 'g' } });
            table.push_back(PadCommand{ 8, 'f', "戦闘", "投射物を撃つ", { 'f' } });
            table.push_back(PadCommand{ 9, '>', "移動", "階段を降りる", { '>' } });
            table.push_back(PadCommand{ 10, '<', "移動", "階段を上る", { '<' } });
            table.push_back(PadCommand{ 11, 'T', "行動", "掘る", { 'T' } });
            /*
             * **`s`（探す）と `S`（探索モードの ON/OFF）は別のコマンドである**
             * （2026-08-11。`ui/game_pad.h` の注記）。表に**両方**入れておくのは、
             * 既定が間違って `S` を掴んだときに**ここで捕まえる**ためである
             * ——`S` が表に無いと `apply_default_pad_binds` は「見つからないので飛ばす」を
             * 選び、割り当てが 1 つ空くだけで検査は通ってしまう。
             */
            table.push_back(PadCommand{ 12, 's', "行動", "探す", { 's' } });
            table.push_back(PadCommand{ 13, 'S', "行動", "探索モードの ON/OFF", { 'S' } });
            //! Sil-Q の 3 つ（`/` 選択して行動・`z` その場で待つ・`S` 隠密の構え）も表へ。
            table.push_back(PadCommand{ 14, '/', "行動", "選択して行動", { '/' } });
            table.push_back(PadCommand{ 15, 'z', "移動", "その場で待つ", { 'z' } });
            table.push_back(PadCommand{ 16, 'm', "魔法", "魔法を使う", { 'm' } });
            PadBinds defaults;
            if (!apply_default_pad_binds(defaults, table, "hengband")) {
                fail("パッド: 既定の割り当てが 1 つも入りません");
            }
            /*
             * **既定のパッド割り当ての表**（2026-09-01）。`X` と `Y` は 2026-08-11 の版と入れ替えてある
             * ——同じ画面の同じボタンの意味が変わるので、**変えたら必ずここも変える**。
             */
            const struct {
                PadInput input;
                int action;
                const char *note;
            } kWant[] = {
                { PadInput::X, 7, "X = 拾う" },
                { PadInput::Y, 8, "Y = 投射物を撃つ" },
                { PadInput::RightShoulder, 9, "R1 = 階段を降りる" },
                { PadInput::LeftShoulder, 10, "L1 = 階段を上る" },
                { PadInput::RightTrigger, 11, "R2 = 掘る" },
                { PadInput::LeftTrigger, 12, "L2 = 探す" },
                { PadInput::LeftStick, kActionFpsToggle, "L3 = 一人称の入切" },
            };
            for (const auto &want : kWant) {
                if (defaults.command[0][static_cast<int>(want.input)] != want.action) {
                    fail(std::string("パッド: 既定の割り当てが違います（") + want.note + "）");
                }
            }
            /*
             * **コアごとに違う所を名指しで押さえる**（2026-09-01）。
             * 幻想蛮怒は `X` が魔法、Sil-Q は `X`・`LT`・`RT` の 3 つが別である。
             */
            {
                /*
                 * **変愚・短愚・幻想・Frox は同じ並びである**（2026-09-01 に 3 コアから
                 * 採った字が 1 つも違わなかった）。同じ表を返すことを押さえる。
                 */
                for (const char *core : { "tangband", "gensoband", "frox" }) {
                    PadBinds same;
                    (void)apply_default_pad_binds(same, table, core);
                    for (const auto &want : kWant) {
                        if (same.command[0][static_cast<int>(want.input)] != want.action) {
                            fail(std::string("パッド: ") + core + " が変愚と違います（"
                                + want.note + "）");
                        }
                    }
                }
                PadBinds silq;
                (void)apply_default_pad_binds(silq, table, "silq");
                const struct {
                    PadInput input;
                    int action;
                    const char *note;
                } kWantSilq[] = {
                    { PadInput::X, 14, "Sil-Q の X = 選択して行動" },
                    { PadInput::Y, 7, "Sil-Q の Y = 拾う" },
                    { PadInput::RightStick, 13, "Sil-Q の R3 = 隠密の構え（S）" },
                    { PadInput::RightTrigger, 15, "Sil-Q の R2 = その場で待つ" },
                };
                //! **Sil-Q の L2 は空**（そう組んである。2026-09-01）。
                if (silq.command[0][static_cast<int>(PadInput::LeftTrigger)] != 0) {
                    fail("パッド: Sil-Q の L2 に既定が入っています（空のはず）");
                }
                for (const auto &want : kWantSilq) {
                    if (silq.command[0][static_cast<int>(want.input)] != want.action) {
                        fail(std::string("パッド: 既定の割り当てが違います（") + want.note + "）");
                    }
                }
            }
            /*
             * **L3 は一人称の入切**（2026-08-11 に決めた「キーマップのデフォルトで
             * LS は一人称切り替えに」）。2026-08-11 の前半までは「空のまま」で、
             * ここもそれを見張っていた——**指示が変われば見張りも変える。**
             * 上の `kWant` が中身を見ているので、ここでは「空でないこと」だけを言う。
             */
            if (defaults.command[0][static_cast<int>(PadInput::LeftStick)] == 0) {
                fail("パッド: L3 に既定の割り当てが入っていません（一人称の入切）");
            }
            /*
             * **R3 は変愚系では空**（2026-09-01 に決めた。手元の cfg に `RS` が無い）。
             * 2026-08-11 の版は「視界を水平に戻す」を入れていた——**指示が変われば見張りも変える。**
             * Sil-Q だけは `S`（隠密の構え）が入る（下の `kWantSilq`）。
             */
            if (defaults.command[0][static_cast<int>(PadInput::RightStick)] != 0) {
                fail("パッド: R3 に既定が入っています（変愚系では空のはず）");
            }
        }
        //! **固定の 3 つ**（A / B / Back）は単独押しでは割り当ての対象にしない。
        if (!pad_input_is_fixed(PadInput::A) || !pad_input_is_fixed(PadInput::B)
            || !pad_input_is_fixed(PadInput::Back) || pad_input_is_fixed(PadInput::X)) {
            fail("パッド: 固定の入力（A / B / Back）の判定が違います");
        }
        /*
         * **固定なのは単独押しのときだけ**（2026-08-14 に気づいた「同時押しなら AB は
         * 割り当ててよいのでは？」）。層の中では空いている枠なので受け付ける。
         * 逆に修飾そのものは層の中では受け付けない（`LB＋LB` は押しようがない）。
         */
        {
            PadBinds layered;
            if (layered.assign(12, PadInput::A, 0)) {
                fail("パッド: 単独の A に割り当てられてしまいます（決定が消える）");
            }
            if (!layered.assign(12, PadInput::A, kPadModCtrl)) {
                fail("パッド: LB＋A に割り当てられません（層の中では空いている枠）");
            }
            if (layered.assign(13, PadInput::LeftShoulder, kPadModCtrl)) {
                fail("パッド: LB＋LB に割り当てられてしまいます（押しようがない枠）");
            }
            //! 綴りの往復も見る（層と固定ボタンの組み合わせは cfg にも出る）。
            PadBinds back;
            back.parse(layered.to_line());
            if (back.to_line() != layered.to_line()) {
                fail("パッド: LB＋A が cfg の綴りで往復しません（" + layered.to_line() + "）");
            }
        }

        PadBinds binds;
        binds.command[0][static_cast<int>(PadInput::X)] = 12;
        binds.command[0][static_cast<int>(PadInput::LeftTrigger)] = 34;
        PadBinds reloaded;
        reloaded.parse(binds.to_line());
        if (reloaded.to_line() != binds.to_line()) {
            fail("パッド: 割り当てが cfg の綴りで往復しません（" + binds.to_line() + " → " + reloaded.to_line() + "）");
        }

        /*
         * バーチャルパッド（2026-08-12 に決めた）。設定が cfg の綴りで往復すること・
         * 既定へ戻せること・ボタンと `PadInput` の対応を見る（触り心地はタッチが要るので実機）。
         */
        {
            VirtualPadSettings vps;
            vps.visible[static_cast<int>(VpadControl::X)] = false;
            vps.slot[kVpadLandscape][static_cast<int>(VpadControl::A)] = VpadSlot{ 0.5f, 0.25f, 1.5f };
            //! 位置は既定のまま倍率だけ
            vps.slot[kVpadLandscape][static_cast<int>(VpadControl::LS)].scale = 0.8f;
            //! **縦は別の値**（2026-08-12 に決めた。同じ値だと片方しか書いていなくても通る）。
            vps.slot[kVpadPortrait][static_cast<int>(VpadControl::A)] = VpadSlot{ 0.2f, 0.9f, 0.6f };
            VirtualPadSettings vps_reload;
            vps_reload.parse_visible(vps.visible_line());
            for (int o = 0; o < kVpadOrientCount; ++o) {
                vps_reload.parse_layout(o, vps.layout_line(o));
            }
            if (vps_reload.visible[static_cast<int>(VpadControl::X)]
                || !vps_reload.visible[static_cast<int>(VpadControl::Y)]) {
                fail("バーチャルパッド: ボタン表示が cfg の綴りで往復しません（" + vps.visible_line() + "）");
            }
            for (int o = 0; o < kVpadOrientCount; ++o) {
                if (vps_reload.layout_line(o) != vps.layout_line(o)) {
                    fail(std::string("バーチャルパッド: 配置が cfg の綴りで往復しません（")
                        + vpad_orient_name(o) + " " + vps.layout_line(o) + " → " + vps_reload.layout_line(o) + "）");
                }
            }
            /*
             * **縦横が混ざっていないこと。**同じ綴りの行を 2 本書いているので、
             * 片方を読んで両方へ入れる実装でも往復の検査だけは通ってしまう。
             */
            if (vps.layout_line(kVpadLandscape) == vps.layout_line(kVpadPortrait)) {
                fail("バーチャルパッド: 縦と横の配置が同じ綴りになっています（別に持てていません）");
            }
            //! 片側だけ戻す（編集画面の「デフォルトに戻す」）。**もう片方は残ること。**
            vps.reset_layout(kVpadPortrait);
            if (!vps.layout_line(kVpadPortrait).empty() || vps.layout_line(kVpadLandscape).empty()) {
                fail("バーチャルパッド: 片側だけの「デフォルトに戻す」が効いていません");
            }
            vps.reset_all();
            if (!vps.visible[static_cast<int>(VpadControl::X)] || !vps.layout_line(kVpadLandscape).empty()
                || !vps.layout_line(kVpadPortrait).empty()) {
                fail("バーチャルパッド: デフォルトに戻りません");
            }
            if ((vpad_control_input(VpadControl::Select) != PadInput::Back)
                || (vpad_control_input(VpadControl::L2) != PadInput::LeftTrigger)
                || (vpad_control_input(VpadControl::R3) != PadInput::RightStick)
                || (vpad_control_input(VpadControl::LS) != PadInput::Count)) {
                fail("バーチャルパッド: ボタンと PadInput の対応が違います（SELECT=Back / L2=LT / R3=RS / LS=軸）");
            }
        }

        std::vector<PadCommand> commands;
        commands.push_back(PadCommand{ 12, '>', "移動", "階段を降りる", { '>' } });
        //! **キー列が空 = 解決不能**（いまのキー配列にそのコマンドが無い）。
        commands.push_back(PadCommand{ 34, 'm', "魔法", "唱える", {} });

        presentation::InputEventsMessage out;
        if (!emit_core_command(binds.command[0][static_cast<int>(PadInput::X)], commands, out)) {
            fail("パッド: 割り当てたコマンドが出ません");
        }
        if ((out.events.size() != 1) || (out.events[0].e != "key") || (out.events[0].chr != ">")) {
            fail("パッド: 割り当てたコマンドのキー列が違います");
        }
        out.events.clear();
        if (emit_core_command(binds.command[0][static_cast<int>(PadInput::LeftTrigger)], commands, out)
            || !out.events.empty()) {
            fail("パッド: 解決できないコマンドを出してしまいました");
        }
        /*
         * **制御文字のコマンドが「届く形」で出ること**（2026-08-11。罠 137 の 2 例目）。
         * ローグライク配列の「掘る」は `^t`（0x14）。`chr=0x14` のまま送ると復号側の検証
         * （印字文字のみ）が**黙って捨てて、ボタンを押しても何も起きない**。
         * Ctrl フラグ付きの `t` へ翻訳し、**符号化 → 復号の往復**まで通す
         * （アダプタが `ctrl && 英字` を `c & 0x1F = 0x14` へ戻す）。
         */
        {
            std::vector<PadCommand> ctrl_commands;
            ctrl_commands.push_back(PadCommand{ 55, 'T', "行動", "掘る", { 0x14, 0x0D } });
            presentation::InputEventsMessage ctrl_out;
            if (!emit_core_command(55, ctrl_commands, ctrl_out) || (ctrl_out.events.size() != 2)
                || (ctrl_out.events[0].e != "key") || (ctrl_out.events[0].chr != "t")
                || !ctrl_out.events[0].ctrl || (ctrl_out.events[1].e != "confirm")) {
                fail("パッド: 制御文字のコマンド（^t）が Ctrl+英字へ翻訳されていません");
            }
            const std::string wire_text = presentation::encode_input_events(ctrl_out);
            presentation::InputEventsMessage decoded;
            std::string codec_err;
            if (!presentation::decode_input_events(wire_text, decoded, codec_err)
                || (decoded.events.size() != 2) || (decoded.events[0].chr != "t") || !decoded.events[0].ctrl) {
                fail("パッド: 制御文字のコマンドが復号を通りません（押しても何も起きない形です）");
            }
        }
        if (emit_core_command(binds.command[0][static_cast<int>(PadInput::Y)], commands, out) || !out.events.empty()) {
            fail("パッド: 割り当てていないボタンで何かを出しました");
        }
        /*
         * 「操作 → 入力」の向きで引けること（2026-08-11 に決めた で画面がこの向きになった）。
         * **同じ操作が 2 つのボタンに乗らない**ことも確かめる（乗ると画面に出せない）。
         */
        if (binds.input_for(12).input != PadInput::X) {
            fail("パッド: 操作から入力を引けません");
        }
        if (!binds.assign(12, PadInput::Y) || (binds.input_for(12).input != PadInput::Y)
            || (binds.command[0][static_cast<int>(PadInput::X)] != 0)) {
            fail("パッド: 割り当て直したのに元のボタンから外れていません");
        }
        if (binds.assign(12, PadInput::A)) {
            fail("パッド: 固定の入力（A）へ割り当てられてしまいました");
        }
        /*
         * 同時押しの層（2026-08-14 に決めた）。**層をまたいでも先客からは外れる**
         * ——外れないと、同じ操作が 2 か所に乗って画面に出せなくなる。
         */
        if (!binds.assign(12, PadInput::X, kPadModCtrl)) {
            fail("パッド: 同時押しの枠へ割り当てられません");
        }
        if ((binds.input_for(12).input != PadInput::X) || (binds.input_for(12).mods != kPadModCtrl)
            || (binds.command[0][static_cast<int>(PadInput::Y)] != 0)) {
            fail("パッド: 同時押しへ移したのに単独の枠から外れていません");
        }
        if (binds.assign(12, PadInput::LeftShoulder, kPadModCtrl)) {
            fail("パッド: 押しようのない枠（LB＋LB）へ割り当てられてしまいました");
        }
        binds.clear(12);
        if (binds.input_for(12).input != PadInput::Count) {
            fail("パッド: 割り当てを消せません");
        }

        /*
         * 背面の絵の切り出し。**帯・列を 1 枚と見なして左上起点で切る**という規則が
         * 守られているか。パネルごとに絵を割り当てる作りに戻ると、境界を動かすたびに柄がずれる。
         */
        const UiLayout layout = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false);
        const int huge = 8192;
        int sx = 0;
        int sy = 0;
        int sw = 0;
        int sh = 0;
        if (!panel_backdrop_source(layout, 0, huge, huge, sx, sy, sw, sh) || (sx != 0) || (sy != 0)) {
            fail("背面の絵: 下段の 1 枚目は帯の原点（0,0）から切るはずです");
        }
        if (!panel_backdrop_source(layout, 1, huge, huge, sx, sy, sw, sh) || (sx != layout.sub[1].x) || (sy != 0)) {
            fail("背面の絵: 下段の 2 枚目が 1 枚目の続きになっていません");
        }
        if (!panel_backdrop_source(layout, kSubRightSlot, huge, huge, sx, sy, sw, sh) || (sx != 0)
            || (sy != layout.sub[kSubRightSlot].y)) {
            fail("背面の絵: 右列の 1 枚目は列の原点（0,0）から切るはずです");
        }
        if (!panel_backdrop_source(layout, kSubRightSlot + 1, huge, huge, sx, sy, sw, sh) || (sx != 0)
            || (sy != layout.sub[kSubRightSlot + 1].y)) {
            fail("背面の絵: 右列の 2 枚目が 1 枚目の続きになっていません");
        }
        //! **マスタが足りなければ諦める**（黙って伸ばさない）。
        if (panel_backdrop_source(layout, 0, 8, 8, sx, sy, sw, sh)) {
            fail("背面の絵: マスタが小さすぎるのに切り出せると答えました");
        }
        std::fprintf(stderr, "  (7) パッド: 綴りと割り当ての往復・コマンドの変換 / 背面の絵: 切り出しの規則\n");
    }

    /*
     * ---- (8) カットイン演出（P10。2026-08-11 に決めた。`ui/floor_cutin.h`）----
     *
     * 見るのは**段の進み方と、出す／出さないの判断**である。絵の出来は目で見るしかないが、
     * 「全体マップでは出さない」「決定キーまで待つ」「その間コアへ流さない」は機械で言える。
     *
     * 検査の検査は `HD2D_BREAK_CUTIN`:
     * | 値 | 何を壊すか |
     * |---|---|
     * | `wild` | 全体マップでも出す |
     * | `nowait` | 決定キーを待たずに勝手に進む |
     * | `stale` | 階段の途中の 1 枚（階だけ新しく地図は元のまま）を「着いた」ことにする |
     */
    {
        const char *const break_env = std::getenv("HD2D_BREAK_CUTIN");
        const std::string break_cutin = (break_env != nullptr) ? break_env : std::string();
        if (!break_cutin.empty()) {
            std::fprintf(stderr, "[hd2d] HD2D_BREAK_CUTIN=%s: **わざと壊します**"
                                 "（--ui-check は FAIL になるのが正しい）\n",
                break_cutin.c_str());
        }
        const UiLayout layout = UiLayout::compute(LayoutMode::Split, screen_w, screen_h,
            text.cell_w(), text.cell_h(), false);

        /*
         * 検査用のフレーム。**地図が出ている**ことにする（`frame_shows_map` が真になる形）。
         *
         * **地図も階ごとに違うものにする**（2026-08-28）。「行先へ着いたか」は
         * `generated_turn` か**地図そのもの**のどちらかが変わったことで見るので、
         * 全部の階が同じ地図だと後者の経路を 1 度も通らない。
         * `make_synthetic_frame` の地図は常に同じ形なので、ここで階の番号を
         * 壁と床に折り込んで違えておく。
         */
        const auto make_floor_frame = [](int kind, int dungeon_id, int level, bool wild) {
            GameFrame f = make_synthetic_frame(21, 15, 100, 44);
            f.floor.kind = kind;
            f.floor.dungeon_id = dungeon_id;
            f.floor.dun_level = level;
            f.floor.generated_turn = static_cast<std::uint64_t>(1000 + level);
            f.floor.wild_mode = wild;
            f.floor.place_name_utf8 = wild ? "地上" : (dungeon_id > 0 ? "イークの洞窟" : "辺境の地");
            for (std::size_t i = 0; i < f.minimap.kinds.size(); ++i) {
                if ((i % 97u) == static_cast<std::size_t>((level * 7) + dungeon_id) % 97u) {
                    f.minimap.kinds[i] = static_cast<std::uint8_t>(MinimapKind::Wall);
                }
            }
            return f;
        };

        FloorCutin cutin;
        std::string cutin_err;
        if (!cutin.init(cutin_err)) {
            fail("カットイン: 用意できませんでした: " + cutin_err);
        } else {
            /*
             * (0) **ゲームが始まる前は何があっても出さない**（2026-08-11 に気づいた:
             * 「新規で開始する際、名前選択の手前で決定ができない」）。
             *
             * キャラクター作成の能力値の画面と名前の入力は**行 0 のプロンプト**なので
             * Term ミラーに出ず、`frame_shows_map()` は「地図が出ている」と答える。
             * そこへ中身の無いフロアの素性が変わりながら流れてくるので、
             * `pre_game_menu` を見ていなかった版は**演出が立ち上がって決定キーを食っていた**。
             */
            for (int level = 0; level < 4; ++level) {
                GameFrame f0 = make_floor_frame(static_cast<int>(FloorKind::Dungeon), level, level, false);
                f0.pre_game_menu = true; //!< 作成中・死んだ後
                cutin.update(f0, 0.016f, layout, true);
                if (cutin.active()) {
                    //! **幕（`Pending`）も含めて**出てはいけない。
                    fail("カットイン: **ゲームが始まる前に出ました**（キャラクター作成中）");
                    break;
                }
            }

            //! (a) 最初の 1 フレームでは出さない（起動直後に必ず演出が入るのは違う）。
            GameFrame f1 = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 1, false);
            cutin.update(f1, 0.016f, layout, true);
            if (cutin.active()) {
                fail("カットイン: 最初のフレームで始まりました（前の階を知らないのに）");
            }

            /*
             * (a') **階だけ新しく・地図は元の階のままの 1 枚**（`-more-` の待ち。
             * 出どころと実測は `ui/floor_cutin.cpp` の表）で見るのは 2 つある。
             *
             * 1. **演出は始めない**（2026-08-21 に気づいた:「階段を上り下りする際に
             *    カットインのあと行先でない画面が表示される。元の階層？」）。始めると
             *    幕が上がったときに元の階が出る
             * 2. **幕は張る**（2026-08-28 に決めた:「元のフロアの画像が表示される
             *    のをやめて」）。この 1 枚は `-more-` の待ちなので**キーを押すまで
             *    画面に残る**——張らないと元の階の絵を見せたまま止まる
             *
             * この 2 つは同時に成り立つ（`Pending`）。**入力は止めない**——
             * `-more-` を越えるキーが要る。
             *
             * **続く本物の到着フレームで必ず始まること**も同じ節で見る。ここを見ないと、
             * 「途中の 1 枚を覚えてしまい以後一度も出ない」直し方でも通ってしまう。
             */
            GameFrame f1_midway = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 3, false);
            f1_midway.floor.generated_turn = f1.floor.generated_turn; //!< 地図はまだ元の階のもの
            f1_midway.minimap = f1.minimap; //!< **地図そのものも**元の階のまま（到着の印は 2 つある）
            if (break_cutin == "stale") {
                f1_midway.floor.generated_turn = 4242u; //!< **わざと壊す**: 地図も新しいことにする
            }
            cutin.update(f1_midway, 0.016f, layout, true);
            if (cutin.blocks_input()) {
                fail("カットイン: **地図が元の階のままの 1 枚で始まりました**（階段の途中）");
            }
            if ((break_cutin != "stale") && !cutin.pending()) {
                fail("カットイン: **階段の途中で幕が張られません**（元の階の絵が見えたまま）");
            }
            if ((break_cutin != "stale") && cutin.confirm()) {
                fail("カットイン: 幕だけの段で決定を食いました（-more- が越えられなくなる）");
            }
            GameFrame f1_arrived = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 3, false);
            cutin.update(f1_arrived, 0.016f, layout, true);
            if (!cutin.blocks_input()) {
                fail("カットイン: 着いたのに始まりません（途中の 1 枚を覚えてしまった）");
            }
            //! 次の節のために畳んでおく（決定 → 抜け → 幕が薄れて → 終わる）。
            (void)cutin.confirm();
            for (int i = 0; i < 240; ++i) {
                cutin.update(f1_arrived, 0.016f, layout, true);
            }

            /*
             * (a'') **`generated_turn` を送らないコアでも同じに動く**（2026-08-28）。
             *
             * 4 コアのうち**変愚蛮怒だけ**が `generated_turn` を送る。幻想蛮怒・Sil-Q・
             * FroxComposband は**階に「作られた turn」を持つ欄が無い**ので 0 のまま送る
             * （各 `fill_floor_and_lighting()` の註記）。到着を `generated_turn` だけで
             * 見ていた版は、**その 3 本でカットインが 1 度も出なかった**——
             * `last_floor_` を更新する条件も同じ式なので、一度階を移ると
             * 以後ずっと「まだ着いていない」に貼り付く。
             *
             * ここでは 0 のまま 2 つの階を渡して、① 途中の 1 枚では始めず幕だけ張る
             * ② 地図が入れ替わったら始まる、の両方を見る。
             */
            {
                FloorCutin zero;
                std::string zero_err;
                if (!zero.init(zero_err)) {
                    fail("カットイン: 用意できませんでした（turn 0 の検査）: " + zero_err);
                } else {
                    GameFrame z1 = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 3, 1, false);
                    z1.floor.generated_turn = 0u;
                    zero.update(z1, 0.016f, layout, true); //!< 最初の 1 枚（覚えるだけ）
                    if (zero.active()) {
                        fail("カットイン: turn 0 のコアで最初のフレームから出ました");
                    }
                    //! 階だけ新しく、地図は 1 階のまま（`-more-` の待ち）。
                    GameFrame z_mid = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 3, 2, false);
                    z_mid.floor.generated_turn = 0u;
                    z_mid.minimap = z1.minimap;
                    zero.update(z_mid, 0.016f, layout, true);
                    if (zero.blocks_input()) {
                        fail("カットイン: turn 0 のコアで**地図が元の階のまま**なのに始まりました");
                    }
                    if (!zero.pending()) {
                        fail("カットイン: turn 0 のコアで**階段の途中の幕が張られません**");
                    }
                    //! 着いた（地図が入れ替わった）。**ここで始まらないと 1 度も出ない。**
                    GameFrame z2 = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 3, 2, false);
                    z2.floor.generated_turn = 0u;
                    zero.update(z2, 0.016f, layout, true);
                    if (!zero.blocks_input()) {
                        fail("カットイン: **`generated_turn` を送らないコアで 1 度も出ません**"
                             "（幻想蛮怒・Sil-Q・Frox）");
                    }
                    zero.shutdown();
                }
            }

            //! (b) 階が変わったら始まり、**コアへ流させない**。
            GameFrame f2 = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 2, false);
            cutin.update(f2, 0.016f, layout, true);
            if (!cutin.active() || !cutin.blocks_input()) {
                fail("カットイン: 階が変わったのに始まりません");
            }

            //! (c) **決定キーまで待つ。**時間では抜けない。
            for (int i = 0; i < 240; ++i) {
                cutin.update(f2, 0.016f, layout, true);
                if (break_cutin == "nowait") {
                    (void)cutin.confirm(); //!< **わざと壊す**: 待たずに進める
                }
            }
            if (!cutin.waiting()) {
                fail("カットイン: 決定キーを待たずに進みました");
            }

            //! (d) 決定 → 抜け → 幕が薄れて → 終わる。
            if (!cutin.confirm()) {
                fail("カットイン: 決定を受け付けません");
            }
            for (int i = 0; i < 240; ++i) {
                cutin.update(f2, 0.016f, layout, true);
            }
            if (cutin.active() || cutin.blocks_input()) {
                fail("カットイン: 決定のあと終わりません");
            }

            /*
             * (e) **同じ層を作り直しただけでは出さない**（2026-08-11 に決めた:
             * 「建物から出る場合のカットインはいらない」「層を移動する際のみに」）。
             * 店・建物から出ると地上の階は作り直されて `generated_turn` が変わるが、
             * プレイヤは**同じ町の同じ場所**に戻ってきている。
             */
            GameFrame f2b = f2;
            f2b.floor.generated_turn = 99999u; //!< 作り直しただけ（層は同じ）
            cutin.update(f2b, 0.016f, layout, true);
            if (cutin.active()) {
                fail("カットイン: **作り直しただけの同じ層で出ました**（建物から出たとき）");
            }

            //! (f) **全体マップでは出さない**（決めたこと）。
            GameFrame f3 = make_floor_frame(static_cast<int>(FloorKind::Surface), 0, 0, true);
            if (break_cutin == "wild") {
                f3.floor.wild_mode = false; //!< **わざと壊す**: 広域マップの印を消す
            }
            cutin.update(f3, 0.016f, layout, true);
            if (cutin.active()) {
                fail("カットイン: **全体マップで出ました**");
            }

            /*
             * (g) 地下から地上へ出ると出る（地名だけ。階層は無い）。
             * **`kind` が変わる**ので「層が変わった」に当たる。
             */
            GameFrame f3b = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 3, false);
            cutin.update(f3b, 0.016f, layout, true);
            (void)cutin.confirm();
            for (int i = 0; i < 240; ++i) {
                cutin.update(f3b, 0.016f, layout, true);
            }
            GameFrame f4 = make_floor_frame(static_cast<int>(FloorKind::Surface), 0, 0, false);
            cutin.update(f4, 0.016f, layout, true);
            if (!cutin.active()) {
                fail("カットイン: 地上マップで出ません");
            }
            /*
             * (h) **出ている最中に窓の大きさが変わっても落ちない**（2026-08-11 に気づいた:
             * 「はじめの辺境のカットインの際にウインドウサイズを変更すると落ちる」）。
             *
             * 字の大きさは地図の縦幅から採るので、窓が変わると**フォントを焼き直す**。
             * ここは 3 通りの大きさを行き来しながら描くところまでやる
             * ——落ちるのは `draw()` の中なので、`update()` だけでは踏めない。
             */
            for (const auto &size : sizes) {
                const UiLayout resized = UiLayout::compute(LayoutMode::Split, size[0], size[1],
                    text.cell_w(), text.cell_h(), false);
                cutin.update(f4, 0.016f, resized, true);
                cutin.draw(resized, size[0], size[1], images);
            }
            if (!cutin.active()) {
                fail("カットイン: 窓の大きさを変えたら消えました");
            }

            //! (i) 地図が消えた（店・タイトル）ら畳む。幕を残したまま別の画面へ行かせない。
            cutin.update(f4, 0.016f, layout, false);
            if (cutin.active()) {
                fail("カットイン: 地図が消えても幕が残りました");
            }
            cutin.shutdown();
            std::fprintf(stderr, "  (8) カットイン: 層が変わったら出る / 着くまで始めない / "
                                 "**着くまで幕は張る**（元の階を見せない） / "
                                 "**turn を送らないコアでも出る** / 決定まで待つ / 作り直しただけでは出ない / 全体マップでは出ない / "
                                 "窓の大きさが変わっても落ちない / 地図が消えたら畳む\n");
        }
    }

    images.shutdown();
    paint.shutdown();
    text.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

} // namespace hd2d
