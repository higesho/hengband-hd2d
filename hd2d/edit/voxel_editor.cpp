/*!
 * @file voxel_editor.cpp
 * @brief `voxel_editor.h` の実装。
 *
 * ## 作りの要点
 * - **描画は本編の部品をそのまま使う**（`VoxelRenderer` / `ShadowMap` / `PostChain` /
 *   `UiPaint` / `TextOverlay`）。編集で見ている絵と本編の絵が別物にならない（P9 ⑤）。
 * - **編集の数学は `prefab_edit.cpp`**（GL 非依存）。ここは入力と表示だけを持つ。
 * - 元に戻す（Ctrl+Z）は**丸ごとの写し**で持つ。操作の逆演算を書き分けるより
 *   事故が少ない。1 筆 1 枚・上限 40 枚（いちばん重い mill でも数十 MB）。
 * - 目印（ホバー・選択・ピボット・footprint）は 1 ボクセルの白い箱 1 種類を
 *   拡大率と自発光で使い回す。**影のパスには入れない**（物ではないので）。
 */
#include "edit/voxel_editor.h"

#include "edit/prefab_edit.h"
#include "edit/prefab_writer.h"
#include "render/post_process.h"
#include "render/shadow_map.h"
#include "render/text_overlay.h"
#include "render/voxel_renderer.h"
#include "ui/ui_paint.h"
#include "voxel/part_motion.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace hd2d {

using namespace hd2d::gl;

namespace {

constexpr int kTextPx = 16;
constexpr int kShadowSide = 2048;
constexpr std::size_t kUndoLimit = 40;

const TextColor kHeader{ 1.f, 0.85f, 0.35f, 1.f };
const TextColor kDim{ 0.55f, 0.60f, 0.70f, 1.f };
const TextColor kBright{ 0.92f, 0.95f, 1.f, 1.f };
const TextColor kWarn{ 1.f, 0.50f, 0.40f, 1.f };
const TextColor kGood{ 0.55f, 0.95f, 0.55f, 1.f };

//! 道具。左クリックが何をするか。
enum class Tool {
    Place, //!< 置く（当たった面の隣。何も無ければ地面の上）
    Erase, //!< 消す
    Paint, //!< 塗る
    Pick, //!< スポイト
};

const char *tool_name(Tool tool)
{
    switch (tool) {
    case Tool::Place:
        return "B 置く";
    case Tool::Erase:
        return "X 消す";
    case Tool::Paint:
        return "C 塗る";
    case Tool::Pick:
    default:
        return "I スポイト";
    }
}

//! 目印用の 1 ボクセル（白）。拡大率と自発光で全部の目印を作る。
Prefab make_marker_prefab()
{
    Prefab prefab;
    prefab.name = "marker";
    prefab.voxels_per_cell = 32;
    VoxModel model;
    model.size[0] = model.size[1] = model.size[2] = 1;
    model.voxels.assign(1, 1);
    prefab.vox.models.push_back(std::move(model));
    PrefabPart part;
    part.name = "main";
    part.voxels = "main";
    part.model_index = 0;
    part.grounded = false;
    part.wind_k = 0.f;
    prefab.parts.push_back(std::move(part));
    prefab.vox.palette[1][0] = 255;
    prefab.vox.palette[1][1] = 255;
    prefab.vox.palette[1][2] = 255;
    prefab.vox.palette[1][3] = 255;
    return prefab;
}

//! 影を受ける地面（1 マス分の板。z = -2..0 ボクセル）。
Prefab make_ground_prefab()
{
    Prefab prefab;
    prefab.name = "ground";
    prefab.voxels_per_cell = 32;
    VoxModel model;
    model.size[0] = 32;
    model.size[1] = 32;
    model.size[2] = 2;
    model.voxels.assign(static_cast<std::size_t>(32) * 32 * 2, 1);
    prefab.vox.models.push_back(std::move(model));
    PrefabPart part;
    part.name = "main";
    part.voxels = "main";
    part.model_index = 0;
    part.offset[2] = -2.f;
    part.grounded = false;
    part.wind_k = 0.f;
    prefab.parts.push_back(std::move(part));
    prefab.vox.palette[1][0] = 255;
    prefab.vox.palette[1][1] = 255;
    prefab.vox.palette[1][2] = 255;
    prefab.vox.palette[1][3] = 255;
    return prefab;
}

//! `save_framebuffer_bmp`（hd2d_app.cpp）と同じ。エディタはリリース用ビルドに入らないので写しを持つ。
bool save_editor_shot(const std::string &path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    SDL_Surface *const surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
    if (surface == nullptr) {
        return false;
    }
    for (int y = 0; y < height; ++y) {
        const unsigned char *const src = pixels.data() + (static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4);
        auto *const dst = static_cast<unsigned char *>(surface->pixels) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch));
        std::memcpy(dst, src, static_cast<std::size_t>(width) * 4);
    }
    const bool ok = SDL_SaveBMP(surface, path.c_str()) == 0;
    SDL_FreeSurface(surface);
    if (ok) {
        std::fprintf(stderr, "[hd2d] wrote %s (%dx%d)\n", path.c_str(), width, height);
    }
    return ok;
}

//! メニューの行。毎フレーム作り直す（有効・無効と値の文字はその場で決まる）。
enum class RowId {
    ActivePart,
    Rename,
    NewPart,
    DeletePart,
    SplitSelection,
    MoveX,
    MoveY,
    MoveZ,
    Parent,
    Grounded,
    WindK,
    MotionKind,
    MotionAxis,
    MotionSpeed,
    MotionAmplitude,
    MotionPeriod,
    MotionPhase,
    MotionDamping,
    MotionState,
    PivotInfo,
    RefreshFootprint,
    PostToggle,
    Save,
    Revert,
    Close,
};

struct MenuRow {
    RowId id;
    std::string label;
    std::string value;
    bool adjustable{ false }; //!< ←→ で動かせる
    bool enabled{ true };
};

const char *motion_kind_label(MotionKind kind)
{
    switch (kind) {
    case MotionKind::Rotate:
        return "rotate（回転）";
    case MotionKind::Pendulum:
        return "pendulum（振り子）";
    case MotionKind::Wind:
        return "wind（風・頂点シェーダ）";
    case MotionKind::StateLinked:
        return "state（状態連動）";
    case MotionKind::Blink:
        return "blink（明滅）";
    case MotionKind::Static:
    default:
        return "static（静止）";
    }
}

std::string format_float(float value, const char *format = "%.2f")
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(value));
    return buffer;
}

//! 入力（名前）を求めている相手。
enum class InputPurpose {
    None,
    NewPart,
    RenamePart,
    SplitName,
    StateName,
};

struct Editor {
    const AppOptions *options{ nullptr };
    SDL_Window *window{ nullptr };

    TextOverlay text;
    UiPaint paint;
    VoxelRenderer renderer;
    PostChain post;
    ShadowMap shadow;

    Prefab prefab;
    std::string file_name; //!< `assets/voxel/<file_name>.{vox,jsonc}`
    bool is_new{ false };
    bool dirty{ false };
    bool mesh_dirty{ true };
    GpuPrefab gpu;
    bool gpu_ready{ false };
    GpuPrefab marker_gpu;
    GpuPrefab ground_gpu;

    EditorCameraState camera;
    int screen_w{ 1600 };
    int screen_h{ 900 };

    Tool tool{ Tool::Place };
    std::uint8_t color{ 1 };
    int active_part{ 0 };

    // ホバー（毎フレーム作り直す）
    bool hover_hit{ false }; //!< 実体に当たった
    VoxelHit hover;
    bool hover_place_valid{ false }; //!< 置く先（アクティブパーツのローカル）が有効
    int place_target[3]{};
    bool mouse_in_scene{ false };

    // 選択（分割用の箱。アクティブパーツのローカル）
    int sel_count{ 0 }; //!< 0 = 無し / 1 = 角 A だけ / 2 = 箱
    int sel_a[3]{};
    int sel_b[3]{};

    // 筆（マウスボタンを押している間が 1 筆 = 1 回の undo）
    bool stroke_active{ false };
    bool stroke_snapshot_done{ false };
    bool have_last_applied{ false };
    int last_applied[3]{};

    // 動きのプレビュー
    bool preview{ false };
    float motion_time{ 0.f };
    float state_value{ 0.f };
    float state_target{ 0.f };

    // 元に戻す
    std::deque<Prefab> undo_stack;
    std::deque<Prefab> redo_stack;

    // メニュー
    bool menu_open{ false };
    int menu_index{ 0 };
    RowId menu_last_adjusted{ RowId::Close };
    bool confirm_armed{ false };
    RowId confirm_row{ RowId::Close };

    // 名前の入力
    InputPurpose input_purpose{ InputPurpose::None };
    std::string input_buffer;

    // 表示
    std::string status;
    TextColor status_color{ kDim };
    int status_frames{ 0 };
    bool quit_armed{ false };
    bool post_all{ false };
    double fps{ 0.0 };

    // footprint の実体（形が変わったときだけ数え直す）
    std::vector<std::pair<int, int>> actual_footprint;

    // パレットのスライダ操作（1 回の掴みで 1 枚だけ undo を積む）
    int slider_drag{ -1 };
    bool slider_snapshot_done{ false };

    // 画面の部品の矩形（毎フレーム敷き直す）
    RectPx palette_rect;
    RectPx slider_rects[3];
    RectPx parts_rect;
    RectPx menu_rect;

    /* ---------------------------------------------------------- 小物 */

    void set_status(const std::string &message, const TextColor &colour, int frames = 240)
    {
        this->status = message;
        this->status_color = colour;
        this->status_frames = frames;
    }

    void snapshot()
    {
        this->undo_stack.push_back(this->prefab);
        if (this->undo_stack.size() > kUndoLimit) {
            this->undo_stack.pop_front();
        }
        this->redo_stack.clear();
    }

    void after_structure_change()
    {
        this->active_part = std::clamp(this->active_part, 0, static_cast<int>(this->prefab.parts.size()) - 1);
        this->sel_count = 0;
        this->have_last_applied = false;
        this->mesh_dirty = true;
        this->dirty = true;
    }

    void undo()
    {
        if (this->undo_stack.empty()) {
            this->set_status("戻すものがありません", kDim);
            return;
        }
        this->redo_stack.push_back(this->prefab);
        this->prefab = this->undo_stack.back();
        this->undo_stack.pop_back();
        this->after_structure_change();
        this->set_status("戻しました（残り " + std::to_string(this->undo_stack.size()) + "）", kDim);
    }

    void redo()
    {
        if (this->redo_stack.empty()) {
            this->set_status("やり直すものがありません", kDim);
            return;
        }
        this->undo_stack.push_back(this->prefab);
        this->prefab = this->redo_stack.back();
        this->redo_stack.pop_back();
        this->after_structure_change();
        this->set_status("やり直しました", kDim);
    }

    void save()
    {
        SaveReport report;
        std::string err;
        if (!save_prefab(this->options->voxel_dir, this->file_name, this->prefab, report, err)) {
            this->set_status("保存に失敗: " + err, kWarn, 600);
            std::fprintf(stderr, "[hd2d] 保存に失敗しました:\n%s\n", err.c_str());
            return;
        }
        this->dirty = false;
        this->is_new = false;
        this->mesh_dirty = true; // footprint の作り直しが宣言を変えたかもしれない（表示を新しく）
        std::string line = "保存しました: " + this->file_name + ".vox (" + std::to_string(report.vox_bytes)
            + " B) + .jsonc (" + std::to_string(report.jsonc_bytes) + " B)";
        if (report.footprint_refreshed) {
            line += "  footprint " + std::to_string(report.footprint_cells) + " マスに作り直し";
        }
        this->set_status(line, kGood, 360);
        std::fprintf(stderr, "[hd2d] %s\n", line.c_str());
    }

    void frame_camera()
    {
        Vec3 lo{ 0.f, 0.f, 0.f };
        Vec3 hi{ 1.f, 1.f, 1.f };
        const float scale = 1.f / static_cast<float>(std::max(1, this->prefab.voxels_per_cell));
        bool first = true;
        for (const auto &part : this->prefab.parts) {
            const VoxModel &model = this->prefab.vox.models[static_cast<std::size_t>(part.model_index)];
            const Vec3 part_lo{ part.offset[0] * scale, part.offset[1] * scale, part.offset[2] * scale };
            const Vec3 part_hi{ part_lo.x + (static_cast<float>(model.size[0]) * scale),
                part_lo.y + (static_cast<float>(model.size[1]) * scale), part_lo.z + (static_cast<float>(model.size[2]) * scale) };
            if (first) {
                lo = part_lo;
                hi = part_hi;
                first = false;
                continue;
            }
            lo.x = std::min(lo.x, part_lo.x);
            lo.y = std::min(lo.y, part_lo.y);
            lo.z = std::min(lo.z, part_lo.z);
            hi.x = std::max(hi.x, part_hi.x);
            hi.y = std::max(hi.y, part_hi.y);
            hi.z = std::max(hi.z, part_hi.z);
        }
        this->bounds_lo = lo;
        this->bounds_hi = hi;
        this->camera.center = Vec3{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
        const Vec3 extent = hi - lo;
        const float radius = std::max(0.75f, 0.5f * std::sqrt(dot(extent, extent)));
        const float half_x = this->camera.fov_x * 0.5f;
        const float half_y = std::atan(std::tan(half_x) * static_cast<float>(this->screen_h)
            / static_cast<float>(std::max(1, this->screen_w)));
        this->camera.distance = (radius / std::sin(std::min(half_x, half_y))) * 1.12f;
    }

    Vec3 bounds_lo{};
    Vec3 bounds_hi{};

    //! 形が変わっていたら、メッシュと footprint の実体を作り直す。
    void remesh_if_dirty()
    {
        if (!this->mesh_dirty) {
            return;
        }
        this->mesh_dirty = false;
        this->actual_footprint = compute_actual_footprint(this->prefab);

        GpuPrefab fresh;
        std::string err;
        if (!this->renderer.upload(this->prefab, fresh, err)) {
            this->set_status("メッシュ化に失敗: " + err, kWarn, 600);
            return;
        }
        if (this->gpu_ready) {
            this->renderer.release(this->gpu);
        }
        this->gpu = fresh;
        this->gpu_ready = true;

        // 外接箱もここで測り直す（地面と影の範囲に使う。カメラは動かさない）。
        const float scale = 1.f / static_cast<float>(std::max(1, this->prefab.voxels_per_cell));
        Vec3 lo{ 0.f, 0.f, 0.f };
        Vec3 hi{ 1.f, 1.f, 1.f };
        bool first = true;
        for (const auto &part : this->prefab.parts) {
            const VoxModel &model = this->prefab.vox.models[static_cast<std::size_t>(part.model_index)];
            const Vec3 part_lo{ part.offset[0] * scale, part.offset[1] * scale, part.offset[2] * scale };
            const Vec3 part_hi{ part_lo.x + (static_cast<float>(model.size[0]) * scale),
                part_lo.y + (static_cast<float>(model.size[1]) * scale), part_lo.z + (static_cast<float>(model.size[2]) * scale) };
            if (first) {
                lo = part_lo;
                hi = part_hi;
                first = false;
                continue;
            }
            lo.x = std::min(lo.x, part_lo.x);
            lo.y = std::min(lo.y, part_lo.y);
            lo.z = std::min(lo.z, part_lo.z);
            hi.x = std::max(hi.x, part_hi.x);
            hi.y = std::max(hi.y, part_hi.y);
            hi.z = std::max(hi.z, part_hi.z);
        }
        this->bounds_lo = lo;
        this->bounds_hi = hi;
    }

    /* ------------------------------------------------------ 画面の敷き */

    void layout()
    {
        SDL_GetWindowSize(this->window, &this->screen_w, &this->screen_h);
        this->camera.screen_w = this->screen_w;
        this->camera.screen_h = this->screen_h;

        // パレット: 左下。16×16 のマス（12px ＋ 1px の隙間）。
        const int swatch_side = (16 * 13) + 1;
        this->palette_rect = RectPx{ 8, this->screen_h - swatch_side - 30, swatch_side, swatch_side };
        for (int k = 0; k < 3; ++k) {
            this->slider_rects[k] = RectPx{ this->palette_rect.x + swatch_side + 14,
                this->palette_rect.y + 24 + (k * 26), 150, 16 };
        }
        // パーツ一覧: 右上。
        const int parts_h = 26 + (static_cast<int>(this->prefab.parts.size()) * 20);
        this->parts_rect = RectPx{ this->screen_w - 248, 108, 240, parts_h };
        // メニュー: 中央。
        const int menu_w = 470;
        const int menu_h = 60 + (static_cast<int>(this->menu_rows().size()) * 20);
        this->menu_rect = RectPx{ (this->screen_w - menu_w) / 2, std::max(60, (this->screen_h - menu_h) / 2), menu_w, menu_h };
    }

    bool mouse_over_ui(int mx, int my) const
    {
        if (this->menu_open || (this->input_purpose != InputPurpose::None)) {
            return true;
        }
        const RectPx slider_zone{ this->slider_rects[0].x - 4, this->slider_rects[0].y - 24, 260, 110 };
        return this->palette_rect.contains(mx, my) || slider_zone.contains(mx, my) || this->parts_rect.contains(mx, my);
    }

    /* ------------------------------------------------------ ホバーと道具 */

    void update_hover(int mx, int my)
    {
        this->hover_hit = false;
        this->hover_place_valid = false;
        this->mouse_in_scene = !this->mouse_over_ui(mx, my);
        if (!this->mouse_in_scene || this->preview) {
            return; // プレビュー中は編集しない（動いている絵に当てても意味が違ってしまう）
        }
        const Ray ray = camera_ray(this->camera, static_cast<float>(mx), static_cast<float>(my));
        VoxelHit hit;
        if (raycast_prefab(this->prefab, ray, 1000.f, hit)) {
            this->hover_hit = true;
            this->hover = hit;
            if (hit.part == this->active_part) {
                this->place_target[0] = hit.voxel[0] + hit.normal[0];
                this->place_target[1] = hit.voxel[1] + hit.normal[1];
                this->place_target[2] = hit.voxel[2] + hit.normal[2];
                this->hover_place_valid = true;
            }
            return;
        }
        // 実体に当たらなければ地面。**アクティブパーツのローカル**に直して置く先にする。
        Vec3 point;
        if (!raycast_ground(ray, 0.f, point)) {
            return;
        }
        const auto &part = this->prefab.parts[static_cast<std::size_t>(this->active_part)];
        const float vpc = static_cast<float>(this->prefab.voxels_per_cell);
        this->place_target[0] = static_cast<int>(std::floor(point.x * vpc)) - static_cast<int>(std::lround(part.offset[0]));
        this->place_target[1] = static_cast<int>(std::floor(point.y * vpc)) - static_cast<int>(std::lround(part.offset[1]));
        this->place_target[2] = -static_cast<int>(std::lround(part.offset[2]));
        this->hover_place_valid = true;
    }

    void begin_stroke()
    {
        this->stroke_active = true;
        this->stroke_snapshot_done = false;
        this->have_last_applied = false;
    }

    void apply_tool(bool erase_override)
    {
        if (this->preview) {
            this->set_status("プレビュー中は編集できません（空白キーで止める）", kDim);
            return;
        }
        const Tool tool_now = erase_override ? Tool::Erase : this->tool;
        int target[3];
        int target_part = this->active_part;
        switch (tool_now) {
        case Tool::Place:
            if (!this->hover_place_valid) {
                return;
            }
            target[0] = this->place_target[0];
            target[1] = this->place_target[1];
            target[2] = this->place_target[2];
            break;
        case Tool::Erase:
        case Tool::Paint:
            if (!this->hover_hit) {
                return;
            }
            if (this->hover.part != this->active_part) {
                this->set_status("そこはアクティブなパーツではありません（, . で切り替え）", kDim);
                return;
            }
            target[0] = this->hover.voxel[0];
            target[1] = this->hover.voxel[1];
            target[2] = this->hover.voxel[2];
            break;
        case Tool::Pick:
        default:
            if (!this->hover_hit) {
                return;
            }
            this->color = get_voxel(this->prefab, this->hover.part, this->hover.voxel[0], this->hover.voxel[1], this->hover.voxel[2]);
            this->set_status("スポイト: 索引 " + std::to_string(this->color), kDim);
            return;
        }

        // 同じ筆の中で同じ所へ 2 度掛けない（ドラッグで 1 マスに連打されるのを避ける）。
        if (this->have_last_applied && (this->last_applied[0] == target[0]) && (this->last_applied[1] == target[1])
            && (this->last_applied[2] == target[2])) {
            return;
        }
        if (!this->stroke_snapshot_done) {
            this->snapshot();
            this->stroke_snapshot_done = true;
        }
        std::string err;
        int shift[3] = {};
        const std::uint8_t value = (tool_now == Tool::Erase) ? 0 : this->color;
        if ((tool_now == Tool::Paint)
            && (get_voxel(this->prefab, target_part, target[0], target[1], target[2]) == 0)) {
            return; // 塗るのは在るものだけ（空へ塗ると「置く」と区別が付かない）
        }
        if (!set_voxel(this->prefab, target_part, target[0], target[1], target[2], value, err, shift)) {
            this->set_status(err, kWarn);
            return;
        }
        if ((shift[0] | shift[1] | shift[2]) != 0) {
            // 育ってローカル座標がずれた。手元の座標は全部捨てる（選択も筆の記憶も）。
            this->sel_count = 0;
            this->have_last_applied = false;
        } else {
            this->last_applied[0] = target[0];
            this->last_applied[1] = target[1];
            this->last_applied[2] = target[2];
            this->have_last_applied = true;
        }
        this->mesh_dirty = true;
        this->dirty = true;
    }

    void set_pivot_at_hover()
    {
        if (!this->hover_hit || (this->hover.part != this->active_part)) {
            this->set_status("ピボットはアクティブパーツのボクセルへホバーして P", kDim);
            return;
        }
        this->snapshot();
        auto &part = this->prefab.parts[static_cast<std::size_t>(this->active_part)];
        part.has_pivot = true;
        for (int k = 0; k < 3; ++k) {
            part.pivot[k] = part.offset[k] + static_cast<float>(this->hover.voxel[k]) + 0.5f;
        }
        this->dirty = true;
        this->set_status("ピボット = (" + format_float(part.pivot[0], "%.1f") + ", " + format_float(part.pivot[1], "%.1f")
                + ", " + format_float(part.pivot[2], "%.1f") + ")",
            kGood);
    }

    void set_selection_corner()
    {
        if (!this->hover_hit || (this->hover.part != this->active_part)) {
            this->set_status("選択はアクティブパーツのボクセルへホバーして V", kDim);
            return;
        }
        if (this->sel_count == 1) {
            for (int k = 0; k < 3; ++k) {
                this->sel_b[k] = this->hover.voxel[k];
            }
            this->sel_count = 2;
            this->set_status("選択の箱を閉じました（メニューの「選択を分割」へ）", kGood);
        } else {
            for (int k = 0; k < 3; ++k) {
                this->sel_a[k] = this->hover.voxel[k];
                this->sel_b[k] = this->hover.voxel[k];
            }
            this->sel_count = 1;
            this->set_status("角 A を置きました。もう 1 つの角で V", kDim);
        }
    }

    /* -------------------------------------------------------- メニュー */

    std::vector<MenuRow> menu_rows() const
    {
        std::vector<MenuRow> rows;
        const auto &part = this->prefab.parts[static_cast<std::size_t>(this->active_part)];
        rows.push_back({ RowId::ActivePart, "パーツ", part.name + "（" + std::to_string(this->active_part + 1) + "/"
                + std::to_string(this->prefab.parts.size()) + "）", true, true });
        rows.push_back({ RowId::Rename, "名前を変える…", "", false, true });
        rows.push_back({ RowId::NewPart, "新しいパーツ…", "", false, true });
        rows.push_back({ RowId::DeletePart, "このパーツを消す", "", false, this->prefab.parts.size() > 1 });
        rows.push_back({ RowId::SplitSelection, "選択を分割…", (this->sel_count == 2) ? "箱あり" : "箱なし（V で選ぶ）",
            false, this->sel_count == 2 });
        rows.push_back({ RowId::MoveX, "動かす x", format_float(part.offset[0], "%.0f"), true, true });
        rows.push_back({ RowId::MoveY, "動かす y", format_float(part.offset[1], "%.0f"), true, true });
        rows.push_back({ RowId::MoveZ, "動かす z", format_float(part.offset[2], "%.0f"), true, true });
        rows.push_back({ RowId::Parent, "親",
            (part.parent < 0) ? "（なし）" : this->prefab.parts[static_cast<std::size_t>(part.parent)].name, true, true });
        rows.push_back({ RowId::Grounded, "接地（footprint に入る）", part.grounded ? "はい" : "いいえ", true, true });
        rows.push_back({ RowId::WindK, "風のしなやかさ wind_k", format_float(part.wind_k), true, true });
        rows.push_back({ RowId::MotionKind, "動き", motion_kind_label(part.motion.kind), true, true });
        const bool rotate = (part.motion.kind == MotionKind::Rotate);
        const bool pendulum = (part.motion.kind == MotionKind::Pendulum);
        if (rotate || pendulum) {
            const char axis_letter[3] = { 'x', 'y', 'z' };
            rows.push_back({ RowId::MotionAxis, "  軸", std::string(1, axis_letter[std::clamp(part.motion.axis, 0, 2)]), true, true });
        }
        if (rotate) {
            rows.push_back({ RowId::MotionSpeed, "  速さ（回/秒）", format_float(part.motion.speed), true, true });
        }
        if (pendulum) {
            rows.push_back({ RowId::MotionAmplitude, "  振幅（度）", format_float(part.motion.amplitude, "%.1f"), true, true });
            rows.push_back({ RowId::MotionPeriod, "  周期（秒）", format_float(part.motion.period), true, true });
            rows.push_back({ RowId::MotionDamping, "  減衰", format_float(part.motion.damping), true, true });
        }
        if (rotate || pendulum) {
            rows.push_back({ RowId::MotionPhase, "  位相", format_float(part.motion.phase), true, true });
        }
        if (part.motion.kind == MotionKind::StateLinked) {
            rows.push_back({ RowId::MotionState, "  状態の名前…", part.motion.state_name.empty() ? "（未設定）" : part.motion.state_name,
                false, true });
        }
        std::string pivot_text = "（なし。ホバーして P）";
        if (part.has_pivot) {
            pivot_text = "(" + format_float(part.pivot[0], "%.1f") + ", " + format_float(part.pivot[1], "%.1f") + ", "
                + format_float(part.pivot[2], "%.1f") + ")";
        }
        rows.push_back({ RowId::PivotInfo, "ピボット", pivot_text, false, false });
        rows.push_back({ RowId::RefreshFootprint, "footprint を実体から作り直す",
            std::to_string(this->prefab.footprint.size()) + " → " + std::to_string(this->actual_footprint.size()) + " マス",
            false, true });
        rows.push_back({ RowId::PostToggle, "ポスト処理（本編の効果）", this->post_all ? "全部入り" : "off（素の色）", true, true });
        rows.push_back({ RowId::Save, "保存（Ctrl+S）", this->dirty ? "未保存の変更あり" : "変更なし", false, true });
        rows.push_back({ RowId::Revert, "読み直す（変更を捨てる）", "", false, !this->is_new });
        rows.push_back({ RowId::Close, "閉じる（Tab）", "", false, true });
        return rows;
    }

    //! ←→ の調整行。**行ごとに最初の 1 回だけ**写しを積む（連打で undo が溢れないように）。
    void snapshot_for_adjust(RowId row)
    {
        if (this->menu_last_adjusted != row) {
            this->snapshot();
            this->menu_last_adjusted = row;
        }
    }

    void adjust_row(const MenuRow &row, int direction, bool big)
    {
        auto &part = this->prefab.parts[static_cast<std::size_t>(this->active_part)];
        const float dir_f = static_cast<float>(direction);
        switch (row.id) {
        case RowId::ActivePart: {
            const int count = static_cast<int>(this->prefab.parts.size());
            this->active_part = ((this->active_part + direction) % count + count) % count;
            this->sel_count = 0;
            this->menu_last_adjusted = RowId::Close;
            return; // パーツ切替は編集ではない（undo に積まない）
        }
        case RowId::MoveX:
        case RowId::MoveY:
        case RowId::MoveZ: {
            this->snapshot_for_adjust(row.id);
            const int step = direction * (big ? 8 : 1);
            std::string err;
            (void)move_part(this->prefab, this->active_part,
                (row.id == RowId::MoveX) ? step : 0, (row.id == RowId::MoveY) ? step : 0, (row.id == RowId::MoveZ) ? step : 0, err);
            this->mesh_dirty = true;
            this->dirty = true;
            return;
        }
        case RowId::Parent: {
            // -1（なし）と自分以外を順に回す。循環になる相手は set_parent が断るので飛ばす。
            const int count = static_cast<int>(this->prefab.parts.size());
            int candidate = part.parent;
            for (int attempts = 0; attempts <= count; ++attempts) {
                candidate += direction;
                if (candidate >= count) {
                    candidate = -1;
                } else if (candidate < -1) {
                    candidate = count - 1;
                }
                if (candidate == this->active_part) {
                    continue;
                }
                std::string err;
                Prefab probe = this->prefab; // 実物を触る前に試す（断られたら何も変えない）
                if (set_parent(probe, this->active_part, candidate, err)) {
                    this->snapshot_for_adjust(row.id);
                    (void)set_parent(this->prefab, this->active_part, candidate, err);
                    this->dirty = true;
                    return;
                }
            }
            this->set_status("付け替えられる親がありません", kDim);
            return;
        }
        case RowId::Grounded:
            this->snapshot_for_adjust(row.id);
            part.grounded = !part.grounded;
            this->mesh_dirty = true; // footprint の実体が変わる
            this->dirty = true;
            return;
        case RowId::WindK:
            this->snapshot_for_adjust(row.id);
            part.wind_k = std::clamp(part.wind_k + (dir_f * (big ? 0.25f : 0.05f)), 0.f, 4.f);
            this->mesh_dirty = true; // wind_k は GPU のパーツに焼かれている（upload で写す）
            this->dirty = true;
            return;
        case RowId::MotionKind: {
            this->snapshot_for_adjust(row.id);
            static constexpr MotionKind kOrder[5] = { MotionKind::Static, MotionKind::Rotate, MotionKind::Pendulum,
                MotionKind::Wind, MotionKind::StateLinked };
            int index = 0;
            for (int i = 0; i < 5; ++i) {
                if (kOrder[i] == part.motion.kind) {
                    index = i;
                }
            }
            index = ((index + direction) % 5 + 5) % 5;
            part.motion.kind = kOrder[index];
            if ((part.motion.kind == MotionKind::Rotate) && (part.motion.speed == 0.f)) {
                part.motion.speed = 0.5f; // 動きが見えない既定値は「壊れている」と区別できない
            }
            if ((part.motion.kind == MotionKind::Pendulum) && (part.motion.period <= 0.f)) {
                part.motion.amplitude = (part.motion.amplitude == 0.f) ? 10.f : part.motion.amplitude;
                part.motion.period = 2.f;
            }
            this->dirty = true;
            return;
        }
        case RowId::MotionAxis:
            this->snapshot_for_adjust(row.id);
            part.motion.axis = ((part.motion.axis + direction) % 3 + 3) % 3;
            this->dirty = true;
            return;
        case RowId::MotionSpeed:
            this->snapshot_for_adjust(row.id);
            part.motion.speed = std::clamp(part.motion.speed + (dir_f * (big ? 0.25f : 0.05f)), -8.f, 8.f);
            this->dirty = true;
            return;
        case RowId::MotionAmplitude:
            this->snapshot_for_adjust(row.id);
            part.motion.amplitude = std::clamp(part.motion.amplitude + (dir_f * (big ? 5.f : 1.f)), 0.f, 180.f);
            this->dirty = true;
            return;
        case RowId::MotionPeriod:
            this->snapshot_for_adjust(row.id);
            part.motion.period = std::clamp(part.motion.period + (dir_f * (big ? 0.5f : 0.1f)), 0.1f, 30.f);
            this->dirty = true;
            return;
        case RowId::MotionPhase:
            this->snapshot_for_adjust(row.id);
            part.motion.phase = part.motion.phase + (dir_f * (big ? 0.5f : 0.1f));
            this->dirty = true;
            return;
        case RowId::MotionDamping:
            this->snapshot_for_adjust(row.id);
            part.motion.damping = std::clamp(part.motion.damping + (dir_f * (big ? 0.25f : 0.05f)), 0.f, 4.f);
            this->dirty = true;
            return;
        case RowId::PostToggle:
            this->post_all = !this->post_all;
            return;
        default:
            return;
        }
    }

    void activate_row(const MenuRow &row)
    {
        switch (row.id) {
        case RowId::Rename:
            this->input_purpose = InputPurpose::RenamePart;
            this->input_buffer = this->prefab.parts[static_cast<std::size_t>(this->active_part)].name;
            SDL_StartTextInput();
            return;
        case RowId::NewPart:
            this->input_purpose = InputPurpose::NewPart;
            this->input_buffer.clear();
            SDL_StartTextInput();
            return;
        case RowId::SplitSelection:
            if (this->sel_count != 2) {
                this->set_status("先に V で箱を選ぶこと", kDim);
                return;
            }
            this->input_purpose = InputPurpose::SplitName;
            this->input_buffer.clear();
            SDL_StartTextInput();
            return;
        case RowId::MotionState:
            this->input_purpose = InputPurpose::StateName;
            this->input_buffer = this->prefab.parts[static_cast<std::size_t>(this->active_part)].motion.state_name;
            SDL_StartTextInput();
            return;
        case RowId::DeletePart: {
            if (!this->arm_confirm(RowId::DeletePart, "もう一度 Enter でパーツ \""
                    + this->prefab.parts[static_cast<std::size_t>(this->active_part)].name + "\" を消します")) {
                return;
            }
            this->snapshot();
            std::string err;
            if (!delete_part(this->prefab, this->active_part, err)) {
                this->undo_stack.pop_back();
                this->set_status(err, kWarn);
                return;
            }
            this->after_structure_change();
            this->set_status("パーツを消しました", kGood);
            return;
        }
        case RowId::RefreshFootprint:
            this->snapshot();
            this->prefab.footprint = compute_actual_footprint(this->prefab);
            this->dirty = true;
            this->set_status("footprint を " + std::to_string(this->prefab.footprint.size()) + " マスに作り直しました"
                    + "（保存時にも作り直される）",
                kGood);
            return;
        case RowId::Save:
            this->save();
            return;
        case RowId::Revert: {
            if (!this->arm_confirm(RowId::Revert, "もう一度 Enter で保存していない変更を捨てて読み直します")) {
                return;
            }
            Prefab reloaded;
            std::string err;
            if (!load_prefab(this->options->voxel_dir, this->file_name, reloaded, err)) {
                this->set_status("読み直せませんでした: " + err, kWarn, 600);
                return;
            }
            std::string log;
            normalize_for_edit(reloaded, log);
            this->prefab = std::move(reloaded);
            this->undo_stack.clear();
            this->redo_stack.clear();
            this->dirty = !log.empty();
            this->after_structure_change();
            this->dirty = !log.empty();
            this->set_status("読み直しました", kGood);
            return;
        }
        case RowId::Close:
            this->menu_open = false;
            return;
        default:
            return;
        }
    }

    //! 2 度押しの確認。1 度目は警告だけ出して待つ。
    bool arm_confirm(RowId row, const std::string &warning)
    {
        if (this->confirm_armed && (this->confirm_row == row)) {
            this->confirm_armed = false;
            return true;
        }
        this->confirm_armed = true;
        this->confirm_row = row;
        this->set_status(warning, kWarn);
        return false;
    }

    void commit_input()
    {
        const InputPurpose purpose = this->input_purpose;
        this->input_purpose = InputPurpose::None;
        SDL_StopTextInput();
        const std::string name = this->input_buffer;
        std::string err;
        switch (purpose) {
        case InputPurpose::NewPart: {
            this->snapshot();
            const int index = add_part(this->prefab, name, err);
            if (index < 0) {
                this->undo_stack.pop_back();
                this->set_status(err, kWarn);
                return;
            }
            this->active_part = index;
            this->after_structure_change();
            this->set_status("パーツ \"" + name + "\" を作りました。地面をクリックして最初のボクセルを置くこと", kGood, 480);
            return;
        }
        case InputPurpose::RenamePart:
            this->snapshot();
            if (!rename_part(this->prefab, this->active_part, name, err)) {
                this->undo_stack.pop_back();
                this->set_status(err, kWarn);
                return;
            }
            this->dirty = true;
            this->set_status("名前を \"" + name + "\" にしました", kGood);
            return;
        case InputPurpose::SplitName: {
            this->snapshot();
            const int index = split_part(this->prefab, this->active_part, this->sel_a, this->sel_b, name, err);
            if (index < 0) {
                this->undo_stack.pop_back();
                this->set_status(err, kWarn);
                return;
            }
            this->active_part = index;
            this->after_structure_change();
            this->set_status("選択を \"" + name + "\" へ分割しました（親 = 元のパーツ）", kGood, 480);
            return;
        }
        case InputPurpose::StateName: {
            this->snapshot();
            auto &part = this->prefab.parts[static_cast<std::size_t>(this->active_part)];
            part.motion.state_name = name;
            this->dirty = true;
            this->set_status("状態の名前を \"" + name + "\" にしました（O で 0⇄1 を見られる）", kGood);
            return;
        }
        case InputPurpose::None:
        default:
            return;
        }
    }
};

} // namespace

int run_voxel_editor(const AppOptions &options, SDL_Window *window)
{
    Editor editor;
    editor.options = &options;
    editor.window = window;
    const bool want_new = !options.edit_new_name.empty();
    editor.file_name = want_new ? options.edit_new_name : options.edit_name;

    /* ------------------------------------------------------ 読み込み */
    {
        const std::string separator = "/";
        const std::string jsonc_path = options.voxel_dir + separator + editor.file_name + ".jsonc";
        std::FILE *const probe = std::fopen(jsonc_path.c_str(), "rb");
        const bool exists = (probe != nullptr);
        if (probe != nullptr) {
            std::fclose(probe);
        }
        std::string err;
        if (want_new) {
            if (exists) {
                std::fprintf(stderr, "[hd2d] %s は既にあります。開くなら --edit=%s\n", jsonc_path.c_str(), editor.file_name.c_str());
                return 1;
            }
            editor.prefab = make_new_prefab(editor.file_name);
            editor.is_new = true;
            editor.dirty = true;
        } else {
            if (!load_prefab(options.voxel_dir, editor.file_name, editor.prefab, err)) {
                std::fprintf(stderr, "[hd2d] プレハブを読めませんでした: %s\n  新しく作るなら --edit-new=%s\n",
                    err.c_str(), editor.file_name.c_str());
                return 1;
            }
        }
        std::string log;
        normalize_for_edit(editor.prefab, log);
        if (!log.empty()) {
            std::fprintf(stderr, "[hd2d] 正規化: %s", log.c_str());
            editor.dirty = true;
        }
    }
    // 最初の色 = いちばん使われている色（新規なら灰の中ほど）。
    {
        std::size_t best = 0;
        for (int c = 1; c < 256; ++c) {
            const std::size_t used = count_color_usage(editor.prefab, static_cast<std::uint8_t>(c));
            if (used > best) {
                best = used;
                editor.color = static_cast<std::uint8_t>(c);
            }
        }
        if (best == 0) {
            editor.color = 8;
        }
    }
    const char *const break_env = std::getenv("HD2D_BREAK_EDIT");
    if ((break_env != nullptr) && (break_env[0] != '\0')) {
        std::fprintf(stderr, "[hd2d] **HD2D_BREAK_EDIT=%s が立ったままです。**保存が壊れます（検査の検査の口）\n", break_env);
    }

    /* ------------------------------------------------------ GL の部品 */
    std::string err;
    if (!editor.text.init(kTextPx, err) || !editor.paint.init(err) || !editor.renderer.init(err) || !editor.post.init(err)) {
        std::fprintf(stderr, "[hd2d] エディタの部品を用意できませんでした: %s\n", err.c_str());
        return 1;
    }
    if (!editor.post.set_grade(GradeParams{}, err)) {
        std::fprintf(stderr, "[hd2d] LUT を焼けませんでした（グレーディング無しで続けます）: %s\n", err.c_str());
    }
    if (!editor.shadow.init(kShadowSide, err)) {
        std::fprintf(stderr, "[hd2d] 影を用意できませんでした（影なしで続けます）: %s\n", err.c_str());
    }
    Prefab marker_prefab = make_marker_prefab();
    Prefab ground_prefab = make_ground_prefab();
    if (!editor.renderer.upload(marker_prefab, editor.marker_gpu, err)
        || !editor.renderer.upload(ground_prefab, editor.ground_gpu, err)) {
        std::fprintf(stderr, "[hd2d] 目印を GPU へ載せられませんでした: %s\n", err.c_str());
        return 1;
    }

    SDL_GetWindowSize(window, &editor.screen_w, &editor.screen_h);
    editor.camera.screen_w = editor.screen_w;
    editor.camera.screen_h = editor.screen_h;
    editor.remesh_if_dirty();
    editor.frame_camera();
    editor.set_status("Tab = メニュー / B X C I = 道具 / V = 選択 / P = ピボット / Ctrl+S = 保存", kDim, 600);

    /* ------------------------------------------------------ 駆動ループ */
    Uint64 last_tick = SDL_GetTicks64();
    Uint64 fps_mark = last_tick;
    int fps_frames = 0;
    int frame_index = 0;
    bool middle_drag = false;
    std::vector<Mat4> part_motions;
    std::vector<InstanceData> markers;
    std::vector<InstanceData> ground_cells;

    for (bool running = true; running;) {
        ++frame_index;
        const Uint64 now = SDL_GetTicks64();
        const float dt = std::min(0.1f, static_cast<float>(now - last_tick) / 1000.f);
        last_tick = now;
        if (editor.preview) {
            editor.motion_time += dt;
        }
        const float state_step = dt * 2.5f;
        editor.state_value += std::clamp(editor.state_target - editor.state_value, -state_step, state_step);
        if (editor.status_frames > 0) {
            --editor.status_frames;
        }

        editor.layout();

        int mouse_x = 0;
        int mouse_y = 0;
        const Uint32 mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                if (!editor.dirty || editor.quit_armed) {
                    running = false;
                } else {
                    editor.quit_armed = true;
                    editor.set_status("未保存の変更があります。もう一度閉じると捨てます（Ctrl+S で保存）", kWarn, 600);
                }
                continue;
            }
            if ((event.type == SDL_WINDOWEVENT) && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                editor.screen_w = event.window.data1;
                editor.screen_h = event.window.data2;
                continue;
            }

            /* --- 名前の入力中は、それだけを見る --- */
            if (editor.input_purpose != InputPurpose::None) {
                if (event.type == SDL_TEXTINPUT) {
                    for (const char *p = event.text.text; *p != '\0'; ++p) {
                        const char c = *p;
                        const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'))
                            || (c == '_') || (c == '-');
                        if (ok && (editor.input_buffer.size() < 32)) {
                            editor.input_buffer += c;
                        }
                    }
                } else if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_BACKSPACE) {
                        if (!editor.input_buffer.empty()) {
                            editor.input_buffer.pop_back();
                        }
                    } else if ((event.key.keysym.sym == SDLK_RETURN) || (event.key.keysym.sym == SDLK_KP_ENTER)) {
                        editor.commit_input();
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        editor.input_purpose = InputPurpose::None;
                        SDL_StopTextInput();
                        editor.set_status("やめました", kDim);
                    }
                }
                continue;
            }

            /* --- メニューが開いている間は、メニューだけを見る --- */
            if (editor.menu_open) {
                if (event.type != SDL_KEYDOWN) {
                    continue;
                }
                const auto rows = editor.menu_rows();
                editor.menu_index = std::clamp(editor.menu_index, 0, static_cast<int>(rows.size()) - 1);
                const bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_TAB:
                case SDLK_F10:
                    editor.menu_open = false;
                    break;
                case SDLK_UP:
                    editor.menu_index = (editor.menu_index + static_cast<int>(rows.size()) - 1) % static_cast<int>(rows.size());
                    editor.confirm_armed = false;
                    editor.menu_last_adjusted = RowId::Close;
                    break;
                case SDLK_DOWN:
                    editor.menu_index = (editor.menu_index + 1) % static_cast<int>(rows.size());
                    editor.confirm_armed = false;
                    editor.menu_last_adjusted = RowId::Close;
                    break;
                case SDLK_LEFT:
                case SDLK_RIGHT:
                    if (rows[static_cast<std::size_t>(editor.menu_index)].adjustable
                        && rows[static_cast<std::size_t>(editor.menu_index)].enabled) {
                        editor.adjust_row(rows[static_cast<std::size_t>(editor.menu_index)],
                            (event.key.keysym.sym == SDLK_RIGHT) ? 1 : -1, shift);
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (rows[static_cast<std::size_t>(editor.menu_index)].enabled) {
                        editor.activate_row(rows[static_cast<std::size_t>(editor.menu_index)]);
                    }
                    break;
                default:
                    break;
                }
                continue;
            }

            /* --- ふだんの入力 --- */
            if (event.type == SDL_KEYDOWN) {
                const bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
                if (event.key.keysym.sym != SDLK_ESCAPE) {
                    editor.quit_armed = false;
                }
                if (ctrl && (event.key.keysym.sym == SDLK_z)) {
                    editor.undo();
                    continue;
                }
                if (ctrl && (event.key.keysym.sym == SDLK_y)) {
                    editor.redo();
                    continue;
                }
                if (ctrl && (event.key.keysym.sym == SDLK_s)) {
                    editor.save();
                    continue;
                }
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (editor.sel_count != 0) {
                        editor.sel_count = 0;
                        editor.set_status("選択を捨てました", kDim);
                    } else if (!editor.dirty || editor.quit_armed) {
                        running = false;
                    } else {
                        editor.quit_armed = true;
                        editor.set_status("未保存の変更があります。もう一度 ESC で捨てて終了（Ctrl+S で保存）", kWarn, 600);
                    }
                    break;
                case SDLK_TAB:
                case SDLK_F10:
                    editor.menu_open = true;
                    editor.menu_index = 0;
                    editor.confirm_armed = false;
                    editor.menu_last_adjusted = RowId::Close;
                    break;
                case SDLK_b:
                    editor.tool = Tool::Place;
                    break;
                case SDLK_x:
                    editor.tool = Tool::Erase;
                    break;
                case SDLK_c:
                    editor.tool = Tool::Paint;
                    break;
                case SDLK_i:
                    editor.tool = Tool::Pick;
                    break;
                case SDLK_v:
                    editor.set_selection_corner();
                    break;
                case SDLK_p:
                    editor.set_pivot_at_hover();
                    break;
                case SDLK_SPACE:
                    editor.preview = !editor.preview;
                    editor.set_status(editor.preview ? "プレビュー中（編集は止まる）" : "プレビューを止めました", kDim);
                    break;
                case SDLK_o:
                    editor.state_target = (editor.state_target > 0.5f) ? 0.f : 1.f;
                    break;
                case SDLK_COMMA:
                case SDLK_PERIOD: {
                    const int count = static_cast<int>(editor.prefab.parts.size());
                    const int direction = (event.key.keysym.sym == SDLK_PERIOD) ? 1 : -1;
                    editor.active_part = ((editor.active_part + direction) % count + count) % count;
                    editor.sel_count = 0;
                    editor.set_status("アクティブ: " + editor.prefab.parts[static_cast<std::size_t>(editor.active_part)].name, kDim);
                    break;
                }
                case SDLK_f:
                    editor.frame_camera();
                    break;
                case SDLK_LEFT:
                    editor.camera.azimuth -= 0.08f;
                    break;
                case SDLK_RIGHT:
                    editor.camera.azimuth += 0.08f;
                    break;
                case SDLK_UP:
                    editor.camera.elevation = std::min(editor.camera.elevation + 0.05f, 1.50f);
                    break;
                case SDLK_DOWN:
                    editor.camera.elevation = std::max(editor.camera.elevation - 0.05f, -0.20f);
                    break;
                case SDLK_EQUALS:
                case SDLK_KP_PLUS:
                    editor.camera.distance = std::max(editor.camera.distance * 0.92f, 0.4f);
                    break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:
                    editor.camera.distance = std::min(editor.camera.distance * 1.08f, 400.f);
                    break;
                case SDLK_LEFTBRACKET:
                    editor.camera.fov_x = std::max(editor.camera.fov_x - 0.05f, 0.15f);
                    break;
                case SDLK_RIGHTBRACKET:
                    editor.camera.fov_x = std::min(editor.camera.fov_x + 0.05f, 1.60f);
                    break;
                default:
                    break;
                }
                continue;
            }
            if (event.type == SDL_MOUSEWHEEL) {
                if (event.wheel.y > 0) {
                    editor.camera.distance = std::max(editor.camera.distance * 0.90f, 0.4f);
                } else if (event.wheel.y < 0) {
                    editor.camera.distance = std::min(editor.camera.distance * 1.11f, 400.f);
                }
                continue;
            }
            if (event.type == SDL_MOUSEMOTION) {
                if (middle_drag) {
                    editor.camera.azimuth += static_cast<float>(event.motion.xrel) * 0.008f;
                    editor.camera.elevation = std::clamp(
                        editor.camera.elevation + (static_cast<float>(event.motion.yrel) * 0.006f), -0.20f, 1.50f);
                }
                continue;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                editor.quit_armed = false;
                if (event.button.button == SDL_BUTTON_MIDDLE) {
                    middle_drag = true;
                    continue;
                }
                const int mx = event.button.x;
                const int my = event.button.y;
                /* パレットのマス */
                if (editor.palette_rect.contains(mx, my)) {
                    const int col = (mx - editor.palette_rect.x - 1) / 13;
                    const int row = (my - editor.palette_rect.y - 1) / 13;
                    const int index = 1 + (row * 16) + col;
                    if ((index >= 1) && (index <= 255) && (col < 16) && (row < 16)) {
                        editor.color = static_cast<std::uint8_t>(index);
                    }
                    continue;
                }
                /* パレットのスライダ */
                {
                    bool on_slider = false;
                    for (int k = 0; k < 3; ++k) {
                        if (editor.slider_rects[k].contains(mx, my)) {
                            editor.slider_drag = k;
                            editor.slider_snapshot_done = false;
                            on_slider = true;
                        }
                    }
                    if (on_slider) {
                        continue;
                    }
                }
                /* パーツ一覧 */
                if (editor.parts_rect.contains(mx, my)) {
                    const int row = (my - editor.parts_rect.y - 24) / 20;
                    if ((row >= 0) && (row < static_cast<int>(editor.prefab.parts.size()))) {
                        editor.active_part = row;
                        editor.sel_count = 0;
                    }
                    continue;
                }
                /* 3D。左 = 道具 / 右 = 消す */
                if (editor.mouse_in_scene
                    && ((event.button.button == SDL_BUTTON_LEFT) || (event.button.button == SDL_BUTTON_RIGHT))) {
                    editor.begin_stroke();
                    editor.update_hover(mx, my);
                    editor.apply_tool(event.button.button == SDL_BUTTON_RIGHT);
                }
                continue;
            }
            if (event.type == SDL_MOUSEBUTTONUP) {
                if (event.button.button == SDL_BUTTON_MIDDLE) {
                    middle_drag = false;
                }
                if ((event.button.button == SDL_BUTTON_LEFT) || (event.button.button == SDL_BUTTON_RIGHT)) {
                    editor.stroke_active = false;
                    editor.slider_drag = -1;
                }
                continue;
            }
        }

        /* --- 押しっぱなしの継続（筆とスライダ） --- */
        editor.update_hover(mouse_x, mouse_y);
        if (editor.stroke_active && ((mouse_buttons & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) != 0)) {
            editor.apply_tool((mouse_buttons & SDL_BUTTON_RMASK) != 0);
        }
        if ((editor.slider_drag >= 0) && ((mouse_buttons & SDL_BUTTON_LMASK) != 0)) {
            const RectPx &rect = editor.slider_rects[editor.slider_drag];
            const float ratio = std::clamp(static_cast<float>(mouse_x - rect.x) / static_cast<float>(std::max(1, rect.w)), 0.f, 1.f);
            const std::uint8_t value = static_cast<std::uint8_t>(std::lround(ratio * 255.f));
            auto &channel = editor.prefab.vox.palette[editor.color][editor.slider_drag];
            if (channel != value) {
                if (!editor.slider_snapshot_done) {
                    editor.snapshot();
                    editor.slider_snapshot_done = true;
                }
                channel = value;
                editor.prefab.vox.palette[editor.color][3] = 255;
                editor.mesh_dirty = true; // パレットは GPU のテクスチャ（upload で写す）
                editor.dirty = true;
            }
        }

        editor.remesh_if_dirty();

        /* ------------------------------------------------ 3D を描く */
        const Vec3 extent = editor.bounds_hi - editor.bounds_lo;
        const float radius = std::max(0.75f, 0.5f * std::sqrt(dot(extent, extent)));
        const Mat4 view = camera_view(editor.camera);
        const float z_near = std::max(0.05f, editor.camera.distance - (radius * 3.f));
        const float z_far = editor.camera.distance + (radius * 6.f) + 4.f;
        const Mat4 projection = camera_projection(editor.camera, z_near, z_far);
        if (!editor.post.resize(editor.screen_w, editor.screen_h, err)) {
            std::fprintf(stderr, "[hd2d] 画面の FBO を作れませんでした: %s\n", err.c_str());
            break;
        }

        // 動き（プレビュー時だけ）。**影のパスにも同じものを渡す**（設計書 §8.2-1）。
        const Mat4 *motions = nullptr;
        std::size_t motion_count = 0;
        WindParams wind;
        if (editor.preview) {
            MotionContext context;
            context.time = editor.motion_time;
            std::set<std::string> state_names;
            for (const auto &part : editor.prefab.parts) {
                if ((part.motion.kind == MotionKind::StateLinked) && !part.motion.state_name.empty()) {
                    state_names.insert(part.motion.state_name);
                }
            }
            for (const auto &name : state_names) {
                context.states.emplace_back(name, editor.state_value);
            }
            compose_part_motions(editor.prefab, context, part_motions);
            motions = part_motions.data();
            motion_count = part_motions.size();
            bool has_wind = false;
            for (const auto &part : editor.prefab.parts) {
                has_wind = has_wind || (part.motion.kind == MotionKind::Wind);
            }
            if (has_wind) {
                wind.dir_x = 0.92f;
                wind.dir_y = 0.39f;
                wind.amplitude = 0.10f;
                wind.phase = WindPhase::Wave;
            }
        }

        // 地面（外接箱の下へ敷く）。
        ground_cells.clear();
        {
            const int x0 = static_cast<int>(std::floor(editor.bounds_lo.x)) - 3;
            const int x1 = static_cast<int>(std::ceil(editor.bounds_hi.x)) + 3;
            const int y0 = static_cast<int>(std::floor(editor.bounds_lo.y)) - 3;
            const int y1 = static_cast<int>(std::ceil(editor.bounds_hi.y)) + 3;
            for (int gy = y0; gy <= y1; ++gy) {
                for (int gx = x0; gx <= x1; ++gx) {
                    InstanceData slab;
                    slab.x = static_cast<float>(gx);
                    slab.y = static_cast<float>(gy);
                    // 1 マスごとに市松で 2 段の灰。置く位置を目で数えられるように。
                    const float shade = (((gx + gy) & 1) == 0) ? 0.60f : 0.68f;
                    slab.r = slab.g = slab.b = shade;
                    ground_cells.push_back(slab);
                }
            }
        }

        SceneLighting light;
        light.sun_dir = normalize(Vec3{ 0.50f, -0.35f, 0.79f });
        light.shadow_strength = 0.85f;
        const ShadowFit fit = fit_shadow_bounds(editor.bounds_lo.x - 1.f, editor.bounds_hi.x + 3.f,
            editor.bounds_lo.y - 1.f, editor.bounds_hi.y + 3.f, editor.bounds_hi.z, light.sun_dir, editor.shadow.side());

        if (editor.shadow.ready()) {
            editor.shadow.begin();
            editor.renderer.begin_motion(wind, editor.motion_time);
            editor.renderer.draw_instanced_depth(editor.ground_gpu, fit.view_projection, ground_cells.data(), ground_cells.size());
            if (editor.gpu_ready) {
                editor.renderer.draw_depth_single(editor.gpu, fit.view_projection, motions, motion_count);
            }
            editor.shadow.end(editor.screen_w, editor.screen_h);
        }

        editor.post.begin_scene(0.10f, 0.11f, 0.13f);
        editor.renderer.begin(projection * view, light, fit.view_projection, editor.shadow.texture(), editor.shadow.side());
        editor.renderer.begin_motion(wind, editor.motion_time);
        editor.renderer.begin_cutaway(Cutaway{});
        editor.renderer.draw_instanced(editor.ground_gpu, ground_cells.data(), ground_cells.size());
        if (editor.gpu_ready) {
            editor.renderer.draw(editor.gpu, motions, motion_count);
        }

        /* --- 目印（影には入れない・プレビュー中は編集の目印を消す） --- */
        markers.clear();
        const float vpc = static_cast<float>(editor.prefab.voxels_per_cell);
        auto push_box = [&](float x, float y, float z, float sx, float sy, float sz,
                            float r, float g, float b, float er, float eg, float eb) {
            InstanceData box;
            box.x = x;
            box.y = y;
            box.z = z;
            box.sx = sx;
            box.sy = sy;
            box.sz = sz;
            box.r = r;
            box.g = g;
            box.b = b;
            box.er = er;
            box.eg = eg;
            box.eb = eb;
            markers.push_back(box);
        };
        //! パーツのローカルなボクセルを囲む箱（少し膨らませて z-fight を避ける）。
        auto push_voxel_marker = [&](int part_index, const int voxel[3], float inflate,
                                     float r, float g, float b, float er, float eg, float eb) {
            const auto &part = editor.prefab.parts[static_cast<std::size_t>(part_index)];
            const float x = (part.offset[0] + static_cast<float>(voxel[0])) / vpc;
            const float y = (part.offset[1] + static_cast<float>(voxel[1])) / vpc;
            const float z = (part.offset[2] + static_cast<float>(voxel[2])) / vpc;
            const float pad = (inflate - 1.f) * 0.5f / vpc;
            push_box(x - pad, y - pad, z - pad, inflate, inflate, inflate, r, g, b, er, eg, eb);
        };
        //! 地面のマスの枠（4 本の棒）。
        auto push_cell_frame = [&](int gx, int gy, float er, float eg, float eb) {
            const float x = static_cast<float>(gx);
            const float y = static_cast<float>(gy);
            const float z = 0.015f;
            const float thickness = 1.6f; // ボクセル
            push_box(x, y, z, 32.f, thickness, thickness, 0.1f, 0.1f, 0.1f, er, eg, eb);
            push_box(x, y + 1.f - (thickness / 32.f), z, 32.f, thickness, thickness, 0.1f, 0.1f, 0.1f, er, eg, eb);
            push_box(x, y, z, thickness, 32.f, thickness, 0.1f, 0.1f, 0.1f, er, eg, eb);
            push_box(x + 1.f - (thickness / 32.f), y, z, thickness, 32.f, thickness, 0.1f, 0.1f, 0.1f, er, eg, eb);
        };

        // footprint: 宣言と実体（P9 ④）。両方 = 緑 / 実体だけ = 黄 / 宣言だけ = 赤。
        {
            const std::set<std::pair<int, int>> declared(editor.prefab.footprint.begin(), editor.prefab.footprint.end());
            const std::set<std::pair<int, int>> actual(editor.actual_footprint.begin(), editor.actual_footprint.end());
            for (const auto &cell : declared) {
                if (actual.count(cell) != 0) {
                    push_cell_frame(cell.first, cell.second, 0.05f, 0.55f, 0.10f);
                } else {
                    push_cell_frame(cell.first, cell.second, 0.90f, 0.08f, 0.06f);
                }
            }
            for (const auto &cell : actual) {
                if (declared.count(cell) == 0) {
                    push_cell_frame(cell.first, cell.second, 0.85f, 0.70f, 0.05f);
                }
            }
        }
        if (!editor.preview) {
            // ホバー。消す・塗るは当たったボクセル、置くは隣。
            if ((editor.tool == Tool::Place) && editor.hover_place_valid) {
                push_voxel_marker(editor.active_part, editor.place_target, 1.06f, 0.2f, 0.9f, 0.3f, 0.05f, 0.60f, 0.10f);
            } else if (editor.hover_hit) {
                const bool active = (editor.hover.part == editor.active_part);
                if (editor.tool == Tool::Erase) {
                    push_voxel_marker(editor.hover.part, editor.hover.voxel, 1.10f, 0.9f, 0.2f, 0.2f,
                        active ? 0.70f : 0.15f, 0.05f, 0.05f);
                } else if (editor.tool == Tool::Paint) {
                    const auto *rgb = editor.prefab.vox.palette[editor.color];
                    push_voxel_marker(editor.hover.part, editor.hover.voxel, 1.10f, 0.2f, 0.2f, 0.9f,
                        static_cast<float>(rgb[0]) / 255.f, static_cast<float>(rgb[1]) / 255.f, static_cast<float>(rgb[2]) / 255.f);
                } else {
                    push_voxel_marker(editor.hover.part, editor.hover.voxel, 1.08f, 0.6f, 0.6f, 0.9f, 0.25f, 0.25f, 0.55f);
                }
            }
            // 選択の箱（12 本の棒）。
            if (editor.sel_count != 0) {
                const auto &part = editor.prefab.parts[static_cast<std::size_t>(editor.active_part)];
                float lo[3];
                float hi[3];
                for (int k = 0; k < 3; ++k) {
                    lo[k] = (part.offset[k] + static_cast<float>(std::min(editor.sel_a[k], editor.sel_b[k]))) / vpc;
                    hi[k] = (part.offset[k] + static_cast<float>(std::max(editor.sel_a[k], editor.sel_b[k]) + 1)) / vpc;
                }
                const float t = 0.9f; // 棒の太さ（ボクセル）
                const float er = 0.10f;
                const float eg = 0.75f;
                const float eb = 0.85f;
                for (int zi = 0; zi < 2; ++zi) {
                    for (int yi = 0; yi < 2; ++yi) {
                        push_box(lo[0], (yi != 0 ? hi[1] : lo[1]) - (yi != 0 ? t / vpc : 0.f),
                            (zi != 0 ? hi[2] : lo[2]) - (zi != 0 ? t / vpc : 0.f),
                            (hi[0] - lo[0]) * vpc, t, t, 0.1f, 0.1f, 0.1f, er, eg, eb);
                    }
                }
                for (int zi = 0; zi < 2; ++zi) {
                    for (int xi = 0; xi < 2; ++xi) {
                        push_box((xi != 0 ? hi[0] : lo[0]) - (xi != 0 ? t / vpc : 0.f), lo[1],
                            (zi != 0 ? hi[2] : lo[2]) - (zi != 0 ? t / vpc : 0.f),
                            t, (hi[1] - lo[1]) * vpc, t, 0.1f, 0.1f, 0.1f, er, eg, eb);
                    }
                }
                for (int yi = 0; yi < 2; ++yi) {
                    for (int xi = 0; xi < 2; ++xi) {
                        push_box((xi != 0 ? hi[0] : lo[0]) - (xi != 0 ? t / vpc : 0.f),
                            (yi != 0 ? hi[1] : lo[1]) - (yi != 0 ? t / vpc : 0.f), lo[2],
                            t, t, (hi[2] - lo[2]) * vpc, 0.1f, 0.1f, 0.1f, er, eg, eb);
                    }
                }
            }
        }
        // ピボット（アクティブパーツ。x 赤 / y 緑 / z 青の十字）。
        {
            const auto &part = editor.prefab.parts[static_cast<std::size_t>(editor.active_part)];
            if (part.has_pivot) {
                const float px = part.pivot[0] / vpc;
                const float py = part.pivot[1] / vpc;
                const float pz = part.pivot[2] / vpc;
                const float arm = 10.f; // 腕の長さ（ボクセル）
                const float t = 1.3f;
                push_box(px - (arm / vpc), py - (t * 0.5f / vpc), pz - (t * 0.5f / vpc), arm * 2.f, t, t,
                    0.1f, 0.02f, 0.02f, 1.0f, 0.10f, 0.10f);
                push_box(px - (t * 0.5f / vpc), py - (arm / vpc), pz - (t * 0.5f / vpc), t, arm * 2.f, t,
                    0.02f, 0.1f, 0.02f, 0.10f, 1.0f, 0.10f);
                push_box(px - (t * 0.5f / vpc), py - (t * 0.5f / vpc), pz - (arm / vpc), t, t, arm * 2.f,
                    0.02f, 0.02f, 0.1f, 0.15f, 0.35f, 1.0f);
            }
        }
        if (!markers.empty()) {
            editor.renderer.draw_instanced(editor.marker_gpu, markers.data(), markers.size());
        }

        editor.post.end_scene(editor.screen_w, editor.screen_h);
        PostFlags flags; // 既定は全部入り
        if (!editor.post_all) {
            flags.fog = flags.dof = flags.bloom = flags.grade = flags.vignette = false; // トーンマップだけ
        }
        editor.post.resolve(flags, PostParams{}, z_near, z_far, editor.camera.distance, editor.screen_w, editor.screen_h);

        /* ------------------------------------------------ UI（板 → 文字） */
        editor.paint.begin(editor.screen_w, editor.screen_h);
        editor.text.begin(editor.screen_w, editor.screen_h);

        // パレット。
        {
            editor.paint.panel(RectPx{ editor.palette_rect.x - 4, editor.palette_rect.y - 22,
                                  editor.palette_rect.w + 240, editor.palette_rect.h + 52 },
                solid_panel_style());
            editor.text.draw(editor.palette_rect.x, editor.palette_rect.y - 20, "パレット（クリックで選択・右のスライダで色を変える）", kDim);
            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    const int index = 1 + (row * 16) + col;
                    if (index > 255) {
                        break;
                    }
                    const auto *rgb = editor.prefab.vox.palette[index];
                    const RectPx swatch{ editor.palette_rect.x + 1 + (col * 13), editor.palette_rect.y + 1 + (row * 13), 12, 12 };
                    editor.paint.rect(swatch, PaintColor{ static_cast<float>(rgb[0]) / 255.f,
                                                  static_cast<float>(rgb[1]) / 255.f, static_cast<float>(rgb[2]) / 255.f, 1.f });
                    if (index == editor.color) {
                        editor.paint.frame(RectPx{ swatch.x - 1, swatch.y - 1, swatch.w + 2, swatch.h + 2 },
                            PaintColor{ 1.f, 1.f, 1.f, 1.f }, 1);
                    }
                }
            }
            const auto *rgb = editor.prefab.vox.palette[editor.color];
            static const char *const kChannel[3] = { "R", "G", "B" };
            for (int k = 0; k < 3; ++k) {
                const RectPx &rect = editor.slider_rects[k];
                static const PaintColor kFill[3] = { { 0.85f, 0.25f, 0.25f, 1.f }, { 0.25f, 0.80f, 0.30f, 1.f },
                    { 0.30f, 0.45f, 0.95f, 1.f } };
                editor.paint.gauge(rect, static_cast<float>(rgb[k]) / 255.f, kFill[k], PaintColor{ 0.12f, 0.13f, 0.17f, 1.f });
                editor.text.draw(rect.x - 14, rect.y, kChannel[k], kDim);
                editor.text.draw(rect.x + rect.w + 6, rect.y, std::to_string(rgb[k]), kBright);
            }
            char color_line[128];
            std::snprintf(color_line, sizeof(color_line), "索引 %d  使用 %zu ボクセル", editor.color,
                count_color_usage(editor.prefab, editor.color));
            editor.text.draw(editor.slider_rects[0].x - 14, editor.slider_rects[2].y + 26, color_line, kDim);
        }

        // パーツ一覧。
        {
            editor.paint.panel(editor.parts_rect, solid_panel_style());
            editor.text.draw(editor.parts_rect.x + 8, editor.parts_rect.y + 4, "パーツ（, . で切替 / クリック）", kDim);
            for (std::size_t i = 0; i < editor.prefab.parts.size(); ++i) {
                const auto &part = editor.prefab.parts[i];
                const int y = editor.parts_rect.y + 24 + (static_cast<int>(i) * 20);
                const bool active = (static_cast<int>(i) == editor.active_part);
                if (active) {
                    editor.paint.rect(RectPx{ editor.parts_rect.x + 2, y - 1, editor.parts_rect.w - 4, 19 },
                        PaintColor{ 0.20f, 0.28f, 0.45f, 0.85f });
                }
                std::string line = (active ? "▶ " : "  ") + part.name;
                if (part.motion.kind != MotionKind::Static) {
                    line += std::string("  (") + motion_kind_label(part.motion.kind) + ")";
                }
                if (part.parent >= 0) {
                    line += " ←" + editor.prefab.parts[static_cast<std::size_t>(part.parent)].name;
                }
                editor.text.draw(editor.parts_rect.x + 8, y, line, active ? kBright : kDim, editor.parts_rect.w - 16);
            }
        }

        // 見出し。
        {
            char line[256];
            std::snprintf(line, sizeof(line), "edit=%s%s  parts=%zu  quads=%zu  triangles=%zu  fps=%.1f",
                editor.file_name.c_str(), editor.dirty ? " ●未保存" : "", editor.prefab.parts.size(),
                editor.gpu_ready ? editor.gpu.quad_count : 0, editor.gpu_ready ? editor.gpu.triangle_count : 0, editor.fps);
            editor.text.draw_cell(8, 8, 0, 0, line, kHeader);

            const auto &part = editor.prefab.parts[static_cast<std::size_t>(editor.active_part)];
            const VoxModel &model = editor.prefab.vox.models[static_cast<std::size_t>(part.model_index)];
            std::snprintf(line, sizeof(line), "アクティブ: %s  %dx%dx%d  offset=(%.0f,%.0f,%.0f)  %s  wind_k=%.2f  %s",
                part.name.c_str(), model.size[0], model.size[1], model.size[2],
                static_cast<double>(part.offset[0]), static_cast<double>(part.offset[1]), static_cast<double>(part.offset[2]),
                motion_kind_label(part.motion.kind), static_cast<double>(part.wind_k), part.grounded ? "接地" : "浮き");
            editor.text.draw_cell(8, 8, 0, 1, line, kDim);

            const std::set<std::pair<int, int>> declared(editor.prefab.footprint.begin(), editor.prefab.footprint.end());
            const std::set<std::pair<int, int>> actual(editor.actual_footprint.begin(), editor.actual_footprint.end());
            std::size_t mismatch = 0;
            for (const auto &cell : declared) {
                mismatch += (actual.count(cell) == 0) ? 1 : 0;
            }
            for (const auto &cell : actual) {
                mismatch += (declared.count(cell) == 0) ? 1 : 0;
            }
            std::snprintf(line, sizeof(line), "footprint: 宣言 %zu / 実体 %zu / 食い違い %zu%s", declared.size(), actual.size(),
                mismatch, (mismatch != 0) ? "（緑=一致 黄=未宣言 赤=宣言だけ。保存時に実体へ作り直す）" : "");
            editor.text.draw_cell(8, 8, 0, 2, line, (mismatch != 0) ? kWarn : kDim);

            std::snprintf(line, sizeof(line), "道具: %s  色=%d  %s%s", tool_name(editor.tool), editor.color,
                editor.preview ? "▶プレビュー中（空白で戻る）" : "", editor.post_all ? "  post=ALL" : "");
            editor.text.draw_cell(8, 8, 0, 3, line, kDim);

            editor.text.draw_cell(8, 8, 0, 4,
                "[P9] 左クリック=道具  右=消す  中ドラッグ/矢印=回す  ホイール=寄る  V=選択  P=ピボット  空白=動き  Tab=メニュー  ESC=終了",
                kDim);
        }

        // メニュー。
        if (editor.menu_open) {
            const auto rows = editor.menu_rows();
            editor.menu_index = std::clamp(editor.menu_index, 0, static_cast<int>(rows.size()) - 1);
            editor.paint.panel(editor.menu_rect, solid_panel_style());
            editor.text.draw(editor.menu_rect.x + 10, editor.menu_rect.y + 6, "メニュー（↑↓ 選ぶ / ←→ 動かす（Shift=大きく） / Enter 実行）", kHeader);
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const int y = editor.menu_rect.y + 34 + (static_cast<int>(i) * 20);
                const bool selected = (static_cast<int>(i) == editor.menu_index);
                if (selected) {
                    editor.paint.rect(RectPx{ editor.menu_rect.x + 4, y - 1, editor.menu_rect.w - 8, 19 },
                        PaintColor{ 0.20f, 0.28f, 0.45f, 0.9f });
                }
                const TextColor colour = rows[i].enabled ? (selected ? kBright : kDim) : TextColor{ 0.35f, 0.38f, 0.45f, 1.f };
                editor.text.draw(editor.menu_rect.x + 12, y, rows[i].label, colour);
                if (!rows[i].value.empty()) {
                    std::string value = rows[i].value;
                    if (rows[i].adjustable) {
                        value = "< " + value + " >";
                    }
                    editor.text.draw(editor.menu_rect.x + 250, y, value, colour, editor.menu_rect.w - 260);
                }
            }
        }

        // 名前の入力。
        if (editor.input_purpose != InputPurpose::None) {
            const RectPx input_rect{ (editor.screen_w / 2) - 220, (editor.screen_h / 2) - 30, 440, 60 };
            editor.paint.panel(input_rect, solid_panel_style());
            const char *title = "名前";
            switch (editor.input_purpose) {
            case InputPurpose::NewPart:
                title = "新しいパーツの名前（A-Za-z0-9_-）";
                break;
            case InputPurpose::RenamePart:
                title = "新しい名前（A-Za-z0-9_-）";
                break;
            case InputPurpose::SplitName:
                title = "分割してできるパーツの名前（A-Za-z0-9_-）";
                break;
            case InputPurpose::StateName:
                title = "状態の名前（例: door）";
                break;
            default:
                break;
            }
            editor.text.draw(input_rect.x + 10, input_rect.y + 6, title, kHeader);
            editor.text.draw(input_rect.x + 10, input_rect.y + 30, editor.input_buffer + "_", kBright);
        }

        // 状態行（パレットの板の右。左端に置くと板と重なる）。
        if ((editor.status_frames > 0) && !editor.status.empty()) {
            editor.text.draw(editor.palette_rect.x + editor.palette_rect.w + 250, editor.screen_h - 24,
                editor.status, editor.status_color, editor.screen_w - (editor.palette_rect.x + editor.palette_rect.w + 258));
        }

        editor.paint.flush();
        editor.text.flush();

        if (!options.shot_path.empty() && (frame_index >= 8)) {
            (void)save_editor_shot(options.shot_path, editor.screen_w, editor.screen_h);
            running = false;
        }
        SDL_GL_SwapWindow(window);

        ++fps_frames;
        if ((now - fps_mark) >= 500) {
            editor.fps = (static_cast<double>(fps_frames) * 1000.0) / static_cast<double>(now - fps_mark);
            fps_frames = 0;
            fps_mark = now;
        }
    }

    if (editor.gpu_ready) {
        editor.renderer.release(editor.gpu);
    }
    editor.renderer.release(editor.marker_gpu);
    editor.renderer.release(editor.ground_gpu);
    editor.post.shutdown();
    editor.shadow.shutdown();
    editor.renderer.shutdown();
    editor.paint.shutdown();
    editor.text.shutdown();
    return 0;
}

} // namespace hd2d
