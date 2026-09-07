/*!
 * @file fc_null_term.c
 * @brief 旧 z-term のヘッドレスドライバ（画を持たない Term を 1 枚立てる）。
 *
 * 基準は / §3.2。
 * 形は `gensoband/adapter/gb_null_term.c` の写しである（Frox は変愚の直系なので
 * z-term の作りが同じ。検討 §2）。**違いは 1 つだけ**——Frox には
 * `TERM_XTRA_MUSIC_*` が無いので、音の枝が丸ごと要らない（設計 §3 の「作らないもの」）。
 *
 * ## なぜ要るのか
 * **Term が 1 枚も無いと `init_angband()` は落ちる。** あちらは進捗を
 * note() で画面に書く。note() の中身は（`frox/src/init2.c`）
 *
 *     Term_erase(0, 23, 255); Term_putstr(20, 23, -1, TERM_WHITE, str); Term_fresh();
 *
 * で、`Term` が NULL なら即死、行数が 24 未満でも書けない。
 *
 * ## 実装する必要があるフックは xtra だけ
 * `term_init()` が curs / bigcurs / wipe / text / pict に「hack」フック
 * （何もせず 0 を返す関数）を自分で入れてくれる（`frox/src/z-term.c`）。
 * 一方 `Term_xtra()` は `if (!Term->xtra_hook) return (-1);` なので、
 * **xtra_hook が無いと Term_fresh() が失敗する**。だからここは xtra だけ書く。
 *
 * 画面の中身は z-term.c 自身が `Term->scr` に積んでくれる。ドライバが
 * 何も描かなくても、そこを読めばミラーは取れる（`fc_term_row`）。
 *
 * ## 待ちの契約（`frox/src/z-term.c` の `Term_inkey` を読むこと）
 * `Term_inkey(ch, TRUE, ...)` は
 *
 *     while (Term->key_head == Term->key_tail) Term_xtra(TERM_XTRA_EVENT, TRUE);
 *
 * である。**キューが空のまま返ると呼び直される**（無限ループにはならないが、
 * 返るたびに回るので中で少し寝ないと CPU を焼く）。だからここでは
 * 「キーが来るまで中で回る」形にしてある。
 */

#include "angband.h"

#include "fc_lang_c.h" /* 自由入力の旗（追補 A1） */
#include "fc_shim.h"

#include <stdio.h>
#include <string.h>

/*! 立てた Term の実体。主 Term は 1 枚だけ（サブパネルは P6 の `fc_sub_terms.c`）。 */
static term fc_term_body;
static int fc_term_ready = 0;

/*! ホスト（C++ 側）のフック。差さっていなければ逃げ道に落ちる。 */
static fc_host_hooks fc_hooks;
static int fc_hooks_ready = 0;

/*! 待ちの空回り 1 周の長さ（ミリ秒）。変愚の `sdl_null_term.cpp` と同じ 10ms。 */
#define FC_IDLE_SLICE_MS 10

/* ---------------------------------------------- 弾道・爆発の観測（FH-05） */

/*
 * コアは弾道を「セルを描く → cursor 合わせ → `Term_fresh()` →
 * `TERM_XTRA_DELAY`」の署名で描く（`spells1.c:2903-2906`。爆発は環ごとに
 * 複数セル → fresh → DELAY）。ここでは text_hook で「fresh 1 回ぶんの
 * 書き込み」を束ね、**fresh の直後に（間に 1 バイトも書かずに）DELAY が
 * 来たときだけ**その束を見せ場として拾う。
 *
 * この「直後」の条件が要——移動・釣り・敵の演出待ちなど、他の DELAY は
 * fresh と DELAY の間に別の書き込みが挟まる（`cmd1.c:6782` の移動は
 * fresh より**前**に DELAY）。署名が合わない束は黙って捨てる。
 *
 * 拾うのは記号 `* | - / \`（`bolt_pict` の 5 種）だけ。矢や投げ物の飛翔
 * （物の記号で描かれる）はここでは拾わない——鉱脈の `*` のような
 * 地図の字と見分けが付かなくなるため。命中そのものは HP の走査
 * （`fc_fx_scan_deltas`）が持つ。
 */

/*! 候補セル（弾道の記号）の上限。大きい息の環でも 1 環はこれに収まる。 */
#define FC_FX_BATCH_MAX 96

typedef struct fc_fx_cell {
    short x; /*!< Term の桁・行（世界のマスへは拾うときに直す） */
    short y;
    char ch;
    unsigned char attr;
} fc_fx_cell;

/*!
 * fresh 1 回ぶんの書き込みを**分類しながら**束ねる:
 *
 * - **行 0** … メッセージ行。`msg_print` は fresh を挟まないので、弾道と同じ束に
 *   メッセージの字が乗る（実測 2026-08-24: `The Floating orb casts ...` の 48 字で
 *   束が溢れて弾道ごと捨てていた）。fx にはならないが署名も破らない——**数えない**。
 * - **地図の矩形**（行 1〜、桁 < wid-13）… 記号 `* | - / \` は**候補**、
 *   それ以外は **other**（署名破り。地図の描き直し・移動・敵の動きの印）。
 * - **状態列・最下段** … HP の描き直しが戦闘と同時に走るだけ。数えない。
 */
static fc_fx_cell fc_fx_pending[FC_FX_BATCH_MAX];
static int fc_fx_pending_n = 0;
static int fc_fx_pending_other = 0;
static int fc_fx_pending_poisoned = 0; /*!< 候補が溢れた（超大物の環）。その束は捨てる */
static fc_fx_cell fc_fx_armed[FC_FX_BATCH_MAX];
static int fc_fx_armed_n = 0;
static int fc_fx_armed_other = 0;
static int fc_fx_armed_poisoned = 0;
/*! 直前の fresh 時点のカーソル位置。soft カーソルの復元描画を読み飛ばす。 */
static int fc_fx_cursor_x = -1;
static int fc_fx_cursor_y = -1;
/*! この fresh サイクルで text_hook を 1 度でも見たか（復元は必ず先頭に来る）。 */
static int fc_fx_saw_text_this_cycle = 0;

static int fc_fx_is_missile_char(char ch)
{
    return (ch == '*') || (ch == '|') || (ch == '-') || (ch == '/') || (ch == '\\');
}

/*!
 * @brief text_hook。書き込みを分類して束ねるだけで、描く先は無い（0 を返す）。
 * @details `soft_cursor` の Term_fresh は「前のカーソルの下のセル」を text_hook で
 * 1 セルだけ描き直す（`z-term.c:1352`）。それは弾道ではないので束に入れない。
 * **見分けは「サイクルの先頭」で行う**——復元は Term_fresh の中で行の差分より
 * 必ず先に呼ばれる。位置だけで見分けてはいけない: 隣の敵の弾はプレイヤーの
 * マス（＝カーソルの真下）に描かれるので、位置で弾くと隣接弾が丸ごと消える。
 */
static errr fc_term_text(int x, int y, int n, byte a, cptr s)
{
    int i;
    int map_w;

    const int first_call = !fc_fx_saw_text_this_cycle;
    fc_fx_saw_text_this_cycle = 1;
    if (first_call && (n == 1) && (x == fc_fx_cursor_x) && (y == fc_fx_cursor_y)) {
        return 0;
    }
    if (!fc_term_ready) {
        return 0;
    }
    if (y == 0) {
        return 0; /* メッセージ行（分類の節註） */
    }
    map_w = (int)fc_term_body.wid - 13; /* `ui_map_rect()` と同じ幅（xtra2.c:3486） */
    if ((y >= (int)fc_term_body.hgt - 1) || (x >= map_w)) {
        return 0; /* 最下段と状態列。戦闘と同時に描き直るだけなので数えない */
    }
    for (i = 0; i < n; i++) {
        if ((x + i) >= map_w) {
            break;
        }
        if (!fc_fx_is_missile_char(s[i])) {
            fc_fx_pending_other++;
            continue;
        }
        if (fc_fx_pending_n >= FC_FX_BATCH_MAX) {
            fc_fx_pending_poisoned = 1;
            continue;
        }
        fc_fx_pending[fc_fx_pending_n].x = (short)(x + i);
        fc_fx_pending[fc_fx_pending_n].y = (short)y;
        fc_fx_pending[fc_fx_pending_n].ch = s[i];
        fc_fx_pending[fc_fx_pending_n].attr = (unsigned char)(a & 0x0F);
        fc_fx_pending_n++;
    }
    return 0;
}

/*! @brief fresh が締まった。束を「装填」へ移し、カーソル位置を覚える。 */
static void fc_fx_on_fresh(void)
{
    memcpy(fc_fx_armed, fc_fx_pending, (size_t)fc_fx_pending_n * sizeof(fc_fx_cell));
    fc_fx_armed_n = fc_fx_pending_n;
    fc_fx_armed_other = fc_fx_pending_other;
    fc_fx_armed_poisoned = fc_fx_pending_poisoned;
    fc_fx_pending_n = 0;
    fc_fx_pending_other = 0;
    fc_fx_pending_poisoned = 0;
    fc_fx_saw_text_this_cycle = 0;
}

/*! @brief DELAY が来た。署名が合えば装填中の束を見せ場として拾う。 */
static void fc_fx_on_delay(void)
{
    int i;
    rect_t mr;

    /* 消費は必ずする（拾わなかった束を later の DELAY に化けさせない）。 */
    const int n = fc_fx_armed_n;
    const int other = fc_fx_armed_other;
    const int poisoned = fc_fx_armed_poisoned;
    fc_fx_armed_n = 0;
    fc_fx_armed_other = 0;
    fc_fx_armed_poisoned = 0;

    if ((n <= 0) || poisoned) {
        return;
    }
    /*
     * 署名の判定は **pending（fresh と DELAY の間の書き込み）だけ**で行う——
     * 移動（`cmd1.c:6782` は fresh より先に DELAY）・敵の突進（`cmd1.c:2062` は
     * fresh の後に lite_spot）・釣り（描かない）はどれもこれで弾ける。
     *
     * 束の中の候補**以外**のセル（other）は弾かない。弾の fresh には正当な
     * 同伴書き込みが乗るのが常だから——敵が起きた・動いたターンの地図の
     * 描き直しが同じ fresh に束ねられる（実測 2026-08-24: 隣の敵の Magic
     * Missile で候補 1 に同伴 67）。代償は「敵の詠唱と同じ fresh で新たに
     * 照らされた鉱脈の `*` が 1 度だけ火花に見える」ことがある程度で、
     * 弾を丸ごと落とすより小さい。
     */
    if ((fc_fx_pending_n > 0) || (fc_fx_pending_other > 0) || fc_fx_pending_poisoned) {
        return;
    }
    (void)other;
    if (!character_generated || !character_dungeon || character_icky || character_xtra) {
        return;
    }
    if (p_ptr->is_dead) {
        return;
    }

    mr = ui_map_rect();
    for (i = 0; i < n; i++) {
        point_t cave_pt;

        if (!rect_contains_pt(mr, fc_fx_armed[i].x, fc_fx_armed[i].y)) {
            continue;
        }
        cave_pt = ui_pt_to_cave_pt(point(fc_fx_armed[i].x, fc_fx_armed[i].y));
        if (!in_bounds2(cave_pt.y, cave_pt.x)) {
            continue;
        }
        fc_fx_push_spark(cave_pt.y, cave_pt.x, fc_fx_armed[i].ch, fc_fx_armed[i].attr);
    }
}

void fc_set_host_hooks(const fc_host_hooks *hooks)
{
    if (hooks) {
        fc_hooks = *hooks;
        fc_hooks_ready = 1;
    } else {
        memset(&fc_hooks, 0, sizeof(fc_hooks));
        fc_hooks_ready = 0;
    }
}

/*! 画を送る（`present`）。フックが無ければ何もしない。 */
static void fc_present(void)
{
    if (fc_hooks_ready && fc_hooks.present) {
        fc_hooks.present();
    }
}

/*!
 * @brief 受信キューにあるだけキーを Term へ積む。
 * @return 積んだ個数
 *
 * @details **末尾へ積む**（`Term_keypress`）。`Term_key_push` は先頭挿入なので、
 * 1 回に 2 個以上積むと消費順が逆転する。
 */
static int fc_drain_keys_into_term(void)
{
    int pushed = 0;
    int key;

    if (!fc_hooks_ready || !fc_hooks.next_key) {
        return 0;
    }

    while ((key = fc_hooks.next_key()) > 0) {
        (void)Term_keypress(key);
        pushed++;
        /* 待ち行列の容量（term_init の第 4 引数）を超えて押し込まない。 */
        if (pushed >= 256) {
            break;
        }
    }

    return pushed;
}

/*!
 * @brief `TERM_XTRA_EVENT` の本体。
 * @param wait 真なら「キーが来るまで待て」
 * @return 0（何か積んだ・待った）/ 1（非待機で何も無かった）
 */
static errr fc_term_event(int wait)
{
    if (!fc_hooks_ready) {
        /*
         * フックが差さっていない（`--selftest`）。
         * ここで本当に待つと `msg_print(NULL)` の -more- で固まり、
         * 初期化の失敗報告すら届かなくなる。
         */
        if (wait) {
            (void)Term_keypress(ESCAPE);
        }
        return 0;
    }

    if (!wait) {
        return fc_drain_keys_into_term() ? 0 : 1;
    }

    for (;;) {
        /*
         * **待っている間も画を送る。** 死亡プロンプトや誕生画面のように
         * 「描いたあと入力を待つだけ」の場面は、ここで送らないと画面側に
         * 何も出ない。
         */
        fc_present();

        if (fc_drain_keys_into_term()) {
            return 0;
        }

        if (fc_hooks.shutdown_requested && fc_hooks.shutdown_requested()) {
            /* 強制保存して畳む。**戻らない**。 */
            fc_shutdown_and_quit();
        }

        if (fc_hooks.sleep_ms) {
            fc_hooks.sleep_ms(FC_IDLE_SLICE_MS);
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
static errr fc_term_xtra(int n, int v)
{
    switch (n) {
    case TERM_XTRA_EVENT:
        return fc_term_event(v);

    case TERM_XTRA_FLUSH:
        /*
         * 溜まった鍵を捨てる。**受信キュー側も捨てる**——ここで捨てないと、
         * コアが「捨てた」と思っている入力が次の待ちで蘇る。
         */
        if (fc_hooks_ready && fc_hooks.drop_keys) {
            fc_hooks.drop_keys();
        }
        return 0;

    case TERM_XTRA_FRESH:
        /* 束を装填してから frame を送る（FH-05。締めの順は fresh → present）。 */
        fc_fx_on_fresh();
        if (fc_term_ready && fc_term_body.scr) {
            fc_fx_cursor_x = (int)fc_term_body.scr->cx;
            fc_fx_cursor_y = (int)fc_term_body.scr->cy;
        }
        /* 画が確定した。ここが frame の主な出どころ。 */
        fc_present();
        return 0;

    case TERM_XTRA_DELAY:
        /* 弾道・爆発の署名ならここで拾う（FH-05。v=0 でも描かれている）。 */
        fc_fx_on_delay();
        /*
         * 演出の間。ヘッドレスでも**捨てない**——捨てると弾道や爆風が
         * 1 フレームに潰れ、画面側が動きを見せられない。ただし長すぎる待ちは
         * 応答を殺すので上限を付ける。
         */
        if (fc_hooks_ready && fc_hooks.sleep_ms && (v > 0)) {
            fc_hooks.sleep_ms((v > 200) ? 200 : v);
        }
        return 0;

    /*
     * 音（`TERM_XTRA_SOUND`）は**ここでは鳴らさない**。名前とマスをフレームへ
     * 載せ、鳴らすのは画面側である。だから
     * 幻想蛮怒の `gb_audio.c`（winmm を直に叩く実体）に当たるものは 1 つも要らない。
     * **Frox に `TERM_XTRA_MUSIC_*` は無い**ので曲の枝も無い（設計 §3 の「作らないもの」）。
     *
     * `SOUND_KILL` は**とどめの合図**としても数える（FH-05。スロット消滅との
     * 突き合わせ）。積むのとは別の用途なので、両方呼ぶ。
     */
    case TERM_XTRA_SOUND:
        if (v == SOUND_KILL) {
            fc_fx_note_kill_sound();
        }
        fc_sound_push(v);
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

int fc_term_install(int cols, int rows)
{
    term *t = &fc_term_body;

    if (fc_term_ready) {
        return FC_ERR_STATE;
    }

    /* note() が 23 行目に書く。24 行未満は成立しない。 */
    if ((cols < 80) || (rows < 24) || (cols > 255) || (rows > 255)) {
        return FC_ERR_ARG;
    }

    /* 第 4 引数は鍵の待ち行列の長さ（main-win.c の td->keys と同じ役目）。 */
    if (term_init(t, cols, rows, 1024)) {
        return FC_ERR_TERM;
    }

    /* 画を持たないのでカーソルは自前で描く扱いにしておく（描かないが）。 */
    t->soft_cursor = TRUE;
    t->attr_blank = TERM_WHITE;
    t->char_blank = ' ';

    /* 要るのはこれだけ。他は term_init() が hack を入れてある。 */
    t->xtra_hook = fc_term_xtra;
    /*
     * text は描く先が無くても**観測のため**に差す（FH-05。弾道・爆発の束ね）。
     * hack の text と同じく 0 を返すので、z-term の流れは 1 バイトも変わらない。
     */
    t->text_hook = fc_term_text;

    /*
     * `inkey()` は `angband_term[0]` を活性にし直す。
     * ここを埋めておかないとそこで NULL 参照になる。
     */
    angband_term[0] = t;
    Term_activate(t);

    fc_term_ready = 1;
    return FC_OK;
}

void fc_term_remove(void)
{
    if (!fc_term_ready) {
        return;
    }

    Term_activate(NULL);
    angband_term[0] = NULL;
    (void)term_nuke(&fc_term_body);
    fc_term_ready = 0;
}

int fc_term_size(int *cols, int *rows)
{
    if (!fc_term_ready) {
        return FC_ERR_STATE;
    }

    if (cols) {
        *cols = (int)fc_term_body.wid;
    }
    if (rows) {
        *rows = (int)fc_term_body.hgt;
    }
    return FC_OK;
}

int fc_term_row(int y, char *text, unsigned char *attr, int cap)
{
    const term_win *win;
    int n;
    int x;

    if (!fc_term_ready) {
        return FC_ERR_STATE;
    }
    if (!text || (cap <= 1) || (y < 0) || (y >= (int)fc_term_body.hgt)) {
        return FC_ERR_ARG;
    }

    /*
     * `scr` が「これから見せる画」、`old` が「もう見せた画」。
     * Term_fresh() は scr を old へ写す。**読むのは scr** で、
     * fresh 前でも最新が取れる。
     *
     * @note 設計 §3.1 / §12 の 2: **Term のセルを読む経路はここ 1 か所に集める。**
     * Sil-Q では境界が 3 か所のつもりが 4 か所あり、メッセージだけ化けた。
     * M1 で CP932 → UTF-8 の変換を入れるとき、直すのはここと
     * `fc_sub_term_row()`（P6）だけで済むようにしておく。
     */
    win = fc_term_body.scr;
    if (!win) {
        return FC_ERR_STATE;
    }

    n = (int)fc_term_body.wid;
    if (n > cap - 1) {
        n = cap - 1;
    }

    for (x = 0; x < n; x++) {
        text[x] = win->c[y][x];
        if (attr) {
            attr[x] = (unsigned char)win->a[y][x];
        }
    }
    text[n] = '\0';

    return n;
}

int fc_term_cursor(int *x, int *y, int *visible)
{
    const term_win *win;

    if (!fc_term_ready) {
        return FC_ERR_STATE;
    }

    win = fc_term_body.scr;
    if (!win) {
        return FC_ERR_STATE;
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
    return FC_OK;
}

void fc_term_clear(void)
{
    (void)Term_clear();
}

void fc_term_putstr(int x, int y, int attr, const char *text)
{
    if (!text) {
        return;
    }
    (void)Term_putstr(x, y, -1, (byte)attr, (cptr)text);
}

void fc_term_present(void)
{
    /*
     * `Term_fresh()` の締めが `TERM_XTRA_FRESH` を起こし、null term がフックの
     * `present` を呼ぶ。つまりこれ 1 つで frame が線に乗る。
     */
    (void)Term_fresh();
}

/*!
 * @brief 変化検出用の安い digest（`fc_shim.h` の説明）。
 *
 * @details ここに置いてあるのは **Term の画（`fc_term_body.scr`）が要る**ためで、
 * あれはこの TU の静的変数である。混ぜるものは「画面に出るものが変われば必ず動く」
 * 材料に限る:
 *
 * | 材料 | なぜ要るか |
 * |---|---|
 * | Term の文字と色 | コアが自分で描いた結果。メニューも地図も全部ここに出る |
 * | カーソル | 文字入力のキャレットだけが動く場面がある |
 * | `game_turn` | **Term の窓の外**で敵が動いた場合を拾う。視界はもっと広い |
 * | メッセージ本数 | 同じ画のまま行が増えることがある |
 * | @ の位置・HP・MP・階 | 保険（上の 3 つで足りるはずだが安い） |
 * | icky / generated / 死亡 | 画面の切り替わり |
 * | doc UI の旗 | **降りる瞬間は Term が 1 セルも変わらない**（Term_load で元の画へ
 *   戻った後、コマンド待ちで降ろすだけ）。混ぜないとミラーが開いたまま残る
 * |
 *
 * FNV-1a（64bit）。**衝突しても実害は「1 フレーム古い画が残る」だけ**で、
 * `game_turn` が混ざっているのでゲームが進んでいる間は必ず動く。
 *
 * @note 変愚の `turn` は Frox では **`game_turn`** である（`externs.h:178`）。
 *       同じ名前の `turn` は無い。
 * @note **`status_col_side` は混ぜない**（設計 §4.3 の註）。
 */
unsigned long long fc_change_digest(void)
{
    const unsigned long long prime = 1099511628211ULL;
    unsigned long long h = 14695981039346656037ULL;
    const term_win *win;
    int y;
    int x;

#define FC_MIX(BYTE) \
    do { \
        h ^= (unsigned long long)(unsigned char)(BYTE); \
        h *= prime; \
    } while (0)
#define FC_MIX_INT(V) \
    do { \
        unsigned long v_ = (unsigned long)(V); \
        FC_MIX(v_ & 0xFF); \
        FC_MIX((v_ >> 8) & 0xFF); \
        FC_MIX((v_ >> 16) & 0xFF); \
        FC_MIX((v_ >> 24) & 0xFF); \
    } while (0)

    if (fc_term_ready && (win = fc_term_body.scr) != NULL) {
        for (y = 0; y < (int)fc_term_body.hgt; y++) {
            for (x = 0; x < (int)fc_term_body.wid; x++) {
                FC_MIX(win->c[y][x]);
                FC_MIX(win->a[y][x]);
            }
        }
        FC_MIX_INT(win->cx);
        FC_MIX_INT(win->cy);
        FC_MIX(win->cu ? 1 : 0);
        FC_MIX(win->cv ? 1 : 0);
    }

    /*
     * **自由入力の旗を混ぜる**。
     * 旗が変わっても Term は 1 セルも動かないことがある——`askfor_aux()` へ入った
     * 瞬間がそれで、混ぜないとフレームが送られず**画面まで届かない**。
     * 届かなければ IME は出ないし、非 ASCII も送られてこない。
     */
    FC_MIX(fc_text_input_active() ? 1 : 0);
    FC_MIX_INT(game_turn);
    FC_MIX_INT(msg_count());
    FC_MIX(character_generated ? 1 : 0);
    FC_MIX(character_icky ? 1 : 0);
    FC_MIX(character_xtra ? 1 : 0);
    FC_MIX(character_dungeon ? 1 : 0);
    /* 降ろす判定も兼ねる（fc_doc_ui_active() は呼ばれた時に inkey_flag を見て降ろす）。 */
    FC_MIX(fc_doc_ui_active() ? 1 : 0);
    /*
     * 見せ場の累計（FH-05）。弾道は描いて即消すので、animation が終わった後の
     * Term は元の画と同じになりうる——混ぜないと、積んだ見せ場が次の別の変化まで
     * 線に乗らない。
     */
    FC_MIX_INT(fc_fx_total());
    /*
     * 音の累計も同じ理由で混ぜる。音は一度きりの出来事で、鳴った瞬間の Term が
     * 1 セルも動かない場面がある（扉を開ける音・買い物の音）。混ぜないとその 1 枚が
     * 線に乗らず、音が次の別の変化まで遅れる。
     */
    FC_MIX_INT(fc_sound_total());

    if (character_generated) {
        FC_MIX_INT(py);
        FC_MIX_INT(px);
        FC_MIX_INT(p_ptr->chp);
        FC_MIX_INT(p_ptr->csp);
        FC_MIX_INT(p_ptr->au);
        FC_MIX_INT(dun_level);
        FC_MIX(p_ptr->is_dead ? 1 : 0);
    }

#undef FC_MIX_INT
#undef FC_MIX

    return h;
}
