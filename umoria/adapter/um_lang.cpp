/*
 * Umoria の日本語化の層。
 *
 * 訳の表は `<exe>/umoria/lang/ja/` の `names.ja.txt`・`messages.ja.txt`・`screens.ja.txt`（1 行に「英語<TAB>日本語」。`#` で始まる行は註）。
 * - 英語に `%s` `%d` などがあれば書式の表として扱う。日本語の側は `{1}` `{2}` で英語の何番目の値かを書く
 *   （語順が変わるため）。`%s` で受けた字は、もう一度この層で訳す（名前の表を引く）。
 * - 書式の無い行は完全一致の表。
 * 同じ英語は一度引いたら覚えておく（フレームごとに同じ文を何度も送るため）。
 */

#include "um_lang.h"

#include "um_host.h"
#include "portable/legacy_os.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

/*
 * 書式の行（`%s hits you.`）を「固定の字」と「値」の並びにしたもの。照合は固定の字を探して合わせる。
 * はじめ std::regex を使ったが、頭が値で始まる書式が多く、libc++ の照合が重すぎて最初の画が出るまで
 * 90 秒かかった（手間の上限で例外も出た）。固定の字を探すだけなら、値の数に対してほぼ線形で済む。
 */
struct Token {
    char kind = 0; //!< 0 = 固定の字、's' = 字（訳し直す）、'n' = 数、'c' = 1 字
    std::string text;
};

struct Pattern {
    std::vector<Token> tokens;
    std::string ja;
};

std::atomic<bool> g_ja{ false };
std::mutex g_mutex;
bool g_loaded = false;
std::unordered_map<std::string, std::string> g_exact;
std::vector<Pattern> g_patterns;
std::unordered_map<std::string, std::string> g_cache;
//! 店主の名前（owners.ja.txt から取り出した英語と日本語）。切れた名前（「Inglorian the」）を補うときに引く。
std::vector<std::pair<std::string, std::string>> g_owner_names;
std::set<std::string> g_misses;
bool g_misses_dirty = false;
//! 書式の候補を試しに訳している深さ。試しの間は「訳が当たらなかった字」に記録しない（translate_locked）。
int g_probing = 0;

std::string lang_dir()
{
    std::string path = portable::exe_dir();
    if (!path.empty() && path.back() != portable::kPathSep) path.push_back(portable::kPathSep);
    return path + "umoria" + portable::kPathSep + "lang" + portable::kPathSep + "ja" + portable::kPathSep;
}

//! 原作の書式（`%-20s` `%3d` `%ld` `%c` `%%`）を固定の字と値の並びにする。値が無ければ偽。
bool compile_format(const std::string &format, std::vector<Token> &tokens)
{
    bool any = false;
    std::string literal;
    const auto flush = [&]() {
        if (!literal.empty()) tokens.push_back(Token{ 0, literal });
        literal.clear();
    };
    for (std::size_t i = 0; i < format.size(); ++i) {
        const char c = format[i];
        if (c != '%' || i + 1 >= format.size()) {
            literal.push_back(c);
            continue;
        }
        if (format[i + 1] == '%') {
            literal.push_back('%');
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < format.size() && std::string("-+ 0#").find(format[j]) != std::string::npos) ++j;
        while (j < format.size() && (std::isdigit(static_cast<unsigned char>(format[j])) || format[j] == '.')) ++j;
        while (j < format.size() && (format[j] == 'l' || format[j] == 'h')) ++j;
        if (j >= format.size()) return false;
        const char conv = format[j];
        char kind = 0;
        if (conv == 's') kind = 's';
        else if (conv == 'd' || conv == 'i' || conv == 'u') kind = 'n';
        else if (conv == 'c') kind = 'c';
        else return false;
        flush();
        tokens.push_back(Token{ kind, std::string() });
        any = true;
        i = j;
    }
    flush();
    return any;
}

//! 値の照合。`ti` 番目からの並びを `text` の `pos` 以降へ当てる。当たれば値を `values` に積んで真。
bool match_tokens(const std::vector<Token> &tokens, std::size_t ti, const std::string &text, std::size_t pos,
    std::vector<std::string> &values)
{
    if (ti == tokens.size()) return pos == text.size();
    const Token &token = tokens[ti];
    if (token.kind == 0) {
        if (text.compare(pos, token.text.size(), token.text) != 0) return false;
        return match_tokens(tokens, ti + 1, text, pos + token.text.size(), values);
    }
    if (token.kind == 'c') {
        if (pos >= text.size()) return false;
        values.push_back(text.substr(pos, 1));
        if (match_tokens(tokens, ti + 1, text, pos + 1, values)) return true;
        values.pop_back();
        return false;
    }
    if (token.kind == 'n') {
        std::size_t at = pos;
        while (at < text.size() && text[at] == ' ') ++at; // 幅の指定（%3d）で前に空白が付く
        std::size_t digits = at;
        if (digits < text.size() && (text[digits] == '+' || text[digits] == '-')) ++digits;
        const std::size_t start = digits;
        while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits]))) ++digits;
        if (digits == start) return false;
        values.push_back(text.substr(at, digits - at));
        if (match_tokens(tokens, ti + 1, text, digits, values)) return true;
        values.pop_back();
        return false;
    }
    // 字の値: 次の固定の字が現れる所ごとに、短い順に試す（最後の値なら残り全部）。
    if (ti + 1 == tokens.size()) {
        if (pos >= text.size()) return false;
        values.push_back(text.substr(pos));
        return true;
    }
    const Token &next = tokens[ti + 1];
    if (next.kind != 0) return false; // 値が続く書式（`%s%s`）は区切れないので扱わない
    /*
     * 前に固定の字があるときは、空の値も試す（「which is%s your sector」の %s は自分の区画のとき空。
     * 空を許さなかったので「Map sector [0,0], which is your sector.」が訳せなかった。2026-10-04）。
     */
    const std::size_t from = (ti > 0 && tokens[ti - 1].kind == 0) ? pos : pos + 1;
    for (std::size_t at = text.find(next.text, from); at != std::string::npos; at = text.find(next.text, at + 1)) {
        values.push_back(text.substr(pos, at - pos));
        if (match_tokens(tokens, ti + 1, text, at, values)) return true;
        values.pop_back();
    }
    return false;
}

void load_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        const std::string en = line.substr(0, tab);
        const std::string ja = line.substr(tab + 1);
        if (en.find('%') != std::string::npos) {
            Pattern pattern;
            if (compile_format(en, pattern.tokens)) {
                pattern.ja = ja;
                g_patterns.push_back(std::move(pattern));
                continue;
            }
        }
        g_exact.emplace(en, ja);
    }
}

void load_item_tables(const std::string &dir);
void load_tsv(const std::string &path, const std::function<void(const std::vector<std::string> &)> &take);

void ensure_loaded()
{
    if (g_loaded) return;
    g_loaded = true;
    const std::string dir = lang_dir();
    // 手で書く表（screens）を先に読む。同じ鍵は先に読んだ方が勝つ（作成物の表に別の訳があっても、手で決めた訳を使う）。
    for (const char *name : { "screens.ja.txt", "names.ja.txt", "messages.ja.txt", "owners.ja.txt" }) load_file(dir + name);
    /*
     * 店主の表の鍵は店の画面の見出しの 1 行（「Ga-nat the Greedy      (Gnome)      Alchemist」）。原作は名前・種族・店の種類を
     * 別々に書くので、名前と種族だけでも引けるように取り出す（訳は「強欲なガナット（ノーム）」。2026-10-04）。
     */
    load_tsv(dir + "owners.ja.txt", [](const std::vector<std::string> &c) {
        const std::size_t gap = c[0].find("  ");
        const std::size_t open = c[0].find('(');
        const std::size_t close = c[0].find(')', open == std::string::npos ? 0 : open);
        const std::size_t ja_open = c[1].find("\xEF\xBC\x88"); // （
        if (gap == std::string::npos || open == std::string::npos || close == std::string::npos || ja_open == std::string::npos) return;
        g_exact.emplace(c[0].substr(0, gap), c[1].substr(0, ja_open));
        g_owner_names.emplace_back(c[0].substr(0, gap), c[1].substr(0, ja_open));
        g_exact.emplace(c[0].substr(open, close - open + 1), c[1].substr(ja_open));
    });
    load_item_tables(dir);
}

bool has_letters(const std::string &text)
{
    int run = 0;
    for (const char c : text) {
        run = std::isalpha(static_cast<unsigned char>(c)) ? run + 1 : 0;
        if (run >= 2) return true;
    }
    return false;
}

std::string translate_locked(const std::string &en, int depth);

//! 訳した字に英語の語（小文字を含む英字 2 字以上）が残っていなければ真。単位の "Lv" は数えない。
bool fully_translated_text(const std::string &text)
{
    for (std::size_t i = 0; i < text.size();) {
        if (!std::isalpha(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::size_t j = i;
        bool lower = false;
        while (j < text.size() && std::isalpha(static_cast<unsigned char>(text[j]))) {
            lower = lower || std::islower(static_cast<unsigned char>(text[j]));
            ++j;
        }
        if (j - i >= 2 && lower && text.compare(i, j - i, "Lv") != 0) return false;
        i = j;
    }
    return true;
}

//! UTF-8 の字の途中で終わっていたら、その字の頭まで切る。
void trim_partial_utf8(std::string &text)
{
    std::size_t lead = text.size();
    while (lead > 0 && (static_cast<unsigned char>(text[lead - 1]) & 0xC0) == 0x80) --lead;
    if (lead == 0) return;
    const unsigned char c = static_cast<unsigned char>(text[lead - 1]);
    const std::size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (lead - 1 + need > text.size()) text.resize(lead - 1);
}

//! 書式の中の字（%s）が全部訳せるか。英字を含まない値（数・記号）は訳せたことにする。試しの間は記録しない。
bool pattern_values_translate(const Pattern &pattern, const std::vector<std::string> &values, int depth)
{
    std::size_t index = 0;
    for (const Token &token : pattern.tokens) {
        if (token.kind == 0) continue;
        if (index >= values.size()) return false;
        const std::string &value = values[index++];
        if (token.kind != 's') continue;
        const std::size_t first = value.find_first_not_of(' ');
        if (first == std::string::npos) continue;
        const std::size_t last = value.find_last_not_of(' ');
        const std::string trimmed = value.substr(first, last - first + 1);
        if (!has_letters(trimmed)) continue;
        ++g_probing;
        // 一部だけ訳せた（欄ごとに訳して英語が残った）ものは訳せたと数えない（後ろの別の文まで飲み込んでいた。2026-10-04）。
        const std::string ja = translate_locked(trimmed, depth + 1);
        const bool ok = ja != trimmed && fully_translated_text(ja);
        --g_probing;
        if (!ok) return false;
    }
    return true;
}

int visual_width(const std::string &text);

std::string apply_pattern(const Pattern &pattern, const std::vector<std::string> &values, int depth)
{
    std::vector<char> kinds;
    for (const Token &token : pattern.tokens) {
        if (token.kind != 0) kinds.push_back(token.kind);
    }
    std::string out;
    for (std::size_t i = 0; i < pattern.ja.size(); ++i) {
        if (pattern.ja[i] == '{') {
            const std::size_t close = pattern.ja.find('}', i);
            if (close != std::string::npos) {
                const int index = std::atoi(pattern.ja.substr(i + 1, close - i - 1).c_str());
                if (index >= 1 && static_cast<std::size_t>(index) <= values.size()) {
                    const std::string &value = values[static_cast<std::size_t>(index - 1)];
                    if (kinds[static_cast<std::size_t>(index - 1)] == 's') {
                        // 字の値は前後の空白を外して引く（"%s: %6d" の "LEV " のように、桁をそろえる空白が付いてくる）。
                        const std::size_t first = value.find_first_not_of(' ');
                        const std::size_t last = value.find_last_not_of(' ');
                        if (first == std::string::npos) {
                            out += value;
                        } else {
                            /*
                             * 後ろに桁合わせの空白（"%-38s: %s" の名前）があった値は、訳した後も同じ幅になるよう空白で埋める
                             * （オプションの画面で「: はい」の桁がそろわなかった。2026-10-04）。
                             */
                            const std::string ja = translate_locked(value.substr(first, last - first + 1), depth + 1);
                            out += value.substr(0, first) + ja;
                            if (last + 1 < value.size()) {
                                const int pad = visual_width(value.substr(first)) - visual_width(ja);
                                if (pad > 0) out.append(static_cast<std::size_t>(pad), ' ');
                            }
                        }
                    } else {
                        out += value;
                    }
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern.ja[i]);
    }
    return out;
}

/*
 * アイテムの名前の組み直し。
 * 原作の itemDescription()（identification.cpp）が組んだ英語を、逆にたどって日本語で組み直す:
 *   [a|an|some|数|no more] 本体 [エゴ] [(1d4)] [(+1,+2)] [[3,+1]] [(5 charges)] [with N turns of light] [{銘}]
 * 本体は items.ja.txt（単数と複数の鍵）、「Potion of 〜」「Blue Potion」の形、巻物の題、魔法書。
 * 個数は変愚と同じ助数詞（items.ja.txt の 3 列目。無ければ「つ」、10 以上は「個」）。
 */
struct ItemKind {
    const char *singular;
    const char *plural;
    const char *flavor_key; //!< flavors.ja.txt の種類
    const char *ja;
    const char *counter;
};

const ItemKind kItemKinds[] = {
    { "Potion", "Potions", "potion", "\xE8\x96\xAC", "\xE6\x9C\x8D" },                   // 薬・服
    { "Scroll", "Scrolls", "scroll", "\xE5\xB7\xBB\xE7\x89\xA9", "\xE5\xB7\xBB" },       // 巻物・巻
    { "Ring", "Rings", "ring", "\xE6\x8C\x87\xE8\xBC\xAA", "" },                          // 指輪
    { "Amulet", "Amulets", "amulet", "\xE3\x82\xA2\xE3\x83\x9F\xE3\x83\xA5\xE3\x83\xAC\xE3\x83\x83\xE3\x83\x88", "" }, // アミュレット
    { "Staff", "Staffs", "staff", "\xE6\x9D\x96", "\xE6\x9C\xAC" },                      // 杖・本
    { "Wand", "Wands", "wand", "\xE9\xAD\x94\xE6\xB3\x95\xE6\xA3\x92", "\xE6\x9C\xAC" }, // 魔法棒・本
    { "Mushroom", "Mushrooms", "mushroom", "\xE3\x82\xAD\xE3\x83\x8E\xE3\x82\xB3", "" }, // キノコ
};

struct ItemName {
    std::string ja;
    std::string counter;
};

std::unordered_map<std::string, ItemName> g_items;
std::unordered_map<std::string, std::string> g_flavors;
std::vector<std::pair<std::string, std::string>> g_egos; //!< 英語 → 日本語（長い順。後ろから外すため）
//! 称号（鍵は「職業:称号」。職業ごとに訳が違う）と、いまの職業（アダプタが渡す）。
std::unordered_map<std::string, std::string> g_titles;
std::string g_player_class;
//! 経歴の部品（鍵は「Umoria の段<TAB>英語」。値は日本語と日本語の段）。history.ja.txt。
std::unordered_map<std::string, std::pair<std::string, int>> g_history;
//! アダプタが外から決める訳（経歴の行など、原作が組み立てて折り返した字）。ほかのどの表より先に引く。
std::unordered_map<std::string, std::string> g_dynamic;

void load_tsv(const std::string &path, const std::function<void(const std::vector<std::string> &)> &take)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::size_t start = 0;
        for (;;) {
            const std::size_t tab = line.find('\t', start);
            cols.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        if (cols.size() >= 2 && !cols[0].empty()) take(cols);
    }
}

void load_item_tables(const std::string &dir)
{
    load_tsv(dir + "items.ja.txt", [](const std::vector<std::string> &c) {
        g_items.emplace(c[0], ItemName{ c[1], c.size() >= 3 ? c[2] : std::string() });
    });
    load_tsv(dir + "flavors.ja.txt", [](const std::vector<std::string> &c) { g_flavors.emplace(c[0], c[1]); });
    load_tsv(dir + "egos.ja.txt", [](const std::vector<std::string> &c) { g_egos.emplace_back(c[0], c[1]); });
    load_tsv(dir + "titles.ja.txt", [](const std::vector<std::string> &c) { g_titles.emplace(c[0], c[1]); });
    load_tsv(dir + "history.ja.txt", [](const std::vector<std::string> &c) {
        if (c.size() >= 4) g_history.emplace(c[0] + "\t" + c[1], std::make_pair(c[2], std::atoi(c[3].c_str())));
    });
    std::sort(g_egos.begin(), g_egos.end(), [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
}

bool starts_with(const std::string &text, const std::string &head) { return text.compare(0, head.size(), head) == 0; }
bool ends_with(const std::string &text, const std::string &tail)
{
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

//! 見た目の語（「青い」「めのうの」）。「の」か「い」で終わらなければ「の」を足す（Angband の an_ja_flavor と同じ）。
std::string flavor_word(const char *kind, const std::string &word)
{
    const auto it = g_flavors.find(std::string(kind) + ":" + word);
    if (it == g_flavors.end()) return std::string();
    std::string ja = it->second;
    if (!(ends_with(ja, "\xE3\x81\xAE") || ends_with(ja, "\xE3\x81\x84"))) ja += "\xE3\x81\xAE"; // の・い
    return ja;
}

//! 本体（個数・付け足し・エゴを外した残り）を訳す。訳せなければ偽。
/*
 * 原作は欄の幅で名前を切る（店の中の持ち物の一覧は右に案内が出るので短い。「a Potion of Restore Intellige」「[Exorc」）。
 * 切れた字で始まる鍵が表に 1 つだけあれば、その鍵を返す（2 つ以上なら決められないので偽）。4 字未満は見ない。
 */
template <typename Map>
bool unique_prefix(const std::string &cut, const Map &map, std::string &whole)
{
    if (cut.size() < 4) return false;
    int found = 0;
    for (const auto &entry : map) {
        if (entry.first.size() > cut.size() && entry.first.compare(0, cut.size(), cut) == 0) {
            whole = entry.first;
            if (++found > 1) return false;
        }
    }
    return found == 1;
}

bool translate_item_core(const std::string &core, int depth, std::string &ja, std::string &counter)
{
    if (const auto it = g_items.find(core); it != g_items.end()) {
        ja = it->second.ja;
        counter = it->second.counter;
        return true;
    }
    // 巻物の題（原作は意味の無い音節を並べる。題はそのまま）。
    for (const char *head : { "Scroll titled \"", "Scrolls titled \"" }) {
        if (starts_with(core, head) && ends_with(core, "\"")) {
            ja = core.substr(std::strlen(head) - 1) + "\xE3\x81\xA8\xE9\xA1\x8C\xE3\x81\x95\xE3\x82\x8C\xE3\x81\x9F\xE5\xB7\xBB\xE7\x89\xA9"; // と題された巻物
            counter = "\xE5\xB7\xBB";
            return true;
        }
    }
    // 魔法書（原作は「Book of Magic Spells [Magick I]」）。
    const std::pair<const char *, const char *> books[] = {
        { "Book of Magic Spells ", "\xE9\xAD\x94\xE8\xA1\x93\xE6\x9B\xB8" }, { "Books of Magic Spells ", "\xE9\xAD\x94\xE8\xA1\x93\xE6\x9B\xB8" }, // 魔術書
        { "Holy Book of Prayers ", "\xE7\xA5\x88\xE7\xA5\xB7\xE6\x9B\xB8" }, { "Holy Books of Prayers ", "\xE7\xA5\x88\xE7\xA5\xB7\xE6\x9B\xB8" }, // 祈祷書
    };
    for (const auto &[head, kind] : books) {
        if (starts_with(core, head)) {
            std::string title = core.substr(std::strlen(head));
            static const std::vector<std::string> kMageTitles = { "[Beginners-Magick]", "[Magick I]", "[Magick II]", "[The Mages' Guide to Power]" };
            static const std::vector<std::string> kPriestTitles = { "[Beginners Handbook]", "[Words of Wisdom]", "[Chants and Blessings]", "[Exorcisms and Dispellings]" };
            if (!title.empty() && title.back() != ']') {
                // 題が切れている（「[Exorc」）。本の種類の題の中で、その字で始まるものが 1 つだけなら補う。
                const auto &titles = (std::strncmp(head, "Holy", 4) == 0) ? kPriestTitles : kMageTitles;
                std::vector<std::string> hits;
                for (const std::string &candidate : titles) {
                    if (candidate.size() > title.size() && candidate.compare(0, title.size(), title) == 0) hits.push_back(candidate);
                }
                if (hits.size() == 1) {
                    title = hits[0];
                } else if (hits.size() > 1) {
                    // 「[Magic」は「[Magick I]」と「[Magick II]」の両方に当たる。訳の共通の頭に「…」を付けて出す。
                    std::string common = translate_locked(hits[0], depth + 1);
                    for (std::size_t k = 1; k < hits.size(); ++k) {
                        const std::string other = translate_locked(hits[k], depth + 1);
                        std::size_t n = 0;
                        while (n < common.size() && n < other.size() && common[n] == other[n]) ++n;
                        common.resize(n);
                    }
                    trim_partial_utf8(common);
                    ja = std::string(kind) + common + "\xE2\x80\xA6"; // …
                    counter = "\xE5\x86\x8A"; // 冊
                    return true;
                }
            }
            ja = std::string(kind) + translate_locked(title, depth + 1);
            counter = "\xE5\x86\x8A"; // 冊
            return true;
        }
    }
    for (const ItemKind &kind : kItemKinds) {
        for (const char *word : { kind.singular, kind.plural }) {
            // 「Potion of Cure Light Wounds」→「軽傷の治癒の薬」
            const std::string of = std::string(word) + " of ";
            if (starts_with(core, of)) {
                const std::string what = core.substr(of.size());
                std::string what_ja = translate_locked(what, depth + 1);
                if (what_ja == what) {
                    // 「Restore Intellige」のように切れていたら、それで始まる名前が 1 つだけのとき補う。
                    std::string whole;
                    if (!unique_prefix(what, g_exact, whole)) return false;
                    what_ja = translate_locked(whole, depth + 1);
                    if (what_ja == whole) return false;
                }
                ja = what_ja + "\xE3\x81\xAE" + kind.ja; // の
                counter = kind.counter;
                return true;
            }
            // 「Blue Potion」→「青い薬」
            const std::string tail = std::string(" ") + word;
            if (ends_with(core, tail)) {
                const std::string flavor = flavor_word(kind.flavor_key, core.substr(0, core.size() - tail.size()));
                if (flavor.empty()) return false;
                ja = flavor + kind.ja;
                counter = kind.counter;
                return true;
            }
            if (core == word) {
                ja = kind.ja;
                counter = kind.counter;
                return true;
            }
        }
    }
    // 本体の名前が切れている（「Dagger (Stilett」）。品物の表で、その字で始まる名前が 1 つだけなら補う。
    if (std::string whole; unique_prefix(core, g_items, whole)) {
        ja = g_items[whole].ja;
        counter = g_items[whole].counter;
        return true;
    }
    // 「Hairy Grey Mold」（毛の生えたカビ。キノコの見た目の一種）
    for (const char *head : { "Hairy " }) {
        for (const char *word : { " Mold", " Molds" }) {
            if (starts_with(core, head) && ends_with(core, word)) {
                const std::string flavor = flavor_word("mushroom", core.substr(std::strlen(head), core.size() - std::strlen(head) - std::strlen(word)));
                if (flavor.empty()) return false;
                ja = "\xE6\xAF\x9B\xE3\x81\xAE\xE7\x94\x9F\xE3\x81\x88\xE3\x81\x9F" + flavor + "\xE3\x82\xAB\xE3\x83\x93"; // 毛の生えた〜カビ
                return true;
            }
        }
    }
    return false;
}

//! 付け足しの括弧の中身を訳す。数だけの物（`1d4` `+1,+2` `x3`）はそのまま。訳せない物は空（エゴかもしれない）。
std::string translate_item_suffix(const std::string &inner)
{
    if (ends_with(inner, " charges")) {
        return "(" + inner.substr(0, inner.size() - std::strlen(" charges")) + "\xE5\x9B\x9E\xE5\x88\x86)"; // 回分
    }
    for (const auto &[en, ja] : { std::pair<const char *, const char *>{ " to STR", "\xE8\x85\x95\xE5\x8A\x9B" },   // 腕力
              std::pair<const char *, const char *>{ " to stealth", "\xE9\x9A\xA0\xE5\xAF\x86" } }) {            // 隠密
        if (ends_with(inner, en)) return "(" + std::string(ja) + inner.substr(0, inner.size() - std::strlen(en)) + ")";
    }
    if (!inner.empty() && inner.find_first_not_of("0123456789d+-,x ") == std::string::npos) return "(" + inner + ")";
    return std::string();
}

bool translate_item(const std::string &en, int depth, std::string &out)
{
    {
        // 後ろが閉じていない括弧（「(14」「[6,+0」「(+0」。欄の幅で切れた付け足し）なら、そこから後ろを切り離して残りの名前全体を
        // 訳し、切り離した字はそのまま後ろへ付ける（「a Dagger (Stiletto) (1d4) (+0」。2026-10-04）。
        const std::size_t open = en.find_last_of("([");
        if (open != std::string::npos && open > 0 && en[open - 1] == ' ' && en.find(en[open] == '(' ? ')' : ']', open) == std::string::npos) {
            std::string head;
            if (translate_item(en.substr(0, open - 1), depth, head)) {
                // 切れた字に英字が残る（「(7 ch」＝ charges の途中）なら、英字の手前で切って「…」を付ける。
                std::string tail = en.substr(open);
                const std::size_t letter = tail.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
                if (letter != std::string::npos) {
                    tail.resize(letter);
                    while (!tail.empty() && tail.back() == ' ') tail.pop_back();
                    tail += "\xE2\x80\xA6"; // …
                }
                out = head + " " + tail;
                return true;
            }
            // 切り離すと訳せない（魔法書の題「[Magic」は名前の一部）。切り離さずに下の手順で訳す。
        }
    }
    std::string rest = en;
    // 一覧では名前の後ろに「.」が付く（原作の itemDescription の呼び手が足す）。日本語の一覧では付けない。
    if (ends_with(rest, ".") && !ends_with(rest, "..")) rest.pop_back();
    long count = 1;
    bool none = false;
    if (starts_with(rest, "no more ")) {
        none = true;
        rest = rest.substr(8);
    } else if (!rest.empty() && std::isdigit(static_cast<unsigned char>(rest[0]))) {
        std::size_t at = 0;
        while (at < rest.size() && std::isdigit(static_cast<unsigned char>(rest[at]))) ++at;
        if (at >= rest.size() || rest[at] != ' ') return false;
        count = std::atol(rest.substr(0, at).c_str());
        rest = rest.substr(at + 1);
    } else {
        for (const char *article : { "a ", "an ", "some ", "A ", "An ", "Some " }) {
            if (starts_with(rest, article)) {
                rest = rest.substr(std::strlen(article));
                break;
            }
        }
    }
    // 付け足しを後ろから外す（原作の並びに戻すため、外した順の逆に並べる）。
    std::vector<std::string> suffixes;
    for (;;) {
        if (ends_with(rest, "}")) {
            const std::size_t at = rest.rfind(" {");
            if (at == std::string::npos) break;
            suffixes.push_back(rest.substr(at + 1));
            rest = rest.substr(0, at);
        } else if (ends_with(rest, " turns of light") && rest.rfind(" with ") != std::string::npos) {
            const std::size_t at = rest.rfind(" with ");
            const std::string turns = rest.substr(at + 6, rest.size() - at - 6 - std::strlen(" turns of light"));
            suffixes.push_back("(" + turns + "\xE3\x82\xBF\xE3\x83\xBC\xE3\x83\xB3\xE5\x88\x86\xE3\x81\xAE\xE6\x98\x8E\xE3\x81\x8B\xE3\x82\x8A)"); // ターン分の明かり
            rest = rest.substr(0, at);
        } else if (ends_with(rest, "]")) {
            const std::size_t at = rest.rfind(" [");
            if (at == std::string::npos) break;
            // 防具の「[4,+0]」だけを付け足しとして外す。魔法書の題「[Beginners-Magick]」は本体の一部
            // （外していたので「Book of Magic Spells」が残って魔法書の名前が訳せなかった。2026-10-04）。
            const std::string inner = rest.substr(at + 2, rest.size() - at - 3);
            if (inner.empty() || inner.find_first_not_of("0123456789+-, ") != std::string::npos) break;
            suffixes.push_back(rest.substr(at + 1));
            rest = rest.substr(0, at);
        } else if (ends_with(rest, ")")) {
            const std::size_t at = rest.rfind(" (");
            if (at == std::string::npos) break;
            const std::string ja = translate_item_suffix(rest.substr(at + 2, rest.size() - at - 3));
            if (ja.empty()) break; // エゴや箱の状態（下で見る）
            suffixes.push_back(ja);
            rest = rest.substr(0, at);
        } else {
            break;
        }
    }
    std::string ego_ja;
    for (const auto &[ego_en, ja] : g_egos) {
        if (ends_with(rest, " " + ego_en)) {
            ego_ja = ja;
            rest = rest.substr(0, rest.size() - ego_en.size() - 1);
            break;
        }
    }
    std::string core_ja;
    std::string counter;
    if (!translate_item_core(rest, depth, core_ja, counter)) return false;
    std::string name = core_ja;
    if (!ego_ja.empty()) name = (ego_ja[0] == '(') ? name + ego_ja : ego_ja + name;
    if (none) {
        name += "(\xE3\x82\x82\xE3\x81\x86\xE7\x84\xA1\xE3\x81\x84)"; // もう無い
    } else if (count != 1) {
        if (counter.empty()) counter = count < 10 ? "\xE3\x81\xA4" : "\xE5\x80\x8B"; // つ・個
        name = std::to_string(count) + counter + "\xE3\x81\xAE" + name;              // の
    }
    for (auto it = suffixes.rbegin(); it != suffixes.rend(); ++it) name += " " + *it;
    out = name;
    return true;
}

/*
 * 得点表の 1 行（原作 scores.cpp: "%-4d%8d %-19.19s %c %-10.10s %-7.7s%3d %-22.22s"）。
 * 桁の幅が決まっているので、名前に空白が入っていても桁で切れば欄が分かる。種族・職業・死因は表で引き、性別は男／女にする。
 * 得点表の行でなければ偽。
 */
int visual_width(const std::string &text);

bool translate_score_row(const std::string &en, int depth, std::string &out)
{
    if (en.size() < 57) return false;
    const auto digits = [&](std::size_t from, std::size_t len) {
        bool any = false;
        for (std::size_t i = from; i < from + len && i < en.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(en[i]))) any = true;
            else if (en[i] != ' ') return false;
        }
        return any;
    };
    if (!digits(0, 4) || !digits(4, 8) || en[12] != ' ' || en[32] != ' ' || en[34] != ' ' || en[45] != ' ' || !digits(53, 3)) return false;
    const char sex = en[33];
    if (sex != 'M' && sex != 'F') return false;
    const auto field = [&](std::size_t from, std::size_t len) {
        std::string text = en.substr(from, std::min(len, en.size() - from));
        const std::size_t first = text.find_first_not_of(' ');
        const std::size_t last = text.find_last_not_of(' ');
        return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
    };
    const std::string died = en.size() > 57 ? field(57, 22) : std::string();
    // 原作の欄の幅に、画面の桁（全角 2 桁）でそろえる（見出しの行と桁を合わせるため）。
    const auto pad = [](std::string text, int width) {
        for (int w = visual_width(text); w < width; ++w) text.push_back(' ');
        return text;
    };
    out = en.substr(0, 13) + pad(field(13, 19), 19) + " " + pad(sex == 'M' ? "\xE7\x94\xB7" : "\xE5\xA5\xB3", 2) // 男・女
        + " " + pad(translate_locked(field(35, 10), depth + 1), 10) + " " + pad(translate_locked(field(46, 7), depth + 1), 7)
        + en.substr(53, 3) + (died.empty() ? std::string() : " " + translate_locked(died, depth + 1));
    return true;
}

std::string translate_locked(const std::string &en, int depth)
{
    if (en.empty() || depth > 4) return en;
    if (const auto dynamic = g_dynamic.find(en); dynamic != g_dynamic.end()) return dynamic->second;
    const auto cached = g_cache.find(en);
    if (cached != g_cache.end()) {
        // 試しの段で訳せなかった字は記録を飛ばしているので、本当に使うときに記録する。
        if (g_probing == 0 && cached->second == en && has_letters(en) && g_misses.insert(en).second) g_misses_dirty = true;
        return cached->second;
    }
    std::string result = en;
    bool hit = false;
    //! 当たったが中の字が全部は訳せなかった書式のうち最初のもの。欄ごとの訳でも訳せないときに使う（2026-10-04）。
    const Pattern *first = nullptr;
    std::vector<std::string> first_values;
    const auto exact = g_exact.find(en);
    if (exact != g_exact.end()) {
        result = exact->second;
        hit = true;
    } else {
        /*
         * 当たった書式のうち、中の字（%s）が全部訳せるものを使う。どれも訳せなければ最初に当たったもの。
         * 表の順で最初に当たったものを使っていたので、前に並ぶ広い形「You have %s」（持っているのは{1}）が
         * 後ろの「You have slain %s.」（あなたは{1}を倒した。）より先に当たり、「持っているのはslain the Kobold.」と出た
         *
         */
        first = nullptr;
        for (const Pattern &pattern : g_patterns) {
            std::vector<std::string> values;
            if (!match_tokens(pattern.tokens, 0, en, 0, values)) continue;
            if (first == nullptr) {
                first = &pattern;
                first_values = values;
            }
            if (pattern_values_translate(pattern, values, depth)) {
                result = apply_pattern(pattern, values, depth);
                hit = true;
                break;
            }
        }
    }
    if (!hit && !g_player_class.empty()) {
        // 称号（状態の列・人物表）。いまの職業で引く。
        const auto title = g_titles.find(g_player_class + ":" + en);
        if (title != g_titles.end()) {
            result = title->second;
            hit = true;
        }
    }
    if (!hit) {
        std::string row;
        if (translate_score_row(en, depth, row)) {
            result = row;
            hit = true;
        }
    }
    if (!hit) {
        std::string item;
        if (translate_item(en, depth, item)) {
            result = item;
            hit = true;
        }
    }
    if (!hit) {
        /*
         * 名前の前の冠詞（`The Kobold` `a Dagger` `an Arrow` `some Food`）。日本語には付けないので、外して名前の表を引く。
         * 原作は怪物の名前を "The " + 名前 で組み（monster.cpp の monsterNameDescription）、アイテムは a/an/some を付ける。
         */
        for (const char *article : { "The ", "the ", "A ", "a ", "An ", "an ", "Some ", "some " }) {
            const std::size_t len = std::strlen(article);
            if (en.size() > len && en.compare(0, len, article) == 0) {
                const std::string rest = en.substr(len);
                const std::string translated = translate_locked(rest, depth + 1);
                if (translated != rest) {
                    result = translated;
                    hit = true;
                }
                break;
            }
        }
    }
    if (!hit && depth < 4) {
        /*
         * 桁をそろえて 1 行に組んだ字（呪文の一覧「Magic Missile    1    1  27% unknown」・装備の一覧「About body    : a Cloak」・
         * オプション「Rogue like commands    : no」・店の行）。行のままでは表に無いので、空白 2 つ以上で欄に分けて欄ごとに訳し、
         * 元の欄の頭の桁にそろえて並べ直す（訳で広がったら空白 1 つで続ける）。どの欄も訳せなければ当たらなかったことにする。
         */
        std::vector<std::pair<std::size_t, std::string>> cells;
        std::size_t at = 0;
        while (at < en.size()) {
            const std::size_t gap = en.find("  ", at);
            const std::size_t end = gap == std::string::npos ? en.size() : gap;
            if (end > at) cells.emplace_back(at, en.substr(at, end - at));
            if (gap == std::string::npos) break;
            at = en.find_first_not_of(' ', gap);
            if (at == std::string::npos) break;
        }
        if (cells.size() >= 2) {
            /*
             * 前から、つながった欄をなるべく長いまとまりで引く（空白 2 つを含む 1 つの文がある。店主の台詞「Sigh.  I'll take it.」）。
             * 行全体（全部の欄）はここで引き直さない（いま引いて当たらなかったもの）。
             */
            std::string out;
            bool changed = false;
            for (std::size_t i = 0; i < cells.size();) {
                std::size_t j = cells.size() - (i == 0 ? 1 : 0);
                std::string ja;
                for (; j > i; --j) {
                    const std::size_t from = cells[i].first;
                    const std::size_t to = cells[j - 1].first + cells[j - 1].second.size();
                    const std::string piece = en.substr(from, to - from);
                    ja = translate_locked(piece, depth + 1);
                    if (j == i + 1) break;
                    if (ja != piece && fully_translated_text(ja)) break;
                }
                if (j == i) j = i + 1;
                const std::size_t to = cells[j - 1].first + cells[j - 1].second.size();
                changed = changed || ja != en.substr(cells[i].first, to - cells[i].first);
                const int pad = static_cast<int>(cells[i].first) - visual_width(out);
                out.append(static_cast<std::size_t>(out.empty() ? std::max(0, pad) : std::max(1, pad)), ' ');
                out += ja;
                i = j;
            }
            if (changed && (fully_translated_text(out) || first == nullptr)) {
                result = out;
                hit = true;
            }
        }
    }
    if (!hit && first != nullptr) {
        // 中の字が全部は訳せない書式しか当たらず、欄ごとにも訳せなかった。最初に当たった書式で訳す（前と同じ）。
        result = apply_pattern(*first, first_values, depth);
        hit = true;
    }
    if (!hit && en.size() >= 8 && en.find(' ') != std::string::npos) {
        // 店主の名前が欄の幅で切れている（「Inglorian the」）。その字で始まる店主が 1 人だけなら補う。
        int found = 0;
        for (const auto &[name, ja] : g_owner_names) {
            if (name.size() > en.size() && name.compare(0, en.size(), en) == 0) {
                result = ja;
                ++found;
            }
        }
        if (found == 1) {
            hit = true;
        } else {
            result = en;
        }
    }
    if (!hit && g_probing == 0 && has_letters(en) && g_misses.insert(en).second) g_misses_dirty = true;
    g_cache.emplace(en, result);
    return result;
}

//! 端末の字。表示できない字は '?'（以前の fill_term_lines と同じ）。
char printable(char c)
{
    const unsigned char ch = static_cast<unsigned char>(c);
    return (ch >= 0x20 && ch < 0x7f) ? c : '?';
}

/*
 * 端末のマスは 1 バイトなので、原作がファイルのパス（「Save file '…' present.」など）を書くと、日本語のフォルダ名は
 * バイトごとに別のマスへ入る。そのまま「?」にすると化けるので、0x80 以上の並びを 1 字にして UTF-8 へ直す
 * （Rogue 系の `rl_decode_ansi_char` と同じ事情。Windows の原作は ANSI（日本語版では CP932）のパスを持つ。
 * Linux・Android のパスは UTF-8）。
 * @return 使ったマスの数（0 ならこのマスは 0x80 以上の字ではない）。`utf8` に直した字を入れる。
 */
int high_char_at(int row, int col, std::string &utf8)
{
    const unsigned char lead = static_cast<unsigned char>(umoria::term_char(row, col));
    if (lead < 0x80) return 0;
    const auto cell = [&](int offset) {
        return col + offset < umoria::kTermCols ? static_cast<unsigned char>(umoria::term_char(row, col + offset)) : 0;
    };
#if defined(_WIN32)
    {
        const int length = (IsDBCSLeadByteEx(CP_ACP, lead) && cell(1) != 0) ? 2 : 1;
        const char bytes[2] = { static_cast<char>(lead), static_cast<char>(cell(1)) };
        wchar_t wide[2] = {};
        if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, bytes, length, wide, 2) == 1) {
            char out[8] = {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, wide, 1, out, sizeof(out), nullptr, nullptr);
            if (n > 0) {
                utf8.assign(out, static_cast<std::size_t>(n));
                return length;
            }
        }
    }
#endif
    const int need = (lead >= 0xC2 && lead < 0xE0) ? 2 : (lead >= 0xE0 && lead < 0xF0) ? 3 : (lead >= 0xF0 && lead < 0xF5) ? 4 : 0;
    if (need == 0 || col + need > umoria::kTermCols) return 0;
    std::string bytes(1, static_cast<char>(lead));
    for (int i = 1; i < need; ++i) {
        if ((cell(i) & 0xC0) != 0x80) return 0;
        bytes.push_back(static_cast<char>(cell(i)));
    }
    utf8 = std::move(bytes);
    return need;
}

//! 画面での幅（全角は 2）。UTF-8 の先頭バイトで見分ける（日本語の字は 3 バイト）。
int visual_width(const std::string &text)
{
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++width;
            ++i;
        } else if (c >= 0xE0) {
            width += 2;
            i += (c >= 0xF0) ? 4 : 3;
        } else {
            ++width;
            i += 2;
        }
    }
    return width;
}

} // namespace

namespace umoria {

void set_language(const std::string &lang) { g_ja.store(lang == "ja"); }

bool history_fragment(int chart, const std::string &en, std::string &ja, int &ja_chart)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ensure_loaded();
    const auto it = g_history.find(std::to_string(chart) + "\t" + en);
    if (it == g_history.end()) return false;
    ja = it->second.first;
    ja_chart = it->second.second;
    return true;
}

void set_screen_translation(const std::string &en, const std::string &ja)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dynamic[en] = ja;
}

void clear_screen_translations()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dynamic.clear();
}

void remove_screen_translation(const std::string &en)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dynamic.erase(en);
}

void set_player_class(const std::string &class_name)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_player_class == class_name) return;
    g_player_class = class_name;
    g_cache.clear(); // 称号の訳は職業で変わるので、覚えた訳を捨てる
}
bool lang_ja() { return g_ja.load(); }

bool fully_translated(const std::string &text) { return fully_translated_text(text); }

std::vector<std::pair<char, std::string>> symbol_descriptions()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ensure_loaded();
    // 訳の表の鍵のうち「a - Giant ant.」の形（原作 identification.cpp の objectDescription の字）を記号ごとに 1 つ。
    std::map<char, std::string> found;
    for (const auto &[en, ja] : g_exact) {
        if (en.size() >= 5 && en[1] == ' ' && en[2] == '-' && en[3] == ' ' && en[0] > ' ' && en[0] < 0x7f) found.emplace(en[0], en);
    }
    return { found.begin(), found.end() };
}

std::string tr(const std::string &english)
{
    if (!g_ja.load()) return english;
    std::lock_guard<std::mutex> lock(g_mutex);
    ensure_loaded();
    // 前後の空白は訳さずに残す（原作は桁をそろえるために空白で埋める）。
    const std::size_t first = english.find_first_not_of(' ');
    if (first == std::string::npos) return english;
    const std::size_t last = english.find_last_not_of(' ');
    const std::string core = english.substr(first, last - first + 1);
    return english.substr(0, first) + translate_locked(core, 0) + english.substr(last + 1);
}

TermLineText term_line(int row, int col_begin, int col_end)
{
    TermLineText out;
    int last = col_begin - 1;
    for (int col = col_begin; col < col_end; ++col) {
        if (term_char(row, col) != ' ') last = col;
    }
    out.byte_at_col.assign(static_cast<std::size_t>(col_end - col_begin + 1), 0);
    const bool ja = g_ja.load();
    /*
     * 訳で幅が広がったときに、後ろへはみ出した桁の数。後に続く空白のマスで吸収して、次のかたまりの桁を保つ
     * （状態の列の「Priest」が「プリースト」になり、その行の地図が 4 マス右へずれた。2026-10-04）。
     */
    int overflow = 0;
    int col = col_begin;
    while (col <= last) {
        const std::uint32_t run = term_run(row, col);
        int end = col + 1;
        if (ja && run != 0) {
            while (end <= last && term_run(row, end) == run) ++end;
        }
        if (end - col == 1) {
            std::string wide;
            if (const int used = high_char_at(row, col, wide); used > 0) {
                for (int c = col; c < col + used && c <= col_end; ++c) {
                    out.byte_at_col[static_cast<std::size_t>(c - col_begin)] = static_cast<int>(out.utf8.size());
                }
                out.utf8 += wide;
                overflow = 0;
                col += used;
                continue;
            }
            out.byte_at_col[static_cast<std::size_t>(col - col_begin)] = static_cast<int>(out.utf8.size());
            if (overflow > 0 && term_char(row, col) == ' ') {
                --overflow;
            } else {
                overflow = 0;
                out.utf8.push_back(printable(term_char(row, col)));
            }
            col = end;
            continue;
        }
        // かたまりは訳した字を頭の桁から置く。訳で幅が縮んだら空白で埋め、次のかたまりの桁を保つ。
        // 訳で幅が広がったら、訳の後ろの空白（原作が桁をそろえるために埋めた分）を削ってかたまりの幅に収め、
        // それでも広い分は後に続く空白のマスで吸収する（overflow）。
        std::string english;
        std::vector<int> offset(static_cast<std::size_t>(end - col) + 1, 0); // 桁 → english のバイト位置
        for (int c = col; c < end;) {
            std::string wide;
            const int used = std::min(high_char_at(row, c, wide), end - c);
            offset[static_cast<std::size_t>(c - col)] = static_cast<int>(english.size());
            if (used > 0) {
                for (int k = 1; k < used; ++k) offset[static_cast<std::size_t>(c - col + k)] = static_cast<int>(english.size());
                english += wide;
                c += used;
            } else {
                english.push_back(printable(term_char(row, c)));
                ++c;
            }
        }
        offset[static_cast<std::size_t>(end - col)] = static_cast<int>(english.size());
        std::string text = tr(english);
        /*
         * 最下行の「Save file '…' present. Attempting restore.」（原作 `loadGame`。セーブの読み込み中の表示）。
         * 80 桁で切れて末尾が残らないことが多く、表の「Save file '%s」（保存に失敗しました）が当たって
         * 読み込み中に「保存に失敗」と出ていた。保存の失敗は 1 行目のメッセージに出るので、最下行なら読み込みの案内にする。
         */
        if (ja && row == umoria::kTermRows - 1 && english.rfind("Save file '", 0) == 0) {
            std::string path = english.substr(std::strlen("Save file '"));
            const std::string tail = "' present. Attempting restore.";
            if (path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0) {
                path.erase(path.size() - tail.size());
            }
            text = "セーブファイル '" + path + "' を読み込んでいます。";
        }
        const int start = static_cast<int>(out.utf8.size());
        for (int c = col; c < end; ++c) out.byte_at_col[static_cast<std::size_t>(c - col_begin)] = start;
        if (text == english) {
            // 前のかたまりのはみ出しは、このかたまりの頭の空白でも吸収する（原作が先に空白で桁を埋め、その上に字を書いた所）。
            int skip = 0;
            while (overflow > 0 && col + skip < end && term_char(row, col + skip) == ' ') {
                ++skip;
                --overflow;
            }
            const int skipped_bytes = offset[static_cast<std::size_t>(skip)];
            for (int c = col; c < end; ++c) {
                const int at = offset[static_cast<std::size_t>(c - col)] - skipped_bytes;
                out.byte_at_col[static_cast<std::size_t>(c - col_begin)] = start + (c - col > skip ? at : 0);
            }
            out.utf8 += english.substr(static_cast<std::size_t>(skipped_bytes));
            overflow = 0;
        } else {
            std::string placed = text;
            while (visual_width(placed) > end - col && !placed.empty() && placed.back() == ' ') placed.pop_back();
            out.utf8 += placed;
            const int pad = (end - col) - visual_width(placed);
            if (pad > 0 && end <= last) out.utf8.append(static_cast<std::size_t>(pad), ' ');
            overflow = pad < 0 ? -pad : 0;
        }
        col = end;
    }
    for (int c = last + 1; c <= col_end; ++c) {
        if (c - col_begin >= 0) out.byte_at_col[static_cast<std::size_t>(c - col_begin)] = static_cast<int>(out.utf8.size());
    }
    return out;
}

void write_misses()
{
    const char *path = std::getenv("UMORIA_JA_MISSES");
    if (path == nullptr || path[0] == '\0') return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_misses_dirty) return;
    g_misses_dirty = false;
    if (FILE *fp = std::fopen(path, "wb"); fp != nullptr) {
        for (const std::string &miss : g_misses) {
            std::fputs(miss.c_str(), fp);
            std::fputc('\n', fp);
        }
        std::fclose(fp);
    }
}

} // namespace umoria
