// CPU-only test of the map click recognizer (AH-58): `hd2d/ui/map_click.{h,cpp}`.
//
// Build and run (the bundled LLVM-MinGW; SDL is not needed):
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I presentation -I hd2d -I third_party/cpp/include ^
//     tools/hd2d_verify/test_map_click_gesture.cpp hd2d/ui/map_click.cpp presentation/frame/protocol_messages.cpp ^
//     -o test_map_click_gesture.exe
//   test_map_click_gesture.exe
//
// What it pins down:
//   - mouse: a press and release without moving is one click of the pressed button; moving past the slop is a drag
//     (no click); a second button pressed while the first is held is ignored;
//   - touch: a short tap is a left click, holding still for the long-press time is one right click (sent by poll()
//     at that moment, and the release afterwards sends nothing), moving past the slop cancels both;
//   - a second finger cancels the first and no click comes from either; a finger that was not accepted (outside
//     the map) still counts as a finger; fingers whose release was lost stop counting after a while;
//   - the reply gate holds until the next frame or the timeout;
//   - `map_click_frame_block_reason()` / `map_click_cell_valid()` cover every state the contract lists;
//   - `push_map_click()` produces the wire event AH-57 decodes.
#include "frame/game_frame.h"
#include "frame/protocol_messages.h"
#include "ui/map_click.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

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

using hd2d::MapClick;
using hd2d::MapClickButton;
using hd2d::MapClickGate;
using hd2d::MapClickGesture;
using Source = hd2d::MapClickGesture::Source;

MapClick target(int gx, int gy, MapClickButton button = MapClickButton::Left, bool shift = false, bool ctrl = false,
    bool alt = false)
{
    MapClick click;
    click.button = button;
    click.gx = gx;
    click.gy = gy;
    click.mods.shift = shift;
    click.mods.ctrl = ctrl;
    click.mods.alt = alt;
    return click;
}

void mouse_tests()
{
    MapClick out;
    {
        MapClickGesture g;
        check(g.press(Source::Mouse, 1, 100, 100, target(7, 8), 1000), "mouse press starts a candidate");
        check(g.release(Source::Mouse, 1, 100, 100, 1050, out), "mouse release gives a click");
        check(out.button == MapClickButton::Left && out.gx == 7 && out.gy == 8, "left click at the pressed cell");
        check(!g.active(), "gesture is finished after release");
    }
    {
        MapClickGesture g;
        check(g.press(Source::Mouse, 2, 10, 10, target(3, 4, MapClickButton::Right, true, false, true), 0), "right press");
        check(g.release(Source::Mouse, 2, 12, 9, 40, out), "right release within the slop");
        check(out.button == MapClickButton::Right && out.mods.shift && !out.mods.ctrl && out.mods.alt,
            "right click keeps the modifiers of the press");
    }
    {
        // A mouse click has no long-press meaning: a long hold is still the pressed button.
        MapClickGesture g;
        (void)g.press(Source::Mouse, 1, 5, 5, target(1, 1), 0);
        MapClick polled;
        check(!g.poll(5000, polled), "poll never fires for the mouse");
        check(g.release(Source::Mouse, 1, 5, 5, 5000, out) && out.button == MapClickButton::Left, "long mouse hold is a left click");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Mouse, 1, 100, 100, target(5, 5), 0);
        g.move(Source::Mouse, 1, 100 + MapClickGesture::kSlopPx + 1, 100);
        check(!g.release(Source::Mouse, 1, 100, 100, 50, out), "moving past the slop and coming back is a drag");
    }
    {
        // The screen point of the press travels with the click (the context menu is placed near it).
        MapClickGesture g;
        MapClick with_point = target(5, 5, MapClickButton::Right);
        with_point.sx = 321;
        with_point.sy = 123;
        (void)g.press(Source::Mouse, 2, 321, 123, with_point, 0);
        check(g.release(Source::Mouse, 2, 321, 123, 30, out) && out.sx == 321 && out.sy == 123, "the pressed screen point is carried");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Mouse, 1, 100, 100, target(5, 5), 0);
        g.move(Source::Mouse, 1, 100 + MapClickGesture::kSlopPx, 100);
        check(g.release(Source::Mouse, 1, 100 + MapClickGesture::kSlopPx, 100, 50, out), "moving exactly the slop is still a click");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Mouse, 1, 100, 100, target(5, 5), 0);
        check(!g.release(Source::Mouse, 1, 100 + 40, 100, 50, out), "released far away without motion events is a drag");
    }
    {
        MapClickGesture g;
        check(g.press(Source::Mouse, 1, 1, 1, target(2, 2), 0), "left press");
        check(!g.press(Source::Mouse, 2, 1, 1, target(9, 9, MapClickButton::Right), 10), "a second button is ignored");
        check(!g.release(Source::Mouse, 2, 1, 1, 20, out), "the ignored button gives no click");
        check(g.release(Source::Mouse, 1, 1, 1, 30, out) && out.gx == 2, "the first button still gives its click");
    }
    {
        MapClickGesture g;
        check(!g.press(Source::Mouse, 1, 1, 1, target(2, 2), 0, false), "a press that is not accepted starts nothing");
        check(!g.release(Source::Mouse, 1, 1, 1, 30, out), "so its release sends nothing");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Mouse, 1, 1, 1, target(2, 2), 0);
        g.reset();
        check(!g.release(Source::Mouse, 1, 1, 1, 30, out), "reset forgets the press");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Mouse, 1, 1, 1, target(2, 2), 0);
        g.abandon();
        check(!g.release(Source::Mouse, 1, 1, 1, 30, out), "abandon drops the press");
    }
}

void touch_tests()
{
    MapClick out;
    {
        MapClickGesture g;
        check(g.press(Source::Touch, 11, 200, 300, target(4, 6), 0), "tap begins");
        check(g.release(Source::Touch, 11, 200, 300, 120, out), "short tap gives a click");
        check(out.button == MapClickButton::Left && out.gx == 4 && out.gy == 6, "short tap is a left click");
        check(g.fingers() == 0, "the finger is gone after release");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Touch, 11, 200, 300, target(4, 6), 0);
        check(g.release(Source::Touch, 11, 200, 300, MapClickGesture::kLongPressMs - 1, out) && out.button == MapClickButton::Left,
            "just under the long-press time is still a left click");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Touch, 11, 200, 300, target(4, 6), 0);
        check(!g.poll(MapClickGesture::kLongPressMs - 1, out), "poll does not fire before the long-press time");
        check(g.poll(MapClickGesture::kLongPressMs, out), "poll fires at the long-press time");
        check(out.button == MapClickButton::Right && out.gx == 4 && out.gy == 6, "long press is a right click at the pressed cell");
        check(!g.poll(MapClickGesture::kLongPressMs + 500, out), "poll fires only once");
        check(!g.release(Source::Touch, 11, 200, 300, MapClickGesture::kLongPressMs + 800, out),
            "releasing after the long press sends nothing more");
    }
    {
        // After a long press the finger belongs to the context menu: its release may never reach us, and the
        // next tap must still work.
        MapClickGesture g;
        (void)g.press(Source::Touch, 1, 200, 300, target(4, 6), 0);
        check(g.poll(MapClickGesture::kLongPressMs, out), "long press fires");
        check(g.fingers() == 0 && !g.active(), "the long-press finger is no longer counted");
        check(g.press(Source::Touch, 2, 10, 10, target(1, 1), 1000) && g.release(Source::Touch, 2, 10, 10, 1050, out) && out.gx == 1,
            "the next tap works although the first finger's release never came");
    }
    {
        // The frame ran slow: no poll came before the release, but the hold was long.
        MapClickGesture g;
        (void)g.press(Source::Touch, 11, 200, 300, target(4, 6), 0);
        check(g.release(Source::Touch, 11, 200, 300, 900, out) && out.button == MapClickButton::Right,
            "a long hold released before poll() is still a right click");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Touch, 11, 200, 300, target(4, 6), 0);
        g.move(Source::Touch, 11, 200, 300 + MapClickGesture::kSlopPx + 1);
        check(!g.poll(MapClickGesture::kLongPressMs + 10, out), "a drag never becomes a long press");
        check(!g.release(Source::Touch, 11, 200, 400, 900, out), "a drag gives no click");
    }
    {
        // Two fingers: nothing comes out of either.
        MapClickGesture g;
        check(g.press(Source::Touch, 1, 10, 10, target(1, 1), 0), "first finger");
        check(!g.press(Source::Touch, 2, 500, 10, target(9, 9), 30), "second finger is not a candidate");
        check(!g.active(), "and it drops the first one too");
        check(g.fingers() == 2, "both fingers are counted");
        check(!g.poll(2000, out), "no long press from a dropped gesture");
        check(!g.release(Source::Touch, 1, 10, 10, 2100, out), "first finger up: nothing");
        check(!g.release(Source::Touch, 2, 500, 10, 2200, out), "second finger up: nothing");
        check(g.fingers() == 0, "all fingers are gone");
        check(g.press(Source::Touch, 3, 50, 50, target(2, 2), 3000) && g.release(Source::Touch, 3, 50, 50, 3050, out),
            "a fresh tap works after the two-finger touch");
    }
    {
        // A finger that was not accepted (for example outside the map) still counts as a finger.
        MapClickGesture g;
        (void)g.press(Source::Touch, 1, 10, 10, target(1, 1), 0, false);
        check(!g.press(Source::Touch, 2, 50, 50, target(2, 2), 10), "a tap next to a rejected finger is not a click");
        check(g.fingers() == 2, "the rejected finger counted");
    }
    {
        // A finger whose release was lost stops counting after a while, and the same id re-pressed replaces itself.
        MapClickGesture g;
        (void)g.press(Source::Touch, 1, 10, 10, target(1, 1), 0);
        check(g.press(Source::Touch, 1, 20, 20, target(3, 3), 100), "the same finger id pressed again is a new press");
        check(g.fingers() == 1, "the same id is one finger");
        check(g.release(Source::Touch, 1, 20, 20, 150, out) && out.gx == 3, "and it clicks at the new place");
        (void)g.press(Source::Touch, 5, 10, 10, target(1, 1), 0);
        check(g.press(Source::Touch, 6, 70, 70, target(4, 4), MapClickGesture::kFingerStaleMs + 1000), "a stale finger no longer counts");
        check(g.fingers() == 1, "only the new finger is counted");
    }
    {
        MapClickGesture g;
        (void)g.press(Source::Touch, 1, 10, 10, target(1, 1), 0);
        g.reset();
        check(g.fingers() == 0 && !g.active(), "reset forgets fingers and the candidate");
        check(g.press(Source::Touch, 2, 10, 10, target(1, 1), 10), "and the next tap works");
    }
    {
        // A mouse press is not turned into a touch by another source's finger.
        MapClickGesture g;
        (void)g.press(Source::Mouse, 1, 10, 10, target(1, 1), 0);
        check(!g.press(Source::Touch, 9, 10, 10, target(2, 2), 10), "a finger during a mouse press does not start a second candidate");
        check(g.release(Source::Mouse, 1, 10, 10, 50, out) && out.gx == 1, "the mouse click is unaffected");
    }
}

void gate_tests()
{
    MapClickGate gate;
    check(gate.ready(0, 0), "a fresh gate is open");
    gate.sent(10, 1000);
    check(!gate.ready(10, 1050), "closed while the frame has not changed");
    check(gate.ready(11, 1050), "open once a newer frame arrived");
    check(!gate.ready(10, 1000 + MapClickGate::kReplyTimeoutMs - 1), "still closed just before the timeout");
    check(gate.ready(10, 1000 + MapClickGate::kReplyTimeoutMs), "open at the timeout (a click that changes nothing sends no frame)");
    gate.reset();
    check(gate.ready(10, 1001), "reset opens it");
}

GameFrame ready_frame()
{
    GameFrame frame;
    frame.cells.emplace_back();
    frame.awaiting_command = true;
    return frame;
}

void frame_tests()
{
    using hd2d::map_click_frame_block_reason;
    {
        GameFrame frame;
        check(map_click_frame_block_reason(frame) != nullptr, "a frame without cells has no map");
    }
    check(map_click_frame_block_reason(ready_frame()) == nullptr, "a plain map waiting for a command is ready");
    {
        GameFrame frame = ready_frame();
        frame.awaiting_command = false;
        check(map_click_frame_block_reason(frame) == nullptr, "a walk in progress is ready too (the click stops it)");
    }
    {
        GameFrame frame = ready_frame();
        frame.menu_open = true;
        check(map_click_frame_block_reason(frame) != nullptr, "menu open");
    }
    {
        GameFrame frame = ready_frame();
        frame.menu_term_lines.emplace_back();
        check(map_click_frame_block_reason(frame) != nullptr, "a terminal mirror is showing");
    }
    {
        GameFrame frame = ready_frame();
        frame.targeting.active = true;
        check(map_click_frame_block_reason(frame) != nullptr, "targeting");
    }
    {
        GameFrame frame = ready_frame();
        frame.prompt.text_utf8 = "-more-";
        check(map_click_frame_block_reason(frame) != nullptr, "-more- prompt");
    }
    {
        GameFrame frame = ready_frame();
        frame.prompt.choices.emplace_back();
        check(map_click_frame_block_reason(frame) != nullptr, "prompt with choices");
    }
    {
        GameFrame frame = ready_frame();
        frame.numeric.active = true;
        check(map_click_frame_block_reason(frame) != nullptr, "number input");
    }
    {
        GameFrame frame = ready_frame();
        frame.text_input_active = true;
        check(map_click_frame_block_reason(frame) != nullptr, "text input");
    }
    {
        GameFrame frame = ready_frame();
        frame.title_screen = true;
        check(map_click_frame_block_reason(frame) != nullptr, "title screen");
        frame.title_screen = false;
        frame.pre_game_menu = true;
        check(map_click_frame_block_reason(frame) != nullptr, "birth / pre-game menu");
    }

    using hd2d::map_click_cell_valid;
    GameFrame frame = ready_frame();
    check(map_click_cell_valid(frame, 0, 0) && map_click_cell_valid(frame, 255, 255), "the wire range is 0..255");
    check(!map_click_cell_valid(frame, -1, 0) && !map_click_cell_valid(frame, 0, -1), "negative cells are not sent");
    check(!map_click_cell_valid(frame, 256, 0) && !map_click_cell_valid(frame, 0, 256), "cells past 255 are not sent");
    frame.minimap.width = 150;
    frame.minimap.height = 44;
    frame.minimap.kinds.assign(150 * 44, 0);
    check(map_click_cell_valid(frame, 149, 43), "the last cell of the floor is sent");
    check(!map_click_cell_valid(frame, 150, 10) && !map_click_cell_valid(frame, 10, 44), "cells outside the floor are not sent");
}

void wire_tests()
{
    presentation::InputEventsMessage message;
    hd2d::push_map_click(message, target(12, 34, MapClickButton::Right, true, true, false));
    hd2d::push_map_click(message, target(0, 255));
    check(message.events.size() == 2, "two events pushed");
    const auto json = nlohmann::json::parse(presentation::encode_input_events(message));
    check(json["t"] == "input_event", "wire type");
    const auto &first = json["events"][0];
    check(first["e"] == "map_click" && first["x"] == 12 && first["y"] == 34 && first["button"] == "right", "first event");
    check(first["mods"].size() == 2 && first["mods"][0] == "shift" && first["mods"][1] == "ctrl", "mods in shift, ctrl, alt order");
    const auto &second = json["events"][1];
    check(second["button"] == "left" && !second.contains("mods"), "second event has no mods");

    presentation::InputEventsMessage decoded;
    std::string err;
    check(presentation::decode_input_events(presentation::encode_input_events(message), decoded, err), "decodes: " + err);
    check(decoded.events.size() == 2 && decoded.events[0].grid_x == 12 && decoded.events[0].button == 2
            && decoded.events[0].shift && decoded.events[0].ctrl && !decoded.events[0].alt,
        "round trip");
}

} // namespace

int main()
{
    mouse_tests();
    touch_tests();
    gate_tests();
    frame_tests();
    wire_tests();
    std::cout << "PASS " << g_checks << " map click checks" << std::endl;
    return 0;
}
