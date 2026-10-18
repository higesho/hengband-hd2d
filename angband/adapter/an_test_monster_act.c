/*!
 * @file an_test_monster_act.c
 * @brief 検査用：敵の行動の指定（環境変数 `HD2D_TEST_MONSTER_ACT`）。
 *
 * 攻撃の効果の検査で、敵が魔法を撃つまで休んで待たずに済ませ、かつ検査したい行動以外の
 * 敵の動きを止めるための入口。**環境変数 `HD2D_TEST_MONSTER_ACT`（命令ファイルのパス）が無いときは
 * 何もしない**（普段の遊び・配布物には効かない。`hold` の登録も命令が来るまでしない）。
 *
 * 呼び出しは `an_null_term.c` の「キーを待つ間の繰り返し」の頭（キーを Term へ積む直前）から。
 * 最上位のコマンドを待っているとき（`an_awaiting_command()`）だけ読む。-more- や選択画面の最中は読まない。
 * ファイルがあれば**先に消して**（消せなければ名前を変える）から、上の行から順に 1 回の命令待ちの中で全部実行する
 * （その間ゲームの時間は進めない）。空行と `#` で始まる行は読み飛ばす。結果は標準エラーへ 1 行ずつ
 *   `[test-monster-act] ok <行>`   ／   `[test-monster-act] fail <理由>`   ／   `[test-monster-act] skip <行>`
 * 途中で fail が出たら残りの行は実行せず、各行に skip を出す。
 *
 * 命令（敵の指定 `<sel>` は `nearest`（見える敵でいちばん近い）か敵の通し番号 `m_idx`。
 * フレームの `monster_slot` と同じ番号。`hold` だけ `all` も取れる）:
 *   cast <魔法> [<sel>]      … その敵に、その魔法をプレイヤーへ向けていま撃たせる（魔法の名前は `list-mon-spells.h` の名前）
 *   melee [<sel>]            … その敵に、いまプレイヤーへ直接攻撃させる（隣にいるときだけ）
 *   place <sel> <dx> <dy>    … その敵を、プレイヤーの位置から (dx, dy) ずれたマスへ動かす（時間は使わない）
 *   hold <sel|all> on|off    … on の間、その敵は自分の番に行動しない（入口から命じた cast / melee / place は働く）
 *
 * ## cast / melee
 * 原作の `do_mon_spell(index, mon, seen)`（`make_ranged_attack()` が選んだ魔法を撃つときに呼ぶ関数）と
 * `make_attack_normal(mon, player)` を直に呼ぶ。原作の旗・確率・種族の魔法の表は書き換えない。
 * 番の外から呼ぶので整える前提:
 *   - `cave->mon_current`（いまの番の敵。魔法の効果が「撃った者」を引く）を撃たせる敵にし、終わったら元へ戻す
 *   - `mon->target.midx`（敵が狙う相手。0 = プレイヤー）を一時的に 0 にし、終わったら元へ戻す
 *   - `inkey_flag`（コマンド待ちの旗。メッセージの -more- が入れ子の `inkey()` を呼ぶと下ろされるので、終わったら戻す）
 *   - 終わったら `notice_stuff()`・`handle_stuff()`・`Term_fresh()`（番の終わりと同じ再描画の要求の処理）
 * `make_ranged_attack()` が呼ぶ前にする「魔法の失敗の判定」「遠隔攻撃の頻度の判定」は通らない（必ず撃つ）。
 *
 * ## place
 * 原作が敵を動かすときと同じ `monster_swap(元のマス, 先のマス)`（`monster_turn()` が使う関数。表示・視界・
 * モンスター一覧の更新も中でする）を呼ぶ。先のマスが地図の外・プレイヤーのマス・ほかの敵がいるマス・
 * その敵が立てない地形（通れない地形。壁を通れる敵でも永久岩は不可）なら fail。
 *
 * ## hold（原作の「保持」状態も眠りも使わない）
 * 敵の番は `process_monsters()` が「`mon->energy` が `z_info->move_energy` 以上か」で決める（エネルギーは 8 ビットで、
 * 番が来るたびにゲーム 1 ターンごとに速度ぶん足される）。そこで**ゲーム 1 ターンごとに通る `EVENT_REFRESH`**
 * （`run_game_loop()` が `process_monsters(0)` の直前と直後に出す）の受け手で、止めている敵の `energy` を 0 に置き直す。
 * 1 回の `process_monsters()` で足されるのは速度ぶん（最大でも 100 未満）だけなので、番に必要な量に届かない。
 * 止める前の `energy` は覚えておき、off で戻す。**命令待ちの間隔に頼らない**ので、プレイヤーが休む・走る・
 * 数ターン続けて動いても止まったままである。副作用は他の状態（眠り・保持・恐怖）を変えないこと：画面の表示は変わらない。
 *   - 止めている敵が死ぬ・別の敵に入れ替わる、階を移る（`EVENT_NEW_LEVEL_DISPLAY`）と、その記録は外す。
 *   - `hold all on` はそのとき以降に現れる敵も止める。`hold <sel> off` は `all on` の最中でもその 1 体だけ解く。
 *     `hold all off` はすべての止めを解く。
 *
 * **乱数と乱数の種には触らない**（魔法の内側の通常の乱数の消費だけ）。
 */
#include "angband.h"

#include "cave.h" /* distance()・square_isview() */
#include "init.h" /* z_info */
#include "player-timed.h" /* TMD_BLIND */
#include "player-calcs.h" /* notice_stuff()・handle_stuff() */
#include "player-util.h" /* disturb() */
#include "game-event.h" /* event_add_handler() */
#include "game-world.h" /* character_generated */
#include "mon-attack.h" /* make_attack_normal() */
#include "mon-predicate.h" /* monster_is_visible()・monster_passes_walls() */
#include "mon-spell.h" /* do_mon_spell()・RSF_* */
#include "mon-util.h" /* monster_swap() */
#include "ui-input.h" /* inkey_flag */
#include "ui-term.h" /* Term_fresh() */

#include "an_shim.h" /* an_awaiting_command() ほかの宣言 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AN_TMA_MAX_LINES 64
#define AN_TMA_LINE_LEN 256
#define AN_TMA_FILE_LEN 16384
#define AN_TMA_MAX_TOKENS 6

#define AN_TMA_FREE 0   /* 記録なし（`all` の指定に従う） */
#define AN_TMA_HELD 1   /* この敵を止める */
#define AN_TMA_LOOSE 2  /* `all on` の最中でもこの敵は止めない */

static int an_tma_busy = 0;
static int an_tma_stuck = 0;

/* ------------------------------------------------------------------ 止める敵の記録（`hold`） */

static int an_tma_all = 0;                          /* `hold all on` の最中 */
static int an_tma_hooked = 0;                       /* `EVENT_REFRESH` などの受け手を登録した */
static int an_tma_slots = 0;                        /* 下の配列の長さ（`z_info->level_monster_max`） */
static unsigned char *an_tma_state = NULL;          /* AN_TMA_FREE / HELD / LOOSE */
static unsigned char *an_tma_saved = NULL;          /* 止める前の energy */
static unsigned char *an_tma_saved_ok = NULL;       /* an_tma_saved が入っている */
static const struct monster_race **an_tma_race = NULL; /* 記録したときの種族（入れ替わりの検出） */

static const struct monster_race *an_tma_base_race(const struct monster *mon)
{
    return mon->original_race ? mon->original_race : mon->race;
}

static void an_tma_forget_slot(int i)
{
    an_tma_state[i] = AN_TMA_FREE;
    an_tma_saved_ok[i] = 0;
    an_tma_race[i] = NULL;
}

/* 記録が外れたことを標準エラーへ知らせる（検査が「死んだ・入れ替わった」を確かめるための `info` 行） */
static void an_tma_drop_slot(int i, const char *why)
{
    if ((an_tma_state[i] != AN_TMA_FREE) || an_tma_saved_ok[i]) {
        fprintf(stderr, "[test-monster-act] info hold-dropped %d %s\n", i, why);
        fflush(stderr);
    }
    an_tma_forget_slot(i);
}

static void an_tma_forget_all(void)
{
    int i;
    int n = 0;
    for (i = 0; i < an_tma_slots; i++) {
        if ((an_tma_state[i] != AN_TMA_FREE) || an_tma_saved_ok[i]) {
            n++;
        }
        an_tma_forget_slot(i);
    }
    if (n > 0) {
        fprintf(stderr, "[test-monster-act] info hold-dropped-all new-level %d\n", n);
        fflush(stderr);
    }
}

/* 止めている敵の energy を 0 に置き直す（ゲーム 1 ターンごと・hold の命令の直後に呼ぶ） */
static void an_tma_apply(void)
{
    int i;
    int top;

    if (!an_tma_state || !cave) {
        return;
    }
    top = cave_monster_max(cave);
    if (top > an_tma_slots) {
        top = an_tma_slots;
    }
    for (i = 1; i < top; i++) {
        struct monster *mon = cave_monster(cave, i);
        int held;

        if (!mon || !mon->race) {
            an_tma_drop_slot(i, "gone");   /* 死んだ・消えた */
            continue;
        }
        if (an_tma_race[i] && (an_tma_race[i] != an_tma_base_race(mon))) {
            an_tma_drop_slot(i, "replaced");   /* 別の敵が同じ番号を使っている */
        }
        held = (an_tma_state[i] == AN_TMA_HELD) || (an_tma_all && (an_tma_state[i] != AN_TMA_LOOSE));
        if (!held) {
            continue;
        }
        if (!an_tma_saved_ok[i]) {
            an_tma_saved[i] = mon->energy;
            an_tma_saved_ok[i] = 1;
            an_tma_race[i] = an_tma_base_race(mon);
        }
        mon->energy = 0;
    }
}

/* 止めを解いた敵の energy を、止める前の値へ戻す */
static void an_tma_release(int i)
{
    struct monster *mon;

    if (i < 1 || i >= an_tma_slots || !cave || i >= cave_monster_max(cave)) {
        return;
    }
    mon = cave_monster(cave, i);
    if (an_tma_saved_ok[i] && mon && mon->race && (an_tma_race[i] == an_tma_base_race(mon))) {
        mon->energy = an_tma_saved[i];
    }
    an_tma_saved_ok[i] = 0;
}

static void an_tma_on_refresh(game_event_type type, game_event_data *data, void *user)
{
    (void)type;
    (void)data;
    (void)user;
    if (an_tma_hooked) {
        an_tma_apply();
    }
}

static void an_tma_on_new_level(game_event_type type, game_event_data *data, void *user)
{
    (void)type;
    (void)data;
    (void)user;
    if (an_tma_state) {
        an_tma_forget_all();   /* 階を移ると敵の番号は別の敵のものになる */
    }
}

/* 最初の `hold` の命令で、記録の置き場と受け手を用意する（環境変数が無ければここまで来ない） */
static int an_tma_hold_prepare(void)
{
    if (an_tma_hooked) {
        return 1;
    }
    an_tma_slots = z_info->level_monster_max;
    if (an_tma_slots < 2) {
        return 0;
    }
    an_tma_state = (unsigned char *)calloc((size_t)an_tma_slots, 1);
    an_tma_saved = (unsigned char *)calloc((size_t)an_tma_slots, 1);
    an_tma_saved_ok = (unsigned char *)calloc((size_t)an_tma_slots, 1);
    an_tma_race = (const struct monster_race **)calloc((size_t)an_tma_slots, sizeof(*an_tma_race));
    if (!an_tma_state || !an_tma_saved || !an_tma_saved_ok || !an_tma_race) {
        return 0;
    }
    event_add_handler(EVENT_REFRESH, an_tma_on_refresh, NULL);
    event_add_handler(EVENT_NEW_LEVEL_DISPLAY, an_tma_on_new_level, NULL);
    an_tma_hooked = 1;
    return 1;
}

/* ------------------------------------------------------------------ 命令の実行 */

typedef struct an_tma_spell {
    const char *name;
    int index;
} an_tma_spell;

static const an_tma_spell an_tma_spells[] = {
#define RSF(a, b) { #a, RSF_##a },
#include "list-mon-spells.h"
#undef RSF
};

static void an_tma_report(const char *verdict, const char *text)
{
    fprintf(stderr, "[test-monster-act] %s %s\n", verdict, text);
    fflush(stderr);
}

/* 整数 1 つ（全体が数字であること） */
static int an_tma_parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v;

    if (!s || !s[0]) {
        return 0;
    }
    v = strtol(s, &end, 10);
    if (!end || (*end != '\0') || (v < -1000) || (v > 1000)) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* 敵の指定（nearest・省略・番号）。番号は敵の通し番号（`midx`。フレームの monster_slot と同じ）。 */
static struct monster *an_tma_pick(const char *arg)
{
    int i;
    struct monster *best = NULL;
    int best_dist = 0;

    if (arg && arg[0] && strcmp(arg, "nearest") != 0) {
        int n;
        struct monster *mon;
        if (!an_tma_parse_int(arg, &n) || (n < 1) || (n >= cave_monster_max(cave))) {
            return NULL;
        }
        mon = cave_monster(cave, n);
        return (mon && mon->race) ? mon : NULL;
    }

    for (i = 1; i < cave_monster_max(cave); i++) {
        struct monster *mon = cave_monster(cave, i);
        int d;

        if (!mon || !mon->race || !monster_is_visible(mon)) {
            continue;
        }
        d = distance(player->grid, mon->grid);
        if (!best || (d < best_dist)) {
            best = mon;
            best_dist = d;
        }
    }
    return best;
}

static const char *an_tma_do_cast(char **tok, int ntok)
{
    struct monster *mon;
    int index = 0;
    size_t k;
    bool saved_inkey_flag;
    int saved_current;
    int saved_target;

    if (ntok < 2) {
        return "cast needs a spell name";
    }
    for (k = 0; k < sizeof(an_tma_spells) / sizeof(an_tma_spells[0]); k++) {
        if ((an_tma_spells[k].index > RSF_NONE) && (an_tma_spells[k].index < RSF_MAX)
            && (strcmp(tok[1], an_tma_spells[k].name) == 0)) {
            index = an_tma_spells[k].index;
            break;
        }
    }
    if (!index) {
        return "unknown spell name";
    }
    mon = an_tma_pick((ntok >= 3) ? tok[2] : NULL);
    if (!mon) {
        return "no such monster";
    }
    if (distance(player->grid, mon->grid) > z_info->max_range) {
        return "the monster is out of range";
    }
    saved_inkey_flag = inkey_flag;
    saved_current = cave->mon_current;
    saved_target = mon->target.midx;
    cave->mon_current = mon->midx;
    mon->target.midx = 0;
    disturb(player);
    do_mon_spell(index, mon, (player->timed[TMD_BLIND] == 0) && monster_is_visible(mon));
    cave->mon_current = saved_current;
    if (mon->race) {
        mon->target.midx = saved_target;
    }
    inkey_flag = saved_inkey_flag;
    return NULL;
}

static const char *an_tma_do_melee(char **tok, int ntok)
{
    struct monster *mon;
    bool saved_inkey_flag;
    int saved_current;
    bool done;

    mon = an_tma_pick((ntok >= 2) ? tok[1] : NULL);
    if (!mon) {
        return "no such monster";
    }
    if (distance(player->grid, mon->grid) != 1) {
        return "the monster is not adjacent";
    }
    saved_inkey_flag = inkey_flag;
    saved_current = cave->mon_current;
    cave->mon_current = mon->midx;
    done = make_attack_normal(mon, player);
    cave->mon_current = saved_current;
    inkey_flag = saved_inkey_flag;
    if (!done) {
        return "the monster did not attack (never-blow?)";
    }
    return NULL;
}

static const char *an_tma_do_place(char **tok, int ntok)
{
    struct monster *mon;
    struct loc grid;
    int dx;
    int dy;

    if (ntok != 4) {
        return "place needs a monster, dx and dy";
    }
    if (!an_tma_parse_int(tok[2], &dx) || !an_tma_parse_int(tok[3], &dy)) {
        return "dx and dy must be integers";
    }
    mon = an_tma_pick(tok[1]);
    if (!mon) {
        return "no such monster";
    }
    grid = loc(player->grid.x + dx, player->grid.y + dy);
    if (!square_in_bounds_fully(cave, grid)) {
        return "the grid is out of the map";
    }
    if (loc_eq(grid, player->grid)) {
        return "the player is there";
    }
    if (loc_eq(grid, mon->grid)) {
        return NULL;   /* もうそこにいる */
    }
    if (square_monster(cave, grid)) {
        return "another monster is there";
    }
    if (!square_ispassable(cave, grid)
        && !(monster_passes_walls(mon) && !square_isperm(cave, grid))) {
        return "the monster cannot stand there";
    }
    monster_swap(mon->grid, grid);
    return NULL;
}

static const char *an_tma_do_hold(char **tok, int ntok)
{
    int on;
    int i;

    if (ntok != 3) {
        return "hold needs a monster (or all) and on|off";
    }
    if (strcmp(tok[2], "on") == 0) {
        on = 1;
    } else if (strcmp(tok[2], "off") == 0) {
        on = 0;
    } else {
        return "hold needs on or off";
    }
    if (!an_tma_hold_prepare()) {
        return "hold could not be prepared";
    }

    if (strcmp(tok[1], "all") == 0) {
        if (on) {
            an_tma_all = 1;
            for (i = 0; i < an_tma_slots; i++) {
                if (an_tma_state[i] == AN_TMA_LOOSE) {
                    an_tma_state[i] = AN_TMA_FREE;
                }
            }
            an_tma_apply();
        } else {
            an_tma_all = 0;
            for (i = 1; i < an_tma_slots; i++) {
                an_tma_release(i);
                an_tma_forget_slot(i);
            }
        }
        return NULL;
    }

    {
        struct monster *mon = an_tma_pick(tok[1]);

        if (!mon) {
            return "no such monster";
        }
        if (mon->midx < 1 || mon->midx >= an_tma_slots) {
            return "the monster number is out of range";
        }
        if (on) {
            an_tma_state[mon->midx] = AN_TMA_HELD;
            an_tma_race[mon->midx] = an_tma_base_race(mon);
            an_tma_apply();
        } else {
            an_tma_release(mon->midx);
            an_tma_race[mon->midx] = an_tma_base_race(mon);
            an_tma_state[mon->midx] = an_tma_all ? AN_TMA_LOOSE : AN_TMA_FREE;
            if (!an_tma_all) {
                an_tma_race[mon->midx] = NULL;
            }
        }
    }
    return NULL;
}

/* 1 命令を実行する。成功なら NULL、失敗なら理由の文字列を返す。 */
static const char *an_tma_run_one(char *line)
{
    char *tok[AN_TMA_MAX_TOKENS];
    int ntok = 0;
    char *p = line;
    const char *why;

    while (*p && (ntok < AN_TMA_MAX_TOKENS)) {
        while ((*p == ' ') || (*p == '\t')) {
            *p++ = '\0';
        }
        if (!*p) {
            break;
        }
        tok[ntok++] = p;
        while (*p && (*p != ' ') && (*p != '\t')) {
            p++;
        }
    }
    if (*p) {
        *p = '\0';
    }
    if (ntok == 0) {
        return NULL;
    }

    if (!character_generated || !player || player->is_dead || !cave) {
        return "the game is not running";
    }

    if (strcmp(tok[0], "cast") == 0) {
        why = an_tma_do_cast(tok, ntok);
    } else if (strcmp(tok[0], "melee") == 0) {
        why = an_tma_do_melee(tok, ntok);
    } else if (strcmp(tok[0], "place") == 0) {
        why = an_tma_do_place(tok, ntok);
    } else if (strcmp(tok[0], "hold") == 0) {
        why = an_tma_do_hold(tok, ntok);
    } else {
        return "unknown command";
    }
    if (why) {
        return why;
    }

    notice_stuff(player);
    handle_stuff(player);
    Term_fresh();
    return NULL;
}

void an_test_monster_act_poll(void)
{
    const char *path = getenv("HD2D_TEST_MONSTER_ACT");
    FILE *fp;
    static char buf[AN_TMA_FILE_LEN];
    static char lines[AN_TMA_MAX_LINES][AN_TMA_LINE_LEN];
    int nlines = 0;
    size_t len;
    int i;
    int failed = 0;

    if (!path || !path[0] || an_tma_busy) {
        return;
    }
    if (!an_awaiting_command()) {
        return;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        an_tma_stuck = 0;
        return;
    }
    if (an_tma_stuck) {
        fclose(fp);
        return;
    }
    len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    /* 先に消す。消せなければ名前を変える。どちらもできなければ二重に実行しないよう印を立てる */
    if (remove(path) != 0) {
        char moved[1100];
        snprintf(moved, sizeof(moved), "%s.done", path);
        (void)remove(moved);
        if (rename(path, moved) != 0) {
            an_tma_stuck = 1;
        }
    }

    {
        char *q = buf;
        while (*q && (nlines < AN_TMA_MAX_LINES)) {
            char *eol = q;
            size_t n;
            while (*eol && (*eol != '\n') && (*eol != '\r')) {
                eol++;
            }
            n = (size_t)(eol - q);
            if (n >= AN_TMA_LINE_LEN) {
                n = AN_TMA_LINE_LEN - 1;
            }
            memcpy(lines[nlines], q, n);
            lines[nlines][n] = '\0';
            nlines++;
            while ((*eol == '\n') || (*eol == '\r')) {
                eol++;
            }
            q = eol;
        }
    }

    an_tma_busy = 1;
    for (i = 0; i < nlines; i++) {
        char shown[AN_TMA_LINE_LEN];
        const char *why;
        char *a = lines[i];

        while ((*a == ' ') || (*a == '\t')) {
            a++;
        }
        if (!*a || (*a == '#')) {
            continue;
        }
        strcpy(shown, a);
        if (failed) {
            an_tma_report("skip", shown);
            continue;
        }
        why = an_tma_run_one(a);
        if (why) {
            failed = 1;
            an_tma_report("fail", why);
        } else {
            an_tma_report("ok", shown);
        }
    }
    an_tma_busy = 0;
}
