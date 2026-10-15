/*!
 * @file an_fx.c
 * @brief 戦闘の見せ場（命中）と、魔法・投射物の効果（飛ぶ魔法・ボール・ブレス・ビーム・矢弾・投擲）を汲む
 *
 *
 * 正本は `presentation/frame/combat_fx.h`（命中）と `presentation/frame/attack_fx.h`（投射物）。手本は
 * `frox/adapter/fc_shim.c`（環の形・属性の逆引き）と `gensoband/adapter/gb_shim.c`（「宣言してから汲む」の形）。
 *
 * ## 命中（`HitPlayer`/`HitMonster`）は事件が無いので 2 段で拾う
 * `EVENT_HP`（HP が変わった）には場所も攻撃者も乗っていない。原作を最小限だけ
 * 開けて（`tools/angband/prepare_sources.py` の弱デフォルト規約 A1〜A7 と同じ形）:
 *   1. 属性が分かる所（`project_p()`／`project_m_player_attack()`）で
 *      「次の 1 発の属性」を宣言する（`an_fx_note_player_typ()`/`an_fx_note_monster_typ()`）。
 *   2. 実際にダメージが入る所（`take_hit()`／`mon_take_hit()`）で汲んで 1 件積み、
 *      宣言は使ったら消す（次の素手の一撃が前の呪文の色を引きずらないように）。
 *
 * ## 魔法・投射物の効果（2026-10-11。`GameFrame::projections`・`missiles`）
 * 原作の `EVENT_BOLT`/`EVENT_EXPLOSION` には撃った者・形・半径・旗が乗らず、`PROJECT_HIDE` の投射
 * （飛ばない魔法・視界内の全員）は事件が出ない。そこで `project()` に**観測の呼び出し 1 か所**
 * （`an_fx_note_projection()`。`tools/core_sources/recipes/angband.json` の `project.c`。範囲と可視が確定した
 * 直後）を足し、矢弾の命中の印（`an_fx_note_missile_hit()`。`player-attack.c` の `ranged_helper()`）も 1 か所足した。
 * どちらも記録するだけで、ゲームのルールは変えない。矢弾の位置と種類は原作の `EVENT_MISSILE` で足りる。
 * 積む先は共通の `presentation/bridge/named_attack_fx.h`（属性を名前で積む記録の置き場。`an_frame.cpp` が
 * C 側から呼ぶ入口を持つ）。旧来の `EVENT_BOLT`/`EVENT_EXPLOSION`/`EVENT_MISSILE` を `combat_fx` へ積む受け手は
 * **外した**（新しい記録と二重に出るため）。`combat_fx` に残るのは命中の火花（`HitPlayer`/`HitMonster`）だけ。
 */
#include "angband.h"

#include "project.h" /* PROJ_*（属性の逆引き）・origin_get_loc() */
#include "cave.h" /* distance()・square_isview() */
#include "init.h" /* z_info */
#include "obj-gear.h" /* equipped_item_by_slot_name() */
#include "obj-slays.h" /* brands[] */
#include "obj-tval.h" /* tval_is_ammo() */
#include "player-timed.h" /* TMD_BLIND */
#include "source.h"

#include "an_shim.h" /* an_fx_event / an_fx_take() の宣言 */

#include <string.h>

/* ==================================================================== 環 */

/* AN_FX_MAX は an_shim.h の定義を使う（見直しの指摘。以前はここだけの
 * ローカル定義で、`an_frame.cpp` 側の受け皿の大きさと食い違っていた）。 */

static an_fx_event an_fx_ring[AN_FX_MAX];
static int an_fx_num = 0;

/*! 溢れたら古いほうを捨てる（新しい出来事のほうが見たい。Frox と同じ）。 */
static void an_fx_push(const an_fx_event *ev)
{
    if (an_fx_num >= AN_FX_MAX) {
        int i;
        for (i = 1; i < AN_FX_MAX; i++) {
            an_fx_ring[i - 1] = an_fx_ring[i];
        }
        an_fx_num = AN_FX_MAX - 1;
    }
    an_fx_ring[an_fx_num++] = *ev;
}

int an_fx_take(an_fx_event *out, int cap)
{
    int n;

    if (!out || (cap <= 0)) {
        an_fx_num = 0;
        return 0;
    }
    n = (an_fx_num < cap) ? an_fx_num : cap;
    memcpy(out, an_fx_ring, (size_t)n * sizeof(an_fx_event));
    /* 汲んだら消す（`combat_fx.h` の契約。取りこぼしても演出が溜まらないように）。 */
    an_fx_num = 0;
    return n;
}

/* ==================================================================== 属性の逆引き */

/*!
 * @brief `PROJ_*`（`project.h`）→ `CombatFxElement` の束（0〜9。`combat_fx.h`）。
 * @details Spacetime/Insanity（10/11）は**幻想蛮怒だけの軸**なので Angband からは
 * 絶対に立てない（`combat_fx.h` の節註）。変愚系（`gensoband/adapter/gb_shim.c` の
 * `gb_fx_element_of()`）が `GF_TIME`/`GF_NEXUS`/`GF_GRAVITY` を混沌・物理へ残している
 * のと同じ考え方に揃えた——Angband の `PROJ_NEXUS`/`PROJ_TIME` も混沌（因果混乱・
 * 時間逆転は `combat_fx.h` の Chaos の節註そのもの）、`PROJ_GRAVITY` は物理のまま。
 * 当てずっぽうで色を付けず、対応が無いものは既定（物理）に残す。
 */
static int an_fx_element_of_proj(int typ)
{
    switch (typ) {
    case PROJ_FIRE:
    case PROJ_PLASMA:
    case PROJ_METEOR:
        return 1; /* 火 */
    case PROJ_COLD:
    case PROJ_ICE:
    case PROJ_WATER: /* 水も冷へ（Frox/幻想蛮怒と同じ振り分け） */
        return 2; /* 冷 */
    case PROJ_ELEC:
        return 3; /* 電 */
    case PROJ_ACID:
        return 4; /* 酸 */
    case PROJ_POIS:
        return 5; /* 毒 */
    case PROJ_DARK:
    case PROJ_DARK_WEAK:
    case PROJ_NETHER: /* 地獄・冥府 */
        return 6; /* 闇 */
    case PROJ_LIGHT:
    case PROJ_LIGHT_WEAK:
    case PROJ_HOLY_ORB: /* 聖なる光 */
        return 7; /* 光 */
    case PROJ_CHAOS:
    case PROJ_DISEN:
    case PROJ_NEXUS: /* 因果混乱 */
    case PROJ_TIME: /* 時間逆転 */
        return 8; /* 混沌 */
    default:
        /*
         * 物理・矢・轟音（`PROJ_SOUND`）・破片（`PROJ_SHARD`）・重力（`PROJ_GRAVITY`）・
         * 慣性・force・弾幕（`PROJ_MISSILE`）・魔力（`PROJ_MANA`）ほか。
         * 変愚も同じ顔ぶれを物理（白）へ残している（`gb_shim.c` の `default` 節註）。
         */
        return 0; /* 物理 */
    }
}

/* ==================================================================== 命中の宣言（項1） */

/*! -1 は「宣言なし」（既定の物理へ落ちる）。使ったら必ず -1 へ戻す。 */
static int an_fx_pending_player_typ = -1;
static int an_fx_pending_monster_typ = -1;

void an_fx_note_player_typ(int typ)
{
    an_fx_pending_player_typ = typ;
}

void an_fx_note_monster_typ(int typ)
{
    an_fx_pending_monster_typ = typ;
}

void an_fx_note_player_hit(int dam, int mhp)
{
    an_fx_event ev;
    int typ = an_fx_pending_player_typ;

    an_fx_pending_player_typ = -1; /* 使ったら消す（次の素手の一撃へ持ち越さない） */
    if (dam <= 0) {
        return;
    }
    memset(&ev, 0, sizeof(ev));
    ev.kind = 0; /* CombatFxKind::HitPlayer */
    ev.element = (typ >= 0) ? an_fx_element_of_proj(typ) : 0;
    ev.y = player ? player->grid.y : 0;
    ev.x = player ? player->grid.x : 0;
    ev.src_y = ev.y;
    ev.src_x = ev.x;
    ev.intensity = (mhp > 0) ? ((float)dam / (float)mhp) : 1.0f;
    if (ev.intensity > 1.0f) {
        ev.intensity = 1.0f;
    }
    if (ev.intensity < 0.15f) {
        ev.intensity = 0.15f; /* かすり傷でも見えるだけの下限（`combat_fx.h` の節註） */
    }
    an_fx_push(&ev);
}

void an_fx_note_monster_hit(int y, int x, int dam, int max_hp)
{
    an_fx_event ev;
    int typ = an_fx_pending_monster_typ;

    an_fx_pending_monster_typ = -1;
    if (dam <= 0) {
        return;
    }
    memset(&ev, 0, sizeof(ev));
    ev.kind = 1; /* CombatFxKind::HitMonster */
    ev.element = (typ >= 0) ? an_fx_element_of_proj(typ) : 0;
    ev.y = y;
    ev.x = x;
    ev.src_y = ev.y;
    ev.src_x = ev.x;
    ev.intensity = (max_hp > 0) ? ((float)dam / (float)max_hp) : 1.0f;
    if (ev.intensity > 1.0f) {
        ev.intensity = 1.0f;
    }
    if (ev.intensity < 0.15f) {
        ev.intensity = 0.15f;
    }
    an_fx_push(&ev);
}

/* ==================================================================== 魔法・投射物の効果（projections / missiles） */

/*!
 * @brief 原作の `PROJ_*`（`list-elements.h` の 25 個と `list-projections.h` の 31 個）→ 画面へ運ぶ属性の名前。
 * @details 名前は `presentation/bridge/attack_fx_attr_names.h`（画面側の登録表と 1 字も違えてはならない表）にあるものだけ。
 * 同じ意味のものは同名（ACID→acid ほか）。画面側に同じものが無いものは、決めたこと（2026-10-11「案どおり寄せる」）で
 * 近い見た目へ寄せる。**全数の照合は `tools/angband/an_attack_fx_check.py --selftest` がする**（原作の列挙の漏れ・
 * 画面側に無い名前・寄せ方の食い違いを落とす）。表に無い番号は空の名前（画面側は汎用の見た目に落ちる）。
 */
static const struct {
    int typ;
    const char *name;
} an_fx_proj_names[] = {
    /* ELEM_*（list-elements.h） */
    { PROJ_ACID, "acid" },
    { PROJ_ELEC, "elec" },
    { PROJ_FIRE, "fire" },
    { PROJ_COLD, "cold" },
    { PROJ_POIS, "pois" },
    { PROJ_LIGHT, "lite" },
    { PROJ_DARK, "dark" },
    { PROJ_SOUND, "sound" },
    { PROJ_SHARD, "shards" },
    { PROJ_NEXUS, "nexus" },
    { PROJ_NETHER, "nether" },
    { PROJ_CHAOS, "chaos" },
    { PROJ_DISEN, "disenchant" },
    { PROJ_WATER, "water" },
    { PROJ_ICE, "ice" },
    { PROJ_GRAVITY, "gravity" },
    { PROJ_INERTIA, "inertial" },
    { PROJ_FORCE, "force" },
    { PROJ_TIME, "time" },
    { PROJ_PLASMA, "plasma" },
    { PROJ_METEOR, "meteor" },
    { PROJ_MISSILE, "missile" },
    { PROJ_MANA, "mana" },
    { PROJ_HOLY_ORB, "holy_fire" },
    { PROJ_ARROW, "monster_shoot" },
    /* PROJ_*（list-projections.h） */
    { PROJ_LIGHT_WEAK, "lite_weak" },
    { PROJ_DARK_WEAK, "dark_weak" },
    { PROJ_KILL_WALL, "kill_wall" },
    { PROJ_KILL_DOOR, "kill_door" },
    { PROJ_KILL_TRAP, "kill_trap" },
    { PROJ_MAKE_DOOR, "make_door" },
    { PROJ_MAKE_TRAP, "make_trap" },
    { PROJ_AWAY_UNDEAD, "away_undead" },
    { PROJ_AWAY_SPIRIT, "away_evil" },
    { PROJ_AWAY_EVIL, "away_evil" },
    { PROJ_AWAY_ALL, "away_all" },
    { PROJ_TURN_UNDEAD, "turn_undead" },
    { PROJ_TURN_EVIL, "turn_evil" },
    { PROJ_TURN_LIVING, "turn_all" },
    { PROJ_TURN_ALL, "turn_all" },
    { PROJ_DISP_UNDEAD, "disp_undead" },
    { PROJ_DISP_EVIL, "disp_evil" },
    { PROJ_DISP_ALL, "disp_all" },
    { PROJ_SLEEP_UNDEAD, "old_sleep" },
    { PROJ_SLEEP_EVIL, "old_sleep" },
    { PROJ_SLEEP_ALL, "old_sleep" },
    { PROJ_MON_CLONE, "old_clone" },
    { PROJ_MON_POLY, "old_poly" },
    { PROJ_MON_HEAL, "old_heal" },
    { PROJ_MON_SPEED, "old_speed" },
    { PROJ_MON_SLOW, "old_slow" },
    { PROJ_MON_CONF, "old_conf" },
    { PROJ_MON_HOLD, "stasis" },
    { PROJ_MON_STUN, "stun" },
    { PROJ_MON_DRAIN, "hypodynamia" },
    { PROJ_MON_CRUSH, "hand_doom" },
};

/*! @brief 矢弾のブランド（`brand.txt` の name）→ 画面へ運ぶ属性の名前。 */
static const struct {
    const char *brand;
    const char *name;
} an_fx_brand_names[] = {
    { "acid", "acid" },
    { "lightning", "elec" },
    { "fire", "fire" },
    { "cold", "cold" },
    { "poison", "pois" },
};

static const char *an_fx_proj_name(int typ)
{
    size_t i;

    for (i = 0; i < sizeof(an_fx_proj_names) / sizeof(an_fx_proj_names[0]); i++) {
        if (an_fx_proj_names[i].typ == typ) {
            return an_fx_proj_names[i].name;
        }
    }
    return "";
}

/*! `presentation/frame/attack_fx.h` の `ProjectionShape` / `AttackFxOrigin` / `ProjectionFxFlag` と同じ並び。 */
enum {
    AN_FX_SHAPE_BOLT = 0,
    AN_FX_SHAPE_BEAM = 1,
    AN_FX_SHAPE_BALL = 2,
    AN_FX_SHAPE_BREATH = 3,
    AN_FX_SHAPE_JUMP = 4
};
enum { AN_FX_ORIGIN_PLAYER = 0, AN_FX_ORIGIN_MONSTER = 1, AN_FX_ORIGIN_OTHER = 2 };
enum { AN_FX_FLAG_HIDDEN = 1, AN_FX_FLAG_BLIND = 2, AN_FX_FLAG_ALL_IN_SIGHT = 8 };
/*! `MissileKind` と同じ並び。 */
enum { AN_FX_MISSILE_ARROW = 0, AN_FX_MISSILE_BOLT = 1, AN_FX_MISSILE_SHOT = 2, AN_FX_MISSILE_THROWN = 3 };

/*! 1 件に積むマスの上限（`named_attack_fx.h` の `kNamedMaxCells` と同じ）。 */
#define AN_FX_CELLS 512

/*!
 * @brief `project()` が範囲と可視を決めた直後に呼ばれる（`project.c` の観測の呼び出し 1 か所。強い定義）。
 * @details 形の決め方：弧（`PROJECT_ARC`。ブレス）→ ビーム（`PROJECT_BEAM`）→ 直接（`PROJECT_JUMP`＝視界内の全員ほか、
 * または始点と着弾点が同じ）→ 半径があればボール、無ければボルト。`PROJECT_JUMP` は `project()` が先に外すので、
 * 「始点が撃った者の位置と違う」ことで見分ける。
 *
 * 範囲（`blast_grid`）はプレイヤの視界内のマスだけ積む（原作の爆発の描画は見えるマスだけ）。隠す投射
 * （`PROJECT_HIDE`）は 1 マス（着弾点）しか使わないので全部積む。
 */
void an_fx_note_projection(struct source origin, struct loc start, struct loc finish, struct loc centre, int rad,
    int flg, int typ, bool blind, int num_path_grids, const struct loc *path_grid, int num_grids,
    const struct loc *blast_grid, const int *distance_to_grid)
{
    static an_fx_cell path[AN_FX_CELLS];
    static an_fx_cell area[AN_FX_CELLS];
    int path_n = 0;
    int area_n = 0;
    int i;
    int shape;
    int origin_kind;
    int flags = 0;
    int jumped;
    struct loc src = origin_get_loc(origin);
    struct loc impact = centre;

    if (src.y < 0 || src.x < 0) {
        src = start;
    }
    jumped = (src.y != start.y) || (src.x != start.x);

    if ((flg & PROJECT_ARC) && (num_path_grids > 0)) {
        shape = AN_FX_SHAPE_BREATH;
    } else if (flg & PROJECT_BEAM) {
        shape = AN_FX_SHAPE_BEAM;
    } else if (jumped || ((start.y == finish.y) && (start.x == finish.x)) || (num_path_grids <= 0)) {
        shape = AN_FX_SHAPE_JUMP;
    } else if (rad > 0) {
        shape = AN_FX_SHAPE_BALL;
    } else {
        shape = AN_FX_SHAPE_BOLT;
    }

    /* 経路：始点の次のマスから着弾点まで（ブレスは弧の中心線。ビームは経路の全部）。 */
    if (shape == AN_FX_SHAPE_BEAM || shape == AN_FX_SHAPE_BREATH) {
        for (i = 0; i < num_path_grids && path_n < AN_FX_CELLS; i++) {
            path[path_n].y = path_grid[i].y;
            path[path_n].x = path_grid[i].x;
            path[path_n].dist = 0;
            path_n++;
        }
    } else if (shape != AN_FX_SHAPE_JUMP && !((centre.y == start.y) && (centre.x == start.x))) {
        for (i = 0; i < num_path_grids && path_n < AN_FX_CELLS; i++) {
            path[path_n].y = path_grid[i].y;
            path[path_n].x = path_grid[i].x;
            path[path_n].dist = 0;
            path_n++;
            if ((path_grid[i].y == centre.y) && (path_grid[i].x == centre.x)) {
                break;
            }
        }
    }
    if (shape == AN_FX_SHAPE_BEAM && path_n > 0) {
        impact = loc(path[path_n - 1].x, path[path_n - 1].y);
    } else if (shape == AN_FX_SHAPE_BREATH) {
        impact = finish;
    } else if (shape == AN_FX_SHAPE_JUMP) {
        impact = jumped ? finish : centre;
    }

    /* 範囲：見えているマスだけ（隠す投射は全部）。 */
    for (i = 0; i < num_grids && area_n < AN_FX_CELLS; i++) {
        int d = distance_to_grid[i];

        if (!(flg & PROJECT_HIDE) && !square_isview(cave, blast_grid[i])) {
            continue;
        }
        area[area_n].y = blast_grid[i].y;
        area[area_n].x = blast_grid[i].x;
        area[area_n].dist = (d < 0) ? 0 : ((d > 255) ? 255 : d);
        area_n++;
    }

    origin_kind = (origin.what == SRC_PLAYER) ? AN_FX_ORIGIN_PLAYER
        : ((origin.what == SRC_MONSTER) ? AN_FX_ORIGIN_MONSTER : AN_FX_ORIGIN_OTHER);
    if (flg & PROJECT_HIDE) {
        flags |= AN_FX_FLAG_HIDDEN;
    }
    if (blind) {
        flags |= AN_FX_FLAG_BLIND;
    }
    /* 視界内の全員（`effect_handler_PROJECT_LOS`）：プレイヤが飛び先指定・当てる・隠す、で半径 0。 */
    if ((origin.what == SRC_PLAYER) && jumped && (rad == 0) && !(flg & (PROJECT_BEAM | PROJECT_ARC))
        && ((flg & ~PROJECT_AWARE) == (PROJECT_KILL | PROJECT_HIDE))) {
        flags |= AN_FX_FLAG_ALL_IN_SIGHT;
    }

    an_named_fx_projection(origin_kind, shape, an_fx_proj_name(typ), rad, src.y, src.x, impact.y, impact.x, flags,
        path, path_n, area, area_n);
}

/*! @brief 矢弾の属性：**識別済みの**ブランドだけ（原作より情報を増やさない）。 */
static const char *an_fx_known_brand(const struct object *obj)
{
    const struct object *launcher = NULL;
    const struct brand *best = NULL;
    int pass;

    if (!obj || !brands) {
        return "";
    }
    if (tval_is_ammo(obj)) {
        launcher = equipped_item_by_slot_name(player, "shooting");
    }
    for (pass = 0; pass < 2; pass++) {
        const struct object *o = (pass == 0) ? obj : launcher;
        int i;

        if (!o || !o->known || !o->known->brands) {
            continue;
        }
        for (i = 0; i < z_info->brand_max; i++) {
            if (o->known->brands[i] && (!best || brands[i].multiplier > best->multiplier)) {
                best = &brands[i];
            }
        }
    }
    if (best && best->name) {
        size_t i;

        for (i = 0; i < sizeof(an_fx_brand_names) / sizeof(an_fx_brand_names[0]); i++) {
            if (streq(best->name, an_fx_brand_names[i].brand)) {
                return an_fx_brand_names[i].name;
            }
        }
    }
    return "";
}

/*!
 * @brief `EVENT_MISSILE`（矢弾・投げた物が 1 マス進むたび）→ 記録の置き場へ 1 歩。
 * @details 種類は、使った物が弾薬で、合う発射具を構えているなら射撃（矢・クロスボウの矢・スリングの弾）、
 * それ以外は投擲。**盲目のときは記録しない**（原作も見えないマスの動きは描かない）。
 * 1 発目の判定は「プレイヤの隣のマス」（経路の先頭は必ず隣接する）。
 */
static void an_fx_on_missile(game_event_type type, game_event_data *data, void *user)
{
    const struct object *obj;
    struct loc grid;
    int first;
    int kind;
    int unaware;
    const char *brand = "";

    (void)type;
    (void)user;
    if (!data || !player || !cave || !data->missile.obj || !data->missile.obj->kind) {
        return;
    }
    if (player->timed[TMD_BLIND]) {
        return;
    }
    obj = data->missile.obj;
    grid = loc(data->missile.x, data->missile.y);
    first = (distance(player->grid, grid) <= 1);

    if (tval_is_ammo(obj) && equipped_item_by_slot_name(player, "shooting")
        && (player->state.ammo_tval == obj->tval)) {
        kind = (obj->tval == TV_BOLT) ? AN_FX_MISSILE_BOLT
            : ((obj->tval == TV_SHOT) ? AN_FX_MISSILE_SHOT : AN_FX_MISSILE_ARROW);
    } else {
        kind = AN_FX_MISSILE_THROWN;
    }
    /* 絵の番号は、名前を知らない薬・巻物などの正体を漏らさないよう、見分けが付かない物では引かない。 */
    unaware = (obj->kind->flavor && !obj->kind->aware) ? 1 : 0;
    if (first) {
        brand = an_fx_known_brand(obj);
    }
    an_named_fx_missile_step(first, kind, obj->kind->name, (int)obj->tval, unaware, brand, player->grid.y,
        player->grid.x, grid.y, grid.x);
}

/*! @brief 命中の印（`player-attack.c` の `ranged_helper()` の観測の呼び出し 1 か所。強い定義）。 */
void an_fx_note_missile_hit(void)
{
    an_named_fx_missile_hit();
}

void an_fx_install_handlers(void)
{
    /* 付け直し（2 回目のゲーム）でも二重にならないよう、先に外す（無ければ何もしない）。 */
    event_remove_handler(EVENT_MISSILE, an_fx_on_missile, NULL);
    event_add_handler(EVENT_MISSILE, an_fx_on_missile, NULL);
}
