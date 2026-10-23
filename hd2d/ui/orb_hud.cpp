/*!
 * @file orb_hud.cpp
 * @brief `orb_hud.h` の実装。
 */
#include "ui/orb_hud.h"
#include "app/ui_probe.h"

#include "render/text_overlay.h"
#include "ui/ui_paint.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace hd2d {

namespace {

struct IconEntry {
    const char *name;
    int index;
};

//! 名前 → シートの番号（生成物。`tools/hud/gen_status_icons.py` が書く。手で直さない）。
constexpr IconEntry kIconTable[] = {
#include "ui/status_icon_table.inc"
};

//! シートの 1 セルの一辺（画素）と列数。`status_icons.png` は 64x64 を 10 列に並べたもの。
constexpr int kIconSheetColumns = 10;

//! 玉の液体の水面の高さの範囲（設計の単位）。**いま覚えている配置**（`orb_profile()`）から決まる。
double liquid_top() //!< 満杯のときの水面
{
    return orb_profile().cy - orb_profile().r;
}

double liquid_bottom() //!< 空のときの水面
{
    return orb_profile().cy + orb_profile().r;
}

//! 玉の素材の矩形と設計の単位の対応。
struct OrbMap {
    RectPx r;
    double sx;
    double sy;
    explicit OrbMap(const RectPx &rect)
        : r(rect)
        , sx(static_cast<double>(rect.w) / static_cast<double>(kOrbDesignW))
        , sy(static_cast<double>(rect.h) / static_cast<double>(kOrbDesignH))
    {
    }
    double x(double design) const { return static_cast<double>(r.x) + (design * sx); }
    double y(double design) const { return static_cast<double>(r.y) + (design * sy); }
};

float clamp01(float v)
{
    return std::clamp(v, 0.f, 1.f);
}

std::uint32_t next_rand(std::uint32_t &state)
{
    //! xorshift32。**検査で同じ泡の並びになるよう**、時計や乱数源を使わず状態だけで進める。
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float rand01(std::uint32_t &state)
{
    return static_cast<float>(next_rand(state) & 0xFFFFFFu) / 16777216.f;
}

//! 基準の波の振幅（設計の単位）。ダメージの直後はこれに `boost` が乗る。
constexpr double kWaveBaseAmp = 2.4;
constexpr double kWaveBoostAmp = 6.0;
//! 回復で 1 秒に上がる割合（100% の速さのとき）。**ゆっくり**（空から満杯まで 5 秒）。
constexpr float kRisePerSecond = 0.20f;
//! ダメージで液面が下がる速さ（指数の係数と、最低でもこれだけ進む割合/秒）。
constexpr float kFallRate = 7.0f;
constexpr float kFallFloorPerSecond = 0.35f;
//! 波の大きさ（boost）が 1/e になるまでの速さ（1/秒）。
constexpr float kBoostDecay = 1.5f;
//! 泡が 1 秒に湧く数（上がっている間）と、1 つの玉の泡の上限。
constexpr float kBubbleSpawnPerSecond = 11.f;
constexpr std::size_t kBubbleMax = 28;

void update_gauge(OrbHudGauge &g, const OrbHudParams &p, float ratio, float dt, std::uint32_t &rng, bool snap)
{
    ratio = clamp01(ratio);
    if (snap) {
        g.shown = ratio;
        g.target = ratio;
        g.boost = 0.f;
        g.bubble_carry = 0.f;
        g.bubbles.clear();
        return;
    }
    const float previous = g.target;
    g.target = ratio;
    //! ダメージ（実際の割合が減った）。波を一時的に大きくする。大きく減るほど大きい。
    if (p.wave_enabled && (ratio < (previous - 1e-4f))) {
        g.boost = std::min(1.6f, g.boost + ((previous - ratio) * 5.f) + 0.35f);
    }
    const float diff = ratio - g.shown;
    if (diff > 0.f) {
        //! 回復。一定の速さでゆっくり上がる。
        g.shown = std::min(ratio, g.shown + (kRisePerSecond * p.rise_speed * dt));
    } else if (diff < 0.f) {
        //! ダメージ。指数で寄せる（最初が速い）が、最後でだらだら残らないよう下限の速さも持つ。
        const float exp_step = -diff * (1.f - std::exp(-kFallRate * p.fall_speed * dt));
        const float floor_step = kFallFloorPerSecond * p.fall_speed * dt;
        g.shown = std::max(ratio, g.shown - std::min(-diff, std::max(exp_step, floor_step)));
    }
    if (std::fabs(ratio - g.shown) < 0.0015f) {
        g.shown = ratio;
    }
    g.boost *= std::exp(-kBoostDecay * dt);
    if (g.boost < 0.002f) {
        g.boost = 0.f;
    }

    //! 泡。上がっている間だけ湧き、液面に届くか玉の縁へ出たら消える。
    const OrbProfile &prof = orb_profile();
    const double surface = liquid_bottom() - (static_cast<double>(g.shown) * 2.0 * prof.r);
    if ((g.target - g.shown) > 0.003f && g.shown > 0.01f) {
        g.bubble_carry += kBubbleSpawnPerSecond * dt;
        while ((g.bubble_carry >= 1.f) && (g.bubbles.size() < kBubbleMax)) {
            g.bubble_carry -= 1.f;
            OrbHudBubble b;
            b.dx = ((rand01(rng) * 2.f) - 1.f) * 0.70f * static_cast<float>(prof.r);
            b.y = static_cast<float>(liquid_bottom()) - 6.f - (rand01(rng) * 8.f);
            b.speed = 28.f + (rand01(rng) * 24.f);
            b.size = 1.3f + (rand01(rng) * 1.3f);
            b.phase = rand01(rng) * 6.2831853f;
            g.bubbles.push_back(b);
        }
        g.bubble_carry = std::min(g.bubble_carry, 1.f);
    } else {
        g.bubble_carry = 0.f;
    }
    for (OrbHudBubble &b : g.bubbles) {
        b.y -= b.speed * dt;
    }
    g.bubbles.erase(std::remove_if(g.bubbles.begin(), g.bubbles.end(),
                        [&](const OrbHudBubble &b) {
                            const double half = std::sqrt(std::max(0.0, (prof.r * prof.r)
                                - ((b.y - prof.cy) * (b.y - prof.cy))));
                            return (static_cast<double>(b.y) < (surface + 2.0))
                                || (std::fabs(static_cast<double>(b.dx)) > (half * 0.88));
                        }),
        g.bubbles.end());
}

/*!
 * @brief 水面の波の高さ（設計の単位。正が下）。
 * @param dx 玉の中心からの横の位置（設計の単位）。
 * @details 周期の違う 3 つの正弦波を、位相の進む向きも変えて重ねる（1 本だと機械的に見える）。
 * 満杯や空に近いときは振幅を絞る（水面が玉の外へ出ないように）。
 */
double wave_height(const OrbHudAnimation &anim, const OrbHudParams &p, const OrbHudGauge &g, double dx,
    double phase_offset)
{
    if (!p.wave_enabled) {
        return 0.0;
    }
    const double t = anim.wave_clock;
    const double ratio = static_cast<double>(g.shown);
    const double taper = std::clamp(std::min(ratio, 1.0 - ratio) * 10.0, 0.0, 1.0);
    const double amp = static_cast<double>(p.wave_size) * (kWaveBaseAmp + (kWaveBoostAmp * static_cast<double>(g.boost)));
    const double x = dx + phase_offset;
    const double h = (0.55 * std::sin((0.0898 * x) + (2.2 * t) + phase_offset))
        + (0.30 * std::sin((0.1532 * x) - (3.1 * t) + 1.7))
        + (0.15 * std::sin((0.2732 * x) + (4.7 * t) + 4.1));
    return amp * taper * h;
}

//! 字を `scale` 倍で、暗い縁取りと下へ落とした影をつけて描く（読みやすさのため。玉の上の字）。
void draw_outlined(TextOverlay &text, int x, int y, const std::string &s, const TextColor &color, int max_width,
    float scale = 1.f)
{
    const TextColor dark{ 0.f, 0.f, 0.f, 0.85f };
    const TextColor shadow{ 0.f, 0.f, 0.f, 0.55f };
    static const int kOffsets[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    text.draw_sized(x + 1, y + 2, s, shadow, scale, max_width);
    for (const auto &o : kOffsets) {
        text.draw_sized(x + o[0], y + o[1], s, dark, scale, max_width);
    }
    text.draw_sized(x, y, s, color, scale, max_width);
}

//! `s` を中心 `cx`・上端 `y` に置く（縁取りつき）。
void draw_centered_outlined(TextOverlay &text, int cx, int y, const std::string &s, const TextColor &color, int max_width,
    float scale = 1.f)
{
    const int w = text.measure_sized(s, scale);
    draw_outlined(text, cx - (w / 2), y, s, color, max_width, scale);
}

/*!
 * @brief 液体 1 つを、水面の高さを帯ごとにずらしながら貼る。
 * @details 帯の幅は 2〜3 画素。水面の線は帯と同じ高さに、玉の円の内側だけへ描く。
 */
void draw_liquid(const OrbHudImages &images, const UiImage &liquid, const OrbMap &m, UiPaint &paint,
    const OrbHudAnimation &anim, const OrbHudParams &params, const OrbHudGauge &g, double cx, double phase_offset,
    const PaintColor &line_color)
{
    const float ratio = g.shown;
    if (ratio < 0.005f) {
        return;
    }
    const OrbProfile &prof = orb_profile();
    const double base_y = liquid_bottom() - (static_cast<double>(ratio) * 2.0 * prof.r);
    const int strip = (m.r.h >= 220) ? 3 : 2;
    const int px0 = static_cast<int>(std::floor(m.x(cx - prof.r)));
    const int px1 = static_cast<int>(std::ceil(m.x(cx + prof.r)));
    const int line_h = std::max(2, static_cast<int>(std::lround(1.6 * m.sy)));
    //! 満杯（99% 以上）では水面の線を出さない（玉の頂点に線だけ浮く）。
    const bool show_line = ratio < 0.985f;
    for (int px = px0; px < px1; px += strip) {
        const int w = std::min(strip, px1 - px);
        const double xc = static_cast<double>(px) + (static_cast<double>(w) * 0.5);
        const double dx = ((xc - static_cast<double>(m.r.x)) / m.sx) - cx;
        const double surface = std::clamp(base_y + wave_height(anim, params, g, dx, phase_offset), liquid_top(), liquid_bottom());
        const int y_top = std::clamp(static_cast<int>(std::lround(m.y(surface))), m.r.y, (m.r.y + m.r.h) - 1);
        const RectPx dst{ px, y_top, w, (m.r.y + m.r.h) - y_top };
        const float u0 = static_cast<float>(px - m.r.x) / static_cast<float>(m.r.w);
        const float u1 = static_cast<float>((px + w) - m.r.x) / static_cast<float>(m.r.w);
        const float v0 = static_cast<float>(y_top - m.r.y) / static_cast<float>(m.r.h);
        images.painter->draw_region(liquid, dst, u0, v0, u1, 1.f);

        if (!show_line) {
            continue;
        }
        //! 玉の円の内側だけ（縁の輪へはみ出さない）。円の弦の長さの手前で止める。
        const double rise = surface - prof.cy;
        const double half = std::sqrt(std::max(0.0, (prof.r * prof.r) - (rise * rise)));
        if (std::fabs(dx) > (half - 2.0)) {
            continue;
        }
        paint.rect(RectPx{ px, y_top, w, line_h }, line_color);
        PaintColor glow = line_color;
        glow.a *= 0.35f;
        paint.rect(RectPx{ px, y_top + line_h, w, line_h * 2 }, glow);
    }
}

void draw_bubbles(UiPaint &paint, const OrbMap &m, const OrbHudGauge &g, double cx, const OrbHudAnimation &anim)
{
    for (const OrbHudBubble &b : g.bubbles) {
        const double sway = std::sin((anim.wave_clock * 2.6) + static_cast<double>(b.phase)) * 1.2;
        const double x = cx + static_cast<double>(b.dx) + sway;
        const int size = std::max(2, static_cast<int>(std::lround(static_cast<double>(b.size) * m.sy)));
        const int px = static_cast<int>(std::lround(m.x(x))) - (size / 2);
        const int py = static_cast<int>(std::lround(m.y(static_cast<double>(b.y)))) - (size / 2);
        paint.rect(RectPx{ px, py, size, size }, PaintColor{ 1.f, 1.f, 1.f, 0.55f });
        //! 小さな照り（左上の 1 点）。
        if (size >= 3) {
            paint.rect(RectPx{ px, py, 1, 1 }, PaintColor{ 1.f, 1.f, 1.f, 0.95f });
        }
    }
}

//! 玉の素材が読めなかったときの棒（これまでの HP/MP の棒と同じもの）。
void draw_fallback_bars(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame)
{
    const RectPx &orb = layout.orb;
    const int cw = text.cell_w();
    const int ch = text.cell_h();
    const int bar_h = std::max(4, ch / 2);
    const char *const hp_name = hp_label_of(frame);
    const char *const sp_name = sp_label_of(frame);
    const int label_w = std::max(cw * 3, std::max(text.measure(hp_name), text.measure(sp_name)) + cw);
    const int w = std::min(orb.w, cw * 30);
    const int x = orb.x + ((orb.w - w) / 2);
    const int y = (orb.y + orb.h) - (ch * 2);
    char buf[96]{};
    const auto ratio = [](int now, int max) {
        return (max > 0) ? (static_cast<float>(now) / static_cast<float>(max)) : 0.f;
    };
    const PaintColor back{ 0.f, 0.f, 0.f, 0.55f };
    const TextColor dim{ 0.62f, 0.66f, 0.72f, 1.f };

    text.draw(x, y, hp_name, dim);
    paint.gauge(RectPx{ x + label_w, y + (ch / 4), w - label_w, bar_h }, ratio(frame.hud.hp, frame.hud.hp_max),
        PaintColor{ 0.85f, 0.25f, 0.25f, 0.92f }, back);
    std::snprintf(buf, sizeof(buf), "%d/%d", frame.hud.hp, frame.hud.hp_max);
    ui_probe().hud_number_text(true, buf);
    text.draw(x + label_w + cw, y, buf, TextColor{ 1.f, 1.f, 1.f, 0.95f });

    const int row2 = y + ch;
    text.draw(x, row2, sp_name, dim);
    paint.gauge(RectPx{ x + label_w, row2 + (ch / 4), w - label_w, bar_h }, ratio(frame.hud.sp, frame.hud.sp_max),
        PaintColor{ 0.30f, 0.45f, 0.90f, 0.92f }, back);
    std::snprintf(buf, sizeof(buf), "%d/%d", frame.hud.sp, frame.hud.sp_max);
    ui_probe().hud_number_text(false, buf);
    text.draw(x + label_w + cw, row2, buf, TextColor{ 1.f, 1.f, 1.f, 0.95f });
}

//! 円を扇形の三角で埋める。中心と半径は設計の単位（`OrbMap` で画素へ直す）。
void fill_disc(UiPaint &paint, const OrbMap &m, double cx, double cy, double r, const PaintColor &color)
{
    constexpr int kSegments = 40;
    constexpr double kTwoPi = 6.283185307179586;
    const float x0 = static_cast<float>(m.x(cx));
    const float y0 = static_cast<float>(m.y(cy));
    for (int i = 0; i < kSegments; ++i) {
        const double a0 = kTwoPi * static_cast<double>(i) / kSegments;
        const double a1 = kTwoPi * static_cast<double>(i + 1) / kSegments;
        paint.triangle(x0, y0, static_cast<float>(m.x(cx + (r * std::cos(a0)))), static_cast<float>(m.y(cy + (r * std::sin(a0)))),
            static_cast<float>(m.x(cx + (r * std::cos(a1)))), static_cast<float>(m.y(cy + (r * std::sin(a1)))), color);
    }
}

//! 向きのある細長い四角（目盛り 1 本）。中心 (cx,cy)・長さ `len`（半径の向き）・幅 `wid`。単位は設計の単位。
void fill_tick(UiPaint &paint, const OrbMap &m, double cx, double cy, double angle, double len, double wid, const PaintColor &color)
{
    const double ux = std::cos(angle);
    const double uy = std::sin(angle);
    const double vx = -uy;
    const double vy = ux;
    const auto corner = [&](double a, double b, float &px, float &py) {
        px = static_cast<float>(m.x(cx + (ux * a * len * 0.5) + (vx * b * wid * 0.5)));
        py = static_cast<float>(m.y(cy + (uy * a * len * 0.5) + (vy * b * wid * 0.5)));
    };
    float p[4][2]{};
    corner(-1.0, -1.0, p[0][0], p[0][1]);
    corner(1.0, -1.0, p[1][0], p[1][1]);
    corner(1.0, 1.0, p[2][0], p[2][1]);
    corner(-1.0, 1.0, p[3][0], p[3][1]);
    paint.triangle(p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1], color);
    paint.triangle(p[0][0], p[0][1], p[2][0], p[2][1], p[3][0], p[3][1], color);
}

//! 玉の中央の飾りに、終末の太陽を重ねる（`days_left` は 0〜14。0 は終末）。
void draw_doomsday_sun(UiPaint &paint, const OrbMap &m, const OrbEmblem &emblem, int days_left)
{
    const DoomsdaySunLook look = doomsday_sun_look(days_left);
    const double ex = emblem.x;
    const double ey = emblem.y;
    const double er = emblem.r;
    //! 絵の中の目盛りの輪（12 本）を隠す暗い下地。14 本の目盛りと太陽は下地の上へ描き直す。
    fill_disc(paint, m, ex, ey, er * 1.9, PaintColor{ 0.07f, 0.06f, 0.07f, 1.f });
    //! 光のにじみ（外から内へ、だんだん濃く）。
    static const double kGlowRadius[4] = { 1.55, 1.38, 1.22, 1.08 };
    static const float kGlowAlpha[4] = { 0.24f, 0.36f, 0.50f, 0.70f };
    for (int i = 0; i < 4; ++i) {
        PaintColor glow = look.glow;
        glow.a *= kGlowAlpha[i];
        fill_disc(paint, m, ex, ey, er * kGlowRadius[i], glow);
    }
    fill_disc(paint, m, ex, ey, er, look.disc);
    //! 14 の目盛り。真上から時計回りに並べ、**先頭から残りの日数ぶん**を明るくする。
    for (int i = 0; i < kDoomsdayTicks; ++i) {
        const double angle = (-1.5707963267948966) + (6.283185307179586 * static_cast<double>(i) / kDoomsdayTicks);
        const bool lit = i < look.lit_ticks;
        fill_tick(paint, m, ex + (std::cos(angle) * er * 1.62), ey + (std::sin(angle) * er * 1.62), angle, er * 0.30, er * 0.13,
            lit ? look.tick_lit : look.tick_dim);
    }
}

//! 玉 1 つぶんの字（数字の帯と名札）。
void draw_orb_texts(TextOverlay &text, const OrbMap &m, double cx, const std::string &label, int now, int max,
    const TextColor &label_color, bool is_hp)
{
    const int ch = text.cell_h();
    const int cx_px = static_cast<int>(std::lround(m.x(cx)));
    //! 名札は**下端をとげの先の上**にそろえる（大きい字でもとげに重ならない）。位置はコアの配置（`label_bottom_y`）。
    const OrbProfile &prof = orb_profile();
    const int label_y = static_cast<int>(std::lround(m.y(prof.label_bottom_y))) - text.cell_h_sized(kOrbLabelScale);
    draw_centered_outlined(text, cx_px, label_y, label, label_color, 0, kOrbLabelScale);

    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%d / %d", now, max);
    std::string number = buf;
    //! 帯（玉の幅の 1.15 倍）に入らなければ、空白を詰める。
    const int band_w = static_cast<int>(std::lround(prof.r * 2.0 * 1.15 * m.sx));
    if (text.measure(number) > band_w) {
        std::snprintf(buf, sizeof(buf), "%d/%d", now, max);
        number = buf;
    }
    ui_probe().hud_number_text(is_hp, number); //!< 検査用の問い合わせ：玉に出した数字の字
    const int number_y = static_cast<int>(std::lround(m.y(prof.number_y))) - (ch / 2);
    draw_centered_outlined(text, cx_px, number_y, number, TextColor{ 0.97f, 0.95f, 0.90f, 1.f }, 0);
}

//! 流れるアイコンの描き方（整数の矩形に足す横の端数・見えている枠・薄さ）。
struct StatusDrawGeometry {
    float frac_x{ 0.f };
    float clip_x0{ -1.0e9f };
    float clip_x1{ 1.0e9f };
    float alpha{ 1.f };

    static StatusDrawGeometry of(const StatusIconPlacement &item)
    {
        StatusDrawGeometry g;
        if (item.rotating) {
            g.frac_x = item.frac_x;
            g.clip_x0 = static_cast<float>(item.clip.x);
            g.clip_x1 = static_cast<float>(item.clip.x + item.clip.w);
            g.alpha = item.alpha;
        }
        return g;
    }
};

void draw_icon_cell(const OrbHudImages &images, const RectPx &dst, int icon_id, const StatusDrawGeometry &geo = {})
{
    const UiImage &sheet = *images.status_icons;
    const int cell = std::max(1, sheet.width() / kIconSheetColumns);
    const int col = icon_id % kIconSheetColumns;
    const int row = icon_id / kIconSheetColumns;
    if (((row + 1) * cell) > sheet.height()) {
        return; // シートに無い番号（表が新しく、シートが古い）。描かない
    }
    //! 半画素だけ内側から取る（縮めたとき、隣のセルが縁に混ざらない）。
    const float tw = static_cast<float>(sheet.width());
    const float th = static_cast<float>(sheet.height());
    const float u0 = (static_cast<float>(col * cell) + 0.5f) / tw;
    const float v0 = (static_cast<float>(row * cell) + 0.5f) / th;
    const float u1 = (static_cast<float>((col + 1) * cell) - 0.5f) / tw;
    const float v1 = (static_cast<float>((row + 1) * cell) - 0.5f) / th;
    //! 枠の外へはみ出す分は、貼り先と UV を同じ割合で詰めて切る。
    const float x0 = static_cast<float>(dst.x) + geo.frac_x;
    const float x1 = x0 + static_cast<float>(dst.w);
    const float nx0 = std::max(x0, geo.clip_x0);
    const float nx1 = std::min(x1, geo.clip_x1);
    if (nx1 <= nx0) {
        return;
    }
    const float span = x1 - x0;
    const float cu0 = u0 + ((u1 - u0) * ((nx0 - x0) / span));
    const float cu1 = u0 + ((u1 - u0) * ((nx1 - x0) / span));
    images.painter->draw_region_f(sheet, nx0, static_cast<float>(dst.y), nx1, static_cast<float>(dst.y + dst.h), cu0, v0,
        cu1, v1, geo.alpha);
}

//! 下敷きの帯（枠で切る・薄くする）。
void draw_band(UiPaint &paint, const RectPx &band, PaintColor color, const StatusDrawGeometry &geo)
{
    const float x0 = std::max(static_cast<float>(band.x) + geo.frac_x, geo.clip_x0);
    const float x1 = std::min(static_cast<float>(band.x + band.w) + geo.frac_x, geo.clip_x1);
    color.a *= geo.alpha;
    paint.rect_f(x0, static_cast<float>(band.y), x1, static_cast<float>(band.y + band.h), color);
}

//! 帯の中に字を中央ぞろえで描く（字は `fit` の大きさ。高さは帯の中央）。
void draw_fitted_text(TextOverlay &text, const RectPx &band, const std::string &s, const FittedText &fit,
    TextColor color, const StatusDrawGeometry &geo = {})
{
    const int w = std::min(fit.width, band.w);
    const int h = text.cell_h_sized(fit.scale);
    color.a *= geo.alpha;
    text.draw_sized_clipped(static_cast<float>(band.x) + (static_cast<float>(band.w - w) * 0.5f) + geo.frac_x,
        band.y + ((band.h - h) / 2), s, color, fit.scale, band.w, geo.clip_x0, geo.clip_x1, fit.x_scale);
}

//! 名前を帯の中に描く。2 行なら 1 行ずつ `line_h` の高さの段に、各行を中央ぞろえで。
void draw_name_lines(TextOverlay &text, const RectPx &band, const std::string &label, const FittedText &fit, int line_h,
    TextColor color, const StatusDrawGeometry &geo = {})
{
    for (int k = 0; k < std::max(1, fit.lines); ++k) {
        const std::string_view line = status_name_line(label, fit, k);
        if (line.empty()) {
            continue;
        }
        FittedText one = fit;
        one.width = (fit.lines >= 2) ? fit.line_width[k] : fit.width;
        draw_fitted_text(text, RectPx{ band.x, band.y + (k * line_h), band.w, line_h }, std::string(line), one, color, geo);
    }
}

PaintColor status_band_color(bool bad)
{
    return bad ? PaintColor{ kStatusBandBadR, kStatusBandBadG, kStatusBandBadB, kStatusBandBadA }
               : PaintColor{ 0.f, 0.f, 0.f, 0.58f };
}

void draw_status_icons(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    const OrbHudImages &images, OrbHudAnimation &anim, std::uint32_t now_ms)
{
    const TextColor name_color{ 0.96f, 0.96f, 0.96f, 1.f };
    //! 巡回の経過時間（その側の並びが変わってからの実時計）。変わったら先頭から始め直す。
    double ring_ms[2]{ 0.0, 0.0 };
    for (int side = 0; side < 2; ++side) {
        const std::uint64_t signature = status_ring_signature(frame, side == 0);
        if (!anim.ring_initialized[side] || (anim.ring_signature[side] != signature) || (now_ms < anim.ring_origin_ms[side])) {
            anim.ring_initialized[side] = true;
            anim.ring_signature[side] = signature;
            anim.ring_origin_ms[side] = now_ms;
        }
        ring_ms[side] = static_cast<double>(now_ms - anim.ring_origin_ms[side]);
    }
    const StatusIconLayout placed = layout_status_icons(text, layout, frame, ring_ms[0], ring_ms[1]);
    for (const StatusIconPlacement &item : placed.items) {
        const StatusEffectView &effect = frame.status_effects[static_cast<std::size_t>(item.effect_index)];
        ui_probe().hud_icon(ProbeIcon{ effect.key, effect.label_utf8, effect.amount_utf8, item.icon_id, false,
            effect.kind == StatusEffectKind::Bad });
        const StatusDrawGeometry geo = StatusDrawGeometry::of(item);
        const PaintColor band = status_band_color(effect.kind == StatusEffectKind::Bad);
        draw_icon_cell(images, item.icon, item.icon_id, geo);
        if (!effect.label_utf8.empty()) {
            draw_band(paint, item.name_band, band, geo);
            draw_name_lines(text, item.name_band, effect.label_utf8, item.name, placed.name_line_h, name_color, geo);
        }
        if (!item.amount_band.empty()) {
            draw_band(paint, item.amount_band, band, geo);
            draw_fitted_text(text, item.amount_band, effect.amount_utf8, item.amount, TextColor{ 1.f, 0.90f, 0.50f, 1.f }, geo);
        }
    }
    //! 常時の列（状態列の横の縦並び）。効果量は出さない。
    const PermanentIconLayout permanent = layout_permanent_icons(text, layout, frame);
    for (const PermanentIconPlacement &item : permanent.items) {
        const StatusEffectView &effect = frame.status_effects[static_cast<std::size_t>(item.effect_index)];
        ui_probe().hud_icon(ProbeIcon{ effect.key, effect.label_utf8, effect.amount_utf8, item.icon_id, true, item.bad });
        draw_icon_cell(images, item.icon, item.icon_id);
        if (!effect.label_utf8.empty()) {
            paint.rect(item.name_band, status_band_color(item.bad));
            draw_name_lines(text, item.name_band, effect.label_utf8, item.name, permanent.name_line_h, name_color);
        }
    }
}

} // namespace

bool OrbHudImages::has_orb() const
{
    const UiImage *const layers[] = { this->back, this->glass, this->liquid_hp, this->liquid_mp, this->glass_top,
        this->front };
    if (this->painter == nullptr) {
        return false;
    }
    for (const UiImage *const layer : layers) {
        if ((layer == nullptr) || !layer->valid()) {
            return false;
        }
    }
    return true;
}

bool OrbHudImages::has_icons() const
{
    return (this->painter != nullptr) && (this->status_icons != nullptr) && this->status_icons->valid();
}

bool OrbHudImages::has_weather_icons() const
{
    return (this->painter != nullptr) && (this->weather_icons != nullptr) && this->weather_icons->valid();
}

void OrbHudAnimation::reset()
{
    *this = OrbHudAnimation{};
}

void orb_hud_update(OrbHudAnimation &anim, const OrbHudParams &params, float hp_ratio, float mp_ratio,
    std::uint32_t now_ms)
{
    bool snap = !anim.initialized;
    float dt = 0.f;
    if (anim.initialized && (now_ms >= anim.last_ms)) {
        const std::uint32_t gap = now_ms - anim.last_ms;
        if (gap >= 1000u) {
            snap = true; // メニューなどで描かれていなかった間。古い動きを引きずらない
        } else {
            dt = static_cast<float>(gap) * 0.001f;
        }
    } else if (anim.initialized) {
        snap = true; // 時計が戻った（検査の撮り直しなど）
    }
    anim.initialized = true;
    anim.last_ms = now_ms;
    anim.wave_clock += static_cast<double>(dt) * static_cast<double>(params.wave_speed);
    update_gauge(anim.hp, params, hp_ratio, dt, anim.rng, snap);
    update_gauge(anim.mp, params, mp_ratio, dt, anim.rng, snap);
}

const char *hp_label_of(const GameFrame &frame)
{
    return frame.hud.hp_label.empty() ? "HP" : frame.hud.hp_label.c_str();
}

const char *sp_label_of(const GameFrame &frame)
{
    return frame.hud.sp_label.empty() ? "SP" : frame.hud.sp_label.c_str();
}

namespace {

float lerp_f(float a, float b, float t)
{
    return a + ((b - a) * t);
}

PaintColor lerp_color(const PaintColor &a, const PaintColor &b, float t)
{
    return PaintColor{ lerp_f(a.r, b.r, t), lerp_f(a.g, b.g, t), lerp_f(a.b, b.b, t), lerp_f(a.a, b.a, t) };
}

} // namespace

DoomsdaySunLook doomsday_sun_look(int days_left)
{
    DoomsdaySunLook look;
    const int left = std::clamp(days_left, 0, kDoomsdayTicks);
    look.doomsday = (left == 0);
    look.lit_ticks = left;
    look.tick_lit = PaintColor{ 1.f, 0.82f, 0.30f, 1.f };
    look.tick_dim = PaintColor{ 0.26f, 0.16f, 0.12f, 0.95f };
    if (look.doomsday) {
        //! 終末（15 日目以降）: 真っ黒な太陽に、赤い光。
        look.disc = PaintColor{ 0.02f, 0.02f, 0.02f, 1.f };
        look.glow = PaintColor{ 1.f, 0.10f, 0.06f, 0.85f };
        return look;
    }
    //! 1 日目（残り 14）が 0、14 日目（残り 1）が 1。
    const float t = static_cast<float>(kDoomsdayTicks - left) / static_cast<float>(kDoomsdayTicks - 1);
    static const PaintColor kStops[4] = {
        PaintColor{ 1.00f, 0.86f, 0.22f, 1.f }, //!< 黄色い太陽
        PaintColor{ 1.00f, 0.60f, 0.12f, 1.f }, //!< 橙
        PaintColor{ 0.55f, 0.20f, 0.06f, 1.f }, //!< 燃えさしの赤
        PaintColor{ 0.045f, 0.035f, 0.035f, 1.f }, //!< ほぼ黒い日食
    };
    static const float kAt[4] = { 0.f, 0.35f, 0.70f, 1.f };
    look.disc = kStops[3];
    for (int i = 0; i < 3; ++i) {
        if (t <= kAt[i + 1]) {
            look.disc = lerp_color(kStops[i], kStops[i + 1], (t - kAt[i]) / (kAt[i + 1] - kAt[i]));
            break;
        }
    }
    //! 光は黄白から橙へ。日食（残り 1）でも輪の光は残る。
    look.glow = lerp_color(PaintColor{ 1.f, 0.85f, 0.40f, 0.55f }, PaintColor{ 1.f, 0.50f, 0.15f, 0.50f }, t);
    return look;
}

namespace {

//! 大文字小文字を区別せずに 2 つの名前が等しいか。
bool equals_ignore_case(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

//! `json` から `"key"` の数を読む（平たい JSON 専用。キーは引用符つきで照合するので `r` が `cx_hp` に当たらない）。
bool read_json_number(std::string_view json, const char *key, double &out)
{
    const std::string needle = std::string("\"") + key + "\"";
    std::size_t at = json.find(needle);
    if (at == std::string_view::npos) {
        return false;
    }
    at += needle.size();
    while ((at < json.size()) && ((json[at] == ' ') || (json[at] == '\t') || (json[at] == '\r') || (json[at] == '\n'))) {
        ++at;
    }
    if ((at >= json.size()) || (json[at] != ':')) {
        return false;
    }
    ++at;
    const std::string tail(json.substr(at, 32));
    char *end = nullptr;
    const double v = std::strtod(tail.c_str(), &end);
    if (end == tail.c_str()) {
        return false;
    }
    out = v;
    return true;
}

//! 素材を丸ごと読む（`SDL_RWFromFile`。Android では APK の assets から読む＝画像と同じ道）。読めなければ空。
std::string read_asset_text(const std::string &path)
{
    SDL_RWops *const rw = SDL_RWFromFile(path.c_str(), "rb");
    if (rw == nullptr) {
        return std::string();
    }
    std::string text;
    char buffer[512];
    for (;;) {
        const std::size_t n = SDL_RWread(rw, buffer, 1, sizeof(buffer));
        if (n == 0) {
            break;
        }
        text.append(buffer, n);
        if (text.size() > (64u * 1024u)) {
            break; // 平たい設定ファイルとしては大きすぎる
        }
    }
    SDL_RWclose(rw);
    return text;
}

} // namespace

std::string orb_asset_key_for_core(std::string_view core_name)
{
    struct Entry {
        const char *core_name;
        const char *key;
    };
    static const Entry kTable[] = {
        { "tangband", "tangband" },
        { "gensoband", "gensoband" },
        //! TOband（2026-10-17。遊ぶ人が選んだタロットの意匠。`tools/hud/gen_core_orbs.py`）
        { "toband", "toband" },
        { "silq", "silq" },
        { "frox", "frox" },
        { "angband", "angband" },
        { "Umoria", "umoria" },
        { "Rogue 3.6", "rogue36" },
        { "Rogue 5.4", "rogue54" },
        { "RogueClone2", "rogue2" },
    };
    for (const Entry &entry : kTable) {
        if (equals_ignore_case(core_name, entry.core_name)) {
            return entry.key;
        }
    }
    return std::string();
}

std::string orb_asset_dir(std::string_view asset_key)
{
    std::string dir = kOrbAssetRoot;
    if (!asset_key.empty()) {
        dir += "/";
        dir += asset_key;
    }
    return dir;
}

std::string orb_layer_path(std::string_view asset_key, std::string_view layer)
{
    return orb_asset_dir(asset_key) + "/" + std::string(layer) + ".png";
}

bool parse_orb_layout(std::string_view json, OrbProfile &out, std::string &err)
{
    OrbProfile p = out;
    double v = 0.0;
    //! 配置の 4 項目は必須（無いものを既定で埋めると、別の意匠の絵に変愚の寸法が当たってしまう）。
    struct Required {
        const char *key;
        double *dst;
    };
    const Required required[] = { { "cx_hp", &p.hp_cx }, { "cx_mp", &p.mp_cx }, { "cy", &p.cy }, { "r", &p.r } };
    for (const Required &item : required) {
        if (!read_json_number(json, item.key, v)) {
            err = std::string("\"") + item.key + "\" がありません";
            return false;
        }
        *item.dst = v;
    }
    if (read_json_number(json, "number_y", v)) {
        p.number_y = v;
    }
    if (read_json_number(json, "label_bottom_y", v)) {
        p.label_bottom_y = v;
    }
    if (read_json_number(json, "art_bottom_y", v)) {
        p.art_bottom_y = static_cast<int>(std::lround(v));
    }
    double ex = 0.0;
    double ey = 0.0;
    double er = 0.0;
    if (read_json_number(json, "emblem_x", ex) && read_json_number(json, "emblem_y", ey) && read_json_number(json, "emblem_r", er)) {
        p.emblem = OrbEmblem{ true, ex, ey, er };
    }
    //! 設計の範囲（760x250）に収まるか。玉が矩形からはみ出す値は、描くと素材の外を指す。
    const double w = static_cast<double>(kOrbDesignW);
    const double h = static_cast<double>(kOrbDesignH);
    const bool in_range = (p.r >= 10.0) && ((p.hp_cx - p.r) >= 0.0) && ((p.hp_cx + p.r) <= w) && ((p.mp_cx - p.r) >= 0.0)
        && ((p.mp_cx + p.r) <= w) && (p.hp_cx < p.mp_cx) && ((p.cy - p.r) >= 0.0) && ((p.cy + p.r) <= h)
        && (p.number_y > 0.0) && (p.number_y <= h) && (p.label_bottom_y > 0.0) && (p.label_bottom_y <= h)
        && (p.art_bottom_y > 0) && (p.art_bottom_y <= kOrbDesignH)
        && (!p.emblem.present || ((p.emblem.r > 0.0) && ((p.emblem.x - p.emblem.r) >= 0.0) && ((p.emblem.x + p.emblem.r) <= w)
                                    && ((p.emblem.y - p.emblem.r) >= 0.0) && ((p.emblem.y + p.emblem.r) <= h)));
    if (!in_range) {
        err = "値が設計の範囲（760x250）に収まっていません";
        return false;
    }
    out = p;
    return true;
}

OrbProfile load_orb_profile(std::string_view core_name, std::string &note)
{
    note.clear();
    OrbProfile profile; //!< 既定 = 変愚
    const std::string key = orb_asset_key_for_core(core_name);
    if (key.empty()) {
        return profile;
    }
    const std::string path = orb_asset_dir(key) + "/orb_layout.json";
    const std::string text = read_asset_text(path);
    if (text.empty()) {
        note = path + " を読めませんでした";
        return profile;
    }
    std::string err;
    OrbProfile loaded;
    if (!parse_orb_layout(text, loaded, err)) {
        note = path + ": " + err;
        return profile;
    }
    loaded.asset_key = key;
    return loaded;
}

int status_icon_index(std::string_view key)
{
    for (const IconEntry &entry : kIconTable) {
        if (key == entry.name) {
            return entry.index;
        }
    }
    return 0;
}

bool status_icon_known(std::string_view key)
{
    for (const IconEntry &entry : kIconTable) {
        if (key == entry.name) {
            return true;
        }
    }
    return false;
}

int status_icon_size(const RectPx &orb)
{
    return std::clamp(static_cast<int>(std::lround(static_cast<double>(orb.h) * 0.24)), 24, 64);
}

namespace {

//! 表示上の幅（全角 1 文字 = 2、半角・半角カナ = 1）。
int display_columns(std::string_view utf8)
{
    int columns = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp = c;
        std::size_t len = 1;
        if (c >= 0xF0u) {
            len = 4;
            cp = c & 0x07u;
        } else if (c >= 0xE0u) {
            len = 3;
            cp = c & 0x0Fu;
        } else if (c >= 0xC0u) {
            len = 2;
            cp = c & 0x1Fu;
        }
        for (std::size_t k = 1; (k < len) && ((i + k) < utf8.size()); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
        }
        i += len;
        columns += ((cp < 0x80u) || ((cp >= 0xFF61u) && (cp <= 0xFF9Fu))) ? 1 : 2;
    }
    return columns;
}

float scale_of_px(const TextOverlay &text, int px)
{
    return static_cast<float>(px) / static_cast<float>(text.base_px());
}

FittedText fit_with_reference(TextOverlay &text, std::string_view utf8, int target_w, int ref_px)
{
    FittedText out;
    if (utf8.empty()) {
        return out;
    }
    if (text.layout_scale() != 1.f) {
        out.width = text.measure(utf8);
        return out;
    }
    out.scale = scale_of_px(text, ref_px);
    out.width = text.measure_sized(utf8, out.scale);
    if (out.width <= target_w) {
        return out;
    }
    if (display_columns(utf8) <= kStatusTextReferenceColumns) {
        //! 全角 4 文字ぶん以内の名前は基準の大きさのまま。それでも収まらないとき（半角 8 字が全角 4 字より広いフォント。
        //! Linux・Android）は、字の高さを保って横だけ縮め、アイコンの幅に収める（幅を広げない）。
        out.x_scale = static_cast<float>(target_w) / static_cast<float>(out.width);
        out.width = target_w;
        return out;
    }
    const int base = text.base_px();
    const int min_px = std::max(6, static_cast<int>(std::ceil((static_cast<double>(base) * static_cast<double>(kStatusTextMinScale)) - 1e-6)));
    const int floor_px = std::min(ref_px, min_px);
    for (int px = ref_px - 1; px >= floor_px; --px) {
        out.scale = scale_of_px(text, px);
        out.width = text.measure_sized(utf8, out.scale);
        if (out.width <= target_w) {
            return out;
        }
    }
    out.scale = scale_of_px(text, floor_px);
    out.width = text.measure_sized(utf8, out.scale);
    out.overflow = true; // 下限の大きさのまま
    return out;
}

//! UTF-8 を 1 字ぶん解く（壊れた並びは 1 バイトずつ進める）。
std::uint32_t decode_one(std::string_view utf8, std::size_t &i)
{
    const unsigned char c = static_cast<unsigned char>(utf8[i]);
    std::uint32_t cp = c;
    std::size_t len = 1;
    if (c >= 0xF0u) {
        len = 4;
        cp = c & 0x07u;
    } else if (c >= 0xE0u) {
        len = 3;
        cp = c & 0x0Fu;
    } else if (c >= 0xC0u) {
        len = 2;
        cp = c & 0x1Fu;
    }
    for (std::size_t k = 1; (k < len) && ((i + k) < utf8.size()); ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
    }
    i += len;
    return cp;
}

enum class CharClass { Hiragana, Katakana, Kanji, Other };

CharClass char_class_of(std::uint32_t cp)
{
    if ((cp >= 0x3040u) && (cp <= 0x309Fu)) {
        return CharClass::Hiragana;
    }
    if (((cp >= 0x30A0u) && (cp <= 0x30FFu)) || ((cp >= 0x31F0u) && (cp <= 0x31FFu))) {
        return CharClass::Katakana; // 長音記号 U+30FC もここ
    }
    if (((cp >= 0x4E00u) && (cp <= 0x9FFFu)) || ((cp >= 0x3400u) && (cp <= 0x4DBFu))) {
        return CharClass::Kanji;
    }
    return CharClass::Other;
}

constexpr std::uint32_t kCpNo = 0x306Eu; //!< の
constexpr std::uint32_t kCpFullwidthParen = 0xFF08u; //!< （

//! 日本語の名前で、隣り合う 2 字 a・b の間を折ってよいか。
bool may_break_between(std::uint32_t a, std::uint32_t b)
{
    if ((a == kCpNo) || (b == kCpFullwidthParen)) {
        return true;
    }
    const CharClass ca = char_class_of(a);
    const CharClass cb = char_class_of(b);
    if ((ca == CharClass::Other) || (cb == CharClass::Other)) {
        return false; // 記号・数字・英字の前後
    }
    if ((ca == CharClass::Katakana) && (cb == CharClass::Katakana)) {
        return false; // カタカナの語の途中
    }
    if ((ca == CharClass::Hiragana) && (cb == CharClass::Hiragana)) {
        return false; // ひらがなの続き
    }
    if ((ca == CharClass::Kanji) && (cb == CharClass::Hiragana)) {
        return false; // 送りがなの前
    }
    return true;
}

//! 2 行に折った名前を、字の高さ `px` で測る。折る位置が複数あれば、長いほうの行がいちばん短い位置（同じなら左）。
FittedText best_two_lines(TextOverlay &text, std::string_view utf8, const std::vector<StatusNameBreak> &breaks, int px)
{
    FittedText best;
    best.scale = scale_of_px(text, px);
    best.lines = 2;
    bool first = true;
    for (const StatusNameBreak &b : breaks) {
        const int w0 = text.measure_sized(utf8.substr(0, b.first_end), best.scale);
        const int w1 = text.measure_sized(utf8.substr(b.second_begin), best.scale);
        const int widest = std::max(w0, w1);
        if (first || (widest < best.width)) {
            first = false;
            best.width = widest;
            best.break_end = b.first_end;
            best.break_begin = b.second_begin;
            best.line_width[0] = w0;
            best.line_width[1] = w1;
        }
    }
    return best;
}

FittedText fit_name_with_reference(TextOverlay &text, std::string_view utf8, int target_w, int ref_px)
{
    FittedText one = fit_with_reference(text, utf8, target_w, ref_px);
    if (utf8.empty() || (text.layout_scale() != 1.f)) {
        one.line_width[0] = one.width;
        return one;
    }
    //! 1. 基準の大きさで 1 行に収まる（全角 4 文字以内も含む）なら 1 行。
    const float ref_scale = scale_of_px(text, ref_px);
    if ((text.measure_sized(utf8, ref_scale) <= target_w) || (display_columns(utf8) <= kStatusTextReferenceColumns)) {
        one.line_width[0] = one.width;
        return one;
    }
    //! 5. 折る位置が無い名前は、1 行のまま縮める。
    const std::vector<StatusNameBreak> breaks = status_name_break_points(utf8);
    if (breaks.empty()) {
        one.line_width[0] = one.width;
        return one;
    }
    //! 2〜3. 基準の大きさの 2 行から、1 画素ずつ縮めて、両方の行が収まった最初の大きさ。
    const int base = text.base_px();
    const int min_px = std::max(6, static_cast<int>(std::ceil((static_cast<double>(base) * static_cast<double>(kStatusTextMinScale)) - 1e-6)));
    const int floor_px = std::min(ref_px, min_px);
    for (int px = ref_px; px >= floor_px; --px) {
        FittedText two = best_two_lines(text, utf8, breaks, px);
        if (two.width <= target_w) {
            return two;
        }
    }
    //! 4. 下限でも収まらない。下限の大きさの 2 行のまま、幅を広げてもらう。
    FittedText floor_case = best_two_lines(text, utf8, breaks, floor_px);
    floor_case.overflow = true;
    return floor_case;
}

} // namespace

std::vector<StatusNameBreak> status_name_break_points(std::string_view utf8)
{
    std::vector<StatusNameBreak> out;
    bool ascii = true;
    for (const char c : utf8) {
        ascii = ascii && (static_cast<unsigned char>(c) < 0x80u);
    }
    if (ascii) {
        //! 英語: 空白の位置だけ。連続する空白はまとめて行に含めない。
        std::size_t i = 0;
        while (i < utf8.size()) {
            if (utf8[i] != ' ') {
                ++i;
                continue;
            }
            std::size_t j = i;
            while ((j < utf8.size()) && (utf8[j] == ' ')) {
                ++j;
            }
            if ((i > 0) && (j < utf8.size())) {
                out.push_back(StatusNameBreak{ i, j });
            }
            i = j;
        }
        return out;
    }
    std::size_t i = 0;
    std::uint32_t prev = 0;
    bool has_prev = false;
    while (i < utf8.size()) {
        const std::size_t start = i;
        const std::uint32_t cp = decode_one(utf8, i);
        if (has_prev && may_break_between(prev, cp)) {
            out.push_back(StatusNameBreak{ start, start });
        }
        prev = cp;
        has_prev = true;
    }
    return out;
}

int status_text_reference_px(TextOverlay &text, int icon_px)
{
    const int base = text.base_px();
    if (text.layout_scale() != 1.f) {
        return base;
    }
    const int lowest = text.sized_px(0.3f);
    for (int px = base; px > lowest; --px) {
        if (text.measure_sized(kStatusTextReferenceString, scale_of_px(text, px)) <= icon_px) {
            return px;
        }
    }
    return lowest;
}

FittedText fit_status_text(TextOverlay &text, std::string_view utf8, int target_w)
{
    return fit_with_reference(text, utf8, target_w, status_text_reference_px(text, target_w));
}

FittedText fit_status_name(TextOverlay &text, std::string_view utf8, int target_w)
{
    return fit_name_with_reference(text, utf8, target_w, status_text_reference_px(text, target_w));
}

std::string_view status_name_line(std::string_view utf8, const FittedText &fit, int index)
{
    if (fit.lines < 2) {
        return (index == 0) ? utf8 : std::string_view{};
    }
    return (index == 0) ? utf8.substr(0, fit.break_end) : utf8.substr(fit.break_begin);
}

/*!
 * @brief 玉の左の段（良い・待っている・特殊のアイコン）の、アイコンの上端の y。
 * @details 玉の横（玉の上から 42%）が基本。**メッセージの帯と名前の行（`hud`）に重なるなら、その上へ逃がす**
 * （決めたこと 2026-10-09「メッセージ枠を左側から2/5くらいにして、一時効果アイコンはその上に置いてみて」）。
 * 玉は地図の下縁にあるので、玉の横の高さのままだと左の帯と名前の行の上に重なる。右の段もこの高さにそろえる。
 * 常時の効果の列（`layout_permanent_icons`）も、この段の上で止めるので同じ関数を使う。
 */
static int left_row_icon_y(const UiLayout &layout, int icon_y_orb, int ch, int cell_h, int gap, int left_limit)
{
    const RectPx &orb = layout.orb;
    int icon_y = icon_y_orb;
    const RectPx *const obstacles[] = { &layout.message_bar, &layout.hud };
    for (int pass = 0; pass < 3; ++pass) {
        const RectPx row{ left_limit, icon_y - ch, std::max(1, orb.x - left_limit), cell_h };
        int ceiling = row.y + row.h;
        for (const RectPx *r : obstacles) {
            if (!r->empty() && (r->x < (row.x + row.w)) && ((r->x + r->w) > row.x) && (r->y < (row.y + row.h)) && ((r->y + r->h) > row.y)) {
                ceiling = std::min(ceiling, r->y - gap);
            }
        }
        if (ceiling >= (row.y + row.h)) {
            break;
        }
        icon_y = ceiling - cell_h + ch;
    }
    return icon_y;
}

//! 玉の横の段の左の端（地図の左＋1 マス。状態列が左にあるときはその右）。
static int status_row_left_limit(const UiLayout &layout)
{
    int left_limit = layout.scene.x + layout.cell_w;
    if (!layout.status_col.empty() && (layout.status_col.x <= layout.scene.x)) {
        left_limit = std::max(left_limit, layout.status_col.x + layout.status_col.w + layout.cell_w);
    }
    return left_limit;
}

int status_name_line_height(TextOverlay &text, int icon_px)
{
    return text.cell_h_sized(scale_of_px(text, status_text_reference_px(text, icon_px)));
}

int permanent_icon_size(const RectPx &orb)
{
    return std::max(kPermanentIconMinPx,
        static_cast<int>(std::lround(static_cast<double>(status_icon_size(orb)) * kPermanentIconRatio)));
}

PermanentIconLayout layout_permanent_icons(TextOverlay &text, const UiLayout &layout, const GameFrame &frame)
{
    PermanentIconLayout out;
    const RectPx &orb = layout.orb;
    if (orb.empty() || frame.status_effects.empty()) {
        return out;
    }
    //! 並び: 良い効果 → 呪い。中はフレームの順。
    struct Order {
        std::size_t index;
        bool bad;
    };
    std::vector<Order> order;
    for (const StatusEffectKind kind : { StatusEffectKind::Permanent, StatusEffectKind::PermanentBad }) {
        for (std::size_t i = 0; i < frame.status_effects.size(); ++i) {
            if (frame.status_effects[i].kind == kind) {
                order.push_back(Order{ i, kind == StatusEffectKind::PermanentBad });
            }
        }
    }
    if (order.empty()) {
        return out;
    }
    out.icon_px = permanent_icon_size(orb);
    const int icon_px = out.icon_px;
    const int ch = text.cell_h();
    const int gap = std::max(4, icon_px / 8);
    const int name_line_h = status_name_line_height(text, icon_px);
    out.name_line_h = name_line_h;
    //! 1 個の高さ = アイコン + 名前の帯（実際の行数。1 個ずつ詰める）。
    const auto item_height = [&](const FittedText &name) { return icon_px + (std::max(1, name.lines) * name_line_h); };

    const RectPx &scene = layout.scene;
    const bool right = !layout.status_col.empty() && (layout.status_col.x > scene.x);
    out.grows_left = right;
    int start_x = 0; //!< 左置きは最初の列の左端、右置きは最初の列の右端
    if (right) {
        start_x = std::min(scene.x + scene.w, layout.status_col.x) - gap;
    } else if (!layout.status_col.empty()) {
        start_x = std::max(scene.x, layout.status_col.x + layout.status_col.w) + gap;
    } else {
        start_x = scene.x + layout.cell_w;
    }
    //! 玉の矩形（翼を含む）には入らない。
    const int x_limit = right ? (orb.x + orb.w + gap) : (orb.x - gap);

    int top = scene.y + gap;
    int bottom = scene.y + scene.h;
    {
        const int row0_icon_y = orb.y + static_cast<int>(std::lround(static_cast<double>(orb.h) * 0.42));
        bottom = std::min(bottom, row0_icon_y - ch - gap); //!< 玉の横のアイコンの 1 段目（効果量の帯）の上
        //! 左の段がメッセージの帯・名前の行の上へ逃げているときは、その段の上で止める（2026-10-09）。
        const int temp_icon_px = status_icon_size(orb);
        const int temp_gap = std::max(4, temp_icon_px / 8);
        const int temp_cell_h = ch + temp_icon_px + (2 * status_name_line_height(text, temp_icon_px));
        const int left_icon_y = left_row_icon_y(layout, row0_icon_y, ch, temp_cell_h, temp_gap, status_row_left_limit(layout));
        bottom = std::min(bottom, left_icon_y - ch - gap);
        for (const RectPx *rect : { &layout.message_bar, &layout.bottom_bar, &layout.prompt_bar }) {
            if (!rect->empty()) {
                bottom = std::min(bottom, rect->y - gap);
            }
        }
    }
    out.top = top;
    out.bottom = bottom;

    //! ミニマップを避けるときの縦の範囲（上寄りならその下から・下寄りならその上まで）。
    int mm_top = top;
    int mm_bottom = bottom;
    const RectPx &mm = layout.minimap;
    if (!mm.empty()) {
        if ((mm.y + (mm.h / 2)) < (scene.y + (scene.h / 2))) {
            mm_top = std::max(top, mm.y + mm.h + gap);
        } else {
            mm_bottom = std::min(bottom, mm.y - gap);
        }
    }

    const int name_ref_px = status_text_reference_px(text, icon_px);
    struct Slot {
        std::size_t index;
        bool bad;
        int y;
        FittedText name;
    };
    struct Column {
        std::vector<Slot> slots;
        int x{ 0 };
        int w{ 0 };
        int top{ 0 };
        int bottom{ 0 };
    };
    std::vector<Column> columns;
    std::size_t next = 0;
    bool bad_started = false;
    int used = 0;
    while (next < order.size()) {
        Column col;
        bool bad_after = bad_started;
        //! 状態列に近い側の列から決める。ミニマップと横が重なる列だけ、縦の範囲を狭める。
        //! 名前の行（`layout.hud`）に実際に重なった列も、その行の上で止めて組み直す（行の高いフォント〔Linux・Android の 8x24〕で、
        //! 分割配置の 1280x720 では、名前の行が列の縦の範囲に入って重なった。2026-10-22。重ならない列は従来のまま）。
        bool narrow_mm = false;
        bool narrow_hud = false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            col = Column{};
            bad_after = bad_started;
            col.top = narrow_mm ? mm_top : top;
            col.bottom = narrow_mm ? mm_bottom : bottom;
            if (narrow_hud) {
                col.bottom = std::min(col.bottom, layout.hud.y - gap);
            }
            int y = col.top;
            for (std::size_t i = next; i < order.size(); ++i) {
                const Order &o = order[i];
                int extra = 0;
                if (o.bad && !bad_after) {
                    extra = (y > col.top) ? (icon_px / 2) : 0; //!< 良い効果と呪いの間のすき間
                }
                const FittedText name = fit_name_with_reference(text, frame.status_effects[o.index].label_utf8, icon_px, name_ref_px);
                const int item_h = item_height(name);
                if ((y + extra + item_h) > col.bottom) {
                    break; // この列はここまで（次の列へ）
                }
                y += extra;
                col.slots.push_back(Slot{ o.index, o.bad, y, name });
                bad_after = bad_after || o.bad;
                y += item_h + gap;
            }
            col.w = icon_px + gap;
            for (const Slot &slot : col.slots) {
                col.w = std::max(col.w, std::max(icon_px, slot.name.width) + gap);
            }
            col.x = right ? (start_x - used - col.w) : (start_x + used);
            const bool hits_minimap = !mm.empty() && (col.x < (mm.x + mm.w + gap)) && ((col.x + col.w) > (mm.x - gap));
            bool hits_hud = false;
            if (!layout.hud.empty() && !narrow_hud && (layout.hud.y > col.top)) {
                for (const Slot &slot : col.slots) {
                    const RectPx cell{ col.x, slot.y, col.w, item_height(slot.name) };
                    hits_hud = hits_hud || ((cell.x < (layout.hud.x + layout.hud.w)) && ((cell.x + cell.w) > layout.hud.x)
                        && (cell.y < (layout.hud.y + layout.hud.h)) && ((cell.y + cell.h) > layout.hud.y));
                }
            }
            bool again = false;
            if (!narrow_mm && hits_minimap && ((mm_top != top) || (mm_bottom != bottom))) {
                narrow_mm = true;
                again = true; // ミニマップを避けた範囲で組み直す
            }
            if (hits_hud) {
                narrow_hud = true;
                again = true; // 名前の行の上で止めて組み直す
            }
            if (!again) {
                break;
            }
        }
        const bool inside = right ? (col.x >= x_limit) : ((col.x + col.w) <= x_limit);
        if (col.slots.empty() || !inside) {
            out.dropped += static_cast<int>(order.size() - next); // 残りは置けない（高さが足りない・横が足りない）
            break;
        }
        next += col.slots.size();
        bad_started = bad_after;
        used += col.w;
        columns.push_back(std::move(col));
    }
    out.columns = static_cast<int>(columns.size());
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    bool first = true;
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const Column &col = columns[c];
        out.column_top.push_back(col.top);
        out.column_bottom.push_back(col.bottom);
        for (const Slot &slot : col.slots) {
            PermanentIconPlacement p;
            p.effect_index = static_cast<int>(slot.index);
            p.bad = slot.bad;
            p.column = static_cast<int>(c);
            p.icon_id = status_icon_index(frame.status_effects[slot.index].key);
            const int item_h = item_height(slot.name);
            p.cell = RectPx{ col.x, slot.y, col.w, item_h };
            p.icon = RectPx{ col.x + ((col.w - icon_px) / 2), slot.y, icon_px, icon_px };
            p.name_band = RectPx{ col.x + (gap / 2), slot.y + icon_px, std::max(1, col.w - gap), item_h - icon_px };
            p.name = slot.name;
            out.items.push_back(p);
            if (first) {
                min_x = col.x;
                max_x = col.x + col.w;
                min_y = slot.y;
                max_y = slot.y + item_h;
                first = false;
            } else {
                min_x = std::min(min_x, col.x);
                max_x = std::max(max_x, col.x + col.w);
                min_y = std::min(min_y, slot.y);
                max_y = std::max(max_y, slot.y + item_h);
            }
        }
    }
    if (!first) {
        out.extent = RectPx{ min_x, min_y, max_x - min_x, max_y - min_y };
    }
    return out;
}

std::uint64_t status_ring_signature(const GameFrame &frame, bool left)
{
    //! FNV-1a。左は 良い・待っている・特殊、右は 悪い（`layout_status_icons` と同じ振り分け）。
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](unsigned char byte) {
        hash ^= byte;
        hash *= 1099511628211ull;
    };
    for (const StatusEffectView &effect : frame.status_effects) {
        const bool on_left = (effect.kind == StatusEffectKind::Good) || (effect.kind == StatusEffectKind::Waiting)
            || (effect.kind == StatusEffectKind::Special);
        const bool on_right = effect.kind == StatusEffectKind::Bad;
        if ((left && !on_left) || (!left && !on_right)) {
            continue;
        }
        for (const char c : effect.key) {
            mix(static_cast<unsigned char>(c));
        }
        mix(0xFFu);
        mix(static_cast<unsigned char>(effect.kind));
    }
    return hash;
}

StatusIconLayout layout_status_icons(TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    double left_ring_ms, double right_ring_ms)
{
    StatusIconLayout out;
    const RectPx &orb = layout.orb;
    if (orb.empty() || frame.status_effects.empty()) {
        return out;
    }
    out.icon_px = status_icon_size(orb);
    const int icon_px = out.icon_px;
    const int ch = text.cell_h();
    const int gap = std::max(4, icon_px / 8);
    //! 名前の場所は常に 2 行ぶん（1 行の名前と 2 行の名前が混ざっても、段の高さは変わらない）。
    const int name_line_h = status_name_line_height(text, icon_px);
    out.name_line_h = name_line_h;
    const int cell_h = ch + icon_px + (2 * name_line_h);

    /*
     * 左右の端。左の端は地図の左＋1 マス（状態列が左にあるときはその右）、右の端は地図の右−1 マス
     * （状態列が右にあるときはその左）。重なる作りでサブパネルを開いているときは、右の列の
     * サブパネルの手前で止める（アイコンがパネルの下へ潜らないように）。
     * 1 行だけなので、常時の列（1 行目より上にある）は避けなくてよい。
     */
    const RectPx &scene = layout.scene;
    int left_limit = scene.x + layout.cell_w;
    int right_limit = (scene.x + scene.w) - layout.cell_w;
    if (!layout.status_col.empty()) {
        if (layout.status_col.x <= scene.x) {
            left_limit = std::max(left_limit, layout.status_col.x + layout.status_col.w + layout.cell_w);
        } else {
            right_limit = std::min(right_limit, layout.status_col.x - layout.cell_w);
        }
    }
    if (layout.sub_overlaid) {
        for (int i = kSubRightSlot; i < (kSubRightSlot + kSubRightMax); ++i) {
            const RectPx &sub = layout.sub[static_cast<std::size_t>(i)];
            if (!sub.empty()) {
                right_limit = std::min(right_limit, sub.x - layout.cell_w);
            }
        }
    }

    struct Pending {
        int effect_index;
        int width; //!< 隙間を含む 1 個の幅
        int icon_id;
        FittedText name;
        FittedText amount;
    };
    std::vector<Pending> left_items;
    std::vector<Pending> right_items;
    //! 名前・効果量は、基準の大きさ（全角 4 文字がアイコンの幅に収まる大きさ）にそろえ、収まらない長い名前だけ縮める。
    const int ref_px = status_text_reference_px(text, icon_px);
    const auto make = [&](std::size_t i) {
        const StatusEffectView &effect = frame.status_effects[i];
        const FittedText name = fit_name_with_reference(text, effect.label_utf8, icon_px, ref_px);
        const FittedText amount = fit_with_reference(text, effect.amount_utf8, icon_px, ref_px);
        const int body = std::max({ icon_px, name.width, amount.width });
        return Pending{ static_cast<int>(i), body + gap, status_icon_index(effect.key), name, amount };
    };
    //! 左は 良い → 待っている → 特殊 の順。並びの中はフレームの順。
    for (const StatusEffectKind kind : { StatusEffectKind::Good, StatusEffectKind::Waiting, StatusEffectKind::Special }) {
        for (std::size_t i = 0; i < frame.status_effects.size(); ++i) {
            if (frame.status_effects[i].kind == kind) {
                left_items.push_back(make(i));
            }
        }
    }
    for (std::size_t i = 0; i < frame.status_effects.size(); ++i) {
        if (frame.status_effects[i].kind == StatusEffectKind::Bad) {
            right_items.push_back(make(i));
        }
    }

    const int icon_y_orb = orb.y + static_cast<int>(std::lround(static_cast<double>(orb.h) * 0.42));
    const int icon_y_left = left_row_icon_y(layout, icon_y_orb, ch, cell_h, gap, left_limit); //!< 左右の両方の段の高さ
    const double speed = static_cast<double>(icon_px + gap); //!< 1 秒にアイコン 1 個ぶん

    const auto place = [&](const std::vector<Pending> &items, bool left, double ring_ms, StatusRing &ring) {
        //! 右の段も左の段の高さにそろえる（決めたこと 2026-10-09「右のアイコンの配置は左に合わせて」）。
        const int icon_y = icon_y_left;
        ring.count = static_cast<int>(items.size());
        if (items.empty()) {
            return;
        }
        const int edge_near = left ? orb.x : (orb.x + orb.w);
        const int span = left ? (edge_near - left_limit) : (right_limit - edge_near);
        if ((span < (icon_px + gap)) || ((icon_y - ch) < scene.y) || ((icon_y - ch + cell_h) > (scene.y + scene.h))) {
            out.dropped += static_cast<int>(items.size()); // 横が足りない・地図の上端か下端を越える
            return;
        }
        const auto fill = [&](StatusIconPlacement &p, const Pending &item, int x, int w) {
            p.effect_index = item.effect_index;
            p.left = left;
            p.row = 0;
            p.icon_id = item.icon_id;
            p.name = item.name;
            p.amount = item.amount;
            p.cell = RectPx{ x, icon_y - ch, w, cell_h };
            p.icon = RectPx{ x + ((w - icon_px) / 2), icon_y, icon_px, icon_px };
            //! 名前の帯は実際の行数ぶん（1 行の名前は上の行）。
            p.name_band = RectPx{ x + (gap / 2), icon_y + icon_px, std::max(1, w - gap), std::max(1, item.name.lines) * name_line_h };
            const StatusEffectView &effect = frame.status_effects[static_cast<std::size_t>(item.effect_index)];
            if (!effect.amount_utf8.empty()) {
                p.amount_band = RectPx{ x + (gap / 2), icon_y - ch, std::max(1, w - gap), ch };
            }
        };

        int total = 0;
        int widest = 0;
        for (const Pending &item : items) {
            total += std::min(item.width, span);
            widest = std::max(widest, item.width);
        }
        if (total <= span) {
            // 1 行に入りきる。止まって並べる（玉に近い側から外へ）。
            int used = 0;
            for (const Pending &item : items) {
                const int w = std::min(item.width, span);
                StatusIconPlacement p;
                fill(p, item, left ? (edge_near - used - w) : (edge_near + used), w);
                out.items.push_back(p);
                used += w;
            }
            return;
        }

        // 入りきらない。全員を同じ幅の輪にして、玉から遠ざかる向きへ等速で流す。
        const int pitch = std::min(widest, span);
        const int capacity = std::max(1, span / pitch);
        const int window_w = capacity * pitch;
        ring.rotating = true;
        ring.capacity = capacity;
        ring.pitch = pitch;
        ring.window = RectPx{ left ? (edge_near - window_w) : edge_near, icon_y - ch, window_w, cell_h };
        ring.length = static_cast<double>(items.size()) * static_cast<double>(pitch);
        ring.speed = speed;
        const double offset = std::fmod(std::max(0.0, ring_ms) * 0.001 * speed, ring.length);
        for (std::size_t i = 0; i < items.size(); ++i) {
            double c = std::fmod((static_cast<double>(i) * static_cast<double>(pitch)) + offset, ring.length);
            if (c > (ring.length - static_cast<double>(pitch))) {
                c -= ring.length; // 輪の継ぎ目: 玉に近い端から入りかけ
            }
            if ((c >= static_cast<double>(window_w)) || ((c + static_cast<double>(pitch)) <= 0.0)) {
                continue; // 枠に見えていない
            }
            const double x_true = left ? (static_cast<double>(edge_near) - c - static_cast<double>(pitch))
                                       : (static_cast<double>(edge_near) + c);
            const double x_floor = std::floor(x_true);
            const double win0 = static_cast<double>(ring.window.x);
            const double win1 = win0 + static_cast<double>(window_w);
            const double visible = std::min(x_true + static_cast<double>(pitch), win1) - std::max(x_true, win0);
            StatusIconPlacement p;
            fill(p, items[i], static_cast<int>(x_floor), pitch);
            p.rotating = true;
            p.frac_x = static_cast<float>(x_true - x_floor);
            p.clip = ring.window;
            p.alpha = static_cast<float>(std::clamp(visible / static_cast<double>(pitch), 0.0, 1.0));
            p.ring_index = static_cast<int>(i);
            p.ring_distance = static_cast<float>(c);
            out.items.push_back(p);
        }
    };
    place(left_items, true, left_ring_ms, out.ring_left);
    place(right_items, false, right_ring_ms, out.ring_right);
    return out;
}

bool draw_orb_hud(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    const OrbHudImages &images, const OrbHudParams &params, OrbHudAnimation &anim, std::uint32_t now_ms)
{
    const RectPx &orb = layout.orb;
    if (orb.empty() || (frame.hud.hp_max <= 0)) {
        anim.reset(); // ゲーム前。次に出るときは実際の割合から始める
        return false;
    }
    const bool has_mp = frame.hud.sp_max > 0;
    const float hp_ratio = clamp01(static_cast<float>(frame.hud.hp) / static_cast<float>(frame.hud.hp_max));
    const float mp_ratio = has_mp ? clamp01(static_cast<float>(frame.hud.sp) / static_cast<float>(frame.hud.sp_max)) : 0.f;
    orb_hud_update(anim, params, hp_ratio, mp_ratio, now_ms);
    const UiProbeLayer probe_layer("hud");
    //! 検査用の問い合わせ：玉（棒）が出している数字と意匠の名前。字は下の `draw_orb_texts` / `draw_fallback_bars` が出す。
    ui_probe().hud_values(frame.hud.hp, frame.hud.hp_max, frame.hud.sp, frame.hud.sp_max, has_mp, images.has_orb(),
        orb_profile().asset_key.empty() ? std::string("default") : orb_profile().asset_key);
    if (frame.hud.doomsday_days_left >= 0) {
        ui_probe().hud_doomsday(frame.hud.doomsday_days_left);
    }

    if (!images.has_orb()) {
        draw_fallback_bars(paint, text, layout, frame);
        return false;
    }

    const OrbMap m(orb);
    const UiImagePainter &painter = *images.painter;
    //! 下の薄い影（`kOrbArtBottomY` より下）は地図の下縁より下にはみ出すので描かない。
    const RectPx orb_shown = orb_visible_rect(orb);
    const float v_shown = static_cast<float>(orb_shown.h) / static_cast<float>(std::max(1, orb.h));
    const auto full = [&](const UiImage &image) { painter.draw_region(image, orb_shown, 0.f, 0.f, 1.f, v_shown); };

    // 重ね順: 後ろ → ガラス → 液体 → ガラスの手前 → 前（水面の線・泡・字は後で重なる）
    full(*images.back);
    full(*images.glass);
    const OrbProfile &prof = orb_profile();
    draw_liquid(images, *images.liquid_hp, m, paint, anim, params, anim.hp, prof.hp_cx, 0.0,
        PaintColor{ 1.f, 0.66f, 0.62f, 0.62f });
    if (has_mp) {
        draw_liquid(images, *images.liquid_mp, m, paint, anim, params, anim.mp, prof.mp_cx, 1.3,
            PaintColor{ 0.66f, 0.78f, 1.f, 0.62f });
    }
    full(*images.glass_top);
    full(*images.front);
    //! 短愚の終末の太陽（中央の飾りの上。コアが `doomsday_days_left` を送っているときだけ）。
    if (prof.emblem.present && (frame.hud.doomsday_days_left >= 0)) {
        draw_doomsday_sun(paint, m, prof.emblem, frame.hud.doomsday_days_left);
    }

    draw_bubbles(paint, m, anim.hp, prof.hp_cx, anim);
    if (has_mp) {
        draw_bubbles(paint, m, anim.mp, prof.mp_cx, anim);
    }
    draw_orb_texts(text, m, prof.hp_cx, hp_label_of(frame), frame.hud.hp, frame.hud.hp_max,
        TextColor{ 1.f, 0.62f, 0.58f, 1.f }, true);
    if (has_mp) {
        draw_orb_texts(text, m, prof.mp_cx, sp_label_of(frame), frame.hud.sp, frame.hud.sp_max,
            TextColor{ 0.62f, 0.76f, 1.f, 1.f }, false);
    }
    if (images.has_icons()) {
        draw_status_icons(paint, text, layout, frame, images, anim, now_ms);
    }
    return true;
}

} // namespace hd2d
