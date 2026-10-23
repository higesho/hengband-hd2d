// DCSS コア: 画面の部品（dcss-ui）のタブの欄へ送る中身を、ゲームの中から読んで組む。段 U2。
//
// libhd2d.cpp だけが取り込む（原作のヘッダを引く TU。`AppHdr.h` を先に引いてから取り込むこと）。Linux・Android の組み立てでも libhd2d.cpp が
// 組まれるので、ファイルの登録は要らない。原作のソースは書き換えない。
//
// 何をするか（〜§1.5.7・§4.1 U2〜U9）:
//   1. タブの欄の 9 つの中身（持ち物と足元のアイテム・呪文・覚える呪文・能力・怪物・技能・命令 3 種）を、原作のタイル版の区画
//      （`tilereg-inv.cc` ほか。`USE_TILE_LOCAL` の中にしか無く、このコアには組まれない）の `update()` と同じ並び・同じ旗で組み、
//      `{"t":"dcss_ui","k":"tabs",…}` の 1 通の JSON にする。ウェブタイル版の JSON には無い物（U2〜U9）。
//      格子の大きさ（マスの数）で変わる並べ方（空きの枠・足元の部分・ページ送り）は画面の側が同じ規則で決める。
//   2. 画面が送ってきたタブの欄の操作（`{"t":"dcss_action",…}`。左クリック・右クリック・Shift/Ctrl 付き）を、原作の各区画の
//      `handle_mouse` と同じ関数で実行する。実行はゲームのスレッドの入力待ち（`getch_ck`）の中だけ。原作は区画の `handle_mouse` を
//      同じ入力待ちの中で呼んで `CK_MOUSE_CMD` を返すので、それと同じ道を通る。
//
// 呼び出しはすべてゲームのスレッド。受信スレッドは `queue_action` で控えを積むだけ。
#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "ability.h"
#include "coord.h"
#include "tag-version.h"
#include "wiz-you.h"
#include "command.h"
#include "database.h"
#include "describe.h"
#include "directn.h"
#include "env.h"
#include "item-name.h"
#include "item-prop.h"
#include "item-status-flag-type.h"
#include "items.h"
#include "japanese.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "mon-info.h"
#include "nearby-danger.h"
#include "options.h"
#include "player.h"
#include "prompt.h"
#include "religion.h"
#include "skills.h"
#include "spl-book.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "stringutil.h"
#include "tile-env.h"
#include "tile-flags.h"
#include "tile-inventory-flags.h"
#include "tilepick.h"
#include "tiles-build-specific.h"
#include "tileweb.h"
#include "unicode.h"
#include "view.h"
#include "viewgeom.h"
#include "viewmap.h"
#include "rltiles/tiledef-dngn.h"
#include "rltiles/tiledef-icons.h"
#include "rltiles/tiledef-main.h"

#include "dcss_tabs_bridge.h"

namespace dcss_tabs_data {

//! 画面へ送る旗（原作の `TILEI_FLAG_*` と同じ値）。
enum : unsigned {
    kFlagChaotic = TILEI_FLAG_CHAOTIC,
    kFlagEquip = TILEI_FLAG_EQUIP,
    kFlagFloor = TILEI_FLAG_FLOOR,
    kFlagCurse = TILEI_FLAG_CURSE,
    kFlagMelded = TILEI_FLAG_MELDED,
    kFlagInvalid = TILEI_FLAG_INVALID,
};

//! 画面が `button` に入れる値（原作の `wm_mouse_event::LEFT`・`RIGHT` ではなく、ウェブタイル版の `click_cell` の数と同じ: 1 左・3 右）。
constexpr int kLeft = 1;
constexpr int kRight = 3;
//! 画面が `mods` に入れる値（原作の `TILES_MOD_SHIFT`・`TILES_MOD_CTRL` と同じ意味）。
constexpr int kModShift = 1;
constexpr int kModCtrl = 2;

//! 画面が送る操作の控えを入力待ちへ知らせるキー。`wint_t`（Windows は 16 ビット）に収まる私用領域の字。原作の入力には現れない。
constexpr int kActionKey = 0xF8D1;

// ---- JSON を書く小さな道具 ----

struct Out
{
    std::string s;

    void raw(const char *t) { s += t; }
    void raw(const std::string &t) { s += t; }
    void num(long long v) { s += std::to_string(v); }
    void str(const std::string &v)
    {
        s += '"';
        for (const unsigned char c : v)
        {
            switch (c)
            {
            case '"': s += "\\\""; break;
            case '\\': s += "\\\\"; break;
            case '\n': s += "\\n"; break;
            case '\r': s += "\\r"; break;
            case '\t': s += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    s += buf;
                }
                else
                    s += (char)c;
            }
        }
        s += '"';
    }
    void key(const char *k)
    {
        s += '"';
        s += k;
        s += "\":";
    }
    void comma() { s += ','; }
};

//! `[` … `]` の並び 1 つ。要素を書く前に `next()` を呼ぶと、2 つ目からカンマが入る。
class List
{
public:
    explicit List(Out &o) : o_(o) { o_.raw("["); }
    void next()
    {
        if (any_)
            o_.comma();
        any_ = true;
    }
    void close() { o_.raw("]"); }

private:
    Out &o_;
    bool any_ = false;
};

//! `{` … `}` の 1 件。欄を足すたびに、2 つ目からカンマとキーが入る。
class Record
{
public:
    explicit Record(Out &o) : o_(o) { o_.raw("{"); }
    Record &num(const char *k, long long v)
    {
        lead(k);
        o_.num(v);
        return *this;
    }
    Record &text(const char *k, const std::string &v)
    {
        lead(k);
        o_.str(v);
        return *this;
    }
    //! 値を自分で書く欄の、キーまでを書く。
    Out &open(const char *k)
    {
        lead(k);
        return o_;
    }
    void close() { o_.raw("}"); }

private:
    void lead(const char *k)
    {
        if (any_)
            o_.comma();
        any_ = true;
        o_.key(k);
    }
    Out &o_;
    bool any_ = false;
};

// ---- 原作 `tilereg-cmd.h` の固定表（Android の命令は入れない）----

static const command_type kActionCommands[] = {
    CMD_EXPLORE, CMD_REST, CMD_WAIT, CMD_DISPLAY_INVENTORY, CMD_DROP, CMD_CAST_SPELL, CMD_USE_ABILITY,
    CMD_DISPLAY_SKILLS, CMD_MEMORISE_SPELL, CMD_INTERLEVEL_TRAVEL, CMD_SEARCH_STASHES, CMD_LOOKUP_HELP,
};
static const command_type kSystemCommands[] = {
    CMD_REPLAY_MESSAGES, CMD_RESISTS_SCREEN, CMD_DISPLAY_OVERMAP, CMD_DISPLAY_RELIGION, CMD_DISPLAY_MUTATIONS,
    CMD_DISPLAY_SKILLS, CMD_DISPLAY_CHARACTER_STATUS, CMD_DISPLAY_KNOWN_OBJECTS,
    CMD_SAVE_GAME_NOW, CMD_DISPLAY_COMMANDS, CMD_GAME_MENU, CMD_LOOKUP_HELP,
};
static const command_type kMapCommands[] = {
    CMD_DISPLAY_MAP, CMD_MAP_GOTO_TARGET,
    CMD_MAP_NEXT_LEVEL, CMD_MAP_PREV_LEVEL, CMD_MAP_GOTO_LEVEL,
    CMD_MAP_EXCLUDE_AREA, CMD_MAP_FIND_EXCLUDED, CMD_MAP_CLEAR_EXCLUDES,
    CMD_MAP_ADD_WAYPOINT, CMD_MAP_FIND_WAYPOINT,
    CMD_MAP_FIND_UPSTAIR, CMD_MAP_FIND_DOWNSTAIR, CMD_MAP_FIND_YOU, CMD_MAP_FIND_PORTAL, CMD_MAP_FIND_TRAP, CMD_MAP_FIND_ALTAR,
    CMD_MAP_FIND_STASH,
};

//! 技能の上限（これに達した技能は鍛えられない）。
constexpr int kSkillCap = 27;

//! 全体地図の画面（`X`）か。原作のタイル版は `tiles.get_map_display()`。ウェブ版は `ui_state`。
static bool map_display()
{
    return tiles.get_ui_state() == UI_VIEW_MAP;
}

//! 原作 `tile_command_not_applicable(cmd, safe)`（`tilereg-cmd.cc`。ローカル版にしか組まれない）と同じ判定。
static bool command_not_applicable(command_type cmd, bool safe)
{
    // 地図の画面では地図の命令だけが、それ以外の画面では地図以外の命令だけが効く。
    if (map_display() != (context_for_command(cmd) == KMC_LEVELMAP))
        return true;

    if (cmd == CMD_REST)
        return !(safe && can_rest_here());
    if (cmd == CMD_EXPLORE || cmd == CMD_INTERLEVEL_TRAVEL || cmd == CMD_MEMORISE_SPELL)
        return !safe;
    if (cmd == CMD_DISPLAY_RELIGION)
        return you.religion == GOD_NO_GOD;
    if (cmd == CMD_USE_ABILITY)
        return your_talents().size() == 0;
    if (cmd == CMD_CAST_SPELL)
        return !(you.spell_no && can_cast_spells(true));
    return false;
}

//! 原作 `get_command_description`（describe.cc。ローカル版にしか組まれない）と同じ文。
static std::string command_description(command_type cmd, bool terse)
{
    const std::string name = command_to_name(cmd);
    const std::string text = getLongDescription(terse ? name : name + " verbose");
    if (!text.empty())
        return text.substr(0, text.size() - 1); // 末尾の 1 字（改行）を除く
    if (!terse)
    {
        // 詳しい説明が無ければ、短い説明に「。」を付けて返す。
        const std::string brief = command_description(cmd, true);
        if (!brief.empty())
            return brief + "。";
    }
    return name;
}

//! 原作 `has_spells_to_memorise`（spl-book.cc。ローカル版にしか組まれない）。
static bool has_memorisable_spells(bool silent)
{
    return !get_sorted_spell_list(silent, true).empty();
}

// ---- 中身の組み立て ----

//! 持ち物・足元の 1 枠（原作 `_fill_item_info` と同じ欄）。
static void put_item(Out &o, const item_def &item, int idx, unsigned flag, bool on_floor)
{
    unsigned f = flag;
    if (item.cursed())
        f |= kFlagCurse;
    if (item.pos.x != -1)
        f |= kFlagFloor;
    if (item.flags & ISFLAG_CHAOTIC)
        f |= kFlagChaotic;

    const tileidx_t tile = tileidx_item(item);
    const tileidx_t base = tileidx_known_base_item(tile);

    // 数: 積める物は 2 個以上のときだけ個数、鑑定済みの杖(wand)は残りの回数、それ以外は -1。
    const object_class_type cls = item.base_type;
    const bool stackable = is_stackable_item(item);
    const bool charged_wand = cls == OBJ_WANDS && item.is_identified();
    const int quantity = stackable ? (item.quantity == 1 ? -1 : item.quantity) : (charged_wand ? item.charges : -1);

    // 絵の上に重ねる物: ブランド付きの装備はそのブランド、羊皮紙は書かれた呪文の絵 2 枚。
    bool branded_class = cls == OBJ_WEAPONS || cls == OBJ_MISSILES || cls == OBJ_ARMOUR || cls == OBJ_STAVES;
#if TAG_MAJOR_VERSION == 34
    branded_class = branded_class || cls == OBJ_RODS;
#endif
    long long overlay1 = 0, overlay2 = 0;
    if (branded_class)
        overlay1 = tileidx_known_brand(item);
    else if (cls == OBJ_BOOKS && item.sub_type == BOOK_PARCHMENT)
    {
        const auto written = static_cast<spell_type>(item.plus);
        overlay1 = tileidx_parchment_overlay(written, 0);
        overlay2 = tileidx_parchment_overlay(written, 1);
    }

    Record r(o);
    r.num("i", idx).num("t", (long long)tile).num("b", (long long)base).num("q", quantity).num("f", f);
    r.num("s", overlay1).num("s2", overlay2).text("n", on_floor ? item.name(DESC_PLAIN) : item.name(DESC_INVENTORY_EQUIP));
    r.close();
}

//! 持ち物と足元の並べ方を決める「種類の記号」の列（`tile_show_items`）。末尾の 0 は「残り全部」。
static std::vector<char32_t> shown_class_symbols()
{
    const std::string spec = Options.tile_show_items.empty() ? std::string("!?/=([)}:|") : Options.tile_show_items;
    std::vector<char32_t> out;
    const char *p = spec.c_str();
    int used;
    do
    {
        char32_t sym;
        used = utf8towc(&sym, p);
        p += used;
        out.push_back(sym);
    } while (used);
    return out;
}

//! 持ち物の 1 枠につく旗（装備中・封じられている）。
static unsigned equip_flags(int slot)
{
    unsigned flag = 0;
    for (const auto &worn : you.equipment.items)
    {
        if (worn.item != slot)
            continue;
        flag |= kFlagEquip;
        if (worn.melded)
            flag |= kFlagMelded;
        break;
    }
    if (you.inv[slot].base_type == OBJ_TALISMANS && you.active_talisman() == &you.inv[slot])
        flag |= kFlagEquip | (you.form == you.default_form ? 0u : (unsigned)kFlagMelded);
    return flag;
}

//! 持ち物（種類の並びは `tile_show_items`。原作 `InventoryRegion::update` の前半と同じ並び）。
static void put_inventory(Out &o)
{
    std::vector<int> order;
    std::vector<char> placed(ENDOFPACK, 0);
    for (const char32_t sym : shown_class_symbols())
    {
        const bool any_class = sym == 0;
        const object_class_type cls = item_class_by_sym(sym);
        for (int slot = 0; slot < ENDOFPACK; ++slot)
        {
            const item_def &it = you.inv[slot];
            if (placed[slot] || !it.defined() || it.quantity == 0 || (!any_class && it.base_type != cls))
                continue;
            placed[slot] = 1;
            order.push_back(slot);
        }
    }
    o.key("inv");
    List list(o);
    for (const int slot : order)
    {
        list.next();
        put_item(o, you.inv[slot], slot, equip_flags(slot), false);
    }
    list.close();
}

//! 足元のアイテム（原作 `InventoryRegion::update` の後半と同じ並び。何ページ目まで要るかは画面が決めるので全部送る）。
static void put_floor(Out &o)
{
    std::vector<int> pile;
    int at = you.visible_igrd(you.pos());
    while (at != NON_ITEM)
    {
        pile.push_back(at);
        at = env.item[at].link;
    }
    std::vector<char> listed(MAX_ITEMS, 0);
    int count = 0;
    o.key("fl");
    List list(o);
    for (const char32_t sym : shown_class_symbols())
    {
        const bool any_class = sym == 0;
        const object_class_type cls = item_class_by_sym(sym);
        for (const int id : pile)
        {
            if (listed[id] || (!any_class && env.item[id].base_type != cls))
                continue;
            if (count >= 200)
                break;
            listed[id] = 1;
            list.next();
            put_item(o, env.item[id], id, 0, true);
            ++count;
        }
    }
    list.close();
}

static std::string spell_desc_line(spell_type spell)
{
    const std::string failure = failure_rate_to_string(raw_spell_fail(spell));
    return make_stringf(jtransc("%d MP    %s    (%s)"), spell_mana(spell), spell_title_jc(spell), failure.c_str());
}

//! 呪文の欄（原作 `SpellRegion::update` と同じ並び）。最大数（22 と格子の数の小さい方）は画面が当てる。
static void put_spells(Out &o)
{
    o.key("spl");
    List list(o);
    const bool blocked = command_not_applicable(CMD_CAST_SPELL, true);
    for (int i = 0; i < 52; ++i)
    {
        const spell_type spell = get_spell_by_letter(index_to_letter(i));
        if (spell == SPELL_NO_SPELL)
            continue;
        const bool unusable = blocked || spell_is_useless(spell, true, true);
        list.next();
        Record r(o);
        r.num("i", (int)spell).num("t", (long long)tileidx_spell(spell)).num("q", spell_mana(spell));
        r.num("f", unusable ? kFlagInvalid : 0u).text("n", spell_desc_line(spell));
        r.close();
    }
    list.close();
}

//! 覚える呪文の欄（原作 `MemoriseRegion::update` と同じ並び）。
static void put_memorise(Out &o)
{
    o.key("mem");
    List list(o);
    if (has_memorisable_spells(true))
    {
        const std::vector<spell_type> all = get_sorted_spell_list();
        const bool blocked = !can_learn_spell(true) || command_not_applicable(CMD_MEMORISE_SPELL, i_feel_safe(false));
        std::vector<spell_type> shown;
        std::copy_if(all.begin(), all.end(), std::back_inserter(shown),
            [](spell_type sp) { return !you.hidden_spells.get(sp); });
        for (const spell_type spell : shown)
        {
            const bool unusable = blocked || spell_difficulty(spell) > you.experience_level
                || player_spell_levels() < spell_levels_required(spell);
            const unsigned f = (unusable ? kFlagInvalid : 0u) | (vehumet_is_offering(spell, true) ? kFlagEquip : 0u);
            const std::string failure = failure_rate_to_string(raw_spell_fail(spell));
            const std::string line = make_stringf(jtransc("%s    (%s)    %d/%d spell slots"), spell_title_jc(spell), failure.c_str(),
                spell_levels_required(spell), player_spell_levels());
            list.next();
            Record r(o);
            r.num("i", (int)spell).num("t", (long long)tileidx_spell(spell)).num("q", spell_difficulty(spell));
            r.num("f", f).text("n", line);
            r.close();
        }
    }
    list.close();
}

//! 能力の欄（原作 `AbilityRegion::update` と同じ並び。固有の能力を先に、祈りを後に並べ、最大数は画面が当てる）。
static void put_abilities(Out &o)
{
    o.key("abl");
    List list(o);
    const std::vector<talent> talents = your_talents(true);
    std::vector<talent> ordered;
    for (const auto &t : talents)
        if (!t.is_invocation)
            ordered.push_back(t);
    for (const auto &t : talents)
        if (t.is_invocation)
            ordered.push_back(t);
    const bool blocked = command_not_applicable(CMD_USE_ABILITY, true);
    for (const auto &t : ordered)
    {
        const ability_type ability = t.which;
        const bool unusable = blocked || !check_ability_possible(ability, true);
        const std::string failure = failure_rate_to_string(get_talent(ability).fail);
        const std::string line = make_stringf("%s    (%s)", ability_name_j(ability).c_str(), failure.c_str());
        list.next();
        Record r(o);
        r.num("i", (int)ability).num("t", (long long)tileidx_ability(ability)).num("q", (int)ability_mp_cost(ability));
        r.num("f", unusable ? kFlagInvalid : 0u).text("n", line);
        r.close();
    }
    list.close();
}

//! 技能の欄（原作 `SkillRegion::update`・`draw_tag` と同じ内容）。
static void put_skills(Out &o)
{
    o.key("skl");
    List list(o);
    for (int n = 0; n < NUM_SKILLS; ++n)
    {
        const auto sk = static_cast<skill_type>(n);
        if (is_useless_skill(sk))
            continue;
        const bool capped = you.skills[sk] >= kSkillCap;
        // 絵の枠: 上限なら「極めた」、鍛えていなければ「休止」、それ以外は今の鍛え方。
        const int mode = capped ? (int)TRAINING_MASTERED : (!you.training[sk] ? (int)TRAINING_INACTIVE : (int)you.train[sk]);
        const bool unusable = capped || !you.can_currently_train[sk];
        const int aptitude = species_apt(sk, you.species);
        const std::string line = make_stringf(jtransc("%s Skill %4.1f Aptitude %c%d"), chop_string(skill_name_j(sk), 14).c_str(),
            you.skill(sk, 10) / 10.0, aptitude > 0 ? '+' : ' ', aptitude);
        list.next();
        Record r(o);
        r.num("i", n).num("t", (long long)tileidx_skill(sk, mode)).num("q", (int)you.skills[sk]).num("a", aptitude);
        r.num("f", unusable ? kFlagInvalid : 0u).text("n", line);
        r.close();
    }
    list.close();
}

//! 命令の欄 1 つ（原作 `CommandRegion::update` と同じ並び）。
static void put_commands(Out &o, const command_type *cmds, int n, bool safe)
{
    List list(o);
    for (int i = 0; i < n; ++i)
    {
        // 地図を開く命令は、すでに地図の画面なら「地図を閉じる」に差し替える。
        const command_type cmd = (cmds[i] == CMD_DISPLAY_MAP && map_display()) ? CMD_MAP_EXIT_MAP : cmds[i];
        list.next();
        Record r(o);
        r.num("i", (int)cmd).num("t", (long long)tileidx_command(cmd)).num("f", command_not_applicable(cmd, safe) ? kFlagInvalid : 0u);
        r.text("n", command_description(cmd, true));
        r.close();
    }
    list.close();
}

//! 態度の輪（味方・中立）と脅威の枠の、前景の旗から選ぶ絵。
struct FlagTile
{
    tileidx_t flag;
    long long tile;
};
struct ThreatTile
{
    tileidx_t flag;
    long long plain;
    long long ghost;
};
static const FlagTile kAttitudeHalos[] = {
    {TILE_FLAG_PET, TILE_HALO_FRIENDLY},
    {TILE_FLAG_GD_NEUTRAL, TILE_HALO_GD_NEUTRAL},
    {TILE_FLAG_NEUTRAL, TILE_HALO_NEUTRAL},
};
static const ThreatTile kThreatFrames[] = {
    {TILE_FLAG_TRIVIAL, TILE_THREAT_TRIVIAL, TILE_THREAT_GHOST_TRIVIAL},
    {TILE_FLAG_EASY, TILE_THREAT_EASY, TILE_THREAT_GHOST_EASY},
    {TILE_FLAG_TOUGH, TILE_THREAT_TOUGH, TILE_THREAT_GHOST_TOUGH},
    {TILE_FLAG_NASTY, TILE_THREAT_NASTY, TILE_THREAT_GHOST_NASTY},
    {TILE_FLAG_UNUSUAL, TILE_THREAT_UNUSUAL, TILE_THREAT_UNUSUAL},
};

//! 怪物の欄の 1 件の背景の絵の列（原作 `pack_background`）: 透ける地形の下の床・地形そのもの・態度の輪・脅威の枠。
static std::vector<long long> monster_backdrop(const coord_def &gc)
{
    const tileidx_t fg = tile_env.bk_fg(gc);
    const tileidx_t terrain = tile_env.bk_bg(gc) & TILE_FLAG_MASK;
    std::vector<long long> under;
    if (terrain >= (tileidx_t)TILE_DNGN_FIRST_TRANSPARENT)
        under.push_back(tile_env.flv(gc).floor);
    under.push_back((long long)terrain);
    if (!fg)
        return under;
    for (const FlagTile &h : kAttitudeHalos)
    {
        if ((fg & TILE_FLAG_ATT_MASK) == h.flag)
        {
            under.push_back(h.tile);
            break;
        }
    }
    const bool ghost = (fg & TILE_FLAG_GHOST) != 0;
    for (const ThreatTile &t : kThreatFrames)
    {
        if ((fg & TILE_FLAG_THREAT_MASK) == t.flag)
        {
            under.push_back(ghost ? t.ghost : t.plain);
            break;
        }
    }
    return under;
}

//! 怪物の欄（原作 `MonsterRegion::update`・`pack_buffers` と同じ並び）。マスの絵の層は、地図の JSON を受けて覚えている前景（dcss_frame.cpp）から取る。
static void put_monsters(Out &o)
{
    o.key("mon");
    List list(o);
    std::vector<monster_info> infos;
    get_monster_info(infos);
    std::sort(infos.begin(), infos.end(), monster_info::less_than_wrapper);
    int count = 0;
    for (const monster_info &mi : infos)
    {
        if (count++ >= 160)
            break;
        const coord_def gc = mi.pos;
        int rx = 0, ry = 0;
        std::vector<std::array<int, 4>> layers;
        const bool known = dcss_tabs_bridge::entity_layers_at(gc.x - you.pos().x, gc.y - you.pos().y, layers, rx, ry);
        const bool in_los = crawl_view.in_los_bounds_g(gc);
        list.next();
        Record r(o);
        r.text("n", mi.proper_name(DESC_A)).num("x", rx).num("y", ry).num("los", (in_los && known) ? 1 : 0);
        if (in_los)
        {
            r.open("u");
            List under(o);
            for (const long long t : monster_backdrop(gc))
            {
                under.next();
                o.num(t);
            }
            under.close();
            r.open("lay");
            List lay(o);
            for (const auto &layer : layers)
            {
                lay.next();
                List quad(o);
                for (const int v : layer)
                {
                    quad.next();
                    o.num(v);
                }
                quad.close();
            }
            lay.close();
            r.open("ic");
            List icons(o);
            const auto found = tile_env.icons.find(gc);
            if (found != tile_env.icons.end())
            {
                for (const tileidx_t t : found->second)
                {
                    icons.next();
                    o.num((long long)t);
                }
            }
            icons.close();
        }
        r.close();
    }
    list.close();
}

/*!
 * @brief タブの欄の中身を 1 通の JSON にする。キャラクターがまだ決まっていなければ空。
 */
static std::string build_json()
{
    if (you.species == SP_UNKNOWN || you.char_class == JOB_UNKNOWN || crawl_state.game_is_arena() || !crawl_state.need_save || !in_bounds(you.pos()))
        return std::string();
    Out o;
    o.raw("{\"t\":\"dcss_ui\",\"k\":\"tabs\",\"map\":");
    o.num(map_display() ? 1 : 0);
    o.raw(",\"floor_tile\":");
    o.num((long long)tile_env.default_flavour.floor);
    o.raw(",\"floor_n\":");
    o.num((long long)tile_dngn_count(tile_env.default_flavour.floor));
    // 欄は決まった順に、前にカンマを置いて足す。
    void (*const sections[])(Out &) = {put_inventory, put_floor, put_spells, put_memorise, put_abilities, put_monsters, put_skills};
    for (auto section : sections)
    {
        o.comma();
        section(o);
    }
    const bool calm = i_feel_safe(false);
    o.raw(",\"cmd\":[");
    put_commands(o, kActionCommands, (int)ARRAYSZ(kActionCommands), calm);
    o.comma();
    put_commands(o, kSystemCommands, (int)ARRAYSZ(kSystemCommands), calm);
    o.comma();
    put_commands(o, kMapCommands, (int)ARRAYSZ(kMapCommands), calm);
    o.raw("]}");
    return o.s;
}

// ---- 画面からの操作 ----

static std::mutex g_action_mutex;
static std::vector<std::string> g_actions;

//! 受信スレッドから。操作の JSON を積む。実行はゲームのスレッドの入力待ちで `run_pending_actions`。
static void queue_action(const std::string &json)
{
    std::lock_guard<std::mutex> lock(g_action_mutex);
    if (g_actions.size() < 32)
        g_actions.push_back(json);
}

static bool json_int(const std::string &j, const char *key, int &out)
{
    const std::string pat = std::string("\"") + key + "\":";
    const std::size_t at = j.find(pat);
    if (at == std::string::npos)
        return false;
    out = std::atoi(j.c_str() + at + pat.size());
    return true;
}

static std::string json_str(const std::string &j, const char *key)
{
    const std::string pat = std::string("\"") + key + "\":\"";
    const std::size_t at = j.find(pat);
    if (at == std::string::npos)
        return std::string();
    const std::size_t from = at + pat.size();
    const std::size_t to = j.find('"', from);
    return to == std::string::npos ? std::string() : j.substr(from, to - from);
}

//! 説明の窓から戻ったあと、画面を描き直させる。
static void after_describe()
{
    redraw_screen();
    update_screen();
}

//! 命令を待っている場面か（区画に入った操作が効くのはこの間か、全体地図の画面）。
static bool command_mode()
{
    return mouse_control::current_mode() == MOUSE_MODE_COMMAND;
}

//! 失敗や取り消しのあと、先に溜まっていた入力を捨てる。
static void abandon_typeahead()
{
    flush_input_buffer(FLUSH_ON_FAILURE);
}

//! 空のときに言う決まり文句を出し、メッセージ行を確定させる。
static void say_canned(canned_message_type which)
{
    canned_msg(which);
    flush_prev_message();
}

//! タブを開いたとき（原作 `GridRegion::activate`）: 何も持っていなければそう言う。真なら処理した。
static bool tab_opened(int idx)
{
    switch (idx)
    {
    case 0:
    {
        const bool nothing_held = inv_count() < 1 && you.num_turns > 0;
        if (nothing_held)
            say_canned(MSG_NOTHING_CARRIED);
        return true;
    }
    case 1:
        if (!you.spell_no)
            say_canned(MSG_NO_SPELLS);
        return true;
    case 2:
        has_memorisable_spells(false);
        return true;
    case 3:
        if (your_talents(true).empty())
        {
            no_ability_msg();
            flush_prev_message();
        }
        return true;
    default:
        return false;
    }
}

//! 持ち物・足元のクリック（原作 `InventoryRegion::handle_mouse`）。
static bool item_click(bool on_floor, int idx, int button, bool shift, bool ctrl)
{
    item_def *slot = nullptr;
    if (on_floor && idx >= 0 && idx < MAX_ITEMS)
        slot = &env.item[idx];
    else if (!on_floor && idx >= 0 && idx < ENDOFPACK)
        slot = &you.inv[idx];
    if (!slot || !slot->defined())
        return false;
    if (button == kLeft)
    {
        if (on_floor)
            tile_item_pickup(idx, ctrl);
        else if (shift)
            tile_item_drop(idx, ctrl);
        else
            tile_item_use(idx);
        return true;
    }
    if (button != kRight)
        return false;
    describe_item(*slot);
    after_describe();
    return true;
}

//! 呪文の欄のクリック（原作 `SpellRegion::handle_mouse`）。
static bool spell_click(int idx, int button)
{
    if (!command_mode() || command_not_applicable(CMD_CAST_SPELL, true))
        return false;
    const auto spell = static_cast<spell_type>(idx);
    if (button == kLeft)
    {
        const bool aborted = cast_a_spell(false, spell) == spret::abort;
        if (aborted)
            abandon_typeahead();
        return true;
    }
    if (button != kRight || spell == NUM_SPELLS)
        return false;
    describe_spell(spell);
    after_describe();
    return true;
}

//! 覚える呪文の欄のクリック（原作 `MemoriseRegion::handle_mouse`）。
static bool memorise_click(int idx, int button)
{
    if (!command_mode() || command_not_applicable(CMD_MEMORISE_SPELL, i_feel_safe(false)))
        return false;
    const auto spell = static_cast<spell_type>(idx);
    if (button == kLeft)
    {
        if (!learn_spell(spell))
            abandon_typeahead();
        return true;
    }
    if (button != kRight)
        return false;
    describe_spell(spell);
    after_describe();
    return true;
}

//! 能力の欄のクリック（原作 `AbilityRegion::handle_mouse`）。
static bool ability_click(int idx, int button)
{
    if (!command_mode() || command_not_applicable(CMD_USE_ABILITY, true))
        return false;
    const auto ability = static_cast<ability_type>(idx);
    if (button == kLeft)
    {
        talent chosen = get_talent(ability);
        if (chosen.which == ABIL_NON_ABILITY || !activate_talent(chosen))
            abandon_typeahead();
        return true;
    }
    if (button != kRight || ability == NUM_ABILITIES)
        return false;
    describe_ability(ability);
    after_describe();
    return true;
}

//! 技能の鍛え方を切り替える（左クリック）。鍛えられない理由があれば、その文を出して何もしない。
static void toggle_training(skill_type skill)
{
    const char *refusal = !you.can_currently_train[skill] ? "You cannot train this skill."
        : you.has_mutation(MUT_DISTRIBUTED_TRAINING)      ? "You can't change your training allocations!"
        : you.skills[skill] >= kSkillCap                  ? "There's no point to toggling this skill anymore."
                                                          : nullptr;
    if (refusal)
    {
        mpr(jtrans(refusal));
        return;
    }
    // 鍛え方が「鍛える／鍛えない」の 2 値の設定なら反転、そうでなければ次の段へ回す。
    const int current = you.train[skill];
    const bool two_state = Options.skill_focus == SKM_FOCUS_OFF;
    set_training_status(skill, static_cast<training_status>(two_state ? !current : (current + 1) % NUM_TRAINING_STATUSES));
    reset_training();
}

//! 技能の欄のクリック（原作 `SkillRegion::handle_mouse`）。
static bool skill_click(int idx, int button, bool ctrl)
{
    if (idx < 0 || idx >= NUM_SKILLS)
        return false;
    const auto skill = static_cast<skill_type>(idx);
    if (button == kLeft)
    {
#ifdef WIZARD
        if (you.wizard && ctrl)
        {
            wizard_set_skill_level(skill);
            return true;
        }
#endif
        toggle_training(skill);
        return true;
    }
    if (button != kRight)
        return false;
    describe_skill(skill);
    after_describe();
    return true;
}

/*!
 * @brief 操作 1 つを原作の各区画の `handle_mouse` と同じ関数で実行する。
 * @return 実行した（入力待ちは `CK_MOUSE_CMD` を返してよい）か。場面に合わないときは偽（捨てる）。
 */
static bool run_action(const std::string &json)
{
    const std::string kind = json_str(json, "a");
    int idx = -1, button = 0, mods = 0;
    json_int(json, "i", idx);
    json_int(json, "b", button);
    json_int(json, "m", mods);
    const bool map_mode = map_display();
    const bool shift = (mods & kModShift) != 0;
    const bool ctrl = (mods & kModCtrl) != 0;
    // 全体地図の画面では、命令を待っていなくても効く区画がある。
    const bool usable_on_map = command_mode() || map_mode;

    if (kind == "act")
        return usable_on_map && tab_opened(idx);
    if (kind == "inv" || kind == "floor")
        return command_mode() && !map_mode && item_click(kind == "floor", idx, button, shift, ctrl);
    if (kind == "spl")
        return spell_click(idx, button);
    if (kind == "mem")
        return memorise_click(idx, button);
    if (kind == "abl")
        return ability_click(idx, button);
    if (kind == "skl")
        return usable_on_map && skill_click(idx, button, ctrl);
    if (kind == "cmd")
    {
        if (!usable_on_map || button != kLeft)
            return false;
        const auto cmd = static_cast<command_type>(idx);
        // 全体地図の画面の命令は、キー入力を待つ別の経路(viewmap)で動くので、専用の入口へ渡す。
        if (map_mode)
            process_map_command(cmd);
        else
            process_command(cmd);
        return true;
    }
    return false;
}

//! 控えた操作を 1 つ取り出して実行する。実行したら真（呼び出し側は `CK_MOUSE_CMD` を返す）。
static bool run_pending_action()
{
    std::string json;
    {
        std::lock_guard<std::mutex> lock(g_action_mutex);
        if (g_actions.empty())
            return false;
        json = g_actions.front();
        g_actions.erase(g_actions.begin());
    }
    return run_action(json);
}

} // namespace dcss_tabs_data
