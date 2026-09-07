/*!
 * @file prefab.cpp
 * @brief `prefab.h` の実装。
 */
#include "voxel/prefab.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace hd2d {

bool is_supported_voxels_per_cell(int value)
{
    return std::find(std::begin(kVoxelsPerCellLadder), std::end(kVoxelsPerCellLadder), value)
        != std::end(kVoxelsPerCellLadder);
}

std::string voxels_per_cell_ladder_text()
{
    std::string out;
    for (const int step : kVoxelsPerCellLadder) {
        if (!out.empty()) {
            out += "/";
        }
        out += std::to_string(step);
    }
    return out;
}

namespace {

/*!
 * @brief `.jsonc` の材質の綴り → 番号。**`MaterialClass`（`render/surface_wear.h`）と同じ並び。**
 * @details ここで `surface_wear.h` を読まないのは、`voxel/` が描き手に依存しないためである
 * （プレハブの読み込みは GL の要らない木）。**並びを変えるときは両方直すこと**——
 * `--material-check` が食い違いを数える。
 */
std::uint8_t material_index_from(const std::string &text)
{
    static const char *const kNames[] = { "default", "stone", "soil", "wood", "metal", "plaster",
        "fabric", "plant", "clean", "leaf" };
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(std::size(kNames)); ++i) {
        if (text == kNames[i]) {
            return i;
        }
    }
    return 0;
}

MotionKind motion_kind_from(const std::string &text)
{
    if (text == "rotate") {
        return MotionKind::Rotate;
    }
    if (text == "pendulum") {
        return MotionKind::Pendulum;
    }
    if (text == "wind") {
        return MotionKind::Wind;
    }
    if (text == "state") {
        return MotionKind::StateLinked;
    }
    if (text == "blink") {
        return MotionKind::Blink;
    }
    return MotionKind::Static;
}

int axis_from(const std::string &text)
{
    if (text == "y") {
        return 1;
    }
    if (text == "z") {
        return 2;
    }
    return 0;
}

std::string read_text_file(const std::string &path, std::string &err)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        err = "開けませんでした: " + path;
        return std::string();
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string cells_to_text(const std::set<std::pair<int, int>> &cells, std::size_t limit = 12)
{
    std::string out;
    std::size_t shown = 0;
    for (const auto &cell : cells) {
        if (shown >= limit) {
            out += " …";
            break;
        }
        if (!out.empty()) {
            out += " ";
        }
        out += "(" + std::to_string(cell.first) + "," + std::to_string(cell.second) + ")";
        ++shown;
    }
    return out.empty() ? "(なし)" : out;
}

} // namespace

/*!
 * @details 「地面の高さ」は `z = 0` の層とする。接地していないパーツ（`grounded = false`／
 * 最小隅の z が 0 でないパーツ）は接地シルエットに寄与しない（§9.2 の「装飾」層と同じ扱い）。
 */
std::vector<std::pair<int, int>> compute_actual_footprint(const Prefab &prefab)
{
    std::set<std::pair<int, int>> cells;
    for (const auto &part : prefab.parts) {
        if (!part.grounded) {
            continue;
        }
        const int z0 = static_cast<int>(part.offset[2]);
        if (z0 != 0) {
            continue;
        }
        const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        for (int y = 0; y < model.size[1]; ++y) {
            for (int x = 0; x < model.size[0]; ++x) {
                if (model.at(x, y, 0) == 0) {
                    continue;
                }
                const int gx = (x + static_cast<int>(part.offset[0])) / prefab.voxels_per_cell;
                const int gy = (y + static_cast<int>(part.offset[1])) / prefab.voxels_per_cell;
                cells.emplace(gx, gy);
            }
        }
    }
    return std::vector<std::pair<int, int>>(cells.begin(), cells.end());
}

bool load_prefab(const std::string &dir, const std::string &name, Prefab &out, std::string &err)
{
    const std::string separator = (dir.empty() || (dir.back() == '/') || (dir.back() == '\\')) ? "" : "/";
    const std::string jsonc_path = dir + separator + name + ".jsonc";
    const std::string vox_path = dir + separator + name + ".vox";

    if (!load_vox_file(vox_path, out.vox, err)) {
        return false;
    }

    const std::string text = read_text_file(jsonc_path, err);
    if (text.empty()) {
        return false;
    }
    nlohmann::json root;
    try {
        // `.jsonc` なので注釈を許す（`lib/edit/*.jsonc` と同じ流儀。設計書 §7）。
        root = nlohmann::json::parse(text, nullptr, true, true);
    } catch (const std::exception &e) {
        err = std::string("`.jsonc` を解釈できませんでした: ") + jsonc_path + "\n" + e.what();
        return false;
    }

    out.name = root.value("name", name);
    out.voxels_per_cell = root.value("voxels_per_cell", 32);
    //! 空から見えない面を作らない（`Prefab::hide_unseen_from_above` の説明）。無ければ従来どおり。
    out.hide_unseen_from_above = root.value("hide_unseen_from_above", false);
    if (!is_supported_voxels_per_cell(out.voxels_per_cell)) {
        //! 受け口は 7 段（`prefab.h` の `kVoxelsPerCellLadder`）。段の外は黙って通さない。
        err = "voxels_per_cell が " + std::to_string(out.voxels_per_cell) + " です。受け入れるのは "
            + voxels_per_cell_ladder_text() + " のいずれかです: " + jsonc_path;
        return false;
    }
    if (root.contains("anchor") && root["anchor"].is_array() && (root["anchor"].size() >= 2)) {
        out.anchor[0] = root["anchor"][0].get<int>();
        out.anchor[1] = root["anchor"][1].get<int>();
    }
    if (root.contains("footprint") && root["footprint"].is_array()) {
        for (const auto &cell : root["footprint"]) {
            if (cell.is_array() && (cell.size() >= 2)) {
                out.footprint.emplace_back(cell[0].get<int>(), cell[1].get<int>());
            }
        }
    }

    /*
     * **このプレハブだけのパレットの材質**（`palette`。2026-08-23）。
     *
     * 面の汚しと木の葉が材質ごとに掛け方を変えるが、**色から当てるのはやめた**
     * （決めたこと:「パレットの色で汚しを入れるのをやめよう」）。パレットはライブラリじゅうで
     * 色を使い回しているので、同じ灰色が切石にも鉛板にもなる。
     *
     * **`.vox` と食い違ったら宣言ごと捨てる。**`.jsonc` だけ作り直して `.vox` が古い、
     * という対は必ずいつか出る（今の木がまさにその状態だった）。そのとき索引を信じると
     * **全部の材質が 1 つずつずれる**——絵は正常に見えるので、症状から原因へ辿り着けない。
     * 宣言に色も書いてあるので、それを鍵に照合できる。
     */
    if (root.contains("palette") && root["palette"].is_array()) {
        bool agrees = true;
        std::uint8_t materials[256]{};
        for (const auto &node : root["palette"]) {
            if (!node.is_object() || !node.contains("i")) {
                continue;
            }
            const int index = node["i"].get<int>();
            if ((index <= 0) || (index > 255)) {
                agrees = false;
                break;
            }
            const std::string rgb = node.value("rgb", std::string());
            if (rgb.size() == 6) {
                const auto declared = std::strtoul(rgb.c_str(), nullptr, 16);
                const std::uint8_t *actual = out.vox.palette[index];
                const unsigned long here = (static_cast<unsigned long>(actual[0]) << 16)
                    | (static_cast<unsigned long>(actual[1]) << 8) | static_cast<unsigned long>(actual[2]);
                if (declared != here) {
                    agrees = false;
                    break;
                }
            }
            materials[index] = material_index_from(node.value("material", std::string()));
        }
        if (agrees) {
            std::memcpy(out.palette_material, materials, sizeof(materials));
            out.palette_material_declared = true;
        } else {
            std::fprintf(stderr,
                "[hd2d] %s: palette の宣言が `.vox` と食い違うので捨てます（材質は色から当てます）\n",
                jsonc_path.c_str());
        }
    }

    if (!root.contains("parts") || !root["parts"].is_array() || root["parts"].empty()) {
        err = "parts がありません: " + jsonc_path;
        return false;
    }
    for (const auto &node : root["parts"]) {
        PrefabPart part;
        part.name = node.value("name", std::string());
        part.voxels = node.value("voxels", part.name);
        const VoxPart *const vox_part = out.vox.find_part(part.voxels);
        if (vox_part == nullptr) {
            err = "`.vox` に節点 \"" + part.voxels + "\" がありません: " + vox_path;
            return false;
        }
        part.model_index = vox_part->model_index;
        for (int k = 0; k < 3; ++k) {
            part.offset[k] = static_cast<float>(vox_part->origin[k]);
        }
        // `.jsonc` にも書いてあれば照合する（配置の基準は `.vox`）。
        if (node.contains("offset") && node["offset"].is_array() && (node["offset"].size() >= 3)) {
            for (int k = 0; k < 3; ++k) {
                const auto declared = node["offset"][static_cast<std::size_t>(k)].get<float>();
                if (std::abs(declared - part.offset[k]) > 0.001f) {
                    err = "パーツ \"" + part.name + "\" の offset が `.vox` と食い違っています（"
                        + "宣言 " + std::to_string(declared) + " / 実際 " + std::to_string(part.offset[k]) + "）";
                    return false;
                }
            }
        }
        if (node.contains("pivot") && node["pivot"].is_array() && (node["pivot"].size() >= 3)) {
            part.has_pivot = true;
            for (int k = 0; k < 3; ++k) {
                part.pivot[k] = node["pivot"][static_cast<std::size_t>(k)].get<float>();
            }
        }
        part.grounded = node.value("grounded", true);
        part.wind_k = node.value("wind_k", 1.f);
        if (node.contains("motion") && node["motion"].is_object()) {
            const auto &motion = node["motion"];
            part.motion.kind = motion_kind_from(motion.value("kind", std::string("static")));
            part.motion.axis = axis_from(motion.value("axis", std::string("x")));
            part.motion.speed = motion.value("speed", 0.f);
            part.motion.phase = motion.value("phase", 0.f);
            part.motion.amplitude = motion.value("amplitude", 0.f);
            part.motion.period = motion.value("period", 0.f);
            part.motion.damping = motion.value("damping", 0.f);
            part.motion.duty = motion.value("duty", 0.5f);
            part.motion.state_name = motion.value("state", std::string());
        }
        out.parts.push_back(std::move(part));
    }

    // --- 親の解決（名前 → 添字） ---
    {
        std::size_t index = 0;
        for (const auto &node : root["parts"]) {
            const std::string parent = node.value("parent", std::string());
            if (!parent.empty()) {
                const auto found = std::find_if(out.parts.begin(), out.parts.end(),
                    [&parent](const PrefabPart &p) { return p.name == parent; });
                if (found == out.parts.end()) {
                    err = "パーツ \"" + out.parts[index].name + "\" の親 \"" + parent + "\" が見つかりません";
                    return false;
                }
                out.parts[index].parent = static_cast<int>(std::distance(out.parts.begin(), found));
            }
            ++index;
        }
    }

    /* ==================================================== 検査（§7.2） */

    // (3) 親子関係に循環が無いこと。**先にこれを見る**（後の検査が回らなくなるので）。
    for (std::size_t i = 0; i < out.parts.size(); ++i) {
        int cursor = out.parts[i].parent;
        for (std::size_t steps = 0; cursor >= 0; ++steps) {
            if (steps > out.parts.size()) {
                err = "パーツの親子関係に循環があります（\"" + out.parts[i].name + "\" から辿れません）";
                return false;
            }
            if (static_cast<std::size_t>(cursor) == i) {
                err = "パーツ \"" + out.parts[i].name + "\" が自分自身の先祖になっています";
                return false;
            }
            cursor = out.parts[static_cast<std::size_t>(cursor)].parent;
        }
    }

    // (2) ピボットがそのパーツの範囲内にあること。
    for (const auto &part : out.parts) {
        if (!part.has_pivot) {
            continue;
        }
        const VoxModel &model = out.vox.models[static_cast<std::size_t>(part.model_index)];
        for (int k = 0; k < 3; ++k) {
            const float local = part.pivot[k] - part.offset[k];
            if ((local < 0.f) || (local > static_cast<float>(model.size[k]))) {
                err = "パーツ \"" + part.name + "\" のピボットが範囲の外にあります（軸 "
                    + std::to_string(k) + ": " + std::to_string(local) + " が 0.."
                    + std::to_string(model.size[k]) + " の外）";
                return false;
            }
        }
    }

    // (1) 宣言 footprint と、地面の高さのボクセルの一致。
    {
        const auto actual_cells = compute_actual_footprint(out);
        const std::set<std::pair<int, int>> actual(actual_cells.begin(), actual_cells.end());
        const std::set<std::pair<int, int>> declared(out.footprint.begin(), out.footprint.end());
        std::set<std::pair<int, int>> missing; // 実際にあるのに宣言していない
        std::set<std::pair<int, int>> extra; // 宣言したのに実際は無い
        std::set_difference(actual.begin(), actual.end(), declared.begin(), declared.end(),
            std::inserter(missing, missing.end()));
        std::set_difference(declared.begin(), declared.end(), actual.begin(), actual.end(),
            std::inserter(extra, extra.end()));
        if (!missing.empty() || !extra.empty()) {
            err = "footprint の宣言が実体と食い違っています（" + jsonc_path + "）\n";
            err += "  宣言していないのに接地しているマス: " + cells_to_text(missing) + "\n";
            err += "  宣言したのに接地していないマス:     " + cells_to_text(extra);
            return false;
        }
    }

    return true;
}

} // namespace hd2d
