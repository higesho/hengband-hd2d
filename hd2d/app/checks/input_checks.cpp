/*!
 * @file input_checks.cpp
 * @brief 入力と動きの検査（`--pad-check` / `--motion-check`）。窓も GL もコアも要らない。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * **遊ぶときには 1 行も通らない**——`run()` の頭で検査のモードを見て、
 * そのまま返る経路にしか無い。分けたのは本体が 17,981 行あって読めなかったため。
 *
 * 宣言は `app/hd2d_checks.h`。振る舞いが変わっていないことは
 * `python tools/hd2d_verify/golden.py --check` で見る。
 */
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "net/core_link.h"
#include "app/app_support.h"
#include "app/hd2d_checks.h"
#include "app/input_controller.h"
#include "app/test_keyboard.h"
#include "app/ui_probe.h"
#include "bridge/modern_content.h"
#include "frame/message_more.h"
#include "bridge/look_screen_cover.h"
#include "bridge/native_modern.h"
#include "frame/modern_window_codec.h"
#include "bridge/native_store_menu.h"
#include "bridge/prompt_semantics.h"
#include "bridge/symbol_prompt.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <SDL2/SDL.h>
#include <array>
#include <initializer_list>

#include "ui/diagonal_only.h"
#include "ui/game_pad.h"
#include "ui/hd2d_settings.h"
#include <iterator>
#include "voxel/part_motion.h"
#include "voxel/prefab.h"
#include "world/prefab_library.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace hd2d {

//! `MotionKind` を人が読む名前へ（画面と記録に出す）。
const char *motion_kind_name(MotionKind kind)
{
    switch (kind) {
    case MotionKind::Rotate:
        return "回転";
    case MotionKind::Pendulum:
        return "振り子";
    case MotionKind::Wind:
        return "風";
    case MotionKind::StateLinked:
        return "状態連動";
    case MotionKind::Blink:
        return "明滅";
    default:
        return "静止";
    }
}

/*!
 * @brief `--pad-check`: 同時押しとマクロのトリガーの検査。**窓も GL もコアも要らない。**
 *
 * @details 実機のコントローラーが無いと手では試せない所を機械で押さえる。
 * とくに `PadChord` は「LB は修飾でもあり単独のコマンドでもある」という**押した瞬間には
 * 決まらない**振る舞いで、間違えると**指を離すたびに階段を上り下りする**。
 */
int run_pad_check(const AppOptions &options)
{
    (void)options;
    int failures = 0;
    const auto fail = [&failures](const char *what) {
        std::fprintf(stderr, "  FAIL %s\n", what);
        ++failures;
    };

    std::fprintf(stderr, "[hd2d] --pad-check: 同時押しとマクロのトリガー\n");

    /* --- 1. トリガーの列。`lib/pref/pref-xxx.prf` の T: 行と対になっている --- */
    {
        const std::vector<int> plain = pad_input_trigger_sequence(PadInput::X);
        const std::vector<int> want_plain{ 31, 'x', 'F', '0', 13 };
        if (plain != want_plain) {
            fail("X の単独トリガーが \x1F x F0 \r になっていない");
        }
        const std::vector<int> ctrl = pad_input_trigger_sequence(PadInput::X, kPadModCtrl);
        const std::vector<int> want_ctrl{ 31, 'C', 'x', 'F', '0', 13 };
        if (ctrl != want_ctrl) {
            fail("LB＋X が control- の列になっていない");
        }
        const std::vector<int> both = pad_input_trigger_sequence(PadInput::X, kPadModCtrl | kPadModAlt);
        const std::vector<int> want_both{ 31, 'C', 'A', 'x', 'F', '0', 13 };
        if (both != want_both) {
            //! 並びは `macro_modifier_chr`（"CSA"）の順。逆にするとコアが読めない。
            fail("LB＋RB＋X が control-alt- の列（C→A の順）になっていない");
        }
        if (!pad_input_trigger_sequence(PadInput::A).empty()) {
            fail("A にトリガーが生えている（決定は固定のはず）");
        }
    }

    /* --- 2. 状態機械。LB は修飾でもあり単独のコマンドでもある --- */
    {
        PadChord chord;
        int mods = 0;
        PadInput solo = PadInput::Count;

        //! (a) LB を押して離しただけ → **単独押しとして出る**（階段を上れないと困る）
        if (!chord.press(PadInput::LeftShoulder, mods)) {
            fail("LB の押しが修飾として飲まれていない");
        }
        if (!chord.release(PadInput::LeftShoulder, solo) || (solo != PadInput::LeftShoulder)) {
            fail("LB を単独で押して離したのに発火しない");
        }

        //! (b) LB を押しながら X → 同時押し。**LB を離しても単独発火しない**
        chord.forget();
        (void)chord.press(PadInput::LeftShoulder, mods);
        mods = 0;
        if (chord.press(PadInput::X, mods)) {
            fail("X が修飾として飲まれた");
        }
        if (mods != kPadModCtrl) {
            fail("LB を押しながらの X に control- が付いていない");
        }
        if (chord.release(PadInput::LeftShoulder, solo)) {
            fail("同時押しに使った LB が離しで単独発火した（階段を上ってしまう）");
        }

        //! (c) LB＋RB → 2 段の層。**どちらを離しても単独発火しない**
        chord.forget();
        (void)chord.press(PadInput::LeftShoulder, mods);
        (void)chord.press(PadInput::RightShoulder, mods);
        mods = 0;
        (void)chord.press(PadInput::Y, mods);
        if (mods != (kPadModCtrl | kPadModAlt)) {
            fail("LB＋RB＋Y に control-alt- が付いていない");
        }
        if (chord.release(PadInput::LeftShoulder, solo) || chord.release(PadInput::RightShoulder, solo)) {
            fail("層に使った LB／RB が離しで単独発火した");
        }

        //! (d) LB＋RB を押して**何も叩かずに**離した → どちらも単独発火しない
        chord.forget();
        (void)chord.press(PadInput::LeftShoulder, mods);
        (void)chord.press(PadInput::RightShoulder, mods);
        if (chord.release(PadInput::RightShoulder, solo) || chord.release(PadInput::LeftShoulder, solo)) {
            fail("修飾を重ねただけで離したのに単独発火した");
        }

        //! (e) 修飾でないボタンの離しは何も出さない（押した瞬間に出しているので二重になる）
        chord.forget();
        mods = 0;
        (void)chord.press(PadInput::X, mods);
        if (chord.release(PadInput::X, solo)) {
            fail("X の離しで二重に発火した");
        }
    }

    /* --- 3. 層つきの割り当てと cfg の往復（2026-08-14 に決めた） --- */
    {
        PadBinds binds;
        binds.command[0][static_cast<int>(PadInput::X)] = 23;
        binds.command[kPadModCtrl][static_cast<int>(PadInput::Y)] = 45;
        binds.command[kPadModCtrl | kPadModAlt][static_cast<int>(PadInput::Start)] = 67;

        /*
         * **綴りは丸ごと突き合わせる。**部分一致で見ていたら、層 0 にも `+` を付ける
         * という壊し方を素通しした（`find("X:23")` は `+X:23` にも当たる。
         * 2026-08-14、検査の検査で判明）。前の版の cfg と同じ形であることが要点なので、
         * 一致すべきものは一致で見る。
         */
        const std::string line = binds.to_line();
        if (line != "X:23,C+Y:45,CA+Start:67") {
            fail("cfg: pad_bind の綴りが期待と違う（層 0 は無印・層つきは C+ / CA+）");
            std::fprintf(stderr, "       出たもの: %s\n", line.c_str());
        }

        PadBinds round;
        round.parse(line);
        for (int mods = 0; mods < kPadModLayerCount; ++mods) {
            for (int i = 0; i < kPadInputCount; ++i) {
                if (round.command[mods][i] != binds.command[mods][i]) {
                    fail("cfg: 書いて読み直したら中身が変わった");
                    mods = kPadModLayerCount;
                    break;
                }
            }
        }

        //! **前の版が書いた cfg がそのまま読めること**（`+` の無い綴りは層 0）。
        PadBinds old_style;
        old_style.parse("X:23,LT:9");
        if ((old_style.command[0][static_cast<int>(PadInput::X)] != 23)
            || (old_style.command[0][static_cast<int>(PadInput::LeftTrigger)] != 9)) {
            fail("cfg: 前の版の pad_bind= が読めない");
        }
    }

    /* --- 4. 検査の検査。**わざと壊した入力に反応すること**（設計書 §14-4） --- */
    {
        PadChord chord;
        int mods = 0;
        (void)chord.press(PadInput::LeftShoulder, mods);
        if (chord.mask() != kPadModCtrl) {
            fail("LB を押している間の mask が control- になっていない");
        }
        chord.forget();
        if (chord.mask() != 0) {
            fail("forget() のあとも修飾が残っている");
        }
    }

    /*
     * --- 5. 画面（窓の種類）が替わっても、押している最中の肩ボタンは捨てない（F-rogue36-08） ---
     * 「RB を押したまま設定タブを押し、設定画面が届いた後で RB を離す」と、画面側は窓の種類の変化で握りを忘れる。
     * 方向の握りだけ忘れるなら、離した瞬間に RB の単独の押しが出る。全部忘れる `forget_hold()` は従来どおり捨てる（対照）。
     */
    {
        const auto button_event = [](Uint32 type, SDL_GameControllerButton button) {
            SDL_Event e{};
            e.type = type;
            e.cbutton.button = static_cast<Uint8>(button);
            return e;
        };
        PadPress press;

        GamePad pad;
        (void)pad.on_event(button_event(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        pad.forget_direction_hold();
        if (pad.chord_mask() != kPadModAlt) {
            fail("方向の握りを忘れたら、押している RB の修飾まで消えた");
        }
        (void)pad.on_event(button_event(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        if (!pad.take_pressed(press) || (press.input != PadInput::RightShoulder) || (press.mods != 0)) {
            fail("RB を押したまま方向の握りを忘れたあと、離しても RB の単独の押しが出ない（捨てられた）");
        }

        //! 対照：取り出し待ちの押しも、方向の握りを忘れるだけでは消えない。
        GamePad queued;
        (void)queued.on_event(button_event(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A));
        queued.forget_direction_hold();
        if (!queued.take_pressed(press) || (press.input != PadInput::A)) {
            fail("方向の握りを忘れたら、取り出し待ちの押し（A）まで消えた");
        }

        //! 対照：全部忘れる `forget_hold()` は従来どおり、押している最中の RB を捨てる。
        GamePad wipe;
        (void)wipe.on_event(button_event(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        wipe.forget_hold();
        (void)wipe.on_event(button_event(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        if (wipe.take_pressed(press)) {
            fail("forget_hold() のあとに RB の離しが押しとして出た（全部忘れる約束が崩れた）");
        }

        //! 方向の握りは捨てる：倒したスティックの連続移動は、忘れたあと続かない。
        GamePad stick;
        ExternalPadState neutral;
        ExternalPadState tilt;
        tilt.left_x = 1.f;
        int dx = 0;
        int dy = 0;
        stick.feed_external(neutral);
        (void)stick.poll_direction(500, true, dx, dy); //!< 中立を 1 度見せる（挿した直後は受け付けない約束）
        stick.feed_external(tilt);
        if (!stick.poll_direction(1000, true, dx, dy) || (dx != 1)) {
            fail("検査の足場：倒したスティックから最初の 1 歩が出ない");
        }
        stick.forget_direction_hold();
        stick.feed_external(tilt); //!< 倒したままの指。中立を見るまで受け付けない（約束 2）
        if (stick.poll_direction(5000, true, dx, dy)) {
            fail("方向の握りを忘れたのに、スティックの連続移動が続いた");
        }

        //! バーチャルパッドの R1 も同じ（探すのは R1 が当たる点。形は設定と配置しだいなので、格子で当てる）。
        VirtualPadSettings vsettings;
        vsettings.show = true;
        const RectPx area{ 0, 0, 1280, 720 };
        bool found = false;
        for (int gy = 4; (gy < 720) && !found; gy += 8) {
            for (int gx = 4; (gx < 1280) && !found; gx += 8) {
                VirtualPad vpad;
                SDL_Event down{};
                down.type = SDL_FINGERDOWN;
                down.tfinger.fingerId = 1;
                down.tfinger.x = static_cast<float>(gx) / 1280.f;
                down.tfinger.y = static_cast<float>(gy) / 720.f;
                (void)vpad.on_event(down, vsettings, area, 1280, 720);
                if (vpad.chord_mask() != kPadModAlt) {
                    continue;
                }
                found = true;
                vpad.forget_direction_hold();
                SDL_Event up = down;
                up.type = SDL_FINGERUP;
                (void)vpad.on_event(up, vsettings, area, 1280, 720);
                if (!vpad.take_pressed(press) || (press.input != PadInput::RightShoulder)) {
                    fail("バーチャルパッドの R1 を押さえたまま方向の握りを忘れたあと、離しても R1 の単独の押しが出ない");
                }
            }
        }
        if (!found) {
            fail("検査の足場：バーチャルパッドの R1 に当たる点が見つからない");
        }
    }

    /*
     * --- 5b. コアごとの既定の割り当て---
     * 「原作にパッドは無いので既定は我々が定める。基本は変愚の割り当てをベースに、同じ意味のコマンドを割り当てる」。
     * **字（`PadCommand::command`）が同じでも意味はコアごとに違う**——Rogue 3.6 の `f` は「方向を訊いて走る」、
     * Rogue 5.4 の `g` も走る（アダプタが足した）、`a` は「直前のコマンドを繰り返す」、Angband の `s` は「盗む」。
     * 変愚の字のまま引くと、Y が走る・X が走る・LT が盗む、になっていた。ここでは**各コアのアダプタが実際に送る表**
     * （`rogue_legacy/adapter/rl_main.cpp`・`rogue2/adapter/r2_main.cpp`・`umoria/adapter/um_main.cpp`・
     * `angband/adapter/an_pad_shim.c` の字と名札。変愚系は原作の `menu_info`）を写して既定を当て、
     * 各ボタンが**そのコアで変愚と同じ意味のコマンド**に当たることを見る。空のはずのボタンが空であることも見る。
     */
    {
        struct Cmd {
            int command;
            const char *label;
        };
        const auto make_table = [](std::initializer_list<Cmd> list) {
            std::vector<PadCommand> table;
            int id = 1;
            for (const Cmd &c : list) {
                table.push_back(PadCommand{ id++, c.command, "行動", c.label, { c.command } });
            }
            return table;
        };
        //! `command == 0 && action == 0` は「空のはず」。`action` は画面側の操作（`kAction*`）。
        struct Want {
            PadInput input;
            int mods;
            int command;
            int action;
            const char *note;
        };
        const auto check_core = [&](const char *core, const std::vector<PadCommand> &table,
                                    std::initializer_list<Want> wants) {
            PadBinds binds;
            (void)apply_default_pad_binds(binds, table, core);
            for (const Want &w : wants) {
                int want_id = w.action;
                const char *want_label = "(画面側の操作)";
                if (w.command != 0) {
                    want_id = 0;
                    want_label = "(空)";
                    for (const PadCommand &c : table) {
                        if (c.command == w.command) {
                            want_id = c.id;
                            want_label = c.label_utf8.c_str();
                            break;
                        }
                    }
                } else if (w.action == 0) {
                    want_label = "(空)";
                }
                const int got = binds.command[w.mods][static_cast<int>(w.input)];
                if (got != want_id) {
                    const char *got_label = "(空)";
                    for (const PadCommand &c : table) {
                        if (c.id == got) {
                            got_label = c.label_utf8.c_str();
                            break;
                        }
                    }
                    char buf[320];
                    std::snprintf(buf, sizeof(buf), "既定の割り当て: %s の %s は「%s」のはずが「%s」", core, w.note,
                        want_label, got_label);
                    fail(buf);
                }
            }
        };

        const int L = kPadModCtrl;
        const int R = kPadModAlt;
        //! 変愚系（変愚・短愚・幻想蛮怒・Frox）。原作の `menu_info` の字は 4 コアとも同じ（2026-10-15 に原作ソースで確認）。
        const std::vector<PadCommand> hengband_family = make_table({ { 'g', "拾う" }, { 'f', "撃つ" }, { '<', "階段を上る" },
            { '>', "階段を下りる" }, { 's', "探す" }, { 'T', "穴を掘る" }, { 'z', "ロッドを振る" }, { 'A', "始動する" },
            { 'o', "開ける" }, { 'c', "閉じる" }, { 'a', "魔法棒で狙う" }, { 'u', "杖を使う" }, { 'S', "探索モード" },
            { 'v', "投げる" }, { 'm', "使う" } });
        for (const char *core : { "hengband", "tangband", "gensoband", "toband", "frox" }) {
            check_core(core, hengband_family,
                { { PadInput::X, 0, 'g', 0, "X(拾う)" }, { PadInput::Y, 0, 'f', 0, "Y(撃つ)" },
                    { PadInput::LeftShoulder, 0, '<', 0, "LB(階段を上る)" },
                    { PadInput::RightShoulder, 0, '>', 0, "RB(階段を下りる)" },
                    { PadInput::LeftTrigger, 0, 's', 0, "LT(探す)" }, { PadInput::RightTrigger, 0, 'T', 0, "RT(掘る)" },
                    { PadInput::X, L, 'z', 0, "LB+X(ロッド)" }, { PadInput::Y, L, 'A', 0, "LB+Y(発動)" },
                    { PadInput::A, R, 'o', 0, "RB+A(開ける)" }, { PadInput::B, R, 'c', 0, "RB+B(閉じる)" },
                    { PadInput::X, R, 'a', 0, "RB+X(魔法棒)" }, { PadInput::Y, R, 'u', 0, "RB+Y(杖)" },
                    { PadInput::Start, 0, 0, kActionCommandMenu, "Start(コマンドメニュー)" },
                    { PadInput::LeftStick, 0, 0, kActionFpsToggle, "L3(一人称)" } });
        }

        /*
         * Rogue 系。**開ける・閉じる・掘る・発動・ロッド・杖（魔法棒とは別）は原作に無い**ので空。
         * 「撃つ」は投げる `t`、魔法棒は Rogue の杖（`z`。魔法棒と杖の区別が無い）。
         * 拾う `,` は 5.4 と RogueClone2 だけ（3.6 は歩いて拾う）。
         */
        const std::initializer_list<Want> rogue_common = {
            { PadInput::Y, 0, 't', 0, "Y(撃つ→投げる)" }, { PadInput::LeftShoulder, 0, '<', 0, "LB(階段を上る)" },
            { PadInput::RightShoulder, 0, '>', 0, "RB(階段を下りる)" }, { PadInput::LeftTrigger, 0, 's', 0, "LT(探す)" },
            { PadInput::RightTrigger, 0, 0, 0, "RT(掘る=無し)" }, { PadInput::X, L, 0, 0, "LB+X(ロッド=無し)" },
            { PadInput::Y, L, 0, 0, "LB+Y(発動=無し)" }, { PadInput::A, R, 0, 0, "RB+A(開ける=無し)" },
            { PadInput::B, R, 0, 0, "RB+B(閉じる=無し)" }, { PadInput::X, R, 'z', 0, "RB+X(魔法棒→杖を振る)" },
            { PadInput::Y, R, 0, 0, "RB+Y(杖=魔法棒と同じ字のため空)" },
            { PadInput::Start, 0, 0, kActionCommandMenu, "Start(コマンドメニュー)" },
            { PadInput::LeftStick, 0, 0, kActionFpsToggle, "L3(一人称)" } };
        const std::vector<PadCommand> rogue36 = make_table({ { '>', "階段を降りる" }, { '<', "階段を上る" },
            { 's', "周囲を探索する" }, { ' ', "その場で休む" }, { 'f', "方向を指定して連続移動" }, { 'S', "保存して終了" },
            { 'Q', "ゲームを終了" }, { '/', "記号の意味を調べる" }, { '?', "原作ヘルプ" }, { 'v', "バージョン情報" },
            { 'V', "スコア一覧" }, { 0x12, "直前のメッセージを表示" }, { 0x0c, "画面を再描画" }, { 'o', "原作の設定" },
            { '0', "繰り返し回数を指定" }, { 't', "投げる" }, { 'q', "飲む" }, { 'r', "読む" }, { 'e', "食べる" },
            { 'z', "杖を振る" }, { 'p', "方向を指定して杖を振る" } });
        check_core("Rogue 3.6", rogue36, rogue_common);
        check_core("Rogue 3.6", rogue36, { { PadInput::X, 0, 0, 0, "X(拾う=3.6 には無い)" } });
        const std::vector<PadCommand> rogue54 = make_table({ { '>', "階段を降りる" }, { '<', "階段を上る" },
            { ',', "足元の物を拾う" }, { 's', "周囲を探索する" }, { '.', "その場で休む" }, { 'f', "指定方向の敵と連続して戦う" },
            { 'F', "指定方向の敵を倒すまで戦う" }, { 'g', "方向を指定して連続移動" }, { 'a', "直前のコマンドを繰り返す" },
            { '^', "指定方向の罠を調べる" }, { 'm', "指定方向の物の上へ移動する" }, { 'S', "保存して終了" },
            { 'Q', "ゲームを終了" }, { '/', "記号の意味を調べる" }, { '?', "原作ヘルプ" }, { 'v', "バージョン情報" },
            { 'V', "スコア一覧" }, { 'D', "発見済みの物を表示" }, { '@', "現在の状態を表示" }, { 0x10, "直前のメッセージを表示" },
            { 0x12, "画面を再描画" }, { 'o', "原作の設定" }, { '0', "繰り返し回数を指定" }, { 't', "投げる" }, { 'q', "飲む" },
            { 'r', "読む" }, { 'e', "食べる" }, { 'z', "杖を振る" }, { ' ', "何もしない" } });
        check_core("Rogue 5.4", rogue54, rogue_common);
        check_core("Rogue 5.4", rogue54, { { PadInput::X, 0, ',', 0, "X(拾う)" } });
        const std::vector<PadCommand> rogue2 = make_table({ { '>', "階段を降りる" }, { '<', "階段を上る" },
            { ',', "足元の物を拾う" }, { 'm', "物の上へ移動する" }, { 'a', "直前のコマンドを繰り返す" }, { '.', "その場で休む" },
            { '0', "繰り返し回数を指定" }, { 's', "周囲を探索する" }, { '^', "罠を調べる" }, { 'f', "指定方向へ連続攻撃" },
            { 'F', "指定方向へ移動しながら戦う" }, { 0x06, "指定方向へ走る" }, { 'S', "保存して終了" }, { 'Q', "ゲームを終了" },
            { 'v', "RogueClone2の版情報" }, { 'D', "発見済みの物を種類別に表示" }, { '/', "記号の意味を調べる" },
            { '@', "現在の状態を表示" }, { 0x10, "直前のメッセージを再表示" }, { '*', "得点表を見る" }, { '?', "原作のヘルプ" },
            { 0x01, "平均HPを表示" }, { 'o', "RogueClone2の設定" }, { 'e', "食べる" }, { 'q', "飲む" }, { 'r', "読む" },
            { 'd', "捨てる" }, { 'z', "杖を振る" }, { 't', "投げる" }, { ' ', "何もしない" } });
        check_core("RogueClone2", rogue2, rogue_common);
        check_core("RogueClone2", rogue2, { { PadInput::X, 0, ',', 0, "X(拾う)" } });

        //! Umoria。拾うコマンドが無い（自動で拾う。`-` は「拾わずに移動」）ので X は空。ロッド・発動も無い。
        const std::vector<PadCommand> umoria = make_table({ { 'R', "休息" }, { 'S', "探索モードの入切" }, { 's', "探す" },
            { 'l', "見る" }, { '<', "階段を上る" }, { '>', "階段を下りる" }, { '.', "走る" }, { '-', "拾わずに移動" },
            { 'o', "開ける" }, { 'c', "閉じる" }, { 'B', "体当たり" }, { 'j', "くさびを打つ" }, { 'D', "罠を解除" },
            { 'T', "掘る" }, { 'r', "巻物を読む" }, { 'q', "薬を飲む" }, { 'u', "杖を使う" }, { 'a', "魔法棒を向ける" },
            { 'E', "食べる" }, { 'f', "撃つ・投げる" }, { 'w', "装備する" }, { 't', "外す" }, { 'x', "武器の持ち替え" },
            { 'F', "ランプに油を足す" }, { 'd', "捨てる" }, { 'm', "呪文を唱える" }, { 'p', "祈る" }, { 'b', "本を見る" } });
        check_core("Umoria", umoria,
            { { PadInput::X, 0, 0, 0, "X(拾う=Umoria には無い)" }, { PadInput::Y, 0, 'f', 0, "Y(撃つ)" },
                { PadInput::LeftShoulder, 0, '<', 0, "LB(階段を上る)" }, { PadInput::RightShoulder, 0, '>', 0, "RB(階段を下りる)" },
                { PadInput::LeftTrigger, 0, 's', 0, "LT(探す)" }, { PadInput::RightTrigger, 0, 'T', 0, "RT(掘る)" },
                { PadInput::X, L, 0, 0, "LB+X(ロッド=無し)" }, { PadInput::Y, L, 0, 0, "LB+Y(発動=無し)" },
                { PadInput::A, R, 'o', 0, "RB+A(開ける)" }, { PadInput::B, R, 'c', 0, "RB+B(閉じる)" },
                { PadInput::X, R, 'a', 0, "RB+X(魔法棒)" }, { PadInput::Y, R, 'u', 0, "RB+Y(杖)" },
                { PadInput::Start, 0, 0, kActionCommandMenu, "Start(コマンドメニュー)" },
                { PadInput::LeftStick, 0, 0, kActionFpsToggle, "L3(一人称)" } });

        //! Angband。`s` は「盗む」であって「探す」ではない（4.2 は罠・隠し扉の探索を廃止した）ので LT は空。
        const std::vector<PadCommand> angband = make_table({ { '{', "刻む" }, { 'w', "装備する" }, { 't', "外す" },
            { 'I', "調べる" }, { 'd', "落とす" }, { 'f', "撃つ" }, { 'u', "杖を使う" }, { 'a', "魔法棒で狙う" },
            { 'z', "ロッドを振る" }, { 'A', "発動する" }, { 'E', "食べる" }, { 'q', "飲む" }, { 'r', "読む" }, { 'F', "燃料を足す" },
            { 'D', "罠を解除" }, { 'R', "休息" }, { 'l', "見る" }, { '*', "ターゲット" }, { 'T', "穴を掘る" }, { '<', "階段を上る" },
            { '>', "階段を下りる" }, { 'o', "開ける" }, { 'c', "閉じる" }, { 'h', "最寄りを撃つ" }, { 'v', "投げる" },
            { 'e', "装備一覧" }, { 'i', "持ち物一覧" }, { 'g', "拾う" }, { 'b', "本を見る" }, { 'm', "呪文を唱える" },
            { 's', "盗む" }, { ';', "歩く" }, { '.', "走る" }, { 'p', "探検する" }, { 'n', "直前の繰り返し" } });
        check_core("angband", angband,
            { { PadInput::X, 0, 'g', 0, "X(拾う)" }, { PadInput::Y, 0, 'f', 0, "Y(撃つ)" },
                { PadInput::LeftShoulder, 0, '<', 0, "LB(階段を上る)" }, { PadInput::RightShoulder, 0, '>', 0, "RB(階段を下りる)" },
                { PadInput::LeftTrigger, 0, 0, 0, "LT(探す=Angband には無い。`s` は盗む)" },
                { PadInput::RightTrigger, 0, 'T', 0, "RT(掘る)" }, { PadInput::X, L, 'z', 0, "LB+X(ロッド)" },
                { PadInput::Y, L, 'A', 0, "LB+Y(発動)" }, { PadInput::A, R, 'o', 0, "RB+A(開ける)" },
                { PadInput::B, R, 'c', 0, "RB+B(閉じる)" }, { PadInput::X, R, 'a', 0, "RB+X(魔法棒)" },
                { PadInput::Y, R, 'u', 0, "RB+Y(杖)" }, { PadInput::Start, 0, 0, kActionCommandMenu, "Start(コマンドメニュー)" },
                { PadInput::LeftStick, 0, 0, kActionFpsToggle, "L3(一人称)" } });

        /*
         * --- 5c. 前の版（cfg の `pad_defaults=1`）の設定を読んだときの移し替え---
         * 前の版は変愚の並びを字のままどのコアの表でも引いていた（Rogue 5.4 の Y が戦う・Angband の LT が盗む）。
         * 新しい既定を入れても、`pad_defaults=1` の cfg では当て直されず、前の版の既定が残っていた。
         * 版 1 で**前の版の既定のまま**の枠だけを今の既定へ移し、遊ぶ人が変えた枠は残すことを見る。
         */
        const auto same_binds = [](const PadBinds &a, const PadBinds &b) {
            for (int mods = 0; mods < kPadModLayerCount; ++mods) {
                for (int i = 0; i < kPadInputCount; ++i) {
                    if (a.command[mods][i] != b.command[mods][i]) {
                        return false;
                    }
                }
            }
            return true;
        };
        const auto id_of = [](const std::vector<PadCommand> &table, int command) {
            for (const PadCommand &c : table) {
                if (c.command == command) {
                    return c.id;
                }
            }
            return 0;
        };
        const std::vector<PadCommand> silq_table = make_table({ { '/', "変える" }, { 'g', "拾う" }, { '<', "階段を上る" },
            { '>', "階段を下りる" }, { 'z', "とどまる" }, { 'm', "主メニュー" }, { 'S', "隠密の構え" }, { 'w', "装備する" },
            { 'r', "外す" }, { 'd', "落とす" }, { 'c', "閉じる" }, { 'f', "撃つ" }, { 'F', "第二の矢筒から撃つ" } });
        //! Sil-Q の既定。Start は原作の主メニュー（`m`）ではなく、ほかのコアと同じ画面のコマンドメニュー。
        check_core("silq", silq_table,
            { { PadInput::X, 0, '/', 0, "X(変える)" }, { PadInput::Y, 0, 'g', 0, "Y(拾う)" },
                { PadInput::LeftShoulder, 0, '<', 0, "LB(階段を上る)" }, { PadInput::RightShoulder, 0, '>', 0, "RB(階段を下りる)" },
                { PadInput::LeftTrigger, 0, 0, 0, "LT(割り当て無し)" }, { PadInput::RightTrigger, 0, 'z', 0, "RT(とどまる)" },
                { PadInput::Start, 0, 0, kActionCommandMenu, "Start(コマンドメニュー。主メニュー `m` ではない)" },
                { PadInput::LeftStick, 0, 0, kActionFpsToggle, "L3(一人称)" }, { PadInput::RightStick, 0, 'S', 0, "R3(隠密の構え)" },
                { PadInput::A, L, 'w', 0, "LB+A(装備する)" }, { PadInput::B, L, 'r', 0, "LB+B(外す)" },
                { PadInput::Y, L, 'd', 0, "LB+Y(落とす)" }, { PadInput::B, R, 'c', 0, "RB+B(閉じる)" },
                { PadInput::X, R, 'f', 0, "RB+X(撃つ)" }, { PadInput::Y, R, 'F', 0, "RB+Y(第二の矢筒から撃つ)" } });
        struct MigrateCase {
            const char *core;
            const std::vector<PadCommand> *table;
            bool changes; //!< 前の版と今の版の既定が違う（＝移し替えで中身が変わる）
        };
        const MigrateCase migrate_cases[] = { { "hengband", &hengband_family, false }, { "tangband", &hengband_family, false },
            { "gensoband", &hengband_family, false }, { "toband", &hengband_family, false },
            { "frox", &hengband_family, false }, { "silq", &silq_table, true },
            { "Umoria", &umoria, false }, { "angband", &angband, true }, { "Rogue 3.6", &rogue36, true },
            { "Rogue 5.4", &rogue54, true }, { "RogueClone2", &rogue2, true } };
        for (const MigrateCase &mc : migrate_cases) {
            PadBinds v1{};
            (void)apply_default_pad_binds(v1, *mc.table, mc.core, true);
            PadBinds fresh{};
            (void)apply_default_pad_binds(fresh, *mc.table, mc.core);
            if (same_binds(v1, fresh) == mc.changes) {
                fail((std::string("前の版と今の版の既定の差が想定と違う: ") + mc.core).c_str());
            }
            //! 版 1 の設定で前の版の既定のまま → 今の既定になる（変愚系・Sil-Q・Umoria は中身が変わらない）。
            PadBinds got = v1;
            int version = 1;
            const bool changed = upgrade_pad_defaults(got, version, *mc.table, mc.core);
            if (!same_binds(got, fresh)) {
                fail((std::string("版 1 の設定（前の版の既定のまま）が今の既定にならない: ") + mc.core).c_str());
            }
            if (version != kPadDefaultsVersion) {
                fail((std::string("移したあとの版が今の版にならない: ") + mc.core).c_str());
            }
            if (changed != mc.changes) {
                fail((std::string("移し替えで割り当てが変わったかの返り値が違う: ") + mc.core).c_str());
            }
            //! 版 2 の設定は触らない（前の版の既定のように見える枠があっても、版 2 は遊ぶ人の持ち物）。
            PadBinds untouched = v1;
            version = kPadDefaultsVersion;
            if (upgrade_pad_defaults(untouched, version, *mc.table, mc.core) || !same_binds(untouched, v1)) {
                fail((std::string("版 2 の設定を触ってしまう: ") + mc.core).c_str());
            }
            //! 版 0（まだ入れていない）は今の既定がそのまま入る。
            PadBinds first{};
            version = 0;
            (void)upgrade_pad_defaults(first, version, *mc.table, mc.core);
            if (!same_binds(first, fresh) || (version != kPadDefaultsVersion)) {
                fail((std::string("版 0 の設定に今の既定が入らない: ") + mc.core).c_str());
            }
        }
        /*
         * Sil-Q：前の版は Start が原作の主メニュー（`m`）だった。版 1 の設定で Start が `m` のままなら
         * コマンドメニューへ移り、ほかの枠は動かない。遊ぶ人が Start を別のものへ変えていたら残る。
         */
        {
            const int id_main = id_of(silq_table, 'm');
            const int id_drop = id_of(silq_table, 'd');
            PadBinds v1{};
            (void)apply_default_pad_binds(v1, silq_table, "silq", true);
            if (v1.command[0][static_cast<int>(PadInput::Start)] != id_main) {
                fail("検査の足場：Sil-Q の前の版の既定の Start が主メニュー（m）でない");
            }
            PadBinds fresh{};
            (void)apply_default_pad_binds(fresh, silq_table, "silq");
            //! 前の版と今の版の差は Start の 1 枠だけ。
            for (int mods = 0; mods < kPadModLayerCount; ++mods) {
                for (int i = 0; i < kPadInputCount; ++i) {
                    const bool is_start = (mods == 0) && (i == static_cast<int>(PadInput::Start));
                    if ((v1.command[mods][i] != fresh.command[mods][i]) != is_start) {
                        fail("Sil-Q：前の版と今の版の既定の差が Start の 1 枠だけでない");
                    }
                }
            }
            //! (a) 何も変えていない：Start が主メニューからコマンドメニューへ移り、主メニュー自体の割り当ては残らない。
            PadBinds a = v1;
            int version = 1;
            const bool changed = upgrade_pad_defaults(a, version, silq_table, "silq");
            if (!changed || (a.command[0][static_cast<int>(PadInput::Start)] != kActionCommandMenu)
                || (a.input_for(id_main).input != PadInput::Count) || !same_binds(a, fresh)) {
                fail("Sil-Q：版 1 の設定の Start（主メニュー）がコマンドメニューへ移らない");
            }
            //! (b) 遊ぶ人が Start を別のコマンド（落とす）へ変えていた：残る。
            PadBinds b = v1;
            b.assign(id_drop, PadInput::Start);
            version = 1;
            (void)upgrade_pad_defaults(b, version, silq_table, "silq");
            if (b.command[0][static_cast<int>(PadInput::Start)] != id_drop) {
                fail("Sil-Q：遊ぶ人が変えた Start が移し替えで上書きされる");
            }
            //! (c) 遊ぶ人が Start を外していた：外したままになる（コマンドメニューは復活しない）。
            PadBinds c = v1;
            c.clear(id_main);
            version = 1;
            (void)upgrade_pad_defaults(c, version, silq_table, "silq");
            if (c.command[0][static_cast<int>(PadInput::Start)] != 0) {
                fail("Sil-Q：遊ぶ人が外した Start が復活する");
            }
        }
        //! 変愚の設定は、遊ぶ人が枠を変えていても中身が変わらない（前の版と今の版の既定が同じ）。
        {
            PadBinds mine{};
            (void)apply_default_pad_binds(mine, hengband_family, "hengband", true);
            mine.assign(id_of(hengband_family, 'v'), PadInput::Y); // 遊ぶ人が Y を「投げる」へ変えた
            mine.clear(id_of(hengband_family, 'g'));                 // 遊ぶ人が X を外した
            const PadBinds before_migrate = mine;
            int version = 1;
            (void)upgrade_pad_defaults(mine, version, hengband_family, "hengband");
            if (!same_binds(mine, before_migrate)) {
                fail("変愚の版 1 の設定（遊ぶ人が変えたもの）が移し替えで変わる");
            }
        }
        //! Rogue 5.4：前の版の既定のうち今の既定と同じ枠は移り、遊ぶ人が変えた枠は残る。
        {
            const int id_throw = id_of(rogue54, 't');
            const int id_pick = id_of(rogue54, ',');
            const int id_wand = id_of(rogue54, 'z');
            const int id_fight = id_of(rogue54, 'f');
            PadBinds v1{};
            (void)apply_default_pad_binds(v1, rogue54, "Rogue 5.4", true);
            //! 前提：前の版は Y が戦う・LB+X が杖だった。
            if ((v1.command[0][static_cast<int>(PadInput::Y)] != id_fight)
                || (v1.command[kPadModCtrl][static_cast<int>(PadInput::X)] != id_wand)) {
                fail("検査の足場：Rogue 5.4 の前の版の既定が想定と違う");
            }
            //! (a) 何も変えていない：Y=投げる・X=拾う・LB+X は空・RB+X=杖。
            PadBinds a = v1;
            int version = 1;
            (void)upgrade_pad_defaults(a, version, rogue54, "Rogue 5.4");
            if ((a.command[0][static_cast<int>(PadInput::Y)] != id_throw)
                || (a.command[0][static_cast<int>(PadInput::X)] != id_pick)
                || (a.command[kPadModCtrl][static_cast<int>(PadInput::X)] != 0)
                || (a.command[kPadModAlt][static_cast<int>(PadInput::X)] != id_wand)) {
                fail("Rogue 5.4：版 1 の設定が Y=投げる・X=拾う・LB+X=空・RB+X=杖にならない");
            }
            //! (b) 遊ぶ人が Y を別のコマンド（食べる）へ変えていた：Y は残り、ほかは移る。
            PadBinds b = v1;
            const int id_eat = id_of(rogue54, 'e');
            b.assign(id_eat, PadInput::Y);
            version = 1;
            (void)upgrade_pad_defaults(b, version, rogue54, "Rogue 5.4");
            if ((b.command[0][static_cast<int>(PadInput::Y)] != id_eat)
                || (b.command[0][static_cast<int>(PadInput::X)] != id_pick)
                || (b.command[kPadModAlt][static_cast<int>(PadInput::X)] != id_wand)) {
                fail("Rogue 5.4：遊ぶ人が変えた枠（Y）が残らない、またはほかの枠が移らない");
            }
            //! (c) 遊ぶ人が X を外していた：外したままになる（新しい既定の「拾う」は復活しない）。
            PadBinds c = v1;
            c.clear(c.command[0][static_cast<int>(PadInput::X)]);
            version = 1;
            (void)upgrade_pad_defaults(c, version, rogue54, "Rogue 5.4");
            if (c.command[0][static_cast<int>(PadInput::X)] != 0) {
                fail("Rogue 5.4：遊ぶ人が外した枠（X）が復活する");
            }
            //! (d) 遊ぶ人が「投げる」を LB+Y へ移していた：新しい既定の Y=投げる は置かない（同じ操作を 2 枠に持たせない）。
            PadBinds d = v1;
            d.assign(id_throw, PadInput::Y, kPadModCtrl);
            version = 1;
            (void)upgrade_pad_defaults(d, version, rogue54, "Rogue 5.4");
            if ((d.command[kPadModCtrl][static_cast<int>(PadInput::Y)] != id_throw)
                || (d.command[0][static_cast<int>(PadInput::Y)] == id_throw)) {
                fail("Rogue 5.4：遊ぶ人が別の枠へ移した操作を、新しい既定が奪う");
            }
        }
        //! Angband：LT（前の版では盗む）は空になる。
        {
            PadBinds v1{};
            (void)apply_default_pad_binds(v1, angband, "angband", true);
            if (v1.command[0][static_cast<int>(PadInput::LeftTrigger)] != id_of(angband, 's')) {
                fail("検査の足場：Angband の前の版の既定の LT が「盗む」でない");
            }
            int version = 1;
            (void)upgrade_pad_defaults(v1, version, angband, "angband");
            if (v1.command[0][static_cast<int>(PadInput::LeftTrigger)] != 0) {
                fail("Angband：版 1 の LT（盗む）が空にならない");
            }
        }
        //! cfg の `pad_defaults` は版として読み書きする（前の版の cfg の `1` は版 1 のまま届く）。
        {
            const std::filesystem::path cfg = std::filesystem::temp_directory_path() / "hd2d_pad_check_defaults.cfg";
            for (const int want : { 0, 1, 2 }) {
                Hd2dSettings saved;
                saved.pad_defaults_version = want;
                Hd2dSettings loaded;
                loaded.pad_defaults_version = 99;
                if (!save_settings(cfg.string(), saved) || !load_settings(cfg.string(), loaded)
                    || (loaded.pad_defaults_version != want)) {
                    fail("cfg の pad_defaults（版）が書いて読んで同じにならない");
                }
            }
            {
                std::ofstream legacy(cfg, std::ios::binary | std::ios::trunc);
                legacy << "pad_defaults=1\n";
            }
            Hd2dSettings from_legacy;
            if (!load_settings(cfg.string(), from_legacy) || (from_legacy.pad_defaults_version != 1)) {
                fail("前の版の cfg（pad_defaults=1）が版 1 として読めない");
            }
            std::error_code ec;
            std::filesystem::remove(cfg, ec);
        }
    }

    /*
     * --- 6. すでに繋がっている機器は、起こし直した後の GamePad も開く（修-03） ---
     * 言語切り替えは SDL_Quit をせず同じプロセスで run() をやり直す。機器の追加の知らせ（SDL_CONTROLLERDEVICEADDED）は
     * SDL_Init のときにしか来ないので、知らせだけに頼ると、起こし直した後の十字キーが二度と効かない。
     * SDL の仮想ゲームコントローラを繋いだままにして、新しい GamePad が `open_present_devices()` で開くことを確かめる。
     */
#if defined(_WIN32) && SDL_VERSION_ATLEAST(2, 0, 14) && !defined(HENGBAND_QUEST)
    {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            fail("検査の足場：SDL のゲームコントローラを初期化できない");
        } else {
            const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
            if (index < 0) {
                fail("検査の足場：仮想のゲームコントローラを繋げない");
            } else {
                if (SDL_IsGameController(index) == SDL_FALSE) {
                    //! 対応表が無ければ足す（仮想機器の GUID に、標準の割り当てを付ける）。
                    char guid[64] = { 0 };
                    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof(guid));
                    const std::string mapping = std::string(guid)
                        + ",Virtual Pad,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,"
                          "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
                          "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,";
                    (void)SDL_GameControllerAddMapping(mapping.c_str());
                }
                if (SDL_IsGameController(index) == SDL_FALSE) {
                    fail("検査の足場：仮想の機器をゲームコントローラとして扱えない");
                } else {
                    //! 対照（直す前の状態）：知らせを受け取らなかった GamePad は、機器が繋がっていても開いていない。
                    GamePad late;
                    if (late.connected()) {
                        fail("機器の追加の知らせを受け取っていない GamePad が、最初から開いている（検査の足場）");
                    }
                    late.open_present_devices();
                    if (!late.connected()) {
                        fail("繋がっている機器があるのに、open_present_devices() の後も GamePad が開いていない（言語切り替え後に十字キーが効かない）");
                    }
                    //! 二度呼んでも二重に開かない（初回起動は知らせも届く）。名前が変わらず、開いたままであること。
                    const std::string first_name = late.name();
                    late.open_present_devices();
                    if (!late.connected() || (late.name() != first_name)) {
                        fail("open_present_devices() を二度呼んだら状態が変わった");
                    }
                    //! 開いた GamePad に知らせ（A ボタン）が届き、押しとして取り出せる。
                    PadPress press;
                    SDL_Event a{};
                    a.type = SDL_CONTROLLERBUTTONDOWN;
                    a.cbutton.button = SDL_CONTROLLER_BUTTON_A;
                    (void)late.on_event(a);
                    if (!late.take_pressed(press) || (press.input != PadInput::A)) {
                        fail("検査の足場：開いた GamePad が A の押しを出さない");
                    }
                    late.close();
                    if (late.connected()) {
                        fail("close() の後も GamePad が開いたままに見える");
                    }
                    //! 閉じた後でも、機器が残っていれば開き直せる（起こし直しを何度重ねても効く）。
                    late.open_present_devices();
                    if (!late.connected()) {
                        fail("閉じた後に open_present_devices() で開き直せない（起こし直しの 2 回目以降）");
                    }
                    late.close();
                }
                SDL_JoystickDetachVirtual(index);
            }
            SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        }
    }
#endif


    /*
     * --- 7. 斜め移動限定（`kActionDiagonalOnly`。決めたこと 2026-10-15） ---
     * 押している間だけ、地図の上の移動を斜めに限る。窓も実機も要らない：`GamePad` に SDL のコントローラーの
     * 出来事を直に渡し、`feed_external` で「繋がった」ことにして `poll_direction` を呼ぶ。
     * キーボードの振り分け（矢印の同時押し・テンキー・字・メニューの間）は `--ui-check` の SDL 入力の検査（`HD2D_SDL_INPUT_CHECK`）。
     */
    {
        //! (a) 既定・名前・一覧：キーボードにもパッドにも置かない（遊ぶ人が割り当てる）。
        const KeyBindEntry def = default_key_binds().find(kActionDiagonalOnly);
        if (def.keycode != 0) {
            fail("斜め移動限定に既定のキーが割り当てられている（既定は未割り当ての約束）");
        }
        for (const int key : { static_cast<int>(SDLK_INSERT), static_cast<int>(SDLK_RALT), static_cast<int>(SDLK_RCTRL) }) {
            if (default_key_binds().action_for(key, 0) == kActionDiagonalOnly) {
                fail("既定の割り当てを引くと斜め移動限定になるキーがある");
            }
        }
        bool listed = false;
        for (const int id : ui_action_ids()) {
            listed = listed || (id == kActionDiagonalOnly);
        }
        if (!listed) {
            fail("斜め移動限定が割り当ての画面の一覧（ui_action_ids）に無い");
        }
        const char *const label = ui_action_label(kActionDiagonalOnly);
        if ((label == nullptr) || (label[0] == '\0')) {
            fail("斜め移動限定に名前（ui_action_label）が無い");
        }
        const std::vector<PadCommand> none_commands;
        for (const char *const core : { "hengband", "tangband", "gensoband", "toband", "frox", "silq", "angband", "Umoria", "Rogue 3.6", "Rogue 5.4",
                 "RogueClone2" }) {
            PadBinds fresh;
            (void)apply_default_pad_binds(fresh, none_commands, core);
            if (fresh.input_for(kActionDiagonalOnly).input != PadInput::Count) {
                fail("パッドの既定に斜め移動限定が入っている（既定は未割り当ての約束）");
                break;
            }
        }
        //! キーボードの割り当て：単独の修飾キー（Shift・Ctrl・Alt の左右）も割り当てられる。修飾は持たず、
        //! 先に別の修飾を押していても（Shift を押してから右 Alt など）、AltGr の左 Ctrl つきでも引ける。
        {
            struct Lone {
                SDL_Keycode key;
                int own_mod;
            };
            for (const Lone lone : { Lone{ SDLK_LSHIFT, kModShift }, Lone{ SDLK_RSHIFT, kModShift }, Lone{ SDLK_LCTRL, kModCtrl },
                     Lone{ SDLK_RCTRL, kModCtrl }, Lone{ SDLK_LALT, kModAlt }, Lone{ SDLK_RALT, kModAlt } }) {
                KeyBinds binds = default_key_binds();
                if (!binds.assign(kActionDiagonalOnly, lone.key, lone.own_mod)) {
                    fail("斜め移動限定に単独の修飾キーを割り当てられない");
                    continue;
                }
                const KeyBindEntry got = binds.find(kActionDiagonalOnly);
                if ((got.keycode != lone.key) || (got.mods != 0)) {
                    fail("単独の修飾キーの割り当てが「そのキーだけ」（修飾なし）で記録されない");
                }
                for (const int mods : { 0, lone.own_mod, kModShift | kModAlt | kModCtrl }) {
                    if (binds.action_for(lone.key, mods) != kActionDiagonalOnly) {
                        fail("単独の修飾キーの割り当てが、ほかの修飾を押していると引けない");
                    }
                }
                if (key_display_name(got.keycode, got.mods).find('+') != std::string::npos) {
                    fail("単独の修飾キーの表示に「Alt+」のような修飾が付く");
                }
                //! cfg の綴りで往復する（古い綴り「修飾つき」で書かれていても同じ割り当てになる）。
                KeyBinds round;
                round.parse(binds.to_line());
                if (round.action_for(lone.key, lone.own_mod) != kActionDiagonalOnly) {
                    fail("単独の修飾キーの割り当てが cfg の綴りで往復できない");
                }
                KeyBinds old_form;
                old_form.parse("-11:" + std::to_string(static_cast<int>(lone.key)) + ":" + std::to_string(lone.own_mod));
                if (old_form.action_for(lone.key, 0) != kActionDiagonalOnly) {
                    fail("修飾つきの古い綴りの割り当てが単独の修飾キーとして読めない");
                }
            }
            //! ほかの操作の割り当ては今までどおり（修飾＋キーは修飾を持つ）。
            KeyBinds other = default_key_binds();
            (void)other.assign(kActionFeatureMenu, SDLK_F10, kModCtrl);
            if ((other.find(kActionFeatureMenu).mods != kModCtrl) || (other.action_for(SDLK_F10, 0) == kActionFeatureMenu)
                || (other.action_for(SDLK_F10, kModCtrl) != kActionFeatureMenu)) {
                fail("修飾＋キーの割り当てが今までどおりに修飾を持たない");
            }
            //! 同じキーを 2 つの操作に持たせない（修飾キーでも先客から外れる）。
            KeyBinds twice = default_key_binds();
            (void)twice.assign(kActionSubsToggle, SDLK_RALT, kModAlt);
            (void)twice.assign(kActionDiagonalOnly, SDLK_RALT, kModAlt);
            if ((twice.find(kActionSubsToggle).keycode != 0) || (twice.action_for(SDLK_RALT, kModAlt) != kActionDiagonalOnly)) {
                fail("同じ修飾キーが 2 つの操作に割り当たった");
            }
            //! ESC で消すと未割り当てに戻る（既定と同じ）。
            twice.clear(kActionDiagonalOnly);
            if (twice.find(kActionDiagonalOnly).keycode != 0) {
                fail("斜め移動限定の割り当てが消せない");
            }
        }
        //! パッドの割り当て：ボタンへ割り当てて、cfg の綴りで往復できる（負の番号 −11 も通る）。
        {
            PadBinds binds;
            if (!binds.assign(kActionDiagonalOnly, PadInput::X)) {
                fail("斜め移動限定をパッドのボタンに割り当てられない");
            }
            PadBinds round;
            round.parse(binds.to_line());
            if (round.input_for(kActionDiagonalOnly).input != binds.input_for(kActionDiagonalOnly).input) {
                fail("斜め移動限定のパッドの割り当てが cfg の綴りで往復できない");
            }
        }
        //! (b) cfg：既定は未割り当てなので、前の版の設定にも何も足さない。割り当てたものは書いて読んで同じ。
        {
            const std::filesystem::path cfg = std::filesystem::temp_directory_path() / "hd2d_pad_check_diag.cfg";
            const auto write_cfg = [&](const std::string &text) {
                std::ofstream out(cfg, std::ios::binary | std::ios::trunc);
                out << text;
            };
            const auto num = [](int v) { return std::to_string(v); };
            const std::string f10 = num(static_cast<int>(SDLK_F10));
            const std::string f1 = num(static_cast<int>(SDLK_F1));
            //! 前の版（epoch 1）の cfg：斜め移動限定は未割り当てのまま。遊ぶ人の元の割り当ても変わらない。
            write_cfg("key_bind=-1:" + f10 + ":0,-10:" + f1 + ":0\nkey_bind_epoch=1\n");
            Hd2dSettings a;
            if (!load_settings(cfg.string(), a)) {
                fail("検査の足場：cfg が読めない");
            }
            if (a.key_binds.find(kActionDiagonalOnly).keycode != 0) {
                fail("前の版の cfg に、斜め移動限定の割り当てが勝手に足された");
            }
            if ((a.key_binds.find(kActionFeatureMenu).keycode != SDLK_F10) || (a.key_binds.find(kActionCommandMenu).keycode != SDLK_F1)) {
                fail("読み込みで、遊ぶ人の元の割り当てが変わった");
            }
            //! 一番古い cfg（epoch の行が無い）：F1（epoch 1 で足した既定）は届くが、斜め移動限定は未割り当て。
            write_cfg("key_bind=-1:" + f10 + ":0\n");
            Hd2dSettings e;
            (void)load_settings(cfg.string(), e);
            if ((e.key_binds.find(kActionCommandMenu).keycode != SDLK_F1) || (e.key_binds.find(kActionDiagonalOnly).keycode != 0)) {
                fail("epoch の無い古い cfg：F1 の既定は届き、斜め移動限定は未割り当てのままのはず");
            }
            //! key_bind の行が無い cfg は、初期値（既定）のまま未割り当て。
            write_cfg("lang=ja\n");
            Hd2dSettings f;
            (void)load_settings(cfg.string(), f);
            if (f.key_binds.find(kActionDiagonalOnly).keycode != 0) {
                fail("key_bind の行が無い cfg で、斜め移動限定に割り当てがある");
            }
            //! 書いて読んでも同じ。右 Alt を単独で割り当てると cfg には修飾なしで書かれ、epoch は 1 のまま。
            Hd2dSettings saved;
            (void)saved.key_binds.assign(kActionDiagonalOnly, SDLK_RALT, kModAlt);
            Hd2dSettings back;
            if (!save_settings(cfg.string(), saved) || !load_settings(cfg.string(), back)
                || (back.key_binds.find(kActionDiagonalOnly).keycode != SDLK_RALT)
                || (back.key_binds.find(kActionDiagonalOnly).mods != 0)) {
                fail("斜め移動限定に割り当てた右 Alt が、cfg に書いて読んで同じにならない");
            }
            {
                std::ifstream in(cfg, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (text.find("key_bind_epoch=1") == std::string::npos) {
                    fail("cfg に key_bind_epoch=1 が書かれていない（epoch は上げない約束）");
                }
            }
            //! 割り当てを消した設定は、読み直しても未割り当て（勝手に戻らない）。
            Hd2dSettings cleared;
            cleared.key_binds.clear(kActionDiagonalOnly);
            Hd2dSettings back2;
            if (!save_settings(cfg.string(), cleared) || !load_settings(cfg.string(), back2)
                || (back2.key_binds.find(kActionDiagonalOnly).keycode != 0)) {
                fail("消した斜め移動限定の割り当てが、読み直すと戻る");
            }
            std::error_code ec;
            std::filesystem::remove(cfg, ec);
        }
        //! (c) スティックと十字：斜めへ寄せる・軸の前後は動かない・押している間だけ。
        {
            GamePad pad;
            std::uint32_t now = 0;
            const auto feed = [&](float x, float y) {
                ExternalPadState s{};
                s.left_x = x;
                s.left_y = y;
                pad.feed_external(s);
            };
            //! 真上を 0 度として時計回りの角度で倒す。
            const auto tilt = [&](double deg, float mag) {
                const double rad = deg * 3.14159265358979323846 / 180.0;
                feed(static_cast<float>(std::sin(rad) * mag), static_cast<float>(-std::cos(rad) * mag));
            };
            const auto poll = [&](int &dx, int &dy) {
                now += 1000;
                dx = 0;
                dy = 0;
                return pad.poll_direction(now, true, dx, dy);
            };
            const auto neutral = [&]() {
                feed(0.f, 0.f);
                int dx = 0;
                int dy = 0;
                (void)poll(dx, dy); //!< 中立を見せる（挿した直後の「一度中立を見るまで受け付けない」を解く）
            };
            const auto dpad = [&](Uint8 button, bool down) {
                SDL_Event e{};
                e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
                e.cbutton.button = button;
                (void)pad.on_event(e);
            };
            neutral();
            int dx = 0;
            int dy = 0;
            //! 対照：斜め移動限定でなければ、今までどおり真上は北へ 1 歩。
            tilt(0.0, 1.f);
            if (!poll(dx, dy) || (dx != 0) || (dy != -1)) {
                fail("対照：斜め移動限定でない真上のスティックが北へ歩かない（今までの動きが変わった）");
            }
            neutral();
            pad.set_diagonal_only(true);
            //! 軸の上・軸の前後（20 度の内側）は動かない。
            for (const double deg : { 0.0, 90.0, 180.0, 270.0, 10.0, 19.0, 81.0, 100.0, 350.0, 265.0 }) {
                neutral();
                tilt(deg, 1.f);
                if (poll(dx, dy)) {
                    std::fprintf(stderr, "       角度 %.0f 度で (%d,%d) が出た\n", deg, dx, dy);
                    fail("斜め移動限定：軸の前後のスティックが動いた（真上・真横は動かない約束）");
                }
            }
            //! 斜めの側（軸から 21 度以上）は、倒した向きにいちばん近い斜め 4 方向へ。
            struct DiagCase {
                double deg;
                int dx;
                int dy;
            };
            for (const DiagCase &c : { DiagCase{ 21.0, 1, -1 }, DiagCase{ 45.0, 1, -1 }, DiagCase{ 69.0, 1, -1 }, DiagCase{ 111.0, 1, 1 },
                     DiagCase{ 135.0, 1, 1 }, DiagCase{ 159.0, 1, 1 }, DiagCase{ 201.0, -1, 1 }, DiagCase{ 225.0, -1, 1 },
                     DiagCase{ 249.0, -1, 1 }, DiagCase{ 291.0, -1, -1 }, DiagCase{ 315.0, -1, -1 }, DiagCase{ 339.0, -1, -1 } }) {
                neutral();
                tilt(c.deg, 1.f);
                if (!poll(dx, dy) || (dx != c.dx) || (dy != c.dy)) {
                    std::fprintf(stderr, "       角度 %.0f 度：期待 (%d,%d)・実際 (%d,%d)\n", c.deg, c.dx, c.dy, dx, dy);
                    fail("斜め移動限定：スティックを斜め側へ倒したのに、近い斜め 4 方向のどれかにならない");
                }
            }
            //! 倒す量：入るのは 18000（約 0.55）以上。弱い傾きは斜めでも動かない。
            neutral();
            tilt(45.0, 0.5f);
            if (poll(dx, dy)) {
                fail("斜め移動限定：倒し足りないスティックが動いた");
            }
            tilt(45.0, 0.6f);
            if (!poll(dx, dy) || (dx != 1) || (dy != -1)) {
                fail("斜め移動限定：十分倒したスティックが斜めにならない");
            }
            //! ヒステリシス：斜めの間は、角度が 18 度（入る 20 度より内側）でも斜めのまま。16 度を割ると止まる。
            neutral();
            tilt(45.0, 1.f);
            if (!poll(dx, dy)) {
                fail("検査の足場：斜めに入れない");
            }
            tilt(18.0, 1.f);
            if (!poll(dx, dy) || (dx != 1) || (dy != -1)) {
                fail("斜め移動限定：斜めの間に角度が少し軸へ寄ったら止まった（境目でがたつく）");
            }
            tilt(14.0, 1.f);
            if (poll(dx, dy)) {
                fail("斜め移動限定：軸の 16 度の内側へ入っても斜めのまま動き続ける");
            }
            neutral();
            tilt(18.0, 1.f); //!< 中立から直に 18 度へ倒しても入らない（入るのは 20 度から）
            if (poll(dx, dy)) {
                fail("斜め移動限定：中立から 18 度へ倒しただけで動いた（入る角度は 20 度）");
            }
            //! 倒す量のヒステリシス：入ったあとは 11000（約 0.34）を割るまで斜めのまま。
            neutral();
            tilt(45.0, 1.f);
            (void)poll(dx, dy);
            tilt(45.0, 0.4f);
            if (!poll(dx, dy) || (dx != 1) || (dy != -1)) {
                fail("斜め移動限定：斜めの間に倒す量が少し減ったら止まった（境目でがたつく）");
            }
            tilt(45.0, 0.3f);
            if (poll(dx, dy)) {
                fail("斜め移動限定：戻したスティックが動き続ける");
            }
            //! 十字キー：1 方向は出さない。2 方向の同時押しは斜め。十字が入っている間はスティックへ落とさない。
            neutral();
            dpad(SDL_CONTROLLER_BUTTON_DPAD_UP, true);
            if (poll(dx, dy)) {
                fail("斜め移動限定：十字の上だけで動いた");
            }
            dpad(SDL_CONTROLLER_BUTTON_DPAD_LEFT, true);
            if (!poll(dx, dy) || (dx != -1) || (dy != -1)) {
                fail("斜め移動限定：十字の上＋左が北西にならない");
            }
            dpad(SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);
            tilt(45.0, 1.f);
            if (poll(dx, dy)) {
                fail("斜め移動限定：十字の上を押している間にスティックの斜めが出た（十字を優先する約束）");
            }
            dpad(SDL_CONTROLLER_BUTTON_DPAD_UP, false);
            neutral();
            //! 離せば元どおり：十字の上を押し続けたまま斜め移動限定を外すと、その場で北へ 1 歩。
            dpad(SDL_CONTROLLER_BUTTON_DPAD_UP, true);
            if (poll(dx, dy)) {
                fail("検査の足場：斜め移動限定のまま十字の上だけで動いた");
            }
            pad.set_diagonal_only(false);
            if (!poll(dx, dy) || (dx != 0) || (dy != -1)) {
                fail("斜め移動限定を外しても、押し続けている十字の上が北へ歩かない（離せば元どおり）");
            }
            dpad(SDL_CONTROLLER_BUTTON_DPAD_UP, false);
        }
        //! (d) ボタンを押している間か（`input_down`）：実機のボタン・トリガ・修飾・VR の Touch。焦点を失うと偽。
        {
            GamePad pad;
            const auto button = [&](Uint8 b, bool down) {
                SDL_Event e{};
                e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
                e.cbutton.button = b;
                (void)pad.on_event(e);
            };
            const auto trigger = [&](Uint8 axis, Sint16 value) {
                SDL_Event e{};
                e.type = SDL_CONTROLLERAXISMOTION;
                e.caxis.axis = axis;
                e.caxis.value = value;
                (void)pad.on_event(e);
            };
            if (pad.input_down(PadInput::X)) {
                fail("input_down：何も押していないのに X が押されている");
            }
            button(SDL_CONTROLLER_BUTTON_X, true);
            if (!pad.input_down(PadInput::X) || pad.input_down(PadInput::Y)) {
                fail("input_down：X を押している間が X だけで真にならない");
            }
            button(SDL_CONTROLLER_BUTTON_X, false);
            if (pad.input_down(PadInput::X)) {
                fail("input_down：X を離したのに偽にならない");
            }
            button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true); //!< 修飾（LB）も押している間は真
            if (!pad.input_down(PadInput::LeftShoulder)) {
                fail("input_down：LB を押している間が真にならない");
            }
            button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false);
            trigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000);
            if (!pad.input_down(PadInput::RightTrigger) || pad.input_down(PadInput::LeftTrigger)) {
                fail("input_down：RT を深く押している間が RT だけで真にならない");
            }
            trigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1000);
            if (pad.input_down(PadInput::RightTrigger)) {
                fail("input_down：RT を戻したのに偽にならない");
            }
            button(SDL_CONTROLLER_BUTTON_Y, true);
            pad.forget_hold();
            if (pad.input_down(PadInput::Y)) {
                fail("input_down：焦点を失った（forget_hold）のに押していることが残る");
            }
            ExternalPadState touch{};
            touch.button[static_cast<int>(PadInput::X)] = true;
            pad.feed_external(touch);
            if (!pad.input_down(PadInput::X)) {
                fail("input_down：VR の Touch の押しが見えない");
            }
            touch.button[static_cast<int>(PadInput::X)] = false;
            pad.feed_external(touch);
            if (pad.input_down(PadInput::X)) {
                fail("input_down：VR の Touch を離したのに偽にならない");
            }
        }
    }

    if (failures == 0) {
        std::fprintf(stderr, "  PASS すべて通りました\n");
    }
    return (failures == 0) ? 0 : 1;
}

/*!
 * @brief `--motion-check`: 動き（P6）の検査。**窓も GL も要らない。**
 *
 * @details 設計書 §8.2 は「落とすと必ず破綻するもの」を 3 つ挙げている。そのうち
 *
 * | § | 内容 | どこで守るか |
 * |---|---|---|
 * | 8.2-1 | 影のパスにも同じ変形 | **構造で守る。**頂点シェーダが 1 本しかない（`voxel_renderer.cpp`） |
 * | 8.2-2 | **当たり判定は動かさない** | **ここ。**時刻を変えても footprint と grounded が変わらないこと |
 * | 8.2-3 | 大きな剛体を頂点シェーダで曲げない | **ここ。**風で曲がるのは `motion.kind == Wind` のパーツだけ |
 *
 * 8.2-1 を検査ではなく構造で守るのは、「2 本のシェーダが一致しているか」を機械で見るのが
 * 難しいからである。**一致させる必要が無い作りにすれば、検査そのものが要らない。**
 */
int run_motion_check(const AppOptions &options)
{
    int failures = 0;
    int checked_prefabs = 0;

    /*
     * 手元にあるプレハブを全部通す。動くものが 1 つも無ければ検査になっていない。
     * P10 から目録は**ライブラリ（対応表）から取る**。町の素材（shop / mill / sign）はまだ表に
     * 載っていないので、従来の 6 個と**和**を取る（ライブラリが読めなくても 6 個では回る）。
     */
    std::set<std::string> name_set = { "shop", "tree", "rock", "mill", "grass", "sign" };
    {
        PrefabLibrary library;
        std::string lib_err;
        if (library.load(options.voxel_dir, lib_err)) {
            for (int i = 0; i < library.count(); ++i) {
                name_set.insert(library.entry(i).name);
            }
        } else {
            std::fprintf(stderr, "  (ライブラリが読めないので従来の 6 個だけ回します: %s)\n", lib_err.c_str());
        }
    }
    int moving_parts = 0;

    for (const std::string &name_string : name_set) {
        const char *const name = name_string.c_str();
        Prefab prefab;
        std::string err;
        if (!load_prefab(options.voxel_dir, name, prefab, err)) {
            // 無い素材は飛ばす（P10 で増える）。**読めたのに壊れている場合は下で落ちる。**
            std::fprintf(stderr, "  (skip) %s: %s\n", name, err.c_str());
            continue;
        }
        ++checked_prefabs;

        /*
         * (1) **当たり判定は動かさない**（§8.2-2）。
         * 動きの行列は見た目だけのもので、`footprint` にも `grounded` にも触ってはならない。
         * 時刻と扉の状態を大きく振って、両方が 1 ビットも変わらないことを見る。
         */
        const auto footprint_before = prefab.footprint;
        std::vector<bool> grounded_before;
        for (const auto &part : prefab.parts) {
            grounded_before.push_back(part.grounded);
        }

        std::vector<Mat4> motions;
        bool moved_here = false;
        for (const float t : { 0.f, 0.37f, 1.0f, 3.3f, 60.0f }) {
            MotionContext ctx;
            ctx.time = t;
            ctx.states.emplace_back("door", (t > 1.f) ? 1.f : 0.f);
            compose_part_motions(prefab, ctx, motions);
            if (motions.size() != prefab.parts.size()) {
                std::fprintf(stderr, "  FAIL %s: 動きの行列の数が合わない (%zu != %zu)\n",
                    name, motions.size(), prefab.parts.size());
                ++failures;
                break;
            }
            for (std::size_t i = 0; i < motions.size(); ++i) {
                // 単位行列でなければ「動いた」。
                for (int k = 0; k < 16; ++k) {
                    const float identity = ((k % 5) == 0) ? 1.f : 0.f;
                    if (std::abs(motions[i].m[k] - identity) > 1e-5f) {
                        moved_here = true;
                        break;
                    }
                }
            }
        }
        if (moved_here) {
            ++moving_parts;
        }

        bool untouched = (prefab.footprint == footprint_before);
        for (std::size_t i = 0; (i < prefab.parts.size()) && (i < grounded_before.size()); ++i) {
            untouched = untouched && (prefab.parts[i].grounded == grounded_before[i]);
        }
        if (!untouched) {
            std::fprintf(stderr, "  FAIL %s: **当たり判定が動いた**（§8.2-2）\n", name);
            ++failures;
        }

        /*
         * (2) 静止パーツは時刻を変えても単位行列のまま（§8.2-3 の裏返し）。
         * ここが落ちるのは「全部のパーツを動かしてしまっている」ときで、
         * 見た目では建物が丸ごと揺れるので気づけるが、検査でも捕まえておく。
         */
        MotionContext late;
        late.time = 12.5f;
        std::vector<Mat4> late_motions;
        compose_part_motions(prefab, late, late_motions);
        for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
            const bool rigid_still = (prefab.parts[i].motion.kind == MotionKind::Static)
                || (prefab.parts[i].motion.kind == MotionKind::Wind);
            if (!rigid_still || (prefab.parts[i].parent >= 0)) {
                continue; // 親が動けば子も動くのが正しい
            }
            for (int k = 0; k < 16; ++k) {
                const float identity = ((k % 5) == 0) ? 1.f : 0.f;
                if (std::abs(late_motions[i].m[k] - identity) > 1e-5f) {
                    std::fprintf(stderr, "  FAIL %s/%s: 静止（または風）のパーツが剛体として動いた\n",
                        name, prefab.parts[i].name.c_str());
                    ++failures;
                    break;
                }
            }
        }

        std::string parts_desc;
        for (const auto &part : prefab.parts) {
            if (!parts_desc.empty()) {
                parts_desc += " ";
            }
            parts_desc += part.name + ":" + motion_kind_name(part.motion.kind);
        }
        /*
         * 「剛体として」動いたかを出す。**風のパーツはここでは「静止」と出るのが正しい**
         * （曲げているのは頂点シェーダで、CPU 側の行列は単位行列のまま）。
         * ただ「静止」とだけ書くと風が効いていないように読めるので、そう書かない。
         */
        const char *const verdict = moved_here ? "剛体が動く" : "剛体は静止";
        std::fprintf(stderr, "  %-6s parts=%zu footprint=%zu  %s  %s\n", name, prefab.parts.size(),
            prefab.footprint.size(), verdict, parts_desc.c_str());
    }

    /*
     * (3) **検査の検査**（設計書 §14-4）。
     * ここまでの (1)(2) は「何も動かない」実装でも全部通ってしまう。動くものが 1 つも
     * 無ければ、この検査は何も見ていない。
     */
    std::fprintf(stderr, "  (3) 検査の検査: 実際に動いたプレハブ %d 個 / 読めた %d 個\n",
        moving_parts, checked_prefabs);
    if (moving_parts == 0) {
        std::fprintf(stderr, "  FAIL **動くプレハブが 1 つも無い。この検査は何も見ていない**\n");
        ++failures;
    }

    /*
     * (4) 動きが**時刻だけで決まる**こと。同じ時刻を 2 回引いて一致しなければ、
     * どこかに状態が residual として残っている（決定性が無いと絵がざわつく。§9.3 と同じ理由）。
     */
    {
        Prefab prefab;
        std::string err;
        if (load_prefab(options.voxel_dir, "mill", prefab, err)) {
            std::vector<Mat4> a;
            std::vector<Mat4> b;
            MotionContext ctx;
            ctx.time = 2.75f;
            compose_part_motions(prefab, ctx, a);
            MotionContext other;
            other.time = 9.0f;
            std::vector<Mat4> scratch;
            compose_part_motions(prefab, other, scratch); // 別の時刻を挟む
            compose_part_motions(prefab, ctx, b);
            int mismatches = 0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                for (int k = 0; k < 16; ++k) {
                    if (a[i].m[k] != b[i].m[k]) {
                        ++mismatches;
                    }
                }
            }
            std::fprintf(stderr, "  (4) 同じ時刻なら同じ姿勢: mismatches=%d\n", mismatches);
            failures += (mismatches != 0) ? 1 : 0;
        }
    }

    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}


// 実際の SDL キューから pump_sdl_events を通す。コアも設定ファイルも使わない。
int run_sdl_input_check()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        return 1;
    }
    int failures = 0;
    int cases = 0;
    auto check = [&](bool ok, const char *name) {
        ++cases;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "[sdl-input-check] FAIL: %s\n", name);
        }
    };
    int width = 1280, height = 720, turn = 0;
    Hd2dSettings settings;
    settings.vpad.show = false;
    UiLayout layout{};
    Camera camera;
    GamePad pad;
    VirtualPad vpad;
    std::vector<PadCommand> commands;
    std::string keymap{ "original" };
    std::string core_name{ "hengband" };
    FeatureMenu menu;
    FloorCutin cutin;
    HudState hud;
    ClickPath path;
    TextOverlay text;
    FpsMode fps;
    std::vector<SubPanelKindChoice> kinds;
    GameFrame frame{};
    InputState state;
    InputBatch batch;
    int actions = 0;
    InputPumpContext ctx{
        .screen_w = width, .screen_h = height,
        .usable_area = [&]() { return RectPx{0, 0, width, height}; },
        .settings = settings, .layout = layout, .camera = camera, .camera_turn = turn,
        .pad = pad, .vpad = vpad, .pad_commands = commands, .pad_keymap = keymap, .core_name = core_name,
        .feature_menu = menu, .floor_cutin = cutin, .hud_state = hud, .click_path = path,
        .text = text, .first_person = fps, .sub_panel_kind_choices = kinds,
        .frame = frame, .state = state,
        .perform_action = [&](int action) {
            ++actions;
            if (action == kActionFeatureMenu) { menu.open(settings); }
        },
        .fps_drives_movement = [] { return false; },
        .turn_drives_movement = [] { return false; },
        .turn_screen_move = [](presentation::InputEventWire &) {},
        .batch = batch,
    };
    auto key = [](SDL_Keycode sym, Uint16 mods = 0) {
        SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = sym; e.key.keysym.mod = mods;
        return e;
    };
    auto chars = [](const char *value, Uint32 type = SDL_TEXTINPUT) {
        SDL_Event e{}; e.type = type;
        // text/edit は別の union メンバーとして書き込む。
        char *dst = type == SDL_TEXTEDITING ? e.edit.text : e.text.text;
        std::strncpy(dst, value, SDL_TEXTINPUTEVENT_TEXT_SIZE - 1);
        return e;
    };
    auto pump = [&](std::initializer_list<SDL_Event> events) {
        batch = InputBatch{};
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        for (auto e : events) {
            if (SDL_PushEvent(&e) != 1) { ++failures; }
        }
        pump_sdl_events(ctx);
    };
    pump({key(SDLK_a), chars("a")});
    check(batch.input.events.size() == 1 && batch.input.events[0].chr == "a", "ASCII is sent once");
    pump({key(SDLK_c, KMOD_CTRL)});
    check(batch.input.events.size() == 1 && batch.input.events[0].ctrl, "Ctrl key");
    pump({key(SDLK_KP_8)});
    check(batch.input.events.size() == 1 && batch.input.events[0].e == "move"
        && batch.input.events[0].dy == -1, "NumLock off");
    pump({key(SDLK_KP_8, KMOD_NUM), chars("8")});
    check(batch.input.events.size() == 1 && batch.input.events[0].chr == "8", "NumLock on");
    (void)settings.key_binds.assign(1001, SDLK_z, kModShift);
    actions = 0;
    pump({key(SDLK_z, KMOD_SHIFT), chars("Z"), chars("x")});
    check(actions == 1 && batch.input.events.size() == 1 && batch.input.events[0].chr == "x",
        "Shift binding consumes only its text");
    (void)settings.key_binds.assign(1002, SDLK_z, kModCtrl);
    pump({key(SDLK_z, KMOD_CTRL), chars("x")});
    check(batch.input.events.size() == 1 && batch.input.events[0].chr == "x",
        "Ctrl binding does not consume following text");
    (void)settings.key_binds.assign(kActionFeatureMenu, SDLK_F10, 0);
    pump({key(SDLK_F10), chars("a"), key(SDLK_DOWN)});
    check(menu.is_open() && batch.input.events.empty(), "Opened menu blocks remaining input");
    menu.close();
    /*
     * 修-02: Shift＋矢印は「走る命令のキー列」＋方向になる（プロトコルは変えない）。
     * 走るキーは、コアが届けるキー列（キー配置ごとの逆引きの結果）をそのまま使う。
     * 表の「キー列」は各コアの実際の逆引きの結果。
     * 方向は、普通のキーで読むコア（Umoria・Rogue 系）には続けて `move`、ターゲット画面で受けるコア
     * （変愚・短愚・幻想蛮怒・Frox・Sil-Q・Angband）には、画面が開いてから `aim_move`・`aim_confirm`。
     */
    {
        struct RunCase {
            const char *core;
            const char *keymap;
            int run_char;              //!< `PadCommand::command`
            std::vector<int> run_keys; //!< いまの配置での解決結果
            bool expect_ctrl;          //!< キー列が Ctrl＋英字で送られるか
            bool via_targeting_ui;     //!< 方向をターゲット画面で答えるコアか
        };
        const std::vector<RunCase> run_cases = {
            { "hengband", "original", '.', { '.' }, false, true },
            { "hengband", "rogue", '.', { ',' }, false, true },
            { "tangband", "original", '.', { '.' }, false, true },
            { "tangband", "rogue", '.', { ',' }, false, true },
            { "gensoband", "original", '.', { '.' }, false, true },
            { "gensoband", "rogue", '.', { ',' }, false, true },
            { "toband", "original", '.', { '.' }, false, true },
            { "toband", "rogue", '.', { ',' }, false, true },
            { "frox", "original", '.', { '.' }, false, true },
            { "frox", "rogue", '.', { ',' }, false, true },
            { "silq", "original", '.', { '.' }, false, true },
            { "silq", "rogue", '.', { '.' }, false, true },
            { "angband", "original", '.', { '.' }, false, true },
            { "angband", "rogue", '.', { ',' }, false, true },
            { "Umoria", "original", '.', { '.' }, false, false },
            { "Umoria", "rogue", '.', { '^', '.' }, false, false },
            { "Rogue 3.6", "rogue", 'f', { 'f' }, false, false },
            { "Rogue 5.4", "rogue", 'g', { 'g' }, false, false },
            { "RogueClone2", "rogue", 0x06, { 0x06 }, true, false },
        };
        const auto set_run_commands = [&](const RunCase &c) {
            commands.clear();
            PadCommand other; // 走る以外の命令が混じっていても、走る命令の字だけを引く
            other.id = 1; other.command = 'g'; other.label_utf8 = "pickup"; other.keys = { 'g' };
            commands.push_back(other);
            PadCommand run;
            run.id = 2; run.command = c.run_char; run.label_utf8 = "run"; run.keys = c.run_keys;
            commands.push_back(run);
            core_name = c.core;
            keymap = c.keymap;
            frame.awaiting_command = true;
            state.pending_run = InputState::PendingRun{};
        };
        const auto sync_aim = [&](bool active, int request_id, bool can_confirm) {
            TargetingState st;
            st.active = active;
            st.mode = "direction8";
            st.scope = 1;
            st.request_id = request_id;
            st.can_confirm = can_confirm;
            hud.targeting.sync(st);
        };
        for (const RunCase &c : run_cases) {
            set_run_commands(c);
            pump({key(SDLK_RIGHT, KMOD_SHIFT)});
            const auto &ev = batch.input.events;
            const std::size_t want = c.run_keys.size() + (c.via_targeting_ui ? 0 : 1);
            bool ok = ev.size() == want;
            for (std::size_t i = 0; ok && i < c.run_keys.size(); ++i) {
                const int k = c.run_keys[i];
                if (c.expect_ctrl) {
                    ok = ev[i].e == "key" && ev[i].ctrl && ev[i].chr == std::string(1, static_cast<char>('a' + k - 1));
                } else {
                    ok = ev[i].e == "key" && !ev[i].ctrl && ev[i].chr == std::string(1, static_cast<char>(k));
                }
            }
            if (c.via_targeting_ui) {
                ok = ok && state.pending_run.active && state.pending_run.dx == 1 && state.pending_run.dy == 0;
            } else {
                ok = ok && !state.pending_run.active && ev.back().e == "move" && ev.back().dx == 1 && ev.back().dy == 0;
            }
            const std::string label = std::string("Shift+Right runs: ") + c.core + " / " + c.keymap;
            check(ok, label.c_str());
        }
        state.pending_run = InputState::PendingRun{};
        // ターゲット画面で方向を受けるコア：画面が開いたら aim_move（東＝6）、受理されたら aim_confirm
        set_run_commands(run_cases[0]);
        pump({key(SDLK_RIGHT, KMOD_SHIFT)});
        sync_aim(true, 0, false);
        pump({});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "ui_action"
            && batch.input.events[0].action == "aim_move" && batch.input.events[0].number == 6,
            "Run waits for the aim screen, then sends the direction");
        const int moved_request = batch.input.events.empty() ? 0 : batch.input.events[0].request_id;
        pump({});
        check(batch.input.events.empty() && state.pending_run.active, "Run waits for the core to accept the direction");
        sync_aim(true, moved_request, true);
        pump({});
        check(batch.input.events.size() == 1 && batch.input.events[0].action == "aim_confirm" && !state.pending_run.active,
            "Run confirms once the direction is accepted");
        sync_aim(false, 0, false);
        // 押しっぱなしの連打（repeat）は捨てる。離して押し直せば、また走る
        set_run_commands(run_cases[0]);
        pump({key(SDLK_RIGHT, KMOD_SHIFT)});
        state.pending_run.active = false;
        SDL_Event held = key(SDLK_RIGHT, KMOD_SHIFT);
        held.key.repeat = 1;
        pump({held});
        check(batch.input.events.empty(), "Held Shift+arrow does not start a second run");
        pump({key(SDLK_RIGHT, KMOD_SHIFT)});
        check(batch.input.events.size() == 1 && batch.input.events[0].chr == ".", "Pressing again starts a new run");
        // 画面が開かないコアでは、一定時間のあと普通の move を送る
        state.pending_run.since_ms = SDL_GetTicks() - 5000;
        pump({});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "move" && batch.input.events[0].dx == 1
            && !state.pending_run.active, "Run falls back to a plain move when no aim screen opens");
        // 4 方向と斜め（テンキー。NumLock 切）も同じ形（普通のキーで読むコアで見る）
        //! 表の並びは番号で引かない（2026-10-18、TOband の 2 行を途中に足して 14 番が Umoria にずれ、
        //! 「Rogue 3.6 の f で走る」を確かめるはずが別のコアの行で落ちた）。コアの名前で引く。
        const auto run_case_of = [&](const char *core) -> const RunCase & {
            for (const RunCase &c : run_cases) {
                if (std::string(c.core) == core) {
                    return c;
                }
            }
            return run_cases[0];
        };
        set_run_commands(run_case_of("Rogue 3.6"));
        pump({key(SDLK_UP, KMOD_SHIFT)});
        check(batch.input.events.size() == 2 && batch.input.events[0].chr == "f" && batch.input.events[1].dy == -1
            && batch.input.events[1].dx == 0, "Shift+Up runs north");
        pump({key(SDLK_KP_7, KMOD_SHIFT)});
        check(batch.input.events.size() == 2 && batch.input.events[0].chr == "f" && batch.input.events[1].dx == -1
            && batch.input.events[1].dy == -1, "Shift+KP7 runs north-west");
        // 走らない場合は今までどおり move 1 つ
        pump({key(SDLK_RIGHT)});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "move", "Plain arrow still walks one step");
        pump({key(SDLK_RIGHT, KMOD_SHIFT | KMOD_CTRL)});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "move", "Ctrl+Shift+arrow is not a run");
        frame.awaiting_command = false;
        pump({key(SDLK_RIGHT, KMOD_SHIFT)});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "move",
            "Shift+arrow while the core asks something else stays a plain move");
        frame.awaiting_command = true;
        core_name = "PlainCore"; // 走る命令が原作に無い（知らない）コアは対象外
        pump({key(SDLK_RIGHT, KMOD_SHIFT)});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "move", "Core without a run command stays a plain move");
        core_name = "Rogue 3.6";
        commands.back().keys.clear(); // いまの配置で解決できない
        pump({key(SDLK_RIGHT, KMOD_SHIFT)});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "move", "Unresolved run key stays a plain move");
        commands.clear();
        pump({key(SDLK_RIGHT, KMOD_SHIFT)});
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "move", "No run command in the table stays a plain move");
        frame.awaiting_command = false;
        core_name = "hengband";
        keymap = "original";
        state.pending_run = InputState::PendingRun{};
    }

    /*
     * 斜め移動限定（`kActionDiagonalOnly`。決めたこと 2026-10-15）。割り当てたキー（既定は未割り当て。ここでは Insert に割り当てて確かめる）を押している間、
     * 地図の上の移動を斜めに限る。縦横の 1 方向だけは捨て、矢印の同時押し・テンキーの 7 9 1 3・ローグライク配置の
     * y u b n は通す。メニュー・窓・狙い・方向を聞く問い・文字入力の間は何も変えない。視点を回したときは
     * 回した後の向きで見る。スティック・十字は `--pad-check` の 7 番。
     */
    {
        const auto keyup = [](SDL_Keycode sym, Uint16 mods = 0) {
            SDL_Event e{};
            e.type = SDL_KEYUP;
            e.key.keysym.sym = sym;
            e.key.keysym.mod = mods;
            return e;
        };
        const auto only_move = [&](int dx, int dy) {
            return batch.input.events.size() == 1 && batch.input.events[0].e == "move" && batch.input.events[0].dx == dx
                && batch.input.events[0].dy == dy;
        };
        const auto only_key = [&](const char *chr) {
            return batch.input.events.size() == 1 && batch.input.events[0].e == "key" && batch.input.events[0].chr == chr;
        };
        const auto none = [&]() { return batch.input.events.empty(); };
        const auto signature = [&]() {
            std::string s;
            for (const auto &e : batch.input.events) {
                s += e.e + ":" + std::to_string(e.dx) + "," + std::to_string(e.dy) + ":" + e.chr + ":" + e.action + ";";
            }
            return s;
        };
        (void)settings.key_binds.assign(kActionDiagonalOnly, SDLK_INSERT, 0);
        commands.clear();
        core_name = "hengband";
        keymap = "original";
        frame.awaiting_command = true;
        state = InputState{};
        // 対照：押していないあいだは、今までどおり縦横も歩く
        pump({key(SDLK_UP)});
        check(only_move(0, -1), "Diagonal-only off: Up walks north as before");
        pump({keyup(SDLK_UP), chars("8")});
        check(only_key("8"), "Diagonal-only off: the digit 8 is sent as before");
        // 割り当てたキーを押す：何も送らず、状態だけ変わる
        actions = 0;
        pump({key(SDLK_INSERT)});
        check(none() && state.diagonal_key_held && actions == 0, "Insert starts diagonal-only without sending anything");
        // 縦横の 1 方向は捨てる
        pump({key(SDLK_UP), keyup(SDLK_UP)});
        check(none(), "Diagonal-only: Up alone does nothing");
        pump({key(SDLK_DOWN), keyup(SDLK_DOWN), key(SDLK_LEFT), keyup(SDLK_LEFT), key(SDLK_RIGHT), keyup(SDLK_RIGHT)});
        check(none(), "Diagonal-only: Down / Left / Right alone do nothing");
        // 矢印の同時押しは斜め
        pump({key(SDLK_UP), key(SDLK_LEFT)});
        check(only_move(-1, -1), "Diagonal-only: Up + Left walks north-west");
        SDL_Event repeat_left = key(SDLK_LEFT);
        repeat_left.key.repeat = 1;
        pump({repeat_left});
        check(only_move(-1, -1), "Diagonal-only: the repeat of the held pair keeps walking north-west");
        pump({keyup(SDLK_UP), keyup(SDLK_LEFT)});
        pump({key(SDLK_RIGHT), key(SDLK_UP)});
        check(only_move(1, -1), "Diagonal-only: Right + Up walks north-east");
        pump({keyup(SDLK_UP), keyup(SDLK_RIGHT)});
        pump({key(SDLK_DOWN), key(SDLK_LEFT)});
        check(only_move(-1, 1), "Diagonal-only: Down + Left walks south-west");
        pump({keyup(SDLK_DOWN), keyup(SDLK_LEFT)});
        pump({key(SDLK_DOWN), key(SDLK_RIGHT)});
        check(only_move(1, 1), "Diagonal-only: Down + Right walks south-east");
        pump({keyup(SDLK_DOWN), keyup(SDLK_RIGHT)});
        pump({key(SDLK_UP), key(SDLK_DOWN)});
        check(none(), "Diagonal-only: Up + Down cancel out and do nothing");
        pump({keyup(SDLK_UP), keyup(SDLK_DOWN)});
        // 片方を離したら、残った 1 方向は捨てる
        pump({key(SDLK_UP), key(SDLK_LEFT)});
        pump({keyup(SDLK_LEFT), key(SDLK_RIGHT)});
        check(only_move(1, -1), "Diagonal-only: after Left is released, Up + Right is north-east");
        pump({keyup(SDLK_UP), keyup(SDLK_RIGHT)});
        // テンキー（NumLock 切）：7 9 1 3 は通り、8 2 4 6 は捨てる。8 2 4 6 の同時押しは斜め。5 は動きではない
        pump({key(SDLK_KP_7)});
        check(only_move(-1, -1), "Diagonal-only: keypad 7 walks north-west");
        pump({key(SDLK_KP_9)});
        check(only_move(1, -1), "Diagonal-only: keypad 9 walks north-east");
        pump({key(SDLK_KP_1)});
        check(only_move(-1, 1), "Diagonal-only: keypad 1 walks south-west");
        pump({key(SDLK_KP_3)});
        check(only_move(1, 1), "Diagonal-only: keypad 3 walks south-east");
        pump({key(SDLK_KP_8), keyup(SDLK_KP_8), key(SDLK_KP_2), keyup(SDLK_KP_2), key(SDLK_KP_4), keyup(SDLK_KP_4),
            key(SDLK_KP_6), keyup(SDLK_KP_6)});
        check(none(), "Diagonal-only: keypad 8 / 2 / 4 / 6 alone do nothing");
        pump({key(SDLK_KP_8), key(SDLK_KP_6)});
        check(only_move(1, -1), "Diagonal-only: keypad 8 + 6 walks north-east");
        pump({keyup(SDLK_KP_8), keyup(SDLK_KP_6)});
        pump({key(SDLK_KP_5)});
        check(only_move(0, 0), "Diagonal-only: keypad 5 (stay) is not a step and still passes");
        // NumLock が入っているテンキーと上段の数字は字で届く
        pump({key(SDLK_KP_8, KMOD_NUM), chars("8"), keyup(SDLK_KP_8, KMOD_NUM)});
        check(none(), "Diagonal-only: NumLock keypad 8 alone does nothing (no stray digit)");
        pump({key(SDLK_KP_7, KMOD_NUM), chars("7"), keyup(SDLK_KP_7, KMOD_NUM)});
        check(only_key("7"), "Diagonal-only: NumLock keypad 7 still sends 7");
        pump({key(SDLK_KP_8, KMOD_NUM), chars("8"), key(SDLK_KP_4, KMOD_NUM), chars("4")});
        check(only_move(-1, -1), "Diagonal-only: NumLock keypad 8 + 4 walks north-west (one move, no stray digits)");
        pump({keyup(SDLK_KP_8, KMOD_NUM), keyup(SDLK_KP_4, KMOD_NUM)});
        for (const char *const orthogonal : { "2", "4", "6", "8" }) {
            pump({chars(orthogonal)});
            check(none(), "Diagonal-only: the digits 2 / 4 / 6 / 8 are dropped");
        }
        for (const char *const diagonal : { "1", "3", "7", "9" }) {
            pump({chars(diagonal)});
            check(only_key(diagonal), "Diagonal-only: the digits 1 / 3 / 7 / 9 pass");
        }
        pump({chars("h")});
        check(only_key("h"), "Diagonal-only: h is not a move in the original key set");
        // ローグライクの配置：hjkl（大文字は走る）は捨て、yubn は通す。数字は回数の指定なので触らない
        keymap = "rogue";
        for (const char *const orthogonal : { "h", "j", "k", "l", "H", "J", "K", "L" }) {
            pump({chars(orthogonal)});
            check(none(), "Diagonal-only (roguelike keys): h j k l (and run H J K L) are dropped");
        }
        for (const char *const diagonal : { "y", "u", "b", "n", "Y", "U", "B", "N" }) {
            pump({chars(diagonal)});
            check(only_key(diagonal), "Diagonal-only (roguelike keys): y u b n (and run Y U B N) pass");
        }
        pump({chars("8")});
        check(only_key("8"), "Diagonal-only (roguelike keys): a digit is a repeat count and passes");
        keymap = "original";
        // Shift＋矢印の走りも斜めに限る
        {
            commands.clear();
            PadCommand run;
            run.id = 2;
            run.command = '.';
            run.label_utf8 = "run";
            run.keys = { '.' };
            commands.push_back(run);
            state.pending_run = InputState::PendingRun{};
            pump({key(SDLK_UP, KMOD_SHIFT), keyup(SDLK_UP, KMOD_SHIFT)});
            check(none() && !state.pending_run.active, "Diagonal-only: Shift + Up alone does not run");
            pump({key(SDLK_UP, KMOD_SHIFT), key(SDLK_LEFT, KMOD_SHIFT)});
            check(batch.input.events.size() == 1 && batch.input.events[0].chr == "." && state.pending_run.active
                    && state.pending_run.dx == -1 && state.pending_run.dy == -1,
                "Diagonal-only: Shift + Up + Left runs north-west");
            pump({keyup(SDLK_UP, KMOD_SHIFT), keyup(SDLK_LEFT, KMOD_SHIFT)});
            state.pending_run = InputState::PendingRun{};
            commands.clear();
        }
        // 効かない場面：メニュー・窓・狙い・方向を聞く問い・文字入力・コマンド待ちでない間は、押していても何も変えない
        {
            TargetingState aim;
            const auto same_with_and_without = [&](const char *name) {
                state.arrows_down = 0;
                state.diagonal_key_held = true;
                hud.targeting.sync(TargetingState{});
                hud.targeting.sync(aim); //!< 狙いは 1 回答えると返事待ちになるので、閉じて開き直して新しい問いにする
                pump({key(SDLK_UP), chars("8")});
                const std::string with = signature();
                state.arrows_down = 0;
                state.diagonal_key_held = false;
                hud.targeting.sync(TargetingState{});
                hud.targeting.sync(aim);
                pump({key(SDLK_UP), chars("8")});
                const std::string without = signature();
                pump({keyup(SDLK_UP)});
                state.arrows_down = 0;
                state.diagonal_key_held = true;
                check(with == without, name);
                return without;
            };
            frame.awaiting_command = false;
            check(!same_with_and_without("Diagonal-only does nothing while the core is not waiting for a command").empty(),
                "Diagonal-only: (check scaffold) the plain input produces events");
            frame.awaiting_command = true;
            frame.text_input_active = true;
            (void)same_with_and_without("Diagonal-only does nothing during text input");
            frame.text_input_active = false;
            frame.menu_open = true;
            (void)same_with_and_without("Diagonal-only does nothing while a core menu is open");
            frame.menu_open = false;
            frame.numeric.active = true;
            (void)same_with_and_without("Diagonal-only does nothing during a number input");
            frame.numeric.active = false;
            PromptChoice yes;
            yes.label_utf8 = "yes";
            yes.key = 'y';
            frame.prompt.choices.push_back(yes);
            (void)same_with_and_without("Diagonal-only does nothing while a question with choices is shown");
            frame.prompt.choices.clear();
            aim.active = true;
            aim.mode = "direction8";
            aim.scope = 1;
            aim.request_id = 7;
            (void)same_with_and_without("Diagonal-only does nothing while aiming");
            aim.active = false;
            hud.targeting.sync(aim);
            menu.open(settings);
            (void)same_with_and_without("Diagonal-only does nothing while the feature menu is open");
            menu.close();
            check(diagonal_only_applies(ctx), "Diagonal-only applies again after every screen is closed");
        }
        // 視点を回したとき（45 度刻み）：回した後の向きで斜めかどうかを見る
        {
            turn = 1;
            ctx.turn_drives_movement = [] { return true; };
            ctx.turn_screen_move = [&](presentation::InputEventWire &wire) {
                if (wire.e == "move") {
                    rotate_screen_delta(turn, wire.dx, wire.dy);
                }
            };
            int up_x = 0;
            int up_y = -1;
            rotate_screen_delta(turn, up_x, up_y);
            int ur_x = 1;
            int ur_y = -1;
            rotate_screen_delta(turn, ur_x, ur_y);
            check(!is_orthogonal_step(up_x, up_y) && is_orthogonal_step(ur_x, ur_y),
                "Diagonal-only: (check scaffold) at 45 degrees Up becomes diagonal and Up + Right becomes straight");
            state.arrows_down = 0;
            pump({key(SDLK_UP)});
            check(only_move(up_x, up_y), "Diagonal-only: Up that turns diagonal on the world after the view is turned still walks");
            pump({keyup(SDLK_UP)});
            pump({key(SDLK_UP)});
            pump({key(SDLK_RIGHT)});
            check(none(), "Diagonal-only: Up + Right that turns straight on the world after the view is turned does nothing");
            pump({keyup(SDLK_UP), keyup(SDLK_RIGHT)});
            // 字（標準配置の数字）も回したあとの字で見る：8 は斜めの 7 か 9 になって通り、9 は縦横になって捨てる
            pump({chars("8")});
            check(batch.input.events.size() == 1 && ((batch.input.events[0].chr == "7") || (batch.input.events[0].chr == "9")),
                "Diagonal-only: the digit 8 that turns diagonal after the view is turned is sent");
            pump({chars("9")});
            check(none(), "Diagonal-only: the digit 9 that turns straight after the view is turned is dropped");
            turn = 0;
            ctx.turn_drives_movement = [] { return false; };
            ctx.turn_screen_move = [](presentation::InputEventWire &) {};
        }
        // 一人称（WASD）：前・横の 1 方向だけは捨てる。W＋D の同時押しは右前。向きが斜めなら W だけでも進む
        {
            fps.set_active(true);
            fps.facing = 0.f;
            ctx.fps_drives_movement = [] { return true; };
            state.wasd_down = 0;
            pump({key(SDLK_w), keyup(SDLK_w), chars("w")});
            check(none(), "Diagonal-only (first person): W alone facing north does nothing");
            pump({key(SDLK_w), key(SDLK_d)});
            check(only_move(1, -1), "Diagonal-only (first person): W + D walks forward-right (north-east)");
            pump({keyup(SDLK_w), keyup(SDLK_d)});
            fps.facing = 0.78539816f;
            pump({key(SDLK_w), keyup(SDLK_w)});
            check(only_move(1, -1), "Diagonal-only (first person): W facing north-east walks diagonally");
            fps.facing = 0.f;
            state.diagonal_key_held = false;
            pump({key(SDLK_w), keyup(SDLK_w)});
            check(only_move(0, -1), "Diagonal-only off (first person): W walks forward as before");
            state.diagonal_key_held = true;
            fps.set_active(false);
            ctx.fps_drives_movement = [] { return false; };
        }
        // 離せば元どおり
        pump({keyup(SDLK_INSERT)});
        check(!state.diagonal_key_held, "Releasing Insert ends diagonal-only");
        pump({key(SDLK_UP)});
        check(only_move(0, -1), "After release: Up walks north again");
        pump({keyup(SDLK_UP)});
        // 焦点を失ったら、押したままの状態を忘れる
        pump({key(SDLK_INSERT), key(SDLK_UP)});
        {
            SDL_Event lost{};
            lost.type = SDL_WINDOWEVENT;
            lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
            pump({lost});
        }
        check(!state.diagonal_key_held && state.arrows_down == 0, "Losing the window focus forgets the held keys");
        // 修飾キーを単独で割り当てた場合（右 Alt。押した出来事には本人の修飾が付き、離した出来事には付かない）
        (void)settings.key_binds.assign(kActionDiagonalOnly, SDLK_RCTRL, kModCtrl);
        pump({key(SDLK_RCTRL, KMOD_RCTRL)});
        check(state.diagonal_key_held, "A modifier key assigned to diagonal-only starts it when pressed");
        pump({keyup(SDLK_RCTRL, 0)});
        check(!state.diagonal_key_held, "A modifier key assigned to diagonal-only ends it when released");
        (void)settings.key_binds.assign(kActionDiagonalOnly, SDLK_RALT, kModAlt);
        check(settings.key_binds.find(kActionDiagonalOnly).mods == 0, "A lone modifier key is recorded without a modifier");
        {
            //! 押している間、ほかのキーには Alt の修飾が付いて届く（SDL は押した修飾を以後の出来事へ付ける）
            pump({key(SDLK_RALT, KMOD_RALT)});
            check(none() && state.diagonal_key_held && actions == 0, "Right Alt starts diagonal-only without sending anything");
            pump({key(SDLK_UP, KMOD_RALT), keyup(SDLK_UP, KMOD_RALT)});
            check(none(), "Right Alt held: Up alone does nothing");
            pump({key(SDLK_UP, KMOD_RALT), key(SDLK_RIGHT, KMOD_RALT)});
            check(only_move(1, -1), "Right Alt held: Up + Right walks north-east (an Alt modifier does not drop the move)");
            pump({keyup(SDLK_UP, KMOD_RALT), keyup(SDLK_RIGHT, KMOD_RALT)});
            pump({key(SDLK_KP_9, KMOD_RALT)});
            check(only_move(1, -1), "Right Alt held: keypad 9 walks north-east");
            pump({keyup(SDLK_KP_9, KMOD_RALT)});
            pump({key(SDLK_KP_8, KMOD_RALT)});
            check(none(), "Right Alt held: keypad 8 alone does nothing");
            pump({keyup(SDLK_KP_8, KMOD_RALT)});
            // 離す：離した出来事に Alt は付かない。その後は普通に歩ける
            pump({keyup(SDLK_RALT, 0)});
            check(!state.diagonal_key_held, "Releasing Right Alt ends diagonal-only");
            pump({key(SDLK_UP)});
            check(only_move(0, -1), "After Right Alt is released: Up walks north again");
            pump({keyup(SDLK_UP)});
            // 先に Shift を押していても、AltGr（左 Ctrl つき）でも押したことになる
            pump({key(SDLK_RALT, KMOD_LSHIFT | KMOD_RALT)});
            check(state.diagonal_key_held, "Right Alt pressed while Shift is held starts diagonal-only");
            pump({keyup(SDLK_RALT, KMOD_LSHIFT)});
            pump({key(SDLK_RALT, KMOD_LCTRL | KMOD_RALT)});
            check(state.diagonal_key_held, "Right Alt that arrives with a left Ctrl (AltGr) starts diagonal-only");
            pump({keyup(SDLK_RALT, 0)});
            // 他の修飾つきの判定から、割り当てた Alt だけを取り除く：一人称の W＋D
            fps.set_active(true);
            fps.facing = 0.f;
            ctx.fps_drives_movement = [] { return true; };
            state.wasd_down = 0;
            pump({key(SDLK_RALT, KMOD_RALT)});
            pump({key(SDLK_w, KMOD_RALT), key(SDLK_d, KMOD_RALT)});
            check(only_move(1, -1), "Right Alt held (first person): W + D walks forward-right");
            pump({keyup(SDLK_w, KMOD_RALT), keyup(SDLK_d, KMOD_RALT), keyup(SDLK_RALT, 0)});
            fps.set_active(false);
            ctx.fps_drives_movement = [] { return false; };
            // Shift＋矢印の走り：割り当てた Alt が走りを止めない（Shift＋上＋左で北西へ走る）
            commands.clear();
            PadCommand run;
            run.id = 2;
            run.command = '.';
            run.label_utf8 = "run";
            run.keys = { '.' };
            commands.push_back(run);
            state.pending_run = InputState::PendingRun{};
            pump({key(SDLK_RALT, KMOD_RALT)});
            pump({key(SDLK_UP, KMOD_SHIFT | KMOD_RALT), key(SDLK_LEFT, KMOD_SHIFT | KMOD_RALT)});
            check(batch.input.events.size() == 1 && batch.input.events[0].chr == "." && state.pending_run.active
                    && state.pending_run.dx == -1 && state.pending_run.dy == -1,
                "Right Alt held: Shift + Up + Left still runs north-west");
            pump({keyup(SDLK_UP, KMOD_SHIFT | KMOD_RALT), keyup(SDLK_LEFT, KMOD_SHIFT | KMOD_RALT), keyup(SDLK_RALT, KMOD_SHIFT)});
            state.pending_run = InputState::PendingRun{};
            commands.clear();
            // 他の操作に割り当てた修飾キーも「そのキーだけ」で引ける（修飾キーを押した出来事には本人の修飾が付く）
            (void)settings.key_binds.assign(1003, SDLK_LALT, kModAlt);
            actions = 0;
            pump({key(SDLK_LALT, KMOD_LALT)});
            check(actions == 1, "A lone modifier key assigned to another action still fires it");
            pump({keyup(SDLK_LALT, 0)});
            settings.key_binds.clear(1003);
        }
        // 割り当てを外したら、押しても何も起きない
        settings.key_binds.clear(kActionDiagonalOnly);
        pump({key(SDLK_INSERT), key(SDLK_UP)});
        check(!state.diagonal_key_held && only_move(0, -1), "Without an assignment Insert does nothing special");
        pump({keyup(SDLK_INSERT), keyup(SDLK_UP)});
        pump({key(SDLK_RALT, KMOD_RALT), key(SDLK_UP, KMOD_RALT)});
        check(!state.diagonal_key_held && only_move(0, -1), "Without an assignment Right Alt does nothing special");
        pump({keyup(SDLK_RALT, 0), keyup(SDLK_UP)});
        frame.awaiting_command = false;
        state = InputState{};
    }
    frame.text_input_active = true;
    pump({chars("test", SDL_TEXTEDITING)});
    check(state.ime_edit_text == "test" && batch.input.events.empty(), "IME composition is local");
    pump({chars("日本語")});
    check(state.ime_edit_text.empty() && batch.input.events.size() == 1
        && batch.input.events[0].e == "text" && batch.input.events[0].text == "日本語", "IME commit");
    pump({key(SDLK_BACKSPACE)});
    check(batch.input.events.size() == 1 && batch.input.events[0].e == "key"
        && batch.input.events[0].name == "backspace", "Text input Backspace");
    pump({key(SDLK_DELETE)});
    check(batch.input.events.size() == 1 && batch.input.events[0].e == "key"
        && batch.input.events[0].name == "delete", "Text input Delete");
    pump({key(SDLK_RETURN)});
    check(batch.input.events.size() == 1 && batch.input.events[0].e == "confirm", "Text input Enter");
    pump({key(SDLK_ESCAPE)});
    check(batch.input.events.size() == 1 && batch.input.events[0].e == "cancel", "Text input Escape");
    frame.text_input_active = false;
    pump({chars("日本語")});
    check(batch.input.events.empty(), "Non-ASCII outside text prompt");
    SDL_Event click{}; click.type = SDL_MOUSEBUTTONDOWN; click.button.button = SDL_BUTTON_LEFT;
    click.button.x = 100; click.button.y = 100;
    pump({click});
    check(batch.input.events.empty() && !path.active(), "Click outside menu choices does not walk");
    SDL_Event quit{}; quit.type = SDL_QUIT;
    pump({quit});
    check(batch.want_quit, "Quit request");

    auto scripted = [&](const std::string &line) {
        batch = InputBatch{};
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        std::vector<SDL_Event> events;
        std::string error;
        const bool ok = compile_test_key(line, events, error);
        if (!ok) { std::fprintf(stderr, "[sdl-input-check] %s\n", error.c_str()); ++failures; }
        for (auto event : events) { if (SDL_PushEvent(&event) != 1) { ++failures; } }
        pump_sdl_events(ctx);
    };
    scripted(R"({"key":"Enter"})");
    check(batch.input.events.size() == 1 && batch.input.events[0].e == "confirm", "Scripted Enter");
    scripted(R"({"key":"Escape"})");
    check(batch.input.events.size() == 1 && batch.input.events[0].e == "cancel", "Scripted Escape");
    scripted(R"({"key":"F10"})");
    check(menu.is_open() && batch.input.events.empty(), "Scripted F10 opens feature menu");
    scripted(R"({"key":"Escape"})");
    check(!menu.is_open() && batch.input.events.empty(), "Scripted Escape closes feature menu");
    scripted(R"({"key":"x","ctrl":true})");
    check(batch.input.events.size() == 1 && batch.input.events[0].chr == "x" && batch.input.events[0].ctrl,
        "Scripted Ctrl+X");
    scripted(R"({"key":"KP_8","numlock":true})");
    check(batch.input.events.size() == 1 && batch.input.events[0].chr == "8", "Scripted NumLock on");
    scripted(R"({"key":"KP_8"})");
    check(batch.input.events.size() == 1 && batch.input.events[0].dy == -1, "Scripted NumLock off");
    // Native macro/custom-key capture must preserve physical PC/AT key identity.
    // In ordinary play Up and KP_8 both mean north; while recording a trigger
    // they are intentionally different byte sequences (x48 vs xK48).
    frame.modern = {};
    frame.modern.version = 2;
    frame.modern.kind = "dialog";
    frame.modern.stage = "choices";
    frame.modern.capture_keys = true;
    frame.modern.input_ready = true;
    hud.modern.sync(frame, true);
    auto captured = [&](SDL_Keycode sym, Uint16 mods, std::initializer_list<int> expected, const char *label) {
        pump({key(sym, mods)});
        bool ok = batch.input.events.size() == 1 && batch.input.events[0].e == "keyseq";
        if (ok) {
            const auto &bytes = batch.input.events[0].bytes;
            ok = bytes.size() == expected.size() && std::equal(bytes.begin(), bytes.end(), expected.begin());
        }
        check(ok, label);
    };
    captured(SDLK_UP, 0, {31, 'x', '4', '8', 13}, "Captured Up keeps scan identity");
    captured(SDLK_KP_8, 0, {31, 'x', 'K', '4', '8', 13}, "Captured KP_8 keeps keypad identity");
    captured(SDLK_HOME, KMOD_CTRL | KMOD_SHIFT, {31, 'C', 'S', 'x', '4', '7', 13}, "Captured modified Home");
    captured(SDLK_PAGEUP, KMOD_ALT, {31, 'A', 'x', '4', '9', 13}, "Captured Alt+PageUp");
    captured(SDLK_DELETE, 0, {31, 'x', '5', '3', 13}, "Captured Delete");
    frame.modern = {};
    hud.modern.sync(frame, true);
    scripted(R"({"key":"z","shift":true})");
    check(batch.input.events.empty(), "Scripted shifted binding consumes generated text");
    frame.text_input_active = true;
    scripted(R"({"text":"日本語の長い確定文字列を途中で壊さずに送信するテスト"})");
    std::string combined;
    for (const auto &event : batch.input.events) { combined += event.text; }
    check(combined == "日本語の長い確定文字列を途中で壊さずに送信するテスト", "UTF-8 event splitting");
    frame.text_input_active = false;
    std::vector<SDL_Event> rejected;
    std::string error;
    check(!compile_test_key(R"({"key":"not-a-key"})", rejected, error) && rejected.empty(), "Unknown key rejected");
    check(!compile_test_key(R"({"text":"\n"})", rejected, error), "Text control character rejected");
    check(!compile_test_key(R"({"key":"x","ctr":true})", rejected, error), "Misspelled field rejected");
    check(!compile_test_key(R"({"key":"1","shift":true})", rejected, error), "Ambiguous shifted punctuation rejected");
    check(!compile_test_key(R"({"key":"x","ctrl":true,"text":"x"})", rejected, error), "Ctrl does not generate printable text");
    // ファイル末尾への追記・未完成行・同じパスでの再設定を検査する。
    const auto input_path = std::filesystem::temp_directory_path()
        / ("hd2d-key-check-" + std::to_string(SDL_GetPerformanceCounter()) + ".jsonl");
    {
        std::ofstream(input_path, std::ios::binary).close();
        TestKeyboard keyboard;
        check(keyboard.configure(input_path.string(), error), "Configure input file");
        { std::ofstream out(input_path, std::ios::binary | std::ios::app); out << R"({"id":"one","key":"Enter"})"; }
        batch = InputBatch{};
        keyboard.poll("test"); pump_sdl_events(ctx);
        check(batch.input.events.empty(), "Partial command is not executed");
        { std::ofstream out(input_path, std::ios::binary | std::ios::app); out << '\n'; }
        keyboard.poll("test"); pump_sdl_events(ctx);
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "confirm", "Appended newline executes command");
        batch = InputBatch{};
        check(keyboard.configure(input_path.string(), error), "Reconfigure same file");
        keyboard.poll("test"); pump_sdl_events(ctx);
        check(batch.input.events.empty(), "Reconfigure does not replay input");
        { std::ofstream out(input_path, std::ios::binary | std::ios::app); out << R"({"id":"two","key":"Escape"})" << '\n'; }
        keyboard.poll("test"); pump_sdl_events(ctx);
        check(batch.input.events.size() == 1 && batch.input.events[0].e == "cancel", "Second appended command");
    }
    std::filesystem::remove(input_path);
    std::filesystem::remove(input_path.string() + ".ack.jsonl");

    {
        // 画面の状態の問い合わせ（`{"query":"ui"}`）・部品を名前で押す入口（`{"tap":...}`）・ホイール・窓を閉じる。
        // 読み取り・拒否・イベントへの変換。実機の確認は tools/hd2d_verify/ui_query_live.py。
        std::vector<SDL_Event> ev;
        std::string err;
        check(compile_test_key(R"({"query":"ui"})", ev, err) && ev.empty(), "UI query accepted without events");
        check(compile_test_key(R"({"id":"q","query":"ui"})", ev, err), "UI query accepts an id");
        check(!compile_test_key(R"({"query":"ui","key":"x"})", ev, err), "UI query cannot be combined with input");
        check(!compile_test_key(R"({"query":"net"})", ev, err), "Unknown query rejected");
        check(compile_test_key(R"({"tap":{"label":"OK"}})", ev, err) && ev.empty(), "tap accepted (resolved at poll time)");
        check(compile_test_key(R"({"tap":{"id":"vpad:3","index":1,"kind":"row","via":"touch"}})", ev, err), "tap by id with index, kind and via");
        check(!compile_test_key(R"({"tap":{}})", ev, err), "tap without label or id rejected");
        check(!compile_test_key(R"({"tap":{"label":"a","id":"b"}})", ev, err), "tap with both label and id rejected");
        check(!compile_test_key(R"({"tap":{"label":""}})", ev, err), "tap with an empty label rejected");
        check(!compile_test_key(R"({"tap":{"label":"a","bogus":1}})", ev, err), "tap unknown field rejected");
        check(!compile_test_key(R"({"tap":{"label":"a","via":"pen"}})", ev, err), "tap unknown via rejected");
        check(!compile_test_key(R"({"tap":{"label":"a","index":-1}})", ev, err), "tap negative index rejected");
        check(!compile_test_key(R"({"tap":{"label":"a"},"key":"x"})", ev, err), "tap cannot be combined with input");
        check(compile_test_key(R"({"wheel":{"x":0,"y":-1}})", ev, err) && ev.size() == 1 && ev[0].type == SDL_MOUSEWHEEL
                && ev[0].wheel.y == -1, "wheel becomes one SDL_MOUSEWHEEL");
        check(compile_test_key(R"({"wheel":{"y":2,"at":[640,300]}})", ev, err), "wheel with a position accepted");
        check(!compile_test_key(R"({"wheel":{"x":0,"y":0}})", ev, err), "zero wheel rejected");
        check(!compile_test_key(R"({"wheel":{"y":1,"at":[-1,5]}})", ev, err), "wheel position out of range rejected");
        check(!compile_test_key(R"({"wheel":{"y":1,"z":1}})", ev, err), "wheel unknown field rejected");
        check(compile_test_key(R"({"quit":true})", ev, err) && ev.size() == 1 && ev[0].type == SDL_QUIT, "quit becomes SDL_QUIT");
        check(!compile_test_key(R"({"quit":false})", ev, err), "quit false rejected");
        check(!compile_test_key(R"({"quit":true,"key":"x"})", ev, err), "quit cannot be combined with input");

        UiProbe empty_probe;
        empty_probe.enable(true);
        check(!compile_test_tap(R"({"tap":{"label":"OK"}})", empty_probe, ev, err) && err.find("no frame") != std::string::npos,
            "tap before any frame is drawn is refused");
        UiProbe probe;
        probe.enable(true);
        probe.button("決定", ProbeRect{ 100, 200, 60, 40 }, true, false, "footer", false, "confirm");
        probe.button("取消", ProbeRect{ 200, 200, 60, 40 }, true, false, "footer", false, "cancel");
        probe.button("X", ProbeRect{ 10, 10, 20, 20 }, true, false, "vpad", true, "vpad:1");
        probe.button("X", ProbeRect{ 300, 10, 20, 20 }, true, false, "row", false, "x-row");
        probe.button("遠い", ProbeRect{ 2000, 10, 20, 20 }, true, false, "row");
        probe.frame_info("core", 7, 1280, 720, "game", "command");
        probe.commit("game");
        check(compile_test_tap(R"({"tap":{"label":"決定"}})", probe, ev, err) && ev.size() == 2
                && ev[0].type == SDL_MOUSEBUTTONDOWN && ev[0].button.x == 130 && ev[0].button.y == 220
                && ev[1].type == SDL_MOUSEBUTTONUP, "tap clicks the center of the button");
        check(compile_test_tap(R"({"tap":{"label":"取消","via":"touch"}})", probe, ev, err) && ev.size() == 2
                && ev[0].type == SDL_FINGERDOWN && std::abs(ev[0].tfinger.x - (230.f / 1280.f)) < 1e-6f
                && std::abs(ev[0].tfinger.y - (220.f / 720.f)) < 1e-6f && ev[1].type == SDL_FINGERUP, "tap via touch uses normalized coordinates");
        check(compile_test_tap(R"({"tap":{"id":"vpad:1"}})", probe, ev, err) && ev.size() == 2 && ev[0].type == SDL_FINGERDOWN,
            "a touch-only button is tapped by touch (auto)");
        check(!compile_test_tap(R"({"tap":{"label":"X"}})", probe, ev, err) && err.find("2 buttons match") != std::string::npos && ev.empty(),
            "an ambiguous name is refused without pressing");
        check(compile_test_tap(R"({"tap":{"label":"X","kind":"row"}})", probe, ev, err) && ev[0].button.x == 310, "kind narrows the match");
        check(compile_test_tap(R"({"tap":{"label":"X","index":1}})", probe, ev, err) && ev[0].button.x == 310, "index picks the n-th match");
        check(!compile_test_tap(R"({"tap":{"label":"X","index":2}})", probe, ev, err) && err.find("out of range") != std::string::npos,
            "an index past the matches is refused");
        check(compile_test_tap(R"({"tap":{"label":"決","contains":true}})", probe, ev, err), "contains matches part of a name");
        check(!compile_test_tap(R"({"tap":{"label":"無い"}})", probe, ev, err) && err.find("no button matches") != std::string::npos,
            "a missing button is refused");
        check(!compile_test_tap(R"({"tap":{"label":"遠い"}})", probe, ev, err) && err.find("outside the window") != std::string::npos,
            "a button whose center is outside the window is refused");
        const auto snapshot = nlohmann::json::parse(probe.snapshot_json("game"));
        check(snapshot["valid"] == true && snapshot["phase"] == "game" && snapshot["core"] == "core" && snapshot["buttons"].size() == 5
                && snapshot["input_wait"]["kind"] == "command" && snapshot["buttons"][0]["id"] == "confirm"
                && snapshot["liveness"].contains("core_alive") && snapshot["fx"].contains("spawned_total")
                && snapshot["floor"]["cutin"]["active"] == false && snapshot["menu"]["open"] == false
                && snapshot["targeting"]["active"] == false && snapshot["notices"].is_array(), "UI snapshot has the documented fields");
        // 壊れた UTF-8 の字が入っても、応答の組み立てで落ちない（U+FFFD に置き換わる）。
        UiProbe broken;
        broken.enable(true);
        broken.text(0, 0, 10, 10, std::string("a\xff" "b"));
        broken.commit("game");
        check(nlohmann::json::parse(broken.snapshot_json("game"))["texts"][0]["text"].get<std::string>().find("\xEF\xBF\xBD") != std::string::npos,
            "broken UTF-8 in a drawn text becomes U+FFFD (no exception)");
        // 通知は、出てから消えて、また出たときだけ回数が増える。
        UiProbe notices;
        notices.enable(true);
        notices.notice("busy");
        notices.commit("game");
        notices.notice("busy");
        notices.commit("game");
        check(nlohmann::json::parse(notices.snapshot_json("game"))["notices"][0]["count"] == 1, "a notice that stays is counted once");
        notices.frame_info("c", 1, 10, 10, "game", "command");
        notices.commit("game");
        notices.notice("busy");
        notices.commit("game");
        check(nlohmann::json::parse(notices.snapshot_json("game"))["notices"][0]["count"] == 2, "a notice that reappears is counted again");
        // 地形・実体の要約（`map.cells`）とマス → 画面座標（`{"query":"cell"}`）。
        check(compile_test_key(R"({"query":"ui","map":"full"})", ev, err) && ev.empty(), "UI query accepts map=full");
        check(!compile_test_key(R"({"query":"ui","map":"all"})", ev, err), "UI query rejects an unknown map range");
        check(compile_test_key(R"({"query":"cell","x":3,"y":-4})", ev, err) && ev.empty(), "cell query accepted");
        check(!compile_test_key(R"({"query":"cell","x":3})", ev, err), "cell query without y rejected");
        check(!compile_test_key(R"({"query":"cell","x":"a","y":1})", ev, err), "cell query with a non-integer rejected");
        check(!compile_test_key(R"({"query":"ui","x":3})", ev, err), "UI query with a coordinate rejected");
        UiProbe grid;
        grid.enable(true);
        std::vector<ProbeCell> cells;
        for (int gy = 0; gy < 40; ++gy) {
            for (int gx = 0; gx < 40; ++gx) {
                ProbeCell pc;
                pc.gx = gx;
                pc.gy = gy;
                pc.kind = ((gx == 30) && (gy == 5)) ? "door" : "floor";
                pc.glyph = '.';
                if ((gx == 30) && (gy == 5)) {
                    pc.glyph = '3';
                    pc.entrance = '3';
                }
                if ((gx == 6) && (gy == 6)) pc.monster = 77;
                cells.push_back(pc);
            }
        }
        grid.map(5, 5, 1, 1, 0, true, "3d");
        grid.map_cells(cells, "0:0:1:1:-", { "#.", ".#" });
        grid.set_cell_projector([](int gx, int gy, int &sx, int &sy, bool &inside) {
            sx = 100 + (gx * 10);
            sy = 50 + (gy * 10);
            inside = (gx < 20);
            return true;
        });
        grid.frame_info("core", 1, 1280, 720, "game", "command");
        grid.commit("game");
        const auto near_snapshot = nlohmann::json::parse(grid.snapshot_json("game", "near"));
        const auto &near_cells = near_snapshot["map"]["cells"];
        check(near_cells["region"] == "near" && near_cells["list"].size() == 18 * 18 && !near_cells.contains("floor"),
            "near map covers the +-12 cells around the player (clipped to the known area)");
        check(near_cells["monsters"].size() == 1 && near_cells["monsters"][0]["id"] == 77 && near_cells["player"]["gx"] == 5,
            "map entities list the monsters");
        check(near_cells["list"][0].contains("screen") && near_cells["list"][0]["screen"]["x"] == 100, "map cells carry their screen position");
        const auto full_snapshot = nlohmann::json::parse(grid.snapshot_json("game", "full"));
        const auto &full_cells = full_snapshot["map"]["cells"];
        check(full_cells["region"] == "full" && full_cells["list"].size() == 1600 && full_cells["floor"]["rows"].size() == 2
                && full_cells["floor"]["h"] == 2, "full map covers every drawn cell and the whole-floor grid");
        check(full_cells["entrances"].size() == 1 && full_cells["entrances"][0]["label"] == "3" && full_cells["entrances"][0]["gx"] == 30,
            "numbered entrances are remembered");
        check(near_cells["entrances"].size() == 0, "a far entrance is not in the near map");
        check(!nlohmann::json::parse(grid.snapshot_json("game", "none"))["map"].contains("cells"), "map=none leaves the cells out");
        const auto cell = nlohmann::json::parse(grid.cell_json(7, 8));
        check(cell["projectable"] == true && cell["on_screen"] == true && cell["screen"]["x"] == 170 && cell["screen"]["y"] == 130
                && cell["kind"] == "floor", "cell query returns the screen position");
        check(nlohmann::json::parse(grid.cell_json(25, 8))["on_screen"] == false, "a cell outside the map rectangle is not on screen");
        // 階が変わると、覚えていた入口の番号は捨てる。
        grid.map(5, 5, 1, 1, 0, true, "3d");
        grid.map_cells({ ProbeCell{} }, "0:0:2:1:-", {});
        grid.commit("game");
        check(nlohmann::json::parse(grid.snapshot_json("game", "full"))["map"]["cells"]["entrances"].size() == 0,
            "entrance numbers are dropped when the floor changes");
        // 無効のときは何も記録しない。
        UiProbe off;
        off.text(0, 0, 1, 1, "x");
        off.button("a", ProbeRect{ 0, 0, 1, 1 });
        off.commit("game");
        check(!off.has_frame() && off.button_count() == 0, "a disabled probe records nothing");
        // 追加の欄：字の色・字形の欠け・はみ出し、光、狙い・見る、重ね書き、実体の輪、窓の焦点、昼夜、マスの詳細。
        check(compile_test_key(R"({"query":"ui","detail":true})", ev, err) && ev.empty(), "UI query accepts detail");
        check(!compile_test_key(R"({"query":"ui","detail":"yes"})", ev, err), "UI query rejects a non-boolean detail");
        UiProbe extra;
        extra.enable(true);
        {
            ProbeTextStyle red;
            red.r = 1.f;
            red.g = 0.f;
            red.b = 0.f;
            extra.text(10, 10, 40, 16, "red", red);
            ProbeTextStyle bad;
            bad.missing = 2;
            bad.replacement = 1;
            bad.clipped = true;
            extra.text(10, 40, 40, 16, "bad", bad);
            extra.text(1270, 700, 40, 40, "off");
            extra.frame_info("core", 1, 1280, 720, "game", "command");
            extra.map(5, 5, 1, 1, 1, true, "3d");
            ProbeCell lit_cell;
            lit_cell.gx = 6;
            lit_cell.gy = 5;
            lit_cell.terrain_id = 33;
            lit_cell.tile_index = 9;
            lit_cell.fg_color = 11;
            lit_cell.fg_rgb[0] = 255;
            lit_cell.fg_rgb[1] = 255;
            lit_cell.fg_rgb[2] = 0;
            lit_cell.lit = true;
            lit_cell.role = "house";
            lit_cell.site = 4;
            extra.map_cells({ lit_cell }, "0:0:1:1:-", {});
            extra.map_tile_names({ { 9, "K:12:torch" } });
            ProbeLight light;
            light.declared = true;
            light.kind = 1;
            light.radius = 2;
            light.lit_cells = 13;
            light.shown_cells = 9;
            light.max_distance = 2;
            extra.light(light);
            ProbeTargetingDetail detail;
            detail.ui_active = true;
            detail.ui_mode = "point";
            detail.has_selected = true;
            detail.selected_x = 7;
            detail.selected_y = 5;
            detail.selected_kind = "free";
            detail.area.push_back({ 7, 5, false });
            detail.area.push_back({ 8, 5, true });
            extra.targeting(true, "target", "");
            extra.targeting_detail(std::move(detail));
            extra.overlays({ ProbeOverlay{ 6, 5, "*", 11 } }, 1);
            extra.entities({ ProbeEntity{ 6, 6, "monster", 77, 3, 0, "unaware", true } }, 1);
            ProbeWindowState window_state;
            window_state.input_focus = false;
            window_state.test_hidden = true;
            window_state.pad_focus = true;
            extra.window_state(window_state);
            ProbeTimeState time_state;
            time_state.day_minute = 60;
            time_state.daytime = false;
            time_state.night_factor = 1.f;
            extra.time_state(time_state);
            extra.commit("game");
        }
        const auto extra_plain = nlohmann::json::parse(extra.snapshot_json("game"));
        check(extra_plain["texts"][0]["color"] == "#FF0000" && extra_plain["texts"][1]["clipped"] == true
                && extra_plain["texts"][1]["missing_glyphs"] == 2 && extra_plain["texts"][1]["replacement_chars"] == 1,
            "texts carry the drawn color and the clipped / missing / replacement marks");
        check(extra_plain["text_issues"]["missing_glyphs"] == 2 && extra_plain["text_issues"]["replacement_chars"] == 1
                && extra_plain["text_issues"]["clipped"] == 1 && extra_plain["text_issues"]["off_screen"] == 1
                && extra_plain["text_issues"]["total"]["missing_glyphs"] == 2 && extra_plain["text_issues"]["samples"].size() >= 3
                && extra_plain["text_issues"]["by_layer"][""]["missing_glyphs"] == 2,
            "text_issues counts the latest frame, the running total and the count per layer");
        check(extra_plain["map"]["lit_cells"] == 13 && extra_plain["map"]["light"]["kind_name"] == "torch"
                && extra_plain["map"]["light"]["shown_cells"] == 9 && extra_plain["map"]["light"]["radius"] == 2,
            "map.lit_cells and map.light report the lit cells, kind and radius");
        check(extra_plain["map"]["overlays"].size() == 1 && extra_plain["map"]["overlays"][0]["text"] == "*"
                && extra_plain["map"]["overlays_drawn"] == 1, "map.overlays lists the temporary overwritten characters");
        check(extra_plain["map"]["entity_list"].size() == 1 && extra_plain["map"]["entity_list"][0]["ring"] == "unaware"
                && extra_plain["map"]["entity_list"][0]["ring_drawn"] == true && extra_plain["map"]["rings_drawn"] == 1,
            "map.entity_list carries the alertness ring");
        check(extra_plain["map"]["time"]["daytime"] == false && extra_plain["map"]["time"]["day_minute"] == 60, "map.time reports day or night");
        check(extra_plain["targeting"]["active"] == true && extra_plain["targeting"]["via"] == "targeting-ui"
                && extra_plain["targeting"]["selected"]["kind"] == "free" && extra_plain["targeting"]["area_cells"].size() == 2
                && extra_plain["targeting"]["area_cells"][1]["possible"] == true && extra_plain["targeting"]["path_cells"].size() == 0,
            "targeting reports what is pointed at and the area cells");
        check(extra_plain["app_window"]["pad_focus"] == true && extra_plain["app_window"]["input_focus"] == false
                && extra_plain["app_window"]["test_hidden"] == true, "app_window reports the focus state");
        check(!extra_plain["map"]["cells"]["list"][0].contains("feat"), "cell details are left out unless asked for");
        const auto extra_detail = nlohmann::json::parse(extra.snapshot_json("game", "near", true));
        const auto &detail_cell = extra_detail["map"]["cells"]["list"][0];
        check(detail_cell["feat"] == 33 && detail_cell["tile"] == "K:12:torch" && detail_cell["color"] == "#FFFF00" && detail_cell["lit"] == true
                && detail_cell["role"] == "house" && detail_cell["site"] == 4, "detail adds terrain number, tile name, color, light and town role");
        // 見る `l` の帯だけが出ているとき（窓は立たない）。
        UiProbe band;
        band.enable(true);
        band.frame_info("core", 1, 1280, 720, "game", "prompt");
        band.targeting(true, "look", "");
        ProbeTargetingDetail band_detail;
        band_detail.band = true;
        band_detail.band_text = "Look: [Next]";
        band.targeting_detail(std::move(band_detail));
        band.commit("game");
        const auto band_snapshot = nlohmann::json::parse(band.snapshot_json("game"));
        check(band_snapshot["targeting"]["active"] == true && band_snapshot["targeting"]["mode"] == "look"
                && band_snapshot["targeting"]["via"] == "prompt-band" && band_snapshot["targeting"]["band_text"] == "Look: [Next]",
            "a look band reports targeting.mode=look without a window");
    }

    /* --- Command/UI audit: pure Modern semantic transforms. ----------------
     * These screens are expensive to reach in a real character.  Feed the
     * exact native screen grammar into the production interpreters instead so
     * every release can verify the controller/touch command surface without a
     * bespoke save file. */
    const auto action_has = [](const ModernWindow &window, int key) {
        return std::any_of(window.actions.begin(), window.actions.end(),
            [key](const ModernAction &action) { return action.key == key; });
    };
    const auto row_has = [](const ModernWindow &window, int key) {
        return std::any_of(window.rows.begin(), window.rows.end(),
            [key](const ModernRow &row) { return row.key == key; });
    };
    const auto add_line = [](GameFrame &target, int row, std::string text) {
        TermMirrorLine line;
        line.source_row = row;
        line.text_utf8 = std::move(text);
        target.menu_term_lines.push_back(std::move(line));
    };
    {
        GameFrame sample;
        const std::string prompt = "Choose a monster race (by symbol): ";
        check(presentation::is_monster_symbol_prompt(prompt), "Monster-symbol prompt recognized");
        presentation::expose_ascii_symbol_prompt(sample, prompt);
        check(sample.menu_open && sample.menu_choices.size() == 94
            && sample.menu_choices.front().key == 33 && sample.menu_choices.back().key == 126,
            "Monster-symbol ASCII choices");
    }
    {
        Hd2dSettings mode_settings;
        UiLayout mode_layout;
        mode_settings.modern_ui=true;
        auto mode_state=build_ui_state(ViewWindow{},mode_layout,mode_settings,false);
        check(mode_state.modern_ui&&mode_state.targeting_ui,"Modern mode reaches core state");
        mode_settings.modern_ui=false;
        mode_state=build_ui_state(ViewWindow{},mode_layout,mode_settings,false);
        check(!mode_state.modern_ui&&!mode_state.targeting_ui,"Original mode clears core state immediately");
        mode_settings.modern_ui=true;
        mode_state=build_ui_state(ViewWindow{},mode_layout,mode_settings,false);
        check(mode_state.modern_ui&&mode_state.targeting_ui,"Modern mode can be re-entered after Original");
    }
    {
        const auto look_semantics=presentation::analyze_command_prompt("Look <dir> +,-,p,m,o,g,t");
        check(look_semantics.kind==presentation::CommandPromptKind::Look
            &&look_semantics.keys==std::vector<int>({'+','-','p','m','o','g','t'}),"Common Look prompt semantics");
        const auto verify_at=presentation::analyze_command_prompt("Please verify SUICIDE by typing the '@' sign");
        check(verify_at.kind==presentation::CommandPromptKind::VerifyAt&&verify_at.keys==std::vector<int>({'@',27}),"Raw @ prompt semantics");
        // AH-60: Angband の引退（Q）の最後の確認は、英語では別の文言で、日本語は変愚と同じ文（'@' と「確認」を含む）。
        // どちらも「'@' を打って確認する」画面と見分けること（英日どちらでも働く）。
        const auto verify_retire=presentation::analyze_command_prompt("Please verify RETIRING THIS CHARACTER by typing the '@' sign:");
        check(verify_retire.kind==presentation::CommandPromptKind::VerifyAt&&verify_retire.keys==std::vector<int>({'@',27}),"Raw @ prompt semantics (Angband retire, English)");
        const auto verify_ja=presentation::analyze_command_prompt(reinterpret_cast<const char*>(u8"確認のため '@' を押して下さい。"));
        check(verify_ja.kind==presentation::CommandPromptKind::VerifyAt&&verify_ja.keys==std::vector<int>({'@',27}),"Raw @ prompt semantics (Japanese)");
        const auto not_verify=presentation::analyze_command_prompt(reinterpret_cast<const char*>(u8"'@' は自分を表す記号です。"));
        check(not_verify.kind==presentation::CommandPromptKind::None,"A sentence with '@' but without the confirmation words is not a verify prompt");
        const auto verify_yes=presentation::analyze_command_prompt("Are you sure? (special action)");
        check(verify_yes.kind==presentation::CommandPromptKind::VerifyYesNo&&verify_yes.keys==std::vector<int>({'y',27}),"Raw yes/no prompt semantics");
        const auto ordinary_yes=presentation::analyze_command_prompt("Are you sure? (Teleport Level) [y/n]");
        check(ordinary_yes.kind==presentation::CommandPromptKind::None&&ordinary_yes.keys.empty(),"Ordinary [y/n] prompt is not hijacked");
        const auto force=presentation::analyze_command_prompt("Choose discipline, w for the Force");
        check(force.kind==presentation::CommandPromptKind::Force&&force.keys==std::vector<int>({'w',27}),"Force prompt semantics");
        // 名前が長くて見る行が桁の端（80 桁）で切れ、終わりの操作の案内が落ちた行（共用の look_row_was_cut）。
        {
            using presentation::LookRowHeads;
            const auto u8c=[](const char8_t*s){return std::string(reinterpret_cast<const char*>(s));};
            const std::string en_monster="[??]Target:Dercynus the Heavy Drinker(Level ???, unhurt, Aq) [r, q,t,p,o,+,-,<di";
            const auto cut_monster=presentation::analyze_command_prompt(en_monster);
            check(cut_monster.kind==presentation::CommandPromptKind::Look&&cut_monster.look_row_cut
                &&cut_monster.keys==std::vector<int>({'+','-','p','o','t'}),"Cut look row (English monster) is a Look prompt with the monster keys");
            const std::string en_item="Target:a Potion of Resist Heat and Cold of the Very Long Name of Testing [2,+0] [q,p,o,+";
            const auto cut_item=presentation::analyze_command_prompt(en_item);
            check(cut_item.kind==presentation::CommandPromptKind::Look&&cut_item.look_row_cut
                &&cut_item.keys==std::vector<int>({'+','-','p','o'}),"Cut look row (English item) has no target key");
            const std::string ja_gone=u8c(u8"[??]ターゲット:グレーター・サイバー・ワイアーム・天使・デーモン・苔(レベル???, 無傷, 地)");
            const auto cut_ja=presentation::analyze_command_prompt(ja_gone);
            check(cut_ja.kind==presentation::CommandPromptKind::Look&&cut_ja.look_row_cut,"Cut look row (Japanese, guide gone) is a Look prompt");
            const std::string ja_part=ja_gone+u8c(u8" [r思 q止 t決 p自 o現 +次");
            const auto part_ja=presentation::analyze_command_prompt(ja_part);
            check(part_ja.kind==presentation::CommandPromptKind::Look&&part_ja.look_row_cut,"Cut look row (Japanese, guide partly left) is a Look prompt");
            const std::string whole_en="[042]Target:Grip, Farmer Maggot's Dog the Barking(Level 5, unhurt, Aq) [r, q,t,p,o,+,-,<dir>]";
            const auto whole=presentation::analyze_command_prompt(whole_en);
            check(whole.kind==presentation::CommandPromptKind::Look&&!whole.look_row_cut&&!presentation::look_row_was_cut(whole_en),
                "A long look row with its whole guide is read from the guide, not treated as cut");
            check(presentation::analyze_command_prompt("Target:a Cave spider").kind==presentation::CommandPromptKind::None,"A short Target: line is not a look prompt");
            check(presentation::analyze_command_prompt("Target:Dercynus the Heavy Drinker(Level ???, unhurt, Aq) and so on and on -more-").kind==presentation::CommandPromptKind::None,
                "A -more- line is not a look prompt");
            check(!presentation::look_row_was_cut("You see a Pair of Leather Boots of Stealth (+1) [3,+0] <+2> that you are wearing"),
                "You see ... is not a cut look row for the Hengband heads");
            // Frox: 頭は `Target: `・`Feature: `・`Monster: `・`Position: `。
            const std::string frox_cut="Monster: Grip, Farmer Maggot's Dog the Bounding and Barking (Asleep) (Rng 3) [r,/,x,q,t";
            check(presentation::look_row_was_cut(frox_cut,LookRowHeads::Frox)&&presentation::look_row_was_cut("Position: "+frox_cut.substr(9),LookRowHeads::Frox),
                "Frox cut look row");
            check(!presentation::look_row_was_cut(frox_cut,LookRowHeads::Hengband)&&!presentation::look_row_was_cut("Monster: a Cave spider",LookRowHeads::Frox)
                &&!presentation::look_row_was_cut(frox_cut+",p,o,x,j,+,-,?,<dir>]",LookRowHeads::Frox),
                "Frox cut look row: other heads, short rows and rows with the whole guide are not cut");
            // Sil-Q: 頭が普通の伝言と同じ書き出しなので 78 桁以上。
            const std::string sq_cut="You see a Pair of Leather Boots of Stealth (+1) [3,+0] <+2> that you are wearing";
            check(presentation::look_row_was_cut(sq_cut,LookRowHeads::SilQ)&&!presentation::look_row_was_cut(sq_cut.substr(0,77),LookRowHeads::SilQ)
                &&!presentation::look_row_was_cut(sq_cut+" -more-",LookRowHeads::SilQ),"Sil-Q cut look row needs 78 columns and no -more-");
            // 表示の幅: 全角は 2 桁、半角カナは 1 桁。
            check(presentation::look_row_display_width(u8c(u8"ターゲット:"))==11&&presentation::look_row_display_width(u8c(u8"ｱｲ"))==2&&presentation::look_row_display_width("abc")==3,
                "Display width counts full-width as 2 and half-width kana as 1");
            // 説明の字: 閉じていない最後の `[` は落とし、閉じた `[`（頭の経験値・アイテムの情報）は残す。
            check(presentation::look_row_cut_subject("abc [r, q")=="abc "&&presentation::look_row_cut_subject("[??]Target:x")=="[??]Target:x"
                &&presentation::look_row_cut_subject("Target:y [2,+0]")=="Target:y [2,+0]"&&presentation::look_row_cut_subject(en_item)=="Target:a Potion of Resist Heat and Cold of the Very Long Name of Testing [2,+0] ",
                "Cut look row subject drops only the unclosed bracket");
        }
    }
    {
        GameFrame look;
        look.modern.version=5;
        look.modern.kind="dialog";
        look.modern.stage="confirm";
        look.prompt.text_utf8="Look:";
        look.prompt.footer_utf8="LB/RB: select action  A/Enter: run  D-pad: move look cursor";
        look.prompt.choices={{"Next interesting",'+',0,1},{"Previous interesting",'-',2,1},{"Player",'p',4,1}};
        ModernStoreUi look_modern;
        look_modern.sync(look,true);
        check(!look_modern.active(),"Look prompt bypasses full Modern dialog");
        // 見る・狙うの途中で `r`（モンスターの思い出）が画面を退避して重なっている間は、アダプタが見るの帯を送らない。
        // 作り直し（2026-10-10）: 以前は画面側が帯の文言（`LB/RB:` で始まり「カーソル移動」を含む）で重なりを見分けていた
        // （`look_prompt_covered()`）。弱いので、各コアのアダプタが「ゲーム本体が画面を退避した深さ」で見分ける形にし、
        // 画面側の判定は外した。その記憶（`look_screen_cover.h`）の検査へ置き換える（文言の検査は要らなくなった）。
        // Android のソフトキーボード：文字入力では出し、数量の欄では出さない。
        {
            GameFrame text;text.text_input_active=true;
            check(wants_soft_keyboard(text),"soft keyboard: a free text prompt shows the keyboard");
            GameFrame quantity;quantity.text_input_active=true;quantity.numeric.active=true;quantity.numeric.min=1;quantity.numeric.max=22;quantity.numeric.value=1;
            check(!wants_soft_keyboard(quantity),"soft keyboard: a quantity prompt does not show the keyboard (the D-pad changes the number)");
            GameFrame idle;
            check(!wants_soft_keyboard(idle),"soft keyboard: no prompt, no keyboard");
        }
        {
            presentation::LookScreenCover cover;
            {
                const presentation::LookScreenCover::Frame frame(cover);
                check(!cover.covered(1),"look cover: the first look prompt fixes the base depth (a core whose look already saved a screen is not covered)");
            }
            {
                const presentation::LookScreenCover::Frame frame(cover);
                check(!cover.covered(1),"look cover: the same depth is not covered");
            }
            {
                const presentation::LookScreenCover::Frame frame(cover);
                check(cover.covered(2),"look cover: a deeper screen (monster recall) covers the look prompt");
            }
            {
                const presentation::LookScreenCover::Frame frame(cover);
                check(cover.covered(3),"look cover: stays covered while still deeper");
            }
            {
                const presentation::LookScreenCover::Frame frame(cover);
                check(!cover.covered(1),"look cover: closing the recall shows the look prompt again");
            }
            {
                const presentation::LookScreenCover::Frame frame(cover); // 見るの帯を作らないフレーム（見るをやめた）
            }
            {
                const presentation::LookScreenCover::Frame frame(cover);
                check(!cover.covered(4),"look cover: a new look after leaving takes its own base depth");
            }
            {
                // 退避が深いまま見るの帯を作らなかったフレームを挟むと、基準は捨てられ、次の見るが新しい基準になる。
                const presentation::LookScreenCover::Frame frame(cover);
                (void)cover.covered(4);
            }
            presentation::LookScreenCover other;
            {
                const presentation::LookScreenCover::Frame frame(other);
                check(!other.covered(0),"look cover: depth 0 is the base of a core whose look saves nothing");
            }
            {
                const presentation::LookScreenCover::Frame frame(other);
                check(other.covered(1),"look cover: depth 1 covers a look whose base is 0");
            }
        }
        // 重なっている間のフレーム（見るの帯なし・原作の画面あり）は、本文の窓になる。
        {
            GameFrame recall;recall.menu_open=true;
            const auto put=[&](int row,const char *text){TermMirrorLine l;l.source_row=row;l.text_utf8=text;recall.menu_term_lines.push_back(std::move(l));};
            put(0,reinterpret_cast<const char*>(u8"[105] 赤イモムシの大群 ('w')/('w'):  [r]"));
            put(1,reinterpret_cast<const char*>(u8"このモンスターを倒したことはない。"));
            put(2,reinterpret_cast<const char*>(u8"それはベトベトしたイモムシの大群だ。"));
            presentation::NativeModernMenu menu;menu.set_enabled(true);menu.set_language("ja");menu.capture(recall);
            check(recall.modern.kind=="dialog"&&recall.modern.stage=="detail"&&!recall.modern.content.empty(),"monster recall over the look is a dialog with a body");
            ModernStoreUi recall_modern;recall_modern.sync(recall,true);
            check(recall_modern.active(),"monster recall over the look (no look prompt sent) opens the modern window");
        }
        // 呪文の選択（本を選んだあとの表）は「見る」と誤判定されず、1 つ 1 行の選択肢になる（変愚の画面の写し）。
        {
            GameFrame spells;spells.menu_open=true;
            const auto put=[&](int row,const char *text){TermMirrorLine l;l.source_row=row;l.text_utf8=text;spells.menu_term_lines.push_back(std::move(l));};
            put(0,reinterpret_cast<const char*>(u8"(呪文:a-h, '*'で一覧, ESCで中断) どの呪文を学びますか?"));
            put(1,reinterpret_cast<const char*>(u8"                    名前                    熟練度 Lv   MP 失率 効果"));
            put(2,reinterpret_cast<const char*>(u8"                 》 マジック・ミサイル        [初]  1    2   8% 未知"));
            put(3,reinterpret_cast<const char*>(u8"                    トラップ/ドア破壊         [初]  1    4  10% 未知"));
            put(4,reinterpret_cast<const char*>(u8"                    閃光                      [初]  2    4  16% 未知"));
            spells.menu_core_cursor={2,17,19,0,0,13};
            spells.menu_core_cursors={spells.menu_core_cursor};
            ModernWindow window;window.version=5;window.language="ja";window.kind="dialog";window.stage="cursor";window.title="Menu";
            window.cursor=2;window.cursor_span_begin=17;window.cursor_span_len=10; // コアのカーソル（`native_cursor_selection`）が先に置く値
            presentation::interpret_modern_content(spells,window);
            check(window.rows.size()==3&&window.content_type=="spells"&&window.stage=="cursor","spell list becomes three cursor rows");
            check(window.rows.size()==3&&window.rows[0].label==reinterpret_cast<const char*>(u8"マジック・ミサイル")
                &&window.rows[2].label==reinterpret_cast<const char*>(u8"閃光"),"spell names are cut out of the table");
            check(window.cursor==0&&!window.rows.empty()&&window.cursor_target==window.rows[0].id,"spell list keeps the core cursor on the first row");
            check(window.rows.size()==3&&window.rows[0].description.find("8%")!=std::string::npos
                &&window.rows[0].description.find(reinterpret_cast<const char*>(u8"未知"))!=std::string::npos,"spell details are in the description");
            check(window.rows.size()==3&&presentation::modern_focus_key(window,window.rows[2].id)=='2',"moving to a lower spell sends Down");
            ModernWindow decoded;
            check(decode_modern_window(encode_modern_window(window),decoded)&&decoded.rows.size()==3,"spell list survives the wire validation (cursor span must not point outside the empty body)");
        }
        UiCursors look_cursor;
        look_cursor.sync(look);
        presentation::InputEventsMessage look_input;
        check(!look_cursor.handle(CursorNav::Up,look_input)&&look_input.events.empty(),"Look D-pad Up passes through");
        check(!look_cursor.handle(CursorNav::Right,look_input)&&look_input.events.empty(),"Look D-pad Right passes through");
        look_cursor.step_prompt(1);
        check(look_cursor.handle(CursorNav::Confirm,look_input)&&look_input.events.size()==1
            &&look_input.events.back().e=="key"&&look_input.events.back().chr=="-","Look shoulder-selected action confirms native key");
    }
    {
        GameFrame sample;
        ModernWindow window;
        presentation::modern_monster_query_result_controls(sample, window,
            "Recall details? (k/y/n)");
        check(window.content_type == "monster_query_results" && row_has(window, 'y')
            && row_has(window, 'k') && row_has(window, 'n'), "Monster query recall choices");
        window = ModernWindow{};
        presentation::modern_monster_query_result_controls(sample, window, "[(r)ecall, ESC]");
        check(row_has(window, 'r') && row_has(window, '-') && row_has(window, ' '),
            "Monster query result navigation");
    }
    {
        GameFrame sample;
        ModernWindow window;
        window.stage = "detail";
        add_line(sample, 0, "M N K D auto-picker filters");
        for (int row = 1; row <= 8; ++row) add_line(sample, row, "|##########.............|");
        check(presentation::modern_map_content(sample, window), "Explored map recognized");
        check(window.content_type == "map" && action_has(window, 'M') && action_has(window, 'N')
            && action_has(window, 'K') && action_has(window, 'D'), "Explored map filter actions");
    }
    {
        GameFrame sample;
        ModernWindow window;
        window.stage = "detail";
        for (int row = 1; row <= 8; ++row) add_line(sample, row, "|##########.............|");
        check(presentation::modern_map_content(sample, window), "Frox explored map recognized without legend");
        presentation::modern_frox_explored_map_controls(sample, window, "frox");
        check(action_has(window, 'M') && action_has(window, 'N') && action_has(window, 'K')
            && action_has(window, 'D'), "Frox explored-map no-legend actions");
    }
    {
        GameFrame sample;
        ModernWindow window;
        add_line(sample, 0, "World Map: arrows move, * Look around");
        presentation::modern_frox_world_map_controls(sample, window, "frox");
        check(window.actions.size() == 1 && action_has(window, '*'), "Frox world-map Look action only");
    }
    {
        GameFrame sample;
        ModernWindow window;
        window.stage = "number";
        add_line(sample, 0, "Request: 100");
        add_line(sample, 1, "Press 'M' to repay your loan instead.");
        presentation::modern_frox_cornucopia_controls(sample, window, "", "frox");
        check(window.content_type == "cornucopia_amount" && action_has(window, 'M'),
            "Frox Cornucopia amount/mode controls");

        window = ModernWindow{};
        presentation::modern_frox_cornucopia_controls(sample, window,
            "[Space] to unselect a plan, [Enter] to buy this policy", "frox");
        for (const int expected_key : std::array<int, 8>{' ', '8', '2', 'N', '+', 'n', '-', 13})
            check(action_has(window, expected_key), "Frox Cornucopia policy control");
    }
    {
        GameFrame sample;
        ModernWindow window;
        window.stage = "text";
        add_line(sample, 0, "Search key(^I:inven ^L:destroyed): sword");
        sample.menu_term_curs_col = 44;
        presentation::modern_frox_autopick_search_controls(sample, window, "frox");
        check(window.content_type == "autopick_search" && action_has(window, 9)
            && action_has(window, 12) && action_has(window, 18), "Frox auto-pick search controls");
    }
    {
        GameFrame sample;
        ModernWindow window;
        add_line(sample, 0, "[Character Sheet, Line 1] Save to: c/0/Enter = default slot");
        presentation::modern_frox_custom_document_controls(sample, window, "frox");
        for (const int expected_key : std::array<int, 7>{'c', '0', '7', '8', '9', 13, 27})
            check(action_has(window, expected_key), "Frox character-snapshot save control");

        sample.menu_term_lines.clear();
        window = ModernWindow{};
        add_line(sample, 0, "High Score Listing");
        add_line(sample, 1, "Press ^N to sort by Name");
        presentation::modern_frox_custom_document_controls(sample, window, "frox");
        for (const int expected_key : std::array<int, 6>{19, 4, 18, 3, 14, 25})
            check(action_has(window, expected_key), "Frox high-score document control");
    }
    {
        const auto store_has_help=[](int kind) {
            GameFrame sample;
            sample.menu_open=true;
            native_store_info info{};
            info.active=1;info.kind=kind;info.root_frame=1;info.top=1;info.page_size=10;
            presentation::NativeStoreMenu store;
            store.set_enabled(true,false,"frox");
            store.capture(sample,info,{});
            return std::any_of(sample.modern.rows.begin(),sample.modern.rows.end(),[](const ModernRow &row){
                return std::any_of(row.actions.begin(),row.actions.end(),[](const ModernAction &action){return action.key=='?';});
            });
        };
        check(store_has_help(2),"Frox Home help action");
        check(store_has_help(3),"Frox Museum help action");
    }
    {
        GameFrame sample;
        sample.menu_open=true;
        native_store_info info{};
        info.active=1;info.kind=1;info.root_frame=1;info.top=1;info.page_size=10;
        info.get_key='p';info.put_key='s';info.previous_key='-';info.next_key=' ';
        info.show_hidden_key='U';info.hidden_count=3;
        presentation::NativeStoreMenu store;
        store.set_enabled(true,false,"frox");
        store.capture(sample,info,{});
        const bool has_unwanted=std::any_of(sample.modern.rows.begin(),sample.modern.rows.end(),[](const ModernRow &row){
            return std::any_of(row.actions.begin(),row.actions.end(),[](const ModernAction &action){return action.key=='U';});
        });
        check(has_unwanted,"Frox shop Unwanted filter action");
    }
    {
        /* 店の確認・数量の操作が、遅れて届いても（直前の画面から続く操作として）受けられ、
         * 受けた操作のあとに画が変わらなくても次の操作が使える（Frox。2026-10-11 の「状態が変わりました」）。
         * 画面の応答は 1 つ前のフレームの revision で届くことがある（コアが同じ段の中で画を描き直したとき）。 */
        const auto make_confirm=[](GameFrame &f,int cursor_line) {
            f=GameFrame{};
            f.menu_open=true;
            f.prompt.text_utf8="Really buy Rations for 5 gp? [y/n]";
            PromptChoice yes;yes.key='y';PromptChoice no;no.key='n';
            f.prompt.choices.push_back(yes);f.prompt.choices.push_back(no);
            f.menu_core_cursor.line_index=cursor_line;
        };
        native_store_info info{};
        info.active=1;info.kind=1;info.root_frame=1;info.top=1;info.page_size=10;
        info.get_key='p';info.put_key='s';info.previous_key='-';info.next_key=' ';
        const auto answer_event=[](int request,int revision,int number) {
            presentation::InputEventWire e;
            e.e="ui_action";e.action="answer";e.request_id=request;e.ui_revision=revision;e.number=number;
            return e;
        };
        for(const char *profile:{"frox","generic"}) {
            const bool frox=std::string(profile)=="frox";
            presentation::NativeStoreMenu store;
            store.set_enabled(true,false,profile);
            GameFrame first,second;
            make_confirm(first,0);
            store.capture(first,info,{});
            const int old_revision=first.modern.revision;
            make_confirm(second,3);            // 同じ確認の段のまま、画の中身だけが変わった
            store.capture(second,info,{});
            check(second.modern.stage=="confirm"&&second.modern.revision!=old_revision,"store confirm: the frame changed inside the same stage");
            std::vector<int> keys;
            store.input(answer_event(1,old_revision,0),keys);
            check(frox?(keys.size()==1&&keys[0]=='y'):keys.empty(),
                frox?"Frox store: an answer sent for the previous frame of the same question is accepted"
                    :"other cores keep rejecting an answer sent for an older revision");
        }
        {
            /* 受けた操作のあと画が変わらなくても、次のフレームで新しい revision が出て、次の操作が使える */
            presentation::NativeStoreMenu store;
            store.set_enabled(true,false,"frox");
            GameFrame f;make_confirm(f,0);
            store.capture(f,info,{});
            std::vector<int> keys;
            store.input(answer_event(1,f.modern.revision,1),keys);
            check(keys.size()==1&&keys[0]=='n',"Frox store: the first answer is accepted");
            GameFrame same;make_confirm(same,0);
            store.capture(same,info,{});       // 核が何も変えなかった（画が同じ）
            check(same.modern.revision!=f.modern.revision,"Frox store: a new revision is published after an accepted action");
            keys.clear();
            store.input(answer_event(2,same.modern.revision,0),keys);
            check(keys.size()==1&&keys[0]=='y',"Frox store: the next answer is accepted after a no-op");
            keys.clear();
            store.input(answer_event(3,f.modern.revision,0),keys);   // 受けた操作より前の revision は、もう受けない
            check(keys.empty(),"Frox store: an answer for a consumed revision is rejected");
        }
    }
    {
        /* --- 地図のクリック・タップ（AH-58）。本物の SDL のキューから `pump_sdl_events` を通す。 ---------
         * カメラは本物（`Camera::project` でマスの中心の画面の点を出し、そこを押す）なので、視点の回転・傾きで
         * 拾い方が食い違えばここで失敗する。申告しないコアでは何も変わらないことも押さえる。 */
        layout.scene = RectPx{ 0, 0, width, height };
        camera.viewport_w = width;
        camera.viewport_h = height;
        camera.target = Vec3{ 20.5f, 20.5f, 0.f };
        camera.yaw = 0.f;
        frame = GameFrame{};
        frame.cells.emplace_back();
        frame.player_gx = 20;
        frame.player_gy = 20;
        frame.awaiting_command = true;
        std::uint64_t frame_number = 1000;
        frame.frame_id = frame_number;
        settings.vpad.show = false;
        settings.main_panel = Hd2dSettings::MainPanel::Hd2d;
        const auto screen_of = [&](int gx, int gy, float height_in_cells = 0.f) {
            float px = 0.f;
            float py = 0.f;
            (void)camera.project(Vec3{ static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, height_in_cells }, px, py);
            return std::pair<int, int>{ static_cast<int>(std::lround(px - 0.5f)) + layout.scene.x,
                static_cast<int>(std::lround(py - 0.5f)) + layout.scene.y };
        };
        const auto mouse = [](Uint32 type, Uint8 button, int x, int y, Uint32 which = 0) {
            SDL_Event e{};
            e.type = type;
            e.button.button = button;
            e.button.x = x;
            e.button.y = y;
            e.button.which = which;
            e.button.state = (type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED;
            return e;
        };
        const auto motion = [](int x, int y) {
            SDL_Event e{};
            e.type = SDL_MOUSEMOTION;
            e.motion.x = x;
            e.motion.y = y;
            return e;
        };
        const auto finger = [&](Uint32 type, int x, int y, SDL_FingerID id = 7) {
            SDL_Event e{};
            e.type = type;
            e.tfinger.touchId = 1;
            e.tfinger.fingerId = id;
            e.tfinger.x = static_cast<float>(x) / static_cast<float>(width);
            e.tfinger.y = static_cast<float>(y) / static_cast<float>(height);
            return e;
        };
        const auto next_frame = [&]() { frame.frame_id = ++frame_number; };
        const auto clicks = [&]() {
            std::vector<presentation::InputEventWire> found;
            for (const auto &event : batch.input.events) {
                if (event.e == "map_click") {
                    found.push_back(event);
                }
            }
            return found;
        };
        const auto only_click = [&](int gx, int gy, int button, const char *name) {
            const auto found = clicks();
            check(found.size() == 1 && found[0].grid_x == gx && found[0].grid_y == gy && found[0].button == button, name);
            return found.empty() ? presentation::InputEventWire{} : found[0];
        };
        const auto click_cell = [&](int gx, int gy, Uint8 button) {
            const auto at = screen_of(gx, gy);
            pump({ mouse(SDL_MOUSEBUTTONDOWN, button, at.first, at.second), mouse(SDL_MOUSEBUTTONUP, button, at.first, at.second) });
        };

        // 申告しないコア: これまでどおり。隣のマスは 1 歩、右クリックと指は何にもならず、map_click は出ない。
        ctx.map_click_enabled = false;
        click_cell(21, 20, SDL_BUTTON_LEFT);
        check(clicks().empty() && batch.input.events.size() == 1 && batch.input.events[0].e == "move"
                && batch.input.events[0].dx == 1 && batch.input.events[0].dy == 0,
            "A core that does not declare map_click keeps the one-step click walk");
        click_cell(23, 18, SDL_BUTTON_RIGHT);
        check(batch.input.events.empty(), "A core that does not declare map_click: right click does nothing");
        {
            const auto at = screen_of(23, 18);
            pump({ finger(SDL_FINGERDOWN, at.first, at.second), finger(SDL_FINGERUP, at.first, at.second) });
            check(batch.input.events.empty(), "A core that does not declare map_click: a tap does nothing");
        }

        ctx.map_click_enabled = true;
        // 左・右・修飾キー。押した所のマスを、離したときに 1 つだけ送る。
        next_frame();
        click_cell(23, 18, SDL_BUTTON_LEFT);
        {
            const auto sent = only_click(23, 18, 1, "Left click sends map_click at the pressed cell");
            check(!sent.shift && !sent.ctrl && !sent.alt, "No modifiers when none is held");
        }
        next_frame();
        click_cell(17, 24, SDL_BUTTON_RIGHT);
        only_click(17, 24, 2, "Right click sends map_click (context menu)");
        next_frame();
        SDL_SetModState(static_cast<SDL_Keymod>(KMOD_RSHIFT | KMOD_LCTRL));
        click_cell(20, 20, SDL_BUTTON_LEFT);
        {
            const auto sent = only_click(20, 20, 1, "The player's own cell can be clicked");
            check(sent.shift && sent.ctrl && !sent.alt, "Shift and Ctrl held at the press are carried (either side key)");
        }
        next_frame();
        SDL_SetModState(static_cast<SDL_Keymod>(KMOD_LALT));
        click_cell(24, 20, SDL_BUTTON_RIGHT);
        {
            const auto sent = only_click(24, 20, 2, "Alt+right click");
            check(!sent.shift && !sent.ctrl && sent.alt, "Alt held at the press is carried");
        }
        SDL_SetModState(KMOD_NONE);

        // 送ったあとは次のフレームが届くまで（または時間切れまで）続けて送らない。
        click_cell(21, 21, SDL_BUTTON_LEFT);
        check(clicks().empty(), "No second click before the next frame arrives");
        next_frame();
        click_cell(21, 21, SDL_BUTTON_LEFT);
        only_click(21, 21, 1, "A click after a new frame is sent");
        click_cell(22, 22, SDL_BUTTON_LEFT);
        check(clicks().empty(), "Held again after sending");
        SDL_Delay(MapClickGate::kReplyTimeoutMs + 60);
        click_cell(22, 22, SDL_BUTTON_LEFT);
        only_click(22, 22, 1, "The gate opens after the timeout even if no frame came (a click that changes nothing)");

        // ドラッグ・中ボタン・指のマウス写し・地図の外は送らない。
        next_frame();
        {
            const auto at = screen_of(25, 20);
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, at.first, at.second), motion(at.first + 30, at.second),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, at.first + 30, at.second) });
            check(clicks().empty(), "Dragging is not a click");
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, at.first, at.second), motion(at.first + 3, at.second + 2),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, at.first + 3, at.second + 2) });
            only_click(25, 20, 1, "A slightly shaky click is still a click");
            next_frame();
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_MIDDLE, at.first, at.second),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_MIDDLE, at.first, at.second) });
            check(clicks().empty(), "The middle button is never sent");
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, at.first, at.second, SDL_TOUCH_MOUSEID),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, at.first, at.second, SDL_TOUCH_MOUSEID) });
            check(clicks().empty(), "The mouse copy of a finger is left to the finger path");
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, at.first, at.second), mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, at.first, at.second),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, at.first, at.second), mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, at.first, at.second) });
            only_click(25, 20, 1, "A second button pressed while the first is held is ignored");
        }
        layout.scene = RectPx{ 100, 50, width - 200, height - 100 };
        next_frame();
        pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 30, 30), mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, 30, 30) });
        check(clicks().empty(), "A click outside the map rectangle is not sent");
        layout.scene = RectPx{ 0, 0, width, height };

        // 送ってはいけない画面。
        next_frame();
        frame.menu_open = true;
        click_cell(23, 18, SDL_BUTTON_LEFT);
        check(clicks().empty(), "No click while a core menu is open");
        click_cell(23, 18, SDL_BUTTON_RIGHT);
        check(clicks().empty(), "No right click while a core menu is open");
        frame.menu_open = false;
        frame.prompt.text_utf8 = "-more-";
        click_cell(23, 18, SDL_BUTTON_LEFT);
        check(clicks().empty(), "No click at a -more- prompt");
        frame.prompt.text_utf8.clear();
        frame.targeting.active = true;
        click_cell(23, 18, SDL_BUTTON_LEFT);
        check(clicks().empty(), "No click while the core is targeting");
        frame.targeting.active = false;
        frame.numeric.active = true;
        click_cell(23, 18, SDL_BUTTON_LEFT);
        check(clicks().empty(), "No click during a number input");
        frame.numeric.active = false;
        frame.text_input_active = true;
        click_cell(23, 18, SDL_BUTTON_LEFT);
        check(clicks().empty(), "No click during a text input");
        frame.text_input_active = false;
        menu.open(settings);
        click_cell(23, 18, SDL_BUTTON_LEFT);
        check(clicks().empty(), "No click while the feature menu is open");
        menu.close();
        fps.set_active(true);
        click_cell(23, 18, SDL_BUTTON_LEFT);
        check(clicks().empty(), "First person keeps the mouse (left = decide)");
        fps.set_active(false);
        frame.awaiting_command = false;
        click_cell(23, 18, SDL_BUTTON_LEFT);
        only_click(23, 18, 1, "A click while the core is busy (a walk in progress) is sent so it stops the walk");
        frame.awaiting_command = true;
        frame.minimap.width = 30;
        frame.minimap.height = 30;
        frame.minimap.kinds.assign(30 * 30, 0);
        next_frame();
        click_cell(23, 18, SDL_BUTTON_LEFT);
        only_click(23, 18, 1, "A cell inside the floor is sent");
        camera.target = Vec3{ 30.5f, 20.5f, 0.f };
        next_frame();
        click_cell(33, 20, SDL_BUTTON_LEFT);
        check(clicks().empty(), "A cell outside the floor is not sent");
        frame.minimap = MinimapSnapshot{};
        camera.target = Vec3{ 20.5f, 20.5f, 0.f };

        // 視点を回した・傾けた・寄せた先でも、絵に見えているマスがそのまま当たる（拾い方はカメラの逆写像）。
        const float yaws[] = { 0.f, 0.7853982f, 1.5707964f, 3.1415927f, 5.4977871f };
        const float pitches[] = { 0.35f, 0.8f, 1.25f };
        const std::pair<int, int> probe_cells[] = { { 23, 18 }, { 17, 22 }, { 20, 24 }, { 22, 20 }, { 19, 17 }, { 24, 23 } };
        int rotated_failures = 0;
        int rotated_cases = 0;
        for (const float yaw : yaws) {
            for (const float pitch : pitches) {
                camera.yaw = yaw;
                camera.pitch = pitch;
                for (const auto &cell : probe_cells) {
                    const auto at = screen_of(cell.first, cell.second);
                    if (!layout.scene.contains(at.first, at.second)) {
                        continue; //!< 画面の外へ出るマスは押せない（検査の対象外）
                    }
                    next_frame();
                    click_cell(cell.first, cell.second, SDL_BUTTON_LEFT);
                    const auto found = clicks();
                    ++rotated_cases;
                    if (found.size() != 1 || found[0].grid_x != cell.first || found[0].grid_y != cell.second) {
                        ++rotated_failures;
                    }
                }
            }
        }
        check(rotated_failures == 0 && rotated_cases >= 80, "Every visible cell is hit at any turn and tilt (5 turns x 3 tilts x 6 cells)");
        camera.yaw = 0.f;
        camera.pitch = 0.8f;

        // 立っている実体は、足元より上の点を押してもその実体のマスに当たる（地面へ落とすと 1 マス奥へずれる）。
        std::vector<TargetingEntity> bodies;
        {
            TargetingEntity body;
            body.id = 5;
            body.gx = 22;
            body.gy = 21;
            body.x = 22.5f;
            body.y = 21.5f;
            body.z = 0.f;
            body.width = 1.f;
            body.height = 1.2f;
            bodies.push_back(body);
        }
        ctx.targeting_entities = bodies;
        {
            const auto upper = screen_of(22, 21, 0.9f);
            const float lx = static_cast<float>(upper.first - layout.scene.x) + 0.5f;
            const float ly = static_cast<float>(upper.second - layout.scene.y) + 0.5f;
            Vec3 ground{};
            const bool hit_ground = camera.unproject_to_plane(lx, ly, 0.f, ground);
            check(hit_ground && static_cast<int>(std::floor(ground.y)) != 21, "The upper body of a monster points at the cell behind it on the ground");
            next_frame();
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, upper.first, upper.second),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, upper.first, upper.second) });
            only_click(22, 21, 2, "The upper body of a monster hits the monster's cell");
        }
        ctx.targeting_entities = {};

        // 2D アスキーの地図は字の格子から拾う。
        settings.main_panel = Hd2dSettings::MainPanel::Ascii;
        hud.targeting.map_projection = TargetingMapProjection{ 0, 0, 80, 25, 16, 32, 20, 20 };
        next_frame();
        pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, (16 * 45) + 5, (32 * 10) + 5),
            mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, (16 * 45) + 5, (32 * 10) + 5) });
        only_click(25, 18, 1, "The 2D ASCII panel picks from its character grid");
        settings.main_panel = Hd2dSettings::MainPanel::Hd2d;

        // 指。短いタップは左・動かさない長押しは右（押し続けて時間が来た瞬間に 1 回）・動かしたら何も送らない。
        {
            const auto at = screen_of(24, 22);
            next_frame();
            pump({ finger(SDL_FINGERDOWN, at.first, at.second), finger(SDL_FINGERUP, at.first, at.second) });
            only_click(24, 22, 1, "A short tap is a left click");
            next_frame();
            pump({ finger(SDL_FINGERDOWN, at.first, at.second) });
            check(clicks().empty(), "Nothing is sent while the finger is still down");
            SDL_Delay(MapClickGesture::kLongPressMs + 80);
            pump({});
            only_click(24, 22, 2, "Holding still long enough is a right click, sent without waiting for the release");
            pump({ finger(SDL_FINGERUP, at.first, at.second) });
            check(clicks().empty(), "The release after a long press sends nothing more");
            next_frame();
            pump({ finger(SDL_FINGERDOWN, at.first, at.second), finger(SDL_FINGERMOTION, at.first + 40, at.second),
                finger(SDL_FINGERUP, at.first + 40, at.second) });
            check(clicks().empty(), "A dragging finger is not a tap");
            pump({ finger(SDL_FINGERDOWN, at.first, at.second), finger(SDL_FINGERMOTION, at.first + 40, at.second) });
            SDL_Delay(MapClickGesture::kLongPressMs + 80);
            pump({});
            check(clicks().empty(), "A dragging finger does not become a long press");
            pump({ finger(SDL_FINGERUP, at.first + 40, at.second) });
            next_frame();
            pump({ finger(SDL_FINGERDOWN, at.first, at.second, 1), finger(SDL_FINGERDOWN, at.first + 90, at.second, 2),
                finger(SDL_FINGERUP, at.first, at.second, 1), finger(SDL_FINGERUP, at.first + 90, at.second, 2) });
            check(clicks().empty(), "Two fingers are not a tap");
            next_frame();
            pump({ finger(SDL_FINGERDOWN, at.first, at.second), finger(SDL_FINGERUP, at.first, at.second) });
            only_click(24, 22, 1, "A tap works again after a two-finger touch");
            next_frame();
            const auto edge = std::pair<int, int>{ 5, 5 };
            layout.scene = RectPx{ 100, 50, width - 200, height - 100 };
            pump({ finger(SDL_FINGERDOWN, edge.first, edge.second), finger(SDL_FINGERUP, edge.first, edge.second) });
            check(clicks().empty(), "A tap outside the map rectangle is not sent");
            layout.scene = RectPx{ 0, 0, width, height };
            SDL_Event lost{};
            lost.type = SDL_WINDOWEVENT;
            lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
            pump({ finger(SDL_FINGERDOWN, at.first, at.second), lost, finger(SDL_FINGERUP, at.first, at.second) });
            check(clicks().empty(), "Losing the window focus forgets a press in progress");
            frame.menu_open = true;
            next_frame();
            pump({ finger(SDL_FINGERDOWN, at.first, at.second), finger(SDL_FINGERUP, at.first, at.second) });
            check(clicks().empty(), "No tap while a core menu is open");
            frame.menu_open = false;
        }
        // パッドを出しているとき、ボタンの上は取られ、すぐ近くの指は地図のタップにしない。遠い指は地図へ。
        settings.vpad.show = true;
        {
            const int pad_a_x = static_cast<int>(0.673f * static_cast<float>(width));
            const int pad_a_y = static_cast<int>(0.892f * static_cast<float>(height));
            next_frame();
            pump({ finger(SDL_FINGERDOWN, pad_a_x, pad_a_y), finger(SDL_FINGERUP, pad_a_x, pad_a_y) });
            check(clicks().empty(), "A finger on a pad button is the pad's");
            pump({ finger(SDL_FINGERDOWN, pad_a_x + 70, pad_a_y), finger(SDL_FINGERUP, pad_a_x + 70, pad_a_y) });
            check(clicks().empty(), "A finger just beside a pad button is not a map tap");
            // 修-12：パッドを出している間は、パッドの部品への操作だけを受け付ける。
            // パッドの外（地図の上）への指・マウスの押し・ホイールは捨てる。パッドの部品は働く。
            const auto middle = screen_of(21, 19);
            next_frame();
            pump({ finger(SDL_FINGERDOWN, middle.first, middle.second), finger(SDL_FINGERUP, middle.first, middle.second) });
            check(clicks().empty() && batch.input.events.empty(), "Pad shown: a finger outside the pad is dropped");
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, middle.first, middle.second),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, middle.first, middle.second) });
            check(clicks().empty() && batch.input.events.empty(), "Pad shown: a left click outside the pad is dropped");
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, middle.first, middle.second),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, middle.first, middle.second) });
            check(clicks().empty() && batch.input.events.empty(), "Pad shown: a right click outside the pad is dropped");
            pump({ mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, middle.first, middle.second, SDL_TOUCH_MOUSEID),
                mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, middle.first, middle.second, SDL_TOUCH_MOUSEID) });
            check(clicks().empty() && batch.input.events.empty(), "Pad shown: the mouse mirror of a finger outside the pad is dropped");
            {
                SDL_Event wheel{};
                wheel.type = SDL_MOUSEWHEEL;
                wheel.wheel.y = 1;
                const float cell_before = settings.camera_cell_px;
                pump({ wheel });
                check(settings.camera_cell_px == cell_before && batch.input.events.empty(),
                    "Pad shown: the mouse wheel is dropped");
                pump({ motion(middle.first, middle.second) });
                check(batch.input.events.empty(), "Pad shown: mouse movement outside the pad does nothing");
            }
            // パッドの部品の上の指は、今までどおりパッドが食う（A の押しがパッドに溜まる）。
            {
                PadPress press{};
                while (vpad.take_pressed(press)) { }
                pump({ finger(SDL_FINGERDOWN, pad_a_x, pad_a_y), finger(SDL_FINGERUP, pad_a_x, pad_a_y) });
                check(vpad.take_pressed(press), "Pad shown: a finger on a pad button still works");
            }
        }
        settings.vpad.show = false;
        // パッドを隠すと、指もマウスも今までどおり地図で働く。
        {
            const auto middle = screen_of(21, 19);
            next_frame();
            pump({ finger(SDL_FINGERDOWN, middle.first, middle.second), finger(SDL_FINGERUP, middle.first, middle.second) });
            only_click(21, 19, 1, "Pad hidden: a finger is a map tap again");
            next_frame();
            click_cell(23, 18, SDL_BUTTON_RIGHT);
            only_click(23, 18, 2, "Pad hidden: a right click is a map click again");
            SDL_Event wheel{};
            wheel.type = SDL_MOUSEWHEEL;
            wheel.wheel.y = 1;
            const float cell_before = settings.camera_cell_px;
            pump({ wheel });
            check(settings.camera_cell_px != cell_before, "Pad hidden: the mouse wheel zooms again");
        }
        ctx.map_click_enabled = false;
        frame = GameFrame{};
        camera = Camera{};
        layout = UiLayout{};
        SDL_SetModState(KMOD_NONE);
    }
    SDL_Quit();
    std::fprintf(stderr, "[sdl-input-check] %d cases, %d failures\n", cases, failures);
    return failures == 0 ? 0 : 1;
}


int run_core_link_check()
{
#if defined(_WIN32)
    DWORD before = 0, after = 0;
    if (!::GetProcessHandleCount(::GetCurrentProcess(), &before)) { return 1; }
    int failures = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        CoreLink link;
        CoreLinkOptions options;
        options.core_path = "__missing_core_for_check__.exe";
        std::string err;
        if (link.start(options, err)) { ++failures; }
        link.shutdown();
        link.shutdown();
    }
    if (!::GetProcessHandleCount(::GetCurrentProcess(), &after) || after > before) {
        ++failures;
    }
    /*
     * 握手してすぐ閉じるだけでも、Rogue 3.6・5.4 のコアは閉じるときに自動でセーブを書く。データの置き場
     * （HENGBAND_DATA_ROOT）が無いとコアは実行ファイルの横に書くので、開発ツリーで直接流すと開発ツリーの
     * rogue36/rogue.save・rogue54/rogue.save を書き換えた（2026-10-15）。置き場が無ければ一時フォルダを作って向ける。
     */
    if (const char *root = std::getenv("HENGBAND_DATA_ROOT"); (root == nullptr) || (*root == '\0')) {
        std::error_code ec;
        const auto temp_root = std::filesystem::temp_directory_path(ec) / ("hd2d-core-link-check-" + std::to_string(::GetCurrentProcessId()));
        std::filesystem::create_directories(temp_root, ec);
        if (ec || (::_putenv_s("HENGBAND_DATA_ROOT", temp_root.string().c_str()) != 0)) {
            std::fprintf(stderr, "[core-link-check] 一時のデータの置き場を用意できません: %s\n", temp_root.string().c_str());
            return 1;
        }
        std::fprintf(stderr, "[core-link-check] HENGBAND_DATA_ROOT=%s\n", temp_root.string().c_str());
    }
    bool warmed_up = false;
    for (const char *name : {"HengbandCore.exe", "TangbandCore.exe", "GensobandCore.exe", "TobandCore.exe", "SilCore.exe", "FroxCore.exe", "Rogue2Core.exe", "Rogue36Core.exe", "Rogue54Core.exe"}) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            CoreLink link;
            CoreLinkOptions options;
            options.core_path = name;
            std::string err;
            if (!link.start(options, err) || !link.handshake(err)) {
                std::fprintf(stderr, "[core-link-check] %s: %s\n", name, err.c_str());
                ++failures;
            } else {
                link.begin_receiving();
            }
            link.shutdown();
            if (!warmed_up) {
                // CRT が最初の std::thread で保持する共通資源を基準へ含める。
                if (!::GetProcessHandleCount(::GetCurrentProcess(), &before)) { ++failures; }
                warmed_up = true;
            }
        }
    }
    if (!::GetProcessHandleCount(::GetCurrentProcess(), &after) || after > before) {
        std::fprintf(stderr, "[core-link-check] handles before=%lu after=%lu\n", before, after);
        ++failures;
    }
    std::fprintf(stderr, "[core-link-check] 20 failed starts, 6 cores x 2 sessions, %d failures\n", failures);
    return failures == 0 ? 0 : 1;
#else
    std::fprintf(stderr, "[core-link-check] Windows process lifecycle check only\n");
    return 1;
#endif
}

} // namespace hd2d
