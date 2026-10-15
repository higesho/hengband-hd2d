/*!
 * @file an_lang.cpp
 * @brief 日本語層のカタログ。`an_lang_c.h` の実体。
 *
 * @details 書式は Frox（`frox/lang/ja/ui/messages.ja.txt` の冒頭）と同じ。
 * `angband/lang/ja/ui/messages.ja.txt` を読み、札ごとに別の表へ入れる（同じ字が別の
 * 意味を持つので混ぜない。設計 §5）。名前は `angband/lang/ja/names/<kind>.ja.tsv`
 * （英語 \t 日本語）。
 *
 * 読み終えた表は**読むだけ**（スレッドをまたいで引いても書き換えは無い）。
 * `an_tr_term()` の作業場所だけはゲームのスレッド専用（Term はゲームのスレッドしか
 * 書かない）。
 */

#include "an_lang.h"

#include "an_lang_c.h"
#include "an_wide.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

//! 原作の z-form.c（`an::selftest_death_causes()` が書式の訳を通して確かめる。`extern "C"` は無名名前空間の外に置く）。
extern "C" size_t strnfmt(char *buf, size_t max, const char *fmt, ...);

namespace {

std::atomic<bool> g_enabled{ false };
/*!
 * @brief 読み物（`textblock`）を組んでいる最中か。`textblock_vappend_c()` がその中の
 * `vstrnfmt()` の前後で上げ下げする（フック #17）。読み物の字は画へ出るだけなので、
 * 1 語の引数（属性の名 `fire`・`cold` ほか）も引いてよい。
 */
std::atomic<int> g_display_depth{ 0 };
std::string g_code = "en";

//! 札（`E:` `A:` ほか）ごとの表。鍵は英語の原文そのもの（正規化しない）。
std::unordered_map<std::string, std::string> g_tables[128];
//! `R:` の句。長い句から順に当てる。
std::vector<std::pair<std::string, std::string>> g_phrases;
//! 名前の表。`kind` → 英語名 → 日本語名。
std::map<std::string, std::unordered_map<std::string, std::string>> g_names;
//! 書式 → 引数を先に引く札（訳文の頭の `{X}`）。
std::unordered_map<std::string, char> g_arg_pref;
//! いま組んでいる書式の `{X}`（`an_tr_fmt()` が立て、同じ `vstrnfmt()` の `an_tr_arg()` が読む）。
//! ゲームのスレッドだけが書式を組む（`vstrnfmt()` の中で別の `vstrnfmt()` は呼ばれない）。
char g_cur_pref = 0;
//! 直前の変換指定の精度が `*` で渡ったか（フック #22。an_fmt_s() が読んで下ろす）。
bool g_star_precision = false;

//! 怪物の日本語名 → 英語名（`an_score_how()` が殿堂の記録へ英語の名を書くための逆引き。初めて引くときに作る）。
std::unordered_map<std::string, std::string> g_monster_rev;
bool g_monster_rev_built = false;

std::string g_report_path;
std::mutex g_report_mutex;
std::set<std::string> g_reported;

bool is_ascii(std::string_view s)
{
    for (const char c : s) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            return false;
        }
    }
    return true;
}

bool has_letter(std::string_view s)
{
    for (const char c : s) {
        if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'))) {
            return true;
        }
    }
    return false;
}

std::string unescape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((s[i] == '\\') && (i + 1 < s.size())) {
            const char n = s[i + 1];
            if (n == 's') {
                out += ' ';
                ++i;
                continue;
            }
            if (n == 'n') {
                out += '\n';
                ++i;
                continue;
            }
            if (n == 't') {
                out += '\t';
                ++i;
                continue;
            }
            if (n == '\\') {
                out += '\\';
                ++i;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

/*!
 * @brief 変換指定の並び（`vstrnfmt()` が引数を読む順）。精度・幅・旗は比べない
 * （日本語で `%.0s` にして引数を読み捨てるのは許す）。`*` は int を 1 つ読むので数える。
 */
std::string conversion_signature(const std::string &s)
{
    std::string sig;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') {
            continue;
        }
        ++i;
        if (i >= s.size()) {
            break;
        }
        if (s[i] == '%') {
            continue;
        }
        bool is_long = false;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '*') {
                sig += '*';
                ++i;
                continue;
            }
            if (c == 'l') {
                is_long = true;
                ++i;
                continue;
            }
            if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'))) {
                if (is_long) {
                    sig += 'l';
                }
                sig += c;
                break;
            }
            ++i;
        }
    }
    return sig;
}

void load_catalog(const std::string &path, an::LangLoadReport &report)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        report.errors.push_back("missing catalog: " + path);
        return;
    }
    std::string line;
    char tag = 0;
    std::string key;
    bool have_key = false;
    int line_no = 0;
    auto flush_pair = [&](const std::string &ja) {
        if (!have_key) {
            return;
        }
        const std::string en = unescape(key);
        const std::string jp = unescape(ja);
        if (conversion_signature(en) != conversion_signature(jp)) {
            report.errors.push_back("line " + std::to_string(line_no) + ": conversion specs differ: " + en);
        } else if (tag == 'R') {
            if (!en.empty()) {
                g_phrases.emplace_back(en, jp);
                report.entries++;
            }
        } else {
            // 訳文の頭の `{X}`（X は札の字）は「この書式の引数は X: を先に引く」の印（設計 §16.3）。
            // `You are hit by %s!` の `fire` は A: では動詞（撃つ）、M: では属性（火炎）。
            std::string body = jp;
            if ((tag == 'E') && (body.size() >= 3) && (body[0] == '{') && (body[2] == '}')) {
                g_arg_pref[en] = body[1];
                body.erase(0, 3);
            }
            g_tables[static_cast<unsigned char>(tag)][en] = body;
            report.entries++;
        }
        have_key = false;
        key.clear();
    };
    while (std::getline(in, line)) {
        ++line_no;
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        if ((line_no == 1) && (line.size() >= 3) && (static_cast<unsigned char>(line[0]) == 0xEF)) {
            line.erase(0, 3); // BOM
        }
        if (line.empty() || (line[0] == '#')) {
            continue;
        }
        if ((line.size() < 2) || (line[1] != ':')) {
            continue;
        }
        const char t = line[0];
        const std::string body = line.substr(2);
        if (t == 'J') {
            flush_pair(body);
            continue;
        }
        if (t == 'W') {
            continue; // 桁の控え（`an_check_ja.py` が読む）。
        }
        if (std::strchr("EAFODSKMRVLQPTH", t) == nullptr) {
            continue;
        }
        if (have_key && (t == tag)) {
            key += body; // 同じ札を続けて書くと連結する（長い鍵を折れる）。
        } else {
            tag = t;
            key = body;
            have_key = true;
        }
    }
    std::stable_sort(g_phrases.begin(), g_phrases.end(),
        [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
}

std::vector<std::string> list_tsv(const std::string &dir)
{
    std::vector<std::string> out;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.ja.tsv").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        out.emplace_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return out;
    }
    while (struct dirent *e = readdir(d)) {
        const std::string n = e->d_name;
        if ((n.size() > 7) && (n.compare(n.size() - 7, 7, ".ja.tsv") == 0)) {
            out.push_back(n);
        }
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

void load_names(const std::string &dir, an::LangLoadReport &report)
{
#if defined(_WIN32)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    for (const std::string &file : list_tsv(dir)) {
        const std::string kind = file.substr(0, file.size() - 7);
        std::ifstream in(dir + sep + file, std::ios::binary);
        std::string line;
        auto &table = g_names[kind];
        while (std::getline(in, line)) {
            if (!line.empty() && (line.back() == '\r')) {
                line.pop_back();
            }
            if ((line.size() >= 3) && (static_cast<unsigned char>(line[0]) == 0xEF)) {
                line.erase(0, 3);
            }
            if (line.empty() || (line[0] == '#')) {
                continue;
            }
            const std::size_t tab = line.find('\t');
            if ((tab == std::string::npos) || (tab == 0) || (tab + 1 >= line.size())) {
                continue;
            }
            std::string ja = line.substr(tab + 1);
            const std::size_t tab2 = ja.find('\t');
            if (tab2 != std::string::npos) {
                ja.erase(tab2); // 3 列目以降（出所の註記）は読まない。
            }
            table[line.substr(0, tab)] = ja;
            report.names++;
        }
    }
}

void report_untranslated(const std::string &s)
{
    if (g_report_path.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_report_mutex);
    if (!g_reported.insert(s).second) {
        return;
    }
    if (std::FILE *f = std::fopen(g_report_path.c_str(), "ab")) {
        std::string esc;
        for (const char c : s) {
            if (c == '\n') {
                esc += "\\n";
            } else {
                esc += c;
            }
        }
        std::fprintf(f, "E:%s\nJ:\n\n", esc.c_str());
        std::fclose(f);
    }
}

const std::string *find_in(char tag, std::string_view key)
{
    const auto &table = g_tables[static_cast<unsigned char>(tag)];
    if (table.empty()) {
        return nullptr;
    }
    const auto it = table.find(std::string(key));
    if (it == table.end()) {
        return nullptr;
    }
    return &it->second;
}

/*!
 * @brief 殿堂・墓の死因（英語の字）を日本語へ引く（AH-60。`H:` の表と罠の名前の表）。
 * @details 死因は `take_hit()` の最後の引数（`player-util.c` ほか）で、怪物のほかに固定の英語の句
 * （`starvation` `poison` ほか）と罠の名（`a pit`。冠詞 + `trap.txt` の `desc`）がある。**書式の訳文の頭の
 * `{H}` が付いた書式の引数だけ**引く（`an_tr_arg()`）ので、`A:` に足して他の `%s` まで巻き込むことがない。
 * 引く順は (1) `H:` の鍵に完全一致 (2) 殿堂の記録（`how[32]`＝31 バイト）で切れた長い句
 * （31 バイトちょうどで、`H:` の鍵の頭と一致する。名前が 31 バイトを超える怪物 7 体もここで名前の表 `monster` から引く）
 * (3) 罠の名（冠詞を落として名前の表 `trap`）。引けなければ nullptr（切れていない怪物の名などは
 * `an_tr_arg()` の続きが名前の表で引く）。
 */
const std::string *death_cause_ja(std::string_view cause)
{
    if (const std::string *hit = find_in('H', cause)) {
        return hit;
    }
    constexpr std::size_t kHow = 31; // `struct high_score` の `how[32]` から終端を除いた長さ
    if (cause.size() == kHow) {
        for (const auto &entry : g_tables[static_cast<unsigned char>('H')]) {
            if ((entry.first.size() > kHow) && (entry.first.compare(0, kHow, cause) == 0)) {
                return &entry.second;
            }
        }
        // 名前が 31 バイトを超える怪物（`Draugluin, Sire of All Werewolves` ほか 7 体）も、記録では切れる。
        const auto monsters = g_names.find("monster");
        if (monsters != g_names.end()) {
            for (const auto &entry : monsters->second) {
                if ((entry.first.size() > kHow) && (entry.first.compare(0, kHow, cause) == 0)) {
                    return &entry.second;
                }
            }
        }
    }
    const auto traps = g_names.find("trap");
    if (traps != g_names.end()) {
        for (const std::string_view article : { std::string_view("a "), std::string_view("an ") }) {
            if ((cause.size() > article.size()) && (cause.substr(0, article.size()) == article)) {
                const auto it = traps->second.find(std::string(cause.substr(article.size())));
                if (it != traps->second.end()) {
                    return &it->second;
                }
            }
        }
    }
    return nullptr;
}

} // namespace

namespace an {

LangLoadReport lang_init(const std::string &lang, const std::string &dir, const std::string &report_path)
{
    LangLoadReport report;
    g_enabled.store(false);
    g_code = "en";
    for (auto &t : g_tables) {
        t.clear();
    }
    g_phrases.clear();
    g_names.clear();
    g_monster_rev.clear();
    g_monster_rev_built = false;
    g_arg_pref.clear();
    g_cur_pref = 0;
    if (lang != "ja") {
        return report;
    }
#if defined(_WIN32)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    load_catalog(dir + sep + "ui" + sep + "messages.ja.txt", report);
    load_names(dir + sep + "names", report);
    g_report_path = report_path;
    g_code = "ja";
    report.enabled = true;
    g_enabled.store(true);
    return report;
}

const std::string &lang_code()
{
    return g_code;
}

std::string ui_text(const char *en)
{
    return an_tr(en);
}

bool selftest_death_causes()
{
    struct Case {
        const char *fmt;
        const char *cause;
        int depth; // -1 は書式に階が無い
        const char *en;
        const char *ja;
    };
    static const Case kCases[] = {
        { "by %s.", "starvation", -1, "by starvation.", "空腹に殺された。" },
        { "by %s.", "poison", -1, "by poison.", "毒に殺された。" },
        { "by %s.", "a fatal wound", -1, "by a fatal wound.", "致命傷に殺された。" },
        { "by %s.", "an earthquake", -1, "by an earthquake.", "地震に殺された。" },
        { "by %s.", "the strain of casting Banishment", -1, "by the strain of casting Banishment.",
            "消滅の呪文を唱えた疲労に殺された。" },
        { "Killed by %s in the town", "a pit", -1, "Killed by a pit in the town", "死因: 落とし穴（町）" },
        { "Killed by %s in the town", "a spiked pit", -1, "Killed by a spiked pit in the town",
            "死因: 杭のある落とし穴（町）" },
        { "Killed by %s on dungeon level %d", "an aggravation trap", 3,
            "Killed by an aggravation trap on dungeon level 3", "死因: 激怒のトラップ（3階）" },
        // 殿堂の記録（how[32]）は 31 バイトで切れる。切れた英語の句も表の鍵の頭と一致すれば引く。
        { "Killed by %s on dungeon level %d", "the strain of casting Mass Bani", 7,
            "Killed by the strain of casting Mass Bani on dungeon level 7", "死因: 周辺消滅の呪文を唱えた疲労（7階）" },
        { "Killed by %s in the town", "the strain of casting Banishmen", -1,
            "Killed by the strain of casting Banishmen in the town", "死因: 消滅の呪文を唱えた疲労（町）" },
        { "Killed by %s in the town", "Ripe Old Age", -1, "Killed by Ripe Old Age in the town", "死因: 大往生（町）" },
        // 変愚に訳の無い句は英語のまま（新しい訳語を作らない）。
        { "Killed by %s in the town", "over-exertion", -1, "Killed by over-exertion in the town",
            "死因: over-exertion（町）" },
        { "by %s.", "stat drain", -1, "by stat drain.", "stat drainに殺された。" },
    };
    bool ok = true;
    int checked = 0;
    const bool ja_on = an_lang_enabled() != 0;
    for (const Case &c : kCases) {
        char buf[200];
        if (c.depth >= 0) {
            strnfmt(buf, sizeof(buf), c.fmt, c.cause, c.depth);
        } else {
            strnfmt(buf, sizeof(buf), c.fmt, c.cause);
        }
        const char *want = ja_on ? c.ja : c.en;
        ++checked;
        if (std::strcmp(buf, want) != 0) {
            std::fprintf(stderr, "[angband] **FAIL** death causes: \"%s\" got \"%s\" want \"%s\"\n", c.cause, buf, want);
            ok = false;
        }
    }
    {
        // 記録へ書く死因（`an_score_how()`）。英語の句はそのまま（日本語でも）。怪物の日本語名は英語の名へ戻す。
        const char *const kEnglish[] = { "starvation", "poison", "a pit", "the strain of casting Mass Banishment" };
        for (const char *cause : kEnglish) {
            ++checked;
            if (std::strcmp(an_score_how(cause), cause) != 0) {
                std::fprintf(stderr, "[angband] **FAIL** death causes: an_score_how(\"%s\") changed an English cause\n", cause);
                ok = false;
            }
        }
        if (ja_on) {
            const char *const grip = an_name("monster", "Grip, Farmer Maggot's Dog");
            ++checked;
            if ((grip == nullptr) || (std::strcmp(an_score_how(grip), "Grip, Farmer Maggot's Dog") != 0)) {
                std::fprintf(stderr, "[angband] **FAIL** death causes: an_score_how() did not map a monster name back to English\n");
                ok = false;
            }
        }
    }
    {
        // 名前が 31 バイトを超える怪物は記録（how[32]）で切れる。切れた英語の名でも日本語の名で出す。
        const char *const kLong = "Draugluin, Sire of All Werewolves"; // 33 バイト
        const std::string truncated(kLong, 31);
        const char *const ja_name = an_name("monster", kLong);
        char buf[200];
        strnfmt(buf, sizeof(buf), "Killed by %s in the town", truncated.c_str());
        const std::string want = (ja_on && (ja_name != nullptr)) ? (std::string("死因: ") + ja_name + "（町）")
                                                                  : (std::string("Killed by ") + truncated + " in the town");
        ++checked;
        if (want != buf) {
            std::fprintf(stderr, "[angband] **FAIL** death causes: a long monster name cut at 31 bytes: got \"%s\" want \"%s\"\n", buf,
                want.c_str());
            ok = false;
        }
    }
    std::fprintf(stderr, "[angband] death causes: %s (%d check(s), lang=%s)\n", ok ? "OK" : "FAIL", checked,
        lang_code().c_str());
    std::fflush(stderr);
    return ok;
}

bool selftest_term_translations()
{
    struct Case {
        const char *en;     // Term へ書かれる字
        const char *ja;     // 日本語層が起きているときの訳の頭（末尾は桁をそろえる空白）
    };
    static const Case kCases[] = {
        // 鍵が末尾の空白を持つ字（書式を通らず Term へ直に来る）。
        { "Hit any key to see more information: ", "何かキーを押すと詳しい情報を見る:" },
        // 前後を落とした字で引ける鍵（末尾の空白付きの鍵と両方ある 6 件のうち 2 件）は従来どおり前者を引く。
        { "Find: ", nullptr },
        { "Drop which item? ", nullptr },
        { "Press Return (or Escape).", "Enter か Esc を押してください。" },
        // 店の窓の見出し行（店の名＋手持ちの金）。
        { "General Store (5000)", "雑貨屋 (5000)" },
        { "Black Market (12)", "ブラック・マーケット (12)" },
        // 変身中の称号欄（`my_strcap()` で頭が大文字）。
        { "Fox         ", "狐" },
        { "Pukel-man", "プーケル人" },
        { "Bat", "コウモリ" },
        // 一般の表には出さない語（`Home` は店専用の表だけ）。
        { "Home", "Home" },
        { "General Store", "General Store" },
        { "Fox", "狐" },
    };
    bool ok = true;
    int checked = 0;
    const bool ja_on = an_lang_enabled() != 0;
    for (const Case &c : kCases) {
        const char *got = an_tr_term(c.en);
        ++checked;
        if (!ja_on) {
            if (got != c.en) {
                std::fprintf(stderr, "[angband] **FAIL** term translations: \"%s\" changed with the Japanese layer off\n", c.en);
                ok = false;
            }
            continue;
        }
        if (c.ja == nullptr) {
            // 従来の引き（前後を落とした鍵）と同じ訳になること。
            std::string core(c.en);
            while (!core.empty() && (core.back() == ' ')) {
                core.pop_back();
            }
            const std::string *e = find_in('E', core);
            if ((e == nullptr) || (std::strncmp(got, e->c_str(), e->size()) != 0)) {
                std::fprintf(stderr, "[angband] **FAIL** term translations: \"%s\" no longer uses the stripped key (got \"%s\")\n", c.en, got);
                ok = false;
            }
            continue;
        }
        if (std::strncmp(got, c.ja, std::strlen(c.ja)) != 0) {
            std::fprintf(stderr, "[angband] **FAIL** term translations: \"%s\" got \"%s\" want \"%s...\"\n", c.en, got, c.ja);
            ok = false;
        }
    }
    {
        // セーブの選択の説明（`wr_description()` が英語でセーブに書く。表示のときに引数を読み替える）。
        struct Desc {
            const char *en;
            const char *ja; // 日本語層が起きているときの訳。同じなら英語のまま
        };
        static const Desc kDescs[] = {
            { "FxDruid, L15 Human Druid, at DL0", "FxDruid, L15 人間 ドルイド, 町" },
            { "Bob, L3 Half-Elf Mage, at DL12", "Bob, L3 ハーフエルフ メイジ, 12階" },
            { "A, B, L1 High-Elf Warrior, at DL05", "A, B, L1 ハイエルフ 戦士, 5階" },
            { "Bob, dead (a Cave spider)", "Bob, dead (a Cave spider)" },
            { "Bob, L3 Elf Wizard, at DL1", "Bob, L3 Elf Wizard, at DL1" },
            { "Bob, L3 Elf Mage Extra, at DL1", "Bob, L3 Elf Mage Extra, at DL1" },
        };
        for (const Desc &d : kDescs) {
            const char *got = an_tr_arg(d.en, 1);
            ++checked;
            const char *want = ja_on ? d.ja : d.en;
            if (std::strcmp(got, want) != 0) {
                std::fprintf(stderr, "[angband] **FAIL** term translations: save description \"%s\" got \"%s\" want \"%s\"\n", d.en, got, want);
                ok = false;
            }
        }
    }
    std::fprintf(stderr, "[angband] term translations: %s (%d check(s), lang=%s)\n", ok ? "OK" : "FAIL", checked,
        lang_code().c_str());
    std::fflush(stderr);
    return ok;
}

} // namespace an

extern "C" {

int an_lang_enabled(void)
{
    return g_enabled.load(std::memory_order_relaxed) ? 1 : 0;
}

const char *an_tr(const char *en)
{
    if (!en || !an_lang_enabled()) {
        return en;
    }
    if (const std::string *hit = find_in('E', en)) {
        return hit->c_str();
    }
    return en;
}

const char *an_tr_fmt(const char *fmt)
{
    if (!fmt || !an_lang_enabled() || !fmt[0]) {
        return fmt;
    }
    g_cur_pref = 0;
    g_star_precision = false;
    if (const std::string *hit = find_in('E', fmt)) {
        if (!g_arg_pref.empty()) {
            const auto it = g_arg_pref.find(fmt);
            if (it != g_arg_pref.end()) {
                g_cur_pref = it->second;
            }
        }
        return hit->c_str();
    }
    return fmt;
}

namespace {

/*!
 * @brief セーブの選択の行に出る説明（`Use %s: %s` の 2 つ目の `%s`）の訳（修-40）。
 * @details 説明は原作の `wr_description()`（`save.c`）がセーブの中に**英語で**書く
 * （`名前, L15 Human Druid, at DL0`）。書式の訳（`%s を使う: %s`）からは届かないので、表示のときに引数を読み替える。
 * セーブには触らない。形が合い、種族・職業が名前の表（`race_class`）に引けたときだけ訳す
 * （`名前, dead (死因)` の形や、表に無い名は英語のまま）。階は町（0）なら「町」、それ以外は「N階」
 * （死因の `（3階）`・`（町）` と同じ書き方）。
 * @return 訳せたら真（`out` に `名前, L15 人間 ドルイド, 町`）
 */
bool save_description_ja(std::string_view s, std::string &out)
{
    const std::size_t at = s.rfind(", at DL");
    if (at == std::string_view::npos) {
        return false;
    }
    const std::string_view depth = s.substr(at + 7);
    if (depth.empty() || !std::all_of(depth.begin(), depth.end(), [](char c) { return (c >= '0') && (c <= '9'); })) {
        return false;
    }
    const std::string_view head = s.substr(0, at);
    const std::size_t lv = head.rfind(", L");
    if ((lv == std::string_view::npos) || (lv == 0)) {
        return false;
    }
    const std::string_view rest = head.substr(lv + 3); // `15 Human Druid`
    const std::size_t sp1 = rest.find(' ');
    const std::size_t sp2 = (sp1 == std::string_view::npos) ? sp1 : rest.find(' ', sp1 + 1);
    if ((sp1 == std::string_view::npos) || (sp1 == 0) || (sp2 == std::string_view::npos)
        || !std::all_of(rest.begin(), rest.begin() + sp1, [](char c) { return (c >= '0') && (c <= '9'); })
        || (rest.find(' ', sp2 + 1) != std::string_view::npos)) {
        return false;
    }
    const std::string race(rest.substr(sp1 + 1, sp2 - sp1 - 1));
    const std::string klass(rest.substr(sp2 + 1));
    const char *race_ja = an_name("race_class", race.c_str());
    const char *class_ja = an_name("race_class", klass.c_str());
    if (!race_ja || !class_ja) {
        return false;
    }
    out.assign(head.substr(0, lv + 3));
    out.append(rest.substr(0, sp1));
    out.append(" ").append(race_ja).append(" ").append(class_ja).append(", ");
    if (depth.find_first_not_of('0') == std::string_view::npos) {
        out.append("\xE7\x94\xBA"); // 町
    } else {
        out.append(depth.substr(depth.find_first_not_of('0'))).append("\xE9\x9A\x8E"); // N階
    }
    return true;
}

} // namespace

const char *an_tr_arg(const char *s, int fmt_translated)
{
    if (!s || !an_lang_enabled() || !s[0]) {
        return s;
    }
    if (fmt_translated && (std::strstr(s, ", at DL") != nullptr)) {
        // セーブの選択の行の説明（`Use %s: %s` の 2 つ目。ゲームのスレッドだけが呼ぶ。呼び手ごとに別の場所）。
        static std::string desc[4];
        static int desc_slot = 0;
        std::string &d = desc[desc_slot];
        if (save_description_ja(s, d)) {
            desc_slot = (desc_slot + 1) % 4;
            return d.c_str();
        }
    }
    const bool phrase = (std::strchr(s, ' ') != nullptr);
    if (!fmt_translated && !phrase && (g_display_depth.load(std::memory_order_relaxed) <= 0)) {
        return s; // 1 語は書式が引けたとき・読み物の中だけ（an_lang_c.h の註記。`"%s.txt"` の `body`）。
    }
    // 読み物の中で書式が英語のまま（`"%s"` で名を並べる所）は、属性の名（M:）を先に引く。
    // `fire` は A: では動詞（「撃つ」。`Really %s` の引数）、M: では属性（「火炎」）。
    if (fmt_translated && (g_cur_pref != 0)) {
        // 書式の訳文の頭の `{X}`（設計 §16.3）。`{H}`（死因。殿堂と墓）は切れた句・罠の名も引く（AH-60）。
        const std::string *hit = (g_cur_pref == 'H') ? death_cause_ja(s) : find_in(g_cur_pref, s);
        if (hit) {
            return hit->c_str();
        }
    }
    const bool prefer_m = !fmt_translated && (g_display_depth.load(std::memory_order_relaxed) > 0);
    if (prefer_m) {
        if (const std::string *hit = find_in('M', s)) {
            return hit->c_str();
        }
    }
    if (const std::string *hit = find_in('A', s)) {
        return hit->c_str();
    }
    if (const std::string *hit = find_in('M', s)) {
        return hit->c_str();
    }
    // 種族・職業の名（`prt_field(player->race->name, ...)`。設計 §3.3 の P0 の 1 語・§6.3）。
    if (const char *name = an_name("race_class", s)) {
        return name;
    }
    // メッセージの書式が訳せたときは、`%s` に裸で入る名前（罠・地形・呪文・怪物の種別と名）も
    // 名前の表で引く（P2。`You have disarmed the %s.` の罠の名、`You have forgotten the %s of %s.`
    // の呪文の名。設計 §16.3）。名前は gamedata の `name:` そのものが鍵。
    if (fmt_translated) {
        for (const char *kind : { "trap", "terrain", "spell", "monster", "monster_base" }) {
            if (const char *name = an_name(kind, s)) {
                return name;
            }
        }
    }
    // 空白を含む句（文）は E: も引く。`textblock_append(tb, "%s", desc)` のように、書式の
    // 側が `%s` だけで文そのものが引数に来る所（由来・効果の説明）。1 語は引かない
    // （E: の 1 語はコマンドの名前などで、引数としてはファイル名や鍵にもなるため）。
    if (phrase) {
        if (const std::string *hit = find_in('E', s)) {
            return hit->c_str();
        }
    }
    return s;
}

void an_display_begin(void)
{
    g_display_depth.fetch_add(1, std::memory_order_relaxed);
}

void an_display_end(void)
{
    g_display_depth.fetch_sub(1, std::memory_order_relaxed);
}

namespace {

/*!
 * @brief 変身中の称号欄の字（`fmt_title()` は `shape->name` を `my_strcap()` で頭だけ大文字にして出す）の訳。
 * @details 表は `names/shape.ja.tsv`（鍵は `shape.txt` の名前そのまま。`fox`・`Pukel-man`）。大文字で来た字は
 * 頭を小文字にして引き直す。形の名前は messages の `A:`（メッセージの語）にも同じ訳があるが、こちらは称号欄の
 * 字（Term へ直に来る 1 語）専用にして、`Fox` のような一般の語を E:/A: へ出さない。
 */
const char *shape_name_ja(std::string_view core)
{
    const std::string key(core);
    if (const char *hit = an_name("shape", key.c_str())) {
        return hit;
    }
    if (!key.empty() && (key[0] >= 'A') && (key[0] <= 'Z')) {
        std::string lower = key;
        lower[0] = static_cast<char>(lower[0] - 'A' + 'a');
        return an_name("shape", lower.c_str());
    }
    return nullptr;
}

/*!
 * @brief 店の窓の見出し行 `"<店の名> (<数>)"`（`store_display_frame()` の `strnfmt(buf, "%s (%ld)", ...)`）の訳。
 * @details 店の名は `f_info[store->feat].name`（`General Store`・`Alchemy Shop`・`Home` ほか）で、書式 `%s (%ld)`
 * には鍵が無く、Term へ直に来る。表は `names/store.ja.tsv`（店の窓専用。`Home` のような 1 語を一般の表に出さない）。
 * @return 訳せたら真（`out` に「雑貨屋 (5000)」）
 */
bool store_title_ja(std::string_view core, std::string &out)
{
    const std::size_t open = core.rfind(" (");
    if ((open == std::string_view::npos) || (open == 0) || (core.back() != ')')) {
        return false;
    }
    const std::string name(core.substr(0, open));
    const char *ja = an_name("store", name.c_str());
    if (!ja) {
        return false;
    }
    out.assign(ja);
    out.append(core.substr(open));
    return true;
}

} // namespace

const char *an_tr_term(const char *buf)
{
    if (!buf || !an_lang_enabled() || !buf[0]) {
        return buf;
    }
    const std::string_view whole(buf);
    if (!is_ascii(whole)) {
        return buf; // 既に訳した字（書式の段で訳された）か、名前。
    }
    std::size_t end = whole.size();
    while ((end > 0) && (whole[end - 1] == ' ')) {
        --end;
    }
    std::size_t begin = 0;
    while ((begin < end) && (whole[begin] == ' ')) {
        ++begin;
    }
    if (begin >= end) {
        return buf;
    }
    const std::string_view core = whole.substr(begin, end - begin);
    const std::string *hit = find_in('E', core);
    std::string name_hit;
    std::string trailing_key;
    if (!hit) {
        // 左の状態列の種族・職業の名（`prt_field(player->race->name, ...)` は書式を通らず
        // `c_put_str()` で Term へ来る。設計 §3.3 の P0 の 1 語）。
        if (const char *name = an_name("race_class", std::string(core).c_str())) {
            name_hit = name;
            hit = &name_hit;
        } else if (const char *owner = an_name("owner", std::string(core).c_str())) {
            // 店主の名（`store_display_frame()` の `put_str(owner_name, ...)`。P4）。
            name_hit = owner;
            hit = &name_hit;
        } else if (const char *shape = shape_name_ja(core)) {
            // 変身中の称号欄（`fmt_title()` は `shape->name` を `my_strcap()` して出す。修-40）。
            name_hit = shape;
            hit = &name_hit;
        } else if (store_title_ja(core, name_hit)) {
            // 店の名と手持ちの金（`store_display_frame()` の `"%s (%ld)"`。書式の鍵が無く、Term へ直に来る。修-40）。
            hit = &name_hit;
        } else if (end < whole.size()) {
            // 鍵が末尾の空白を持つ字（`E:...:\s`。`prt("Hit any key to see more information: ", ...)` のように
            // 書式を通らず Term へ直に来るもの。修-40）。ここまでの引き（前後を落とした字・名前の表）が
            // 全部外れたときだけ引くので、これまで訳せていた字の結果は変わらない。
            // 空白付きの鍵は末尾の空白が 1 個のものが多いので、実際の並びと 1 個に詰めた並びの順に引く。
            hit = find_in('E', whole.substr(begin));
            if (!hit) {
                trailing_key.assign(core);
                trailing_key += ' ';
                hit = find_in('E', trailing_key);
            }
        }
    }
    if (!hit) {
        if (has_letter(core) && (core.size() >= 2)) {
            report_untranslated(std::string(core));
        }
        return buf;
    }
    static std::string work;
    work.assign(whole.substr(0, begin));
    work += *hit;
    const std::size_t want = whole.size();
    std::size_t cols = an_utf8_cols(work.c_str());
    while (cols < want) {
        work += ' ';
        ++cols;
    }
    return work.c_str();
}

const char *an_frame_label(const char *s)
{
    if (!s || !an_lang_enabled() || !s[0]) {
        return s;
    }
    const std::string_view whole(s);
    if (!is_ascii(whole)) {
        return s;
    }
    std::size_t end = whole.size();
    while ((end > 0) && (whole[end - 1] == ' ')) {
        --end;
    }
    std::size_t begin = 0;
    while ((begin < end) && (whole[begin] == ' ')) {
        ++begin;
    }
    if (begin >= end) {
        return s;
    }
    const std::string_view core = whole.substr(begin, end - begin);
    const std::string *hit = find_in('F', core);
    std::string rest_hit;
    if (!hit && (core.size() > 5) && (core.substr(0, 5) == "Rest ")) {
        // 休憩の残り（`prt_state()` が "Rest" の後ろの桁へ数字を直に書く。書式を通らない）。
        if (const std::string *rest = find_in('F', "Rest")) {
            rest_hit = *rest + std::string(core.substr(4));
            hit = &rest_hit;
        }
    }
    if (!hit) {
        return s; // Term の段（an_tr_term()）が E: と名前の表を引く。
    }
    // 呼び手ごとに別の場所（同じ行で 2 つ続けて引かれても前の字を壊さない）。
    static std::string work[4];
    static int slot = 0;
    std::string &w = work[slot];
    slot = (slot + 1) % 4;
    w.assign(whole.substr(0, begin));
    w += *hit;
    std::size_t cols = an_utf8_cols(w.c_str());
    while (cols < whole.size()) {
        w += ' ';
        ++cols;
    }
    return w.c_str();
}

const char *an_tr_tag(char tag, const char *en)
{
    if (!en || !an_lang_enabled()) {
        return en;
    }
    if (const std::string *hit = find_in(tag, en)) {
        return hit->c_str();
    }
    return en;
}

const char *an_ui_entry_label(const char *entry, const char *field, const char *en)
{
    if (!en || !entry || !field || !an_lang_enabled()) {
        return en;
    }
    std::string key = "ui_entry:";
    key += entry;
    key += ':';
    key += field;
    if (const std::string *hit = find_in('F', key)) {
        return hit->c_str();
    }
    return en;
}

int an_frame_label_cols(const char *s)
{
    const char *t = an_frame_label(s);
    return t ? static_cast<int>(an_utf8_cols(t)) : 0;
}

namespace {
//! `src` を `cap` バイトに収めて写す（字の途中では切らない）。
void copy_utf8(char *buf, size_t cap, const std::string &src)
{
    if (!buf || (cap == 0)) {
        return;
    }
    std::size_t n = std::min(src.size(), cap - 1);
    while ((n > 0) && (n < src.size()) && ((static_cast<unsigned char>(src[n]) & 0xC0) == 0x80)) {
        --n;
    }
    std::memcpy(buf, src.data(), n);
    buf[n] = '\0';
}
} // namespace

int an_name_to(const char *kind, char *buf, size_t cap)
{
    if (!buf || !an_lang_enabled()) {
        return 0;
    }
    const char *ja = an_name(kind, buf);
    if (!ja) {
        return 0;
    }
    copy_utf8(buf, cap, ja);
    return 1;
}

void an_owner_short(char *buf, size_t cap, const char *owner_name)
{
    if (!buf || !owner_name || !an_lang_enabled()) {
        return;
    }
    const char *ja = an_name("owner", owner_name);
    if (!ja) {
        return;
    }
    // 「親切なビルボ (ホビット)」の種族の括弧を落とす（原作は英語名の頭の 1 語だけを使う）。
    std::string s(ja);
    const std::size_t paren = s.rfind(" (");
    if (paren != std::string::npos) {
        s.erase(paren);
    }
    copy_utf8(buf, cap, s);
}

void an_look_health(char *buf, size_t cap)
{
    if (!buf || (cap == 0) || !an_lang_enabled()) {
        return;
    }
    // `look_mon_desc()` は「無傷」に当たる 1 句（`unhurt` `somewhat wounded` ほか）の後へ、
    // `, asleep` のような状態の句を「, 」でつなぐ。句ごとに `T:` を引き、日本語の読点でつなぎ直す。
    const std::string src(buf);
    std::string out;
    std::size_t pos = 0;
    while (pos <= src.size()) {
        const std::size_t comma = src.find(", ", pos);
        const std::string frag = src.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
        if (!out.empty()) {
            out += "、"; // アダプタの非 ASCII の字は an_lang* の中だけ（設計 §1 制約 11）
        }
        const std::string *hit = find_in('T', frag);
        out += hit ? *hit : frag;
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 2;
    }
    copy_utf8(buf, cap, out);
}

void an_look_feature(char *buf, size_t cap, const char *prefix, const char *name)
{
    if (!buf || !prefix || !name || (cap == 0) || !an_lang_enabled()) {
        return;
    }
    const char *ja = an_name("terrain", name);
    if (!ja) {
        return; // 名前の表に無い地形は原作の英語のまま（制約 5）。
    }
    // 冠詞（`a ` `an ` `some `）は日本語には要らない。`the entrance to the ` のような句は、鍵「句 + %s」の
    // `T:` が組み直す（`%sの入口`）。訳文の `%s` は 1 つだけ（読み込みが変換指定の並びを英日で照合する）。
    const std::string key = std::string(prefix) + "%s";
    std::string out = ja;
    if (const std::string *frame = find_in('T', key)) {
        out = *frame;
        const std::size_t at = out.find("%s");
        if (at != std::string::npos) {
            out.replace(at, 2, ja);
        }
    }
    copy_utf8(buf, cap, out);
}

const char *an_score_how(const char *died_from)
{
    if (!died_from || !an_lang_enabled() || is_ascii(died_from)) {
        return died_from;
    }
    // 殿堂の記録の死因は 31 バイトまで（`struct high_score` の `how[32]`）。日本語の名は 10 字ほどで尽き、
    // 字の途中で切れて U+FFFD が出る（『くいつき』のような唯一の敵の名は 40 バイトを超える）。記録には
    // 英語の名（`Grip, Farmer Maggot's Dog`。英語の起動と同じ字）を書き、画へ出すときに `an_tr_arg()` が
    // 名前の表で日本語へ引く。表に無い日本語の名は、字の境で 31 バイトへ切る。
    static std::string work;
    std::string name(died_from);
    const std::string off = an_tr_v(" (offscreen)"); // 画面の外の敵の名には `(画面外)` が付く
    if ((off != " (offscreen)") && (name.size() > off.size())
        && (name.compare(name.size() - off.size(), off.size(), off) == 0)) {
        name.erase(name.size() - off.size());
    }
    if (!g_monster_rev_built) {
        g_monster_rev_built = true;
        const auto found = g_names.find("monster");
        if (found != g_names.end()) {
            for (const auto &entry : found->second) {
                g_monster_rev.emplace(entry.second, entry.first); // 同じ日本語名が 2 つあれば先の方
            }
        }
    }
    const auto hit = g_monster_rev.find(name);
    if (hit != g_monster_rev.end()) {
        work = hit->second;
        return work.c_str();
    }
    std::vector<char> tmp(name.begin(), name.end());
    tmp.push_back('\0');
    constexpr std::size_t kHow = 31; // how[32] から終端を除いた長さ
    if (tmp.size() - 1 > kHow) {
        std::size_t n = kHow;
        while ((n > 0) && ((static_cast<unsigned char>(tmp[n]) & 0xC0) == 0x80)) {
            --n; // 多バイト字の途中で切らない
        }
        tmp[n] = '\0';
    }
    work = tmp.data();
    return work.c_str();
}

const char *an_status_field(const char *s)
{
    if (!s || !an_lang_enabled() || !s[0]) {
        return s;
    }
    const char *ja = is_ascii(s) ? an_name("race_class", s) : s;
    if (!ja) {
        return s; // 称号・地名ほか（Term の段と F: が引く）。
    }
    static std::string work;
    work = ja;
    constexpr std::size_t kField = 13; // prt_field() の欄の幅
    if (an_utf8_cols(work.c_str()) > kField) {
        std::vector<char> tmp(work.begin(), work.end());
        tmp.push_back('\0');
        an_utf8_clip_cols(tmp.data(), kField);
        work = tmp.data();
    }
    return work.c_str();
}

int an_u_wt_int(int w)
{
    return an_lang_enabled() ? (w * 5) / 100 : w / 10;
}

int an_u_wt_frac(int w)
{
    return an_lang_enabled() ? ((w * 5) % 100) / 10 : w % 10;
}

double an_u_wt_real(int w)
{
    if (an_lang_enabled()) {
        return an_u_wt_int(w) + (an_u_wt_frac(w) / 10.0);
    }
    return w / 10.0F; // 原作の式（float を可変長引数へ渡すと double になる）
}

int an_u_ft(int feet)
{
    return an_lang_enabled() ? (feet * 3048) / 10000 : feet;
}

int an_u_body_kg(int lb)
{
    return an_lang_enabled() ? (lb * 4536) / 10000 : lb;
}

int an_u_body_cm(int inch)
{
    return an_lang_enabled() ? (inch * 254) / 100 : inch;
}

void an_tr_phrases(char *buf, size_t cap)
{
    if (!buf || (cap == 0) || !an_lang_enabled() || g_phrases.empty()) {
        return;
    }
    std::string s(buf);
    bool changed = false;
    for (const auto &[en, ja] : g_phrases) {
        std::size_t pos = 0;
        while ((pos = s.find(en, pos)) != std::string::npos) {
            s.replace(pos, en.size(), ja);
            pos += ja.size();
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    if (s.size() >= cap) {
        s.resize(cap - 1);
        // 多バイト字の途中で切らない。
        while (!s.empty() && ((static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)) {
            s.pop_back();
        }
        if (!s.empty() && (static_cast<unsigned char>(s.back()) >= 0xC0)) {
            s.pop_back();
        }
    }
    std::memcpy(buf, s.c_str(), s.size() + 1);
}

void an_fmt_star_precision(void)
{
    g_star_precision = true;
}

int an_fmt_s(char *out, size_t cap, const char *aux, const char *arg)
{
    // `%.*s` の精度はバイト（呼び手が `next - in_cursor` で数えた長さ）。フック #22 が立てた印を
    // この変換で読んで下ろす（次の変換へ持ち越さない）。
    const bool star_bytes = g_star_precision;
    g_star_precision = false;
    if (!an_lang_enabled() || !out || (cap == 0) || !aux || !arg) {
        return 0;
    }
    if (is_ascii(arg)) {
        return 0;
    }
    bool left = false;
    long width = -1;
    long precision = -1;
    const char *p = aux;
    if (*p == '%') {
        ++p;
    }
    while ((*p == '-') || (*p == '+') || (*p == ' ') || (*p == '#') || (*p == '0')) {
        if (*p == '-') {
            left = true;
        }
        ++p;
    }
    if ((*p >= '0') && (*p <= '9')) {
        width = std::strtol(p, const_cast<char **>(&p), 10);
    }
    if (*p == '.') {
        ++p;
        precision = std::strtol(p, const_cast<char **>(&p), 10);
    }
    if ((width < 0) && (precision < 0)) {
        return 0;
    }
    std::string s(arg);
    if ((precision >= 0) && star_bytes) {
        if (s.size() > static_cast<std::size_t>(precision)) {
            s.resize(static_cast<std::size_t>(precision));
            // 字の途中では切らない（呼び手の長さは字の境にあるはずだが念のため）。
            std::size_t keep = s.size();
            while ((keep > 0) && ((static_cast<unsigned char>(s[keep - 1]) & 0xC0) == 0x80)) {
                --keep;
            }
            if ((keep > 0) && (static_cast<unsigned char>(s[keep - 1]) >= 0xC0)) {
                const unsigned char lead = static_cast<unsigned char>(s[keep - 1]);
                const std::size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : 2;
                if (s.size() - (keep - 1) < need) {
                    s.resize(keep - 1);
                }
            }
        }
    } else if (precision >= 0) {
        std::vector<char> tmp(s.begin(), s.end());
        tmp.push_back('\0');
        an_utf8_clip_cols(tmp.data(), static_cast<size_t>(precision));
        s = tmp.data();
    }
    std::size_t cols = an_utf8_cols(s.c_str());
    if ((width > 0) && (cols < static_cast<std::size_t>(width))) {
        const std::string pad(static_cast<std::size_t>(width) - cols, ' ');
        s = left ? (s + pad) : (pad + s);
    }
    if (s.size() >= cap) {
        s.resize(cap - 1);
    }
    std::memcpy(out, s.c_str(), s.size() + 1);
    return 1;
}

const char *an_name(const char *kind, const char *en)
{
    if (!kind || !en || !an_lang_enabled()) {
        return nullptr;
    }
    const auto t = g_names.find(kind);
    if (t == g_names.end()) {
        return nullptr;
    }
    const auto it = t->second.find(en);
    if (it == t->second.end()) {
        return nullptr;
    }
    return it->second.c_str();
}

const char *an_tr_v(const char *en)
{
    if (!en || !an_lang_enabled() || !en[0]) {
        return en;
    }
    if (const std::string *hit = find_in('V', en)) {
        return hit->c_str();
    }
    return en;
}

const char *an_tr_pred(const char *en)
{
    if (!en || !an_lang_enabled() || !en[0]) {
        return en;
    }
    if (const std::string *hit = find_in('V', en)) {
        return hit->c_str(); // 札 V: の訳は頭の助詞まで書いてある
    }
    if (is_ascii(en)) {
        return en; // 訳が無い英語（制約 5）
    }
    // gamedata 側で訳された述語（lib-ja の pain.txt の `message:`・blow_methods.txt の `act:`。P3）は
    // 主語に継ぐ助詞を持たない（「攻撃を振り払った。」）。頭が助詞でなければ「は」を足す。
    static const char *const particles[] = { "\xE3\x81\xAF", "\xE3\x81\x8C", "\xE3\x81\xAB", "\xE3\x82\x92",
        "\xE3\x81\xAE", "\xE3\x82\x82", "\xE3\x81\xA8", "\xE3\x81\xA7", "\xE3\x81\xB8", "\xE3\x80\x81" };
    for (const char *p : particles) {
        if (std::strncmp(en, p, std::strlen(p)) == 0) {
            return en;
        }
    }
    static std::unordered_map<std::string, std::string> cache; // ゲームのスレッドだけが呼ぶ
    auto it = cache.find(en);
    if (it == cache.end()) {
        it = cache.emplace(en, std::string("\xE3\x81\xAF") + en).first;
    }
    return it->second.c_str();
}

int an_mon_subject_ja(char *buf, size_t cap, const char *race_name, int unique, int count, int invisible,
    int offscreen)
{
    if (!buf || (cap == 0) || !an_lang_enabled()) {
        return 0;
    }
    std::string s;
    if (invisible) {
        s = (count == 1) ? an_tr_v("It") : (std::to_string(count) + an_tr_v(" monsters"));
    } else {
        const char *ja = race_name ? an_name("monster", race_name) : nullptr;
        const std::string name = ja ? ja : (race_name ? race_name : "");
        if (unique || (count == 1)) {
            s = name;
        } else {
            // 「3体のコボルド」。助数詞は `V: %d monsters of` の訳（体）。
            s = std::to_string(count) + an_tr_v(" of ") + name;
        }
    }
    if (offscreen) {
        s += an_tr_v(" (offscreen)");
    }
    s += ' '; // 原作と同じく区切りの空白（`an_msg_tidy()` が全角どうしの間の空白を詰める）
    if (s.size() >= cap) {
        s.resize(cap - 1);
        while (!s.empty() && ((static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)) {
            s.pop_back();
        }
        if (!s.empty() && (static_cast<unsigned char>(s.back()) >= 0xC0)) {
            s.pop_back();
        }
    }
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return 1;
}

} // extern "C"

namespace {

//! UTF-8 の 1 字を読む（壊れていれば 1 バイトを 1 字とみなす）。
unsigned long decode_at(const std::string &s, std::size_t i, std::size_t &len)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        len = 1;
        return c;
    }
    int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    if (i + static_cast<std::size_t>(n) > s.size()) {
        n = 1;
    }
    unsigned long wc = (n == 4) ? (c & 0x07u) : (n == 3) ? (c & 0x0Fu) : (n == 2) ? (c & 0x1Fu) : c;
    for (int k = 1; k < n; ++k) {
        wc = (wc << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3Fu);
    }
    len = static_cast<std::size_t>(n);
    return wc;
}

bool is_wide_at(const std::string &s, std::size_t i)
{
    if (i >= s.size()) {
        return false;
    }
    std::size_t len = 0;
    return an_wcwidth(decode_at(s, i, len)) == 2;
}

//! 直前の 1 字の頭のバイト位置（無ければ npos）。
std::size_t prev_char(const std::string &s, std::size_t i)
{
    if (i == 0) {
        return std::string::npos;
    }
    std::size_t j = i - 1;
    while ((j > 0) && ((static_cast<unsigned char>(s[j]) & 0xC0) == 0x80)) {
        --j;
    }
    return j;
}

bool ends_sentence(const std::string &s, std::size_t at)
{
    // 「。」「！」「？」（U+3002・U+FF01・U+FF1F）
    return (s.compare(at, 3, "\xE3\x80\x82") == 0) || (s.compare(at, 3, "\xEF\xBC\x81") == 0)
        || (s.compare(at, 3, "\xEF\xBC\x9F") == 0);
}

//! 未訳のメッセージを数える（`--report-untranslated=`。印は `M?:`）。英字 3 字以上の語があれば未訳。
void report_message(const std::string &text)
{
    if (g_report_path.empty()) {
        return;
    }
    static const std::set<std::string> allowed = { "ESC", "Esc", "Angband", "HTML" };
    bool english = false;
    std::size_t i = 0;
    while (i < text.size()) {
        if (!std::isalpha(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while ((j < text.size()) && std::isalpha(static_cast<unsigned char>(text[j]))) {
            ++j;
        }
        if ((j - i >= 3) && !allowed.count(text.substr(i, j - i))) {
            english = true;
            break;
        }
        i = j;
    }
    if (!english) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_report_mutex);
    if (!g_reported.insert("M?" + text).second) {
        return;
    }
    if (std::FILE *f = std::fopen(g_report_path.c_str(), "ab")) {
        std::fprintf(f, "M?:%s\n", text.c_str());
        std::fclose(f);
    }
}

} // namespace

extern "C" void an_msg_cut(const char *t, int max_cols, int min_cols, int *bytes, int *cols)
{
    // 桁 `max_cols` までに収まる頭の部分を、字の境で切る。`min_cols` より右に半角の空白が
    // あれば、いちばん右の空白で切る（原作の語の切れ目と同じ）。無ければ収まるだけ詰める。
    *bytes = 0;
    *cols = 0;
    if (!t) {
        return;
    }
    const std::string s(t);
    int best_space_bytes = -1;
    int best_space_cols = 0;
    std::size_t i = 0;
    int c = 0;
    while (i < s.size()) {
        std::size_t len = 1;
        const unsigned long wc = decode_at(s, i, len);
        const int w = (an_wcwidth(wc) == 2) ? 2 : 1;
        if (c + w > max_cols) {
            break;
        }
        if ((wc == ' ') && (c > min_cols)) {
            best_space_bytes = static_cast<int>(i);
            best_space_cols = c;
        }
        c += w;
        i += len;
    }
    if (i >= s.size()) {
        *bytes = static_cast<int>(i);
        *cols = c;
        return;
    }
    if (best_space_bytes > 0) {
        *bytes = best_space_bytes;
        *cols = best_space_cols;
        return;
    }
    *bytes = static_cast<int>(i);
    *cols = c;
}

extern "C" void an_msg_tidy(char *buf, size_t cap)
{
    if (!buf || (cap == 0) || !an_lang_enabled() || is_ascii(buf)) {
        if (buf && an_lang_enabled()) {
            report_message(buf);
        }
        return;
    }
    const std::string in(buf);
    std::string out;
    out.reserve(in.size() + 16);
    for (std::size_t i = 0; i < in.size();) {
        const char c = in[i];
        // 全角どうしの間の空白を詰める（原作は語を空白で継ぐ。`%s %s` の主語と述語ほか）。
        if (c == ' ') {
            std::size_t j = i;
            while ((j < in.size()) && (in[j] == ' ')) {
                ++j;
            }
            const std::size_t p = prev_char(out, out.size());
            if ((p != std::string::npos) && is_wide_at(out, p) && is_wide_at(in, j)) {
                i = j;
                continue;
            }
        }
        // 全角の直後の `.` `!` `?` を全角の句読点へ。「。」「！」「？」の直後なら落とす。
        if ((c == '.') || (c == '!') || (c == '?')) {
            const std::size_t p = prev_char(out, out.size());
            const bool after_wide = (p != std::string::npos) && is_wide_at(out, p);
            const bool next_ok = (i + 1 >= in.size()) || (in[i + 1] == ' ') || (in[i + 1] == '(')
                || (in[i + 1] == '.') || (in[i + 1] == '!') || (in[i + 1] == '?') || is_wide_at(in, i + 1);
            if (after_wide && next_ok) {
                if (!ends_sentence(out, p)) {
                    out += (c == '.') ? "\xE3\x80\x82" : (c == '!') ? "\xEF\xBC\x81" : "\xEF\xBC\x9F";
                }
                ++i;
                continue;
            }
        }
        out += c;
        ++i;
    }
    // 使い切ったアイテム（`object_desc()` の数 0 は日本語で頭に「もうない」。an_name_ja.c）を
    // 「〜 (a) を持っている。」へ入れると意味が逆になる（`You have no more Rations of Food (a).`）。
    // 「もう〜 (a) を持っていない。」へ直す。
    {
        static const std::string head = "\xE3\x82\x82\xE3\x81\x86\xE3\x81\xAA\xE3\x81\x84"; // もうない
        static const std::string have = "\xE3\x82\x92\xE6\x8C\x81\xE3\x81\xA3\xE3\x81\xA6\xE3\x81\x84\xE3\x82\x8B\xE3\x80\x82"; // を持っている。
        static const std::string have_not = "\xE3\x82\x92\xE6\x8C\x81\xE3\x81\xA3\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xAA\xE3\x81\x84\xE3\x80\x82"; // を持っていない。
        if ((out.compare(0, head.size(), head) == 0) && (out.size() > head.size() + have.size())
            && (out.compare(out.size() - have.size(), have.size(), have) == 0)) {
            out = std::string("\xE3\x82\x82\xE3\x81\x86") + out.substr(head.size(), out.size() - head.size() - have.size()) + have_not;
        }
    }
    report_message(out);
    if (out.size() >= cap) {
        out.resize(cap - 1);
        while (!out.empty() && ((static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)) {
            out.pop_back();
        }
        if (!out.empty() && (static_cast<unsigned char>(out.back()) >= 0xC0)) {
            out.pop_back();
        }
    }
    std::memcpy(buf, out.c_str(), out.size() + 1);
}
