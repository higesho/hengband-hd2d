/*!
 * @file gb_frame.h
 * @brief 幻想蛮怒コアの状態 → `GameFrame`（設計 §4 フレーム充填規約）。
 *
 * 変愚側の `presentation/bridge/presentation_bridge.cpp`（`Bridge::capture`）に
 * 当たる位置。ただし**共用はできない**——あちらは変愚のコア型
 * （`PlayerType` / `FloorType`）を直に読む。こちらは `gb_shim.h` 越しに読む。
 *
 * ## P2 で埋めるもの（設計 §8 の P2 行）
 * - `menu_term_lines`（プロトコル上の term ミラー）… 全 27 行・**色 span 付き**
 * - `hud` … HP/MP/Lv/AU・階層・現在地名
 * - `messages` … メッセージリングの末尾
 *
 * ## P3 で埋めたもの（設計 §4 / §8 の P3 行）
 * - `cells` … `ui_state` の視界矩形・@ 中心。見た目は `map_info()`、ID は `cave` 直読み
 * - `minimap` … 階の全域
 * - `floor` / `lighting` … 階の素性と昼夜（HD2D の見た目の種と演出が読む）
 * - `menu_open` / `pre_game_menu` … `character_icky` からの本判定
 * - `tile_index` … mapping.csv の目録引き（§5.1）
 *
 * ## まだ空のまま（設計 §4 の「後段」）
 * `prompt` / `numeric` / `menu_choices` / `combat_fx` / `under_tile_index` / `graf_*`。
 * 空なら v1 §6.1 の省略規則で送られもしない。
 */
#pragma once

#include "frame/game_frame.h"

namespace gb {

class TileManifest;

/*!
 * @brief 視界の大きさ（`ui_state.view_cells`。v1 §7）。
 * @details 0 以下は無視する（申告なし）。**@ を中心に切り出す**ので、
 * ここが変わると次の capture から窓の大きさが変わる。
 */
void set_view_size(int w, int h);

/*!
 * @brief カメラが @ に追従するか（`ui_state.camera_follow_player`）。
 * @details 偽のときだけ階の端で丸める（変愚の `Bridge::set_camera_follow_player` と同じ）。
 */
void set_camera_follow_player(bool follow);

/*!
 * @brief 画面側の `ui_state.cursor_mode` を覚える（v1 §7）。既定は真。
 * @details 覚えるだけで、コアへ当てるのは `apply_cursor_mode()`。
 */
void set_cursor_mode(bool enabled);

/*!
 * @brief 覚えた値をコアへ当てる（`gb_apply_cursor_mode()`）。
 * @details **capture の中ではなく、digest の門より手前で毎周回呼ぶこと。**
 * `request_command()` はコマンドを 1 つ受けるたびに `use_menu` を落とすので、
 * 「画が変わらないので capture を省いた」フレームでも立て直しは要る。
 */
void apply_cursor_mode();

/*!
 * @brief タイル目録を差す。`nullptr` で外れる（`tile_index` が全部 0 になる）。
 * @details 寿命は呼び出し側の持ち物。`gb_main.cpp` が起動時に 1 回差して外さない。
 */
void set_tile_manifest(const TileManifest *manifest);

/*!
 * @brief ゲーム前画面（タイトル＋セーブ選択。P5）の選択肢を宣言する。
 * @details アダプタが自分で描いた画面の選択肢を、そのまま `frame.menu_choices` に
 * 載せる口。**画面側のカーソル層（矢印・パッド・クリック）がこれを読んで動く**
 * （`hd2d/ui/ui_cursor.cpp`。コアの Term からの自動抽出は幻想蛮怒には無い）。
 * `line_index` は Term の行番号そのまま（こちらのミラーは空行を捨てない）。
 * 空を渡すと外れる。ゲームへ入る前に必ず外すこと。
 */
void set_pregame_choices(const std::vector<MenuChoice> &choices);

/*!
 * @brief いまのコアの状態を 1 フレームに写す。
 * @param frame_id 通し番号（呼び手が増やす。v1 §6.1 で常に出る唯一の数）
 * @details 呼んでよいのは**ゲームスレッド**だけ（`gb_shim.h` の読み口はどれも
 * 素の大域変数を読む）。
 */
GameFrame capture_frame(unsigned long long frame_id);

} // namespace gb
