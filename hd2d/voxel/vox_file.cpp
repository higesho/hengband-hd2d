/*!
 * @file vox_file.cpp
 * @brief `vox_file.h` の実装。
 */
#include "voxel/vox_file.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace hd2d {

namespace {

//! 読みながら位置を進める小道具。**範囲外を読もうとしたら黙って失敗させる**。
class Reader {
public:
    Reader(const std::uint8_t *data, std::size_t size)
        : data_(data)
        , size_(size)
    {
    }

    bool ok() const { return this->ok_; }
    std::size_t left() const { return (this->pos_ < this->size_) ? (this->size_ - this->pos_) : 0; }

    std::int32_t i32()
    {
        if (this->left() < 4) {
            this->ok_ = false;
            return 0;
        }
        std::int32_t value = 0;
        std::memcpy(&value, this->data_ + this->pos_, 4);
        this->pos_ += 4;
        return value;
    }

    std::string tag()
    {
        if (this->left() < 4) {
            this->ok_ = false;
            return std::string();
        }
        std::string value(reinterpret_cast<const char *>(this->data_ + this->pos_), 4);
        this->pos_ += 4;
        return value;
    }

    std::string str()
    {
        const std::int32_t len = this->i32();
        if (!this->ok_ || (len < 0) || (static_cast<std::size_t>(len) > this->left())) {
            this->ok_ = false;
            return std::string();
        }
        std::string value(reinterpret_cast<const char *>(this->data_ + this->pos_), static_cast<std::size_t>(len));
        this->pos_ += static_cast<std::size_t>(len);
        return value;
    }

    //! `int32 個数` に続く（文字列, 文字列）の並び。
    std::map<std::string, std::string> dict()
    {
        std::map<std::string, std::string> out;
        const std::int32_t count = this->i32();
        if (!this->ok_ || (count < 0) || (count > 4096)) {
            this->ok_ = false;
            return out;
        }
        for (std::int32_t i = 0; i < count; ++i) {
            const std::string key = this->str();
            const std::string value = this->str();
            if (!this->ok_) {
                return out;
            }
            out[key] = value;
        }
        return out;
    }

    const std::uint8_t *bytes(std::size_t count)
    {
        if (this->left() < count) {
            this->ok_ = false;
            return nullptr;
        }
        const std::uint8_t *const at = this->data_ + this->pos_;
        this->pos_ += count;
        return at;
    }

    //! チャンクの「読み残し」を飛ばす。**引き算が負になる形（＝壊れたファイル）を弾く。**
    void skip_rest(std::size_t content_bytes, std::size_t consumed)
    {
        if (content_bytes < consumed) {
            this->ok_ = false;
            return;
        }
        this->skip(content_bytes - consumed);
    }

    void skip(std::size_t count)
    {
        if (this->left() < count) {
            this->ok_ = false;
            this->pos_ = this->size_;
            return;
        }
        this->pos_ += count;
    }

private:
    const std::uint8_t *data_{ nullptr };
    std::size_t size_{ 0 };
    std::size_t pos_{ 0 };
    bool ok_{ true };
};

//! `nTRN`。子は 1 つだけ（形式の決まり）。
struct TrnNode {
    std::string name;
    int child{ -1 };
    int translation[3]{};
    bool rotated{ false }; //!< `_r` が単位行列でない
};

struct GrpNode {
    std::vector<int> children;
};

struct ShpNode {
    int model_index{ -1 };
};

//! `_t` は `"x y z"`。読めなければ 0 のまま（形式違反ではあるが落とすほどではない）。
void parse_translation(const std::string &text, int out[3])
{
    (void)std::sscanf(text.c_str(), "%d %d %d", &out[0], &out[1], &out[2]);
}

/*!
 * @brief `_r` の 1 バイトが単位行列を表しているか。
 * @details bit0-1 = 第 1 行の非零の位置、bit2-3 = 第 2 行の非零の位置、bit4-6 = 符号。
 * 単位行列は「第 1 行が 0 番・第 2 行が 1 番・符号が全部正」＝ `0b0000100` = 4。
 * MagicaVoxel は回転していないノードにも `_r` を書かないことが多いので、
 * **無い＝単位行列**として扱う。
 */
bool is_identity_rotation(const std::string &text)
{
    if (text.empty()) {
        return true;
    }
    int value = 0;
    if (std::sscanf(text.c_str(), "%d", &value) != 1) {
        return false;
    }
    return value == 4;
}

} // namespace

const VoxPart *VoxFile::find_part(const std::string &name) const
{
    for (const auto &part : this->parts) {
        if (part.name == name) {
            return &part;
        }
    }
    return nullptr;
}

bool load_vox_file(const std::string &path, VoxFile &out, std::string &err)
{
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        err = "開けませんでした: " + path;
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    const long file_size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (file_size <= 8) {
        std::fclose(fp);
        err = "中身がありません: " + path;
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(file_size));
    const std::size_t read = std::fread(data.data(), 1, data.size(), fp);
    std::fclose(fp);
    if (read != data.size()) {
        err = "最後まで読めませんでした: " + path;
        return false;
    }

    Reader reader(data.data(), data.size());
    if (reader.tag() != "VOX ") {
        err = "`.vox` ではありません（先頭が \"VOX \" でない）: " + path;
        return false;
    }
    (void)reader.i32(); // 版。150 でも 200 でも中身の読み方は変わらない

    if (reader.tag() != "MAIN") {
        err = "MAIN チャンクがありません: " + path;
        return false;
    }
    const std::int32_t main_content = reader.i32();
    const std::int32_t main_children = reader.i32();
    if (!reader.ok() || (main_content < 0) || (main_children < 0)) {
        err = "MAIN チャンクの頭が壊れています: " + path;
        return false;
    }
    reader.skip(static_cast<std::size_t>(main_content));

    std::map<int, TrnNode> transforms;
    std::map<int, GrpNode> groups;
    std::map<int, ShpNode> shapes;
    int pending_size[3] = { 0, 0, 0 };
    bool has_palette = false;

    while (reader.ok() && (reader.left() >= 12)) {
        const std::string id = reader.tag();
        const std::int32_t content_size = reader.i32();
        const std::int32_t children_size = reader.i32();
        if (!reader.ok() || (content_size < 0) || (children_size < 0)) {
            err = "チャンクの頭が壊れています: " + path;
            return false;
        }
        const std::size_t content_bytes = static_cast<std::size_t>(content_size);

        if (id == "SIZE") {
            pending_size[0] = reader.i32();
            pending_size[1] = reader.i32();
            pending_size[2] = reader.i32();
            reader.skip_rest(content_bytes, 12);
        } else if ((id == "XYZI") || (id == "XYZ2")) {
            //! `XYZ2` は座標 16 ビットの拡張（一辺 256 超。`vox_file.h` の表）。
            const bool wide = (id == "XYZ2");
            const std::size_t stride = wide ? 8u : 4u;
            const std::int32_t count = reader.i32();
            if (!reader.ok() || (count < 0)) {
                err = id + " の個数が壊れています: " + path;
                return false;
            }
            if ((pending_size[0] <= 0) || (pending_size[1] <= 0) || (pending_size[2] <= 0)) {
                err = id + " の前に SIZE がありません: " + path;
                return false;
            }
            if (!wide) {
                /*
                 * **黙って折り返させない。**1 バイト座標では 256 までしか表せないので、
                 * SIZE がそれを超えていたらファイルのほうが壊れている（書いた側が
                 * 切り詰めた）。捨てると絵が重なって出るだけで、原因が分からなくなる。
                 */
                for (int k = 0; k < 3; ++k) {
                    if (pending_size[k] > kVoxClassicMaxSide) {
                        err = "一辺 " + std::to_string(pending_size[k]) + " の模型が XYZI（1 バイト座標）で書かれています。"
                              "256 を超える模型は XYZ2 で書いてください: " + path;
                        return false;
                    }
                }
            }
            const std::size_t cells = static_cast<std::size_t>(pending_size[0])
                * static_cast<std::size_t>(pending_size[1]) * static_cast<std::size_t>(pending_size[2]);
            if (cells > kVoxMaxVoxels) {
                err = "模型が大きすぎます（" + std::to_string(pending_size[0]) + "x" + std::to_string(pending_size[1])
                    + "x" + std::to_string(pending_size[2]) + " = " + std::to_string(cells) + " ボクセル）: " + path;
                return false;
            }
            VoxModel model;
            model.size[0] = pending_size[0];
            model.size[1] = pending_size[1];
            model.size[2] = pending_size[2];
            model.voxels.assign(cells, 0);
            const std::uint8_t *const raw = reader.bytes(static_cast<std::size_t>(count) * stride);
            if (raw == nullptr) {
                err = id + " が途中で切れています: " + path;
                return false;
            }
            for (std::int32_t i = 0; i < count; ++i) {
                const std::size_t at = static_cast<std::size_t>(i) * stride;
                const int x = wide ? (raw[at + 0] | (raw[at + 1] << 8)) : raw[at + 0];
                const int y = wide ? (raw[at + 2] | (raw[at + 3] << 8)) : raw[at + 1];
                const int z = wide ? (raw[at + 4] | (raw[at + 5] << 8)) : raw[at + 2];
                const std::uint8_t color = wide ? raw[at + 6] : raw[at + 3];
                if ((x >= model.size[0]) || (y >= model.size[1]) || (z >= model.size[2])) {
                    continue; // 範囲外は捨てる（壊れたファイルで落ちない）
                }
                model.voxels[static_cast<std::size_t>(x)
                    + (static_cast<std::size_t>(y) * static_cast<std::size_t>(model.size[0]))
                    + (static_cast<std::size_t>(z) * static_cast<std::size_t>(model.size[0]) * static_cast<std::size_t>(model.size[1]))]
                    = color;
            }
            out.models.push_back(std::move(model));
            reader.skip_rest(content_bytes, 4 + (static_cast<std::size_t>(count) * stride));
        } else if (id == "RGBA") {
            const std::uint8_t *const raw = reader.bytes(256 * 4);
            if (raw == nullptr) {
                err = "RGBA が途中で切れています: " + path;
                return false;
            }
            // 形式の決まり: 索引 i (1..255) の色は rgba[i - 1]。
            for (int i = 1; i < 256; ++i) {
                std::memcpy(out.palette[i], raw + ((i - 1) * 4), 4);
            }
            has_palette = true;
            reader.skip_rest(content_bytes, 256 * 4);
        } else if (id == "nTRN") {
            const int node_id = reader.i32();
            const auto attributes = reader.dict();
            TrnNode node;
            const auto name = attributes.find("_name");
            if (name != attributes.end()) {
                node.name = name->second;
            }
            node.child = reader.i32();
            (void)reader.i32(); // reserved（必ず -1）
            (void)reader.i32(); // layer
            const std::int32_t frames = reader.i32();
            for (std::int32_t i = 0; i < frames; ++i) {
                const auto frame = reader.dict();
                if (i != 0) {
                    continue; // 先頭フレームだけ見る
                }
                const auto t = frame.find("_t");
                if (t != frame.end()) {
                    parse_translation(t->second, node.translation);
                }
                const auto r = frame.find("_r");
                if ((r != frame.end()) && !is_identity_rotation(r->second)) {
                    node.rotated = true;
                }
            }
            if (!reader.ok()) {
                err = "nTRN が壊れています: " + path;
                return false;
            }
            transforms[node_id] = std::move(node);
        } else if (id == "nGRP") {
            const int node_id = reader.i32();
            (void)reader.dict();
            const std::int32_t count = reader.i32();
            GrpNode node;
            for (std::int32_t i = 0; i < count; ++i) {
                node.children.push_back(reader.i32());
            }
            if (!reader.ok()) {
                err = "nGRP が壊れています: " + path;
                return false;
            }
            groups[node_id] = std::move(node);
        } else if (id == "nSHP") {
            const int node_id = reader.i32();
            (void)reader.dict();
            const std::int32_t count = reader.i32();
            ShpNode node;
            for (std::int32_t i = 0; i < count; ++i) {
                const int model_index = reader.i32();
                (void)reader.dict();
                if (i == 0) {
                    node.model_index = model_index;
                }
            }
            if (!reader.ok()) {
                err = "nSHP が壊れています: " + path;
                return false;
            }
            shapes[node_id] = node;
        } else {
            reader.skip(content_bytes); // 知らないチャンクは飛ばす（PACK / MATL / LAYR など）
        }
        reader.skip(static_cast<std::size_t>(children_size));
    }

    if (out.models.empty()) {
        err = "模型が 1 つも入っていません: " + path;
        return false;
    }
    if (!has_palette) {
        // 既定パレットは持たない。色が無いまま描くと「黒い塊」になって原因が分からなくなる。
        err = "RGBA チャンク（パレット）がありません: " + path;
        return false;
    }

    // --- シーングラフを辿ってパーツを取り出す ---
    if (transforms.empty()) {
        // シーングラフの無い古い `.vox`。模型 1 個 = パーツ 1 個として扱う。
        for (std::size_t i = 0; i < out.models.size(); ++i) {
            VoxPart part;
            part.name = (out.models.size() == 1) ? "main" : ("part" + std::to_string(i));
            part.model_index = static_cast<int>(i);
            out.parts.push_back(part);
        }
        return true;
    }

    struct Walker {
        const std::map<int, TrnNode> &transforms;
        const std::map<int, GrpNode> &groups;
        const std::map<int, ShpNode> &shapes;
        VoxFile &out;
        std::string &err;

        //! @param depth 入れ子の深さ。**環があると戻ってこないので、ここで頭打ちにする。**
        bool visit(int node_id, const int parent_offset[3], const std::string &inherited_name, int depth)
        {
            if (depth > 64) {
                this->err = "シーングラフが深すぎます（環がある可能性）";
                return false;
            }
            const auto trn = this->transforms.find(node_id);
            if (trn != this->transforms.end()) {
                if (trn->second.rotated) {
                    this->err = "回転したノード（`_r`）は未対応です: \"" + trn->second.name + "\"";
                    return false;
                }
                const int center[3] = {
                    parent_offset[0] + trn->second.translation[0],
                    parent_offset[1] + trn->second.translation[1],
                    parent_offset[2] + trn->second.translation[2],
                };
                const std::string name = trn->second.name.empty() ? inherited_name : trn->second.name;
                return this->visit(trn->second.child, center, name, depth + 1);
            }
            const auto grp = this->groups.find(node_id);
            if (grp != this->groups.end()) {
                for (const int child : grp->second.children) {
                    if (!this->visit(child, parent_offset, inherited_name, depth + 1)) {
                        return false;
                    }
                }
                return true;
            }
            const auto shp = this->shapes.find(node_id);
            if (shp != this->shapes.end()) {
                const int model_index = shp->second.model_index;
                if ((model_index < 0) || (model_index >= static_cast<int>(this->out.models.size()))) {
                    this->err = "nSHP が存在しない模型を指しています";
                    return false;
                }
                const VoxModel &model = this->out.models[static_cast<std::size_t>(model_index)];
                VoxPart part;
                part.name = inherited_name.empty() ? ("part" + std::to_string(this->out.parts.size())) : inherited_name;
                part.model_index = model_index;
                // `_t` は中心なので最小隅へ直す（MagicaVoxel の約束）。
                for (int k = 0; k < 3; ++k) {
                    part.origin[k] = parent_offset[k] - (model.size[k] / 2);
                }
                this->out.parts.push_back(std::move(part));
                return true;
            }
            this->err = "シーングラフに存在しない節点への参照があります";
            return false;
        }
    };

    const int root_offset[3] = { 0, 0, 0 };
    Walker walker{ transforms, groups, shapes, out, err };
    if (!walker.visit(0, root_offset, std::string(), 0)) {
        return false;
    }
    if (out.parts.empty()) {
        err = "シーングラフからパーツを 1 つも取り出せませんでした: " + path;
        return false;
    }
    return true;
}

} // namespace hd2d
