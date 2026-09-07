/*!
 * @file android_log_redirect.h
 * @brief `stdout` / `stderr` を logcat へ流す
 *
 * 既存コードは診断を `std::fprintf(stderr, ...)` で出している
 * （フォントの選択結果・タイル読み込みの失敗・HD2D の退避理由・音声の初期化など）。
 * Android では標準出力の行き先が `/dev/null` なので、**そのままだと調査の手掛かりが
 * 全部消える**。Windows で `SubSystem=Windows` にコンソールが無いのと同じ問題で、
 * あちらは機能メニューのステータス行を用意して凌いだ（G-3）。こちらは logcat がある。
 */
#pragma once

namespace platform_android {

/*!
 * @brief `stdout` / `stderr` を logcat（タグ `hengband`）へ繋ぐ。
 * @details 何度呼んでも 1 度しか繋がない。失敗しても起動は続ける（診断が出ないだけ）。
 * **`SDL_Init` より前に呼ぶ**（SDL 自身の診断も拾えるように）。
 */
void redirect_stdio_to_logcat();

} // namespace platform_android
