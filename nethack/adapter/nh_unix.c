/* NetHack のコアの Linux 専用の部分（C）。。
 *
 * Windows では windmain.c・windsys.c（と consoletty.c）が持っている関数を、Linux では原作の
 * sys/unix/unixmain.c（文字端末か GUI がある前提の入口）と sys/share/unixtty.c（curses.h を要する）を
 * 組まないので、ここで書く。原作は書き換えない。
 * getlock・eraseoldlocks・regularize などは原作の sys/unix/unixunix.c の物をそのまま使う。
 *
 * このファイルは Linux だけで組む（Windows の組み立て tools/nethack/build_windows.py には入れない）。
 */
#ifndef _WIN32

#include "hack.h"
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---- 置き場の用意 ---- */

/* 空のファイルが無ければ作る。ある・作れたら 0、作れなければ -1 */
static int
touch_file(const char *dir, const char *name)
{
    char path[BUFSZ * 2];
    int fd;

    if (snprintf(path, sizeof path, "%s%s", dir, name) >= (int) sizeof path)
        return -1;
    fd = open(path, O_RDWR | O_CREAT, FCMASK);
    if (fd < 0)
        return -1;
    (void) close(fd);
    return 0;
}

/*
 * 書く場所（セーブの置き場）の用意。nh_game_main が、原作の初期化より前に呼ぶ。
 * 原作の Makefile.top の install と同じ物を、コアの起動のたびに無ければ作る:
 *   空のファイル perm（ロックの土台。原作の USE_FCNTL の lock_file が O_RDWR で開く）・record・logfile・
 *   xlogfile・livelog、フォルダ save/（セーブは save/<uid><名前>.gz。files.c の set_savefile_name）。
 * 組み立てのときではなく起動のときに作る理由: 書く場所は遊ぶ人のデータの置き場（言語ごと）であって、
 * 実行ファイルの隣の読むだけのデータ（nethack/data/<en|ja>）ではないため。
 * dir は末尾が '/' のフォルダ。成功で 1、失敗で 0。
 *
 * ほかに、SIGPIPE を無視にする。画面が先に居なくなったあと、プロトコルへ書いてもプロセスごと落ちないように
 * するため（presentation/frame/protocol_transport.cpp の注意）。
 * 原作の入口がやる umask(0777 & ~FCMASK) は呼ばない。作るファイルの許可は遊ぶ人の umask に任せる。
 */
int
nh_unix_prepare(const char *dir)
{
    char path[BUFSZ * 2];
    static const char *const files[] = { "perm", "record", "logfile", "xlogfile", "livelog",
                                         (const char *) 0 };
    int i;

    (void) signal(SIGPIPE, SIG_IGN);
    for (i = 0; files[i]; i++) {
        if (touch_file(dir, files[i]) != 0)
            return 0;
    }
    if (snprintf(path, sizeof path, "%ssave", dir) >= (int) sizeof path)
        return 0;
    if (mkdir(path, 0777) != 0 && errno != EEXIST)
        return 0;
    return 1;
}

/* ---- 原作では unixmain.c にある、ほかのファイルが使う関数 ---- */

/* earlyarg.c が呼ぶ（原作では $USER などから plname を決める）。
   名前は画面の askname で問うので、何もしない */
boolean
whoami(void)
{
    return FALSE;
}

/* end.c・files.c が使う。原作は SIGHUP・SIGTERM に handler を結ぶが、このコアの終わらせ方は
   画面からのプロトコル（exit の通知・パイプの切断）で、そちらが nh_request_hangup で保存して終わる。
   シグナルの配送先が受信スレッドになりうるので、結ばない（Windows の windsys.c と同じく何もしない） */
void
sethanguphandler(void (*handler)(int))
{
    (void) handler;
}

/* cfgfiles.c・files.c が使う（unixmain.c と同じ） */
void
append_slash(char *name)
{
    char *ptr;

    if (!*name)
        return;
    ptr = name + (strlen(name) - 1);
    if (*ptr != '/') {
        *++ptr = '/';
        *++ptr = '\0';
    }
}

/* rnd.c が使う（unixmain.c と同じ。/dev/urandom から読む） */
unsigned long
sys_random_seed(void)
{
    unsigned long seed = 0L;
    unsigned long pid = (unsigned long) getpid();
    boolean no_seed = TRUE;
    FILE *fptr = fopen(DEV_RANDOM, "r");

    if (fptr) {
        if (fread(&seed, sizeof (long), 1, fptr) == 1) {
            has_strong_rngseed = TRUE; /* decl.c */
            no_seed = FALSE;
        }
        (void) fclose(fptr);
    }
    if (no_seed) {
        seed = (unsigned long) getnow();
        if (pid) {
            if (!(pid & 3L))
                pid -= 1L;
            seed *= pid;
        }
    }
    return seed;
}

/* allmain.c が使う。NHUUID を定義しない組み方なので何もしない */
void
get_nhuuid(void)
{
}

void
free_nhuuid(void)
{
}

/* cmd.c（^Z）と pager.c が使う。原作では sys/share/ioctl.c・unixtty.c にあるが、
   文字端末の入出力の設定を要するので組まず、何もしない物を置く */
int
dosuspend(void)
{
    Norep("Suspend command not available.");
    return ECMD_OK;
}

void
intron(void)
{
}

void
introff(void)
{
}

/*
 * 致命的な誤り。原作では sys/share/unixtty.c にある。unixtty.c は curses.h を要し組めないので、
 * 同じ働き（画面を閉じて 1 行出して終わる）を書く。
 * **これを定義しないとリンクが通ってしまう**——glibc に別の error(int, int, const char *, ...) があり、
 * 原作の error(const char *, ...) の呼び出しがそちらに結ばれて、呼ぶと落ちる（終了コード 139）。
 * 呼び出しを glibc の error と区別するため、原作と同じ宣言（hack.h → extern.h）を見たうえで定義する。
 */
void
error(const char *s, ...)
{
    va_list the_args;

    va_start(the_args, s);
    if (iflags.window_inited)
        exit_nhwindows((char *) 0);
    vfprintf(stderr, s, the_args);
    (void) fputc('\n', stderr);
    va_end(the_args);
    exit(EXIT_FAILURE);
}

#endif /* !_WIN32 */
