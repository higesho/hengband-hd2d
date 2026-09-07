/*!
 * @file gb_menu.cpp
 * @brief カーソルとプロンプト。
 *
 * 変愚の `presentation_bridge.cpp` にある同名の仕掛けを**幻想蛮怒の文言で建て直した**もの。
 * あちらの実装は共用できない（コアの `game_term` と `_()` を直に触る）が、
 * **やっていることは文字列の照合だけ**なので、判定の理屈はそのまま写せる。
 *
 * ## ここが受けている 2 つの仕組み
 * - **コアのカーソル**（S2 後半）。`use_menu` が真のとき、コアは選択行の頭に `》` を書く。
 *   書き手は 1 か所ではない（コマンドメニュー `util.c:4587`・持ち物・呪文・特技…）が、
 *   **印は全部これ 1 種類**なので、印を探せば「この画面はコアがカーソルを持っている」が分かる。
 * - **プロンプト**（S4）。`[y/n]`（`util.c:3956`）と数値入力（`util.c:4118`）は
 *   `screen_save()` を通らない＝ icky にならないので、**Term ミラーにも出てこない**。
 *   行 0 を直に読まないと、パッドでは「はい」と答える手段が 1 つも無い。
 *
 * ## 判定は緩めない
 * 行 0 はメッセージ行でもある。「`[` と `/` があれば選択肢」のような緩い解釈にすると、
 * 普通のメッセージ表示中にキーを飲み込む（誤コマンドより性質が悪い）。
 * だから**画面に出る文字列そのもの**で引き、行末一致だけを見る。
 */
#include "gb_menu.h"

#include "gb_lang_c.h" //GB: english layer #33 - the prompt-bar labels never reach Term, so #1 cannot see them
#include "gb_shim.h"
#include "gb_text.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace gb {

namespace {

//! Term 1 行の受け皿（主 Term は 80 桁だが余裕を取る）。
constexpr int kRowBytes = 512;

//! コアのカーソルの印。**CP932 `81 74`**（`util.c:4587`）。変愚と同じ字。
const std::string &cursor_mark()
{
    static const std::string mark = []() {
        static const char sjis[] = "》";
        return sjis_to_utf8(sjis, sizeof(sjis) - 1);
    }();
    return mark;
}

//! CP932 のリテラルを 1 度だけ UTF-8 にして使い回す。
const std::string &utf8_of(const char *sjis, std::size_t len)
{
    // 呼び出し側が静的な文字列しか渡さないので、道具箱は道ごとに 1 つで足りる。
    static std::vector<std::pair<const char *, std::string>> cache;
    for (const auto &entry : cache) {
        if (entry.first == sjis) {
            return entry.second;
        }
    }
    cache.emplace_back(sjis, sjis_to_utf8(sjis, len));
    return cache.back().second;
}

/*!
 * @brief 行 0（またはミラーの 1 行）が `y/n` 系のプロンプトなら選択肢へ持ち上げる。
 * @details 表は**画面に出る文字列そのもの**。出どころは順に
 * `util.c:3956`（`[y/n]` / `[Y/n]` / `[(O)k/(C)ancel]`）・`cmd3.c:2309`（`[y/n/Auto]`）・
 * `cmd4.c:6422`（記念撮影）・`cmd5.c:2639`（ペットを放す。頭数が入るので**前方一致**）。
 * 幻想蛮怒と変愚でここは同じ綴りだった（2026-08-20 に実読して照合）。
 */
bool parse_prompt_line(const std::string &raw, PromptBar &out)
{
    out = PromptBar{};

    std::string text = raw;
    while (!text.empty() && (text.back() == ' ')) {
        text.pop_back();
    }
    if (text.empty()) {
        return false;
    }

    /*!
     * ## 札は**日英 2 本持つ**（フック #33。GR-07。2026-08-28）
     *
     * カタログでは引けない。鍵 `いいえ` の訳は**設定の画面の桁合わせ**（`cmd4.c:1779`
     * の `"はい  "` / `"いいえ"` を並べる欄）から機械が抜いたもので **`no ` と桁が付いている**。
     * ここは押す前に読むボタンの札なので `No` が要る。**1 つの鍵に 2 つの意味**を
     * 持たせるわけにいかないので、`xtra1.c` の `bar_en[]`（#31）や
     * `autopick.c` の `KEY_*_EN[]`（#26）と同じ作法で**並びの同じ英語の表**を足す。
     */
    struct Option {
        const char *label_sjis;
        std::size_t label_len;
        const char *label_en; //!< 英語の札。**日本語と同じ並びで置く**
        std::size_t label_en_len;
        char key; //!< 積むキー。**接尾辞の中で最初に現れる位置**へ枠を重ねる
    };
    struct Pattern {
        const char *suffix;
        bool by_prefix; //!< 行末ではなく「この文字列で始まる `[`」を探すか
        Option options[4]; //!< `key == 0` で終端
    };

#define GB_OPT(lit, en, k) { lit, sizeof(lit) - 1, en, sizeof(en) - 1, k }
    static const Pattern kPatterns[] = {
        { "[y/n]", false, { GB_OPT("はい", "Yes", 'y'), GB_OPT("いいえ", "No", 'n') } },
        { "[Y/n]", false, { GB_OPT("はい", "Yes", 'Y'), GB_OPT("いいえ", "No", 'n') } },
        /*!
         * クイックスタートの問い（`birth.c:7892`）。**行 0 ではない**（行 14 に出るので、
         * 下の `fill_row0_prompt()` の走査が拾う）。GU-09 — 変愚の同じ表
         * （`presentation_bridge.cpp` の `kPatterns`）には在ったのに、
         * ここへ写すとき 1 行落ちていた。コアは `y` / `Y` **以外をすべて「いいえ」**
         * として扱う（同 `:7913`）ので、無いとパッドの A で黙って新規作成へ倒れる。
         */
        { "[y/N]", false, { GB_OPT("はい", "Yes", 'y'), GB_OPT("いいえ", "No", 'N') } },
        //! アイテム破壊（`cmd3.c:2309`）。`A` は「以後自動で壊す」（自動拾い登録）。
        { "[y/n/Auto]", false,
            { GB_OPT("はい", "Yes", 'y'), GB_OPT("いいえ", "No", 'n'),
                GB_OPT("以後自動で", "Always", 'A') } },
        { "[(O)k/(C)ancel]", false,
            { GB_OPT("OK", "OK", 'O'), GB_OPT("キャンセル", "Cancel", 'C') } },
        //! 記念撮影（`cmd4.c:6422`）。
        { "[(y)es/(h)tml/(n)o]", false,
            { GB_OPT("テキスト", "Text", 'y'), GB_OPT("HTML", "HTML", 'h'),
                GB_OPT("いいえ", "No", 'n') } },
        //! ペットを放す（`cmd5.c:2639`）。頭数が入るので**前方一致**で引く。
        { "[Yes/No/Unnamed", true,
            { GB_OPT("はい", "Yes", 'Y'), GB_OPT("いいえ", "No", 'N'),
                GB_OPT("名前なしを全部", "Unnamed", 'U') } },
    };
#undef GB_OPT

    for (const auto &pattern : kPatterns) {
        const std::string suffix = pattern.suffix;
        std::size_t suffix_begin = std::string::npos;
        if (pattern.by_prefix) {
            const std::size_t at = text.rfind(suffix);
            //! 前方一致でも**行末が `]`** であることは要る（本文中の言及を拾わない）。
            if ((at != std::string::npos) && (text.back() == ']')) {
                suffix_begin = at;
            }
        } else if ((text.size() >= suffix.size())
            && (text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            suffix_begin = text.size() - suffix.size();
        }
        if (suffix_begin == std::string::npos) {
            continue;
        }

        PromptBar built{};
        bool ok = true;
        for (const auto &option : pattern.options) {
            if (option.key == '\0') {
                break;
            }
            const std::size_t at = text.find(option.key, suffix_begin);
            if (at == std::string::npos) {
                ok = false; // 表の書き間違い。積むより出さない方が安全
                break;
            }
            PromptChoice choice{};
            //! **英語なら英語の表から採る**（見出しの註。GR-07）。ASCII なので変換は素通り。
            const bool en = (gb_lang_enabled() != 0); //GB: english layer #33
            choice.label_utf8 = en
                ? utf8_of(option.label_en, option.label_en_len)
                : utf8_of(option.label_sjis, option.label_len);
            choice.key = option.key;
            choice.begin = static_cast<int>(at);
            choice.len = 1;
            built.choices.push_back(std::move(choice));
        }
        if (!ok || (built.choices.size() < 2)) {
            continue;
        }
        built.text_utf8 = text;
        out = built;
        return true;
    }
    return false;
}

/*!
 * @brief 行 0 が数値入力（`いくつですか (1-4): 1`）なら最小・最大・現在値を読む。
 * @details 判定は **`(数字-数字): ` があり、その後ろが数字だけ（または空）**であること。
 * `get_quantity()`（`util.c:4120`）が `いくつですか (1-%d): ` を出し、
 * 編集中の数字がその後ろに並ぶ。アイテム選択の `(持ち物:b-c, ...)` は
 * **括弧の中が英字**なので当たらない。
 *
 * ## `(MAX:n)` も数値入力である（GU-11。2026-08-24 の実機確認で見つけた）
 * 賭け試合の賭け口数は `get_quantity("「一口$1000だよ。何口賭けるね？」(MAX:%d)", max)`
 * （`bldg2.c:1870`）——**プロンプトが独自の書式**なので上の判定に当たらず、
 * 桁カーソルが出ないままだった。中身は同じ askfor（既定値 1・Enter で確定）なので、
 * この書式も**最小 1・最大 n の数値入力**として持ち上げる。編集中の数字は `)` の
 * **直後**に並ぶ（`: ` が無い）。この書式を書くのはコア全体でこの 1 か所だけである
 * （2026-08-24 に `(MAX:` を全数 grep して確認）。
 */
bool parse_numeric_input_line(const std::string &raw, NumericInput &out)
{
    out = NumericInput{};

    std::string text = raw;
    while (!text.empty() && (text.back() == ' ')) {
        text.pop_back();
    }
    if (text.empty()) {
        return false;
    }

    //! `(MAX:n)` の道（見出しの註）。`)` の直後から編集中の数字が始まる。
    const std::size_t max_at = text.rfind("(MAX:");
    if (max_at != std::string::npos) {
        const std::size_t close = text.find(')', max_at + 5);
        if (close != std::string::npos) {
            const std::string hi = text.substr(max_at + 5, close - max_at - 5);
            const std::string tail = text.substr(close + 1);
            const auto digits_only = [](const std::string &s) {
                return !s.empty()
                    && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0') && (c <= '9'); });
            };
            if (digits_only(hi) && (tail.empty() || digits_only(tail))) {
                out.active = true;
                out.prompt_utf8 = text.substr(0, close + 1);
                out.min = 1;
                out.max = std::atoi(hi.c_str());
                out.value = tail.empty() ? out.min : std::atoi(tail.c_str());
                out.text_len = static_cast<int>(tail.size());
                out.digits = static_cast<int>(hi.size());
                out.value_begin = static_cast<int>(close + 1);
                out.value_len = static_cast<int>(tail.size());
                return true;
            }
        }
    }

    const std::size_t close = text.rfind("): ");
    if (close == std::string::npos) {
        return false;
    }
    const std::size_t open = text.rfind('(', close);
    if (open == std::string::npos) {
        return false;
    }
    const std::string inside = text.substr(open + 1, close - open - 1);
    const std::size_t dash = inside.find('-');
    if ((dash == std::string::npos) || (dash == 0) || (dash + 1 >= inside.size())) {
        return false;
    }
    const std::string lo = inside.substr(0, dash);
    const std::string hi = inside.substr(dash + 1);
    const auto all_digits = [](const std::string &s) {
        if (s.empty()) {
            return false;
        }
        return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0') && (c <= '9'); });
    };
    if (!all_digits(lo) || !all_digits(hi)) {
        return false;
    }

    const std::string tail = text.substr(close + 3);
    if (!tail.empty() && !all_digits(tail)) {
        return false; // `): ` の後ろに文字がある＝数値入力ではない
    }

    out.active = true;
    out.prompt_utf8 = text.substr(0, close + 3);
    out.min = std::atoi(lo.c_str());
    out.max = std::atoi(hi.c_str());
    out.value = tail.empty() ? out.min : std::atoi(tail.c_str());
    out.text_len = static_cast<int>(tail.size());
    out.digits = static_cast<int>(hi.size());
    //! 値の位置（ミラーに出ている画面で、その場の数字へ桁カーソルを重ねるのに使う）。
    out.value_begin = static_cast<int>(close + 3);
    out.value_len = static_cast<int>(tail.size());
    return true;
}

//! 主 Term の 1 行を UTF-8 で読む（制御文字は空白に倒す。`gb_frame.cpp` と同じ規則）。
std::string term_row_utf8(int y)
{
    char text[kRowBytes];
    const int n = gb_term_row(y, text, nullptr, kRowBytes);
    if (n <= 0) {
        return {};
    }
    std::string sjis;
    sjis.reserve(static_cast<std::size_t>(n));
    for (int x = 0; x < n; ++x) {
        const auto u = static_cast<unsigned char>(text[x]);
        sjis.push_back(((u < 0x20) || (u == 0x7F)) ? ' ' : text[x]);
    }
    return sjis_to_utf8(sjis.data(), sjis.size());
}

} // namespace

/* ==================================== 店・建物の選択肢（R3。その2） */

/*!
 * @name `a)` の走査
 *
 * 店と建物の画面は**コアがカーソルを持たない**（`》` を書かない）。だから画面側が
 * 選択肢を数え上げてやらないと、パッドでもクリックでも「商品を買う」を選べない
 * ——キーボードで `p` を打つ以外に道が無い。
 *
 * 理屈は変愚の `presentation_bridge.cpp`（`scan_choice_tokens` 一式）から写した。
 * あちらは**文字列の照合しかしていない**ので理屈はそのまま通る。文言は
 * 幻想蛮怒の `src/` を実読し、実機の Term ミラーで確かめた（2026-08-21）:
 *
 * | 画面 | 目印 | 出どころ |
 * | --- | --- | --- |
 * | 店 | ` ESC) 建物から出る` ＋ `コマンド:` | `store.c:6500` / `:6595` |
 * | 建物 | ` ESC) 建物を出る` ＋ `手持ちのお金:` | `bldg.c:257` / `:104` |
 * | 品物選び | 行 0 が `(商品:a-o, ESCで中断) …` | `store.c` の `get_stock` |
 *
 * **判定は緩めない。**「`x)` があれば選択肢」にすると、別の icky 画面（一覧・ヘルプ）の
 * 本文まで拾って押しても効かない選択肢を並べるか、悪くすると別のコマンドを積む。
 * @{
 */

constexpr std::size_t kMenuChoiceMax = 64;

/*!
 * 建物の行動が並ぶ行。`bldg.c:252` が `19 + (i/2)` へ書き、行動は 8 つまで
 * （`types.h:1903` の `act_names[8][30]`）なので 19〜22。`ESC)` の 23 を足して 19〜23。
 */
constexpr int kBuildingChoiceRowMin = 19;
constexpr int kBuildingChoiceRowMax = 23;

bool is_ascii_alnum(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return ((u >= '0') && (u <= '9')) || ((u >= 'a') && (u <= 'z')) || ((u >= 'A') && (u <= 'Z'));
}

//! `)` の直後として妥当か（空白／行末／全角文字）。` -)前ページ` は `)` の後が全角。
bool is_choice_token_terminator(const std::string &text, std::size_t pos)
{
    if (pos >= text.size()) {
        return true;
    }
    const auto u = static_cast<unsigned char>(text[pos]);
    return (u == ' ') || (u >= 0x80);
}

//! 1 つの札 `ESC)` / `x)` / `i/e)` / `-)` / `スペース)`。`keys` は `i/e)` で 2 個になる。
struct ChoiceTokenGroup {
    std::size_t begin{};
    std::vector<std::pair<std::size_t, std::size_t>> key_spans;
    std::vector<int> keys;
    //! `(1)` の書式で読んだ札か（枠を行末まで伸ばすかの判断に使う。`emit_choices_for_line`）。
    bool paren{ false };
};

//! 行頭か空白の直後から始まる札を 1 件読む。@return 読めたら true
bool parse_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
    static const char kSpaceWordSjis[] = "スペース";
    static const std::string kEsc = "ESC";
    static const std::string kSpaceAscii = "SPACE";
    const std::string &space_word = utf8_of(kSpaceWordSjis, sizeof(kSpaceWordSjis) - 1);

    out = ChoiceTokenGroup{};
    out.begin = p;

    const auto accept = [&](std::size_t key_len, int key) {
        if (text.compare(p + key_len, 1, ")") != 0) {
            return false;
        }
        if (!is_choice_token_terminator(text, p + key_len + 1)) {
            return false;
        }
        out.key_spans.emplace_back(p, key_len);
        out.keys.push_back(key);
        return true;
    };

    if ((text.compare(p, kEsc.size(), kEsc) == 0) && accept(kEsc.size(), 0x1B)) {
        return true;
    }
    if ((text.compare(p, space_word.size(), space_word) == 0) && accept(space_word.size(), ' ')) {
        return true;
    }
    /*
     * **日本語版でも `SPACE)` と英字のまま書く画面がある。**知識の一覧がそうで、
     * 店だけを見て `スペース)` しか拾わないと**ページ送りが選べない**
     * （実測で 1 件しか出ず気づいた）。変愚も同じ穴を塞いでいる。
     */
    if ((text.compare(p, kSpaceAscii.size(), kSpaceAscii) == 0) && accept(kSpaceAscii.size(), ' ')) {
        return true;
    }
    //! `i/e)` `w/t)`: 1 つの札に 2 コマンド。キーはそれぞれ独立に積める。
    if ((p + 2 < text.size()) && is_ascii_alnum(text[p]) && (text[p + 1] == '/')
        && is_ascii_alnum(text[p + 2])) {
        if ((text.compare(p + 3, 1, ")") == 0) && is_choice_token_terminator(text, p + 4)) {
            out.key_spans.emplace_back(p, 1);
            out.keys.push_back(static_cast<unsigned char>(text[p]));
            out.key_spans.emplace_back(p + 2, 1);
            out.keys.push_back(static_cast<unsigned char>(text[p + 2]));
            return true;
        }
        return false;
    }
    if ((p < text.size()) && (is_ascii_alnum(text[p]) || (text[p] == '-'))) {
        return accept(1, static_cast<unsigned char>(text[p]));
    }
    return false;
}

std::vector<ChoiceTokenGroup> scan_choice_tokens(const std::string &text)
{
    std::vector<ChoiceTokenGroup> groups;
    for (std::size_t p = 0; p < text.size(); ++p) {
        if ((p != 0) && (text[p - 1] != ' ')) {
            continue;
        }
        ChoiceTokenGroup g;
        if (parse_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
        }
    }
    return groups;
}

/*!
 * @brief 枠を敷く範囲の終わり。次の札の直前まで。ただし**空白 3 連で切る**。
 * @details コアは同じ行の別の桁へ値段や `手持ちのお金:` を置く（`bldg.c:104` は桁 53）。
 * 切らないと ` ESC) 建物を出る` の枠が所持金まで伸びる。札そのものに空白 3 連は出ない
 * （`bldg.c:252` の `" %c) %s %s"` も 1 個ずつ）。
 */
std::size_t choice_span_end(
    const std::string &text, const std::vector<ChoiceTokenGroup> &groups, std::size_t index)
{
    const std::size_t begin = groups[index].begin;
    std::size_t end = (index + 1 < groups.size()) ? groups[index + 1].begin : text.size();
    const std::size_t gap = text.find("   ", begin);
    if ((gap != std::string::npos) && (gap < end)) {
        end = gap;
    }
    while ((end > begin) && ((text[end - 1] == ' ') || (text[end - 1] == ','))) {
        --end;
    }
    return end;
}

/*!
 * @brief 枠を**行末まで**伸ばしてよい行か。
 * @details `choice_span_end` は空白 3 連で枠を切る（同じ行の別の桁に値段や所持金が来るため）。
 * ところが `(1)` 書式の画面は**ラベル自身が空白で桁揃えされている**
 * （`(1)     キー入力     オプション` ／ `(1) 既知の伝説のアイテム        の一覧`）ので、
 * そのままだと枠が `(1)` だけを囲む。**丸括弧の札しか無い行だけ**行末まで伸ばす
 * ——知識の `ESC) 抜ける   SPACE) 次ページ` のような行で伸ばすと、
 * 手前の札の枠が次の札を飲み込む。
 */
bool line_spans_whole(const std::vector<ChoiceTokenGroup> &groups)
{
    if (groups.empty()) {
        return false;
    }
    for (const ChoiceTokenGroup &g : groups) {
        if (!g.paren) {
            return false;
        }
    }
    return true;
}

void emit_choices_for_line(
    GameFrame &frame, std::size_t line_index, const std::vector<ChoiceTokenGroup> &groups)
{
    const std::string &text = frame.menu_term_lines[line_index].text_utf8;
    const bool whole = line_spans_whole(groups);
    std::size_t trimmed = text.size();
    while ((trimmed > 0) && (text[trimmed - 1] == ' ')) {
        --trimmed;
    }
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const ChoiceTokenGroup &g = groups[gi];
        const std::size_t span_end = whole ? trimmed : choice_span_end(text, groups, gi);
        for (std::size_t ki = 0; ki < g.keys.size(); ++ki) {
            if (frame.menu_choices.size() >= kMenuChoiceMax) {
                return;
            }
            MenuChoice choice{};
            choice.line_index = static_cast<int>(line_index);
            choice.span_begin = static_cast<int>(g.begin);
            choice.span_len = static_cast<int>(span_end - g.begin);
            choice.key_begin = static_cast<int>(g.key_spans[ki].first);
            choice.key_len = static_cast<int>(g.key_spans[ki].second);
            choice.key = g.keys[ki];
            frame.menu_choices.push_back(choice);
        }
    }
}

std::size_t first_non_space(const std::string &text)
{
    std::size_t i = 0;
    while ((i < text.size()) && (text[i] == ' ')) {
        ++i;
    }
    return i;
}

bool line_starts_with(const std::string &text, const std::string &needle)
{
    return text.compare(first_non_space(text), needle.size(), needle) == 0;
}

/*!
 * @brief 行 0 が `(商品:a-o, ESCで中断) …` なら選べる文字の範囲を返す。
 * @details **この形以外では文字を積まない。**`i` の持ち物一覧のように「次の 1 キーが
 * そのままコマンドになる」画面で誤って文字を積むと、別のコマンドが走る。
 */
bool parse_letter_range_prompt(const std::string &text, char &lo, char &hi)
{
    if (text.empty() || (text[0] != '(')) {
        return false;
    }
    const std::size_t close = text.find(')', 1);
    const std::size_t limit = (close == std::string::npos) ? text.size() : close;
    for (std::size_t i = 1; i + 2 < limit; ++i) {
        if (text[i + 1] != '-') {
            continue;
        }
        if (!is_ascii_alnum(text[i]) || !is_ascii_alnum(text[i + 2])) {
            continue;
        }
        lo = text[i];
        hi = text[i + 2];
        return true;
    }
    return false;
}

/* ================================ 名指しの選択画面（その3 / c） */

/*!
 * @name コアがカーソルを持たない「名指しの選択画面」
 *
 * 店と建物（R3）は ` ESC) 建物を出る` や `コマンド:` という**手掛かり**で見分けられた。
 * ところが設定や知識の画面にはその手掛かりが無く、`classify_menu_screen()` は
 * `None` を返す——つまり**選択肢が 1 件も作られない**。かといって
 * 「icky 画面の `x)` を全部拾う」に緩めてはいけない（別の画面で誤ったキーを積む）。
 *
 * そこで変愚と同じ作法を採る: **画面ごとに文言・行範囲・待ちの行を明示する。**
 * 表は実機の Term ミラーで 1 画面ずつ測った（2026-08-21）。
 *
 * ## 「降りている」を待ちの行で見分ける
 * これらの画面は、項目を選ぶと**一覧を消さずに**下へプロンプトを書き足してそこで待つ
 * （マクロなら `トリガーキー:` を行 18 へ）。一覧が残っているので、放っておくと
 * **もう効かない項目**が選択肢のまま残り、決定するとその数字が
 * トリガーキーやファイル名に混ざる。
 *
 * 見るのは「その行に字があるか」ではなく**コアのカーソルがどこにあるか**である
 * （字で見ると、下位から戻っても行が消えていないので一覧を殺してしまう）。
 * 一覧の待ちの行を `prompt_row` に書いておき、**そこに居るときだけ**選択肢を作る。
 * 記録の設定だけは降りた先が**行 0**（`内容:` の自由入力）だが、同じ規則で外れる。
 * @{
 */

//! 札の書式。画面ごとに使い分ける。
enum ChoiceTokenStyle : unsigned {
    kStylePlain = 1u << 0, //!< `a) ラベル`（店・建物と同じ）
    kStyleParen = 1u << 1, //!< `(1) ラベル`（設定・知識）
    kStyleBracket = 1u << 2, //!< `[a](x  3.83) ラベル`（賭け試合だけ。GU-11）
};

//! `(1)` `(R)` 形式の札を 1 件読む。**丸括弧の中の 1 文字**がキー。
bool parse_paren_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
    if ((p + 3 > text.size()) || (text[p] != '(')) {
        return false;
    }
    if (!is_ascii_alnum(text[p + 1]) || (text[p + 2] != ')')) {
        return false;
    }
    if (!is_choice_token_terminator(text, p + 3)) {
        return false;
    }
    out = ChoiceTokenGroup{};
    out.begin = p;
    out.paren = true;
    out.key_spans.emplace_back(p + 1, 1);
    /*
     * **見えている字のまま積む。**英字は `toupper` して描かれる（`cmd4.c:2512`）が、
     * コアは `tolower` で照合するので大文字で通る（同 `:2537`）。逆に
     * 詐欺オプションの `switch` は `case 'C':` しか持たない（同 `:2653`）ので、
     * 小文字へ直すと**そこだけ効かなくなる**。
     */
    out.keys.push_back(static_cast<unsigned char>(text[p + 1]));
    return true;
}

/*!
 * @brief `[a]` 形式の札を 1 件読む（GU-11。賭け試合 `battle_mon_gambling()` だけの書式）。
 * @details `bldg2.c:1793` は `[%c](x%3d.%02d) 名前` と書く——`a)` でも `(a)` でもない
 * **第 3 の書式**である。`]` の直後は倍率の `(` なので、終端の検査もそこだけ違う。
 */
bool parse_bracket_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
    if ((p + 3 > text.size()) || (text[p] != '[')) {
        return false;
    }
    if (!is_ascii_alnum(text[p + 1]) || (text[p + 2] != ']')) {
        return false;
    }
    //! `](` が続くこと（`[妹紅]` のような本文中の角括弧を拾わない）。
    if ((p + 3 >= text.size()) || (text[p + 3] != '(')) {
        return false;
    }
    out = ChoiceTokenGroup{};
    out.begin = p;
    out.paren = true; //!< 枠は行末まで伸ばす（`(1)` と同じ扱い。ラベルは倍率の右にある）
    out.key_spans.emplace_back(p + 1, 1);
    out.keys.push_back(static_cast<unsigned char>(text[p + 1]));
    return true;
}

//! 指定した書式の札を 1 行から全部読む。開始位置の条件は `scan_choice_tokens` と同じ。
std::vector<ChoiceTokenGroup> scan_choice_tokens_styled(const std::string &text, unsigned styles)
{
    std::vector<ChoiceTokenGroup> groups;
    for (std::size_t p = 0; p < text.size(); ++p) {
        if ((p != 0) && (text[p - 1] != ' ')) {
            continue;
        }
        ChoiceTokenGroup g;
        if (((styles & kStyleParen) != 0) && parse_paren_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
            continue;
        }
        if (((styles & kStyleBracket) != 0) && parse_bracket_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
            continue;
        }
        if (((styles & kStylePlain) != 0) && parse_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
        }
    }
    return groups;
}

/*!
 * @name ヘルプの画（`files.c` の `show_file()`）
 *
 * @details 2026-08-29 に気づいた「ヘルプの目次等の選択肢でカーソル移動ができない」。
 * 目次は `(a)幻想蛮怒とは？` の丸括弧書式だが、**`kNamedMenus` のどれとも噛み合わない**
 * ので選択肢が 1 件も積まれず、カーソルも枠も出ていなかった（実測で 0 件）。
 *
 * 見分けは**行 0 の見出し**（`files.c:9648` の `caption`）。日英で綴りが違うので両方見る。
 * 拡張子まで見て **`.hlp`（枝分かれの画）のときだけ項目を積む**——`.txt` は本文なので、
 * 文の中の `(1)` を選択肢にすると「押しても効かない札」が並ぶ。
 * @{
 */
//! `[キー:(RET/スペース)↓ (-)↑ (?)ヘルプ (ESC)終了]` の 2 つ。左右をこれへ翻訳するのは画面側。
constexpr int kHelpPagePrevKey = '-';
constexpr int kHelpPageNextKey = ' ';

//! @brief 行 0 がヘルプの見出しか。@param menu_file 枝分かれの画（`.hlp`）なら真
bool help_caption(const std::string &text, bool &menu_file)
{
    static const char kJpHelp[] = "ヘルプ・ファイル'";
    static const char kJpInfo[] = "スポイラー・ファイル'";
    const std::string &jp_help = utf8_of(kJpHelp, sizeof(kJpHelp) - 1);
    const std::string &jp_info = utf8_of(kJpInfo, sizeof(kJpInfo) - 1);
    const bool found = (text.find(jp_help) != std::string::npos)
        || (text.find(jp_info) != std::string::npos)
        || (text.find("Help file '") != std::string::npos)
        || (text.find("Info file '") != std::string::npos);
    if (!found) {
        return false;
    }
    menu_file = (text.find(".hlp'") != std::string::npos);
    return true;
}

/*!
 * @brief ヘルプの項目 `(a)ラベル` を 1 行から読む。@return 読めたら true
 * @details `parse_paren_choice_token()` では読めない——あちらは `)` の直後に
 * 空白か全角を要求するが、ヘルプには `(o)***初心者ガイド***` のように
 * **半角の記号が続く行**がある。鍵も `(?)` を採る（`jhelpinfo.txt` への枝）。
 * 1 行に 1 件だけ——`(p)変愚蛮怒のヘルプファイル(参考)( jhelp.hlp)` の
 * 後ろの丸括弧は項目ではないので、**行頭のものしか見ない**。
 */
bool parse_help_entry(const std::string &text, ChoiceTokenGroup &out)
{
    const std::size_t p = first_non_space(text);
    if (((p + 3) > text.size()) || (text[p] != '(') || (text[p + 2] != ')')) {
        return false;
    }
    const char key = text[p + 1];
    if (!is_ascii_alnum(key) && (key != '?')) {
        return false;
    }
    out = ChoiceTokenGroup{};
    out.begin = p;
    out.paren = true;
    out.key_spans.emplace_back(p + 1, 1);
    out.keys.push_back(static_cast<unsigned char>(key));
    return true;
}

//! @brief ヘルプの画なら選択肢とページのキーを入れる。@return ヘルプの画だったか
bool fill_help_menu(GameFrame &frame)
{
    bool menu_file = false;
    if (!help_caption(frame.menu_term_lines.front().text_utf8, menu_file)) {
        return false;
    }
    //! **本文でも繰れる。**選択肢が 1 つも無い画なので、ここを入れないと
    //! パッドだけの機体からはページを繰る手が 1 つも無い。
    frame.menu_page_prev_key = kHelpPagePrevKey;
    frame.menu_page_next_key = kHelpPageNextKey;
    if (!menu_file) {
        return true;
    }
    //! 行 0 は見出し・最終行は `[キー:…]` の案内。どちらも項目ではない。
    for (std::size_t i = 1; (i + 1) < frame.menu_term_lines.size(); ++i) {
        ChoiceTokenGroup group;
        if (parse_help_entry(frame.menu_term_lines[i].text_utf8, group)) {
            emit_choices_for_line(frame, i, { group });
        }
    }
    return true;
}
/*! @} */

//! 名指しの画面 1 件分の素性。行番号は **Term の行**（ミラーの添字と同じ）。
struct NamedMenuSpec {
    const char *tag; //!< 検査で名前を出すため
    const char *signature; //!< この画面を名指しする文言（CP932。部分一致）
    std::size_t signature_len;
    unsigned styles;
    int row_min;
    int row_max;
    /*!
     * @brief 一覧の入力をコアが待っている行。**カーソルがここに無ければ選択肢を作らない。**
     * @details 下位のやり取りへ降りている印。上の @name の注記を見ること。
     */
    int prompt_row;
};

#define GB_SIG(s) (s), (sizeof(s) - 1)

/*!
 * @details 順に見て**最初に当たったもの**を使う。
 *
 * | tag | コア | 一覧 | 待ち |
 * |---|---|---|---|
 * | `option_root` | `do_cmd_options`（`cmd4.c:2498`） | 3〜19 | 21 |
 * | `macros` | `do_cmd_macros`（同 `:3183`） | 4〜13 | 16 |
 * | `visuals` | `print_visuals_menu`（同 `:3799`） | 3〜13 | 15 |
 * | `colors` | `do_cmd_colors`（同 `:4607`） | 4〜6 | 8 |
 * | `diary` | `do_cmd_diary`（同 `:1122`） | 4〜7 | 18 |
 * | `knowledge` | `do_cmd_knowledge`（同 `:10533`） | 6〜21 | 20 |
 * | `recall_dungeon` | `choose_dungeon`（`spells3.c:1014`） | 1〜19 | 0 |
 * | `tele_town` | `tele_town`（`bldg.c:10979`） | 6〜19 | 0 |
 * | `melee_arena` | 賭け試合（`bldg2.c:1365` `battle_mon_gambling`） | 4〜19 | 0 |
 *
 * @note `option_root` の上限を 19 にしてあるのは、**詐欺オプションの行が出るとき**
 * だけ 19 行目に増えるためである（`p_ptr->noscore` のとき。普段は 18 で終わる）。
 *
 * @note 下の 3 つは 2026-08-24 に**枠を作って測り直した**（GU-11〜13。枠の作りは
 * `tools/gensoband/mk_gu_saves.py`、記録は ）。
 * 初版は読みだけで書いて **2 つとも 1 行足りなかった**——`choose_dungeon` は建物からは
 * `y = 4` で呼ばれ（`bldg.c:14514`）16 本目が行 19、`tele_town` も最後の町が行 19 に出る。
 * **行範囲は「出うる最大」まで取る。**`prompt_row == 0` の条件が別に効いているので、
 * 広げても誤爆しない。
 *
 * @note `melee_arena` の名指しは**建物の主の名**（行 2）である。初版の
 * 「どれに賭けますか」は **`#if 0` の死んだ関数**（`bldg.c:2702` `kakutoujou`）の文言で、
 * 生きている画面には出ない。行 0 の口上は性別で変わる（「やあ兄さん／姐さん」）ので
 * 使えない。札は `[a](x  3.83) 名前` の第 3 の書式（`kStyleBracket`）。
 */
const NamedMenuSpec kNamedMenus[] = {
    { "option_root", GB_SIG("[ オプションの設定 ]"), kStyleParen, 3, 19, 21 },
    { "macros", GB_SIG("[ マクロの設定 ]"), kStyleParen, 4, 13, 16 },
    { "visuals", GB_SIG("[ 画面表示の設定 ]"), kStyleParen, 3, 13, 15 },
    { "colors", GB_SIG("[ カラーの設定 ]"), kStyleParen, 4, 6, 8 },
    { "diary", GB_SIG("[ 記録の設定 ]"), kStyleParen, 4, 7, 18 },
    //! 知識だけは丸括弧の項目と `ESC) 抜ける  SPACE) 次ページ` が同居する。両方の書式で読む。
    { "knowledge", GB_SIG("現在の知識を確認する"), kStyleParen | kStylePlain, 6, 21, 20 },
    { "recall_dungeon", GB_SIG("どのダンジョン"), kStylePlain, 1, 19, 0 },
    { "tele_town", GB_SIG("どこに行きますか"), kStylePlain, 6, 19, 0 },
    { "melee_arena", GB_SIG("雀斑の河童娘"), kStyleBracket, 4, 19, 0 },
};

#undef GB_SIG

//! @brief 表に載っている画面なら選択肢を作る。@return 1 件でも積んだら true
bool collect_named_menu_choices(GameFrame &frame)
{
    /*
     * **自由入力の間は作らない**（GU-11 で見つけた穴）。賭け試合は組を選ぶと
     * `get_quantity()` の askfor（賭け口数）へ降りるが、**カーソルは同じ行 0** に
     * 居るので `prompt_row` では見分けられない。組の一覧は画面に残っているから、
     * ここで作ると **[b] をクリック＝バッファへ `b` が入り、英字は「全部賭ける」**
     * （`util.c:4165` `isalpha(buf[0]) なら amt = max`）。旗で丸ごと外すのが確実で、
     * ほかの名指し画面の一覧の待ちはどれも `inkey()`（旗が立たない）なので巻き添えは無い。
     */
    if (frame.text_input_active) {
        return false;
    }
    for (const NamedMenuSpec &spec : kNamedMenus) {
        const std::string &signature = utf8_of(spec.signature, spec.signature_len);
        bool found = false;
        for (const auto &line : frame.menu_term_lines) {
            if (line.text_utf8.find(signature) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }
        /*
         * **一覧の待ちに居るときだけ**作る。降りている間に作ると、
         * もう効かない項目が選択肢のまま残る（上の @name の注記）。
         */
        if (frame.menu_term_curs_row != spec.prompt_row) {
            return false;
        }
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            const int row = frame.menu_term_lines[i].source_row;
            if ((row < spec.row_min) || (row > spec.row_max)) {
                continue;
            }
            emit_choices_for_line(
                frame, i, scan_choice_tokens_styled(frame.menu_term_lines[i].text_utf8, spec.styles));
        }
        return !frame.menu_choices.empty();
    }
    return false;
}
/*! @} */

//! 画面の種類。**判定順がそのまま優先順**（店は建物の目印を両方持っている）。
enum class MenuScreen {
    None,
    ItemPrompt,
    Store,
    Building,
};

MenuScreen classify_menu_screen(const GameFrame &frame, int &store_command_line)
{
    static const char kCommandSjis[] = "コマンド:";
    static const char kGoldSjis[] = "手持ちのお金:";
    const std::string &command_label = utf8_of(kCommandSjis, sizeof(kCommandSjis) - 1);
    const std::string &gold_label = utf8_of(kGoldSjis, sizeof(kGoldSjis) - 1);

    store_command_line = -1;
    bool has_esc_choice = false;
    bool has_gold = false;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        if (line_starts_with(text, "ESC)")) {
            has_esc_choice = true;
        }
        if (line_starts_with(text, command_label)) {
            store_command_line = static_cast<int>(i);
        }
        if (text.find(gold_label) != std::string::npos) {
            has_gold = true;
        }
    }

    /*
     * 品物選びは店・建物の**上に重ねて**出る（商品一覧はそのまま残る）ので最優先で見る。
     * コアがカーソルを持つ画面は呼ぶ前に弾いてある（`fill_core_cursor`）。
     */
    if (!frame.menu_term_lines.empty() && (frame.menu_term_lines.front().source_row == 0)) {
        char lo = 0;
        char hi = 0;
        if (parse_letter_range_prompt(frame.menu_term_lines.front().text_utf8, lo, hi)) {
            return MenuScreen::ItemPrompt;
        }
    }
    if (has_esc_choice && (store_command_line >= 0)) {
        return MenuScreen::Store;
    }
    if (has_esc_choice && has_gold) {
        return MenuScreen::Building;
    }
    return MenuScreen::None;
}

/*!
 * @brief 品物選び: 行頭の札が `lo` から**連番で続く**行だけを拾う。
 * @details 買うときは商品一覧（`a)` からの連番・桁 0）とコマンド行（`p)` `s)` ほか）が
 * 同時に画面へ出る。連番を条件にすると商品一覧だけが残る。
 */
void collect_item_prompt_choices(GameFrame &frame)
{
    char lo = 0;
    char hi = 0;
    if (!parse_letter_range_prompt(frame.menu_term_lines.front().text_utf8, lo, hi)) {
        return;
    }

    char expected = lo;
    for (std::size_t i = 1; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        ChoiceTokenGroup g;
        if (!parse_choice_token(text, first_non_space(text), g)) {
            continue;
        }
        if ((g.keys.size() != 1)
            || (g.keys[0] != static_cast<int>(static_cast<unsigned char>(expected)))) {
            continue;
        }
        emit_choices_for_line(frame, i, std::vector<ChoiceTokenGroup>{ g });
        if (expected >= hi) {
            break;
        }
        ++expected;
    }
}
/*! @} */


bool fill_core_cursor(GameFrame &frame)
{
    frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
    frame.menu_core_cursors.clear();

    const std::string &mark = cursor_mark();
    if (mark.empty()) {
        return false;
    }

    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        /*
         * 小メニューの箱は親メニューと**同じ行に重ねて**描かれる
         * （`inkey_from_menu()` は `basey += 2; basex += 8;` で潜る）ので、
         * 親の `》` と子の `》` が 1 行に 2 つ並ぶ。1 行 1 個で打ち切らない。
         */
        std::size_t p = 0;
        while (p + mark.size() <= text.size()) {
            if (text.compare(p, mark.size(), mark) != 0) {
                ++p;
                continue;
            }
            // 行頭・空白の直後・枠線 `|` の直後だけ（本文中の印を拾わないため）。
            if ((p != 0) && (text[p - 1] != ' ') && (text[p - 1] != '|')) {
                ++p;
                continue;
            }
            // 印だけで終わる行（消し残し）は選択行ではない。
            const std::size_t after = p + mark.size();
            if (text.find_first_not_of(' ', after) == std::string::npos) {
                ++p;
                continue;
            }
            /*
             * 枠の右端は「空白 3 連（別の桁）」か「枠線 `|` の手前」の早い方。
             * `|` で切らないと、親メニューの枠が同じ行にある小メニューの項目まで伸びて
             * 1 本の帯に見える。項目名の中の `(|)`（`プレイ記録(|)`）だけは枠線ではない。
             */
            std::size_t end = text.find("   ", after);
            for (std::size_t q = after; q < text.size(); ++q) {
                if ((text[q] != '|') || ((q > 0) && (text[q - 1] == '('))) {
                    continue;
                }
                end = (end == std::string::npos) ? q : (std::min)(end, q);
                break;
            }
            if (end == std::string::npos) {
                end = text.size();
            }
            while ((end > after) && (text[end - 1] == ' ')) {
                --end;
            }

            MenuChoice cursor{ -1, 0, 0, 0, 0, 0 };
            cursor.line_index = static_cast<int>(i);
            cursor.span_begin = static_cast<int>(p);
            cursor.span_len = static_cast<int>(end - p);
            cursor.key_len = 0; // コアの画面に「決定で積む 1 文字」は無い
            if (frame.menu_core_cursors.empty()) {
                frame.menu_core_cursor = cursor;
            }
            frame.menu_core_cursors.push_back(cursor);
            p = end; // 次の印はこの枠より右から探す
        }
    }
    return !frame.menu_core_cursors.empty();
}

void fill_row0_prompt(GameFrame &frame)
{
    frame.prompt = PromptBar{};
    frame.numeric = NumericInput{};

    const std::string row0 = term_row_utf8(0);
    if (parse_prompt_line(row0, frame.prompt)) {
        /*
         * ミラーが開いている場面では、同じ行がミラーの何行目かを教える
         * （画面側がその行に枠を重ねられる）。開いていなければ -1 ＝
         * 「ミラーには出ていないので、画面が自分の帯へ出す」。
         */
        frame.prompt.line_index = -1;
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (frame.menu_term_lines[i].source_row == 0) {
                frame.prompt.line_index = static_cast<int>(i);
                break;
            }
        }
        return;
    }
    if (parse_numeric_input_line(row0, frame.numeric)) {
        frame.numeric.line_index = -1;
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (frame.menu_term_lines[i].source_row == 0) {
                frame.numeric.line_index = static_cast<int>(i);
                break;
            }
        }
        return;
    }

    std::string trimmed = row0;
    while (!trimmed.empty() && (trimmed.back() == ' ')) {
        trimmed.pop_back();
    }

    /*
     * **行 0 に無いプロンプトもある**（GU-10。変愚の `fill_row0_prompts()` の写し）。
     * クイックスタートの問いは `put_str(…, 14, 10)` で**行 14** に出る（`birth.c:7892`）ので、
     * 行 0 だけを見ていると**パッドで答えられない**。
     *
     * **広げても誤爆しにくいのは、表が「画面に出る文字列そのもの」で引いているから**である
     * （`[y/N]` のような綴りは問い以外に出てこない）。緩い解釈にしていたらこの走査は危険だった。
     *
     * そのうえで**行 0 が空のときだけ**にしてある。行 0 に何か出ているなら待っている入力は
     * そちらのはずで、画面のどこかに残っている古い問いを拾ってしまう余地を残さない。
     */
    if (trimmed.empty()) {
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (parse_prompt_line(frame.menu_term_lines[i].text_utf8, frame.prompt)) {
                //! ミラー行そのものを解析したので、桁位置はもうミラー基準になっている。
                frame.prompt.line_index = static_cast<int>(i);
                return;
            }
        }
        //! 拾えなかった分は空へ戻す（`parse_prompt_line` は失敗しても `out` を消す）。
        frame.prompt = PromptBar{};
    }

    /*
     * **打っている行そのもの**（S5 と対）。銘の刻印や検索語のように、`askfor_aux` が
     * **地図の上で**行 0 だけを使う場面がある。この間は icky が立たないのでミラーは
     * 1 行も開かず、`y/n` でも数値でもないので prompt にも載らない——つまり画面側には
     * 「いま何を打っているか」を知る手立てが 1 つも無い。打っても画面が変わらないので、
     * 確定したのかどうかも分からなくなる。
     *
     * コアが行 0 に書いたそのもの（プロンプト＋編集中の文字列）を帯へ渡す。
     * **選択肢は 1 つも作らない**——ここは選ぶ画面ではないので、カーソル層は素通りし、
     * キーは今までどおりコアの `askfor_aux` へ届く。
     * ミラーが開いている場面では渡さない（名前入力や自動拾いエディタは全画面の Term で、
     * 行 0 はミラーにそのまま出ている。ここでも渡すと同じ行が 2 か所に出る）。
     */
    if (!frame.text_input_active || !frame.menu_term_lines.empty() || trimmed.empty()) {
        return;
    }
    frame.prompt.text_utf8 = trimmed;
    frame.prompt.line_index = -1; //!< ミラーには出ていない＝画面が自分の帯へ出す
}

/*!
 * @brief 店・建物・品物選びの選択肢を `frame.menu_choices` へ積む（R3）。
 * @return 1 件でも積んだら true
 */
bool fill_store_menu_choices(GameFrame &frame)
{
    if (!frame.menu_open || frame.menu_term_lines.empty()) {
        return false;
    }
    /*
     * **ヘルプがいちばん先。**丸括弧の項目は名指しの表とも店・建物の見分けとも
     * 噛み合わないので、ここで見ないと 1 件も積まれない（2026-08-29 に気づいた）。
     */
    if (fill_help_menu(frame)) {
        return !frame.menu_choices.empty();
    }
    /*
     * **名指しの画面を先に見る**（その3 / c）。設定や知識には店・建物の手掛かりが無いので
     * `classify_menu_screen()` は `None` を返す……はずだが、知識だけは
     * ` ESC) 抜ける` と `コマンド:` の両方を持っていて **`Store` と誤判定される**。
     * 後回しにすると項目が 1 つも選べないまま「ページ送りだけ選べる画面」になる（実測）。
     */
    if (collect_named_menu_choices(frame)) {
        return true;
    }

    int store_command_line = -1;
    switch (classify_menu_screen(frame, store_command_line)) {
    case MenuScreen::ItemPrompt:
        collect_item_prompt_choices(frame);
        break;
    case MenuScreen::Store:
        /*
         * 商品一覧（行 6〜）の `a)` はこの画面では押しても効かない。
         * コアが毎周回書く `コマンド:` の**下だけ**がコマンドである。
         */
        for (std::size_t i = static_cast<std::size_t>(store_command_line) + 1;
             i < frame.menu_term_lines.size(); ++i) {
            emit_choices_for_line(frame, i, scan_choice_tokens(frame.menu_term_lines[i].text_utf8));
        }
        break;
    case MenuScreen::Building:
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            const int row = frame.menu_term_lines[i].source_row;
            if ((row < kBuildingChoiceRowMin) || (row > kBuildingChoiceRowMax)) {
                continue;
            }
            emit_choices_for_line(frame, i, scan_choice_tokens(frame.menu_term_lines[i].text_utf8));
        }
        break;
    case MenuScreen::None:
    default:
        break;
    }
    return !frame.menu_choices.empty();
}

} // namespace gb
