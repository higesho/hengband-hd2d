/*!
 * @file lang.cpp
 * @brief 多言語の入口（画面側）の実装。設計の理由は `lang.h` に書いてある。
 */
#include "i18n/lang.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace hd2d::i18n {
namespace {

/*!
 * @brief カタログ 1 つ。**ID から文言への表**。
 * @details `std::map` にするのは、`tr()` が返す `const char *` を安定させるため。
 * 節点ごとに確保するので、後から他の言語を足しても既に返した先は動かない。
 */
using Catalog = std::map<std::string, std::string, std::less<>>;

//! 読めたカタログ。**一度読んだら解放しない**（返した `const char *` を生かすため）。
std::map<std::string, Catalog, std::less<>> g_catalogs;
std::vector<LanguageInfo> g_available;
std::string g_current = "ja";

//! 英語のカタログ（フォールバックの 2 段目）。無ければ nullptr。
const Catalog *g_english = nullptr;

//! `tr()` が ID をそのまま返すとき、`const char *` を生かすための置き場。
std::map<std::string, std::string, std::less<>> g_passthrough;

const Catalog *find_catalog(std::string_view code)
{
    const auto it = g_catalogs.find(code);
    return (it == g_catalogs.end()) ? nullptr : &it->second;
}

} // namespace

const std::vector<LanguageInfo> &all_languages()
{
    /*
     * フルセット 14（EFIGS ＋ ポルトガル語(BR) ＋ 露 ＋ ポーランド ＋ トルコ
     * ＋ 簡体字 ＋ 繁体字 ＋ 日 ＋ 韓 ＋ 蘭）。並びは英・日を先頭にして、あとは
     * だいたい話者の多い順。**足すときはここと `assets/lang/` の両方**。
     */
    //! **`u8"…"` は使わない。**C++20 では `const char8_t *` になって `const char *` に入らない。
    //! このツリーは `/execution-charset:utf-8` で組むので、素のリテラルがそのまま UTF-8 である。
    static const std::vector<LanguageInfo> kAll{
        { "en", "English" },
        { "ja", "日本語" },
        { "zh-Hans", "简体中文" },
        { "zh-Hant", "繁體中文" },
        { "ko", "한국어" },
        { "de", "Deutsch" },
        { "fr", "Français" },
        { "es", "Español" },
        { "it", "Italiano" },
        { "pt-BR", "Português (Brasil)" },
        { "ru", "Русский" },
        { "pl", "Polski" },
        { "tr", "Türkçe" },
        { "nl", "Nederlands" },
    };
    return kAll;
}

void load(const std::string &lang_dir)
{
    g_catalogs.clear();
    g_available.clear();
    g_english = nullptr;

    std::error_code ec;
    for (const auto &info : all_languages()) {
        const auto path = std::filesystem::path(lang_dir) / (std::string(info.code) + ".json");
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            continue;
        }
        /*
         * **読めない 1 つで起動を止めない。**カタログが壊れていても、その言語が
         * 選べなくなるだけで遊べる。ここで例外を投げると、訳の追加作業のたびに
         * 起動しなくなって作業にならない。
         */
        nlohmann::json doc;
        try {
            ifs >> doc;
        } catch (const nlohmann::json::exception &) {
            continue;
        }
        if (!doc.is_object()) {
            continue;
        }
        Catalog cat;
        for (const auto &[key, value] : doc.items()) {
            if (value.is_string()) {
                cat.emplace(key, value.get<std::string>());
            }
        }
        if (cat.empty()) {
            continue;
        }
        g_catalogs.emplace(info.code, std::move(cat));
        g_available.push_back(info);
    }
    g_english = find_catalog("en");

    //! いまの言語が読めていなければ、英語 → 読めた先頭、の順で寄せる。
    if (find_catalog(g_current) == nullptr) {
        if (g_english != nullptr) {
            g_current = "en";
        } else if (!g_available.empty()) {
            g_current = g_available.front().code;
        }
    }
}

const std::vector<LanguageInfo> &available()
{
    return g_available;
}

bool set_language(std::string_view code)
{
    if (find_catalog(code) == nullptr) {
        return false;
    }
    g_current.assign(code);
    return true;
}

std::string_view current()
{
    return g_current;
}

const char *current_endonym()
{
    for (const auto &info : g_available) {
        if (g_current == info.code) {
            return info.endonym;
        }
    }
    return g_current.c_str();
}

const char *tr(std::string_view id)
{
    if (const auto *cat = find_catalog(g_current); cat != nullptr) {
        if (const auto it = cat->find(id); it != cat->end()) {
            return it->second.c_str();
        }
    }
    if (g_english != nullptr) {
        if (const auto it = g_english->find(id); it != g_english->end()) {
            return it->second.c_str();
        }
    }
    /*
     * どちらにも無ければ **ID をそのまま出す**。空文字にすると画面から抜けが判らない。
     * 返す先を生かすため、置き場へ写してからその中を指す。
     */
    const auto it = g_passthrough.find(id);
    if (it != g_passthrough.end()) {
        return it->second.c_str();
    }
    const auto [pos, _] = g_passthrough.emplace(std::string(id), std::string(id));
    return pos->second.c_str();
}

} // namespace hd2d::i18n
