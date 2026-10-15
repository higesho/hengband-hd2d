/*!
 * @file an_pad_shim.h
 * @brief `ui-game.c` の `cmds_all[]` を読む純 C ABI（設計 §6.3「コマンド一覧」）。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct an_pad_command {
    const char *group; /*!< `command_list::name`（静的文字列。開放不要） */
    const char *label; /*!< `cmd_info::desc`（同上） */
    int key1; /*!< `cmd_info::key[0]`。0 なら無効な行 */
    int key2; /*!< `cmd_info::key[1]`。0 なら 1 打のみ */
} an_pad_command;

/*!
 * @brief `cmds_all[]` のうち `Items` / `Action commands` / `Manage items` /
 * `Information` / `Utility` の 5 分類だけを集めて `out` へ書く
 * （`Hidden` と `Debug*` は出さない。設計 §6.3）。
 * @return 書いた件数（`cap` を超えたら `cap` で止める）
 */
int an_pad_commands(an_pad_command *out, int cap);

/*!
 * @brief AH-17: `OPT(player, rogue_like_commands)`
 * を読む。
 * @details 以前は「Angband には切替オプションが無い」と誤って決めつけ、
 * `an_main.cpp` の `build_pad_commands()` が `current_keymap` を常に `"original"`
 * にしていた——実際には `list-options.h` に `rogue_like_commands` があり、
 * `ui-*.c` の各所が `OPT(player, rogue_like_commands) ? KEYMAP_MODE_ROGUE :
 * KEYMAP_MODE_ORIG` で切り替えている。画面側（`hd2d/app/run_parts.cpp`）は
 * `current_keymap=="rogue"` のときだけ `entry.seq_rogue`（`cmd_info::key[1]`）を
 * 使うので、常に `"original"` のままだと roguelike keyset を入れた原作に対して
 * 常に `key[0]`（原作既定の並び）を送り、割り当てが違うコマンドが走っていた。
 * @return 1（有効）／0（既定か、`player` がまだ無い）
 */
int an_rogue_like_commands(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
