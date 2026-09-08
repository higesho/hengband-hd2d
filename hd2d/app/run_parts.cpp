/*!
 * @file run_parts.cpp
 * @brief `run()` から切り出した部品の中身。宣言は `app/run_parts.h`。
 */
#include "app/run_parts.h"

#include "net/core_link.h"
#include "assets/tile_catalog.h"
#include "ui/game_pad.h"
#include "ui/feature_menu.h"
#include "ui/hd2d_settings.h"
#include "frame/protocol_messages.h"
#include <string>
#include <vector>
#include <utility>
#include "xr/xr_room.h"    //!< `xr::UiSurface`
#include "xr/xr_session.h"
#include "xr/xr_fake.h"
#include "xr/xr_math.h"
#include "app/app_clock.h"
#include "ui/game_hud.h"
#include "ui/combat_fx_view.h"
#include "render/term_colors.h"
#include "frame/frame_codec.h"
#include "frame/sound_event.h"
#include "ui/fps_mode.h"
#include "render/render_view.h" //!< `RenderView`
#include "render/camera.h"
#include "render/math3d.h"
#include "ui/ui_layout.h"
#include "frame/game_frame.h"
#include "frame/minimap_snapshot.h"

#include "ui/key_binds.h"     //!< `kAction*` / `kMod*`
#include "ui/ui_cursor.h"     //!< `CursorNav` / `handle_text_edit_arrows`
#include "render/gl_core.h"   //!< `glViewport`
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace hd2d::gl; //!< GL の関数は `hd2d::gl` に居る（`gl_core.h`。`app_support.cpp` と同じ）

namespace hd2d {

ArenaWatch apply_arena_watch(const GameFrame &frame, bool force, bool first_person_active,
    const UiLayout &layout, Camera &camera)
{
    /*
     * 闘技場の観戦カメラ（設計書 §4.6。2026-08-24 に決めた「かけが始まった時点で
     * カメラの位置を闘技場の中心に移し、DOF を外し、一番ひきの状態にして闘技場が
     * 映るようにしよう。これはどのコアの闘技場も同じ」）。
     *
     * 賭け試合ではプレイヤは端で観戦するだけなので、追従カメラでは**試合が画面の外**になる。
     * 合図は `FloorKind::Arena`（v1 §12-2。コア固有の知識を持たない——変愚・短愚は
     * `inside_arena ‖ phase_out`、幻想は `inside_arena ‖ inside_battle` をアダプタが畳む）。
     * 中心と大きさはミニマップ（フロア全域）から採る。闘技場のフロアは小さいので必ず載っている。
     *
     * **一人称が勝つ**（観戦中でも一人称に切り替えたら、そちらの視点を尊重する）。
     * この旗はこの下の 3 か所も見る: なめらか移動の注視点書き戻し（止める）・
     * 可視窓（フロア全域まで広げる）・DOF（切る）。
     */
    ArenaWatch out;
    bool arena_watch = (force || (frame.floor.kind == static_cast<int>(FloorKind::Arena)))
        && frame.minimap.valid() && !first_person_active;
    int arena_w = 0;
    int arena_h = 0;
    if (arena_watch) {
        /*
         * 場内は**フロアの中心とは限らない**。幻想蛮怒の `battle_gen()`（`generate.c:1944`）は
         * フロア寸法を 198×66 のまま**全面を記憶済みの永久壁で埋め、左上に 66×22 の場内を
         * 彫る**——寸法の中心を見ると岩盤しか映らない（実測 2026-08-24。@ は (33,2) に立つ）。
         * だから**床の連なりの外接矩形**をミニマップから測る。壁（2）・未踏（0）・山（8）
         * 以外が「場内」である。13K マス走査は 1 フレームに数マイクロ秒で済む。
         */
        int min_x = frame.minimap.width;
        int max_x = -1;
        int min_y = frame.minimap.height;
        int max_y = -1;
        for (int gy = 0; gy < frame.minimap.height; ++gy) {
            for (int gx = 0; gx < frame.minimap.width; ++gx) {
                const uint8_t kind
                    = frame.minimap.kinds[static_cast<std::size_t>(gy) * frame.minimap.width + gx];
                if ((kind == static_cast<uint8_t>(MinimapKind::Unknown))
                    || (kind == static_cast<uint8_t>(MinimapKind::Wall))
                    || (kind == static_cast<uint8_t>(MinimapKind::Mountain))) {
                    continue;
                }
                min_x = std::min(min_x, gx);
                max_x = std::max(max_x, gx);
                min_y = std::min(min_y, gy);
                max_y = std::max(max_y, gy);
            }
        }
        if (max_x < min_x) {
            arena_watch = false; //!< 床が 1 マスも無い＝測れない。追従のまま（捏造しない）
        } else {
            arena_w = (max_x - min_x) + 1;
            arena_h = (max_y - min_y) + 1;
            camera.target = Vec3{ (static_cast<float>(min_x + max_x) + 1.f) * 0.5f,
                (static_cast<float>(min_y + max_y) + 1.f) * 0.5f, 0.f };
            /*
             * 見下ろしは **70°** へ（`hd2d/app/checks/world_checks.cpp` の検査も同じ値）。
             * 普段の 46° では**南側の「記憶済みの永久壁」の塊が台地になって場内を隠す**
             * （実測 2026-08-24。幻想蛮怒の battle_gen はフロアの残り全面が壁で、
             * 場外の壁も CAVE_MARK 済みなので描かれる）。ピッチは毎フレーム設定から
             * 書き直される（上の `camera.pitch = settings...`）ので、抜ければ元へ戻る。
             */
            camera.pitch = 70.f * 0.0174533f;
            /*
             * 「一番ひき」＝設定で選べる下限の 24px/マス（`hd2d_settings.cpp` の clamp と同値）。
             * それでも入らない大きさなら、場内が入るまでさらに引く。縦は見下ろしの
             * 縮み `sin(pitch)` の近似で見積る（厳密な視錐台合わせまでは要らない——
             * 余白が数マス増えるだけで、欠けない方向にしか外れない）。
             */
            float cell_px = 24.f;
            const float scene_w = static_cast<float>(std::max(1, layout.scene.w));
            const float scene_h = static_cast<float>(std::max(1, layout.scene.h));
            const float fit_w = scene_w / static_cast<float>(arena_w + 4);
            const float fit_h = scene_h
                / (static_cast<float>(arena_h + 4) * std::max(0.2f, std::sin(camera.pitch)));
            cell_px = std::max(6.f, std::min(cell_px, std::min(fit_w, fit_h)));
            camera.distance = distance_for_cell_px(camera, cell_px);
        }
    }
    out.active = arena_watch;
    out.width = arena_w;
    out.height = arena_h;
    return out;
}

/*!
 * @brief コアから届いた表を取り込む（v1 §8）。
 *
 * @details **`asset_manifest` はビルボードの目録**（v1 §8.2）、`pad_commands` は
 * パッドに割り当てられるコマンド、`macro_triggers` は同時押しの引き金、
 * `sub_panel_kinds` はサブパネルに出せる種類。
 *
 * **表を足したらここにも足すこと。**忘れると届いているのに誰も読まない
 * （`net/core_link.cpp` の `dispatch()` と対になっている）。
 */
void take_core_tables(CoreLink &link, TileCatalog &tile_catalog,
    std::vector<PadCommand> &pad_commands, PadMacroFlags &pad_macros,
    Hd2dSettings &settings, std::vector<SubPanelKindChoice> &sub_panel_kind_choices)
{
    // (0) コアから届いた表。**`asset_manifest` はビルボードの目録**（v1 §8.2）。
    for (const auto &payload : link.take_table_payloads()) {
        const std::string type = presentation::peek_message_type(payload);
        if (type == "asset_manifest") {
            presentation::AssetManifestMessage manifest;
            std::string manifest_err;
            if (presentation::decode_asset_manifest(payload, manifest, manifest_err)) {
                tile_catalog.apply(manifest);
            } else {
                std::fprintf(stderr, "[hd2d] bad \"asset_manifest\": %s\n", manifest_err.c_str());
            }
            continue;
        }
        if (type == "pad_commands") {
            /*
             * パッドに割り当てられるコマンド（v1 §8）。**キー列までコアが解決して送ってくる**
             * ので、こちらは「どのキーがどのコマンドか」を持たない。
             * キー配列を切り替えると再送されるため、そのまま入れ替える。
             */
            presentation::PadCommandsMessage table;
            std::string table_err;
            if (presentation::decode_pad_commands(payload, table, table_err)) {
                const bool rogue = (table.current_keymap == "rogue");
                pad_commands.clear();
                for (const auto &entry : table.entries) {
                    PadCommand command;
                    command.id = entry.id;
                    //! **既定の割り当てはこの文字で引く**（`id` は表の並び順で動く）。
                    command.command = entry.command;
                    command.group_utf8 = entry.group_utf8;
                    //! 小分類（v1 の追補）。**送らないコアでは空**＝今までどおり 2 段。
                    command.subgroup_utf8 = entry.subgroup_utf8;
                    command.label_utf8 = entry.label_utf8;
                    command.keys = rogue ? entry.seq_rogue : entry.seq_original;
                    pad_commands.push_back(std::move(command));
                }
                std::fprintf(stderr, "[hd2d] pad_commands: %zu 件（%s）\n",
                    pad_commands.size(), table.current_keymap.c_str());
                /*
                 * **既定の割り当ては表が届いてから**（2026-08-11 に決めた）。
                 * コアのコマンド `id` は実行時にしか分からないので、cfg を読んだ時点では
                 * 書けない。入れたことは cfg に覚えさせて、**2 度目からは触らない**
                 * （触ると、利用者が外した割り当てが毎回復活する）。
                 */
                if (!settings.pad_defaults_applied && !pad_commands.empty()) {
                    settings.pad_defaults_applied = true;
                    if (apply_default_pad_binds(settings.pad, pad_commands,
                            link.hello_ack().core_name)) {
                        std::fprintf(stderr, "[hd2d] パッドの既定の割り当てを入れました（%s）\n",
                            settings.pad.to_line().c_str());
                    }
                }
            } else {
                std::fprintf(stderr, "[hd2d] bad \"pad_commands\": %s\n", table_err.c_str());
            }
            continue;
        }
        if (type == "macro_triggers") {
            /*
             * いま登録されているマクロのトリガー（v1 §8.4）。**マクロが増減すると
             * 送り直される**ので、そのまま入れ替えて畳み直す。
             * これが「このボタンにマクロが乗っているか」を知る唯一の道である
             * （押すときの判定は登録の有無を見ない——空振りはコアが飲む）。
             */
            presentation::MacroTriggersMessage triggers;
            std::string triggers_err;
            if (presentation::decode_macro_triggers(payload, triggers, triggers_err)) {
                pad_macros = make_pad_macro_flags(triggers.patterns);
                std::fprintf(stderr, "[hd2d] macro_triggers: %zu 件\n", triggers.patterns.size());
            } else {
                std::fprintf(stderr, "[hd2d] bad \"macro_triggers\": %s\n", triggers_err.c_str());
            }
            continue;
        }
        if (type == "sub_panel_kinds") {
            /*
             * どのサブウインドウを選べるか（v1 §8.1）。**番号と名前の対応はコアが持つ**ので、
             * 機能メニューに出す名前はここから採る（こちらで表を持つと版がずれる）。
             */
            presentation::SubPanelKindsMessage kinds;
            std::string kinds_err;
            if (presentation::decode_sub_panel_kinds(payload, kinds, kinds_err)) {
                sub_panel_kind_choices.clear();
                for (const auto &entry : kinds.entries) {
                    sub_panel_kind_choices.push_back(SubPanelKindChoice{ entry.flag, entry.label_utf8 });
                }
            } else {
                std::fprintf(stderr, "[hd2d] bad \"sub_panel_kinds\": %s\n", kinds_err.c_str());
            }
            continue;
        }
        std::fprintf(stderr, "[hd2d] received table: %s (%zu bytes)\n", type.c_str(), payload.size());
    }
}

/*!
 * @brief VR の画面の板を空間へ据える（§22）。
 *
 * @details **板のテクスチャは窓と同じ大きさでなければならない**（罠 30）。
 * ずれると板が別の場所を写す（実機で「サブパネルがちりぢり」として出た）。
 * 置き場所は「再中心のときの頭」であって、いまの頭ではない
 * （2026-08-14 に決めた「完全に空間固定」）。
 */
void place_vr_hud_panel(bool vr_frame_ready, xr::Session &xr, const xr::Frame &vr_frame,
    const xr::HeadAnchor &vr_anchor, const xr::UiSurface &vr_fake_ui,
    const Hd2dSettings &settings, const UiLayout &layout,
    int screen_w, int screen_h, float frame_seconds,
    float &vr_panel_yaw, std::vector<xr::UiPanel> &vr_panels)
{
    if (vr_frame_ready) {
        /*
         * **板のテクスチャは窓と同じ大きさでなければならない**（罠 30）。
         * 板の矩形は窓の座標で指すので、全画面へ切り替えたり窓を広げたりして
         * ここがずれると、**板が別の場所を写す**（実機で「サブパネルがちりぢり」
         * 「ミニマップが 2 つにちぎれる」として出た）。フレームの頭で見比べて、
         * 違っていれば作り直す。
         */
        if (xr.valid() && ((xr.ui_width() != screen_w) || (xr.ui_height() != screen_h))) {
            std::string ui_err;
            if (!xr.create_ui_layer(screen_w, screen_h, ui_err)) {
                std::fprintf(stderr, "[hd2d] %s（文字は HMD に出ません）\n", ui_err.c_str());
            }
        }
        /*
         * **向いている方角へゆっくり付いてくる**（§22）。頭に貼り付けると振り向きに
         * ついてきて疲れ、北に固定すると旋回して見えなくなる——遊びの角
         * （`vr_panel_follow_deg`）の外へ出たぶんだけ動かす。
         */
        xr::follow_panel_yaw(vr_panel_yaw, vr_frame.views[0].orientation, settings.vr_panel_follow_deg,
            frame_seconds);
        const float deg_per_px = settings.vr_panel_deg_per_cell / static_cast<float>(std::max(1, layout.cell_w));
        /*
         * **置き場所は「再中心のときの頭」**（`vr_anchor`）で、いまの頭では
         * ない（2026-08-14 に決めた「完全に空間固定」）。既定の
         * `vr_panel_follow_deg = 180` では向きも動かないので、板は本当に
         * 空間へ据わったままになる。小さくしていけば付いてくるようにもできる。
         */
        const xr::PanelPose pose = xr::place_hud_panel(vr_anchor.position, vr_panel_yaw, screen_w, screen_h,
            deg_per_px, settings.vr_panel_lift_deg, settings.vr_panel_dist_m);
        xr::UiPanel panel;
        panel.rect_x = 0;
        panel.rect_y = 0;
        panel.rect_w = screen_w;
        panel.rect_h = screen_h;
        panel.center[0] = pose.center.x;
        panel.center[1] = pose.center.y;
        panel.center[2] = pose.center.z;
        for (int k = 0; k < 4; ++k) {
            panel.orientation[k] = pose.orientation[k];
        }
        panel.width_m = pose.width_m;
        panel.height_m = pose.height_m;
        vr_panels.push_back(panel);
        if (xr.valid()) {
            xr.set_ui_panels(vr_panels.data(), static_cast<int>(vr_panels.size()));
        }
        /*
         * **中身が変わったときだけ言う。**窓と板の大きさが食い違っていないか、
         * 板がどれだけの大きさで出ているかを目で確かめる手段がここしか無い
         * （外出先の機械から持ち帰ってもらう記録がこれ）。
         */
        static std::string last_panel_summary;
        char line[220]{};
        const int surface_w = xr.valid() ? xr.ui_width() : vr_fake_ui.width();
        const int surface_h = xr.valid() ? xr.ui_height() : vr_fake_ui.height();
        std::snprintf(line, sizeof(line),
            "[hd2d] XR_PANEL 窓 %dx%d・板 %dx%d → %.2f×%.2fm（1 桁 %.2f 度・見上げ %.0f 度・遊び %.0f 度）",
            screen_w, screen_h, surface_w, surface_h, static_cast<double>(pose.width_m),
            static_cast<double>(pose.height_m), static_cast<double>(settings.vr_panel_deg_per_cell),
            static_cast<double>(settings.vr_panel_lift_deg),
            static_cast<double>(settings.vr_panel_follow_deg));
        if (line != last_panel_summary) {
            last_panel_summary = line;
            std::fprintf(stderr, "%s\n", line);
        }
    }
}

/*!
 * @brief 届いている最新のフレームを取り込む（v1 §6.4）。
 *
 * @details 無ければ最後のもので描き続ける（毎周コアを待たない）。取り込むときに
 * 色表を貰い、捨てたフレームから持ち越した縁と効果音を混ぜ、サブパネルの
 * 割り当てをコアと擦り合わせる。
 *
 * **色表はここで貰わないと、利用者が色を変えても 1 ドットも変わらない**
 * （`render/term_colors.h`）。
 */
void take_new_frame(CoreLink &link, GameFrame &frame, std::size_t &frame_bytes,
    HudState &hud_state, CombatFxView &combat_fx, Hd2dSettings &settings,
    bool &sub_kinds_synced, const std::vector<SubPanelKindChoice> &sub_panel_kind_choices,
    const int (&sent_sub_kinds)[kUiSubPanels],
    Uint64 perf_freq, double &ms_decode)
{
    // (c) 最新フレーム（無ければ最後のもので描き続ける。v1 §6.4）。
    std::string wire;
    bool carried_burst = false;
    std::vector<SoundEvent> carried_sounds;
    if (link.take_latest_frame(wire, carried_burst, carried_sounds)) {
        const Uint64 decode_start = clock::perf();
        GameFrame decoded{};
        std::string decode_err;
        const bool decoded_ok = presentation::decode_game_frame(wire, decoded, decode_err);
        ms_decode = static_cast<double>(clock::perf() - decode_start) * 1000.0
            / static_cast<double>(perf_freq);
        if (decoded_ok) {
            frame = std::move(decoded);
            frame_bytes = wire.size();
            /*
             * 色表を取り込む。`&`（カラーの設定）と pref の `V:` 行はコアの
             * `angband_color_table` を書き換えるので、**ここで貰わないと
             * 利用者が色を変えても 1 ドットも変わらない**（`render/term_colors.h`）。
             */
            set_term_palette(frame.term_palette);
            if (carried_burst) {
                frame.teleport_fx.burst = true;
            }
            /*
             * **捨てられたフレームの効果音を頭へ足す**（v1 §6.4 の救済。`burst` と同じ）。
             * 1 回のキーでコアは何枚もフレームを出すので、最新値スロットで古いほうを
             * 捨てると**そこに載っていた音は鳴らずに消える**（2026-08-21 に気づいた
             * 「効果音がたまにしかならなくなった」）。古い順に前へ置く。
             */
            if (!carried_sounds.empty()) {
                frame.sounds.insert(frame.sounds.begin(),
                    std::make_move_iterator(carried_sounds.begin()),
                    std::make_move_iterator(carried_sounds.end()));
            }
            //! 新しい画面になったらカーソルを取り込む（P8。画面が変われば頭へ戻る）。
            hud_state.cursors.sync(frame);
            /*
             * **コア側で変えられたサブパネルの中身を吸い上げる**（2026-08-19 に決めた
             * 「どちらで設定しても UI の設定に反映するように」）。
             *
             * `frame.sub_panels[i].kind` は v1 §7 が言う**実効値**である
             * （「ui のメニュー表示は実効値を使う」と書いてありながら、これまで
             * 画面側は `settings` を直に読んでいた）。コアの `=`→`w` や Ctrl-I で
             * 変えたぶんはここから返ってくる。
             *
             * **在庫のフレームで巻き戻らない**のは、コア側が「画面が同じ値を送り返す
             * まで抱える」形にしてあるから（`sdl_sub_window_terms.cpp` の
             * `g_core_override`）。こちらが往復のあいだ古い値を見せられることはない。
             *
             * @note 機能メニューを開いている間はコアへキーが流れないので、
             * 「両方から同時に変える」は起こらない（同時に操作できる画面が無い）。
             */
            /*
             * **往復が済むまでは吸い上げない**（2026-08-23 に気づいた。宣言の所の註記）。
             * 済んだ印は「返ってきた値が、送った値（コアが落とす番号は `-1`）と
             * 揃うこと」。落とす番号はコアの `sub_panel_kinds` が根拠である。
             */
            if (!sub_kinds_synced) {
                bool agreed = true;
                for (int i = 0; i < kUiSubPanels; ++i) {
                    int want = sent_sub_kinds[i];
                    if (want >= 0) {
                        bool offered = sub_panel_kind_choices.empty();
                        for (const auto &choice : sub_panel_kind_choices) {
                            if (choice.flag == want) {
                                offered = true;
                                break;
                            }
                        }
                        if (!offered) {
                            want = -1; //!< コアが持っていない番号は落ちて返ってくる
                        }
                    }
                    if (frame.sub_panels[static_cast<std::size_t>(i)].kind != want) {
                        agreed = false;
                        break;
                    }
                }
                sub_kinds_synced = agreed;
            }
            for (int i = 0; sub_kinds_synced && (i < kUiSubPanels); ++i) {
                const int kind = frame.sub_panels[static_cast<std::size_t>(i)].kind;
                if (kind != settings.sub_panel_kind[i]) {
                    settings.sub_panel_kind[i] = kind;
                }
            }
            /*
             * 戦闘の見せ場。
             * **フレームが来たときだけ**取り込む。毎フレーム取り込むと、同じ縁を
             * 何度も拾って演出が止まらなくなる（コア側は汲んだら空にしている）。
             */
            combat_fx.push(frame.combat_fx, clock::ticks());
        } else {
            std::fprintf(stderr, "[hd2d] bad \"frame\": %s\n", decode_err.c_str());
        }
    }
}

/*!
 * @brief VR の盤（世界を載せる卓）を空間へ据える（§17）。
 *
 * @details **最初のフレームで 1 回だけ据える**（以後は再中心の操作でだけ動く）。
 * 空間固定なので、頭を動かせば覗き込める。一人称かどうかで据え方が変わるが、
 * 写像そのものは同じ `world_to_stage` を通り、変わるのは縮尺と置き場所だけ。
 */
void place_vr_board(bool vr_frame_ready, const xr::Frame &vr_frame,
    xr::Session &xr, Camera &camera, const ViewWindow &view_window,
    FpsMode &first_person, const Hd2dSettings &settings, const AppOptions &options,
    int screen_w, int screen_h, RenderView (&vr_views)[2],
    xr::BoardPlacement &vr_board, xr::RoomPlacement &vr_room,
    xr::HeadAnchor &vr_anchor, bool &vr_board_placed, bool &vr_board_first_person,
    int &vr_board_turn, float &vr_panel_yaw)
{
    if (vr_frame_ready) {
        /*
         * 盤の据え方。**最初のフレームで 1 回だけ据える**（以後は再中心の操作でだけ動く。
         * 割り当ては M3）。空間固定なので、頭を動かせば覗き込める。
         */
        const float mid_pos[3] = {
            (vr_frame.views[0].position[0] + vr_frame.views[1].position[0]) * 0.5f,
            (vr_frame.views[0].position[1] + vr_frame.views[1].position[1]) * 0.5f,
            (vr_frame.views[0].position[2] + vr_frame.views[1].position[2]) * 0.5f,
        };
        /*
         * **一人称かどうかで据え方が変わる**（§17）。写像そのものは同じ
         * `world_to_stage` を通り、変わるのは縮尺と盤の置き場所だけである。
         * 見せ方を切り替えたら据え直す（1 マス 4cm の盤と 3m の世界は別物なので）。
         */
        if (vr_board_first_person != first_person.active) {
            vr_board_first_person = first_person.active;
            vr_board_placed = false;
        }
        if (!vr_board_placed) {
            /*
             * **基準の頭**。盤と卓はここに据わる（板は §22 のとおり別で、
             * 向いている方角へゆっくり付いてくる——ここでは向きだけ揃える）。
             */
            {
                const Vec3 flat = xr::quat_rotate(vr_frame.views[0].orientation, Vec3{ 0.f, 0.f, -1.f });
                vr_panel_yaw = std::atan2(-flat.x, -flat.z);
            }
            vr_anchor.position[0] = mid_pos[0];
            vr_anchor.position[1] = mid_pos[1];
            vr_anchor.position[2] = mid_pos[2];
            for (int i = 0; i < 4; ++i) {
                vr_anchor.orientation[i] = vr_frame.views[0].orientation[i];
            }
            if (first_person.active) {
                /*
                 * 一人称（§17）。縮尺は**頭の高さから決める**のが既定
                 * （平らな画面の目の高さ 0.55 マスが実際の身長になる比）。
                 * cfg に 0 以外が書いてあればそれで固定する。
                 * **部屋も卓も出さない**——ゲーム空間の中に立つのがこの見せ方だから。
                 */
                const float cell_m = (settings.vr_fps_cell_m > 0.f)
                    ? settings.vr_fps_cell_m
                    : xr::first_person_cell_m(mid_pos[1], kFpsEyeHeight);
                vr_board = xr::first_person_recenter(mid_pos, vr_frame.views[0].orientation, cell_m);
                vr_room = xr::RoomPlacement{}; //!< `visible == false`
                std::fprintf(stderr, "[hd2d] XR_BOARD 一人称: 1 マス %.2fm（頭の高さ %.2fm%s）\n",
                    static_cast<double>(cell_m), static_cast<double>(mid_pos[1]),
                    (settings.vr_fps_cell_m > 0.f) ? "・cfg で固定" : "から導出");
            } else {
                /*
                 * ジオラマ（§20。2026-08-14 に決めた）。**卓は畳 1 畳で固定、
                 * 縮尺は設定。**盤は天板の上に載る。
                 */
                vr_room = xr::place_room(mid_pos, vr_frame.views[0].orientation,
                    settings.vr_table_forward_m, settings.vr_table_height_m);
                vr_board = xr::recenter(vr_room, settings.vr_tile_m);
                const float board_w = static_cast<float>(view_window.cols) * settings.vr_tile_m;
                const float board_d = static_cast<float>(view_window.rows) * settings.vr_tile_m;
                std::fprintf(stderr,
                    "[hd2d] XR_BOARD ジオラマ: 1 マス %.3fm → 盤 %.2f×%.2fm / 卓 %.2f×%.2fm（天板 %.2fm%s）%s\n",
                    static_cast<double>(settings.vr_tile_m), static_cast<double>(board_w),
                    static_cast<double>(board_d), static_cast<double>(vr_room.table_w),
                    static_cast<double>(vr_room.table_d), static_cast<double>(vr_room.table_center.y),
                    (settings.vr_table_height_m > 0.f) ? "・cfg で固定" : "・頭の高さから導出",
                    ((board_w > vr_room.table_w) || (board_d > vr_room.table_d)) ? " ※盤が卓からはみ出しています"
                                                                                 : "");
            }
            vr_board_placed = true;
            /*
             * 盤の回転は据え直しで**素へ戻す**（設計書 §4.3）。卓を置き直したのに
             * 前の向きが残っていると、「再中心したのに斜めから見ている」になる。
             */
            vr_board_turn = ((options.vr_board_turn % xr::kBoardTurnCount) + xr::kBoardTurnCount)
                % xr::kBoardTurnCount;
            /*
             * 空中の板（§7・§20）。**中身は 1 枚のテクスチャ**で、板ごとに
             * その部分矩形を指す。解像度は窓と同じにする（`ui_layout` を作り直さずに
             * 済み、字の縦横比も崩れない）。足りなければ窓を大きくして起動する
             * ——`--windowed=2560x1440` がそのまま板の解像度になる。
             */
            if (xr.valid()) {
                std::string ui_err;
                if (!xr.create_ui_layer(screen_w, screen_h, ui_err)) {
                    std::fprintf(stderr, "[hd2d] %s（文字は HMD に出ません）\n", ui_err.c_str());
                }
            }
        }
        /*
         * **一人称では頭が照準である**（§17）。歩く向きも実体の向きもここから決まる。
         * 右スティックで首を回す作りにすると HMD と喧嘩する（頭が既に向きを持っている）。
         *
         * `facing` と `yaw` の両方へ入れるのは、`advance()` の追随を働かせないため
         * ——頭はもう向いているので、絵が遅れて追いつく必要が無い。
         */
        if (first_person.active) {
            const float facing = xr::head_azimuth(vr_board, vr_frame.views[0].orientation);
            first_person.facing = facing;
            first_person.yaw = facing;
            first_person.apply(camera); //!< 上で当てた `yaw` を今の値で入れ直す
        }
        /*
         * **拡大率はその場で効かせる**（2026-08-14 に決めた「ジオラマの拡大率は
         * ゲーム内でリアルタイムに変更させて」）。据え直し（再中心）を待たせると、
         * メニューで回しても何も起きないように見える。卓と盤の関係は縮尺に
         * よらないので、ここだけ差し替えればよい。
         */
        if (!first_person.active && (settings.vr_tile_m > 0.f)) {
            vr_board.tile_m = settings.vr_tile_m;
        }
        /*
         * **盤の 90° 回転を当てた写し**。
         *
         * 通すのは**世界 ↔ ステージの写像だけ**である。部屋・卓（`vr_room`）と
         * 空中の板（`vr_anchor`）は素の `vr_board` のまま——回すのは卓の上の盤だけで、
         * 家具と文字は動かない（設計書 罠 8。一人称のスナップ振り向きはアンカーごと回すが、
         * ジオラマで同じことをすると「盤を回したら文字が後ろへ消える」）。
         *
         * 一人称では `vr_board_turn` は常に 0（`turn_view` が `first_person` で弾く）なので、
         * `board_with_turn` は恒等写像になり 1 ビットも変わらない。
         */
        const xr::BoardPlacement vr_board_view = xr::board_with_turn(vr_board, vr_board_turn);
        const Mat4 to_stage = xr::world_to_stage(camera.target, vr_board_view);
        /*
         * **かきわりの方位角は中央の目で 1 本**（罠 10）。左右で別々に向けると
         * 板が目ごとにねじれて立体視が壊れる。左目の向きで代表させる——
         * 左右の目の向きは同じ（並進だけが違う）ので、これで中央と一致する。
         */
        const float azimuth = xr::billboard_azimuth(vr_board_view, vr_frame.views[0].orientation);
        for (int eye = 0; eye < vr_frame.view_count; ++eye) {
            const xr::EyeView &src = vr_frame.views[eye];
            RenderView &dst = vr_views[eye];
            dst.view = xr::eye_view_from_pose(src.position, src.orientation) * to_stage;
            const Vec3 eye_stage{ src.position[0], src.position[1], src.position[2] };
            if (first_person.active) {
                xr::depth_range_for_first_person(vr_board.tile_m, dst.z_near, dst.z_far);
            } else {
                xr::depth_range_for_board(eye_stage, vr_board_view, vr_room, view_window.cols,
                    view_window.rows, kMaxPropHeight, dst.z_near, dst.z_far);
            }
            dst.projection = xr::projection_from_fov(src.fov_left, src.fov_right, src.fov_up,
                src.fov_down, dst.z_near, dst.z_far);
            /*
             * 目の位置を**世界座標へ戻す**。減衰も鏡面も「マス」で書かれているので、
             * ステージのメートルをそのまま渡すと光が全部消える。
             */
            dst.eye = xr::stage_to_world(eye_stage, camera.target, vr_board_view);
            dst.azimuth = azimuth;
            dst.width = src.width;
            dst.height = src.height;
            /*
             * **部屋と卓はステージ空間のまま描く**（§20）。盤の写像を掛けない
             * 視点行列を別に持たせておき、`draw_scene` が世界の幾何と同じ深度
             * バッファへ重ねる。単位はどちらも「目から見たメートル」なので、
             * 卓の縁と盤が正しく前後する。
             */
            dst.stage_view = xr::eye_view_from_pose(src.position, src.orientation);
            dst.stage_valid = true;
        }
    }
}

} // namespace hd2d
