/*!
 * @file map_click.cpp
 * @brief `map_click.h` の実装。
 */
#include "ui/map_click.h"

#include <utility>

namespace hd2d {

bool MapClickGesture::beyond_slop(int sx, int sy) const
{
    const long long dx = static_cast<long long>(sx) - this->start_x_;
    const long long dy = static_cast<long long>(sy) - this->start_y_;
    return ((dx * dx) + (dy * dy)) > (static_cast<long long>(kSlopPx) * kSlopPx);
}

int MapClickGesture::fingers() const
{
    int count = 0;
    for (const FingerSlot &slot : this->fingers_) {
        count += slot.used ? 1 : 0;
    }
    return count;
}

void MapClickGesture::add_finger(std::int64_t id, std::uint32_t now_ms)
{
    FingerSlot *free_slot = nullptr;
    for (FingerSlot &slot : this->fingers_) {
        if (slot.used && ((now_ms - slot.since_ms) > kFingerStaleMs)) {
            slot = FingerSlot{}; //!< 離す出来事を取りこぼした古い指
        }
        if (slot.used && (slot.id == id)) {
            slot.since_ms = now_ms; //!< 同じ指の押し直し（離す出来事が届かなかった）
            return;
        }
        if (!slot.used && (free_slot == nullptr)) {
            free_slot = &slot;
        }
    }
    if (free_slot != nullptr) {
        *free_slot = FingerSlot{ true, id, now_ms };
    }
}

void MapClickGesture::remove_finger(std::int64_t id)
{
    for (FingerSlot &slot : this->fingers_) {
        if (slot.used && (slot.id == id)) {
            slot = FingerSlot{};
        }
    }
}

bool MapClickGesture::press(Source source, std::int64_t id, int sx, int sy, const MapClick &target,
    std::uint32_t now_ms, bool accept)
{
    if (this->active_ && ((now_ms - this->start_ms_) > kFingerStaleMs)) {
        this->active_ = false; //!< 離す出来事を取りこぼした古い押し。次の押しを塞がない
    }
    if (source == Source::Touch) {
        this->add_finger(id, now_ms);
        if (this->fingers() != 1) {
            //! 2 本目の指（表が満杯で数えられなかったときも同じ扱い）。追っていた 1 本目も含めて、
            //! どちらもクリックにしない（両手の操作・ピンチ）。
            this->active_ = false;
            return false;
        }
    }
    if (!accept) {
        return false;
    }
    if (this->active_ && ((source != this->source_) || (id != this->id_))) {
        //! すでに別の押しを追っている（左を押したまま右を押した、など）。先の押しを優先する。
        return false;
    }
    //! 同じ押しがもう一度来たときは、離す出来事が届かなかったのだと見て押し直しにする。
    this->active_ = true;
    this->source_ = source;
    this->id_ = id;
    this->start_x_ = sx;
    this->start_y_ = sy;
    this->start_ms_ = now_ms;
    this->moved_ = false;
    this->target_ = target;
    return true;
}

void MapClickGesture::move(Source source, std::int64_t id, int sx, int sy)
{
    if (!this->active_ || (source != this->source_) || (id != this->id_) || this->moved_) {
        return;
    }
    if (this->beyond_slop(sx, sy)) {
        this->moved_ = true;
    }
}

bool MapClickGesture::release(Source source, std::int64_t id, int sx, int sy, std::uint32_t now_ms, MapClick &out)
{
    if (source == Source::Touch) {
        this->remove_finger(id);
    }
    if (!this->active_ || (source != this->source_) || (id != this->id_)) {
        return false;
    }
    this->active_ = false;
    if (this->moved_ || this->beyond_slop(sx, sy)) {
        return false;
    }
    out = this->target_;
    if (source == Source::Touch) {
        out.button = ((now_ms - this->start_ms_) >= kLongPressMs) ? MapClickButton::Right : MapClickButton::Left;
    }
    return true;
}

bool MapClickGesture::poll(std::uint32_t now_ms, MapClick &out)
{
    if (!this->active_ || (this->source_ != Source::Touch) || this->moved_) {
        return false;
    }
    if ((now_ms - this->start_ms_) < kLongPressMs) {
        return false;
    }
    out = this->target_;
    out.button = MapClickButton::Right;
    //! この押しはここで終わり。指はこのあと文脈メニューなど別の持ち主の物になり、離す出来事が
    //! こちらへ届かないことがある——数えたまま残すと、次のタップが「2 本目の指」になってしまう。
    this->remove_finger(this->id_);
    this->active_ = false;
    return true;
}

const char *map_click_frame_block_reason(const GameFrame &frame)
{
    if (frame.title_screen || frame.pre_game_menu) {
        return "not in the game";
    }
    if (frame.cells.empty()) {
        return "no map";
    }
    if (frame.menu_open || !frame.menu_term_lines.empty()) {
        return "menu open";
    }
    if (frame.targeting.active) {
        return "targeting";
    }
    if (!frame.prompt.choices.empty() || !frame.prompt.text_utf8.empty()) {
        return "prompt";
    }
    if (frame.numeric.active) {
        return "number input";
    }
    if (frame.text_input_active) {
        return "text input";
    }
    return nullptr;
}

bool map_click_cell_valid(const GameFrame &frame, int gx, int gy)
{
    if ((gx < 0) || (gy < 0) || (gx > 255) || (gy > 255)) {
        return false;
    }
    if (frame.minimap.valid() && ((gx >= frame.minimap.width) || (gy >= frame.minimap.height))) {
        return false;
    }
    return true;
}

void push_map_click(presentation::InputEventsMessage &out, const MapClick &click)
{
    presentation::InputEventWire wire;
    wire.e = "map_click";
    wire.grid_x = click.gx;
    wire.grid_y = click.gy;
    wire.button = static_cast<int>(click.button);
    wire.shift = click.mods.shift;
    wire.ctrl = click.mods.ctrl;
    wire.alt = click.mods.alt;
    out.events.push_back(std::move(wire));
}

} // namespace hd2d
