/*!
 * @file gb_pad_commands.cpp
 * @brief `gb_pad_commands.h` の実装。**根拠はコードと先方の change.txt**。
 *
 * ## いまの正は**コアの表**（M1 / S3。2026-08-20）
 * `gb_read_pad_commands()` が `menu_info[10][10]`（`util.c:4210`）を平坦化して
 * 60 件あまりを返す。名札は `special_menu_info` の差し替えを通し、キーは
 * `keymap_act` の逆引きで決める（どちらも `gb_shim.c`）。**表を手で持たない**
 * ——コアが増やしたコマンドに黙って追従する（必守制約 1 と同じ考え方）。
 *
 * 下の手書きの表（`kSeeds`）は**落ち先**として残してある。使われるのは
 * 先方が `menu_naiyou` の並びを変えて型の写しが合わなくなったときだけ。
 *
 * ## 例外 — **コアの表に無い 2 命令だけは手で足す**（`kExtras`。2026-08-24）
 * 必守制約 1（設計 §1）は「pad 表はコアが申告する」と言っている。**ここはその例外で、
 * そう決めて入れた**。
 *
 * `process_command()`（`dungeon.c:6812`）の `case` 88 個と `menu_info` の 63 件を
 * 突き合わせると、**14 命令がコアの表に載っていない**。
 * キーボードなら打てるので Windows では困らないが、**パッドとタッチだけの機体では
 * 出す手立てが 1 つも無い**。うち 2 件は遊びに直接効くので継ぎ足す:
 *
 * | コマンド | 何か | なぜ足すか |
 * |---|---|---|
 * | `^G`（7） | **結界ガード**（`dungeon.c:7045` → `cmd7.c:7755` `set_mana_shield`） | **幻想蛮怒だけの命令。** `magicmaster` の職の防御手段で、無いと該当職がパッドで守れない |
 * | `.` | **走る**（`dungeon.c:7055` `do_cmd_run`） | 長い通路をパッドで進めない |
 *
 * 残り 12 件（`+` `;` `-` `,` `` ` `` `j` `_` `^I` `^O` `^Q` `]` `^V`）は**足さない**。
 * 代わりが在るか（トラベルはクリック移動、`^I` は機能メニューのサブパネル）、
 * パッドだけの機体で使う場面が薄い。**増やしたくなったら先に §2.5 を読むこと。**
 *
 * **キーは手で書かない。** `gb_pad_key_for_command()` に逆引きさせる——
 * ローグライク配列では「走る」を押すキーが `.` ではなく `,` になる（`pref-key.prf:56`）ので、
 * 手で書くと片方の配列で必ず外す。
 *
 * ## どこから採ったか
 * キーの正は `gensoband/src/dungeon.c` の `process_command()`（:6812〜）の
 * `case` と、`gensoband/lib/pref/pref-key.prf` の keymap 定義である。
 * `lib/help/jcommand.txt` は**本家の古い表のまま**で `j` / `J` が違う（:110）。
 * 信用しないこと。
 *
 * | 動作 | キー | 出どころ |
 * |---|---|---|
 * | 移動 8 方向 | `1`〜`9`（`5` を除く） | `pref-key.prf:18-33`（両配列とも `;<数字>` へ写る） |
 * | 上り階段 | `<` | `dungeon.c:7122` |
 * | 下り階段 | `>` | `dungeon.c:7158` |
 * | 拾う | `g` | `dungeon.c:7069`（`do_cmd_stay(!always_pickup)`） |
 * | 特技 | `J`（ローグライクは `j`） | `dungeon.c:7199` / `:7185-7195`・`change.txt:17` |
 * | 持ち物 | `i` | `dungeon.c:6969` |
 * | 装備 | `e` | `dungeon.c:6962` |
 * | 魔法 | `m` | `dungeon.c:7289` |
 * | 休憩 | `R` | `dungeon.c:7076` |
 * | 探索 | `s` | `dungeon.c:7083` |
 * | セーブ | `Ctrl-S`（0x13） | `dungeon.c:7811` |
 *
 * ## 配列ごとの違いは 1 つだけ
 * 上の表のうち**ローグライク配列で写し替えられるのは `J` だけ**である
 * （`pref-key.prf:129` が `C:1:J` → `A:.2`＝南へ走る、に取られてしまう）。
 * 先方はそれを承知していて、ローグライクでは `j` を「技の実行」に変えている
 * （`dungeon.c:7190-7194`）。ほかの `g` `i` `e` `m` `R` `s` `<` `>` `^S` は
 * どちらの配列でも keymap に現れないので、そのまま通る。
 *
 * ## 文字コード
 * 名札は **UTF-8 で載せる**（v1）。この TU の日本語リテラルは
 * execution-charset が ACP なので **CP932 で焼かれる**（設計 §3.1）。
 * だから `gb::sjis_to_utf8()` を必ず通す。**素で載せると画面側で化ける。**
 */

#include "gb_pad_commands.h"

#include "gb_lang_c.h" //GB: english layer #33 - the pad labels never reach Term, so #1 cannot see them
#include "gb_shim.h"
#include "gb_text.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace gb {

namespace {

struct PadSeed {
    int command; //!< オリジナル配列のコマンド文字（永続化の鍵）
    const char *group_sjis;
    const char *label_sjis;
    int key_original;
    int key_rogue; //!< 0 なら「オリジナルと同じ」
};

/*!
 * @brief **落ち先**の最小表（M0 の手書き）。
 * @details いまは `gb_read_pad_commands()` がコアの `menu_info` を平坦化して
 * 60 件あまりを返すので、普段ここは使われない。使われるのは**型の写しが合わなかった**
 * とき（`gb_menu_table_sane()` が偽＝先方が `menu_naiyou` を変えた）だけである。
 * そのとき 0 件になると**パッドから何も押せなくなる**ので、最低限の移動と行動を残す。
 */
const PadSeed kSeeds[] = {
    { '8', "移動", "北へ", '8', 0 },
    { '2', "移動", "南へ", '2', 0 },
    { '4', "移動", "西へ", '4', 0 },
    { '6', "移動", "東へ", '6', 0 },
    { '7', "移動", "北西へ", '7', 0 },
    { '9', "移動", "北東へ", '9', 0 },
    { '1', "移動", "南西へ", '1', 0 },
    { '3', "移動", "南東へ", '3', 0 },
    { '<', "移動", "上り階段", '<', 0 },
    { '>', "移動", "下り階段", '>', 0 },
    { 'g', "行動", "拾う", 'g', 0 },
    { 'J', "行動", "特技", 'J', 'j' },
    { 'm', "行動", "魔法", 'm', 0 },
    { 'R', "行動", "休憩", 'R', 0 },
    { 's', "行動", "探索", 's', 0 },
    { 'i', "情報", "持ち物", 'i', 0 },
    { 'e', "情報", "装備", 'e', 0 },
    { 0x13, "システム", "セーブ", 0x13, 0 },
};

//! `kExtras` 1 件ぶん。**キーは持たない**（`gb_pad_key_for_command()` が逆引きする）。
struct PadExtra {
    int command; //!< Angband のコマンド文字（`^G` なら 7）。**永続化の鍵**
    const char *group_sjis; //!< **コアの分類名と字面を合わせる**（合わせないと別の束になる）
    const char *label_sjis;
};

/*!
 * @brief **コアの `menu_info` に載っていない命令**（見出しの「例外」）。
 * @details 名札の書式はコアに合わせて「名前(キー)」にしてある
 * （`menu_info` は `穴を掘る(T/^t)` のように書く。`util.c:4216`）。
 */
const PadExtra kExtras[] = {
    { 7, "行動", "結界を張る(^g)" },
    { '.', "行動", "走る(.)" },
};

/*!
 * @brief コアの `menu_info` から組む（S3）。組めなければ空を返す。
 * @details 件数の上限はコアの表の形（分類 9 × 項目 10）から来る 90 で足りるが、
 * 先方が増やしても落ちないように少し余裕を持たせる。**溢れたぶんは黙って捨てない**
 * ——`gb_read_pad_commands()` は cap で打ち切った件数を返すので、下の警告で分かる。
 */
std::vector<presentation::PadCommandWireEntry> entries_from_core()
{
    constexpr int kMax = 128;
    std::vector<gb_pad_command> raw(static_cast<std::size_t>(kMax));
    const int n = gb_read_pad_commands(raw.data(), kMax);

    std::vector<presentation::PadCommandWireEntry> out;
    if (n <= 0) {
        return out;
    }
    if (n >= kMax) {
        std::fprintf(stderr, "[gensoband] pad_commands: 表が %d 件で頭打ち。受け皿を増やすこと\n", kMax);
    }

    out.reserve(static_cast<std::size_t>(n));
    int id = 1;
    for (int i = 0; i < n; ++i) {
        const gb_pad_command &src = raw[static_cast<std::size_t>(i)];
        presentation::PadCommandWireEntry wire;
        wire.id = id++;
        wire.command = src.command;
        wire.group_utf8 = sjis_to_utf8(src.group, std::char_traits<char>::length(src.group));
        wire.label_utf8 = sjis_to_utf8(src.label, std::char_traits<char>::length(src.label));
        /*
         * **キーが 0 の配列には何も積まない。**「そのコマンドはこの配列では出せない」
         * の意味で、変愚の `resolve_pad_command_keys()` が空列を返すのと同じ
         * （黙って誤ったキーを積むより無反応が安全）。項目そのものは残す——
         * 一覧から消すと「押せないこと」まで見えなくなる。
         */
        if (src.key_original != 0) {
            wire.seq_original.push_back(src.key_original);
        }
        if (src.key_rogue != 0) {
            wire.seq_rogue.push_back(src.key_rogue);
        }
        out.push_back(std::move(wire));
    }
    return out;
}

//! 落ち先の最小表から組む（`kSeeds`）。
std::vector<presentation::PadCommandWireEntry> entries_from_seeds()
{
    std::vector<presentation::PadCommandWireEntry> out;
    out.reserve(std::size(kSeeds));
    int id = 1;
    for (const PadSeed &seed : kSeeds) {
        presentation::PadCommandWireEntry wire;
        wire.id = id++;
        wire.command = seed.command;
        wire.group_utf8 = sjis_to_utf8(seed.group_sjis, std::char_traits<char>::length(seed.group_sjis));
        wire.label_utf8 = sjis_to_utf8(seed.label_sjis, std::char_traits<char>::length(seed.label_sjis));
        wire.seq_original.push_back(seed.key_original);
        wire.seq_rogue.push_back((seed.key_rogue != 0) ? seed.key_rogue : seed.key_original);
        out.push_back(std::move(wire));
    }
    return out;
}

/*!
 * @brief `kExtras` を末尾へ継ぎ足す（見出しの「例外」）。
 * @details `id` は**続きの番号**を振る。`command` はコアの表と重ならない
 * （`menu_info` に `7` も `.` も無い。2026-08-24 に全数当てた）ので、
 * 画面側が `command` で覚えた割り当ても衝突しない。
 *
 * **押せない配列では列を積まない。** `gb_pad_key_for_command()` が 0 を返すのは
 * 「その配列ではそのコマンドを出せない」の意味で、`entries_from_core()` と同じ約束である。
 */
void append_extras(std::vector<presentation::PadCommandWireEntry> &out)
{
    int id = static_cast<int>(out.size()) + 1;
    for (const PadExtra &extra : kExtras) {
        presentation::PadCommandWireEntry wire;
        wire.id = id++;
        wire.command = extra.command;
        //! **英語ならカタログで引く**（フック #33。GR-01）。コアの表から採る側は
        //! `gb_read_pad_commands()` が引くが、この 2 件はここが持っている字である。
        const char *group = gb_tr(extra.group_sjis); //GB: english layer #33
        const char *label = gb_tr(extra.label_sjis); //GB: english layer #33
        wire.group_utf8 = sjis_to_utf8(group, std::strlen(group));
        wire.label_utf8 = sjis_to_utf8(label, std::strlen(label));

        const unsigned char key_original = gb_pad_key_for_command(0, extra.command);
        const unsigned char key_rogue = gb_pad_key_for_command(1, extra.command);
        if (key_original != 0) {
            wire.seq_original.push_back(key_original);
        }
        if (key_rogue != 0) {
            wire.seq_rogue.push_back(key_rogue);
        }
        out.push_back(std::move(wire));
    }
}

} // namespace

presentation::PadCommandsMessage build_pad_commands()
{
    presentation::PadCommandsMessage message;
    message.current_keymap = (gb_rogue_like_commands() != 0) ? "rogue" : "original";

    message.entries = entries_from_core();
    if (message.entries.empty()) {
        std::fprintf(stderr,
            "[gensoband] pad_commands: コアの menu_info を読めなかった。手書きの最小表で出す\n");
        message.entries = entries_from_seeds();
    }
    //! **落ち先の表のときも継ぎ足す。** `kExtras` は `menu_info` に依らない（見出しの「例外」）。
    append_extras(message.entries);
    return message;
}

} // namespace gb
