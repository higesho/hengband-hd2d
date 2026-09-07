/*!
 * @file fc_frame.h
 * @brief Frox コアの状態 → `GameFrame`（設計 §4 フレーム充填規約）。
 *
 * 変愚側の `presentation/bridge/presentation_bridge.cpp`（`Bridge::capture`）に
 * 当たる位置。ただし**共用はできない**——あちらは変愚のコア型を直に読む。
 * こちらは `fc_shim.h` 越しに読む。形は `gensoband/adapter/gb_frame.{h,cpp}` の写し。
 *
 * ## 幻想蛮怒版との違い
 * - **文字コード変換が無い**（M0 の Frox は純 ASCII。設計 §3.1）
 * - **状態列が右**（`ui_char_info_rect()` は右端 12 桁）。`status_col_side = 1` を
 *   申告する（設計 §4.3 / §6.1）
 * - **最下段の階層が左**（`prt_depth()` は行の頭に書き、状態バーがその右に続く）
 * - 戦闘の見せ場（`combat_fx`）と文字入力の旗は**後段**（FH-nn へ積む）
 *
 * ## Term のセルを読む経路は 1 か所（設計 §3.1・§12 の 2）
 * セル → 文字列の組み立てはこの TU の `row_to_utf8()` / `span_to_line()` /
 * `row_to_runs()` に集めてある。M1 で CP932 → UTF-8 の変換を入れるときは
 * ここと `fc_sub_panels.cpp` の `capture_lines()` だけを直す。
 */
#pragma once

#include "frame/game_frame.h"

namespace fc {

class TileManifest;

/*!
 * @brief 視界の大きさ（`ui_state.view_cells`。v1 §7）。
 * @details 0 以下は無視する（申告なし）。**@ を中心に切り出す**ので、
 * ここが変わると次の capture から窓の大きさが変わる。
 */
void set_view_size(int w, int h);

/*! @brief カメラが @ に追従するか（`ui_state.camera_follow_player`）。 */
void set_camera_follow_player(bool follow);

/*!
 * @brief 画面側の `ui_state.cursor_mode` を覚える（v1 §7）。既定は真。
 * @details 覚えるだけで、コアへ当てるのは `apply_cursor_mode()`。
 */
void set_cursor_mode(bool enabled);

/*!
 * @brief 覚えた値をコアへ当てる（`fc_apply_cursor_mode()`）。
 * @details **capture の中ではなく、digest の門より手前で毎周回呼ぶこと。**
 * `request_command()` はコマンドを 1 つ受けるたびに `use_menu` を落とすので、
 * 「画が変わらないので capture を省いた」フレームでも立て直しは要る。
 */
void apply_cursor_mode();

/*!
 * @brief タイル目録を差す。`nullptr` で外れる（`tile_index` が全部 0 になる）。
 * @details 寿命は呼び出し側の持ち物。`fc_main.cpp` が起動時に 1 回差して外さない。
 */
void set_tile_manifest(const TileManifest *manifest);

/*!
 * @brief ゲーム前画面（タイトル＋セーブ選択）の選択肢を宣言する。
 * @details アダプタが自分で描いた画面の選択肢を、そのまま `frame.menu_choices` に
 * 載せる口。**画面側のカーソル層（矢印・パッド・クリック）がこれを読んで動く**。
 * 空を渡すと外れる。ゲームへ入る前に必ず外すこと。
 */
void set_pregame_choices(const std::vector<MenuChoice> &choices);

/*!
 * @brief いまのコアの状態を 1 フレームに写す。
 * @param frame_id 通し番号（呼び手が増やす。v1 §6.1 で常に出る唯一の数）
 * @details 呼んでよいのは**ゲームスレッド**だけ（`fc_shim.h` の読み口はどれも
 * 素の大域変数を読む）。
 */
GameFrame capture_frame(unsigned long long frame_id);

} // namespace fc
