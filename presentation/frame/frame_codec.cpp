/*!
 * @file frame_codec.cpp
 * @brief `GameFrame` ⇔ プロトコル v1 frame ワイヤ形式の実装。
 *
 * 依存は `frame/` のヘッダと JSON ライブラリだけ（中立性は frame_codec.h 冒頭のとおり）。
 */
#include "frame/frame_codec.h"

#include <nlohmann/json.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace presentation {

namespace {

using json = nlohmann::json;

/*
 * ============================================================================
 * base64（v1 §6.3。ミニマップの kinds はここを通る）
 * ============================================================================
 */
constexpr const char *kB64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//! 逆引き表。-1 = アルファベット外。`=` は呼び側で別扱いにする。
const signed char *b64_reverse_table()
{
    static signed char table[256];
    static bool built = false;
    if (!built) {
        std::memset(table, -1, sizeof(table));
        for (int i = 0; i < 64; ++i) {
            table[static_cast<unsigned char>(kB64Alphabet[i])] = static_cast<signed char>(i);
        }
        built = true;
    }
    return table;
}

std::string sfmt(const char *fmt, ...)
{
    char buf[512]{};
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

/*
 * ============================================================================
 * encode 側の小道具
 * ============================================================================
 * 既定値なら**書かない**。v1 §6.1 の省略規則を「してよい」ではなく「する」に固定して
 * いるのは、揺れると同じ値から 2 通りのバイト列が出て正準性が壊れるため。
 */
template <class T>
void put_num(json &o, const char *key, T value, T def)
{
    if (value != def) {
        o[key] = value;
    }
}

void put_str(json &o, const char *key, const std::string &value, const std::string &def)
{
    if (value != def) {
        o[key] = value;
    }
}

void put_bool(json &o, const char *key, bool value, bool def)
{
    if (value != def) {
        o[key] = value;
    }
}

//! 空でないときだけ親へ挿す（＝「構造体まるごと既定なら省略」）。
void put_obj(json &parent, const char *key, json &&child)
{
    if (!child.empty()) {
        parent[key] = std::move(child);
    }
}

/*
 * ============================================================================
 * decode 側の小道具
 * ============================================================================
 * キーが無ければ**触らない**（out は構造体の既定値で初期化済み。v1 §6.1）。
 * 型が違えばプロトコル違反として失敗させる（未知“キー”の無視（§2.3）とは別の話）。
 */
template <class T>
bool rd_num(const json &o, const char *key, T &dst, std::string &err)
{
    const auto it = o.find(key);
    if (it == o.end()) {
        return true;
    }
    if (!it->is_number_integer()) {
        err = sfmt("\"%s\" is not an integer", key);
        return false;
    }
    dst = it->get<T>();
    return true;
}

bool rd_char(const json &o, const char *key, char &dst, std::string &err)
{
    int tmp = static_cast<int>(dst);
    if (!rd_num(o, key, tmp, err)) {
        return false;
    }
    dst = static_cast<char>(tmp);
    return true;
}

bool rd_bool(const json &o, const char *key, bool &dst, std::string &err)
{
    const auto it = o.find(key);
    if (it == o.end()) {
        return true;
    }
    if (!it->is_boolean()) {
        err = sfmt("\"%s\" is not a boolean", key);
        return false;
    }
    dst = it->get<bool>();
    return true;
}

bool rd_str(const json &o, const char *key, std::string &dst, std::string &err)
{
    const auto it = o.find(key);
    if (it == o.end()) {
        return true;
    }
    if (!it->is_string()) {
        err = sfmt("\"%s\" is not a string", key);
        return false;
    }
    dst = it->get<std::string>();
    return true;
}

//! float は double 経由。`nlohmann` は double へ戻して一致する最短表記を書くので往復無損失。
bool rd_float(const json &o, const char *key, float &dst, std::string &err)
{
    const auto it = o.find(key);
    if (it == o.end()) {
        return true;
    }
    if (!it->is_number()) {
        err = sfmt("\"%s\" is not a number", key);
        return false;
    }
    dst = static_cast<float>(it->get<double>());
    return true;
}

//! オブジェクトの子を取り出す。無ければ nullptr（＝既定のまま）。型違いは失敗。
const json *sub_obj(const json &o, const char *key, std::string &err, bool &ok)
{
    ok = true;
    const auto it = o.find(key);
    if (it == o.end()) {
        return nullptr;
    }
    if (!it->is_object()) {
        err = sfmt("\"%s\" is not an object", key);
        ok = false;
        return nullptr;
    }
    return &(*it);
}

const json *sub_arr(const json &o, const char *key, std::string &err, bool &ok)
{
    ok = true;
    const auto it = o.find(key);
    if (it == o.end()) {
        return nullptr;
    }
    if (!it->is_array()) {
        err = sfmt("\"%s\" is not an array", key);
        ok = false;
        return nullptr;
    }
    return &(*it);
}

/*
 * ============================================================================
 * 部品ごとの encode / decode
 * ============================================================================
 */

// ---- SubPanelLine（status_col_lines / depth / sub_panels[].lines） ----
json enc_sub_panel_line(const SubPanelLine &v)
{
    const SubPanelLine def{};
    json o = json::object();
    put_str(o, "text_utf8", v.text_utf8, def.text_utf8);
    put_num<uint8_t>(o, "color", v.color, def.color);
    return o;
}

bool dec_sub_panel_line(const json &o, SubPanelLine &v, std::string &err)
{
    return rd_str(o, "text_utf8", v.text_utf8, err) && rd_num(o, "color", v.color, err);
}

// ---- TermTextRun（bottom_row_runs） ----
json enc_term_text_run(const TermTextRun &v)
{
    const TermTextRun def{};
    json o = json::object();
    put_num(o, "col", v.col, def.col);
    put_str(o, "text_utf8", v.text_utf8, def.text_utf8);
    put_num<uint8_t>(o, "color", v.color, def.color);
    return o;
}

bool dec_term_text_run(const json &o, TermTextRun &v, std::string &err)
{
    return rd_num(o, "col", v.col, err) && rd_str(o, "text_utf8", v.text_utf8, err) &&
           rd_num(o, "color", v.color, err);
}

// ---- TermColorSpan（menu_term_lines[].color_spans） ----
json enc_term_color_span(const TermColorSpan &v)
{
    const TermColorSpan def{};
    json o = json::object();
    put_num(o, "begin", v.begin, def.begin);
    put_num(o, "len", v.len, def.len);
    put_num<uint8_t>(o, "color", v.color, def.color);
    return o;
}

bool dec_term_color_span(const json &o, TermColorSpan &v, std::string &err)
{
    return rd_num(o, "begin", v.begin, err) && rd_num(o, "len", v.len, err) &&
           rd_num(o, "color", v.color, err);
}

// ---- TermMirrorLine（menu_term_lines） ----
json enc_term_mirror_line(const TermMirrorLine &v)
{
    const TermMirrorLine def{};
    json o = json::object();
    put_str(o, "text_utf8", v.text_utf8, def.text_utf8);
    put_num<uint8_t>(o, "color", v.color, def.color);
    put_num(o, "source_row", v.source_row, def.source_row);
    if (!v.color_spans.empty()) {
        json arr = json::array();
        for (const TermColorSpan &span : v.color_spans) {
            arr.push_back(enc_term_color_span(span));
        }
        o["color_spans"] = std::move(arr);
    }
    put_num(o, "highlight_begin", v.highlight_begin, def.highlight_begin);
    put_num(o, "highlight_len", v.highlight_len, def.highlight_len);
    return o;
}

bool dec_term_mirror_line(const json &o, TermMirrorLine &v, std::string &err)
{
    bool ok = true;
    if (const json *arr = sub_arr(o, "color_spans", err, ok)) {
        v.color_spans.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            if (!(*arr)[i].is_object() || !dec_term_color_span((*arr)[i], v.color_spans[i], err)) {
                if (err.empty()) {
                    err = "color_spans[] is not an object";
                }
                return false;
            }
        }
    } else if (!ok) {
        return false;
    }
    return rd_str(o, "text_utf8", v.text_utf8, err) && rd_num(o, "color", v.color, err) &&
           rd_num(o, "source_row", v.source_row, err) && rd_num(o, "highlight_begin", v.highlight_begin, err) &&
           rd_num(o, "highlight_len", v.highlight_len, err);
}

/*!
 * @brief MenuChoice。
 * @param def 既定値。`menu_choices` / `menu_core_cursors` の要素は `MenuChoice{}`、
 *   `menu_core_cursor` は `GameFrame` のメンバ初期化子 `{-1,0,0,0,0,0}` を渡す。
 *   省略規則の基準は「受信側が初期化に使う値」なので、両者で違う（v1 §6.1）。
 */
json enc_menu_choice(const MenuChoice &v, const MenuChoice &def)
{
    json o = json::object();
    put_num(o, "line_index", v.line_index, def.line_index);
    put_num(o, "span_begin", v.span_begin, def.span_begin);
    put_num(o, "span_len", v.span_len, def.span_len);
    put_num(o, "key_begin", v.key_begin, def.key_begin);
    put_num(o, "key_len", v.key_len, def.key_len);
    put_num(o, "key", v.key, def.key);
    return o;
}

bool dec_menu_choice(const json &o, MenuChoice &v, std::string &err)
{
    return rd_num(o, "line_index", v.line_index, err) && rd_num(o, "span_begin", v.span_begin, err) &&
           rd_num(o, "span_len", v.span_len, err) && rd_num(o, "key_begin", v.key_begin, err) &&
           rd_num(o, "key_len", v.key_len, err) && rd_num(o, "key", v.key, err);
}

// ---- PromptChoice ----
json enc_prompt_choice(const PromptChoice &v)
{
    const PromptChoice def{};
    json o = json::object();
    put_str(o, "label_utf8", v.label_utf8, def.label_utf8);
    put_num(o, "key", v.key, def.key);
    put_num(o, "begin", v.begin, def.begin);
    put_num(o, "len", v.len, def.len);
    return o;
}

bool dec_prompt_choice(const json &o, PromptChoice &v, std::string &err)
{
    return rd_str(o, "label_utf8", v.label_utf8, err) && rd_num(o, "key", v.key, err) &&
           rd_num(o, "begin", v.begin, err) && rd_num(o, "len", v.len, err);
}

// ---- MapCellView（v1 §6.2 の 17 要素固定順タプル） ----
constexpr size_t kCellTupleArity = 17;

json enc_cell(const MapCellView &c)
{
    json a = json::array();
    a.push_back(c.gx);
    a.push_back(c.gy);
    a.push_back(c.terrain_id);
    a.push_back(c.feature_flags);
    a.push_back(c.monster_id);
    a.push_back(c.monster_slot);
    a.push_back(c.object_id);
    a.push_back(c.light_level);
    a.push_back(c.fg_color);
    a.push_back(c.bg_color);
    a.push_back(static_cast<int>(c.ascii_fallback)); // char は int で運ぶ（負値も通る）
    a.push_back(c.tile_index);
    a.push_back(c.under_tile_index);
    a.push_back(c.graf_fg_row);
    a.push_back(c.graf_fg_col);
    a.push_back(c.graf_bg_row);
    a.push_back(c.graf_bg_col);
    return a;
}

bool dec_cell(const json &a, MapCellView &c, size_t index, std::string &err)
{
    if (!a.is_array() || a.size() != kCellTupleArity) {
        err = sfmt("cells[%u] is not a %u-element tuple", static_cast<unsigned>(index),
            static_cast<unsigned>(kCellTupleArity));
        return false;
    }
    for (size_t i = 0; i < kCellTupleArity; ++i) {
        if (!a[i].is_number_integer()) {
            err = sfmt("cells[%u][%u] is not an integer", static_cast<unsigned>(index), static_cast<unsigned>(i));
            return false;
        }
    }
    c.gx = a[0].get<int16_t>();
    c.gy = a[1].get<int16_t>();
    c.terrain_id = a[2].get<uint16_t>();
    c.feature_flags = a[3].get<uint16_t>();
    c.monster_id = a[4].get<uint16_t>();
    c.monster_slot = a[5].get<uint16_t>();
    c.object_id = a[6].get<uint16_t>();
    c.light_level = a[7].get<uint8_t>();
    c.fg_color = a[8].get<uint8_t>();
    c.bg_color = a[9].get<uint8_t>();
    c.ascii_fallback = static_cast<char>(a[10].get<int>());
    c.tile_index = a[11].get<uint16_t>();
    c.under_tile_index = a[12].get<uint16_t>();
    c.graf_fg_row = a[13].get<int16_t>();
    c.graf_fg_col = a[14].get<int16_t>();
    c.graf_bg_row = a[15].get<int16_t>();
    c.graf_bg_col = a[16].get<int16_t>();
    return true;
}

/*!
 * @brief `sounds` の配列を読む。**フレーム全体と単体救済の両方から呼ぶ**ので関数にしてある。
 * @param[in,out] out 読み込み先（この関数が丸ごと入れ替える）。
 * @param[out] ok `sub_arr` の作法をそのまま通す（型違いは偽）。
 * @return 読めたか（鍵が無いのは成功・0 件）。
 */
bool dec_sounds(const json &j, std::vector<SoundEvent> &out, std::string &err, bool &ok)
{
    const json *const arr = sub_arr(j, "sounds", err, ok);
    if (arr == nullptr) {
        return ok; // 鍵が無い＝音無しのフレーム。型違いだけが失敗
    }
    out.resize(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
        const json &e = (*arr)[i];
        if (!e.is_object()) {
            err = "sounds[] is not an object";
            return false;
        }
        SoundEvent &s = out[i];
        if (!rd_str(e, "n", s.name, err) || !rd_num(e, "y", s.y, err) || !rd_num(e, "x", s.x, err) ||
            !rd_num(e, "hy", s.heard_y, err) || !rd_num(e, "hx", s.heard_x, err) ||
            !rd_num(e, "p", s.path, err) || !rd_num(e, "g", s.gain, err)) {
            return false;
        }
    }
    return true;
}

} // namespace

/*
 * ============================================================================
 * base64
 * ============================================================================
 */
std::string frame_base64_encode(const std::vector<uint8_t> &bytes)
{
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= bytes.size()) {
        const uint32_t v = (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8) |
                           static_cast<uint32_t>(bytes[i + 2]);
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 6) & 0x3F]);
        out.push_back(kB64Alphabet[v & 0x3F]);
        i += 3;
    }
    const size_t rest = bytes.size() - i;
    if (rest == 1) {
        const uint32_t v = static_cast<uint32_t>(bytes[i]) << 16;
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rest == 2) {
        const uint32_t v = (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

bool frame_base64_decode(const std::string &text, std::vector<uint8_t> &out)
{
    out.clear();
    if ((text.size() % 4) != 0) {
        return false;
    }
    const signed char *rev = b64_reverse_table();
    out.reserve((text.size() / 4) * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int vals[4]{};
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char ch = text[i + static_cast<size_t>(k)];
            if (ch == '=') {
                // 詰め文字は末尾 4 文字の後ろ 2 つにしか来られない。
                if (i + 4 != text.size() || k < 2) {
                    return false;
                }
                ++pad;
                vals[k] = 0;
                continue;
            }
            if (pad > 0) {
                return false; // `=` のあとに実体が来た
            }
            const signed char d = rev[static_cast<unsigned char>(ch)];
            if (d < 0) {
                return false;
            }
            vals[k] = d;
        }
        const uint32_t v = (static_cast<uint32_t>(vals[0]) << 18) | (static_cast<uint32_t>(vals[1]) << 12) |
                           (static_cast<uint32_t>(vals[2]) << 6) | static_cast<uint32_t>(vals[3]);
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        if (pad < 2) {
            out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        }
        if (pad < 1) {
            out.push_back(static_cast<uint8_t>(v & 0xFF));
        }
    }
    return true;
}

/*
 * ============================================================================
 * encode
 * ============================================================================
 */
std::string encode_game_frame(const GameFrame &f)
{
    const GameFrame def{};
    json j = json::object();
    // `t` と `frame_id` は**省略規則の例外**で常に出す（v1 §6.1 正準形の規則）。
    // 会話ログ（§11.2）を frame_id で追えることを、数バイトより優先する裁定。
    j["t"] = "frame";
    j["frame_id"] = f.frame_id;

    put_num(j, "cam_x", f.cam_x, def.cam_x);
    put_num(j, "cam_y", f.cam_y, def.cam_y);
    put_num(j, "view_w", f.view_w, def.view_w);
    put_num(j, "view_h", f.view_h, def.view_h);
    put_num(j, "player_gx", f.player_gx, def.player_gx);
    put_num(j, "player_gy", f.player_gy, def.player_gy);

    // ---- cells（§6.2） ----
    if (!f.cells.empty()) {
        json arr = json::array();
        for (const MapCellView &c : f.cells) {
            arr.push_back(enc_cell(c));
        }
        j["cells"] = std::move(arr);
    }

    // ---- minimap（§6.3） ----
    {
        json o = json::object();
        put_num(o, "width", f.minimap.width, def.minimap.width);
        put_num(o, "height", f.minimap.height, def.minimap.height);
        put_num(o, "player_gx", f.minimap.player_gx, def.minimap.player_gx);
        put_num(o, "player_gy", f.minimap.player_gy, def.minimap.player_gy);
        if (!f.minimap.kinds.empty()) {
            o["kinds_b64"] = frame_base64_encode(f.minimap.kinds);
        }
        put_obj(j, "minimap", std::move(o));
    }

    // ---- hud ----
    {
        json o = json::object();
        put_str(o, "name", f.hud.name, def.hud.name);
        put_num(o, "hp", f.hud.hp, def.hud.hp);
        put_num(o, "hp_max", f.hud.hp_max, def.hud.hp_max);
        put_str(o, "hp_label", f.hud.hp_label, def.hud.hp_label);
        put_num(o, "sp", f.hud.sp, def.hud.sp);
        put_num(o, "sp_max", f.hud.sp_max, def.hud.sp_max);
        //! 空なら書かない（既定と同じ）。**古い画面とも古いコアとも噛み合う**（SH-08）。
        put_str(o, "sp_label", f.hud.sp_label, def.hud.sp_label);
        put_num(o, "gold", f.hud.gold, def.hud.gold);
        put_num(o, "depth", f.hud.depth, def.hud.depth);
        put_num(o, "level", f.hud.level, def.hud.level);
        put_str(o, "status_line", f.hud.status_line, def.hud.status_line);
        if (!f.hud.right_top_lines.empty()) {
            o["right_top_lines"] = f.hud.right_top_lines;
        }
        if (!f.hud.right_bottom_lines.empty()) {
            o["right_bottom_lines"] = f.hud.right_bottom_lines;
        }
        put_obj(j, "hud", std::move(o));
    }

    // ---- messages ----
    if (!f.messages.empty()) {
        const MessageEvent mdef{};
        json arr = json::array();
        for (const MessageEvent &m : f.messages) {
            json o = json::object();
            put_num(o, "seq", m.seq, mdef.seq);
            put_num<uint8_t>(o, "color", m.color, mdef.color);
            put_str(o, "text_utf8", m.text_utf8, mdef.text_utf8);
            arr.push_back(std::move(o));
        }
        j["messages"] = std::move(arr);
    }

    /*
     * ---- map_overlay----
     * **タプルで運ぶ**（`cells` と同じ流儀）。1 件 4 要素 `[gx, gy, 字, 色]`。
     * 出すのは Sil-Q だけで、空なら鍵ごと出ない＝他コアのフレームは 1 バイトも変わらない。
     */
    if (!f.map_overlay.empty()) {
        json arr = json::array();
        for (const MapOverlayCell &c : f.map_overlay) {
            json a = json::array();
            a.push_back(c.gx);
            a.push_back(c.gy);
            a.push_back(static_cast<int>(c.ascii)); // char は int で運ぶ（負値も通る）
            a.push_back(c.color);
            arr.push_back(std::move(a));
        }
        j["map_overlay"] = std::move(arr);
    }

    /*
     * ---- monster_alerts / target----
     * 1 件 3 要素 `[gx, gy, 段]`。照準は既定 -1 なので、出ていなければ鍵も出ない。
     */
    if (!f.monster_alerts.empty()) {
        json arr = json::array();
        for (const MonsterAlertCell &c : f.monster_alerts) {
            json a = json::array();
            a.push_back(c.gx);
            a.push_back(c.gy);
            a.push_back(c.level);
            arr.push_back(std::move(a));
        }
        j["monster_alerts"] = std::move(arr);
    }
    put_num(j, "target_gx", f.target_gx, def.target_gx);
    put_num(j, "target_gy", f.target_gy, def.target_gy);

    // ---- combat_fx----
    if (!f.combat_fx.empty()) {
        const CombatFxEvent cdef{};
        json arr = json::array();
        for (const CombatFxEvent &c : f.combat_fx) {
            json o = json::object();
            put_num<uint8_t>(o, "kind", static_cast<uint8_t>(c.kind), static_cast<uint8_t>(cdef.kind));
            put_num<uint8_t>(o, "elem", static_cast<uint8_t>(c.element), static_cast<uint8_t>(cdef.element));
            put_num(o, "y", c.y, cdef.y);
            put_num(o, "x", c.x, cdef.x);
            put_num(o, "sy", c.src_y, cdef.src_y);
            put_num(o, "sx", c.src_x, cdef.src_x);
            /*
             * 強さは**小数**。`put_num` は整数用（`rd_num` が `is_number_integer()` で
             * 弾く）なので、`teleport_fx.charge` と同じく double で直に置く。
             * ここを取り違えると**フレームまるごと復号に失敗して、演出どころか
             * その 1 フレームが丸ごと捨てられる**（実際にそうなっていた）。
             */
            if (c.intensity != cdef.intensity) {
                o["i"] = static_cast<double>(c.intensity);
            }
            arr.push_back(std::move(o));
        }
        j["combat_fx"] = std::move(arr);
    }


    // ---- sounds----
    if (!f.sounds.empty()) {
        const SoundEvent sdef{};
        json arr = json::array();
        for (const SoundEvent &s : f.sounds) {
            json o = json::object();
            o["n"] = s.name; //!< 名前は**必ず**書く（これが無いと何の音か分からない）
            put_num(o, "y", s.y, sdef.y);
            put_num(o, "x", s.x, sdef.x);
            put_num(o, "hy", s.heard_y, sdef.heard_y);
            put_num(o, "hx", s.heard_x, sdef.heard_x);
            put_num(o, "p", s.path, sdef.path);
            put_num(o, "g", s.gain, sdef.gain);
            arr.push_back(std::move(o));
        }
        j["sounds"] = std::move(arr);
    }

    put_str(j, "controller_hint", f.controller_hint, def.controller_hint);
    put_bool(j, "text_input_active", f.text_input_active, def.text_input_active);
    put_bool(j, "awaiting_command", f.awaiting_command, def.awaiting_command);
    put_bool(j, "camera_detached", f.camera_detached, def.camera_detached);
    put_bool(j, "menu_open", f.menu_open, def.menu_open);
    put_bool(j, "menu_over_map", f.menu_over_map, def.menu_over_map);
    put_bool(j, "pre_game_menu", f.pre_game_menu, def.pre_game_menu);
    put_bool(j, "title_screen", f.title_screen, def.title_screen);

    if (!f.sub2_lines.empty()) {
        j["sub2_lines"] = f.sub2_lines;
    }
    if (!f.sub3_lines.empty()) {
        j["sub3_lines"] = f.sub3_lines;
    }
    if (!f.sub5_lines.empty()) {
        j["sub5_lines"] = f.sub5_lines;
    }

    // ---- sub_panels（`kSubPanelCount` 枚。1 枚でも既定と違えば全部出す） ----
    {
        bool any = false;
        for (int i = 0; i < kSubPanelCount; ++i) {
            const SubPanelContent &p = f.sub_panels[static_cast<size_t>(i)];
            const SubPanelContent &d = def.sub_panels[static_cast<size_t>(i)];
            if (p.kind != d.kind || p.title_utf8 != d.title_utf8 || !p.lines.empty()) {
                any = true;
                break;
            }
        }
        if (any) {
            json arr = json::array();
            for (int i = 0; i < kSubPanelCount; ++i) {
                const SubPanelContent &p = f.sub_panels[static_cast<size_t>(i)];
                const SubPanelContent d{};
                json o = json::object();
                put_num(o, "kind", p.kind, d.kind);
                put_str(o, "title_utf8", p.title_utf8, d.title_utf8);
                if (!p.lines.empty()) {
                    json lines = json::array();
                    for (const SubPanelLine &ln : p.lines) {
                        lines.push_back(enc_sub_panel_line(ln));
                    }
                    o["lines"] = std::move(lines);
                }
                arr.push_back(std::move(o));
            }
            j["sub_panels"] = std::move(arr);
        }
    }

    if (!f.status_col_lines.empty()) {
        json arr = json::array();
        for (const SubPanelLine &ln : f.status_col_lines) {
            arr.push_back(enc_sub_panel_line(ln));
        }
        j["status_col_lines"] = std::move(arr);
    }

    if (!f.bottom_row_runs.empty()) {
        json arr = json::array();
        for (const TermTextRun &r : f.bottom_row_runs) {
            arr.push_back(enc_term_text_run(r));
        }
        j["bottom_row_runs"] = std::move(arr);
    }
    put_num(j, "status_col_cols", f.status_col_cols, def.status_col_cols);
    put_num(j, "status_col_side", f.status_col_side, def.status_col_side);
    put_num(j, "bottom_row_cols", f.bottom_row_cols, def.bottom_row_cols);

    put_obj(j, "depth", enc_sub_panel_line(f.depth));

    /*
     * 色表は**既定（コアの初期値）と違うときだけ**平らな 48 個の数として出す。
     * `&` で色をいじっていない通常のプレイでは 1 バイトも増えない。
     */
    if (f.term_palette != def.term_palette) {
        json arr = json::array();
        for (const auto &entry : f.term_palette.rgb) {
            for (const uint8_t component : entry) {
                arr.push_back(component);
            }
        }
        j["term_palette"] = std::move(arr);
    }

    if (!f.menu_term_lines.empty()) {
        json arr = json::array();
        for (const TermMirrorLine &ln : f.menu_term_lines) {
            arr.push_back(enc_term_mirror_line(ln));
        }
        j["menu_term_lines"] = std::move(arr);
    }
    put_num(j, "menu_term_curs_col", f.menu_term_curs_col, def.menu_term_curs_col);
    put_num(j, "menu_term_curs_row", f.menu_term_curs_row, def.menu_term_curs_row);
    put_num(j, "menu_page_prev_key", f.menu_page_prev_key, def.menu_page_prev_key);
    put_num(j, "menu_page_next_key", f.menu_page_next_key, def.menu_page_next_key);

    if (!f.menu_choices.empty()) {
        const MenuChoice mcdef{};
        json arr = json::array();
        for (const MenuChoice &mc : f.menu_choices) {
            arr.push_back(enc_menu_choice(mc, mcdef));
        }
        j["menu_choices"] = std::move(arr);
    }

    put_obj(j, "menu_core_cursor", enc_menu_choice(f.menu_core_cursor, def.menu_core_cursor));

    if (!f.menu_core_cursors.empty()) {
        const MenuChoice mcdef{};
        json arr = json::array();
        for (const MenuChoice &mc : f.menu_core_cursors) {
            arr.push_back(enc_menu_choice(mc, mcdef));
        }
        j["menu_core_cursors"] = std::move(arr);
    }

    // ---- prompt ----
    {
        json o = json::object();
        put_str(o, "text_utf8", f.prompt.text_utf8, def.prompt.text_utf8);
        if (!f.prompt.choices.empty()) {
            json arr = json::array();
            for (const PromptChoice &pc : f.prompt.choices) {
                arr.push_back(enc_prompt_choice(pc));
            }
            o["choices"] = std::move(arr);
        }
        put_str(o, "footer_utf8", f.prompt.footer_utf8, def.prompt.footer_utf8);
        put_num(o, "line_index", f.prompt.line_index, def.prompt.line_index);
        put_obj(j, "prompt", std::move(o));
    }

    // ---- numeric ----
    {
        json o = json::object();
        put_bool(o, "active", f.numeric.active, def.numeric.active);
        put_str(o, "prompt_utf8", f.numeric.prompt_utf8, def.numeric.prompt_utf8);
        put_num(o, "min", f.numeric.min, def.numeric.min);
        put_num(o, "max", f.numeric.max, def.numeric.max);
        put_num(o, "value", f.numeric.value, def.numeric.value);
        put_num(o, "digits", f.numeric.digits, def.numeric.digits);
        put_num(o, "text_len", f.numeric.text_len, def.numeric.text_len);
        put_num(o, "line_index", f.numeric.line_index, def.numeric.line_index);
        put_num(o, "value_begin", f.numeric.value_begin, def.numeric.value_begin);
        put_num(o, "value_len", f.numeric.value_len, def.numeric.value_len);
        put_obj(j, "numeric", std::move(o));
    }

    // ---- teleport_fx ----
    {
        json o = json::object();
        if (f.teleport_fx.charge != def.teleport_fx.charge) {
            // float → double へ広げて出す。`nlohmann` は「double へ戻して一致する最短表記」を
            // 書くので、double → 文字列 → double が無損失、したがって float も無損失。
            o["charge"] = static_cast<double>(f.teleport_fx.charge);
        }
        put_bool(o, "burst", f.teleport_fx.burst, def.teleport_fx.burst);
        put_obj(j, "teleport_fx", std::move(o));
    }

    // ---- floor（ボクセル HD2D 設計書 §12-1・§12-2。**追加だけ**なので版は上げない） ----
    {
        json o = json::object();
        put_num(o, "dungeon_id", f.floor.dungeon_id, def.floor.dungeon_id);
        put_num(o, "dun_level", f.floor.dun_level, def.floor.dun_level);
        put_num(o, "generated_turn", f.floor.generated_turn, def.floor.generated_turn);
        put_num(o, "kind", f.floor.kind, def.floor.kind);
        put_num(o, "town_id", f.floor.town_id, def.floor.town_id);
        //! カットイン演出（P10。2026-08-11）。**追加だけ**なので版は上げない。
        put_str(o, "place_name", f.floor.place_name_utf8, def.floor.place_name_utf8);
        put_bool(o, "wild_mode", f.floor.wild_mode, def.floor.wild_mode);
        put_obj(j, "floor", std::move(o));
    }

    // ---- lighting（ボクセル HD2D 設計書 §12-4・§12-5。**追加だけ**なので版は上げない） ----
    {
        json o = json::object();
        put_num(o, "day_minute", f.lighting.day_minute, def.lighting.day_minute);
        put_bool(o, "daytime", f.lighting.daytime, def.lighting.daytime);
        put_num(o, "light_radius", f.lighting.light_radius, def.lighting.light_radius);
        put_obj(j, "lighting", std::move(o));
    }

    /*
     * ---- surroundings----
     *
     * **数えていないときは丸ごと出ない**（`radius == 0` が既定なので `put_obj` が畳む）。
     * 受け手はその場合「層を使わない」に落ちる。
     */
    {
        json o = json::object();
        put_num(o, "grass", f.surroundings.grass, def.surroundings.grass);
        put_num(o, "tree", f.surroundings.tree, def.surroundings.tree);
        put_num(o, "dirt", f.surroundings.dirt, def.surroundings.dirt);
        put_num(o, "swamp", f.surroundings.swamp, def.surroundings.swamp);
        put_num(o, "water", f.surroundings.water, def.surroundings.water);
        put_num(o, "deep_water", f.surroundings.deep_water, def.surroundings.deep_water);
        put_num(o, "lava", f.surroundings.lava, def.surroundings.lava);
        put_num(o, "rock", f.surroundings.rock, def.surroundings.rock);
        put_num(o, "glass", f.surroundings.glass, def.surroundings.glass);
        put_num(o, "wall", f.surroundings.wall, def.surroundings.wall);
        put_num(o, "radius", f.surroundings.radius, def.surroundings.radius);
        put_num(o, "counted", f.surroundings.counted, def.surroundings.counted);
        put_obj(j, "surroundings", std::move(o));
    }

    // indent=-1（詰めて出す）。`error_handler_t::replace` は**投げないため**であって
    // 手心ではない。不正 UTF-8 が来たら往復検査が「差がある」と言う（例外で落ちるより良い）。
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

/*
 * ============================================================================
 * decode
 * ============================================================================
 */
bool decode_frame_sounds(const std::string &json_text, std::vector<SoundEvent> &out, std::string &err)
{
    err.clear();
    const json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "invalid JSON";
        return false;
    }
    std::vector<SoundEvent> sounds;
    bool ok = true;
    if (!dec_sounds(j, sounds, err, ok)) {
        return false;
    }
    out.insert(out.end(), std::make_move_iterator(sounds.begin()), std::make_move_iterator(sounds.end()));
    return true;
}

bool decode_game_frame(const std::string &json_text, GameFrame &out, std::string &err)
{
    err.clear();
    const json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) {
        err = "invalid JSON";
        return false;
    }
    if (!j.is_object()) {
        err = "payload is not a JSON object";
        return false;
    }
    const auto t = j.find("t");
    if (t == j.end() || !t->is_string() || t->get<std::string>() != "frame") {
        err = "\"t\" is not \"frame\"";
        return false;
    }

    GameFrame f{}; // 既定値で初期化してから上書き（v1 §6.1）
    bool ok = true;

    if (!rd_num(j, "frame_id", f.frame_id, err) || !rd_num(j, "cam_x", f.cam_x, err) ||
        !rd_num(j, "cam_y", f.cam_y, err) || !rd_num(j, "view_w", f.view_w, err) ||
        !rd_num(j, "view_h", f.view_h, err) || !rd_num(j, "player_gx", f.player_gx, err) ||
        !rd_num(j, "player_gy", f.player_gy, err)) {
        return false;
    }

    // ---- cells ----
    if (const json *arr = sub_arr(j, "cells", err, ok)) {
        f.cells.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            if (!dec_cell((*arr)[i], f.cells[i], i, err)) {
                return false;
            }
        }
    } else if (!ok) {
        return false;
    }

    // ---- minimap ----
    if (const json *o = sub_obj(j, "minimap", err, ok)) {
        if (!rd_num(*o, "width", f.minimap.width, err) || !rd_num(*o, "height", f.minimap.height, err) ||
            !rd_num(*o, "player_gx", f.minimap.player_gx, err) ||
            !rd_num(*o, "player_gy", f.minimap.player_gy, err)) {
            return false;
        }
        std::string b64;
        if (!rd_str(*o, "kinds_b64", b64, err)) {
            return false;
        }
        if (!b64.empty() && !frame_base64_decode(b64, f.minimap.kinds)) {
            err = "minimap.kinds_b64 is not valid base64";
            return false;
        }
    } else if (!ok) {
        return false;
    }

    // ---- hud ----
    if (const json *o = sub_obj(j, "hud", err, ok)) {
        if (!rd_str(*o, "name", f.hud.name, err) || !rd_num(*o, "hp", f.hud.hp, err) ||
            !rd_num(*o, "hp_max", f.hud.hp_max, err) || !rd_num(*o, "sp", f.hud.sp, err) ||
            !rd_num(*o, "sp_max", f.hud.sp_max, err) || !rd_num(*o, "gold", f.hud.gold, err) ||
            !rd_num(*o, "depth", f.hud.depth, err) || !rd_num(*o, "level", f.hud.level, err) ||
            !rd_str(*o, "status_line", f.hud.status_line, err)
            || !rd_str(*o, "hp_label", f.hud.hp_label, err)
            || !rd_str(*o, "sp_label", f.hud.sp_label, err)) {
            return false;
        }
        for (const char *key : { "right_top_lines", "right_bottom_lines" }) {
            const json *lines = sub_arr(*o, key, err, ok);
            if (!ok) {
                return false;
            }
            if (lines == nullptr) {
                continue;
            }
            std::vector<std::string> dst;
            dst.reserve(lines->size());
            for (const json &e : *lines) {
                if (!e.is_string()) {
                    err = sfmt("hud.%s[] is not a string", key);
                    return false;
                }
                dst.push_back(e.get<std::string>());
            }
            if (std::strcmp(key, "right_top_lines") == 0) {
                f.hud.right_top_lines = std::move(dst);
            } else {
                f.hud.right_bottom_lines = std::move(dst);
            }
        }
    } else if (!ok) {
        return false;
    }

    // ---- messages ----
    if (const json *arr = sub_arr(j, "messages", err, ok)) {
        f.messages.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            const json &e = (*arr)[i];
            if (!e.is_object()) {
                err = "messages[] is not an object";
                return false;
            }
            MessageEvent &m = f.messages[i];
            if (!rd_num(e, "seq", m.seq, err) || !rd_num(e, "color", m.color, err) ||
                !rd_str(e, "text_utf8", m.text_utf8, err)) {
                return false;
            }
        }
    } else if (!ok) {
        return false;
    }

    // ---- map_overlay（SQ-2）----
    if (const json *arr = sub_arr(j, "map_overlay", err, ok)) {
        f.map_overlay.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            const json &a = (*arr)[i];
            if (!a.is_array() || (a.size() != 4)) {
                err = sfmt("map_overlay[%u] is not a 4-element tuple", static_cast<unsigned>(i));
                return false;
            }
            for (size_t k = 0; k < 4; ++k) {
                if (!a[k].is_number_integer()) {
                    err = sfmt("map_overlay[%u][%u] is not an integer", static_cast<unsigned>(i),
                        static_cast<unsigned>(k));
                    return false;
                }
            }
            MapOverlayCell &c = f.map_overlay[i];
            c.gx = a[0].get<int16_t>();
            c.gy = a[1].get<int16_t>();
            c.ascii = static_cast<char>(a[2].get<int>());
            c.color = a[3].get<uint8_t>();
        }
    } else if (!ok) {
        return false;
    }

    // ---- monster_alerts / target（SQ-1）----
    if (const json *arr = sub_arr(j, "monster_alerts", err, ok)) {
        f.monster_alerts.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            const json &a = (*arr)[i];
            if (!a.is_array() || (a.size() != 3)) {
                err = sfmt("monster_alerts[%u] is not a 3-element tuple", static_cast<unsigned>(i));
                return false;
            }
            for (size_t k = 0; k < 3; ++k) {
                if (!a[k].is_number_integer()) {
                    err = sfmt("monster_alerts[%u][%u] is not an integer", static_cast<unsigned>(i),
                        static_cast<unsigned>(k));
                    return false;
                }
            }
            MonsterAlertCell &c = f.monster_alerts[i];
            c.gx = a[0].get<int16_t>();
            c.gy = a[1].get<int16_t>();
            c.level = a[2].get<uint8_t>();
        }
    } else if (!ok) {
        return false;
    }
    if (!rd_num(j, "target_gx", f.target_gx, err) || !rd_num(j, "target_gy", f.target_gy, err)) {
        return false;
    }

    // ---- combat_fx ----
    if (const json *arr = sub_arr(j, "combat_fx", err, ok)) {
        f.combat_fx.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            const json &e = (*arr)[i];
            if (!e.is_object()) {
                err = "combat_fx[] is not an object";
                return false;
            }
            CombatFxEvent &c = f.combat_fx[i];
            uint8_t kind = 0;
            uint8_t elem = 0;
            if (!rd_num(e, "kind", kind, err) || !rd_num(e, "elem", elem, err) ||
                !rd_num(e, "y", c.y, err) || !rd_num(e, "x", c.x, err) ||
                !rd_num(e, "sy", c.src_y, err) || !rd_num(e, "sx", c.src_x, err) ||
                !rd_float(e, "i", c.intensity, err)) {
                return false;
            }
            c.kind = static_cast<CombatFxKind>(kind);
            c.element = static_cast<CombatFxElement>(elem);
        }
    } else if (!ok) {
        return false;
    }

    // ---- sounds ----
    if (!dec_sounds(j, f.sounds, err, ok)) {
        return false;
    }

    if (!rd_str(j, "controller_hint", f.controller_hint, err) ||
        !rd_bool(j, "text_input_active", f.text_input_active, err) ||
        !rd_bool(j, "awaiting_command", f.awaiting_command, err) ||
        !rd_bool(j, "camera_detached", f.camera_detached, err) ||
        !rd_bool(j, "menu_open", f.menu_open, err) || !rd_bool(j, "menu_over_map", f.menu_over_map, err) ||
        !rd_bool(j, "pre_game_menu", f.pre_game_menu, err) ||
        !rd_bool(j, "title_screen", f.title_screen, err)) {
        return false;
    }

    {
        std::vector<std::string> *dsts[3] = { &f.sub2_lines, &f.sub3_lines, &f.sub5_lines };
        const char *keys[3] = { "sub2_lines", "sub3_lines", "sub5_lines" };
        for (int i = 0; i < 3; ++i) {
            const json *arr = sub_arr(j, keys[i], err, ok);
            if (!ok) {
                return false;
            }
            if (arr == nullptr) {
                continue;
            }
            dsts[i]->reserve(arr->size());
            for (const json &e : *arr) {
                if (!e.is_string()) {
                    err = sfmt("%s[] is not a string", keys[i]);
                    return false;
                }
                dsts[i]->push_back(e.get<std::string>());
            }
        }
    }

    // ---- sub_panels ----
    if (const json *arr = sub_arr(j, "sub_panels", err, ok)) {
        const size_t n = (arr->size() < static_cast<size_t>(kSubPanelCount)) ? arr->size()
                                                                            : static_cast<size_t>(kSubPanelCount);
        for (size_t i = 0; i < n; ++i) {
            const json &e = (*arr)[i];
            if (!e.is_object()) {
                err = "sub_panels[] is not an object";
                return false;
            }
            SubPanelContent &p = f.sub_panels[i];
            if (!rd_num(e, "kind", p.kind, err) || !rd_str(e, "title_utf8", p.title_utf8, err)) {
                return false;
            }
            const json *lines = sub_arr(e, "lines", err, ok);
            if (!ok) {
                return false;
            }
            if (lines == nullptr) {
                continue;
            }
            p.lines.resize(lines->size());
            for (size_t k = 0; k < lines->size(); ++k) {
                if (!(*lines)[k].is_object() || !dec_sub_panel_line((*lines)[k], p.lines[k], err)) {
                    if (err.empty()) {
                        err = "sub_panels[].lines[] is not an object";
                    }
                    return false;
                }
            }
        }
    } else if (!ok) {
        return false;
    }

    if (const json *arr = sub_arr(j, "status_col_lines", err, ok)) {
        f.status_col_lines.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            if (!(*arr)[i].is_object() || !dec_sub_panel_line((*arr)[i], f.status_col_lines[i], err)) {
                if (err.empty()) {
                    err = "status_col_lines[] is not an object";
                }
                return false;
            }
        }
    } else if (!ok) {
        return false;
    }

    if (const json *arr = sub_arr(j, "bottom_row_runs", err, ok)) {
        f.bottom_row_runs.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            if (!(*arr)[i].is_object() || !dec_term_text_run((*arr)[i], f.bottom_row_runs[i], err)) {
                if (err.empty()) {
                    err = "bottom_row_runs[] is not an object";
                }
                return false;
            }
        }
    } else if (!ok) {
        return false;
    }
    if (!rd_num(j, "status_col_cols", f.status_col_cols, err)) {
        return false;
    }
    if (!rd_num(j, "status_col_side", f.status_col_side, err)) {
        return false;
    }
    if (!rd_num(j, "bottom_row_cols", f.bottom_row_cols, err)) {
        return false;
    }

    if (const json *o = sub_obj(j, "depth", err, ok)) {
        if (!dec_sub_panel_line(*o, f.depth, err)) {
            return false;
        }
    } else if (!ok) {
        return false;
    }

    if (const json *arr = sub_arr(j, "term_palette", err, ok)) {
        constexpr size_t kComponents = 16 * 3;
        if (arr->size() != kComponents) {
            err = sfmt("term_palette has %u entries (want %u)",
                static_cast<unsigned>(arr->size()), static_cast<unsigned>(kComponents));
            return false;
        }
        for (size_t i = 0; i < kComponents; ++i) {
            const json &component = (*arr)[i];
            if (!component.is_number_unsigned() || component.get<uint64_t>() > 0xFF) {
                err = sfmt("term_palette[%u] is not a byte", static_cast<unsigned>(i));
                return false;
            }
            f.term_palette.rgb[i / 3][i % 3] = component.get<uint8_t>();
        }
    } else if (!ok) {
        return false;
    }

    if (const json *arr = sub_arr(j, "menu_term_lines", err, ok)) {
        f.menu_term_lines.resize(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) {
            if (!(*arr)[i].is_object() || !dec_term_mirror_line((*arr)[i], f.menu_term_lines[i], err)) {
                if (err.empty()) {
                    err = "menu_term_lines[] is not an object";
                }
                return false;
            }
        }
    } else if (!ok) {
        return false;
    }
    if (!rd_num(j, "menu_term_curs_col", f.menu_term_curs_col, err) ||
        !rd_num(j, "menu_term_curs_row", f.menu_term_curs_row, err) ||
        !rd_num(j, "menu_page_prev_key", f.menu_page_prev_key, err) ||
        !rd_num(j, "menu_page_next_key", f.menu_page_next_key, err)) {
        return false;
    }

    {
        std::vector<MenuChoice> *dsts[2] = { &f.menu_choices, &f.menu_core_cursors };
        const char *keys[2] = { "menu_choices", "menu_core_cursors" };
        for (int i = 0; i < 2; ++i) {
            const json *arr = sub_arr(j, keys[i], err, ok);
            if (!ok) {
                return false;
            }
            if (arr == nullptr) {
                continue;
            }
            dsts[i]->resize(arr->size());
            for (size_t k = 0; k < arr->size(); ++k) {
                if (!(*arr)[k].is_object() || !dec_menu_choice((*arr)[k], (*dsts[i])[k], err)) {
                    if (err.empty()) {
                        err = sfmt("%s[] is not an object", keys[i]);
                    }
                    return false;
                }
            }
        }
    }

    if (const json *o = sub_obj(j, "menu_core_cursor", err, ok)) {
        if (!dec_menu_choice(*o, f.menu_core_cursor, err)) {
            return false;
        }
    } else if (!ok) {
        return false;
    }

    // ---- prompt ----
    if (const json *o = sub_obj(j, "prompt", err, ok)) {
        if (!rd_str(*o, "text_utf8", f.prompt.text_utf8, err) ||
            !rd_str(*o, "footer_utf8", f.prompt.footer_utf8, err) ||
            !rd_num(*o, "line_index", f.prompt.line_index, err)) {
            return false;
        }
        const json *arr = sub_arr(*o, "choices", err, ok);
        if (!ok) {
            return false;
        }
        if (arr != nullptr) {
            f.prompt.choices.resize(arr->size());
            for (size_t i = 0; i < arr->size(); ++i) {
                if (!(*arr)[i].is_object() || !dec_prompt_choice((*arr)[i], f.prompt.choices[i], err)) {
                    if (err.empty()) {
                        err = "prompt.choices[] is not an object";
                    }
                    return false;
                }
            }
        }
    } else if (!ok) {
        return false;
    }

    // ---- numeric ----
    if (const json *o = sub_obj(j, "numeric", err, ok)) {
        if (!rd_bool(*o, "active", f.numeric.active, err) ||
            !rd_str(*o, "prompt_utf8", f.numeric.prompt_utf8, err) || !rd_num(*o, "min", f.numeric.min, err) ||
            !rd_num(*o, "max", f.numeric.max, err) || !rd_num(*o, "value", f.numeric.value, err) ||
            !rd_num(*o, "digits", f.numeric.digits, err) || !rd_num(*o, "text_len", f.numeric.text_len, err) ||
            !rd_num(*o, "line_index", f.numeric.line_index, err) ||
            !rd_num(*o, "value_begin", f.numeric.value_begin, err) ||
            !rd_num(*o, "value_len", f.numeric.value_len, err)) {
            return false;
        }
    } else if (!ok) {
        return false;
    }

    // ---- floor ----
    if (const json *o = sub_obj(j, "floor", err, ok)) {
        if (!rd_num(*o, "dungeon_id", f.floor.dungeon_id, err) ||
            !rd_num(*o, "dun_level", f.floor.dun_level, err) ||
            !rd_num(*o, "generated_turn", f.floor.generated_turn, err) ||
            !rd_num(*o, "kind", f.floor.kind, err) ||
            !rd_num(*o, "town_id", f.floor.town_id, err) ||
            !rd_str(*o, "place_name", f.floor.place_name_utf8, err) ||
            !rd_bool(*o, "wild_mode", f.floor.wild_mode, err)) {
            return false;
        }
    }
    if (!ok) {
        return false;
    }

    // ---- lighting ----
    if (const json *o = sub_obj(j, "lighting", err, ok)) {
        if (!rd_num(*o, "day_minute", f.lighting.day_minute, err) ||
            !rd_bool(*o, "daytime", f.lighting.daytime, err) ||
            !rd_num(*o, "light_radius", f.lighting.light_radius, err)) {
            return false;
        }
    }

    // ---- surroundings ----
    if (const json *o = sub_obj(j, "surroundings", err, ok)) {
        if (!rd_num(*o, "grass", f.surroundings.grass, err) ||
            !rd_num(*o, "tree", f.surroundings.tree, err) ||
            !rd_num(*o, "dirt", f.surroundings.dirt, err) ||
            !rd_num(*o, "swamp", f.surroundings.swamp, err) ||
            !rd_num(*o, "water", f.surroundings.water, err) ||
            !rd_num(*o, "deep_water", f.surroundings.deep_water, err) ||
            !rd_num(*o, "lava", f.surroundings.lava, err) ||
            !rd_num(*o, "rock", f.surroundings.rock, err) ||
            !rd_num(*o, "glass", f.surroundings.glass, err) ||
            !rd_num(*o, "wall", f.surroundings.wall, err) ||
            !rd_num(*o, "radius", f.surroundings.radius, err) ||
            !rd_num(*o, "counted", f.surroundings.counted, err)) {
            return false;
        }
    }
    if (!ok) {
        return false;
    }

    // ---- teleport_fx ----
    if (const json *o = sub_obj(j, "teleport_fx", err, ok)) {
        if (!rd_float(*o, "charge", f.teleport_fx.charge, err) ||
            !rd_bool(*o, "burst", f.teleport_fx.burst, err)) {
            return false;
        }
    } else if (!ok) {
        return false;
    }

    out = std::move(f);
    return true;
}

/*
 * ============================================================================
 * フィールド単位の等価比較（二重簿記の片翼）
 * ============================================================================
 * `game_frame.h` のメンバを**上から順に全部**並べてある。encode/decode に列挙漏れが
 * あると、ここが「値が違う」と言う（byte 比較では両側同じに落ちるので出ない）。
 */
namespace {

bool eq_sub_panel_line(const SubPanelLine &a, const SubPanelLine &b)
{
    return a.text_utf8 == b.text_utf8 && a.color == b.color;
}

bool eq_menu_choice(const MenuChoice &a, const MenuChoice &b)
{
    return a.line_index == b.line_index && a.span_begin == b.span_begin && a.span_len == b.span_len &&
           a.key_begin == b.key_begin && a.key_len == b.key_len && a.key == b.key;
}

//! float は**ビット単位**で見る（`==` だと -0.0 と +0.0 が同一視されて落ちが見えない）。
bool eq_float_bits(float a, float b)
{
    uint32_t ba = 0;
    uint32_t bb = 0;
    std::memcpy(&ba, &a, sizeof(ba));
    std::memcpy(&bb, &b, sizeof(bb));
    return ba == bb;
}

} // namespace

bool game_frames_equal(const GameFrame &a, const GameFrame &b, std::string &first_diff)
{
    first_diff.clear();
    auto fail = [&first_diff](const std::string &s) {
        first_diff = s;
        return false;
    };

    // -- スカラ（frame_id 〜 player_gy）
    if (a.frame_id != b.frame_id) {
        return fail(sfmt("frame_id %llu != %llu", static_cast<unsigned long long>(a.frame_id),
            static_cast<unsigned long long>(b.frame_id)));
    }
    if (a.cam_x != b.cam_x) {
        return fail(sfmt("cam_x %d != %d", a.cam_x, b.cam_x));
    }
    if (a.cam_y != b.cam_y) {
        return fail(sfmt("cam_y %d != %d", a.cam_y, b.cam_y));
    }
    if (a.view_w != b.view_w) {
        return fail(sfmt("view_w %d != %d", a.view_w, b.view_w));
    }
    if (a.view_h != b.view_h) {
        return fail(sfmt("view_h %d != %d", a.view_h, b.view_h));
    }
    if (a.player_gx != b.player_gx) {
        return fail(sfmt("player_gx %d != %d", a.player_gx, b.player_gx));
    }
    if (a.player_gy != b.player_gy) {
        return fail(sfmt("player_gy %d != %d", a.player_gy, b.player_gy));
    }

    // -- cells（17 フィールド全部）
    if (a.cells.size() != b.cells.size()) {
        return fail(sfmt("cells.size %u != %u", static_cast<unsigned>(a.cells.size()),
            static_cast<unsigned>(b.cells.size())));
    }
    for (size_t i = 0; i < a.cells.size(); ++i) {
        const MapCellView &x = a.cells[i];
        const MapCellView &y = b.cells[i];
        const char *which = nullptr;
        if (x.gx != y.gx) {
            which = "gx";
        } else if (x.gy != y.gy) {
            which = "gy";
        } else if (x.terrain_id != y.terrain_id) {
            which = "terrain_id";
        } else if (x.feature_flags != y.feature_flags) {
            which = "feature_flags";
        } else if (x.monster_id != y.monster_id) {
            which = "monster_id";
        } else if (x.monster_slot != y.monster_slot) {
            which = "monster_slot";
        } else if (x.object_id != y.object_id) {
            which = "object_id";
        } else if (x.light_level != y.light_level) {
            which = "light_level";
        } else if (x.fg_color != y.fg_color) {
            which = "fg_color";
        } else if (x.bg_color != y.bg_color) {
            which = "bg_color";
        } else if (x.ascii_fallback != y.ascii_fallback) {
            which = "ascii_fallback";
        } else if (x.tile_index != y.tile_index) {
            which = "tile_index";
        } else if (x.under_tile_index != y.under_tile_index) {
            which = "under_tile_index";
        } else if (x.graf_fg_row != y.graf_fg_row) {
            which = "graf_fg_row";
        } else if (x.graf_fg_col != y.graf_fg_col) {
            which = "graf_fg_col";
        } else if (x.graf_bg_row != y.graf_bg_row) {
            which = "graf_bg_row";
        } else if (x.graf_bg_col != y.graf_bg_col) {
            which = "graf_bg_col";
        }
        if (which != nullptr) {
            return fail(sfmt("cells[%u].%s differs", static_cast<unsigned>(i), which));
        }
    }

    // -- map_overlay（SQ-2）
    if (a.map_overlay.size() != b.map_overlay.size()) {
        return fail(sfmt("map_overlay.size %u != %u", static_cast<unsigned>(a.map_overlay.size()),
            static_cast<unsigned>(b.map_overlay.size())));
    }
    for (size_t i = 0; i < a.map_overlay.size(); ++i) {
        const MapOverlayCell &x = a.map_overlay[i];
        const MapOverlayCell &y = b.map_overlay[i];
        if ((x.gx != y.gx) || (x.gy != y.gy) || (x.ascii != y.ascii) || (x.color != y.color)) {
            return fail(sfmt("map_overlay[%u] differs", static_cast<unsigned>(i)));
        }
    }

    // -- monster_alerts / target（SQ-1）
    if (a.monster_alerts.size() != b.monster_alerts.size()) {
        return fail(sfmt("monster_alerts.size %u != %u", static_cast<unsigned>(a.monster_alerts.size()),
            static_cast<unsigned>(b.monster_alerts.size())));
    }
    for (size_t i = 0; i < a.monster_alerts.size(); ++i) {
        const MonsterAlertCell &x = a.monster_alerts[i];
        const MonsterAlertCell &y = b.monster_alerts[i];
        if ((x.gx != y.gx) || (x.gy != y.gy) || (x.level != y.level)) {
            return fail(sfmt("monster_alerts[%u] differs", static_cast<unsigned>(i)));
        }
    }
    if (a.target_gx != b.target_gx) {
        return fail(sfmt("target_gx %d != %d", a.target_gx, b.target_gx));
    }
    if (a.target_gy != b.target_gy) {
        return fail(sfmt("target_gy %d != %d", a.target_gy, b.target_gy));
    }

    // -- minimap
    if (a.minimap.width != b.minimap.width) {
        return fail(sfmt("minimap.width %d != %d", a.minimap.width, b.minimap.width));
    }
    if (a.minimap.height != b.minimap.height) {
        return fail(sfmt("minimap.height %d != %d", a.minimap.height, b.minimap.height));
    }
    if (a.minimap.player_gx != b.minimap.player_gx) {
        return fail(sfmt("minimap.player_gx %d != %d", a.minimap.player_gx, b.minimap.player_gx));
    }
    if (a.minimap.player_gy != b.minimap.player_gy) {
        return fail(sfmt("minimap.player_gy %d != %d", a.minimap.player_gy, b.minimap.player_gy));
    }
    if (a.minimap.kinds.size() != b.minimap.kinds.size()) {
        return fail(sfmt("minimap.kinds.size %u != %u", static_cast<unsigned>(a.minimap.kinds.size()),
            static_cast<unsigned>(b.minimap.kinds.size())));
    }
    for (size_t i = 0; i < a.minimap.kinds.size(); ++i) {
        if (a.minimap.kinds[i] != b.minimap.kinds[i]) {
            return fail(sfmt("minimap.kinds[%u] %u != %u", static_cast<unsigned>(i),
                static_cast<unsigned>(a.minimap.kinds[i]), static_cast<unsigned>(b.minimap.kinds[i])));
        }
    }

    // -- hud
    if (a.hud.name != b.hud.name) {
        return fail("hud.name differs");
    }
    if (a.hud.hp != b.hud.hp) {
        return fail(sfmt("hud.hp %d != %d", a.hud.hp, b.hud.hp));
    }
    if (a.hud.hp_max != b.hud.hp_max) {
        return fail(sfmt("hud.hp_max %d != %d", a.hud.hp_max, b.hud.hp_max));
    }
    if (a.hud.sp != b.hud.sp) {
        return fail(sfmt("hud.sp %d != %d", a.hud.sp, b.hud.sp));
    }
    if (a.hud.sp_max != b.hud.sp_max) {
        return fail(sfmt("hud.sp_max %d != %d", a.hud.sp_max, b.hud.sp_max));
    }
    if (a.hud.gold != b.hud.gold) {
        return fail(sfmt("hud.gold %d != %d", a.hud.gold, b.hud.gold));
    }
    if (a.hud.depth != b.hud.depth) {
        return fail(sfmt("hud.depth %d != %d", a.hud.depth, b.hud.depth));
    }
    if (a.hud.level != b.hud.level) {
        return fail(sfmt("hud.level %d != %d", a.hud.level, b.hud.level));
    }
    if (a.hud.hp_label != b.hud.hp_label) {
        return fail("hud.hp_label differs");
    }
    if (a.hud.sp_label != b.hud.sp_label) {
        return fail("hud.sp_label differs");
    }
    if (a.hud.status_line != b.hud.status_line) {
        return fail("hud.status_line differs");
    }
    if (a.hud.right_top_lines != b.hud.right_top_lines) {
        return fail("hud.right_top_lines differs");
    }
    if (a.hud.right_bottom_lines != b.hud.right_bottom_lines) {
        return fail("hud.right_bottom_lines differs");
    }

    // -- messages
    if (a.messages.size() != b.messages.size()) {
        return fail(sfmt("messages.size %u != %u", static_cast<unsigned>(a.messages.size()),
            static_cast<unsigned>(b.messages.size())));
    }
    for (size_t i = 0; i < a.messages.size(); ++i) {
        if (a.messages[i].seq != b.messages[i].seq || a.messages[i].color != b.messages[i].color ||
            a.messages[i].text_utf8 != b.messages[i].text_utf8) {
            return fail(sfmt("messages[%u] differs", static_cast<unsigned>(i)));
        }
    }

    // -- 単純メンバ
    if (a.controller_hint != b.controller_hint) {
        return fail("controller_hint differs");
    }
    if (a.text_input_active != b.text_input_active) {
        return fail("text_input_active differs");
    }
    if (a.camera_detached != b.camera_detached) {
        return fail("camera_detached differs");
    }
    if (a.awaiting_command != b.awaiting_command) {
        return fail("awaiting_command differs");
    }
    if (a.menu_over_map != b.menu_over_map) {
        return fail("menu_over_map differs");
    }
    if (a.menu_open != b.menu_open) {
        return fail("menu_open differs");
    }
    if (a.pre_game_menu != b.pre_game_menu) {
        return fail("pre_game_menu differs");
    }
    if (a.title_screen != b.title_screen) {
        return fail("title_screen differs");
    }
    if (a.sub2_lines != b.sub2_lines) {
        return fail("sub2_lines differs");
    }
    if (a.sub3_lines != b.sub3_lines) {
        return fail("sub3_lines differs");
    }
    if (a.sub5_lines != b.sub5_lines) {
        return fail("sub5_lines differs");
    }

    // -- sub_panels（`kSubPanelCount` 枚）
    for (int i = 0; i < kSubPanelCount; ++i) {
        const SubPanelContent &x = a.sub_panels[static_cast<size_t>(i)];
        const SubPanelContent &y = b.sub_panels[static_cast<size_t>(i)];
        if (x.kind != y.kind) {
            return fail(sfmt("sub_panels[%d].kind %d != %d", i, x.kind, y.kind));
        }
        if (x.title_utf8 != y.title_utf8) {
            return fail(sfmt("sub_panels[%d].title_utf8 differs", i));
        }
        if (x.lines.size() != y.lines.size()) {
            return fail(sfmt("sub_panels[%d].lines.size %u != %u", i, static_cast<unsigned>(x.lines.size()),
                static_cast<unsigned>(y.lines.size())));
        }
        for (size_t k = 0; k < x.lines.size(); ++k) {
            if (!eq_sub_panel_line(x.lines[k], y.lines[k])) {
                return fail(sfmt("sub_panels[%d].lines[%u] differs", i, static_cast<unsigned>(k)));
            }
        }
    }

    // -- status_col_lines
    if (a.status_col_lines.size() != b.status_col_lines.size()) {
        return fail(sfmt("status_col_lines.size %u != %u", static_cast<unsigned>(a.status_col_lines.size()),
            static_cast<unsigned>(b.status_col_lines.size())));
    }
    for (size_t i = 0; i < a.status_col_lines.size(); ++i) {
        if (!eq_sub_panel_line(a.status_col_lines[i], b.status_col_lines[i])) {
            return fail(sfmt("status_col_lines[%u] differs", static_cast<unsigned>(i)));
        }
    }

    // -- bottom_row_runs / bottom_row_cols
    if (a.bottom_row_runs.size() != b.bottom_row_runs.size()) {
        return fail(sfmt("bottom_row_runs.size %u != %u", static_cast<unsigned>(a.bottom_row_runs.size()),
            static_cast<unsigned>(b.bottom_row_runs.size())));
    }
    for (size_t i = 0; i < a.bottom_row_runs.size(); ++i) {
        const TermTextRun &x = a.bottom_row_runs[i];
        const TermTextRun &y = b.bottom_row_runs[i];
        if (x.col != y.col || x.text_utf8 != y.text_utf8 || x.color != y.color) {
            return fail(sfmt("bottom_row_runs[%u] differs", static_cast<unsigned>(i)));
        }
    }
    if (a.status_col_cols != b.status_col_cols) {
        return fail(sfmt("status_col_cols %d != %d", a.status_col_cols, b.status_col_cols));
    }
    if (a.status_col_side != b.status_col_side) {
        return fail(sfmt("status_col_side %d != %d", a.status_col_side, b.status_col_side));
    }
    if (a.bottom_row_cols != b.bottom_row_cols) {
        return fail(sfmt("bottom_row_cols %d != %d", a.bottom_row_cols, b.bottom_row_cols));
    }

    // -- depth
    if (!eq_sub_panel_line(a.depth, b.depth)) {
        return fail("depth differs");
    }

    // -- term_palette
    if (a.term_palette != b.term_palette) {
        return fail("term_palette differs");
    }

    // -- menu_term_lines / カーソル
    if (a.menu_term_lines.size() != b.menu_term_lines.size()) {
        return fail(sfmt("menu_term_lines.size %u != %u", static_cast<unsigned>(a.menu_term_lines.size()),
            static_cast<unsigned>(b.menu_term_lines.size())));
    }
    for (size_t i = 0; i < a.menu_term_lines.size(); ++i) {
        const TermMirrorLine &x = a.menu_term_lines[i];
        const TermMirrorLine &y = b.menu_term_lines[i];
        if (x.text_utf8 != y.text_utf8 || x.color != y.color || x.source_row != y.source_row ||
            x.highlight_begin != y.highlight_begin || x.highlight_len != y.highlight_len) {
            return fail(sfmt("menu_term_lines[%u] differs", static_cast<unsigned>(i)));
        }
        if (x.color_spans.size() != y.color_spans.size()) {
            return fail(sfmt("menu_term_lines[%u].color_spans.size %u != %u", static_cast<unsigned>(i),
                static_cast<unsigned>(x.color_spans.size()), static_cast<unsigned>(y.color_spans.size())));
        }
        for (size_t k = 0; k < x.color_spans.size(); ++k) {
            const TermColorSpan &p = x.color_spans[k];
            const TermColorSpan &q = y.color_spans[k];
            if (p.begin != q.begin || p.len != q.len || p.color != q.color) {
                return fail(sfmt("menu_term_lines[%u].color_spans[%u] differs",
                    static_cast<unsigned>(i), static_cast<unsigned>(k)));
            }
        }
    }
    if (a.menu_term_curs_col != b.menu_term_curs_col) {
        return fail(sfmt("menu_term_curs_col %d != %d", a.menu_term_curs_col, b.menu_term_curs_col));
    }
    if (a.menu_term_curs_row != b.menu_term_curs_row) {
        return fail(sfmt("menu_term_curs_row %d != %d", a.menu_term_curs_row, b.menu_term_curs_row));
    }
    if (a.menu_page_prev_key != b.menu_page_prev_key) {
        return fail(sfmt("menu_page_prev_key %d != %d", a.menu_page_prev_key, b.menu_page_prev_key));
    }
    if (a.menu_page_next_key != b.menu_page_next_key) {
        return fail(sfmt("menu_page_next_key %d != %d", a.menu_page_next_key, b.menu_page_next_key));
    }

    // -- menu_choices / menu_core_cursor / menu_core_cursors
    if (a.menu_choices.size() != b.menu_choices.size()) {
        return fail(sfmt("menu_choices.size %u != %u", static_cast<unsigned>(a.menu_choices.size()),
            static_cast<unsigned>(b.menu_choices.size())));
    }
    for (size_t i = 0; i < a.menu_choices.size(); ++i) {
        if (!eq_menu_choice(a.menu_choices[i], b.menu_choices[i])) {
            return fail(sfmt("menu_choices[%u] differs", static_cast<unsigned>(i)));
        }
    }
    if (!eq_menu_choice(a.menu_core_cursor, b.menu_core_cursor)) {
        return fail("menu_core_cursor differs");
    }
    if (a.menu_core_cursors.size() != b.menu_core_cursors.size()) {
        return fail(sfmt("menu_core_cursors.size %u != %u", static_cast<unsigned>(a.menu_core_cursors.size()),
            static_cast<unsigned>(b.menu_core_cursors.size())));
    }
    for (size_t i = 0; i < a.menu_core_cursors.size(); ++i) {
        if (!eq_menu_choice(a.menu_core_cursors[i], b.menu_core_cursors[i])) {
            return fail(sfmt("menu_core_cursors[%u] differs", static_cast<unsigned>(i)));
        }
    }

    // -- prompt
    if (a.prompt.text_utf8 != b.prompt.text_utf8) {
        return fail("prompt.text_utf8 differs");
    }
    if (a.prompt.choices.size() != b.prompt.choices.size()) {
        return fail(sfmt("prompt.choices.size %u != %u", static_cast<unsigned>(a.prompt.choices.size()),
            static_cast<unsigned>(b.prompt.choices.size())));
    }
    for (size_t i = 0; i < a.prompt.choices.size(); ++i) {
        const PromptChoice &x = a.prompt.choices[i];
        const PromptChoice &y = b.prompt.choices[i];
        if (x.label_utf8 != y.label_utf8 || x.key != y.key || x.begin != y.begin || x.len != y.len) {
            return fail(sfmt("prompt.choices[%u] differs", static_cast<unsigned>(i)));
        }
    }
    if (a.prompt.footer_utf8 != b.prompt.footer_utf8) {
        return fail("prompt.footer_utf8 differs");
    }
    if (a.prompt.line_index != b.prompt.line_index) {
        return fail(sfmt("prompt.line_index %d != %d", a.prompt.line_index, b.prompt.line_index));
    }

    // -- numeric
    if (a.numeric.active != b.numeric.active) {
        return fail("numeric.active differs");
    }
    if (a.numeric.prompt_utf8 != b.numeric.prompt_utf8) {
        return fail("numeric.prompt_utf8 differs");
    }
    if (a.numeric.min != b.numeric.min) {
        return fail(sfmt("numeric.min %d != %d", a.numeric.min, b.numeric.min));
    }
    if (a.numeric.max != b.numeric.max) {
        return fail(sfmt("numeric.max %d != %d", a.numeric.max, b.numeric.max));
    }
    if (a.numeric.value != b.numeric.value) {
        return fail(sfmt("numeric.value %d != %d", a.numeric.value, b.numeric.value));
    }
    if (a.numeric.digits != b.numeric.digits) {
        return fail(sfmt("numeric.digits %d != %d", a.numeric.digits, b.numeric.digits));
    }
    if (a.numeric.text_len != b.numeric.text_len) {
        return fail(sfmt("numeric.text_len %d != %d", a.numeric.text_len, b.numeric.text_len));
    }
    if (a.numeric.line_index != b.numeric.line_index) {
        return fail(sfmt("numeric.line_index %d != %d", a.numeric.line_index, b.numeric.line_index));
    }
    if (a.numeric.value_begin != b.numeric.value_begin) {
        return fail(sfmt("numeric.value_begin %d != %d", a.numeric.value_begin, b.numeric.value_begin));
    }
    if (a.numeric.value_len != b.numeric.value_len) {
        return fail(sfmt("numeric.value_len %d != %d", a.numeric.value_len, b.numeric.value_len));
    }

    // -- teleport_fx
    if (!eq_float_bits(a.teleport_fx.charge, b.teleport_fx.charge)) {
        return fail(sfmt("teleport_fx.charge %.9g != %.9g", static_cast<double>(a.teleport_fx.charge),
            static_cast<double>(b.teleport_fx.charge)));
    }
    if (a.teleport_fx.burst != b.teleport_fx.burst) {
        return fail("teleport_fx.burst differs");
    }

    // -- floor（P3 で新設。**比べる側も足さないと往復検査が新しい欄を素通しする**）
    if (a.floor.dungeon_id != b.floor.dungeon_id) {
        return fail(sfmt("floor.dungeon_id %d != %d", a.floor.dungeon_id, b.floor.dungeon_id));
    }
    if (a.floor.dun_level != b.floor.dun_level) {
        return fail(sfmt("floor.dun_level %d != %d", a.floor.dun_level, b.floor.dun_level));
    }
    if (a.floor.generated_turn != b.floor.generated_turn) {
        return fail(sfmt("floor.generated_turn %llu != %llu",
            static_cast<unsigned long long>(a.floor.generated_turn),
            static_cast<unsigned long long>(b.floor.generated_turn)));
    }
    if (a.floor.kind != b.floor.kind) {
        return fail(sfmt("floor.kind %d != %d", a.floor.kind, b.floor.kind));
    }
    if (a.floor.place_name_utf8 != b.floor.place_name_utf8) {
        return fail("floor.place_name differs");
    }
    if (a.floor.wild_mode != b.floor.wild_mode) {
        return fail("floor.wild_mode differs");
    }
    if (a.floor.town_id != b.floor.town_id) {
        return fail(sfmt("floor.town_id %d != %d", a.floor.town_id, b.floor.town_id));
    }

    // -- lighting（P5 で新設。同上）
    if (a.lighting.day_minute != b.lighting.day_minute) {
        return fail(sfmt("lighting.day_minute %d != %d", a.lighting.day_minute, b.lighting.day_minute));
    }
    if (a.lighting.daytime != b.lighting.daytime) {
        return fail("lighting.daytime differs");
    }
    if (a.lighting.light_radius != b.lighting.light_radius) {
        return fail(sfmt("lighting.light_radius %d != %d", a.lighting.light_radius, b.lighting.light_radius));
    }

    // -- surroundings（環境音の層。同上）
    {
        const auto &x = a.surroundings;
        const auto &y = b.surroundings;
        const std::pair<const char *, std::pair<int, int>> fields[] = {
            { "grass", { x.grass, y.grass } },
            { "tree", { x.tree, y.tree } },
            { "dirt", { x.dirt, y.dirt } },
            { "swamp", { x.swamp, y.swamp } },
            { "water", { x.water, y.water } },
            { "deep_water", { x.deep_water, y.deep_water } },
            { "lava", { x.lava, y.lava } },
            { "rock", { x.rock, y.rock } },
            { "glass", { x.glass, y.glass } },
            { "wall", { x.wall, y.wall } },
            { "radius", { x.radius, y.radius } },
            { "counted", { x.counted, y.counted } },
        };
        for (const auto &[name, values] : fields) {
            if (values.first != values.second) {
                return fail(sfmt("surroundings.%s %d != %d", name, values.first, values.second));
            }
        }
    }

    return true;
}

} // namespace presentation
