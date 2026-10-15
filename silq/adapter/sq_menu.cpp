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
#include "bridge/symbol_prompt.h"
#include "bridge/look_screen_cover.h"
#include "frame/message_more.h"

#include "sq_lang_c.h"
#include "sq_shim.h"
#include "sq_text.h"

#include <algorithm>
#include <cstdlib>
#include <map>
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
        if (lead >= 0x80) wide = true; // Half-width kana also use the core encoding.
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

    const std::string text = presentation::strip_message_repeat_suffix(trim_right(raw));
    /*
     * 見る（照準の途中）の帯。行 0 の末尾の `[...]` は `xtra2.c` の `info` で、次の 5 通りがある:
     * `(t)arget, (m)anual, <dir>` / `(m)anual, <dir>` / `(t)arget, (a)uto, <dir>` / `(a)uto, <dir>` /
     * `<space>, <tab>, <dir>`。日本語ではカタログの訳が入る（`messages.ja.txt` の
     * 「(t) 狙う, (m) 手動, <向き>」）ので、英語の綴りだけで照合すると日本語では帯が出なかった
     * （2026-10-07。近くに何も無い所で見ると必ずこの形になる）。英語と訳の両方で照合し、
     * 帯の字（見出し・選択肢・脚注）も日本語のときは日本語にする。
     * 長い綴りが短い綴りを含む（`(t)arget, (m)anual, <dir>` ⊃ `(m)anual, <dir>`）ので、長いほうから当てる。
     */
    static const char *const kLookInfos[] = {
        "(t)arget, (m)anual, <dir>", "(m)anual, <dir>", "(t)arget, (a)uto, <dir>", "(a)uto, <dir>", "<space>, <tab>, <dir>" };
    const char *look_info = nullptr;
    for (const char *const info : kLookInfos) {
        const std::string translated = tr_utf8(info);
        if ((text.find(info) != std::string::npos)
            || (!translated.empty() && (translated != info) && (text.find(translated) != std::string::npos))) {
            look_info = info;
            break;
        }
    }
    if (look_info != nullptr) {
        // 見る・狙うの途中で `r`（モンスターの思い出）などが画面を退避して重なっている間は、帯を作らない（`look_screen_cover.h`）。
        if (presentation::look_screen_cover().covered(sq_screen_depth())) {
            out = PromptBar{};
            return false;
        }
        const bool ja = sq_lang_enabled() != 0;
        const auto label = [ja](const char *en) -> std::string {
            if (!ja) return en;
            static const struct { const char *en; const char8_t *ja; } kTable[] = {
                { "Look: ", u8"見る: " }, { "Manual mode", u8"手動モード" }, { "Automatic target mode", u8"自動モード" },
                { "Select target", u8"ここを対象にする" }, { "Next interesting", u8"次の注目地点" },
                { "Previous interesting", u8"前の注目地点" },
                { "LB/RB: select action  A/Enter: run  D-pad: move look cursor", u8"LB/RB: 操作を選ぶ  A/Enter: 実行  十字キー: カーソル移動" } };
            for (const auto &row : kTable) if (std::strcmp(row.en, en) == 0) return reinterpret_cast<const char *>(row.ja);
            return en;
        };
        const auto add=[&](int key,const char *en){PromptChoice c{};c.key=key;c.label_utf8=label(en);if(!out.text_utf8.empty())out.text_utf8+="  ";out.text_utf8+='[';c.begin=static_cast<int>(out.text_utf8.size());out.text_utf8+=c.label_utf8;c.len=static_cast<int>(c.label_utf8.size());out.text_utf8+=']';out.choices.push_back(std::move(c));};
        const std::string info(look_info);
        out.text_utf8=label("Look: ");
        /*
         * 見ている物の説明。行 0 の頭にある「<名前> <状態> [(r)ecall, <info>]」（敵）や「<名前> [<info>]」（アイテム・地形）の
         * `[` より前を、帯の見出しのすぐ後ろへ残す（`xtra2.c` の `target_set_interactive_aux`）。
         * 選択肢だけを出すと、何を見ているか帯のどこにも出なかった（調-13）。
         * 続きの待ちの印（`-more-`）は説明に含めない。
         */
        {
            std::string subject;
            if(const std::size_t open=text.rfind('[');open!=std::string::npos)subject=text.substr(0,open);
            const auto trim=[](std::string &s){while(!s.empty()&&s.back()==' ')s.pop_back();std::size_t i=0;while(i<s.size()&&s[i]==' ')++i;s.erase(0,i);};
            trim(subject);
            for(const char *const more:{"-more-",reinterpret_cast<const char *>(u8"-続く-")}){const std::string m(more);if(subject.size()>=m.size()&&subject.compare(subject.size()-m.size(),m.size(),m)==0){subject.erase(subject.size()-m.size());trim(subject);}}
            out.text_utf8+=subject;
        }
        if(info.find("(m)anual")!=std::string::npos)add('m',"Manual mode");
        if(info.find("(a)uto")!=std::string::npos)add('a',"Automatic target mode");
        if(info.find("(t)arget")!=std::string::npos)add('t',"Select target");
        if(info.find("<space>")!=std::string::npos)add(' ',"Next interesting");
        if(info.find("<tab>")!=std::string::npos)add('\t',"Previous interesting");
        out.footer_utf8=label("LB/RB: select action  A/Enter: run  D-pad: move look cursor");
        return !out.choices.empty();
    }
    if(text.size()<5)return false;
    const std::size_t begin=text.size()-5;
    if(text[begin]!='['||text[begin+4]!=']'||(text[begin+2]!='/'&&text[begin+2]!=',')
        ||(text[begin+1]!='y'&&text[begin+1]!='Y')||(text[begin+3]!='n'&&text[begin+3]!='N'))return false;
    struct Option {const char *label_ascii;char key;};
    const Option kOptions[]={{"Yes",text[begin+1]},{"No",text[begin+3]}};

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
void collect_list_choices(GameFrame &frame, const std::vector<char> &expected,int only_column=-1)
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
            if(only_column>=0&&x!=only_column)continue;
            const char label = cells[x];
            if ((cells[x + 1] != ')')
                || (std::find(expected.begin(), expected.end(), label) == expected.end())) {
                continue;
            }
            if ((only_column < 0) && (x > 0) && !cell_is_blank(cells[x - 1])) {
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
                break; // 名乗りに無いか、もう通り過ぎた札。ここで打ち切る
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
        if(only_column>=0){
            choice.label_utf8=sjis_to_utf8(cells+candidate.col,n-candidate.col);
            const auto gap=choice.label_utf8.find("   ",3);if(gap!=std::string::npos)choice.label_utf8.resize(gap);
        }
        choice.span_begin = begin_byte;
        choice.span_len = (std::max)(0, end_byte - begin_byte);
        choice.key_begin = begin_byte;
        choice.key_len = 1;
        choice.key = static_cast<unsigned char>(candidate.label);
        frame.menu_choices.push_back(choice);
    }
}

} // namespace

static void fill_row0_prompt_inner(GameFrame &frame);
void fill_row0_prompt(GameFrame &frame)
{
    const presentation::LookScreenCover::Frame cover_frame(presentation::look_screen_cover()); // 見るの帯を作らなかったフレームで基準を忘れる
    fill_row0_prompt_inner(frame);
}
static void fill_row0_prompt_inner(GameFrame &frame)
{
    frame.prompt = PromptBar{};
    frame.numeric = NumericInput{};

    const std::string row0 = term_row_utf8(0);
    if (row0.empty()) {
        return;
    }

    if (presentation::is_monster_symbol_prompt(row0)) {
        presentation::expose_ascii_symbol_prompt(frame, row0);
        return;
    }

    PromptBar prompt;
    NumericInput numeric;
    if (trim_right(row0) == trim_right(tr_utf8("Please verify ABORTING by typing the '@' sign: "))) {
        prompt.text_utf8=row0;
        prompt.choices.push_back({sq_lang_enabled()?reinterpret_cast<const char*>(u8"取消 (Esc)"):"Cancel (Esc)",27,0,0});
        prompt.choices.push_back({"@",'@',0,0});
        frame.prompt=std::move(prompt);
    } else if (frame.menu_term_lines.empty() && presentation::message_more_suffix(trim_right(row0))) {
        /*
         * 地図の上の `-続く-`（`util.c` の `msg_flush()`）。メニューの外ではミラーが開かないので、
         * 行 0 をここで渡さないと frame に待っている印が 1 つも無い（持ち物メニューから食べた後に
         * 「満腹だ！」で止まって見えた。2026-09-25）。Angband・Frox と同じく帯へ出す。
         */
        frame.prompt.text_utf8 = trim_right(row0);
        frame.prompt.line_index = -1;
        return;
    } else if (parse_prompt_line(row0, prompt)) {
        frame.prompt = std::move(prompt);
    } else if (parse_numeric_input_line(row0, numeric)) {
        frame.numeric = std::move(numeric);
    } else if(sq_text_input_active()!=0&&frame.menu_term_lines.empty()) {
        int cx=0,cy=0;if(sq_term_cursor(&cx,&cy,nullptr)==SQ_OK){frame.prompt.text_utf8=term_row_utf8(cy);frame.menu_term_curs_col=cx;}
        return;
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

namespace {
std::string native_menu_title;
int list_scope_toggle_key(const std::string &row0)
{
    if (row0 == tr_utf8("Monsters you can see (press [ to toggle):")
        || row0 == tr_utf8("Monsters on screen (press [ to toggle):")) return '[';
    if (row0 == tr_utf8("Objects you can see (press ] to toggle):")
        || row0 == tr_utf8("Objects on screen (press ] to toggle):")) return ']';
    return 0;
}
bool has_menu_label(const GameFrame &frame,const char *label) {
    const auto translated=tr_utf8(label);
    for(const auto &line:frame.menu_term_lines)if(line.text_utf8.find(translated)!=std::string::npos)return true;
    return false;
}
std::string panel_text(int column) {
    std::string text;char cells[kRowCells];unsigned char attrs[kRowCells];int cols=0,rows=0;sq_term_size(&cols,&rows);
    for(int y=2;y<rows;++y){int n=sq_term_row(y,cells,attrs,kRowCells);if(n<=column)continue;
        auto line=sjis_to_utf8(cells+column,n-column);while(!line.empty()&&line.back()==' ')line.pop_back();
        text+=line+"\n";
    }
    return text;
}
void attach_native_cursor(GameFrame &frame,int column,bool smithing) {
    int x=0,y=0,v=0;sq_term_cursor(&x,&y,&v);
    char cells[kRowCells];unsigned char attrs[kRowCells];
    for(auto &choice:frame.menu_choices){
        if(choice.line_index<0||choice.line_index>=int(frame.menu_term_lines.size()))continue;
        const auto &line=frame.menu_term_lines[choice.line_index];
        const int n=sq_term_row(line.source_row,cells,attrs,kRowCells);
        if(smithing&&column+3<n&&attrs[column+3]==8){choice.available=false;choice.reason_utf8=tr_utf8("Invalid choice.");}
        if((column<n&&attrs[column]==14)||(line.source_row==y&&x==column&&frame.menu_core_cursor.line_index<0)){
            frame.menu_core_cursor=choice;frame.menu_core_cursor.key=0;
            if(smithing){choice.description_utf8=term_row_utf8(0)+"\n"+panel_text(column<16?18:(column<36?38:68));if(!choice.available&&!choice.description_utf8.empty())choice.reason_utf8=choice.description_utf8;}
        }
    }
}
void named_choices(GameFrame &frame,const char *const *labels,int column,bool smithing) {
    for(int k=0;labels[k];++k){
        const auto label=tr_utf8(labels[k]);
        for(size_t i=0;i<frame.menu_term_lines.size();++i){
            const auto start=frame.menu_term_lines[i].text_utf8.find(label);if(start==std::string::npos)continue;
            MenuChoice c;c.line_index=int(i);c.span_begin=int(start);c.span_len=int(label.size());c.key_begin=int(start);c.key_len=1;c.key=labels[k][labels[k][0]=='('?1:0];c.description_utf8=label;frame.menu_choices.push_back(std::move(c));break;
        }
    }
    attach_native_cursor(frame,column,smithing);
}
bool highlighted_choice_menu(GameFrame &frame) {
    int cols=0,rows=0,tx=0,ty=0,tv=0;sq_term_size(&cols,&rows);sq_term_cursor(&tx,&ty,&tv);
    std::map<int,std::vector<char>> columns;std::map<int,int> active_rows;
    char cells[kRowCells];unsigned char attrs[kRowCells];
    for(int y=1;y<rows;++y){
        const int n=sq_term_row(y,cells,attrs,kRowCells);
        for(int x=0;x+2<n;++x){
            const char key=cells[x];
            if(!((key>='a'&&key<='z')||(key>='A'&&key<='Z'))||cells[x+1]!=')'||!cell_is_blank(cells[x+2])||(x>0&&!cell_is_blank(cells[x-1])))continue;
            columns[x].push_back(key);
            if(attrs[x]==14||(attrs[x]==4&&tx==x&&ty==y))active_rows[x]=y;
        }
    }
    int chosen=-1;
    for(const auto &[col,keys]:columns){
        if(keys.size()<2||!active_rows.count(col))continue;
        if(chosen<0||col>chosen)chosen=col;
    }
    if(chosen<0)return false;
    if(columns.count(tx)&&columns[tx].size()>=2&&active_rows.count(tx)&&active_rows[tx]==ty)chosen=tx;
    collect_list_choices(frame,columns[chosen],chosen);
    for(auto &choice:frame.menu_choices)if(frame.menu_term_lines[choice.line_index].source_row==active_rows[chosen]){
        if(chosen==17 && has_menu_label(frame,"Abilities")){
            /* 能力の一覧（2 段目）は、原作が英字で強調を動かすだけで、買うのは Enter（cmd4.c abilities_menu2）。
             * 強調中の行は key=0（決定＝Enter）にし、ほかの行は英字のまま（押すと強調が移る）。
             * 技能の一覧（1 段目）は英字で即選べるので触らない。 */
            choice.description_utf8=panel_text(42);choice.key=0;
        }
        else{
            for(const auto &line:frame.menu_term_lines)choice.description_utf8+=line.text_utf8+"\n";
            /* 誓いの種類の一覧（3 段目。キーの桁は 42）も、原作は英字で強調を動かすだけ（cmd4.c oath_menu。小文字は強調を動かして自分を呼び直し、選ぶのは Enter・空白・6）。
             * 強調中の行は key=0（決定＝Enter）にする。ほかの行は英字のまま（押すと強調が移る）。修-36 の残り。 */
            if(chosen==42&&has_menu_label(frame,"Mercy")&&has_menu_label(frame,"Silence")&&has_menu_label(frame,"Iron"))choice.key=0;
        }
        frame.menu_core_cursor=choice;frame.menu_core_cursor.key=0;frame.menu_core_cursors.assign(1,frame.menu_core_cursor);break;
    }
    return !frame.menu_choices.empty()&&frame.menu_core_cursor.line_index>=0;
}

bool known_choice_menu(GameFrame &frame) {

    /*
     * The monster/object list is itself a live child screen.  Pressing the
     * opening key again toggles visible-only versus all-on-screen scope; the
     * original implementation advertises that operation only in prose on row
     * zero, so the generic list collector cannot discover it.
     */
    const std::string row0=term_row_utf8(0);
    const int scope_toggle=list_scope_toggle_key(row0);
    if(scope_toggle!=0) {
        MenuChoice c{};
        c.line_index=0;
        c.key=scope_toggle;
        c.label_utf8=sq_lang_enabled()?reinterpret_cast<const char*>(u8"表示範囲を切り替える"):"Toggle visible/on-screen scope";
        c.description_utf8=row0;
        frame.menu_choices.push_back(std::move(c));
        native_menu_title=scope_toggle=='['
            ?(sq_lang_enabled()?reinterpret_cast<const char*>(u8"敵の一覧"):"Monster list")
            :(sq_lang_enabled()?reinterpret_cast<const char*>(u8"アイテムの一覧"):"Object list");
        return true;
    }

    // The native footer is visible only while this form owns input. A child
    // prompt can leave its parent list behind, so require the live prompt too.
    for(const auto &spec:std::vector<std::pair<const char*,int>>{{"Interact with Macros",16},{"Interact with Colors",8},{"Interact with Visuals",15}}) {
        if(trim_right(term_row_utf8(2))!=tr_utf8(spec.first))continue;
        if(trim_right(term_row_utf8(spec.second))!=trim_right(tr_utf8("Command: ")))return false;
        native_menu_title=tr_utf8(spec.first);
        for(size_t i=0;i<frame.menu_term_lines.size();++i){
            const auto &line=frame.menu_term_lines[i];if(line.source_row<3||line.source_row>=spec.second)continue;
            const auto start=line.text_utf8.find_first_not_of(' ');if(start==std::string::npos)continue;
            const auto text=line.text_utf8.substr(start);
            if(text.size()<4||text[0]!='('||text[2]!=')'||!((text[1]>='0'&&text[1]<='9')||text[1]=='R'))continue;
            MenuChoice c;c.line_index=int(i);c.span_begin=int(start);c.span_len=int(text.size());c.key=text[1];c.key_begin=int(start)+1;c.key_len=1;c.label_utf8=text;frame.menu_choices.push_back(c);
        }
        return !frame.menu_choices.empty();
    }
    const bool allocate_stats=has_menu_label(frame,"Arrow keys allocate points to stats");
    if(allocate_stats||has_menu_label(frame,"Arrow keys allocate points to skills")) {
        native_menu_title=allocate_stats?(sq_lang_enabled()?reinterpret_cast<const char*>(u8"能力値を決める"):"Allocate stats"):(sq_lang_enabled()?reinterpret_cast<const char*>(u8"技能を伸ばす"):"Increase skills");
        const int first=allocate_stats?2:7,last=allocate_stats?5:14,column=allocate_stats?74:72;
        char cells[kRowCells];unsigned char attrs[kRowCells];
        for(size_t i=0;i<frame.menu_term_lines.size();++i){
            const int y=frame.menu_term_lines[i].source_row;if(y<first||y>last)continue;
            const int n=sq_term_row(y,cells,attrs,kRowCells);
            for(int x=column;x<n;++x)if(attrs[x]==14){frame.menu_core_cursor.line_index=int(i);frame.menu_core_cursor.key=13;frame.menu_term_curs_row=int(i);frame.menu_term_curs_col=column;return true;}
        }
    }
    if(frame.pre_game_menu && has_menu_label(frame,"Enter accept age/height/weight") && has_menu_label(frame,"Space reroll")) {
        const char *labels[]={"Enter accept age/height/weight","Space reroll","    m manually enter"};const int keys[]={13,32,'m'};
        std::string body;for(const auto &line:frame.menu_term_lines)body+=line.text_utf8+"\n";
        for(int k=0;k<3;++k){const auto label=tr_utf8(labels[k]);for(size_t i=0;i<frame.menu_term_lines.size();++i)if(frame.menu_term_lines[i].text_utf8.find(label)!=std::string::npos){MenuChoice c;c.line_index=int(i);c.key=keys[k];c.label_utf8=label;c.description_utf8=body;frame.menu_choices.push_back(c);break;}}
        return !frame.menu_choices.empty();
    }
    if(has_menu_label(frame,"notes   change name   save to a file   abilities   increase skills   ESC")) {
        native_menu_title=tr_utf8("Character sheet");
        const char *keys="ncsai";
        const char *en[]={"notes","change name","save to a file","abilities","increase skills"};
        const char8_t *ja[]={u8"覚書",u8"名を変える",u8"綴りに保存",u8"能力取得",u8"技能を伸ばす"};
        std::string sheet;int footer=-1;for(size_t i=0;i<frame.menu_term_lines.size();++i){const auto &line=frame.menu_term_lines[i];if(line.source_row==23)footer=int(i);else sheet+=line.text_utf8+"\n";}
        if(footer>=0)for(int k=0;k<5;++k){MenuChoice c;c.line_index=footer;c.key=keys[k];c.label_utf8=std::string(1,keys[k])+" "+(sq_lang_enabled()?reinterpret_cast<const char*>(ja[k]):en[k]);c.description_utf8=sheet;frame.menu_choices.push_back(c);}
        return !frame.menu_choices.empty();
    }

    static const char *death_labels[]={"a) View scores","b) View inventory and equipment","c) View dungeon","d) View final messages","e) View character sheet","f) Add comment to notes","g) Save character sheet","h) Exit",nullptr};
    if (frame.pre_game_menu && has_menu_label(frame,death_labels[0]) && has_menu_label(frame,death_labels[7])) {
        native_menu_title = sq_lang_enabled() ? reinterpret_cast<const char*>(u8"死亡後のメニュー") : "After death";
        named_choices(frame,death_labels,15,false);
        std::string summary;
        for (const auto &line:frame.menu_term_lines) if (line.source_row<10 && !line.text_utf8.empty()) summary+=line.text_utf8+"\n";
        for (auto &choice:frame.menu_choices) choice.description_utf8=summary;
        return !frame.menu_choices.empty();
    }
    static const char *knowledge_labels[]={"(1) Display known artefacts","(2) Display known monsters","(3) Display known objects","(4) Display names of the fallen","(5) Display kill counts","(6) Display character notes file",nullptr};
    if(has_menu_label(frame,"Display current knowledge")&&has_menu_label(frame,knowledge_labels[0])){native_menu_title=tr_utf8("Display current knowledge");named_choices(frame,knowledge_labels,5,false);return !frame.menu_choices.empty();}
    static const char *main_labels[]={"a) Return to game","b) Character sheet","c) Options","d) Map","e) Names of the fallen","f) Known objects","g) Known artefacts","h) Known monsters","i) Slain monsters","j) Write a note","k) Take HTML screenshot","l) Set macros","m) Set colours","n) Show old messages","o) Sil version info","p) Abort current game","q) Save game","r) Save and quit",nullptr};
    static const char *option_labels[]={"a) Interface Options","b) Visual Options","c) Challenge Options","d) Load a 'Pref' File","e) Append Options to a 'Pref' File","f) Return to Game","g) Debugging Options",nullptr};
    static const char *smith_labels[]={"a) Base Item","b) Enchant","c) Artifice","d) Numbers","e) Melt","f) Accept","f) Resume",nullptr};
    if(has_menu_label(frame,main_labels[0])&&has_menu_label(frame,main_labels[17])){native_menu_title=tr_utf8("Main menu");named_choices(frame,main_labels,29,false);return true;}
    if(has_menu_label(frame,option_labels[0])&&has_menu_label(frame,option_labels[5])){native_menu_title=tr_utf8("Options");named_choices(frame,option_labels,2,false);return true;}
    for(const char *page:{"Interface Options","Visual Options","Challenge Options","Debugging Options"}) {
        auto heading=term_row_utf8(1);const auto first=heading.find_first_not_of(' ');if(first!=std::string::npos)heading.erase(0,first);heading=trim_right(heading);
        if(heading!=tr_utf8(page)&&heading!=page)continue;
        native_menu_title=tr_utf8(page);char cells[kRowCells];unsigned char attrs[kRowCells];
        for(size_t i=0;i<frame.menu_term_lines.size();++i){int y=frame.menu_term_lines[i].source_row;if(y<3)continue;int n=sq_term_row(y,cells,attrs,kRowCells);
            if(n>2&&attrs[2]==14){frame.menu_core_cursor.line_index=int(i);frame.menu_core_cursor.span_begin=0;frame.menu_core_cursor.span_len=int(frame.menu_term_lines[i].text_utf8.size());return true;}
        }
    }
    if(has_menu_label(frame,smith_labels[1])&&has_menu_label(frame,smith_labels[2])){
        native_menu_title=tr_utf8("Smithing");int col=0,row=0,visible=0;sq_term_cursor(&col,&row,&visible);
        char cells[kRowCells];unsigned char attrs[kRowCells];int cols=0,rows=0;sq_term_size(&cols,&rows);
        for(int candidate:{2,16,36,66})for(int y=1;y<rows;++y){int n=sq_term_row(y,cells,attrs,kRowCells);if(candidate+1<n&&cells[candidate]>='a'&&cells[candidate]<='z'&&cells[candidate+1]==')'&&attrs[candidate]==14)col=candidate;}
        if(col==16||col==36||col==66){std::vector<char> keys;for(char c='a';c<='z';++c)keys.push_back(c);collect_list_choices(frame,keys,col);attach_native_cursor(frame,col,true);}
        else named_choices(frame,smith_labels,2,true);
        return !frame.menu_choices.empty();
    }
    return highlighted_choice_menu(frame);
}

int menu_semantics_selftest_impl()
{
    int failed=0;
    const auto check=[&](bool ok,const char *what){if(!ok){std::fprintf(stderr,"[silq] menu selftest FAIL: %s\n",what);++failed;}};
    check(list_scope_toggle_key(tr_utf8("Monsters you can see (press [ to toggle):"))=='[',"monster visible toggle");
    check(list_scope_toggle_key(tr_utf8("Monsters on screen (press [ to toggle):"))=='[',"monster screen toggle");
    check(list_scope_toggle_key(tr_utf8("Objects you can see (press ] to toggle):"))==']',"object visible toggle");
    check(list_scope_toggle_key(tr_utf8("Objects on screen (press ] to toggle):"))==']',"object screen toggle");
    check(list_scope_toggle_key("ordinary message")==0,"ordinary message rejected");
    {
        // 見る（照準の途中）の帯。英語の綴りと、カタログの訳（日本語）のどちらの行 0 でも出ること（2026-10-07）。
        PromptBar look;
        const auto info_row=[](const char *info){return std::string("You see a floor.  [")+tr_utf8(info)+"]";};
        check(parse_prompt_line(info_row("(t)arget, (m)anual, <dir>"),look)&&look.choices.size()==2&&look.choices[0].key=='m'&&look.choices[1].key=='t',"look free mode (t)arget, (m)anual");
        check(parse_prompt_line(info_row("(m)anual, <dir>"),look)&&look.choices.size()==1&&look.choices[0].key=='m',"look free mode (m)anual only");
        check(parse_prompt_line(info_row("(m)anual, <dir>"),look)&&look.text_utf8.find("You see a floor.")!=std::string::npos&&look.text_utf8.find("[",look.text_utf8.find("You see a floor."))==look.choices[0].begin-1,"look band keeps what is being looked at (cho-13)");
        check(parse_prompt_line(info_row("(t)arget, (a)uto, <dir>"),look)&&look.choices.size()==2&&look.choices[0].key=='a'&&look.choices[1].key=='t',"look auto mode (t)arget, (a)uto");
        check(parse_prompt_line(info_row("<space>, <tab>, <dir>"),look)&&look.choices.size()==2&&look.choices[0].key==' '&&look.choices[1].key=='\t',"look interesting <space>, <tab>");
        check(!parse_prompt_line("You see a floor.",look),"look text without info rejected");
    }
    {
        // タイトルの取消（判-17）：タイトルが入力を待つ間だけ Esc を「やめる」（d）に読み替える。ほかのキーと、待っていない間の Esc は素通し。
        sq_title_menu_set_active(0);
        check(sq_title_menu_map_key(27)==27,"title cancel: Esc passes through outside the title");
        sq_title_menu_set_active(1);
        check(sq_title_menu_map_key(27)=='d',"title cancel: Esc becomes quit (d) while the title waits");
        check(sq_title_menu_map_key('a')=='a'&&sq_title_menu_map_key('\r')=='\r',"title cancel: other keys pass through");
        sq_title_menu_set_active(0);
    }
    {
        // 誓いの種類の一覧（修-36 の残り）：強調中の行の鍵は 0（決定＝Enter）、ほかの行は英字のまま。画面は原作の oath_menu と同じ並び（桁 42）。
        sq_term_selftest_clear();
        sq_term_selftest_put(42,2,1,"Oath");
        sq_term_selftest_put(42,4,2,(std::string("a) ")+utf8_to_sjis(tr_utf8("Mercy"))).c_str());
        sq_term_selftest_put(42,5,2,(std::string("b) ")+utf8_to_sjis(tr_utf8("Silence"))).c_str());
        sq_term_selftest_put(42,6,2,(std::string("c) ")+utf8_to_sjis(tr_utf8("Iron"))).c_str());
        sq_term_selftest_put(42,5,14,"b)");   // 強調（TERM_L_BLUE）
        GameFrame frame;
        frame.menu_open=true;
        int cols=0,rows=0;sq_term_size(&cols,&rows);
        for(int y=0;y<rows;++y){
            char cells[kRowCells];unsigned char attrs[kRowCells];
            const int n=sq_term_row(y,cells,attrs,kRowCells);
            TermMirrorLine line;line.source_row=y;line.text_utf8=n>0?sjis_to_utf8(cells,n):std::string();
            frame.menu_term_lines.push_back(std::move(line));
        }
        const bool ok=highlighted_choice_menu(frame);
        check(ok&&frame.menu_choices.size()==3,"oath list: three choices collected");
        int zero_rows=0,letter_rows=0;
        for(const auto &c:frame.menu_choices){if(c.key==0)++zero_rows;else if(c.key=='a'||c.key=='c')++letter_rows;}
        check(zero_rows==1&&letter_rows==2,"oath list: the highlighted row has key 0 (Enter), the others keep their letters");
        check(ok&&frame.menu_core_cursor.line_index>=0&&frame.menu_core_cursor.key==0,"oath list: core cursor is set");
        sq_term_selftest_clear();
    }
    return failed;
}
}
int menu_semantics_selftest(){return menu_semantics_selftest_impl();}
std::string menu_title(){return native_menu_title;}

bool fill_menu_choices(GameFrame &frame)
{
    native_menu_title.clear();
    if (!frame.menu_open || frame.menu_term_lines.empty()) {
        return false;
    }
    /*
     * 行 0 は**ミラーではなく Term から直に**読む。ミラーの先頭が行 0 とは限らない
     * （いまは全行積んでいるが、そこを直したときに黙って別の行を読まないように）。
     */
    std::vector<char> expected;
    if (!parse_choice_prompt(term_row_utf8(0), expected)) {
        return known_choice_menu(frame);
    }
    collect_list_choices(frame, expected);
    return !frame.menu_choices.empty();
}

} // namespace sq
