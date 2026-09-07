/*!
 * @file fc_bootstrap.c
 * @brief Frox コアの起動列と進行。`init_file_paths()` → `init_angband()` → `play_game()`。
 *
 * 基準は （配置）/ §3 / §3.2（Term と終了）/ §3.6（起動の順序）。
 * 形は `gensoband/adapter/gb_bootstrap.c` の写しである。
 *
 * ## 先方との違い（3 つだけ）
 * | | 幻想蛮怒 | Frox |
 * |---|---|---|
 * | `init_file_paths()` | 引数 1 つ | **引数 3 つ**（config / lib / data。`init2.c:103`。`main-win.c:4443` は 3 つとも同じ道を渡す） |
 * | `lib/info` `lib/script` | 使う | 中身が `delete.me` だけなので**写していない**。検査から外す |
 * | 緊急セーブの日誌 | `do_cmd_write_nikki()` がある | **無い**。`main-win.c:3816` の列そのまま（`died_from` に "(panic save)"） |
 *
 * ## 置き場は環境変数に依らない
 * 先方の `init_stuff()`（`main.c:117`）は `ANGBAND_PATH` を見るが、こちらは見ない。
 * `<exe_dir>/frox/lib` を呼び出し側（`fc_main.cpp`）が組んで渡す。
 * 変愚の `lib/` とは交差しない（設計 §1 制約 2）。
 *
 * ## 失敗の受け方
 * コアは失敗すると `quit()` / `core()` を呼び、そのまま `exit()` する。
 * それでは「どこで死んだか」を呼び出し側へ返せないので、
 * `z-util.h:31-33` の 3 つのフックを差し込んで setjmp/longjmp で戻す。
 * **パッチではなく、コアが元から持っている口を使う**（設計 §1 制約 1）。
 * 飛び越える枠はすべて C なので、C++ のデストラクタを踏み飛ばすことはない。
 *
 * ## 跳び先は 2 つある
 * `quit()` は**起動の失敗**にも**遊び終えた合図**にも使われる。同じ跳び先で
 * 受けると正常終了が `FC_ERR_QUIT` に化けるので、段ごとに別の `jmp_buf` を持つ:
 *
 * | 段 | armed になる関数 | `quit()` の意味 |
 * |---|---|---|
 * | 起動 | `fc_bootstrap()` | 失敗（`FC_ERR_QUIT`） |
 * | 進行 | `fc_run_game()` | **正常終了**（`FC_OK`） |
 */

#include "angband.h"

#include "portable/legacy_os_c.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "fc_shim.h"

/*! 起動の段の跳び先（`fc_bootstrap`）。ここへ跳ぶ `quit()` は**失敗**である。 */
static jmp_buf fc_abort_buf;
/*! 跳び先が生きているか。生きていない間は素通し（＝コアの既定どおり exit する）。 */
static int fc_abort_armed = 0;
/*! 跳んだ理由（FC_ERR_QUIT / FC_ERR_CORE）。 */
static int fc_abort_kind = 0;

/*! 進行の段の跳び先（`fc_run_game`）。ここへ跳ぶ `quit()` は**正常終了**である。 */
static jmp_buf fc_game_buf;
static int fc_game_armed = 0;
/*! 進行中に `core()` を踏んだか（踏んだら正常終了とは呼べない）。 */
static int fc_game_kind = 0;
/*! `play_game()` に入ったか。`main-win.c` の `game_in_progress` に当たる。 */
static int fc_game_in_progress = 0;
/*! 強制保存の再入よけ。セーブの途中で 2 度目を走らせない。 */
static int fc_shutting_down = 0;

/*! 直近の失敗の説明。**M0 は ASCII**（設計 §3.1）。 */
static char fc_error_text[1024] = "";

const char *fc_last_error(void)
{
    return fc_error_text;
}

static void fc_record_error(const char *tag, cptr str)
{
    sprintf(fc_error_text, "%.16s: %.980s", tag, (str && str[0]) ? str : "(no message)");
    fprintf(stderr, "[frox] %s\n", fc_error_text);
    fflush(stderr);
}

/*! plog() のフック。人間向けの出力は stdout ではなく **stderr** へ（プロトコル v1 §1.1）。 */
static void fc_plog_aux(cptr str)
{
    fprintf(stderr, "[frox:plog] %s\n", (str && str[0]) ? str : "");
    fflush(stderr);
}

/*!
 * @brief quit() のフック。**段によって意味が変わる**。
 *
 * @details 起動の段では失敗、進行の段では正常終了。どちらの跳び先も生きて
 * いなければコアの既定（`exit()`）に任せる。
 */
static void fc_quit_aux(cptr str)
{
    if (fc_game_armed) {
        /*
         * 遊び終えた（`play_game()` の末尾 → `close_game()` → `quit(NULL)`、
         * あるいは `fc_shutdown_and_quit()` の末尾）。**失敗ではない**ので
         * stderr へ「error」とは書かない。
         */
        if (str && str[0]) {
            fprintf(stderr, "[frox] quit: %s\n", str);
        } else {
            fprintf(stderr, "[frox] quit (normal)\n");
        }
        fflush(stderr);

        fc_game_armed = 0;
        longjmp(fc_game_buf, 1);
    }

    fc_record_error("quit", str);

    if (fc_abort_armed) {
        fc_abort_armed = 0;
        fc_abort_kind = FC_ERR_QUIT;
        longjmp(fc_abort_buf, 1);
    }

    /* 跳び先が無ければコアの既定に任せる（quit() がこの後 exit する）。 */
}

/*! core() のフック。**これが無いと NULL 参照でわざと落ちる**（z-util.c の core()）。 */
static void fc_core_aux(cptr str)
{
    fc_record_error("core", str);

    if (fc_game_armed) {
        fc_game_armed = 0;
        fc_game_kind = FC_ERR_CORE;
        longjmp(fc_game_buf, 1);
    }

    if (fc_abort_armed) {
        fc_abort_armed = 0;
        fc_abort_kind = FC_ERR_CORE;
        longjmp(fc_abort_buf, 1);
    }
}

/*! main-win.c の check_dir() と同じ判定（末尾の区切りを落としてから属性を見る）。 */
static int fc_dir_exists(const char *s)
{
    /*
     * 末尾の区切り落としも属性の見方も `portable::dir_exists` が持っている
     * （`platform/portable/legacy_os.cpp`）。
     */
    return portable_dir_exists(s);
}

/*!
 * @brief main-win.c の validate_dir() と同じ役目。
 * @param vital 真なら「無いと成立しない」、偽なら「無ければ作る」
 * @return 1 で通過、0 で失敗（fc_error_text に理由が入る）
 */
static int fc_validate_dir(const char *s, int vital)
{
    if (fc_dir_exists(s)) {
        return 1;
    }

    if (vital) {
        sprintf(fc_error_text, "required directory is missing: %.900s", s ? s : "(null)");
        fprintf(stderr, "[frox] %s\n", fc_error_text);
        fflush(stderr);
        return 0;
    }

    if (!portable_make_dir(s)) {
        sprintf(fc_error_text, "cannot create directory: %.900s", s ? s : "(null)");
        fprintf(stderr, "[frox] %s\n", fc_error_text);
        fflush(stderr);
        return 0;
    }

    return 1;
}

/* ============================================ 日本語のときの lib の差し替え（設計 §3.3） */

/*!
 * @brief `<lib_dir>` の親の下の `name` を組み立てる（`frox/lib` → `frox/lib-ja`）。
 * @return 1 で組めた。長すぎる・親が取れないときは 0
 */
static int fc_sibling_dir(const char *lib_dir, const char *name, char *out, size_t max)
{
    size_t len;
    size_t cut;

    if (!lib_dir || !name || !out) {
        return 0;
    }
    len = strlen(lib_dir);
    /* 末尾の区切りは落としてから親を探す（末尾に区切りが付いていても同じ答え）。 */
    while ((len > 0) && ((lib_dir[len - 1] == '/') || (lib_dir[len - 1] == '\\'))) {
        len--;
    }
    cut = len;
    while ((cut > 0) && (lib_dir[cut - 1] != '/') && (lib_dir[cut - 1] != '\\')) {
        cut--;
    }
    if (cut == 0) {
        return 0; /* 親が無い（相対の 1 段だけ）。差し替えない */
    }
    if ((cut + strlen(name) + 1) >= max) {
        return 0;
    }
    memcpy(out, lib_dir, cut);
    out[cut] = '\0';
    strcat(out, name);
    return 1;
}

/*!
 * @brief 日本語なら `edit` / `data` / `help` を日本語のものへ向ける（設計 §2・§3.3）。
 *
 * @details **`init_file_paths()` の後・`fc_validate_lib_dirs()` の前**に呼ぶ。
 * 差し替えるのは 3 つだけで、`file` `pref` `save` `apex` `bone` `user` は英語のまま
 * ——訳すのは実体データ（`edit`）とヘルプだけだからである。
 *
 * | 元 | 日本語 | 無いとき |
 * |---|---|---|
 * | `<lib>/edit` | `<lib-ja>/edit` | **英語のまま**（制約 4）。`data` も動かさない |
 * | `<lib>/data` | `<lib>/data-ja` | `edit` を差し替えたときだけ。無ければ作られる |
 * | `<lib>/help` | `<lib-ja>/help` | **英語のまま**。空の入れ物を作らない |
 *
 * `edit` を差し替えたのに `data` を据え置くと、**英語で作った `*.raw` がそのまま
 * 読まれて訳が 1 文字も出ない**（作成物は日付ではなく有無で判じられる）。
 * だから 2 つは必ず一緒に動かす。
 *
 * @return 常に 1。差し替えられなくても**英語で立つのが正しい**ので失敗にしない
 */
static int fc_apply_lang_dirs(const char *lib_dir, const char *lang)
{
    char base[1024];
    char buf[1024];

    if (!lang || (strcmp(lang, "ja") != 0)) {
        return 1; /* 英語。**1 バイトも動かさない**（設計 §1 制約 1） */
    }
    if (!fc_sibling_dir(lib_dir, "lib-ja", base, sizeof(base))) {
        fprintf(stderr, "[frox] cannot locate lib-ja next to %s; staying english\n", lib_dir);
        fflush(stderr);
        return 1;
    }

    if ((strlen(base) + 16) >= sizeof(buf)) {
        return 1;
    }

    strcpy(buf, base);
    strcat(buf, PATH_SEP);
    strcat(buf, "edit");
    if (fc_dir_exists(buf)) {
        z_string_free(ANGBAND_DIR_EDIT);
        ANGBAND_DIR_EDIT = z_string_make(buf);

        strcpy(buf, lib_dir);
        if (!suffix(buf, PATH_SEP)) {
            strcat(buf, PATH_SEP);
        }
        strcat(buf, "data-ja");
        z_string_free(ANGBAND_DIR_DATA);
        ANGBAND_DIR_DATA = z_string_make(buf);
        fprintf(stderr, "[frox] japanese edit = %s\n", ANGBAND_DIR_EDIT);
    } else {
        fprintf(stderr, "[frox] %s not built yet; entity names stay english\n", buf);
    }

    strcpy(buf, base);
    strcat(buf, PATH_SEP);
    strcat(buf, "help");
    if (fc_dir_exists(buf)) {
        z_string_free(ANGBAND_DIR_HELP);
        ANGBAND_DIR_HELP = z_string_make(buf);
        fprintf(stderr, "[frox] japanese help = %s\n", ANGBAND_DIR_HELP);
    }

    fflush(stderr);
    return 1;
}

/*!
 * @brief `main-win.c` の validate_dir 列の写し。
 *
 * @details 幻想蛮怒版と違うのは 2 点:
 * - **`ANGBAND_DIR_XTRA` を見ない。** 音・絵・フォントは取り込まない決まりなので
 *   （設計 §1 制約 5）、`frox/lib/xtra` は存在しない。先方は `main-win.c` で
 *   これを必須にしているが、当方は絵もフォントも自前を使うので要らない。
 * - **`ANGBAND_DIR_INFO` を見ない。** 先方の `lib/info` は `delete.me` しか
 *   入っておらず、写していない。
 */
static int fc_validate_lib_dirs(void)
{
    if (!fc_validate_dir(ANGBAND_DIR_APEX, 0)) {
        return 0;
    }
    if (!fc_validate_dir(ANGBAND_DIR_BONE, 0)) {
        return 0;
    }

    /* edit が無いなら data の raw だけで走る。どちらも無いと成立しない。 */
    if (!fc_dir_exists(ANGBAND_DIR_EDIT)) {
        if (!fc_validate_dir(ANGBAND_DIR_DATA, 1)) {
            return 0;
        }
    } else {
        /* raw の作成先。無ければ作る。 */
        if (!fc_validate_dir(ANGBAND_DIR_DATA, 0)) {
            return 0;
        }
    }

    if (!fc_validate_dir(ANGBAND_DIR_FILE, 1)) {
        return 0;
    }
    if (!fc_validate_dir(ANGBAND_DIR_HELP, 0)) {
        return 0;
    }
    if (!fc_validate_dir(ANGBAND_DIR_PREF, 1)) {
        return 0;
    }
    if (!fc_validate_dir(ANGBAND_DIR_SAVE, 0)) {
        return 0;
    }
    if (!fc_validate_dir(ANGBAND_DIR_USER, 1)) {
        return 0;
    }

    return 1;
}

int fc_bootstrap(const char *lib_dir, const char *lang)
{
    /*
     * setjmp() の後に読む値は volatile でなければならない。
     * ここで longjmp の後に読むのは静的変数だけなので、局所変数は素のままでよい。
     */
    char path[1024];
    size_t len;

    /*
     * `lang` は `init_file_paths()` の後で `fc_apply_lang_dirs()` が使う（設計 §3.3）。
     * カタログ（`messages.ja.txt`）はここへ来る前に `fc::lang_init()` が読んでいる
     * ——**言語は `fc_bootstrap()` に入る時点で確定している**（親契約 §3.6 の起動の順序）。
     */

    if (!lib_dir || !lib_dir[0]) {
        strcpy(fc_error_text, "lib directory was not given");
        return FC_ERR_ARG;
    }

    len = strlen(lib_dir);
    /* init_file_paths() は渡した buffer の末尾へ "edit" 等を書き足す。余白を残す。 */
    if (len + 32 >= sizeof(path)) {
        strcpy(fc_error_text, "lib directory path is too long");
        return FC_ERR_ARG;
    }

    strcpy(path, lib_dir);
    if (!suffix(path, PATH_SEP)) {
        strcat(path, PATH_SEP);
    }

    if (!fc_dir_exists(path)) {
        sprintf(fc_error_text, "lib directory not found: %.900s", path);
        fprintf(stderr, "[frox] %s\n", fc_error_text);
        fflush(stderr);
        return FC_ERR_LIB;
    }

    /* Term が要る。note() が画面に書くので、無いと init_angband() が落ちる。 */
    {
        int cols = 0;
        int rows = 0;
        if (fc_term_size(&cols, &rows) != FC_OK) {
            strcpy(fc_error_text, "no term installed (call fc_term_install first)");
            return FC_ERR_STATE;
        }
    }

    /* コアの 3 つのフックを差し込む（z-util.h:31-33）。 */
    plog_aux = fc_plog_aux;
    quit_aux = fc_quit_aux;
    core_aux = fc_core_aux;

    /*
     * `main-win.c` と同じ。`init_angband()` の末尾が `pref-%s.prf` を読むので、
     * ここを "win" にしておかないと `lib/pref/pref-win.prf` が読まれない。
     */
    ANGBAND_SYS = "win";

    fc_error_text[0] = '\0';
    fc_abort_kind = 0;
    fc_abort_armed = 1;

    if (setjmp(fc_abort_buf) != 0) {
        fc_abort_armed = 0;
        return fc_abort_kind ? fc_abort_kind : FC_ERR_QUIT;
    }

    /*
     * **引数は 3 つ**（config / lib / data）。`main-win.c:4443` と同じく 3 つとも
     * 同じ道を渡す——当方は先方の「設定は別の場所」という作法を採らない
     * （`frox/lib` の下で完結させる。設計 §2）。
     * **buffer は書き換えられる。** `init_file_paths()` は tail に部分名を継ぎ足す。
     */
    init_file_paths(path, path, path);

    /* 日本語なら edit / data / help を差し替える（設計 §3.3）。**作るのは下の validate**。 */
    fc_apply_lang_dirs(lib_dir, lang);

    if (!fc_validate_lib_dirs()) {
        fc_abort_armed = 0;
        return FC_ERR_LIB;
    }

    /*
     * @note `init_angband()` は**初回だけ `lib/help/` を書き戻す**
     *       （`ALLOW_SPOILERS`。設計 §3.5 の 2026-08-24 追記）。
     *       `frox/lib/edit/help_upd.txt` の `V:` 行が版と一致していれば動かない。
     *       取り込み時に 1 度作ってコミットする決まりで、そこが済んでいれば
     *       ここは 1 バイトも書かない（受け入れ §9 の 9）。
     */
    init_angband();

    /*
     * ここから先は `msg_line_rect()` を読んでよい（`msg_on_startup()` は
     * `init_angband()` の中・`init2.c:1960`）。**init の途中でも `Term_fresh()` は
     * 起きる**（進捗の note）ので、合図を立てるのは必ずこの後（FH-13）。
     */
    fc_mark_msg_ready();

    /*
     * `sound()` を `Term_xtra(TERM_XTRA_SOUND)` まで届かせる（FH-05）。
     * null term は **`SOUND_KILL` を数えるだけ**で音は 1 つも鳴らさない
     * 音の設計は保留のまま。`use_sound` を読むのは
     * `util.c:1883` の 1 か所だけで、他に副作用は無い（2026-08-24 に全数 grep）。
     */
    use_sound = TRUE;

    fc_abort_armed = 0;
    return FC_OK;
}

/* ============================ 自動で開き直す（追補 A2。`--resume`。設計 §9） */

/*!
 * @name 言語切り替えの立て直し
 *
 * @details 言語は `init_angband()` より前に決まっていなければならないので、遊んでいる
 * 途中で替えるには**コアを終了させて起こし直す**しかない。起こし直したあとに
 * セーブを選び直させるのは筋が悪いので、**終わるときに印を書き、起きるときに読む**。
 *
 * 印は `<save>/.resume` の 1 行（セーブ枠の綴り）。**点で始まる**ので
 * `list_save_slots()`（`fc_main.cpp`）の一覧には出ない——あちらは名前に `.` を
 * 含むものを外す。読んだら消す：残しておくと、次に `--resume` で起きたとき
 * 別の人物が勝手に開く。
 *
 * **`last_slot.txt` では代わりにならない。** あちらは「前に遊んだ枠」で、@ が
 * 出来た時点で書かれる。タイトルに居るあいだに言語を替えたら、印は無く、
 * タイトルがもう一度出るのが正しい。
 * @{
 */

//! 印の綴り。点で始めること（一覧から外れる条件が「`.` を含む」なので）。
#define FC_RESUME_FILE ".resume"

/*! @brief 印の道を組む。置き場が未設定なら 0 を返す。 */
static int fc_resume_path(char *buf, size_t max)
{
    if (!ANGBAND_DIR_SAVE || !ANGBAND_DIR_SAVE[0]) {
        return 0;
    }
    path_build(buf, (int)max, ANGBAND_DIR_SAVE, FC_RESUME_FILE);
    return 1;
}

void fc_resume_clear(void)
{
    char path[1024];

    if (!fc_resume_path(path, sizeof(path))) {
        return;
    }
    (void)remove(path);
}

/*!
 * @brief 印を書く（強制保存に成功した直後だけ）。
 * @details 書けなくても遊びは止めない——**次の立て直しでタイトルが出るだけ**である。
 */
static void fc_resume_write(void)
{
    char path[1024];
    FILE *fp;

    if (!savefile_base[0]) {
        return;
    }
    if (!fc_resume_path(path, sizeof(path))) {
        return;
    }
    fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "[frox] could not write the resume mark: %s\n", path);
        fflush(stderr);
        return;
    }
    fprintf(fp, "%s\n", savefile_base);
    fclose(fp);
    fprintf(stderr, "[frox] resume mark -> %s\n", savefile_base);
    fflush(stderr);
}

int fc_resume_take(char *name, size_t max)
{
    char path[1024];
    char save_path[1024];
    FILE *fp;
    size_t len;

    if (!name || (max == 0)) {
        return 0;
    }
    name[0] = '\0';
    if (!fc_resume_path(path, sizeof(path))) {
        return 0;
    }
    fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }
    if (!fgets(name, (int)max, fp)) {
        name[0] = '\0';
    }
    fclose(fp);
    (void)remove(path); /* **必ず消す。**読めても読めなくても、印は一度きりである */

    /* 行末の改行と空白を落とす。 */
    len = strlen(name);
    while ((len > 0)
        && ((name[len - 1] == '\n') || (name[len - 1] == '\r') || (name[len - 1] == ' '))) {
        name[--len] = '\0';
    }
    if (len == 0) {
        return 0;
    }
    /* 道の区切りと `.` は受けない（枠の名はディレクトリを含まず、一覧に出る名である）。 */
    if (strpbrk(name, "\\/:.") != 0) {
        fprintf(stderr, "[frox] the resume mark is not a save slot: %s\n", name);
        fflush(stderr);
        return 0;
    }

    /* **実在を確かめる。**無い綴りで続きから始めると新規作成が走る。 */
    path_build(save_path, sizeof(save_path), ANGBAND_DIR_SAVE, name);
    fp = fopen(save_path, "rb");
    if (!fp) {
        fprintf(stderr, "[frox] resume mark names a save that is not there: %s\n", name);
        fflush(stderr);
        return 0;
    }
    fclose(fp);
    return 1;
}
/*! @} */

/* ================================================================ 進行と終了 */

int fc_run_game(int new_game)
{
    fc_game_kind = 0;
    fc_shutting_down = 0;

    if (setjmp(fc_game_buf) != 0) {
        /*
         * `quit()` か `core()` で戻ってきた。**ここが唯一の正常な出口**である
         * （`play_game()` は自力では戻らない）。
         */
        fc_game_armed = 0;
        fc_game_in_progress = 0;
        return fc_game_kind ? fc_game_kind : FC_OK;
    }

    fc_game_armed = 1;
    fc_game_in_progress = 1;

    (void)Term_flush();
    play_game(new_game ? TRUE : FALSE);

    /*
     * ここへは普通は来ない（`play_game()` の末尾が `close_game()` → `quit(NULL)`）。
     * 来たなら「遊び終えたが quit を踏まなかった」なので、正常終了として終了させる。
     */
    fc_game_armed = 0;
    fc_game_in_progress = 0;
    return FC_OK;
}

void fc_shutdown_and_quit(void)
{
    bool saved = FALSE;

    if (fc_shutting_down) {
        /*
         * 再入。`save_player()` はコアの都合で Term を触る（`Term_fresh()` が
         * 挟まると `TERM_XTRA_FRESH` → present → 待ち → ここ、と戻ってくる）ので、
         * この守りが無いと**書きかけのセーブを残す**。
         */
        return;
    }
    fc_shutting_down = 1;

    fprintf(stderr, "[frox] shutdown requested; forced save\n");
    fflush(stderr);

    /*
     * `main-win.c:3816`（`WM_QUERYENDSESSION`）の写し。
     *
     * ウィンドウを閉じたときの `WM_CLOSE` ではなく**こちら**を採る理由:
     * あちらはまとめる合図を積むだけで、保存は主ループの脱出 → `close_game()` まで
     * 下りてから行われ、その途中で **`inkey()` が待つ**。見る相手がいない場面で
     * 待たせるわけにいかない。`WM_QUERYENDSESSION` は同期で保存して終了の列である。
     */
    if (fc_game_in_progress && character_generated) {
        /* Mega-Hack -- 瀕死のまま保存して「死亡」扱いにされないように。 */
        if (p_ptr->chp < 0) {
            p_ptr->is_dead = FALSE;
        }

        p_ptr->panic_save = 1;
        signals_ignore_tstp();
        strcpy(p_ptr->died_from, "(panic save)");

        saved = save_player();
        fprintf(stderr, "[frox] forced save %s\n", saved ? "succeeded" : "FAILED");
        fflush(stderr);

        /*
         * 追補 A2。**保存できたときだけ**印を書く——書けていない枠を次の起動で
         * 黙って開くと、遊んでいた進行が「巻き戻った」ように見える。
         */
        if (saved) {
            fc_resume_write();
        }
    } else {
        fprintf(stderr, "[frox] nothing to save (character_generated=%d)\n",
            character_generated ? 1 : 0);
        fflush(stderr);
    }

    /*
     * `quit(NULL)` は `fc_quit_aux` を通り、進行の段の跳び先へ跳ぶ
     * （＝`fc_run_game()` が `FC_OK` を返す）。**ここから先は実行されない。**
     */
    quit(NULL);
}
