/*!
 * @file sq_frame.h
 * @brief Sil-Q コアの状態 → `GameFrame`（設計 §4 フレーム充填規約）。
 *
 * 変愚側の `presentation/bridge/presentation_bridge.cpp`（`Bridge::capture`）に
 * 当たる位置。ただし**共用はできない**——あちらは変愚のコア型
 * （`PlayerType` / `FloorType`）を直に読む。こちらは `sq_shim.h` 越しに読む。
 *
 * ## P2 で埋めるもの（設計 §8 の P2 行）
 * - `menu_term_lines`（プロトコル上の Term ミラー）… 全 27 行・**色 span 付き**
 * - `hud` … HP / SP / 階層（**金と Lv は Sil-Q に無い**ので 0）
 * - `messages` … メッセージリングの末尾
 *
 * ## P3 で埋めたもの
 * - `cells` … `ui_state` の視界矩形・@ 中心。見た目は `map_info()`、ID は `cave_*` 直読み
 * - `minimap` … 階の全域（**見たマスだけ**）
 * - `floor` / `lighting` … 階の素性と光（HD2D の見た目の種と演出が読む）
 * - `tile_index` … **地形だけ** `terrain_map.csv` の目録引き（設計 §5）
 *
 * ## M0.5 ① で埋めたもの
 * `menu_choices` / `prompt` / `numeric`。中身は `sq_menu.h` が作る——**選ぶ画面が
 * どこかを知っているのはあちら 1 か所**で、ここは呼ぶ順だけを持つ。
 *
 * ## 空のままにするもの（設計 §4 の「後段」）
 * `combat_fx` / `under_tile_index` / `graf_*`。
 * 空なら v1 §6.1 の省略規則で送られもしない。
 */
#pragma once

#include "frame/game_frame.h"

namespace sq {

class TileManifest;

/*!
 * @brief 視界の大きさ（`ui_state.view_cells`。v1 §7）。
 * @details 0 以下は無視する（申告なし）。**@ を中心に切り出す**ので、
 * ここが変わると次の capture から窓の大きさが変わる。P3 で効き始める。
 */
void set_view_size(int w, int h);

/*!
 * @brief カメラが @ に追従するか（`ui_state.camera_follow_player`）。
 */
void set_camera_follow_player(bool follow);

/*!
 * @brief タイル目録を差す。`nullptr` で外れる（`tile_index` が全部 0 になる）。
 * @details 寿命は呼び出し側の持ち物。`sq_main.cpp` が起動時に 1 回差して外さない。
 */
void set_tile_manifest(const TileManifest *manifest);

/*!
 * @brief いまのコアの状態を 1 フレームに写す。
 * @param frame_id 通し番号（呼び手が増やす。v1 §6.1 で常に出る唯一の数）
 * @details 呼んでよいのは**ゲームスレッド**だけ（`sq_shim.h` の読み口はどれも
 * 素の大域変数を読む）。
 */
GameFrame capture_frame(unsigned long long frame_id);

} // namespace sq
