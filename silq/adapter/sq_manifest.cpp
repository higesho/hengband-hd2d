/*!
 * @file sq_manifest.cpp
 * @brief `sq_manifest.h` の実装。
 *
 * ## 目録の形（v1 §8.2）
 * 1 行 = `{index, kind, id, path}`。`index` は**この表の中だけで通じる番号**で、
 * 画面側は `index` → `path` の表を作り、フレームの `tile_index` でそこを引く。
 * だから **`tile_index` を出す側（`sq_frame`）と同じ表**を使わなければならない。
 */

#include "sq_manifest.h"

#include "portable/legacy_os.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sq {

namespace {

/*!
 * @brief 絵を分け合う品の連番。`first` の絵を `first`〜`last` の全部で使う。
 *
 * @details ここに挙げてよいのは「**見た目が同じで、中身だけが違う**」区間だけである。
 * 似ているというだけで寄せると、遊び手が別の品だと見分けられなくなる。
 */
struct SharedRun {
    int first; //!< 代表（この番号の絵を焼く）
    int last; //!< ここまで同じ絵を配る
};

/*!
 * チュートリアルの覚え書き（`TV_NOTE` = `defines.h:1082`。`object.txt` の 451〜490）。
 * **40 個とも名前は `& Note~`・記号は `~`** で、違うのは `D:` に書いてある案内文
 * だけである（「まず扉を開けてみよ」など）。紙の絵は 1 枚で足りる。
 *
 * 焼く側にも対がある——`tools/silq/gen_silq_tiles.py` の `records()` が
 * **451 だけを焼き、452〜490 を飛ばす**。片方を直したら両方直すこと。
 *
 * @note 寄せないと「絵の無い品」になり、画面側が実体と見て板を探しに行って外れ、
 * **何も描かれないまま `警告: タイル欠け` が出る**（`sq_frame.cpp` の
 * `resolve_tile_index()` が地形の索引へ落ちるため）。チュートリアルの床の `~` が
 * まるごと消えていたのがこれである。
 */
constexpr SharedRun kSharedObjectRuns[] = {
    { 451, 490 },
};

//! 1 行を `,` で割る（引用符は扱わない。`sq_manifest.h` の説明）。
std::vector<std::string> split_row(const std::string &line)
{
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ',')) {
        out.push_back(field);
    }
    return out;
}

bool file_exists(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    // ディレクトリを弾く必要があるので `dir_exists` の否定では足りない。
    if (portable::dir_exists(path)) {
        return false;
    }
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return false;
    }
    std::fclose(fp);
    return true;
}

std::string join_path(const std::string &root, const std::string &rel)
{
    if (root.empty()) {
        return rel;
    }
    std::string out = root;
    if ((out.back() != '\\') && (out.back() != '/')) {
        out.push_back(portable::kPathSep);
    }
    out += rel;
    return out;
}

} // namespace

bool TileManifest::load(const std::string &csv_path, const std::string &root)
{
    this->message_ = presentation::AssetManifestMessage();
    this->index_.clear();
    this->voxel_index_.clear();
    this->missing_files_ = 0;
    this->error_.clear();

    std::ifstream in(csv_path, std::ios::binary);
    if (!in) {
        this->error_ = "cannot open " + csv_path;
        return false;
    }

    this->message_.root = root;

    std::string line;
    bool header_seen = false;
    int next_index = 1;
    while (std::getline(in, line)) {
        /* 行末の CR を落とす（この表は CRLF で書かれる）。 */
        while (!line.empty() && ((line.back() == '\r') || (line.back() == '\n'))) {
            line.pop_back();
        }
        if (line.empty() || (line[0] == '#')) {
            continue; // 註記行
        }
        if (!header_seen) {
            header_seen = true;
            if (line.rfind("feat,", 0) == 0) {
                continue; // 見出し行
            }
            /* 見出しが無い表も受ける（人が削ったとき）。落ちずに読み進む。 */
        }

        const std::vector<std::string> cells = split_row(line);
        if (cells.size() < 5) {
            continue;
        }

        int feat = 0;
        try {
            feat = std::stoi(cells[0]);
        } catch (const std::exception &) {
            continue; // 数でない行は捨てる
        }
        /*
         * 欄の並びは `feat,name,glyph,heng_id,voxel_id,path,note`（2026-08-22 に
         * `voxel_id` が増えた）。**古い並び（`voxel_id` の無い 6 欄）も受ける**
         * ——配り物の表が 1 版古いだけで絵がまるごと消えるのは割に合わない。
         * 見分けは「5 番目の欄が数字だけか」で付く（新しい表は必ず数、古い表は絵の道）。
         */
        const bool has_voxel_id = (cells.size() >= 6)
            && !cells[4].empty()
            && (cells[4].find_first_not_of("0123456789") == std::string::npos);
        const std::string &path = has_voxel_id ? cells[5] : cells[4];
        if (path.empty()) {
            continue;
        }
        /*
         * 立体へ送る地形番号（`sq_frame` の `cell.terrain_id`）。無い表では
         * `heng_id` へ落ちる——それが `voxel_id` の既定でもある。
         */
        int voxel_id = 0;
        try {
            voxel_id = std::stoi(has_voxel_id ? cells[4] : cells[3]);
        } catch (const std::exception &) {
            voxel_id = 0;
        }

        presentation::AssetManifestWireEntry wire;
        wire.index = next_index;
        wire.kind = "F";
        wire.id = feat;
        wire.path = path;
        this->message_.assets.push_back(std::move(wire));
        this->index_[feat] = static_cast<std::uint16_t>(next_index);
        if (voxel_id > 0) {
            this->voxel_index_[feat] = static_cast<std::uint16_t>(voxel_id);
        }
        ++next_index;

        if (!file_exists(join_path(root, path))) {
            ++this->missing_files_;
        }
    }

    if (this->message_.assets.empty()) {
        this->error_ = "no rows in " + csv_path;
        return false;
    }

    return true;
}

int TileManifest::add_entities(const std::string &tile_dir_rel, const std::string &root)
{
    const std::string dir = join_path(root, tile_dir_rel);
    /*
     * かつては `FindFirstFileA(".../S*.png")` の一発だった。平台を跨ぐために
     * **並べてから絞る**形へ替えた（`S` で始まる縛りは下の名前検査に足してある）。
     */
    std::vector<std::string> names = portable::list_files(dir);
    // A prebuilt voxel is sufficient for gameplay when the PNG is omitted.
    // Keep the same logical PNG names and ordering in the protocol manifest.
    const auto tile_part = tile_dir_rel.find("/tilework/");
    if (tile_part != std::string::npos) {
        const auto slab_dir = join_path(root, tile_dir_rel.substr(0, tile_part) + "/assets/slab");
        for (const auto &name : portable::list_files(slab_dir)) {
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".vox") == 0)
                names.push_back(name.substr(0, name.size() - 4) + ".png");
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    if (names.empty()) {
        return 0; // 絵がまだ無い木。**失敗ではない**（アスキー実体で遊べる）
    }

    int added = 0;
    auto next_index = static_cast<int>(this->message_.assets.size()) + 1;
    for (const std::string &name : names) {
        /* `SR11.png` / `SK240.png` / `SP0.png` の形だけを採る。 */
        if (name.empty() || (name[0] != 'S')) {
            continue;
        }
        if (name.size() < 7 || name.compare(name.size() - 4, 4, ".png") != 0) {
            continue;
        }
        const char kind = name[1];
        if ((kind != 'R') && (kind != 'K') && (kind != 'P')) {
            continue;
        }
        const std::string digits = name.substr(2, name.size() - 6);
        if (digits.empty() || (digits.find_first_not_of("0123456789") != std::string::npos)) {
            continue;
        }
        const int id = std::stoi(digits);
        /*
         * 家（`SP1000` 以降）は @ の絵ではない——**誕生画面の意匠**で、地図には出ない。
         * 目録へ入れると `lookup_player(race)` と番号が衝突するので採らない。
         */
        if ((kind == 'P') && (id >= 1000)) {
            continue;
        }

        presentation::AssetManifestWireEntry wire;
        wire.index = next_index;
        wire.kind = std::string(1, kind);
        wire.id = id;
        wire.path = tile_dir_rel + "/" + name;
        this->message_.assets.push_back(std::move(wire));

        const auto slot = static_cast<std::uint16_t>(next_index);
        if (kind == 'R') {
            this->monster_index_[id] = slot;
        } else if (kind == 'K') {
            this->object_index_[id] = slot;
        } else {
            this->player_index_[id] = slot;
        }
        ++next_index;
        ++added;
    }

    this->entity_count_ = added;
    this->apply_shared_runs();
    return added;
}

void TileManifest::apply_shared_runs()
{
    this->alias_count_ = 0;
    for (const SharedRun &run : kSharedObjectRuns) {
        const auto shared = this->object_index_.find(run.first);
        if (shared == this->object_index_.end()) {
            /*
             * 代表の絵がまだ無い。**区間ごと索引 0 のまま**にする——ここで隣の絵を
             * 当てにいくと「別の品の絵が出る」になり、無いより悪い（捏造しない）。
             */
            continue;
        }
        for (int id = run.first + 1; id <= run.last; ++id) {
            if (this->object_index_.find(id) != this->object_index_.end()) {
                continue; //!< その番号だけの絵を後から焼いたなら、**そちらが勝つ**
            }
            this->object_index_[id] = shared->second;

            /*
             * 目録にも載せる。`aliases[]` は**新しい索引を作らない**ので、画面側の
             * 目録の件数は増えない（`tile_catalog.cpp` は共有元が在ることだけ確かめる）。
             * 引きに使うのは上の `object_index_` のほうで、こちらは申告である。
             */
            presentation::AssetManifestAliasWireEntry alias;
            alias.kind = "K";
            alias.id = id;
            alias.index = static_cast<int>(shared->second);
            this->message_.aliases.push_back(std::move(alias));
            ++this->alias_count_;
        }
    }
}

namespace {
//! 正体の分からないアイテムの代表タイルに割り当てる id の起点（`object_index_` のキー）。
//! Sil-Q 自身の k_idx（`object.txt` の N）とはぶつからない帯を選んである。
constexpr int kUnawareRepresentativeIdBase = 60000; // + 1..8 (UNAWARE_CATEGORY_*) = 60001..60008
} // namespace

void TileManifest::add_unaware_representative_tiles()
{
    /*
     * 分類 → 変愚の K の番号は
     * `presentation::unaware_item_representative_bi_id()`（`unaware_item_tiles.h`）の
     * 1 か所だけにあり、ここは新しい絵を作らずその番号の板（`tilework/sfc/K<id>.png`）を
     * 目録へ追加登録するだけ。Sil-Q 自身の板（`silq/tilework/128/SK*.png`）は別の絵なので
     * 触らない——画面側の `SlabLibrary::acquire()` が
     * ［申告された置き場（`silq/assets/slab`）→ 既定の置き場（`assets/voxel/slab`）］の
     * 2 段で探し、申告された置き場に無い名前（`K229` 等）は既定へ落ちる。
     */
    constexpr presentation::UnawareItemCategory kAllCategories[] = {
        presentation::UnawareItemCategory::Potion,
        presentation::UnawareItemCategory::Scroll,
        presentation::UnawareItemCategory::Ring,
        presentation::UnawareItemCategory::Amulet,
        presentation::UnawareItemCategory::Wand,
        presentation::UnawareItemCategory::Staff,
        presentation::UnawareItemCategory::Rod,
        presentation::UnawareItemCategory::Mushroom,
    };

    int next_index = static_cast<int>(this->message_.assets.size()) + 1;
    for (const auto category : kAllCategories) {
        const auto bi_id = presentation::unaware_item_representative_bi_id(category);
        char path[64]{};
        std::snprintf(path, sizeof(path), "tilework/sfc/K%u.png", static_cast<unsigned>(bi_id));

        presentation::AssetManifestWireEntry wire;
        wire.index = next_index;
        wire.kind = "K";
        wire.id = kUnawareRepresentativeIdBase + static_cast<int>(category);
        wire.path = path;
        this->message_.assets.push_back(wire);

        this->object_index_[wire.id] = static_cast<std::uint16_t>(next_index);
        ++next_index;

        /* 2D の絵（PNG）が無くても実害は無い（HD2D は .vox しか使わない）。 */
        if (!file_exists(join_path(this->message_.root, path))) {
            ++this->missing_files_;
        }
    }
}

std::uint16_t TileManifest::lookup_unaware_representative(presentation::UnawareItemCategory category) const
{
    return this->lookup_object(kUnawareRepresentativeIdBase + static_cast<int>(category));
}

std::uint16_t TileManifest::lookup_terrain(int feat) const
{
    const auto it = this->index_.find(feat);
    return (it == this->index_.end()) ? 0 : it->second;
}

std::uint16_t TileManifest::voxel_terrain(int feat, std::uint16_t fallback) const
{
    const auto it = this->voxel_index_.find(feat);
    return (it == this->voxel_index_.end()) ? fallback : it->second;
}

std::uint16_t TileManifest::lookup_monster(int r_idx) const
{
    const auto it = this->monster_index_.find(r_idx);
    return (it == this->monster_index_.end()) ? 0 : it->second;
}

std::uint16_t TileManifest::lookup_object(int k_idx) const
{
    const auto it = this->object_index_.find(k_idx);
    return (it == this->object_index_.end()) ? 0 : it->second;
}

std::uint16_t TileManifest::lookup_player(int race) const
{
    const auto it = this->player_index_.find(race);
    return (it == this->player_index_.end()) ? 0 : it->second;
}

} // namespace sq
