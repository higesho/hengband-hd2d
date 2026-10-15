/*!
 * @file an_targeting.c
 * @brief `an_targeting.h` の実体。
 * - §8.5 T1: `get_aim_dir_hook` を新 UI の目標選択（`hb_target_choose()`）へ寄せる。
 * - §8.5 T2: A3（`cmd-core.c` の `an_note_target_command()`）で知った「いまのコマンド」
 *   から使用中のアイテムを引き、`object_effect()` の `struct effect` でボルト・
 *   ビーム・ボールの射線・範囲を予測する。予測はゲームの乱数を進めない
 *   （`project_path()`・`los()`・`square_isprojectable()` はどれも純粋な幾何関数）。
 *
 * 手本は `silq/adapter/sq_targeting.inl`
 * の候補・妥当性判定・射線予測の部分。
 * `an_ui_shim.c` と同じ「差し替えるのではなく包む」流儀——`textui_init()` が
 * `get_aim_dir_hook` へ既定実装（`textui_get_aim_dir`）を差した**後**に、
 * それを控えてから乗せ替える。
 */

#include "angband.h"

#include "cave.h"
#include "cmd-core.h" /* struct command / cmd_get_arg_item */
#include "effects.h" /* struct effect / effect_index (EF_*) */
#include "game-input.h"
#include "game-world.h" /* extract_energy / turn_energy */
#include "init.h" /* z_info->move_energy / z_info->max_range */
#include "mon-desc.h"
#include "message.h" /* msg()（T4「見る」の「詳細」をメッセージへ流す） */
#include "mon-lore.h" /* get_lore() / lore_description()（T4「見る」の「詳細」） */
#include "mon-predicate.h" /* monster_is_obvious() */
#include "obj-desc.h" /* object_desc() / ODESC_*（T4「見る」のアイテム） */
#include "obj-ignore.h" /* ignore_known_item_ok() */
#include "obj-info.h" /* object_info()（T4「見る」の「詳細」） */
#include "obj-knowledge.h" /* object_flavor_is_aware() */
#include "obj-pile.h" /* scan_distant_floor()（AH-54: 見る・狙うの「無視」） */
#include "player-calcs.h" /* track_object() / handle_stuff()（AH-54） */
#include "ui-object.h" /* textui_cmd_ignore_menu()（AH-54） */
#include "obj-util.h" /* object_effect() */
#include "player-path.h" /* pathfind_direction_to()（T3） */
#include "project.h" /* project_path() / PROJECT_* */
#include "target.h"
#include "trap.h" /* struct trap / square()->trap（T4「見る」の罠） */
#include "ui-event.h"
#include "ui-input.h"
#include "ui-target.h" /* target_dir_allow */
#include "ui-game.h" /* cmd_lookup_key()（「無視する」のキーを、いまのキー配置で引く） */
#include "ui-keymap.h" /* KEYMAP_MODE_ORIG / KEYMAP_MODE_ROGUE */
#include "z-form.h" /* strnfmt() */
#include "z-rand.h" /* Rand_quick / Rand_value / STATE / z0-z2 */
#include "z-textblock.h" /* textblock（T4「見る」の「詳細」） */
#include "z-type.h" /* struct point_set / loc() */
#include "z-util.h" /* is_a_vowel() */
#include "z-virt.h" /* mem_alloc() / mem_free() */

#include "bridge/native_targeting_api.h"

#include "an_lang_c.h" /* an_tr()（AH-54: 追加操作の字） */
#include "an_targeting.h"
#include "an_wide.h" /* AN_WIDE_PAD（AH-59: 「詳細」のメッセージから埋め字を除く） */

#include <string.h>
#include <wchar.h>

static bool (*an_orig_get_aim_dir)(int *dp) = NULL;

/* AH-54: 見る・狙うの追加操作の申告（実体は an_wrapped_look() の前）。 */
static void an_target_emit_extras(const native_target_choice *choice);

static int an_target_shape_for_effect(int index);

/*!
 * `cmd_get_target()` が呼ばれた時点で、そのコマンドのアイテムから**効果の写しを取っておく**。
 * コマンドが持つ `item` は生のオブジェクトへのポインタで、使い切った・投げ切ったアイテムは
 * コマンドの後で解放される（「You have no more Rations」）。それを後の `get_aim_dir()` で
 * たどると解放済みの領域を読む（デバッグの効果の命令 `^a E` など、`cmd_get_target()` を通らずに
 * `get_aim_dir()` が呼ばれる経路で、直前の投げたあとの古いポインタをたどってコアが
 * アクセス違反で落ちた。`tools/angband/an_attack_fx_check.py` の盲目のボルトで、負荷が高いときだけ）。
 */
#define AN_TARGET_SNAP_MAX 8
typedef struct an_target_snap_effect {
    int shape;
    int subtype;
    int radius;
} an_target_snap_effect;

static an_target_snap_effect an_target_snap[AN_TARGET_SNAP_MAX];
static int an_target_snap_count = 0;
static int an_target_snap_known = 0;

void an_note_target_command(struct command *cmd)
{
    struct object *obj = NULL;
    struct effect *eff;

    an_target_snap_count = 0;
    an_target_snap_known = 0;
    if (cmd && cmd_get_arg_item(cmd, "item", &obj) == CMD_OK && obj && obj->kind) {
        /* 正体が未識別のワンド・ロッドは効果を明かさない（`object_flavor_is_aware()` が偽）。 */
        an_target_snap_known = !obj->kind->flavor || object_flavor_is_aware(obj);
        if (an_target_snap_known) {
            for (eff = object_effect(obj); eff && an_target_snap_count < AN_TARGET_SNAP_MAX; eff = eff->next) {
                int shape = an_target_shape_for_effect(eff->index);
                int radius;

                if (shape == NATIVE_SHAPE_NONE) continue;
                radius = eff->radius;
                if (shape == NATIVE_SHAPE_BALL && radius <= 0) radius = 2; /* effect_handler_BALL と同じ既定。 */
                an_target_snap[an_target_snap_count].shape = shape;
                an_target_snap[an_target_snap_count].subtype = eff->subtype;
                an_target_snap[an_target_snap_count].radius = radius;
                an_target_snap_count++;
            }
        }
    }
}

/*! 予測がゲームの乱数を進めないことを確かめる検査（設計 §8.5 T2。
 *  `silq/adapter/sq_targeting.inl` の `_sq_target_rng_check` と同じ考え方）。
 *  `z-rand.h` の複素 RNG の状態一式（`Rand_quick`・`Rand_value` は簡易 RNG、
 *  `state_i`・`STATE[]`・`z0`〜`z2` は複素 RNG）を候補列挙・予測の前後で
 *  比べる。 */
typedef struct an_target_rng_snapshot {
    bool quick;
    uint32_t value;
    uint32_t state_i;
    uint32_t state[RAND_DEG];
    uint32_t z0, z1, z2;
} an_target_rng_snapshot;

static an_target_rng_snapshot an_target_rng_save(void)
{
    an_target_rng_snapshot s;
    s.quick = Rand_quick;
    s.value = Rand_value;
    s.state_i = state_i;
    memcpy(s.state, STATE, sizeof(s.state));
    s.z0 = z0; s.z1 = z1; s.z2 = z2;
    return s;
}

static void an_target_rng_check(const char *name, const an_target_rng_snapshot *saved)
{
    int unchanged = saved->quick == Rand_quick && saved->value == Rand_value
        && saved->state_i == state_i && saved->z0 == z0 && saved->z1 == z1 && saved->z2 == z2
        && memcmp(saved->state, STATE, sizeof(saved->state)) == 0;
    hb_target_trace_invariant(name, unchanged);
}

/*! `effect->index`（`list-effects.h` の `EF_*`）を、新 UI の形（射線・範囲の
 *  出し方）へ寄せる。対応の無い効果（自分中心の `EF_SPOT`・全方位の `EF_STAR`
 *  ほか——どれも「方向を選ばない」効果なので、そもそも `get_aim_dir_hook` を
 *  通らない）は `NATIVE_SHAPE_NONE` のまま返し、予測を出さない
 *  （新 UI 側は「この行動の射線・範囲表示は未対応」の扱いになるだけで、
 *  取消・確定そのものは T1 のまま成り立つ）。 */
static int an_target_shape_for_effect(int index)
{
    switch (index) {
    case EF_BOLT:
    case EF_BOLT_STATUS:
    case EF_BOLT_STATUS_DAM:
    case EF_BOLT_AWARE:
    case EF_BOLT_OR_BEAM: /* 実際の命中判定は BOLT/BEAM を実行時に乱数で選ぶが、
                            * 予測は控えめに BOLT（最初の 1 体で止まる）として出す。 */
        return NATIVE_SHAPE_BOLT;
    case EF_BEAM:
    case EF_LINE:
        return NATIVE_SHAPE_BEAM;
    case EF_BALL:
    case EF_STRIKE:
    case EF_SPHERE:
        return NATIVE_SHAPE_BALL;
    default:
        return NATIVE_SHAPE_NONE;
    }
}

/*! いまの命令（A3）が使っているアイテムから予測の元を作る。写しは `an_note_target_command()` が取っておく
 *  （`object_flavor_is_aware()` が偽の道具は `hb_target_profile_known(0)` のまま——
 *  効果を明かすと識別前のネタバレになる。`cmd-obj.c` の `use_aux()` 自身が `was_aware` で同じ区別をしている）。
 *  写しは 1 回の `get_aim_dir()` で使い切る（次の `cmd_get_target()` までの古い写しを使わない）。 */
static void an_target_register_profile(void)
{
    int i;
    int any = an_target_snap_count > 0;
    int known = an_target_snap_known;

    hb_target_profile_begin("angband-aim");
    for (i = 0; i < an_target_snap_count; i++) {
        hb_target_profile_effect(an_target_snap[i].shape, an_target_snap[i].subtype, an_target_snap[i].radius,
            z_info->max_range, 0, 0, 0, 0);
    }
    hb_target_profile_known(any && known);
    an_target_snap_count = 0;
    an_target_snap_known = 0;
}

/*! ボルト／ビームの射線を予測する。`project_aimed()`（`effect-handler-attack.c`）
 *  と同じ `PROJECT_THRU`（指定マスで止めない）に、ボルトだけ `PROJECT_STOP`
 *  （最初の敵で止まる）を足す——ビームは素通りする。 */
static void an_target_predict_bolt_beam(int shape, int range, struct loc origin, struct loc dest)
{
    struct loc path[64];
    int flg = PROJECT_THRU | (shape == NATIVE_SHAPE_BOLT ? PROJECT_STOP : 0);
    int n, i;

    if (range > 64) range = 64;
    n = project_path(cave, path, range, origin, dest, flg);
    hb_target_path_begin(NATIVE_PATH_REACHABLE);
    hb_target_path_cell(origin.x, origin.y);
    for (i = 0; i < n; ++i) {
        struct monster *mon;

        hb_target_path_cell(path[i].x, path[i].y);
        if (shape == NATIVE_SHAPE_BEAM || i == n - 1) {
            mon = square_monster(cave, path[i]);
            if (mon) hb_target_affected(mon->midx, path[i].x, path[i].y, 1);
        }
    }
}

/*! ボールの射線と範囲を予測する。`effect_handler_BALL()` と同じ
 *  `PROJECT_THRU|PROJECT_STOP` で中心（爆心）を決め、そこから半径ぶんを
 *  `los()`・`square_isprojectable()` で埋める（`silq/adapter/sq_targeting.inl`
 *  の `_sq_target_projection` と同じ考え方）。 */
static void an_target_predict_ball(int element, int radius, int range, struct loc origin, struct loc dest)
{
    struct loc path[64];
    int n, i, y, x;
    struct loc center;
    struct monster *mon;

    (void)element;
    if (range > 64) range = 64;
    n = project_path(cave, path, range, origin, dest, PROJECT_THRU | PROJECT_STOP);
    center = (n > 0) ? path[n - 1] : origin;

    hb_target_path_begin(NATIVE_PATH_REACHABLE);
    hb_target_path_cell(origin.x, origin.y);
    for (i = 0; i < n; ++i) hb_target_path_cell(path[i].x, path[i].y);

    hb_target_area_cell(center.x, center.y, 0);
    mon = square_monster(cave, center);
    if (mon) hb_target_affected(mon->midx, center.x, center.y, 1);

    for (y = center.y - radius; y <= center.y + radius; ++y) {
        for (x = center.x - radius; x <= center.x + radius; ++x) {
            struct loc grid = loc(x, y);

            if (x == center.x && y == center.y) continue;
            if (!square_in_bounds_fully(cave, grid)) continue;
            if (distance(center, grid) > radius) continue;
            if (!square_isprojectable(cave, grid)) continue;
            if (!los(cave, center, grid)) continue;
            hb_target_area_cell(x, y, 0);
            mon = square_monster(cave, grid);
            if (mon) hb_target_affected(mon->midx, x, y, 1);
        }
    }
}

/*! T1 は候補・妥当性判定のみ。T2 で、`hb_target_profile()` が予測を持っていれば
 *  （`an_target_register_profile()` が事前に登録したもの）射線・範囲も出す。
 *  予測が無い／効果不明のときは新 UI 側が「この行動の射線・範囲表示は未対応」
 *  の扱いにするだけで、取消・確定そのものは T1 のまま成り立つ。 */

static void an_target_candidates(void *data)
{
    struct point_set *targets;
    int i;
    an_target_rng_snapshot saved = an_target_rng_save();

    (void)data;
    targets = target_get_monsters(TARGET_KILL, NULL, false);
    for (i = 0; i < point_set_size(targets); ++i) {
        struct loc grid = targets->pts[i];
        struct monster *mon = square_monster(cave, grid);
        char name[160];
        int gain, ticks;

        if (!mon) continue;

        gain = turn_energy(mon->race->speed);
        if (gain <= 0) {
            ticks = 1000000000;
        } else if (mon->energy >= z_info->move_energy) {
            ticks = 0;
        } else {
            ticks = (z_info->move_energy - mon->energy + gain - 1) / gain;
        }

        monster_desc(name, sizeof(name), mon, 0);
        hb_target_candidate(mon->midx, grid.x, grid.y,
            distance(player->grid, grid), ticks, i, name, 1);
    }
    point_set_dispose(targets);
    an_target_rng_check("angband-candidates-rng", &saved);
}

static void an_target_preview(native_target_choice *choice, void *data)
{
    struct loc grid = loc(choice->x, choice->y);
    int valid = 1, reason = NATIVE_TARGET_OK, id = 0, dist;
    char name[160] = "";
    an_target_rng_snapshot saved = an_target_rng_save();

    (void)data;

    if (!square_in_bounds_fully(cave, grid)) {
        valid = 0;
        reason = NATIVE_TARGET_OUTSIDE;
    } else {
        struct monster *mon = square_monster(cave, grid);
        if (mon) {
            /*
             * AH-22 の直し。`monster_is_obvious()` を通さずに `id` を立てていたので、
             * `*` と狙いのプレビューが、まだ見えていない敵の存在をマスの反応で
             * 教えてしまっていた（手本は `an_look_describe_grid()` の同じ判定）。
             */
            if (monster_is_obvious(mon)) {
                id = mon->midx;
                monster_desc(name, sizeof(name), mon, 0);
            }
            if (!target_able(mon)) {
                valid = 0;
                reason = NATIVE_TARGET_UNAVAILABLE;
            }
        }
        if (loc_eq(grid, player->grid)) {
            valid = 0;
            reason = NATIVE_TARGET_SELF_FORBIDDEN;
        }
    }
    choice->id = id;
    choice->direction = 5;
    dist = distance(player->grid, grid);

    if (hb_target_preview_enabled() && valid) {
        const native_target_profile *profile = hb_target_profile();
        if (profile->supported && profile->effect_known) {
            int i;
            for (i = 0; i < profile->count; ++i) {
                const native_target_effect *effect = &profile->effects[i];
                if (effect->shape == NATIVE_SHAPE_BALL) {
                    an_target_predict_ball(effect->element, effect->radius, effect->range,
                        player->grid, grid);
                } else {
                    an_target_predict_bolt_beam(effect->shape, effect->range, player->grid, grid);
                }
            }
        }
    }

    an_target_emit_extras(choice);
    hb_target_status(valid, reason, dist, name, 0, 0);
    an_target_rng_check("angband-preview-rng", &saved);
}

static int an_target_read_key(void *data)
{
    struct keypress kp;

    (void)data;
    kp = inkey();
    /*
     * `hb_target_choose()`（`native_targeting_runtime.cpp`）は取消/確定の合図を
     * 素の ASCII（27 / 13 / ' '）で判じる——Sil-Q の古い `inkey()` が素の 1 byte を
     * 返していた頃の約束のまま（`sq_targeting.inl` の `_sq_target_read`）。
     * 原作 Angband の `keycode_t` は ESCAPE も Enter も「名前つきキー」へ
     * 逃がす（`ui-event.h`: `ESCAPE=0xE000`・`KC_ENTER=0x9c`。実機で確認——
     * ESC は `direction_key` 側へ回って `continue` され続け取消が一生効かず、
     * 方向キーの後に届く Enter も `13` ではなく `0x9c` だった）。両方を
     * 素の ASCII へ戻す。
     */
    if (kp.code == ESCAPE) return 27;
    if (kp.code == KC_ENTER) return 13;
    return (int)kp.code;
}

static int an_target_direction_key(int key, void *data)
{
    struct keypress kp;
    int dir;

    (void)data;
    memset(&kp, 0, sizeof(kp));
    kp.type = EVT_KBRD;
    kp.code = (keycode_t)key;
    dir = target_dir_allow(kp, false, true);
    if (dir < 1 || dir > 9) return 0;
    return dir;
}

static void an_target_refresh(void *data)
{
    (void)data;
    Term_fresh();
}

/*!
 * キーマップの続きが残っているか（`ui-input.c` の `inkey_next`）。
 * カーソルキーは原作の `pref.prf` のキーマップで「`;` ＋方向の数字」へ展開され、
 * 原作の `textui_get_rep_dir()` はその残りの数字を読んで歩く。ここで目標選択の画面を
 * 開くと、残りの数字が画面の操作に食われて**歩かずに目標選択が出る**（気づいたこと
 * 2026-09-24「キーボードのカーソル入力で移動にならずターゲットが起動した」）。
 * 続きがあるときは原作の処理に任せる（T1・T3 の両方）。
 */
static bool an_keymap_pending(void)
{
    return (inkey_next != NULL) && (inkey_next->code != 0);
}

static bool an_wrapped_get_aim_dir(int *dp)
{
    native_target_request request;
    native_target_choice choice;
    native_target_hooks hooks;
    memset(&hooks, 0, sizeof(hooks)); /* AH-54: 追加のフック（extra ほか）を空にする。 */

    memset(&request, 0, sizeof(request));
    memset(&choice, 0, sizeof(choice));
    hooks.candidates = an_target_candidates;
    hooks.preview = an_target_preview;
    hooks.read_key = an_target_read_key;
    hooks.direction_key = an_target_direction_key;
    hooks.refresh = an_target_refresh;
    hooks.recall = NULL; /* 「詳細」はモード "look" 専用（T4）。T1 の点選択では使わない。 */

    if (!hb_target_enabled() || an_keymap_pending()) {
        return an_orig_get_aim_dir ? an_orig_get_aim_dir(dp) : false;
    }

    an_target_register_profile(); /* 設計 §8.5 T2。A3 が控えた効果の写しから。 */

    request.width = cave->width;
    request.height = cave->height;
    request.source_x = player->grid.x;
    request.source_y = player->grid.y;
    request.mode = NATIVE_TARGET_POINT;
    request.kind = NATIVE_TARGET_ATTACK;
    request.label = "";
    if (target_okay()) {
        struct monster *mon = target_get_monster();
        struct loc grid;
        request.previous_valid = 1;
        if (mon) {
            request.previous_id = mon->midx;
            request.previous_x = mon->grid.x;
            request.previous_y = mon->grid.y;
        } else {
            target_get(&grid);
            request.previous_id = 0;
            request.previous_x = grid.x;
            request.previous_y = grid.y;
        }
    }

    if (!hb_target_choose(&request, &hooks, NULL, &choice)) {
        *dp = 0;
        return false;
    }

    /*
     * 原作の「方向」: 方向キーだけで動かしたカーソルが、モンスターのいないマスで、
     * プレイヤーから 8 方向のどれかの直線上にあるときは、原作の方向指定と同じに扱う。
     * 目標は変えず（`target_set_*` を呼ばない）、方向を原作へ返す。弾は目標で止まらず、
     * 射程の端（またはモンスターや壁）まで飛ぶ（effect-handler-general.c は
     * `dir == DIR_TARGET` のときだけ `PROJECT_STOP|PROJECT_THRU` を外す）。
     * クリックで選んだ点・候補の巡回・モンスターのマスは、いままでどおり目標にする。
     */
    if (choice.cursor_by_direction_key && choice.id <= 0) {
        int dx = choice.x - player->grid.x, dy = choice.y - player->grid.y;
        if ((dx || dy) && (dx == 0 || dy == 0 || abs(dx) == abs(dy))) {
            *dp = pathfind_direction_to(player->grid, loc(choice.x, choice.y));
            if (*dp >= 1 && *dp <= 9 && *dp != 5) return true;
        }
    }

    if (choice.track_creature && choice.id > 0) {
        struct monster *mon = square_monster(cave, loc(choice.x, choice.y));
        if (mon && mon->midx == choice.id) target_set_monster(mon);
        else target_set_location(choice.y, choice.x);
    } else {
        target_set_location(choice.y, choice.x);
    }
    *dp = 5; /* T1 の点選択は常に「現在の目標を使う」5 で返す（原作と同じ約束）。 */
    return true;
}

/*!
 * §8.5 T3: `get_rep_dir_hook`（掘る `T`・解除 `D` など、方向だけを選ぶ操作）を
 * `NATIVE_TARGET_DIRECTION8` で包む。候補・予測は無く（掘る/解除は対象が壁や
 * 扉のマスで、敵を選ぶ意味が無い）、選んだマスを `pathfind_direction_to()`
 * （`textui_get_rep_dir()` がマウスクリックの扱いに使っているのと同じ関数）で
 * 8 方向（+ 中心＝`DIR_NONE`）へ直す。
 */
static bool (*an_orig_get_rep_dir)(int *dir, bool allow_none) = NULL;

static void an_target_direction_candidates(void *data)
{
    (void)data; /* 方向だけの操作に候補は無い。 */
}

static void an_target_direction_preview(native_target_choice *choice, void *data)
{
    struct loc grid = loc(choice->x, choice->y);
    bool allow_none = data ? *(bool *)data : false;
    int valid = 1, reason = NATIVE_TARGET_OK, dir;

    if (!square_in_bounds_fully(cave, grid)) {
        valid = 0;
        reason = NATIVE_TARGET_OUTSIDE;
    }
    dir = pathfind_direction_to(player->grid, grid);
    if (dir == DIR_NONE && !allow_none) {
        valid = 0;
        reason = NATIVE_TARGET_SELF_FORBIDDEN;
    }
    choice->id = 0;
    choice->direction = dir;
    hb_target_status(valid, reason, distance(player->grid, grid), "", 0, 0);
}

static bool an_wrapped_get_rep_dir(int *dp, bool allow_none)
{
    native_target_request request;
    native_target_choice choice;
    native_target_hooks hooks;
    memset(&hooks, 0, sizeof(hooks)); /* AH-54: 追加のフック（extra ほか）を空にする。 */

    memset(&request, 0, sizeof(request));
    memset(&choice, 0, sizeof(choice));
    hooks.candidates = an_target_direction_candidates;
    hooks.preview = an_target_direction_preview;
    hooks.read_key = an_target_read_key;
    hooks.direction_key = an_target_direction_key;
    hooks.refresh = an_target_refresh;
    hooks.recall = NULL;

    if (!hb_target_enabled() || an_keymap_pending()) {
        return an_orig_get_rep_dir ? an_orig_get_rep_dir(dp, allow_none) : false;
    }

    request.width = cave->width;
    request.height = cave->height;
    request.source_x = player->grid.x;
    request.source_y = player->grid.y;
    request.mode = NATIVE_TARGET_DIRECTION8;
    request.kind = NATIVE_TARGET_NEUTRAL;
    request.label = "";

    if (!hb_target_choose(&request, &hooks, &allow_none, &choice)) {
        *dp = 0;
        return false;
    }
    *dp = choice.direction;
    return true;
}

/*!
 * §8.5 T4・§8.5.1: `target_set_interactive()`（`ui-target.c`）を丸ごと乗せ替える。
 * 原作の 2 つの呼び出し口——`textui_target()`（`*`）と `do_cmd_look()`（`l`）——は
 * どちらもこの 1 関数だけを通るので（ほかに `cmd-wizard.c`・`ui-context.c`・
 * `ui-knowledge.c` の一覧の「詳細」も同じ関数を呼ぶ）、A4（`ui-target.c` へ足す
 * 1 関数のフック）1 か所で両方を新 UI へ寄せられる。
 *
 * `mode` の `TARGET_LOOK` ビットで「見る」か「目標を決める」かを分ける
 * （`target.h`）。「見る」は §8.5.1 の決定により**常に「見るのを終える」だけ**
 * （目標は置かない・「Target Selected.」は出さない）。
 */

/*! 1 マスぶんの種別 (`kind`) と 1 行の説明 (`detail`) を、原作
 *  `target_accept()`（`target.c`）と同じ優先順位（モンスター→罠→床のアイテム→
 *  地形）で決める。候補の列挙（`an_look_candidates`）・現在位置の判定
 *  （`an_look_preview`）・「詳細」（`an_look_recall`）の 3 か所から呼ぶ。 */
static void an_look_describe_grid(struct loc grid, int *kind_out, char *detail, size_t detail_size)
{
    struct monster *mon = square_monster(cave, grid);

    *kind_out = NATIVE_CANDIDATE_NONE;
    detail[0] = '\0';

    if (mon && monster_is_obvious(mon)) {
        char name[160], health[160];
        monster_desc(name, sizeof(name), mon, MDESC_IND_VIS);
        look_mon_desc(health, 80, mon->midx); /* 英語の長さの上限は原作のまま（80）。日本語は下で 160 バイトまで広げる。 */
        /* AH-59: 日本語では状態（`unhurt, asleep` ほか）を句ごとに訳す（「無傷、睡眠中」）。括弧の形は
         * アイテムの名（`ダガー (1d4)`）と同じ。英語では `an_look_health()` は何もしない。 */
        an_look_health(health, sizeof(health));
        strnfmt(detail, detail_size, "%s (%s)", name, health);
        *kind_out = NATIVE_CANDIDATE_MONSTER;
        return;
    }

    if (square_isvisibletrap(player->cave, grid)) {
        struct trap *trap = square(player->cave, grid)->trap;
        strnfmt(detail, detail_size, "%s%s",
            is_a_vowel(trap->kind->desc[0]) ? "an " : "a ", trap->kind->desc);
        if (an_lang_enabled()) {
            /* AH-59: 冠詞の無い罠の名（名前の表 `trap`。鍵は `trap.txt` の `name:` の 2 つ目の欄＝`desc`）。 */
            char ja[80];
            my_strcpy(ja, trap->kind->desc, sizeof(ja));
            if (an_name_to("trap", ja, sizeof(ja))) {
                my_strcpy(detail, ja, detail_size);
            }
        }
        *kind_out = NATIVE_CANDIDATE_TRAP;
        return;
    }

    {
        /* §T4 実機確認で見つかった不具合: `target_accept()`（このマスが
         * 「興味を引くか」の判定。上の候補の列挙が使う）は
         * `obj->kind == unknown_item_kind`（正体を感知しただけでまだ実物を
         * 見ていない物）も「興味を引く」に数えるが、`object_desc()` は
         * そういう物には中身が無く `"(nothing)"` を返す（`obj-desc.c` の
         * `if (!obj || !obj->known) return "(nothing)";`）。原作の
         * `aux_object()` が実際に使う `scan_distant_floor()`
         * （`obj-pile.c`）は `unknown_item_kind` を最初から数えない——
         * ここも同じにする。数えなかった結果 0 個なら、このマスの
         * 「興味を引く」理由は罠でも地形でもなかったことになり、下の
         * 地形の判定へそのまま流れる（原作が地形の判定へ流れるのと同じ）。 */
        struct object *obj, *first = NULL;
        int count = 0;

        for (obj = square_object(player->cave, grid); obj; obj = obj->next) {
            if (obj->kind == unknown_item_kind) continue;
            if (ignore_known_item_ok(player, obj)) continue;
            if (!first) first = obj;
            ++count;
        }
        if (count == 1) {
            /* 実機確認で見つかった 2 つ目の不具合: `first` は `player->cave`
             * （プレイヤーの記憶）側の写しで、`object_desc()` が求める `known`
             * を持たない（`"(nothing)"` になる）。原作の
             * `target_recall_loop_object()`（`ui-target.c`）が実際に説明へ
             * 渡しているのは `cave->objects[obj->oidx]`（本物のゲーム状態側の
             * 実体）である。同じにする。 */
            object_desc(detail, detail_size, cave->objects[first->oidx], ODESC_PREFIX | ODESC_FULL, player);
            *kind_out = NATIVE_CANDIDATE_OBJECT;
            return;
        } else if (count > 1) {
            strnfmt(detail, detail_size, "a pile of %d objects", count);
            *kind_out = NATIVE_CANDIDATE_OBJECT;
            return;
        }
    }

    if (square_isknown(cave, grid) && square_isinteresting(player->cave, grid)) {
        const char *prefix = square_apparent_look_prefix(cave, grid);
        const char *name = square_apparent_name(cave, grid);

        strnfmt(detail, detail_size, "%s%s", prefix, name);
        /* AH-59: 日本語では冠詞を落とした地形の名（店の入口は「〜の入口」）。英語では何もしない。 */
        an_look_feature(detail, detail_size, prefix, name);
        *kind_out = NATIVE_CANDIDATE_FEATURE;
    }
}

/*! 原作 `target_get_monsters(TARGET_LOOK, NULL, false)`（§4.5 と同じ全マス走査。
 *  `target_accept()` で「興味を引くマス」だけに絞り、距離順に並んでいる）を
 *  そのまま使い、各マスを `an_look_describe_grid()` で種別・説明づけする。 */
static void an_look_candidates(void *data)
{
    struct point_set *targets;
    int i, synthetic = 0;

    (void)data;
    targets = target_get_monsters(TARGET_LOOK, NULL, false);
    for (i = 0; i < point_set_size(targets); ++i) {
        struct loc grid = targets->pts[i];
        int kind;
        char detail[200];
        int id;

        an_look_describe_grid(grid, &kind, detail, sizeof(detail));
        if (kind == NATIVE_CANDIDATE_NONE) continue; /* target_accept() と食い違うことは無いはずだが念のため。 */

        if (kind == NATIVE_CANDIDATE_MONSTER) {
            struct monster *mon = square_monster(cave, grid);
            id = mon ? mon->midx : 0;
        } else {
            /* モンスターの id（正）・自分（-1）と衝突しない合成 id。値そのものに
             * 意味は無く、この候補列挙の中で一意でありさえすればよい。 */
            id = -(1000 + (++synthetic));
        }
        if (!id) continue;

        hb_target_candidate_look(id, grid.x, grid.y, distance(player->grid, grid), 0, i,
            detail, 1, kind, detail);
    }
    point_set_dispose(targets);
}

static void an_look_preview(native_target_choice *choice, void *data)
{
    struct loc grid = loc(choice->x, choice->y);
    int kind = NATIVE_CANDIDATE_NONE;
    char detail[200] = "";

    (void)data;

    if (square_in_bounds_fully(cave, grid)) {
        an_look_describe_grid(grid, &kind, detail, sizeof(detail));
        /* モンスターの上に自由移動したときだけ id を付け直す（`an_target_preview`
         * と同じ流儀）。それ以外は呼び出し側の id をそのまま保つ——`Cycle` が
         * 渡した候補固有の合成 id を、この後の `Cycle` の巡回判定
         * （`native_targeting_runtime.cpp` の `hb_target_choose()`）が
         * 引き続き見つけられるようにするため。 */
        if (kind == NATIVE_CANDIDATE_MONSTER) {
            struct monster *mon = square_monster(cave, grid);
            choice->id = mon ? mon->midx : 0;
        }
    }
    choice->direction = 5;
    an_target_emit_extras(choice);
    /* 「見る」は常に確定でき、確定は「見るのを終える」だけを意味する
     * （§8.5.1）。`detail`（1 行の説明）は `hb_target_status()` の `name` 引数
     * に載せ、新 UI の目標選択の欄にそのまま出す（`hd2d/ui/targeting_ui.cpp`）。 */
    hb_target_status(1, NATIVE_TARGET_OK, distance(player->grid, grid), detail, 0, 1);
}

/*! `textblock` の中身をメッセージへ流す。改行で切り、空行は送らない
 *  （決めたこと 2026-09-23「文書の表示へは切り替えない……メッセージ
 *  （frame.messages）として送る」）。 */
static void an_look_emit_textblock(textblock *tb)
{
    const wchar_t *wide = textblock_text(tb);
    size_t wlen = wcslen(wide);
    size_t cap = wlen * 4 + 4; /* UTF-8 最悪 4 byte/文字（実際はほぼ ASCII）。 */
    char *buf = mem_alloc(cap);
    char *line;
    size_t i, out = 0;

    if (!buf) return;
    /*
     * AH-21 の直し。CRT の `wcstombs()` は C ロケールの多バイト変換に落ちるので、
     * `é` などの非 ASCII が化けたり、変換そのものが失敗して詳細がまるごと消えたり
     * していた（`an_text_hooks.c` が差している `text_wctomb` フックを素通りして
     * いたため）。1 文字ずつ `text_wctomb()`（`an_install_text_hooks()` が
     * `WideCharToMultiByte(CP_UTF8, ...)` を差す）で UTF-8 へ変換する。
     */
    for (i = 0; i < wlen; ++i) {
        int n;

        /* AH-59: 日本語では説明文の全角の直後に埋め字（設計 §3）が積んである。Term の 2 セルを作る印なので
         * 文字ではない——落とさないと、メッセージに私用領域の字（U+F8FF）が 1 字おきに混ざって出た。 */
        if (wide[i] == AN_WIDE_PAD) continue;
        n = text_wctomb(buf + out, wide[i]);
        if (n <= 0) continue; /* 変換できない 1 字は落として続ける（全滅よりまし）。 */
        out += (size_t)n;
    }
    buf[out] = '\0';

    line = buf;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (*line) msg("%s", line);
        line = nl ? nl + 1 : NULL;
    }
    mem_free(buf);
}

/*! `ui-mon-lore.c` の `lore_description()` は `lore_show_interactive()` が
 *  Term へ描く前に呼ぶ組み立て関数——原作のどのヘッダにも宣言が無い
 *  （呼び手が同じファイル内の `lore_show_interactive()` だけだったため）。
 *  `static` ではないので、ここで前方宣言して直接使う。 */
extern void lore_description(textblock *tb, const struct monster_race *race,
    const struct monster_lore *lore, bool spoilers);

/*! 「詳細」（`aim_recall`）。原作の画面は一切開かない——伝承・説明の
 *  組み立て（`lore_description()` / `object_info()`。どちらも Term に描く前の
 *  `textblock` を返す関数）から文だけを取り、メッセージへ流す。目標選択の
 *  画面は開いたまま（`native_targeting_runtime.cpp` の `hb_target_choose()` が
 *  `Recall` を確定・取消と別に扱い、ループを終えない）。 */
static void an_look_recall(native_target_choice *choice, void *data)
{
    struct loc grid = loc(choice->x, choice->y);
    int kind;
    char detail[200];
    /*
     * AH-03 の直し。伝承・説明の複数行を `msg()` で流すと、原作の
     * メッセージ行が埋まった所で `msg_flush()`（`ui-input.c`）が
     * `-more-` を出して `anykey()` で本物のキー入力を待ってしまう
     * （`OPT(player, auto_more)` が偽のとき）。この標準入出力の新 UI には
     * 「-more- を出している」を伝える手立てが無く、次のキーが遅れて効く・
     * 拒まれる形で現れていた。「詳細」を流している間だけ `auto_more` を
     * 真にして、終わったら元へ戻す。
     */
    const bool old_auto_more = OPT(player, auto_more);

    (void)data;
    if (!square_in_bounds_fully(cave, grid)) return;
    an_look_describe_grid(grid, &kind, detail, sizeof(detail));

    player->opts.opt[OPT_auto_more] = true;

    if (kind == NATIVE_CANDIDATE_MONSTER) {
        struct monster *mon = square_monster(cave, grid);
        if (mon) {
            const struct monster_lore *lore = get_lore(mon->race);
            textblock *tb = textblock_new();
            lore_description(tb, mon->race, lore, false);
            an_look_emit_textblock(tb);
            textblock_free(tb);
        }
        player->opts.opt[OPT_auto_more] = old_auto_more;
        return;
    }
    if (kind == NATIVE_CANDIDATE_OBJECT) {
        /* an_look_describe_grid() と同じ理由（scan_distant_floor() に合わせる）。 */
        struct object *obj;
        for (obj = square_object(player->cave, grid); obj; obj = obj->next) {
            if (obj->kind == unknown_item_kind) continue;
            if (ignore_known_item_ok(player, obj)) continue;
            break;
        }
        if (obj) {
            /* 原作の `r`（詳細）は `display_object_recall_interactive(cave->objects[obj->oidx])`
             * （`ui-target.c`）——`an_look_describe_grid()` と同じく本物のゲーム状態側を渡す。 */
            textblock *tb = object_info(cave->objects[obj->oidx], OINFO_NONE);
            an_look_emit_textblock(tb);
            textblock_free(tb);
        }
        player->opts.opt[OPT_auto_more] = old_auto_more;
        return;
    }
    /* 罠・地形には原作にもそれ以上の伝承が無いので、1 行の説明をそのまま送る。 */
    if (*detail) msg("%s", detail);
    player->opts.opt[OPT_auto_more] = old_auto_more;
}

/*
 * AH-54: 見る（`l`）・狙う（`*`）の追加操作。原作 `target_set_interactive()`（`ui-target.c`）の
 * キー `<` `>`（最寄りの階段へカーソル）・`x`（最寄りの未知のマスへ）・`p`（自分へ）・`m`（最寄りの
 * 「興味を引くマス」へ）・`g`（そのマスへ歩く）・`k`（そこにあるアイテムを無視に設定）を、新 UI の
 * 「操作」の一覧（と原作と同じキー）から使えるようにする。`o`（自由移動へ切り替え）は、この選択器の
 * カーソルが最初から自由に動くので何もすることが無い。
 */
static int an_extras_mode = 0; /* 0: 出さない（狙う方向の問い・投げるなど）, 1: 見る, 2: 狙う */
static bool an_extras_pathfinding = false; /* `target_set_interactive()` の allow_pathfinding */
static bool an_extra_k_ok = false; /* カーソルの位置に無視できるアイテムがある */

/*!
 * 「無視する」のキー。原作 `target_set_interactive()` と同じく、いまのキー配置で引く
 * （標準は `k`、ローグライクは ^D。ローグライクの `k` は上への移動。2026-10-07）。
 */
static int an_ignore_key(void)
{
    return cmd_lookup_key(CMD_IGNORE, OPT(player, rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG);
}

/*! カーソルのマスの、原作 `aux_object()` が最初に拾うアイテム（無ければ NULL）。 */
static struct object *an_extra_floor_object(struct loc grid)
{
    struct object *items[1];

    if (!square_in_bounds_fully(cave, grid)) return NULL;
    if (scan_distant_floor(items, 1, player, grid) <= 0) return NULL;
    return items[0];
}

/*! プレビューのたびに呼び、いまのカーソルで使える追加操作を新 UI へ申告する。 */
static void an_target_emit_extras(const native_target_choice *choice)
{
    struct loc grid = loc(choice->x, choice->y);

    if (!an_extras_mode) return;
    hb_target_extra('<', an_tr("Nearest up staircase"), 1);
    hb_target_extra('>', an_tr("Nearest down staircase"), 1);
    hb_target_extra('x', an_tr("Nearest unexplored grid"), 1);
    hb_target_extra('p', an_tr("Cursor to yourself"), 1);
    hb_target_extra('m', an_tr("Nearest interesting grid"), 1);
    if (an_extras_pathfinding) {
        hb_target_extra('g', an_tr("Walk to the cursor"), square_in_bounds_fully(cave, grid) && !loc_eq(grid, player->grid));
    }
    if (an_extras_mode == 1) {
        an_extra_k_ok = an_extra_floor_object(grid) != NULL;
        hb_target_extra(an_ignore_key(), an_tr("Ignore the item here"), an_extra_k_ok);
    } else {
        an_extra_k_ok = false;
    }
}

static int an_extra_move_to(native_target_choice *choice, struct loc grid)
{
    choice->x = grid.x;
    choice->y = grid.y;
    choice->id = 0;
    choice->track_creature = 1;
    return NATIVE_EXTRA_MOVED;
}

static int an_extra_hook(int key, native_target_choice *choice, void *data)
{
    struct loc grid = loc(choice->x, choice->y);
    struct loc dest;

    (void)data;
    /* 「無視する」は下の `case 'k'` で受ける。ローグライクでは `k` は無視ではない（上への移動）。 */
    if (key == an_ignore_key()) {
        key = 'k';
    } else if (key == 'k') {
        return NATIVE_EXTRA_UNKNOWN;
    }
    switch (key) {
    case '<':
        if (path_nearest_known(player, grid, square_isupstairs, &dest, NULL) > 0) return an_extra_move_to(choice, dest);
        hb_target_notice(an_tr("There is no known up staircase."));
        return NATIVE_EXTRA_NONE;
    case '>':
        if (path_nearest_known(player, grid, square_isdownstairs, &dest, NULL) > 0) return an_extra_move_to(choice, dest);
        hb_target_notice(an_tr("There is no known down staircase."));
        return NATIVE_EXTRA_NONE;
    case 'x':
        if (path_nearest_unknown(player, grid, &dest, NULL) > 0) return an_extra_move_to(choice, dest);
        hb_target_notice(an_tr("There is no unexplored grid to go to."));
        return NATIVE_EXTRA_NONE;
    case 'p':
        return an_extra_move_to(choice, player->grid);
    case 'm': {
        struct point_set *targets = target_get_monsters((an_extras_mode == 1) ? TARGET_LOOK : TARGET_KILL, NULL, false);
        int i, best = -1, best_distance = 999;

        for (i = 0; i < point_set_size(targets); ++i) {
            int d = distance(grid, targets->pts[i]);
            if (d < best_distance) {
                best = i;
                best_distance = d;
            }
        }
        if (best >= 0) {
            dest = targets->pts[best];
            point_set_dispose(targets);
            return an_extra_move_to(choice, dest);
        }
        point_set_dispose(targets);
        hb_target_notice(an_tr("There is nothing of interest."));
        return NATIVE_EXTRA_NONE;
    }
    case 'g':
        if (!an_extras_pathfinding) return NATIVE_EXTRA_UNKNOWN;
        if (!square_in_bounds_fully(cave, grid) || loc_eq(grid, player->grid)) {
            hb_target_notice(an_tr("Choose a grid other than your own."));
            return NATIVE_EXTRA_NONE;
        }
        cmdq_push(CMD_PATHFIND);
        cmd_set_arg_point(cmdq_peek(), "point", grid);
        return NATIVE_EXTRA_END;
    case 'k': {
        struct object *obj;

        if (an_extras_mode != 1) return NATIVE_EXTRA_UNKNOWN;
        obj = an_extra_floor_object(grid);
        if (!obj) {
            hb_target_notice(an_tr("There is no item to ignore here."));
            return NATIVE_EXTRA_NONE;
        }
        /* 原作は `aux_object()` が追跡したアイテムを `textui_cmd_ignore_menu()` へ渡す。 */
        track_object(player->upkeep, obj);
        handle_stuff(player);
        textui_cmd_ignore_menu(player->upkeep->object);
        handle_stuff(player);
        return NATIVE_EXTRA_MOVED;
    }
    default:
        return NATIVE_EXTRA_UNKNOWN;
    }
}

/*! 画面（メニュー）を開く操作か。開く間は新 UI の目標選択を閉じる（メニューが隠れないように）。 */
static int an_extra_screen_hook(int key)
{
    return (key == an_ignore_key()) && (an_extras_mode == 1) && an_extra_k_ok;
}

/*!
 * `x`・`y` は原作 `target_set_interactive()` の引数のとおり: 負なら「最初の候補から」、
 * 0 以上なら「そのマスから、カーソルを自由に動かす形で」始める（AH-57。マスの右クリックの
 * 「見る」・Alt+クリックが、クリックしたマスを `TARGET_LOOK` の起点として渡す。`ui-context.c`）。
 */
static bool an_wrapped_look(int x, int y)
{
    native_target_request request;
    native_target_choice choice;
    native_target_hooks hooks;
    memset(&hooks, 0, sizeof(hooks)); /* AH-54: 追加のフック（extra ほか）を空にする。 */

    memset(&request, 0, sizeof(request));
    memset(&choice, 0, sizeof(choice));
    hooks.candidates = an_look_candidates;
    hooks.preview = an_look_preview;
    hooks.read_key = an_target_read_key;
    hooks.direction_key = an_target_direction_key;
    hooks.refresh = an_target_refresh;
    hooks.recall = an_look_recall;
    hooks.extra = an_extra_hook;
    hooks.extra_screen = an_extra_screen_hook;
    an_extras_mode = 1;

    request.width = cave->width;
    request.height = cave->height;
    request.source_x = player->grid.x;
    request.source_y = player->grid.y;
    request.mode = NATIVE_TARGET_LOOK;
    request.kind = NATIVE_TARGET_NEUTRAL;
    request.label = "";
    if ((x >= 0) && (y >= 0) && square_in_bounds_fully(cave, loc(x, y))) {
        /* AH-57: 呼び手が指したマスから始める（`native_targeting_runtime.cpp` の `previous_*`）。 */
        struct monster *mon = square_monster(cave, loc(x, y));

        request.previous_valid = 1;
        request.previous_x = x;
        request.previous_y = y;
        request.previous_id = (mon && monster_is_obvious(mon)) ? mon->midx : 0;
    }

    target_set_monster(0); /* 原作 target_set_interactive() 冒頭と同じ「古い目標を消す」。 */
    (void)hb_target_choose(&request, &hooks, NULL, &choice);
    an_extras_mode = 0;
    return false; /* §8.5.1: 「見る」は確定しても目標を置かない。 */
}

/*! `*`（目標を決める）。候補・妥当性判定は T1（`an_wrapped_get_aim_dir`）と
 *  同じ `an_target_candidates` / `an_target_preview` を再利用する——違うのは
 *  「方向」を返すのではなく、確定した目標をここで直接
 *  `target_set_monster()` / `target_set_location()` へ置くこと。 */
static bool an_wrapped_target_command(void)
{
    native_target_request request;
    native_target_choice choice;
    native_target_hooks hooks;
    memset(&hooks, 0, sizeof(hooks)); /* AH-54: 追加のフック（extra ほか）を空にする。 */

    memset(&request, 0, sizeof(request));
    memset(&choice, 0, sizeof(choice));
    hooks.candidates = an_target_candidates;
    hooks.preview = an_target_preview;
    hooks.read_key = an_target_read_key;
    hooks.direction_key = an_target_direction_key;
    hooks.refresh = an_target_refresh;
    hooks.recall = NULL; /* 「詳細」は 'l' のモード "look" 専用。 */
    hooks.extra = an_extra_hook;
    hooks.extra_screen = an_extra_screen_hook;
    an_extras_mode = 2;

    request.width = cave->width;
    request.height = cave->height;
    request.source_x = player->grid.x;
    request.source_y = player->grid.y;
    request.mode = NATIVE_TARGET_POINT;
    request.kind = NATIVE_TARGET_ATTACK;
    request.label = "";
    if (target_okay()) {
        struct monster *mon = target_get_monster();
        struct loc grid;
        request.previous_valid = 1;
        if (mon) {
            request.previous_id = mon->midx;
            request.previous_x = mon->grid.x;
            request.previous_y = mon->grid.y;
        } else {
            target_get(&grid);
            request.previous_id = 0;
            request.previous_x = grid.x;
            request.previous_y = grid.y;
        }
    }

    target_set_monster(0);

    if (!hb_target_choose(&request, &hooks, NULL, &choice)) {
        an_extras_mode = 0;
        if (!choice.ended_by_extra) msg("Target Aborted."); /* 'g'（歩いて行く）で終えたときは中断ではない。 */
        return false;
    }
    an_extras_mode = 0;
    if (choice.track_creature && choice.id > 0) {
        struct monster *mon = square_monster(cave, loc(choice.x, choice.y));
        if (mon && mon->midx == choice.id) target_set_monster(mon);
        else target_set_location(choice.y, choice.x);
    } else {
        target_set_location(choice.y, choice.x);
    }
    msg("Target Selected.");
    return true;
}

bool an_target_set_interactive_hook(int mode, int x, int y, bool allow_pathfinding, bool *result)
{
    an_extras_pathfinding = allow_pathfinding;
    if (!hb_target_enabled()) return false; /* 原作のまま（A4 の弱いデフォルトと同じ約束）。 */

    if (mode & TARGET_LOOK) *result = an_wrapped_look(x, y);
    else if (mode & TARGET_KILL) *result = an_wrapped_target_command();
    else return false; /* 未知の呼び出し方。原作のまま続けさせる。 */
    return true;
}

void an_install_targeting_hooks(void)
{
    an_orig_get_aim_dir = get_aim_dir_hook;
    get_aim_dir_hook = an_wrapped_get_aim_dir;

    an_orig_get_rep_dir = get_rep_dir_hook;
    get_rep_dir_hook = an_wrapped_get_rep_dir;
}
