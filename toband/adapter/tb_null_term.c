/*!
 * @file tb_null_term.c
 * @brief 旧 z-term のヘッドレスドライバ（画を持たない Term を 1 枚立てる）。
 *
 * 作りは （幻想蛮怒）。
 *
 * ## なぜ要るのか
 * **Term が 1 枚も無いと `init_angband()` は落ちる。** あちらは進捗を
 * note() で画面に書く。note() の中身は（`toband/src/init2.c:2232`）
 *
 *     Term_erase(0, 23, 255); Term_putstr(20, 23, -1, TERM_WHITE, str); Term_fresh();
 *
 * で、`Term` が NULL なら即死、行数が 24 未満でも書けない。
 *
 * ## 実装する必要があるフックは xtra だけ
 * `term_init()` が curs / bigcurs / wipe / text / pict に「hack」フック
 * （何もせず 0 を返す関数）を自分で入れてくれる（`toband/src/z-term.c`）。
 * 一方 `Term_xtra()` は `if (!Term->xtra_hook) return (-1);` なので、
 * **xtra_hook が無いと Term_fresh() が失敗する**。だからここは xtra だけ書く。
 *
 * 画面の中身は z-term.c 自身が `Term->scr` に積んでくれる。ドライバが
 * 何も描かなくても、そこを読めばミラーは取れる（tb_term_row）。
 *
 * ## P2 でここが本物になった
 * P1 の `TERM_XTRA_EVENT` は「ESC を 1 個積んで返す」逃げ道だった。
 * P2 では `tb_host_hooks`（`tb_shim.h`）越しに C++ 側の受信キューへ繋ぐ。
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

#include "tb_shim.h"

#include <stdio.h>
#include <string.h>

/*! 立てた Term の実体。1 枚しか立てない。主 Term は作成画面の行27も含む80x28。 */
static term tb_term_body;
static int tb_term_ready = 0;

/*! ホスト（C++ 側）のフック。差さっていなければ P1 と同じ逃げ道に落ちる。 */
static tb_host_hooks tb_hooks;
static int tb_hooks_ready = 0;

/*! 待ちの空回り 1 周の長さ（ミリ秒）。変愚の `sdl_null_term.cpp:521` と同じ 10ms。 */
#define TB_IDLE_SLICE_MS 10

void tb_set_host_hooks(const tb_host_hooks *hooks)
{
    if (hooks) {
        tb_hooks = *hooks;
        tb_hooks_ready = 1;
    } else {
        memset(&tb_hooks, 0, sizeof(tb_hooks));
        tb_hooks_ready = 0;
    }
}

/*! 画を送る（`present`）。フックが無ければ何もしない。 */
static void tb_present(void)
{
    if (tb_hooks_ready && tb_hooks.present) {
        tb_hooks.present();
    }
}

/*!
 * @brief 誕生の最初の画面（性別の選択）が出ているか。
 * @details 原作 `birth.c` の `get_player_sex()` が `Term_putstr(QUESTION_COL, QUESTION_ROW, ...,
 * "《性別》の違いは種族とクラスの選択に影響を及ぼします。")`（英語は "Your 'sex' effects ..."）を描いて
 * キーを待つ。**行 7（`QUESTION_ROW`）の桁 2（`QUESTION_COL`）の字**で見分ける。字は CP932 のバイトで
 * 直に書く（ソースの文字コードに依らない）。性別を選ぶと次の画面（種族）では別の文が描かれるので偽になる。
 * （幻想蛮怒は最初が難易度の選択だったが、TOband は性別から始まる。）
 */
static int tb_birth_first_screen(void)
{
    static const char ja[] = "\x81\x73\x90\xab\x95\xca\x81\x74";
    static const char en[] = "Your 'sex' effects";
    char cells[256];

    if (character_generated) {
        return 0;
    }
    if (tb_term_row(7, cells, NULL, (int)sizeof(cells)) < 0) {
        return 0;
    }
    return (strncmp(cells + 2, ja, sizeof(ja) - 1) == 0) || (strncmp(cells + 2, en, sizeof(en) - 1) == 0);
}

/*!
 * @brief 受信キューにあるだけキーを Term へ積む。
 * @return 積んだ個数
 *
 * @details **末尾へ積む**（`Term_keypress`）。`Term_key_push` は先頭挿入なので、
 * 1 回に 2 個以上積むと消費順が逆転する（変愚で踏んだ穴。K-20）。
 */
static int tb_drain_keys_into_term(void)
{
    int pushed = 0;
    int key;

    if (!tb_hooks_ready || !tb_hooks.next_key) {
        return 0;
    }

    while ((key = tb_hooks.next_key()) > 0) {
        /* 誕生の最初の画面での取消は、原作が捨てて待ち続けるので、ここで拾ってタイトルへ戻る（修-61）。 */
        if ((key == ESCAPE) && tb_birth_first_screen()) {
            (void)tb_cancel_birth_to_title(); /* 戻らない。跳び先が無いときだけ素通し */
        }
        (void)Term_keypress(key);
        pushed++;
        /* 待ち行列の容量（term_init の第 4 引数）を超えて押し込まない。 */
        if (pushed >= 256) {
            break;
        }
    }

    return pushed;
}

/*! 終了を求められてから、命令待ちまで戻すために送った ESC の数（`tb_term_event`）。 */
static int tb_unwind_keys = 0;
#define TB_UNWIND_LIMIT 200

/*!
 * @brief `TERM_XTRA_EVENT` の本体。
 * @param wait 真なら「キーが来るまで待て」
 * @return 0（何か積んだ・待った）/ 1（非待機で何も無かった）
 */
static errr tb_term_event(int wait)
{
    if (!tb_hooks_ready) {
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
        return tb_drain_keys_into_term() ? 0 : 1;
    }

    for (;;) {
        /*
         * **待っている間も画を送る。** 死亡プロンプトや誕生画面のように
         * 「描いたあと入力を待つだけ」の場面は、ここで送らないと画面側に
         * 何も出ない（変愚の `sdl_null_term.cpp:504` と同じ理由）。
         */
        tb_present();

        if (tb_drain_keys_into_term()) {
            return 0;
        }

        if (tb_hooks.shutdown_requested && tb_hooks.shutdown_requested()) {
            /*
             * **コマンドの途中なら、先に命令待ちまで戻す**（2026-10-16）。
             * 原作の窓を閉じる処理（`main-win.c` の WM_CLOSE）は `can_save` が偽なら
             * 「今は終了できません」と断る。コマンドの途中（例: ダンジョンの入口で
             * 「入った」と出た直後、階を作る前）の状態を保存すると、読み込んだあと
             * `change_floor()` が古い `new_floor_id` で空の値を読んで落ちる。
             * 断る代わりに ESC を送って命令待ち（`can_save` が真）まで進め、そこで保存する。
             * ESC を TB_UNWIND_LIMIT 回送っても戻らないときだけ、その場で保存する（従来どおり）。
             */
            if (!can_save && character_generated && tb_unwind_keys < TB_UNWIND_LIMIT) {
                ++tb_unwind_keys;
                (void)Term_keypress(ESCAPE);
                return 0;
            }
            /* 強制保存して畳む。**戻らない**（設計 V4）。 */
            tb_shutdown_and_quit();
        }

        if (tb_hooks.sleep_ms) {
            tb_hooks.sleep_ms(TB_IDLE_SLICE_MS);
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
static errr tb_term_xtra(int n, int v)
{
    switch (n) {
    case TERM_XTRA_EVENT:
        return tb_term_event(v);

    case TERM_XTRA_FLUSH:
        /*
         * 溜まった鍵を捨てる。**受信キュー側も捨てる**——ここで捨てないと、
         * コアが「捨てた」と思っている入力が次の待ちで蘇る。
         * 変愚の `SdlNullTerm::on_flush` と同じ挙動（そちらは吸い上げて捨てる）。
         */
        if (tb_hooks_ready && tb_hooks.drop_keys) {
            tb_hooks.drop_keys();
        }
        return 0;

    case TERM_XTRA_FRESH:
        /* 画が確定した。ここが frame の主な出どころ。 */
        tb_present();
        return 0;

    case TERM_XTRA_DELAY:
        /*
         * 演出の間。ヘッドレスでも**捨てない**——捨てると弾道や爆風が
         * 1 フレームに潰れ、画面側が動きを見せられない。ただし長すぎる待ちは
         * 応答を殺すので上限を付ける。
         */
        if (tb_hooks_ready && tb_hooks.sleep_ms && (v > 0)) {
            tb_hooks.sleep_ms((v > 200) ? 200 : v);
        }
        return 0;

    case TERM_XTRA_CLEAR:
    case TERM_XTRA_SHAPE:
    case TERM_XTRA_FROSH:
    case TERM_XTRA_NOISE:
        return 0;

    /*
     * 音（M1 / S10・a）。**何を鳴らすかはコアが決めている**（`util.c` の
     * `sound()` と `play_music()`）ので、ここは受けて `tb_audio.c` へ渡すだけ。
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
         * （音の無い機・Android の空実装）**従来どおり `tb_audio.c` が鳴らす**。
         * 切り替えは自動で、設定の項目は増やさない。
         *
         * TOband には曲（`TERM_XTRA_MUSIC_*`）の仕組みが無いので、曲の分岐は持たない。
         */
        if (tb_sound_wanted()) {
            tb_sound_push(v);
            return 0;
        }
        return tb_audio_play_sound(v);
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

int tb_term_install(int cols, int rows)
{
    term *t = &tb_term_body;

    if (tb_term_ready) {
        return TB_ERR_STATE;
    }

    /* note() が 23 行目に書く。24 行未満は成立しない。 */
    if ((cols < 80) || (rows < 24) || (cols > 255) || (rows > 255)) {
        return TB_ERR_ARG;
    }

    /* 第 4 引数は鍵の待ち行列の長さ（main-win.c の td->keys と同じ役目）。 */
    if (term_init(t, cols, rows, 1024)) {
        return TB_ERR_TERM;
    }

    /* 画を持たないのでカーソルは自前で描く扱いにしておく（描かないが）。 */
    t->soft_cursor = TRUE;
    t->attr_blank = TERM_WHITE;
    t->char_blank = ' ';

    /* 要るのはこれだけ。他は term_init() が hack を入れてある。 */
    t->xtra_hook = tb_term_xtra;

    /*
     * `inkey()` は `angband_term[0]` を活性にし直す（util.c:2448）。
     * ここを埋めておかないとそこで NULL 参照になる。
     */
    angband_term[0] = t;
    Term_activate(t);

    tb_term_ready = 1;
    return TB_OK;
}

void tb_term_remove(void)
{
    if (!tb_term_ready) {
        return;
    }

    Term_activate(NULL);
    angband_term[0] = NULL;
    (void)term_nuke(&tb_term_body);
    tb_term_ready = 0;
}

int tb_term_size(int *cols, int *rows)
{
    if (!tb_term_ready) {
        return TB_ERR_STATE;
    }

    if (cols) {
        *cols = (int)tb_term_body.wid;
    }
    if (rows) {
        *rows = (int)tb_term_body.hgt;
    }
    return TB_OK;
}

int tb_term_resize_rows(int rows)
{
    term *previous;
    errr result;
    if (!tb_term_ready) return TB_ERR_STATE;
    if ((rows < 24) || (rows > 80)) return TB_ERR_ARG;
    if ((int)tb_term_body.hgt == rows) return TB_OK;
    previous = Term;
    Term_activate(&tb_term_body);
    result = Term_resize((int)tb_term_body.wid, rows);
    if (previous != &tb_term_body) Term_activate(previous);
    return result ? TB_ERR_TERM : TB_OK;
}

int tb_term_row(int y, char *text, unsigned char *attr, int cap)
{
    const term_win *win;
    int n;
    int x;

    if (!tb_term_ready) {
        return TB_ERR_STATE;
    }
    if (!text || (cap <= 1) || (y < 0) || (y >= (int)tb_term_body.hgt)) {
        return TB_ERR_ARG;
    }

    /*
     * `scr` が「これから見せる画」、`old` が「もう見せた画」。
     * Term_fresh() は scr を old へ写す。**読むのは scr** で、
     * fresh 前でも最新が取れる（V1 の確認）。
     */
    win = tb_term_body.scr;
    if (!win) {
        return TB_ERR_STATE;
    }

    n = (int)tb_term_body.wid;
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
 * @brief 変化検出用の安い digest（設計の P2 申し送り 6・`tb_shim.h` の説明）。
 *
 * @details ここに置いてあるのは **Term の画（`tb_term_body.scr`）が要る**ためで、
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
unsigned long long tb_change_digest(void)
{
    const unsigned long long prime = 1099511628211ULL;
    unsigned long long h = 14695981039346656037ULL;
    const term_win *win;
    int y;
    int x;

#define TB_MIX(BYTE) \
    do { \
        h ^= (unsigned long long)(unsigned char)(BYTE); \
        h *= prime; \
    } while (0)
#define TB_MIX_INT(V) \
    do { \
        unsigned long v_ = (unsigned long)(V); \
        TB_MIX(v_ & 0xFF); \
        TB_MIX((v_ >> 8) & 0xFF); \
        TB_MIX((v_ >> 16) & 0xFF); \
        TB_MIX((v_ >> 24) & 0xFF); \
    } while (0)

    if (tb_term_ready && (win = tb_term_body.scr) != NULL) {
        for (y = 0; y < (int)tb_term_body.hgt; y++) {
            for (x = 0; x < (int)tb_term_body.wid; x++) {
                TB_MIX(win->c[y][x]);
                TB_MIX(win->a[y][x]);
            }
        }
        TB_MIX_INT(win->cx);
        TB_MIX_INT(win->cy);
        TB_MIX(win->cu ? 1 : 0);
        TB_MIX(win->cv ? 1 : 0);
    }

    TB_MIX_INT(turn);
    TB_MIX_INT(message_num());
    TB_MIX(character_generated ? 1 : 0);
    TB_MIX(character_icky ? 1 : 0);
    TB_MIX(character_xtra ? 1 : 0);
    TB_MIX(character_dungeon ? 1 : 0);
    /*
     * **文字入力の待ちも混ぜる**（M1 / S5）。これを落とすと、`askfor_aux()` が
     * 画面を描いてから待ちに入っても**画は 1 画素も変わらない**ので門で止まり、
     * 画面側に `text_input_active` が届かない。届かないと画面は非 ASCII を送らない
     * ので、**日本語を打とうとした瞬間に手詰まりになる**（実測して踏んだ）。
     */
    TB_MIX(tb_text_input_active() ? 1 : 0);
    TB_MIX(tb_awaiting_command() ? 1 : 0);

    /*
     * **サブウィンドウの割り当て世代も混ぜる**（M1 / R1）。`=`→`w` を閉じた直後は
     * 主 Term が描き直されるので普通は門を越えるが、同じ画へ戻る場合に取り落とすと
     * 「変えたのに画面へ返らない」が残る。1 つ混ぜるだけで確実になる。
     */
    TB_MIX_INT(tb_window_flags_generation());

    /*
     * **戦闘の見せ場が溜まっていることも混ぜる**（M1 / R4）。被弾で HP が減れば
     * 主 Term は変わるが、見えていない所での命中や `PROJECT_HIDE` の飛道は
     * 1 画素も変えない。混ぜないと門で止まり、次に何かが動くまで演出が出ない。
     */
    TB_MIX_INT(tb_fx_pending_count());

    /*
     * **音の累計も混ぜる**。音は一度きりの出来事で、
     * 鳴った瞬間の Term が 1 セルも動かない場面がある（扉を開ける音・買い物の音）。
     * 混ぜないとその 1 枚が線に乗らず、音が次の別の変化まで遅れる。
     *
     * **累計であって溜まっている数ではない。**上の見せ場は数を混ぜているが、
     * こちらは汲んだ直後に 0 へ戻るので、数だと「積んで汲んだ」が同じ値に見える。
     */
    TB_MIX_INT(tb_sound_total());

    /*
     * **状態の旗の組も混ぜる**。`prt_status` が組み直した旗は
     * Term の最下段の字が変わるので普通は digest が動くが、字が溢れて短縮されたり、同じ字のまま
     * 別の状態へ替わる場面でも、アイコンは必ず追いつかせる。
     */
    TB_MIX_INT(tb_status_stamp());

    if (character_generated) {
        TB_MIX_INT(py);
        TB_MIX_INT(px);
        TB_MIX_INT(p_ptr->chp);
        TB_MIX_INT(p_ptr->csp);
        TB_MIX_INT(p_ptr->au_sum);
        TB_MIX_INT(dun_level);
        TB_MIX(p_ptr->is_dead ? 1 : 0);
    }

#undef TB_MIX_INT
#undef TB_MIX

    return h;
}

int tb_term_cursor(int *x, int *y, int *visible)
{
    const term_win *win;

    if (!tb_term_ready) {
        return TB_ERR_STATE;
    }

    win = tb_term_body.scr;
    if (!win) {
        return TB_ERR_STATE;
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
    return TB_OK;
}
