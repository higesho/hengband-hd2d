#include "app/test_keyboard.h"
#include "app/ui_probe.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace hd2d {
namespace {
using Json = nlohmann::json;

SDL_Keycode keycode(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "enter") { name = "return"; }
    if (name == "esc") { name = "escape"; }
    if (name == "space") { return SDLK_SPACE; }
    if (name == "kp_enter") { return SDLK_KP_ENTER; }
    if (name.size() == 4 && name.substr(0, 3) == "kp_" && name[3] >= '0' && name[3] <= '9') {
        static const SDL_Keycode digits[] = { SDLK_KP_0, SDLK_KP_1, SDLK_KP_2, SDLK_KP_3, SDLK_KP_4,
            SDLK_KP_5, SDLK_KP_6, SDLK_KP_7, SDLK_KP_8, SDLK_KP_9 };
        return digits[name[3] - '0'];
    }
    return SDL_GetKeyFromName(name.c_str());
}

void append_text(const std::string &text, std::vector<SDL_Event> &events)
{
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7f) { throw std::runtime_error("text must not contain control characters; use key instead"); }
    }
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = std::min(start + SDL_TEXTINPUTEVENT_TEXT_SIZE - 1, text.size());
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) { --end; }
        if (end == start) { throw std::runtime_error("invalid UTF-8 text"); }
        SDL_Event event{};
        event.type = SDL_TEXTINPUT;
        std::memcpy(event.text.text, text.data() + start, end - start);
        events.push_back(event);
        start = end;
    }
}
} // namespace

bool compile_test_key(const std::string &line, std::vector<SDL_Event> &events, std::string &error)
{
    events.clear();
    error.clear();
    try {
        const auto request = Json::parse(line);
        if (!request.is_object()) { throw std::runtime_error("expected a JSON object"); }
        for (auto it = request.begin(); it != request.end(); ++it) {
            const auto &name = it.key();
            if (name != "id" && name != "key" && name != "text" && name != "ctrl" && name != "shift"
                && name != "alt" && name != "numlock" && name != "repeat" && name != "drop_file" && name != "probe"
                && name != "mouse_button" && name != "x" && name != "y" && name != "touch" && name != "pad_button" && name != "pressed"
                && name != "phase" && name != "finger" && name != "query" && name != "tap" && name != "wheel" && name != "quit" && name != "map" && name != "detail") {
                throw std::runtime_error("unknown field: " + name);
            }
        }
        if (!request.contains("mouse_button") && !request.contains("touch") && !request.contains("query") && (request.contains("x") || request.contains("y")))
            throw std::runtime_error("coordinates require mouse_button or touch");
        if(!request.contains("pad_button")&&request.contains("pressed"))throw std::runtime_error("pressed requires pad_button");
        if (request.contains("id") && !request["id"].is_string()) { throw std::runtime_error("id must be a string"); }
        if (request.contains("probe")) {
            if (!request.at("probe").is_boolean() || !request.at("probe").get<bool>()
                || request.size() != (request.contains("id") ? 2u : 1u))
                throw std::runtime_error("probe must be true and cannot be combined with input");
            return true; // Observe the current phase without injecting a keyboard event.
        }
        if (request.contains("quit")) {
            if (request.size() != (request.contains("id") ? 2u : 1u) || !request.at("quit").is_boolean() || !request.at("quit").get<bool>())
                throw std::runtime_error("quit must be true and cannot be combined with input");
            SDL_Event event{};
            event.type = SDL_QUIT; // 窓を閉じる（遊ぶ人が閉じるボタンを押したのと同じ出来事）
            events.push_back(event);
            return true;
        }
        if (request.contains("wheel")) {
            if (request.size() != (request.contains("id") ? 2u : 1u) || !request.at("wheel").is_object())
                throw std::runtime_error("wheel must be an object and cannot be combined with input");
            const auto &wheel = request.at("wheel");
            for (auto it = wheel.begin(); it != wheel.end(); ++it) {
                if (it.key() != "x" && it.key() != "y" && it.key() != "at") throw std::runtime_error("unknown wheel field: " + it.key());
            }
            const long long wx = wheel.value("x", 0LL), wy = wheel.value("y", 0LL);
            if ((wx == 0 && wy == 0) || wx < -100 || wx > 100 || wy < -100 || wy > 100)
                throw std::runtime_error("wheel needs a non-zero integer x or y within +-100");
            if (wheel.contains("x") && !wheel.at("x").is_number_integer()) throw std::runtime_error("wheel x must be an integer");
            if (wheel.contains("y") && !wheel.at("y").is_number_integer()) throw std::runtime_error("wheel y must be an integer");
            if (wheel.contains("at")) {
                const auto &at = wheel.at("at");
                if (!at.is_array() || at.size() != 2 || !at[0].is_number_integer() || !at[1].is_number_integer()
                    || at[0].get<long long>() < 0 || at[0].get<long long>() > 32767 || at[1].get<long long>() < 0 || at[1].get<long long>() > 32767)
                    throw std::runtime_error("wheel at must be [x, y] (integers, 0..32767)");
            }
            SDL_Event event{};
            event.type = SDL_MOUSEWHEEL;
            event.wheel.x = static_cast<Sint32>(wx);
            event.wheel.y = static_cast<Sint32>(wy);
            event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            events.push_back(event);
            return true;
        }
        if (request.contains("query")) {
            const unsigned base = request.contains("id") ? 2u : 1u;
            const std::string what = request.at("query").is_string() ? request.at("query").get<std::string>() : std::string();
            if (what == "ui") {
                // 任意の `map`：地形・実体の要約の範囲（none・near〔既定〕・full）。任意の `detail`：マスごとの詳細（地形番号・絵・色・光）。
                if (request.size() != base + (request.contains("map") ? 1u : 0u) + (request.contains("detail") ? 1u : 0u)
                    || request.contains("x") || request.contains("y"))
                    throw std::runtime_error("query ui accepts only an optional map and detail and cannot be combined with input");
                if (request.contains("detail") && !request.at("detail").is_boolean())
                    throw std::runtime_error("query detail must be a boolean");
                if (request.contains("map")) {
                    const bool ok_map = request.at("map").is_string()
                        && (request.at("map") == "none" || request.at("map") == "near" || request.at("map") == "full");
                    if (!ok_map) throw std::runtime_error("query map must be none, near or full");
                }
            } else if (what == "cell") {
                // マス (x, y) の画面座標を返す。
                if (request.size() != base + 2u || !request.contains("x") || !request.contains("y")
                    || !request.at("x").is_number_integer() || !request.at("y").is_number_integer())
                    throw std::runtime_error("query cell requires integer x and y");
                const auto cx = request.at("x").get<long long>(), cy = request.at("y").get<long long>();
                if (cx < -100000 || cx > 100000 || cy < -100000 || cy > 100000) throw std::runtime_error("query cell coordinates out of range");
            } else {
                throw std::runtime_error("query must be \"ui\" or \"cell\" and cannot be combined with input");
            }
            return true; // 画面が直前に出したものを返す。イベントは入れない。
        }
        if (request.contains("tap")) {
            if (request.size() != (request.contains("id") ? 2u : 1u) || !request.at("tap").is_object())
                throw std::runtime_error("tap must be an object and cannot be combined with input");
            const auto &tap = request.at("tap");
            for (auto it = tap.begin(); it != tap.end(); ++it) {
                const auto &key = it.key();
                if (key != "label" && key != "id" && key != "contains" && key != "index" && key != "kind" && key != "via")
                    throw std::runtime_error("unknown tap field: " + key);
            }
            const bool has_label = tap.contains("label") && tap.at("label").is_string() && !tap.at("label").get<std::string>().empty();
            const bool has_id = tap.contains("id") && tap.at("id").is_string() && !tap.at("id").get<std::string>().empty();
            if (has_label == has_id) throw std::runtime_error("tap requires exactly one of a non-empty label or id");
            if (tap.contains("contains") && !tap.at("contains").is_boolean())
                throw std::runtime_error("tap contains must be a boolean");
            if (tap.contains("index") && (!tap.at("index").is_number_integer() || tap.at("index").get<long long>() < 0
                    || tap.at("index").get<long long>() > 100000))
                throw std::runtime_error("tap index must be a non-negative integer");
            if (tap.contains("kind") && !tap.at("kind").is_string()) throw std::runtime_error("tap kind must be a string");
            if (tap.contains("via")) {
                const bool ok_via = tap.at("via").is_string()
                    && (tap.at("via") == "click" || tap.at("via") == "touch" || tap.at("via") == "auto");
                if (!ok_via) throw std::runtime_error("tap via must be click, touch or auto");
            }
            return true; // 部品の探索は、描いたフレームを持つ `poll()` 側で行う。
        }
        if(request.contains("pad_button")) {
            if(request.size()!=(request.contains("id")?3u:2u)||!request.contains("pressed")||!request["pressed"].is_boolean())
                throw std::runtime_error("pad_button requires pressed and cannot be combined with other input");
            const auto button=SDL_GameControllerGetButtonFromString(request.at("pad_button").get<std::string>().c_str());
            if(button==SDL_CONTROLLER_BUTTON_INVALID)throw std::runtime_error("unknown controller button");
            SDL_Event event{};const bool down=request["pressed"].get<bool>();
            event.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;event.cbutton.button=static_cast<Uint8>(button);
            event.cbutton.state=down?SDL_PRESSED:SDL_RELEASED;events.push_back(event);
        } else if(request.contains("touch")) {
            /* Optional `phase` ("down" | "motion" | "up") sends that one finger event, so a test can hold a finger
             * down for as long as it likes (a long press) or drag it; without it the finger goes down and up at
             * once (a tap).  Optional `finger` (an integer) picks the finger number for multi-touch. */
            const unsigned optional_fields=(request.contains("id")?1u:0u)+(request.contains("phase")?1u:0u)+(request.contains("finger")?1u:0u);
            if(request.size()!=3u+optional_fields||!request["touch"].is_boolean()||!request["touch"].get<bool>()
                ||!request.contains("x")||!request.contains("y")||!request["x"].is_number()||!request["y"].is_number())
                throw std::runtime_error("touch requires normalized x/y and cannot be combined with other input");
            const double x=request["x"].get<double>(),y=request["y"].get<double>();
            if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)throw std::runtime_error("touch coordinates out of range");
            std::string phase="tap";
            if(request.contains("phase")) {
                if(!request["phase"].is_string())throw std::runtime_error("touch phase must be a string");
                phase=request["phase"].get<std::string>();
                if(phase!="down"&&phase!="motion"&&phase!="up")throw std::runtime_error("touch phase must be down, motion or up");
            }
            long long finger=1;
            if(request.contains("finger")) {
                if(!request["finger"].is_number_integer())throw std::runtime_error("touch finger must be an integer");
                finger=request["finger"].get<long long>();
                if(finger<0||finger>1000000)throw std::runtime_error("touch finger out of range");
            }
            SDL_Event event{};event.type=SDL_FINGERDOWN;event.tfinger.touchId=1;event.tfinger.fingerId=finger;
            event.tfinger.x=static_cast<float>(x);event.tfinger.y=static_cast<float>(y);event.tfinger.pressure=1;
            if(phase=="tap"||phase=="down")events.push_back(event);
            if(phase=="motion"){event.type=SDL_FINGERMOTION;events.push_back(event);}
            if(phase=="tap"||phase=="up"){event.type=SDL_FINGERUP;event.tfinger.pressure=0;events.push_back(event);}
        } else if (request.contains("mouse_button")) {
            if (request.size() != (request.contains("id") ? 4u : 3u)
                || !request.contains("x") || !request.contains("y")
                || !request["x"].is_number_integer() || !request["y"].is_number_integer())
                throw std::runtime_error("mouse_button requires integer x/y and cannot be combined with other input");
            const auto button = request.at("mouse_button").get<std::string>();
            const auto x = request["x"].get<long long>(), y = request["y"].get<long long>();
            if (x < 0 || x > 32767 || y < 0 || y > 32767) throw std::runtime_error("mouse coordinates out of range");
            const Uint8 code = button == "left" ? SDL_BUTTON_LEFT : button == "right" ? SDL_BUTTON_RIGHT : button == "middle" ? SDL_BUTTON_MIDDLE : 0;
            if (!code) throw std::runtime_error("unknown mouse button");
            SDL_Event down{}; down.type = SDL_MOUSEBUTTONDOWN;
            down.button.button = code; down.button.state = SDL_PRESSED; down.button.clicks = 1;
            down.button.x = static_cast<int>(x); down.button.y = static_cast<int>(y);
            events.push_back(down);
            SDL_Event up = down; up.type = SDL_MOUSEBUTTONUP; up.button.state = SDL_RELEASED;
            events.push_back(up);
        } else if (request.contains("drop_file")) {
            if (request.size() > (request.contains("id") ? 2u : 1u)) throw std::runtime_error("drop_file cannot be combined with keys");
            const auto path = request.at("drop_file").get<std::string>();
            if (path.empty() || path.find('\0') != std::string::npos) throw std::runtime_error("invalid drop_file path");
            SDL_Event event{}; event.type = SDL_DROPFILE; event.drop.file = SDL_strdup(path.c_str());
            if (!event.drop.file) throw std::runtime_error("cannot allocate drop event");
            events.push_back(event);
        } else if (!request.contains("key")) {
            if (!request.contains("text") || request.size() > (request.contains("id") ? 2u : 1u)) {
                throw std::runtime_error("expected text or key");
            }
            append_text(request.at("text").get<std::string>(), events);
        } else {
            const auto name = request.at("key").get<std::string>();
            const auto sym = keycode(name);
            if (sym == SDLK_UNKNOWN) { throw std::runtime_error("unknown key: " + name); }
            const bool ctrl = request.value("ctrl", false), shift = request.value("shift", false);
            const bool alt = request.value("alt", false), numlock = request.value("numlock", false);
            const bool repeat = request.value("repeat", false);
            const Uint16 mods = (ctrl ? KMOD_LCTRL : 0) | (shift ? KMOD_LSHIFT : 0)
                | (alt ? KMOD_LALT : 0) | (numlock ? KMOD_NUM : 0);
            SDL_Event down{};
            down.type = SDL_KEYDOWN;
            down.key.state = SDL_PRESSED;
            down.key.repeat = repeat ? 1 : 0;
            down.key.keysym.sym = sym;
            down.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
            down.key.keysym.mod = mods;
            events.push_back(down);
            std::string text;
            if (request.contains("text")) {
                text = request["text"].get<std::string>();
            } else if (!ctrl && !alt) {
                if (sym >= 0x20 && sym <= 0x7e) {
                    char c = static_cast<char>(sym);
                    if (shift && c >= 'a' && c <= 'z') { c -= 'a' - 'A'; }
                    else if (shift && c != ' ') {
                        throw std::runtime_error("shifted punctuation requires explicit text");
                    }
                    text.assign(1, c);
                } else if (numlock && sym >= SDLK_KP_1 && sym <= SDLK_KP_9) {
                    text.assign(1, static_cast<char>('1' + sym - SDLK_KP_1));
                } else if (numlock && sym == SDLK_KP_0) { text = "0"; }
            }
            if ((ctrl || alt) && !text.empty()) { throw std::runtime_error("Ctrl/Alt key must not generate text"); }
            append_text(text, events);
            SDL_Event up = down;
            up.type = SDL_KEYUP;
            up.key.state = SDL_RELEASED;
            up.key.repeat = 0;
            events.push_back(up);
        }
        if (events.empty() || events.size() > 128) { throw std::runtime_error("empty or oversized event sequence"); }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        events.clear();
        return false;
    }
}

bool compile_test_tap(const std::string &line, const UiProbe &probe, std::vector<SDL_Event> &events, std::string &error)
{
    events.clear();
    error.clear();
    try {
        const auto request = Json::parse(line);
        if (!request.is_object() || !request.contains("tap") || !request.at("tap").is_object()) {
            throw std::runtime_error("expected a tap object");
        }
        const auto &tap = request.at("tap");
        UiProbe::TapQuery query;
        query.label = tap.value("label", std::string());
        query.id = tap.value("id", std::string());
        query.contains = tap.value("contains", false);
        query.index = tap.contains("index") ? static_cast<int>(tap.at("index").get<long long>()) : -1;
        query.kind = tap.value("kind", std::string());
        ProbeButton button;
        if (!probe.find_button(query, button, error)) { return false; }
        const int cx = button.rect.x + (button.rect.w / 2);
        const int cy = button.rect.y + (button.rect.h / 2);
        const int width = probe.screen_w();
        const int height = probe.screen_h();
        if (cx < 0 || cy < 0 || (width > 0 && cx >= width) || (height > 0 && cy >= height)) {
            throw std::runtime_error("the center of '" + button.label + "' is outside the window");
        }
        std::string via = tap.value("via", std::string("auto"));
        if (via == "auto") { via = button.touch_only ? "touch" : "click"; }
        if (via == "touch") {
            if (width <= 0 || height <= 0) { throw std::runtime_error("screen size is unknown; cannot send a touch"); }
            SDL_Event down{};
            down.type = SDL_FINGERDOWN;
            down.tfinger.touchId = 1;
            down.tfinger.fingerId = 1;
            down.tfinger.x = static_cast<float>(cx) / static_cast<float>(width);
            down.tfinger.y = static_cast<float>(cy) / static_cast<float>(height);
            down.tfinger.pressure = 1;
            events.push_back(down);
            SDL_Event up = down;
            up.type = SDL_FINGERUP;
            up.tfinger.pressure = 0;
            events.push_back(up);
        } else {
            SDL_Event down{};
            down.type = SDL_MOUSEBUTTONDOWN;
            down.button.button = SDL_BUTTON_LEFT;
            down.button.state = SDL_PRESSED;
            down.button.clicks = 1;
            down.button.x = cx;
            down.button.y = cy;
            events.push_back(down);
            SDL_Event up = down;
            up.type = SDL_MOUSEBUTTONUP;
            up.button.state = SDL_RELEASED;
            events.push_back(up);
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        events.clear();
        return false;
    }
}

bool TestKeyboard::configure(const std::string &path, std::string &error)
{
    if (path == path_) { return true; }
    input_.close();
    ack_.close();
    enabled_ = false;
    path_.clear();
    pending_.clear();
    offset_ = line_ = 0;
    if (path.empty()) { return true; }
    input_.open(std::filesystem::u8path(path), std::ios::binary);
    ack_.open(std::filesystem::u8path(path + ".ack.jsonl"), std::ios::binary | std::ios::trunc);
    if (!input_ || !ack_) {
        error = "cannot open test input or acknowledgement file";
        input_.close();
        ack_.close();
        return false;
    }
    path_ = path;
    enabled_ = true;
    ui_probe().enable(true); //!< 検査用の起動でだけ、画面が出したものを記録する
    std::fprintf(stderr, "[test-keyboard] enabled: %s\n", path.c_str());
    return true;
}

void TestKeyboard::poll(const char *phase)
{
    if (!enabled_) { return; }
    ui_probe().commit(phase); //!< 直前に描いたフレームを確定する（問い合わせはこれを返す）
    std::error_code ec;
    const auto size = std::filesystem::file_size(std::filesystem::u8path(path_), ec);
    if (ec || size < offset_) {
        std::fprintf(stderr, "[test-keyboard] input removed or truncated; disabled\n");
        enabled_ = false;
        return;
    }
    input_.clear();
    char c;
    // 未完成の長い行でもフレームを占有しない。
    for (std::size_t n = 0; n < 4096 && input_.get(c); ++n) {
        ++offset_;
        if (c != '\n') {
            pending_ += c;
            if (pending_.size() > 4096) {
                std::fprintf(stderr, "[test-keyboard] line too long; disabled\n");
                enabled_ = false;
                return;
            }
            continue;
        }
        ++line_;
        if (!pending_.empty() && pending_.back() == '\r') { pending_.pop_back(); }
        std::vector<SDL_Event> events;
        std::string error;
        bool ok = compile_test_key(pending_, events, error);
        ui_probe().clear_wheel_at();
        std::string id;
        const auto request = Json::parse(pending_, nullptr, false);
        const bool is_query = ok && request.is_object() && request.contains("query");
        if (ok && request.is_object() && request.contains("wheel") && request.at("wheel").contains("at")) {
            const auto &at = request.at("wheel").at("at");
            ui_probe().set_wheel_at(static_cast<int>(at[0].get<long long>()), static_cast<int>(at[1].get<long long>()));
        }
        if (ok && request.is_object() && request.contains("tap")) {
            ok = compile_test_tap(pending_, ui_probe(), events, error);
        }
        if (request.is_object() && request.contains("id") && request["id"].is_string()) { id = request["id"]; }
        int queued = 0;
        if(ok&&request.contains("pad_button")&&virtual_controller_index_<0) {
#if SDL_VERSION_ATLEAST(2,0,14)
            virtual_controller_index_=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);
            if(virtual_controller_index_<0){ok=false;error=std::string("cannot attach test controller: ")+SDL_GetError();}
#else
            ok=false;error="this SDL version does not support a test controller";
#endif
        }
        if (ok) {
            for (auto &event : events) {
                event.common.timestamp = SDL_GetTicks();
                if (SDL_PushEvent(&event) != 1) {
                    ok = false;
                    error = "SDL event queue rejected input";
                    break;
                }
                ++queued;
            }
        }
        for (std::size_t i = static_cast<std::size_t>(queued); i < events.size(); ++i) {
            if (events[i].type == SDL_DROPFILE) SDL_free(events[i].drop.file);
        }
        Json reply = { {"id", id}, {"line", line_}, {"phase", phase},
            {"status", ok ? (is_query ? "ok" : "queued") : "error"}, {"events", queued}, {"error", error} };
        if (is_query && ok) {
            if (request.at("query") == "cell") {
                reply["cell"] = Json::parse(ui_probe().cell_json(static_cast<int>(request.at("x").get<long long>()),
                    static_cast<int>(request.at("y").get<long long>())));
            } else {
                reply["ui"] = Json::parse(ui_probe().snapshot_json(phase, request.value("map", std::string("near")),
                    request.value("detail", false)));
            }
        }
        // 画面の字に壊れた UTF-8 が混ざっていても落ちない（U+FFFD に置き換わる。化けの検査はそれを見る）。
        ack_ << reply.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
        ack_.flush();
        std::fprintf(stderr, "[test-keyboard] line=%llu phase=%s %s (%d events)%s%s\n",
            static_cast<unsigned long long>(line_), phase, ok ? "queued" : "error", queued,
            error.empty() ? "" : ": ", error.c_str());
        pending_.clear();
        return;
    }
}

TestKeyboard &test_keyboard()
{
    static TestKeyboard input;
    return input;
}
} // namespace hd2d
