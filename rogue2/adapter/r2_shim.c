#include "r2_shim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat/curses.h"

#include "rogue.h"
#include "object.h"
#include "invent.h"
#include "room.h"
#include "monster.h"
#include "pack.h"
#include "message.h"

extern fighter rogue;
extern short blind;
extern short halluc, confused, levitate, haste_self, bear_trap;
extern boolean see_invisible, detect_monster, being_held;
extern unsigned short dungeon[ROGUE_LINES][ROGUE_COLUMNS];
extern object level_objects;
extern object level_monsters;
extern short cur_level;
extern char *nick_name;
extern char hunger_str[];
extern char *m_names[];

#define R2_MESSAGE_HISTORY_MAX 32
static char *r2_message_history[R2_MESSAGE_HISTORY_MAX];
static uint32_t r2_message_history_seq[R2_MESSAGE_HISTORY_MAX];
static int r2_message_history_used;
static uint32_t r2_message_next_seq;

int r2_text_set_n(char **target, const char *text, size_t bytes)
{
    char *grown;
    if (target == 0) return 0;
    if (text == 0) {
        text = "";
        bytes = 0;
    }
    grown = (char *)realloc(*target, bytes + 1);
    if (grown == 0) return 0;
    if (bytes != 0) memcpy(grown, text, bytes);
    grown[bytes] = 0;
    *target = grown;
    return 1;
}

int r2_text_set(char **target, const char *text)
{
    return r2_text_set_n(target, text != 0 ? text : "", text != 0 ? strlen(text) : 0);
}

static int r2_text_format(char **target, int append, const char *format, va_list args)
{
    va_list copy;
    int formatted;
    size_t prefix;
    char *grown;

    if (target == 0 || format == 0) return 0;
    prefix = (append && *target != 0) ? strlen(*target) : 0;
    va_copy(copy, args);
    formatted = vsnprintf(0, 0, format, copy);
    va_end(copy);
    if (formatted < 0) return 0;

    grown = (char *)realloc(*target, prefix + (size_t)formatted + 1);
    if (grown == 0) return 0;
    *target = grown;
    vsnprintf(grown + prefix, (size_t)formatted + 1, format, args);
    return 1;
}

int r2_text_setf(char **target, const char *format, ...)
{
    int ok;
    va_list args;
    va_start(args, format);
    ok = r2_text_format(target, 0, format, args);
    va_end(args);
    return ok;
}

int r2_text_appendf(char **target, const char *format, ...)
{
    int ok;
    va_list args;
    va_start(args, format);
    ok = r2_text_format(target, 1, format, args);
    va_end(args);
    return ok;
}

int r2_text_append(char **target, const char *text)
{
    size_t before;
    size_t add;
    char *grown;
    if (target == 0 || text == 0) return 0;
    before = *target != 0 ? strlen(*target) : 0;
    add = strlen(text);
    grown = (char *)realloc(*target, before + add + 1);
    if (grown == 0) return 0;
    if (add != 0) memcpy(grown + before, text, add);
    grown[before + add] = 0;
    *target = grown;
    return 1;
}

void r2_text_clear(char **target)
{
    if (target == 0) return;
    free(*target);
    *target = 0;
}

const char *r2_text_cstr(const char *text)
{
    return text != 0 ? text : "";
}

void r2_message_history_push(const char *text)
{
    int slot;
    char localized[1024];
    if (text == 0 || text[0] == 0) return;
    /* 英語のときは、画面へ書くのと同じ英語の語順にして積む（修-56）。 */
    if (r2_localize_text(text, localized, sizeof(localized))) text = localized;
    if (r2_message_history_used >= R2_MESSAGE_HISTORY_MAX) {
        free(r2_message_history[0]);
        memmove(r2_message_history, r2_message_history + 1,
            sizeof(r2_message_history[0]) * (R2_MESSAGE_HISTORY_MAX - 1));
        memmove(r2_message_history_seq, r2_message_history_seq + 1,
            sizeof(r2_message_history_seq[0]) * (R2_MESSAGE_HISTORY_MAX - 1));
        r2_message_history[R2_MESSAGE_HISTORY_MAX - 1] = 0;
        r2_message_history_used = R2_MESSAGE_HISTORY_MAX - 1;
    }
    slot = r2_message_history_used;
    if (!r2_text_set(&r2_message_history[slot], text)) return;
    ++r2_message_history_used;
    if (++r2_message_next_seq == 0) ++r2_message_next_seq;
    r2_message_history_seq[slot] = r2_message_next_seq;
}

int r2_message_history_count(void) { return r2_message_history_used; }

int r2_message_history_get(int index, char *out, int out_size, uint32_t *seq)
{
    size_t take;
    if (index < 0 || index >= r2_message_history_used || out == 0 || out_size <= 0) return 0;
    take = strlen(r2_text_cstr(r2_message_history[index]));
    if (take >= (size_t)out_size) take = (size_t)out_size - 1;
    while (take > 0 && (((unsigned char)r2_message_history[index][take] & 0xc0u) == 0x80u)) --take;
    memcpy(out, r2_text_cstr(r2_message_history[index]), take);
    out[take] = 0;
    if (seq != 0) *seq = r2_message_history_seq[index];
    return 1;
}

const char *r2_message_history_text(int index, uint32_t *seq)
{
    if (index < 0 || index >= r2_message_history_used) return 0;
    if (seq != 0) *seq = r2_message_history_seq[index];
    return r2_text_cstr(r2_message_history[index]);
}

/* ==================================================================== 言語（修-56）
 *
 * 原作は日本語のメッセージ表（rogue2/mesg）しか持たない。英語のときは、読み込んだ mesg[番号] の
 * 中身を r2_mesg_en.h の英語へ差し替える。日本語へ戻すときは、読み込み直後に控えた日本語を書き戻す。
 * 言語の指定は画面側の ui_state から来る（受信スレッド。r2_set_language_english）。mesg を書き換えるのは
 * ゲームのスレッドだけ（r2_language_apply。initscr() とキー待ちの中から呼ばれる）。
 * 状態の行は、切り替えの直後のコマンド待ちで描き直す（r2_language_service）。
 */
#include "r2_mesg_en.h"

#define R2_MESG_SLOTS 507

static volatile int r2_lang_want_en = 0;
static int r2_lang_applied_en = 0;
static int r2_lang_redraw_pending = 0;
static int r2_mesg_snapped = 0;
static char r2_mesg_ja[R2_MESG_SLOTS][R2_MESSAGE_BYTES];

void r2_set_language_english(int english) { r2_lang_want_en = english ? 1 : 0; }

int r2_language_english(void) { return r2_lang_applied_en; }

/*
 * 言語を切り替えたとき、切り替える前の言語で積んだメッセージ履歴は捨てる（日本語の文を英語へ直す手が無く、
 * 残すと英語の画面に日本語の履歴が混ざる。修-56）。
 */
static void r2_message_history_clear(void)
{
    int i;
    for (i = 0; i < r2_message_history_used; ++i) {
        free(r2_message_history[i]);
        r2_message_history[i] = 0;
    }
    r2_message_history_used = 0;
}

/* 英語の語順への組み直し（持ち物の説明・動詞の位置・辞書）。r2_localize_text() を定義する。 */
#include "r2_en_text.h"

void r2_language_apply(void)
{
    const int want = r2_lang_want_en;
    const size_t count = sizeof(r2_mesg_en_table) / sizeof(r2_mesg_en_table[0]);
    size_t i;
    int hunger_slot = -1;
    if (want == r2_lang_applied_en) return;
    if (!r2_mesg_snapped) {
        /* mesg は do_args() の read_mesg() で読まれる。まだ空なら読み込み後にやり直す。 */
        if (mesg[10][0] == 0) return;
        memcpy(r2_mesg_ja, mesg, sizeof(r2_mesg_ja));
        r2_mesg_snapped = 1;
    }
    /* 空腹の字は hunger_str へ写してある。切り替えたあとも同じ段階の字になるよう、番号を控える。 */
    if (hunger_str[0] != 0) {
        static const int slots[3] = { 71, 73, 75 };
        int k;
        for (k = 0; k < 3; ++k) {
            if (strcmp(hunger_str, mesg[slots[k]]) == 0) hunger_slot = slots[k];
        }
    }
    for (i = 0; i < count; ++i) {
        const int index = r2_mesg_en_table[i].index;
        const char *text = want ? r2_mesg_en_table[i].text : r2_mesg_ja[index];
        if (index < 0 || index >= R2_MESG_SLOTS) continue;
        snprintf(mesg[index], R2_MESSAGE_BYTES, "%s", text);
    }
    /* 日本語の語順で継ぎ足される断片は、私用領域の 1 字にする（英語のとき）か、日本語へ戻す。 */
    for (i = 0; i < sizeof(r2_mesg_markers) / sizeof(r2_mesg_markers[0]); ++i) {
        const int index = r2_mesg_markers[i];
        if (index < 0 || index >= R2_MESG_SLOTS) continue;
        if (want) {
            const unsigned cp = 0xe000u + (unsigned)index;
            mesg[index][0] = (char)(0xe0u | (cp >> 12));
            mesg[index][1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            mesg[index][2] = (char)(0x80u | (cp & 0x3fu));
            mesg[index][3] = 0;
        } else {
            snprintf(mesg[index], R2_MESSAGE_BYTES, "%s", r2_mesg_ja[index]);
        }
    }
    if (hunger_slot >= 0) snprintf(hunger_str, R2_TITLE_BYTES, "%s", mesg[hunger_slot]);
    r2_lang_applied_en = want;
    r2_lang_redraw_pending = 1;
    r2_message_history_clear();
}

/*
 * キー待ちの中から呼ぶ（ゲームのスレッド）。言語を適用し、切り替えた直後の通常のコマンド待ちで
 * 状態の行を描き直す。問い・［続く］・設定・一覧の最中は描き直さず、次のキー待ちで再び試す。
 */
void r2_language_service(void)
{
    r2_language_apply();
    if (!r2_lang_redraw_pending) return;
    if (!r2_term_command_input_active() || r2_term_modal_active() || r2_term_text_input_active()
        || r2_term_numeric_active() || r2_term_char_prompt_kind() != 0 || r2_term_pack_prompt_mask() != 0u) {
        return;
    }
    r2_lang_redraw_pending = 0;
    check_message();            /* 前の言語のメッセージを消す */
    move(ROGUE_LINES - 1, 0);   /* 古い言語のラベルが残らないよう、行ごと描き直す */
    clrtoeol();
    print_stats(STAT_ALL);
}

/* 状態の行（最下段）の桁。原作の日本語版の配置 → 英語版の配置（message.c print_stats のどちらの #if も同じ順）。 */
int r2_status_column_en(int japanese_column)
{
    static const int table[][2] = {
        { 0, 0 }, { 4, 7 }, { 7, 10 }, { 13, 16 }, { 20, 23 }, { 26, 27 }, { 35, 36 },
        { 41, 41 }, { 48, 48 }, { 54, 53 }, { 57, 56 }, { 63, 61 }, { 75, 73 },
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (table[i][0] == japanese_column) return table[i][1];
    }
    return japanese_column;
}

static const char *r2_lbl(const char *japanese, const char *english)
{
    return r2_lang_applied_en ? english : japanese;
}

int r2_player_row(void) { return rogue.row; }
int r2_player_col(void) { return rogue.col; }
int r2_player_hp(void) { return rogue.hp_current; }
int r2_player_hp_max(void) { return rogue.hp_max; }
int r2_player_level(void) { return rogue.exp; }
long r2_player_gold(void) { return rogue.gold; }
int r2_dungeon_level(void) { return cur_level; }
const char *r2_player_name(void) { return nick_name != 0 ? nick_name : ""; }
const char *r2_hunger_text(void) { return hunger_str; }
/* 原作の終了文「それでは、またね。」（mesg[12]）。clean_up() の文がこれなら正常終了。 */
const char *r2_byebye_text(void) { return mesg[12]; }

void r2_farewell_show(const char *text)
{
    check_message();            /* 終了の確認の問い（「ゲームを終了してよいのですか？」）を消す */
    message((char *)text, 0);   /* 履歴へ積み、行 0 へ出す */
    message((char *)"", 0);     /* 直前の文に［続く］を付けて、決定・取消・Space を待つ */
    check_message();
}
const char *r2_monster_name(int monster_id)
{
    if (monster_id < 1 || monster_id > 26) return "";
    return m_names[monster_id - 1] != 0 ? m_names[monster_id - 1] : "";
}
/*
 * 光の届く範囲。どちらも読むだけでゲームの状態は変えない。
 *
 * r2_can_see: 原作の rogue_can_see()。同じ部屋（迷路の部屋を除く）か、すぐ隣のマスで、目が見えているとき真。
 *   モンスターを描くかどうかも原作はこの判定で決める（monster.c）。「いま見えている」マス。
 * r2_light_reaches: 松明風の光（半径 1）が届くマス。原作の rogue_is_around()（自分の周り 3x3）で、目が見えているとき真。
 *   RogueClone2 に光源の道具は無いが、決めたこと（2026-10-11）で光源は松明風にする。
 */
int r2_can_see(int row, int col)
{
    return rogue_can_see(row, col) ? 1 : 0;
}

/* 盲目か（攻撃の効果の記録が、見えない間は絵を出さないために使う。読むだけ）。 */
int r2_player_blind(void)
{
    return blind > 0 ? 1 : 0;
}

int r2_light_reaches(int row, int col)
{
    return (!blind && rogue_is_around((short)row, (short)col)) ? 1 : 0;
}

/*
 * 状態のアイコン。範囲は原作の最下段（空腹）に、メッセージで知らされる状態（盲目・混乱・幻覚・浮遊・加速・透明視・怪物感知・
 * 熊の罠や食人植物に捕まっている）と、鑑定済みの指輪の効果を足したもの。
 * 指輪は「種類が分かっている」（原作の get_desc が本当の名前を出す条件。obj->identified か、種類の id_status が IDENTIFIED）
 * ものだけ。未鑑定の指輪の効果は出さない（原作より情報を増やさない）。効果量は原作が数字を見せるとき（obj->identified）だけ。
 */
#define R2_ST_GOOD 0
#define R2_ST_BAD 1
#define R2_ST_PERM 4
#define R2_ST_CURSE 5

static void r2_status_add(r2_status_effect *out, int *count, int cap, const char *key, const char *label,
    const char *amount, int kind)
{
    int i;
    for (i = 0; i < *count; ++i) {
        if (strcmp(out[i].key, key) == 0 && out[i].kind == kind && strcmp(out[i].label, label) == 0) return; /* 重なりは 1 つ */
    }
    if (*count >= cap) return;
    snprintf(out[*count].key, sizeof(out[*count].key), "%s", key);
    snprintf(out[*count].label, sizeof(out[*count].label), "%s", label);
    snprintf(out[*count].amount, sizeof(out[*count].amount), "%s", amount != 0 ? amount : "");
    out[*count].kind = kind;
    ++*count;
}

static void r2_ring_status(const object *ring, r2_status_effect *out, int *count, int cap)
{
    char amount[12];
    if (ring == 0 || ring->which_kind >= RINGS) return;
    if (!(ring->identified || id_rings[ring->which_kind].id_status == IDENTIFIED)) return; /* 未鑑定は出さない */
    switch (ring->which_kind) {
    case STEALTH:
        r2_status_add(out, count, cap, "stealth", r2_lbl("隠密", "Stealth"), 0, R2_ST_PERM);
        break;
    case R_TELEPORT:
        r2_status_add(out, count, cap, "cursed", r2_lbl("テレポート", "Teleport"), 0, R2_ST_CURSE); /* 勝手に飛ばされる呪い */
        break;
    case REGENERATION:
        r2_status_add(out, count, cap, "regeneration", r2_lbl("再生", "Regeneration"), 0, R2_ST_PERM);
        break;
    case SLOW_DIGEST:
        r2_status_add(out, count, cap, "slow_digest", r2_lbl("遅消化", "Slow digestion"), 0, R2_ST_PERM);
        break;
    case ADD_STRENGTH:
    case DEXTERITY:
        amount[0] = 0;
        if (ring->identified) snprintf(amount, sizeof(amount), "%+d", (int)ring->class);
        if (ring->class < 0) {
            r2_status_add(out, count, cap, "low_melee", ring->which_kind == ADD_STRENGTH ? r2_lbl("腕力低下", "Strength down") : r2_lbl("器用さ低下", "Dexterity down"), amount, R2_ST_CURSE);
        } else if (ring->which_kind == ADD_STRENGTH) {
            r2_status_add(out, count, cap, "might", r2_lbl("腕力", "Might"), amount, R2_ST_PERM);
        } else {
            r2_status_add(out, count, cap, "blows", r2_lbl("器用さ", "Dexterity"), amount, R2_ST_PERM);
        }
        break;
    case SUSTAIN_STRENGTH:
        r2_status_add(out, count, cap, "sust_str", r2_lbl("腕力維持", "Sustain strength"), 0, R2_ST_PERM);
        break;
    case R_SEE_INVISIBLE:
        r2_status_add(out, count, cap, "senseunseen", r2_lbl("透明視", "See invisible"), 0, R2_ST_PERM);
        break;
    case MAINTAIN_ARMOR:
        r2_status_add(out, count, cap, "stoneskin", r2_lbl("鎧維持", "Maintain armor"), 0, R2_ST_PERM);
        break;
    case SEARCHING:
        r2_status_add(out, count, cap, "search", r2_lbl("探索", "Searching"), 0, R2_ST_PERM);
        break;
    default: /* 装飾の指輪（ADORNMENT）は効果が無い */
        break;
    }
}

int r2_status_effects(r2_status_effect *out, int cap)
{
    int count = 0;
    const char *hunger = hunger_str;
    if (out == 0 || cap <= 0) return 0;
    /* 空腹。原作の最下段と同じ字（mesg[71]・[73]・[75]。空なら普通）。 */
    if (hunger != 0 && hunger[0] != 0) r2_status_add(out, &count, cap, "hungry", hunger, 0, R2_ST_BAD);
    if (blind > 0) r2_status_add(out, &count, cap, "blind", r2_lbl("盲目", "Blind"), 0, R2_ST_BAD);
    if (confused > 0) r2_status_add(out, &count, cap, "confused", r2_lbl("混乱", "Confused"), 0, R2_ST_BAD);
    if (halluc > 0) r2_status_add(out, &count, cap, "hallucination", r2_lbl("幻覚", "Hallucinating"), 0, R2_ST_BAD);
    if (bear_trap > 0 || being_held) r2_status_add(out, &count, cap, "rl_held", r2_lbl("拘束", "Held"), 0, R2_ST_BAD);
    if (levitate > 0) r2_status_add(out, &count, cap, "levitate", r2_lbl("浮遊", "Levitating"), 0, R2_ST_GOOD);
    if (haste_self > 0) r2_status_add(out, &count, cap, "haste", r2_lbl("加速", "Hasted"), 0, R2_ST_GOOD);
    if (see_invisible) r2_status_add(out, &count, cap, "senseunseen", r2_lbl("透明視", "See invisible"), 0, R2_ST_GOOD);
    if (detect_monster) r2_status_add(out, &count, cap, "telepathy", r2_lbl("怪物感知", "Detect monsters"), 0, R2_ST_GOOD);
    r2_ring_status(rogue.left_ring, out, &count, cap);
    r2_ring_status(rogue.right_ring, out, &count, cap);
    return count;
}

/*
 * 自己検査。ゲームの大域変数を直に書き換えて、組み合わせを全数で照合する（このプロセスはこの検査で終わる）。
 * 未鑑定の指輪・名前だけ付けた（CALLED）指輪・重なりなどは実機で作りにくい。
 */
/*
 * 送った鍵の控えと、画面側の表（`hd2d/ui/status_icon_table.inc`）との突き合わせ。知らない鍵は疑問符のアイコンになるので、
 * 表に無い鍵を送らないことを自己検査で見る。表のパスは環境変数 HENGBAND_STATUS_ICON_TABLE（無ければ突き合わせは 0 件。関門が渡す）。
 */
static char r2_seen_keys[96][24];
static int r2_seen_count = 0;

static void r2_seen_add(const char *key)
{
    int i;
    for (i = 0; i < r2_seen_count; ++i) {
        if (strcmp(r2_seen_keys[i], key) == 0) return;
    }
    if (r2_seen_count < 96) snprintf(r2_seen_keys[r2_seen_count++], sizeof(r2_seen_keys[0]), "%s", key);
}

static int r2_table_check(void)
{
    const char *path = getenv("HENGBAND_STATUS_ICON_TABLE");
    char *table;
    long size;
    FILE *fp;
    int i, failures = 0;
    if (path == 0 || path[0] == 0) return 0;
    fp = fopen(path, "rb");
    if (fp == 0) {
        fprintf(stderr, "[rogue2] status effects selftest FAIL: cannot read icon table %s\n", path);
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
    for (i = 0; i < r2_seen_count; ++i) {
        char needle[40];
        snprintf(needle, sizeof(needle), "\"%s\"", r2_seen_keys[i]);
        if (strstr(table, needle) == 0) {
            fprintf(stderr, "[rogue2] status effects selftest FAIL: key %s is not in the icon table\n", r2_seen_keys[i]);
            ++failures;
        }
    }
    free(table);
    if (failures == 0) fprintf(stderr, "[rogue2] status effects: %d keys checked against the icon table\n", r2_seen_count);
    return failures;
}

static int r2_selftest_expect(const char *what, const char *want)
{
    r2_status_effect list[16];
    char got[512];
    int n = r2_status_effects(list, 16);
    int i;
    got[0] = 0;
    for (i = 0; i < n; ++i) {
        char piece[128];
        r2_seen_add(list[i].key);
        snprintf(piece, sizeof(piece), "%s%s/%d=%s%s%s", i > 0 ? "," : "", list[i].key, list[i].kind, list[i].label,
            list[i].amount[0] != 0 ? "!" : "", list[i].amount);
        strncat(got, piece, sizeof(got) - strlen(got) - 1);
    }
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "[rogue2] status effects selftest FAIL %s: want [%s] got [%s]\n", what, want, got);
        return 1;
    }
    return 0;
}

int r2_status_effects_selftest(void)
{
    int failures = 0;
    object left, right;
    object *saved_left = rogue.left_ring, *saved_right = rogue.right_ring;
    unsigned short saved_status[RINGS];
    int i;
    for (i = 0; i < RINGS; ++i) saved_status[i] = id_rings[i].id_status;
    hunger_str[0] = 0;
    rogue.left_ring = 0;
    rogue.right_ring = 0;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));

    failures += r2_selftest_expect("何もかかっていない", "");
    blind = 3; confused = 3; halluc = 3; levitate = 3; haste_self = 3; see_invisible = 1; detect_monster = 1;
    failures += r2_selftest_expect("一時的な状態の全部",
        "blind/1=盲目,confused/1=混乱,hallucination/1=幻覚,levitate/0=浮遊,haste/0=加速,senseunseen/0=透明視,telepathy/0=怪物感知");
    blind = confused = halluc = levitate = haste_self = 0; see_invisible = detect_monster = 0;
    bear_trap = 4;
    failures += r2_selftest_expect("熊の罠", "rl_held/1=拘束");
    being_held = 1;
    failures += r2_selftest_expect("食人植物と熊の罠の重なりは 1 つ", "rl_held/1=拘束");
    bear_trap = 0; being_held = 0;
    strcpy(hunger_str, "空腹");
    failures += r2_selftest_expect("空腹の字は原作のまま", "hungry/1=空腹");
    hunger_str[0] = 0;

    /* 指輪は種類が分かっているものだけ。 */
    for (i = 0; i < RINGS; ++i) id_rings[i].id_status = UNIDENTIFIED;
    left.what_is = RING;
    left.which_kind = REGENERATION;
    rogue.left_ring = &left;
    failures += r2_selftest_expect("未鑑定の指輪は出さない", "");
    id_rings[REGENERATION].id_status = CALLED;
    failures += r2_selftest_expect("名前を付けただけの指輪は出さない", "");
    id_rings[REGENERATION].id_status = IDENTIFIED;
    failures += r2_selftest_expect("種類が分かった指輪", "regeneration/4=再生");
    id_rings[REGENERATION].id_status = UNIDENTIFIED;
    left.identified = 1;
    failures += r2_selftest_expect("その指輪だけ鑑定済み（obj->identified）でも出す", "regeneration/4=再生");
    left.identified = 0;
    id_rings[REGENERATION].id_status = IDENTIFIED;
    right = left;
    rogue.right_ring = &right;
    failures += r2_selftest_expect("同じ指輪を 2 つ着けても 1 つ", "regeneration/4=再生");
    right.which_kind = R_TELEPORT;
    id_rings[R_TELEPORT].id_status = IDENTIFIED;
    failures += r2_selftest_expect("テレポートの指輪は呪い", "regeneration/4=再生,cursed/5=テレポート");
    left.which_kind = ADD_STRENGTH; left.class = 2; left.identified = 0;
    right.which_kind = DEXTERITY; right.class = -1; right.identified = 1;
    id_rings[ADD_STRENGTH].id_status = IDENTIFIED;
    id_rings[DEXTERITY].id_status = IDENTIFIED;
    failures += r2_selftest_expect("効果量は obj->identified のときだけ（負は呪い）", "might/4=腕力,low_melee/5=器用さ低下!-1");
    left.which_kind = ADD_STRENGTH; left.class = -2; left.identified = 1;
    right.which_kind = DEXTERITY; right.class = 1; right.identified = 1;
    failures += r2_selftest_expect("負の腕力と正の器用さ", "low_melee/5=腕力低下!-2,blows/4=器用さ!+1");
    left.which_kind = STEALTH; right.which_kind = SLOW_DIGEST;
    id_rings[STEALTH].id_status = IDENTIFIED; id_rings[SLOW_DIGEST].id_status = IDENTIFIED;
    failures += r2_selftest_expect("隠密と遅消化", "stealth/4=隠密,slow_digest/4=遅消化");
    left.which_kind = SUSTAIN_STRENGTH; right.which_kind = R_SEE_INVISIBLE;
    id_rings[SUSTAIN_STRENGTH].id_status = IDENTIFIED; id_rings[R_SEE_INVISIBLE].id_status = IDENTIFIED;
    failures += r2_selftest_expect("腕力維持と透明視", "sust_str/4=腕力維持,senseunseen/4=透明視");
    left.which_kind = MAINTAIN_ARMOR; right.which_kind = SEARCHING;
    id_rings[MAINTAIN_ARMOR].id_status = IDENTIFIED; id_rings[SEARCHING].id_status = IDENTIFIED;
    failures += r2_selftest_expect("鎧維持と探索", "stoneskin/4=鎧維持,search/4=探索");
    left.which_kind = ADORNMENT; id_rings[ADORNMENT].id_status = IDENTIFIED;
    right.which_kind = ADORNMENT;
    failures += r2_selftest_expect("装飾の指輪は効果が無い", "");

    failures += r2_table_check();
    rogue.left_ring = saved_left;
    rogue.right_ring = saved_right;
    for (i = 0; i < RINGS; ++i) id_rings[i].id_status = saved_status[i];
    return failures;
}

/*
 * テスト用の状態の付与（環境変数 HENGBAND_TEST_STATUS。カンマ区切り）。画面の入力の経路の検査（スクリーンショット）と
 * プロトコルの検査（tools/rogue2/protocol_audit.py の status_effects_audit）が、乱数に頼らず状態のアイコンを確かめるための足場
 * （変愚の HD2D_TEST_FORCE_IMAGE と同じ扱い）。**環境変数が無ければ何もしない**。ゲームのルールは変えない——
 * 付ける状態は原作の本物の状態（盲目・混乱・幻覚・浮遊・加速・透明視・怪物感知・熊の罠・空腹）で、原作の処理がそのまま減らす。
 *
 * 名前: blind confused halluc levitate haste seeinv detect held hungry。最初の 1 回（ゲームが始まってから）だけ付ける。
 * hungry は残りを HUNGRY の 1 手前にして、次の手番で原作の check_hunger に空腹の字を出させる。
 */
static int r2_test_spec_has(const char *spec, const char *name)
{
    const size_t length = strlen(name);
    const char *at = spec;
    while (at != 0 && *at != 0) {
        const char *end = strchr(at, ',');
        const size_t span = end != 0 ? (size_t)(end - at) : strlen(at);
        if (span == length && strncmp(at, name, length) == 0) return 1;
        at = end != 0 ? end + 1 : 0;
    }
    return 0;
}

void r2_test_status_apply(void)
{
    static int done = 0;
    const char *spec;
    if (done) return;
    done = 1;
    spec = getenv("HENGBAND_TEST_STATUS");
    if (spec == 0 || spec[0] == 0) return;
    if (r2_test_spec_has(spec, "blind")) blind = 9000;
    if (r2_test_spec_has(spec, "confused")) confused = 9000;
    if (r2_test_spec_has(spec, "halluc")) halluc = 9000;
    if (r2_test_spec_has(spec, "levitate")) levitate = 9000;
    if (r2_test_spec_has(spec, "haste")) haste_self = 9001; /* 奇数（原作の薬と同じ） */
    if (r2_test_spec_has(spec, "seeinv")) see_invisible = 1;
    if (r2_test_spec_has(spec, "detect")) detect_monster = 1;
    if (r2_test_spec_has(spec, "held")) bear_trap = 9000;
    if (r2_test_spec_has(spec, "hungry")) rogue.moves_left = HUNGRY + 1;
}

/*
 * テスト用の持ち物の付与（環境変数 HENGBAND_TEST_FX_ITEMS。`種類:原作の定数名` を ; で区切る。種類は wand・scroll・weapon。
 * 定数名は rogue.h の MAGIC_MISSILE・HOLD_MONSTER・DAGGER など）。攻撃の効果の検査（tools/rogue2/r2_attack_fx_check.py）が、
 * 杖・巻物・武器を乱数に頼らず手に入れるための足場（上の HENGBAND_TEST_STATUS と同じ扱い）。**環境変数が無ければ何もしない**。
 * 作り方は原作と同じ（alloc_object・gr_weapon・add_to_pack をそのまま呼ぶ）で、ゲームのルールは変えない。名前の終わりに ! を付けると
 * その種類を鑑定済みにする（持ち物の名前で字を探すため）。ゲームが始まってから最初の 1 回だけ。
 */
static const struct { const char *name; int kind; } r2_test_wands[] = {
    { "TELE_AWAY", TELE_AWAY }, { "SLOW_MONSTER", SLOW_MONSTER }, { "CONFUSE_MONSTER", CONFUSE_MONSTER },
    { "INVISIBILITY", INVISIBILITY }, { "POLYMORPH", POLYMORPH }, { "HASTE_MONSTER", HASTE_MONSTER },
    { "PUT_TO_SLEEP", PUT_TO_SLEEP }, { "MAGIC_MISSILE", MAGIC_MISSILE }, { "CANCELLATION", CANCELLATION },
    { "DO_NOTHING", DO_NOTHING },
};
static const struct { const char *name; int kind; } r2_test_scrolls[] = {
    { "PROTECT_ARMOR", PROTECT_ARMOR }, { "HOLD_MONSTER", HOLD_MONSTER }, { "ENCH_WEAPON", ENCH_WEAPON },
    { "ENCH_ARMOR", ENCH_ARMOR }, { "IDENTIFY", IDENTIFY }, { "TELEPORT", TELEPORT }, { "SLEEP", SLEEP },
    { "SCARE_MONSTER", SCARE_MONSTER }, { "REMOVE_CURSE", REMOVE_CURSE }, { "CREATE_MONSTER", CREATE_MONSTER },
    { "AGGRAVATE_MONSTER", AGGRAVATE_MONSTER }, { "MAGIC_MAPPING", MAGIC_MAPPING },
};
static const struct { const char *name; int kind; } r2_test_weapons[] = {
    { "BOW", BOW }, { "DART", DART }, { "ARROW", ARROW }, { "DAGGER", DAGGER }, { "SHURIKEN", SHURIKEN },
    { "MACE", MACE }, { "LONG_SWORD", LONG_SWORD }, { "TWO_HANDED_SWORD", TWO_HANDED_SWORD },
};

static int r2_test_find_kind(const char *kind, const char *name)
{
    size_t i;
    if (strcmp(kind, "wand") == 0) {
        for (i = 0; i < sizeof(r2_test_wands) / sizeof(r2_test_wands[0]); ++i)
            if (strcmp(r2_test_wands[i].name, name) == 0) return r2_test_wands[i].kind;
    } else if (strcmp(kind, "scroll") == 0) {
        for (i = 0; i < sizeof(r2_test_scrolls) / sizeof(r2_test_scrolls[0]); ++i)
            if (strcmp(r2_test_scrolls[i].name, name) == 0) return r2_test_scrolls[i].kind;
    } else if (strcmp(kind, "weapon") == 0) {
        for (i = 0; i < sizeof(r2_test_weapons) / sizeof(r2_test_weapons[0]); ++i)
            if (strcmp(r2_test_weapons[i].name, name) == 0) return r2_test_weapons[i].kind;
    }
    return -1;
}

void r2_test_fx_items_apply(void)
{
    static int done = 0;
    char spec[512];
    const char *env;
    char *at;
    if (done) return;
    done = 1;
    env = getenv("HENGBAND_TEST_FX_ITEMS");
    if (env == 0 || env[0] == 0) return;
    snprintf(spec, sizeof(spec), "%s", env);
    for (at = strtok(spec, ";"); at != 0; at = strtok(0, ";")) {
        char *colon = strchr(at, ':');
        size_t length;
        int kind;
        int known = 0;
        object *obj;
        if (colon == 0) continue;
        *colon = 0;
        length = strlen(colon + 1);
        if (length > 0 && colon[length] == '!') {
            colon[length] = 0;
            known = 1;
        }
        kind = r2_test_find_kind(at, colon + 1);
        if (kind < 0) continue;
        obj = alloc_object();
        obj->which_kind = (unsigned short)kind;
        if (strcmp(at, "wand") == 0) {
            obj->what_is = WAND;
            obj->class = 10;
            if (known) id_wands[kind].id_status = IDENTIFIED;
        } else if (strcmp(at, "scroll") == 0) {
            obj->what_is = SCROL;
            if (known) id_scrolls[kind].id_status = IDENTIFIED;
        } else {
            gr_weapon(obj, 0);
            obj->hit_enchant = obj->d_enchant = 0;
            obj->identified = IDENTIFIED;
        }
        add_to_pack(obj, &rogue.pack, 1);
    }
}

int r2_trap_type_at(int row, int col)
{
    int i;
    for (i = 0; i < MAX_TRAPS && traps[i].trap_type >= 0; ++i) {
        if (traps[i].trap_row == row && traps[i].trap_col == col) return traps[i].trap_type;
    }
    return -1;
}

/*
 * 原作は未鑑定（id_status が IDENTIFIED でない）の薬・巻物・杖・指輪を、題名・色・材質でしか見せない。
 * 絵が本当の種類ごとに違うと拾う前に正体が分かるので、種類を 0x3f（未鑑定）に伏せて返す。
 * 画面側（r2_frame.cpp）は 0x3f をカテゴリごとの代表の絵で描く。
 * 呼び名を付けた（CALLED）だけの物も、原作は種類を明かさないので未鑑定として扱う。
 */
static unsigned int visible_kind(const object *item)
{
    if ((item->what_is & (SCROL | POTION | WAND | RING)) != 0) {
        struct id *table = get_id_table((object *)item);
        if (table == 0 || table[item->which_kind].id_status != IDENTIFIED) return 0x003fu;
    }
    return (unsigned int)(item->which_kind & 0x003fu);
}

static uint16_t object_id_at(int row, int col)
{
    object *item = object_at(&level_objects, row, col);
    unsigned int category;
    unsigned int kind;
    if (item == 0) {
        return 0;
    }
    category = (unsigned int)(item->what_is & 0x01ffu);
    kind = visible_kind(item);
    return (uint16_t)((category << 6) | kind);
}

static uint16_t object_id_of(const object *item)
{
    unsigned int category;
    unsigned int kind;
    if (item == 0) return 0;
    category = (unsigned int)(item->what_is & 0x01ffu);
    kind = visible_kind(item);
    return (uint16_t)((category << 6) | kind);
}

/* ==================================================================== 効果音 */

/*
 * Frox（`fc_shim.c`）・Sil-Q（`sq_shim.c`）と同じ形。鳴らすのは画面側なので、
 * コアは出来事の名前とマスを書き留めるだけ。画面側が欲しがっているとき
 * （`r2_sound_set_wanted`）しか積まない。溢れたら古いほうを捨てる。
 */
#define R2_SOUND_MAX 32

static r2_sound_event r2_sound_ring[R2_SOUND_MAX];
static int r2_sound_num = 0;
static int r2_sound_wanted = 0;

void r2_sound_set_wanted(int wanted)
{
    r2_sound_wanted = wanted ? 1 : 0;
    if (!r2_sound_wanted) {
        /* 欲しがらなくなったら溜まっているぶんも捨てる。 */
        r2_sound_num = 0;
    }
}

static void r2_sound_push_ev(const char *name, int row, int col)
{
    r2_sound_event *ev;

    if (!r2_sound_wanted) return;
    if (name == 0 || name[0] == 0) return;

    if (r2_sound_num >= R2_SOUND_MAX) {
        /* 溢れたら最も古い 1 つを捨てる。 */
        int i;
        for (i = 1; i < R2_SOUND_MAX; i++) {
            r2_sound_ring[i - 1] = r2_sound_ring[i];
        }
        r2_sound_num = R2_SOUND_MAX - 1;
    }
    ev = &r2_sound_ring[r2_sound_num++];
    memset(ev, 0, sizeof(*ev));
    strncpy(ev->name, name, sizeof(ev->name) - 1);
    ev->row = (short)row;
    ev->col = (short)col;
}

/*! 位置が分からない音。頭で鳴る（@ のマス）。 */
void r2_sound_push(const char *name)
{
    r2_sound_push_ev(name, rogue.row, rogue.col);
}

/*! 呼び出し箇所が実際のマスを持っているときはこちら。 */
void r2_sound_push_at(const char *name, int row, int col)
{
    r2_sound_push_ev(name, row, col);
}

/*
 * HP が残り 1/4 以下に落ちた「境目」でだけ積む（呼び出し側は毎回のダメージ後に呼ぶ）。
 * 境目を越えて戻った（回復した）ら次に落ちたときにまた鳴るよう戻す。
 */
static int r2_hp_warn_low = 0;

void r2_hp_warn_check(int hp_current, int hp_max, int row, int col)
{
    if (hp_max <= 0) return;
    if ((hp_current > 0) && (hp_current * 4 <= hp_max)) {
        if (!r2_hp_warn_low) {
            r2_hp_warn_low = 1;
            r2_sound_push_at("rogue_hitpoint_warn", row, col);
        }
    } else {
        r2_hp_warn_low = 0;
    }
}

int r2_sound_take(r2_sound_event *out, int cap)
{
    int n = r2_sound_num;
    if (!out || (cap <= 0)) {
        return 0;
    }
    if (n > cap) {
        n = cap;
    }
    memcpy(out, r2_sound_ring, (size_t)n * sizeof(r2_sound_event));
    /* 汲んだら消す。音は状態ではなく一度きりの出来事である。 */
    r2_sound_num = 0;
    return n;
}

int r2_read_cell(int row, int col, r2_cell_state *out)
{
    if (out == 0 || row < 0 || row >= ROGUE_LINES || col < 0 || col >= ROGUE_COLUMNS) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->dungeon_flags = dungeon[row][col];
    /*
     * 地図として読むのは、原作が地図の上へ一覧を重ねる前の画面である。持ち物・わかったもの一覧・
     * ヘルプ・設定は r2_term_snapshot_push() で画面を写してから文字を重ねるので、重ねている間は
     * その写しを読む。いまの端末を読むと、一覧の文字が載った岩盤が「見えているマス」になり、
     * HD2D の地形の記憶に歩けない床として残っていた（2026-09-25）。
     */
    out->codepoint = r2_term_map_codepoint(row, col);
    out->attr = r2_term_map_attr(row, col);
    if ((out->dungeon_flags & MONSTER) != 0) {
        object *monster = object_at(&level_monsters, row, col);
        if (monster != 0) {
            /*
             * trail_char は敵を描く前にそのマスに見えていた地形。敵の一時的な描画で
             * 端末セルが空白になっても、既知の床まで Unknown に落とさないため UI 側へ渡す。
             */
            out->under_codepoint = ((uint32_t)(unsigned short)monster->trail_char) & A_CHARTEXT;
            /*
             * 実際に本来の文字で見えている敵だけを「敵」として公開する。透明・擬態・幻覚で
             * 別の記号を表示している場合に、本当の正体をボクセル板で漏らさない。
             */
            if (monster->m_char >= 'A' && monster->m_char <= 'Z'
                && out->codepoint == (uint32_t)(unsigned char)monster->m_char) {
                out->monster_id = (uint16_t)(monster->m_char - 'A' + 1);
            }
        }
    }
    if ((out->dungeon_flags & OBJECT) != 0) {
        object *item = object_at(&level_objects, row, col);
        if (item != 0 && out->codepoint == (uint32_t)get_mask_char(item->what_is)) {
            out->object_id = object_id_at(row, col);
        }
    }
    return 1;
}

int r2_inventory_count(void)
{
    int count = 0;
    object *item = rogue.pack.next_object;
    while (item != 0) {
        ++count;
        item = item->next_object;
    }
    return count;
}

int r2_inventory_item(int index, r2_item_state *out)
{
    int at = 0;
    object *item = rogue.pack.next_object;
    if (out == 0 || index < 0) return 0;
    while (item != 0 && at < index) {
        item = item->next_object;
        ++at;
    }
    if (item == 0) return 0;
    memset(out, 0, sizeof(*out));
    out->key = item->ichar;
    out->category = (uint16_t)(item->what_is & 0x01ffu);
    out->kind = (uint16_t)(item->which_kind & 0x003fu);
    out->object_id = object_id_of(item);
    out->quantity = item->quantity;
    out->in_use_flags = item->in_use_flags;
    out->is_cursed = item->is_cursed ? 1 : 0;
    out->identified = item->identified ? 1 : 0;
    out->callable = 0;
    if ((item->what_is & (SCROL | POTION | WAND | RING)) != 0) {
        struct id *table = get_id_table(item);
        if (table != 0 && table[item->which_kind].id_status != IDENTIFIED) out->callable = 1;
    }
    get_desc(item, out->description, 0);
    out->description[sizeof(out->description) - 1] = 0;
    {
        /* 英語のときは、日本語の語順で組まれた説明を英語の語順にする（修-56）。 */
        char localized[sizeof(out->description)];
        if (r2_localize_text(out->description, localized, sizeof(localized))) {
            snprintf(out->description, sizeof(out->description), "%s", localized);
        }
    }
    return 1;
}

/* ==================================================================== 英語の表示の自己検査（修-56 の第 2 回）
 *
 * 日本語のメッセージ表（rogue2/mesg）を読み込み、英語へ切り替えて次を確かめる。
 *   1. 中身のある番号が全部、英語（差し替え・断片の印・辞書の鍵）で覆われ、差し替えた文に日本語の字が残らない。
 *   2. 書式（%s %d など）の並びが日本語の版と同じ（意図した違いの 22・23 だけ許す）。
 *   3. 日本語の語順で継ぎ足された字（持ち物の説明・動詞が後ろの文・得点表の行）が、原作の英語版と同じ形になる。
 *   4. 日本語へ戻すと mesg が元どおりになり、変換は何も変えない。
 * 訳し残し（英語のときに日本語が残る文）の数を数えて 0 にする。
 */
static int r2_st_has_cjk(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0) {
        const int len = r2_utf8_len(p);
        const unsigned cp = r2_utf8_cp(p, len);
        if (cp >= 0xe000u && cp <= 0xf8ffu) { p += len; continue; } /* 断片の印（私用領域）は日本語ではない */
        if ((cp >= 0x3000u && cp <= 0xffefu) || (cp >= 0x2200u && cp <= 0x22ffu)) return 1;
        p += len;
    }
    return 0;
}

static void r2_st_fullwidth(const char *ascii, char *out, size_t cap)
{
    size_t used = 0;
    for (; *ascii != 0 && used + 4 < cap; ++ascii) {
        if (*ascii >= '0' && *ascii <= '9') {
            out[used++] = (char)0xef; out[used++] = (char)0xbc; out[used++] = (char)(0x90 + (*ascii - '0'));
        } else if (*ascii == '+') {
            out[used++] = (char)0xef; out[used++] = (char)0xbc; out[used++] = (char)0x8b;
        } else if (*ascii == '-') {
            out[used++] = (char)0xe2; out[used++] = (char)0x88; out[used++] = (char)0x92;
        } else if (*ascii == ',') {
            out[used++] = (char)0xef; out[used++] = (char)0xbc; out[used++] = (char)0x8c;
        } else {
            out[used++] = *ascii;
        }
    }
    out[used] = 0;
}

/*
 * 日本語の語順の字を組み立てる。{Mn}＝断片の印（mesg[n] を英語にしたときの中身）、{Jn}＝起動時の日本語の mesg[n]、
 * {Fx}＝znum の全角の数（x は +1・-2・12 など）、{Px}＝（…）、{Bx}＝［…］、{Ta,b}＝「音節 音節 」。
 */
static void r2_st_build(const char *spec, char *out, size_t cap)
{
    size_t used = 0;
    out[0] = 0;
    while (*spec != 0 && used + 8 < cap) {
        if (*spec == '{') {
            const char kind = spec[1];
            const char *end = strchr(spec, '}');
            char arg[64];
            size_t n;
            if (end == 0) break;
            n = (size_t)(end - spec - 2);
            if (n >= sizeof(arg)) n = sizeof(arg) - 1;
            memcpy(arg, spec + 2, n);
            arg[n] = 0;
            if (kind == 'M') {
                used = r2_out_put(out, cap, used, mesg[atoi(arg)]);
            } else if (kind == 'J') {
                used = r2_out_put(out, cap, used, r2_mesg_ja[atoi(arg)]);
            } else if (kind == 'F') {
                char fw[64];
                r2_st_fullwidth(arg, fw, sizeof(fw));
                used = r2_out_put(out, cap, used, fw);
            } else if (kind == 'P' || kind == 'B') {
                char fw[64];
                r2_st_fullwidth(arg, fw, sizeof(fw));
                used = r2_out_put(out, cap, used, kind == 'P' ? "\xef\xbc\x88" : "\xef\xbc\xbb");
                used = r2_out_put(out, cap, used, fw);
                used = r2_out_put(out, cap, used, kind == 'P' ? "\xef\xbc\x89" : "\xef\xbc\xbd");
            } else if (kind == 'T') {
                char *comma;
                char *cursor = arg;
                used = r2_out_put(out, cap, used, "\xe3\x80\x8c");
                while (cursor != 0 && *cursor != 0) {
                    comma = strchr(cursor, ',');
                    if (comma != 0) *comma = 0;
                    used = r2_out_put(out, cap, used, r2_mesg_ja[atoi(cursor)]);
                    cursor = comma != 0 ? comma + 1 : 0;
                }
                used = r2_out_put(out, cap, used, "\xe3\x80\x8d");
            }
            spec = end + 1;
        } else {
            out[used++] = *spec++;
            out[used] = 0;
        }
    }
}

typedef struct r2_st_case {
    const char *spec;
    const char *want;
} r2_st_case;

static int r2_st_load_mesg(const char *path)
{
    FILE *fp = fopen(path, "r");
    char buf[1024];
    int loaded = 0;
    if (fp == 0) return 0;
    while (fgets(buf, sizeof(buf), fp) != 0) {
        const int n = atoi(buf);
        char *a, *b;
        if (n <= 0 || n >= 500) continue;
        a = strchr(buf, '"');
        if (a == 0) continue;
        b = strchr(a + 1, '"');
        if (b == 0) continue;
        *b = 0;
        snprintf(mesg[n], R2_MESSAGE_BYTES, "%s", a + 1);
        ++loaded;
    }
    fclose(fp);
    return loaded;
}

static int r2_st_specifiers(const char *s, char *out, size_t cap)
{
    size_t used = 0;
    for (; *s != 0; ++s) {
        if (*s == '%') {
            ++s;
            if (*s == '%') continue;
            if (used + 3 >= cap) break;
            out[used++] = '%';
            while (*s != 0 && strchr("-+ 0#.0123456789", *s) != 0) ++s;
            if (*s == 'l') ++s;
            if (*s == 0) break;
            out[used++] = *s;
        }
    }
    out[used] = 0;
    return (int)used;
}

int r2_i18n_selftest(const char *path)
{
    static const r2_st_case cases[] = {
        /* 持ち物の説明（原作の get_desc の英語版と同じ形） */
        { "{F2}{M32}{T454,455}{M33}{M3}", "2 scrolls titled 'blech foo'" },
        { "{T454,455}{M33}{M3}", "A scroll titled 'blech foo'" },
        { "{J334}{M4}", "A blue potion" },
        { "{F3}{M32}{J334}{M4}", "3 blue potions" },
        { "{M350}{M4}", "A potion of healing" },
        { "{F3}{M32}{M350}{M4}", "3 potions of healing" },
        { "{J410}{J39}{M5}", "A steel wand" },
        { "{M390}{M5}{B5}", "A wand of slow monster [5]" },
        { "{J440}{J40}{M8}", "A diamond ring" },
        { "{J425}{J39}", "teak" },
        { "{M405}{P+2}{M8}{M37}", "A +2 ring of dexterity (on left hand)" },
        { "{M362}{M3}", "A scroll of protect armor" },
        { "foo{M34}{M4}", "A potion called foo" },
        { "{F2}{M32} foo{M34}{M3}", "2 scrolls called foo" },
        { "{P+1,+2}{M379}{M35}", "A +1,+2 mace (weapon in hand)" },
        { "{F3}{M29}{M375}", "3 darts" },
        { "{M379}", "A mace" },
        { "{P+1}{M383}{B4}{M36}", "+1 ring mail [4] (being worn)" },
        { "{M383}", "Ring mail" },
        { "{M2}", "Some food" },
        { "{F3}{M30}{M2}", "3 rations of food" },
        { "{F1}{M30}{M2}", "Some food" },
        { "{F1}{M31}{J333}", "A slime-mold" },
        { "{J333}", "A slime-mold" },
        { "{F3}{M31}{J333}", "3 slime-molds" },
        { "{M27}", "The Amulet of Yendor" },
        /* 動詞が後ろに付く文 */
        { "{F37}{M28}{M69}", "You found 37 gold pieces" },
        { "{M379}{M69}(c)", "You now have a mace (c)" },
        { "{M379}{M92}", "Dropped a mace" },
        { "{M383}{M100}", "Wearing ring mail" },
        { "{M379}{M107}", "Wielding a mace" },
        { "{M401}{M8}{M166}", "Removed a ring of regeneration" },
        { "{M379}{M205}", "She stole a mace" },
        { "{M379}{M70}", "Moved onto a mace" },
        { "{M383}{M94}", "Was wearing ring mail" },
        /* 一覧の行 */
        { "d) {M379}{M36}", "d) A mace (being worn)" },
        { "  {M362}{M3}", "  Scroll of protect armor" },
        { "  foo{M34}{M3}", "  Scroll called foo" },
        { "  (No known {M3}s)", "  (No known scrolls)" },
        /* 得点表の行 */
        { " 1   1234   login: {M190}{F3}{M191}kobold{M197}\xe3\x80\x82", " 1   1234   login: killed by a kobold on level 3." },
        { " 1   1234   login: {M189}{M190}{F3}{M191}orc{M197}\xe3\x80\x82", " 1   1234   login: killed by an orc on level 3 with Amulet." },
        { " 1   1234   login: {M190}{F5}{M191}died of starvation\xe3\x80\x82", " 1   1234   login: died of starvation on level 5." },
        /* 平らな文・記号 */
        { "Your {J334}", "Your blue" },
        { "Welcome to level {F12}", "Welcome to level 12" },
        { "Au {F123}\xe3\x80\x82", "Au 123." },
        { "  \xef\xbc\x9d\xe3\x82\xb9\xe3\x83\x9a\xe3\x83\xbc\xe3\x82\xb9\xe3\x82\x92\xe6\x8a\xbc\xe3\x81\x97\xe3\x81\xa6\xe3\x81\x8f\xe3\x81\xa0\xe3\x81\x95\xe3\x81\x84\xef\xbc\x9d",
          "  --Press space to continue--" },
        { "\xe7\xb5\x82\xe4\xba\x86\xe3\x81\x99\xe3\x82\x8b\xe3\x81\x8b\xe3\x81\xa9\xe3\x81\x86\xe3\x81\x8b\xe7\xa2\xba\xe8\xaa\x8d\xe3\x82\x92\xe3\x81\xa8\xe3\x82\x8b", "Ask whether quit or not on quit signal" },
    };
    int failures = 0, i, n, untranslated = 0;
    static char ja_copy[R2_MESG_SLOTS][R2_MESSAGE_BYTES];
    char got[512], spec[512];

    if (path == 0 || r2_st_load_mesg(path) < 400) {
        fprintf(stderr, "[rogue2] i18n selftest FAIL: cannot read the message file %s\n", path != 0 ? path : "(none)");
        return 1;
    }
    r2_mesg_snapped = 0;
    r2_lang_applied_en = 0;
    r2_lang_want_en = 1;
    memcpy(ja_copy, mesg, sizeof(ja_copy));
    r2_language_apply();
    if (!r2_lang_applied_en) {
        fprintf(stderr, "[rogue2] i18n selftest FAIL: language was not applied\n");
        return 1;
    }

    /* 1. 覆い */
    for (n = 1; n < R2_MESG_SLOTS; ++n) {
        size_t k;
        int in_dict = 0;
        if (r2_mesg_ja[n][0] == 0) continue;
        for (k = 0; k < sizeof(r2_dict_en) / sizeof(r2_dict_en[0]); ++k) {
            if (r2_dict_en[k].index == n) in_dict = 1;
        }
        if (n == 39 || n == 40 || in_dict) continue; /* の（材質・宝石に付く）・辞書で英語にする字 */
        if (r2_st_has_cjk(mesg[n])) {
            fprintf(stderr, "[rogue2] i18n selftest FAIL: mesg[%d] still Japanese in English mode: %s\n", n, mesg[n]);
            ++untranslated;
            ++failures;
        }
    }
    for (i = 0; i < (int)(sizeof(r2_dict_en) / sizeof(r2_dict_en[0])); ++i) {
        if (r2_mesg_ja[r2_dict_en[i].index][0] == 0) {
            fprintf(stderr, "[rogue2] i18n selftest FAIL: dictionary entry %d has no Japanese key\n", r2_dict_en[i].index);
            ++failures;
        }
    }
    /* 2. 書式 */
    for (i = 0; i < (int)(sizeof(r2_mesg_en_table) / sizeof(r2_mesg_en_table[0])); ++i) {
        const int index = r2_mesg_en_table[i].index;
        char js[64], es[64];
        r2_st_specifiers(r2_mesg_ja[index], js, sizeof(js));
        r2_st_specifiers(r2_mesg_en_table[i].text, es, sizeof(es));
        if (strcmp(js, es) != 0 && index != 22 && index != 23) {
            fprintf(stderr, "[rogue2] i18n selftest FAIL: mesg[%d] format differs: ja[%s] en[%s]\n", index, js, es);
            ++failures;
        }
    }
    /* 3. 語順 */
    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); ++i) {
        r2_st_build(cases[i].spec, spec, sizeof(spec));
        if (!r2_localize_text(spec, got, sizeof(got))) snprintf(got, sizeof(got), "%s", spec);
        if (strcmp(got, cases[i].want) != 0 || r2_st_has_cjk(got)) {
            fprintf(stderr, "[rogue2] i18n selftest FAIL case %d: [%s] want [%s] got [%s]\n", i, cases[i].spec, cases[i].want, got);
            ++failures;
        }
    }
    /* 4. 日本語へ戻す */
    r2_lang_want_en = 0;
    r2_language_apply();
    if (r2_lang_applied_en) {
        fprintf(stderr, "[rogue2] i18n selftest FAIL: language stayed English\n");
        ++failures;
    }
    for (n = 1; n < R2_MESG_SLOTS; ++n) {
        if (strcmp(ja_copy[n], mesg[n]) != 0) {
            fprintf(stderr, "[rogue2] i18n selftest FAIL: mesg[%d] was not restored for Japanese: [%s]\n", n, mesg[n]);
            ++failures;
        }
    }
    {
        const char *japanese = "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";
        if (r2_localize_text(japanese, got, sizeof(got))) {
            fprintf(stderr, "[rogue2] i18n selftest FAIL: Japanese text was changed in Japanese mode\n");
            ++failures;
        }
        snprintf(spec, sizeof(spec), "a\xee\x80\x83" "b"); /* U+E003（mesg[3] の印）を日本語のときに見た場合 */
        if (!r2_localize_text(spec, got, sizeof(got)) || strcmp(got, "a\xe5\xb7\xbb\xe3\x81\x8d\xe7\x89\xa9" "b") != 0) {
            fprintf(stderr, "[rogue2] i18n selftest FAIL: a saved English fragment was not returned to Japanese: %s\n", got);
            ++failures;
        }
    }
    fprintf(stderr, "[rogue2] i18n selftest: %d untranslated, %d failures: %s\n", untranslated, failures, failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
