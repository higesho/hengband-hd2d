/*!
 * @file core_link.h
 * @brief 外部ゲームコアとの通信、起動、終了処理（プロトコル v1）。
 *
 * UI はこの型から受け取るフレームと通知だけを使い、コアの内部状態を参照しない。
 * フレームは最新値を保持し、捨てるフレームの burst と効果音は持ち越す。
 * 受信スレッドは通知と持ち越す音を解釈し、フレーム全体の復元と描画は UI が行う。
 *
 * Windows は子プロセスを起動する。終了時は stdin を閉じて保存の猶予を与え、
 * 必要なら子を終了させる。受信と stderr のスレッドを join した後に資源を解放する。
 * Android は同一プロセス内のコアスレッドであり、受信終了まで共有状態を保持する。
 */
#pragma once

#include "frame/protocol_messages.h"
#include "frame/sound_event.h" //!< `take_latest_frame` が捨てたフレームの音を持ち越す

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hd2d {

//! `CoreLink::start` の引数（起動の作法だけ。プロトコルの内容は含まない）。
struct CoreLinkOptions {
    //! `HengbandCore.exe` の場所。空なら自 exe と同じディレクトリを見る。
    std::filesystem::path core_path;
    //! 自分が解釈しなかった起動引数。**全部そのまま子へ渡す**（`--bot-json-output=` 等）。
    std::vector<std::string> forwarded_args;
    //! `--protocol-log=`（こちら側の会話ログ）。空なら残さない。
    std::string protocol_log_path;
    //! `--core-protocol-log=`（コア側に書かせる会話ログ）。空なら渡さない。
    std::string core_protocol_log_path;
#if !defined(_WIN32)
    /*!
     * @brief コアスレッドの入口（Android は子プロセスではなく同一プロセスのスレッド。
     * ）。**platform 層（入口）が差す。**
     * @details 型は `hengband_core_protocol_main`（`platform/shared/core_protocol_main.h`）
     * そのもの。hd2d/ からコアのヘッダを include しないための関数ポインタである
     * （設計書 必守制約 4「情報はプロトコル経由のみ」の結線版）。
     */
    int (*core_thread_main)(void *in, void *out, const std::string &protocol_log_path,
        const std::vector<std::string> &forwarded_args){ nullptr };
#endif
};

/*!
 * @brief コアを起こして繋ぎ、フレームを受け取り、入力を送る。
 * @note 1 プロセスに 1 個。コピー禁止。
 */
class CoreLink {
public:
    CoreLink() = default;
    ~CoreLink();
    CoreLink(const CoreLink &) = delete;
    CoreLink &operator=(const CoreLink &) = delete;

    /*!
     * @brief コアを起こす（パイプ 3 本 ＋ `CreateProcess`）。まだ握手はしない。
     * @param[out] err 失敗の理由（利用者に見せてよい日本語）。
     */
    bool start(const CoreLinkOptions &options, std::string &err);

    /*!
     * @brief `hello` を送って `hello_ack` を待つ（v1 §3.1）。
     * @details 待つのはここだけ（上限 30 秒）。窓は呼び出し側が先に出している前提。
     * 握手の**前**に他の種別が来たら異常として扱う（§2.3 の「黙って捨てる」は握手の後の規則）。
     */
    bool handshake(std::string &err);

    //! 握手で受け取った `hello_ack`。`handshake` が成功した後だけ意味を持つ。
    const presentation::HelloAckMessage &hello_ack() const { return this->ack_; }

    //! 受信スレッドを起こす。**握手の後に 1 回だけ**呼ぶこと。
    void begin_receiving();

    /*!
     * @brief 再生モードで 1 周ぶん配る。**環の頭で 1 回だけ**呼ぶ。
     * @details 再生でないときは何もしない。`HD2D_REPLAY_LOG` に会話の記録
     * （`--protocol-log=` で採ったもの）を渡すと、コアを起こさずにそこから配る。
     * 別スレッドを使わないので、配られる順が走らせるたびに変わることがない。
     */
    void replay_step();

    /*! @brief いま再生モードか。 */
    bool replaying() const;

    /*!
     * @brief 溜まっている最新のフレームを取り出す（v1 §6.4 の最新値スロット）。
     * @param[out] wire フレームのペイロード（JSON）。
     * @param[out] carried_burst 捨てたフレームに `teleport_fx.burst` が立っていた。
     * @param[out] carried_sounds 捨てたフレームに載っていた効果音（**古い順**。空にしてから書く）。
     * @return 新しいフレームがあったか。false のとき出力は触らない。
     * @details 一度きりの縁（`burst` と `sounds`）は最新値スロットで消える。
     * **取り出しと同じロックの中で**持ち越さないと、拾った音が次の取り出しへずれる。
     */
    bool take_latest_frame(std::string &wire, bool &carried_burst, std::vector<SoundEvent> &carried_sounds);

    //! コアから届いた `pad_commands` / `sub_panel_kinds` / `asset_manifest` を全部持っていく。
    std::vector<std::string> take_table_payloads();

    bool send_ui_state(const presentation::UiStateMessage &message);
    bool send_input_events(const presentation::InputEventsMessage &message);
    bool send_keys(const presentation::KeysMessage &message);
    bool send_quit_request();

    bool exit_received() const;
    int exit_code() const;
    bool fatal_received() const;
    //! `fatal` の理由（受け取っていなければ空）。
    std::string fatal_reason();
    //! stdout の EOF / 読み書き失敗を見た（＝コアが消えた。v1 §9.2）。
    bool core_gone() const;
    //! 子プロセスが終わっているか（待たずに見るだけ）。
    bool child_exited() const;

    //! コアの stderr の直近 `lines` 行。異常終了の理由を利用者へ運ぶのに使う（v1 §13-4）。
    std::string core_stderr_tail(std::size_t lines);
    //! コアの stderr の全文ログの場所（開けていなければ空）。
    std::string core_stderr_log_path();
    //! ダイアログに出す既定の行数。
    static constexpr std::size_t kStderrDialogLines = 6;

    /*!
     * @brief コアの stdin を閉じる（＝「ui が消えた」のと同じ合図。v1 §9.2）。
     * @details コアはこれを見て緊急セーブしてから畳む。**kill の前に必ずこれ。**
     */
    void close_core_stdin();

    //! コアが畳み終わるのを `timeout_ms` まで待つ。戻り値は畳んだか。
    bool wait_for_core_exit(unsigned timeout_ms);

    /*!
     * @brief 利用者が明示に「終わらせる」を選んだときの畳み方（v1 §9.2）。
     * @details まず stdin を閉じて緊急セーブの機会を与え、それでも死ななければ `TerminateProcess`。
     * **いきなり殺さない**のがこの手順の要点（セーブ中に殺すのが最悪）。
     */
    void force_quit();

    //! 保存の猶予を与え、Windows では受信終了を待って資源を解放する。複数回呼べる。
    void shutdown();

private:
    struct Impl;
    //! Win32 のハンドル・スレッド・同期を隠す（この宣言に `<windows.h>` を持ち込まないため）。
    std::shared_ptr<Impl> impl_;
    presentation::HelloAckMessage ack_;
};

} // namespace hd2d
