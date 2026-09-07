/*!
 * @file fc_manifest.cpp
 * @brief `fc_manifest.h` の実装。
 *
 * ## 目録の形（v1 §8.2）
 * 1 行 = `{index, kind, id, path}`。`index` は**この表の中だけで通じる番号**で、
 * 画面側は `index` → `path` の表を作り、フレームの `tile_index` でそこを引く。
 * だから **`tile_index` を出す側（`fc_frame`）と同じ表**を使わなければならない。
 */

#include "fc_manifest.h"

#include "portable/legacy_os.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace fc {

namespace {

/*!
 * @brief RFC4180 の 1 行を列へ割る。
 * @details 規則は 3 つだけ:
 * - 引用の外の `,` が区切り
 * - `"` で始まる列は次の `"` まで（`""` は `"` そのもの）
 * - 引用の外の `"` は普通の文字
 *
 * 註記の欄に `,` が入る行があるので、素の `split(',')` では割れない。
 */
std::vector<std::string> split_csv_line(const std::string &line)
{
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    std::size_t i = 0;

    while (i < line.size()) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (((i + 1) < line.size()) && (line[i + 1] == '"')) {
                    field.push_back('"');
                    i += 2;
                    continue;
                }
                quoted = false;
                ++i;
                continue;
            }
            field.push_back(c);
            ++i;
            continue;
        }
        if (c == '"') {
            if (field.empty()) {
                quoted = true;
            } else {
                field.push_back(c);
            }
            ++i;
            continue;
        }
        if (c == ',') {
            out.push_back(field);
            field.clear();
            ++i;
            continue;
        }
        field.push_back(c);
        ++i;
    }
    out.push_back(field);
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
    this->entity_index_.clear();
    this->missing_files_ = 0;
    this->error_.clear();

    std::ifstream in(csv_path, std::ios::binary);
    if (!in) {
        this->error_ = "cannot open " + csv_path;
        return false;
    }

    this->message_.root = root;

    std::string line;
    int next_index = 1;
    while (std::getline(in, line)) {
        while (!line.empty() && ((line.back() == '\r') || (line.back() == '\n'))) {
            line.pop_back();
        }
        if (line.empty() || (line[0] == '#')) {
            continue; // 註記行
        }
        if (line.rfind("feat,", 0) == 0) {
            continue; // 見出し行
        }

        //! 欄の並びは `feat,name,symbol,heng_id,voxel_id,path,note`。
        const std::vector<std::string> cells = split_csv_line(line);
        if (cells.size() < 6) {
            continue;
        }

        int feat = 0;
        try {
            feat = std::stoi(cells[0]);
        } catch (const std::exception &) {
            continue; // 数でない行は捨てる
        }

        const std::string &path = cells[5];
        if (path.empty()) {
            continue;
        }

        int voxel_id = 0;
        try {
            voxel_id = std::stoi(cells[4]);
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

bool TileManifest::load_entities(const std::string &csv_path, const std::string &root)
{
    /*
     * **地形のうしろへ継ぎ足す。**`message_` は消さない——`tile_index` は
     * 地形と実体を通した 1 本の番号で、ここで作り直すと地形側の索引がずれる。
     */
    std::ifstream in(csv_path, std::ios::binary);
    if (!in) {
        this->error_ = "cannot open " + csv_path;
        return false;
    }

    const int first_index = static_cast<int>(this->message_.assets.size()) + 1;
    int next_index = first_index;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && ((line.back() == '\r') || (line.back() == '\n'))) {
            line.pop_back();
        }
        if (line.empty() || (line[0] == '#')) {
            continue; // 註記行
        }
        if (line.rfind("kind,", 0) == 0) {
            continue; // 見出し行
        }

        //! 欄の並びは `kind,fc_id,source,value`。
        const std::vector<std::string> cells = split_csv_line(line);
        if (cells.size() < 4) {
            continue;
        }
        if (cells[0].size() != 1) {
            continue;
        }
        const char kind = cells[0][0];
        if ((kind != 'R') && (kind != 'K') && (kind != 'P')) {
            continue;
        }
        if (cells[2] != "tile") {
            continue; // 字の板は画面側が `ascii_fallback` から立てる（目録に載せない）
        }
        const std::string &path = cells[3];
        if (path.empty()) {
            continue;
        }

        int id = 0;
        try {
            id = std::stoi(cells[1]);
        } catch (const std::exception &) {
            continue; // 数でない行は捨てる
        }
        if ((id < 0) || (id > 0xFFFF)) {
            continue;
        }

        presentation::AssetManifestWireEntry wire;
        wire.index = next_index;
        wire.kind = std::string(1, kind);
        wire.id = id;
        wire.path = path;
        this->message_.assets.push_back(std::move(wire));
        const std::uint32_t key = (static_cast<std::uint32_t>(static_cast<unsigned char>(kind)) << 24)
            | static_cast<std::uint32_t>(id);
        this->entity_index_[key] = static_cast<std::uint16_t>(next_index);
        ++next_index;

        if (!file_exists(join_path(root, path))) {
            ++this->missing_files_;
        }
    }

    if (next_index == first_index) {
        this->error_ = "no entity rows in " + csv_path;
        return false;
    }
    return true;
}

std::uint16_t TileManifest::lookup_entity(char kind, int id) const
{
    if ((id < 0) || (id > 0xFFFF)) {
        return 0;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(static_cast<unsigned char>(kind)) << 24)
        | static_cast<std::uint32_t>(id);
    const auto it = this->entity_index_.find(key);
    return (it == this->entity_index_.end()) ? static_cast<std::uint16_t>(0) : it->second;
}

std::uint16_t TileManifest::lookup_terrain(int feat) const
{
    if (feat < 0) {
        return 0;
    }
    const auto it = this->index_.find(feat);
    return (it == this->index_.end()) ? static_cast<std::uint16_t>(0) : it->second;
}

std::uint16_t TileManifest::voxel_terrain(int feat, std::uint16_t fallback) const
{
    if (feat < 0) {
        return fallback;
    }
    const auto it = this->voxel_index_.find(feat);
    /*
     * **表に無い feat は素通し**（`fallback` = 送り手の feat）。表は f_info の
     * 全 188 種を持っているので、ここへ落ちるのは上流が地形を足したときだけ
     * ——そのときは 221 までと同じ「変愚と同じ番号」の推定でしのぎ、
     * `--selftest` の全数照合が次の取り込みで気づかせる。
     */
    return (it == this->voxel_index_.end()) ? fallback : it->second;
}

} // namespace fc
