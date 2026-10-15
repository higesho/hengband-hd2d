/*!
 * @file an_manifest.h
 * @brief `tilework/tile_mapping.csv` の F/R/K 行を名前で引く目録（設計 §6.2）。
 *
 * @details Angband の地形・種族・アイテムの番号は変愚（このリポジトリの `src/`）とは無関係な、
 * まったく別の採番である。画面側の立体・タイルは**変愚の番号**（`tilework/tile_mapping.csv`
 * の `id` 列。原作は `presentation_bridge.cpp` が `enum2i(mon.get_monrace_id())` /
 * `bi_id` をそのまま `cell.monster_id` / `cell.object_id` に積んでいる）で引くので、
 * Angband の名前を**名前で**変愚の番号へ対応づけてから送る（Sil-Q・Frox と同じ要領。
 * 手本は `tools/silq/` の名寄せの流儀）。
 *
 * ## `tile_index` は別の採番（S1 の見直しで追加）
 * `cell.terrain_id`（このファイルの `id`）は**変愚の番号**で、3D のプレハブ
 * （`terrain_prefabs.jsonc`）を引く。`cell.tile_index` は**この目録が自分で振った通し番号**
 * （`asset_manifest.assets[].index`）で、2D の絵（`path`）を引く。2 つは別物であり、
 * どちらか一方だけでは 2D も 3D も揃わない（`hd2d/assets/tile_catalog.h` の `TileEntry`
 * と `hd2d/world/terrain_view.cpp` の `PrefabLibrary::rule_for_terrain` を参照）。
 */
#pragma once

#include "bridge/unaware_item_tiles.h"
#include "frame/protocol_messages.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace an {

//! 名前の正規化（設計 §6.2「空白と記号を除き小文字、Armour→Armor、アクセントを落とす」）。
std::string normalize_name(const std::string &name);

//! 名前で引いた結果（変愚の番号と、この目録の中の通し番号の両方）。
struct TileLookup {
    std::uint16_t id{ 0 }; //!< 変愚の番号（`cell.terrain_id` / `monster_id` / `object_id`）
    std::uint16_t tile_index{ 0 }; //!< この目録の通し番号（`cell.tile_index`）。0 = 未登録
};

class TileMapping {
public:
    /*!
     * @brief 読み込む。**`asset_manifest` もここで組む**（設計 §6.2 の「目録」）。
     * @param csv_path `<exe_dir>/tilework/tile_mapping.csv`（変愚・共有）
     * @param asset_root `asset_manifest.root`（= exe のあるディレクトリの絶対パス）
     * @param baseitem_jsonc_path `<exe_dir>/lib/edit/BaseitemDefinitions.jsonc`。
     * K 行の `id` をこの jsonc の `id` で引き、`itemkind.type_value`（分類）を
     * 対応づけるのに使う（1a 直し。省略すると分類ぬきの名前だけの照合になる）。
     * @param extra_csv_path `<exe_dir>/angband/tilework/mapping.csv`（M2。
     * 変愚の絵を流用できない Angband 固有の実体。同じ 16 列の形式で、
     * `csv_path` を読み終えたあとへ**継ぎ足す**——`tile_index` の通し番号は
     * 地形＋変愚の実体のぶんから続くので、2 つの目録を分けて持たない
     * （の「表を 2 つ持たない」と同じ理由）。
     * 空文字列 = 無し（Angband 固有の絵をまだ用意していない状態でも動く）。
     * @return 読めたら真。読めなければ偽（`error()` に理由）。`extra_csv_path`
     * が開けないだけなら致命にしない（無ければ無いで、変愚の絵だけで動く）。
     */
    bool load(const std::string &csv_path, const std::string &asset_root,
        const std::string &baseitem_jsonc_path = std::string(),
        const std::string &extra_csv_path = std::string());

    const std::string &error() const { return this->error_; }
    int terrain_count() const { return static_cast<int>(this->terrain_by_name_.size()); }
    int monster_count() const { return static_cast<int>(this->monster_by_name_.size()); }
    int object_count() const { return static_cast<int>(this->object_by_name_.size()); }

    //! v1 §8.2 の `asset_manifest`。`load()` が成功していれば行数ぶん入っている。
    const presentation::AssetManifestMessage &message() const { return this->message_; }

    /*!
     * @brief 地形名 → 変愚の番号・目録の通し番号。
     * @details 一致しない既知の 6 件（設計 §6.2）は当て込みの表で手当てしてある。
     * @return `id`/`tile_index` とも 0 = 未登録（画面側は字のボクセル板へ落とす）
     */
    TileLookup lookup_terrain(const std::string &angband_name) const;
    TileLookup lookup_monster(const std::string &angband_name) const;
    /*!
     * @brief アイテムの種類名 → 変愚の番号・目録の通し番号（K 行）。
     * 見直しの指摘 1/14（旧「品」→「アイテム」）。
     * @param angband_tval Angband の生の tval（`an_cell_info::object_tval`。
     * `list-tvals.h` の `TV_*` の値そのもの）。**規則の訂正**（本スレッドの判断。
     * 2026-09-23）: 以前は `an_item_category`（8 分類だけ）が一致しないときに
     * 名前だけの索引へ**黙って落ちていた**（1b 直し）が、これが取り違えの原因
     * だった——`Speed`・`Light`・`Healing` のような同名で分類だけ違う組（Angband
     * 実測 30 組以上）が、フォールバックのせいで**別の分類の絵**を掴んでいた
     * （例: 速度の薬がリングの絵を借りる）。**いまは tval が変愚の `type_value` へ
     * 対応づけられる限り、名前だけの索引へは絶対に落ちない**——対応する
     * `type_value` の行が無ければ「無い」を返す（呼び出し側は字の板へ落ちる。
     * 捏造しない）。tval が対応表に無い（0 = `TV_NONE` など、実際には起こらない
     * はずの値）ときだけ、これまでどおり名前だけの索引を使う。
     */
    TileLookup lookup_object(const std::string &angband_name, int angband_tval = 0) const;
    //! プレイヤの種族名 → 変愚の P 行。一致しなければ 0 で、呼び出し側が字・色へ落とす。
    TileLookup lookup_player(const std::string &angband_race_name) const;

    /*!
     * @brief 正体の分からないアイテムの代表タイル。
     * @details 分類 → 変愚の K 番号は `presentation::unaware_item_representative_bi_id()`
     * （共有の `presentation/bridge/unaware_item_tiles.h`）の 1 か所だけにある。ここでは
     * その K 番号を、この目録が `load()` で振った**通し番号**（`tile_index`）へ変換するだけ
     * （新しい絵は登録しない。CSV に既に在る K 行の tile_index をそのまま返す）。
     * @return 目録の通し番号。0 = 見つからない（呼び出し側は種類の板へ落としてよい。契約 §2 の 7）
     */
    std::uint16_t lookup_unaware_representative(presentation::UnawareItemCategory category) const;

private:
    //! `load()` の共通処理（M2 で 2 本の CSV を継ぎ足すために切り出した）。
    //! 定義は an_manifest.cpp を参照。
    bool load_rows_from(const std::string &csv_path,
        const std::unordered_map<long, int> &id_to_type_value, bool have_type_values,
        std::uint16_t &next_index);

    std::unordered_map<std::string, TileLookup> terrain_by_name_;
    std::unordered_map<std::string, TileLookup> monster_by_name_;
    std::unordered_map<std::string, TileLookup> object_by_name_;
    //! 名前＋変愚の `type_value` の組で引く索引（1b 直し）。キーは
    //! `normalize_name(name) + "|" + std::to_string(type_value)`。
    std::unordered_map<std::string, TileLookup> object_by_name_and_type_;
    std::unordered_map<std::string, TileLookup> player_by_name_;
    //! 変愚の K 番号（bi_id）→ この目録の通し番号（`tile_index`）。
    //! `lookup_unaware_representative()`専用の逆引き（1 直し）。
    std::unordered_map<std::uint16_t, std::uint16_t> object_tile_index_by_bi_id_;
    presentation::AssetManifestMessage message_;
    std::string error_;
};

} // namespace an
