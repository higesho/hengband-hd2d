#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "rogue.h"
#include "save.h"
#include "init.h"
#include "r2_host.h"

/* tools/core_sources/recipes/rogue2.json の再構成で原作の file-static から外へ出した決定論状態。 */
short r2_move_left_cou = 0;
short r2_heal_c = 0;
boolean r2_heal_alt = 0;
boolean r2_search_reg = 0;
char r2_rand_pos[] = "\010\007\001\003\004\005\002\006\0";

extern boolean trap_door;

void r2_rng_get(long out[32], int *front, int *rear);
int r2_rng_set(const long in[32], int front, int rear);

#ifdef _WIN32
/* windows.h を取り込むと原作の名前（boolean など）とぶつかるので、使う kernel32 の関数だけを宣言する。 */
__declspec(dllimport) int __stdcall MoveFileExA(const char *existing, const char *new_name, unsigned long flags);
__declspec(dllimport) void *__stdcall CreateFileA(const char *name, unsigned long access, unsigned long share,
    void *security, unsigned long disposition, unsigned long flags, void *templ);
__declspec(dllimport) int __stdcall CloseHandle(void *handle);
__declspec(dllimport) int __stdcall SetFileInformationByHandle(void *handle, int info_class, void *info, unsigned long size);
__declspec(dllimport) unsigned long __stdcall GetLastError(void);
__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned code_page, unsigned long flags, const char *src, int src_len,
    unsigned short *dst, int dst_len);
__declspec(dllimport) unsigned long __stdcall GetFullPathNameW(const unsigned short *name, unsigned long len,
    unsigned short *buffer, unsigned short **file_part);
#define r2_move_file_ex_a MoveFileExA
#endif

static char g_autosave_path[4096];
static int g_autosave_busy = 0;
/* 置き換えに失敗した保存を完全な形で残してあるか（Windows だけ。r2_autosave_turn の説明）。 */
static int g_autosave_pending = 0;

enum { R2_TERM_ROWS = 24, R2_TERM_COLS = 80, R2_TERM_CELLS = R2_TERM_ROWS * R2_TERM_COLS };

struct r2_extra_body {
    long rng[32];
    int rng_front;
    int rng_rear;
    short move_left_cou;
    short heal_c;
    boolean heal_alt;
    boolean search_reg;
    boolean pending_trap_door;
    char rand_pos[9];
    uint32_t term_codepoints[R2_TERM_CELLS];
    uint32_t term_attrs[R2_TERM_CELLS];
    int term_cursor_y;
    int term_cursor_x;
    uint32_t term_current_attr;
};

int r2_term_export(uint32_t *codepoints, uint32_t *attrs, int count,
    int *cursor_y, int *cursor_x, uint32_t *current_attr);
int r2_term_import(const uint32_t *codepoints, const uint32_t *attrs, int count,
    int cursor_y, int cursor_x, uint32_t current_attr);

void r2_autosave_set_path(const char *path)
{
    if (path == 0) path = "";
    strncpy(g_autosave_path, path, sizeof(g_autosave_path) - 1);
    g_autosave_path[sizeof(g_autosave_path) - 1] = 0;
}

/*
 * 得点ファイル（.rogue.scores）の置き場。原作の md_ghome() は HOME か cwd を返すので、
 * Windows では exe の隣や起動元のフォルダに散らばっていた。自動セーブと同じフォルダ
 * （<data root>/rogue2）に置く。
 *
 * 原作の put_scores() は、この置き場に ".rogue.scores" を足したパスを char file[100] へ
 * strcpy する（長さの確認が無い）。データの置き場の絶対パスが 77 バイトを超えると file[] を
 * あふれ、ほかの局所変数を壊して、得点が書かれない・「得点ファイルが正しくありません。」の
 * fatal で終わる（未定義動作。Windows のゲートのサンドボックスや、深いフォルダに置いた版で起きる）。
 * 原作は書き換えられないので、原作には短い目印（R2_HOME_TOKEN）だけを返し、実際のパスへの
 * 置き換えは fopen の代役 r2_fopen_redirect()（r2_build_config.h の fopen マクロから呼ぶ）で行う。
 */
#define R2_HOME_TOKEN "@r2home/"

static char g_home_real[4096];

static void home_real_refresh(void)
{
    size_t n = strlen(g_autosave_path);
    while (n > 0 && g_autosave_path[n - 1] != '/' && g_autosave_path[n - 1] != '\\') --n;
    memcpy(g_home_real, g_autosave_path, n);
    g_home_real[n] = 0;
}

const char *r2_home_dir(void)
{
    home_real_refresh();
    if (g_home_real[0] == 0) return "";
    return R2_HOME_TOKEN;
}

/* 目印で始まるパスだけ実際の置き場に置き換える。それ以外は素通し。 */
#undef fopen
FILE *r2_fopen_redirect(const char *path, const char *mode)
{
    const size_t token_len = sizeof(R2_HOME_TOKEN) - 1;
    FILE *fp;
    char *real;
    size_t real_len, rest_len;
    if (path == 0 || strncmp(path, R2_HOME_TOKEN, token_len) != 0 || g_home_real[0] == 0) {
        return fopen(path, mode);
    }
    real_len = strlen(g_home_real);
    rest_len = strlen(path + token_len);
    real = (char *)malloc(real_len + rest_len + 1);
    if (real == 0) return 0;
    memcpy(real, g_home_real, real_len);
    memcpy(real + real_len, path + token_len, rest_len + 1);
    fp = fopen(real, mode);
    free(real);
    return fp;
}

static int autosave_ready(void)
{
    return g_autosave_path[0] != 0 && !g_autosave_busy;
}

#ifdef _WIN32
/*
 * Windows の置き換え。別のプロセス（ウイルス対策・同期ソフト・バックアップ）が autosave を開いている最中でも保存が失われず、
 * 置き換えの最中に autosave が見えなくなる瞬間も無いようにする（2026-10-23。小さな試験 replace_gap3.py で 3 つを比べた）。
 *
 *   MoveFileExA（従来）  別のプロセスが開いていると、共有の指定によらず ERROR_ACCESS_DENIED で失敗する（stat しているだけでも
 *                        4 割ほど失敗）。失敗するとそのターンの保存が黙って飛んだ。
 *   ReplaceFileW         置き換えの最中に元のファイルが約 5% の確率で見えなくなる（MoveFileEx より悪い）。
 *   SetFileInformationByHandle(FileRenameInfoEx, REPLACE_IF_EXISTS | POSIX_SEMANTICS)
 *                        Linux の rename と同じ意味で置き換える。見えなくなる瞬間が無く、相手が FILE_SHARE_DELETE で
 *                        開いていても成功する（Windows 10 1709 以降）。**これを使う**。使えない版では MoveFileExA へ戻る。
 * 相手が FILE_SHARE_DELETE なしで開いているときだけは、どの方法でも置き換えられない。そのときは短い間隔でやり直し、
 * それでも駄目なら呼び出し側が完全な別名（.pending）へ残す。
 */
static int r2_rename_posix(const char *from, const char *to, unsigned long *err)
{
    struct {
        unsigned long flags;
        void *root;
        unsigned long length;   /* 名前のバイト数（終端の NUL を含まない） */
        unsigned short name[4100];
    } info;
    unsigned short wide[4100];
    unsigned short full[4100];
    unsigned long full_len;
    void *handle;
    int ok;
    if (MultiByteToWideChar(0, 0, to, -1, wide, 4100) <= 0) { *err = 87; return 0; }
    full_len = GetFullPathNameW(wide, 4100, full, 0);
    if (full_len == 0 || full_len >= 4100) { *err = 87; return 0; }
    memcpy(info.name, full, (full_len + 1) * sizeof(unsigned short));
    info.flags = 0x1u | 0x2u;   /* FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS */
    info.root = 0;
    info.length = full_len * (unsigned long)sizeof(unsigned short);
    /* DELETE | FILE_READ_ATTRIBUTES、共有は READ|WRITE|DELETE、OPEN_EXISTING */
    handle = CreateFileA(from, 0x10000u | 0x80u, 7u, 0, 3u, 0x80u, 0);
    if (handle == (void *)(long)-1) { *err = GetLastError(); return 0; }
    /* FileRenameInfoEx = 22。大きさは先頭の 3 項目＋名前（終端の NUL を含む） */
    ok = SetFileInformationByHandle(handle, 22, &info,
        (unsigned long)(offsetof(__typeof__(info), name) + info.length + sizeof(unsigned short)));
    if (!ok) *err = GetLastError();
    CloseHandle(handle);
    return ok;
}

static int g_posix_rename_unusable = 0;

/* from を to へ置き換える。成功で 1。共有違反・拒否は短い間隔で数回やり直す（約 40 ミリ秒まで）。from が無いときは直ちに 0。 */
static int r2_replace_file(const char *from, const char *to)
{
    int attempt;
    for (attempt = 0; attempt < 5; ++attempt) {
        unsigned long err = 0;
        int ok = 0;
        if (!g_posix_rename_unusable) {
            ok = r2_rename_posix(from, to, &err);
            /* 古い Windows（FileRenameInfoEx が無い）。以後は MoveFileExA だけを使う */
            if (!ok && (err == 87 || err == 1 || err == 50 || err == 120)) g_posix_rename_unusable = 1;
        }
        if (!ok && g_posix_rename_unusable) {
            ok = r2_move_file_ex_a(from, to, 0x1u | 0x8u);
            if (!ok) err = GetLastError();
        }
        if (ok) return 1;
        if (err == 2) return 0;   /* from が無い（保存が失敗して消された） */
        if (attempt + 1 < 5) Sleep(10);
    }
    return 0;
}
#endif

void r2_autosave_turn(void)
{
    if (!autosave_ready()) return;
    g_autosave_busy = 1;
    /*
     * 別の名前に書き終えてから置き換える。直接書くと、書いている途中で止められた（強制終了・電源断）ときに途中までのファイルが残り、
     * 次の起動の restore() が「ファイルが読めません」で終わってしまう（以後も毎回。2026-10-22、関門の protocol_audit.py が 8 回に 1 回、
     * 強制終了の直後の再起動で再現した）。POSIX の rename() は原子的に置き換える。Windows は r2_replace_file（上の説明）。
     */
    {
        char tmp[sizeof(g_autosave_path) + 16];
        snprintf(tmp, sizeof(tmp), "%s.tmp", g_autosave_path);
        save_into_file(tmp);
#ifdef _WIN32
        if (r2_replace_file(tmp, g_autosave_path)) {
            if (g_autosave_pending) {
                char pending[sizeof(g_autosave_path) + 16];
                snprintf(pending, sizeof(pending), "%s.pending", g_autosave_path);
                (void)remove(pending);   /* 追いついた。古い残りは要らない */
                g_autosave_pending = 0;
            }
        } else {
            /*
             * 別のプロセスが autosave を（共有を許さずに）開いていて、やり直しても置き換えられなかった。
             * .tmp は save_into_file が最後まで書き終えた完全なものなので、.pending という名前にして残す
             * （.tmp のままだと、書いている途中で止まったものと見分けがつかない）。次の保存が成功すれば消し、
             * 失敗が続いても最新の完全な保存が残る。次の起動は r2_autosave_recover で、autosave より新しければ使う。
             * .tmp が無いのは保存の失敗（save_into_file が消した）なので、何も残さない。
             */
            char pending[sizeof(g_autosave_path) + 16];
            snprintf(pending, sizeof(pending), "%s.pending", g_autosave_path);
            if (r2_move_file_ex_a(tmp, pending, 0x1u | 0x8u)) {
                g_autosave_pending = 1;
            }
        }
#else
        if (rename(tmp, g_autosave_path) != 0) {
            (void)remove(tmp);
        }
#endif
    }
    g_autosave_busy = 0;
}

/*
 * 起動時（restore の前）。置き換えに失敗したまま終わった .pending（完全な保存）が autosave より新しい、
 * または autosave が無いときは、それを autosave にして使う。古ければ捨てる。Windows 以外は何もしない。
 */
void r2_autosave_recover(const char *path)
{
#ifdef _WIN32
    char pending[4096 + 16];
    struct stat st_pending, st_save;
    int have_save;
    if (path == 0 || path[0] == 0) return;
    snprintf(pending, sizeof(pending), "%s.pending", path);
    if (stat(pending, &st_pending) != 0) return;
    have_save = stat(path, &st_save) == 0;
    if (!have_save || st_pending.st_mtime >= st_save.st_mtime) {
        if (!r2_replace_file(pending, path)) {
            /* 置き換えられなかった（相手が開いたまま）。.pending は残す。このまま始めると古い autosave から再開する。 */
            fprintf(stderr, "[rogue2] could not apply %s (autosave is locked)\n", pending);
        }
        return;
    }
    (void)remove(pending);
#else
    (void)path;
#endif
}

void r2_autosave_boundary(void)
{
    r2_autosave_turn();
}

void r2_autosave_invalidate(void)
{
    if (g_autosave_path[0] != 0) {
        (void)remove(g_autosave_path);
    }
}

/*
 * 設定画面の値を 1 行 1 項目で保存する。真偽値は "name=0/1"、文字列（fruit / name / map）は
 * "name=値"。原作はこれらを環境変数 ROGUEOPTS からしか読まず、セーブにも入れないため、
 * 保存しないと起動のたびに既定値へ戻る（3.6 / 5.4 はセーブに入るので残る）。
 */
static char g_options_path[4096];

extern char login_name[];

void r2_options_set_path(const char *path)
{
    if (path == 0) path = "";
    strncpy(g_options_path, path, sizeof(g_options_path) - 1);
    g_options_path[sizeof(g_options_path) - 1] = 0;
}

/*
 * 文字列の値を原作と同じ経路（set_opts → env_get_value）で反映する。原作の規則——
 * name の ':' は ';' に替える（得点ファイルの区切りだから。init.c 582-584）、30 バイトで
 * 打ち切る、md_malloc した領域を cp へ入れる——がそのまま効く。
 */
static void apply_string_option(const opt *op, const char *value)
{
    char line[BUFSIZ];
    if (op == 0 || op->name == 0 || op->cp == 0 || value == 0) return;
    snprintf(line, sizeof(line), ",%s:%s", op->name, value);
    set_opts(line);
}

void r2_options_apply_string(int index, const char *value)
{
    int i;
    for (i = 0; i < index; ++i) {
        if (envopt[i].name == 0) return;
    }
    if (envopt[index].name == 0) return;
    apply_string_option(&envopt[index], value);
    r2_options_save();
}

void r2_options_load(void)
{
    FILE *fp;
    char line[512];
    if (g_options_path[0] == 0 || (fp = fopen(g_options_path, "r")) == 0) return;
    while (fgets(line, sizeof(line), fp) != 0) {
        char *eq = strchr(line, '=');
        opt *op;
        size_t vlen;
        if (eq == 0) continue;
        *eq = 0;
        vlen = strlen(eq + 1);
        while (vlen > 0 && (eq[1 + vlen - 1] == '\n' || eq[1 + vlen - 1] == '\r')) {
            eq[1 + --vlen] = 0;
        }
        for (op = envopt; op->name; op++) {
            if (strcmp(op->name, line) != 0) continue;
            if (op->bp != 0) {
                *(op->bp) = (eq[1] == '1');
            } else if (op->cp != 0 && vlen > 0) {
                /* 値は set_opts() の書式では ',' が区切りなので、そこまでしか読めない。 */
                apply_string_option(op, eq + 1);
            }
        }
    }
    fclose(fp);
}

void r2_options_save(void)
{
    FILE *fp;
    opt *op;
    if (g_options_path[0] == 0 || (fp = fopen(g_options_path, "w")) == 0) return;
    for (op = envopt; op->name; op++) {
        if (op->bp != 0) {
            fprintf(fp, "%s=%d\n", op->name, *(op->bp) ? 1 : 0);
        } else if (op->cp != 0 && *(op->cp) != 0 && (*(op->cp))[0] != 0) {
            /*
             * name が PlayerIdentity の名前（login_name）と同じなら書かない。書くと、
             * あとでアプリ側の名前を変えてもこのファイルの古い名前が勝ってしまう。
             * 設定画面で別の名前にしたときだけ残す。
             */
            if (strcmp(op->name, "name") == 0 && strcmp(*(op->cp), login_name) == 0) continue;
            /* 値は options() の入力行そのもの（改行を含まない 1 行）なのでそのまま書く。 */
            fprintf(fp, "%s=%s\n", op->name, *(op->cp));
        }
    }
    fclose(fp);
}

/* 得点表を見るだけの分岐（rogue2.json の再構成が put_scores() に足した）。 */
int r2_score_view_only = 0;

void put_scores(object *monster, short other);

void r2_show_scores(void)
{
    r2_score_view_only = 1;
    put_scores((object *)0, 0);
    r2_score_view_only = 0;
}

/*
 * 原作は SIGINT（Ctrl-C）→ onintr() で休息・探索・走行を中断する。パイプ越しには
 * シグナルが来ないので、画面側から届いたキャンセル（Esc / Ctrl-C）を 1 ターンごとに見る。
 * 画面側が終了を求めているときも同じく中断して、次のキー待ちで終了処理へ進める。
 */
void r2_interrupt_poll(void)
{
    if (r2_host_take_interrupt() || r2_host_shutdown_requested()) {
        onintr(0);
    }
}

void r2_hd2d_quit(void)
{
    r2_autosave_boundary();
    clean_up("");
}

/*
 * 弓（BOW）の威力の文字列は、セーブ後に別プロセスで読むとポインタが無効になるので、
 * 再構成した save.c の復元が武器の種類の表から引き直す。その表では弓は "1d1" だが、
 * 開始装備の弓だけは原作が "1d2" を直接入れる。引き直しで威力が変わらないよう、
 * 弓ごとの威力をここで別に保存し、読み込み後に戻す（床の物 → 持ち物の順）。
 */
#define R2_BOW_DAMAGE_MAX 4096

static object *r2_bow_list_head(int which)
{
    return which == 0 ? &level_objects : &rogue.pack;
}

static int r2_is_bow(const object *o)
{
    return o->what_is == WEAPON && o->which_kind == BOW;
}

static void r2_save_bow_damage(FILE *fp)
{
    uint32_t count = 0;
    int which;
    object *o;
    for (which = 0; which < 2; ++which) {
        for (o = r2_bow_list_head(which)->next_object; o; o = o->next_object) {
            if (r2_is_bow(o)) ++count;
        }
    }
    if (count > R2_BOW_DAMAGE_MAX) count = R2_BOW_DAMAGE_MAX;
    r_write(fp, (char *)&count, (int)sizeof(count));
    for (which = 0; which < 2; ++which) {
        for (o = r2_bow_list_head(which)->next_object; o && count > 0; o = o->next_object) {
            char d[4] = { '1', 'd', '1', 0 };
            if (!r2_is_bow(o)) continue;
            if (o->damage && o->damage[0] == '1' && o->damage[1] == 'd' && o->damage[2] == '2') d[2] = '2';
            r_write(fp, d, (int)sizeof(d));
            --count;
        }
    }
}

static void r2_load_bow_damage(FILE *fp)
{
    static char d1[] = "1d1";
    static char d2[] = "1d2";
    uint32_t count = 0, i = 0;
    int which;
    object *o;
    r_read(fp, (char *)&count, (int)sizeof(count));
    if (count > R2_BOW_DAMAGE_MAX) {
        r2_autosave_invalidate();
        clean_up("HD2D自動セーブの弓の威力の記録が壊れています。");
        return;
    }
    for (which = 0; which < 2; ++which) {
        for (o = r2_bow_list_head(which)->next_object; o && i < count; o = o->next_object) {
            char d[4];
            if (!r2_is_bow(o)) continue;
            r_read(fp, d, (int)sizeof(d));
            ++i;
            o->damage = (d[2] == '2') ? d2 : d1;
        }
    }
    /* 保存した数のほうが多い（読めなかった分）ときも、ファイルの位置をそろえる。 */
    for (; i < count; ++i) {
        char d[4];
        r_read(fp, d, (int)sizeof(d));
    }
}

void r2_save_extra(void *raw_fp)
{
    FILE *fp = (FILE *)raw_fp;
    static const unsigned char magic[8] = { 'R','2','H','D','2','D','5',0 };
    const unsigned long version = 5;
    struct r2_extra_body state;
    memset(&state, 0, sizeof(state));
    r2_rng_get(state.rng, &state.rng_front, &state.rng_rear);
    state.move_left_cou = r2_move_left_cou;
    state.heal_c = r2_heal_c;
    state.heal_alt = r2_heal_alt;
    state.search_reg = r2_search_reg;
    state.pending_trap_door = trap_door;
    memcpy(state.rand_pos, r2_rand_pos, sizeof(state.rand_pos));
    if (r2_term_export(state.term_codepoints, state.term_attrs, R2_TERM_CELLS,
            &state.term_cursor_y, &state.term_cursor_x, &state.term_current_attr) != 0) {
        return;
    }
    /*
     * 版番号を本文より先に独立して置く。旧版を読み込んだ時に、大きくなった本文を
     * 読もうとして EOF へ進む前に破棄できる。
     */
    r_write(fp, (char *)magic, (int)sizeof(magic));
    r_write(fp, (char *)&version, (int)sizeof(version));
    r_write(fp, (char *)&state, (int)sizeof(state));
    r2_save_bow_damage(fp);
}

void r2_load_extra(void *raw_fp)
{
    FILE *fp = (FILE *)raw_fp;
    unsigned char magic[8];
    unsigned long version = 0;
    struct r2_extra_body state;
    memset(&state, 0, sizeof(state));
    r_read(fp, (char *)magic, (int)sizeof(magic));
    r_read(fp, (char *)&version, (int)sizeof(version));
    /* 版 4（弓の威力の記録が無い古いセーブ）も読む。その弓は従来どおり表の値になる。 */
    if (!((memcmp(magic, "R2HD2D4", 7) == 0 && version == 4) ||
          (memcmp(magic, "R2HD2D5", 7) == 0 && version == 5))) {
        r2_autosave_invalidate();
        clean_up("HD2D自動セーブの追加状態が一致しません。");
        return;
    }
    r_read(fp, (char *)&state, (int)sizeof(state));
    if (!r2_rng_set(state.rng, state.rng_front, state.rng_rear)) {
        r2_autosave_invalidate();
        clean_up("HD2D自動セーブの乱数状態が壊れています。");
        return;
    }
    r2_move_left_cou = state.move_left_cou;
    r2_heal_c = state.heal_c;
    r2_heal_alt = state.heal_alt;
    r2_search_reg = state.search_reg;
    trap_door = state.pending_trap_door;
    memcpy(r2_rand_pos, state.rand_pos, sizeof(state.rand_pos));
    if (r2_term_import(state.term_codepoints, state.term_attrs, R2_TERM_CELLS,
            state.term_cursor_y, state.term_cursor_x, state.term_current_attr) != 0) {
        r2_autosave_invalidate();
        clean_up("HD2D自動セーブの画面状態が壊れています。");
        return;
    }
    if (version == 5) {
        r2_load_bow_damage(fp);
    }
}
