/*!
 * @file pad_command_table.cpp
 * @brief コアの `menu_info` を平坦化して `pad_command_table()` を充填し、
 * キーマップ逆引きの解決関数を登録する。
 *
 * ## なぜ逆引きが要るか（§2.1 の罠）
 * `menu_info` の `cmd` は**押されたキーではなくキーマップ変換後の Angband コマンド**である。
 * 根拠（実際に読んだ場所）:
 *   - `src/io/input-key-requester.cpp:87`
 *     `keymap_actions_map.at(this->mode).at(cmd)` を通してから `command_cmd` になる。
 *     `mode` は同 `:46` の `rogue_like_commands ? KeymapMode::ROGUE : KeymapMode::ORIGINAL`。
 *   - `src/cmd-io/cmd-menu-content-table.cpp` のラベルが `_("調べる(b/P)")` のように
 *     2 つのキーを併記している（`b`=オリジナル / `P`=ローグライク）。
 *   - `lib/pref/pref-key.prf` の `##### Roguelike Keyset Mappings #####` に
 *     `A:b` / `C:1:P`（= ROGUE では `P` を押すと `b` コマンド）が並んでいる。
 * したがって `cmd` をそのまま `KeyQueue` へ積むと、**ローグライク配列では別のコマンドになる**。
 * たとえば `b`（調べる）をそのまま積むと ROGUE では `A:;1`（南西へ移動）が走る。
 *
 * ## 逆引きの実装（前例に合わせる）
 * `src/io/input-key-requester.cpp:278-303` の `get_caret_command()` が
 * `keymap_actions_map` を 0..255 走査して「動作が `command_cmd` 1 文字と一致する `i`」を
 * 探している。ここでも**同じ形**で `i` を探し、見つかればその `i` を押すべきキーとする。
 */
#include "frame/pad_command_table.h"

#include "cmd-io/cmd-menu-content-table.h"
#include "game-option/input-options.h"
#include "io/macro-configurations-store.h"
#include "locale/character-encoding.h"

#include <deque>
#include <string>

namespace {

/*!
 * @brief UTF-8 に直した表示文字列の置き場。
 * @details `PadCommandEntry` は `const char *` を持つので、指す先が動かない容器が要る。
 * `std::deque` は push_back で既存要素の参照・ポインタを無効化しない。
 */
std::deque<std::string> g_label_pool;

const char *intern_utf8(const char *sys_str)
{
    if (sys_str == nullptr) {
        sys_str = "";
    }
    const std::string sys(sys_str);
    if (const auto utf8 = sys_to_utf8(sys)) {
        g_label_pool.push_back(*utf8);
    } else {
        g_label_pool.push_back(sys);
    }
    return g_label_pool.back().c_str();
}

KeymapMode keymap_mode_of(PadKeymapMode mode)
{
    switch (mode) {
    case PadKeymapMode::Original:
        return KeymapMode::ORIGINAL;
    case PadKeymapMode::Rogue:
        return KeymapMode::ROGUE;
    case PadKeymapMode::Current:
    default:
        return rogue_like_commands ? KeymapMode::ROGUE : KeymapMode::ORIGINAL;
    }
}

/*!
 * @brief command（オリジナル配列の Angband コマンド）→ 指定配列でそれを出すキー列。
 * @details `src/io/input-key-requester.cpp:278-303` と同じ走査で、
 * 「動作が command 1 文字ちょうど」のキー `i` を探す。見つかればそれを押す。
 *
 * @note **逆引きが失敗したときのフォールバックを検証する（K-2）**
 * 走査で見つからなかったとき、以前は無条件に `command` 自身を返していた。しかし
 * **`command` 自身がその配列でキーマップされていたら、押した瞬間に別のコマンドが走る**。
 * `src/io/input-key-requester.cpp:87` はキュー由来のキーを必ず
 * `keymap_actions_map.at(mode).at(cmd)` に通すので、たとえば利用者が `@`
 * （`src/cmd-io/cmd-macro.cpp` のキーマップ編集画面）や自前 pref で
 * 「`r` を押したら `q`（薬を飲む）」と書き換えると、
 *   - `r` を出すキーはどこにも無くなる（走査は失敗）
 *   - フォールバックで `r` を積む → コアが `q` に変換して**薬を飲む**
 * となり、設計書 §2.1 でわざわざ避けた罠に経路を変えて戻ってしまう。
 * そこで `command` 自身がキーマップ済みなら**空を返す**（＝そのコマンドは出せないので
 * 何もしない）。**黙って誤ったキーを積むより無反応が安全**。
 * 空でないフォールバックを返すのは「`command` を押せば無変換で `command` になる」と
 * 確かめられたときだけ。
 *
 * @note 戻り値が 1 バイトでない場合は無い（キーマップは「1 キー → 動作文字列」なので、
 * 逆引きの結果は常に 1 キー）。それでも列で返すのは、将来 2 キー以上を要する
 * コマンドが出たときに呼び出し側を変えずに済ませるため。
 */
std::vector<uint8_t> resolve_pad_command_keys(uint8_t command, PadKeymapMode mode)
{
    if (command == 0) {
        return {};
    }

    const auto km = keymap_mode_of(mode);
    const auto &actions = keymap_actions_map.at(km);
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto &action_opt = actions.at(i);
        if (!action_opt) {
            continue;
        }
        const auto &action = *action_opt;
        if ((action.length() == 1) && (static_cast<uint8_t>(action[0]) == command)) {
            return { static_cast<uint8_t>(i) };
        }
    }

    // 逆引き失敗。`command` 自身がキーマップされているなら押しても別物になるので出さない。
    if (static_cast<size_t>(command) < actions.size()) {
        if (actions.at(static_cast<size_t>(command))) {
            return {};
        }
    }
    return { command };
}

} // namespace

namespace presentation {

/*!
 * @brief `menu_info` を平坦化して表を充填し、解決関数を登録する。
 * @details 呼ぶのは `init_angband` の後（`run_sdl_game`）。
 * `menu_info` 自体は静的初期化済みだが、逆引きが見る `keymap_actions_map` は
 * `init_angband` 内の `process_pref_file("pref.prf")` で初めて埋まる
 * （`src/main/angband-initializer.cpp:238`）。順序を守らないと
 * 「表はあるが逆引きが常に恒等」という**動いて見えて誤動作する**状態になる。
 */
void register_pad_command_table()
{
    g_label_pool.clear();

    std::vector<PadCommandEntry> entries;
    uint16_t next_id = 0;

    // menu_info[0] は分類（fin == false で cmd がサブメニュー番号）。
    for (int g = 0; g < MAX_COMMAND_PER_SCREEN; ++g) {
        const auto &group = menu_info[0][g];
        if (group.fin) {
            continue; // Root に直接コマンドが増えたら（今は無い）ここでは拾わない
        }
        if (group.name.get()[0] == '\0') {
            continue;
        }
        const int sub = static_cast<int>(group.cmd);
        if ((sub <= 0) || (sub >= MAX_COMMAND_MENU_NUM)) {
            continue;
        }
        const char *group_utf8 = intern_utf8(group.name.get());

        for (int k = 0; k < MAX_COMMAND_PER_SCREEN; ++k) {
            const auto &item = menu_info[sub][k];
            // fin == true の項目だけがコマンド（false はサブメニューへの分岐）。
            if (!item.fin) {
                continue;
            }
            if ((item.name.get()[0] == '\0') || (item.cmd == 0)) {
                continue;
            }
            PadCommandEntry entry{};
            entry.id = next_id++;
            entry.command = static_cast<uint8_t>(item.cmd);
            entry.group_utf8 = group_utf8;
            entry.label_utf8 = intern_utf8(item.name.get());
            entries.push_back(entry);
        }
    }

    set_pad_command_table(std::move(entries));
    set_pad_command_resolver(&resolve_pad_command_keys);
}

} // namespace presentation
