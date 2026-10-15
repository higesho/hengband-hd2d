#pragma once

#include <string_view>
#include <vector>

namespace presentation {

enum class CommandPromptKind { None, Look, VerifyAt, VerifyYesNo, Force };

struct CommandPromptSemantics {
    CommandPromptKind kind{ CommandPromptKind::None };
    std::vector<int> keys;
};

inline CommandPromptSemantics analyze_command_prompt(std::string_view text)
{
    CommandPromptSemantics out;
    const auto has = [&](std::string_view value) { return text.find(value) != std::string_view::npos; };
    if ((has("<dir>") || has(reinterpret_cast<const char *>(u8"q止"))) && has("p") && (has("+") || has("-"))) {
        out.kind = CommandPromptKind::Look;
        if (has("+")) out.keys.push_back('+');
        if (has("-")) out.keys.push_back('-');
        out.keys.push_back('p');
        if (has("m,") || has(reinterpret_cast<const char *>(u8"m近"))) out.keys.push_back('m');
        if (has("o,") || has(reinterpret_cast<const char *>(u8"o現"))) out.keys.push_back('o');
        if (has("g") || has(reinterpret_cast<const char *>(u8"g移"))) out.keys.push_back('g');
        if (has("t")) out.keys.push_back('t');
        return out;
    }
    if (has("Please verify SUICIDE by typing the '@' sign")
        || (has("'@'") && has(reinterpret_cast<const char *>(u8"確認")))
        /*
         * AH-10: Angband の生年退場の確認
         * （`ui-command.c` の `textui_cmd_retire()`）は文言が
         * 「Please verify RETIRING THIS CHARACTER by typing the '@' sign: 」で、
         * 上の 2 つの完全一致にも掛からない。**「'@' sign」という言い回しは
         * このコアツリー全体で「'@' を打って確認する」画面にしか出てこない**
         * （SUICIDE／ABORTING の文言のどちらも含む。ほかの言い回しの言及は無い。
         * 2026-09-24 に `grep -r "'@' sign"` で全数確認済み）ので、ここへ足しても
         * 既存の 2 条件の判定が真になっていた行が偽に変わることは無い
         * （OR で足しただけ）。
         */
        || has("'@' sign")) {
        out.kind = CommandPromptKind::VerifyAt;
        out.keys = { '@', 27 };
        return out;
    }
    if ((has("Are you sure? (") || has(reinterpret_cast<const char *>(u8"本当によろしいですか？ ("))) && !has("[y/")) {
        out.kind = CommandPromptKind::VerifyYesNo;
        out.keys = { 'y', 27 };
        return out;
    }
    if (has("w for the Force") || has(reinterpret_cast<const char *>(u8"'w'練気術"))) {
        out.kind = CommandPromptKind::Force;
        out.keys = { 'w', 27 };
        return out;
    }
    return out;
}

} // namespace presentation
