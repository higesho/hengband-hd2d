/*!
 * @file gb_manifest.h
 * @brief `gensoband/tilework/mapping.csv` → タイル目録（設計 §5.1 / v1 §8.2）。
 *
 * 対応表は**コアが持つ**（必守制約 1: UI にコア固有の知識を持ち込まない）。
 * ここが `asset_manifest` を組み、`gb_frame` の `tile_index` 引きにも同じ表を使う。
 */
#pragma once

#include "frame/protocol_messages.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace gb {

/*!
 * @brief mapping.csv の写し。**行順が `tile_index` の 1..N** である（§5.1）。
 *
 * @details 表を 2 つ持たない——`asset_manifest` に載せる並びと、`tile_index` を
 * 引く索引は**同じ 1 本の読み込み結果**から作る。別々に作ると番号がずれたときに
 * 画面と索引が静かに食い違う（絵が 1 つずれるという、いちばん気づきにくい壊れ方）。
 */
class TileManifest {
public:
    /*!
     * @brief 読み込む。
     * @param csv_path `<exe_dir>/gensoband/tilework/mapping.csv`
     * @param root `asset_manifest.root`（＝ exe のあるディレクトリ。絶対パス）
     * @return 読めたら真。読めなければ偽（`error()` に理由）
     *
     * **RFC4180 の引用に対応する。** 記号に `,` と `"` が居る（R962 / R1417 / K594 /
     * K150 ほか）ので、素の `split(',')` では割れない（§5.1）。
     */
    bool load(const std::string &csv_path, const std::string &root);

    //! v1 §8.2 の `asset_manifest`。`load()` が成功していれば行数ぶん入っている。
    const presentation::AssetManifestMessage &message() const { return this->message_; }

    /*!
     * @brief 実体 → 目録索引。
     * @param kind 'P' / 'R' / 'K' / 'F'
     * @param id 幻想蛮怒の id（P は 0）
     * @return 1..N、または 0（未登録）
     */
    std::uint16_t lookup(char kind, int id) const;

    //! 絵の実体が無かった行の数（起動時に stderr へ数える。捏造しない。§5.1）。
    int missing_files() const { return this->missing_files_; }
    const std::string &error() const { return this->error_; }

private:
    presentation::AssetManifestMessage message_;
    //! `(kind << 24) | id` → index。kind は 4 種・id は 16bit に収まるので衝突しない。
    std::unordered_map<std::uint32_t, std::uint16_t> index_;
    int missing_files_{ 0 };
    std::string error_;
};

} // namespace gb
