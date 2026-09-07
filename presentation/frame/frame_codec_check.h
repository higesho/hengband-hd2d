/*!
 * @file frame_codec_check.h
 * @brief frame codec の自己検査（合成フレーム）と、実プレイでの往復挿し込みの入口。
 *
 * 2 プロセス分割の前に「直列化→復元を描画経路に挟んでも情報が 1 ビットも落ちない」を
 * 証明するための 2 本立て:
 *
 * | 環境変数 | 何をするか |
 * |----------|-----------|
 * | `HENGBAND_SDL2_PROTO_SMOKE=1` | 合成フレーム検査だけ走らせて終了コードを返す（ゲームは起動しない） |
 * | `HENGBAND_SDL2_PROTO_ROUNDTRIP=1` | present に渡る直前のフレームを encode→decode し、**復元した方**を描画へ回す |
 * | `HENGBAND_SDL2_PROTO_LOG=<path>` | 上 2 つのログの出力先（未指定なら `docs/` 配下の既定名） |
 *
 * この TU も codec と同じく **`frame/` と JSON ライブラリしか知らない**
 * （SDL もコア型も include しない）。分割後にどちら側へでも持って行けるようにするため。
 */
#pragma once

#include "frame/game_frame.h"

#include <functional>

namespace presentation {

/*!
 * @brief 合成フレームの encode→decode→比較を回す（`HENGBAND_SDL2_PROTO_SMOKE`）。
 * @return 0 = 全項目 PASS / 非 0 = FAIL 件数。
 */
int run_frame_codec_smoke();

//! `HENGBAND_SDL2_PROTO_ROUNDTRIP` が立っているか。
bool proto_roundtrip_enabled();

/*!
 * @brief `UiSeam::present` を包み、**復元したフレーム**を内側へ渡すようにする。
 * @details 挿し込みは合成ルートの仕事だが、**いまは誰も挿していない**
 * （旧 SDL2 の入口が挿していた）。
 */
std::function<void(const GameFrame &)> wrap_present_with_roundtrip(std::function<void(const GameFrame &)> inner);

} // namespace presentation
