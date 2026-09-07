/*!
 * @file core_protocol_main.h
 * @brief プロトコル v1 のコア側本体の入口（実体は `platform/windows/core_main.cpp`）。
 *
 * 基準はプロトコル v1 の仕様。
 *
 * ## なぜ「windows」の TU に実体があるのか
 * コア側の駆動（握手 → 受信スレッド → `run_sdl_game`）は Windows の
 * `HengbandCore.exe` として先に完成しており、中身はほぼ可搬だった。
 * Androidは**同じ TU をそのまま組み**、
 * `main()` の代わりにこの関数をコアスレッドの入口として呼ぶ。
 * 写しを作らないための置き方であって、Windows 専用という意味ではない
 * （Windows 固有の箇所は TU 内の `#if defined(_WIN32)` に閉じてある）。
 */
#pragma once

#include "frame/protocol_transport.h"

#include <string>
#include <vector>

/*!
 * @brief プロトコル v1 のコア側本体。握手し、受信スレッドを起こし、`run_sdl_game` を回す。
 * @param in  読み取り端（Windows: 自プロセスの stdin ／ Android: ui → core パイプの読み口）。
 * @param out 書き込み端（Windows: 自プロセスの stdout ／ Android: core → ui パイプの書き口）。
 * @param protocol_log_path `--protocol-log=` 相当（空なら残さない）。
 * @param forwarded_args ui から流れてきた起動引数。`--bot-json-output=-` の検出にだけ使う
 * （stdout ガード。プロトコルの通り道を bot JSON に潰されないため）。
 * @return 終了コード。**ただし正常系では戻らない**（`run_sdl_game` は末尾で
 * `quit("")` → `std::exit(0)` する）。Android では 1 プロセスに同居するので、
 * これは**アプリごと終わる**ことを意味する。
 */
int hengband_core_protocol_main(presentation::TransportHandle in, presentation::TransportHandle out,
    const std::string &protocol_log_path, const std::vector<std::string> &forwarded_args);
