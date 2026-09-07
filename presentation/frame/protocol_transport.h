/*!
 * @file protocol_transport.h
 * @brief プロトコル v1 のフレーミング（u32 LE 長さ前置）を読み書きする中立トランスポート層。
 *
 * 基準はプロトコル v1 の §2.1（フレーミング）と
 * §9.3（プロトコル違反）。実装計画は分割の第 4 段の §2。
 *
 * ## 中立性（frame_codec.h 冒頭と同じ規約）
 * この TU が知ってよいのは **標準ライブラリと OS API だけ**である。SDL・コア型
 * （`PlayerType` / `term_type` / `angband_terms`）・`GameFrame` すら知らない。
 * 分割後は core 側と ui 側の**両方**がこのファイルをリンクするので、
 * どちらか一方にしか無いものへ依存した瞬間に片側がビルドできなくなる。
 *
 * ## 責務の線引き
 * ここは「長さ枠を付ける／外す」だけを行う。ペイロードの中身（JSON）は
 * `protocol_messages.h` と `frame_codec.h` の担当で、こちらは一切解釈しない。
 *
 * ## Windows 以外
 * v1 の 2 プロセス**分割**は Windows だけで行うが、
 * この輸送層そのものは POSIX 版も持つ（`.cpp` の `#else` 側）。Android の HD2D 版が
 * **1 プロセス 2 スレッド＋ `pipe()`** で同じフレーミングを流す
 * 端点は fd を `transport_from_fd` で包んだもの。
 */
#pragma once

#include <cstdint>
#include <string>

namespace presentation {

/*!
 * @brief トランスポートの端点。**実体は Win32 の `HANDLE`**（＝`void *`）。
 * @details ヘッダから `windows.h` を追い出すための別名。`HANDLE` をそのまま渡してよい。
 */
using TransportHandle = void *;

//! ペイロード長の上限（v1 §2.1）。これを超える長さを読んだらプロトコル違反として切る。
inline constexpr uint32_t kProtocolMaxPayloadBytes = 64u * 1024u * 1024u;

//! `read_message` の結果。`Ok` 以外はいずれも「この接続はもう使えない」を意味する。
enum class TransportResult {
    Ok, //!< 1 メッセージを読み切った
    Eof, //!< 相手が閉じた（パイプ切断・EOF）。異常ではない
    IoError, //!< OS の読み取りが失敗した
    TooLarge, //!< 長さが 64MB を超えた（v1 §9.3 のプロトコル違反）
    Truncated, //!< 長さの途中／ペイロードの途中で EOF（フレーミング破損）
};

//! ログ・エラーメッセージ用の名前。
const char *transport_result_name(TransportResult result);

/*!
 * @brief 長さ前置のメッセージを 1 個読む。読み切るまでブロックする。
 * @param[in]  handle 読み取り側の端点（core の stdin ／ ui が握る子の stdout）。
 * @param[out] payload 成功時のみ書き換わる（失敗時は触らない）。UTF-8 の JSON 1 個のはず。
 * @param[out] err 失敗した理由（`Ok` のときは触らない）。
 * @details 長さ 0 のメッセージは `payload` が空文字列の `Ok` として返る（枠としては正当）。
 */
TransportResult read_message(TransportHandle handle, std::string &payload, std::string &err);

/*!
 * @brief 長さ前置のメッセージを 1 個書く。**全バイト書き切るまで戻らない**。
 * @return 書き切れたか。false のとき `err` に理由が入る。
 * @details 64MB 超のペイロードは送信を拒否する（相手が §9.3 で切るものを送らない）。
 * ヘッダ 4 バイトとペイロードは 1 回の書き込みにまとめる（別々に書くと、相手が
 * 長さだけ読んだところで送信側が死んだときに相手を長時間ブロックさせるため）。
 */
bool write_message(TransportHandle handle, const std::string &payload, std::string &err);

/*!
 * @brief 自プロセスの stdin / stdout をバイナリモードにする。
 * @details テキストモードのままだと `0x0A` が `0x0D 0x0A` に化けて長さ枠が壊れる。
 * core は起動直後に、ui は（子のパイプしか使わないので）呼ばなくてよいが、
 * 呼んでも害はない。
 */
void set_stdio_binary_mode();

//! 自プロセスの stdin ハンドル。取得できなければ `nullptr`。
TransportHandle transport_stdin();

#if !defined(_WIN32)
/*!
 * @brief POSIX の fd を端点に包む（fd + 1 を詰める。fd 0 と nullptr の衝突を避けるため）。
 * @details Android の HD2D 版が `pipe()` の両端をこれで包んで使う。負の fd は nullptr。
 */
TransportHandle transport_from_fd(int fd);
#endif

//! 自プロセスの stdout ハンドル。取得できなければ `nullptr`。
TransportHandle transport_stdout();

} // namespace presentation
