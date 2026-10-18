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
#include "gb_unaware_category.h"
#include "bridge/unaware_item_categories.h"

#include <stdio.h>
#include <stdlib.h>
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

int gb_birth_race_score_mult(int race_index)
{
    int i;
    int choice = 0;

    if (race_index < 0) {
        return -1;
    }
    for (i = 0; i < MAX_RACES; i++) {
        if (race_info[i].flag_nobirth || race_info[i].flag_special || race_info[i].flag_only_unique) {
            continue;
        }
        if (choice++ == race_index) {
            return race_info[i].score_mult;
        }
    }
    return -1;
}

const char *gb_birth_race_title(int race_index)
{
    int i;
    int choice = 0;

    if (race_index < 0) {
        return NULL;
    }
    for (i = 0; i < MAX_RACES; i++) {
        if (race_info[i].flag_nobirth || race_info[i].flag_special || race_info[i].flag_only_unique) {
            continue;
        }
        if (choice++ == race_index) {
            return race_info[i].title;
        }
    }
    return NULL;
}

static const unsigned char *gb_skip_race_title_space(const unsigned char *text)
{
    while (*text == ' ' || (text[0] == 0x81 && text[1] == 0x40)) {
        text += *text == ' ' ? 1 : 2;
    }
    return text;
}

int gb_birth_race_score_mult_named(const char *title)
{
    int i;
    if (!title) {
        return -1;
    }
    for (i = 0; i < MAX_RACES; i++) {
        const unsigned char *left;
        const unsigned char *right;
        if (race_info[i].flag_nobirth || race_info[i].flag_special || race_info[i].flag_only_unique) {
            continue;
        }
        left = (const unsigned char *)title;
        right = (const unsigned char *)race_info[i].title;
        for (;;) {
            left = gb_skip_race_title_space(left);
            right = gb_skip_race_title_space(right);
            if (*left != *right) {
                break;
            }
            if (!*left) {
                return race_info[i].score_mult;
            }
            left++;
            right++;
        }
    }
    return -1;
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

/*
 * 画面を作るための呼び出しで、ゲーム本体の乱数を進めない。
 * `map_info()`（`cave.c`）は幻覚・色が変わるモンスターで `randint0` / `randint1` /
 * `one_in_` を引き、`monster_desc()`（`monster2.c`）も幻覚中は `one_in_(2)` と
 * `randint1` で名前を選ぶ。原作はゲームの進行に沿った回数だけ引くが、ここは画面を送る
 * 頻度（経過時間）で回数が変わるので、同じセーブ・同じ入力でも乱数の列が一致しなくなる。
 * 呼び出しの前に状態（`Rand_place` と `Rand_state[]`。`z-rand.c` にはほかの状態が無い）を
 * 丸ごと退避し、終わったら戻す。`Rand_external()` は音の選択専用の別の状態なので対象外。
 * `gb_targeting.inl` の `_gb_target_rng_save` と同じ内訳。
 */
typedef struct gb_rng_snapshot {
    u16b place;
    u32b state[RAND_DEG];
} gb_rng_snapshot;

static void gb_rng_save(gb_rng_snapshot *snap)
{
    snap->place = Rand_place;
    memcpy(snap->state, Rand_state, sizeof(snap->state));
}

static void gb_rng_restore(const gb_rng_snapshot *snap)
{
    Rand_place = snap->place;
    memcpy(Rand_state, snap->state, sizeof(snap->state));
}

/*
 * 検査用（環境変数が無ければ何もしない）。`HD2D_TEST_RNG=1` で、呼び出しの間に乱数の状態が
 * どう動いたか（inside）と戻した後（kept）の要約値を標準エラーへ出す。`HD2D_TEST_RNG=nofix` は
 * 戻さない（この直しの前の動きを再現する）。`HD2D_TEST_FORCE_IMAGE=1` は毎回幻覚にする。
 */
static unsigned gb_rng_digest(void)
{
    unsigned h = 2166136261u;
    int i;

    h = (h ^ (unsigned)Rand_place) * 16777619u;
    for (i = 0; i < RAND_DEG; i++) {
        h = (h ^ (unsigned)Rand_state[i]) * 16777619u;
    }
    return h;
}

static void gb_test_force_image(void)
{
    if (getenv("HD2D_TEST_FORCE_IMAGE") != NULL) {
        p_ptr->image = 1000;
    }
}

static void gb_rng_finish(const gb_rng_snapshot *snap, const char *name)
{
    const char *mode = getenv("HD2D_TEST_RNG");
    const int report = (mode != NULL) && (mode[0] != '\0');
    const unsigned inside = report ? gb_rng_digest() : 0;

    if (!(report && (strcmp(mode, "nofix") == 0))) {
        gb_rng_restore(snap);
    }
    if (report) {
        fprintf(stderr, "[hd2d-rng] %s inside=%08x kept=%08x\n", name, inside, gb_rng_digest());
    }
}

static int gb_read_visible_monster_raw(int index, char *out, int cap)
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

int gb_read_visible_monster(int index, char *out, int cap)
{
    gb_rng_snapshot snap;
    int result;

    gb_rng_save(&snap);
    gb_test_force_image();
    result = gb_read_visible_monster_raw(index, out, cap);
    gb_rng_finish(&snap, "gb_read_visible_monster");
    return result;
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

/* ==================================================================== 魔法・投射物の効果（projections / missiles） */

/*
 * `GameFrame::projections`（飛ぶ魔法・ボール・ブレス・ビーム・視界内の全員）と `GameFrame::missiles`（矢弾・投擲）の材料
 * （決めたこと 2026-10-11）。
 *
 * 原作の `project()` などに足した**観測の呼び出し 1 行**（`tools/targeting/attack_fx_overlay.py`。記録するだけで遊びの結果は
 * 変えない）がここを呼ぶ。ここは原作の型（`GF_xxx`・`PROJECT_xxx`・物）を読んで、属性のコア名・形・範囲・経路・旗の整数に直し、
 * 共通の記録の置き場（`presentation/bridge/named_attack_fx.h`）へ `gb_named_fx_*`（gb_frame.cpp）経由で積む。
 * 属性のコア名から見た目の名前への変換は C++ 側（`attack_fx_core_attr_aliases.h` の `gensoband_look_name`）。
 *
 * 旧来の `combat_fx` の飛道（`gb_fx_bolt`）は**積まない**（上の記録と二重に出るため。命中の火花だけ残す）。
 */
#include "gb_fx_names.inc"

/*! `presentation/frame/attack_fx.h` の `ProjectionShape` / `AttackFxOrigin` / `ProjectionFxFlag` / `MissileKind` と同じ並び。 */
enum { GB_FXS_BOLT = 0, GB_FXS_BEAM = 1, GB_FXS_BALL = 2, GB_FXS_BREATH = 3, GB_FXS_JUMP = 4 };
enum { GB_FXO_PLAYER = 0, GB_FXO_MONSTER = 1, GB_FXO_OTHER = 2 };
enum { GB_FXF_HIDDEN = 1, GB_FXF_BLIND = 2, GB_FXF_REFLECTED = 4, GB_FXF_ALL_IN_SIGHT = 8 };
enum { GB_FXM_ARROW = 0, GB_FXM_BOLT = 1, GB_FXM_SHOT = 2, GB_FXM_THROWN = 3 };

/*! 1 件に積むマスの上限（`named_attack_fx.h` の `kNamedMaxCells` と同じ）。 */
#define GB_FX_CELLS 512

static const char *gb_fx_gf_name(int typ)
{
    size_t i;

    if (typ <= 0) {
        return "";
    }
    for (i = 0; i < sizeof(gb_fx_gf_names) / sizeof(gb_fx_gf_names[0]); i++) {
        if (gb_fx_gf_names[i].typ == typ) {
            return gb_fx_gf_names[i].name;
        }
    }
    return "";
}

static int gb_fx_origin_of(int who)
{
    return (who == 0) ? GB_FXO_PLAYER : ((who > 0) ? GB_FXO_MONSTER : GB_FXO_OTHER);
}

/*! 直前の投射の要約（反射の見分け用。反射は `project()` の中で入れ子に呼ばれるので、記録の並びで見分ける）。 */
static struct {
    int valid;
    int reflectable;
    int grids;
    int typ;
    int impact_y, impact_x;
    int flg;
} gb_fx_prev;

void gb_fx_note_projection(int who, int y1, int x1, int by, int bx, int typ, int rad, int flg, int blind, int breath, int jump,
    const u16b *path_g, int path_n, const byte *gy, const byte *gx, int grids)
{
    static int path[GB_FX_CELLS * 2];
    static int area[GB_FX_CELLS * 3];
    int shape;
    int flags = 0;
    int pn = 0;
    int an = 0;
    int i;
    int impact_y = by;
    int impact_x = bx;
    const int hidden = (flg & PROJECT_HIDE) ? 1 : 0;
    const int origin = gb_fx_origin_of(who);

    if (breath) {
        shape = GB_FXS_BREATH;
    } else if ((flg & PROJECT_BEAM) || (flg & PROJECT_MASTERSPARK)) {
        shape = GB_FXS_BEAM;
    } else if (jump || (path_n <= 0) || ((y1 == by) && (x1 == bx))) {
        shape = GB_FXS_JUMP;
    } else if (rad > 0) {
        shape = GB_FXS_BALL;
    } else {
        shape = GB_FXS_BOLT;
    }

    /* 経路：始点の次のマスから着弾点まで（ボルト・ボールは `path_n` が着弾点で切ってある。ビーム・ブレスは全部） */
    if (shape != GB_FXS_JUMP) {
        for (i = 0; (i < path_n) && (pn < GB_FX_CELLS); i++) {
            path[pn * 2] = GRID_Y(path_g[i]);
            path[pn * 2 + 1] = GRID_X(path_g[i]);
            pn++;
        }
        if ((shape == GB_FXS_BEAM) && (pn > 0)) {
            impact_y = path[(pn - 1) * 2];
            impact_x = path[(pn - 1) * 2 + 1];
        }
    }

    /* 範囲：プレイヤの視界内のマスだけ（隠す投射は全部）。距離は中心（ブレスは撃った者）から。ビームは 0。 */
    for (i = 0; (i < grids) && (an < GB_FX_CELLS); i++) {
        const int y = gy[i];
        const int x = gx[i];
        int d = 0;

        /* ビームの範囲の先頭には撃った者のマスが入っている（原作の「ビームのマスを集める」の最初の 1 マス）。経路に無いので数えない */
        if ((shape == GB_FXS_BEAM) && (y == y1) && (x == x1)) {
            continue;
        }
        if (!hidden && !player_has_los_bold(y, x)) {
            continue;
        }
        if (shape == GB_FXS_BREATH) {
            d = distance(y1, x1, y, x);
        } else if (shape != GB_FXS_BEAM) {
            d = distance(by, bx, y, x);
        }
        area[an * 3] = (d < 0) ? 0 : ((d > 255) ? 255 : d);
        area[an * 3 + 1] = y;
        area[an * 3 + 2] = x;
        an++;
    }

    if (hidden) {
        flags |= GB_FXF_HIDDEN;
    }
    if (blind) {
        flags |= GB_FXF_BLIND;
    }
    /* 視界内の全員（`project_hack()`）：プレイヤが飛び先指定・当てる・隠す、で半径 0 */
    if ((who == 0) && jump && (rad == 0) && !breath && (flg == (PROJECT_KILL | PROJECT_HIDE))) {
        flags |= GB_FXF_ALL_IN_SIGHT;
    }
    /* 反射：直前が反射できる 1 マスの投射で、そのマスからモンスターが同じ属性を撃ち返した */
    if (gb_fx_prev.valid && gb_fx_prev.reflectable && (gb_fx_prev.grids <= 1) && (origin == GB_FXO_MONSTER) && (rad == 0)
        && (typ == gb_fx_prev.typ) && (y1 == gb_fx_prev.impact_y) && (x1 == gb_fx_prev.impact_x)
        && ((flg & ~PROJECT_PLAYER) == (gb_fx_prev.flg & ~PROJECT_PLAYER))) {
        flags |= GB_FXF_REFLECTED;
    }
    gb_fx_prev.valid = 1;
    gb_fx_prev.reflectable = (flg & PROJECT_REFLECTABLE) ? 1 : 0;
    gb_fx_prev.grids = grids;
    gb_fx_prev.typ = typ;
    gb_fx_prev.impact_y = impact_y;
    gb_fx_prev.impact_x = impact_x;
    gb_fx_prev.flg = flg;

    gb_named_fx_projection(origin, shape, gb_fx_gf_name(typ), rad, y1, x1, impact_y, impact_x, flags, path, pn, area, an);
}

/*! シーカーレイ・スーパーレイ：`project()` が途中で戻るので、最初の経路だけを太いビームとして記録する。 */
void gb_fx_note_ray(int who, int y1, int x1, int typ, int flg, int blind, int jump, const u16b *path_g, int path_n)
{
    static int path[GB_FX_CELLS * 2];
    static int area[GB_FX_CELLS * 3];
    int pn = 0;
    int an = 0;
    int i;
    int impact_y = y1;
    int impact_x = x1;
    const int hidden = (flg & PROJECT_HIDE) ? 1 : 0;

    (void)jump;
    for (i = 0; (i < path_n) && (pn < GB_FX_CELLS); i++) {
        const int y = GRID_Y(path_g[i]);
        const int x = GRID_X(path_g[i]);

        path[pn * 2] = y;
        path[pn * 2 + 1] = x;
        pn++;
        impact_y = y;
        impact_x = x;
        if (hidden || player_has_los_bold(y, x)) {
            area[an * 3] = 0;
            area[an * 3 + 1] = y;
            area[an * 3 + 2] = x;
            an++;
        }
    }
    gb_named_fx_projection(gb_fx_origin_of(who), GB_FXS_BEAM, gb_fx_gf_name(typ), 0, y1, x1, impact_y, impact_x,
        (hidden ? GB_FXF_HIDDEN : 0) | (blind ? GB_FXF_BLIND : 0), path, pn, area, an);
}

/*!
 * 視界内の全員に別々の属性をかける魔法（`project_hack2()`。嵐のアンサンブル・金閣寺・各種の全体攻撃）。`project()` を通らないので、
 * 視界内の判定の配列が出そろった所で 1 件だけ記録する。画面側は画面全体の光の波にまとめる。
 */
void gb_fx_note_all_sight(int typ_base, bool flag_in_sight[MAX_HGT][MAX_WID])
{
    static int area[GB_FX_CELLS * 3];
    int an = 0;
    int y;
    int x;

    for (y = 1; (y < cur_hgt - 1) && (an < GB_FX_CELLS); y++) {
        for (x = 1; (x < cur_wid - 1) && (an < GB_FX_CELLS); x++) {
            int d;

            if (!flag_in_sight[y][x]) {
                continue;
            }
            d = distance(py, px, y, x);
            area[an * 3] = (d < 0) ? 0 : ((d > 255) ? 255 : d);
            area[an * 3 + 1] = y;
            area[an * 3 + 2] = x;
            an++;
        }
    }
    gb_named_fx_projection(GB_FXO_PLAYER, GB_FXS_JUMP, gb_fx_gf_name(typ_base), 0, py, px, py, px,
        GB_FXF_HIDDEN | GB_FXF_ALL_IN_SIGHT | (p_ptr->blind ? GB_FXF_BLIND : 0), NULL, 0, area, an);
}

/*! 飛ばした物の基本アイテムの番号。名前を知らない物（正体の分からないアイテム）は正体を漏らさないので 0。 */
static int gb_fx_item_id(const object_type *o_ptr)
{
    if (!o_ptr || !o_ptr->k_idx) {
        return 0;
    }
    if (gb_unaware_category(o_ptr) != 0) {
        return 0;
    }
    return o_ptr->k_idx;
}

/*!
 * 矢弾の属性：**識別済みのものだけ**（原作より情報を増やさない）。プレイヤ自身が選んだスナイパーの技（`snipe_type`）と、
 * 矢弾・弓の識別済みの旗（`object_flags_known`）から決める。理力は原作と同じ MP の条件（`csp > msp / 30`）を満たすときだけ。
 */
static const char *gb_fx_known_brand(const object_type *ammo, const object_type *launcher)
{
    u32b fa[TR_FLAG_SIZE];
    u32b fl[TR_FLAG_SIZE];

    if (snipe_type == SP_LITE) {
        return "lite";
    }
    if (snipe_type == SP_FIRE) {
        return "fire";
    }
    if (snipe_type == SP_COLD) {
        return "cold";
    }
    if (snipe_type == SP_ELEC) {
        return "elec";
    }
    if (snipe_type == SP_KILL_WALL) {
        return "kill_wall";
    }
    if (snipe_type == SP_FINAL) {
        return "mana";
    }
    object_flags_known((object_type *)ammo, fa);
    object_flags_known((object_type *)launcher, fl);
#define GB_FX_KNOWN(flag) (have_flag(fa, (flag)) || have_flag(fl, (flag)))
    if (GB_FX_KNOWN(TR_BRAND_FIRE)) {
        return "fire";
    }
    if (GB_FX_KNOWN(TR_BRAND_COLD)) {
        return "cold";
    }
    if (GB_FX_KNOWN(TR_BRAND_ELEC)) {
        return "elec";
    }
    if (GB_FX_KNOWN(TR_BRAND_ACID)) {
        return "acid";
    }
    if (GB_FX_KNOWN(TR_BRAND_POIS)) {
        return "pois";
    }
    if (GB_FX_KNOWN(TR_FORCE_WEAPON) && (p_ptr->csp > (p_ptr->msp / 30))) {
        return "mana";
    }
#undef GB_FX_KNOWN
    return "";
}

/*! 射撃 1 歩（`do_cmd_fire_aux()`）。`cur_dis == 1` が 1 発の始まり。盲目のときは記録しない（原作も見えないマスの動きは描かない）。 */
void gb_fx_shot(int cur_dis, int y, int x, int prev_y, int prev_x, const object_type *ammo, const object_type *launcher)
{
    int kind = GB_FXM_ARROW;

    if (p_ptr->blind || !ammo) {
        return;
    }
    if (ammo->tval == TV_BOLT) {
        kind = GB_FXM_BOLT;
    }
    gb_named_fx_missile_step((cur_dis <= 1) ? 1 : 0, kind, gb_fx_item_id(ammo), (cur_dis <= 1) ? gb_fx_known_brand(ammo, launcher) : "",
        prev_y, prev_x, y, x);
}

/*! 投擲 1 歩（`do_cmd_throw_aux()`）。 */
void gb_fx_throw(int cur_dis, int y, int x, int prev_y, int prev_x, const object_type *obj)
{
    if (p_ptr->blind || !obj) {
        return;
    }
    gb_named_fx_missile_step((cur_dis <= 1) ? 1 : 0, GB_FXM_THROWN, gb_fx_item_id(obj), "", prev_y, prev_x, y, x);
}

/*! 射撃・投擲が相手に当たった。 */
void gb_fx_hit(void)
{
    if (p_ptr->blind) {
        return;
    }
    gb_named_fx_missile_hit();
}

/*! 投げた物が戻ってくる（ブーメランなど。原作が戻りの動きを描く所）。 */
void gb_fx_throw_return(void)
{
    if (p_ptr->blind) {
        return;
    }
    gb_named_fx_missile_returns();
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

int gb_screen_depth(void)
{
    return (int)character_icky;
}

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
 * @brief 位置の確認（`L`。原作の `do_cmd_locate()`）の最中なら、見ている区画の中心を返す（修-64）。
 *
 * @details 原作はこの間 `panel_row_min`/`panel_col_min`（見ている区画）だけを動かし、`px`/`py`（自キャラ）は
 * 動かさない。行 0 は「Map sector [..] .. Direction?」（日本語は「マップ位置 [..] ..  方向?」。SJIS）を出して
 * 方向を待つ。この目印で見分ける（原作は 1 バイトも変えない。Angband の `an_locate_active()` と同じ手）。
 * 区画の大きさは `do_cmd_locate()` と同じ `get_screen_size()` で取る。
 * @return 最中なら 1（`*cx`/`*cy` に区画の中心のマス）。そうでなければ 0。
 */
int gb_locate_panel_center(int *cx, int *cy)
{
    static const char en[] = "Map sector";
    static const char jp[] = "\x83\x7d\x83\x62\x83\x76\x88\xca\x92\x75"; /* マップ位置（SJIS） */
    char row0[32];
    int wid = 0;
    int hgt = 0;

    if (!character_generated || !character_dungeon) {
        return 0;
    }
    if (gb_term_row(0, row0, NULL, (int)sizeof(row0)) <= 0) {
        return 0;
    }
    if ((strncmp(row0, en, sizeof(en) - 1) != 0) && (strncmp(row0, jp, sizeof(jp) - 1) != 0)) {
        return 0;
    }
    get_screen_size(&wid, &hgt);
    if (cx) {
        *cx = (int)panel_col_min + (wid / 2);
    }
    if (cy) {
        *cy = (int)panel_row_min + (hgt / 2);
    }
    return 1;
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

/*!
 * @brief 光源の枠の物から光の種類を決める（`LightingState::light_kind`）。
 * @param o_ptr 光源の枠（`INVEN_LITE`）の物。使えない枠は NULL
 * @param cur_lite 光の半径（`p_ptr->cur_lite`）
 * @return 0 = 光が無い、1 = 松明、2 = ランタン、3 = アーティファクトの光源、4 = 装備によらない光
 * @details 原作の `calc_torch()` と同じ見方をする。松明・ランタンは燃料切れなら半径に足されないので、
 * 半径が残っていれば装備によらない光（4）。**行灯はランタンの見せ方（2）、宝塔・SANAGI・DRAGON などは
 * アーティファクトの見せ方（3）**。
 * 行灯は固定アーティファクトの土台でもあるので、アーティファクトの判定より先に見る。
 * 読むだけでコアの状態は変えない。
 */
static int gb_light_kind_of(object_type *o_ptr, int cur_lite)
{
    if (cur_lite <= 0) {
        return 0;
    }
    if (!o_ptr || !o_ptr->k_idx || (o_ptr->tval != TV_LITE)) {
        return 4;
    }
    if (o_ptr->name2 == EGO_LITE_DARKNESS) {
        return 4; /* 暗黒の光源は照らさない。残っている半径は別の光 */
    }
    if (o_ptr->sval == SV_LITE_ANDON) {
        return 2;
    }
    if (object_is_artifact(o_ptr)) {
        return 3;
    }
    if (o_ptr->sval == SV_LITE_TORCH) {
        return (o_ptr->xtra4 > 0) ? 1 : 4;
    }
    if (o_ptr->sval == SV_LITE_LANTERN) {
        return (o_ptr->xtra4 > 0) ? 2 : 4;
    }
    if (o_ptr->sval == SV_LITE_FEANOR) {
        return 2;
    }
    return 3;
}

/*! @brief いまのプレイヤの光源の種類。光源の枠が使えない状態（`check_invalidate_inventory`）は枠が空と同じ。 */
static int gb_player_light_kind(void)
{
    object_type *o_ptr = &inventory[INVEN_LITE];

    if (check_invalidate_inventory(INVEN_LITE)) {
        o_ptr = NULL;
    }
    return gb_light_kind_of(o_ptr, (int)p_ptr->cur_lite);
}

/*! @brief `gb_light_selftest` の 1 件。作った物（`tval`/`sval`/`name1`/`name2`/`xtra4`）の判定が `want` か。 */
static void gb_light_case(const char *name, int tval, int sval, int name1, int name2, int xtra4,
    int cur_lite, int want, char *err, int cap, int *fails)
{
    object_type forge;
    s16b k_idx = lookup_kind(tval, sval);
    int got;

    if (!k_idx) {
        got = -1;
    } else {
        object_prep(&forge, k_idx);
        forge.name1 = (byte)name1;
        forge.name2 = (byte)name2;
        forge.xtra4 = (s16b)xtra4;
        got = gb_light_kind_of(&forge, cur_lite);
    }
    if (got != want) {
        if (*fails == 0 && err && cap > 1) {
            sprintf(err, "%.60s (want %d, got %d)", name, want, got);
            err[cap - 1] = '\0';
        }
        (*fails)++;
    }
}

int gb_light_selftest(char *err, int cap)
{
    int fails = 0;
    object_type empty;

    if (err && cap > 0) {
        err[0] = '\0';
    }

    /* 光が無い */
    gb_light_case("torch, radius 0", TV_LITE, SV_LITE_TORCH, 0, 0, 1000, 0, 0, err, cap, &fails);
    if (gb_light_kind_of(NULL, 0) != 0) {
        fails++;
    }
    /* 枠が空・光源でない物でも半径が残っていれば装備によらない光 */
    if (gb_light_kind_of(NULL, 1) != 4) {
        if (fails == 0 && err && cap > 1) {
            sprintf(err, "empty slot (want 4)");
        }
        fails++;
    }
    memset(&empty, 0, sizeof(empty));
    if (gb_light_kind_of(&empty, 2) != 4) {
        if (fails == 0 && err && cap > 1) {
            sprintf(err, "k_idx 0 (want 4)");
        }
        fails++;
    }
    gb_light_case("ring in the light slot", TV_RING, SV_RING_AGGRAVATION, 0, 0, 0, 1, 4, err, cap, &fails);
    /* 松明・ランタン（燃料の有無） */
    gb_light_case("torch with fuel", TV_LITE, SV_LITE_TORCH, 0, 0, 1000, 1, 1, err, cap, &fails);
    gb_light_case("torch without fuel", TV_LITE, SV_LITE_TORCH, 0, 0, 0, 1, 4, err, cap, &fails);
    gb_light_case("lantern with fuel", TV_LITE, SV_LITE_LANTERN, 0, 0, 1000, 2, 2, err, cap, &fails);
    gb_light_case("lantern without fuel", TV_LITE, SV_LITE_LANTERN, 0, 0, 0, 2, 4, err, cap, &fails);
    gb_light_case("magic lantern (Feanor)", TV_LITE, SV_LITE_FEANOR, 0, 0, 0, 2, 2, err, cap, &fails);
    gb_light_case("darkness torch", TV_LITE, SV_LITE_TORCH, 0, EGO_LITE_DARKNESS, 1000, 1, 4, err, cap, &fails);
    /* 行灯はランタンの見せ方（固定アーティファクトの土台でも 2） */
    gb_light_case("andon", TV_LITE, SV_LITE_ANDON, 0, 0, 0, 3, 2, err, cap, &fails);
    gb_light_case("andon (artifact)", TV_LITE, SV_LITE_ANDON, 1, 0, 0, 3, 2, err, cap, &fails);
    /* アーティファクトの見せ方 */
    gb_light_case("houtou", TV_LITE, SV_LITE_HOUTOU, ART_HOUTOU, 0, 0, 3, 3, err, cap, &fails);
    gb_light_case("sanagi", TV_LITE, SV_LITE_SANAGI, 0, 0, 0, 3, 3, err, cap, &fails);
    gb_light_case("dragon", TV_LITE, SV_LITE_DRAGON, 0, 0, 0, 3, 3, err, cap, &fails);
    gb_light_case("artifact torch", TV_LITE, SV_LITE_TORCH, ART_CLOWNPIECE, 0, 1000, 3, 3, err, cap, &fails);
    return fails;
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
    if (have_flag(f_ptr->flags, FF_SHAFT)) {
        bits |= GB_FEAT_SHAFT;
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

/*!
 * @brief 正体の分からないアイテムの分類（1..8）を返す。0 = 対象外。
 * @details。
 * 「正体そのものが分からない」の判定は `k_info[].flavor != 0`（未鑑定名の有無。
 * 変愚の `is_apparent()` に当たる）と `k_info[].aware`（鑑定済みか。変愚の
 * `is_aware()` に当たる）。分類は `k_info[].tval`（幻想蛮怒独自の番号。
 * `TV_RING=47`・`TV_AMULET=48` など、変愚/Frox とは値が違うので流用しない）。
 * 番号は `unaware_item_categories.h`（C からも読める）と
 * `presentation::UnawareItemCategory`（`unaware_item_tiles.h`）の 2 つに `static_assert`
 * で突き合わせてある。**`gb_native_store.c` と共有する**（宣言は `gb_unaware_category.h`）
 * ——同じ判定を 2 か所に書かない（契約 §4）。
 */
unsigned char gb_unaware_category(const object_type *o_ptr)
{
    object_kind *k_ptr;
    if (!o_ptr->k_idx) {
        return 0;
    }
    k_ptr = &k_info[o_ptr->k_idx];
    if (!k_ptr->flavor || k_ptr->aware) {
        return 0; /* 未鑑定名が無い（武器・防具の類）か、すでに鑑定済み */
    }
    switch (k_ptr->tval) {
    case TV_POTION:
        return UNAWARE_CATEGORY_POTION;
    case TV_SCROLL:
        return UNAWARE_CATEGORY_SCROLL;
    case TV_RING:
        return UNAWARE_CATEGORY_RING;
    case TV_AMULET:
        return UNAWARE_CATEGORY_AMULET;
    case TV_WAND:
        return UNAWARE_CATEGORY_WAND;
    case TV_STAFF:
        return UNAWARE_CATEGORY_STAFF;
    case TV_ROD:
        return UNAWARE_CATEGORY_ROD;
    case TV_MUSHROOM:
        /* 幻想蛮怒はキノコが `TV_FOOD` と別の tval（`flavor.c:348` の `shuffle_flavors(TV_MUSHROOM)`）。
         * 変愚・Frox の「FOOD のうちキノコだけ」とは形が違うが、分類としては同じ「キノコ」。 */
        return UNAWARE_CATEGORY_MUSHROOM;
    default:
        return 0; /* Sil-Q の角笛のような 8 分類の外。幻想蛮怒には今のところ無い */
    }
}

static int gb_read_map_raw(int x0, int y0, int w, int h, gb_map_cell *out, int cap)
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
            /* プレイヤの光源で照らされているマス（`update_lite()` が立てる）。 */
            cell->lit_by_player = (c_ptr->info & CAVE_LITE) ? 1 : 0;

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
                        cell->object_unaware_category = gb_unaware_category(o_ptr);
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

int gb_read_map(int x0, int y0, int w, int h, gb_map_cell *out, int cap)
{
    gb_rng_snapshot snap;
    int result;

    gb_rng_save(&snap);
    gb_test_force_image();
    result = gb_read_map_raw(x0, y0, w, h, out, cap);
    gb_rng_finish(&snap, "gb_read_map");
    return result;
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
                } else if (have_flag(f_ptr->flags, FF_WALL) || have_flag(f_ptr->flags, FF_ICE_WALL)) {
                    /* 氷壁（240。町 4 の紅魔館）は WALL の旗が無く（CAN_PASS の瓦礫扱い）床で送られていたが、見た目は塀なので壁で送る（修-27）。
                     * 町の読み取りとクリック移動の経路が壁として扱う（町 4 では東の山と地続きなので岩山に読まれ、氷塊の絵にはならない）。通れるかどうかの規則は変えない（ミニマップの種別は表示と経路の目安だけ）。 */
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
    out->light_kind = gb_player_light_kind();

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

/* Report only the core command input boundary, never a nested prompt. */
int gb_awaiting_command(void)
{
    return character_generated && !p_ptr->is_dead && inkey_flag;
}

/* ============================================================ 状態のアイコン（`GameFrame::status_effects`） */

/*
 * の幻想蛮怒版。
 *
 * ## 取り方
 * - **一時的な状態**は、原作の `prt_status()`（`xtra1.c`。static）が作る旗の配列 `bar_flags` から取る。
 *   `tools/targeting/overlay.py` が `prt_status()` の「長さを数える」の手前に 1 行足し、立っている旗の
 *   番号・名札（原作が画面へ出す字。言語の切り替えを含む）・色を `gb_status_record()` へ渡す。
 *   記録するだけで、旗にも画面にも乱数にも触れない。
 * - 速さ・朦朧・切り傷・空腹は、原作の左の状態欄（`prt_speed`・`prt_stun`・`prt_cut`・`prt_hunger`）と同じ
 *   しきい値・同じ字で、`p_ptr` を直に読む。
 * - **常時の効果**は C 画面（`files.c` の `display_player_flag_info` ほか）の表と同じ判定。
 *   **鑑定済みの旗だけ**（`object_flags_known`）と、種族・職業などの固有の旗（`player_flags` など。static なので
 *   overlay.py が `files.c` の末尾へ足す読み取りの関数 `gb_native_player_flags` から取る）。一時的な効果（画面の `#`）は載せない。
 *
 * ## 種類（`StatusEffectKind`）
 * 0 良い・1 悪い・2 待っている・3 特殊・4 常時の良い・5 常時の悪い。幻想蛮怒の種類の直し
 * （冬眠・加重・財禍・忘却・生命爆発は良い、停止は特殊）はこの表に入っている。
 */

#define GB_BAR_COUNT 121 /* xtra1.c の BAR_ARIYA_STOP（120）+ 1。tools/gensoband/gb_status_table_check.py が原作と突き合わせる */

#define GB_SK_GOOD 0
#define GB_SK_BAD 1
#define GB_SK_WAIT 2
#define GB_SK_SPECIAL 3
#define GB_SK_PERM 4
#define GB_SK_PERM_BAD 5

typedef struct gb_bar_icon {
    const char *key;
    unsigned char kind;
} gb_bar_icon;

/*! `BAR_*` の番号で引く。鍵は `hd2d/ui/status_icon_table.inc` にあるものだけ。 */
static const gb_bar_icon gb_bar_icons[GB_BAR_COUNT] = {
    /*   0 */ { "tsuyoshi", GB_SK_SPECIAL }, /* BAR_TSUYOSHI */
    /*   1 */ { "hallucination", GB_SK_BAD }, /* BAR_HALLUCINATION */
    /*   2 */ { "blind", GB_SK_BAD }, /* BAR_BLINDNESS */
    /*   3 */ { "paralyzed", GB_SK_BAD }, /* BAR_PARALYZE */
    /*   4 */ { "confused", GB_SK_BAD }, /* BAR_CONFUSE */
    /*   5 */ { "poisoned", GB_SK_BAD }, /* BAR_POISONED */
    /*   6 */ { "afraid", GB_SK_BAD }, /* BAR_AFRAID */
    /*   7 */ { "levitate", GB_SK_GOOD }, /* BAR_LEVITATE */
    /*   8 */ { "reflect", GB_SK_GOOD }, /* BAR_REFLECTION */
    /*   9 */ { "passwall", GB_SK_GOOD }, /* BAR_PASSWALL */
    /*  10 */ { "wraith", GB_SK_GOOD }, /* BAR_WRAITH */
    /*  11 */ { "protevil", GB_SK_GOOD }, /* BAR_PROTEVIL */
    /*  12 */ { "kawarimi", GB_SK_GOOD }, /* BAR_KAWARIMI */
    /*  13 */ { "magicdef", GB_SK_GOOD }, /* BAR_MAGICDEFENSE */
    /*  14 */ { "gb_death_aura", GB_SK_GOOD }, /* BAR_EXPAND（幻想蛮怒では死霊オーラ） */
    /*  15 */ { "stoneskin", GB_SK_GOOD }, /* BAR_STONESKIN（物理防御） */
    /*  16 */ { "multishadow", GB_SK_GOOD }, /* BAR_MULTISHADOW */
    /*  17 */ { "resmagic", GB_SK_GOOD }, /* BAR_REGMAGIC */
    /*  18 */ { "ultimate", GB_SK_GOOD }, /* BAR_ULTIMATE */
    /*  19 */ { "invuln", GB_SK_GOOD }, /* BAR_INVULN */
    /*  20 */ { "imm_acid", GB_SK_GOOD }, /* BAR_IMMACID */
    /*  21 */ { "res_acid", GB_SK_GOOD }, /* BAR_RESACID */
    /*  22 */ { "imm_elec", GB_SK_GOOD }, /* BAR_IMMELEC */
    /*  23 */ { "res_elec", GB_SK_GOOD }, /* BAR_RESELEC */
    /*  24 */ { "imm_fire", GB_SK_GOOD }, /* BAR_IMMFIRE */
    /*  25 */ { "res_fire", GB_SK_GOOD }, /* BAR_RESFIRE */
    /*  26 */ { "imm_cold", GB_SK_GOOD }, /* BAR_IMMCOLD */
    /*  27 */ { "res_cold", GB_SK_GOOD }, /* BAR_RESCOLD */
    /*  28 */ { "res_pois", GB_SK_GOOD }, /* BAR_RESPOIS */
    /*  29 */ { "res_neth", GB_SK_GOOD }, /* BAR_RESNETH */
    /*  30 */ { "res_time", GB_SK_GOOD }, /* BAR_RESTIME */
    /*  31 */ { "dustrobe", GB_SK_GOOD }, /* BAR_DUSTROBE */
    /*  32 */ { "sh_fire", GB_SK_GOOD }, /* BAR_SHFIRE */
    /*  33 */ { "gb_barrier_wave", GB_SK_GOOD }, /* BAR_EVIL_UNDULATION（障壁波動） */
    /*  34 */ { "sh_holy", GB_SK_GOOD }, /* BAR_SHHOLY */
    /*  35 */ { "eyeeye", GB_SK_GOOD }, /* BAR_EYEEYE */
    /*  36 */ { "blessed", GB_SK_GOOD }, /* BAR_BLESSED */
    /*  37 */ { "hero", GB_SK_GOOD }, /* BAR_HEROISM */
    /*  38 */ { "berserk", GB_SK_GOOD }, /* BAR_BERSERK */
    /*  39 */ { "attk_fire", GB_SK_GOOD }, /* BAR_ATTKFIRE */
    /*  40 */ { "attk_cold", GB_SK_GOOD }, /* BAR_ATTKCOLD */
    /*  41 */ { "attk_elec", GB_SK_GOOD }, /* BAR_ATTKELEC */
    /*  42 */ { "attk_acid", GB_SK_GOOD }, /* BAR_ATTKACID */
    /*  43 */ { "attk_pois", GB_SK_GOOD }, /* BAR_ATTKPOIS */
    /*  44 */ { "attk_conf", GB_SK_GOOD }, /* BAR_ATTKCONF */
    /*  45 */ { "senseunseen", GB_SK_GOOD }, /* BAR_SENSEUNSEEN */
    /*  46 */ { "telepathy", GB_SK_GOOD }, /* BAR_TELEPATHY */
    /*  47 */ { "regeneration", GB_SK_GOOD }, /* BAR_REGENERATION */
    /*  48 */ { "infravision", GB_SK_GOOD }, /* BAR_INFRAVISION */
    /*  49 */ { "stealth", GB_SK_GOOD }, /* BAR_STEALTH */
    /*  50 */ { "superstealth", GB_SK_GOOD }, /* BAR_SUPERSTEALTH */
    /*  51 */ { "recall", GB_SK_WAIT }, /* BAR_RECALL */
    /*  52 */ { "alter", GB_SK_WAIT }, /* BAR_ALTER */
    /*  53 */ { "gb_hibernation", GB_SK_GOOD }, /* BAR_HIBERNATION（冬眠。 で良い） */
    /*  54 */ { "gb_silence", GB_SK_BAD }, /* BAR_SILENT */
    /*  55 */ { "gb_silence", GB_SK_BAD }, /* BAR_SILENT2 */
    /*  56 */ { "resmagic", GB_SK_GOOD }, /* BAR_MAG_ABSORB（魔力吸収） */
    /*  57 */ { "gb_sakuya_world", GB_SK_GOOD }, /* BAR_SAKUYA */
    /*  58 */ { "gb_hina_doll", GB_SK_SPECIAL }, /* BAR_HINA */
    /*  59 */ { "gb_boundary", GB_SK_GOOD }, /* BAR_YUKARI（境界操作） */
    /*  60 */ { "superstealth", GB_SK_GOOD }, /* BAR_OPTICAL（光学迷彩） */
    /*  61 */ { "gb_special_immunity", GB_SK_GOOD }, /* BAR_DANGO_IMM */
    /*  62 */ { "gb_mist_wheel", GB_SK_GOOD }, /* BAR_EIBON */
    /*  63 */ { "gb_crown_shots", GB_SK_GOOD }, /* BAR_SAGUME */
    /*  64 */ { "gb_orbit_trap", GB_SK_SPECIAL }, /* BAR_ORBITAL_TRAP */
    /*  65 */ { "reflect", GB_SK_GOOD }, /* BAR_HECATIA_MIRROR（反射鏡） */
    /*  66 */ { "gb_nameless_art", GB_SK_GOOD }, /* BAR_JUNKO_TIME_SKILL */
    /*  67 */ { "res_water", GB_SK_GOOD }, /* BAR_RESWATER */
    /*  68 */ { "wraith", GB_SK_GOOD }, /* BAR_DARKSIDEOFTHEMOON（ルーミアの幽体） */
    /*  69 */ { "res_chaos", GB_SK_GOOD }, /* BAR_RESCHAO */
    /*  70 */ { "antitele", GB_SK_GOOD }, /* BAR_NENNBAKU（念縛。自分と周りの敵のテレポートを妨げる） */
    /*  71 */ { "buildup", GB_SK_GOOD }, /* BAR_EXSTAT（強化） */
    /*  72 */ { "cure", GB_SK_GOOD }, /* BAR_REACTIVE_HEAL（反応回復） */
    /*  73 */ { "gb_radar", GB_SK_GOOD }, /* BAR_RADAR */
    /*  74 */ { "runesword", GB_SK_GOOD }, /* BAR_EVILWEAPON（魔剣） */
    /*  75 */ { "res_dark", GB_SK_GOOD }, /* BAR_RESDARK */
    /*  76 */ { "gb_unite_dark", GB_SK_GOOD }, /* BAR_UNITE_DARK */
    /*  77 */ { "gb_lucky", GB_SK_GOOD }, /* BAR_LUCKY */
    /*  78 */ { "gb_foresight", GB_SK_GOOD }, /* BAR_FORESIGHT */
    /*  79 */ { "gb_deportation", GB_SK_GOOD }, /* BAR_DEPORTATION */
    /*  80 */ { "gb_claw_extend", GB_SK_GOOD }, /* BAR_CLAWEXTEND */
    /*  81 */ { "gb_speedster", GB_SK_GOOD }, /* BAR_SPEEDSTER（高速移動） */
    /*  82 */ { "force_weapon", GB_SK_GOOD }, /* BAR_FORCEWEAPON */
    /*  83 */ { "gb_res_insanity", GB_SK_GOOD }, /* BAR_RES_INSANITY */
    /*  84 */ { "gb_kamioroshi", GB_SK_GOOD }, /* BAR_KAMIOROSHI */
    /*  85 */ { "gb_blade_foresight", GB_SK_GOOD }, /* BAR_ATTACK_FORE（予見魔剣） */
    /*  86 */ { "gb_blade_warp", GB_SK_GOOD }, /* BAR_ATTACK_UNSUMMON（歪曲魔剣） */
    /*  87 */ { "brand_magic", GB_SK_GOOD }, /* BAR_ATTACK_INC_DICES（強化魔剣） */
    /*  88 */ { "chaotic", GB_SK_GOOD }, /* BAR_ATTACK_CHAOS（混沌魔剣） */
    /*  89 */ { "might", GB_SK_GOOD }, /* BAR_KAIOUKEN（界王拳） */
    /*  90 */ { "gb_aim", GB_SK_GOOD }, /* BAR_HEAD_SHOT（照準） */
    /*  91 */ { "magicdef", GB_SK_GOOD }, /* BAR_MANA_SHIELD（結界ガード） */
    /*  92 */ { "vampiric", GB_SK_GOOD }, /* BAR_ROB_MANA（魔力奪取） */
    /*  93 */ { "stoneskin", GB_SK_GOOD }, /* BAR_STATUE_FORM（石像化） */
    /*  94 */ { "gb_salvation", GB_SK_GOOD }, /* BAR_HELLFIRE_BARRIER（救済） */
    /*  95 */ { "vorpal", GB_SK_GOOD }, /* BAR_ATTACK_VORPAL（切れ味） */
    /*  96 */ { "gb_squander", GB_SK_SPECIAL }, /* BAR_WASTE_MONEY */
    /*  97 */ { "gb_wealth_curse", GB_SK_GOOD }, /* BAR_PLUCK_PIGEON（財禍。 で良い） */
    /*  98 */ { "gb_back_door", GB_SK_GOOD }, /* BAR_BACKDOOR_POWER */
    /*  99 */ { "gb_spellcard", GB_SK_SPECIAL }, /* BAR_SPELLCARD */
    /* 100 */ { "gb_muen", GB_SK_SPECIAL }, /* BAR_MUEN */
    /* 101 */ { "gb_heavy", GB_SK_GOOD }, /* BAR_HEAVY（加重。§2.5 で良い） */
    /* 102 */ { "gb_bribe", GB_SK_SPECIAL }, /* BAR_YACHIE_BRIBE */
    /* 103 */ { "gb_moth_lamp", GB_SK_SPECIAL }, /* BAR_YUYUKO_YUUGA */
    /* 104 */ { "gb_drunken_fist", GB_SK_GOOD }, /* BAR_SUIKEN */
    /* 105 */ { "gb_pierce_cold", GB_SK_GOOD }, /* BAR_LETTY_POWERUP */
    /* 106 */ { "superstealth", GB_SK_GOOD }, /* BAR_SUPERSTEALTH_FOREST（超隠密(森)） */
    /* 107 */ { "gb_gluttony", GB_SK_SPECIAL }, /* BAR_MOMOYO_EAT */
    /* 108 */ { "aggravate", GB_SK_BAD }, /* BAR_TIM_AGGRAVATION（一時反感） */
    /* 109 */ { "gb_amplify_stone", GB_SK_GOOD }, /* BAR_AMPLIFY_STONE */
    /* 110 */ { "gb_counter", GB_SK_GOOD }, /* BAR_COUNTER */
    /* 111 */ { "gb_life_explode", GB_SK_GOOD }, /* BAR_LIFE_EXPLODE（生命爆発。§2.5 で良い） */
    /* 112 */ { "gb_hirari_cloth", GB_SK_GOOD }, /* BAR_HIRARINUNO */
    /* 113 */ { "gb_forgotten", GB_SK_GOOD }, /* BAR_MIYOI_FORGOTTEN（忘却。§2.5 で良い） */
    /* 114 */ { "gb_no_move", GB_SK_BAD }, /* BAR_NO_MOVE */
    /* 115 */ { "gb_transport_trap", GB_SK_SPECIAL }, /* BAR_TRANSPORTATION_TRAP */
    /* 116 */ { "gb_void", GB_SK_GOOD }, /* BAR_VOID */
    /* 117 */ { "gb_bees", GB_SK_GOOD }, /* BAR_BEES */
    /* 118 */ { "stealth", GB_SK_GOOD }, /* BAR_HIDE（潜伏） */
    /* 119 */ { "gb_blast_guard", GB_SK_GOOD }, /* BAR_RES_BLAST */
    /* 120 */ { "gb_halt", GB_SK_SPECIAL }, /* BAR_ARIYA_STOP（停止。§2.5 で特殊） */
};

/*! 表の組み方の外から見える数（自己検査と check スクリプト用）。 */
int gb_status_bar_table_count(void)
{
    return GB_BAR_COUNT;
}

const char *gb_status_bar_table_key(int bar, int *kind)
{
    if ((bar < 0) || (bar >= GB_BAR_COUNT)) {
        return NULL;
    }
    if (kind) {
        *kind = (int)gb_bar_icons[bar].kind;
    }
    return gb_bar_icons[bar].key;
}

/* ---------------------------------------------------------------- 記録（overlay.py が足した 1 行の呼び先） */

static unsigned char gb_status_on[GB_BAR_COUNT];
static char gb_status_label[GB_BAR_COUNT][48];
static unsigned char gb_status_attr[GB_BAR_COUNT];
static int gb_status_overflow; /* 原作の `bar[]` が表より長い（原作が版上げで増えた）ときの印 */
static unsigned long gb_status_hash;

/*!
 * @brief `prt_status()` が組んだ旗を覚える。`bar < 0` は「組み直しの始まり」（`attr` に原作の `bar[]` の行数）。
 * @details 名札は原作が画面へ出す字（CP932 か英語）。全角を割らないように切る。
 */
void gb_status_record(int bar, const char *label, int attr)
{
    if (bar < 0) {
        memset(gb_status_on, 0, sizeof(gb_status_on));
        gb_status_hash = 2166136261UL;
        gb_status_overflow = (attr > GB_BAR_COUNT) ? 1 : 0;
        return;
    }
    if (bar >= GB_BAR_COUNT) {
        gb_status_overflow = 1;
        return;
    }
    gb_status_on[bar] = 1;
    gb_status_attr[bar] = (unsigned char)attr;
    {
        int i = 0;
        const char *s = label ? label : "";
        while (s[i] && (i < (int)sizeof(gb_status_label[bar]) - 1)) {
            unsigned char c = (unsigned char)s[i];
            int lead = ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));
            if (lead && (!s[i + 1] || (i + 2 > (int)sizeof(gb_status_label[bar]) - 1))) {
                break;
            }
            gb_status_label[bar][i] = s[i];
            i++;
            if (lead) {
                gb_status_label[bar][i] = s[i];
                i++;
            }
        }
        gb_status_label[bar][i] = '\0';
    }
    gb_status_hash = (gb_status_hash ^ (unsigned long)(bar + 1)) * 16777619UL;
}

/*! 旗の組が変わったら変わる値（`gb_change_digest` に混ぜる）。 */
unsigned long gb_status_stamp(void)
{
    return gb_status_hash;
}

/* ---------------------------------------------------------------- 常時の効果の表 */

#define GB_PM_NORMAL 0 /* 装備の枠 12 + 固有の旗 */
#define GB_PM_WEAPON 1 /* 利き手・逆手 + 固有の旗（`DP_WP`） */
#define GB_PM_CURSE 2 /* 装備の呪い（`DP_CURSE`） */

typedef struct gb_perm_row {
    short flag; /*!< 耐性・能力の旗（呪いの行は -1） */
    short greater; /*!< 免疫・皆殺し・極みの旗（無ければ -1） */
    unsigned char mode;
    unsigned char kind;
    const char *key;
    const char *ja;
    const char *en;
    const char *imm_key; /*!< 免疫のときの鍵（無ければ NULL ＝ key のまま） */
    const char *imm_ja;
    const char *imm_en;
} gb_perm_row;

#define GBP(flag, key, ja, en) { flag, -1, GB_PM_NORMAL, GB_SK_PERM, key, ja, en, NULL, NULL, NULL }
#define GBP_BAD(flag, key, ja, en) { flag, -1, GB_PM_NORMAL, GB_SK_PERM_BAD, key, ja, en, NULL, NULL, NULL }
#define GBP_IMM(flag, gr, key, ja, en, ikey, ija, ien) { flag, gr, GB_PM_NORMAL, GB_SK_PERM, key, ja, en, ikey, ija, ien }
#define GBP_WP(flag, gr, key, ja, en) { flag, gr, GB_PM_WEAPON, GB_SK_PERM, key, ja, en, NULL, NULL, NULL }

/*!
 * C 画面（`display_player_flag_info` / `display_player_other_flag_info`）の行の順。
 * 描いていないアイコン（罠解除 `TR_DISARM`・魔法防御上昇 `TR_SAVING`・乱テレポート `TR_TELEPORT`）は載せない。
 */
static const gb_perm_row gb_perm_rows[] = {
    /* ---- ページ 1 ---- */
    GBP_IMM(TR_RES_ACID, TR_IM_ACID, "res_acid", "耐酸", "Resist Acid", "imm_acid", "酸免疫", "Immune Acid"),
    GBP_IMM(TR_RES_ELEC, TR_IM_ELEC, "res_elec", "耐電撃", "Resist Elec", "imm_elec", "電撃免疫", "Immune Elec"),
    GBP_IMM(TR_RES_FIRE, TR_IM_FIRE, "res_fire", "耐火炎", "Resist Fire", "imm_fire", "火炎免疫", "Immune Fire"),
    GBP_IMM(TR_RES_COLD, TR_IM_COLD, "res_cold", "耐冷気", "Resist Cold", "imm_cold", "冷気免疫", "Immune Cold"),
    GBP(TR_RES_POIS, "res_pois", "耐毒", "Resist Poison"),
    GBP(TR_RES_LITE, "res_lite", "耐閃光", "Resist Light"),
    GBP(TR_RES_DARK, "res_dark", "耐暗黒", "Resist Dark"),
    GBP(TR_RES_SHARDS, "res_shard", "耐破片", "Resist Shards"),
    GBP(TR_RES_WATER, "res_water", "耐水", "Resist Water"),
    GBP(TR_RES_HOLY, "gb_res_holy", "耐破邪", "Resist Holy"),
    /* ---- ページ 2 ---- */
    GBP(TR_RES_SOUND, "res_sound", "耐轟音", "Resist Sound"),
    GBP(TR_RES_NETHER, "res_neth", "耐地獄", "Resist Nether"),
    GBP(TR_RES_TIME, "res_time", "耐時空", "Resist Time"),
    GBP(TR_RES_CHAOS, "res_chaos", "耐混沌", "Resist Chaos"),
    GBP(TR_RES_DISEN, "res_disen", "耐劣化", "Resist Disen"),
    GBP(TR_RES_FEAR, "res_fear", "耐恐怖", "Resist Fear"),
    GBP(TR_FREE_ACT, "free_act", "耐麻痺", "Free Action"),
    GBP(TR_RES_CONF, "res_conf", "耐混乱", "Resist Conf"),
    GBP(TR_RES_BLIND, "res_blind", "耐盲目", "Resist Blind"),
    GBP(TR_RES_INSANITY, "gb_res_insanity", "耐狂気", "Resist Insanity"),
    /* ---- ページ 3 ---- */
    GBP(TR_SEE_INVIS, "senseunseen", "透明視認", "See Invisible"),
    GBP(TR_WARNING, "warning", "警告", "Warning"),
    GBP(TR_SLOW_DIGEST, "slow_digest", "栄養持続", "Slow Digest"),
    GBP(TR_REGEN, "regeneration", "急速回復", "Regeneration"),
    GBP(TR_LEVITATION, "levitate", "浮遊", "Levitation"),
    GBP(TR_SPEEDSTER, "gb_speedster", "高速移動", "Fast Move"),
    GBP_BAD(TR_NO_TELE, "no_tele", "反テレポ", "No Teleport"),
    GBP_BAD(TR_NO_MAGIC, "no_magic", "反魔法", "Anti-Magic"),
    GBP(TR_REFLECT, "reflect", "反射", "Reflection"),
    { -1, -1, GB_PM_CURSE, GB_SK_PERM_BAD, "cursed", "呪い", "Cursed", NULL, NULL, NULL },
    /* ---- 武器の性質（利き手・逆手） ---- */
    GBP_WP(TR_SLAY_EVIL, TR_KILL_EVIL, "slay_evil", "混沌倍打", "Slay Evil"),
    GBP_WP(TR_SLAY_GOOD, TR_KILL_GOOD, "slay_good", "秩序倍打", "Slay Good"),
    GBP_WP(TR_SLAY_HUMAN, TR_KILL_HUMAN, "slay_human", "人間倍打", "Slay Human"),
    GBP_WP(TR_SLAY_ANIMAL, TR_KILL_ANIMAL, "slay_animal", "動物倍打", "Slay Animal"),
    GBP_WP(TR_SLAY_UNDEAD, TR_KILL_UNDEAD, "slay_undead", "不死倍打", "Slay Undead"),
    GBP_WP(TR_SLAY_DRAGON, TR_KILL_DRAGON, "slay_dragon", "竜倍打", "Slay Dragon"),
    GBP_WP(TR_SLAY_DEMON, TR_KILL_DEMON, "slay_demon", "悪魔倍打", "Slay Demon"),
    GBP_WP(TR_SLAY_DEITY, TR_KILL_DEITY, "gb_slay_deity", "神格倍打", "Slay Deity"),
    GBP_WP(TR_SLAY_KWAI, TR_KILL_KWAI, "gb_slay_kwai", "妖怪倍打", "Slay Youkai"),
    GBP_WP(TR_BRAND_ACID, -1, "attk_acid", "溶解", "Acid Brand"),
    GBP_WP(TR_BRAND_ELEC, -1, "attk_elec", "電撃", "Elec Brand"),
    GBP_WP(TR_BRAND_FIRE, -1, "attk_fire", "焼棄", "Fire Brand"),
    GBP_WP(TR_BRAND_COLD, -1, "attk_cold", "凍結", "Cold Brand"),
    GBP_WP(TR_BRAND_POIS, -1, "attk_pois", "毒殺", "Poison Brand"),
    GBP_WP(TR_VORPAL, TR_EX_VORPAL, "vorpal", "切れ味", "Sharpness"),
    GBP_WP(TR_IMPACT, -1, "earthquake", "地震", "Quake"),
    GBP_WP(TR_VAMPIRIC, -1, "vampiric", "吸血", "Vampiric"),
    GBP_WP(TR_CHAOTIC, -1, "chaotic", "カオス効果", "Chaotic"),
    GBP_WP(TR_FORCE_WEAPON, -1, "force_weapon", "理力", "Force Weapon"),
    GBP_WP(TR_GENZI, -1, "gb_genzi", "二刀流緩和", "Dual Wield"), /* C 画面には行が無いが、鑑定済みの旗なので出す */
    /* ---- 感知 ---- */
    GBP(TR_TELEPATHY, "telepathy", "テレパシー", "Telepathy"),
    GBP(TR_ESP_UNIQUE, "esp_unique", "ユニークESP", "Sense Uniques"),
    GBP(TR_ESP_NONLIVING, "esp_nonliving", "無生物ESP", "Sense Nonliving"),
    GBP(TR_ESP_EVIL, "esp_evil", "混沌ESP", "Sense Evil"),
    GBP(TR_ESP_GOOD, "esp_good", "秩序ESP", "Sense Good"),
    GBP(TR_ESP_HUMAN, "esp_human", "人間ESP", "Sense Humans"),
    GBP(TR_ESP_ANIMAL, "esp_animal", "動物ESP", "Sense Animals"),
    GBP(TR_ESP_UNDEAD, "esp_undead", "不死ESP", "Sense Undead"),
    GBP(TR_ESP_DRAGON, "esp_dragon", "竜ESP", "Sense Dragons"),
    GBP(TR_ESP_DEMON, "esp_demon", "悪魔ESP", "Sense Demons"),
    GBP(TR_ESP_DEITY, "gb_esp_deity", "神格ESP", "Sense Deity"),
    GBP(TR_ESP_KWAI, "gb_esp_kwai", "妖怪ESP", "Sense Youkai"),
    /* ---- 能力値の維持 ---- */
    GBP(TR_SUST_STR, "sust_str", "腕力維持", "Sust Str"),
    GBP(TR_SUST_INT, "sust_int", "知力維持", "Sust Int"),
    GBP(TR_SUST_WIS, "sust_wis", "賢さ維持", "Sust Wis"),
    GBP(TR_SUST_DEX, "sust_dex", "器用維持", "Sust Dex"),
    GBP(TR_SUST_CON, "sust_con", "耐久維持", "Sust Con"),
    GBP(TR_SUST_CHR, "sust_chr", "魅力維持", "Sust Chr"),
    /* ---- 能力・その他 ---- */
    GBP(TR_SPEED, "haste", "加速", "Speed"),
    GBP(TR_BLOWS, "blows", "追加攻撃", "Extra Blows"),
    GBP(TR_INFRA, "infravision", "赤外線視力", "Infravision"),
    GBP(TR_MAGIC_MASTERY, "magic_mastery", "魔法道具支配", "Magic Mastery"),
    GBP(TR_STEALTH, "stealth", "隠密", "Stealth"),
    GBP(TR_SEARCH, "search", "探索", "Searching"),
    GBP(TR_TUNNEL, "tunnel", "採掘", "Tunneling"),
    GBP(TR_EASY_SPELL, "easy_spell", "魔法難度減少", "Easy Spell"),
    GBP(TR_DEC_MANA, "dec_mana", "消費魔力減少", "Economic Mana"),
    GBP(TR_BLESSED, "blessed", "祝福", "Blessed"),
    GBP(TR_LITE, "lite", "永遠光源", "Perm Light"),
    GBP(TR_RIDING, "riding", "乗馬", "For Riding"),
    GBP(TR_THROW, "throw", "投擲", "Throwing"),
    GBP_BAD(TR_DRAIN_EXP, "drain_exp", "経験値吸収", "Drain Exp"),
    GBP_BAD(TR_AGGRAVATE, "aggravate", "反感", "Aggravate"),
    GBP_BAD(TR_TY_CURSE, "ty_curse", "太古の怨念", "TY Curse"),
};

#define GB_PERM_ROWS ((int)(sizeof(gb_perm_rows) / sizeof(gb_perm_rows[0])))

int gb_status_perm_row_count(void)
{
    return GB_PERM_ROWS;
}

const char *gb_status_perm_row_key(int row, int *kind, const char **imm_key)
{
    if ((row < 0) || (row >= GB_PERM_ROWS)) {
        return NULL;
    }
    if (kind) {
        *kind = (int)gb_perm_rows[row].kind;
    }
    if (imm_key) {
        *imm_key = gb_perm_rows[row].imm_key;
    }
    return gb_perm_rows[row].key;
}

/* 原作の `files.c` の末尾へ overlay.py が足す読み取りの関数（static な `player_flags` などを外へ出すだけ）。 */
extern void gb_native_player_flags(u32b *player, u32b *tim, u32b *imm, u32b *tim_imm);
/* `files.c`（static ではない）。女苑の指輪の枠（拡張インベントリ）の旗をまとめる。 */
extern void obj_flags_known_jyoon_ring(int slot, u32b flgs[TR_FLAG_SIZE]);

typedef struct gb_perm_ctx {
    u32b slot[INVEN_TOTAL][TR_FLAG_SIZE]; /*!< 装備の枠ごとの鑑定済みの旗 */
    unsigned char used[INVEN_TOTAL]; /*!< 枠に物があって数える */
    u32b player[TR_FLAG_SIZE];
    u32b tim[TR_FLAG_SIZE];
    u32b imm[TR_FLAG_SIZE];
    u32b tim_imm[TR_FLAG_SIZE];
} gb_perm_ctx;

static void gb_perm_collect(gb_perm_ctx *c)
{
    int i;

    memset(c, 0, sizeof(*c));
    for (i = INVEN_RARM; i < INVEN_TOTAL; i++) {
        object_type *o_ptr = &inventory[i];

        if (check_invalidate_inventory(i)) {
            continue;
        }
        if ((p_ptr->pclass == CLASS_JYOON) && ((i == INVEN_RIGHT) || (i == INVEN_LEFT))) {
            obj_flags_known_jyoon_ring(i, c->slot[i]);
            c->used[i] = 1;
            continue;
        }
        if (!o_ptr->k_idx) {
            continue;
        }
        object_flags_known(o_ptr, c->slot[i]);
        c->used[i] = 1;
    }
    gb_native_player_flags(c->player, c->tim, c->imm, c->tim_imm);
}

/*!
 * @brief 1 行の判定。C 画面の `display_flag_aux` と同じ。一時的な効果（`tim` 側）は見ない。
 * @return 0 = 無い、1 = 耐性・能力あり、2 = 免疫あり（`imm_key` があるとき）
 */
static int gb_perm_row_state(const gb_perm_ctx *c, const gb_perm_row *r)
{
    int i;
    int max_i = (r->mode == GB_PM_WEAPON) ? (INVEN_LARM + 1) : INVEN_TOTAL;
    int has = 0;
    int imm = 0;

    if (r->mode == GB_PM_CURSE) {
        for (i = INVEN_RARM; i < INVEN_TOTAL; i++) {
            object_type *o_ptr = &inventory[i];

            if (check_invalidate_inventory(i) || !o_ptr->k_idx) {
                continue;
            }
            if (o_ptr->curse_flags & (TRC_CURSED | TRC_HEAVY_CURSE | TRC_PERMA_CURSE)) {
                return 1;
            }
            if (c->used[i] && (have_flag(c->slot[i], TR_ADD_L_CURSE) || have_flag(c->slot[i], TR_ADD_H_CURSE))) {
                return 1;
            }
        }
        return 0;
    }

    for (i = INVEN_RARM; i < max_i; i++) {
        if (!c->used[i]) {
            continue;
        }
        if ((r->greater >= 0) && have_flag(c->slot[i], r->greater)) {
            imm = 1;
        }
        if (have_flag(c->slot[i], r->flag)) {
            has = 1;
        }
    }
    /* 種族・職業などの固有の旗。免疫（`player_imm`）は耐性の旗そのものを立てて来ることもある。 */
    if (have_flag(c->player, r->flag)) {
        has = 1;
    }
    if (have_flag(c->imm, r->flag)) {
        has = 1;
        imm = 1;
    }
    if ((r->greater >= 0) && have_flag(c->imm, r->greater)) {
        imm = 1;
    }
    if (imm && r->imm_key) {
        return 2;
    }
    return (has || imm) ? 1 : 0;
}

/* ---------------------------------------------------------------- 組み立て */

static int gb_status_push(gb_status_effect *out, int *n, int cap, const char *key, const char *label_sjis,
    const char *amount, int kind)
{
    int i;
    gb_status_effect *e;

    if (!key || !key[0]) {
        return 0;
    }
    for (i = 0; i < *n; i++) { /* 同じ鍵・同じ種類の重複は 1 つにする（光学迷彩と超隠密など） */
        if ((out[i].kind == kind) && (strcmp(out[i].key, key) == 0)) {
            return 0;
        }
    }
    if (*n >= cap) {
        return 0;
    }
    e = &out[*n];
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    if (label_sjis) {
        strncpy(e->label, label_sjis, sizeof(e->label) - 1);
    }
    if (amount) {
        strncpy(e->amount, amount, sizeof(e->amount) - 1);
    }
    e->kind = kind;
    (*n)++;
    return 1;
}

#define GB_L(ja, en) (gb_lang_enabled() ? (en) : (ja))

int gb_read_status_effects(gb_status_effect *out, int cap)
{
    int n = 0;
    int i;

    if (!out || (cap <= 0) || !character_generated || !p_ptr || p_ptr->is_dead) {
        return 0;
    }
    if (gb_status_overflow) { /* 原作の `bar[]` が表より長い。版上げで BAR_* が増えたので表を足す（一度だけ知らせる） */
        static int warned;

        if (!warned) {
            warned = 1;
            fprintf(stderr, "[gensoband] status icons: the original bar[] is longer than GB_BAR_COUNT (%d); extend gb_bar_icons[]\n",
                GB_BAR_COUNT);
        }
    }

    /* (1) 速さ。`prt_speed` と同じ式（探索中の減速は見かけから外す）。 */
    {
        int sp = p_ptr->pspeed;
        char amt[16];

        if ((p_ptr->action == ACTION_SEARCH) && !p_ptr->lightspeed) {
            sp += 10;
        }
        if (CLASS_RIDING_BACKDANCE && p_ptr->riding) {
            const char *rdesc = (p_ptr->pclass == CLASS_TSUKASA) ? GB_L("寄生", "Parasite") : GB_L("ダンス", "Dance");

            if (sp > 110) {
                sprintf(amt, "+%d", sp - 110);
                gb_status_push(out, &n, cap, "haste", rdesc, amt, GB_SK_GOOD);
            } else if (sp < 110) {
                sprintf(amt, "-%d", 110 - sp);
                gb_status_push(out, &n, cap, "slow", rdesc, amt, GB_SK_BAD);
            }
        } else if (sp > 110) {
            sprintf(amt, "+%d", sp - 110);
            gb_status_push(out, &n, cap, "haste", p_ptr->riding ? GB_L("乗馬", "Ride") : GB_L("加速", "Fast"), amt,
                GB_SK_GOOD);
        } else if (sp < 110) {
            sprintf(amt, "-%d", 110 - sp);
            gb_status_push(out, &n, cap, "slow", p_ptr->riding ? GB_L("乗馬", "Ride") : GB_L("減速", "Slow"), amt,
                GB_SK_BAD);
        }
    }

    /* (2) 状態の旗。`BAR_*` の小さい順＝原作の最下段の並び。 */
    for (i = 0; i < GB_BAR_COUNT; i++) {
        if (gb_status_on[i]) {
            gb_status_push(out, &n, cap, gb_bar_icons[i].key, gb_status_label[i], NULL, (int)gb_bar_icons[i].kind);
        }
    }

    /* (3) 朦朧・切り傷・空腹（原作の `prt_stun`・`prt_cut`・`prt_hunger`）。 */
    {
        int s = p_ptr->stun;
        const char *label = NULL;

        if ((p_ptr->alcohol >= DRANK_4) && !(p_ptr->muta2 & MUT2_ALCOHOL)) {
            label = GB_L("意識不明瞭", "Knocked out");
        } else if (s > 100) {
            label = GB_L("意識不明瞭", "Knocked out");
        } else if (s > 50) {
            label = GB_L("ひどく朦朧", "Heavy stun");
        } else if (s) {
            label = GB_L("朦朧", "Stun");
        } else if (REF_BROKEN_VAL) {
            int b = REF_BROKEN_VAL;

            if (b < BROKEN_1) {
                label = GB_L("傷", "Scratch");
            } else if (b < BROKEN_2) {
                label = GB_L("亀裂", "Crack");
            } else if (b < BROKEN_3) {
                label = GB_L("破損", "Damaged");
            } else {
                label = GB_L("大破", "Wrecked");
            }
        }
        if (label) {
            gb_status_push(out, &n, cap, "stun", label, NULL, GB_SK_BAD);
        }
    }
    {
        int c = p_ptr->cut;
        const char *label = NULL;

        if (c > CUT_7) {
            label = GB_L("深刻な大怪我", "Mortal wound");
        } else if (c > CUT_6) {
            label = GB_L("ひどい深手", "Deep gash");
        } else if (c > CUT_5) {
            label = GB_L("大怪我", "Severe cut");
        } else if (c > CUT_4) {
            label = GB_L("大変な傷", "Nasty cut");
        } else if (c > CUT_3) {
            label = GB_L("ひどい傷", "Bad cut");
        } else if (c > CUT_2) {
            label = GB_L("軽傷", "Light cut");
        } else if (c) {
            label = GB_L("かすり傷", "Graze");
        }
        if (label) {
            gb_status_push(out, &n, cap, "cut", label, NULL, GB_SK_BAD);
        }
    }
    {
        const char *label = NULL;

        if ((CHECK_YUMA_FOOD_STOCK) > 0) {
            label = NULL; /* 尤魔の満腹のたくわえは悪い状態ではない */
        } else if (p_ptr->food < PY_FOOD_FAINT) {
            label = GB_L("衰弱", "Weak");
        } else if (p_ptr->food < PY_FOOD_WEAK) {
            label = GB_L("衰弱", "Weak");
        } else if (p_ptr->food < PY_FOOD_ALERT) {
            label = GB_L("空腹", "Hungry");
        } else if ((p_ptr->food >= PY_FOOD_MAX) && (p_ptr->pclass != CLASS_YUMA)) {
            label = GB_L("過食", "Gorged");
        }
        if (label) {
            gb_status_push(out, &n, cap, "hungry", label, NULL, GB_SK_BAD);
        }
    }

    /* (4) 常時の効果（C 画面の耐性・能力の表。鑑定済みの分だけ）。 */
    {
        static gb_perm_ctx ctx; /* ゲームのスレッドは 1 本。大きいので静的に持つ */

        gb_perm_collect(&ctx);
        for (i = 0; i < GB_PERM_ROWS; i++) {
            const gb_perm_row *r = &gb_perm_rows[i];
            int st = gb_perm_row_state(&ctx, r);

            if (st == 2) {
                gb_status_push(out, &n, cap, r->imm_key, GB_L(r->imm_ja, r->imm_en), NULL, r->kind);
            } else if (st == 1) {
                gb_status_push(out, &n, cap, r->key, GB_L(r->ja, r->en), NULL, r->kind);
            }
        }
    }
    return n;
}

/*!
 * @brief 状態のアイコンの表の自己検査（キャラクター不要）。
 * @details 表の鍵が空でない・長さの上限内・種類が 0..5・同じ BAR の鍵が別の種類で重複しない、
 * 常時の効果の行の旗が範囲内、を見る。画面側の表（`status_icon_table.inc`）との突き合わせは、
 * 環境変数 `HENGBAND_STATUS_ICON_TABLE` にそのファイルの絶対パスがあるときだけここで行う
 * （無ければ `*checked` を 0 のまま返し、突き合わせは `tools/gensoband/gb_status_table_check.py` が行う）。
 * @return 外れた件数（0 なら全部合っている）
 */
int gb_status_selftest(char *err, int cap, int *checked)
{
    int fails = 0;
    int i;
    const char *path = getenv("HENGBAND_STATUS_ICON_TABLE");
    char *table = NULL;
    long len = 0;

    if (err && (cap > 0)) {
        err[0] = '\0';
    }
    if (checked) {
        *checked = 0;
    }
#define GB_ST_FAIL(...) \
    do { \
        if ((fails == 0) && err && (cap > 1)) { \
            sprintf(err, __VA_ARGS__); \
            err[cap - 1] = '\0'; \
        } \
        fails++; \
    } while (0)

    if (path && path[0]) {
        FILE *fp = fopen(path, "rb");

        if (fp) {
            fseek(fp, 0, SEEK_END);
            len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if ((len > 0) && (len < 4 * 1024 * 1024)) {
                table = (char *)malloc((size_t)len + 1);
                if (table) {
                    len = (long)fread(table, 1, (size_t)len, fp);
                    table[len] = '\0';
                }
            }
            fclose(fp);
        }
        if (!table) {
            GB_ST_FAIL("icon table unreadable: %.100s", path);
        }
    }

    for (i = 0; i < GB_BAR_COUNT; i++) {
        const gb_bar_icon *b = &gb_bar_icons[i];

        if (!b->key || !b->key[0] || (strlen(b->key) >= sizeof(((gb_status_effect *)0)->key))) {
            GB_ST_FAIL("bar %d: bad key", i);
            continue;
        }
        if (b->kind > GB_SK_SPECIAL) {
            GB_ST_FAIL("bar %d (%s): kind %d is not a temporary kind", i, b->key, (int)b->kind);
        }
        if (table) {
            char needle[64];

            sprintf(needle, "{ \"%s\",", b->key);
            if (!strstr(table, needle)) {
                GB_ST_FAIL("bar %d: key '%s' is not in the icon table", i, b->key);
            }
            if (checked) {
                (*checked)++;
            }
        }
    }
    for (i = 0; i < GB_PERM_ROWS; i++) {
        const gb_perm_row *r = &gb_perm_rows[i];

        if (!r->key || !r->key[0] || !r->ja || !r->en) {
            GB_ST_FAIL("perm row %d: missing key or label", i);
            continue;
        }
        if ((r->kind != GB_SK_PERM) && (r->kind != GB_SK_PERM_BAD)) {
            GB_ST_FAIL("perm row %d (%s): kind %d is not permanent", i, r->key, (int)r->kind);
        }
        if ((r->mode != GB_PM_CURSE) && ((r->flag < 0) || (r->flag >= TR_FLAG_MAX))) {
            GB_ST_FAIL("perm row %d (%s): flag %d out of range", i, r->key, (int)r->flag);
        }
        if (r->imm_key && (!r->imm_ja || !r->imm_en)) {
            GB_ST_FAIL("perm row %d (%s): immune label missing", i, r->key);
        }
        if (table) {
            char needle[64];

            sprintf(needle, "{ \"%s\",", r->key);
            if (!strstr(table, needle)) {
                GB_ST_FAIL("perm row %d: key '%s' is not in the icon table", i, r->key);
            }
            if (checked) {
                (*checked)++;
            }
            if (r->imm_key) {
                sprintf(needle, "{ \"%s\",", r->imm_key);
                if (!strstr(table, needle)) {
                    GB_ST_FAIL("perm row %d: key '%s' is not in the icon table", i, r->imm_key);
                }
                if (checked) {
                    (*checked)++;
                }
            }
        }
    }
    /* 速さ・朦朧・切り傷・空腹の鍵（`gb_read_status_effects` が直に書く）。 */
    if (table) {
        static const char *const direct[] = { "haste", "slow", "stun", "cut", "hungry" };

        for (i = 0; i < 5; i++) {
            char needle[64];

            sprintf(needle, "{ \"%s\",", direct[i]);
            if (!strstr(table, needle)) {
                GB_ST_FAIL("direct key '%s' is not in the icon table", direct[i]);
            }
            if (checked) {
                (*checked)++;
            }
        }
    }
    free(table);
#undef GB_ST_FAIL
    return fails;
}

/* ============================================================ 検査用：敵の行動を指定する入口 */

/*
 * ## これは何か
 *
 * 攻撃の効果（ブレス・ボルト・ボール・直接攻撃）の検査は、敵が魔法を撃つまで休んで待つので長くかかり、
 * くじ運で結果も揺れる。そこで、**検査のときだけ効く**入口を足す。環境変数 `HD2D_TEST_MONSTER_ACT` に
 * 命令ファイルのパスが入っているときだけ働き、無ければ何もしない（普段の遊び・配布物には効かない）。
 *
 * コアが**プレイヤーの次のキーを読むとき**（命令待ち。`gb_awaiting_command()` が真）に、その
 * ファイルがあれば読み、**先にファイルを消して**（消せなければ名前を `.done` へ変える。どちらもできなければ
 * 実行しない）から、命令を**上から順に、1 回の命令待ちの中で全部**実行する（その間ゲームの時間は進めない）。
 * 結果は 1 行ずつ標準エラーへ出す:
 *
 *   [test-monster-act] ok <命令の全文>
 *   [test-monster-act] fail <理由>
 *   [test-monster-act] skip <命令の全文>    ... 手前の行が fail したので実行しなかった行
 *   [test-monster-act] info <補足>
 *
 * 途中で fail したら、残りの行は実行せず、行ごとに skip を出す。空行と `#` で始まる行は読み飛ばす（何も出さない）。
 * 命令ファイルは**別の名前で書いてから改名して**置くこと（書きかけを読まない）。空のファイルは読まない。
 * 大きさは 16 KB まで。
 *
 * ## 命令（1 行 1 命令。空白区切り。`nearest` は見えているいちばん近い敵対の敵）
 *
 *   cast <魔法の名前> [nearest|<m_idx>]  指定の敵（省略時は nearest）に、その魔法をプレイヤーへ向けていま撃たせる
 *   melee [nearest|<m_idx>]               指定の敵に、いまプレイヤーへ直接攻撃させる（隣にいるときだけ）
 *   place <nearest|<m_idx>> <dx> <dy>     指定の敵を、プレイヤーの位置から (dx, dy) ずれたマスへ動かす（時間は使わない）。
 *                                         マスが地図の外・敵が入れない地形・ほかの敵やプレイヤーがいる・乗っている馬は fail。
 *                                         動かしたあとの見え方・距離・光の更新は、原作が敵を動かすときと同じ関数で行う（`gbt_place()`）。
 *                                         `info place m_idx=… r_idx=… from=x,y to=x,y player=x,y turn=…` を出す（nearest が誰だったかが分かる）
 *   hold <nearest|<m_idx>|all> on|off     on の間、その敵は自分の番に行動しない（動かない・魔法を撃たない・殴らない）。
 *                                         入口から命じた cast・melee・place は働く。off で元に戻す。実装は下の「hold」の節
 *                                         `info hold … state=on|off saved=<止める前の energy_need> now=<直近の置き直しの直前に見えていた値>
 *                                         turn=<ゲームターン> pos=x,y player=x,y` を出す。止めている敵に `hold <id> on` をもう一度命じると、
 *                                         状態を読むだけ（止めた時点の値は変わらない）
 *
 * 魔法の名前は原作の旗の名前から `RF4_`・`RF5_`・`RF6_`・`RF9_` を外したもの（`BR_FIRE`・`BO_COLD`・`MISSILE` など）。
 * 大文字小文字は区別しない。知らない名前は fail。
 *
 * ## どうやって必ず撃たせるか（原作 `mspells1.c` の `make_attack_spell()` は書き換えない）
 *
 * 原作は `make_attack_spell()` の中で、種族（`r_info`）の魔法の旗からくじ引きで 1 つ選ぶ。そこで呼ぶ直前に
 * その種族の次の 4 つだけを一時的に変え、**呼んだらすぐ元の値へ戻す**（`gbt_race_restore()`。戻したあと読み直して
 * 照合し、食い違えば fail にする）。乱数と乱数の種には触れない。
 *
 *   1. `flags4`・`flags5`・`flags6`・`flags9`  … 指定の 1 つだけを立てる（ほかは 0）
 *   2. `freq_spell`                            … 100（`no_inate` を立てるくじ `randint0(100) >= freq_spell * 2` を必ず外す）
 *   3. `flags2` の `RF2_STUPID`                … 立てる。原作の STUPID の扱いは「選ぶとき無作為（＝旗が 1 つなら必ずそれ）」
 *      「失敗率 0」「学習による除外（`remove_bad_spells`）をしない」「味方に当たるボルトの除外・召喚の除外をしない」
 *      「魔法を封じる状態でも撃つ」。くじ運に左右される分岐をまとめて外すためのもの
 *   4. `m_ptr->mflag` の `MFLAG_NICE`          … 下ろす（出たばかりの敵は、プレイヤーが 1 歩動くまで魔法を撃たない旗）。
 *      呼んだあと、元から立っていたなら立て直す
 *
 * 命令待ちの中（`inkey()` の待ちの途中）から呼ぶので、原作の「モンスターの番」と同じ状態になるよう整える:
 *   * `inkey_flag` を偽にして呼ぶ（モンスターの番では偽。真のままだと、途中の `-続く-` を待つフレームが
 *     「命令待ち」として送られ、矢弾の記録が早く確定する）。**呼んだあと元の値へ戻す**。入れ子の `inkey()` は
 *     終わりに `inkey_base`・`inkey_xtra`・`inkey_flag`・`inkey_scan` を全部偽にするので、4 つとも保存して戻す
 *   * 呼んだあと `notice_stuff()`・`handle_stuff()`（体力などの再描画）と `Term_fresh()`（フレームを送る）
 *   * プレイヤーが死んだ・階を離れる（`is_dead`・`leaving`）ときは、待っているコアを起こすために
 *     ESC を 1 つ返す（原作はこの場合、プレイヤーの番の終わりでループを抜ける）
 *
 * ## 呼ぶ前に調べて fail にするもの（くじ運に左右されず、魔法が出ない／失敗する状態）
 *   * 敵が狂戦士（`MON_BERSERK`）・混乱・敵対でない（ペット・友好）・プレイヤーに射線が通らない・射程（`MAX_RANGE`）の外
 *   * `ARIYA_STOP`（有耶「時の止まった」の状態）
 *   * 魔法（`spell_is_inate()` が偽のもの）で、反魔法の洞窟・黙らせる床・`MFLAG_NO_SPELL`・朦朧・恐怖・魔法力低下・
 *     SUPER_SHION・女苑のプラックピジョン（失敗率を足す状態）のとき。呼ぶと「呪文を唱えようとしたが失敗した」になりうる
 *
 * 職業ごとの打ち消し（ヘックスの反魔法バリア・メルランのハッピーライブ・魔理沙の吸収・送還術 ほか）は調べない。
 * 検査のキャラクターが該当する職業のときは、撃たない（`ok` なのに効果が出ない）ことがある。
 */

#define GBT_SLOT_4 3 /* RF4 = spell 番号 96〜 */
#define GBT_SLOT_5 4 /* RF5 = 128〜 */
#define GBT_SLOT_6 5 /* RF6 = 160〜 */
#define GBT_SLOT_9 6 /* RF9 = 192〜 */
#define GBT_SPELL(w, n) { #n, GBT_SLOT_##w, RF##w##_##n }

typedef struct {
    const char *name;
    int slot;
    u32b bit;
} gbt_spell_def;

static const gbt_spell_def gbt_spells[] = {
    GBT_SPELL(4, SHRIEK),
    GBT_SPELL(4, DANMAKU),
    GBT_SPELL(4, DISPEL),
    GBT_SPELL(4, ROCKET),
    GBT_SPELL(4, SHOOT),
    GBT_SPELL(4, BR_HOLY),
    GBT_SPELL(4, BR_HELL),
    GBT_SPELL(4, BR_AQUA),
    GBT_SPELL(4, BR_ACID),
    GBT_SPELL(4, BR_ELEC),
    GBT_SPELL(4, BR_FIRE),
    GBT_SPELL(4, BR_COLD),
    GBT_SPELL(4, BR_POIS),
    GBT_SPELL(4, BR_NETH),
    GBT_SPELL(4, BR_LITE),
    GBT_SPELL(4, BR_DARK),
    GBT_SPELL(4, WAVEMOTION),
    GBT_SPELL(4, BR_SOUN),
    GBT_SPELL(4, BR_CHAO),
    GBT_SPELL(4, BR_DISE),
    GBT_SPELL(4, BR_NEXU),
    GBT_SPELL(4, BR_TIME),
    GBT_SPELL(4, BR_INER),
    GBT_SPELL(4, BR_GRAV),
    GBT_SPELL(4, BR_SHAR),
    GBT_SPELL(4, BR_PLAS),
    GBT_SPELL(4, BA_FORCE),
    GBT_SPELL(4, BR_MANA),
    GBT_SPELL(4, SPECIAL2),
    GBT_SPELL(4, BR_NUKE),
    GBT_SPELL(4, BA_CHAO),
    GBT_SPELL(4, BR_DISI),
    GBT_SPELL(5, BA_ACID),
    GBT_SPELL(5, BA_ELEC),
    GBT_SPELL(5, BA_FIRE),
    GBT_SPELL(5, BA_COLD),
    GBT_SPELL(5, BA_POIS),
    GBT_SPELL(5, BA_NETH),
    GBT_SPELL(5, BA_WATE),
    GBT_SPELL(5, BA_MANA),
    GBT_SPELL(5, BA_DARK),
    GBT_SPELL(5, DRAIN_MANA),
    GBT_SPELL(5, MIND_BLAST),
    GBT_SPELL(5, BRAIN_SMASH),
    GBT_SPELL(5, CAUSE_1),
    GBT_SPELL(5, CAUSE_2),
    GBT_SPELL(5, CAUSE_3),
    GBT_SPELL(5, CAUSE_4),
    GBT_SPELL(5, BO_ACID),
    GBT_SPELL(5, BO_ELEC),
    GBT_SPELL(5, BO_FIRE),
    GBT_SPELL(5, BO_COLD),
    GBT_SPELL(5, BA_LITE),
    GBT_SPELL(5, BO_NETH),
    GBT_SPELL(5, BO_WATE),
    GBT_SPELL(5, BO_MANA),
    GBT_SPELL(5, BO_PLAS),
    GBT_SPELL(5, BO_ICEE),
    GBT_SPELL(5, MISSILE),
    GBT_SPELL(5, SCARE),
    GBT_SPELL(5, BLIND),
    GBT_SPELL(5, CONF),
    GBT_SPELL(5, SLOW),
    GBT_SPELL(5, HOLD),
    GBT_SPELL(6, HASTE),
    GBT_SPELL(6, HAND_DOOM),
    GBT_SPELL(6, HEAL),
    GBT_SPELL(6, INVULNER),
    GBT_SPELL(6, BLINK),
    GBT_SPELL(6, TPORT),
    GBT_SPELL(6, WORLD),
    GBT_SPELL(6, SPECIAL),
    GBT_SPELL(6, TELE_TO),
    GBT_SPELL(6, TELE_AWAY),
    GBT_SPELL(6, TELE_LEVEL),
    GBT_SPELL(6, PSY_SPEAR),
    GBT_SPELL(6, DARKNESS),
    GBT_SPELL(6, TRAPS),
    GBT_SPELL(6, FORGET),
    GBT_SPELL(6, COSMIC_HORROR),
    GBT_SPELL(6, S_KIN),
    GBT_SPELL(6, S_CYBER),
    GBT_SPELL(6, S_MONSTER),
    GBT_SPELL(6, S_MONSTERS),
    GBT_SPELL(6, S_ANT),
    GBT_SPELL(6, S_SPIDER),
    GBT_SPELL(6, S_HOUND),
    GBT_SPELL(6, S_HYDRA),
    GBT_SPELL(6, S_ANGEL),
    GBT_SPELL(6, S_DEMON),
    GBT_SPELL(6, S_UNDEAD),
    GBT_SPELL(6, S_DRAGON),
    GBT_SPELL(6, S_HI_UNDEAD),
    GBT_SPELL(6, S_HI_DRAGON),
    GBT_SPELL(6, S_AMBERITES),
    GBT_SPELL(6, S_UNIQUE),
    GBT_SPELL(9, FIRE_STORM),
    GBT_SPELL(9, ICE_STORM),
    GBT_SPELL(9, THUNDER_STORM),
    GBT_SPELL(9, ACID_STORM),
    GBT_SPELL(9, TOXIC_STORM),
    GBT_SPELL(9, BA_POLLUTE),
    GBT_SPELL(9, BA_DISI),
    GBT_SPELL(9, BA_HOLY),
    GBT_SPELL(9, BA_METEOR),
    GBT_SPELL(9, BA_DISTORTION),
    GBT_SPELL(9, PUNISH_1),
    GBT_SPELL(9, PUNISH_2),
    GBT_SPELL(9, PUNISH_3),
    GBT_SPELL(9, PUNISH_4),
    GBT_SPELL(9, BO_HOLY),
    GBT_SPELL(9, GIGANTIC_LASER),
    GBT_SPELL(9, BO_SOUND),
    GBT_SPELL(9, S_ANIMAL),
    GBT_SPELL(9, BEAM_LITE),
    GBT_SPELL(9, SONICWAVE),
    GBT_SPELL(9, HELLFIRE),
    GBT_SPELL(9, HOLYFIRE),
    GBT_SPELL(9, FINALSPARK),
    GBT_SPELL(9, TORNADO),
    GBT_SPELL(9, DESTRUCTION),
    GBT_SPELL(9, S_DEITY),
    GBT_SPELL(9, S_HI_DEMON),
    GBT_SPELL(9, S_KWAI),
    GBT_SPELL(9, TELE_APPROACH),
    GBT_SPELL(9, TELE_HI_APPROACH),
    GBT_SPELL(9, MAELSTROM),
    GBT_SPELL(9, ALARM),
};

/*! 触る項目をまとめて保存する入れ物（`gbt_race_restore()` で戻す）。 */
typedef struct {
    u32b flags2;
    u32b flags4;
    u32b flags5;
    u32b flags6;
    u32b flags9;
    byte freq_spell;
} gbt_race_save;

static const char gbt_tag[] = "[test-monster-act]";
static char gbt_why[160];

static u32b *gbt_word(monster_race *r_ptr, int slot)
{
    switch (slot) {
    case GBT_SLOT_4: return &r_ptr->flags4;
    case GBT_SLOT_5: return &r_ptr->flags5;
    case GBT_SLOT_6: return &r_ptr->flags6;
    default: return &r_ptr->flags9;
    }
}

static int gbt_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a;
        int cb = (unsigned char)*b;

        if ((ca >= 'a') && (ca <= 'z')) ca -= 'a' - 'A';
        if ((cb >= 'a') && (cb <= 'z')) cb -= 'a' - 'A';
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
}

static const gbt_spell_def *gbt_find_spell(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(gbt_spells) / sizeof(gbt_spells[0]); i++) {
        if (gbt_stricmp(gbt_spells[i].name, name) == 0) return &gbt_spells[i];
    }
    return NULL;
}

static void gbt_race_save_take(gbt_race_save *s, const monster_race *r_ptr)
{
    s->flags2 = r_ptr->flags2;
    s->flags4 = r_ptr->flags4;
    s->flags5 = r_ptr->flags5;
    s->flags6 = r_ptr->flags6;
    s->flags9 = r_ptr->flags9;
    s->freq_spell = r_ptr->freq_spell;
}

static int gbt_race_same(const gbt_race_save *s, const monster_race *r_ptr)
{
    return (s->flags2 == r_ptr->flags2) && (s->flags4 == r_ptr->flags4) && (s->flags5 == r_ptr->flags5) &&
           (s->flags6 == r_ptr->flags6) && (s->flags9 == r_ptr->flags9) && (s->freq_spell == r_ptr->freq_spell);
}

/*! 元へ戻して、読み直して照合する。照合できたら 1。 */
static int gbt_race_restore(const gbt_race_save *s, monster_race *r_ptr)
{
    r_ptr->flags2 = s->flags2;
    r_ptr->flags4 = s->flags4;
    r_ptr->flags5 = s->flags5;
    r_ptr->flags6 = s->flags6;
    r_ptr->flags9 = s->flags9;
    r_ptr->freq_spell = s->freq_spell;
    return gbt_race_same(s, r_ptr);
}

static int gbt_fail(const char *fmt, const char *arg)
{
    if (arg != NULL) {
        snprintf(gbt_why, sizeof(gbt_why), fmt, arg);
    } else {
        snprintf(gbt_why, sizeof(gbt_why), "%s", fmt);
    }
    return 0;
}

/*! 見えていて敵対している、いちばん近い敵（同じ距離なら番号が小さいほう）。無ければ 0。 */
static int gbt_nearest_monster(void)
{
    int i;
    int best = 0;

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = &m_list[i];

        if (!m_ptr->r_idx || !m_ptr->ml || !is_hostile(m_ptr)) continue;
        if (!best || (m_ptr->cdis < m_list[best].cdis)) best = i;
    }
    return best;
}

/*! 対象の指定（省略・`nearest`・番号）を番号へ。解けなければ 0 を返し `gbt_why` に理由を入れる。 */
static int gbt_resolve_target(const char *tok)
{
    int idx;
    const char *p;

    if ((tok == NULL) || (gbt_stricmp(tok, "nearest") == 0)) {
        idx = gbt_nearest_monster();
        if (!idx) (void)gbt_fail("no-visible-hostile-monster", NULL);
        return idx;
    }
    for (p = tok; *p; p++) {
        if ((*p < '0') || (*p > '9')) {
            (void)gbt_fail("bad-target %s", tok);
            return 0;
        }
    }
    idx = atoi(tok);
    if ((idx < 1) || (idx >= m_max) || !m_list[idx].r_idx) {
        (void)gbt_fail("no-such-monster %s", tok);
        return 0;
    }
    return idx;
}

/*! 反魔法の洞窟（`make_attack_spell()` の `in_no_magic_dungeon` と同じ式）。 */
static int gbt_in_no_magic_dungeon(void)
{
    return ((d_info[dungeon_type].flags1 & DF1_NO_MAGIC) && dun_level &&
            (!p_ptr->inside_quest || is_fixed_quest_idx(p_ptr->inside_quest)))
               ? 1
               : 0;
}

/*! 指定の敵に、指定の魔法を撃たせる。撃たせたら 1。 */
static int gbt_cast(int m_idx, const gbt_spell_def *sp)
{
    monster_type *m_ptr = &m_list[m_idx];
    const int r_idx = m_ptr->r_idx;
    monster_race *r_ptr = &r_info[r_idx];
    gbt_race_save save;
    const u32b nice0 = m_ptr->mflag & MFLAG_NICE;
    const bool inkey_flag0 = inkey_flag;
    const bool inkey_base0 = inkey_base;
    const bool inkey_xtra0 = inkey_xtra;
    const bool inkey_scan0 = inkey_scan;
    int spell_no = 0;
    int k;
    int cast;
    int restored;

    for (k = 0; k < 32; k++) {
        if (sp->bit == (1UL << k)) spell_no = 32 * sp->slot + k;
    }
    if (!spell_no) return gbt_fail("bad-spell-table", NULL);

    if (MON_BERSERK(m_ptr)) return gbt_fail("monster-berserk", NULL);
    if (MON_CONFUSED(m_ptr)) return gbt_fail("monster-confused", NULL);
    if (!is_hostile(m_ptr)) return gbt_fail("monster-not-hostile", NULL);
    if (ARIYA_STOP) return gbt_fail("ariya-stop", NULL);
    if (m_ptr->cdis > MAX_RANGE) return gbt_fail("out-of-range", NULL);
    if (!projectable(m_ptr->fy, m_ptr->fx, py, px)) return gbt_fail("not-projectable", NULL);
    if (!spell_is_inate((u16b)spell_no)) {
        /* 魔法は、失敗率に足される状態があると「失敗した」になりうる（STUPID でも 0 に戻らない） */
        if (gbt_in_no_magic_dungeon()) return gbt_fail("magic-blocked:no-magic-dungeon", NULL);
        if (p_ptr->silent_floor) return gbt_fail("magic-blocked:silent-floor", NULL);
        if (m_ptr->mflag & MFLAG_NO_SPELL) return gbt_fail("magic-blocked:monster-no-spell", NULL);
        if (MON_STUNNED(m_ptr)) return gbt_fail("magic-may-fail:monster-stunned", NULL);
        if (MON_MONFEAR(m_ptr)) return gbt_fail("magic-may-fail:monster-afraid", NULL);
        if (MON_DEC_MAG(m_ptr)) return gbt_fail("magic-may-fail:monster-dec-mag", NULL);
        if (SUPER_SHION) return gbt_fail("magic-may-fail:super-shion", NULL);
        if ((p_ptr->pclass == CLASS_JYOON) && p_ptr->tim_general[0]) return gbt_fail("magic-may-fail:pluck-pigeon", NULL);
    }

    /* ---- 一時的に整える（ここから `gbt_race_restore()` までの間に return しない） ---- */
    gbt_race_save_take(&save, r_ptr);
    r_ptr->flags4 = 0L;
    r_ptr->flags5 = 0L;
    r_ptr->flags6 = 0L;
    r_ptr->flags9 = 0L;
    *gbt_word(r_ptr, sp->slot) = sp->bit;
    r_ptr->freq_spell = 100;
    r_ptr->flags2 |= RF2_STUPID;
    m_ptr->mflag &= ~MFLAG_NICE;
    inkey_flag = FALSE;

    cast = make_attack_spell(m_idx, 0) ? 1 : 0;

    restored = gbt_race_restore(&save, r_ptr);
    if (m_ptr->r_idx == r_idx) m_ptr->mflag |= nice0;
    notice_stuff();
    handle_stuff();
    inkey_flag = inkey_flag0;
    inkey_base = inkey_base0;
    inkey_xtra = inkey_xtra0;
    inkey_scan = inkey_scan0;
    Term_fresh();
    /* ---- 戻した ---- */

    /* 呼ぶ前と戻したあとの値を並べて出す（検査が外から見比べる）。形は flags2/flags4/flags5/flags6/flags9/freq_spell の順 */
    fprintf(stderr, "%s info restore race=%d restored=%d before=%08lx/%08lx/%08lx/%08lx/%08lx/%d after=%08lx/%08lx/%08lx/%08lx/%08lx/%d m_idx=%d\n",
            gbt_tag, r_idx, restored, (unsigned long)save.flags2, (unsigned long)save.flags4, (unsigned long)save.flags5,
            (unsigned long)save.flags6, (unsigned long)save.flags9, (int)save.freq_spell, (unsigned long)r_ptr->flags2,
            (unsigned long)r_ptr->flags4, (unsigned long)r_ptr->flags5, (unsigned long)r_ptr->flags6, (unsigned long)r_ptr->flags9,
            (int)r_ptr->freq_spell, m_idx);
    if (!restored) return gbt_fail("race-flags-not-restored", NULL);
    if (!cast) return gbt_fail("make_attack_spell-returned-false", NULL);
    return 1;
}

/*! 指定の敵に、プレイヤーへ直接攻撃させる。攻撃させたら 1。 */
static int gbt_melee(int m_idx)
{
    monster_type *m_ptr = &m_list[m_idx];
    monster_race *r_ptr = &r_info[m_ptr->r_idx];
    const bool inkey_flag0 = inkey_flag;
    const bool inkey_base0 = inkey_base;
    const bool inkey_xtra0 = inkey_xtra;
    const bool inkey_scan0 = inkey_scan;
    int hit;

    if (distance(m_ptr->fy, m_ptr->fx, py, px) > 1) return gbt_fail("not-adjacent", NULL);
    if (!is_hostile(m_ptr)) return gbt_fail("monster-not-hostile", NULL);
    if (r_ptr->flags1 & RF1_NEVER_BLOW) return gbt_fail("never-blow", NULL);
    if (d_info[dungeon_type].flags1 & DF1_NO_MELEE) return gbt_fail("no-melee-dungeon", NULL);

    inkey_flag = FALSE;
    hit = make_attack_normal(m_idx) ? 1 : 0;
    notice_stuff();
    handle_stuff();
    inkey_flag = inkey_flag0;
    inkey_base = inkey_base0;
    inkey_xtra = inkey_xtra0;
    inkey_scan = inkey_scan0;
    Term_fresh();

    if (!hit) return gbt_fail("make_attack_normal-returned-false", NULL);
    return 1;
}

/* ---- 入口を呼ぶ間、入れ子の `inkey()` が書き換える状態を保存して戻す ---- */

typedef struct {
    bool flag;
    bool base;
    bool xtra;
    bool scan;
} gbt_inkey_state;

static void gbt_inkey_save(gbt_inkey_state *s)
{
    s->flag = inkey_flag;
    s->base = inkey_base;
    s->xtra = inkey_xtra;
    s->scan = inkey_scan;
}

static void gbt_inkey_load(const gbt_inkey_state *s)
{
    inkey_flag = s->flag;
    inkey_base = s->base;
    inkey_xtra = s->xtra;
    inkey_scan = s->scan;
}

/* ---- 整数の引数 ---- */

static int gbt_parse_int(const char *s, int *out)
{
    char *end;
    long v;

    if ((s == NULL) || (*s == '\0')) return 0;
    v = strtol(s, &end, 10);
    if ((*end != '\0') || (v < -1000L) || (v > 1000L)) return 0;
    *out = (int)v;
    return 1;
}

/* ---- place：敵をプレイヤーからずれたマスへ動かす ---- */

/*!
 * 指定の敵を、プレイヤーの位置から (dx, dy) ずれたマスへ動かす。時間は使わない。動かせたら 1。
 *
 * 位置を書き換えるときに原作が呼んでいるものと同じ並びで更新する（`melee2.c` の `process_monster()` の移動、
 * `spells3.c` の `teleport_away()`）: 旧マスの `m_idx` を 0・新マスへ `m_idx`・`fy`/`fx`・`reset_target()`・
 * `update_mon(m_idx, TRUE)`（見え方と `cdis` を更新）・旧マスと新マスの `lite_spot()`・光や闇を持つ敵なら `PU_MON_LITE`。
 * 立てるか否かは原作の `monster_can_enter()`（プレイヤーや別の敵がいるマス・敵が入れない地形を断る）で決める。
 */
static int gbt_place(int m_idx, int dx, int dy)
{
    monster_type *m_ptr = &m_list[m_idx];
    monster_race *r_ptr = &r_info[m_ptr->r_idx];
    const int oy = m_ptr->fy;
    const int ox = m_ptr->fx;
    const int ny = py + dy;
    const int nx = px + dx;
    gbt_inkey_state ink;

    if (p_ptr->wild_mode) return gbt_fail("wild-mode", NULL);
    if (!in_bounds(ny, nx)) return gbt_fail("out-of-bounds", NULL);
    if (p_ptr->riding == m_idx) return gbt_fail("monster-is-riding", NULL);
    if ((ny != oy) || (nx != ox)) {
        if (player_bold(ny, nx)) return gbt_fail("occupied-by-player", NULL);
        if (cave[ny][nx].m_idx) return gbt_fail("occupied-by-monster", NULL);
        if (!monster_can_enter(ny, nx, r_ptr, 0)) return gbt_fail("terrain-not-enterable", NULL);

        gbt_inkey_save(&ink);
        cave[oy][ox].m_idx = 0;
        cave[ny][nx].m_idx = (s16b)m_idx;
        m_ptr->fy = (byte)ny;
        m_ptr->fx = (byte)nx;
        reset_target(m_ptr);
        update_mon(m_idx, TRUE);
        lite_spot(oy, ox);
        lite_spot(ny, nx);
        if (r_ptr->flags7 & (RF7_LITE_MASK | RF7_DARK_MASK)) p_ptr->update |= PU_MON_LITE;
        notice_stuff();
        handle_stuff();
        gbt_inkey_load(&ink);
        Term_fresh();
    }
    fprintf(stderr, "%s info place m_idx=%d r_idx=%d from=%d,%d to=%d,%d player=%d,%d turn=%ld\n", gbt_tag, m_idx, (int)m_ptr->r_idx, ox, oy,
            nx, ny, px, py, (long)turn);
    return 1;
}

/* ---- hold：敵の番を飛ばす ---- */

/*
 * 原作 `process_monsters()` は、敵ごとに毎ゲームターン `energy_need` から速度ぶんを引き、0 以下になった敵だけ
 * `process_monster()` を呼ぶ（動く・魔法を撃つ・殴るはすべてその中）。そこで、止める敵の `energy_need` を
 * 命令待ちのたびに `GBT_HOLD_ENERGY` に置き直す。1 回の命令待ちの間にゲームターンが進んでも 0 以下にならない
 * （プレイヤーが休憩で 80 ターン進んでも足りる量）。止める前の値を覚え、`off` で戻す。
 * 入口から命じた `cast`・`melee`・`place` は `energy_need` を見ないので働く。
 * 眠らせる・状態異常を足すといった、画面に出る副作用のある手は使わない。
 *
 * 止まるのは**自分の番の行動**だけである。原作は眠りの経過・状態異常の経過を `process_monsters_mtimed()` で
 * 別に進めるので、止めている敵でも、眠っていれば目を覚まし得る（「〜が目を覚ました。」のメッセージが出る）。
 * 目を覚ましても番は来ないので、動かない・魔法を撃たない・殴らないことは変わらない。
 */
#define GBT_HOLD_ENERGY 30000

typedef struct {
    s16b r_idx;   /* 止めた時点の種族（死んだ・変わったかの照合） */
    u16b token;   /* 止めた時点の mon_random_number（同じ番号に別の敵が入ったかの照合） */
    s16b saved;   /* 止める前の energy_need */
    s16b seen;    /* 直近の置き直しの直前に見えていた energy_need（止まっていたかの証拠） */
    byte held;
} gbt_hold_slot;

static gbt_hold_slot *gbt_hold = NULL;
static int gbt_hold_hi = 0;       /* 止めたことのある最大の m_idx */
static int gbt_hold_all = 0;      /* `hold all on` の間 1 */
static long gbt_hold_key = -1L;   /* 記録した階の識別 */

static long gbt_floor_key(void)
{
    return ((long)dun_level << 20) | ((long)(dungeon_type & 0xFF) << 12) | ((long)(p_ptr->inside_quest & 0x3FF) << 2) |
           (p_ptr->inside_arena ? 1L : 0L);
}

static int gbt_hold_alloc(void)
{
    if (gbt_hold == NULL) gbt_hold = (gbt_hold_slot *)calloc((size_t)max_m_idx + 1, sizeof(gbt_hold_slot));
    return gbt_hold != NULL;
}

static void gbt_hold_take(int i)
{
    monster_type *m_ptr = &m_list[i];
    gbt_hold_slot *h = &gbt_hold[i];

    h->held = 1;
    h->r_idx = m_ptr->r_idx;
    h->token = m_ptr->mon_random_number;
    h->saved = m_ptr->energy_need;
    h->seen = m_ptr->energy_need;
    if (i > gbt_hold_hi) gbt_hold_hi = i;
}

/*! 同じ敵のままなら、止める前の値へ戻す。記録は外す。 */
static void gbt_hold_release(int i)
{
    monster_type *m_ptr = &m_list[i];
    gbt_hold_slot *h = &gbt_hold[i];

    if (h->held && m_ptr->r_idx && (m_ptr->r_idx == h->r_idx) && (m_ptr->mon_random_number == h->token)) {
        m_ptr->energy_need = h->saved;
    }
    h->held = 0;
}

/*! 止めている敵の `energy_need` を置き直す。死んだ・別の敵に替わった・階を移った敵は記録から外す。 */
static void gbt_hold_sync(void)
{
    int i;
    int top;
    const long key = gbt_floor_key();

    if (gbt_hold == NULL) {
        if (!gbt_hold_all || !gbt_hold_alloc()) return;
    }
    if (key != gbt_hold_key) {
        /* 階を移った（敵の一覧が作り直された）。記録を捨てる。元の値は戻さない（敵ごと無くなっている） */
        memset(gbt_hold, 0, ((size_t)max_m_idx + 1) * sizeof(gbt_hold_slot));
        gbt_hold_hi = 0;
        gbt_hold_key = key;
    }
    top = (m_max > gbt_hold_hi + 1) ? m_max : gbt_hold_hi + 1;
    if (top > (int)max_m_idx) top = max_m_idx;
    for (i = 1; i < top; i++) {
        monster_type *m_ptr = &m_list[i];
        gbt_hold_slot *h = &gbt_hold[i];

        if (h->held && (!m_ptr->r_idx || (m_ptr->r_idx != h->r_idx) || (m_ptr->mon_random_number != h->token))) h->held = 0;
        if (!h->held && gbt_hold_all && m_ptr->r_idx) gbt_hold_take(i);
        if (h->held) {
            h->seen = m_ptr->energy_need;
            m_ptr->energy_need = GBT_HOLD_ENERGY;
        }
    }
}

/*! `hold <nearest|m_idx|all> on|off`。成功したら 1。 */
static int gbt_hold_cmd(const char *target, const char *mode)
{
    int on;
    int idx;
    int i;
    int n = 0;

    if (mode == NULL) return gbt_fail("hold-needs-target-and-on-off", NULL);
    if (gbt_stricmp(mode, "on") == 0) {
        on = 1;
    } else if (gbt_stricmp(mode, "off") == 0) {
        on = 0;
    } else {
        return gbt_fail("bad-hold-mode %s", mode);
    }
    if (!gbt_hold_alloc()) return gbt_fail("out-of-memory", NULL);
    if (gbt_hold_key != gbt_floor_key()) gbt_hold_sync(); /* 階が変わっていたら先に捨てる */

    if (gbt_stricmp(target, "all") == 0) {
        gbt_hold_all = on;
        if (on) {
            gbt_hold_sync();
        } else {
            for (i = 1; i <= gbt_hold_hi && i < (int)max_m_idx; i++) {
                if (gbt_hold[i].held) {
                    gbt_hold_release(i);
                    n++;
                }
            }
        }
        for (i = 1; on && (i <= gbt_hold_hi) && (i < (int)max_m_idx); i++) n += gbt_hold[i].held ? 1 : 0;
        fprintf(stderr, "%s info hold all state=%s count=%d turn=%ld\n", gbt_tag, on ? "on" : "off", n, (long)turn);
        return 1;
    }

    idx = gbt_resolve_target(target);
    if (!idx) return 0;
    if (on) {
        if (!gbt_hold[idx].held) {
            gbt_hold_take(idx);
            m_list[idx].energy_need = GBT_HOLD_ENERGY;
        }
    } else {
        if (gbt_hold_all) return gbt_fail("hold-all-is-on", NULL);
        if (!gbt_hold[idx].held) return gbt_fail("not-held", NULL);
        gbt_hold[idx].seen = m_list[idx].energy_need;
        gbt_hold_release(idx);
        fprintf(stderr, "%s info hold m_idx=%d r_idx=%d state=off saved=%d now=%d turn=%ld pos=%d,%d player=%d,%d\n", gbt_tag, idx,
                (int)m_list[idx].r_idx, (int)m_list[idx].energy_need, (int)gbt_hold[idx].seen, (long)turn, (int)m_list[idx].fx,
                (int)m_list[idx].fy, px, py);
        return 1;
    }
    fprintf(stderr, "%s info hold m_idx=%d r_idx=%d state=on saved=%d now=%d turn=%ld pos=%d,%d player=%d,%d\n", gbt_tag, idx,
            (int)m_list[idx].r_idx, (int)gbt_hold[idx].saved, (int)gbt_hold[idx].seen, (long)turn, (int)m_list[idx].fx, (int)m_list[idx].fy, px,
            py);
    return 1;
}

/*! 命令 1 行を実行する。結果を標準エラーへ出す。成功したら 1。 */
static int gbt_run_line(char *line)
{
    char *tok[4];
    int ntok = 0;
    char *p = line;
    char full[160];
    int ok = 0;
    int idx;

    snprintf(full, sizeof(full), "%s", line);
    while (*p && (ntok < 4)) {
        while ((*p == ' ') || (*p == '\t')) p++;
        if (!*p) break;
        tok[ntok++] = p;
        while (*p && (*p != ' ') && (*p != '\t')) p++;
        if (*p) *p++ = '\0';
    }
    if (!ntok) return 1;

    gbt_why[0] = '\0';
    if (!character_dungeon || !p_ptr->playing || p_ptr->is_dead || p_ptr->leaving) {
        (void)gbt_fail("player-not-in-play", NULL);
    } else if (gbt_stricmp(tok[0], "cast") == 0) {
        const gbt_spell_def *sp;

        if (ntok < 2) {
            (void)gbt_fail("cast-needs-spell-name", NULL);
        } else if ((sp = gbt_find_spell(tok[1])) == NULL) {
            (void)gbt_fail("unknown-spell %s", tok[1]);
        } else if ((idx = gbt_resolve_target((ntok >= 3) ? tok[2] : NULL)) != 0) {
            ok = gbt_cast(idx, sp);
        }
    } else if (gbt_stricmp(tok[0], "melee") == 0) {
        if ((idx = gbt_resolve_target((ntok >= 2) ? tok[1] : NULL)) != 0) {
            ok = gbt_melee(idx);
        }
    } else if (gbt_stricmp(tok[0], "place") == 0) {
        int dx = 0;
        int dy = 0;

        if (ntok < 4) {
            (void)gbt_fail("place-needs-target-dx-dy", NULL);
        } else if (!gbt_parse_int(tok[2], &dx) || !gbt_parse_int(tok[3], &dy)) {
            (void)gbt_fail("bad-offset %s", tok[2]);
        } else if ((idx = gbt_resolve_target(tok[1])) != 0) {
            ok = gbt_place(idx, dx, dy);
        }
    } else if (gbt_stricmp(tok[0], "hold") == 0) {
        if (ntok < 3) {
            (void)gbt_fail("hold-needs-target-and-on-off", NULL);
        } else {
            ok = gbt_hold_cmd(tok[1], tok[2]);
        }
    } else {
        (void)gbt_fail("unknown-command %s", tok[0]);
    }

    if (ok) {
        fprintf(stderr, "%s ok %s\n", gbt_tag, full);
    } else {
        fprintf(stderr, "%s fail %s\n", gbt_tag, gbt_why);
    }
    fflush(stderr);
    return ok;
}

/*!
 * @brief 命令ファイルがあれば読んで実行する（`host_next_key()` から、命令待ちのときだけ呼ばれる）。
 * @return 待っているコアを起こすキー（死亡・階の移動のとき ESC）。要らなければ 0
 */
int gb_test_monster_act_poll(void)
{
    static int checked = 0;
    static const char *path = NULL;
    static int busy = 0;
    static int broken = 0;
    char text[16384];
    char moved[1100];
    size_t n;
    FILE *fp;
    char *line;
    char *next;
    int skipping = 0;
    int too_big;

    if (!checked) {
        path = getenv("HD2D_TEST_MONSTER_ACT");
        if ((path != NULL) && (path[0] == '\0')) path = NULL;
        checked = 1;
    }
    if ((path == NULL) || busy || broken) return 0;
    if (!gb_awaiting_command() || !character_dungeon || !p_ptr->playing) return 0;

    /* 止めている敵の番を飛ばす（命令待ちのたびに置き直す） */
    gbt_hold_sync();

    fp = fopen(path, "rb");
    if (fp == NULL) return 0;
    n = fread(text, 1, sizeof(text) - 1, fp);
    too_big = (n == sizeof(text) - 1) && (fgetc(fp) != EOF);
    fclose(fp);
    if (n == 0) return 0; /* 空（書きかけ）は読まない */
    text[n] = '\0';

    /* 先に消す。消せなければ名前を変える。どちらもできなければ、同じ命令を繰り返さないよう実行しない */
    if (remove(path) != 0) {
        snprintf(moved, sizeof(moved), "%s.done", path);
        (void)remove(moved);
        if (rename(path, moved) != 0) {
            fprintf(stderr, "%s fail cannot-consume-command-file\n", gbt_tag);
            fflush(stderr);
            broken = 1;
            return 0;
        }
    }
    if (too_big) {
        fprintf(stderr, "%s fail command-file-too-large\n", gbt_tag);
        fflush(stderr);
        return 0;
    }

    /* 上から順に、1 回の命令待ちの中で全部実行する（ゲームの時間は進めない）。fail した行より後は実行せず skip を出す。
     * 空行と `#` で始まる行は読み飛ばす */
    busy = 1;
    for (line = text; line != NULL && *line; line = next) {
        char *p;

        next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        n = strlen(line);
        while ((n > 0) && ((line[n - 1] == '\r') || (line[n - 1] == ' ') || (line[n - 1] == '\t'))) line[--n] = '\0';
        p = line;
        while ((*p == ' ') || (*p == '\t')) p++;
        if ((*p == '\0') || (*p == '#')) continue;
        if (skipping) {
            fprintf(stderr, "%s skip %s\n", gbt_tag, p);
            fflush(stderr);
            continue;
        }
        if (!gbt_run_line(p)) skipping = 1;
    }
    busy = 0;

    return (p_ptr->is_dead || p_ptr->leaving) ? ESCAPE : 0;
}
