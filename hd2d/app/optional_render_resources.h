// 遊ぶ途中で初めて生成する描画資源と、その生成状態をまとめて所有する。
#pragma once
#include "render/text_overlay.h"
#include "render/glyph_atlas.h"
#include "render/billboard_renderer.h"
#include "render/ground_ring.h"
#include "render/targeting_renderer.h"
#include "render/light_shaft.h"
#include "render/model_renderer.h"
#include "render/voxel_fx_renderer.h"

namespace hd2d {
struct OptionalRenderResources {
    TextOverlay ascii_text;
    int ascii_text_px{ 0 };
    bool ascii_text_failed{ false };
    bool ascii_text_pending{ false };
    GlyphAtlas glyph_atlas;
    BillboardRenderer glyph_boards;
    bool glyph_ready{ false };
    bool glyph_failed{ false };
    GroundRingRenderer ground_rings;
    TargetingRenderer targeting;
    bool targeting_failed{ false };
    LightShaftRenderer light_shafts;
    //! 攻撃の視覚効果（矢・火の玉・爆発のボクセル）。効果が起きた最初の 1 度だけ用意する。
    VoxelFxRenderer attack_fx;
    bool attack_fx_failed{ false };
    ModelRenderer player_model;
    ModelRenderer wall_model;
    ModelRenderer floor_model;
    std::array<ModelRenderer,kTerrainModelCount> terrain_parts;

    OptionalRenderResources() = default;
    OptionalRenderResources(const OptionalRenderResources &) = delete;
    OptionalRenderResources &operator=(const OptionalRenderResources &) = delete;
    ~OptionalRenderResources() { shutdown(); }

    // GL コンテキストの破棄前に呼ぶ。未生成・解放済みの資源にも安全に呼べる。
    void shutdown()
    {
        player_model.shutdown();
        wall_model.shutdown();
        floor_model.shutdown();
        for(auto &part:terrain_parts) part.shutdown();
        ascii_text.shutdown();
        ground_rings.shutdown();
        targeting.shutdown();
        light_shafts.shutdown();
        attack_fx.shutdown();
        glyph_atlas.shutdown();
        glyph_boards.shutdown();
        ascii_text_px = 0;
        ascii_text_pending = false;
        glyph_ready = false;
    }
};
} // namespace hd2d
