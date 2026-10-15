/*!
 * @file hengband_source_version.h
 * @brief いま組んでいる変愚系の原作ソースの版を、画面寄りのコードから見分ける。
 *
 * 変愚（HengbandCore）は 3.0.2.5-Beta の原作ソースを、短愚（TangbandCore）は 3.0.2.4-Beta の写しを組む
 * （短愚は版上げしていないので、変愚の版上げで変えない）。
 * `presentation/` の同じソースを両方で組むので、原作の API が版で違う所だけをこの印で分ける。
 *
 * 見分け方は「3.0.2.5 で足されたヘッダが見えるか」。インクルードパスはコアごとに自分の原作ソースを指すので、
 * 変愚では見え、短愚では見えない。版の番号（`H_VER_*`）は短愚が自分の番号（26.0.4）を名乗るので使えない。
 *
 * **3.0.2.4 側には、分ける前のコードをそのまま残すこと**（短愚のコードを 1 文字も変えないため）。
 */
#pragma once

#if __has_include("mspell/mspell-projection-table.h")
#define HB_HENGBAND_SOURCE_3025 1
#else
#define HB_HENGBAND_SOURCE_3025 0
#endif
