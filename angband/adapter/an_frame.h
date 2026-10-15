/*!
 * @file an_frame.h
 * @brief Angband 4.2 のマップ・HUD・メッセージを `GameFrame` へ写す（設計 §6.2。S2）。
 *
 * `an_main.cpp`（プロトコル本体）から呼ばれる。ここだけが `an_map_shim.h` と
 * `an_manifest.h` の両方を知る——マス読み取りと名前対応づけを 1 か所にまとめる。
 */
#pragma once

#include "frame/game_frame.h"

#include "an_manifest.h"

namespace an {

/*!
 * @brief マップ（`frame.cells`）・左状態列・最下段・HUD・メッセージを埋める。
 * @details `frame.menu_open` が真の間は呼ばないこと（Term の写しだけを送る場面。
 * `an_main.cpp` 側で分岐する）。
 */
void fill_game_frame(GameFrame &frame, const TileMapping &mapping);

/*!
 * @brief `frame.messages` だけを埋める。
 * @details 見直しの指摘 9: `fill_game_frame()` は `frame.menu_open` の間は呼ばない
 * 契約だが、メッセージ（`plog()`/`msg_print()` 相当）はメニューが開いていても
 * 積まれる（例: 終了を拒んだ理由）。`build_frame()` は `menu_open` の分岐と関係なく
 * 毎回これを呼び、メニュー表示中でもメッセージが画面へ届くようにする。
 *
 * AH-01/AH-02 の直し（見直しの指摘。ここの説明も古かった）: ほかのコアと同じく、
 * `an_take_message()` でキューを先入れ先出しに消費して空にするのではなく、
 * `an_recent_message_count()`/`an_get_recent_message()` で直近 `kMessageLines` 行
 * （`an_frame.cpp`）を**消費せず**毎フレーム丸ごと送る。差分で送ると、画面側の
 * 最新値スロットで捨てられたフレームの分が読めないまま消えるため。
 */
void fill_messages(GameFrame &frame);

/*!
 * @brief 魔法・投射物の効果（`GameFrame::projections`・`missiles`。`presentation/bridge/named_attack_fx.h`）に、
 * いま汲めるものがあるか。
 * @details `host_present()` は「手番と文字の格子が前回と同じならフレームを組まない」安いしきい値を持つ。
 * 魔法・矢弾の記録が溜まっているときはそれを飛ばして組む（記録は汲まれるまで残るので、組まないと画面に届かない）。
 */
bool attack_fx_ready();

} // namespace an
