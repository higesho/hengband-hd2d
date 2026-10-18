/*!
 * @file tb_window_extras.h
 * @brief 共用の窓が出さない、TOband 固有の操作を窓へ足す。
 *
 * 全体地図（`M`。`cave.c` の `do_cmd_view_map()`）の下の行は `'M':拾う 'N':放置 'D':M+N 'K':壊すアイテムを表示 'E':エレメント分布` と案内する。
 * 共用の地図の窓（`modern_map_content`）が操作に出すのは M・N・K・D の 4 つだけで、`E`（エレメントの分布の切り替え。TOband だけの操作）は
 * 画面から届かなかった（キーボードで `E` を打つしかない）。`capture()` の後で窓へ足す。
 * 窓の種類・版は変えない。足した操作の翻訳は `NativeModernMenu` の `input()` では出来ない（`current_` に無い）ので、ここで翻訳し、
 * 版・要求番号の管理は `NativeModernMenu` に任せる（`tb_birth_stats.h` と同じ作り）。
 */
#pragma once

#include "bridge/input_event_adapter.h"
#include "bridge/native_modern.h"
#include "frame/game_frame.h"

#include <vector>

namespace tb {

/*! @brief 全体地図の窓なら、`E`（エレメントの分布）の操作を足す。 */
void apply_window_extras(GameFrame &frame);

/*! @brief 足した操作の `window_action` をキーへ翻訳する。扱ったら true（受理のときだけ `keys` に積む）。 */
bool window_extras_input(const presentation::InputEventWire &event, std::vector<int> &keys, presentation::NativeModernMenu &menu);

} // namespace tb
