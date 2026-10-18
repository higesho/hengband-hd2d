/*!
 * @file tb_unaware_category.h
 * @brief 正体の分からないアイテムの分類判定（TObandのヘッダを見てよい C の TU 専用）。
 *
 * `object_type` はTObandのヘッダ（`angband.h`）が持つ型なので、これを include する
 * 側（`tb_shim.c`）は、その TU で `angband.h` を先に include して
 * おくこと。実装は `tb_shim.c` にある——同じ判定を 2 か所に書かない。
 */
#pragma once

/*!
 * @brief 正体の分からないアイテムの分類（`UNAWARE_CATEGORY_*`。`unaware_item_categories.h`）を返す。
 * @return 0（対象外）か 1..8（分類番号）
 */
unsigned char tb_unaware_category(const object_type *o_ptr);
