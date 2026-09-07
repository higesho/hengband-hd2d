/*!
 * @file fc_menu.cpp
 * @brief `fc_menu.h` の実装（M0.5 FH-01。設計 §14.2(b)）。
 *
 * 手本は `gensoband/adapter/gb_menu.cpp`。判定の理屈（文字列の照合・
 * 「画面に出る文字列そのもの」で引く）は写し、文言は Frox の実読で差し替えた:
 *
 * | 何 | Frox の綴り | 出どころ |
 * | --- | --- | --- |
 * | はい/いいえ | 末尾 `[y/n]`・`[Y/n]`・`[(O)k/(C)ancel]` | `util.c:3041` / `:3074-3084` |
 * | 個数 | `Quantity (1-%d): `（一般形 `(lo-hi): `） | `util.c:3246` |
 * | 札 1 | ` a) 品名`（品・呪文・誕生・menu.c） | `inv.c` の ` %c)`・`monspell.c:5537` |
 * | 札 2 | `(1) General Options`（オプションの根） | `cmd4.c:2765` の `(%c) %s` |
 * | 札 3 | `[b/p/g] Buy`（doc の命令列。`/` 区切り） | `shop.c:1704` ほか |
 * | カーソル | `"> "`＋枠 `+----` | `util.c` `inkey_from_menu()` |
 *
 * ## 判定の緩さ（設計 §14.2(b) の判断。ここに残す）
 * 幻想蛮怒は「画面ごとに名指し」で絞ったが、**Frox の doc UI は知らない字を黙って
 * 無視する**ので、ミラーが開いている間の全行走査でも害が小さい。M0.5 は全行走査で
 * 入れ、誤爆が実測されたら名指しの表に絞る。プロンプト（`[y/n]`・数値）だけは
 * 従来どおり**画面に出る文字列そのもの**の照合で、緩めていない——行 0 は
 * メッセージ行でもあり、緩い解釈は普通の文言の最中にキーを飲み込む。
 *
 * ## 文字コード
 * M0 の Frox は純 ASCII（`frox/UPSTREAM.md`）なので変換が無く、バイト位置＝桁位置。
 * M1 で CP932 が入るときは `sq_menu.cpp` の「桁は素のセルで見る」を持ち込むこと。
 */

#include "fc_menu.h"

#include "fc_text.h" //!< CP932 → UTF-8（設計 §3.1）

#include "fc_shim.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fc {

namespace {

//! Term 1 行の受け皿（主 Term は 80 桁だが余裕を取る。`fc_frame.cpp` の `kMaxCols` と同値）。
constexpr int kRowBytes = 256;

//! 積む選択肢の上限（暴走よけ。品は 26 枠＋命令列なので普通は届かない）。
constexpr std::size_t kMenuChoiceMax = 64;

//! セル 1 個ぶんを空白とみなすか。**`fc_frame.cpp` の同名の関数と同じ規則**。
bool cell_is_blank(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u == ' ') || (u < 0x20) || (u == 0x7F);
}

/*!
 * @brief 主 Term の 1 行を読む（制御文字は空白に倒す）。
 * @details 行 0〜2 は**ミラーが開いていなくても**読める（`fc_term_row()` は
 * `Term->scr` を直に見る）。プロンプトが icky にならない以上、ここが唯一の読み口。
 */
std::string term_row_text(int y)
{
    char cells[kRowBytes];
    std::memset(cells, ' ', sizeof(cells));
    const int n = fc_term_row(y, cells, nullptr, kRowBytes);
    if (n <= 0) {
        return std::string();
    }
    /*
     * CP932 のまま組み、最後に 1 回だけ変換する（設計 §3.2）。
     * **純 ASCII なら 1 バイトも触らない**——英語のときの答えを M0 と変えない。
     *
     * @note ここが返す字は**そのまま `prompt.text_utf8` に載る**。品名が日本語に
     * なると（J3）確認の問い（`Really sell ... [y/n]`）に 2 バイト文字が混ざるので、
     * この 1 か所を通しておかないと帯が化ける。
     * 綴りの照合（`[y/n]` `(1 to 5): `）は ASCII なので、変換しても答えは変わらない。
     */
    std::string sys;
    sys.reserve(static_cast<std::size_t>(n));
    bool wide = false;
    for (int x = 0; x < n; ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        if (fc::is_sjis_lead(lead) && ((x + 1) < n)) {
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            wide = true;
            ++x;
            continue;
        }
        if (fc::is_sjis_lead(lead)) {
            sys.push_back(' '); // 行の端で割れた。**半端なバイトは運ばない**
            continue;
        }
        sys.push_back(cell_is_blank(cells[x]) ? ' ' : cells[x]);
    }

    if (!wide) {
        return sys;
    }
    std::string out = fc::sjis_to_utf8(sys);
    if (out.empty() && !sys.empty()) {
        out.assign(sys.size(), ' ');
    }
    return out;
}

//! 末尾の空白を落とす。プロンプトの照合はどれも「末尾から」なので先に均す。
std::string trim_right(const std::string &raw)
{
    std::string text = raw;
    while (!text.empty() && (text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

bool is_ascii_alpha(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return ((u >= 'a') && (u <= 'z')) || ((u >= 'A') && (u <= 'Z'));
}

bool is_ascii_alnum(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return ((u >= '0') && (u <= '9')) || is_ascii_alpha(c);
}

/*!
 * @brief `] ` の直後に来る**札の頭**として認めてよい字か（札 3 の担保）。
 * @details 英字のほか、**2 バイト文字の先頭バイト**（UTF-8 の 0x80 以上）も認める。
 *
 * M1 で命令列が日本語になり（`[b/p/g] 買う`）、英字だけを見ていたせいで
 * **店の命令列が 1 つも札にならず、カーソルが品行へ落ちていた**
 * （実機で見つけた（2026-08-28）「店のメニューで初めから商品リストにカーソルがあり、
 * コントローラーではコマンドが使えない」）。
 *
 * **数字と記号は認めない。** 防具の `[1] (+2)` を拾わない担保はそこに残る。
 */
bool is_choice_label_head(char c)
{
    return is_ascii_alpha(c) || (static_cast<unsigned char>(c) >= 0x80);
}

/* ==================================================== プロンプト（`[y/n]` と個数） */

/*!
 * @brief 1 行が `y/n` 系のプロンプトなら選択肢へ持ち上げる。
 * @details 表は**画面に出る文字列そのもの**（見出しの表）。`get_check()` の色タグ
 * （`<color:y>[y/n]</color>`）は Term に出ないので、照合は素の `[y/n]` でよい
 * （§14.1 の事実 4）。`[(O)k/(C)ancel]` は rogue 式配列のときだけ出る
 * （`util.c:3066` で外している）が、表に置いておいて害は無い。
 */
bool parse_prompt_line(const std::string &raw, PromptBar &out)
{
    out = PromptBar{};

    const std::string text = trim_right(raw);
    if (text.empty()) {
        return false;
    }

    struct Option {
        const char *label; //!< ASCII。M0 の Frox は英語で立つ（設計 §3.6）
        char key;
    };
    struct Pattern {
        const char *suffix;
        Option options[4]; //!< `key == 0` で終端
    };
    static const Pattern kPatterns[] = {
        //! 破壊の確認（`obj.c:1287`）。`A` は「以後自動で壊す」（自動拾い登録）。
        { "[y/n/Auto]", { { "Yes", 'y' }, { "No", 'n' }, { "Auto", 'A' }, { nullptr, 0 } } },
        //! 移動の再開（`dungeon.c:4532`）。`A` は「以後聞かずに再開」。
        { "[y/n/Always]", { { "Yes", 'y' }, { "No", 'n' }, { "Always", 'A' }, { nullptr, 0 } } },
        { "[y/n]", { { "Yes", 'y' }, { "No", 'n' }, { nullptr, 0 } } },
        //! `CHECK_DEFAULT_Y`（`util.c:3079`）。n と Esc 以外はすべて「はい」扱い。
        { "[Y/n]", { { "Yes", 'Y' }, { "No", 'n' }, { nullptr, 0 } } },
        { "[(O)k/(C)ancel]", { { "Ok", 'O' }, { "Cancel", 'C' }, { nullptr, 0 } } },
    };

    for (const Pattern &pattern : kPatterns) {
        const std::string suffix = pattern.suffix;
        if ((text.size() <= suffix.size())
            || (text.compare(text.size() - suffix.size(), suffix.size(), suffix) != 0)) {
            continue;
        }
        const std::size_t begin = text.size() - suffix.size();

        PromptBar built;
        bool ok = true;
        for (const Option &option : pattern.options) {
            if (option.key == '\0') {
                break;
            }
            const std::size_t at = text.find(option.key, begin);
            if (at == std::string::npos) {
                ok = false; // 表の書き間違い。積むより出さない方が安全
                break;
            }
            PromptChoice choice;
            choice.label_utf8 = option.label;
            choice.key = option.key;
            choice.begin = static_cast<int>(at);
            choice.len = 1;
            built.choices.push_back(std::move(choice));
        }
        if (!ok || (built.choices.size() < 2)) {
            continue;
        }
        built.text_utf8 = text;
        out = std::move(built);
        return true;
    }
    return false;
}

/*!
 * @brief 行 0 が数値入力（`Quantity (1-5): 1`）なら最小・最大・現在値を読む。
 * @details 判定は **`(数字-数字): ` があり、その後ろが数字だけ（または空）**であること。
 * `get_quantity()`（`util.c:3246`）が `Quantity (1-%d): ` を出し、`askfor()` が
 * 編集中の数字をその後ろに書く。呼び手が独自の文言を渡しても `(lo-hi): ` の形なら
 * 同じに当たる。品選びの行 0 は**括弧の中が英字**なので当たらない。
 * 幻想蛮怒に在った `(MAX:n)` の枝は Frox に無いので落とした（設計 §14.2(b)）。
 */
bool parse_numeric_input_line(const std::string &raw, NumericInput &out)
{
    out = NumericInput{};

    const std::string text = trim_right(raw);
    if (text.empty()) {
        return false;
    }

    /*
     * 値の欄は空のこともある（問いが出た直後）。`trim_right` が末尾の空白を
     * 落とすので `): ` が `):`  になっている——**両方見る**（FH-15 の検査が捕まえた）。
     */
    std::size_t close = text.rfind("): ");
    std::size_t tail_at = (close == std::string::npos) ? std::string::npos : (close + 3);
    if (close == std::string::npos) {
        if ((text.size() < 2) || (text.compare(text.size() - 2, 2, "):") != 0)) {
            return false;
        }
        close = text.size() - 2;
        tail_at = text.size();
    }
    const std::size_t open = text.rfind('(', close);
    if (open == std::string::npos) {
        return false;
    }
    const std::string inside = text.substr(open + 1, close - open - 1);
    /*
     * 区切りは **2 綴り**ある（FH-15。実機で見つけた（2026-08-24）
     * 「数量入力で Enter/A を押すと a が入り購入可否に進めない」）:
     *   - `(1-5)`   … `get_quantity()`（`util.c:3246`）
     *   - `(1 to 5)` … `msg_input_num()`（`message.c:626`）。**店の個数はこちら**
     * 片方しか見ていなかったので、店では `frame.numeric` が立たず、決定が
     * カーソル層の**選択肢**まで流れて品の letter（`a`）を送っていた。
     *
     * @warning **この 2 綴りを日本語化しないこと**（M1 J4 で踏みかけた）。
     * カタログで `(%d to %d)` を `(%d〜%d)` に替えると、ここが区切りを
     * 見つけられず `frame.numeric` が立たない——**機械の検査は全部通り、
     * 実機で個数がカーソル入力できなくなる**。括弧の外（`Quantity` の語）は
     * 訳してよい。訳したくなったら、まず**ここの探索を綴りに依らない形へ
     * 直してから**にする。
     */
    std::size_t sep = inside.find(" to ");
    std::size_t sep_len = 4;
    if (sep == std::string::npos) {
        sep = inside.find('-');
        sep_len = 1;
    }
    if ((sep == std::string::npos) || (sep == 0) || (sep + sep_len >= inside.size())) {
        return false;
    }
    const auto all_digits = [](const std::string &s) {
        return !s.empty()
            && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0') && (c <= '9'); });
    };
    const std::string lo = inside.substr(0, sep);
    const std::string hi = inside.substr(sep + sep_len);
    if (!all_digits(lo) || !all_digits(hi)) {
        return false;
    }

    const std::string tail = text.substr(tail_at);
    if (!tail.empty() && !all_digits(tail)) {
        return false; // `): ` の後ろに文字がある＝数値入力ではない
    }

    out.active = true;
    out.prompt_utf8 = text.substr(0, tail_at);
    out.min = std::atoi(lo.c_str());
    out.max = std::atoi(hi.c_str());
    out.value = tail.empty() ? out.min : std::atoi(tail.c_str());
    out.text_len = static_cast<int>(tail.size());
    out.digits = static_cast<int>(hi.size());
    out.value_begin = static_cast<int>(tail_at);
    out.value_len = static_cast<int>(tail.size());
    return true;
}

/* ============================================================ 選択肢の札（3 書式） */

//! 1 つの札。`keys` は `i/e)` のような 2 コマンド札で 2 個になる。
struct ChoiceTokenGroup {
    std::size_t begin{};
    std::vector<std::pair<std::size_t, std::size_t>> key_spans;
    std::vector<int> keys;
    //! `(1)` の書式で読んだ札か（枠を行末まで伸ばすかの判断。`emit_choices_for_line`）。
    bool paren{ false };
    //! `[b/p/g]` の書式で読んだ札か（**店の命令列の目印**。FH-14 の絞り込み）。
    bool bracket{ false };
};

//! `)` の直後として妥当か（空白／行末）。
bool is_choice_token_terminator(const std::string &text, std::size_t pos)
{
    if (pos >= text.size()) {
        return true;
    }
    return text[pos] == ' ';
}

/*!
 * @brief 札 1: ` a)` `ESC)` `Enter)` `Tab)` `*)` `i/e)` `-)`（行頭か空白の直後）。
 * @details `gb_menu.cpp` の `parse_choice_token` から日本語の「スペース)」だけを
 * 落とした写し。`10)` のような 2 桁の札は**わざと**読めない（`1` の次が `)` でない
 * →不成立、`0)` は前が字→開始位置に来ない）——多桁の札を Frox の一覧は使わない。
 *
 * **語キーと記号キーは実機の報告で足した**（FH-12。2026-08-24: 誕生画面の右列
 * `*) Random Name` `?) Help` `=) Options` `Tab) More Info` `Enter) Next Screen`
 * `Esc) Prev Screen` が札にならず、カーソルが左列に閉じ込められて先へ進めなかった）。
 * 綴りは `frox/src` の doc 文字列の全数 grep（ESC 19・Esc 3・RET 10・Enter 1・
 * ENTER・Tab 2・TAB・Space・SPACE・`*` 18・`?` 5・`=` 3）。
 */
bool parse_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
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

    /*
     * 語のキー。**単字より先に見る**（`RET)` を `R` の札と読まないため）。
     * Enter と Esc は制御キーの値そのもの（0x0D / 0x1B）を積む——画面のカーソル層が
     * Enter は `confirm`・Esc は `cancel`・その他の制御キーは ctrl 付き文字へ
     * 畳んで送り返す（`ui_cursor.cpp` の `handle_choice`）。
     */
    static const struct {
        const char *word;
        int key;
    } kWordKeys[] = {
        { "ESC", 0x1B }, { "Esc", 0x1B },
        { "ENTER", 0x0D }, { "Enter", 0x0D }, { "RET", 0x0D },
        { "SPACE", ' ' }, { "Space", ' ' },
        { "TAB", 0x09 }, { "Tab", 0x09 },
    };
    for (const auto &w : kWordKeys) {
        const std::size_t len = std::strlen(w.word);
        if ((text.compare(p, len, w.word) == 0) && accept(len, w.key)) {
            return true;
        }
    }
    //! `i/e)`: 1 つの札に 2 コマンド。キーはそれぞれ独立に積める。
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
    if ((p < text.size())
        && (is_ascii_alnum(text[p]) || (text[p] == '-') || (text[p] == '*') || (text[p] == '?')
            || (text[p] == '='))) {
        /* `*` `?` `=` は誕生画面の右列と doc の命令列で使われる記号キー（FH-12）。 */
        return accept(1, static_cast<unsigned char>(text[p]));
    }
    return false;
}

/*!
 * @brief 札 2: `(1)` `(V)`（オプションの根。`cmd4.c:2765` の `(%c) %s`）。
 * @details **見えている字のまま積む。**オプションの根は大文字で描き
 * `tolower` で照合する（`cmd4.c:2786`）ので、大文字のまま送って通る。
 */
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
    out.keys.push_back(static_cast<unsigned char>(text[p + 1]));
    return true;
}

/*!
 * @brief 札 3 の語（`[q/Esc]` の `Esc` など）。`key < 0` は「押して送れない」
 * ——読み飛ばして同じ札の別のキーに頼る（`[Space/PgDn]` は Space で送る）。
 */
int bracket_word_key(const std::string &text, std::size_t p, std::size_t len)
{
    struct Word {
        const char *word;
        int key;
    };
    static const Word kWords[] = {
        { "Esc", 0x1B },
        { "Enter", '\r' },
        { "Space", ' ' },
        { "Tab", '\t' },
        //! 特殊キー（`SKEY_*`）はプロトコルの keys では送れない。同義のキーに任せる。
        { "PgDn", -1 },
        { "PgUp", -1 },
        { "Home", -1 },
        { "End", -1 },
    };
    for (const Word &w : kWords) {
        if ((std::strlen(w.word) == len) && (text.compare(p, len, w.word) == 0)) {
            return w.key;
        }
    }
    return -2; // 知らない語
}

/*!
 * @brief 札 3: `[b/p/g] Buy`（doc の命令列。`shop.c:1704` ほか）。
 * @details 中身は `/` 区切りで、1 文字のキーか語（`Esc` `Space/PgDn` `-/PgUp`）。
 *
 * **積むのは最初の 1 キーだけ**——`[b/p/g]` の 3 つは同じ命令の同義キーで、
 * 同じ span に選択肢を重ねると左右移動が足踏みする（`fc_main.cpp` のセーブ選択で
 * 踏んだ穴と同じ）。
 *
 * ## 品名の角括弧を拾わない担保
 * 防具の `[1]` や `[3,+5]` も角括弧である。そこで
 * - **数字 1 文字はキーに採らない**（Frox の命令列に数字の角括弧は無い）
 * - `,` や倍数字は語としても不成立
 * - **`]` の直後が「空白＋札の頭」**であること（命令列は必ず `] Buy` の形。
 *   品名では `[1] (+2)` や行末になる）。**札の頭は英字か 2 バイト文字**
 *   （`is_choice_label_head`。日本語の `] 買う` を落としていた）
 */
bool parse_bracket_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
    if ((p >= text.size()) || (text[p] != '[')) {
        return false;
    }
    const std::size_t close = text.find(']', p + 1);
    if ((close == std::string::npos) || (close == p + 1)) {
        return false;
    }
    if ((close - p - 1) > 16) {
        return false; // 長い角括弧は本文（`[Press Any Key to Continue]` の類）
    }
    if ((close + 2 >= text.size()) || (text[close + 1] != ' ')
        || !is_choice_label_head(text[close + 2])) {
        return false;
    }

    out = ChoiceTokenGroup{};
    out.begin = p;

    std::size_t q = p + 1;
    while (q < close) {
        std::size_t sep = text.find('/', q);
        if ((sep == std::string::npos) || (sep > close)) {
            sep = close;
        }
        const std::size_t len = sep - q;
        if (len == 0) {
            return false;
        }
        int key = -2;
        if (len == 1) {
            const auto c = static_cast<unsigned char>(text[q]);
            if ((c <= 0x20) || (c >= 0x7F) || ((c >= '0') && (c <= '9')) || (c == '[')
                || (c == ']')) {
                return false; // 数字・空白・記号外れはキーの札ではない
            }
            key = c;
        } else {
            key = bracket_word_key(text, q, len);
            if (key == -2) {
                return false; // 知らない語 → 札ではない（品名などの本文）
            }
        }
        if ((key > 0) && out.keys.empty()) {
            out.key_spans.emplace_back(q, len);
            out.keys.push_back(key);
        }
        q = sep + 1;
    }
    out.bracket = !out.keys.empty();
    return !out.keys.empty();
}

//! 3 書式の札を 1 行から全部読む。開始位置は行頭か空白の直後。
std::vector<ChoiceTokenGroup> scan_choice_tokens(const std::string &text)
{
    std::vector<ChoiceTokenGroup> groups;
    for (std::size_t p = 0; p < text.size(); ++p) {
        if ((p != 0) && (text[p - 1] != ' ')) {
            continue;
        }
        ChoiceTokenGroup g;
        if (parse_bracket_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
            continue;
        }
        if (parse_paren_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
            continue;
        }
        if (parse_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
        }
    }
    return groups;
}

/*!
 * @brief 枠を敷く範囲の終わり。次の札の直前まで。ただし**空白 3 連で切る**
 * （同じ行の別の桁に値段や重さが並ぶ。`gb_menu.cpp` と同じ理屈）。
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
 * @brief 枠を**行末まで**伸ばしてよい行か（`(1)` の札しか無い行だけ）。
 * @details オプションの根はラベルが `(1) General Options` と 1 行 1 札で並ぶ。
 * 空白 3 連で切るとラベルの途中で枠が終わる画面があるため。
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

//! Term の行番号 → ミラーの添字（`source_row` で引く。無ければ -1）。
int mirror_line_of(const GameFrame &frame, int row)
{
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].source_row == row) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

/* ============================================================== コアのカーソル */

bool fill_core_cursor(GameFrame &frame)
{
    frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
    frame.menu_core_cursors.clear();

    if (frame.menu_term_lines.empty()) {
        return false;
    }

    /*
     * 枠 `+----` の行（コマンドメニューの上辺と下辺。`util.c` の
     * `put_str("+----…----+", basey, basex)`）。**枠の無いミラーでは探さない**
     * ——`>` は階段の字でもあり、ミラーには前の画（ASCII の地図）が残っている。
     */
    std::vector<int> frame_rows;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].text_utf8.find("+----") != std::string::npos) {
            frame_rows.push_back(static_cast<int>(i));
        }
    }
    if (frame_rows.size() < 2) {
        return false;
    }

    static const std::string mark = "> ";
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        /*
         * **箱の中の行だけ**（上と下の両方に枠の行があること）。箱の外は地図の
         * 消し残りで、` > `（暗がりに挟まれた階段）が印と同じ字面になりうる。
         * 箱の中身は `|    |` で塗り直されているので地図は漏れない。
         */
        bool above = false;
        bool below = false;
        for (const int r : frame_rows) {
            above = above || (r < static_cast<int>(i));
            below = below || (r > static_cast<int>(i));
        }
        if (!above || !below) {
            continue;
        }

        const std::string &text = frame.menu_term_lines[i].text_utf8;
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
             * 枠の右端は「空白 3 連（隣の列の項目）」か「枠線 `|`」の早い方。
             * 小メニューは親の箱に重ねて描かれる（`basey += 2; basex += 8`）ので、
             * 親の印と子の印が同時に見える。全部積む（`menu_core_cursors`）。
             */
            std::size_t end = text.find("   ", after);
            const std::size_t bar = text.find('|', after);
            if (bar != std::string::npos) {
                end = (end == std::string::npos) ? bar : (std::min)(end, bar);
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

/* ================================================================== プロンプト */

void fill_row0_prompt(GameFrame &frame)
{
    frame.prompt = PromptBar{};
    frame.numeric = NumericInput{};

    /*
     * `[y/n]` はメッセージの箱に出る（`get_check()` → `msg_prompt()`）。箱は
     * **最大 10 行 × 幅 MIN(72, wid-13)**（`ui_msg_rect()`。`xtra2.c:3496`）で、
     * 長い前置きが積まれると問いの行が 3 行目より下へ落ちる——wizard の確認
     * （`^A` の初回）で実際に落ちた（2026-08-24）。箱の全行を**下から**試す
     * （メッセージは下の行ほど新しい）。
     *
     * **箱の幅で切ってから照合する**のが要——行 1 以降の右端には状態列の字
     * （`LEVEL` `EXP` …）が残っており、行の素の末尾は `[y/n]` にならない。
     * 切った中身が地形の字で偶然 `[y/n]` に終わることは実質無い（5 字の並び）。
     */
    int term_cols = 0;
    int term_rows = 0;
    (void)fc_term_size(&term_cols, &term_rows);

    /*
     * **箱の場所はコアに聞く**（FH-13）。決め打ちの `MIN(72, wid-13)` で切って
     * いたが、**店・我が家・建物・クエストでは `rect(0,0,80,3)` へ移る**ので、
     * 68 桁目以降に出た `[y/n]` を丸ごと取りこぼしていた（実機で見つけたこと
     * 2026-08-24「y/n の選択がカーソルでできない」）。
     * 聞けないとき（`init_angband()` 前）だけ従来の決め打ちへ落ちる。
     */
    int box_x = 0;
    int box_y = 0;
    int box_w_i = (term_cols > 13) ? (std::min)(72, term_cols - 13) : 72;
    int box_h = 10;
    if (fc_msg_rect(&box_x, &box_y, &box_w_i, &box_h) != FC_OK) {
        box_x = 0;
        box_y = 0;
        box_w_i = (term_cols > 13) ? (std::min)(72, term_cols - 13) : 72;
        box_h = 10;
    }
    /*
     * 高さは**いま使っている行数**なので 0 にもなる。少なくとも 3 行は見る
     * ——`msg_prompt()` は箱へ書き足す途中の行数を返しうるし、長い品名の
     * 折り返しで尻尾が下の行へ落ちる。
     */
    const int begin_row = (std::max)(0, box_x >= 0 ? box_y : 0);
    const int rows = (std::max)(3, box_h);
    const int end_row = (std::min)((term_rows > 0) ? term_rows : begin_row + rows,
        begin_row + rows);
    const std::size_t clip_begin = static_cast<std::size_t>((std::max)(0, box_x));
    const std::size_t clip_end = clip_begin + static_cast<std::size_t>((std::max)(1, box_w_i));

    PromptBar prompt;
    for (int y = end_row - 1; y >= begin_row; --y) {
        std::string clipped = term_row_text(y);
        if (clipped.size() > clip_end) {
            clipped.resize(clip_end);
        }
        if (clip_begin > 0) {
            clipped = (clipped.size() > clip_begin) ? clipped.substr(clip_begin) : std::string();
        }
        if (!parse_prompt_line(clipped, prompt)) {
            continue;
        }
        /*
         * 桁は**行の頭から数えた位置**で運ぶ（画面は行の字に枠を重ねる）。
         * 箱が右へずれている画面のために、切り落とした分を足し戻す。
         */
        if (clip_begin > 0) {
            for (PromptChoice &choice : prompt.choices) {
                choice.begin += static_cast<int>(clip_begin);
            }
        }
        frame.prompt = std::move(prompt);
        frame.prompt.line_index = mirror_line_of(frame, y);
        return;
    }

    /*
     * 個数も**箱の全行を下から**見る（FH-15）。`get_quantity()` は
     * `prt(prompt, 0, 0)` なので行 0 だが、**店の個数は `msg_input_num()`**
     * ＝メッセージ行へ書く（`message.c:626`）ので、店の箱（行 0〜2）の
     * どこにでも出る。行 0 だけを見ていたので拾えていなかった。
     */
    NumericInput numeric;
    for (int y = end_row - 1; y >= begin_row; --y) {
        std::string clipped = term_row_text(y);
        if (clipped.size() > clip_end) {
            clipped.resize(clip_end);
        }
        if (clip_begin > 0) {
            clipped = (clipped.size() > clip_begin) ? clipped.substr(clip_begin) : std::string();
        }
        if (!parse_numeric_input_line(clipped, numeric)) {
            continue;
        }
        if (clip_begin > 0) {
            numeric.value_begin += static_cast<int>(clip_begin);
        }
        frame.numeric = std::move(numeric);
        frame.numeric.line_index = mirror_line_of(frame, y);
        return;
    }
}

/*!
 * @brief **連番で続く最初の letter の並び**だけを残す（フック #37 の受け）。
 * @param per_line 行ごとの札（呼び出し側が作ったもの。ここで間引く）
 * @return 1 件でも残ったか
 *
 * @details 品選びの窓（`obj_prompt`）は**下の画を消さずに重なる**ので、
 * 店で開くと画面に letter の並びが 2 つ出る——窓の中の持ち物（`a) b)`）と、
 * 下に残った店の在庫（`a) 〜 o)`）である。**同じ letter が 2 度出る**ので、
 * 素通しにすると「品 a を選んだつもりで店の a を指す」札ができる。
 *
 * 窓は**必ず上に描かれる**（`doc_sync_menu()` が `ui_doc_menu_rect()` の頭から置く）。
 * そこで**上から見て最初に見つかる連番の並び**だけを採る。変愚が店で使っている手と
 * 同じである（`presentation/bridge/presentation_bridge.cpp` の
 * `collect_item_prompt_choices()`。あちらは行 0 の `(a-x)` から `lo` を読むが、
 * Frox の `obj_prompt` は範囲を書かないので `a` から数える）。
 *
 * **角括弧の札は 1 つも残さない。** 下に残っている店の命令列がそれである。
 */
bool keep_first_letter_run(std::vector<std::vector<ChoiceTokenGroup>> &per_line)
{
    int expected = 'a';
    bool started = false;
    bool done = false;
    bool kept = false;
    for (auto &groups : per_line) {
        std::vector<ChoiceTokenGroup> take;
        if (!done) {
            for (const ChoiceTokenGroup &g : groups) {
                if (g.bracket || g.paren || g.keys.empty()) {
                    continue;
                }
                if (g.keys[0] != expected) {
                    continue;
                }
                take.push_back(g);
                started = true;
                kept = true;
                //! `z` の次は `A`（`slot_label()` と同じ並び）。
                expected = (expected == 'z') ? 'A' : (expected + 1);
                break; //!< 1 行から採るのは 1 つだけ
            }
            /*
             * **並びが途切れたら、そこから先は採らない。** 窓の下の在庫がまた
             * `a)` から始まるので、止めないと 2 つ目の並びを拾い直してしまう。
             * ただし**始まる前の行は数えない**（窓の見出しと合計の行が上にある）。
             */
            if (started && take.empty()) {
                done = true;
            }
        }
        /*
         * **採らなかった行は必ず空にする。** ここで `break` して抜けると、
         * 残りの行は元の札を持ったままになり、呼び手がそれを積んでしまう
         * （2026-08-28 に実測で踏んだ。窓の 2 件のはずが 29 件出た）。
         */
        groups = std::move(take);
    }
    return kept;
}

/* ================================================================ 選択肢の走査 */

bool fill_menu_choices(GameFrame &frame)
{
    if (!frame.menu_open || frame.menu_term_lines.empty()) {
        return false;
    }

    std::vector<std::vector<ChoiceTokenGroup>> per_line;
    per_line.reserve(frame.menu_term_lines.size());
    bool has_bracket = false;
    for (const TermMirrorLine &line : frame.menu_term_lines) {
        per_line.push_back(scan_choice_tokens(line.text_utf8));
        for (const ChoiceTokenGroup &g : per_line.back()) {
            has_bracket = has_bracket || g.bracket;
        }
    }

    /*
     * **命令列のある画面で、コアが何も訊いていない間は命令列だけを札にする**
     * （FH-14。2026-08-24 に決めた「コマンド行だけに限る」）。
     *
     * 店の画面は下段が `[b/p/g] Buy …` の命令列で、その上に品行 `a) …` が並ぶ。
     * **待ち受け中に押した letter は「品の指定」ではなく「店のコマンド」**である
     * （`shop.c:1552` の switch。`d)` の品を選ぶつもりで押すと売りの口が開く）。
     * 品行を札にしてよいのは、コアが「どの品を買う?」と**訊いている間だけ**。
     *
     * その見分けは**メッセージ行に中身があるか**で付く——店の待ち受けは
     * `inkey_special()` の**直後**に `msg_line_clear()` する（`shop.c:1541`）ので
     * 待ち受け中は空（`doc_line_count == 0`）、`msg_command()` が問いを書いている
     * 間だけ 1 以上になる（`message.c:462`）。**語や画面名で名指ししない**ので
     * M1 の日本語化でも壊れない。
     *
     * 命令列の無い画面（誕生・オプション・品選びの doc UI）は素通し——
     * `[Press Any Key to Continue]` の類は「`]` の直後が空白＋札の頭」で弾かれる。
     *
     * **逆に、コアが訊いている間は品行だけにする**（2026-08-28。変愚と同じ仕様）。
     * `Buy which item?` の答えは品の letter だけで、命令列の札は 1 つも受け付けない
     * （`shop.c:2318` の `if (cmd < 'a' || cmd > 'z') continue;`）。それでいて
     * `[b/p/g]` `[s/d]` `[x]` の送るキーは `b` `s` `x` ——**どれも a〜z の範囲**なので、
     * 積んだままにすると命令のつもりで選んだ札が**品 b・品 s・品 x を買う**。
     * 変愚は同じ場面を `MenuChoiceScreen::ItemPrompt` で品行だけに絞っている
     * （`presentation/bridge/presentation_bridge.cpp` の `collect_item_prompt_choices`）。
     */
    /*
     * ---- **品選びの窓が開いている間は、窓の中の品だけ**（#37。2026-08-28）----
     *
     * `obj_prompt()` の問いは `msg_line` ではなく doc の窓へ出るので、
     * 下の「メッセージ行に中身があるか」では見分けられない。旗はコアが立てる。
     * 2026-08-28 に気づいた「店の譲る（売る）を選択した際に、売る対象の
     * リストにカーソルが移らない」——`no_selling`（既定 ON）の道はここを通る。
     */
    if (fc_obj_prompt_active() != 0) {
        if (keep_first_letter_run(per_line)) {
            for (std::size_t i = 0; i < per_line.size(); ++i) {
                if (frame.menu_choices.size() >= kMenuChoiceMax) {
                    break;
                }
                if (!per_line[i].empty()) {
                    emit_choices_for_line(frame, i, per_line[i]);
                }
            }
            return !frame.menu_choices.empty();
        }
        /*
         * 窓は開いているのに連番が 1 つも無い（品が 0 件・呪文の別書式など）。
         * **命令列だけを残す道へは落とさない**——下に残っている店の命令は
         * いま押しても効かないので、素通しにして下の従来の道へ任せる。
         */
    }

    int msg_lines = 0;
    const bool msg_known = (fc_msg_rect(nullptr, nullptr, nullptr, &msg_lines) == FC_OK);
    const bool asking = msg_known && (msg_lines > 0);
    const bool commands_only = has_bracket && msg_known && !asking
        && (fc_obj_prompt_active() == 0);
    const bool items_only = has_bracket && asking;

    for (std::size_t i = 0; i < per_line.size(); ++i) {
        if (frame.menu_choices.size() >= kMenuChoiceMax) {
            break;
        }
        std::vector<ChoiceTokenGroup> groups = std::move(per_line[i]);
        if (commands_only || items_only) {
            const bool keep = commands_only; // 残すのは bracket == keep の札だけ
            groups.erase(std::remove_if(groups.begin(), groups.end(),
                             [keep](const ChoiceTokenGroup &g) { return g.bracket != keep; }),
                groups.end());
            if (groups.empty()) {
                continue;
            }
        }
        emit_choices_for_line(frame, i, groups);
    }
    return !frame.menu_choices.empty();
}


/* ======================================== 品選びの窓の札の検査（#37。2026-08-28） */

int obj_prompt_choice_selftest()
{
    /*
     * 店で `s`（売る）を押した実物の画（`fc_protocol_driver.py` で撮った）。
     * 行 0〜4 が `obj_prompt` の窓、行 5 以降が下に残った店、行 24 が命令列である。
     * 日本語は **UTF-8 の 16 進**で書く（`command_label_selftest()` と同じ理由）。
     */
    static const char *const kLines[] = {
        "\xe6\x8c\x81\xe3\x81\xa1\xe7\x89\xa9",       /* 持ち物（窓の見出し） */
        "  a) | \xe5\x89\xa3                     13.0 lb",  /* a) 剣 */
        "  b) { \xe7\x9f\xa2                      5.0 lb",  /* b) 矢 */
        "                                     18.0 lb",        /* 合計（札は無い） */
        "\xe3\x81\xa9\xe3\x82\x8c\xe3\x82\x92\xe5\xa3\xb2\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x99\xe3\x81\x8b\xef\xbc\x9f", /* どれを売りますか？ */
        "    Item Description",
        "  a) ? \xe6\x9c\xac                       3.0 lbs    171", /* 店の a) 本 */
        "  b) ? \xe6\x9c\xac                       3.0 lbs   1710",
        "  c) ~ \xe7\x9f\xa2\xe7\xad\x92 [60]              1.0 lbs     39",
        "[b/p/g] \xe8\xb2\xb7\xe3\x81\x86  [s/d] \xe5\xa3\xb2\xe3\x82\x8b", /* 命令列 */
    };

    std::vector<std::vector<ChoiceTokenGroup>> per_line;
    for (const char *const line : kLines) {
        per_line.push_back(scan_choice_tokens(line));
    }

    int failed = 0;
    if (!keep_first_letter_run(per_line)) {
        std::fprintf(stderr, "[frox] selftest: obj-prompt choices: nothing kept\n");
        return 1;
    }

    //! 残ってよいのは**行 1 の a と行 2 の b だけ**。
    for (std::size_t i = 0; i < per_line.size(); ++i) {
        const std::size_t want = ((i == 1) || (i == 2)) ? 1u : 0u;
        if (per_line[i].size() != want) {
            std::fprintf(stderr,
                "[frox] selftest: obj-prompt choices: line %d kept %d (want %d): '%s'\n",
                (int)i, (int)per_line[i].size(), (int)want, kLines[i]);
            ++failed;
            continue;
        }
        if ((want == 1u) && (per_line[i][0].keys.at(0) != (int)('a' + i - 1))) {
            std::fprintf(stderr, "[frox] selftest: obj-prompt choices: line %d key '%c'\n",
                (int)i, (char)per_line[i][0].keys.at(0));
            ++failed;
        }
    }
    return failed;
}

/* ============================================== 命令列の札の検査（2026-08-28） */

int command_label_selftest()
{
    struct Case {
        const char *line;
        const char *bracket_keys; //!< 拾うべき札 3 のキーを並べたもの
        int plain; //!< 拾うべき札 1 / 札 2（品行）の数
    };
    /*
     * 綴りは実物から採った——英語は `shop.c:1714`、日本語は
     * `frox/lang/ja/ui/messages.ja.txt` の同じ鍵の訳文である。
     * 日本語は **UTF-8 の 16 進で書く**——このファイルは BOM 付き UTF-8 だが、
     * MSVC は狭い文字列を実行時の符号（CP932）へ落とすので、
     * そのまま書くと `text_utf8` と照らし合わなくなる。
     */
    static const Case kCases[] = {
        //! 店の帯の 1 行目（英語）。
        { "[b/p/g] Buy  [s/d] Sell  [x] Examine  [?] Help  [q/Esc] Exit", "bsx?q", 0 },
        //! 同じ行の訳文。`] 買う` を拾えなければパッドから店が使えない。
        { "[b/p/g] \xe8\xb2\xb7\xe3\x81\x86  [s/d] \xe5\xa3\xb2\xe3\x82\x8b  "
          "[x] \xe8\xaa\xbf\xe3\x81\xb9\xe3\x82\x8b  [?] \xe3\x83\x98\xe3\x83\xab\xe3\x83\x97  "
          "[q/Esc] \xe5\x87\xba\xe3\x82\x8b",
            "bsx?q", 0 },
        //! 3 行目の訳文。`Space` と `-` の組も日本語の札を従える。
        { "[Space/PgDn] \xe6\xac\xa1\xe3\x81\xae\xe9\xa0\x81  "
          "[-/PgUp] \xe5\x89\x8d\xe3\x81\xae\xe9\xa0\x81  "
          "[U] \xe4\xb8\x8d\xe8\xa6\x81\xe3\x81\xaa\xe5\x93\x81\xe3\x82\x92\xe9\x9a\xa0\xe3\x81\x99 (12)",
            " -U", 0 },
        //! 防具の `[2,+0]` は札ではない。拾うのは行頭の `a)` だけ。
        { "  a) \xe9\x89\x84\xe3\x81\xae\xe5\xb8\xbd\xe5\xad\x90 [2,+0]  120", "", 1 },
        //! 同じもので、角括弧の後ろが日本語だった場合（頭の検査を通る形）。
        { "  a) \xe9\x89\x84\xe3\x81\xae\xe5\xb8\xbd\xe5\xad\x90 [2,+0] \xe3\x81\xae\xe5\x86\x99\xe3\x81\x97",
            "", 1 },
        //! 本文の角括弧。
        { "[Press Any Key to Continue]", "", 0 },
    };

    int failed = 0;
    for (const Case &c : kCases) {
        const std::vector<ChoiceTokenGroup> groups = scan_choice_tokens(c.line);
        std::string keys;
        int plain = 0;
        for (const ChoiceTokenGroup &g : groups) {
            if (!g.bracket) {
                ++plain;
                continue;
            }
            for (const int key : g.keys) {
                keys.push_back(static_cast<char>(key));
            }
        }
        if (keys != c.bracket_keys) {
            std::fprintf(stderr, "[frox] selftest: command labels '%s' -> '%s' (want '%s')\n",
                c.line, keys.c_str(), c.bracket_keys);
            ++failed;
        }
        if (plain != c.plain) {
            std::fprintf(stderr, "[frox] selftest: command labels '%s' -> %d plain (want %d)\n",
                c.line, plain, c.plain);
            ++failed;
        }
    }
    return failed;
}

/* ================================================ 個数の綴りの検査（FH-15） */

int numeric_spelling_selftest()
{
    struct Case {
        const char *line;
        bool want;
        int min;
        int max;
        int value;
    };
    //! 綴りは実物から採った（`util.c:3246` と `message.c:626`）。
    static const Case kCases[] = {
        { "Quantity (1-5): 1", true, 1, 5, 1 },
        { "Quantity (1 to 5): 1", true, 1, 5, 1 },
        { "Quantity (1 to 21): ", true, 1, 21, 1 },
        { "Quantity (2-40): 12", true, 2, 40, 12 },
        //! 札や本文を数値入力と読まないこと。
        { "  a) 6 Wooden Torches (with 1500 turns of light)", false, 0, 0, 0 },
        { "Really sell a Broad Sword for 150 gp? [y/n]", false, 0, 0, 0 },
        { "Buy which item (Esc when done)?", false, 0, 0, 0 },
    };

    int failed = 0;
    for (const Case &c : kCases) {
        NumericInput got;
        const bool ok = parse_numeric_input_line(c.line, got);
        if (ok != c.want) {
            std::fprintf(stderr, "[frox] selftest: numeric %s for '%s'\n",
                ok ? "matched but should not" : "did not match", c.line);
            ++failed;
            continue;
        }
        if (!c.want) {
            continue;
        }
        if ((got.min != c.min) || (got.max != c.max) || (got.value != c.value)) {
            std::fprintf(stderr,
                "[frox] selftest: numeric read %d..%d = %d (want %d..%d = %d) for '%s'\n",
                got.min, got.max, got.value, c.min, c.max, c.value, c.line);
            ++failed;
        }
    }
    return failed;
}

} // namespace fc
