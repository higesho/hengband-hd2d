/*!
 * @file sq_lang.cpp
 * @brief 訳文カタログの実装と、C の皮のうち引く 3 つ。
 *
 * ## この TU の立ち位置
 * `sq_lang_c.h` が約束している 8 つのうち、**カタログを持つ 3 つ**
 * （`sq_lang_enabled` / `sq_tr` / `sq_tr_fmt`）をここで実装する。
 * 残る 5 つは `sq_lang_c.c`（素のバイト算）にある。
 *
 * ## 寿命
 * `sq_tr()` が返す `const char *` は**カタログの中を指す**。カタログは
 * `lang_init()` から終了まで生きたままで、要素を消したり並べ替えたりしない
 * （`std::unordered_map` のノードは再ハッシュしても動かない）。
 * よって呼び手（`silq/src`）は解放しないし、持ち回してよい。
 *
 * ## 糸
 * 引くのは**ゲームの糸だけ**である（Term を触るのがそこだけなので）。
 * 受信糸はここへ来ない。だからロックは要らない。`lang_write_report()` は
 * `sq_run_game()` から戻った後＝ゲームの糸が終わってから呼ぶ。
 *
 * ## 文字コード
 * 原本（`silq/lang/ja/ui/messages.ja.txt`）は **UTF-8**、表に入るのは **CP932**。
 * 変換は `sq_text.h` の 1 か所だけを通る（設計 §3.1）。
 */

#include "sq_lang.h"

#include "sq_lang_c.h"
#include "sq_text.h"

#include "portable/legacy_os.h"

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

//! `J:` が空だった鍵＝**わざと訳さない**印。未訳一覧から外す。
std::set<std::string> g_silent;

//! 引けなかった鍵（`--report-untranslated=`）。重複はまとめる。
std::set<std::string> g_missing;
std::string g_report_path;

/* ============================================================== 原本を読む道具 */

/*!
 * @brief `\n` `\t` `\r` `\\` `\s` を解く。
 * @details `\s` は**空白 1 個**。行頭・行末の空白は編集器に落とされやすいので、
 * 抜き出し器（`tools/silq/extract_ja_keys.py`）はそこだけ `\s` で書く。
 * 知らない綴り（`\q` など）は**背中の `\` ごとそのまま**残す——鍵は 1 バイトでも
 * 違えば別物なので、勝手に落とすほうが害が大きい（設計 §5「鍵は正規化しない」）。
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
 * @details `vstrnfmt()`（`z-form.c:180-400`）の読み方に合わせてある:
 * - `%%` は引数を食わない
 * - `%n` は `size_t*` を 1 つ食う
 * - `*` は `int` を 1 つ**余分に**食う
 * - `^`（先頭大文字）と桁指定は引数に効かない
 * - `l` は型を変える
 *
 * 例: `"%^s hits %s for %d damage."` → `"s|s|d|"`、`"%*d"` → `"*d|"`。
 *
 * これを英日で突き合わせ、**食い違う訳はカタログに入れない**（設計 §4）。
 * 実行時に落ちる前に見つけるのが目的なので、厳しめでよい。
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
            sig += "?|"; // 尻切れの書式。英日どちらも同じ形でなければ弾かれる
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
        for (; i < fmt.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(fmt[i]);
            if (c == '*') {
                one += '*';
                continue;
            }
            if (c == '^') {
                continue; // 引数には効かない
            }
            const bool alpha = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
            if (!alpha) {
                continue; // 桁・精度・符号。引数には効かない（`*` を除く）
            }
            one += static_cast<char>(c);
            if (c != 'l') {
                closed = true;
                break;
            }
        }
        sig += closed ? one : (one + "?");
        sig += '|';
    }
    return sig;
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

namespace sq {

LangLoadReport lang_init(const std::string &lang, const std::string &lang_dir,
    const std::string &report_path)
{
    LangLoadReport rep;

    g_enabled = (lang == "ja");
    g_catalog.clear();
    g_silent.clear();
    g_missing.clear();
    g_report_path = report_path;
    rep.enabled = g_enabled;
    if (!g_enabled) {
        return rep; // 英語。**1 バイトも読まない**（設計 §1 制約 1）
    }

    const std::string path = lang_dir + portable::kPathSep + "ui" + portable::kPathSep
        + "messages.ja.txt";
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        /*
         * **これは致命ではない**。名前は `lib-ja/edit` から来るので、
         * カタログが無くても日本語は出る（設計 §5 の註）。
         */
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

    /* UTF-8 の BOM は落とす（編集器が付けることがある）。 */
    if ((text.size() >= 3) && (static_cast<unsigned char>(text[0]) == 0xEF)
        && (static_cast<unsigned char>(text[1]) == 0xBB) && (static_cast<unsigned char>(text[2]) == 0xBF)) {
        text.erase(0, 3);
    }

    std::string key_utf8;
    std::string val_utf8;
    bool have_key = false;
    bool have_val = false;
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
        if (format_signature(key) != format_signature(value)) {
            ++rep.rejected;
            if (rep.rejected <= 10) {
                std::fprintf(stderr, "[silq:lang] argument mismatch, not loaded: \"%s\" -> \"%s\"\n",
                    key.c_str(), value.c_str());
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
        if (take_tag(line, "E:", body)) {
            if (have_val) {
                flush();
                key_utf8.clear();
                val_utf8.clear();
                have_key = false;
                have_val = false;
            }
            key_utf8 += body; // `E:` を続けると連結（長い書式を折れるように）
            have_key = true;
            continue;
        }
        if (take_tag(line, "J:", body)) {
            val_utf8 += body;
            have_val = true;
            continue;
        }
        if (take_tag(line, "W:", body)) {
            continue; // 桁の控え。見るのは `tools/silq/check_ja.py`
        }
        std::fprintf(stderr, "[silq:lang] %s:%d: unknown line, ignored: %s\n",
            path.c_str(), line_no, line.c_str());
    }
    flush();

    if (first_dup_line != 0) {
        rep.error = "duplicate key at " + path + ":" + std::to_string(first_dup_line)
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
        std::fprintf(stderr, "[silq:lang] cannot write the untranslated report: %s\n",
            g_report_path.c_str());
        return 0;
    }

    /*
     * **そのまま `messages.ja.txt` へ貼れる形で書く**（`E:` と空の `J:`）。
     * 原本は UTF-8 なので、書き出しも UTF-8 に戻す。
     */
    std::fprintf(fp, "# %s\n", "untranslated keys collected by SilCore --report-untranslated");
    std::fprintf(fp, "# %s\n\n", "paste into silq/lang/ja/ui/messages.ja.txt and fill in J:");
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
         * **行頭と行末の空白は 1 つ残らず `\s` にする**（編集器に落とされないように）。
         * 1 個だけ守っても、その内側の空白が落ちれば鍵は別物になる
         * ——`" The world was young, ...        "` のような画面の 1 行が実際にそうなる。
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
    std::fprintf(stderr, "[silq:lang] %d untranslated key(s) written to %s\n",
        count, g_report_path.c_str());
    return count;
}

} // namespace sq

/* ============================================== C の皮（`sq_lang_c.h` の 3 つ） */

extern "C" {

int sq_lang_enabled(void)
{
    return g_enabled ? 1 : 0;
}

/*!
 * @brief 引く本体。`sq_tr` と `sq_tr_fmt` の共通部分。
 * @param s 原文
 * @param record 引けなかったときに未訳一覧へ積むか
 */
static const char *sq_lookup(const char *s, int record)
{
    if (!g_enabled || (s == nullptr) || (s[0] == '\0')) {
        return s;
    }
    /*
     * **二重引きを避ける**（設計 §4）。`vstrnfmt()` で訳した日本語が
     * `Term_addstr()` にも流れてくる。
     *
     * **先頭だけでは足りない。** `format("%c) %s", 'a', "ノルドール")` の結果は
     * `a) ノルドール` で、頭は ASCII だが中身は訳し終わっている。先頭 1 バイトで
     * 判じていたころ、この形が未訳一覧を埋めた（誕生の品書きだけで 8 件）。
     * **鍵はすべて英語のリテラルなので、0x80 以上を 1 バイトでも含むなら訳済み**
     * と見てよい。
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
 * @details **変換指定の中の字は数えない**——`format("%s", ...)` のような
 * 内部の配管や `(%+d,%dd%d)`（ダイスの表示）で一覧が埋まると読めなくなる。
 * 素の数字・空白・罫線（`41:41` / `2,000` / `____` / 桁合わせの空白）も
 * 同じ理由で外す。
 *
 * **引くこと自体は止めない。** 止めるのは「一覧へ積む」だけで、
 * 訳が要ると分かればカタログに書けばそのまま効く（設計 §5 の `J:` 空と同じ立場）。
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
        /* 変換指定を丸ごと飛ばす（`%%` `%n` `%*d` `%^s` `%ld` すべて）。 */
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
         * **変換指定の直後の 1 字も数えない。** `%dd%d`（`1d3`）の 2 つ目の `d` が
         * 「言葉」に見えてしまう。ダイスと座標の表示がここで落ちる。
         */
        const unsigned char nxt = static_cast<unsigned char>(p[1]);
        if (((nxt >= 'a') && (nxt <= 'z')) || ((nxt >= 'A') && (nxt <= 'Z'))) {
            ++p;
        }
    }
    return false;
}

const char *sq_tr(const char *s)
{
    return sq_lookup(s, key_has_word(s) ? 1 : 0);
}

const char *sq_tr_fmt(const char *fmt)
{
    return sq_lookup(fmt, key_has_word(fmt) ? 1 : 0);
}

/*!
 * @brief `%s` の引数を引く（フック #9）。**引けなくても積まない。**
 * @details `%s` には敵名・品名・プレイヤ名・道・数字も流れてくる。
 * 未訳一覧へ積むとそれで埋まって読めなくなるので、`record = 0` で引く。
 */
const char *sq_tr_arg(const char *s)
{
    return sq_lookup(s, 0);
}

} // extern "C"
