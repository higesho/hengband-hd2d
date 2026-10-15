/*!
 * @file an_pad_shim.c
 * @brief `an_pad_shim.h` の実体。
 *
 * 「コマンド一覧」。
 * `cmds_all[]`（`ui-game.c:329-350`）は末尾が `{ NULL, NULL, 0, 0, false }` の
 * 番人つきなので、`N_ELEMENTS()` を使わず名前が尽きるまで読む
 * （`extern struct command_list cmds_all[];` は不完全型で、この TU から
 * `sizeof` できない）。
 */

#include "angband.h"

#include "ui-input.h"

#include "an_pad_shim.h"

#include <string.h>

/*! 設計 §6.3 のとおり、この 5 分類だけを出す（`Hidden` と `Debug*` は出さない）。 */
static const char *const kWantedGroups[] = {
    "Items", "Action commands", "Manage items", "Information", "Utility",
};

/*!
 * @brief AH-18: `cmd_hidden[]`（`ui-game.c`）の
 * うち、コマンド一覧に出してよい 7 個だけの許可票（AH-43 の 3 個は `kHiddenLateList`）。
 * @details `Hidden` 分類は本来デバッグ・ウィザードモード・pref 読み込みなど
 * **本当に隠す物**を含む（`Version info`・`Load a single pref line`・
 * `Toggle windows`・`Alter a grid`・`Stand still`・`Center map`・
 * `Toggle wizard mode`・`Debug mode commands`・`Borg commands`）ので、分類ごと
 * 出すのではなく、遊ぶ人が日常使う 7 個（盗む・走る・歩く・繰り返す・探索・
 * 自動拾い・メモ）だけを `desc` の文字列で名指しして通す。
 */
static const char *const kHiddenAllowList[] = {
    "Take notes",
    "Steal from a monster",
    "Walk",
    "Start running",
    "Start exploring",
    "Repeat previous command",
    "Do autopickup",
};

/*!
 * @brief AH-43: 原作の要素は極力出す方針で足した 3 個（`"`・`+`・`,`）。
 * @details 画面側はキー割り当て（`pad_bind=`）を**一覧の通し番号（id）**で
 * 保存する。`cmd_hidden[]` の並びのまま混ぜると既存の 7 個の id がずれて
 * 遊ぶ人の割り当てが別のコマンドを指してしまうので、この 3 個は一覧の
 * **末尾**へ別に足す（`an_pad_commands()` の最後）。
 * `Toggle windows`（^E。`toggle_inven_equip()` は `angband_term[]` の
 * サブウィンドウの持ち物と装備を入れ替えるだけで、HD2D は term 0 しか
 * 立てない）と `Center map`（^L/@。`center_panel()` は `Term->offset_*`
 * を動かすだけで、HD2D のカメラは `L` の最中以外は常に `@` を追う。
 * `an_frame.cpp`）は画面で何も起きないので出さない。
 */
static const char *const kHiddenLateList[] = {
    "Load a single pref line",
    "Alter a grid",
    "Stand still",
};

static int an_hidden_command_allowed(const char *desc)
{
    size_t i;
    if (!desc) {
        return 0;
    }
    for (i = 0; i < (sizeof(kHiddenAllowList) / sizeof(kHiddenAllowList[0])); i++) {
        if (strcmp(desc, kHiddenAllowList[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int an_pad_commands(an_pad_command *out, int cap)
{
    int written = 0;
    int list_index;

    if (!out || (cap <= 0)) {
        return 0;
    }

    const struct command_list *hidden_group = NULL;

    for (list_index = 0; cmds_all[list_index].name != NULL; list_index++) {
        const struct command_list *group = &cmds_all[list_index];
        int wanted = 0;
        int is_hidden;
        size_t w;
        size_t entry;

        for (w = 0; w < (sizeof(kWantedGroups) / sizeof(kWantedGroups[0])); w++) {
            if (strcmp(group->name, kWantedGroups[w]) == 0) {
                wanted = 1;
                break;
            }
        }
        is_hidden = (strcmp(group->name, "Hidden") == 0);
        if (is_hidden) {
            hidden_group = group;
        }
        if (!wanted && !is_hidden) {
            continue;
        }

        for (entry = 0; entry < group->len; entry++) {
            const struct cmd_info *info = &group->list[entry];
            if (written >= cap) {
                return written;
            }
            if (!info->desc || (info->key[0] == 0)) {
                continue; /* 名前も鍵も無い行は出さない。 */
            }
            if (is_hidden && !an_hidden_command_allowed(info->desc)) {
                continue; /* 許可票に無い Hidden の行（デバッグ・ウィザードほか）は出さない。 */
            }
            out[written].group = group->name;
            out[written].label = info->desc;
            out[written].key1 = (int)info->key[0];
            out[written].key2 = (int)info->key[1];
            written++;
        }
    }

    /* AH-43: 既存の id を動かさないよう、足した 3 個は末尾へ（`kHiddenLateList` の順）。 */
    if (hidden_group) {
        size_t late;
        for (late = 0; late < (sizeof(kHiddenLateList) / sizeof(kHiddenLateList[0])); late++) {
            size_t entry;
            for (entry = 0; entry < hidden_group->len; entry++) {
                const struct cmd_info *info = &hidden_group->list[entry];
                if (!info->desc || (info->key[0] == 0) || (strcmp(info->desc, kHiddenLateList[late]) != 0)) {
                    continue;
                }
                if (written >= cap) {
                    return written;
                }
                out[written].group = hidden_group->name;
                out[written].label = info->desc;
                out[written].key1 = (int)info->key[0];
                out[written].key2 = (int)info->key[1];
                written++;
                break;
            }
        }
    }

    return written;
}

int an_rogue_like_commands(void)
{
    return (player && OPT(player, rogue_like_commands)) ? 1 : 0;
}
