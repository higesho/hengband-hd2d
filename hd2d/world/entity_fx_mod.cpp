/*!
 * @file entity_fx_mod.cpp
 * @brief `entity_fx_mod.h` の中身。
 */
#include "world/entity_fx_mod.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

FxModTransform fx_mod_transform(const FxEntityMod &mod)
{
    FxModTransform t;
    const float squash = std::min(std::max(mod.squash, 0.f), 1.f);
    const float recoil = std::min(std::max(mod.recoil, 0.f), 1.f);
    const float grey = std::min(std::max(mod.grey, 0.f), 1.f);
    t.scale_z = 1.f - (0.6f * squash);
    t.scale_xy = 1.f + (0.25f * squash);
    t.dx = (mod.tremble * std::sin(mod.tremble_phase)) + (mod.away_x * 0.22f * recoil);
    t.dy = (mod.tremble * std::cos(mod.tremble_phase * 1.31f) * 0.6f) + (mod.away_y * 0.22f * recoil);
    t.dz = mod.lift;
    t.pitch = 0.32f * recoil;
    t.tint = 1.f - (0.55f * grey);
    t.glow = 0.28f * grey;
    return t;
}

const FxEntityMod *fx_find_mod(const std::vector<FxEntityMod> &mods, int gx, int gy)
{
    for (const FxEntityMod &m : mods) {
        if ((m.gx == gx) && (m.gy == gy)) {
            return &m;
        }
    }
    return nullptr;
}

namespace {

void tint_slab(InstanceData &inst, const FxModTransform &t)
{
    inst.r *= t.tint;
    inst.g *= t.tint;
    inst.b *= t.tint;
    inst.er += t.glow;
    inst.eg += t.glow;
    inst.eb += t.glow;
}

} // namespace

void fx_apply_entity_mods(EntityView &view, const std::vector<FxEntityMod> &mods)
{
    if (mods.empty()) {
        return;
    }
    // ボクセル板
    std::vector<SlabInstance> echoes;
    for (SlabInstance &slab : view.slabs) {
        const FxEntityMod *m = fx_find_mod(mods, static_cast<int>(std::floor(slab.cx)), static_cast<int>(std::floor(slab.cy)));
        if ((m == nullptr) || (slab.slab < 0) || (slab.layer != EntityLayer::Monster && slab.layer != EntityLayer::Player)) {
            continue;
        }
        const FxModTransform t = fx_mod_transform(*m);
        const float old_sx = slab.inst.sx;
        const float old_sy = slab.inst.sy;
        slab.inst.sx *= t.scale_xy;
        slab.inst.sy *= t.scale_xy;
        slab.inst.sz *= t.scale_z;
        slab.inst.x += (0.5f * (old_sx - slab.inst.sx)) + t.dx;
        slab.inst.y += (0.5f * (old_sy - slab.inst.sy)) + t.dy;
        slab.inst.z += t.dz;
        slab.inst.pitch += t.pitch;
        slab.height *= t.scale_z;
        tint_slab(slab.inst, t);
        if ((std::fabs(m->echo_x) + std::fabs(m->echo_y)) > 1e-4f) {
            SlabInstance copy = slab;
            copy.inst.x += m->echo_x;
            copy.inst.y += m->echo_y;
            copy.cx += m->echo_x;
            copy.cy += m->echo_y;
            copy.inst.r *= 0.85f;
            copy.inst.g *= 0.95f;
            copy.inst.b *= 1.1f;
            echoes.push_back(copy);
        }
    }
    view.slabs.insert(view.slabs.end(), echoes.begin(), echoes.end());

    // 字の板（アスキー実体）
    const bool layered = view.glyph_layers.size() == view.glyphs.size();
    const std::size_t glyph_count = view.glyphs.size();
    for (std::size_t i = 0; i < glyph_count; ++i) {
        BillboardInstance &g = view.glyphs[i];
        const FxEntityMod *m = fx_find_mod(mods, static_cast<int>(std::floor(g.x)), static_cast<int>(std::floor(g.y)));
        if (m == nullptr) {
            continue;
        }
        if (layered && (view.glyph_layers[i] != EntityLayer::Monster) && (view.glyph_layers[i] != EntityLayer::Player)) {
            continue;
        }
        const FxModTransform t = fx_mod_transform(*m);
        g.width *= t.scale_xy;
        g.height *= t.scale_z;
        g.x += t.dx;
        g.y += t.dy;
        g.z += t.dz;
        g.r *= t.tint;
        g.g *= t.tint;
        g.b *= t.tint;
        if ((std::fabs(m->echo_x) + std::fabs(m->echo_y)) > 1e-4f) {
            BillboardInstance copy = g;
            copy.x += m->echo_x;
            copy.y += m->echo_y;
            copy.r *= 0.85f;
            copy.g *= 0.95f;
            copy.b *= 1.1f;
            view.glyphs.push_back(copy);
            if (layered) {
                view.glyph_layers.push_back(view.glyph_layers[i]);
            }
        }
    }
}

Mat4 fx_mod_model_matrix(const FxEntityMod &mod, const Vec3 &foot)
{
    const FxModTransform t = fx_mod_transform(mod);
    Mat4 scale = Mat4::identity();
    scale.m[0] = t.scale_xy;
    scale.m[5] = t.scale_xy;
    scale.m[10] = t.scale_z;
    return translation(Vec3{ foot.x + t.dx, foot.y + t.dy, foot.z + t.dz }) * scale;
}

} // namespace hd2d
