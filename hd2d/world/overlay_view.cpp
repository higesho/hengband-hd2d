/*!
 * @file overlay_view.cpp
 * @brief `overlay_view.h` の実装。
 */
#include "world/overlay_view.h"

#include "render/term_colors.h" //!< コアの 16 色（重ね書きも同じ表を引く）

#include "frame/game_frame.h"

namespace hd2d {

namespace {

/*!
 * @brief 床から浮かせる量（マス）。**実体より高く**。
 * @details 重ね書きは「そのマスで起きたこと」なので、足元に置くと立っている敵の板
 * （高さ 1 マト）に隠れる——ダメージの数字が敵の背中へ埋まる。
 * 板の上端より少し上に置いて、必ず読めるようにする。
 */
constexpr float kOverlayLift = 1.05f;

//! 字が無いときに使う縦横比（`entity_view.cpp` と同じ既定）。
constexpr float kFallbackAspect = 0.6f;

} // namespace

int build_overlay_glyphs(const GameFrame &frame, GlyphAtlas &atlas, float height_cells,
    std::vector<BillboardInstance> &out)
{
    out.clear();
    if (frame.map_overlay.empty()) {
        return 0;
    }
    const float tall = (height_cells > 0.01f) ? height_cells : 0.8f;

    out.reserve(frame.map_overlay.size());
    for (const MapOverlayCell &cell : frame.map_overlay) {
        //! **空白は重ね書きではない。** 消し残しや区画の端で普通に出る。
        if ((cell.ascii == '\0') || (cell.ascii == ' ')) {
            continue;
        }
        const GlyphAtlas::Entry *const glyph = atlas.entry_for(cell.ascii);
        if (glyph == nullptr) {
            continue; //!< 焼けなかった字。**捏造しない**（実体の字と同じ作法）
        }
        const float aspect
            = (glyph->h > 0) ? (static_cast<float>(glyph->w) / static_cast<float>(glyph->h)) : kFallbackAspect;
        const RgbColor rgb = term_color_to_rgb(cell.color);

        BillboardInstance board;
        //! マスの中心。**追随はしない**（重ね書きに追う個体は無い。`overlay_view.h`）。
        board.x = static_cast<float>(cell.gx) + 0.5f;
        board.y = static_cast<float>(cell.gy) + 0.5f;
        board.z = kOverlayLift;
        board.height = tall;
        board.width = tall * aspect;
        board.u0 = glyph->u0;
        board.v0 = glyph->v0;
        board.u1 = glyph->u1;
        board.v1 = glyph->v1;
        //! 色は**コアの 16 色そのまま**（`&` や pref で変えた色もそのまま届く）。
        board.r = static_cast<float>(rgb.r) / 255.f;
        board.g = static_cast<float>(rgb.g) / 255.f;
        board.b = static_cast<float>(rgb.b) / 255.f;
        out.push_back(board);
    }
    return static_cast<int>(out.size());
}

} // namespace hd2d
