/*!
 * @file fc_pad_commands.cpp
 * @brief `fc_pad_commands.h` の実装。**根拠はコアの表**。
 *
 * ## 正は**コアの表**
 * `fc_read_pad_commands()` が `menu_info[10][10]`（`frox/src/util.c:3347`）を
 * 平坦化して 60 件あまりを返す。名札は `special_menu_info` の差し替えを通し、
 * キーは `keymap_act` の逆引きで決める（どちらも `fc_shim.c`）。**表を手で持たない**
 * ——上流は現役なので、先方が項目を増やしたら黙って追従する。
 *
 * Frox の広域マップ移動（設計 §3 の「(j)ourney の世界地図移動など Frox が足した
 * 操作も拾う」）は**この平坦化がそのまま拾う**——`Resume travel(J/())` と
 * `Travel to item(H/^E)` は `menu_info` に載っており、地上での
 * `Enter global map(<)` / `Enter local map(>)` は `special_menu_info` の
 * 差し替えとして `fc_menu_name()` が通す。手で足すものは無い。
 *
 * 下の手書きの表（`kSeeds`）は**落ち先**として残してある。使われるのは
 * 先方が `menu_naiyou` の並びを変えて型の写しが合わなくなったときだけ
 * （`fc_menu_table_sane()` が偽を返す）。上流が現役なので、この落ち先は
 * 幻想蛮怒より現実に踏む見込みが高い——0 件で「パッドから何も押せない」に
 * しないための保険である。
 *
 * ## 例外 — **コアの表に無い 1 命令だけは手で足す**（`kExtras`）
 * `.`（走る。`dungeon.c:3736`）は `menu_info` に載っていない。キーボードなら
 * 打てるが、**パッドとタッチだけの機体では長い通路を進む手立てが無い**。
 * 幻想蛮怒が同じ理由で `.` を足したときそう決めている
 * （`gb_pad_commands.cpp` の頭・2026-08-24）ので、同じ判断をここにも当てる。
 * **キーは手で書かない**——ローグライク配列では走るキーが変わるので、
 * `fc_pad_key_for_command()` に逆引きさせる。
 *
 * ## 文字コード
 * M0 の名札は**コアの英語のまま**（純 ASCII）。変換は無い。M1 で訳の層を通す。
 */

#include "fc_pad_commands.h"

#include "fc_lang_c.h" //!< 名札をカタログで引く（追補 A4）
#include "fc_text.h"   //!< CP932 → UTF-8（設計 §3.1）

#include "fc_shim.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace fc {

namespace {

/*!
 * @brief 名札をカタログで引いて **UTF-8** で返す（追補 A4）。
 * @param ascii 表に書いてある英語の名札。**これがそのまま鍵**である
 * @return 訳文（UTF-8）。訳が無ければ `ascii` のまま
 *
 * @details 鍵はコアの `menu_info[10][10]`（`frox/src/util.c`）の字そのもので、
 * **キーの記号まで含む**（`Rest(R)` が 1 本の鍵）。記号を訳文にも残さないと
 * 「どのキーか」が消えるので、訳は `休憩(R)` の形で書く。
 *
 * `fc_tr()` が返すのは **CP932** なので、線に載せる前に UTF-8 へ直す。
 * **日本語層が寝ているときは 1 バイトも変わらない**——`fc_tr()` が引数を
 * そのまま返し、非 ASCII が無いので変換器も呼ばない（制約 1）。
 *
 * 日本語のリテラルをここへ直に書かないのは、**この TU の焼かれ方が平台で違う**
 * ためである（Windows は CP932、Android は UTF-8）。カタログという 1 本の道を通す。
 */
std::string tr_utf8(const char *ascii)
{
    if ((ascii == nullptr) || (ascii[0] == '\0')) {
        return std::string();
    }
    const char *const translated = fc_tr(ascii);
    if (translated == nullptr) {
        return std::string();
    }
    for (const char *q = translated; *q != '\0'; ++q) {
        if (static_cast<unsigned char>(*q) >= 0x80u) {
            return fc::sjis_to_utf8(translated, std::strlen(translated));
        }
    }
    return std::string(translated); //!< 英語のまま（訳が無いか、日本語層が寝ている）
}


struct PadSeed {
    int command; //!< オリジナル配列のコマンド文字（永続化の鍵）
    const char *group;
    const char *label;
    int key_original;
    int key_rogue; //!< 0 なら「オリジナルと同じ」
};

/*!
 * @brief **落ち先**の最小表。
 * @details 普段は使われない（コアの `menu_info` が正）。使われるのは型の写しが
 * 合わなかったとき——0 件になるとパッドから何も押せなくなるので、最低限を残す。
 * キーの正は `frox/src/dungeon.c` の `process_command()` の `case`。
 */
const PadSeed kSeeds[] = {
    { '8', "Move", "North", '8', 0 },
    { '2', "Move", "South", '2', 0 },
    { '4', "Move", "West", '4', 0 },
    { '6', "Move", "East", '6', 0 },
    { '7', "Move", "North-west", '7', 0 },
    { '9', "Move", "North-east", '9', 0 },
    { '1', "Move", "South-west", '1', 0 },
    { '3', "Move", "South-east", '3', 0 },
    { '<', "Move", "Go up stairs", '<', 0 },
    { '>', "Move", "Go down stairs", '>', 0 },
    { 'g', "Action", "Get items", 'g', 0 },
    { 'm', "Action", "Use magic", 'm', 0 },
    { 'R', "Action", "Rest", 'R', 0 },
    { 's', "Action", "Search", 's', 0 },
    { 'i', "Info", "Inventory", 'i', 0 },
    { 'e', "Info", "Equipment", 'e', 0 },
    { 0x13, "System", "Save", 0x13, 0 },
};

//! `kExtras` 1 件ぶん。**キーは持たない**（`fc_pad_key_for_command()` が逆引きする）。
struct PadExtra {
    int command; //!< Angband のコマンド文字。**永続化の鍵**
    const char *group; //!< **コアの分類名と字面を合わせる**（合わせないと別の束になる）
    const char *label;
};

/*!
 * @brief **コアの `menu_info` に載っていない命令**（見出しの「例外」）。
 * @details 名札の書式はコアに合わせて「名前(キー)」にしてある
 * （`menu_info` は `Rest(R)` のように書く）。
 */
const PadExtra kExtras[] = {
    { '.', "Action", "Run(.)" },
};

/*!
 * @brief コアの `menu_info` から組む。組めなければ空を返す。
 * @details 件数の上限はコアの表の形（分類 9 × 項目 10）から来る 90 で足りるが、
 * 先方が増やしても落ちないように少し余裕を持たせる。
 */
std::vector<presentation::PadCommandWireEntry> entries_from_core()
{
    constexpr int kMax = 128;
    std::vector<fc_pad_command> raw(static_cast<std::size_t>(kMax));
    const int n = fc_read_pad_commands(raw.data(), kMax);

    std::vector<presentation::PadCommandWireEntry> out;
    if (n <= 0) {
        return out;
    }
    if (n >= kMax) {
        std::fprintf(stderr, "[frox] pad_commands: the table is capped at %d entries\n", kMax);
    }

    out.reserve(static_cast<std::size_t>(n));
    int id = 1;
    for (int i = 0; i < n; ++i) {
        const fc_pad_command &src = raw[static_cast<std::size_t>(i)];
        presentation::PadCommandWireEntry wire;
        wire.id = id++;
        wire.command = src.command;
        wire.group_utf8 = tr_utf8(src.group); //!< 追補 A4
        wire.label_utf8 = tr_utf8(src.label); //!< 追補 A4
        /*
         * **キーが 0 の配列には何も積まない。**「そのコマンドはこの配列では出せない」
         * の意味（黙って誤ったキーを積むより無反応が安全）。項目そのものは残す——
         * 一覧から消すと「押せないこと」まで見えなくなる。
         */
        if (src.key_normal != 0) {
            wire.seq_original.push_back(src.key_normal);
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
        wire.group_utf8 = tr_utf8(seed.group); //!< 追補 A4
        wire.label_utf8 = tr_utf8(seed.label); //!< 追補 A4
        wire.seq_original.push_back(seed.key_original);
        wire.seq_rogue.push_back((seed.key_rogue != 0) ? seed.key_rogue : seed.key_original);
        out.push_back(std::move(wire));
    }
    return out;
}

/*!
 * @brief `kExtras` を末尾へ継ぎ足す（見出しの「例外」）。
 * @details `id` は**続きの番号**を振る。`command`（`.`）はコアの表と重ならない
 * （`menu_info` に `.` は無い。2026-08-24 に全数当てた）ので、
 * 画面側が `command` で覚えた割り当ても衝突しない。
 */
void append_extras(std::vector<presentation::PadCommandWireEntry> &out)
{
    int id = static_cast<int>(out.size()) + 1;
    for (const PadExtra &extra : kExtras) {
        presentation::PadCommandWireEntry wire;
        wire.id = id++;
        wire.command = extra.command;
        wire.group_utf8 = tr_utf8(extra.group); //!< 追補 A4
        wire.label_utf8 = tr_utf8(extra.label); //!< 追補 A4

        const unsigned char key_original = fc_pad_key_for_command(0, extra.command);
        const unsigned char key_rogue = fc_pad_key_for_command(1, extra.command);
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
    message.current_keymap = (fc_rogue_like_commands() != 0) ? "rogue" : "original";

    message.entries = entries_from_core();
    if (message.entries.empty()) {
        std::fprintf(stderr,
            "[frox] pad_commands: menu_info could not be read; using the minimal seed table\n");
        message.entries = entries_from_seeds();
    }
    //! **落ち先の表のときも継ぎ足す。** `kExtras` は `menu_info` に依らない。
    append_extras(message.entries);
    return message;
}

} // namespace fc
