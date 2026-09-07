/*!
 * @file prefab_library.cpp
 * @brief `prefab_library.h` の実装。
 */
#include "world/prefab_library.h"

#include "voxel/part_motion.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <chrono> //!< 汲む時間の予算（`pump_upload`）
#include <condition_variable> //!< ライブラリのメッシュ化を多スレッドで回す（2026-08-23）

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <memory> //!< 同上（仕事を持つ）
#include <mutex> //!< 同上
#include <sstream>
#include <thread> //!< 同上

namespace hd2d {

namespace {

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

/*!
 * @brief 末尾カンマを落とす（`lib/edit/*.jsonc` は `,}` を含み、nlohmann は受け付けない）。
 * @details 文字列と注釈（`//` `／* *／`）の中は触らない。コアは独自の読み方をしているので、
 * こちらは**読むためだけ**の前処理である（書き戻しはしない）。
 */
std::string strip_trailing_commas(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    const std::size_t n = text.size();
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = text[i];
        if (in_string) {
            out.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            out.push_back(c);
            continue;
        }
        if ((c == '/') && (i + 1 < n) && (text[i + 1] == '/')) {
            while ((i < n) && (text[i] != '\n')) {
                out.push_back(text[i]);
                ++i;
            }
            if (i < n) {
                out.push_back('\n');
            }
            continue;
        }
        if ((c == '/') && (i + 1 < n) && (text[i + 1] == '*')) {
            out.push_back(text[i]);
            out.push_back(text[i + 1]);
            i += 2;
            while ((i + 1 < n) && !((text[i] == '*') && (text[i + 1] == '/'))) {
                out.push_back(text[i]);
                ++i;
            }
            if (i + 1 < n) {
                out.push_back('*');
                out.push_back('/');
                ++i;
            }
            continue;
        }
        if (c == ',') {
            // 先を覗く（空白と注釈を飛ばす）。次が `}` か `]` ならこのカンマは書かない。
            std::size_t j = i + 1;
            while (j < n) {
                if ((text[j] == ' ') || (text[j] == '\t') || (text[j] == '\r') || (text[j] == '\n')) {
                    ++j;
                    continue;
                }
                if ((text[j] == '/') && (j + 1 < n) && (text[j + 1] == '/')) {
                    while ((j < n) && (text[j] != '\n')) {
                        ++j;
                    }
                    continue;
                }
                if ((text[j] == '/') && (j + 1 < n) && (text[j + 1] == '*')) {
                    j += 2;
                    while ((j + 1 < n) && !((text[j] == '*') && (text[j + 1] == '/'))) {
                        ++j;
                    }
                    j = (j + 1 < n) ? (j + 2) : n;
                    continue;
                }
                break;
            }
            if ((j < n) && ((text[j] == '}') || (text[j] == ']'))) {
                continue; // 末尾カンマ。捨てる
            }
        }
        out.push_back(c);
    }
    return out;
}

//! `HD2D_BREAK_TERRAIN_MAP`（検査の検査）。
const char *break_terrain_map()
{
    const char *const value = std::getenv("HD2D_BREAK_TERRAIN_MAP");
    return ((value != nullptr) && (value[0] != '\0')) ? value : nullptr;
}

const char *role_name_of(CellRole role)
{
    switch (role) {
    case CellRole::RoomFloor:
        return "room_floor";
    case CellRole::CorridorFloor:
        return "corridor_floor";
    case CellRole::StructuralWall:
        return "structural_wall";
    case CellRole::Bedrock:
        return "bedrock";
    case CellRole::Doorway:
        return "doorway";
    case CellRole::Stairs:
        return "stairs";
    case CellRole::Unknown:
    default:
        return nullptr;
    }
}

} // namespace

int PrefabLibrary::terrain_id_for_key(const std::string &key) const
{
    for (const auto &[name, id] : this->mapped_keys_) {
        if (name == key) {
            return id;
        }
    }
    return 0;
}

bool PrefabLibrary::parse_rule(const void *json_node, TerrainRule &out, std::string &err)
{
    const auto &node = *static_cast<const nlohmann::json *>(json_node);
    auto read_set = [&](const char *field, std::vector<int> &dst) -> bool {
        if (!node.contains(field)) {
            return true;
        }
        for (const auto &name_node : node[field]) {
            const std::string name = name_node.get<std::string>();
            const auto found = this->by_name_.find(name);
            if (found == this->by_name_.end()) {
                err = "対応表が知らないプレハブを指しています: " + name;
                return false;
            }
            dst.push_back(found->second);
        }
        return true;
    };
    if (!read_set("ground", out.ground) || !read_set("structure", out.structure) || !read_set("object", out.object)
        || !read_set("object_ew", out.object_ew) || !read_set("sign", out.sign)) {
        return false;
    }
    /*
     * 姿勢の対は**同じ枚数**でなければならない（P10 レビュー 11）。`object` から選んだ
     * 添字をそのまま `object_ew` に使うので、数が違うと黙って別の物が出る。
     */
    if (!out.object_ew.empty() && (out.object_ew.size() != out.object.size())) {
        err = "対応表の object_ew は object と同じ枚数にしてください（姿勢の対なので）";
        return false;
    }
    out.density = node.value("density", 1.f);
    out.jitter = node.value("jitter", 0.f);
    if (node.contains("emissive") && node["emissive"].is_array() && (node["emissive"].size() >= 3)) {
        for (int k = 0; k < 3; ++k) {
            out.emissive[k] = node["emissive"][static_cast<std::size_t>(k)].get<float>();
        }
    }
    if (node.contains("tint") && node["tint"].is_array() && (node["tint"].size() >= 3)) {
        for (int k = 0; k < 3; ++k) {
            out.tint[k] = node["tint"][static_cast<std::size_t>(k)].get<float>();
        }
    }
    /*
     * 光の柱（`render/light_shaft.h`）。**入れ子の欄**にしてあるのは、4 つが 1 組で
     * 意味を持つからである（`alpha` だけ書いても色と大きさが既定では絵にならない）。
     */
    if (node.contains("light_shaft") && node["light_shaft"].is_object()) {
        const auto &shaft = node["light_shaft"];
        if (shaft.contains("color") && shaft["color"].is_array() && (shaft["color"].size() >= 3)) {
            for (int k = 0; k < 3; ++k) {
                out.shaft_color[k] = shaft["color"][static_cast<std::size_t>(k)].get<float>();
            }
        }
        if (shaft.contains("slant") && shaft["slant"].is_array() && (shaft["slant"].size() >= 2)) {
            for (int k = 0; k < 2; ++k) {
                out.shaft_slant[k] = shaft["slant"][static_cast<std::size_t>(k)].get<float>();
            }
        }
        out.shaft_radius = shaft.value("radius", out.shaft_radius);
        out.shaft_height = shaft.value("height", out.shaft_height);
        out.shaft_full = shaft.value("full", out.shaft_full);
        out.shaft_taper = shaft.value("taper", out.shaft_taper);
        out.shaft_alpha = shaft.value("alpha", out.shaft_alpha);
    }
    return true;
}

/*!
 * @brief 先回りして抱えてよい大きさ（バイト）。**記憶の峰を抑える蓋。**
 * @details 詳しくは `begin_upload()` の註。個数ではなくバイトで数えるのは、
 * アトラスの大きさが 8 KB から 2 MB まで開いているためである。
 * 32 MB は「全部を先にメッシュ化した場合の 110 MB」の 3 分の 1 以下で、
 * 小物ばかりなら 900 個ぶん先回りできる＝汲む側の速さでメッシュ化が絞られない。
 */
constexpr std::size_t kUploadWindowBytes = 32u * 1024u * 1024u;
//! それでも個数の蓋は残す（極端に小さいプレハブばかりのときの暴走止め）。
constexpr std::size_t kUploadWindowCount = 1024;

PrefabLibrary::PrefabLibrary() = default;

/*!
 * @brief 看取り。**走っている載せる仕事があれば畳んでから消える。**
 * @details ここが `.cpp` に在るのは `UploadJob` が `.h` では不完全型だからである。
 */
PrefabLibrary::~PrefabLibrary()
{
    this->finish_upload_job();
}

bool PrefabLibrary::load(const std::string &voxel_dir, std::string &err)
{
    this->entries_.clear();
    this->by_name_.clear();
    this->terrain_rules_.clear();
    this->rule_by_id_.clear();
    this->mapped_keys_.clear();
    for (bool &present : this->role_present_) {
        present = false;
    }
    this->loaded_ = false;

    const std::string separator = (voxel_dir.empty() || (voxel_dir.back() == '/') || (voxel_dir.back() == '\\')) ? "" : "/";
    const std::string map_path = voxel_dir + separator + "terrain_prefabs.jsonc";
    const std::string text = read_text_file(map_path, err);
    if (text.empty()) {
        return false;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text, nullptr, true, true);
    } catch (const std::exception &e) {
        err = "対応表を解釈できませんでした: " + map_path + "\n" + e.what();
        return false;
    }

    // --- まず表が参照するプレハブ名を全部集める（重複は 1 回だけ読む）---
    std::set<std::string> names;
    auto collect = [&names](const nlohmann::json &node) {
        //! **節を足したらここにも足す**（罠 52・67。`sign` は P10 第 2 期で足した）。
        for (const char *const field : { "ground", "structure", "object", "object_ew", "sign" }) {
            if (node.contains(field)) {
                for (const auto &name_node : node[field]) {
                    names.insert(name_node.get<std::string>());
                }
            }
        }
    };
    /*
     * **上書きの節（`*_surface`）も必ず走査する。**`terrains` と `roles` だけを見ていて、
     * `terrains_surface` が指す木（tree_03〜10）がライブラリに載らず「知らないプレハブ」で
     * 落ちた（罠 52）。節を足したら、ここにも足す。
     */
    for (const char *const section : { "roles", "roles_surface" }) {
        if (!root.contains(section)) {
            continue;
        }
        for (const auto &item : root[section].items()) {
            collect(item.value());
        }
    }
    for (const char *const section : { "terrains", "terrains_surface", "terrains_extra" }) {
        if (!root.contains(section)) {
            continue;
        }
        for (const auto &node : root[section]) {
            collect(node);
        }
    }
    if (root.contains("extra")) {
        // 装飾（marks からコードが置くもの）。規則からは参照されないがライブラリには要る。
        for (const auto &name_node : root["extra"]) {
            names.insert(name_node.get<std::string>());
        }
    }
    const char *const broken = break_terrain_map();
    if ((broken != nullptr) && (std::strcmp(broken, "prefab") == 0)) {
        // **わざと壊す**: 存在しないプレハブを 1 つ要求する。load は失敗しなければならない。
        names.insert("__hd2d_break_terrain_map__");
    }

    // --- プレハブを読む（GL 不要。footprint 照合は load_prefab の中）---
    for (const std::string &name : names) {
        LibraryEntry entry;
        entry.name = name;
        if (!load_prefab(voxel_dir, name, entry.prefab, err)) {
            err = "対応表のプレハブ \"" + name + "\" を読めませんでした:\n" + err;
            return false;
        }
        float top = 0.f;
        //! 水平に覆う範囲（ボクセル）。**すべてのパーツの和**（葉の張り出しを含む）。
        int span_x0 = 0;
        int span_x1 = 0;
        int span_y0 = 0;
        int span_y1 = 0;
        bool span_seen = false;
        for (const auto &part : entry.prefab.parts) {
            const VoxModel &model = entry.prefab.vox.models[static_cast<std::size_t>(part.model_index)];
            const float part_top = (part.offset[2] + static_cast<float>(model.size[2]))
                / static_cast<float>(entry.prefab.voxels_per_cell);
            top = (part_top > top) ? part_top : top;
            entry.has_motion = entry.has_motion || (part.motion.kind == MotionKind::Rotate)
                || (part.motion.kind == MotionKind::Pendulum) || (part.motion.kind == MotionKind::StateLinked)
                || (part.motion.kind == MotionKind::Blink);

            const int x0 = static_cast<int>(part.offset[0]);
            const int y0 = static_cast<int>(part.offset[1]);
            const int x1 = x0 + model.size[0];
            const int y1 = y0 + model.size[1];
            if (!span_seen) {
                span_x0 = x0;
                span_x1 = x1;
                span_y0 = y0;
                span_y1 = y1;
                span_seen = true;
            } else {
                span_x0 = std::min(span_x0, x0);
                span_x1 = std::max(span_x1, x1);
                span_y0 = std::min(span_y0, y0);
                span_y1 = std::max(span_y1, y1);
            }
        }
        if (span_seen) {
            //! マスへ落とす（`span_x1` は終端なので 1 引いてから割る）。
            const int per = std::max(1, entry.prefab.voxels_per_cell);
            entry.cover_x0 = static_cast<int>(std::floor(static_cast<float>(span_x0) / static_cast<float>(per)));
            entry.cover_x1 = static_cast<int>(std::floor(static_cast<float>(span_x1 - 1) / static_cast<float>(per)));
            entry.cover_y0 = static_cast<int>(std::floor(static_cast<float>(span_y0) / static_cast<float>(per)));
            entry.cover_y1 = static_cast<int>(std::floor(static_cast<float>(span_y1 - 1) / static_cast<float>(per)));
        }
        entry.top_z = top;
        entry.is_ground = (top <= 0.05f);
        if (!entry.prefab.footprint.empty()) {
            entry.anchor_x = static_cast<float>(entry.prefab.footprint.front().first);
            entry.anchor_y = static_cast<float>(entry.prefab.footprint.front().second);
        }
        this->by_name_.emplace(name, static_cast<int>(this->entries_.size()));
        this->entries_.push_back(std::move(entry));
    }

    // --- 役割の既定（`roles` = どこでも / `roles_surface` = 地上だけの上書き）---
    for (const char *const section : { "roles", "roles_surface" }) {
        if (!root.contains(section)) {
            continue;
        }
        const bool surface = (std::strcmp(section, "roles_surface") == 0);
        for (const auto &item : root[section].items()) {
            CellRole role = CellRole::Unknown;
            for (const CellRole candidate : { CellRole::RoomFloor, CellRole::CorridorFloor, CellRole::StructuralWall,
                     CellRole::Bedrock, CellRole::Doorway, CellRole::Stairs }) {
                const char *const candidate_name = role_name_of(candidate);
                if ((candidate_name != nullptr) && (item.key() == candidate_name)) {
                    role = candidate;
                    break;
                }
            }
            if (role == CellRole::Unknown) {
                err = std::string("対応表の ") + section + " に知らない役割があります: " + item.key();
                return false;
            }
            TerrainRule rule;
            if (!this->parse_rule(&item.value(), rule, err)) {
                return false;
            }
            if (surface) {
                this->role_surface_rules_[static_cast<int>(role)] = rule;
                this->role_surface_present_[static_cast<int>(role)] = true;
            } else {
                this->role_rules_[static_cast<int>(role)] = rule;
                this->role_present_[static_cast<int>(role)] = true;
            }
        }
    }

    // --- 地形の細別（`terrains` = どこでも / `terrains_surface` = 地上だけの上書き）---
    bool first = true;
    for (const char *const section : { "terrains", "terrains_surface", "terrains_extra" }) {
        if (!root.contains(section)) {
            continue;
        }
        const bool surface = (std::strcmp(section, "terrains_surface") == 0);
        /*
         * `terrains_extra` は**変愚以外のコアだけが送る地形**である（表の頭の説明）。
         * 変愚の `TerrainDefinitions.jsonc` に key が無いので、`cross_check` の
         * 突き合わせには載せない——載せると「key が無い」で必ず FAIL する。
         * 規則そのものは `terrains` と同じ表（`rule_by_id_`）へ入る。
         */
        const bool cross_checked = (std::strcmp(section, "terrains_extra") != 0);
        for (const auto &node : root[section]) {
            if (!node.contains("key") || !node.contains("id")) {
                err = std::string("対応表の ") + section + " に key か id の無い行があります";
                return false;
            }
            TerrainRule rule;
            if (!this->parse_rule(&node, rule, err)) {
                return false;
            }
            int id = node["id"].get<int>();
            if (first && (broken != nullptr) && (std::strcmp(broken, "id") == 0)) {
                id += 1; // **わざと壊す**: cross_check が食い違いとして落とさなければならない
            }
            first = false;
            if (surface) {
                this->rule_by_id_surface_[id] = static_cast<int>(this->terrain_rules_.size());
            } else {
                this->rule_by_id_[id] = static_cast<int>(this->terrain_rules_.size());
            }
            this->terrain_rules_.push_back(std::move(rule));
            if (cross_checked) {
                this->mapped_keys_.emplace_back(node["key"].get<std::string>(), id);
            }
        }
    }

    this->motions_.assign(this->entries_.size(), {});
    this->loaded_ = true;
    return true;
}

int PrefabLibrary::find(const std::string &name) const
{
    const auto found = this->by_name_.find(name);
    return (found == this->by_name_.end()) ? -1 : found->second;
}

const TerrainRule *PrefabLibrary::rule_for_terrain(std::uint16_t terrain_id, bool surface) const
{
    if (surface) {
        const auto over = this->rule_by_id_surface_.find(static_cast<int>(terrain_id));
        if (over != this->rule_by_id_surface_.end()) {
            return &this->terrain_rules_[static_cast<std::size_t>(over->second)];
        }
    }
    const auto found = this->rule_by_id_.find(static_cast<int>(terrain_id));
    return (found == this->rule_by_id_.end()) ? nullptr : &this->terrain_rules_[static_cast<std::size_t>(found->second)];
}

const TerrainRule *PrefabLibrary::rule_for_role(CellRole role, bool surface) const
{
    const int at = static_cast<int>(role);
    if ((at < 0) || (at >= 8)) {
        return nullptr;
    }
    if (surface && this->role_surface_present_[at]) {
        return &this->role_surface_rules_[at];
    }
    if (!this->role_present_[at]) {
        return nullptr;
    }
    return &this->role_rules_[at];
}

/*!
 * @brief 走っている「載せる仕事」の中身。
 * @details メッシュ化のスレッドと、作りかけのメッシュの置き場。`.h` には名前だけ出す
 * （スレッドとロックの頭をライブラリを呼ぶ側ぜんぶへ配りたくない）。
 */
struct PrefabLibrary::UploadJob {
    std::vector<std::size_t> todo; //!< まだ載っていないもの（`entries_` の添字）
    std::vector<PrefabMesh> meshes; //!< `todo` と同じ並び。載せたら捨てる
    std::vector<char> ready; //!< メッシュ化が済んだか
    std::vector<std::thread> pool;
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t next{ 0 }; //!< 次にメッシュ化する添字
    std::size_t cursor{ 0 }; //!< 主スレッドが載せ終えた地点
    std::size_t in_flight_bytes{ 0 }; //!< メッシュ化は済んだが、まだ載せていないぶんの大きさ
    bool failed{ false };
    std::string fail_msg;
    std::size_t workers{ 1 };
};

/*!
 * @brief 載せる仕事を起こす。**メッシュ化は多スレッド・GL は触らない。**
 *
 * @details 起動時間のほとんどがここにある。内訳の実測（2026-08-23。プレハブ 2,514 個。
 * Windows / 24 コア）: 読み込み（.vox と jsonc の解釈）465 ms に対し、載せるほうが 5,980 ms。
 * そのうち **`mesh_model()` が 5,755 ms** で、GL の呼び出しは 225 ms しかなかった。
 * メッシュ化はプレハブごとに独立した純粋な CPU 仕事なので、ここで割って並べる。
 *
 * 形は**上限つきの先回り**である:
 * - 手すきのスレッドが「まだメッシュ化していない次の 1 個」を取ってメッシュ化する
 * - ただし**まだ載せていないメッシュの合計が `kUploadWindowBytes` を超えたら止まる**
 * - 主スレッドは（`pump_upload` で）順番どおりに受け取り、GPU へ載せ、メッシュを捨てる
 *
 * 上限が要るのは**記憶の量**のため。全部を先にメッシュ化すると、アトラスだけで 93.4 MB
 * （2,615 メッシュ・RG8UI・1 枚最大 2 MB）＋索引 13 MB ＋頂点を一度に抱える。
 * デスクトップなら平気でも Quest や携帯では危ない。上限を挟めば峰は 32 MB で頭打ちになる。
 *
 * **個数ではなくバイトで数える理由が 2 つある。** 1 つはアトラスの大きさが 8 KB 〜 2 MB と
 * 開いていること。もう 1 つは**汲む側の速さでメッシュ化が絞られないため**である——
 * 演出のフレームから少しずつ載せる形（`pump_upload`）では主スレッドは 1 フレーム 12 ms しか
 * 使えないので、個数 32 の蓋だとメッシュ化がそれに引きずられて遅くなった（実測: 決定から
 * コア起動まで 2,545 ms のうち、演出の裏で終えられていたのは半分ほどだった）。
 */
void PrefabLibrary::begin_upload()
{
    if (this->upload_job_) {
        return; // すでに走っている
    }
    auto job = std::make_unique<UploadJob>();
    job->todo.reserve(this->entries_.size());
    for (std::size_t i = 0; i < this->entries_.size(); ++i) {
        if (!this->entries_[i].gpu_ready) {
            job->todo.push_back(i);
        }
    }
    if (job->todo.empty()) {
        return; // 載せるものが無い
    }
    job->meshes.resize(job->todo.size());
    job->ready.assign(job->todo.size(), 0);

    /*
     * スレッドの本数。主スレッドは GL と描画で塞がるので 1 本ぶん空ける。
     * 上限 16 は「数だけあっても先回りの上限で頭打ちになる」ところで切った値。
     * 数が取れない機械では 1 本＝従来と同じ直列に落ちる（**必ず動く道を残す**）。
     */
    const unsigned cores = std::thread::hardware_concurrency();
    job->workers = std::min<std::size_t>(16, (cores > 1) ? (cores - 1) : 1);

    UploadJob *const raw_job = job.get();
    const auto mesh_worker = [this, raw_job]() {
        for (;;) {
            std::size_t at = 0;
            {
                std::unique_lock<std::mutex> lock(raw_job->mutex);
                raw_job->cv.wait(lock, [raw_job]() {
                    return raw_job->failed || (raw_job->next >= raw_job->todo.size())
                        || ((raw_job->in_flight_bytes < kUploadWindowBytes)
                            && ((raw_job->next - raw_job->cursor) < kUploadWindowCount));
                });
                if (raw_job->failed || (raw_job->next >= raw_job->todo.size())) {
                    return;
                }
                at = raw_job->next++;
            }
            PrefabMesh built;
            std::string mesh_err;
            //! **ここは GL に触らない。**1 個 1 行のログも切る（2,615 行になる）。
            const bool ok = VoxelRenderer::build_mesh(
                this->entries_[raw_job->todo[at]].prefab, built, mesh_err, false);
            {
                std::lock_guard<std::mutex> lock(raw_job->mutex);
                if (ok) {
                    raw_job->in_flight_bytes += built.bytes();
                    raw_job->meshes[at] = std::move(built);
                } else if (!raw_job->failed) {
                    raw_job->failed = true;
                    raw_job->fail_msg = "プレハブ \"" + this->entries_[raw_job->todo[at]].name
                        + "\" をメッシュにできませんでした:\n" + mesh_err;
                }
                raw_job->ready[at] = 1;
            }
            raw_job->cv.notify_all();
        }
    };
    job->pool.reserve(job->workers);
    for (std::size_t i = 0; i < job->workers; ++i) {
        job->pool.emplace_back(mesh_worker);
    }
    std::fprintf(stderr, "[hd2d] メッシュ化を始めます: %u 個（スレッド %u 本・先回り %u MB まで）\n",
        static_cast<unsigned>(job->todo.size()), static_cast<unsigned>(job->workers),
        static_cast<unsigned>(kUploadWindowBytes / (1024u * 1024u)));
    this->upload_job_ = std::move(job);
}

//! スレッドを畳んで仕事を捨てる。**成功でも失敗でも必ずここを通る。**
void PrefabLibrary::finish_upload_job()
{
    if (!this->upload_job_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(this->upload_job_->mutex);
        this->upload_job_->failed = true; //!< 待っているスレッドを起こして終わらせる合図
    }
    this->upload_job_->cv.notify_all();
    for (std::thread &thread : this->upload_job_->pool) {
        thread.join();
    }
    this->upload_job_.reset();
}

bool PrefabLibrary::upload_busy() const
{
    return static_cast<bool>(this->upload_job_);
}

bool PrefabLibrary::pump_upload(VoxelRenderer &renderer, int budget_ms, std::string &err)
{
    if (!this->upload_job_) {
        return true; // 仕事が無い
    }
    const auto began = std::chrono::steady_clock::now();
    const auto spent_ms = [&began]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - began)
            .count();
    };
    UploadJob &job = *this->upload_job_;
    bool ok = true;
    while (job.cursor < job.todo.size()) {
        PrefabMesh built;
        {
            std::unique_lock<std::mutex> lock(job.mutex);
            /*
             * **`failed` も待ちの終わりに入れる**（入れないと、落ちた後のものを永久に待つ）。
             * 予算があるときは待ちにも上限を掛ける——ここで待ち込むと演出が止まる。
             */
            const auto done = [&job]() { return job.failed || (job.ready[job.cursor] != 0); };
            if (budget_ms > 0) {
                /*
                 * **予算があるときは待たない。**届いているぶんだけ載せて戻る。
                 * ここで待つと、予算がメッシュ化の待ちに食われて**絵のほうが落ちる**——
                 * 待つ形にしていたときは 1.5 秒の演出が 70 フレーム（45 fps）だった。
                 * 待たない形なら主スレッドが使うのは GL の仕事そのものだけになる。
                 */
                if (!done()) {
                    break; // まだメッシュ化が届かない。続きは次のフレームで
                }
            } else {
                job.cv.wait(lock, done);
            }
            if (job.failed) {
                err = job.fail_msg;
                ok = false;
            } else {
                built = std::move(job.meshes[job.cursor]);
                job.meshes[job.cursor] = PrefabMesh{};
                const std::size_t freed = built.bytes();
                job.in_flight_bytes = (job.in_flight_bytes > freed) ? (job.in_flight_bytes - freed) : 0;
            }
        }
        if (!ok) {
            break;
        }
        LibraryEntry &entry = this->entries_[job.todo[job.cursor]];
        if (!renderer.upload_meshed(entry.prefab, built, entry.gpu, err)) {
            err = "プレハブ \"" + entry.name + "\" を GPU へ載せられませんでした:\n" + err;
            ok = false;
            break;
        }
        entry.gpu_ready = true;
        {
            std::lock_guard<std::mutex> lock(job.mutex);
            job.cursor += 1;
        }
        job.cv.notify_all(); //!< 上限が 1 個ぶん開いた
        if ((budget_ms > 0) && (spent_ms() >= budget_ms)) {
            break;
        }
    }
    this->upload_ms_ += static_cast<unsigned>(spent_ms());
    const bool complete = !ok || (this->upload_job_->cursor >= this->upload_job_->todo.size());
    if (complete) {
        this->finish_upload_job();
    }
    return ok;
}

bool PrefabLibrary::upload_gpu(VoxelRenderer &renderer, std::string &err)
{
    this->begin_upload();
    return this->pump_upload(renderer, 0, err); // 予算なし＝終わるまで
}
void PrefabLibrary::release_gpu(VoxelRenderer &renderer)
{
    for (auto &entry : this->entries_) {
        if (entry.gpu_ready) {
            renderer.release(entry.gpu);
            entry.gpu_ready = false;
        }
    }
}

void PrefabLibrary::update_motions(float time)
{
    MotionContext ctx;
    ctx.time = time;
    for (std::size_t i = 0; i < this->entries_.size(); ++i) {
        if (!this->entries_[i].has_motion) {
            continue;
        }
        compose_part_motions(this->entries_[i].prefab, ctx, this->motions_[i]);
    }
}

const Mat4 *PrefabLibrary::motions_of(int i) const
{
    const auto &entry = this->entries_[static_cast<std::size_t>(i)];
    if (!entry.has_motion || this->motions_[static_cast<std::size_t>(i)].empty()) {
        return nullptr;
    }
    return this->motions_[static_cast<std::size_t>(i)].data();
}

std::size_t PrefabLibrary::motion_count_of(int i) const
{
    const auto &entry = this->entries_[static_cast<std::size_t>(i)];
    if (!entry.has_motion) {
        return 0;
    }
    return this->motions_[static_cast<std::size_t>(i)].size();
}

bool PrefabLibrary::cross_check(const std::string &terrain_defs_path, std::string &report, std::string &err) const
{
    report.clear();
    const std::string raw = read_text_file(terrain_defs_path, err);
    if (raw.empty()) {
        return false;
    }
    nlohmann::json root;
    try {
        // lib/edit の .jsonc は末尾カンマを含むので、読める形に直してから渡す。
        root = nlohmann::json::parse(strip_trailing_commas(raw), nullptr, true, true);
    } catch (const std::exception &e) {
        err = std::string("TerrainDefinitions.jsonc を解釈できませんでした:\n") + e.what();
        return false;
    }
    if (!root.contains("terrains")) {
        err = "TerrainDefinitions.jsonc に terrains がありません";
        return false;
    }

    std::unordered_map<std::string, int> id_of_key;
    std::vector<std::pair<int, std::string>> all;
    for (const auto &node : root["terrains"]) {
        if (!node.contains("key") || !node.contains("id")) {
            continue;
        }
        const std::string key = node["key"].get<std::string>();
        const int id = node["id"].get<int>();
        id_of_key[key] = id;
        all.emplace_back(id, key);
    }

    // (a) 表の key が実在し、id が一致すること。**食い違いは表の書き間違い＝失敗。**
    for (const auto &[key, id] : this->mapped_keys_) {
        const auto found = id_of_key.find(key);
        if (found == id_of_key.end()) {
            err = "対応表の key \"" + key + "\" が TerrainDefinitions.jsonc にありません";
            return false;
        }
        if (found->second != id) {
            err = "対応表の \"" + key + "\" の id (" + std::to_string(id) + ") が本物 ("
                + std::to_string(found->second) + ") と食い違っています";
            return false;
        }
    }

    // (b) 取りこぼし（表に無い id）。役割の既定で描かれるだけなので**情報**。
    int unmapped = 0;
    std::string list;
    for (const auto &[id, key] : all) {
        if (this->rule_by_id_.find(id) != this->rule_by_id_.end()) {
            continue;
        }
        ++unmapped;
        if (!list.empty()) {
            list += " ";
        }
        list += key;
    }
    report = "対応 " + std::to_string(this->mapped_keys_.size()) + " 行 / 全 " + std::to_string(all.size())
        + " 地形。表に無いもの " + std::to_string(unmapped) + " 件（役割の既定で描く）: " + list;
    return true;
}

} // namespace hd2d
