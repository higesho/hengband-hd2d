/* NetHack のコアの画面の実装（C）。。
 *
 * 原作のヘッダ（hack.h）を読むのはこのファイルだけ。原作は書き換えず、次の物をここで用意する。
 * - 原作の windows.c は SHIM_GRAPHICS のとき外部の shim_procs を "shim" の名で登録する。
 *   その shim_procs をここで定義する（原作の win/shim/winshim.c は組まない）。
 * - 原作の Windows の入口 sys/windows/windmain.c は文字端末か GUI がある前提なので組まない。
 *   ほかのファイルが windmain.c から使う getlock ほか 5 つ、consoletty.c の GUILaunched、
 *   windmain.c の portable、そして入口（nh_game_main）を、ここで書く。
 *   Linux では原作の sys/unix/unixunix.c の getlock などをそのまま組み、原作の unixmain.c・unixtty.c が持つ
 *   関数は nh_unix.c に書く（Windows 専用の所は #ifdef _WIN32 で分けてある）。
 * - 原作の set_default_prefix_locations（windsys.c）は呼ばない。呼ぶと遊ぶ人のプロファイルの下
 *   （%USERPROFILE%\NetHack\ など）にフォルダを作ってしまうので、置き場はすべて自分で決める。
 * 画面の関数は、原作の型を素の値に直して nh_host.h の関数で C++ の側（nh_main.cpp）へ渡す。
 * メニューの操作・問い・文字入力・キーの待ちは C++ の側が行う。
 */
#include "hack.h"
#include "func_tab.h"
#include "dlb.h"
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <direct.h>
#define NH_PATH_SEP '\\'
#define nh_strdup _strdup
#define nh_getpid _getpid
#else
/* Linux。Windows の _strdup・_getpid の代わりと、Linux だけで要る関数 nh_unix_prepare（nh_unix.c）の宣言 */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#define NH_PATH_SEP '/'
#define nh_strdup strdup
#define nh_getpid getpid
extern int nh_unix_prepare(const char *dir);
#endif
#include "nh_host.h"

/* nh_host.h の番号が原作と合っているか（原作が替わったらここで落ちる） */
#define NH_SAME(a, b) ((int) (a) == (int) (b))
_Static_assert(NH_SAME(NH_PICK_NONE, PICK_NONE) && NH_SAME(NH_PICK_ONE, PICK_ONE)
                   && NH_SAME(NH_PICK_ANY, PICK_ANY),
               "pick mode");
_Static_assert(NH_SAME(NH_BL_TITLE, BL_TITLE) && NH_SAME(NH_BL_ALIGN, BL_ALIGN)
                   && NH_SAME(NH_BL_GOLD, BL_GOLD) && NH_SAME(NH_BL_ENE, BL_ENE)
                   && NH_SAME(NH_BL_ENEMAX, BL_ENEMAX) && NH_SAME(NH_BL_XP, BL_XP)
                   && NH_SAME(NH_BL_AC, BL_AC) && NH_SAME(NH_BL_HP, BL_HP)
                   && NH_SAME(NH_BL_HPMAX, BL_HPMAX) && NH_SAME(NH_BL_LEVELDESC, BL_LEVELDESC)
                   && NH_SAME(NH_BL_CONDITION, BL_CONDITION) && NH_SAME(NH_BL_MAX, MAXBLSTATS),
               "status field");

/* 地図の大きさと、地形の表示記号の並び（nh_terrain.cpp の表が前提にする）が原作と合っているか */
_Static_assert(NH_MAP_COLS == COLNO && NH_MAP_ROWS == ROWNO, "map size");
_Static_assert(MAXPCHARS == 105 && S_stone == 0 && S_arrow_trap == 49 && S_vbeam == 74
                   && S_expl_br == 104,
               "cmap numbering (nh_terrain.cpp)");

#ifdef _WIN32
/* ---- 原作では windmain.c・consoletty.c にある、ほかのファイルが使う物 ---- */

/* windsys.c が参照する */
boolean getreturn_enabled = FALSE;
/* mdlib.c と windsys.c が参照する */
int GUILaunched = FALSE;
boolean portable = FALSE;
#endif

/* ---- 置き場 ---- */

static char g_datadir[BUFSZ];
static char g_savedir[BUFSZ];

static void
copy_dir(char *dst, size_t size, const char *src)
{
    size_t n;

    Snprintf(dst, size, "%s", src);
    n = strlen(dst);
    if (n > 0 && dst[n - 1] != '\\' && dst[n - 1] != '/' && n + 1 < size) {
        dst[n] = NH_PATH_SEP;
        dst[n + 1] = '\0';
    }
}

/* データ（DATAPREFIX・HACKPREFIX・SYSCONFPREFIX）は読むだけの場所、ほかは書く場所 */
static void
set_prefixes(void)
{
    int i;

    for (i = 0; i < PREFIX_COUNT; i++)
        gf.fqn_prefix[i] = g_savedir;
    gf.fqn_prefix[HACKPREFIX] = g_datadir;
    gf.fqn_prefix[DATAPREFIX] = g_datadir;
    gf.fqn_prefix[SYSCONFPREFIX] = g_datadir;
}

/* ---- 窓の管理（メニューと文字の窓の中身を溜める） ---- */

#define NH_MAXWIN 32

struct nhwin {
    int used;
    int type;
    nh_menu_item *items;
    anything *ids;
    int n;
    int cap;
    char *prompt;
    /* outrip が墓標の材料を入れた窓（nethack-ui の rip）。その後の putstr は墓標の下の行になる */
    int is_rip;
    char rip_name[BUFSZ];
    char rip_killer[BUFSZ];
    long rip_gold;
    int rip_year;
    /* メニューの窓に putstr だけで行を積んだ（add_menu が無い）か。原作の GUI 版はこれを文字の窓として出す */
    int nputstr;
    int nadd;
    /* 持ち物の常設の窓（start_menu が MENU_BEHAVE_PERMINV）。nethack-ui の win type=invent。返事は待たない */
    int perminv;
    int perminv_announced;
};
static struct nhwin g_wins[NH_MAXWIN];

static struct nhwin *
getwin(winid w)
{
    if (w <= 0 || w >= NH_MAXWIN || !g_wins[w].used)
        return (struct nhwin *) 0;
    return &g_wins[w];
}

static void
win_clear_items(struct nhwin *nw)
{
    int i;

    nw->nputstr = 0;
    nw->nadd = 0;
    for (i = 0; i < nw->n; i++)
        free((genericptr_t) nw->items[i].text);
    nw->n = 0;
    if (nw->prompt) {
        free((genericptr_t) nw->prompt);
        nw->prompt = (char *) 0;
    }
}

static void
win_add_item(struct nhwin *nw, const anything *id, char ch, char gch, int attr, int clr,
             int preselected, const char *text, int glyph)
{
    nh_menu_item *it;

    if (nw->n >= nw->cap) {
        int cap = nw->cap ? nw->cap * 2 : 32;

        nw->items = (nh_menu_item *) realloc(nw->items, (size_t) cap * sizeof *nw->items);
        nw->ids = (anything *) realloc(nw->ids, (size_t) cap * sizeof *nw->ids);
        if (!nw->items || !nw->ids)
            panic("nh_winport: out of memory");
        nw->cap = cap;
    }
    it = &nw->items[nw->n];
    it->selectable = (id && id->a_void) ? 1 : 0;
    it->selector = ch;
    it->group = gch;
    it->attr = attr;
    it->color = clr;
    it->preselected = preselected;
    it->glyph = glyph;
    if (id)
        nw->nadd++; /* add_menu 経由（putstr は id が NULL） */
    it->text = nh_strdup(text ? text : "");
    if (id)
        nw->ids[nw->n] = *id;
    else
        memset(&nw->ids[nw->n], 0, sizeof nw->ids[nw->n]);
    nw->n++;
}

/* ---- 窓の関数 ---- */

static void
p_init_nhwindows(int *argc UNUSED, char **argv UNUSED)
{
    iflags.window_inited = TRUE;
}

/*
 * nethack-ui（画面と申告し合ったとき）のキャラクタ作成。原作の genl_player_setup（yn_function と確認のメニューで問う）の
 * 代わりに、役割・種族・性別・属性の表と有効な組み合わせの表を 1 度に画面へ送り、選びを受ける。
 * 画面が選んだ番号は flags.initrole などへ入れる。-1 のところはおまかせで、原作の「ゲームに選ばせる」道
 * （genl_player_setup の pick4u が y か a のとき）と同じ関数で、ほかの選びに合う物を選ぶ。
 */
static nh_select_table g_sel_table; /* 大きいので static */

static void
build_select_table(nh_select_table *t)
{
    int i, role, race, gend, algn;

    memset(t, 0, sizeof *t);
    /* 日本語版（NetHackJP）の内部の名前は英語のままなので、表示用の関数で日本語にする（原作の GUI 版の mhdlg.c と同じ） */
    for (i = 0; roles[i].name.m && t->nroles < NH_SEL_MAX_ROLES; i++) {
#ifdef NH_LANG_JA
        t->role_name[t->nroles] = jp_role_name_for_display(i, 0);
        t->role_name_f[t->nroles] = roles[i].name.f ? jp_role_name_for_display(i, 1) : (const char *) 0;
#else
        t->role_name[t->nroles] = roles[i].name.m;
        t->role_name_f[t->nroles] = roles[i].name.f;
#endif
        t->nroles++;
    }
    for (i = 0; races[i].noun && t->nraces < NH_SEL_MAX_RACES; i++) {
#ifdef NH_LANG_JA
        t->race_name[t->nraces] = jp_race_noun_for_display(i);
        t->race_adj[t->nraces] = jp_race_noun_for_display(i);
#else
        t->race_name[t->nraces] = races[i].noun;
        t->race_adj[t->nraces] = races[i].adj;
#endif
        t->nraces++;
    }
    for (i = 0; i < ROLE_GENDERS && i < NH_SEL_MAX_GENDERS; i++) {
#ifdef NH_LANG_JA
        t->gender_name[t->ngenders++] = jp_gender_for_display(i);
#else
        t->gender_name[t->ngenders++] = genders[i].adj;
#endif
    }
    for (i = 0; i < ROLE_ALIGNS && i < NH_SEL_MAX_ALIGNS; i++) {
#ifdef NH_LANG_JA
        t->align_name[t->naligns++] = jp_align_for_display(i);
#else
        t->align_name[t->naligns++] = aligns[i].adj;
#endif
    }
    for (role = 0; role < t->nroles; role++)
        for (race = 0; race < t->nraces; race++)
            for (gend = 0; gend < t->ngenders; gend++)
                for (algn = 0; algn < t->naligns; algn++) {
                    if (!ok_role(role, race, gend, algn) || !ok_race(role, race, gend, algn)
                        || !ok_gend(role, race, gend, algn) || !ok_align(role, race, gend, algn))
                        continue;
                    if (t->ncombos >= NH_SEL_MAX_COMBOS)
                        continue;
                    t->combos[t->ncombos][0] = (unsigned char) role;
                    t->combos[t->ncombos][1] = (unsigned char) race;
                    t->combos[t->ncombos][2] = (unsigned char) gend;
                    t->combos[t->ncombos][3] = (unsigned char) algn;
                    t->ncombos++;
                }
    t->pick[0] = flags.initrole;
    t->pick[1] = flags.initrace;
    t->pick[2] = flags.initgend;
    t->pick[3] = flags.initalign;
    t->name = svp.plname;
}

/* 0 以上の番号のうち、範囲外の物は -1（おまかせ）にする */
static int
sel_index(int v, int limit)
{
    return (v >= 0 && v < limit) ? v : ROLE_NONE;
}

/* 決まっていない facet を、決まっている物に合うように埋める（genl_player_setup の pick4u == 'y' の道と同じ関数） */
static void
fill_random_facets(void)
{
    int k;

    rigid_role_checks();
    if (flags.initrole < 0) {
        k = pick_role(flags.initrace, flags.initgend, flags.initalign, PICK_RANDOM);
        if (k < 0)
            k = randrole(FALSE);
        flags.initrole = k;
    }
    if (flags.initrace < 0 || !validrace(flags.initrole, flags.initrace)) {
        k = pick_race(flags.initrole, flags.initgend, flags.initalign, PICK_RANDOM);
        if (k < 0)
            k = randrace(flags.initrole);
        flags.initrace = k;
    }
    if (flags.initgend < 0 || !validgend(flags.initrole, flags.initrace, flags.initgend)) {
        k = pick_gend(flags.initrole, flags.initrace, flags.initalign, PICK_RANDOM);
        if (k < 0)
            k = randgend(flags.initrole, flags.initrace);
        flags.initgend = k;
    }
    if (flags.initalign < 0 || !validalign(flags.initrole, flags.initrace, flags.initalign)) {
        k = pick_align(flags.initrole, flags.initrace, flags.initgend, PICK_RANDOM);
        if (k < 0)
            k = randalign(flags.initrole, flags.initrace);
        flags.initalign = k;
    }
}

static void
p_player_selection(void)
{
    int result[4];

    if (!nh_host_ui_enabled()) {
        /* 原作の共通の実装。yn_function と確認のメニューで問う */
        genl_player_selection();
        return;
    }
    /* 設定ファイルなどで決まっている物があれば、先にそれに合わせる（原作と同じ） */
    rigid_role_checks();
    build_select_table(&g_sel_table);
    if (!nh_host_select(&g_sel_table, result)) {
        /* やめる: 遊ばずに終える（genl_player_selection と同じ） */
        nh_terminate(EXIT_SUCCESS);
        return;
    }
    flags.initrole = sel_index(result[0], g_sel_table.nroles);
    flags.initrace = sel_index(result[1], g_sel_table.nraces);
    flags.initgend = sel_index(result[2], g_sel_table.ngenders);
    flags.initalign = sel_index(result[3], g_sel_table.naligns);
    fill_random_facets();
}

/* 名前の問い。tty の tty_askname と同じく、空なら聞き直し、ESC なら終える */
static void
p_askname(void)
{
    char buf[BUFSZ];
    int tries = 0;
    size_t n;
    const char *query = nh_host_lang_is_ja() ? "あなたの名前は?" : "Who are you?";

    for (;;) {
        buf[0] = '\0';
        if (!nh_host_ask_text(NH_ASK_NAME, query, buf, PL_NSIZ, (const char *const *) 0, 0)) {
            /* ESC: 遊ばずに終える */
            exit_nhwindows((char *) 0);
            nethack_exit(EXIT_SUCCESS);
        }
        /* 前後の空白を落とす */
        n = strlen(buf);
        while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'))
            buf[--n] = '\0';
        for (n = 0; buf[n] == ' ' || buf[n] == '\t'; n++)
            ;
        if (buf[n] != '\0') {
            Strcpy(svp.plname, "");
            (void) strncpy(svp.plname, buf + n, PL_NSIZ - 1);
            svp.plname[PL_NSIZ - 1] = '\0';
            return;
        }
        if (++tries >= 10) {
            exit_nhwindows((char *) 0);
            nethack_exit(EXIT_SUCCESS);
        }
    }
}

static void p_get_nh_event(void) {}

static void
p_exit_nhwindows(const char *str)
{
    nh_host_exit_notice(str);
}

static void p_suspend_nhwindows(const char *str UNUSED) {}
static void p_resume_nhwindows(void) {}

static winid
p_create_nhwindow(int type)
{
    int i;

    for (i = 1; i < NH_MAXWIN; i++) {
        if (!g_wins[i].used) {
            g_wins[i].used = 1;
            g_wins[i].type = type;
            g_wins[i].n = 0;
            g_wins[i].prompt = (char *) 0;
            g_wins[i].is_rip = 0;
            g_wins[i].perminv = 0;
            g_wins[i].perminv_announced = 0;
            nh_host_ui_win(NH_UI_WIN_CREATE, i, type, 0);
            return (winid) i;
        }
    }
    panic("nh_winport: too many windows");
    return WIN_ERR;
}

static void
p_clear_nhwindow(winid w)
{
    struct nhwin *nw = getwin(w);

    if (!nw)
        return;
    if (nw->type == NHW_MAP)
        nh_host_map_clear();
    else if (nw->type == NHW_MENU || nw->type == NHW_TEXT)
        win_clear_items(nw);
    nh_host_ui_win(NH_UI_WIN_CLEAR, (int) w, nw->type, 0);
}

/* 溜めた行を、キーを 1 つ待つ窓として出す。nethack-ui では text / rip / menu（選ばない）の知らせで、閉じられるまで待つ */
static void
show_window_lines(winid w, struct nhwin *nw)
{
    if (nw->is_rip) {
        nh_host_rip((int) w, nw->rip_name, nw->rip_gold, nw->rip_killer, nw->rip_year, nw->items, nw->n);
        return;
    }
    if (nw->n <= 0)
        return;
    /* 文字の窓。メニューの窓でも putstr だけで行を積んだ物（神話・伝説・説明など）は、原作の GUI 版と同じく文字の窓として出す */
    if (nw->type == NHW_TEXT || (nw->type == NHW_MENU && nw->nadd == 0 && nw->nputstr > 0))
        nh_host_text_win((int) w, nw->items, nw->n);
    else
        (void) nh_host_menu_win((int) w, nw->type, NH_PICK_NONE, nw->prompt, nw->items, nw->n, (int *) 0,
                                (long *) 0);
}

static void
p_display_nhwindow(winid w, boolean blocking)
{
    struct nhwin *nw = getwin(w);

    if (!nw)
        return;
    nh_host_ui_win(NH_UI_WIN_DISPLAY, (int) w, nw->is_rip ? 100 : nw->type, blocking ? 1 : 0);
    /* メニュー・テキストの窓は、tty と同じく見せて閉じられるまで待つ。
       メッセージ窓の「--More--」は、共通の画面（frame）では待たない（メッセージは frame の欄に流れる）。
       nethack-ui では、メッセージ窓と地図窓の blocking な display_nhwindow を原作の GUI 版（mswin_display_nhwindow）と同じく
       キー 1 つを待つ（保存が済んだ後は待たない）。--More-- の規則は putstr の中（nh_host_ui_putstr） */
    if (nw->type == NHW_MENU || nw->type == NHW_TEXT)
        show_window_lines(w, nw);
    else if (blocking && (nw->type == NHW_MAP || nw->type == NHW_MESSAGE) && !program_state.savefile_completed)
        nh_host_ui_wait_any_key();
}

static void
p_destroy_nhwindow(winid w)
{
    struct nhwin *nw = getwin(w);

    if (!nw)
        return;
    nh_host_ui_win(NH_UI_WIN_DESTROY, (int) w, nw->is_rip ? 100 : nw->type, 0);
    win_clear_items(nw);
    nw->is_rip = 0;
    free((genericptr_t) nw->items);
    free((genericptr_t) nw->ids);
    nw->items = (nh_menu_item *) 0;
    nw->ids = (anything *) 0;
    nw->cap = 0;
    nw->used = 0;
}

static void
p_curs(winid w, int x, int y)
{
    struct nhwin *nw = getwin(w);

    if (nw && nw->type == NHW_MAP)
        nh_host_map_cursor(x, y);
    if (nw)
        nh_host_ui_curs((int) w, x, y);
}

static void
p_putstr(winid w, int attr, const char *str)
{
    struct nhwin *nw = getwin(w);
    char line[BUFSZ * 2];
    const char *p;

    if (!str)
        return;
    if (!nw || nw->type == NHW_MESSAGE) {
        nh_host_message(str);
        if (nh_host_ui_enabled()) {
            int hist = (int) iflags.msg_history;

            nh_host_ui_putstr((int) w, attr, str, hist < 1 ? 1 : (hist > 128 ? 128 : hist));
        }
        return;
    }
    if (nw->type == NHW_MENU || nw->type == NHW_TEXT) {
        nw->nputstr++;
        /* 改行を含む字は行に分ける */
        p = str;
        do {
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t) (e - p) : strlen(p);

            if (len >= sizeof line)
                len = sizeof line - 1;
            memcpy(line, p, len);
            line[len] = '\0';
            win_add_item(nw, (const anything *) 0, '\0', '\0', attr, -1, 0, line, -1);
            p = e ? e + 1 : (const char *) 0;
        } while (p && *p);
    }
}

/* 原作の display_file（help など）。ファイルの行を文字の窓として出す */
static void
p_display_file(const char *fname, boolean complain)
{
    dlb *f = dlb_fopen(fname, "r");
    struct nhwin *nw;
    winid w;
    char line[BUFSZ * 2];

    if (!f) {
        if (complain)
            pline("Cannot open \"%s\".", fname);
        return;
    }
    w = p_create_nhwindow(NHW_TEXT);
    nw = getwin(w);
    while (dlb_fgets(line, (int) sizeof line, f)) {
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        win_add_item(nw, (const anything *) 0, '\0', '\0', ATR_NONE, -1, 0, line, -1);
    }
    (void) dlb_fclose(f);
    show_window_lines(w, nw);
    p_destroy_nhwindow(w);
}

static void
p_start_menu(winid w, unsigned long mbehavior)
{
    struct nhwin *nw = getwin(w);

    if (nw) {
        win_clear_items(nw);
        nw->perminv = (mbehavior & MENU_BEHAVE_PERMINV) != 0UL;
    }
}

static void
p_add_menu(winid w, const glyph_info *gi, const ANY_P *id, char ch, char gch, int attr,
           int clr, const char *str, unsigned int itemflags)
{
    struct nhwin *nw = getwin(w);
    int glyph = (gi && gi->glyph >= 0 && gi->glyph < NO_GLYPH) ? gi->glyph : -1;

    if (nw)
        win_add_item(nw, id, ch, gch, attr, clr, (itemflags & MENU_ITEMFLAGS_SELECTED) ? 1 : 0, str, glyph);
}

static void
p_end_menu(winid w, const char *prompt)
{
    struct nhwin *nw = getwin(w);

    if (!nw)
        return;
    if (nw->prompt)
        free((genericptr_t) nw->prompt);
    nw->prompt = (prompt && *prompt) ? nh_strdup(prompt) : (char *) 0;
}

static int
p_select_menu(winid w, int how, MENU_ITEM_P **menu_list)
{
    struct nhwin *nw = getwin(w);
    int *chosen;
    long *counts;
    int i, n;

    *menu_list = (MENU_ITEM_P *) 0;
    if (!nw || nw->n <= 0)
        return 0;
    chosen = (int *) malloc((size_t) nw->n * sizeof *chosen);
    counts = (long *) malloc((size_t) nw->n * sizeof *counts);
    if (!chosen || !counts)
        panic("nh_winport: out of memory");
    if (nw->perminv && nh_host_ui_enabled()) {
        /* 持ち物の常設の窓: 窓の種類を invent として 1 度知らせ、中身を送って、返事は待たない */
        if (!nw->perminv_announced) {
            nh_host_ui_win(NH_UI_WIN_CREATE, (int) w, NHW_PERMINVENT, 0);
            nw->perminv_announced = 1;
        }
        (void) nh_host_menu_win((int) w, NHW_PERMINVENT, NH_PICK_NONE, nw->prompt, nw->items, nw->n, chosen, counts);
        free((genericptr_t) chosen);
        free((genericptr_t) counts);
        return 0;
    }
    n = nh_host_menu_win((int) w, nw->type, how, nw->prompt, nw->items, nw->n, chosen, counts);
    if (how == PICK_NONE)
        n = 0;
    if (n > 0) {
        *menu_list = (MENU_ITEM_P *) alloc((unsigned) n * sizeof(MENU_ITEM_P));
        for (i = 0; i < n; i++) {
            (*menu_list)[i].item = nw->ids[chosen[i]];
            (*menu_list)[i].count = counts[i];
            (*menu_list)[i].itemflags = 0;
        }
    }
    free((genericptr_t) chosen);
    free((genericptr_t) counts);
    return n;
}

static void p_mark_synch(void) {}
static void p_wait_synch(void) {}
static void
p_cliparound(int x, int y)
{
    nh_host_ui_cliparound(x, y);
}

/* glyph の分類と分類の中の番号 */
static void
classify_glyph(int g, int *cls, int *idx)
{
    *idx = 0;
    if (glyph_is_nothing(g)) {
        *cls = NH_GLYPH_NOTHING;
    } else if (glyph_is_unexplored(g)) {
        *cls = NH_GLYPH_UNEXPLORED;
    } else if (glyph_is_pet(g)) {
        *cls = NH_GLYPH_PET;
        *idx = glyph_to_mon(g);
    } else if (glyph_is_monster(g)) {
        *cls = NH_GLYPH_MONSTER;
        *idx = glyph_to_mon(g);
    } else if (glyph_is_invisible(g)) {
        *cls = NH_GLYPH_INVISIBLE;
    } else if (glyph_is_warning(g)) {
        *cls = NH_GLYPH_WARNING;
        *idx = glyph_to_warning(g);
    } else if (glyph_is_object(g)) {
        *cls = NH_GLYPH_OBJECT;
        *idx = glyph_to_obj(g);
    } else if (glyph_is_trap(g)) {
        *cls = NH_GLYPH_TRAP;
        *idx = glyph_to_trap(g);
    } else if (glyph_is_swallow(g)) {
        *cls = NH_GLYPH_SWALLOW;
        *idx = glyph_to_swallow(g);
    } else if (glyph_is_explosion(g)) {
        *cls = NH_GLYPH_EXPLOSION;
        *idx = glyph_to_explosion(g);
    } else if (glyph_is_cmap_zap(g)) {
        *cls = NH_GLYPH_ZAP;
        *idx = g - GLYPH_ZAP_OFF;
    } else if (glyph_is_cmap(g)) {
        *cls = NH_GLYPH_CMAP;
        *idx = glyph_to_cmap(g);
    } else {
        *cls = NH_GLYPH_OTHER;
    }
}

/*
 * 地形の glyph が持つ変種の番号（nh_host_map_cell の terrain_variant）。
 * 壁は迷宮の系統（0 通常・1 鉱山・2 ゲヘナ・3 ノックス・4 ソコバン）、祭壇は属性（altar_types の値）、それ以外は 0。
 */
static int
terrain_variant_of(int g)
{
    if (glyph_is_cmap_altar(g))
        return g - GLYPH_ALTAR_OFF;
    if (glyph_is_cmap_mines(g))
        return 1;
    if (glyph_is_cmap_gehennom(g))
        return 2;
    if (glyph_is_cmap_knox(g))
        return 3;
    if (glyph_is_cmap_sokoban(g))
        return 4;
    return 0;
}

/*
 * そのマスの下の地形（表示記号 S_*）と、その変種（terrain_variant_of）。怪物・アイテムなどが載っているマスの地形を、
 * 原作の地図の記憶から引く。
 *
 * print_glyph が渡す下敷き（bkglyphinfo）は使わない。下敷きは iflags.use_background_glyph が真のときだけ入り、
 * しかも部屋の床（S_room）は入れない（display.c の get_bkglyph_and_framecolor）ので、床の上の怪物の下が分からない。
 * 代わりに levl[x][y].glyph（主人公が覚えている物。怪物は含まない）を見る。それが地形か罠ならそのまま、
 * アイテムなら、一度でも見たマス（seenv）に限って本当の地形（back_to_glyph。秘密の扉は壁として返る）を使う。
 * 見たことのないマスの下は分からない（-1）。
 */
static int
cell_under_cmap(coordxy x, coordxy y, int *variant)
{
    struct rm *lev;
    struct trap *tt;
    int g, c;

    *variant = 0;
    if (!isok(x, y))
        return -1;
    lev = &levl[x][y];
    g = lev->glyph;
    if (glyph_is_cmap(g)) {
        c = glyph_to_cmap(g);
        if (c > S_stone && c < S_vbeam) {
            *variant = terrain_variant_of(g);
            return c;
        }
        return -1;
    }
    if (lev->seenv == 0 && !(x == u.ux && y == u.uy))
        return -1;
    tt = t_at(x, y);
    if (tt && tt->tseen)
        return trap_to_defsym(tt->ttyp);
    g = back_to_glyph(x, y);
    if (glyph_is_cmap(g)) {
        c = glyph_to_cmap(g);
        if (c > S_stone && c < S_vbeam) {
            *variant = terrain_variant_of(g);
            return c;
        }
        return -1;
    }
    return -1;
}

static void
p_print_glyph(winid w UNUSED, coordxy x, coordxy y, const glyph_info *gi,
              const glyph_info *bk UNUSED)
{
    int cls, idx, terrain, variant = 0;

    if (!gi)
        return;
    classify_glyph(gi->glyph, &cls, &idx);
    switch (cls) {
    case NH_GLYPH_NOTHING:
    case NH_GLYPH_UNEXPLORED:
        terrain = -1;
        break;
    case NH_GLYPH_CMAP:
    case NH_GLYPH_TRAP:
        /* 地形・罠。一番上が地形なら、その記号がそのまま地形。ビームなどの効果の記号は下の地形を描く */
        terrain = glyph_to_cmap(gi->glyph);
        if (terrain >= S_vbeam)
            terrain = cell_under_cmap(x, y, &variant);
        else
            variant = terrain_variant_of(gi->glyph);
        break;
    default:
        terrain = cell_under_cmap(x, y, &variant);
        break;
    }
    nh_host_map_cell((int) x, (int) y, gi->ttychar, (int) gi->gm.sym.color, gi->glyph, cls, idx,
                     (unsigned) gi->gm.glyphflags, terrain, variant);
}

static void
p_raw_print(const char *str)
{
    if (!str || !*str)
        return;
    fprintf(stderr, "[nethack] %s\n", str);
    nh_host_message(str);
    if (nh_host_ui_enabled())
        nh_host_ui_putstr(-1, ATR_NONE, str, 20);
}

/* 原作の input_state から、キーを待つ理由を決める */
static int
current_key_mode(void)
{
    if (program_state.input_state == commandInp)
        return NH_KEY_COMMAND;
    if (program_state.input_state == getposInp)
        return NH_KEY_POS; /* 共通の画面では NH_KEY_OTHER と同じに扱われる */
    return NH_KEY_OTHER;
}

/* 窓の関数がキーを読む。UI が居なくなった・終了を求められたときは、ホストが hangup へ入って ESC を返す */
static int
p_nhgetch(void)
{
    return nh_host_getkey(current_key_mode());
}

static int
p_nh_poskey(coordxy *x, coordxy *y, int *mod)
{
    int px = 0, py = 0, pmod = 0, key;

    if (mod)
        *mod = 0;
    key = nh_host_getkey_pos(current_key_mode(), &px, &py, &pmod);
    if (key == 0) {
        /* 地図のクリック（nethack-ui の pos の返事）。原作の CLICK_1 / CLICK_2 */
        if (x)
            *x = (coordxy) px;
        if (y)
            *y = (coordxy) py;
        if (mod)
            *mod = (pmod == 2) ? CLICK_2 : CLICK_1;
    }
    return key;
}

static void
p_nhbell(void)
{
    nh_host_ui_bell();
}

static int
p_doprev_message(void)
{
    nh_host_show_message_history();
    return 0;
}

static char
p_yn_function(const char *query, const char *resp, char def)
{
    return nh_host_yn(query, resp, def);
}

static void
p_getlin(const char *query, char *buf)
{
    if (!nh_host_getlin(query, buf, BUFSZ)) {
        buf[0] = '\033';
        buf[1] = '\0';
    }
}

/* 拡張コマンド（#）。名前を文字で入れさせ、完全一致か、一意に決まる前方一致を採る */
static int
p_get_ext_cmd(void)
{
    char buf[BUFSZ];
    const char *query = nh_host_lang_is_ja() ? "拡張コマンド (#):" : "Extended command (#):";
    const char *cmds[512];
    int ncmds = 0;
    int *list;
    int n, i;
    char *p;

    /* nethack-ui の ask kind=ext_cmd に付ける、選べる拡張コマンドの名前の一覧（内部用・使えない物・ウィザード専用を除く） */
    for (i = 0; extcmdlist[i].ef_txt != (char *) 0 && ncmds < 512; i++) {
        if ((extcmdlist[i].flags & (INTERNALCMD | CMD_NOT_AVAILABLE)) != 0)
            continue;
        if ((extcmdlist[i].flags & WIZMODECMD) != 0 && !wizard)
            continue;
        cmds[ncmds++] = extcmdlist[i].ef_txt;
    }
    for (;;) {
        buf[0] = '\0';
        if (!nh_host_ask_text(NH_ASK_EXT_CMD, query, buf, sizeof buf, cmds, ncmds))
            return -1;
        p = buf;
        while (*p == ' ' || *p == '#')
            p++;
        if (!*p)
            return -1;
        n = extcmds_match(p, ECM_EXACTMATCH | ECM_IGNOREAC, &list);
        if (n == 1)
            return list[0];
        n = extcmds_match(p, ECM_IGNOREAC, &list);
        if (n == 1)
            return list[0];
        if (n > 1)
            pline("Ambiguous extended command: %s", p);
        else
            pline("Unknown extended command: %s", p);
    }
}

static void p_number_pad(int state UNUSED) {}
static void p_delay_output(void) {}

/* 状態異常の色と属性（原作の GUI 版の mswin_condcolor・mswin_condattr と同じ） */
static int
cond_color_of(long bm, unsigned long *bmarray)
{
    int i;

    if (bm && bmarray)
        for (i = 0; i < CLR_MAX; ++i) {
            if ((bm & bmarray[i]) != 0)
                return i;
        }
    return NO_COLOR;
}

static int
cond_attr_of(long bm, unsigned long *bmarray)
{
    if (bm && bmarray) {
        if (bm & bmarray[HL_ATTCLR_BOLD])
            return HL_BOLD;
        if (bm & bmarray[HL_ATTCLR_DIM])
            return HL_DIM;
        if (bm & bmarray[HL_ATTCLR_ITALIC])
            return HL_ITALIC;
        if (bm & bmarray[HL_ATTCLR_ULINE])
            return HL_ULINE;
        if (bm & bmarray[HL_ATTCLR_BLINK])
            return HL_BLINK;
        if (bm & bmarray[HL_ATTCLR_INVERSE])
            return HL_INVERSE;
    }
    return HL_NONE;
}

static void
p_status_update(int idx, genericptr_t ptr, int chg UNUSED, int percent, int color,
                unsigned long *colormasks)
{
    if (idx == BL_FLUSH || idx == BL_RESET) {
        nh_host_status_flush(svp.plname);
        nh_host_ui_status_flush(idx == BL_RESET);
    } else if (idx == BL_CONDITION) {
        unsigned long mask = ptr ? *(unsigned long *) ptr : 0UL;
        char names[BUFSZ];
        const char *cname[CONDITION_COUNT];
        int ccolor[CONDITION_COUNT], cattr[CONDITION_COUNT];
        int i, ci, nc = 0;

        names[0] = '\0';
        for (i = 0; i < CONDITION_COUNT; i++) {
            if ((mask & (unsigned long) conditions[i].mask) != 0UL) {
                if (names[0])
                    Strcat(names, " ");
                if (strlen(names) + strlen(conditions[i].text[0]) + 2 < sizeof names)
                    Strcat(names, conditions[i].text[0]);
            }
        }
        nh_host_status_conditions(mask, names);
        /* nethack-ui: 原作の GUI 版と同じ順（cond_idx）で、色と属性つきに */
        for (i = 0; i < CONDITION_COUNT; i++) {
            ci = cond_idx[i];
            if (ci < 0 || ci >= CONDITION_COUNT)
                continue;
            if ((mask & (unsigned long) conditions[ci].mask) != 0UL) {
                cname[nc] = conditions[ci].text[0];
                ccolor[nc] = cond_color_of(conditions[ci].mask, colormasks);
                cattr[nc] = cond_attr_of(conditions[ci].mask, colormasks);
                nc++;
            }
        }
        nh_host_ui_status_cond(mask, nc, cname, ccolor, cattr);
    } else if (idx >= 0 && ptr) {
        nh_host_status_field(idx, (const char *) ptr);
        /* color の下位 8 ビットが色、その上の 8 ビットが属性（HL_*）。原作の GUI 版と同じ */
        nh_host_ui_status(idx, (const char *) ptr, color & 0xff, (color >> 8) & 0xff,
                          idx == BL_HP ? percent : -1);
    }
}

/* 墓標（nethack-ui の rip）。名前・所持金・死因・年を窓に覚えさせ、点数などの続きの行は後の putstr が積む。
   窓は display_nhwindow（end.c の最後）で rip の知らせとして出す。共通の画面では原作の genl_outrip と同じ */
static void
p_outrip(winid w, int how, time_t when)
{
    struct nhwin *nw = getwin(w);
    char buf[BUFSZ];

    if (!nh_host_ui_enabled() || !nw) {
        genl_outrip(w, how, when);
        return;
    }
    nw->is_rip = 1;
    Snprintf(nw->rip_name, sizeof nw->rip_name, "%s", svp.plname);
    nw->rip_gold = gd.done_money > 0 ? gd.done_money : 0;
    formatkiller(buf, sizeof buf, how, FALSE);
    Snprintf(nw->rip_killer, sizeof nw->rip_killer, "%s", buf);
    nw->rip_year = (int) ((yyyymmdd(when) / 10000L) % 10000L);
    win_clear_items(nw);
    nh_host_ui_win(NH_UI_WIN_CREATE, (int) w, 100, 0); /* 同じ窓の番号で種類が text から rip に替わる */
}

/* 設定の変更の知らせ（nethack-ui の pref）。画面が使うのは perm_invent など。値は分かる物だけ入れる */
static void
p_preference_update(const char *pref)
{
    char value[32];

    if (!pref)
        return;
    value[0] = '\0';
    if (!strcmp(pref, "perm_invent"))
        Snprintf(value, sizeof value, "%d", iflags.perm_invent ? 1 : 0);
    nh_host_ui_pref(pref, value);
}

/* 持ち物が変わった知らせ。nethack-ui で perm_invent が On なら、常設の窓を送り直させる（原作の GUI 版 mswin_update_inventory と同じ） */
static void
p_update_inventory(int arg UNUSED)
{
    if (nh_host_ui_enabled() && iflags.perm_invent && program_state.something_worth_saving
        && iflags.window_inited && WIN_INVEN != WIN_ERR)
        repopulate_perminvent();
}

/* 窓の設定のやり取り。nethack-ui では、持ち物の常設の窓の設定の問い（request_settings）に「使える」と答える */
static win_request_info *
p_ctrl_nhwindow(winid w UNUSED, int req, win_request_info *wri)
{
    if (!wri || !nh_host_ui_enabled())
        return (win_request_info *) 0;
    if (req == request_settings) {
        wri->tocore.tocore_flags = 0L;
        wri->tocore.active = TRUE;
        wri->tocore.maxslot = 52; /* 持ち物の字の数。0 だと原作は常設の窓を作らない */
    }
    return wri;
}

struct window_procs shim_procs = {
    WPID(shim),
    (WC_COLOR | WC_HILITE_PET | WC_INVERSE | WC_EIGHT_BIT_IN),
    (WC2_DARKGRAY | WC2_STATUSLINES
#if defined(STATUS_HILITES)
     | WC2_HILITE_STATUS | WC2_FLUSH_STATUS | WC2_RESET_STATUS
#endif
     ),
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    p_init_nhwindows, p_player_selection, p_askname, p_get_nh_event,
    p_exit_nhwindows, p_suspend_nhwindows, p_resume_nhwindows,
    p_create_nhwindow, p_clear_nhwindow, p_display_nhwindow,
    p_destroy_nhwindow, p_curs, p_putstr, genl_putmixed,
    p_display_file, p_start_menu, p_add_menu, p_end_menu,
    p_select_menu, genl_message_menu, p_mark_synch, p_wait_synch,
#ifdef CLIPPING
    p_cliparound,
#endif
#ifdef POSITIONBAR
    0,
#endif
    p_print_glyph, p_raw_print, p_raw_print, p_nhgetch,
    p_nh_poskey, p_nhbell, p_doprev_message, p_yn_function,
    p_getlin, p_get_ext_cmd, p_number_pad, p_delay_output,
#ifdef CHANGE_COLOR
    0,
    0,
#endif
    p_outrip,
    p_preference_update,
    genl_getmsghistory, genl_putmsghistory,
    genl_status_init,
    genl_status_finish, genl_status_enablefield,
    p_status_update,
    genl_can_suspend_no,
    p_update_inventory,
    p_ctrl_nhwindow,
};

/* ---- 原作では windmain.c にある、ほかのファイルが使う関数 ---- */

#ifdef _WIN32
/* 前のゲームの跡（レベルファイル）を消す。windmain.c の eraseoldlocks と同じ */
static int
erase_old_locks(void)
{
    int i;

    for (i = 1; i <= MAXDUNGEON * MAXLEVEL + 1; i++) {
        set_levelfile_name(gl.lock, i);
        (void) unlink(fqname(gl.lock, LEVELPREFIX, 0));
    }
    set_levelfile_name(gl.lock, 0);
    if (unlink(fqname(gl.lock, LEVELPREFIX, 0)))
        return 0;
    return 1;
}

/*
 * 同じ名前のゲームが動いている（か、異常終了の跡が残っている）かを調べてロックを取る。
 * windmain.c の getlock の流れのまま、問いは yn_function で行う。1: 続けてよい、0: 終える。
 */
int
getlock(void)
{
    int fd, ern = 0;
    const char *fq_lock;
    char oops[BUFSZ * 2];

    if (!lock_file(HLOCK, LOCKPREFIX, 10))
        error("Quitting.");

    set_levelfile_name(gl.lock, 0);
    fq_lock = fqname(gl.lock, LEVELPREFIX, 1);
    if ((fd = open(fq_lock, 0)) == -1) {
        if (errno == ENOENT)
            goto gotlock;
        unlock_file(HLOCK);
        Sprintf(oops, "Cannot open %s", fq_lock);
        raw_print(oops);
        nethack_exit(EXIT_FAILURE);
    }
    (void) close(fd);

    if (y_n("There are files from a game in progress under your name. Recover?") == 'y') {
        if (recover_savefile())
            goto gotlock;
        unlock_file(HLOCK);
        raw_print("Couldn't recover the old game.");
        return 0;
    }
    if (y_n("Destroy the old game and start a new one?") == 'y') {
        if (erase_old_locks())
            goto gotlock;
        unlock_file(HLOCK);
        raw_print("Couldn't destroy the old game.");
        return 0;
    }
    unlock_file(HLOCK);
    return 0;

gotlock:
    fd = creat(fq_lock, FCMASK);
    if (fd == -1)
        ern = errno;
    unlock_file(HLOCK);
    if (fd == -1) {
        Sprintf(oops, "cannot creat file (%s.)\n%s\n%s\"%s\" exists?\n", fq_lock, strerror(ern),
                " Are you sure that the directory", gf.fqn_prefix[LEVELPREFIX]);
        raw_print(oops);
    } else {
        svh.hackpid = nh_getpid();
        if (write(fd, (char *) &svh.hackpid, sizeof(svh.hackpid)) != sizeof(svh.hackpid))
            error("cannot write lock (%s)", fq_lock);
        if (close(fd) == -1)
            error("cannot close lock (%s)", fq_lock);
    }
    return 1;
}
#endif /* _WIN32 (Linux は原作の sys/unix/unixunix.c の getlock を使う) */

boolean
authorize_wizard_mode(void)
{
    return !strcmp(svp.plname, WIZARD_NAME);
}

boolean
authorize_explore_mode(void)
{
    return TRUE;
}

#ifdef PORT_HELP
void
port_help(void)
{
    display_file(PORT_HELP, 1);
}
#endif

/* ---- ホストからの呼び出し ---- */

void
nh_request_hangup(void)
{
    hangup(0);
}

int
nh_snapshot_view(unsigned char *visible, int *hero_x, int *hero_y)
{
    int x, y;

    if (!isok(u.ux, u.uy) || u.ux <= 0)
        return 0;
    for (y = 0; y < ROWNO; y++)
        for (x = 0; x < COLNO; x++)
            visible[y * COLNO + x] = (x > 0 && cansee(x, y)) ? 1 : 0;
    *hero_x = u.ux;
    *hero_y = u.uy;
    return 1;
}

/* コマンドの欄の絵（nh_host.h）。番号は nh_main.cpp の build_cmdbar の表と同じ */
int
nh_cmdbar_glyph(int id)
{
    switch (id) {
    case 2: return objnum_to_glyph(LENSES);         /* 探す */
    case 3: return objnum_to_glyph(GOLD_PIECE);     /* 拾う */
    case 4: return cmap_to_glyph(S_upstair);        /* 上る */
    case 5: return cmap_to_glyph(S_dnstair);        /* 下りる */
    case 6: return cmap_to_glyph(S_vodoor);         /* 開ける */
    case 7: return cmap_to_glyph(S_vcdoor);         /* 閉める */
    case 8: return objnum_to_glyph(HIGH_BOOTS);     /* 蹴る */
    case 10: return objnum_to_glyph(CREDIT_CARD);   /* 払う */
    case 20: return objnum_to_glyph(SACK);          /* 持ち物 */
    case 21: return objnum_to_glyph(PICK_AXE);      /* 使う */
    case 22: return objnum_to_glyph(FOOD_RATION);   /* 食べる */
    case 23: return objnum_to_glyph(POT_WATER);     /* 飲む */
    case 24: return objnum_to_glyph(SCR_BLANK_PAPER); /* 読む */
    case 25: return objnum_to_glyph(LONG_SWORD);    /* 構える */
    case 26: return objnum_to_glyph(PLATE_MAIL);    /* 着る */
    case 27: return objnum_to_glyph(ORCISH_HELM);   /* 脱ぐ */
    case 28: return objnum_to_glyph(RIN_ADORNMENT); /* 身につける */
    case 29: return objnum_to_glyph(AMULET_OF_ESP); /* 外す */
    case 30: return objnum_to_glyph(LARGE_BOX);     /* 置く */
    case 31: return objnum_to_glyph(DAGGER);        /* 投げる */
    case 32: return objnum_to_glyph(ARROW);         /* 撃つ */
    case 33: return objnum_to_glyph(SCIMITAR);      /* 武器の持ち替え */
    case 34: return objnum_to_glyph(WAN_STRIKING);  /* 杖を振る */
    case 35: return objnum_to_glyph(BOW);           /* 矢筒に入れる */
    case 40: return objnum_to_glyph(SPE_FORCE_BOLT); /* 呪文を唱える */
    case 41: return objnum_to_glyph(SPE_HEALING);   /* 覚えた呪文 */
    case 42: return altar_to_glyph(AM_NEUTRAL);     /* 祈る */
    case 50: return objnum_to_glyph(CRYSTAL_BALL);  /* 遠くを見る */
    case 52: return objnum_to_glyph(MAGIC_MARKER);  /* 発見したもの */
    case 54: return objnum_to_glyph(MIRROR);        /* 能力 */
    case 62: return cmap_to_glyph(S_grave);         /* 終了 */
    default: return -1;
    }
}

int
nh_describe_cell(int x, int y, char *buf, int bufsize)
{
    coord cc;
    char tmpbuf[BUFSZ];
    const char *firstmatch = "";

    if (!buf || bufsize < 1)
        return 0;
    buf[0] = '\0';
    if (u.ux <= 0 || !isok(x, y))
        return 0;
    cc.x = x;
    cc.y = y;
    tmpbuf[0] = '\0';
    /* getpos の auto_describe と同じ問い（looked = TRUE。画面に出ている記号で調べる）。副作用は無い */
    if (!do_screen_description(cc, TRUE, 0, tmpbuf, &firstmatch, (struct permonst **) 0) || !firstmatch)
        return 0;
    (void) strncpy(buf, firstmatch, (size_t) bufsize - 1);
    buf[bufsize - 1] = '\0';
    return 1;
}

/*
 * 遊ぶ人個人の NetHack の設定を読まないようにする。設定は置き場（savedir）の下の .nethackrc だけにする。
 *
 * 原作（cfgfiles.c の rcfile と fopen_config_file）の探し方は次のとおり。
 *   1. 環境変数 NETHACKOPTIONS（なければ HACKOPTIONS）。値が / \ @ で始まれば設定ファイルの名前、
 *      それ以外ならオプションの列として設定ファイルのあとに足される。
 *   2. 設定ファイル .nethackrc を CONFIGPREFIX の下で探す（set_prefixes で savedir にしてある）。
 *      なければ今のフォルダ（cwd）の .nethackrc を探す。
 *   3. このほか、ローグ階層の記号を ROGUEOPTS、ウィザードモードの初期アイテムを WIZKIT が環境変数で指す。
 * 遊ぶ人のホームの NetHack フォルダの .nethackrc を探すのは原作の set_default_prefix_locations（windsys.c）で、
 * ここでは呼ばないので届かない。だが 1・3 の環境変数と、2 の cwd は、遊ぶ人の環境や起動の仕方で別の物を指しうる。
 * そこで環境変数はこのプロセスの中でだけ消し（遊ぶ人の環境は変わらない）、cwd は initoptions の前に savedir にする
 * （データを開くための datadir への chdir は、initoptions のあとに今までどおり行う）。
 */
static void
isolate_user_config(void)
{
    static const char *const vars[] = { "NETHACKOPTIONS=", "HACKOPTIONS=", "ROGUEOPTS=",
                                        "WIZKIT=", (const char *) 0 };
    int i;

#ifdef _WIN32
    for (i = 0; vars[i]; i++)
        (void) _putenv(vars[i]); /* 「名前=」だけにすると、このプロセスの環境から消える */
    (void) _chdir(g_savedir);
#else
    /*
     * Linux。環境変数は同じく消す。原作の探し方はこのあと 2 つ違う（cfgfiles.c の fopen_config_file）:
     * 設定ファイルは $HOME/.nethackrc を探す（Windows の set_default_prefix_locations に当たる物は無い）ので、
     * このプロセスの HOME だけを置き場（savedir）に向ける。遊ぶ人の ~/.nethackrc は読まず、
     * 置き場の .nethackrc だけが効く。cwd の .nethackrc は探さない。
     * cwd はデータ（datadir）にする: 原作の assure_syscf_file（cfgfiles.c）は Windows のときだけ fqname で
     * sysconf を開き、ほかは今のフォルダの "sysconf" を開くため、initoptions の前に sysconf のある所へ居る必要がある。
     */
    char name[32];
    char home[BUFSZ];
    size_t n;

    for (i = 0; vars[i]; i++) {
        Snprintf(name, sizeof name, "%s", vars[i]);
        n = strlen(name);
        if (n > 0 && name[n - 1] == '=')
            name[n - 1] = '\0';
        (void) unsetenv(name);
    }
    Snprintf(home, sizeof home, "%s", g_savedir);
    n = strlen(home);
    if (n > 1 && home[n - 1] == '/')
        home[n - 1] = '\0';
    (void) setenv("HOME", home, 1);
    if (chdir(g_datadir) != 0) {
        /* 失敗すると sysconf が開けず、原作が「Unable to open SYSCF_FILE.」で終える */
    }
#endif
}

/* ---- 入口（windmain.c の MAIN の流れから、文字端末の部分を除いた物） ---- */

int
nh_game_main(const char *datadir, const char *savedir)
{
    static char name0[] = "NetHack";
    char *argv0[2];
    char **argv = argv0;
    int argc = 1;
    boolean resuming = FALSE;
    NHFILE *nhfp;
    char fnamebuf[BUFSZ], encodedfnamebuf[BUFSZ], failbuf[BUFSZ];
    char *dir = NULL;
    int i;
#ifdef _WIN32
    int getlock_result;
#endif

    argv0[0] = name0;
    argv0[1] = (char *) 0;
    copy_dir(g_datadir, sizeof g_datadir, datadir);
    copy_dir(g_savedir, sizeof g_savedir, savedir);
#ifndef _WIN32
    /* 書く場所に、ロックの土台 perm などの空のファイルとフォルダ save/ を用意する（nh_unix.c） */
    if (!nh_unix_prepare(g_savedir)) {
        fprintf(stderr, "[nethack] cannot prepare the save directory: %s\n", g_savedir);
        return 6;
    }
#endif
    isolate_user_config();

    early_init(argc, argv);
    gh.hname = "NetHack";
    set_prefixes();
    allopt_array_init();
    program_state.early_options = 1;
    choose_windows("shim");
    /* 専用の画面（nethack-ui）は持ち物の常設の窓（perm_invent）を自分で描く。申告があるときだけ、原作に「使える」と知らせる */
    if (nh_host_ui_enabled())
        windowprocs.wincap |= WC_PERM_INVENT;
    initoptions_init();
    set_prefixes(); /* initoptions_init が fqn_prefix を消すので、もう一度 */
    early_options(&argc, &argv, &dir);
    program_state.early_options = 0;
    initoptions();
    /*
     * 専用の画面（nethack-ui）は HP の割合（題の欄の HP の棒）を使う。原作が HP の割合を status_update に渡すのは
     * hitpointbar が On のときだけ（ほかは hilite_status の規則があるときだけ）なので、申告があるときは On にする。
     */
    if (nh_host_ui_enabled()) {
        windowprocs.wincap2 |= WC2_HITPOINTBAR;
        iflags.wc2_hitpointbar = TRUE;
#ifdef NH_LANG_JA
        /* NetHackJP の GUI 版（mswproc.c）は WC2_SUPPRESS_HIST を申告する。履歴に積まない一時の行が putstr の nohistory で届く */
        windowprocs.wincap2 |= WC2_SUPPRESS_HIST;
#endif
    }
    if (getenv("HENGBAND_NETHACK_TRACE_CONFIG"))
        fprintf(stderr, "[nethack] config file tried last: %s (fruit option: %s)\n", get_configfile(),
                svp.pl_fruit);
    /* DLB 無しではデータを今のフォルダから相対で開く（原作の入口も HACKPREFIX へ chdir する） */
    if (chdir(g_datadir) != 0)
        error("cannot chdir to %s", g_datadir);
    check_recordfile((char *) 0);
    genl_prag(argc, argv);
#ifdef _WIN32
    /* fqn_prefix_locked は Windows の原作にだけある */
    for (i = 0; i < PREFIX_COUNT; i++)
        fqn_prefix_locked[i] = TRUE;
#endif
    if (!validate_prefix_locations(failbuf)) {
        raw_printf("Some invalid directory locations were specified:\n\t%s\n", failbuf);
        nethack_exit(EXIT_FAILURE);
    }

    nh_host_set_phase(NH_PHASE_PREGAME);
    init_nhwindows(&argc, argv);

    u.uhp = 1; /* 早い終了で墓標を出さない */
    u.ux = 0;  /* flush_screen を防ぐ */
    iflags.use_background_glyph = FALSE;

    /* -role などの接尾辞を外し、名前が空なら askname を呼ぶ */
    plnamesuffix();
    set_playmode();
    iflags.renameallowed = FALSE;
    Sprintf(fnamebuf, "%s", svp.plname);
    (void) fname_encode("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-.", '%', fnamebuf,
                        encodedfnamebuf, BUFSZ);
    Snprintf(gl.lock, sizeof gl.lock, "%s", encodedfnamebuf);
#ifdef _WIN32
    if ((getlock_result = getlock()) == 0)
        nethack_exit(EXIT_SUCCESS);
#else
    /* 原作の unixunix.c の getlock は値を返さない（終えるときは自分で終える）。
       レベル 0 のファイルにはプロセスの番号を書くので、先に入れておく */
    svh.hackpid = nh_getpid();
    getlock();
#endif

#ifdef _WIN32
    /* ゲームの状態を持つレベル 0 のファイルを作る（Linux は原作の getlock が作る） */
    nhfp = create_levelfile(0, (char *) 0);
    if (!nhfp) {
        raw_print("Cannot create lock file");
    } else {
        svh.hackpid = nh_getpid();
        (void) write(nhfp->fd, (genericptr_t) &svh.hackpid, sizeof svh.hackpid);
        close_nhfile(nhfp);
    }
#endif
    vision_init();
    init_sound_disp_gamewindows();

    /* 同じ名前のセーブがあれば続きから */
    if ((nhfp = restore_saved_game()) != 0) {
        pline("%s", nh_host_lang_is_ja() ? "セーブデータを読み込み中..." : "Restoring save file...");
        mark_synch();
        if (dorecover(nhfp)) {
            resuming = TRUE;
            nh_host_set_phase(NH_PHASE_GAME);
            if (discover)
                You("are in non-scoring discovery mode.");
        }
    }
    if (!resuming) {
        /* 新しいゲーム: 職業・種族などを選ぶ（途中で名前を替えたときの読み直しは対応しない） */
        player_selection();
        nh_host_set_phase(NH_PHASE_GAME);
        newgame();
        if (discover)
            You("are in non-scoring discovery mode.");
    }
    moveloop(resuming);
    nethack_exit(EXIT_SUCCESS);
    return 0;
}
