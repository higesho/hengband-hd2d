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

namespace {

/* ================================================================ 入力の翻訳 */

/*!
 * @brief `SDL_Keymod` → 割り当て表の修飾（`ui/key_binds.h` の `kMod*`）。
 * @details **左右をまとめる。**`KMOD_LSHIFT` をそのまま覚えると、右 Shift で割り当てた人が
 * 左 Shift で反応しないことになる。NumLock・CapsLock は「押した修飾」ではないので落とす。
 */
int mods_from_sdl(Uint16 mod)
{
    int mods = 0;
    if ((mod & KMOD_SHIFT) != 0) {
        mods |= kModShift;
    }
    if ((mod & KMOD_CTRL) != 0) {
        mods |= kModCtrl;
    }
    if ((mod & KMOD_ALT) != 0) {
        mods |= kModAlt;
    }
    return mods;
}

/*!
 * @brief UI のカーソルが解釈する向き（P8）。**斜めは渡さない。**
 * @details 斜めを渡すと、店で `1`（南西）を押したときに選択肢が動いてしまい、
 * 「文字キーでも選べる」という従来の道が塞がる。カーソルは上下左右と決定だけを食う。
 */
CursorNav cursor_nav_from_key(const SDL_KeyboardEvent &key)
{
    const bool numlock = (key.keysym.mod & KMOD_NUM) != 0;
    switch (key.keysym.sym) {
    case SDLK_UP:
        return CursorNav::Up;
    case SDLK_DOWN:
        return CursorNav::Down;
    case SDLK_LEFT:
        return CursorNav::Left;
    case SDLK_RIGHT:
        return CursorNav::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return CursorNav::Confirm;
    case SDLK_KP_8:
        return numlock ? CursorNav::None : CursorNav::Up;
    case SDLK_KP_2:
        return numlock ? CursorNav::None : CursorNav::Down;
    case SDLK_KP_4:
        return numlock ? CursorNav::None : CursorNav::Left;
    case SDLK_KP_6:
        return numlock ? CursorNav::None : CursorNav::Right;
    default:
        return CursorNav::None;
    }
}


/*!
 * @brief テンキーの数字（`1`〜`9`。`5` は除く）を**画面基準**の (dx,dy) にする。
 * @return 数字でなければ偽（dx,dy は触らない）。
 * @details 並びはテンキーそのもの——`7 8 9` が上の段、`1 2 3` が下の段。
 */
bool digit_to_screen_delta(char c, int &dx, int &dy)
{
    switch (c) {
    case '1':
        dx = -1;
        dy = 1;
        return true;
    case '2':
        dx = 0;
        dy = 1;
        return true;
    case '3':
        dx = 1;
        dy = 1;
        return true;
    case '4':
        dx = -1;
        dy = 0;
        return true;
    case '6':
        dx = 1;
        dy = 0;
        return true;
    case '7':
        dx = -1;
        dy = -1;
        return true;
    case '8':
        dx = 0;
        dy = -1;
        return true;
    case '9':
        dx = 1;
        dy = -1;
        return true;
    default:
        return false;
    }
}

//! 上の逆。8 近傍でなければ `'\0'`。
char screen_delta_to_digit(int dx, int dy)
{
    if ((dx == -1) && (dy == 1)) {
        return '1';
    }
    if ((dx == 0) && (dy == 1)) {
        return '2';
    }
    if ((dx == 1) && (dy == 1)) {
        return '3';
    }
    if ((dx == -1) && (dy == 0)) {
        return '4';
    }
    if ((dx == 1) && (dy == 0)) {
        return '6';
    }
    if ((dx == -1) && (dy == -1)) {
        return '7';
    }
    if ((dx == 0) && (dy == -1)) {
        return '8';
    }
    if ((dx == 1) && (dy == -1)) {
        return '9';
    }
    return '\0';
}

/*!
 * @brief SDL のキー入力を `input_event`（v1 §8.3）へ翻訳する。
 *
 * @details **`keys`（生のバイト列）ではなく `input_event` を送る。** 抽象イベントなら
 * 「北へ 1 歩」がコアの側でキー配列に合わせて解決されるので、こちらがテンキーの数字や
 * ローグライク配列を知らずに済む（`presentation/bridge/input_event_adapter.cpp`）。
 *
 * @note 印字文字は `SDL_TEXTINPUT` から採る（Shift や記号の解決を SDL に任せられる）。
 * `SDL_KEYDOWN` で拾うのは**印字にならないキーと Ctrl 併用**だけ。両方で拾うと 2 回入る。
 */
bool translate_key_down(const SDL_KeyboardEvent &key, presentation::InputEventWire &out)
{
    const SDL_Keycode sym = key.keysym.sym;
    const bool ctrl = (key.keysym.mod & KMOD_CTRL) != 0;
    const bool shift = (key.keysym.mod & KMOD_SHIFT) != 0;
    const bool alt = (key.keysym.mod & KMOD_ALT) != 0;
    const bool numlock = (key.keysym.mod & KMOD_NUM) != 0;

    auto move = [&out](int dx, int dy) {
        out.e = "move";
        out.dx = dx;
        out.dy = dy;
        return true;
    };

    switch (sym) {
    case SDLK_UP:
        return move(0, -1);
    case SDLK_DOWN:
        return move(0, 1);
    case SDLK_LEFT:
        return move(-1, 0);
    case SDLK_RIGHT:
        return move(1, 0);
    case SDLK_ESCAPE:
        out.e = "cancel";
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        out.e = "confirm";
        return true;
    case SDLK_TAB:
        out.e = "key";
        out.name = "tab";
        return true;
    case SDLK_BACKSPACE:
        out.e = "key";
        out.name = "backspace";
        return true;
    case SDLK_DELETE:
        out.e = "key";
        out.name = "delete";
        return true;
    default:
        break;
    }

    // テンキー。NumLock が入っているときは `SDL_TEXTINPUT` から数字が来るので触らない
    // （両方で拾うと 1 打鍵で 2 歩動く）。
    if (!numlock) {
        switch (sym) {
        case SDLK_KP_1:
            return move(-1, 1);
        case SDLK_KP_2:
            return move(0, 1);
        case SDLK_KP_3:
            return move(1, 1);
        case SDLK_KP_4:
            return move(-1, 0);
        case SDLK_KP_5:
            return move(0, 0);
        case SDLK_KP_6:
            return move(1, 0);
        case SDLK_KP_7:
            return move(-1, -1);
        case SDLK_KP_8:
            return move(0, -1);
        case SDLK_KP_9:
            return move(1, -1);
        default:
            break;
        }
    }

    if ((sym >= SDLK_F1) && (sym <= SDLK_F12)) {
        out.e = "fkey";
        out.n = (sym - SDLK_F1) + 1;
        out.ctrl = ctrl;
        out.shift = shift;
        out.alt = alt;
        return true;
    }

    // Ctrl + 英字。`SDL_TEXTINPUT` は制御文字を出さないのでここでしか拾えない。
    if (ctrl && (sym >= SDLK_a) && (sym <= SDLK_z)) {
        out.e = "key";
        out.chr = std::string(1, static_cast<char>(sym));
        out.ctrl = true;
        return true;
    }
    return false;
}

} // namespace

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

namespace {

/*!
 * @brief `SDL_TEXTINPUT`（印字文字と IME の確定）を捌く。`pump_sdl_events()` の一部。
 *
 * @details 本体は `pump_sdl_events()` の中に在ったときの字面のまま。**`continue;`（次の
 * 出来事へ）が `return;` になっただけ**で、順番も分岐も動かしていない。
 * `for` の中の `continue;` は for のものなので、そのまま。
 */
void pump_text_input(InputPumpContext &ctx, const SDL_Event &event)
{
    ctx.ime_edit_text.clear(); //!< 確定した。未確定の表示は消す
    if (ctx.feature_menu.is_open()) {
        return; // メニューを開いている間は印字文字もコアへ流さない
    }
    if (ctx.swallow_next_text
        || ((ctx.swallow_char != '\0') && (event.text.text[0] == ctx.swallow_char)
            && (event.text.text[1] == '\0'))) {
        //! 直前の KEYDOWN で UI が食った 1 文字。**その 1 個だけ**落とす。
        ctx.swallow_next_text = false;
        ctx.swallow_char = '\0';
        return;
    }
    ctx.swallow_char = '\0';
    /*
     * **日本語などの非 ASCII は `text` で送る**（2026-08-13 に決めた）。
     *
     * コアは 2 バイト文字を受けられる——`askfor` は 1 バイト目が `iskanji`
     * なら `inkey_base` で次の 1 バイトをそのまま取る（`asking-player.cpp`）。
     * 受け取れなかったのは**ここが捨てていた**からで、コアの制約ではない。
     *
     * 送るのは **UTF-8 のまま**。系の文字コード（Windows は SJIS・Android は
     * EUC）へ直すのはコア側の仕事である（`input_event_adapter.cpp`）——
     * 「ui は文字コードを知らない」という分担は、地形テーブルと同じ約束。
     *
     * **自由文字入力の最中だけ**にする。コマンドを待っている所へ 2 バイト文字を
     * 流すと、コアはそれを 2 つのコマンドとして読む（`iskanji` の判定は
     * 文字入力の中にしか無い）。外では今までどおり黙って捨てる。
     */
    bool non_ascii = false;
    for (const char *p = event.text.text; *p != '\0'; ++p) {
        const auto byte = static_cast<unsigned char>(*p);
        non_ascii = non_ascii || (byte < 0x20) || (byte > 0x7E);
    }
    if (non_ascii && ctx.frame.text_input_active) {
        presentation::InputEventWire wire;
        wire.e = "text";
        wire.text = event.text.text;
        ctx.input.events.push_back(wire);
        return;
    }
    for (const char *p = event.text.text; *p != '\0'; ++p) {
        const auto byte = static_cast<unsigned char>(*p);
        if ((byte < 0x20) || (byte > 0x7E)) {
            continue; // 印字 ASCII だけ（コアのキューは 1 バイトのキーを取る）
        }
        /*
         * **数字キーの移動も視点回転ぶん回す**（2026-09-06 に気づいた）。
         *
         * 矢印とテンキー（NumLock 切）は `translate_key_down` が `move` にするので
         * `turn_screen_move` を通る。ところが **NumLock が入っているテンキーと
         * 上段の数字は文字として届く**ので、ここを素通りして素の向きでコアへ行っていた。
         *
         * 直すのは**向きだけ**——コアが受け取るのは今までどおり数字なので、
         * 走る・止まる・回数の意味は 1 つも変わらない。回してよい場面かどうかは
         * `turn_drives_movement()` が持つ（メニュー・店・選択肢・数の入力では回さない）。
         */
        char chr = static_cast<char>(byte);
        int digit_dx = 0;
        int digit_dy = 0;
        if (ctx.turn_drives_movement() && digit_to_screen_delta(chr, digit_dx, digit_dy)) {
            rotate_screen_delta(ctx.camera_turn, digit_dx, digit_dy);
            const char turned = screen_delta_to_digit(digit_dx, digit_dy);
            if (turned != '\0') {
                chr = turned;
            }
        }
        presentation::InputEventWire wire;
        wire.e = "key";
        wire.chr = std::string(1, chr);
        ctx.input.events.push_back(wire);
    }
    return;
}

/*!
 * @brief `SDL_KEYDOWN` を捌く。`pump_sdl_events()` の一部で、いちばん長い枝。
 *
 * @details 本体は `pump_sdl_events()` の中に在ったときの字面のまま。**`continue;`（次の
 * 出来事へ）が `return;` になっただけ**で、順番も分岐も動かしていない。
 * 見る順は、配置の編集画面 → Alt+Enter → カットイン → 機能メニュー → 一人称の WASDQE →
 * 割り当て → カーソル層 → 文字編集の矢印 → Enter のコマンドメニュー → コアへ。
 * それぞれの理由は中の註釈にある。
 *
 * @param fps_owns_move_keys 一人称が WASDQE を持っているか（`ctx.fps_drives_movement()`。
 * 出来事ごとに `pump_sdl_events()` が判じ直す——同じ 1 巡の中でメニューが開くことがある）
 */
void pump_key_down(InputPumpContext &ctx, const SDL_Event &event, bool fps_owns_move_keys)
{
    /*
     * **UI が先に食うキー**（P8）。既定の綴りは既存 UI と同じ
     * （F10 = 機能メニュー・Alt+Enter = 画面モード）ので、利用者の記憶がそのまま効く。
     * 割り当ては機能メニューの「操作の割り当て」で変えられる。
     * ここで `continue` する＝**コアへは流さない**（誤コマンド禁止）。
     */
    const SDL_Keycode sym = event.key.keysym.sym;
    const int key_mods = mods_from_sdl(event.key.keysym.mod);

    /*
     * ボタン配置の編集画面（2026-08-12 に決めた）。**すべてのキーを食う**
     * （下にはメニューと地図があり、通すと見えない画面への誤操作になる）。
     * 抜ける道はタッチの「戻る」と、ここの ESC / Enter / Android の戻るキー。
     */
    if (ctx.vpad.editor_open()) {
        if ((sym == SDLK_ESCAPE) || (sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER)
            || (sym == SDLK_AC_BACK)) {
            ctx.vpad.close_editor();
        }
        ctx.swallow_next_text = true;
        return;
    }

    // Alt+Enter で全画面 ⇔ ウインドウ（Windows の慣習。コアは受け取らない）。
    if ((sym == SDLK_RETURN) && ((event.key.keysym.mod & KMOD_ALT) != 0)) {
        ctx.settings.windowed = !ctx.settings.windowed;
        return;
    }
    /*
     * カットインの間は**すべてのキーを演出が食う**（P10。`ui/floor_cutin.h`）。
     * 進めるのは決定キー（Enter / Space）だけで、ほかのキーは**捨てる**
     * ——幕の向こうは見えていないので、通せば誤コマンドになる。
     * **機能メニューより先**に見る（幕が出ている間はメニューも開かせない。
     * 開けてしまうと、いちばん上に描く幕の下にメニューが隠れる）。
     */
    if (ctx.floor_cutin.blocks_input()) {
        if ((sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER) || (sym == SDLK_SPACE)) {
            (void)ctx.floor_cutin.confirm();
        }
        ctx.swallow_next_text = true; //!< 続く `SDL_TEXTINPUT` も落とす
        return;
    }
    /*
     * 機能メニューを開いている間は**すべてのキーをメニューが食う**。
     * 誤コマンド禁止（メニューの中で押した矢印がゲームの中で歩いてはいけない）。
     */
    if (ctx.feature_menu.is_open()) {
        /*
         * 割り当ての変更モード。**十字も Enter も割り当てたいキーでありうる**ので、
         * メニューの操作より先に見る（`handle_bind_key` が予約キーを弾く）。
         */
        if (ctx.feature_menu.waiting_for_key()) {
            if (ctx.feature_menu.handle_bind_key(static_cast<int>(sym), key_mods, ctx.settings)) {
                return;
            }
        }
        if (ctx.settings.key_binds.action_for(static_cast<int>(sym), key_mods) == kActionFeatureMenu) {
            /*
             * 同じキーで閉じる。**ただしキーのリピートでは閉じない**
             * ここと
             * 開く側（`perform_action` の `kActionFeatureMenu`）は同じキーで
             * 交互に踏まれるので、リピートを通すと **F10 を押しっぱなしに
             * するだけでメニューが点滅する**（PC で再現する。Quest の
             * 「一瞬開いて閉じる」と同じ形）。開閉は物理的な押し直しでだけ動かす。
             */
            if ((event.key.repeat == 0) && !ctx.menu_toggled_this_pump) {
                ctx.menu_toggled_this_pump = true;
                ctx.feature_menu.close();
            }
            return;
        }
        MenuNav nav = MenuNav::None;
        switch (sym) {
        case SDLK_UP:
        case SDLK_KP_8:
            nav = MenuNav::Up;
            break;
        case SDLK_DOWN:
        case SDLK_KP_2:
            nav = MenuNav::Down;
            break;
        case SDLK_LEFT:
        case SDLK_KP_4:
            nav = MenuNav::Left;
            break;
        case SDLK_RIGHT:
        case SDLK_KP_6:
            nav = MenuNav::Right;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            nav = MenuNav::Confirm;
            break;
        case SDLK_ESCAPE:
            nav = MenuNav::Cancel;
            break;
        default:
            break;
        }
        ctx.feature_menu.handle(nav, ctx.settings);
        return;
    }
    /*
     * 一人称（おまけ。`ui/fps_mode.h`）の操作。**横取りするのは WASDQE と
     * 右 Shift だけ**で、ほかのキーは今までどおりコアのコマンドとして働く
     * （2026-08-11 に決めた「WASDQE 以外は FPS モードでも使えるように」）。
     *
     * **方向キーとテンキーは一人称でも見下ろしの絶対方位のまま**（↑＝北固定）。
     * これは手つかずではなく**仕様**である——確認のうえ自分で決めた
     * （2026-08-12「これはこのままの方がいいな。直さないでおく」）。
     * 向き基準で歩きたければ WASD／左スティック（`direction_delta`）を使う。
     * **将来「画面の向きと合っていない」と直したくなってもここを読むこと。**
     *
     * 食わない場合:
     *   - 選択肢・はい／いいえ・数値入力・機能メニューが出ている間
     *     （店で品物の a・s が選べなくなる）
     *   - Ctrl / Alt / GUI / Shift 付き
     *     （コアでは大文字と Ctrl 付きが別のコマンドなので、奪うとその操作が消える）
     */
    if (fps_owns_move_keys && (sym == SDLK_RSHIFT)) {
        //! 一人称の間の「メニュー」。開いた先の操作はカーソル＋Enter＋ESC のまま。
        //! ここも開閉なので**リピートでは動かさない**（§15 の 2・罠 Q-13）。
        if ((event.key.repeat == 0) && !ctx.menu_toggled_this_pump) {
            ctx.menu_toggled_this_pump = true;
            ctx.feature_menu.set_sub_panel_kinds(ctx.sub_panel_kind_choices);
            ctx.feature_menu.set_pad_state(ctx.pad_commands, ctx.pad.connected() || ctx.settings.vpad.show,
                ctx.pad.connected() ? ctx.pad.name() : std::string("バーチャルパッド"));
            ctx.feature_menu.open(ctx.settings);
            ctx.pad.forget_hold();
        }
        return;
    }
    if (fps_owns_move_keys
        && ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI | KMOD_SHIFT)) == 0)) {
        int forward = 0;
        int strafe = 0;
        bool eaten = true;
        switch (sym) {
        case SDLK_w:
            forward = 1;
            break;
        case SDLK_s:
            forward = -1;
            break;
        case SDLK_a:
            strafe = -1;
            break;
        case SDLK_d:
            strafe = 1;
            break;
        case SDLK_q:
            //! 向きだけ。**ターンは消費しない。**押している間ずっと回る（`turn_hold_*`）。
            if (event.key.repeat == 0) {
                ctx.first_person.turn_hold_begin(-1);
            }
            break;
        case SDLK_e:
            if (event.key.repeat == 0) {
                ctx.first_person.turn_hold_begin(1);
            }
            break;
        default:
            eaten = false;
            break;
        }
        if (eaten) {
            int dx = 0;
            int dy = 0;
            if (ctx.first_person.direction_delta(forward, strafe, dx, dy)) {
                ctx.click_path.cancel(); //!< 自分で歩き始めたら経路は捨てる
                presentation::InputEventWire wire;
                wire.e = "move";
                wire.dx = dx;
                wire.dy = dy;
                ctx.input.events.push_back(wire);
            }
            /*
             * **続く `SDL_TEXTINPUT` の 1 文字を落とす札を立てる。**
             * KEYDOWN を食っただけでは印字文字が別のイベントでコアへ届く
             * （これを見落として Q が「飲む」を開いていた。2026-08-11 に気づいた）。
             */
            ctx.swallow_char = static_cast<char>(sym);
            return;
        }
    }
    /*
     * **割り当てられた操作**（`ui/key_binds.h`）。UI 自身の操作もコアのコマンドも
     * 同じ表から引く。ここで食ったキーはコアへ流さない（二重に効かない）。
     *
     * **一人称の WASDQE より後。**一人称の間の WASDQE はその場の移動であり、
     * 同じキーに割り当てた操作より優先する（§7.1.1 の「横取りするのは WASDQE」）。
     * 先に置くと、`a` に何かを割り当てた瞬間に一人称の左横歩きが消える。
     */
    {
        const int action = ctx.settings.key_binds.action_for(static_cast<int>(sym), key_mods);
        if (action != kActionNone) {
            /*
             * **機能メニューの開閉だけはキーのリピートで動かさない**
             * ほかの操作は今までどおりリピートを通す——押しっぱなしで
             * 進むのが自然なもの（コアのコマンド）まで一律に止めると、
             * 「押し続けても 1 回しか効かない」という別の不具合になる。
             */
            if ((action != kActionFeatureMenu) || (event.key.repeat == 0)) {
                ctx.perform_action(action);
            }
            /*
             * **続く `SDL_TEXTINPUT` の 1 文字を落とす札を立てる。**
             * KEYDOWN を食っただけでは印字文字が別のイベントでコアへ届く
             * （一人称の WASDQE で実際に踏んだ穴。2026-08-11 に気づいた）。
             * Ctrl / Alt 併用は印字にならないので札を立てない（立てると
             * 次に打った文字を巻き添えにする）。
             */
            if ((sym >= 0x20) && (sym <= 0x7E) && ((key_mods & (kModCtrl | kModAlt)) == 0)) {
                ctx.swallow_next_text = true;
            }
            return;
        }
    }
    /*
     * **カーソルが先に食う**（P8。選択肢・はい／いいえ・数値入力）。
     * 食われなかったものだけが従来どおりコアへ行く。ここを後回しにすると
     * 「個数を聞かれている最中の Enter」が店のコマンドとして走る。
     *
     * ## `pre_game_menu` で切ってはいけない（2026-08-11 に切って 3 つ壊した）
     * 「作成画面はコアがカーソルを持つ」と誤診して層ごと切った版は、
     * **矢印がコアへ素通りして数字キーに化けた**（`translate_key_down` の
     * 移動イベント → アダプタが `6` などへ）。`[Y/n]` は y/n 以外のキーを
     * 「はい」と読むので、**→ を押すと勝手に決定**し、プロンプトの枠も
     * 動かなくなった（2026-08-11 に気づいた）。作成画面の [Y/n] も
     * ロール確認も**この層が持つ**（K-24 がそのために作った）。
     * 本当の不具合はカーソルの初期位置だった（`ui_cursor.cpp` の `sync`）。
     */
    if (ctx.hud_state.cursors.handle(cursor_nav_from_key(event.key), ctx.input)) {
        return;
    }
    /*
     * **文字を編集している画面の矢印**（`ui/ui_cursor.h` の
     * `handle_text_edit_arrows`）。エディタは矢印を `SKEY_*` としてしか
     * 受け取れず、この線に SKEY は無い——そのうえ `move` はアダプタで
     * `'4'` `'8'` になるので、**押すたびに本文へ数字が入っていた**。
     * カーソル層の**後**に置くこと（数値入力・はい／いいえ・
     * 自動拾いエディタの ESC メニューは向こうのものである）。
     */
    if (handle_text_edit_arrows(ctx.frame, cursor_nav_from_key(event.key), ctx.input)) {
        return;
    }
    /*
     * **Enter も A と同じ**（すぐ下のパッドの枝と同じ理屈）。変愚では
     * `\r` を受けたコアが通常メニューを開くので、旗を立てないあちらでは
     * ここは素通りする——キーボードの手触りも変わらない。
     */
    if (((sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER))
        && ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_GUI)) == 0)
        && confirm_opens_command_menu(ctx.frame)) {
        ctx.perform_action(kActionCommandMenu);
        return;
    }
    presentation::InputEventWire wire;
    if (translate_key_down(event.key, wire)) {
        //! 矢印・テンキーは**画面基準**。視点回転ぶんを当ててから積む（設計書 罠 1）。
        ctx.turn_screen_move(wire);
        ctx.input.events.push_back(wire);
    }
    return;
}

} // namespace

/*!
 * @brief SDL の出来事を全部捌く（(a) 入力）。仕様は `run_parts.h`。
 * @details 本体は `run()` に在ったときの字面のまま。外の名前に `ctx.` が付いただけで、
 * 順番も分岐も 1 つも動かしていない（`ctx.` を剥がせば元の字面に戻る）。
 */
void pump_sdl_events(InputPumpContext &ctx)
{
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            ctx.want_quit = true;
            continue;
        }
        if ((event.type == SDL_WINDOWEVENT) && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
            ctx.screen_w = event.window.data1;
            ctx.screen_h = event.window.data2;
            glViewport(0, 0, ctx.screen_w, ctx.screen_h);
            continue;
        }
        if ((event.type == SDL_WINDOWEVENT) && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
            //! **「握ったまま」を忘れる**（`game_pad.h` の約束 1・2）。
            ctx.pad.forget_hold();
            ctx.vpad.forget_hold(); //!< 指の掴みも捨てる（FINGERUP が来ない道があるため）
            continue;
        }
        /*
         * バーチャルパッド（2026-08-12 に決めた）。**マウスの分岐より先**に見る
         * ——ボタンに落ちた指（とそのマウス写し）はここが食い、タップ移動や
         * 一人称の視界に化けさせない。ボタンの外はいっさい触らず従来の道へ落とす。
         * 配置の編集画面が開いている間はポインタの出来事を全部ここが食う。
         */
        if (ctx.vpad.on_event(event, ctx.settings.vpad, ctx.usable_area(), ctx.screen_w, ctx.screen_h)) {
            continue;
        }
#if defined(__ANDROID__)
        /*
         * **タップしたらソフトキーボードを出し直す**（2026-08-13 に決めた）。
         * 戻るキーで閉じても**コアはまだ入力を待っている**ので、戻る道が要る。
         * バーチャルパッドより後・ほかの指の道より先に見る（パッドのボタンを
         * 押したつもりの指がキーボードを呼ばないように）。
         *
         * Android はタッチのマウス写しを切ってある（`hd2d_entry_android.cpp`）ので、
         * 指の出来事はマウスではなくここへ来る。
         *
         * **当たりは「パッド以外の画面ぜんぶ」**である。初版は入力行（`prompt_bar`）
         * だけにしていて、**名前入力で 1 度も効かなかった**（エミュレータで実測）。
         * `prompt_bar` は遊んでいる最中の `[y/n]` の行で、名前入力・生い立ちの編集・
         * 自動拾い／マクロのエディタは**全画面のターミナル**として描かれる
         * ——つまり文字入力の場面はどれも入力行があそこに無い。画面ごとに
         * 入力行の在り処を当てにいくより、**外れない所を当たりにする**ほうが正しい。
         *
         * 広げても誤爆しない。Android では文字入力の間にタップで起きることが
         * ほかに無い（タップ移動はマウス写しの道なので端から無い）。
         */
        if ((event.type == SDL_FINGERDOWN) && ctx.ime_prompt_open && ctx.ime_dismissed) {
            SDL_StartTextInput();
            ctx.ime_dismissed = false;
            continue;
        }
#endif
        if (ctx.pad.on_event(event)) {
            continue; // パッドの出来事はパッドが食う
        }
        /*
         * 一人称（おまけ）が**この 6 つのキーだけ**を持っているか。
         *
         * 決めたことは 2 段階で動いた。**いまは後者が正**:
         *   1.（2026-08-11）「元のキーボード操作は OFF にして WASDQE のみ有効に」
         *   2.（同日・訂正）「**WASDQE 以外は FPS モードでも使えるように**」
         * つまり一人称は WASDQE（＋右 Shift のメニュー）だけを横取りし、
         * ほかのキーは今までどおりコアのコマンドとして働く。
         *
         * 1 の発端は **Q が「飲む」を開く**ことだった。原因は「全部を食っていなかった」
         * ことではなく、**`SDL_TEXTINPUT` を塞いでいなかった**ことである（KEYDOWN を
         * 食っても印字文字は別のイベントで届く）。**塞ぐ場所は 2 つある**と覚える。
         * いまは `swallow_char` で「いま食った 1 文字」だけを落としている。
         *
         * メニュー（機能メニュー・コアの Term の写し・選択肢・数値入力）が出ている間は
         * 横取りもしない。一人称のまま店へ入る／罠の確認を聞かれることは普通に起きるので、
         * そこで WASD を食うと品物の a・s が選べない。判定は `fps_drives_movement()`
         * 1 か所に置いてある（パッドの方と食い違わせない）。
         */
        const bool fps_owns_move_keys = ctx.fps_drives_movement();
        /*
         * 一人称のマウス（2026-08-11 に決めた）:
         *   左クリック          決定
         *   右クリック          取消
         *   右ドラッグ          視点の自由移動（離した所で最寄りの 8 方位へ吸着）
         *   ホイール            画角 90〜120 度
         *
         * **キーと違って選択肢が出ていても横取りする。**キーの方は「店で品物の a・s が
         * 選べなくなる」ので譲っているが、マウスにはその衝突が無い。むしろ選択肢や
         * はい／いいえこそ決定・取消を押したい場面なので、ここで譲ると使えない。
         */
        const bool fps_owns_mouse = ctx.first_person.active && !ctx.feature_menu.is_open();
        /*
         * **視界を回すのは地図が見えているときだけ。**メニューが覆っている間も回すと、
         * 店から出た瞬間に景色が明後日を向いている。ボタン（決定・取消）は覆われていても
         * 効かせたいので、条件を分けてある。
         */
        const bool fps_owns_look = fps_owns_mouse && frame_shows_map(ctx.frame);
        /*
         * IME の未確定文字列。**コアへは送らない**（確定するまでは文字ではない）。
         * 覚えておいて、こちらで入力欄の桁に描く（`ime_edit_text` の注記）。
         */
        if (event.type == SDL_TEXTEDITING) {
            /*
             * **1 度だけ出す。**「候補は出るのに打っている字が見えない」の相談は、
             * この行が出ているかどうかで**こちらへ届いていない**のか
             * **描けていない**のかが分かれる（毎回出すと変換のたびに埋まる）。
             */
            static bool told = false;
            if (!told && (event.edit.text[0] != '\0')) {
                told = true;
                std::fprintf(stderr, "[hd2d] IME の未確定文字列を受け取りました（こちらで描きます）\n");
            }
            ctx.ime_edit_text = event.edit.text;
            continue;
        }
        if (event.type == SDL_TEXTINPUT) {
            pump_text_input(ctx, event); //!< 中身は上の関数
            continue;
        }
        /*
         * カメラの手直し（計画 §4-1 の「実物を見て決める」項目）。
         * **マウスだけを使う**（キーはコアへ転送しているので衝突する）。
         *   ホイール          … 1 マスの見かけの大きさ（＝距離）
         *   Shift + ホイール  … 見下ろし角
         *   Ctrl  + ホイール  … 水平画角
         * いまの値は画面に出ているので、気に入った値をそのまま `--camera=` に書ける。
         */
        if (event.type == SDL_MOUSEWHEEL) {
            const SDL_Keymod mod = SDL_GetModState();
            const auto step = static_cast<float>(event.wheel.y);
            if (fps_owns_mouse) {
                /*
                 * 一人称のホイールは**画角**（2026-08-11 に決めた・90〜120 度）。
                 * 見下ろしの「1 マスの大きさ」は一人称では意味を持たない（カメラは
                 * マスの中に立っていて距離が無い）ので、同じ回し方を当てても何も動かない。
                 */
                ctx.first_person.adjust_fov(event.wheel.y);
                ctx.settings.fps_fov_deg = ctx.first_person.fov_deg; //!< cfg へ残す
                continue;
            }
            //! **設定へ書く**（P8）。カメラへ直に書くと機能メニューの数字とずれ、cfg にも残らない。
            if ((mod & KMOD_SHIFT) != 0) {
                ctx.settings.camera_pitch_deg = std::clamp(ctx.settings.camera_pitch_deg + step, 10.f, 85.f);
            } else if ((mod & KMOD_CTRL) != 0) {
                ctx.settings.camera_fov_deg = std::clamp(ctx.settings.camera_fov_deg + step, 10.f, 80.f);
            } else {
                ctx.settings.camera_cell_px = std::clamp(
                    ctx.settings.camera_cell_px * ((step > 0.f) ? 1.08f : 0.926f), 24.f, 400.f);
            }
            continue;
        }
        /*
         * 一人称のマウス（2026-08-11 に決めた・**改訂**）。
         * 「右クリックをマウススライドではなく、マウススライド単体で視界移動に」
         * ＝ **ボタンを押さずに動かすだけで視界が動く。**吸着も無い。
         * したがって右ボタンは**取消だけ**を意味する。
         */
        if (fps_owns_look && (event.type == SDL_MOUSEMOTION) && (ctx.hud_state.grabbed_grip < 0)) {
            //! 相対量（`xrel`/`yrel`）で回す。相対マウスモード中は座標が来ないので必須。
            ctx.first_person.look(event.motion.xrel, event.motion.yrel);
            continue;
        }
        if (fps_owns_mouse && (event.type == SDL_MOUSEBUTTONDOWN)
            && (event.button.button == SDL_BUTTON_RIGHT)) {
            presentation::InputEventWire wire;
            wire.e = "cancel";
            ctx.input.events.push_back(wire);
            continue;
        }
        if (fps_owns_mouse && (event.type == SDL_MOUSEBUTTONDOWN)
            && (event.button.button == SDL_BUTTON_MIDDLE)) {
            /*
             * ホイールの押し込み ＝ **視界を水平に戻す**（2026-08-11 に決めた）。
             * これだけは割り当ての表に無い（マウスのボタンは表に並べていない）ので、
             * ここで直に見ている。同じ操作はコントローラー R3 とキーの割り当てからも出せる。
             */
            ctx.first_person.level_view();
            continue;
        }
        if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_RIGHT)) {
            continue; //!< 右ボタンは押した所で効かせた。離しは捨てる
        }
        if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_LEFT)) {
            //! 掴みを離す。**cfg へ書くのはここから先**（動かすたびにディスクを叩かない）。
            ctx.hud_state.grabbed_grip = -1;
            continue;
        }
        if ((event.type == SDL_MOUSEMOTION) && (ctx.hud_state.grabbed_grip >= 0)) {
            //! 掴んでいる仕切りを動かす。次のフレームの `compute()` が新しい形で組み直す。
            (void)drag_grip(ctx.layout, ctx.hud_state.grabbed_grip, event.motion.x, event.motion.y,
                ctx.settings.sub_split);
            continue;
        }
        if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_LEFT)) {
            /*
             * 下段の境界を掴む（P8。既存 UI の K-31 相当）。**地図より先に見る**
             * ので、境界の上をクリックしても歩き出さない。
             */
            if (!ctx.feature_menu.is_open()) {
                const int grip = grip_at(ctx.layout, event.button.x, event.button.y);
                if (grip >= 0) {
                    ctx.hud_state.grabbed_grip = grip;
                    ctx.click_path.cancel();
                    continue;
                }
            }
            /*
             * Term の写しに出ている**選択肢へのクリック**（P5 その2・2026-08-19。
             * 「セーブデータ選択が文字指定でしか選択できない」に気づいた）。
             * 当たりは描画と同じ計算（`menu_choice_at`）なので見えている枠と
             * ずれない。送るのはカーソル層の決定と同じ「その選択肢の 1 キー」
             * ——幻想蛮怒のセーブ選択だけでなく、選択肢が立つ画面すべて
             * （変愚の店・建物の一覧）で効く。
             */
            if (!frame_shows_map(ctx.frame)) {
                const int hit = menu_choice_at(ctx.text, ctx.layout, ctx.frame, event.button.x, event.button.y);
                if (hit >= 0) {
                    const MenuChoice &choice = ctx.frame.menu_choices[static_cast<std::size_t>(hit)];
                    presentation::InputEventWire wire;
                    if (choice.key == '\r') {
                        wire.e = "confirm";
                    } else if (choice.key == 0x1B) {
                        wire.e = "cancel";
                    } else {
                        wire.e = "key";
                        wire.chr = std::string(1, static_cast<char>(choice.key));
                    }
                    ctx.input.events.push_back(wire);
                    ctx.click_path.cancel();
                    continue;
                }
                if (!fps_owns_mouse) {
                    //! 写しの外れをクリックしても歩き出さない（下の unproject へ流さない）。
                    continue;
                }
            }
            /*
             * 一人称の左クリックは**決定**（2026-08-11 に決めた）。
             * 一人称では地面を指しても「そこへ歩く」が読めない（見えているのは
             * 目の高さの壁で、床の 1 マスを狙って押せる作りになっていない）ので、
             * クリック移動は見下ろしのときだけの操作にする。
             * **下段の境界を掴む道より後**に置く（掴みが決定に食われない）。
             */
            if (fps_owns_mouse) {
                ctx.click_path.cancel();
                if (!ctx.hud_state.cursors.handle(CursorNav::Confirm, ctx.input)) {
                    presentation::InputEventWire wire;
                    wire.e = "confirm";
                    ctx.input.events.push_back(wire);
                }
                continue;
            }
            /*
             * クリックした所まで歩く（P8）。**隣なら 1 歩、遠ければ経路**。
             * 経路は既知のマスの上だけを通す（見ていない所を通り抜ける経路は引かない）。
             */
            ctx.click_path.cancel();
            if (ctx.feature_menu.is_open() || !ctx.layout.scene.contains(event.button.x, event.button.y)) {
                continue;
            }
            Vec3 ground{};
            const float local_x = static_cast<float>(event.button.x - ctx.layout.scene.x) + 0.5f;
            const float local_y = static_cast<float>(event.button.y - ctx.layout.scene.y) + 0.5f;
            if (!ctx.camera.unproject_to_plane(local_x, local_y, 0.f, ground)) {
                continue; // 地平線より上
            }
            const int gx = static_cast<int>(std::floor(ground.x));
            const int gy = static_cast<int>(std::floor(ground.y));
            const int dx = gx - ctx.frame.player_gx;
            const int dy = gy - ctx.frame.player_gy;
            if ((dx == 0) && (dy == 0)) {
                continue;
            }
            if ((std::abs(dx) <= 1) && (std::abs(dy) <= 1)) {
                presentation::InputEventWire wire;
                wire.e = "move";
                wire.dx = dx;
                wire.dy = dy;
                ctx.input.events.push_back(wire);
                continue;
            }
            (void)ctx.click_path.begin(ctx.frame.minimap, ctx.frame.player_gx, ctx.frame.player_gy, gx, gy);
            continue;
        }
        if (event.type == SDL_MOUSEMOTION) {
            /*
             * ヒットテスト（P2 ④）。画素の**中心**を渡す（往復検査と同じ約束）。
             * **`scene.x/y` を引く**（P8）。カメラは 3D の矩形の中の座標しか知らない。
             * ここを忘れると `Split` で「描いた絵と当たり判定が食い違う」（必守制約 3）。
             */
            const float local_x = static_cast<float>(event.motion.x - ctx.layout.scene.x) + 0.5f;
            const float local_y = static_cast<float>(event.motion.y - ctx.layout.scene.y) + 0.5f;
            Vec3 ground{};
            ctx.hover_valid = ctx.layout.scene.contains(event.motion.x, event.motion.y)
                && ctx.camera.unproject_to_plane(local_x, local_y, 0.f, ground);
            if (ctx.hover_valid) {
                ctx.hover_gx = static_cast<int>(std::floor(ground.x));
                ctx.hover_gy = static_cast<int>(std::floor(ground.y));
            }
            continue;
        }
        if (event.type == SDL_KEYUP) {
            //! 一人称の向き替えは**離した所で刻みへ吸い付く**（`FpsMode::turn_hold_end`）。
            if (event.key.keysym.sym == SDLK_q) {
                ctx.first_person.turn_hold_end(-1);
            } else if (event.key.keysym.sym == SDLK_e) {
                ctx.first_person.turn_hold_end(1);
            }
            continue; //!< コアは KEYUP を使わない（印字は TEXTINPUT から採っている）
        }
        if (event.type == SDL_KEYDOWN) {
            pump_key_down(ctx, event, fps_owns_move_keys); //!< 中身は上の関数（長いので分けた）
            continue;
        }
    }
}

} // namespace hd2d
