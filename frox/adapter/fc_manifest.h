/*!
 * @file fc_manifest.h
 * @brief `frox/tilework/terrain_map.csv` → タイル目録（設計 §4.2 / §5 / v1 §8.2）。
 *
 * ## 表は 2 つのことを言っている
 * 1. `path` … **2D の絵**（目録に載せる。`tile_index` で引く）
 * 2. `voxel_id` … **立体の地形番号**（`fc_frame` が `cell.terrain_id` に載せる）
 *
 * 対応表は**コアが持つ**（設計 §1 制約 3: UI にコア固有の知識を持ち込まない）。
 * ここが `asset_manifest` を組み、`fc_frame` の `tile_index` 引きにも同じ表を使う。
 *
 * ## 実体（R / K / P）は **M2 で載せた**
 * `frox/tilework/mapping.csv` を `load_entities()` で**同じ目録へ継ぎ足す**。
 * 地形 188 行のうしろに 2,004 行が並び、`tile_index` は 1 本の通し番号になる
 * ——表を 2 つ持つと番号がずれたときに画面と索引が静かに食い違う。
 *
 * 目録に無い実体は**引かずに 0 を返す**（上流が実体を足したときはここへ落ちる）。
 * 画面側は `ascii_fallback` から**字の板**を立てる（`hd2d/render/glyph_atlas.h`）。
 * **0 を返すことと嘘の索引を返すことは違う**（捏造しない）。
 *
 * 表の作り方は `tools/frox/fc_terrain_map.py`。表と規則がずれていないかは
 * 引数なしで走らせれば照合できる。
 */
#pragma once

#include "frame/protocol_messages.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace fc {

/*!
 * @brief terrain_map.csv の写し。**行順が `tile_index` の 1..N** である。
 *
 * @details 表を 2 つ持たない——`asset_manifest` に載せる並びと、`tile_index` を
 * 引く索引は**同じ 1 本の読み込み結果**から作る。別々に作ると番号がずれたときに
 * 画面と索引が静かに食い違う（絵が 1 つずれるという、いちばん気づきにくい壊れ方）。
 */
class TileManifest {
public:
    /*!
     * @brief 読み込む。
     * @param csv_path `<exe_dir>/frox/tilework/terrain_map.csv`
     * @param root `asset_manifest.root`（＝ exe のあるディレクトリ。絶対パス）
     * @return 読めたら真。読めなければ偽（`error()` に理由）
     *
     * 引用符に対応する。**地形の名前に `,` が入る**（`slippery slope -> MOUNTAIN
     * (F: MOVE | CAN_CLIMB, no WALL...)` の註記欄）ので、素の `split(',')` では
     * 割れない。註記は最後の欄なので実害は註記だけだが、割れ方を毎回考えるより
     * RFC4180 を守るほうが安い。
     */
    bool load(const std::string &csv_path, const std::string &root);

    /*!
     * @brief 実体（R / K / P）を**継ぎ足す**。`load()` の**あと**に呼ぶこと。
     * @param csv_path `<exe_dir>/frox/tilework/mapping.csv`
     * @param root `load()` に渡したものと同じ（`message_.root` は上書きしない）
     * @return 読めたら真。読めなければ偽（`error()` に理由。**地形だけで遊べる**ので致命ではない）
     *
     * @details 欄は `kind,fc_id,source,value`。`source` は今のところ `tile` だけで、
     * 字の板は画面側が `ascii_fallback` から立てるので目録には載せない。
     */
    bool load_entities(const std::string &csv_path, const std::string &root);

    /*!
     * @brief 実体 → 目録索引。
     * @param kind `'R'`（`r_idx`）/ `'K'`（`k_idx`）/ `'P'`（`prace`）
     * @param id Frox の id
     * @return 1..N、または 0（未登録＝字の板へ落とす）
     */
    std::uint16_t lookup_entity(char kind, int id) const;

    //! 実体の行数（起動時に stderr へ出す）。
    int entity_count() const { return static_cast<int>(this->entity_index_.size()); }

    //! v1 §8.2 の `asset_manifest`。`load()` が成功していれば行数ぶん入っている。
    const presentation::AssetManifestMessage &message() const { return this->message_; }

    /*!
     * @brief 地形 → 目録索引（2D の絵）。
     * @param feat Frox の feat 番号（ミミック解決後）
     * @return 1..N、または 0（未登録）
     */
    std::uint16_t lookup_terrain(int feat) const;

    /*!
     * @brief 地形 → **立体（HD2D）へ送る地形番号**（`terrain_map.csv` の `voxel_id`）。
     * @param feat Frox の feat 番号
     * @param fallback 表に無い feat のときに返す値
     *
     * @details 画面側は `terrain_id` で `assets/voxel/terrain_prefabs.jsonc` を引く。
     * あの表は**変愚の番号**でできている。
     * Frox の feat は 221 まで変愚と同じなのでふつうは恒等だが、**222〜244 は
     * Frox が足した地形**なので、そのまま送ると同じ番号の別の地形が出る
     * （雪の階が丸ごと土と石になる）。そこだけ 1522〜1544 の予約帯を指す。
     */
    std::uint16_t voxel_terrain(int feat, std::uint16_t fallback) const;

    //! 表の行数（起動時に stderr へ出す）。
    int terrain_count() const { return static_cast<int>(this->index_.size()); }

    //! 絵の実体が無かった行の数（起動時に stderr へ数える。捏造しない）。
    int missing_files() const { return this->missing_files_; }
    const std::string &error() const { return this->error_; }

private:
    presentation::AssetManifestMessage message_;
    std::unordered_map<int, std::uint16_t> index_; //!< feat → 目録索引
    std::unordered_map<int, std::uint16_t> voxel_index_; //!< feat → 立体の地形番号
    //! `(kind << 24) | id` → 目録索引。kind は 3 種・id は 16bit に収まるので衝突しない。
    std::unordered_map<std::uint32_t, std::uint16_t> entity_index_;
    int missing_files_{ 0 };
    std::string error_;
};

} // namespace fc
