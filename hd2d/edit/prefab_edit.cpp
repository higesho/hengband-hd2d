/*!
 * @file prefab_edit.cpp
 * @brief `prefab_edit.h` の実装。
 */
#include "edit/prefab_edit.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace hd2d {

namespace {

constexpr float kTiny = 1e-9f;

//! パーツ名に使える字か。`.vox` の節点名と `.jsonc` の鍵になるので欲張らない。
bool valid_part_name(const std::string &name)
{
    if (name.empty() || (name.size() > 32)) {
        return false;
    }
    for (const char c : name) {
        const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'))
            || (c == '_') || (c == '-');
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool name_in_use(const Prefab &prefab, const std::string &name, int except_index)
{
    for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
        if (static_cast<int>(i) == except_index) {
            continue;
        }
        if ((prefab.parts[i].name == name) || (prefab.parts[i].voxels == name)) {
            return true;
        }
    }
    return false;
}

//! 模型 `model_index` を参照しているパーツの数。
std::size_t model_reference_count(const Prefab &prefab, int model_index)
{
    std::size_t count = 0;
    for (const auto &part : prefab.parts) {
        count += (part.model_index == model_index) ? 1 : 0;
    }
    return count;
}

//! 4 成分の掛け算（`camera_project` 用。`Mat4` は列優先）。
void transform4(const Mat4 &m, const float in[4], float out[4])
{
    for (int row = 0; row < 4; ++row) {
        out[row] = (m.m[row] * in[0]) + (m.m[4 + row] * in[1]) + (m.m[8 + row] * in[2]) + (m.m[12 + row] * in[3]);
    }
}

} // namespace

/* ============================================================ カメラとレイ */

Vec3 camera_eye(const EditorCameraState &camera)
{
    return Vec3{ camera.center.x + (camera.distance * std::cos(camera.elevation) * std::cos(camera.azimuth)),
        camera.center.y + (camera.distance * std::cos(camera.elevation) * std::sin(camera.azimuth)),
        camera.center.z + (camera.distance * std::sin(camera.elevation)) };
}

Mat4 camera_view(const EditorCameraState &camera)
{
    return look_at(camera_eye(camera), camera.center, Vec3{ 0.f, 0.f, 1.f });
}

Mat4 camera_projection(const EditorCameraState &camera, float z_near, float z_far)
{
    return perspective_horizontal(camera.fov_x,
        static_cast<float>(camera.screen_w) / static_cast<float>(std::max(1, camera.screen_h)), z_near, z_far);
}

Ray camera_ray(const EditorCameraState &camera, float pixel_x, float pixel_y)
{
    const Vec3 eye = camera_eye(camera);
    const Vec3 f = normalize(camera.center - eye);
    // **左手系なので右 = 上 × 前**（`look_at` と同じ式でなければ、絵と当たりがずれる）。
    const Vec3 s = normalize(cross(Vec3{ 0.f, 0.f, 1.f }, f));
    const Vec3 u = cross(f, s);

    const float w = static_cast<float>(std::max(1, camera.screen_w));
    const float h = static_cast<float>(std::max(1, camera.screen_h));
    const float ndc_x = ((2.f * (pixel_x + 0.5f)) / w) - 1.f;
    const float ndc_y = 1.f - ((2.f * (pixel_y + 0.5f)) / h);
    const float tan_half = std::tan(camera.fov_x * 0.5f);
    // `perspective_horizontal` の逆読み: view.x = ndc.x·tan / 1、view.y = ndc.y·tan·h/w（分母が aspect）。
    const float vx = ndc_x * tan_half;
    const float vy = ndc_y * tan_half * (h / w);

    Ray ray;
    ray.origin = eye;
    ray.dir = normalize((s * vx) + (u * vy) + f);
    return ray;
}

bool camera_project(const EditorCameraState &camera, const Vec3 &world, float &pixel_x, float &pixel_y)
{
    const Mat4 vp = camera_projection(camera, 0.1f, 1000.f) * camera_view(camera);
    const float in[4] = { world.x, world.y, world.z, 1.f };
    float clip[4];
    transform4(vp, in, clip);
    if (clip[3] <= kTiny) {
        return false; // カメラの後ろ
    }
    const float ndc_x = clip[0] / clip[3];
    const float ndc_y = clip[1] / clip[3];
    pixel_x = (((ndc_x * 0.5f) + 0.5f) * static_cast<float>(camera.screen_w)) - 0.5f;
    pixel_y = (((0.5f - (ndc_y * 0.5f))) * static_cast<float>(camera.screen_h)) - 0.5f;
    return true;
}

/* ============================================================== レイ判定 */

bool raycast_part(const Prefab &prefab, int part_index, const Ray &ray, float max_t, VoxelHit &out)
{
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        return false;
    }
    const PrefabPart &part = prefab.parts[static_cast<std::size_t>(part_index)];
    const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
    const float vpc = static_cast<float>(prefab.voxels_per_cell);

    // ボクセル空間へ（媒介変数 t はマス空間と共通のまま）。
    const float origin[3] = { (ray.origin.x * vpc) - part.offset[0], (ray.origin.y * vpc) - part.offset[1],
        (ray.origin.z * vpc) - part.offset[2] };
    const float dir[3] = { ray.dir.x * vpc, ray.dir.y * vpc, ray.dir.z * vpc };

    // --- 箱 [0, size] へのクリップ（slab 法）。入った面の軸も覚える ---
    float t_min = 0.f;
    float t_max = max_t;
    int entry_axis = -1;
    bool inside = true;
    for (int k = 0; k < 3; ++k) {
        const float lo = 0.f;
        const float hi = static_cast<float>(model.size[k]);
        if ((origin[k] < lo) || (origin[k] > hi)) {
            inside = false;
        }
        if (std::abs(dir[k]) < kTiny) {
            if ((origin[k] < lo) || (origin[k] > hi)) {
                return false; // この軸に平行で、箱の外
            }
            continue;
        }
        float t0 = (lo - origin[k]) / dir[k];
        float t1 = (hi - origin[k]) / dir[k];
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        if (t0 > t_min) {
            t_min = t0;
            entry_axis = k;
        }
        t_max = std::min(t_max, t1);
    }
    if (t_min > t_max) {
        return false;
    }
    if (inside) {
        t_min = 0.f;
        entry_axis = -1; // 箱の中から始まった（入った面が無い）
    }

    // --- 格子行進（Amanatides & Woo） ---
    int cell[3];
    int step[3];
    float t_next[3];
    float t_delta[3];
    for (int k = 0; k < 3; ++k) {
        const float p = origin[k] + (dir[k] * t_min);
        cell[k] = std::clamp(static_cast<int>(std::floor(p)), 0, model.size[k] - 1);
        if (dir[k] > kTiny) {
            step[k] = 1;
            t_next[k] = (static_cast<float>(cell[k] + 1) - origin[k]) / dir[k];
            t_delta[k] = 1.f / dir[k];
        } else if (dir[k] < -kTiny) {
            step[k] = -1;
            t_next[k] = (static_cast<float>(cell[k]) - origin[k]) / dir[k];
            t_delta[k] = -1.f / dir[k];
        } else {
            step[k] = 0;
            t_next[k] = 1e30f;
            t_delta[k] = 1e30f;
        }
    }

    float t_entry = t_min;
    int last_axis = entry_axis;
    const int max_steps = model.size[0] + model.size[1] + model.size[2] + 3;
    for (int walked = 0; walked < max_steps; ++walked) {
        if (model.at(cell[0], cell[1], cell[2]) != 0) {
            if (t_entry > max_t) {
                return false;
            }
            out.part = part_index;
            out.voxel[0] = cell[0];
            out.voxel[1] = cell[1];
            out.voxel[2] = cell[2];
            out.normal[0] = out.normal[1] = out.normal[2] = 0;
            if (last_axis >= 0) {
                out.normal[last_axis] = -step[last_axis];
            } else {
                // 箱の中の詰まったボクセルから始まった。レイの主軸の逆を仮の面にする。
                const float ax = std::abs(ray.dir.x);
                const float ay = std::abs(ray.dir.y);
                const float az = std::abs(ray.dir.z);
                const int dominant = (ax >= ay) ? ((ax >= az) ? 0 : 2) : ((ay >= az) ? 1 : 2);
                out.normal[dominant] = (dir[dominant] > 0.f) ? -1 : 1;
            }
            out.t = t_entry;
            return true;
        }
        const int m = (t_next[0] <= t_next[1]) ? ((t_next[0] <= t_next[2]) ? 0 : 2) : ((t_next[1] <= t_next[2]) ? 1 : 2);
        t_entry = t_next[m];
        if ((t_entry > t_max) || (t_entry > max_t)) {
            return false;
        }
        cell[m] += step[m];
        if ((cell[m] < 0) || (cell[m] >= model.size[m])) {
            return false;
        }
        last_axis = m;
        t_next[m] += t_delta[m];
    }
    return false;
}

bool raycast_prefab(const Prefab &prefab, const Ray &ray, float max_t, VoxelHit &out)
{
    bool found = false;
    VoxelHit best;
    for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
        VoxelHit hit;
        if (!raycast_part(prefab, static_cast<int>(i), ray, max_t, hit)) {
            continue;
        }
        if (!found || (hit.t < best.t)) {
            best = hit;
            found = true;
        }
    }
    if (found) {
        out = best;
    }
    return found;
}

bool raycast_ground(const Ray &ray, float plane_z, Vec3 &point_out)
{
    if (std::abs(ray.dir.z) < kTiny) {
        return false;
    }
    const float t = (plane_z - ray.origin.z) / ray.dir.z;
    if (t < 0.f) {
        return false;
    }
    point_out = ray.origin + (ray.dir * t);
    return true;
}

/* ============================================================== 編集操作 */

void normalize_for_edit(Prefab &prefab, std::string &log)
{
    log.clear();

    // (1) 模型の共有を解く（1 パーツ 1 模型でないと、片方を彫ると両方が変わる）。
    for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
        auto &part = prefab.parts[i];
        bool shared = false;
        for (std::size_t j = 0; j < i; ++j) {
            shared = shared || (prefab.parts[j].model_index == part.model_index);
        }
        if (!shared) {
            continue;
        }
        prefab.vox.models.push_back(prefab.vox.models[static_cast<std::size_t>(part.model_index)]);
        part.model_index = static_cast<int>(prefab.vox.models.size() - 1);
        log += "パーツ \"" + part.name + "\" が模型を共有していたので複製しました\n";
    }

    // (2) 節点名の重複を直す（書き出しは節点名でしか区別できない）。
    for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
        auto &part = prefab.parts[i];
        if (part.voxels.empty()) {
            part.voxels = part.name.empty() ? ("part" + std::to_string(i)) : part.name;
        }
        std::string base = part.voxels;
        int serial = 2;
        while (name_in_use(prefab, part.voxels, static_cast<int>(i))) {
            part.voxels = base + "_" + std::to_string(serial);
            ++serial;
        }
        if (part.voxels != base) {
            log += "節点名 \"" + base + "\" が重複していたので \"" + part.voxels + "\" に直しました\n";
            if (part.name == base) {
                part.name = part.voxels;
            }
        }
    }
}

std::uint8_t get_voxel(const Prefab &prefab, int part_index, int x, int y, int z)
{
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        return 0;
    }
    const auto &part = prefab.parts[static_cast<std::size_t>(part_index)];
    const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
    if ((x < 0) || (y < 0) || (z < 0) || (x >= model.size[0]) || (y >= model.size[1]) || (z >= model.size[2])) {
        return 0;
    }
    return model.at(x, y, z);
}

bool set_voxel(Prefab &prefab, int part_index, int x, int y, int z, std::uint8_t color,
    std::string &err, int shift_out[3])
{
    if (shift_out != nullptr) {
        shift_out[0] = shift_out[1] = shift_out[2] = 0;
    }
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        err = "パーツの添字が不正です";
        return false;
    }
    auto &part = prefab.parts[static_cast<std::size_t>(part_index)];
    if (model_reference_count(prefab, part.model_index) > 1) {
        err = "模型が共有されています（normalize_for_edit を先に）";
        return false;
    }
    VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];

    const bool in_bounds = (x >= 0) && (y >= 0) && (z >= 0) && (x < model.size[0]) && (y < model.size[1]) && (z < model.size[2]);
    if (!in_bounds && (color == 0)) {
        return true; // 範囲の外を消す＝何もしない
    }
    if (!in_bounds) {
        // --- 育てる ---
        const int coord[3] = { x, y, z };
        int new_min[3];
        int new_max[3];
        for (int k = 0; k < 3; ++k) {
            new_min[k] = std::min(0, coord[k]);
            new_max[k] = std::max(model.size[k] - 1, coord[k]);
            const int new_size = new_max[k] - new_min[k] + 1;
            if (new_size > 256) {
                err = "これ以上は育てられません（`.vox` の一辺は 256 まで）";
                return false;
            }
        }
        VoxModel grown;
        for (int k = 0; k < 3; ++k) {
            grown.size[k] = new_max[k] - new_min[k] + 1;
        }
        grown.voxels.assign(static_cast<std::size_t>(grown.size[0]) * static_cast<std::size_t>(grown.size[1])
                * static_cast<std::size_t>(grown.size[2]),
            0);
        const int shift[3] = { -new_min[0], -new_min[1], -new_min[2] };
        for (int oz = 0; oz < model.size[2]; ++oz) {
            for (int oy = 0; oy < model.size[1]; ++oy) {
                for (int ox = 0; ox < model.size[0]; ++ox) {
                    const std::uint8_t v = model.at(ox, oy, oz);
                    if (v == 0) {
                        continue;
                    }
                    grown.voxels[static_cast<std::size_t>(ox + shift[0])
                        + (static_cast<std::size_t>(oy + shift[1]) * static_cast<std::size_t>(grown.size[0]))
                        + (static_cast<std::size_t>(oz + shift[2]) * static_cast<std::size_t>(grown.size[0])
                            * static_cast<std::size_t>(grown.size[1]))]
                        = v;
                }
            }
        }
        model = std::move(grown);
        // 負の側へ育ったぶんだけ最小隅が動く。プレハブ空間では何も動いていない。
        for (int k = 0; k < 3; ++k) {
            part.offset[k] += static_cast<float>(new_min[k]);
        }
        if (shift_out != nullptr) {
            shift_out[0] = shift[0];
            shift_out[1] = shift[1];
            shift_out[2] = shift[2];
        }
        x += shift[0];
        y += shift[1];
        z += shift[2];
    }

    model.voxels[static_cast<std::size_t>(x) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(model.size[0]))
        + (static_cast<std::size_t>(z) * static_cast<std::size_t>(model.size[0]) * static_cast<std::size_t>(model.size[1]))]
        = color;
    return true;
}

int add_part(Prefab &prefab, const std::string &name, std::string &err)
{
    if (!valid_part_name(name)) {
        err = "パーツ名に使える字は A-Za-z0-9_-（32 字まで）です";
        return -1;
    }
    if (name_in_use(prefab, name, -1)) {
        err = "その名前は既に使われています: " + name;
        return -1;
    }
    VoxModel model;
    model.size[0] = model.size[1] = model.size[2] = 1;
    model.voxels.assign(1, 0);
    prefab.vox.models.push_back(std::move(model));

    PrefabPart part;
    part.name = name;
    part.voxels = name;
    part.model_index = static_cast<int>(prefab.vox.models.size() - 1);
    part.parent = -1;
    part.grounded = true;
    part.wind_k = 0.f;
    prefab.parts.push_back(std::move(part));
    return static_cast<int>(prefab.parts.size() - 1);
}

bool rename_part(Prefab &prefab, int part_index, const std::string &new_name, std::string &err)
{
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        err = "パーツの添字が不正です";
        return false;
    }
    if (!valid_part_name(new_name)) {
        err = "パーツ名に使える字は A-Za-z0-9_-（32 字まで）です";
        return false;
    }
    if (name_in_use(prefab, new_name, part_index)) {
        err = "その名前は既に使われています: " + new_name;
        return false;
    }
    auto &part = prefab.parts[static_cast<std::size_t>(part_index)];
    part.name = new_name;
    part.voxels = new_name;
    return true;
}

bool delete_part(Prefab &prefab, int part_index, std::string &err)
{
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        err = "パーツの添字が不正です";
        return false;
    }
    if (prefab.parts.size() <= 1) {
        err = "最後のパーツは消せません";
        return false;
    }
    const int removed_model = prefab.parts[static_cast<std::size_t>(part_index)].model_index;
    if (model_reference_count(prefab, removed_model) > 1) {
        err = "模型が共有されています（normalize_for_edit を先に）";
        return false;
    }
    const int fallback_parent = prefab.parts[static_cast<std::size_t>(part_index)].parent;

    // 子は消すパーツの親へ付け替える（孤児にしない）。
    for (auto &part : prefab.parts) {
        if (part.parent == part_index) {
            part.parent = fallback_parent;
        }
    }
    prefab.parts.erase(prefab.parts.begin() + part_index);
    for (auto &part : prefab.parts) {
        if (part.parent > part_index) {
            --part.parent;
        }
        if (part.model_index > removed_model) {
            --part.model_index;
        }
    }
    prefab.vox.models.erase(prefab.vox.models.begin() + removed_model);
    return true;
}

bool move_part(Prefab &prefab, int part_index, int dx, int dy, int dz, std::string &err)
{
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        err = "パーツの添字が不正です";
        return false;
    }
    auto &part = prefab.parts[static_cast<std::size_t>(part_index)];
    part.offset[0] += static_cast<float>(dx);
    part.offset[1] += static_cast<float>(dy);
    part.offset[2] += static_cast<float>(dz);
    if (part.has_pivot) {
        // ピボットはパーツに付いている点（水車の軸）なので、一緒に動かす。
        part.pivot[0] += static_cast<float>(dx);
        part.pivot[1] += static_cast<float>(dy);
        part.pivot[2] += static_cast<float>(dz);
    }
    return true;
}

bool set_parent(Prefab &prefab, int part_index, int new_parent, std::string &err)
{
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        err = "パーツの添字が不正です";
        return false;
    }
    if (new_parent >= static_cast<int>(prefab.parts.size())) {
        err = "親の添字が不正です";
        return false;
    }
    if (new_parent == part_index) {
        err = "自分を親にはできません";
        return false;
    }
    // 循環の検査（§7.2-3 をロード前に守る側）。new_parent の先祖に自分がいたら断る。
    for (int cursor = new_parent; cursor >= 0; cursor = prefab.parts[static_cast<std::size_t>(cursor)].parent) {
        if (cursor == part_index) {
            err = "その親付けは循環になります（\"" + prefab.parts[static_cast<std::size_t>(part_index)].name
                + "\" の子孫を親にしようとしています）";
            return false;
        }
    }
    prefab.parts[static_cast<std::size_t>(part_index)].parent = std::max(-1, new_parent);
    return true;
}

int split_part(Prefab &prefab, int part_index, const int box_min[3], const int box_max[3],
    const std::string &new_name, std::string &err)
{
    if ((part_index < 0) || (static_cast<std::size_t>(part_index) >= prefab.parts.size())) {
        err = "パーツの添字が不正です";
        return -1;
    }
    if (!valid_part_name(new_name)) {
        err = "パーツ名に使える字は A-Za-z0-9_-（32 字まで）です";
        return -1;
    }
    if (name_in_use(prefab, new_name, -1)) {
        err = "その名前は既に使われています: " + new_name;
        return -1;
    }
    auto &src_part = prefab.parts[static_cast<std::size_t>(part_index)];
    if (model_reference_count(prefab, src_part.model_index) > 1) {
        err = "模型が共有されています（normalize_for_edit を先に）";
        return -1;
    }
    VoxModel &src = prefab.vox.models[static_cast<std::size_t>(src_part.model_index)];

    int lo[3];
    int hi[3];
    for (int k = 0; k < 3; ++k) {
        lo[k] = std::clamp(std::min(box_min[k], box_max[k]), 0, src.size[k] - 1);
        hi[k] = std::clamp(std::max(box_min[k], box_max[k]), 0, src.size[k] - 1);
    }

    // 選択の中の詰まっているボクセルのぴったりの箱を測る。
    int tight_lo[3] = { src.size[0], src.size[1], src.size[2] };
    int tight_hi[3] = { -1, -1, -1 };
    std::size_t selected = 0;
    std::size_t total = 0;
    for (int z = 0; z < src.size[2]; ++z) {
        for (int y = 0; y < src.size[1]; ++y) {
            for (int x = 0; x < src.size[0]; ++x) {
                if (src.at(x, y, z) == 0) {
                    continue;
                }
                ++total;
                const bool in_box = (x >= lo[0]) && (x <= hi[0]) && (y >= lo[1]) && (y <= hi[1]) && (z >= lo[2]) && (z <= hi[2]);
                if (!in_box) {
                    continue;
                }
                ++selected;
                tight_lo[0] = std::min(tight_lo[0], x);
                tight_lo[1] = std::min(tight_lo[1], y);
                tight_lo[2] = std::min(tight_lo[2], z);
                tight_hi[0] = std::max(tight_hi[0], x);
                tight_hi[1] = std::max(tight_hi[1], y);
                tight_hi[2] = std::max(tight_hi[2], z);
            }
        }
    }
    if (selected == 0) {
        err = "選択の中にボクセルがありません";
        return -1;
    }
    if (selected == total) {
        err = "選択がパーツ全体です（分割になりません。名前を変えたいだけならパーツ名の変更を）";
        return -1;
    }

    VoxModel moved;
    for (int k = 0; k < 3; ++k) {
        moved.size[k] = tight_hi[k] - tight_lo[k] + 1;
    }
    moved.voxels.assign(static_cast<std::size_t>(moved.size[0]) * static_cast<std::size_t>(moved.size[1])
            * static_cast<std::size_t>(moved.size[2]),
        0);
    for (int z = tight_lo[2]; z <= tight_hi[2]; ++z) {
        for (int y = tight_lo[1]; y <= tight_hi[1]; ++y) {
            for (int x = tight_lo[0]; x <= tight_hi[0]; ++x) {
                const bool in_box = (x >= lo[0]) && (x <= hi[0]) && (y >= lo[1]) && (y <= hi[1]) && (z >= lo[2]) && (z <= hi[2]);
                if (!in_box) {
                    continue;
                }
                const std::uint8_t v = src.at(x, y, z);
                if (v == 0) {
                    continue;
                }
                moved.voxels[static_cast<std::size_t>(x - tight_lo[0])
                    + (static_cast<std::size_t>(y - tight_lo[1]) * static_cast<std::size_t>(moved.size[0]))
                    + (static_cast<std::size_t>(z - tight_lo[2]) * static_cast<std::size_t>(moved.size[0])
                        * static_cast<std::size_t>(moved.size[1]))]
                    = v;
                src.voxels[static_cast<std::size_t>(x) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(src.size[0]))
                    + (static_cast<std::size_t>(z) * static_cast<std::size_t>(src.size[0]) * static_cast<std::size_t>(src.size[1]))]
                    = 0;
            }
        }
    }

    prefab.vox.models.push_back(std::move(moved));
    PrefabPart part;
    part.name = new_name;
    part.voxels = new_name;
    part.model_index = static_cast<int>(prefab.vox.models.size() - 1);
    for (int k = 0; k < 3; ++k) {
        part.offset[k] = src_part.offset[k] + static_cast<float>(tight_lo[k]);
    }
    // 親は元のパーツ（吊り看板を柱から切り出す形が既定）。動きと風は引き継ぐ。
    part.parent = part_index;
    part.grounded = (std::lround(part.offset[2]) == 0);
    part.motion = src_part.motion;
    part.wind_k = src_part.wind_k;
    prefab.parts.push_back(std::move(part));
    return static_cast<int>(prefab.parts.size() - 1);
}

std::size_t count_color_usage(const Prefab &prefab, std::uint8_t color)
{
    if (color == 0) {
        return 0;
    }
    std::size_t count = 0;
    std::set<int> counted;
    for (const auto &part : prefab.parts) {
        if (!counted.insert(part.model_index).second) {
            continue; // 共有模型を 2 度数えない
        }
        const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        for (const std::uint8_t v : model.voxels) {
            count += (v == color) ? 1 : 0;
        }
    }
    return count;
}

Prefab make_new_prefab(const std::string &name)
{
    Prefab prefab;
    prefab.name = name;
    prefab.voxels_per_cell = 32;
    prefab.anchor[0] = prefab.anchor[1] = 0;

    VoxModel model;
    model.size[0] = model.size[1] = model.size[2] = 1;
    model.voxels.assign(1, 0);
    prefab.vox.models.push_back(std::move(model));

    PrefabPart part;
    part.name = "main";
    part.voxels = "main";
    part.model_index = 0;
    part.parent = -1;
    part.grounded = true;
    part.wind_k = 0.f;
    prefab.parts.push_back(std::move(part));

    // 見やすい既定パレット: 1..16 = 灰の階段、17..232 = 6×6×6 の色の立方体、残り = 中間の灰。
    for (int i = 1; i < 256; ++i) {
        std::uint8_t r = 128;
        std::uint8_t g = 128;
        std::uint8_t b = 128;
        if (i <= 16) {
            const int v = 24 + ((i - 1) * 14);
            r = g = b = static_cast<std::uint8_t>(std::min(255, v));
        } else if (i <= 232) {
            static constexpr std::uint8_t kLevels[6] = { 0, 51, 102, 153, 204, 255 };
            const int cube = i - 17;
            r = kLevels[(cube / 36) % 6];
            g = kLevels[(cube / 6) % 6];
            b = kLevels[cube % 6];
        }
        prefab.vox.palette[i][0] = r;
        prefab.vox.palette[i][1] = g;
        prefab.vox.palette[i][2] = b;
        prefab.vox.palette[i][3] = 255;
    }
    return prefab;
}

} // namespace hd2d
