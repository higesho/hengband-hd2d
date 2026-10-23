// DCSS コア: 画面の部品（dcss-ui）のゲームパッドの割り当て（`hd2d/dcss/dcss_pad.*`）へ送る、割り当てられるコマンドの表。段 5。
//
// libhd2d.cpp だけが取り込む（原作のヘッダを引く TU。`dcss_tabs_data.h` の後に取り込むこと。`dcss_tabs_data::Out` を使う）。原作のソースは書き換えない。
//
// 背景: 原作の日本語版のパッド対応（`gamepad.cc`・`gamepad-menu.cc`）は `USE_TILE_LOCAL` の中にしか組まれないので、このコア（`USE_TILE_WEB`）には無い。
// そこでパッドの状態機械と割り当ては画面の側で再現し（の案 A）、コアへは**コマンドを符号にしたキー**
// （`encode_command_as_key`。`-(int)cmd`）を送る。画面はコマンドの番号を知らないので、ゲームの中から読める情報だけをここで 1 通にする:
//   - 番号と `CMD_` の名前（割り当ての名前は `bindkey` と同じ綴り）
//   - いまのキーの名前（割り当ての画面の「自動探索 (o)」の括弧の中。`command_to_key` と `keycode_to_name`）
// 割り当てられるかの判定は `gamepad_command_bindable`（`gamepad-menu.cc`。`USE_TILE_LOCAL` の中なので同じ規則で持ち直した）。
// 名前と分類（日本語）は画面の側の表（`tools/dcss/gen_dcss_pad_cmds.py` が作る `hd2d/dcss/dcss_pad_cmds.inc`）。
//
// 呼び出しはゲームのスレッド。
#pragma once

#include <string>

#include "command-type.h"
#include "macro.h"

namespace dcss_pad_data {

//! 原作 `gamepad_command_bindable`（`gamepad-menu.cc`）と同じ規則。メイン画面のコマンドで、内部で使うだけの物を除く。
static bool bindable(command_type cmd)
{
    if (cmd <= CMD_NO_CMD || cmd > CMD_MAX_NORMAL || context_for_command(cmd) != KMC_DEFAULT || command_to_name(cmd) == "CMD_NO_CMD")
        return false;
    switch (cmd)
    {
    case CMD_NO_CMD_DEFAULT:
    case CMD_MOUSE_MOVE:
    case CMD_MOUSE_CLICK:
        return false;
    default:
        return true;
    }
}

/*!
 * @brief 割り当てられるコマンドの表を 1 通の JSON（`{"t":"dcss_ui","k":"pad","cmds":[[番号,"CMD_…","キーの名前"],…]}`）にする。
 * @details キーの名前は、そのコマンドにいま割り当たっているキー（rc の `bindkey` を含む）。無ければ空の字。
 */
static std::string build_json()
{
    dcss_tabs_data::Out o;
    o.raw("{\"t\":\"dcss_ui\",\"k\":\"pad\",\"cmds\":[");
    bool first = true;
    for (int c = CMD_NO_CMD + 1; c <= CMD_MAX_NORMAL; ++c)
    {
        const command_type cmd = static_cast<command_type>(c);
        if (!bindable(cmd))
            continue;
        if (!first)
            o.raw(",");
        first = false;
        o.raw("[");
        o.num((long long)c);
        o.raw(",");
        o.str(command_to_name(cmd));
        o.raw(",");
        const int key = command_to_key(cmd);
        o.str(key != '\0' ? keycode_to_name(key) : std::string());
        o.raw("]");
    }
    o.raw("]}");
    return o.s;
}

} // namespace dcss_pad_data
