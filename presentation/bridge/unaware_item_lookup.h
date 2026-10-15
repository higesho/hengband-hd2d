/*!
 * @file unaware_item_lookup.h
 * @brief 変愚・短愚（`ItemEntity`）向け: 正体の分からないアイテムの tile_index 解決
 *
 * 契約:。分類 → 代表 K の対応表は unaware_item_tiles.h の
 * 1 か所だけに置いてあり、このファイルはそれを `ItemEntity` の tval へつなぐだけ。
 * 床のアイテム（`presentation_bridge.cpp`）と一覧のアイコン（`modern_store.cpp`）の
 * 両方がここを呼ぶ——**書き写さない**。
 */
#pragma once

#include "bridge/official_tile_table.h"
#include "bridge/unaware_item_tiles.h"
#include "object/tval-types.h"
#include "system/baseitem/baseitem-record.h" // get_baseitem_record().is_apparent() に要る（item-entity.h は前方宣言だけ）
#include "system/item/item-entity.h"
#include <cstdint>
#include <tl/optional.hpp>

namespace presentation {

/*!
 * @brief アイテムが「正体の分からないアイテム」の 8 分類のどれかを返す
 * @details 分類の有無は `BaseitemRecord::is_apparent()`（未鑑定名の有無＝薬・巻物・指輪・
 * アミュレット・ワンド・杖・ロッドと、キノコだけの FOOD）で決まる
 * （`system/baseitem/baseitem-record.cpp` の `is_apparent()` の注記のとおり）。
 * 呼ぶ側は `is_apparent() && !is_aware()` を先に確かめてから呼ぶこと。
 */
inline tl::optional<UnawareItemCategory> unaware_item_category(const ItemEntity &item)
{
    switch (item.bi_key.tval()) {
    case ItemKindType::POTION:
        return UnawareItemCategory::Potion;
    case ItemKindType::SCROLL:
        return UnawareItemCategory::Scroll;
    case ItemKindType::RING:
        return UnawareItemCategory::Ring;
    case ItemKindType::AMULET:
        return UnawareItemCategory::Amulet;
    case ItemKindType::WAND:
        return UnawareItemCategory::Wand;
    case ItemKindType::STAFF:
        return UnawareItemCategory::Staff;
    case ItemKindType::ROD:
        return UnawareItemCategory::Rod;
    case ItemKindType::FOOD:
        // FOOD のうち is_apparent() が真になるのはキノコだけ（配給・水袋等は appearance_id を持たない）。
        return UnawareItemCategory::Mushroom;
    default:
        return tl::nullopt;
    }
}

//! アイテム 1 個の tile_index。正体不明なら代表の板、識別済み（か対象外）なら種類の板。
inline uint16_t resolve_item_tile(const ItemEntity &item)
{
    if (item.get_baseitem_record().is_apparent() && !item.is_aware()) {
        if (const auto category = unaware_item_category(item)) {
            if (const uint16_t ut = lookup_object_tile(unaware_item_representative_bi_id(*category))) {
                return ut;
            }
        }
    }
    return lookup_object_tile(static_cast<uint16_t>(item.bi_id));
}

} // namespace presentation
