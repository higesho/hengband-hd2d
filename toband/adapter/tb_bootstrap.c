/*!
 * @file tb_bootstrap.c
 * @brief TOband コアの起動列と進行。`init_file_paths()` → `init_angband()` → `play_game()`。
 *
 * （lib の置き場）・§4（アダプタ）。Term と終了の作りは （幻想蛮怒）。
 *
 * ## 何の写しか
 * `toband/src/main.c` の `init_stuff()`（:117）→ `init_file_paths()`（:145）と、
 * `main.c:319`（init_stuff の呼び出し）→ `:789`（init_angband）の流れ。
 * ディレクトリの検査は `main-win.c` の `WinMain` 内 `validate_dir()` 列（:6167 以降）を
 * そのまま並べ直したもの。ゲームの開始は `main-win.c:4374`（[ファイル]→[新規]）の写し。
 *
 * ## 置き場は環境変数に依らない
 * 先方の `init_stuff()` は `ANGBAND_PATH` を見るが、こちらは見ない。
 * `<exe_dir>/toband/lib` を呼び出し側（tb_main.cpp）が組んで渡す。
 * 変愚の `lib/` とは交差しない（R5・設計 §1 制約 6）。
 *
 * ## 失敗の受け方
 * コアは失敗すると `quit()` / `core()` を呼び、そのまま `exit()` する。
 * それでは「どこで死んだか」を呼び出し側へ返せないので、
 * `z-util.h:31-33` の 3 つのフックを差し込んで setjmp/longjmp で戻す。
 * **パッチではなく、コアが元から持っている口を使う**（設計 §3 の但し書き）。
 * 飛び越える枠はすべて C なので、C++ のデストラクタを踏み飛ばすことはない。
 *
 * ## 跳び先は 2 つある（設計 V4）
 * `quit()` は**起動の失敗**にも**遊び終えた合図**にも使われる。同じ跳び先で
 * 受けると正常終了が `TB_ERR_QUIT` に化けるので、段ごとに別の `jmp_buf` を持つ:
 *
 * | 段 | armed になる関数 | `quit()` の意味 |
 * |---|---|---|
 * | 起動 | `tb_bootstrap()` | 失敗（`TB_ERR_QUIT`） |
 * | 進行 | `tb_run_game()` | **正常終了**（`TB_OK`） |
 */

#include "angband.h"

#include "portable/legacy_os_c.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "tb_lang_c.h"
#include "tb_shim.h"

/*! 起動の段の跳び先（`tb_bootstrap`）。ここへ跳ぶ `quit()` は**失敗**である。 */
static jmp_buf tb_abort_buf;
/*! 跳び先が生きているか。生きていない間は素通し（＝コアの既定どおり exit する）。 */
static int tb_abort_armed = 0;
/*! 跳んだ理由（TB_ERR_QUIT / TB_ERR_CORE）。 */
static int tb_abort_kind = 0;

/*! 進行の段の跳び先（`tb_run_game`）。ここへ跳ぶ `quit()` は**正常終了**である。 */
static jmp_buf tb_game_buf;
static int tb_game_armed = 0;
/*! 進行中に `core()` を踏んだか（踏んだら正常終了とは呼べない）。 */
static int tb_game_kind = 0;
/*! `play_game()` に入ったか。`main-win.c` の `game_in_progress`（:456）に当たる。 */
static int tb_game_in_progress = 0;
/*! 強制保存の再入よけ。セーブの途中で 2 度目を走らせない。 */
static int tb_shutting_down = 0;

/*! 直近の失敗の説明。中身は SJIS のまま（設計 §3.1: 変換は境界の出口で）。 */
static char tb_error_text[1024] = "";

const char *tb_last_error(void)
{
    return tb_error_text;
}

static void tb_record_error(const char *tag, cptr str)
{
    sprintf(tb_error_text, "%.16s: %.980s", tag, (str && str[0]) ? str : "(no message)");
    fprintf(stderr, "[toband] %s\n", tb_error_text);
    fflush(stderr);
}

/*! plog() のフック。人間向けの出力は stdout ではなく **stderr** へ（プロトコル v1 §1.1）。 */
static void tb_plog_aux(cptr str)
{
    fprintf(stderr, "[toband:plog] %s\n", (str && str[0]) ? str : "");
    fflush(stderr);
}

/*!
 * @brief quit() のフック。**段によって意味が変わる**（設計 V4）。
 *
 * @details 起動の段では失敗、進行の段では正常終了。どちらの跳び先も生きて
 * いなければコアの既定（`exit()`）に任せる。
 */
static void tb_quit_aux(cptr str)
{
    if (tb_game_armed) {
        /*
         * 遊び終えた（`play_game()` の末尾 → `close_game()` → `quit(NULL)`、
         * あるいは `tb_shutdown_and_quit()` の末尾）。**失敗ではない**ので
         * stderr へ「error」とは書かない。
         */
        if (str && str[0]) {
            fprintf(stderr, "[toband] quit: %s\n", str);
        } else {
            fprintf(stderr, "[toband] quit (normal)\n");
        }
        fflush(stderr);

        tb_game_armed = 0;
        longjmp(tb_game_buf, 1);
    }

    tb_record_error("quit", str);

    if (tb_abort_armed) {
        tb_abort_armed = 0;
        tb_abort_kind = TB_ERR_QUIT;
        longjmp(tb_abort_buf, 1);
    }

    /* 跳び先が無ければコアの既定に任せる（quit() がこの後 exit する）。 */
}

/*! core() のフック。**これが無いと NULL 参照でわざと落ちる**（z-util.c の core()）。 */
static void tb_core_aux(cptr str)
{
    tb_record_error("core", str);

    if (tb_game_armed) {
        tb_game_armed = 0;
        tb_game_kind = TB_ERR_CORE;
        longjmp(tb_game_buf, 1);
    }

    if (tb_abort_armed) {
        tb_abort_armed = 0;
        tb_abort_kind = TB_ERR_CORE;
        longjmp(tb_abort_buf, 1);
    }
}

/*! main-win.c の check_dir() と同じ判定（末尾の区切りを落としてから属性を見る）。 */
static int tb_dir_exists(const char *s)
{
    /*
     * 末尾の区切り落としも属性の見方も `portable::dir_exists` が持っている
     * （`platform/portable/legacy_os.cpp`。Windows の中身はここに在ったものの写し）。
     */
    return portable_dir_exists(s);
}

/*!
 * @brief main-win.c の validate_dir() と同じ役目。
 * @param vital 真なら「無いと成立しない」、偽なら「無ければ作る」
 * @return 1 で通過、0 で失敗（tb_error_text に理由が入る）
 */
static int tb_validate_dir(const char *s, int vital)
{
    if (tb_dir_exists(s)) {
        return 1;
    }

    if (vital) {
        sprintf(tb_error_text, "required directory is missing: %.900s", s ? s : "(null)");
        fprintf(stderr, "[toband] %s\n", tb_error_text);
        fflush(stderr);
        return 0;
    }

    if (!portable_make_dir(s)) {
        sprintf(tb_error_text, "cannot create directory: %.900s", s ? s : "(null)");
        fprintf(stderr, "[toband] %s\n", tb_error_text);
        fflush(stderr);
        return 0;
    }

    return 1;
}

/*! main-win.c:6167 以降の validate_dir 列の写し。 */
static int tb_validate_lib_dirs(void)
{
    if (!tb_validate_dir(ANGBAND_DIR_APEX, 0)) {
        return 0;
    }
    if (!tb_validate_dir(ANGBAND_DIR_BONE, 0)) {
        return 0;
    }

    /* edit が無いなら data の raw だけで走る。どちらも無いと成立しない。 */
    if (!tb_dir_exists(ANGBAND_DIR_EDIT)) {
        if (!tb_validate_dir(ANGBAND_DIR_DATA, 1)) {
            return 0;
        }
    } else {
        /* raw の焼き先。無ければ作る（先方の clone には data/ が無い）。 */
        if (!tb_validate_dir(ANGBAND_DIR_DATA, 0)) {
            return 0;
        }
    }

    if (!tb_validate_dir(ANGBAND_DIR_FILE, 1)) {
        return 0;
    }
    if (!tb_validate_dir(ANGBAND_DIR_HELP, 0)) {
        return 0;
    }
    if (!tb_validate_dir(ANGBAND_DIR_INFO, 0)) {
        return 0;
    }
    if (!tb_validate_dir(ANGBAND_DIR_PREF, 1)) {
        return 0;
    }
    if (!tb_validate_dir(ANGBAND_DIR_SAVE, 0)) {
        return 0;
    }
    if (!tb_validate_dir(ANGBAND_DIR_USER, 1)) {
        return 0;
    }
    if (!tb_validate_dir(ANGBAND_DIR_XTRA, 1)) {
        return 0;
    }

    return 1;
}

#if defined(HENGBAND_DESKTOP_LINUX)
// The Linux launcher supplies a separate data root for the current user.
// Initialize the original Unix identity fields before any permission check.
static void tb_linux_user_paths(const char *root)
{
    char path[1024];
    player_uid = getuid();
    player_euid = geteuid();
    player_egid = getegid();
    snprintf(path, sizeof(path), "%s%suser", root, suffix(root, "/") ? "" : "/");
    string_free(ANGBAND_DIR_USER);
    ANGBAND_DIR_USER = string_make(path);
    snprintf(path, sizeof(path), "%s%ssave", root, suffix(root, "/") ? "" : "/");
    string_free(ANGBAND_DIR_SAVE);
    ANGBAND_DIR_SAVE = string_make(path);
    snprintf(path, sizeof(path), "%s%sapex", root, suffix(root, "/") ? "" : "/");
    string_free(ANGBAND_DIR_APEX);
    ANGBAND_DIR_APEX = string_make(path);
    snprintf(path, sizeof(path), "%s%sbone", root, suffix(root, "/") ? "" : "/");
    string_free(ANGBAND_DIR_BONE);
    ANGBAND_DIR_BONE = string_make(path);
    snprintf(path, sizeof(path), "%s%sdata", root, suffix(root, "/") ? "" : "/");
    string_free(ANGBAND_DIR_DATA);
    ANGBAND_DIR_DATA = string_make(path);
    snprintf(path, sizeof(path), "%s%sscript", root, suffix(root, "/") ? "" : "/");
    string_free(ANGBAND_DIR_SCRIPT);
    ANGBAND_DIR_SCRIPT = string_make(path);
}
#endif

int tb_bootstrap(const char *lib_dir)
{
    /*
     * setjmp() の後に読む値は volatile でなければならない。
     * ここで longjmp の後に読むのは静的変数だけなので、局所変数は素のままでよい。
     */
    char path[1024];
    size_t len;

    if (!lib_dir || !lib_dir[0]) {
        strcpy(tb_error_text, "lib directory was not given");
        return TB_ERR_ARG;
    }

    len = strlen(lib_dir);
    /* init_file_paths() は渡した buffer の末尾へ "script" 等を書き足す。余白を残す。 */
    if (len + 32 >= sizeof(path)) {
        strcpy(tb_error_text, "lib directory path is too long");
        return TB_ERR_ARG;
    }

    strcpy(path, lib_dir);
    if (!suffix(path, PATH_SEP)) {
        strcat(path, PATH_SEP);
    }

    if (!tb_dir_exists(path)) {
        sprintf(tb_error_text, "lib directory not found: %.900s", path);
        fprintf(stderr, "[toband] %s\n", tb_error_text);
        fflush(stderr);
        return TB_ERR_LIB;
    }

    /* Term が要る。note() が画面に書くので、無いと init_angband() が落ちる。 */
    {
        int cols = 0;
        int rows = 0;
        if (tb_term_size(&cols, &rows) != TB_OK) {
            strcpy(tb_error_text, "no term installed (call tb_term_install first)");
            return TB_ERR_STATE;
        }
    }

    /* コアの 3 つのフックを差し込む（z-util.h:31-33）。 */
    plog_aux = tb_plog_aux;
    quit_aux = tb_quit_aux;
    core_aux = tb_core_aux;

    /*
     * main-win.c:6384 と同じ。init_angband() の末尾が pref-%s.prf を読むので、
     * ここを "win" にしておかないと lib/pref/pref-win.prf が読まれない。
     */
    ANGBAND_SYS = "win";

    tb_error_text[0] = '\0';
    tb_abort_kind = 0;
    tb_abort_armed = 1;

    if (setjmp(tb_abort_buf) != 0) {
        tb_abort_armed = 0;
        return tb_abort_kind ? tb_abort_kind : TB_ERR_QUIT;
    }

    /* **buffer は書き換えられる。** init_file_paths() は tail に部分名を継ぎ足す。 */
    init_file_paths(path);
#if defined(HENGBAND_DESKTOP_LINUX)
    tb_linux_user_paths(lib_dir);
#endif

    /*
     * 英語モード（ （幻想蛮怒）` §4.1）: edit と data を差し替える。
     * - edit は作成済みの `toband/lib-en/edit`（`tools/toband/build_en_edit.py`）。
     *   **無ければ日本語のまま走る**（設計 §2 制約 4。stderr に理由を書く）。
     * - data は `toband/lib/data-en`。**分離は必須**——raw の名は
     *   `"%s_j.raw"` でコンパイル時に固定されており（init2.c:468,585,643）、
     *   同じ置き場だと日本語の raw と衝突する。
     * - file は作成済みの `toband/lib-en/file`（`tools/toband/build_en_file.py`。E4）。
     *   **置き場ごと替える**——名前の引き替え `tb_lang_file()`（フック #8）だけでは
     *   届かない道が 3 つある: 英語対の無いファイル（`*_gen.txt`・`rumors_new.txt`・
     *   `f_*_j.txt`・`ru_*.txt`）、対はあるが節が足りないファイル（`monspeak_j.txt` は
     *   幻想蛮怒の敵の節を持つが英語対は持たない）、`ANGBAND_DIR_FILE` を直に読む口
     *   （`news_j.txt`。init2.c:2352,2389）。引き替えは生かしたままでよい——
     *   lib-en/file では `xxx_j.txt` も `xxx.txt` も同じ英語だからである。
     * - help は作成済みの `toband/lib-en/help`（`tools/toband/build_en_help.py`。E7）。
     *   **置き場ごと替える**——引き替え `tb_lang_file()`（#9）が向く先の `help.hlp` は
     *   **変愚蛮怒の古い英語ヘルプ**で、幻想蛮怒の中身を 1 つも書いていない。
     *   替えたときは `tb_lang_set_en_help(1)` で引き替えを止める（さもないと入口が
     *   英訳版ではなく上流の英語ヘルプに化ける）。
     * SAVE は共通（セーブは言語をまたいで互換。設計 §4.4）。
     */
    if (tb_lang_enabled()) {
        char base[1024];
        char en_edit[1024];
        char en_data[1024];
        char en_file[1024];
        char en_help[1024];
        char en_pref[1024];
        size_t blen;

        strcpy(base, lib_dir);
        blen = strlen(base);
        while ((blen > 0) && ((base[blen - 1] == '\\') || (base[blen - 1] == '/'))) {
            base[--blen] = '\0';
        }
        sprintf(en_edit, "%s-en%sedit", base, PATH_SEP);
        sprintf(en_data, "%s%sdata-en", base, PATH_SEP);
        sprintf(en_file, "%s-en%sfile", base, PATH_SEP);
        sprintf(en_help, "%s-en%shelp", base, PATH_SEP);
        sprintf(en_pref, "%s-en%spref", base, PATH_SEP);

        /*
         * pref は**置き場ごと替える**。英語の自動拾いの見本 `pickpref.prf`（`toband/lang/en/pref/` から
         * `tools/toband/` の道具が作る）は `lib-en/pref` にしか無い（`autopick.c` のフック #17 が英語のとき
         * `pickpref*.prf` を読み、`prepare_default_pickpref()` が `ANGBAND_DIR_PREF` から写す）。
         * `lib-en/pref` は `lib/pref` と同じ一式を持つ。無ければ日本語のまま（自動拾いの見本が見つからないだけ）。
         */
        if (tb_dir_exists(en_pref)) {
            string_free(ANGBAND_DIR_PREF);
            ANGBAND_DIR_PREF = string_make(en_pref);
            fprintf(stderr, "[toband] english data: pref=%s\n", en_pref);
        } else {
            fprintf(stderr, "[toband] english mode, but %s is missing; keeping the japanese pref\n", en_pref);
        }

        if (tb_dir_exists(en_file)) {
            string_free(ANGBAND_DIR_FILE);
            ANGBAND_DIR_FILE = string_make(en_file);
            fprintf(stderr, "[toband] english data: file=%s\n", en_file);
        } else {
            fprintf(stderr,
                "[toband] english mode, but %s is missing; keeping the japanese file "
                "(run tools/toband/build_en_file.py)\n",
                en_file);
        }

        if (tb_dir_exists(en_help)) {
            string_free(ANGBAND_DIR_HELP);
            ANGBAND_DIR_HELP = string_make(en_help);
            tb_lang_set_en_help(1);
            fprintf(stderr, "[toband] english data: help=%s\n", en_help);
        } else {
            fprintf(stderr,
                "[toband] english mode, but %s is missing; keeping the japanese help "
                "(run tools/toband/build_en_help.py)\n",
                en_help);
        }

        if (tb_dir_exists(en_edit)) {
            string_free(ANGBAND_DIR_EDIT);
            ANGBAND_DIR_EDIT = string_make(en_edit);
            string_free(ANGBAND_DIR_DATA);
            ANGBAND_DIR_DATA = string_make(en_data);
            fprintf(stderr, "[toband] english data: edit=%s data=%s\n", en_edit, en_data);
        } else {
            fprintf(stderr,
                "[toband] english mode, but %s is missing; keeping the japanese data "
                "(run tools/toband/build_en_edit.py)\n",
                en_edit);
        }
        fflush(stderr);
    }

    if (!tb_validate_lib_dirs()) {
        tb_abort_armed = 0;
        return TB_ERR_LIB;
    }

    init_angband();

    tb_abort_armed = 0;
    return TB_OK;
}

/* ================================================================ 進行と終了（P2） */

int tb_run_game(int new_game)
{
    tb_game_kind = 0;
    tb_shutting_down = 0;

    if (setjmp(tb_game_buf) != 0) {
        /*
         * `quit()` か `core()` で戻ってきた。**ここが唯一の正常な出口**である
         * （`play_game()` は自力では戻らない。`main-win.c` も直後に `quit(NULL)` を
         *  置いてあるだけで、そこへ落ちてくることを当てにしていない）。
         */
        tb_game_armed = 0;
        tb_game_in_progress = 0;
        if (tb_game_kind == TB_BIRTH_CANCEL) {
            /*
             * 誕生の最初の画面で取消された（`tb_cancel_birth_to_title`）。`play_game()` は先頭で
             * `character_icky = TRUE` を立てて入るので、次に呼ばれたときに困らないよう、
             * 画面の退避の計数と未表示のメッセージを戻しておく。
             */
            character_icky = 0;
            character_xtra = 0;
            msg_flag = FALSE;
        }
        return tb_game_kind ? tb_game_kind : TB_OK;
    }

    tb_game_armed = 1;
    tb_game_in_progress = 1;

    /* `main-win.c:4374-4377`（IDM_FILE_NEW）と同じ順序。 */
    Term_flush();
    play_game(new_game ? TRUE : FALSE);

    /*
     * ここへは普通は来ない（`play_game()` の末尾が `close_game()` → `quit(NULL)`）。
     * 来たなら「遊び終えたが quit を踏まなかった」なので、正常終了として畳む。
     */
    tb_game_armed = 0;
    tb_game_in_progress = 0;
    return TB_OK;
}

int tb_cancel_birth_to_title(void)
{
    if (!tb_game_armed || character_generated) {
        return 0;
    }
    fprintf(stderr, "[toband] birth cancelled at the first screen; back to the title\n");
    fflush(stderr);
    tb_game_armed = 0;
    tb_game_kind = TB_BIRTH_CANCEL;
    longjmp(tb_game_buf, 1);
    return 0; /* 戻らない */
}

void tb_shutdown_and_quit(void)
{
    bool saved = FALSE;

    if (tb_shutting_down) {
        /*
         * 再入。`save_player()` はコアの都合で Term を触る（`Term_fresh()` が
         * 挟まると `TERM_XTRA_FRESH` → present → 待ち → ここ、と戻ってくる）ので、
         * この守りが無いと**書きかけのセーブを残す**。
         */
        return;
    }
    tb_shutting_down = 1;

    fprintf(stderr, "[toband] shutdown requested; forced save\n");
    fflush(stderr);

    /*
     * `main-win.c:5512`（WM_QUERYENDSESSION）の写し（設計 V4）。
     *
     * ウィンドウを閉じたときの `WM_CLOSE`（:5476）ではなく**こちら**を採る理由:
     * あちらは `Term_key_push(SPECIAL_KEY_QUIT)` を積むだけで、保存は
     * `do_cmd_save_and_exit()` → 主ループの脱出 → `close_game()` まで下りてから
     * 行われ、その途中 `files.c:12042` で **`inkey()` が待つ**（スコア予測の
     * 「リターンで続行」）。見る相手がいない場面で待たせるわけにいかない。
     * `WM_QUERYENDSESSION` は `can_save` も見ずに同期で保存して畳む列である。
     *
     * 日本語のリテラルは **CP932 で焼かれる**（この TU は UTF-8/BOM だが
     * execution-charset は ACP）。コアの内部コードと揃っているので、
     * `died_from` や日誌にそのまま入れてよい（プロトコルへは出さない）。
     */
    if (tb_game_in_progress && character_generated) {
        msg_flag = FALSE;

        /* Mega-Hack -- 瀕死のまま保存して「死亡」扱いにされないように。 */
        if (p_ptr->chp < 0) {
            p_ptr->is_dead = FALSE;
        }

        (void)do_cmd_write_nikki(NIKKI_GAMESTART, 0, "----ゲーム中断----");

        p_ptr->panic_save = 1;
        signals_ignore_tstp();
        strcpy(p_ptr->died_from, "(緊急セーブ)");

        saved = save_player();
        fprintf(stderr, "[toband] forced save %s\n", saved ? "succeeded" : "FAILED");
        fflush(stderr);

        /*
         * 階の一時ファイル（<セーブ>.Fnn）を消す。原作の普通の終了（`files.c` の `close_game()`）は
         * 保存のあとにこれを呼ぶが、強制保存はそこを通らないので、ダンジョンの中で畳むと残り、
         * 次の起動で「古いテンポラリ・ファイルが残っています」の問いで止まっていた（2026-10-16）。
         */
        if (saved) {
            clear_saved_floor_files();
        }
    } else {
        fprintf(stderr, "[toband] nothing to save (character_generated=%d)\n",
            character_generated ? 1 : 0);
        fflush(stderr);
    }

    /*
     * `quit(NULL)` は `tb_quit_aux` を通り、進行の段の跳び先へ跳ぶ
     * （＝`tb_run_game()` が `TB_OK` を返す）。**ここから先は実行されない。**
     * 文字列を渡さないのは `main-win.c` の全経路と同じ（渡すと非 NULL 扱いになる）。
     */
    quit(NULL);
}
