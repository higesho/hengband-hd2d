/*!
 * @file tile_index_table.h
 * @brief ASCII + fg_color → tile_index 静的ルックアップ（PHASE3 T2 / P3-B）
 *
 * コア地形定義は改変しない。未登録は 0（色クアッド／共通 placeholder）。
 */
#pragma once

#include <cstdint>

namespace presentation {

/*!
 * @brief ascii_fallback と前景色からアトラス索引を返す。
 * @return 0 = 未登録／空白。非 0 = プレースホルダ色 mod 用索引。
 */
inline uint16_t lookup_tile_index(char ascii_fallback, uint8_t fg_color)
{
    if (ascii_fallback == '\0' || ascii_fallback == ' ') {
        return 0;
    }

    // 印字 ASCII を 1..95 相当にマップし、色下位 4bit でスロットをずらす。
    // 単一 placeholder アトラスでも Renderer が色 mod で区別できる。
    const auto ch = static_cast<unsigned char>(ascii_fallback);
    if (ch < 33 || ch > 126) {
        return 0;
    }
    const uint16_t base = static_cast<uint16_t>(ch - 32); // 1..94
    const uint16_t slot = static_cast<uint16_t>((fg_color & 0x0Fu) << 7);
    return static_cast<uint16_t>(base | slot);
}

} // namespace presentation
