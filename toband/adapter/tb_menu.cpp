/*!
 * @file tb_menu.cpp
 * @brief カーソルとプロンプト（ （幻想蛮怒）` S2 の後半・S4）。
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
#include "tb_menu.h"
#include "bridge/input_event_adapter.h"
#include "bridge/symbol_prompt.h"
#include "bridge/look_screen_cover.h"
#include "frame/message_more.h"

#include "tb_lang_c.h" //GB: english layer #33 - the prompt-bar labels never reach Term, so #1 cannot see them
#include "tb_shim.h"
#include "tb_text.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <deque>

namespace tb {

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

    if ((text.find("<dir>") != std::string::npos || text.find(reinterpret_cast<const char *>(u8"q止")) != std::string::npos)
        && text.find('p') != std::string::npos && (text.find('+') != std::string::npos || text.find('-') != std::string::npos)) {
        // 見る・狙うの途中で `r`（モンスターの思い出）などが画面を退避して重なっている間は、帯を作らない（`look_screen_cover.h`）。
        if (presentation::look_screen_cover().covered(tb_screen_depth())) {
            out = PromptBar{};
            return false;
        }
        //! `tb_lang_enabled()` は**英語層が生きているとき**に真（`tb_lang_c.h`）。以前は向きが逆で、日本語の画面に英語の札が出ていた（2026-10-07）。
        const bool ja=tb_lang_enabled()==0;
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
            if(const std::size_t open=text.rfind('[');open!=std::string::npos)subject=text.substr(0,open);
            const auto trim=[](std::string &s){while(!s.empty()&&s.back()==' ')s.pop_back();std::size_t i=0;while(i<s.size()&&s[i]==' ')++i;s.erase(0,i);};
            trim(subject);
            for(const char *const more:{"-more-",reinterpret_cast<const char *>(u8"-続く-")}){const std::string m(more);if(subject.size()>=m.size()&&subject.compare(subject.size()-m.size(),m.size(),m)==0){subject.erase(subject.size()-m.size());trim(subject);}}
            out.text_utf8+=subject;
        }
        if(text.find('+')!=std::string::npos)add('+',reinterpret_cast<const char *>(u8"次の注目地点"),"Next interesting");
        if(text.find('-')!=std::string::npos)add('-',reinterpret_cast<const char *>(u8"前の注目地点"),"Previous interesting");
        add('p',reinterpret_cast<const char *>(u8"自分の位置へ戻る"),"Recenter on player");
        if(text.find("m,")!=std::string::npos||text.find(reinterpret_cast<const char *>(u8"m近"))!=std::string::npos)add('m',reinterpret_cast<const char *>(u8"近い注目地点へ"),"Nearest interesting");
        if(text.find("o,")!=std::string::npos||text.find(reinterpret_cast<const char *>(u8"o現"))!=std::string::npos)add('o',reinterpret_cast<const char *>(u8"現在位置を自由に見る"),"Manual/current mode");
        if(text.find("t")!=std::string::npos)add('t',reinterpret_cast<const char *>(u8"ここを対象にする"),"Select target");
        out.footer_utf8=ja?reinterpret_cast<const char *>(u8"LB/RB: 操作を選ぶ  A/Enter: 実行  十字キー: カーソル移動"):"LB/RB: select action  A/Enter: run  D-pad: move look cursor";
        return !out.choices.empty();
    }

    if ((text.find("Please verify SUICIDE by typing the '@' sign") != std::string::npos)
        || ((text.find("'@'") != std::string::npos) && (text.find(reinterpret_cast<const char *>(u8"確認")) != std::string::npos))) {
        out.text_utf8 = text;
        //! 札は日本語の画面では日本語（`tb_lang_enabled()` が真なら英語層）。
        const bool en = (tb_lang_enabled() != 0);
        out.choices.push_back({ en ? "Confirm (@)" : reinterpret_cast<const char *>(u8"確認 (@)"), '@', 0, 0 });
        out.choices.push_back({ en ? "Cancel (Esc)" : reinterpret_cast<const char *>(u8"取消 (Esc)"), 27, 0, 0 });
        return true;
    }
    if (text.find("w for the Force") != std::string::npos
        || text.find(reinterpret_cast<const char *>(u8"'w'練気")) != std::string::npos) {
        out.text_utf8 = text;
        const bool en = (tb_lang_enabled() != 0);
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

#define TB_OPT(lit, en, k) { lit, sizeof(lit) - 1, en, sizeof(en) - 1, k }
    static const Pattern kPatterns[] = {
        { "[y/n]", false, { TB_OPT("はい", "Yes", 'y'), TB_OPT("いいえ", "No", 'n') } },
        { "[Y/n]", false, { TB_OPT("はい", "Yes", 'Y'), TB_OPT("いいえ", "No", 'n') } },
        /*!
         * クイックスタートの問い（`birth.c:7892`）。**行 0 ではない**（行 14 に出るので、
         * 下の `fill_row0_prompt()` の走査が拾う）。GU-09 — 変愚の同じ表
         * （`presentation_bridge.cpp` の `kPatterns`）には在ったのに、
         * ここへ写すとき 1 行落ちていた。コアは `y` / `Y` **以外をすべて「いいえ」**
         * として扱う（同 `:7913`）ので、無いとパッドの A で黙って新規作成へ倒れる。
         */
        { "[y/N]", false, { TB_OPT("はい", "Yes", 'y'), TB_OPT("いいえ", "No", 'N') } },
        /*!
         * クイックスタートとアストラルモードの問い（`birth.c:1916`・`:1937`）。
         * 文言は `…[y/n, 'Q'終了]`（日本語）／`… [y/n, 'Q' Quit]`（英語）で、**行 0 ではなく**
         * 行 2〜5 に出る（下の `fill_row0_prompt()` の走査が拾う）。頭が `[y/n, 'Q'` で行末が `]` の
         * 前方一致で引く（日英で綴りが違うため）。`Q` は原作が `quit()` を呼ぶ**ゲームの終了**なので、
         * 選択肢には出さない（取り違えて押すと遊びが終わる）。コアは `y`/`Y`＝はい、`n`/`N`＝いいえ、
         * `?`＝ヘルプで、`Q` と ESC 以外のキーは無視して待ち続ける。
         */
        { "[y/n, 'Q'", true, { TB_OPT("はい", "Yes", 'y'), TB_OPT("いいえ", "No", 'n') } },
        /*!
         * ガンナー・エレメンタラーが先頭で仲間が 2 人以上のときの `m` の問い（`dungeon.c:3962`。
         * `get_com("a) 職業の特殊能力  b) 魔法書 : ", …)`）。文言は日英とも日本語のまま
         * （原作が `#ifdef JP` を付けていない）。`a` は職業の特殊能力（ガンナーの技術・エレメントの操作）、
         * `b` は魔法書（`do_cmd_cast()`）。それ以外のキーは原作が何もせず抜ける。
         */
        { reinterpret_cast<const char *>(u8"a) 職業の特殊能力  b) 魔法書 :"), false,
            { TB_OPT("職業の特殊能力", "Class powers", 'a'), TB_OPT("魔法書", "Spell books", 'b') } },
        //! アイテム破壊（`cmd3.c:2309`）。`A` は「以後自動で壊す」（自動拾い登録）。
        { "[y/n/Auto]", false,
            { TB_OPT("はい", "Yes", 'y'), TB_OPT("いいえ", "No", 'n'),
                TB_OPT("以後自動で", "Always", 'A') } },
        { "[(O)k/(C)ancel]", false,
            { TB_OPT("OK", "OK", 'O'), TB_OPT("キャンセル", "Cancel", 'C') } },
        //! 記念撮影（`cmd4.c:6422`）。
        { "[(y)es/(h)tml/(n)o]", false,
            { TB_OPT("テキスト", "Text", 'y'), TB_OPT("HTML", "HTML", 'h'),
                TB_OPT("いいえ", "No", 'n') } },
        //! ペットを放す（`cmd5.c:2639`）。頭数が入るので**前方一致**で引く。
        { "[Yes/No/Unnamed", true,
            { TB_OPT("はい", "Yes", 'Y'), TB_OPT("いいえ", "No", 'N'),
                TB_OPT("名前なしを全部", "Unnamed", 'U') } },
        /*!
         * ガンナーの製造・強化の問い（`object2.c:8152`・`:8259`・`:8290`・`:9184`・`:9340`・`:9348`）。どれも `get_com()` が行 0 へ
         * `[B]通常弾, [R]ライフル弾, [S]散弾 :` のように描き、`inkey()` で 1 文字を読む。問いが窓に出ないと、この問いに答える手段が
         * 文字キーしか無く、問い自体も画面に出なかった（気づいたこと 2026-10-18 の同じ種類の不具合）。
         * 日本語の文言は `#ifdef JP` の枝そのまま。英語層では英語の枝の文言（`Create [B]ullet, …`）になる。
         * 札のキーは大文字（原作は `switch (ch)` で `'B'`・`'b'` の両方を受ける）。
         */
        { reinterpret_cast<const char *>(u8"[B]通常弾, [R]ライフル弾, [S]散弾 :"), false,
            { TB_OPT("通常弾", "Bullet", 'B'), TB_OPT("ライフル弾", "Round", 'R'), TB_OPT("散弾", "Shell", 'S') } },
        { "Create [B]ullet, [R]ound or [S]hell ?", false,
            { TB_OPT("通常弾", "Bullet", 'B'), TB_OPT("ライフル弾", "Round", 'R'), TB_OPT("散弾", "Shell", 'S') } },
        { reinterpret_cast<const char *>(u8"[M]ミスリル, [A]アダマンタイト :"), false,
            { TB_OPT("ミスリル", "Mithril", 'M'), TB_OPT("アダマンタイト", "Adamantite", 'A') } },
        { "Create from [M]ithril or [A]damantite ?", false,
            { TB_OPT("ミスリル", "Mithril", 'M'), TB_OPT("アダマンタイト", "Adamantite", 'A') } },
        { reinterpret_cast<const char *>(u8"[R]ロケット弾, [A]反重力ロケット弾 :"), false,
            { TB_OPT("ロケット弾", "Rocket", 'R'), TB_OPT("反重力ロケット弾", "Anti-gravity Rocket", 'A') } },
        { "Create [R]ocket or [A]nti-gravity Rocket ?", false,
            { TB_OPT("ロケット弾", "Rocket", 'R'), TB_OPT("反重力ロケット弾", "Anti-gravity Rocket", 'A') } },
        { reinterpret_cast<const char *>(u8"[E]修正強化, [S]高速射撃化 :"), false,
            { TB_OPT("修正強化", "Enchant", 'E'), TB_OPT("高速射撃化", "Extra shot", 'S') } },
        { "[E]nchant or add extra [S]hot ?", false,
            { TB_OPT("修正強化", "Enchant", 'E'), TB_OPT("高速射撃化", "Extra shot", 'S') } },
        { reinterpret_cast<const char *>(u8"[E]修正強化, [M]強力射撃化, [S]高速射撃化 :"), false,
            { TB_OPT("修正強化", "Enchant", 'E'), TB_OPT("強力射撃化", "Might", 'M'), TB_OPT("高速射撃化", "Extra shot", 'S') } },
        { "[E]nchant, make [M]ight or add extra [S]hot ?", false,
            { TB_OPT("修正強化", "Enchant", 'E'), TB_OPT("強力射撃化", "Might", 'M'), TB_OPT("高速射撃化", "Extra shot", 'S') } },
        /*!
         * カジノの「もう一度」（`bldg.c:1582`。`inkey()` で `Y`／`y` だけが「はい」、ほかは「いいえ」）と、
         * モンスターの記号を調べる `/` の「思い出を見ますか」（`cmd3.c:2011`。`k`＝殺害数の順で見る・`y`＝見る・`n`＝やめる）。
         * どちらも `[y/n]` の綴りではないので、上の表では引けなかった。
         */
        { reinterpret_cast<const char *>(u8"もう一度(Y/N)？"), false, { TB_OPT("はい", "Yes", 'Y'), TB_OPT("いいえ", "No", 'N') } },
        { "Again(Y/N)?", false, { TB_OPT("はい", "Yes", 'Y'), TB_OPT("いいえ", "No", 'N') } },
        { reinterpret_cast<const char *>(u8"(k:殺害順/y/n):"), false,
            { TB_OPT("殺害順で見る", "By kills", 'k'), TB_OPT("見る", "Yes", 'y'), TB_OPT("やめる", "No", 'n') } },
        { "Recall details? (k/y/n):", false,
            { TB_OPT("殺害順で見る", "By kills", 'k'), TB_OPT("見る", "Yes", 'y'), TB_OPT("やめる", "No", 'n') } },
    };
#undef TB_OPT

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
            const bool en = (tb_lang_enabled() != 0); //GB: english layer #33
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

//! 主 Term の 1 行を UTF-8 で読む（制御文字は空白に倒す。`tb_frame.cpp` と同じ規則）。
std::string term_row_utf8(int y)
{
    char text[kRowBytes];
    const int n = tb_term_row(y, text, nullptr, kRowBytes);
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
    //! `a/u/U)`: Toband の browse 種別は 1 札に 3 個の同義キーを持つ。
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
 * @brief ガンナーの技術メニューの問い（行 0）の、化けた頭の字を直す。
 * @details 原作は `どの種類の技術を使いますか？`（`only_browse` なら `調べますか？`）・`どの能力を使いますか:` と描くが、
 * 実機の Term のミラーでは行 0 の桁 2〜3 の字（`どの` の `の`）が空白 2 つに化ける（`ど  種類の…`。実機で確かめた。
 * 化ける理由は突き止めていない）。窓の題は行 0 の字から決まるので、頭が `ど` ＋空白 2 つなら `の` に戻す
 * （問いが 1 つの文として読めるようになり、題が問いの文になる）。**この 2 つの画面だけ**で呼ぶ。
 */
void repair_gunner_question(GameFrame &frame)
{
    static const char kDo[] = "ど";
    const std::string &prefix = utf8_of(kDo, sizeof(kDo) - 1);
    static const char kNo[] = "の";
    const std::string &no = utf8_of(kNo, sizeof(kNo) - 1);
    for (auto &line : frame.menu_term_lines) {
        if (line.source_row != 0) {
            continue;
        }
        std::string &text = line.text_utf8;
        if ((text.compare(0, prefix.size(), prefix) == 0) && (text.compare(prefix.size(), 2, "  ") == 0)
            && (text.size() > prefix.size() + 2) && (text[prefix.size() + 2] != ' ')) {
            text.replace(prefix.size(), 2, no);
            //! 色の範囲も合わせる：化けた 2 桁（無色）は 3 バイトの `の` になり、あとの範囲は 1 バイトずれる。
            const uint8_t color = line.color_spans.empty() ? 1 : line.color_spans.front().color;
            const int lost = static_cast<int>(prefix.size());
            for (auto &span : line.color_spans) {
                if ((span.begin == lost) && (span.len == 2)) {
                    span.len = static_cast<int>(no.size());
                    span.color = color;
                } else if (span.begin > lost) {
                    span.begin += static_cast<int>(no.size()) - 2;
                }
            }
        }
        return;
    }
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
        show.label_utf8 = (tb_lang_enabled() != 0) ? "Show choices" : reinterpret_cast<const char *>(u8"一覧を見る");
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

/*!
 * @name 仲間の字のメニュー（`party.c`）
 *
 * @details 原作の仲間のメニューは `screen_save()` のうえに `prt()` で `a) 名前 …` の行を並べ、
 * `get_com()` か `inkey()` で**字のキーを 1 つ待つ**だけで、コアにカーソルが無い。
 * 画面側が行を選べるように、行ごとに `MenuChoice`（決定で積むキーはその行の字）を積む。
 * 並ぶ項目と字は原作が出しているままで、足したり並べ替えたりしない。
 *
 * 画面の見分けは見出しの文言（`kPartyMenus`）。行は `a)` から `p)`（`MAX_PARTY_MEMBERS` は 16）で、
 * 交代（`do_cmd_party`）だけ `)` の次が現在の操作中を示す `*`・戦闘不能を示す `X`・空白の 1 字になる
 * （`a)*名前`）。ほかのメニューは `a) 名前`。使えない仲間の行は原作が空行にする（行を出さない）ので、
 * 積まれるのは原作が出した行だけ。
 * @{
 */
struct PartyMenuSpec {
    const char *tag;
    const char *signature; //!< 見出しの文言（CP932。部分一致）
    std::size_t signature_len;
    int signature_row; //!< 見出しの行（Term の行）
};

constexpr int kPartyRowFirst = 2;
constexpr int kPartyMembersMax = 16;

#define TB_SIG(s) (s), (sizeof(s) - 1)

const PartyMenuSpec kPartyMenus[] = {
    //! 交代：`U` の先頭（広域マップは `U` で直接）。問いは `get_com()` が行 0 に出し、行 1 が列の見出し。
    { "party_switch", TB_SIG("誰に交代しますか？"), 0 },
    //! 倒れたときのタロット（`party_handle_death`）。5 枚から 1 枚。取消は無い。
    { "party_tarot", TB_SIG("残すタロットカードを1枚選んでください。"), 1 },
    //! 倒れたあとの交代（`party_handle_death`）。生き残りが 2 人以上のときだけ。取消は無い。
    { "party_successor", TB_SIG("交代する仲間を選んでください。"), 1 },
    //! 自宅の `T`（`do_cmd_party_train`）。見出しは上限、問いは `get_com()` が行 0 に出す。
    { "party_train", TB_SIG("トレーニング上限:"), 1 },
    //! 蘇生・ネクロマンシー（`party_resurrect_aux`）。
    { "party_resurrect", TB_SIG("蘇生する仲間を選んでください。"), 1 },
    //! 転生（`party_reincarnate`）。
    { "party_reincarnate", TB_SIG("転生する仲間を選んでください。"), 1 },
};

//! @brief 仲間のメニューの 1 行 `a)…` を読む。@return 読めたら字（`a`〜`p`）、読めなければ 0
char parse_party_row(const std::string &text, std::size_t &begin)
{
    begin = first_non_space(text);
    if ((begin + 1) >= text.size() || text[begin + 1] != ')') {
        return 0;
    }
    const char key = text[begin];
    if (key < 'a' || key >= static_cast<char>('a' + kPartyMembersMax)) {
        return 0;
    }
    return key;
}

//! @brief 仲間のメニューなら選択肢を積む。@return 積んだら true
bool fill_party_menu(GameFrame &frame)
{
    const PartyMenuSpec *found = nullptr;
    for (const PartyMenuSpec &spec : kPartyMenus) {
        const std::string &signature = utf8_of(spec.signature, spec.signature_len);
        const std::string translated = sjis_to_utf8(tb_tr(spec.signature));
        for (const auto &line : frame.menu_term_lines) {
            if (line.source_row != spec.signature_row) {
                continue;
            }
            if (line.text_utf8.find(signature) != std::string::npos
                || line.text_utf8.find(translated) != std::string::npos) {
                found = &spec;
            }
            break;
        }
        if (found != nullptr) {
            break;
        }
    }
    if (found == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const auto &line = frame.menu_term_lines[i];
        if (line.source_row < kPartyRowFirst || line.source_row >= kPartyRowFirst + kPartyMembersMax) {
            continue;
        }
        std::size_t begin = 0;
        const char key = parse_party_row(line.text_utf8, begin);
        if (key == 0) {
            continue;
        }
        std::size_t end = line.text_utf8.size();
        while (end > begin && line.text_utf8[end - 1] == ' ') {
            --end;
        }
        MenuChoice choice{};
        choice.line_index = static_cast<int>(i);
        choice.span_begin = static_cast<int>(begin);
        choice.span_len = static_cast<int>(end - begin);
        choice.key_begin = static_cast<int>(begin);
        choice.key_len = 1;
        choice.key = static_cast<unsigned char>(key);
        frame.menu_choices.push_back(choice);
    }
    if (frame.menu_choices.empty()) {
        return false;
    }
    /*
     * 問いが行 1 にある画面（タロット・交代する仲間・蘇生・転生・訓練の上限）は、行 0 が空で窓の題が決まらず
     * 「メニュー」になる。問いの文は窓の見出し（題の下）へ出す（`window.subtitle`）。原作の文のまま。
     */
    if (found->signature_row == 1) {
        for (const auto &line : frame.menu_term_lines) {
            if (line.source_row != 1) {
                continue;
            }
            std::string heading = line.text_utf8;
            const std::size_t first = heading.find_first_not_of(' ');
            const std::size_t last = heading.find_last_not_of(' ');
            if (first != std::string::npos) {
                frame.menu_box_header_utf8 = heading.substr(first, last - first + 1);
            }
            break;
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
     * 建物の選択肢が作られない（F-toband-09。賭け試合の最初の画面が決定で進まない）。
     */
    bool require_listed{false};
    /*!
     * @brief 札の枠を、札の頭から**この桁数まで**に切る（-1 なら切らない）。
     * @details 札の右に別の欄が同じ行へ重なる画面（クラスチェンジは右の欄に必要能力値が並ぶ）で、
     * 札の幅が欄の幅いっぱいだと空白 3 連の切れ目が無く、右の欄の字が札の名前に混ざる。
     */
    int span_cols{-1};
    //! 真なら、カーソルがある行（`menu_term_curs_row`）の札を現在の選択にする（`selected`。パッドの上下移動がコアのキーになる）。
    bool cursor_selected{false};
    //! 真なら、札の色が暗い灰（スレート）か青の行を選べない札にする（クラスチェンジの「選べないクラス」）。
    bool dim_unavailable{false};
    /*!
     * @brief 待ちの行（カーソルがある行）の頭がこの字であること（CP932。無ければ見ない）。
     * @details 下位の問いが**一覧の待ちの行と同じ行**へ書かれる画面がある。画面表示の設定は、一覧の待ち `コマンド:` も
     * ファイル名の問い `ファイル: ` も行 18 に書く（`cmd4.c:3581`・`:3606`。(1) だけ。(2)〜(4) は行 17）。行の位置だけでは降りたことが分からず、
     * ファイル名を打つ間も一覧が選択肢のまま残って、決定の数字がファイル名に混ざる。待ちの行の字で見分ける。
     */
    const char *prompt_prefix{nullptr};
};

#define TB_SIG(s) (s), (sizeof(s) - 1)

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
 * `tools/toband/mk_gu_saves.py`、記録は （幻想蛮怒）` §2.5）。
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
    { "browse_kind", TB_SIG("a/u/U)"), kStylePlain, 2, 4, 0, 0, 2, 16, 16 },
    //! レベルアップの能力値の選択（`xtra2.c` の `check_experience()`。行 1 が問い・行 2〜7 が `a) 腕力 (現在値 …)`）。
    { "stat_raise", TB_SIG("どの能力値を上げますか"), kStylePlain, 2, 7, 1, 1, 1, 20, 24 },
    //! 英語層のとき（カタログは**行全体**の鍵で引くので、部分文字列の署名は訳せない。英語の原文で別に持つ）。
    { "stat_raise_en", TB_SIG("Which stat do you want to raise"), kStylePlain, 2, 7, 1, 1, 1, 20, 24 },
    { "elemental_brand", TB_SIG("どの元素攻撃をしますか"), kStylePlain, 2, 6, 1, 1, 1, 20, 24 },
    { "elemental_immunity", TB_SIG("どの元素の免疫をつけますか"), kStylePlain, 2, 5, 1, 1, 1, 20, 24 },
    { "monk_stance", TB_SIG("どの構えをとりますか"), kStylePlain, 2, 10, 1, 1, 1, 20, 22 },
    { "samurai_stance", TB_SIG("どの型で構えますか"), kStylePlain, 2, 10, 1, 1, 1, 20, 22 },
    { "card_power", TB_SIG("(スペース:次のページ ESC:キャンセル)"), kStylePlain, 4, 25, 4, 26, -1, 20, 20 },
    { "marisa_present", TB_SIG("(a～h:プレゼントを選択 / n,スペース:次のページへ / ESC:キャンセル)"), kStylePlain, 8, 15, 8, 16, 6, 10, 10 },
    { "wanted_target", TB_SIG("誰を狙いますか？(ESC:キャンセル)"), kStylePlain, 15, 17, 14, 17, 14, 5, 5 },
    { "weapon_shape", TB_SIG("この柄をどんな武器にして欲しいのだ？"), kStylePlain, 2, 6, 2, 6, 0, 40, 40 },
    { "marisa_magic_info", TB_SIG("「どの魔法について確認しよう？」(ESC:キャンセル)"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "marisa_magic_carry", TB_SIG("「どの魔法を持って行こう？」"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "marisa_magic_make", TB_SIG("「どの魔法を作ろう？」(ESC:キャンセル)"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "marisa_magic_use", TB_SIG("「どの魔法を使おう？」(ESC:キャンセル)"), kStylePlain, 7, 20, 7, 20, 4, 18, 54 },
    { "compound_drug", TB_SIG("素材にするものを3つまで選んで下さい:"), kStylePlain, 10, 14, 10, 17, 8, 10, 10 },
    { "nightmare_diary", TB_SIG("累計評価点:"), kStylePlain, 9, 17, 19, 19, 19, 22, 22 },
    { "cooking_order", TB_SIG("何を作ってもらいますか？(ESC:キャンセル)"), kStylePlain, 5, 23, 5, 23, 4, 20, 20 },
    { "cooking_make", TB_SIG("何を作りますか？(ESC:キャンセル)"), kStylePlain, 5, 23, 5, 23, 4, 20, 20 },
    { "chosen_trap", TB_SIG("どのトラップを配置しよう？"), kStylePlain, 3, 23, 3, 23, 2, 30, 30 },
    { "prism_class", TB_SIG("誰と交代しますか？"), kStylePlain, 12, 14, 12, 14, 10, 10, 10 },
    { "drug_recipe", TB_SIG("どの薬について確認しますか？」(ESC:キャンセル)"), kStylePlain, 7, 22, 7, 22, 4, 20, 20 },
    { "hecatia_body", TB_SIG("どの体と交代しますか？"), kStylePlain, 12, 14, 12, 14, 10, 30, 30 },
    { "hecatia_earth", TB_SIG("あなたの「地球」の体はどんな性格ですか？"), kStylePlain, 12, 23, 12, 23, 10, 25, 25 },
    { "hecatia_moon", TB_SIG("あなたの「月」の体はどんな性格ですか？"), kStylePlain, 12, 23, 12, 23, 10, 25, 25 },
    { "soldier_skill_info", TB_SIG("どの技能の説明を見ますか？(ESC:終了)"), kStylePlain, 4, 23, 4, 23, 2, 18, 18 },
    { "soldier_skill_gain", TB_SIG("どの技能を習得しますか？(ESC:キャンセル)"), kStylePlain, 4, 23, 4, 23, 2, 18, 18 },
    { "soldier_bullet_info", TB_SIG("どの銃弾の説明を見ますか？(ESC:キャンセル ?:確認を終える)"), kStylePlain, 5, 20, 5, 20, 2, 18, 50 },
    { "soldier_bullet_use", TB_SIG("どの銃弾を使用しますか？(ESC:キャンセル ?:銃弾の効果を確認する)"), kStylePlain, 5, 20, 5, 20, 2, 18, 50 },
    { "ariya_wall", TB_SIG("a) 普通の壁を永久壁にする"), kStylePlain, 10, 11, 12, 12, 10, 20, 20 },
    { "nina_building", TB_SIG("どんな建物の蜃気楼を作りますか？(ESC:キャンセル)"), kStylePlain, 1, 25, 1, 25, -1, 25, 25 },
    { "nina_monster", TB_SIG("誰のコピーを呼び出しますか？(ESC:キャンセル)"), kStylePlain, 1, 25, 1, 25, -1, 25, 25 },
    { "chaos_effect", TB_SIG("起こしたい効果を記号で選択してください。"), kStylePlain, 3, 5, 5, 5, 0, 40, 40 },
    { "reincarnation", TB_SIG("転生したい種族を記号で選択してください。"), kStylePlain, 15, 17, 17, 17, 0, 40, 40 },
    //! 待ちの行は TOband では 19（`cmd4.c:2333` `prt("コマンド:", 19, 0)`。幻想蛮怒は 21 だった）。一覧は行 4〜17（詐欺オプションの行 17 まで）。
    { "option_root", TB_SIG("[ オプションの設定 ]"), kStyleParen, 3, 19, 19 },
    { "macros", TB_SIG("[ マクロの設定 ]"), kStyleParen, 4, 13, 16 },
    /*!
     * 画面表示の設定（`cmd4.c:3537` `do_cmd_visuals()`）。TOband の見出しは括弧なしの `画面表示の設定`（行 2）で、
     * 幻想蛮怒の `[ 画面表示の設定 ]` とは違う（以前の署名は外れていた）。一覧は `(1)`〜`(0)` が行 4〜13、
     * 待ちの `コマンド:` は日本語が行 18・英語が行 15（`:3581`・`:3583`）。
     */
    { "visuals", TB_SIG("画面表示の設定"), kStyleParen, 4, 13, 18, -1, 2, -1, -1, false, -1, false, false, "コマンド:" },
    { "visuals_en", TB_SIG("Interact with Visuals"), kStyleParen, 4, 13, 15, -1, 2 },
    /*!
     * エレメントの選択（`xtra2.c:7369` `choose_elem()`）。`screen_save()` のあと、問い `エレメントを選んでください。` を
     * 行 1 の桁 22 に、`a) 火`・`b) 水`・`c) 地`・`d) 風` を行 2〜5 の桁 22 に描き、`inkey()` で待つ（カーソルは行 1）。
     * `a`〜`d`（大文字も可）以外のキーは「選ばない」（`NO_ELEM`）になる。呼び出しは `racial.c:180`（ダーク・エレメントの
     * エレメント変更）・`do-spell.c:4440`・`:4774`（ウィッチの魔法）。
     */
    { "choose_elem", TB_SIG("エレメントを選んでください"), kStylePlain, 2, 5, 1, 1, 1, 22, 22 },
    /*!
     * ガンナーの技術メニュー（`object2.c:9557` `do_cmd_gunner()`。`use_menu` が偽のとき）。行 2〜12 の桁 16 に
     * `a) アイテムからの弾丸の製造`〜`k) 銃器の強化` を描き、行 0 の `get_com()` の問い
     * `どの能力を使いますか:`（`only_browse` なら `調べますか:`）で待つ。`z` と ESC は戻る。
     * `use_menu` が真のときは `》` のカーソルになる（`fill_core_cursor()` が受ける）。画面側（`cursor_mode` 真）では
     * いつもそちらで、この a〜k の型は `cursor_mode` が偽の接続でだけ出る。
     * **署名は問いの頭を含めない**：行 0 のミラーでは、頭の `どの` の `の` が空白 2 つに化けて
     * `ど  能力を使いますか:` になる（実機で確かめた。2026-10-18）。
     */
    { "gunner_tech", TB_SIG("能力を使いますか"), kStylePlain, 2, 12, 0, 0, 0, 16, 16 },
    { "gunner_tech_browse", TB_SIG("能力を調べますか"), kStylePlain, 2, 12, 0, 0, 0, 16, 16 },
    /*!
     * 自宅の `c` のクラスチェンジ（`store.c:4384` `change_player_class()`）。行 1 の見出し
     * `以下のメニューから新しいクラスを選んでください。`・行 10 から `a) クラス名`（桁 2。27 個目からは `A)`…）が並び、
     * 現在の選択の行へカーソルを置いて `inkey()` で待つ。選べないクラスは暗い灰（選択中は青）で描かれ、
     * 字を押しても選択が動くだけで決定にならない。右の桁 22 には必要能力値などが同じ行へ重なる
     * （札は 20 桁で切る）。決定すると行 0 の `よろしいですか？ [y/n]` に降りる。
     * 一覧の行数は端末の高さで決まる（`hgt = Term->hgt - 11`）ので、行の上限は端末いっぱいまで取る。
     */
    { "class_change", TB_SIG("新しいクラスを選んでください"), kStylePlain, 10, 60, 10, 60, 1, 2, 2, false, 20, true, true },
    { "colors", TB_SIG("[ カラーの設定 ]"), kStyleParen, 4, 6, 8 },
    { "diary", TB_SIG("[ 記録の設定 ]"), kStyleParen, 4, 7, 18 },
    //! 知識だけは丸括弧の項目と `ESC) 抜ける  SPACE) 次ページ` が同居する。両方の書式で読む。
    { "knowledge", TB_SIG("現在の知識を確認する"), kStyleParen | kStylePlain, 6, 21, 20 },
    { "recall_dungeon", TB_SIG("どのダンジョン"), kStylePlain, 1, 19, 0 },
    { "tele_town", TB_SIG("どこに行きますか"), kStylePlain, 6, 19, 0 },
    { "melee_arena", TB_SIG("雀斑の河童娘"), kStyleBracket, 4, 19, 0, -1, -1, -1, -1, true },
    /*
     * 英語層の署名（2026-10-12 修-24）。上の日本語の署名は**部分文字列**で、訳の表の鍵は行全体なので
     * `tb_tr()` では引けない（`stat_raise_en` と同じ事情）。訳は `lang/en/ui/*.txt`・`lang/en/edit/town.en.txt`
     * の英語。形（行・桁）は日本語の同名の項目と同じ。日本語の画面に英語の署名は出ないので、日本語の動きは変わらない。
     */
    { "elemental_brand_en", TB_SIG("Choose a temporary elemental brand"), kStylePlain, 2, 6, 1, 1, 1, 20, 24 },
    { "elemental_immunity_en", TB_SIG("Choose a temporary elemental immune"), kStylePlain, 2, 5, 1, 1, 1, 20, 24 },
    //! 武術家の構え・剣術家の型は英語では同じ問い（`Choose Form:`）。形も同じ。
    { "stance_en", TB_SIG("Choose Form:"), kStylePlain, 2, 10, 1, 1, 1, 20, 22 },
    { "nightmare_diary_en", TB_SIG("Total rating:"), kStylePlain, 9, 17, 19, 19, 19, 22, 22 },
    { "recall_dungeon_en", TB_SIG("Which dungeon do you"), kStylePlain, 1, 19, 0 },
    { "tele_town_en", TB_SIG("Where do you go:"), kStylePlain, 6, 19, 0 },
    //! 賭け試合の建物の主の名の英語（`lang/en/edit/town.en.txt` の `BE:` 行）。
    { "melee_arena_en", TB_SIG("Freckled Kappa Girl"), kStyleBracket, 4, 19, 0, -1, -1, -1, -1, true },
};

#undef TB_SIG

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
        const std::string translated=sjis_to_utf8(tb_tr(spec.signature));
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
        if (std::strncmp(spec.tag, "gunner_tech", 11) == 0) {
            repair_gunner_question(frame);
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
        if (spec.prompt_prefix != nullptr) {
            const std::string &prefix = utf8_of(spec.prompt_prefix, std::strlen(spec.prompt_prefix));
            const auto &cursor_line = frame.menu_term_lines[static_cast<std::size_t>(frame.menu_term_curs_row)].text_utf8;
            if (cursor_line.compare(first_non_space(cursor_line), prefix.size(), prefix) != 0) {
                return -1;
            }
        }
        const std::size_t first_new = frame.menu_choices.size();
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
        //! 札ごとの仕上げ（枠の幅・現在の選択・選べない札）。表の `span_cols`・`cursor_selected`・`dim_unavailable`。
        for (std::size_t ci = first_new; ci < frame.menu_choices.size(); ++ci) {
            MenuChoice &choice = frame.menu_choices[ci];
            const auto &line = frame.menu_term_lines[static_cast<std::size_t>(choice.line_index)];
            if (spec.span_cols >= 0) {
                //! 札の頭から `span_cols` 桁（CP932 の 1 バイト＝1 桁）までの、文字の切れ目の位置を採って右の空白を削る。
                const std::string rest = line.text_utf8.substr(static_cast<std::size_t>(choice.span_begin));
                std::size_t cut = 0;
                while (cut < rest.size()) {
                    std::size_t next = cut + 1;
                    while ((next < rest.size()) && ((static_cast<unsigned char>(rest[next]) & 0xC0) == 0x80)) {
                        ++next;
                    }
                    if (static_cast<int>(utf8_to_sjis(rest.substr(0, next)).size()) > spec.span_cols) {
                        break;
                    }
                    cut = next;
                }
                while ((cut > 0) && (rest[cut - 1] == ' ')) {
                    --cut;
                }
                choice.span_len = (std::min)(choice.span_len, static_cast<int>(cut));
            }
            if (spec.cursor_selected && (line.source_row == cursor_row)) {
                choice.selected = true;
            }
            if (spec.dim_unavailable) {
                for (const auto &color : line.color_spans) {
                    if ((color.begin <= choice.key_begin) && (choice.key_begin < color.begin + color.len)) {
                        const int attr = color.color & 15;
                        if ((attr == 2) || (attr == 6)) { //!< TERM_SLATE（選べない）・TERM_BLUE（選べないクラスを選択中）
                            choice.available = false;
                            choice.reason_utf8 = (tb_lang_enabled() != 0) ? "You cannot choose this class now."
                                                                          : reinterpret_cast<const char *>(u8"現在はこのクラスを選べません。");
                        }
                        break;
                    }
                }
            }
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
            const bool aux_en=(tb_lang_enabled()!=0);
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
bool menu_english() { return g_menu_english_for_test || (tb_lang_enabled() != 0); }
//! 店・建物の目印の英語の字（`tb_lang_enabled()` 真のときの `コマンド:`・`手持ちのお金:` の訳）。
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
     * 英語層（`tb_lang_enabled()` が真）では、同じ行の訳が `Command:`・`Gold Remaining:` になる
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
/*!
 * @brief 誕生の選択（性別・種族・クラス・エレメント）の、現在の選択を窓のカーソルにする。
 * @details 4 つとも `get_player_choice()`（`birth.c:874`）で、行 10 から `a) 名前` を並べ、**現在の選択の行だけ
 * `TERM_L_BLUE`（明るい青）・ほかは白**で描き、コアのカーソルは現在の行の頭へ置く（`put_str("", TABLE_ROW + cur - top, col)`）。
 * `》` は書かないので、色で探す。以前は幻想蛮怒の写しで**黄色**を探していたため、TOband では 1 度も当たらず、
 * 本文に `a)` `b)` が出るだけでカーソルで選べなかった。
 *
 * 選び終えた列は最後の色（明るい青）のまま残るので、色だけでは今の列が分からない。コアのカーソルの位置
 * （`menu_term_curs_row`・`menu_term_curs_col`）が合う札を採る（無ければ最も右の列）。
 * 最初の誕生（`pre_game_menu`）と、自宅の `a` の仲間の誕生（`party_birth_member()`。ゲーム中の窓）の両方で使う。
 * 文字を押すとその項目に決まり、Enter／スペースで現在の項目に決まる（上下は 8／2。窓のカーソルの移動がそのキーになる）。
 */
bool fill_birth_selection_cursor(GameFrame &frame)
{
    if(frame.text_input_active)return false;
    static const char *const headings[]={
        reinterpret_cast<const char*>(u8"以下のメニューからキャラクタの要素を選んでください"),
        // 英語（`birth.c` の `#else` の枝）。
        "Please select your character from the menu below",
        /*
         * テンプルナイトの必殺技の選択（`bldg.c:3375` の `get_temple_blow()`。クラスチェンジの直後）。誕生の選択と同じ作りで、
         * 行 10 から `a) 名前` を並べ、選択中の行は `TERM_L_BLUE`、カーソルは選択中の行の頭（`put_str("", TABLE_ROW + cur - top, CLASS_COL)`）。
         * 文字を押すとその項目に決まり（選べるときだけ）、`2`／`8` で動き、Enter で決める。
         */
        reinterpret_cast<const char*>(u8"以下のメニューから覚えたい必殺技を選んでください"),
        "Please select new special blow from the menu below"};
    bool selection=false;
    for(const auto &line:frame.menu_term_lines)for(const char *heading:headings)
        if(line.text_utf8.find(heading)!=std::string::npos)selection=true;
    if(!selection)return false;
    struct Candidate{int line;int begin;int len;int column;};
    std::vector<Candidate> found;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto &line=frame.menu_term_lines[i];
        //! 見出しの行（行 1）にも明るい青があるが、頭が `x)` の札だけを採る。
        for(size_t j=0;j<line.color_spans.size();++j) {
            const auto &span=line.color_spans[j];
            if((span.color&15)!=14 || span.begin<0 || span.len<=0)continue;
            const size_t begin=size_t(span.begin);
            if(begin+2>line.text_utf8.size())continue;
            const char key=line.text_utf8[begin];
            if(!((key>='a'&&key<='z')||(key>='A'&&key<='Z'))||line.text_utf8[begin+1]!=')')continue;
            int end=span.begin+span.len;
            for(size_t next=j+1;next<line.color_spans.size();++next){const auto &part=line.color_spans[next];if(part.begin!=end||(part.color&15)!=14)break;end+=part.len;}
            if(size_t(end)>line.text_utf8.size())continue;
            //! 札の末尾の空白（欄の幅に合わせた埋め草）は枠に入れない。
            while(end>span.begin&&line.text_utf8[size_t(end)-1]==' ')--end;
            found.push_back({int(i),int(begin),end-int(begin),int(utf8_to_sjis(line.text_utf8.substr(0,begin)).size())});
        }
    }
    if(found.empty())return false;
    const Candidate *pick=nullptr;
    for(const Candidate &c:found)if(c.line==frame.menu_term_curs_row&&c.column==frame.menu_term_curs_col){pick=&c;break;}
    if(!pick)for(const Candidate &c:found)if(!pick||c.column>pick->column)pick=&c;
    frame.menu_core_cursor={pick->line,pick->begin,pick->len,0,0,0};
    frame.menu_core_cursors.assign(1,frame.menu_core_cursor);frame.menu_choices.clear();
    /*
     * **今の列の札だけを行にする**。誕生の画面は性別・種族・クラス・エレメントの 4 列が同じ画面に横に並び、
     * 選び終えた列も残る。本文のまま窓に渡すと、4 列が 1 つの表に混ざり、カーソルの行が別の列の札に見える
     * （実画面で確かめた）。札は `a)`〜（桁がコアのカーソルと同じもの）。文字を押すとその項目に決まる
     * （`get_player_choice()` は英字で選び終える）ので、札のキーは字そのもの。上下の移動（8／2）は窓のカーソルから。
     * 札の幅は次の札の手前か 20 桁まで（クラスの右には能力値の欄が同じ行へ続く）。
     */
    std::vector<MenuChoice> choices;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto &line=frame.menu_term_lines[i];
        if(line.source_row<10)continue;
        const std::vector<ChoiceTokenGroup> groups=scan_choice_tokens(line.text_utf8);
        for(size_t k=0;k<groups.size();++k) {
            const ChoiceTokenGroup &g=groups[k];
            if(g.keys.size()!=1||g.keys[0]<'A'||(g.keys[0]>'Z'&&g.keys[0]<'a')||g.keys[0]>'z')continue;
            if(int(utf8_to_sjis(line.text_utf8.substr(0,g.begin)).size())!=pick->column)continue;
            std::size_t end=(k+1<groups.size())?groups[k+1].begin:line.text_utf8.size();
            //! 桁数（CP932 の 1 バイト＝1 桁）が 20 を超えない切れ目まで。
            const std::string rest=line.text_utf8.substr(g.begin,end-g.begin);
            std::size_t cut=0;
            while(cut<rest.size()) {
                std::size_t next=cut+1;
                while(next<rest.size()&&(static_cast<unsigned char>(rest[next])&0xC0)==0x80)++next;
                if(int(utf8_to_sjis(rest.substr(0,next)).size())>20)break;
                cut=next;
            }
            while(cut>0&&rest[cut-1]==' ')--cut;
            MenuChoice choice{};
            choice.line_index=int(i);choice.span_begin=int(g.begin);choice.span_len=int(cut);
            choice.key_begin=int(g.key_spans[0].first);choice.key_len=int(g.key_spans[0].second);
            choice.key=g.keys[0];choice.selected=(int(i)==pick->line);
            choices.push_back(std::move(choice));
        }
    }
    if(choices.size()>=2)frame.menu_choices=std::move(choices);
    return true;
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

/*!
 * @brief コアのカーソル（`》`）で選ぶ名指しのメニューを、画面側の選択肢（行）にする。
 * @details 画面側の接続（`cursor_mode` 真）では、`use_menu` が真なので、字（`a)`）で選ぶ画面はコアのカーソルで選ぶ型になる。
 * ところがコアのカーソルがある画面では、名指しの表（`kNamedMenus`）は引かれず（`tb_frame.cpp`）、
 * 窓は本文をそのまま並べる「カーソルの段」になる。呪文や持ち物のような表の型でない画面（ガンナーの技術メニュー）は、
 * 行が 1 行に詰まって見える（実画面で確かめた）。そこで、この関数が**行ごとの選択肢**を作る。
 *
 * 札は `key == 13`（Enter）で、現在の行の札が `selected`（窓のカーソル）になる。これは呪文の一覧と同じ約束
 * （画面側が上下の移動 `2`/`8` でカーソルを目的の行へ動かしてから Enter を送る）。
 *
 * 対象は今のところ**ガンナーの技術メニュー**（`object2.c:9557`。行 2〜12 に ` 》 アイテムからの弾丸の製造` と
 * `    鉱石からの弾丸の製造` …、行 0 が `どの種類の技術を使いますか？`／`調べますか？`）だけ。
 * @return 選択肢を作ったら true
 */
bool fill_cursor_menu_choices(GameFrame &frame)
{
    if (frame.menu_core_cursors.empty() && (frame.menu_core_cursor.line_index < 0)) {
        return false;
    }
    //! 署名は問いの**頭を含めない**（行 0 のミラーでは `どの` の `の` が空白に化ける。実機で確かめた）。
    static const char kSignature[] = "種類の技術を";
    const std::string &signature = utf8_of(kSignature, sizeof(kSignature) - 1);
    bool found = false;
    for (const auto &line : frame.menu_term_lines) {
        if ((line.source_row == 0) && (line.text_utf8.find(signature) != std::string::npos)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    repair_gunner_question(frame);
    const std::string &mark = cursor_mark();
    std::vector<MenuChoice> choices;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const auto &line = frame.menu_term_lines[i];
        if ((line.source_row < 2) || (line.source_row > 12)) {
            continue;
        }
        const std::string &text = line.text_utf8;
        std::size_t begin = text.find_first_not_of(' ');
        if (begin == std::string::npos) {
            continue;
        }
        bool selected = false;
        if (text.compare(begin, mark.size(), mark) == 0) {
            selected = true;
            begin = text.find_first_not_of(' ', begin + mark.size());
            if (begin == std::string::npos) {
                continue;
            }
        }
        std::size_t end = text.size();
        while ((end > begin) && (text[end - 1] == ' ')) {
            --end;
        }
        /*
         * 札の位置は**選択中かどうかで変えない**。窓の行の名前（id）が位置を含むので、`》`（3 バイト）の分だけ
         * ずれると、カーソルが載った行の名前が変わり、画面側が目的の行を見失う（2 桁の空白の行と同じ位置へ揃える）。
         */
        const std::size_t stable = selected ? (begin - (mark.size() - 2)) : begin;
        MenuChoice choice{};
        choice.line_index = static_cast<int>(i);
        choice.span_begin = static_cast<int>(stable);
        choice.span_len = static_cast<int>(end - stable);
        choice.key_begin = static_cast<int>(stable);
        choice.key_len = 0;
        choice.key = 13;
        choice.selected = selected;
        choice.label_utf8 = text.substr(begin, end - begin);
        choices.push_back(std::move(choice));
    }
    if (choices.size() < 2) {
        return false;
    }
    //! 窓の右の欄に、原作の問い（行 0。化けた字は上で直してある）を出す（無いと `》` の 1 文字だけが出る）。
    std::string question;
    for (const auto &line : frame.menu_term_lines) {
        if (line.source_row == 0) {
            question = line.text_utf8;
            while (!question.empty() && (question.back() == ' ')) {
                question.pop_back();
            }
            break;
        }
    }
    for (MenuChoice &choice : choices) {
        choice.description_utf8 = question;
    }
    frame.menu_choices = std::move(choices);
    return true;
}

/*!
 * @brief 記号を聞く `/` の問い（`cmd3.c:1866`）か。共用の判定（`is_monster_symbol_prompt()`）は「モンスター」と「文字」の両方の語を
 * 要るが、この問いの文言は「知りたい文字を入力して下さい」でモンスターの語が無く、判定に当たらなかった（問いが窓に出なかった）。
 */
static bool is_character_query_prompt(const std::string &row0)
{
    return row0.find(reinterpret_cast<const char *>(u8"知りたい文字を入力して下さい")) != std::string::npos
        || row0.find("Enter character to be identified") != std::string::npos;
}

void fix_search_input_window(GameFrame &frame)
{
    ModernWindow &w = frame.modern;
    if (w.stage != "text" || frame.menu_term_lines.empty() || frame.menu_term_lines.front().source_row != 0) {
        return;
    }
    const std::string &row0 = frame.menu_term_lines.front().text_utf8;
    const std::string ja = reinterpret_cast<const char *>(u8"検索(^I:持ち物 ^L:破壊された物):");
    const std::string en = "Search key(^I:inven ^L:destroyed):";
    const std::string &prompt = (row0.compare(0, ja.size(), ja) == 0) ? ja : en;
    if (row0.compare(0, prompt.size(), prompt) != 0) {
        return;
    }
    // 共用の窓は問いを最初の `:` で切るので、`(^I:` の中で切れてしまう。問いの全体を札にし、打った字だけを欄に入れる。
    std::string value = row0.substr(prompt.size());
    while (!value.empty() && value.front() == ' ') {
        value.erase(0, 1);
    }
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    w.input_label = prompt.substr(0, prompt.size() - 1);
    w.input = value;
    w.title = (&prompt == &ja) ? std::string(reinterpret_cast<const char *>(u8"自動拾いの検索")) : std::string("Auto-pick search");
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
        // `fill_core_cursor()` はこの関数より先に呼ばれる。tb_frame.cpp）。ここで `*` を選択肢に足すと、
        // 窓が「選択肢の段」になってカーソルの一覧として扱われず、`*` は一覧の表示を切り替えるだけなので
        // パッドやマウスでは先へ進めなかった（呪文を覚える G。遊ぶ人に届いた報告 2026-10-06）。
        // 変愚と同じく、カーソルの段のまま決定で選ばせる。
        if (frame.menu_choices.empty() && frame.menu_core_cursors.empty()) {
            frame.menu_open = true;
            MenuChoice choice{};
            choice.line_index = -1;
            choice.key = '*';
            choice.label_utf8 = (tb_lang_enabled() != 0) ? "Show choices" : reinterpret_cast<const char *>(u8"一覧を見る");
            choice.description_utf8 = row0;
            frame.menu_choices.push_back(std::move(choice));
        }
        return;
    }
    if (presentation::is_monster_symbol_prompt(row0) || is_character_query_prompt(row0)) {
        presentation::expose_ascii_symbol_prompt(frame, row0);
        /*
         * 問いの文言に出ている `^A全,^Uユ,^N非ユ,^M名前`（`cmd3.c:1866`・`bldg.c:3938`）。共用の記号の一覧は印字できる ASCII だけなので、
         * 制御文字の 4 つを選択肢に足す（足さないと、全モンスターの一覧・名前での検索に画面から届かない）。
         */
        if (row0.find("^A") != std::string::npos && row0.find("^M") != std::string::npos) {
            const bool en = (tb_lang_enabled() != 0);
            const struct { int key; const char *ja; const char *en; } controls[] = {
                { 1, reinterpret_cast<const char *>(u8"全モンスターの一覧 (^A)"), "All monsters (^A)" },
                { 21, reinterpret_cast<const char *>(u8"ユニークの一覧 (^U)"), "Unique monsters (^U)" },
                { 14, reinterpret_cast<const char *>(u8"ユニーク以外の一覧 (^N)"), "Non-unique monsters (^N)" },
                { 13, reinterpret_cast<const char *>(u8"名前で探す (^M)"), "Search by name (^M)" } };
            for (const auto &control : controls) {
                MenuChoice choice{};
                choice.line_index = -1;
                choice.key = control.key;
                choice.label_utf8 = en ? control.en : control.ja;
                choice.description_utf8 = row0;
                frame.menu_choices.push_back(std::move(choice));
            }
        }
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
    int cx=0,cy=0;if(tb_term_cursor(&cx,&cy,nullptr)==TB_OK)frame.menu_term_curs_col=cx;
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
    /* 仲間の字のメニュー（`party.c`）。見出しの文言が専用なので、ほかの画面とは取り違えない。 */
    if (fill_party_menu(frame)) {
        return true;
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

    // Last-resort semantic list support for live Toband class-power screens
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
        std::fprintf(stderr, "[toband] menu selftest FAIL: %s\n", what);
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
            if (!ok) std::fprintf(stderr, "[toband] menu selftest look case %s band=\"%s\"\n", lc.name, look.text_utf8.c_str());
            check(ok, "Genso Look band keeps the name of what is looked at");
            if (ok) ++look_ok;
        }
        std::fprintf(stderr, "[toband] menu selftest: look band name %d/3 ok\n", look_ok);
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
        //! F-toband-09：賭け試合の建物の最初の画面は、主の名が出ていても一覧ではなく建物として扱う。
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

        //! 誕生の見出し。明るい青（`TERM_L_BLUE` ＝ 14）の `a)` の枠がコアのカーソルになる（最初の誕生・仲間の誕生の両方）。
        for (const bool pregame : { true, false }) {
            for (const char *heading : { "Please select your character from the menu below:", reinterpret_cast<const char *>(u8"以下のメニューからキャラクタの要素を選んでください。") }) {
                GameFrame birth;
                birth.pre_game_menu = pregame;
                add_line(birth, 1, heading);
                add_line(birth, 10, "a)EASY");
                birth.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 0, 6, 14 });
                check(fill_birth_selection_cursor(birth) && birth.menu_core_cursor.line_index == 1,
                    pregame ? "birth selection cursor (new game)" : "birth selection cursor (party member)");
            }
        }
        {
            // 選び終えた列は明るい青のまま残る。コアのカーソルの位置（行 11・桁 14）の札を採る。
            GameFrame birth;
            add_line(birth, 1, reinterpret_cast<const char *>(u8"以下のメニューからキャラクタの要素を選んでください。"));
            add_line(birth, 10, std::string("  a) ") + reinterpret_cast<const char *>(u8"女性") + "     a) " + reinterpret_cast<const char *>(u8"人間"));
            birth.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 2, 9, 14 });
            birth.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 16, 9, 1 });
            add_line(birth, 11, std::string("  b) ") + reinterpret_cast<const char *>(u8"男性") + "     b) " + reinterpret_cast<const char *>(u8"ホークマン"));
            birth.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 2, 9, 1 });
            birth.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 16, 18, 14 });
            birth.menu_term_curs_row = 2;
            birth.menu_term_curs_col = 14;
            check(fill_birth_selection_cursor(birth) && birth.menu_core_cursor.line_index == 2
                    && birth.menu_core_cursor.span_begin == 16,
                "birth selection cursor follows the core cursor column, not the finished column");
            // 今の列（桁 14）の札だけが行になる（選び終えた列の `女性`・`男性` は入らない）。現在の行が selected。
            check(birth.menu_choices.size() == 2 && birth.menu_choices[0].key == 'a' && birth.menu_choices[1].key == 'b'
                    && !birth.menu_choices[0].selected && birth.menu_choices[1].selected,
                "birth selection offers only the active column as choices");
        }
        GameFrame birth_other;
        birth_other.pre_game_menu = true;
        add_line(birth_other, 3, "Enter your name");
        add_line(birth_other, 5, "a)EASY");
        birth_other.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 0, 6, 14 });
        check(!fill_birth_selection_cursor(birth_other), "unrelated english text is not a birth heading");

        //! 英語の名指しの画面（形は日本語の同名の項目と同じ）。
        for (const NamedMenuSpec &spec : kNamedMenus) {
            const std::string tag(spec.tag);
            if (tag.size() < 3 || tag.compare(tag.size() - 3, 3, "_en") != 0 || tag == "stat_raise_en" || tag == "melee_arena_en") continue;
            if (spec.styles != kStylePlain) continue; //!< 丸括弧の札の画面（`visuals_en`）は下の専用の確かめで見る
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
    /*
     * 原作の字の問いとメニュー。画面の字は原作のソースの文言そのまま。
     * 実機の検査は `tools/toband/tb_menu_check.py`。ここは単体の確かめ（コアが無くても流れる）。
     */
    {
        PromptBar prompt;
        const auto prompt_keys = [&](const PromptBar &p) {
            std::string keys;
            for (const PromptChoice &c : p.choices) keys += static_cast<char>(c.key);
            return keys;
        };
        // クイックスタート（`birth.c:1937`）とアストラルモード（`:1916`）。行末は `[y/n, 'Q'終了]`。`Q`（ゲームの終了）は出さない。
        check(parse_prompt_line(reinterpret_cast<const char *>(u8"  クイック・スタートを使いますか？[y/n, 'Q'終了]"), prompt)
                && prompt_keys(prompt) == "yn",
            "quick start prompt offers y and n only");
        check(parse_prompt_line(reinterpret_cast<const char *>(u8"  突入しますか？[y/n, 'Q'終了]"), prompt)
                && prompt_keys(prompt) == "yn",
            "astral mode prompt offers y and n only");
        check(parse_prompt_line("  Use quick start? [y/n, 'Q' Quit]", prompt) && prompt_keys(prompt) == "yn",
            "quick start prompt (english text) offers y and n only");
        // `m` の問い（`dungeon.c:3962`）。ガンナー・エレメンタラーが先頭で仲間が 2 人以上のとき。
        check(parse_prompt_line(reinterpret_cast<const char *>(u8"a) 職業の特殊能力  b) 魔法書 : "), prompt)
                && prompt_keys(prompt) == "ab",
            "class power or spell book prompt offers a and b");
        // 似た綴りは拾わない（`[y/n, 'Q'` で始まらない・`]` で終わらない行）。
        check(!parse_prompt_line(reinterpret_cast<const char *>(u8"クイック・スタートの説明です [y/n, 'Q'終了] の意味"), prompt),
            "quick start text in the middle of a sentence is not a prompt");
        // ガンナーの製造・強化の問い（`get_com()`。行 0 の `[B]通常弾, …`）と、カジノの「もう一度」・`/` の「思い出を見ますか」。
        const auto u8 = [](const char8_t *text) { return std::string(reinterpret_cast<const char *>(text)); };
        check(parse_prompt_line(u8(u8"[B]通常弾, [R]ライフル弾, [S]散弾 :"), prompt) && prompt_keys(prompt) == "BRS",
            "gunner bullet kind prompt offers B, R and S");
        check(parse_prompt_line(u8(u8"[M]ミスリル, [A]アダマンタイト :"), prompt) && prompt_keys(prompt) == "MA",
            "gunner ore kind prompt offers M and A");
        check(parse_prompt_line(u8(u8"[R]ロケット弾, [A]反重力ロケット弾 :"), prompt) && prompt_keys(prompt) == "RA",
            "gunner rocket kind prompt offers R and A");
        check(parse_prompt_line(u8(u8"[E]修正強化, [S]高速射撃化 :"), prompt) && prompt_keys(prompt) == "ES",
            "gunner enchant prompt offers E and S");
        check(parse_prompt_line(u8(u8"[E]修正強化, [M]強力射撃化, [S]高速射撃化 :"), prompt) && prompt_keys(prompt) == "EMS",
            "gunner enchant prompt (with might) offers E, M and S");
        check(parse_prompt_line("Create [B]ullet, [R]ound or [S]hell ?", prompt) && prompt_keys(prompt) == "BRS",
            "gunner bullet kind prompt (english text) offers B, R and S");
        check(parse_prompt_line("[E]nchant, make [M]ight or add extra [S]hot ?", prompt) && prompt_keys(prompt) == "EMS",
            "gunner enchant prompt (english text) offers E, M and S");
        check(parse_prompt_line(u8(u8"                  もう一度(Y/N)？"), prompt) && prompt_keys(prompt) == "YN",
            "casino again prompt offers Y and N");
        check(parse_prompt_line(u8(u8"                                    思い出を見ますか? (k:殺害順/y/n): "), prompt) && prompt_keys(prompt) == "kyn",
            "monster symbol recall prompt offers k, y and n");
        {
            // テンプルナイトの必殺技の選択（`get_temple_blow()`）は、誕生の選択と同じ作りなのでカーソルと札が立つ。
            GameFrame blow;
            add_line(blow, 1, u8(u8"  以下のメニューから覚えたい必殺技を選んでください。"));
            add_line(blow, 10, "  a) First blow");
            blow.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 2, 13, 14 });
            add_line(blow, 11, "  b) Second blow");
            blow.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 2, 14, 1 });
            blow.menu_term_curs_row = 1; // ミラーの添字（行 10 の札）
            blow.menu_term_curs_col = 2;
            check(fill_birth_selection_cursor(blow) && blow.menu_core_cursor.line_index == 1,
                "temple knight special blow list has a core cursor");
            check(blow.menu_choices.size() == 2 && blow.menu_choices[0].key == 'a' && blow.menu_choices[0].selected
                    && blow.menu_choices[1].key == 'b' && !blow.menu_choices[1].selected,
                "temple knight special blow list offers a letter choice per blow");
        }
        {
            GameFrame symbol;
            symbol.menu_open = true;
            std::string row0 = u8(u8"知りたい文字を入力して下さい(記号 or ^A全,^Uユ,^N非ユ,^M名前): ");
            check(is_character_query_prompt(row0), "symbol query prompt (/) recognized");
            check(!is_character_query_prompt(u8(u8"どのアイテムを調べますか？")), "an item prompt is not the symbol query prompt");
        }
    }
    {
        const auto utf8 = [](const char8_t *s) { return std::string(reinterpret_cast<const char *>(s)); };
        // エレメントの選択（`xtra2.c:7369`）。行 1 が問い・行 2〜5 に `a) 火`〜`d) 風`（桁 22）。
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, 1, std::string(22, ' ') + utf8(u8"エレメントを選んでください。"));
        add_line(frame, 2, std::string(22, ' ') + utf8(u8"a) 火"));
        add_line(frame, 3, std::string(22, ' ') + utf8(u8"b) 水"));
        add_line(frame, 4, std::string(22, ' ') + utf8(u8"c) 地"));
        add_line(frame, 5, std::string(22, ' ') + utf8(u8"d) 風"));
        frame.menu_term_curs_row = 0; //!< 先頭の行（行 1。問い）にカーソルがある
        check(collect_named_menu_choices(frame) > 0 && has_key(frame, 'a') && has_key(frame, 'b') && has_key(frame, 'c') && has_key(frame, 'd'),
            "choose element offers a-d");
        check(frame.menu_choices.size() == 4, "choose element offers exactly four choices");
        // 問いに降りている間（カーソルが問いの行に無い）は作らない。
        frame.menu_choices.clear();
        frame.menu_term_curs_row = 2;
        check(collect_named_menu_choices(frame) < 0 && frame.menu_choices.empty(), "choose element is not offered away from the question row");
    }
    {
        const auto utf8 = [](const char8_t *s) { return std::string(reinterpret_cast<const char *>(s)); };
        // ガンナーの技術メニュー（`object2.c:9557`。`use_menu` が偽のとき）。行 0 が問い・行 2〜12 に `a)`〜`k)`（桁 16）。
        for (const char8_t *question : { u8"どの能力を使いますか:", u8"どの能力を調べますか:", u8"ど  能力を使いますか:" /* 実機のミラーでは頭の「の」が空白に化ける */ }) {
            GameFrame frame;
            frame.menu_open = true;
            add_line(frame, 0, utf8(question));
            const std::array<std::string, 11> rows = { utf8(u8"a) アイテムからの弾丸の製造"), utf8(u8"b) 鉱石からの弾丸の製造"), utf8(u8"c) エッセンス一覧"),
                utf8(u8"d) エッセンス抽出"), utf8(u8"e) エッセンス消去"), utf8(u8"f) エッセンス付加"), utf8(u8"g) 弾丸の強化"),
                utf8(u8"h) 弾丸を限界まで強化"), utf8(u8"i) ロケット弾の製造"), utf8(u8"j) ロケット弾へのエッセンス付加"), utf8(u8"k) 銃器の強化") };
            for (int i = 0; i < 11; ++i) add_line(frame, 2 + i, std::string(16, ' ') + rows[static_cast<std::size_t>(i)]);
            frame.menu_term_curs_row = 0;
            check(collect_named_menu_choices(frame) > 0 && frame.menu_choices.size() == 11, "gunner technique menu offers a-k");
            bool all = true;
            for (int k = 'a'; k <= 'k'; ++k) all = all && has_key(frame, k);
            check(all, "gunner technique menu keys a-k");
        }
    }
    {
        const auto utf8 = [](const char8_t *s) { return std::string(reinterpret_cast<const char *>(s)); };
        // 自宅のクラスチェンジ（`store.c:4384`）。行 1 が見出し・行 10〜に `a) クラス名`（桁 2）。右の桁 22 に必要能力値が同じ行へ重なる。
        // 暗い灰（スレート 2）は選べない・青（6）は選べないクラスを選択中・白（1）は選べる・明るい青（14）は選べるクラスを選択中。
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, 1, utf8(u8"  以下のメニューから新しいクラスを選んでください。"));
        const auto class_row = [&](int row, const std::string &text, uint8_t color) {
            add_line(frame, row, text);
            frame.menu_term_lines.back().color_spans.push_back(TermColorSpan{ 2, 3, color });
        };
        // 名前の幅が 17 桁（全角 8 字 + `a) `）で右の欄との空白が 3 つ無い行（札の幅を 20 桁で切る確認）
        class_row(10, utf8(u8"  a) ナイト           腕力 :    15"), 14);
        class_row(11, utf8(u8"  b) バーサーカー     知能 :"), 2);
        class_row(12, utf8(u8"  c) ビーストテイマー 器用 :    16"), 1);
        frame.menu_term_curs_row = 1; //!< 行 10（先頭のクラス）
        check(collect_named_menu_choices(frame) > 0 && frame.menu_choices.size() == 3, "class change offers one choice per class");
        const auto label_of = [&](int key) {
            for (const MenuChoice &c : frame.menu_choices) {
                if (c.key == key) {
                    return frame.menu_term_lines[static_cast<std::size_t>(c.line_index)].text_utf8.substr(
                        static_cast<std::size_t>(c.span_begin), static_cast<std::size_t>(c.span_len));
                }
            }
            return std::string("(none)");
        };
        check(label_of('a') == utf8(u8"a) ナイト"), "class change label stops before the stat column");
        check(label_of('c') == utf8(u8"c) ビーストテイマー"), "class change label of a full-width name is not cut short");
        const auto choice_of = [&](int key) -> const MenuChoice * {
            for (const MenuChoice &c : frame.menu_choices) if (c.key == key) return &c;
            return nullptr;
        };
        check(choice_of('a') && choice_of('a')->available && choice_of('a')->selected, "class change marks the selected row");
        check(choice_of('b') && !choice_of('b')->available && !choice_of('b')->selected, "class change marks an unavailable class");
        check(choice_of('c') && choice_of('c')->available && !choice_of('c')->selected, "class change keeps an available class");
        // 決定のあとの `よろしいですか？ [y/n]`（カーソルが行 0）では一覧を作らない。
        frame.menu_choices.clear();
        frame.menu_term_curs_row = 0;
        check(collect_named_menu_choices(frame) < 0 && frame.menu_choices.empty(), "class change is not offered while the confirmation is asked");
    }
    {
        const auto utf8 = [](const char8_t *s) { return std::string(reinterpret_cast<const char *>(s)); };
        // 画面表示の設定（`cmd4.c:3537`）。括弧なしの見出しが行 2・`(1)`〜`(0)` が行 4〜13・待ちが行 18（日本語）／15（英語）。
        for (const bool english : { false, true }) {
            GameFrame frame;
            frame.menu_open = true;
            add_line(frame, 2, english ? "Interact with Visuals" : utf8(u8"画面表示の設定"));
            add_line(frame, 4, std::string(5, ' ') + (english ? "(1) Load a user pref file" : utf8(u8"(1) ユーザー設定ファイルのロード")));
            add_line(frame, 5, std::string(5, ' ') + (english ? "(2) Dump monster attr/chars" : utf8(u8"(2) モンスターの 色/文字 をファイルに書き出す")));
            add_line(frame, 13, std::string(5, ' ') + (english ? "(0) Reset visuals" : utf8(u8"(0) 画面表示方法の初期化")));
            add_line(frame, english ? 15 : 18, english ? "Command: " : utf8(u8"コマンド:"));
            frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size()) - 1;
            check(collect_named_menu_choices(frame) > 0 && has_key(frame, '1') && has_key(frame, '2') && has_key(frame, '0'),
                english ? "visuals menu (english) offers its items" : "visuals menu offers its items");
            if (!english) {
                // (1) を決定すると、ファイル名の問い `ファイル: ` が一覧の待ちと同じ行 18 に書かれる。降りたあとは一覧を出さない。
                frame.menu_choices.clear();
                frame.menu_term_lines.back().text_utf8 = utf8(u8"ファイル: tbmbase.prf");
                check(collect_named_menu_choices(frame) < 0 && frame.menu_choices.empty(),
                    "visuals menu is not offered while the file name is typed (same row as the command prompt)");
            }
        }
        // オプションの設定（`cmd4.c:2245`）。待ちは TOband では行 19。
        GameFrame options;
        options.menu_open = true;
        add_line(options, 2, utf8(u8"[ オプションの設定 ]"));
        add_line(options, 4, std::string(5, ' ') + utf8(u8"(1)     キー入力          オプション"));
        add_line(options, 10, std::string(5, ' ') + utf8(u8"(R)    プレイ記録         オプション"));
        add_line(options, 19, utf8(u8"コマンド:"));
        options.menu_term_curs_row = static_cast<int>(options.menu_term_lines.size()) - 1;
        check(collect_named_menu_choices(options) > 0 && has_key(options, '1') && has_key(options, 'R'), "options menu offers its items");
    }
    {
        const auto utf8 = [](const char8_t *s) { return std::string(reinterpret_cast<const char *>(s)); };
        // ガンナーの技術メニュー（`use_menu` が真＝コアのカーソル）。実機のミラーの形（頭の「の」が空白に化ける）で確かめる。
        const std::array<std::string, 4> items = { utf8(u8"アイテムからの弾丸の製造"), utf8(u8"鉱石からの弾丸の製造"), utf8(u8"エッセンス一覧"), utf8(u8"銃器の強化") };
        GameFrame frame;
        frame.menu_open = true;
        add_line(frame, 0, utf8(u8"ど  種類の技術を使いますか？"));
        for (int i = 0; i < 4; ++i) {
            add_line(frame, 2 + i, std::string(14, ' ') + (i == 1 ? utf8(u8" 》 ") : std::string("    ")) + items[static_cast<std::size_t>(i)]);
        }
        check(fill_core_cursor(frame), "gunner cursor menu has a core cursor");
        check(fill_cursor_menu_choices(frame) && frame.menu_choices.size() == 4, "gunner cursor menu offers one choice per item");
        bool ok = frame.menu_choices.size() == 4;
        for (std::size_t i = 0; ok && (i < 4); ++i) {
            const MenuChoice &c = frame.menu_choices[i];
            ok = (c.key == 13) && (c.label_utf8 == items[i]) && (c.selected == (i == 1));
        }
        check(ok, "gunner cursor menu keys, labels and the selected row");
        check(frame.menu_term_lines.front().text_utf8 == utf8(u8"どの種類の技術を使いますか？"), "gunner question has its lost character restored");
        // ほかの画面（問いが違う）では作らない。
        GameFrame other;
        other.menu_open = true;
        add_line(other, 0, utf8(u8"どの呪文を唱えますか?"));
        add_line(other, 2, std::string(14, ' ') + utf8(u8" 》 火炎の矢"));
        add_line(other, 3, std::string(14, ' ') + "    " + utf8(u8"氷の矢"));
        check(fill_core_cursor(other) && !fill_cursor_menu_choices(other) && other.menu_choices.empty(), "cursor menu choices are only for the gunner technique menu");
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

} // namespace tb
