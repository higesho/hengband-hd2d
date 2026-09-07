/*!
 * @file prefab_writer.cpp
 * @brief `prefab_writer.h` の実装。
 */
#include "edit/prefab_writer.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>

namespace hd2d {

namespace {

//! `HD2D_BREAK_EDIT` の中身（無ければ空）。**検査の検査のための手段**（ヘッダの表）。
std::string edit_break_mode()
{
    const char *const raw = std::getenv("HD2D_BREAK_EDIT");
    return (raw != nullptr) ? std::string(raw) : std::string();
}

/* ------------------------------------------------------------- .vox 書き出し */

void append_i32(std::string &out, std::int32_t value)
{
    char raw[4];
    std::memcpy(raw, &value, 4); // x86 はリトルエンディアン（形式の約束と同じ）
    out.append(raw, 4);
}

void append_vox_string(std::string &out, const std::string &text)
{
    append_i32(out, static_cast<std::int32_t>(text.size()));
    out.append(text);
}

void append_vox_dict(std::string &out, const std::vector<std::pair<std::string, std::string>> &pairs)
{
    append_i32(out, static_cast<std::int32_t>(pairs.size()));
    for (const auto &[key, value] : pairs) {
        append_vox_string(out, key);
        append_vox_string(out, value);
    }
}

std::string make_chunk(const char *id, const std::string &content, const std::string &children = std::string())
{
    std::string out(id, 4);
    append_i32(out, static_cast<std::int32_t>(content.size()));
    append_i32(out, static_cast<std::int32_t>(children.size()));
    out += content;
    out += children;
    return out;
}

/* ------------------------------------------------------------ .jsonc の材料 */

//! 書き出す数値を 1e-6 へ量子化して double にする。
//! float をそのまま double へ広げると `0.55f` が `0.550000011920929` と書かれてしまう。
double quantized(float value)
{
    return std::round(static_cast<double>(value) * 1e6) / 1e6;
}

const char *axis_name(int axis)
{
    return (axis == 1) ? "y" : ((axis == 2) ? "z" : "x");
}

const char *motion_kind_text(MotionKind kind)
{
    switch (kind) {
    case MotionKind::Rotate:
        return "rotate";
    case MotionKind::Pendulum:
        return "pendulum";
    case MotionKind::Wind:
        return "wind";
    case MotionKind::StateLinked:
        return "state";
    case MotionKind::Blink:
        return "blink";
    case MotionKind::Static:
    default:
        return "static";
    }
}

std::string read_file_binary(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::string();
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool write_file_binary(const std::string &path, const std::string &data, std::string &err)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        err = "開けませんでした（書き込み）: " + path;
        return false;
    }
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!stream) {
        err = "最後まで書けませんでした: " + path;
        return false;
    }
    return true;
}

//! 浮動小数の比較。書き出しが 1e-6 へ量子化するので、1e-3 あれば往復の揺れは全部吸える。
bool nearly(float a, float b)
{
    return std::abs(a - b) <= 1e-3f;
}

} // namespace

bool write_vox_file(const std::string &path, const Prefab &prefab, std::string &err, std::size_t *bytes_out)
{
    if (prefab.parts.empty()) {
        err = "パーツが 1 つもありません（.vox に書くものがありません）";
        return false;
    }
    // 節点名（voxels）の重複は読み戻しで最初の 1 個に吸われて静かに壊れるので、書く前に断る。
    {
        std::set<std::string> seen;
        for (const auto &part : prefab.parts) {
            if (!seen.insert(part.voxels).second) {
                err = "節点名 \"" + part.voxels + "\" が 2 つ以上のパーツで使われています（読み戻しで区別できません）";
                return false;
            }
        }
    }

    //! わざと壊すオプション（ヘッダの表）。最初の 1 ボクセルを黙って落とす。
    bool break_drop_one = (edit_break_mode() == "vox");

    // --- SIZE / XYZI（パーツの並び順に模型を詰め直す） ---
    std::string models;
    for (const auto &part : prefab.parts) {
        if ((part.model_index < 0) || (static_cast<std::size_t>(part.model_index) >= prefab.vox.models.size())) {
            err = "パーツ \"" + part.name + "\" が存在しない模型を指しています";
            return false;
        }
        const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        /*
         * 一辺 256 までは `XYZI`（1 バイト座標）、超えたら `XYZ2`（16 ビット座標）で書く
         * （`vox_file.h` の表。2026-08-21 に 512px の板のために足した拡張）。
         * **切り詰めて書かない**——1 バイトへ丸めると x=256 が x=0 に化けて絵が重なる。
         */
        bool wide = false;
        for (int k = 0; k < 3; ++k) {
            if ((model.size[k] <= 0) || (model.size[k] > 65535)) {
                err = "パーツ \"" + part.name + "\" の模型が形式の上限を超えています（一辺 1..65535）: "
                    + std::to_string(model.size[k]);
                return false;
            }
            wide = wide || (model.size[k] > kVoxClassicMaxSide);
        }
        std::string size_content;
        append_i32(size_content, model.size[0]);
        append_i32(size_content, model.size[1]);
        append_i32(size_content, model.size[2]);

        // 走査順を z → y → x に固定する（同じ入力から必ず同じバイト列を作るため）。
        std::string cells;
        std::int32_t count = 0;
        for (int z = 0; z < model.size[2]; ++z) {
            for (int y = 0; y < model.size[1]; ++y) {
                for (int x = 0; x < model.size[0]; ++x) {
                    const std::uint8_t color = model.at(x, y, z);
                    if (color == 0) {
                        continue;
                    }
                    if (break_drop_one) {
                        break_drop_one = false; // 1 個だけ落とす
                        continue;
                    }
                    if (wide) {
                        const char raw[8] = { static_cast<char>(x & 0xFF), static_cast<char>((x >> 8) & 0xFF),
                            static_cast<char>(y & 0xFF), static_cast<char>((y >> 8) & 0xFF),
                            static_cast<char>(z & 0xFF), static_cast<char>((z >> 8) & 0xFF),
                            static_cast<char>(color), 0 };
                        cells.append(raw, 8);
                    } else {
                        const char raw[4] = { static_cast<char>(x), static_cast<char>(y), static_cast<char>(z),
                            static_cast<char>(color) };
                        cells.append(raw, 4);
                    }
                    ++count;
                }
            }
        }
        std::string xyzi_content;
        append_i32(xyzi_content, count);
        xyzi_content += cells;
        models += make_chunk("SIZE", size_content);
        models += make_chunk(wide ? "XYZ2" : "XYZI", xyzi_content);
    }

    // --- シーングラフ: nTRN(根 id0) → nGRP(id1) → パーツごとに nTRN(2+2i)・nSHP(3+2i) ---
    std::string graph;
    {
        std::string root;
        append_i32(root, 0);
        append_vox_dict(root, {});
        append_i32(root, 1); // 子 = nGRP
        append_i32(root, -1); // reserved
        append_i32(root, 0); // layer
        append_i32(root, 1); // frames
        append_vox_dict(root, {});
        graph += make_chunk("nTRN", root);

        std::string group;
        append_i32(group, 1);
        append_vox_dict(group, {});
        append_i32(group, static_cast<std::int32_t>(prefab.parts.size()));
        for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
            append_i32(group, static_cast<std::int32_t>(2 + (i * 2)));
        }
        graph += make_chunk("nGRP", group);
    }
    for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
        const auto &part = prefab.parts[i];
        const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        // `_t` は**模型の中心**（MagicaVoxel の約束。`vox_file.cpp` が最小隅へ直すのと逆向き）。
        char centre[64];
        std::snprintf(centre, sizeof(centre), "%ld %ld %ld",
            std::lround(part.offset[0]) + (model.size[0] / 2),
            std::lround(part.offset[1]) + (model.size[1] / 2),
            std::lround(part.offset[2]) + (model.size[2] / 2));

        std::string trn;
        append_i32(trn, static_cast<std::int32_t>(2 + (i * 2)));
        append_vox_dict(trn, { { "_name", part.voxels } });
        append_i32(trn, static_cast<std::int32_t>(3 + (i * 2))); // 子 = nSHP
        append_i32(trn, -1);
        append_i32(trn, 0);
        append_i32(trn, 1);
        append_vox_dict(trn, { { "_t", centre } });
        graph += make_chunk("nTRN", trn);

        std::string shp;
        append_i32(shp, static_cast<std::int32_t>(3 + (i * 2)));
        append_vox_dict(shp, {});
        append_i32(shp, 1); // 模型 1 つ
        append_i32(shp, static_cast<std::int32_t>(i)); // 詰め直した並びの添字
        append_vox_dict(shp, {});
        graph += make_chunk("nSHP", shp);
    }

    // --- RGBA（索引 i の色は rgba[i-1]。`vox_file.cpp` の読みと同じ約束） ---
    std::string palette(256 * 4, '\0');
    for (int i = 1; i < 256; ++i) {
        std::memcpy(palette.data() + ((i - 1) * 4), prefab.vox.palette[i], 4);
    }

    const std::string body = models + graph + make_chunk("RGBA", palette);
    std::string data("VOX ", 4);
    append_i32(data, 150);
    data += make_chunk("MAIN", std::string(), body);

    if (!write_file_binary(path, data, err)) {
        return false;
    }
    if (bytes_out != nullptr) {
        *bytes_out = data.size();
    }
    return true;
}

bool write_prefab_jsonc(const std::string &path, const Prefab &prefab, std::string &err, std::size_t *bytes_out)
{
    // --- 先頭の `//` 注釈と改行の流儀は、既存ファイルから引き継ぐ ---
    std::vector<std::string> comments;
    bool crlf = false;
    {
        const std::string old = read_file_binary(path);
        crlf = (old.find("\r\n") != std::string::npos);
        std::size_t pos = 0;
        while (pos < old.size()) {
            std::size_t eol = old.find('\n', pos);
            if (eol == std::string::npos) {
                eol = old.size();
            }
            std::string line = old.substr(pos, eol - pos);
            if (!line.empty() && (line.back() == '\r')) {
                line.pop_back();
            }
            if (line.rfind("//", 0) != 0) {
                break; // 注釈は先頭の連続だけ。中ほどの注釈は保存で消える（ヘッダの注記）
            }
            comments.push_back(line);
            pos = eol + 1;
        }
    }
    if (comments.empty()) {
        comments.push_back("// " + prefab.name + " — プレハブ定義（設計書 §7）。`" + prefab.name + ".vox` と対で使う。");
        comments.push_back("// HengbandHd2d.exe のボクセルエディタ（--edit。P9）が書き出したもの。手で直してもよいが、");
        comments.push_back("// footprint を偽ると読み込み時の照合で落ちる（§7.2）。");
        comments.push_back("// 注意: 保存はこのファイルを丸ごと書き直す。残るのは先頭のこの注釈だけで、中の注釈は消える。");
    }

    // --- 中身。鍵の順序は既存ファイル（export_vox.py の出力）と同じにする ---
    nlohmann::ordered_json root;
    root["name"] = prefab.name;
    root["voxels_per_cell"] = prefab.voxels_per_cell;
    {
        // 宣言はそのまま書く（作り直すかは保存の入口が決める）。書く順だけ正準にする。
        std::set<std::pair<int, int>> cells(prefab.footprint.begin(), prefab.footprint.end());
        nlohmann::ordered_json footprint = nlohmann::ordered_json::array();
        for (const auto &[gx, gy] : cells) {
            footprint.push_back({ gx, gy });
        }
        root["footprint"] = std::move(footprint);
    }
    root["anchor"] = { prefab.anchor[0], prefab.anchor[1] };
    {
        nlohmann::ordered_json parts = nlohmann::ordered_json::array();
        for (const auto &part : prefab.parts) {
            nlohmann::ordered_json node;
            node["name"] = part.name;
            node["voxels"] = part.voxels;
            if ((part.parent >= 0) && (static_cast<std::size_t>(part.parent) < prefab.parts.size())) {
                node["parent"] = prefab.parts[static_cast<std::size_t>(part.parent)].name;
            }
            node["grounded"] = part.grounded;
            if (part.has_pivot) {
                node["pivot"] = { quantized(part.pivot[0]), quantized(part.pivot[1]), quantized(part.pivot[2]) };
            }
            nlohmann::ordered_json motion;
            motion["kind"] = motion_kind_text(part.motion.kind);
            switch (part.motion.kind) {
            case MotionKind::Rotate:
                motion["axis"] = axis_name(part.motion.axis);
                motion["speed"] = quantized(part.motion.speed);
                motion["phase"] = quantized(part.motion.phase);
                break;
            case MotionKind::Pendulum:
                motion["axis"] = axis_name(part.motion.axis);
                motion["amplitude"] = quantized(part.motion.amplitude);
                motion["period"] = quantized(part.motion.period);
                motion["phase"] = quantized(part.motion.phase);
                motion["damping"] = quantized(part.motion.damping);
                break;
            case MotionKind::StateLinked:
                motion["state"] = part.motion.state_name;
                break;
            case MotionKind::Blink:
                motion["period"] = quantized(part.motion.period);
                motion["phase"] = quantized(part.motion.phase);
                motion["duty"] = quantized(part.motion.duty);
                break;
            case MotionKind::Static:
            case MotionKind::Wind:
            default:
                break;
            }
            node["motion"] = std::move(motion);
            node["wind_k"] = quantized(part.wind_k);
            parts.push_back(std::move(node));
        }
        root["parts"] = std::move(parts);
    }
    // variants は読み込み（`Prefab`）が持っていないので、書き出しも空になる。
    // 手で足してあった場合は**保存で消える**（P9 の注記。P10 で variants を入れるときに拡張する）。
    root["variants"] = nlohmann::ordered_json::array();

    std::string text;
    for (const auto &line : comments) {
        text += line + "\n";
    }
    text += root.dump(2);
    text += "\n";
    if (crlf) {
        std::string converted;
        converted.reserve(text.size() + 256);
        for (const char c : text) {
            if (c == '\n') {
                converted += "\r\n";
            } else {
                converted += c;
            }
        }
        text = std::move(converted);
    }

    if (!write_file_binary(path, text, err)) {
        return false;
    }
    if (bytes_out != nullptr) {
        *bytes_out = text.size();
    }
    return true;
}

bool prefabs_equivalent(const Prefab &a, const Prefab &b, std::string &why)
{
    if (a.name != b.name) {
        why = "name が違います（" + a.name + " / " + b.name + "）";
        return false;
    }
    if (a.voxels_per_cell != b.voxels_per_cell) {
        why = "voxels_per_cell が違います";
        return false;
    }
    if ((a.anchor[0] != b.anchor[0]) || (a.anchor[1] != b.anchor[1])) {
        why = "anchor が違います";
        return false;
    }
    {
        const std::set<std::pair<int, int>> fa(a.footprint.begin(), a.footprint.end());
        const std::set<std::pair<int, int>> fb(b.footprint.begin(), b.footprint.end());
        if (fa != fb) {
            why = "footprint が違います（" + std::to_string(fa.size()) + " / " + std::to_string(fb.size()) + " マス）";
            return false;
        }
    }
    if (a.parts.size() != b.parts.size()) {
        why = "パーツの数が違います（" + std::to_string(a.parts.size()) + " / " + std::to_string(b.parts.size()) + "）";
        return false;
    }
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        const PrefabPart &pa = a.parts[i];
        const PrefabPart &pb = b.parts[i];
        const std::string label = "パーツ \"" + pa.name + "\" の ";
        if (pa.name != pb.name) {
            why = "パーツ " + std::to_string(i) + " の名前が違います（" + pa.name + " / " + pb.name + "）";
            return false;
        }
        if (pa.voxels != pb.voxels) {
            why = label + "voxels が違います";
            return false;
        }
        if (pa.parent != pb.parent) {
            why = label + "parent が違います";
            return false;
        }
        if (pa.grounded != pb.grounded) {
            why = label + "grounded が違います";
            return false;
        }
        if (!nearly(pa.wind_k, pb.wind_k)) {
            why = label + "wind_k が違います";
            return false;
        }
        if (pa.has_pivot != pb.has_pivot) {
            why = label + "pivot の有無が違います";
            return false;
        }
        for (int k = 0; pa.has_pivot && (k < 3); ++k) {
            if (!nearly(pa.pivot[k], pb.pivot[k])) {
                why = label + "pivot が違います";
                return false;
            }
        }
        for (int k = 0; k < 3; ++k) {
            if (!nearly(pa.offset[k], pb.offset[k])) {
                why = label + "offset が違います";
                return false;
            }
        }
        if ((pa.motion.kind != pb.motion.kind) || (pa.motion.axis != pb.motion.axis)
            || !nearly(pa.motion.speed, pb.motion.speed) || !nearly(pa.motion.phase, pb.motion.phase)
            || !nearly(pa.motion.amplitude, pb.motion.amplitude) || !nearly(pa.motion.period, pb.motion.period)
            || !nearly(pa.motion.damping, pb.motion.damping) || (pa.motion.state_name != pb.motion.state_name)) {
            why = label + "motion が違います";
            return false;
        }

        // 模型は添字ではなく**パーツ経由**で突き合わせる（書き出しが模型を詰め直すため）。
        const VoxModel &ma = a.vox.models[static_cast<std::size_t>(pa.model_index)];
        const VoxModel &mb = b.vox.models[static_cast<std::size_t>(pb.model_index)];
        if ((ma.size[0] != mb.size[0]) || (ma.size[1] != mb.size[1]) || (ma.size[2] != mb.size[2])) {
            why = label + "模型の大きさが違います";
            return false;
        }
        if (ma.voxels != mb.voxels) {
            std::size_t diff = 0;
            for (std::size_t v = 0; v < ma.voxels.size(); ++v) {
                diff += (ma.voxels[v] != mb.voxels[v]) ? 1 : 0;
            }
            why = label + "模型の中身が違います（" + std::to_string(diff) + " ボクセル）";
            return false;
        }
    }
    for (int i = 1; i < 256; ++i) {
        if (std::memcmp(a.vox.palette[i], b.vox.palette[i], 4) != 0) {
            why = "パレットの索引 " + std::to_string(i) + " が違います";
            return false;
        }
    }
    why.clear();
    return true;
}

bool save_prefab(const std::string &dir, const std::string &name, Prefab &prefab, SaveReport &report, std::string &err)
{
    report = SaveReport{};

    /*
     * footprint は**保存の側が宣言の出どころ**（`export_vox.py` の `derive_footprint` と同じ立場）。
     * `HD2D_BREAK_EDIT=footprint` はこれを黙って飛ばす口で、そのとき読み戻しの
     * `load_prefab`（§7.2-1）が落ちなければ「照合が働いていない」ことになる。
     */
    if (edit_break_mode() == "footprint") {
        std::fprintf(stderr, "[hd2d] HD2D_BREAK_EDIT=footprint: **保存時の footprint 更新をわざと飛ばします**\n");
    } else {
        prefab.footprint = compute_actual_footprint(prefab);
        report.footprint_refreshed = true;
        report.footprint_cells = prefab.footprint.size();
    }

    const std::string separator = (dir.empty() || (dir.back() == '/') || (dir.back() == '\\')) ? "" : "/";
    const std::string vox_path = dir + separator + name + ".vox";
    const std::string jsonc_path = dir + separator + name + ".jsonc";
    if (!write_vox_file(vox_path, prefab, err, &report.vox_bytes)) {
        return false;
    }
    if (!write_prefab_jsonc(jsonc_path, prefab, err, &report.jsonc_bytes)) {
        return false;
    }

    // 読み戻して照合するまでが保存（frame codec の encode(decode(encode)) と同じ考え方）。
    Prefab reloaded;
    std::string reload_err;
    if (!load_prefab(dir, name, reloaded, reload_err)) {
        err = "書いた対を読み戻せませんでした:\n" + reload_err;
        return false;
    }
    std::string why;
    if (!prefabs_equivalent(prefab, reloaded, why)) {
        err = "書いた対を読み戻すと中身が食い違いました（書き出しの欠陥）:\n" + why;
        return false;
    }
    return true;
}

} // namespace hd2d
