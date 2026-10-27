/*!
 * @file lineage_ui.cpp
 * @brief ローグライクの系譜図の画面。`hd2d/app/lineage_ui.h` の実装。
 */
#include "app/lineage_ui.h"

#include "app/app_support.h" //!< save_framebuffer_bmp（`--lineage-shot=`）
#include "app/video_recorder.h" //!< 画面内録画（HD2D_RECORD）
#include "app/test_keyboard.h"
#include "i18n/lang.h"
#include "render/text_overlay.h"
#include "ui/game_pad.h"
#include "ui/hd2d_settings.h"
#include "ui/ui_image.h"
#include "ui/ui_paint.h"
#include "ui/virtual_pad.h"

#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_map>

namespace hd2d {
using namespace gl;
namespace {

//! `text_overlay.h` の `TextColor` と `ui_paint.h` の `PaintColor` は同じ形だが別の型。
constexpr TextColor kWhite{ 1.f, 1.f, 1.f, 1.f };
constexpr TextColor kDim{ 0.72f, 0.75f, 0.82f, 1.f };

bool lang_is_ja() { return i18n::current() == "ja"; }

std::string display_name(const LineageNode &n) {
    return (!lang_is_ja() && !n.name_en.empty()) ? n.name_en : n.name;
}
std::string display_authors(const LineageNode &n) {
    return (!lang_is_ja() && !n.authors_en.empty()) ? n.authors_en : n.authors;
}
std::string display_note(const LineageNode &n) {
    if (lang_is_ja()) {
        return n.note_ja;
    }
    return n.note_en.empty() ? n.note_ja : n.note_en;
}
//! 今の言語の読み物。無ければ他方の言語の読み物へ落ちる（`nullptr` なら読み物自体が無い）。
const LineageGuide *display_guide(const LineageNode &n) {
    const LineageGuide *primary = lang_is_ja() ? &n.guide_ja : &n.guide_en;
    const LineageGuide *fallback = lang_is_ja() ? &n.guide_en : &n.guide_ja;
    if (!primary->empty()) return primary;
    if (!fallback->empty()) return fallback;
    return nullptr;
}

/*!
 * @name 折り返し（禁則処理・英単語を割らない）
 * @details `TextOverlay::fit_bytes` は画素幅だけで切る（ちょうど収まるバイト数を返す）ので、
 * 「、」「。」が行頭に来たり、英単語やラテン文字の並びが途中で割れたりする
 * （コーディネーターの指摘。`final_rogue36.png`・`final_heng_portrait.png` で実際に見つかった）。
 * ここでは符号位置（コードポイント）単位で切れ目を選び直し、
 * - 行頭に来てはいけない文字（句読点・閉じ括弧・小さい仮名など）は前の行へ引き上げる
 *   （はみ出してもよい。いわゆる追い出し）
 * - 行末に来てはいけない文字（開き括弧）は次の行へ送る
 * - 英字・数字の並び（`.` `-` `_` `'` を含む）は 1 つの単語として扱い、
 *   単語の途中では切らない（単語そのものが行の幅より長いときだけ、その単語の中で切る）
 * を当ててから、実際に描く／幅を測る位置（バイト数）を返す。
 * @{
 */
namespace kinsoku {
struct Codepoint {
    std::size_t pos{};
    std::size_t len{};
    char32_t code{};
};

std::vector<Codepoint> decode(const std::string &s) {
    std::vector<Codepoint> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c0 = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        char32_t code = c0;
        if ((c0 & 0x80) == 0x00) {
            len = 1;
            code = c0;
        } else if (((c0 & 0xE0) == 0xC0) && (i + 1 < s.size())) {
            len = 2;
            code = (static_cast<char32_t>(c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        } else if (((c0 & 0xF0) == 0xE0) && (i + 2 < s.size())) {
            len = 3;
            code = (static_cast<char32_t>(c0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        } else if (((c0 & 0xF8) == 0xF0) && (i + 3 < s.size())) {
            len = 4;
            code = (static_cast<char32_t>(c0 & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12)
                | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        } else {
            len = 1; // 壊れたバイトは 1 バイトだけ進める
            code = 0xFFFD;
        }
        if (i + len > s.size()) len = s.size() - i;
        out.push_back(Codepoint{ i, len, code });
        i += len;
    }
    return out;
}

bool is_word_char(char32_t c) {
    return ((c < 128) && (std::isalnum(static_cast<int>(c)) != 0)) || (c == U'.') || (c == U'-') || (c == U'_')
        || (c == U'\'');
}
bool cannot_start_line(char32_t c) {
    switch (c) {
    case U'、': case U'。': case U'，': case U'．': case U'）': case U'」': case U'』': case U'】':
    case U'！': case U'？': case U'ー': case U'・':
    case U'ゃ': case U'ゅ': case U'ょ': case U'っ': case U'ぁ': case U'ぃ': case U'ぅ': case U'ぇ': case U'ぉ':
    case U'ャ': case U'ュ': case U'ョ': case U'ッ': case U'ァ': case U'ィ': case U'ゥ': case U'ェ': case U'ォ':
        return true;
    default:
        return false;
    }
}
bool cannot_end_line(char32_t c) {
    switch (c) {
    case U'（': case U'「': case U'『': case U'【':
        return true;
    default:
        return false;
    }
}
} // namespace kinsoku

//! `body` の先頭から、幅 `max_w` に収まり、上の規則も満たす分だけのバイト数を返す。
//! 戻り値が 0 なら 1 文字も入らない（呼ぶ側は輪を止めること。`fit_bytes` と同じ約束）。
std::size_t wrap_bytes(TextOverlay &text, const std::string &body, int max_w) {
    if (body.empty() || (max_w <= 0)) return 0;
    const auto cps = kinsoku::decode(body);
    if (cps.empty()) return 0;
    std::size_t k = 0;
    while (k < cps.size()) {
        const std::size_t end_byte = cps[k].pos + cps[k].len;
        if (text.measure(body.substr(0, end_byte)) > max_w) break;
        ++k;
    }
    if (k == 0) k = 1; //!< 1 文字も入らなくても、進まないと輪が止まらないので最低 1 つは取る
    if (k < cps.size()) {
        //! 英字・数字の並びの途中で切れていたら、単語の頭まで戻す
        //! （戻した結果 0 文字になる＝単語そのものが 1 行より長いときだけ、割ってよい）。
        if (kinsoku::is_word_char(cps[k - 1].code) && kinsoku::is_word_char(cps[k].code)) {
            std::size_t run_start = k;
            while ((run_start > 0) && kinsoku::is_word_char(cps[run_start - 1].code)) --run_start;
            if (run_start > 0) k = run_start;
        }
    }
    //! 次の行の頭に来てはいけない文字は、この行へ引き上げる（はみ出してよい＝追い出し）。
    while ((k < cps.size()) && kinsoku::cannot_start_line(cps[k].code)) ++k;
    //! この行の末尾に開き括弧が来ていたら、次の行へ送る。
    while ((k > 1) && kinsoku::cannot_end_line(cps[k - 1].code)) --k;
    k = std::max<std::size_t>(1, std::min(k, cps.size()));
    return cps[k - 1].pos + cps[k - 1].len;
}
/*! @} */

//! ライセンスの色（凡例・チップの両方で使う）。
PaintColor license_color(const std::string &code) {
    if (code == "G") return { 0.25f, 0.75f, 0.35f, 1.f };
    if (code == "AGPL") return { 0.10f, 0.45f, 0.22f, 1.f };
    if (code == "N") return { 0.20f, 0.65f, 0.65f, 1.f };
    if (code == "L") return { 0.25f, 0.70f, 0.85f, 1.f };
    if (code == "B") return { 0.30f, 0.50f, 0.95f, 1.f };
    if (code == "A") return { 0.90f, 0.60f, 0.20f, 1.f };
    if (code == "P") return { 0.85f, 0.30f, 0.30f, 1.f };
    if (code == "closed") return { 0.55f, 0.16f, 0.16f, 1.f };
    return { 0.55f, 0.55f, 0.58f, 1.f }; // unknown
}
const char *license_key(const std::string &code) {
    if (code == "G") return "hd2d.app.lineage.license-g";
    if (code == "AGPL") return "hd2d.app.lineage.license-agpl";
    if (code == "N") return "hd2d.app.lineage.license-n";
    if (code == "L") return "hd2d.app.lineage.license-l";
    if (code == "B") return "hd2d.app.lineage.license-b";
    if (code == "A") return "hd2d.app.lineage.license-a";
    if (code == "P") return "hd2d.app.lineage.license-p";
    if (code == "closed") return "hd2d.app.lineage.license-closed";
    return "hd2d.app.lineage.license-unknown";
}
//! 凡例だけの短い言い方（情報パネルは長い説明のまま）。設計の判断：凡例の記号だけでは
//! 読み取れないので、短い言い方を添える。
const char *legend_license_key(const std::string &code) {
    if (code == "G") return "hd2d.app.lineage.legend-license-g";
    if (code == "AGPL") return "hd2d.app.lineage.legend-license-agpl";
    if (code == "N") return "hd2d.app.lineage.legend-license-n";
    if (code == "L") return "hd2d.app.lineage.legend-license-l";
    if (code == "B") return "hd2d.app.lineage.legend-license-b";
    if (code == "A") return "hd2d.app.lineage.legend-license-a";
    if (code == "P") return "hd2d.app.lineage.legend-license-p";
    if (code == "closed") return "hd2d.app.lineage.legend-license-closed";
    return "hd2d.app.lineage.legend-license-unknown";
}

struct StatusStyle {
    PaintColor fill;
    PaintColor border;
    int border_px;
};
StatusStyle status_style(const std::string &s) {
    if (s == "core") return { { 0.10f, 0.20f, 0.42f, 0.95f }, { 0.55f, 0.75f, 1.f, 1.f }, 3 };
    if (s == "candidate") return { { 0.16f, 0.14f, 0.08f, 0.95f }, { 0.85f, 0.65f, 0.25f, 1.f }, 2 };
    if (s == "closed") return { { 0.14f, 0.06f, 0.06f, 0.92f }, { 0.65f, 0.25f, 0.25f, 1.f }, 1 };
    return { { 0.10f, 0.10f, 0.13f, 0.9f }, { 0.5f, 0.5f, 0.55f, 1.f }, 1 }; // info
}
const char *status_key(const std::string &s) {
    if (s == "core") return "hd2d.app.lineage.status-core";
    if (s == "candidate") return "hd2d.app.lineage.status-candidate";
    if (s == "closed") return "hd2d.app.lineage.status-closed";
    return "hd2d.app.lineage.status-info";
}
//! リンクの種類の短い札（公式／ソース／Wiki／スポイラー／コミュニティ／ダウンロード）。
const char *link_kind_key(const std::string &kind) {
    if (kind == "home") return "hd2d.app.lineage.link-kind-home";
    if (kind == "source") return "hd2d.app.lineage.link-kind-source";
    if (kind == "wiki") return "hd2d.app.lineage.link-kind-wiki";
    if (kind == "spoiler") return "hd2d.app.lineage.link-kind-spoiler";
    if (kind == "community") return "hd2d.app.lineage.link-kind-community";
    if (kind == "download") return "hd2d.app.lineage.link-kind-download";
    return nullptr; //!< 未知の種類は札を出さない
}

/*!
 * @brief 矩形 `r` を `bounds` の内側だけに切り詰める。重ならなければ幅・高さ 0。
 * @details `UiPaint` に GL のシザーの道具が無いため（自前ローダの一覧に無い）、
 * **積む前に矩形そのものを切る**ことで地図の外（凡例・情報パネル）へにじむのを防ぐ。
 */
RectPx clip_rect(const RectPx &r, const RectPx &bounds) {
    const int x0 = std::max(r.x, bounds.x);
    const int y0 = std::max(r.y, bounds.y);
    const int x1 = std::min(r.x + r.w, bounds.x + bounds.w);
    const int y1 = std::min(r.y + r.h, bounds.y + bounds.h);
    if ((x1 <= x0) || (y1 <= y0)) return RectPx{ 0, 0, 0, 0 };
    return RectPx{ x0, y0, x1 - x0, y1 - y0 };
}

//! 水平・垂直の細い矩形として線を積む（`UiPaint` に線分の道具は無いため）。
//! `clip` を渡すと、その矩形の外側は積まない（地図の外へにじませないため）。
void h_line(UiPaint &paint, int x0, int x1, int y, int thickness, const PaintColor &color,
    const RectPx *clip = nullptr) {
    int a = std::min(x0, x1);
    int b = std::max(x0, x1);
    int yy = y;
    int th = std::max(1, thickness);
    if (clip != nullptr) {
        if ((yy + (th / 2) < clip->y) || (yy - (th / 2) > clip->y + clip->h)) return;
        a = std::max(a, clip->x);
        b = std::min(b, clip->x + clip->w);
    }
    if (b <= a) return;
    paint.rect(RectPx{ a, yy - (th / 2), b - a, th }, color);
}
void v_line(UiPaint &paint, int x, int y0, int y1, int thickness, const PaintColor &color,
    const RectPx *clip = nullptr) {
    int a = std::min(y0, y1);
    int b = std::max(y0, y1);
    int xx = x;
    int th = std::max(1, thickness);
    if (clip != nullptr) {
        if ((xx + (th / 2) < clip->x) || (xx - (th / 2) > clip->x + clip->w)) return;
        a = std::max(a, clip->y);
        b = std::min(b, clip->y + clip->h);
    }
    if (b <= a) return;
    paint.rect(RectPx{ xx - (th / 2), a, th, b - a }, color);
}
/*!
 * @brief 破線を刻んで積む。
 * @details 不具合の直し：**ループの範囲は `clip` に収まる分だけ**に切り詰めてから回す。
 * 前の版は `x0`〜`x1`（切り詰め前の全長）をそのままループの範囲にしていた。地図をつまんで
 * 大きく動かした直後など、画素の座標が地図の外まで大きく振れる回があり得るところへ、
 * 刻み幅は固定（`dash+gap`）なので、ループの回数が座標の振れ幅に比例して膨らんでいた
 * ——描く物は `clip` の中の数十個程度なのに、ループ自体は何億回も回ることになり、
 * 1 回の描画で `UiPaint` の積み上げ（`vertices_`）に大量の四角を積み続けて
 * `std::bad_alloc` に至っていた（クラッシュ報告の直し）。`clip` があれば
 * ループの前に範囲を重なりだけへ縮めておけば、ループの回数は `clip` の大きさで頭打ちになる。
 * `dash+gap` が 0 以下（本来は起きない設定ミス）でもループが進まなくならないよう最小値を 1 にする。
 */
void h_dashed(UiPaint &paint, int x0, int x1, int y, int thickness, const PaintColor &color, int dash, int gap,
    const RectPx *clip = nullptr) {
    int a = std::min(x0, x1);
    int b = std::max(x0, x1);
    if (clip != nullptr) {
        if ((y + (std::max(1, thickness) / 2) < clip->y) || (y - (std::max(1, thickness) / 2) > clip->y + clip->h)) {
            return; //!< この破線の高さそのものが clip の外。1 本も描かない
        }
        a = std::max(a, clip->x - dash);      //!< 手前の 1 刻みぶんは残す（境界で切れて見えないように）
        b = std::min(b, clip->x + clip->w + dash);
    }
    const int step = std::max(1, dash + gap);
    for (int x = a; x < b; x += step) {
        h_line(paint, x, std::min(b, x + dash), y, thickness, color, clip);
    }
}
void v_dashed(UiPaint &paint, int x, int y0, int y1, int thickness, const PaintColor &color, int dash, int gap,
    const RectPx *clip = nullptr) {
    int a = std::min(y0, y1);
    int b = std::max(y0, y1);
    if (clip != nullptr) {
        if ((x + (std::max(1, thickness) / 2) < clip->x) || (x - (std::max(1, thickness) / 2) > clip->x + clip->w)) {
            return;
        }
        a = std::max(a, clip->y - dash);
        b = std::min(b, clip->y + clip->h + dash);
    }
    const int step = std::max(1, dash + gap);
    for (int y = a; y < b; y += step) {
        v_line(paint, x, y, std::min(b, y + dash), thickness, color, clip);
    }
}

//! 親の右端 → 中間の縦線 → 子の左端、という直角の折れ線。
void draw_elbow(UiPaint &paint, const RectPx &from, const RectPx &to, bool dashed, int thickness,
    const PaintColor &color, const RectPx *clip = nullptr) {
    const int px = from.x + from.w;
    const int py = from.y + (from.h / 2);
    const int cx = to.x;
    const int cy = to.y + (to.h / 2);
    const int mid_x = px + ((cx - px) / 2);
    if (dashed) {
        h_dashed(paint, px, mid_x, py, thickness, color, 6, 5, clip);
        v_dashed(paint, mid_x, py, cy, thickness, color, 6, 5, clip);
        h_dashed(paint, mid_x, cx, cy, thickness, color, 6, 5, clip);
    } else {
        h_line(paint, px, mid_x, py, thickness, color, clip);
        v_line(paint, mid_x, py, cy, thickness, color, clip);
        h_line(paint, mid_x, cx, cy, thickness, color, clip);
    }
}

bool rect_intersects(const RectPx &a, const RectPx &b) {
    return (a.x < b.x + b.w) && (b.x < a.x + a.w) && (a.y < b.y + b.h) && (b.y < a.y + a.h);
}

} // namespace

bool load_lineage(LineageData &out, std::string &error, const std::string &path) {
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (rw == nullptr) {
        error = path + " を開けません: " + SDL_GetError();
        return false;
    }
    const Sint64 size = SDL_RWsize(rw);
    if (size <= 0) {
        SDL_RWclose(rw);
        error = path + " が空です";
        return false;
    }
    std::string buffer(static_cast<std::size_t>(size), '\0');
    const std::size_t read = SDL_RWread(rw, buffer.data(), 1, static_cast<std::size_t>(size));
    SDL_RWclose(rw);
    if (read != static_cast<std::size_t>(size)) {
        error = path + " を読み切れません";
        return false;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(buffer);
    } catch (const std::exception &e) {
        error = std::string("lineage.json の解析に失敗: ") + e.what();
        return false;
    }
    out = LineageData{};
    if (!root.contains("root") || !root["root"].is_string()) {
        error = "lineage.json に root がありません";
        return false;
    }
    out.root = root["root"].get<std::string>();
    if (!root.contains("nodes") || !root["nodes"].is_array()) {
        error = "lineage.json に nodes がありません";
        return false;
    }
    for (const auto &j : root["nodes"]) {
        if (!j.is_object()) continue;
        LineageNode node;
        const auto get_str = [&](const char *key) -> std::string {
            return (j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : std::string();
        };
        node.id = get_str("id");
        if (node.id.empty()) continue; //! 鍵の無い項目は読めない
        node.parent = get_str("parent");
        node.influence = get_str("influence");
        if (j.contains("extra_parents") && j["extra_parents"].is_array()) {
            for (const auto &e : j["extra_parents"]) {
                if (e.is_string()) node.extra_parents.push_back(e.get<std::string>());
            }
        }
        node.name = get_str("name");
        node.name_en = get_str("name_en");
        node.year = get_str("year");
        node.authors = get_str("authors");
        node.authors_en = get_str("authors_en");
        node.status = get_str("status");
        node.core = get_str("core");
        node.license = get_str("license");
        node.verified = j.contains("verified") && j["verified"].is_boolean() && j["verified"].get<bool>();
        if (j.contains("note") && j["note"].is_object()) {
            const auto &note = j["note"];
            node.note_ja = (note.contains("ja") && note["ja"].is_string()) ? note["ja"].get<std::string>() : std::string();
            node.note_en = (note.contains("en") && note["en"].is_string()) ? note["en"].get<std::string>() : std::string();
        }
        //! 概要（任意）。無い項目のほうが多い前提で、無ければ黙って空のまま。
        if (j.contains("summary") && j["summary"].is_object()) {
            const auto &summary = j["summary"];
            node.summary_ja = (summary.contains("ja") && summary["ja"].is_string()) ? summary["ja"].get<std::string>() : std::string();
            node.summary_en = (summary.contains("en") && summary["en"].is_string()) ? summary["en"].get<std::string>() : std::string();
        }
        //! 読み物（任意。概要よりさらに長い解説）。壊れた項目があっても他を読めなくしない。
        if (j.contains("guide") && j["guide"].is_object()) {
            const auto &guide = j["guide"];
            const auto parse_guide = [&](const char *lang_key, LineageGuide &out) {
                if (!guide.contains(lang_key) || !guide[lang_key].is_object()) return;
                const auto &g = guide[lang_key];
                if (g.contains("lead") && g["lead"].is_string()) out.lead = g["lead"].get<std::string>();
                if (g.contains("highlights") && g["highlights"].is_array()) {
                    for (const auto &item : g["highlights"]) {
                        if (item.is_string()) out.highlights.push_back(item.get<std::string>());
                    }
                }
                if (g.contains("lineage") && g["lineage"].is_string()) out.lineage_text = g["lineage"].get<std::string>();
                if (g.contains("tips") && g["tips"].is_array()) {
                    for (const auto &item : g["tips"]) {
                        if (item.is_string()) out.tips.push_back(item.get<std::string>());
                    }
                }
            };
            parse_guide("ja", node.guide_ja);
            parse_guide("en", node.guide_en);
        }
        //! リンク（任意）。壊れた 1 件だけを飛ばし、ほかを読めなくしない。
        if (j.contains("links") && j["links"].is_array()) {
            for (const auto &link_json : j["links"]) {
                if (!link_json.is_object()) continue;
                if (!link_json.contains("url") || !link_json["url"].is_string()) continue;
                const std::string url = link_json["url"].get<std::string>();
                if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) continue; //! http(s) 以外は開かせない
                LineageLinkFull link;
                link.url = url;
                link.kind = (link_json.contains("kind") && link_json["kind"].is_string())
                    ? link_json["kind"].get<std::string>() : std::string();
                if (link_json.contains("label") && link_json["label"].is_object()) {
                    const auto &label = link_json["label"];
                    link.label_ja = (label.contains("ja") && label["ja"].is_string()) ? label["ja"].get<std::string>() : std::string();
                    link.label_en = (label.contains("en") && label["en"].is_string()) ? label["en"].get<std::string>() : std::string();
                }
                if (link.label_ja.empty() && link.label_en.empty()) link.label_ja = link.url; //! 表示が空になるのを避ける
                node.links.push_back(std::move(link));
            }
        }
        out.nodes.push_back(std::move(node));
    }
    for (std::size_t i = 0; i < out.nodes.size(); ++i) {
        if (out.nodes[i].id == out.root) {
            out.root_index = static_cast<int>(i);
            break;
        }
    }
    if (out.root_index < 0) {
        error = "root \"" + out.root + "\" が nodes に無い";
        return false;
    }
    return true;
}

/*!
 * @brief 項目ごとの高さを決める。**コアの項目はバナー比 4:1**（`node_w / 4`）、
 * それ以外は 2 行ぶんの文字の高さ（`base_h`）。
 */
std::vector<int> compute_node_heights(const LineageData &data, int node_w, int base_h) {
    std::vector<int> heights(data.nodes.size(), base_h);
    for (std::size_t i = 0; i < data.nodes.size(); ++i) {
        if (data.nodes[i].status == "core") {
            heights[i] = std::max(base_h, node_w / 4);
        }
    }
    return heights;
}

LineageLayout layout_lineage(
    const LineageData &data, int node_w, const std::vector<int> &node_heights, int col_gap, int row_gap) {
    LineageLayout out;
    const std::size_t n = data.nodes.size();
    out.nodes.resize(n);
    if (n == 0) return out;

    std::unordered_map<std::string, int> index_by_id;
    index_by_id.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        index_by_id[data.nodes[i].id] = static_cast<int>(i);
    }

    std::vector<int> primary_parent(n, -1);
    std::vector<LineageEdgeKind> primary_kind(n, LineageEdgeKind::Source);
    std::vector<std::vector<int>> children(n);
    for (std::size_t i = 0; i < n; ++i) {
        const LineageNode &node = data.nodes[i];
        std::string pid;
        LineageEdgeKind kind = LineageEdgeKind::Source;
        if (!node.parent.empty()) {
            pid = node.parent;
            kind = LineageEdgeKind::Source;
        } else if (!node.influence.empty()) {
            pid = node.influence;
            kind = LineageEdgeKind::Influence;
        }
        if (pid.empty()) continue;
        const auto found = index_by_id.find(pid);
        if (found == index_by_id.end()) continue; //! 親が見当たらない項目は根扱いにする
        primary_parent[i] = found->second;
        primary_kind[i] = kind;
        children[static_cast<std::size_t>(found->second)].push_back(static_cast<int>(i));
    }

    std::vector<int> depth(n, 0);
    std::vector<bool> visited(n, false);

    std::function<void(int, int)> assign_depth = [&](int idx, int d) {
        if ((idx < 0) || (static_cast<std::size_t>(idx) >= n) || visited[static_cast<std::size_t>(idx)]) return;
        visited[static_cast<std::size_t>(idx)] = true;
        depth[static_cast<std::size_t>(idx)] = d;
        for (int c : children[static_cast<std::size_t>(idx)]) {
            assign_depth(c, d + 1);
        }
    };
    //! 項目ごとの高さ。**コアの項目はバナー比 4:1 で他より背が高い**ので 1 本の数では
    //! 足りない（実装済みコアの項目にバナー画像を使うという気づいたことを受けての作り）。
    //! 列が短い・無い場合は既定の高さで埋める。
    const auto height_of = [&](std::size_t i) {
        if (i < node_heights.size()) return std::max(1, node_heights[i]);
        return std::max(1, row_gap * 3);
    };
    /*!
     * y は**画素そのもの**を積む（葉は縦に並べて詰め、内側の節は子の中心の中間）。
     * 高さが項目ごとに違うので、以前のような「行番号 × 一律の行送り」では表せない。
     * 内側の節は自分の高さぶんを新たに確保しない（中間に載せるだけ）ので、
     * 極端に背の高い項目が隣の枝へ食い込む余地は残る——`--lineage-check` の
     * 重なり検査で実測して確かめる。
     */
    std::vector<double> center(n, 0.0);
    std::vector<double> top(n, 0.0);
    std::vector<bool> row_set(n, false);
    double cursor = 0.0;
    std::function<double(int)> assign_row = [&](int idx) -> double {
        if ((idx < 0) || (static_cast<std::size_t>(idx) >= n)) return 0.0;
        if (row_set[static_cast<std::size_t>(idx)]) return center[static_cast<std::size_t>(idx)];
        row_set[static_cast<std::size_t>(idx)] = true;
        const auto &kids = children[static_cast<std::size_t>(idx)];
        const double h = static_cast<double>(height_of(static_cast<std::size_t>(idx)));
        if (kids.empty()) {
            top[static_cast<std::size_t>(idx)] = cursor;
            center[static_cast<std::size_t>(idx)] = cursor + (h / 2.0);
            cursor += h + row_gap;
        } else {
            double mn = 1e18;
            double mx = -1e18;
            for (int c : kids) {
                const double cy = assign_row(c);
                mn = std::min(mn, cy);
                mx = std::max(mx, cy);
            }
            center[static_cast<std::size_t>(idx)] = (mn + mx) / 2.0;
            top[static_cast<std::size_t>(idx)] = center[static_cast<std::size_t>(idx)] - (h / 2.0);
        }
        return center[static_cast<std::size_t>(idx)];
    };

    //! 根から先に置く。データの取り決めどおりなら、これで全項目に届く。
    if (data.root_index >= 0) {
        assign_depth(data.root_index, 0);
    }
    //! 万一届かない項目が残っていたら（データの取り違え）、根扱いで別の列に足す
    //! ——落とすと「あるのに見えない」になるため。
    for (std::size_t i = 0; i < n; ++i) {
        if (!visited[i]) {
            assign_depth(static_cast<int>(i), 0);
        }
    }
    if (data.root_index >= 0) {
        assign_row(data.root_index);
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!row_set[i]) {
            assign_row(static_cast<int>(i));
        }
    }

    int max_depth = 0;
    double max_bottom = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        max_depth = std::max(max_depth, depth[i]);
        const int h = height_of(i);
        const int x = depth[i] * (node_w + col_gap);
        const int y = static_cast<int>(std::lround(top[i]));
        max_bottom = std::max(max_bottom, top[i] + h);
        out.nodes[i].rect = RectPx{ x, y, node_w, h };
        out.nodes[i].depth = depth[i];
    }
    out.content_w = ((max_depth + 1) * node_w) + (max_depth * col_gap);
    out.content_h = static_cast<int>(std::lround(max_bottom));

    for (std::size_t i = 0; i < n; ++i) {
        if (primary_parent[i] >= 0) {
            out.edges.push_back(LineageEdge{ primary_parent[i], static_cast<int>(i), primary_kind[i] });
        }
        for (const auto &extra : data.nodes[i].extra_parents) {
            const auto found = index_by_id.find(extra);
            if (found != index_by_id.end()) {
                out.edges.push_back(LineageEdge{ found->second, static_cast<int>(i), LineageEdgeKind::Extra });
            }
        }
    }
    return out;
}

int run_lineage_check(const std::string &data_path) {
    LineageData data;
    std::string error;
    if (!load_lineage(data, error, data_path)) {
        std::fprintf(stderr, "[hd2d] lineage-check: %s\n", error.c_str());
        std::fprintf(stderr, "[hd2d] RESULT: FAIL\n");
        return 1;
    }
    const std::vector<int> node_heights = compute_node_heights(data, 220, 80);
    const LineageLayout layout = layout_lineage(data, 220, node_heights, 90, 18);
    int max_depth = 0;
    for (const auto &nr : layout.nodes) {
        max_depth = std::max(max_depth, nr.depth);
    }
    bool ok = true;
    for (std::size_t i = 0; i < layout.nodes.size(); ++i) {
        for (std::size_t j = i + 1; j < layout.nodes.size(); ++j) {
            if (rect_intersects(layout.nodes[i].rect, layout.nodes[j].rect)) {
                std::fprintf(stderr, "[hd2d] lineage-check: overlap %s / %s\n", data.nodes[i].id.c_str(),
                    data.nodes[j].id.c_str());
                ok = false;
            }
        }
    }
    static constexpr const char *kKnownCores[] = { "rogue36", "rogue54", "rogue2", "umoria", "angband", "sil", "heng",
        "tang", "toband", "genso", "frox", "dcss" };
    for (const auto &node : data.nodes) {
        if (node.status != "core") continue;
        bool known = false;
        for (const char *k : kKnownCores) {
            if (node.core == k) {
                known = true;
                break;
            }
        }
        if (!known) {
            std::fprintf(stderr, "[hd2d] lineage-check: unknown core key \"%s\" on \"%s\"\n", node.core.c_str(),
                node.id.c_str());
            ok = false;
        }
    }
    std::fprintf(stderr, "[hd2d] lineage-check: %u nodes, depth %d, size %dx%d\n",
        static_cast<unsigned>(data.nodes.size()), max_depth, layout.content_w, layout.content_h);
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

LineageResult run_lineage(SDL_Window *window, TextOverlay &text, UiPaint &paint, UiImagePainter &images,
    GamePad *pad, const Hd2dSettings &cfg, const std::vector<std::string> &available_core_keys,
    const std::string &initial_core_key, const std::string &shot_path, int shot_after,
    const std::string &data_path) {
    LineageData data;
    std::string load_error;
    if (!load_lineage(data, load_error, data_path)) {
        std::fprintf(stderr, "[hd2d] lineage: %s\n", load_error.c_str());
        return LineageResult{ LineageAction::Back, {} };
    }
    const std::size_t n = data.nodes.size();
    const int node_h = (text.cell_h() * 2) + 16;
    const int col_gap = text.cell_h() * 5;
    const int row_gap = text.cell_h();

    /*!
     * コアの項目はバナー（`assets/ui/banner_<鍵>.png`。`run_core_select` と同じ絵・
     * 同じ比 4:1）を貼る。設計の判断：読めなかった項目は文字の箱のままにする
     * （読み込みに失敗しても項目が選べなくなるのは本末転倒なため）。
     */
    std::vector<UiImage> banners(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (data.nodes[i].status != "core" || data.nodes[i].core.empty()) continue;
        std::string image_err;
        if (!banners[i].load(std::string("assets/ui/banner_") + data.nodes[i].core + ".png", image_err)) {
            std::fprintf(stderr, "[hd2d] lineage banner: %s\n", image_err.c_str());
        }
    }
    struct BannerGuard {
        std::vector<UiImage> &list;
        ~BannerGuard() {
            for (UiImage &image : list) image.unload();
        }
    } banner_guard{ banners };

    //! 木の並べ方。**言語が替わると項目の幅も替わる**ので、まとめて作り直せるようにする。
    LineageLayout layout;
    int node_w = 0;
    std::vector<int> primary_parent(n, -1);
    std::vector<std::vector<int>> children(n);
    const auto rebuild_layout = [&]() {
        int max_name_w = 0;
        for (const auto &nd : data.nodes) {
            max_name_w = std::max(max_name_w, text.measure(display_name(nd)));
        }
        //! padding（左右の余白・左端のライセンスのチップ）ぶんを足し、
        //! 極端に長い／短い名前でも扱える範囲へ収める。
        node_w = std::clamp(max_name_w + (text.cell_h() * 3), text.cell_h() * 10, text.cell_h() * 30);
        const std::vector<int> node_heights = compute_node_heights(data, node_w, node_h);
        layout = layout_lineage(data, node_w, node_heights, col_gap, row_gap);
        primary_parent.assign(n, -1);
        children.assign(n, {});
        for (const auto &e : layout.edges) {
            if (e.kind == LineageEdgeKind::Extra) continue;
            primary_parent[static_cast<std::size_t>(e.to)] = e.from;
            children[static_cast<std::size_t>(e.from)].push_back(e.to);
        }
    };
    rebuild_layout();

    const auto is_launchable = [&](int idx) {
        if ((idx < 0) || (static_cast<std::size_t>(idx) >= n)) return false;
        const auto &node = data.nodes[static_cast<std::size_t>(idx)];
        if (node.status != "core") return false;
        return std::find(available_core_keys.begin(), available_core_keys.end(), node.core)
            != available_core_keys.end();
    };

    int selected = data.root_index >= 0 ? data.root_index : 0;
    if (!initial_core_key.empty()) {
        //! コアの鍵で探す。見つからなければ項目の id そのものとして探す
        //! （検査・撮影で、コアではない項目を初期選択にしたいときのための拡張）。
        int found_index = -1;
        for (std::size_t i = 0; i < n; ++i) {
            if (data.nodes[i].core == initial_core_key) {
                found_index = static_cast<int>(i);
                break;
            }
        }
        if (found_index < 0) {
            for (std::size_t i = 0; i < n; ++i) {
                if (data.nodes[i].id == initial_core_key) {
                    found_index = static_cast<int>(i);
                    break;
                }
            }
        }
        if (found_index >= 0) selected = found_index;
    }

    const auto go_parent = [&](int idx) {
        return (primary_parent[static_cast<std::size_t>(idx)] >= 0) ? primary_parent[static_cast<std::size_t>(idx)]
                                                                     : idx;
    };
    const auto go_child = [&](int idx) {
        const auto &kids = children[static_cast<std::size_t>(idx)];
        if (kids.empty()) return idx;
        const int cy = layout.nodes[static_cast<std::size_t>(idx)].rect.y;
        int best = kids.front();
        int best_d = std::abs(layout.nodes[static_cast<std::size_t>(best)].rect.y - cy);
        for (int c : kids) {
            const int d = std::abs(layout.nodes[static_cast<std::size_t>(c)].rect.y - cy);
            if (d < best_d) {
                best = c;
                best_d = d;
            }
        }
        return best;
    };
    const auto go_vert = [&](int idx, int dir) {
        const int depth = layout.nodes[static_cast<std::size_t>(idx)].depth;
        const int cy = layout.nodes[static_cast<std::size_t>(idx)].rect.y;
        int best = -1;
        int best_d = INT_MAX;
        for (std::size_t i = 0; i < n; ++i) {
            if (layout.nodes[i].depth != depth) continue;
            const int oy = layout.nodes[i].rect.y;
            if ((dir < 0) && (oy < cy) && ((cy - oy) < best_d)) {
                best = static_cast<int>(i);
                best_d = cy - oy;
            }
            if ((dir > 0) && (oy > cy) && ((oy - cy) < best_d)) {
                best = static_cast<int>(i);
                best_d = oy - cy;
            }
        }
        return (best >= 0) ? best : idx;
    };

    //! 言語の札の切り替え（コア選択画面と同じ規則。Tab / Shift+Tab）。
    const auto cycle_lang = [](int delta) {
        const auto &langs = i18n::available();
        if (langs.size() < 2) return;
        const int count = static_cast<int>(langs.size());
        int index = 0;
        for (int i = 0; i < count; ++i) {
            if (i18n::current() == langs[static_cast<std::size_t>(i)].code) {
                index = i;
                break;
            }
        }
        const auto &next = langs[static_cast<std::size_t>((((index + delta) % count) + count) % count)];
        i18n::set_language(next.code);
    };

    VirtualPad vpad;
    VirtualPadSettings vpad_settings = cfg.vpad;
    for (int i = 0; i < kVpadControlCount; ++i) {
        const bool keep = (i == static_cast<int>(VpadControl::LS)) || (i == static_cast<int>(VpadControl::A))
            || (i == static_cast<int>(VpadControl::B));
        vpad_settings.visible[i] = keep && cfg.vpad.visible[i];
    }

    //! 凡例の 1 項目。線の見本／ライセンスのチップ／状態の見本のどれか。
    enum class LegendKind { Line, Chip, Swatch };
    struct LegendItem {
        LegendKind kind{ LegendKind::Line };
        bool dashed{ false };
        PaintColor color{};
        PaintColor border{};
        int border_px{ 1 };
        std::string label;
        int width{ 0 }; //!< この項目が占める画素幅（次の項目までの間隔を含む）
    };
    /*!
     * @brief 凡例の中身を組み立てる。**言語が替わったら幅も変わる**ので、
     * 呼ぶたびに測り直す（項目は 16 個ほどなので毎フレーム呼んでも軽い）。
     */
    const auto build_legend_items = [&text]() {
        std::vector<LegendItem> items;
        const int sample_w = text.cell_h() * 2;
        const int chip = std::max(8, text.cell_h() / 2);
        const auto add_line = [&](bool dashed, const PaintColor &c, const char *key) {
            LegendItem it;
            it.kind = LegendKind::Line;
            it.dashed = dashed;
            it.color = c;
            it.label = i18n::tr(key);
            it.width = sample_w + 6 + text.measure(it.label) + 24;
            items.push_back(std::move(it));
        };
        add_line(false, PaintColor{ 0.55f, 0.62f, 0.72f, 1.f }, "hd2d.app.lineage.legend-source");
        add_line(true, PaintColor{ 0.5f, 0.5f, 0.56f, 1.f }, "hd2d.app.lineage.legend-influence");
        add_line(true, PaintColor{ 0.85f, 0.65f, 0.25f, 1.f }, "hd2d.app.lineage.legend-extra");
        static constexpr const char *kCodes[] = { "G", "AGPL", "N", "L", "B", "A", "P", "closed", "unknown" };
        for (const char *code : kCodes) {
            LegendItem it;
            it.kind = LegendKind::Chip;
            it.color = license_color(code);
            it.label = i18n::tr(legend_license_key(code));
            it.width = chip + 8 + text.measure(it.label) + 22;
            items.push_back(std::move(it));
        }
        static constexpr const char *kStatuses[] = { "core", "candidate", "info", "closed" };
        for (const char *s : kStatuses) {
            const StatusStyle style = status_style(s);
            LegendItem it;
            it.kind = LegendKind::Swatch;
            it.color = style.fill;
            it.border = style.border;
            it.border_px = style.border_px;
            it.label = i18n::tr(status_key(s));
            it.width = chip + 8 + text.measure(it.label) + 22;
            items.push_back(std::move(it));
        }
        return items;
    };
    //! 幅 `avail_w` に収まらなくなったら次の行へ折り返す。戻り値は項目ごとの (x, 行)。
    const auto place_legend = [](const std::vector<LegendItem> &items, int avail_w) {
        std::vector<int> item_x(items.size());
        std::vector<int> item_row(items.size());
        int x = 12;
        int row = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if ((x + items[i].width > avail_w) && (x > 12)) {
                ++row;
                x = 12;
            }
            item_x[i] = x;
            item_row[i] = row;
            x += items[i].width;
        }
        return std::pair<std::vector<int>, std::vector<int>>{ std::move(item_x), std::move(item_row) };
    };
    const auto legend_rows_for_width = [&](int avail_w) {
        const auto items = build_legend_items();
        const auto placed = place_legend(items, avail_w);
        int rows = 0;
        for (const int r : placed.second) rows = std::max(rows, r);
        return rows + 1;
    };

    struct ScreenLayout {
        RectPx top_bar{};
        RectPx back_button{};
        RectPx legend{};
        RectPx map{};
        RectPx info{};
        bool portrait{ false };
    };
    const auto compute_screen = [&](int w, int h) {
        ScreenLayout s{};
        s.portrait = h > w;
        const int margin = 16;
        const int bar_h = text.cell_h() + 24;
        s.top_bar = RectPx{ 0, 0, w, bar_h };
        const int back_w = text.measure(i18n::tr("hd2d.app.lineage.back")) + 28;
        s.back_button = RectPx{ margin, (bar_h - (text.cell_h() + 12)) / 2, back_w, text.cell_h() + 12 };
        const int legend_row_h = text.cell_h() + 18;
        const int legend_rows = legend_rows_for_width(w);
        const int legend_h = legend_row_h * legend_rows;
        s.legend = RectPx{ 0, bar_h, w, legend_h };
        const int content_y = bar_h + legend_h;
        if (s.portrait) {
            /*!
             * 設計の判断：中身に合わせて縮める。見出し・年著者・親子関係の行・
             * ライセンス・状態・注記数行・遊ぶボタンが入るだけの高さを見積もり、
             * それでも画面の 3 割を超えるときだけ 3 割で頭打ちにする
             * ——**逆に大きくはしない**。この見積もりは粗いもので、実際の高さは
             * 描く直前にその回の中身（`panel_lines`）を実測して詰め直す（下）。
             */
            const int line = text.cell_h() + 4;
            const int content_estimate = (line * 9) + (text.cell_h() + 20) + 70;
            const int cap_h = std::max(text.cell_h() * 9, static_cast<int>(h * 0.30f));
            const int info_h = std::min(content_estimate, cap_h);
            s.map = RectPx{ 0, content_y, w, std::max(1, h - content_y - info_h) };
            s.info = RectPx{ 0, h - info_h, w, info_h };
        } else {
            //! 設計の判断：読み物（`guide`）が有る項目は文章が長いので、読みやすさのために
            //! 横持ちのパネルを 4 割まで広げる（無ければ既定の 3.4 割のまま）。
            const bool selected_has_guide = (selected >= 0) && (static_cast<std::size_t>(selected) < n)
                && (display_guide(data.nodes[static_cast<std::size_t>(selected)]) != nullptr);
            const float info_ratio = selected_has_guide ? 0.40f : 0.34f;
            const int info_w = std::max(text.cell_h() * 16, static_cast<int>(w * info_ratio));
            s.map = RectPx{ 0, content_y, std::max(1, w - info_w), h - content_y };
            s.info = RectPx{ w - info_w, content_y, info_w, h - content_y };
        }
        return s;
    };

    float cam_x = 0.f;
    float cam_y = 0.f;
    bool cam_init = false;
    bool auto_follow = true;
    //! 最初のフレームで「選び直した」判定を誤発火させない（`selected` と同じ値で始める）。
    //! そうしないと、撮影・検査専用の `HD2D_LINEAGE_FOCUS_LINK` が 1 フレーム目で
    //! `link_focus = 0` に巻き戻されていた。
    int last_selected = selected;

    /*!
     * 情報パネルの中身を**行 1 本ずつ**の列にしておく（概要・読み物・リンクを出す作り）。
     * 選択が変わったときだけ組み直す（`rebuild_panel_lines`。ループの中で定義する）。
     * 毎フレーム組み直さないのでここ（ループの外）に置き、選び直しが起きるまで前の
     * フレームの中身を保つ。**選択がフレームの途中（イベント処理）で変わっても、
     * 描く前に必ず組み直してから描く**——以前は選択が変わっても古い項目のままの行を
     * 描いてしまい、`data.nodes[selected].links[...]` を古い項目の本数で作った添字の
     * まま新しい項目の配列の境界の外まで読んで `std::bad_alloc` になっていた
     * （クラッシュ報告の直し）。1 フレームだけ中身を描かずに飛ばす形は採らない
     * ——描き直しは軽いので、その場で作り直して同じフレームでそのまま描く。
     */
    struct PanelLine {
        enum Kind { Banner, BannerHeader, Text, Heading, Bullet, LicenseRow, LinksHeader, LinkRow, Spacer } kind{
            Text
        };
        int height{ 0 };
        int aux{ 0 };       //!< BannerHeader: バナーの幅。Bullet: 1=箇条書きの行頭（印を出す）/0=折り返しの続き
        std::string text1;  //!< 表示する文字列（種類ごとに意味が違う。上の註を見る）
        std::string text2;  //!< LicenseRow の「未確認」の 2 行目に使う（無ければ空）
        std::vector<std::string> lines2; //!< BannerHeader: 年・著者（右列の幅で折り返し済み）
        TextColor color{ kWhite };
        int link_index{ -1 };
    };
    std::vector<PanelLine> panel_lines;
    std::vector<int> line_top; //!< 積む前の頭出し（`info_scroll_area` の上端からの画素）
    int panel_content_h = 0;
    int panel_built_for_selected = selected;
    int panel_built_for_iw = -1; //!< 窓の横幅が変わったら組み直す（縦持ちの幅は窓幅そのもの）

    bool mouse_dragging = false;
    bool mouse_drag_moved = false;
    int mouse_drag_last_x = 0;
    int mouse_drag_last_y = 0;
    int mouse_down_node = -1;

    bool finger_dragging = false;
    bool finger_drag_moved = false;
    SDL_FingerID finger_id{};
    float finger_last_x = 0.f;
    float finger_last_y = 0.f;
    int finger_down_node = -1;
    int drawn = 0;

    //! 情報パネルの中身（概要・リンク）は長いことがあるので縦に流す。
    float info_scroll = 0.f;
    int info_scroll_max = 0; //!< 描画のたびに実測して詰め直す
    bool info_dragging = false;
    int info_drag_last_y = 0;
    //! 操作のフォーカス（地図の項目／情報パネルのリンク一覧）。設計の判断：概要＋リンクを
    //! 足したことで、パッド・キーボードだけでもリンクを選べる手段が要る。O キー・パッド X で切り替える。
    bool panel_focus = false;
    int link_focus = 0;
    /*!
     * @brief フォーカスをリンクへ置いた状態で撮るための検査専用のオプション
     * （`HD2D_LINEAGE_FOCUS_LINK`）。
     * @details `--lineage-shot=` は入力を注ぎ込まないので、O キーを押した絵を撮る手段が無い。
     * 環境変数で「この番号のリンクへフォーカスを置いて始める」を指定できるようにした。
     */
    if (const char *focus_env = SDL_getenv("HD2D_LINEAGE_FOCUS_LINK")) {
        link_focus = std::max(0, std::atoi(focus_env));
        panel_focus = true;
    }
    //! 検査・撮影のときはブラウザを実際には開かず、ログだけ出す（`HD2D_LINEAGE_NO_OPEN_URL`。
    //! 設計の判断：試験だけを走らせる環境で実際にブラウザを開くと検査が止まってしまう）。
    const bool suppress_open_url = SDL_getenv("HD2D_LINEAGE_NO_OPEN_URL") != nullptr;
    const auto open_link = [&](const LineageLinkFull &link) {
        std::fprintf(stderr, "[hd2d] lineage link: %s\n", link.url.c_str());
        if (!suppress_open_url) {
            SDL_OpenURL(link.url.c_str());
        }
    };

    /*!
     * @brief 検査専用：全項目を順番に地図で選んでクリックし、落ちないことを確かめる
     * （`HD2D_LINEAGE_CLICK_ALL`）。地図を動かして狙った項目を選び直す→そこを本物の
     * SDL のマウス事象（`SDL_PushEvent`）でクリックする、という遊ぶ人の操作の道筋を
     * そのまま通す（キーボードで `selected` を直に書き換えるのとは別の経路を通したいため）。
     */
    const bool click_all = SDL_getenv("HD2D_LINEAGE_CLICK_ALL") != nullptr;
    std::size_t click_index = 0;
    int click_phase_frame = 0;
    enum class ClickPhase { Pan, Click, Settle } click_phase = ClickPhase::Pan;
    unsigned click_ok_count = 0;

    VideoRecorder recorder; //!< `HD2D_RECORD=<mp4>` のときだけ動く
    recorder.start_from_env();
    //! 録画専用の巡回（下の `HD2D_LINEAGE_TOUR` の註を見る）。
    std::vector<std::string> tour_ids;
    std::vector<int> tour_nodes;
    bool tour_ready = false;
    Uint32 tour_t0 = 0;
    float tour_seconds = 10.f;
    float tour_hold = 1.f;
    if (const char *tour_env = SDL_getenv("HD2D_LINEAGE_TOUR")) {
        std::string rest = tour_env;
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const std::string id = rest.substr(0, comma);
            if (!id.empty()) tour_ids.push_back(id);
            rest = (comma == std::string::npos) ? std::string() : rest.substr(comma + 1);
        }
        if (const char *v = SDL_getenv("HD2D_LINEAGE_TOUR_SECONDS")) tour_seconds = static_cast<float>(std::atof(v));
        if (const char *v = SDL_getenv("HD2D_LINEAGE_TOUR_HOLD")) tour_hold = static_cast<float>(std::atof(v));
    }

    for (;;) {
        test_keyboard().poll("lineage");
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window, &w, &h);
        ScreenLayout screen = compute_screen(w, h);

        //! 「遊ぶ」ボタンを出すかどうかだけ先に決める。矩形は下（`play_rect`）で
        //! 決める——縦持ちでは中身の高さが決まってから `screen.info` を詰め直すため。
        bool play_visible = false;
        bool play_launchable = false;
        if ((selected >= 0) && (static_cast<std::size_t>(selected) < n)
            && (data.nodes[static_cast<std::size_t>(selected)].status == "core")) {
            play_visible = true;
            play_launchable = is_launchable(selected);
        }

        //! 横幅は縦持ちでも `screen.info.h` の詰め直しの影響を受けない
        //! （`screen.info.w` は窓幅のまま。中身をまず幅だけで組み立てる）。
        const int ix = screen.info.x + 16;
        const int iw = screen.info.w - 32;
        const bool selected_valid = (selected >= 0) && (static_cast<std::size_t>(selected) < n);

        //! `panel_lines`（ループの外の持ち物）を、今の `selected` で組み直す。
        const auto rebuild_panel_lines = [&]() {
            panel_lines.clear();
            if (!selected_valid) {
                line_top.clear();
                panel_content_h = 0;
                return;
            }
            const LineageNode &node = data.nodes[static_cast<std::size_t>(selected)];
            const bool show_banner = (node.status == "core") && banners[static_cast<std::size_t>(selected)].valid();
            std::string title = display_name(node);
            if (lang_is_ja() && !node.name_en.empty() && (node.name_en != node.name)) {
                title += "（" + node.name_en + "）";
            }
            std::string meta = node.year;
            const std::string authors = display_authors(node);
            if (!authors.empty()) {
                if (!meta.empty()) meta += " ・ ";
                meta += authors;
            }
            /*!
             * 設計の判断：バナーは `draw_cover` が縦横比で切り出す場所を決めるので、
             * 縦横比 4:1 を崩した矩形へ貼ると絵が途中で切れて壊れて見える
             * （変愚蛮怒・幻想蛮怒の表題が半分に切れていた不具合）。**幅を詰めるのではなく
             * 高さを文字 2.5〜3 行ぶんに抑え、幅はその 4 倍のまま**にする。空いた右側に
             * 表題・年著者を並べ、右側の幅が狭すぎるときだけ下へ回す。
             */
            //! 横持ちはパネルの横幅そのものが広いので、バナーは今までどおり全幅で出す
            //! （4:1 のまま `iw` いっぱいに貼ると、縦横比が保たれて歪まない）。
            //! 縦持ちだけ、右側に表題・年著者を並べる作りにする。
            bool banner_beside = false;
            int banner_h = 0;
            int banner_w = 0;
            if (show_banner && screen.portrait) {
                banner_h = (text.cell_h() * 11) / 4; // 文字 2.75 行ぶん
                banner_w = banner_h * 4;
                const int gap = 14;
                const int right_w = iw - banner_w - gap;
                banner_beside = right_w >= (text.cell_h() * 6);
                if (!banner_beside) {
                    // 横に並べる余地が無い窓幅では、バナーだけ全幅で出す（比は保つ・中央寄せ）。
                    banner_w = std::min(banner_w, iw);
                    banner_h = banner_w / 4;
                }
            } else if (show_banner) {
                banner_h = iw / 4;
            }
            if (show_banner && banner_beside) {
                const int gap = 14;
                const int right_w = iw - banner_w - gap;
                std::vector<std::string> meta_lines;
                {
                    std::string body = meta;
                    while (!body.empty() && (meta_lines.size() < 2)) {
                        const auto cut = wrap_bytes(text, body, right_w);
                        if (cut == 0) break;
                        meta_lines.push_back(body.substr(0, cut));
                        body.erase(0, cut);
                    }
                }
                const int text_stack_h = text.cell_h() + 8 + static_cast<int>(meta_lines.size()) * (text.cell_h() + 4);
                PanelLine line{ PanelLine::BannerHeader, std::max(banner_h, text_stack_h), banner_w, title, {},
                    meta_lines, kWhite, -1 };
                panel_lines.push_back(std::move(line));
                panel_lines.push_back(PanelLine{ PanelLine::Spacer, 6, 0, {}, {}, {}, kDim, -1 });
            } else {
                if (show_banner) {
                    panel_lines.push_back(PanelLine{ PanelLine::Banner, banner_h, 0, {}, {}, {}, kWhite, -1 });
                }
                panel_lines.push_back(PanelLine{ PanelLine::Text, text.cell_h() + 10, 0, title, {}, {}, kWhite, -1 });
                std::string body = meta;
                int lines = 0;
                while (!body.empty() && (lines < 2)) {
                    const auto cut = wrap_bytes(text, body, iw);
                    if (cut == 0) break;
                    panel_lines.push_back(
                        PanelLine{ PanelLine::Text, text.cell_h() + 4, 0, body.substr(0, cut), {}, {}, kDim, -1 });
                    body.erase(0, cut);
                    ++lines;
                }
                if (lines > 0) panel_lines.push_back(PanelLine{ PanelLine::Spacer, 6, 0, {}, {}, {}, kDim, -1 });
            }
            if (!node.parent.empty()) {
                const auto found = std::find_if(data.nodes.begin(), data.nodes.end(),
                    [&](const LineageNode &x) { return x.id == node.parent; });
                if (found != data.nodes.end()) {
                    panel_lines.push_back(PanelLine{ PanelLine::Text, text.cell_h() + 6, 0,
                        i18n::tr("hd2d.app.lineage.relation-source-prefix") + display_name(*found)
                            + i18n::tr("hd2d.app.lineage.relation-source-suffix"),
                        {}, {}, kDim, -1 });
                }
            } else if (!node.influence.empty()) {
                const auto found = std::find_if(data.nodes.begin(), data.nodes.end(),
                    [&](const LineageNode &x) { return x.id == node.influence; });
                if (found != data.nodes.end()) {
                    panel_lines.push_back(PanelLine{ PanelLine::Text, text.cell_h() + 6, 0,
                        i18n::tr("hd2d.app.lineage.relation-influence-prefix") + display_name(*found), {}, {}, kDim,
                        -1 });
                }
            }
            for (const auto &extra_id : node.extra_parents) {
                const auto found = std::find_if(data.nodes.begin(), data.nodes.end(),
                    [&](const LineageNode &x) { return x.id == extra_id; });
                if (found != data.nodes.end()) {
                    panel_lines.push_back(PanelLine{ PanelLine::Text, text.cell_h() + 6, 0,
                        i18n::tr("hd2d.app.lineage.extra-parent-prefix") + display_name(*found), {}, {}, kDim, -1 });
                }
            }
            panel_lines.push_back(PanelLine{ PanelLine::Spacer, 8, 0, {}, {}, {}, kDim, -1 });
            {
                std::string unverified = node.verified ? std::string() : i18n::tr("hd2d.app.lineage.license-unverified");
                panel_lines.push_back(PanelLine{ PanelLine::LicenseRow, text.cell_h() + 4, 0,
                    i18n::tr(license_key(node.license)), unverified, {}, kWhite, -1 });
                if (!unverified.empty()) {
                    panel_lines.push_back(
                        PanelLine{ PanelLine::Text, text.cell_h() + 4, 0, unverified, {}, {}, kDim, -1 });
                }
            }
            panel_lines.push_back(PanelLine{ PanelLine::Spacer, 6, 0, {}, {}, {}, kDim, -1 });
            panel_lines.push_back(PanelLine{ PanelLine::Text, text.cell_h() + 12, 0,
                i18n::tr(status_key(node.status)), {}, {}, kDim, -1 });
            /*!
             * 読み物（`guide`）があればそちらを、無ければ従来の概要（`summary`）・注記
             * （`note`）。読み物は導入の段落→「遊びどころ」の箇条書き→「系譜での位置」の
             * 段落→「はじめる人へ」の箇条書き（空でもよい）の順。
             */
            const LineageGuide *guide = display_guide(node);
            const auto push_paragraph = [&](const std::string &body_in, int max_lines) {
                std::string body = body_in;
                int lines = 0;
                while (!body.empty() && (lines < max_lines)) {
                    const auto cut = wrap_bytes(text, body, iw);
                    if (cut == 0) break;
                    panel_lines.push_back(
                        PanelLine{ PanelLine::Text, text.cell_h() + 4, 0, body.substr(0, cut), {}, {}, kDim, -1 });
                    body.erase(0, cut);
                    ++lines;
                }
            };
            if (guide != nullptr) {
                if (!guide->lead.empty()) push_paragraph(guide->lead, 60);
                //! 箇条書き：印は行頭だけ（`aux==1`）。折り返した続きの行は印を出さず、
                //! 印の幅ぶん字下げして文字の下へそろえる（ぶら下げ）。
                const std::string bullet_glyph = lang_is_ja() ? "・" : "\xe2\x80\xa2 "; //!< "・" / "• "
                const int bullet_indent = text.measure(bullet_glyph) + 4;
                const auto push_bullets = [&](const std::vector<std::string> &items) {
                    for (const auto &item : items) {
                        std::string body = item;
                        bool first = true;
                        int lines = 0;
                        while (!body.empty() && (lines < 20)) {
                            const int max_w = std::max(1, iw - bullet_indent);
                            const auto cut = wrap_bytes(text, body, max_w);
                            if (cut == 0) break;
                            panel_lines.push_back(PanelLine{ PanelLine::Bullet, text.cell_h() + 4, first ? 1 : 0,
                                body.substr(0, cut), {}, {}, kDim, -1 });
                            body.erase(0, cut);
                            first = false;
                            ++lines;
                        }
                    }
                };
                const auto push_heading = [&](const char *key) {
                    panel_lines.push_back(PanelLine{ PanelLine::Spacer, 10, 0, {}, {}, {}, kDim, -1 });
                    panel_lines.push_back(
                        PanelLine{ PanelLine::Heading, text.cell_h() + 14, 0, i18n::tr(key), {}, {}, kWhite, -1 });
                };
                if (!guide->highlights.empty()) {
                    push_heading("hd2d.app.lineage.guide-highlights");
                    push_bullets(guide->highlights);
                }
                if (!guide->lineage_text.empty()) {
                    push_heading("hd2d.app.lineage.guide-lineage");
                    push_paragraph(guide->lineage_text, 40);
                }
                if (!guide->tips.empty()) {
                    push_heading("hd2d.app.lineage.guide-tips");
                    push_bullets(guide->tips);
                }
            } else {
                const bool has_summary = lang_is_ja() ? !node.summary_ja.empty() : !node.summary_en.empty();
                std::string overview = has_summary ? (lang_is_ja() ? node.summary_ja : node.summary_en) : display_note(node);
                if (overview.empty() && lang_is_ja()) overview = node.summary_ja.empty() ? node.note_ja : node.summary_ja;
                push_paragraph(overview, 60);
            }
            if (!node.links.empty()) {
                panel_lines.push_back(PanelLine{ PanelLine::Spacer, 14, 0, {}, {}, {}, kDim, -1 });
                panel_lines.push_back(
                    PanelLine{ PanelLine::LinksHeader, text.cell_h() + 8, 0,
                        i18n::tr("hd2d.app.lineage.links-header"), {}, {}, kWhite, -1 });
                for (std::size_t li = 0; li < node.links.size(); ++li) {
                    panel_lines.push_back(PanelLine{ PanelLine::LinkRow, text.cell_h() + 18, 0, {}, {}, {}, kWhite,
                        static_cast<int>(li) });
                }
            }
            line_top.assign(panel_lines.size(), 0);
            panel_content_h = 0;
            for (std::size_t i = 0; i < panel_lines.size(); ++i) {
                line_top[i] = panel_content_h;
                panel_content_h += panel_lines[i].height;
            }
        };
        /*!
         * 設計の判断：縦持ちのパネルの高さは、その回に実際に組んだ行の高さの合計に
         * 合わせて詰め直す（`compute_screen` の粗い見積もりではなく実測を使う。
         * バナー・リンクの有無で中身の量が大きく変わるため）。上限は既定で画面の 3 割、
         * **リンクが有る項目は 4.5 割**まで広げる（リンクが多い項目だと 3 割では
         * 入り切らないため）。それでも収まらなければ、この下のスクロール表示に任せる。
         */
        RectPx play_rect{};
        RectPx info_scroll_area{};
        const auto recompute_layout = [&]() {
            if (screen.portrait) {
                const bool has_links = selected_valid && !data.nodes[static_cast<std::size_t>(selected)].links.empty();
                const int button_room = play_visible ? (text.cell_h() + 20 + 16) : 0;
                const int wanted = panel_content_h + 14 + button_room + 10;
                const float cap_ratio = has_links ? 0.45f : 0.30f;
                const int cap_h = std::max(text.cell_h() * 9, static_cast<int>(h * cap_ratio));
                const int new_info_h = std::clamp(wanted, std::min(cap_h, text.cell_h() * 9), cap_h);
                const int content_y = screen.legend.y + screen.legend.h;
                screen.map = RectPx{ 0, content_y, w, std::max(1, h - content_y - new_info_h) };
                screen.info = RectPx{ 0, h - new_info_h, w, new_info_h };
            }
            play_rect = RectPx{};
            if (play_visible) {
                play_rect = RectPx{ screen.info.x + 16, screen.info.y + screen.info.h - text.cell_h() - 28,
                    screen.info.w - 32, text.cell_h() + 20 };
            }
            info_scroll_area = play_visible
                ? RectPx{ screen.info.x, screen.info.y, screen.info.w, screen.info.h - (text.cell_h() + 20) - 16 }
                : screen.info;
            info_scroll_max = std::max(0, panel_content_h - info_scroll_area.h + 14);
        };
        //! 選択（またはパネルの横幅）が前回と違えば組み直す。イベント処理の後でも
        //! もう一度この判定を通すので、フレームの途中で選択が変わっても
        //! そのフレームのうちに新しい中身で描ける（1 フレームの空白を作らない）。
        if ((selected != panel_built_for_selected) || (iw != panel_built_for_iw)) {
            rebuild_panel_lines();
            panel_built_for_selected = selected;
            panel_built_for_iw = iw;
        }
        recompute_layout();
        //! フォーカスがリンクにあるときは、その行が見えるところまで追いかける（地図のカメラと同じ考え方）。
        if (panel_focus && selected_valid) {
            for (std::size_t i = 0; i < panel_lines.size(); ++i) {
                if ((panel_lines[i].kind != PanelLine::LinkRow) || (panel_lines[i].link_index != link_focus)) continue;
                const int top = line_top[i];
                const int bottom = top + panel_lines[i].height;
                const int visible_h = info_scroll_area.h - 14;
                if (static_cast<float>(top) < info_scroll) info_scroll = static_cast<float>(top);
                if (static_cast<float>(bottom) > (info_scroll + static_cast<float>(visible_h))) {
                    info_scroll = static_cast<float>(bottom - visible_h);
                }
                break;
            }
        }
        //! 検査・撮影の専用：情報パネルのスクロール位置を強制する（`HD2D_LINEAGE_SCROLL`）。
        //! `--lineage-shot=` は入力を注ぎ込まないので、下まで送った絵を撮る手段が無い。
        if (const char *scroll_env = SDL_getenv("HD2D_LINEAGE_SCROLL")) {
            info_scroll = static_cast<float>(std::atof(scroll_env));
        }
        info_scroll = std::clamp(info_scroll, 0.f, static_cast<float>(info_scroll_max));
        //! リンクの行の画面座標（当たり判定と描画の両方でここを使う）。
        std::vector<std::pair<RectPx, int>> link_hit_rects;
        for (std::size_t i = 0; i < panel_lines.size(); ++i) {
            if (panel_lines[i].kind != PanelLine::LinkRow) continue;
            const int screen_y = info_scroll_area.y + 14 + line_top[i] - static_cast<int>(info_scroll);
            link_hit_rects.push_back(
                { RectPx{ ix, screen_y, iw, panel_lines[i].height - 6 }, panel_lines[i].link_index });
        }
        const auto find_link_hit = [&](int mx, int my) -> int {
            if (!((mx >= info_scroll_area.x) && (mx < info_scroll_area.x + info_scroll_area.w) && (my >= info_scroll_area.y)
                    && (my < info_scroll_area.y + info_scroll_area.h))) {
                return -1;
            }
            for (const auto &hit : link_hit_rects) {
                const RectPx &r = hit.first;
                if ((mx >= r.x) && (mx < r.x + r.w) && (my >= r.y) && (my < r.y + r.h)) return hit.second;
            }
            return -1;
        };

        const auto world_hit = [&](int wx, int wy) {
            for (std::size_t i = 0; i < n; ++i) {
                const RectPx &r = layout.nodes[i].rect;
                if ((wx >= r.x) && (wx < r.x + r.w) && (wy >= r.y) && (wy < r.y + r.h)) return static_cast<int>(i);
            }
            return -1;
        };
        const auto screen_to_world = [&](int sx, int sy, int &wx, int &wy) {
            wx = static_cast<int>(sx - screen.map.x + cam_x);
            wy = static_cast<int>(sy - screen.map.y + cam_y);
        };

        if (click_all) {
            if (click_index >= n) {
                std::fprintf(stderr, "[hd2d] lineage-click-all: %u/%u 項目をクリックして落ちなかった\n",
                    click_ok_count, static_cast<unsigned>(n));
                std::fprintf(stderr, "[hd2d] RESULT: PASS\n");
                return LineageResult{ LineageAction::Back, {} };
            }
            const RectPx &wr = layout.nodes[click_index].rect;
            if (click_phase == ClickPhase::Pan) {
                //! 地図を動かして選び直す（実際にドラッグはしないが、選択の変化そのものは
                //! 「クリックで選ぶ」場合と同じ状態にする）。カメラは直接その項目の中心へ飛ばす。
                std::fprintf(stderr, "[hd2d] lineage-click-all: %u/%u %s (前の選択 %s) へ動かす\n",
                    static_cast<unsigned>(click_index) + 1, static_cast<unsigned>(n), data.nodes[click_index].id.c_str(),
                    ((selected >= 0) && (static_cast<std::size_t>(selected) < n)) ? data.nodes[static_cast<std::size_t>(selected)].id.c_str() : "?");
                std::fflush(stderr);
                cam_x = static_cast<float>(wr.x + (wr.w / 2) - (screen.map.w / 2));
                cam_y = static_cast<float>(wr.y + (wr.h / 2) - (screen.map.h / 2));
                cam_init = true;
                auto_follow = false;
                click_phase = ClickPhase::Click;
                click_phase_frame = 0;
            } else if (click_phase == ClickPhase::Click) {
                if (click_phase_frame == 2) {
                    //! 本物の SDL のマウス事象を差し込む（遊ぶ人が実際に押して離すのと同じ道筋）。
                    const int sx = screen.map.x + (wr.x + (wr.w / 2)) - static_cast<int>(cam_x);
                    const int sy = screen.map.y + (wr.y + (wr.h / 2)) - static_cast<int>(cam_y);
                    SDL_Event down{};
                    down.type = SDL_MOUSEBUTTONDOWN;
                    down.button.button = SDL_BUTTON_LEFT;
                    down.button.x = sx;
                    down.button.y = sy;
                    SDL_PushEvent(&down);
                    SDL_Event up{};
                    up.type = SDL_MOUSEBUTTONUP;
                    up.button.button = SDL_BUTTON_LEFT;
                    up.button.x = sx;
                    up.button.y = sy;
                    SDL_PushEvent(&up);
                    std::fprintf(stderr, "[hd2d] lineage-click-all: %s (%d,%d) を押す\n",
                        data.nodes[click_index].id.c_str(), sx, sy);
                    std::fflush(stderr);
                    click_phase = ClickPhase::Settle;
                    click_phase_frame = 0;
                } else {
                    ++click_phase_frame;
                }
            } else {
                if (click_phase_frame >= 6) {
                    std::fprintf(stderr, "[hd2d] lineage-click-all: %s 落ちなかった（選択 %s）\n",
                        data.nodes[click_index].id.c_str(),
                        ((selected >= 0) && (static_cast<std::size_t>(selected) < n)) ? data.nodes[static_cast<std::size_t>(selected)].id.c_str() : "?");
                    std::fflush(stderr);
                    ++click_ok_count; //!< この行に来た時点でまだ落ちていない
                    ++click_index;
                    click_phase = ClickPhase::Pan;
                    click_phase_frame = 0;
                } else {
                    ++click_phase_frame;
                }
            }
        }

        SDL_Event event;
        LineageResult early{};
        bool has_early = false;
        while (SDL_PollEvent(&event)) {
            if (pad != nullptr && pad->on_event(event)) continue;
            if (vpad.on_event(event, vpad_settings, RectPx{ 0, 0, w, h }, w, h)) continue;
            if (event.type == SDL_QUIT) {
                early = LineageResult{ LineageAction::Quit, {} };
                has_early = true;
                break;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                const int mx = event.button.x;
                const int my = event.button.y;
                if ((mx >= screen.back_button.x) && (mx < screen.back_button.x + screen.back_button.w)
                    && (my >= screen.back_button.y) && (my < screen.back_button.y + screen.back_button.h)) {
                    early = LineageResult{ LineageAction::Back, {} };
                    has_early = true;
                    break;
                }
                if (play_visible && play_launchable && (mx >= play_rect.x) && (mx < play_rect.x + play_rect.w)
                    && (my >= play_rect.y) && (my < play_rect.y + play_rect.h)) {
                    early = LineageResult{ LineageAction::Launch, data.nodes[static_cast<std::size_t>(selected)].core };
                    has_early = true;
                    break;
                }
                {
                    const int hit_link = find_link_hit(mx, my);
                    //! `hit_link` は直前に描いた `panel_lines`（このフレームで選択が
                    //! まだ変わっていなければ今の `selected` と一致）を元にした添字。
                    //! 範囲外なら開かない（`data.nodes[selected].links` の境界の外を
                    //! 読むクラッシュと同じ種類の不具合を、ここでも塞いでおく）。
                    if ((hit_link >= 0) && (selected >= 0) && (static_cast<std::size_t>(selected) < n)
                        && (static_cast<std::size_t>(hit_link) < data.nodes[static_cast<std::size_t>(selected)].links.size())) {
                        panel_focus = true;
                        link_focus = hit_link;
                        open_link(data.nodes[static_cast<std::size_t>(selected)].links[static_cast<std::size_t>(hit_link)]);
                        continue; //!< 押した瞬間に開く（ドラッグの対象にはしない）
                    }
                }
                if ((mx >= screen.map.x) && (mx < screen.map.x + screen.map.w) && (my >= screen.map.y)
                    && (my < screen.map.y + screen.map.h)) {
                    int wx = 0;
                    int wy = 0;
                    screen_to_world(mx, my, wx, wy);
                    mouse_down_node = world_hit(wx, wy);
                    mouse_dragging = true;
                    mouse_drag_moved = false;
                    mouse_drag_last_x = mx;
                    mouse_drag_last_y = my;
                } else if ((mx >= info_scroll_area.x) && (mx < info_scroll_area.x + info_scroll_area.w)
                    && (my >= info_scroll_area.y) && (my < info_scroll_area.y + info_scroll_area.h)) {
                    //! 情報パネルの空いた所を掴んで縦へ引く。
                    info_dragging = true;
                    info_drag_last_y = my;
                }
            } else if (event.type == SDL_MOUSEMOTION && mouse_dragging) {
                const int dx = event.motion.x - mouse_drag_last_x;
                const int dy = event.motion.y - mouse_drag_last_y;
                if (std::abs(dx) + std::abs(dy) > 3) {
                    mouse_drag_moved = true;
                    cam_x -= static_cast<float>(dx);
                    cam_y -= static_cast<float>(dy);
                    auto_follow = false;
                }
                mouse_drag_last_x = event.motion.x;
                mouse_drag_last_y = event.motion.y;
            } else if (event.type == SDL_MOUSEMOTION && info_dragging) {
                const int dy = event.motion.y - info_drag_last_y;
                info_scroll -= static_cast<float>(dy);
                info_drag_last_y = event.motion.y;
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                if (mouse_dragging && !mouse_drag_moved && (mouse_down_node >= 0)) {
                    if (mouse_down_node == selected) {
                        if (is_launchable(selected)) {
                            early = LineageResult{ LineageAction::Launch, data.nodes[static_cast<std::size_t>(selected)].core };
                            has_early = true;
                            mouse_dragging = false;
                            break;
                        }
                    } else {
                        selected = mouse_down_node;
                        auto_follow = true;
                    }
                }
                mouse_dragging = false;
                info_dragging = false;
            } else if (event.type == SDL_MOUSEWHEEL) {
                int mx = 0;
                int my = 0;
                SDL_GetMouseState(&mx, &my);
                const bool over_info = (mx >= screen.info.x) && (mx < screen.info.x + screen.info.w)
                    && (my >= screen.info.y) && (my < screen.info.y + screen.info.h);
                if (over_info) {
                    info_scroll -= static_cast<float>(event.wheel.y) * static_cast<float>(text.cell_h() * 2);
                } else {
                    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    const float step = static_cast<float>(text.cell_h() * 2);
                    if (shift) {
                        cam_x -= static_cast<float>(event.wheel.y) * step;
                    } else {
                        cam_y -= static_cast<float>(event.wheel.y) * step;
                        cam_x -= static_cast<float>(event.wheel.x) * step;
                    }
                    auto_follow = false;
                }
            } else if (event.type == SDL_FINGERDOWN) {
                const int fx = static_cast<int>(event.tfinger.x * w);
                const int fy = static_cast<int>(event.tfinger.y * h);
                if ((fx >= screen.back_button.x) && (fx < screen.back_button.x + screen.back_button.w)
                    && (fy >= screen.back_button.y) && (fy < screen.back_button.y + screen.back_button.h)) {
                    early = LineageResult{ LineageAction::Back, {} };
                    has_early = true;
                    break;
                }
                if (play_visible && play_launchable && (fx >= play_rect.x) && (fx < play_rect.x + play_rect.w)
                    && (fy >= play_rect.y) && (fy < play_rect.y + play_rect.h)) {
                    early = LineageResult{ LineageAction::Launch, data.nodes[static_cast<std::size_t>(selected)].core };
                    has_early = true;
                    break;
                }
                {
                    const int hit_link = find_link_hit(fx, fy);
                    //! マウスと同じ境界検査（上の註のとおり）。
                    if ((hit_link >= 0) && (selected >= 0) && (static_cast<std::size_t>(selected) < n)
                        && (static_cast<std::size_t>(hit_link) < data.nodes[static_cast<std::size_t>(selected)].links.size())) {
                        panel_focus = true;
                        link_focus = hit_link;
                        open_link(data.nodes[static_cast<std::size_t>(selected)].links[static_cast<std::size_t>(hit_link)]);
                        continue;
                    }
                }
                if ((fx >= screen.map.x) && (fx < screen.map.x + screen.map.w) && (fy >= screen.map.y)
                    && (fy < screen.map.y + screen.map.h)) {
                    int wx = 0;
                    int wy = 0;
                    screen_to_world(fx, fy, wx, wy);
                    finger_down_node = world_hit(wx, wy);
                    finger_dragging = true;
                    finger_drag_moved = false;
                    finger_id = event.tfinger.fingerId;
                    finger_last_x = event.tfinger.x * w;
                    finger_last_y = event.tfinger.y * h;
                } else if ((fx >= info_scroll_area.x) && (fx < info_scroll_area.x + info_scroll_area.w)
                    && (fy >= info_scroll_area.y) && (fy < info_scroll_area.y + info_scroll_area.h)) {
                    info_dragging = true;
                    info_drag_last_y = fy;
                    finger_id = event.tfinger.fingerId;
                }
            } else if (event.type == SDL_FINGERMOTION && finger_dragging
                && (event.tfinger.fingerId == finger_id)) {
                const float fx = event.tfinger.x * w;
                const float fy = event.tfinger.y * h;
                const float dx = fx - finger_last_x;
                const float dy = fy - finger_last_y;
                if ((std::abs(dx) + std::abs(dy)) > 3.f) {
                    finger_drag_moved = true;
                    cam_x -= dx;
                    cam_y -= dy;
                    auto_follow = false;
                }
                finger_last_x = fx;
                finger_last_y = fy;
            } else if (event.type == SDL_FINGERMOTION && info_dragging && (event.tfinger.fingerId == finger_id)) {
                const int fy = static_cast<int>(event.tfinger.y * h);
                info_scroll -= static_cast<float>(fy - info_drag_last_y);
                info_drag_last_y = fy;
            } else if (event.type == SDL_FINGERUP && finger_dragging && (event.tfinger.fingerId == finger_id)) {
                if (!finger_drag_moved && (finger_down_node >= 0)) {
                    if (finger_down_node == selected) {
                        if (is_launchable(selected)) {
                            early = LineageResult{ LineageAction::Launch, data.nodes[static_cast<std::size_t>(selected)].core };
                            has_early = true;
                            finger_dragging = false;
                            break;
                        }
                    } else {
                        selected = finger_down_node;
                        auto_follow = true;
                    }
                }
                finger_dragging = false;
            } else if (event.type == SDL_FINGERUP && info_dragging && (event.tfinger.fingerId == finger_id)) {
                info_dragging = false;
            } else if (event.type == SDL_KEYDOWN) {
                const auto key = event.key.keysym.sym;
                const std::vector<LineageLinkFull> cur_links = ((selected >= 0)
                    && (static_cast<std::size_t>(selected) < n))
                    ? data.nodes[static_cast<std::size_t>(selected)].links
                    : std::vector<LineageLinkFull>{};
                /*!
                 * **操作のフォーカスが情報パネルのリンク一覧のときは、別の小さなループで受ける**
                 * （設計の判断：O キーでリンク一覧へフォーカスを移す）。
                 * 地図のループ（矢印で選択を動かす等）とは意味が違うので混ぜない。
                 */
                if (panel_focus) {
                    switch (key) {
                    case SDLK_o:
                    case SDLK_LEFT:
                    case SDLK_h:
                    case SDLK_ESCAPE:
                        panel_focus = false;
                        break;
                    case SDLK_UP:
                    case SDLK_k:
                        if (!cur_links.empty()) {
                            link_focus = (link_focus + static_cast<int>(cur_links.size()) - 1)
                                % static_cast<int>(cur_links.size());
                        }
                        break;
                    case SDLK_DOWN:
                    case SDLK_j:
                        if (!cur_links.empty()) {
                            link_focus = (link_focus + 1) % static_cast<int>(cur_links.size());
                        }
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                    case SDLK_SPACE:
                        if (!cur_links.empty() && (link_focus >= 0)
                            && (static_cast<std::size_t>(link_focus) < cur_links.size())) {
                            open_link(cur_links[static_cast<std::size_t>(link_focus)]);
                        }
                        break;
                    default:
                        break;
                    }
                    continue; //!< 地図のループは素通りさせる
                }
                switch (key) {
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE:
                    early = LineageResult{ LineageAction::Back, {} };
                    has_early = true;
                    break;
                case SDLK_o:
                    if (!cur_links.empty()) {
                        panel_focus = true;
                        link_focus = std::clamp(link_focus, 0, static_cast<int>(cur_links.size()) - 1);
                    }
                    break;
                case SDLK_LEFT:
                case SDLK_h:
                    selected = go_parent(selected);
                    auto_follow = true;
                    break;
                case SDLK_RIGHT:
                case SDLK_l:
                    selected = go_child(selected);
                    auto_follow = true;
                    break;
                case SDLK_UP:
                case SDLK_k:
                    selected = go_vert(selected, -1);
                    auto_follow = true;
                    break;
                case SDLK_DOWN:
                case SDLK_j:
                    selected = go_vert(selected, +1);
                    auto_follow = true;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    if (is_launchable(selected)) {
                        early = LineageResult{ LineageAction::Launch, data.nodes[static_cast<std::size_t>(selected)].core };
                        has_early = true;
                    }
                    break;
                case SDLK_TAB:
                    cycle_lang((event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : +1);
                    rebuild_layout(); //!< 名前の言語が替わると幅も替わるので並べ直す
                    cam_init = false; //!< 大きさが変わった地図へ合わせて一気に飛ぶ
                    break;
                default:
                    break;
                }
                if (has_early) break;
            }
        }
        if (has_early) return early;

        if (pad != nullptr) {
            const std::vector<LineageLinkFull> cur_links = ((selected >= 0)
                && (static_cast<std::size_t>(selected) < n))
                ? data.nodes[static_cast<std::size_t>(selected)].links
                : std::vector<LineageLinkFull>{};
            int dx = 0;
            int dy = 0;
            if (pad->poll_direction(SDL_GetTicks(), true, dx, dy) && ((dx != 0) || (dy != 0))) {
                if (panel_focus) {
                    if ((dy != 0) && !cur_links.empty()) {
                        link_focus = ((link_focus + (dy > 0 ? 1 : -1)) % static_cast<int>(cur_links.size())
                            + static_cast<int>(cur_links.size())) % static_cast<int>(cur_links.size());
                    }
                } else {
                    if (dy != 0) selected = go_vert(selected, dy);
                    if (dx != 0) selected = (dx > 0) ? go_child(selected) : go_parent(selected);
                    auto_follow = true;
                }
            }
            //! 右肩・左肩は情報パネルの上下スクロール（右スティックの代わりに肩ボタンを使う
            //! 設計の判断）。フォーカスが地図・リンクのどちらでも効く。
            PadPress press{};
            while (pad->take_pressed(press)) {
                if (press.input == PadInput::LeftShoulder) info_scroll -= static_cast<float>(text.cell_h() * 3);
                if (press.input == PadInput::RightShoulder) info_scroll += static_cast<float>(text.cell_h() * 3);
                if (press.input == PadInput::X) {
                    if (!cur_links.empty()) {
                        panel_focus = !panel_focus;
                        link_focus = std::clamp(link_focus, 0, static_cast<int>(cur_links.size()) - 1);
                    } else {
                        panel_focus = false;
                    }
                }
                if (panel_focus) {
                    if ((press.input == PadInput::A) && !cur_links.empty() && (link_focus >= 0)
                        && (static_cast<std::size_t>(link_focus) < cur_links.size())) {
                        open_link(cur_links[static_cast<std::size_t>(link_focus)]);
                    }
                    if (press.input == PadInput::B) panel_focus = false;
                } else {
                    if ((press.input == PadInput::A) && is_launchable(selected)) {
                        return LineageResult{ LineageAction::Launch, data.nodes[static_cast<std::size_t>(selected)].core };
                    }
                    if (press.input == PadInput::B) {
                        return LineageResult{ LineageAction::Back, {} };
                    }
                }
            }
        }
        {
            int dx = 0;
            int dy = 0;
            if (vpad.poll_direction(SDL_GetTicks(), dx, dy) && ((dx != 0) || (dy != 0))) {
                if (dy != 0) selected = go_vert(selected, dy);
                if (dx != 0) selected = (dx > 0) ? go_child(selected) : go_parent(selected);
                auto_follow = true;
            }
            PadPress press{};
            while (vpad.take_pressed(press)) {
                if ((press.input == PadInput::A) && is_launchable(selected)) {
                    return LineageResult{ LineageAction::Launch, data.nodes[static_cast<std::size_t>(selected)].core };
                }
                if (press.input == PadInput::B) {
                    return LineageResult{ LineageAction::Back, {} };
                }
            }
        }

        /*!
         * 録画専用（`HD2D_LINEAGE_TOUR=rogue,moria,angband,hengband`）。指定した項目の並びを、
         * カメラを**時間で補間して**ゆっくり滑らかにたどり、通り過ぎた項目を選んでいく。
         * 最後の項目で止まる（その項目の詳細が開いたまま）。`HD2D_LINEAGE_TOUR_SECONDS` は
         * たどる時間（秒。既定 10）、`HD2D_LINEAGE_TOUR_HOLD` は動き出す前の待ち（秒。既定 1）。
         * キー送りだと 1 項目ごとにカメラが跳ねてカクつくので、録画のためだけに足した。
         */
        if (!tour_ids.empty() && !tour_ready) {
            tour_ready = true;
            tour_t0 = SDL_GetTicks();
            for (const std::string &id : tour_ids) {
                const auto found = std::find_if(data.nodes.begin(), data.nodes.end(),
                    [&](const LineageNode &x) { return x.id == id; });
                if (found != data.nodes.end()) tour_nodes.push_back(static_cast<int>(found - data.nodes.begin()));
            }
            if (!tour_nodes.empty()) selected = tour_nodes.front();
        }
        if (tour_ready && (tour_nodes.size() >= 2)) {
            const auto center = [&](int idx, float &cx, float &cy) {
                const RectPx &r = layout.nodes[static_cast<std::size_t>(idx)].rect;
                cx = static_cast<float>(r.x + (r.w / 2) - (screen.map.w / 2));
                cy = static_cast<float>(r.y + (r.h / 2) - (screen.map.h / 2));
            };
            std::vector<float> acc(tour_nodes.size(), 0.f); //!< 各項目までの道のり
            for (std::size_t i = 1; i < tour_nodes.size(); ++i) {
                float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
                center(tour_nodes[i - 1], ax, ay);
                center(tour_nodes[i], bx, by);
                acc[i] = acc[i - 1] + std::hypot(bx - ax, by - ay);
            }
            const float total = std::max(1.f, acc.back());
            const float elapsed = static_cast<float>(SDL_GetTicks() - tour_t0) / 1000.f;
            float u = std::clamp((elapsed - tour_hold) / std::max(0.1f, tour_seconds), 0.f, 1.f);
            u = u * u * (3.f - 2.f * u); //!< 始めと終わりをなだらかに
            const float dist = u * total;
            std::size_t seg = 1;
            while ((seg + 1 < tour_nodes.size()) && (dist > acc[seg])) ++seg;
            const float span = std::max(1.f, acc[seg] - acc[seg - 1]);
            const float f = std::clamp((dist - acc[seg - 1]) / span, 0.f, 1.f);
            float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
            center(tour_nodes[seg - 1], ax, ay);
            center(tour_nodes[seg], bx, by);
            cam_x = ax + (bx - ax) * f;
            cam_y = ay + (by - ay) * f;
            cam_init = true;
            auto_follow = false;
            std::size_t passed = 0; //!< カメラが中心を越えた最後の項目
            for (std::size_t i = 0; i < tour_nodes.size(); ++i) {
                if (dist >= acc[i] - 0.5f) passed = i;
            }
            selected = tour_nodes[passed];
        }
        if (selected != last_selected) {
            auto_follow = true;
            last_selected = selected;
            info_scroll = 0.f; //!< 選び直したら情報パネルも頭から
            link_focus = 0;
        }
        //! このフレームのイベント処理で選択が変わっていたら、描く前にもう一度組み直す
        //! （クラッシュの直し。上の `rebuild_panel_lines` の定義のところの註を見る）。
        if (selected != panel_built_for_selected) {
            rebuild_panel_lines();
            panel_built_for_selected = selected;
            recompute_layout();
        }
        if (auto_follow && (selected >= 0) && (static_cast<std::size_t>(selected) < n)) {
            const RectPx &nr = layout.nodes[static_cast<std::size_t>(selected)].rect;
            const float target_x = static_cast<float>(nr.x + (nr.w / 2) - (screen.map.w / 2));
            const float target_y = static_cast<float>(nr.y + (nr.h / 2) - (screen.map.h / 2));
            if (!cam_init) {
                cam_x = target_x;
                cam_y = target_y;
                cam_init = true;
            } else {
                cam_x += (target_x - cam_x) * 0.25f;
                cam_y += (target_y - cam_y) * 0.25f;
            }
        }
        {
            /*!
             * 不具合の直し：`std::clamp` は比較が全部 false になる NaN を**素通しする**
             * （比較のどちらも成り立たないので、渡した値をそのまま返す）。一度 NaN・無限大に
             * なると、以後ずっと直らないまま `to_screen()` の画素座標へ流れ込む
             * （`static_cast<int>(NaN)` は未定義動作で、実機では極端な値になり得る）。
             * `clamp` の前に、有限でなければ 0 へ戻す。
             */
            if (!std::isfinite(cam_x)) cam_x = 0.f;
            if (!std::isfinite(cam_y)) cam_y = 0.f;
            const float max_x = std::max(0.f, static_cast<float>(layout.content_w - screen.map.w));
            const float max_y = std::max(0.f, static_cast<float>(layout.content_h - screen.map.h));
            const float pad_margin = static_cast<float>(node_w);
            cam_x = std::clamp(cam_x, -pad_margin, max_x + pad_margin);
            cam_y = std::clamp(cam_y, -pad_margin, max_y + pad_margin);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.02f, 0.02f, 0.04f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const RectPx view{ static_cast<int>(cam_x), static_cast<int>(cam_y), screen.map.w, screen.map.h };
        const auto to_screen = [&](const RectPx &world) {
            return RectPx{ screen.map.x + world.x - static_cast<int>(cam_x),
                screen.map.y + world.y - static_cast<int>(cam_y), world.w, world.h };
        };

        //! 地図の下地。
        paint.begin(w, h);
        paint.rect(screen.map, PaintColor{ 0.045f, 0.05f, 0.07f, 1.f });
        paint.flush();

        //! 辺。culling は世界矩形と地図の見える範囲との重なりで行う。
        paint.begin(w, h);
        for (const auto &edge : layout.edges) {
            const RectPx &wf = layout.nodes[static_cast<std::size_t>(edge.from)].rect;
            const RectPx &wt = layout.nodes[static_cast<std::size_t>(edge.to)].rect;
            RectPx bound{ std::min(wf.x, wt.x), std::min(wf.y, wt.y), std::abs(wt.x - wf.x) + wf.w,
                std::abs(wt.y - wf.y) + wf.h };
            if (!rect_intersects(bound, view)) continue;
            const RectPx sf = to_screen(wf);
            const RectPx st = to_screen(wt);
            if (edge.kind == LineageEdgeKind::Source) {
                draw_elbow(paint, sf, st, false, 2, PaintColor{ 0.55f, 0.62f, 0.72f, 0.95f }, &screen.map);
            } else if (edge.kind == LineageEdgeKind::Influence) {
                draw_elbow(paint, sf, st, true, 2, PaintColor{ 0.5f, 0.5f, 0.56f, 0.7f }, &screen.map);
            } else {
                draw_elbow(paint, sf, st, true, 1, PaintColor{ 0.85f, 0.65f, 0.25f, 0.75f }, &screen.map);
            }
        }
        paint.flush();

        /*!
         * バナー（コアの項目。`assets/ui/banner_<鍵>.png`）。**歪まないよう、
         * 地図の枠に完全に収まっているときだけ貼る**——`draw_cover` は矩形の縦横比で
         * 切り出す場所を決めるので、はみ出た分を切り詰めた矩形へ貼ると絵が歪む。
         * 設計の判断：バナーが読み込めない項目は文字の箱で代える約束を、画面の外へ
         * 切れて歪む場合にも広げた。
         */
        std::vector<bool> shows_banner(n, false);
        images.begin(w, h);
        for (std::size_t i = 0; i < n; ++i) {
            if (!banners[i].valid()) continue;
            const RectPx &wr = layout.nodes[i].rect;
            if (!rect_intersects(wr, view)) continue;
            const RectPx sr = to_screen(wr);
            const bool fully_visible = (sr.x >= screen.map.x) && (sr.y >= screen.map.y)
                && ((sr.x + sr.w) <= (screen.map.x + screen.map.w)) && ((sr.y + sr.h) <= (screen.map.y + screen.map.h));
            if (!fully_visible) continue;
            images.draw_cover(banners[i], sr, 1.f);
            shows_banner[i] = true;
        }

        //! ノードの下敷きと枠。設計の判断：バナーの項目は下敷きを重ねず、
        //! 枠・選択・チップだけをバナーの上に重ねる。
        paint.begin(w, h);
        for (std::size_t i = 0; i < n; ++i) {
            const RectPx &wr = layout.nodes[i].rect;
            if (!rect_intersects(wr, view)) continue;
            const RectPx sr = to_screen(wr);
            const StatusStyle style = status_style(data.nodes[i].status);
            if (!shows_banner[i]) {
                paint.rect(clip_rect(sr, screen.map), style.fill);
            }
            paint.frame(clip_rect(sr, screen.map), style.border, style.border_px);
            if (static_cast<int>(i) == selected) {
                paint.frame(clip_rect(RectPx{ sr.x - 3, sr.y - 3, sr.w + 6, sr.h + 6 }, screen.map),
                    PaintColor{ 1.f, 1.f, 1.f, 0.9f }, 2);
            }
            //! ライセンスのチップ（左端の小さな四角）。裏付け済みは塗り、未確認は枠だけ。
            const int chip = std::max(8, text.cell_h() / 2);
            const RectPx chip_rect{ sr.x - (chip / 2), sr.y - (chip / 2), chip, chip };
            const PaintColor lc = license_color(data.nodes[i].license);
            if (data.nodes[i].verified) {
                paint.rect(clip_rect(chip_rect, screen.map), lc);
            } else {
                paint.rect(clip_rect(chip_rect, screen.map), PaintColor{ 0.05f, 0.05f, 0.06f, 0.95f });
                paint.frame(clip_rect(chip_rect, screen.map), lc, 2);
            }
        }
        paint.flush();

        /*!
         * 名前・年。**バナーの項目は絵に題字が入っているので出さない**
         * （`run_core_select` と同じ約束）。矩形そのものは切れないので、
         * **行ごとに**「地図の上下をまたがない行だけ」描き、右は地図の右端で切る
         * （地図の端で切れた項目が空の箱になっていた不具合の直し）。
         */
        text.begin(w, h);
        for (std::size_t i = 0; i < n; ++i) {
            if (shows_banner[i]) continue;
            const RectPx &wr = layout.nodes[i].rect;
            if (!rect_intersects(wr, view)) continue;
            const RectPx sr = to_screen(wr);
            const bool closed = data.nodes[i].status == "closed";
            const TextColor color = closed ? TextColor{ 0.6f, 0.55f, 0.55f, 1.f } : kWhite;
            const int pad_x = 10;
            const int text_x = sr.x + pad_x;
            const int max_w = std::min(sr.w - (pad_x * 2), (screen.map.x + screen.map.w) - text_x);
            if (max_w <= 0) continue; //!< 右端より左に何も残っていない
            const int name_y = sr.y + 6;
            if ((name_y >= screen.map.y) && ((name_y + text.cell_h()) <= (screen.map.y + screen.map.h))) {
                text.draw(text_x, name_y, display_name(data.nodes[i]), color, max_w);
            }
            const int year_y = name_y + text.cell_h() + 2;
            if (!data.nodes[i].year.empty() && (year_y >= screen.map.y)
                && ((year_y + text.cell_h()) <= (screen.map.y + screen.map.h))) {
                text.draw(text_x, year_y, data.nodes[i].year, kDim, max_w);
            }
        }
        text.flush();

        //! 上帯（題字・戻るボタン）。
        paint.begin(w, h);
        paint.rect(screen.top_bar, PaintColor{ 0.f, 0.f, 0.f, 0.55f });
        paint.rect(screen.back_button, PaintColor{ 0.15f, 0.2f, 0.3f, 1.f });
        paint.flush();
        text.begin(w, h);
        text.draw((w - text.measure(i18n::tr("hd2d.app.lineage.title"))) / 2,
            (screen.top_bar.h - text.cell_h()) / 2, i18n::tr("hd2d.app.lineage.title"), kWhite, w - 32);
        text.draw(screen.back_button.x + 12, screen.back_button.y + (screen.back_button.h - text.cell_h()) / 2,
            i18n::tr("hd2d.app.lineage.back"), kWhite, screen.back_button.w - 24);
        text.flush();

        /*!
         * 凡例（線の意味・ライセンスのチップ・状態の見本）。
         * **`paint`/`text` それぞれ 1 回だけ begin して 1 回だけ flush する。**
         * 前の版は項目ごとに begin を呼び直しており、`begin()` は積んだものを
         * 空にする約束なので、まだ流していない矩形・文字が次の begin で消えていた
         * （チップも見出しの文字も出ていなかった不具合の原因）。
         */
        {
            const auto items = build_legend_items();
            const auto placed = place_legend(items, w);
            const int legend_row_h = text.cell_h() + 18;
            paint.begin(w, h);
            text.begin(w, h);
            paint.rect(screen.legend, PaintColor{ 0.f, 0.f, 0.f, 0.45f });
            const int chip = std::max(8, text.cell_h() / 2);
            const int sample_w = text.cell_h() * 2;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const LegendItem &it = items[i];
                const int row_y = screen.legend.y + (placed.second[i] * legend_row_h);
                const int cy = row_y + (legend_row_h / 2);
                int lx = placed.first[i];
                if (it.kind == LegendKind::Line) {
                    if (it.dashed) {
                        h_dashed(paint, lx, lx + sample_w, cy, 2, it.color, 6, 5);
                    } else {
                        h_line(paint, lx, lx + sample_w, cy, 2, it.color);
                    }
                    lx += sample_w + 6;
                } else if (it.kind == LegendKind::Chip) {
                    paint.rect(RectPx{ lx, cy - (chip / 2), chip, chip }, it.color);
                    lx += chip + 8;
                } else {
                    paint.rect(RectPx{ lx, cy - (chip / 2), chip, chip }, it.color);
                    paint.frame(RectPx{ lx, cy - (chip / 2), chip, chip }, it.border, it.border_px);
                    lx += chip + 8;
                }
                text.draw(lx, row_y + ((legend_row_h - text.cell_h()) / 2), it.label, kDim, it.width);
            }
            paint.flush();
            text.flush();
        }

        /*!
         * 右（横）または下（縦）の情報パネル。**`panel_lines`（ループの頭で組んだもの）を
         * そのまま流す。**
         *
         * `paint`/`text` は 1 区画につき 1 回だけ begin して 1 回だけ flush する
         * （`begin()` は積んだものを空にする約束なので、途中で呼び直すと、まだ流していない
         * 分が消える。ライセンスのチップを描く前に `text.begin()` を挟んでいたせいで
         * 見出し・年著者・親子関係の行が消えていた不具合と、凡例のチップが 1 つも
         * 出ていなかった不具合の、どちらも原因はこれだった）。
         *
         * ただし**下敷きの板だけは先に流す**——`images.draw_cover`（バナー）は
         * `paint`/`text` とは別の道具で、呼んだ瞬間にそのまま描く。下敷きを
         * まとめて後回しにすると、後から流れる下敷きがバナーの上に重なって暗く見える
         * （バナーが暗く見えていた不具合の直し）。
         */
        {
            paint.begin(w, h);
            paint.panel(screen.info, overlay_panel_style());
            paint.flush(); //!< バナーより先に描いておく（重ねて暗くしないため）
            paint.begin(w, h);
            text.begin(w, h);
            //! `panel_lines` はここまでに必ず今の `selected` で組み直し済み（イベント処理の
            //! 直後にも `rebuild_panel_lines` を通しているため）。抜けフレームは作らない。
            for (std::size_t i = 0; i < panel_lines.size(); ++i) {
                const auto &line = panel_lines[i];
                const int top = info_scroll_area.y + 14 + line_top[i] - static_cast<int>(info_scroll);
                const int bottom = top + line.height;
                if ((bottom <= info_scroll_area.y) || (top >= info_scroll_area.y + info_scroll_area.h)) continue;
                switch (line.kind) {
                case PanelLine::Banner: {
                    const RectPx dst{ ix, top, iw, line.height };
                    if ((top >= info_scroll_area.y) && (bottom <= info_scroll_area.y + info_scroll_area.h)) {
                        images.draw_cover(banners[static_cast<std::size_t>(selected)], dst, 1.f);
                    }
                    break;
                }
                case PanelLine::BannerHeader: {
                    if ((top < info_scroll_area.y) || (bottom > info_scroll_area.y + info_scroll_area.h)) break;
                    //! 縦横比 4:1 のまま、高さ line.aux（バナーの幅）×高さ 4:1 で貼る——切り詰めない。
                    const int bh = line.aux / 4;
                    const RectPx dst{ ix, top, line.aux, bh };
                    images.draw_cover(banners[static_cast<std::size_t>(selected)], dst, 1.f);
                    const int text_x = ix + line.aux + 14;
                    const int text_w = iw - line.aux - 14;
                    const int text_stack_h = text.cell_h() + 8
                        + static_cast<int>(line.lines2.size()) * (text.cell_h() + 4);
                    int text_y = top + std::max(0, (line.height - text_stack_h) / 2);
                    text.draw(text_x, text_y, line.text1, kWhite, text_w);
                    text_y += text.cell_h() + 8;
                    for (const auto &meta_line : line.lines2) {
                        text.draw(text_x, text_y, meta_line, kDim, text_w);
                        text_y += text.cell_h() + 4;
                    }
                    break;
                }
                case PanelLine::Text:
                    if ((top >= info_scroll_area.y) && (bottom <= info_scroll_area.y + info_scroll_area.h)) {
                        text.draw(ix, top, line.text1, line.color, iw);
                    }
                    break;
                case PanelLine::Heading:
                    //! 見出し：少し明るい色・上に間隔・下に細い罫線（コーディネーターの指示）。
                    if ((top >= info_scroll_area.y) && (bottom <= info_scroll_area.y + info_scroll_area.h)) {
                        const int text_y = top + 8;
                        text.draw(ix, text_y, line.text1, TextColor{ 0.75f, 0.85f, 1.f, 1.f }, iw);
                        const int rule_y = text_y + text.cell_h() + 4;
                        paint.rect(RectPx{ ix, rule_y, iw, 1 }, PaintColor{ 0.4f, 0.48f, 0.6f, 0.6f });
                    }
                    break;
                case PanelLine::Bullet:
                    //! 箇条書き：印は行頭だけ（`aux==1`）。折り返した続きの行は、印の幅ぶん
                    //! 字下げして文字の真下にそろえる（ぶら下げインデント）。
                    if ((top >= info_scroll_area.y) && (bottom <= info_scroll_area.y + info_scroll_area.h)) {
                        const std::string glyph = lang_is_ja() ? "・" : "\xe2\x80\xa2 ";
                        const int indent = text.measure(glyph) + 4;
                        if (line.aux != 0) {
                            text.draw(ix, top, glyph, kDim, indent);
                        }
                        text.draw(ix + indent, top, line.text1, line.color, iw - indent);
                    }
                    break;
                case PanelLine::LicenseRow:
                    if ((top >= info_scroll_area.y) && (bottom <= info_scroll_area.y + info_scroll_area.h)) {
                        const int chip = text.cell_h();
                        paint.rect(RectPx{ ix, top, chip, chip },
                            license_color(data.nodes[static_cast<std::size_t>(selected)].license));
                        text.draw(ix + chip + 8, top, line.text1, line.color, iw - chip - 8);
                    }
                    break;
                case PanelLine::LinksHeader:
                    if ((top >= info_scroll_area.y) && (bottom <= info_scroll_area.y + info_scroll_area.h)) {
                        text.draw(ix, top, line.text1, line.color, iw);
                    }
                    break;
                case PanelLine::LinkRow: {
                    if ((top < info_scroll_area.y) || (bottom > info_scroll_area.y + info_scroll_area.h)) break;
                    const auto &link = data.nodes[static_cast<std::size_t>(selected)]
                                            .links[static_cast<std::size_t>(line.link_index)];
                    const RectPx row{ ix, top, iw, line.height - 6 };
                    const bool focused = panel_focus && (line.link_index == link_focus);
                    paint.rect(row, focused ? PaintColor{ 0.22f, 0.3f, 0.42f, 1.f } : PaintColor{ 0.14f, 0.16f, 0.2f, 1.f });
                    if (focused) paint.frame(row, PaintColor{ 0.62f, 0.78f, 1.f, 1.f }, 2);
                    const char *kind_key = link_kind_key(link.kind);
                    int label_x = row.x + 10;
                    if (kind_key != nullptr) {
                        const std::string tag = std::string("[") + i18n::tr(kind_key) + "]";
                        text.draw(label_x, row.y + ((row.h - text.cell_h()) / 2), tag,
                            TextColor{ 0.7f, 0.85f, 1.f, 1.f }, row.w - 20);
                        label_x += text.measure(tag) + 10;
                    }
                    const std::string label = lang_is_ja() && !link.label_ja.empty() ? link.label_ja
                        : (!link.label_en.empty() ? link.label_en : link.label_ja);
                    text.draw(label_x, row.y + ((row.h - text.cell_h()) / 2), label, kWhite, (row.x + row.w) - label_x - 10);
                    break;
                }
                case PanelLine::Spacer:
                default:
                    break;
                }
            }
            //! 遊ぶボタン、または「入っていません」。当たり判定はループの頭で決めた
            //! `play_rect` / `play_launchable` と同じ矩形を使う（描画と判定がずれないように）。
            //! **スクロールの対象外**（下端に固定）。
            if (play_visible) {
                paint.rect(play_rect, play_launchable ? PaintColor{ 0.2f, 0.45f, 0.3f, 1.f }
                                                       : PaintColor{ 0.2f, 0.2f, 0.22f, 1.f });
                const char *msg = play_launchable ? i18n::tr("hd2d.app.lineage.play")
                                                   : i18n::tr("hd2d.app.lineage.not-included");
                text.draw(play_rect.x + ((play_rect.w - text.measure(msg)) / 2),
                    play_rect.y + ((play_rect.h - text.cell_h()) / 2), msg, kWhite, play_rect.w - 16);
            }
            /*!
             * 設計の判断：スクロールできても、まだ下に隠れている行があるとわからないと
             * リンクへ気づけない。`info_scroll` がまだ下限に届いていないときは、
             * 帯を薄く重ねて「▼」を出す。
             */
            if (info_scroll < static_cast<float>(info_scroll_max) - 0.5f) {
                const int hint_h = std::min(28, info_scroll_area.h / 6);
                const RectPx hint{ info_scroll_area.x, info_scroll_area.y + info_scroll_area.h - hint_h,
                    info_scroll_area.w, hint_h };
                paint.rect(hint, PaintColor{ 0.02f, 0.03f, 0.05f, 0.65f });
                text.draw(hint.x + ((hint.w - text.measure("\xe2\x96\xbc")) / 2),
                    hint.y + ((hint.h - text.cell_h()) / 2), "\xe2\x96\xbc", TextColor{ 0.8f, 0.85f, 0.95f, 1.f },
                    hint.w);
            }
            paint.flush();
            text.flush();
        }

        //! バーチャルパッド。
        paint.begin(w, h);
        text.begin(w, h);
        vpad.draw(paint, text, vpad_settings, RectPx{ 0, 0, w, h }, PadMacroFlags{}, vpad.chord_mask(), cfg.pad,
            std::vector<PadCommand>{});
        paint.flush();
        text.flush();

        /*!
         * `--lineage-shot=`。**swap の前に読む**（既定のフレームバッファは swap で
         * 入れ替わるので、後に読むと前のフレームが写る。`run_core_select` の同じオプションと同じ作り）。
         */
        ++drawn;
        if (!shot_path.empty() && (drawn >= shot_after)) {
            int sw = 0;
            int sh = 0;
            SDL_GetWindowSize(window, &sw, &sh);
            (void)save_framebuffer_bmp(shot_path, sw, sh);
            return LineageResult{ LineageAction::Back, {} };
        }

        {
            int rw = 0;
            int rh = 0;
            SDL_GetWindowSize(window, &rw, &rh);
            recorder.capture(rw, rh);
        }
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }
}

} // namespace hd2d
