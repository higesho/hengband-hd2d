/*!
 * @file sdl_game_bootstrap.h
 * @brief コア初期化 → play_game（設計書 §6.4 / PHASE2_PLAN T2・T3）
 *
 * 既存 public API のみで起動する（設計レビュー H2）。
 * game_main（static・GDI 前提）は再利用せず、init_windows も呼ばない。
 * コア初期化ロジックの改変・切り出しは行わない。
 */
#pragma once

#include "frame/ui_seam.h"

#include <filesystem>

class PlayerType;

namespace presentation {

class Bridge;

/*!
 * @brief null term を設置し、コア初期化後に play_game を実行する。
 * @param lib_path ANGBAND ライブラリパス（...\lib\）。合成ルートが exe から算出して渡す。
 * @param bridge GameFrame 生成器
 * @param seam ui 機能（present/pump_input/delay/quit）
 * @return 終了コード（play_game 完了後は 0）
 * @details GDI init_windows は呼ばない。単一 SDL 窓・Term 画素なし。
 */
int run_sdl_game(const std::filesystem::path &lib_path, Bridge &bridge, const UiSeam &seam);

/*!
 * @brief K-49: ボット用 JSON 出力がコマンドライン／環境変数で指定されたか。
 * @details 真なら機能メニューの設定（`SdlUiOptions::bot_json_enabled`）に関わらず ON にする。
 * 設定ファイルへは書き戻さないので、次に普通に起動すれば OFF に戻る。
 */
bool bot_json_forced();

} // namespace presentation
