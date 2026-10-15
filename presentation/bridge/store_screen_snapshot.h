/*!
 * @file store_screen_snapshot.h
 * @brief 原作の店の一覧を描いた瞬間の値の写し（変愚 3.0.2.5 から）。
 *
 * 変愚 3.0.2.5 は店の大域変数（`st_ptr`・`ot_ptr`・`store_top`・`store_bottom`・`cur_store_feat`）をやめ、
 * 表示中のページなどを `StoreScreen`（店のコマンドの中だけにある）にまとめた。画面寄りのコード
 * （`modern_store.cpp` の新しい店の画面）はそれを外から読めないので、原作が店の一覧を描く関数
 * `display_store_inventory()` の頭に、狙いの上書き（`tools/targeting/overlay.py` の `observe_store_screen`）が
 * `hb_store_screen_drawn()` を差す。描くたびに値を写すので、画面の字とずれる瞬間が無い。
 *
 * 店を出たことは、画面寄りのコードがゲームの画面へ戻った（`character_icky_depth` が 0 の）フレームで知り、写しを捨てる。
 * 短愚（3.0.2.4）は原作の大域変数を読むままで、これを使わない。。
 */
#pragma once

#include <cstddef>

class Store;

namespace presentation {

struct StoreScreenSnapshot {
    const Store *store = nullptr; //!< 町の店（TownList が持つので、店を出ても消えない）
    std::size_t town_index = 0; //!< 店のある町
    int page_top = 0; //!< 表示中のページの先頭にある在庫の番号
    int page_size = 0; //!< 1 ページの行数（端末の高さで変わる）
    int page_item_count = 0; //!< 表示中のページにある在庫の数
};

//! いちばん新しい写し。`store == nullptr` なら店の中ではない。
inline StoreScreenSnapshot store_screen_snapshot;

} // namespace presentation

/*!
 * @brief 原作が店の一覧を描いた（狙いの上書きが `display_store_inventory()` の頭に差す）。
 */
inline void hb_store_screen_drawn(const Store &store, std::size_t town_index, int page_top, int page_size, int page_item_count)
{
    presentation::store_screen_snapshot = { &store, town_index, page_top, page_size, page_item_count };
}
