/*!
 * @file attack_fx_view.cpp
 * @brief `attack_fx_view.h` の中身。フレームの 1 件を `FxSpec` にして、時計に合わせて再生する。
 */
#include "render/attack_fx_view.h"
#include "app/ui_probe.h"

#include "render/fx_styles/fx_style.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

namespace {

//! 撃つ側の手の高さ・相手の胸の高さ・クロスボウの矢とビームの一定の高さ（マス）。
constexpr float kHandZ = 0.62f;
constexpr float kChestZ = 0.52f;
constexpr float kBoltZ = 0.57f;
constexpr float kBeamZ = 0.50f;

Vec3 cell_centre(int gx, int gy, float z)
{
    return Vec3{ static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, z };
}

std::uint32_t seed_of(std::uint32_t seq)
{
    std::uint32_t h = (seq * 0x9e3779b1u) + 0x1234567u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h | 1u;
}

//! 経路の点を作る：始点・経路のマスの中心・着弾点。高さは道のりに比例して `z_start` から `z_end` へ。
std::vector<Vec3> route_points(int src_x, int src_y, const std::vector<::FxCell> &path, int end_x, int end_y, float z_start,
    float z_end)
{
    std::vector<Vec3> pts;
    pts.push_back(cell_centre(src_x, src_y, z_start));
    for (const ::FxCell &cell : path) {
        pts.push_back(cell_centre(cell.x, cell.y, z_start));
    }
    const Vec3 &last = pts.back();
    const bool at_end = (static_cast<int>(std::floor(last.x)) == end_x) && (static_cast<int>(std::floor(last.y)) == end_y);
    if (!at_end) {
        pts.push_back(cell_centre(end_x, end_y, z_start));
    }
    // 高さ：道のりに比例して変える。
    std::vector<float> cum(pts.size(), 0.f);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const Vec3 d = pts[i] - pts[i - 1];
        cum[i] = cum[i - 1] + std::sqrt(dot(d, d));
    }
    const float total = cum.back();
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const float k = (total > 1e-5f) ? (cum[i] / total) : 1.f;
        pts[i].z = z_start + ((z_end - z_start) * k);
    }
    return pts;
}

bool recorded(const std::vector<std::pair<std::uint32_t, std::uint32_t>> &list, std::uint32_t seq)
{
    for (const auto &entry : list) {
        if (entry.first == seq) {
            return true;
        }
    }
    return false;
}

void prune(std::vector<std::pair<std::uint32_t, std::uint32_t>> &list, std::uint32_t now_ms)
{
    list.erase(std::remove_if(list.begin(), list.end(),
                   [now_ms](const auto &entry) {
                       const auto age = static_cast<std::int32_t>(now_ms - entry.second);
                       return (age < 0) || (age > static_cast<std::int32_t>(AttackFxView::kSeenMs));
                   }),
        list.end());
}

} // namespace

bool AttackFxView::make_spec(const ProjectionFx &p, FxSpec &out)
{
    // 盲目のときは原作も描かない（見えないものは描かない）。
    if ((p.flags & kProjPlayerBlind) != 0u) {
        return false;
    }
    FxSpec spec;
    spec.seed = seed_of(p.seq);
    spec.palette = fx_palette_for(p.attr);
    spec.attr = p.attr;
    spec.style = fx_find_style(p.attr);
    spec.centre_gx = p.impact_x;
    spec.centre_gy = p.impact_y;
    spec.src_gx = p.src_x;
    spec.src_gy = p.src_y;

    // 飛ばない魔法（原作が描かない投射・視界内の全員にかかる魔法・足元だけに効く魔法）：対象の上の小さな光。
    if (((p.flags & (kProjHidden | kProjAllInSight)) != 0u) || (p.shape == ProjectionShape::Touch)) {
        spec.kind = FxKind::Glow;
        spec.glow_mode = ((p.flags & kProjAllInSight) != 0u) ? FxGlowMode::AllInSight
            : ((p.shape == ProjectionShape::Touch) ? FxGlowMode::Touch : FxGlowMode::Target);
        out = std::move(spec);
        return true;
    }

    const auto area_cells = [&p](bool from_source) {
        std::vector<FxBurstCell> cells;
        cells.reserve(p.area.size());
        for (const ::FxCell &cell : p.area) {
            FxBurstCell c;
            c.gx = cell.x;
            c.gy = cell.y;
            // ブレスは撃った者からの距離（近いマスから順に広がる）、他は中心からの距離（原作の `dist`）。
            c.ring = from_source ? fx_grid_distance(cell.x - p.src_x, cell.y - p.src_y) : static_cast<int>(cell.dist);
            cells.push_back(c);
        }
        return cells;
    };
    const auto single_cell = [&p]() {
        return std::vector<FxBurstCell>{ FxBurstCell{ p.impact_x, p.impact_y, 0 } };
    };

    switch (p.shape) {
    case ProjectionShape::Bolt:
    case ProjectionShape::Ball: {
        spec.kind = FxKind::Orb;
        spec.explode = (p.shape == ProjectionShape::Ball);
        fx_set_route(spec, route_points(p.src_x, p.src_y, p.path, p.impact_x, p.impact_y, kHandZ, kChestZ));
        spec.cells = spec.explode ? area_cells(false) : single_cell();
        if (spec.cells.empty()) {
            spec.cells = single_cell();
        }
        break;
    }
    case ProjectionShape::Beam: {
        spec.kind = FxKind::Beam;
        fx_set_route(spec, route_points(p.src_x, p.src_y, p.path, p.impact_x, p.impact_y, kBeamZ, kBeamZ));
        // 終点で小さく弾ける。経路は `route` が持つので、`cells` は終点のマスだけ。
        spec.cells = single_cell();
        break;
    }
    case ProjectionShape::Breath: {
        spec.kind = FxKind::Breath;
        spec.centre_gx = p.src_x;
        spec.centre_gy = p.src_y;
        spec.cells = area_cells(true);
        if (spec.cells.empty()) {
            spec.cells = single_cell();
        }
        break;
    }
    case ProjectionShape::Jump:
    default: {
        spec.kind = FxKind::Burst;
        spec.cells = area_cells(false);
        if (spec.cells.empty()) {
            spec.cells = single_cell();
        }
        break;
    }
    }
    out = std::move(spec);
    return true;
}

bool AttackFxView::make_spec(const MissileFx &m, FxSpec &out)
{
    if (m.path.empty()) {
        return false; // 飛んだ先が無い（描く経路が無い）
    }
    FxSpec spec;
    spec.seed = seed_of(m.seq);
    spec.hit = m.hit;
    spec.item_id = m.item_id;
    switch (m.kind) {
    case MissileKind::Arrow:
        spec.kind = FxKind::Arrow;
        break;
    case MissileKind::Bolt:
        spec.kind = FxKind::Bolt;
        break;
    case MissileKind::Shot:
        spec.kind = FxKind::Shot;
        break;
    case MissileKind::Thrown:
        spec.kind = FxKind::Thrown;
        break;
    case MissileKind::Boomerang:
        spec.kind = FxKind::Boomerang;
        break;
    }
    if (!m.brand.empty()) {
        spec.has_brand = true;
        spec.brand = fx_palette_for(m.brand);
    }
    const int end_x = m.path.back().x;
    const int end_y = m.path.back().y;
    // クロスボウの矢は**まっすぐ**（高さが一定）。他は手の高さから胸の高さへ。
    const float z_start = (spec.kind == FxKind::Bolt) ? kBoltZ : kHandZ;
    const float z_end = (spec.kind == FxKind::Bolt) ? kBoltZ : kChestZ;
    const std::vector<Vec3> outbound = route_points(m.src_x, m.src_y, m.path, end_x, end_y, z_start, z_end);
    std::vector<Vec3> back;
    if (spec.kind == FxKind::Boomerang) {
        // 戻りの経路を戻ってくる。無ければ来た道を逆にたどる。**最後は必ず撃った位置へ戻す**。
        if (!m.return_path.empty()) {
            for (const ::FxCell &cell : m.return_path) {
                back.push_back(cell_centre(cell.x, cell.y, z_end));
            }
        } else {
            for (std::size_t i = outbound.size() - 1; i > 0; --i) {
                back.push_back(outbound[i - 1]);
            }
        }
        const Vec3 home = cell_centre(m.src_x, m.src_y, z_start);
        const Vec3 d = back.empty() ? Vec3{ 1.f, 0.f, 0.f } : (back.back() - home);
        if (back.empty() || (dot(d, d) > 1e-6f)) {
            back.push_back(home);
        }
    }
    fx_set_route(spec, outbound, back);
    out = std::move(spec);
    return true;
}

void AttackFxView::push(const std::vector<ProjectionFx> &projections, const std::vector<MissileFx> &missiles, std::uint32_t now_ms)
{
    prune(this->seen_projections_, now_ms);
    prune(this->seen_missiles_, now_ms);
    const auto add = [this, now_ms](FxSpec &&spec) {
        if (this->actives_.size() >= kMaxActive) {
            this->actives_.erase(this->actives_.begin());
        }
        Active active;
        active.spec = std::move(spec);
        active.start_ms = now_ms;
        this->actives_.push_back(std::move(active));
    };
    for (const ProjectionFx &p : projections) {
        if ((p.seq != 0) && recorded(this->seen_projections_, p.seq)) {
            continue;
        }
        this->seen_projections_.emplace_back(p.seq, now_ms);
        FxSpec spec;
        if (make_spec(p, spec)) {
            {
                static const char *const shapes[] = { "bolt", "beam", "ball", "breath", "jump", "touch" };
                const auto shape_index = static_cast<std::size_t>(p.shape);
                ui_probe().fx_started(p.attr, (shape_index < 6) ? shapes[shape_index] : "unknown", "projection"); //!< 検査用の記録（無効なら何もしない）
            }
            add(std::move(spec));
        }
    }
    for (const MissileFx &m : missiles) {
        if ((m.seq != 0) && recorded(this->seen_missiles_, m.seq)) {
            continue;
        }
        this->seen_missiles_.emplace_back(m.seq, now_ms);
        FxSpec spec;
        if (make_spec(m, spec)) {
            {
                static const char *const kinds[] = { "arrow", "bolt", "shot", "thrown", "boomerang" };
                const auto kind_index = static_cast<std::size_t>(m.kind);
                ui_probe().fx_started(m.brand, (kind_index < 5) ? kinds[kind_index] : "unknown", "missile");
            }
            add(std::move(spec));
        }
    }
}

void AttackFxView::evaluate(std::uint32_t now_ms, const VoxelFxParams &params, const FxViewParams &view, FxFrame &out)
{
    std::vector<Active> keep;
    keep.reserve(this->actives_.size());
    const std::size_t light_start = out.lights.size();
    for (Active &active : this->actives_) {
        // 単調時計の巻き戻りに耐える（差を符号付きで見る）。
        const auto diff = static_cast<std::int32_t>(now_ms - active.start_ms);
        if (diff < 0) {
            continue;
        }
        const float t = static_cast<float>(diff) / 1000.f;
        if (t >= fx_total_seconds(active.spec, params)) {
            continue;
        }
        if (((active.spec.kind == FxKind::Thrown) || (active.spec.kind == FxKind::Boomerang)) && !active.slab_decided) {
            active.spec.use_slab = view.has_slab ? view.has_slab(active.spec.item_id) : false;
            active.slab_decided = true;
        }
        //! 板の向きはカメラとともに変わる（視点を回しても面が正対する）。
        active.spec.face_yaw = view.face_yaw;
        active.spec.face_pitch = view.face_pitch;
        active.spec.cam_right = view.cam_right;
        active.spec.cam_up = view.cam_up;
        active.spec.tile_at = view.tile_at;
        const std::size_t slab_start = out.slabs.size();
        fx_evaluate(active.spec, params, t, out);
        if (view.face_toward) {
            // 一人称：板は本人のほうを向く（`entity_view.cpp` の `facing_yaw` と同じ式）。
            for (std::size_t i = slab_start; i < out.slabs.size(); ++i) {
                const float dx = view.toward_x - out.slabs[i].centre.x;
                const float dy = view.toward_y - out.slabs[i].centre.y;
                if (((dx * dx) + (dy * dy)) > 1e-8f) {
                    out.slabs[i].face_yaw = std::atan2(-dx, dy);
                    out.slabs[i].face_pitch = 0.f;
                }
            }
        }
        keep.push_back(std::move(active));
    }
    this->actives_ = std::move(keep);
    out.finished = this->actives_.empty();

    // 点光源は枠が少ない（`kMaxExtraPointLights`）。強い順に残す。
    const std::size_t added = out.lights.size() - light_start;
    if (added > static_cast<std::size_t>(kMaxExtraPointLights)) {
        std::stable_sort(out.lights.begin() + static_cast<std::ptrdiff_t>(light_start), out.lights.end(),
            [](const PointLight &a, const PointLight &b) { return (a.intensity * a.radius) > (b.intensity * b.radius); });
        out.lights.resize(light_start + static_cast<std::size_t>(kMaxExtraPointLights));
    }
}

void AttackFxView::clear()
{
    this->actives_.clear();
    this->seen_projections_.clear();
    this->seen_missiles_.clear();
}

} // namespace hd2d
