/*!
 * @file sound_event_queue.cpp
 * @brief `sound_event_queue.h` の実装。**溜めて汲むだけ。**
 */
#include "audio/sound_event_queue.h"

#include <algorithm>
#include <mutex>

namespace presentation {

namespace {

/*!
 * 1 フレームに載せる上限。**溢れたら古いほうから捨てる。**
 * 画面は 1 フレームぶんしか鳴らせないので、溜め込んでも遅れて鳴るだけになる。
 */
constexpr std::size_t kMaxPending = 64;

std::mutex g_mutex;
std::vector<SoundEvent> g_pending;
bool g_wanted = false;

} // namespace

bool sound_events_wanted()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_wanted;
}

void set_sound_events_wanted(bool wanted)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_wanted && !wanted) {
        //! 画面側が受け取らなくなったら**溜めているものは捨てる**（後で一気に鳴らさない）。
        g_pending.clear();
    }
    g_wanted = wanted;
}

void push_sound_event(const std::string &name, int y, int x)
{
    if (name.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_wanted) {
        return;
    }
    if (g_pending.size() >= kMaxPending) {
        g_pending.erase(g_pending.begin());
    }
    SoundEvent event;
    event.name = name;
    event.y = static_cast<int16_t>(y);
    event.x = static_cast<int16_t>(x);
    g_pending.push_back(std::move(event));
}

std::vector<SoundEvent> take_sound_events()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<SoundEvent> out;
    out.swap(g_pending);
    return out;
}

} // namespace presentation
