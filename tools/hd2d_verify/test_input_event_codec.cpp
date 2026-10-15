// CPU-only test of the `input_event` codec's `map_click` (AH-57) and of the shared key expander.
//
// Build and run (the bundled LLVM-MinGW; nothing else is linked):
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I presentation -I third_party/cpp/include ^
//     tools/hd2d_verify/test_input_event_codec.cpp presentation/frame/protocol_messages.cpp ^
//     presentation/bridge/input_event_adapter.cpp -o test_input_event_codec.exe
//   test_input_event_codec.exe
// (`-Wno-missing-field-initializers` only hides existing warnings in `game_frame.h`; none come from this file.)
//
// What it pins down:
//   - encode -> decode round trip for every left/right x modifier subset and the edge grids (0 and 255);
//   - the wire shape the window must send: {"e":"map_click","x","y","button":"left"|"right","mods":[...]}
//     with mods in the fixed order shift, ctrl, alt and omitted when none is held;
//   - every malformed map_click (missing / out-of-range / mistyped x or y, unknown button, unknown modifier,
//     mods that is not an array) is dropped **alone**: the events around it survive;
//   - the shared expander (`append_input_event_keys`, used by every core) turns a map_click into no key at all,
//     so a stray map_click sent to a core that did not declare `map_click` changes nothing.
#include "bridge/input_event_adapter.h"
#include "frame/protocol_messages.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_checks = 0;

void check(bool ok, const std::string &what)
{
    if (!ok) {
        std::cerr << "FAILED: " << what << std::endl;
        std::exit(1);
    }
    ++g_checks;
}

presentation::InputEventWire click(int x, int y, int button, bool shift, bool ctrl, bool alt)
{
    presentation::InputEventWire event;
    event.e = "map_click";
    event.grid_x = x;
    event.grid_y = y;
    event.button = button;
    event.shift = shift;
    event.ctrl = ctrl;
    event.alt = alt;
    return event;
}

std::vector<presentation::InputEventWire> decode(const std::string &text)
{
    presentation::InputEventsMessage message;
    std::string err;
    check(presentation::decode_input_events(text, message, err), "decode of a well-formed input_event: " + err);
    return message.events;
}

std::string wrap(const std::string &events_json)
{
    return "{\"t\":\"input_event\",\"events\":" + events_json + "}";
}

} // namespace

int main()
{
    using nlohmann::json;

    // ---- round trip: all button x modifier subsets, edge grids
    for (const int button : { 1, 2 }) {
        for (int mods = 0; mods < 8; ++mods) {
            for (const int x : { 0, 1, 66, 197, 255 }) {
                for (const int y : { 0, 1, 21, 65, 255 }) {
                    presentation::InputEventsMessage out;
                    out.events.push_back(click(x, y, button, (mods & 1) != 0, (mods & 2) != 0, (mods & 4) != 0));
                    const auto back = decode(presentation::encode_input_events(out));
                    check(back.size() == 1, "one event survives the round trip");
                    const auto &e = back.front();
                    check(e.e == "map_click" && e.grid_x == x && e.grid_y == y && e.button == button
                            && e.shift == ((mods & 1) != 0) && e.ctrl == ((mods & 2) != 0) && e.alt == ((mods & 4) != 0),
                        "round trip keeps x, y, button and the modifiers");
                }
            }
        }
    }

    // ---- the wire shape the window must send
    {
        presentation::InputEventsMessage out;
        out.events.push_back(click(12, 34, 2, true, true, true));
        out.events.push_back(click(0, 255, 1, false, false, false));
        const json wire = json::parse(presentation::encode_input_events(out));
        check(wire["t"] == "input_event" && wire["events"].size() == 2, "the message is an input_event with two events");
        const json &a = wire["events"][0];
        check(a["e"] == "map_click" && a["x"] == 12 && a["y"] == 34 && a["button"] == "right", "first event's fields");
        check(a["mods"] == json::array({ "shift", "ctrl", "alt" }), "mods are listed in the fixed order shift, ctrl, alt");
        const json &b = wire["events"][1];
        check(b["button"] == "left" && b["x"] == 0 && b["y"] == 255 && !b.contains("mods"), "no modifier -> no mods key");
    }

    // ---- malformed events are dropped one by one; the good one after them survives
    const std::vector<std::string> bad = {
        R"({"e":"map_click","y":3,"button":"left"})",                              // x missing
        R"({"e":"map_click","x":4,"button":"left"})",                              // y missing
        R"({"e":"map_click","x":256,"y":3,"button":"left"})",                      // x out of range
        R"({"e":"map_click","x":-1,"y":3,"button":"left"})",                       // x negative
        R"({"e":"map_click","x":4,"y":256,"button":"left"})",                      // y out of range
        R"({"e":"map_click","x":"4","y":3,"button":"left"})",                      // x is a string
        R"({"e":"map_click","x":4.5,"y":3,"button":"left"})",                      // x is not an integer
        R"({"e":"map_click","x":4,"y":null,"button":"left"})",                     // y is null
        R"({"e":"map_click","x":4,"y":3,"button":"middle"})",                      // unknown button
        R"({"e":"map_click","x":4,"y":3})",                                        // no button
        R"({"e":"map_click","x":4,"y":3,"button":1})",                             // button is a number
        R"({"e":"map_click","x":4,"y":3,"button":"left","mods":["meta"]})",        // unknown modifier
        R"({"e":"map_click","x":4,"y":3,"button":"left","mods":["shift","meta"]})", // one good, one unknown
        R"({"e":"map_click","x":4,"y":3,"button":"left","mods":"shift"})",         // mods is not an array
        R"({"e":"map_click","x":4,"y":3,"button":"left","mods":[7]})",             // a mod that is not a string
        R"({"e":"map_teleport","x":4,"y":3})",                                     // an event nobody knows
        R"({"e":5})",                                                              // not even a name
        R"(7)",                                                                    // not an object
    };
    std::string list = "[";
    for (const auto &one : bad) {
        list += one + ",";
    }
    list += R"({"e":"map_click","x":9,"y":8,"button":"right","mods":["alt"]},{"e":"cancel"}])";
    const auto survivors = decode(wrap(list));
    check(survivors.size() == 2, "only the well-formed events survive: got " + std::to_string(survivors.size()));
    check(survivors[0].e == "map_click" && survivors[0].grid_x == 9 && survivors[0].grid_y == 8 && survivors[0].button == 2
            && survivors[0].alt && !survivors[0].shift && !survivors[0].ctrl,
        "the good map_click is intact");
    check(survivors[1].e == "cancel", "the event after the good one is intact");

    // an unknown key of a good event is ignored (v1 section 2.3), and so is a missing `events`
    check(decode(wrap(R"([{"e":"map_click","x":1,"y":2,"button":"left","later":true}])")).size() == 1, "unknown keys are ignored");
    check(decode(R"({"t":"input_event"})").empty(), "no events key -> no events");

    // ---- the shared key expander ignores map_click: a stray one cannot press anything in another core
    {
        presentation::InputEventsMessage message;
        message.events.push_back(click(5, 5, 1, false, false, false));
        message.events.push_back(click(6, 6, 2, true, true, true));
        std::vector<int> keys;
        presentation::append_input_event_keys(message, keys);
        check(keys.empty(), "append_input_event_keys must not turn a map_click into keys");
        presentation::InputEventWire step;
        step.e = "move";
        step.dx = 1;
        message.events.push_back(step);
        keys.clear();
        presentation::append_input_event_keys(message, keys);
        check(keys == std::vector<int>({ '6' }), "the events around a map_click still expand as before");
    }

    std::cout << "input_event codec (map_click): " << g_checks << " checks passed" << std::endl;
    return 0;
}
