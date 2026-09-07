/*!
 * @file virtual_pad.cpp
 * @brief `virtual_pad.h` の実装。
 */
#include "ui/virtual_pad.h"

#include "i18n/lang.h"

#include "render/text_overlay.h"
#include "ui/key_binds.h" //!< UI 自身の操作の名前（負の番号）
#include "ui/ui_paint.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hd2d {

namespace {

/*!
 * @name 方向のリピート
 * @details **`game_pad.cpp` と同じ値**（物理パッドと指で歩く速さを揃える。
 * 片方だけ変えると「パッドだと速いのに指だと遅い」になる）。
 * @{
 */
constexpr std::uint32_t kFirstRepeatMs = 260;
constexpr std::uint32_t kNextRepeatMs = 90;
/*! @} */

//! 8 方向の境界（22.5 度の正弦）。傾きの成分がこれを超えた軸だけ ±1 にする。
constexpr float kOctantSin = 0.3827f;

/*!
 * @name スティックのしきい値（外周半径に対する比）
 * @details 入りと出を別の値にする（`game_pad.h` の約束 3 と同じ理由——境目のがたつき）。
 * @{
 */
constexpr float kStickEnterRatio = 0.34f;
constexpr float kStickLeaveRatio = 0.22f;
//! RS（連続値）の死区。
constexpr float kAnalogDeadRatio = 0.30f;
/*! @} */

//! 拡大縮小の範囲。編集画面のピンチとメニューの両方がこれを見る。
constexpr float kScaleMin = 0.5f;
constexpr float kScaleMax = 2.0f;

/*!
 * @brief 既定値を詰めたときの画面の縦横比（実機。2281×968 ＝ 2.36:1）。
 * @details 大きさの基準を「短辺」だけにすると、**これより横に狭い画面で大きくなりすぎる**
 * ——短辺（＝高さ）は同じでも横幅の余裕が減るため、同じ比のボタンが画面を埋めて重なる
 * （16:10 のエミュレータで実測）。そこで**この比より狭いときは幅から基準を採る**。
 * 既定値を詰めた端末では `h ≒ w / 2.36` なので、あちらの見えは変わらない。
 */
constexpr float kTunedAspect = 2.36f;

/*!
 * @brief 縦持ちの既定を詰めたときの画面の縦横比（参考画像 `VirtualPad_V.jpg` ＝ 1080×1920 ＝ 1:1.78）。
 * @details 上の `kTunedAspect` と同じ理屈を 90 度回したもの。縦では**短辺は幅**なので、
 * ふつうは幅を基準にする。ただし 4:3 の板（縦持ちで 1536×2048 など）のように
 * **これより縦に短い**画面では、幅から採ると背が足りず、ボタンが下段パネルへ食い込む。
 * そこで「これより縦に短いときは高さから採る」。9:16 の端末では幅そのものになる。
 */
constexpr float kTunedAspectPortrait = 1.78f;

/*!
 * @brief 大きさの基準（画素）。**出すのはここ 1 か所**（描画・当たり判定・編集で必ず揃える）。
 * @details 縦持ちと横持ちで基準の採り方が違う（短辺がどちらの辺かが入れ替わるため）。
 */
float size_basis(const RectPx &area)
{
    const float w = static_cast<float>(std::max(1, area.w));
    const float h = static_cast<float>(std::max(1, area.h));
    if (vpad_orient_of(area) == kVpadPortrait) {
        return std::min(w, h / kTunedAspectPortrait);
    }
    return std::min(h, w / kTunedAspect);
}

/*!
 * @brief 既定の配置。**実機で並べ直したものを写した**（2026-08-12 に決めた
 * 「デフォルトの配置、サイズをこんな感じにして」＋その画面の写真）。
 *
 * 出自は 2 段ある。最初は参考画像 `VirtualPad.jpg`（1920×1080）から起こしたが、
 * 実機で触ってみると**小さすぎ・寄りすぎ**だった。いまの値は編集画面で
 * 詰めた実物を実測したもの（使える範囲 2281×968 の画素から比へ直した）。
 * **触り心地の基準は実機であって参考画像ではない。**
 *
 * 位置は**使える範囲**（切り欠きを避けた矩形）の幅・高さに対する比、
 * 大きさは `size_basis()` に対する比で持つ——横長でも縦長でも形が崩れない。
 */
struct DefaultSpec {
    float xf; //!< 中心 x（幅比）
    float yf; //!< 中心 y（高さ比）
    int shape; //!< 0 = 円 / 1 = 角丸長方形 / 2 = スティック
    float a; //!< 円: 半径 / 長方形: 半幅 / スティック: 外周半径（`size_basis()` 比）
    float b; //!< 長方形: 半高 / スティック: つまみ半径（`size_basis()` 比）
};

//! 並びは `VpadControl` と同じ（A B X Y R1 R2 R3 L1 L2 L3 START SELECT RS LS）。
constexpr DefaultSpec kDefaultsLand[kVpadControlCount] = {
    { 0.673f, 0.892f, 0, 0.085f, 0.f }, // A
    { 0.660f, 0.670f, 0, 0.079f, 0.f }, // B
    { 0.713f, 0.441f, 0, 0.084f, 0.f }, // X
    { 0.799f, 0.313f, 0, 0.082f, 0.f }, // Y
    { 0.917f, 0.098f, 1, 0.186f, 0.065f }, // R1
    { 0.744f, 0.150f, 1, 0.093f, 0.031f }, // R2
    { 0.911f, 0.316f, 0, 0.073f, 0.f }, // R3
    { 0.057f, 0.098f, 1, 0.186f, 0.065f }, // L1
    { 0.231f, 0.153f, 1, 0.093f, 0.031f }, // L2
    { 0.079f, 0.292f, 0, 0.068f, 0.f }, // L3
    { 0.504f, 0.870f, 1, 0.060f, 0.028f }, // START
    { 0.374f, 0.868f, 1, 0.060f, 0.028f }, // SELECT
    { 0.840f, 0.712f, 2, 0.262f, 0.100f }, // RS
    { 0.164f, 0.711f, 2, 0.262f, 0.100f }, // LS
};

/*!
 * @brief 縦持ちの既定。**参考画像 `VirtualPad_V.jpg` の実測**（2026-08-12 に決めた
 * 「VirtualPad_V.jpg を参考に盾持ちになった場合の画面構成を設定」「画像の配置がデフォルト」）。
 *
 * 画像は 1080×1920。緑の輪郭の外接矩形を測り、中心は画面の幅・高さに対する比、
 * 大きさは `size_basis()`（縦では ≒ 幅 1078px）に対する比へ直してある。
 * 線の太さ（約 5px）は半径から引いた——**輪郭の外側ではなく形そのもの**を写す。
 *
 * @note **画像に START と SELECT は描かれていない。**落とすと機能メニュー（SELECT）へ
 * 入る手が無くなるので、空いている帯（L2/R2 の行とスティックの間）へ小さく置いた。
 * ここだけは実測ではなく決めである。
 */
constexpr DefaultSpec kDefaultsPort[kVpadControlCount] = {
    { 0.519f, 0.924f, 0, 0.043f, 0.f }, // A
    { 0.527f, 0.839f, 0, 0.043f, 0.f }, // B
    { 0.608f, 0.767f, 0, 0.043f, 0.f }, // X
    { 0.765f, 0.738f, 0, 0.043f, 0.f }, // Y
    { 0.857f, 0.557f, 1, 0.117f, 0.046f }, // R1
    { 0.626f, 0.579f, 1, 0.068f, 0.037f }, // R2
    { 0.907f, 0.665f, 0, 0.043f, 0.f }, // R3
    { 0.143f, 0.559f, 1, 0.117f, 0.046f }, // L1
    { 0.381f, 0.578f, 1, 0.069f, 0.039f }, // L2
    { 0.096f, 0.675f, 0, 0.043f, 0.f }, // L3
    { 0.500f, 0.672f, 1, 0.060f, 0.028f }, // START（画像に無い。空き帯へ置いた）
    { 0.360f, 0.672f, 1, 0.060f, 0.028f }, // SELECT（同上）
    { 0.772f, 0.865f, 2, 0.136f, 0.073f }, // RS
    { 0.202f, 0.863f, 2, 0.136f, 0.073f }, // LS
};

//! いま向いている側の既定。**引くのはここ 1 か所**（描画・当たり判定・編集で必ず揃える）。
const DefaultSpec &default_spec(int control, const RectPx &area)
{
    return (vpad_orient_of(area) == kVpadPortrait) ? kDefaultsPort[control] : kDefaultsLand[control];
}

//! 1 コントロールの画素での姿。描画・当たり判定・編集の**全員がここから作る**（必守制約 3 の親戚）。
struct ControlGeom {
    float cx{ 0.f };
    float cy{ 0.f };
    int shape{ 0 }; //!< `DefaultSpec::shape` と同じ
    float half_w{ 0.f }; //!< 長方形だけ
    float half_h{ 0.f };
    float radius{ 0.f }; //!< 円・スティック外周
    float knob{ 0.f }; //!< スティックのつまみ
};

/*!
 * @brief 1 コントロールの画素での姿を出す。
 * @param area 置ける範囲（**窓いっぱいとは限らない**。Android は切り欠きを避けた矩形）。
 * @details 位置の比（`xf`/`yf`）は**この範囲に対する**割合で、大きさは範囲の短辺から採る。
 * 窓ではなく範囲を基準にするので、切り欠きのある端末でも形は変わらず、置き場所だけ寄る。
 */
ControlGeom control_geom(int control, const VirtualPadSettings &settings, const RectPx &area)
{
    const DefaultSpec &spec = default_spec(control, area);
    const VpadSlot &slot = settings.slot[vpad_orient_of(area)][control];
    const float u = size_basis(area);
    ControlGeom g;
    g.shape = spec.shape;
    const float xf = slot.overridden() ? slot.xf : spec.xf;
    const float yf = slot.overridden() ? slot.yf : spec.yf;
    const float scale = std::clamp(slot.scale, kScaleMin, kScaleMax);
    g.cx = static_cast<float>(area.x) + (xf * static_cast<float>(area.w));
    g.cy = static_cast<float>(area.y) + (yf * static_cast<float>(area.h));
    g.half_w = spec.a * u * scale;
    g.half_h = spec.b * u * scale;
    g.radius = spec.a * u * scale;
    g.knob = spec.b * u * scale;
    /*
     * **範囲からはみ出させない。**位置は幅に対する比・大きさは短辺に対する比なので、
     * 既定の値より**横に狭い画面**（既定は 2.36:1 の実機で詰めた）では端のボタンが
     * 画面の外へ出る。出た所は押せないのに見えもしないので、気づきようが無い。
     * 描画と当たり判定はここを共有しているので、寄せても食い違わない。
     */
    const float half_x = (g.shape == 1) ? g.half_w : g.radius;
    const float half_y = (g.shape == 1) ? g.half_h : g.radius;
    const float min_x = static_cast<float>(area.x) + half_x;
    const float max_x = static_cast<float>(area.x + area.w) - half_x;
    const float min_y = static_cast<float>(area.y) + half_y;
    const float max_y = static_cast<float>(area.y + area.h) - half_y;
    if (min_x <= max_x) {
        g.cx = std::clamp(g.cx, min_x, max_x);
    }
    if (min_y <= max_y) {
        g.cy = std::clamp(g.cy, min_y, max_y);
    }
    return g;
}

/*!
 * @name 形の部品（`UiPaint` の三角と四角から組む）
 * @details 円と角丸はこのファイルにしか要らないので、`UiPaint` へは足さない。
 * @{
 */
void fill_circle(UiPaint &paint, float cx, float cy, float r, const PaintColor &color, int segments = 36)
{
    const float step = 6.2831853f / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        const float a0 = step * static_cast<float>(i);
        const float a1 = a0 + step;
        paint.triangle(cx, cy, cx + (std::cos(a0) * r), cy + (std::sin(a0) * r),
            cx + (std::cos(a1) * r), cy + (std::sin(a1) * r), color);
    }
}

//! 部分円環（`a0`〜`a1` ラジアン）。丸ごとの輪も角丸の角もこれで描く。
void ring_arc(UiPaint &paint, float cx, float cy, float r_in, float r_out, float a0, float a1,
    const PaintColor &color, int segments)
{
    const float step = (a1 - a0) / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        const float b0 = a0 + (step * static_cast<float>(i));
        const float b1 = b0 + step;
        const float c0 = std::cos(b0);
        const float s0 = std::sin(b0);
        const float c1 = std::cos(b1);
        const float s1 = std::sin(b1);
        paint.triangle(cx + (c0 * r_in), cy + (s0 * r_in), cx + (c0 * r_out), cy + (s0 * r_out),
            cx + (c1 * r_out), cy + (s1 * r_out), color);
        paint.triangle(cx + (c0 * r_in), cy + (s0 * r_in), cx + (c1 * r_out), cy + (s1 * r_out),
            cx + (c1 * r_in), cy + (s1 * r_in), color);
    }
}

void ring(UiPaint &paint, float cx, float cy, float r_in, float r_out, const PaintColor &color, int segments = 48)
{
    ring_arc(paint, cx, cy, r_in, r_out, 0.f, 6.2831853f, color, segments);
}

//! 扇（角丸の角の塗り）。
void fan(UiPaint &paint, float cx, float cy, float r, float a0, float a1, const PaintColor &color, int segments = 8)
{
    const float step = (a1 - a0) / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        const float b0 = a0 + (step * static_cast<float>(i));
        const float b1 = b0 + step;
        paint.triangle(cx, cy, cx + (std::cos(b0) * r), cy + (std::sin(b0) * r),
            cx + (std::cos(b1) * r), cy + (std::sin(b1) * r), color);
    }
}

constexpr float kPi = 3.1415927f;

void fill_round_rect(UiPaint &paint, float cx, float cy, float half_w, float half_h, float rad, const PaintColor &color)
{
    rad = std::min(rad, std::min(half_w, half_h));
    const float x0 = cx - half_w;
    const float y0 = cy - half_h;
    const float x1 = cx + half_w;
    const float y1 = cy + half_h;
    // 中央の帯＋上下の帯＋四隅の扇。
    paint.rect(RectPx{ static_cast<int>(x0), static_cast<int>(y0 + rad), static_cast<int>(x1 - x0),
                   static_cast<int>((y1 - rad) - (y0 + rad)) },
        color);
    paint.rect(RectPx{ static_cast<int>(x0 + rad), static_cast<int>(y0), static_cast<int>((x1 - rad) - (x0 + rad)),
                   static_cast<int>(rad) },
        color);
    paint.rect(RectPx{ static_cast<int>(x0 + rad), static_cast<int>(y1 - rad), static_cast<int>((x1 - rad) - (x0 + rad)),
                   static_cast<int>(rad) },
        color);
    fan(paint, x0 + rad, y0 + rad, rad, kPi, kPi * 1.5f, color);
    fan(paint, x1 - rad, y0 + rad, rad, kPi * 1.5f, kPi * 2.f, color);
    fan(paint, x1 - rad, y1 - rad, rad, 0.f, kPi * 0.5f, color);
    fan(paint, x0 + rad, y1 - rad, rad, kPi * 0.5f, kPi, color);
}

void frame_round_rect(UiPaint &paint, float cx, float cy, float half_w, float half_h, float rad, float t,
    const PaintColor &color)
{
    rad = std::min(rad, std::min(half_w, half_h));
    t = std::min(t, rad);
    const float x0 = cx - half_w;
    const float y0 = cy - half_h;
    const float x1 = cx + half_w;
    const float y1 = cy + half_h;
    // 四辺の帯＋四隅の部分円環。
    paint.rect(RectPx{ static_cast<int>(x0 + rad), static_cast<int>(y0), static_cast<int>((x1 - rad) - (x0 + rad)),
                   static_cast<int>(t) },
        color);
    paint.rect(RectPx{ static_cast<int>(x0 + rad), static_cast<int>(y1 - t), static_cast<int>((x1 - rad) - (x0 + rad)),
                   static_cast<int>(t) },
        color);
    paint.rect(RectPx{ static_cast<int>(x0), static_cast<int>(y0 + rad), static_cast<int>(t),
                   static_cast<int>((y1 - rad) - (y0 + rad)) },
        color);
    paint.rect(RectPx{ static_cast<int>(x1 - t), static_cast<int>(y0 + rad), static_cast<int>(t),
                   static_cast<int>((y1 - rad) - (y0 + rad)) },
        color);
    ring_arc(paint, x0 + rad, y0 + rad, rad - t, rad, kPi, kPi * 1.5f, color, 6);
    ring_arc(paint, x1 - rad, y0 + rad, rad - t, rad, kPi * 1.5f, kPi * 2.f, color, 6);
    ring_arc(paint, x1 - rad, y1 - rad, rad - t, rad, 0.f, kPi * 0.5f, color, 6);
    ring_arc(paint, x0 + rad, y1 - rad, rad - t, rad, kPi * 0.5f, kPi, color, 6);
}
/*! @} */

/*! @name 色（遊んでいる画面は薄く、編集画面は濃く） @{ */
const PaintColor kFill{ 0.08f, 0.10f, 0.14f, 0.30f };
const PaintColor kFillPressed{ 0.95f, 0.85f, 0.30f, 0.40f };
const PaintColor kLine{ 0.90f, 0.93f, 1.f, 0.55f };
const PaintColor kLinePressed{ 1.f, 0.95f, 0.55f, 0.95f };
const TextColor kLabel{ 0.95f, 0.96f, 1.f, 0.85f };
const TextColor kLabelPressed{ 1.f, 0.95f, 0.55f, 1.f };
const PaintColor kSelected{ 1.f, 0.85f, 0.25f, 0.95f };
const TextColor kEditorHint{ 0.75f, 0.78f, 0.85f, 1.f };
/*! @} */

//! 編集画面の中央ボタン（0 = デフォルトに戻す / 1 = 戻る）。描画と当たり判定が共有する。
RectPx editor_button_rect(int which, const RectPx &area)
{
    const float u = size_basis(area);
    const int w = static_cast<int>(u * 0.32f);
    const int h = static_cast<int>(u * 0.075f);
    const int x = area.x + ((area.w - w) / 2);
    const int y = area.y
        + static_cast<int>((static_cast<float>(area.h) * 0.44f) + (static_cast<float>(which) * u * 0.10f));
    return RectPx{ x, y, w, h };
}

} // namespace

const char *vpad_control_name(VpadControl control)
{
    switch (control) {
    case VpadControl::A:
        return "A";
    case VpadControl::B:
        return "B";
    case VpadControl::X:
        return "X";
    case VpadControl::Y:
        return "Y";
    case VpadControl::R1:
        return "R1";
    case VpadControl::R2:
        return "R2";
    case VpadControl::R3:
        return "R3";
    case VpadControl::L1:
        return "L1";
    case VpadControl::L2:
        return "L2";
    case VpadControl::L3:
        return "L3";
    case VpadControl::Start:
        return "START";
    case VpadControl::Select:
        return "SELECT";
    case VpadControl::RS:
        return "RS";
    case VpadControl::LS:
        return "LS";
    default:
        return "?";
    }
}

PadInput vpad_control_input(VpadControl control)
{
    switch (control) {
    case VpadControl::A:
        return PadInput::A;
    case VpadControl::B:
        return PadInput::B;
    case VpadControl::X:
        return PadInput::X;
    case VpadControl::Y:
        return PadInput::Y;
    case VpadControl::R1:
        return PadInput::RightShoulder;
    case VpadControl::R2:
        return PadInput::RightTrigger;
    case VpadControl::R3:
        return PadInput::RightStick;
    case VpadControl::L1:
        return PadInput::LeftShoulder;
    case VpadControl::L2:
        return PadInput::LeftTrigger;
    case VpadControl::L3:
        return PadInput::LeftStick;
    case VpadControl::Start:
        return PadInput::Start;
    case VpadControl::Select:
        return PadInput::Back;
    default:
        return PadInput::Count; // LS / RS はボタンではない
    }
}

namespace {

bool parse_vpad_control(const std::string &name, int &out)
{
    for (int i = 0; i < kVpadControlCount; ++i) {
        if (name == vpad_control_name(static_cast<VpadControl>(i))) {
            out = i;
            return true;
        }
    }
    return false;
}

} // namespace

int vpad_orient_of(const RectPx &area)
{
    return (area.h > area.w) ? kVpadPortrait : kVpadLandscape;
}

const char *vpad_orient_name(int orient)
{
    return (orient == kVpadPortrait) ? "port" : "land";
}

void VirtualPadSettings::reset_layout(int orient)
{
    if ((orient < 0) || (orient >= kVpadOrientCount)) {
        return;
    }
    for (VpadSlot &s : this->slot[orient]) {
        s = VpadSlot{};
    }
}

void VirtualPadSettings::reset_all()
{
    *this = VirtualPadSettings{};
}

std::string VirtualPadSettings::visible_line() const
{
    std::string line;
    for (int i = 0; i < kVpadControlCount; ++i) {
        if (!line.empty()) {
            line += ',';
        }
        line += vpad_control_name(static_cast<VpadControl>(i));
        line += this->visible[i] ? ":1" : ":0";
    }
    return line;
}

void VirtualPadSettings::parse_visible(const std::string &line)
{
    std::size_t pos = 0;
    while (pos < line.size()) {
        std::size_t end = line.find(',', pos);
        if (end == std::string::npos) {
            end = line.size();
        }
        const std::string token = line.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t colon = token.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        int control = 0;
        if (!parse_vpad_control(token.substr(0, colon), control)) {
            continue; // 知らない綴りは黙って飛ばす（版が進んだ cfg でも落ちない）
        }
        this->visible[control] = (token.substr(colon + 1) != "0");
    }
}

std::string VirtualPadSettings::layout_line(int orient) const
{
    if ((orient < 0) || (orient >= kVpadOrientCount)) {
        return std::string();
    }
    const DefaultSpec *const defaults = (orient == kVpadPortrait) ? kDefaultsPort : kDefaultsLand;
    std::string line;
    char buf[96]{};
    for (int i = 0; i < kVpadControlCount; ++i) {
        const VpadSlot &s = this->slot[orient][i];
        //! 大きさだけ変えた（位置は既定のまま）ものも書きたいので、scale も上書き扱いに含める。
        if (!s.overridden() && (s.scale == 1.f)) {
            continue;
        }
        const float xf = s.overridden() ? s.xf : defaults[i].xf;
        const float yf = s.overridden() ? s.yf : defaults[i].yf;
        std::snprintf(buf, sizeof(buf), "%s:%.4f,%.4f,%.3f", vpad_control_name(static_cast<VpadControl>(i)),
            static_cast<double>(xf), static_cast<double>(yf), static_cast<double>(s.scale));
        if (!line.empty()) {
            line += ';';
        }
        line += buf;
    }
    return line;
}

void VirtualPadSettings::parse_layout(int orient, const std::string &line)
{
    if ((orient < 0) || (orient >= kVpadOrientCount)) {
        return;
    }
    this->reset_layout(orient); //!< **既定へ戻してから**読む（消した上書きが残らないように）
    std::size_t pos = 0;
    while (pos < line.size()) {
        std::size_t end = line.find(';', pos);
        if (end == std::string::npos) {
            end = line.size();
        }
        const std::string token = line.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t colon = token.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        int control = 0;
        if (!parse_vpad_control(token.substr(0, colon), control)) {
            continue;
        }
        float xf = 0.f;
        float yf = 0.f;
        float scale = 1.f;
        if (std::sscanf(token.c_str() + colon + 1, "%f,%f,%f", &xf, &yf, &scale) < 2) {
            continue;
        }
        VpadSlot &s = this->slot[orient][control];
        s.xf = std::clamp(xf, 0.f, 1.f);
        s.yf = std::clamp(yf, 0.f, 1.f);
        s.scale = std::clamp(scale, kScaleMin, kScaleMax);
    }
}

bool VirtualPadSettings::differs_from(const VirtualPadSettings &other) const
{
    if ((this->show != other.show) || (this->opacity != other.opacity)) {
        return true;
    }
    for (int i = 0; i < kVpadControlCount; ++i) {
        if (this->visible[i] != other.visible[i]) {
            return true;
        }
    }
    //! **縦横の両方を見る**（片方だけだと、回した先で並べ直した配置が書き戻されない）。
    for (int o = 0; o < kVpadOrientCount; ++o) {
        if (this->layout_line(o) != other.layout_line(o)) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------ 実行時の状態 */

VirtualPad::Grab *VirtualPad::find_grab(std::int64_t finger)
{
    for (Grab &g : this->grabs_) {
        //! **空きは `active` で見る**（Android の実指は id=0。番号の番人値は使えない。ヘッダの注記）。
        if (g.active && (g.finger == finger)) {
            return &g;
        }
    }
    return nullptr;
}

VirtualPad::Grab *VirtualPad::free_grab()
{
    for (Grab &g : this->grabs_) {
        if (!g.active) {
            return &g;
        }
    }
    return nullptr;
}

int VirtualPad::control_at(float x, float y, const VirtualPadSettings &settings, const RectPx &area,
    bool editor) const
{
    const float u = size_basis(area);
    int best = -1;
    float best_dist = 0.f;
    for (int i = 0; i < kVpadControlCount; ++i) {
        if (!editor && !settings.visible[i]) {
            continue; // 消したボタンは押せない（編集画面では選べる）
        }
        const ControlGeom g = control_geom(i, settings, area);
        const float dx = x - g.cx;
        const float dy = y - g.cy;
        bool hit = false;
        if (g.shape == 1) {
            //! 角丸長方形。**指の当たりは少し広く取る**（縁ぎりぎりのタップを拾う）。
            const float margin = u * 0.012f;
            hit = (std::fabs(dx) <= (g.half_w + margin)) && (std::fabs(dy) <= (g.half_h + margin));
        } else if (g.shape == 2) {
            hit = ((dx * dx) + (dy * dy)) <= (g.radius * g.radius);
        } else {
            const float r = g.radius * 1.25f;
            hit = ((dx * dx) + (dy * dy)) <= (r * r);
        }
        if (!hit) {
            continue;
        }
        //! 重なっていたら**中心が近いほう**（並べ替えで重ねてしまっても、狙った方が勝つ）。
        const float dist = (dx * dx) + (dy * dy);
        if ((best < 0) || (dist < best_dist)) {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

void VirtualPad::update_stick(int control, float x, float y, const VirtualPadSettings &settings,
    const RectPx &area)
{
    const ControlGeom g = control_geom(control, settings, area);
    const float dx = x - g.cx;
    const float dy = y - g.cy;
    const float r = std::sqrt((dx * dx) + (dy * dy));
    if (static_cast<VpadControl>(control) == VpadControl::LS) {
        const float enter = g.radius * kStickEnterRatio;
        const float leave = g.radius * kStickLeaveRatio;
        if (r >= enter) {
            const float nx = dx / r;
            const float ny = dy / r;
            this->ls_dx_ = (nx > kOctantSin) ? 1 : ((nx < -kOctantSin) ? -1 : 0);
            this->ls_dy_ = (ny > kOctantSin) ? 1 : ((ny < -kOctantSin) ? -1 : 0);
        } else if (r <= leave) {
            this->ls_dx_ = 0;
            this->ls_dy_ = 0;
        }
        // enter と leave の間は前の向きのまま（ヒステリシス）
        return;
    }
    // RS: 連続値。死区の外側を 0〜1 へ引き伸ばす（`game_pad.cpp` の right_stick_axis と同じ形）。
    const float dead = g.radius * kAnalogDeadRatio;
    if (r <= dead) {
        this->rs_out_x_ = 0.f;
        this->rs_out_y_ = 0.f;
        return;
    }
    const float span = std::max(g.radius - dead, 1.f);
    const float mag = std::min((r - dead) / span, 1.f);
    this->rs_out_x_ = (dx / r) * mag;
    this->rs_out_y_ = (dy / r) * mag;
}

void VirtualPad::release_stick(int control)
{
    if (static_cast<VpadControl>(control) == VpadControl::LS) {
        /*
         * 短いタップの 1 歩（ヘッダの `tap_dx_` の注記）。**まだ 1 歩も出していない**
         * （`held_dx_/dy_` が中立のまま）向きが残っていたら、離しても捨てずに 1 歩ぶん取っておく。
         * 長押しで既に歩いた後の離しは何も足さない（1 歩余計に出る）。
         */
        if (((this->ls_dx_ != 0) || (this->ls_dy_ != 0)) && (this->held_dx_ == 0) && (this->held_dy_ == 0)) {
            this->tap_dx_ = this->ls_dx_;
            this->tap_dy_ = this->ls_dy_;
        }
        this->ls_dx_ = 0;
        this->ls_dy_ = 0;
    } else {
        this->rs_out_x_ = 0.f;
        this->rs_out_y_ = 0.f;
    }
}

void VirtualPad::forget_hold()
{
    for (Grab &g : this->grabs_) {
        g = Grab{};
    }
    this->pressed_.clear();
    //! 修飾を握ったままにしない（`GamePad::forget_hold` と同じ理由）。
    this->chord_.forget();
    this->ls_dx_ = 0;
    this->ls_dy_ = 0;
    this->held_dx_ = 0;
    this->held_dy_ = 0;
    this->tap_dx_ = 0;
    this->tap_dy_ = 0;
    this->next_repeat_ms_ = 0;
    this->rs_out_x_ = 0.f;
    this->rs_out_y_ = 0.f;
    this->touch_mouse_swallow_ = false;
    this->drag_active_ = false;
    this->pinch_active_ = false;
}

void VirtualPad::open_editor()
{
    this->forget_hold(); //!< ゲーム用の掴みを持ち越さない（押しっぱなしが残ると誤コマンド）
    this->editor_open_ = true;
    this->selected_ = -1;
}

void VirtualPad::close_editor()
{
    this->editor_open_ = false;
    this->selected_ = -1;
    this->drag_active_ = false;
    this->pinch_active_ = false;
}

bool VirtualPad::pointer_down(std::int64_t finger, float x, float y, VirtualPadSettings &settings,
    const RectPx &area)
{
    if (this->editor_open_) {
        //! 中央のボタンが最優先（ボタンの上へコントロールを重ねられても押せるように）。
        if (editor_button_rect(0, area).contains(static_cast<int>(x), static_cast<int>(y))) {
            //! **いま向いている側だけ**戻す（縦を直しに来て横まで消えると取り返しがつかない）。
            settings.reset_layout(vpad_orient_of(area));
            return true;
        }
        if (editor_button_rect(1, area).contains(static_cast<int>(x), static_cast<int>(y))) {
            this->close_editor();
            return true;
        }
        const int control = this->control_at(x, y, settings, area, true);
        if (!this->drag_active_) {
            if (control >= 0) {
                this->selected_ = control;
                this->drag_active_ = true;
                this->drag_finger_ = finger;
                const ControlGeom g = control_geom(control, settings, area);
                this->drag_off_x_ = g.cx - x;
                this->drag_off_y_ = g.cy - y;
                this->grabs_[0] = Grab{ true, finger, control, x, y }; //!< ピンチの距離計算に使う
            }
            return true;
        }
        if (!this->pinch_active_ && (this->selected_ >= 0)) {
            //! 2 本目の指はどこへ落ちてもピンチ（小さいボタンの上に 2 本は载らない）。
            this->pinch_active_ = true;
            this->pinch_finger_ = finger;
            this->grabs_[1] = Grab{ true, finger, -1, x, y };
            const float ddx = this->grabs_[0].x - x;
            const float ddy = this->grabs_[0].y - y;
            this->pinch_dist0_ = std::max(std::sqrt((ddx * ddx) + (ddy * ddy)), 8.f);
            this->pinch_scale0_ = std::clamp(settings.slot[vpad_orient_of(area)][this->selected_].scale,
                kScaleMin, kScaleMax);
        }
        return true;
    }

    if (!settings.show) {
        return false;
    }
    const int control = this->control_at(x, y, settings, area, false);
    if (control < 0) {
        return false; // パッドの外。タップ移動・掴みなど従来の道へ
    }
    Grab *const grab = this->free_grab();
    if (grab == nullptr) {
        return true; // 指が多すぎる。食うだけ食って何もしない
    }
    *grab = Grab{ true, finger, control, x, y };
    const auto c = static_cast<VpadControl>(control);
    if ((c == VpadControl::LS) || (c == VpadControl::RS)) {
        this->update_stick(control, x, y, settings, area);
    } else {
        /*
         * **同時押しは離しも見る**（`GamePad` と同じ `PadChord`）。修飾（L1/R1）は
         * 押した瞬間には出さず、単独だったと確定する離しで初めて出す。
         */
        const PadInput input = vpad_control_input(c);
        int mods = 0;
        if (!this->chord_.press(input, mods)) {
            this->pressed_.push_back(PadPress{ input, mods });
        }
    }
    return true;
}

bool VirtualPad::pointer_move(std::int64_t finger, float x, float y, VirtualPadSettings &settings,
    const RectPx &area)
{
    if (this->editor_open_) {
        if (this->drag_active_ && (finger == this->drag_finger_)) {
            this->grabs_[0].x = x;
            this->grabs_[0].y = y;
        } else if (this->pinch_active_ && (finger == this->pinch_finger_)) {
            this->grabs_[1].x = x;
            this->grabs_[1].y = y;
        } else {
            return true; // 編集中はどの指も食う
        }
        if (this->selected_ >= 0) {
            VpadSlot &slot = settings.slot[vpad_orient_of(area)][this->selected_];
            if (this->pinch_active_) {
                //! ピンチ中は**大きさだけ**動かす（動かすと拡縮が同時に走って狙いが定まらない）。
                const float ddx = this->grabs_[0].x - this->grabs_[1].x;
                const float ddy = this->grabs_[0].y - this->grabs_[1].y;
                const float dist = std::sqrt((ddx * ddx) + (ddy * ddy));
                slot.scale = std::clamp(this->pinch_scale0_ * (dist / this->pinch_dist0_), kScaleMin, kScaleMax);
                if (!slot.overridden()) {
                    slot.xf = default_spec(this->selected_, area).xf;
                    slot.yf = default_spec(this->selected_, area).yf;
                }
            } else {
                //! **置ける範囲に対する比**（`control_geom` と同じ基準。窓ではない）。
                slot.xf = std::clamp(
                    ((x + this->drag_off_x_) - static_cast<float>(area.x)) / static_cast<float>(std::max(1, area.w)),
                    0.02f, 0.98f);
                slot.yf = std::clamp(
                    ((y + this->drag_off_y_) - static_cast<float>(area.y)) / static_cast<float>(std::max(1, area.h)),
                    0.02f, 0.98f);
            }
        }
        return true;
    }

    Grab *const grab = this->find_grab(finger);
    if (grab == nullptr) {
        return false;
    }
    grab->x = x;
    grab->y = y;
    const auto c = static_cast<VpadControl>(grab->control);
    if ((c == VpadControl::LS) || (c == VpadControl::RS)) {
        this->update_stick(grab->control, x, y, settings, area);
    }
    return true;
}

bool VirtualPad::pointer_up(std::int64_t finger, VirtualPadSettings &settings, const RectPx &area)
{
    (void)settings;
    (void)area;
    if (this->editor_open_) {
        if (this->pinch_active_ && (finger == this->pinch_finger_)) {
            this->pinch_active_ = false;
        } else if (this->drag_active_ && (finger == this->drag_finger_)) {
            //! ドラッグの指を離したらピンチも終わり（残った 1 本を掴み直させる）。
            this->drag_active_ = false;
            this->pinch_active_ = false;
        }
        return true;
    }
    Grab *const grab = this->find_grab(finger);
    if (grab == nullptr) {
        return false;
    }
    const auto c = static_cast<VpadControl>(grab->control);
    if ((c == VpadControl::LS) || (c == VpadControl::RS)) {
        this->release_stick(grab->control);
    } else {
        //! 修飾を単独で押していたなら、離したいまが発火の瞬間である。
        PadInput solo = PadInput::Count;
        if (this->chord_.release(vpad_control_input(c), solo)) {
            this->pressed_.push_back(PadPress{ solo, 0 });
        }
    }
    *grab = Grab{};
    return true;
}

bool VirtualPad::on_event(const SDL_Event &event, VirtualPadSettings &settings, const RectPx &area,
    int window_w, int window_h)
{
    /*
     * **指の座標は窓に対する 0〜1**（`SDL_TouchFingerEvent`）なので、画素へ直すのに
     * 窓の実寸が要る。当たり判定に使う `area` は窓より狭いことがある（切り欠き）ので、
     * この 2 つは**別物**である。混ぜると切り欠きのある端末だけ指がずれる。
     */
    const float w = static_cast<float>(window_w);
    const float h = static_cast<float>(window_h);
    switch (event.type) {
    case SDL_FINGERDOWN:
        return this->pointer_down(event.tfinger.fingerId, event.tfinger.x * w, event.tfinger.y * h, settings, area);
    case SDL_FINGERMOTION:
        return this->pointer_move(event.tfinger.fingerId, event.tfinger.x * w, event.tfinger.y * h, settings, area);
    case SDL_FINGERUP:
        return this->pointer_up(event.tfinger.fingerId, settings, area);
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.which == SDL_TOUCH_MOUSEID) {
            /*
             * 指のマウス写し。**指の処理は FINGERDOWN が済ませている**ので、ここでは
             * 「パッドに落ちた指の写しを飲み込む」だけ。当たらなかった写しは通す
             * （タップ移動・決定は従来どおりこの写しで動いている）。
             *
             * **写しの座標（event.button.x/y）では判定しない。**一人称の間は視界を回す
             * ための相対マウスモードが効いていて、写しの座標は実際のタッチ位置から
             * ずれる。座標で当てていた頃は、スティックを離して触り直すと写しが
             * 「パッドの外」に見えて素通りし、一人称の「左クリック＝決定」に化けて
             * コマンドメニューが開いた（2026-08-12 に気づいた。**実機の一人称でだけ**
             * 出る——1 回目は直前のタッチ位置＝L3 の上に写しが残っていて偶然当たる）。
             * 指の実座標は SDL の指の表（`SDL_GetTouchFinger`）から引く。写しはずれても
             * 表は常に本物を持っている。
             */
            if (this->editor_open_) {
                return true;
            }
            bool on_pad = false;
            if (settings.show) {
                const int devices = SDL_GetNumTouchDevices();
                for (int d = 0; (d < devices) && !on_pad; ++d) {
                    const SDL_TouchID device = SDL_GetTouchDevice(d);
                    const int fingers = SDL_GetNumTouchFingers(device);
                    for (int i = 0; (i < fingers) && !on_pad; ++i) {
                        const SDL_Finger *const finger = SDL_GetTouchFinger(device, i);
                        if ((finger != nullptr)
                            && (this->control_at(finger->x * w, finger->y * h, settings, area, false) >= 0)) {
                            on_pad = true;
                        }
                    }
                }
            }
            if (on_pad) {
                this->touch_mouse_swallow_ = true;
                return true;
            }
            return false;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            //! 実マウス。タッチ画面の Windows と開発時の確認のため、左ボタンを指として扱う。
            return this->pointer_down(kMouseFinger, static_cast<float>(event.button.x),
                static_cast<float>(event.button.y), settings, area);
        }
        return this->editor_open_;
    case SDL_MOUSEMOTION:
        if (event.motion.which == SDL_TOUCH_MOUSEID) {
            return this->editor_open_ || this->touch_mouse_swallow_;
        }
        return this->pointer_move(kMouseFinger, static_cast<float>(event.motion.x),
            static_cast<float>(event.motion.y), settings, area);
    case SDL_MOUSEBUTTONUP:
        if (event.button.which == SDL_TOUCH_MOUSEID) {
            if (this->touch_mouse_swallow_) {
                this->touch_mouse_swallow_ = false;
                return true;
            }
            return this->editor_open_;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            return this->pointer_up(kMouseFinger, settings, area);
        }
        return this->editor_open_;
    case SDL_MOUSEWHEEL:
        //! 編集中はホイールも食う（裏でカメラの設定が回ると、閉じたとき絵が変わっている）。
        return this->editor_open_;
    default:
        return false;
    }
}

bool VirtualPad::take_pressed(PadPress &out)
{
    if (this->pressed_.empty()) {
        return false;
    }
    out = this->pressed_.front();
    this->pressed_.erase(this->pressed_.begin());
    return true;
}

bool VirtualPad::poll_direction(std::uint32_t now_ms, int &dx, int &dy)
{
    if ((this->tap_dx_ != 0) || (this->tap_dy_ != 0)) {
        //! 短いタップの 1 歩（`release_stick` の注記）。出したら忘れる。
        dx = this->tap_dx_;
        dy = this->tap_dy_;
        this->tap_dx_ = 0;
        this->tap_dy_ = 0;
        this->held_dx_ = 0;
        this->held_dy_ = 0;
        this->next_repeat_ms_ = now_ms + kFirstRepeatMs;
        return true;
    }
    const int now_dx = this->ls_dx_;
    const int now_dy = this->ls_dy_;
    if ((now_dx == 0) && (now_dy == 0)) {
        this->held_dx_ = 0;
        this->held_dy_ = 0;
        return false;
    }
    const bool changed = (now_dx != this->held_dx_) || (now_dy != this->held_dy_);
    if (changed) {
        //! **入れ替えでも間隔を置く**（`game_pad.cpp` の約束 4 と同じ）。
        this->held_dx_ = now_dx;
        this->held_dy_ = now_dy;
        this->next_repeat_ms_ = now_ms + kFirstRepeatMs;
        dx = now_dx;
        dy = now_dy;
        return true;
    }
    if (now_ms < this->next_repeat_ms_) {
        return false;
    }
    this->next_repeat_ms_ = now_ms + kNextRepeatMs;
    dx = now_dx;
    dy = now_dy;
    return true;
}

/* ------------------------------------------------------------------ 描画 */

namespace {

//! ラベルを中心へ（`TextOverlay` は左上原点なので測ってから引く）。
void draw_label_centred(TextOverlay &text, float cx, float cy, const std::string &label, const TextColor &color)
{
    const int width = text.measure(label);
    text.draw(static_cast<int>(cx) - (width / 2), static_cast<int>(cy) - (text.cell_h() / 2), label, color);
}

/*!
 * @brief 中心へ描く。**入らなければ 2 行に割る**。
 * @param max_width ボタンの中で使える幅（画素）。
 * @details 操作の名前は綴りより長い（`一人称の視界を水平に戻す` は 24 マス）。
 * 端で切ると何の操作か分からなくなるので、**符号位置の境目**で 2 つに割って重ねる
 * （多バイト文字の途中で切らない）。それでも入らない行は `TextOverlay` 側で切れる。
 */
void draw_label_fit(TextOverlay &text, float cx, float cy, const std::string &label, const TextColor &color,
    int max_width)
{
    if (label.empty()) {
        return;
    }
    const int cell_h = text.cell_h();
    if ((max_width <= 0) || (text.measure(label) <= max_width)) {
        draw_label_centred(text, cx, cy, label, color);
        return;
    }
    std::size_t split = 0;
    int best_diff = -1;
    for (std::size_t i = 1; i < label.size(); ++i) {
        if ((static_cast<unsigned char>(label[i]) & 0xC0) == 0x80) {
            continue; // 多バイト文字の途中
        }
        const int diff = std::abs(text.measure(label.substr(0, i)) - text.measure(label.substr(i)));
        if ((best_diff < 0) || (diff < best_diff)) {
            best_diff = diff;
            split = i;
        }
    }
    if (split == 0) {
        draw_label_centred(text, cx, cy, label, color);
        return;
    }
    const auto line = [&](const std::string &part, float y) {
        const int width = std::min(text.measure(part), max_width);
        text.draw(static_cast<int>(cx) - (width / 2), static_cast<int>(y) - (cell_h / 2), part, color, max_width);
    };
    line(label.substr(0, split), cy - (static_cast<float>(cell_h) * 0.5f));
    line(label.substr(split), cy + (static_cast<float>(cell_h) * 0.5f));
}

/*!
 * @brief ボタンに出す名前。**割り当てられている操作の名前**（無ければボタンの綴り）。
 * @details 固定の 3 つ（A＝決定・B＝取消・SELECT＝機能メニュー）は割り当ての表に
 * 載らない（`pad_input_is_fixed`）ので、ここで名指しする。**綴りは画面のほかの場所と
 * 揃えてある**（`feature_menu.cpp` の固定行）。
 */
std::string vpad_button_label(VpadControl control, const PadBinds &binds, const std::vector<PadCommand> &commands,
    const PadMacroFlags &macros, int chord_mask)
{
    const PadInput input = vpad_control_input(control);
    if (input == PadInput::Count) {
        return std::string(); // スティックには出さない
    }
    /*
     * **修飾を押さえている間は同時押しの層を出す**（2026-08-14 に決めた）。
     * 板の上では指を離さずに次を選ぶので、押さえた瞬間に中身が見えないと選べない。
     * 相手にならないのは**修飾そのもの**だけなので、そこだけ綴りのまま残す。
     */
    const bool in_chord = (chord_mask != 0) && !pad_input_is_modifier(input);
    if (in_chord) {
        //! 層でも規則は同じ（割り当て → マクロ → 綴り）。**単独押しと読み方を揃える。**
        if (binds.command[chord_mask][static_cast<int>(input)] == kActionNone) {
            return macros(input, chord_mask) ? i18n::tr("hd2d.ui.game-pad.macro") : vpad_control_name(control);
        }
    }
    /*
     * 決定・取消・メニューは**単独押しの役**（2026-08-14 に気づいた）。
     * 修飾を押さえている間は層の割り当てを出す——`LB＋A` に何か置いたのに
     * 「決定」と出ていては、押すまで分からない。
     */
    if (!in_chord) {
        if (input == PadInput::A) {
            return i18n::tr("hd2d.ui.feature-menu.confirm");
        }
        if (input == PadInput::B) {
            return i18n::tr("hd2d.ui.feature-menu.cancel");
        }
        if (input == PadInput::Back) {
            return i18n::tr("hd2d.ui.virtual-pad.menu");
        }
    }
    //! 層を押さえている間はその層、そうでなければ層 0（単独押し）。
    const int action = binds.command[(chord_mask >= 0 && chord_mask < kPadModLayerCount) ? chord_mask : 0]
                                    [static_cast<int>(input)];
    if (action != kActionNone) {
        const char *const ui = ui_action_label(action); //!< 負 = この exe 自身の操作
        if ((ui != nullptr) && (ui[0] != '\0')) {
            return ui;
        }
        for (const auto &command : commands) {
            if (command.id != action) {
                continue;
            }
            /*
             * コアの名前は `拾う(g)` のように**キーボードの綴りが末尾に付く**。
             * 指で押す板の上では意味を持たないうえ、狭いボタンでは名前を押し出すので落とす
             * （`穴を掘る(T/^t)` → `穴を掘る`）。落とすのは**末尾の 1 組だけ**で、
             * 名前の途中の括弧（`プレイ記録(|)` の類）は触らない。
             */
            const std::string &label = command.label_utf8;
            if (!label.empty() && (label.back() == ')')) {
                const std::size_t open = label.rfind('(');
                if ((open != std::string::npos) && (open > 0)) {
                    return label.substr(0, open);
                }
            }
            return label;
        }
    }
    /*
     * 割り当てが無く、**そのボタンにマクロが登録されている**なら「マクロ」（決めたこと
     * 2026-08-14）。指で押す板では綴り（`R1`）より何が起きるかの方が要る。
     *
     * **見るのは `action` そのもの**であって、上の探索が名前を引けたかどうかではない。
     * 割り当て済みなのにコアの表がまだ来ていない（＝名前が引けずここまで落ちてくる）ときに
     * 「マクロ」と出すと、走るのは割り当ての方なので嘘になる。
     */
    if ((action == kActionNone) && macros(input)) {
        return i18n::tr("hd2d.ui.game-pad.macro");
    }
    //! 割り当ても登録も無い／コアの表がまだ来ていない。**綴りを出す**（空白にしない）。
    return vpad_control_name(control);
}

} // namespace

void VirtualPad::draw(UiPaint &paint, TextOverlay &text, const VirtualPadSettings &settings, const RectPx &area,
    const PadMacroFlags &macros, int chord_mask, const PadBinds &binds,
    const std::vector<PadCommand> &commands) const
{
    if (!settings.show || this->editor_open_) {
        return; // 編集中は `draw_editor` が描く（同じものを 2 度描くと編集の下に薄い分身が出る）
    }
    const float u = size_basis(area);
    const float t = std::max(2.f, u * 0.004f); //!< 線の太さ
    //! 表示濃度（2026-08-12 に決めた）。**α にだけ**掛ける（色味は変えない）。
    const float op = std::clamp(settings.opacity, kVpadOpacityMin, kVpadOpacityMax);
    const auto dimmed = [op](const PaintColor &c) {
        return PaintColor{ c.r, c.g, c.b, std::min(c.a * op, 1.f) };
    };
    const auto dimmed_text = [op](const TextColor &c) {
        return TextColor{ c.r, c.g, c.b, std::min(c.a * op, 1.f) };
    };
    for (int i = 0; i < kVpadControlCount; ++i) {
        if (!settings.visible[i]) {
            continue;
        }
        const ControlGeom g = control_geom(i, settings, area);
        bool pressed = false;
        float knob_x = g.cx;
        float knob_y = g.cy;
        for (const Grab &grab : this->grabs_) {
            if (!grab.active || (grab.control != i)) {
                continue;
            }
            pressed = true;
            //! スティックのつまみは指へ寄せる（外周から出ない所まで）。
            const float dx = grab.x - g.cx;
            const float dy = grab.y - g.cy;
            const float len = std::sqrt((dx * dx) + (dy * dy));
            const float limit = std::max(g.radius - g.knob, 0.f);
            if ((len > 1.f) && (limit > 0.f)) {
                const float k = std::min(len, limit) / len;
                knob_x = g.cx + (dx * k);
                knob_y = g.cy + (dy * k);
            }
        }
        const PaintColor fill = dimmed(pressed ? kFillPressed : kFill);
        const PaintColor line = dimmed(pressed ? kLinePressed : kLine);
        const TextColor label = dimmed_text(pressed ? kLabelPressed : kLabel);
        //! **綴りではなく操作の名前**（2026-08-12 に決めた）。スティックには出ない。
        const std::string caption = vpad_button_label(static_cast<VpadControl>(i), binds, commands, macros, chord_mask);
        if (g.shape == 1) {
            const float rad = g.half_h * 0.45f;
            fill_round_rect(paint, g.cx, g.cy, g.half_w, g.half_h, rad, fill);
            frame_round_rect(paint, g.cx, g.cy, g.half_w, g.half_h, rad, t, line);
            draw_label_fit(text, g.cx, g.cy, caption, label, static_cast<int>(g.half_w * 1.9f));
        } else if (g.shape == 2) {
            fill_circle(paint, g.cx, g.cy, g.radius, dimmed(PaintColor{ kFill.r, kFill.g, kFill.b, 0.12f }), 48);
            ring(paint, g.cx, g.cy, g.radius - t, g.radius, line, 56);
            fill_circle(paint, knob_x, knob_y, g.knob,
                dimmed(pressed ? kFillPressed : PaintColor{ 0.75f, 0.78f, 0.88f, 0.35f }));
            ring(paint, knob_x, knob_y, g.knob - t, g.knob, line);
        } else {
            fill_circle(paint, g.cx, g.cy, g.radius, fill);
            ring(paint, g.cx, g.cy, g.radius - t, g.radius, line, 40);
            //! 円は幅が中心から離れるほど狭い。**直径いっぱいには書かない**（1.7 倍まで）。
            draw_label_fit(text, g.cx, g.cy, caption, label, static_cast<int>(g.radius * 1.7f));
        }
    }
}

void VirtualPad::draw_editor(UiPaint &paint, TextOverlay &text, const VirtualPadSettings &settings,
    const RectPx &area) const
{
    if (!this->editor_open_) {
        return;
    }
    const float u = size_basis(area);
    const float t = std::max(2.f, u * 0.004f);
    //! 黒背景（2026-08-12 に決めた「黒背景にバーチャルパッドを表示」）。
    paint.rect(area, PaintColor{ 0.02f, 0.02f, 0.04f, 1.f });

    for (int i = 0; i < kVpadControlCount; ++i) {
        const ControlGeom g = control_geom(i, settings, area);
        //! 非表示のボタンも**薄く**出す（消してあっても場所は変えられる。見えないと選べない）。
        const float dim = settings.visible[i] ? 1.f : 0.35f;
        const PaintColor fill{ 0.10f, 0.12f, 0.18f, 0.85f * dim };
        const PaintColor line{ 0.90f, 0.93f, 1.f, 0.9f * dim };
        const TextColor label{ 0.95f, 0.96f, 1.f, dim };
        if (g.shape == 1) {
            const float rad = g.half_h * 0.45f;
            fill_round_rect(paint, g.cx, g.cy, g.half_w, g.half_h, rad, fill);
            frame_round_rect(paint, g.cx, g.cy, g.half_w, g.half_h, rad, t, line);
            draw_label_centred(text, g.cx, g.cy, vpad_control_name(static_cast<VpadControl>(i)), label);
        } else if (g.shape == 2) {
            fill_circle(paint, g.cx, g.cy, g.radius, PaintColor{ 0.10f, 0.12f, 0.18f, 0.5f * dim }, 48);
            ring(paint, g.cx, g.cy, g.radius - t, g.radius, line, 56);
            fill_circle(paint, g.cx, g.cy, g.knob, PaintColor{ 0.75f, 0.78f, 0.88f, 0.5f * dim });
            ring(paint, g.cx, g.cy, g.knob - t, g.knob, line);
            draw_label_centred(text, g.cx, g.cy - g.radius - static_cast<float>(text.cell_h()), // 外周の上
                vpad_control_name(static_cast<VpadControl>(i)), label);
        } else {
            fill_circle(paint, g.cx, g.cy, g.radius, fill);
            ring(paint, g.cx, g.cy, g.radius - t, g.radius, line, 40);
            draw_label_centred(text, g.cx, g.cy, vpad_control_name(static_cast<VpadControl>(i)), label);
        }
        if (i == this->selected_) {
            //! 選択の印。形に沿った黄色い枠（どれを触っているかが一目で分かるように）。
            if (g.shape == 1) {
                frame_round_rect(paint, g.cx, g.cy, g.half_w + (t * 2.f), g.half_h + (t * 2.f), g.half_h * 0.45f,
                    t, kSelected);
            } else {
                ring(paint, g.cx, g.cy, g.radius + t, g.radius + (t * 2.f), kSelected, 56);
            }
        }
    }

    //! 中央のボタン（デフォルトに戻す / 戻る）。当たり判定と同じ矩形から描く。
    for (int which = 0; which < 2; ++which) {
        const RectPx r = editor_button_rect(which, area);
        const float cx = static_cast<float>(r.x) + (static_cast<float>(r.w) / 2.f);
        const float cy = static_cast<float>(r.y) + (static_cast<float>(r.h) / 2.f);
        fill_round_rect(paint, cx, cy, static_cast<float>(r.w) / 2.f, static_cast<float>(r.h) / 2.f,
            static_cast<float>(r.h) * 0.25f, PaintColor{ 0.16f, 0.18f, 0.26f, 0.95f });
        frame_round_rect(paint, cx, cy, static_cast<float>(r.w) / 2.f, static_cast<float>(r.h) / 2.f,
            static_cast<float>(r.h) * 0.25f, t, PaintColor{ 0.65f, 0.72f, 0.85f, 1.f });
        draw_label_centred(text, cx, cy, (which == 0) ? i18n::tr("hd2d.ui.feature-menu.restore-the-defaults") : i18n::tr("hd2d.ui.virtual-pad.back"),
            TextColor{ 0.95f, 0.96f, 1.f, 1.f });
    }

    /*
     * 操作の説明と、いま選んでいるもの。
     * **どちら向きを編集しているかを必ず出す**（2026-08-12 に決めた で配置は縦横別々に
     * なった。出さないと「直したのに戻っている」＝実は反対側を見ている、を切り分けられない）。
     */
    const int ch = text.cell_h();
    const bool portrait = (vpad_orient_of(area) == kVpadPortrait);
    char hint[192]{};
    std::snprintf(hint, sizeof(hint),
        i18n::tr("hd2d.ui.virtual-pad.button-layout-s-touch-to-select-drag"),
        portrait ? i18n::tr("hd2d.ui.virtual-pad.portrait") : i18n::tr("hd2d.ui.virtual-pad.landscape"));
    text.draw(area.x + ch, area.y + ch, hint, kEditorHint);
    if (this->selected_ >= 0) {
        const VpadSlot &slot = settings.slot[vpad_orient_of(area)][this->selected_];
        const float xf = slot.overridden() ? slot.xf : default_spec(this->selected_, area).xf;
        const float yf = slot.overridden() ? slot.yf : default_spec(this->selected_, area).yf;
        char line[128]{};
        std::snprintf(line, sizeof(line), i18n::tr("hd2d.ui.virtual-pad.selected-s-size-d-position-2f-2f"),
            vpad_control_name(static_cast<VpadControl>(this->selected_)),
            static_cast<int>(std::lround(static_cast<double>(std::clamp(slot.scale, kScaleMin, kScaleMax)) * 100.0)),
            static_cast<double>(xf), static_cast<double>(yf));
        text.draw(area.x + ch, area.y + (ch * 2) + (ch / 2), line, TextColor{ 1.f, 0.95f, 0.55f, 1.f });
    }
}

} // namespace hd2d
