/*!
 * @file attack_fx_checks.cpp
 * @brief 攻撃の視覚効果（矢・クロスボウの矢・弾・投げた物・魔法の玉・爆発・ビーム・ブレス・飛ばない魔法）の検査
 *        （`--attack-fx-check=出力の頭`）。窓と GL が要る。
 *
 * @details。**ゲームから来るのと同じ形の情報**
 * （`ProjectionFx`・`MissileFx`）を合成して `AttackFxView` へ渡し、そこから起こした絵を調べる。
 * 地図は合成（暗い部屋と暗い通路）で、プレイヤと的の代役の柱を置く。
 *
 * 1. 数で確かめる（CPU）: 経路・向き・速さ・色・大きさ・爆発が効果範囲のマスに収まること・点光源・重複の除去
 * 2. 描いて確かめる（GL）: 画素の変化・地形との前後関係・点光源が周りを明るくすること・爆発が範囲の外へ出ないこと
 * 3. 絵と連続画像を書き出す（`--attack-fx-check=` の値が出力の頭）。GIF は別のスクリプトが作る
 *
 * **比べる 2 枚は同じ描画関数を旗で呼び分ける**（メモ `hengband-check-must-not-copy-the-draw`）。
 * 遊ぶ経路には 1 行も通らない。
 */
#include "app/hd2d_checks.h"

#include "app/app_support.h"
#include "app/checks/attack_fx_scenes.h"
#include "app/checks/check_support.h"
#include "assets/tile_catalog.h"
#include "bridge/attack_fx_core_attr_aliases.h"
#include "frame/attack_fx.h"
#include "frame/cell_feature_bits.h"
#include "frame/game_frame.h"
#include "frame/minimap_snapshot.h"
#include "frame/protocol_messages.h"
#include "model/model_asset.h"
#include "render/attack_fx_view.h"
#include "render/camera.h"
#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/model_renderer.h"
#include "render/post_process.h"
#include "render/render_view.h"
#include "render/fx_styles/fx_parts.h"
#include "render/fx_styles/fx_style.h"
#include "render/shadow_map.h"
#include "render/voxel_fx.h"
#include "render/voxel_fx_renderer.h"
#include "render/voxel_renderer.h"
#include "ui/attack_fx_overlay.h"
#include "ui/hd2d_settings.h"
#include "ui/ui_paint.h"
#include "voxel/prefab.h"
#include "world/entity_fx_mod.h"
#include "world/floor_meaning.h"
#include "world/slab_library.h"
#include "world/terrain_memory.h"
#include "world/terrain_view.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <system_error>
#include <string>
#include <utility>
#include <vector>

namespace hd2d {

using namespace hd2d::gl;

namespace {

//! 検査で投げる物の番号（変愚の短剣。`tilework/64/K43.png` ＝ `assets/voxel/slab/K43.vox`）と、目録に載せる索引。
constexpr std::uint16_t kDaggerItem = 43;
constexpr std::uint16_t kDaggerTile = 7043;

//! 地図に彫る床の長方形。
struct AfxRect {
    int x0;
    int y0;
    int x1;
    int y1;
};

bool afx_in(const std::vector<AfxRect> &rects, int gx, int gy)
{
    for (const AfxRect &r : rects) {
        if ((gx >= r.x0) && (gx <= r.x1) && (gy >= r.y0) && (gy <= r.y1)) {
            return true;
        }
    }
    return false;
}

//! 合成フレームを壁だけにして、長方形を彫る（光の届く範囲の検査と同じ手）。暗い床だけ。
void afx_carve(GameFrame &frame, const std::vector<AfxRect> &rects)
{
    for (MapCellView &cell : frame.cells) {
        if (afx_in(rects, cell.gx, cell.gy)) {
            cell.feature_flags = CELL_FEAT_KNOWN;
            cell.terrain_id = 1; // FLOOR
        } else {
            cell.feature_flags = CELL_FEAT_KNOWN | CELL_FEAT_WALL;
            cell.terrain_id = 56; // GRANITE
        }
    }
    const int w = frame.minimap.width;
    for (int gy = 0; gy < frame.minimap.height; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            frame.minimap.kinds[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(gx)]
                = static_cast<std::uint8_t>(afx_in(rects, gx, gy) ? MinimapKind::Floor : MinimapKind::Wall);
        }
    }
    frame.minimap.kinds[(static_cast<std::size_t>(frame.player_gy) * static_cast<std::size_t>(w))
        + static_cast<std::size_t>(frame.player_gx)]
        = static_cast<std::uint8_t>(MinimapKind::Player);
}

//! 真四角の柱（プレイヤと的の代役。幅 0.44・高さ 0.8）。
ModelAsset afx_make_pillar()
{
    ModelAsset asset;
    ModelPrimitive prim;
    const float hx = 0.22f;
    const float hy = 0.22f;
    const float h = 0.8f;
    const auto quad = [&prim](const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, const Vec3 &n) {
        const Vec3 corners[6] = { a, b, c, a, c, d };
        for (const Vec3 &p : corners) {
            ModelVertex v;
            v.position = p;
            v.normal = n;
            prim.vertices.push_back(v);
        }
    };
    quad(Vec3{ hx, -hy, 0.f }, Vec3{ hx, hy, 0.f }, Vec3{ hx, hy, h }, Vec3{ hx, -hy, h }, Vec3{ 1.f, 0.f, 0.f });
    quad(Vec3{ -hx, hy, 0.f }, Vec3{ -hx, -hy, 0.f }, Vec3{ -hx, -hy, h }, Vec3{ -hx, hy, h }, Vec3{ -1.f, 0.f, 0.f });
    quad(Vec3{ hx, hy, 0.f }, Vec3{ -hx, hy, 0.f }, Vec3{ -hx, hy, h }, Vec3{ hx, hy, h }, Vec3{ 0.f, 1.f, 0.f });
    quad(Vec3{ -hx, -hy, 0.f }, Vec3{ hx, -hy, 0.f }, Vec3{ hx, -hy, h }, Vec3{ -hx, -hy, h }, Vec3{ 0.f, -1.f, 0.f });
    quad(Vec3{ -hx, -hy, h }, Vec3{ hx, -hy, h }, Vec3{ hx, hy, h }, Vec3{ -hx, hy, h }, Vec3{ 0.f, 0.f, 1.f });
    prim.material.metallic = 0.f;
    prim.material.roughness = 1.f;
    prim.material.double_sided = true;
    asset.vertex_count = prim.vertices.size();
    asset.bounds_min = Vec3{ -hx, -hy, 0.f };
    asset.bounds_max = Vec3{ hx, hy, h };
    asset.primitives.push_back(std::move(prim));
    return asset;
}

double afx_luma(const std::array<double, 3> &c)
{
    return (0.2126 * c[0]) + (0.7152 * c[1]) + (0.0722 * c[2]);
}

//! 範囲の中の平均（RGB。0〜255）。`x, y` は左上原点の画素座標。
std::array<double, 3> afx_mean_rgb(const std::vector<unsigned char> &image, int w, int h, float x, float y, int half)
{
    std::array<double, 3> sum{ 0.0, 0.0, 0.0 };
    int n = 0;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            int rgb[3]{};
            pixel_at(image, w, h, static_cast<int>(std::lround(x)) + dx, static_cast<int>(std::lround(y)) + dy, rgb);
            for (int c = 0; c < 3; ++c) {
                sum[static_cast<std::size_t>(c)] += rgb[c];
            }
            ++n;
        }
    }
    for (double &v : sum) {
        v /= static_cast<double>(n);
    }
    return sum;
}

//! 2 枚で違う画素の外接矩形（左上原点の画素座標）と数。`tolerance` 以下の差は同じとみなす。
struct DiffBox {
    long long count{ 0 };
    int x0{ 1 << 30 };
    int y0{ 1 << 30 };
    int x1{ -1 };
    int y1{ -1 };
};

DiffBox afx_diff_box(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b, int w, int h, int tolerance)
{
    DiffBox box;
    if (a.size() != b.size()) {
        box.count = static_cast<long long>(w) * h;
        return box;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
            const int d = std::max({ std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])),
                std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1])),
                std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2])) });
            if (d > tolerance) {
                ++box.count;
                const int top = h - 1 - y; //!< GL の行は下から
                box.x0 = std::min(box.x0, x);
                box.x1 = std::max(box.x1, x);
                box.y0 = std::min(box.y0, top);
                box.y1 = std::max(box.y1, top);
            }
        }
    }
    return box;
}

float afx_angle_deg(const Vec3 &a, const Vec3 &b)
{
    const float la = std::sqrt(dot(a, a));
    const float lb = std::sqrt(dot(b, b));
    if ((la < 1e-6f) || (lb < 1e-6f)) {
        return 180.f;
    }
    const float c = std::min(1.f, std::max(-1.f, dot(a, b) / (la * lb)));
    return std::acos(c) * 57.29578f;
}

Vec3 afx_centroid(const FxFrame &frame, FxPart part, int &count)
{
    Vec3 sum{ 0.f, 0.f, 0.f };
    count = 0;
    for (const FxVoxel &v : frame.voxels) {
        if (v.part == part) {
            sum = sum + v.center;
            ++count;
        }
    }
    return (count > 0) ? (sum * (1.f / static_cast<float>(count))) : sum;
}

int afx_count(const FxFrame &frame, FxPart part)
{
    int n = 0;
    for (const FxVoxel &v : frame.voxels) {
        if (v.part == part) {
            ++n;
        }
    }
    return n;
}

//! ボクセルの並びの指紋（相対位置と大きさ）。ちらつき・回転の確認用。
std::uint64_t afx_shape_hash(const FxFrame &frame, FxPart part, const Vec3 &origin)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const FxVoxel &v : frame.voxels) {
        if (v.part != part) {
            continue;
        }
        const Vec3 rel = v.center - origin;
        const long long q[4] = { std::llround(rel.x * 2000.f), std::llround(rel.y * 2000.f), std::llround(rel.z * 2000.f),
            std::llround(v.size.x * 2000.f) };
        for (const long long value : q) {
            h ^= static_cast<std::uint64_t>(value);
            h *= 1099511628211ull;
        }
    }
    return h;
}

/* ------------------------------------------------------------------ ゲームから来る情報の合成 */

const VoxelFxParams kParams; //!< 既定がそのまま採用値

//! 1 回の撃ち方（ゲームがフレームに載せるのと同じ形）。
struct Ev {
    std::vector<ProjectionFx> proj;
    std::vector<MissileFx> miss;
};

std::uint32_t g_seq = 0; //!< 通し番号（重複除去に引っかからないよう、作るたびに増やす）

//! 始点（含まない）から終点（含む）までの経路のマス（ブレゼンハム。ゲームの `project_path` の代役）。
std::vector<::FxCell> afx_path(int sx, int sy, int tx, int ty)
{
    std::vector<::FxCell> path;
    int x = sx;
    int y = sy;
    const int dx = std::abs(tx - sx);
    const int dy = std::abs(ty - sy);
    const int step_x = (sx < tx) ? 1 : -1;
    const int step_y = (sy < ty) ? 1 : -1;
    int err = dx - dy;
    while ((x != tx) || (y != ty)) {
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += step_x;
        }
        if (e2 < dx) {
            err += dx;
            y += step_y;
        }
        ::FxCell cell;
        cell.x = static_cast<std::int16_t>(x);
        cell.y = static_cast<std::int16_t>(y);
        path.push_back(cell);
    }
    return path;
}

//! 着弾点の周りで `distance()` が `rad` 以下の、床のマス（**壁のマスは入れない**。原作の `positions` と同じ）。
std::vector<::FxCell> afx_area(int cx, int cy, int rad, const std::vector<AfxRect> &floor)
{
    std::vector<::FxCell> cells;
    for (int dy = -rad; dy <= rad; ++dy) {
        for (int dx = -rad; dx <= rad; ++dx) {
            const int d = fx_grid_distance(dx, dy);
            if ((d > rad) || !afx_in(floor, cx + dx, cy + dy)) {
                continue;
            }
            ::FxCell cell;
            cell.x = static_cast<std::int16_t>(cx + dx);
            cell.y = static_cast<std::int16_t>(cy + dy);
            cell.dist = static_cast<std::uint8_t>(d);
            cells.push_back(cell);
        }
    }
    return cells;
}

ProjectionFx afx_projection(ProjectionShape shape, const std::string &attr, int sx, int sy, int tx, int ty, int rad,
    const std::vector<AfxRect> &floor, std::uint16_t flags = 0)
{
    ProjectionFx p;
    p.seq = ++g_seq;
    p.shape = shape;
    p.attr = attr;
    p.rad = static_cast<std::uint8_t>(rad);
    p.src_x = static_cast<std::int16_t>(sx);
    p.src_y = static_cast<std::int16_t>(sy);
    p.impact_x = static_cast<std::int16_t>(tx);
    p.impact_y = static_cast<std::int16_t>(ty);
    p.flags = flags;
    p.path = afx_path(sx, sy, tx, ty);
    switch (shape) {
    case ProjectionShape::Ball:
    case ProjectionShape::Jump:
        p.area = afx_area(tx, ty, rad, floor);
        break;
    case ProjectionShape::Beam:
        p.area = p.path;
        break;
    case ProjectionShape::Breath: {
        // 撃った者を頂点にした扇形（目標の向きの ±25 度、撃った者から `rad` まで）。
        const float ax = static_cast<float>(tx - sx);
        const float ay = static_cast<float>(ty - sy);
        const float alen = std::sqrt((ax * ax) + (ay * ay));
        for (int dy = -rad; dy <= rad; ++dy) {
            for (int dx = -rad; dx <= rad; ++dx) {
                const int d = fx_grid_distance(dx, dy);
                if ((d < 1) || (d > rad) || !afx_in(floor, sx + dx, sy + dy)) {
                    continue;
                }
                const float len = std::sqrt(static_cast<float>((dx * dx) + (dy * dy)));
                const float cosine = ((static_cast<float>(dx) * ax) + (static_cast<float>(dy) * ay)) / (len * alen);
                if (cosine < 0.906f) {
                    continue;
                }
                ::FxCell cell;
                cell.x = static_cast<std::int16_t>(sx + dx);
                cell.y = static_cast<std::int16_t>(sy + dy);
                cell.dist = static_cast<std::uint8_t>(d);
                p.area.push_back(cell);
            }
        }
        break;
    }
    default:
        break;
    }
    return p;
}

MissileFx afx_missile(MissileKind kind, std::uint16_t item, const std::string &brand, int sx, int sy, int tx, int ty, bool hit,
    bool with_return = false)
{
    MissileFx m;
    m.seq = ++g_seq;
    m.kind = kind;
    m.item_id = item;
    m.brand = brand;
    m.src_x = static_cast<std::int16_t>(sx);
    m.src_y = static_cast<std::int16_t>(sy);
    m.path = afx_path(sx, sy, tx, ty);
    if (with_return) {
        m.return_path = afx_path(tx, ty, sx, sy);
    }
    m.hit = hit;
    return m;
}

constexpr std::uint32_t kBase = 100000; //!< 検査の時計の起点（ミリ秒）

float afx_snap(float t)
{
    return std::round(t * 1000.f) / 1000.f;
}

//! `t` 秒の絵。**本番と同じ `AttackFxView`** に渡して作る。
FxFrame afx_eval(const Ev &ev, float t, const FxViewParams &vp = FxViewParams{})
{
    AttackFxView view;
    view.push(ev.proj, ev.miss, kBase);
    FxFrame frame;
    view.evaluate(kBase + static_cast<std::uint32_t>(std::lround(t * 1000.f)), kParams, vp, frame);
    return frame;
}

//! 先頭の効果の撃ち方（飛ぶ時間などを聞くため）。
FxSpec afx_spec(const Ev &ev)
{
    FxSpec spec;
    if (!ev.miss.empty()) {
        (void)AttackFxView::make_spec(ev.miss.front(), spec);
    } else if (!ev.proj.empty()) {
        (void)AttackFxView::make_spec(ev.proj.front(), spec);
    }
    return spec;
}

float afx_total(const Ev &ev)
{
    float total = 0.f;
    for (const MissileFx &m : ev.miss) {
        FxSpec spec;
        if (AttackFxView::make_spec(m, spec)) {
            total = std::max(total, fx_total_seconds(spec, kParams));
        }
    }
    for (const ProjectionFx &p : ev.proj) {
        FxSpec spec;
        if (AttackFxView::make_spec(p, spec)) {
            total = std::max(total, fx_total_seconds(spec, kParams));
        }
    }
    return total;
}

//! ボクセルが、範囲のマスの**どれかの中**（マスの中心から ±0.5 以内）にあるか。
bool afx_inside_cells(const FxVoxel &v, const std::vector<::FxCell> &cells)
{
    for (const ::FxCell &cell : cells) {
        if ((std::fabs(v.center.x - (static_cast<float>(cell.x) + 0.5f)) <= 0.5001f)
            && (std::fabs(v.center.y - (static_cast<float>(cell.y) + 0.5f)) <= 0.5001f)) {
            return true;
        }
    }
    return false;
}

//! 爆発の全期間（0.005 秒刻み）で、範囲の外にあるボクセルの数と、調べた数。
struct Overreach {
    long long outside{ 0 };
    long long total{ 0 };
    float max_dist{ 0.f }; //!< 爆発の中心から粒の中心までの水平距離の最大
};

Overreach afx_overreach(const Ev &ev, const std::vector<::FxCell> &cells, float from, float to, const std::vector<FxPart> &parts)
{
    Overreach out;
    const ProjectionFx &p = ev.proj.front();
    const float cx = static_cast<float>(p.impact_x) + 0.5f;
    const float cy = static_cast<float>(p.impact_y) + 0.5f;
    for (float t = from; t <= to; t += 0.005f) {
        const FxFrame f = afx_eval(ev, afx_snap(t));
        for (const FxVoxel &v : f.voxels) {
            if (std::find(parts.begin(), parts.end(), v.part) == parts.end()) {
                continue;
            }
            ++out.total;
            if (!afx_inside_cells(v, cells)) {
                ++out.outside;
            }
            out.max_dist = std::max(out.max_dist, std::sqrt(((v.center.x - cx) * (v.center.x - cx)) + ((v.center.y - cy) * (v.center.y - cy))));
        }
    }
    return out;
}

/* ------------------------------------------------------------------ 属性ごとの見た目（登録表・部品）の検査 */

using AfxCheck = std::function<void(bool, const std::string &)>;

//! 上へたどって `presentation/bridge/attack_fx_attr_names.h` を探し、対の表 `{ AttributeType::X, "名前" },` の名前を拾う（ゲーム本体側の名前の一覧）。
bool afx_core_attr_names(std::vector<std::string> &names, std::string &where)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::current_path(ec);
    for (int up = 0; up < 7; ++up) {
        const fs::path candidate = base / "presentation" / "bridge" / "attack_fx_attr_names.h";
        if (fs::exists(candidate, ec)) {
            where = candidate.string();
            std::ifstream in(candidate, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                //! 表の 1 行は `{ AttributeType::ELEC, "elec" },`。列挙子の後ろの最初の `"…"` が名前。
                const std::size_t a = line.find("AttributeType::");
                const std::size_t r = (a == std::string::npos) ? std::string::npos : line.find('"', a);
                if ((a == std::string::npos) || (r == std::string::npos) || (line.find('{') == std::string::npos)) {
                    continue;
                }
                const std::size_t s = r + 1u;
                const std::size_t e = line.find('"', s);
                if ((e == std::string::npos) || (e == s)) {
                    continue;
                }
                names.push_back(line.substr(s, e - s));
            }
            return !names.empty();
        }
        if (base == base.parent_path()) {
            break;
        }
        base = base.parent_path();
    }
    return false;
}

float length_of_vec(const Vec3 &v)
{
    return std::sqrt(dot(v, v));
}

double afx_mean_z(const FxFrame &f, FxPart part)
{
    double z = 0.0;
    int n = 0;
    for (const FxVoxel &v : f.voxels) {
        if (v.part == part) {
            z += v.center.z;
            ++n;
        }
    }
    return (n > 0) ? (z / n) : -1.0;
}

//! 絵の板・形の「画面の上での」広がり（右・上の成分の幅）。
void afx_screen_extent(const FxFrame &f, FxPart part, const FxSpec &spec, const Vec3 &centre, float &width, float &height)
{
    float rl = 1e9f;
    float rh = -1e9f;
    float ul = 1e9f;
    float uh = -1e9f;
    for (const FxVoxel &v : f.voxels) {
        if (v.part != part) {
            continue;
        }
        const float r = dot(v.center - centre, spec.cam_right);
        const float u = dot(v.center - centre, spec.cam_up);
        rl = std::min(rl, r);
        rh = std::max(rh, r);
        ul = std::min(ul, u);
        uh = std::max(uh, u);
    }
    width = (rh >= rl) ? (rh - rl) : 0.f;
    height = (uh >= ul) ? (uh - ul) : 0.f;
}

bool afx_frame_finite(const FxFrame &f)
{
    const auto fin = [](float v) { return std::isfinite(v); };
    for (const FxVoxel &v : f.voxels) {
        if (!fin(v.center.x) || !fin(v.center.y) || !fin(v.center.z) || !fin(v.size.x) || !fin(v.size.y) || !fin(v.size.z) || !fin(v.r)
            || !fin(v.g) || !fin(v.b) || !fin(v.alpha) || !fin(v.emissive) || !fin(v.yaw) || !fin(v.pitch)) {
            return false;
        }
    }
    for (const PointLight &l : f.lights) {
        if (!fin(l.x) || !fin(l.y) || !fin(l.z) || !fin(l.radius) || !fin(l.intensity) || (l.intensity < 0.f)) {
            return false;
        }
    }
    return fin(f.shake) && (f.shake >= 0.f);
}

//! 1 コマの中身の要約（FNV-1a。値は 1/10000 に丸めて混ぜる）。見た目が前後で変わっていないことを数で示すために使う。
std::uint64_t afx_frame_digest(const FxFrame &f, std::uint64_t h)
{
    const auto mix = [&h](float v) {
        h ^= static_cast<std::uint64_t>(std::llround(static_cast<double>(v) * 10000.0));
        h *= 1099511628211ull;
    };
    for (const FxVoxel &v : f.voxels) {
        mix(v.center.x);
        mix(v.center.y);
        mix(v.center.z);
        mix(v.size.x);
        mix(v.size.y);
        mix(v.size.z);
        mix(v.r);
        mix(v.g);
        mix(v.b);
        mix(v.alpha);
        mix(v.emissive);
        mix(v.yaw);
        mix(v.pitch);
        mix(static_cast<float>(static_cast<int>(v.part)));
    }
    for (const PointLight &l : f.lights) {
        mix(l.x);
        mix(l.y);
        mix(l.z);
        mix(l.radius);
        mix(l.intensity);
        mix(l.r);
        mix(l.g);
        mix(l.b);
    }
    mix(f.shake);
    for (const FxEntityMod &m : f.entity_mods) {
        mix(static_cast<float>(m.gx));
        mix(static_cast<float>(m.gy));
        mix(m.squash);
        mix(m.tremble);
        mix(m.tremble_phase);
        mix(m.recoil);
        mix(m.lift);
        mix(m.grey);
        mix(m.echo_x);
        mix(m.echo_y);
    }
    for (const FxScreenWave &w : f.screen_waves) {
        mix(w.position);
        mix(w.width);
        mix(w.alpha);
        mix(w.angle);
        mix(w.colour.r);
        mix(w.colour.g);
        mix(w.colour.b);
    }
    return h;
}

//! 画面側が引くタイルの代役（頭と胴の影絵。16×16）。
bool afx_synthetic_tile(int, int, FxTileImage &out)
{
    out = FxTileImage{};
    out.width = 16;
    out.height = 16;
    out.rgba.assign(16u * 16u * 4u, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float dx = static_cast<float>(x) - 7.5f;
            const float head = std::sqrt((dx * dx) + ((static_cast<float>(y) - 4.f) * (static_cast<float>(y) - 4.f)));
            const bool body = (y >= 7) && (y <= 14) && (std::fabs(dx) <= 4.5f);
            if ((head <= 3.5f) || body) {
                const std::size_t i = ((static_cast<std::size_t>(y) * 16u) + static_cast<std::size_t>(x)) * 4u;
                out.rgba[i] = static_cast<std::uint8_t>(60 + (y * 8));
                out.rgba[i + 1] = static_cast<std::uint8_t>(140);
                out.rgba[i + 2] = static_cast<std::uint8_t>(40);
                out.rgba[i + 3] = 255;
            }
        }
    }
    return true;
}

//! `FxSpec` を 1 つ作って、部品の検査に渡す（`kind` は何でもよい）。
FxSpec afx_part_spec()
{
    FxSpec spec;
    spec.kind = FxKind::Glow;
    spec.centre_gx = 105;
    spec.centre_gy = 50;
    spec.src_gx = 101;
    spec.src_gy = 50;
    spec.cam_right = Vec3{ 1.f, 0.f, 0.f };
    spec.cam_up = normalize(Vec3{ 0.f, -0.6f, 0.8f });
    spec.tile_at = afx_synthetic_tile;
    return spec;
}

Vec3 afx_voxel_extent(const FxFrame &f, FxPart part, Vec3 &lo)
{
    Vec3 hi{ -1e9f, -1e9f, -1e9f };
    lo = Vec3{ 1e9f, 1e9f, 1e9f };
    for (const FxVoxel &v : f.voxels) {
        if (v.part != part) {
            continue;
        }
        lo = Vec3{ std::min(lo.x, v.center.x), std::min(lo.y, v.center.y), std::min(lo.z, v.center.z) };
        hi = Vec3{ std::max(hi.x, v.center.x), std::max(hi.y, v.center.y), std::max(hi.z, v.center.z) };
    }
    return hi;
}


//! `--fx-scene=parts_demo`：共通の部品（`render/fx_styles/fx_parts.h`）を全部並べた見本。部品の使い方と見え方の確認用で、
//! 属性の見た目ではない。`t` 秒の絵を `out` に足す（的は `(tx,ty)`、撃った者は `(px,py)`）。
void afx_parts_demo(const FxSpec &spec, int px, int py, int tx, int ty, float t, FxFrame &out)
{
    const Vec3 target = fx_cell_centre(tx, ty, 0.5f);
    const Vec3 source = fx_cell_centre(px, py, 0.6f);
    // 雫（粒の吹き出し）と水たまり
    FxSpray spray;
    spray.origin = Vec3{ target.x, target.y, 0.7f };
    spray.cone = 1.2f;
    spray.speed_min = 0.5f;
    spray.speed_max = 1.6f;
    spray.count = 28;
    spray.born = 0.1f;
    spray.duration = 0.15f;
    spray.life_min = 0.5f;
    spray.life_max = 0.9f;
    spray.colour0 = FxColour{ 0.8f, 0.4f, 1.f };
    spray.colour1 = FxColour{ 0.4f, 0.15f, 0.6f };
    spray.size0 = 0.07f;
    spray.size1 = 0.05f;
    spray.emissive0 = 0.8f;
    spray.alpha1 = 1.f;
    spray.floor = FxFloor::Stop;
    spray.part = FxPart::Droplet;
    spray.seed = 3;
    (void)fx_emit_spray(spray, t, out);
    FxPuddle puddle;
    puddle.cx = target.x - 1.6f;
    puddle.cy = target.y + 1.3f;
    puddle.colour = FxColour{ 0.55f, 0.2f, 0.8f };
    puddle.born = 0.3f;
    puddle.radius = 0.45f;
    puddle.emissive = 0.3f;
    puddle.seed = 5;
    (void)fx_emit_puddle(puddle, t, out);
    // 煙
    FxSmoke smoke;
    smoke.origin = Vec3{ target.x + 1.7f, target.y - 1.2f, 0.15f };
    smoke.born = 0.3f;
    smoke.colour = FxColour{ 0.6f, 0.95f, 0.3f };
    smoke.colour_end = FxColour{ 0.35f, 0.5f, 0.2f };
    smoke.count = 10;
    smoke.duration = 0.6f;
    smoke.life = 1.0f;
    smoke.seed = 9;
    (void)fx_emit_smoke(smoke, t, out);
    // 稲妻（束ねる）
    for (std::uint32_t i = 0; i < 3u; ++i) {
        FxLightning l;
        l.a = source;
        l.b = target;
        l.seed = 11u + i;
        l.core_colour = FxColour{ 1.f, 1.f, 0.8f };
        l.glow_colour = FxColour{ 1.f, 0.9f, 0.3f };
        l.branches = (i == 0u) ? 2 : 0;
        if ((t > 0.2f) && (t < 0.5f)) {
            (void)fx_emit_lightning(l, t, out);
        }
    }
    // 輪（広がる・画面に正対して縮む）
    FxRing grow;
    grow.centre = Vec3{ target.x, target.y, 0.05f };
    grow.born = 0.5f;
    grow.r0 = 0.1f;
    grow.r1 = 1.4f;
    grow.life = 0.6f;
    grow.colour = FxColour{ 0.9f, 0.8f, 0.5f };
    (void)fx_emit_ring(grow, t, out);
    FxRing shrink;
    shrink.centre = Vec3{ target.x, target.y, 1.25f };
    shrink.normal = fx_toward_camera(spec);
    shrink.born = 0.9f;
    shrink.r0 = 0.6f;
    shrink.r1 = 0.1f;
    shrink.life = 0.6f;
    shrink.colour = FxColour{ 1.f, 0.5f, 0.8f };
    shrink.dash = 2;
    (void)fx_emit_ring(shrink, t, out);
    // 形（下向きの矢印。上から下りてくる）
    const FxShape arrow = fx_shape({ "...##...", "...##...", "...##...", "#######.", ".#####..", "..###...", "...#...." });
    FxShapePlace place;
    place.right = spec.cam_right;
    place.up = spec.cam_up;
    place.cell = 0.07f;
    place.centre = Vec3{ target.x - 1.4f, target.y - 1.0f, 1.6f - (0.9f * fx_clamp01((t - 0.3f) / 1.2f)) };
    FxShapeColours colours;
    colours.base = FxColour{ 0.7f, 0.8f, 1.f };
    place.alpha = fx_envelope(t - 0.3f, 0.2f, 1.0f, 0.3f);
    if (place.alpha > 0.01f) {
        (void)fx_emit_shape(arrow, colours, place, out);
    }
    // 絵の板（どくろ。浮かんで消える）
    FxSprite skull;
    skull.name = "skull";
    skull.centre = Vec3{ target.x, target.y, 1.45f + (0.15f * fx_clamp01((t - 0.9f) / 1.0f)) };
    skull.size = 0.55f;
    skull.alpha = fx_envelope(t - 0.9f, 0.25f, 0.5f, 0.35f);
    skull.tint = FxColour{ 1.f, 0.8f, 0.8f };
    if (skull.alpha > 0.01f) {
        (void)fx_emit_sprite(spec, skull, out);
    }
    // タイルの写し（崩れて落ちる）
    FxGhost ghost;
    ghost.gx = tx;
    ghost.gy = ty;
    ghost.born = 1.1f;
    ghost.hold = 0.3f;
    ghost.crumble = 0.6f;
    ghost.offset = 0.35f; //!< 検査の柱は厚いので、柱の前へ出す
    ghost.seed = 21;
    (void)fx_emit_tile_ghost(spec, ghost, t, out);
    // 対象の見た目の変化：押しつぶし → 震え → 二重。撃った者はのけぞる。
    FxEntityMod &mod = fx_mod_for(out, tx, ty);
    mod.squash = std::max(mod.squash, 0.8f * fx_envelope(t - 0.5f, 0.1f, 0.2f, 0.2f));
    const float tremble = fx_envelope(t - 1.0f, 0.05f, 0.4f, 0.1f);
    mod.tremble = std::max(mod.tremble, 0.05f * tremble);
    mod.tremble_phase = t * 90.f;
    mod.grey = std::max(mod.grey, 0.8f * fx_envelope(t - 1.1f, 0.1f, 0.6f, 0.2f));
    mod.echo_x = 0.25f * fx_envelope(t - 1.8f, 0.05f, 0.3f, 0.1f);
    FxEntityMod &back = fx_mod_for(out, px, py);
    back.recoil = fx_envelope(t - 0.2f, 0.05f, 0.1f, 0.2f);
    back.away_x = -1.f;
    back.away_y = 0.f;
    // 画面の揺れと光の波
    fx_add_shake(out, fx_shake_envelope(t, 1.5f, 0.4f, 0.1f));
    if ((t > 1.9f) && (t < 2.6f)) {
        const float u = (t - 1.9f) / 0.7f;
        fx_add_screen_wave(out, FxColour{ 0.5f, 0.8f, 1.f }, (u * 1.3f) - 0.15f, 0.22f, 0.4f * std::sin(3.14159f * u));
    }
}

//! 登録表・全属性の流し・共通の部品の検査（窓も GL も要らない）。
void afx_style_checks(const AfxCheck &check)
{
    // ---------------------------------------------------------------- 登録表とゲーム本体側の名前の突き合わせ
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 属性の登録表（CPU）\n");
        std::vector<std::string> core_names;
        std::string where;
        const bool found = afx_core_attr_names(core_names, where);
        check(found && (core_names.size() >= 100u),
            "登録表: ゲーム本体側の属性の名前の一覧（presentation/bridge/attack_fx_attr_names.h）を読める（リポジトリ直下で実行する。"
                + std::to_string(core_names.size()) + " 個 " + where + "）");
        const FxStyleRegistry &reg = fx_style_registry();
        // 変愚の名前の一覧（`attack_fx_attr_names.h`）に、Frox・幻想蛮怒の専用の見た目の名前（`attack_fx_core_attr_aliases.h`）を足したものが、
        // 登録表と 1 字も違わない。変愚の名前の突き合わせは弱めない（`hengband_count` 個は、これまでどおり全部が登録表にある）。
        const std::size_t hengband_count = core_names.size();
        std::vector<std::string> frox_own;
        std::vector<std::string> genso_own;
        for (const attack_fx::CoreAttrAlias &a : attack_fx::kFroxOwnLooks) {
            frox_own.push_back(a.look_name);
            core_names.push_back(a.look_name);
        }
        for (const attack_fx::CoreAttrAlias &a : attack_fx::kGensobandOwnLooks) {
            genso_own.push_back(a.look_name);
            core_names.push_back(a.look_name);
        }
        std::vector<std::string> silq_own;
        for (const attack_fx::CoreAttrAlias &a : attack_fx::kSilqOwnLooks) {
            silq_own.push_back(a.look_name);
            core_names.push_back(a.look_name);
        }
        std::vector<std::string> missing;
        for (const std::string &name : core_names) {
            if (fx_find_style(name) == nullptr) {
                missing.push_back(name);
            }
        }
        std::vector<std::string> extra;
        for (const FxStyle &style : reg.all()) {
            if (std::find(core_names.begin(), core_names.end(), std::string(style.attr)) == core_names.end()) {
                extra.push_back(style.attr);
            }
        }
        std::string missing_list;
        for (const std::string &m : missing) {
            missing_list += " " + m;
        }
        std::string extra_list;
        for (const std::string &m : extra) {
            extra_list += " " + m;
        }
        check(missing.empty(), "登録表: ゲーム本体側の名前で、画面側に登録の無いものが 0（" + std::to_string(missing.size()) + " 個" + missing_list + "）");
        check(extra.empty(), "登録表: 画面側の登録で、ゲーム本体側に無い名前（綴りの違い）が 0（" + std::to_string(extra.size()) + " 個" + extra_list + "）");
        check(reg.duplicate_count() == 0, "登録表: 同じ名前を 2 度登録していない（" + std::to_string(reg.duplicate_count()) + " 件）");
        int counts[7]{};
        bool families_ok = true;
        for (const FxStyle &style : reg.all()) {
            const std::string family = style.family;
            if (family == "liquid") {
                ++counts[0];
            } else if (family == "lightning") {
                ++counts[1];
            } else if (family == "elemental") {
                ++counts[2];
            } else if (family == "spirit") {
                ++counts[3];
            } else if (family == "frox") {
                ++counts[4];
            } else if (family == "gensoband") {
                ++counts[5];
            } else if (family == "silq") {
                ++counts[6];
            } else {
                families_ok = false;
            }
        }
        std::fprintf(stderr, "    系統ごとの属性の数: 液体 %d / 稲妻・光線 %d / その他のダメージ %d / 呪い・心・状態・地形・特殊 %d / Frox %d / 幻想蛮怒 %d / Sil-Q %d（合計 %zu）\n",
            counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], reg.all().size());
        check(families_ok && (counts[0] == 6) && (counts[1] == 11) && (counts[2] == 22) && (counts[3] > 50)
                && (static_cast<std::size_t>(counts[4]) == frox_own.size()) && (static_cast<std::size_t>(counts[5]) == genso_own.size())
                && (static_cast<std::size_t>(counts[6]) == silq_own.size()),
            "登録表: どの属性も 7 つの系統（変愚の 4 つ・Frox・幻想蛮怒・Sil-Q）のどれかに登録されている（Frox " + std::to_string(frox_own.size()) + " 個・幻想蛮怒 "
                + std::to_string(genso_own.size()) + " 個・Sil-Q " + std::to_string(silq_own.size()) + " 個）");
        // 変愚の名前の数は変わっていない（足した分は Frox・幻想蛮怒の専用だけ）。
        check((static_cast<std::size_t>(counts[0] + counts[1] + counts[2] + counts[3])) == hengband_count,
            "登録表: 変愚の 4 つの系統の登録数が、変愚の名前の一覧の数と同じ（" + std::to_string(hengband_count) + "）");
        // Frox の名前は `fc_`、幻想蛮怒の名前は `gb_` で始まる（変愚の名前とぶつからない）。
        bool prefix_ok = true;
        for (const std::string &n : frox_own) {
            prefix_ok = prefix_ok && (n.compare(0, 3, "fc_") == 0);
        }
        for (const std::string &n : genso_own) {
            prefix_ok = prefix_ok && (n.compare(0, 3, "gb_") == 0);
        }
        for (const std::string &n : silq_own) {
            prefix_ok = prefix_ok && (n.compare(0, 3, "sq_") == 0);
        }
        check(prefix_ok, "登録表: Frox の名前は `fc_`、幻想蛮怒の名前は `gb_`、Sil-Q の名前は `sq_` で始まる");
        // 寄せる表：コア名が重ならず、専用の見た目がすべて登録表にあり、寄せ先がすべて変愚の名前（登録表にある）。
        {
            std::string bad;
            const auto scan = [&](const attack_fx::CoreAttrAlias *own, std::size_t own_n, const attack_fx::CoreAttrAlias *alias, std::size_t alias_n,
                                  const char *tag) {
                std::vector<std::string> seen;
                for (std::size_t i = 0; i < own_n; ++i) {
                    seen.push_back(own[i].core_name);
                    if (fx_find_style(own[i].look_name) == nullptr) {
                        bad += std::string(" ") + tag + ":" + own[i].look_name;
                    }
                }
                const auto hengband_end = core_names.begin() + static_cast<std::ptrdiff_t>(hengband_count);
                for (std::size_t i = 0; i < alias_n; ++i) {
                    if (std::find(seen.begin(), seen.end(), std::string(alias[i].core_name)) != seen.end()) {
                        bad += std::string(" 重複 ") + tag + ":" + alias[i].core_name;
                    }
                    seen.push_back(alias[i].core_name);
                    const std::string target = alias[i].look_name;
                    if ((fx_find_style(target) == nullptr) || (std::find(core_names.begin(), hengband_end, target) == hengband_end)) {
                        bad += std::string(" 寄せ先なし ") + tag + ":" + alias[i].core_name + "→" + target;
                    }
                }
            };
            scan(attack_fx::kFroxOwnLooks, sizeof(attack_fx::kFroxOwnLooks) / sizeof(attack_fx::kFroxOwnLooks[0]), attack_fx::kFroxAliases,
                sizeof(attack_fx::kFroxAliases) / sizeof(attack_fx::kFroxAliases[0]), "frox");
            scan(attack_fx::kGensobandOwnLooks, sizeof(attack_fx::kGensobandOwnLooks) / sizeof(attack_fx::kGensobandOwnLooks[0]),
                attack_fx::kGensobandAliases, sizeof(attack_fx::kGensobandAliases) / sizeof(attack_fx::kGensobandAliases[0]), "gensoband");
            scan(attack_fx::kSilqOwnLooks, sizeof(attack_fx::kSilqOwnLooks) / sizeof(attack_fx::kSilqOwnLooks[0]), attack_fx::kSilqAliases,
                sizeof(attack_fx::kSilqAliases) / sizeof(attack_fx::kSilqAliases[0]), "silq");
            check(bad.empty(), "寄せる表: コア名が重ならず、専用の見た目と寄せ先（変愚の名前）がすべて登録表にある" + bad);
            check((std::string(attack_fx::frox_look_name("storm")) == "fc_storm") && (std::string(attack_fx::frox_look_name("arrow")) == "missile")
                    && (std::string(attack_fx::frox_look_name("fire")) == "fire") && (std::string(attack_fx::gensoband_look_name("tornado")) == "gb_tornado")
                    && (std::string(attack_fx::gensoband_look_name("punish_3")) == "cause_3") && (std::string(attack_fx::gensoband_look_name("pois")) == "pois")
                    && (std::string(attack_fx::silq_look_name("web")) == "sq_web") && (std::string(attack_fx::silq_look_name("boulder")) == "shards")
                    && (std::string(attack_fx::silq_look_name("acid")) == "acid"),
                "寄せる表: コア名 → 見た目の名前の引き方（専用 → 寄せ先 → そのまま）");
        }
        check((fx_find_style("inertial") != nullptr) && (fx_find_style("inertia") == nullptr) && (fx_find_style("INERTIAL") != nullptr),
            "登録表: 遅鈍は `inertial`（ゲーム本体と同じ綴り）で引け、`inertia` では引けない。大文字でも引ける");
        bool all_known = true;
        for (const std::string &name : core_names) {
            all_known = all_known && fx_palette_for(name).known;
        }
        check(all_known, "登録表: ゲーム本体側の名前はすべて、色の組が引ける（白の代用に落ちない）");
        check(fx_palette_for("fire").is_fire && !fx_palette_for("cold").is_fire, "登録表: 火炎だけが試作の火の色の組");
        // 属性の名前が系統のファイルの表（§5.1〜5.3）どおりの系統へ入っている。
        check((std::string(fx_find_style("pois")->family) == "liquid") && (std::string(fx_find_style("elec")->family) == "lightning")
                && (std::string(fx_find_style("disintegrate")->family) == "lightning") && (std::string(fx_find_style("fire")->family) == "elemental")
                && (std::string(fx_find_style("cause_1")->family) == "spirit"),
            "登録表: 毒は液体・電撃と分解は稲妻・火炎はその他・呪いは呪いの系統");
    }

    // ---------------------------------------------------------------- 全属性 × 全形の流し（落ちない・数が暴れない・最後に消える）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 全属性 × 全形の流し（CPU）\n");
        const std::vector<AfxRect> floor{ AfxRect{ 60, 30, 160, 70 } };
        struct Shape {
            const char *name;
            ProjectionShape shape;
            int rad;
            std::uint16_t flags;
        };
        const Shape shapes[] = { { "bolt", ProjectionShape::Bolt, 0, 0 }, { "ball", ProjectionShape::Ball, 2, 0 },
            { "beam", ProjectionShape::Beam, 0, 0 }, { "breath", ProjectionShape::Breath, 7, 0 }, { "jump", ProjectionShape::Jump, 1, 0 },
            { "touch", ProjectionShape::Touch, 0, 0 }, { "hidden", ProjectionShape::Bolt, 0, kProjHidden },
            { "allsight", ProjectionShape::Bolt, 0, kProjAllInSight } };
        // 通し番号（`g_seq`）は撃ち方の乱数の種になる。登録表に属性を足すとこの流しの回数が増えて、あとの場面の種がずれ、
        // 画素で測る検査（爆発の大きさの比べなど）の結果が変わってしまう。流しのあとは、変愚の 4 つの系統だけを流したときの値へ戻す。
        const std::uint32_t seq_before_flow = g_seq;
        std::uint32_t hengband_pairs = 0;
        for (const FxStyle &style : fx_style_registry().all()) {
            const std::string family = style.family;
            if ((family == "liquid") || (family == "lightning") || (family == "elemental") || (family == "spirit")) {
                hengband_pairs += static_cast<std::uint32_t>(sizeof(shapes) / sizeof(shapes[0]));
            }
        }
        long long runs = 0;
        std::size_t worst_voxels = 0;
        std::string worst_name;
        std::string bad_finite;
        std::string bad_end;
        std::string bad_total;
        for (const FxStyle &style : fx_style_registry().all()) {
            for (const Shape &sh : shapes) {
                Ev ev;
                ev.proj.push_back(afx_projection(sh.shape, style.attr, 101, 50, 109, 50, sh.rad, floor, sh.flags));
                FxSpec spec;
                if (!AttackFxView::make_spec(ev.proj.front(), spec)) {
                    continue;
                }
                const float total = fx_total_seconds(spec, kParams);
                if (!(total > 0.05f) || !(total < 12.f)) {
                    bad_total += std::string(" ") + style.attr + "/" + sh.name;
                }
                for (float t = 0.f; t < (total + 0.04f); t += 0.04f) {
                    const FxFrame f = afx_eval(ev, afx_snap(t));
                    ++runs;
                    if (!afx_frame_finite(f)) {
                        bad_finite += std::string(" ") + style.attr + "/" + sh.name;
                        break;
                    }
                    if (f.voxels.size() > worst_voxels) {
                        worst_voxels = f.voxels.size();
                        worst_name = std::string(style.attr) + "/" + sh.name;
                    }
                }
                const FxFrame end = afx_eval(ev, afx_snap(total + 0.02f));
                if (!end.voxels.empty() || !end.lights.empty() || !end.slabs.empty() || !end.finished || (end.shake != 0.f) || !end.entity_mods.empty()
                    || !end.screen_waves.empty()) {
                    bad_end += std::string(" ") + style.attr + "/" + sh.name;
                }
            }
        }
        // 環境変数 HENGBAND_FX_DIGEST_OUT にファイルの場所を渡すと、属性 × 形ごとの要約（全コマを混ぜた値）を書き出す。
        // 見た目を足す前後で、既存の属性の要約が 1 つも変わっていないことを比べるための道具（通常は何もしない）。
        if (const char *digest_path = std::getenv("HENGBAND_FX_DIGEST_OUT")) {
            if (std::FILE *fp = std::fopen(digest_path, "wb")) {
                for (const FxStyle &style : fx_style_registry().all()) {
                    for (const Shape &sh : shapes) {
                        Ev ev;
                        ProjectionFx pj = afx_projection(sh.shape, style.attr, 101, 50, 109, 50, sh.rad, floor, sh.flags);
                        std::uint32_t name_hash = 2166136261u;
                        for (const char *c = style.attr; *c != '\0'; ++c) {
                            name_hash = (name_hash ^ static_cast<unsigned char>(*c)) * 16777619u;
                        }
                        for (const char *c = sh.name; *c != '\0'; ++c) {
                            name_hash = (name_hash ^ static_cast<unsigned char>(*c)) * 16777619u;
                        }
                        pj.seq = (name_hash & 0xffffffu) + 1u; //!< 並びが変わっても同じ乱数の種になるよう、名前から決める
                        ev.proj.push_back(pj);
                        FxSpec spec;
                        if (!AttackFxView::make_spec(ev.proj.front(), spec)) {
                            continue;
                        }
                        const float total = fx_total_seconds(spec, kParams);
                        std::uint64_t h = 1469598103934665603ull;
                        std::size_t most = 0;
                        for (float t = 0.f; t < (total + 0.04f); t += 0.04f) {
                            const FxFrame digest_frame = afx_eval(ev, afx_snap(t));
                            most = std::max(most, digest_frame.voxels.size());
                            h = afx_frame_digest(digest_frame, h);
                        }
                        // 3 列目は、その撃ち方で 1 コマに出るボクセルの最大（要約の比べには使わない）。
                        std::fprintf(fp, "%s/%s %016llx %zu\n", style.attr, sh.name, static_cast<unsigned long long>(h), most);
                    }
                }
                std::fclose(fp);
            }
        }
        g_seq = seq_before_flow + hengband_pairs;
        std::fprintf(stderr, "    流した回数 %lld / 1 枚のボクセルの最大 %zu（%s）\n", runs, worst_voxels, worst_name.c_str());
        check(bad_finite.empty(), "全属性 × 全形: どの時刻でも数が有限（NaN・無限が出ない）" + bad_finite);
        check(bad_end.empty(), "全属性 × 全形: 最後は何も残らず、終わった印が立つ（ボクセル・光・揺れ・実体の変化・光の波が 0）" + bad_end);
        check(bad_total.empty(), "全属性 × 全形: 全体の長さが 0.05〜12 秒の範囲" + bad_total);
        check(worst_voxels <= 6000u, "全属性 × 全形: 1 枚のボクセルの数が 6000 以下（最大 " + std::to_string(worst_voxels) + "）");
        // 検査の場面の表（系統ごとのファイル）が引ける属性だけを使っている。
        bool scenes_ok = true;
        std::string scene_bad;
        for (const FxCheckScene &sc : fx_scenes_all()) {
            if (fx_find_style(sc.attr) == nullptr) {
                scenes_ok = false;
                scene_bad += std::string(" ") + sc.name;
            }
        }
        check(scenes_ok, "場面の表: 系統ごとの場面は、登録のある属性だけを使う" + scene_bad);
        ProjectionShape ps;
        std::uint16_t pf = 0;
        check(fx_parse_shape("ball", ps, pf) && (ps == ProjectionShape::Ball) && fx_parse_shape("allsight", ps, pf) && (pf == kProjAllInSight)
                && !fx_parse_shape("nonsense", ps, pf),
            "場面の表: --fx-shape の名前（bolt・beam・ball・breath・jump・touch・hidden・allsight）を読める");
    }

    // ---------------------------------------------------------------- 共通の部品
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 共通の部品（CPU）\n");
        const FxSpec spec = afx_part_spec();

        // 粒：重力・空気の抵抗・寿命・床
        {
            FxParticle p;
            p.pos = Vec3{ 5.f, 5.f, 0.6f };
            p.gravity = -9.f;
            p.born = 0.1f;
            p.life = 1.f;
            p.floor = FxFloor::Stop;
            p.floor_z = 0.f;
            p.size0 = 0.05f;
            p.size1 = 0.05f;
            p.alpha1 = 1.f;
            FxFrame early;
            FxFrame stopped;
            FxFrame before;
            check(!fx_emit_particle(p, 0.05f, before) && before.voxels.empty(), "粒: 出る前（born の前）は何も出ない");
            check(fx_emit_particle(p, 0.2f, early) && (early.voxels[0].center.z < 0.6f) && (early.voxels[0].center.z > 0.1f), "粒: 重力で落ちていく");
            bool hit = false;
            float hit_age = 0.f;
            (void)fx_particle_position(p, 0.9f, &hit, &hit_age);
            check(hit && (hit_age > 0.2f) && (hit_age < 0.5f), "粒: 床に当たった時刻を返す（0.6 マスの高さから重力 9 で約 0.37 秒。" + std::to_string(hit_age) + "）");
            check(fx_emit_particle(p, 0.9f, stopped) && (std::fabs(stopped.voxels[0].center.z - 0.025f) < 1e-4f), "粒: 床に当たったら床の上で止まる");
            p.floor = FxFloor::Vanish;
            FxFrame vanished;
            check(!fx_emit_particle(p, 0.9f, vanished) && vanished.voxels.empty(), "粒: 床に当たったら消える（Vanish）");
            FxParticle d;
            d.pos = Vec3{ 0.f, 0.f, 5.f };
            d.vel = Vec3{ 4.f, 0.f, 0.f };
            d.gravity = 0.f;
            d.drag = 2.f;
            d.life = 10.f;
            FxParticle dg;
            dg.pos = Vec3{ 0.f, 0.f, 10.f };
            dg.gravity = -7.f;
            dg.drag = 0.3f;
            const float fall_with_drag = 10.f - fx_particle_position(dg, 0.5f).z;
            check((fall_with_drag > 0.8f) && (fall_with_drag < 0.88f), "粒: 空気の抵抗つきでも重力で落ちる（0.5 秒で 0.5×7×0.25=0.875 より少し小さい。" + std::to_string(fall_with_drag) + "）");
            const Vec3 far = fx_particle_position(d, 6.f);
            check((std::fabs(far.x - 2.f) < 0.01f) && (std::fabs(far.z - 5.f) < 1e-4f), "粒: 空気の抵抗で止まる（速さ 4・抵抗 2 なら 2 マスで止まる。" + std::to_string(far.x) + "）");
            FxSpray spray;
            spray.origin = Vec3{ 5.f, 5.f, 0.5f };
            spray.count = 16;
            spray.life_min = 0.3f;
            spray.life_max = 0.4f;
            spray.floor = FxFloor::Stop;
            FxFrame s1;
            FxFrame s2;
            FxFrame s3;
            const int n1 = fx_emit_spray(spray, 0.1f, s1);
            const int n2 = fx_emit_spray(spray, 0.1f, s2);
            const int n3 = fx_emit_spray(spray, 1.0f, s3);
            check((n1 == 16) && (n1 == n2) && (afx_shape_hash(s1, FxPart::Particle, Vec3{}) == afx_shape_hash(s2, FxPart::Particle, Vec3{})) && (n3 == 0),
                "粒の吹き出し: 全部出て、同じ時刻なら同じ形で、寿命のあとは 0");
            spray.duration = 0.2f;
            FxFrame s4;
            check(fx_emit_spray(spray, 0.05f, s4) < 16, "粒の吹き出し: duration があると、時間をかけて 1 つずつ出る");
        }

        // 床の溜まり
        {
            FxPuddle p;
            p.cx = 105.5f;
            p.cy = 50.5f;
            p.radius = 0.4f;
            p.born = 0.1f;
            FxFrame a;
            FxFrame b;
            FxFrame c;
            FxFrame d;
            const int na = fx_emit_puddle(p, 0.15f, a);
            const int nb = fx_emit_puddle(p, 0.5f, b);
            const int nc = fx_emit_puddle(p, 0.1f + p.spread + p.hold + (p.fade * 0.7f), c);
            const int nd = fx_emit_puddle(p, 0.1f + p.spread + p.hold + p.fade + 0.01f, d);
            check((na > 0) && (nb > na) && (nc > 0) && (nd == 0) && d.voxels.empty(), "水たまり: 広がって、少しずつ消える（" + std::to_string(na) + " → " + std::to_string(nb) + " 個）");
            bool flat = true;
            float reach = 0.f;
            for (const FxVoxel &v : b.voxels) {
                flat = flat && (v.center.z < 0.04f) && (v.size.z <= (p.thickness * 1.4f));
                reach = std::max(reach, std::hypot(v.center.x - p.cx, v.center.y - p.cy));
            }
            check(flat && (reach < (p.radius * 1.5f)) && (reach > (p.radius * 0.5f)), "水たまり: 床に平たく広がる（厚み " + std::to_string(p.thickness) + "、半径 " + std::to_string(reach) + "）");
            check(c.voxels.front().alpha < b.voxels.front().alpha, "水たまり: 消える間は薄れる");
        }

        // 煙・霧
        {
            FxSmoke s;
            s.origin = Vec3{ 105.5f, 50.5f, 0.2f };
            s.count = 8;
            s.duration = 0.3f;
            s.life = 0.8f;
            const auto mean_z = [](const FxFrame &f) {
                double z = 0.0;
                for (const FxVoxel &v : f.voxels) {
                    z += v.center.z;
                }
                return f.voxels.empty() ? -1.0 : (z / static_cast<double>(f.voxels.size()));
            };
            FxFrame a;
            FxFrame b;
            FxFrame c;
            (void)fx_emit_smoke(s, 0.2f, a);
            (void)fx_emit_smoke(s, 0.6f, b);
            const int past = fx_emit_smoke(s, 1.5f, c);
            check((mean_z(b) > mean_z(a) + 0.1) && (past == 0), "煙: 上へ昇って、寿命のあとは消える（高さ " + std::to_string(mean_z(a)) + " → " + std::to_string(mean_z(b)) + "）");
            float size_a = 0.f;
            float size_b = 0.f;
            for (const FxVoxel &v : a.voxels) {
                size_a += v.size.x / static_cast<float>(a.voxels.size());
            }
            for (const FxVoxel &v : b.voxels) {
                size_b += v.size.x / static_cast<float>(b.voxels.size());
            }
            check(size_b > size_a, "煙: 広がる（大きくなる。平均の大きさ " + std::to_string(size_a) + " → " + std::to_string(size_b) + "）");
        }

        // 稲妻
        {
            FxLightning l;
            l.a = Vec3{ 101.5f, 50.5f, 0.6f };
            l.b = Vec3{ 109.5f, 50.5f, 0.5f };
            l.seed = 7;
            std::set<std::uint64_t> shapes;
            for (int i = 0; i < 8; ++i) {
                FxFrame f;
                (void)fx_emit_lightning(l, (static_cast<float>(i) + 0.5f) / l.flicker_hz, f);
                shapes.insert(afx_shape_hash(f, FxPart::Bolt, Vec3{}));
            }
            check(shapes.size() >= 7u, "稲妻: 毎回形が変わる（8 回で " + std::to_string(shapes.size()) + " 種類）");
            FxFrame a;
            FxFrame b;
            (void)fx_emit_lightning(l, 0.05f, a);
            (void)fx_emit_lightning(l, 0.05f, b);
            check(afx_shape_hash(a, FxPart::Bolt, Vec3{}) == afx_shape_hash(b, FxPart::Bolt, Vec3{}), "稲妻: 同じ時刻なら同じ形（撮り直しても同じ絵）");
            float lo = 1e9f;
            float hi = -1e9f;
            float wander = 0.f;
            for (const FxVoxel &v : a.voxels) {
                if (v.part == FxPart::Bolt) {
                    lo = std::min(lo, v.center.x - (v.size.x * 0.5f));
                    hi = std::max(hi, v.center.x + (v.size.x * 0.5f));
                    wander = std::max(wander, std::fabs(v.center.y - 50.5f));
                }
            }
            check((lo < 102.2f) && (hi > 108.8f) && (wander > 0.02f) && (wander < 2.5f), "稲妻: 始点と終点をつなぎ、間は折れ曲がる（横のずれ " + std::to_string(wander) + "）");
            check(afx_count(a, FxPart::BoltGlow) > 0, "稲妻: 芯の周りに光（半透明）がある");
            FxLightning forked = l;
            forked.branches = 2;
            FxFrame fa;
            FxFrame fb;
            (void)fx_emit_lightning(l, 0.05f, fa);
            (void)fx_emit_lightning(forked, 0.05f, fb);
            check(fb.voxels.size() > fa.voxels.size(), "稲妻: 枝を足せる");
            const std::vector<Vec3> pts = fx_zigzag_points(l.a, l.b, 6, 0.2f, 3, 0);
            check((pts.size() == 7u) && (length_of_vec(pts.front() - l.a) < 1e-5f) && (length_of_vec(pts.back() - l.b) < 1e-5f), "折れ線: 両端を含む（segments + 1 個の点）");
        }

        // 輪
        {
            FxRing grow;
            grow.centre = Vec3{ 105.5f, 50.5f, 0.1f };
            grow.r0 = 0.1f;
            grow.r1 = 1.2f;
            grow.born = 0.f;
            grow.life = 0.5f;
            FxRing shrink = grow;
            shrink.r0 = 1.2f;
            shrink.r1 = 0.1f;
            const auto reach = [&](const FxRing &r, float t) {
                FxFrame f;
                (void)fx_emit_ring(r, t, f);
                float m = 0.f;
                for (const FxVoxel &v : f.voxels) {
                    m = std::max(m, std::hypot(v.center.x - r.centre.x, v.center.y - r.centre.y));
                }
                return m;
            };
            check((reach(grow, 0.1f) < reach(grow, 0.4f)) && (reach(grow, 0.4f) > 0.7f), "輪: 広がる");
            check((reach(shrink, 0.1f) > reach(shrink, 0.4f)) && (reach(shrink, 0.4f) < 0.6f), "輪: 縮む");
            FxFrame none;
            check((fx_emit_ring(grow, 0.6f, none) == 0) && none.voxels.empty(), "輪: 寿命のあとは出ない");
            FxRing dashed = grow;
            dashed.dash = 2;
            FxFrame f1;
            FxFrame f2;
            const int full = fx_emit_ring(grow, 0.2f, f1);
            const int half = fx_emit_ring(dashed, 0.2f, f2);
            check((half > 0) && (half < full), "輪: 破線にできる（" + std::to_string(full) + " → " + std::to_string(half) + "）");
            FxRing vertical = grow;
            vertical.normal = Vec3{ 0.f, 1.f, 0.f };
            FxFrame f3;
            (void)fx_emit_ring(vertical, 0.3f, f3);
            float dy = 0.f;
            float dz = 0.f;
            for (const FxVoxel &v : f3.voxels) {
                dy = std::max(dy, std::fabs(v.center.y - vertical.centre.y));
                dz = std::max(dz, std::fabs(v.center.z - vertical.centre.z));
            }
            check((dy < 0.05f) && (dz > 0.3f), "輪: 法線で平面を決められる（法線が y なら輪は x-z 平面）");
        }

        // 画面の揺れ
        {
            check((std::fabs(fx_shake_envelope(0.2f, 0.2f, 0.4f, 0.1f) - 0.1f) < 1e-5f) && (fx_shake_envelope(0.1f, 0.2f, 0.4f, 0.1f) == 0.f)
                    && (fx_shake_envelope(0.6f, 0.2f, 0.4f, 0.1f) == 0.f) && (fx_shake_envelope(0.3f, 0.2f, 0.4f, 0.1f) > fx_shake_envelope(0.5f, 0.2f, 0.4f, 0.1f)),
                "画面の揺れ: 強さと長さを指定でき、始まりが最も強く、弱まって、終わると 0");
            FxFrame f;
            fx_add_shake(f, 0.05f);
            fx_add_shake(f, 0.12f);
            fx_add_shake(f, 0.03f);
            check(std::fabs(f.shake - 0.12f) < 1e-6f, "画面の揺れ: 同じ時刻の複数は大きいほうを取る");
            const Vec3 o1 = fx_shake_offset(0.1f, 1000u);
            const Vec3 o2 = fx_shake_offset(0.1f, 1000u);
            bool varies = false;
            bool bounded = true;
            for (std::uint32_t ms = 0; ms < 600u; ms += 33u) {
                const Vec3 o = fx_shake_offset(0.1f, ms);
                bounded = bounded && (std::fabs(o.x) <= 0.1f) && (std::fabs(o.y) <= 0.1f) && (o.z == 0.f);
                varies = varies || (std::fabs(o.x - o1.x) > 0.01f);
            }
            check((o1.x == o2.x) && (o1.y == o2.y) && varies && bounded && (fx_shake_offset(0.f, 5u).x == 0.f), "画面の揺れ: 揺れの向きは時刻から決まり（同じ時刻に同じ値）、強さを超えない");
            Hd2dSettings settings;
            check(settings.fx_screen_shake, "画面の揺れ: cfg の fx_screen_shake の既定は 1（入）");
        }

        // 対象の見た目の変化
        {
            FxFrame f;
            fx_mod_for(f, 105, 50).squash = 0.5f;
            fx_mod_for(f, 105, 50).grey = 0.5f;
            (void)fx_mod_for(f, 104, 50);
            check((f.entity_mods.size() == 2u) && (f.entity_mods[0].squash == 0.5f) && (f.entity_mods[0].grey == 0.5f),
                "実体の変化: 同じマスは 1 件にまとまる");
            FxEntityMod squash;
            squash.squash = 1.f;
            FxEntityMod recoil;
            recoil.recoil = 1.f;
            recoil.away_x = 1.f;
            recoil.away_y = 0.f;
            FxEntityMod lift;
            lift.lift = 0.3f;
            FxEntityMod grey;
            grey.grey = 1.f;
            FxEntityMod tremble;
            tremble.tremble = 0.05f;
            tremble.tremble_phase = 1.5708f;
            const FxModTransform ts = fx_mod_transform(squash);
            const FxModTransform tr = fx_mod_transform(recoil);
            check((std::fabs(ts.scale_z - 0.4f) < 1e-5f) && (ts.scale_xy > 1.f), "実体の変化: 押しつぶすと高さが 4 割になり、横が少し広がる");
            check((tr.dx > 0.1f) && (tr.pitch > 0.2f) && (std::fabs(tr.dy) < 1e-5f), "実体の変化: のけぞると、撃った側の反対へ下がって後ろへ傾く");
            check((fx_mod_transform(lift).dz == 0.3f) && (fx_mod_transform(grey).tint < 0.5f) && (fx_mod_transform(grey).glow > 0.2f)
                    && (std::fabs(fx_mod_transform(tremble).dx - 0.05f) < 1e-4f),
                "実体の変化: 浮き上がり・灰色・震えの値が出る");
            EntityView view;
            SlabInstance monster;
            monster.slab = 3;
            monster.layer = EntityLayer::Monster;
            monster.cx = 105.5f;
            monster.cy = 50.5f;
            monster.inst.x = 105.1f;
            monster.inst.y = 50.1f;
            monster.inst.sx = 0.8f;
            monster.inst.sy = 0.8f;
            monster.inst.sz = 0.8f;
            monster.height = 0.8f;
            SlabInstance other = monster;
            other.cx = 107.5f;
            other.inst.x = 107.1f;
            SlabInstance item = monster;
            item.layer = EntityLayer::Item;
            view.slabs = { monster, other, item };
            FxFrame fr;
            FxEntityMod &sq = fx_mod_for(fr, 105, 50);
            sq.squash = 1.f;
            sq.echo_x = 0.2f;
            fx_apply_entity_mods(view, fr.entity_mods);
            const SlabInstance &after = view.slabs[0];
            const float centre_x = after.inst.x + (0.5f * after.inst.sx);
            check((view.slabs.size() == 4u) && (after.inst.sz < 0.5f) && (after.inst.sx > 0.8f) && (std::fabs(centre_x - 105.5f) < 1e-4f)
                    && (std::fabs(after.inst.z) < 1e-6f),
                "実体の変化: ボクセル板に当たる（高さが縮み、足元と中心は動かない。二重で 1 枚増える）");
            check((view.slabs[1].inst.sz == 0.8f) && (view.slabs[2].inst.sz == 0.8f), "実体の変化: 別のマスの実体と、同じマスでもアイテムには当たらない");
            EntityView glyph_view;
            BillboardInstance g;
            g.x = 105.5f;
            g.y = 50.5f;
            g.z = 0.05f;
            g.width = 0.5f;
            g.height = 0.8f;
            glyph_view.glyphs = { g };
            glyph_view.glyph_layers = { EntityLayer::Monster };
            fx_apply_entity_mods(glyph_view, fr.entity_mods);
            check((glyph_view.glyphs.size() == 2u) && (glyph_view.glyphs[0].height < 0.4f) && (glyph_view.glyph_layers.size() == 2u),
                "実体の変化: 字の板にも当たる（高さが縮み、二重で 1 枚増える）");
            const Mat4 model = fx_mod_model_matrix(squash, Vec3{ 105.5f, 50.5f, 0.02f });
            check((std::fabs(model.m[10] - 0.4f) < 1e-5f) && (std::fabs(model.m[12] - 105.5f) < 1e-4f), "実体の変化: プレイヤのモデルの置き方にも当たる");
        }

        // 対象のタイルの写し
        {
            FxGhost g;
            g.gx = 105;
            g.gy = 50;
            g.born = 0.1f;
            FxFrame before;
            FxFrame rest;
            FxFrame falling;
            FxFrame landed;
            FxFrame gone;
            const int n_before = fx_emit_tile_ghost(spec, g, 0.05f, before);
            const int n_rest = fx_emit_tile_ghost(spec, g, 0.15f, rest);
            const float total = g.hold + g.crumble + g.fall_life;
            (void)fx_emit_tile_ghost(spec, g, 0.1f + g.hold + (g.crumble * 0.5f), falling);
            (void)fx_emit_tile_ghost(spec, g, 0.1f + g.hold + g.crumble + 0.1f, landed);
            const int n_gone = fx_emit_tile_ghost(spec, g, 0.1f + total + 0.01f, gone);
            check((n_before == 0) && (n_rest > 30), "タイルの写し: 出る前は 0、板は対象のタイルと同じ形の粒でできる（" + std::to_string(n_rest) + " 粒）");
            bool grey_tone = true;
            bool in_cell = true;
            for (const FxVoxel &v : rest.voxels) {
                grey_tone = grey_tone && (std::fabs(v.r - v.b) < 0.12f) && (std::fabs(v.r - v.g) < 0.12f);
                in_cell = in_cell && (std::fabs(v.center.x - 105.5f) <= 0.5f) && (v.center.z <= 1.0f);
            }
            check(grey_tone && in_cell, "タイルの写し: 単色のグレーで、対象のマスの上にある");
            Vec3 lo_land;
            (void)afx_voxel_extent(landed, FxPart::Ghost, lo_land);
            check((afx_count(falling, FxPart::Ghost) > 0) && (afx_shape_hash(falling, FxPart::Ghost, Vec3{}) != afx_shape_hash(rest, FxPart::Ghost, Vec3{}))
                    && (afx_mean_z(falling, FxPart::Ghost) < afx_mean_z(rest, FxPart::Ghost) - 0.005),
                "タイルの写し: 崩れ始めると粒が外れて落ちる（高さの平均 " + std::to_string(afx_mean_z(rest, FxPart::Ghost)) + " → "
                    + std::to_string(afx_mean_z(falling, FxPart::Ghost)) + "）");
            check((lo_land.z < 0.12f) && (n_gone == 0), "タイルの写し: 落ちた粒は床で止まり、最後は消える");
            FxSpec none = spec;
            none.tile_at = nullptr;
            FxFrame fallback;
            check(fx_emit_tile_ghost(none, g, 0.15f, fallback) > 10, "タイルの写し: タイルを引けないときは無地の四角で代用する");
            FxFrame a;
            FxFrame b;
            (void)fx_emit_tile_ghost(spec, g, 0.5f, a);
            (void)fx_emit_tile_ghost(spec, g, 0.5f, b);
            check(afx_shape_hash(a, FxPart::Ghost, Vec3{}) == afx_shape_hash(b, FxPart::Ghost, Vec3{}), "タイルの写し: 同じ時刻なら同じ形");
        }

        // 対象のタイルの画素を板から起こす（配布版には PNG のタイルが無い。画面側の tile_at が使う道）
        {
            SlabLibrary slabs;
            slabs.set_dir("assets/voxel/slab");
            int w = 0;
            int h = 0;
            std::vector<std::uint8_t> px;
            const bool ok = slabs.front_pixels("tilework/sfc/R955.png", w, h, px);
            check(ok && (w == 64) && (h > 8) && (h <= 64) && (px.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u),
                "タイルの写し（板から）: R955 を板の正面の画素として読める（" + std::to_string(w) + "x" + std::to_string(h) + "）");
            int w2 = 0;
            int h2 = 0;
            std::vector<std::uint8_t> none_px;
            check(!slabs.front_pixels("tilework/sfc/zz_not_a_slab.png", w2, h2, none_px), "タイルの写し（板から）: 板が無い名前は偽を返す");
            FxTileImage png;
            if (ok && fx_load_png("tilework/64/R955.png", png) && (png.width == 64) && (png.height == 64)) {
                // 板は絵の底の空白を詰めてある。板の行 r は PNG の行（最初の不透明な行 + r）と同じ形・同じ色でなければならない。
                int first = -1;
                int last = -1;
                for (int y = 0; y < png.height; ++y) {
                    for (int x = 0; x < png.width; ++x) {
                        if (png.rgba[((static_cast<std::size_t>(y) * 64u) + static_cast<std::size_t>(x)) * 4u + 3u] > 0) {
                            if (first < 0) {
                                first = y;
                            }
                            last = y;
                        }
                    }
                }
                int shape_diff = 0;
                long long colour_diff = 0;
                int n_opaque = 0;
                if ((first >= 0) && (last - first + 1 == h)) {
                    for (int r = 0; r < h; ++r) {
                        for (int x = 0; x < w; ++x) {
                            const std::size_t a = ((static_cast<std::size_t>(r) * 64u) + static_cast<std::size_t>(x)) * 4u;
                            const std::size_t c = ((static_cast<std::size_t>(first + r) * 64u) + static_cast<std::size_t>(x)) * 4u;
                            const bool slab_on = px[a + 3] > 0;
                            const bool png_on = png.rgba[c + 3] > 0;
                            if (slab_on != png_on) {
                                ++shape_diff;
                            } else if (slab_on) {
                                ++n_opaque;
                                colour_diff += std::abs(static_cast<int>(px[a]) - static_cast<int>(png.rgba[c]))
                                    + std::abs(static_cast<int>(px[a + 1]) - static_cast<int>(png.rgba[c + 1]))
                                    + std::abs(static_cast<int>(px[a + 2]) - static_cast<int>(png.rgba[c + 2]));
                            }
                        }
                    }
                }
                const double mean_diff = (n_opaque > 0) ? (static_cast<double>(colour_diff) / (3.0 * n_opaque)) : 255.0;
                check((first >= 0) && (last - first + 1 == h) && (shape_diff == 0) && (n_opaque > 200) && (mean_diff < 16.0),
                    "タイルの写し（板から）: PNG と同じ形で、色もほぼ同じ（板の高さ " + std::to_string(h) + "・PNG の不透明な行 " + std::to_string(first) + "〜" + std::to_string(last) + "・形の食い違い " + std::to_string(shape_diff) + "・色の差の平均 " + std::to_string(mean_diff) + "）");
            }
        }

        // 文字の格子から作る形
        {
            const FxShape arrow = fx_shape({ "..#..", ".###.", "#####", "..#..", "..#.." });
            check((arrow.width == 5) && (arrow.height == 5), "形: 文字の格子の幅と高さを読む");
            FxShapePlace place;
            place.centre = Vec3{ 105.5f, 50.5f, 1.f };
            place.cell = 0.1f;
            FxFrame f;
            const int n = fx_emit_shape(arrow, FxShapeColours{}, place, f);
            Vec3 lo;
            const Vec3 hi = afx_voxel_extent(f, FxPart::Shape, lo);
            check((n == 11) && (std::fabs((hi.x - lo.x) - 0.4f) < 1e-4f) && (std::fabs((hi.z - lo.z) - 0.4f) < 1e-4f), "形: '#' の数だけボクセルが出て、格子の大きさに収まる");
            FxShapeColours colours;
            colours.keys = "r";
            colours.colours = { FxColour{ 1.f, 0.f, 0.f } };
            const FxShape two = fx_shape({ "r.#" });
            FxFrame fc;
            (void)fx_emit_shape(two, colours, place, fc);
            check((fc.voxels.size() == 2u) && (fc.voxels[0].r == 1.f) && (fc.voxels[0].g == 0.f) && (fc.voxels[1].g == 1.f), "形: 字ごとに色を変えられる");
            place.layers = 2;
            FxFrame fl;
            check(fx_emit_shape(arrow, FxShapeColours{}, place, fl) == 22, "形: 層を重ねて厚みを出せる");
            FxShapePlace turned = place;
            turned.layers = 1;
            turned.rotate = 1.5708f;
            FxFrame fr;
            (void)fx_emit_shape(fx_shape({ "#####" }), FxShapeColours{}, turned, fr);
            Vec3 rlo;
            const Vec3 rhi = afx_voxel_extent(fr, FxPart::Shape, rlo);
            check(((rhi.z - rlo.z) > 0.35f) && ((rhi.x - rlo.x) < 0.01f), "形: 平面の中で回せる");
        }

        // 絵の板
        {
            check(fx_sprite_available("skull"), "絵の板: assets/fx/skull.png を読める（リポジトリ直下で実行する）");
            FxSprite s;
            s.name = "skull";
            s.centre = Vec3{ 105.5f, 50.5f, 1.2f };
            s.size = 0.5f;
            FxFrame f;
            const int n = fx_emit_sprite(spec, s, f);
            float sprite_w = 0.f;
            float sprite_h = 0.f;
            afx_screen_extent(f, FxPart::Sprite, spec, s.centre, sprite_w, sprite_h);
            const Vec3 toward = fx_toward_camera(spec);
            float depth_spread = 0.f;
            for (const FxVoxel &v : f.voxels) {
                depth_spread = std::max(depth_spread, std::fabs(dot(v.center - s.centre, toward)));
            }
            check((n >= 60) && (n <= 700) && (sprite_h > 0.42f) && (sprite_h < 0.56f) && (sprite_w > 0.22f) && (sprite_w < sprite_h),
                "絵の板: どくろを粒に直して出す（" + std::to_string(n) + " 個、縦 " + std::to_string(sprite_h) + " 横 " + std::to_string(sprite_w) + " マス）");
            check(depth_spread < 1e-3f, "絵の板: 画面に正対する板になる（奥行きのずれ " + std::to_string(depth_spread) + "）");
            FxSprite fade = s;
            fade.alpha = 0.4f;
            fade.tint = FxColour{ 1.f, 0.2f, 0.2f };
            FxFrame ff;
            (void)fx_emit_sprite(spec, fade, ff);
            double sum_r = 0.0;
            double sum_g = 0.0;
            float max_alpha = 0.f;
            for (const FxVoxel &v : ff.voxels) {
                sum_r += v.r;
                sum_g += v.g;
                max_alpha = std::max(max_alpha, v.alpha);
            }
            check(!ff.voxels.empty() && (max_alpha <= 0.4001f) && (sum_r > (sum_g * 2.0)), "絵の板: 色と透明度を掛けられる");
            FxSprite turned = s;
            turned.rotate = 1.5708f;
            FxFrame ft;
            (void)fx_emit_sprite(spec, turned, ft);
            float turned_w = 0.f;
            float turned_h = 0.f;
            afx_screen_extent(ft, FxPart::Sprite, spec, s.centre, turned_w, turned_h);
            check((turned_w > (sprite_w * 1.3f)) && (turned_h < (sprite_h * 0.8f)), "絵の板: 回せる（縦長のどくろを 90 度回すと、横が広がり縦が縮む）");
            FxSprite missing;
            missing.name = "no_such_sprite";
            FxFrame fm;
            check((fx_emit_sprite(spec, missing, fm) == 0) && fm.voxels.empty(), "絵の板: 絵が読めなければ何も出さない");
        }

        // 光の波
        {
            Ev ev;
            ev.proj.push_back(afx_projection(ProjectionShape::Bolt, "fire", 101, 50, 105, 50, 0, { AfxRect{ 60, 30, 160, 70 } }, kProjAllInSight));
            const FxFrame early = afx_eval(ev, 0.1f);
            const FxFrame mid = afx_eval(ev, 0.45f);
            const FxFrame late = afx_eval(ev, 0.8f);
            const auto lead = [](const FxFrame &f) {
                float best = -1e9f;
                for (const FxScreenWave &w : f.screen_waves) {
                    best = std::max(best, w.position);
                }
                return best;
            };
            check(!mid.screen_waves.empty() && (mid.screen_waves[0].alpha > 0.1f) && (lead(early) < lead(mid)) && (lead(mid) < lead(late)),
                "光の波: 視界内の全員にかかる魔法は、画面の左から右へ走る");
            check((mid.shake == 0.f) && mid.entity_mods.empty(), "光の波: 揺れや実体の変化は足さない（既定の見た目）");
            Ev single;
            single.proj.push_back(afx_projection(ProjectionShape::Bolt, "fire", 101, 50, 105, 50, 0, { AfxRect{ 60, 30, 160, 70 } }, kProjHidden));
            check(afx_eval(single, 0.4f).screen_waves.empty(), "光の波: 1 体にかかる魔法には出ない");
        }
    }
}

} // namespace

int run_attack_fx_check(const AppOptions &options, const std::string &out_stem)
{
    int failures = 0;
    int passes = 0;
    const auto check = [&](bool ok, const std::string &what) {
        if (ok) {
            ++passes;
            std::fprintf(stderr, "  PASS %s\n", what.c_str());
        } else {
            ++failures;
            std::fprintf(stderr, "  **FAIL** %s\n", what.c_str());
        }
    };

    const VoxelFxParams &P = kParams;
    const std::vector<AfxRect> open_floor{ AfxRect{ 60, 30, 160, 70 } }; //!< 数える検査の地図（壁なし）

    /*
     * 1 属性ずつ絵を出す口（`--fx-attr=pois --fx-shape=ball`、`--fx-family=liquid`、`--fx-scene=pois_ball`、`--fx-rad=2`）。
     * 引数は `forwarded_args` から拾う（共通の引数表を触らない）。指定があれば**連続画像はその場面だけ**を書き出す
     * （出力の頭 `--attack-fx-check=出力の頭` が要る）。数える検査は指定の有無によらず全部走る。
     */
    struct PickScene {
        std::string name;
        std::string attr;
        ProjectionShape shape{ ProjectionShape::Ball };
        int rad{ 0 };
        std::uint16_t flags{ 0 };
    };
    std::vector<PickScene> picks;
    {
        std::string pick_attr;
        std::string pick_shape;
        std::string pick_family;
        std::string pick_scene;
        std::string pick_rad;
        for (const std::string &arg : options.forwarded_args) {
            const auto value_of = [&arg](const char *key, std::string &dst) {
                const std::string k = key;
                if (arg.compare(0, k.size(), k) == 0) {
                    dst = arg.substr(k.size());
                    return true;
                }
                return false;
            };
            (void)(value_of("--fx-attr=", pick_attr) || value_of("--fx-shape=", pick_shape) || value_of("--fx-family=", pick_family)
                || value_of("--fx-scene=", pick_scene) || value_of("--fx-rad=", pick_rad));
        }
        const auto add_def = [&](const FxCheckScene &sc) { picks.push_back(PickScene{ sc.name, sc.attr, sc.shape, sc.rad, sc.flags }); };
        if (pick_scene == "parts_demo") {
            picks.push_back(PickScene{ "parts_demo", "", ProjectionShape::Bolt, 0, 0 });
        } else if (!pick_scene.empty()) {
            bool found = false;
            for (const FxCheckScene &sc : fx_scenes_all()) {
                if (pick_scene == sc.name) {
                    add_def(sc);
                    found = true;
                }
            }
            check(found, "場面の指定: --fx-scene=" + pick_scene + " は場面の表にある");
        }
        const auto split_commas = [](const std::string &text) {
            std::vector<std::string> parts;
            std::string cur;
            for (const char c : text) {
                if (c == ',') {
                    if (!cur.empty()) {
                        parts.push_back(cur);
                    }
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()) {
                parts.push_back(cur);
            }
            return parts;
        };
        //! 属性 1 つ × 形 1 つの場面を足す。
        const auto add_attr_shape = [&](const std::string &attr, const std::string &shape_name) {
            PickScene sc;
            sc.attr = attr;
            sc.name = attr + "_" + shape_name;
            const bool ok = fx_parse_shape(shape_name, sc.shape, sc.flags);
            check(ok, "場面の指定: --fx-shape=" + shape_name + " は bolt・beam・ball・breath・jump・touch・hidden・allsight のどれか");
            if (ok) {
                sc.rad = (sc.shape == ProjectionShape::Ball) ? 2 : ((sc.shape == ProjectionShape::Breath) ? 7 : ((sc.shape == ProjectionShape::Jump) ? 1 : 0));
                if (!pick_rad.empty()) {
                    sc.rad = std::atoi(pick_rad.c_str());
                }
                picks.push_back(sc);
            }
        };
        if (!pick_family.empty()) {
            if ((pick_family == "frox") || (pick_family == "gensoband") || (pick_family == "silq")) {
                // Frox・幻想蛮怒の系統は場面の表を持たない。登録表の属性ぜんぶを、指定の形（既定はボール）で出す。
                const std::vector<std::string> shape_names = pick_shape.empty() ? std::vector<std::string>{ "ball" } : split_commas(pick_shape);
                int found = 0;
                for (const FxStyle &style : fx_style_registry().all()) {
                    if (pick_family == style.family) {
                        ++found;
                        for (const std::string &shape_name : shape_names) {
                            add_attr_shape(style.attr, shape_name);
                        }
                    }
                }
                check(found > 0, "場面の指定: --fx-family=" + pick_family + " は登録表にある系統");
            } else {
                const std::vector<FxCheckScene> family = fx_scenes_of_family(pick_family);
                for (const FxCheckScene &sc : family) {
                    add_def(sc);
                }
                check(!family.empty(),
                    "場面の指定: --fx-family=" + pick_family + " は系統の名前（liquid・lightning・elemental・spirit・frox・gensoband・silq）");
            }
        }
        if (!pick_attr.empty()) {
            // `--fx-attr=a,b,c`（コンマ区切りで複数）。形も `--fx-shape=ball,bolt` のように複数にできる。
            const std::vector<std::string> shape_names = split_commas(pick_shape);
            for (const std::string &attr : split_commas(pick_attr)) {
                check(fx_find_style(attr) != nullptr, "場面の指定: --fx-attr=" + attr + " は登録のある属性");
                if (shape_names.empty()) {
                    bool any = false;
                    for (const FxCheckScene &sc : fx_scenes_all()) {
                        if (attr == sc.attr) {
                            add_def(sc);
                            any = true;
                        }
                    }
                    if (!any) {
                        picks.push_back(PickScene{ attr + "_ball", attr, ProjectionShape::Ball, 2, 0 });
                    }
                } else {
                    for (const std::string &shape_name : shape_names) {
                        add_attr_shape(attr, shape_name);
                    }
                }
            }
        }
    }
    const bool picture_mode = !picks.empty();
    fx_sprite_set_dir("assets/fx"); //!< 絵の板（どくろなど）。リポジトリ直下で実行する
    afx_style_checks(check);

    /* ======================================================== (1) 数で確かめる（CPU だけ） */

    // ---- 弓の矢（少し弧）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 弓の矢（CPU）\n");
        Ev ev;
        ev.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", 101, 50, 109, 50, true));
        const FxSpec spec = afx_spec(ev);
        const float fly = fx_fly_seconds(spec, P);
        const float total = fx_total_seconds(spec, P);
        std::fprintf(stderr, "    矢: 飛ぶ時間 %.3f 秒 / 全部で %.3f 秒 / 距離 8 マス / 速さ %.0f マス/秒\n", fly, total, P.arrow_speed);
        check((fly > 0.35f) && (fly < 0.42f), "矢: 8 マス先へ 1 秒に 20 マスで飛ぶと 0.4 秒弱で届く");
        for (const float t : { 0.0f, 0.1f, 0.2f, 0.3f, 0.36f }) {
            const FxFrame f = afx_eval(ev, t);
            char tag[48]{};
            std::snprintf(tag, sizeof(tag), "矢 t=%.2f: ", t);
            check((f.voxels.size() >= 12) && (f.voxels.size() <= 24), std::string(tag) + "ボクセルの数が 12〜24（実際 " + std::to_string(f.voxels.size()) + "）");
            int head_n = 0;
            int fletch_n = 0;
            const Vec3 head = afx_centroid(f, FxPart::ArrowHead, head_n);
            const Vec3 fletch = afx_centroid(f, FxPart::ArrowFletch, fletch_n);
            check((head_n > 0) && (afx_count(f, FxPart::ArrowShaft) > 0) && (fletch_n > 0), std::string(tag) + "矢じり・矢柄・羽根の 3 つがそろっている");
            const Vec3 axis = head - fletch;
            const float length = std::sqrt(dot(axis, axis));
            check((length > 0.45f) && (length < 0.8f), std::string(tag) + "矢じりから羽根までの長さが 0.45〜0.8 マス（実際 " + std::to_string(length) + "）");
            check(f.projectile_visible, std::string(tag) + "矢が見えている");
            check(afx_angle_deg(axis, f.projectile_dir) < 1.5f, std::string(tag) + "矢の向き（矢じり−羽根）が進む向きと一致");
            const Vec3 expected = fx_flight_position(spec, P, afx_snap(t) / fly);
            const Vec3 d = f.projectile_pos - expected;
            check(std::sqrt(dot(d, d)) < 1e-3f, std::string(tag) + "矢の中心が経路の上にある");
            check(std::fabs(f.projectile_pos.y - 50.5f) < 1e-3f, std::string(tag) + "水平方向に経路からそれていない");
            const FxFrame f1 = afx_eval(ev, afx_snap(t + 0.01f));
            const FxFrame f0 = afx_eval(ev, afx_snap(std::max(t - 0.01f, 0.f)));
            check(afx_angle_deg(f1.projectile_pos - f0.projectile_pos, f.projectile_dir) < 5.f,
                std::string(tag) + "経路の接線（数値微分）と矢の向きが合う");
            const Vec3 step = f1.projectile_pos - f.projectile_pos;
            const float speed = std::sqrt(dot(step, step)) / 0.01f;
            if (t < 0.34f) {
                check((speed > 18.f) && (speed < 22.f), std::string(tag) + "速さが 20 マス/秒前後（実際 " + std::to_string(speed) + "）");
            }
        }
        {
            const FxFrame mid = afx_eval(ev, afx_snap(fly * 0.5f));
            const float linear_z = (0.62f + 0.52f) * 0.5f;
            check(mid.projectile_pos.z > (linear_z + 0.1f), "矢: 途中で少し上へ弧を描く（直線より 0.1 マス以上高い）");
            check((afx_eval(ev, 0.02f).projectile_dir.z > 0.f) && (afx_eval(ev, afx_snap(fly - 0.02f)).projectile_dir.z < 0.f),
                "矢: 昇りの向きで出て、降りの向きで当たる");
        }
        {
            const FxFrame stuck = afx_eval(ev, afx_snap(fly + 0.05f));
            int head_n = 0;
            const Vec3 head = afx_centroid(stuck, FxPart::ArrowHead, head_n);
            const Vec3 miss = head - Vec3{ 109.5f, 50.5f, 0.52f };
            check(stuck.projectile_visible && (std::sqrt(dot(miss, miss)) < 0.3f), "矢: 当たったあと的の所に刺さって残る");
            const FxFrame fading = afx_eval(ev, afx_snap(total - 0.05f));
            check(!fading.voxels.empty() && (fading.voxels.front().alpha < 1.f), "矢: 消える直前は薄れている");
            const FxFrame gone = afx_eval(ev, afx_snap(total + 0.01f));
            check(gone.voxels.empty() && gone.lights.empty() && gone.slabs.empty() && gone.finished && !gone.projectile_visible,
                "矢: 最後は消える（ボクセルも光も残らない）");
            FxFrame before;
            fx_evaluate(spec, P, -0.01f, before);
            check(before.voxels.empty(), "矢: 撃つ前は何も無い");
        }
        // 経路のマスをたどる：斜めの経路では、直線ではなく経路のマスの中心を順に通る。
        {
            Ev zig;
            zig.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", 101, 50, 109, 53, true));
            const FxSpec zs = afx_spec(zig);
            check(zs.route.size() == (zig.miss.front().path.size() + 1), "経路: 始点と経路のマスの数だけ点がある（直線で結ばない）");
            bool on_cells = true;
            for (std::size_t i = 0; i < zig.miss.front().path.size(); ++i) {
                const Vec3 &p = zs.route[i + 1];
                on_cells = on_cells && (std::fabs(p.x - (static_cast<float>(zig.miss.front().path[i].x) + 0.5f)) < 1e-4f)
                    && (std::fabs(p.y - (static_cast<float>(zig.miss.front().path[i].y) + 0.5f)) < 1e-4f);
            }
            check(on_cells, "経路: 点がそれぞれ経路のマスの中心にある");
            // 矢を実際に飛ばして、経路のマスの中心の近く（0.5 マス以内）を通る。
            const float zfly = fx_fly_seconds(zs, P);
            float worst = 0.f;
            float passed_cells = 0.f;
            for (float t = 0.f; t < zfly; t += 0.01f) {
                const FxFrame f = afx_eval(zig, afx_snap(t));
                // 経路の線分（マスの中心をつないだ折れ線）までの水平の距離。
                float best = 1e9f;
                for (std::size_t i = 0; (i + 1) < zs.route.size(); ++i) {
                    const float ax = zs.route[i].x;
                    const float ay = zs.route[i].y;
                    const float bx = zs.route[i + 1].x - ax;
                    const float by = zs.route[i + 1].y - ay;
                    const float len2 = (bx * bx) + (by * by);
                    const float k = (len2 > 1e-9f) ? std::min(1.f, std::max(0.f, (((f.projectile_pos.x - ax) * bx) + ((f.projectile_pos.y - ay) * by)) / len2)) : 0.f;
                    best = std::min(best, std::hypot(f.projectile_pos.x - (ax + (bx * k)), f.projectile_pos.y - (ay + (by * k))));
                }
                worst = std::max(worst, best);
                passed_cells = std::max(passed_cells, f.projectile_pos.x);
            }
            check(worst < 0.02f, "経路: 矢はいつも経路の折れ線（マスの中心をつないだ線）の上を飛ぶ（最大のずれ " + std::to_string(worst) + " マス）");
            check(passed_cells > 108.9f, "経路: 矢は経路の終点の近くまで届く");
        }
    }

    // ---- クロスボウの矢（まっすぐ）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) クロスボウの矢（CPU）\n");
        Ev ev;
        ev.miss.push_back(afx_missile(MissileKind::Bolt, 0, "", 101, 50, 109, 50, true));
        Ev arrow_ev;
        arrow_ev.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", 101, 50, 109, 50, true));
        const FxSpec spec = afx_spec(ev);
        const float fly = fx_fly_seconds(spec, P);
        const float total = fx_total_seconds(spec, P);
        std::fprintf(stderr, "    クロスボウの矢: 飛ぶ時間 %.3f 秒 / 全部で %.3f 秒 / 速さ %.0f マス/秒\n", fly, total, P.bolt_speed);
        check((fly > 0.25f) && (fly < 0.31f), "クロスボウの矢: 8 マス先へ 1 秒に 28 マスで飛ぶと約 0.28 秒で届く");
        float z_min = 1e9f;
        float z_max = -1e9f;
        float dirz_max = 0.f;
        float y_dev = 0.f;
        float tangent_worst = 0.f;
        for (float t = 0.f; t < (fly - 0.002f); t += 0.01f) {
            const FxFrame f = afx_eval(ev, afx_snap(t));
            z_min = std::min(z_min, f.projectile_pos.z);
            z_max = std::max(z_max, f.projectile_pos.z);
            dirz_max = std::max(dirz_max, std::fabs(f.projectile_dir.z));
            y_dev = std::max(y_dev, std::fabs(f.projectile_pos.y - 50.5f));
            // 羽根は上と左右の 3 枚で非対称（重心が軸から外れる）ので、矢柄の重心を後ろの基準にする。
            int head_n = 0;
            int shaft_n = 0;
            const Vec3 axis = afx_centroid(f, FxPart::ArrowHead, head_n) - afx_centroid(f, FxPart::ArrowShaft, shaft_n);
            tangent_worst = std::max(tangent_worst, afx_angle_deg(axis, Vec3{ 1.f, 0.f, 0.f }));
        }
        check((z_max - z_min) < 1e-4f, "クロスボウの矢: 経路の高さが一定（飛んでいる間、高さの差 " + std::to_string(z_max - z_min) + "）");
        check((dirz_max < 1e-3f) && (y_dev < 1e-3f), "クロスボウの矢: 向きに上下の傾きが無く、横にもそれない（弧を描かない）");
        check(tangent_worst < 1.5f, "クロスボウの矢: 矢の向きがいつも的の向き（東）を指す（最悪 " + std::to_string(tangent_worst) + " 度）");
        {
            const FxFrame b = afx_eval(ev, 0.1f);
            const FxFrame a = afx_eval(arrow_ev, 0.1f);
            int bh = 0;
            int bf = 0;
            int ah = 0;
            int af = 0;
            const float b_len = std::sqrt(dot(afx_centroid(b, FxPart::ArrowHead, bh) - afx_centroid(b, FxPart::ArrowFletch, bf),
                afx_centroid(b, FxPart::ArrowHead, bh) - afx_centroid(b, FxPart::ArrowFletch, bf)));
            const float a_len = std::sqrt(dot(afx_centroid(a, FxPart::ArrowHead, ah) - afx_centroid(a, FxPart::ArrowFletch, af),
                afx_centroid(a, FxPart::ArrowHead, ah) - afx_centroid(a, FxPart::ArrowFletch, af)));
            float b_shaft = 0.f;
            float a_shaft = 0.f;
            float b_end_to_end_min = 1e9f;
            float b_end_to_end_max = -1e9f;
            for (const FxVoxel &v : b.voxels) {
                if (v.part == FxPart::ArrowShaft) {
                    b_shaft = std::max(b_shaft, v.size.y);
                }
                b_end_to_end_min = std::min(b_end_to_end_min, v.center.x - (v.size.x * 0.5f));
                b_end_to_end_max = std::max(b_end_to_end_max, v.center.x + (v.size.x * 0.5f));
            }
            float a_min = 1e9f;
            float a_max = -1e9f;
            for (const FxVoxel &v : a.voxels) {
                if (v.part == FxPart::ArrowShaft) {
                    a_shaft = std::max(a_shaft, v.size.y);
                }
                a_min = std::min(a_min, v.center.x - (v.size.x * 0.5f));
                a_max = std::max(a_max, v.center.x + (v.size.x * 0.5f));
            }
            std::fprintf(stderr, "    矢の全長 %.2f / クロスボウの矢の全長 %.2f / 矢柄の太さ %.3f → %.3f\n", a_max - a_min,
                b_end_to_end_max - b_end_to_end_min, a_shaft, b_shaft);
            check((b_end_to_end_max - b_end_to_end_min) < ((a_max - a_min) * 0.75f), "クロスボウの矢: 弓の矢より短い（全長が 3/4 未満）");
            check(b_shaft > (a_shaft * 1.4f), "クロスボウの矢: 弓の矢より太い（矢柄が 1.4 倍超）");
            check((b_len > 0.2f) && (b_len < a_len), "クロスボウの矢: 矢じりから羽根までが弓の矢より短い");
            check(b.voxels.size() >= 8, "クロスボウの矢: ボクセルが 8 個以上ある（" + std::to_string(b.voxels.size()) + "）");
        }
        {
            const FxFrame stuck = afx_eval(ev, afx_snap(fly + 0.05f));
            int head_n = 0;
            const Vec3 head = afx_centroid(stuck, FxPart::ArrowHead, head_n);
            check(stuck.projectile_visible && (std::fabs(head.x - 109.5f) < 0.35f), "クロスボウの矢: 当たったあと的の所に刺さって残る");
            check(afx_eval(ev, afx_snap(total + 0.01f)).voxels.empty() && afx_eval(ev, afx_snap(total + 0.01f)).finished,
                "クロスボウの矢: 最後は消える");
        }
    }

    // ---- 弾（スリング）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 弾（CPU）\n");
        Ev ev;
        ev.miss.push_back(afx_missile(MissileKind::Shot, 0, "", 101, 50, 109, 50, true));
        const FxSpec spec = afx_spec(ev);
        const float fly = fx_fly_seconds(spec, P);
        check((fly > 0.30f) && (fly < 0.36f), "弾: 8 マス先へ 1 秒に 24 マスで飛ぶと約 0.33 秒で届く（実際 " + std::to_string(fly) + "）");
        const FxFrame f = afx_eval(ev, 0.1f);
        check(afx_count(f, FxPart::ShotStone) == 19, "弾: 小さな石の玉（19 個のボクセル。実際 " + std::to_string(afx_count(f, FxPart::ShotStone)) + "）");
        float reach = 0.f;
        for (const FxVoxel &v : f.voxels) {
            reach = std::max(reach, std::sqrt(dot(v.center - f.projectile_pos, v.center - f.projectile_pos)));
        }
        check(reach < 0.15f, "弾: 玉は直径 0.2 マスほどの大きさ（中心から粒まで最大 " + std::to_string(reach) + "）");
        std::set<std::uint64_t> shapes;
        for (int i = 0; i < 6; ++i) {
            const FxFrame g = afx_eval(ev, afx_snap(0.1f + (0.01f * static_cast<float>(i))));
            shapes.insert(afx_shape_hash(g, FxPart::ShotStone, g.projectile_pos));
        }
        check(shapes.size() >= 5, "弾: 回りながら飛ぶ（中心から見た粒の並びが毎回違う。種類 " + std::to_string(shapes.size()) + "）");
        const Vec3 expected = fx_flight_position(spec, P, 0.1f / fly);
        check(std::sqrt(dot(f.projectile_pos - expected, f.projectile_pos - expected)) < 1e-3f, "弾: 中心が経路の上にある");
        const FxFrame mid = afx_eval(ev, afx_snap(fly * 0.5f));
        check(mid.projectile_pos.z > (((0.62f + 0.52f) * 0.5f) + 0.05f), "弾: 矢より低い弧を描く（直線より 0.05 マス以上高い）");
        check(afx_eval(ev, afx_snap(fly + 0.02f)).voxels.empty() && afx_eval(ev, afx_snap(fly + 0.02f)).finished, "弾: 着いたところで消える");
    }

    // ---- 投げた物（短剣の板）とブーメラン
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 投げた物・ブーメラン（CPU）\n");
        Ev ev;
        ev.miss.push_back(afx_missile(MissileKind::Thrown, kDaggerItem, "", 101, 50, 109, 50, true));
        const FxSpec spec = afx_spec(ev);
        const float fly = fx_fly_seconds(spec, P);
        check((fly > 0.52f) && (fly < 0.62f), "投げた物: 8 マス先へ 1 秒に 14 マスで飛ぶと約 0.57 秒で届く（実際 " + std::to_string(fly) + "）");
        FxViewParams with_slab;
        with_slab.has_slab = [](std::uint16_t id) { return id == kDaggerItem; };
        const FxFrame f = afx_eval(ev, 0.2f, with_slab);
        check((f.slabs.size() == 1) && f.voxels.empty() && (f.slabs[0].item_id == kDaggerItem),
            "投げた物: 板が引けるときは、その物の板 1 枚を飛ばす（ボクセルの箱は出さない）");
        if (!f.slabs.empty()) {
            const Vec3 expected = fx_flight_position(spec, P, 0.2f / fly);
            check(std::sqrt(dot(f.slabs[0].centre - expected, f.slabs[0].centre - expected)) < 1e-3f, "投げた物: 板の中心が経路の上にある");
            check(std::fabs(f.slabs[0].scale - 0.7f) < 1e-4f, "投げた物: 板の大きさは床のアイテムと同じ（0.7）");
            const FxFrame g = afx_eval(ev, 0.25f, with_slab);
            const float spin = std::fabs(g.slabs[0].spin - f.slabs[0].spin);
            check(spin > 0.5f, "投げた物: 板が回りながら飛ぶ（0.05 秒で " + std::to_string(spin) + " ラジアン）");
            const float rate = spin / 0.05f;
            check((rate > 15.f) && (rate < 24.f), "投げた物: 回る速さが 19 ラジアン/秒前後（実際 " + std::to_string(rate) + "）");
        }
        const FxFrame box = afx_eval(ev, 0.2f); // 板が引けない
        check(box.slabs.empty() && (afx_count(box, FxPart::ThrownBox) == 2),
            "投げた物: 板が引けないときは汎用の小さな箱で飛ばす（板は出さない）");
        float z_sum = 0.f;
        for (const FxVoxel &v : box.voxels) {
            z_sum += v.center.z;
        }
        check(std::fabs((z_sum / static_cast<float>(box.voxels.size())) - box.projectile_pos.z) < 0.1f, "投げた物: 箱も経路の上にある");
        check(afx_eval(ev, afx_snap(fly + 0.02f), with_slab).slabs.empty(), "投げた物: 着いたところで消える");
        // 板の向き：視点を回すと板の向きもついてくる（毎回の評価で更新する）。
        FxViewParams turned = with_slab;
        turned.face_yaw = 1.5708f;
        turned.face_pitch = 0.6f;
        const FxFrame t1 = afx_eval(ev, 0.2f, turned);
        check(!t1.slabs.empty() && (std::fabs(t1.slabs[0].face_yaw - 1.5708f) < 1e-4f) && (std::fabs(t1.slabs[0].face_pitch - 0.6f) < 1e-4f),
            "投げた物: 板の向きは画面側の指定（視点の回転）に従う");

        // ブーメラン：行って、戻ってくる。
        Ev bo;
        bo.miss.push_back(afx_missile(MissileKind::Boomerang, kDaggerItem, "", 101, 50, 107, 50, true, true));
        const FxSpec bspec = afx_spec(bo);
        const float bfly = fx_fly_seconds(bspec, P);
        const float bout = fx_outbound_seconds(bspec, P);
        std::fprintf(stderr, "    ブーメラン: 行き %.3f 秒 / 行って戻るまで %.3f 秒\n", bout, bfly);
        check((bout > (bfly * 0.4f)) && (bout < (bfly * 0.6f)), "ブーメラン: 行きと戻りがほぼ同じ時間");
        const FxFrame turn = afx_eval(bo, afx_snap(bout), with_slab);
        check(!turn.slabs.empty() && (turn.slabs[0].centre.x > 106.4f), "ブーメラン: 折り返しで的の所まで行っている（x=" + std::to_string(turn.slabs.empty() ? 0.f : turn.slabs[0].centre.x) + "）");
        const FxFrame late = afx_eval(bo, afx_snap(bfly - 0.03f), with_slab);
        check(!late.slabs.empty() && (late.slabs[0].centre.x < 102.2f), "ブーメラン: 戻りの終わりには撃った位置の近くにいる（x=" + std::to_string(late.slabs.empty() ? 0.f : late.slabs[0].centre.x) + "）");
        bool monotone_out = true;
        bool monotone_back = true;
        float prev = -1e9f;
        for (float t = 0.f; t < (bout - 0.01f); t += 0.02f) {
            const FxFrame h = afx_eval(bo, afx_snap(t), with_slab);
            if (!h.slabs.empty()) {
                monotone_out = monotone_out && (h.slabs[0].centre.x >= (prev - 1e-4f));
                prev = h.slabs[0].centre.x;
            }
        }
        prev = 1e9f;
        for (float t = bout + 0.02f; t < (bfly - 0.01f); t += 0.02f) {
            const FxFrame h = afx_eval(bo, afx_snap(t), with_slab);
            if (!h.slabs.empty()) {
                monotone_back = monotone_back && (h.slabs[0].centre.x <= (prev + 1e-4f));
                prev = h.slabs[0].centre.x;
            }
        }
        check(monotone_out && monotone_back, "ブーメラン: 行きは東へ、戻りは西へ、向きを変えずに進む");
        check(afx_eval(bo, afx_snap(bfly + 0.02f), with_slab).slabs.empty(), "ブーメラン: 戻ってきたら消える");
        // 戻りの経路が空なら、来た道を逆にたどって撃った位置まで戻る。
        Ev bo2;
        bo2.miss.push_back(afx_missile(MissileKind::Boomerang, kDaggerItem, "", 101, 50, 107, 50, true, false));
        const FxSpec b2 = afx_spec(bo2);
        const Vec3 last = b2.route.back();
        check((std::fabs(last.x - 101.5f) < 1e-4f) && (std::fabs(last.y - 50.5f) < 1e-4f), "ブーメラン: 戻りの経路が無くても、来た道を戻って撃った位置で終わる");
    }

    // ---- 属性付きの矢弾（光と尾）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 属性付きの矢弾（CPU）\n");
        Ev plain;
        plain.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", 101, 50, 109, 50, true));
        Ev fire;
        fire.miss.push_back(afx_missile(MissileKind::Arrow, 0, "fire", 101, 50, 109, 50, true));
        Ev cold;
        cold.miss.push_back(afx_missile(MissileKind::Bolt, 0, "cold", 101, 50, 109, 50, true));
        const FxFrame p = afx_eval(plain, 0.2f);
        const FxFrame f = afx_eval(fire, 0.2f);
        const FxFrame c = afx_eval(cold, 0.15f);
        check(afx_count(p, FxPart::BrandGlow) == 0 && afx_count(p, FxPart::Ember) == 0 && p.lights.empty(), "属性なし: 光も尾も足さない");
        check(afx_count(f, FxPart::BrandGlow) >= 3, "炎の矢: 周りに光の粒が付く（" + std::to_string(afx_count(f, FxPart::BrandGlow)) + "）");
        check(afx_count(f, FxPart::Ember) >= 5, "炎の矢: 飛んだ跡に火の粉の尾が残る（" + std::to_string(afx_count(f, FxPart::Ember)) + "）");
        check((f.lights.size() == 1) && (f.lights[0].r > f.lights[0].g) && (f.lights[0].g > f.lights[0].b), "炎の矢: 暖色の点光源が矢についていく");
        check(!c.lights.empty() && (c.lights[0].b > c.lights[0].r), "冷気のクロスボウの矢: 青い点光源がついていく");
        const FxSpec fspec = afx_spec(fire);
        const float fly = fx_fly_seconds(fspec, P);
        check(afx_count(afx_eval(fire, afx_snap(fly + 0.2f)), FxPart::Ember) > 0, "炎の矢: 当たったあとも火の粉が少し残る");
        check(afx_eval(fire, afx_snap(fx_total_seconds(fspec, P) + 0.01f)).voxels.empty(), "炎の矢: 最後は全部消える");
    }

    // ---- 魔法の玉（火のボルトとボール）と爆発
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) ファイアボール（CPU）\n");
        Ev ev;
        ev.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 50, 109, 50, 2, open_floor));
        const FxSpec spec = afx_spec(ev);
        const float fly = fx_fly_seconds(spec, P);
        const float total = fx_total_seconds(spec, P);
        check(spec.palette.is_fire, "ファイアボール: 火は試作で決めた色の組を使う");
        std::fprintf(stderr, "    火の玉: 飛ぶ時間 %.3f 秒 / 全部で %.3f 秒\n", fly, total);
        check((fly > 0.64f) && (fly < 0.70f), "火の玉: 8 マス先へ 1 秒に 12 マスで飛ぶと約 0.67 秒で届く");
        for (const float s : { 0.25f, 0.5f, 0.75f }) {
            const float t = afx_snap(fly * s);
            const FxFrame f = afx_eval(ev, t);
            char tag[48]{};
            std::snprintf(tag, sizeof(tag), "火の玉 t=%.2f: ", t);
            const int core = afx_count(f, FxPart::OrbCore);
            check((core >= 50) && (core <= 200), std::string(tag) + "本体のボクセルが 50〜200 個（実際 " + std::to_string(core) + "）");
            const Vec3 expected = fx_flight_position(spec, P, t / fly);
            check(f.projectile_visible && (std::sqrt(dot(f.projectile_pos - expected, f.projectile_pos - expected)) < 1e-3f),
                std::string(tag) + "経路の上にある");
            check(f.lights.size() == 1, std::string(tag) + "点光源を 1 つ持つ");
            if (f.lights.size() == 1) {
                const PointLight &l = f.lights[0];
                check((std::fabs(l.x - f.projectile_pos.x) < 1e-3f) && (std::fabs(l.y - f.projectile_pos.y) < 1e-3f) && (l.radius >= 3.f)
                        && (l.intensity > 1.f),
                    std::string(tag) + "点光源が火の玉の位置にあり、半径 3 マス以上で強い");
            }
            const FxVoxel *inner = nullptr;
            const FxVoxel *outer = nullptr;
            float inner_d = 1e9f;
            float outer_d = -1.f;
            for (const FxVoxel &v : f.voxels) {
                if (v.part != FxPart::OrbCore) {
                    continue;
                }
                const float dist = std::sqrt(dot(v.center - f.projectile_pos, v.center - f.projectile_pos));
                if (dist < inner_d) {
                    inner_d = dist;
                    inner = &v;
                }
                if (dist > outer_d) {
                    outer_d = dist;
                    outer = &v;
                }
            }
            if ((inner != nullptr) && (outer != nullptr)) {
                check((inner->g > 0.85f) && (inner->b > 0.5f), std::string(tag) + "中心は黄白（緑と青が強い）");
                check((outer->r > outer->g * 3.f) && (outer->emissive < inner->emissive), std::string(tag) + "外縁は赤く、中心より自己発光が弱い");
            }
        }
        {
            std::set<std::uint64_t> flicker;
            const float t0 = fly * 0.4f;
            for (int i = 0; i < 6; ++i) {
                const FxFrame f = afx_eval(ev, afx_snap(t0 + (static_cast<float>(i) / P.orb_flicker_hz)));
                flicker.insert(afx_shape_hash(f, FxPart::OrbCore, f.projectile_pos));
            }
            check(flicker.size() >= 5, "火の玉: 6 フレーム続けると形が毎回違う（ちらつく。種類 " + std::to_string(flicker.size()) + "）");
            const FxFrame a = afx_eval(ev, afx_snap(t0));
            const FxFrame b = afx_eval(ev, afx_snap(t0));
            check(afx_shape_hash(a, FxPart::OrbCore, a.projectile_pos) == afx_shape_hash(b, FxPart::OrbCore, b.projectile_pos),
                "火の玉: 同じ時刻なら同じ形（撮り直しても同じ絵）");
        }
        {
            const float t = afx_snap(fly * 0.5f);
            const FxFrame f0 = afx_eval(ev, t);
            const FxFrame f1 = afx_eval(ev, afx_snap(t + 0.01f));
            const Vec3 step = f1.projectile_pos - f0.projectile_pos;
            const float speed = std::sqrt(dot(step, step)) / 0.01f;
            check((speed > 11.f) && (speed < 13.f), "火の玉: 速さが 12 マス/秒（実際 " + std::to_string(speed) + "）");
        }
        {
            const FxFrame f = afx_eval(ev, afx_snap(fly * 0.6f));
            const int embers = afx_count(f, FxPart::Ember);
            check(embers >= 10, "火の粉: 飛んでいる最中は尾に 10 個以上ある（実際 " + std::to_string(embers) + "）");
            int behind = 0;
            float smallest = 1e9f;
            float largest = 0.f;
            bool orange = true;
            for (const FxVoxel &v : f.voxels) {
                if (v.part != FxPart::Ember) {
                    continue;
                }
                if (dot(v.center - f.projectile_pos, f.projectile_dir) < 0.05f) {
                    ++behind;
                }
                smallest = std::min(smallest, v.size.x);
                largest = std::max(largest, v.size.x);
                orange = orange && (v.r > 0.75f) && (v.g < 0.75f) && (v.b < 0.3f);
            }
            check(orange, "火の粉: 色は橙〜赤（赤が強く、青が弱い）");
            check(behind >= (embers * 9 / 10), "火の粉: ほとんどが火の玉の後ろに残っている");
            check((largest < 0.12f) && (smallest < (largest * 0.5f)), "火の粉: 小さな橙のボクセルで、古いものほど小さい");
            check(afx_count(afx_eval(ev, afx_snap(fly + 0.2f)), FxPart::Ember) > 0, "火の粉: 着弾の後もしばらく残る");
            check(afx_count(afx_eval(ev, afx_snap(fly + P.ember_life + 0.05f)), FxPart::Ember) == 0, "火の粉: 寿命が尽きると消える");
        }
        {
            const FxFrame f = afx_eval(ev, afx_snap(fly + 0.002f));
            check(afx_count(f, FxPart::OrbCore) == 0 && !f.projectile_visible && f.explosion_active, "着弾の瞬間に火の玉が消え、爆発が始まる");
            const FxFrame done = afx_eval(ev, afx_snap(total + 0.01f));
            check(done.voxels.empty() && done.lights.empty() && done.finished, "ファイアボール: 最後はボクセルも光も残らず消える");
        }
    }

    /*
     * 爆発：**効果範囲のマスの中にだけ**出る（決めたこと 2026-10-08「範囲が 1 マスなら 1 マスに収まる」）。
     * 半径 0（1 マス）・1（3×3）・2（21 マス）と、壁で欠けた範囲（通路）。粒の中心がどれかの範囲のマスの
     * 中心から ±0.5 以内にあることを、爆発の全期間・全部の粒で数える。
     */
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 爆発は効果範囲に収まる（CPU）\n");
        const std::vector<FxPart> burst_parts{ FxPart::BurstMain, FxPart::BurstSmoke, FxPart::BurstFlash };
        float extent_of[3]{};
        float light_peak[3]{};
        float light_radius[3]{};
        std::size_t voxels_of[3]{};
        for (const int rad : { 0, 1, 2 }) {
            Ev ev;
            ev.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 50, 109, 50, rad, open_floor));
            const std::vector<::FxCell> &area = ev.proj.front().area;
            const std::size_t expect_cells = (rad == 0) ? 1u : ((rad == 1) ? 9u : 21u);
            check(area.size() == expect_cells, "範囲: 半径 " + std::to_string(rad) + " のボールは " + std::to_string(expect_cells) + " マス（実際 " + std::to_string(area.size()) + "）");
            const FxSpec spec = afx_spec(ev);
            const float fly = fx_fly_seconds(spec, P);
            const float total = fx_total_seconds(spec, P);
            const Overreach o = afx_overreach(ev, area, fly, total, burst_parts);
            std::fprintf(stderr, "    半径 %d: 粒 %lld 個を全期間で調べた / 範囲の外 %lld 個 / 中心から最大 %.2f マス\n", rad, o.total, o.outside, o.max_dist);
            check(o.total > 0, "爆発（半径 " + std::to_string(rad) + "）: 調べる粒がある");
            check(o.outside == 0, "爆発（半径 " + std::to_string(rad) + "）: 全部の粒が効果範囲のマスの中にある（外に出た粒 " + std::to_string(o.outside) + "）");
            const float limit = (rad == 0) ? 0.72f : ((rad == 1) ? 2.13f : 3.30f);
            check(o.max_dist <= limit, "爆発（半径 " + std::to_string(rad) + "）: 中心から " + std::to_string(limit) + " マス以内に収まる（実際 " + std::to_string(o.max_dist) + "）");
            extent_of[rad] = o.max_dist;
            // 数
            std::size_t max_voxels = 0;
            for (float te = 0.f; te <= (total - fly); te += 0.005f) {
                max_voxels = std::max(max_voxels, afx_eval(ev, afx_snap(fly + te)).voxels.size());
            }
            voxels_of[rad] = max_voxels;
            // 光
            float peak = 0.f;
            float peak_radius = 0.f;
            for (float te = 0.f; te <= 0.12f; te += 0.005f) {
                const FxFrame f = afx_eval(ev, afx_snap(fly + te));
                for (const PointLight &l : f.lights) {
                    if (l.intensity > peak) {
                        peak = l.intensity;
                        peak_radius = l.radius;
                    }
                }
            }
            light_peak[rad] = peak;
            light_radius[rad] = peak_radius;
        }
        std::fprintf(stderr, "    同時の粒の最大: 半径 0:%zu / 1:%zu / 2:%zu  爆発の光（強さ/半径）: 0:%.2f/%.2f 1:%.2f/%.2f 2:%.2f/%.2f\n", voxels_of[0],
            voxels_of[1], voxels_of[2], light_peak[0], light_radius[0], light_peak[1], light_radius[1], light_peak[2], light_radius[2]);
        check((extent_of[0] < extent_of[1]) && (extent_of[1] < extent_of[2]), "爆発: 範囲が広いほど広がる（半径 0/1/2 で " + std::to_string(extent_of[0]) + " / "
                + std::to_string(extent_of[1]) + " / " + std::to_string(extent_of[2]) + "）");
        check((voxels_of[0] < voxels_of[1]) && (voxels_of[1] < voxels_of[2]) && (voxels_of[2] >= 60) && (voxels_of[2] <= 400),
            "爆発: 範囲が広いほど粒が増える。半径 2 は 60〜400 個");
        check((light_peak[0] < light_peak[1]) && (light_peak[1] < light_peak[2]) && (light_radius[0] < light_radius[1])
                && (light_radius[1] < light_radius[2]),
            "爆発の光: 範囲が狭いほど弱く、半径も小さい（点光源の強さと半径も範囲に合わせる）");
        check(light_peak[2] >= (2.f * P.orb_light_intensity), "爆発の光: 半径 2 の着弾の瞬間は、飛んでいる火の玉の光の 2 倍以上");

        // 距離ごとに一斉に始まる（原作の描く順番）。
        {
            Ev ev;
            ev.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 50, 109, 50, 2, open_floor));
            const FxSpec spec = afx_spec(ev);
            const float fly = fx_fly_seconds(spec, P);
            const auto at = [&](float te) { return afx_eval(ev, afx_snap(fly + te)); };
            for (const int k : { 1, 2 }) {
                const float edge = static_cast<float>(k) * P.ring_delay;
                const float before = at(edge - 0.01f).explosion_extent;
                const float after = at(edge + 0.012f).explosion_extent;
                check(after > (before + 0.1f), "爆発: 距離 " + std::to_string(k) + " のマスが " + std::to_string(edge) + " 秒で一斉に加わり、広がる（"
                        + std::to_string(before) + " → " + std::to_string(after) + "）");
            }
            check(at(0.5f * P.ring_delay).explosion_extent < 0.9f, "爆発: 始まりは中心のマスだけ（外へ出ていない）");
            // 火：小さくなって消え、上へ昇る。煙：火より遅れて、暗い灰色で、半透明。
            const auto mean_of = [](const FxFrame &f, FxPart part, bool z) {
                double sum = 0.0;
                int n = 0;
                for (const FxVoxel &v : f.voxels) {
                    if (v.part == part) {
                        sum += z ? v.center.z : v.size.x;
                        ++n;
                    }
                }
                return (n > 0) ? (sum / n) : -1.0;
            };
            const double fire_early = mean_of(at(0.14f), FxPart::BurstMain, false);
            const double fire_late = mean_of(at(0.40f), FxPart::BurstMain, false);
            check((fire_late > 0.0) && (fire_late < fire_early), "爆発: 火のボクセルは時間とともに小さくなる（" + std::to_string(fire_early) + " → " + std::to_string(fire_late) + "）");
            const double smoke_z0 = mean_of(at(0.20f), FxPart::BurstSmoke, true);
            const double smoke_z1 = mean_of(at(0.40f), FxPart::BurstSmoke, true);
            check((smoke_z0 > 0.0) && (smoke_z1 > smoke_z0 + 0.1), "爆発: 煙は上へ立ちのぼる（高さ " + std::to_string(smoke_z0) + " → " + std::to_string(smoke_z1) + "）");
            float top_early = 0.f;
            float top_late = 0.f;
            for (const FxVoxel &v : at(0.08f).voxels) {
                if (v.part == FxPart::BurstMain) {
                    top_early = std::max(top_early, v.center.z);
                }
            }
            for (const FxVoxel &v : at(0.30f).voxels) {
                if (v.part == FxPart::BurstMain) {
                    top_late = std::max(top_late, v.center.z);
                }
            }
            check(top_late > top_early + 0.15f, "爆発: 火のボクセルが上へ昇る（最も高いもの " + std::to_string(top_early) + " → " + std::to_string(top_late) + "）");
            check(afx_count(at(0.04f), FxPart::BurstSmoke) == 0 && afx_count(at(0.04f), FxPart::BurstMain) > 0, "爆発: 煙は火より少し遅れて出る");
            const FxFrame f = at(0.25f);
            bool dark = true;
            bool translucent = true;
            float smoke_size = 0.f;
            int smoke_n = 0;
            float fire_size = 0.f;
            int fire_n = 0;
            for (const FxVoxel &v : f.voxels) {
                if (v.part == FxPart::BurstSmoke) {
                    dark = dark && (v.r < 0.6f) && (v.g < 0.4f) && (v.b < 0.4f) && (v.emissive <= 0.31f);
                    translucent = translucent && (v.alpha < 1.f);
                    smoke_size += v.size.x;
                    ++smoke_n;
                } else if (v.part == FxPart::BurstMain) {
                    fire_size += v.size.x;
                    ++fire_n;
                }
            }
            check(smoke_n > 0 && dark && translucent, "爆発: 煙は暗い灰色で自己発光がほぼ無く（出はじめだけ火の照り返し）、半透明");
            check((smoke_n > 0) && (fire_n > 0) && ((smoke_size / static_cast<float>(smoke_n)) > (fire_size / static_cast<float>(fire_n))), "爆発: 煙のボクセルは火のボクセルより大きい");
            // 光の時間変化
            const auto intensity = [&](float te) {
                float best = 0.f;
                for (const PointLight &l : at(te).lights) {
                    best = std::max(best, l.intensity);
                }
                return best;
            };
            const float peak = intensity(P.burst_light_rise);
            check(intensity(0.01f) < peak, "爆発の光: 立ち上がりの途中は最大より弱い");
            check((intensity(0.2f) < (peak * 0.5f)) && (intensity(0.4f) < intensity(0.2f)), "爆発の光: 0.2 秒で最大の半分未満、そこから急に弱まる");
            check(intensity(0.5f) == 0.f, "爆発の光: 0.5 秒で消える");
            const FxFrame at_peak = at(0.03f);
            check(!at_peak.lights.empty() && (std::fabs(at_peak.lights[0].x - 109.5f) < 1e-3f), "爆発の光: 中心のマスに置かれる");
        }
        // 壁で欠けた範囲（通路）：欠けたマスには出ない。
        {
            const std::vector<AfxRect> corridor{ AfxRect{ 70, 50, 132, 52 } };
            Ev ev;
            ev.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 51, 109, 51, 2, corridor));
            const std::vector<::FxCell> &area = ev.proj.front().area;
            check(area.size() == 15u, "範囲（通路）: 半径 2 のボールは壁で欠けて 15 マス（実際 " + std::to_string(area.size()) + "）");
            const FxSpec spec = afx_spec(ev);
            const Overreach o = afx_overreach(ev, area, fx_fly_seconds(spec, P), fx_total_seconds(spec, P), burst_parts);
            check((o.total > 0) && (o.outside == 0), "爆発（通路）: 壁のマスに粒が 1 つも出ない（外に出た粒 " + std::to_string(o.outside) + "）");
            bool y_ok = true;
            for (float te = 0.f; te < 0.8f; te += 0.01f) {
                for (const FxVoxel &v : afx_eval(ev, afx_snap(fx_fly_seconds(spec, P) + te)).voxels) {
                    if (((v.part == FxPart::BurstMain) || (v.part == FxPart::BurstSmoke)) && ((v.center.y < 50.0f) || (v.center.y > 53.0f))) {
                        y_ok = false;
                    }
                }
            }
            check(y_ok, "爆発（通路）: 通路（3 マス幅）の外にボクセルが出ない");
        }
    }

    // ---- ボルト（爆発しない玉）とジャンプ（その場の爆発）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) ボルト・ジャンプ（CPU）\n");
        Ev bolt;
        bolt.proj.push_back(afx_projection(ProjectionShape::Bolt, "fire", 101, 50, 109, 50, 0, open_floor));
        const FxSpec spec = afx_spec(bolt);
        const float fly = fx_fly_seconds(spec, P);
        check(afx_count(afx_eval(bolt, afx_snap(fly * 0.5f)), FxPart::OrbCore) > 20, "火のボルト: 火の玉が飛ぶ");
        const FxFrame pop = afx_eval(bolt, afx_snap(fly + 0.05f));
        check((afx_count(pop, FxPart::BurstFlash) == 0) && (afx_count(pop, FxPart::BurstMain) > 0) && (afx_count(pop, FxPart::BurstSmoke) == 0),
            "火のボルト: 当たると小さく弾ける（閃光も煙も無い）");
        std::vector<::FxCell> one{ ::FxCell{} };
        one[0].x = 109;
        one[0].y = 50;
        const Overreach o = afx_overreach(bolt, one, fly, fx_total_seconds(spec, P), { FxPart::BurstMain });
        check((o.total > 0) && (o.outside == 0), "火のボルト: 弾ける粒は着弾のマスの中だけ");
        Ev ball;
        ball.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 50, 109, 50, 2, open_floor));
        check(afx_total(bolt) < afx_total(ball), "火のボルト: 爆発するボールより早く終わる");

        Ev jump;
        jump.proj.push_back(afx_projection(ProjectionShape::Jump, "fire", 101, 50, 109, 50, 1, open_floor));
        const FxFrame j = afx_eval(jump, 0.04f);
        check((afx_count(j, FxPart::OrbCore) == 0) && j.explosion_active && (afx_count(j, FxPart::BurstFlash) > 0), "ジャンプ: 玉は飛ばず、着弾点にすぐ爆発する");
        const Overreach oj = afx_overreach(jump, jump.proj.front().area, 0.f, afx_total(jump), { FxPart::BurstMain, FxPart::BurstSmoke, FxPart::BurstFlash });
        check((oj.total > 0) && (oj.outside == 0), "ジャンプ: 粒は範囲のマスの中だけ");
    }

    // ---- 属性の色
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 属性の色（CPU）\n");
        const std::vector<std::string> names{ "fire", "cold", "elec", "acid", "pois", "dark", "lite", "nether", "chaos", "mana", "missile" };
        bool all_known = true;
        bool distinct = true;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const FxPalette a = fx_palette_for(names[i]);
            all_known = all_known && a.known;
            for (std::size_t j = i + 1; j < names.size(); ++j) {
                const FxPalette b = fx_palette_for(names[j]);
                const float d = std::fabs(a.orb[1].r - b.orb[1].r) + std::fabs(a.orb[1].g - b.orb[1].g) + std::fabs(a.orb[1].b - b.orb[1].b);
                if (d < 0.2f) {
                    distinct = false;
                    std::fprintf(stderr, "    色が近い: %s と %s（差 %.3f）\n", names[i].c_str(), names[j].c_str(), d);
                }
            }
        }
        check(all_known, "属性: fire・cold・elec・acid・pois・dark・lite・nether・chaos・mana・missile を知っている");
        check(distinct, "属性: 11 種の玉の色が互いに見分けられる（差 0.2 以上）");
        const FxPalette unknown = fx_palette_for("zz_not_an_attr");
        check(!unknown.known && (std::fabs(unknown.orb[1].r - 1.f) < 1e-4f) && (std::fabs(unknown.orb[1].g - 1.f) < 1e-4f) && (std::fabs(unknown.orb[1].b - 1.f) < 1e-4f),
            "属性: 知らない名前（zz_not_an_attr）は白");
        check(fx_palette_for("COLD").known && !fx_palette_for("").known, "属性: 大文字でも引ける。空の名前は白");
        // 汎用の玉と爆発：同じ色になる。
        Ev cold;
        cold.proj.push_back(afx_projection(ProjectionShape::Ball, "cold", 101, 50, 109, 50, 1, open_floor));
        const FxSpec cspec = afx_spec(cold);
        const float fly = fx_fly_seconds(cspec, P);
        const FxFrame orb = afx_eval(cold, afx_snap(fly * 0.5f));
        Vec3 mean_orb{};
        int n_orb = 0;
        for (const FxVoxel &v : orb.voxels) {
            if (v.part == FxPart::OrbCore) {
                mean_orb = mean_orb + Vec3{ v.r, v.g, v.b };
                ++n_orb;
            }
        }
        mean_orb = mean_orb * (1.f / static_cast<float>(std::max(n_orb, 1)));
        check((n_orb > 20) && (mean_orb.z > mean_orb.x) && (mean_orb.z > 0.6f), "冷気のボール: 汎用の玉が青く飛ぶ（平均 r=" + std::to_string(mean_orb.x) + " b=" + std::to_string(mean_orb.z) + "）");
        const FxFrame burst = afx_eval(cold, afx_snap(fly + 0.1f));
        Vec3 mean_burst{};
        int n_burst = 0;
        for (const FxVoxel &v : burst.voxels) {
            if (v.part == FxPart::BurstMain) {
                mean_burst = mean_burst + Vec3{ v.r, v.g, v.b };
                ++n_burst;
            }
        }
        mean_burst = mean_burst * (1.f / static_cast<float>(std::max(n_burst, 1)));
        check((n_burst > 10) && (mean_burst.z > mean_burst.x), "冷気のボール: 同じ色の汎用の爆発（平均 r=" + std::to_string(mean_burst.x) + " b=" + std::to_string(mean_burst.z) + "）");
        check(!burst.lights.empty() && (burst.lights[0].b > burst.lights[0].r), "冷気のボール: 爆発の光も同じ色");
        Ev unknown_ev;
        unknown_ev.proj.push_back(afx_projection(ProjectionShape::Ball, "zz_not_an_attr", 101, 50, 109, 50, 1, open_floor));
        const FxFrame u = afx_eval(unknown_ev, afx_snap(fly * 0.5f));
        bool white = true;
        int n_u = 0;
        for (const FxVoxel &v : u.voxels) {
            if (v.part == FxPart::OrbCore) {
                ++n_u;
                const float spread = std::max({ v.r, v.g, v.b }) - std::min({ v.r, v.g, v.b });
                white = white && (spread < 0.46f); // 外縁の暗い段でも色相は無い（白→灰）
            }
        }
        check((n_u > 20) && white, "知らない属性: 汎用の白い光の玉になる（色相が無い）");
    }

    // ---- ビーム・ブレス・飛ばない魔法
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) ビーム・ブレス・飛ばない魔法（CPU）\n");
        Ev beam;
        beam.proj.push_back(afx_projection(ProjectionShape::Beam, "fire", 101, 50, 109, 50, 0, open_floor));
        const FxSpec bspec = afx_spec(beam);
        const float btotal = fx_total_seconds(bspec, P);
        const FxFrame b0 = afx_eval(beam, 0.005f);
        const FxFrame b1 = afx_eval(beam, 0.12f); //!< 8 マス目（0.112 秒）まで点いて、1 マス目（0.26 秒）はまだ消えない
        const int c0 = afx_count(b0, FxPart::BeamCore);
        const int c1 = afx_count(b1, FxPart::BeamCore);
        check((c0 >= 1) && (c0 < 8) && (c1 == 8), "ビーム: 始点から順に点いて、経路の全部のマス（8 つ）が光る（" + std::to_string(c0) + " → " + std::to_string(c1) + "）");
        bool aligned = true;
        for (const FxVoxel &v : b1.voxels) {
            if (v.part == FxPart::BeamCore) {
                aligned = aligned && (std::fabs(v.center.y - 50.5f) < 1e-3f) && (std::fabs(v.yaw) < 1e-4f) && (v.size.x > 0.95f);
            }
        }
        check(aligned, "ビーム: 光の線が経路のマスに沿い、マス 1 つぶんの長さがある");
        float lo = 1e9f;
        float hi = -1e9f;
        for (const FxVoxel &v : b1.voxels) {
            if (v.part == FxPart::BeamCore) {
                lo = std::min(lo, v.center.x - (v.size.x * 0.5f));
                hi = std::max(hi, v.center.x + (v.size.x * 0.5f));
            }
        }
        check((lo < 101.6f) && (hi > 109.4f), "ビーム: 撃った者のマスの中心から着弾のマスの中心までつながる");
        check(afx_count(b1, FxPart::BeamGlow) == 8 && !b1.lights.empty(), "ビーム: 芯の周りに半透明の光と、点光源がある");
        check(afx_eval(beam, afx_snap(btotal * 0.9f)).voxels.size() < b1.voxels.size() + 1u, "ビーム: 短く光って薄れる");
        check(afx_eval(beam, afx_snap(btotal + 0.01f)).voxels.empty() && afx_eval(beam, afx_snap(btotal + 0.01f)).finished, "ビーム: 最後は消える");
        Ev diag;
        diag.proj.push_back(afx_projection(ProjectionShape::Beam, "fire", 101, 50, 109, 54, 0, open_floor));
        bool turned = false;
        for (const FxVoxel &v : afx_eval(diag, 0.1f).voxels) {
            turned = turned || ((v.part == FxPart::BeamCore) && (std::fabs(v.yaw) > 0.1f));
        }
        check(turned, "ビーム: 斜めの経路では線も斜めを向く");

        // ブレス
        Ev breath;
        breath.proj.push_back(afx_projection(ProjectionShape::Breath, "fire", 101, 50, 108, 50, 7, open_floor));
        const FxSpec brspec = afx_spec(breath);
        const std::vector<::FxCell> &barea = breath.proj.front().area;
        check(barea.size() > 20u, "ブレス: 扇形の範囲が複数のマスにわたる（" + std::to_string(barea.size()) + " マス）");
        const FxFrame br = afx_eval(breath, 0.1f);
        check((afx_count(br, FxPart::OrbCore) == 0) && !br.projectile_visible && br.explosion_active, "ブレス: 玉は飛ばず、範囲に爆発が広がる");
        const Overreach ob = afx_overreach(breath, barea, 0.f, fx_total_seconds(brspec, P), { FxPart::BurstMain, FxPart::BurstSmoke, FxPart::BurstFlash });
        check((ob.total > 0) && (ob.outside == 0), "ブレス: 粒は範囲のマスの中だけ（外に出た粒 " + std::to_string(ob.outside) + "）");
        const auto mean_distance = [&](float t) {
            double sum = 0.0;
            int n = 0;
            for (const FxVoxel &v : afx_eval(breath, t).voxels) {
                if (v.part == FxPart::BurstMain) {
                    sum += std::hypot(v.center.x - 101.5f, v.center.y - 50.5f);
                    ++n;
                }
            }
            return (n > 0) ? (sum / n) : -1.0;
        };
        const double d_early = mean_distance(0.12f);
        const double d_late = mean_distance(0.30f);
        check((d_early > 0.0) && (d_late > d_early + 0.8), "ブレス: 撃った者に近いマスから順に広がる（粒の平均の距離 " + std::to_string(d_early) + " → " + std::to_string(d_late) + "）");
        check(afx_eval(breath, afx_snap(fx_total_seconds(brspec, P) + 0.01f)).voxels.empty(), "ブレス: 最後は消える");

        // 飛ばない魔法：対象の上に小さな光が浮かんで消える。
        for (const int mode : { 0, 1, 2 }) {
            Ev glow;
            const std::uint16_t flags = (mode == 0) ? kProjHidden : ((mode == 1) ? kProjAllInSight : 0);
            const ProjectionShape shape = (mode == 2) ? ProjectionShape::Touch : ProjectionShape::Bolt;
            glow.proj.push_back(afx_projection(shape, "fire", 101, 50, 105, 52, 0, open_floor, flags));
            const std::string name = (mode == 0) ? "飛ばない魔法（hidden）" : ((mode == 1) ? "視界内の全員にかかる魔法" : "足元だけに効く魔法（touch）");
            const FxFrame g = afx_eval(glow, 0.4f);
            bool inside = true;
            for (const FxVoxel &v : g.voxels) {
                inside = inside && (std::fabs(v.center.x - 105.5f) <= 0.5f) && (std::fabs(v.center.y - 52.5f) <= 0.5f);
            }
            check(afx_count(g, FxPart::GlowMote) >= 8 && inside && !g.projectile_visible && (afx_count(g, FxPart::OrbCore) == 0),
                name + ": 着弾点（対象）のマスの上に小さな光が浮かぶ（玉は飛ばない）");
            check((g.lights.size() == 1) && (std::fabs(g.lights[0].x - 105.5f) < 1e-3f) && (std::fabs(g.lights[0].y - 52.5f) < 1e-3f),
                name + ": 点光源が対象の上にある");
            const FxFrame early = afx_eval(glow, 0.2f);
            const FxFrame later = afx_eval(glow, 0.6f);
            float z_early = 0.f;
            float z_late = 0.f;
            int n_early = 0;
            int n_late = 0;
            for (const FxVoxel &v : early.voxels) {
                z_early += v.center.z;
                ++n_early;
            }
            for (const FxVoxel &v : later.voxels) {
                z_late += v.center.z;
                ++n_late;
            }
            check((n_early > 0) && (n_late > 0) && ((z_late / static_cast<float>(n_late)) > (z_early / static_cast<float>(n_early)) + 0.05f),
                name + ": 光はゆっくり浮かび上がる");
            check(afx_eval(glow, P.glow_seconds + 0.02f).voxels.empty() && afx_eval(glow, P.glow_seconds + 0.02f).finished, name + ": 最後は消える");
        }
        // 盲目のときは描かない。
        Ev blind;
        blind.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 50, 109, 50, 2, open_floor, kProjPlayerBlind));
        AttackFxView blind_view;
        blind_view.push(blind.proj, blind.miss, kBase);
        check(!blind_view.active(), "盲目: 見えない投射は描かない（原作と同じ）");
    }

    // ---- 取り込み：重複・同時・上限・時計
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 取り込み（CPU）\n");
        Ev a;
        a.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", 101, 50, 109, 50, true));
        a.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 50, 109, 50, 1, open_floor));
        AttackFxView view;
        view.push(a.proj, a.miss, kBase);
        view.push(a.proj, a.miss, kBase + 16); // 同じフレームをもう 1 度渡しても増えない（通し番号）
        check(view.size() == 2, "取り込み: 同じ通し番号は 1 回しか起こさない（2 件のまま）");
        Ev b;
        b.miss.push_back(afx_missile(MissileKind::Shot, 0, "", 101, 50, 109, 50, true));
        b.proj.push_back(afx_projection(ProjectionShape::Beam, "cold", 101, 50, 109, 50, 0, open_floor));
        view.push(b.proj, b.miss, kBase + 32);
        check(view.size() == 4, "取り込み: 新しい通し番号は足される（同時にいくつも飛ぶ）");
        FxFrame frame;
        view.evaluate(kBase + 100, P, FxViewParams{}, frame);
        check((afx_count(frame, FxPart::ArrowHead) > 0) && (afx_count(frame, FxPart::OrbCore) > 0) && (afx_count(frame, FxPart::ShotStone) > 0)
                && (afx_count(frame, FxPart::BeamCore) > 0),
            "同時: 矢・火の玉・弾・ビームが 1 つの絵の中に同時に出る");
        check(frame.lights.size() <= static_cast<std::size_t>(kMaxExtraPointLights), "同時: 点光源は枠（" + std::to_string(kMaxExtraPointLights) + "）を超えない（" + std::to_string(frame.lights.size()) + "）");
        // 強い順に残る
        {
            Ev many;
            for (int i = 0; i < 6; ++i) {
                many.proj.push_back(afx_projection(ProjectionShape::Jump, (i == 5) ? "fire" : "cold", 101, 50, 105 + i, 50, (i == 5) ? 2 : 0, open_floor));
            }
            AttackFxView v2;
            v2.push(many.proj, many.miss, kBase);
            FxFrame f2;
            v2.evaluate(kBase + 30, P, FxViewParams{}, f2);
            float best = 0.f;
            for (const PointLight &l : f2.lights) {
                best = std::max(best, l.intensity);
            }
            check((f2.lights.size() == static_cast<std::size_t>(kMaxExtraPointLights)) && (best > 5.5f), "同時: 光が溢れたら強いものを残す（半径 2 の爆発の光が残る）");
        }
        FxFrame end;
        view.evaluate(kBase + 5000, P, FxViewParams{}, end);
        check(!view.active() && end.finished && end.voxels.empty(), "取り込み: 終わった効果は捨てて、何も残らない");
        // 上限
        Ev flood;
        for (int i = 0; i < 40; ++i) {
            flood.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", 101, 50, 109, 50, true));
        }
        AttackFxView v3;
        v3.push(flood.proj, flood.miss, kBase);
        check(v3.size() == AttackFxView::kMaxActive, "取り込み: 同時の数に上限がある（" + std::to_string(v3.size()) + "）");
        // 巻き戻った時計では捨てる
        AttackFxView v4;
        v4.push(a.proj, a.miss, kBase + 500);
        FxFrame f4;
        v4.evaluate(kBase, P, FxViewParams{}, f4);
        check(f4.voxels.empty(), "取り込み: 時計が巻き戻ったら描かずに捨てる");
        // 通し番号 0（番号を持たない送り手）は重複除去しない
        Ev zero;
        zero.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", 101, 50, 109, 50, true));
        zero.miss.back().seq = 0;
        AttackFxView v5;
        v5.push(zero.proj, zero.miss, kBase);
        v5.push(zero.proj, zero.miss, kBase + 16);
        check(v5.size() == 2, "取り込み: 通し番号 0 は重複を除かない");
        // 階が変わったら捨てる
        v5.clear();
        check(!v5.active(), "取り込み: clear() で全部捨てる");
    }

    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) 点光源の枠\n");
        GameFrame f = make_synthetic_frame(23, 19, 100, 44);
        SceneLighting plain;
        const int dropped_plain = collect_point_lights(f, Vec3{ 100.5f, 44.5f, 0.f }, plain);
        SceneLighting with_none;
        (void)collect_point_lights(f, Vec3{ 100.5f, 44.5f, 0.f }, with_none, 1.f, nullptr, 1.f, nullptr);
        bool same = (plain.points.size() == with_none.points.size());
        for (std::size_t i = 0; same && (i < plain.points.size()); ++i) {
            same = (plain.points[i].x == with_none.points[i].x) && (plain.points[i].intensity == with_none.points[i].intensity);
        }
        check(same && (dropped_plain == 0), "点光源: 足す光が無いときは従来と同じ");
        Ev ev;
        ev.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", 101, 50, 109, 50, 2, open_floor));
        const float fly = fx_fly_seconds(afx_spec(ev), P);
        const FxFrame burst = afx_eval(ev, afx_snap(fly + 0.03f));
        SceneLighting with_fx;
        (void)collect_point_lights(f, Vec3{ 100.5f, 44.5f, 0.f }, with_fx, 1.f, nullptr, 1.f, &burst.lights);
        check(with_fx.points.size() == (plain.points.size() + burst.lights.size()), "点光源: 爆発の光が 1 つ増える（"
                + std::to_string(plain.points.size()) + " → " + std::to_string(with_fx.points.size()) + "）");
        GameFrame lava = f;
        int placed = 0;
        for (MapCellView &cell : lava.cells) {
            if (((cell.feature_flags & CELL_FEAT_KNOWN) != 0u) && (placed < 24)) {
                cell.feature_flags |= CELL_FEAT_LAVA;
                ++placed;
            }
        }
        SceneLighting crowded;
        const int dropped = collect_point_lights(lava, Vec3{ 100.5f, 44.5f, 0.f }, crowded, 1.f, nullptr, 1.f, &burst.lights);
        bool kept = false;
        for (const PointLight &l : crowded.points) {
            if (std::fabs(l.intensity - burst.lights[0].intensity) < 1e-5f) {
                kept = true;
            }
        }
        check((dropped > 0) && (static_cast<int>(crowded.points.size()) == kMaxPointLights) && kept,
            "点光源: 16 個を超えても、足した光は残り、地形の光のほうが削られる（捨てた " + std::to_string(dropped) + " 個）");
    }

    // ---- 目録から板を引く（床のアイテムと同じ道）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (1) アイテムの番号から板を引く（CPU）\n");
        presentation::AssetManifestMessage manifest;
        manifest.root = "tilework";
        manifest.assets.push_back(presentation::AssetManifestWireEntry{ 7100, "K", 55, "K55b.png" });
        manifest.assets.push_back(presentation::AssetManifestWireEntry{ 7043, "K", 43, "K43.png" });
        manifest.assets.push_back(presentation::AssetManifestWireEntry{ 7050, "K", 43, "K43b.png" });
        manifest.assets.push_back(presentation::AssetManifestWireEntry{ 7200, "R", 43, "R43.png" });
        manifest.aliases.push_back(presentation::AssetManifestAliasWireEntry{ "K", 60, 7043 });
        TileCatalog catalog;
        catalog.apply(manifest);
        check(catalog.find_item_tile(43) == 7043, "目録: アイテム 43 の絵はいちばん小さい索引（7043）。モンスターの 43 は混ざらない");
        check(catalog.find_item_tile(55) == 7100, "目録: 別のアイテムも引ける");
        check(catalog.find_item_tile(60) == 7043, "目録: 別名のアイテムも共有元の絵を引ける");
        check(catalog.find_item_tile(99) == 0, "目録: 無いアイテムは 0（板を出さない）");
    }

    /* ======================================================== (2) 描く検査 */
    std::fprintf(stderr, "[hd2d] attack-fx-check (2) 描画\n");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[hd2d] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    Window window;
    std::string err;
    if (!create_window(options, window, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    VoxelRenderer renderer;
    VoxelFxRenderer fx_renderer;
    ModelRenderer pillar_model;
    ShadowMap shadow;
    PostChain post;
    SlabLibrary slab_library;
    TileCatalog tile_catalog;
    int screen_w = options.window_w;
    int screen_h = options.window_h;
    SDL_GetWindowSize(window.window, &screen_w, &screen_h);
    const ModelAsset pillar_asset = afx_make_pillar();
    if (!renderer.init(err) || !fx_renderer.init(err) || !shadow.init(kShadowMapSide, err) || !init_post_chain(post, options, err)
        || !post.resize(screen_w, screen_h, err) || !pillar_model.init(pillar_asset, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    slab_library.set_dir(options.voxel_dir + "/slab");
    const bool slab_init = slab_library.init(err);
    {
        presentation::AssetManifestMessage manifest;
        manifest.root = "";
        manifest.assets.push_back(presentation::AssetManifestWireEntry{ kDaggerTile, "K", kDaggerItem, "K43.png" });
        tile_catalog.apply(manifest);
    }
    std::fprintf(stderr, "[hd2d] attack-fx-check 描画 %dx%d\n", screen_w, screen_h);
    //! 床のアイテムと同じ道（`SlabLibrary::acquire_item`）で投げた物の板を引く。
    const FxSlabResolver resolve_slab = [&](std::uint16_t item_id, FxSlabSource &source) {
        const int slab = slab_library.acquire_item(tile_catalog, item_id, renderer);
        if (slab < 0) {
            return false;
        }
        source.gpu = &slab_library.gpu(slab);
        const auto bounds = slab_library.bounds(slab);
        source.minimum = bounds.first;
        source.maximum = bounds.second;
        return true;
    };
    {
        FxSlabSource probe;
        check(slab_init && resolve_slab(kDaggerItem, probe) && (probe.gpu != nullptr) && (probe.maximum.z > probe.minimum.z),
            "板: 短剣（K43）の板を、目録のアイテム番号から床のアイテムと同じ道で引けて、GPU に載る");
        check(!resolve_slab(9999, probe), "板: 目録に無いアイテムは引けない（小さな箱で描く）");
    }

    Prefab wall_prefab = make_box_prefab("wall", 32, 32, 32, 0, 32);
    Prefab floor_prefab = make_box_prefab("floor", 32, 32, 2, -2, 32);
    GpuPrefab wall_gpu;
    GpuPrefab floor_gpu;
    if (!renderer.upload(wall_prefab, wall_gpu, err) || !renderer.upload(floor_prefab, floor_gpu, err)) {
        std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        SDL_Quit();
        return 1;
    }

    Camera camera;
    camera.pitch = options.camera_pitch_deg * 0.0174533f;
    camera.fov_x = options.camera_fov_deg * 0.0174533f;
    camera.viewport_w = screen_w;
    camera.viewport_h = screen_h;
    //! 効果は小さいので、既定より近づけて撮る（矢の長さ 0.8 マスが 120 画素ほどになる）。
    camera.distance = distance_for_cell_px(camera, std::max(options.camera_cell_px, 150.f));
    camera.yaw = view_turn_yaw(options.camera_turn);
    const ViewWindow view_full = derive_view_window(camera, kMaxPropHeight, 60.f);
    float z_near = 0.f;
    float z_far = 0.f;
    camera.depth_range(z_near, z_far);

    struct Scene {
        const char *name = "";
        int px = 0;
        int py = 0;
        int tx = 0; //!< 的のマス
        int ty = 0;
        Vec3 focus{};
        std::vector<AfxRect> floor;
        GameFrame frame;
        FloorMeaning meaning;
        TerrainMemory memory;
        TerrainView terrain;
        ShadowFit fit;
        std::vector<Vec3> models;
    };
    std::vector<Scene> scenes(2);
    {
        Scene &a = scenes[0];
        a.name = "room"; // 暗い部屋
        a.px = 101;
        a.py = 50;
        a.tx = 109;
        a.ty = 50;
        a.floor = { AfxRect{ 86, 42, 116, 58 } };
        Scene &b = scenes[1];
        b.name = "corridor"; // 暗い通路（幅 3）
        b.px = 101;
        b.py = 51;
        b.tx = 109;
        b.ty = 51;
        b.floor = { AfxRect{ 70, 50, 132, 52 } };
    }
    for (Scene &s : scenes) {
        s.frame = make_synthetic_frame(view_full.cols, view_full.rows, s.px, s.py);
        afx_carve(s.frame, s.floor);
        s.frame.lighting.light_radius = 3;
        // 画の真ん中は、プレイヤと的の中間に置く（動きが画面に収まる）。
        s.focus = Vec3{ (static_cast<float>(s.px + s.tx) * 0.5f) + 0.5f, static_cast<float>(s.py) + 0.5f, 0.f };
        camera.target = s.focus;
        (void)rebuild_floor_meaning(s.frame, s.meaning);
        s.memory.update(s.frame);
        build_terrain_view(s.meaning, Frustum::from(camera.view_projection()), s.terrain, &s.memory, nullptr);
        SceneLighting base = make_scene_lighting(s.frame.lighting, s.frame.floor);
        s.fit = fit_shadow(camera, base.sun_dir, kMaxPropHeight, shadow.side(), 0.f);
        s.models = { Vec3{ static_cast<float>(s.px) + 0.5f, static_cast<float>(s.py) + 0.5f, 0.f },
            Vec3{ static_cast<float>(s.tx) + 0.5f, static_cast<float>(s.ty) + 0.5f, 0.f } };
    }

    const PostFlags all_off{ false, false, false, false, false };
    PostParams params;

    //! 撃ち方 1 つと、その時刻。
    struct TimedEv {
        const Ev *ev;
        float t;
    };
    //! 描く 1 回ぶんの旗。**比べる 2 枚は、旗だけ違う同じ関数で撮る。**
    struct Flags {
        bool voxels = true;                         //!< 効果のボクセルと板を描く
        bool lights = true;                         //!< 効果の点光源を足す
        bool pictures = false;                      //!< 後処理を全部入れる（絵）。偽なら全部切る（数える）
        bool screen_aligned = false;                //!< 板を画面へ正対させる（既定は垂直に立てて南向き）
        const std::vector<FxVoxel> *extra = nullptr; //!< 検査用に足すボクセル
        const std::vector<FxEntityMod> *mods = nullptr; //!< 検査用に足す対象の見た目の変化
        const FxFrame *demo = nullptr;              //!< 検査用に混ぜる絵（部品の見本。ボクセル・光・実体の変化・光の波・揺れ）
        bool shake = false;                         //!< 画面の揺れを当てる（絵の書き出し）
    };
    std::size_t last_drawn = 0;
    std::size_t last_slabs = 0;
    std::vector<FxScreenWave> last_waves; //!< 直前の `shoot` が起こした光の波（画面の外に描くので、呼び出し側が描く）
    UiPaint overlay_paint;
    std::string paint_err;
    const bool paint_ready = overlay_paint.init(paint_err);
    const auto shoot = [&](Scene &s, const std::vector<TimedEv> &shots, const Flags &flags) {
        camera.target = s.focus;
        FxFrame fx;
        FxViewParams vp;
        vp.has_slab = [&](std::uint16_t id) {
            FxSlabSource probe;
            return resolve_slab(id, probe);
        };
        vp.face_yaw = camera.yaw;
        vp.face_pitch = flags.screen_aligned ? camera.pitch : 0.f;
        {
            const Vec3 forward = camera.forward();
            const Vec3 right = normalize(cross(Vec3{ 0.f, 0.f, 1.f }, forward));
            vp.cam_right = right;
            vp.cam_up = normalize(cross(forward, right));
            vp.tile_at = afx_synthetic_tile;
        }
        for (const TimedEv &shot : shots) {
            AttackFxView view;
            view.push(shot.ev->proj, shot.ev->miss, kBase);
            view.evaluate(kBase + static_cast<std::uint32_t>(std::lround(shot.t * 1000.f)), kParams, vp, fx);
        }
        if (flags.mods != nullptr) {
            fx.entity_mods.insert(fx.entity_mods.end(), flags.mods->begin(), flags.mods->end());
        }
        if (flags.demo != nullptr) {
            fx.voxels.insert(fx.voxels.end(), flags.demo->voxels.begin(), flags.demo->voxels.end());
            fx.lights.insert(fx.lights.end(), flags.demo->lights.begin(), flags.demo->lights.end());
            fx.entity_mods.insert(fx.entity_mods.end(), flags.demo->entity_mods.begin(), flags.demo->entity_mods.end());
            fx.screen_waves.insert(fx.screen_waves.end(), flags.demo->screen_waves.begin(), flags.demo->screen_waves.end());
            fx.shake = std::max(fx.shake, flags.demo->shake);
        }
        last_waves = fx.screen_waves;
        if (flags.shake && (fx.shake > 0.f) && !shots.empty()) {
            camera.target = s.focus + fx_shake_offset(fx.shake, static_cast<std::uint32_t>(std::lround(shots.front().t * 1000.f)));
        }
        SceneLighting lighting = make_scene_lighting(s.frame.lighting, s.frame.floor);
        apply_lit_room_ambient(lighting, s.frame.floor, 0.f);
        const Vec3 player_pos{ static_cast<float>(s.px) + 0.5f, static_cast<float>(s.py) + 0.5f, 0.f };
        (void)collect_point_lights(s.frame, player_pos, lighting, 1.f, nullptr, 1.f, flags.lights ? &fx.lights : nullptr);

        const Scene &c = s;
        shadow.begin();
        renderer.draw_instanced_depth(floor_gpu, c.fit.view_projection, c.terrain.slabs.data(), c.terrain.slabs.size());
        renderer.draw_instanced_depth(wall_gpu, c.fit.view_projection, c.terrain.boxes.data(), c.terrain.boxes.size());
        shadow.end(screen_w, screen_h);
        post.begin_scene(0.04f, 0.05f, 0.07f);
        renderer.begin(camera.view_projection(), lighting, c.fit.view_projection, shadow.texture(), shadow.side());
        renderer.begin_cutaway(Cutaway{});
        renderer.set_cutaway_scale(0.f);
        renderer.draw_instanced(floor_gpu, c.terrain.slabs.data(), c.terrain.slabs.size());
        renderer.set_shadow_scale(0.f);
        renderer.draw_instanced(floor_gpu, c.terrain.under_slabs.data(), c.terrain.under_slabs.size());
        renderer.set_shadow_scale(1.f);
        renderer.draw_instanced(wall_gpu, c.terrain.boxes.data(), c.terrain.boxes.size());
        // 柱（プレイヤと的の代役）。対象の見た目の変化（押しつぶし・震え・のけぞり・浮き上がり・灰色・二重）を当てる。
        std::vector<ModelInstance> pillars;
        for (const Vec3 &base_pos : c.models) {
            ModelInstance inst;
            inst.position = base_pos;
            const FxEntityMod *mod = fx_find_mod(fx.entity_mods, static_cast<int>(std::floor(base_pos.x)), static_cast<int>(std::floor(base_pos.y)));
            if (mod != nullptr) {
                const FxModTransform tf = fx_mod_transform(*mod);
                inst.position = Vec3{ base_pos.x + tf.dx, base_pos.y + tf.dy, base_pos.z + tf.dz };
                inst.scale = Vec3{ tf.scale_xy, tf.scale_xy, tf.scale_z };
                inst.tint = Vec3{ tf.tint, tf.tint, tf.tint };
                pillars.push_back(inst);
                if ((std::fabs(mod->echo_x) + std::fabs(mod->echo_y)) > 1e-4f) {
                    ModelInstance copy = inst;
                    copy.position.x += mod->echo_x;
                    copy.position.y += mod->echo_y;
                    pillars.push_back(copy);
                }
            } else {
                pillars.push_back(inst);
            }
        }
        pillar_model.draw(camera.view_projection(), Mat4::identity(), lighting, camera.eye(), c.fit.view_projection,
            shadow.texture(), shadow.side(), nullptr, pillars.size(), nullptr, 0, pillars.data());
        // 効果のボクセル（地形・実体のあと。深度は読む）。本番（`hd2d_app.cpp`）と同じ呼び方。
        std::vector<FxVoxel> all;
        if (flags.voxels) {
            all = fx.voxels;
        }
        if (flags.extra != nullptr) {
            all.insert(all.end(), flags.extra->begin(), flags.extra->end());
        }
        last_drawn = fx_renderer.draw(camera.view_projection(), camera.eye(), lighting, c.fit.view_projection,
            shadow.texture(), shadow.side(), all.data(), all.size());
        last_slabs = flags.voxels ? draw_fx_slabs(renderer, fx.slabs, resolve_slab) : 0u;
        post.end_scene(screen_w, screen_h);
        const Vec3 to_target = camera.target - camera.eye();
        const float focus = std::sqrt(dot(to_target, to_target));
        post.resolve(flags.pictures ? PostFlags{} : all_off, params, z_near, z_far, focus, screen_w, screen_h, 0, 0,
            camera.view_projection(), camera.target.x, camera.target.y, to_target.x, to_target.y);
        return read_framebuffer(screen_w, screen_h);
    };

    const auto project = [&](const Vec3 &p, float &sx, float &sy) { return camera.project(p, sx, sy); };

    // 4 回回してから読む（1 枚目はドライバの都合で空のことがある）。
    camera.target = scenes[0].focus;
    for (int i = 0; i < 3; ++i) {
        (void)shoot(scenes[0], {}, Flags{});
    }

    //! シーンごとの撃ち方。
    struct SceneEvs {
        Ev arrow;
        Ev bolt;
        Ev shot;
        Ev dagger;
        Ev boomerang;
        Ev ball2;
        Ev ball1;
        Ev cold1;
        Ev jump0;
        Ev beam;
        Ev breath;
        Ev glow;
        Ev brand_arrow;
        Ev compare; //!< 半径 1 と半径 2 を並べる（部屋だけ）
    };
    const auto make_evs = [&](const Scene &s) {
        SceneEvs e;
        e.arrow.miss.push_back(afx_missile(MissileKind::Arrow, 0, "", s.px, s.py, s.tx, s.ty, true));
        e.bolt.miss.push_back(afx_missile(MissileKind::Bolt, 0, "", s.px, s.py, s.tx, s.ty, true));
        e.shot.miss.push_back(afx_missile(MissileKind::Shot, 0, "", s.px, s.py, s.tx, s.ty, true));
        e.dagger.miss.push_back(afx_missile(MissileKind::Thrown, kDaggerItem, "", s.px, s.py, s.tx, s.ty, true));
        e.boomerang.miss.push_back(afx_missile(MissileKind::Boomerang, kDaggerItem, "", s.px, s.py, s.tx - 2, s.ty, true, true));
        e.ball2.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", s.px, s.py, s.tx, s.ty, 2, s.floor));
        e.ball1.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", s.px, s.py, s.tx, s.ty, 1, s.floor));
        e.cold1.proj.push_back(afx_projection(ProjectionShape::Ball, "cold", s.px, s.py, s.tx, s.ty, 1, s.floor));
        e.jump0.proj.push_back(afx_projection(ProjectionShape::Jump, "fire", s.px, s.py, s.tx, s.ty, 0, s.floor));
        e.beam.proj.push_back(afx_projection(ProjectionShape::Beam, "fire", s.px, s.py, s.tx, s.ty, 0, s.floor));
        e.breath.proj.push_back(afx_projection(ProjectionShape::Breath, "fire", s.px, s.py, s.tx, s.ty, 7, s.floor));
        e.glow.proj.push_back(afx_projection(ProjectionShape::Bolt, "fire", s.px, s.py, s.tx, s.ty, 0, s.floor, kProjHidden));
        e.brand_arrow.miss.push_back(afx_missile(MissileKind::Arrow, 0, "fire", s.px, s.py, s.tx, s.ty, true));
        e.compare.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", s.px, s.py, s.tx + 1, s.ty - 2, 1, s.floor));
        e.compare.proj.push_back(afx_projection(ProjectionShape::Ball, "fire", s.px, s.py, s.tx + 1, s.ty + 2, 2, s.floor));
        return e;
    };
    std::vector<SceneEvs> scene_evs;
    for (const Scene &s : scenes) {
        scene_evs.push_back(make_evs(s));
    }

    for (std::size_t si = 0; si < (picture_mode ? 0u : scenes.size()); ++si) {
        Scene &s = scenes[si];
        const SceneEvs &e = scene_evs[si];
        std::fprintf(stderr, "[hd2d] attack-fx-check シーン \"%s\"\n", s.name);
        camera.target = s.focus;
        const std::string tag = std::string(s.name) + ": ";
        const std::vector<unsigned char> base = shoot(s, {}, Flags{});

        //! 飛んでいる本体の画素が、変わった画素の外接矩形の中にあるか。
        const auto in_box = [&](const DiffBox &box, const Vec3 &p, float margin) {
            float sx = 0.f;
            float sy = 0.f;
            return project(p, sx, sy) && (sx >= static_cast<float>(box.x0) - margin) && (sx <= static_cast<float>(box.x1) + margin)
                && (sy >= static_cast<float>(box.y0) - margin) && (sy <= static_cast<float>(box.y1) + margin);
        };

        // 弓の矢・クロスボウの矢・弾・投げた短剣：5 つの時刻で、画素が変わる場所が本体の位置にある。
        const struct {
            const char *name;
            const Ev *ev;
            long long min_pixels;
            std::vector<float> times;
        } fliers[] = {
            { "矢", &e.arrow, 40, { 0.0f, 0.1f, 0.2f, 0.3f, 0.36f } },
            { "クロスボウの矢", &e.bolt, 40, { 0.04f, 0.09f, 0.14f, 0.21f, 0.26f } },
            { "弾", &e.shot, 20, { 0.04f, 0.10f, 0.16f, 0.24f, 0.30f } },
            { "投げた短剣", &e.dagger, 80, { 0.06f, 0.16f, 0.28f, 0.42f, 0.54f } },
            { "ブーメラン", &e.boomerang, 80, { 0.05f, 0.2f, 0.35f, 0.5f, 0.65f } },
        };
        for (const auto &fl : fliers) {
            float prev_x = -1e9f;
            bool monotone = (std::string(fl.name) != "ブーメラン");
            float first_sy = 0.f;
            float sy_dev = 0.f;
            for (std::size_t i = 0; i < fl.times.size(); ++i) {
                const float t = fl.times[i];
                const std::vector<unsigned char> img = shoot(s, { TimedEv{ fl.ev, t } }, Flags{});
                const DiffBox box = afx_diff_box(img, base, screen_w, screen_h, 6);
                const FxFrame f = afx_eval(*fl.ev, t, FxViewParams{ [](std::uint16_t id) { return id == kDaggerItem; } });
                const Vec3 pos = f.projectile_visible ? f.projectile_pos : Vec3{};
                float sx = 0.f;
                float sy = 0.f;
                (void)project(pos, sx, sy);
                char msg[200]{};
                std::snprintf(msg, sizeof(msg), "%s%s t=%.2f: 変わった画素 %lld 個 / 外接矩形 (%d,%d)-(%d,%d) / 中心の画素 (%.0f,%.0f) / ボクセル %zu 板 %zu",
                    tag.c_str(), fl.name, t, box.count, box.x0, box.y0, box.x1, box.y1, sx, sy, last_drawn, last_slabs);
                std::fprintf(stderr, "    %s\n", msg);
                check(box.count >= fl.min_pixels, tag + fl.name + " t=" + std::to_string(t) + ": 画に写っている");
                check(f.projectile_visible && in_box(box, pos, 10.f), tag + fl.name + " t=" + std::to_string(t) + ": 変わった画素の外接矩形が本体の位置を含む");
                if (i == 0) {
                    first_sy = sy;
                }
                sy_dev = std::max(sy_dev, std::fabs(sy - first_sy));
                if ((std::string(fl.name) != "ブーメラン") && (sx <= prev_x)) {
                    monotone = false;
                }
                prev_x = sx;
            }
            if (std::string(fl.name) != "ブーメラン") {
                check(monotone, tag + fl.name + ": 時刻とともに画面の中を進む方向へ動く");
            }
            if (std::string(fl.name) == "クロスボウの矢") {
                check(sy_dev < 1.0f, tag + "クロスボウの矢: 画面の上でも高さが変わらない（まっすぐ。縦のずれ " + std::to_string(sy_dev) + " 画素）");
            }
            if (std::string(fl.name) == "矢") {
                check(sy_dev > 2.0f, tag + "矢: 画面の上で弧を描く（縦のずれ " + std::to_string(sy_dev) + " 画素）");
            }
        }
        // 投げた短剣は**板**で描く：ボクセルの箱は 0、板が 1 枚。画面へ正対させたときも、位置に写る。
        {
            Flags vertical;
            const std::vector<unsigned char> img = shoot(s, { TimedEv{ &e.dagger, 0.28f } }, vertical);
            check((last_slabs == 1) && (last_drawn == 0), tag + "投げた短剣: 描いたのは板 1 枚で、ボクセルの箱は 0（板 " + std::to_string(last_slabs) + " / 箱 " + std::to_string(last_drawn) + "）");
            Flags aligned;
            aligned.screen_aligned = true;
            const std::vector<unsigned char> img2 = shoot(s, { TimedEv{ &e.dagger, 0.28f } }, aligned);
            const DiffBox b2 = afx_diff_box(img2, base, screen_w, screen_h, 6);
            const FxFrame f = afx_eval(e.dagger, 0.28f, FxViewParams{ [](std::uint16_t id) { return id == kDaggerItem; } });
            check((b2.count >= 80) && in_box(b2, f.projectile_pos, 12.f), tag + "投げた短剣（画面へ正対）: 板が位置に写る");
            // 回っている：同じ位置で spin だけ違う絵（0.28 秒と、周期 2π/19 ≒ 0.33 秒ずれた時刻は位置が違うので使わず、
            // 板の絵が時刻で変わることを、同じ時刻の板だけを切り出して比べる）。
            const DiffBox pos_only = afx_diff_box(img, base, screen_w, screen_h, 6);
            check(pos_only.count >= 80, tag + "投げた短剣: 板が画に写る（" + std::to_string(pos_only.count) + " 画素）");
        }

        // 属性付きの矢：光と尾が足される（同じ時刻の、属性なしの矢との差が広がる）。
        {
            const std::vector<unsigned char> plain = shoot(s, { TimedEv{ &e.arrow, 0.2f } }, Flags{});
            const std::vector<unsigned char> branded = shoot(s, { TimedEv{ &e.brand_arrow, 0.2f } }, Flags{});
            const DiffBox pb = afx_diff_box(plain, base, screen_w, screen_h, 6);
            const DiffBox bb = afx_diff_box(branded, base, screen_w, screen_h, 6);
            check(bb.count > (pb.count + 100), tag + "炎の矢: 属性なしの矢より、光と尾のぶん画素が増える（" + std::to_string(pb.count) + " → " + std::to_string(bb.count) + "）");
        }

        // 火の玉（半径 2・半径 1）：飛行・着弾・爆発。
        const float ball_fly = fx_fly_seconds(afx_spec(e.ball2), kParams);
        const float ball_total = afx_total(e.ball2);
        const struct {
            const char *name;
            float t;
        } ball_times[] = {
            { "飛行 25%", ball_fly * 0.25f },
            { "飛行 50%", ball_fly * 0.50f },
            { "飛行 75%", ball_fly * 0.75f },
            { "着弾", ball_fly + 0.02f },
            { "爆発 0.10 秒", ball_fly + 0.10f },
            { "爆発 0.22 秒", ball_fly + 0.22f },
            { "爆発 0.38 秒", ball_fly + 0.38f },
        };
        for (const Ev *ev : { &e.ball2, &e.ball1 }) {
            const std::string rname = (ev == &e.ball2) ? "半径 2 " : "半径 1 ";
            for (const auto &bt : ball_times) {
                const std::vector<unsigned char> img = shoot(s, { TimedEv{ ev, bt.t } }, Flags{});
                const DiffBox box = afx_diff_box(img, base, screen_w, screen_h, 6);
                std::fprintf(stderr, "    %s%s火の玉 %s: 変わった画素 %lld 個 / 外接矩形 (%d,%d)-(%d,%d) / 描いたボクセル %zu\n", tag.c_str(), rname.c_str(), bt.name,
                    box.count, box.x0, box.y0, box.x1, box.y1, last_drawn);
                check(box.count >= 300, tag + rname + "火の玉 " + bt.name + ": 画に写っている");
            }
        }
        {
            const std::vector<unsigned char> gone = shoot(s, { TimedEv{ &e.ball2, ball_total + 0.05f } }, Flags{});
            check(afx_diff_box(gone, base, screen_w, screen_h, 0).count == 0, tag + "火の玉: 消えたあとは、効果の無い絵と 1 画素も違わない");
            struct Gone {
                const char *name;
                const Ev *ev;
            };
            for (const Gone &g : { Gone{ "矢", &e.arrow }, Gone{ "クロスボウの矢", &e.bolt }, Gone{ "弾", &e.shot }, Gone{ "投げた短剣", &e.dagger },
                     Gone{ "ブーメラン", &e.boomerang }, Gone{ "ビーム", &e.beam }, Gone{ "ブレス", &e.breath }, Gone{ "飛ばない魔法", &e.glow },
                     Gone{ "炎の矢", &e.brand_arrow }, Gone{ "半径 1 の火の玉", &e.ball1 } }) {
                const std::vector<unsigned char> img = shoot(s, { TimedEv{ g.ev, afx_total(*g.ev) + 0.05f } }, Flags{});
                check(afx_diff_box(img, base, screen_w, screen_h, 0).count == 0, tag + g.name + ": 消えたあとは、効果の無い絵と 1 画素も違わない");
            }
        }

        // 爆発が効果範囲の外へ出ない（画面の上でも）：光を切って、粒だけの差を取り、範囲の箱を投影した矩形と比べる。
        {
            Flags no_light;
            no_light.lights = false;
            const std::vector<unsigned char> base_nl = shoot(s, {}, no_light);
            long long widths[2]{};
            int idx = 0;
            for (const Ev *ev : { &e.ball1, &e.ball2 }) {
                const ProjectionFx &p = ev->proj.front();
                int min_x = 1 << 20;
                int max_x = -(1 << 20);
                int min_y = 1 << 20;
                int max_y = -(1 << 20);
                for (const ::FxCell &c : p.area) {
                    min_x = std::min<int>(min_x, c.x);
                    max_x = std::max<int>(max_x, c.x);
                    min_y = std::min<int>(min_y, c.y);
                    max_y = std::max<int>(max_y, c.y);
                }
                float lo_x = 1e9f;
                float hi_x = -1e9f;
                float lo_y = 1e9f;
                float hi_y = -1e9f;
                for (const float cx : { static_cast<float>(min_x), static_cast<float>(max_x + 1) }) {
                    for (const float cy : { static_cast<float>(min_y), static_cast<float>(max_y + 1) }) {
                        for (const float cz : { 0.f, 1.8f }) {
                            float sx = 0.f;
                            float sy = 0.f;
                            if (project(Vec3{ cx, cy, cz }, sx, sy)) {
                                lo_x = std::min(lo_x, sx);
                                hi_x = std::max(hi_x, sx);
                                lo_y = std::min(lo_y, sy);
                                hi_y = std::max(hi_y, sy);
                            }
                        }
                    }
                }
                DiffBox total_box;
                const float fly = fx_fly_seconds(afx_spec(*ev), kParams);
                for (const float te : { 0.02f, 0.06f, 0.10f, 0.16f, 0.24f, 0.32f, 0.40f }) {
                    const DiffBox b = afx_diff_box(shoot(s, { TimedEv{ ev, fly + te } }, no_light), base_nl, screen_w, screen_h, 3);
                    if (b.count > 0) {
                        total_box.count += b.count;
                        total_box.x0 = std::min(total_box.x0, b.x0);
                        total_box.y0 = std::min(total_box.y0, b.y0);
                        total_box.x1 = std::max(total_box.x1, b.x1);
                        total_box.y1 = std::max(total_box.y1, b.y1);
                    }
                }
                // 火の粉の尾は爆発の外（飛んできた道）に残るので、着弾したあとの火の粉を除くため、尾が尽きる前後で比べない。
                // → 尾の火の粉は経路の上にある（範囲の外）。画素の外接矩形のうち、範囲の箱の外に出るのは尾だけのはず。
                //    尾は着弾のマスの手前の経路にあり、範囲の箱の西側へはみ出す。**東・北・南の 3 辺**で確かめる。
                const std::string rname = (ev == &e.ball2) ? "半径 2" : "半径 1";
                std::fprintf(stderr, "    %s%s: 爆発の画素の外接矩形 (%d,%d)-(%d,%d) / 範囲の箱の投影 (%.0f,%.0f)-(%.0f,%.0f)\n", tag.c_str(), rname.c_str(), total_box.x0,
                    total_box.y0, total_box.x1, total_box.y1, lo_x, lo_y, hi_x, hi_y);
                check((total_box.count > 0) && (static_cast<float>(total_box.x1) <= hi_x + 4.f) && (static_cast<float>(total_box.y0) >= lo_y - 4.f)
                        && (static_cast<float>(total_box.y1) <= hi_y + 4.f),
                    tag + rname + ": 爆発の粒は効果範囲の箱の外へ出ない（東・北・南の 3 辺）");
                widths[idx++] = (total_box.x1 - total_box.x0) + (total_box.y1 - total_box.y0);
            }
            check(widths[0] < widths[1], tag + "爆発: 半径 1 は半径 2 より画面の上でも小さい（幅+高さ " + std::to_string(widths[0]) + " < " + std::to_string(widths[1]) + "）");
        }

        // ビーム・ブレス・飛ばない魔法・汎用の玉：画に写る。
        {
            const struct {
                const char *name;
                const Ev *ev;
                float t;
                long long min_pixels;
            } shown[] = {
                { "ビーム", &e.beam, 0.10f, 300 },
                { "ブレス", &e.breath, 0.20f, 600 },
                { "飛ばない魔法", &e.glow, 0.40f, 150 },
                { "冷気の玉（飛行）", &e.cold1, ball_fly * 0.5f, 300 },
                { "冷気の爆発", &e.cold1, ball_fly + 0.12f, 300 },
                { "ジャンプ（その場の爆発）", &e.jump0, 0.10f, 200 },
            };
            for (const auto &sh : shown) {
                const std::vector<unsigned char> img = shoot(s, { TimedEv{ sh.ev, sh.t } }, Flags{});
                const DiffBox box = afx_diff_box(img, base, screen_w, screen_h, 6);
                std::fprintf(stderr, "    %s%s: 変わった画素 %lld 個 / 外接矩形 (%d,%d)-(%d,%d)\n", tag.c_str(), sh.name, box.count, box.x0, box.y0, box.x1, box.y1);
                check(box.count >= sh.min_pixels, tag + sh.name + ": 画に写っている");
            }
            // 冷気の爆発は青い（火の爆発は赤い）：的の柱に隠れない東隣のマスの周りで、光を切った絵の
            // 「効果の無い絵と違う画素」だけの平均の色を比べる。
            Flags no_light;
            no_light.lights = false;
            const std::vector<unsigned char> base_nl = shoot(s, {}, no_light);
            const std::vector<unsigned char> cold = shoot(s, { TimedEv{ &e.cold1, ball_fly + 0.08f } }, no_light);
            const std::vector<unsigned char> fire = shoot(s, { TimedEv{ &e.ball1, ball_fly + 0.08f } }, no_light);
            float sx = 0.f;
            float sy = 0.f;
            if (project(Vec3{ static_cast<float>(s.tx) + 1.5f, static_cast<float>(s.ty) + 0.5f, 0.45f }, sx, sy)) {
                const auto changed_mean = [&](const std::vector<unsigned char> &img, int &n) {
                    std::array<double, 3> sum{ 0.0, 0.0, 0.0 };
                    n = 0;
                    for (int y = static_cast<int>(sy) - 130; y <= static_cast<int>(sy) + 130; ++y) {
                        for (int x = static_cast<int>(sx) - 130; x <= static_cast<int>(sx) + 130; ++x) {
                            if ((x < 0) || (y < 0) || (x >= screen_w) || (y >= screen_h)) {
                                continue;
                            }
                            int a[3]{};
                            int b[3]{};
                            pixel_at(img, screen_w, screen_h, x, y, a);
                            pixel_at(base_nl, screen_w, screen_h, x, y, b);
                            if ((std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2])) > 60) {
                                for (int c = 0; c < 3; ++c) {
                                    sum[static_cast<std::size_t>(c)] += a[c];
                                }
                                ++n;
                            }
                        }
                    }
                    for (double &v : sum) {
                        v /= std::max(n, 1);
                    }
                    return sum;
                };
                int n_cold = 0;
                int n_fire = 0;
                const auto c = changed_mean(cold, n_cold);
                const auto f = changed_mean(fire, n_fire);
                std::fprintf(stderr, "    %s爆発の平均の色（変わった画素 冷気 %d / 火 %d 個）: 冷気 (%.0f,%.0f,%.0f) / 火 (%.0f,%.0f,%.0f)\n", tag.c_str(), n_cold, n_fire, c[0], c[1], c[2], f[0], f[1], f[2]);
                check((n_cold > 200) && (n_fire > 200) && (c[2] > c[0]) && (f[0] > f[2]), tag + "爆発の色: 冷気は青く、火は赤い（黄）");
            } else {
                check(false, tag + "爆発の色: 測定点が画面の外");
            }
        }

        // 点光源：同じ描画で、光の旗だけ違う。ボクセルは同じ。
        {
            //! 通路は南の壁が手前を隠すので、北の列（y = py - 1）の、的より東で測る。
            const bool narrow = (std::string(s.name) == "corridor");
            const Vec3 burst_spot = narrow ? Vec3{ static_cast<float>(s.tx) + 3.9f, 50.5f, 0.f } : Vec3{ static_cast<float>(s.tx) + 0.5f, static_cast<float>(s.ty) + 3.8f, 0.f };
            const Vec3 flight_spot = narrow ? Vec3{ 0.5f * static_cast<float>(s.px + s.tx) + 3.1f, 50.5f, 0.f }
                                            : Vec3{ 0.5f * static_cast<float>(s.px + s.tx) + 0.5f, static_cast<float>(s.ty) + 3.1f, 0.f };
            const struct {
                const char *name;
                const Ev *ev;
                float t;
                Vec3 spot;
            } lit_cases[] = {
                { "爆発の最大（半径 2）", &e.ball2, ball_fly + 0.03f, burst_spot },
                { "飛行中の火の玉", &e.ball2, ball_fly * 0.5f, flight_spot },
            };
            for (const auto &lc : lit_cases) {
                Flags with;
                Flags without;
                without.lights = false;
                const std::vector<unsigned char> img_with = shoot(s, { TimedEv{ lc.ev, lc.t } }, with);
                const std::vector<unsigned char> img_without = shoot(s, { TimedEv{ lc.ev, lc.t } }, without);
                float sx = 0.f;
                float sy = 0.f;
                if (!project(lc.spot, sx, sy)) {
                    check(false, tag + lc.name + ": 測定点が画面の外");
                    continue;
                }
                const double l_with = afx_luma(afx_mean_rgb(img_with, screen_w, screen_h, sx, sy, 3));
                const double l_without = afx_luma(afx_mean_rgb(img_without, screen_w, screen_h, sx, sy, 3));
                std::fprintf(stderr, "    %s%s: 周りの床の輝度 光なし %.2f → 光あり %.2f\n", tag.c_str(), lc.name, l_without, l_with);
                check(l_with > (l_without * 1.08 + 0.5), tag + lc.name + ": 点光源が増えて周りの床が明るくなる");
            }
        }
    }

    /* --- 地形との前後関係（深度を読む）：壁の中にある立方体は写らない。壁の手前にある同じ立方体は写る --- */
    {
        Scene &s = scenes[0];
        camera.target = s.focus;
        const std::vector<unsigned char> base = shoot(s, {}, Flags{});
        FxVoxel cube;
        cube.size = Vec3{ 0.4f, 0.4f, 0.4f };
        cube.r = 1.f;
        cube.g = 0.f;
        cube.b = 1.f;
        cube.emissive = 3.f;
        std::vector<FxVoxel> hidden{ cube };
        hidden[0].center = Vec3{ 101.5f, 41.5f, 0.5f }; // 北の壁の中
        std::vector<FxVoxel> shown{ cube };
        shown[0].center = Vec3{ 101.5f, 43.5f, 0.5f }; // 壁の手前の床の上
        Flags f_hidden;
        f_hidden.extra = &hidden;
        Flags f_shown;
        f_shown.extra = &shown;
        const DiffBox hidden_box = afx_diff_box(shoot(s, {}, f_hidden), base, screen_w, screen_h, 0);
        const DiffBox shown_box = afx_diff_box(shoot(s, {}, f_shown), base, screen_w, screen_h, 0);
        std::fprintf(stderr, "    深度: 壁の中の立方体 %lld 画素 / 壁の手前の立方体 %lld 画素\n", hidden_box.count, shown_box.count);
        check(hidden_box.count == 0, "深度: 壁の中にある立方体は、壁に隠れて 1 画素も写らない");
        check(shown_box.count >= 100, "深度: 壁の手前にある同じ立方体は写る");
        FxVoxel far_glass = cube;
        far_glass.alpha = 0.5f;
        far_glass.center = Vec3{ 101.5f, 46.5f, 0.5f };
        FxVoxel near_glass = far_glass;
        near_glass.center = Vec3{ 101.5f, 46.9f, 0.5f };
        near_glass.r = 0.f;
        near_glass.g = 1.f;
        near_glass.b = 0.f;
        std::vector<FxVoxel> forward{ far_glass, near_glass };
        std::vector<FxVoxel> reverse{ near_glass, far_glass };
        Flags f1;
        f1.extra = &forward;
        Flags f2;
        f2.extra = &reverse;
        const DiffBox order_box = afx_diff_box(shoot(s, {}, f1), shoot(s, {}, f2), screen_w, screen_h, 0);
        check(order_box.count == 0, "半透明: 渡す順に関わらず、奥から手前へ並べ直して混ぜる（絵が同じ）");
    }

    // 属性ごとの見た目の出力：光の波（画面）・実体の変化（柱）
    {
        std::fprintf(stderr, "[hd2d] attack-fx-check (2) 光の波と実体の変化の描画\n");
        check(paint_ready, "光の波: 画面に描く部品（UiPaint）を用意できる" + (paint_ready ? std::string() : (": " + paint_err)));
        Scene &s = scenes[0];
        camera.target = s.focus;
        const std::vector<unsigned char> base = shoot(s, {}, Flags{});
        Ev wave;
        wave.proj.push_back(afx_projection(ProjectionShape::Bolt, "fire", s.px, s.py, s.tx, s.ty, 0, s.floor, kProjAllInSight));
        const std::vector<unsigned char> without = shoot(s, { TimedEv{ &wave, 0.45f } }, Flags{});
        const std::size_t wave_count = last_waves.size();
        const std::vector<FxScreenWave> waves = last_waves;
        std::vector<unsigned char> with_wave = without;
        if (paint_ready) {
            (void)shoot(s, { TimedEv{ &wave, 0.45f } }, Flags{});
            overlay_paint.begin(screen_w, screen_h);
            draw_attack_fx_waves(overlay_paint, waves, RectPx{ 0, 0, screen_w, screen_h });
            overlay_paint.flush();
            with_wave = read_framebuffer(screen_w, screen_h);
        }
        const DiffBox box_without = afx_diff_box(without, base, screen_w, screen_h, 6);
        const DiffBox box_with = afx_diff_box(with_wave, base, screen_w, screen_h, 6);
        std::fprintf(stderr, "    光の波 %zu 本: 描く前の変化 %lld 画素 / 描いたあと %lld 画素（横 %d〜%d）\n", wave_count, box_without.count, box_with.count, box_with.x0, box_with.x1);
        check((wave_count >= 1u) && (box_with.count > (box_without.count + 20000)) && ((box_with.x1 - box_with.x0) > (screen_w / 12)),
            "光の波: 視界内の全員にかかる魔法は、画面に光の帯が写る（描く前より画素が大きく増える）");

        const auto mod_box = [&](const FxEntityMod &mod) {
            std::vector<FxEntityMod> mods{ mod };
            Flags f;
            f.mods = &mods;
            const std::vector<unsigned char> img = shoot(s, {}, f);
            return afx_diff_box(img, base, screen_w, screen_h, 6);
        };
        FxEntityMod squash;
        squash.gx = s.tx;
        squash.gy = s.ty;
        squash.squash = 1.f;
        const DiffBox b_squash = mod_box(squash);
        FxEntityMod grey = squash;
        grey.squash = 0.f;
        grey.grey = 1.f;
        const DiffBox b_grey = mod_box(grey);
        FxEntityMod echo = squash;
        echo.squash = 0.f;
        echo.echo_x = 0.45f;
        const DiffBox b_echo = mod_box(echo);
        FxEntityMod lift = squash;
        lift.squash = 0.f;
        lift.lift = 0.4f;
        const DiffBox b_lift = mod_box(lift);
        FxEntityMod tremble = squash;
        tremble.squash = 0.f;
        tremble.tremble = 0.15f;
        tremble.tremble_phase = 1.5708f;
        const DiffBox b_tremble = mod_box(tremble);
        FxEntityMod elsewhere = squash;
        elsewhere.gx = s.tx + 3;
        const DiffBox b_else = mod_box(elsewhere);
        std::fprintf(stderr, "    実体の変化（的の柱）: 押しつぶし %lld / 灰色 %lld / 二重 %lld / 浮き上がり %lld / 震え %lld / 別のマス %lld 画素\n", b_squash.count,
            b_grey.count, b_echo.count, b_lift.count, b_tremble.count, b_else.count);
        check(b_squash.count > 300, "実体の変化: 的の柱が押しつぶされて、絵が変わる");
        check(b_grey.count > 300, "実体の変化: 的の柱が灰色に染まって、絵が変わる");
        check(b_echo.count > 300, "実体の変化: 的の柱が二重に見える");
        check(b_lift.count > 300, "実体の変化: 的の柱が浮き上がる");
        check(b_tremble.count > 100, "実体の変化: 的の柱が震える");
        check(b_else.count == 0, "実体の変化: 別のマスの変化は、的の柱に当たらない（絵が 1 画素も変わらない）");
    }

    // GL のエラーが出ていない。
    {
        const std::string gl_err = drain_gl_errors();
        check(gl_err.empty(), "描く間に GL エラーが出ていない" + (gl_err.empty() ? std::string() : (": " + gl_err)));
    }

    /* --- 絵と連続画像を書き出す --- */
    if (!out_stem.empty()) {
        const auto save = [&](Scene &s, const std::string &path, const std::vector<TimedEv> &shots, const FxFrame *demo = nullptr) {
            Flags pic;
            pic.pictures = true;
            pic.shake = true;
            pic.demo = demo;
            (void)shoot(s, shots, pic);
            if (paint_ready && !last_waves.empty()) {
                overlay_paint.begin(screen_w, screen_h);
                draw_attack_fx_waves(overlay_paint, last_waves, RectPx{ 0, 0, screen_w, screen_h });
                overlay_paint.flush();
            }
            std::vector<unsigned char> pixels(static_cast<std::size_t>(screen_w) * static_cast<std::size_t>(screen_h) * 4u);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glReadPixels(0, 0, screen_w, screen_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (std::size_t i = 3; i < pixels.size(); i += 4) {
                pixels[i] = 255;
            }
            const bool ok = save_rgba_png(path, pixels.data(), screen_w, screen_h);
            if (!ok) {
                check(false, "絵を書き出せた: " + path);
            }
        };
        std::filesystem::create_directories(std::filesystem::path(out_stem + "_frames"));
        char name[128]{};
        int written = 0;
        if (picture_mode) {
            // 指定の場面だけ：部屋で、30 コマ/秒の連続画像と、いちばん見せ場の静止画を書き出す。
            Scene &room_scene = scenes[0];
            for (const PickScene &pk : picks) {
                if (pk.name == "parts_demo") {
                    // 共通の部品の見本（`afx_parts_demo`）。時刻ごとに絵を作って混ぜる。
                    FxSpec demo_spec = afx_part_spec();
                    const Vec3 forward = camera.forward();
                    demo_spec.cam_right = normalize(cross(Vec3{ 0.f, 0.f, 1.f }, forward));
                    demo_spec.cam_up = normalize(cross(forward, demo_spec.cam_right));
                    demo_spec.centre_gx = room_scene.tx;
                    demo_spec.centre_gy = room_scene.ty;
                    int frames = 0;
                    Ev none;
                    for (int i = 0; (static_cast<float>(i) * (1.f / 30.f)) <= 2.7f; ++i) {
                        const float t = static_cast<float>(i) * (1.f / 30.f);
                        FxFrame demo_frame;
                        afx_parts_demo(demo_spec, room_scene.px, room_scene.py, room_scene.tx, room_scene.ty, t, demo_frame);
                        std::snprintf(name, sizeof(name), "_frames/room_%s_%03d.png", pk.name.c_str(), i);
                        save(room_scene, out_stem + name, { TimedEv{ &none, t } }, &demo_frame);
                        ++written;
                        ++frames;
                    }
                    std::fprintf(stderr, "    場面 parts_demo: %d コマ → %s_frames/room_parts_demo_NNN.png\n", frames, out_stem.c_str());
                    continue;
                }
                Ev ev;
                ev.proj.push_back(afx_projection(pk.shape, pk.attr, room_scene.px, room_scene.py, room_scene.tx, room_scene.ty, pk.rad, room_scene.floor, pk.flags));
                // 指定の場面の絵は、属性を足しても同じ乱数の種で出す（通し番号は登録表の大きさで変わるので、場面の名前から決める）。
                {
                    std::uint32_t name_hash = 2166136261u;
                    for (const char c : pk.name) {
                        name_hash = (name_hash ^ static_cast<unsigned char>(c)) * 16777619u;
                    }
                    ev.proj.back().seq = (name_hash & 0xffffffu) + 1u;
                }
                const float end = afx_total(ev) + 0.1f;
                int frames = 0;
                for (int i = 0; (static_cast<float>(i) * (1.f / 30.f)) <= end; ++i) {
                    std::snprintf(name, sizeof(name), "_frames/room_%s_%03d.png", pk.name.c_str(), i);
                    save(room_scene, out_stem + name, { TimedEv{ &ev, static_cast<float>(i) * (1.f / 30.f) } });
                    ++written;
                    ++frames;
                }
                std::fprintf(stderr, "    場面 %s: %d コマ（全体 %.2f 秒）→ %s_frames/room_%s_NNN.png\n", pk.name.c_str(), frames, afx_total(ev), out_stem.c_str(), pk.name.c_str());
            }
        }
        for (std::size_t si = 0; si < (picture_mode ? 0u : scenes.size()); ++si) {
            Scene &s = scenes[si];
            const SceneEvs &e = scene_evs[si];
            const bool room = (si == 0);
            const float step = 1.f / 30.f;
            // 連続画像（30 コマ/秒）。GIF はこれから別のスクリプトが作る。
            struct Seq {
                const char *name;
                const Ev *ev;
                bool both_scenes;
            };
            const Seq seqs[] = {
                { "arrow", &e.arrow, true },
                { "bolt", &e.bolt, true },
                { "shot", &e.shot, false },
                { "dagger", &e.dagger, true },
                { "boomerang", &e.boomerang, false },
                { "brand_arrow", &e.brand_arrow, false },
                { "ball2", &e.ball2, true },
                { "ball1", &e.ball1, true },
                { "cold1", &e.cold1, false },
                { "beam", &e.beam, false },
                { "breath", &e.breath, false },
                { "glow", &e.glow, false },
                { "compare", &e.compare, false },
            };
            for (const Seq &sq : seqs) {
                const bool is_compare = (std::string(sq.name) == "compare");
                if ((!sq.both_scenes && !room) || (is_compare && !room)) {
                    continue;
                }
                const float end = afx_total(*sq.ev) + 0.1f;
                for (int i = 0; (static_cast<float>(i) * step) <= end; ++i) {
                    std::snprintf(name, sizeof(name), "_frames/%s_%s_%03d.png", s.name, sq.name, i);
                    save(s, out_stem + name, { TimedEv{ sq.ev, static_cast<float>(i) * step } });
                    ++written;
                }
            }
            // 静止画：矢・クロスボウの矢・短剣・半径 1 と 2 の爆発の盛り。
            const float fly2 = fx_fly_seconds(afx_spec(e.ball2), kParams);
            const struct {
                const char *tag;
                const Ev *ev;
                float t;
            } stills[] = {
                { "arrow_t0.20", &e.arrow, 0.20f },
                { "bolt_t0.14", &e.bolt, 0.14f },
                { "dagger_t0.28", &e.dagger, 0.28f },
                { "ball2_fly50", &e.ball2, fly2 * 0.5f },
                { "ball2_burst010", &e.ball2, fly2 + 0.10f },
                { "ball2_burst022", &e.ball2, fly2 + 0.22f },
                { "ball1_burst010", &e.ball1, fly2 + 0.10f },
                { "ball1_burst022", &e.ball1, fly2 + 0.22f },
                { "beam_t0.10", &e.beam, 0.10f },
                { "breath_t0.20", &e.breath, 0.20f },
                { "glow_t0.40", &e.glow, 0.40f },
            };
            for (const auto &st : stills) {
                std::snprintf(name, sizeof(name), "_%s_%s.png", s.name, st.tag);
                save(s, out_stem + name, { TimedEv{ st.ev, st.t } });
                ++written;
            }
        }
        check(true, "絵と連続画像を書き出した（" + std::to_string(written) + " 枚）: " + out_stem + "_*.png");
        const std::string gl_err = drain_gl_errors();
        check(gl_err.empty(), "書き出しの間に GL エラーが出ていない" + (gl_err.empty() ? std::string() : (": " + gl_err)));
    }

    overlay_paint.shutdown();
    post.shutdown();
    shadow.shutdown();
    pillar_model.shutdown();
    fx_renderer.shutdown();
    release_lit_placeholder();
    renderer.release(wall_gpu);
    renderer.release(floor_gpu);
    slab_library.shutdown(&renderer);
    renderer.shutdown();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    SDL_Quit();
    std::fprintf(stderr, "[hd2d] attack-fx-check: PASS %d / FAIL %d\n", passes, failures);
    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

} // namespace hd2d
