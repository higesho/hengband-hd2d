#pragma once
#include "app/app_support.h"
#include "bridge/native_modern.h"
#include "frame/frame_codec.h"
#include "frame/frame_codec_check.h"
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
namespace hd2d {
/*!
 * @brief ゲーム終了の画面の旗（`GameFrame::game_over`。修-10）の検査。窓もコアも要らない。
 *
 * @details 確かめること:
 *   - 旗がプロトコルを往復する。旗の無い古いフレームは偽のまま読める（未知・欠けた欄は捨てる／既定の約束）
 *   - `phase`：旗が先（死亡後は誕生ではなく `death`）・タイトルの窓だけが `title`・
 *     `title_screen` が真のままのコア（Frox・幻想蛮怒・Sil-Q）の誕生の窓は `birth`（観-1）
 *   - 窓の題：死亡後は「キャラクター作成」ではなく「ゲームオーバー」。誕生は今までどおり
 *   - 死亡後の窓に、誕生の操作（「作成をやめる」）を付けない
 *   - 文字入力の窓の題が「…してくださいの入力」にならない（観-1）
 * 実行: `HD2D_GAME_OVER_CHECK=1 HengbandHd2d.exe --ui-check`
 */
inline int run_game_over_checks()
{
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const char *label) {
        if (value) {
            ++passed;
        } else {
            ++failed;
            std::fprintf(stderr, "FAIL: %s\n", label);
        }
    };
    const auto ja = [](const char8_t *text) { return std::string(reinterpret_cast<const char *>(text)); };

    // --- プロトコルの往復 ---
    {
        GameFrame f;
        f.pre_game_menu = true;
        f.game_over = true;
        GameFrame decoded;
        std::string err;
        const std::string wire = presentation::encode_game_frame(f);
        check(presentation::decode_game_frame(wire, decoded, err) && decoded.game_over && decoded.pre_game_menu,
            "game_over survives the frame protocol round trip");
        GameFrame plain;
        const std::string plain_wire = presentation::encode_game_frame(plain);
        check(plain_wire.find("game_over") == std::string::npos, "an unset game_over is not sent (old viewers see no new key)");
        GameFrame fresh;
        check(presentation::decode_game_frame(plain_wire, fresh, err) && !fresh.game_over, "a frame without the key reads as game_over=false");
        check(presentation::run_frame_codec_smoke() == 0, "complete frame protocol smoke checks (includes game_over)");
    }

    // --- phase ---
    {
        const GameFrame idle;
        check(std::string(ui_probe_phase(idle)) == "game", "phase: ordinary play is game");
        GameFrame title;
        title.pre_game_menu = true;
        title.modern.kind = "dialog";
        title.modern.stage = "choices";
        title.modern.content_type = "start";
        check(std::string(ui_probe_phase(title)) == "title", "phase: the title window is title");
        GameFrame opening;
        opening.title_screen = true;
        check(std::string(ui_probe_phase(opening)) == "title", "phase: the opening (no window yet) is title");
        GameFrame birth;
        birth.pre_game_menu = true;
        birth.modern.kind = "dialog";
        birth.modern.stage = "choices";
        birth.modern.content_type = "character";
        check(std::string(ui_probe_phase(birth)) == "birth", "phase: a creation window is birth");
        GameFrame birth_over_title_screen = birth; //!< Frox・幻想蛮怒・Sil-Q は @ が出来るまで title_screen が真
        birth_over_title_screen.title_screen = true;
        check(std::string(ui_probe_phase(birth_over_title_screen)) == "birth", "phase: a creation window stays birth while title_screen is up (F-gensoband observation 1)");
        GameFrame dead = birth;
        dead.game_over = true;
        check(std::string(ui_probe_phase(dead)) == "death", "phase: the game-over screen is death, not birth");
        dead.title_screen = true;
        dead.modern.content_type = "start";
        check(std::string(ui_probe_phase(dead)) == "death", "phase: game_over wins over every other state");
    }

    // --- 窓の題・操作 ---
    {
        const auto build = [&](bool over, const std::string &line_text, int cursor_row) {
            presentation::NativeModernMenu native;
            native.set_enabled(true);
            native.set_language("ja");
            GameFrame f;
            f.menu_open = true;
            f.pre_game_menu = true;
            f.game_over = over;
            TermMirrorLine line;
            line.source_row = 5;
            line.text_utf8 = line_text;
            line.color = 1;
            f.menu_term_lines.push_back(line);
            f.menu_term_curs_row = cursor_row;
            f.menu_term_curs_col = static_cast<int>(line_text.size());
            MenuChoice choice;
            choice.key = 'a';
            choice.line_index = 0;
            choice.span_begin = 0;
            choice.span_len = 1;
            choice.label_utf8 = ja(u8"記録を残さない");
            f.menu_choices.push_back(choice);
            native.capture(f);
            return f.modern;
        };
        const std::string sentence = ja(u8"あなたは死んだ。");
        const ModernWindow birth = build(false, sentence, -1);
        const ModernWindow over = build(true, sentence, -1);
        check(birth.title == ja(u8"キャラクター作成"), "title: a creation window is still called character creation");
        check(over.title == ja(u8"ゲームオーバー"), "title: the game-over window is not called character creation");
        check(over.content_type != "character", "type: the game-over window is not a character form");
        const auto has_stop = [&](const ModernWindow &w) {
            for (const auto &action : w.actions) {
                if (action.label.find(ja(u8"作成をやめる")) != std::string::npos) return true;
            }
            return false;
        };
        check(!has_stop(over), "actions: the game-over window has no 'stop character creation'");
    }
    {
        //! 問いが文のときに「の入力」を足さない（幻想蛮怒の「セーブファイル名を入力してください:」。観-1）
        presentation::NativeModernMenu native;
        native.set_enabled(true);
        native.set_language("ja");
        GameFrame f;
        f.menu_open = true;
        f.pre_game_menu = true;
        f.text_input_active = true;
        const std::string prompt = ja(u8"セーブファイル名を入力してください: ");
        TermMirrorLine line;
        line.source_row = 0;
        line.text_utf8 = prompt;
        line.color = 1;
        f.menu_term_lines.push_back(line);
        f.menu_term_curs_row = 0;
        f.menu_term_curs_col = static_cast<int>(prompt.size());
        native.capture(f);
        check(f.modern.stage == "text", "precondition: a text-input window");
        check(f.modern.title == ja(u8"セーブファイル名の入力"), "title: a request sentence becomes 'item name + input', not 'sentence + input'");
    }

    {
        //! Umoria の名前の入力：端末には直前の職業の選択の行が残っている。題が「職業の選択」のままにならない（F-umoria-09 の後半）
        presentation::NativeModernMenu native;
        native.set_enabled(true);
        native.set_language("ja");
        GameFrame f;
        f.menu_open = true;
        f.pre_game_menu = true;
        f.text_input_active = true;
        const std::vector<std::pair<int, std::string>> rows = {
            { 2, ja(u8"名前: ") }, { 3, ja(u8"年齢: 19") }, { 4, ja(u8"種族: 人間") }, { 20, "  Choose a class" },
            { 21, "  a) Warrior   b) Mage" }, { 22, "  c) Priest" } };
        for (const auto &row : rows) {
            TermMirrorLine line;
            line.source_row = row.first;
            line.text_utf8 = row.second;
            line.color = 1;
            f.menu_term_lines.push_back(line);
        }
        f.menu_term_curs_row = 0;
        f.menu_term_curs_col = 6;
        //! 端末に残った「a) …」の行は、橋渡しが選択肢として拾う（実機の窓の項目に職業が並んでいた）
        for (const auto &pick : std::vector<std::pair<int, char>>{ { 4, 'a' }, { 4, 'b' }, { 5, 'c' } }) {
            MenuChoice choice;
            choice.key = pick.second;
            choice.line_index = pick.first;
            choice.span_begin = 2;
            choice.span_len = 10;
            choice.key_begin = 2;
            choice.key_len = 1;
            choice.label_utf8 = std::string(1, pick.second);
            f.menu_choices.push_back(choice);
        }
        native.capture(f);
        check(f.modern.stage == "text", "precondition: Umoria name input is a text window");
        check(f.modern.title != ja(u8"職業の選択") && f.modern.title != "Choose a class",
            "title: the name input window does not keep the previous class-selection title");
        check(f.modern.title == ja(u8"名前の入力"), "title: the name input window is called name input");
        if (f.modern.title != ja(u8"名前の入力")) {
            std::fprintf(stderr, "  (title was: %s / label: %s)\n", f.modern.title.c_str(), f.modern.input_label.c_str());
        }
    }

    if (failed != 0) {
        std::fprintf(stderr, "game_over checks: %d passed, %d failed\n", passed, failed);
        return 1;
    }
    std::printf("game_over checks: %d passed\n", passed);
    return 0;
}
} // namespace hd2d
