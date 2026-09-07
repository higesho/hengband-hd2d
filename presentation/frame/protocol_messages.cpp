/*!
 * @file protocol_messages.cpp
 * @brief `protocol_messages.h` の実装（v1 §3・§4 の制御メッセージ）。
 *
 * 依存は JSON ライブラリだけ（中立性は protocol_messages.h 冒頭のとおり）。
 */
#include "frame/protocol_messages.h"

#include <nlohmann/json.hpp>

namespace presentation {

namespace {

using json = nlohmann::json;

//! 型が合っていれば取り出す。無い／型違いは既定のまま（v1 §2.3 の「未知キーは無視」の裏返し）。
void take_string(const json &obj, const char *key, std::string &out)
{
    const auto it = obj.find(key);
    if ((it != obj.end()) && it->is_string()) {
        out = it->get<std::string>();
    }
}

void take_int(const json &obj, const char *key, int &out)
{
    const auto it = obj.find(key);
    if ((it != obj.end()) && it->is_number_integer()) {
        out = it->get<int>();
    }
}

void take_bool(const json &obj, const char *key, bool &out)
{
    const auto it = obj.find(key);
    if ((it != obj.end()) && it->is_boolean()) {
        out = it->get<bool>();
    }
}

//! 入れ子オブジェクトを開く。無い／オブジェクトでなければ nullptr。
const json *take_object(const json &obj, const char *key)
{
    const auto it = obj.find(key);
    if ((it == obj.end()) || !it->is_object()) {
        return nullptr;
    }
    return &(*it);
}

/*!
 * @brief 整数配列を取り出す。
 * @param lo,hi 受け入れる値域（外れた要素は**捨てる**）。
 * @details `keys` は 1〜255（v1 §4.1）。範囲外を素通しするとコア側で `char` に
 * 詰められて別のキーに化けるので、ここで落とすのが唯一の防波堤になる。
 */
void take_int_array(const json &obj, const char *key, std::vector<int> &out, int lo, int hi)
{
    const auto it = obj.find(key);
    if ((it == obj.end()) || !it->is_array()) {
        return;
    }
    out.clear();
    for (const auto &element : *it) {
        if (!element.is_number_integer()) {
            continue;
        }
        const auto value = element.get<long long>();
        if ((value < lo) || (value > hi)) {
            continue;
        }
        out.push_back(static_cast<int>(value));
    }
}

//! 文字列配列を取り出す。要素に文字列以外が混じっていたらその要素だけ捨てる。
void take_string_array(const json &obj, const char *key, std::vector<std::string> &out)
{
    const auto it = obj.find(key);
    if ((it == obj.end()) || !it->is_array()) {
        return;
    }
    out.clear();
    for (const auto &element : *it) {
        if (element.is_string()) {
            out.push_back(element.get<std::string>());
        }
    }
}

/*!
 * @brief ペイロードを JSON オブジェクトとして開き、`"t"` が期待どおりかを確かめる。
 * @return 失敗なら false（`err` に理由）。
 */
bool open_object(const std::string &json_text, const char *expected_type, json &out, std::string &err)
{
    json parsed = json::parse(json_text, nullptr, false);
    if (parsed.is_discarded()) {
        err = std::string("broken JSON for \"") + expected_type + "\"";
        return false;
    }
    if (!parsed.is_object()) {
        err = std::string("payload is not a JSON object (expected \"") + expected_type + "\")";
        return false;
    }
    const auto it = parsed.find("t");
    if ((it == parsed.end()) || !it->is_string()) {
        err = std::string("payload has no string \"t\" (expected \"") + expected_type + "\")";
        return false;
    }
    const auto actual = it->get<std::string>();
    if (actual != expected_type) {
        err = std::string("message type is \"") + actual + "\" but \"" + expected_type + "\" was expected";
        return false;
    }
    out = std::move(parsed);
    return true;
}

} // namespace

std::string peek_message_type(const std::string &json_text)
{
    const json parsed = json::parse(json_text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::string();
    }
    const auto it = parsed.find("t");
    if ((it == parsed.end()) || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

std::string encode_hello(const HelloMessage &message)
{
    json out = json::object();
    out["t"] = "hello";
    out["protocol"] = message.protocol;
    out["ui_name"] = message.ui_name;
    out["ui_version"] = message.ui_version;
    if (!message.features.empty()) {
        out["features"] = message.features;
    }
    return out.dump();
}

std::string encode_hello_ack(const HelloAckMessage &message)
{
    json out = json::object();
    out["t"] = "hello_ack";
    out["protocol"] = message.protocol;
    out["core_name"] = message.core_name;
    out["core_version"] = message.core_version;
    if (!message.features.empty()) {
        out["features"] = message.features;
    }
    if (!message.asset_root_graf.empty() || !message.asset_root_slab.empty()) {
        json roots = json::object();
        if (!message.asset_root_graf.empty()) {
            roots["graf"] = message.asset_root_graf;
        }
        // 設計 §5.4（幻想蛮怒だけが申告する。変愚は空 → キーごと出ない）。
        if (!message.asset_root_slab.empty()) {
            roots["slab"] = message.asset_root_slab;
        }
        out["asset_roots"] = std::move(roots);
    }
    return out.dump();
}

std::string encode_fatal(const FatalMessage &message)
{
    json out = json::object();
    out["t"] = "fatal";
    out["reason"] = message.reason;
    return out.dump();
}

std::string encode_exit(const ExitMessage &message)
{
    json out = json::object();
    out["t"] = "exit";
    out["code"] = message.code;
    return out.dump();
}

bool decode_hello(const std::string &json_text, HelloMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "hello", parsed, err)) {
        return false;
    }
    HelloMessage work;
    take_int(parsed, "protocol", work.protocol);
    take_string(parsed, "ui_name", work.ui_name);
    take_string(parsed, "ui_version", work.ui_version);
    take_string_array(parsed, "features", work.features);
    out = std::move(work);
    return true;
}

bool decode_hello_ack(const std::string &json_text, HelloAckMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "hello_ack", parsed, err)) {
        return false;
    }
    HelloAckMessage work;
    take_int(parsed, "protocol", work.protocol);
    take_string(parsed, "core_name", work.core_name);
    take_string(parsed, "core_version", work.core_version);
    take_string_array(parsed, "features", work.features);
    const auto roots = parsed.find("asset_roots");
    if ((roots != parsed.end()) && roots->is_object()) {
        take_string(*roots, "graf", work.asset_root_graf);
        take_string(*roots, "slab", work.asset_root_slab);
    }
    out = std::move(work);
    return true;
}

bool decode_fatal(const std::string &json_text, FatalMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "fatal", parsed, err)) {
        return false;
    }
    FatalMessage work;
    take_string(parsed, "reason", work.reason);
    out = std::move(work);
    return true;
}

bool decode_exit(const std::string &json_text, ExitMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "exit", parsed, err)) {
        return false;
    }
    ExitMessage work;
    take_int(parsed, "code", work.code);
    out = std::move(work);
    return true;
}

/* ------------------------------------------------------------------ ui_state */

std::string encode_ui_state(const UiStateMessage &message)
{
    json out = json::object();
    out["t"] = "ui_state";

    json view = json::object();
    view["w"] = message.view_w;
    view["h"] = message.view_h;
    out["view_cells"] = std::move(view);
    out["camera_follow_player"] = message.camera_follow_player;

    json cells = json::array();
    for (const auto &panel : message.sub_panel_cells) {
        json one = json::object();
        one["cols"] = panel.cols;
        one["rows"] = panel.rows;
        cells.push_back(std::move(one));
    }
    out["sub_panel_cells"] = std::move(cells);

    out["cursor_mode"] = message.cursor_mode;
    if (message.has_sub_panel_kinds) {
        json kinds = json::array();
        for (const int kind : message.sub_panel_kinds) {
            kinds.push_back(kind);
        }
        out["sub_panel_kinds"] = std::move(kinds);
    }

    json style = json::object();
    style["graf_px"] = message.map_style_graf_px;
    style["graf_tag"] = message.map_style_graf_tag;
    out["map_style"] = std::move(style);

    json audio = json::object();
    audio["sound_on"] = message.sound_on;
    audio["music_on"] = message.music_on;
    audio["sound_volume_index"] = message.sound_volume_index;
    audio["sound_events"] = message.sound_events;
    audio["music_volume_index"] = message.music_volume_index;
    out["audio"] = std::move(audio);

    json bot = json::object();
    bot["enabled"] = message.bot_json_enabled;
    bot["path"] = message.bot_json_path;
    out["bot_json"] = std::move(bot);

    //! コア側の言語（実行時多言語化）。空なら送らない＝コアの既定を触らない。
    if (!message.lang.empty()) {
        out["lang"] = message.lang;
    }

    // リアルタイム進行。設定は画面側、時計はコア側にある。
    json realtime = json::object();
    realtime["enabled"] = message.realtime_enabled;
    realtime["speed_index"] = message.realtime_speed_index;
    realtime["prompt_live"] = message.realtime_prompt_live;
    realtime["self_span"] = message.realtime_self_span_index;
    out["realtime"] = std::move(realtime);

    return out.dump();
}

bool decode_ui_state(const std::string &json_text, UiStateMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "ui_state", parsed, err)) {
        return false;
    }
    UiStateMessage work;

    if (const json *view = take_object(parsed, "view_cells"); view != nullptr) {
        take_int(*view, "w", work.view_w);
        take_int(*view, "h", work.view_h);
    }
    take_bool(parsed, "camera_follow_player", work.camera_follow_player);

    if (const auto it = parsed.find("sub_panel_cells"); (it != parsed.end()) && it->is_array()) {
        int index = 0;
        for (const auto &element : *it) {
            if (index >= kProtocolSubPanelCount) {
                break; // 5 枚を超えた分は捨てる（枚数はプロトコルで固定）
            }
            if (element.is_object()) {
                take_int(element, "cols", work.sub_panel_cells[index].cols);
                take_int(element, "rows", work.sub_panel_cells[index].rows);
            }
            ++index;
        }
    }

    take_bool(parsed, "cursor_mode", work.cursor_mode);

    if (const auto it = parsed.find("sub_panel_kinds"); (it != parsed.end()) && it->is_array()) {
        work.has_sub_panel_kinds = true;
        int index = 0;
        for (const auto &element : *it) {
            if (index >= kProtocolSubPanelCount) {
                break;
            }
            if (element.is_number_integer()) {
                work.sub_panel_kinds[index] = element.get<int>();
            }
            ++index;
        }
    }

    if (const json *style = take_object(parsed, "map_style"); style != nullptr) {
        take_int(*style, "graf_px", work.map_style_graf_px);
        take_string(*style, "graf_tag", work.map_style_graf_tag);
    }
    if (const json *audio = take_object(parsed, "audio"); audio != nullptr) {
        take_bool(*audio, "sound_on", work.sound_on);
        take_bool(*audio, "music_on", work.music_on);
        take_int(*audio, "sound_volume_index", work.sound_volume_index);
        take_bool(*audio, "sound_events", work.sound_events);
        take_int(*audio, "music_volume_index", work.music_volume_index);
    }
    if (const json *bot = take_object(parsed, "bot_json"); bot != nullptr) {
        take_bool(*bot, "enabled", work.bot_json_enabled);
        take_string(*bot, "path", work.bot_json_path);
    }
    take_string(parsed, "lang", work.lang);
    if (const json *realtime = take_object(parsed, "realtime"); realtime != nullptr) {
        take_bool(*realtime, "enabled", work.realtime_enabled);
        take_int(*realtime, "speed_index", work.realtime_speed_index);
        take_bool(*realtime, "prompt_live", work.realtime_prompt_live);
        take_int(*realtime, "self_span", work.realtime_self_span_index);
    }

    out = std::move(work);
    return true;
}

/* ---------------------------------------------------------- keys / quit_request */

std::string encode_keys(const KeysMessage &message)
{
    json out = json::object();
    out["t"] = "keys";
    out["keys"] = message.keys;
    return out.dump();
}

bool decode_keys(const std::string &json_text, KeysMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "keys", parsed, err)) {
        return false;
    }
    KeysMessage work;
    take_int_array(parsed, "keys", work.keys, 1, 255);
    out = std::move(work);
    return true;
}

std::string encode_quit_request(const QuitRequestMessage &)
{
    json out = json::object();
    out["t"] = "quit_request";
    return out.dump();
}

bool decode_quit_request(const std::string &json_text, QuitRequestMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "quit_request", parsed, err)) {
        return false;
    }
    out = QuitRequestMessage{};
    return true;
}

/* -------------------------------------------------------------- pad_commands */

std::string encode_pad_commands(const PadCommandsMessage &message)
{
    json out = json::object();
    out["t"] = "pad_commands";
    out["current_keymap"] = message.current_keymap;
    json entries = json::array();
    for (const auto &entry : message.entries) {
        json one = json::object();
        one["id"] = entry.id;
        one["command"] = entry.command;
        one["group_utf8"] = entry.group_utf8;
        //! **空でも書く**（v1 の追補。読む側は「無い＝小分類なし」と同じに扱う）。
        one["subgroup_utf8"] = entry.subgroup_utf8;
        one["label_utf8"] = entry.label_utf8;
        // **空配列も必ず書く**（v1 §8 の「空配列 = 解決不能」を値として運ぶため。
        // 省略すると受信側が「未知＝解決を試みてよい」と誤読しうる）。
        one["seq_original"] = entry.seq_original;
        one["seq_rogue"] = entry.seq_rogue;
        entries.push_back(std::move(one));
    }
    out["entries"] = std::move(entries);
    return out.dump();
}

bool decode_pad_commands(const std::string &json_text, PadCommandsMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "pad_commands", parsed, err)) {
        return false;
    }
    PadCommandsMessage work;
    take_string(parsed, "current_keymap", work.current_keymap);
    if (const auto it = parsed.find("entries"); (it != parsed.end()) && it->is_array()) {
        for (const auto &element : *it) {
            if (!element.is_object()) {
                continue;
            }
            PadCommandWireEntry entry;
            take_int(element, "id", entry.id);
            take_int(element, "command", entry.command);
            take_string(element, "group_utf8", entry.group_utf8);
            //! 古いコアは書いてこない。**空のまま**＝小分類なし（2 段のまま）。
            take_string(element, "subgroup_utf8", entry.subgroup_utf8);
            take_string(element, "label_utf8", entry.label_utf8);
            take_int_array(element, "seq_original", entry.seq_original, 1, 255);
            take_int_array(element, "seq_rogue", entry.seq_rogue, 1, 255);
            work.entries.push_back(std::move(entry));
        }
    }
    out = std::move(work);
    return true;
}

/* ----------------------------------------------------------- sub_panel_kinds */

std::string encode_sub_panel_kinds(const SubPanelKindsMessage &message)
{
    json out = json::object();
    out["t"] = "sub_panel_kinds";
    json entries = json::array();
    for (const auto &entry : message.entries) {
        json one = json::object();
        one["flag"] = entry.flag;
        one["label_utf8"] = entry.label_utf8;
        entries.push_back(std::move(one));
    }
    out["entries"] = std::move(entries);
    return out.dump();
}

bool decode_sub_panel_kinds(const std::string &json_text, SubPanelKindsMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "sub_panel_kinds", parsed, err)) {
        return false;
    }
    SubPanelKindsMessage work;
    if (const auto it = parsed.find("entries"); (it != parsed.end()) && it->is_array()) {
        for (const auto &element : *it) {
            if (!element.is_object()) {
                continue;
            }
            SubPanelKindWireEntry entry;
            take_int(element, "flag", entry.flag);
            take_string(element, "label_utf8", entry.label_utf8);
            work.entries.push_back(std::move(entry));
        }
    }
    out = std::move(work);
    return true;
}

std::string encode_macro_triggers(const MacroTriggersMessage &message)
{
    json out = json::object();
    out["t"] = "macro_triggers";
    json patterns = json::array();
    for (const auto &pattern : message.patterns) {
        patterns.push_back(pattern);
    }
    out["patterns"] = std::move(patterns);
    return out.dump();
}

bool decode_macro_triggers(const std::string &json_text, MacroTriggersMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "macro_triggers", parsed, err)) {
        return false;
    }
    MacroTriggersMessage work;
    if (const auto it = parsed.find("patterns"); (it != parsed.end()) && it->is_array()) {
        for (const auto &element : *it) {
            if (!element.is_array() || element.empty()) {
                continue; // 空のトリガーは存在しない（1 件だけ捨てる。§2.3）
            }
            std::vector<int> pattern;
            bool ok = true;
            for (const auto &one_byte : element) {
                if (!one_byte.is_number_integer()) {
                    ok = false;
                    break;
                }
                const auto value = one_byte.get<long long>();
                if ((value < 1) || (value > 255)) {
                    ok = false;
                    break;
                }
                pattern.push_back(static_cast<int>(value));
            }
            if (ok) {
                work.patterns.push_back(std::move(pattern));
            }
        }
    }
    out = std::move(work);
    return true;
}

std::string encode_input_events(const InputEventsMessage &message)
{
    json out = json::object();
    out["t"] = "input_event";
    json events = json::array();
    for (const auto &event : message.events) {
        json one = json::object();
        one["e"] = event.e;
        if (event.e == "move") {
            one["dx"] = event.dx;
            one["dy"] = event.dy;
        } else if (event.e == "answer") {
            one["value"] = event.value;
        } else if (event.e == "key") {
            if (!event.name.empty()) {
                one["name"] = event.name;
            } else {
                one["char"] = event.chr;
                if (event.ctrl) {
                    one["ctrl"] = true;
                }
            }
        } else if (event.e == "text") {
            one["s"] = event.text;
        } else if (event.e == "fkey") {
            one["n"] = event.n;
            if (event.ctrl) {
                one["ctrl"] = true;
            }
            if (event.shift) {
                one["shift"] = true;
            }
            if (event.alt) {
                one["alt"] = true;
            }
        } else if (event.e == "set_number") {
            one["value"] = event.number;
            if (event.digits > 1) {
                one["digits"] = event.digits;
            }
        } else if (event.e == "keyseq") {
            one["bytes"] = event.bytes;
        }
        // "cancel" / "confirm" はタグだけ。
        events.push_back(std::move(one));
    }
    out["events"] = std::move(events);
    return out.dump();
}

bool decode_input_events(const std::string &json_text, InputEventsMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "input_event", parsed, err)) {
        return false;
    }
    InputEventsMessage work;
    const auto it = parsed.find("events");
    if ((it == parsed.end()) || !it->is_array()) {
        out = std::move(work);
        return true;
    }
    // 値域検査はここが唯一の防波堤（v1 §8.3。以降のアダプタは検査なしで読む）。
    // 通らないイベントは**その 1 件だけ**捨てる（§2.3 の要素単位の適用）。
    for (const auto &element : *it) {
        if (!element.is_object()) {
            continue;
        }
        InputEventWire event;
        take_string(element, "e", event.e);
        if (event.e == "move") {
            take_int(element, "dx", event.dx);
            take_int(element, "dy", event.dy);
            if ((event.dx < -1) || (event.dx > 1) || (event.dy < -1) || (event.dy > 1)) {
                continue;
            }
        } else if ((event.e == "cancel") || (event.e == "confirm")) {
            // タグだけ。
        } else if (event.e == "answer") {
            take_string(element, "value", event.value);
            if ((event.value != "yes") && (event.value != "no")) {
                continue;
            }
        } else if (event.e == "key") {
            take_string(element, "name", event.name);
            if (!event.name.empty()) {
                if ((event.name != "tab") && (event.name != "delete") && (event.name != "backspace")) {
                    continue;
                }
            } else {
                take_string(element, "char", event.chr);
                take_bool(element, "ctrl", event.ctrl);
                if ((event.chr.size() != 1) || (event.chr[0] < 0x20) || (event.chr[0] > 0x7E)) {
                    continue;
                }
            }
        } else if (event.e == "text") {
            take_string(element, "s", event.text);
            if (event.text.empty()) {
                continue;
            }
        } else if (event.e == "fkey") {
            take_int(element, "n", event.n);
            take_bool(element, "ctrl", event.ctrl);
            take_bool(element, "shift", event.shift);
            take_bool(element, "alt", event.alt);
            if ((event.n < 1) || (event.n > 12)) {
                continue;
            }
        } else if (event.e == "set_number") {
            if (const auto vit = element.find("value"); (vit != element.end()) && vit->is_number_integer()) {
                event.number = vit->get<long long>();
            }
            take_int(element, "digits", event.digits);
            if ((event.number < 0) || (event.digits < 1) || (event.digits > 10)) {
                continue;
            }
        } else if (event.e == "keyseq") {
            /*
             * 生のバイト列。**ここが唯一の防波堤**なので値域と長さを両方見る。
             * 長さの上限はマクロのトリガーに要る長さ（F キーで 6 バイト。
             * `input_event_adapter.cpp` の `fkey` 展開）から余裕を見た値で、
             * 「長い列を積んでコアを溺れさせる」経路を作らないために切る。
             */
            constexpr size_t kMaxKeyseq = 64;
            const auto bit = element.find("bytes");
            if ((bit == element.end()) || !bit->is_array() || bit->empty() || (bit->size() > kMaxKeyseq)) {
                continue;
            }
            bool ok = true;
            for (const auto &one_byte : *bit) {
                if (!one_byte.is_number_integer()) {
                    ok = false;
                    break;
                }
                const auto value = one_byte.get<long long>();
                if ((value < 1) || (value > 255)) {
                    ok = false; // 0 は「キーではない」（`push_key_back` が弾く値）
                    break;
                }
                event.bytes.push_back(static_cast<int>(value));
            }
            if (!ok) {
                continue;
            }
        } else {
            continue; // 未知の e はそのイベントだけ捨てる（v1 §8.3）
        }
        work.events.push_back(std::move(event));
    }
    out = std::move(work);
    return true;
}

std::string encode_asset_manifest(const AssetManifestMessage &message)
{
    json out = json::object();
    out["t"] = "asset_manifest";
    if (!message.root.empty()) {
        out["root"] = message.root; // 空 = ui の cwd 基準（キーごと省略。v1 §8.2）
    }
    json assets = json::array();
    for (const auto &entry : message.assets) {
        json one = json::object();
        one["i"] = entry.index;
        one["k"] = entry.kind;
        one["id"] = entry.id;
        one["p"] = entry.path;
        assets.push_back(std::move(one));
    }
    out["assets"] = std::move(assets);
    if (!message.aliases.empty()) {
        json aliases = json::array();
        for (const auto &alias : message.aliases) {
            json one = json::object();
            one["k"] = alias.kind;
            one["id"] = alias.id;
            one["i"] = alias.index;
            aliases.push_back(std::move(one));
        }
        out["aliases"] = std::move(aliases);
    }
    return out.dump();
}

bool decode_asset_manifest(const std::string &json_text, AssetManifestMessage &out, std::string &err)
{
    json parsed;
    if (!open_object(json_text, "asset_manifest", parsed, err)) {
        return false;
    }
    AssetManifestMessage work;
    take_string(parsed, "root", work.root);
    // tile_index は `frame` の cells と同じ uint16 空間（v1 §6.2）。範囲外・空パスは
    // ここで捨てるのが唯一の防波堤（受信側の辞書は uint16 で持つ）。
    constexpr int kIndexMax = 65535;
    if (const auto it = parsed.find("assets"); (it != parsed.end()) && it->is_array()) {
        for (const auto &element : *it) {
            if (!element.is_object()) {
                continue;
            }
            AssetManifestWireEntry entry;
            take_int(element, "i", entry.index);
            take_string(element, "k", entry.kind);
            take_int(element, "id", entry.id);
            take_string(element, "p", entry.path);
            if ((entry.index < 1) || (entry.index > kIndexMax) || entry.kind.empty() || entry.path.empty()) {
                continue;
            }
            work.assets.push_back(std::move(entry));
        }
    }
    if (const auto it = parsed.find("aliases"); (it != parsed.end()) && it->is_array()) {
        for (const auto &element : *it) {
            if (!element.is_object()) {
                continue;
            }
            AssetManifestAliasWireEntry alias;
            take_string(element, "k", alias.kind);
            take_int(element, "id", alias.id);
            take_int(element, "i", alias.index);
            if ((alias.index < 1) || (alias.index > kIndexMax) || alias.kind.empty()) {
                continue;
            }
            work.aliases.push_back(std::move(alias));
        }
    }
    out = std::move(work);
    return true;
}

} // namespace presentation
