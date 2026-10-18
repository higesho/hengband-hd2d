/*!
 * @file tb_manifest.cpp
 * @brief `tb_manifest.h` の実装。mapping.csv の読み取りと目録の組み立て。
 *
 * ## RFC4180 を守る理由（設計 §5.1）
 * 記号そのものが値である列があり、そこに `,` と `"` が実在する:
 * `R,962,ascii,",",5` ／ `K,150,ascii,"""",9`。素の `split(',')` で割ると
 * **その行だけ列がずれて別の絵になる**。しかも 2,543 行のうち数行なので、
 * 目で見て気づけない。ここは引用に対応した読み取りを書く。
 *
 * ## 索引と目録は 1 本の読み込みから作る
 * `tile_index` は「この表の行順で 1 から機械的に」（§5.1）。だから
 * `asset_manifest` に積む順と索引の番号は**同じループの中で**決める。
 */

#include "tb_manifest.h"

#include <cstdio>
#include <fstream>
#include "portable/legacy_os.h"

namespace tb {

namespace {

//! ascii 行の絵の道は規約で決まる（設計 §5.1）: `GA{文字コード10進}_{色}.png`。
constexpr const char *kAsciiDir = "toband/tilework/ascii/";

/*!
 * @brief RFC4180 の 1 行を列へ割る。
 * @details 規則は 3 つだけ:
 * - 引用の外の `,` が区切り
 * - `"` で始まる列は次の `"` まで（`""` は `"` そのもの）
 * - 引用の外の `"` は普通の文字（この表には出ないが、落として捨てない）
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
            /* 列の頭の `"` だけが引用の開始。途中の `"` は文字として置く。 */
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

//! 末尾の CR と両端の空白を落とす（行末の改行は `getline` が食べ残す）。
std::string trim(const std::string &s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while ((b < e) && ((s[b] == ' ') || (s[b] == '\t'))) {
        ++b;
    }
    while ((e > b) && ((s[e - 1] == ' ') || (s[e - 1] == '\t') || (s[e - 1] == '\r') || (s[e - 1] == '\n'))) {
        --e;
    }
    return s.substr(b, e - b);
}

bool file_exists(const std::string &path)
{
    // `_stat` は MSVC の名。平台を跨ぐので開いて確かめる（呼ぶ回数は目録の行数だけ）。
    if (path.empty()) {
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

/*!
 * 正体の分からないアイテムの代表タイルに割り当てる id の起点（`tb::TileManifest::kind='K'`）。
 * 幻想蛮怒自身の k_idx（mapping.csv 実測で最大 924）とはぶつからない帯を選んである。
 */
constexpr int kUnawareRepresentativeIdBase = 9000; // + 1..8 (UNAWARE_CATEGORY_*) = 9001..9008

} // namespace

bool TileManifest::load(const std::string &csv_path, const std::string &root)
{
    this->message_ = presentation::AssetManifestMessage{};
    this->index_.clear();
    this->terrain_alias_.clear();
    this->missing_files_ = 0;
    this->error_.clear();

    std::ifstream file(csv_path, std::ios::binary);
    if (!file) {
        this->error_ = "cannot open " + csv_path;
        return false;
    }

    this->message_.root = root;

    std::string raw;
    int line_no = 0;
    int index = 0;
    while (std::getline(file, raw)) {
        ++line_no;
        const std::string line = trim(raw);
        if (line.empty() || (line[0] == '#')) {
            continue;
        }

        const std::vector<std::string> col = split_csv_line(line);
        if (col.size() < 4) {
            continue;
        }
        if (col[0] == "kind") {
            continue; // 見出し行
        }

        const std::string &kind = col[0];
        if ((kind.size() != 1) || ((kind[0] != 'P') && (kind[0] != 'R') && (kind[0] != 'K') && (kind[0] != 'F'))) {
            std::fprintf(stderr, "[toband] mapping.csv:%d unknown kind \"%s\"\n", line_no, kind.c_str());
            continue;
        }

        int id = 0;
        try {
            id = std::stoi(col[1]);
        } catch (...) {
            std::fprintf(stderr, "[toband] mapping.csv:%d bad id \"%s\"\n", line_no, col[1].c_str());
            continue;
        }

        std::string rel;
        if (col[2] == "tile") {
            /* 変愚の既存の絵。道はリポジトリ直下相対のまま（root が exe の場所）。 */
            rel = col[3];
        } else if (col[2] == "ascii") {
            if (col[3].empty()) {
                std::fprintf(stderr, "[toband] mapping.csv:%d ascii row has no character\n", line_no);
                continue;
            }
            int color = 1;
            if ((col.size() > 4) && !col[4].empty()) {
                try {
                    color = std::stoi(col[4]);
                } catch (...) {
                    color = 1;
                }
            }
            char buf[128]{};
            std::snprintf(buf, sizeof(buf), "%sGA%d_%d.png", kAsciiDir,
                static_cast<int>(static_cast<unsigned char>(col[3][0])), color);
            rel = buf;
        } else {
            std::fprintf(stderr, "[toband] mapping.csv:%d unknown source \"%s\"\n", line_no, col[2].c_str());
            continue;
        }

        if (rel.empty()) {
            continue;
        }

        /*
         * **行順で 1 から採番する**（§5.1）。0 は「未登録」の意味に取ってあるので使わない。
         * 65535 を超える表は v1 の `tile_index`（u16）に載らないので、そこで打ち切る。
         */
        if (index >= 65535) {
            std::fprintf(stderr, "[toband] mapping.csv has more than 65535 rows; the rest is dropped\n");
            break;
        }
        ++index;

        presentation::AssetManifestWireEntry entry;
        entry.index = index;
        entry.kind = kind;
        entry.id = id;
        entry.path = rel;
        this->message_.assets.push_back(std::move(entry));

        const auto key = (static_cast<std::uint32_t>(static_cast<unsigned char>(kind[0])) << 24)
            | (static_cast<std::uint32_t>(id) & 0x00FFFFFFu);
        /* 同じ (kind,id) が 2 度出たら**先に出たほうを正**にする（行順が索引だから）。 */
        this->index_.emplace(key, static_cast<std::uint16_t>(index));

        /* F 行の 6 列目 `hb_id`（変愚の地形番号）。空なら素通しなので表に入れない。 */
        if ((kind[0] == 'F') && (col.size() > 5) && !col[5].empty()) {
            try {
                const int hb_id = std::stoi(col[5]);
                if ((id >= 0) && (id <= 65535) && (hb_id >= 0) && (hb_id <= 65535)) {
                    this->terrain_alias_.emplace(static_cast<std::uint16_t>(id), static_cast<std::uint16_t>(hb_id));
                }
            } catch (...) {
                std::fprintf(stderr, "[toband] mapping.csv:%d bad hb_id \"%s\"\n", line_no, col[5].c_str());
            }
        }

        if (!file_exists(join_path(root, rel))) {
            ++this->missing_files_;
        }
    }

    if (this->message_.assets.empty()) {
        this->error_ = "no usable rows in " + csv_path;
        return false;
    }
    return true;
}

void TileManifest::add_unaware_representative_tiles()
{
    /*
     * TOband は絵を流用しない決まり（ 
     * T4）なので、代表は変愚の K の板ではなく **TOband の新しい絵**（`toband/tilework/64/TU<分類>.png` →
     * 板 `toband/assets/slab/TU<分類>.vox`。分類の番号は `UNAWARE_CATEGORY_*` の 1〜8）。
     * 絵は `tools/toband/import_art.py` が `toband-art` の `items/cut/U<分類>_s1.png` から作る
     * （決めたこと 2026-10-17「英語化以外の作業をすべて進めて」の残りの 1 つ）。
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

    int index = static_cast<int>(this->message_.assets.size());
    for (const auto category : kAllCategories) {
        if (index >= 65535) {
            std::fprintf(stderr, "[toband] unaware representative tiles: index exhausted\n");
            break;
        }
        ++index;

        char path[64]{};
        std::snprintf(path, sizeof(path), "toband/tilework/64/TU%d.png", static_cast<int>(category));

        presentation::AssetManifestWireEntry entry;
        entry.index = index;
        entry.kind = "K";
        entry.id = kUnawareRepresentativeIdBase + static_cast<int>(category);
        entry.path = path;
        this->message_.assets.push_back(entry);

        const auto key = (static_cast<std::uint32_t>(static_cast<unsigned char>('K')) << 24)
            | (static_cast<std::uint32_t>(entry.id) & 0x00FFFFFFu);
        this->index_.emplace(key, static_cast<std::uint16_t>(index));

        if (!file_exists(join_path(this->message_.root, path))) {
            ++this->missing_files_;
        }
    }
}

std::uint16_t TileManifest::lookup_unaware_representative(presentation::UnawareItemCategory category) const
{
    return this->lookup('K', kUnawareRepresentativeIdBase + static_cast<int>(category));
}

std::uint16_t TileManifest::terrain_alias(std::uint16_t tb_id) const
{
    const auto it = this->terrain_alias_.find(tb_id);
    return (it == this->terrain_alias_.end()) ? tb_id : it->second;
}

std::uint16_t TileManifest::lookup(char kind, int id) const
{
    if (id < 0) {
        return 0;
    }
    const auto key = (static_cast<std::uint32_t>(static_cast<unsigned char>(kind)) << 24)
        | (static_cast<std::uint32_t>(id) & 0x00FFFFFFu);
    const auto it = this->index_.find(key);
    return (it == this->index_.end()) ? static_cast<std::uint16_t>(0) : it->second;
}

} // namespace tb
