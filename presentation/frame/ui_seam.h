/*!
 * @file ui_seam.h
 * @brief 層分離用の中立 seam 型（設計レビュー M1）
 *
 * 所属は presentation/frame 専属（中立ヘッダ）。presentation と合成ルートだけが include する。
 * ここには SDL 依存もコア依存（PlayerType/term_type/TERM_XTRA_/term include）も置かない。
 *
 * - 型だけの中立ヘッダなので、どちら側から include しても依存が増えない。
 * - presentation/term（null term）が UiSeam を保持し、フック内から呼ぶ。
 * - 合成ルート（`platform/windows/core_main.cpp`）が UiSeam を組み立てて presentation へ注入する。
 */
#pragma once

#include "frame/game_frame.h"

#include <cstdint>
#include <functional>
#include <vector>

/*!
 * @brief SDL 側で翻訳済みの入力キー列（ASCII/コマンド文字コード）。
 * @details 画面側が入力を翻訳して push、presentation が term_key_push で消費する。
 */
using KeyQueue = std::vector<int>;

/*!
 * @brief 画面側の機能へのコールバック束。presentation はこれ越しにのみ画面側を呼ぶ。
 */
struct UiSeam {
    //! capture 直前フック（platform が Bridge::set_view_size 等を束ねる。H1）。
    //! 画面側は Bridge を知らない。null term の on_fresh 先頭で呼ぶ。
    std::function<void()> before_capture;
    //! 1 フレーム描画（recompute → begin → draw → end、present は末尾一度）。
    std::function<void(const GameFrame &)> present;
    //! SDL イベントを pump し、翻訳済みキーを out に追加する（待機はしない）。
    std::function<void(KeyQueue &out)> pump_input;
    //! 指定ミリ秒待機（SDL_Delay 相当）。
    std::function<void(int ms)> delay_ms;
    //! ウィンドウクローズ等で終了要求が出ているか。
    std::function<bool()> quit_requested;
    /*!
     * @brief 単調増加のミリ秒（SDL_GetTicks 相当）。
     * @details リアルタイムモードの締切に使う。
     * **束ねられていなければリアルタイムは働かず、従来どおりのターン制に落ちる。**
     * Android は 1 プロセス 2 スレッドなので、基準時計はここ 1 つに揃えること。
     */
    std::function<uint32_t()> now_ms;
    /*!
     * @brief いま押しっぱなしの方向キー（'1'〜'9'）。無ければ 0。
     * @details 「ターン切り替え時に押されていたキーが次ターンの入力」（決めたこと）を
     * OS のキーリピートに頼らず実現するための手段。リピートは初回 500ms 待つので刻みに合わない。
     */
    std::function<int()> held_dir_key;
};
