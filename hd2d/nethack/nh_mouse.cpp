/*!
 * @file nh_mouse.cpp
 * @brief `nh_mouse.h` の実装。
 */
#include "nethack/nh_mouse.h"

namespace hd2d::nethack {

bool nh_map_cell_ok(int x, int y)
{
    return (x >= 1) && (x < kNhMapCols) && (y >= 0) && (y < kNhMapRows);
}

std::string nh_pos_reply_json(int x, int y, int mod)
{
    return "{\"t\":\"nethack_reply\",\"r\":0,\"a\":\"pos\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"mod\":" + std::to_string(mod) + "}";
}

std::string nh_hover_query_json(int x, int y)
{
    return "{\"t\":\"nethack_query\",\"q\":\"hover\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}";
}

NhPressResult NhMapMouse::on_press(const NhMapMouseContext &ctx, bool on_map, int x, int y, int button, int clicks)
{
    NhPressResult result;
    if (!on_map || ((button != 1) && (button != 2))) {
        return result;
    }
    result.consumed = true;
    if (!ctx.allowed() || !nh_map_cell_ok(x, y)) {
        return result;
    }
    // 右クリック、または連続の 2 回目の左クリックは CLICK_2（原作の GUI 版 `mhmap.c` の WM_RBUTTONDOWN・WM_LBUTTONDBLCLK）
    const int mod = ((button == 2) || (clicks >= 2)) ? 2 : 1;
    result.reply = nh_pos_reply_json(x, y, mod);
    // 返したので待ちは終わる。次の待ちで説明を聞き直す。
    this->answered_ = false;
    return result;
}

std::string NhMapMouse::maybe_ask(const NhMapMouseContext &ctx, std::uint64_t wait_serial)
{
    if (!this->cell_valid_ || !ctx.allowed()) {
        return {};
    }
    if (this->asked_ && (this->asked_x_ == this->cx_) && (this->asked_y_ == this->cy_) && (this->asked_serial_ == wait_serial)) {
        return {};
    }
    this->asked_ = true;
    this->asked_x_ = this->cx_;
    this->asked_y_ = this->cy_;
    this->asked_serial_ = wait_serial;
    return nh_hover_query_json(this->cx_, this->cy_);
}

std::string NhMapMouse::on_motion(const NhMapMouseContext &ctx, bool on_map, int x, int y, int px, int py, std::uint64_t wait_serial)
{
    this->px_ = px;
    this->py_ = py;
    if (!on_map || !nh_map_cell_ok(x, y)) {
        this->cell_valid_ = false;
        return {};
    }
    if (!this->cell_valid_ || (this->cx_ != x) || (this->cy_ != y)) {
        this->cell_valid_ = true;
        this->cx_ = x;
        this->cy_ = y;
        this->answered_ = false; // 別のマスの説明は出さない
    }
    return this->maybe_ask(ctx, wait_serial);
}

std::string NhMapMouse::on_tick(const NhMapMouseContext &ctx, std::uint64_t wait_serial)
{
    return this->maybe_ask(ctx, wait_serial);
}

void NhMapMouse::on_answer(int x, int y, const std::string &text)
{
    if (!this->cell_valid_ || (x != this->cx_) || (y != this->cy_)) {
        return;
    }
    this->answered_ = true;
    this->ans_x_ = x;
    this->ans_y_ = y;
    this->text_ = text;
}

bool NhMapMouse::tooltip(const NhMapMouseContext &ctx, std::string &text, int &px, int &py) const
{
    if (!this->cell_valid_ || !this->answered_ || this->text_.empty() || !ctx.allowed()) {
        return false;
    }
    if ((this->ans_x_ != this->cx_) || (this->ans_y_ != this->cy_)) {
        return false;
    }
    text = this->text_;
    px = this->px_;
    py = this->py_;
    return true;
}

bool NhMapMouse::hovered_cell(int &x, int &y) const
{
    if (!this->cell_valid_) {
        return false;
    }
    x = this->cx_;
    y = this->cy_;
    return true;
}

void NhMapMouse::reset()
{
    *this = NhMapMouse{};
}

std::string nh_map_key_hint(bool ja, const std::string &wait_reason)
{
    if (wait_reason == "command") {
        return ja ? "左クリック:移動・操作  右クリック:調べる  ;:遠くを見る  i:持ち物  ?:ヘルプ  "
                  : "Left click:Move/Act  Right click:Look  ;:Farlook  i:Inventory  ?:Help  ";
    }
    if (wait_reason == "pos") {
        return ja ? "クリックか . , ; : で決定  hjklyubn:カーソル移動  m o d x:候補へ  ?:説明  Esc:取消  "
                  : "Click or . , ; : to select  hjklyubn:Move cursor  m o d x:Jump  ?:Help  Esc:Cancel  ";
    }
    return {};
}

} // namespace hd2d::nethack
