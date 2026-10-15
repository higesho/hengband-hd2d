#pragma once
#include "app/app_support.h"
#include "frame/message_more.h"
#include <cstdio>
namespace hd2d {
/*!
 * @brief メニューを開いてよいかの規則の検査（修-01・判-5。`top_menu_available()`）。窓もコアも要らない。
 *
 * @details 確かめること:
 *   - 何も待っていないときは開ける
 *   - コアのメニュー・タイトル・文字入力・数の入力・選択肢・HUD の窓・狙い中の各々で開けない
 *   - `-続く-`（日本語）・`-more-`（英語）・`［続く］` が、行 0 のミラーか問いの文の末尾にあるとき開けない（F1・F10・Start・Back の共通の規則）
 *   - 続き待ちでない `-続く-`（店の一覧の 18 行目の頁送り・文章の途中の言及）では開ける（誤って塞がない）
 * 実行: `HD2D_TOP_MENU_CHECK=1 HengbandHd2d.exe --ui-check`
 */
inline int run_top_menu_checks()
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
    const auto mirror = [](GameFrame &f, int row, const char *text) {
        TermMirrorLine line;
        line.source_row = row;
        line.text_utf8 = text;
        line.color = 1;
        f.menu_term_lines.push_back(line);
    };
    const GameFrame idle;
    check(top_menu_available(idle, false, false), "nothing is waiting: the menu can open");
    {
        GameFrame f;
        f.menu_open = true;
        check(!top_menu_available(f, false, false), "core menu open: no top menu");
    }
    {
        GameFrame f;
        f.pre_game_menu = true;
        check(!top_menu_available(f, false, false), "title/birth: no top menu");
    }
    {
        GameFrame f;
        f.text_input_active = true;
        check(!top_menu_available(f, false, false), "text input: no top menu");
    }
    {
        GameFrame f;
        f.numeric.active = true;
        check(!top_menu_available(f, false, false), "number input: no top menu");
    }
    {
        GameFrame f;
        f.prompt.choices = { { "No", 'n', 0, 1 } };
        check(!top_menu_available(f, false, false), "choice prompt: no top menu");
    }
    check(!top_menu_available(idle, true, false), "HUD window open: no top menu");
    check(!top_menu_available(idle, false, true), "HUD targeting: no top menu");
    // -続く-（修-01・判-5）。行 0 のミラー。
    for (const char *marker : { "-続く-", "-more-", "［続く］" }) {
        GameFrame f;
        mirror(f, 0, (std::string("The orc hits you. ") + marker).c_str());
        check(presentation::message_more_pending(f), "precondition: row 0 marker is a message wait");
        check(!top_menu_available(f, false, false), "row 0 -more-: no top menu (Start/F1/F10/Back)");
    }
    // 問いの文の末尾（変愚・短愚が行 0 を持たない形）。
    for (const char *marker : { "-続く-", "-more-", "［続く］" }) {
        GameFrame f;
        f.prompt.text_utf8 = std::string("You hit it. ") + marker;
        check(!top_menu_available(f, false, false), "prompt text -more-: no top menu");
    }
    // 続き待ちでないもの。
    {
        GameFrame f;
        mirror(f, 0, "そのコマンドは店の中では使えません。");
        mirror(f, 18, "-続く-");
        check(top_menu_available(f, false, false), "stock pagination on row 18 is not a message wait");
    }
    {
        GameFrame f;
        mirror(f, 0, "-続く- と出たら Space を押します。");
        check(top_menu_available(f, false, false), "a mention inside prose is not a message wait");
    }
    {
        GameFrame f;
        f.prompt.text_utf8 = "Direction or <click> (Escape to cancel)? ";
        check(top_menu_available(f, false, false), "an ordinary prompt text alone does not block the top menu");
    }
    {
        // 続きを送って行 0 が普通の文に戻れば、また開ける（塞ぎっぱなしにしない）。
        GameFrame f;
        mirror(f, 0, "You hit it. -more-");
        check(!top_menu_available(f, false, false), "while waiting");
        f.menu_term_lines.clear();
        mirror(f, 0, "You killed it.");
        check(top_menu_available(f, false, false), "after the wait is over the top menu opens again");
    }
    // Rogue 系：印が最新のメッセージの末尾にだけ付く。命令待ちの旗は降りている。
    {
        check(core_marks_more_at_message_tail("RogueClone2") && core_marks_more_at_message_tail("Rogue 3.6")
                && core_marks_more_at_message_tail("Rogue 5.4"),
            "Rogue family marks -more- at the message tail");
        check(!core_marks_more_at_message_tail("Hengband") && !core_marks_more_at_message_tail("angband")
                && !core_marks_more_at_message_tail("Umoria"),
            "other cores are not treated as tail-marking");
        GameFrame f;
        f.awaiting_command = false;
        MessageEvent entry;
        entry.text_utf8 = "You hit the bat.［続く］";
        f.messages.push_back(entry);
        check(!top_menu_available(f, false, false, true), "Rogue tail -more-: no top menu (Start/F1/F10/Back)");
        check(top_menu_available(f, false, false, false), "the same frame on a non-Rogue core does not block (history keeps no mark)");
        f.awaiting_command = true;
        check(top_menu_available(f, false, false, true), "Rogue waiting for a command (flag up): the top menu opens");
        f.awaiting_command = false;
        f.messages.back().text_utf8 = "You hit the bat.";
        check(top_menu_available(f, false, false, true), "Rogue message without a mark: the top menu opens");
    }
    std::fprintf(stderr, "Top menu: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
}
