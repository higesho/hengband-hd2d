/*!
 * @file sq_manifest.h
 * @brief `silq/tilework/terrain_map.csv` → タイル目録（設計 §5 / v1 §8.2）。
 *
 * ## 表は 2 つのことを言っている
 * 1. `path` … **2D の絵**（目録に載せる。`tile_index` で引く）
 * 2. `voxel_id` … **立体の地形番号**（`sq_frame` が `cell.terrain_id` に載せる）
 *
 * 2 は 2026-08-22 に足した欄で、それまでは feat をそのまま送っていた（→ `voxel_terrain`）。
 *
 * 対応表は**コアが持つ**（必守制約 1: UI にコア固有の知識を持ち込まない）。
 * ここが `asset_manifest` を組み、`sq_frame` の `tile_index` 引きにも同じ表を使う。
 *
 * ## 実体（R / K / P）も載せる（M2。2026-08-21）
 * `silq/tilework/128/S{R,K,P}<番号>.png` を**その場で数えて**足す。表を別に持たないのは
 * 地形と同じ理由で、**在るものだけを載せる**——絵を 1 枚足せば次の起動から出る。
 * 番号の意味は Sil-Q の定義ファイルそのまま（`SR` = `monster.txt` の N、`SK` =
 * `object.txt` の N、`SP` = `race.txt` の N）。
 *
 * 絵が無い実体は**索引 0 のまま**で、画面側はアスキー実体モードで文字の板を立てる。
 * 0 を返すことと嘘の索引を返すことは違う（捏造しない）。
 *
 * ## 絵を分け合う品がある（2026-08-21）
 * `object.txt` には**中身だけが違って見た目が同じ**品が並ぶ区間がある（チュートリアルの
 * 地形の記憶 40 個）。そこは代表の 1 枚を焼き、残りを `aliases[]` で同じ索引へ寄せる
 * （v1 §8.2 の「既存 index への別 ID 対応」。**新しい索引は作らない**）。
 * 区間の表は `sq_manifest.cpp` の `kSharedObjectRuns` 1 か所だけにある。
 */
#pragma once

#include "frame/protocol_messages.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace sq {

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
     * @param csv_path `<exe_dir>/silq/tilework/terrain_map.csv`
     * @param root `asset_manifest.root`（＝ exe のあるディレクトリ。絶対パス）
     * @return 読めたら真。読めなければ偽（`error()` に理由）
     *
     * 引用符の対応は要らない（この表の値に `,` も `"` も入らない——地形名は
     * `terrain.txt` の N 行そのままで、`<darkness>` のような山括弧だけである）。
     * ただし**先頭が `#` の註記行は落とす**（変愚の mapping.csv で踏んだ穴）。
     */
    bool load(const std::string &csv_path, const std::string &root);

    //! v1 §8.2 の `asset_manifest`。`load()` が成功していれば行数ぶん入っている。
    const presentation::AssetManifestMessage &message() const { return this->message_; }

    /*!
     * @brief 地形 → 目録索引。
     * @param feat Sil-Q の feat 番号（`cave_feat` のミミック解決後）
     * @return 1..N、または 0（未登録）
     */
    std::uint16_t lookup_terrain(int feat) const;

    /*!
     * @brief 地形 → **立体（HD2D）へ送る地形番号**（`terrain_map.csv` の `voxel_id`）。
     * @param feat Sil-Q の feat 番号
     * @param fallback 表に無い feat のときに返す値
     *
     * @details 画面側は `terrain_id` で `assets/voxel/terrain_prefabs.jsonc` を引く。
     * あの表は**変愚の番号**でできているので、Sil-Q の feat をそのまま送ると
     * **同じ番号の別の地形**が出る（陽だまり 9 が変愚の `QUEST_EXIT` ＝上り階段に
     * 化けていたのがこれ。2026-08-22 に直した。35 件が当たっていた）。
     *
     * 表の `voxel_id` はふつう `heng_id`（＝2D の絵の倒し先）と同じで、Sil-Q だけの
     * 地形に専用のプレハブがあるときだけ 1300 番台の予約帯を指す。
     */
    std::uint16_t voxel_terrain(int feat, std::uint16_t fallback) const;

    /*!
     * @brief 実体の絵を目録へ足す（`load()` の**後**に呼ぶ。索引は続きから振る）。
     * @param tile_dir_rel `root` から見た絵の置き場（例 `silq\tilework\128`）
     * @param root `asset_manifest.root`
     * @return 足した枚数。置き場が無ければ 0（**失敗ではない**——絵が無い木でも動く）
     */
    int add_entities(const std::string &tile_dir_rel, const std::string &root);

    //! 敵 → 目録索引。`r_idx`（`monster.txt` の N）。0 = 絵が無い。
    std::uint16_t lookup_monster(int r_idx) const;
    //! 品 → 目録索引。`k_idx`（`object.txt` の N）。0 = 絵が無い。
    std::uint16_t lookup_object(int k_idx) const;
    //! @ → 目録索引。`prace`（`race.txt` の N）。0 = 絵が無い。
    std::uint16_t lookup_player(int race) const;

    //! 実体の枚数（R / K / P の合計。起動時に stderr へ出す）。
    int entity_count() const { return this->entity_count_; }

    //! 別名の件数（絵を分け合う品。`add_entities()` が付ける。起動時に stderr へ出す）。
    int alias_count() const { return this->alias_count_; }

    //! 絵の実体が無かった行の数（起動時に stderr へ数える。捏造しない）。
    int missing_files() const { return this->missing_files_; }
    const std::string &error() const { return this->error_; }

private:
    /*!
     * @brief 絵を分け合う区間（`kSharedObjectRuns`）を `object_index_` と `aliases[]` へ写す。
     * @details `add_entities()` の**最後**に呼ぶ。代表の絵が無ければ何もしない
     * （区間ごと索引 0 のまま。**嘘の索引を返さない**）。
     */
    void apply_shared_runs();

    presentation::AssetManifestMessage message_;
    std::unordered_map<int, std::uint16_t> index_;
    std::unordered_map<int, std::uint16_t> voxel_index_; //!< feat → 立体の地形番号
    std::unordered_map<int, std::uint16_t> monster_index_;
    std::unordered_map<int, std::uint16_t> object_index_;
    std::unordered_map<int, std::uint16_t> player_index_;
    int entity_count_{ 0 };
    int alias_count_{ 0 };
    int missing_files_{ 0 };
    std::string error_;
};

} // namespace sq
