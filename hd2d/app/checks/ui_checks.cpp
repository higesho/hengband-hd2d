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
#include <SDL2/SDL_image.h> //!< `--status-hud-check` が素材の画素を測る

#include "app/checks/command_tabs_checks.h"
#include "app/checks/top_menu_checks.h"
#include "app/checks/game_over_checks.h"
#include "app/checks/modern_skin_preview.h"
#include "app/checks/panel_skin_preview.h"
#include "app/checks/english_details_preview.h"
#include "app/checks/menu_input_icons_checks.h"
#include "app/checks/menu_scale_detection_checks.h"
#include "app/checks/menu_selection_checks.h"
#include "app/checks/menu_content_checks.h"
#include "app/checks/context_popup_checks.h"
#include "app/checks/footer_cancel_checks.h"
#include "app/checks/scroll_list_checks.h"
#include "app/checks/birth_cursor_checks.h"
#include "app/checks/text_number_input_checks.h"
#include "app/checks/message_history_checks.h"
#include "app/checks/panel_font_sync_checks.h"
#include "app/checks/targeting_checks.h"
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
    if(const auto *output=std::getenv("HD2D_TARGETING_CHECK"))return run_targeting_checks(options,output);
    if(const auto *output=std::getenv("HD2D_PANEL_FONT_CHECK"))return run_panel_font_sync_checks(options,output);
    if(const auto *output=std::getenv("HD2D_MENU_CONTENT_CHECK"))return run_menu_content_checks(options,output);
    if(const auto *output=std::getenv("HD2D_MENU_SELECTION_CHECK"))return run_menu_selection_checks(options,output);
    if(const auto *output=std::getenv("HD2D_MENU_SCALE_CHECK"))return run_menu_scale_detection_checks(options,output);
    if(const auto *output=std::getenv("HD2D_MENU_INPUT_CHECK"))return run_menu_input_icons_check(options,output);
    if(const auto *output=std::getenv("HD2D_CONTEXT_POPUP_CHECK"))return run_context_popup_check(options,output);
    if(const auto *output=std::getenv("HD2D_FOOTER_CANCEL_CHECK"))return run_footer_cancel_check(options,output);
    if(const auto *output=std::getenv("HD2D_ENGLISH_DETAILS_PREVIEW"))return render_english_details_preview(options,output);
    if(const auto *output=std::getenv("HD2D_PANEL_SKIN_PREVIEW"))return render_panel_skin_preview(options,output);
    if(const auto *output=std::getenv("HD2D_MENU_SKIN_PREVIEW"))return render_modern_skin_preview(options,output);
    if (std::getenv("HD2D_COMMAND_TABS_CHECK") != nullptr) return run_command_tabs_checks();
    if (std::getenv("HD2D_TOP_MENU_CHECK") != nullptr) return run_top_menu_checks();
    if (std::getenv("HD2D_GAME_OVER_CHECK") != nullptr) return run_game_over_checks();
    if (std::getenv("HD2D_SCROLL_LIST_CHECK") != nullptr) return run_scroll_list_checks();
    if (std::getenv("HD2D_BIRTH_CURSOR_CHECK") != nullptr) return run_birth_cursor_checks();
    if (std::getenv("HD2D_TEXT_NUMBER_INPUT_CHECK") != nullptr) return run_text_number_input_checks();
    if (std::getenv("HD2D_MESSAGE_HISTORY_CHECK") != nullptr) return message_history_check::run_message_history_checks(options);
    if (std::getenv("HD2D_CORE_LINK_CHECK") != nullptr) {
        return run_core_link_check();
    }
    if (std::getenv("HD2D_SDL_INPUT_CHECK") != nullptr) {
        return run_sdl_input_check();
    }
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
    /*!
     * **実際の窓の大きさが固定のどれかと同じなら、重ねずに 1 度だけ回す。**`--windowed=1280x720` で
     * 回すと先頭（実際の窓）と 2 番目が同じ大きさになり、同じ配置の検査が 2 度走って、失敗が
     * 2 度数えられていた（2026-09-30。AH-60 の担当が Linux の `--ui-check` で見つけた）。
     */
    std::vector<std::array<int, 2>> sizes;
    for (const std::array<int, 2> &candidate : { std::array<int, 2>{ screen_w, screen_h }, std::array<int, 2>{ 1280, 720 },
             std::array<int, 2>{ 2560, 1080 }, std::array<int, 2>{ 1080, 1920 } }) {
        if (std::find(sizes.begin(), sizes.end(), candidate) == sizes.end()) {
            sizes.push_back(candidate);
        }
    }

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
                /*
                 * **HP/MP の玉**。ゲーム中の作りのどれでも置かれ、
                 * 地図の中・左右の中央に収まり、最下段の帯・状態列・名前の行・下段のサブパネルと
                 * 重ならないこと（メッセージとプロンプトの帯は玉の上に重ねて描くので見ない。2026-10-09）。
                 * **右列のサブパネル（`sub[4..6]`）は見ない**——重なる作りで開いたとき、玉の右の翼の
                 * 端がパネルの下へ入るのは仕様である（玉は地図の左右の中央に置く）。
                 */
                {
                    const RectPx &orb = layout.orb;
                    if (orb.empty()) {
                        fail(tag + ": HP/MP の玉が置かれていません");
                    } else {
                        //! 見えている部分（下の薄い影を除く）で見る。設計の矩形は下縁より下へはみ出す（2026-10-09）。
                        const RectPx shown = orb_visible_rect(orb);
                        const long long orb_area = static_cast<long long>(shown.w) * static_cast<long long>(shown.h);
                        if (rect_overlap_area(shown, layout.scene) != orb_area) {
                            fail(tag + ": 玉が地図の枠からはみ出しています");
                        }
                        const std::pair<const char *, RectPx> avoids[] = {
                            { "bottom_bar", layout.bottom_bar }, { "message_bar", layout.message_bar }, { "status_col", layout.status_col },
                            { "hud", layout.hud }, { "sub[0]", layout.sub[0] }, { "sub[1]", layout.sub[1] },
                            { "sub[2]", layout.sub[2] }, { "sub[3]", layout.sub[3] },
                        };
                        for (const auto &avoid : avoids) {
                            const long long overlap = rect_overlap_area(shown, avoid.second);
                            if (overlap > 0) {
                                fail(tag + ": 玉と " + avoid.first + " が重なっています（" + std::to_string(overlap) + " 画素）");
                            }
                        }
                        if (std::abs(((orb.x * 2) + orb.w) - ((layout.scene.x * 2) + layout.scene.w)) > 2) {
                            fail(tag + ": 玉が地図の左右の中央にありません");
                        }
                        if ((orb.h > kOrbHeightMax)
                            || (std::abs((orb.w * kOrbDesignH) - (orb.h * kOrbDesignW)) > kOrbDesignW)) {
                            fail(tag + ": 玉の大きさが上限を超えているか縦横比が違います（"
                                + std::to_string(orb.w) + "x" + std::to_string(orb.h) + "）");
                        }
                    }
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

    /*
     * (1b) 状態列の幅（修-15）。字を大きくしても、申告された桁数（＋左右の余白 1 桁ずつ）が幅で切れない。
     * 13 桁（変愚・短愚など）と 20 桁（幻想蛮怒・Frox）を、作り 3 種・窓 2 種・マスの幅 8〜16 で回す。
     */
    for (const int cols : { 13, 20 }) {
        set_status_col_cols(cols);
        for (const LayoutMode mode : { LayoutMode::Split, LayoutMode::Hybrid, LayoutMode::Tall }) {
            for (const std::array<int, 2> &size : { std::array<int, 2>{ 1280, 720 }, std::array<int, 2>{ 2560, 1080 } }) {
                for (const int cell_w : { 8, 12, 14, 16 }) {
                    const UiLayout layout = UiLayout::compute(mode, size[0], size[1], cell_w, cell_w * 2, false);
                    if (layout.status_col.empty()) {
                        continue; // 状態列が無い作り
                    }
                    const int want = cell_w * (cols + 2);
                    if (layout.status_col.w < want) {
                        fail(std::string("状態列が桁の幅に足りません（") + layout_mode_name(mode) + " " + std::to_string(size[0])
                            + "x" + std::to_string(size[1]) + "・" + std::to_string(cols) + "桁・マス " + std::to_string(cell_w)
                            + "：" + std::to_string(layout.status_col.w) + " < " + std::to_string(want) + "）");
                    }
                }
            }
        }
    }
    set_status_col_cols(0);
    /*
     * (1c) 下の帯の操作説明（修-14）。コントローラーの案内（左）とキーの案内（右端）が同じ行に出る。
     * どんな幅でも、実際に描く幅の合計が帯の幅を超えない（＝矩形が交わらない）。
     */
    for (int body_w = 40; body_w <= 1800; body_w += 7) {
        for (const int hint_w : { 300, 520, 900 }) {
            for (const int key_w : { 0, 200, 480, 760 }) {
                const int kw = std::min(key_w, body_w);
                const int room = bottom_hint_room(body_w, hint_w, kw, 16, 32);
                const int drawn = (room > 0) ? std::min(hint_w, room) : 0;
                if (drawn + kw > body_w) {
                    fail("下の帯の左右の案内が重なります（帯 " + std::to_string(body_w) + "・左 " + std::to_string(hint_w)
                        + "・右 " + std::to_string(kw) + " → 左を " + std::to_string(drawn) + "）");
                }
            }
        }
    }

    /*
     * (1d) 問いの帯の選択肢が幅を超えるとき（修-32 の担当の気づき。Frox の `Travel: `）。右が切れて選んでいる選択肢の枠が
     * 見えない所へ行っていた。どの選択肢を選んでも、その選択肢が帯に入る（入るときは行も枠も従来のまま）。
     * 字の幅は ASCII 8・それ以外 16 画素（窓 1280x720 の実測と同じ割合）。
     */
    {
        const auto measure = [](const std::string &s) {
            int w = 0;
            for (std::size_t i = 0; i < s.size();) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                if (c < 0x80) {
                    w += 8;
                    i += 1;
                } else {
                    w += 16;
                    i += (c >= 0xF0) ? 4 : ((c >= 0xE0) ? 3 : 2);
                }
            }
            return w;
        };
        struct Band {
            std::string head;
            std::vector<std::string> labels;
        };
        const std::vector<Band> bands = {
            { "Travel: ", { "Select this square", "Next down stairs", "Next up stairs", "Next monster", "Toggle monster visibility", "Journey targets" } },
            { reinterpret_cast<const char *>(u8"移動先を選ぶ: "),
                { reinterpret_cast<const char *>(u8"このマスを選ぶ"), reinterpret_cast<const char *>(u8"次の下り階段"),
                    reinterpret_cast<const char *>(u8"次の上り階段"), reinterpret_cast<const char *>(u8"次のモンスター"),
                    reinterpret_cast<const char *>(u8"モンスターの表示切替"), reinterpret_cast<const char *>(u8"目的地の一覧") } },
        };
        for (const Band &band : bands) {
            std::string line = band.head;
            std::vector<std::pair<int, int>> ranges;
            for (const std::string &label : band.labels) {
                if (line != band.head) {
                    line += "  ";
                }
                line += '[';
                ranges.emplace_back(static_cast<int>(line.size()), static_cast<int>(label.size()));
                line += label;
                line += ']';
            }
            for (const int limit : { 330, 420, 560, 730, 1100, 2000 }) {
                for (int sel = 0; sel < static_cast<int>(ranges.size()); ++sel) {
                    const PromptBandView v = fit_prompt_band(line, ranges, sel, limit, measure);
                    const std::string where = "（帯 " + std::to_string(limit) + "・" + std::to_string(sel) + " 番）";
                    if (measure(line) <= limit) {
                        if (v.cut || (v.text != line)) {
                            fail("問いの帯が入るのに絞っています" + where);
                        }
                        continue;
                    }
                    const std::string label = band.labels[static_cast<std::size_t>(sel)];
                    const std::size_t at = v.text.find("[" + label + "]");
                    if (!v.cut || (at == std::string::npos)) {
                        fail("問いの帯が入らないのに、選んでいる選択肢が帯に出ません" + where);
                        continue;
                    }
                    if (measure(v.text) > limit) {
                        fail("問いの帯の絞った字が幅を超えています" + where);
                    }
                    if ((v.frame_x != measure(v.text.substr(0, at + 1))) || (v.frame_w != measure(label))) {
                        fail("問いの帯の枠が選んでいる選択肢とずれています" + where);
                    }
                }
            }
        }
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
        want.push_back(Named{ "orb", layout.orb }); //!< 画像なしの検査なので、玉の代わりの棒が描かれる
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
        /*
         * 棒は玉（`orb`）に置き換わった（2026-10-08）。この検査は画像を渡さないので、
         * 玉の矩形の中に**元の棒**が描かれる。名札はそこに出る。
         * 玉の画像で描いた名札は `--status-hud-check=` が同じ約束で見る。
         */
        const std::size_t same = count_changed_pixels(empty_label, spelled_sp,
            screen_w, screen_h, gauge_layout.orb);
        const std::size_t moved = count_changed_pixels(empty_label, voice,
            screen_w, screen_h, gauge_layout.orb);
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
        /*
         * この節は、Modern UI 導入前からある「従来の機能メニュー」の回帰検査である。
         * 現在の既定値は modern_ui=true で、Modern 側は Confirm で編集モードへ入る
         * 設計・タブ式コマンドメニューなので、ここへその既定値をそのまま流すと
         * 「直接 Right で値を変える」「分類へ潜る」という旧契約の検査そのものが
         * 別 UI を誤操作して FAIL する。Modern 側は command-tabs / menu-input /
         * menu-scale / menu-selection / menu-content の専用検査で別に覆う。
         */
        settings.modern_ui = false;
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
         * ——遊ぶ人から見れば「片方を直すともう片方が壊れる」になる。
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
        /*
         * **画面が自分で描くサブパネルの種類（100〜104。Rogue 系・Umoria）が往復すること**
         * （読む側が 0〜15 だけを受けて、保存した割り当てが次の起動で消えていた。2026-10-04）。
         */
        settings.sub_panel_kind[1] = kBuiltinSubPanelKindBase + 2;
        settings.sub_panel_kind[4] = kBuiltinSubPanelKindBase + 4;
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
            /*
             * `HENGBAND_MUTE=1`（検査で音を鳴らさない。決めたこと 2026-09-26）のときは
             * 装置を開かないので、装置が要る項目だけ飛ばす。表引き・定位・減衰は下で見る。
             */
            std::string audio_log = "HENGBAND_MUTE のため装置は見ていない";
            const char *audio_mute = std::getenv("HENGBAND_MUTE");
            if (audio_mute != nullptr && audio_mute[0] != '0') {
                std::puts("[ui-check] 音の装置: HENGBAND_MUTE のため省いた");
            } else {
            AudioEngine engine;
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
            }
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
        bind_state.controller_binds = build_pad_bind_items(settings.pad, many, PadMacroFlags{}, 0, settings.modern_ui);
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
         * (4''''') **決定（Enter / A）が何もしなくなる条件**（決めたこと 2026-09-25。
         * 元は 2026-08-23 の「A に、決定と同じく既定でコマンドメニューを置きたい」）。
         *
         * コマンドメニューはいま F1 とパッド Start だけが開く。決定は確認の専任で、
         * 確認する相手が無いとき（`nothing_to_confirm()`）は**何もしない**
         * （コアへも送らない）。規則は `nothing_to_confirm()` の 1 か所で、パッドの A と
         * キーボードの Enter が同じそこを見る。ここで押さえるのは 3 つ:
         *   1. **命令をただ待っているときだけ「無い」と判定する**
         *      （コアが `awaiting_command` を立てたとき）
         *   2. **`-more-` や `y/n` の待ちでは「ある」と判定する**（決定がそのまま答えを運ぶ）
         *   3. **文字を打っている最中は「ある」と判定する**（打ち終わりの Enter を奪わない）
         *
         * 2 は「旗を立てないコア（変愚・短愚・幻想）では今までどおり」も兼ねている
         * ——あちらは `\r` を受けたコア自身が通常メニューを開く。
         */
        {
            GameFrame waiting{};
            waiting.awaiting_command = true;
            if (!nothing_to_confirm(waiting)) {
                fail("決定の行き先: 命令を待っているのに Enter/A が何もしなくなりません");
            }
            GameFrame busy{};
            busy.awaiting_command = false;
            if (nothing_to_confirm(busy)) {
                fail("決定の行き先: 命令待ちでないのに Enter/A が何もしなくなります"
                     "（`-more-` が進まなくなります）");
            }
            GameFrame typing{};
            typing.awaiting_command = true;
            typing.text_input_active = true;
            if (nothing_to_confirm(typing)) {
                fail("決定の行き先: 文字を打っている最中に Enter/A が何もしなくなります");
            }
            std::fprintf(stderr, "  (4''''') 決定の行き先: 命令待ちだけで何もしなくなる"
                                 "（`-more-`・文字入力では今までどおり確認へ渡る。"
                                 "コマンドメニューは F1 / Start 専任）\n");
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
             * （`id=2`・`i=1`）は `b) 検査の命令 2` になっていなければならない。
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
                if (!has("b) 検査の命令 2")) {
                    fail("コマンドメニュー: 名札に盤の綴りが出ていません（「b) 検査の命令 2」のはず）");
                }
                if (!has("c) 検査の命令 3")) {
                    fail("コマンドメニュー: 綴りが 1 件だけしか出ていません（「c) 検査の命令 3」のはず）");
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

            /*
             * (a''') **前の案内の「抜け」「幕が上がる」の最中に次の階へ着いても、新しい案内が始まる**
             * （2 周目のテストの報告 2026-10-15。案内を閉じて 0.85 秒以内に次の階段を踏むと、
             * `Idle` でしか受けない版は幕も案内も出さず、新しい階がそのまま見えた）。
             *
             * 抜け（`Out`）と幕が上がる間（`Reveal`）の 2 通りで、それぞれ
             * ① 階だけ新しく地図は元のまま → 幕が張り直される（`pending()`）
             * ② 続く到着 → 案内が始まる（`blocks_input()`）
             * を見る。
             */
            for (int phase_case = 0; phase_case < 2; ++phase_case) {
                const bool in_out = (phase_case == 0); //!< 0＝抜けの最中・1＝幕が上がる最中
                const char *const label = in_out ? "抜けの最中" : "幕が上がる最中";
                FloorCutin re;
                std::string re_err;
                if (!re.init(re_err)) {
                    fail("カットイン: 用意できませんでした（連続の階段の検査）: " + re_err);
                    continue;
                }
                const GameFrame ra = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 1, false);
                re.update(ra, 0.016f, layout, true); //!< 覚えるだけ
                const GameFrame rb = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 2, false);
                re.update(rb, 0.016f, layout, true); //!< 到着 → 案内が始まる
                for (int i = 0; i < 120; ++i) {
                    re.update(rb, 0.016f, layout, true); //!< 入り → 待ち
                }
                if (!re.confirm()) {
                    fail(std::string("カットイン: 連続の階段の検査で決定を受け付けません（") + label + "）");
                    continue;
                }
                //! 抜け（0.30 秒）は 5 フレームで抜けの最中、40 フレーム（0.64 秒）で幕が上がる最中。
                for (int i = 0; i < (in_out ? 5 : 40); ++i) {
                    re.update(rb, 0.016f, layout, true);
                }
                if (!re.active() || re.blocks_input() == !in_out) {
                    //! 抜けの間は入力を止めている・幕が上がる間は止めていない（段に居ることの確かめ）。
                    fail(std::string("カットイン: 連続の階段の検査の段に居ません（") + label + "）");
                    continue;
                }
                //! ① 階だけ新しく・地図は元の階のまま（`-more-` の待ち）。
                GameFrame rmid = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 3, false);
                rmid.floor.generated_turn = rb.floor.generated_turn;
                rmid.minimap = rb.minimap;
                re.update(rmid, 0.016f, layout, true);
                if (!re.pending()) {
                    fail(std::string("カットイン: **前の案内の") + label
                        + "に次の階段を踏んでも幕が張り直されません**（元の階が見える）");
                }
                if (re.blocks_input()) {
                    fail(std::string("カットイン: 連続の階段で、地図が元の階のままの 1 枚で始まりました（")
                        + label + "）");
                }
                //! ② 行先の地図が来た。**新しい階の案内が始まる。**
                const GameFrame rarr = make_floor_frame(static_cast<int>(FloorKind::Dungeon), 2, 3, false);
                re.update(rarr, 0.016f, layout, true);
                for (int i = 0; i < 40; ++i) {
                    re.update(rarr, 0.016f, layout, true); //!< 入り（0.42 秒）→ 決定待ち。前の案内の抜けが続くだけなら待ちに着かない
                }
                if (!re.waiting()) {
                    fail(std::string("カットイン: **前の案内の") + label
                        + "に着いた階の案内が決定待ちに着きません**（新しい階がそのまま見える）");
                }
                re.shutdown();
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
                                 "**前の案内の抜け・幕が上がる最中に着いても始まる** / "
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

/* ====================================================================== */
/*  --status-hud-check=出力.png                                            */
/*  HP/MP の玉とバフ・デバフのアイコン            */
/* ====================================================================== */

namespace {

//! 画面外の描画先（窓の大きさに縛られず 2560x1080 などを描くため）。
struct StatusHudTarget {
    GLuint fbo{ 0 };
    GLuint tex{ 0 };
    int w{ 0 };
    int h{ 0 };
};

bool make_status_hud_target(StatusHudTarget &t, int w, int h)
{
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void release_status_hud_target(StatusHudTarget &t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (t.fbo != 0) {
        glDeleteFramebuffers(1, &t.fbo);
    }
    if (t.tex != 0) {
        glDeleteTextures(1, &t.tex);
    }
    t = StatusHudTarget{};
}

//! 合成のバフ・デバフ（玉の横: 良い 8・悪い 3・知らない名前 1 ／ 常時の列: 良い 14・呪い 3）。
void fill_status_hud_frame(GameFrame &frame)
{
    frame.hud.name = "検査用のキャラクター";
    frame.hud.hp = 171;
    frame.hud.hp_max = 245;
    frame.hud.sp = 38;
    frame.hud.sp_max = 92;
    frame.hud.gold = 12345;
    frame.hud.level = 27;
    frame.status_effects.clear();
    const auto add = [&](const char *key, const char *label, const char *amount, StatusEffectKind kind) {
        StatusEffectView v;
        v.key = key;
        v.label_utf8 = label;
        v.amount_utf8 = amount;
        v.kind = kind;
        frame.status_effects.push_back(v);
    };
    add("haste", "加速", "+10", StatusEffectKind::Good);
    add("hero", "ヒーロー", "", StatusEffectKind::Good);
    add("blessed", "祝福", "", StatusEffectKind::Good);
    add("res_fire", "火炎耐性", "", StatusEffectKind::Good);
    add("res_cold", "冷気耐性", "", StatusEffectKind::Good);
    add("telepathy", "テレパシー", "", StatusEffectKind::Good);
    add("sh_fire", "火のオーラ", "", StatusEffectKind::Good); //!< 5 文字: 「火の／オーラ」に折れる
    add("recall", "帰還待ち", "", StatusEffectKind::Waiting);
    add("poisoned", "毒", "", StatusEffectKind::Bad);
    add("confused", "混乱", "", StatusEffectKind::Bad);
    add("cut", "ひどい切り傷", "", StatusEffectKind::Bad); //!< 6 文字: 「ひどい／切り傷」
    add("no_such_key", "Sense Dragons", "", StatusEffectKind::Good); //!< 英語 2 語: 「Sense／Dragons」
    //! 常時の効果（良い 14・呪い 3）。名前の長いものを混ぜて、字の縮めを通す。
    add("res_fire", "耐火炎", "", StatusEffectKind::Permanent);
    add("imm_cold", "冷気免疫", "", StatusEffectKind::Permanent);
    add("res_shard", "耐破片", "", StatusEffectKind::Permanent);
    add("res_chaos", "耐カオス", "", StatusEffectKind::Permanent);
    add("free_act", "麻痺知らず", "", StatusEffectKind::Permanent);
    add("senseunseen", "透明視認", "", StatusEffectKind::Permanent);
    add("telepathy", "テレパシー", "", StatusEffectKind::Permanent);
    add("esp_dragon", "ドラゴンの感知", "", StatusEffectKind::Permanent); //!< 7 文字: 「ドラゴン／の感知」
    add("sust_str", "腕力維持", "", StatusEffectKind::Permanent);
    add("slay_evil", "邪悪攻撃", "", StatusEffectKind::Permanent);
    add("attk_fire", "火炎攻撃", "", StatusEffectKind::Permanent);
    add("haste", "加速", "", StatusEffectKind::Permanent);
    add("lite", "照明", "", StatusEffectKind::Permanent);
    add("regeneration", "Fast Regeneration", "", StatusEffectKind::Permanent); //!< 英語 2 語
    add("cursed", "弱い呪い増殖", "", StatusEffectKind::PermanentBad); //!< 6 文字: 「弱い／呪い増殖」
    add("aggravate", "反感", "", StatusEffectKind::PermanentBad);
    add("drain_exp", "経験値減少", "", StatusEffectKind::PermanentBad);
}

//! 同じ合成フレームの名前を英語にする（英語のコアの見え方。空白で折れる名前・折れない 1 語の名前を混ぜる）。
void make_status_hud_frame_english(GameFrame &frame)
{
    struct Entry {
        const char *key;
        StatusEffectKind kind;
        const char *label;
    };
    static const Entry kEnglish[] = {
        { "haste", StatusEffectKind::Good, "Haste" }, { "hero", StatusEffectKind::Good, "Heroism" },
        { "blessed", StatusEffectKind::Good, "Blessed" }, { "res_fire", StatusEffectKind::Good, "Resist Fire" },
        { "res_cold", StatusEffectKind::Good, "Resist Cold" }, { "telepathy", StatusEffectKind::Good, "Telepathy" },
        { "sh_fire", StatusEffectKind::Good, "Fire Aura" }, { "recall", StatusEffectKind::Waiting, "Word of Recall" },
        { "poisoned", StatusEffectKind::Bad, "Poisoned" }, { "confused", StatusEffectKind::Bad, "Hallucination" },
        { "cut", StatusEffectKind::Bad, "Deep Gash" }, { "no_such_key", StatusEffectKind::Good, "Sense Dragons" },
        { "res_fire", StatusEffectKind::Permanent, "Resist Fire" }, { "imm_cold", StatusEffectKind::Permanent, "Immune to Cold" },
        { "res_shard", StatusEffectKind::Permanent, "Resist Shards" }, { "res_chaos", StatusEffectKind::Permanent, "Resist Chaos" },
        { "free_act", StatusEffectKind::Permanent, "Free Action" }, { "senseunseen", StatusEffectKind::Permanent, "See Invisible" },
        { "telepathy", StatusEffectKind::Permanent, "Telepathy" }, { "esp_dragon", StatusEffectKind::Permanent, "Sense Dragons" },
        { "sust_str", StatusEffectKind::Permanent, "Sustain Strength" }, { "slay_evil", StatusEffectKind::Permanent, "Slay Evil" },
        { "attk_fire", StatusEffectKind::Permanent, "Fire Brand" }, { "haste", StatusEffectKind::Permanent, "Speed" },
        { "lite", StatusEffectKind::Permanent, "Light Source" }, { "regeneration", StatusEffectKind::Permanent, "Fast Regeneration" },
        { "cursed", StatusEffectKind::PermanentBad, "Weak Curse Spread" }, { "aggravate", StatusEffectKind::PermanentBad, "Aggravate" },
        { "drain_exp", StatusEffectKind::PermanentBad, "Experience Drain" },
    };
    for (StatusEffectView &v : frame.status_effects) {
        for (const Entry &e : kEnglish) {
            if ((v.key == e.key) && (v.kind == e.kind)) {
                v.label_utf8 = e.label;
                break;
            }
        }
    }
}

} // namespace

int run_status_hud_check(const AppOptions &options)
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
    UiImagePainter images;
    if (!text.init(kTextPx, err) || !paint.init(err) || !images.init(err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }

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

    // ---- 素材 ----
    UiImage back_image;
    UiImage glass_image;
    UiImage liquid_hp_image;
    UiImage liquid_mp_image;
    UiImage glass_top_image;
    UiImage front_image;
    UiImage icons_image;
    {
        const std::pair<UiImage *, const char *> files[] = {
            { &back_image, kOrbBackPath }, { &glass_image, kOrbGlassPath }, { &liquid_hp_image, kOrbLiquidHpPath },
            { &liquid_mp_image, kOrbLiquidMpPath }, { &glass_top_image, kOrbGlassTopPath },
            { &front_image, kOrbFrontPath }, { &icons_image, kStatusIconsPath },
        };
        for (const auto &file : files) {
            std::string load_err;
            const bool ok = file.first->load_minified(file.second, load_err);
            check(ok, std::string("素材を読めます: ") + file.second + (ok ? "" : (" / " + load_err)));
        }
    }
    UiBackdrops backdrops;
    backdrops.painter = &images;
    backdrops.orb.painter = &images;
    backdrops.orb.back = &back_image;
    backdrops.orb.glass = &glass_image;
    backdrops.orb.liquid_hp = &liquid_hp_image;
    backdrops.orb.liquid_mp = &liquid_mp_image;
    backdrops.orb.glass_top = &glass_top_image;
    backdrops.orb.front = &front_image;
    backdrops.orb.status_icons = &icons_image;
    check(backdrops.orb.has_orb() && backdrops.orb.has_icons(), "玉の 6 層とアイコンのシートが揃っています");
    check(icons_image.valid() && (icons_image.width() / 10 == 64) && (icons_image.height() / 64 >= 8),
        "アイコンのシートは 64x64 のセルを 10 列に並べたもの");

    // ---- 名前の表 ----
    check(status_icon_index("haste") == 1 && status_icon_known("haste"), "名前の表: haste = 1");
    check(status_icon_index("no_such_key") == 0 && !status_icon_known("no_such_key"),
        "名前の表: 知らない名前は 0 番（汎用）");
    check(status_icon_index("poisoned") == 70 && status_icon_index("tsuyoshi") == 79, "名前の表: poisoned = 70 / tsuyoshi = 79");

    // ---- 画面の外の描画先と、1 枚描く手 ----
    GameFrame base;
    fill_synthetic_ui(base);
    fill_status_hud_frame(base);

    const auto read_rgba = [&](int w, int h) {
        std::vector<unsigned char> pixels = read_framebuffer(w, h);
        for (std::size_t i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 255; // ブレンドで α が 1 未満に残った所も不透明にして書く
        }
        return pixels;
    };
    LayoutMode render_mode = LayoutMode::Hybrid; //!< 描く作り（既定は Hybrid。絵の書き出しだけ Full に替える）
    const auto make_layout = [&](int w, int h) {
        return UiLayout::compute(render_mode, w, h, text.cell_w(), text.cell_h(), false);
    };
    /*
     * 1 枚描く。**地図の代わりの市松を先に流す**（画像は呼んだ瞬間に描かれ、下敷きと字は
     * 後で流れるので、市松を後回しにすると玉の上へ乗ってしまう）。
     */
    const auto render = [&](StatusHudTarget &target, const GameFrame &frame, HudState &state, std::uint32_t now_ms) {
        const int w = target.w;
        const int h = target.h;
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.06f, 0.08f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        paint.begin(w, h);
        text.begin(w, h);
        images.begin(w, h);
        const int tile = 40;
        for (int ty = 0; ty < ((h / tile) + 1); ++ty) {
            for (int tx = 0; tx < ((w / tile) + 1); ++tx) {
                const bool even = ((tx + ty) % 2) == 0;
                const float v = even ? 0.30f : 0.24f;
                paint.rect(RectPx{ tx * tile, ty * tile, tile, tile }, PaintColor{ v, v * 1.05f, v * 0.85f, 1.f });
            }
        }
        paint.flush();
        const UiLayout layout = make_layout(w, h);
        state.orb_now_ms = now_ms;
        draw_game_ui(paint, text, layout, frame, state, &backdrops);
        paint.flush();
        text.flush();
        return read_rgba(w, h);
    };
    const auto ratio_hp = [](const GameFrame &f) {
        return static_cast<float>(f.hud.hp) / static_cast<float>(f.hud.hp_max);
    };
    const auto ratio_mp = [](const GameFrame &f) {
        return static_cast<float>(f.hud.sp) / static_cast<float>(f.hud.sp_max);
    };
    //! 時刻を 16 ms 刻みで進める（動きだけ。描かない）。
    const auto advance = [](HudState &state, float hp, float mp, std::uint32_t from_ms, std::uint32_t to_ms) {
        for (std::uint32_t t = from_ms + 16u; t <= to_ms; t += 16u) {
            orb_hud_update(state.orb_anim, state.orb_params, hp, mp, t);
        }
    };

    std::string stem = options.status_hud_check_path;
    std::string ext = ".png";
    {
        const std::size_t dot = stem.find_last_of('.');
        const std::size_t slash = stem.find_last_of("/\\");
        if ((dot != std::string::npos) && ((slash == std::string::npos) || (dot > slash))) {
            ext = stem.substr(dot);
            stem = stem.substr(0, dot);
        }
    }
    const auto out_path = [&](int w, int h, const char *suffix) {
        return stem + "_" + std::to_string(w) + "x" + std::to_string(h) + suffix + ext;
    };

    // ---- 2 つの大きさで、配置の規則を数える ----
    for (const std::array<int, 2> &size : { std::array<int, 2>{ 2560, 1080 }, std::array<int, 2>{ 1280, 720 } }) {
        const int w = size[0];
        const int h = size[1];
        const std::string tag = std::to_string(w) + "x" + std::to_string(h);
        StatusHudTarget target;
        if (!make_status_hud_target(target, w, h)) {
            check(false, tag + ": 画面の外の描画先を作れません");
            release_status_hud_target(target);
            continue;
        }
        const UiLayout layout = make_layout(w, h);
        const RectPx &orb = layout.orb;
        const RectPx window_rect{ 0, 0, w, h };

        // 玉の置き場所
        check(!orb.empty(), tag + ": 玉が置かれています");
        const RectPx shown = orb_visible_rect(orb); //!< 見えている部分（下の薄い影は下縁より下にはみ出して描かない）
        const long long orb_area = static_cast<long long>(shown.w) * static_cast<long long>(shown.h);
        check(rect_overlap_area(shown, layout.scene) == orb_area, tag + ": 玉（見えている部分）は地図の中");
        check(std::abs(((orb.x * 2) + orb.w) - ((layout.scene.x * 2) + layout.scene.w)) <= 2, tag + ": 玉は左右の中央");
        const int expected_h = std::clamp(static_cast<int>(std::lround(layout.scene.h * kOrbHeightRatio)), kOrbHeightMin, kOrbHeightMax);
        check(orb.h == expected_h, tag + ": 玉の高さ = 地図の高さ x 0.247（" + std::to_string(orb.h) + " / 期待 " + std::to_string(expected_h) + "）");
        check(std::abs((orb.w * kOrbDesignH) - (orb.h * kOrbDesignW)) <= kOrbDesignW, tag + ": 玉の縦横比は 760:250");
        {
            const RectPx *const avoid[] = { &layout.bottom_bar, &layout.status_col, &layout.hud };
            long long overlap = 0;
            for (const RectPx *rect : avoid) {
                overlap += rect_overlap_area(shown, *rect);
            }
            check(overlap == 0, tag + ": 玉は最下段・状態列・名前の行と重ならない");
            check(rect_overlap_area(shown, layout.message_bar) == 0, tag + ": メッセージの帯は玉の左で止まる");
        }
        {
            // 玉の下端は地図の下縁（最下段の帯・重なる作りで開いた下段のサブパネルがあればその上）。
            // メッセージとプロンプトの帯は避けない（帯を玉の上に重ねて描く。決めたこと 2026-10-09）。
            int want_bottom = layout.scene.y + layout.scene.h;
            if (!layout.bottom_bar.empty()) {
                want_bottom = std::min(want_bottom, layout.bottom_bar.y);
            }
            if (layout.sub_overlaid) {
                for (int i = kSubBottomSlot; i < (kSubBottomSlot + kSubBottomMax); ++i) {
                    if (!layout.sub[static_cast<std::size_t>(i)].empty()) {
                        want_bottom = std::min(want_bottom, layout.sub[static_cast<std::size_t>(i)].y);
                    }
                }
            }
            check((shown.y + shown.h) == want_bottom, tag + ": 玉の見えている下端は地図の下縁（" + std::to_string(shown.y + shown.h) + " / 期待 " + std::to_string(want_bottom) + "）");
        }

        // アイコンの置き場所（玉の横は左右とも 1 行だけ。入りきらない側は輪にして流す）
        const StatusIconLayout placed = layout_status_icons(text, layout, base);
        int left_total = 0; //!< 玉の横に置くもの（常時の効果は別の列）
        int right_total = 0;
        for (const StatusEffectView &effect : base.status_effects) {
            left_total += ((effect.kind == StatusEffectKind::Good) || (effect.kind == StatusEffectKind::Waiting)
                              || (effect.kind == StatusEffectKind::Special)) ? 1 : 0;
            right_total += (effect.kind == StatusEffectKind::Bad) ? 1 : 0;
        }
        check(placed.icon_px == std::clamp(static_cast<int>(std::lround(orb.h * 0.24)), 24, 64),
            tag + ": アイコンの大きさ = 玉の高さ x 0.24（" + std::to_string(placed.icon_px) + "）");
        int left_limit = layout.scene.x + layout.cell_w;
        if (!layout.status_col.empty() && (layout.status_col.x <= layout.scene.x)) {
            left_limit = std::max(left_limit, layout.status_col.x + layout.status_col.w + layout.cell_w);
        }
        const int right_limit = (layout.scene.x + layout.scene.w) - layout.cell_w;
        bool sides_ok = true;
        bool inside_ok = true;
        bool apart_ok = true;
        bool one_row_ok = true;
        bool kinds_ok = true;
        bool order_ok = true;
        bool icon_ids_ok = true;
        int left_count = 0;
        int right_count = 0;
        const int row0_y = orb.y + static_cast<int>(std::lround(orb.h * 0.42));
        long long avoided_overlap = 0;
        const auto true_x = [](const StatusIconPlacement &item) { return static_cast<double>(item.cell.x) + static_cast<double>(item.frac_x); };
        //! 見えている部分（巡回しているものは枠で切った分）の矩形。
        const auto visible_rect = [&](const StatusIconPlacement &item) {
            if (!item.rotating) {
                return item.cell;
            }
            const int x0 = std::max(item.clip.x, static_cast<int>(std::floor(true_x(item))));
            const int x1 = std::min(item.clip.x + item.clip.w, static_cast<int>(std::ceil(true_x(item) + item.cell.w)));
            return RectPx{ x0, item.cell.y, std::max(0, x1 - x0), item.cell.h };
        };
        for (std::size_t a = 0; a < placed.items.size(); ++a) {
            const StatusIconPlacement &item = placed.items[a];
            const StatusEffectView &effect = base.status_effects[static_cast<std::size_t>(item.effect_index)];
            const bool want_left = (effect.kind != StatusEffectKind::Bad);
            kinds_ok = kinds_ok && (item.left == want_left);
            const RectPx vis = visible_rect(item);
            if (item.left) {
                ++left_count;
                sides_ok = sides_ok && ((vis.x + vis.w) <= orb.x) && (vis.x >= left_limit);
            } else {
                ++right_count;
                sides_ok = sides_ok && (vis.x >= (orb.x + orb.w)) && ((vis.x + vis.w) <= right_limit);
            }
            const long long vis_area = static_cast<long long>(vis.w) * static_cast<long long>(vis.h);
            inside_ok = inside_ok && (vis_area > 0) && (rect_overlap_area(vis, layout.scene) == vis_area)
                && (rect_overlap_area(vis, window_rect) == vis_area);
            //! プロンプトの帯は見ない（玉とアイコンの上に重ねて描く）。左の段はメッセージの帯と名前の行の上へ逃がすので、
            //! その 2 つとも重ならない。右の段も同じ高さにそろえる。
            const RectPx none{};
            const RectPx *const avoid[] = { &layout.bottom_bar, &layout.status_col, item.left ? &layout.message_bar : &none,
                item.left ? &layout.hud : &none };
            for (const RectPx *rect : avoid) {
                avoided_overlap += rect_overlap_area(vis, *rect);
            }
            one_row_ok = one_row_ok && (item.row == 0) && (item.icon.y <= row0_y) && (item.icon.y == placed.items.front().icon.y) && (item.icon.w == placed.icon_px)
                && (item.icon.h == placed.icon_px);
            icon_ids_ok = icon_ids_ok && (item.icon_id == status_icon_index(effect.key));
            for (std::size_t b = a + 1; b < placed.items.size(); ++b) {
                const StatusIconPlacement &other = placed.items[b];
                if (item.rotating || other.rotating) {
                    // 巡回の輪は、隣どうしがちょうど 1 個ぶん（pitch）離れている（重ならない）
                    if (item.left == other.left) {
                        apart_ok = apart_ok && (std::fabs(true_x(item) - true_x(other)) >= (item.cell.w - 0.01));
                    }
                } else if (rect_overlap_area(item.cell, other.cell) > 0) {
                    apart_ok = false;
                }
            }
        }
        //! 玉に近い側から外へ（止まって並ぶ側だけ。巡回する側は輪の検査で見る）
        for (const bool left_side : { true, false }) {
            long long last = -1;
            for (const StatusIconPlacement &item : placed.items) {
                if ((item.left != left_side) || item.rotating) {
                    continue;
                }
                const long long distance = left_side ? (orb.x - (item.cell.x + item.cell.w)) : (item.cell.x - (orb.x + orb.w));
                order_ok = order_ok && (distance > last);
                last = distance;
            }
        }
        //! 左は 良い → 待っている → 特殊 の順（フレームの順の中で kind が増える）。止まって並ぶ側だけ。
        {
            int last_kind = -1;
            for (const StatusIconPlacement &item : placed.items) {
                if (!item.left || item.rotating) {
                    continue;
                }
                const int kind_rank = (base.status_effects[static_cast<std::size_t>(item.effect_index)].kind == StatusEffectKind::Good) ? 0
                    : (base.status_effects[static_cast<std::size_t>(item.effect_index)].kind == StatusEffectKind::Waiting) ? 1 : 2;
                order_ok = order_ok && (kind_rank >= last_kind);
                last_kind = kind_rank;
            }
        }
        // 入りきる側は止まって全部並び、入りきらない側は輪になって一部だけ見える
        const auto ring_ok = [&](const StatusRing &ring, int total, int placed_count) {
            if (!ring.rotating) {
                return (ring.count == total) && (placed_count == total);
            }
            return (ring.count == total) && (ring.capacity >= 1) && (ring.capacity < total)
                && (placed_count >= ring.capacity) && (placed_count <= (ring.capacity + 1));
        };
        check(placed.dropped == 0 && ring_ok(placed.ring_left, left_total, left_count)
                && ring_ok(placed.ring_right, right_total, right_count),
            tag + ": 玉の横のアイコンは、入りきるなら全部、入りきらないなら輪の一部が置かれる（左 "
                + std::to_string(left_count) + "/" + std::to_string(left_total) + (placed.ring_left.rotating ? " 巡回" : " 静止")
                + " / 右 " + std::to_string(right_count) + "/" + std::to_string(right_total)
                + (placed.ring_right.rotating ? " 巡回" : " 静止") + "）");
        //! 入りきる 2560 では回さず、狭い 1280 の左は回る（この合成フレームの左は 9 個）
        check(placed.ring_left.rotating == (w < 2000), tag + ": 巡回は入りきらないときだけ（左は " + (placed.ring_left.rotating ? "巡回" : "静止") + "）");
        check(kinds_ok, tag + ": 良い・待っている・特殊は左、悪いは右");
        check(sides_ok, tag + ": 左のアイコンは玉の左、右は玉の右（端は地図の左右 1 マス内）");
        check(inside_ok, tag + ": アイコンは地図と画面の中");
        check(apart_ok, tag + ": アイコンどうしが重ならない");
        check(avoided_overlap == 0, tag + ": アイコンは最下段・状態列と重ならず、左の段はメッセージの帯と名前の行とも重ならない");
        check(one_row_ok, tag + ": 玉の横は 1 行だけ（左右の段は同じ高さ。玉の上から 42% か、メッセージの帯と名前の行を避けてそれより上）");
        check(order_ok, tag + ": 玉に近い側から外へ並ぶ");
        check(icon_ids_ok, tag + ": 知らない名前は 0 番、知っている名前は表の番号");
        std::fprintf(stderr, "    （%s: 玉 %d,%d %dx%d / アイコン %dpx / 基準の字 %dpx / 左 %s 右 %s）\n", tag.c_str(), orb.x, orb.y, orb.w, orb.h,
            placed.icon_px, status_text_reference_px(text, placed.icon_px), placed.ring_left.rotating ? "巡回" : "静止",
            placed.ring_right.rotating ? "巡回" : "静止");

        // 描いて書き出す（時刻を固定）
        HudState state;
        orb_hud_update(state.orb_anim, state.orb_params, 1.f, 1.f, 0u);
        advance(state, ratio_hp(base), ratio_mp(base), 0u, 3000u);
        const std::vector<unsigned char> shot = render(target, base, state, 3000u);
        check(drain_gl_errors().empty(), tag + ": 描いて GL エラーが出ない");
        check(save_rgba_png(out_path(w, h, ""), shot.data(), w, h), tag + ": 画像を書き出せた " + out_path(w, h, ""));

        // 同じ状態・同じ時刻は同じ絵（泡の乱数も時計に頼らない）
        {
            const std::vector<unsigned char> again = render(target, base, state, 3000u);
            check(count_changed_pixels(shot, again, 0) == 0, tag + ": 同じ状態・同じ時刻なら同じ絵");
        }

        // ---- 玉の絵の中身（水面の高さ・玉が無いコア・ゲーム前・名札・波の切り替え） ----
        const double sx = static_cast<double>(orb.w) / static_cast<double>(kOrbDesignW);
        const double sy = static_cast<double>(orb.h) / static_cast<double>(kOrbDesignH);
        const auto channel = [&](const std::vector<unsigned char> &img, int x, int y, int c) {
            return static_cast<int>(img[(((static_cast<std::size_t>(h - 1 - y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)) * 4) + static_cast<std::size_t>(c)]);
        };
        const auto liquid_at = [&](const std::vector<unsigned char> &img, int x, int y, bool red) {
            const int r = channel(img, x, y, 0);
            const int g = channel(img, x, y, 1);
            const int b = channel(img, x, y, 2);
            return red ? ((r >= 100) && ((r * 10) >= (g * 17)) && ((r * 10) >= (b * 17)))
                       : ((b >= 100) && ((b * 10) >= (r * 15)) && ((b * 10) >= (g * 12)));
        };
        //! 玉の真ん中の帯（中心から ±30）の 11 本の縦線で、液体の色が上から数えて最初に出る行。
        const auto surface_row = [&](const std::vector<unsigned char> &img, double cx_design, bool red) {
            const int y0 = orb.y + static_cast<int>(std::lround(60.0 * sy));
            const int y1 = orb.y + static_cast<int>(std::lround(196.0 * sy));
            for (int y = y0; y < y1; ++y) {
                int hit = 0;
                for (int k = -5; k <= 5; ++k) {
                    const int x = orb.x + static_cast<int>(std::lround((cx_design + (k * 6.0)) * sx));
                    hit += liquid_at(img, x, y, red) ? 1 : 0;
                }
                if (hit >= 8) {
                    return y;
                }
            }
            return -1;
        };
        //! 玉の外接の箱の中で液体の色の画素を数える（2 画素おき）。
        const auto liquid_count = [&](const std::vector<unsigned char> &img, double cx_design, bool red) {
            int count = 0;
            const int x0 = orb.x + static_cast<int>(std::lround((cx_design - kOrbRadius) * sx));
            const int x1 = orb.x + static_cast<int>(std::lround((cx_design + kOrbRadius) * sx));
            const int y0 = orb.y + static_cast<int>(std::lround((kOrbCenterY - kOrbRadius) * sy));
            const int y1 = orb.y + static_cast<int>(std::lround((kOrbCenterY + kOrbRadius) * sy));
            for (int y = y0; y < y1; y += 2) {
                for (int x = x0; x < x1; x += 2) {
                    count += liquid_at(img, x, y, red) ? 1 : 0;
                }
            }
            return count;
        };
        const auto expected_row = [&](float ratio) {
            return static_cast<double>(orb.y) + ((kOrbCenterY + kOrbRadius - (static_cast<double>(ratio) * 2.0 * kOrbRadius)) * sy);
        };
        const double tolerance = ((2.6 * 1.3) + 3.0) * sy + 3.0;
        {
            const int hp_row = surface_row(shot, kOrbHpCenterX, true);
            const int mp_row = surface_row(shot, kOrbMpCenterX, false);
            const double hp_want = expected_row(ratio_hp(base));
            const double mp_want = expected_row(ratio_mp(base));
            check((hp_row >= 0) && (std::fabs(static_cast<double>(hp_row) - hp_want) <= tolerance),
                tag + ": HP の水面の高さ（見つけた行 " + std::to_string(hp_row) + " / 期待 " + std::to_string(static_cast<int>(hp_want))
                    + " ±" + std::to_string(static_cast<int>(tolerance)) + "）");
            check((mp_row >= 0) && (std::fabs(static_cast<double>(mp_row) - mp_want) <= tolerance),
                tag + ": MP の水面の高さ（見つけた行 " + std::to_string(mp_row) + " / 期待 " + std::to_string(static_cast<int>(mp_want))
                    + " ±" + std::to_string(static_cast<int>(tolerance)) + "）");
        }
        {
            //! MP の玉が無いコア（`sp_max <= 0`）: 青い液体を描かない。赤は残る。
            GameFrame no_mp = base;
            no_mp.hud.sp = 0;
            no_mp.hud.sp_max = 0;
            HudState fresh;
            const std::vector<unsigned char> shot_no_mp = render(target, no_mp, fresh, 3000u);
            check((liquid_count(shot, kOrbMpCenterX, false) > 200) && (liquid_count(shot_no_mp, kOrbMpCenterX, false) == 0)
                    && (liquid_count(shot_no_mp, kOrbHpCenterX, true) > 200),
                tag + ": sp_max が 0 以下のコアでは MP の液体を描かない（玉の枠と HP は残る）");
            //! ゲーム前（`hp_max <= 0`）: 玉は出ない。
            GameFrame pre = base;
            pre.hud.hp = 0;
            pre.hud.hp_max = 0;
            HudState fresh2;
            const std::vector<unsigned char> shot_pre = render(target, pre, fresh2, 3000u);
            check((liquid_count(shot_pre, kOrbHpCenterX, true) == 0) && (liquid_count(shot_pre, kOrbMpCenterX, false) == 0),
                tag + ": hp_max が 0 以下（ゲーム前）では玉を描かない");
        }
        {
            //! 名札はコアが決める。空は `SP` と 1 画素も違わず、変えると絵が変わる。
            const auto with_label = [&](const std::string &label) {
                GameFrame f = base;
                f.hud.sp_label = label;
                HudState fresh;
                return render(target, f, fresh, 3000u);
            };
            const std::vector<unsigned char> empty_label = with_label(std::string());
            const std::vector<unsigned char> spelled = with_label("SP");
            const std::vector<unsigned char> voice = with_label("Voice");
            check(count_changed_pixels(empty_label, spelled, w, h, orb) == 0, tag + ": 名札が空のときの絵は `SP` と同じ");
            check(count_changed_pixels(empty_label, voice, w, h, orb) > 0, tag + ": 名札を変えると絵が変わる（`Voice`）");
        }
        {
            //! 波。打つ設定なら時刻で絵が変わり、止めたら（残量が動かない限り）変わらない。
            HudState wave_on;
            HudState wave_off;
            wave_off.orb_params.wave_enabled = false;
            for (HudState *st : { &wave_on, &wave_off }) {
                orb_hud_update(st->orb_anim, st->orb_params, ratio_hp(base), ratio_mp(base), 0u);
                advance(*st, ratio_hp(base), ratio_mp(base), 0u, 3000u);
            }
            const std::vector<unsigned char> on_a = render(target, base, wave_on, 3000u);
            const std::vector<unsigned char> off_a = render(target, base, wave_off, 3000u);
            advance(wave_on, ratio_hp(base), ratio_mp(base), 3000u, 3400u);
            advance(wave_off, ratio_hp(base), ratio_mp(base), 3000u, 3400u);
            const std::vector<unsigned char> on_b = render(target, base, wave_on, 3400u);
            const std::vector<unsigned char> off_b = render(target, base, wave_off, 3400u);
            check(count_changed_pixels(on_a, on_b, w, h, orb) > 0, tag + ": 波あり: 時刻が進むと水面の絵が変わる");
            check(count_changed_pixels(off_a, off_b, w, h, orb) == 0, tag + ": 波を止める: 時刻が進んでも絵が変わらない（平ら）");
            check(count_changed_pixels(on_a, off_a, w, h, orb) > 0, tag + ": 波あり・波なしで水面の絵が違う");
        }
        release_status_hud_target(target);
    }

    // ---- 常時の列・名前の縮め・名札（作り 3 つ x 大きさ 3 つ x 状態列の左右） ----
    {
        int permanent_total = 0;
        for (const StatusEffectView &effect : base.status_effects) {
            permanent_total += ((effect.kind == StatusEffectKind::Permanent) || (effect.kind == StatusEffectKind::PermanentBad)) ? 1 : 0;
        }
        const int min_px = std::max(6, static_cast<int>(std::ceil((static_cast<double>(text.base_px()) * static_cast<double>(kStatusTextMinScale)) - 1e-6)));
        //! 表示上の幅（全角 1 文字 = 2、半角 = 1）。検査側の数え方（実装とは別に書く）。
        const auto columns_of = [](const std::string &label) {
            int columns = 0;
            for (std::size_t i = 0; i < label.size();) {
                const unsigned char c = static_cast<unsigned char>(label[i]);
                const std::size_t len = (c >= 0xF0u) ? 4u : (c >= 0xE0u) ? 3u : (c >= 0xC0u) ? 2u : 1u;
                columns += (len == 1u) ? 1 : 2;
                i += len;
            }
            return columns;
        };
        //! 名前の字の大きさの規則を 1 個ずつ数える。
        //! 1〜4 文字は基準の大きさ（icon の幅に全角 4 文字が収まるいちばん大きい字。普通の字は超えない）。
        //! 5 文字以上で基準でも収まらないものだけ縮め、下限でも収まらないものだけ幅が広がる。
        const auto name_rule_ok = [&](const FittedText &fit, int icon_px, const std::string &label) {
            if (label.empty()) {
                return true;
            }
            const int ref = status_text_reference_px(text, icon_px);
            const int shown_px = text.sized_px(fit.scale);
            const bool ref_is_biggest = (ref == text.base_px()) || (text.measure_sized(kStatusTextReferenceString, static_cast<float>(ref + 1) / static_cast<float>(text.base_px())) > icon_px);
            if (!ref_is_biggest || (text.measure_sized(kStatusTextReferenceString, static_cast<float>(ref) / static_cast<float>(text.base_px())) > icon_px)
                || (ref > text.base_px())) {
                return false;
            }
            if (shown_px > ref) {
                return false;
            }
            if (columns_of(label) <= 8) {
                return (shown_px == ref) && (fit.width <= icon_px) && !fit.overflow; // 短い名前はすべて基準の大きさ
            }
            if (fit.overflow) {
                return (shown_px == std::min(ref, min_px)) && (fit.width > icon_px); // 下限まで縮めても収まらない
            }
            // 収まった: アイコンの幅以内で、1 つ大きい字では収まらなかった（または基準の大きさで収まった）
            const bool fits = fit.width <= icon_px;
            const bool biggest = (shown_px == ref)
                || (text.measure_sized(label, static_cast<float>(shown_px + 1) / static_cast<float>(text.base_px())) > icon_px);
            return fits && biggest && (shown_px >= std::min(ref, min_px));
        };
        /*
         * 名前（最大 2 行）の規則を 1 個ずつ数える（実装の `fit_status_name` とは別に書く）。
         * - 基準の大きさで 1 行に収まる・全角 4 文字以内・折る位置が無い、のどれかなら 1 行（`name_rule_ok` の規則）。
         * - それ以外は 2 行。折る位置は `status_name_break_points` の中にあり、両方の行が収まるいちばん大きい字
         *   （基準の大きさを上限に、1 画素大きくすると収まらない）、長いほうの行がいちばん短い位置を選ぶ。
         * - 下限でも収まらないものだけ `overflow`（下限の大きさの 2 行・長いほうの行が幅を超える）。
         */
        const auto name_wrap_ok = [&](const FittedText &fit, int icon_px, const std::string &label) {
            if (label.empty()) {
                return fit.lines == 1;
            }
            const int ref = status_text_reference_px(text, icon_px);
            const float ref_scale = static_cast<float>(ref) / static_cast<float>(text.base_px());
            const std::vector<StatusNameBreak> breaks = status_name_break_points(label);
            const bool one_line_at_ref = (text.measure_sized(label, ref_scale) <= icon_px) || (columns_of(label) <= 8);
            if (one_line_at_ref || breaks.empty()) {
                return (fit.lines == 1) && name_rule_ok(fit, icon_px, label) && (status_name_line(label, fit, 0) == label)
                    && status_name_line(label, fit, 1).empty();
            }
            if (fit.lines != 2) {
                return false;
            }
            const int shown_px = text.sized_px(fit.scale);
            if (shown_px > ref) {
                return false;
            }
            const std::string first = std::string(status_name_line(label, fit, 0));
            const std::string second = std::string(status_name_line(label, fit, 1));
            bool is_candidate = false;
            for (const StatusNameBreak &b : breaks) {
                is_candidate = is_candidate || ((b.first_end == fit.break_end) && (b.second_begin == fit.break_begin));
            }
            const int w0 = text.measure_sized(first, fit.scale);
            const int w1 = text.measure_sized(second, fit.scale);
            if (!is_candidate || first.empty() || second.empty() || (fit.line_width[0] != w0) || (fit.line_width[1] != w1)
                || (fit.width != std::max(w0, w1))) {
                return false;
            }
            //! 長いほうの行がいちばん短い位置か（どの候補もこれより短くならない）。
            const auto best_widest = [&](int px) {
                const float s = static_cast<float>(px) / static_cast<float>(text.base_px());
                int best = 1 << 30;
                for (const StatusNameBreak &b : breaks) {
                    best = std::min(best, std::max(text.measure_sized(label.substr(0, b.first_end), s), text.measure_sized(label.substr(b.second_begin), s)));
                }
                return best;
            };
            if (fit.width != best_widest(shown_px)) {
                return false;
            }
            if (fit.overflow) {
                return (shown_px == std::min(ref, min_px)) && (fit.width > icon_px);
            }
            return (fit.width <= icon_px) && (shown_px >= std::min(ref, min_px))
                && ((shown_px == ref) || (best_widest(shown_px + 1) > icon_px));
        };
        std::vector<std::string> overflow_names;
        for (const LayoutMode mode : { LayoutMode::Full, LayoutMode::Hybrid, LayoutMode::Split }) {
            for (const std::array<int, 2> &size : { std::array<int, 2>{ 2560, 1080 }, std::array<int, 2>{ 1920, 1080 }, std::array<int, 2>{ 1280, 720 } }) {
                for (const int side : { 0, 1 }) {
                    if ((mode == LayoutMode::Full) && (side == 1)) {
                        continue; // Full は状態列が無い
                    }
                    const int w = size[0];
                    const int h = size[1];
                    set_status_col_side(side);
                    const UiLayout layout = UiLayout::compute(mode, w, h, text.cell_w(), text.cell_h(), false);
                    set_status_col_side(0);
                    const std::string tag = std::string(layout_mode_name(mode)) + " " + std::to_string(w) + "x" + std::to_string(h)
                        + ((side == 1) ? " 状態列が右" : "");
                    const RectPx &orb = layout.orb;
                    if (orb.empty()) {
                        check(false, tag + ": 玉が置かれていない");
                        continue;
                    }
                    const RectPx window_rect{ 0, 0, w, h };
                    const PermanentIconLayout perm = layout_permanent_icons(text, layout, base);
                    const StatusIconLayout placed = layout_status_icons(text, layout, base);
                    const int gap = std::max(4, perm.icon_px / 8);
                    const int ch = text.cell_h();
                    // 数・大きさ
                    {
                        const bool all_placed = (static_cast<int>(perm.items.size()) == permanent_total) && (perm.dropped == 0);
                        //! 置けなかった分があるのは、列をもう増やせないときだけ（状態列が右で 1280x720 のような狭い所）
                        const int next_w = perm.icon_px + gap;
                        const bool exhausted = !perm.extent.empty()
                            && (static_cast<int>(perm.items.size()) + perm.dropped == permanent_total)
                            && (perm.grows_left ? ((perm.extent.x - next_w) < (orb.x + orb.w + gap))
                                                : ((perm.extent.x + perm.extent.w + next_w) > (orb.x - gap)));
                        check(all_placed || ((side == 1) && exhausted),
                            tag + ": 常時の効果 " + std::to_string(permanent_total) + " 個がすべて置かれる（置けた "
                                + std::to_string(perm.items.size()) + " / 置けなかった " + std::to_string(perm.dropped) + " / 列 "
                                + std::to_string(perm.columns) + (all_placed ? "" : "。列を増やせる横幅が尽きた") + "）");
                    }
                    check(perm.icon_px == std::max(24, static_cast<int>(std::lround(status_icon_size(orb) * 0.8))),
                        tag + ": 常時の列のアイコンは玉の横の 0.8 倍・下限 24（" + std::to_string(perm.icon_px) + "）");

                    // 置き場（重ならない・範囲の中）
                    bool inside_ok = true;
                    bool avoid_ok = true;
                    bool apart_ok = true;
                    bool orb_side_apart_ok = true;
                    long long orb_overlap = 0;
                    for (std::size_t a = 0; a < perm.items.size(); ++a) {
                        const PermanentIconPlacement &item = perm.items[a];
                        const long long area = static_cast<long long>(item.cell.w) * static_cast<long long>(item.cell.h);
                        inside_ok = inside_ok && (rect_overlap_area(item.cell, layout.scene) == area)
                            && (rect_overlap_area(item.cell, window_rect) == area)
                            && (item.cell.y >= perm.top) && ((item.cell.y + item.cell.h) <= perm.bottom);
                        const RectPx *const avoid[] = { &layout.status_col, &layout.bottom_bar, &layout.message_bar,
                            &layout.prompt_bar, &layout.minimap, &layout.hud };
                        for (const RectPx *rect : avoid) {
                            avoid_ok = avoid_ok && (rect_overlap_area(item.cell, *rect) == 0);
                        }
                        orb_overlap += rect_overlap_area(item.cell, orb);
                        for (const StatusIconPlacement &other : placed.items) {
                            orb_side_apart_ok = orb_side_apart_ok && (rect_overlap_area(item.cell, other.cell) == 0);
                        }
                        for (std::size_t b = a + 1; b < perm.items.size(); ++b) {
                            apart_ok = apart_ok && (rect_overlap_area(item.cell, perm.items[b].cell) == 0);
                        }
                    }
                    check(inside_ok, tag + ": 常時の列は地図と画面の中・縦の範囲の中");
                    check(avoid_ok, tag + ": 常時の列は状態列・最下段・メッセージ・プロンプト・ミニマップ・名前の行と重ならない");
                    check(orb_overlap == 0, tag + ": 常時の列は玉の矩形と重ならない");
                    check(apart_ok, tag + ": 常時の列のアイコンどうしが重ならない");
                    check(orb_side_apart_ok, tag + ": 常時の列は玉の横のアイコンと重ならない");

                    // 状態列の横・外へ伸びる向き
                    {
                        bool side_ok = true;
                        for (const PermanentIconPlacement &item : perm.items) {
                            if (item.column == 0) {
                                if (side == 1) {
                                    side_ok = side_ok && (item.cell.x + item.cell.w <= layout.status_col.x);
                                } else if (!layout.status_col.empty()) {
                                    side_ok = side_ok && (item.cell.x >= layout.status_col.x + layout.status_col.w);
                                } else {
                                    side_ok = side_ok && (item.cell.x >= layout.scene.x + layout.cell_w);
                                }
                            }
                        }
                        //! 列の番号が増えるほど、右置きでは左へ・左置きでは右へ離れる
                        for (const PermanentIconPlacement &a : perm.items) {
                            for (const PermanentIconPlacement &b : perm.items) {
                                if (b.column > a.column) {
                                    side_ok = side_ok && ((side == 1) ? (b.cell.x < a.cell.x) : (b.cell.x > a.cell.x));
                                }
                            }
                        }
                        check(side_ok, tag + ": 常時の列は状態列の" + ((side == 1) ? "左" : (layout.status_col.empty() ? "無い作りでは地図の左端＋1 マス" : "右"))
                                + "から、外へ向かって列が増える");
                    }

                    // 良い効果 → 呪い・フレームの順・縦に並ぶ
                    {
                        bool order_ok = true;
                        bool column_ok = true;
                        long long last_key = -1;
                        bool seen_bad = false;
                        for (const PermanentIconPlacement &item : perm.items) {
                            const long long key = (static_cast<long long>(item.column) * 100000LL) + item.cell.y;
                            order_ok = order_ok && (key > last_key) && !(seen_bad && !item.bad);
                            last_key = key;
                            seen_bad = seen_bad || item.bad;
                            for (const PermanentIconPlacement &other : perm.items) {
                                if ((other.column == item.column) && !item.bad && other.bad) {
                                    column_ok = column_ok && ((item.cell.y + item.cell.h) <= other.cell.y); // 同じ列なら良い効果が呪いより上
                                }
                            }
                        }
                        check(order_ok, tag + ": 良い効果 → 呪いの順で、中はフレームの順（上から下・列は外へ）");
                        check(column_ok, tag + ": 同じ列なら良い効果が呪いより上");
                        //! 縦の並びの幅は 1 本ごとにそろう（同じ列の x が同じ）
                        bool aligned = true;
                        for (const PermanentIconPlacement &a : perm.items) {
                            for (const PermanentIconPlacement &b : perm.items) {
                                aligned = aligned && ((a.column != b.column) || ((a.cell.x == b.cell.x) && (a.cell.w == b.cell.w)));
                            }
                        }
                        check(aligned, tag + ": 同じ列のアイコンは縦にそろっている");
                    }

                    // あふれたら右（外）へ
                    {
                        bool overflow_ok = true;
                        for (int c = 1; c < perm.columns; ++c) {
                            int last_bottom = 0;
                            for (const PermanentIconPlacement &item : perm.items) {
                                if (item.column == (c - 1)) {
                                    last_bottom = std::max(last_bottom, item.cell.y + item.cell.h);
                                }
                            }
                            bool next_is_bad = false; //!< 次の列の先頭が呪いなら、良い効果との間のすき間ぶんも要る
                            int first_h = 0; //!< 次の列の先頭の実際の高さ（名前の行数で変わる）
                            for (const PermanentIconPlacement &item : perm.items) {
                                if ((item.column == c) && ((item.cell.y == perm.column_top[static_cast<std::size_t>(c)]))) {
                                    next_is_bad = item.bad;
                                    first_h = item.cell.h;
                                }
                            }
                            const int need = first_h + (next_is_bad ? (perm.icon_px / 2) : 0);
                            overflow_ok = overflow_ok && ((last_bottom + gap + need) > perm.column_bottom[static_cast<std::size_t>(c - 1)]); // 前の列にはもう入らなかった
                        }
                        //! 全部を 1 列に積んだ高さ（1 個ずつ実際の高さ + すき間。良い効果と呪いの間のすき間は数えない = 下限）
                        int stacked = 0;
                        for (const PermanentIconPlacement &item : perm.items) {
                            stacked += item.cell.h + gap;
                        }
                        stacked -= gap;
                        //! 列が 2 以上になるのは、1 列に入りきらないとき（全部が 1 列に入るなら 1 列）
                        const bool needs_more = (stacked > (perm.bottom - perm.top)) || (perm.dropped > 0);
                        check(overflow_ok && (perm.columns >= 1) && (needs_more || (perm.columns == 1)),
                            tag + ": 列があふれたら次の列へ（前の列が満ちてから。列 " + std::to_string(perm.columns) + " / 積んだ高さ "
                                + std::to_string(stacked) + " / 範囲 " + std::to_string(perm.bottom - perm.top) + "）");
                    }

                    // 玉の横のアイコン（1 行だけ）は常時の列の下で、重ならない
                    {
                        bool below_ok = true;
                        if (!perm.extent.empty()) {
                            for (const StatusIconPlacement &item : placed.items) {
                                below_ok = below_ok && (item.cell.y >= (perm.extent.y + perm.extent.h));
                            }
                        }
                        check(below_ok, tag + ": 玉の横のアイコン（1 行）は常時の列より下にある");
                    }

                    // 名前は字を縮めてアイコンの幅に収める
                    {
                        bool perm_names_ok = true;
                        bool perm_width_ok = true;
                        for (const PermanentIconPlacement &item : perm.items) {
                            const std::string &label = base.status_effects[static_cast<std::size_t>(item.effect_index)].label_utf8;
                            perm_names_ok = perm_names_ok && name_wrap_ok(item.name, perm.icon_px, label)
                                && (item.name_band.w >= item.name.width)
                                && (item.name_band.h == (item.name.lines * perm.name_line_h))
                                && (item.name_band.y == (item.icon.y + item.icon.h))
                                && (item.cell.h == (perm.icon_px + item.name_band.h));
                            bool column_overflows = false;
                            for (const PermanentIconPlacement &other : perm.items) {
                                column_overflows = column_overflows || ((other.column == item.column) && other.name.overflow);
                            }
                            perm_width_ok = perm_width_ok
                                && (column_overflows ? (item.cell.w > (perm.icon_px + gap)) : (item.cell.w == (perm.icon_px + gap)));
                            if (item.name.overflow && (mode == LayoutMode::Hybrid) && (side == 0)) {
                                overflow_names.push_back(std::to_string(w) + "x" + std::to_string(h) + ":" + label);
                            }
                        }
                        bool side_names_ok = true;
                        bool width_ok = true;
                        for (const StatusIconPlacement &item : placed.items) {
                            const StatusEffectView &effect = base.status_effects[static_cast<std::size_t>(item.effect_index)];
                            side_names_ok = side_names_ok && name_wrap_ok(item.name, placed.icon_px, effect.label_utf8)
                                && name_rule_ok(item.amount, placed.icon_px, effect.amount_utf8) && (item.amount.lines == 1)
                                && (item.name_band.w >= item.name.width)
                                && (item.name_band.h == (item.name.lines * placed.name_line_h))
                                && (item.name_band.y == (item.icon.y + item.icon.h))
                                && ((item.name_band.y + item.name_band.h) <= (item.cell.y + item.cell.h));
                            const int side_gap = std::max(4, placed.icon_px / 8);
                            const StatusRing &ring = item.left ? placed.ring_left : placed.ring_right;
                            if (item.rotating) {
                                width_ok = width_ok && (item.cell.w == ring.pitch) && (ring.pitch >= (placed.icon_px + side_gap)); // 輪は全員同じ幅
                            } else if (!item.name.overflow && !item.amount.overflow) {
                                width_ok = width_ok && (item.cell.w == (placed.icon_px + side_gap)); // 基本は アイコンの幅 + すき間
                            } else {
                                width_ok = width_ok && (item.cell.w > (placed.icon_px + side_gap)); // 下限でも収まらないものだけ広がる
                            }
                            if (item.name.overflow && (mode == LayoutMode::Hybrid) && (side == 0)) {
                                overflow_names.push_back(std::to_string(w) + "x" + std::to_string(h) + ":" + effect.label_utf8);
                            }
                        }
                        check(perm_names_ok, tag + ": 常時の列の名前は、基準の大きさで最大 2 行に折り、長いものだけ縮める（下限 " + std::to_string(min_px) + "px）。帯は実際の行数ぶん");
                        check(perm_width_ok, tag + ": 常時の列の 1 個の幅 = アイコンの幅 + すき間（収まらない名前のある列だけ広い）");
                        check(side_names_ok, tag + ": 玉の横の名前（最大 2 行）・効果量（1 行）も、基準の大きさ（長いものだけ縮める）でアイコンの幅に収める。帯は実際の行数ぶん");
                        check(width_ok, tag + ": 玉の横の 1 個の幅 = アイコンの幅 + すき間（収まらないものだけ広い。輪は全員同じ幅）");
                    }

                    // 玉の名札は、とげの先（設計の単位で 43）より上にある
                    {
                        const double sy = static_cast<double>(orb.h) / static_cast<double>(kOrbDesignH);
                        const double sx = static_cast<double>(orb.w) / static_cast<double>(kOrbDesignW);
                        const int label_h = text.cell_h_sized(kOrbLabelScale);
                        const int label_bottom = orb.y + static_cast<int>(std::lround(kOrbLabelBottomY * sy));
                        const int spike_tip = orb.y + static_cast<int>(std::lround(43.0 * sy));
                        const int label_w = text.measure_sized("HP", kOrbLabelScale);
                        check((label_bottom <= spike_tip) && (label_h >= ch) && ((label_bottom - label_h) >= layout.scene.y)
                                && (label_w <= static_cast<int>(std::lround(kOrbRadius * 2.0 * sx))),
                            tag + ": 玉の名札は数字の帯の字より大きく、とげの先より上にある（高さ " + std::to_string(label_h) + " / 下端 "
                                + std::to_string(label_bottom - orb.y) + " <= とげ " + std::to_string(spike_tip - orb.y) + "）");
                    }
                }
            }
        }
        {
            std::sort(overflow_names.begin(), overflow_names.end());
            overflow_names.erase(std::unique(overflow_names.begin(), overflow_names.end()), overflow_names.end());
            std::string joined;
            for (const std::string &n : overflow_names) {
                joined += (joined.empty() ? "" : "、") + n;
            }
            std::fprintf(stderr, "    （下限 %dpx でも収まらなかった名前: %s）\n", min_px, joined.empty() ? "なし" : joined.c_str());
        }
        //! 常時の効果を送らないコア（kind 4/5 が無い）では何も置かない。
        {
            GameFrame none = base;
            none.status_effects.erase(std::remove_if(none.status_effects.begin(), none.status_effects.end(),
                                          [](const StatusEffectView &e) {
                                              return (e.kind == StatusEffectKind::Permanent) || (e.kind == StatusEffectKind::PermanentBad);
                                          }),
                none.status_effects.end());
            const UiLayout layout = make_layout(2560, 1080);
            const PermanentIconLayout empty = layout_permanent_icons(text, layout, none);
            check(empty.items.empty() && empty.dropped == 0 && empty.extent.empty(), "kind 4/5 が無いコアでは、常時の列は何も置かれない");
        }
        //! 縮めの確かめ: 同じ名前を、収まる幅・収まらない幅・極端に狭い幅で。
        {
            const FittedText fits = fit_status_text(text, "ヒーロー", 200);
            const FittedText shrinks = fit_status_text(text, "ドラゴン感知", text.measure("ドラゴン感知") - 8);
            const FittedText floor_case = fit_status_text(text, "ドラゴン感知", 10);
            check((fits.scale == 1.f) && !fits.overflow, "字の縮め: 収まるときは普通の大きさ");
            check((shrinks.scale < 1.f) && !shrinks.overflow && (shrinks.width <= (text.measure("ドラゴン感知") - 8)),
                "字の縮め: 少し足りないときは縮めて収める（" + std::to_string(text.sized_px(shrinks.scale)) + "px）");
            const int floor_ref = status_text_reference_px(text, 10);
            check(floor_case.overflow && (text.sized_px(floor_case.scale) == std::min(floor_ref, min_px)) && (floor_case.width > 10),
                "字の縮め: 下限（普通の 0.55 倍 = " + std::to_string(min_px) + "px。基準がそれより小さければ基準 = " + std::to_string(floor_ref)
                    + "px）でも収まらないものは下限のまま");
        }
        //! 名前の折り方（最大 2 行）。折ってよい位置の一覧を、文字列で確かめる。
        {
            struct BreakCase {
                const char *name;
                std::vector<std::string> splits; //!< 「1 行目|2 行目」の並び（左から）
            };
            const BreakCase cases[] = {
                { "ドラゴンの感知", { "ドラゴン|の感知", "ドラゴンの|感知", "ドラゴンの感|知" } },
                { "弱い呪い増殖", { "弱い|呪い増殖", "弱い呪い|増殖", "弱い呪い増|殖" } },
                { "テレパシー", {} },
                { "ヒーロー", {} },
                { "経験値減少", { "経|験値減少", "経験|値減少", "経験値|減少", "経験値減|少" } },
                { "耐火炎", { "耐|火炎", "耐火|炎" } },
                { "加速", { "加|速" } },
                { "火のオーラ", { "火の|オーラ" } },
                { "ひどい切り傷", { "ひどい|切り傷", "ひどい切り|傷" } },
                { "ガーディアンの目", { "ガーディアン|の目", "ガーディアンの|目" } },
                { "毒（軽）", { "毒|（軽）" } },
                { "HP回復", { "HP回|復" } },
                { "毒 軽", {} },
                { "の", {} },
                { "Sense Dragons", { "Sense|Dragons" } },
                { "Hallucination", {} },
                { "Resist Fire", { "Resist|Fire" } },
                { "Word of Recall", { "Word|of Recall", "Word of|Recall" } },
                { "a  b", { "a|b" } },
                { " ab", {} },
                { "ab ", {} },
                { "ab", {} },
                { "", {} },
            };
            bool breaks_ok = true;
            std::string first_bad;
            for (const BreakCase &c : cases) {
                const std::string name = c.name;
                std::vector<std::string> got;
                for (const StatusNameBreak &b : status_name_break_points(name)) {
                    got.push_back(name.substr(0, b.first_end) + "|" + name.substr(b.second_begin));
                }
                if (got != c.splits) {
                    breaks_ok = false;
                    if (first_bad.empty()) {
                        first_bad = name;
                    }
                }
            }
            check(breaks_ok, "名前の折る位置: 英語は空白だけ・日本語は「の」の後ろと字の種類の境目だけ（"
                    + std::to_string(sizeof(cases) / sizeof(cases[0])) + " 例" + (breaks_ok ? "" : "。違った例: " + first_bad) + "）");

            //! 例の名前の折れ方（アイコンの幅 3 とおり）。
            struct FitCase {
                const char *name;
                const char *first;
                const char *second; //!< 空なら 1 行
            };
            const FitCase fits[] = {
                { "ドラゴンの感知", "ドラゴン", "の感知" }, { "弱い呪い増殖", "弱い", "呪い増殖" }, { "テレパシー", "テレパシー", "" },
                { "Sense Dragons", "Sense", "Dragons" }, { "Hallucination", "Hallucination", "" }, { "経験値減少", "経験", "値減少" },
                { "耐火炎", "耐火炎", "" }, { "ヒーロー", "ヒーロー", "" }, { "Resist Fire", "Resist", "Fire" },
            };
            for (const int icon : { 64, 43, 34 }) {
                bool fits_ok = true;
                bool rule_ok = true;
                std::string detail;
                for (const FitCase &c : fits) {
                    const std::string name = c.name;
                    const FittedText f = fit_status_name(text, name, icon);
                    const bool two = c.second[0] != '\0';
                    const bool lines_ok = (f.lines == (two ? 2 : 1)) && (status_name_line(name, f, 0) == std::string_view(c.first))
                        && (status_name_line(name, f, 1) == std::string_view(c.second));
                    fits_ok = fits_ok && lines_ok;
                    rule_ok = rule_ok && name_wrap_ok(f, icon, name);
                    detail += std::string(detail.empty() ? "" : "、") + c.name + "→" + std::string(status_name_line(name, f, 0))
                        + (two ? ("／" + std::string(status_name_line(name, f, 1))) : std::string()) + "(" + std::to_string(text.sized_px(f.scale)) + "px)";
                }
                check(fits_ok, "名前の折れ方(アイコン " + std::to_string(icon) + "px): " + detail);
                check(rule_ok, "名前の折れ方(アイコン " + std::to_string(icon) + "px): 基準の大きさ → 2 行に折る → 2 行のまま縮める、の順で決まる");
            }
            //! 基準の大きさで 2 行に収まるなら縮めない（ドラゴンの感知は基準の大きさのまま 2 行）。
            {
                const int icon = 64;
                const int ref = status_text_reference_px(text, icon);
                const FittedText f = fit_status_name(text, "ドラゴンの感知", icon);
                check((f.lines == 2) && (text.sized_px(f.scale) == ref) && (f.width <= icon) && !f.overflow,
                    "名前: 基準の大きさ(" + std::to_string(ref) + "px)で 2 行に収まるなら、そのまま 2 行");
            }
            //! 2 行でも基準の大きさで収まらない長い名前は、2 行のまま縮める。さらに下限でも収まらなければ下限の 2 行で幅を広げる。
            {
                const int icon = 64;
                const int ref = status_text_reference_px(text, icon);
                const FittedText shrunk = fit_status_name(text, "ドラゴンの感知ドラゴンの感知", icon);
                const FittedText wide = fit_status_name(text, "ドラゴンの感知ドラゴンの感知ドラゴンの感知ドラゴンの感知", icon);
                check((shrunk.lines == 2) && (wide.lines == 2) && (text.sized_px(shrunk.scale) <= ref)
                        && (text.sized_px(wide.scale) <= text.sized_px(shrunk.scale)) && wide.overflow && (wide.width > icon)
                        && (text.sized_px(wide.scale) == std::min(ref, min_px)),
                    "名前: 2 行でも収まらない長さは 2 行のまま縮め(" + std::to_string(text.sized_px(shrunk.scale)) + "px)、下限("
                        + std::to_string(text.sized_px(wide.scale)) + "px)でも収まらなければ幅を広げる");
            }
            //! 空の名前・折れない名前は 1 行。
            {
                const FittedText none = fit_status_name(text, "", 64);
                const FittedText unbroken = fit_status_name(text, "Hallucinationxxxx", 64);
                check((none.lines == 1) && (unbroken.lines == 1) && (unbroken.scale < 1.f) && status_name_line("abc", none, 1).empty(),
                    "名前: 空の名前・折れる位置が無い名前は 1 行のまま（折れない長い名前は縮める）");
            }
        }
        //! 玉の横の段は、名前が 1 行でも 2 行でも高さが変わらない。常時の列は実際の行数で詰める。
        {
            for (const LayoutMode mode : { LayoutMode::Full, LayoutMode::Hybrid, LayoutMode::Split }) {
                for (const std::array<int, 2> &size : { std::array<int, 2>{ 2560, 1080 }, std::array<int, 2>{ 1280, 720 } }) {
                    const UiLayout layout = UiLayout::compute(mode, size[0], size[1], text.cell_w(), text.cell_h(), false);
                    const std::string tag = std::string(layout_mode_name(mode)) + " " + std::to_string(size[0]) + "x" + std::to_string(size[1]);
                    GameFrame short_names = base;
                    GameFrame long_names = base;
                    for (StatusEffectView &e : short_names.status_effects) {
                        e.label_utf8 = "毒";
                    }
                    for (StatusEffectView &e : long_names.status_effects) {
                        e.label_utf8 = (e.kind == StatusEffectKind::Bad) ? "弱い呪い増殖" : "ドラゴンの感知";
                    }
                    const StatusIconLayout mixed = layout_status_icons(text, layout, base);
                    const StatusIconLayout ones = layout_status_icons(text, layout, short_names);
                    const StatusIconLayout twos = layout_status_icons(text, layout, long_names);
                    int one_line = 0;
                    int two_line = 0;
                    bool same_h = !mixed.items.empty() && !ones.items.empty() && !twos.items.empty();
                    bool same_y = true;
                    bool band_ok = true;
                    for (const StatusIconLayout *l : { &mixed, &ones, &twos }) {
                        for (const StatusIconPlacement &item : l->items) {
                            same_h = same_h && (item.cell.h == mixed.items.front().cell.h);
                            same_y = same_y && (item.cell.y == mixed.items.front().cell.y) && (item.icon.y == mixed.items.front().icon.y);
                            band_ok = band_ok && (item.name_band.y == (item.icon.y + item.icon.h))
                                && (item.name_band.h == (item.name.lines * l->name_line_h))
                                && ((item.name_band.y + (2 * l->name_line_h)) == (item.cell.y + item.cell.h));
                            if (l == &mixed) {
                                one_line += (item.name.lines == 1) ? 1 : 0;
                                two_line += (item.name.lines == 2) ? 1 : 0;
                            }
                        }
                    }
                    check(same_h && same_y && band_ok && (one_line > 0) && (two_line > 0),
                        tag + ": 玉の横の段の高さは、名前が 1 行・2 行・混在でも同じ（1 行の名前 " + std::to_string(one_line) + " 個 / 2 行 "
                            + std::to_string(two_line) + " 個。名前の場所は常に 2 行ぶん）");

                    //! 常時の列: 1 個ずつ実際の行数で詰める（同じ列の隣どうしの間はすき間だけ。良い効果と呪いの境だけ広い）。
                    const PermanentIconLayout perm = layout_permanent_icons(text, layout, base);
                    const PermanentIconLayout perm_ones = layout_permanent_icons(text, layout, short_names);
                    const PermanentIconLayout perm_twos = layout_permanent_icons(text, layout, long_names);
                    const int gap = std::max(4, perm.icon_px / 8);
                    int perm_one = 0;
                    int perm_two = 0;
                    bool packed_ok = true;
                    bool height_ok = true;
                    for (const PermanentIconPlacement &a : perm.items) {
                        perm_one += (a.name.lines == 1) ? 1 : 0;
                        perm_two += (a.name.lines == 2) ? 1 : 0;
                        height_ok = height_ok && (a.cell.h == (perm.icon_px + (a.name.lines * perm.name_line_h)));
                        for (const PermanentIconPlacement &b : perm.items) {
                            if ((a.column == b.column) && (b.cell.y > a.cell.y)) {
                                bool is_next = true;
                                for (const PermanentIconPlacement &c : perm.items) {
                                    is_next = is_next && !((c.column == a.column) && (c.cell.y > a.cell.y) && (c.cell.y < b.cell.y));
                                }
                                if (is_next) {
                                    const int space = b.cell.y - (a.cell.y + a.cell.h);
                                    packed_ok = packed_ok && ((space == gap) || ((space == (gap + (perm.icon_px / 2))) && b.bad && !a.bad));
                                }
                            }
                        }
                    }
                    bool all_one = !perm_ones.items.empty();
                    for (const PermanentIconPlacement &p : perm_ones.items) {
                        all_one = all_one && (p.cell.h == (perm_ones.icon_px + perm_ones.name_line_h));
                    }
                    bool all_two = !perm_twos.items.empty();
                    for (const PermanentIconPlacement &p : perm_twos.items) {
                        all_two = all_two && (p.cell.h == (perm_twos.icon_px + (2 * perm_twos.name_line_h)));
                    }
                    check(packed_ok && height_ok && (perm_one > 0) && (perm_two > 0) && all_one && all_two,
                        tag + ": 常時の列は 1 個ずつ実際の行数で詰める（1 行 " + std::to_string(perm_one) + " 個 / 2 行 " + std::to_string(perm_two) + " 個）");

                    //! 英語の名前の合成フレームでも同じ規則（空白でだけ折れ、1 語は折れない）。段の高さは日本語と同じ。
                    GameFrame english = base;
                    make_status_hud_frame_english(english);
                    const StatusIconLayout en_row = layout_status_icons(text, layout, english);
                    const PermanentIconLayout en_perm = layout_permanent_icons(text, layout, english);
                    bool en_ok = !en_row.items.empty() && !en_perm.items.empty();
                    int en_two = 0;
                    int en_one = 0;
                    for (const StatusIconPlacement &item : en_row.items) {
                        const std::string &label = english.status_effects[static_cast<std::size_t>(item.effect_index)].label_utf8;
                        en_ok = en_ok && name_wrap_ok(item.name, en_row.icon_px, label) && (item.cell.h == mixed.items.front().cell.h);
                        en_two += (item.name.lines == 2) ? 1 : 0;
                        en_one += (item.name.lines == 1) ? 1 : 0;
                    }
                    for (const PermanentIconPlacement &item : en_perm.items) {
                        const std::string &label = english.status_effects[static_cast<std::size_t>(item.effect_index)].label_utf8;
                        en_ok = en_ok && name_wrap_ok(item.name, en_perm.icon_px, label);
                        en_two += (item.name.lines == 2) ? 1 : 0;
                        en_one += (item.name.lines == 1) ? 1 : 0;
                    }
                    check(en_ok && (en_two > 0) && (en_one > 0),
                        tag + ": 英語の名前も同じ規則（空白でだけ折れ、1 語は折れない。1 行 " + std::to_string(en_one) + " 個 / 2 行 " + std::to_string(en_two) + " 個）");
                }
            }
        }
        //! 基準の大きさ: アイコンの幅ごとに 1 つ。1〜4 文字は同じ大きさ、5 文字以上だけ縮む。
        {
            for (const int icon : { 64, 43, 34, 24, 200 }) {
                const int ref = status_text_reference_px(text, icon);
                const bool fits_ref = text.measure_sized(kStatusTextReferenceString, static_cast<float>(ref) / static_cast<float>(text.base_px())) <= icon;
                const bool one_bigger_fails = (ref == text.base_px())
                    || (text.measure_sized(kStatusTextReferenceString, static_cast<float>(ref + 1) / static_cast<float>(text.base_px())) > icon);
                const bool floor_case = ref == text.sized_px(0.3f); //!< 極端に狭いとき。収まらなくても下限
                check((fits_ref || floor_case) && one_bigger_fails && (ref <= text.base_px()),
                    "基準の字: アイコン " + std::to_string(icon) + "px では全角 4 文字が収まるいちばん大きい字 = " + std::to_string(ref)
                        + "px（普通の字 " + std::to_string(text.base_px()) + "px を超えない）");
            }
            for (const int icon : { 64, 43, 34 }) {
                const int ref = status_text_reference_px(text, icon);
                bool same = true;
                for (const char *label : { "毒", "加速", "祝福", "ヒーロー", "火炎耐性", "Haste", "AB", "+10", "+1" }) {
                    const FittedText f = fit_status_text(text, label, icon);
                    same = same && (text.sized_px(f.scale) == ref) && !f.overflow;
                }
                const FittedText five = fit_status_text(text, "テレパシー", icon);
                const FittedText six = fit_status_text(text, "ドラゴン感知", icon);
                const FittedText english = fit_status_text(text, "Resist Fire", icon);
                const bool shrinks_only_long = (text.sized_px(five.scale) <= ref) && (text.sized_px(six.scale) < ref || ref <= min_px)
                    && (text.sized_px(english.scale) < ref || ref <= min_px) && (text.sized_px(six.scale) <= text.sized_px(five.scale));
                check(same, "基準の字(" + std::to_string(icon) + "px): 1〜4 文字・短い英数字・効果量はすべて同じ大きさ（" + std::to_string(ref) + "px）");
                check(shrinks_only_long, "基準の字(" + std::to_string(icon) + "px): 5 文字以上・長い英語だけが縮む（5 文字 "
                        + std::to_string(text.sized_px(five.scale)) + "px / 6 文字 " + std::to_string(text.sized_px(six.scale)) + "px / 英語 "
                        + std::to_string(text.sized_px(english.scale)) + "px）");
            }
        }
    }

    // ---- 作りを替えた絵（人が見る用）: Full と、状態列が右の Hybrid ----
    {
        struct Extra {
            int w;
            int h;
            LayoutMode mode;
            int side;
            const char *suffix;
            bool english{ false }; //!< 名前を英語にした合成フレームで描く
        };
        GameFrame english_frame = base;
        make_status_hud_frame_english(english_frame);
        for (const Extra &extra : { Extra{ 2560, 1080, LayoutMode::Full, 0, "_full" }, Extra{ 1280, 720, LayoutMode::Full, 0, "_full" },
                 Extra{ 2560, 1080, LayoutMode::Hybrid, 1, "_right" }, Extra{ 2560, 1080, LayoutMode::Split, 0, "_split" },
                 Extra{ 2560, 1080, LayoutMode::Hybrid, 0, "_en", true }, Extra{ 1280, 720, LayoutMode::Hybrid, 0, "_en", true } }) {
            StatusHudTarget target;
            if (!make_status_hud_target(target, extra.w, extra.h)) {
                check(false, "画面の外の描画先を作れません（作りを替えた絵）");
                release_status_hud_target(target);
                continue;
            }
            render_mode = extra.mode;
            set_status_col_side(extra.side);
            HudState state;
            orb_hud_update(state.orb_anim, state.orb_params, 1.f, 1.f, 0u);
            advance(state, ratio_hp(base), ratio_mp(base), 0u, 3000u);
            const std::vector<unsigned char> shot = render(target, extra.english ? english_frame : base, state, 3000u);
            set_status_col_side(0);
            render_mode = LayoutMode::Hybrid;
            check(drain_gl_errors().empty(), std::string("描いて GL エラーが出ない（") + extra.suffix + "）");
            check(save_rgba_png(out_path(extra.w, extra.h, extra.suffix), shot.data(), extra.w, extra.h),
                "画像を書き出せた " + out_path(extra.w, extra.h, extra.suffix));
            release_status_hud_target(target);
        }
    }

    // ---- 玉の横のアイコンの巡回（あふれたとき。1 行のまま、玉から遠ざかる向きへ等速で止まらずに流す） ----
    {
        const char *const left_extra[][2] = {
            { "berserk", "狂戦士化" }, { "might", "剛力" }, { "stoneskin", "石の肌" }, { "magicdef", "魔法防御" },
            { "protevil", "邪悪防御" }, { "res_acid", "酸耐性" }, { "res_elec", "電撃耐性" }, { "res_pois", "毒耐性" },
            { "reflect", "反射" }, { "levitate", "浮遊" }, { "stealth", "隠密" }, { "infravision", "赤外線視力" },
            { "nightsight", "暗視" }, { "esp_evil", "邪悪感知" }, { "cure", "治癒" },
        };
        const char *const right_extra[][2] = {
            { "blind", "盲目" }, { "paralyzed", "麻痺" }, { "afraid", "恐怖" }, { "slow", "減速" }, { "stun", "朦朧" },
            { "hungry", "空腹" }, { "hallucination", "幻覚" }, { "expand", "膨張" }, { "no_tele", "転移不能" },
            { "no_magic", "魔法不能" }, { "slow_regen", "回復低下" }, { "cowardice", "臆病" }, { "self_fire", "自傷火炎" },
            { "low_ac", "防御低下" },
        };
        const auto add_effect = [](GameFrame &f, const char *key, const char *label, StatusEffectKind kind) {
            StatusEffectView v;
            v.key = key;
            v.label_utf8 = label;
            v.kind = kind;
            f.status_effects.push_back(v);
        };
        GameFrame left_only = base; //!< 左だけあふれる（左 24 個・右 3 個）
        for (const auto &e : left_extra) {
            add_effect(left_only, e[0], e[1], StatusEffectKind::Good);
        }
        GameFrame over = left_only; //!< 左右ともあふれる（左 24 個・右 17 個）
        for (const auto &e : right_extra) {
            add_effect(over, e[0], e[1], StatusEffectKind::Bad);
        }
        const auto without_kinds = [](GameFrame f, bool drop_left) {
            f.status_effects.erase(std::remove_if(f.status_effects.begin(), f.status_effects.end(),
                                       [&](const StatusEffectView &e) {
                                           const bool is_left = (e.kind == StatusEffectKind::Good) || (e.kind == StatusEffectKind::Waiting)
                                               || (e.kind == StatusEffectKind::Special);
                                           return drop_left ? is_left : (e.kind == StatusEffectKind::Bad);
                                       }),
                f.status_effects.end());
            return f;
        };

        for (const std::array<int, 2> &size : { std::array<int, 2>{ 2560, 1080 }, std::array<int, 2>{ 1280, 720 } }) {
            const int w = size[0];
            const int h = size[1];
            const std::string tag = "巡回 " + std::to_string(w) + "x" + std::to_string(h);
            const UiLayout layout = make_layout(w, h);
            const RectPx &orb = layout.orb;
            const RectPx window_rect{ 0, 0, w, h };
            const auto at = [&](const GameFrame &f, double left_ms, double right_ms) {
                return layout_status_icons(text, layout, f, left_ms, right_ms);
            };
            const StatusIconLayout t0 = at(over, 0.0, 0.0);
            const int icon_px = t0.icon_px;
            const int gap = std::max(4, icon_px / 8);
            const double v = static_cast<double>(icon_px + gap); //!< 1 秒にアイコン 1 個ぶん
            check(t0.ring_left.rotating && t0.ring_right.rotating && (t0.ring_left.count == 24) && (t0.ring_right.count == 17),
                tag + ": あふれる合成フレーム（左 24・右 17）は左右とも輪になる（枠に入る個数 左 " + std::to_string(t0.ring_left.capacity)
                    + " / 右 " + std::to_string(t0.ring_right.capacity) + "）");
            check((t0.ring_left.speed == v) && (t0.ring_right.speed == v),
                tag + ": 速さは 1 秒にアイコン 1 個ぶん（アイコンの幅 + すき間 = " + std::to_string(static_cast<int>(v)) + " 画素/秒）");
            const auto side_items = [](const StatusIconLayout &l, bool left) {
                std::vector<const StatusIconPlacement *> out;
                for (const StatusIconPlacement &item : l.items) {
                    if (item.left == left) {
                        out.push_back(&item);
                    }
                }
                return out;
            };
            const auto true_x = [](const StatusIconPlacement &item) { return static_cast<double>(item.cell.x) + static_cast<double>(item.frac_x); };

            for (const bool left : { true, false }) {
                const std::string side = tag + (left ? " 左" : " 右");
                const StatusRing &ring = left ? t0.ring_left : t0.ring_right;
                const int edge_near = left ? orb.x : (orb.x + orb.w);
                const int n = ring.count;
                const double pitch = static_cast<double>(ring.pitch);
                /*
                 * 段の高さ: 左右とも同じ。玉の上から 42% か、メッセージの帯と名前の行を避けてそれより上
                 *。左右の両方の枠が同じ高さにあることで見る。
                 */
                const int orb_row_y = orb.y + static_cast<int>(std::lround(orb.h * 0.42));
                const int row0_y = t0.ring_left.window.y + text.cell_h();
                check((row0_y <= orb_row_y) && (t0.ring_left.window.y == t0.ring_right.window.y), side + ": 段の高さは左右で同じで、玉の横の高さか、それより上");

                // 枠は玉の横・地図の中・1 行ぶんの高さ
                {
                    const RectPx &win = ring.window;
                    const bool near_ok = left ? ((win.x + win.w) == edge_near) : (win.x == edge_near);
                    const bool inside = (rect_overlap_area(win, layout.scene) == (static_cast<long long>(win.w) * win.h))
                        && (rect_overlap_area(win, window_rect) == (static_cast<long long>(win.w) * win.h))
                        && (win.w == (ring.capacity * ring.pitch)) && (win.h == (text.cell_h() + icon_px + (2 * t0.name_line_h)))
                        && (win.y + text.cell_h() == row0_y);
                    check(near_ok && inside, side + ": 見えている枠は玉の縁から外へ、1 行ぶんの高さ（" + std::to_string(win.w) + "x" + std::to_string(win.h) + "）");
                }

                // 全時刻を通した検査（輪を 1 周まるごと + 2 秒を、37 ms 刻みで。継ぎ目も通る）
                bool count_ok = true;
                bool window_ok = true;
                bool position_ok = true;
                bool chain_ok = true;
                bool continuity_ok = true;
                bool fade_ok = true;
                bool one_row_ok = true;
                bool seam_seen = false;
                bool wrap_seen = false;
                double worst_step = 0.0;
                const double cycle_ms = (ring.length / v) * 1000.0;
                std::vector<const StatusIconPlacement *> previous;
                StatusIconLayout previous_layout;
                const double step_ms = 37.0;
                for (double t = 0.0; t <= (cycle_ms + 2000.0); t += step_ms) {
                    const StatusIconLayout now = at(over, t, t);
                    std::vector<const StatusIconPlacement *> items = side_items(now, left);
                    count_ok = count_ok && (static_cast<int>(items.size()) >= ring.capacity) && (static_cast<int>(items.size()) <= (ring.capacity + 1));
                    std::sort(items.begin(), items.end(), [](const StatusIconPlacement *a, const StatusIconPlacement *b) {
                        return a->ring_distance < b->ring_distance;
                    });
                    for (std::size_t k = 0; k < items.size(); ++k) {
                        const StatusIconPlacement &it = *items[k];
                        const double c = static_cast<double>(it.ring_distance);
                        const double x = true_x(it);
                        const double want_x = left ? (static_cast<double>(edge_near) - c - pitch) : (static_cast<double>(edge_near) + c);
                        position_ok = position_ok && (std::fabs(x - want_x) < 0.01);
                        one_row_ok = one_row_ok && (it.row == 0) && (it.icon.y == row0_y) && it.rotating;
                        // 枠: 切る枠は輪の枠そのもので、見えている割合が alpha（全部入っていれば 1）
                        const double win0 = static_cast<double>(ring.window.x);
                        const double win1 = win0 + static_cast<double>(ring.window.w);
                        const double visible = std::min(x + pitch, win1) - std::max(x, win0);
                        window_ok = window_ok && (it.clip.x == ring.window.x) && (it.clip.w == ring.window.w) && (visible > 0.0);
                        fade_ok = fade_ok && (it.alpha > 0.f) && (it.alpha <= 1.f)
                            && (std::fabs(static_cast<double>(it.alpha) - std::clamp(visible / pitch, 0.0, 1.0)) < 0.01)
                            && ((visible < (pitch - 0.01)) || (it.alpha > 0.999f));
                        if (k > 0) {
                            const StatusIconPlacement &before = *items[k - 1];
                            const int step_index = ((it.ring_index - before.ring_index) + n) % n;
                            chain_ok = chain_ok && (std::fabs(c - static_cast<double>(before.ring_distance) - pitch) < 0.01) && (step_index == 1);
                            if ((before.ring_index == (n - 1)) && (it.ring_index == 0)) {
                                seam_seen = true; // 輪の継ぎ目（最後の次に最初）が枠の中で並んだ
                            }
                        }
                        if (c < 0.0) {
                            wrap_seen = true;
                        }
                    }
                    // 前の標本とのつながり: 同じアイコンの動きは時間に比例し、向きは玉から遠ざかる
                    if (t > 0.0) {
                        for (const StatusIconPlacement *a : previous) {
                            for (const StatusIconPlacement &bi : now.items) {
                                if ((bi.left == left) && (bi.ring_index == a->ring_index)) {
                                    const double dx = true_x(bi) - true_x(*a);
                                    const double dc = static_cast<double>(bi.ring_distance) - static_cast<double>(a->ring_distance);
                                    const double want_dc = v * step_ms * 0.001;
                                    worst_step = std::max(worst_step, std::fabs(dc - want_dc));
                                    continuity_ok = continuity_ok && (std::fabs(dc - want_dc) < 0.01)
                                        && (std::fabs(dx - (left ? -want_dc : want_dc)) < 0.01);
                                }
                            }
                        }
                    }
                    previous_layout = now; // `previous` の指す先を、次の標本まで生かしておく
                    previous = side_items(previous_layout, left);
                }
                check(count_ok, side + ": 枠に見えているのは常に " + std::to_string(ring.capacity) + " 個か " + std::to_string(ring.capacity + 1)
                        + " 個（半分入りかけ・出かけを含む）");
                check(position_ok, side + ": 位置は輪の中の距離と一致する（左は玉の縁から左へ・右は右へ）");
                check(window_ok && one_row_ok, side + ": 見えるものはすべて枠にかかり、1 行のまま（上に段を積まない）");
                check(fade_ok, side + ": 枠の端で入りかけ・出かけのアイコンは、見えている割合で薄くなる");
                check(chain_ok && seam_seen && wrap_seen,
                    side + ": 輪の継ぎ目: 最後の次に最初が、ちょうど 1 個ぶん離れて続く（継ぎ目が枠を通るのを確認 "
                        + (seam_seen ? "済" : "未") + " / 玉の側から入りかけも確認 " + (wrap_seen ? "済" : "未") + "）");
                check(continuity_ok, side + ": 37 ms ごとの動きは一定（誤差の最大 " + std::to_string(worst_step) + " 画素。飛ばない・止まらない）");

                // 0 秒・0.5 秒・1 秒・2 秒: 動いた量が時間に比例する（0.5 秒でちょうど半個ぶん）
                {
                    const StatusIconLayout base_layout = at(over, 0.0, 0.0);
                    const std::vector<const StatusIconPlacement *> first = side_items(base_layout, left);
                    check(!first.empty() && (first.front()->ring_index == 0) && (std::fabs(first.front()->ring_distance) < 0.001f),
                        side + ": 0 秒は先頭が玉の縁にぴったり（輪の先頭 = 玉に近い側）");
                    double shift[4]{};
                    const double times[4] = { 0.0, 500.0, 1000.0, 2000.0 };
                    bool shift_ok = true;
                    for (int k = 1; k < 4; ++k) {
                        const StatusIconLayout later = at(over, times[k], times[k]);
                        double measured = 0.0;
                        bool found = false;
                        for (const StatusIconPlacement *a : first) {
                            for (const StatusIconPlacement &bi : later.items) {
                                if ((bi.left == left) && (bi.ring_index == a->ring_index)) {
                                    const double d = left ? (true_x(*a) - true_x(bi)) : (true_x(bi) - true_x(*a));
                                    if (!found) {
                                        measured = d;
                                        found = true;
                                    }
                                    shift_ok = shift_ok && (std::fabs(d - (v * times[k] * 0.001)) < 0.01);
                                }
                            }
                        }
                        shift[k] = measured;
                        shift_ok = shift_ok && found;
                    }
                    check(shift_ok && (std::fabs(shift[1] - (0.5 * v)) < 0.01) && (std::fabs(shift[2] - v) < 0.01) && (std::fabs(shift[3] - (2.0 * v)) < 0.01)
                            && (std::fabs((shift[1] * 2.0) - shift[2]) < 0.01),
                        side + ": 流れた量は時間に比例（0.5 秒 " + std::to_string(shift[1]) + " / 1 秒 " + std::to_string(shift[2]) + " / 2 秒 "
                            + std::to_string(shift[3]) + " 画素。1 個ぶん = " + std::to_string(v) + "）");
                }
            }

            // 入りきる側は回さない（左だけあふれるフレームでは、右の 3 個は時刻が進んでも動かない。左は動く）
            {
                const StatusIconLayout a = at(left_only, 0.0, 0.0);
                const StatusIconLayout b = at(left_only, 7300.0, 7300.0);
                bool right_same = true;
                bool left_moved = false;
                const std::vector<const StatusIconPlacement *> ra = side_items(a, false);
                const std::vector<const StatusIconPlacement *> rb = side_items(b, false);
                right_same = (ra.size() == rb.size()) && (ra.size() == 3u) && !a.ring_right.rotating;
                for (std::size_t i = 0; right_same && (i < ra.size()); ++i) {
                    right_same = (ra[i]->cell.x == rb[i]->cell.x) && (ra[i]->frac_x == rb[i]->frac_x) && !ra[i]->rotating
                        && (ra[i]->alpha == 1.f);
                }
                const std::vector<const StatusIconPlacement *> la = side_items(a, true);
                const std::vector<const StatusIconPlacement *> lb = side_items(b, true);
                for (std::size_t i = 0; (i < la.size()) && (i < lb.size()); ++i) {
                    left_moved = left_moved || (true_x(*la[i]) != true_x(*lb[i]));
                }
                check(right_same && left_moved && a.ring_left.rotating, tag + ": 入りきる側（右 3 個）は時刻が進んでも止まったまま、あふれる左だけが動く（左右は別々に回る）");
                // 全部入りきるフレーム（合成フレームそのもの。2560 だけ）
                if (w >= 2000) {
                    const StatusIconLayout c0 = at(base, 0.0, 0.0);
                    const StatusIconLayout c1 = at(base, 9100.0, 9100.0);
                    bool same = (c0.items.size() == c1.items.size()) && !c0.ring_left.rotating && !c0.ring_right.rotating;
                    for (std::size_t i = 0; same && (i < c0.items.size()); ++i) {
                        same = (c0.items[i].cell.x == c1.items[i].cell.x) && (c0.items[i].frac_x == c1.items[i].frac_x) && !c0.items[i].rotating;
                    }
                    check(same, tag + ": 入りきるときは回さない（時刻を変えても位置が同じ）");
                }
            }
        }

        // ---- 描いた絵（枠の外へ出ない・悪い効果の下敷きが赤・巡回の途中の絵）と、巡回の起点 ----
        /*
         * プロンプトを外したフレームで描く。玉とアイコンは地図の下縁に置き、プロンプトの帯はその上に重ねて描く
         *ので、帯があると名前の下敷きの色を測れない。
         */
        GameFrame quiet = over;
        quiet.prompt.text_utf8.clear();
        quiet.prompt.choices.clear();
        for (const std::array<int, 2> &size : { std::array<int, 2>{ 2560, 1080 }, std::array<int, 2>{ 1280, 720 } }) {
            const int w = size[0];
            const int h = size[1];
            const std::string tag = "巡回の絵 " + std::to_string(w) + "x" + std::to_string(h);
            StatusHudTarget target;
            if (!make_status_hud_target(target, w, h)) {
                check(false, tag + ": 画面の外の描画先を作れません");
                release_status_hud_target(target);
                continue;
            }
            const UiLayout layout = make_layout(w, h);
            const StatusIconLayout lay = layout_status_icons(text, layout, quiet, 0.0, 0.0);
            const auto fresh_state = [&](const GameFrame &f) {
                HudState st;
                orb_hud_update(st.orb_anim, st.orb_params, ratio_hp(f), ratio_mp(f), 0u);
                advance(st, ratio_hp(f), ratio_mp(f), 0u, 3000u);
                return st;
            };
            //! 0 秒（3000 ms）・0.5 秒・1 秒の 3 枚（起点は最初に描いた時刻 = 3000）
            std::vector<unsigned char> shots[3];
            {
                HudState st = fresh_state(quiet);
                const std::uint32_t times[3] = { 3000u, 3500u, 4000u };
                for (int k = 0; k < 3; ++k) {
                    advance(st, ratio_hp(quiet), ratio_mp(quiet), (k == 0) ? 3000u : times[k - 1], times[k]);
                    shots[k] = render(target, quiet, st, times[k]);
                }
                const char *const suffix[3] = { "_ring0", "_ring500", "_ring1000" };
                for (int k = 0; k < 3; ++k) {
                    check(save_rgba_png(out_path(w, h, suffix[k]), shots[k].data(), w, h), tag + ": 画像を書き出せた " + out_path(w, h, suffix[k]));
                }
                check((st.orb_anim.ring_origin_ms[0] == 3000u) && (st.orb_anim.ring_origin_ms[1] == 3000u), tag + ": 巡回の起点は最初に描いた時刻");
                check((count_changed_pixels(shots[0], shots[1], w, h, lay.ring_left.window) > 0)
                        && (count_changed_pixels(shots[1], shots[2], w, h, lay.ring_left.window) > 0)
                        && (count_changed_pixels(shots[0], shots[1], w, h, lay.ring_right.window) > 0)
                        && (count_changed_pixels(shots[1], shots[2], w, h, lay.ring_right.window) > 0),
                    tag + ": 0 秒・0.5 秒・1 秒で、左右の枠の中の絵がそれぞれ違う（流れている）");
            }
            // 枠の外へ描かない: 片側のアイコンを全部抜いた絵と比べ、枠の外（玉・端の側）が 1 画素も変わらない
            {
                const GameFrame no_left = without_kinds(quiet, true);
                const GameFrame no_right = without_kinds(quiet, false);
                HudState st_a = fresh_state(quiet);
                HudState st_l = fresh_state(no_left);
                HudState st_r = fresh_state(no_right);
                std::vector<unsigned char> img_a;
                std::vector<unsigned char> img_l;
                std::vector<unsigned char> img_r;
                for (const std::uint32_t t : { 3000u, 3700u }) {
                    advance(st_a, ratio_hp(quiet), ratio_mp(quiet), (t == 3000u) ? 3000u : 3000u, t);
                    advance(st_l, ratio_hp(quiet), ratio_mp(quiet), (t == 3000u) ? 3000u : 3000u, t);
                    advance(st_r, ratio_hp(quiet), ratio_mp(quiet), (t == 3000u) ? 3000u : 3000u, t);
                    img_a = render(target, quiet, st_a, t);
                    img_l = render(target, no_left, st_l, t);
                    img_r = render(target, no_right, st_r, t);
                }
                const RectPx &lw = lay.ring_left.window;
                const RectPx &rw = lay.ring_right.window;
                const RectPx left_beyond{ layout.scene.x, lw.y, lw.x - layout.scene.x, lw.h };
                const RectPx right_beyond{ rw.x + rw.w, rw.y, (layout.scene.x + layout.scene.w) - (rw.x + rw.w), rw.h };
                const std::size_t left_leak = count_changed_pixels(img_a, img_l, w, h, left_beyond) + count_changed_pixels(img_a, img_l, w, h, layout.orb);
                const std::size_t right_leak = count_changed_pixels(img_a, img_r, w, h, right_beyond) + count_changed_pixels(img_a, img_r, w, h, layout.orb);
                check((left_leak == 0) && (right_leak == 0),
                    tag + ": 枠の外（玉の上・端の側）へは 1 画素もはみ出さない（左 " + std::to_string(left_leak) + " / 右 " + std::to_string(right_leak) + " 画素）");
                check((count_changed_pixels(img_a, img_l, w, h, lw) > 0) && (count_changed_pixels(img_a, img_r, w, h, rw) > 0),
                    tag + ": 枠の中には描かれている（対照）");
            }
            // 巡回の起点: 並びの中身が変わった側だけ先頭へ戻り、効果量の変化では戻らない
            {
                HudState st = fresh_state(quiet);
                render(target, quiet, st, 3000u);
                render(target, quiet, st, 3500u);
                const bool first_ok = (st.orb_anim.ring_origin_ms[0] == 3000u) && (st.orb_anim.ring_origin_ms[1] == 3000u);
                GameFrame amount_changed = quiet;
                amount_changed.status_effects[0].amount_utf8 = "+11"; // 加速の効果量
                render(target, amount_changed, st, 3600u);
                const bool amount_ok = (st.orb_anim.ring_origin_ms[0] == 3000u);
                GameFrame left_changed = amount_changed;
                left_changed.status_effects.erase(left_changed.status_effects.begin() + 1); // 左の 1 個（ヒーロー）が切れた
                render(target, left_changed, st, 3700u);
                const bool left_ok = (st.orb_anim.ring_origin_ms[0] == 3700u) && (st.orb_anim.ring_origin_ms[1] == 3000u);
                check(first_ok && amount_ok && left_ok, tag + ": 並びが変わった側だけ先頭へ戻る（効果量だけの変化や反対側は戻らない）");
            }
            // 悪い効果の名前の下敷きは暗めの赤、それ以外は黒
            if (w >= 2000) {
                GameFrame base_quiet = base; //!< プロンプトの帯が名前に重ならないように外す（上の quiet と同じ理由）
                base_quiet.prompt.text_utf8.clear();
                base_quiet.prompt.choices.clear();
                HudState st = fresh_state(base_quiet);
                const std::vector<unsigned char> img = render(target, base_quiet, st, 3000u);
                const StatusIconLayout plain = layout_status_icons(text, layout, base);
                const PermanentIconLayout perm = layout_permanent_icons(text, layout, base);
                const auto red_ratio = [&](const RectPx &band) {
                    int red = 0;
                    int total = 0;
                    long long sum_r = 0;
                    for (int y = band.y; y < (band.y + band.h); ++y) {
                        for (int x = band.x; x < (band.x + band.w); ++x) {
                            const std::size_t i = (((static_cast<std::size_t>(h - 1 - y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)) * 4);
                            const int r = img[i];
                            const int g = img[i + 1];
                            const int b = img[i + 2];
                            red += ((r >= 70) && (r >= (g * 2)) && (r >= (b * 2))) ? 1 : 0;
                            sum_r += r;
                            ++total;
                        }
                    }
                    return std::array<double, 2>{ static_cast<double>(red) / std::max(1, total), static_cast<double>(sum_r) / std::max(1, total) };
                };
                bool bad_red = true;
                bool good_dark = true;
                bool perm_bad_red = true;
                bool perm_good_dark = true;
                int bad_n = 0;
                int good_n = 0;
                for (const StatusIconPlacement &item : plain.items) {
                    const StatusEffectView &effect = base.status_effects[static_cast<std::size_t>(item.effect_index)];
                    if (effect.label_utf8.empty()) {
                        continue;
                    }
                    const auto r = red_ratio(item.name_band);
                    if (effect.kind == StatusEffectKind::Bad) {
                        bad_red = bad_red && (r[0] > 0.35);
                        ++bad_n;
                    } else {
                        good_dark = good_dark && (r[0] < 0.01);
                        ++good_n;
                    }
                    if (!item.amount_band.empty()) {
                        good_dark = good_dark && (red_ratio(item.amount_band)[0] < 0.01);
                    }
                }
                for (const PermanentIconPlacement &item : perm.items) {
                    const auto r = red_ratio(item.name_band);
                    if (item.bad) {
                        perm_bad_red = perm_bad_red && (r[0] > 0.35);
                    } else {
                        perm_good_dark = perm_good_dark && (r[0] < 0.01);
                    }
                }
                //! 名前の字: 2 行の名前はどちらの行にも字が描かれ、1 行の名前は下の行（玉の横では空けてある場所）に字が無い。
                {
                    const auto bright = [&](const RectPx &r) {
                        int n = 0;
                        for (int y = r.y; y < (r.y + r.h); ++y) {
                            for (int x = r.x; x < (r.x + r.w); ++x) {
                                const std::size_t i = (((static_cast<std::size_t>(h - 1 - y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)) * 4);
                                n += ((img[i] >= 190) && (img[i + 1] >= 190) && (img[i + 2] >= 190)) ? 1 : 0;
                            }
                        }
                        return n;
                    };
                    bool drawn_ok = true;
                    bool empty_ok = true;
                    int two_items = 0;
                    int one_items = 0;
                    const int nlh = plain.name_line_h;
                    for (const StatusIconPlacement &item : plain.items) {
                        for (int k = 0; k < 2; ++k) {
                            const RectPx slot{ item.name_band.x, item.icon.y + item.icon.h + (k * nlh), item.name_band.w, nlh };
                            const int n = bright(slot);
                            if (k < item.name.lines) {
                                drawn_ok = drawn_ok && (n >= 3);
                            } else {
                                empty_ok = empty_ok && (n == 0);
                            }
                        }
                        two_items += (item.name.lines == 2) ? 1 : 0;
                        one_items += (item.name.lines == 1) ? 1 : 0;
                    }
                    for (const PermanentIconPlacement &item : perm.items) {
                        for (int k = 0; k < item.name.lines; ++k) {
                            const RectPx slot{ item.name_band.x, item.icon.y + item.icon.h + (k * perm.name_line_h), item.name_band.w, perm.name_line_h };
                            drawn_ok = drawn_ok && (bright(slot) >= 3);
                        }
                        two_items += (item.name.lines == 2) ? 1 : 0;
                        one_items += (item.name.lines == 1) ? 1 : 0;
                    }
                    check(drawn_ok && empty_ok && (two_items > 0) && (one_items > 0),
                        tag + ": 名前の字は行数どおりに描かれる（2 行 " + std::to_string(two_items) + " 個はどちらの行にも字がある / 1 行 "
                            + std::to_string(one_items) + " 個の下の行は空）");
                }
                check(bad_red && (bad_n == 3), tag + ": 悪い効果（kind 1）の名前の下敷きは赤（" + std::to_string(bad_n) + " 個）");
                check(good_dark && (good_n >= 9), tag + ": 良い効果の名前・効果量の下敷きは黒のまま（" + std::to_string(good_n) + " 個）");
                check(perm_bad_red && !perm.items.empty(), tag + ": 常時の悪い効果（kind 5）の名前の下敷きも赤");
                check(perm_good_dark, tag + ": 常時の良い効果（kind 4）の名前の下敷きは黒のまま");
            }
            check(drain_gl_errors().empty(), tag + ": 描いて GL エラーが出ない");
            release_status_hud_target(target);
        }
    }

    // ---- 動き（数だけ。描かない） ----
    {
        OrbHudParams params;
        // 回復: ゆっくり上がる（既定で 1 秒に 0.2）。泡が昇り、止まったら泡は消える。
        OrbHudAnimation heal;
        orb_hud_update(heal, params, 0.3f, 0.3f, 0u);
        std::uint32_t t = 0;
        for (t = 16u; t <= 496u; t += 16u) {
            orb_hud_update(heal, params, 0.7f, 0.7f, t);
        }
        const float expect = 0.3f + (0.2f * 0.496f);
        check((heal.hp.shown > 0.3f) && (heal.hp.shown < 0.7f) && (std::fabs(heal.hp.shown - expect) < 0.02f),
            "回復: 液面は一定の速さでゆっくり上がる（0.5 秒で " + std::to_string(heal.hp.shown) + " / 期待 " + std::to_string(expect) + "）");
        check(!heal.hp.bubbles.empty() && (heal.hp.boost == 0.f), "回復: 上がっている間は泡が昇る（波は大きくならない）");
        for (; t <= 4000u; t += 16u) {
            orb_hud_update(heal, params, 0.7f, 0.7f, t);
        }
        check(heal.hp.shown == 0.7f, "回復: 目標へ着いたら止まる");
        for (; t <= 12000u; t += 16u) {
            orb_hud_update(heal, params, 0.7f, 0.7f, t);
        }
        check(heal.hp.bubbles.empty(), "回復: 止まったあと、泡は液面へ着いて消える");

        // ダメージ: すばやく下がり、波が大きくなってだんだん静まる。
        OrbHudAnimation hit;
        orb_hud_update(hit, params, 0.8f, 0.8f, 0u);
        orb_hud_update(hit, params, 0.4f, 0.4f, 16u);
        check(hit.hp.boost > 0.5f, "ダメージ: 波が一時的に大きくなる（" + std::to_string(hit.hp.boost) + "）");
        std::uint32_t fall_done = 0;
        for (t = 32u; t <= 4000u; t += 16u) {
            orb_hud_update(hit, params, 0.4f, 0.4f, t);
            if ((fall_done == 0u) && (hit.hp.shown - 0.4f) < 0.02f) {
                fall_done = t;
            }
        }
        std::uint32_t rise_done = 0;
        OrbHudAnimation slow;
        orb_hud_update(slow, params, 0.4f, 0.4f, 0u);
        for (t = 16u; t <= 6000u; t += 16u) {
            orb_hud_update(slow, params, 0.8f, 0.8f, t);
            if ((rise_done == 0u) && (0.8f - slow.hp.shown) < 0.02f) {
                rise_done = t;
            }
        }
        check((fall_done > 0u) && (rise_done > 0u) && (fall_done * 3u < rise_done),
            "ダメージで下がるのは回復で上がるよりずっと速い（0.4 の差: 下がる " + std::to_string(fall_done) + " ms / 上がる "
                + std::to_string(rise_done) + " ms）");
        check((hit.hp.shown == 0.4f) && (hit.hp.boost < 0.05f), "ダメージ: 4 秒後には目標に着き、波は静まる（boost " + std::to_string(hit.hp.boost) + "）");

        // 波を止めると、ダメージでも大波にならない。
        OrbHudParams calm = params;
        calm.wave_enabled = false;
        OrbHudAnimation calm_hit;
        orb_hud_update(calm_hit, calm, 0.8f, 0.8f, 0u);
        orb_hud_update(calm_hit, calm, 0.4f, 0.4f, 16u);
        check(calm_hit.hp.boost == 0.f, "波を止める: ダメージでも波は大きくならない");

        // 間が空いたら（メニューなど）実際の割合へ即座に合わせる。
        OrbHudAnimation gap;
        orb_hud_update(gap, params, 0.8f, 0.8f, 0u);
        orb_hud_update(gap, params, 0.2f, 0.2f, 5000u);
        check(gap.hp.shown == 0.2f && gap.hp.bubbles.empty(), "描かれていなかった間（1 秒以上）の変化は即座に合わせる");
    }

    // ---- 2560x1080 で、ダメージの直後と回復の途中も書き出す（人が見る用。時刻は固定） ----
    {
        const int w = 2560;
        const int h = 1080;
        StatusHudTarget target;
        if (make_status_hud_target(target, w, h)) {
            {
                //! ダメージの直後（120 ms）: 液面が下がりかけ、波が大きい。
                GameFrame f = base;
                f.hud.hp = 93;
                HudState st;
                orb_hud_update(st.orb_anim, st.orb_params, 171.f / 245.f, ratio_mp(f), 0u);
                advance(st, ratio_hp(f), ratio_mp(f), 0u, 136u);
                const std::vector<unsigned char> shot = render(target, f, st, 136u);
                check((st.orb_anim.hp.shown > ratio_hp(f)) && (st.orb_anim.hp.shown < (171.f / 245.f)) && (st.orb_anim.hp.boost > 0.5f),
                    "ダメージの直後: 液面は下がりかけで、波が大きい（shown " + std::to_string(st.orb_anim.hp.shown) + " / boost "
                        + std::to_string(st.orb_anim.hp.boost) + "）");
                check(save_rgba_png(out_path(w, h, "_hit"), shot.data(), w, h), "ダメージ直後の画像を書き出せた " + out_path(w, h, "_hit"));
            }
            {
                //! 回復の途中（1.5 秒）: 液面がせり上がり、泡が昇る。
                GameFrame f = base;
                f.hud.hp = 171;
                f.hud.sp = 55;
                HudState st;
                orb_hud_update(st.orb_anim, st.orb_params, 0.25f, 0.20f, 0u);
                advance(st, ratio_hp(f), ratio_mp(f), 0u, 1500u);
                const std::vector<unsigned char> shot = render(target, f, st, 1500u);
                check((st.orb_anim.hp.shown < ratio_hp(f)) && (st.orb_anim.mp.shown < ratio_mp(f))
                        && !st.orb_anim.hp.bubbles.empty() && !st.orb_anim.mp.bubbles.empty(),
                    "回復の途中: 液面は上がりかけで、どちらの玉にも泡が昇っている（泡 "
                        + std::to_string(st.orb_anim.hp.bubbles.size()) + " / " + std::to_string(st.orb_anim.mp.bubbles.size()) + "）");
                check(save_rgba_png(out_path(w, h, "_rise"), shot.data(), w, h), "回復途中の画像を書き出せた " + out_path(w, h, "_rise"));
            }
            check(drain_gl_errors().empty(), "動きの途中を描いて GL エラーが出ない");
        } else {
            check(false, "画面の外の描画先（2560x1080）を作れません");
        }
        release_status_hud_target(target);
    }

    /*
     * ---- コアごとの玉の素材と配置（決めたこと 2026-10-11）----
     * 1. コア名 → 素材の組の対応と、変愚の配置が今の定数のままであること
     * 2. 全コアの素材（6 層）が読めて、配置が設計の範囲に収まり、名札・数字の帯が玉の中に収まる
     * 3. 配置ごとに、地図の下縁・メッセージの帯・名前の行・アイコンの段と重ならない（3 つの作り x 3 つの大きさ）
     * 4. 全コアの玉を 1 枚ずつ描いて書き出し、変愚と別の絵であること
     * 5. 短愚の終末の太陽（残り日数 14・7・1・0・無効）
     */
    {
        struct CoreCase {
            const char *core_name;
            const char *key;
        };
        //! 握手の `core_name` と、選ばれる素材の組（変愚と知らないコアは空 = `assets/ui/hud/` 直下）。
        static const CoreCase kCores[] = {
            { "hengband", "" }, { "tangband", "tangband" }, { "gensoband", "gensoband" }, { "silq", "silq" },
            { "frox", "frox" }, { "angband", "angband" }, { "Umoria", "umoria" }, { "Rogue 3.6", "rogue36" },
            { "Rogue 5.4", "rogue54" }, { "RogueClone2", "rogue2" },
        };
        const OrbProfile legacy{};
        const auto same_as_legacy = [&](const OrbProfile &p) {
            return p.asset_key.empty() && (p.hp_cx == 238.0) && (p.mp_cx == 522.0) && (p.cy == 128.0) && (p.r == 74.0)
                && (p.number_y == 217.0) && (p.label_bottom_y == 41.0) && (p.art_bottom_y == 230) && !p.emblem.present;
        };

        // ---- 1. 対応と、変愚は今のまま ----
        for (const CoreCase &c : kCores) {
            check(orb_asset_key_for_core(c.core_name) == c.key,
                std::string("コア名 ") + c.core_name + " の素材の組は「" + c.key + "」（空 = 変愚の素材）");
        }
        check(orb_asset_key_for_core("TANGBAND") == "tangband" && orb_asset_key_for_core("PlainCore").empty()
                && orb_asset_key_for_core("").empty(),
            "コア名は大文字小文字を区別せず、知らないコア・名前なしは変愚の素材");
        check(same_as_legacy(legacy), "変愚の配置（既定）は今の定数と同じ（238 / 522 / 128 / 74 / 数字 217 / 名札 41 / 下端 230）");
        {
            std::string note;
            check(same_as_legacy(load_orb_profile("hengband", note)) && note.empty() && same_as_legacy(load_orb_profile("PlainCore", note)),
                "変愚と知らないコアは、読んでも今の配置のまま");
            check((orb_layer_path("", "orb_back") == kOrbBackPath) && (orb_layer_path("", "orb_glass") == kOrbGlassPath)
                    && (orb_layer_path("", "orb_liquid_hp") == kOrbLiquidHpPath) && (orb_layer_path("", "orb_liquid_mp") == kOrbLiquidMpPath)
                    && (orb_layer_path("", "orb_glass_top") == kOrbGlassTopPath) && (orb_layer_path("", "orb_front") == kOrbFrontPath),
                "変愚の素材のパスは今のまま（assets/ui/hud/orb_*.png）");
        }
        {
            //! 設定ファイルの読み取り（壊れた・範囲外の値は黙って使わない）。
            OrbProfile p;
            std::string perr;
            check(parse_orb_layout("{ \"cx_hp\": 200.5, \"cx_mp\": 500, \"cy\": 120, \"r\": 90, \"art_bottom_y\": 248, "
                                   "\"emblem_x\": 350, \"emblem_y\": 125, \"emblem_r\": 20 }", p, perr)
                    && (p.hp_cx == 200.5) && (p.mp_cx == 500.0) && (p.r == 90.0) && (p.art_bottom_y == 248) && p.emblem.present
                    && (p.emblem.x == 350.0) && (p.number_y == 217.0),
                "配置の読み取り: 書いた項目は上書きし、無い項目（名札・数字）は既定のまま");
            OrbProfile q;
            check(!parse_orb_layout("{ \"cx_hp\": 200, \"cx_mp\": 500, \"cy\": 120 }", q, perr) && same_as_legacy(q),
                "配置の読み取り: 必須の項目（r）が無ければ失敗し、値は動かない");
            check(!parse_orb_layout("{ \"cx_hp\": 20, \"cx_mp\": 500, \"cy\": 120, \"r\": 90 }", q, perr) && same_as_legacy(q),
                "配置の読み取り: 玉が設計の範囲（760x250）からはみ出す値は失敗する");
            OrbProfile e;
            check(parse_orb_layout("{ \"cx_hp\": 200, \"cx_mp\": 500, \"cy\": 120, \"r\": 90, \"emblem_x\": 350 }", e, perr) && !e.emblem.present,
                "配置の読み取り: 飾りは 3 項目（x・y・r）が揃ったときだけ");
        }

        // ---- 2. 全コアの素材・範囲・名札と数字の帯 ----
        const auto surface_bottom_row = [](const std::string &path) {
            //! 前の層の不透明な画素（α >= 128）の最下段（素材の画素）。読めなければ -1。
            SDL_Surface *raw = IMG_Load(path.c_str());
            if (raw == nullptr) {
                return -1;
            }
            SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(raw);
            if (rgba == nullptr) {
                return -1;
            }
            int last = -1;
            const unsigned char *px = static_cast<const unsigned char *>(rgba->pixels);
            for (int y = rgba->h - 1; (y >= 0) && (last < 0); --y) {
                for (int x = 0; x < rgba->w; ++x) {
                    if (px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch)) + (static_cast<std::size_t>(x) * 4) + 3] >= 128) {
                        last = y;
                        break;
                    }
                }
            }
            SDL_FreeSurface(rgba);
            return last;
        };
        const char *const kLayerNames[] = { "orb_back", "orb_glass", "orb_liquid_hp", "orb_liquid_mp", "orb_glass_top", "orb_front" };
        UiImage *const kLayerImages[] = { &back_image, &glass_image, &liquid_hp_image, &liquid_mp_image, &glass_top_image, &front_image };
        //! 6 層を読む（全部読めたら true）。
        const auto load_layers = [&](const std::string &asset_key) {
            bool all = true;
            for (int i = 0; i < 6; ++i) {
                std::string load_err;
                all = kLayerImages[i]->load_minified(orb_layer_path(asset_key, kLayerNames[i]), load_err) && all;
            }
            return all;
        };
        constexpr double kLabelH = 36.0; //!< 名札の高さ（設計の単位。1.5 倍の字 ≒ 24 画素 x 1.5。2560x1080 の見た目から）
        constexpr double kNumberH = 24.0; //!< 数字の帯の高さ
        for (const CoreCase &c : kCores) {
            std::string tag = std::string("[") + c.core_name + "] ";
            std::string note;
            const OrbProfile p = load_orb_profile(c.core_name, note);
            check(note.empty() && (p.asset_key == c.key), tag + "配置を読めて、コアの素材の組が選ばれる（assets/ui/hud/" + c.key + "）" + (note.empty() ? "" : " / " + note));
            if (std::string(c.key).empty()) {
                continue; // 変愚は上で見た
            }
            check((p.r != legacy.r) && (p.hp_cx != legacy.hp_cx) && (p.art_bottom_y != legacy.art_bottom_y || p.cy != legacy.cy),
                tag + "変愚の定数ではなく、コアの配置になっている（半径 " + std::to_string(p.r) + " / 左の中心 " + std::to_string(p.hp_cx) + "）");
            const bool in_range = ((p.hp_cx - p.r) >= 0.0) && ((p.mp_cx + p.r) <= kOrbDesignW) && (p.hp_cx < p.mp_cx)
                && ((p.cy - p.r) >= 0.0) && ((p.cy + p.r) <= kOrbDesignH) && ((p.mp_cx - p.hp_cx) >= (2.0 * p.r));
            check(in_range, tag + "2 つの玉は設計の範囲（760x250）に収まり、重ならない");
            //! 名札・数字の帯は、矩形の中で、お互いに重ならず、玉の円の内側に中心がある（玉の外の枠は薄くて入らないコアがある）。
            const double label_top = p.label_bottom_y - kLabelH;
            const double number_top = p.number_y - (kNumberH / 2.0);
            const double number_bottom = p.number_y + (kNumberH / 2.0);
            const double label_mid = p.label_bottom_y - (kLabelH / 2.0);
            const bool bands_ok = (label_top >= 0.0) && (number_bottom <= kOrbDesignH) && (p.label_bottom_y <= number_top)
                && (std::fabs(label_mid - p.cy) <= p.r) && (std::fabs(p.number_y - p.cy) <= p.r);
            check(bands_ok, tag + "名札（" + std::to_string(label_top) + "〜" + std::to_string(p.label_bottom_y) + "）と数字の帯（"
                    + std::to_string(number_top) + "〜" + std::to_string(number_bottom) + "）は矩形の中で、重ならず、玉の中");
            //! 数字の帯の幅（玉の幅の 1.15 倍）が、帯の高さでの玉の弦に収まる（はみ出すと枠の上に乗る）。
            {
                const double dy = std::max(std::fabs(number_top - p.cy), std::fabs(number_bottom - p.cy));
                const double chord = (dy < p.r) ? (2.0 * std::sqrt((p.r * p.r) - (dy * dy))) : 0.0;
                check(chord >= (p.r * 1.15), tag + "数字の帯は玉の中に収まる（帯の端での弦 " + std::to_string(chord) + " / 帯の幅 "
                        + std::to_string(p.r * 1.15) + "）");
            }
            const bool loaded = load_layers(p.asset_key);
            bool sizes_ok = loaded;
            for (int i = 0; i < 6; ++i) {
                sizes_ok = sizes_ok && (kLayerImages[i]->width() == 1013) && (kLayerImages[i]->height() == 333);
            }
            check(sizes_ok, tag + "玉の 6 層が読めて、どれも 1013x333（設計の 4/3 倍）");
            //! 配置の「絵の見えている下端」は、前の層の実際の画素から測り直した値と一致する（素材と配置のずれ）。
            const int bottom_row = surface_bottom_row(orb_layer_path(p.asset_key, "orb_front"));
            const int measured = std::min(kOrbDesignH, static_cast<int>(std::ceil(static_cast<double>(bottom_row + 1) / (4.0 / 3.0))) + 2);
            check((bottom_row >= 0) && (p.art_bottom_y == measured), tag + "絵の見えている下端 " + std::to_string(p.art_bottom_y)
                    + " は前の層の画素（最下段 " + std::to_string(bottom_row) + "）から測った値 " + std::to_string(measured) + " と一致");
            check(p.emblem.present == (std::string(c.key) == "tangband"), tag + "中央の飾りの位置があるのは短愚だけ");
            if (p.emblem.present) {
                const bool between = (p.emblem.x > (p.hp_cx + p.r)) && (p.emblem.x < (p.mp_cx - p.r)) && (std::fabs(p.emblem.y - p.cy) < 8.0)
                    && (p.emblem.r > 10.0) && (p.emblem.r < 30.0);
                check(between, tag + "飾りは 2 つの玉の間にあり、大きさは玉の半径の 1/4 前後（r = " + std::to_string(p.emblem.r) + "）");
            }
        }
        {
            //! 変愚の素材も 6 層が読めて 1013x333（既存の検査は 6 層の有無だけ見ていた）。
            const bool ok = load_layers("");
            bool sizes_ok = ok;
            for (int i = 0; i < 6; ++i) {
                sizes_ok = sizes_ok && (kLayerImages[i]->width() == 1013) && (kLayerImages[i]->height() == 333);
            }
            check(sizes_ok, "[hengband] 玉の 6 層が読めて、どれも 1013x333");
            const int bottom_row = surface_bottom_row(orb_layer_path("", "orb_front"));
            const int measured = std::min(kOrbDesignH, static_cast<int>(std::ceil(static_cast<double>(bottom_row + 1) / (4.0 / 3.0))) + 2);
            check(measured == legacy.art_bottom_y,
                "[hengband] 配置の測り方（前の層の不透明な画素の最下段 + 2）を変愚の素材に当てると 230 になる（今の定数と同じ測り方。測った値 "
                    + std::to_string(measured) + "）");
        }

        // ---- 3. 配置ごとの置き場（地図の下縁・メッセージの帯・名前の行・アイコンの段） ----
        {
            const std::array<LayoutMode, 3> kModes = { LayoutMode::Full, LayoutMode::Hybrid, LayoutMode::Split };
            const std::array<std::array<int, 2>, 3> kSizes = { { { 2560, 1080 }, { 1280, 720 }, { 1080, 1920 } } };
            bool all_ok = true;
            std::string first_bad;
            int combos = 0;
            const auto note_bad = [&](bool ok, const std::string &what) {
                if (!ok) {
                    all_ok = false;
                    if (first_bad.empty()) {
                        first_bad = what;
                    }
                }
            };
            for (const CoreCase &c : kCores) {
                std::string note;
                const OrbProfile p = load_orb_profile(c.core_name, note);
                set_orb_profile(p);
                for (const LayoutMode mode : kModes) {
                    for (const std::array<int, 2> &size : kSizes) {
                        const std::string where = std::string(c.core_name) + " / " + std::to_string(size[0]) + "x" + std::to_string(size[1])
                            + " / mode " + std::to_string(static_cast<int>(mode));
                        const UiLayout layout = UiLayout::compute(mode, size[0], size[1], text.cell_w(), text.cell_h(), false);
                        ++combos;
                        const RectPx &orb = layout.orb;
                        if (orb.empty()) {
                            continue; // 極端に狭い窓は玉を置かない（`orb_h < kOrbHeightMin / 2`）。他の検査が見ている
                        }
                        const RectPx shown = orb_visible_rect(orb);
                        const long long shown_area = static_cast<long long>(shown.w) * static_cast<long long>(shown.h);
                        note_bad(rect_overlap_area(shown, layout.scene) == shown_area, where + ": 玉（見えている部分）が地図の外");
                        int want_bottom = layout.scene.y + layout.scene.h;
                        if (!layout.bottom_bar.empty()) {
                            want_bottom = std::min(want_bottom, layout.bottom_bar.y);
                        }
                        note_bad((shown.y + shown.h) == want_bottom, where + ": 絵の下端が地図の下縁に接していない");
                        note_bad(shown.h == static_cast<int>((static_cast<long long>(orb.h) * p.art_bottom_y + (kOrbDesignH / 2)) / kOrbDesignH),
                            where + ": 見えている高さが配置の下端と違う");
                        for (const RectPx *rect : { &layout.bottom_bar, &layout.status_col, &layout.hud, &layout.message_bar }) {
                            note_bad(rect_overlap_area(shown, *rect) == 0, where + ": 玉が最下段・状態列・名前の行・メッセージの帯と重なる");
                        }
                        //! 玉の横のアイコン（左右の段）と常時の列は、玉の矩形（翼を含む）に入らない。
                        const StatusIconLayout placed = layout_status_icons(text, layout, base);
                        for (const StatusIconPlacement &item : placed.items) {
                            RectPx vis = item.cell;
                            if (item.rotating) {
                                const int x0 = std::max(item.clip.x, item.cell.x);
                                const int x1 = std::min(item.clip.x + item.clip.w, item.cell.x + item.cell.w);
                                vis = RectPx{ x0, item.cell.y, std::max(0, x1 - x0), item.cell.h };
                            }
                            note_bad(rect_overlap_area(vis, orb) == 0, where + ": 玉の横のアイコンが玉の矩形に入っている");
                            note_bad(!item.left || ((rect_overlap_area(vis, layout.message_bar) == 0) && (rect_overlap_area(vis, layout.hud) == 0)),
                                where + ": 左のアイコンがメッセージの帯か名前の行と重なる");
                        }
                        const PermanentIconLayout permanent = layout_permanent_icons(text, layout, base);
                        for (const PermanentIconPlacement &item : permanent.items) {
                            note_bad(rect_overlap_area(item.cell, orb) == 0, where + ": 常時の列が玉の矩形に入っている");
                        }
                    }
                }
            }
            set_orb_profile(OrbProfile{});
            check(all_ok, "全コアの配置 x 3 つの作り x 3 つの大きさ（" + std::to_string(combos) + " 通り）で、玉は地図の下縁に接し、最下段・状態列・名前の行・メッセージの帯・アイコンの段と重ならない"
                    + (first_bad.empty() ? "" : " / 最初の失敗: " + first_bad));
        }

        // ---- 4. 全コアの玉を描いて書き出す（人が見る用）。変愚と別の絵であること ----
        const int kW = 2560;
        const int kH = 1080;
        StatusHudTarget big;
        if (make_status_hud_target(big, kW, kH)) {
            std::vector<unsigned char> heng_crop;
            std::vector<std::vector<unsigned char>> crops; //!< 上下が反転していない（左上原点）。玉の矩形 + 左右上下の余白
            RectPx crop_rect{};
            std::vector<std::string> crop_names;
            for (const CoreCase &c : kCores) {
                std::string note;
                const OrbProfile p = load_orb_profile(c.core_name, note);
                set_orb_profile(p);
                const bool loaded = load_layers(p.asset_key);
                const std::string tag = std::string("[") + c.core_name + "] ";
                check(loaded && backdrops.orb.has_orb(), tag + "玉の素材を読み直した");
                HudState st;
                orb_hud_update(st.orb_anim, st.orb_params, 1.f, 1.f, 0u);
                advance(st, ratio_hp(base), ratio_mp(base), 0u, 3000u);
                const std::vector<unsigned char> shot = render(big, base, st, 3000u);
                check(drain_gl_errors().empty(), tag + "描いて GL エラーが出ない");
                const std::string suffix = std::string("_core_") + (std::string(c.key).empty() ? "hengband" : c.key);
                check(save_rgba_png(out_path(kW, kH, suffix.c_str()), shot.data(), kW, kH), tag + "画像を書き出せた " + out_path(kW, kH, suffix.c_str()));
                //! 玉の矩形の中の、玉の絵が見えている所に、背景の市松以外の画素がある（描かれている）
                const UiLayout layout = make_layout(kW, kH);
                const RectPx shown = orb_visible_rect(layout.orb);
                //! 切り出しの枠は全コアで同じ（見えている下端はどのコアでも地図の下縁に接し、玉の高さも同じ）。
                crop_rect = RectPx{ layout.orb.x - 40, (shown.y + shown.h) - layout.orb.h - 10, layout.orb.w + 80, layout.orb.h + 30 };
                crop_rect.y = std::max(0, crop_rect.y);
                crop_rect.h = std::min(kH - crop_rect.y, crop_rect.h);
                std::vector<unsigned char> crop(static_cast<std::size_t>(crop_rect.w) * static_cast<std::size_t>(crop_rect.h) * 4, 0);
                for (int y = 0; y < crop_rect.h; ++y) {
                    for (int x = 0; x < crop_rect.w; ++x) {
                        const int sx = std::clamp(crop_rect.x + x, 0, kW - 1);
                        const int sy = crop_rect.y + y;
                        const std::size_t from = ((static_cast<std::size_t>(kH - 1 - sy) * static_cast<std::size_t>(kW)) + static_cast<std::size_t>(sx)) * 4;
                        const std::size_t to = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(crop_rect.w)) + static_cast<std::size_t>(x)) * 4;
                        for (std::size_t k = 0; k < 4; ++k) {
                            crop[to + k] = shot[from + k];
                        }
                    }
                }
                if (std::string(c.key).empty()) {
                    heng_crop = crop;
                } else {
                    check(!heng_crop.empty() && (heng_crop.size() == crop.size()) && (count_changed_pixels(heng_crop, crop, 8) > 20000),
                        tag + "変愚とは別の絵が描かれている（玉の周りで変わった画素 " + std::to_string(count_changed_pixels(heng_crop, crop, 8)) + "）");
                }
                crops.push_back(std::move(crop));
                crop_names.push_back(c.core_name);
            }
            //! 見本の 1 枚（5 行 x 2 列。左上から 変愚・短愚・幻想・Sil-Q・Frox・Angband・Umoria・Rogue 3.6・Rogue 5.4・RogueClone2）。
            if (!crops.empty()) {
                const int cols = 2;
                const int rows = static_cast<int>((crops.size() + 1) / 2);
                const int cw = crop_rect.w;
                const int ch = crop_rect.h;
                const int sheet_w = cw * cols;
                const int sheet_h = ch * rows;
                std::vector<unsigned char> sheet(static_cast<std::size_t>(sheet_w) * static_cast<std::size_t>(sheet_h) * 4, 255);
                for (std::size_t i = 0; i < crops.size(); ++i) {
                    const int ox = static_cast<int>(i % 2) * cw;
                    const int oy = static_cast<int>(i / 2) * ch;
                    for (int y = 0; y < ch; ++y) {
                        //! `save_rgba_png` は下から上の並びを受け取る。
                        const std::size_t to_row = static_cast<std::size_t>(sheet_h - 1 - (oy + y));
                        for (int x = 0; x < cw; ++x) {
                            const std::size_t from = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(cw)) + static_cast<std::size_t>(x)) * 4;
                            const std::size_t to = ((to_row * static_cast<std::size_t>(sheet_w)) + static_cast<std::size_t>(ox + x)) * 4;
                            for (std::size_t k = 0; k < 4; ++k) {
                                sheet[to + k] = crops[i][from + k];
                            }
                        }
                    }
                }
                const std::string sheet_path = stem + "_cores_sheet" + ext;
                check(save_rgba_png(sheet_path, sheet.data(), sheet_w, sheet_h), "全コアの玉の見本を書き出せた " + sheet_path);
            }

            // ---- 5. 短愚の終末の太陽（残り日数 14・7・1・0 と、無効） ----
            {
                std::string note;
                const OrbProfile tang = load_orb_profile("tangband", note);
                set_orb_profile(tang);
                check(load_layers(tang.asset_key) && tang.emblem.present, "[終末の太陽] 短愚の素材と飾りの位置が読めた");
                const UiLayout layout = make_layout(kW, kH);
                const RectPx &orb = layout.orb;
                const auto design_x = [&](double d) { return static_cast<double>(orb.x) + (d * static_cast<double>(orb.w) / kOrbDesignW); };
                const auto design_y = [&](double d) { return static_cast<double>(orb.y) + (d * static_cast<double>(orb.h) / kOrbDesignH); };
                const auto rgb_at = [&](const std::vector<unsigned char> &img, int x, int y, int out[3]) {
                    x = std::clamp(x, 0, kW - 1);
                    y = std::clamp(y, 0, kH - 1);
                    const std::size_t at = ((static_cast<std::size_t>(kH - 1 - y) * static_cast<std::size_t>(kW)) + static_cast<std::size_t>(x)) * 4;
                    out[0] = img[at];
                    out[1] = img[at + 1];
                    out[2] = img[at + 2];
                };
                const auto luma = [](const int c[3]) { return ((0.30 * c[0]) + (0.59 * c[1]) + (0.11 * c[2])) / 255.0; };
                const auto near_color = [](const int c[3], const PaintColor &want, int tolerance) {
                    return (std::abs(c[0] - static_cast<int>(std::lround(want.r * 255.f))) <= tolerance)
                        && (std::abs(c[1] - static_cast<int>(std::lround(want.g * 255.f))) <= tolerance)
                        && (std::abs(c[2] - static_cast<int>(std::lround(want.b * 255.f))) <= tolerance);
                };
                //! 目盛り 1 本の中心の周りの明るさ（3x3 の最大。傾いた細い四角でも拾う）。
                const auto tick_luma = [&](const std::vector<unsigned char> &img, int i) {
                    const double angle = (-1.5707963267948966) + (6.283185307179586 * static_cast<double>(i) / kDoomsdayTicks);
                    const int cx = static_cast<int>(std::lround(design_x(tang.emblem.x + (std::cos(angle) * tang.emblem.r * 1.62))));
                    const int cy = static_cast<int>(std::lround(design_y(tang.emblem.y + (std::sin(angle) * tang.emblem.r * 1.62))));
                    double best = 0.0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int c[3];
                            rgb_at(img, cx + dx, cy + dy, c);
                            best = std::max(best, luma(c));
                        }
                    }
                    return best;
                };
                const int ex = static_cast<int>(std::lround(design_x(tang.emblem.x)));
                const int ey = static_cast<int>(std::lround(design_y(tang.emblem.y)));
                std::vector<unsigned char> shot_disabled;
                std::vector<unsigned char> shot_first;
                double last_luma = 2.0;
                for (const int days : { 14, 7, 1, 0, -1 }) {
                    const std::string tag = std::string("[終末の太陽 残り ") + ((days < 0) ? std::string("無効") : std::to_string(days)) + "] ";
                    GameFrame f = base;
                    f.hud.doomsday_days_left = days;
                    HudState st;
                    orb_hud_update(st.orb_anim, st.orb_params, 1.f, 1.f, 0u);
                    advance(st, ratio_hp(f), ratio_mp(f), 0u, 3000u);
                    const std::vector<unsigned char> shot = render(big, f, st, 3000u);
                    check(drain_gl_errors().empty(), tag + "描いて GL エラーが出ない");
                    int center[3];
                    rgb_at(shot, ex, ey, center);
                    if (days >= 0) {
                        const DoomsdaySunLook look = doomsday_sun_look(days);
                        check(near_color(center, look.disc, 10), tag + "太陽の円の色が期待どおり（描いた色 " + std::to_string(center[0]) + ","
                                + std::to_string(center[1]) + "," + std::to_string(center[2]) + " / 期待 "
                                + std::to_string(static_cast<int>(std::lround(look.disc.r * 255.f))) + ","
                                + std::to_string(static_cast<int>(std::lround(look.disc.g * 255.f))) + ","
                                + std::to_string(static_cast<int>(std::lround(look.disc.b * 255.f))) + "）");
                        int lit = 0;
                        for (int i = 0; i < kDoomsdayTicks; ++i) {
                            lit += (tick_luma(shot, i) > 0.5) ? 1 : 0;
                        }
                        check(lit == days, tag + "明るい目盛りは " + std::to_string(days) + " 本（描いた数 " + std::to_string(lit) + " / 全 "
                                + std::to_string(kDoomsdayTicks) + " 本）");
                        check(look.lit_ticks == days && (look.doomsday == (days == 0)), tag + "見た目の記述（明るい目盛りの数・終末の旗）が期待どおり");
                        const double lum = luma(center);
                        check((days == 14) ? ((center[0] > 230) && (center[1] > 190) && (center[2] < 90)) // 黄色
                              : (days == 7)  ? (lum > 0.15 && lum < 0.75 && center[0] > center[2])      // 橙〜燃えさし
                              : (days == 1)  ? (lum < 0.12)                                              // ほぼ黒い日食
                                             : ((lum < 0.08) && (center[0] <= 12)),                      // 真っ黒
                            tag + "円の色の段階（残り 14 は黄色 / 7 は橙〜赤 / 1 はほぼ黒 / 0 は真っ黒）");
                        if (days >= 1) {
                            check(lum < last_luma, tag + "日が進むほど円は暗い（明るさ " + std::to_string(lum) + " < 直前 " + std::to_string(last_luma) + "）");
                            last_luma = lum;
                        }
                        if (days == 0) {
                            //! 終末は赤い光（円のすぐ外が赤い）。
                            int rim[3];
                            rgb_at(shot, ex + static_cast<int>(std::lround(tang.emblem.r * 1.15 * orb.w / kOrbDesignW)), ey, rim);
                            check((rim[0] > 70) && (rim[0] > (rim[1] * 2)) && (rim[0] > (rim[2] * 2)),
                                tag + "黒い太陽の周りは赤い光（円のすぐ外 " + std::to_string(rim[0]) + "," + std::to_string(rim[1]) + "," + std::to_string(rim[2]) + "）");
                        }
                        const std::string suffix = std::string("_doomsday_") + std::to_string(days);
                        check(save_rgba_png(out_path(kW, kH, suffix.c_str()), shot.data(), kW, kH), tag + "画像を書き出せた " + out_path(kW, kH, suffix.c_str()));
                        if (days == 14) {
                            shot_first = shot;
                        }
                    } else {
                        shot_disabled = shot;
                        int lit = 0;
                        for (int i = 0; i < kDoomsdayTicks; ++i) {
                            lit += (tick_luma(shot, i) > 0.5) ? 1 : 0;
                        }
                        check(luma(center) < 0.25 && lit == 0, tag + "太陽は描かれず、絵のまま（中央の明るさ " + std::to_string(luma(center)) + " / 明るい目盛り " + std::to_string(lit) + "）");
                        const RectPx emblem_box{ static_cast<int>(design_x(tang.emblem.x - (tang.emblem.r * 2.0))), static_cast<int>(design_y(tang.emblem.y - (tang.emblem.r * 2.0))),
                            static_cast<int>(tang.emblem.r * 4.0 * orb.w / kOrbDesignW), static_cast<int>(tang.emblem.r * 4.0 * orb.h / kOrbDesignH) };
                        check(!shot_first.empty() && (count_changed_pixels(shot_first, shot_disabled, kW, kH, emblem_box) > 500),
                            tag + "無効のときと残り 14 のときで、飾りの周りの絵が違う");
                        check(save_rgba_png(out_path(kW, kH, "_doomsday_off"), shot.data(), kW, kH), tag + "画像を書き出せた " + out_path(kW, kH, "_doomsday_off"));
                    }
                }
                //! 飾りの無いコアは、終末の日数が届いても太陽を描かない（短愚だけの見た目）。
                for (const char *other : { "frox", "hengband" }) {
                    std::string note2;
                    const OrbProfile p = load_orb_profile(other, note2);
                    set_orb_profile(p);
                    load_layers(p.asset_key);
                    GameFrame with = base;
                    with.hud.doomsday_days_left = 7;
                    GameFrame without = base;
                    HudState st_a;
                    HudState st_b;
                    for (HudState *st : { &st_a, &st_b }) {
                        orb_hud_update(st->orb_anim, st->orb_params, 1.f, 1.f, 0u);
                        advance(*st, ratio_hp(base), ratio_mp(base), 0u, 3000u);
                    }
                    const std::vector<unsigned char> a = render(big, with, st_a, 3000u);
                    const std::vector<unsigned char> b = render(big, without, st_b, 3000u);
                    check(count_changed_pixels(a, b, 0) == 0, std::string("[終末の太陽] ") + other + ": 飾りの位置が無いコアは、日数が届いても太陽を描かない");
                }
            }
        } else {
            check(false, "画面の外の描画先（2560x1080。コアごとの見本）を作れません");
        }
        release_status_hud_target(big);
        set_orb_profile(OrbProfile{});
        load_layers("");
    }

    {
        //! 送らないコア（`status_effects` が空）ではアイコンは 1 つも置かれない。
        GameFrame empty = base;
        empty.status_effects.clear();
        const UiLayout layout = make_layout(2560, 1080);
        const StatusIconLayout none = layout_status_icons(text, layout, empty);
        check(none.items.empty() && (none.dropped == 0), "status_effects が空なら、アイコンは何も置かれない");
    }

    back_image.unload();
    glass_image.unload();
    liquid_hp_image.unload();
    liquid_mp_image.unload();
    glass_top_image.unload();
    front_image.unload();
    icons_image.unload();
    images.shutdown();
    paint.shutdown();
    text.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    std::fprintf(stderr, "[hd2d] status-hud-check: %d 件 PASS / %d 件 FAIL\n", passes, failures);
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

} // namespace hd2d
