// 遊ぶ途中で初めて生成する描画資源と、その生成状態をまとめて所有する。
#pragma once
#include "render/text_overlay.h"
#include "render/glyph_atlas.h"
#include "render/billboard_renderer.h"
#include "render/ground_ring.h"
#include "render/light_shaft.h"

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
    LightShaftRenderer light_shafts;

    OptionalRenderResources() = default;
    OptionalRenderResources(const OptionalRenderResources &) = delete;
    OptionalRenderResources &operator=(const OptionalRenderResources &) = delete;
    ~OptionalRenderResources() { shutdown(); }

    // GL コンテキストの破棄前に呼ぶ。未生成・解放済みの資源にも安全に呼べる。
    void shutdown()
    {
        ascii_text.shutdown();
        ground_rings.shutdown();
        light_shafts.shutdown();
        glyph_atlas.shutdown();
        glyph_boards.shutdown();
        ascii_text_px = 0;
        ascii_text_pending = false;
        glyph_ready = false;
    }
};
} // namespace hd2d
