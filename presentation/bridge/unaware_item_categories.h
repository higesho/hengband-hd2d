/*!
 * @file unaware_item_categories.h
 * @brief 正体の分からないアイテムの分類番号（1..8）。C からも C++ からも読める。
 *
 * 契約:。プレーンな C のマクロにしてあるのは、
 * 各コアのシム（`gb_shim.c`・`fc_shim.c`・`sq_shim.c`）と一覧の記述コード
 * （`gb_native_store.c`・`fc_native_store_common.h`・`sq_native_look.c`）が
 * これらの TU（`.c`）から直接使うため。C++ 側の `presentation::UnawareItemCategory`
 * （`unaware_item_tiles.h`）はこの番号と静的に突き合わせている
 * （`unaware_item_tiles.h` の `static_assert`）——**この番号を直接書き換えたら
 * 向こうの static_assert がコンパイルを止める**ので、ずれたまま気づかないことはない。
 *
 * 0 は「対象外」（識別済み・8 分類の外）に予約してあるので使わない。
 */
#ifndef HENGBAND_UNAWARE_ITEM_CATEGORIES_H
#define HENGBAND_UNAWARE_ITEM_CATEGORIES_H

#define UNAWARE_CATEGORY_NONE 0
#define UNAWARE_CATEGORY_POTION 1
#define UNAWARE_CATEGORY_SCROLL 2
#define UNAWARE_CATEGORY_RING 3
#define UNAWARE_CATEGORY_AMULET 4
#define UNAWARE_CATEGORY_WAND 5
#define UNAWARE_CATEGORY_STAFF 6
#define UNAWARE_CATEGORY_ROD 7
#define UNAWARE_CATEGORY_MUSHROOM 8

#endif /* HENGBAND_UNAWARE_ITEM_CATEGORIES_H */
