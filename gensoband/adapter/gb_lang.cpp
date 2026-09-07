/*!
 * @file gb_lang.cpp
 * @brief 英訳カタログの実装と C の皮の中身。
 *
 * 手本は `silq/adapter/sq_lang.cpp`（日本語化 M1）。**向きが逆**なので:
 * - 鍵は**日本語**（0x80 以上を含む CP932）、訳は ASCII。
 * - 二重引きよけも逆——**0x80 以上を 1 バイトも含まない文字列は引かない**
 *   （訳し終わった英語・数字・桁合わせの空白がそれ。あちらは「含んだら引かない」）。
 * - バイト算（切り・折り・禁則）は**要らない**。英語は ASCII ⊂ CP932 で、
 *   JP ビルドの既存の勘定がそのまま通る（設計 §4.3）。
 *
 * ## 寿命
 * `gb_tr()` が返す `const char *` は**カタログの中を指す**。カタログは
 * `lang_init()` から終了まで生きたままで、要素を消したり並べ替えたりしない
 * （`std::unordered_map` のノードは再ハッシュしても動かない）。
 *
 * ## スレッド
 * 引くのは**ゲームのスレッドだけ**である（Term を触るのがそこだけなので）。
 * 受信スレッドはここへ来ない。だからロックは要らない。`lang_write_report()` は
 * `gb_run_game()` から戻った後＝ゲームのスレッドが終わってから呼ぶ。
 *
 * ## 文字コード
 * 原本（`gensoband/lang/en/ui/*.en.txt`）は **UTF-8**、表に入るのは **CP932**。
 * 変換は `gb_text.h` の 1 か所だけを通る。
 */

#include "gb_lang.h"

#include "gb_lang_c.h"
#include "gb_text.h"

#include "portable/legacy_os.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>

namespace {

/* ====================================================================== 表の実体 */

//! 英語層が起きているか（`lang == "en"`）。カタログの成否とは別（設計 §6）。
bool g_enabled = false;

//! 鍵（CP932 の日本語原文）→ 訳（ASCII）。
std::unordered_map<std::string, std::string> g_catalog;

//! `E:` が空だった鍵＝**わざと訳さない**印。未訳一覧から外す。
std::set<std::string> g_silent;

//! 引けなかった鍵（`--report-untranslated=`）。重複はまとめる。
std::set<std::string> g_missing;
std::string g_report_path;

//! `gb_lang_file()` の実在検査に使う lib の根（`gensoband/lib`）。
std::string g_lib_dir;

//! `gb_lang_file()` の答えの置き場（返す `const char *` の寿命のため）。
std::unordered_map<std::string, std::string> g_file_map;

//! 英訳したヘルプの置き場を使っているか（E7）。使うならヘルプ名は引き替えない。
bool g_en_help = false;

/* ============================================================== 原本を読む道具 */

/*!
 * @brief `\n` `\t` `\r` `\\` `\s` を解く。
 * @details `\s` は**空白 1 個**（行頭・行末の空白は編集器に落とされやすい）。
 * 知らない綴りは**背中の `\` ごとそのまま**残す（鍵は 1 バイトでも違えば別物。
 * `sq_lang.cpp` と同じ規則——片方を直したら両方直す）。
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
 * @brief 書式の**引数の並び**を 1 本の文字列にする（`sq_lang.cpp` の写し）。
 * @details `vstrnfmt()`（`gensoband/src/z-form.c:235`）の読み方に合わせてある。
 * 日英で食い違う訳はカタログに入れない——実行時に落ちる前に見つける。
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
            break; //! 末尾が裸の `%`。素の文字として読む
        }
        if (static_cast<unsigned char>(fmt[i]) >= 0x80) {
            //! `%` の次が CP932 の 1 バイト目（`%ã®ç¢ºç` など）。変換指定ではないので**素の文字**として読む
            continue;
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
                continue; // 先頭大文字。引数には効かない
            }
            if (c >= 0x80) {
                break; //! CP932 の 1 バイト目。変換指定には現れないので**素の `%`** として読む
            }
            const bool alpha = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
            if (!alpha) {
                if (std::strchr("-+ #0'.0123456789", static_cast<char>(c)) != nullptr) {
                    continue; // 桁・精度・符号。引数には効かない（`*` を除く）
                }
                //! 変換指定に現れ得ない字（`%:鉱脈…` の `:`・`(40%)` の `)`）。
                //! ここで打ち切らないと、**CP932 の 2 バイト目が英字に見えて**
                //! 出鱈目な並びが出る（`鉱` = 0x8D 0x7A なので `z`）。E8 で直した。
                break;
            }
            one += static_cast<char>(c);
            if (c != 'l') {
                closed = true;
                break;
            }
        }
        if (!closed) {
            //! 変換の字が見つからないまま尽きた（`(destroyed 50%)` など）。**素の `%`** として読む
            continue;
        }
        sig += one;
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

/*!
 * @brief カタログを 1 ファイルぶん読む。
 * @param allow_override 真なら既存の鍵を上書きする（`messages.en.txt` が
 *        `harvested.en.txt` に勝つため）。同じファイルの中の重複は常に誤り。
 * @return 開けなければ偽（致命ではない。呼び手が数を見る）
 */
bool load_catalog_file(const std::string &path, bool allow_override, gb::LangLoadReport &rep)
{
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return false;
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

    std::set<std::string> seen_here; //!< このファイルの中の重複を見張る
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
        const std::string key = gb::utf8_to_sjis(unescape(key_utf8));
        if (key.empty()) {
            ++rep.not_in_cp932;
            return;
        }
        if (!seen_here.insert(key).second) {
            /* 同じファイルに同じ鍵が 2 度。黙って後勝ちにしない（Sil-Q §5 と同じ）。 */
            if (first_dup_line == 0) {
                first_dup_line = line_no;
                first_dup_key = key;
            }
            return;
        }
        if (!have_val || val_utf8.empty()) {
            g_silent.insert(key);
            ++rep.silent;
            return;
        }
        const std::string value = gb::utf8_to_sjis(unescape(val_utf8));
        if (value.empty()) {
            ++rep.not_in_cp932;
            return;
        }
        if (format_signature(key) != format_signature(value)) {
            ++rep.rejected;
            if (rep.rejected <= 10) {
                std::fprintf(stderr, "[gensoband:lang] argument mismatch, not loaded: \"%s\"\n",
                    gb::sjis_to_utf8(key).c_str());
            }
            return;
        }
        const auto found = g_catalog.find(key);
        if (found != g_catalog.end()) {
            if (!allow_override) {
                if (first_dup_line == 0) {
                    first_dup_line = line_no;
                    first_dup_key = key;
                }
                return;
            }
            found->second = value;
            ++rep.overridden;
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
        if (take_tag(line, "J:", body)) {
            if (have_val) {
                flush();
                key_utf8.clear();
                val_utf8.clear();
                have_key = false;
                have_val = false;
            }
            key_utf8 += body; // `J:` を続けると連結（長い書式を折れるように）
            have_key = true;
            continue;
        }
        if (take_tag(line, "E:", body)) {
            val_utf8 += body;
            have_val = true;
            continue;
        }
        if (take_tag(line, "W:", body)) {
            continue; // 桁の控え。見るのは `tools/gensoband/check_en.py`
        }
        std::fprintf(stderr, "[gensoband:lang] %s:%d: unknown line, ignored\n",
            path.c_str(), line_no);
    }
    flush();

    if ((first_dup_line != 0) && rep.error.empty()) {
        rep.error = "duplicate key at " + path + ":" + std::to_string(first_dup_line)
            + " -> \"" + gb::sjis_to_utf8(first_dup_key) + "\"";
    }
    return true;
}

/*! @brief 実在するファイルか（ディレクトリは実在に数えない）。 */
bool file_exists(const std::string &path)
{
    if (path.empty() || portable::dir_exists(path)) {
        return false;
    }
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return false;
    }
    std::fclose(fp);
    return true;
}

std::string join(const std::string &dir, const char *a, const std::string &b)
{
    std::string out = dir;
    if (!out.empty() && (out.back() != '\\') && (out.back() != '/')) {
        out.push_back(portable::kPathSep);
    }
    out += a;
    out.push_back(portable::kPathSep);
    out += b;
    return out;
}

} // namespace

namespace gb {

LangLoadReport lang_init(const std::string &lang, const std::string &lang_dir,
    const std::string &lib_dir, const std::string &report_path)
{
    LangLoadReport rep;

    g_enabled = (lang == "en");
    g_catalog.clear();
    g_silent.clear();
    g_missing.clear();
    g_file_map.clear();
    g_report_path = report_path;
    g_lib_dir = lib_dir;
    rep.enabled = g_enabled;
    if (!g_enabled) {
        return rep; // 日本語。**1 バイトも読まない**（設計 §2 制約 1）
    }

    const std::string ui = lang_dir + portable::kPathSep + "ui";
    /* 機械で抜いた対 → 手書き（後の方が勝つ）。どちらも無くてよい。 */
    (void)load_catalog_file(ui + portable::kPathSep + "harvested.en.txt", false, rep);
    (void)load_catalog_file(ui + portable::kPathSep + "messages.en.txt", true, rep);
    return rep;
}

int lang_write_report()
{
    if (g_report_path.empty() || g_missing.empty()) {
        return 0;
    }

    std::FILE *fp = std::fopen(g_report_path.c_str(), "wb");
    if (fp == nullptr) {
        std::fprintf(stderr, "[gensoband:lang] cannot write the untranslated report: %s\n",
            g_report_path.c_str());
        return 0;
    }

    /* **そのまま `messages.en.txt` へ貼れる形で書く**（`J:` と空の `E:`）。UTF-8。 */
    std::fprintf(fp, "# %s\n", "untranslated keys collected by GensobandCore --report-untranslated");
    std::fprintf(fp, "# %s\n\n", "sort with tools/gensoband/gb_untranslated.py, then fill in E:");
    int count = 0;
    for (const std::string &key : g_missing) {
        std::string utf8 = gb::sjis_to_utf8(key);
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
        /* 行頭と行末の空白は 1 つ残らず `\s` にする（編集器に落とされないように）。 */
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
        std::fprintf(fp, "J:%s\nE:\n\n", escaped.c_str());
        ++count;
    }
    std::fclose(fp);
    std::fprintf(stderr, "[gensoband:lang] %d untranslated key(s) written to %s\n",
        count, g_report_path.c_str());
    return count;
}

} // namespace gb

/* ============================================== C の皮（`gb_lang_c.h` の中身） */

extern "C" {

int gb_lang_enabled(void)
{
    return g_enabled ? 1 : 0;
}

/*!
 * @brief 引く本体。
 * @param record 引けなかったときに未訳一覧へ積むか
 * @details **0x80 以上を 1 バイトも含まない文字列は引かない**（設計 §5）。
 * 鍵はすべて日本語のリテラルなので、純 ASCII は「訳す物が無い」。
 * 訳した結果も ASCII なので、二重引きはこの門だけで起きなくなる。
 */
static const char *gb_lookup(const char *s, int record)
{
    if (!g_enabled || (s == nullptr) || (s[0] == '\0')) {
        return s;
    }
    bool has_hi = false;
    for (const char *p = s; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80) {
            has_hi = true;
            break;
        }
    }
    if (!has_hi) {
        return s;
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

const char *gb_tr(const char *s)
{
    return gb_lookup(s, 1);
}

const char *gb_tr_addstr(const char *s, int *n)
{
    if (!g_enabled || (s == nullptr) || (n == nullptr)) {
        return s;
    }
    const int len = static_cast<int>(std::strlen(s));
    if ((*n >= 0) && (*n < len)) {
        return s; // 部分文字列の描画。切り出しの意味を壊さない
    }
    const char *out = gb_lookup(s, 1);
    if ((out != s) && (*n >= 0)) {
        *n = -1; // 訳は長さが変わる。全文の描画なので「あるだけ」に戻す
    }
    return out;
}

const char *gb_tr_fmt(const char *fmt)
{
    return gb_lookup(fmt, 1);
}

const char *gb_tr_arg(const char *s)
{
    /*
     * **引けなかった引数も記録する。** 最初は記録していなかったが、それだと
     * `msg_format("%s%s", m_name, msg_py_atk(...))` の後ろの句（`を攻撃した。`）が
     * **引かれているのに一覧へ出ない**——画面には日本語が残るのに閉ループは
     * 「訳し終わった」と言う（E3 の通しで踏んだ）。
     *
     * 代わりに人物名・敵名のような訳す物でない日本語も並ぶが、それは
     * `tools/gensoband/gb_untranslated.py` が `dynamic` へ仕分ける。
     * **見えない穴より、仕分けの要る一覧のほうがよい。**
     */
    return gb_lookup(s, 1);
}

const char *gb_lang_file(const char *name)
{
    if (!g_enabled || (name == nullptr) || (name[0] == '\0')) {
        return name;
    }

    const std::string original(name);
    const auto cached = g_file_map.find(original);
    if (cached != g_file_map.end()) {
        return cached->second.c_str();
    }

    /* 引き替え先の候補を決める。 */
    std::string cand;
    if (g_en_help && (original.size() > 4)
        && (original.compare(original.size() - 4, 4, ".hlp") == 0)) {
        /*
         * **英訳したヘルプを使うときは引き替えない**（E7）。`lib-en/help` には
         * `thelp.hlp` も `jhelp.hlp` も英語で入っている——`thelp.hlp` は幻想蛮怒の
         * 英訳版、`jhelp.hlp` は上流の `help.hlp` の写し。ここで引き替えると
         * 入口が上流の古い英語ヘルプに化ける。
         */
    } else if (original == "thelp.hlp" || original == "jhelp.hlp") {
        cand = "help.hlp"; // 幻想蛮怒のヘルプの入口は thelp.hlp（files.c:10167）
    } else if (!g_en_help && (original == "jhelpinfo.txt")) {
        cand = "helpinfo.txt";
    } else {
        const auto strip = [&](const char *suffix, const char *repl) {
            const std::size_t n = std::strlen(suffix);
            if ((original.size() > n) && (original.compare(original.size() - n, n, suffix) == 0)) {
                cand = original.substr(0, original.size() - n) + repl;
                return true;
            }
            return false;
        };
        if (!strip("_j.txt", ".txt")) {
            (void)strip("_jp.txt", ".txt");
        }
    }

    /*
     * **実在するときだけ**替える。`get_rnd_line()` は lib/file を、`show_file()` は
     * lib/help を見る——どちらで開かれるかはここからは判らないので両方を見る
     * （名前は互いに重ならない。`_gen.txt` のような英語対の無い物は原名のまま
     *  ＝日本語が出る。設計 §2 制約 4）。
     */
    std::string mapped = original;
    if (!cand.empty()
        && (file_exists(join(g_lib_dir, "file", cand)) || file_exists(join(g_lib_dir, "help", cand)))) {
        mapped = cand;
        std::fprintf(stderr, "[gensoband:lang] file \"%s\" -> \"%s\"\n", name, mapped.c_str());
    }
    const auto stored = g_file_map.emplace(original, mapped);
    return stored.first->second.c_str();
}

void gb_lang_set_en_help(int on)
{
    g_en_help = (on != 0);
    g_file_map.clear(); // 憶えた答えは前提が変わったので捨てる
}

} // extern "C"
