#include "rl_inventory_api.h"
#include "compat/curses.h"
#include "rogue.h"
#include "rl_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RL_GENERATION
#error RL_GENERATION must be 36 or 54
#endif

static void copy_desc(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    if (src == NULL) src = "";
    snprintf(dst, cap, "%s", src);
    dst[cap - 1] = '\0';
}

const char *rl_monster_name(int letter)
{
    /*
     * 3.6 も 5.4 も、記号 A-Z がそのまま monsters[] の添字である
     * （3.6 は init.c、5.4 は extern.c の表）。名前は原作のまま返し、
     * 訳はサブパネルを組むところで当てる。
     */
    int at;
    if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
    if (letter < 'A' || letter > 'Z') return NULL;
    at = letter - 'A';
    return monsters[at].m_name;
}

/*
 * 3.6 の cansee()（chase.c）は「同じ部屋で明るい、または DISTANCE < 3」、5.4 の cansee() は
 * 「dist < LAMPDIST（3）なら（角越しの斜めを除いて）見える、でなければ同じ部屋で明るい」。
 * どちらの「近い」も二乗距離が 3 未満＝自分の周り 3x3 なので、光が届くマスは
 * 「3x3 の中で cansee() が真」で両方の版を同じ式で書ける（盲目は cansee() が偽にする）。
 */
#if RL_GENERATION == 54
/*
 * 5.4 の cansee() は、近く（LAMPDIST 未満）でないマスで roomin() を呼ぶ。roomin() は部屋にも通路にも
 * 入らない座標だと msg("in some bizarre place") を出し、msg() → endmsg() → refresh() → 画面の描画
 * （host_present → capture_frame → rl_can_see）へ戻って同じ呼び出しを繰り返し、スタックがあふれて
 * コアが落ちる（0xC00000FD。F-rogue54-01）。マップでなく文字の一覧が出ている画面（V のスコア一覧・
 * ヘルプ）の字を、capture_frame がマップのマスと読んで問い合わせるのが引き金。
 * roomin() と同じ判定を、メッセージを出さずに先に行い、どこにも入らないマスは「見えない」とする。
 * 3.6 の roomin() は何も出さないので、この手当ては 5.4 だけ。
 */
static int rl_place_is_known(int y, int x)
{
    const struct room *rp;
    if (flat(y, x) & F_PASS) return 1;
    for (rp = rooms; rp < &rooms[MAXROOMS]; rp++) {
        if (x <= rp->r_pos.x + rp->r_max.x && rp->r_pos.x <= x
            && y <= rp->r_pos.y + rp->r_max.y && rp->r_pos.y <= y)
            return 1;
    }
    return 0;
}
#endif

int rl_can_see(int y, int x)
{
#if RL_GENERATION == 54
    if ((player.t_flags & ISBLIND) == 0 && dist(y, x, hero.y, hero.x) >= LAMPDIST
        && !rl_place_is_known(y, x))
        return 0;
#endif
    return cansee(y, x) ? 1 : 0;
}

/* 盲目か（攻撃の効果の観測が、見えない間は絵を出さないために使う。読むだけ）。 */
int rl_player_blind(void)
{
    return (player.t_flags & ISBLIND) != 0 ? 1 : 0;
}

int rl_light_reaches(int y, int x)
{
    const int dy = y - hero.y;
    const int dx = x - hero.x;
    return (dy * dy + dx * dx < 3 && cansee(y, x)) ? 1 : 0;
}

#if RL_GENERATION == 36
static int object_identified(const struct object *obj)
{
    switch (obj->o_type) {
    case POTION: return p_know[obj->o_which] != 0;
    case SCROLL: return s_know[obj->o_which] != 0;
    case RING: return r_know[obj->o_which] != 0;
    case STICK: return ws_know[obj->o_which] != 0;
    default: return (obj->o_flags & ISKNOW) != 0;
    }
}

static int object_callable(const struct object *obj)
{
    if (obj->o_type != POTION && obj->o_type != SCROLL && obj->o_type != RING && obj->o_type != STICK) return 0;
    return !object_identified(obj);
}

int rl_inventory_count(void)
{
    int count = 0;
    struct linked_list *item;
    for (item = pack; item != NULL; item = next(item)) ++count;
    return count;
}

int rl_inventory_item(int index, rl_inventory_item_state *out)
{
    struct linked_list *item;
    int at = 0;
    if (out == NULL || index < 0) return 0;
    for (item = pack; item != NULL; item = next(item), ++at) {
        struct object *obj;
        if (at != index) continue;
        obj = (struct object *)ldata(item);
        memset(out, 0, sizeof(*out));
        out->key = 'a' + at;
        out->type = obj->o_type;
        out->quantity = obj->o_count > 0 ? obj->o_count : 1;
        if (obj == cur_weapon) out->equipped_flags |= 1u;
        if (obj == cur_armor) out->equipped_flags |= 2u;
        if (obj == cur_ring[LEFT]) out->equipped_flags |= 4u;
        if (obj == cur_ring[RIGHT]) out->equipped_flags |= 8u;
        out->cursed = (obj->o_flags & ISCURSED) != 0;
        out->identified = object_identified(obj);
        out->callable = object_callable(obj);
        copy_desc(out->description, sizeof(out->description), inv_name(obj, FALSE));
        return 1;
    }
    return 0;
}

#elif RL_GENERATION == 54
static int object_identified(const THING *obj)
{
    switch (obj->o_type) {
    case POTION: return pot_info[obj->o_which].oi_know != 0;
    case SCROLL: return scr_info[obj->o_which].oi_know != 0;
    case RING: return ring_info[obj->o_which].oi_know != 0;
    case STICK: return ws_info[obj->o_which].oi_know != 0;
    default: return (obj->o_flags & ISKNOW) != 0;
    }
}

static int object_callable(const THING *obj)
{
    if (obj->o_type == FOOD || obj->o_type == AMULET) return 0;
    if (obj->o_type == POTION || obj->o_type == SCROLL || obj->o_type == RING || obj->o_type == STICK)
        return !object_identified(obj);
    return 1;
}

int rl_inventory_count(void)
{
    int count = 0;
    THING *item;
    for (item = pack; item != NULL; item = next(item)) ++count;
    return count;
}

int rl_inventory_item(int index, rl_inventory_item_state *out)
{
    THING *obj;
    int at = 0;
    if (out == NULL || index < 0) return 0;
    for (obj = pack; obj != NULL; obj = next(obj), ++at) {
        if (at != index) continue;
        memset(out, 0, sizeof(*out));
        out->key = obj->o_packch ? obj->o_packch : ('a' + at);
        out->type = obj->o_type;
        out->quantity = obj->o_count > 0 ? obj->o_count : 1;
        if (obj == cur_weapon) out->equipped_flags |= 1u;
        if (obj == cur_armor) out->equipped_flags |= 2u;
        if (obj == cur_ring[LEFT]) out->equipped_flags |= 4u;
        if (obj == cur_ring[RIGHT]) out->equipped_flags |= 8u;
        out->cursed = (obj->o_flags & ISCURSED) != 0;
        out->identified = object_identified(obj);
        out->callable = object_callable(obj);
        copy_desc(out->description, sizeof(out->description), inv_name(obj, FALSE));
        return 1;
    }
    return 0;
}

#else
#error Unsupported RL_GENERATION
#endif

/*
 * 状態のアイコン（`GameFrame::status_effects`。・CORE_HUD_ROLLOUT.md §2.3。決めたこと 2026-10-11）。
 *
 * 範囲は原作の最下段（空腹）に、メッセージで知らされる状態（盲目・混乱・麻痺・加速・透明視・混乱させる手・熊の罠や食人植物に
 * 捕まっている。5.4 は浮遊・幻覚・怪物感知も）と、鑑定済みの指輪の効果を足したもの。
 * 指輪は「種類が分かっている」（3.6 は r_know、5.4 は ring_info の oi_know）ものだけ。未鑑定の指輪の効果は出さない
 * （原作より情報を増やさない）。効果量は原作が数字を見せるとき（ISKNOW）だけ。名前は画面の言語に合わせる（日本語・英語。RL_LBL）。
 * どれも読むだけでゲームの状態は変えない。
 */
/* 表示言語（修-46）。名前は画面の言語に合わせる（既定は日本語）。 */
#define RL_LBL(ja, en) (rl_i18n_is_english() ? (en) : (ja))
#define RL_ST_GOOD 0
#define RL_ST_BAD 1
#define RL_ST_PERM 4
#define RL_ST_CURSE 5

#if RL_GENERATION == 36
typedef struct object RL_RING_OBJ;
#define RL_RING_KNOWN(r) (r_know[(r)->o_which] != 0)
#define RL_RING_BONUS(r) ((r)->o_ac)
#define RL_RING_MAX MAXRINGS
#else
typedef THING RL_RING_OBJ;
#define RL_RING_KNOWN(r) (ring_info[(r)->o_which].oi_know != 0)
#define RL_RING_BONUS(r) ((r)->o_arm)
#define RL_RING_MAX MAXRINGS
#endif
#define RL_PLAYER_ON(flag) ((player.t_flags & (flag)) != 0)

static void rl_status_add(rl_status_effect *out, int *count, int cap, const char *key, const char *label,
    const char *amount, int kind)
{
    int i;
    for (i = 0; i < *count; ++i) {
        if (strcmp(out[i].key, key) == 0 && out[i].kind == kind && strcmp(out[i].label, label) == 0) return; /* 重なりは 1 つ */
    }
    if (*count >= cap) return;
    snprintf(out[*count].key, sizeof(out[*count].key), "%s", key);
    snprintf(out[*count].label, sizeof(out[*count].label), "%s", label);
    snprintf(out[*count].amount, sizeof(out[*count].amount), "%s", amount != NULL ? amount : "");
    out[*count].kind = kind;
    ++*count;
}

/* 数を持つ指輪（防御・腕力・命中・ダメージ）。負なら呪い（効果が逆）。 */
static void rl_ring_number(rl_status_effect *out, int *count, int cap, const RL_RING_OBJ *ring,
    const char *good_key, const char *good_label, const char *bad_label)
{
    char amount[12];
    const int bonus = (int)RL_RING_BONUS(ring);
    amount[0] = 0;
    if ((ring->o_flags & ISKNOW) != 0) snprintf(amount, sizeof(amount), "%+d", bonus);
    if (bonus < 0) {
        rl_status_add(out, count, cap, strcmp(good_key, "magicdef") == 0 ? "low_ac" : "low_melee", bad_label, amount, RL_ST_CURSE);
    } else {
        rl_status_add(out, count, cap, good_key, good_label, amount, RL_ST_PERM);
    }
}

static void rl_ring_status(const RL_RING_OBJ *ring, rl_status_effect *out, int *count, int cap)
{
    if (ring == NULL || ring->o_which < 0 || ring->o_which >= RL_RING_MAX) return;
    if (!RL_RING_KNOWN(ring)) return; /* 未鑑定は出さない */
    switch (ring->o_which) {
    case R_PROTECT:
        rl_ring_number(out, count, cap, ring, "magicdef", RL_LBL("防御", "Protection"), RL_LBL("防御低下", "Protection down"));
        break;
    case R_ADDSTR:
        rl_ring_number(out, count, cap, ring, "might", RL_LBL("腕力", "Strength"), RL_LBL("腕力低下", "Strength down"));
        break;
    case R_SUSTSTR:
        rl_status_add(out, count, cap, "sust_str", RL_LBL("腕力維持", "Sustain strength"), NULL, RL_ST_PERM);
        break;
    case R_SEARCH:
        rl_status_add(out, count, cap, "search", RL_LBL("探索", "Searching"), NULL, RL_ST_PERM);
        break;
    case R_SEEINVIS:
        rl_status_add(out, count, cap, "senseunseen", RL_LBL("透明視", "See invisible"), NULL, RL_ST_PERM);
        break;
    case R_AGGR:
        rl_status_add(out, count, cap, "aggravate", RL_LBL("反感", "Aggravate"), NULL, RL_ST_CURSE);
        break;
    case R_ADDHIT:
        rl_ring_number(out, count, cap, ring, "blows", RL_LBL("命中", "Accuracy"), RL_LBL("命中低下", "Accuracy down"));
        break;
    case R_ADDDAM:
        rl_ring_number(out, count, cap, ring, "impact", RL_LBL("ダメージ", "Damage"), RL_LBL("ダメージ低下", "Damage down"));
        break;
    case R_REGEN:
        rl_status_add(out, count, cap, "regeneration", RL_LBL("再生", "Regeneration"), NULL, RL_ST_PERM);
        break;
    case R_DIGEST:
        rl_status_add(out, count, cap, "slow_digest", RL_LBL("遅消化", "Slow digestion"), NULL, RL_ST_PERM);
        break;
    case R_TELEPORT:
        rl_status_add(out, count, cap, "cursed", RL_LBL("テレポート", "Teleportation"), NULL, RL_ST_CURSE); /* 勝手に飛ばされる呪い */
        break;
    case R_STEALTH:
        rl_status_add(out, count, cap, "stealth", RL_LBL("隠密", "Stealth"), NULL, RL_ST_PERM);
        break;
#if RL_GENERATION == 54
    case R_SUSTARM:
        rl_status_add(out, count, cap, "stoneskin", RL_LBL("鎧維持", "Sustain armor"), NULL, RL_ST_PERM);
        break;
#endif
    default: /* R_NOP（装飾）は効果が無い */
        break;
    }
}

int rl_status_effects(rl_status_effect *out, int cap)
{
    int count = 0;
    if (out == NULL || cap <= 0) return 0;
    /* 空腹。原作の最下段（status()）と同じ 3 段階。訳は rl_i18n.c の「Hungry」「Weak」「Fainting」と同じ。 */
    if (hungry_state == 1) rl_status_add(out, &count, cap, "hungry", RL_LBL("空腹", "Hungry"), NULL, RL_ST_BAD);
    else if (hungry_state == 2) rl_status_add(out, &count, cap, "hungry", RL_LBL("飢餓", "Weak"), NULL, RL_ST_BAD);
    else if (hungry_state >= 3) rl_status_add(out, &count, cap, "hungry", RL_LBL("瀕死", "Fainting"), NULL, RL_ST_BAD);
    if (RL_PLAYER_ON(ISBLIND)) rl_status_add(out, &count, cap, "blind", RL_LBL("盲目", "Blind"), NULL, RL_ST_BAD);
    if (RL_PLAYER_ON(ISHUH)) rl_status_add(out, &count, cap, "confused", RL_LBL("混乱", "Confused"), NULL, RL_ST_BAD);
#if RL_GENERATION == 54
    if (RL_PLAYER_ON(ISHALU)) rl_status_add(out, &count, cap, "hallucination", RL_LBL("幻覚", "Hallucinating"), NULL, RL_ST_BAD);
#endif
    if (no_command > 0) rl_status_add(out, &count, cap, "paralyzed", RL_LBL("麻痺", "Paralyzed"), NULL, RL_ST_BAD); /* 眠り・凍り・気絶 */
    if (no_move > 0 || RL_PLAYER_ON(ISHELD)) rl_status_add(out, &count, cap, "rl_held", RL_LBL("拘束", "Held"), NULL, RL_ST_BAD);
#if RL_GENERATION == 54
    if (RL_PLAYER_ON(ISLEVIT)) rl_status_add(out, &count, cap, "levitate", RL_LBL("浮遊", "Levitating"), NULL, RL_ST_GOOD);
#endif
    if (RL_PLAYER_ON(ISHASTE)) rl_status_add(out, &count, cap, "haste", RL_LBL("加速", "Hasted"), NULL, RL_ST_GOOD);
    if (RL_PLAYER_ON(CANSEE)) rl_status_add(out, &count, cap, "senseunseen", RL_LBL("透明視", "See invisible"), NULL, RL_ST_GOOD);
#if RL_GENERATION == 54
    if (RL_PLAYER_ON(SEEMONST)) rl_status_add(out, &count, cap, "telepathy", RL_LBL("怪物感知", "Sensing monsters"), NULL, RL_ST_GOOD);
#endif
    if (RL_PLAYER_ON(CANHUH)) rl_status_add(out, &count, cap, "attk_conf", RL_LBL("混乱打撃", "Confusing touch"), NULL, RL_ST_GOOD);
    rl_ring_status(cur_ring[LEFT], out, &count, cap);
    rl_ring_status(cur_ring[RIGHT], out, &count, cap);
    return count;
}

/*
 * テスト用の状態の付与（環境変数 HENGBAND_TEST_STATUS。カンマ区切り）。画面の入力の経路の検査（スクリーンショット）と
 * プロトコルの検査（tools/rogue_legacy/status_effects_smoke.py）が、乱数に頼らず状態のアイコンを確かめるための足場
 * （変愚の HD2D_TEST_FORCE_IMAGE と同じ扱い）。**環境変数が無ければ何もしない**。ゲームのルールは変えない——
 * 付けるのは原作の本物の状態の旗で、原作の処理がそのまま扱う（空腹は食料の残りを境の 1 手前にして、原作の stomach() に
 * 空腹へ移らせる）。名前: blind confused haste seeinv confmon held hungry。5.4 だけ levitate halluc detect。
 * ゲームが始まってから最初の 1 回だけ付ける。
 */
static int rl_test_spec_has(const char *spec, const char *name)
{
    const size_t length = strlen(name);
    const char *at = spec;
    while (at != NULL && *at != 0) {
        const char *end = strchr(at, ',');
        const size_t span = end != NULL ? (size_t)(end - at) : strlen(at);
        if (span == length && strncmp(at, name, length) == 0) return 1;
        at = end != NULL ? end + 1 : NULL;
    }
    return 0;
}

void rl_test_status_apply(void)
{
    static int done = 0;
    const char *spec;
    if (done) return;
    done = 1;
    spec = getenv("HENGBAND_TEST_STATUS");
    if (spec == NULL || spec[0] == 0) return;
    if (rl_test_spec_has(spec, "blind")) player.t_flags |= ISBLIND;
    if (rl_test_spec_has(spec, "confused")) player.t_flags |= ISHUH;
    if (rl_test_spec_has(spec, "haste")) player.t_flags |= ISHASTE;
    if (rl_test_spec_has(spec, "seeinv")) player.t_flags |= CANSEE;
    if (rl_test_spec_has(spec, "confmon")) player.t_flags |= CANHUH;
    if (rl_test_spec_has(spec, "held")) no_move = 9000;
#if RL_GENERATION == 54
    if (rl_test_spec_has(spec, "levitate")) player.t_flags |= ISLEVIT;
    if (rl_test_spec_has(spec, "halluc")) player.t_flags |= ISHALU;
    if (rl_test_spec_has(spec, "detect")) player.t_flags |= SEEMONST;
#endif
    if (rl_test_spec_has(spec, "hungry")) food_left = 2 * MORETIME + 1;
}

/*
 * テスト用の持ち物の付与（環境変数 HENGBAND_TEST_FX_ITEMS。`種類:原作の名前` を ; で区切る。種類は wand・scroll・weapon）。
 * 攻撃の効果の検査（tools/rogue_legacy/rl_attack_fx_check.py）が、杖・巻物・武器を乱数に頼らず手に入れるための足場
 * （上の HENGBAND_TEST_STATUS と同じ扱い）。**環境変数が無ければ何もしない**。作り方は原作の new_thing() と同じ
 * （原作の new_item・fix_stick・init_weapon・add_pack をそのまま呼ぶ）で、ゲームのルールは変えない。名前の終わりに ! を付けると
 * その杖・巻物を鑑定済みにする（持ち物の名前で字を探すため）。ゲームが始まってから最初の 1 回だけ。
 */
static int rl_test_find_name(const char *kind, const char *name)
{
    int i;
    if (strcmp(kind, "wand") == 0) {
        for (i = 0; i < MAXSTICKS; ++i) {
#if RL_GENERATION == 36
            if (strcmp(ws_magic[i].mi_name, name) == 0) return i;
#else
            if (strcmp(ws_info[i].oi_name, name) == 0) return i;
#endif
        }
    } else if (strcmp(kind, "scroll") == 0) {
        for (i = 0; i < MAXSCROLLS; ++i) {
#if RL_GENERATION == 36
            if (strcmp(s_magic[i].mi_name, name) == 0) return i;
#else
            if (strcmp(scr_info[i].oi_name, name) == 0) return i;
#endif
        }
    } else if (strcmp(kind, "weapon") == 0) {
        for (i = 0; i < MAXWEAPONS; ++i) {
#if RL_GENERATION == 36
            if (strcmp(w_names[i], name) == 0) return i;
#else
            if (strcmp(weap_info[i].oi_name, name) == 0) return i;
#endif
        }
    }
    return -1;
}

static void rl_test_give(const char *kind, int which, int known)
{
#if RL_GENERATION == 36
    struct linked_list *item = new_item(sizeof(struct object));
    struct object *obj = (struct object *)ldata(item);
    obj->o_hplus = obj->o_dplus = 0;
    strcpy(obj->o_damage, "0d0");
    strcpy(obj->o_hurldmg, "0d0");
    obj->o_ac = 11;
    obj->o_count = 1;
    obj->o_group = 0;
    obj->o_flags = 0;
    obj->o_which = which;
    if (strcmp(kind, "weapon") == 0) { obj->o_type = WEAPON; init_weapon(obj, which); }
    else if (strcmp(kind, "scroll") == 0) obj->o_type = SCROLL;
    else { obj->o_type = STICK; fix_stick(obj); }
    add_pack(item, TRUE);
    if (known && strcmp(kind, "wand") == 0) ws_know[which] = TRUE;
    if (known && strcmp(kind, "scroll") == 0) s_know[which] = TRUE;
#else
    THING *obj = new_item();
    obj->o_hplus = 0;
    obj->o_dplus = 0;
    strncpy(obj->o_damage, "0x0", sizeof(obj->o_damage));
    strncpy(obj->o_hurldmg, "0x0", sizeof(obj->o_hurldmg));
    obj->o_arm = 11;
    obj->o_count = 1;
    obj->o_group = 0;
    obj->o_flags = 0;
    obj->o_which = which;
    if (strcmp(kind, "weapon") == 0) init_weapon(obj, which);
    else if (strcmp(kind, "scroll") == 0) obj->o_type = SCROLL;
    else { obj->o_type = STICK; fix_stick(obj); }
    add_pack(obj, TRUE);
    if (known && strcmp(kind, "wand") == 0) ws_info[which].oi_know = TRUE;
    if (known && strcmp(kind, "scroll") == 0) scr_info[which].oi_know = TRUE;
#endif
}

void rl_test_fx_items_apply(void)
{
    static int done = 0;
    char spec[512];
    const char *env;
    char *at;
    if (done) return;
    done = 1;
    env = getenv("HENGBAND_TEST_FX_ITEMS");
    if (env == NULL || env[0] == 0) return;
    snprintf(spec, sizeof(spec), "%s", env);
    for (at = strtok(spec, ";"); at != NULL; at = strtok(NULL, ";")) {
        char *colon = strchr(at, ':');
        size_t length;
        int which;
        int known = 0;
        if (colon == NULL) continue;
        *colon = 0;
        length = strlen(colon + 1);
        if (length > 0 && colon[length] == '!') { /* 名前の終わりの ! は「鑑定済みにする」 */
            colon[length] = 0;
            known = 1;
        }
        which = rl_test_find_name(at, colon + 1);
        if (which >= 0) rl_test_give(at, which, known);
    }
}

/*
 * 自己検査。ゲームの大域変数を直に書き換えて、組み合わせを全数で照合する（このプロセスはこの検査で終わる）。
 * 未鑑定の指輪・重なり・効果量の出し分けなどは実機で作りにくい。
 */
/*
 * 送った鍵の控えと、画面側の表（`hd2d/ui/status_icon_table.inc`）との突き合わせ。知らない鍵は疑問符のアイコンになるので、
 * 表に無い鍵を送らないことを自己検査で見る。表のパスは環境変数 HENGBAND_STATUS_ICON_TABLE（無ければ突き合わせは 0 件。関門が渡す）。
 */
static char rl_seen_keys[96][24];
static int rl_seen_count = 0;

static void rl_seen_add(const char *key)
{
    int i;
    for (i = 0; i < rl_seen_count; ++i) {
        if (strcmp(rl_seen_keys[i], key) == 0) return;
    }
    if (rl_seen_count < 96) snprintf(rl_seen_keys[rl_seen_count++], sizeof(rl_seen_keys[0]), "%s", key);
}

static int rl_table_check(void)
{
    const char *path = getenv("HENGBAND_STATUS_ICON_TABLE");
    char *table;
    long size;
    FILE *fp;
    int i, failures = 0;
    if (path == 0 || path[0] == 0) return 0;
    fp = fopen(path, "rb");
    if (fp == 0) {
        fprintf(stderr, "[rogue_legacy] status effects selftest FAIL: cannot read icon table %s\n", path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    table = (char *)malloc((size_t)size + 1);
    if (table == 0) {
        fclose(fp);
        return 1;
    }
    size = (long)fread(table, 1, (size_t)size, fp);
    table[size] = 0;
    fclose(fp);
    for (i = 0; i < rl_seen_count; ++i) {
        char needle[40];
        snprintf(needle, sizeof(needle), "\"%s\"", rl_seen_keys[i]);
        if (strstr(table, needle) == 0) {
            fprintf(stderr, "[rogue_legacy] status effects selftest FAIL: key %s is not in the icon table\n", rl_seen_keys[i]);
            ++failures;
        }
    }
    free(table);
    if (failures == 0) fprintf(stderr, "[rogue_legacy] status effects: %d keys checked against the icon table\n", rl_seen_count);
    return failures;
}

static int rl_selftest_expect(const char *what, const char *want)
{
    rl_status_effect list[24];
    char got[768];
    int n = rl_status_effects(list, 24);
    int i;
    got[0] = 0;
    for (i = 0; i < n; ++i) {
        char piece[128];
        rl_seen_add(list[i].key);
        snprintf(piece, sizeof(piece), "%s%s/%d=%s%s%s", i > 0 ? "," : "", list[i].key, list[i].kind, list[i].label,
            list[i].amount[0] != 0 ? "!" : "", list[i].amount);
        strncat(got, piece, sizeof(got) - strlen(got) - 1);
    }
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "[rogue_legacy] status effects selftest FAIL %s: want [%s] got [%s]\n", what, want, got);
        return 1;
    }
    return 0;
}

static void rl_selftest_know(int which, int known)
{
#if RL_GENERATION == 36
    r_know[which] = known;
#else
    ring_info[which].oi_know = known ? TRUE : FALSE;
#endif
}

#if RL_GENERATION == 36
#define RL_SET_BONUS(r, v) ((r).o_ac = (v))
#else
#define RL_SET_BONUS(r, v) ((r).o_arm = (v))
#endif

int rl_status_effects_selftest(void)
{
    int failures = 0;
    RL_RING_OBJ left, right;
    RL_RING_OBJ *saved_left = cur_ring[LEFT], *saved_right = cur_ring[RIGHT];
    int saved_flags = (int)player.t_flags, saved_hungry = hungry_state, saved_no_command = no_command, saved_no_move = no_move;
    int i;
    int saved_know[RL_RING_MAX];
    for (i = 0; i < RL_RING_MAX; ++i) {
#if RL_GENERATION == 36
        saved_know[i] = r_know[i];
#else
        saved_know[i] = ring_info[i].oi_know ? 1 : 0;
#endif
    }
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    player.t_flags = 0;
    hungry_state = 0; no_command = 0; no_move = 0;
    cur_ring[LEFT] = NULL; cur_ring[RIGHT] = NULL;
    for (i = 0; i < RL_RING_MAX; ++i) rl_selftest_know(i, 0);

    failures += rl_selftest_expect("何もかかっていない", "");
    hungry_state = 1;
    failures += rl_selftest_expect("空腹", "hungry/1=空腹");
    hungry_state = 2;
    failures += rl_selftest_expect("飢餓", "hungry/1=飢餓");
    hungry_state = 3;
    failures += rl_selftest_expect("瀕死", "hungry/1=瀕死");
    hungry_state = 0;
    player.t_flags = ISBLIND | ISHUH | ISHASTE | CANSEE | CANHUH;
    no_command = 3;
    no_move = 4;
    failures += rl_selftest_expect("一時的な状態",
        "blind/1=盲目,confused/1=混乱,paralyzed/1=麻痺,rl_held/1=拘束,haste/0=加速,senseunseen/0=透明視,attk_conf/0=混乱打撃");
    no_command = 0; no_move = 0;
    player.t_flags = ISHELD;
    failures += rl_selftest_expect("食人植物に捕まっている", "rl_held/1=拘束");
    no_move = 2;
    failures += rl_selftest_expect("食人植物と熊の罠の重なりは 1 つ", "rl_held/1=拘束");
    no_move = 0;
#if RL_GENERATION == 54
    player.t_flags = ISLEVIT | ISHALU | SEEMONST;
    failures += rl_selftest_expect("5.4 だけの状態", "hallucination/1=幻覚,levitate/0=浮遊,telepathy/0=怪物感知");
#endif
    player.t_flags = 0;

    /* 指輪は種類が分かっているものだけ。 */
    left.o_which = R_REGEN;
    cur_ring[LEFT] = &left;
    failures += rl_selftest_expect("未鑑定の指輪は出さない", "");
    rl_selftest_know(R_REGEN, 1);
    failures += rl_selftest_expect("種類が分かった指輪", "regeneration/4=再生");
    right = left;
    cur_ring[RIGHT] = &right;
    failures += rl_selftest_expect("同じ指輪を 2 つ着けても 1 つ", "regeneration/4=再生");
    right.o_which = R_TELEPORT;
    failures += rl_selftest_expect("未鑑定の隣の指輪は出さない", "regeneration/4=再生");
    rl_selftest_know(R_TELEPORT, 1);
    failures += rl_selftest_expect("テレポートの指輪は呪い", "regeneration/4=再生,cursed/5=テレポート");
    left.o_which = R_ADDSTR; RL_SET_BONUS(left, 2); left.o_flags = 0;
    right.o_which = R_PROTECT; RL_SET_BONUS(right, -1); right.o_flags = ISKNOW;
    rl_selftest_know(R_ADDSTR, 1); rl_selftest_know(R_PROTECT, 1);
    failures += rl_selftest_expect("効果量は ISKNOW のときだけ（負は呪い）", "might/4=腕力,low_ac/5=防御低下!-1");
    left.o_which = R_ADDHIT; RL_SET_BONUS(left, 3); left.o_flags = ISKNOW;
    right.o_which = R_ADDDAM; RL_SET_BONUS(right, -2); right.o_flags = ISKNOW;
    rl_selftest_know(R_ADDHIT, 1); rl_selftest_know(R_ADDDAM, 1);
    failures += rl_selftest_expect("命中とダメージ", "blows/4=命中!+3,low_melee/5=ダメージ低下!-2");
    left.o_which = R_PROTECT; RL_SET_BONUS(left, 1); left.o_flags = ISKNOW;
    right.o_which = R_ADDDAM; RL_SET_BONUS(right, 1); right.o_flags = 0;
    rl_selftest_know(R_PROTECT, 1);
    failures += rl_selftest_expect("防御とダメージ（正）", "magicdef/4=防御!+1,impact/4=ダメージ");
    left.o_which = R_STEALTH; right.o_which = R_DIGEST;
    rl_selftest_know(R_STEALTH, 1); rl_selftest_know(R_DIGEST, 1);
    failures += rl_selftest_expect("隠密と遅消化", "stealth/4=隠密,slow_digest/4=遅消化");
    left.o_which = R_SUSTSTR; right.o_which = R_SEEINVIS;
    rl_selftest_know(R_SUSTSTR, 1); rl_selftest_know(R_SEEINVIS, 1);
    failures += rl_selftest_expect("腕力維持と透明視", "sust_str/4=腕力維持,senseunseen/4=透明視");
    left.o_which = R_SEARCH; right.o_which = R_AGGR;
    rl_selftest_know(R_SEARCH, 1); rl_selftest_know(R_AGGR, 1);
    failures += rl_selftest_expect("探索と反感の呪い", "search/4=探索,aggravate/5=反感");
#if RL_GENERATION == 54
    left.o_which = R_SUSTARM; right.o_which = R_NOP;
    rl_selftest_know(R_SUSTARM, 1); rl_selftest_know(R_NOP, 1);
    failures += rl_selftest_expect("5.4 の鎧維持と装飾", "stoneskin/4=鎧維持");
#else
    left.o_which = R_NOP; right.o_which = R_NOP;
    rl_selftest_know(R_NOP, 1);
    failures += rl_selftest_expect("装飾の指輪は効果が無い", "");
#endif

    failures += rl_table_check();
    cur_ring[LEFT] = saved_left;
    cur_ring[RIGHT] = saved_right;
    player.t_flags = saved_flags;
    hungry_state = saved_hungry; no_command = saved_no_command; no_move = saved_no_move;
    for (i = 0; i < RL_RING_MAX; ++i) rl_selftest_know(i, saved_know[i]);
    return failures;
}
