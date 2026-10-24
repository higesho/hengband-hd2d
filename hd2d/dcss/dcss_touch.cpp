/*!
 * @file dcss_touch.cpp
 * @brief `dcss_touch.h` の実装。
 */
#include "dcss/dcss_touch.h"

#include "dcss/dcss_tabs.h"
#include "dcss/dcss_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hd2d::dcss {

namespace {

SDL_Event make_motion(float x, float y, float xrel, float yrel, bool pressed)
{
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.which = kTouchMouseId;
    e.motion.state = pressed ? SDL_BUTTON_LMASK : 0u;
    e.motion.x = static_cast<int>(x);
    e.motion.y = static_cast<int>(y);
    e.motion.xrel = static_cast<int>(xrel);
    e.motion.yrel = static_cast<int>(yrel);
    return e;
}

SDL_Event make_button(Uint32 type, Uint8 button, float x, float y)
{
    SDL_Event e{};
    e.type = type;
    e.button.which = kTouchMouseId;
    e.button.button = button;
    e.button.state = (type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = 1;
    e.button.x = static_cast<int>(x);
    e.button.y = static_cast<int>(y);
    return e;
}

SDL_Event make_wheel(int ticks, float x, float y)
{
    SDL_Event e{};
    e.type = SDL_MOUSEWHEEL;
    e.wheel.which = kTouchMouseId;
    e.wheel.x = 0;
    e.wheel.y = ticks;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2, 26, 0)
    e.wheel.preciseX = 0.f;
    e.wheel.preciseY = static_cast<float>(ticks);
    e.wheel.mouseX = static_cast<Sint32>(x);
    e.wheel.mouseY = static_cast<Sint32>(y);
#else
    (void)x;
    (void)y;
#endif
    return e;
}

} // namespace

void DcssTouch::on_finger(const SDL_Event &event, int screen_w, int screen_h, std::uint32_t now, bool ignore_down, std::vector<SDL_Event> &out)
{
    const float px = event.tfinger.x * static_cast<float>(screen_w);
    const float py = event.tfinger.y * static_cast<float>(screen_h);
    const std::int64_t id = static_cast<std::int64_t>(event.tfinger.fingerId);
    switch (event.type) {
    case SDL_FINGERDOWN: {
        // 焦点が移るなどで離す出来事が届かなかった古い指は捨てる（残るとクリックが出なくなる）。
        for (auto it = this->fingers_.begin(); it != this->fingers_.end();) {
            it = ((now - it->second.t0) > kStaleFingerMs) ? this->fingers_.erase(it) : std::next(it);
        }
        if (this->fingers_.empty()) {
            this->scrolling_ = false;
        }
        Finger f;
        f.x0 = f.x = px;
        f.y0 = f.y = py;
        f.last_y = py;
        f.t0 = now;
        f.ignored = ignore_down;
        this->fingers_[id] = f;
        if (this->fingers_.size() >= 2) {
            this->scrolling_ = true; // 原作 `ACTION_POINTER_DOWN`: 全部離れるまでクリックにしない
            this->scroll_acc_ = 0.f;
        }
        break;
    }
    case SDL_FINGERMOTION: {
        const auto it = this->fingers_.find(id);
        if (it == this->fingers_.end()) {
            break;
        }
        Finger &f = it->second;
        const float dy = py - f.last_y;
        const float dx = px - f.x;
        f.x = px;
        f.y = py;
        f.last_y = py;
        f.dy_last = dy;
        if (this->fingers_.size() >= 2) {
            // 2 本の指が同じ向きに縦に動いたとき、先の指（番号の小さい方）の動きでホイールを回す（原作 `scroll0 * scroll1 > 0`）。
            // 先の指が動いたときにだけ数える（もう 1 本の動きは、その指の最後の動きの向きで見る）。
            const auto first = this->fingers_.begin();
            const auto second = std::next(first);
            if ((it == first) && ((first->second.dy_last * second->second.dy_last) > 0.f)) {
                this->scroll_acc_ += dy;
                int ticks = 0;
                while (std::fabs(this->scroll_acc_) >= kScrollPixelsPerTick) {
                    ticks += (this->scroll_acc_ > 0.f) ? 1 : -1;
                    this->scroll_acc_ += (this->scroll_acc_ > 0.f) ? -kScrollPixelsPerTick : kScrollPixelsPerTick;
                }
                if (ticks != 0) {
                    out.push_back(make_wheel(ticks, px, py));
                }
            }
            break;
        }
        if (f.ignored) {
            break;
        }
        out.push_back(make_motion(px, py, dx, dy, true));
        break;
    }
    case SDL_FINGERUP: {
        const auto it = this->fingers_.find(id);
        if (it == this->fingers_.end()) {
            break;
        }
        const Finger f = it->second;
        this->fingers_.erase(it);
        if (!this->scrolling_ && !f.ignored) {
            // 動いたのが画面の短い辺の 1% 以内なら、クリック（500 ミリ秒未満 = 左・以上 = 右）。
            const float margin = static_cast<float>(std::min(screen_w, screen_h)) / 100.f;
            if ((std::fabs(px - f.x0) <= margin) && (std::fabs(py - f.y0) <= margin)) {
                const bool left = (now - f.t0) < kLongPressMs;
                out.push_back(make_motion(px, py, px - f.x, py - f.y, false));
                out.push_back(make_button(SDL_MOUSEBUTTONDOWN, left ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT, px, py));
                out.push_back(make_button(SDL_MOUSEBUTTONUP, left ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT, px, py));
            }
        }
        if (this->fingers_.empty()) {
            this->scrolling_ = false;
            this->scroll_acc_ = 0.f;
        }
        break;
    }
    default:
        break;
    }
}

namespace {

SDL_Event finger_event(Uint32 type, std::int64_t id, float x, float y)
{
    SDL_Event e{};
    e.type = type;
    e.tfinger.fingerId = static_cast<SDL_FingerID>(id);
    e.tfinger.x = x;
    e.tfinger.y = y;
    return e;
}

} // namespace

int dcss_touch_selftest()
{
    int failures = 0;
    const auto check = [&](bool ok, const char *what) {
        if (!ok) {
            std::fprintf(stderr, "[dcss-touch] selftest: FAIL %s\n", what);
            ++failures;
        }
    };
    constexpr int W = 2400;
    constexpr int H = 1080;
    {
        // 短いタップ = 左クリック（動き・押す・離す）。位置は離した所。
        DcssTouch t;
        std::vector<SDL_Event> out;
        t.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.5f, 0.5f), W, H, 1000, false, out);
        check(out.empty(), "down emits nothing");
        t.on_finger(finger_event(SDL_FINGERUP, 1, 0.5f, 0.5f), W, H, 1200, false, out);
        check(out.size() == 3, "tap emits motion+down+up");
        check(out.size() == 3 && out[1].type == SDL_MOUSEBUTTONDOWN && out[1].button.button == SDL_BUTTON_LEFT, "tap is the left button");
        check(out.size() == 3 && out[1].button.x == 1200 && out[1].button.y == 540, "tap position");
        check(out.size() == 3 && out[1].button.which == kTouchMouseId, "tap which");
    }
    {
        // 長押し（500 ミリ秒以上）= 右クリック。
        DcssTouch t;
        std::vector<SDL_Event> out;
        t.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.25f, 0.25f), W, H, 1000, false, out);
        t.on_finger(finger_event(SDL_FINGERUP, 1, 0.25f, 0.25f), W, H, 1500, false, out);
        check(out.size() == 3 && out[1].button.button == SDL_BUTTON_RIGHT, "long press is the right button");
        out.clear();
        DcssTouch u;
        u.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.25f, 0.25f), W, H, 1000, false, out);
        u.on_finger(finger_event(SDL_FINGERUP, 1, 0.25f, 0.25f), W, H, 1499, false, out);
        check(out.size() == 3 && out[1].button.button == SDL_BUTTON_LEFT, "499 ms is still the left button");
    }
    {
        // 1% を超えて動いたらクリックにならない。動きは出る。
        DcssTouch t;
        std::vector<SDL_Event> out;
        t.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.5f, 0.5f), W, H, 1000, false, out);
        t.on_finger(finger_event(SDL_FINGERMOTION, 1, 0.5f, 0.55f), W, H, 1050, false, out);
        check(out.size() == 1 && out[0].type == SDL_MOUSEMOTION, "drag emits a motion");
        t.on_finger(finger_event(SDL_FINGERUP, 1, 0.5f, 0.6f), W, H, 1100, false, out);
        check(out.size() == 1, "a drag past 1% is not a click");
        // 1% 以内（短い辺 1080 の 1% = 10.8 画素）。
        DcssTouch u;
        out.clear();
        u.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.5f, 0.5f), W, H, 1000, false, out);
        u.on_finger(finger_event(SDL_FINGERUP, 1, 0.5f, 0.5f + (10.f / H)), W, H, 1100, false, out);
        check(out.size() == 3, "a move within 1% is still a click");
    }
    {
        // 2 本指: クリックにならない。同じ向きの縦の動きでホイール。
        DcssTouch t;
        std::vector<SDL_Event> out;
        t.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.4f, 0.5f), W, H, 1000, false, out);
        t.on_finger(finger_event(SDL_FINGERDOWN, 2, 0.6f, 0.5f), W, H, 1000, false, out);
        // 実機の指の動きは 2 本が交互に届く。2 周目から、2 本とも動いた向きがそろう。
        t.on_finger(finger_event(SDL_FINGERMOTION, 1, 0.4f, 0.5f + (10.f / H)), W, H, 1030, false, out);
        t.on_finger(finger_event(SDL_FINGERMOTION, 2, 0.6f, 0.5f + (10.f / H)), W, H, 1030, false, out);
        t.on_finger(finger_event(SDL_FINGERMOTION, 1, 0.4f, 0.5f + (25.f / H)), W, H, 1050, false, out);
        t.on_finger(finger_event(SDL_FINGERMOTION, 2, 0.6f, 0.5f + (25.f / H)), W, H, 1050, false, out);
        int ticks = 0;
        for (const SDL_Event &e : out) {
            if (e.type == SDL_MOUSEWHEEL) {
                ticks += e.wheel.y;
            }
        }
        check(ticks > 0, "two fingers moving down scroll up");
        const std::size_t before = out.size();
        t.on_finger(finger_event(SDL_FINGERUP, 1, 0.4f, 0.5f + (25.f / H)), W, H, 1100, false, out);
        t.on_finger(finger_event(SDL_FINGERUP, 2, 0.6f, 0.5f + (25.f / H)), W, H, 1100, false, out);
        check(out.size() == before, "no click after a two-finger gesture");
        check(t.fingers_down() == 0, "all fingers are released");
        // その後の 1 本指のタップは普通に通る。
        t.on_finger(finger_event(SDL_FINGERDOWN, 3, 0.5f, 0.5f), W, H, 2000, false, out);
        t.on_finger(finger_event(SDL_FINGERUP, 3, 0.5f, 0.5f), W, H, 2100, false, out);
        check(out.size() == before + 3, "a tap works again after the gesture");
    }
    {
        // 反対向きの 2 本はホイールにならない。
        DcssTouch t;
        std::vector<SDL_Event> out;
        t.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.4f, 0.5f), W, H, 1000, false, out);
        t.on_finger(finger_event(SDL_FINGERDOWN, 2, 0.6f, 0.5f), W, H, 1000, false, out);
        for (int round = 1; round <= 3; ++round) {
            t.on_finger(finger_event(SDL_FINGERMOTION, 1, 0.4f, 0.5f + ((50.f * round) / H)), W, H, 1050 + round, false, out);
            t.on_finger(finger_event(SDL_FINGERMOTION, 2, 0.6f, 0.5f - ((50.f * round) / H)), W, H, 1050 + round, false, out);
        }
        bool wheel = false;
        for (const SDL_Event &e : out) {
            wheel = wheel || (e.type == SDL_MOUSEWHEEL);
        }
        check(!wheel, "opposite directions do not scroll");
    }
    {
        // 仮想パッドの近くを触った指は、クリックにも動きにもならない。
        DcssTouch t;
        std::vector<SDL_Event> out;
        t.on_finger(finger_event(SDL_FINGERDOWN, 1, 0.5f, 0.5f), W, H, 1000, true, out);
        t.on_finger(finger_event(SDL_FINGERMOTION, 1, 0.5f, 0.52f), W, H, 1050, true, out);
        t.on_finger(finger_event(SDL_FINGERUP, 1, 0.5f, 0.52f), W, H, 1100, true, out);
        check(out.empty(), "an ignored finger emits nothing");
    }
    {
        // 名前の問い（原作 `_choose_name`）。ウェブタイル版の構造では、窓は来ず、`init_input`（`messages`）の直後の `close_input` だけが来る。
        // 実機の記録（f4ab86c4e9 の仕掛けで取った `dcss_trace.jsonl`）と同じ並びで試す。
        const char *const kWeaponUi =
            "{\"title\":\"<brown>ようこそ、名もなきノールの戦士。\",\"prompt\":\"<cyan>武器を選んでください。\",\"main-items\":{\"menu_id\":\"weapon-main\",\"width\":1,\"height\":6,\"buttons\":[]},\"msg\":\"ui-push\",\"type\":\"newgame-choice\"}";
        const char *const kInit = "{\"msg\":\"init_input\",\"type\":\"messages\",\"prefill\":\"\",\"select_prefill\":false,\"maxlen\":30,\"size\":15}";
        const char *const kClose = "{\"msg\":\"close_input\"}";
        {
            DcssWindowStack st;
            st.set_pregame(true);
            (void)st.ingest_line(kWeaponUi);
            (void)st.ingest_line("{\"msg\":\"ui-pop\"}");
            check(st.empty(), "the weapon window is closed");
            (void)st.ingest_line(kInit);
            check(!st.name_prompt(), "init_input alone is not yet the name prompt");
            (void)st.ingest_line(kClose);
            check(st.name_prompt(), "init_input then close_input before the game is the name prompt");
            check(st.name_prompt_job() == "ノールの戦士", "the job is taken from the last creation title");
            check(st.text_input().active, "the name prompt takes keys (text input is on)");
            // ゲームの画面が来たら消える。
            st.set_pregame(false);
            check(!st.name_prompt() && !st.text_input().active, "the name prompt goes away when the game starts");
        }
        {
            // 作成の窓へ戻る（Esc）と消える。
            DcssWindowStack st;
            st.set_pregame(true);
            (void)st.ingest_line(kWeaponUi);
            (void)st.ingest_line("{\"msg\":\"ui-pop\"}");
            (void)st.ingest_line(kInit);
            (void)st.ingest_line(kClose);
            check(st.name_prompt(), "name prompt (again)");
            (void)st.ingest_line(kWeaponUi);
            check(!st.name_prompt(), "a creation window closes the name prompt");
        }
        {
            // ゲームの中の同じ並び（メニューの絞り込みの読み手など）は問いにしない。
            DcssWindowStack st;
            st.set_pregame(false);
            (void)st.ingest_line(kInit);
            (void)st.ingest_line(kClose);
            check(!st.name_prompt(), "the same pair in the game is not the name prompt");
            // 文字画面の入力（`generic`）も問いにしない。
            DcssWindowStack gt;
            gt.set_pregame(true);
            (void)gt.ingest_line("{\"msg\":\"init_input\",\"type\":\"generic\",\"prefill\":\"\",\"maxlen\":30}");
            (void)gt.ingest_line(kClose);
            check(!gt.name_prompt(), "a generic (crt) input is not the name prompt");
            // 本物の 1 行入力（閉じるまでに別の行が挟まる）も問いにしない。
            DcssWindowStack rt;
            rt.set_pregame(true);
            (void)rt.ingest_line(kInit);
            (void)rt.ingest_line("{\"msg\":\"txt\",\"id\":\"menu_txt\",\"lines\":{}}");
            (void)rt.ingest_line(kClose);
            check(!rt.name_prompt(), "a real line input is not the name prompt");
        }
    }
    failures += dcss_gear_heading_selftest(); // 右端の見出しの列の歯車（D31）
    if (failures == 0) {
        std::fprintf(stderr, "[dcss-touch] selftest: ok\n");
    }
    return failures;
}

} // namespace hd2d::dcss
