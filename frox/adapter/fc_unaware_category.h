/*!
 * @file fc_unaware_category.h
 * @brief 正体の分からないアイテムの分類判定（Frox のヘッダを見てよい C の TU 専用）。
 *
 * `object_type` は Frox のヘッダ（`angband.h`）が持つ型なので、これを include する側
 * （`fc_shim.c`・`fc_native_store_common.h`）は、その TU で `angband.h` を先に include
 * しておくこと。実装は `fc_shim.c` にある——一覧側は extern で共有するだけで、同じ判定を
 * 2 か所に書かない。
 */
#pragma once

/*!
 * @brief 正体の分からないアイテムの分類（`UNAWARE_CATEGORY_*`。`unaware_item_categories.h`）を返す。
 * @return 0（対象外）か 1・2・8（Frox で実際に出る番号。指輪・アミュレット・ワンド・杖・
 * ロッドは Frox では該当しない。`fc_shim.c` の `fc_unaware_category()` の註記参照）
 */
unsigned char fc_unaware_category(const object_type *o_ptr);
