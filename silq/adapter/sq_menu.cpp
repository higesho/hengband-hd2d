/*!
 * @file sq_menu.cpp
 * @brief `sq_menu.h` の実装。
 *
 * 手本は `gensoband/adapter/gb_menu.cpp`。**やっていることは文字列の照合だけ**なので
 * 判定の理屈はそのまま写せるが、当てる先は Sil-Q を実読して数え直した
 * （幻想蛮怒の店・建物・設定・知識に当たるものは Sil-Q に 1 つも無い）。
 *
 * ## 判定は緩めない
 * 「`x)` があれば選択肢」にすると、別の icky 画面（一覧・鍛冶・ヘルプ）の本文まで
 * 拾って**押しても効かない選択肢**を並べるか、悪くすると別のコマンドを積む。
 * だから画面に出る文字列そのもの（` ESC` と `,` 区切りの札）で引き、
 * さらに**ミラーに札が並んでいること**まで確かめてから積む。
 *
 * ## 桁は素のセルで見る
 * `MenuChoice::span_begin` は `menu_term_lines[].text_utf8` の**バイト位置**だが、
 * 一覧の札（`a)`）は `show_inven()` が**全行同じ桁**へ置く（`object1.c` の `col`）。
 * 左に残っている前の画（状態列と地図）は行ごとにバイト幅が違う——状態列は
 * M1 から日本語である——ので、**バイト位置で束ねると桁が揃わない**。
 * そこで束ねるのは `sq_term_row()` の素のセル（＝ Term の桁）で行い、
 * 決まった桁を `column_to_utf8_offset()` でバイト位置へ直す。
 */

#include "sq_menu.h"

#include "sq_lang_c.h"
#include "sq_shim.h"
#include "sq_text.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace sq {

namespace {

//! Term 1 行の受け皿（主 Term は 80 桁だが余裕を取る。`sq_frame.cpp` の `kMaxCols` と同値）。
constexpr int kRowCells = 256;

//! 積む選択肢の上限。荷物袋は 23 枠なので普通は届かない（暴走よけ）。
constexpr std::size_t kMenuChoiceMax = 64;

/*!
 * @brief セル 1 個ぶんを空白とみなすか。**`sq_frame.cpp` の同名の関数と同じ規則**。
 * @details 判じ方を 2 か所に分けると、ミラーの字と枠の位置が食い違う。
 * 0x80 以上は日本語の文字のバイトであって空白ではない（M1 でここが変わった）。
 */
bool cell_is_blank(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u == ' ') || (u < 0x20) || (u == 0x7F);
}

/*!
 * @brief 名札をカタログで引いて **UTF-8** で返す。
 * @param ascii 英語の名札。これが**そのまま鍵**である
 * @return 訳文（UTF-8）。訳が無ければ `ascii` のまま
 *
 * @details `sq_pad_commands.cpp` の同名の関数と同じ道（M1 追補 A4）。
 * `sq_tr()` が返すのは **CP932** なので、線に載せる前に直す。
 * **日本語層が寝ているときは 1 バイトも変わらない**（必守制約 1）。
 *
 * ここで日本語リテラルを直に書かないのは、**この TU の焼かれ方が平台で違う**ため
 * （Windows は ACP＝CP932、Android は UTF-8）。カタログという 1 本の道を通す。
 */
std::string tr_utf8(const char *ascii)
{
    const char *const translated = sq_tr(ascii);
    if (translated == nullptr) {
        return std::string();
    }
    for (const char *p = translated; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) {
            return sjis_to_utf8(translated, std::strlen(translated));
        }
    }
    return std::string(translated);
}

/*!
 * @brief 主 Term の 1 行を UTF-8 で読む（制御文字は空白に倒す）。
 * @details 行 0 は**ミラーが開いていなくても**読める（`sq_term_row()` は
 * `Term->scr` を直に見る）。それがこの層の存在理由である。
 */
std::string term_row_utf8(int y)
{
    char cells[kRowCells];
    std::memset(cells, ' ', sizeof(cells));
    const int n = sq_term_row(y, cells, nullptr, kRowCells);
    if (n <= 0) {
        return std::string();
    }

    std::string sys;
    sys.reserve(static_cast<std::size_t>(n));
    bool wide = false;
    for (int x = 0; x < n; ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        if (is_sjis_lead(lead) && ((x + 1) < n)) {
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            wide = true;
            ++x;
            continue;
        }
        if (is_sjis_lead(lead)) {
            sys.push_back(' '); // 行の端で割れた。**半端なバイトは運ばない**
            continue;
        }
        sys.push_back(cell_is_blank(cells[x]) ? ' ' : cells[x]);
    }

    if (!wide) {
        return sys; // 純 ASCII。**1 バイトも触らない**（設計 §1 制約 1）
    }
    std::string out = sjis_to_utf8(sys);
    if (out.empty() && !sys.empty()) {
        out.assign(sys.size(), ' ');
    }
    return out;
}

//! 末尾の空白を落とす。行 0 の照合はどれも「末尾から」なので先に均す。
std::string trim_right(const std::string &raw)
{
    std::string text = raw;
    while (!text.empty() && (text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

/*!
 * @brief 行 0 が `[y/n]` のプロンプトなら選択肢へ持ち上げる。
 * @details 出どころは `get_check()`（`util.c:3793`）の
 * `strnfmt(buf, 78, "%.70s[y/n] ", prompt)` **1 か所だけ**である
 * （`silq/src` に `strchr("YyNn", ch)` は `get_check` と `get_check_other` の
 * 2 つしか無く、後者は**呼び手が 1 つも無い**——実読 2026-08-22）。
 *
 * 枠の `"%.70s[y/n] "` はカタログで `J:` を空にしてある（`messages.ja.txt:8641`）
 * ＝わざと訳さない指定なので、**日本語でも綴りは `[y/n]` のまま**である。
 * 問い本文のほうは `%s` の断片として訳される（`z-form.c:519`）。
 *
 * @note 「`[` と `/` があれば選択肢」のような緩い解釈にしない。行 0 は
 * メッセージ行でもあり、普通の文言の最中にキーを飲み込むと誤コマンドより性質が悪い。
 */
bool parse_prompt_line(const std::string &raw, PromptBar &out)
{
    out = PromptBar{};

    const std::string text = trim_right(raw);
    static const std::string kSuffix = "[y/n]";
    if ((text.size() <= kSuffix.size())
        || (text.compare(text.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)) {
        return false;
    }
    const std::size_t begin = text.size() - kSuffix.size();

    struct Option {
        const char *label_ascii; //!< カタログの鍵。引けなければこのまま出る
        char key;
    };
    static const Option kOptions[] = { { "Yes", 'y' }, { "No", 'n' } };

    PromptBar built;
    for (const Option &option : kOptions) {
        const std::size_t at = text.find(option.key, begin);
        if (at == std::string::npos) {
            return false; // ここへは来ない（`[y/n]` に y も n も在る）。積むより出さない
        }
        PromptChoice choice;
        choice.label_utf8 = tr_utf8(option.label_ascii);
        choice.key = option.key;
        choice.begin = static_cast<int>(at);
        choice.len = 1;
        built.choices.push_back(std::move(choice));
    }
    built.text_utf8 = text;
    out = std::move(built);
    return true;
}

/*!
 * @brief 行 0 が数値入力（`いくつ (0-5): 1`）なら最小・最大・現在値を読む。
 * @details 判定は **`(数字-数字): ` があり、その後ろが数字だけ（または空）**であること。
 * `get_quantity()`（`util.c:3684`）が `Quantity (0-%d): ` を出し、
 * `askfor_aux()` が編集中の数字をその後ろに書く。
 * 訳文も `いくつ (0-%d): ` で並びは同じ（`messages.ja.txt:8543`）。
 *
 * 品選びの行 0（`(荷物: a-b, … ESC) …`）は**括弧の中が英字**なので当たらない。
 * 色の編集（`"Red (0-255) "`。`cmd4.c:8895`）は `): ` ではなく `) ` なので当たらない
 * ——あちらは M0.5 の範囲外である。
 */
bool parse_numeric_input_line(const std::string &raw, NumericInput &out)
{
    out = NumericInput{};

    const std::string text = trim_right(raw);
    if (text.empty()) {
        return false;
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
    const auto all_digits = [](const std::string &s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0') && (c <= '9'); });
    };
    const std::string lo = inside.substr(0, dash);
    const std::string hi = inside.substr(dash + 1);
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
    out.value_begin = static_cast<int>(close + 3);
    out.value_len = static_cast<int>(tail.size());
    return true;
}

/*!
 * @brief 行 0 が「一覧から 1 つ選べ」の問いなら、**札を出る順に**返す。
 * @return 2 通りの組み立てのどちらかに当たったら true
 *
 * @details Sil-Q でこの形の画面は **2 つだけ**である（`silq/src` を実読。2026-08-22）。
 * どちらも `prt(tmp_val, 0, 0)` で行 0 へ `(<鍵の並び>) <問い>` を書き、`inkey()` で
 * 1 文字を待つ。**コアはカーソルを持たない**（`*_menu_aux(int *highlight)` の形では無い）。
 *
 * | 画面 | 出どころ | 行 0 の字面 | 鍵の並び |
 * |---|---|---|---|
 * | 品選び | `get_item()`（`object1.c:3032`） | `(荷物: a-b, / for Equip, ESC) どれを装備しますか?\s` | `a`〜`b`（範囲） |
 * | 歌選び | `do_cmd_change_song()`（`cmd4.c:621`） | `(歌: s,a,b, * to see) どの歌を歌いますか:\s` | `s` `a` `b`（並び） |
 *
 * ## 見分けを緩めない
 * - 品選び … 括弧の中に `%c-%c`（`object1.c:2933`）があり、**` ESC` で終わる**
 *   （同 `:3032` の `my_strcat`）。どちらも訳のフックを 1 つも通らないので、
 *   **どの言語でも必ずこの綴りで出る**。
 * - 歌選び … 括弧の中が `,` 区切りの**1 文字の札の並び**（同 `:600-613` の `,%c`）。
 *   見出し（`Songs: s` → `歌: s`）は訳されるが、区切りと札は訳を通らない。
 *
 * どちらも「行頭が `(`」を要る。行 0 はメッセージ行でもあるが、`msg_print` の文が
 * `(` で始まることは無い（品名は `You have ...` の形で出る）。
 * 加えて `collect_list_choices()` が**ミラーに実際に札が並んでいるか**を確かめるので、
 * 字面だけで積むことはない。
 */
bool parse_choice_prompt(const std::string &raw, std::vector<char> &expected)
{
    expected.clear();

    const std::string text = trim_right(raw);
    if (text.empty() || (text[0] != '(')) {
        return false;
    }
    const std::size_t close = text.find(") ");
    if (close == std::string::npos) {
        return false;
    }
    const std::string inside = text.substr(1, close - 1);

    const auto is_alpha = [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return ((u >= 'a') && (u <= 'z')) || ((u >= 'A') && (u <= 'Z'));
    };
    const auto is_alnum = [&is_alpha](char c) {
        const auto u = static_cast<unsigned char>(c);
        return is_alpha(c) || ((u >= '0') && (u <= '9'));
    };

    /* --- 品選び: ` ESC` で終わり、括弧の中に `%c-%c` がある --- */
    static const std::string kEsc = " ESC";
    if ((inside.size() >= kEsc.size())
        && (inside.compare(inside.size() - kEsc.size(), kEsc.size(), kEsc) == 0)) {
        for (std::size_t i = 0; (i + 2) < inside.size(); ++i) {
            if ((inside[i + 1] != '-') || !is_alnum(inside[i]) || !is_alnum(inside[i + 2])) {
                continue;
            }
            if (inside[i] > inside[i + 2]) {
                continue;
            }
            for (char c = inside[i]; c <= inside[i + 2]; ++c) {
                expected.push_back(c);
            }
            return true;
        }
        return false; // 一覧が空（`i1 > i2`）。選べる札が 1 枚も無い
    }

    /*
     * --- 歌選び: `,` で切って、**1 文字の札**だけを拾う ---
     * 先頭の切れ端は `歌: s` のように見出しが付いているので、**末尾の 1 文字**を見る
     * （その手前が空白か `:` のときだけ札とみなす）。`* to see` や ` / for Equip` は
     * 末尾の手前が字なので外れる。
     */
    std::vector<std::string> pieces;
    std::string current;
    for (const char c : inside) {
        if (c == ',') {
            pieces.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    pieces.push_back(current);

    for (const std::string &raw_piece : pieces) {
        std::string piece = raw_piece;
        while (!piece.empty() && (piece.front() == ' ')) {
            piece.erase(piece.begin());
        }
        while (!piece.empty() && (piece.back() == ' ')) {
            piece.pop_back();
        }
        if (piece.empty() || !is_alpha(piece.back())) {
            continue;
        }
        if (piece.size() == 1) {
            expected.push_back(piece.back());
            continue;
        }
        const char before = piece[piece.size() - 2];
        if ((before == ' ') || (before == ':')) {
            expected.push_back(piece.back());
        }
    }
    if (expected.size() < 2) {
        expected.clear();
        return false; // 1 枚だけの「並び」は偶然と区別が付かない
    }
    return true;
}

/*!
 * @brief Term の桁 `col` が、その行の `text_utf8` の何バイト目にあたるか。
 * @details `sq_frame.cpp` の `row_to_text()` と**同じ規則**で前半を組み直して長さを測る。
 * あちらは属性の変わり目で塊に割って変換するが、CP932 → UTF-8 は状態を持たないので
 * 塊ごとの変換をつないだ長さと、まとめて変換した長さは等しい。
 *
 * @note 2 バイト文字が `col` をまたぐ場合はここが 1 バイトずれるが、**起きない**——
 * 呼び手は「`col - 1` が空白」を確かめた桁しか渡さない（`collect_list_choices`）。
 */
int column_to_utf8_offset(const char *cells, int n, int col)
{
    std::string sys;
    for (int x = 0; (x < col) && (x < n); ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        if (is_sjis_lead(lead) && ((x + 1) < n) && ((x + 1) < col)) {
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            ++x;
            continue;
        }
        if (is_sjis_lead(lead)) {
            sys.push_back(' ');
            continue;
        }
        sys.push_back(cell_is_blank(cells[x]) ? ' ' : cells[x]);
    }

    bool wide = false;
    for (const char c : sys) {
        if (static_cast<unsigned char>(c) >= 0x80u) {
            wide = true;
            break;
        }
    }
    if (!wide) {
        return static_cast<int>(sys.size());
    }
    const std::string out = sjis_to_utf8(sys);
    //! 変換できない塊は `row_to_text()` が空白で埋める（＝バイト数はそのまま）。
    return static_cast<int>(out.empty() ? sys.size() : out.size());
}

//! 一覧の 1 行ぶんの候補。`col` は **Term の桁**、`end` はその行の最後の非空白の次。
struct ListCandidate {
    int row{ 0 };
    int col{ 0 };
    char label{ 0 };
    int end{ 0 };
};

/*!
 * @brief 一覧の札を集めて `frame.menu_choices` へ積む。
 * @param expected 行 0 が名乗った札。**出る順**（`parse_choice_prompt()`）
 *
 * @details `show_inven()` / `show_equip()` / `show_floor()` / `show_songs()` はどれも
 * **行 1 から下へ 1 行 1 件**、札は `put_str("%c)", 行, col)` で**全行同じ桁**、
 * 札の左 2 桁は `prt("", 行, col - 2)` で消してある。
 * だから「同じ桁に、行を下るごとに `expected` を順にたどる札」を探せば一覧が出る。
 *
 * **飛びを許す。**品選びの札は荷物の枠番号そのものなので、選べない品を挟むと
 * `a) d)` のように抜ける（`expected` は `a-d` と名乗る）。飛ぶのは前へだけで、
 * 戻ったらそこで打ち切る。
 *
 * 左に残っている前の画（状態列と地図）にも `a)` の形が出うる——`a` は蟻、`)` は
 * 落ちている武器である——ので、**桁で束ねて長いほうを採る**。ここが
 * 「行 0 の字面だけで積まない」の担保でもある。
 */
void collect_list_choices(GameFrame &frame, const std::vector<char> &expected)
{
    int cols = 0;
    int rows = 0;
    if (expected.empty() || (sq_term_size(&cols, &rows) != SQ_OK)) {
        return;
    }

    char cells[kRowCells];
    unsigned char attrs[kRowCells];
    std::vector<ListCandidate> candidates;

    for (int y = 1; y < rows; ++y) {
        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int n = sq_term_row(y, cells, attrs, kRowCells);
        if (n <= 0) {
            continue;
        }
        int end = n;
        while ((end > 0) && cell_is_blank(cells[end - 1])) {
            --end;
        }
        for (int x = 0; (x + 1) < end; ++x) {
            const char label = cells[x];
            if ((cells[x + 1] != ')')
                || (std::find(expected.begin(), expected.end(), label) == expected.end())) {
                continue;
            }
            if ((x > 0) && !cell_is_blank(cells[x - 1])) {
                continue;
            }
            if (((x + 2) < end) && !cell_is_blank(cells[x + 2])) {
                continue;
            }
            candidates.push_back(ListCandidate{ y, x, label, end });
        }
    }
    if (candidates.empty()) {
        return;
    }

    /*
     * 桁ごとに「`expected[0]` から始まり、行を下るごとに `expected` を前へたどる」
     * いちばん長い並びを採る。同点なら**最後まで（`expected` の末尾で）終わっている**ほう。
     */
    int best_col = -1;
    int best_score = 0;
    std::vector<ListCandidate> best_seq;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const int col = candidates[i].col;
        if (std::any_of(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(i),
                [col](const ListCandidate &c) { return c.col == col; })) {
            continue; // この桁はもう試した
        }
        std::vector<ListCandidate> seq;
        std::size_t cursor = 0;
        for (const ListCandidate &candidate : candidates) {
            if (candidate.col != col) {
                continue;
            }
            const auto at = std::find(expected.begin() + static_cast<std::ptrdiff_t>(cursor),
                expected.end(), candidate.label);
            if (at == expected.end()) {
                break; // 申告に無いか、もう通り過ぎた札。ここで打ち切る
            }
            if (seq.empty() && (at != expected.begin())) {
                break; // 一覧は必ず先頭の札から始まる
            }
            cursor = static_cast<std::size_t>(at - expected.begin()) + 1;
            seq.push_back(candidate);
        }
        if (seq.empty()) {
            continue;
        }
        const int score = (static_cast<int>(seq.size()) * 2) + ((cursor == expected.size()) ? 1 : 0);
        if (score > best_score) {
            best_score = score;
            best_col = col;
            best_seq = std::move(seq);
        }
    }
    if ((best_col < 0) || best_seq.empty()) {
        return;
    }

    for (const ListCandidate &candidate : best_seq) {
        if (frame.menu_choices.size() >= kMenuChoiceMax) {
            break;
        }
        //! ミラーの添字を `source_row` で引く（行を捨てる作りに変わっても壊れないように）。
        int line_index = -1;
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (frame.menu_term_lines[i].source_row == candidate.row) {
                line_index = static_cast<int>(i);
                break;
            }
        }
        if (line_index < 0) {
            continue;
        }
        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int n = sq_term_row(candidate.row, cells, attrs, kRowCells);
        if (n <= 0) {
            continue;
        }
        const int begin_byte = column_to_utf8_offset(cells, n, candidate.col);
        /*
         * 枠は**その行の右端まで**伸ばす。札の左 2 桁から右は一覧が塗り直した区画で、
         * 前の画は残っていない（`prt("", row, col - 2)`）。重さ（桁 71）まで
         * 囲むことになるが、それは 1 品ぶんの表示そのものである。
         */
        const int end_byte = column_to_utf8_offset(cells, n, candidate.end);

        MenuChoice choice;
        choice.line_index = line_index;
        choice.span_begin = begin_byte;
        choice.span_len = (std::max)(0, end_byte - begin_byte);
        choice.key_begin = begin_byte;
        choice.key_len = 1;
        choice.key = static_cast<unsigned char>(candidate.label);
        frame.menu_choices.push_back(choice);
    }
}

} // namespace

void fill_row0_prompt(GameFrame &frame)
{
    frame.prompt = PromptBar{};
    frame.numeric = NumericInput{};

    const std::string row0 = term_row_utf8(0);
    if (row0.empty()) {
        return;
    }

    PromptBar prompt;
    NumericInput numeric;
    if (parse_prompt_line(row0, prompt)) {
        frame.prompt = std::move(prompt);
    } else if (parse_numeric_input_line(row0, numeric)) {
        frame.numeric = std::move(numeric);
    } else {
        return;
    }

    /*
     * ミラーが開いている場面では、同じ行がミラーの何行目かを教える（画面側がその行に
     * 枠を重ねられる）。開いていなければ **-1 ＝ ミラーには出ていない**で、
     * 画面側が自分の帯・小窓を出す。個数を聞かれるのは `get_item()` が
     * `screen_load()` した後なので、実際にはほぼ -1 になる。
     */
    int line_index = -1;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].source_row == 0) {
            line_index = static_cast<int>(i);
            break;
        }
    }
    frame.prompt.line_index = line_index;
    frame.numeric.line_index = line_index;
}

bool fill_menu_choices(GameFrame &frame)
{
    if (!frame.menu_open || frame.menu_term_lines.empty()) {
        return false;
    }
    /*
     * 行 0 は**ミラーではなく Term から直に**読む。ミラーの先頭が行 0 とは限らない
     * （いまは全行積んでいるが、そこを直したときに黙って別の行を読まないように）。
     */
    std::vector<char> expected;
    if (!parse_choice_prompt(term_row_utf8(0), expected)) {
        return false;
    }
    collect_list_choices(frame, expected);
    return !frame.menu_choices.empty();
}

} // namespace sq
