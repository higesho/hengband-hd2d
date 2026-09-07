/*!
 * @file prefab_edit.h
 * @brief プレハブの**編集操作**とレイ判定（P9 ②③）。**GL に依存しない。**
 *
 * 段取りは P9。
 * この TU はエディタ側（`HD2D_EDITOR`）にしか入らない。
 *
 * ## なぜ描画から切り離してあるか
 * ここの全部が `--edit-check`（窓も GL も要らない）で検査できるようにするため。
 * 「クリックしたボクセルが消える」は目でしか見えないが、「このレイはこのボクセルに
 * この面から当たる」は数字で検査できる。目でしか確かめられないものを最小にする。
 *
 * ## 編集中の基準は `Prefab` である
 * `VoxFile::parts`（読み込み時のシーングラフ）は編集中は**更新しない**。書き出し
 * （`prefab_writer.cpp`）は `Prefab::parts` からシーングラフを作り直すので、読み戻せば
 * また一致する。2 つの一覧を同時に維持しようとするほうが必ずずれる（引き継ぎ §3 罠 25）。
 *
 * ## 座標系
 * | 呼び名 | 単位 | 原点 |
 * |---|---|---|
 * | マス（cell） | 1 = 1 マス | プレハブ原点。カメラ・レイはこれ |
 * | ボクセル | 1 = 1/32 マス | パーツごとに `offset` から始まる**ローカル** |
 */
#pragma once

#include "render/math3d.h"
#include "voxel/prefab.h"

#include <cstdint>
#include <string>

namespace hd2d {

/* ============================================================ カメラとレイ */

//! エディタのカメラ（`run_prefab_view` と同じ回し方の球面座標）。
struct EditorCameraState {
    Vec3 center{};
    float azimuth{ 0.9f };
    float elevation{ 0.62f };
    float distance{ 10.f };
    float fov_x{ 0.62f }; //!< 水平画角（ラジアン）
    int screen_w{ 1600 };
    int screen_h{ 900 };
};

//! プレハブ空間（マス単位）のレイ。`dir` は正規化されている前提。
struct Ray {
    Vec3 origin{};
    Vec3 dir{};
};

Vec3 camera_eye(const EditorCameraState &camera);
Mat4 camera_view(const EditorCameraState &camera);
Mat4 camera_projection(const EditorCameraState &camera, float z_near, float z_far);

/*!
 * @brief 画素（左上原点）からプレハブ空間へのレイを作る。
 * @details `look_at` / `perspective_horizontal` と**同じ約束**（この世界は左手系で、
 * 右方向 = 上×前）から逆算する。ここが描画とずれるとクリックが 1 ボクセル隣を
 * 選ぶようになるので、`--edit-check` が投影との往復（画素 → レイ → 当たり → 画素）を確かめる。
 */
Ray camera_ray(const EditorCameraState &camera, float pixel_x, float pixel_y);

//! 世界座標を画素（左上原点）へ。カメラの後ろにあるときは偽。
bool camera_project(const EditorCameraState &camera, const Vec3 &world, float &pixel_x, float &pixel_y);

/* ============================================================== レイ判定 */

//! レイが最初に当たった**詰まっている**ボクセル。
struct VoxelHit {
    int part{ -1 }; //!< `Prefab::parts` の添字
    int voxel[3]{}; //!< パーツのローカル（ボクセル単位）
    int normal[3]{}; //!< 当たった面の外向き法線（軸沿い）。隣へ置くなら `voxel + normal`
    float t{ 0.f }; //!< レイの媒介変数（＝マス単位の距離）
};

/*!
 * @brief 1 パーツへの当たり（Amanatides & Woo の格子行進）。
 * @param max_t これより遠い当たりは捨てる（マス単位）。
 * @return 当たったか。
 */
bool raycast_part(const Prefab &prefab, int part_index, const Ray &ray, float max_t, VoxelHit &out);

//! 全パーツのうち最も手前の当たり。
bool raycast_prefab(const Prefab &prefab, const Ray &ray, float max_t, VoxelHit &out);

//! 水平面 z = `plane_z`（マス単位）との交点。地面クリック（何も無い所へ置く）用。
bool raycast_ground(const Ray &ray, float plane_z, Vec3 &point_out);

/* ============================================================== 編集操作 */

/*!
 * @brief 編集を始める前の正規化。
 * @details 各パーツが**自分だけの模型**を持つようにする（共有されていたら複製する）。
 * 節点名（`voxels`）が重複していたら添字を付けて直す。やったことは `log` に書く
 * （黙って直さない）。既存 6 素材ではどちらも起きないが、手で書いた `.jsonc` は
 * 何でもありうる。
 */
void normalize_for_edit(Prefab &prefab, std::string &log);

//! 範囲外は 0。`part_index` が不正でも 0（落とさない）。
std::uint8_t get_voxel(const Prefab &prefab, int part_index, int x, int y, int z);

/*!
 * @brief ボクセルを置く／消す（`color` = 0 で消す）。**範囲の外へ置くと模型が育つ。**
 * @details 負の側へ育つと既存ボクセルのローカル座標がずれ、`offset` がその分だけ動く
 * （プレハブ空間では何も動かない）。ずれた量は `shift_out` に返す（呼び出し側が
 * 持っているローカル座標を捨てる合図）。一辺 256（形式の上限）を超える成長は断る。
 * ピボットはプレハブ空間なので**育っても動かさない**。
 */
bool set_voxel(Prefab &prefab, int part_index, int x, int y, int z, std::uint8_t color,
    std::string &err, int shift_out[3] = nullptr);

//! 新しい空のパーツ（1×1×1・原点）。戻りは添字、失敗は -1。
int add_part(Prefab &prefab, const std::string &name, std::string &err);

//! 名前（`name` と `voxels` の両方）を変える。使える字は `A-Za-z0-9_-`。
bool rename_part(Prefab &prefab, int part_index, const std::string &new_name, std::string &err);

/*!
 * @brief パーツを消す。最後の 1 つは消せない。
 * @details 消すパーツを親にしていたパーツは、消すパーツの親へ付け替える（孤児にしない）。
 * 模型も消し、後ろの `model_index` を詰める。
 */
bool delete_part(Prefab &prefab, int part_index, std::string &err);

//! パーツをボクセル単位で平行移動する。**ピボットも一緒に動く**（ピボットはパーツに付く点なので）。
bool move_part(Prefab &prefab, int part_index, int dx, int dy, int dz, std::string &err);

//! 親を付け替える（-1 = 根へ）。自分や子孫を親にはできない（循環になる）。
bool set_parent(Prefab &prefab, int part_index, int new_parent, std::string &err);

/*!
 * @brief 選択の箱（パーツのローカル・両端含む）の中のボクセルを**新しいパーツへ移す**（P9 ③の分割）。
 * @details 新しい模型は移したボクセルのぴったりの箱にする。動きと `wind_k` と `grounded` の
 * 初期値は元のパーツから引き継ぎ、親は元のパーツにする（吊り看板を柱から切り出す形が既定）。
 * 選択が空、または元のパーツの全部にあたるときは断る。
 * @return 新しいパーツの添字。失敗は -1。
 */
int split_part(Prefab &prefab, int part_index, const int box_min[3], const int box_max[3],
    const std::string &new_name, std::string &err);

//! パレット索引 `color` を使っているボクセルの数（全パーツ）。
std::size_t count_color_usage(const Prefab &prefab, std::uint8_t color);

//! `--edit-new=` の雛形。パーツ `main`（空の 1×1×1）と見やすい既定パレット。
Prefab make_new_prefab(const std::string &name);

} // namespace hd2d
