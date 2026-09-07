/*!
 * @file frame_codec.h
 * @brief `GameFrame` ⇔ プロトコル v1 の frame ワイヤ形式（JSON）の相互変換。
 *
 * 基準はプロトコル v1 の §2（フレーミングと符号化）と
 * §6（frame の直列化仕様）。**型の定義は `frame/*.h`**（キー名は構造体メンバ名の snake_case）。
 *
 * ## 中立性（これを壊すと 2 プロセス分割で詰む）
 * この TU が include してよいのは **`frame/` のヘッダと JSON ライブラリだけ**である。
 * SDL・コア型（`PlayerType` / `term_type` / `angband_terms`）は一切知らない。
 * 分割後は core 側（encode）と ui 側（decode）の**両方**がこのファイルをリンクするので、
 * どちらか一方にしか無いものへ依存した瞬間に片側がビルドできなくなる。
 *
 * ## 正準出力（canonical）
 * 同じ `GameFrame` からは常に同じバイト列が出る。根拠は 3 つ:
 *   1. 既定値のフィールドは**必ず省略**する（v1 §6.1 の省略規則を「してよい」ではなく
 *      「する」に固定した。省略するかどうかが揺れると同じ値から 2 通りのバイト列が出る）。
 *      ただし **`t` と `frame_id` は例外で常に出す**。会話ログ（§11.2）を frame_id で
 *      追えることを数バイトより優先する、という v1 §6.1 の裁定。
 *   2. キー順は `nlohmann::json` の既定（`std::map` の辞書順）。値に依らず決まる。
 *   3. 浮動小数（`TeleportFx::charge`）は `double` へ広げて出す。`nlohmann` の
 *      シリアライザは **double へ戻したとき同じ値になる最短表記**を書くので、
 *      `float` → `double` → 文字列 → `double` → `float` は無損失。
 */
#pragma once

#include "frame/game_frame.h"

#include <string>

namespace presentation {

/*!
 * @brief `GameFrame` をプロトコル v1 の frame メッセージ（`"t":"frame"` を含む完全な JSON）にする。
 * @return UTF-8 の JSON オブジェクト 1 個。長さ枠（v1 §2.1 の u32 前置）は**付けない**
 *   （フレーミングはトランスポートの仕事で、codec の仕事ではない）。
 */
std::string encode_game_frame(const GameFrame &frame);

/*!
 * @brief `encode_game_frame` の逆。
 * @param[out] out 成功時のみ書き換わる（失敗時は触らない）。
 * @param[out] err 失敗した理由。
 * @return 成功したか。JSON 破損・`"t"` 不一致・cells タプル長違い・base64 破損は失敗。
 * @note **既知メッセージ内の未知キーは黙って無視する**（v1 §2.3）。
 */
bool decode_game_frame(const std::string &json_text, GameFrame &out, std::string &err);

/*!
 * @brief 捨てるフレームから **`sounds` だけ**を取り出す（v1 §6.4 の救済）。
 * @param[out] out 追記する（呼ぶ側が溜める器を持つ）。**成功しても 0 件はある。**
 * @return 読めたか（`sounds` が無いフレームでも真）。
 * @details 受け手は最新値スロットで古いフレームを捨てる（`CoreLink::take_latest_frame`）。
 * 効果音は**その 1 フレームにしか載らない一度きりの縁**なので、捨てると鳴らない
 * （2026-08-21 に気づいた「効果音がたまにしかならなくなった」）。`teleport_fx.burst` を
 * 持ち越しているのと同じ理由・同じ場所で拾うためにこれがある。
 * @note フレーム全部を解くのは高くつくので、**呼ぶ側が `"sounds"` の有無で先に篩う**こと。
 */
bool decode_frame_sounds(const std::string &json_text, std::vector<SoundEvent> &out, std::string &err);

/*!
 * @brief 2 つの `GameFrame` が**フィールド単位で**等しいか。
 * @param[out] first_diff 異なるとき、最初に見つかった差異の場所と中身（空文字列を渡すこと）。
 * @details `encode` の結果の byte 比較とは**別の証拠**として使う。byte 比較だけだと
 * 「encode が落としたフィールド」は両側で等しく落ちるので永久に見つからない。
 * 実装は `game_frame.h` の全メンバを手で列挙している（`frame_codec_check.cpp` の
 * 合成フレーム生成との二重簿記。詳細はそちらの冒頭コメント）。
 */
bool game_frames_equal(const GameFrame &a, const GameFrame &b, std::string &first_diff);

//! base64（標準アルファベット・`=` 詰め）。ミニマップの `kinds` に使う（v1 §6.3）。
std::string frame_base64_encode(const std::vector<uint8_t> &bytes);
//! @return 破損（アルファベット外・長さが 4 の倍数でない）なら false。
bool frame_base64_decode(const std::string &text, std::vector<uint8_t> &out);

} // namespace presentation
