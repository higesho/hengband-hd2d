/*!
 * @file gb_shim.c
 * @brief 境界の実装のうち「コアの名札」と「ゲーム状態の読み口」を持つ TU（設計 §3）。
 *
 * ここは**幻想蛮怒のヘッダを include してよい場所**である（`gb_null_term.c` /
 * `gb_bootstrap.c` も同族）。C++ からは `gb_shim.h` 越しにしか見えない。
 *
 * ## 文字コード（adapter/ 全体の約束）
 * このファイルは **UTF-8（BOM 付き）**。`gensoband/src/` の写しは **SJIS** のまま。
 * vcxproj は `/source-charset` を足していないので、MSVC は既定で ACP（932）として読み、
 * **BOM のあるファイルだけ UTF-8 として読む**。だから 2 つが同居できる。
 * - `gensoband/src/` に charset 指定を足してはいけない（写しが丸ごと化ける）。
 * - `gensoband/adapter/` から BOM を落としてはいけない（コメントが化ける）。
 *
 * 実行時文字コード（execution-charset）は指定していないので **ACP = CP932**。
 * つまり adapter に日本語のリテラルを書くと SJIS で焼かれ、コアの内部コードと揃う。
 * **この TU が返す文字列はすべて SJIS のバイト列**であり、UTF-8 化は C++ 側
 * （`gb_text.h`）の仕事である。v1 は UTF-8 で喋る（設計 §3.1）。
 */

#include "angband.h"

#include "gb_lang_c.h"
#include "gb_shim.h"

#include <stdio.h>
#include <string.h>

/*
 * 版は defines.h:80-83 の H_VER_* から組む。
 *
 *   #define H_VER_MAJOR 2
 *   #define H_VER_MINOR 1
 *   #define H_VER_PATCH 6
 *   #define H_VER_EXTRA 0
 *
 * H_VER_EXTRA は先方が 0 のまま使っていないので、申告には入れない
 * （入れると "2.1.6.0" になり、先方の版表記 "v2.1.6" とずれる）。
 * 数字の直書きはしない──写しを取り込み直したら自動で追随させるため。
 */
#define GB_STRINGIFY_(x) #x
#define GB_STRINGIFY(x) GB_STRINGIFY_(x)
#define GB_VERSION_STRING \
    GB_STRINGIFY(H_VER_MAJOR) "." GB_STRINGIFY(H_VER_MINOR) "." GB_STRINGIFY(H_VER_PATCH)

const char *gb_core_name(void)
{
    return "gensoband";
}

const char *gb_core_version(void)
{
    return GB_VERSION_STRING;
}

/* ==================================================================== 小さな道具 */

static void gb_copy_str(char *dst, int cap, cptr src)
{
    int n;

    if (!dst || (cap <= 0)) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    n = (int)strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

/* ================================================================== HUD の読み口 */

int gb_character_generated(void)
{
    return character_generated ? 1 : 0;
}

int gb_rogue_like_commands(void)
{
    return rogue_like_commands ? 1 : 0;
}

/* ================================== UI 既定パネルの中身（M1 / S1 の対） */

int gb_read_item_line(int what, int index, char *out, int cap)
{
    /* 装備の枠名。変愚の `fill_sub2_equipment()` と同じ 2 文字。
       並びは `defines.h:962` の INVEN_RARM から。**幻想蛮怒は弓枠が無く
       リボン枠がある**ので、そこだけ変愚と違う（RI）。 */
    static const char *kSlot[] = {
        "MH", "SH", "RI", "MR", "SR", "NK", "LT", "BD", "OT", "HD", "AR", "FT"
    };
    int from;
    int to;
    int i;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return GB_ERR_ARG;
    }
    out[0] = 0;
    if (!character_generated) {
        return 0;
    }

    if (what == GB_ITEMS_EQUIPMENT) {
        from = INVEN_RARM;
        to = INVEN_TOTAL;
    } else {
        from = 0;
        to = INVEN_PACK;
    }

    for (i = from; i < to; i++) {
        object_type *o_ptr = &inventory[i];
        char name[MAX_NLEN];
        char line[MAX_NLEN + 8];

        if (!o_ptr->k_idx) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        object_desc(name, o_ptr, OD_NAME_ONLY | OD_OMIT_PREFIX);
        if (what == GB_ITEMS_EQUIPMENT) {
            const int slot = i - INVEN_RARM;
            sprintf(line, "%s %s", (slot >= 0 && slot < 12) ? kSlot[slot] : "??", name);
        } else {
            sprintf(line, "%c) %s", (char)(0x61 + i), name);
        }
        gb_copy_str(out, cap, line);
        return (int)strlen(out);
    }
    return 0;
}

int gb_read_visible_monster(int index, char *out, int cap)
{
    int i;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return GB_ERR_ARG;
    }
    out[0] = 0;
    if (!character_generated || !character_dungeon) {
        return 0;
    }

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = &m_list[i];
        char name[MAX_NLEN];

        if (!m_ptr->r_idx) {
            continue;
        }
        /* **視認しているものだけ。**見えていない敵を名前で漏らさない。 */
        if (!m_ptr->ml) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        monster_desc(name, m_ptr, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE);
        gb_copy_str(out, cap, name);
        return (int)strlen(out);
    }
    return 0;
}

/* ================================================== 文字入力（M1 / S5） */

/*! `askfor_aux()` の待ちの間だけ 1（`gb_shim.h` の `gb_text_input_set`）。 */
static int gb_text_input_flag = 0;

void gb_text_input_set(int active)
{
    gb_text_input_flag = active ? 1 : 0;
}

int gb_text_input_active(void)
{
    return gb_text_input_flag;
}

/* ============================================ 戦闘の見せ場（M1 / S10・R4） */

/*! 溜め場。ゲームスレッドしか触らない（capture も同じスレッド）ので鍵は要らない。 */
static gb_fx_event gb_fx_ring[GB_FX_MAX];
static int gb_fx_num = 0;
/*! 宣言中の属性（`gb_fx_set_pending`）。-1 は「宣言なし＝物理」。 */
static int gb_fx_pending = -1;

/*! 1 件積む。**あふれたら古いほうを捨てる**（新しい出来事のほうが見たい）。 */
static void gb_fx_push(const gb_fx_event *ev)
{
    int i;

    if (gb_fx_num >= GB_FX_MAX) {
        for (i = 1; i < GB_FX_MAX; i++) {
            gb_fx_ring[i - 1] = gb_fx_ring[i];
        }
        gb_fx_num = GB_FX_MAX - 1;
    }
    gb_fx_ring[gb_fx_num++] = *ev;
}

void gb_fx_set_pending(int typ)
{
    gb_fx_pending = typ;
}

void gb_fx_player_hit(int damage, int mhp, int melee)
{
    gb_fx_event ev;

    (void)melee; /* いまの器は近接かどうかを持たない（変愚も色は属性だけで決める） */
    if (damage <= 0) {
        gb_fx_pending = -1;
        return;
    }
    ev.kind = 0;
    ev.typ = gb_fx_pending;
    ev.y = py;
    ev.x = px;
    ev.src_y = py;
    ev.src_x = px;
    ev.num = damage;
    ev.den = (mhp > 0) ? mhp : damage;
    gb_fx_push(&ev);

    /* 宣言は 1 回きり。次の被弾へ持ち越さない。 */
    gb_fx_pending = -1;
}

void gb_fx_monster_hit(int y, int x, int dam, int max_hp)
{
    gb_fx_event ev;

    if (dam <= 0) {
        return;
    }
    ev.kind = 1;
    ev.typ = gb_fx_pending;
    ev.y = y;
    ev.x = x;
    ev.src_y = y;
    ev.src_x = x;
    ev.num = dam;
    ev.den = (max_hp > 0) ? max_hp : dam;
    gb_fx_push(&ev);

    /*
     * **宣言はここでも消す。**残したままにすると、次に来る素手の打撃が前の呪文の
     * 属性で色づく。宣言するのは `project_m()` / `project_p()` /
     * `make_attack_normal()` の 3 か所で、いずれも「その直後の 1 件」のためである。
     */
    gb_fx_pending = -1;
}

void gb_fx_bolt(int src_y, int src_x, int y, int x, int typ)
{
    gb_fx_event ev;

    ev.kind = 2;
    ev.typ = typ;
    ev.y = y;
    ev.x = x;
    ev.src_y = src_y;
    ev.src_x = src_x;
    ev.num = 1;
    ev.den = 1;
    gb_fx_push(&ev);
}

int gb_fx_element_of(int typ)
{
    /*
     * ## 何を根拠に振るか（2026-08-22。GH-27 / GH-27b）
     *
     * **コア自身が属性ごとの色を宣言している**——`gensoband/lib/pref/spell-xx.prf` の
     * `Z:<GF 名>:<色の文字>` 行（読むのは `files.c:755`、引くのは `spells1.c:204`）。
     * 幻想蛮怒だけの属性をどの束へ入れるかは、**まずその宣言を見て**決めた。
     * 註釈の `コア:` はその文字列である（`w` 白 / `y` 黄 / `r` 赤 / `R` 明赤 /
     * `B` 明青 / `G` 明緑 / `v` 紫 / `D` 暗灰 / `d` 黒 / `s` 灰 / `U` 明茶）。
     *
     * **束は 10 種しかない**（`presentation/frame/combat_fx.h`）ので、色が近いだけで
     * 意味の合わないものは白（物理）に残す。**当てずっぽうで色を付けない**
     * ——それらしい色が付いていると「決めてある」ように見えて、後から直せなくなる。
     */
    switch (typ) {
    case GF_FIRE:
    case GF_PLASMA:
    case GF_METEOR:
    case GF_HELL_FIRE:
    case GF_ROCKET:
    case GF_LAVA_FLOW: /* 変愚も火（`presentation_bridge.cpp` の `LAVA_FLOW`） */
    case GF_KANAMEISHI: /* 要石。コア: DDDroy ＝ `GF_METEOR` と 1 字も違わない */
    case GF_REDMAGIC: /* レミリア専用の赤い地獄の劫火。コア: r */
    case GF_GUNGNIR: /* レミリア専用。**名は光の剣だが「表示が赤い」**。コア: r */
    case GF_LUNATIC_TORCH: /* クラウンピースの松明。コア: rrRv */
    case GF_STEAM: /* 蒸気。当たった相手は「溶けた」（`spells1.c:807`） */
        return 1; /* 火 */
    case GF_COLD:
    case GF_ICE:
    case GF_WATER:
    case GF_WATER_FLOW:
    case GF_HOLY_WATER:
    case GF_MAKE_BLIZZARD:
    case GF_MOSES: /* 早苗「海が割れる日」＝水。コア: B */
        return 2; /* 冷（水もここへ） */
    case GF_ELEC:
    case GF_MAKE_STORM:
        return 3; /* 電 */
    case GF_ACID:
    case GF_MAKE_ACID_PUDDLE:
        return 4; /* 酸 */
    case GF_POIS:
    case GF_NUKE:
    case GF_MAKE_POISON_PUDDLE:
    case GF_POLLUTE: /* 汚染。コア: rrRvvv（毒と同じ紫の側） */
        return 5; /* 毒（核熱もここへ） */
    case GF_DARK:
    case GF_DARK_WEAK:
    case GF_NETHER:
    case GF_DEATH_RAY:
    case GF_SOULSTEAL: /* 魂を奪う。コア: R */
    case GF_POSSESSION: /* 憑依。コア: R */
        return 6; /* 闇 */
    case GF_LITE:
    case GF_LITE_WEAK:
    case GF_HOLY_FIRE:
    /*
     * **`GF_PSY_SPEAR` は変愚に揃えて光へ移した**（GH-27b。2026-08-22）。
     * 前はここだけ精神（桃）で、**同じ光の剣が変愚と幻想蛮怒で違う色に光っていた**。
     * どちらのコアも pref では `y`（黄）と宣言しているので、光のほうが近い。
     */
    case GF_PSY_SPEAR:
    /*
     * **退魔 4 段＝幻想蛮怒の「破邪」。** コアは `wwwy` と宣言していて、
     * これは**同じコアの `GF_HOLY_FIRE` と 1 字も違わない**。だから光へ入れる。
     * @note 破邪は幻想蛮怒が新設した耐性の軸だが、**専用の束を作っても
     *   コアの言う色では光と同じ**になる。増やすかどうかは
     * の宿題である。
     */
    case GF_PUNISH_1:
    case GF_PUNISH_2:
    case GF_PUNISH_3:
    case GF_PUNISH_4:
    case GF_RAINBOW: /* 虹・プリズム */
    case GF_BANKI_BEAM: /* 赤蛮奇のビーム。コア: wy */
    case GF_BANKI_BEAM2:
        return 7; /* 光（破邪もここへ） */
    case GF_CHAOS:
    case GF_CONFUSION:
    case GF_DISENCHANT:
    case GF_KYUTTOSHITEDOKA_N: /* コアの色は `GF_CHAOS` と同じ「全色から 1 つ」 */
        return 8; /* 混沌 */
    /*
     * 時空（B。2026-08-22）。**顔ぶれはコアが決めている**——`spells1.c` で
     * `p_ptr->resist_time` が守っているのがこの 4 つである（:10950 / :11217 /
     * :11241 / :11341）。
     *
     * **変愚とわざと食い違わせている。** 変愚では `TIME` と `NEXUS` は混沌・
     * `GRAVITY` は物理のままで、それでよい——変愚には因果混乱耐性が在り、
     * 幻想蛮怒はそれを**廃して時空へ畳んだ**（`change.txt`）。**軸が違う。**
     * GH-27b（同じ攻撃が違う色で光っていた取りこぼし）とは別物である。
     */
    case GF_TIME:
    case GF_NEXUS:
    case GF_DISTORTION:
    case GF_GRAVITY:
        return 10; /* 時空 */
    /*
     * 精神。**ここは 2026-08-22 まで `GF_PSY_SPEAR` 1 件しか無く、
     * 本物の精神攻撃が全部白へ落ちていた**（GH-27b）。変愚は同じ 5 種を
     * 精神にしている（`presentation_bridge.cpp` の `PSI` / `PSI_DRAIN` /
     * `MIND_BLAST` / `BRAIN_SMASH` / `DOMINATION`）ので、そちらへ揃えた。
     */
    case GF_PSI:
    case GF_PSI_DRAIN:
    case GF_MIND_BLAST:
    case GF_BRAIN_SMASH:
    case GF_DOMINATION:
    case GF_NIGHTMARE: /* 悪夢。コア: B ＝ `GF_PSI` と同じ */
    case GF_SATORI: /* さとり「テリブルスーヴニール」 */
    case GF_BRAIN_FINGER_PRINT: /* さとり「ブレインフィンガープリント」 */
        return 9; /* 精神 */
    /*
     * 狂気（B。2026-08-22）。**1 件しか無い**——`p_ptr->resist_insanity` が
     * 守るのは `GF_COSMIC_HORROR` だけである（`spells1.c:11902`）。
     * エルドリッチホラーは属性ではなく正気度の側なので、ここには来ない。
     */
    case GF_COSMIC_HORROR:
        return 11; /* 狂気 */
    default:
        /*
         * 物理・矢・轟音・破片・重力・分解・気・**弾幕**（`GF_MISSILE`）。
         * **変愚もこれらを白にしている**ので、白いのは取りこぼしではない。
         *
         * ここに**わざと残してあるもの**:
         * - `GF_TORNADO`（竜巻）……装備の耐性が無く、浮遊と所持重量で軽減する
         *   ＝風の物理。コアの色は `GGGGw` だが、緑に光らせる根拠にはしない。
         * - `GF_CAUSE_1`〜`4` / `GF_HAND_DOOM`……**変愚も白**なので揃えてある。
         * - `GF_TIMED_SHARD`……コアが `GF_SHARDS` と同じ扱いで、破片は白。
         * - `GF_YOUMU` / `GF_WINDCUTTER` / `GF_REDEYE` / `GF_HOUTOU` /
         *   `GF_SEIRAN_BEAM` ほか……**何の属性か決めきれなかった**。
         *   当てずっぽうで色を付けない（この関数の頭の註記）。
         */
        return 0;
    }
}

int gb_fx_pending_count(void)
{
    return gb_fx_num;
}

int gb_fx_take(gb_fx_event *out, int cap)
{
    int n;
    int i;

    if (!out || (cap <= 0)) {
        gb_fx_num = 0;
        return 0;
    }
    n = (gb_fx_num < cap) ? gb_fx_num : cap;
    for (i = 0; i < n; i++) {
        out[i] = gb_fx_ring[i];
    }
    gb_fx_num = 0;
    return n;
}

/* ==================================================================== 効果音 */

/*
 * 建付けは `gb_shim.h` の同名の節のとおり。積むのは `gb_null_term.c` の
 * `TERM_XTRA_SOUND`、汲むのは `gb_frame.cpp`。**汲んだら消える**（音は状態ではなく
 * 一度きりの出来事）。
 *
 * 溢れたら**古いほうを捨てる**。ゲームスレッドしか触らないので鍵は要らない
 * （上の見せ場の環と同じ）。
 */

#define GB_SOUND_MAX 32

static gb_sound_event gb_sound_ring[GB_SOUND_MAX];
static int gb_sound_num = 0;
static unsigned long gb_sound_pushed_total = 0;
static int gb_sound_wanted_flag = 0;

void gb_sound_set_wanted(int wanted)
{
    gb_sound_wanted_flag = wanted ? 1 : 0;
    if (!gb_sound_wanted_flag) {
        /* 欲しがらなくなったら溜まっているぶんも捨てる（古い音が後で鳴らないように）。 */
        gb_sound_num = 0;
    }
}

int gb_sound_wanted(void)
{
    return gb_sound_wanted_flag;
}

void gb_sound_push(int v)
{
    gb_sound_event *ev;
    cptr name;
    int i;

    if (!gb_sound_wanted_flag) {
        return;
    }
    if ((v <= 0) || (v >= SOUND_MAX)) {
        return;
    }
    name = angband_sound_name[v];
    if (!name || !name[0]) {
        return;
    }

    if (gb_sound_num >= GB_SOUND_MAX) {
        for (i = 1; i < GB_SOUND_MAX; i++) {
            gb_sound_ring[i - 1] = gb_sound_ring[i];
        }
        gb_sound_num = GB_SOUND_MAX - 1;
    }

    ev = &gb_sound_ring[gb_sound_num++];
    my_strcpy(ev->name, name, sizeof(ev->name));
    /*
     * **鳴ったマスは @ のマス**にする。`TERM_XTRA_SOUND` は番号しか運ばないので
     * 音源の位置が分からない——画面側では頭で鳴る。マスを持つ音は後から
     * 呼び出し箇所ごとに足していく。
     */
    if (character_generated) {
        ev->y = (short)py;
        ev->x = (short)px;
    } else {
        ev->y = 0;
        ev->x = 0;
    }
    gb_sound_pushed_total++;
}

int gb_sound_take(gb_sound_event *out, int cap)
{
    int n;
    int i;

    if (!out || (cap <= 0)) {
        gb_sound_num = 0;
        return 0;
    }
    n = (gb_sound_num < cap) ? gb_sound_num : cap;
    for (i = 0; i < n; i++) {
        out[i] = gb_sound_ring[i];
    }
    gb_sound_num = 0;
    return n;
}

unsigned long gb_sound_total(void)
{
    return gb_sound_pushed_total;
}

/* ============================== サブウィンドウの割り当て世代（M1 / R1） */

/*!
 * `=`→`w` の画面で割り当てが変わった回数（`gb_shim.h` の `gb_window_flags_generation`）。
 * **`window_flag[]` の差分では見分けられない**――あの配列を書くのは遊ぶ側だけではなく、
 * `toggle_inven_equip()`（`object1.c:4036`）が床の品物を選ぶたびに持ち物と装備を
 * 全窓で入れ替える。差分で拾うと歩くだけで振動する（変愚が実際に踏んだ穴)。
 */
static unsigned int gb_window_flags_gen = 0;

void gb_window_flags_bumped(void)
{
    gb_window_flags_gen++;
}

unsigned int gb_window_flags_generation(void)
{
    return gb_window_flags_gen;
}

/* ================================================== コマンドの表（M1 / S3） */

/*
 * **`menu_info` / `special_menu_info` の型は `externs.h` に出ていない**
 * （`util.c:4201` と `:4478` の中だけで typedef している）。配列そのものは
 * static ではないのでリンクはできる。そこで**同じ並びの型をここに写して** extern する。
 *
 * 写しなので、先方が構造体を変えたら黙って別物を読む。それを防ぐために
 * `gb_menu_table_sane()` で**表の実際の値**を確かめ、合わなければ 0 件を返す
 * （呼び手の `gb_pad_commands.cpp` が手書きの最小表へ落ちる）。
 * **`gensoband/src/` は 1 行も触らない**（設計 §1 制約 2）。
 */
typedef struct gb_menu_naiyou {
    cptr name;
    byte cmd;
    bool fin;
} gb_menu_naiyou;

typedef struct gb_special_menu_naiyou {
    cptr name;
    byte window;
    byte number;
    byte jouken;
    byte jouken_naiyou;
} gb_special_menu_naiyou;

extern gb_menu_naiyou menu_info[10][10];
extern gb_special_menu_naiyou special_menu_info[];

#define GB_MENU_CLASS 1 /* util.c:4488 の MENU_CLASS */
#define GB_MENU_WILD 2 /* 同 MENU_WILD */

/*!
 * @brief 型の写しが合っているかを、表の**実際の値**で確かめる。
 * @details 大きさが同じでも並びが違えば読み間違える。だから値で見る。
 * ここに挙げた 5 つは `util.c:4210` の表の頭と尻で、どれも意味のある値である。
 */
static int gb_menu_table_sane(void)
{
    if (menu_info[0][0].fin) return 0; /* 分類は fin == FALSE */
    if (menu_info[0][0].cmd != 1) return 0; /* 「魔法/特殊能力」→ 小メニュー 1 */
    if (!menu_info[1][0].fin) return 0; /* 小メニューの項目は fin == TRUE */
    if (menu_info[1][0].cmd != (byte)0x6D) return 0; /* 「呪文を使う(m)」の m */
    if (!menu_info[0][9].name || menu_info[0][9].name[0]) return 0; /* 尻は空文字列 */
    return 1;
}

/*!
 * @brief コマンド → その配列で押すべきキー（逆引き）。
 * @details `util.c:4766` が `keymap_act[mode][cmd]` を見ているのと同じ理屈。
 * 変愚の `resolve_pad_command_keys()` の写し。
 * @return 押すキー。**0 は「そのコマンドは出せない」**
 */
static unsigned char gb_key_for_command(int mode, unsigned char command)
{
    int i;

    if (!command) {
        return 0;
    }
    for (i = 0; i < 256; i++) {
        cptr act = keymap_act[mode][i];
        if (!act) continue;
        if ((act[0] == (char)command) && !act[1]) {
            return (unsigned char)i;
        }
    }
    /* 逆引き失敗。cmd 自身がキーマップ済みなら押しても別物になるので出さない。 */
    if (keymap_act[mode][command]) {
        return 0;
    }
    return command;
}

/*!
 * @brief `special_menu_info` による名札の差し替え（`util.c:4563-4580` の写し）。
 * @details 画面に出る名前とパッドの名札を食い違わせないために、判定を丸ごと写す。
 */
static cptr gb_menu_name(int window, int number, cptr fallback)
{
    int i;

    for (i = 0;; i++) {
        if (!special_menu_info[i].name || !special_menu_info[i].name[0]) break;
        if ((int)special_menu_info[i].window != window) continue;
        if ((int)special_menu_info[i].number != number) continue;
        switch (special_menu_info[i].jouken) {
        case GB_MENU_CLASS:
            if (p_ptr->pclass == special_menu_info[i].jouken_naiyou) {
                fallback = special_menu_info[i].name;
            }
            break;
        case GB_MENU_WILD:
            if (!dun_level && !p_ptr->inside_arena && !p_ptr->inside_quest) {
                if ((byte)p_ptr->wild_mode == special_menu_info[i].jouken_naiyou) {
                    fallback = special_menu_info[i].name;
                }
            }
            break;
        default:
            break;
        }
    }
    return fallback;
}

int gb_read_pad_commands(gb_pad_command *out, int cap)
{
    int n = 0;
    int g;

    if (!out || (cap <= 0)) {
        return GB_ERR_ARG;
    }
    if (!gb_menu_table_sane()) {
        return 0;
    }

    /* menu_info[0] は分類（fin == FALSE で cmd が小メニュー番号）。 */
    for (g = 0; g < 10; g++) {
        const gb_menu_naiyou *group = &menu_info[0][g];
        cptr group_name;
        int sub;
        int k;

        if (group->fin) continue;
        if (!group->name || !group->name[0]) continue;
        sub = (int)group->cmd;
        if ((sub <= 0) || (sub >= 10)) continue;
        /*
         * **英語ならカタログで引く**（フック #33。GR-01）。この名札は
         * `pad_commands` に載って画面側のコマンドメニューへ出るだけで、
         * **Term を 1 度も通らない**——だから #1 では拾えず、未訳一覧にも出ない。
         * 引けなければ原文が返るので、日本語のときは 1 ビットも変わらない。
         */
        group_name = gb_tr(gb_menu_name(0, g, group->name)); //GB: english layer #33

        for (k = 0; k < 10; k++) {
            const gb_menu_naiyou *item = &menu_info[sub][k];

            /* fin == TRUE の項目だけがコマンド（FALSE は小メニューへの分岐）。 */
            if (!item->fin) continue;
            if (!item->name || !item->name[0] || !item->cmd) continue;
            if (n >= cap) return n;

            memset(&out[n], 0, sizeof(out[n]));
            out[n].command = item->cmd;
            out[n].key_original = gb_key_for_command(KEYMAP_MODE_ORIG, item->cmd);
            out[n].key_rogue = gb_key_for_command(KEYMAP_MODE_ROGUE, item->cmd);
            gb_copy_str(out[n].group, (int)sizeof(out[n].group), group_name);
            gb_copy_str(out[n].label, (int)sizeof(out[n].label), gb_tr(gb_menu_name(sub, k, item->name))); //GB: english layer #33
            n++;
        }
    }
    return n;
}

unsigned char gb_pad_key_for_command(int rogue, int command)
{
    if ((command <= 0) || (command > 255)) {
        return 0;
    }
    return gb_key_for_command(rogue ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG, (unsigned char)command);
}

int gb_pad_table_stamp(void)
{
    int s = (int)p_ptr->pclass;

    s = s * 31 + (p_ptr->wild_mode ? 1 : 0);
    s = s * 31 + (dun_level ? 1 : 0);
    s = s * 31 + (p_ptr->inside_arena ? 1 : 0);
    s = s * 31 + (p_ptr->inside_quest ? 1 : 0);
    s = s * 31 + (rogue_like_commands ? 1 : 0);
    s = s * 31 + (character_generated ? 1 : 0);
    return s;
}

/*!
 * @brief 色表を 16 色ぶん写す（`gb_shim.h` の `gb_read_palette`）。
 * @details `angband_color_table[i]` は **4 バイト組で、先頭 1 バイトは使っていない**
 * （`variable.c:605` の並びは `{0x00, R, G, B}`）。だから添字は 1〜3 である。
 * 変愚の `fill_term_palette()`（`presentation_bridge.cpp:1018`）も同じ添字で読む。
 */
int gb_read_palette(unsigned char *rgb, int cap)
{
    int i;

    if (!rgb || (cap < 16 * 3)) {
        return GB_ERR_ARG;
    }
    for (i = 0; i < 16; i++) {
        rgb[i * 3 + 0] = angband_color_table[i][1];
        rgb[i * 3 + 1] = angband_color_table[i][2];
        rgb[i * 3 + 2] = angband_color_table[i][3];
    }
    return 16 * 3;
}

void gb_apply_cursor_mode(int enable)
{
    if (!enable) {
        return; /* 従来どおり（文字キー主体）。use_menu はコアに任せる */
    }
    if (!character_generated) {
        return; /* タイトル・誕生画面はコアの別メニュー。触らない */
    }
    command_menu = TRUE; /* Enter ＝ コマンドメニュー */
    use_menu = TRUE; /* 以降の選択はコアの 》 カーソルで */
}

/*!
 * @brief 現在地名。`xtra1.c:301`（町名）と `bldg.c:12007`（ダンジョン名）の写し。
 * @details 町 = `town[p_ptr->town_num].name`、ダンジョン = `d_name + d_info[].name`。
 * どちらでもないとき（クエスト内・荒野）は空にする——**捏造しない**。
 */
static void gb_place_name(char *out, int cap)
{
    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!character_generated) {
        return;
    }

    if (dun_level && dungeon_type && d_info && d_name && (dungeon_type < max_d_idx)) {
        gb_copy_str(out, cap, d_name + d_info[dungeon_type].name);
        return;
    }

    if (town && (p_ptr->town_num > 0) && (p_ptr->town_num < max_towns)) {
        gb_copy_str(out, cap, town[p_ptr->town_num].name);
    }
}

/*!
 * @brief 「地上」「12 階」等。`xtra1.c:1546` の `prt_depth()` と**同じ文言**にする。
 * @details 画面側で組み直すと文言が割れるので、こちらで作って運ぶ。
 * 中身は SJIS（`_()` 相当の分岐はコアのビルド定義 `JP` に従う）。
 *
 * **英語のときは `#else` 側の文言をここで選ぶ**（設計 §5 の「アダプタ側」）。
 * カタログでは引けない——`sprintf` で数を埋めた後の字が来るので鍵に合わない
 * （E3 の通しで `15 階` が残って判った）。ここは `gensoband/src` の外なので
 * フックを増やさずに直せる。
 */
static void gb_depth_text(char *out, int cap)
{
    char buf[32];
    const int en = gb_lang_enabled();

    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!character_generated) {
        return;
    }

    if (!dun_level) {
#ifdef JP
        strcpy(buf, en ? "Surf." : "地上");
#else
        strcpy(buf, "Surf.");
#endif
    } else if (p_ptr->inside_quest && !dungeon_type) {
#ifdef JP
        strcpy(buf, en ? "Quest" : "地上");
#else
        strcpy(buf, "Quest");
#endif
    } else if (depth_in_feet) {
        sprintf(buf, "%d ft", (int)dun_level * 50);
    } else {
#ifdef JP
        sprintf(buf, en ? "Lev %d" : "%d 階", (int)dun_level);
#else
        sprintf(buf, "Lev %d", (int)dun_level);
#endif
    }

    gb_copy_str(out, cap, buf);
}

int gb_read_hud(gb_hud_data *out)
{
    if (!out) {
        return GB_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));

    gb_copy_str(out->name, (int)sizeof(out->name), player_name);
    out->in_game = character_generated ? 1 : 0;

    /*
     * `p_ptr` は静的な実体（variable.c:894 `player_type *p_ptr = &p_body;`）なので
     * 常に読める。ただし `character_generated` が偽のあいだは中身が既定値のままで、
     * 誕生画面の途中経過（振り直し中の能力値など）を意味しない。**数値を出すのは
     * ゲームが始まってから**にする——半端な値を HUD に出すと画面側で嘘になる。
     */
    if (!character_generated) {
        return GB_OK;
    }

    out->hp = (int)p_ptr->chp;
    out->hp_max = (int)p_ptr->mhp;
    out->sp = (int)p_ptr->csp;
    out->sp_max = (int)p_ptr->msp;
    out->level = (int)p_ptr->lev;
    out->depth = (int)dun_level;
    out->gold = (long)p_ptr->au;

    gb_place_name(out->place, (int)sizeof(out->place));
    gb_depth_text(out->depth_text, (int)sizeof(out->depth_text));

    return GB_OK;
}

/* =============================================================== 画面の状態（P3） */

int gb_screen_flags(void)
{
    int bits = 0;

    if (character_generated) {
        bits |= GB_SCREEN_GENERATED;
    }
    /*
     * **`character_icky` は計数器である。** `screen_save()`（util.c:3291）が `++`、
     * `screen_load()`（:3309）が `--` する。`== TRUE` で比べると入れ子の 2 段目を
     * 取り逃がす（先方も `cmd7.c:5080` でそう注記している）。非 0 だけを見る。
     */
    if (character_icky) {
        bits |= GB_SCREEN_ICKY;
    }
    if (character_xtra) {
        bits |= GB_SCREEN_XTRA;
    }
    if (character_dungeon) {
        bits |= GB_SCREEN_DUNGEON;
    }
    if (character_generated && p_ptr->is_dead) {
        bits |= GB_SCREEN_DEAD;
    }
    if (character_generated && p_ptr->playing) {
        bits |= GB_SCREEN_PLAYING;
    }
    return bits;
}

/* =================================================================== 地図（P3） */

int gb_floor_size(int *width, int *height)
{
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return GB_ERR_STATE;
    }
    if (width) {
        *width = (int)cur_wid;
    }
    if (height) {
        *height = (int)cur_hgt;
    }
    return GB_OK;
}

int gb_player_kind(int *pclass, int *prace)
{
    if (!character_generated) {
        return GB_ERR_STATE;
    }
    if (pclass) {
        *pclass = (int)p_ptr->pclass;
    }
    if (prace) {
        *prace = (int)p_ptr->prace;
    }
    return GB_OK;
}

int gb_player_pos(int *x, int *y)
{
    if (!character_generated || !character_dungeon) {
        return GB_ERR_STATE;
    }
    if (x) {
        *x = px;
    }
    if (y) {
        *y = py;
    }
    return GB_OK;
}

/*!
 * @brief そのマスの地形が「見えている」か。**`map_info()` と同じ判定にする**。
 *
 * @details 判定を独自に作ると、コアが画面に描いているものと HD2D の立体が食い違う
 * （記号は伏せたまま形だけ漏れる、あるいはその逆）。`cave.c:985` 以降の分岐を写す:
 * - `FF_REMEMBER` を持つ地形（壁・扉・階段）… `CAVE_MARK` だけ。盲目でも消えない
 * - それ以外（床）… 盲目なら見えない。`CAVE_MARK|CAVE_LITE|CAVE_MNLT` か、
 *   視界内かつ（自照かつ消灯でない、または暗視）
 */
static int gb_grid_known(cave_type *c_ptr, s16b feat)
{
    feature_type *f_ptr = &f_info[feat];

    if (have_flag(f_ptr->flags, FF_REMEMBER)) {
        return (c_ptr->info & CAVE_MARK) ? 1 : 0;
    }

    if (p_ptr->blind) {
        return 0;
    }
    if (c_ptr->info & (CAVE_MARK | CAVE_LITE | CAVE_MNLT)) {
        return 1;
    }
    if ((c_ptr->info & CAVE_VIEW)
        && (((c_ptr->info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW) || p_ptr->see_nocto)) {
        return 1;
    }
    return 0;
}

/*! @brief いま照明が当たっているか（`light_level` の 2 の条件）。 */
static int gb_grid_lit_now(cave_type *c_ptr)
{
    if (c_ptr->info & CAVE_MNDK) {
        return 0;
    }
    if (c_ptr->info & (CAVE_LITE | CAVE_MNLT)) {
        return 1;
    }
    return ((c_ptr->info & (CAVE_GLOW | CAVE_VIEW)) == (CAVE_GLOW | CAVE_VIEW)) ? 1 : 0;
}

/*! @brief 地形フラグ → `GB_FEAT_*`（設計 §4.2 の表）。`KNOWN` / `PLAYER` は呼び出し側。 */
static unsigned int gb_feature_bits(s16b feat, cave_type *c_ptr)
{
    feature_type *f_ptr = &f_info[feat];
    unsigned int bits = 0;

    if (have_flag(f_ptr->flags, FF_WALL)) {
        bits |= GB_FEAT_WALL;
    }
    if (have_flag(f_ptr->flags, FF_DOOR)) {
        bits |= GB_FEAT_DOOR;
        /*
         * 開いているか。**`FF_CLOSE`（＝「閉じる」の対象＝いま開いている）で見る。**
         * `FF_OPEN` は「開ける」の対象＝まだ閉じている側なので、意味が逆になる
         * （変愚側 `translate_feature_flags` と同じ理屈。あちらは `CLOSE`）。
         */
        if (have_flag(f_ptr->flags, FF_CLOSE)) {
            bits |= GB_FEAT_DOOR_OPEN;
        }
    }
    /*
     * 瓦礫。幻想蛮怒に「瓦礫」そのもののフラグは無い。いちばん近いのが
     * `FF_HURT_ROCK`（岩石溶解の対象）で、これは壁・鉱脈・隠し扉にも付く。
     * そこで**壁でも扉でもない HURT_ROCK** を瓦礫と読む（変愚の `STONE` と同じ形）。
     * 裏取り: `f_info.txt` を機械的に走査すると、`HURT_ROCK` を持ち `WALL` も `DOOR` も
     * 持たない地形は RUBBLE(49) だけである。
     */
    if (have_flag(f_ptr->flags, FF_HURT_ROCK) && !(bits & (GB_FEAT_WALL | GB_FEAT_DOOR))) {
        bits |= GB_FEAT_RUBBLE;
    }
    /* 階段のほか、ダンジョンの口とクエストの入口も「降りる場所」として同じ扱いにする。 */
    if (have_flag(f_ptr->flags, FF_STAIRS) || have_flag(f_ptr->flags, FF_ENTRANCE)
        || have_flag(f_ptr->flags, FF_QUEST_ENTER)) {
        bits |= GB_FEAT_STAIRS;
    }
    if (have_flag(f_ptr->flags, FF_PERMANENT)) {
        bits |= GB_FEAT_PERMANENT;
    }
    if (have_flag(f_ptr->flags, FF_TREE)) {
        bits |= GB_FEAT_TREE;
    }
    if (have_flag(f_ptr->flags, FF_WATER)) {
        bits |= GB_FEAT_WATER;
    }
    if (have_flag(f_ptr->flags, FF_LAVA)) {
        bits |= GB_FEAT_LAVA;
    }
    if (have_flag(f_ptr->flags, FF_GLOW)) {
        bits |= GB_FEAT_GLOW;
    }
    /* 通り抜けられる床（HD2D の扉の向き判定の主材料）。壁・扉・木・岩は明示的に外す。 */
    if (have_flag(f_ptr->flags, FF_MOVE)
        && !(bits & (GB_FEAT_WALL | GB_FEAT_DOOR | GB_FEAT_TREE | GB_FEAT_RUBBLE))) {
        bits |= GB_FEAT_PASSABLE;
    }
    if (c_ptr->info & CAVE_ROOM) {
        bits |= GB_FEAT_ROOM;
    }
    if ((c_ptr->info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW) {
        bits |= GB_FEAT_GLOWING;
    }
    return bits;
}

int gb_read_map(int x0, int y0, int w, int h, gb_map_cell *out, int cap)
{
    int vx;
    int vy;
    int n = 0;

    if (!out || (w <= 0) || (h <= 0) || (cap < (w * h))) {
        return GB_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return GB_ERR_STATE;
    }

    for (vy = 0; vy < h; vy++) {
        const int gy = y0 + vy;
        for (vx = 0; vx < w; vx++) {
            const int gx = x0 + vx;
            gb_map_cell *cell = &out[n++];
            cave_type *c_ptr;
            s16b feat;
            byte a = 0;
            char c = ' ';
            byte ta = 0;
            char tc = ' ';
            int known;

            memset(cell, 0, sizeof(*cell));
            cell->gx = (short)gx;
            cell->gy = (short)gy;
            cell->ascii = ' ';

            /* 階の外。**空白で返す**（画面側が黒く塗る）。 */
            if (!in_bounds2(gy, gx)) {
                continue;
            }

            c_ptr = &cave[gy][gx];
            feat = get_feat_mimic(c_ptr);
            known = gb_grid_known(c_ptr, feat);

            /* 見た目はコアの `map_info()` そのまま（記号・色を作り直さない）。 */
            map_info(gy, gx, &a, &c, &ta, &tc);
            cell->fg_color = (unsigned char)(a & 0x0F);
            cell->bg_color = (unsigned char)(ta & 0x0F);
            cell->ascii = c;

            if (known) {
                cell->terrain_id = (unsigned short)feat;
                cell->feature_flags = (unsigned short)(GB_FEAT_KNOWN | gb_feature_bits(feat, c_ptr));
            }
            /* @ の居場所は既知・未知に関わらず立てる（隠す情報ではない）。 */
            if ((gy == py) && (gx == px)) {
                cell->feature_flags = (unsigned short)(cell->feature_flags | GB_FEAT_PLAYER);
            }

            if (gb_grid_lit_now(c_ptr)) {
                cell->light_level = 2;
            } else if (c_ptr->info & CAVE_VIEW) {
                cell->light_level = 1;
            } else {
                cell->light_level = 0;
            }

            /*
             * モンスターは **`ml`（視認中）のときだけ**（設計 §4）。
             * 種族は `ap_r_idx`（見かけの種族）を使う——`map_info` が絵に使うのもこちらで、
             * 変化・変装の敵で記号とタイルが食い違わないようにする。
             */
            if (c_ptr->m_idx > 0 && c_ptr->m_idx < m_max) {
                monster_type *m_ptr = &m_list[c_ptr->m_idx];
                if (m_ptr->r_idx && m_ptr->ml) {
                    cell->monster_id = (unsigned short)(m_ptr->ap_r_idx ? m_ptr->ap_r_idx : m_ptr->r_idx);
                    cell->monster_slot = (unsigned short)c_ptr->m_idx;
                }
            }

            /*
             * 床のアイテムは `OM_FOUND`（見つけたもの）の先頭 1 つだけ。
             * **いま照らされているマスに限る**（`light_level == 2`）。
             *
             * P4 で塞いだ穴（実機の「警告: タイル欠け 1」の正体）: ここを光と無関係に
             * 立てていた。すると**記憶しているだけの暗いマス**でも `object_id` が入り、
             * 画面側は「実体がいる」と見て板を探しにいく（`entity_view.cpp:130` の
             * `is_object = cell.object_id != 0`）。ところが絵を決める `resolve_tile_index`
             * （`gb_frame.cpp`）は設計 §5.1 の順「@ ＞ 見えている敵 ＞ **照らされた**床の物 ＞
             * 地形」に従って**物を飛ばし地形の絵を返す**ので、探しにいく先が `F1`（床）に
             * なる。地形には板が無い（板は R/K/P だけ。地形はタイルを地面に貼る）ので
             * 必ず外れ、欠けが 1 つ数えられていた。**板の焼き漏れではない。**
             *
             * 揃える相手は 2 つとも同じ条件になっている:
             *   - 絵の決め方 … `resolve_tile_index`（`light_level >= 2 && object_id`）
             *   - ミニマップ … `gb_read_minimap`（`gb_grid_lit_now(c_ptr)`）
             * 変愚も同じ形である（`presentation_bridge.cpp:3483` の `if (lit_now && ...)` で
             * `cell.object_id` を立て、:524 の `resolve_tile_index` も `lit_now` で見る）。
             * 記憶しているだけの物は Term の記号（`ascii`）には残るので、情報は落ちない。
             */
            if (cell->light_level >= 2) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = &o_list[this_o_idx];
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        cell->object_id = (unsigned short)o_ptr->k_idx;
                        break;
                    }
                }
            }

            /* 未視認のマスは地形を出さない（真っ暗）。ただし見えている敵の記号は残す。 */
            if (!known) {
                cell->bg_color = 0;
                if (!cell->monster_id) {
                    cell->ascii = ' ';
                    cell->fg_color = 0;
                }
            }
        }
    }

    return n;
}

int gb_read_minimap(unsigned char *out, int cap)
{
    int gx;
    int gy;
    int need;

    if (!out || (cap <= 0)) {
        return GB_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return GB_ERR_STATE;
    }

    need = (int)cur_hgt * (int)cur_wid;
    if (cap < need) {
        return GB_ERR_ARG;
    }
    memset(out, GB_MM_UNKNOWN, (size_t)need);

    for (gy = 0; gy < (int)cur_hgt; gy++) {
        for (gx = 0; gx < (int)cur_wid; gx++) {
            cave_type *c_ptr = &cave[gy][gx];
            const s16b feat = get_feat_mimic(c_ptr);
            feature_type *f_ptr = &f_info[feat];
            const int known = gb_grid_known(c_ptr, feat);
            int monster_here = 0;
            unsigned char kind;

            if (c_ptr->m_idx > 0 && c_ptr->m_idx < m_max) {
                monster_type *m_ptr = &m_list[c_ptr->m_idx];
                monster_here = (m_ptr->r_idx && m_ptr->ml) ? 1 : 0;
            }
            /* 未踏破は出さない。ただし見えている敵（テレパシー等）は出す。 */
            if (!known && !monster_here) {
                continue;
            }

            kind = GB_MM_FLOOR;
            if (known) {
                if (have_flag(f_ptr->flags, FF_STAIRS) || have_flag(f_ptr->flags, FF_ENTRANCE)
                    || have_flag(f_ptr->flags, FF_QUEST_ENTER)) {
                    kind = GB_MM_STAIRS;
                } else if (have_flag(f_ptr->flags, FF_DOOR)) {
                    kind = GB_MM_DOOR;
                } else if (have_flag(f_ptr->flags, FF_WALL)) {
                    /* 山は壁と分けて送る（町の読み方が「敷地か岩山か」を決めるのに使う）。 */
                    kind = have_flag(f_ptr->flags, FF_MOUNTAIN) ? GB_MM_MOUNTAIN : GB_MM_WALL;
                }
            }
            if (known && gb_grid_lit_now(c_ptr) && c_ptr->o_idx && (kind != GB_MM_STAIRS)) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = &o_list[this_o_idx];
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        kind = GB_MM_ITEM;
                        break;
                    }
                }
            }
            if (monster_here) {
                kind = GB_MM_MONSTER;
            }
            out[(gy * (int)cur_wid) + gx] = kind;
        }
    }

    if (in_bounds2(py, px)) {
        out[(py * (int)cur_wid) + px] = GB_MM_PLAYER;
    }

    return need;
}


/* ============================================ 周囲の地形の内訳（環境音の層） */

/*! 数える半径（マス）。**耳が届くと思える範囲**であって、視界とは別。変愚と同じ 12。 */
#define GB_SURROUNDINGS_RADIUS 12

int gb_read_surroundings(gb_surroundings *out)
{
    int dy;
    int dx;
    int counted = 0;
    int grass = 0;
    int tree = 0;
    int dirt = 0;
    int swamp = 0;
    int water = 0;
    int deep_water = 0;
    int lava = 0;
    int rock = 0;
    int glass = 0;
    int wall = 0;
    const int r = GB_SURROUNDINGS_RADIUS;

    if (!out) {
        return GB_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return GB_OK; /* radius 0 のまま＝「数えていない」 */
    }

    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            const int gy = py + dy;
            const int gx = px + dx;
            cave_type *c_ptr;
            feature_type *f_ptr;
            s16b feat;
            int is_wall;

            if (((dx * dx) + (dy * dy)) > (r * r)) {
                continue; /* 円で数える */
            }
            if (!in_bounds2(gy, gx)) {
                continue;
            }
            c_ptr = &cave[gy][gx];
            feat = get_feat_mimic(c_ptr);
            if (!gb_grid_known(c_ptr, feat)) {
                continue;
            }
            counted++;

            f_ptr = &f_info[feat];
            is_wall = have_flag(f_ptr->flags, FF_WALL) ? 1 : 0;
            if (is_wall) {
                wall++;
            }
            if (have_flag(f_ptr->flags, FF_GLASS)) {
                glass++;
            }

            /* ---- 材料は排他。順番に意味がある（`gb_shim.h` の註記）---- */
            if (feat == feat_swamp) {
                swamp++;
            } else if (have_flag(f_ptr->flags, FF_LAVA)) {
                lava++;
            } else if (have_flag(f_ptr->flags, FF_WATER)) {
                if (have_flag(f_ptr->flags, FF_DEEP)) {
                    deep_water++;
                } else {
                    water++;
                }
            } else if (have_flag(f_ptr->flags, FF_TREE)) {
                tree++;
            } else if (feat == feat_mountain) {
                rock++;
            } else if (have_flag(f_ptr->flags, FF_HURT_ROCK) && !is_wall
                && !have_flag(f_ptr->flags, FF_DOOR)) {
                rock++; /* 瓦礫（変愚の STONE に当たる） */
            } else if ((feat == feat_grass) || (feat == feat_brake) || (feat == feat_flower)) {
                grass++;
            } else if (feat == feat_dirt) {
                dirt++;
            }
        }
    }

    if (counted <= 0) {
        return GB_OK;
    }

#define GB_SURR_RATIO(N) ((unsigned char)(((N) * 255) / counted))
    out->grass = GB_SURR_RATIO(grass);
    out->tree = GB_SURR_RATIO(tree);
    out->dirt = GB_SURR_RATIO(dirt);
    out->swamp = GB_SURR_RATIO(swamp);
    out->water = GB_SURR_RATIO(water);
    out->deep_water = GB_SURR_RATIO(deep_water);
    out->lava = GB_SURR_RATIO(lava);
    out->rock = GB_SURR_RATIO(rock);
    out->glass = GB_SURR_RATIO(glass);
    out->wall = GB_SURR_RATIO(wall);
#undef GB_SURR_RATIO
    out->radius = (unsigned char)r;
    out->counted = (unsigned short)((counted > 0xFFFF) ? 0xFFFF : counted);
    return GB_OK;
}

int gb_read_floor_info(gb_floor_info *out)
{
    int day = 0;
    int hour = 0;
    int minute = 0;

    if (!out) {
        return GB_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->day_minute = 720;
    out->daytime = 1;

    if (!character_generated) {
        return GB_OK;
    }

    out->dungeon_id = (int)dungeon_type;
    out->dun_level = (int)dun_level;
    out->town_id = (int)p_ptr->town_num;
    out->wild_mode = p_ptr->wild_mode ? 1 : 0;

    if (p_ptr->inside_arena || p_ptr->inside_battle) {
        out->kind = 4; /* 闘技場 */
    } else if (p_ptr->inside_quest) {
        out->kind = 3; /* クエスト階 */
    } else if (dun_level) {
        out->kind = 2; /* ダンジョン */
    } else {
        out->kind = 1; /* 地上（町・荒野・広域マップ） */
    }

    gb_place_name(out->place_name, (int)sizeof(out->place_name));

    extract_day_hour_min(&day, &hour, &minute);
    out->day_minute = ((hour % 24) * 60) + (minute % 60);
    out->daytime = is_daytime() ? 1 : 0;
    out->light_radius = (int)p_ptr->cur_lite;

    return GB_OK;
}

/* ================================================================ セーブの口（P3） */

const char *gb_save_dir(void)
{
    return ANGBAND_DIR_SAVE ? ANGBAND_DIR_SAVE : "";
}

int gb_set_savefile(const char *slot)
{
    if (!slot || !slot[0]) {
        return GB_ERR_ARG;
    }
    if (!ANGBAND_DIR_SAVE || !ANGBAND_DIR_SAVE[0]) {
        return GB_ERR_STATE; /* init_file_paths() より前 */
    }
    if (strlen(slot) >= sizeof(savefile_base)) {
        return GB_ERR_ARG;
    }

    /* `process_player_name()` が両方を見る（片方だけだと sf が真に倒れて訊かれる）。 */
    strcpy(savefile_base, slot);
    path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, slot);
    return GB_OK;
}

void gb_clear_savefile(void)
{
    savefile[0] = '\0';
    savefile_base[0] = '\0';
}

const char *gb_savefile_base(void)
{
    return savefile_base;
}

const char *gb_savefile_path(void)
{
    return savefile;
}

/* ============================================================= メッセージの読み口 */

int gb_message_count(void)
{
    return (int)message_num();
}

int gb_message_text(int age, char *out, int cap)
{
    cptr s;
    int n;

    if (!out || (cap <= 0)) {
        return GB_ERR_ARG;
    }

    out[0] = '\0';
    if (age < 0) {
        return 0;
    }

    /* `message_str()` は範囲外に空文字列を返す（util.c:2752）。素直に任せる。 */
    s = message_str(age);
    if (!s) {
        return 0;
    }

    gb_copy_str(out, cap, s);
    n = (int)strlen(out);
    return n;
}

/* ======================================================= ゲーム前画面の描き口（P5） */

void gb_term_clear(void)
{
    Term_clear();
}

void gb_term_putstr(int x, int y, int attr, const char *sjis)
{
    if (!sjis) {
        return;
    }
    Term_putstr(x, y, -1, (byte)attr, (cptr)sjis);
}

void gb_term_present(void)
{
    /* `Term_fresh()` の締めが `TERM_XTRA_FRESH` を起こし、null term がフックの
     * `present` を呼ぶ（`gb_null_term.c`）。つまりこれ 1 つで frame が線に乗る。 */
    Term_fresh();
}

/* ======================================================= 紙の書き口（フック #29） */

/*!
 * @brief `fprintf()` の代わり。書式と `%s` の引数をカタログで引いてから書く。
 *
 * @details 知識の画面（`~`）と日記（`|`）は一時ファイルへ書いてから `show_file()`
 * で見せる。`fprintf()` はフックを 1 つも通らないので、書式（#2）も引数（#3）も
 * 引けなかった。`vstrnfmt()` を挟むだけで両方が効く——#2 は `vstrnfmt()` の頭に、
 * #3 は `%s` の取り出しに在る。
 *
 * **日本語モードでは 1 ビットも動きが変わらない**（`gb_lookup()` が
 * `gb_lang_enabled()` を見て素通りする）。
 *
 * 枠は `msg_format()` と同じ 1024 バイト。溢れたぶんは切れる（`fprintf` は
 * 切らなかったが、画面は 80 桁なので実害は無い。設計 §11.13）。
 */
void gb_fprintf(FILE *fff, const char *fmt, ...)
{
	char buf[1024];
	va_list vp;

	if (!fff || !fmt) return;

	va_start(vp, fmt);
	(void)vstrnfmt(buf, sizeof(buf), (cptr)fmt, vp);
	va_end(vp);

	fputs(buf, fff);
}

/*!
 * @brief `fputs()` の代わり。引数の順は `fputs()` と同じ。
 * @details 値で埋める所が無いので `gb_tr()` を 1 回引くだけでよい。
 */
void gb_fputs(const char *s, FILE *fff)
{
	if (!fff || !s) return;

	fputs(gb_tr(s), fff);
}
