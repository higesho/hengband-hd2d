#pragma once
/*!
 * @file rl_sound.h
 * @brief 音の出来事の環。Frox/Sil-Q と同じ流儀。
 *
 * 建付けは `frox/adapter/fc_shim.h` の `fc_sound_*` と同じ環である。違いは、
 * Rogue 3.6/5.4 の原作コードは Angband の `SOUND_*` のような番号付きテーブルを
 * 持たないので、**呼び出し側が名前とマスを直に渡す**（`rl_` 頭の専用の名前。
 * `tools/gen_sfx_catalog.py` の `ROGUE_SFX`）。
 *
 * 積むのは `tools/rogue_legacy/prepare_sources.py` のパッチで原作の出来事へ差した
 * 呼び出し箇所、汲むのは `rl_frame.cpp`（`fill_sounds`）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//! 音 1 つ。`name` は `rogue_hit` のような綴り（NUL 終端）。
typedef struct rl_sound_event {
    char name[24];
    short y;
    short x;
} rl_sound_event;

/*!
 * @brief 画面側が音の出来事を欲しがっているか（`ui_state.sound_events`）を覚える。
 * @details 偽の間は 1 つも積まない。Rogue 3.6/5.4 は自分では鳴らせない
 * （winmm 等を叩く実体を持たない）ので、真のときだけ `frame.sounds` へ書き留める。
 */
void rl_sound_set_wanted(int wanted);

//! 出来事を 1 つ積む。`name` が空、または欲しがっていなければ捨てる。
void rl_sound_push(const char *name, int y, int x);

//! 溜まっている音を汲んで消す。@return 書いた個数
int rl_sound_take(rl_sound_event *out, int cap);

#ifdef __cplusplus
}
#endif
