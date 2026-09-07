/*!
 * @file gb_null_term.c
 * @brief 旧 z-term のヘッドレスドライバ（画を持たない Term を 1 枚立てる）。
 *
 * 基準は / §3.2。
 *
 * ## なぜ要るのか
 * **Term が 1 枚も無いと `init_angband()` は落ちる。** あちらは進捗を
 * note() で画面に書く。note() の中身は（`gensoband/src/init2.c:2232`）
 *
 *     Term_erase(0, 23, 255); Term_putstr(20, 23, -1, TERM_WHITE, str); Term_fresh();
 *
 * で、`Term` が NULL なら即死、行数が 24 未満でも書けない。
 *
 * ## 実装する必要があるフックは xtra だけ
 * `term_init()` が curs / bigcurs / wipe / text / pict に「hack」フック
 * （何もせず 0 を返す関数）を自分で入れてくれる（`gensoband/src/z-term.c`）。
 * 一方 `Term_xtra()` は `if (!Term->xtra_hook) return (-1);` なので、
 * **xtra_hook が無いと Term_fresh() が失敗する**。だからここは xtra だけ書く。
 *
 * 画面の中身は z-term.c 自身が `Term->scr` に積んでくれる。ドライバが
 * 何も描かなくても、そこを読めばミラーは取れる（gb_term_row）。
 *
 * ## P2 でここが本物になった
 * P1 の `TERM_XTRA_EVENT` は「ESC を 1 個積んで返す」逃げ道だった。
 * P2 では `gb_host_hooks`（`gb_shim.h`）越しに C++ 側の受信キューへ繋ぐ。
 * 形は変愚の `presentation/term/sdl_null_term.cpp`（`on_event` / `on_flush` /
 * `on_fresh` / `on_delay`）をそのまま踏襲した——**待ちの中でも画を送り続ける**のが
 * 肝で、これが無いと「-more-」や誕生画面が画面側に出ないまま固まる。
 *
 * ## 待ちの契約（z-term.c:2505 を読むこと）
 * `Term_inkey(ch, TRUE, ...)` は
 *
 *     while (Term->key_head == Term->key_tail) Term_xtra(TERM_XTRA_EVENT, TRUE);
 *
 * である。**キューが空のまま返ると呼び直される**（無限ループにはならないが、
 * 返るたびに回るので中で少し寝ないと CPU を焼く）。だからここでは
 * 「キーが来るまで中で回る」形にしてある。
 */

#include "angband.h"

#include "gb_shim.h"

#include <stdio.h>
#include <string.h>

/*! 立てた Term の実体。1 枚しか立てない（設計 §3.2 の「主 Term は 80x27 固定」）。 */
static term gb_term_body;
static int gb_term_ready = 0;

/*! ホスト（C++ 側）のフック。差さっていなければ P1 と同じ逃げ道に落ちる。 */
static gb_host_hooks gb_hooks;
static int gb_hooks_ready = 0;

/*! 待ちの空回り 1 周の長さ（ミリ秒）。変愚の `sdl_null_term.cpp:521` と同じ 10ms。 */
#define GB_IDLE_SLICE_MS 10

void gb_set_host_hooks(const gb_host_hooks *hooks)
{
    if (hooks) {
        gb_hooks = *hooks;
        gb_hooks_ready = 1;
    } else {
        memset(&gb_hooks, 0, sizeof(gb_hooks));
        gb_hooks_ready = 0;
    }
}

/*! 画を送る（`present`）。フックが無ければ何もしない。 */
static void gb_present(void)
{
    if (gb_hooks_ready && gb_hooks.present) {
        gb_hooks.present();
    }
}

/*!
 * @brief 受信キューにあるだけキーを Term へ積む。
 * @return 積んだ個数
 *
 * @details **末尾へ積む**（`Term_keypress`）。`Term_key_push` は先頭挿入なので、
 * 1 回に 2 個以上積むと消費順が逆転する（変愚で踏んだ穴。K-20）。
 */
static int gb_drain_keys_into_term(void)
{
    int pushed = 0;
    int key;

    if (!gb_hooks_ready || !gb_hooks.next_key) {
        return 0;
    }

    while ((key = gb_hooks.next_key()) > 0) {
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
static errr gb_term_event(int wait)
{
    if (!gb_hooks_ready) {
        /*
         * フックが差さっていない（`--selftest`）。P1 と同じ逃げ道。
         * ここで本当に待つと `msg_print(NULL)` の -more- で固まり、
         * 初期化の失敗報告すら届かなくなる。
         */
        if (wait) {
            (void)Term_keypress(ESCAPE);
        }
        return 0;
    }

    if (!wait) {
        return gb_drain_keys_into_term() ? 0 : 1;
    }

    for (;;) {
        /*
         * **待っている間も画を送る。** 死亡プロンプトや誕生画面のように
         * 「描いたあと入力を待つだけ」の場面は、ここで送らないと画面側に
         * 何も出ない（変愚の `sdl_null_term.cpp:504` と同じ理由）。
         */
        gb_present();

        if (gb_drain_keys_into_term()) {
            return 0;
        }

        if (gb_hooks.shutdown_requested && gb_hooks.shutdown_requested()) {
            /* 強制保存して畳む。**戻らない**（設計 V4）。 */
            gb_shutdown_and_quit();
        }

        if (gb_hooks.sleep_ms) {
            gb_hooks.sleep_ms(GB_IDLE_SLICE_MS);
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
static errr gb_term_xtra(int n, int v)
{
    switch (n) {
    case TERM_XTRA_EVENT:
        return gb_term_event(v);

    case TERM_XTRA_FLUSH:
        /*
         * 溜まった鍵を捨てる。**受信キュー側も捨てる**——ここで捨てないと、
         * コアが「捨てた」と思っている入力が次の待ちで蘇る。
         * 変愚の `SdlNullTerm::on_flush` と同じ挙動（そちらは吸い上げて捨てる）。
         */
        if (gb_hooks_ready && gb_hooks.drop_keys) {
            gb_hooks.drop_keys();
        }
        return 0;

    case TERM_XTRA_FRESH:
        /* 画が確定した。ここが frame の主な出どころ。 */
        gb_present();
        return 0;

    case TERM_XTRA_DELAY:
        /*
         * 演出の間。ヘッドレスでも**捨てない**——捨てると弾道や爆風が
         * 1 フレームに潰れ、画面側が動きを見せられない。ただし長すぎる待ちは
         * 応答を殺すので上限を付ける。
         */
        if (gb_hooks_ready && gb_hooks.sleep_ms && (v > 0)) {
            gb_hooks.sleep_ms((v > 200) ? 200 : v);
        }
        return 0;

    case TERM_XTRA_CLEAR:
    case TERM_XTRA_SHAPE:
    case TERM_XTRA_FROSH:
    case TERM_XTRA_NOISE:
        return 0;

    /*
     * 音（M1 / S10・a）。**何を鳴らすかはコアが決めている**（`util.c` の
     * `sound()` と `play_music()`）ので、ここは受けて `gb_audio.c` へ渡すだけ。
     * 素材や設定が無ければ向こうが黙って 1 を返す——**進行には影響しない**。
     *
     * @note **この束の手前に `return 0;` を置くこと。**ここは元々
     * 「何もしない案件」を全部ひとまとめにした fallthrough の帯で、
     * その途中へ `case` を挿したせいで `TERM_XTRA_FROSH`（行ごとの再描画。
     * `v` は行番号）が音として鳴った——画面を 1 枚描くたびに 27 個の音が出た。
     * 実測で気づいた（起動直後に効果音の名前が 1 から順に並ぶ）。
     */
    case TERM_XTRA_SOUND:
        /*
         * **鳴らす担当は 2 通りある**。
         * 画面側の装置が開けていれば、ここでは鳴らさず名前とマスを書き留める
         * ——マスで定位できるのは画面側だけだからである。開いていなければ
         * （音の無い機・Android の空実装）**従来どおり `gb_audio.c` が鳴らす**。
         * 切り替えは自動で、設定の項目は増やさない。
         *
         * 曲（下の `TERM_XTRA_MUSIC_*`）はこの分岐に入らない——画面側に
         * 曲を受ける器が無いので、今までどおりコアが鳴らす。
         */
        if (gb_sound_wanted()) {
            gb_sound_push(v);
            return 0;
        }
        return gb_audio_play_sound(v);
    case TERM_XTRA_MUSIC_BASIC:
    case TERM_XTRA_MUSIC_DUNGEON:
    case TERM_XTRA_MUSIC_QUEST:
    case TERM_XTRA_MUSIC_TOWN:
    case TERM_XTRA_MUSIC_MON_L:
    case TERM_XTRA_MUSIC_MON_M:
    case TERM_XTRA_MUSIC_MON_H:
        return gb_audio_play_music(n, v);
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

int gb_term_install(int cols, int rows)
{
    term *t = &gb_term_body;

    if (gb_term_ready) {
        return GB_ERR_STATE;
    }

    /* note() が 23 行目に書く。24 行未満は成立しない。 */
    if ((cols < 80) || (rows < 24) || (cols > 255) || (rows > 255)) {
        return GB_ERR_ARG;
    }

    /* 第 4 引数は鍵の待ち行列の長さ（main-win.c の td->keys と同じ役目）。 */
    if (term_init(t, cols, rows, 1024)) {
        return GB_ERR_TERM;
    }

    /* 画を持たないのでカーソルは自前で描く扱いにしておく（描かないが）。 */
    t->soft_cursor = TRUE;
    t->attr_blank = TERM_WHITE;
    t->char_blank = ' ';

    /* 要るのはこれだけ。他は term_init() が hack を入れてある。 */
    t->xtra_hook = gb_term_xtra;

    /*
     * `inkey()` は `angband_term[0]` を活性にし直す（util.c:2448）。
     * ここを埋めておかないとそこで NULL 参照になる。
     */
    angband_term[0] = t;
    Term_activate(t);

    gb_term_ready = 1;
    return GB_OK;
}

void gb_term_remove(void)
{
    if (!gb_term_ready) {
        return;
    }

    Term_activate(NULL);
    angband_term[0] = NULL;
    (void)term_nuke(&gb_term_body);
    gb_term_ready = 0;
}

int gb_term_size(int *cols, int *rows)
{
    if (!gb_term_ready) {
        return GB_ERR_STATE;
    }

    if (cols) {
        *cols = (int)gb_term_body.wid;
    }
    if (rows) {
        *rows = (int)gb_term_body.hgt;
    }
    return GB_OK;
}

int gb_term_row(int y, char *text, unsigned char *attr, int cap)
{
    const term_win *win;
    int n;
    int x;

    if (!gb_term_ready) {
        return GB_ERR_STATE;
    }
    if (!text || (cap <= 1) || (y < 0) || (y >= (int)gb_term_body.hgt)) {
        return GB_ERR_ARG;
    }

    /*
     * `scr` が「これから見せる画」、`old` が「もう見せた画」。
     * Term_fresh() は scr を old へ写す。**読むのは scr** で、
     * fresh 前でも最新が取れる（V1 の確認）。
     */
    win = gb_term_body.scr;
    if (!win) {
        return GB_ERR_STATE;
    }

    n = (int)gb_term_body.wid;
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

/*!
 * @brief 変化検出用の安い digest（設計の P2 申し送り 6・`gb_shim.h` の説明）。
 *
 * @details ここに置いてあるのは **Term の画（`gb_term_body.scr`）が要る**ためで、
 * あれはこの TU の静的変数である。混ぜるものは「画面に出るものが変われば必ず動く」
 * 材料に限る:
 *
 * | 材料 | なぜ要るか |
 * |---|---|
 * | Term の文字と色 | コアが自分で描いた結果。メニューも地図も全部ここに出る |
 * | カーソル | 文字入力のキャレットだけが動く場面がある |
 * | `turn` | **Term の窓（66x22）の外**で敵が動いた場合を拾う。視界はもっと広い |
 * | メッセージ本数 | 同じ画のまま行が増えることがある |
 * | @ の位置・HP・MP・階 | 保険（上の 3 つで足りるはずだが安い） |
 * | icky / generated / 死亡 | `menu_open` / `pre_game_menu` の切り替わり |
 *
 * FNV-1a（64bit）。**衝突しても実害は「1 フレーム古い画が残る」だけ**で、`turn` が
 * 混ざっているのでゲームが進んでいる間は必ず動く。
 */
unsigned long long gb_change_digest(void)
{
    const unsigned long long prime = 1099511628211ULL;
    unsigned long long h = 14695981039346656037ULL;
    const term_win *win;
    int y;
    int x;

#define GB_MIX(BYTE) \
    do { \
        h ^= (unsigned long long)(unsigned char)(BYTE); \
        h *= prime; \
    } while (0)
#define GB_MIX_INT(V) \
    do { \
        unsigned long v_ = (unsigned long)(V); \
        GB_MIX(v_ & 0xFF); \
        GB_MIX((v_ >> 8) & 0xFF); \
        GB_MIX((v_ >> 16) & 0xFF); \
        GB_MIX((v_ >> 24) & 0xFF); \
    } while (0)

    if (gb_term_ready && (win = gb_term_body.scr) != NULL) {
        for (y = 0; y < (int)gb_term_body.hgt; y++) {
            for (x = 0; x < (int)gb_term_body.wid; x++) {
                GB_MIX(win->c[y][x]);
                GB_MIX(win->a[y][x]);
            }
        }
        GB_MIX_INT(win->cx);
        GB_MIX_INT(win->cy);
        GB_MIX(win->cu ? 1 : 0);
        GB_MIX(win->cv ? 1 : 0);
    }

    GB_MIX_INT(turn);
    GB_MIX_INT(message_num());
    GB_MIX(character_generated ? 1 : 0);
    GB_MIX(character_icky ? 1 : 0);
    GB_MIX(character_xtra ? 1 : 0);
    GB_MIX(character_dungeon ? 1 : 0);
    /*
     * **文字入力の待ちも混ぜる**（M1 / S5）。これを落とすと、`askfor_aux()` が
     * 画面を描いてから待ちに入っても**画は 1 画素も変わらない**ので門で止まり、
     * 画面側に `text_input_active` が届かない。届かないと画面は非 ASCII を送らない
     * ので、**日本語を打とうとした瞬間に手詰まりになる**（実測して踏んだ）。
     */
    GB_MIX(gb_text_input_active() ? 1 : 0);

    /*
     * **サブウィンドウの割り当て世代も混ぜる**（M1 / R1）。`=`→`w` を閉じた直後は
     * 主 Term が描き直されるので普通は門を越えるが、同じ画へ戻る場合に取り落とすと
     * 「変えたのに画面へ返らない」が残る。1 つ混ぜるだけで確実になる。
     */
    GB_MIX_INT(gb_window_flags_generation());

    /*
     * **戦闘の見せ場が溜まっていることも混ぜる**（M1 / R4）。被弾で HP が減れば
     * 主 Term は変わるが、見えていない所での命中や `PROJECT_HIDE` の飛道は
     * 1 画素も変えない。混ぜないと門で止まり、次に何かが動くまで演出が出ない。
     */
    GB_MIX_INT(gb_fx_pending_count());

    /*
     * **音の累計も混ぜる**。音は一度きりの出来事で、
     * 鳴った瞬間の Term が 1 セルも動かない場面がある（扉を開ける音・買い物の音）。
     * 混ぜないとその 1 枚が線に乗らず、音が次の別の変化まで遅れる。
     *
     * **累計であって溜まっている数ではない。**上の見せ場は数を混ぜているが、
     * こちらは汲んだ直後に 0 へ戻るので、数だと「積んで汲んだ」が同じ値に見える。
     */
    GB_MIX_INT(gb_sound_total());

    if (character_generated) {
        GB_MIX_INT(py);
        GB_MIX_INT(px);
        GB_MIX_INT(p_ptr->chp);
        GB_MIX_INT(p_ptr->csp);
        GB_MIX_INT(p_ptr->au);
        GB_MIX_INT(dun_level);
        GB_MIX(p_ptr->is_dead ? 1 : 0);
    }

#undef GB_MIX_INT
#undef GB_MIX

    return h;
}

int gb_term_cursor(int *x, int *y, int *visible)
{
    const term_win *win;

    if (!gb_term_ready) {
        return GB_ERR_STATE;
    }

    win = gb_term_body.scr;
    if (!win) {
        return GB_ERR_STATE;
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
    return GB_OK;
}
