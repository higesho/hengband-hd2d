/*!
 * @file sq_bootstrap.c
 * @brief Sil-Q コアの起動列と進行。`init_file_paths()` → `init_angband()` → タイトル → `play_game()`。
 *
 * 基準は （lib の解決）/ §3.2（Term と終了）/ §3.3（タイトル）。
 * 手本は `gensoband/adapter/gb_bootstrap.c`。
 *
 * ## 何の写しか
 * `silq/src/main.c` の `init_stuff()`（:131）→ `init_file_paths()`（:155）と、
 * `main.c:560`（init_angband）→ `:569-658`（タイトルとゲームのループ）。
 *
 * ## 置き場は環境変数に依らない
 * 先方の `init_stuff()` は `ANGBAND_PATH` を見るが、こちらは見ない。
 * `<exe_dir>/silq/lib` を呼び出し側（`sq_main.cpp`）が組んで渡す。
 * 変愚の `lib/` とは交差しない（設計 §1 制約 6）。
 *
 * ## 失敗の受け方
 * コアは失敗すると `quit()` / `core()` を呼び、そのまま `exit()` する。
 * それでは「どこで死んだか」を呼び出し側へ返せないので、
 * `z-util.h:28-30` の 3 つのフックを差し込んで setjmp/longjmp で戻す。
 * **パッチではなく、コアが元から持っている口を使う**（設計 §7）。
 * 飛び越える枠はすべて C なので、C++ のデストラクタを踏み飛ばすことはない。
 *
 * ## 跳び先は 2 つある
 * `quit()` は**起動の失敗**にも**遊び終えた合図**にも使われる。同じ跳び先で
 * 受けると正常終了が `SQ_ERR_QUIT` に化けるので、段ごとに別の `jmp_buf` を持つ:
 *
 * | 段 | armed になる関数 | `quit()` の意味 |
 * |---|---|---|
 * | 起動 | `sq_bootstrap()` | 失敗（`SQ_ERR_QUIT`） |
 * | 進行 | `sq_run_game()` | **正常終了**（`SQ_OK`） |
 */

#include "angband.h"

#include "portable/legacy_os_c.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "sq_shim.h"
#include "sq_lang_c.h"

/*! 起動の段の跳び先（`sq_bootstrap`）。ここへ跳ぶ `quit()` は**失敗**である。 */
static jmp_buf sq_abort_buf;
static int sq_abort_armed = 0;
static int sq_abort_kind = 0;

/*! 進行の段の跳び先（`sq_run_game`）。ここへ跳ぶ `quit()` は**正常終了**である。 */
static jmp_buf sq_game_buf;
static int sq_game_armed = 0;
static int sq_game_kind = 0;

/*!
 * @brief `play_game()` に入っているか。`main-win.c:327` の `game_in_progress` に当たる。
 * @details 本家のそれは `main-win.c`（コンパイル除外）にあるので、**ここで持ち直す**。
 * `main.c` の同名の変数と役目は同じ（タイトルへ戻るかどうかの旗）。
 */
static int sq_game_in_progress = 0;

/*! 強制保存の再入よけ。セーブの途中で 2 度目を走らせない。 */
static int sq_shutting_down = 0;

/*! `init_file_paths()` は渡した buffer を書き換えるので、原本をここに持つ。 */
static char sq_lib_dir[1024] = "";

/*!
 * @name 日本語の置き場
 * @details 日本語で立てたときだけ `ANGBAND_DIR_EDIT` / `ANGBAND_DIR_DATA` を
 * ここへ差し替える。**英語の写し（`lib/edit`）は 1 バイトも触らない**（設計 §1 制約 3）。
 * 作った `lib-ja/edit` が無ければ**黙って英語へ落ちる**のではなく、
 * stderr に理由を出してから英語で立てる（設計 §1 制約 4）。
 * @{
 */
static char sq_ja_edit_dir[1024] = "";
static char sq_ja_data_dir[1024] = "";
static int sq_use_ja_edit = 0;
/*!
 * 作成したマニュアルの道。**`ANGBAND_DIR_HELP` は使わない**
 * ——Sil-Q はあれを解放するだけで一度も組まない（`sq_validate_lib_dirs()` の註）。
 * フック #17 だけが読む（設計 §8.6）。
 *
 * **日英で置き場が違う**（2026-08-23。追補 ③）:
 * 日本語は `lib-ja/help/manual.txt`（CP932）、英語は `lib-en/help/manual.txt`（ASCII）。
 * どちらも `tools/silq/build_ja_help.py` が作成した物で、追跡していない（`.gitignore`）。
 */
static char sq_help_file[1024] = "";
/*! @} */

/*! 直近の失敗の説明。 */
static char sq_error_text[1024] = "";

const char *sq_last_error(void)
{
    return sq_error_text;
}

static void sq_record_error(const char *tag, cptr str)
{
    sprintf(sq_error_text, "%.16s: %.980s", tag, (str && str[0]) ? str : "(no message)");
    fprintf(stderr, "[silq] %s\n", sq_error_text);
    fflush(stderr);
}

/*! plog() のフック。人間向けの出力は stdout ではなく **stderr** へ（プロトコル v1 §1.1）。 */
static void sq_plog_aux(cptr str)
{
    fprintf(stderr, "[silq:plog] %s\n", (str && str[0]) ? str : "");
    fflush(stderr);
}

/*!
 * @brief quit() のフック。**段によって意味が変わる**。
 * @details 起動の段では失敗、進行の段では正常終了。どちらの跳び先も生きて
 * いなければコアの既定（`exit()`）に任せる。
 */
static void sq_quit_aux(cptr str)
{
    if (sq_game_armed) {
        if (str && str[0]) {
            fprintf(stderr, "[silq] quit: %s\n", str);
        } else {
            fprintf(stderr, "[silq] quit (normal)\n");
        }
        fflush(stderr);

        sq_game_armed = 0;
        longjmp(sq_game_buf, 1);
    }

    sq_record_error("quit", str);

    if (sq_abort_armed) {
        sq_abort_armed = 0;
        sq_abort_kind = SQ_ERR_QUIT;
        longjmp(sq_abort_buf, 1);
    }

    /* 跳び先が無ければコアの既定に任せる（quit() がこの後 exit する）。 */
}

/*! core() のフック。**これが無いと NULL 参照でわざと落ちる**（z-util.c の core()）。 */
static void sq_core_aux(cptr str)
{
    sq_record_error("core", str);

    if (sq_game_armed) {
        sq_game_armed = 0;
        sq_game_kind = SQ_ERR_CORE;
        longjmp(sq_game_buf, 1);
    }

    if (sq_abort_armed) {
        sq_abort_armed = 0;
        sq_abort_kind = SQ_ERR_CORE;
        longjmp(sq_abort_buf, 1);
    }
}

/*! `main-win.c` の `check_dir()` と同じ判定（末尾の区切りを落としてから属性を見る）。 */
static int sq_dir_exists(const char *s)
{
    /*
     * 末尾の区切り落としも属性の見方も `portable::dir_exists` が持っている
     * （`platform/portable/legacy_os.cpp`。Windows の中身はここに在ったものの写し）。
     */
    return portable_dir_exists(s);
}

/*!
 * @brief `main-win.c` の `validate_dir()` と同じ役目。
 * @param vital 真なら「無いと成立しない」、偽なら「無ければ作る」
 * @return 1 で通過、0 で失敗（`sq_error_text` に理由が入る）
 */
static int sq_validate_dir(const char *s, int vital)
{
    if (sq_dir_exists(s)) {
        return 1;
    }

    if (vital) {
        sprintf(sq_error_text, "required directory is missing: %.900s", s ? s : "(null)");
        fprintf(stderr, "[silq] %s\n", sq_error_text);
        fflush(stderr);
        return 0;
    }

    if (!portable_make_dir(s)) {
        sprintf(sq_error_text, "cannot create directory: %.900s", s ? s : "(null)");
        fprintf(stderr, "[silq] %s\n", sq_error_text);
        fflush(stderr);
        return 0;
    }

    return 1;
}

/*!
 * @brief lib 配下の検査。
 *
 * @details 見るのは **`init_file_paths()` が実際に組む 7 つ**だけである
 * （`init2.c:85-190`）。`ANGBAND_DIR_FILE` / `HELP` / `INFO` / `BONE` は
 * **Sil-Q では解放されるだけで一度も組まれない**（NULL のまま）ので、
 * ここで検査すると必ず落ちる。先方の `main-win.c:4021-4026` でも
 * その 4 つの `validate_dir` は**コメントアウトされている**——同じ理由である。
 */
static int sq_validate_lib_dirs(void)
{
    if (!sq_validate_dir(ANGBAND_DIR_EDIT, 1)) {
        return 0; /* 実体の定義。無いと何も作れない */
    }
    if (!sq_validate_dir(ANGBAND_DIR_PREF, 1)) {
        return 0; /* pref.prf を読む */
    }
    if (!sq_validate_dir(ANGBAND_DIR_XTRA, 0)) {
        return 0; /* tutorial のセーブが居る */
    }
    if (!sq_validate_dir(ANGBAND_DIR_DATA, 0)) {
        return 0; /* edit/*.txt を作った raw の書き先 */
    }
    if (!sq_validate_dir(ANGBAND_DIR_APEX, 0)) {
        return 0; /* init_angband() が scores.raw を作る */
    }
    if (!sq_validate_dir(ANGBAND_DIR_SAVE, 0)) {
        return 0;
    }
    if (!sq_validate_dir(ANGBAND_DIR_USER, 0)) {
        return 0;
    }

    return 1;
}

/*!
 * @brief `sq_lib_dir`（`<silq>/lib/`）の**親**を組む。
 * @return 1 = 組めた／0 = 区切りが 1 つも無くて辿れない
 * @details `sq_lib_dir` は `<...>/silq/lib\` の形（末尾に区切りが付いている）。
 * `lib-ja` / `lib-en` はどちらも `lib` と並ぶ（設計 §2 の配置）ので、
 * 日本語の `edit` と、日英のマニュアルの両方がここから伸びる。
 */
static int sq_lib_parent(char *out, size_t max)
{
    size_t i;

    if (strlen(sq_lib_dir) >= max) {
        return 0;
    }
    strcpy(out, sq_lib_dir);
    i = strlen(out);
    /* 末尾の区切りを落とす → 最後の名前（"lib"）を落とす。 */
    while (i && ((out[i - 1] == '\\') || (out[i - 1] == '/'))) {
        out[--i] = '\0';
    }
    while (i && ((out[i - 1] != '\\') && (out[i - 1] != '/'))) {
        out[--i] = '\0';
    }
    return i ? 1 : 0;
}

/*!
 * @brief 日本語の `edit` / `data` の置き場を組む。
 * @return 1 なら差し替えてよい（作成した `lib-ja/edit` が実在する）
 *
 * @details 親（`sq_lib_parent()`）の下に `lib-ja` を並べる（設計 §2 の配置）。
 * `data-ja` のほうは `lib` の下でよい（`*.raw` は生成物で、英語の `data` と
 * 混ざらなければ足りる）。
 */
static int sq_prepare_ja_dirs(void)
{
    char parent[1024];

    sq_ja_edit_dir[0] = '\0';
    sq_ja_data_dir[0] = '\0';

    if (!sq_lib_parent(parent, sizeof(parent))) {
        fprintf(stderr, "[silq] cannot derive the lib-ja directory from %s\n", sq_lib_dir);
        fflush(stderr);
        return 0;
    }

    if ((strlen(parent) + 32) >= sizeof(sq_ja_edit_dir)) {
        fprintf(stderr, "[silq] lib-ja path would be too long\n");
        fflush(stderr);
        return 0;
    }

    strcpy(sq_ja_edit_dir, parent);
    strcat(sq_ja_edit_dir, "lib-ja");
    strcat(sq_ja_edit_dir, PATH_SEP);
    strcat(sq_ja_edit_dir, "edit");

    strcpy(sq_ja_data_dir, sq_lib_dir);
    strcat(sq_ja_data_dir, "data-ja");

    if (!sq_dir_exists(sq_ja_edit_dir)) {
        /*
         * **黙って英語にしない。** 焼き忘れ（`tools/silq/build_ja_edit.py`）は
         * 必ず 1 度は踏むので、踏んだときに何をすればよいかまで出す。
         */
        fprintf(stderr, "[silq] japanese edit files are not baked yet: %s\n", sq_ja_edit_dir);
        fprintf(stderr, "[silq]   run: python tools/silq/build_ja_edit.py\n");
        fprintf(stderr, "[silq]   falling back to the english edit files\n");
        fflush(stderr);
        return 0;
    }

    return 1;
}

/*! `init_file_paths()` へ渡す道を組み直す（あちらが buffer を壊すので毎回作る）。 */
static void sq_apply_file_paths(void)
{
    char path[1024];

    strcpy(path, sq_lib_dir);
    init_file_paths(path);

    if (!sq_use_ja_edit) {
        return;
    }

    /*
     * 日本語の置き場へ差し替える。**`init_file_paths()` の後**で行う
     * ——あちらは冒頭で全部 `string_free()` してから組み直すので、
     * 先に差し替えると解放される。
     */
    string_free(ANGBAND_DIR_EDIT);
    ANGBAND_DIR_EDIT = string_make(sq_ja_edit_dir);
    string_free(ANGBAND_DIR_DATA);
    ANGBAND_DIR_DATA = string_make(sq_ja_data_dir);

    fprintf(stderr, "[silq] edit = %s\n", ANGBAND_DIR_EDIT);
    fprintf(stderr, "[silq] data = %s\n", ANGBAND_DIR_DATA);
    fflush(stderr);
}

/*!
 * @name マニュアル
 *
 * @details **Sil-Q に `lib/help` は無い。** `do_cmd_help()` が出すのは
 * `files.c` の C で描く 3 枚だけで、詳しい規則は `lib/docs/` の PDF にある。
 * そこを 1 本のテキストにしたのが `lib-{ja,en}/help/manual.txt` で、
 * ここはそれを `show_file()` へ渡すだけの口である。
 *
 * **英語にも出る**（2026-08-23。追補 ③。決めた基準「どちらの言語でも過不足が無いこと」）。
 * それまでは `sq_lang_enabled()` で閉じていて、`?` の 2 枚目に案内すら出なかった。
 * 作成していない言語では**今までどおり何も起きない**——案内を出さず、
 * `m` は元どおり「次の頁」として食われる（設計 §1 制約 1）。
 * @{
 */

/*!
 * @brief マニュアルの道を組む（`sq_bootstrap()` から 1 回）。
 * @param lang `"ja"` なら `lib-ja`、それ以外は `lib-en`
 * @details **作成していなくても構わない**——無ければ案内を出さないだけで、
 * 遊びは 1 ビットも変わらない。だから在るかは確かめずに組むだけにする。
 */
static void sq_prepare_help_file(const char *lang)
{
    char parent[1024];
    const char *dir = (lang && (strcmp(lang, "ja") == 0)) ? "lib-ja" : "lib-en";

    sq_help_file[0] = '\0';
    if (!sq_lib_parent(parent, sizeof(parent))) {
        return;
    }
    if ((strlen(parent) + 32) >= sizeof(sq_help_file)) {
        return;
    }

    strcpy(sq_help_file, parent);
    strcat(sq_help_file, dir);
    strcat(sq_help_file, PATH_SEP);
    strcat(sq_help_file, "help");
    strcat(sq_help_file, PATH_SEP);
    strcat(sq_help_file, "manual.txt");
}

/*! 作成したマニュアルが実在するか。無ければ案内も出さない（作り忘れで画を壊さない）。 */
static int sq_manual_ready(void)
{
    FILE *fff;

    if (!sq_help_file[0]) {
        return 0;
    }

    fff = fopen(sq_help_file, "r");
    if (!fff) {
        return 0;
    }
    fclose(fff);

    return 1;
}

/*
 * 案内と表題。**カタログを通す**（2026-08-23）。
 *
 * 以前は CP932 のバイト列を 16 進で直に書いていた（このツリーは UTF-8 なので
 * 日本語のリテラルがそのままでは置けない）。英語にもマニュアルを出すことにした以上、
 * 鍵は英文であるべきで、日本語は `silq/lang/ja/ui/messages.ja.txt` の側にある。
 * `sq_tr()` は英語のとき鍵をそのまま返すので、両方の言語がこの 1 行で足りる。
 */

/*! 案内の鍵。日本語は「m を押すとマニュアル（規則の詳しい説明）が開く」。 */
static const char sq_manual_hint_key[] = "m brings up the manual (the rules in detail)";

/*! `show_file()` の caption。画には出ないが NULL だと開かない。 */
static const char sq_manual_caption_key[] = "Manual";

int sq_help_manual_hint(void)
{
    if (!sq_manual_ready()) {
        return 0;
    }

    /*
     * 24 行目・3 桁目。**`do_cmd_help()` の 3 枚はどれも 23 行目までしか使わない**
     * （頁 1 の左の列が 23 行目の "- can be done by pressing ," で終わる）。
     * Term は 80x27 なので 24 行目は空いている。
     */
    c_put_str(TERM_SLATE, sq_tr(sq_manual_hint_key), 24, 3);
    c_put_str(TERM_WHITE, "m", 24, 3);

    return 1;
}

int sq_help_manual_open(int ch)
{
    if (((ch != 'm') && (ch != 'M')) || !sq_manual_ready()) {
        return 0;
    }

    (void)show_file(sq_help_file, sq_tr(sq_manual_caption_key), 0);

    return 1;
}

/*! @} */

int sq_bootstrap(const char *lib_dir, const char *lang)
{
    size_t len;

    if (!lib_dir || !lib_dir[0]) {
        strcpy(sq_error_text, "lib directory was not given");
        return SQ_ERR_ARG;
    }

    len = strlen(lib_dir);
    /* `init_file_paths()` は渡した buffer の末尾へ "script" 等を書き足す。余白を残す。 */
    if (len + 32 >= sizeof(sq_lib_dir)) {
        strcpy(sq_error_text, "lib directory path is too long");
        return SQ_ERR_ARG;
    }

    strcpy(sq_lib_dir, lib_dir);
    if (!suffix(sq_lib_dir, PATH_SEP)) {
        strcat(sq_lib_dir, PATH_SEP);
    }

    if (!sq_dir_exists(sq_lib_dir)) {
        sprintf(sq_error_text, "lib directory not found: %.900s", sq_lib_dir);
        fprintf(stderr, "[silq] %s\n", sq_error_text);
        fflush(stderr);
        return SQ_ERR_LIB;
    }

    /*
     * 言語を確定する。**`init_angband()` より前でなければ意味がない**（設計 §3.3）。
     * `edit` の置き場がここで決まり、そのまま `data-ja/*.raw` に焼かれる。
     */
    sq_use_ja_edit = 0;
    if (lang && (strcmp(lang, "ja") == 0)) {
        sq_use_ja_edit = sq_prepare_ja_dirs();
    }

    /*
     * マニュアルの置き場（フック #17）。**言語で選ぶだけ**で、在るかは見ない
     * ——作成していなければ案内が出ないだけである（`sq_manual_ready()`）。
     * `sq_prepare_ja_dirs()` が失敗しても英語のマニュアルは開けるので、その外に置く。
     */
    sq_prepare_help_file(lang);

    /* Term が要る。`note()` が画面に書くので、無いと `init_angband()` が落ちる。 */
    {
        int cols = 0;
        int rows = 0;
        if (sq_term_size(&cols, &rows) != SQ_OK) {
            strcpy(sq_error_text, "no term installed (call sq_term_install first)");
            return SQ_ERR_STATE;
        }
    }

    /* コアの 3 つのフックを差し込む（z-util.h:28-30）。 */
    plog_aux = sq_plog_aux;
    quit_aux = sq_quit_aux;
    core_aux = sq_core_aux;

    /*
     * `main-win.c:4179` と同じ。pref ファイルの `%s` 展開（`files.c:910`）が
     * これを見る。Sil-Q が読むのは `pref.prf` だけだが、揃えておく。
     */
    ANGBAND_SYS = "win";

    /*
     * 絵は使わない（ASCII の記号だけを frame に載せる。設計 §4）。
     * 既定でも 0 だが、`map_info()` の分岐に効くので明示しておく。
     */
    use_graphics = 0;

    sq_error_text[0] = '\0';
    sq_abort_kind = 0;
    sq_abort_armed = 1;

    if (setjmp(sq_abort_buf) != 0) {
        sq_abort_armed = 0;
        return sq_abort_kind ? sq_abort_kind : SQ_ERR_QUIT;
    }

    sq_apply_file_paths();

    if (!sq_validate_lib_dirs()) {
        sq_abort_armed = 0;
        return SQ_ERR_LIB;
    }

    init_angband();

    /*
     * `sound()` を `Term_xtra(TERM_XTRA_SOUND)` まで届かせる。
     * null term は**鳴らさない**——名前とマスを `frame.sounds` へ積むだけで、
     * 鳴らすのは画面側である。積むかどうかは `sq_sound_set_wanted()` の側が決める。
     *
     * **これを立てないと出来事ごと消える。** `sound()` は `use_sound` が偽だと
     * `Term_xtra` すら呼ばない（`util.c:2098`）。読むのはそこ 1 か所だけで、
     * ほかに副作用は無い（`silq/src` を全数 grep。2026-08-31）。
     */
    use_sound = TRUE;

    sq_abort_armed = 0;
    return SQ_OK;
}

/* ================================================================ 進行と終了 */

/*!
 * @brief タイトル（`initial_menu`）を回して 1 局を選ぶ。
 * @param new_game 新規かどうかの受け皿
 * @return 1 = 遊ぶ / 0 = 終了が選ばれた
 *
 * @details `main.c:572-637` の写し。**画面を自作しない**（設計 §3.3）。
 */
/*!
 * @name セーブの選択（2026-08-23 に決めた「ファイル名の手打ちをやめて選ぶ形に」）
 *
 * @details 先方の `main.c:597` は `askfor_aux()` で**名前を打たせる**。
 * 触りだけの機体にはキーボードが無く、あっても綴りを 1 字違えれば開けない。
 * ここは**セーブの置き場を読んで一覧にする**。
 *
 * **`silq/src` は触っていない**（必守制約 2）。この関数はアダプタの中で、
 * コアが公開している道具（`Term_*` / `inkey()` / `path_build()`）だけを使う。
 *
 * 文字はカタログを通す（`sq_tr()`）ので、英語のときは鍵の英文がそのまま出る。
 * @{
 */

/*!
 * @name 一覧の形（**2 列**。2026-08-23 に決めた）
 * @details 1 列だと 20 件で頭打ちになり、実際に 28 件のうち 8 件が出なかった。
 * 画は 80x27 なので、20 行 × 2 列で 40 件まで出る。
 * 並びは**列ごとに上から**（1 列目を埋めてから 2 列目）——名前の順に読むならこちら。
 * @{
 */
#define SQ_SAVE_ROWS 20
#define SQ_SAVE_COLS 2
#define SQ_SAVE_SHOWN (SQ_SAVE_ROWS * SQ_SAVE_COLS)
//! 2 列目の左端（桁）。1 列目は 7 桁目。
#define SQ_SAVE_COL2 42
/*! @} */
//! 読み取る数（並べ替えてから上の数だけ出す）。
#define SQ_SAVE_SCAN 64
//! 1 件ぶんの名前の長さ。Sil-Q のセーブ名は `op_ptr->base_name`（32）より短い。
#define SQ_SAVE_NAME 48

/*!
 * @brief セーブの置き場を読んで名前を並べる。
 * @return 見つかった数（`SQ_SAVE_SCAN` で頭打ち）
 * @details **点で始まる名前は外す**（`.keepalive` ほか、遊びの記録ではない）。
 * 並べ替えは大文字小文字を無視した辞書順——OS の返す順は当てにしない。
 */
static int sq_collect_savefiles(char names[SQ_SAVE_SCAN][SQ_SAVE_NAME])
{
    static char raw[SQ_SAVE_SCAN][SQ_SAVE_NAME];
    int found;
    int count = 0;
    int i;
    int j;

    found = portable_list_files(ANGBAND_DIR_SAVE, &raw[0][0], SQ_SAVE_NAME, SQ_SAVE_SCAN);
    for (i = 0; i < found; i++) {
        if (raw[i][0] == '.' || raw[i][0] == '\0') {
            continue;
        }
        my_strcpy(names[count], raw[i], SQ_SAVE_NAME);
        count++;
    }

    /* 挿入法。件数はたかだか 64 なので、これで十分速い。 */
    for (i = 1; i < count; i++) {
        char key[SQ_SAVE_NAME];

        my_strcpy(key, names[i], sizeof(key));
        for (j = i - 1; (j >= 0) && (my_stricmp(names[j], key) > 0); j--) {
            my_strcpy(names[j + 1], names[j], SQ_SAVE_NAME);
        }
        my_strcpy(names[j + 1], key, SQ_SAVE_NAME);
    }

    return count;
}

/*!
 * @brief 一覧から選んだ 1 件を消す（追補 ⑧。2026-08-23 に決めた）。
 * @param name セーブファイルの綴り
 * @return 1 = 消した／0 = やめた・消せなかった
 *
 * @details **消す前に `[y/n]` で確かめる**（誤爆は取り返しがつかない）。
 * 走査は `portable_list_files()` だが**削除の代役は無い**ので、素の
 * `remove()` を使う（ANSI C。Windows・Android とも通る）。渡す道は
 * **一覧に出ている名前から組んだものだけ**にする。
 */
static int sq_delete_savefile(const char *name)
{
    char path[1024];
    char ask[128];

    if (!name || !name[0] || (name[0] == '.')) {
        return 0; /* 点で始まる名前は一覧に出していない。触らない */
    }

    strnfmt(ask, sizeof(ask), "%s \"%s\"? ", sq_tr("Delete"), name);
    if (!get_check(ask)) {
        return 0;
    }

    path_build(path, sizeof(path), ANGBAND_DIR_SAVE, name);
    if (remove(path) != 0) {
        prt(sq_tr("Could not delete that character."), 0, 0);
        (void)inkey();
        prt("", 0, 0);
        return 0;
    }

    fprintf(stderr, "[silq] deleted %s\n", name);
    fflush(stderr);
    return 1;
}

/*!
 * @brief 一覧から 1 つ選び、`savefile` を組む。
 * @return 1 = 選んだ / 0 = やめた（タイトルへ戻る）
 */
static int sq_pick_savefile(void)
{
    static char names[SQ_SAVE_SCAN][SQ_SAVE_NAME];
    char line[SQ_SAVE_NAME + 16];
    int count;
    int shown;
    int highlight = 0;
    /*!
     * @brief 消す気で選んでいるか（追補 ⑧。2026-08-23 の疑問
     * 「追加した機能は全てコントローラー単体で操作可能か？」への答え）。
     *
     * **`D` はキーボードでしか押せない。** 触りだけの機体（Android・Quest）と
     * ゲームパッドだけで遊ぶ機では、この一覧へ届くのは十字・決定・取消の 3 つだけである。
     * そこで**「消す」を一覧の行にした**——十字で降りて決定を押せば消す気になり、
     * もう一度セーブを選ぶとそれを消す。取消で気が変わる。
     */
    int deleting = 0;
    int i;
    int ch;

    count = sq_collect_savefiles(names);

    screen_save();

    if (count <= 0) {
        Term_clear();
        c_put_str(TERM_WHITE, sq_tr("There are no saved characters."), 8, 5);
        c_put_str(TERM_SLATE, sq_tr("(press any key)"), 10, 5);
        Term_fresh();
        (void)inkey();
        screen_load();
        return 0;
    }

    shown = (count > SQ_SAVE_SHOWN) ? SQ_SAVE_SHOWN : count;

    for (;;) {
        /*
         * 最後の行が「消す」（`shown` 番）。**行の数は `shown + 1`** で、
         * カーソルはそこまで降りられる。
         */
        const int delete_row = shown;
        const int rows_used = (shown < SQ_SAVE_ROWS) ? shown : SQ_SAVE_ROWS;

        Term_clear();
        c_put_str(TERM_WHITE, deleting ? sq_tr("Choose a character to delete")
                                       : sq_tr("Choose a saved character"),
            2, 5);

        for (i = 0; i < shown; i++) {
            const int row = 4 + (i % SQ_SAVE_ROWS);
            const int col = (i < SQ_SAVE_ROWS) ? 7 : SQ_SAVE_COL2;

            /*
             * **letter は a〜z の 26 件まで。**それより後ろは印を空けておく
             * ——無い letter を出すと押せる気にさせてしまう。カーソルでは届く。
             */
            if (i < 26) {
                strnfmt(line, sizeof(line), "%c) %s", 'a' + i, names[i]);
            } else {
                strnfmt(line, sizeof(line), "   %s", names[i]);
            }
            c_put_str((i == highlight) ? TERM_L_BLUE : TERM_WHITE, line, row, col);
        }

        /*
         * **黙って切らない。**入り切らなかった件数はその場で言う
         * （出ていないものを「無い」と読まれるのがいちばん困る）。
         */
        if (count > shown) {
            strnfmt(line, sizeof(line), sq_tr("(%d more not shown)"), count - shown);
            c_put_str(TERM_SLATE, line, 4 + SQ_SAVE_ROWS, 7);
        }

        /*
         * 「消す」の行。**一覧の下**（1 列目の続き）に置く。ここへ十字で降りられるので、
         * キーボードの無い機体でも消せる。`D` は今までどおり近道として残す。
         */
        c_put_str((highlight == delete_row) ? TERM_L_BLUE : TERM_SLATE,
            deleting ? sq_tr("[ stop deleting ]") : sq_tr("[ delete a character ]"),
            5 + rows_used + ((count > shown) ? 1 : 0), 7);

        c_put_str(TERM_SLATE,
            deleting ? sq_tr("Arrows to choose, Enter to delete, Escape to stop")
                     : sq_tr("Arrows to choose, Enter to open, D to delete, Escape to cancel"),
            7 + rows_used + ((count > shown) ? 1 : 0), 5);

        /* 選んでいるマスに印を置く（`initial_menu()` と同じ流儀）。 */
        if (highlight == delete_row) {
            Term_gotoxy(5, 5 + rows_used + ((count > shown) ? 1 : 0));
        } else {
            Term_gotoxy(((highlight < SQ_SAVE_ROWS) ? 7 : SQ_SAVE_COL2) - 2,
                4 + (highlight % SQ_SAVE_ROWS));
        }
        Term_fresh();

        hide_cursor = TRUE;
        ch = inkey();
        hide_cursor = FALSE;

        if (ch == ESCAPE) {
            if (deleting) {
                deleting = 0; /* 消す気をやめるだけ。一覧は閉じない */
                continue;
            }
            screen_load();
            return 0;
        }
        if ((ch == '\r') || (ch == '\n') || (ch == ' ')) {
            /*
             * 「消す」の行なら**気を切り替えるだけ**。セーブの行なら、
             * 消す気なら消し、そうでなければ開く。
             */
            if (highlight == delete_row) {
                deleting = !deleting;
                continue;
            }
            if (deleting) {
                if (sq_delete_savefile(names[highlight])) {
                    count = sq_collect_savefiles(names);
                    if (count <= 0) {
                        screen_load();
                        return 0; /* 全部消した。タイトルへ戻す */
                    }
                    shown = (count > SQ_SAVE_SHOWN) ? SQ_SAVE_SHOWN : count;
                    if (highlight >= shown) {
                        highlight = shown - 1;
                    }
                }
                continue;
            }
            break;
        }
        /*
         * 上下は 1 つずつ（列の中を移る。列の端では隣の列へ繋がる）。
         * 左右は列そのものを移る（`SQ_SAVE_ROWS` ぶん飛ぶ）。
         * 矢印は画面側が `8` `2` `4` `6` にして送ってくる。
         */
        if (ch == '8') {
            if (highlight > 0) {
                highlight--;
            }
            continue;
        }
        if (ch == '2') {
            if (highlight < delete_row) {
                highlight++; /* 一覧の下は「消す」の行（`delete_row`） */
            }
            continue;
        }
        if (ch == '4') {
            if (highlight == delete_row) {
                highlight = shown - 1; /* 「消す」からは一覧の末尾へ戻る */
            } else if (highlight >= SQ_SAVE_ROWS) {
                highlight -= SQ_SAVE_ROWS;
            }
            continue;
        }
        if (ch == '6') {
            if (highlight == delete_row) {
                /* 行き先が無い。動かさない */
            } else if ((highlight + SQ_SAVE_ROWS) < shown) {
                highlight += SQ_SAVE_ROWS;
            }
            continue;
        }
        /*
         * **消すの近道**（追補 ⑧。2026-08-23 に決めた「セーブデータの削除」）。
         *
         * 綴りに `D` を選んだのは、**小文字が全部ふさがっている**からである
         * （`a`〜`z` は選ぶための印）。`d` にすると 4 件目のセーブが選ばれる。
         *
         * **これはキーボードの近道であって、唯一の道ではない。**
         * コントローラだけの機体は一覧の「消す」の行を使う（`deleting`）。
         *
         * 消した後は**読み直す**——名前も件数も letter も動くので、
         * 記憶している添字はどれも当てにならない。
         */
        if (ch == 'D') {
            if ((highlight != delete_row) && sq_delete_savefile(names[highlight])) {
                count = sq_collect_savefiles(names);
                if (count <= 0) {
                    screen_load();
                    return 0; /* 全部消した。タイトルへ戻す */
                }
                shown = (count > SQ_SAVE_SHOWN) ? SQ_SAVE_SHOWN : count;
                if (highlight >= shown) {
                    highlight = shown - 1;
                }
            }
            continue;
        }
        if ((ch >= 'a') && (ch < 'a' + ((shown < 26) ? shown : 26))) {
            highlight = ch - 'a';
            if (deleting) {
                continue; /* 消す気のときは letter では開かない。決定を要る */
            }
            break;
        }
    }

    screen_load();

    /*
     * **名前と道の両方を据える。**`process_player_name()` は名前から道を組み直すが、
     * 綴りを整える（英数字以外を潰す）ので、**ファイル名と 1 バイト違う道**になりうる。
     * そこで組ませたあとに、実在するファイルの道で上書きする。
     */
    my_strcpy(op_ptr->full_name, names[highlight], sizeof(op_ptr->full_name));
    process_player_name(TRUE);
    path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, names[highlight]);

    return 1;
}
/*! @} */

/*!
 * @name 自動で開き直す（`--resume`。言語切り替えの立て直し。設計 §12.2 の案 A）
 *
 * @details 言語は `init_angband()` より前に決まっていなければならないので、遊んでいる
 * 途中で替えるには**コアを畳んで起こし直す**しかない。起こし直したあとに人物を
 * 選び直させるのは筋が悪いので、**畳むときに印を書き、起きるときに読む**。
 *
 * 印は `<save>/.resume` の 1 行（セーブファイルの綴り）。**点で始まる**ので
 * `sq_collect_savefiles()` の一覧には出ない。読んだら消す——残しておくと、
 * 次に `--resume` で起きたとき別の人物が勝手に開く。
 * @{
 */

//! 印の綴り。点で始めること（一覧から外れる条件が「点で始まる」なので）。
#define SQ_RESUME_FILE ".resume"

//! `--resume` が渡されたか。`sq_set_resume()` が立て、`sq_pick_game()` が使って倒す。
static int sq_resume_armed = 0;

/*! @brief 印の道を組む。 */
static void sq_resume_path(char *buf, size_t max)
{
    path_build(buf, (int)max, ANGBAND_DIR_SAVE, SQ_RESUME_FILE);
}

/*!
 * @brief 印を書く（保存した直後に呼ぶ）。
 * @param name セーブファイルの綴り
 * @details 書けなくても遊びは止めない——**次の立て直しでタイトルが出るだけ**である。
 */
static void sq_resume_write(const char *name)
{
    char path[1024];
    FILE *fp;

    if (!name || !name[0]) {
        return;
    }
    sq_resume_path(path, sizeof(path));
    fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "[silq] could not write the resume mark: %s\n", path);
        fflush(stderr);
        return;
    }
    fprintf(fp, "%s\n", name);
    fclose(fp);
}

/*!
 * @brief いま開いている `savefile` の**末尾**（ファイル名だけ）。
 * @details 区切りは `PATH_SEP` だけを見るのでは足りない——Windows では
 * `/` で組まれた道も通る。両方を見て後ろのほうを採る。
 */
static const char *sq_savefile_leaf(void)
{
    const char *leaf = savefile;
    const char *p;

    for (p = savefile; *p; p++) {
        if ((*p == '\\') || (*p == '/')) {
            leaf = p + 1;
        }
    }
    return leaf;
}

/*! @brief 印を消す。無ければ何もしない。 */
static void sq_resume_clear(void)
{
    char path[1024];

    sq_resume_path(path, sizeof(path));
    (void)remove(path);
}

/*!
 * @brief 印を読んで消す。
 * @param name 綴りの受け皿
 * @param max 受け皿の大きさ
 * @return 1 = 読めて、その名前のセーブが実在する／0 = 印が無い・読めない・セーブが無い
 */
static int sq_resume_take(char *name, size_t max)
{
    char path[1024];
    char save_path[1024];
    FILE *fp;
    size_t len;

    sq_resume_path(path, sizeof(path));
    fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }
    name[0] = '\0';
    if (!fgets(name, (int)max, fp)) {
        name[0] = '\0';
    }
    fclose(fp);
    (void)remove(path); /* **必ず消す。**読めても読めなくても、印は一度きりである */

    /* 行末の改行と空白を落とす。 */
    len = strlen(name);
    while ((len > 0) && ((name[len - 1] == '\n') || (name[len - 1] == '\r') || (name[len - 1] == ' '))) {
        name[--len] = '\0';
    }
    if ((len == 0) || (name[0] == '.')) {
        return 0; /* 空・点で始まる（一覧に出ない名前）は取らない */
    }

    /* **実在を確かめる。**無い綴りで `play_game()` へ入ると新規作成が始まる。 */
    path_build(save_path, sizeof(save_path), ANGBAND_DIR_SAVE, name);
    fp = fopen(save_path, "rb");
    if (!fp) {
        fprintf(stderr, "[silq] resume mark names a save that is not there: %s\n", name);
        fflush(stderr);
        return 0;
    }
    fclose(fp);
    return 1;
}

void sq_set_resume(int on)
{
    sq_resume_armed = on ? 1 : 0;
}

/*!
 * @brief 印が在るならそれを開く。
 * @return 1 = 開く（`savefile` を組んだ）／0 = ふつうにタイトルを出す
 */
static int sq_take_resume(void)
{
    char name[SQ_SAVE_NAME];

    if (!sq_resume_armed) {
        return 0;
    }
    sq_resume_armed = 0; /* **一度きり。**遊び終えて戻ってきたらタイトルを出す */

    if (!sq_resume_take(name, sizeof(name))) {
        return 0;
    }

    /*
     * **綴りと道の両方を据える**（`sq_pick_savefile()` の末尾と同じ理由）。
     * `process_player_name()` は名前から道を組み直すが、英数字以外を潰すので
     * ファイル名と 1 バイト違う道になりうる。
     */
    my_strcpy(op_ptr->full_name, name, sizeof(op_ptr->full_name));
    process_player_name(TRUE);
    path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, name);

    fprintf(stderr, "[silq] resuming %s\n", name);
    fflush(stderr);
    return 1;
}
/*! @} */

static int sq_pick_game(bool *new_game)
{
    int highlight = 1;

    /*
     * 言語切り替えの立て直し（`sq_set_resume()`）。**タイトルを出さずに開く。**
     * 印が無ければ（＝ふつうの起動、または保存していない）ここは素通りする。
     */
    if (sq_take_resume()) {
        *new_game = FALSE;
        return 1;
    }

    if (p_ptr->is_dead) {
        highlight = 4;
    }

    /*
     * **押しっぱなしの余りをタイトルへ持ち込まない**（2026-08-23 に決めた
     * 「死亡等でゲームが終わった際、自動でチュートリアルが始まるのをやめて」）。
     *
     * 死んだあとのタイトルは**カーソルが `a) Tutorial` に戻っている**
     * ——`re_init_some_things()` が `WIPE(p_ptr)` するので上の `is_dead` が偽になり、
     * `highlight` は 1 のままである。そして `initial_menu()` は Enter・Space で
     * **その行を実行する**（`init2.c:1811`）。だから死の画面を送るために押した
     * 最後の 1 打がここへ落ちると、チュートリアルが勝手に始まる。
     *
     * 直しは「拾わない」ことにした。**`highlight` を別の行へ移す案は採らない**
     * ——`d) Quit` なら誤爆で窓ごと畳み、`c) Open saved character` なら
     * 名前の待ちへ入る（`askfor_aux` を ESC で抜けても `<name>` のまま進む）。
     * どの行も誤爆すると痛いので、**余った打鍵そのものを捨てる**のが筋である。
     *
     * タイトルが出てから押した 1 打は、この待ちの後に届くので今までどおり効く。
     */
    sq_settle_input(300);

    for (;;) {
        const int choice = initial_menu(&highlight);

        switch (choice) {
        case 1:
            /*
             * チュートリアル。**`sizeof(savefile)` を渡す**——先方は
             * `sizeof(buf)`（局所配列 80）を渡しているが、受け皿は
             * `savefile[1024]`（`externs.h:117`）である。意味は変わらない。
             */
            path_build(savefile, sizeof(savefile), ANGBAND_DIR_XTRA, "tutorial");
            *new_game = FALSE;
            return 1;

        case 2:
            *new_game = TRUE;
            return 1;

        case 3: {
            /*
             * 再開。**一覧から選ぶ**（2026-08-23 に決めた）。
             * 先方（`main.c:597`）は `askfor_aux()` で名前を打たせるが、
             * 触りだけの機体にはキーボードが無く、綴りを 1 字違えれば開けない。
             *
             * **やめたらタイトルへ戻る**（`return` せずに `for` を回す）。
             * 打ち込みの頃は「空で決定」から抜ける道が無く、`<name>` のまま
             * 進んでしまっていた。
             */
            *new_game = FALSE;

            if (!sq_pick_savefile()) {
                break;
            }

            return 1;
        }

        case 4:
            return 0;

        default:
            /* 矢印などで選択が動いただけ。もう一度描く。 */
            break;
        }
    }
}

int sq_run_game(void)
{
    bool new_game = FALSE;

    sq_game_kind = 0;
    sq_shutting_down = 0;

    /*
     * **`--resume` で起きていないなら、残っている印を消す。** 前の回の印を持ち越すと、
     * 次に言語を替えたときに**そのとき遊んでいない人物**が開く。印は一度きりのメッセージである。
     */
    if (!sq_resume_armed) {
        sq_resume_clear();
    }

    if (setjmp(sq_game_buf) != 0) {
        /*
         * `quit()` か `core()` で戻ってきた。**ここが唯一の正常な出口**である
         * （`play_game()` は自力では戻らない）。
         */
        sq_game_armed = 0;
        sq_game_in_progress = 0;
        return sq_game_kind ? sq_game_kind : SQ_OK;
    }

    sq_game_armed = 1;

    /* `main.c:569` の `while (1)`。遊び終えたらタイトルへ戻る。 */
    for (;;) {
        if (!sq_pick_game(&new_game)) {
            /* d) Quit。先方と同じ列（後始末して quit）。 */
            cleanup_angband();
            quit(NULL);
            /* 戻らない（`sq_quit_aux` が上の setjmp へ跳ぶ）。 */
        }

        sq_game_in_progress = 1;

        /* 溜まった入力を捨ててから入る（`main.c:640`）。 */
        Term_flush();

        play_game(new_game);

        sq_game_in_progress = 0;

        /*
         * **遊び終えたらコアを畳む**（2026-08-23 に決めた
         * 「ゲーム終了後、コアの起動画面に遷移させて」）。
         *
         * 先方（`main.c:569`）はここでタイトルへ戻る。**それがチュートリアルの誤爆の元**だった: `close_game()` は保存のあとに人物表と
         * 成績の画を何枚も出し、利用者はそれを送るために決定を連打する。最後の 1 打が
         * タイトルへ落ちると、カーソルが乗っている `a) チュートリアル` が始まる
         * （`re_init_some_things()` の `WIPE(p_ptr)` で `is_dead` が消えるので、
         * カーソルは 1 行目に戻っている）。**捨てるだけでは直らない**——
         * 送り終えた直後の 1 打は、意図した選択と見分けがつかない。
         *
         * だからタイトルへ戻さない。畳んで画面側へ返し、**コア選択へ出す**
         * （`hd2d/app/hd2d_app.h` の `kRunRestart`）。新しい冒険者を作るのも
         * 別のセーブを開くのも、そこから入り直せる。
         *
         * `quit(NULL)` は `sq_run_game()` の頭の `setjmp` へ跳び、`SQ_OK` で返る
         * （`d) Quit` と同じ道）。`re_init_some_things()` はもう要らない
         * ——この処理系で次の局は始まらない。
         */
        cleanup_angband();
        quit(NULL);
        /* 戻らない（`sq_quit_aux` が上の setjmp へ跳ぶ）。 */
    }
}

void sq_shutdown_and_quit(void)
{
    if (sq_shutting_down) {
        /*
         * 再入。`save_player()` はコアの都合で Term を触る（`Term_fresh()` が
         * 挟まると `TERM_XTRA_FRESH` → present → 待ち → ここ、と戻ってくる）ので、
         * この守りが無いと**書きかけのセーブを残す**。
         */
        return;
    }
    sq_shutting_down = 1;

    fprintf(stderr, "[silq] shutdown requested\n");
    fflush(stderr);

    /*
     * `main-win.c:3439`（`WM_CLOSE`）の写し（設計 §3.2）。
     *
     * `msg_flag` を先に落とすのが肝である——`do_cmd_save_game()` は
     * `message_flush()` を通り、そこが `-more-` で `inkey()` を待つことがある。
     * 見る相手のいない場面で待たせるわけにいかない。
     *
     * `save_game_quietly` も立てておく（`files.c:3308` 以降。`prt("Saving game...")`
     * を出さない）。画面はもう畳まれている。
     */
    if (sq_game_in_progress && character_generated) {
        msg_flag = FALSE;
        save_game_quietly = TRUE;
        do_cmd_save_game();
        fprintf(stderr, "[silq] saved\n");
        fflush(stderr);
        /*
         * **どの人物を保存したかを印に残す**（言語切り替えの立て直し。§12.2 の案 A）。
         * 画面側は綴りを知らなくてよくなる。
         *
         * 綴りは **`savefile` の末尾**から採る。`op_ptr->base_name` ではない——
         * `sq_pick_savefile()` は実在するファイルの道で `savefile` を上書きするので、
         * `process_player_name()` が組んだ綴りと 1 バイト違うことがある（同関数の末尾の註）。
         * 書き込んだ先そのものを控えるのが確実である。
         */
        sq_resume_write(sq_savefile_leaf());
    } else {
        fprintf(stderr, "[silq] nothing to save (character_generated=%d)\n",
            character_generated ? 1 : 0);
        fflush(stderr);
    }

    /*
     * `quit(NULL)` は `sq_quit_aux` を通り、進行の段の跳び先へ跳ぶ
     * （＝`sq_run_game()` が `SQ_OK` を返す）。**ここから先は実行されない。**
     */
    quit(NULL);
}
