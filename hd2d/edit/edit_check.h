/*!
 * @file edit_check.h
 * @brief エディタ（P9）の検査 `--edit-check`。**窓も GL も要らない。**
 *
 * ## 何を見るか
 * 1. 既存の全プレハブが 書く → 読み戻す → 意味が等しい（往復）
 * 2. 同じものを 2 回書くと**バイトまで同じ**（正準。frame codec の canonical と同じ考え方）
 * 3. 編集操作の一連（置く・消す・育てる・分割・削除・移動・親・パレット）の後も
 *    保存と読み戻しが通り、メッシュ化もできる
 * 4. レイ判定（画面 → レイ → ボクセル）が当たるべきものに当たり、外すべきものを外す。
 *    投影との往復（画素 → レイ → 当たり）も見る
 *
 * ## 検査の検査（設計書 §14-4。**FAIL になるのが正しい**）
 * ```
 * $env:HD2D_BREAK_EDIT='vox';       .\HengbandHd2d.exe --edit-check   # (1) で落ちる
 * $env:HD2D_BREAK_EDIT='footprint'; .\HengbandHd2d.exe --edit-check   # (3) で落ちる
 * $env:HD2D_BREAK_EDIT=''           # 戻すのを忘れない
 * ```
 */
#pragma once

#include "app/hd2d_app.h"

namespace hd2d {

//! 実行する。0 = PASS。
int run_edit_check(const AppOptions &options);

} // namespace hd2d
