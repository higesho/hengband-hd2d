/*!
 * @file dcss_mouse.cpp
 * @brief `dcss_mouse.h` の実装。
 */
#include "dcss/dcss_mouse.h"

#include "app/ui_probe.h"
#include "dcss/dcss_cells.h"
#include "dcss/dcss_model.h"

#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <climits>

namespace hd2d::dcss {

namespace {

constexpr std::uint32_t kTooltipDelayMs = 500; //!< 原作 `tile_tooltip_ms`（Windows の既定）

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char c : text) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

TextColor text_colour(int colour)
{
    const DcssRgb &c = dcss_term_rgb(colour);
    return TextColor{ static_cast<float>(c.r) / 255.f, static_cast<float>(c.g) / 255.f, static_cast<float>(c.b) / 255.f, 1.f };
}

} // namespace

void DcssMapMouse::request_hover(int dx, int dy)
{
    this->outgoing_.push_back("{\"msg\":\"hd2d_hover\",\"dx\":" + std::to_string(dx) + ",\"dy\":" + std::to_string(dy) + "}");
    ++this->queries_sent_;
}

void DcssMapMouse::clear()
{
    this->tip_text_.clear();
    this->tag_text_.clear();
    this->tip_have_ = false;
    this->tip_shown_ = false;
    this->cell_valid_ = false;
}

std::vector<std::string> DcssMapMouse::take_outgoing()
{
    std::vector<std::string> out;
    out.swap(this->outgoing_);
    return out;
}

void DcssMapMouse::ingest_reply(const std::string &payload)
{
    const nlohmann::json m = nlohmann::json::parse(payload, nullptr, false);
    if (m.is_discarded() || !m.is_object()) {
        return;
    }
    const int dx = m.value("dx", 0);
    const int dy = m.value("dy", 0);
    if (!this->cell_valid_ || (dx != this->cell_dx_) || (dy != this->cell_dy_)) {
        return; // 古い返事（もう別のマスにいる）
    }
    this->tip_text_ = m.value("tip", std::string());
    this->tag_text_ = m.value("tag", std::string());
    this->tip_have_ = true;
}

bool DcssMapMouse::on_event(const SDL_Event &event, bool on_map, int dx, int dy, int player_x, int player_y, int input_mode, int mods)
{
    if (event.type == SDL_MOUSEMOTION) {
        if (event.motion.which == SDL_TOUCH_MOUSEID) {
            return false;
        }
        this->mouse_x_ = event.motion.x;
        this->mouse_y_ = event.motion.y;
        this->moved_at_ = SDL_GetTicks();
        this->tip_shown_ = false;
        this->on_map_ = on_map;
        if (!on_map) {
            this->clear();
            return false;
        }
        if (!this->cell_valid_ || (dx != this->cell_dx_) || (dy != this->cell_dy_)) {
            this->cell_dx_ = dx;
            this->cell_dy_ = dy;
            this->cell_valid_ = true;
            this->tip_have_ = false;
            this->tip_text_.clear();
            this->tag_text_.clear();
            if ((input_mode >= 2) && (input_mode <= 4)) {
                // 狙い（点・方向・経路）: カーソルを寄せる（原作 `CK_MOUSE_MOVE`）。
                const int tx = player_x + dx;
                const int ty = player_y + dy;
                if ((tx != this->last_target_x_) || (ty != this->last_target_y_)) {
                    this->last_target_x_ = tx;
                    this->last_target_y_ = ty;
                    this->outgoing_.push_back("{\"msg\":\"target_cursor\",\"x\":" + std::to_string(tx) + ",\"y\":" + std::to_string(ty) + "}");
                }
            } else if ((input_mode == 0) || (input_mode == 1)) {
                this->request_hover(dx, dy);
            }
        }
        return false;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        if ((event.button.which == SDL_TOUCH_MOUSEID) || !on_map) {
            return false;
        }
        const bool left = (event.button.button == SDL_BUTTON_LEFT);
        const bool right = (event.button.button == SDL_BUTTON_RIGHT);
        if (!left && !right) {
            return false;
        }
        this->tip_shown_ = false;
        this->moved_at_ = SDL_GetTicks();
        const int button = left ? 1 : 3;
        if ((input_mode >= 5) && (input_mode <= 8)) {
            return true; // -more-・問い・はい/いいえ: 地図のクリックには反応しない（原作 `MOUSE_MODE_MORE` ほか）
        }
        if ((input_mode == 1) && (dx == 0) && (dy == 0)) {
            this->outgoing_.push_back("{\"msg\":\"hd2d_click_self\",\"button\":" + std::to_string(button) + ",\"mods\":" + std::to_string(mods) + "}");
            return true;
        }
        this->outgoing_.push_back("{\"msg\":\"click_cell\",\"x\":" + std::to_string(player_x + dx) + ",\"y\":" + std::to_string(player_y + dy)
            + ",\"button\":" + std::to_string(button) + "}");
        return true;
    }
    return false;
}

void DcssMapMouse::draw(UiPaint &paint, TextOverlay &tip_font, TextOverlay &tag_font, const RectPx &area, int screen_w, int screen_h, int ui_scale)
{
    if (!this->on_map_ || !this->cell_valid_) {
        return;
    }
    // マスの説明の 1 行（白。マウスの位置の上に中央寄せ）。
    if (!this->tag_text_.empty()) {
        const UiProbeLayer probe_layer("dcss-tag");
        tag_font.begin(screen_w, screen_h);
        if (ui_scale > 1) {
            tag_font.set_layout_scale(static_cast<float>(ui_scale)); // 窓の拡大（Android・Quest）。座標は論理のまま
        }
        const std::vector<std::string> lines = split_lines(this->tag_text_);
        const int half = std::max(1, tag_font.cell_w());
        const int line_h = std::max(1, tag_font.cell_h());
        int total_h = static_cast<int>(lines.size()) * line_h;
        int y = this->mouse_y_ - 16 - total_h;
        for (const std::string &l : lines) {
            const int w = dcss_text_width(l) * half;
            tag_font.draw(this->mouse_x_ - (w / 2), y, l, text_colour(kWhite));
            y += line_h;
        }
        tag_font.flush();
    }
    // ツールチップ（500 ミリ秒じっとしたとき）。
    if (!this->tooltip_enabled_) {
        return;
    }
    const std::uint32_t now = SDL_GetTicks();
    if (this->tip_have_ && !this->tip_text_.empty() && ((now - this->moved_at_) >= kTooltipDelayMs)) {
        this->tip_shown_ = true;
    }
    if (!this->tip_shown_ || this->tip_text_.empty()) {
        return;
    }
    const UiProbeLayer probe_layer("dcss-tip");
    tip_font.begin(screen_w, screen_h);
    if (ui_scale > 1) {
        tip_font.set_layout_scale(static_cast<float>(ui_scale));
    }
    const std::vector<std::string> lines = split_lines(this->tip_text_);
    const int half = std::max(1, tip_font.cell_w());
    const int line_h = std::max(1, tip_font.cell_h());
    int wx = 0;
    for (const std::string &l : lines) {
        wx = std::max(wx, dcss_text_width(l) * half);
    }
    const int wy = static_cast<int>(lines.size()) * line_h;
    int outline = 7;
    int tx = this->mouse_x_ - 15;
    int ty = this->mouse_y_ + 20;
    const int buffer = 5;
    const int min_x = area.x + buffer;
    const int min_y = area.y + buffer;
    const int max_x = area.x + area.w - buffer;
    const int max_y = area.y + area.h - buffer;
    const int sx = tx - outline;
    const int sy = ty - outline;
    const int ex = tx + wx + outline;
    const int ey = ty + wy + outline;
    if (ex > max_x) {
        tx += max_x - ex;
    } else if (sx < min_x) {
        tx -= sx - min_x;
    }
    if (ey > max_y) {
        ty += max_y - ey;
    } else if (sy < min_y) {
        ty -= sy - min_y;
    }
    paint.rect(RectPx{ tx - outline, ty - outline, wx + (2 * outline), wy + (2 * outline) }, PaintColor{ 125.f / 255.f, 98.f / 255.f, 60.f / 255.f, 1.f });
    outline -= 2;
    paint.rect(RectPx{ tx - outline, ty - outline, wx + (2 * outline), wy + (2 * outline) }, PaintColor{ 4.f / 255.f, 2.f / 255.f, 4.f / 255.f, 1.f });
    paint.flush();
    int y = ty;
    for (const std::string &l : lines) {
        tip_font.draw(tx, y - 1, l, text_colour(kLightGrey));
        y += line_h;
    }
    tip_font.flush();
}

} // namespace hd2d::dcss
