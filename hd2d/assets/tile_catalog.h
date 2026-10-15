/*!
 * @file tile_catalog.h
 * @brief コアが申告したタイル目録（`asset_manifest`、v1 §8.2）の辞書。
 *
 * @details **タイル表は同梱しない。**どの実体がどの絵かを決めているのはコアなので、
 * 握手のあとに送られてくる目録だけを引く（Phase 2 P2-1「タイル供給の逆転」）。
 * 目録に無い索引は「未登録」を返し、呼び出し側は**何も描かない**（捏造しない）。
 *
 * 旧 2D UI のタイルの目録が同じことをしていたが、**コードは引かない**
 * （設計書 必守制約 6）。あちらは `const char *` を返す C 風の口で、こちらは
 * 種別と ID も一緒に返す。ビルボードは「モンスターかアイテムか」で立ち方を変えるため。
 */
#pragma once

#include "frame/protocol_messages.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace hd2d {

struct TileEntry {
    char kind{ 0 }; //!< 'P' プレイヤ / 'R' モンスター / 'K' アイテム / 'F' 地形
    int id{ 0 };
    std::string path; //!< root 解決済みの絶対パス
};

class TileCatalog {
public:
    //! 目録を差し替える（2 回目以降は全置換）。
    void apply(const presentation::AssetManifestMessage &message);
    bool present() const { return this->present_; }
    std::size_t size() const { return this->entries_.size(); }

    //! 索引を引く。未登録は nullptr。
    const TileEntry *find(std::uint16_t tile_index) const;

    /*!
     * @brief アイテムの番号（`MapCellView::object_id`／`MissileFx::item_id` と同じ番号）から、床のアイテムを描くのと
     * 同じ絵の索引を引く。無ければ 0。
     * @details 飛んでいく物の板（攻撃の視覚効果）が、床のアイテムと同じ板を使うための逆引き。
     * 同じ番号に絵が複数あれば、いちばん小さい索引を返す。別名（`aliases`）の索引も引ける。
     */
    std::uint16_t find_item_tile(std::uint16_t item_id) const;

private:
    bool present_{ false };
    std::unordered_map<std::uint16_t, TileEntry> entries_;
    //! アイテム（`kind == 'K'`）の番号 → 索引。
    std::unordered_map<std::uint16_t, std::uint16_t> item_tiles_;
};

} // namespace hd2d
