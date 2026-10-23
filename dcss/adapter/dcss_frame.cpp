// DCSS コア: 原作（ウェブタイル版 tileweb.cc）が出す JSON → HD2D の GameFrame。
//
// 方式（D2。 が対応表）: 原作の JSON は差分で届くので、ここで状態を持つ。
//   - 地図（map）: 最初に見たマスを原点にした相対座標。(80, 70) を足して絶対のマス（0〜159, 0〜139）にし、フロアの識別（clear）で捨てる。
//   - 窓の積み（menu / ui-push / txt）: M0 では**文字の画面**に起こして menu_term_lines で出す（新しい画面の窓は M3）。
//   - メッセージ（msgs）・プレイヤー（player）・入力待ちの種類（input_mode）。
// 原作のソースは読まない（JSON の欄だけ）。地形の絵は M1（ボクセルのプレハブ）、怪物・アイテム・プレイヤーの絵は M2（dcss_slab.h。ボクセルの板をその場で作る）。
#include "dcss_frame.h"

#include "dcss_slab.h"
#include "dcss_text.h"

#include "frame/cell_feature_bits.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

//! いまプレイヤー自身の行動の最中か（libhd2d.cpp。原作の `you.turn_is_over` を読む。偽 = プレイヤーが撃った・投げた最中、真 = 世界が反応している最中＝怪物・味方の行動）。ゲームのスレッドから。
bool dcss_adapter_world_reacting();

//! そのマスの雲の見通し（libhd2d.cpp。原作の内部を読む）。-1 = 雲が無い・0 = 視界を遮らない雲・1 = 視界を遮る雲（`is_opaque_cloud`。煙・蒸気・墨など）。プレイヤーのマスからの相対（dx, dy）で引く。ゲームのスレッドから。
int dcss_adapter_cloud_kind(int dx, int dy);

namespace {

using json = nlohmann::json;
using dcss_text::Colored;

// ---- 地形の表（tools/dcss/gen_terrain_table.py が feature_map.csv から作る） ----
struct DcssTerrainRow {
    std::uint16_t terrain;
    std::uint16_t flags;
    MinimapKind kind;
};
#include "dcss_terrain_table.inc"

// ---- タイル番号による地形の変種（tools/dcss/gen_dcss_terrain_rules.py が作る） ----
// 原作は床・壁のマスの背景に、そのマスのブランチ・ヴォールトに合わせて選んだ実際のタイル（t.bg の下位 16 ビット）を送る。
// 範囲 -> 地形番号の表で引く。雲は t.cloud のタイル。
struct DcssTileSpan {
    std::uint16_t first;
    std::uint16_t last;
    std::uint16_t terrain;
    std::int16_t feat; //!< 地形の値（-1 = どれでも）
};
#include "dcss_tile_variants.inc"

constexpr int kFeatFloor = 33; //!< DNGN_FLOOR
//! 画像ページ main の番号の範囲（TILE_FEAT_MAX・TILE_MAIN_MAX）。板の材料が読めたときは、その表の値を使う（これは読めないときの既定）。
constexpr int kFxFeatMax = 3454;
constexpr int kFxMainMax = 5215;
constexpr std::uint64_t kTileMask = 0xFFFFULL; //!< TILE_FLAG_MASK

// ---- 地図の格子 ----
constexpr int kMapW = 160;
constexpr int kMapH = 140;
constexpr int kOffX = 80;
constexpr int kOffY = 70;

// 原作の map_feature（map-feature.h）
constexpr int kMfItem = 6;
constexpr int kMfPlayer = 21;
// 背景タイルの旗（tile-flags.h）
constexpr std::uint64_t kTileFlagMmUnseen = 0x00020000ULL;
constexpr std::uint64_t kTileFlagUnseen = 0x00040000ULL;

// 原作の視界の既定の半径（defines.h の LOS_DEFAULT_RANGE）と、画面の光の種類（LightingState::light_kind の 4 = 装備によらない光）
constexpr int kVisionRadius = 7;
constexpr std::uint8_t kLightKindOther = 4;

// 原作の input_mode（defines.h）
constexpr int kModeNormal = 0;
constexpr int kModeCommand = 1;
constexpr int kModeMore = 5;
constexpr int kModePrompt = 7;
constexpr int kModeYesNo = 8;

struct MapCell {
    int f = 0;
    int mf = 0;
    std::string g = " ";
    int col = 7;
    bool has_mon = false;
    int mon_id = 0;
    int mon_type = 0;
    int mon_att = 0;
    std::uint64_t bg = kTileFlagUnseen;
    std::uint64_t fg = 0;
    std::uint64_t cloud = 0; //!< t.cloud（雲のタイル。0 = 無し）
    std::vector<int> icons; //!< t.icons（状態のアイコンのタイル番号。眠り・混乱ほか。変わったときだけ全部送られてくる）
    bool summoner = false; //!< t.highlighted_summoner（召喚者の強調）
    // 前景の絵の材料（M2）。原作は fg が変わったときにだけ送るので、ここで覚える。
    std::uint64_t base = 0; //!< t.base（アイテムの下地。薬の瓶など）
    std::uint64_t overlay1 = 0; //!< t.overlay1・overlay2（呪文書の流派の印）
    std::uint64_t overlay2 = 0;
    std::vector<std::array<int, 2>> doll; //!< t.doll の [タイル番号, y_max]（人形・怪物の重ねる部品。順に重ねる）
    std::vector<std::array<int, 3>> mcache; //!< t.mcache の [タイル番号, x のずれ, y のずれ]
    int flv_floor = 0; //!< t.flv.f（床のタイル番号。扉・階段・祭壇など背景が透ける物のマスにだけ、変わったときに送られる。0 = 未受信）
    bool trans = false; //!< t.trans（半透明の怪物。原作のクライアントは doll を受けて trans が無ければ偽へ戻す）
    //! t.ov のうち画像ページ main の範囲のタイル（ビームの弾・矢・投げた物・爆発。原作の cell_renderer.js が「雲の上に重ねる」物。攻撃の演出。段 4）。
    //! ov は変わったときに全部届くので、届くたびに置き換える（壁の影・水の縁などの地形の重ねは含まない）。
    std::vector<int> fx;
    bool fx_by_player = false; //!< fx が届いたとき、プレイヤー自身の行動の最中だったか（弾が飛ぶ向きの手がかり。怪物・味方の行動なら偽）
    int flash = 0; //!< flc（閃光の色。原作の色番号 0〜15。0 = 無し）
    int flash_alpha = 0; //!< fla（0 = 色の表の既定の濃さ）
};

struct MenuItem {
    std::string text;
    int colour = 7;
    int level = 2;
    std::vector<int> hotkeys;
};

struct Window {
    enum class Kind { Menu, Ui, Crt } kind = Kind::Menu;
    // menu
    std::string tag;
    std::string title;
    std::string more;
    std::vector<MenuItem> items;
    int hovered = -1;
    int first = 0;
    // ui
    std::string type;
    json data;
    json state;
    int generation = 0;
};

struct TextInput {
    bool active = false;
    std::string prompt;
    std::string prefill;
    std::string typed;
    bool replace_on_first_key = false;
    int maxlen = 0;
};

struct Msg {
    std::uint32_t seq = 0;
    std::string text; //!< 書式つき
    int channel = 0;
};

struct State {
    // 地図
    std::vector<MapCell> cells = std::vector<MapCell>(static_cast<std::size_t>(kMapW) * kMapH);
    bool map_received = false;
    std::uint64_t floor_generation = 0;
    int cursor_x = -1; //!< 狙いのカーソル（id 0）。絶対のマス
    int cursor_y = -1;
    int ui_state = 0;
    int input_mode = 0;
    // プレイヤー
    std::map<std::string, std::int64_t> num;
    std::map<std::string, std::string> str;
    int pos_x = 0; //!< 相対
    int pos_y = 0;
    bool pos_known = false;
    json status = json::array();
    // メッセージ
    std::deque<Msg> msgs;
    std::uint32_t msg_seq = 0;
    bool more = false;
    std::string more_text;
    // 窓
    std::array<std::string, 24> crt_lines{};
    std::vector<Window> stack;
    int generation = 0;
    bool creation_started = false;
    TextInput input;
    std::string exit_reason;
    bool demon_tier = false; //!< tile_show_demon_tier（悪魔の等級の数字のアイコンを怪物の板に焼き込む。原作の既定は偽）
};

State g;

dcss_slab::Bank g_bank; //!< 怪物・アイテム・プレイヤーの板（ゲームのスレッドだけが触る）

std::mutex g_hint_mutex;
int g_view_w = 0;
int g_view_h = 0;
std::string g_lang = "ja";
std::atomic<int> g_scene{ static_cast<int>(dcss_frame::InputScene::Other) };
std::atomic<bool> g_ui_offered_frame{ false }; //!< 画面が dcss-ui を挙げた（窓は画面が描く）
std::mutex g_name_mutex;
std::string g_player_name;
std::string g_exit_reason;
std::atomic<bool> g_text_active{ false };
std::mutex g_typed_mutex;
std::string g_typed_delta; //!< 受信スレッドが覚える、文字入力の中身の編集（表示のため）

// ---- 文字列の道具 ----

void sanitize_utf8(std::string &s)
{
    std::string out;
    out.reserve(s.size());
    bool changed = false;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = 1;
        if (c < 0x80) n = 1;
        else if ((c >> 5) == 0x6) n = 2;
        else if ((c >> 4) == 0xE) n = 3;
        else if ((c >> 3) == 0x1E) n = 4;
        else n = 0;
        bool ok = n > 0 && i + static_cast<std::size_t>(n) <= s.size();
        for (int k = 1; ok && k < n; ++k) {
            ok = (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) == 0x80;
        }
        if (ok) {
            out.append(s, i, static_cast<std::size_t>(n));
            i += static_cast<std::size_t>(n);
        } else {
            out += "\xEF\xBF\xBD";
            ++i;
            changed = true;
        }
    }
    if (changed) s = std::move(out);
}

int cell_width(std::uint32_t cp)
{
    if (cp >= 0x1100
        && (cp <= 0x115F || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF)
            || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6))) {
        return 2;
    }
    return 1;
}

int display_width(const std::string &s)
{
    int w = 0;
    for (const std::uint32_t cp : dcss_text::utf8_to_codepoints(s)) w += cell_width(cp);
    return w;
}

//! 字の列を表示幅 `width` で折り返す（`\n` で段落を切る）。
std::vector<std::string> wrap_plain(const std::string &text, int width)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string para = text.substr(start, end - start);
        std::string cur;
        int cur_w = 0;
        for (std::size_t i = 0; i < para.size();) {
            const int n = dcss_text::utf8_len(static_cast<unsigned char>(para[i]));
            const std::string ch = para.substr(i, static_cast<std::size_t>(n));
            const std::uint32_t cp = dcss_text::utf8_to_codepoints(ch).empty() ? 0 : dcss_text::utf8_to_codepoints(ch)[0];
            const int w = cell_width(cp);
            if (cur_w + w > width && !cur.empty()) {
                lines.push_back(cur);
                cur.clear();
                cur_w = 0;
                if (ch == " ") {
                    i += static_cast<std::size_t>(n);
                    continue;
                }
            }
            cur += ch;
            cur_w += w;
            i += static_cast<std::size_t>(n);
        }
        lines.push_back(cur);
        if (end >= text.size()) break;
        start = end + 1;
    }
    return lines;
}

//! 行末の空白を落とす（色の区間も縮める）。
void rtrim_colored(Colored &c)
{
    while (!c.text.empty() && (c.text.back() == ' ' || c.text.back() == '\t')) c.text.pop_back();
    const int size = static_cast<int>(c.text.size());
    while (!c.spans.empty() && c.spans.back().begin >= size) c.spans.pop_back();
    if (!c.spans.empty() && c.spans.back().begin + c.spans.back().len > size) c.spans.back().len = size - c.spans.back().begin;
    if (c.spans.size() <= 1) {
        if (!c.spans.empty()) c.color = c.spans.front().color;
        c.spans.clear();
    }
}

std::string trim_right(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    return s;
}

//! 色つきの字を `\n` で行に分ける（色の区間も切る）。
std::vector<Colored> split_lines(const Colored &c)
{
    std::vector<Colored> out;
    std::size_t start = 0;
    const auto color_at = [&](std::size_t pos) -> std::uint8_t {
        for (const auto &s : c.spans) {
            if (static_cast<int>(pos) >= s.begin && static_cast<int>(pos) < s.begin + s.len) return s.color;
        }
        return c.color;
    };
    while (start <= c.text.size()) {
        std::size_t end = c.text.find('\n', start);
        if (end == std::string::npos) end = c.text.size();
        Colored line;
        line.text = c.text.substr(start, end - start);
        if (!c.spans.empty()) {
            // 行の中の区間を、行頭からのバイト位置にして並べる。
            std::vector<dcss_text::Span> spans;
            for (const auto &s : c.spans) {
                const int b = std::max<int>(s.begin, static_cast<int>(start));
                const int e = std::min<int>(s.begin + s.len, static_cast<int>(end));
                if (e > b) spans.push_back(dcss_text::Span{ b - static_cast<int>(start), e - b, s.color });
            }
            line.spans = spans;
            line.color = line.text.empty() ? c.color : color_at(start);
            if (line.spans.size() <= 1) {
                if (!line.spans.empty()) line.color = line.spans.front().color;
                line.spans.clear();
            }
        } else {
            line.color = c.color;
        }
        out.push_back(std::move(line));
        if (end >= c.text.size()) break;
        start = end + 1;
    }
    return out;
}

//! JSON の欄の取り出し（型が違えば既定）。
std::string js(const json &o, const char *key, const std::string &def = std::string())
{
    if (!o.is_object()) return def;
    const auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : def;
}
std::int64_t jn(const json &o, const char *key, std::int64_t def = 0)
{
    if (!o.is_object()) return def;
    const auto it = o.find(key);
    if (it == o.end()) return def;
    if (it->is_number_integer()) return it->get<std::int64_t>();
    if (it->is_number_unsigned()) return static_cast<std::int64_t>(it->get<std::uint64_t>());
    if (it->is_number_float()) return static_cast<std::int64_t>(it->get<double>());
    if (it->is_boolean()) return it->get<bool>() ? 1 : 0;
    return def;
}
std::uint64_t ju(const json &o, const char *key, std::uint64_t def = 0)
{
    if (!o.is_object()) return def;
    const auto it = o.find(key);
    if (it == o.end()) return def;
    if (it->is_number_unsigned()) return it->get<std::uint64_t>();
    if (it->is_number_integer()) return static_cast<std::uint64_t>(it->get<std::int64_t>());
    return def;
}

//! タイルの番号（旗つき）。原作は JavaScript が符号つきの 32 ビットしか扱えないので、旗が 33 ビット目より上にあるときは [下位, 上位] の対で送る
//! （tileweb.cc の write_tileidx）。脅威度・毒の旗は最上位の 3 ビットなので、怪物の fg はほぼ常に対で届く。
std::uint64_t tile_value(const json &o, const char *key, std::uint64_t def = 0)
{
    if (!o.is_object()) return def;
    const auto it = o.find(key);
    if (it == o.end()) return def;
    const json &v = *it;
    if (v.is_array() && v.size() >= 2 && v[0].is_number() && v[1].is_number()) {
        const std::uint64_t lo = static_cast<std::uint64_t>(v[0].get<std::int64_t>()) & 0xFFFFFFFFULL;
        const std::uint64_t hi = static_cast<std::uint64_t>(v[1].get<std::int64_t>()) & 0xFFFFFFFFULL;
        return lo | (hi << 32);
    }
    if (v.is_number_unsigned()) return v.get<std::uint64_t>();
    if (v.is_number_integer()) return static_cast<std::uint64_t>(v.get<std::int64_t>()) & 0xFFFFFFFFULL;
    return def;
}

bool lang_is_ja()
{
    std::lock_guard<std::mutex> lock(g_hint_mutex);
    return g_lang != "en";
}

// ---- 地形の変種（床・壁・雲・派生） ----

/// 範囲の表（先頭の番号順・重ならない）から引く。無ければ 0。
template <std::size_t N>
std::uint16_t find_span(const DcssTileSpan (&spans)[N], unsigned tile)
{
    const DcssTileSpan *end = spans + N;
    const DcssTileSpan *it = std::upper_bound(spans, end, tile, [](unsigned t, const DcssTileSpan &s) { return t < s.first; });
    if (it == spans) return 0;
    --it;
    return (tile <= it->last) ? it->terrain : 0;
}

/// そのマスの地形番号。地形の値の既定（row.terrain）を、原作が選んだタイルに合わせて替える。
///  1. 派生: 店の種別・ゾットの門と像など（地形の値は同じで、タイルで絵が変わる物）
///  2. 床（DNGN_FLOOR）: 雲があれば雲の色つきの床、無ければブランチ・ヴォールトの床
///  3. 壁: ブランチ・ヴォールトの壁
/// 表に無いタイルは既定のまま（捏造しない）。
/// 物（扉・階段・祭壇など、背景が透ける物）の下の床を、そのマスの床にする（原作は `flv.f` に床のタイルを送る）。
/// 物 × 床の組の番号は kDcssUnderBase + 物の位置 * kDcssUnderStride + (床の地形番号 - kDcssFloorBase)。
/// 床が既定（灰色の土）・床の組が表に無い・物が組を持たないときは、物の元の番号のまま。
std::uint16_t with_floor_under(std::uint16_t object_terrain, int flv_floor)
{
    if (flv_floor <= 0 || flv_floor >= kDcssTileFloorMax) return object_terrain;
    const std::uint16_t floor_terrain = find_span(kDcssFloorSpans, static_cast<unsigned>(flv_floor));
    if (floor_terrain == 0 || floor_terrain == kDcssFloorGrey) return object_terrain;
    const std::uint16_t *end = kDcssUnderObjects + (sizeof(kDcssUnderObjects) / sizeof(kDcssUnderObjects[0]));
    const std::uint16_t *at = std::lower_bound(kDcssUnderObjects, end, object_terrain);
    if (at == end || *at != object_terrain) return object_terrain;
    return static_cast<std::uint16_t>(kDcssUnderBase + static_cast<unsigned>(at - kDcssUnderObjects) * kDcssUnderStride + (floor_terrain - kDcssFloorBase));
}

std::uint16_t terrain_for(const MapCell &c, const DcssTerrainRow &row)
{
    const unsigned tile = static_cast<unsigned>(c.bg & kTileMask);
    for (const DcssTileSpan &s : kDcssDerivedSpans) {
        if (tile >= s.first && tile <= s.last && (s.feat < 0 || s.feat == c.f)) return with_floor_under(s.terrain, c.flv_floor);
    }
    if (c.f == kFeatFloor) {
        if (tile < static_cast<unsigned>(kDcssTileFloorMax)) {
            if (const std::uint16_t t = find_span(kDcssFloorSpans, tile)) return t;
        }
    } else if ((row.flags & CELL_FEAT_WALL) != 0 && tile >= static_cast<unsigned>(kDcssTileFloorMax) && tile < static_cast<unsigned>(kDcssTileWallMax)) {
        if (const std::uint16_t t = find_span(kDcssWallSpans, tile)) return t;
    }
    return with_floor_under(row.terrain, c.flv_floor);
}

// ---- 取り込み ----

MapCell &cell_at(int x, int y, bool &ok)
{
    static MapCell dummy;
    const int ax = x + kOffX;
    const int ay = y + kOffY;
    ok = ax >= 0 && ax < kMapW && ay >= 0 && ay < kMapH;
    return ok ? g.cells[static_cast<std::size_t>(ay) * kMapW + static_cast<std::size_t>(ax)] : dummy;
}

void on_map(const json &m)
{
    if (m.value("clear", false)) {
        for (auto &c : g.cells) c = MapCell{};
        g.floor_generation++;
        g.cursor_x = g.cursor_y = -1;
        g.pos_known = false;
    }
    g.map_received = true;
    const auto cells = m.find("cells");
    if (cells == m.end() || !cells->is_array()) return;
    int x = 0;
    int y = 0;
    bool first = true;
    for (const json &c : *cells) {
        if (!c.is_object()) continue;
        if (c.contains("x") || c.contains("y")) {
            x = static_cast<int>(jn(c, "x", x));
            y = static_cast<int>(jn(c, "y", y));
        } else if (!first) {
            ++x;
        }
        first = false;
        bool ok = false;
        MapCell &cell = cell_at(x, y, ok);
        if (ok) {
            if (c.contains("f")) cell.f = static_cast<int>(jn(c, "f"));
            if (c.contains("mf")) cell.mf = static_cast<int>(jn(c, "mf"));
            if (c.contains("g") && c["g"].is_string()) cell.g = c["g"].get<std::string>();
            if (c.contains("col")) cell.col = static_cast<int>(jn(c, "col")) & 15;
            if (c.contains("flc")) cell.flash = static_cast<int>(jn(c, "flc")) & 15;
            if (c.contains("fla")) cell.flash_alpha = static_cast<int>(jn(c, "fla"));
            if (c.contains("mon")) {
                const json &mon = c["mon"];
                if (mon.is_object()) {
                    cell.has_mon = true;
                    cell.mon_id = static_cast<int>(jn(mon, "id"));
                    cell.mon_type = static_cast<int>(jn(mon, "type"));
                    cell.mon_att = static_cast<int>(jn(mon, "att"));
                } else {
                    cell.has_mon = false;
                }
            }
            if (c.contains("t") && c["t"].is_object()) {
                const json &t = c["t"];
                if (t.contains("bg")) cell.bg = tile_value(t, "bg");
                if (t.contains("fg")) {
                    // fg が変わると、下地・流派の印は同時に送り直される（無ければ無し）。
                    cell.fg = tile_value(t, "fg");
                    cell.base = 0;
                    cell.overlay1 = cell.overlay2 = 0;
                }
                if (t.contains("base")) cell.base = tile_value(t, "base");
                if (t.contains("overlay1")) cell.overlay1 = tile_value(t, "overlay1");
                if (t.contains("overlay2")) cell.overlay2 = tile_value(t, "overlay2");
                if (t.contains("cloud")) cell.cloud = tile_value(t, "cloud");
                if (t.contains("ov")) {
                    // 画像ページ main の範囲だけ拾う（原作の cell_renderer.js は FEAT_MAX <= ov < MAIN_MAX を、ビーム・矢・爆発の絵として雲の上に描く）。
                    cell.fx.clear();
                    cell.fx_by_player = !dcss_adapter_world_reacting();
                    const json &d = t["ov"];
                    const int lo = g_bank.atlas().ok() ? g_bank.atlas().feat_max : kFxFeatMax;
                    const int hi = g_bank.atlas().ok() ? g_bank.atlas().main_max : kFxMainMax;
                    if (d.is_array()) {
                        for (const json &v : d) {
                            if (!v.is_number()) continue;
                            const int tile = v.get<int>();
                            if (tile >= lo && tile < hi) cell.fx.push_back(tile);
                        }
                    }
                }
                if (t.contains("icons")) {
                    cell.icons.clear();
                    const json &d = t["icons"];
                    if (d.is_array()) {
                        for (const json &part : d) {
                            if (part.is_number()) cell.icons.push_back(part.get<int>());
                            else if (part.is_array() && !part.empty() && part[0].is_number()) cell.icons.push_back(part[0].get<int>());
                        }
                    }
                }
                if (t.contains("highlighted_summoner")) cell.summoner = t["highlighted_summoner"].is_boolean() && t["highlighted_summoner"].get<bool>();
                if (t.contains("flv") && t["flv"].is_object()) cell.flv_floor = static_cast<int>(jn(t["flv"], "f"));
                if (t.contains("trans")) cell.trans = jn(t, "trans") != 0;
                else if (t.contains("doll")) cell.trans = false;
                if (t.contains("doll")) {
                    cell.doll.clear();
                    const json &d = t["doll"];
                    if (d.is_array()) {
                        for (const json &part : d) {
                            if (part.is_array() && part.size() >= 2 && part[0].is_number() && part[1].is_number()) {
                                cell.doll.push_back({ part[0].get<int>(), part[1].get<int>() });
                            }
                        }
                    }
                }
                if (t.contains("mcache")) {
                    cell.mcache.clear();
                    const json &d = t["mcache"];
                    if (d.is_array()) {
                        for (const json &part : d) {
                            if (part.is_array() && part.size() >= 3 && part[0].is_number() && part[1].is_number() && part[2].is_number()) {
                                cell.mcache.push_back({ part[0].get<int>(), part[1].get<int>(), part[2].get<int>() });
                            }
                        }
                    }
                }
            }
        }
    }
}

void on_player(const json &m)
{
    for (auto it = m.begin(); it != m.end(); ++it) {
        const std::string &k = it.key();
        const json &v = it.value();
        if (k == "msg") continue;
        if (k == "pos" && v.is_object()) {
            g.pos_x = static_cast<int>(jn(v, "x"));
            g.pos_y = static_cast<int>(jn(v, "y"));
            g.pos_known = true;
        } else if (k == "status" && v.is_array()) {
            g.status = v;
        } else if (v.is_number_integer() || v.is_number_unsigned()) {
            g.num[k] = v.is_number_unsigned() ? static_cast<std::int64_t>(v.get<std::uint64_t>()) : v.get<std::int64_t>();
        } else if (v.is_boolean()) {
            g.num[k] = v.get<bool>() ? 1 : 0;
        } else if (v.is_string()) {
            g.str[k] = v.get<std::string>();
        }
    }
    const auto name = g.str.find("name");
    if (name != g.str.end() && !name->second.empty()) {
        std::lock_guard<std::mutex> lock(g_name_mutex);
        g_player_name = name->second;
    }
}

void on_msgs(const json &m)
{
    if (m.contains("rollback")) {
        std::int64_t n = jn(m, "rollback");
        while (n-- > 0 && !g.msgs.empty()) g.msgs.pop_back();
    }
    if (m.contains("messages") && m["messages"].is_array()) {
        for (const json &one : m["messages"]) {
            Msg msg;
            msg.seq = ++g.msg_seq;
            msg.text = js(one, "text");
            msg.channel = static_cast<int>(jn(one, "channel"));
            g.msgs.push_back(std::move(msg));
            while (g.msgs.size() > 200) g.msgs.pop_front();
        }
    }
    if (m.contains("more")) g.more = m.value("more", false);
    if (m.contains("more_text")) g.more_text = js(m, "more_text");
}

void on_txt(const json &m)
{
    if (js(m, "id") != "menu_txt" && m.contains("id")) return;
    if (m.value("clear", false)) {
        for (auto &l : g.crt_lines) l.clear();
    }
    const auto lines = m.find("lines");
    if (lines == m.end() || !lines->is_object()) return;
    for (auto it = lines->begin(); it != lines->end(); ++it) {
        const int row = std::atoi(it.key().c_str());
        if (row >= 0 && row < static_cast<int>(g.crt_lines.size()) && it.value().is_string()) {
            g.crt_lines[static_cast<std::size_t>(row)] = it.value().get<std::string>();
        }
    }
}

std::vector<MenuItem> parse_items(const json &items)
{
    std::vector<MenuItem> out;
    if (!items.is_array()) return out;
    for (const json &it : items) {
        MenuItem item;
        item.text = js(it, "text");
        item.colour = static_cast<int>(jn(it, "colour", 7)) & 15;
        item.level = static_cast<int>(jn(it, "level", 2));
        if (it.contains("hotkeys") && it["hotkeys"].is_array()) {
            for (const json &h : it["hotkeys"]) {
                if (h.is_number()) item.hotkeys.push_back(h.get<int>());
            }
        }
        out.push_back(std::move(item));
    }
    return out;
}

Window *top_menu()
{
    for (auto it = g.stack.rbegin(); it != g.stack.rend(); ++it) {
        if (it->kind == Window::Kind::Menu) return &*it;
    }
    return nullptr;
}

Window *top_ui(const std::string &type)
{
    for (auto it = g.stack.rbegin(); it != g.stack.rend(); ++it) {
        if (it->kind == Window::Kind::Ui && (type.empty() || it->type == type)) return &*it;
    }
    return nullptr;
}

void on_menu(const json &m)
{
    Window w;
    if (js(m, "type") == "crt") {
        w.kind = Window::Kind::Crt;
        for (auto &l : g.crt_lines) l.clear();
    } else {
        w.kind = Window::Kind::Menu;
        w.tag = js(m, "tag");
        if (m.contains("title") && m["title"].is_object()) w.title = js(m["title"], "text");
        w.more = js(m, "more");
        w.items = parse_items(m.value("items", json::array()));
        w.hovered = static_cast<int>(jn(m, "last_hovered", -1));
    }
    w.generation = ++g.generation;
    g.stack.push_back(std::move(w));
}

void pop_menu_family()
{
    for (auto it = g.stack.rbegin(); it != g.stack.rend(); ++it) {
        if (it->kind != Window::Kind::Ui) {
            g.stack.erase(std::next(it).base());
            return;
        }
    }
}

void on_ui_push(const json &m)
{
    Window w;
    w.kind = Window::Kind::Ui;
    w.type = js(m, "type");
    w.data = m;
    w.state = json::object();
    w.generation = ++g.generation;
    if (w.type == "newgame-choice" || w.type == "newgame-random-combo") g.creation_started = true;
    g.stack.push_back(std::move(w));
}

void on_ui_pop()
{
    for (auto it = g.stack.rbegin(); it != g.stack.rend(); ++it) {
        if (it->kind == Window::Kind::Ui) {
            g.stack.erase(std::next(it).base());
            return;
        }
    }
}

void on_ui_state(const json &m)
{
    Window *w = top_ui(js(m, "type"));
    if (w == nullptr) return;
    for (auto it = m.begin(); it != m.end(); ++it) {
        if (it.key() == "msg" || it.key() == "type") continue;
        w->state[it.key()] = it.value();
    }
}

void on_init_input(const json &m)
{
    g.input = TextInput{};
    g.input.active = true;
    g.input.prompt = js(m, "prompt");
    g.input.prefill = js(m, "prefill");
    g.input.typed = g.input.prefill;
    g.input.replace_on_first_key = m.value("select_prefill", false);
    g.input.maxlen = static_cast<int>(jn(m, "maxlen"));
    std::lock_guard<std::mutex> lock(g_typed_mutex);
    g_typed_delta.clear();
}

} // namespace

namespace dcss_frame {

void ingest(const std::string &raw)
{
    std::string line = raw;
    bool control = false;
    if (!line.empty() && line[0] == '*') {
        control = true;
        line.erase(0, 1);
    }
    sanitize_utf8(line);
    json m = json::parse(line, nullptr, false);
    if (m.is_discarded() || !m.is_object()) return;
    const std::string kind = js(m, "msg");
    if (control) {
        if (kind == "exit_reason") {
            g.exit_reason = js(m, "type");
            std::lock_guard<std::mutex> lock(g_name_mutex);
            g_exit_reason = g.exit_reason;
        }
        return;
    }
    if (kind == "map") on_map(m);
    else if (kind == "options") {
        const auto opts = m.find("options");
        if (opts != m.end() && opts->is_object() && opts->contains("tile_show_demon_tier")) g.demon_tier = jn(*opts, "tile_show_demon_tier") != 0;
    } else if (kind == "player") on_player(m);
    else if (kind == "msgs") on_msgs(m);
    else if (kind == "input_mode") g.input_mode = static_cast<int>(jn(m, "mode"));
    else if (kind == "ui_state") g.ui_state = static_cast<int>(jn(m, "state"));
    else if (kind == "cursor") {
        if (jn(m, "id", -1) == 0) {
            if (m.contains("loc") && m["loc"].is_object()) {
                g.cursor_x = static_cast<int>(jn(m["loc"], "x")) + kOffX;
                g.cursor_y = static_cast<int>(jn(m["loc"], "y")) + kOffY;
            } else {
                g.cursor_x = g.cursor_y = -1;
            }
        }
    } else if (kind == "txt") on_txt(m);
    else if (kind == "menu") on_menu(m);
    else if (kind == "update_menu") {
        if (Window *w = top_menu()) {
            if (m.contains("last_hovered")) w->hovered = static_cast<int>(jn(m, "last_hovered", -1));
            if (m.contains("title") && m["title"].is_object()) w->title = js(m["title"], "text");
            if (m.contains("more")) w->more = js(m, "more");
        }
    } else if (kind == "update_menu_items") {
        if (Window *w = top_menu()) {
            const std::size_t start = static_cast<std::size_t>(std::max<std::int64_t>(0, jn(m, "chunk_start")));
            std::vector<MenuItem> items = parse_items(m.value("items", json::array()));
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (start + i < w->items.size()) w->items[start + i] = std::move(items[i]);
                else w->items.push_back(std::move(items[i]));
            }
        }
    } else if (kind == "menu_scroll") {
        if (Window *w = top_menu()) {
            w->first = static_cast<int>(jn(m, "first"));
            if (m.contains("last_hovered")) w->hovered = static_cast<int>(jn(m, "last_hovered", -1));
        }
    } else if (kind == "close_menu") pop_menu_family();
    else if (kind == "close_all_menus") {
        g.stack.erase(std::remove_if(g.stack.begin(), g.stack.end(), [](const Window &w) { return w.kind != Window::Kind::Ui; }), g.stack.end());
    } else if (kind == "ui-push") on_ui_push(m);
    else if (kind == "ui-pop") on_ui_pop();
    else if (kind == "ui-state") on_ui_state(m);
    else if (kind == "init_input") on_init_input(m);
    else if (kind == "close_input") {
        g.input = TextInput{};
    }
}

void set_view_cells(int w, int h)
{
    std::lock_guard<std::mutex> lock(g_hint_mutex);
    g_view_w = w;
    g_view_h = h;
}

void set_language(const std::string &lang)
{
    std::lock_guard<std::mutex> lock(g_hint_mutex);
    if (!lang.empty()) g_lang = lang;
}

void set_ui_offered(bool offered) { g_ui_offered_frame.store(offered); }

InputScene input_scene() { return static_cast<InputScene>(g_scene.load()); }

void note_text_key(int keycode)
{
    if (!g_text_active.load()) return;
    std::lock_guard<std::mutex> lock(g_typed_mutex);
    if (keycode == 8 || keycode == 127) {
        // 最後の 1 文字（UTF-8 の符号点）を消す。表示の編集は組むときに適用する（印: 0x08）。
        g_typed_delta.push_back('\b');
    } else if (keycode == 21) { // Ctrl-U
        g_typed_delta.push_back('\x15');
    } else if (keycode >= 32 && keycode != 127) {
        // 符号点を UTF-8 にして足す。
        std::uint32_t cp = static_cast<std::uint32_t>(keycode);
        if (cp < 0x80) g_typed_delta.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            g_typed_delta.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            g_typed_delta.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            g_typed_delta.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            g_typed_delta.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            g_typed_delta.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            g_typed_delta.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            g_typed_delta.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            g_typed_delta.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            g_typed_delta.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
}

std::string player_name()
{
    std::lock_guard<std::mutex> lock(g_name_mutex);
    return g_player_name;
}

std::string exit_reason()
{
    std::lock_guard<std::mutex> lock(g_name_mutex);
    return g_exit_reason;
}

bool game_ended()
{
    const std::string r = exit_reason();
    return r == "dead" || r == "won" || r == "quit" || r == "bailed out";
}

bool init_slabs(const std::string &atlas_dir, const std::string &slab_dir) { return g_bank.init(atlas_dir, slab_dir); }

bool take_new_assets(std::vector<AssetEntry> &all)
{
    std::vector<dcss_slab::Entry> entries;
    if (!g_bank.take_manifest(entries)) return false;
    all.clear();
    all.reserve(entries.size());
    for (const dcss_slab::Entry &e : entries) all.push_back(AssetEntry{ e.index, e.kind, e.id, e.name });
    return true;
}

std::size_t asset_count() { return g_bank.count(); }

int slab_tool(int argc, char **argv)
{
    namespace fs = std::filesystem;
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--dcss-slab-bake" && argc >= 5) {
        // 32x32 の PNG 1 枚から板を作る（名前は指定）。tools/voxel/tile_to_slab.py の出力と突き合わせるため。
        dcss_slab::Image img;
        std::string err;
        if (!dcss_slab::load_png_file(argv[2], img, err) || img.w != dcss_slab::kCell || img.h < dcss_slab::kCell) {
            std::fprintf(stderr, "PNG を読めない（8 ビット RGBA・幅 32 でない）: %s %s\n", argv[2], err.c_str());
            return 2;
        }
        dcss_slab::Canvas canvas;
        canvas.w = img.w;
        canvas.h = img.h;
        canvas.top = dcss_slab::kCell - img.h;
        canvas.rgba = std::move(img.rgba);
        dcss_slab::Baked baked;
        if (!dcss_slab::bake(canvas, argv[3], baked)) {
            std::fprintf(stderr, "不透明な画素が無い\n");
            return 3;
        }
        fs::create_directories(argv[4]);
        const bool ok = dcss_slab::write_file_atomic(fs::path(argv[4]) / (std::string(argv[3]) + ".vox"), baked.vox)
            && dcss_slab::write_file_atomic(fs::path(argv[4]) / (std::string(argv[3]) + ".jsonc"), baked.jsonc);
        std::printf("voxels=%d colors=%d reduced_from=%d\n", baked.voxels, baked.colors, baked.reduced_from);
        return ok ? 0 : 4;
    }
    if (mode == "--dcss-slab-layers" && argc >= 5) {
        // 画像ページの層（tile[,x のずれ,y のずれ,y_max]）から板を作る。名前はハッシュ。
        dcss_slab::Bank bank;
        if (!bank.init(argv[2], argv[3])) return 2;
        std::vector<dcss_slab::Layer> layers;
        for (int i = 4; i < argc; ++i) {
            dcss_slab::Layer l;
            std::sscanf(argv[i], "%d,%d,%d,%d", &l.tile, &l.ofs_x, &l.ofs_y, &l.y_max);
            layers.push_back(l);
        }
        // 半透明の確かめ用: 環境変数 DCSS_SLAB_FADE（1 = 半透明の怪物・2 = 水に浸かる・3 = 両方）
        const char *fade_env = std::getenv("DCSS_SLAB_FADE");
        const int index = bank.resolve('R', 1, layers, fade_env != nullptr ? std::atoi(fade_env) : 0);
        std::vector<dcss_slab::Entry> all;
        bank.take_manifest(all);
        for (const auto &e : all) std::printf("%d %c %d %s\n", e.index, e.kind, e.id, e.name.c_str());
        return index != 0 ? 0 : 3;
    }
    if (mode == "--dcss-slab-all" && argc >= 4) {
        // 画像ページの単独のタイルを全部板にして、数・大きさ・時間を数える（「全部前もって作ると」の見積もり。書いた板は出力先に残る）。
        dcss_slab::Bank bank;
        if (!bank.init(argv[2], argv[3])) return 2;
        const dcss_slab::Atlas &a = bank.atlas();
        const auto t0 = std::chrono::steady_clock::now();
        std::size_t ok = 0, empty = 0;
        for (int tile = a.feat_max; tile < a.player_max; ++tile) {
            if (a.info(tile) == nullptr) continue;
            if (bank.resolve('R', 1, { dcss_slab::Layer{ tile, 0, 0, 0 } }) != 0) ++ok;
            else ++empty;
        }
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::uintmax_t bytes = 0;
        std::size_t files = 0;
        for (const auto &e : fs::directory_iterator(argv[3])) {
            std::error_code ec;
            bytes += e.file_size(ec);
            ++files;
        }
        std::printf("tiles=%zu empty=%zu files=%zu bytes=%llu seconds=%.2f\n", ok, empty, files, static_cast<unsigned long long>(bytes), sec);
        return 0;
    }
    std::fprintf(stderr, "usage: --dcss-slab-bake <png> <name> <out_dir> | --dcss-slab-layers <atlas_dir> <out_dir> <tile[,ox,oy,ymax]>... | --dcss-slab-all <atlas_dir> <out_dir>\n");
    return 1;
}

} // namespace dcss_frame

// ================================================================== フレームを組む

namespace {

//! 行の中の選べる部分 1 つ（メニューの項目、格子の 1 マス）。
struct RChoice {
    int key = 0;
    int span_begin = 0; //!< 行の中のバイト位置
    int span_len = 0;
    int key_begin = 0;
    int key_len = 0;
    std::string label;
};

struct RLine {
    Colored c;
    std::vector<RChoice> choices;
    bool selected = false;
};

RLine plain_line(const std::string &text, int dcss_color = 7)
{
    RLine l;
    l.c = dcss_text::parse_formatted(text, dcss_color);
    return l;
}

void add_wrapped(std::vector<RLine> &out, const std::string &formatted, int width, int dcss_color = 7)
{
    const Colored c = dcss_text::parse_formatted(formatted, dcss_color);
    for (const Colored &line : split_lines(c)) {
        // 色つきの行は折り返さない（長い行は画面側が切る）。折り返しは色の無い字だけ。
        if (line.spans.empty() && display_width(line.text) > width) {
            for (const std::string &w : wrap_plain(line.text, width)) {
                RLine l;
                l.c.text = w;
                l.c.color = line.color;
                out.push_back(std::move(l));
            }
        } else {
            RLine l;
            l.c = line;
            out.push_back(std::move(l));
        }
    }
}

//! 説明系の JSON から読む字を集める（キーの優先順）。
void collect_text(const json &o, std::vector<std::string> &out, int depth)
{
    if (depth > 4) return;
    static const char *const kKeys[] = { "title", "name", "prefix", "body", "description", "desc", "status", "quote", "suffix", "favour",
        "powers_list", "info_table", "wrath", "extra", "information", "features", "changes", "footer", "bar_text", "prompt" };
    if (o.is_string()) {
        out.push_back(o.get<std::string>());
        return;
    }
    if (o.is_array()) {
        for (const json &e : o) collect_text(e, out, depth + 1);
        return;
    }
    if (!o.is_object()) return;
    for (const char *key : kKeys) {
        const auto it = o.find(key);
        if (it == o.end()) continue;
        if (it->is_string()) {
            std::string s = it->get<std::string>();
            const std::size_t ph = s.find("SPELLSET_PLACEHOLDER");
            if (ph != std::string::npos) s.erase(ph, std::strlen("SPELLSET_PLACEHOLDER"));
            if (!s.empty()) out.push_back(s);
        } else if (it->is_array() || it->is_object()) {
            collect_text(*it, out, depth + 1);
        }
    }
    for (const char *key : { "feats", "cards", "spellset", "spells", "buttons" }) {
        const auto it = o.find(key);
        if (it != o.end() && it->is_array()) {
            for (const json &e : *it) {
                if (e.is_object() && e.contains("label") && e["label"].is_string()) out.push_back(e["label"].get<std::string>());
                collect_text(e, out, depth + 1);
            }
        }
    }
}

std::string ui_hint(const char *ja, const char *en) { return lang_is_ja() ? ja : en; }

//! 窓 1 枚を文字の画面に起こす。
void render_window(const Window &w, std::vector<RLine> &out)
{
    constexpr int kWidth = 78;
    if (w.kind == Window::Kind::Crt) {
        int last = -1;
        for (int i = 0; i < static_cast<int>(g.crt_lines.size()); ++i) {
            if (!g.crt_lines[static_cast<std::size_t>(i)].empty()) last = i;
        }
        for (int i = 0; i <= last; ++i) {
            RLine l;
            l.c = dcss_text::parse_html_line(g.crt_lines[static_cast<std::size_t>(i)]);
            out.push_back(std::move(l));
        }
        return;
    }
    if (w.kind == Window::Kind::Menu) {
        if (!w.title.empty()) out.push_back(plain_line(w.title, 15));
        out.push_back(RLine{});
        constexpr int kRows = 18;
        int first = std::max(0, w.first);
        if (w.hovered >= 0) {
            if (w.hovered < first) first = w.hovered;
            if (w.hovered >= first + kRows) first = w.hovered - kRows + 1;
        }
        const int total = static_cast<int>(w.items.size());
        for (int i = first; i < total && i < first + kRows; ++i) {
            const MenuItem &item = w.items[static_cast<std::size_t>(i)];
            RLine l;
            l.c = dcss_text::parse_formatted(item.text, item.colour);
            rtrim_colored(l.c);
            if (item.level == 1) {
                // 見出し
                l.c.color = dcss_text::term_color(item.colour == 7 ? 14 : item.colour);
                l.c.spans.clear();
            }
            if (!item.hotkeys.empty() && item.hotkeys.front() > 0) {
                RChoice choice;
                choice.key = item.hotkeys.front();
                choice.span_begin = 0;
                choice.span_len = static_cast<int>(l.c.text.size());
                const std::size_t p = l.c.text.find_first_not_of(' ');
                if (p != std::string::npos && choice.key < 128 && l.c.text[p] == static_cast<char>(choice.key)) {
                    choice.key_begin = static_cast<int>(p);
                    choice.key_len = 1;
                }
                choice.label = l.c.text;
                l.choices.push_back(std::move(choice));
            }
            l.selected = (i == w.hovered);
            out.push_back(std::move(l));
        }
        if (first > 0 || first + kRows < total) {
            out.push_back(plain_line(ui_hint("（上下で選び、PgUp / PgDn で頁を送る）", "(Up/Down to move, PgUp/PgDn for pages)"), 8));
        }
        out.push_back(RLine{});
        if (!w.more.empty()) {
            for (RLine &l : [&] {
                     std::vector<RLine> tmp;
                     add_wrapped(tmp, w.more, kWidth, 7);
                     return tmp;
                 }()) {
                out.push_back(std::move(l));
            }
        }
        return;
    }

    // ui
    const json &d = w.data;
    if (w.type == "newgame-choice") {
        add_wrapped(out, js(d, "title"), kWidth, 7);
        if (!js(d, "prompt").empty()) add_wrapped(out, js(d, "prompt"), kWidth, 7);
        out.push_back(RLine{});
        const int focus = static_cast<int>(jn(w.state, "button_focus", -1));
        std::string focus_desc;
        // ボタンの字（書式つき）。`labels`（複数）か `label`（1 つ）。
        const auto button_text = [](const json &b) {
            std::string joined;
            if (b.contains("labels") && b["labels"].is_array()) {
                for (const json &l : b["labels"]) {
                    if (!l.is_string()) continue;
                    if (!joined.empty()) joined += " ";
                    joined += l.get<std::string>();
                }
            } else if (b.contains("label") && b["label"].is_string()) {
                joined = b["label"].get<std::string>();
            }
            return joined;
        };
        for (const char *group : { "main-items", "sub-items" }) {
            const auto it = d.find(group);
            if (it == d.end() || !it->is_object()) continue;
            const auto buttons_it = it->find("buttons");
            if (buttons_it == it->end() || !buttons_it->is_array()) continue;
            const json &buttons = *buttons_it;
            // 行（y）ごとに、列（x）の順に並べる。
            std::map<int, std::vector<const json *>> rows;
            for (const json &b : buttons) rows[static_cast<int>(jn(b, "y"))].push_back(&b);
            std::map<int, int> col_width;
            for (auto &kv : rows) {
                std::sort(kv.second.begin(), kv.second.end(), [](const json *a, const json *b) { return jn(*a, "x") < jn(*b, "x"); });
                for (const json *b : kv.second) {
                    const int x = static_cast<int>(jn(*b, "x"));
                    col_width[x] = std::max(col_width[x], display_width(dcss_text::strip_formatted(button_text(*b))));
                }
            }
            for (auto &kv : rows) {
                RLine line;
                int col = 0;
                std::vector<dcss_text::Span> spans;
                for (const json *b : kv.second) {
                    const int x = static_cast<int>(jn(*b, "x"));
                    // 列の頭まで空白で送る。
                    int target = 0;
                    for (const auto &cw : col_width) {
                        if (cw.first < x) target += cw.second + 2;
                    }
                    while (col < target) {
                        line.c.text.push_back(' ');
                        ++col;
                    }
                    Colored lc = dcss_text::parse_formatted(button_text(*b), 7);
                    rtrim_colored(lc);
                    if (lc.text.empty()) continue;
                    const int begin = static_cast<int>(line.c.text.size());
                    line.c.text += lc.text;
                    if (!lc.spans.empty()) {
                        for (const auto &sp : lc.spans) spans.push_back(dcss_text::Span{ begin + sp.begin, sp.len, sp.color });
                    } else {
                        spans.push_back(dcss_text::Span{ begin, static_cast<int>(lc.text.size()), lc.color });
                    }
                    col += display_width(lc.text);
                    const int hotkey = static_cast<int>(jn(*b, "hotkey"));
                    RChoice choice;
                    choice.key = hotkey;
                    choice.span_begin = begin;
                    choice.span_len = static_cast<int>(lc.text.size());
                    if (hotkey > 0 && hotkey < 128 && !lc.text.empty() && lc.text[0] == static_cast<char>(hotkey)) {
                        choice.key_begin = begin;
                        choice.key_len = 1;
                    }
                    choice.label = lc.text;
                    if (hotkey > 0) line.choices.push_back(std::move(choice));
                    if (hotkey == focus) {
                        line.selected = true;
                        focus_desc = js(*b, "description");
                    }
                }
                if (line.c.text.empty()) continue;
                // 隙間（空白）を既定色で埋めて、区間を整える。
                std::vector<dcss_text::Span> filled;
                int pos = 0;
                for (const auto &sp : spans) {
                    if (sp.begin > pos) filled.push_back(dcss_text::Span{ pos, sp.begin - pos, 1 });
                    filled.push_back(sp);
                    pos = sp.begin + sp.len;
                }
                if (filled.size() > 1) line.c.spans = filled;
                line.c.color = spans.empty() ? 1 : spans.front().color;
                out.push_back(std::move(line));
            }
        }
        if (!focus_desc.empty()) {
            out.push_back(RLine{});
            add_wrapped(out, focus_desc, kWidth, 7);
        }
        out.push_back(RLine{});
        out.push_back(plain_line(ui_hint("表の字のキー（例: a）で選ぶ。矢印で動かして Enter で決める。Esc で戻る。",
                                     "Press the key shown (e.g. a). Arrow keys then Enter also work. Esc goes back."),
            8));
        return;
    }
    if (w.type == "formatted-scroller") {
        if (!js(d, "title").empty()) {
            add_wrapped(out, js(d, "title"), kWidth, 15);
            out.push_back(RLine{});
        }
        std::string text = js(d, "text");
        if (w.state.contains("text") && w.state["text"].is_string()) text = w.state["text"].get<std::string>();
        const Colored c = dcss_text::parse_formatted(text, 7);
        std::vector<Colored> lines = split_lines(c);
        const int scroll = static_cast<int>(std::clamp<std::int64_t>(jn(w.state, "scroll", 0), 0, std::max<int>(0, static_cast<int>(lines.size()) - 1)));
        constexpr int kRows = 20;
        for (int i = scroll; i < static_cast<int>(lines.size()) && i < scroll + kRows; ++i) {
            RLine l;
            l.c = lines[static_cast<std::size_t>(i)];
            out.push_back(std::move(l));
        }
        out.push_back(RLine{});
        if (static_cast<int>(lines.size()) > kRows) {
            out.push_back(plain_line(ui_hint("（上下・PgUp / PgDn で頁を送る）", "(Up/Down, PgUp/PgDn to scroll)"), 8));
        }
        if (!js(d, "more").empty()) add_wrapped(out, js(d, "more"), kWidth, 7);
        return;
    }
    // そのほかの窓（説明・神・死亡・進み具合・文字入力など）は、字を集めて段落にする。
    std::vector<std::string> paragraphs;
    collect_text(d, paragraphs, 0);
    if (w.state.contains("body") && w.state["body"].is_string()) paragraphs.push_back(w.state["body"].get<std::string>());
    bool first = true;
    for (const std::string &p : paragraphs) {
        if (p.empty()) continue;
        if (first) {
            add_wrapped(out, p, kWidth, 15);
            first = false;
        } else {
            out.push_back(RLine{});
            add_wrapped(out, p, kWidth, 7);
        }
        if (out.size() > 40) break;
    }
    if (w.type == "progress-bar") {
        const std::string bar = js(w.state, "bar_text");
        if (!bar.empty()) out.push_back(plain_line(bar, 14));
    }
    if (out.empty()) out.push_back(plain_line(w.type, 8));
}

std::uint16_t item_object_id(char glyph)
{
    switch (glyph) {
    case ')': return 43;  // 武器 → 変愚の短剣
    case '(': return 78;  // 矢弾
    case '[': return 103; // 防具
    case '%': return 21;  // 食べ物・死体
    case '?': return 173; // 巻物
    case '!': return 222; // 薬
    case '=': return 132; // 指輪
    case '"': return 163; // 魔除け
    case '/': return 269; // 魔法の杖（原作では wand）
    case '|': return 300; // 杖
    case '+': return 334; // 呪文書
    case '$': return 488; // 金貨
    case '}': return 73;  // 飛び道具
    default: return 391;  // そのほか
    }
}

char glyph_of(const MapCell &c)
{
    if (!c.g.empty() && static_cast<unsigned char>(c.g[0]) >= 0x20 && static_cast<unsigned char>(c.g[0]) < 0x7F) return c.g[0];
    return 0;
}

//! そのマスの前景（怪物・プレイヤー・床のアイテム）を描く層の並び。原作のブラウザ側（cell_renderer.js の draw_foreground・draw_dolls）と同じ決まり:
//!  - fg が画像ページ main の範囲（アイテム・死体など）: 下地（base）→ fg →（呪文書の紙は流派の印 2 枚）
//!  - fg が画像ページ player の範囲（怪物・プレイヤー）: 人形（doll）の部品を順に。mcache に同じ番号があれば、その部品はそのずれで。
//!    fg が mcache の番号以上なら、mcache の部品も順に（武器を持った怪物・ドラコニアンなど）。
//! 怪物・プレイヤーの状態のアイコン（段 3。D15・D23）を重ね物の層として足す。原作の `draw_foreground`（cell_renderer.js:953〜1089）と同じ順・同じずらし幅。
//! 態度の細い円（FRIENDLY ほか）は出さない（足元の輪が同じ意味。D23）。悪魔の等級は原作どおり数字のアイコン（`tile_show_demon_tier` が真のとき）。
//! 怪物の板の一部として焼き込むので、板と同じ向き・同じ位置（右上など）に立体の中で立ち、壁の向こうでは隠れる。
void append_icon_layers(const MapCell &c, std::vector<dcss_slab::Layer> &layers)
{
    const dcss_slab::Atlas &a = g_bank.atlas();
    if (!a.icons_ok()) return;
    const std::uint64_t f = c.fg;
    const auto add = [&](const char *name, int ofs_x = 0) {
        const int tile = a.icon(name);
        if (tile >= 0) layers.push_back(dcss_slab::Layer{ tile, ofs_x, 0, 0, true });
    };
    if ((f & 0x00800000ULL) != 0) add("TILEI_TRAP_NET");
    if ((f & 0x01000000ULL) != 0) add("TILEI_TRAP_WEB");
    if ((f & 0x00040000ULL) != 0) add("TILEI_SOMETHING_UNDER");
    int shift = 0;
    switch (f & 0x00700000ULL) {
    case 0x00400000ULL: add("TILEI_PARALYSED"); shift += 12; break;
    case 0x00100000ULL: add("TILEI_STAB_BRAND"); shift += 12; break;
    case 0x00200000ULL: add("TILEI_UNAWARE"); shift += 7; break;
    case 0x00300000ULL: add("TILEI_FLEEING"); shift += 3; break;
    default: break;
    }
    switch (f & 0x1800000000000000ULL) {
    case 0x0800000000000000ULL: add("TILEI_POISON", -shift); shift += 5; break;
    case 0x1000000000000000ULL: add("TILEI_MORE_POISON", -shift); shift += 5; break;
    case 0x1800000000000000ULL: add("TILEI_MAX_POISON", -shift); shift += 5; break;
    default: break;
    }
    for (const int tile : c.icons) {
        const int size = a.status_icon_size(tile);
        if (size < 0) continue;
        layers.push_back(dcss_slab::Layer{ tile, size == 0 ? 0 : -shift, 0, 0, true });
        shift += size;
    }
    switch (f & 0x1C0000000ULL) {
    case 0x040000000ULL: add("TILEI_MDAM_LIGHTLY_DAMAGED"); break;
    case 0x080000000ULL: add("TILEI_MDAM_MODERATELY_DAMAGED"); break;
    case 0x0C0000000ULL: add("TILEI_MDAM_HEAVILY_DAMAGED"); break;
    case 0x100000000ULL: add("TILEI_MDAM_SEVERELY_DAMAGED"); break;
    case 0x1C0000000ULL: add("TILEI_MDAM_ALMOST_DEAD"); break;
    default: break;
    }
    if (g.demon_tier) {
        switch (f & 0xE00000000ULL) {
        case 0xE00000000ULL: add("TILEI_DEMON_NUM1"); break;
        case 0x800000000ULL: add("TILEI_DEMON_NUM2"); break;
        case 0x600000000ULL: add("TILEI_DEMON_NUM3"); break;
        case 0x400000000ULL: add("TILEI_DEMON_NUM4"); break;
        case 0x200000000ULL: add("TILEI_DEMON_NUM5"); break;
        default: break;
        }
    }
}

//! 板の半透明の種類（原作の cell_renderer.js と同じ条件）。水に浸かる = 背景が水（TILE_FLAG_WATER。浅い水・深い水・溶岩）で、前景が飛んでいない（TILE_FLAG_FLYING）。
//! 半透明の怪物（t.trans）は怪物・プレイヤーの人形にだけ付く。
int fade_of(const MapCell &c, bool doll_entity)
{
    constexpr std::uint64_t kTileFlagWater = 0x04000000ULL;
    constexpr std::uint64_t kTileFlagFlying = 0x00080000ULL;
    int fade = 0;
    if ((c.bg & kTileFlagWater) != 0 && (c.fg & kTileFlagFlying) == 0) fade |= dcss_slab::kFadeSubmerged;
    if (doll_entity && c.trans) fade |= dcss_slab::kFadeTrans;
    return fade;
}

std::vector<dcss_slab::Layer> entity_layers(const MapCell &c, bool with_icons = false)
{
    std::vector<dcss_slab::Layer> layers;
    const dcss_slab::Atlas &a = g_bank.atlas();
    const int fg = static_cast<int>(c.fg & kTileMask);
    if (fg == 0) return layers;
    if (fg >= a.main_max) {
        for (const auto &part : c.doll) {
            dcss_slab::Layer l;
            l.tile = part[0];
            l.y_max = part[1];
            for (const auto &m : c.mcache) {
                if (m[0] == part[0]) {
                    l.ofs_x = m[1];
                    l.ofs_y = m[2];
                    break;
                }
            }
            layers.push_back(l);
        }
        if (fg >= a.mcache_start) {
            for (const auto &m : c.mcache) layers.push_back(dcss_slab::Layer{ m[0], m[1], m[2], 0 });
        } else if (layers.empty() && fg != a.tilep_player) {
            layers.push_back(dcss_slab::Layer{ fg, 0, 0, dcss_slab::kCell }); // doll が届いていない怪物は、番号そのものの 1 枚
        }
    } else if (fg >= a.feat_max) {
        const int base = static_cast<int>(c.base & kTileMask);
        if (base >= a.feat_max && base < a.main_max) layers.push_back(dcss_slab::Layer{ base, 0, 0, 0 });
        layers.push_back(dcss_slab::Layer{ fg, 0, 0, 0 });
        if (fg >= a.parchment_low && fg <= a.parchment_high) {
            for (const std::uint64_t o : { c.overlay1, c.overlay2 }) {
                const int idx = static_cast<int>(o & kTileMask);
                if (idx >= a.feat_max && idx < a.main_max) layers.push_back(dcss_slab::Layer{ idx, 0, 0, 0 });
            }
        }
    }
    if (with_icons && !layers.empty()) append_icon_layers(c, layers);
    return layers;
}

} // namespace

namespace dcss_frame {

void build(GameFrame &frame)
{
    int view_w = 0;
    int view_h = 0;
    {
        std::lock_guard<std::mutex> lock(g_hint_mutex);
        view_w = g_view_w;
        view_h = g_view_h;
    }
    if (view_w <= 0) view_w = 41;
    if (view_h <= 0) view_h = 25;
    view_w = std::min(view_w, kMapW);
    view_h = std::min(view_h, kMapH);

    const std::string name = g.str.count("name") ? g.str["name"] : std::string();
    const bool in_game = g.map_received && !name.empty();
    const bool windows_open = !g.stack.empty();
    const bool text_open = g.input.active;

    // 文字入力の表示用の中身（受信スレッドが覚えた編集を、ここで取り込む）。
    if (text_open) {
        std::string delta;
        {
            std::lock_guard<std::mutex> lock(g_typed_mutex);
            delta.swap(g_typed_delta);
        }
        for (std::size_t i = 0; i < delta.size(); ++i) {
            const char c = delta[i];
            if (c == '\b') {
                while (!g.input.typed.empty() && (static_cast<unsigned char>(g.input.typed.back()) & 0xC0) == 0x80) g.input.typed.pop_back();
                if (!g.input.typed.empty()) g.input.typed.pop_back();
                g.input.replace_on_first_key = false;
            } else if (c == '\x15') {
                g.input.typed.clear();
                g.input.replace_on_first_key = false;
            } else {
                if (g.input.replace_on_first_key) {
                    g.input.typed.clear();
                    g.input.replace_on_first_key = false;
                }
                g.input.typed.push_back(c);
            }
        }
    }
    g_text_active.store(text_open);

    // ---- HUD・状態の列・メッセージ（地図の有無によらず、キャラクターがあれば）----
    const auto num = [&](const char *k) -> std::int64_t {
        const auto it = g.num.find(k);
        return it == g.num.end() ? 0 : it->second;
    };
    const auto str = [&](const char *k) -> std::string {
        const auto it = g.str.find(k);
        return it == g.str.end() ? std::string() : it->second;
    };
    // 画面が窓を自分で描くとき（dcss-ui）は、HD2D の新しい窓（modern_ui）へ読み替えさせない（`prepare_modern_menu_frame` は lightweight の問いを読み替えない）。
    if (g_ui_offered_frame.load()) frame.prompt.lightweight = true;
    frame.hud.name = name.empty() ? std::string("DCSS") : name;
    frame.hud.hp = static_cast<int>(num("hp"));
    frame.hud.hp_max = static_cast<int>(num("hp_max"));
    frame.hud.hp_label = "HP";
    frame.hud.sp = static_cast<int>(num("mp"));
    frame.hud.sp_max = static_cast<int>(num("mp_max"));
    frame.hud.sp_label = "MP";
    frame.hud.gold = static_cast<int>(num("gold"));
    frame.hud.level = static_cast<int>(num("xl"));
    frame.hud.depth = static_cast<int>(num("depth"));
    const std::string place = str("place");
    const std::string place_text = place.empty() ? std::string() : (place + ":" + std::to_string(num("depth")));
    frame.floor.place_name_utf8 = place_text;
    frame.floor.kind = (in_game && !place.empty()) ? static_cast<int>(FloorKind::Dungeon) : static_cast<int>(FloorKind::Unknown);
    frame.floor.dun_level = static_cast<int>(num("depth"));
    frame.floor.generated_turn = g.floor_generation;
    frame.depth.text_utf8 = place_text;
    frame.depth.color = 1;
    frame.lighting.memory_dim = 1;

    if (in_game) {
        const auto add_status = [&](const std::string &text, std::uint8_t color = 1) {
            SubPanelLine line;
            line.text_utf8 = text;
            line.color = color;
            frame.status_col_lines.push_back(std::move(line));
        };
        add_status(name, 1);
        if (!str("title").empty()) add_status(str("title"), 9);
        if (!str("species").empty()) add_status(str("species"), 9);
        if (!str("god").empty()) add_status(str("god"), 11);
        add_status("HP " + std::to_string(num("hp")) + "/" + std::to_string(num("hp_max")), num("hp") * 3 < num("hp_max") ? 12 : 13);
        add_status("MP " + std::to_string(num("mp")) + "/" + std::to_string(num("mp_max")), 14);
        add_status("AC " + std::to_string(num("ac")) + " EV " + std::to_string(num("ev")) + " SH " + std::to_string(num("sh")));
        add_status("Str " + std::to_string(num("str")) + " Int " + std::to_string(num("int")));
        add_status("Dex " + std::to_string(num("dex")));
        add_status("XL " + std::to_string(num("xl")) + " (" + std::to_string(num("progress")) + "%)");
        add_status("$ " + std::to_string(num("gold")), 11);
        add_status(place_text);
        add_status("T " + std::to_string(num("turn")), 2);
        for (const json &s : g.status) {
            if (!s.is_object()) continue;
            const Colored c = dcss_text::parse_formatted(js(s, "text"), static_cast<int>(jn(s, "col", 7)) & 15);
            if (!c.text.empty()) add_status(c.text, c.color);
        }
        frame.status_col_cols = 20;
    }
    {
        std::uint32_t seq0 = 0;
        const std::size_t count = std::min<std::size_t>(g.msgs.size(), 50);
        for (std::size_t i = g.msgs.size() - count; i < g.msgs.size(); ++i) {
            const Msg &m = g.msgs[i];
            const Colored c = dcss_text::parse_formatted(m.text, 7);
            MessageEvent ev;
            ev.seq = m.seq;
            ev.color = c.color;
            ev.text_utf8 = c.text;
            frame.messages.push_back(std::move(ev));
            seq0 = m.seq;
        }
        (void)seq0;
    }

    // ---- 窓（文字の画面）----
    InputScene scene = InputScene::Other;
    if (windows_open || (text_open && !in_game)) {
        std::vector<RLine> lines;
        if (windows_open) render_window(g.stack.back(), lines);
        if (text_open && !g.stack.empty() && g.stack.back().kind != Window::Kind::Crt) {
            lines.push_back(RLine{});
            lines.push_back(plain_line(dcss_text::strip_formatted(g.input.prompt) + g.input.typed + "_", 15));
        }
        frame.menu_open = true;
        frame.pre_game_menu = !in_game;
        frame.menu_over_map = in_game;
        frame.text_input_active = text_open;
        frame.title_screen = !in_game && !g.creation_started && !g.stack.empty() && g.stack.back().kind == Window::Kind::Crt;
        // 死亡・勝利の後の画面（得点表つきの窓）。
        for (const Window &w : g.stack) {
            if (w.kind == Window::Kind::Ui && w.type == "game-over") {
                frame.game_over = true;
                frame.pre_game_menu = true;
                frame.menu_over_map = false;
            }
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            TermMirrorLine l;
            l.text_utf8 = lines[i].c.text;
            l.color = lines[i].c.color;
            l.source_row = static_cast<int>(i);
            for (const auto &s : lines[i].c.spans) l.color_spans.push_back(TermColorSpan{ s.begin, s.len, s.color });
            if (lines[i].selected) {
                l.highlight_begin = 0;
                l.highlight_len = static_cast<int>(lines[i].c.text.size());
            }
            frame.menu_term_lines.push_back(std::move(l));
            for (const RChoice &rc : lines[i].choices) {
                MenuChoice choice;
                choice.line_index = static_cast<int>(i);
                choice.span_begin = rc.span_begin;
                choice.span_len = rc.span_len;
                choice.key_begin = rc.key_begin;
                choice.key_len = rc.key_len;
                choice.key = rc.key;
                choice.label_utf8 = rc.label;
                frame.menu_choices.push_back(std::move(choice));
            }
        }
        if (text_open) {
            frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size()) - 1;
            frame.menu_term_curs_col = display_width(dcss_text::strip_formatted(g.input.prompt)) + display_width(g.input.typed);
        }
        // 起動メニューの名前の入力欄。原作は文字画面（txt）の 1 行に入力を映すだけで、init_input も出さない。
        // 欄は「名前を入力:」（英語は「Enter your name:」）で始まる行なので、そこを入力中の行にする。
        bool crt_name_entry = false;
        if (!text_open && windows_open && g.stack.back().kind == Window::Kind::Crt) {
            for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
                const std::string &t = frame.menu_term_lines[i].text_utf8;
                if (t.rfind("名前を入力", 0) == 0 || t.rfind("Enter your name", 0) == 0) {
                    crt_name_entry = true;
                    frame.text_input_active = true;
                    frame.menu_term_curs_row = static_cast<int>(i);
                    frame.menu_term_curs_col = display_width(t);
                    frame.menu_choices.clear();
                    break;
                }
            }
        }
        scene = (text_open || crt_name_entry) ? InputScene::Text : InputScene::Window;
        g_scene.store(static_cast<int>(scene));
        // 画面が窓を自分で描くとき（dcss-ui）は、字の行と選択肢を載せない（窓が開いている・文字入力中などの旗だけ残す）。
        // 原作は窓の下にも地図を描き続けるので、地図の部分へ進む（窓を描くのは画面の部品）。
        if (g_ui_offered_frame.load()) {
            frame.menu_term_lines.clear();
            frame.menu_choices.clear();
            frame.menu_term_curs_row = -1;
            frame.menu_term_curs_col = -1;
        } else {
            return;
        }
    }

    // ---- 地図 ----
    const int px = g.pos_x + kOffX;
    const int py = g.pos_y + kOffY;
    if (in_game) {
        frame.player_gx = px;
        frame.player_gy = py;

        MinimapSnapshot &mm = frame.minimap;
        mm.width = kMapW;
        mm.height = kMapH;
        mm.player_gx = px;
        mm.player_gy = py;
        mm.kinds.assign(static_cast<std::size_t>(kMapW) * kMapH, 0);
        mm.markers.assign(mm.kinds.size(), 0);
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                const MapCell &c = g.cells[static_cast<std::size_t>(y) * kMapW + static_cast<std::size_t>(x)];
                if (c.f <= 0 || c.f >= kDcssFeatureCount) continue;
                const std::size_t at = static_cast<std::size_t>(y) * kMapW + static_cast<std::size_t>(x);
                mm.kinds[at] = static_cast<std::uint8_t>(kDcssTerrain[c.f].kind);
                if (c.has_mon && (c.bg & kTileFlagUnseen) == 0) mm.markers[at] = static_cast<std::uint8_t>(MinimapMarker::Monster);
                else if (c.mf == kMfItem) mm.markers[at] = static_cast<std::uint8_t>(MinimapMarker::Item);
            }
        }
        if (px >= 0 && px < kMapW && py >= 0 && py < kMapH) {
            mm.kinds[static_cast<std::size_t>(py) * kMapW + static_cast<std::size_t>(px)] = static_cast<std::uint8_t>(MinimapKind::Player);
        }

        const int ox = std::clamp(px - view_w / 2, 0, std::max(0, kMapW - view_w));
        const int oy = std::clamp(py - view_h / 2, 0, std::max(0, kMapH - view_h));
        frame.cam_x = ox + view_w / 2;
        frame.cam_y = oy + view_h / 2;
        frame.view_w = view_w;
        frame.view_h = view_h;
        frame.cells.reserve(static_cast<std::size_t>(view_w) * static_cast<std::size_t>(view_h));
        // 明かりの 3 段階。いま見えているマスのビット列（`cells` と同じ並び・下位ビットから）。
        std::vector<std::uint8_t> lit_bits((static_cast<std::size_t>(view_w) * static_cast<std::size_t>(view_h) + 7u) / 8u, 0u);
        for (int gy = oy; gy < oy + view_h; ++gy) {
            for (int gx = ox; gx < ox + view_w; ++gx) {
                const MapCell &c = g.cells[static_cast<std::size_t>(gy) * kMapW + static_cast<std::size_t>(gx)];
                const std::size_t cell_index = frame.cells.size();
                MapCellView cell{};
                cell.gx = static_cast<std::int16_t>(gx);
                cell.gy = static_cast<std::int16_t>(gy);
                const bool is_player = gx == px && gy == py;
                if (c.f <= 0 || c.f >= kDcssFeatureCount) {
                    if (!is_player) {
                        frame.cells.push_back(cell); // 未探知
                        continue;
                    }
                }
                const DcssTerrainRow row = (c.f > 0 && c.f < kDcssFeatureCount) ? kDcssTerrain[c.f] : kDcssTerrain[33];
                cell.terrain_id = terrain_for(c, row);
                cell.feature_flags = static_cast<std::uint16_t>(row.flags | CELL_FEAT_KNOWN);
                // 見えている = いまの視界の中（原作の旗 UNSEEN・MM_UNSEEN がどちらも立っていない）。プレイヤー自身のマスは常に見えている。
                const bool visible = is_player || ((c.bg & kTileFlagUnseen) == 0 && (c.bg & kTileFlagMmUnseen) == 0);
                // 段 1 = 見えている（light_level 2 + player_lit）／段 2 = 分かっているが見えていない（1。memory_dim で薄暗く）／段 3 = 分かっていない（0。上で未探知のまま送る）。
                cell.light_level = visible ? 2 : 1;
                if (visible) lit_bits[cell_index >> 3] = static_cast<std::uint8_t>(lit_bits[cell_index >> 3] | (1u << (cell_index & 7u)));
                const char glyph = glyph_of(c);
                cell.ascii_fallback = glyph;
                cell.fg_color = dcss_text::term_color(c.col);
                if (is_player) {
                    cell.feature_flags |= CELL_FEAT_PLAYER;
                    cell.ascii_fallback = '@';
                    cell.tile_index = static_cast<std::uint16_t>(g_bank.resolve('P', 0, entity_layers(c, true), fade_of(c, true)));
                } else if (c.has_mon && visible) {
                    cell.monster_slot = static_cast<std::uint16_t>(c.mon_id & 0xFFFF);
                    cell.monster_id = static_cast<std::uint16_t>(1 + (c.mon_type % 60000));
                    if (cell.monster_slot == 0) cell.monster_slot = 1;
                    if (cell.ascii_fallback == 0) cell.ascii_fallback = 'M';
                    cell.tile_index = static_cast<std::uint16_t>(g_bank.resolve('R', cell.monster_id, entity_layers(c, true), fade_of(c, true)));
                } else if ((c.mf == kMfItem || (g_bank.atlas().ok() && (c.fg & kTileMask) >= static_cast<std::uint64_t>(g_bank.atlas().feat_max)
                                                   && (c.fg & kTileMask) < static_cast<std::uint64_t>(g_bank.atlas().main_max)))
                           && (c.fg & 0xFFFF) != 0) {
                    // 地形の特徴（祭壇など）の上の物は、原作の地図の印（mf）が特徴のままで物にならないので、前景がアイテムのタイル（画像ページ main）かで決める。
                    const int index = g_bank.resolve('K', 0, entity_layers(c), fade_of(c, false));
                    cell.tile_index = static_cast<std::uint16_t>(index);
                    cell.object_id = index != 0 ? static_cast<std::uint16_t>(index) : item_object_id(glyph); // 板があれば、その番号がアイテムの番号（目録の 'K' の id）
                    if (cell.ascii_fallback == 0) cell.ascii_fallback = '*';
                } else {
                    cell.ascii_fallback = 0;
                }
                frame.cells.push_back(cell);
            }
        }
        // 原作には光源のアイテムが無く、見える範囲は視界の半径（LOS_DEFAULT_RANGE = 7）で決まる。
        // 見えているマスを「プレイヤの光で照らされたマス」として送り、ほかのコアの光源の範囲と同じ明るさで描かせる。
        // 光の種類 4 = 装備によらない光（淡い白。揺らがない）。
        frame.lighting.player_lit = std::move(lit_bits);
        frame.lighting.light_radius = kVisionRadius;
        frame.lighting.light_kind = kLightKindOther;
        if (g.cursor_x >= 0 && (g.input_mode == 2 || g.input_mode == 3 || g.input_mode == 4)) {
            frame.target_gx = static_cast<std::int16_t>(g.cursor_x);
            frame.target_gy = static_cast<std::int16_t>(g.cursor_y);
        }
    }

    // ---- 問い ----
    const std::string last_msg = g.msgs.empty() ? std::string() : dcss_text::strip_formatted(g.msgs.back().text);
    if (in_game && g.more) {
        std::string text = g.more_text.empty() ? last_msg : dcss_text::strip_formatted(g.more_text);
        if (text.find("-more-") == std::string::npos && text.find("-続く-") == std::string::npos) text += " -more-";
        frame.prompt.text_utf8 = text;
        frame.prompt.line_index = -1;
        scene = InputScene::Other;
    } else if (in_game && g.input_mode == kModeYesNo) {
        frame.prompt.text_utf8 = last_msg;
        frame.prompt.line_index = -1;
        PromptChoice yes;
        yes.label_utf8 = lang_is_ja() ? "はい" : "Yes";
        yes.key = 'y';
        PromptChoice no;
        no.label_utf8 = lang_is_ja() ? "いいえ" : "No";
        no.key = 'n';
        frame.prompt.choices.push_back(std::move(yes));
        frame.prompt.choices.push_back(std::move(no));
        scene = InputScene::Other;
    } else if (in_game && text_open) {
        // 地図の上での 1 行の文字入力（刻印・検索など）。小さな窓に出す。
        frame.menu_open = true;
        frame.menu_over_map = true;
        frame.menu_popup = true;
        frame.text_input_active = true;
        // 入力欄の前置き（init_input の prompt）が空なら、問いはメッセージ欄にある（確認の問いなど）。その最新の 1 行を問いの文として添える。
        const std::string prompt_text = dcss_text::strip_formatted(g.input.prompt);
        if (prompt_text.empty() && !last_msg.empty()) {
            TermMirrorLine q;
            q.text_utf8 = last_msg;
            q.source_row = 0;
            q.color = 1;
            frame.menu_term_lines.push_back(std::move(q));
        }
        TermMirrorLine l;
        l.text_utf8 = prompt_text + g.input.typed;
        l.source_row = static_cast<int>(frame.menu_term_lines.size());
        l.color = 1;
        frame.menu_term_lines.push_back(std::move(l));
        frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size()) - 1;
        frame.menu_term_curs_col = display_width(frame.menu_term_lines.back().text_utf8);
        scene = InputScene::Text;
    } else if (in_game && g.input_mode == kModeCommand) {
        frame.awaiting_command = true;
        scene = InputScene::Command;
    } else {
        scene = InputScene::Other;
    }
    // 画面が窓・文字入力を自分で描くとき（dcss-ui）は、字の行と選択肢を載せない（旗だけ残す）。
    if (g_ui_offered_frame.load()) {
        frame.menu_term_lines.clear();
        frame.menu_choices.clear();
        frame.menu_popup = false;
        frame.menu_term_curs_row = -1;
        frame.menu_term_curs_col = -1;
    }
    frame.camera_detached = false;
    (void)kModeNormal;
    (void)kModeMore;
    (void)kModePrompt;
    (void)kMfPlayer;
    g_scene.store(static_cast<int>(scene));
}

//! 地図の上の印（段 3。D15・D16）。見えている怪物とプレイヤーのマスの、前景の旗と状態のアイコン（原作が `t.fg`・`t.icons` で送る物）。
//! 並び: { "t":"dcss_ui", "k":"marks", "m":[[gx, gy, 旗の下位32ビット, 旗の上位32ビット, [アイコンのタイル番号…], 召喚者の強調(0|1), プレイヤー(0|1)], …] }。
//! 何も無ければ空の文字列。輪の色は画面が原作の `cell_renderer.js` と同じ規則で決める（状態のアイコンはコアが怪物の板に焼き込むので、`icons` は検査用）。
//! 雲（段 3・D14）: `"c":[[gx, gy, 雲のタイル番号, 視界を遮るか(0|1)], …]`。見えているマスの雲だけ。遮るかは原作の内部（`is_opaque_cloud`）から読む。
//! 攻撃の演出（段 4）: `"f":[[gx, gy, [タイル番号…], プレイヤーの行動の最中か(0|1)], …]`＝ビームの弾・矢・投げた物・爆発の絵（原作が `t.ov` に載せる画像ページ main のタイル。`delay` のたびに今ある物が届く）、
//! `"fl":[[gx, gy, 色の番号 0〜15, 濃さ 0〜255（0 = 色の表の既定）], …]`＝閃光（`flc`・`fla`）。どちらも無ければ空の配列。
std::string marks_json()
{
    if (!g.map_received || !g.pos_known) return std::string();
    const int px = g.pos_x + kOffX;
    const int py = g.pos_y + kOffY;
    json list = json::array();
    json clouds = json::array();
    json fx = json::array();
    json flashes = json::array();
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const MapCell &c = g.cells[static_cast<std::size_t>(y) * kMapW + static_cast<std::size_t>(x)];
            const bool is_player = (x == px && y == py);
            if (!c.fx.empty()) fx.push_back(json::array({ x, y, c.fx, c.fx_by_player ? 1 : 0 }));
            if (c.flash != 0) flashes.push_back(json::array({ x, y, c.flash, c.flash_alpha }));
            if ((c.cloud & kTileMask) != 0 && (c.bg & kTileFlagUnseen) == 0 && (c.bg & kTileFlagMmUnseen) == 0) {
                clouds.push_back(json::array({ x, y, static_cast<int>(c.cloud & kTileMask), dcss_adapter_cloud_kind(x - px, y - py) == 1 ? 1 : 0 }));
            }
            const bool is_mon = c.has_mon && (c.bg & kTileFlagUnseen) == 0 && (c.bg & kTileFlagMmUnseen) == 0;
            if (!is_player && !is_mon) continue;
            const std::uint64_t flags = c.fg & ~kTileMask; // 下位 16 ビットはタイル番号
            if (flags == 0 && c.icons.empty() && !c.summoner) continue;
            json icons = json::array();
            for (const int i : c.icons) icons.push_back(i);
            list.push_back(json::array({ x, y, static_cast<std::uint32_t>(flags & 0xFFFFFFFFULL), static_cast<std::uint32_t>(flags >> 32),
                std::move(icons), c.summoner ? 1 : 0, is_player ? 1 : 0 }));
        }
    }
    json out;
    out["t"] = "dcss_ui";
    out["k"] = "marks";
    out["m"] = std::move(list);
    out["c"] = std::move(clouds);
    out["f"] = std::move(fx);
    out["fl"] = std::move(flashes);
    out["p"] = json::array({ px, py }); // プレイヤーのマス（攻撃の演出で、弾の飛ぶ向きを決める手がかり）
    return out.dump(-1, ' ', false, json::error_handler_t::replace);
}

} // namespace dcss_frame

// ---- タブの欄の怪物の絵（段 U2。dcss_tabs_data.h が問い合わせる）----

#include "dcss_tabs_bridge.h"

namespace dcss_tabs_bridge {

bool entity_layers_at(int dx, int dy, std::vector<std::array<int, 4>> &layers, int &rel_x, int &rel_y)
{
    layers.clear();
    rel_x = g.pos_x + dx;
    rel_y = g.pos_y + dy;
    bool ok = false;
    const MapCell &c = cell_at(rel_x, rel_y, ok);
    if (!ok) return false;
    for (const dcss_slab::Layer &l : entity_layers(c)) layers.push_back({ l.tile, l.ofs_x, l.ofs_y, l.y_max });
    return true;
}

} // namespace dcss_tabs_bridge
