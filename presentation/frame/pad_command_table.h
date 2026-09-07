/*!
 * @file pad_command_table.h
 * @brief パッドへ割り当てられる「ゲーム内コマンド」の表と、キー列の解決口。
 *
 * 設計書。**コア型・SDL 依存なし**。
 * `ui/` が include してよい presentation はここ（`presentation/frame/*`）だけなので、
 * コアの `menu_info` を知っているのは `presentation/bridge/pad_command_table.cpp` だけにする。
 *
 * - 表の実体はこのヘッダ内の関数ローカル静的（TU をまたいで 1 つ）。
 *   bridge が起動時（`init_angband` の後）に一度だけ充填する。
 *   **充填前は空**（レイアウト smoke などはここを通る）。ui は空でも落ちないこと。
 * - `label_utf8` / `group_utf8` は **UTF-8**。コアの文字列は
 *   `/execution-charset:shift-jis` で焼かれた Shift_JIS なので、bridge が
 *   `sys_to_utf8` で変換したうえで安定した領域に持ち、その先を指す
 *   （ここでポインタを持つのは設計書 §3 の型定義に合わせるため）。
 *
 * @note 設計書 §3 の `PadCommandResolverFn` は引数 1 個だったが、
 * 「オリジナル配列とローグライク配列で解決結果が違う」ことを**機械で確かめる**ために
 * 配列を明示指定できる引数を足してある。既定は `Current`（いま有効な配列）なので
 * 設計書どおりの `pad_command_key_sequence(command)` はそのまま使える。
 */
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

/*!
 * @brief 割り当て先として選べるゲーム内コマンド 1 件。
 */
struct PadCommandEntry {
    uint16_t id; //!< 表内で安定な通し番号（UI のカーソル用。永続化には使わない）
    uint8_t command; //!< menu_info の cmd（オリジナル配列の Angband コマンド）。**永続化はこれ**
    const char *group_utf8; //!< 分類名（UTF-8）
    const char *label_utf8; //!< 表示名（UTF-8）
};

/*!
 * @brief キー配列の指定。
 * @details `Current` は「いまゲームで有効な配列」（`rogue_like_commands` を見る）。
 * `Original` / `Rogue` は検査用に明示指定するときだけ使う。
 */
enum class PadKeymapMode : int {
    Current = -1,
    Original = 0,
    Rogue = 1,
};

//! command → 指定配列でそのコマンドを出すキー列。bridge が登録する。
using PadCommandResolverFn = std::vector<uint8_t> (*)(uint8_t command, PadKeymapMode mode);

//! パッド割り当ての自己検査。**実体は登録されていない**（旧 2D UI が登録していた）。
using PadBindSmokeFn = int (*)();

namespace pad_command_table_detail {

struct Store {
    std::vector<PadCommandEntry> entries;
    PadCommandResolverFn resolver{ nullptr };
    PadBindSmokeFn smoke{ nullptr };
};

//! 関数ローカル静的なので、ヘッダを何 TU から include しても実体は 1 つ。
inline Store &store()
{
    static Store s;
    return s;
}

} // namespace pad_command_table_detail

//! コマンド表。**bridge の充填前は空**。
inline const std::vector<PadCommandEntry> &pad_command_table()
{
    return pad_command_table_detail::store().entries;
}

//! 表を充填する（bridge 専用。呼ぶのは起動時の一度だけ）。
inline void set_pad_command_table(std::vector<PadCommandEntry> entries)
{
    pad_command_table_detail::store().entries = std::move(entries);
}

//! キーマップ逆引きの解決関数を登録する（bridge 専用）。
inline void set_pad_command_resolver(PadCommandResolverFn fn)
{
    pad_command_table_detail::store().resolver = fn;
}

inline bool pad_command_resolver_registered()
{
    return pad_command_table_detail::store().resolver != nullptr;
}

/*!
 * @brief command → 指定配列でそれを出すキー列。
 * @details ここで得たバイト列をそのまま `KeyQueue` へ積むと、コア側のキーマップ変換を
 * 通って目的の Angband コマンドになる。
 *
 * @note **解決器が未登録のときは空を返す（K-2）。**
 * 以前は command 1 文字を返していたが、解決器が無い＝`keymap_actions_map` を
 * 誰も見られない状態であり、**その command 自身がキーマップされていないかを
 * 確かめる手段が無い**。確かめられないまま積むと、コア側が
 * `src/io/input-key-requester.cpp:87` で変換して**別のコマンドを実行**しうる。
 * 設計書の「解決器が未登録／表が空のときに黙って誤ったキーを積まない」に合わせ、
 * **無反応（空）を選ぶ**。本線では `register_pad_command_table()` が
 * `run_sdl_game` の `init_angband` 直後に走り、パッド入力を扱う
 * オープニング／本編はすべてその後なので、実プレイでここを通ることはない。
 */
inline std::vector<uint8_t> pad_command_key_sequence(uint8_t command, PadKeymapMode mode = PadKeymapMode::Current)
{
    const auto fn = pad_command_table_detail::store().resolver;
    if (fn == nullptr) {
        return {};
    }
    if (command == 0) {
        return {};
    }
    return fn(command, mode);
}

//! コマンド文字から表の項目を引く。無ければ nullptr（cfg 読込時の照合に使う）。
inline const PadCommandEntry *find_pad_command(uint8_t command)
{
    for (const auto &entry : pad_command_table()) {
        if (entry.command == command) {
            return &entry;
        }
    }
    return nullptr;
}

//! 自己検査の実体を登録する（platform が env を見て ui の実装を渡す）。
inline void set_pad_bind_smoke_hook(PadBindSmokeFn fn)
{
    pad_command_table_detail::store().smoke = fn;
}

//! 登録されていれば実行して終了コードを返す。未登録なら -1（＝検査しない）。
inline int run_pad_bind_smoke_hook()
{
    const auto fn = pad_command_table_detail::store().smoke;
    if (fn == nullptr) {
        return -1;
    }
    return fn();
}
