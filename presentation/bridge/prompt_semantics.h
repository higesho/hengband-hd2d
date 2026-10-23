#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace presentation {

enum class CommandPromptKind { None, Look, VerifyAt, VerifyYesNo, Force };

struct CommandPromptSemantics {
    CommandPromptKind kind{ CommandPromptKind::None };
    std::vector<int> keys;
    /*!
     * 見る・狙うの行が Term の幅で切れ、終わりの操作の案内が落ちていた（`look_row_was_cut()`）。
     * このとき `keys` は行から読んだものではなく、見る `l` の既定の操作（`+` `-` `p` `o`、敵の行は `t` も）。
     * 帯の説明に使う字は `look_row_cut_subject()` で取る（最後の `[` の手前で切ると、頭の `[<経験値>]` や
     * アイテムの `[4,+0]` を説明から落とす）。
     */
    bool look_row_cut{ false };
};

/*!
 * 見る・狙うの行の頭の語の組。コアごとに頭の語が違う。
 * - Hengband: 変愚蛮怒・短愚蛮怒・幻想蛮怒・TOband。`Target:`／`ターゲット:`（敵の行は直前に `[<経験値>]`）。
 *   ほかの問いや伝言にこの頭は無い。
 * - Frox: FroxComposband。`Target: `・`Feature: `・`Monster: `・`Position: `（`xtra2.c` の `target_set_aux()` が
 *   操作の案内の中身で選ぶ）。日本語でも頭は英語のまま（コードの `s1` は訳を通らない）。
 * - SilQ: Sil-Q。`You see `・`You are `・`It is `・`She is `・`He is `・`They are `。頭が普通の伝言と同じ書き出しなので、
 *   最小の幅を高くしてある（`look_row_min_width()`）。
 * Angband（見る行に操作の案内を持たず、`<dir>` も出さない）・Umoria・Rogue 系は行 0 の見る行を使わないので対象外。
 */
enum class LookRowHeads { Hengband, Frox, SilQ };

/*! @brief 表示の幅（桁数）。全角は 2 桁、半角カナ（U+FF61〜FF9F）は 1 桁。UTF-8 の字を読む。 */
inline int look_row_display_width(std::string_view text)
{
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++width;
            ++i;
        } else if (c >= 0xF0) {
            width += 2;
            i += 4;
        } else if (c >= 0xE0) {
            const auto byte_at = [&](std::size_t k) { return static_cast<unsigned char>(text[std::min(k, text.size() - 1)]); };
            const unsigned cp = ((c & 0x0Fu) << 12) | ((byte_at(i + 1) & 0x3Fu) << 6) | (byte_at(i + 2) & 0x3Fu);
            width += ((cp >= 0xFF61u) && (cp <= 0xFF9Fu)) ? 1 : 2;
            i += 3;
        } else {
            ++width;
            i += 2;
        }
    }
    return width;
}

/*!
 * @brief 切れた見る行と見なす、表示の幅の下限。
 * @details 切れた行は桁の端（80 桁）まで字が詰まっている。短い行を誤って拾わないための下限。
 * 頭の語がほかの伝言と同じ書き出しになる Sil-Q は、普通の伝言が超えない幅（`msg_print()` は 72 桁で折り返す）まで上げる。
 */
inline int look_row_min_width(LookRowHeads heads)
{
    return heads == LookRowHeads::SilQ ? 78 : 70;
}

/*!
 * @brief 見る・狙うの行が Term の幅（80 桁）で切れ、終わりの操作の案内（`<dir>`・`q止` など）が落ちた行か。
 * @details 原作の `target_set_aux()` は、行 0 に「<頭><名前> … [<操作の案内>]」を出す。名前が長い敵・アイテム・地形では案内が
 * 80 桁の外へ出て落ちる（英語は名前が 26 字ほどから。日本語は全角 8 字ほどから）。落ちた行は `<dir>`／`q止` を含まないので、
 * 案内を手がかりにした判定では見る帯と見分けられず、フレームに帯が載らなかった（パッドの案内が消える）。
 * 条件は次の 4 つ。(1) 頭が `LookRowHeads` の語のどれか。(2) 表示の幅が `look_row_min_width()` 以上。
 * (3) `-more-`／`-続く-` で終わらない（続きを待つ伝言の行は含めない）。
 * (4) 案内の終わりが残っていない。`Hengband` と `Frox` は `<dir>` を含む行（日本語の `Hengband` は `-前` も）を除く。
 * 案内まで全部が収まった行は、幅が 70 桁以上でも切れた行ではなく、案内から操作を読める。案内の一部（`<di`・`+次`）が
 * 残った行は切れた行に数える。`SilQ` は案内が日本語に訳されるので、この道具では見ない（呼ぶ側が、案内を読める行を先に外す）。
 * 呼ぶ側は、行の終わりの空白を取っておくこと。
 */
inline bool look_row_was_cut(std::string_view text, LookRowHeads heads = LookRowHeads::Hengband)
{
    const auto ends_with = [&](std::string_view tail) {
        return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail;
    };
    if (ends_with("-more-") || ends_with(reinterpret_cast<const char *>(u8"-続く-"))) {
        return false;
    }
    const auto contains = [&](std::string_view part) { return text.find(part) != std::string_view::npos; };
    if ((heads != LookRowHeads::SilQ) && contains("<dir>")) {
        return false;
    }
    if ((heads == LookRowHeads::Hengband) && contains(reinterpret_cast<const char *>(u8"-前"))) {
        return false;
    }
    std::size_t at = 0;
    if (heads == LookRowHeads::Hengband && !text.empty() && text[0] == '[') {
        const std::size_t close = text.find(']');
        if (close == std::string_view::npos || close > 9) {
            return false;
        }
        at = close + 1; // 敵の行の頭の `[<経験値>]`
    }
    const auto has_head = [&](std::string_view head) { return text.substr(at, head.size()) == head; };
    bool matched = false;
    switch (heads) {
    case LookRowHeads::Hengband:
        matched = has_head("Target:") || has_head(reinterpret_cast<const char *>(u8"ターゲット:"));
        break;
    case LookRowHeads::Frox:
        matched = has_head("Target: ") || has_head("Feature: ") || has_head("Monster: ") || has_head("Position: ");
        break;
    case LookRowHeads::SilQ:
        matched = has_head("You see ") || has_head("You are ") || has_head("It is ") || has_head("She is ") || has_head("He is ")
            || has_head("They are ");
        break;
    }
    return matched && look_row_display_width(text) >= look_row_min_width(heads);
}

/*! @brief 切れた見る行が敵の行か（`Hengband` の頭の組だけ。頭の前に `[<経験値>]` が付く）。敵の行だけ `t`（ターゲットにする）が使える。 */
inline bool look_row_is_monster(std::string_view text)
{
    return !text.empty() && text[0] == '[';
}

/*!
 * @brief 切れた見る行から、見ている物の説明（帯の見出しの後ろへ残す字）を取る。
 * @details 最後の `[` が閉じていなければ（`]` が無い）、切れた操作の案内の頭なので、その手前までが説明。
 * 閉じていれば（頭の `[<経験値>]`・アイテムの `[4,+0]`）説明の一部なので全部を使う。
 * 続きの待ちの印（`-more-`）や前後の空白を取るのは呼ぶ側。
 */
inline std::string_view look_row_cut_subject(std::string_view text)
{
    const std::size_t open = text.rfind('[');
    if (open != std::string_view::npos && text.find(']', open) == std::string_view::npos) {
        return text.substr(0, open);
    }
    return text;
}

inline CommandPromptSemantics analyze_command_prompt(std::string_view text)
{
    CommandPromptSemantics out;
    const auto has = [&](std::string_view value) { return text.find(value) != std::string_view::npos; };
    /*
     * 名前が長くて案内が 80 桁の外へ落ちた見る行（変愚蛮怒・短愚蛮怒）。案内の一部（`q止 t決 p自 o現 +次`）だけが残った行も
     * ここで受ける（下の通常の判定は残った字だけから操作を読み、`-` などが抜ける。幻想蛮怒・TOband の `parse_prompt_line()` と同じ扱い）。ほかの問いの判定より先に見る。
     * 案内の中身は読めないので、見る `l` の操作はどれも出す（幻想蛮怒・TOband の `parse_prompt_line()` と同じ選び方）。
     */
    if (look_row_was_cut(text, LookRowHeads::Hengband)) {
        out.kind = CommandPromptKind::Look;
        out.look_row_cut = true;
        out.keys = { '+', '-', 'p', 'o' };
        if (look_row_is_monster(text)) out.keys.push_back('t');
        return out;
    }
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
