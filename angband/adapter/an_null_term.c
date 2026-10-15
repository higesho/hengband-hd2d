/*!
 * @file an_null_term.c
 * @brief Angband 4.2 のヘッドレス Term 駆動。手本は `silq/adapter/sq_null_term.c`。
 *
 * `main-test.c`（原作の疑似 UI）が
 * どのフックを埋めればよいかの手本になっている——`xtra_hook` だけ埋めれば、
 * 残り（curs / wipe / text / pict）は `term_init()` が no-op のフックを自分で入れる。
 *
 * ## Sil-Q との違い
 * Angband の `term_win` は `wchar_t **c` / `int **a`（`ui-term.h:39`）。
 * Sil-Q は `char **c` / 8bit の属性だったので、ここでは UTF-8 への変換が要る。
 * **Windows の `wchar_t` は 16 bit（UTF-16）** なので、非 ASCII な名前
 * （`Sméagol` `Grishnákh` ほか。検討 §1）は 1 wchar_t = 1 符号位置とは限らない。
 * `an_term_row()` はサロゲート対（上位 0xD800-0xDBFF・下位 0xDC00-0xDFFF）を
 * 1 組にまとめてから UTF-8 へ変換する（S1 の見直しで訂正。以前の註は誤り）。
 * 全角 2 桁の扱いは M1 の課題。
 */

#include "angband.h"
#include "cave.h" /* AH-57: square_in_bounds_fully() */
#include "cmd-core.h" /* AH-57: cmd_get_nrepeats() */
#include "game-event.h" /* AH-36: EVENT_REFRESH の受け手を足すため。 */
#include "game-world.h"
#include "player-timed.h"
#include "player-util.h" /* AH-57: player_is_resting() */
#include "ui-event.h" /* AH-57: KC_MOD_* / ESCAPE */
#include "ui-input.h"
#include "ui-output.h"
#include "ui-term.h"

#include "an_map_shim.h"
#include "an_shim.h"
#include "an_wide.h" /* AN_WIDE_PAD */
#include "an_lang_c.h"

#include <stdio.h>
#include <string.h>

/*! 立てる Term は 1 枚だけ（設計 §6.1 の 80x27 固定）。 */
static term an_term_body;
static int an_term_ready = 0;

static an_host_hooks an_hooks;
static int an_hooks_ready = 0;

/*! AH-59: 持ち物の窓の「全部落とす」の予約を、命令待ちで古くなったとき捨てる（実体は `an_main.cpp`）。 */
extern void an_drop_all_idle(void);

#define AN_IDLE_SLICE_MS 10

/*!
 * AH-36: `TERM_XTRA_FRESH` が来るたびに
 * `an_present()`（`build_frame()` を含む）を呼んでいた。原作の `update_maps()`
 * （`ui-display.c`）は明るい未知の部屋へ入ると、部屋のマス 1 つずつに対して
 * 個別に `Term_fresh()` を呼ぶ——本家の他コア（変愚・Sil-Q・Frox の
 * `lite_spot()`）はこれをしないので、Angband だけの負担だった（実測で
 * 1 回の入室あたり約 76 枚・9.4MB の JSON。既知の部屋の再入室でも
 * フレームの作り直しに最大 360ms）。
 *
 * ## 直しの形——「区切りの最初の 1 回だけ即送り、続きは次の区切りへまとめる」
 * 最初に試した「`TERM_XTRA_FRESH` は一切送らず、次の区切りへ全部預ける」という
 * 単純な形は、**見直しで実機テストに落として分かった 2 つの退行**を持っていた:
 *
 * 1. **AH-04（持ち物メニュー）が固まる。** 新 UI の一覧（`NativeInventoryMenu`）は
 *    元の Term を一切書き換えず `g_native_menu_dirty` だけを立てる。これを拾うのは
 *    `TERM_XTRA_EVENT` の「待つ」ループが 10ms ごとに回している `an_present()`
 *    だけなので、**この無条件呼び出しは削ってはいけない**（削ると `ui_action` への
 *    返事が永久に届かない）。
 * 2. **`targeting_smoke.py` の `confirm-direction-only` が落ちる。** 点選択
 *    （T1）でカーソルを動かすと、`an_target_refresh()`（`an_targeting.c`）が
 *    `Term_fresh()` を 2 回続けて呼ぶ——実測すると 1 回目は Term の見た目が一瞬
 *    変わり（`an_change_digest()` が変わる）、2 回目でまた元の値へ戻る（`host_present()`
 *    の安いしきい値の入力である digest だけを見ると「結局何も変わっていない」ように
 *    映る）。的の状態そのもの（`selected`/`can_confirm` 等）はどちらの
 *    `Term_fresh()` より前に `bridge.publish()` で更新済みなので、**この 2 回の
 *    どちらか一方だけでも実際に送れば正しい中身が乗る**——ところが「次の区切りまで
 *    一切送らない」形だと、この 2 回とも遅延され、待ちループに入った時点では
 *    digest が「動く前の値」と一致してしまっていて（後述の理由で `host_present()`
 *    はこれを「変化なし」と判じて丸ごと省く）、`can_confirm` が更新された
 *    フレームが 1 枚も外へ出ない。
 *
 * 直しは、`TERM_XTRA_FRESH` を「まとまり（区切りから区切りまで）の**最初の
 * 1 回**は直す前と同じくその場で送り、2 回目以降だけを次の区切りへ溜める」形に
 * した——`an_fresh_burst_open`（このまとまりで既に 1 回送ったか）と
 * `an_present_pending`（溜まっている分が残っているか）の 2 つの旗で判じる。
 * 区切り（`an_checkpoint_flush()`）は次の 3 つ:
 *   - `TERM_XTRA_DELAY` の直前（ボルト・爆発・矢の 1 コマごとの見た目はそのまま保つ）
 *   - `TERM_XTRA_EVENT`（待たない確認）
 *   - 新設の `EVENT_REFRESH` 受け手（`game-world.c` の `process_player()` が
 *     毎ターン発する。走る・休む・繰り返しの最中もこれで 1 ターンに 1 枚は出る）
 * `TERM_XTRA_EVENT` の「待つ」ループだけは直す前と同じ**無条件**の `an_present()`
 * を回し続ける（上の 1. の理由。ここは区切りの旗もその場でリセットする）。
 *
 * この形なら、明るい未知の部屋（`update_maps()` が同じ区切りの中で `Term_fresh()`
 * を何十回も連打する）は「1 回目（最初のマス）だけ即送り、残りは次の区切りで
 * まとめて 1 回」——実測で 76 枚が 2 枚まで落ちる。かつ、単発またはごく少数回
 * （的のカーソル移動・アニメーションの 1 コマ）の `Term_fresh()` は、直す前と
 * 同じく**最初の 1 回が必ずその場で送られる**ので、上の 2. のような「一瞬変わって
 * すぐ戻る」digest の揺れも取りこぼさない。
 */
static int an_fresh_burst_open = 0; /*!< このまとまりで既に 1 回 an_present() したか。 */
static int an_present_pending = 0; /*!< 2 回目以降の TERM_XTRA_FRESH が溜まっているか。 */

void an_set_host_hooks(const an_host_hooks *hooks)
{
    if (hooks) {
        an_hooks = *hooks;
        an_hooks_ready = 1;
    } else {
        memset(&an_hooks, 0, sizeof(an_hooks));
        an_hooks_ready = 0;
    }
}

static void an_present(void)
{
    if (an_hooks_ready && an_hooks.present) {
        an_hooks.present();
    }
}

/*!
 * @brief 区切り（AH-36）。溜まっていれば 1 回だけ実際に送り、まとまりを閉じる
 * （次の `TERM_XTRA_FRESH` はまた「まとまりの最初」として即送りへ戻る）。
 */
static void an_checkpoint_flush(void)
{
    if (an_present_pending) {
        an_present_pending = 0;
        an_present();
    }
    an_fresh_burst_open = 0;
}

/*!
 * @brief `EVENT_REFRESH` の受け手（AH-36）。1 ターンに 1 枚は必ず外へ出す。
 * @details `an_term_install()` の中、`ui_enter_world()`（`ui-display.c`）より
 * 先に登録する——`event_add_handler()` は先頭挿入なので、後から登録された側が
 * 先に呼ばれる。ここを先に登録しておけば、コア自身の `refresh()` ハンドラ
 * （`Term_fresh()` を呼ぶ）が後から挿し込まれても、いつも「コアの `refresh()` が
 * 先・このハンドラが後」の順で呼ばれる。
 */
static void an_on_event_refresh(game_event_type type, game_event_data *data, void *user)
{
    (void)type;
    (void)data;
    (void)user;
    an_checkpoint_flush();
}

/*!
 * @name 地図のクリック（AH-57）
 * @details 画面が送る `map_click`（ダンジョンのマスの座標）を、**原作のマウスの道そのまま**へ入れる。
 * 原作の `main-win.c` などは、押された位置を `Term_mousepress(x, y, button | (mods << 4))` で Term の
 * 待ち行列へ積み、`textui_get_command()` が `EVT_MOUSE` として受け取って `textui_process_click()`
 * （`ui-context.c`）へ渡す——左クリックの歩行・パスファインド・走る・変える・見る、右クリックの
 * 文脈メニュー、自分のマスのクリックはすべてそこにある。**そこは 1 行も変えず、同じ事象を積むだけ**にする。
 * @{
 */

/*! 見つけた状態。`an_map_click_state()` の戻り値。 */
#define AN_CLICK_IGNORE 0 /*!< 命令待ちでも走り・休憩・繰り返しの最中でもない（メニュー・問い・-more- など） */
#define AN_CLICK_COMMAND 1 /*!< 命令待ち。マウスの事象として積む */
#define AN_CLICK_INTERRUPT 2 /*!< 走り・休憩・繰り返しの最中。ほかの入力と同じく、それを止める */

/*!
 * @brief いまクリックを受けてよい状態か。
 * @details **判断は「その場」でする**（受信スレッドが積んだ時点ではなく、ゲームスレッドが
 * 入力を取りに来たとき）。命令待ちは `inkey_flag`（`textui_get_command()` が立てる。
 * `an_awaiting_command()` と同じ）で、メニュー（`screen_save_depth`）・問い・`-more-`・目標選択の
 * 最中は含まない——それらは画面側が自分の窓で受けるので、地図のクリックで原作の別の入力処理
 * （メニューの行・`anykey()`）が動かないようにする。走り・休憩・繰り返しの最中は、原作の
 * `check_for_player_interrupt()`（`ui-game.c`）が **どんな入力でも**止める作りなので、
 * その確認の読み取り（`inkey_scan == SCAN_INSTANT`）のときだけ受ける。
 */
static int an_map_click_state(void)
{
    if (!player || !cave || !character_generated || !Term) {
        return AN_CLICK_IGNORE;
    }
    if (screen_save_depth > 0) {
        return AN_CLICK_IGNORE;
    }
    if (inkey_flag) {
        return AN_CLICK_COMMAND;
    }
    if ((inkey_scan == SCAN_INSTANT)
        && (player->upkeep->running || (cmd_get_nrepeats() > 0) || player_is_resting(player))) {
        return AN_CLICK_INTERRUPT;
    }
    return AN_CLICK_IGNORE;
}

/*!
 * @brief `AN_MAP_CLICK_PACK()` の整数 1 個を、原作の入力へ変える。
 * @return 1 = マウスの事象として積んだ／2 = 走り・休憩・繰り返しを止める入力（ESC）として積んだ／
 *   0 = 捨てた（理由を stderr へ 1 行出す）
 * @details マスの座標を Term の座標へ直す式は、原作の `KEY_GRID_X` / `KEY_GRID_Y`（`ui-target.h`）の
 * 逆——`grid = (term - COL_MAP) / tile_width + Term->offset_x`。原作の事象の座標は 8 ビットで、
 * 画面に映っている区画（`Term->offset_x/y` から `SCREEN_WID` × `SCREEN_HGT` マス）の外は表せない。
 * 新 UI の絵は区画ではなくプレイヤーを中心にした範囲を映すので、**区画の外のマスもクリックされる**。
 * そのときは区画を（`modify_panel()` で）そのマスが入る最小の量だけ動かす——プレイヤーが歩けば
 * 原作の `verify_panel()` がまた整える。区画は絵に映らない（新 UI の地図はマスの座標で描く）。
 */
static int an_map_click_apply(int packed)
{
    const int grid_x = packed & 0xFF;
    const int grid_y = (packed >> 8) & 0xFF;
    const int button = (packed >> 16) & 0x3;
    const int wire_mods = (packed >> 18) & 0x7;
    int ui_mods = 0;
    int state;
    int wx;
    int wy;
    int col;
    int row;
    char label[32];

    if (wire_mods & AN_MAP_CLICK_SHIFT) {
        ui_mods |= KC_MOD_SHIFT;
    }
    if (wire_mods & AN_MAP_CLICK_CTRL) {
        ui_mods |= KC_MOD_CONTROL;
    }
    if (wire_mods & AN_MAP_CLICK_ALT) {
        ui_mods |= KC_MOD_ALT;
    }
    snprintf(label, sizeof(label), "%s%s%s%s", (button == 2) ? "right" : "left", (wire_mods & AN_MAP_CLICK_SHIFT) ? "+shift" : "",
        (wire_mods & AN_MAP_CLICK_CTRL) ? "+ctrl" : "", (wire_mods & AN_MAP_CLICK_ALT) ? "+alt" : "");

    if ((button != 1) && (button != 2)) {
        fprintf(stderr, "[angband] map_click ignored: unknown button %d\n", button);
        return 0;
    }

    state = an_map_click_state();
    if (state == AN_CLICK_IGNORE) {
        fprintf(stderr, "[angband] map_click (%d,%d) %s ignored: not at the command prompt (menu, prompt or -more-)\n", grid_x,
            grid_y, label);
        return 0;
    }
    if (state == AN_CLICK_INTERRUPT) {
        /*
         * 原作は入力があれば何でも止める（`check_for_player_interrupt()`）。マウスの事象のまま積むと、
         * 止める前に走りが終わっていた場合に古いクリックが次の命令待ちで歩行になってしまうので、
         * ESC（命令待ちでは何もしないキー）で同じ止め方をする。
         */
        (void)Term_keypress((keycode_t)ESCAPE, 0);
        fprintf(stderr, "[angband] map_click (%d,%d) %s: interrupts the running/resting/repeating command\n", grid_x, grid_y,
            label);
        return 2;
    }

    if (!square_in_bounds_fully(cave, loc(grid_x, grid_y))) {
        fprintf(stderr, "[angband] map_click (%d,%d) %s ignored: outside the level\n", grid_x, grid_y, label);
        return 0;
    }

    wx = Term->offset_x;
    wy = Term->offset_y;
    if (grid_x < wx) {
        wx = grid_x;
    } else if (grid_x >= wx + SCREEN_WID) {
        wx = grid_x - SCREEN_WID + 1;
    }
    if (grid_y < wy) {
        wy = grid_y;
    } else if (grid_y >= wy + SCREEN_HGT) {
        wy = grid_y - SCREEN_HGT + 1;
    }
    if ((wx != Term->offset_x) || (wy != Term->offset_y)) {
        (void)modify_panel(Term, wy, wx);
    }
    col = grid_x - Term->offset_x;
    row = grid_y - Term->offset_y;
    if ((col < 0) || (col >= SCREEN_WID) || (row < 0) || (row >= SCREEN_HGT)) {
        fprintf(stderr, "[angband] map_click (%d,%d) %s ignored: the grid is not on the panel (offset %d,%d)\n", grid_x, grid_y,
            label, Term->offset_x, Term->offset_y);
        return 0;
    }
    col = COL_MAP + (col * tile_width);
    row = ROW_MAP + (row * tile_height);
    fprintf(stderr, "[angband] map_click (%d,%d) %s: queued as a mouse event (term %d,%d panel %d,%d)\n", grid_x, grid_y, label,
        col, row, Term->offset_x, Term->offset_y);
    if ((button == 2) && (ui_mods == 0)) {
        an_menu_expect_click_context(); /* AH-58: 修飾なしの右クリックは文脈メニューを開く。次に始まるメニューがそれ */
    } else {
        an_menu_forget_click_context_expectation();
    }
    (void)Term_mousepress(col, row, (char)((ui_mods << 4) | button));
    return 1;
}

/*! @} */

/*!
 * AH-57: 読み出したがまだ評価していないクリック（0 = 無い）。クリックは**原作が入力待ちで
 * Term の待ち行列が空になったときの、その回の先頭**でだけ評価する（下の `an_drain_keys_into_term()`）。
 */
static int an_pending_click = 0;

/*!
 * @brief 受信キューにあるだけキーを Term へ積む。
 * @details `Term_keypress()`（末尾へ積む）を使う。Sil-Q と同じ注意——`Term_key_push`
 * （先頭挿入）ではなく、1 回に複数積んでも消費順が保たれる方を使う。
 *
 * AH-57: 地図のクリックは、キーと同じ列の中に `AN_KEY_MAP_CLICK` つきの整数で混ざっている。
 * **「いま命令待ちか」の判断は、その回の先頭の 1 個でしか正しくない**（`Term_inkey()` は待ち行列が
 * 空のときだけここへ来る。先に積んだキーが次の画面を開くなら、後ろのクリックの判断は古くなる）ので、
 * ①先に積んだ入力がある回のクリックは次の回へ持ち越し（`an_pending_click`）、
 * ②クリックを積んだ後は続きを読まない（続きは、そのクリックを原作が処理し終えた次の入力待ちで読む）。
 */
static int an_drain_keys_into_term(void)
{
    int pushed = 0;
    int key;

    if (!an_hooks_ready || !an_hooks.next_key) {
        return 0;
    }

    for (;;) {
        if (an_pending_click) {
            if (pushed) {
                break;
            }
            key = an_pending_click;
            an_pending_click = 0;
        } else {
            key = an_hooks.next_key();
            if (key <= 0) {
                break;
            }
        }
        if (key & AN_KEY_MAP_CLICK) {
            if (pushed) {
                an_pending_click = key;
                break;
            }
            if (an_map_click_apply(key)) {
                pushed++;
                break;
            }
            continue;
        }
        an_menu_forget_click_context_expectation(); /* AH-58: 別の入力を読んだ。メニューが開かなかった右クリックの期待は捨てる */
        if (((key == 27) || (key == ESCAPE)) && (pushed == 0)) { /* 画面からは生の 27 が来る（`Term_keypress()` が ESCAPE に直す） */
            /* 修-61: 誕生の最初の画面の取消はセーブ選択へ戻る（最初の画面でなければ戻ってくる）。 */
            an_birth_cancel_if_first_screen();
        }
        (void)Term_keypress((keycode_t)key, 0);
        pushed++;
        if (pushed >= 256) {
            break;
        }
    }

    return pushed;
}

static errr an_term_event(int wait)
{
    if (!an_hooks_ready) {
        /* `--selftest` はフックを差さない。待つと固まるので ESC を積むだけ。 */
        if (wait) {
            (void)Term_keypress((keycode_t)ESCAPE, 0);
        }
        return 0;
    }

    if (!wait) {
        an_checkpoint_flush(); /* AH-36: 待たない確認でも区切りとして扱う。 */
        return an_drain_keys_into_term() ? 0 : 1;
    }

    for (;;) {
        /*
         * AH-36: ここは直す前と同じ無条件呼び出しのまま——ファイル冒頭の註記の
         * 1.（AH-04 の持ち物メニューが Term を一切触らない件）のため。
         * 溜まっている分もこれで一緒に出るので、旗を両方下ろしておく。
         */
        an_present();
        an_present_pending = 0;
        an_fresh_burst_open = 0;

        if (an_drain_keys_into_term()) {
            return 0;
        }

        /*
         * AH-59: 命令待ちで入力を待つ回に何も読まなかった（Term の待ち行列も空）。持ち物の窓の「全部落とす」
         * の予約（`d` と一緒に立つ）が残っていれば、数量の問いに至らなかった古い予約なので捨てる。
         * 捨てる判断は `an_main.cpp` が待ち行列と同じロックの中で行う。
         */
        if (an_awaiting_command()) {
            an_drop_all_idle();
        }

        if (an_hooks.shutdown_requested) {
            const int kind = an_hooks.shutdown_requested(); /* 0=無し/1=穏やか/2=強制 */
            if (kind != 0) {
                an_shutdown_and_quit(kind == 2);
                /*
                 * 穏やかな要求（kind==1）で `inkey_flag` が偽なら、ここへ戻ってくる
                 * （拒まれただけ。設計 §5.5）。強制（kind==2）または `inkey_flag` が
                 * 真のときは `quit()` が longjmp するので戻らない。
                 */
            }
        }

        if (an_hooks.sleep_ms) {
            an_hooks.sleep_ms(AN_IDLE_SLICE_MS);
        }
    }
}

static errr an_term_xtra(int n, int v)
{
    switch (n) {
    case TERM_XTRA_EVENT:
        return an_term_event(v);

    case TERM_XTRA_FLUSH:
        if (an_hooks_ready && an_hooks.drop_keys) {
            an_hooks.drop_keys();
        }
        an_pending_click = 0; /* AH-57: 持ち越していたクリックも入力の一部。捨てる。 */
        return 0;

    case TERM_XTRA_FRESH:
        /*
         * AH-36: `an_menu_note_present()`（AH-13 の「present が要求された」印）は
         * 直す前と同じくここで毎回鳴らす——実際に送るかどうかとは無関係。
         * このまとまりでまだ 1 回も送っていなければ（`an_fresh_burst_open` 偽）
         * 直す前と同じくその場で送る。2 回目以降は溜めるだけにして、
         * 次の区切り（`an_checkpoint_flush()`）でまとめて 1 回だけ送る
         * （ファイル冒頭の註記）。
         */
        an_menu_note_present();
        if (!an_fresh_burst_open) {
            an_fresh_burst_open = 1;
            an_present();
        } else {
            an_present_pending = 1;
        }
        return 0;

    case TERM_XTRA_DELAY:
        /*
         * AH-36: アニメーション（ボルト・爆発・矢）の 1 コマは
         * `Term_fresh()` の直後に `Term_xtra(TERM_XTRA_DELAY, ...)` が来る
         * （`display_bolt()` ほか）。1 コマ 1 回の `Term_fresh()` なら
         * 上のまとまりの最初としてもう送信済みのはずだが、念のため区切りとして
         * 溜まり分も出しておく（待つより前に必ず）。
         */
        an_checkpoint_flush();
        if (an_hooks_ready && an_hooks.sleep_ms && (v > 0)) {
            an_hooks.sleep_ms((v > 200) ? 200 : v);
        }
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
        return 0;
    }
}

static int an_event_refresh_handler_added = 0;

int an_term_install(int cols, int rows)
{
    term *t = &an_term_body;

    if (an_term_ready) {
        return AN_ERR_STATE;
    }

    /* `ui-init.c:76` は 80x24 未満を弾く。設計 §6.1 は 80x27。 */
    if ((cols < 80) || (rows < 24) || (cols > 255) || (rows > 255)) {
        return AN_ERR_ARG;
    }

    if (term_init(t, cols, rows, 1024)) {
        return AN_ERR_TERM;
    }

    t->soft_cursor = true;
    t->xtra_hook = an_term_xtra;

    angband_term[0] = t;
    Term_activate(t);

    /*
     * AH-36: `ui_enter_world()`（`ui-display.c`）が `EVENT_REFRESH` へ
     * 自分の `refresh()` を差す前に、こちらを先に登録しておく——
     * `event_add_handler()` は先頭挿入なので、後から挿さる側（コアの
     * `refresh()`）が先に呼ばれ、こちら（先に挿した側）が後に呼ばれる
     * 並びになる。1 プロセスに Term は 1 枚だけなので、二重登録しない。
     */
    if (!an_event_refresh_handler_added) {
        event_add_handler(EVENT_REFRESH, an_on_event_refresh, NULL);
        an_event_refresh_handler_added = 1;
    }

    an_term_ready = 1;
    return AN_OK;
}

/*! @brief 2 回目のゲームで `EVENT_REFRESH` の受け手を付け直す（`cleanup_angband()` が全部外すため）。 */
void an_term_reinstall_refresh_handler(void)
{
    if (!an_term_ready) {
        return;
    }
    event_remove_handler(EVENT_REFRESH, an_on_event_refresh, NULL);
    event_add_handler(EVENT_REFRESH, an_on_event_refresh, NULL);
    an_event_refresh_handler_added = 1;
}

void an_term_remove(void)
{
    if (!an_term_ready) {
        return;
    }

    Term_activate(NULL);
    angband_term[0] = NULL;
    (void)term_nuke(&an_term_body);
    an_term_ready = 0;
}

int an_term_size(int *cols, int *rows)
{
    if (!an_term_ready) {
        return AN_ERR_STATE;
    }
    if (cols) {
        *cols = an_term_body.wid;
    }
    if (rows) {
        *rows = an_term_body.hgt;
    }
    return AN_OK;
}

/*!
 * @brief 符号位置（UTF-32）を UTF-8 へ足す。
 * @details 呼び手（`an_term_row()`）がサロゲート対を 1 つの符号位置へ組み立ててから渡す。
 */
static int an_append_utf8(char *out, int cap, int pos, unsigned long cp)
{
    if (cp == 0) {
        cp = ' ';
    }
    if (cp <= 0x7Ful) {
        if (pos + 1 >= cap) return pos;
        out[pos++] = (char)cp;
    } else if (cp <= 0x7FFul) {
        if (pos + 2 >= cap) return pos;
        out[pos++] = (char)(0xC0u | (cp >> 6));
        out[pos++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0xFFFFul) {
        if (pos + 3 >= cap) return pos;
        out[pos++] = (char)(0xE0u | (cp >> 12));
        out[pos++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[pos++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        if (pos + 4 >= cap) return pos;
        out[pos++] = (char)(0xF0u | (cp >> 18));
        out[pos++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[pos++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[pos++] = (char)(0x80u | (cp & 0x3Fu));
    }
    return pos;
}

/*!
 * @brief `y` 行目を UTF-8 へ変換して `out` へ書く（末尾 NUL 込み）。
 * @param out 受け皿。`cap` バイト
 * @return 書いたバイト数（末尾 NUL を除く。UTF-8 なので符号位置の数とは限らない）。
 *   負なら `AN_ERR_*`
 * @details Windows の `wchar_t` は 16 bit（UTF-16）。非 ASCII な名前がサロゲート対
 *   （上位 0xD800-0xDBFF・下位 0xDC00-0xDFFF）で来た場合は 1 つの符号位置に組み立ててから
 *   変換する。対をなさない片割れは U+FFFD（置換文字）に落とす
 *   （`an_install_text_hooks()` の `an_text_iswprint()` が事実上こちらへ通さない設計だが、
 *   Term に直接書き込む経路が万一あっても不正な UTF-8 を出さないための保険）。
 */
/*!
 * @brief `an_term_row()` / `an_term_row_cols()` の共通実装。
 * @param trim_trailing_blank 真なら末尾の空白を詰める（`an_term_row()` の挙動）
 */
static int an_term_row_range_impl(int y, int col_start, int col_count, char *out, int cap,
    int trim_trailing_blank)
{
    const term_win *win;
    int x;
    int last;
    int pos;
    int col_end;

    if (!an_term_ready) {
        return AN_ERR_STATE;
    }
    if (!out || (cap <= 1) || (y < 0) || (y >= an_term_body.hgt) || (col_start < 0) || (col_count < 0)) {
        return AN_ERR_ARG;
    }

    win = an_term_body.scr;
    if (!win) {
        return AN_ERR_STATE;
    }

    col_end = col_start + col_count;
    if (col_end > an_term_body.wid) {
        col_end = an_term_body.wid;
    }

    last = col_start - 1;
    for (x = col_start; x < col_end; x++) {
        const wchar_t c = win->c[y][x];
        if (!trim_trailing_blank || ((c != 0) && (c != L' '))) {
            last = x;
        }
    }

    pos = 0;
    for (x = col_start; x <= last; x++) {
        const unsigned long unit = (unsigned long)(unsigned short)win->c[y][x];
        /*
         * 日本語（「読む所」）: 全角の直後の埋め字の
         * セルは字ではない（全角が 2 セル目を占めている印）。英語では 1 つも出ない。
         */
        if (unit == (unsigned long)(unsigned short)AN_WIDE_PAD) {
            continue;
        }
        if ((unit >= 0xD800ul) && (unit <= 0xDBFFul) && (x + 1 <= last)) {
            const unsigned long low = (unsigned long)(unsigned short)win->c[y][x + 1];
            if ((low >= 0xDC00ul) && (low <= 0xDFFFul)) {
                const unsigned long cp = 0x10000ul + ((unit - 0xD800ul) << 10) + (low - 0xDC00ul);
                pos = an_append_utf8(out, cap, pos, cp);
                x++; /* 2 セルで 1 符号位置。 */
                continue;
            }
        }
        if ((unit >= 0xD800ul) && (unit <= 0xDFFFul)) {
            pos = an_append_utf8(out, cap, pos, 0xFFFDul); /* 対をなさない片割れ。 */
            continue;
        }
        pos = an_append_utf8(out, cap, pos, unit);
    }
    if (pos >= cap) {
        pos = cap - 1;
    }
    out[pos] = '\0';
    return pos;
}

int an_term_row(int y, char *out, int cap)
{
    if (!an_term_ready) {
        return AN_ERR_STATE;
    }
    return an_term_row_range_impl(y, 0, an_term_body.wid, out, cap, 1);
}

int an_term_row_cols(int y, int col_start, int col_count, char *out, int cap)
{
    return an_term_row_range_impl(y, col_start, col_count, out, cap, 0);
}

/*!
 * @brief フック #4 の中身（`ui-input.c` の `msg_flush_split_existing()`）。行 0 を折る位置を直す。
 * @details 原作は右から `L' '` を探し、無ければ `MIN(x, w - 8)` で折る。日本語には空白が無い
 * ので、(1) 探した位置が空白でなければ全角の頭（その直前で折れる）を右から探し、(2) どの
 * 場合も全角と埋め字の間では折らない。英語（日本語層が寝ている）では `split` のまま。
 */
int an_msg_split(int split, int w)
{
    int a;
    wchar_t c;
    int i;

    if (!an_lang_enabled() || !Term) {
        return split;
    }
    if ((split >= 0) && (split < w) && (Term_what(split, 0, &a, &c) == 0) && (c == L' ')) {
        return split; /* 原作が空白を見つけた。 */
    }
    for (i = split; i > w / 2; i--) {
        if ((Term_what(i, 0, &a, &c) == 0) && (c != AN_WIDE_PAD) && (an_wcwidth((unsigned long)c) == 2)) {
            return i; /* 全角の直前で折る。 */
        }
    }
    if ((split > 0) && (Term_what(split, 0, &a, &c) == 0) && (c == AN_WIDE_PAD)) {
        return split - 1; /* 全角ごと次の行へ。 */
    }
    return split;
}

/*!
 * @brief 下の状態行の次の欄の桁（P4。`ui-display.c` の `update_statusline_aux()`）。
 * @details 原作の各欄は「書いた英語の字数＋1」を決め打ちで返す（`prt_light()` の 8 ほか）。
 * 日本語の札は英語より幅が違うので、実際に Term に書いた所（カーソルの桁）まで進める。
 * 書いた最後の字が空白でなければ 1 桁あける。返す値は原作の `n` より小さくしない
 * （欄の幅を決め打ちで取っている `prt_terrain()` などは原作のまま）。英語では `n` のまま。
 */
int an_status_advance(int row, int col, int n)
{
    int a;
    wchar_t c;
    int x;
    int used;

    if (!an_lang_enabled() || !Term || (n <= 0)) {
        return n;
    }
    x = Term->scr->cx;
    if ((Term->scr->cy != row) || (x <= col)) {
        return n;
    }
    used = x - col;
    if ((Term_what(x - 1, row, &a, &c) == 0) && (c != L' ')) {
        used++;
    }
    return (used > n) ? used : n;
}

int an_term_cols_to_bytes(int y, int col_start, int col_end)
{
    char buf[1024];
    int n;

    if (col_end <= col_start) {
        return 0;
    }
    n = an_term_row_range_impl(y, col_start, col_end - col_start, buf, (int)sizeof(buf), 0);
    if (n < 0) {
        /* 範囲の外（呼び手の元の計算と同じ値へ倒す。英語では桁 = バイト）。 */
        return col_end - col_start;
    }
    return n;
}

/*!
 * @brief 16〜27（`z-color.h` の拡張色）→ 0〜15 の対応表（設計 §4-1 の案 a）。
 * @details `z-color.c` の `angband_color_table[]`（RGB）から、0〜15 の中でいちばん
 * ユークリッド距離が近い色を機械的に選んだ（見た目の勘ではない）。
 * 添字は 0 が色 16（`COLOUR_PURPLE`）に対応する。
 */
static const unsigned char an_extended_color_table[12] = {
    8,  /* 16 PURPLE      -> L_DARK */
    10, /* 17 VIOLET      -> L_PURPLE */
    5,  /* 18 TEAL        -> GREEN */
    8,  /* 19 MUD         -> L_DARK */
    9,  /* 20 L_YELLOW    -> L_WHITE */
    10, /* 21 MAGENTA     -> L_PURPLE */
    14, /* 22 L_TEAL      -> L_BLUE */
    9,  /* 23 L_VIOLET    -> L_WHITE */
    15, /* 24 L_PINK      -> L_UMBER */
    15, /* 25 MUSTARD     -> L_UMBER */
    9,  /* 26 BLUE_SLATE  -> L_WHITE */
    14, /* 27 DEEP_L_BLUE -> L_BLUE */
};

unsigned char an_round_color(int a)
{
    const int c = a & 0xFF; /* `TERM_SHADE`（28）などの上位ビットは無視。 */
    if (c < 16) {
        return (unsigned char)c;
    }
    if (c <= 27) {
        return an_extended_color_table[c - 16];
    }
    return 1; /* 未知の値は白へ逃がす（黙って 0＝黒にしない）。 */
}

int an_term_colors(int y, unsigned char *out, int cap)
{
    const term_win *win;
    int x;
    int n;

    if (!an_term_ready) {
        return AN_ERR_STATE;
    }
    if (!out || (cap <= 0) || (y < 0) || (y >= an_term_body.hgt)) {
        return AN_ERR_ARG;
    }

    win = an_term_body.scr;
    if (!win) {
        return AN_ERR_STATE;
    }

    n = an_term_body.wid;
    if (n > cap) {
        n = cap;
    }
    for (x = 0; x < n; x++) {
        out[x] = an_round_color(win->a[y][x]);
    }
    return n;
}

int an_term_cursor(int *x, int *y, int *visible)
{
    const term_win *win;

    if (!an_term_ready) {
        return AN_ERR_STATE;
    }
    win = an_term_body.scr;
    if (!win) {
        return AN_ERR_STATE;
    }
    if (x) *x = win->cx;
    if (y) *y = win->cy;
    if (visible) *visible = (!win->cu && win->cv) ? 1 : 0;
    return AN_OK;
}

/*!
 * 変化検出用の安い digestの実体（`an_shim.h` の註記）。
 * FNV-1a（64bit）。手本は `silq/adapter/sq_null_term.c` の `sq_change_digest()`。
 */
unsigned long long an_change_digest(void)
{

    const unsigned long long prime = 1099511628211ULL;
    unsigned long long h = 14695981039346656037ULL;
    const term_win *win;
    int y;
    int x;

#define AN_DIGEST_MIX(BYTE)     do {         h ^= (unsigned long long)(unsigned char)(BYTE);         h *= prime;     } while (0)
#define AN_DIGEST_MIX_INT(V)     do {         unsigned long v_ = (unsigned long)(V);         AN_DIGEST_MIX(v_ & 0xFF);         AN_DIGEST_MIX((v_ >> 8) & 0xFF);         AN_DIGEST_MIX((v_ >> 16) & 0xFF);         AN_DIGEST_MIX((v_ >> 24) & 0xFF);     } while (0)

    if (an_term_ready && ((win = an_term_body.scr) != NULL)) {
        for (y = 0; y < (int)an_term_body.hgt; y++) {
            for (x = 0; x < (int)an_term_body.wid; x++) {
                AN_DIGEST_MIX_INT(win->c[y][x]);
                AN_DIGEST_MIX_INT(win->a[y][x]);
            }
        }
        AN_DIGEST_MIX_INT(win->cx);
        AN_DIGEST_MIX_INT(win->cy);
        AN_DIGEST_MIX(win->cu ? 1 : 0);
        AN_DIGEST_MIX(win->cv ? 1 : 0);
    }

    AN_DIGEST_MIX_INT(turn);
    AN_DIGEST_MIX_INT((unsigned long)an_message_event_count());
    AN_DIGEST_MIX(character_generated ? 1 : 0);
    AN_DIGEST_MIX(character_dungeon ? 1 : 0);
    /*
     * 命令待ち・メニューの開閉も混ぜる（Sil-Q と同じ理由）。
     * `inkey_flag` が立つのは「画を描き終えて `request_command()` が `inkey()` を
     * 呼ぶ直前」で、そのときマスは 1 つも変わっていない。混ぜないと
     * digest が動かず、`frame.awaiting_command` が真になったフレームが送られない。
     */
    AN_DIGEST_MIX(inkey_flag ? 1 : 0);
    AN_DIGEST_MIX_INT(screen_save_depth);
    AN_DIGEST_MIX(player ? 1 : 0);
    if (player) {
        AN_DIGEST_MIX_INT(player->depth);
        AN_DIGEST_MIX_INT(player->grid.x);
        AN_DIGEST_MIX_INT(player->grid.y);
        AN_DIGEST_MIX_INT(player->chp);
        AN_DIGEST_MIX_INT(player->state.cur_light); /* 光の種類・照らされたマスの材料 */
        AN_DIGEST_MIX_INT(player->timed[TMD_IMAGE]);
        AN_DIGEST_MIX_INT(an_status_stamp()); /* 状態のアイコンの材料（一時的な状態・装備の既知の旗） */
    }

#undef AN_DIGEST_MIX
#undef AN_DIGEST_MIX_INT

    return h;
}
