/*!
 * @file hengband_source_version.h
 * @brief いま組んでいる変愚系の原作ソースの版を、画面寄りのコードから見分ける。
 *
 * 変愚（HengbandCore）は 3.0.2.6-Beta の原作ソースを、短愚（TangbandCore）は 3.0.2.4-Beta の写しを組む
 * （短愚は版上げしていないので、変愚の版上げで変えない）。
 * `presentation/` の同じソースを両方で組むので、原作の API が版で違う所だけをこの印で分ける。
 *
 * 見分け方は「その版で足されたヘッダが見えるか」。インクルードパスはコアごとに自分の原作ソースを指すので、
 * 変愚では見え、短愚では見えない。版の番号（`H_VER_*`）は短愚が自分の番号（26.0.4）を名乗るので使えない。
 *
 * - `HB_HENGBAND_SOURCE_3025`: 3.0.2.5 以降（3.0.2.6 でも 1）。
 * - `HB_HENGBAND_SOURCE_3026`: 3.0.2.6 以降。`format()`・`term/z-form.h` が無くなり、
 *   `RedrawingFlagsUpdater` が `DeferredUpdateRequests` になった。
 *
 * **古い版の側には、分ける前のコードをそのまま残すこと**（短愚のコードを 1 文字も変えないため）。
 */
#pragma once

#if __has_include("mspell/mspell-projection-table.h")
#define HB_HENGBAND_SOURCE_3025 1
#else
#define HB_HENGBAND_SOURCE_3025 0
#endif

#if __has_include("system/deferred-update-requests.h")
#define HB_HENGBAND_SOURCE_3026 1
#else
#define HB_HENGBAND_SOURCE_3026 0
#endif

//! 3.0.2.7 以降。`save_player()` から保存の種類（`SaveType`）の引数が無くなった（前の `CLOSE_GAME` と同じ振る舞い）。
//! 印は 3.0.2.7 で足された `term/tile-layout.h`（Original32 の 2 倍表示）。 
#if __has_include("term/tile-layout.h")
#define HB_HENGBAND_SOURCE_3027 1
#else
#define HB_HENGBAND_SOURCE_3027 0
#endif
