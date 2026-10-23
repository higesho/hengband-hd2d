/*!
 * @file tb_shim.c
 * @brief 境界の実装のうち「コアの名札」と「ゲーム状態の読み口」を持つ TU（設計 §3）。
 *
 * ここは**TOband のヘッダを include してよい場所**である（`tb_null_term.c` /
 * `tb_bootstrap.c` も同族）。C++ からは `tb_shim.h` 越しにしか見えない。
 *
 * ## 文字コード（adapter/ 全体の約束）
 * このファイルは **UTF-8（BOM 付き）**。`toband/src/` の写しは **SJIS** のまま。
 * vcxproj は `/source-charset` を足していないので、MSVC は既定で ACP（932）として読み、
 * **BOM のあるファイルだけ UTF-8 として読む**。だから 2 つが同居できる。
 * - `toband/src/` に charset 指定を足してはいけない（写しが丸ごと化ける）。
 * - `toband/adapter/` から BOM を落としてはいけない（コメントが化ける）。
 *
 * 実行時文字コード（execution-charset）は指定していないので **ACP = CP932**。
 * つまり adapter に日本語のリテラルを書くと SJIS で焼かれ、コアの内部コードと揃う。
 * **この TU が返す文字列はすべて SJIS のバイト列**であり、UTF-8 化は C++ 側
 * （`tb_text.h`）の仕事である。v1 は UTF-8 で喋る（設計 §3.1）。
 */

#include "angband.h"
#include "party.h" /* `party_has_successor()`（死亡が仲間の交代で済むか。`tb_screen_flags`） */

#include "tb_lang_c.h"
#include "tb_shim.h"
#include "tb_unaware_category.h"
#include "bridge/unaware_item_categories.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 版は defines.h の FAKE_VER_* から組む。
 *
 *   #define FAKE_VER_MAJOR 2
 *   #define FAKE_VER_MINOR 7
 *   #define FAKE_VER_PATCH 0
 *
 * 数字の直書きはしない──写しを取り込み直したら自動で追随させるため。
 * （変愚蛮怒の版表記「FAKE_VER_MAJOR-10」ではなく、TOband2 の版 "2.7.0" をそのまま使う。
 *   `VERSION_NAME` は "TOband-R3"。）
 */
#define TB_STRINGIFY_(x) #x
#define TB_STRINGIFY(x) TB_STRINGIFY_(x)
#define TB_VERSION_STRING     TB_STRINGIFY(FAKE_VER_MAJOR) "." TB_STRINGIFY(FAKE_VER_MINOR) "." TB_STRINGIFY(FAKE_VER_PATCH)

const char *tb_core_name(void)
{
    return "toband";
}

const char *tb_core_version(void)
{
    return TB_VERSION_STRING;
}

/*
 * 誕生画面の種族ごとの「スコア倍率」。幻想蛮怒の `player_race` には `score_mult` があるが、
 * TOband には無い。呼び手（`tb_main.cpp`）は -1 を「不明」として扱う（倍率の添え書きを出さない）。
 */
int tb_birth_race_score_mult(int race_index)
{
    (void)race_index;
    return -1;
}

const char *tb_birth_race_title(int race_index)
{
    (void)race_index;
    return NULL;
}

int tb_birth_race_score_mult_named(const char *title)
{
    (void)title;
    return -1;
}

/* ==================================================================== 小さな道具 */

static void tb_copy_str(char *dst, int cap, cptr src)
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

int tb_character_generated(void)
{
    return character_generated ? 1 : 0;
}

/* モンスター・所持品・床の物の配列から、添字の要素を指すポインタを返す（添字の範囲は呼び出し側が守る）。 */
static monster_type *tb_monster_slot(int idx)
{
    return &m_list[idx];
}

static object_type *tb_pack_slot(int idx)
{
    return &inventory[idx];
}

static object_type *tb_floor_obj(int idx)
{
    return &o_list[idx];
}

int tb_rogue_like_commands(void)
{
    return rogue_like_commands ? 1 : 0;
}

/* ================================== UI 既定パネルの中身（M1 / S1 の対） */

int tb_read_item_line(int what, int index, char *out, int cap)
{
    /* 装備の枠名。変愚の `fill_sub2_equipment()` と同じ 2 文字。
       並びは `defines.h` の INVEN_RARM から（TOband は弓枠 BW が在り、指輪が左右 2 枠）。 */
    static const char *kSlot[] = {
        "MH", "SH", "BW", "RR", "LR", "NK", "LT", "BD", "OT", "HD", "AR", "FT"
    };
    int from;
    int to;
    int i;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return TB_ERR_ARG;
    }
    out[0] = 0;
    if (!tb_character_generated()) {
        return 0;
    }

    if (what == TB_ITEMS_EQUIPMENT) {
        from = INVEN_RARM;
        to = INVEN_TOTAL;
    } else {
        from = 0;
        to = INVEN_PACK;
    }

    for (i = from; i < to; i++) {
        object_type *o_ptr = tb_pack_slot(i);
        char name[MAX_NLEN];
        char line[MAX_NLEN + 8];

        if (!o_ptr->k_idx) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        object_desc(name, o_ptr, OD_NAME_ONLY | OD_OMIT_PREFIX);
        if (what == TB_ITEMS_EQUIPMENT) {
            const int slot = i - INVEN_RARM;
            sprintf(line, "%s %s", (slot >= 0 && slot < 12) ? kSlot[slot] : "??", name);
        } else {
            sprintf(line, "%c) %s", (char)(0x61 + i), name);
        }
        tb_copy_str(out, cap, line);
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
 * `tb_targeting.inl` の `_tb_target_rng_save` と同じ内訳。
 */
typedef struct tb_rng_snapshot {
    u16b place;
    u32b state[RAND_DEG];
} tb_rng_snapshot;

static void tb_rng_save(tb_rng_snapshot *snap)
{
    snap->place = Rand_place;
    memcpy(snap->state, Rand_state, sizeof(snap->state));
}

static void tb_rng_restore(const tb_rng_snapshot *snap)
{
    Rand_place = snap->place;
    memcpy(Rand_state, snap->state, sizeof(snap->state));
}

/*
 * 検査用（環境変数が無ければ何もしない）。`HD2D_TEST_RNG=1` で、呼び出しの間に乱数の状態が
 * どう動いたか（inside）と戻した後（kept）の要約値を標準エラーへ出す。`HD2D_TEST_RNG=nofix` は
 * 戻さない（この直しの前の動きを再現する）。`HD2D_TEST_FORCE_IMAGE=1` は毎回幻覚にする。
 */
static unsigned tb_rng_digest(void)
{
    unsigned h = 2166136261u;
    int i;

    h = (h ^ (unsigned)Rand_place) * 16777619u;
    for (i = 0; i < RAND_DEG; i++) {
        h = (h ^ (unsigned)Rand_state[i]) * 16777619u;
    }
    return h;
}

static void tb_test_force_image(void)
{
    if (getenv("HD2D_TEST_FORCE_IMAGE") != NULL) {
        p_ptr->image = 1000;
    }
}

static void tb_rng_finish(const tb_rng_snapshot *snap, const char *name)
{
    const char *mode = getenv("HD2D_TEST_RNG");
    const int report = (mode != NULL) && (mode[0] != '\0');
    const unsigned inside = report ? tb_rng_digest() : 0;

    if (!(report && (strcmp(mode, "nofix") == 0))) {
        tb_rng_restore(snap);
    }
    if (report) {
        fprintf(stderr, "[hd2d-rng] %s inside=%08x kept=%08x\n", name, inside, tb_rng_digest());
    }
}

static int tb_read_visible_monster_raw(int index, char *out, int cap)
{
    int i;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return TB_ERR_ARG;
    }
    out[0] = 0;
    if (!character_generated || !character_dungeon) {
        return 0;
    }

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = tb_monster_slot(i);
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
        tb_copy_str(out, cap, name);
        return (int)strlen(out);
    }
    return 0;
}

int tb_read_visible_monster(int index, char *out, int cap)
{
    tb_rng_snapshot snap;
    int result;

    tb_rng_save(&snap);
    tb_test_force_image();
    result = tb_read_visible_monster_raw(index, out, cap);
    tb_rng_finish(&snap, "tb_read_visible_monster");
    return result;
}

/* ================================================== 文字入力（M1 / S5） */

/*! `askfor_aux()` の待ちの間だけ 1（`tb_shim.h` の `tb_text_input_set`）。 */
static int tb_text_input_flag = 0;

void tb_text_input_set(int active)
{
    tb_text_input_flag = active ? 1 : 0;
}

int tb_text_input_active(void)
{
    return tb_text_input_flag;
}

/* ============================================ 戦闘の見せ場（M1 / S10・R4） */

/*! 溜め場。ゲームスレッドしか触らない（capture も同じスレッド）ので鍵は要らない。 */
static tb_fx_event tb_fx_ring[TB_FX_MAX];
static int tb_fx_num = 0;
/*! 宣言中の属性（`tb_fx_set_pending`）。-1 は「宣言なし＝物理」。 */
static int tb_fx_pending = -1;

/*! 1 件積む。**あふれたら古いほうを捨てる**（新しい出来事のほうが見たい）。 */
static void tb_fx_push(const tb_fx_event *ev)
{
    int i;

    if (tb_fx_num >= TB_FX_MAX) {
        for (i = 1; i < TB_FX_MAX; i++) {
            tb_fx_ring[i - 1] = tb_fx_ring[i];
        }
        tb_fx_num = TB_FX_MAX - 1;
    }
    tb_fx_ring[tb_fx_num++] = *ev;
}

void tb_fx_set_pending(int typ)
{
    tb_fx_pending = typ;
}

void tb_fx_player_hit(int damage, int mhp, int melee)
{
    tb_fx_event ev;

    (void)melee; /* いまの器は近接かどうかを持たない（変愚も色は属性だけで決める） */
    if (damage <= 0) {
        tb_fx_pending = -1;
        return;
    }
    ev.kind = 0;
    ev.typ = tb_fx_pending;
    ev.y = py;
    ev.x = px;
    ev.src_y = py;
    ev.src_x = px;
    ev.num = damage;
    ev.den = (mhp > 0) ? mhp : damage;
    tb_fx_push(&ev);

    /* 宣言は 1 回きり。次の被弾へ持ち越さない。 */
    tb_fx_pending = -1;
}

void tb_fx_monster_hit(int y, int x, int dam, int max_hp)
{
    tb_fx_event ev;

    if (dam <= 0) {
        return;
    }
    ev.kind = 1;
    ev.typ = tb_fx_pending;
    ev.y = y;
    ev.x = x;
    ev.src_y = y;
    ev.src_x = x;
    ev.num = dam;
    ev.den = (max_hp > 0) ? max_hp : dam;
    tb_fx_push(&ev);

    /*
     * **宣言はここでも消す。**残したままにすると、次に来る素手の打撃が前の呪文の
     * 属性で色づく。宣言するのは `project_m()` / `project_p()` /
     * `make_attack_normal()` の 3 か所で、いずれも「その直後の 1 件」のためである。
     */
    tb_fx_pending = -1;
}

void tb_fx_bolt(int src_y, int src_x, int y, int x, int typ)
{
    tb_fx_event ev;

    ev.kind = 2;
    ev.typ = typ;
    ev.y = y;
    ev.x = x;
    ev.src_y = src_y;
    ev.src_x = src_x;
    ev.num = 1;
    ev.den = 1;
    tb_fx_push(&ev);
}

int tb_fx_element_of(int typ)
{
    /*
     * TOband の GF_xxx は 100 種ほどで、幻想蛮怒より少ない。束は 10 種しかない
     * （`presentation/frame/combat_fx.h`）ので、属性として明らかなものだけを振り、
     * 残りは白（物理）に残す。当てずっぽうで色を付けない。M0 では呼び出し元（原作への観測の
     * 呼び出し）が無いので、使われるのは M1 以降。
     */
    switch (typ) {
    case GF_FIRE:
    case GF_PLASMA:
    case GF_METEOR:
    case GF_HELL_FIRE:
    case GF_ROCKET:
    case GF_LAVA_FLOW:
    case GF_VOLCANIC_BOMB:
    case GF_PURE_FIRE:
        return 1; /* 火 */
    case GF_COLD:
    case GF_ICE:
    case GF_WATER:
    case GF_WATER_FLOW:
    case GF_PURE_AQUA:
        return 2; /* 冷（水もここへ） */
    case GF_ELEC:
        return 3; /* 電 */
    case GF_ACID:
        return 4; /* 酸 */
    case GF_POIS:
    case GF_NUKE:
        return 5; /* 毒（核熱もここへ） */
    case GF_DARK:
    case GF_DARK_WEAK:
    case GF_NETHER:
    case GF_DEATH_RAY:
        return 6; /* 闇 */
    case GF_LITE:
    case GF_LITE_WEAK:
    case GF_HOLY_FIRE:
    case GF_GODLY_SPEAR:
    case GF_SHINING:
        return 7; /* 光 */
    case GF_CHAOS:
    case GF_CONFUSION:
    case GF_DISENCHANT:
        return 8; /* 混沌 */
    case GF_TIME:
    case GF_GRAVITY:
        return 10; /* 時空 */
    case GF_PSI:
    case GF_MIND_BLAST:
    case GF_BRAIN_SMASH:
        return 9; /* 精神 */
    default:
        return 0;
    }
}

int tb_fx_pending_count(void)
{
    return tb_fx_num;
}

int tb_fx_take(tb_fx_event *out, int cap)
{
    int n;
    int i;

    if (!out || (cap <= 0)) {
        tb_fx_num = 0;
        return 0;
    }
    n = (tb_fx_num < cap) ? tb_fx_num : cap;
    for (i = 0; i < n; i++) {
        out[i] = tb_fx_ring[i];
    }
    tb_fx_num = 0;
    return n;
}

/* ==================================================================== 魔法・投射物の効果（projections / missiles） */

/*
 * `GameFrame::projections`（飛ぶ魔法・ボール・ブレス・ビーム・視界内の全員）と `GameFrame::missiles`（矢弾・投擲）の材料
 * （決めたこと 2026-10-17。幻想蛮怒の `gb_fx_*` と同じ作り）。
 *
 * 原作の `project()` などに足した**観測の呼び出し**（`tools/targeting/attack_fx_overlay.py` の `toband_project`・`toband_cmd2`。
 * 計 6 行。記録するだけで遊びの結果は変えない）がここを呼ぶ。ここは原作の型（`GF_xxx`・`PROJECT_xxx`・物）を読んで、
 * 属性のコア名・形・範囲・経路・旗の整数に直し、共通の記録の置き場（`presentation/bridge/named_attack_fx.h`）へ
 * `tb_named_fx_*`（tb_frame.cpp）経由で積む。属性のコア名から見た目の名前への変換は C++ 側
 * （`attack_fx_core_attr_aliases.h` の `toband_look_name`）。
 *
 * 旧来の `combat_fx` の飛道（`tb_fx_bolt`）は呼び元が無く、`tb_frame.cpp` も積まない（上の記録と二重に出ない）。
 */
#include "tb_fx_names.inc"

/*! `presentation/frame/attack_fx.h` の `ProjectionShape` / `AttackFxOrigin` / `ProjectionFxFlag` / `MissileKind` と同じ並び。 */
enum { TB_FXS_BOLT = 0, TB_FXS_BEAM = 1, TB_FXS_BALL = 2, TB_FXS_BREATH = 3, TB_FXS_JUMP = 4 };
enum { TB_FXO_PLAYER = 0, TB_FXO_MONSTER = 1, TB_FXO_OTHER = 2 };
enum { TB_FXF_HIDDEN = 1, TB_FXF_BLIND = 2, TB_FXF_REFLECTED = 4, TB_FXF_ALL_IN_SIGHT = 8 };
enum { TB_FXM_ARROW = 0, TB_FXM_BOLT = 1, TB_FXM_SHOT = 2, TB_FXM_THROWN = 3 };

/*! 1 件に積むマスの上限（`named_attack_fx.h` の `kNamedMaxCells` と同じ）。 */
#define TB_FX_CELLS 512

static const char *tb_fx_gf_name(int typ)
{
    size_t i;

    if (typ <= 0) {
        return "";
    }
    for (i = 0; i < sizeof(tb_fx_gf_names) / sizeof(tb_fx_gf_names[0]); i++) {
        if (tb_fx_gf_names[i].typ == typ) {
            return tb_fx_gf_names[i].name;
        }
    }
    return "";
}

static int tb_fx_origin_of(int who)
{
    return (who == 0) ? TB_FXO_PLAYER : ((who > 0) ? TB_FXO_MONSTER : TB_FXO_OTHER);
}

/*! 直前の投射の要約（反射の見分け用。反射は `project()` の中で入れ子に呼ばれるので、記録の並びで見分ける）。 */
static struct {
    int valid;
    int reflectable;
    int grids;
    int typ;
    int impact_y, impact_x;
    int flg;
} tb_fx_prev;

/*!
 * 反射の見分けで無視する旗。モンスターが撃ち返す `project()` は、原作が旗を `~PROJECT_MONSTER` にし
 * `PROJECT_THRU | PROJECT_PLAYER | PROJECT_REFLECTABLE` を足して呼ぶ。
 */
#define TB_FX_REFLECT_IGNORED (PROJECT_MONSTER | PROJECT_THRU | PROJECT_PLAYER | PROJECT_REFLECTABLE)

void tb_fx_note_projection(int who, int y1, int x1, int by, int bx, int typ, int rad, int flg, int blind, int breath, int jump,
    const u16b *path_g, int path_n, const byte *gy, const byte *gx, int grids)
{
    static int path[TB_FX_CELLS * 2];
    static int area[TB_FX_CELLS * 3];
    int shape;
    int flags = 0;
    int pn = 0;
    int an = 0;
    int i;
    int impact_y = by;
    int impact_x = bx;
    const int hidden = (flg & PROJECT_HIDE) ? 1 : 0;
    const int origin = tb_fx_origin_of(who);

    /* 範囲を数えるだけの内部の印（元素の増減・水の流れの下ごしらえ）。見える魔法ではないので記録しない。 */
    if (typ == GF_CAVE_TEMP) {
        return;
    }

    if (breath) {
        shape = TB_FXS_BREATH;
    } else if (flg & PROJECT_BEAM) {
        shape = TB_FXS_BEAM;
    } else if (jump || (path_n <= 0) || ((y1 == by) && (x1 == bx))) {
        shape = TB_FXS_JUMP;
    } else if (rad > 0) {
        shape = TB_FXS_BALL;
    } else {
        shape = TB_FXS_BOLT;
    }

    /* 経路：始点の次のマスから着弾点まで（ボルト・ボールは `path_n` が着弾点で切ってある。ビーム・ブレスは全部） */
    if (shape != TB_FXS_JUMP) {
        for (i = 0; (i < path_n) && (pn < TB_FX_CELLS); i++) {
            path[pn * 2] = GRID_Y(path_g[i]);
            path[pn * 2 + 1] = GRID_X(path_g[i]);
            pn++;
        }
        if ((shape == TB_FXS_BEAM) && (pn > 0)) {
            impact_y = path[(pn - 1) * 2];
            impact_x = path[(pn - 1) * 2 + 1];
        }
    }

    /* 範囲：プレイヤの視界内のマスだけ（隠す投射は全部）。距離は中心（ブレスは撃った者）から。ビームは 0。 */
    for (i = 0; (i < grids) && (an < TB_FX_CELLS); i++) {
        const int y = gy[i];
        const int x = gx[i];
        int d = 0;

        /* ビームの範囲の先頭には撃った者のマスが入っている（原作の「ビームのマスを集める」の最初の 1 マス）。経路に無いので数えない */
        if ((shape == TB_FXS_BEAM) && (y == y1) && (x == x1)) {
            continue;
        }
        if (!hidden && !player_has_los_bold(y, x)) {
            continue;
        }
        if (shape == TB_FXS_BREATH) {
            d = distance(y1, x1, y, x);
        } else if (shape != TB_FXS_BEAM) {
            d = distance(by, bx, y, x);
        }
        area[an * 3] = (d < 0) ? 0 : ((d > 255) ? 255 : d);
        area[an * 3 + 1] = y;
        area[an * 3 + 2] = x;
        an++;
    }

    if (hidden) {
        flags |= TB_FXF_HIDDEN;
    }
    if (blind) {
        flags |= TB_FXF_BLIND;
    }
    /* 視界内の全員（`project_hack()`）：プレイヤが飛び先指定・当てる・隠す、で半径 0 */
    if ((who == 0) && jump && (rad == 0) && !breath && (flg == (PROJECT_KILL | PROJECT_HIDE))) {
        flags |= TB_FXF_ALL_IN_SIGHT;
    }
    /* 反射：直前が反射できる 1 マスの投射で、そのマスからモンスターが同じ属性を撃ち返した */
    if (tb_fx_prev.valid && tb_fx_prev.reflectable && (tb_fx_prev.grids <= 1) && (origin == TB_FXO_MONSTER) && (rad == 0)
        && (typ == tb_fx_prev.typ) && (y1 == tb_fx_prev.impact_y) && (x1 == tb_fx_prev.impact_x)
        && ((flg & ~TB_FX_REFLECT_IGNORED) == (tb_fx_prev.flg & ~TB_FX_REFLECT_IGNORED))) {
        flags |= TB_FXF_REFLECTED;
    }
    tb_fx_prev.valid = 1;
    tb_fx_prev.reflectable = (flg & PROJECT_REFLECTABLE) ? 1 : 0;
    tb_fx_prev.grids = grids;
    tb_fx_prev.typ = typ;
    tb_fx_prev.impact_y = impact_y;
    tb_fx_prev.impact_x = impact_x;
    tb_fx_prev.flg = flg;

    tb_named_fx_projection(origin, shape, tb_fx_gf_name(typ), rad, y1, x1, impact_y, impact_x, flags, path, pn, area, an);
}

/*! 飛ばした物の基本アイテムの番号。名前を知らない物（正体の分からないアイテム）は正体を漏らさないので 0。 */
static int tb_fx_item_id(const object_type *o_ptr)
{
    if (!o_ptr || !o_ptr->k_idx) {
        return 0;
    }
    if (tb_unaware_category(o_ptr) != 0) {
        return 0;
    }
    return o_ptr->k_idx;
}

/*!
 * 矢弾の属性：**識別済みのものだけ**（原作より情報を増やさない）。プレイヤ自身が選んだ技（`shot_flgs` の稲妻射ち・風の矢）と、
 * 矢弾・弓の識別済みの旗（`object_flags_known`）から決める。理力は原作と同じ MP の条件（`csp > msp / 30`）を満たすときだけ。
 */
static const char *tb_fx_known_brand(const object_type *ammo, const object_type *launcher, int shot_flgs)
{
    u32b fa[TR_FLAG_SIZE];
    u32b fl[TR_FLAG_SIZE];

    if (shot_flgs & DCFA_STORM) {
        return "elec";
    }
    if (shot_flgs & DCFA_PURE_WIND) {
        return "force";
    }
    object_flags_known((object_type *)ammo, fa);
    object_flags_known((object_type *)launcher, fl);
#define TB_FX_KNOWN(flag) (have_flag(fa, (flag)) || have_flag(fl, (flag)))
    if (TB_FX_KNOWN(TR_BRAND_FIRE)) {
        return "fire";
    }
    if (TB_FX_KNOWN(TR_BRAND_COLD)) {
        return "cold";
    }
    if (TB_FX_KNOWN(TR_BRAND_ELEC)) {
        return "elec";
    }
    if (TB_FX_KNOWN(TR_BRAND_ACID)) {
        return "acid";
    }
    if (TB_FX_KNOWN(TR_BRAND_POIS)) {
        return "pois";
    }
    if (TB_FX_KNOWN(TR_FORCE_WEAPON) && (p_ptr->csp > (p_ptr->msp / 30))) {
        return "mana";
    }
#undef TB_FX_KNOWN
    return "";
}

/*!
 * 射撃 1 歩（`do_cmd_fire_aux()`）。`cur_dis == 1` が 1 発の始まり。盲目のときは記録しない（原作も見えないマスの動きは描かない）。
 * 矢（弓）と クロスボウの矢だけ別の種類。銃の弾・ロケット・小石は「弾」の種類で出す。
 */
void tb_fx_shot(int cur_dis, int y, int x, int prev_y, int prev_x, const object_type *ammo, const object_type *launcher, int shot_flgs)
{
    int kind = TB_FXM_SHOT;

    if (p_ptr->blind || !ammo) {
        return;
    }
    if (ammo->tval == TV_ARROW) {
        kind = TB_FXM_ARROW;
    } else if (ammo->tval == TV_BOLT) {
        kind = TB_FXM_BOLT;
    }
    tb_named_fx_missile_step((cur_dis <= 1) ? 1 : 0, kind, tb_fx_item_id(ammo),
        (cur_dis <= 1) ? tb_fx_known_brand(ammo, launcher, shot_flgs) : "", prev_y, prev_x, y, x);
}

/*! 投擲 1 歩（`do_cmd_throw_aux()`）。 */
void tb_fx_throw(int cur_dis, int y, int x, int prev_y, int prev_x, const object_type *obj)
{
    if (p_ptr->blind || !obj) {
        return;
    }
    tb_named_fx_missile_step((cur_dis <= 1) ? 1 : 0, TB_FXM_THROWN, tb_fx_item_id(obj), "", prev_y, prev_x, y, x);
}

/*! 射撃・投擲が相手に当たった。 */
void tb_fx_hit(void)
{
    if (p_ptr->blind) {
        return;
    }
    tb_named_fx_missile_hit();
}

/*! 投げた物が戻ってくる（ブーメランなど。原作が戻りの動きを描く所）。 */
void tb_fx_throw_return(void)
{
    if (p_ptr->blind) {
        return;
    }
    tb_named_fx_missile_returns();
}

/* ==================================================================== 効果音 */

/*
 * 建付けは `tb_shim.h` の同名の節のとおり。積むのは `tb_null_term.c` の
 * `TERM_XTRA_SOUND`、汲むのは `tb_frame.cpp`。**汲んだら消える**（音は状態ではなく
 * 一度きりの出来事。 ）。
 *
 * 溢れたら**古いほうを捨てる**。ゲームスレッドしか触らないので鍵は要らない
 * （上の見せ場の環と同じ）。
 */

#define TB_SOUND_MAX 32

static tb_sound_event tb_sound_ring[TB_SOUND_MAX];
static int tb_sound_num = 0;
static unsigned long tb_sound_pushed_total = 0;
static int tb_sound_wanted_flag = 0;

void tb_sound_set_wanted(int wanted)
{
    tb_sound_wanted_flag = wanted ? 1 : 0;
    if (!tb_sound_wanted_flag) {
        /* 欲しがらなくなったら溜まっているぶんも捨てる（古い音が後で鳴らないように）。 */
        tb_sound_num = 0;
    }
}

int tb_sound_wanted(void)
{
    return tb_sound_wanted_flag;
}

void tb_sound_push(int v)
{
    tb_sound_event *ev;
    cptr name;
    int i;

    if (!tb_sound_wanted_flag) {
        return;
    }
    if ((v <= 0) || (v >= SOUND_MAX)) {
        return;
    }
    name = angband_sound_name[v];
    if (!name || !name[0]) {
        return;
    }

    if (tb_sound_num >= TB_SOUND_MAX) {
        for (i = 1; i < TB_SOUND_MAX; i++) {
            tb_sound_ring[i - 1] = tb_sound_ring[i];
        }
        tb_sound_num = TB_SOUND_MAX - 1;
    }

    ev = &tb_sound_ring[tb_sound_num++];
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
    tb_sound_pushed_total++;
}

int tb_sound_take(tb_sound_event *out, int cap)
{
    int n;
    int i;

    if (!out || (cap <= 0)) {
        tb_sound_num = 0;
        return 0;
    }
    n = (tb_sound_num < cap) ? tb_sound_num : cap;
    for (i = 0; i < n; i++) {
        out[i] = tb_sound_ring[i];
    }
    tb_sound_num = 0;
    return n;
}

unsigned long tb_sound_total(void)
{
    return tb_sound_pushed_total;
}

/* ============================== サブウィンドウの割り当て世代（M1 / R1） */

/*!
 * `=`→`w` の画面で割り当てが変わった回数（`tb_shim.h` の `tb_window_flags_generation`）。
 * **`window_flag[]` の差分では見分けられない**――あの配列を書くのは遊ぶ側だけではなく、
 * `toggle_inven_equip()`（`object1.c:4036`）が床の品物を選ぶたびに持ち物と装備を
 * 全窓で入れ替える。差分で拾うと歩くだけで振動する（変愚が実際に踏んだ穴)。
 */
static unsigned int tb_window_flags_gen = 0;

void tb_window_flags_bumped(void)
{
    tb_window_flags_gen++;
}

unsigned int tb_window_flags_generation(void)
{
    return tb_window_flags_gen;
}

/* ================================================== コマンドの表（M1 / S3） */

/*
 * **`menu_info` / `special_menu_info` の型は `externs.h` に出ていない**
 * （`util.c:4201` と `:4478` の中だけで typedef している）。配列そのものは
 * static ではないのでリンクはできる。そこで**同じ並びの型をここに写して** extern する。
 *
 * 写しなので、先方が構造体を変えたら黙って別物を読む。それを防ぐために
 * `tb_menu_table_sane()` で**表の実際の値**を確かめ、合わなければ 0 件を返す
 * （呼び手の `tb_pad_commands.cpp` が手書きの最小表へ落ちる）。
 * **`toband/src/` は 1 行も触らない**（設計 §1 制約 2）。
 */
typedef struct tb_menu_naiyou {
    cptr name;
    byte cmd;
    bool fin;
} tb_menu_naiyou;

typedef struct tb_special_menu_naiyou {
    cptr name;
    byte window;
    byte number;
    byte jouken;
    byte jouken_naiyou;
} tb_special_menu_naiyou;

extern tb_menu_naiyou menu_info[12][12];
extern tb_special_menu_naiyou special_menu_info[];

#define TB_MENU_CLASS 1 /* util.c:4488 の MENU_CLASS */
#define TB_MENU_WILD 2 /* 同 MENU_WILD */

/*!
 * @brief 型の写しが合っているかを、表の**実際の値**で確かめる。
 * @details 大きさが同じでも並びが違えば読み間違える。だから値で見る。
 * ここに挙げた 5 つは `util.c:4210` の表の頭と尻で、どれも意味のある値である。
 */
static int tb_menu_table_sane(void)
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
static unsigned char tb_key_for_command(int mode, unsigned char command)
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
static cptr tb_menu_name(int window, int number, cptr fallback)
{
    int i;

    for (i = 0;; i++) {
        if (!special_menu_info[i].name || !special_menu_info[i].name[0]) break;
        if ((int)special_menu_info[i].window != window) continue;
        if ((int)special_menu_info[i].number != number) continue;
        switch (special_menu_info[i].jouken) {
        case TB_MENU_CLASS:
            if (p_ptr->pclass == special_menu_info[i].jouken_naiyou) {
                fallback = special_menu_info[i].name;
            }
            break;
        case TB_MENU_WILD:
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

int tb_read_pad_commands(tb_pad_command *out, int cap)
{
    int n = 0;
    int g;

    if (!out || (cap <= 0)) {
        return TB_ERR_ARG;
    }
    if (!tb_menu_table_sane()) {
        return 0;
    }

    /* menu_info[0] は分類（fin == FALSE で cmd が小メニュー番号）。 */
    for (g = 0; g < 12; g++) {
        const tb_menu_naiyou *group = &menu_info[0][g];
        cptr group_name;
        int sub;
        int k;

        if (group->fin) continue;
        if (!group->name || !group->name[0]) continue;
        sub = (int)group->cmd;
        if ((sub <= 0) || (sub >= 12)) continue;
        /*
         * **英語ならカタログで引く**（フック #33。GR-01）。この名札は
         * `pad_commands` に載って画面側のコマンドメニューへ出るだけで、
         * **Term を 1 度も通らない**——だから #1 では拾えず、未訳一覧にも出ない。
         * 引けなければ原文が返るので、日本語のときは 1 ビットも変わらない。
         */
        group_name = tb_tr(tb_menu_name(0, g, group->name)); //GB: english layer #33

        for (k = 0; k < 12; k++) {
            const tb_menu_naiyou *item = &menu_info[sub][k];

            /* fin == TRUE の項目だけがコマンド（FALSE は小メニューへの分岐）。 */
            if (!item->fin) continue;
            if (!item->name || !item->name[0] || !item->cmd) continue;
            if (n >= cap) return n;

            memset(&out[n], 0, sizeof(out[n]));
            out[n].command = item->cmd;
            out[n].key_original = tb_key_for_command(KEYMAP_MODE_ORIG, item->cmd);
            out[n].key_rogue = tb_key_for_command(KEYMAP_MODE_ROGUE, item->cmd);
            tb_copy_str(out[n].group, (int)sizeof(out[n].group), group_name);
            tb_copy_str(out[n].label, (int)sizeof(out[n].label), tb_tr(tb_menu_name(sub, k, item->name))); //GB: english layer #33
            n++;
        }
    }
    return n;
}

unsigned char tb_pad_key_for_command(int rogue, int command)
{
    if ((command <= 0) || (command > 255)) {
        return 0;
    }
    return tb_key_for_command(rogue ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG, (unsigned char)command);
}

int tb_pad_table_stamp(void)
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
 * @brief 色表を 16 色ぶん写す（`tb_shim.h` の `tb_read_palette`）。
 * @details `angband_color_table[i]` は **4 バイト組で、先頭 1 バイトは使っていない**
 * （`variable.c:605` の並びは `{0x00, R, G, B}`）。だから添字は 1〜3 である。
 * 変愚の `fill_term_palette()`（`presentation_bridge.cpp:1018`）も同じ添字で読む。
 */
int tb_read_palette(unsigned char *rgb, int cap)
{
    int i;

    if (!rgb || (cap < 16 * 3)) {
        return TB_ERR_ARG;
    }
    for (i = 0; i < 16; i++) {
        rgb[i * 3 + 0] = angband_color_table[i][1];
        rgb[i * 3 + 1] = angband_color_table[i][2];
        rgb[i * 3 + 2] = angband_color_table[i][3];
    }
    return 16 * 3;
}

void tb_apply_cursor_mode(int enable)
{
    if (!enable) {
        return; /* 従来どおり（文字キー主体）。use_menu はコアに任せる */
    }
    if (!tb_character_generated()) {
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
static void tb_place_name(char *out, int cap)
{
    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!tb_character_generated()) {
        return;
    }

    if (dun_level && dungeon_type && d_info && d_name && (dungeon_type < max_d_idx)) {
        tb_copy_str(out, cap, d_name + d_info[dungeon_type].name);
        return;
    }

    if (town && (p_ptr->town_num > 0) && (p_ptr->town_num < max_towns)) {
        tb_copy_str(out, cap, town[p_ptr->town_num].name);
    }
}

/*!
 * @brief 「地上」「12 階」等。`xtra1.c:1546` の `prt_depth()` と**同じ文言**にする。
 * @details 画面側で組み直すと文言が割れるので、こちらで作って運ぶ。
 * 中身は SJIS（`_()` 相当の分岐はコアのビルド定義 `JP` に従う）。
 *
 * **英語のときは `#else` 側の文言をここで選ぶ**（設計 §5 の「アダプタ側」）。
 * カタログでは引けない——`sprintf` で数を埋めた後の字が来るので鍵に合わない
 * （E3 の通しで `15 階` が残って判った）。ここは `toband/src` の外なので
 * フックを増やさずに直せる。
 */
static void tb_depth_text(char *out, int cap)
{
    char buf[32];
    const int en = tb_lang_enabled();

    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!tb_character_generated()) {
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

    tb_copy_str(out, cap, buf);
}

int tb_read_hud(tb_hud_data *out)
{
    if (!out) {
        return TB_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));

    tb_copy_str(out->name, (int)sizeof(out->name), player_name);
    out->in_game = character_generated ? 1 : 0;

    /*
     * `p_ptr` は静的な実体（variable.c:894 `player_type *p_ptr = &p_body;`）なので
     * 常に読める。ただし `character_generated` が偽のあいだは中身が既定値のままで、
     * 誕生画面の途中経過（振り直し中の能力値など）を意味しない。**数値を出すのは
     * ゲームが始まってから**にする——半端な値を HUD に出すと画面側で嘘になる。
     */
    if (!tb_character_generated()) {
        return TB_OK;
    }

    out->hp = (int)p_ptr->chp;
    out->hp_max = (int)p_ptr->mhp;
    out->sp = (int)p_ptr->csp;
    out->sp_max = (int)p_ptr->msp;
    out->level = (int)p_ptr->lev;
    out->depth = (int)dun_level;
    out->gold = (long)p_ptr->au_sum; /* TOband は通貨が複数あり、au は配列。合計は au_sum（prt_gold と同じ） */

    tb_place_name(out->place, (int)sizeof(out->place));
    tb_depth_text(out->depth_text, (int)sizeof(out->depth_text));

    return TB_OK;
}

/* =============================================================== 画面の状態（P3） */

int tb_screen_depth(void)
{
    return (int)character_icky;
}

int tb_screen_flags(void)
{
    int bits = 0;

    if (character_generated) {
        bits |= TB_SCREEN_GENERATED;
    }
    /*
     * **`character_icky` は計数器である。** `screen_save()`（util.c:3291）が `++`、
     * `screen_load()`（:3309）が `--` する。`== TRUE` で比べると入れ子の 2 段目を
     * 取り逃がす（先方も `cmd7.c:5080` でそう注記している）。非 0 だけを見る。
     */
    if (character_icky) {
        bits |= TB_SCREEN_ICKY;
    }
    if (character_xtra) {
        bits |= TB_SCREEN_XTRA;
    }
    if (character_dungeon) {
        bits |= TB_SCREEN_DUNGEON;
    }
    /*
     * 倒れても、控えの仲間が残っていれば遊びは続く。原作の `take_hit()` は
     * `party_has_successor() && !p_ptr->inside_arena` のとき死亡を保留し（`is_dead` を立てて動作の終わりを待つ）、
     * `party_handle_death()` が装備を落とし・タロットを選ばせ・交代する。**その間はゲームオーバーの画ではない**
     * （題が「ゲームオーバー」になり、地図が消える）ので、死亡の旗は立てない。控えがいない本当の死は従来どおり。
     */
    if (character_generated && p_ptr->is_dead && !(party_has_successor() && !p_ptr->inside_arena)) {
        bits |= TB_SCREEN_DEAD;
    }
    if (character_generated && p_ptr->playing) {
        bits |= TB_SCREEN_PLAYING;
    }
    return bits;
}

/* =================================================================== 地図（P3） */

int tb_floor_size(int *width, int *height)
{
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return TB_ERR_STATE;
    }
    if (width) {
        *width = (int)cur_wid;
    }
    if (height) {
        *height = (int)cur_hgt;
    }
    return TB_OK;
}

int tb_player_kind(int *pclass, int *prace)
{
    if (!tb_character_generated()) {
        return TB_ERR_STATE;
    }
    if (pclass) {
        *pclass = (int)p_ptr->pclass;
    }
    if (prace) {
        *prace = (int)p_ptr->prace;
    }
    return TB_OK;
}

int tb_player_sex(int *psex)
{
    if (!tb_character_generated()) {
        return TB_ERR_STATE;
    }
    if (psex) {
        *psex = (int)p_ptr->psex;
    }
    return TB_OK;
}

int tb_player_pos(int *x, int *y)
{
    if (!character_generated || !character_dungeon) {
        return TB_ERR_STATE;
    }
    if (x) {
        *x = px;
    }
    if (y) {
        *y = py;
    }
    return TB_OK;
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
int tb_locate_panel_center(int *cx, int *cy)
{
    static const char en[] = "Map sector";
    static const char jp[] = "\x83\x7d\x83\x62\x83\x76\x88\xca\x92\x75"; /* マップ位置（SJIS） */
    char row0[32];
    int wid = 0;
    int hgt = 0;

    if (!character_generated || !character_dungeon) {
        return 0;
    }
    if (tb_term_row(0, row0, NULL, (int)sizeof(row0)) <= 0) {
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
 * @brief 見かけの地形（`mimic` を適用した値）。`cave.c` の `map_info()` の冒頭と同じ式。
 * @details TOband（変愚蛮怒 1.5.1 系）には `get_feat_mimic()` が無い。
 */
static byte tb_feat_mimic(const cave_type *c_ptr)
{
    return c_ptr->mimic ? c_ptr->mimic : f_info[c_ptr->feat].mimic;
}

/*
 * 主 Term のカーソルが指している地図のマス。原作の `move_cursor_relative()`（`cave.c`）の逆:
 *   Term の桁 = (x - panel_col_min) * (use_bigtile ? 2 : 1) + 13　　Term の行 = y - panel_row_prt
 */
int tb_cursor_cave_cell(int *gx, int *gy)
{
    int cx = 0;
    int cy = 0;
    int visible = 0;
    int x;
    int y;

    if (!character_generated || !character_dungeon) {
        return 0;
    }
    if (tb_term_cursor(&cx, &cy, &visible) != TB_OK) {
        return 0;
    }
    if (!visible || (cx < 13)) {
        return 0;
    }
    x = (int)panel_col_min + ((cx - 13) / (use_bigtile ? 2 : 1));
    y = cy + (int)panel_row_prt;
    if (!in_bounds2(y, x) || !panel_contains(y, x)) {
        return 0;
    }
    if (gx) {
        *gx = x;
    }
    if (gy) {
        *gy = y;
    }
    return 1;
}

/*
 * カーソルのマスのポップアップの中身（宣言は tb_shim.h）。原作の `target_set_aux()`（`xtra2.c`）が、そのマスを調べる**最初の段**に出すもの
 * と同じ（原作は段ごとに 1 行を出し、見る `l` では空白などで次の段へ進む。狙う `*` は最初の段だけ）。段の順と出す条件は原作のまま:
 *   1. 幻覚中                 … 「何か奇妙な物」（頭と末尾は自分のマスのときだけ付く）
 *   2. 見えているモンスター   … 「[あと何回]ターゲット:名前(レベル???, 無傷, 属性の名前)」（属性の名前はその属性の色）。
 *                              自分のマスなら頭が「あなたは」、末尾が「の上にいる」（乗馬）
 *   3. 自分のおとり           … 「ダミー人形」
 *   4. 床の物                 … 1 つなら物の名前、山（`easy_floor` で 2 つ以上）なら「N個のアイテム」、`easy_floor` でなければ印のついた最初の物の名前
 *   5. 地形                   … `prt_grid_elem()` の字 `(火+02,水+00,地-04,風+08,攻+40,防+15)`（いちばん強い属性の項目だけその属性の色）。
 *                              地形が分かっている（`CAVE_MARK` か、いま見えている）ときだけで、広域マップでは出さない
 * どの行も操作の案内（`[r思 …]` など）は付けない。原作の `prt_grid_elem()` は Term へ描くだけで呼べないので、公開の部品
 * （`cave[y][x].elem[]`・`get_dominant_feature_elem()`・`elem_attr()`・`f_info[]`・`get_cur_melem()`・`look_mon_desc()`・`object_desc()`・
 * `scan_floor()`）から同じ字を組む。「[あと何回]」は xtra2.c の static な `evaluate_monster_exp()` を `tb_targeting.inl` の入口
 * `tb_monster_exp_text()` 越しに呼ぶ。
 */
extern void tb_monster_exp_text(char *buf, int m_idx);

void tb_elem_info(int y, int x, tb_info_span_fn span, tb_info_newline_fn newline, void *user)
{
    static const char *const short_en[ELEM_NUM] = { "F:", "A:", "E:", "W:" };
    /* 攻・防・ターゲット:・あなたは・の上・にいる・何か奇妙な物・ダミー人形・個のアイテム（CP932）。
     * 文字列の16進の書き方だと MSVC が下位バイトを ASCII と見て警告するので、バイトで並べる。 */
    static const char kanji_attack[] = { (char)0x8d, (char)0x55, 0 };
    static const char kanji_defense[] = { (char)0x96, (char)0x68, 0 };
    static const char jp_target[] = { (char)0x83, (char)0x5e, (char)0x81, (char)0x5b, (char)0x83, (char)0x51, (char)0x83, (char)0x62, (char)0x83, (char)0x67, (char)0x3a, 0 };
    static const char jp_you[] = { (char)0x82, (char)0xa0, (char)0x82, (char)0xc8, (char)0x82, (char)0xbd, (char)0x82, (char)0xcd, 0 };
    static const char jp_on[] = { (char)0x82, (char)0xcc, (char)0x8f, (char)0xe3, 0 };
    static const char jp_be[] = { (char)0x82, (char)0xc9, (char)0x82, (char)0xa2, (char)0x82, (char)0xe9, 0 };
    static const char jp_strange[] = { (char)0x89, (char)0xbd, (char)0x82, (char)0xa9, (char)0x8a, (char)0xef, (char)0x96, (char)0xad, (char)0x82, (char)0xc8, (char)0x95, (char)0xa8, 0 };
    static const char jp_decoy[] = { (char)0x83, (char)0x5f, (char)0x83, (char)0x7e, (char)0x81, (char)0x5b, (char)0x90, (char)0x6c, (char)0x8c, (char)0x60, 0 };
    static const char jp_items[] = { (char)0x8c, (char)0xc2, (char)0x82, (char)0xcc, (char)0x83, (char)0x41, (char)0x83, (char)0x43, (char)0x83, (char)0x65, (char)0x83, (char)0x80, 0 };
    cave_type *c_ptr;
    byte feat;
    char buf[MAX_NLEN + 160];
    char o_name[MAX_NLEN];
    int i;
    const int english = tb_lang_enabled();
    const int on_player = ((y == py) && (x == px));
    /* 原作の s1・s2・s3（最初の段のもの） */
    const char *s1 = on_player ? (english ? "You are " : jp_you) : (english ? "Target:" : jp_target);
    const char *s2 = on_player ? (english ? "on " : jp_on) : "";
    const char *s3 = on_player ? (english ? "" : jp_be) : "";
    tb_rng_snapshot snap;
    int floor_list[23];
    int floor_num = 0;

    (void)newline;
    if (!span || !in_bounds2(y, x)) {
        return;
    }
    c_ptr = &cave[y][x];

    /* 1. 幻覚中 */
    if (p_ptr->image) {
        if (english) {
            sprintf(buf, "%s%s%ssomething strange", s1, s2, s3);
        } else {
            sprintf(buf, "%s%s%s%s", s1, jp_strange, s2, s3);
        }
        span(user, TERM_WHITE, buf);
        return;
    }

    /* 2. 見えているモンスター */
    if (c_ptr->m_idx && (c_ptr->m_idx < m_max) && m_list[c_ptr->m_idx].ml) {
        monster_type *m_ptr = tb_monster_slot(c_ptr->m_idx);
        char acount[10];
        char m_name[80];
        const s16b elem = get_cur_melem(m_ptr);

        acount[0] = '\0';
        tb_monster_exp_text(acount, c_ptr->m_idx);
        tb_rng_save(&snap);
        monster_desc(m_name, m_ptr, MD_INDEF_VISIBLE);
        tb_rng_restore(&snap);
        /* JP: [%s]%s%s(%s,   EN: [%s]%s%s%s%s(%s,   （原作の sprintf のとおり。英語は s2・s3 が名前の前に来る） */
        if (english) {
            sprintf(buf, "[%s]%s%s%s%s(%s, ", acount, s1, s2, s3, m_name, look_mon_desc(c_ptr->m_idx, 0x01));
        } else {
            sprintf(buf, "[%s]%s%s(%s, ", acount, s1, m_name, look_mon_desc(c_ptr->m_idx, 0x01));
        }
        span(user, TERM_WHITE, buf);
        if ((elem >= MIN_ELEM) && (elem < ELEM_NUM)) {
            span(user, elem_attr(elem), english ? E_elem_names[elem] : elem_names[elem]);
        }
        if (english) {
            span(user, TERM_WHITE, ")");
        } else {
            sprintf(buf, ")%s%s", s2, s3);
            span(user, TERM_WHITE, buf);
        }
        return;
    }

    /* 3. 自分のおとり */
    if (p_ptr->use_decoy && (y == p_ptr->decoy_y) && (x == p_ptr->decoy_x)) {
        if (english) {
            sprintf(buf, "%s%s%sYour Decoy", s1, s2, s3);
        } else {
            sprintf(buf, "%s%s%s%s", s1, jp_decoy, s2, s3);
        }
        span(user, TERM_WHITE, buf);
        return;
    }

    /* 4. 床の物（原作は `easy_floor` の山・1 つ、そうでなければ印のついた物を 1 つずつ） */
    if (easy_floor) {
        floor_num = scan_floor(floor_list, y, x, 0x02);
    }
    if (floor_num >= 2) {
        if (english) {
            sprintf(buf, "%s%s%sa pile of %d items", s1, s2, s3, floor_num);
        } else {
            sprintf(buf, "%s %d%s%s%s", s1, floor_num, jp_items, s2, s3);
        }
        span(user, TERM_WHITE, buf);
        return;
    }
    {
        object_type *o_ptr = NULL;

        if (floor_num == 1) {
            o_ptr = &o_list[floor_list[0]];
        } else {
            s16b this_o_idx;
            s16b next_o_idx = 0;

            for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                object_type *candidate = &o_list[this_o_idx];

                next_o_idx = candidate->next_o_idx;
                if (candidate->marked) {
                    o_ptr = candidate;
                    break;
                }
            }
        }
        if (o_ptr) {
            tb_rng_save(&snap);
            object_desc(o_name, o_ptr, 0);
            tb_rng_restore(&snap);
            if (english) {
                sprintf(buf, "%s%s%s%s", s1, s2, s3, o_name);
            } else {
                sprintf(buf, "%s%s%s%s", s1, o_name, s2, s3);
            }
            span(user, TERM_WHITE, buf);
            return;
        }
    }

    /* 5. 地形 */
    feat = tb_feat_mimic(c_ptr);
    if (!(c_ptr->info & CAVE_MARK) && !player_can_see_bold(y, x)) {
        feat = FEAT_NONE;
    }
    if ((feat != FEAT_NONE) && !p_ptr->wild_mode) {
        const s16b dominant = get_dominant_feature_elem(c_ptr);

        span(user, TERM_WHITE, "(");
        for (i = MIN_ELEM; i < ELEM_NUM; i++) {
            sprintf(buf, "%s%c%02d,", english ? short_en[i] : elem_names[i], (c_ptr->elem[i] < 0) ? '-' : '+', abs(c_ptr->elem[i]));
            span(user, (i == dominant) ? elem_attr((s16b)i) : TERM_WHITE, buf);
        }
        if (english) {
            sprintf(buf, "At:+%02d,Df:+%02d)", f_info[feat].to_offence, f_info[feat].to_defence);
        } else {
            sprintf(buf, "%s+%02d,%s+%02d)", kanji_attack, f_info[feat].to_offence, kanji_defense, f_info[feat].to_defence);
        }
        span(user, TERM_WHITE, buf);
    }
}

/*! @brief `map_info()` が「床の仲間」として扱う地形か（`cave.c` の分岐と同じ）。 */
static int tb_feat_is_floorish(byte feat)
{
    return ((feat <= FEAT_INVIS) || (feat == FEAT_DIRT) || (feat == FEAT_GRASS)
               || (feat == FEAT_SWAMP) || (feat == FEAT_TUNDRA))
        ? 1
        : 0;
}

static int tb_feat_is_door(byte feat)
{
    return (feat >= FEAT_DOOR_HEAD && feat <= FEAT_DOOR_TAIL) ? 1 : 0;
}

/*! 壁（掘れる壁・永久壁・山）。隠し扉は見かけ（`mimic`）が壁なので、見かけの地形で呼べばよい。 */
static int tb_feat_is_wall(byte feat)
{
    return ((feat >= FEAT_MAGMA && feat <= FEAT_PERM_SOLID) || (feat == FEAT_MOUNTAIN)
               || (feat == FEAT_SECRET))
        ? 1
        : 0;
}

static int tb_feat_is_permanent(byte feat)
{
    return ((feat >= FEAT_PERM_EXTRA && feat <= FEAT_PERM_SOLID) || (feat == FEAT_MOUNTAIN)) ? 1 : 0;
}

static int tb_feat_is_stairs(byte feat)
{
    switch (feat) {
    case FEAT_LESS:
    case FEAT_MORE:
    case FEAT_LESS_LESS:
    case FEAT_MORE_MORE:
    case FEAT_QUEST_ENTER:
    case FEAT_QUEST_EXIT:
    case FEAT_QUEST_DOWN:
    case FEAT_QUEST_UP:
    case FEAT_ENTRANCE:
    case FEAT_ENTRANCE_UPWARD:
    case FEAT_TOWN:
        return 1;
    default:
        return 0;
    }
}

static int tb_feat_is_water(byte feat)
{
    return ((feat == FEAT_DEEP_WATER) || (feat == FEAT_SHAL_WATER) || (feat == FEAT_DEEP_SEA)) ? 1 : 0;
}

static int tb_feat_is_lava(byte feat)
{
    return ((feat == FEAT_DEEP_LAVA) || (feat == FEAT_SHAL_LAVA)) ? 1 : 0;
}

/*!
 * @brief そのマスの地形が「見えている」か。**`map_info()` と同じ判定にする**。
 *
 * @details 判定を独自に作ると、コアが画面に描いているものと HD2D の立体が食い違う
 * （記号は伏せたまま形だけ漏れる、あるいはその逆）。`cave.c:947` の `map_info()` の分岐を写す:
 * - 床の仲間（`tb_feat_is_floorish`）… `CAVE_MARK`、または盲目でなく
 *   （`CAVE_LITE`・`CAVE_MNLT`・視界内かつ `CAVE_GLOW`）
 * - それ以外（壁・扉・階段）… `CAVE_MARK` だけ。盲目でも消えない
 */
static int tb_grid_known(cave_type *c_ptr, s16b feat)
{
    if (!tb_feat_is_floorish((byte)feat)) {
        return (c_ptr->info & CAVE_MARK) ? 1 : 0;
    }
    if (c_ptr->info & CAVE_MARK) {
        return 1;
    }
    if (p_ptr->blind) {
        return 0;
    }
    if (c_ptr->info & (CAVE_LITE | CAVE_MNLT)) {
        return 1;
    }
    if ((c_ptr->info & (CAVE_GLOW | CAVE_VIEW)) == (CAVE_GLOW | CAVE_VIEW)) {
        return 1;
    }
    return 0;
}

/*! @brief いま照明が当たっているか（`light_level` の 2 の条件）。 */
static int tb_grid_lit_now(cave_type *c_ptr)
{
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
 * @details 光源の枠の種類から見せ方を決める。松明・ランタンは燃料切れなら半径に足されないので、
 * 半径が残っていれば装備によらない光（4）。フィアノールのランタンと燃料の要らない光源はランタンの
 * 見せ方（2）、アーティファクトと魔法のランプは 3、時計やオルゴールのように光源の枠に在っても
 * 自分では照らさない物は 4。読むだけでコアの状態は変えない。
 */
static int tb_light_kind_of(object_type *o_ptr, int cur_lite)
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
    if ((o_ptr->sval == SV_LITE_EMPTY) && have_flag(o_ptr->art_flags, TR_LITE)) {
        return 2; /* 燃料の要らない光（空の光源に付けた光の旗）。ランタンと同じ明るさ */
    }
    if (o_ptr->sval == SV_LITE_MAGICAL_LAMP) {
        return 3;
    }
    return 4; /* 光源の枠に在っても自分では照らさない物（時計・オルゴールなど）。残っている半径は別の光 */
}

/*! @brief いまのプレイヤの光源の種類。 */
static int tb_player_light_kind(void)
{
    object_type *o_ptr = &inventory[INVEN_LITE];

    return tb_light_kind_of(o_ptr, (int)p_ptr->cur_lite);
}

/*! @brief `tb_light_selftest` の 1 件。作った物（`tval`/`sval`/`name1`/`name2`/`xtra4`）の判定が `want` か。 */
static void tb_light_case(const char *name, int tval, int sval, int name1, int name2, int xtra4,
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
        got = tb_light_kind_of(&forge, cur_lite);
    }
    if (got != want) {
        if (*fails == 0 && err && cap > 1) {
            sprintf(err, "%.60s (want %d, got %d)", name, want, got);
            err[cap - 1] = '\0';
        }
        (*fails)++;
    }
}

int tb_light_selftest(char *err, int cap)
{
    int fails = 0;
    object_type empty;

    if (err && cap > 0) {
        err[0] = '\0';
    }

    /* 光が無い */
    tb_light_case("torch, radius 0", TV_LITE, SV_LITE_TORCH, 0, 0, 1000, 0, 0, err, cap, &fails);
    if (tb_light_kind_of(NULL, 0) != 0) {
        fails++;
    }
    /* 枠が空・光源でない物でも半径が残っていれば装備によらない光 */
    if (tb_light_kind_of(NULL, 1) != 4) {
        if (fails == 0 && err && cap > 1) {
            sprintf(err, "empty slot (want 4)");
        }
        fails++;
    }
    memset(&empty, 0, sizeof(empty));
    if (tb_light_kind_of(&empty, 2) != 4) {
        if (fails == 0 && err && cap > 1) {
            sprintf(err, "k_idx 0 (want 4)");
        }
        fails++;
    }
    /* 松明・ランタン（燃料の有無） */
    tb_light_case("torch with fuel", TV_LITE, SV_LITE_TORCH, 0, 0, 1000, 1, 1, err, cap, &fails);
    tb_light_case("torch without fuel", TV_LITE, SV_LITE_TORCH, 0, 0, 0, 1, 4, err, cap, &fails);
    tb_light_case("lantern with fuel", TV_LITE, SV_LITE_LANTERN, 0, 0, 1000, 2, 2, err, cap, &fails);
    tb_light_case("lantern without fuel", TV_LITE, SV_LITE_LANTERN, 0, 0, 0, 2, 4, err, cap, &fails);
    tb_light_case("magic lantern (Feanor)", TV_LITE, SV_LITE_FEANOR, 0, 0, 0, 2, 2, err, cap, &fails);
    tb_light_case("darkness torch", TV_LITE, SV_LITE_TORCH, 0, EGO_LITE_DARKNESS, 1000, 1, 4, err, cap, &fails);
    /* 燃料の要らない光源・光らない物 */
    tb_light_case("magical lamp", TV_LITE, SV_LITE_MAGICAL_LAMP, 0, 0, 0, 3, 3, err, cap, &fails);
    tb_light_case("watch (does not shine)", TV_LITE, SV_LITE_WATCH, 0, 0, 0, 1, 4, err, cap, &fails);
    return fails;
}

/*! @brief 地形 → `TB_FEAT_*`（設計 §4.2 の表）。`KNOWN` / `PLAYER` は呼び出し側。 */
static unsigned int tb_feature_bits(s16b feat, cave_type *c_ptr)
{
    const byte f = (byte)feat;
    unsigned int bits = 0;

    /*
     * TOband（変愚蛮怒 1.5.1 系）の `feature_type` は旗を持たない。地形の性質は
     * 地形番号（`FEAT_*`）で決まっているので、番号の範囲で振る（`defines.h`）。
     */
    if (tb_feat_is_wall(f)) {
        bits |= TB_FEAT_WALL;
    }
    if (tb_feat_is_door(f) || (f == FEAT_OPEN) || (f == FEAT_BROKEN)) {
        bits |= TB_FEAT_DOOR;
        /* 開いているか。開いた扉（`FEAT_OPEN`）と壊れた扉（`FEAT_BROKEN`）が開いている側。 */
        if ((f == FEAT_OPEN) || (f == FEAT_BROKEN)) {
            bits |= TB_FEAT_DOOR_OPEN;
        }
    }
    if (f == FEAT_RUBBLE) {
        bits |= TB_FEAT_RUBBLE;
    }
    /* 階段のほか、ダンジョンの入口とクエストの入口も「降りる場所」として同じ扱いにする。 */
    if (tb_feat_is_stairs(f)) {
        bits |= TB_FEAT_STAIRS;
    }
    if ((f == FEAT_LESS_LESS) || (f == FEAT_MORE_MORE)) {
        bits |= TB_FEAT_SHAFT;
    }
    if (tb_feat_is_permanent(f)) {
        bits |= TB_FEAT_PERMANENT;
    }
    if (f == FEAT_TREES) {
        bits |= TB_FEAT_TREE;
    }
    if (tb_feat_is_water(f)) {
        bits |= TB_FEAT_WATER;
    }
    if (tb_feat_is_lava(f)) {
        bits |= TB_FEAT_LAVA;
    }
    /* 通り抜けられる床（HD2D の扉の向き判定の主材料）。壁・扉・木・岩は明示的に外す。 */
    if (!(bits & (TB_FEAT_WALL | TB_FEAT_DOOR | TB_FEAT_TREE | TB_FEAT_RUBBLE))
        && (f != FEAT_NONE) && (f != FEAT_UNDETECTED) && (f != FEAT_DEEP_SEA)) {
        bits |= TB_FEAT_PASSABLE;
    }
    if (c_ptr->info & CAVE_ROOM) {
        bits |= TB_FEAT_ROOM;
    }
    if (c_ptr->info & CAVE_GLOW) {
        bits |= TB_FEAT_GLOWING;
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
 * で突き合わせてある。**`tb_native_store.c` と共有する**（宣言は `tb_unaware_category.h`）
 * ——同じ判定を 2 か所に書かない（契約 §4）。
 */
unsigned char tb_unaware_category(const object_type *o_ptr)
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
    case TV_FOOD:
        /* TOband（変愚蛮怒 1.x 系）は食べ物のうち未鑑定名（flavor）を持つのがキノコだけなので、
         * 上の `flavor` の判定を通った `TV_FOOD` は「キノコ」の分類にする。 */
        return UNAWARE_CATEGORY_MUSHROOM;
    default:
        return 0; /* Sil-Q の角笛のような 8 分類の外。幻想蛮怒には今のところ無い */
    }
}

static int tb_read_map_raw(int x0, int y0, int w, int h, tb_map_cell *out, int cap)
{
    int vx;
    int vy;
    int n = 0;
    int scope_active;

    if (!out || (w <= 0) || (h <= 0) || (cap < (w * h))) {
        return TB_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return TB_ERR_STATE;
    }
    scope_active = (p_ptr->action == ACTION_ELEMSCOPE) ? 1 : 0;

    for (vy = 0; vy < h; vy++) {
        const int gy = y0 + vy;
        for (vx = 0; vx < w; vx++) {
            const int gx = x0 + vx;
            tb_map_cell *cell = &out[n++];
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
            feat = tb_feat_mimic(c_ptr);
            known = tb_grid_known(c_ptr, feat);

            /* 見た目はコアの `map_info()` そのまま（記号・色を作り直さない）。 */
            map_info(gy, gx, &a, &c, &ta, &tc);
            cell->fg_color = (unsigned char)(a & 0x0F);
            cell->bg_color = (unsigned char)(ta & 0x0F);
            cell->ascii = c;

            if (known) {
                cell->terrain_id = (unsigned short)feat;
                cell->feature_flags = (unsigned short)(TB_FEAT_KNOWN | tb_feature_bits(feat, c_ptr));
            }
            /* @ の居場所は既知・未知に関わらず立てる（隠す情報ではない）。 */
            if ((gy == py) && (gx == px)) {
                cell->feature_flags = (unsigned short)(cell->feature_flags | TB_FEAT_PLAYER);
            }

            if (tb_grid_lit_now(c_ptr)) {
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
                monster_type *m_ptr = tb_monster_slot(c_ptr->m_idx);
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
             * （`tb_frame.cpp`）は設計 §5.1 の順「@ ＞ 見えている敵 ＞ **照らされた**床の物 ＞
             * 地形」に従って**物を飛ばし地形の絵を返す**ので、探しにいく先が `F1`（床）に
             * なる。地形には板が無い（板は R/K/P だけ。地形はタイルを地面に貼る）ので
             * 必ず外れ、欠けが 1 つ数えられていた。**板の焼き漏れではない。**
             *
             * 揃える相手は 2 つとも同じ条件になっている:
             *   - 絵の決め方 … `resolve_tile_index`（`light_level >= 2 && object_id`）
             *   - ミニマップ … `tb_read_minimap`（`tb_grid_lit_now(c_ptr)`）
             * 変愚も同じ形である（`presentation_bridge.cpp:3483` の `if (lit_now && ...)` で
             * `cell.object_id` を立て、:524 の `resolve_tile_index` も `lit_now` で見る）。
             * 記憶しているだけの物は Term の記号（`ascii`）には残るので、情報は落ちない。
             */
            if (cell->light_level >= 2) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = tb_floor_obj(this_o_idx);
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        cell->object_id = (unsigned short)o_ptr->k_idx;
                        cell->object_unaware_category = tb_unaware_category(o_ptr);
                        break;
                    }
                }
            }

            /*
             * エレメントスコープ中は、原作の `map_info()` が字の色を属性の色へ差し替えて返す
             * （`cave.c` の末尾の `if (p_ptr->action == ACTION_ELEMSCOPE) (*ap) = map_elem_attr(y, x)`）。
             * その `a` を読むだけで、色の決め方（プレイヤ・見えているモンスター・マスのいちばん強い属性）は写さない。
             * 重ねるのは原作で字が見えるマス（見えている・覚えている地形、見えているモンスター、@）に限る。
             */
            if (scope_active && (known || cell->monster_id || ((gy == py) && (gx == px)))) {
                cell->elem_tint = (unsigned char)((a & 0x0F) + 1);
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

/* ============================================ モンスターの絵の番号（ランダムユニーク・ルーンウェポン） */

static int tb_popcount32(unsigned long v)
{
    int n = 0;
    while (v) {
        n += (int)(v & 1UL);
        v >>= 1;
    }
    return n;
}

/*
 * ランダムユニーク `a` と既存のモンスター `b` の食い違いの大きさ（小さいほど近い）。
 * 見るのは原作が持っている値だけ（レベル・種族の旗・性別・移動の旗・呪文を使うか）。
 * 重みは「絵の見た目に効く順」に大きくしてある。
 *   種族の旗（ドラゴン・悪魔・巨人・トロル・オーク・アンデッド・邪悪・動物・善）と人間 … 1 個の食い違いにつき 10
 *   元素（炎・水・地・風）… 8、呪文を使うか … 8、水棲 … 12、飛行 … 6、性別 … 6
 *   レベル … 4 差につき 1
 */
static int tb_art_distance(const monster_race *a, const monster_race *b)
{
    int d = 0;
    int dl = (int)a->level - (int)b->level;
    if (dl < 0) {
        dl = -dl;
    }
    d += dl / 4;
    d += 10 * tb_popcount32((a->flags3 ^ b->flags3) & (RF3_RACE_MASK | RF3_UNDEAD | RF3_EVIL | RF3_ANIMAL | RF3_GOOD));
    d += 10 * (((a->flags2 ^ b->flags2) & RF2_HUMAN) ? 1 : 0);
    d += 8 * tb_popcount32((a->flags3 ^ b->flags3) & RF3_ELEM_MASK);
    d += 8 * (((a->freq_spell != 0) != (b->freq_spell != 0)) ? 1 : 0);
    d += 12 * (((a->flags7 ^ b->flags7) & RF7_AQUATIC) ? 1 : 0);
    d += 6 * (((a->flags7 ^ b->flags7) & RF7_CAN_FLY) ? 1 : 0);
    d += 6 * tb_popcount32((a->flags1 ^ b->flags1) & (RF1_MALE | RF1_FEMALE));
    return d;
}

/*
 * ランダムユニークの枠（r_idx 989〜991）に**いま入っている中身**に、いちばん近い既存のモンスターの番号を返す。
 *
 * 選び方（決めたこと 2026-10-18「出てきたモンスターにより近い絵」）:
 *   1. 記号（`d_char`）が同じで、ユニークでない（`RF1_UNIQUE`・`RF7_UNIQUE2` のどちらも無い）… 名前のある人物や主要な敵の絵は
 *      別人に見えるので、一般の種を先に取る
 *   2. 記号が同じで、ユニーク（`RF1_UNIQUE` か `RF7_UNIQUE2`）
 *   3. 記号が違うが、ユニークでない
 *   4. 記号が違い、ユニーク
 * の順に最初に候補のある段を使い、その段の中で `tb_art_distance` が最小のもの（同点なら番号の小さいもの）。
 * 生成前の枠（記号が '?'）は選べないので、枠の番号をそのまま返す（従来の絵 TR989〜991）。
 */
static int tb_random_unique_art_id(int r_idx)
{
    const monster_race *a = &r_info[r_idx];
    int best = r_idx;
    int best_tier = 99;
    int best_dist = 0;
    int i;

    if (a->d_char == '?') {
        return r_idx;
    }
    for (i = 1; i < max_r_idx; i++) {
        const monster_race *b = &r_info[i];
        int tier;
        int dist;
        if (IS_RANDOM_UNIQUE(i) || (b->name == 0)) {
            continue;
        }
        tier = ((b->d_char == a->d_char) ? 0 : 2) + (((b->flags1 & RF1_UNIQUE) || (b->flags7 & RF7_UNIQUE2)) ? 1 : 0);
        if (tier > best_tier) {
            continue;
        }
        dist = tb_art_distance(a, b);
        if ((tier < best_tier) || (dist < best_dist)) {
            best = i;
            best_tier = tier;
            best_dist = dist;
        }
    }

    if (getenv("TB_ART_TRACE") != NULL) {
        static int last_key[3] = { -1, -1, -1 };
        const int slot = r_idx - MON_RANDOM_UNIQUE_1;
        const int key = ((int)a->level << 16) | ((int)(unsigned char)a->d_char << 8) | (best & 0xFF);
        if ((slot >= 0) && (slot < 3) && (last_key[slot] != key)) {
            last_key[slot] = key;
            fprintf(stderr,
                "[toband-art] random_unique r_idx=%d char=%c level=%d flags1=%08lx flags2=%08lx flags3=%08lx flags7=%08lx freq=%d -> art r_idx=%d tier=%d dist=%d\n",
                r_idx, a->d_char, (int)a->level, (unsigned long)a->flags1, (unsigned long)a->flags2, (unsigned long)a->flags3,
                (unsigned long)a->flags7, (int)a->freq_spell, best, best_tier, best_dist);
        }
    }
    return best;
}

/*
 * ルーンウェポンの武器の種類（`create_runeweapon`、`monster2.c`）。`tval`・`sval` の組から種類の番号 k（0〜9）へ。
 * 魔剣 0・魔爪 1／魔弓 2・魔銃 3／魔槌 4・魔鞭 5・魔杖 6・魔扇 7／魔槍 8・魔斧 9。表にない組は -1。
 */
int tb_runeweapon_kind(int tval, int sval)
{
    switch (tval) {
    case TV_SWORD:
        if (sval == SV_RUNEBLADE) return 0;
        if (sval == SV_RUNECLAW) return 1;
        break;
    case TV_BOW:
        if (sval == SV_RUNEBOW) return 2;
        if (sval == SV_RUNEGUN) return 3;
        break;
    case TV_HAFTED:
        if (sval == SV_RUNEHAMMER) return 4;
        if (sval == SV_RUNEWHIP) return 5;
        if (sval == SV_RUNESTAFF) return 6;
        if (sval == SV_RUNEFAN) return 7;
        break;
    case TV_POLEARM:
        if (sval == SV_RUNESPEAR) return 8;
        if (sval == SV_RUNEAXE) return 9;
        break;
    default:
        break;
    }
    return -1;
}

int tb_runeweapon_art_for(int n, int tval, int sval, int astral, int race)
{
    int k;

    /* アストラルモードの 1 枠目は祖先の体（人型）。その種族のプレイヤーの絵を使う。 */
    if (astral && (n == 1) && (race >= 0) && (race < 1000)) {
        return TB_ART_PLAYER_RACE_BASE + race;
    }
    k = tb_runeweapon_kind(tval, sval);
    if (k >= 0) {
        return TB_ART_RUNEWEAPON_KIND_BASE + k;
    }
    /* 知らない種類は従来の絵（TRW1〜3）。 */
    return TB_ART_RUNEWEAPON_BASE + n;
}

int tb_monster_art_id(int r_idx)
{
    if ((r_idx >= MON_RANDOM_UNIQUE_1) && IS_RANDOM_UNIQUE(r_idx) && (r_idx < max_r_idx)) {
        return tb_random_unique_art_id(r_idx);
    }
    if ((r_idx >= max_r_idx) && monster_is_runeweapon(r_idx)) {
        const int n = runeweapon_num_from(r_idx);
        const runeweapon_type *rw = &runeweapon_list[n];
        const int art = tb_runeweapon_art_for(n, (int)rw->weapon.tval, (int)rw->weapon.sval, astral_mode ? 1 : 0, (int)rw->race);

        if (getenv("TB_ART_TRACE") != NULL) {
            static int last_key[MAX_RUNEWEAPON + 1] = { 0 };
            if ((n >= 1) && (n <= MAX_RUNEWEAPON) && (last_key[n] != (art + 1))) {
                last_key[n] = art + 1;
                fprintf(stderr, "[toband-art] runeweapon n=%d tval=%d sval=%d astral=%d race=%d -> art=%d\n",
                    n, (int)rw->weapon.tval, (int)rw->weapon.sval, astral_mode ? 1 : 0, (int)rw->race, art);
            }
        }
        return art;
    }
    return r_idx;
}

int tb_monster_art_fallback_id(int r_idx)
{
    if ((r_idx >= max_r_idx) && monster_is_runeweapon(r_idx)) {
        return TB_ART_RUNEWEAPON_BASE + runeweapon_num_from(r_idx);
    }
    return r_idx;
}

/*! @brief `tb_runeweapon_art_selftest` の 1 件。 */
static void tb_runeweapon_art_case(const char *name, int got, int want, char *err, int cap, int *fails, int *checked)
{
    (*checked)++;
    if (got != want) {
        if (*fails == 0 && err && cap > 1) {
            sprintf(err, "%.60s (want %d, got %d)", name, want, got);
            err[cap - 1] = '\0';
        }
        (*fails)++;
    }
}

int tb_runeweapon_art_selftest(char *err, int cap, int *checked_out)
{
    static const struct {
        const char *name;
        int tval;
        int sval;
        int k;
    } kinds[TB_ART_RUNEWEAPON_KIND_COUNT] = {
        { "Runeblade", TV_SWORD, SV_RUNEBLADE, 0 },
        { "Runeclaw", TV_SWORD, SV_RUNECLAW, 1 },
        { "Runebow", TV_BOW, SV_RUNEBOW, 2 },
        { "Runegun", TV_BOW, SV_RUNEGUN, 3 },
        { "Runehammer", TV_HAFTED, SV_RUNEHAMMER, 4 },
        { "Runewhip", TV_HAFTED, SV_RUNEWHIP, 5 },
        { "Runestaff", TV_HAFTED, SV_RUNESTAFF, 6 },
        { "Runefan", TV_HAFTED, SV_RUNEFAN, 7 },
        { "Runespear", TV_POLEARM, SV_RUNESPEAR, 8 },
        { "Runeaxe", TV_POLEARM, SV_RUNEAXE, 9 },
    };
    static const int tvals[4] = { TV_SWORD, TV_BOW, TV_HAFTED, TV_POLEARM };
    int fails = 0;
    int checked = 0;
    int i;
    int t;
    int sval;
    int hits;

    if (err && cap > 0) {
        err[0] = '\0';
    }
    for (i = 0; i < TB_ART_RUNEWEAPON_KIND_COUNT; i++) {
        int n;
        /* 武器 10 種：枠 1〜3 のどれでも、アストラルモードでなければ種類の絵 */
        for (n = 1; n <= 3; n++) {
            tb_runeweapon_art_case(kinds[i].name, tb_runeweapon_art_for(n, kinds[i].tval, kinds[i].sval, 0, 0),
                TB_ART_RUNEWEAPON_KIND_BASE + kinds[i].k, err, cap, &fails, &checked);
        }
        /* アストラルモードでも 2 枠目は武器のまま */
        tb_runeweapon_art_case(kinds[i].name, tb_runeweapon_art_for(2, kinds[i].tval, kinds[i].sval, 1, 3),
            TB_ART_RUNEWEAPON_KIND_BASE + kinds[i].k, err, cap, &fails, &checked);
        /* アストラルモードの 1 枠目は、武器が何でも祖先の体（種族の絵） */
        tb_runeweapon_art_case(kinds[i].name, tb_runeweapon_art_for(1, kinds[i].tval, kinds[i].sval, 1, 5),
            TB_ART_PLAYER_RACE_BASE + 5, err, cap, &fails, &checked);
        tb_runeweapon_art_case(kinds[i].name, tb_runeweapon_kind(kinds[i].tval, kinds[i].sval), kinds[i].k, err, cap, &fails, &checked);
    }
    /* 4 つの tval を sval 0〜255 で全数当てて、当たるのがちょうど 10 組（上の表）だけ */
    hits = 0;
    for (t = 0; t < 4; t++) {
        for (sval = 0; sval < 256; sval++) {
            if (tb_runeweapon_kind(tvals[t], sval) >= 0) {
                hits++;
            }
        }
    }
    tb_runeweapon_art_case("kind table size", hits, TB_ART_RUNEWEAPON_KIND_COUNT, err, cap, &fails, &checked);
    /* 知らない種類は従来の絵（10000 + n） */
    tb_runeweapon_art_case("unknown sval", tb_runeweapon_art_for(2, TV_SWORD, 0, 0, 0), TB_ART_RUNEWEAPON_BASE + 2, err, cap, &fails, &checked);
    tb_runeweapon_art_case("unknown tval", tb_runeweapon_art_for(3, TV_SHIELD, SV_RUNEBLADE, 0, 0), TB_ART_RUNEWEAPON_BASE + 3, err, cap, &fails, &checked);
    tb_runeweapon_art_case("empty weapon", tb_runeweapon_art_for(1, 0, 0, 0, 0), TB_ART_RUNEWEAPON_BASE + 1, err, cap, &fails, &checked);
    /* 祖先の体の種族の端 */
    tb_runeweapon_art_case("ancestor race 0", tb_runeweapon_art_for(1, TV_SWORD, SV_RUNEBLADE, 1, 0), TB_ART_PLAYER_RACE_BASE, err, cap, &fails, &checked);
    tb_runeweapon_art_case("ancestor race out of range", tb_runeweapon_art_for(1, TV_SWORD, SV_RUNEBLADE, 1, 1000),
        TB_ART_RUNEWEAPON_KIND_BASE, err, cap, &fails, &checked);
    /* 下駄が重ならない */
    tb_runeweapon_art_case("bases do not overlap", ((TB_ART_RUNEWEAPON_BASE + 3 < TB_ART_RUNEWEAPON_KIND_BASE)
            && (TB_ART_RUNEWEAPON_KIND_BASE + TB_ART_RUNEWEAPON_KIND_COUNT <= TB_ART_PLAYER_RACE_BASE)) ? 1 : 0, 1, err, cap, &fails, &checked);

    if (checked_out) {
        *checked_out = checked;
    }
    return fails;
}

int tb_read_map(int x0, int y0, int w, int h, tb_map_cell *out, int cap)
{
    tb_rng_snapshot snap;
    int result;

    tb_rng_save(&snap);
    tb_test_force_image();
    result = tb_read_map_raw(x0, y0, w, h, out, cap);
    tb_rng_finish(&snap, "tb_read_map");
    return result;
}

int tb_read_minimap(unsigned char *out, int cap)
{
    int gx;
    int gy;
    int need;

    if (!out || (cap <= 0)) {
        return TB_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return TB_ERR_STATE;
    }

    need = (int)cur_hgt * (int)cur_wid;
    if (cap < need) {
        return TB_ERR_ARG;
    }
    memset(out, TB_MM_UNKNOWN, (size_t)need);

    for (gy = 0; gy < (int)cur_hgt; gy++) {
        for (gx = 0; gx < (int)cur_wid; gx++) {
            cave_type *c_ptr = &cave[gy][gx];
            const s16b feat = tb_feat_mimic(c_ptr);
            const int known = tb_grid_known(c_ptr, feat);
            int monster_here = 0;
            unsigned char kind;

            if (c_ptr->m_idx > 0 && c_ptr->m_idx < m_max) {
                monster_type *m_ptr = tb_monster_slot(c_ptr->m_idx);
                monster_here = (m_ptr->r_idx && m_ptr->ml) ? 1 : 0;
            }
            /* 未踏破は出さない。ただし見えている敵（テレパシー等）は出す。 */
            if (!known && !monster_here) {
                continue;
            }

            kind = TB_MM_FLOOR;
            if (known) {
                if (tb_feat_is_stairs((byte)feat)) {
                    kind = TB_MM_STAIRS;
                } else if (tb_feat_is_door((byte)feat) || (feat == FEAT_OPEN) || (feat == FEAT_BROKEN)) {
                    kind = TB_MM_DOOR;
                } else if (tb_feat_is_wall((byte)feat)) {
                    /* 山は壁と分けて送る（町の読み方が「敷地か岩山か」を決めるのに使う）。 */
                    kind = (feat == FEAT_MOUNTAIN) ? TB_MM_MOUNTAIN : TB_MM_WALL;
                }
            }
            if (known && tb_grid_lit_now(c_ptr) && c_ptr->o_idx && (kind != TB_MM_STAIRS)) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = tb_floor_obj(this_o_idx);
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        kind = TB_MM_ITEM;
                        break;
                    }
                }
            }
            if (monster_here) {
                kind = TB_MM_MONSTER;
            }
            out[(gy * (int)cur_wid) + gx] = kind;
        }
    }

    if (in_bounds2(py, px)) {
        out[(py * (int)cur_wid) + px] = TB_MM_PLAYER;
    }

    return need;
}


/* ============================================ 周囲の地形の内訳（環境音の層） */

/*! 数える半径（マス）。**耳が届くと思える範囲**であって、視界とは別。変愚と同じ 12。 */
#define TB_SURROUNDINGS_RADIUS 12

int tb_read_surroundings(tb_surroundings *out)
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
    const int r = TB_SURROUNDINGS_RADIUS;

    if (!out) {
        return TB_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return TB_OK; /* radius 0 のまま＝「数えていない」 */
    }

    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            const int gy = py + dy;
            const int gx = px + dx;
            cave_type *c_ptr;
            s16b feat;
            int is_wall;

            if (((dx * dx) + (dy * dy)) > (r * r)) {
                continue; /* 円で数える */
            }
            if (!in_bounds2(gy, gx)) {
                continue;
            }
            c_ptr = &cave[gy][gx];
            feat = tb_feat_mimic(c_ptr);
            if (!tb_grid_known(c_ptr, feat)) {
                continue;
            }
            counted++;

            is_wall = tb_feat_is_wall((byte)feat);
            if (is_wall) {
                wall++;
            }

            /* ---- 材料は排他。順番に意味がある（`tb_shim.h` の註記）---- */
            if (feat == FEAT_SWAMP) {
                swamp++;
            } else if (tb_feat_is_lava((byte)feat)) {
                lava++;
            } else if (tb_feat_is_water((byte)feat)) {
                if ((feat == FEAT_DEEP_WATER) || (feat == FEAT_DEEP_SEA)) {
                    deep_water++;
                } else {
                    water++;
                }
            } else if (feat == FEAT_TREES) {
                tree++;
            } else if (feat == FEAT_MOUNTAIN) {
                rock++;
            } else if (feat == FEAT_RUBBLE) {
                rock++; /* 瓦礫（変愚の STONE に当たる） */
            } else if ((feat == FEAT_GRASS) || (feat == FEAT_DEEP_GRASS) || (feat == FEAT_FLOWER)) {
                grass++;
            } else if (feat == FEAT_DIRT) {
                dirt++;
            }
        }
    }

    if (counted <= 0) {
        return TB_OK;
    }

#define TB_SURR_RATIO(N) ((unsigned char)(((N) * 255) / counted))
    out->grass = TB_SURR_RATIO(grass);
    out->tree = TB_SURR_RATIO(tree);
    out->dirt = TB_SURR_RATIO(dirt);
    out->swamp = TB_SURR_RATIO(swamp);
    out->water = TB_SURR_RATIO(water);
    out->deep_water = TB_SURR_RATIO(deep_water);
    out->lava = TB_SURR_RATIO(lava);
    out->rock = TB_SURR_RATIO(rock);
    out->glass = TB_SURR_RATIO(glass);
    out->wall = TB_SURR_RATIO(wall);
#undef TB_SURR_RATIO
    out->radius = (unsigned char)r;
    out->counted = (unsigned short)((counted > 0xFFFF) ? 0xFFFF : counted);
    return TB_OK;
}

int tb_read_floor_info(tb_floor_info *out)
{
    int day = 0;
    int hour = 0;
    int minute = 0;

    if (!out) {
        return TB_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->day_minute = 720;
    out->daytime = 1;

    if (!tb_character_generated()) {
        return TB_OK;
    }

    out->dungeon_id = (int)dungeon_type;
    out->dun_level = (int)dun_level;
    out->town_id = (int)p_ptr->town_num;
    out->wild_mode = p_ptr->wild_mode ? 1 : 0;

    if (p_ptr->inside_arena) {
        out->kind = 4; /* 闘技場 */
    } else if (p_ptr->inside_quest) {
        out->kind = 3; /* クエスト階 */
    } else if (dun_level) {
        out->kind = 2; /* ダンジョン */
    } else {
        out->kind = 1; /* 地上（町・荒野・広域マップ） */
    }

    tb_place_name(out->place_name, (int)sizeof(out->place_name));

    extract_day_hour_min(&day, &hour, &minute);
    out->day_minute = ((hour % 24) * 60) + (minute % 60);
    out->daytime = is_daytime() ? 1 : 0;
    out->light_radius = (int)p_ptr->cur_lite;
    out->light_kind = tb_player_light_kind();

    return TB_OK;
}

int tb_read_weather(int out[3])
{
    int i;

    if (!out) {
        return 0;
    }
    out[0] = out[1] = out[2] = 0;
    if (!tb_character_generated()) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        int v = (int)weather[i];
        if (v < 0) v = 0;
        if (v > MAX_WEATHER_VAL) v = MAX_WEATHER_VAL;
        out[i] = v;
    }
    return 1;
}

/* ================================================================ セーブの口（P3） */

const char *tb_save_dir(void)
{
    return ANGBAND_DIR_SAVE ? ANGBAND_DIR_SAVE : "";
}

int tb_set_savefile(const char *slot)
{
    if (!slot || !slot[0]) {
        return TB_ERR_ARG;
    }
    if (!ANGBAND_DIR_SAVE || !ANGBAND_DIR_SAVE[0]) {
        return TB_ERR_STATE; /* init_file_paths() より前 */
    }
    if (strlen(slot) >= sizeof(savefile_base)) {
        return TB_ERR_ARG;
    }

    /* `process_player_name()` が両方を見る（片方だけだと sf が真に倒れて訊かれる）。 */
    strcpy(savefile_base, slot);
    path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, slot);
    return TB_OK;
}

void tb_clear_savefile(void)
{
    savefile[0] = '\0';
    savefile_base[0] = '\0';
}

const char *tb_savefile_base(void)
{
    return savefile_base;
}

const char *tb_savefile_path(void)
{
    return savefile;
}

/* ============================================================= メッセージの読み口 */

int tb_message_count(void)
{
    return (int)message_num();
}

int tb_message_text(int age, char *out, int cap)
{
    cptr s;
    int n;

    if (!out || (cap <= 0)) {
        return TB_ERR_ARG;
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

    tb_copy_str(out, cap, s);
    n = (int)strlen(out);
    return n;
}

/* ======================================================= ゲーム前画面の描き口（P5） */

void tb_term_clear(void)
{
    Term_clear();
}

void tb_term_putstr(int x, int y, int attr, const char *sjis)
{
    if (!sjis) {
        return;
    }
    Term_putstr(x, y, -1, (byte)attr, (cptr)sjis);
}

void tb_term_present(void)
{
    /* `Term_fresh()` の締めが `TERM_XTRA_FRESH` を起こし、null term がフックの
     * `present` を呼ぶ（`tb_null_term.c`）。つまりこれ 1 つで frame が線に乗る。 */
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
 * **日本語モードでは 1 ビットも動きが変わらない**（`tb_lookup()` が
 * `tb_lang_enabled()` を見て素通りする）。
 *
 * 枠は `msg_format()` と同じ 1024 バイト。溢れたぶんは切れる（`fprintf` は
 * 切らなかったが、画面は 80 桁なので実害は無い。設計 §11.13）。
 */
void tb_fprintf(FILE *fff, const char *fmt, ...)
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
 * @details 値で埋める所が無いので `tb_tr()` を 1 回引くだけでよい。
 */
void tb_fputs(const char *s, FILE *fff)
{
	if (!fff || !s) return;

	fputs(tb_tr(s), fff);
}

/*!
 * @brief `sprintf()` の代わり。書き込み先の大きさを見ない版（フック #18。`tb_lang_c.h` の `tb_sprintf_u`）。
 * @details `vstrnfmt()` の `max == 0` は「無制限（32767）」。書式と `%s` の引数は #2・#3 が引く。
 */
int tb_sprintf_u(char *dest, const char *fmt, ...)
{
	va_list vp;
	uint n;

	va_start(vp, fmt);
	n = vstrnfmt(dest, 0, (cptr)fmt, vp);
	va_end(vp);

	return (int)n;
}

/* Report only the core command input boundary, never a nested prompt. */
int tb_awaiting_command(void)
{
    return character_generated && !p_ptr->is_dead && inkey_flag;
}

/* ============================================================ 状態のアイコン（`GameFrame::status_effects`） */

/*
 * の TOband 版（幻想蛮怒と同じ流れ）。
 *
 * ## 取り方
 * - **一時的な状態**は、原作の `prt_status()`（`xtra1.c`。static）が作る旗の配列 `bar_flags` から取る。
 *   `tools/targeting/overlay.py`（`build_toband`）が `prt_status()` の「長さを数える」の手前に 1 行足し、
 *   立っている旗の番号・名札（原作が画面へ出す字）・色を `tb_status_record()` へ渡す。
 *   記録するだけで、旗にも画面にも乱数にも触れない。TOband の `bar[]` は切り傷（7 段）・朦朧（4 段）も
 *   持つので、それらも同じ道で来る。
 * - 速さと空腹は、原作の左の状態欄・最下段（`prt_speed`・`prt_hunger`）が**画面へ出した字**を読む。
 *   原作の式（しきい値・乗馬の分岐）をこちらへ写さない。画面を触っていない間（icky・再計算中）は
 *   直前に読めた字を使い回す。
 * - **常時の効果**は C 画面（`files.c` の `display_player_flag_info` ほか）の表と同じ範囲。
 *   **鑑定済みの旗だけ**（`object_flags_known`）と、種族・職業などの固有の旗（`player_flags` など。
 *   static なので overlay.py が `files.c` の末尾へ足す読み取りの関数 `tb_native_player_flags` から取る）。
 *   一時的な効果（画面の `#`）は載せない。
 *
 * ## 種類（`StatusEffectKind`）
 * 0 良い・1 悪い・2 待っている・3 特殊・4 常時の良い・5 常時の悪い。
 */

#define TB_BAR_COUNT 62 /* xtra1.c の BAR_MERMAID（61）+ 1。tools/toband/tb_status_table_check.py が原作と突き合わせる */

#define TB_SK_GOOD 0
#define TB_SK_BAD 1
#define TB_SK_WAIT 2
#define TB_SK_SPECIAL 3
#define TB_SK_PERM 4
#define TB_SK_PERM_BAD 5

#define TB_L(ja, en) (tb_lang_enabled() ? (en) : (ja))

typedef struct tb_bar_icon {
    const char *key;
    unsigned char kind;
} tb_bar_icon;

/*!
 * `BAR_*` の番号で引く。鍵は `hd2d/ui/status_icon_table.inc` にあるものだけ。
 * 切り傷（0〜6）・朦朧（7〜10）は段階ごとの項目だが、絵は 1 つずつ（`cut`・`stun`）。
 * 変愚・幻想蛮怒・Frox の既存の絵に寄せたものは鍵がそのコアの名前のまま（`fc_res_teleport` など）。
 * 描き足したのは `tb_` の 8 個（`tools/hud/status_icons_toband.py`）。
 */
static const tb_bar_icon tb_bar_icons[TB_BAR_COUNT] = {
    /*  0 */ { "cut", TB_SK_BAD }, /* BAR_CUT_BASE（かすり傷） */
    /*  1 */ { "cut", TB_SK_BAD }, /* BAR_CUT_BASE+1（軽傷） */
    /*  2 */ { "cut", TB_SK_BAD }, /* BAR_CUT_BASE+2（ひどい傷） */
    /*  3 */ { "cut", TB_SK_BAD }, /* BAR_CUT_BASE+3（大変な傷） */
    /*  4 */ { "cut", TB_SK_BAD }, /* BAR_CUT_BASE+4（重傷） */
    /*  5 */ { "cut", TB_SK_BAD }, /* BAR_CUT_BASE+5（ひどい深手） */
    /*  6 */ { "cut", TB_SK_BAD }, /* BAR_CUT_BASE+6（致命傷） */
    /*  7 */ { "stun", TB_SK_BAD }, /* BAR_STUN_BASE（朦朧） */
    /*  8 */ { "stun", TB_SK_BAD }, /* BAR_STUN_BASE+1（ひどく朦朧） */
    /*  9 */ { "stun", TB_SK_BAD }, /* BAR_STUN_BASE+2（気絶寸前） */
    /* 10 */ { "stun", TB_SK_BAD }, /* BAR_STUN_BASE+3（意識不明瞭） */
    /* 11 */ { "hallucination", TB_SK_BAD }, /* BAR_HALLUCINATION */
    /* 12 */ { "blind", TB_SK_BAD }, /* BAR_BLINDNESS */
    /* 13 */ { "paralyzed", TB_SK_BAD }, /* BAR_PARALYZE */
    /* 14 */ { "confused", TB_SK_BAD }, /* BAR_CONFUSE */
    /* 15 */ { "poisoned", TB_SK_BAD }, /* BAR_POISONED */
    /* 16 */ { "afraid", TB_SK_BAD }, /* BAR_AFRAID */
    /* 17 */ { "tb_stoning", TB_SK_BAD }, /* BAR_STONING（石化） */
    /* 18 */ { "wraith", TB_SK_GOOD }, /* BAR_WRAITH */
    /* 19 */ { "protevil", TB_SK_GOOD }, /* BAR_PROTEVIL */
    /* 20 */ { "magicdef", TB_SK_GOOD }, /* BAR_MAGICDEFENSE */
    /* 21 */ { "stoneskin", TB_SK_GOOD }, /* BAR_STONESKIN */
    /* 22 */ { "multishadow", TB_SK_GOOD }, /* BAR_MULTISHADOW */
    /* 23 */ { "invuln", TB_SK_GOOD }, /* BAR_INVULN */
    /* 24 */ { "res_acid", TB_SK_GOOD }, /* BAR_RESACID */
    /* 25 */ { "res_elec", TB_SK_GOOD }, /* BAR_RESELEC */
    /* 26 */ { "res_fire", TB_SK_GOOD }, /* BAR_RESFIRE */
    /* 27 */ { "res_cold", TB_SK_GOOD }, /* BAR_RESCOLD */
    /* 28 */ { "res_pois", TB_SK_GOOD }, /* BAR_RESPOIS */
    /* 29 */ { "res_time", TB_SK_GOOD }, /* BAR_RESTIME */
    /* 30 */ { "dustrobe", TB_SK_GOOD }, /* BAR_DUSTROBE */
    /* 31 */ { "sh_fire", TB_SK_GOOD }, /* BAR_SHFIRE */
    /* 32 */ { "sh_elec", TB_SK_GOOD }, /* BAR_SHELEC */
    /* 33 */ { "sh_cold", TB_SK_GOOD }, /* BAR_SHCOLD */
    /* 34 */ { "sh_holy", TB_SK_GOOD }, /* BAR_SHHOLY */
    /* 35 */ { "touki", TB_SK_GOOD }, /* BAR_SHAURA（闘気） */
    /* 36 */ { "eyeeye", TB_SK_GOOD }, /* BAR_EYEEYE */
    /* 37 */ { "blessed", TB_SK_GOOD }, /* BAR_BLESSED */
    /* 38 */ { "hero", TB_SK_GOOD }, /* BAR_HEROISM */
    /* 39 */ { "berserk", TB_SK_GOOD }, /* BAR_BERSERK */
    /* 40 */ { "runesword", TB_SK_GOOD }, /* BAR_ATTKEVIL（邪剣。善良・生命への倍打が付く） */
    /* 41 */ { "attk_fire", TB_SK_GOOD }, /* BAR_ATTKFIRE */
    /* 42 */ { "attk_cold", TB_SK_GOOD }, /* BAR_ATTKCOLD */
    /* 43 */ { "attk_elec", TB_SK_GOOD }, /* BAR_ATTKELEC */
    /* 44 */ { "attk_acid", TB_SK_GOOD }, /* BAR_ATTKACID */
    /* 45 */ { "attk_pois", TB_SK_GOOD }, /* BAR_ATTKPOIS */
    /* 46 */ { "attk_conf", TB_SK_GOOD }, /* BAR_ATTKCONF */
    /* 47 */ { "senseunseen", TB_SK_GOOD }, /* BAR_SENSEUNSEEN */
    /* 48 */ { "telepathy", TB_SK_GOOD }, /* BAR_TELEPATHY */
    /* 49 */ { "infravision", TB_SK_GOOD }, /* BAR_INFRAVISION */
    /* 50 */ { "recall", TB_SK_WAIT }, /* BAR_RECALL */
    /* 51 */ { "alter", TB_SK_WAIT }, /* BAR_ALTER */
    /* 52 */ { "tb_no_flood", TB_SK_WAIT }, /* BAR_INH_FLOOD（洪水禁止。しばらく大洪水の魔法が使えない） */
    /* 53 */ { "tb_reverted", TB_SK_SPECIAL }, /* BAR_REVERTED（反転。エレメントが反転する） */
    /* 54 */ { "tb_charge_spell", TB_SK_GOOD }, /* BAR_CHARGESPELL（魔力） */
    /* 55 */ { "fc_res_teleport", TB_SK_GOOD }, /* BAR_EARTHSPIKE（大地の楔。他者のテレポートを受けない） */
    /* 56 */ { "invuln_arrow", TB_SK_GOOD }, /* BAR_WINDGUARD（風の護り。射撃をかわす） */
    /* 57 */ { "tb_resurrection", TB_SK_GOOD }, /* BAR_RESURRECTION */
    /* 58 */ { "blows", TB_SK_GOOD }, /* BAR_INC_BLOW（攻撃増加） */
    /* 59 */ { "tb_dec_blow", TB_SK_BAD }, /* BAR_DEC_BLOW（攻撃減少） */
    /* 60 */ { "tb_zoshonel", TB_SK_GOOD }, /* BAR_ZOSHONEL */
    /* 61 */ { "tb_mermaid", TB_SK_GOOD }, /* BAR_MERMAID（水中行動） */
};

/*! 表の組み方の外から見える数（自己検査と check スクリプト用）。 */
int tb_status_bar_table_count(void)
{
    return TB_BAR_COUNT;
}

const char *tb_status_bar_table_key(int bar, int *kind)
{
    if ((bar < 0) || (bar >= TB_BAR_COUNT)) {
        return NULL;
    }
    if (kind) {
        *kind = (int)tb_bar_icons[bar].kind;
    }
    return tb_bar_icons[bar].key;
}

/* ---------------------------------------------------------------- 記録（overlay.py が足した 1 行の呼び先） */

static unsigned char tb_status_on[TB_BAR_COUNT];
static char tb_status_label[TB_BAR_COUNT][48];
static unsigned char tb_status_attr[TB_BAR_COUNT];
static int tb_status_overflow; /* 原作の `bar[]` が表より長い（原作が版上げで増えた）ときの印 */
static unsigned long tb_status_hash;

/*!
 * @brief `prt_status()` が組んだ旗を覚える。`bar < 0` は「組み直しの始まり」（`attr` に原作の `bar[]` の行数）。
 * @details 名札は原作が画面へ出す字（CP932 か英語）。全角を割らないように切る。
 */
void tb_status_record(int bar, const char *label, int attr)
{
    if (bar < 0) {
        memset(tb_status_on, 0, sizeof(tb_status_on));
        tb_status_hash = 2166136261UL;
        tb_status_overflow = (attr > TB_BAR_COUNT) ? 1 : 0;
        return;
    }
    if (bar >= TB_BAR_COUNT) {
        tb_status_overflow = 1;
        return;
    }
    tb_status_on[bar] = 1;
    tb_status_attr[bar] = (unsigned char)attr;
    {
        int i = 0;
        const char *s = label ? label : "";
        while (s[i] && (i < (int)sizeof(tb_status_label[bar]) - 1)) {
            unsigned char c = (unsigned char)s[i];
            int lead = ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));
            if (lead && (!s[i + 1] || (i + 2 > (int)sizeof(tb_status_label[bar]) - 1))) {
                break;
            }
            tb_status_label[bar][i] = s[i];
            i++;
            if (lead) {
                tb_status_label[bar][i] = s[i];
                i++;
            }
        }
        tb_status_label[bar][i] = '\0';
    }
    tb_status_hash = (tb_status_hash ^ (unsigned long)(bar + 1)) * 16777619UL;
}

/*! 旗の組が変わったら変わる値（`tb_change_digest` に混ぜる）。 */
unsigned long tb_status_stamp(void)
{
    return tb_status_hash;
}

/* ---------------------------------------------------------------- 速さ・空腹（画面に出た字を読む） */

#define TB_SCAN_SPEED_CELLS 9 /* 最下段の速さの欄の幅 */
#define TB_SCAN_HUNGER_CELLS 6 /* 左の状態欄の空腹の欄の幅 */

static char tb_scan_speed[24]; /*!< 速さの欄の字（CP932。前後の空白を除く）。空なら出ていない */
static char tb_scan_hunger[16]; /*!< 空腹の欄の字（同上） */

/*!
 * @brief Term の 1 行の `[x0, x0 + n)` を字にして `dst` へ入れる（前後の空白・制御文字を除く）。
 * @return 読めたら 1、行が読めなかったら 0（そのとき `dst` は触らない）
 */
static int tb_scan_cells(int y, int x0, int n, char *dst, int cap)
{
    char cells[256];
    unsigned char attrs[256];
    int got;
    int begin;
    int end;
    int len;

    if ((x0 < 0) || (n <= 0) || (n >= cap)) {
        return 0;
    }
    got = tb_term_row(y, cells, attrs, (int)sizeof(cells));
    if ((got < 0) || ((x0 + n) > got)) {
        return 0;
    }
    begin = x0;
    end = x0 + n;
    while ((begin < end) && (((unsigned char)cells[begin] <= ' ') || ((unsigned char)cells[begin] == 0x7F))) {
        begin++;
    }
    while ((end > begin) && (((unsigned char)cells[end - 1] <= ' ') || ((unsigned char)cells[end - 1] == 0x7F))) {
        end--;
    }
    len = end - begin;
    memcpy(dst, cells + begin, (size_t)len);
    dst[len] = '\0';
    return 1;
}

/*!
 * @brief 速さ・空腹の欄を読み直す。画面を触っていない間（icky・再計算中）は直前の値のまま。
 * @details 欄の位置は原作の `ROW_*`・`COL_*`（`defines.h`）。字の中身の判定は `tb_read_status_effects` が持つ。
 */
static void tb_status_scan_screen(void)
{
    const int flags = tb_screen_flags();
    int cols = 0;
    int rows = 0;

    if (((flags & TB_SCREEN_GENERATED) == 0) || ((flags & TB_SCREEN_DUNGEON) == 0) || ((flags & TB_SCREEN_DEAD) != 0)) {
        tb_scan_speed[0] = '\0';
        tb_scan_hunger[0] = '\0';
        return;
    }
    if ((flags & (TB_SCREEN_ICKY | TB_SCREEN_XTRA)) != 0) {
        return;
    }
    if (tb_term_size(&cols, &rows) != TB_OK) {
        return;
    }
    (void)tb_scan_cells(ROW_HUNGRY, COL_HUNGRY, TB_SCAN_HUNGER_CELLS, tb_scan_hunger, (int)sizeof(tb_scan_hunger));
    (void)tb_scan_cells(rows + ROW_SPEED, cols + COL_SPEED, TB_SCAN_SPEED_CELLS, tb_scan_speed,
        (int)sizeof(tb_scan_speed));
}

/* ---------------------------------------------------------------- 常時の効果の表 */

#define TB_PM_NORMAL 0 /* 装備の枠 12 + 固有の旗 */
#define TB_PM_WEAPON 1 /* 利き手・逆手 + 固有の旗（`DP_WP`） */
#define TB_PM_WEAPON_B 2 /* 利き手・逆手・弓 + 固有の旗（`DP_WP_B`） */
#define TB_PM_CURSE 3 /* 装備の呪い（`DP_CURSE`） */

typedef struct tb_perm_row {
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
} tb_perm_row;

#define TBP(flag, key, ja, en) { flag, -1, TB_PM_NORMAL, TB_SK_PERM, key, ja, en, NULL, NULL, NULL }
#define TBP_BAD(flag, key, ja, en) { flag, -1, TB_PM_NORMAL, TB_SK_PERM_BAD, key, ja, en, NULL, NULL, NULL }
#define TBP_IMM(flag, gr, key, ja, en, ikey, ija, ien) { flag, gr, TB_PM_NORMAL, TB_SK_PERM, key, ja, en, ikey, ija, ien }
#define TBP_WP(flag, gr, key, ja, en) { flag, gr, TB_PM_WEAPON, TB_SK_PERM, key, ja, en, NULL, NULL, NULL }
#define TBP_WPB(flag, gr, key, ja, en) { flag, gr, TB_PM_WEAPON_B, TB_SK_PERM, key, ja, en, NULL, NULL, NULL }

/*!
 * C 画面（`display_player_flag_info` / `display_player_other_flag_info`）の行の順。
 * 乱テレポート（`TR_TELEPORT`）・恐怖空間・不浄・生命への倍打・耐石化は `tb_` の絵を描いた。
 */
static const tb_perm_row tb_perm_rows[] = {
    /* ---- ページ 1 ---- */
    TBP_IMM(TR_RES_ACID, TR_IM_ACID, "res_acid", "耐酸", "Resist Acid", "imm_acid", "酸免疫", "Immune Acid"),
    TBP_IMM(TR_RES_ELEC, TR_IM_ELEC, "res_elec", "耐電撃", "Resist Elec", "imm_elec", "電撃免疫", "Immune Elec"),
    TBP_IMM(TR_RES_FIRE, TR_IM_FIRE, "res_fire", "耐火炎", "Resist Fire", "imm_fire", "火炎免疫", "Immune Fire"),
    TBP_IMM(TR_RES_COLD, TR_IM_COLD, "res_cold", "耐冷気", "Resist Cold", "imm_cold", "冷気免疫", "Immune Cold"),
    TBP(TR_RES_POIS, "res_pois", "耐毒", "Resist Poison"),
    TBP(TR_RES_LITE, "res_lite", "耐閃光", "Resist Light"),
    TBP_IMM(TR_RES_DARK, -1, "res_dark", "耐暗黒", "Resist Dark", "imm_dark", "暗黒免疫", "Immune Dark"),
    TBP(TR_RES_SHARDS, "res_shard", "耐破片", "Resist Shards"),
    TBP(TR_RES_BLIND, "res_blind", "耐盲目", "Resist Blind"),
    TBP(TR_RES_CONF, "res_conf", "耐混乱", "Resist Conf"),
    /* ---- ページ 2 ---- */
    TBP(TR_RES_SOUND, "res_sound", "耐轟音", "Resist Sound"),
    TBP(TR_RES_NETHER, "res_neth", "耐地獄", "Resist Nether"),
    TBP(TR_RES_STONE, "tb_res_stone", "耐石化", "Resist Stone"),
    TBP(TR_RES_CHAOS, "res_chaos", "耐カオス", "Resist Chaos"),
    TBP(TR_RES_DISEN, "res_disen", "耐劣化", "Resist Disen"),
    TBP(TR_RES_FEAR, "res_fear", "耐恐怖", "Resist Fear"),
    TBP(TR_REFLECT, "reflect", "反射", "Reflection"),
    TBP(TR_SH_FIRE, "sh_fire", "火炎オーラ", "Fire Aura"),
    TBP(TR_SH_ELEC, "sh_elec", "電気オーラ", "Elec Aura"),
    TBP(TR_SH_COLD, "sh_cold", "冷気オーラ", "Cold Aura"),
    /* ---- ページ 3 ---- */
    TBP(TR_SPEED, "haste", "加速", "Speed"),
    TBP(TR_FREE_ACT, "free_act", "耐麻痺", "Free Action"),
    TBP(TR_SEE_INVIS, "senseunseen", "透明体視認", "See Invisible"),
    TBP(TR_HOLD_LIFE, "hold_exp", "経験値保持", "Hold Life"),
    TBP(TR_WARNING, "warning", "警告", "Warning"),
    TBP(TR_SLOW_DIGEST, "slow_digest", "遅消化", "Slow Digest"),
    TBP(TR_REGEN, "regeneration", "急回復", "Regeneration"),
    TBP(TR_LEVITATION, "levitate", "浮遊", "Levitation"),
    TBP(TR_LITE, "lite", "永遠光源", "Perm Light"),
    { -1, -1, TB_PM_CURSE, TB_SK_PERM_BAD, "cursed", "呪い", "Cursed", NULL, NULL, NULL },
    /* ---- 武器の性質（利き手・逆手・弓） ---- */
    TBP_WPB(TR_SLAY_EVIL, TR_KILL_EVIL, "slay_evil", "邪悪倍打", "Slay Evil"),
    TBP_WPB(TR_SLAY_GOOD, TR_KILL_GOOD, "slay_good", "善良倍打", "Slay Good"),
    TBP_WPB(TR_SLAY_UNDEAD, TR_KILL_UNDEAD, "slay_undead", "不死倍打", "Slay Undead"),
    TBP_WPB(TR_SLAY_LIVING, TR_KILL_LIVING, "tb_slay_living", "生命倍打", "Slay Living"),
    TBP_WPB(TR_SLAY_DEMON, TR_KILL_DEMON, "slay_demon", "悪魔倍打", "Slay Demon"),
    TBP_WPB(TR_SLAY_DRAGON, TR_KILL_DRAGON, "slay_dragon", "龍倍打", "Slay Dragon"),
    TBP_WPB(TR_SLAY_HUMAN, TR_KILL_HUMAN, "slay_human", "人間倍打", "Slay Human"),
    TBP_WPB(TR_SLAY_ANIMAL, TR_KILL_ANIMAL, "slay_animal", "動物倍打", "Slay Animal"),
    TBP_WPB(TR_SLAY_ORC, TR_KILL_ORC, "slay_orc", "オーク倍打", "Slay Orc"),
    TBP_WPB(TR_SLAY_TROLL, TR_KILL_TROLL, "slay_troll", "トロル倍打", "Slay Troll"),
    TBP_WPB(TR_SLAY_GIANT, TR_KILL_GIANT, "slay_giant", "巨人倍打", "Slay Giant"),
    TBP_WPB(TR_BRAND_ACID, -1, "attk_acid", "溶解", "Acid Brand"),
    TBP_WPB(TR_BRAND_ELEC, -1, "attk_elec", "電撃", "Elec Brand"),
    TBP_WPB(TR_BRAND_FIRE, -1, "attk_fire", "焼棄", "Fire Brand"),
    TBP_WPB(TR_BRAND_COLD, -1, "attk_cold", "凍結", "Cold Brand"),
    TBP_WPB(TR_BRAND_POIS, -1, "attk_pois", "毒殺", "Poison Brand"),
    TBP_WPB(TR_UNHOLY, -1, "tb_unholy", "不浄", "Unholy"),
    TBP_WP(TR_VORPAL, TR_EXTRA_VORPAL, "vorpal", "切れ味", "Sharpness"),
    TBP_WP(TR_IMPACT, -1, "earthquake", "地震", "Quake"),
    TBP_WP(TR_VAMPIRIC, -1, "vampiric", "吸血", "Vampiric"),
    TBP_WP(TR_CHAOTIC, -1, "chaotic", "カオス効果", "Chaotic"),
    TBP_WP(TR_FORCE_WEAPON, -1, "force_weapon", "理力", "Force Weapon"),
    TBP_WP(TR_RIDING, -1, "riding", "乗馬", "For Riding"),
    TBP_WP(TR_THROW, -1, "throw", "投擲", "Throwing"),
    /* ---- 能力値の維持・魔法 ---- */
    TBP(TR_SUST_STR, "sust_str", "腕力維持", "Sust Str"),
    TBP(TR_SUST_INT, "sust_int", "知能維持", "Sust Int"),
    TBP(TR_SUST_WIS, "sust_wis", "賢さ維持", "Sust Wis"),
    TBP(TR_SUST_DEX, "sust_dex", "器用維持", "Sust Dex"),
    TBP(TR_SUST_CON, "sust_con", "耐久維持", "Sust Con"),
    TBP(TR_SUST_CHR, "sust_chr", "魅力維持", "Sust Chr"),
    TBP(TR_RES_MAGIC, "resmagic", "耐魔法", "Res. Magic"),
    TBP_BAD(TR_NO_MAGIC, "no_magic", "反魔法", "Anti-Magic"),
    TBP(TR_ANTI_MAGIC, "antimagic", "封魔空間", "AM Field"),
    /* ---- 能力・その他 ---- */
    TBP(TR_BLOWS, "blows", "追加攻撃", "Add Blows"),
    TBP(TR_TUNNEL, "tunnel", "採掘", "Add Tunnel"),
    TBP(TR_INFRA, "infravision", "赤外線視力", "Add Infra"),
    TBP(TR_MAGIC_MASTERY, "magic_mastery", "魔法道具支配", "Add Device"),
    TBP(TR_STEALTH, "stealth", "隠密", "Add Stealth"),
    TBP(TR_SEARCH, "search", "探索", "Add Search"),
    TBP(TR_TELEPATHY, "telepathy", "テレパシー", "Telepathy"),
    TBP(TR_BLESSED, "blessed", "祝福", "Blessed"),
    TBP_BAD(TR_NO_TELE, "no_tele", "反テレポート", "No Teleport"),
    TBP(TR_REGEN_MANA, "fc_sp_regen", "魔力急回復", "Regen. Mana"),
    TBP(TR_DEC_MANA, "dec_mana", "消費魔力減少", "Econom. Mana"),
    TBP(TR_EASY_SPELL, "easy_spell", "難易度減少", "Easy Spell"),
    TBP(TR_WRAITH, "wraith", "幽体化", "Wraith Form"),
    TBP(TR_FEAR_FIELD, "tb_fear_field", "恐怖空間", "Fear Field"),
    TBP_BAD(TR_DRAIN_EXP, "drain_exp", "経験値減少", "Drain Exp"),
    TBP_BAD(TR_TELEPORT, "tb_rnd_tele", "乱テレポート", "Rnd.Teleport"),
    TBP_BAD(TR_AGGRAVATE, "aggravate", "反感", "Aggravate"),
    TBP_BAD(TR_TY_CURSE, "ty_curse", "太古の怨念", "TY Curse"),
};

#define TB_PERM_ROWS ((int)(sizeof(tb_perm_rows) / sizeof(tb_perm_rows[0])))

int tb_status_perm_row_count(void)
{
    return TB_PERM_ROWS;
}

const char *tb_status_perm_row_key(int row, int *kind, const char **imm_key)
{
    if ((row < 0) || (row >= TB_PERM_ROWS)) {
        return NULL;
    }
    if (kind) {
        *kind = (int)tb_perm_rows[row].kind;
    }
    if (imm_key) {
        *imm_key = tb_perm_rows[row].imm_key;
    }
    return tb_perm_rows[row].key;
}

/* 原作の `files.c` の末尾へ overlay.py が足す読み取りの関数（static な旗の関数を外へ出すだけ）。 */
extern void tb_native_player_flags(u32b *player, u32b *tim, u32b *imm, u32b *tim_imm);

typedef struct tb_perm_ctx {
    u32b slot[INVEN_TOTAL][TR_FLAG_SIZE]; /*!< 装備の枠ごとの鑑定済みの旗 */
    unsigned char used[INVEN_TOTAL]; /*!< 枠に物があって数える */
    u32b player[TR_FLAG_SIZE]; /*!< 種族・職業などの固有の旗 */
    u32b imm[TR_FLAG_SIZE]; /*!< 固有の免疫（耐性の旗そのものを立てて来る） */
} tb_perm_ctx;

static void tb_perm_collect(tb_perm_ctx *c)
{
    u32b tim[TR_FLAG_SIZE];
    u32b tim_imm[TR_FLAG_SIZE];
    int i;

    memset(c, 0, sizeof(*c));
    for (i = INVEN_RARM; i < INVEN_TOTAL; i++) {
        object_type *o_ptr = tb_pack_slot(i);

        if (!o_ptr->k_idx) {
            continue;
        }
        object_flags_known(o_ptr, c->slot[i]);
        c->used[i] = 1;
    }
    tb_native_player_flags(c->player, tim, c->imm, tim_imm);
}

/*!
 * @brief 1 行の判定。C 画面の `display_flag_aux` と同じ範囲。一時的な効果（`tim` 側）は見ない。
 * @return 0 = 無い、1 = 耐性・能力あり、2 = 免疫あり（`imm_key` があるとき）
 */
static int tb_perm_row_state(const tb_perm_ctx *c, const tb_perm_row *r)
{
    int i;
    int max_i = INVEN_TOTAL;
    int has = 0;
    int imm = 0;

    if (r->mode == TB_PM_CURSE) {
        for (i = INVEN_RARM; i < INVEN_TOTAL; i++) {
            object_type *o_ptr = tb_pack_slot(i);

            if (o_ptr->k_idx && (o_ptr->curse_flags & (TRC_CURSED | TRC_HEAVY_CURSE | TRC_PERMA_CURSE))) {
                return 1;
            }
        }
        return 0;
    }
    if (r->mode == TB_PM_WEAPON) {
        max_i = INVEN_LARM + 1;
    } else if (r->mode == TB_PM_WEAPON_B) {
        max_i = INVEN_BOW + 1;
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
    /* 種族・職業などの固有の旗。免疫（`imm`）は耐性の旗そのものを立てて来る。 */
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

static int tb_status_push(tb_status_effect *out, int *n, int cap, const char *key, const char *label_sjis,
    const char *amount, int kind)
{
    int i;
    tb_status_effect *e;

    if (!key || !key[0]) {
        return 0;
    }
    for (i = 0; i < *n; i++) { /* 同じ鍵・同じ種類の重複は 1 つにする（切り傷の段階など） */
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

/*!
 * @brief 速さの欄の字（「加速(+10)」「減速(-5)」「乗馬(+3)」。英語は「Fast(+10)」）から、符号と数を取る。
 * @param label 括弧の前の名前を入れる
 * @param amount 符号付きの数（`+10`）を入れる
 * @return 符号（+1 / -1）。速さを示す字でなければ 0（「乗馬中」など）
 */
static int tb_speed_parse(const char *text, char *label, int lcap, char *amount, int acap)
{
    const char *open = strchr(text, '(');
    int sign;
    int len;
    int i = 0;

    if (!open || ((open[1] != '+') && (open[1] != '-')) || (open[2] < '0') || (open[2] > '9')) {
        return 0;
    }
    sign = (open[1] == '+') ? 1 : -1;
    len = (int)(open - text);
    if (len >= lcap) {
        len = lcap - 1;
    }
    memcpy(label, text, (size_t)len);
    label[len] = '\0';
    amount[i++] = open[1];
    for (len = 2; (open[len] >= '0') && (open[len] <= '9') && (i < (acap - 1)); len++) {
        amount[i++] = open[len];
    }
    amount[i] = '\0';
    return sign;
}

int tb_read_status_effects(tb_status_effect *out, int cap)
{
    int n = 0;
    int i;

    if (!out || (cap <= 0) || !character_generated || !p_ptr || p_ptr->is_dead) {
        return 0;
    }
    if (tb_status_overflow) { /* 原作の `bar[]` が表より長い。版上げで BAR_* が増えたので表を足す（一度だけ知らせる） */
        static int warned;

        if (!warned) {
            warned = 1;
            fprintf(stderr, "[toband] status icons: the original bar[] is longer than TB_BAR_COUNT (%d); extend tb_bar_icons[]\n",
                TB_BAR_COUNT);
        }
    }

    tb_status_scan_screen();

    /* (1) 速さ。原作が左下に出した字（探索中の見かけ・乗馬の分岐を含む）を、そのまま使う。 */
    if (tb_scan_speed[0]) {
        char label[24];
        char amount[16];
        const int sign = tb_speed_parse(tb_scan_speed, label, (int)sizeof(label), amount, (int)sizeof(amount));

        if (sign > 0) {
            tb_status_push(out, &n, cap, "haste", label, amount, TB_SK_GOOD);
        } else if (sign < 0) {
            tb_status_push(out, &n, cap, "slow", label, amount, TB_SK_BAD);
        }
    }

    /* (2) 状態の旗（切り傷・朦朧を含む）。`BAR_*` の小さい順＝原作の最下段の並び。 */
    for (i = 0; i < TB_BAR_COUNT; i++) {
        if (tb_status_on[i]) {
            tb_status_push(out, &n, cap, tb_bar_icons[i].key, tb_status_label[i], NULL, (int)tb_bar_icons[i].kind);
        }
    }

    /* (3) 空腹。原作が左の状態欄に出した字。満腹は悪い状態ではないので出さない。 */
    if (tb_scan_hunger[0] && (strncmp(tb_scan_hunger, "満腹", 4) != 0) && (strncmp(tb_scan_hunger, "Full", 4) != 0)) {
        tb_status_push(out, &n, cap, "hungry", tb_scan_hunger, NULL, TB_SK_BAD);
    }

    /* (4) 常時の効果（C 画面の耐性・能力の表。鑑定済みの分だけ）。 */
    {
        static tb_perm_ctx ctx; /* ゲームのスレッドは 1 本。大きいので静的に持つ */

        tb_perm_collect(&ctx);
        for (i = 0; i < TB_PERM_ROWS; i++) {
            const tb_perm_row *r = &tb_perm_rows[i];
            const int st = tb_perm_row_state(&ctx, r);

            if (st == 2) {
                tb_status_push(out, &n, cap, r->imm_key, TB_L(r->imm_ja, r->imm_en), NULL, r->kind);
            } else if (st == 1) {
                tb_status_push(out, &n, cap, r->key, TB_L(r->ja, r->en), NULL, r->kind);
            }
        }
    }
    return n;
}

/*!
 * @brief 状態のアイコンの表の自己検査（キャラクター不要）。
 * @details 表の鍵が空でない・長さの上限内・種類が 0..3（一時的）・常時の効果の行の旗が範囲内・種類が 4 か 5、
 * 速さの字の読み取りが決めた形どおり、を見る。画面側の表（`status_icon_table.inc`）との突き合わせは、
 * 環境変数 `HENGBAND_STATUS_ICON_TABLE` にそのファイルの絶対パスがあるときだけここで行う
 * （無ければ `*checked` を 0 のまま返し、突き合わせは `tools/toband/tb_status_table_check.py` が行う）。
 * @return 外れた件数（0 なら全部合っている）
 */
int tb_status_selftest(char *err, int cap, int *checked)
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
#define TB_ST_FAIL(...) \
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
            TB_ST_FAIL("icon table unreadable: %.100s", path);
        }
    }

    for (i = 0; i < TB_BAR_COUNT; i++) {
        const tb_bar_icon *b = &tb_bar_icons[i];

        if (!b->key || !b->key[0] || (strlen(b->key) >= sizeof(((tb_status_effect *)0)->key))) {
            TB_ST_FAIL("bar %d: bad key", i);
            continue;
        }
        if (b->kind > TB_SK_SPECIAL) {
            TB_ST_FAIL("bar %d (%s): kind %d is not a temporary kind", i, b->key, (int)b->kind);
        }
        if (table) {
            char needle[64];

            sprintf(needle, "{ \"%s\",", b->key);
            if (!strstr(table, needle)) {
                TB_ST_FAIL("bar %d: key '%s' is not in the icon table", i, b->key);
            }
            if (checked) {
                (*checked)++;
            }
        }
    }
    for (i = 0; i < TB_PERM_ROWS; i++) {
        const tb_perm_row *r = &tb_perm_rows[i];

        if (!r->key || !r->key[0] || !r->ja || !r->en) {
            TB_ST_FAIL("perm row %d: missing key or label", i);
            continue;
        }
        if ((r->kind != TB_SK_PERM) && (r->kind != TB_SK_PERM_BAD)) {
            TB_ST_FAIL("perm row %d (%s): kind %d is not permanent", i, r->key, (int)r->kind);
        }
        if ((r->mode != TB_PM_CURSE) && ((r->flag < 0) || (r->flag >= TR_FLAG_MAX))) {
            TB_ST_FAIL("perm row %d (%s): flag %d out of range", i, r->key, (int)r->flag);
        }
        if (r->imm_key && (!r->imm_ja || !r->imm_en)) {
            TB_ST_FAIL("perm row %d (%s): immune label missing", i, r->key);
        }
        if (table) {
            char needle[64];

            sprintf(needle, "{ \"%s\",", r->key);
            if (!strstr(table, needle)) {
                TB_ST_FAIL("perm row %d: key '%s' is not in the icon table", i, r->key);
            }
            if (checked) {
                (*checked)++;
            }
            if (r->imm_key) {
                sprintf(needle, "{ \"%s\",", r->imm_key);
                if (!strstr(table, needle)) {
                    TB_ST_FAIL("perm row %d: key '%s' is not in the icon table", i, r->imm_key);
                }
                if (checked) {
                    (*checked)++;
                }
            }
        }
    }
    /* 速さ・空腹の鍵（`tb_read_status_effects` が直に書く）。 */
    if (table) {
        static const char *const direct[] = { "haste", "slow", "hungry" };

        for (i = 0; i < 3; i++) {
            char needle[64];

            sprintf(needle, "{ \"%s\",", direct[i]);
            if (!strstr(table, needle)) {
                TB_ST_FAIL("direct key '%s' is not in the icon table", direct[i]);
            }
            if (checked) {
                (*checked)++;
            }
        }
    }
    /* 速さの字の読み取り（符号・数・乗馬中は読まない）。 */
    {
        char label[24];
        char amount[16];

        if ((tb_speed_parse("加速(+10)", label, (int)sizeof(label), amount, (int)sizeof(amount)) != 1) || strcmp(amount, "+10")) {
            TB_ST_FAIL("speed text: fast not parsed");
        }
        if ((tb_speed_parse("Slow(-5)", label, (int)sizeof(label), amount, (int)sizeof(amount)) != -1) || strcmp(amount, "-5")) {
            TB_ST_FAIL("speed text: slow not parsed");
        }
        if (tb_speed_parse("乗馬中", label, (int)sizeof(label), amount, (int)sizeof(amount)) != 0) {
            TB_ST_FAIL("speed text: riding-only must not parse");
        }
    }
    free(table);
#undef TB_ST_FAIL
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
 * コアが**プレイヤーの次のキーを読むとき**（命令待ち。`tb_awaiting_command()` が真）に、その
 * ファイルがあれば読み、**先にファイルを消して**（消せなければ名前を `.done` へ変える。どちらもできなければ
 * 実行しない）から、命令を 1 行ずつ実行する。結果は 1 行ずつ標準エラーへ出す:
 *
 *   [test-monster-act] ok <命令の全文>
 *   [test-monster-act] fail <理由>
 *   [test-monster-act] info <補足>
 *
 * 命令ファイルは**別の名前で書いてから改名して**置くこと（書きかけを読まない）。空のファイルは読まない。
 *
 * ## 複数行
 *
 * 1 つのファイルに複数行を書ける。**上から順に、1 回の命令待ちの中で全部実行する**（その間ゲームの時間は進まない）。
 * 空行と `#` で始まる行は読み飛ばす。途中で fail した行があれば、**残りの行は実行せず**、それぞれ
 * `[test-monster-act] skip <行>` を出す（前の行が効いた前提で後の行を実行して、紛らわしい結果にしないため）。
 *
 * ## 命令（1 行 1 命令。空白区切り）
 *
 *   cast <魔法の名前> [nearest|<m_idx>]  指定の敵（省略時は見えているいちばん近い敵）に、その魔法を
 *                                         プレイヤーへ向けていま撃たせる
 *   melee [nearest|<m_idx>]               指定の敵に、いまプレイヤーへ直接攻撃させる（隣にいるときだけ）
 *   place <nearest|<m_idx>> <dx> <dy>     指定の敵を、プレイヤーの位置から (dx, dy) ずれたマスへ動かす（時間は使わない）。
 *                                         地図の外・その敵が立てない地形・壁・ほかの敵（仲間のペットを含む）かプレイヤーがいる
 *                                         マスは fail。動かしたあとの更新は、原作が敵を動かすときと同じ
 *                                         （`teleport_away()` と同じ手順。ただし移動先は指定のマス）
 *   hold <nearest|<m_idx>|all> on|off     on の間、その敵は自分の番に行動しない（動かない・魔法を撃たない・殴らない）。
 *                                         入口から命じた cast / melee / place は働く。`all` は敵対している敵だけ
 *                                         （ペット・友好は止めない）で、あとから現れた敵対の敵も止める
 *   list                                  今の時刻（`info time turn=… player_x=… player_y=…`）と、階にいる敵の一覧
 *                                         （`info monster m_idx=… r_idx=… dx=… dy=… hostile=… visible=… hold=… energy=… name=<名前>`。
 *                                         dx・dy はプレイヤーからのずれ。`name=` は行の最後で、見るコマンドが使うのと同じ
 *                                         `monster_desc()`（`MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE`）の名前。見るコマンドの行は
 *                                         80 桁で切れて長い名前が映らないことがあるので、名前の確かめはこちらを読む）を出す。
 *                                         読み取りだけ（検査が `m_idx` と名前を知るための入口。乱数の状態は動かさない）
 *
 * ## hold のしくみ（原作ソースは書き換えない）
 *
 * 原作は `process_monsters()`（melee2.c）で、敵ごとに `m_ptr->energy_need`（行動の順番を待つ値。0 以下で番が来る）から
 * 速さに応じた量を引き、0 以下になったときだけ `process_monster()` を呼ぶ。そこで、命令待ちのたびに
 * （命令の各行のあとにも）止めている敵の `energy_need` を `TBT_HOLD_ENERGY`（30000）へ置き直す。1 回の命令の中で
 * 引かれる量は 1 ゲームティックに最大 49（`SPEED_TO_ENERGY`）なので、約 600 ティック（普通の速さのプレイヤーの 60 ターンほど）
 * は番が来ない。次の命令待ちでまた置き直す。止める前の値は記録に覚え、
 * `off` で戻す。眠らせてはいない（「眠っている」の表示や、起こす処理は起きない）。
 * 止めている敵が死んだ・階を移った（`floor_id` か `dun_level` が変わった）ときは、次の命令待ちで記録から外す
 * （階を移った敵は戻せない）。`hold … on` の敵が復活ではなく同じ番号に別の種族で入れ替わったときも外す。
 * 入口から命じた行動のあとは `energy_need` に原作が足す量（`cast` の一部）が残るので、各行のあとに置き直す。
 *
 * 魔法の名前は原作の旗の名前から `RF4_`・`RF5_`・`RF6_`・`RFA_` を外したもの（`BR_FIRE`・`BO_COLD`・`MISSILE` など）。
 * 大文字小文字は区別しない。知らない名前は fail。
 *
 * ## どうやって必ず撃たせるか（原作 `mspells1.c` の `make_attack_spell()` は書き換えない）
 *
 * 原作は `make_attack_spell()` の中で、種族（`r_info`）の魔法の旗からくじ引きで 1 つ選ぶ。そこで呼ぶ直前に
 * その種族の次の 4 つだけを一時的に変え、**呼んだらすぐ元の値へ戻す**（`tbt_race_restore()`。戻したあと読み直して
 * 照合し、食い違えば fail にする）。乱数と乱数の種には触れない。
 *
 *   1. `flags4`・`flags5`・`flags6`・`flagsa`  … 指定の 1 つだけを立てる（ほかは 0）
 *   2. `freq_spell`                            … 100（`no_inate` を立てるくじ `randint0(100) >= freq_spell * 2` を必ず外す）
 *   3. `flags2` の `RF2_STUPID`                … 立てる。原作の STUPID の扱いは「選ぶとき無作為（＝旗が 1 つなら必ずそれ）」
 *      「失敗率 0」「学習による除外（`remove_bad_spells`）をしない」「味方に当たるボルトの除外・召喚の除外をしない」
 *      「反魔法の床でも撃つ」。くじ運に左右される分岐をまとめて外すためのもの。原作は `r_ptr->flags2` と
 *      `ms_info[m_ptr->s_idx].flags2` を OR して見るので、種族の旗を立てれば足りる
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
 *   * 敵が混乱・敵対でない（ペット・友好）・プレイヤーに射線が通らない・射程（`MAX_RANGE`）の外（`cdis` と `ddis`）
 *   * 身代わり（`p_ptr->use_decoy`）が出ている（原作は身代わりへ向けて撃つので、プレイヤーへは撃たない）
 *   * 魔法（`spell_is_inate()` が偽のもの）で、反魔法の床（`is_anti_magic_grid`）・黙らされた敵（`MON_SILENT`・`silent_song`）・
 *     朦朧（`MON_STUNNED`）のとき。呼ぶと「呪文を唱えようとしたが失敗した」になる
 *   * 敵の今の属性が火・水・地・風でないときの「現在の属性のブレス」（`RFA_BR_PURE_ELEM`。原作がその旗を外す）
 *
 * 職業ごとの打ち消しは調べない（TOband の `make_attack_spell()` にはそれに当たる分岐が無い）。
 */

#define TBT_SLOT_4 3 /* RF4 = spell 番号 96〜 */
#define TBT_SLOT_5 4 /* RF5 = 128〜 */
#define TBT_SLOT_6 5 /* RF6 = 160〜 */
#define TBT_SLOT_A 9 /* RFA = 288〜（`32 * 9 + k`） */
#define TBT_SPELL(w, n) { #n, TBT_SLOT_##w, RF##w##_##n }

typedef struct {
    const char *name;
    int slot;
    u32b bit;
} tbt_spell_def;

static const tbt_spell_def tbt_spells[] = {
    TBT_SPELL(4, SHRIEK),
    TBT_SPELL(4, SHIFT_ELEM),
    TBT_SPELL(4, DISPEL),
    TBT_SPELL(4, ROCKET),
    TBT_SPELL(4, SHOOT),
    TBT_SPELL(4, SHOOT_GUN),
    TBT_SPELL(4, BR_ACID),
    TBT_SPELL(4, BR_ELEC),
    TBT_SPELL(4, BR_FIRE),
    TBT_SPELL(4, BR_COLD),
    TBT_SPELL(4, BR_POIS),
    TBT_SPELL(4, BR_NETH),
    TBT_SPELL(4, BR_LITE),
    TBT_SPELL(4, BR_DARK),
    TBT_SPELL(4, BR_CONF),
    TBT_SPELL(4, BR_SOUN),
    TBT_SPELL(4, BR_CHAO),
    TBT_SPELL(4, BR_DISE),
    TBT_SPELL(4, BR_STON),
    TBT_SPELL(4, BR_TIME),
    TBT_SPELL(4, BR_INER),
    TBT_SPELL(4, BR_GRAV),
    TBT_SPELL(4, BR_SHAR),
    TBT_SPELL(4, BR_PLAS),
    TBT_SPELL(4, BR_WALL),
    TBT_SPELL(4, BR_MANA),
    TBT_SPELL(4, BA_NUKE),
    TBT_SPELL(4, BR_NUKE),
    TBT_SPELL(4, BA_CHAO),
    TBT_SPELL(4, BR_DISI),
    TBT_SPELL(5, BA_ACID),
    TBT_SPELL(5, BA_ELEC),
    TBT_SPELL(5, BA_FIRE),
    TBT_SPELL(5, BA_COLD),
    TBT_SPELL(5, BA_POIS),
    TBT_SPELL(5, BA_NETH),
    TBT_SPELL(5, BA_WATE),
    TBT_SPELL(5, BA_MANA),
    TBT_SPELL(5, BA_DARK),
    TBT_SPELL(5, DRAIN_MANA),
    TBT_SPELL(5, MIND_BLAST),
    TBT_SPELL(5, BRAIN_SMASH),
    TBT_SPELL(5, CAUSE_1),
    TBT_SPELL(5, CAUSE_2),
    TBT_SPELL(5, CAUSE_3),
    TBT_SPELL(5, CAUSE_4),
    TBT_SPELL(5, BO_ACID),
    TBT_SPELL(5, BO_ELEC),
    TBT_SPELL(5, BO_FIRE),
    TBT_SPELL(5, BO_COLD),
    TBT_SPELL(5, BA_LITE),
    TBT_SPELL(5, BO_NETH),
    TBT_SPELL(5, BO_WATE),
    TBT_SPELL(5, BO_MANA),
    TBT_SPELL(5, BO_PLAS),
    TBT_SPELL(5, BO_ICEE),
    TBT_SPELL(5, MISSILE),
    TBT_SPELL(5, SCARE),
    TBT_SPELL(5, BLIND),
    TBT_SPELL(5, CONF),
    TBT_SPELL(5, SLOW),
    TBT_SPELL(5, HOLD),
    TBT_SPELL(6, HASTE),
    TBT_SPELL(6, HAND_DOOM),
    TBT_SPELL(6, HEAL),
    TBT_SPELL(6, INVULNER),
    TBT_SPELL(6, BLINK),
    TBT_SPELL(6, TPORT),
    TBT_SPELL(6, STOP_TIME),
    TBT_SPELL(6, SPECIAL),
    TBT_SPELL(6, TELE_TO),
    TBT_SPELL(6, TELE_AWAY),
    TBT_SPELL(6, TELE_LEVEL),
    TBT_SPELL(6, GODLY_SPEAR),
    TBT_SPELL(6, DARKNESS),
    TBT_SPELL(6, TRAPS),
    TBT_SPELL(6, FORGET),
    TBT_SPELL(6, RAISE_DEAD),
    TBT_SPELL(6, S_KIN),
    TBT_SPELL(6, S_XXX1),
    TBT_SPELL(6, S_MONSTER),
    TBT_SPELL(6, S_MONSTERS),
    TBT_SPELL(6, S_ANT),
    TBT_SPELL(6, S_SPIDER),
    TBT_SPELL(6, S_HOUND),
    TBT_SPELL(6, S_BEAST),
    TBT_SPELL(6, S_ANGEL),
    TBT_SPELL(6, S_DEMON),
    TBT_SPELL(6, S_UNDEAD),
    TBT_SPELL(6, S_DRAGON),
    TBT_SPELL(6, S_HI_UNDEAD),
    TBT_SPELL(6, S_HI_DRAGON),
    TBT_SPELL(6, S_TEMPLES),
    TBT_SPELL(6, S_UNIQUE),
    TBT_SPELL(A, FIRE_STORM),
    TBT_SPELL(A, AQUA_STORM),
    TBT_SPELL(A, EARTH_STORM),
    TBT_SPELL(A, WIND_STORM),
    TBT_SPELL(A, BR_PURE_FIRE),
    TBT_SPELL(A, BR_PURE_AQUA),
    TBT_SPELL(A, BR_PURE_EARTH),
    TBT_SPELL(A, BR_PURE_WIND),
    TBT_SPELL(A, PETRO_CLOUD),
    TBT_SPELL(A, SAND_STORM),
    TBT_SPELL(A, ERASE_ELEM),
    TBT_SPELL(A, CHANGE_ELEM),
    TBT_SPELL(A, SALAMANDER),
    TBT_SPELL(A, FENRER),
    TBT_SPELL(A, GNOME),
    TBT_SPELL(A, THUNDERBIRD),
    TBT_SPELL(A, IGNIS_FATUUS),
    TBT_SPELL(A, DARK_LORE),
    TBT_SPELL(A, STONE_GAZE),
    TBT_SPELL(A, HOLY_ORB),
    TBT_SPELL(A, DARK_FIRE),
    TBT_SPELL(A, S_ZENOBIAN),
    TBT_SPELL(A, S_HI_DEMON),
    TBT_SPELL(A, BA_DISI),
    TBT_SPELL(A, PURE_ELEM_BEAM),
    TBT_SPELL(A, BR_PURE_ELEM),
};

/*! 触る項目をまとめて保存する入れ物（`tbt_race_restore()` で戻す）。 */
typedef struct {
    u32b flags2;
    u32b flags4;
    u32b flags5;
    u32b flags6;
    u32b flagsa;
    byte freq_spell;
} tbt_race_save;

static const char tbt_tag[] = "[test-monster-act]";
static char tbt_why[160];

static u32b *tbt_word(monster_race *r_ptr, int slot)
{
    switch (slot) {
    case TBT_SLOT_4: return &r_ptr->flags4;
    case TBT_SLOT_5: return &r_ptr->flags5;
    case TBT_SLOT_6: return &r_ptr->flags6;
    default: return &r_ptr->flagsa;
    }
}

static int tbt_stricmp(const char *a, const char *b)
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

static const tbt_spell_def *tbt_find_spell(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(tbt_spells) / sizeof(tbt_spells[0]); i++) {
        if (tbt_stricmp(tbt_spells[i].name, name) == 0) return &tbt_spells[i];
    }
    return NULL;
}

static void tbt_race_save_take(tbt_race_save *s, const monster_race *r_ptr)
{
    s->flags2 = r_ptr->flags2;
    s->flags4 = r_ptr->flags4;
    s->flags5 = r_ptr->flags5;
    s->flags6 = r_ptr->flags6;
    s->flagsa = r_ptr->flagsa;
    s->freq_spell = r_ptr->freq_spell;
}

static int tbt_race_same(const tbt_race_save *s, const monster_race *r_ptr)
{
    return (s->flags2 == r_ptr->flags2) && (s->flags4 == r_ptr->flags4) && (s->flags5 == r_ptr->flags5) &&
           (s->flags6 == r_ptr->flags6) && (s->flagsa == r_ptr->flagsa) && (s->freq_spell == r_ptr->freq_spell);
}

/*! 元へ戻して、読み直して照合する。照合できたら 1。 */
static int tbt_race_restore(const tbt_race_save *s, monster_race *r_ptr)
{
    r_ptr->flags2 = s->flags2;
    r_ptr->flags4 = s->flags4;
    r_ptr->flags5 = s->flags5;
    r_ptr->flags6 = s->flags6;
    r_ptr->flagsa = s->flagsa;
    r_ptr->freq_spell = s->freq_spell;
    return tbt_race_same(s, r_ptr);
}

static int tbt_fail(const char *fmt, const char *arg)
{
    if (arg != NULL) {
        snprintf(tbt_why, sizeof(tbt_why), fmt, arg);
    } else {
        snprintf(tbt_why, sizeof(tbt_why), "%s", fmt);
    }
    return 0;
}

/*! 見えていて敵対している、いちばん近い敵（同じ距離なら番号が小さいほう）。無ければ 0。 */
static int tbt_nearest_monster(void)
{
    int i;
    int best = 0;

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = tb_monster_slot(i);

        if (!m_ptr->r_idx || !m_ptr->ml || !is_hostile(m_ptr)) continue;
        if (!best || (m_ptr->cdis < m_list[best].cdis)) best = i;
    }
    return best;
}

/*! 対象の指定（省略・`nearest`・番号）を番号へ。解けなければ 0 を返し `tbt_why` に理由を入れる。 */
static int tbt_resolve_target(const char *tok)
{
    int idx;
    const char *p;

    if ((tok == NULL) || (tbt_stricmp(tok, "nearest") == 0)) {
        idx = tbt_nearest_monster();
        if (!idx) (void)tbt_fail("no-visible-hostile-monster", NULL);
        return idx;
    }
    for (p = tok; *p; p++) {
        if ((*p < '0') || (*p > '9')) {
            (void)tbt_fail("bad-target %s", tok);
            return 0;
        }
    }
    idx = atoi(tok);
    if ((idx < 1) || (idx >= m_max) || !m_list[idx].r_idx) {
        (void)tbt_fail("no-such-monster %s", tok);
        return 0;
    }
    return idx;
}

/*! 指定の敵に、指定の魔法を撃たせる。撃たせたら 1。 */
static int tbt_cast(int m_idx, const tbt_spell_def *sp)
{
    monster_type *m_ptr = tb_monster_slot(m_idx);
    const int r_idx = m_ptr->r_idx;
    monster_race *r_ptr = &r_info[r_idx];
    tbt_race_save save;
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
    if (!spell_no) return tbt_fail("bad-spell-table", NULL);

    if (MON_CONFUSED(m_ptr)) return tbt_fail("monster-confused", NULL);
    if (!is_hostile(m_ptr)) return tbt_fail("monster-not-hostile", NULL);
    if (p_ptr->use_decoy) return tbt_fail("decoy-in-use", NULL);
    if ((m_ptr->cdis > MAX_RANGE) || (m_ptr->ddis > MAX_RANGE)) return tbt_fail("out-of-range", NULL);
    if (!projectable(m_ptr->fy, m_ptr->fx, py, px)) return tbt_fail("not-projectable", NULL);
    if ((sp->slot == TBT_SLOT_A) && (sp->bit == RFA_BR_PURE_ELEM) &&
        ((get_cur_melem(m_ptr) < ELEM_FIRE) || (get_cur_melem(m_ptr) > ELEM_WIND))) {
        return tbt_fail("monster-element-not-pure", NULL);
    }
    if (!spell_is_inate((u16b)spell_no)) {
        /* 魔法は、これらの状態だと「失敗した」になる（STUPID でも 0 に戻らない） */
        if (MON_SILENT(m_ptr) || m_ptr->silent_song) return tbt_fail("magic-blocked:monster-silent", NULL);
        if (is_anti_magic_grid(m_idx, m_ptr->fy, m_ptr->fx)) return tbt_fail("magic-blocked:anti-magic-grid", NULL);
        if (MON_STUNNED(m_ptr)) return tbt_fail("magic-may-fail:monster-stunned", NULL);
    }

    /* ---- 一時的に整える（ここから `tbt_race_restore()` までの間に return しない） ---- */
    tbt_race_save_take(&save, r_ptr);
    r_ptr->flags4 = 0L;
    r_ptr->flags5 = 0L;
    r_ptr->flags6 = 0L;
    r_ptr->flagsa = 0L;
    *tbt_word(r_ptr, sp->slot) = sp->bit;
    r_ptr->freq_spell = 100;
    r_ptr->flags2 |= RF2_STUPID;
    m_ptr->mflag &= ~MFLAG_NICE;
    inkey_flag = FALSE;

    cast = make_attack_spell(m_idx) ? 1 : 0;

    restored = tbt_race_restore(&save, r_ptr);
    if (m_ptr->r_idx == r_idx) m_ptr->mflag |= nice0;
    notice_stuff();
    handle_stuff();
    inkey_flag = inkey_flag0;
    inkey_base = inkey_base0;
    inkey_xtra = inkey_xtra0;
    inkey_scan = inkey_scan0;
    Term_fresh();
    /* ---- 戻した ---- */

    /* 呼ぶ前と戻したあとの値を並べて出す（検査が外から見比べる）。形は flags2/flags4/flags5/flags6/flagsa/freq_spell の順 */
    fprintf(stderr, "%s info restore race=%d restored=%d before=%08lx/%08lx/%08lx/%08lx/%08lx/%d after=%08lx/%08lx/%08lx/%08lx/%08lx/%d\n",
            tbt_tag, r_idx, restored, (unsigned long)save.flags2, (unsigned long)save.flags4, (unsigned long)save.flags5,
            (unsigned long)save.flags6, (unsigned long)save.flagsa, (int)save.freq_spell, (unsigned long)r_ptr->flags2,
            (unsigned long)r_ptr->flags4, (unsigned long)r_ptr->flags5, (unsigned long)r_ptr->flags6, (unsigned long)r_ptr->flagsa,
            (int)r_ptr->freq_spell);
    if (!restored) return tbt_fail("race-flags-not-restored", NULL);
    if (!cast) return tbt_fail("make_attack_spell-returned-false", NULL);
    return 1;
}

/*! 指定の敵に、プレイヤーへ直接攻撃させる。攻撃させたら 1。 */
static int tbt_melee(int m_idx)
{
    monster_type *m_ptr = tb_monster_slot(m_idx);
    monster_race *r_ptr = &r_info[m_ptr->r_idx];
    const bool inkey_flag0 = inkey_flag;
    const bool inkey_base0 = inkey_base;
    const bool inkey_xtra0 = inkey_xtra;
    const bool inkey_scan0 = inkey_scan;
    int hit;

    if (distance(m_ptr->fy, m_ptr->fx, py, px) > 1) return tbt_fail("not-adjacent", NULL);
    if (!is_hostile(m_ptr)) return tbt_fail("monster-not-hostile", NULL);
    if (r_ptr->flags1 & RF1_NEVER_BLOW) return tbt_fail("never-blow", NULL);

    inkey_flag = FALSE;
    hit = make_attack_normal(m_idx) ? 1 : 0;
    notice_stuff();
    handle_stuff();
    inkey_flag = inkey_flag0;
    inkey_base = inkey_base0;
    inkey_xtra = inkey_xtra0;
    inkey_scan = inkey_scan0;
    Term_fresh();

    if (!hit) return tbt_fail("make_attack_normal-returned-false", NULL);
    return 1;
}

/*! 符号つきの整数の字を読む。読めたら 1（地図より十分大きい値は桁あふれ除けで読まない）。 */
static int tbt_parse_int(const char *tok, int *out)
{
    const char *p = tok;
    int sign = 1;
    long v = 0;

    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if (!*p) return 0;
    for (; *p; p++) {
        if ((*p < '0') || (*p > '9')) return 0;
        v = v * 10 + (*p - '0');
        if (v > 100000L) return 0;
    }
    *out = (int)(sign * v);
    return 1;
}

/*! 指定の敵を、プレイヤーの位置から (dx, dy) ずれたマスへ動かす。動かしたら 1。
 *
 * 原作が敵を別のマスへ動かすときの手順（`spells3.c` の `teleport_away()`）と同じ:
 * 新しいマスの `m_idx`・古いマスの `m_idx`・`fy`/`fx`・`reset_target()`・`update_mon(m_idx, TRUE)`・
 * 古いマスと新しいマスの `lite_spot()`・光る敵なら `PU_MON_LITE`。 */
static int tbt_place(int m_idx, int dx, int dy)
{
    monster_type *m_ptr = tb_monster_slot(m_idx);
    const int oy = m_ptr->fy;
    const int ox = m_ptr->fx;
    const int ny = py + dy;
    const int nx = px + dx;
    const bool inkey_flag0 = inkey_flag;
    const bool inkey_base0 = inkey_base;
    const bool inkey_xtra0 = inkey_xtra;
    const bool inkey_scan0 = inkey_scan;
    cave_type *c_ptr;

    if (p_ptr->riding == m_idx) return tbt_fail("monster-is-ridden", NULL);
    if (!in_bounds(ny, nx)) return tbt_fail("out-of-map", NULL);
    if ((ny == oy) && (nx == ox)) return 1; /* もうそのマスにいる */
    if ((ny == py) && (nx == px)) return tbt_fail("player-grid", NULL);
    c_ptr = &cave[ny][nx];
    if (!cave_floor_bold(ny, nx) && (c_ptr->feat != FEAT_TREES)) return tbt_fail("grid-is-wall", NULL);
    if (!monster_can_enter_terrain(c_ptr->feat, m_ptr)) return tbt_fail("terrain-not-allowed", NULL);
    if (c_ptr->m_idx) return tbt_fail("occupied-by-monster", NULL);

    c_ptr->m_idx = (s16b)m_idx;
    cave[oy][ox].m_idx = 0;
    m_ptr->fy = (byte)ny;
    m_ptr->fx = (byte)nx;
    reset_target(m_ptr);
    inkey_flag = FALSE;
    update_mon(m_idx, TRUE);
    lite_spot(oy, ox);
    lite_spot(ny, nx);
    if (r_info[m_ptr->r_idx].flags7 & (RF7_HAS_LITE_1 | RF7_SELF_LITE_1 | RF7_HAS_LITE_2 | RF7_SELF_LITE_2)) {
        p_ptr->update |= (PU_MON_LITE);
    }
    notice_stuff();
    handle_stuff();
    inkey_flag = inkey_flag0;
    inkey_base = inkey_base0;
    inkey_xtra = inkey_xtra0;
    inkey_scan = inkey_scan0;
    Term_fresh();
    return 1;
}

/* ---- hold：止めている敵の記録 ---- */

#define TBT_HOLD_ENERGY 30000 /* 止めている敵の energy_need に置く値（s16b の上限 32767 の手前。原作が足す量の余裕を残す） */
#define TBT_HOLD_MAX 2048

typedef struct {
    s16b m_idx;
    s16b r_idx;
    s16b floor_id;
    s16b dun_level;
    s16b saved;  /* 止める前の energy_need */
    byte by_all; /* `hold all on` で止めたもの（敵対でなくなったら外す） */
} tbt_hold_rec;

static tbt_hold_rec tbt_holds[TBT_HOLD_MAX];
static int tbt_hold_n = 0;
static int tbt_hold_all = 0;
static int tbt_hold_full_told = 0;

static int tbt_hold_find(int m_idx)
{
    int i;

    for (i = 0; i < tbt_hold_n; i++) {
        if (tbt_holds[i].m_idx == m_idx) return i;
    }
    return -1;
}

/*! 記録の敵が、記録したときと同じ敵か（同じ階・同じ番号・同じ種族）。 */
static int tbt_hold_valid(const tbt_hold_rec *h)
{
    return (h->m_idx >= 1) && (h->m_idx < m_max) && (m_list[h->m_idx].r_idx == h->r_idx) &&
           (h->floor_id == p_ptr->floor_id) && (h->dun_level == dun_level);
}

/*! 記録 i を外す。`restore` が真で敵が生きていれば、止める前の energy_need へ戻す。 */
static void tbt_hold_remove_at(int i, int restore)
{
    if (restore && tbt_hold_valid(&tbt_holds[i])) m_list[tbt_holds[i].m_idx].energy_need = tbt_holds[i].saved;
    tbt_holds[i] = tbt_holds[--tbt_hold_n];
}

/*! 記録を足す（まだ記録に無い敵）。足したら 1。 */
static int tbt_hold_add(int m_idx, int by_all)
{
    tbt_hold_rec *h;

    if (tbt_hold_n >= TBT_HOLD_MAX) return 0;
    h = &tbt_holds[tbt_hold_n++];
    h->m_idx = (s16b)m_idx;
    h->r_idx = m_list[m_idx].r_idx;
    h->floor_id = p_ptr->floor_id;
    h->dun_level = dun_level;
    h->saved = m_list[m_idx].energy_need;
    h->by_all = (byte)by_all;
    return 1;
}

/*!
 * 止めている敵の energy_need を置き直す。命令待ちのたびと、命令の各行のあとに呼ぶ。
 * 外れた記録（死んだ・階を移った・別の敵に入れ替わった）はここで外す。`hold all` の間は、敵対している敵を全部記録に足す。
 */
static void tbt_hold_refresh(void)
{
    int i;

    if (!tbt_hold_n && !tbt_hold_all) return;
    for (i = tbt_hold_n - 1; i >= 0; i--) {
        tbt_hold_rec *h = &tbt_holds[i];

        if (!tbt_hold_valid(h)) {
            fprintf(stderr, "%s info hold-dropped m_idx=%d r_idx=%d\n", tbt_tag, (int)h->m_idx, (int)h->r_idx);
            fflush(stderr);
            tbt_hold_remove_at(i, 0);
        } else if (h->by_all && !is_hostile(&m_list[h->m_idx])) {
            tbt_hold_remove_at(i, 1);
        } else {
            m_list[h->m_idx].energy_need = TBT_HOLD_ENERGY;
        }
    }
    if (tbt_hold_all) {
        for (i = 1; i < m_max; i++) {
            monster_type *m_ptr = tb_monster_slot(i);

            if (!m_ptr->r_idx || !is_hostile(m_ptr) || (tbt_hold_find(i) >= 0)) continue;
            if (tbt_hold_add(i, 1)) {
                m_ptr->energy_need = TBT_HOLD_ENERGY;
            } else if (!tbt_hold_full_told) {
                tbt_hold_full_told = 1;
                fprintf(stderr, "%s info hold-table-full\n", tbt_tag);
                fflush(stderr);
            }
        }
    }
}

/*! `hold <nearest|m_idx|all> on|off`。 */
static int tbt_hold_command(char **tok, int ntok)
{
    int on;
    int idx;
    int i;

    if (ntok < 3) return tbt_fail("hold-needs-target-and-on-off", NULL);
    if (tbt_stricmp(tok[2], "on") == 0) {
        on = 1;
    } else if (tbt_stricmp(tok[2], "off") == 0) {
        on = 0;
    } else {
        return tbt_fail("bad-hold-state %s", tok[2]);
    }

    if (tbt_stricmp(tok[1], "all") == 0) {
        if (on) {
            tbt_hold_all = 1;
            tbt_hold_full_told = 0;
            tbt_hold_refresh();
        } else {
            tbt_hold_all = 0;
            while (tbt_hold_n > 0) tbt_hold_remove_at(tbt_hold_n - 1, 1);
        }
        return 1;
    }
    if ((idx = tbt_resolve_target(tok[1])) == 0) return 0;
    i = tbt_hold_find(idx);
    if (on) {
        if (i >= 0) {
            tbt_holds[i].by_all = 0; /* 個別の指定が優先（敵対でなくなっても止め続ける） */
        } else if (!tbt_hold_add(idx, 0)) {
            return tbt_fail("hold-table-full", NULL);
        }
        m_list[idx].energy_need = TBT_HOLD_ENERGY;
    } else if (i >= 0) {
        tbt_hold_remove_at(i, 1);
    } else {
        fprintf(stderr, "%s info not-held m_idx=%d\n", tbt_tag, idx);
    }
    return 1;
}

/*! `list`：敵の一覧（読み取りだけ）。 */
static void tbt_list_monsters(void)
{
    int i;

    fprintf(stderr, "%s info time turn=%ld player_x=%d player_y=%d\n", tbt_tag, (long)turn, px, py);
    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = tb_monster_slot(i);
        char name[MAX_NLEN];
        tb_rng_snapshot snap;

        if (!m_ptr->r_idx) continue;
        /* 名前は行の最後（空白を含むので、数の欄の読み取りを邪魔しない）。幻覚中の名前選びが乱数を引くので、状態を戻す。 */
        tb_rng_save(&snap);
        monster_desc(name, m_ptr, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE);
        tb_rng_restore(&snap);
        fprintf(stderr, "%s info monster m_idx=%d r_idx=%d dx=%d dy=%d hostile=%d visible=%d hold=%d energy=%d name=%s\n", tbt_tag, i,
                (int)m_ptr->r_idx, (int)m_ptr->fx - px, (int)m_ptr->fy - py, is_hostile(m_ptr) ? 1 : 0, m_ptr->ml ? 1 : 0,
                (tbt_hold_find(i) >= 0) ? 1 : 0, (int)m_ptr->energy_need, name);
    }
}

/*! 命令 1 行を実行する。結果を標準エラーへ出し、うまくいったら 1。空行と `#` の行は呼ばれない。 */
static int tbt_run_line(char *line)
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

    tbt_why[0] = '\0';
    if (!ntok) {
        (void)tbt_fail("empty-command", NULL);
    } else if (!character_dungeon || !p_ptr->playing || p_ptr->is_dead || p_ptr->leaving) {
        (void)tbt_fail("player-not-in-play", NULL);
    } else if (tbt_stricmp(tok[0], "cast") == 0) {
        const tbt_spell_def *sp;

        if (ntok < 2) {
            (void)tbt_fail("cast-needs-spell-name", NULL);
        } else if ((sp = tbt_find_spell(tok[1])) == NULL) {
            (void)tbt_fail("unknown-spell %s", tok[1]);
        } else if ((idx = tbt_resolve_target((ntok >= 3) ? tok[2] : NULL)) != 0) {
            ok = tbt_cast(idx, sp);
        }
    } else if (tbt_stricmp(tok[0], "melee") == 0) {
        if ((idx = tbt_resolve_target((ntok >= 2) ? tok[1] : NULL)) != 0) {
            ok = tbt_melee(idx);
        }
    } else if (tbt_stricmp(tok[0], "place") == 0) {
        int dx = 0;
        int dy = 0;

        if (ntok < 4) {
            (void)tbt_fail("place-needs-target-dx-dy", NULL);
        } else if (!tbt_parse_int(tok[2], &dx)) {
            (void)tbt_fail("bad-number %s", tok[2]);
        } else if (!tbt_parse_int(tok[3], &dy)) {
            (void)tbt_fail("bad-number %s", tok[3]);
        } else if ((idx = tbt_resolve_target(tok[1])) != 0) {
            ok = tbt_place(idx, dx, dy);
        }
    } else if (tbt_stricmp(tok[0], "hold") == 0) {
        ok = tbt_hold_command(tok, ntok);
    } else if (tbt_stricmp(tok[0], "list") == 0) {
        tbt_list_monsters();
        ok = 1;
    } else {
        (void)tbt_fail("unknown-command %s", tok[0]);
    }
    tbt_hold_refresh(); /* 命令の中で原作が足した energy_need を、止めている敵から取り除く */

    if (ok) {
        fprintf(stderr, "%s ok %s\n", tbt_tag, full);
    } else {
        fprintf(stderr, "%s fail %s\n", tbt_tag, tbt_why);
    }
    fflush(stderr);
    return ok;
}

/*!
 * @brief 命令ファイルがあれば読んで実行する（`host_next_key()` から、命令待ちのときだけ呼ばれる）。
 * @return 待っているコアを起こすキー（死亡・階の移動のとき ESC）。要らなければ 0
 */
int tb_test_monster_act_poll(void)
{
    static int checked = 0;
    static const char *path = NULL;
    static int busy = 0;
    static int broken = 0;
    static char text[16384];
    char moved[1100];
    size_t n;
    FILE *fp;
    char *line;
    char *next;
    int failed = 0;

    if (!checked) {
        path = getenv("HD2D_TEST_MONSTER_ACT");
        if ((path != NULL) && (path[0] == '\0')) path = NULL;
        checked = 1;
    }
    if ((path == NULL) || busy || broken) return 0;
    if (!tb_awaiting_command() || !character_dungeon || !p_ptr->playing) return 0;

    tbt_hold_refresh();

    fp = fopen(path, "rb");
    if (fp == NULL) return 0;
    n = fread(text, 1, sizeof(text) - 1, fp);
    fclose(fp);
    if (n == 0) return 0; /* 空（書きかけ）は読まない */
    text[n] = '\0';

    /* 先に消す。消せなければ名前を変える。どちらもできなければ、同じ命令を繰り返さないよう実行しない */
    if (remove(path) != 0) {
        snprintf(moved, sizeof(moved), "%s.done", path);
        (void)remove(moved);
        if (rename(path, moved) != 0) {
            fprintf(stderr, "%s fail cannot-consume-command-file\n", tbt_tag);
            fflush(stderr);
            broken = 1;
            return 0;
        }
    }

    busy = 1;
    for (line = text; line != NULL && *line; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        while ((*line == ' ') || (*line == '\t')) line++;
        n = strlen(line);
        while ((n > 0) && ((line[n - 1] == '\r') || (line[n - 1] == ' ') || (line[n - 1] == '\t'))) line[--n] = '\0';
        if ((n == 0) || (line[0] == '#')) continue;
        if (failed) {
            fprintf(stderr, "%s skip %s\n", tbt_tag, line);
            fflush(stderr);
        } else if (!tbt_run_line(line)) {
            failed = 1;
        }
    }
    busy = 0;

    return (p_ptr->is_dead || p_ptr->leaving) ? ESCAPE : 0;
}
