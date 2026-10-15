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

/*!
 * @brief 通し番号が 0 になる先頭の 1 件に振る固定の番号。
 * @details 画面側は番号 0 を「割り当て無し」（`hd2d/ui/key_binds.h` の `kActionNone`）として扱う。
 * 先頭の「使う(m)」に 0 を振ると、操作の割り当ての一覧で見出し扱いになり、
 * キーにもゲームパッドにも割り当てられなかった（2026-10-09）。
 * 番号は cfg の `key_bind=`・`pad_bind=` に書かれるので、**他の命令の番号は動かさず**、
 * 0 になる 1 件だけを別の番号にする（並ぶ順も変えない）。
 * 通し番号は 65 件ほどで 1 から振っていくので、将来の追加（数十件）と重ならないように
 * 十分離した固定値にする。`uint16_t` に収まる。重なったかどうかは
 * `tools/hd2d_verify/pad_command_ids_check.py` が見る。
 */
constexpr uint16_t kFirstEntryId = 1000;

/*!
 * @brief 通し番号を 1 つ振り、`next_id` を進める。
 * @details 番号 0 は予約（上の `kFirstEntryId` の註）なので、0 になるときは `kFirstEntryId` を返す。
 */
uint16_t allocate_entry_id(uint16_t &next_id)
{
    const uint16_t id = next_id++;
    return (id == 0) ? kFirstEntryId : id;
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
 * `keymap_actions_map.at(mode).at(cmd)` に通すので、たとえば遊ぶ人が `@`
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

    /*
     * 逆引き失敗。`command` 自身がキーマップされているなら、そのまま押すと別物になる。
     * そこで原作の「キー配置を飛ばす」`\`（`InputKeyRequestor::process_command_command`）を前に付けて
     * `command` をそのまま出す（2026-10-07）。以前は空（＝押しても何も起きない）を返していて、
     * ローグライクのキー配置に切り替えるとそのコマンドの割り当てが死んでいた。
     */
    if (static_cast<size_t>(command) < actions.size()) {
        if (actions.at(static_cast<size_t>(command))) {
            return { static_cast<uint8_t>('\\'), command };
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
    uint16_t next_id = 0; //!< 次に振る通し番号（0 を振る場面だけ `allocate_entry_id` が別の番号に替える）

    //! 「行動」の分類名（`menu_info[0]` の `cmd == 2`）。下の繰り返しをここへ置く。
    const char *action_group_utf8 = nullptr;

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
        if (sub == 2) {
            action_group_utf8 = group_utf8;
        }

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
            entry.id = allocate_entry_id(next_id);
            entry.command = static_cast<uint8_t>(item.cmd);
            entry.group_utf8 = group_utf8;
            entry.label_utf8 = intern_utf8(item.name.get());
            entries.push_back(entry);
        }
    }

    /*
     * 原作の要素のうち `menu_info` に載っていないものを 1 つだけ足す:
     * 「直前のコマンド繰り返し」（`n`。ローグライク配列では `[`）。
     * 実体は `src/io/command-repeater.cpp` の `repeat_check()`（`command_cmd == 'n'`）で、
     * `lib/help/jcommdesc.txt` の「特別コマンド」に載っている。キーボードなら打てるが、
     * パッドとタッチだけの機体ではコマンドメニューが唯一の手段なので、ここに出す。
     * 押すキーは他の項目と同じく逆引き（`resolve_pad_command_keys`）で決まる。
     * 監査で足した低レベルキーの項目（2026-09-21 に整理）とは別で、原作の命令である。
     */
    if (action_group_utf8 != nullptr) {
        static const auto repeat_label = _L("直前のコマンド繰り返し(n/[)", "Repeat last command(n/[)");
        PadCommandEntry entry{};
        entry.id = allocate_entry_id(next_id);
        entry.command = static_cast<uint8_t>('n');
        entry.group_utf8 = action_group_utf8;
        entry.label_utf8 = intern_utf8(repeat_label.get());
        entries.push_back(entry);

        /*
         * 「繰り返し回数を指定」（`0`。両方の配列で同じ）。実体は
         * `src/io/input-key-requester.cpp` の `process_repeat_num()` で、キーマップより前に
         * 生のキーで見ている。ヘルプの冒頭（`jcommdesc.txt`・`commdesc.txt`）が説明する
         * 「繰り返し回数」（repeat count）そのもの。これも `menu_info` に無い。
         * 回数は数量の欄と同じ枠で入れる（`fill_row0_prompts` と `input_event_adapter.h`）。
         */
        static const auto count_label = _L("繰り返し回数を指定(0)", "Set repeat count(0)");
        PadCommandEntry count_entry{};
        count_entry.id = allocate_entry_id(next_id);
        count_entry.command = static_cast<uint8_t>('0');
        count_entry.group_utf8 = action_group_utf8;
        count_entry.label_utf8 = intern_utf8(count_label.get());
        entries.push_back(count_entry);

        /*
         * 「走る」（`.`。ローグライク配列では `,`）。実体は `do_cmd_run` で、`menu_info` に載っていない
         * （幻想蛮怒・Frox・Sil-Q の表には載っている）。キーボードの Shift＋矢印（修-02）も
         * この行を引いて「走る＋方向」を送るので、ここに無いと変愚・短愚だけ走れない。
         * **並びの最後に足す**——通し番号は cfg の `key_bind=`・`pad_bind=` に書かれるので、
         * 既存の項目の番号を動かさない。押すキーは他の項目と同じく逆引きで決まる。
         */
        static const auto run_label = _L("走る(./,)", "Run(./,)");
        PadCommandEntry run_entry{};
        run_entry.id = allocate_entry_id(next_id);
        run_entry.command = static_cast<uint8_t>('.');
        run_entry.group_utf8 = action_group_utf8;
        run_entry.label_utf8 = intern_utf8(run_label.get());
        entries.push_back(run_entry);
    }

    set_pad_command_table(std::move(entries));
    set_pad_command_resolver(&resolve_pad_command_keys);
}

} // namespace presentation
