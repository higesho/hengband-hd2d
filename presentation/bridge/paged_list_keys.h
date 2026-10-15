#pragma once
/*!
 * @file paged_list_keys.h
 * @brief ページ式の一覧のページ送りのキーの申告（変愚・短愚の原作の画面）。
 * @details 関数の説明は下の `fill_paged_list_keys()`。`presentation_bridge.cpp` の `fill_menu_choices()` が呼ぶ。
 * 画面側の検査（`hd2d/app/checks/scroll_list_checks.h`）も同じ関数を、原作の描いた行を模した合成フレームで呼ぶ。
 */
#include "frame/game_frame.h"
#include <string>
namespace presentation {
/*!
 * @brief ページ式の一覧（原作が 1 ページぶんだけ描き、空白や `-` で次・前のページへ送る画面）のページ送りのキーを申告する。
 * @details これらの画面は選択肢（`a)` …）は読めるが、ページを送るキーを新しい UI が知らず、2 ページ目以降の項目へ
 * 届かなかった。申告すれば、画面のページ送り（PageUp/PageDown・LB/RB）が原作のキーを送る
 * （`GameFrame::menu_page_prev_key` / `_next_key`。共通の窓は `modern_window` の `prev_key`/`next_key` へ運ぶ）。
 * 画面の字（原作の案内・見出し）で見分ける——変愚・短愚の同じ原作の画面だけ。
 *  - 施設の選択ほか `util/candidate-selector.h`（最大 15 件で ` -- more (N/M) --`。` ` が次・`-` が前。どちらも回り込む）
 *  - 鍛冶師の持っているエッセンスの一覧 `mind/mind-weaponsmith.cpp`（`現在所持しているエッセンス N/M`。` ` が次・`-` が前）
 *  - 鍛冶師の能力付加 同（`(SPACEで次ページ, ESCで中断) …`。` ` が次だけ。前は無く、端で先頭へ戻る）
 *  - レイシャルパワーのメニューでない形式 `cmd-action/cmd-racial.cpp`（`(特殊能力 a-?, '*'で一覧 …`。` `（`*`）が次だけ）
 * 原作に前のページのキーが無い画面は、次のキーだけを申告する（前は 0 のまま）。
 */
inline void fill_paged_list_keys(GameFrame &frame)
{
    if (frame.menu_page_prev_key || frame.menu_page_next_key) {
        return;
    }
    // `marker` の後ろの `N/M` を読み、M が 2 以上なら true。
    const auto multi_page = [](const std::string &text, const char *marker) {
        const auto at = text.find(marker);
        if (at == std::string::npos) {
            return false;
        }
        size_t i = at + std::char_traits<char>::length(marker);
        while (i < text.size() && text[i] == ' ') {
            ++i;
        }
        size_t j = i;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
            ++j;
        }
        if (j == i || j >= text.size() || text[j] != '/') {
            return false;
        }
        size_t k = j + 1;
        int total = 0;
        while (k < text.size() && text[k] >= '0' && text[k] <= '9') {
            total = total * 10 + (text[k] - '0');
            ++k;
        }
        return total >= 2;
    };
    for (const auto &line : frame.menu_term_lines) {
        const std::string &text = line.text_utf8;
        if (multi_page(text, "-- more (")) {
            frame.menu_page_prev_key = '-';
            frame.menu_page_next_key = ' ';
            return;
        }
        if (multi_page(text, "現在所持しているエッセンス") || multi_page(text, "List of all essences you have.")) {
            frame.menu_page_prev_key = '-';
            frame.menu_page_next_key = ' ';
            return;
        }
        if (text.find("(SPACEで次ページ, ESCで中断)") != std::string::npos || text.find("(SPACE=next, ESC=exit) Add which ability?") != std::string::npos
            || (text.find("(特殊能力 ") != std::string::npos && text.find("どの能力") != std::string::npos)
            || (text.find("(Powers ") != std::string::npos && text.find("which power") != std::string::npos)) {
            frame.menu_page_next_key = ' ';
            return;
        }
    }
}
}
