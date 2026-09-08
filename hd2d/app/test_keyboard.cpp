#include "app/test_keyboard.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
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
                && name != "alt" && name != "numlock" && name != "repeat" && name != "drop_file") {
                throw std::runtime_error("unknown field: " + name);
            }
        }
        if (request.contains("id") && !request["id"].is_string()) { throw std::runtime_error("id must be a string"); }
        if (request.contains("drop_file")) {
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
    std::fprintf(stderr, "[test-keyboard] enabled: %s\n", path.c_str());
    return true;
}

void TestKeyboard::poll(const char *phase)
{
    if (!enabled_) { return; }
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
        std::string id;
        const auto request = Json::parse(pending_, nullptr, false);
        if (request.is_object() && request.contains("id") && request["id"].is_string()) { id = request["id"]; }
        int queued = 0;
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
        const Json reply = { {"id", id}, {"line", line_}, {"phase", phase},
            {"status", ok ? "queued" : "error"}, {"events", queued}, {"error", error} };
        ack_ << reply.dump() << '\n';
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
