/*!
 * @file sq_null_term.c
 * @brief 旧 z-term のヘッドレスドライバ（画を持たない Term を 1 枚立てる）。
 *
 * 基準は / §3.2。手本は `gensoband/adapter/gb_null_term.c`。
 *
 * ## なぜ要るのか
 * **Term が 1 枚も無いと `init_angband()` は落ちる。** あちらは進捗を
 * `note()` で画面に書く。中身は（`silq/src/init2.c:1587`）
 *
 *     Term_erase(0, 23, 255); Term_putstr(20, 23, -1, TERM_SLATE, str); Term_fresh();
 *
 * で、`Term` が NULL なら即死、行数が 24 未満でも書けない。
 *
 * ## 実装する必要があるフックは xtra だけ
 * `term_init()` が curs / bigcurs / wipe / text / pict に「hack」フック
 * （何もせず 0 を返す関数）を自分で入れてくれる（`silq/src/z-term.c`）。
 * 一方 `Term_xtra()` は `if (!Term->xtra_hook) return (-1);` なので、
 * **xtra_hook が無いと Term_fresh() が失敗する**。だからここは xtra だけ書く。
 *
 * 画面の中身は z-term.c 自身が `Term->scr` に積んでくれる。ドライバが
 * 何も描かなくても、そこを読めばミラーは取れる（`sq_term_row`）。
 *
 * ## 待ちの契約（`silq/src/z-term.c` の `Term_inkey` を読むこと）
 *     while (Term->key_head == Term->key_tail) Term_xtra(TERM_XTRA_EVENT, TRUE);
 * **キューが空のまま返ると呼び直される**（無限ループにはならないが、返るたびに
 * 回るので中で少し寝ないと CPU を焼く）。だからここでは「キーが来るまで中で回る」形にする。
 */

#include "angband.h"

#include "sq_shim.h"

#include <stdio.h>
#include <string.h>

/*! 立てた Term の実体。1 枚しか立てない（設計 §3.2 の「主 Term は 80x27 固定」）。 */
static term sq_term_body;
static int sq_term_ready = 0;

/*! ホスト（C++ 側）のフック。差さっていなければ `--selftest` 用の逃げ道に落ちる。 */
static sq_host_hooks sq_hooks;
static int sq_hooks_ready = 0;

/*! 待ちの空回り 1 周の長さ（ミリ秒）。変愚の `sdl_null_term.cpp` と同じ 10ms。 */
#define SQ_IDLE_SLICE_MS 10

void sq_set_host_hooks(const sq_host_hooks *hooks)
{
    if (hooks) {
        sq_hooks = *hooks;
        sq_hooks_ready = 1;
    } else {
        memset(&sq_hooks, 0, sizeof(sq_hooks));
        sq_hooks_ready = 0;
    }
}

/*! 画を送る（`present`）。フックが無ければ何もしない。 */
static void sq_present(void)
{
    if (sq_hooks_ready && sq_hooks.present) {
        sq_hooks.present();
    }
}

/*!
 * @brief 受信キューにあるだけキーを Term へ積む。
 * @return 積んだ個数
 *
 * @details **末尾へ積む**（`Term_keypress`）。`Term_key_push` は先頭挿入なので、
 * 1 回に 2 個以上積むと消費順が逆転する（変愚で踏んだ穴。K-20）。
 */
static int sq_drain_keys_into_term(void)
{
    int pushed = 0;
    int key;

    if (!sq_hooks_ready || !sq_hooks.next_key) {
        return 0;
    }

    while ((key = sq_hooks.next_key()) > 0) {
        (void)Term_keypress(key);
        pushed++;
        /* 待ち行列の容量（term_init の第 4 引数）を超えて押し込まない。 */
        if (pushed >= 256) {
            break;
        }
    }

    return pushed;
}

void sq_settle_input(int ms)
{
    int waited = 0;

    if (!sq_hooks_ready) {
        return;
    }

    /*
     * **捨てる → 待つ → もう一度捨てる。**1 度きりだと、捨てた瞬間にまだ線の途中に
     * いた押しが後から届く（画面とコアは別プロセスで、押しは非同期に流れてくる）。
     * 待っている間も画を送る——止めると画面が固まって見える。
     */
    if (sq_hooks.drop_keys) {
        sq_hooks.drop_keys();
    }
    (void)Term_flush();

    while (waited < ms) {
        sq_present();
        if (sq_hooks.sleep_ms) {
            sq_hooks.sleep_ms(SQ_IDLE_SLICE_MS);
        }
        waited += SQ_IDLE_SLICE_MS;
    }

    if (sq_hooks.drop_keys) {
        sq_hooks.drop_keys();
    }
    (void)Term_flush();
}

/*!
 * @brief `TERM_XTRA_EVENT` の本体。
 * @param wait 真なら「キーが来るまで待て」
 * @return 0（何か積んだ・待った）/ 1（非待機で何も無かった）
 */
static errr sq_term_event(int wait)
{
    if (!sq_hooks_ready) {
        /*
         * フックが差さっていない（`--selftest`）。ここで本当に待つと
         * `-more-` の類で固まり、初期化の失敗報告すら届かなくなる。
         */
        if (wait) {
            (void)Term_keypress(ESCAPE);
        }
        return 0;
    }

    if (!wait) {
        return sq_drain_keys_into_term() ? 0 : 1;
    }

    for (;;) {
        /*
         * **待っている間も画を送る。** 「描いたあと入力を待つだけ」の場面
         * （タイトル・誕生・死亡プロンプト）は、ここで送らないと画面側に
         * 何も出ないまま固まる。
         */
        sq_present();

        if (sq_drain_keys_into_term()) {
            return 0;
        }

        if (sq_hooks.shutdown_requested && sq_hooks.shutdown_requested()) {
            /* 保存して畳む。**戻らない**（設計 §3.2）。 */
            sq_shutdown_and_quit();
        }

        if (sq_hooks.sleep_ms) {
            sq_hooks.sleep_ms(SQ_IDLE_SLICE_MS);
        }
    }
}

/*!
 * @brief 唯一のフック。
 *
 * 大半は no-op でよい（画を持たないので描く先が無い）。**0 を返すこと**──
 * 非 0 は z-term.c 側で失敗として扱われる（`TERM_XTRA_EVENT` の 1 だけは
 * 「キーが無い」の意味で、あちらが承知している）。
 */
static errr sq_term_xtra(int n, int v)
{
    switch (n) {
    case TERM_XTRA_EVENT:
        return sq_term_event(v);

    case TERM_XTRA_FLUSH:
        /*
         * 溜まった鍵を捨てる。**受信キュー側も捨てる**——ここで捨てないと、
         * コアが「捨てた」と思っている入力が次の待ちで蘇る。
         */
        if (sq_hooks_ready && sq_hooks.drop_keys) {
            sq_hooks.drop_keys();
        }
        return 0;

    case TERM_XTRA_FRESH:
        /* 画が確定した。ここが frame の主な出どころ。 */
        sq_present();
        return 0;

    case TERM_XTRA_DELAY:
        /*
         * 演出の間。ヘッドレスでも**捨てない**——捨てると弾道や爆風が
         * 1 フレームに潰れ、画面側が動きを見せられない。ただし長すぎる待ちは
         * 応答を殺すので上限を付ける。
         */
        if (sq_hooks_ready && sq_hooks.sleep_ms && (v > 0)) {
            sq_hooks.sleep_ms((v > 200) ? 200 : v);
        }
        return 0;

    /*
     * 音（`TERM_XTRA_SOUND`）は**ここでは鳴らさない**。名前とマスをフレームへ
     * 載せ、鳴らすのは画面側である。
     * Sil-Q に `TERM_XTRA_MUSIC_*` は無いので曲の枝も無い。
     */
    case TERM_XTRA_SOUND:
        sq_sound_push(v);
        return 0;

    case TERM_XTRA_CLEAR:
    case TERM_XTRA_SHAPE:
    case TERM_XTRA_FROSH:
    case TERM_XTRA_NOISE:
    case TERM_XTRA_BORED:
    case TERM_XTRA_REACT:
    case TERM_XTRA_ALIVE:
    case TERM_XTRA_LEVEL:
        return 0;

    default:
        /* 知らない要求も握り潰す。先方が番号を足しても止まらないように。 */
        return 0;
    }
}

int sq_term_install(int cols, int rows)
{
    term *t = &sq_term_body;

    if (sq_term_ready) {
        return SQ_ERR_STATE;
    }

    /*
     * `note()` が 23 行目に書き、`play_game()` が 80x24 未満を弾く
     * （`dungeon.c:2944`）。`term` の `wid` / `hgt` は byte なので上限も見る。
     */
    if ((cols < 80) || (rows < 24) || (cols > 255) || (rows > 255)) {
        return SQ_ERR_ARG;
    }

    /* 第 4 引数は鍵の待ち行列の長さ（main-win.c の td->keys と同じ役目）。 */
    if (term_init(t, cols, rows, 1024)) {
        return SQ_ERR_TERM;
    }

    /* 画を持たないのでカーソルは自前で描く扱いにしておく（描かないが）。 */
    t->soft_cursor = TRUE;
    t->attr_blank = TERM_WHITE;
    t->char_blank = ' ';

    /* 要るのはこれだけ。他は term_init() が hack を入れてある。 */
    t->xtra_hook = sq_term_xtra;

    /*
     * `inkey()` は `angband_term[0]` を活性にし直す。
     * ここを埋めておかないとそこで NULL 参照になる。
     */
    angband_term[0] = t;
    Term_activate(t);

    sq_term_ready = 1;
    return SQ_OK;
}

void sq_term_remove(void)
{
    if (!sq_term_ready) {
        return;
    }

    Term_activate(NULL);
    angband_term[0] = NULL;
    (void)term_nuke(&sq_term_body);
    sq_term_ready = 0;
}

int sq_term_size(int *cols, int *rows)
{
    if (!sq_term_ready) {
        return SQ_ERR_STATE;
    }

    if (cols) {
        *cols = (int)sq_term_body.wid;
    }
    if (rows) {
        *rows = (int)sq_term_body.hgt;
    }
    return SQ_OK;
}

int sq_term_row(int y, char *text, unsigned char *attr, int cap)
{
    const term_win *win;
    int n;
    int x;

    if (!sq_term_ready) {
        return SQ_ERR_STATE;
    }
    if (!text || (cap <= 1) || (y < 0) || (y >= (int)sq_term_body.hgt)) {
        return SQ_ERR_ARG;
    }

    /*
     * `scr` が「これから見せる画」、`old` が「もう見せた画」。
     * `Term_fresh()` は scr を old へ写す。**読むのは scr** で、
     * fresh 前でも最新が取れる。
     */
    win = sq_term_body.scr;
    if (!win) {
        return SQ_ERR_STATE;
    }

    n = (int)sq_term_body.wid;
    if (n > cap - 1) {
        n = cap - 1;
    }

    for (x = 0; x < n; x++) {
        text[x] = win->c[y][x];
        if (attr) {
            /*
             * **16 に畳む**（設計 §4.1）。Sil-Q は `MAX_COLORS 32` で、
             * 16 以上は `TERM_SHADE`（= 16）を足した「暗い写し」である。
             * 当方の `TermPalette` は 16 固定なので、ここが唯一の畳み口。
             */
            attr[x] = (unsigned char)(win->a[y][x] & 0x0F);
        }
    }
    text[n] = '\0';

    return n;
}

int sq_term_cursor(int *x, int *y, int *visible)
{
    const term_win *win;

    if (!sq_term_ready) {
        return SQ_ERR_STATE;
    }

    win = sq_term_body.scr;
    if (!win) {
        return SQ_ERR_STATE;
    }

    if (x) {
        *x = (int)win->cx;
    }
    if (y) {
        *y = (int)win->cy;
    }
    if (visible) {
        /* `cu` = cursor useless（画面外・隠し）。`cv` = cursor visible。 */
        *visible = (!win->cu && win->cv) ? 1 : 0;
    }
    return SQ_OK;
}

/*!
 * @brief 変化検出用の安い digest（`sq_shim.h` の説明）。
 *
 * @details ここに置いてあるのは **Term の画（`sq_term_body.scr`）が要る**ためで、
 * あれはこの TU の静的変数である。混ぜるものは「画面に出るものが変われば必ず動く」
 * 材料に限る。FNV-1a（64bit）。
 */
unsigned long long sq_change_digest(void)
{
    const unsigned long long prime = 1099511628211ULL;
    unsigned long long h = 14695981039346656037ULL;
    const term_win *win;
    int y;
    int x;

#define SQ_MIX(BYTE) \
    do { \
        h ^= (unsigned long long)(unsigned char)(BYTE); \
        h *= prime; \
    } while (0)
#define SQ_MIX_INT(V) \
    do { \
        unsigned long v_ = (unsigned long)(V); \
        SQ_MIX(v_ & 0xFF); \
        SQ_MIX((v_ >> 8) & 0xFF); \
        SQ_MIX((v_ >> 16) & 0xFF); \
        SQ_MIX((v_ >> 24) & 0xFF); \
    } while (0)

    if (sq_term_ready && (win = sq_term_body.scr) != NULL) {
        for (y = 0; y < (int)sq_term_body.hgt; y++) {
            for (x = 0; x < (int)sq_term_body.wid; x++) {
                SQ_MIX(win->c[y][x]);
                SQ_MIX(win->a[y][x]);
            }
        }
        SQ_MIX_INT(win->cx);
        SQ_MIX_INT(win->cy);
        SQ_MIX(win->cu ? 1 : 0);
        SQ_MIX(win->cv ? 1 : 0);
    }

    SQ_MIX_INT(turn);
    SQ_MIX_INT(message_num());
    SQ_MIX(character_generated ? 1 : 0);
    SQ_MIX(character_icky ? 1 : 0);
    SQ_MIX(character_xtra ? 1 : 0);
    SQ_MIX(character_dungeon ? 1 : 0);
    /*
     * **文字入力の待ちも混ぜる**（W1）。これを落とすと、`askfor_aux()` が画を
     * 描いてから待ちに入っても**マスが 1 つも変わらない**ので門で止まり、旗が
     * 画面まで届かない。届かなければ IME は切れず、非 ASCII も送られない。
     * 幻想蛮怒が実測で踏んだ穴と同じ形（`gb_null_term.c:394` の註記）。
     */
    SQ_MIX(sq_text_input_active() ? 1 : 0);
    /*
     * **音の累計も混ぜる**。音は一度きりの出来事で、
     * 鳴った瞬間の Term が 1 セルも動かない場面がある（扉を開ける音・穴を掘る音）。
     * 混ぜないとその 1 枚が線に乗らず、音が次の別の変化まで遅れる。
     */
    SQ_MIX_INT(sq_sound_total());
    /*
     * **コアが見ている区画も混ぜる**（2026-08-26。`L` ＝ 地図を動かす）。
     * `p_ptr->wy` / `wx` は画面側が窓を切るのに読む値なので、
     * 記憶 [[silq-flag-must-be-in-digest]] の言うとおり混ぜないと届かない。
     * いまは区画が動けば Term の画も丸ごと変わるので実害は出ていないが、
     * **「画が変わるから大丈夫」は根拠として弱い**（同じ画に見える階もありうる）。
     */
    SQ_MIX_INT(p_ptr->wx);
    SQ_MIX_INT(p_ptr->wy);
    SQ_MIX(sq_locate_active() ? 1 : 0);
    /*
     * **命令待ちも混ぜる**（2026-08-23。上と**まったく同じ穴**である）。
     *
     * `inkey_flag` が立つのは「画を描き終えて `request_command()` が `inkey()` を
     * 呼ぶ直前」で、そのとき**マスは 1 つも変わっていない**。混ぜないと digest が
     * 動かず capture が省かれ、`frame.awaiting_command` が真になったフレームが
     * **1 枚も送られない**——画面側では A がいつまでも決定のままになる
     * （2026-08-23 に気づいた「a ボタンがコマンドメニューになっていない」）。
     *
     * 増える capture は**旗の上げ下げの 2 回だけ**（1 手につき 2 枚）で、
     * 待っている間の空回りでは digest が動かないので増えない。
     */
    SQ_MIX(sq_awaiting_command() ? 1 : 0);

    if (character_generated) {
        SQ_MIX_INT(p_ptr->py);
        SQ_MIX_INT(p_ptr->px);
        SQ_MIX_INT(p_ptr->chp);
        SQ_MIX_INT(p_ptr->csp);
        SQ_MIX_INT(p_ptr->depth);
        SQ_MIX(p_ptr->is_dead ? 1 : 0);
    }

#undef SQ_MIX_INT
#undef SQ_MIX

    return h;
}
