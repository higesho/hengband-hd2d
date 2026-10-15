/*!
 * @file sq_unaware_category.h
 * @brief 正体の分からないアイテムの分類判定（Sil-Q のヘッダを見てよい C の TU 専用）。
 *
 * `object_type` は Sil-Q のヘッダ（`angband.h`）が持つ型なので、これを include する側
 * （`sq_shim.c`・`sq_native_look.c`）は、その TU で `angband.h` を先に include して
 * おくこと。実装は `sq_shim.c` にある——一覧側は extern で共有するだけで、同じ判定を
 * 2 か所に書かない。
 */
#pragma once

/*!
 * @brief 正体の分からないアイテムの分類（`UNAWARE_CATEGORY_*`。`unaware_item_categories.h`）を返す。
 * @return 0（対象外）か 1・3・4・6（Sil-Q で実際に出る番号。巻物・ワンド・ロッドの
 * tval が無いので 2・5・7 は出ない。`sq_shim.c` の `sq_unaware_category()` の註記参照）
 */
unsigned char sq_unaware_category(const object_type *o_ptr);
