/*!
 * @file gb_menu.cpp
 * @brief カーソルとプロンプト（S2 の後半・S4）。
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
#include "bridge/input_event_adapter.h"
#include "bridge/symbol_prompt.h"
#include "bridge/look_screen_cover.h"
#include "bridge/prompt_semantics.h"
#include "frame/message_more.h"

#include "gb_lang_c.h" //GB: english layer #33 - the prompt-bar labels never reach Term, so #1 cannot see them
#include "gb_shim.h"
#include "gb_text.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <deque>

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
    static std::deque<std::pair<const char *, std::string>> cache;
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

    //! 名前が長くて案内が 80 桁の外へ落ちた行（共用の `presentation::look_row_was_cut()`。TOband と同じ頭 `Target:`／`ターゲット:`）。
    //! 案内の中身は読めないので、見る `l` の操作はどれも出す（敵の行は `t` も）。
    const bool cut_look = presentation::look_row_was_cut(text, presentation::LookRowHeads::Hengband);
    if (cut_look
        || ((text.find("<dir>") != std::string::npos || text.find(reinterpret_cast<const char *>(u8"q止")) != std::string::npos)
            && text.find('p') != std::string::npos && (text.find('+') != std::string::npos || text.find('-') != std::string::npos))) {
        // 見る・狙うの途中で `r`（モンスターの思い出）などが画面を退避して重なっている間は、帯を作らない（`look_screen_cover.h`）。
        if (presentation::look_screen_cover().covered(gb_screen_depth())) {
            out = PromptBar{};
            return false;
        }
        //! `gb_lang_enabled()` は**英語層が生きているとき**に真（`gb_lang_c.h`）。以前は向きが逆で、日本語の画面に英語の札が出ていた（2026-10-07）。
        const bool ja=gb_lang_enabled()==0;
        const auto add=[&](int key,const char *ja_label,const char *en_label){PromptChoice c{};c.key=key;c.label_utf8=ja?ja_label:en_label;if(!out.text_utf8.empty())out.text_utf8+="  ";out.text_utf8+='[';c.begin=static_cast<int>(out.text_utf8.size());out.text_utf8+=c.label_utf8;c.len=static_cast<int>(c.label_utf8.size());out.text_utf8+=']';out.choices.push_back(std::move(c));};
        out.text_utf8=ja?reinterpret_cast<const char *>(u8"見る: "):"Look: ";
        /*
         * 見ている物の説明。行 0 の頭にある「<名前> <状態> [r,…]」（敵）や「<名前> [<情報>]」（アイテム・地形）の
         * 最後の `[` より前を、帯の見出しのすぐ後ろへ残す（`target_set_aux()`。アイテムの名前にも `[4,+0]` が付くので最後を取る）。
         * 選択肢だけを出すと、何を見ているか帯のどこにも出なかった（調-13）。続きの待ちの印（`-more-`／`-続く-`）は説明に含めない。
         * 手本は Sil-Q・Frox。
         */
        {
            std::string subject;
            //! 案内が落ちた行は共用の道具で取る（落ちた案内の頭の `[` だけを外す。頭の `[<経験値>]`・アイテムの `[4,+0]` は説明に残る）。
            if(cut_look)subject=std::string(presentation::look_row_cut_subject(text));
            else if(const std::size_t open=text.rfind('[');open!=std::string::npos)subject=text.substr(0,open);
            const auto trim=[](std::string &s){while(!s.empty()&&s.back()==' ')s.pop_back();std::size_t i=0;while(i<s.size()&&s[i]==' ')++i;s.erase(0,i);};
            trim(subject);
            for(const char *const more:{"-more-",reinterpret_cast<const char *>(u8"-続く-")}){const std::string m(more);if(subject.size()>=m.size()&&subject.compare(subject.size()-m.size(),m.size(),m)==0){subject.erase(subject.size()-m.size());trim(subject);}}
            out.text_utf8+=subject;
        }
        if(cut_look||text.find('+')!=std::string::npos)add('+',reinterpret_cast<const char *>(u8"次の注目地点"),"Next interesting");
        if(cut_look||text.find('-')!=std::string::npos)add('-',reinterpret_cast<const char *>(u8"前の注目地点"),"Previous interesting");
        add('p',reinterpret_cast<const char *>(u8"自分の位置へ戻る"),"Recenter on player");
        if(text.find("m,")!=std::string::npos||text.find(reinterpret_cast<const char *>(u8"m近"))!=std::string::npos)add('m',reinterpret_cast<const char *>(u8"近い注目地点へ"),"Nearest interesting");
        if(cut_look||text.find("o,")!=std::string::npos||text.find(reinterpret_cast<const char *>(u8"o現"))!=std::string::npos)add('o',reinterpret_cast<const char *>(u8"現在位置を自由に見る"),"Manual/current mode");
        if(cut_look?presentation::look_row_is_monster(text):text.find("t")!=std::string::npos)add('t',reinterpret_cast<const char *>(u8"ここを対象にする"),"Select target");
        out.footer_utf8=ja?reinterpret_cast<const char *>(u8"LB/RB: 操作を選ぶ  A/Enter: 実行  十字キー: カーソル移動"):"LB/RB: select action  A/Enter: run  D-pad: move look cursor";
        return !out.choices.empty();
    }

    if ((text.find("Please verify SUICIDE by typing the '@' sign") != std::string::npos)
        || ((text.find("'@'") != std::string::npos) && (text.find(reinterpret_cast<const char *>(u8"確認")) != std::string::npos))) {
        out.text_utf8 = text;
        //! 札は日本語の画面では日本語（`gb_lang_enabled()` が真なら英語層）。
        const bool en = (gb_lang_enabled() != 0);
        out.choices.push_back({ en ? "Confirm (@)" : reinterpret_cast<const char *>(u8"確認 (@)"), '@', 0, 0 });
        out.choices.push_back({ en ? "Cancel (Esc)" : reinterpret_cast<const char *>(u8"取消 (Esc)"), 27, 0, 0 });
        return true;
    }
    if (text.find("w for the Force") != std::string::npos
        || text.find(reinterpret_cast<const char *>(u8"'w'練気")) != std::string::npos) {
        out.text_utf8 = text;
        const bool en = (gb_lang_enabled() != 0);
        out.choices.push_back({ en ? "Force" : reinterpret_cast<const char *>(u8"練気術"), 'w', 0, 0 });
        out.choices.push_back({ en ? "Cancel" : reinterpret_cast<const char *>(u8"取消"), 27, 0, 0 });
        return true;
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
    //! `a/u/U)`: Gensoband の browse 種別は 1 札に 3 個の同義キーを持つ。
    if ((p + 4 < text.size()) && is_ascii_alnum(text[p]) && (text[p + 1] == '/')
        && is_ascii_alnum(text[p + 2]) && (text[p + 3] == '/') && is_ascii_alnum(text[p + 4])) {
        if ((text.compare(p + 5, 1, ")") == 0) && is_choice_token_terminator(text, p + 6)) {
            out.key_spans.emplace_back(p, 1);
            out.keys.push_back(static_cast<unsigned char>(text[p]));
            out.key_spans.emplace_back(p + 2, 1);
            out.keys.push_back(static_cast<unsigned char>(text[p + 2]));
            out.key_spans.emplace_back(p + 4, 1);
            out.keys.push_back(static_cast<unsigned char>(text[p + 4]));
            return true;
        }
        return false;
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

/*!
 * @brief `text` の `[from, to)` で、開いたまま閉じていない `(` があるか（括弧の中か）。
 * @details レベルアップの能力値の選択（`xtra2.c` の `check_experience()`）は
 * `a) 腕力 (現在値     10)` と並ぶ。括弧の中の `8)` を札と読んだり、空白 3 つで名札を切ったりすると、
 * 枠が「腕力 (現在値」で切れる（変愚の `presentation_bridge.cpp` と FroxComposband の FH-24 で直した形。2026-10-07）。
 */
static bool inside_open_paren(const std::string &text, std::size_t from, std::size_t to)
{
    int depth = 0;
    for (std::size_t i = from; (i < to) && (i < text.size()); ++i) {
        if (text[i] == '(') {
            ++depth;
        } else if ((text[i] == ')') && (depth > 0)) {
            --depth;
        }
    }
    return depth > 0;
}

std::vector<ChoiceTokenGroup> scan_choice_tokens(const std::string &text)
{
    std::vector<ChoiceTokenGroup> groups;
    for (std::size_t p = 0; p < text.size(); ++p) {
        if ((p != 0) && (text[p - 1] != ' ')) {
            continue;
        }
        //! 括弧の中の `8)` は札ではない（直前の札の頭から数えて、閉じていない `(` があれば読み飛ばす）。
        if (!groups.empty() && inside_open_paren(text, groups.back().begin, p)) {
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
    std::size_t gap = text.find("   ", begin);
    //! 括弧の中の空白（`(現在値     10)`）では切らない。閉じ括弧の後ろから探し直す。
    while ((gap != std::string::npos) && (gap < end) && inside_open_paren(text, begin, gap)) {
        const std::size_t close = text.find(')', gap);
        if ((close == std::string::npos) || (close >= end)) {
            break;
        }
        gap = text.find("   ", close);
    }
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

/*
 * Legacy spell/power selectors sometimes keep the list hidden until `*`,
 * Space or `?` is pressed.  They are not ordinary item prompts: the prompt
 * advertises both a letter range and a list-toggle key, and the visible list
 * is drawn as a stable vertical band of `a)` rows without a core cursor.
 */
bool is_raw_toggle_list_prompt(const std::string &text, char &lo, char &hi)
{
    if ((text.find('*') == std::string::npos) || (text.find("ESC") == std::string::npos)) {
        return false;
    }
    if (!parse_letter_range_prompt(text, lo, hi)) {
        return false;
    }
    return (lo >= 'a') && (lo <= 'z') && (hi >= lo) && (hi <= 'z');
}

bool collect_raw_toggle_list_choices(GameFrame &frame)
{
    const auto row0 = std::find_if(frame.menu_term_lines.begin(), frame.menu_term_lines.end(),
        [](const auto &line) { return line.source_row == 0; });
    if (row0 == frame.menu_term_lines.end()) {
        return false;
    }

    char lo = 0;
    char hi = 0;
    if (!is_raw_toggle_list_prompt(row0->text_utf8, lo, hi)) {
        return false;
    }

    struct Candidate {
        std::size_t line_index{};
        ChoiceTokenGroup group;
        int column{};
    };
    std::vector<Candidate> candidates;
    int anchor_column = -1;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const auto &line = frame.menu_term_lines[i];
        if ((line.source_row < 2) || (line.source_row > 22)) {
            continue;
        }
        for (const auto &group : scan_choice_tokens(line.text_utf8)) {
            if ((group.keys.size() != 1) || (group.keys.front() < lo) || (group.keys.front() > hi)) {
                continue;
            }
            const int column = static_cast<int>(utf8_to_sjis(line.text_utf8.substr(0, group.begin)).size());
            // Sniper draws at x=20 (`a)` at ~22); monster powers draw at x=16 (~18).
            if ((column < 16) || (column > 24)) {
                continue;
            }
            if (group.keys.front() == static_cast<unsigned char>(lo)) {
                anchor_column = column;
            }
            candidates.push_back({ i, group, column });
        }
    }
    if (anchor_column < 0) {
        /* The raw selector starts with its list hidden.  At that stage the
         * range prompt is already live, but none of the a..z rows exist yet;
         * '*' is the only route that reveals them.  Expose that operation as
         * a semantic choice instead of requiring a physical keyboard. */
        MenuChoice show{};
        show.line_index = static_cast<int>(std::distance(frame.menu_term_lines.begin(), row0));
        show.key = '*';
        show.key_len = 1;
        show.label_utf8 = (gb_lang_enabled() != 0) ? "Show choices" : reinterpret_cast<const char *>(u8"一覧を見る");
        show.description_utf8 = row0->text_utf8;
        frame.menu_choices.push_back(std::move(show));
        return true;
    }

    std::string description;
    for (const auto &line : frame.menu_term_lines) {
        description += line.text_utf8;
        description += '\n';
    }
    for (const auto &candidate : candidates) {
        if (candidate.column != anchor_column) {
            continue;
        }
        const auto before = frame.menu_choices.size();
        emit_choices_for_line(frame, candidate.line_index, { candidate.group });
        for (std::size_t i = before; i < frame.menu_choices.size(); ++i) {
            frame.menu_choices[i].description_utf8 = description;
        }
    }
    return !frame.menu_choices.empty();
}

/* Auto-pick editor's Escape menu is a floating command box.  Unlike the
 * editor itself it does not expose a core cursor: row 0 says `(a-x) Command:`
 * and the box contains the matching `a) ...` rows.  Treat only that exact
 * range-prefixed command prompt as a menu so ordinary prose/list screens do
 * not become clickable by accident.  This also covers nested submenus because
 * do_command_menu() uses the same grammar at every level. */
bool collect_autopick_command_menu(GameFrame &frame)
{
    const auto row0 = std::find_if(frame.menu_term_lines.begin(), frame.menu_term_lines.end(),
        [](const auto &line) { return line.source_row == 0; });
    if (row0 == frame.menu_term_lines.end()) return false;
    const std::string &prompt = row0->text_utf8;
    if (prompt.find("Command:") == std::string::npos
        && prompt.find(reinterpret_cast<const char *>(u8"コマンド:")) == std::string::npos) return false;
    char lo = 0, hi = 0;
    if (!parse_letter_range_prompt(prompt, lo, hi) || lo != 'a' || hi < lo || hi > 'z') return false;

    unsigned char expected = static_cast<unsigned char>(lo);
    std::string description;
    for (const auto &line : frame.menu_term_lines) description += line.text_utf8 + "\n";
    for (std::size_t i = 0; i < frame.menu_term_lines.size() && expected <= static_cast<unsigned char>(hi); ++i) {
        for (const auto &group : scan_choice_tokens(frame.menu_term_lines[i].text_utf8)) {
            if (group.keys.size() != 1 || group.keys.front() != expected) continue;
            const auto before = frame.menu_choices.size();
            emit_choices_for_line(frame, i, { group });
            for (std::size_t j = before; j < frame.menu_choices.size(); ++j)
                frame.menu_choices[j].description_utf8 = description;
            ++expected;
            break;
        }
    }
    return expected > static_cast<unsigned char>(lo) && expected == static_cast<unsigned char>(hi) + 1;
}

bool collect_strict_unnamed_letter_list(GameFrame &frame)
{
    if (!frame.menu_core_cursors.empty() || frame.menu_core_cursor.line_index >= 0) {
        return false;
    }

    int prompt_source_row = -1;
    for (const auto &line : frame.menu_term_lines) {
        if (line.text_utf8.find("ESC") != std::string::npos) {
            prompt_source_row = line.source_row;
            break;
        }
    }
    if (prompt_source_row < 0) {
        return false;
    }

    struct Candidate {
        std::size_t line_index{};
        ChoiceTokenGroup group;
        int column{};
    };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const auto &line = frame.menu_term_lines[i];
        if ((line.source_row <= prompt_source_row) || (line.source_row > prompt_source_row + 22)) {
            continue;
        }
        for (const auto &group : scan_choice_tokens(line.text_utf8)) {
            if ((group.keys.size() != 1) || (group.keys.front() < 'a') || (group.keys.front() > 'z')) {
                continue;
            }
            const int column = static_cast<int>(utf8_to_sjis(line.text_utf8.substr(0, group.begin)).size());
            candidates.push_back({ i, group, column });
        }
    }
    if (candidates.empty()) {
        return false;
    }

    // Only accept a true vertical a,b,c... run at one terminal column.  This
    // deliberately rejects prose containing an isolated `a)` token.
    const int column = candidates.front().column;
    unsigned char expected = 'a';
    std::vector<Candidate> run;
    for (const auto &candidate : candidates) {
        if (candidate.column != column || candidate.group.keys.front() != expected) {
            continue;
        }
        run.push_back(candidate);
        ++expected;
    }
    if (run.empty() || run.front().group.keys.front() != 'a') {
        return false;
    }

    std::string description;
    for (const auto &line : frame.menu_term_lines) {
        description += line.text_utf8;
        description += '\n';
    }
    for (const auto &candidate : run) {
        const auto before = frame.menu_choices.size();
        emit_choices_for_line(frame, candidate.line_index, { candidate.group });
        for (std::size_t i = before; i < frame.menu_choices.size(); ++i) {
            frame.menu_choices[i].description_utf8 = description;
        }
    }
    return !frame.menu_choices.empty();
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
        //! 括弧の中の `8)` は札ではない（`scan_choice_tokens` と同じ）。
        if (!groups.empty() && inside_open_paren(text, groups.back().begin, p)) {
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
 * @details 気づいたこと 2026-08-29「ヘルプの目次等の選択肢でカーソル移動ができない」。
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
    int prompt_row_last{-1};
    int signature_row{-1};
    int column_min{-1},column_max{-1};
    /*!
     * @brief 署名が**建物の主の名**など、一覧の無い最初の画面にも出る文言のとき真。
     * @details 真なら、`row_min`〜`row_max` に札が 1 つも読めない画面は**この表の画面とは見なさず**、
     * 次の判定（建物・店）へ回す。これが無いと、最初の画面でも署名が当たって `-1` を返し、
     * 建物の選択肢が作られない（F-gensoband-09。賭け試合の最初の画面が決定で進まない）。
     */
    bool require_listed{false};
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
    // `choose_browse()` is a raw three-row selector.  Use its stable ASCII
    // key band as the signature so both JP/EN cores expose the same choices.
    { "browse_kind", GB_SIG("a/u/U)"), kStylePlain, 2, 4, 0, 0, 2, 16, 16 },
    //! レベルアップの能力値の選択（`xtra2.c` の `check_experience()`。行 1 が問い・行 2〜7 が `a) 腕力 (現在値 …)`）。
    { "stat_raise", GB_SIG("どの能力値を上げますか"), kStylePlain, 2, 7, 1, 1, 1, 20, 24 },
    //! 英語層のとき（カタログは**行全体**の鍵で引くので、部分文字列の署名は訳せない。英語の原文で別に持つ）。
    { "stat_raise_en", GB_SIG("Which stat do you want to raise"), kStylePlain, 2, 7, 1, 1, 1, 20, 24 },
    { "elemental_brand", GB_SIG("どの元素攻撃をしますか"), kStylePlain, 2, 6, 1, 1, 1, 20, 24 },
    { "elemental_immunity", GB_SIG("どの元素の免疫をつけますか"), kStylePlain, 2, 5, 1, 1, 1, 20, 24 },
    { "monk_stance", GB_SIG("どの構えをとりますか"), kStylePlain, 2, 10, 1, 1, 1, 20, 22 },
    { "samurai_stance", GB_SIG("どの型で構えますか"), kStylePlain, 2, 10, 1, 1, 1, 20, 22 },
    { "card_power", GB_SIG("(スペース:次のページ ESC:キャンセル)"), kStylePlain, 4, 25, 4, 26, -1, 20, 20 },
    { "marisa_present", GB_SIG("(a～h:プレゼントを選択 / n,スペース:次のページへ / ESC:キャンセル)"), kStylePlain, 8, 15, 8, 16, 6, 10, 10 },
    { "wanted_target", GB_SIG("誰を狙いますか？(ESC:キャンセル)"), kStylePlain, 15, 17, 14, 17, 14, 5, 5 },
    { "weapon_shape", GB_SIG("この柄をどんな武器にして欲しいのだ？"), kStylePlain, 2, 6, 2, 6, 0, 40, 40 },
    { "marisa_magic_info", GB_SIG("「どの魔法について確認しよう？」(ESC:キャンセル)"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "marisa_magic_carry", GB_SIG("「どの魔法を持って行こう？」"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "marisa_magic_make", GB_SIG("「どの魔法を作ろう？」(ESC:キャンセル)"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "marisa_magic_use", GB_SIG("「どの魔法を使おう？」(ESC:キャンセル)"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "compound_drug", GB_SIG("素材にするものを3つまで選んで下さい:"), kStylePlain, 10, 14, 10, 17, 8, 10, 10 },
    { "nightmare_diary", GB_SIG("累計評価点:"), kStylePlain, 9, 17, 19, 19, 19, 22, 22 },
    { "cooking_order", GB_SIG("何を作ってもらいますか？(ESC:キャンセル)"), kStylePlain, 5, 23, 5, 23, 4, 20, 20 },
    { "cooking_make", GB_SIG("何を作りますか？(ESC:キャンセル)"), kStylePlain, 5, 23, 5, 23, 4, 20, 20 },
    { "chosen_trap", GB_SIG("どのトラップを配置しよう？"), kStylePlain, 3, 23, 3, 23, 2, 30, 30 },
    { "prism_class", GB_SIG("誰と交代しますか？"), kStylePlain, 12, 14, 12, 14, 10, 10, 10 },
    { "drug_recipe", GB_SIG("どの薬について確認しますか？」(ESC:キャンセル)"), kStylePlain, 7, 22, 7, 22, 4, 20, 20 },
    { "hecatia_body", GB_SIG("どの体と交代しますか？"), kStylePlain, 12, 14, 12, 14, 10, 30, 30 },
    { "hecatia_earth", GB_SIG("あなたの「地球」の体はどんな性格ですか？"), kStylePlain, 12, 23, 12, 23, 10, 25, 25 },
    { "hecatia_moon", GB_SIG("あなたの「月」の体はどんな性格ですか？"), kStylePlain, 12, 23, 12, 23, 10, 25, 25 },
    { "soldier_skill_info", GB_SIG("どの技能の説明を見ますか？(ESC:終了)"), kStylePlain, 4, 23, 4, 23, 2, 18, 18 },
    { "soldier_skill_gain", GB_SIG("どの技能を習得しますか？(ESC:キャンセル)"), kStylePlain, 4, 23, 4, 23, 2, 18, 18 },
    { "soldier_bullet_info", GB_SIG("どの銃弾の説明を見ますか？(ESC:キャンセル ?:確認を終える)"), kStylePlain, 5, 20, 5, 20, 2, 18, 50 },
    { "soldier_bullet_use", GB_SIG("どの銃弾を使用しますか？(ESC:キャンセル ?:銃弾の効果を確認する)"), kStylePlain, 5, 20, 5, 20, 2, 18, 50 },
    { "ariya_wall", GB_SIG("a) 普通の壁を永久壁にする"), kStylePlain, 10, 11, 12, 12, 10, 20, 20 },
    { "nina_building", GB_SIG("どんな建物の蜃気楼を作りますか？(ESC:キャンセル)"), kStylePlain, 1, 25, 1, 25, -1, 25, 25 },
    { "nina_monster", GB_SIG("誰のコピーを呼び出しますか？(ESC:キャンセル)"), kStylePlain, 1, 25, 1, 25, -1, 25, 25 },
    { "chaos_effect", GB_SIG("起こしたい効果を記号で選択してください。"), kStylePlain, 3, 5, 5, 5, 0, 40, 40 },
    { "reincarnation", GB_SIG("転生したい種族を記号で選択してください。"), kStylePlain, 15, 17, 17, 17, 0, 40, 40 },
    { "option_root", GB_SIG("[ オプションの設定 ]"), kStyleParen, 3, 19, 21 },
    { "macros", GB_SIG("[ マクロの設定 ]"), kStyleParen, 4, 13, 16 },
    { "visuals", GB_SIG("[ 画面表示の設定 ]"), kStyleParen, 3, 13, 15 },
    { "colors", GB_SIG("[ カラーの設定 ]"), kStyleParen, 4, 6, 8 },
    { "diary", GB_SIG("[ 記録の設定 ]"), kStyleParen, 4, 7, 18 },
    //! 知識だけは丸括弧の項目と `ESC) 抜ける  SPACE) 次ページ` が同居する。両方の書式で読む。
    { "knowledge", GB_SIG("現在の知識を確認する"), kStyleParen | kStylePlain, 6, 21, 20 },
    { "recall_dungeon", GB_SIG("どのダンジョン"), kStylePlain, 1, 19, 0 },
    { "tele_town", GB_SIG("どこに行きますか"), kStylePlain, 6, 19, 0 },
    { "melee_arena", GB_SIG("雀斑の河童娘"), kStyleBracket, 4, 19, 0, -1, -1, -1, -1, true },
    /*
     * 英語層の署名（2026-10-12 修-24）。上の日本語の署名は**部分文字列**で、訳の表の鍵は行全体なので
     * `gb_tr()` では引けない（`stat_raise_en` と同じ事情）。訳は `lang/en/ui/*.txt`・`lang/en/edit/town.en.txt`
     * の英語。形（行・桁）は日本語の同名の項目と同じ。日本語の画面に英語の署名は出ないので、日本語の動きは変わらない。
     */
    { "elemental_brand_en", GB_SIG("Choose a temporary elemental brand"), kStylePlain, 2, 6, 1, 1, 1, 20, 24 },
    { "elemental_immunity_en", GB_SIG("Choose a temporary elemental immune"), kStylePlain, 2, 5, 1, 1, 1, 20, 24 },
    //! 武術家の構え・剣術家の型は英語では同じ問い（`Choose Form:`）。形も同じ。
    { "stance_en", GB_SIG("Choose Form:"), kStylePlain, 2, 10, 1, 1, 1, 20, 22 },
    { "nightmare_diary_en", GB_SIG("Total rating:"), kStylePlain, 9, 17, 19, 19, 19, 22, 22 },
    { "recall_dungeon_en", GB_SIG("Which dungeon do you"), kStylePlain, 1, 19, 0 },
    { "tele_town_en", GB_SIG("Where do you go:"), kStylePlain, 6, 19, 0 },
    //! 賭け試合の建物の主の名の英語（`lang/en/edit/town.en.txt` の `BE:` 行）。
    { "melee_arena_en", GB_SIG("Freckled Kappa Girl"), kStyleBracket, 4, 19, 0, -1, -1, -1, -1, true },
};

#undef GB_SIG

//! @brief 表に載っている画面なら選択肢を作る。@return 1 件でも積んだら true
int collect_named_menu_choices(GameFrame &frame)
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
        return -1;
    }
    for (const NamedMenuSpec &spec : kNamedMenus) {
        const std::string &signature = utf8_of(spec.signature, spec.signature_len);
        const std::string translated=sjis_to_utf8(gb_tr(spec.signature));
        bool found = false;
        for (const auto &line : frame.menu_term_lines) {
            if(spec.signature_row>=0 && line.source_row!=spec.signature_row)continue;
            if (line.text_utf8.find(signature) != std::string::npos || line.text_utf8.find(translated) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }
        /*
         * 署名が建物の主の名のように最初の画面にも出る文言のときは、**札が 1 つも読めなければ
         * この画面ではない**。最初の画面（`g) 話しかける` `ESC) 建物を出る`）を建物として扱わせる。
         * 札が読めるのにカーソルが外れているとき（口数の問いなど、降りている間）は従来どおり下で `-1`。
         */
        if (spec.require_listed) {
            bool listed = false;
            for (const auto &line : frame.menu_term_lines) {
                if ((line.source_row >= spec.row_min) && (line.source_row <= spec.row_max)
                    && !scan_choice_tokens_styled(line.text_utf8, spec.styles).empty()) {
                    listed = true;
                    break;
                }
            }
            if (!listed) {
                continue;
            }
        }
        /*
         * **一覧の待ちに居るときだけ**作る。降りている間に作ると、
         * もう効かない項目が選択肢のまま残る（上の @name の注記）。
         */
        const int cursor_row=frame.menu_term_curs_row>=0 && frame.menu_term_curs_row<int(frame.menu_term_lines.size()) ? frame.menu_term_lines[frame.menu_term_curs_row].source_row : -1;
        if (cursor_row < spec.prompt_row || cursor_row > (spec.prompt_row_last<0?spec.prompt_row:spec.prompt_row_last)) {
            return -1;
        }
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            const int row = frame.menu_term_lines[i].source_row;
            if ((row < spec.row_min) || (row > spec.row_max)) {
                continue;
            }
            const auto before=frame.menu_choices.size();
            emit_choices_for_line(frame, i, scan_choice_tokens_styled(frame.menu_term_lines[i].text_utf8, spec.styles));
            if(spec.column_min>=0)frame.menu_choices.erase(std::remove_if(frame.menu_choices.begin()+before,frame.menu_choices.end(),[&](const auto &choice){
                const auto column=int(utf8_to_sjis(frame.menu_term_lines[i].text_utf8.substr(0,choice.span_begin)).size());
                return column<spec.column_min||column>spec.column_max;
            }),frame.menu_choices.end());
        }
        if(spec.column_min>=0){
            std::string description;for(const auto &line:frame.menu_term_lines)description+=line.text_utf8+"\n";
            for(auto &choice:frame.menu_choices)choice.description_utf8=description;
            if(std::string(spec.tag)=="marisa_present"||std::string(spec.tag)=="card_power")frame.menu_page_next_key=' ';

            /*
             * Some named screens have a real command in the prompt/footer in
             * addition to the lettered rows.  Treat that operation as a first-
             * class choice instead of assuming that recognizing the list rows
             * makes the whole child screen operable.
             */
            const std::string tag(spec.tag);
            //! 札は日本語の画面では日本語（以前は英語固定だった。2026-10-07）。日本語は原作の文言に合わせた。
            const bool aux_en=(gb_lang_enabled()!=0);
            const auto add_aux_choice=[&](int key,const char *label,const char *label_ja,int source_row){
                MenuChoice choice{};
                choice.key=key;
                choice.label_utf8=aux_en?label:label_ja;
                choice.description_utf8=description;
                for(std::size_t li=0;li<frame.menu_term_lines.size();++li) {
                    if(frame.menu_term_lines[li].source_row==source_row){choice.line_index=int(li);break;}
                }
                frame.menu_choices.push_back(std::move(choice));
            };
            if(tag=="marisa_magic_carry") {
                add_aux_choice('R',"Return all carried spells to home",reinterpret_cast<const char *>(u8"持った魔法を全て家に戻す"),5);
            } else if(tag=="soldier_bullet_use") {
                add_aux_choice('?',"Show bullet effects",reinterpret_cast<const char *>(u8"銃弾の効果を確認する"),2);
            } else if(tag=="soldier_bullet_info") {
                add_aux_choice('?',"Return to bullet selection",reinterpret_cast<const char *>(u8"確認を終える"),2);
            }
        }
        return frame.menu_choices.empty()?-1:1;
    }
    return false;
}
/*! @} */

//! 画面の種類の判定で英語の目印を見るか。**英語層が生きているとき**だけ（自己検査は `g_menu_english_for_test` で立てる）。
bool g_menu_english_for_test = false;
bool menu_english() { return g_menu_english_for_test || (gb_lang_enabled() != 0); }
//! 店・建物の目印の英語の字（`gb_lang_enabled()` 真のときの `コマンド:`・`手持ちのお金:` の訳）。
const std::string kCommandEn = "Command:";
const std::string kGoldEn = "Gold Remaining:";

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
    /*
     * 英語層（`gb_lang_enabled()` が真）では、同じ行の訳が `Command:`・`Gold Remaining:` になる
     * （`lang/en/ui/harvested.en.txt`）。**日本語の目印と同じ規則**（`Command:` で始まる行 ＋ `ESC)` の行、
     * または `ESC)` の行 ＋ お金の行）で見る。日本語のときは英語の目印を見ない（動きを変えない）。
     */
    const bool english = menu_english();
    const std::string &command_label = english ? kCommandEn : utf8_of(kCommandSjis, sizeof(kCommandSjis) - 1);
    const std::string &gold_label = english ? kGoldEn : utf8_of(kGoldSjis, sizeof(kGoldSjis) - 1);

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


// Birth grids use yellow text, including the high bit on wide characters.
// Only an active, known birth selection with a letter label owns this cursor.
bool fill_birth_selection_cursor(GameFrame &frame)
{
    if(!frame.pre_game_menu || frame.text_input_active)return false;
    static const char *const headings[]={
        reinterpret_cast<const char*>(u8"難易度を選択してください"),
        reinterpret_cast<const char*>(u8"あなたは・・・"),
        reinterpret_cast<const char*>(u8"あなたの登場作品を選んでください"),
        reinterpret_cast<const char*>(u8"灰字は未実装 ESCで作品選択へ戻る"),
        reinterpret_cast<const char*>(u8"を選んで下さい"),
        // 英語層（`lang/en/ui/messages.en.txt` の訳。誕生の窓の見出し）。
        "Choose the difficulty", "You are...", "Choose the work you come from",
        "Grey is not implemented",
        "Choose a", "Select difficulty", "Select your difficulty"};
    bool selection=false;
    for(const auto &line:frame.menu_term_lines)for(const char *heading:headings)
        if(line.text_utf8.find(heading)!=std::string::npos)selection=true;
    if(!selection)return false;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto &line=frame.menu_term_lines[i];
        for(size_t j=0;j<line.color_spans.size();++j) {
            const auto &span=line.color_spans[j];
            if((span.color&15)!=11 || span.begin<0 || span.len<=0)continue;
            const size_t begin=size_t(span.begin);
            if(begin+2>line.text_utf8.size())continue;
            const char key=line.text_utf8[begin];
            if(!((key>='a'&&key<='z')||(key>='A'&&key<='Z'))||line.text_utf8[begin+1]!=')')continue;
            int end=span.begin+span.len;
            for(size_t next=j+1;next<line.color_spans.size();++next){const auto &part=line.color_spans[next];if(part.begin!=end||(part.color&15)!=11)break;end+=part.len;}
            if(size_t(end)>line.text_utf8.size())continue;
            frame.menu_core_cursor={int(i),int(begin),end-int(begin),0,0,0};
            frame.menu_core_cursors.assign(1,frame.menu_core_cursor);frame.menu_choices.clear();return true;
        }
    }
    return false;
}

bool fill_core_cursor(GameFrame &frame)
{
    frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
    frame.menu_core_cursors.clear();
    if(fill_birth_selection_cursor(frame))return true;

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
    /*
     * 「繰り返し回数」（`0` の後の `回数: 12`／英語層では `Count: 12`）。数字を 1 キーずつ
     * 読む入力なので他の問いに当たらない。変愚と同じ枠を出す（`input_event_adapter.h`）。
     */
    if (presentation::fill_repeat_count_input(row0, frame.numeric)) {
        return;
    }
    char raw_list_lo = 0;
    char raw_list_hi = 0;
    if (is_raw_toggle_list_prompt(row0, raw_list_lo, raw_list_hi)) {
        // If the list is visible, fill_store_menu_choices() has already built
        // the concrete rows.  If it is hidden, expose the original list toggle
        // so touch/controller users can reveal the selectable rows first.
        //
        // **コアのカーソル（`》`）が見つかっていれば、一覧はもう出ている**（`use_menu` の呪文の一覧。
        // `fill_core_cursor()` はこの関数より先に呼ばれる。gb_frame.cpp）。ここで `*` を選択肢に足すと、
        // 窓が「選択肢の段」になってカーソルの一覧として扱われず、`*` は一覧の表示を切り替えるだけなので
        // パッドやマウスでは先へ進めなかった（呪文を覚える G。遊ぶ人に届いた報告 2026-10-06）。
        // 変愚と同じく、カーソルの段のまま決定で選ばせる。
        if (frame.menu_choices.empty() && frame.menu_core_cursors.empty()) {
            frame.menu_open = true;
            MenuChoice choice{};
            choice.line_index = -1;
            choice.key = '*';
            choice.label_utf8 = (gb_lang_enabled() != 0) ? "Show choices" : reinterpret_cast<const char *>(u8"一覧を見る");
            choice.description_utf8 = row0;
            frame.menu_choices.push_back(std::move(choice));
        }
        return;
    }
    if (presentation::is_monster_symbol_prompt(row0)) {
        presentation::expose_ascii_symbol_prompt(frame, row0);
        return;
    }
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
     * **地図の上の `-続く-`**（変愚の `fill_row0_prompts()` と同じ直し。2026-09-25）。
     * `auto_more` が偽のとき、コアは `msg_flush()` で行 0 に `-続く-` を書いてキーを待つ。
     * メニューの外ではミラーが開かないので、ここで渡さないと frame に待っている印が
     * 1 つも無く、持ち物メニューから命令を送った後に一覧が消えたまま止まって見える。
     * 選択肢は作らない（帯へ出すだけ）。
     */
    if (frame.menu_term_lines.empty() && !frame.text_input_active && presentation::message_more_suffix(trimmed)) {
        frame.prompt.text_utf8 = trimmed;
        frame.prompt.line_index = -1;
        return;
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
    frame.prompt.text_utf8 = row0;
    int cx=0,cy=0;if(gb_term_cursor(&cx,&cy,nullptr)==GB_OK)frame.menu_term_curs_col=cx;
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
     * 噛み合わないので、ここで見ないと 1 件も積まれない。
     */
    if (fill_help_menu(frame)) {
        return !frame.menu_choices.empty();
    }
    if (collect_autopick_command_menu(frame)) {
        return true;
    }
    if (collect_raw_toggle_list_choices(frame)) {
        return true;
    }
    /*
     * **名指しの画面を先に見る**（その3 / c）。設定や知識には店・建物の手掛かりが無いので
     * `classify_menu_screen()` は `None` を返す……はずだが、知識だけは
     * ` ESC) 抜ける` と `コマンド:` の両方を持っていて **`Store` と誤判定される**。
     * 後回しにすると項目が 1 つも選べないまま「ページ送りだけ選べる画面」になる（実測）。
     */
    int store_command_line = -1;
    if(classify_menu_screen(frame,store_command_line)==MenuScreen::ItemPrompt){collect_item_prompt_choices(frame);return !frame.menu_choices.empty();}
    const int named=collect_named_menu_choices(frame);
    if(named!=0)return named>0;

    // Last-resort semantic list support for live Gensoband class-power screens
    // which have no core cursor and no stable named signature.  Keep this after
    // all named screens and before generic Store/Building parsing.
    int unnamed_command_line = -1;
    if (classify_menu_screen(frame, unnamed_command_line) == MenuScreen::None
        && collect_strict_unnamed_letter_list(frame)) {
        return true;
    }

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

int menu_semantics_selftest()
{
    int failed = 0;
    const auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::fprintf(stderr, "[gensoband] menu selftest FAIL: %s\n", what);
        ++failed;
    };
    const auto add_line = [](GameFrame &frame, int row, std::string text) {
        TermMirrorLine line;
        line.source_row = row;
        line.text_utf8 = std::move(text);
        frame.menu_term_lines.push_back(std::move(line));
    };
    const auto has_key = [](const GameFrame &frame, int key) {
        return std::any_of(frame.menu_choices.begin(), frame.menu_choices.end(),
            [key](const MenuChoice &choice) { return choice.key == key; });
    };
    const auto prompt_has_key = [](const PromptBar &prompt, int key) {
        return std::any_of(prompt.choices.begin(), prompt.choices.end(),
            [key](const PromptChoice &choice) { return choice.key == key; });
    };

    {
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, 0, "(a-c, *=List, ESC) Select");
        check(collect_raw_toggle_list_choices(frame), "raw toggle hidden stage recognized");
        check(has_key(frame, '*'), "raw toggle hidden stage exposes list key");
    }

    {
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, 0, "(a-c, *=List, ESC) Select");
        add_line(frame, 2, std::string(18, ' ') + "a) First");
        add_line(frame, 3, std::string(18, ' ') + "b) Second");
        add_line(frame, 4, std::string(18, ' ') + "c) Third");
        check(collect_raw_toggle_list_choices(frame), "raw toggle list recognized");
        check(has_key(frame, 'a') && has_key(frame, 'b') && has_key(frame, 'c'),
            "raw toggle list choices");
    }
    {
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, 0, "(a-c) Command:");
        add_line(frame, 3, "    a) First");
        add_line(frame, 4, "    b) Second");
        add_line(frame, 5, "    c) Third");
        check(collect_autopick_command_menu(frame), "auto-pick command box recognized");
        check(has_key(frame, 'a') && has_key(frame, 'b') && has_key(frame, 'c'),
            "auto-pick command choices");

        /* Nested Auto-pick command menus use the identical grammar but may
         * expose a shorter range.  Exercise a second level explicitly. */
        GameFrame nested;
        nested.menu_open = true;
        add_line(nested, 0, "(a-b) Command:");
        add_line(nested, 3, "    a) Nested first");
        add_line(nested, 4, "    b) Nested second");
        check(collect_autopick_command_menu(nested), "nested auto-pick command box recognized");
        check(has_key(nested, 'a') && has_key(nested, 'b'), "nested auto-pick command choices");
    }
    {
        PromptBar prompt;
        check(parse_prompt_line("Please verify SUICIDE by typing the '@' sign", prompt), "Genso raw verification recognized");
        check(prompt_has_key(prompt, '@') && prompt_has_key(prompt, 27), "Genso raw verification controls");
        check(parse_prompt_line("Choose discipline, w for the Force", prompt), "Genso Force prompt recognized");
        check(prompt_has_key(prompt, 'w') && prompt_has_key(prompt, 27), "Genso Force controls");
        check(parse_prompt_line("Look q,t,p,m,+,-,<dir>", prompt), "Genso Look prompt recognized");
        for (const int key : std::array<int, 5>{'+','-','p','m','t'}) {
            check(prompt_has_key(prompt, key), "Genso Look auxiliary controls");
        }
        // 調-13：見る（l）の帯に、見ている物の名前（行 0 の頭の説明）が残る。`-more-` は残らず、説明の直後に最初の選択肢の `[` が来る。
        struct LookCase { const char *name; const char *row; const char *subject; };
        int look_ok = 0;
        for (const LookCase &lc : std::array<LookCase, 3>{{
                 {"look_monster", "a Filthy street urchin (Asleep) [r,/,+,-,p,q,o,<dir>]", "a Filthy street urchin (Asleep)"},
                 {"look_item_bracket", "You see a Soft Leather Armor [2,+0] [r,/,+,-,p,q,o,<dir>]", "You see a Soft Leather Armor [2,+0]"},
                 {"look_more_dropped", "a Cave spider -more- [r,/,+,-,p,q,o,<dir>]", "a Cave spider"}}}) {
            PromptBar look;
            bool ok = parse_prompt_line(lc.row, look) && !look.choices.empty();
            if (ok) {
                const std::size_t at = look.text_utf8.find(lc.subject);
                ok = (at != std::string::npos) && (look.text_utf8.find("-more-") == std::string::npos)
                    && (look.text_utf8.find('[', at + std::strlen(lc.subject)) == static_cast<std::size_t>(look.choices.front().begin) - 1);
            }
            if (!ok) std::fprintf(stderr, "[gensoband] menu selftest look case %s band=\"%s\"\n", lc.name, look.text_utf8.c_str());
            check(ok, "Genso Look band keeps the name of what is looked at");
            if (ok) ++look_ok;
        }
        std::fprintf(stderr, "[gensoband] menu selftest: look band name %d/3 ok\n", look_ok);
    }
    {
        //! 見る `l` の行が Term の幅（80 桁）で切れて、案内（`<dir>`・`q止`）が落ちた行でも、帯が出て名前が残る（共用の `presentation::look_row_was_cut()`）。
        //! 敵の行（頭に `[<経験値>]`）だけ `t` が出る。
        struct CutCase { const char *name; std::string row; std::string subject; bool band; bool target_key; };
        const auto u8s = [](const char8_t *s) { return std::string(reinterpret_cast<const char *>(s)); };
        const std::array<CutCase, 7> cut_cases{{
            {"cut_en_tail_clipped", "[??]Target:Dercynus the Heavy Drinker(Level ???, unhurt, Aq) [r, q,t,p,o,+,-,<di", "[??]Target:Dercynus the Heavy Drinker(Level ???, unhurt, Aq)", true, true},
            {"cut_en_tail_gone", "[??]Target:Demunza, the God of the Underworld and Demon Realm(Level ???, unhurt, A", "[??]Target:Demunza, the God of the Underworld and Demon Realm(Level ???, unhurt, A", true, true},
            {"cut_en_item", "Target:a Potion of Resist Heat and Cold of the Very Long Name of Testing [2,+0] [q,p,o,+", "Target:a Potion of Resist Heat and Cold of the Very Long Name of Testing [2,+0]", true, false},
            {"cut_ja_tail_gone", u8s(u8"[??]ターゲット:グレーター・サイバー・ワイアーム・天使・デーモン・苔(レベル???, 無傷, 地) [r思 q止 t決 p自 o現 +次"), u8s(u8"[??]ターゲット:グレーター・サイバー・ワイアーム・天使・デーモン・苔(レベル???, 無傷, 地)"), true, true},
            {"cut_ja_in_name", u8s(u8"ターゲット:ブラックマーケットのエージェントの強欲に染まる白銀の弱虫『サイモン』のそのまた弟子の弟子の弟子"), u8s(u8"ターゲット:ブラックマーケットのエージェントの強欲に染まる白銀の弱虫『サイモン』のそのまた弟子の弟子の弟子"), true, false},
            {"short_target_line", "Target:a Cave spider", "", false, false},
            {"more_line", "Target:Dercynus the Heavy Drinker(Level ???, unhurt, Aq) and so on and on -more-", "", false, false}}};
        int cut_ok = 0;
        for (const CutCase &cc : cut_cases) {
            PromptBar band;
            const bool got = parse_prompt_line(cc.row, band);
            bool ok = (got == cc.band);
            if (ok && cc.band) {
                ok = (band.text_utf8.find(cc.subject) != std::string::npos) && prompt_has_key(band, '+') && prompt_has_key(band, '-')
                    && prompt_has_key(band, 'p') && prompt_has_key(band, 'o') && (prompt_has_key(band, 't') == cc.target_key);
            }
            if (!ok) std::fprintf(stderr, "[gensoband] menu selftest cut look case %s got=%d band=\"%s\"\n", cc.name, got ? 1 : 0, band.text_utf8.c_str());
            check(ok, "Genso Look band appears for a row cut at the Term width");
            if (ok) ++cut_ok;
        }
        std::fprintf(stderr, "[gensoband] menu selftest: cut look band %d/7 ok\n", cut_ok);
    }
    {
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_core_cursor = MenuChoice{-1, 0, 0, 0, 0, 0};
        add_line(frame, 0, "Choose power (ESC to cancel)");
        add_line(frame, 2, "          a) First");
        add_line(frame, 3, "          b) Second");
        add_line(frame, 4, "          c) Third");
        check(collect_strict_unnamed_letter_list(frame), "unnamed vertical list recognized");
        check(has_key(frame, 'a') && has_key(frame, 'b') && has_key(frame, 'c'),
            "unnamed vertical list choices");
    }
    {
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, 0, "Select information");
        frame.menu_term_curs_row = 0;
        add_line(frame, 2, std::string(16, ' ') + "a/u/U) race powers");
        add_line(frame, 3, std::string(16, ' ') + "b/m/M) magic");
        add_line(frame, 4, std::string(16, ' ') + "c/j/J) class powers");
        check(collect_named_menu_choices(frame) > 0, "browse selector recognized");
        for (const int key : {'a','u','U','b','m','M','c','j','J'})
            check(has_key(frame, key), "browse selector aliases");
    }
    {
        //! F-gensoband-09：賭け試合の建物の最初の画面は、主の名が出ていても一覧ではなく建物として扱う。
        const auto melee = std::find_if(std::begin(kNamedMenus), std::end(kNamedMenus),
            [](const NamedMenuSpec &spec) { return std::string(spec.tag) == "melee_arena"; });
        check(melee != std::end(kNamedMenus), "melee arena spec exists");
        if (melee != std::end(kNamedMenus)) {
            const std::string master = utf8_of(melee->signature, melee->signature_len);
            const auto bldg_lines = [&](GameFrame &frame) {
                frame.menu_open = true;
                add_line(frame, 2, master);
                add_line(frame, 5, std::string(reinterpret_cast<const char *>(u8"手持ちのお金:   1000")));
                add_line(frame, 21, std::string(reinterpret_cast<const char *>(u8"g) 話しかける")));
                add_line(frame, 23, std::string(reinterpret_cast<const char *>(u8"ESC) 建物を出る")));
            };
            GameFrame first;
            bldg_lines(first);
            first.menu_term_curs_row = 3;
            check(collect_named_menu_choices(first) == 0, "melee arena first screen is not a named list");
            check(fill_store_menu_choices(first) && has_key(first, 'g'), "melee arena first screen offers g");

            GameFrame list;
            bldg_lines(list);
            add_line(list, 6, std::string(reinterpret_cast<const char *>(u8"[a](x  3.83) リリーホワイト")));
            add_line(list, 7, std::string(reinterpret_cast<const char *>(u8"[b](x  1.20) 藍")));
            add_line(list, 0, std::string(reinterpret_cast<const char *>(u8"どれに賭ける?")));
            list.menu_term_curs_row = static_cast<int>(list.menu_term_lines.size()) - 1;
            check(collect_named_menu_choices(list) > 0 && has_key(list, 'a') && has_key(list, 'b'),
                "melee arena list keeps its choices");

            GameFrame descended;
            bldg_lines(descended);
            add_line(descended, 6, std::string(reinterpret_cast<const char *>(u8"[a](x  3.83) リリーホワイト")));
            descended.menu_term_curs_row = 4; // 札の行（行 6）にカーソルが居る＝行 0 の一覧の待ちではない（降りている）
            check(collect_named_menu_choices(descended) < 0, "melee arena descended stays blocked");
            GameFrame typing;
            bldg_lines(typing);
            add_line(typing, 6, std::string(reinterpret_cast<const char *>(u8"[a](x  3.83) リリーホワイト")));
            typing.text_input_active = true;
            check(collect_named_menu_choices(typing) < 0, "melee arena quantity input keeps [b] out of choices");
        }
    }
    {
        //! 修-24：英語層の目印（店・建物・誕生の見出し・名指しの画面）。日本語のときは英語の目印を見ないことも確かめる。
        const auto english_store_lines = [&](GameFrame &frame) {
            frame.menu_open = true;
            add_line(frame, 6, "a) Rations of Food");
            add_line(frame, 20, "Command:");
            add_line(frame, 21, "p) Purchase an item   s) Sell an item");
            add_line(frame, 22, "ESC) Exit building");
        };
        GameFrame ja_screen;
        english_store_lines(ja_screen);
        check(!g_menu_english_for_test, "english marker flag starts off");
        int command_line = -1;
        check(classify_menu_screen(ja_screen, command_line) == MenuScreen::None,
            "english store text is not a store while the japanese layer is on");

        g_menu_english_for_test = true;
        GameFrame store;
        english_store_lines(store);
        check(classify_menu_screen(store, command_line) == MenuScreen::Store && command_line == 1,
            "english store screen is a store");
        check(fill_store_menu_choices(store) && has_key(store, 'p') && has_key(store, 's'),
            "english store screen offers its commands");

        GameFrame building;
        building.menu_open = true;
        add_line(building, 5, "Gold Remaining: 1000");
        add_line(building, 21, "g) Talk");
        add_line(building, 23, "ESC) Exit building");
        check(classify_menu_screen(building, command_line) == MenuScreen::Building, "english building screen is a building");
        check(fill_store_menu_choices(building) && has_key(building, 'g'), "english building screen offers g");

        //! 英語の別の画面（コマンド行だけ・ESC の行だけ）は店と見なさない。
        GameFrame only_command;
        only_command.menu_open = true;
        add_line(only_command, 20, "Command: Load a user pref file");
        check(classify_menu_screen(only_command, command_line) == MenuScreen::None, "english command line alone is not a store");
        GameFrame only_esc;
        only_esc.menu_open = true;
        add_line(only_esc, 22, "ESC) Exit menu");
        check(classify_menu_screen(only_esc, command_line) == MenuScreen::None, "english ESC line alone is not a store");

        //! 誕生の見出し（難易度・出自・作品）。黄色の `a)` の枠がコアのカーソルになる。
        for (const char *heading : { "Choose the difficulty:", "You are...", "Choose the work you come from. (*: random)" }) {
            GameFrame birth;
            birth.pre_game_menu = true;
            add_line(birth, 3, heading);
            add_line(birth, 5, "a)EASY");
            birth.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 0, 6, 11 });
            check(fill_birth_selection_cursor(birth) && birth.menu_core_cursor.line_index >= 0, heading);
        }
        GameFrame birth_other;
        birth_other.pre_game_menu = true;
        add_line(birth_other, 3, "Enter your name");
        add_line(birth_other, 5, "a)EASY");
        birth_other.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 0, 6, 11 });
        check(!fill_birth_selection_cursor(birth_other), "unrelated english text is not a birth heading");

        //! 英語の名指しの画面（形は日本語の同名の項目と同じ）。
        for (const NamedMenuSpec &spec : kNamedMenus) {
            const std::string tag(spec.tag);
            if (tag.size() < 3 || tag.compare(tag.size() - 3, 3, "_en") != 0 || tag == "stat_raise_en" || tag == "melee_arena_en") continue;
            GameFrame named;
            named.menu_open = true;
            const int sig_row = spec.signature_row >= 0 ? spec.signature_row : spec.prompt_row;
            add_line(named, sig_row, utf8_of(spec.signature, spec.signature_len));
            const std::size_t sig_index = named.menu_term_lines.size() - 1;
            add_line(named, spec.row_min, std::string(static_cast<std::size_t>((std::max)(0, spec.column_min)), ' ') + "a) entry");
            named.menu_term_curs_row = static_cast<int>(sig_index);
            check(collect_named_menu_choices(named) > 0 && has_key(named, 'a'), tag.c_str());
        }
        {
            GameFrame arena;
            arena.menu_open = true;
            add_line(arena, 2, "Freckled Kappa Girl");
            add_line(arena, 6, "[a](x  3.83) Lily White");
            add_line(arena, 0, "Who do you bet on?");
            arena.menu_term_curs_row = static_cast<int>(arena.menu_term_lines.size()) - 1;
            check(collect_named_menu_choices(arena) > 0 && has_key(arena, 'a'), "english melee arena list");
            GameFrame arena_first;
            arena_first.menu_open = true;
            add_line(arena_first, 2, "Freckled Kappa Girl");
            add_line(arena_first, 5, "Gold Remaining: 1000");
            add_line(arena_first, 21, "g) Talk");
            add_line(arena_first, 23, "ESC) Exit building");
            arena_first.menu_term_curs_row = 3;
            check(collect_named_menu_choices(arena_first) == 0, "english melee arena first screen is not a named list");
            check(fill_store_menu_choices(arena_first) && has_key(arena_first, 'g'), "english melee arena first screen offers g");
        }
        g_menu_english_for_test = false;
    }
    const auto named_aux = [&](const char *tag, int wanted_key) {
        const auto it = std::find_if(std::begin(kNamedMenus), std::end(kNamedMenus),
            [tag](const NamedMenuSpec &spec) { return std::string(spec.tag) == tag; });
        check(it != std::end(kNamedMenus), "named auxiliary spec exists");
        if (it == std::end(kNamedMenus)) return;
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, it->signature_row, utf8_of(it->signature, it->signature_len));
        const int prompt_row = it->prompt_row;
        add_line(frame, prompt_row, std::string(static_cast<std::size_t>((std::max)(0, it->column_min)), ' ') + "a) entry");
        frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size()) - 1;
        check(collect_named_menu_choices(frame) > 0, tag);
        check(has_key(frame, wanted_key), tag);
    };
    named_aux("marisa_magic_carry", 'R');
    named_aux("soldier_bullet_use", '?');
    named_aux("soldier_bullet_info", '?');

    return failed;
}

} // namespace gb
