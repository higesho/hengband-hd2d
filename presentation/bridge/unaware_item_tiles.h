/*!
 * @file unaware_item_tiles.h
 * @brief 正体の分からないアイテムを描くときの代表ボクセル板（分類名 → 変愚の K の番号）
 *
 * 契約:。
 * 対応表は各コアで書き写さず、このファイル 1 か所だけに置く（同文書 §4）。
 * 各コアのアダプタは、自分の分類をここの UnawareItemCategory へ対応づけたうえで
 * unaware_item_representative_bi_id() を呼ぶ。K の番号から実際の tile_index への変換は
 * lookup_object_tile()（official_tile_table.h）で行う——このファイルは K の番号だけを持つ。
 *
 * 8 分類に当たらない「見た目だけ分かる分類」（Sil-Q の角笛・薬草、Frox の
 * Hand of Vecna・Eye of Vecna など）はここに足さない。契約 §3 の決定により、
 * その分類は代表を当てず今までどおりそのアイテム自身のタイルで描く
 * （決めたこと待ちではない。読む側が「まだ決まっていない」と誤読しないよう明記する）。
 */
#pragma once

#include "bridge/unaware_item_categories.h"

#include <cstdint>

namespace presentation {

/*!
 * @brief の 8 分類。
 * @details 値は `unaware_item_categories.h`（C からも読めるマクロ）と揃えてある。
 * `static_assert` 群（下）が突き合わせる——ここだけ書き換えると必ずコンパイルが止まる。
 */
enum class UnawareItemCategory : uint8_t {
    Potion = UNAWARE_CATEGORY_POTION,     //!< 薬
    Scroll = UNAWARE_CATEGORY_SCROLL,     //!< 巻物
    Ring = UNAWARE_CATEGORY_RING,         //!< 指輪
    Amulet = UNAWARE_CATEGORY_AMULET,     //!< アミュレット
    Wand = UNAWARE_CATEGORY_WAND,         //!< ワンド
    Staff = UNAWARE_CATEGORY_STAFF,       //!< 杖
    Rod = UNAWARE_CATEGORY_ROD,           //!< ロッド
    Mushroom = UNAWARE_CATEGORY_MUSHROOM, //!< キノコ
};

static_assert(static_cast<int>(UnawareItemCategory::Potion) == UNAWARE_CATEGORY_POTION, "分類番号が C 側とずれている: Potion");
static_assert(static_cast<int>(UnawareItemCategory::Scroll) == UNAWARE_CATEGORY_SCROLL, "分類番号が C 側とずれている: Scroll");
static_assert(static_cast<int>(UnawareItemCategory::Ring) == UNAWARE_CATEGORY_RING, "分類番号が C 側とずれている: Ring");
static_assert(static_cast<int>(UnawareItemCategory::Amulet) == UNAWARE_CATEGORY_AMULET, "分類番号が C 側とずれている: Amulet");
static_assert(static_cast<int>(UnawareItemCategory::Wand) == UNAWARE_CATEGORY_WAND, "分類番号が C 側とずれている: Wand");
static_assert(static_cast<int>(UnawareItemCategory::Staff) == UNAWARE_CATEGORY_STAFF, "分類番号が C 側とずれている: Staff");
static_assert(static_cast<int>(UnawareItemCategory::Rod) == UNAWARE_CATEGORY_ROD, "分類番号が C 側とずれている: Rod");
static_assert(static_cast<int>(UnawareItemCategory::Mushroom) == UNAWARE_CATEGORY_MUSHROOM, "分類番号が C 側とずれている: Mushroom");

//! 分類 → 代表の変愚 K の番号（bi_id）。 の表そのもの。
constexpr uint16_t unaware_item_representative_bi_id(UnawareItemCategory category)
{
    switch (category) {
    case UnawareItemCategory::Potion:
        return 229; // 灰色の瓶
    case UnawareItemCategory::Scroll:
        return 175; // 封蝋だけの巻物
    case UnawareItemCategory::Ring:
        return 147; // 飾りのない灰色の輪
    case UnawareItemCategory::Amulet:
        return 166; // 灰色の札
    case UnawareItemCategory::Wand:
        return 270; // ただの棒
    case UnawareItemCategory::Staff:
        return 319; // ただの杖
    case UnawareItemCategory::Rod:
        return 353; // 暗い色の棒
    case UnawareItemCategory::Mushroom:
        return 2; // 灰色のキノコ
    }
    return 0;
}

} // namespace presentation
