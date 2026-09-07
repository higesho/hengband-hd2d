/*!
 * @file fc_lang.cpp
 * @brief `fc_lang.h` の実装（設計 §5）と、C の皮のうち**引く 4 つ**。
 *
 * ## この TU の立ち位置
 * `fc_lang_c.h` が約束しているうち、**カタログを持つ 4 つ**
 * （`fc_lang_enabled` / `fc_tr` / `fc_tr_fmt` / `fc_tr_arg`）をここで実装する。
 * 素のバイト算は `fc_lang_c.c`、名前の組み立ては `fc_name_ja.c` にある。
 *
 * ## 寿命
 * `fc_tr()` が返す `const char *` は**カタログの中を指す**。カタログは
 * `lang_init()` から終了まで生きたままで、要素を消したり並べ替えたりしない
 * （`std::unordered_map` のノードは再ハッシュしても動かない）。
 * よって呼び手（`frox/src`）は解放しないし、持ち回してよい。
 *
 * ## スレッド
 * 引くのは**ゲームのスレッドだけ**である（Term を触るのがそこだけなので）。
 * 受信スレッドはここへ来ない。だからロックは要らない。`lang_write_report()` は
 * ゲームのスレッドが終わってから呼ぶ。
 *
 * ## 文字コード
 * 原本（`frox/lang/ja/ui/messages.ja.txt`）は **UTF-8**、表に入るのは **CP932**。
 * 変換は `fc_text.h` の 1 か所だけを通る（設計 §3.1）。
 *
 * ## Frox に固有のこと — **タグ**
 * 鍵にはドキュメントのタグ（`<color:y>…</color>`）が**そのまま入る**（設計 §3.4-1）。
 * 剥がして引くと訳文でタグの位置を復元できないからで、代わりに読み込みのときに
 * 「英日でタグの数と種類が一致するか」を見て、食い違う訳は**入れない**。
 */

#include "fc_lang.h"

#include "fc_lang_c.h"
#include "fc_text.h"

#include "portable/legacy_os.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

/* ====================================================================== 表の実体 */

//! 日本語層が起きているか（`lang == "ja"`）。**カタログの成否とは別**（設計 §5 の註）。
bool g_enabled = false;

//! 鍵（CP932 の原文）→ 訳（CP932）。
std::unordered_map<std::string, std::string> g_catalog;

/*!
 * @brief 枠の名札（`F:`。フック #27）。**一般のカタログとは別の表**である。
 *
 * @details 名札は `Light` `Chaos` `Good` のような**裸の 1 語**で、一般のカタログでは
 * 別の意味を持つ（`Light` = 光源・`Chaos` = カオス領域）。同じ表へ入れると
 * 人物調書の耐性の欄に「光源」と出る（実測 2026-08-27）ので、表を分ける。
 */
std::unordered_map<std::string, std::string> g_frame;

/*!
 * @brief 飾りの札（`D:`。フック #31 #32 #33）。**これも別の表**である。
 *
 * @details 品に直に足される字（偽の銘・感触・箱と罠の札・由来の場所）。
 * `g_frame` と分ける理由は同じ——鍵が裸の 1 語（`cursed` `good` `empty`）で、
 * 一般のカタログでは別の意味を持つ。**桁の制限は無い**（欄に流し込むのではなく
 * 名前の尻に継ぎ足されるだけなので）。
 */
std::unordered_map<std::string, std::string> g_decor;

/*!
 * @brief 店と建物の名（`S:`。フック #34 #35）。**これも別の表**である。
 *
 * @details 鍵は `Home` `Temple` `Museum` のような**裸の 1 語**で、一般のカタログへ
 * 入れるとキーの名前まで巻き添えにする（`cmd4.c` のマクロの引き金の表に
 * `"Home"` がある）。**桁の制限は無い**——店の見出しは `doc_width() - ct` で
 * 右へ寄せるだけで、欄に流し込むわけではない。
 */
std::unordered_map<std::string, std::string> g_shop;

/*!
 * @brief 店主の名前（`K:`。フック #36）。**これも別の表**である。
 *
 * @details 店の画面の左上に出る名前（`shop->owner->name`）。鍵に
 * `Grug` `Angel` `Vile` `Regen` `Martin` `Conan` のような**裸の 1 語**が
 * 30 人ぶんあり、一般の表へ入れると画面の別の字を巻き添えにする
 * （`Angel` は敵の記号の凡例にも出る）。`S:` とも分けたのは、
 * **どちらも全数照合を掛けている**からで、混ぜると照合が緩む。
 */
std::unordered_map<std::string, std::string> g_keeper;

/*!
 * @brief **引数の裸の 1 語**（`A:`。フック #3 の中）。**5 枚目の別表**である。
 *
 * @details `fc_tr_arg()` は裸の 1 語を**わざと引かない**（設計 §4 追記 (d)）。
 * `format("%s.raw", name)` のようなパスの組み立てまで訳してしまうからである。
 * だが `msg_format("You feel less %s.", "strong")` のように、
 * **語そのものが画面へ出る引数**も同じ形で来る。
 *
 * **この表に載せた語だけは、裸でも引く。** 巻き添えの範囲は表の中に閉じるので、
 * `F:` `D:` `S:` `K:` を分けたのとまったく同じ理屈である。
 *
 * @warning **載せる語は 1 つずつツリーで確かめること。** ほかの `%s` に同じ字が
 * 渡っていれば巻き添えになる（道具は `tools/frox/fc_arg_words.py`）。
 * 危ない語は載せず、その鍵だけ #39 の手（解いた 1 文を鍵にする）で処理する。
 */
std::unordered_map<std::string, std::string> g_argword;

/*!
 * @brief **呪文・属性・召喚の名**（`M:`。フック #46）。**6 枚目の別表**である。
 *
 * @details 敵の呪文の表示名（`Magic Missile`）・系統名（`Breathe` `Ball`）・
 * 属性の名（`gf.c` の `_gf_tbl[]`。`Fire` `Nether`）・召喚の種別名（`Undead`）。
 * どれも表の中の素の `char *` で、`sprintf` で組み立ててから `msg_print()` へ
 * 渡るので、`fc_tr()` にも `fc_tr_fmt()` にも届かない。
 *
 * **`A:`（引数の裸の 1 語）へは入れられない。** `Breathe` `Teleport` `Identify`
 * `Berserk` は**職の力の名前**でもあり（`var_set_string(res, "Breathe")`）、
 * 力の一覧は `%-23.23s` のような桁の決まった欄に並ぶ。引数の表へ入れると
 * 確かめていない画の桁が動く。**`F:`（枠の名札）とも別**で、あちらの
 * `Acid` は耐性の欄なので「耐酸」、こちらは属性そのものなので「酸」である。
 */
std::unordered_map<std::string, std::string> g_spell;

/*!
 * @brief **状態列の札**（`xtra1.c` の `bar[]`。設計 §8.65 の #77）。原本では **`B:`**。
 *
 * @details 画面の下の状態列は、**短い札と長い札の対**でできている
 * （`IAc` / `ImmAcid`）。桁は `strlen()` で数えてから中央へ寄せるので、
 * **訳の長さで数え直さないと欄が崩れる**。
 *
 * **一般のカタログとは別の表である。** 短い札は `Bl` のような裸の 2 字で、
 * 同じ字が 5 つの別物（`Blind` `Blood Blade` `Blending` `Block` `Blink`）に
 * 使われている。だから**短い札は長い札と対にして引く**——鍵は
 * `"<長い札>\\t<短い札>"` である。長い札は `"<長い札>"` そのものが鍵。
 */
std::unordered_map<std::string, std::string> g_bar;

//! `J:` が空だった鍵＝**わざと訳さない**印。未訳一覧から外す。
std::set<std::string> g_silent;

//! 引けなかった鍵（`--report-untranslated=`）。重複はまとめる。
std::set<std::string> g_missing;
std::string g_report_path;

/* ============================================================== 原本を読む道具 */

/*!
 * @brief エスケープ（改行・タブ・空白）を解く。
 * @details `\s` は**空白 1 個**。行頭・行末の空白は編集器に落とされやすいので、
 * 抜き出し器（`tools/frox/fc_extract_ja_keys.py`）はそこだけ `\s` で書く。
 * 知らない綴りは**背中の印ごとそのまま**残す——鍵は 1 バイトでも違えば別物なので、
 * 勝手に落とすほうが害が大きい。
 */
std::string unescape(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if ((in[i] != '\\') || ((i + 1) >= in.size())) {
            out.push_back(in[i]);
            continue;
        }
        const char c = in[i + 1];
        switch (c) {
        case 'n': out.push_back('\n'); ++i; break;
        case 't': out.push_back('\t'); ++i; break;
        case 'r': out.push_back('\r'); ++i; break;
        case 's': out.push_back(' '); ++i; break;
        case '\\': out.push_back('\\'); ++i; break;
        default: out.push_back('\\'); break;
        }
    }
    return out;
}

/*!
 * @brief 書式の**引数の並び**を 1 本の文字列にする。
 *
 * @details `vstrnfmt()`（`frox/src/z-form.c:235`）の読み方に合わせてある:
 * - 二重のパーセントは引数を食わない
 * - `%n` は `size_t*` を 1 つ食う
 * - `*` は `int` を 1 つ**余分に**食う
 * - `^`（先頭大文字）と桁指定は引数に効かない
 * - `l` は型を変える
 * - **`vstrnfmt()` が受けない字は変換指定ではない**（`z-form.c` の `default:` は
 *   行ごと消してしまうので、そんな書式は元から在り得ない）。`20% bonus` の
 *   `%` は**素のパーセント**で、`%|` を 1 つ数えるだけにする（設計 §8.56）
 *
 * 例: `"%^s hits %s for %d damage."` → `"s|s|d|"`、`"%*d"` → `"*d|"`、
 * `"a 20% bonus"` → `"%|"`。
 *
 * これを英日で突き合わせ、**食い違う訳はカタログに入れない**（設計 §5）。
 * **素のパーセントも数える**ので、訳文が `%` を足したり落としたりすれば弾かれる。
 */
std::string format_signature(const std::string &fmt)
{
    std::string sig;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            continue;
        }
        ++i;
        if (i >= fmt.size()) {
            sig += "%|"; // 末尾の裸の `%` も素のパーセント（`85% to 115%` の 2 つ目）
            break;
        }
        if (fmt[i] == '%') {
            continue;
        }
        if (fmt[i] == 'n') {
            sig += "n|";
            continue;
        }

        std::string one;
        bool closed = false;
        const std::size_t start = i;
        for (; i < fmt.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(fmt[i]);
            if (c == '*') {
                one += '*';
                continue;
            }
            if (c == '^') {
                continue; // 引数には効かない
            }
            if (std::strchr("-+#.0123456789", static_cast<char>(c)) != nullptr) {
                continue; // 桁・精度・符号。引数には効かない（`*` を除く）
            }
            if (c == 'l') {
                one += 'l';
                continue; // 型を変えるだけ。次の字を待つ
            }
            if (std::strchr("diouxXfeEgGcspvV", static_cast<char>(c)) != nullptr) {
                one += static_cast<char>(c);
                closed = true;
            }
            break; // 変換の字か、変換ではない字。どちらでもここで終わる
        }
        if (closed) {
            sig += one;
        } else {
            /*
             * **素のパーセント**（`20% bonus` の `%`）。`vstrnfmt()` が受けない字が
             * 来たので変換指定ではない。数だけ数え、読む位置は `%` の次へ戻す
             * ——`%` のあとの字はふつうの本文だからである。
             */
            sig += "%";
            i = start;
        }
        sig += '|';
    }
    return sig;
}

//! 先方が解するタグ名（`doc_parse_tag` の実読）。ここに無い `<...>` はただの字。
bool is_doc_tag_name(const std::string &name)
{
    static const char *const kNames[] = {
        "color", "/color", "style", "/style", "topic", "link",
        "$", "var", "indent", "/indent", "tab", nullptr,
    };
    for (int i = 0; kNames[i] != nullptr; ++i) {
        if (name == kNames[i]) {
            return true;
        }
    }
    return false;
}

/*!
 * @brief ドキュメントのタグを**並べ替えて**取り出す（設計 §3.4-1）。
 *
 * @details 取るのは開き山括弧から閉じ山括弧までで、**名前が先方の語彙に在るもの
 * だけ**を数える（`doc_parse_tag()`（`frox/src/z-doc.c:850`）の実読。2026-08-25）:
 * `color` `/color` `style` `/style` `topic` `link` `$` `var` `indent` `/indent` `tab`。
 *
 * **語彙で絞らないと `<dir>` のような素の字までタグに見える**——`Move to <dir>, …`
 * の訳が「`<dir>` を残さないと弾かれる」ことになり、訳文が縛られる（J4 で踏んだ）。
 * 先方も語彙に無い名前は撥ねて素で描くので、数えないのが正しい。
 * ASCII アートの上り階段は閉じまで届かないか改行を挟むので拾われない。
 *
 * **並べ替えてから比べる。** 日本語は語順が変わるので、色づけの前後が入れ替わって
 * よい。数と種類さえ合っていればタグは壊れない。
 */
std::vector<std::string> tag_signature(const std::string &s)
{
    std::vector<std::string> tags;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '<') {
            continue;
        }
        std::size_t j = i + 1;
        bool ok = false;
        for (; j < s.size(); ++j) {
            const char c = s[j];
            if ((c == '\r') || (c == '\n') || (c == '\t') || (c == '<')) {
                break;
            }
            if (c == '>') {
                ok = true;
                break;
            }
        }
        if (!ok) {
            continue;
        }
        /* 名前は `<` の次から `:` か `>` まで（先方の切り出しと同じ）。 */
        std::size_t k = i + 1;
        while ((k < j) && (s[k] != ':')) {
            ++k;
        }
        if (!is_doc_tag_name(s.substr(i + 1, k - i - 1))) {
            continue; // 語彙に無い。ただの字なので数えない
        }
        tags.push_back(s.substr(i, j - i + 1));
        i = j;
    }
    std::sort(tags.begin(), tags.end());
    return tags;
}

/*!
 * @brief **品の種別の型**の印を数える（`O:` の照合。設計 §4 の #25）。
 *
 * @details 型は `& Potion~ of %` の形で、`%`（種別の名前）と `#`（風味）が
 * **写しの環のフック**である。訳文で数が変われば名前が欠けるか二重になるので、
 * 英日で数を突き合わせる。`&`（冠詞）と `~`（複数形）は**日本語では落とす**ので数えない。
 *
 * **`%` も `#` も CP932 の 2 バイト目には現れない**（2 バイト目は 0x40〜0x7E と
 * 0x80〜0xFC。`%` は 0x25・`#` は 0x23）ので、バイトのまま数えてよい
 * （設計 §4 追記 (g) の数え上げと同じ）。
 */
std::string marker_signature(const std::string &s)
{
    int kind = 0;
    int flavor = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') {
            ++kind;
        } else if (s[i] == '#') {
            ++flavor;
        }
    }
    return std::to_string(kind) + "|" + std::to_string(flavor);
}

/*! @brief 1 行から `X:` の札を剥がす。札が違えば偽。 */
bool take_tag(const std::string &line, const char *tag, std::string &body)
{
    const std::size_t len = std::strlen(tag);
    if ((line.size() < len) || (line.compare(0, len, tag) != 0)) {
        return false;
    }
    body = line.substr(len);
    return true;
}

} // namespace

namespace fc {

LangLoadReport lang_init(const std::string &lang, const std::string &lang_dir,
    const std::string &report_path)
{
    if (lang != "ja") {
        /* 英語。**1 バイトも読まない**（設計 §1 制約 1）。 */
        return lang_load_text(lang, std::string(), report_path);
    }

    const std::string path = lang_dir + portable::kPathSep + "ui" + portable::kPathSep
        + "messages.ja.txt";
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        /*
         * **これは致命ではない**。名前は `lib-ja/edit` から来るので、
         * カタログが無くても日本語は出る（設計 §5 の註）。
         * **`enabled` は立てる**——ここで下げると `fc_is_text_byte()` が
         * 0x80 以上を潰し、`lib-ja/edit` の日本語まで空白になる。
         */
        LangLoadReport rep = lang_load_text(lang, std::string(), report_path);
        rep.error = "cannot open " + path;
        return rep;
    }

    std::string text;
    {
        char chunk[8192];
        std::size_t got = 0;
        while ((got = std::fread(chunk, 1, sizeof(chunk), fp)) > 0) {
            text.append(chunk, got);
        }
    }
    std::fclose(fp);

    LangLoadReport rep = lang_load_text(lang, text, report_path);
    if (!rep.error.empty()) {
        rep.error = path + ": " + rep.error;
    }
    return rep;
}

LangLoadReport lang_load_text(const std::string &lang, const std::string &raw,
    const std::string &report_path)
{
    LangLoadReport rep;

    g_enabled = (lang == "ja");
    g_catalog.clear();
    g_frame.clear();
    g_decor.clear();
    g_shop.clear();
    g_keeper.clear();
    g_argword.clear();
    g_spell.clear();
    g_bar.clear();
    g_silent.clear();
    g_missing.clear();
    g_report_path = report_path;
    rep.enabled = g_enabled;
    if (!g_enabled) {
        return rep;
    }

    std::string text = raw;

    /* UTF-8 の BOM は落とす（編集器が付けることがある）。 */
    if ((text.size() >= 3) && (static_cast<unsigned char>(text[0]) == 0xEF)
        && (static_cast<unsigned char>(text[1]) == 0xBB) && (static_cast<unsigned char>(text[2]) == 0xBF)) {
        text.erase(0, 3);
    }

    std::string key_utf8;
    std::string val_utf8;
    bool have_key = false;
    bool have_val = false;
    bool key_is_basenm = false; //!< 鍵が `O:`（品の種別の型。設計 §4 の #25）で書かれた
    bool key_is_frame = false;  //!< 鍵が `F:`（枠の名札。設計 §4 の #27）で書かれた
    bool key_is_decor = false;  //!< 鍵が `D:`（飾りの札。設計 §4 の #31）で書かれた
    bool key_is_argword = false; //!< 鍵が `A:`（引数の裸の 1 語。設計 §8.35）で書かれた
    bool key_is_shop = false;   //!< 鍵が `S:`（店と建物の名。設計 §4 の #34）で書かれた
    bool key_is_keeper = false; //!< 鍵が `K:`（店主の名前。設計 §4 の #36）で書かれた
    bool key_is_spell = false;  //!< 鍵が `M:`（呪文・属性・召喚の名。設計 §4 の #46）で書かれた
    bool key_is_bar = false;    //!< 鍵が `B:`（状態列の札。設計 §8.65 の #77）で書かれた
    int line_no = 0;
    int first_dup_line = 0;
    std::string first_dup_key;

    const auto flush = [&]() {
        if (!have_key) {
            return;
        }
        const std::string key = utf8_to_sjis(unescape(key_utf8));
        if (key.empty()) {
            ++rep.not_in_cp932;
            return;
        }
        if (!have_val || val_utf8.empty()) {
            g_silent.insert(key);
            ++rep.silent;
            return;
        }
        const std::string value = utf8_to_sjis(unescape(val_utf8));
        if (value.empty()) {
            ++rep.not_in_cp932;
            return;
        }
        /*
         * **`O:` の鍵は引数の並びで照合しない。** 型の `%` は変換指定ではなく
         * 「ここへ種別の名前が入る」印で、訳文では CP932 の 2 バイト目に
         * 英字が来て（`シ` = 83 **56**）並びが食い違って見える。
         * 代わりに `%` と `#` の**数**を突き合わせる（設計 §4 の #25）。
         */
        if (key_is_basenm && (marker_signature(key) != marker_signature(value))) {
            ++rep.rejected;
            if (rep.rejected <= 10) {
                std::fprintf(stderr, "[frox:lang] marker mismatch, not loaded: \"%s\" -> \"%s\"\n",
                    key.c_str(), value.c_str());
            }
            return;
        }
        if (!key_is_basenm && format_signature(key) != format_signature(value)) {
            ++rep.rejected;
            if (rep.rejected <= 10) {
                std::fprintf(stderr, "[frox:lang] argument mismatch, not loaded: \"%s\" -> \"%s\"\n",
                    key.c_str(), value.c_str());
            }
            return;
        }
        if (tag_signature(key) != tag_signature(value)) {
            ++rep.tag_mismatch;
            if (rep.tag_mismatch <= 10) {
                std::fprintf(stderr, "[frox:lang] doc tag mismatch, not loaded: \"%s\" -> \"%s\"\n",
                    key.c_str(), value.c_str());
            }
            return;
        }
        if (key_is_decor) {
            if (g_decor.find(key) == g_decor.end()) {
                g_decor.emplace(key, value);
                ++rep.entries;
            }
            return;
        }
        if (key_is_shop) {
            if (g_shop.find(key) == g_shop.end()) {
                g_shop.emplace(key, value);
                ++rep.entries;
            }
            return;
        }
        if (key_is_keeper) {
            if (g_keeper.find(key) == g_keeper.end()) {
                g_keeper.emplace(key, value);
                ++rep.entries;
            }
            return;
        }
        if (key_is_argword) {
            if (g_argword.find(key) == g_argword.end()) {
                g_argword.emplace(key, value);
                ++rep.entries;
            }
            return;
        }
        if (key_is_spell) {
            if (g_spell.find(key) == g_spell.end()) {
                g_spell.emplace(key, value);
                ++rep.entries;
            }
            return;
        }
        if (key_is_bar) {
            if (g_bar.find(key) == g_bar.end()) {
                g_bar.emplace(key, value);
                ++rep.entries;
            }
            return;
        }
        if (key_is_frame) {
            /*
             * **枠の名札は桁が決まっている**（`py_info.c` の `%-11.11s`）。
             * CP932 は 1 バイト＝1 桁なので、11 バイトを超えると欄が崩れ、
             * 境で切れると 2 バイト文字が割れる。**入れずに英語のまま残す。**
             */
            if (value.size() > 11) {
                ++rep.rejected;
                if (rep.rejected <= 10) {
                    std::fprintf(stderr, "[frox:lang] frame label too wide, not loaded: \"%s\" -> \"%s\" (%d bytes)\n",
                        key.c_str(), value.c_str(), (int)value.size());
                }
                return;
            }
            if (g_frame.find(key) == g_frame.end()) {
                g_frame.emplace(key, value);
                ++rep.entries;
            }
            return;
        }
        const auto seen = g_catalog.find(key);
        if (seen != g_catalog.end()) {
            /* 設計 §5「`E:` が重複したら読み込みで失敗させる」。黙って後勝ちにしない。 */
            if (first_dup_line == 0) {
                first_dup_line = line_no;
                first_dup_key = key;
            }
            return;
        }
        g_catalog.emplace(key, value);
        ++rep.entries;
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            nl = text.size();
        }
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++line_no;
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }

        if (line.empty() || (line[0] == '#')) {
            continue; // 註記と空行。**行末の空白は落とさない**（鍵の一部でありうる）
        }

        std::string body;
        const bool is_e = take_tag(line, "E:", body);
        const bool is_o = is_e ? false : take_tag(line, "O:", body);
        const bool is_f = (is_e || is_o) ? false : take_tag(line, "F:", body);
        const bool is_d = (is_e || is_o || is_f) ? false : take_tag(line, "D:", body);
        const bool is_s = (is_e || is_o || is_f || is_d) ? false : take_tag(line, "S:", body);
        const bool is_k = (is_e || is_o || is_f || is_d || is_s) ? false : take_tag(line, "K:", body);
        const bool is_a = (is_e || is_o || is_f || is_d || is_s || is_k) ? false : take_tag(line, "A:", body);
        const bool is_m = (is_e || is_o || is_f || is_d || is_s || is_k || is_a) ? false : take_tag(line, "M:", body);
        const bool is_b = (is_e || is_o || is_f || is_d || is_s || is_k || is_a || is_m) ? false : take_tag(line, "B:", body);
        if (is_e || is_o || is_f || is_d || is_s || is_k || is_a || is_m || is_b) {
            if (have_val) {
                flush();
                key_utf8.clear();
                val_utf8.clear();
                have_key = false;
                have_val = false;
                key_is_basenm = false;
                key_is_frame = false;
                key_is_decor = false;
                key_is_shop = false;
                key_is_keeper = false;
                key_is_argword = false;
                key_is_spell = false;
                key_is_bar = false;
            }
            key_utf8 += body; // `E:` を続けると連結（長い書式を折れるように）
            have_key = true;
            if (is_o) {
                key_is_basenm = true; // 品の種別の型（設計 §4 の #25）
            }
            if (is_f) {
                key_is_frame = true;  // 枠の名札（設計 §4 の #27）
            }
            if (is_d) {
                key_is_decor = true;  // 飾りの札（設計 §4 の #31）
            }
            if (is_s) {
                key_is_shop = true;   // 店と建物の名（設計 §4 の #34）
            }
            if (is_k) {
                key_is_keeper = true; // 店主の名前（設計 §4 の #36）
            }
            if (is_a) {
                key_is_argword = true; // 引数の裸の 1 語（設計 §8.35）
            }
            if (is_m) {
                key_is_spell = true;  // 呪文・属性・召喚の名（設計 §4 の #46）
            }
            if (is_b) {
                key_is_bar = true;    // 状態列の札（設計 §8.65 の #77）
            }
            continue;
        }
        if (take_tag(line, "J:", body)) {
            val_utf8 += body;
            have_val = true;
            continue;
        }
        if (take_tag(line, "W:", body)) {
            continue; // 桁の控え。見るのは `tools/frox/fc_check_ja.py`
        }
        std::fprintf(stderr, "[frox:lang] line %d: unknown line, ignored: %s\n",
            line_no, line.c_str());
    }
    flush();

    if (first_dup_line != 0) {
        rep.error = "duplicate key at line " + std::to_string(first_dup_line)
            + " -> \"" + first_dup_key + "\"";
    }
    return rep;
}

int lang_write_report()
{
    if (g_report_path.empty() || g_missing.empty()) {
        return 0;
    }

    std::FILE *fp = std::fopen(g_report_path.c_str(), "wb");
    if (fp == nullptr) {
        std::fprintf(stderr, "[frox:lang] cannot write the untranslated report: %s\n",
            g_report_path.c_str());
        return 0;
    }

    /*
     * **そのまま `messages.ja.txt` へ貼れる形で書く**（`E:` と空の `J:`）。
     * 原本は UTF-8 なので、書き出しも UTF-8 に戻す。
     */
    std::fprintf(fp, "# untranslated keys collected by FroxCore --report-untranslated\n");
    std::fprintf(fp, "# paste into frox/lang/ja/ui/messages.ja.txt and fill in J:\n\n");
    int count = 0;
    for (const std::string &key : g_missing) {
        std::string utf8 = sjis_to_utf8(key);
        std::string escaped;
        for (const char c : utf8) {
            switch (c) {
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            case '\\': escaped += "\\\\"; break;
            default: escaped.push_back(c); break;
            }
        }
        /*
         * **行頭と行末の空白は 1 つ残らず印にする**（編集器に落とされないように）。
         * 1 個だけ守っても、その内側の空白が落ちれば鍵は別物になる。
         */
        std::size_t head = 0;
        while ((head < escaped.size()) && (escaped[head] == ' ')) {
            ++head;
        }
        std::size_t tail = escaped.size();
        while ((tail > head) && (escaped[tail - 1] == ' ')) {
            --tail;
        }
        std::string safe;
        safe.reserve(escaped.size() + head + (escaped.size() - tail));
        for (std::size_t i = 0; i < head; ++i) {
            safe += "\\s";
        }
        safe.append(escaped, head, tail - head);
        for (std::size_t i = tail; i < escaped.size(); ++i) {
            safe += "\\s";
        }
        escaped.swap(safe);
        std::fprintf(fp, "E:%s\nJ:\n\n", escaped.c_str());
        ++count;
    }
    std::fclose(fp);
    std::fprintf(stderr, "[frox:lang] %d untranslated key(s) written to %s\n",
        count, g_report_path.c_str());
    return count;
}

/* ===================================================================== 検査（§11 の 5） */

namespace {

int g_fail = 0;

void expect(bool ok, const char *what)
{
    if (!ok) {
        ++g_fail;
        std::fprintf(stderr, "[frox:lang] selftest FAIL: %s\n", what);
    }
}

//! CP932 の 2 バイト（「あ」= 0x82 0xA0、「。」= 0x81 0x42、「（」= 0x81 0x69）。
const char *const kA = "\x82\xA0";
const char *const kMaru = "\x81\x42";
const char *const kOpen = "\x81\x69";

} // namespace

int lang_selftest()
{
    g_fail = 0;

    /* ---- 1: カタログの判断 ------------------------------------------------ */
    {
        /*
         * 使い捨てのカタログ（UTF-8）。**CP932 に無い字を 1 件だけ混ぜてある**
         * ——ハングル（U+D55C）は CP932 に無いので、そこだけ弾かれるのが正解である。
         */
        const std::string text =
            "# comment\n"
            "E:Hello\n"
            "J:\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\n"   // こんにちは
            "\n"
            "E:You have %d gold.\n"
            "J:\xE9\x87\x91%d\n"                                                            // 金%d
            "\n"
            "E:Bad %s and %d\n"
            "J:\xE6\x82\xAA%d\n"                                                            // 悪%d（引数が合わない）
            "\n"
            "E:<color:r>Danger</color>\n"
            "J:<color:r>\xE5\x8D\xB1\xE9\x99\xBA</color>\n"                              // 危険
            "\n"
            "E:<color:r>Lost tag</color>\n"
            "J:\xE7\x84\xA1\xE3\x81\x97\n"                                               // 無し（タグが落ちている）
            "\n"
            "E:On purpose\n"
            "J:\n"
            "\n"
            "E:Dunadan\n"
            "J:\xE3\x83\x89\xE3\x82\xA5\xE3\x83\x8A\xE3\x83\x80\xE3\x83\xB3\n"      // ドゥナダン
            "\n"
            "E:hits you\n"
            "J:\xE6\xAE\xB4\xE3\x81\xA3\xE3\x81\x9F\n"                                   // 殴った
            "\n"
            "E:Not in cp932\n"
            "J:\xEA\x95\x9C\n"                                                          // 한
            "\n"
            /* フック #38 #39: 継ぎ足しのメッセージを書式へ組み直して引く。 */
            "E:You have %s (%c).\n"
            "J:\x25\x73\x20\x28\x25\x63\x29\x20\xE3\x82\x92\xE6\x8C\x81\xE3\x81\xA3\xE3\x81\xA6\xE3\x81\x84\xE3\x82\x8B\xE3\x80\x82\n"
            "\n"
            "E:Your %s slays animals.\n"
            "J:\x25\x73\xE3\x81\x8C\xE5\x8B\x95\xE7\x89\xA9\xE3\x82\x92\xE5\x88\x87\xE3\x82\x8A\xE8\xA3\x82\xE3\x81\x84\xE3\x81\x9F\xE3\x80\x82\n"
            "\n"
            /* `A:` の札（引数の裸の 1 語。設計 §8.35） */
            "A:strong\n"
            "J:\xE5\xBC\xB7\xE3\x81\x8F\n";

        const LangLoadReport rep = lang_load_text("ja", text, std::string());
        expect(rep.enabled, "the layer wakes up for ja");
        expect(rep.entries == 8, "8 entries loaded");
        expect(rep.silent == 1, "an empty J: means \"do not translate\"");
        expect(rep.rejected == 1, "a format with different args is rejected");
        expect(rep.tag_mismatch == 1, "a translation that drops a doc tag is rejected");
        expect(rep.not_in_cp932 == 1, "a translation with a char cp932 lacks is rejected");
        expect(rep.error.empty(), "no load error");

        expect(std::strcmp(fc_tr("Hello"), "Hello") != 0, "a known key is translated");
        expect(std::strcmp(fc_tr("Unknown"), "Unknown") == 0, "an unknown key falls back");
        expect(std::strcmp(fc_tr("On purpose"), "On purpose") == 0, "a silent key falls back");
        expect(std::strcmp(fc_tr_fmt("Bad %s and %d"), "Bad %s and %d") == 0,
            "the rejected format falls back");

        /* 二重引きよけ: 訳し終わった字（0x80 以上を含む）は引きに行かない。 */
        {
            std::string mixed = "a) ";
            mixed += kA;
            expect(fc_tr(mixed.c_str()) == mixed.c_str(), "an already-translated string is not looked up");
        }

        /* 裸の 1 語は doc の旗の中だけ（設計 §4 追記 (d)。pref 名を守る）。 */
        {
            expect(std::strcmp(fc_tr_arg("Dunadan"), "Dunadan") == 0,
                "a bare word is NOT translated as an arg outside doc scope");
            expect(std::strcmp(fc_tr_arg("hits you"), "hits you") != 0,
                "a phrase arg is translated outside doc scope");
            fc_lang_doc_scope(1);
            expect(std::strcmp(fc_tr_arg("Dunadan"), "Dunadan") != 0,
                "a bare word IS translated as an arg inside doc scope");
            fc_lang_doc_scope(0);
            expect(std::strcmp(fc_tr("Dunadan"), "Dunadan") != 0,
                "a bare word is translated at a display sink (fc_tr)");

            /*
             * `A:` の札。**この表に載せた語だけは、裸でも引数として引く**
             * （設計 §8.35）。画面の字を引く口（`fc_tr`）には出さない
             * ——出すと `F:` を分けた意味が無くなる。
             */
            expect(std::strcmp(fc_tr_arg("strong"), "strong") != 0,
                "an A: word IS translated as a bare arg outside doc scope");
            expect(std::strcmp(fc_tr("strong"), "strong") == 0,
                "an A: word is NOT translated at a display sink");
            expect(std::strcmp(fc_tr_arg("clumsy"), "clumsy") == 0,
                "a bare word the A: table lacks still falls back");
        }

        /*
         * フック #38 #39。どちらも先方が字を継ぎ足してから出すので、
         * **継ぐ前の形**を鍵にする（設計 §8.32）。**引けない鍵では 0 を返して
         * 英語の道へ落ちる**ことも見る（制約 1）。
         */
        {
            char buf[256];

            expect(fc_obj_carry_msg(buf, sizeof(buf), "sword", 0, 0, 'c') == 1,
                "#38 builds the carry line from a format");
            expect(std::strstr(buf, "sword") != nullptr, "#38 keeps the item name");
            expect(std::strstr(buf, "(c)") != nullptr, "#38 keeps the slot letter");
            expect(fc_obj_carry_msg(buf, sizeof(buf), "sword", 0, 0, 0) == 0,
                "#38 falls back when the catalogue lacks that shape");
            expect(fc_obj_carry_msg(buf, sizeof(buf), "sword", 1, 0, 'c') == 0,
                "#38 keeps wearing and carrying apart");

            expect(fc_slay_msg(buf, sizeof(buf), "sword", 1, "animals") == 1,
                "#39 keys on the resolved sentence");
            expect(std::strstr(buf, "sword") != nullptr, "#39 keeps the weapon name");
            expect(fc_slay_msg(buf, sizeof(buf), "sword", 0, "animals") == 0,
                "#39 keeps slays and is-covered-in apart");
            expect(fc_slay_msg(buf, sizeof(buf), "sword", 1, "dragons") == 0,
                "#39 falls back for a sentence the catalogue lacks");
        }
    }

    /* ---- 2: バイト算（日本語のとき） -------------------------------------- */
    {
        expect(fc_lang_enabled() == 1, "fc_lang_enabled while ja");
        expect(fc_is_text_byte(0x41) == 1, "ascii passes");
        expect(fc_is_text_byte(0x82) == 1, "0x80+ passes while ja");
        expect(fc_is_text_byte(0x7F) == 0, "0x7f never passes");
        expect(fc_is_text_byte(0x1F) == 0, "control never passes");

        std::string two = kA;
        two += kA;                                    // 「ああ」= 4 バイト
        expect(fc_clip(two.c_str(), 3) == 2, "clip does not split a 2-byte char");
        expect(fc_clip(two.c_str(), 4) == 4, "clip keeps a whole char");
        expect(fc_clip("abc", 2) == 2, "clip leaves ascii alone");
        expect(fc_last_char_bytes(two.c_str(), 4) == 2, "the last char is 2 bytes");
        expect(fc_last_char_bytes("ab", 2) == 1, "the last ascii char is 1 byte");

        /* 「先行バイトに見える 1 バイト文字」を 2 バイトと誤らない。 */
        expect(fc_is_sjis_lead(0x82) == 1, "0x82 is a lead byte");
        expect(fc_is_sjis_lead(0x41) == 0, "A is not a lead byte");
        expect(fc_is_sjis_trail(0x7F) == 0, "0x7f is not a trail byte");
    }

    /* ---- 3: ドキュメントの単語切り（フック #4） ----------------------- */
    {
        expect(fc_doc_word_bytes("abc") == 0, "ascii keeps the english rule");
        expect(fc_doc_word_bytes(kA) == 2, "one kanji is one word");

        std::string hang = kA;
        hang += kMaru;                                // 「あ。」
        expect(fc_doc_word_bytes(hang.c_str()) == 4, "a closing mark hangs onto the word");

        std::string open = kOpen;
        open += kA;                                   // 「（あ」
        expect(fc_doc_word_bytes(open.c_str()) == 4, "an opening mark keeps the next char");

        /* 半端な先行バイト（尻尾が無い）は 1 バイト文字として英語の道へ。 */
        expect(fc_doc_word_bytes("\x82") == 0, "a broken lead byte falls back to ascii");
    }

    /* ---- 3.5: 品の種別の型（`O:`。フック #25） ------------------------ */
    {
        /*
         * **`O:` の鍵は引数の並びで照合しない。** `& Potion~ of %` の `%` を
         * 変換指定と読むと、訳文 `%のポーション` の CP932 の 2 バイト目
         * （`シ` = 83 **56**）が英字に見えて食い違い、正しい訳が弾かれる。
         * ここはその 1 件が**入る**ことと、印の数が違う訳が**弾かれる**ことを見る。
         */
        const std::string text =
            "O:& Potion~ of %\n"
            "J:\x25\xE3\x81\xAE\xE3\x83\x9D\xE3\x83\xBC\xE3\x82\xB7\xE3\x83\xA7\xE3\x83\xB3\n" // %のポーション
            "\n"
            "O:& # Potion~\n"
            "J:\x23\xE3\x81\xAE\xE8\x96\xAC\n"                                    // #の薬
            "\n"
            "O:& Scroll~ of %\n"
            "J:\xE5\xB7\xBB\xE7\x89\xA9\n";                                        // 巻物（`%` が落ちている）

        const LangLoadReport rep = lang_load_text("ja", text, std::string());
        expect(rep.entries == 2, "2 type templates loaded");
        expect(rep.rejected == 1, "a template that drops % is rejected");
        expect(std::strcmp(fc_tr_basenm("& Potion~ of %"), "& Potion~ of %") != 0,
            "a known type template is translated");
        expect(std::strcmp(fc_tr_basenm("& Scroll~ of %"), "& Scroll~ of %") == 0,
            "the rejected template falls back");
        expect(std::strcmp(fc_tr_basenm("& Dagger~"), "& Dagger~") == 0,
            "an unknown type template falls back");

        /* 日本語の `k_info` 名もここへ来る。**引きに行かない**（二重引きよけ）。 */
        {
            std::string ja_name = kA;
            ja_name += kA;
            expect(fc_tr_basenm(ja_name.c_str()) == ja_name.c_str(),
                "a name that is already japanese is not looked up");
        }
    }

    /* ---- 3.55: 飾りの札（`D:`。フック #31 #32 #33） ------------------- */
    {
        /*
         * **裸の 1 語が一般のカタログへ漏れないこと**が眼目である
         * （`good` は `F:` では「良い」・`D:` では「上質」で、`E:` には無い）。
         */
        const std::string text =
            "D:good\n"
            "J:\xE4\xB8\x8A\xE8\xB3\xAA\n"                                 // 上質
            "\n"
            "D: (charging)\n"
            "J:(\xE5\x85\x85\xE5\xA1\xAB\xE4\xB8\xAD)\n"                // (充填中)
            "\n"
            "D:%s, level %d\n"
            "J:%s\xE3\x81\xAE%d\xE9\x9A\x8E\n"                             // %sの%d階
            "\n"
            "D:on level %d of %s\n"
            "J:%s\xE3\x81\xAE%d\xE9\x9A\x8E\n";                            // 並びが逆。弾く

        const LangLoadReport rep = lang_load_text("ja", text, std::string());
        expect(rep.entries == 3, "3 decorations loaded");
        expect(rep.rejected == 1, "a decoration whose arguments swap is rejected");
        expect(std::strcmp(fc_decor("good"), "good") != 0, "a bare decoration is translated");
        expect(std::strcmp(fc_decor(" (charging)"), " (charging)") != 0,
            "a decoration keeps its leading space in the key");
        expect(std::strcmp(fc_decor("on level %d of %s"), "on level %d of %s") == 0,
            "the rejected decoration falls back");
        expect(std::strcmp(fc_decor("unseen"), "unseen") == 0, "an unknown decoration falls back");

        /* **一般のカタログには漏れない**（設計 §4 追記 (m)）。 */
        expect(std::strcmp(fc_tr("good"), "good") == 0, "a decoration is not in the general catalogue");
        expect(std::strcmp(fc_frame_label("good"), "good") == 0, "nor in the frame table");

        /* 訳し終わった日本語は引きに行かない（二重引きよけ）。 */
        {
            std::string ja = kA;
            ja += kA;
            expect(fc_decor(ja.c_str()) == ja.c_str(), "a japanese decoration is not looked up");
        }
    }

    /* ---- 3.56: 店と建物の名（`S:`。フック #34 #35） ------------------- */
    {
        /*
         * **`Home` が一般のカタログとキーの名前へ漏れないこと**が眼目である
         * ——`cmd4.c` のマクロの引き金の表に `{"\033[H", "Home"}` がある。
         */
        const std::string text =
            "S:Home\n"
            "J:\xE6\x88\x91\xE3\x81\x8C\xE5\xAE\xB6\n"           // 我が家
            "\n"
            "S:General Store\n"
            "J:\xE9\x9B\x91\xE8\xB2\xA8\xE5\xB1\x8B\n";          // 雑貨屋

        const LangLoadReport rep = lang_load_text("ja", text, std::string());
        expect(rep.entries == 2, "2 shop names loaded");
        expect(std::strcmp(fc_shop("Home"), "Home") != 0, "a bare shop name is translated");
        expect(std::strcmp(fc_shop("General Store"), "General Store") != 0,
            "a two-word shop name is translated");
        expect(std::strcmp(fc_shop("Satchel"), "Satchel") == 0, "an unknown shop name falls back");

        /* **ほかの表には漏れない**（設計 §4 追記 (n)）。 */
        expect(std::strcmp(fc_tr("Home"), "Home") == 0, "a shop name is not in the general catalogue");
        expect(std::strcmp(fc_decor("Home"), "Home") == 0, "nor in the decoration table");
        expect(std::strcmp(fc_frame_label("Home"), "Home") == 0, "nor in the frame table");

        /* 訳し終わった日本語は引きに行かない（二重引きよけ）。 */
        {
            std::string ja = kA;
            ja += kA;
            expect(fc_shop(ja.c_str()) == ja.c_str(), "a japanese shop name is not looked up");
        }
    }

    /* ---- 3.57: 店主の名前（`K:`。フック #36） ------------------------- */
    {
        /*
         * **裸の 1 語がほかの表へ漏れないこと**が眼目である——`Angel` は
         * 敵の記号の凡例にも出るので、一般のカタログへ入れてはいけない。
         */
        const std::string text =
            "K:Angel\n"
            "J:\xE3\x82\xA8\xE3\x83\xB3\xE3\x82\xB8\xE3\x82\xA7\xE3\x83\xAB\n"  // エンジェル
            "\n"
            "K:Bilbo the Friendly\n"
            "J:\xE3\x83\x93\xE3\x83\xAB\xE3\x83\x9C\n";                              // ビルボ

        const LangLoadReport rep = lang_load_text("ja", text, std::string());
        expect(rep.entries == 2, "2 shop owners loaded");
        expect(std::strcmp(fc_shop_owner("Angel"), "Angel") != 0, "a bare owner name is translated");
        expect(std::strcmp(fc_shop_owner("Bilbo the Friendly"), "Bilbo the Friendly") != 0,
            "an owner name with a title is translated");
        expect(std::strcmp(fc_shop_owner("Butch"), "Butch") == 0, "an unknown owner falls back");

        /* **ほかの表には漏れない**（設計 §4 追記 (o)）。 */
        expect(std::strcmp(fc_tr("Angel"), "Angel") == 0, "an owner is not in the general catalogue");
        expect(std::strcmp(fc_shop("Angel"), "Angel") == 0, "nor in the shop table");
        expect(std::strcmp(fc_decor("Angel"), "Angel") == 0, "nor in the decoration table");

        /* 訳し終わった日本語は引きに行かない（二重引きよけ）。 */
        {
            std::string ja = kA;
            ja += kA;
            expect(fc_shop_owner(ja.c_str()) == ja.c_str(), "a japanese owner is not looked up");
        }
    }

    /* ---- 3.58: 呪文・属性・召喚の名（`M:`。フック #46） --------------- */
    {
        /*
         * **`Breathe` が `A:` にも一般の表にも漏れないこと**が眼目である——
         * あれは職の力の名前でもあり、力の一覧は `%-23.23s` の欄に並ぶ
         * （設計 §8.39 ④）。表を分けたのはそのためである。
         */
        const std::string text =
            "M:Fire\n"
            "J:\xE7\x81\xAB\xE7\x82\x8E\n"                                      // 火炎
            "\n"
            "M:Breathe\n"
            "J:\xE3\x83\x96\xE3\x83\xAC\xE3\x82\xB9\n"                    // ブレス
            "\n"
            "M:Undead\n"
            "J:\xE3\x82\xA2\xE3\x83\xB3\xE3\x83\x87\xE3\x83\x83\xE3\x83\x89\n";  // アンデッド

        const LangLoadReport rep = lang_load_text("ja", text, std::string());
        expect(rep.entries == 3, "3 spell names loaded");
        expect(std::strcmp(fc_spell_name("Fire"), "Fire") != 0, "an attribute name is translated");
        expect(std::strcmp(fc_spell_name("Breathe"), "Breathe") != 0,
            "a spell group name is translated");
        expect(std::strcmp(fc_spell_name("Undead"), "Undead") != 0,
            "a summon type name is translated");
        expect(std::strcmp(fc_spell_name("Chaos"), "Chaos") == 0, "an unknown spell name falls back");

        /* **ほかの表には漏れない**（設計 §8.40 ②）。 */
        expect(std::strcmp(fc_tr("Breathe"), "Breathe") == 0,
            "a spell name is not in the general catalogue");
        expect(std::strcmp(fc_tr_arg("Breathe"), "Breathe") == 0, "nor in the argument table");
        expect(std::strcmp(fc_frame_label("Fire"), "Fire") == 0, "nor in the frame table");

        /* 訳し終わった日本語は引きに行かない（二重引きよけ）。 */
        {
            std::string ja = kA;
            ja += kA;
            expect(fc_spell_name(ja.c_str()) == ja.c_str(), "a japanese spell name is not looked up");
        }
    }

    /* ---- 3.59: 状態列の札（`B:`。フック #77） ------------------------- */
    {
        /*
         * **短い札は長い札と対で引くこと**が眼目である——`Bl` は `Blind`
         * `Blood Blade` `Blending` `Block` `Blink` の 5 つに使い回されている
         * （設計 §8.65 の #77）。**桁を数える所と画へ出す所が同じ字を返すこと**も見る。
         */
        const std::string text =
            "B:Blind\\tBl\n"
            "J:\xE7\x9B\xB2\n"
            "\n"
            "B:Blind\n"
            "J:\xE7\x9B\xB2\xE7\x9B\xAE\n"
            "\n"
            "B:Blink\\tBl\n"
            "J:\xE7\x9E\xAC\n"
            "\n"
            "E:Blind\n"
            "J:\xE7\x9B\xAE\xE3\x81\x8C\xE8\xA6\x8B\xE3\x81\x88\xE3\x81\xAA\xE3\x81\x84\n";

        const LangLoadReport rep = lang_load_text("ja", text, std::string());
        expect(rep.entries == 4, "3 status labels and 1 general entry loaded");

        /* 同じ `Bl` が、長い札しだいで別の訳になる。 */
        expect(std::strcmp(fc_bar_short("Blind", "Bl"), fc_bar_short("Blink", "Bl")) != 0,
            "the same short label means two different things");

        /* **長い札は専用の表が先**（一般のカタログの「目が見えない」ではない）。 */
        expect(std::strcmp(fc_bar("Blind"), fc_tr("Blind")) != 0,
            "the status label wins over the general catalogue");

        /* 専用の表に無ければ一般のカタログへ落ちる（桁を数える所と出す所を合わせる）。 */
        expect(std::strcmp(fc_bar("Blink"), fc_tr("Blink")) == 0,
            "an unknown long label falls through to the general catalogue");

        /* **短い札は一般のカタログへは落とさない。** */
        expect(std::strcmp(fc_bar_short("Blind", "Zz"), "Zz") == 0,
            "an unknown short label stays as it is");

        /* 訳し終わった日本語は引きに行かない（二重引きよけ）。 */
        {
            std::string ja = kA;
            ja += kA;
            expect(fc_bar(ja.c_str()) == ja.c_str(), "a japanese status label is not looked up");
        }
    }

    /* ---- 3.6: セーブの符号の印（追補 A3。フック #29 #30） -------------- */
    {
        /*
         * **日本語で立てているあいだは必ず CP932 の印を書く。** ここは日本語の節の
         * 途中なので `fc_lang_enabled()` は真である。
         */
        expect(fc_save_enc_mark() == 0x46430002UL, "a japanese session marks the save cp932");
    }

    /* ---- 4: 英語へ戻すと M0 と同じ答えになる ------------------------------ */
    {
        lang_load_text("en", std::string(), std::string());
        fc_save_enc_note(0UL); //!< 印なし＝上流・M0 のセーブ（A3 の節が汚さないように）
        expect(fc_lang_enabled() == 0, "the layer sleeps for en");
        expect(fc_is_text_byte(0x82) == 0, "0x80+ is dropped again while en");
        std::string two = kA;
        expect(fc_clip(two.c_str(), 1) == 1, "clip is a no-op while en");
        expect(fc_doc_word_bytes(kA) == 0, "doc word split is off while en");
        expect(std::strcmp(fc_tr("Hello"), "Hello") == 0, "nothing is translated while en");
        expect(std::strcmp(fc_decor("good"), "good") == 0, "no decoration is translated while en");
        expect(fc_save_enc_mark() == 0x46430001UL, "an english session marks the save ascii");
    }

    /* ---- 5: 英語で立てて、日本語のセーブを開いた（追補 A3 の本番） --------- */
    {
        /*
         * **ここが A3 の眼目である。** カタログは寝たまま（英語の字は 1 バイトも
         * 変わらない）だが、**バイト算だけが起きる**——記憶の中の生い立ち・銘・
         * 履歴は CP932 のバイトのままだからで、ASCII と思って扱うと
         * `Term_addstr` が 2 バイト文字を割り、doc は段落まるごとを 1 単語にする。
         */
        fc_save_enc_note(0x46430002UL);
        expect(fc_lang_enabled() == 0, "the catalogue stays asleep for a cp932 save in en");
        expect(std::strcmp(fc_tr("Hello"), "Hello") == 0, "nothing is translated (constraint 1)");
        expect(fc_cp932_text() == 1, "byte arithmetic wakes for a cp932 save");

        std::string two = kA;
        two += kA;
        expect(fc_clip(two.c_str(), 3) == 2, "clip does not split a 2-byte char from the save");
        expect(fc_doc_word_bytes(kA) == 2, "doc word split works for text from the save");
        expect(fc_is_text_byte(0x82) == 1, "0x80+ passes for text from the save");

        /* **印は下げない。**英語で保存し直しても CP932 のままである。 */
        expect(fc_save_enc_mark() == 0x46430002UL, "an english session does not clear the mark");

        /* ASCII の印なら英語のままの答えへ戻る。 */
        fc_save_enc_note(0x46430001UL);
        expect(fc_cp932_text() == 0, "an ascii save leaves the byte arithmetic asleep");
        expect(fc_save_enc_mark() == 0x46430001UL, "an ascii save stays ascii");
        fc_save_enc_note(0UL);
    }

    return g_fail;
}

} // namespace fc

/* ============================================== C の皮（`fc_lang_c.h` の 4 つ） */

extern "C" {

int fc_lang_enabled(void)
{
    return g_enabled ? 1 : 0;
}

/*!
 * @brief 引く本体。`fc_tr` / `fc_tr_fmt` / `fc_tr_arg` の共通部分。
 * @param s 原文
 * @param record 引けなかったときに未訳一覧へ積むか
 */
static const char *fc_lookup(const char *s, int record)
{
    if (!g_enabled || (s == nullptr) || (s[0] == '\0')) {
        return s;
    }
    /*
     * **二重引きを避ける**（設計 §4）。`vstrnfmt()` で訳した日本語が
     * `Term_addstr()` にも流れてくる。
     *
     * **先頭だけでは足りない。** 「英字 1 文字＋閉じ括弧＋空白＋訳語」の形は
     * 頭が ASCII で中身が訳し終わっている。
     * **鍵はすべて英語のリテラルなので、0x80 以上を 1 バイトでも含むなら訳済み**
     * と見てよい（Sil-Q で実測した罠）。
     */
    for (const char *p = s; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80) {
            return s;
        }
    }

    const std::string key(s);
    const auto found = g_catalog.find(key);
    if (found != g_catalog.end()) {
        return found->second.c_str();
    }
    if ((record != 0) && (g_silent.find(key) == g_silent.end())) {
        g_missing.insert(key);
    }
    return s;
}

/*!
 * @brief 鍵が「言葉」を含んでいるか（＝未訳一覧へ積んでよいか）。
 *
 * @details **変換指定の中の字は数えない**——内部の配管やダイスの表示で
 * 一覧が埋まると読めなくなる。素の数字・空白・罫線も同じ理由で外す。
 *
 * **引くこと自体は止めない。** 止めるのは「一覧へ積む」だけで、
 * 訳が要ると分かればカタログに書けばそのまま効く。
 */
static bool key_has_word(const char *s)
{
    if ((s == nullptr) || (s[0] == '\0')) {
        return false;
    }
    for (const char *p = s; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c != '%') {
            if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'))) {
                return true;
            }
            continue;
        }
        /* 変換指定を丸ごと飛ばす。 */
        ++p;
        if (*p == '\0') {
            break;
        }
        if ((*p == '%') || (*p == 'n')) {
            continue;
        }
        for (; *p != '\0'; ++p) {
            const unsigned char k = static_cast<unsigned char>(*p);
            const bool alpha = ((k >= 'a') && (k <= 'z')) || ((k >= 'A') && (k <= 'Z'));
            if (alpha && (k != 'l')) {
                break;
            }
        }
        if (*p == '\0') {
            break;
        }
        /*
         * **変換指定の直後の 1 字も数えない。** ダイスの表示（`1d3`）の
         * 2 つ目の `d` が「言葉」に見えてしまう。座標の表示も同じ。
         */
        const unsigned char nxt = static_cast<unsigned char>(p[1]);
        if (((nxt >= 'a') && (nxt <= 'z')) || ((nxt >= 'A') && (nxt <= 'Z'))) {
            ++p;
        }
    }
    return false;
}

const char *fc_tr(const char *s)
{
    return fc_lookup(s, key_has_word(s) ? 1 : 0);
}

const char *fc_tr_fmt(const char *fmt)
{
    return fc_lookup(fmt, key_has_word(fmt) ? 1 : 0);
}

/*!
 * @brief 品の種別の型を引く（フック #25）。宣言は `fc_lang_c.h`。
 *
 * @details 中身は `fc_tr()` と同じ引きである。**名前を分けてあるのは道が違うから**
 * ——ここへ来るのは `flavor.c` の `basenm` だけで、鍵は `O:` で書いた型
 * （`& Potion~ of %`）である。訳が無ければ英語の型がそのまま組まれ、
 * 未訳一覧（`--report-untranslated=`）に型が落ちる。
 *
 * `basenm` には日本語の `k_info` 名（`ダガー`）も渡ってくるが、
 * `fc_lookup()` が **0x80 以上を含む鍵は引かずに返す**ので素通りする。
 */
const char *fc_tr_basenm(const char *s)
{
    return fc_lookup(s, key_has_word(s) ? 1 : 0);
}

/*!
 * @brief 枠の名札を引く（フック #27）。宣言は `fc_lang_c.h`。
 *
 * @details **一般のカタログは引かない。** 名札は裸の 1 語が多く、
 * `Light` は光源・`Chaos` はカオス領域という別の訳を既に持っている。
 * ここは `F:` で書いた専用の表だけを見るので、その取り違えが起きない。
 *
 * 引けなければ `s` をそのまま返す——呼び手の `%-11.11s` が英語の名札を組む（制約 4）。
 */
const char *fc_frame_label(const char *s)
{
    if (!g_enabled || (s == nullptr) || (s[0] == '\0')) {
        return s;
    }
    const auto found = g_frame.find(std::string(s));
    if (found != g_frame.end()) {
        return found->second.c_str();
    }
    return s;
}

/*!
 * @brief 飾りの札を引く（フック #31 #32 #33）。宣言は `fc_lang_c.h`。
 *
 * @details **一般のカタログは引かない。** 鍵は `cursed` `good` `empty` のような
 * 裸の 1 語で、一般の表では別の意味を持つ。ここは `D:` で書いた専用の表だけを見る。
 *
 * **0x80 以上を含む鍵は引かない**（`fc_tr()` と同じ二重引きよけ）。品の名前は
 * 訳し終わった日本語で流れてくることがあり、それを鍵に立てても当たらないうえ、
 * 未訳一覧が汚れる。
 *
 * 引けなければ `s` をそのまま返す（制約 4）。
 */
const char *fc_decor(const char *s)
{
    if (!g_enabled || (s == nullptr) || (s[0] == '\0')) {
        return s;
    }
    for (const char *p = s; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) {
            return s;
        }
    }
    const auto found = g_decor.find(std::string(s));
    if (found != g_decor.end()) {
        return found->second.c_str();
    }
    return s;
}

/*!
 * @brief **店と建物の名**を引く（フック #34 #35）。
 *
 * @details 店の見出し（`shop.c` の `shop->type->name`）と、品の一覧に出る
 * 建物の札（`cmd4.c` の `_dungeon_notes_store_name()`）で使う。
 *
 * **一般のカタログ（`fc_tr`）とは別の表を見る。** 鍵が `Home` `Temple` `Museum`
 * のような**裸の 1 語**で、一般の表へ入れるとキーの名前まで巻き添えにする
 * ——`cmd4.c` のマクロの引き金の表に `{"[H", "Home"}` がある。
 * 原本では **`S:`** の札で書く。
 *
 * **0x80 以上を含む鍵は引かない**（`fc_tr()` と同じ二重引きよけ）。
 * 引けなければ `s` をそのまま返す（制約 4）。
 */
const char *fc_shop(const char *s)
{
    if (!g_enabled || (s == nullptr) || (s[0] == '\0')) {
        return s;
    }
    for (const char *p = s; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) {
            return s;
        }
    }
    const auto found = g_shop.find(std::string(s));
    if (found != g_shop.end()) {
        return found->second.c_str();
    }
    return s;
}

/*!
 * @brief **店主の名前**を引く（フック #36）。
 *
 * @details 店の画面の左上（`残酷なるヴェンジェラ (ハーフトロル)` の名前の側）。
 * `shop.c` の `_types[]` に埋め込まれた素の `char *` で、カタログのどの口も
 * 通らない。**`S:` とも別の表**である——どちらも全数照合を掛けているので、
 * 混ぜると照合が緩む。原本では **`K:`** の札で書く。
 *
 * **0x80 以上を含む鍵は引かない**（`fc_tr()` と同じ二重引きよけ）。
 * 引けなければ `s` をそのまま返す（制約 4）。
 */
const char *fc_shop_owner(const char *s)
{
    if (!g_enabled || (s == nullptr) || (s[0] == 0)) {
        return s;
    }
    for (const char *p = s; *p != 0; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) {
            return s;
        }
    }
    const auto found = g_keeper.find(std::string(s));
    if (found != g_keeper.end()) {
        return found->second.c_str();
    }
    return s;
}

/*!
 * @brief **呪文・属性・召喚の名**を引く（フック #46）。
 *
 * @details 敵の呪文の名（`Magic Missile`）・系統名（`Breathe`）・属性の名
 * （`Fire` `Nether`）・召喚の種別名（`Undead`）。どれも表の中の素の `char *` で、
 * `sprintf` / `string_printf` で組み立ててから画へ出るので、カタログのどの口も
 * 通らない。**呼ぶ側で引く。**
 *
 * **一般のカタログとも `A:` とも別の表を見る**（設計 §8.39 ④）。原本では
 * **`M:`** の札で書く。
 *
 * **0x80 以上を含む鍵は引かない**（`fc_tr()` と同じ二重引きよけ）。
 * 引けなければ `s` をそのまま返す（制約 4）。
 */
const char *fc_spell_name(const char *s)
{
    if (!g_enabled || (s == nullptr) || (s[0] == 0)) {
        return s;
    }
    for (const char *p = s; *p != 0; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) {
            return s;
        }
    }
    const auto found = g_spell.find(std::string(s));
    if (found != g_spell.end()) {
        return found->second.c_str();
    }
    return s;
}

/*!
 * @brief **状態列の長い札**を引く（フック #77）。宣言は `fc_lang_c.h`。
 *
 * @details 引けなければ `fc_tr()` へ落とす。**落とし先を一般のカタログにしてあるのは、
 * 桁を数える所と画へ出す所で同じ字を返させるため**である——`xtra1.c` は
 * `strlen()` で桁を数えてから `c_put_str()` で出し、その中の `Term_addstr()` が
 * また `fc_tr()` を通す。ここで落としておかないと、数えた桁と出た字の長さが食い違う。
 *
 * 引けなければ `s` をそのまま返す（制約 4）。
 */
const char *fc_bar(const char *s)
{
    if (!g_enabled || (s == nullptr) || (s[0] == 0)) {
        return s;
    }
    for (const char *p = s; *p != 0; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) {
            return s;
        }
    }
    const auto found = g_bar.find(std::string(s));
    if (found != g_bar.end()) {
        return found->second.c_str();
    }
    return fc_tr(s);
}

/*!
 * @brief **状態列の短い札**を引く（フック #77）。宣言は `fc_lang_c.h`。
 *
 * @details 鍵は `"<長い札>\\t<短い札>"` である。**短い札だけでは引けない**
 * ——`Bl` は 5 つの別物に使い回されている。
 *
 * **引けなければ `sstr` をそのまま返す**（`fc_tr()` へは落とさない）。
 * 短い札は 2 字の略号で、一般のカタログに同じ字があると別物の訳が出る。
 */
const char *fc_bar_short(const char *lstr, const char *sstr)
{
    if (!g_enabled || (sstr == nullptr) || (sstr[0] == 0) || (lstr == nullptr)) {
        return sstr;
    }
    std::string key(lstr);
    key += static_cast<char>(9);
    key += sstr;
    const auto found = g_bar.find(key);
    if (found != g_bar.end()) {
        return found->second.c_str();
    }
    return sstr;
}

/*!
 * @brief doc の旗（フック #13 の展開の間だけ立つ。設計 §4 追記 (d)）。
 * @details `fc_doc_vprintf()` が `vstrnfmt()` を呼ぶ間だけ 1 になる。
 * ゲームのスレッドは 1 本なのでロックは要らない。
 */
static int g_doc_scope = 0;

void fc_lang_doc_scope(int on)
{
    g_doc_scope = on;
}

/*!
 * @brief 裸の 1 語か（英数と `'` `-` `_` だけ＝ファイル名に化けうる形）。
 * @details `Dunadan` `Warrior` はこれに当たり、`hits you`（空白）や
 * `(1) Input Options`（記号）は当たらない。
 */
static bool bare_word(const char *s)
{
    if ((s == nullptr) || (s[0] == '\0')) {
        return false;
    }
    for (const char *p = s; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        const bool alnum = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'))
            || ((c >= '0') && (c <= '9'));
        if (!alnum && (c != '\'') && (c != '-') && (c != '_')) {
            return false;
        }
    }
    return true;
}

/*!
 * @brief 引数として渡る断片を引く（フック #3）。**引けなくても積まない。**
 * @details ここには敵名・品名・プレイヤ名・パス・数字も流れてくる。
 * 未訳一覧へ積むとそれで埋まって読めなくなるので、`record = 0` で引く。
 *
 * **裸の 1 語は doc の旗が立っているときにしか引かない**（設計 §4 追記 (d)）。
 * `format("%s-%s.prf", namebase, player_base)`（`autopick.c:925`）のように
 * `%s` にはファイル名も流れてくるので、種族名・職業名のような 1 語の鍵を
 * 素で引くと同名のキャラ名や pref 名まで訳してファイルが開けなくなる。
 */
const char *fc_tr_arg(const char *s)
{
    if ((g_doc_scope == 0) && bare_word(s)) {
        /*
         * **裸の 1 語は一般のカタログでは引かない**（設計 §4 追記 (d)）。
         * ただし `A:` の表に載せた語だけは引く——`You feel less %s.` の
         * `strong` のように、**語そのものが画面へ出る引数**があるためで、
         * 巻き添えの範囲は表の中に閉じる（設計 §8.35）。
         */
        const auto found = g_argword.find(std::string(s));
        if (found != g_argword.end()) {
            return found->second.c_str();
        }
        return s;
    }
    return fc_lookup(s, 0);
}


/*!
 * @brief モンスター一覧の見出し（フック #22 #23）。宣言は `fc_lang_c.h`。
 */
int fc_mon_list_header(char *buf, size_t max, int los, int sub, int probe,
                       int other, int count, int awake)
{
    if (!g_enabled || (buf == nullptr) || (max == 0)) {
        return 0;
    }

    const char *key = nullptr;
    if (los) {
        key = sub ? "%d monsters seen, %d awake:"
                  : (probe ? "You probe %d monsters, %d are awake:"
                           : "You see %d monsters, %d are awake:");
    } else if (sub) {
        key = other ? "%d other monsters known, %d awake:"
                    : "%d monsters known, %d awake:";
    } else {
        key = other ? "You are aware of %d other monsters, %d are awake:"
                    : "You are aware of %d monsters, %d are awake:";
    }

    const char *fmt = fc_tr_fmt(key);
    if (fmt == key) {
        return 0; /* 訳が無い。**英語の道へ落とす**（制約 4） */
    }
    std::snprintf(buf, max, fmt, count, awake);
    return 1;
}

/*!
 * @brief 拾った品を告げる 1 行（フック #38）。**書式に組み直してから引く。**
 *
 * `obj_delayed_describe()` は字を継ぎ足して 1 本にしてしまうので、
 * 継いだ結果ではなく**継ぐ前の形**を鍵にする（設計 §8.31 ⑦・§8.32）。
 */
int fc_obj_carry_msg(char *buf, size_t max, const char *name,
                     int wearing, int quiver, int slot_ch)
{
    if (!g_enabled || (buf == nullptr) || (max == 0) || (name == nullptr)) {
        return 0;
    }

    const bool slot = (slot_ch != 0);
    const char *key = nullptr;
    if (wearing) {
        key = slot ? "You are wearing %s (%c)." : "You are wearing %s.";
    } else if (quiver) {
        key = slot ? "You have %s in your quiver (%c)." : "You have %s in your quiver.";
    } else {
        key = slot ? "You have %s (%c)." : "You have %s.";
    }

    const char *fmt = fc_tr_fmt(key);
    if (fmt == key) {
        return 0; /* 訳が無い。**英語の道へ落とす**（制約 4） */
    }
    if (slot) {
        std::snprintf(buf, max, fmt, name, slot_ch);
    } else {
        std::snprintf(buf, max, fmt, name);
    }
    return 1;
}

/*!
 * @brief 建物の画面の見出し（フック #41）。**桁は当方で詰める。**
 *
 * 先方の `%.20s (%.20s) %35.35s` はバイトで切るので 2 バイト文字が割れる。
 * ここでは `fc_clip()`（文字の境で止める）を通してから組む。
 */
int fc_bldg_header(char *buf, size_t max, const char *owner,
                   const char *race, const char *name)
{
    if (!g_enabled || (buf == nullptr) || (max == 0)) {
        return 0;
    }
    if ((owner == nullptr) || (race == nullptr) || (name == nullptr)) {
        return 0;
    }

    const char *jo = fc_shop_owner(owner);
    const char *jr = fc_shop(race);
    const char *jn = fc_shop(name);
    if ((jo == owner) && (jr == race) && (jn == name)) {
        return 0; /* 3 つとも引けない。**英語の道へ落とす**（制約 4） */
    }

    /* 先方と同じ桁（20 / 20 / 35 で右寄せ）だが、**文字の境で切る**。 */
    char o[64], r[64], n[128];
    std::snprintf(o, sizeof(o), "%.*s", fc_clip(jo, 20), jo);
    std::snprintf(r, sizeof(r), "%.*s", fc_clip(jr, 20), jr);
    std::snprintf(n, sizeof(n), "%.*s", fc_clip(jn, 35), jn);
    std::snprintf(buf, max, "%s (%s) %35s", o, r, n);
    return 1;
}

/*!
 * @brief 建物の命令の名（フック #42）。**`S:` の表を引く。**
 *
 * **桁は 20 バイトまで。** 命令は 35 桁の欄へ 2 段で並ぶので、
 * `" x) "`（4）＋名＋`" (99999gp)"`（10）で 35 に収まらないと右の段と重なる。
 * **超える訳は使わず英語のまま出す**（`F:` の名札を 11 バイトで撥ねるのと同じ形）。
 * `S:` は店の名（桁の制限が無い）と同じ表なので、**撥ねるのは引く側でやる。**
 */
const char *fc_bldg_action(const char *s)
{
    const char *ja = fc_shop(s);
    if ((ja != s) && (std::strlen(ja) > 20)) {
        return s;
    }
    return ja;
}

/*!
 * @brief 武器の属性が乗った 1 行（フック #39）。**動詞まで解いた 1 文**を鍵にする。
 *
 * 先方は動詞（`slays` / `is covered in`）を引数で渡す。裸の 1 語は
 * `fc_tr_arg()` がわざと引かないので（設計 §4 追記 (d)）、書式だけ訳しても
 * 英語が残る。`fc_mon_list_header()`（#22 #23）と同じ手で解いた 1 文を鍵にする。
 */
int fc_slay_msg(char *buf, size_t max, const char *o_name,
                int is_slay, const char *kill_desc)
{
    if (!g_enabled || (buf == nullptr) || (max == 0)) {
        return 0;
    }
    if ((o_name == nullptr) || (kill_desc == nullptr) || (kill_desc[0] == '\0')) {
        return 0;
    }

    /* `Your %s slays animals.` / `Your %s is covered in acid.` を鍵にする。 */
    char key[128];
    std::snprintf(key, sizeof(key), "Your %%s %s %s.",
        is_slay ? "slays" : "is covered in", kill_desc);

    const char *fmt = fc_tr_fmt(key);
    if (fmt == key) {
        return 0; /* 訳が無い。**英語の道へ落とす**（制約 4） */
    }
    std::snprintf(buf, max, fmt, o_name);
    return 1;
}

} // extern "C"
