/*!
 * @file an_targeting.h
 * @brief `get_aim_dir_hook` を新 UI の目標選択（`hb_target_choose()`）へ寄せる
 * （設計 §8.5 T1）。`an_ui_shim.h` と同じ「差し替えるのではなく包む」流儀。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief `get_aim_dir_hook` を包む。**`textui_init()` の後**に呼ぶこと
 * （それより前は `get_aim_dir_hook` がまだ既定実装を指していない）。
 */
void an_install_targeting_hooks(void);

/*!
 * @brief §8.5 T4, A4 の強シンボル。`ui-target.c` の `target_set_interactive()`
 * の先頭から呼ばれる（`tools/angband/prepare_sources.py` の当てる側）。
 * `mode` に `TARGET_LOOK` があれば「見る」（`l`）、`TARGET_KILL` のみなら
 * 目標を決めるコマンド（`*`）——原作の 2 つの呼び出し口はどちらもこの
 * 1 関数を通るので、ここ 1 か所で両方を新 UI へ寄せられる。
 * 新 UI が無効（`hb_target_enabled()` が偽）なら `false` を返し、
 * 呼び出し元（弱いデフォルト実装と同じ約束）に原作のまま続けさせる。
 */
bool an_target_set_interactive_hook(int mode, int x, int y, bool allow_pathfinding, bool *result);

#ifdef __cplusplus
} /* extern "C" */
#endif
