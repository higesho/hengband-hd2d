/*!
 * @file ui_probe.cpp
 * @brief `ui_probe.h` の実装。
 */
#include "app/ui_probe.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace hd2d {
namespace {
using Json = nlohmann::json;

constexpr std::size_t kMaxTexts = 4000;
constexpr std::size_t kMaxButtons = 2000;
constexpr std::int64_t kRecentFxMs = 5000;
constexpr std::size_t kRecentFxKeep = 32;

std::int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool blank(std::string_view s)
{
    for (const char c : s) {
        if ((c != ' ') && (c != '\t') && (c != '\r') && (c != '\n')) {
            return false;
        }
    }
    return true;
}

Json rect_json(const ProbeRect &r)
{
    return Json{ { "x", r.x }, { "y", r.y }, { "w", r.w }, { "h", r.h } };
}

Json item_json(const ProbeItem &i)
{
    return Json{ { "id", i.id }, { "label", i.label }, { "cols", i.cols }, { "enabled", i.enabled }, { "selected", i.selected } };
}
} // namespace

UiProbe &ui_probe()
{
    static UiProbe probe;
    return probe;
}

void UiProbe::text(int x, int y, int w, int h, std::string_view utf8)
{
    this->text(x, y, w, h, utf8, ProbeTextStyle{});
}

void UiProbe::text(int x, int y, int w, int h, std::string_view utf8, const ProbeTextStyle &style)
{
    if (!this->enabled_ || blank(utf8) || (this->cur_.texts.size() >= kMaxTexts)) {
        return;
    }
    // 地図の 1 マス 1 字（アスキー地図）は問い合わせの対象にしない（`map` の欄で答える）。
    if (std::string_view(this->layer_) == "map") {
        return;
    }
    // 同じ字が同じ所へ 2 度積まれる（影を付ける描き方・二重に流す描き方）ものは 1 つにする。
    const std::size_t begin = (this->cur_.texts.size() > 12) ? (this->cur_.texts.size() - 12) : 0;
    for (std::size_t i = begin; i < this->cur_.texts.size(); ++i) {
        const ProbeText &t = this->cur_.texts[i];
        if ((t.x == x) && (t.y == y) && (t.text == utf8)) {
            return;
        }
    }
    ProbeText t;
    t.text = std::string(utf8);
    t.x = x;
    t.y = y;
    t.w = w;
    t.h = h;
    t.layer = this->layer_;
    const auto to_byte = [](float v) { return std::clamp(static_cast<int>((v * 255.f) + 0.5f), 0, 255); };
    t.r = to_byte(style.r);
    t.g = to_byte(style.g);
    t.b = to_byte(style.b);
    t.alpha = style.a;
    t.clipped = style.clipped;
    t.missing = style.missing;
    t.replacement = style.replacement;
    this->cur_.texts.push_back(std::move(t));
    this->touched_ = true;
}

void UiProbe::button(std::string label, const ProbeRect &rect, bool enabled, bool selected, const char *kind,
    bool touch_only, std::string id)
{
    if (!this->enabled_ || (rect.w <= 0) || (rect.h <= 0) || (this->cur_.buttons.size() >= kMaxButtons)) {
        return;
    }
    for (const ProbeButton &b : this->cur_.buttons) {
        if ((b.rect.x == rect.x) && (b.rect.y == rect.y) && (b.rect.w == rect.w) && (b.rect.h == rect.h)
            && (b.label == label) && (b.kind == kind)) {
            return;
        }
    }
    ProbeButton b;
    b.label = std::move(label);
    b.id = std::move(id);
    b.rect = rect;
    b.enabled = enabled;
    b.selected = selected;
    b.kind = kind;
    b.touch_only = touch_only;
    b.layer = this->layer_;
    this->cur_.buttons.push_back(std::move(b));
    this->touched_ = true;
}

void UiProbe::window(ProbeWindow window)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.windows.push_back(std::move(window));
    this->touched_ = true;
}

void UiProbe::hud_values(int hp, int hp_max, int sp, int sp_max, bool has_sp, bool orb, std::string design)
{
    if (!this->enabled_) {
        return;
    }
    Hud &h = this->cur_.hud;
    h.present = true;
    h.hp = hp;
    h.hp_max = hp_max;
    h.sp = sp;
    h.sp_max = sp_max;
    h.has_sp = has_sp;
    h.orb = orb;
    h.design = std::move(design);
    this->touched_ = true;
}

void UiProbe::hud_number_text(bool hp, std::string text)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.hud.present = true;
    (hp ? this->cur_.hud.hp_text : this->cur_.hud.sp_text) = std::move(text);
    this->touched_ = true;
}

void UiProbe::hud_icon(ProbeIcon icon)
{
    if (!this->enabled_) {
        return;
    }
    for (const ProbeIcon &i : this->cur_.hud.icons) {
        if ((i.key == icon.key) && (i.permanent == icon.permanent)) {
            return;
        }
    }
    this->cur_.hud.present = true;
    this->cur_.hud.icons.push_back(std::move(icon));
    this->touched_ = true;
}

void UiProbe::hud_doomsday(int days_left)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.hud.present = true;
    this->cur_.hud.doomsday_days = days_left;
    this->touched_ = true;
}

void UiProbe::hud_name(std::string name, int level, int gold)
{
    if (!this->enabled_) {
        return;
    }
    Hud &h = this->cur_.hud;
    h.present = true;
    h.has_name = true;
    h.name = std::move(name);
    h.level = level;
    h.gold = gold;
    this->touched_ = true;
}

void UiProbe::hud_depth(std::string text)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.hud.present = true;
    this->cur_.hud.depth = std::move(text);
    this->touched_ = true;
}

void UiProbe::map(int player_gx, int player_gy, int players, int monsters, int objects, bool visible, std::string view)
{
    if (!this->enabled_) {
        return;
    }
    Map &m = this->cur_.map;
    m.present = true;
    m.gx = player_gx;
    m.gy = player_gy;
    m.players = players;
    m.monsters = monsters;
    m.objects = objects;
    m.visible = visible;
    m.view = std::move(view);
    this->touched_ = true;
}

void UiProbe::map_cells(std::vector<ProbeCell> cells, std::string floor_key, std::vector<std::string> floor_rows)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.cells = std::move(cells);
    this->cur_.floor_key = std::move(floor_key);
    this->cur_.floor_rows = std::move(floor_rows);
    this->touched_ = true;
}

void UiProbe::set_cell_projector(ProbeCellProjector projector)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.projector = std::move(projector);
    this->touched_ = true;
}

void UiProbe::fx_started(std::string attr, std::string shape, std::string kind)
{
    if (!this->enabled_) {
        return;
    }
    ++this->fx_started_total_;
    this->fx_recent_.push_back(FxStart{ now_ms(), std::move(attr), std::move(shape), std::move(kind) });
    if (this->fx_recent_.size() > kRecentFxKeep) {
        this->fx_recent_.erase(this->fx_recent_.begin(),
            this->fx_recent_.begin() + static_cast<std::ptrdiff_t>(this->fx_recent_.size() - kRecentFxKeep));
    }
}

void UiProbe::fx_active(std::vector<std::string> names)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.fx_names = std::move(names);
    this->touched_ = true;
}

void UiProbe::messages(std::vector<std::string> lines)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.messages = std::move(lines);
    this->touched_ = true;
}

void UiProbe::notice(std::string text)
{
    if (!this->enabled_ || text.empty()) {
        return;
    }
    if (std::find(this->cur_.notices.begin(), this->cur_.notices.end(), text) == this->cur_.notices.end()) {
        this->cur_.notices.push_back(std::move(text));
    }
    this->touched_ = true;
}

void UiProbe::targeting(bool active, std::string mode, std::string label)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.has_targeting = true;
    this->cur_.targeting_active = active;
    this->cur_.targeting_mode = std::move(mode);
    this->cur_.targeting_label = std::move(label);
    this->touched_ = true;
}

void UiProbe::targeting_detail(ProbeTargetingDetail detail)
{
    if (!this->enabled_) {
        return;
    }
    detail.present = true;
    this->cur_.targeting_detail = std::move(detail);
    this->touched_ = true;
}

void UiProbe::light(const ProbeLight &light)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.light = light;
    this->cur_.light.present = true;
    this->touched_ = true;
}

void UiProbe::overlays(std::vector<ProbeOverlay> overlays, int drawn)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.overlays = std::move(overlays);
    this->cur_.overlays_drawn = drawn;
    this->touched_ = true;
}

void UiProbe::element_scope(std::vector<ProbeScopeCell> cells, int drawn)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.has_element_scope = true;
    this->cur_.element_scope = std::move(cells);
    this->cur_.element_scope_drawn = drawn;
    this->touched_ = true;
}

void UiProbe::entities(std::vector<ProbeEntity> entities, int rings_drawn)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.entities = std::move(entities);
    this->cur_.rings_drawn = rings_drawn;
    this->touched_ = true;
}

void UiProbe::window_state(const ProbeWindowState &state)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.window_state = state;
    this->cur_.window_state.present = true;
    this->touched_ = true;
}

void UiProbe::time_state(const ProbeTimeState &state)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.time_state = state;
    this->cur_.time_state.present = true;
    this->touched_ = true;
}

void UiProbe::weather(const ProbeWeather &weather)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.weather = weather;
    this->cur_.weather.present = true;
    this->touched_ = true;
}

void UiProbe::cutin_stage(std::string stage)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.cutin_stage = std::move(stage);
    this->touched_ = true;
}

void UiProbe::map_tile_names(std::map<int, std::string> names)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.tile_names = std::move(names);
    this->touched_ = true;
}

void UiProbe::liveness(bool core_alive, int exit_code)
{
    if (!this->enabled_) {
        return;
    }
    this->cur_.has_liveness = true;
    this->cur_.core_alive = core_alive;
    this->cur_.core_exit_code = exit_code;
    this->touched_ = true;
}

void UiProbe::frame_info(std::string core, std::uint64_t frame_seq, int screen_w, int screen_h, std::string phase,
    std::string input_wait)
{
    if (!this->enabled_) {
        return;
    }
    const std::int64_t now = now_ms();
    // コアのフレームの番号が変わった時刻と、入力待ちの種類が変わった時刻は、描き直しをまたいで持つ。
    if ((frame_seq != this->last_frame_id_) || (this->frame_changed_ms_ == 0)) {
        this->last_frame_id_ = frame_seq;
        this->frame_changed_ms_ = now;
    }
    if (input_wait != this->wait_kind_) {
        this->wait_kind_ = input_wait;
        this->wait_since_ms_ = now;
    }
    this->cur_.core = std::move(core);
    this->cur_.frame_seq = frame_seq;
    this->cur_.screen_w = screen_w;
    this->cur_.screen_h = screen_h;
    this->cur_.phase = std::move(phase);
    this->cur_.input_wait = std::move(input_wait);
    this->touched_ = true;
}

void UiProbe::commit(std::string_view poll_phase)
{
    if (!this->enabled_ || !this->touched_) {
        return;
    }
    this->cur_.valid = true;
    this->cur_.poll_phase = std::string(poll_phase);
    this->cur_.draw_seq = ++this->draw_seq_;
    // 画面の大きさは、フレーム情報を積まない相（系譜など）では前の確定から引き継ぐ。
    if (this->cur_.screen_w <= 0) {
        this->cur_.screen_w = this->last_.screen_w;
        this->cur_.screen_h = this->last_.screen_h;
    }
    // 入口の番号は階ごとに覚える（ビューポートの外へ出ても残す。階が変わったら捨てる）。
    if (this->cur_.floor_key != this->entrance_floor_) {
        this->entrance_floor_ = this->cur_.floor_key;
        this->entrances_.clear();
    }
    for (const ProbeCell &cell : this->cur_.cells) {
        if (cell.entrance != 0) {
            this->entrances_[{ cell.gx, cell.gy }] = cell.entrance;
        }
    }
    // 字の欠け・はみ出し。最新のフレームの数は毎回数え直し、累計は同じ字（種類・レイヤー・字）を 1 回と数える。
    this->issue_last_.clear();
    this->issue_layers_.clear();
    {
        const auto add_issue = [this](const char *kind, const ProbeText &t, int amount) {
            this->issue_last_[kind] += amount;
            this->issue_layers_[t.layer][kind] += amount;
            const std::string key = std::string(kind) + "|" + t.layer + "|" + t.text;
            if (this->issue_seen_.insert(key).second) {
                this->issue_totals_[kind] += amount;
                if (this->issue_samples_.size() < 40) {
                    this->issue_samples_.push_back(TextIssue{ kind, t.text, t.layer, t.x, t.y, t.w, t.h });
                }
            }
        };
        const int sw = this->cur_.screen_w;
        const int sh = this->cur_.screen_h;
        for (const ProbeText &t : this->cur_.texts) {
            if (t.alpha <= 0.01f) {
                continue;
            }
            if (t.replacement > 0) {
                add_issue("replacement_chars", t, t.replacement);
            }
            if (t.missing > 0) {
                add_issue("missing_glyphs", t, t.missing);
            }
            if (t.clipped) {
                add_issue("clipped", t, 1);
            }
            // 階の案内（`cutin`）の字は、画面の外から流れ込む演出なので数えない。
            if ((sw > 0) && (sh > 0) && (std::string_view(t.layer) != "cutin")
                && ((t.x < 0) || (t.y < 0) || ((t.x + t.w) > sw) || ((t.y + t.h) > sh))) {
                add_issue("off_screen", t, 1);
            }
            // 新しい UI の窓の中の字は、窓の矩形に収まる（窓の矩形は 1 枚〜数枚。字の左上を含む窓の中で見る）。
            if (std::string_view(t.layer) == "window") {
                bool known = false;
                bool inside = false;
                for (const ProbeWindow &w : this->cur_.windows) {
                    if (!w.has_rect || (w.kind == "feature-menu") || (w.kind == "cutin") || (w.kind == "core-select")) {
                        continue;
                    }
                    known = true;
                    if ((t.x >= w.rect.x) && (t.y >= w.rect.y) && ((t.x + t.w) <= (w.rect.x + w.rect.w))
                        && ((t.y + t.h) <= (w.rect.y + w.rect.h))) {
                        inside = true;
                        break;
                    }
                }
                if (known && !inside) {
                    add_issue("outside_window", t, 1);
                }
            }
        }
    }
    // 通知は、前のフレームに無かったものが出たときだけ回数を増やす（出しっぱなしでは増えない）。
    for (const std::string &n : this->cur_.notices) {
        if (std::find(this->last_.notices.begin(), this->last_.notices.end(), n) == this->last_.notices.end()) {
            ++this->notice_counts_[n];
        }
    }
    this->last_ = std::move(this->cur_);
    this->cur_ = Frame{};
    this->touched_ = false;
}

std::string UiProbe::snapshot_json(std::string_view poll_phase, std::string_view map_mode, bool detail) const
{
    const Frame &f = this->last_;
    const std::int64_t now = now_ms();
    std::string phase(poll_phase);
    if (f.valid && (poll_phase == "game") && !f.phase.empty()) {
        phase = f.phase;
    }
    Json out = Json::object();
    out["valid"] = f.valid;
    out["phase"] = phase;
    out["core"] = f.core;
    out["frame_seq"] = f.frame_seq;
    out["draw_seq"] = f.draw_seq;
    out["screen"] = Json{ { "w", f.screen_w }, { "h", f.screen_h } };
    out["input_wait"] = Json{ { "kind", f.input_wait.empty() ? std::string("none") : f.input_wait },
        { "since_ms", (this->wait_since_ms_ > 0) ? (now - this->wait_since_ms_) : 0 } };
    out["liveness"] = Json{ { "core_alive", f.has_liveness && f.core_alive },
        { "core_exit_code", (f.has_liveness && f.core_exit_code >= 0) ? Json(f.core_exit_code) : Json(nullptr) },
        { "frame_age_ms", (this->frame_changed_ms_ > 0 && f.has_liveness) ? Json(now - this->frame_changed_ms_) : Json(-1) } };

    Json texts = Json::array();
    for (const ProbeText &t : f.texts) {
        Json j = Json{ { "text", t.text }, { "x", t.x }, { "y", t.y }, { "w", t.w }, { "h", t.h }, { "layer", t.layer } };
        // 描いた色（頂点の色そのもの）。透明度が 1 でないときだけ `alpha` も出す。
        char hex[16];
        std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", t.r, t.g, t.b);
        j["color"] = std::string(hex);
        if (t.alpha < 0.999f) {
            j["alpha"] = t.alpha;
        }
        if (t.clipped) {
            j["clipped"] = true;
        }
        if (t.missing > 0) {
            j["missing_glyphs"] = t.missing;
        }
        if (t.replacement > 0) {
            j["replacement_chars"] = t.replacement;
        }
        texts.push_back(std::move(j));
    }
    out["texts"] = std::move(texts);

    // 字の欠け・はみ出し。キーの数は最新のフレーム、`total` は開始からの累計（同じ字は 1 回）、`samples` は見つけた字の見本。
    {
        static const char *const kKinds[] = { "replacement_chars", "missing_glyphs", "clipped", "off_screen", "outside_window" };
        Json issues = Json::object();
        Json total = Json::object();
        for (const char *kind : kKinds) {
            const auto last = this->issue_last_.find(kind);
            issues[kind] = (last != this->issue_last_.end()) ? last->second : 0;
            const auto sum = this->issue_totals_.find(kind);
            total[kind] = (sum != this->issue_totals_.end()) ? sum->second : 0;
        }
        issues["total"] = std::move(total);
        // レイヤー別（サブパネルの長い行は設計どおり幅で切られる。レイヤーごとに見られるようにする）。
        Json by_layer = Json::object();
        for (const auto &layer : this->issue_layers_) {
            Json counts = Json::object();
            for (const auto &entry : layer.second) {
                counts[entry.first] = entry.second;
            }
            by_layer[layer.first] = std::move(counts);
        }
        issues["by_layer"] = std::move(by_layer);
        Json samples = Json::array();
        for (const TextIssue &s : this->issue_samples_) {
            samples.push_back(Json{ { "kind", s.kind }, { "text", s.text }, { "layer", s.layer }, { "x", s.x }, { "y", s.y },
                { "w", s.w }, { "h", s.h } });
        }
        issues["samples"] = std::move(samples);
        out["text_issues"] = std::move(issues);
    }

    Json buttons = Json::array();
    for (const ProbeButton &b : f.buttons) {
        Json j = rect_json(b.rect);
        j["label"] = b.label;
        j["id"] = b.id;
        j["enabled"] = b.enabled;
        j["selected"] = b.selected;
        j["kind"] = b.kind;
        j["layer"] = b.layer;
        if (b.touch_only) {
            j["touch_only"] = true;
        }
        buttons.push_back(std::move(j));
    }
    out["buttons"] = std::move(buttons);

    Json windows = Json::array();
    for (const ProbeWindow &w : f.windows) {
        Json j = Json::object();
        j["kind"] = w.kind;
        j["title"] = w.title;
        j["stage"] = w.stage;
        j["type"] = w.type;
        j["prompt"] = w.prompt;
        j["rect"] = w.has_rect ? rect_json(w.rect) : Json(nullptr);
        j["revision"] = w.revision;
        Json items = Json::array();
        for (const ProbeItem &i : w.items) {
            items.push_back(item_json(i));
        }
        j["items"] = std::move(items);
        j["selected"] = w.selected;
        if (w.has_numeric) {
            j["numeric"] = Json{ { "value", w.numeric_value }, { "min", w.numeric_min }, { "max", w.numeric_max } };
        }
        if (w.gold >= 0) {
            j["gold"] = w.gold;
        }
        windows.push_back(std::move(j));
    }
    out["windows"] = std::move(windows);

    Json hud = Json::object();
    if (f.hud.present) {
        hud["hp"] = f.hud.hp;
        hud["hp_max"] = f.hud.hp_max;
        if (f.hud.has_sp) {
            hud["sp"] = f.hud.sp;
            hud["sp_max"] = f.hud.sp_max;
        }
        hud["orb"] = f.hud.orb;
        hud["design"] = f.hud.design;
        hud["hp_text"] = f.hud.hp_text;
        hud["sp_text"] = f.hud.sp_text;
        if (f.hud.has_name) {
            hud["name"] = f.hud.name;
            hud["level"] = f.hud.level;
            hud["gold"] = f.hud.gold;
        }
        hud["depth"] = f.hud.depth;
        Json icons = Json::array();
        for (const ProbeIcon &i : f.hud.icons) {
            icons.push_back(Json{ { "key", i.key }, { "label", i.label }, { "amount", i.amount },
                { "icon_id", i.icon_id }, { "permanent", i.permanent }, { "bad", i.bad } });
        }
        hud["icons"] = std::move(icons);
        if (f.hud.doomsday_days >= 0) {
            hud["doomsday_days"] = f.hud.doomsday_days;
        }
    }
    out["hud"] = std::move(hud);

    Json map = Json::object();
    if (f.map.present) {
        map["player"] = Json{ { "gx", f.map.gx }, { "gy", f.map.gy } };
        map["entities"] = f.map.players + f.map.monsters + f.map.objects;
        map["players"] = f.map.players;
        map["monsters"] = f.map.monsters;
        map["objects"] = f.map.objects;
        map["visible"] = f.map.visible;
        map["view"] = f.map.view;
    }
    if (f.map.present && (map_mode != "none")) {
        // 画面が描いた地形・実体。`near` はプレイヤーの周り（±12 マス）、`full` はビューポートの全部とフロア全域の格子。
        const bool full = (map_mode == "full");
        constexpr int kNear = 12;
        Json list = Json::array();
        Json monsters = Json::array();
        Json objects = Json::array();
        for (const ProbeCell &cell : f.cells) {
            if (!full && ((std::abs(cell.gx - f.map.gx) > kNear) || (std::abs(cell.gy - f.map.gy) > kNear))) {
                continue;
            }
            Json j = Json{ { "gx", cell.gx }, { "gy", cell.gy }, { "kind", cell.kind }, { "glyph", std::string(1, cell.glyph) } };
            if (cell.entrance != 0) {
                j["entrance"] = std::string(1, cell.entrance);
            }
            int sx = 0, sy = 0;
            bool inside = false;
            if (f.projector && f.projector(cell.gx, cell.gy, sx, sy, inside)) {
                j["screen"] = Json{ { "x", sx }, { "y", sy } };
                j["on_screen"] = inside;
            }
            if (detail) {
                // 詳細：地形番号・使った絵・色・光・町の意味づけ。
                j["feat"] = cell.terrain_id;
                if (cell.tile_index > 0) {
                    j["tile_index"] = cell.tile_index;
                    const auto name = f.tile_names.find(cell.tile_index);
                    j["tile"] = (name != f.tile_names.end()) ? name->second : std::string();
                }
                char hex[16];
                std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", cell.fg_rgb[0], cell.fg_rgb[1], cell.fg_rgb[2]);
                j["fg"] = cell.fg_color;
                j["color"] = std::string(hex);
                j["light_level"] = cell.light_level;
                if (f.light.declared) {
                    j["lit"] = cell.lit;
                }
                if ((cell.role != nullptr) && (*cell.role != '\0')) {
                    j["role"] = cell.role;
                    j["site"] = cell.site;
                }
            }
            list.push_back(std::move(j));
            if (cell.monster > 0) {
                monsters.push_back(Json{ { "gx", cell.gx }, { "gy", cell.gy }, { "id", cell.monster } });
            }
            if (cell.object > 0) {
                objects.push_back(Json{ { "gx", cell.gx }, { "gy", cell.gy }, { "id", cell.object } });
            }
        }
        Json entrances = Json::array();
        for (const auto &entry : this->entrances_) {
            if (!full && ((std::abs(entry.first.first - f.map.gx) > kNear) || (std::abs(entry.first.second - f.map.gy) > kNear))) {
                continue;
            }
            entrances.push_back(Json{ { "gx", entry.first.first }, { "gy", entry.first.second }, { "label", std::string(1, entry.second) } });
        }
        Json cells = Json{ { "region", full ? "full" : "near" }, { "list", std::move(list) }, { "monsters", std::move(monsters) },
            { "objects", std::move(objects) }, { "entrances", std::move(entrances) },
            { "player", Json{ { "gx", f.map.gx }, { "gy", f.map.gy } } } };
        if (full && !f.floor_rows.empty()) {
            cells["floor"] = Json{ { "w", static_cast<int>(f.floor_rows.front().size()) }, { "h", static_cast<int>(f.floor_rows.size()) },
                { "rows", f.floor_rows },
                { "legend", " 未踏  . 床  # 壁  + 扉  > 階段  ! アイテム  m 敵  @ 自分  ^ 山  ~ 水  T 木  \" 草  L 溶岩  s 雪" } };
        }
        map["cells"] = std::move(cells);
    }
    if (f.map.present) {
        // 光の届く範囲：`lit_cells` はコアが照らしたマスの数（送らないコアは 0。`light.declared` が偽）。
        if (f.light.present) {
            static const char *const kLightKinds[] = { "none", "torch", "lantern", "artifact", "other", "dark" };
            const int kind = f.light.kind;
            map["lit_cells"] = f.light.lit_cells;
            map["light"] = Json{ { "declared", f.light.declared }, { "kind", kind },
                { "kind_name", ((kind >= 0) && (kind <= 5)) ? kLightKinds[kind] : "unknown" }, { "radius", f.light.radius },
                { "lit_cells", f.light.lit_cells }, { "shown_cells", f.light.shown_cells },
                { "max_distance", f.light.max_distance }, { "dark_side", kind == 5 }, { "memory_dim", f.light.memory_dim },
                { "memory_cells", f.light.memory_cells } };
        }
        if (map_mode != "none") {
            Json overlays = Json::array();
            for (const ProbeOverlay &o : f.overlays) {
                overlays.push_back(Json{ { "gx", o.gx }, { "gy", o.gy }, { "text", o.text }, { "color", o.color } });
            }
            map["overlays"] = std::move(overlays);
            map["overlays_drawn"] = f.overlays_drawn;
            // エレメントスコープ（TOband）。コアが四角を送ったフレームだけ出る。
            if (f.has_element_scope) {
                Json scope_cells = Json::array();
                for (const ProbeScopeCell &c : f.element_scope) {
                    scope_cells.push_back(Json::array({ c.gx, c.gy, c.color, c.rgb[0], c.rgb[1], c.rgb[2] }));
                }
                map["element_scope"] = Json{ { "drawn", f.element_scope_drawn }, { "cells", std::move(scope_cells) },
                    { "cell_format", "[gx, gy, TERM_COLOR, r, g, b]" } };
            }
            Json entities = Json::array();
            for (const ProbeEntity &e : f.entities) {
                Json j = Json{ { "gx", e.gx }, { "gy", e.gy }, { "kind", e.kind }, { "id", e.id } };
                if (std::string_view(e.kind) == "monster") {
                    j["slot"] = e.slot;
                    j["ring"] = (*e.alert != '\0') ? std::string(e.alert) : std::string("none");
                    j["ring_drawn"] = e.ring_drawn;
                }
                if (detail && (e.tile_index > 0)) {
                    j["tile_index"] = e.tile_index;
                    const auto name = f.tile_names.find(e.tile_index);
                    j["tile"] = (name != f.tile_names.end()) ? name->second : std::string();
                }
                entities.push_back(std::move(j));
            }
            map["entity_list"] = std::move(entities);
            map["rings_drawn"] = f.rings_drawn;
        }
        if (f.time_state.present) {
            map["time"] = Json{ { "day_minute", f.time_state.day_minute }, { "daytime", f.time_state.daytime },
                { "night_factor", f.time_state.night_factor } };
        }
        // 天候（コアが申告したときだけ）。画面が描いている量・風で流れた量・昼の割合。
        if (f.weather.present) {
            map["weather"] = Json{ { "rain", f.weather.rain }, { "wind", f.weather.wind }, { "temp", f.weather.temp },
                { "rain_amount", f.weather.rain_amount }, { "snow_amount", f.weather.snow_amount },
                { "wind_x", f.weather.wind_x }, { "wind_y", f.weather.wind_y }, { "right_x", f.weather.right_x },
                { "right_y", f.weather.right_y }, { "icon", f.weather.icon }, { "dust_amount", f.weather.dust_amount },
                { "daylight", f.weather.daylight } };
        }
    }
    out["map"] = std::move(map);

    Json recent = Json::array();
    for (const FxStart &s : this->fx_recent_) {
        if ((now - s.at_ms) <= kRecentFxMs) {
            recent.push_back(Json{ { "attr", s.attr }, { "shape", s.shape }, { "kind", s.kind }, { "age_ms", now - s.at_ms } });
        }
    }
    out["fx"] = Json{ { "active", f.fx_names.size() }, { "names", f.fx_names }, { "spawned_total", this->fx_started_total_ },
        { "recent", std::move(recent) } };

    out["messages"] = f.messages;

    Json notices = Json::array();
    for (const std::string &n : f.notices) {
        const auto it = this->notice_counts_.find(n);
        notices.push_back(Json{ { "text", n }, { "count", (it != this->notice_counts_.end()) ? it->second : 1 } });
    }
    out["notices"] = std::move(notices);

    // 階：階層の文字と、階の案内（カットイン）。
    Json cutin = Json{ { "active", false }, { "text", "" }, { "stage", f.cutin_stage } };
    Json menu = Json{ { "open", false }, { "tab", "" }, { "stage", "" }, { "rows", Json::array() }, { "selected", -1 } };
    for (const ProbeWindow &w : f.windows) {
        if (w.kind == "cutin") {
            std::string text;
            for (const ProbeItem &i : w.items) {
                if (!i.label.empty()) {
                    text += (text.empty() ? "" : " ") + i.label;
                }
            }
            cutin = Json{ { "active", true }, { "text", text }, { "stage", f.cutin_stage } };
        } else if (w.kind == "feature-menu") {
            Json rows = Json::array();
            for (const ProbeItem &i : w.items) {
                rows.push_back(Json{ { "id", i.id }, { "label", i.label }, { "enabled", i.enabled } });
            }
            menu = Json{ { "open", true }, { "tab", w.title }, { "stage", w.stage }, { "rows", std::move(rows) },
                { "selected", w.selected } };
        }
    }
    out["floor"] = Json{ { "depth_label", f.hud.depth }, { "cutin", std::move(cutin) } };
    out["menu"] = std::move(menu);
    Json targeting = Json{ { "active", f.has_targeting && f.targeting_active },
        { "mode", (f.has_targeting && f.targeting_active) ? f.targeting_mode : std::string() },
        { "target_label", (f.has_targeting && f.targeting_active) ? f.targeting_label : std::string() } };
    targeting["area_cells"] = Json::array();
    targeting["path_cells"] = Json::array();
    if (f.has_targeting && f.targeting_active && f.targeting_detail.present) {
        // 追加の欄：どの経路で出ているか・いま指しているものの種類・効果の範囲のマス。
        const ProbeTargetingDetail &d = f.targeting_detail;
        targeting["via"] = d.ui_active ? "targeting-ui" : (d.band ? "prompt-band" : "");
        targeting["band"] = d.band;
        if (d.band) {
            targeting["band_text"] = d.band_text;
        }
        if (d.ui_active) {
            targeting["ui_mode"] = d.ui_mode;
            targeting["ui_kind"] = d.ui_kind;
            targeting["can_confirm"] = d.can_confirm;
            targeting["extras_open"] = d.extras_open;
            targeting["candidates"] = d.candidates;
            targeting["markers"] = d.markers;
            targeting["distance"] = d.distance;
            targeting["source"] = Json{ { "gx", d.source_x }, { "gy", d.source_y } };
            if (d.has_selected) {
                targeting["selected"] = Json{ { "gx", d.selected_x }, { "gy", d.selected_y }, { "kind", d.selected_kind },
                    { "id", d.selected_id } };
            }
        }
        Json area = Json::array();
        for (const ProbeTargetingDetail::Cell &c : d.area) {
            area.push_back(Json{ { "gx", c.x }, { "gy", c.y }, { "possible", c.possible } });
        }
        targeting["area_cells"] = std::move(area);
        Json path = Json::array();
        for (const ProbeTargetingDetail::Cell &c : d.path) {
            path.push_back(Json{ { "gx", c.x }, { "gy", c.y }, { "possible", c.possible } });
        }
        targeting["path_cells"] = std::move(path);
    }
    out["targeting"] = std::move(targeting);
    if (f.window_state.present) {
        const ProbeWindowState &s = f.window_state;
        out["app_window"] = Json{ { "input_focus", s.input_focus }, { "mouse_focus", s.mouse_focus }, { "shown", s.shown },
            { "minimized", s.minimized }, { "test_hidden", s.test_hidden }, { "pad_focus", s.pad_focus }, { "w", s.w }, { "h", s.h } };
    }
    return out.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string UiProbe::cell_json(int gx, int gy) const
{
    const Frame &f = this->last_;
    Json out = Json{ { "gx", gx }, { "gy", gy }, { "projectable", false }, { "on_screen", false }, { "screen", nullptr },
        { "kind", "unknown" }, { "glyph", "" } };
    int sx = 0, sy = 0;
    bool inside = false;
    if (f.valid && f.projector && f.projector(gx, gy, sx, sy, inside)) {
        out["projectable"] = true;
        out["on_screen"] = inside;
        out["screen"] = Json{ { "x", sx }, { "y", sy } };
    }
    for (const ProbeCell &cell : f.cells) {
        if ((cell.gx == gx) && (cell.gy == gy)) {
            out["kind"] = cell.kind;
            out["glyph"] = std::string(1, cell.glyph);
            if (cell.entrance != 0) {
                out["entrance"] = std::string(1, cell.entrance);
            }
            break;
        }
    }
    return out.dump(-1, ' ', false, Json::error_handler_t::replace);
}

bool UiProbe::find_button(const TapQuery &query, ProbeButton &out, std::string &error) const
{
    error.clear();
    if (!this->last_.valid) {
        error = "no frame has been drawn yet";
        return false;
    }
    const bool by_id = !query.id.empty();
    const std::string shown = by_id ? query.id : query.label;
    std::vector<const ProbeButton *> matches;
    for (const ProbeButton &b : this->last_.buttons) {
        bool name_ok = false;
        if (by_id) {
            name_ok = (b.id == query.id);
        } else {
            name_ok = query.contains ? (b.label.find(query.label) != std::string::npos) : (b.label == query.label);
        }
        if (name_ok && (query.kind.empty() || (b.kind == query.kind))) {
            matches.push_back(&b);
        }
    }
    if (matches.empty()) {
        error = std::string("no button matches '") + shown + "'";
        return false;
    }
    if (query.index >= 0) {
        if (static_cast<std::size_t>(query.index) >= matches.size()) {
            error = "index " + std::to_string(query.index) + " is out of range (" + std::to_string(matches.size())
                + " buttons match)";
            return false;
        }
        out = *matches[static_cast<std::size_t>(query.index)];
        return true;
    }
    if (matches.size() > 1) {
        error = std::to_string(matches.size()) + " buttons match '" + shown + "' (give index or kind)";
        return false;
    }
    out = *matches.front();
    return true;
}

} // namespace hd2d
