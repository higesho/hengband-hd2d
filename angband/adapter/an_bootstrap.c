/*!
 * @file an_bootstrap.c
 * @brief Angband 4.2 コアの起動列と進行。`init_file_paths()` → `init_angband()` → `play_game(GAME_SELECT)`。
 *
 * 手本は `silq/adapter/sq_bootstrap.c`。
 *
 * ## Sil-Q との違い（どれも「あちらより楽」側。検討 §2・§3 のとおり）
 * - **タイトル／セーブ選択を自作しない。** `play_game(GAME_SELECT)` にすると
 *   原作の `select_savefile()`（`ui-game.c:770`）が Term にメニューを描く。
 * - **言語の下ごしらえが無い。** S1 は英語のみ（設計の範囲外に日本語化を明記）。
 * - **`core_aux` が無い。** この版の `z-util.h` は `plog_aux` / `quit_aux` の 2 つだけ。
 *
 * ## longjmp の戻り先は 2 つ（Sil-Q と同じ理由）
 * `quit()` は**起動の失敗**にも**遊び終えた合図**にも使われる。同じ戻り先で受けると
 * 正常終了が失敗に化けるので、段ごとに別の `jmp_buf` を持つ。
 */

#include "angband.h"

#include "buildid.h"
#include "cave.h"
#include "cmd-core.h"
#include "init.h"
#include "savefile.h"
#include "store.h"
#include "player-birth.h"
#include "ui-birth.h"
#include "ui-display.h"
#include "ui-game.h"
#include "ui-init.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-output.h"
#include "ui-prefs.h"
#include "game-world.h"

#include "portable/legacy_os_c.h"

#include "an_lang_c.h"
#include "an_map_shim.h"
#include "an_shim.h"
#include "an_targeting.h"
#include "an_ui_shim.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/*! 起動の段の longjmp の戻り先（`an_bootstrap`）。ここへ跳ぶ `quit()` は**失敗**である。 */
static jmp_buf an_abort_buf;
static int an_abort_armed = 0;
static int an_abort_kind = 0;

/*! 進行の段の longjmp の戻り先（`an_run_game`）。ここへ跳ぶ `quit()` は**正常終了**である。 */
static jmp_buf an_game_buf;
static int an_game_armed = 0;
static int an_game_kind = 0;

/*! `main-win.c` の `game_in_progress` に当たる（設計 §5.5）。 */
static int an_game_in_progress = 0;

/*! 強制終了の再入よけ。 */
static int an_shutting_down = 0;

static char an_error_text[1024] = "";

/*! A1/A2 が呼ばれた回数（診断用。設計 §4 の受け手は S1 では記録するだけでよい）。 */
static int an_menu_hooks_seen = 0;

const char *an_last_error(void)
{
    return an_error_text;
}

const char *an_core_version(void)
{
    /* `buildid.c` の `VERSION_STRING`（"4.2.6"）。決め打ちの文字列を持たない。 */
    return buildver;
}

static void an_record_error(const char *tag, const char *str)
{
    snprintf(an_error_text, sizeof(an_error_text), "%.16s: %.980s", tag,
        (str && str[0]) ? str : "(no message)");
    fprintf(stderr, "[angband] %s\n", an_error_text);
    fflush(stderr);
}

/*! `plog()` のフック。人間向けの出力は stdout ではなく stderr へ（プロトコル v1 §1.1）。 */
static void an_plog_aux(const char *str)
{
    fprintf(stderr, "[angband:plog] %s\n", (str && str[0]) ? str : "");
    fflush(stderr);
}

/*!
 * @brief `quit()` のフック。段によって意味が変わる（起動＝失敗、進行＝正常終了）。
 * @details 進行の段では、文字列が付いていて `"user choice"`（`cmd-wizard.c` の
 * デバッグ「終了」コマンド）でなければ**異常終了**として扱う（S1 の見直しで訂正）。
 * `an_game_kind` に `AN_ERR_CORE` を入れておき、`an_run_game()` の呼び手
 * （`an_main.cpp`）がそれを見て `fatal` を送る。ゲーム中の `quit_fmt()`
 * （`obj-pile.c:117` の整合性崩れなど）はこちらに当たる。
 */
static void an_quit_aux(const char *str)
{
    if (an_game_armed) {
        const int is_normal = (!str || !str[0] || (strcmp(str, "user choice") == 0));
        if (str && str[0]) {
            fprintf(stderr, "[angband] quit: %s\n", str);
        } else {
            fprintf(stderr, "[angband] quit (normal)\n");
        }
        fflush(stderr);

        if (!is_normal) {
            an_record_error("quit", str);
            an_game_kind = AN_ERR_CORE;
        } else {
            an_game_kind = AN_OK;
        }

        an_game_armed = 0;
        longjmp(an_game_buf, 1);
    }

    an_record_error("quit", str);

    if (an_abort_armed) {
        an_abort_armed = 0;
        an_abort_kind = AN_ERR_QUIT;
        longjmp(an_abort_buf, 1);
    }

    /* longjmp の戻り先が無ければコアの既定に任せる（quit() がこの後 exit する）。 */
}

/* ==================================================================== A1/A2（設計 §4） */

/*! `an_menu_begin()` を呼ぶたびに 1 増える。S3 が古い行の記録を捨てる目印（`an_shim.h` の註）。 */
static int an_menu_epoch_value = 0;

/*!
 * @brief AH-13: `an_present()` が呼ばれるたびに 1 増える（`an_menu_note_present()`。
 * `an_shim.h` の註）。
 * @details 知識画面（`ui-knowledge.c` の `display_knowledge()`）は非活性側→
 * 活性側の順で、1 回のループの中で `menu_refresh()`（＝`an_menu_begin()`）を
 * 2 回呼ぶ——その間、画面は 1 度も外へ present されない。この目印が
 * 「前の `an_menu_begin()` と今回の `an_menu_begin()` のあいだに present が
 * 挟まったか」を判じる材料になる。
 */
static int an_present_epoch_value = 0;

/*! 直近の `an_menu_begin()` の時点での `an_present_epoch_value`。初期値 -1 は
 * 「まだ一度も `an_menu_begin()` を呼んでいない」の意味（0 と区別するため）。 */
static int an_menu_begin_present_epoch = -1;

void an_menu_note_present(void)
{
    an_present_epoch_value++;
}

/*!
 * @brief 直近のメニュー 1 枚ぶんの行の記録（設計 §6.3「選択肢」）。
 * @details `an_menu_begin()` で空にする——**世代が変わったら古い記録は要らない**
 * （`an_menu_epoch()` と同じ理由。1 枚のメニューしか同時に描かれないので、
 * 世代の突き合わせより配列を空にするほうが単純で確実）。
 */
#define AN_MENU_ROW_CAP 128
typedef struct an_menu_row_record {
    int row;
    int col;
    int width;
    int cursor;
    char sel;
    int available; /*!< 見直しの指摘 11。`valid_row()` が MN_ROW_VALID を返したら 1 */
    char snapshot[32]; /*!< AH-11。描いた直後の行内容の写し（`an_shim.h` の註記） */
    int snapshot_len;
} an_menu_row_record;
static an_menu_row_record an_menu_rows[AN_MENU_ROW_CAP];
static int an_menu_row_count = 0;

/*!
 * @brief `menu->active`（設計 §6.3 の見直し。詳細欄の混入直し）。`an_menu_begin()` で
 * 毎回取り直す——`menu->active` は `menu_calc_size()` が `region_calculate()` で
 * 解決済みの絶対座標にしてある（ゼロ初期化のままの全面表示メニューでも画面いっぱいの
 * 矩形へ解決される）ので、そのまま写せば「箱の外を切る」も「切らない（全面表示）」も
 * 同じ経路で扱える。
 */
static int an_menu_active_row = -1;
static int an_menu_active_col = -1;
static int an_menu_active_width = -1;
static int an_menu_active_page_rows = -1;

/*!
 * @brief `menu->cmd_keys`（一覧の行を経由せず直接効くコマンド鍵。`d`/`g`/`?`/
 * `=` ほか。`ui-store.c` の `store_menu_set_selections()` が積む）。
 * @details 見出し込みで空の我が家の直し（本スレッド、決めたこと）。
 * `an_menu_row_drawn()` は一覧の行しか見ないので、一覧 0 件の店では
 * `frame.menu_choices` が空のまま新 UI へ届き、「預ける」も「出る」も
 * 押せなくなる（起票済みの罠）。
 * `menu->cmd_keys` は静的文字列リテラル（`store_menu_set_selections()` が
 * 代入する 2 通りのどちらか）なので、**中身をコピーせずポインタだけ**
 * 持ち回してよい——`menu` 自体の生存期間より長生きする。
 */
static const char *an_menu_cmd_keys_value = NULL;

/*!
 * @brief AH-09/AH-06: 直近の `an_menu_begin()` が持ち回っている `menu->switch_keys`。
 * @details `an_menu_cmd_keys_value` と同じ理由・同じ寿命（静的文字列リテラルの
 * ポインタのみ）。`item_menu()`（`ui-object.c`）の `m->switch_keys = "/|-";` を
 * 拾うためだけに増やした。
 */
static const char *an_menu_switch_keys_value = NULL;

/*!
 * @brief いま開いている画面が店（0=違う／1=我が家／2=それ以外の店）。
 * @details `an_menu_begin()` の時点で `player->grid` の足元を見る——
 * 店の画面（`use_store()`）は必ず店のマスに立ったまま開いているので、
 * その場で判じてよい（`cmd-cave.c` の `player_handle_post_move()` と同じ
 * 前提）。店でない画面（birth・持ち物一覧など）では `store_at()` が
 * `NULL` を返すので 0 のままになる。
 */
static int an_menu_store_kind_value = 0;

/*!
 * @brief AH-42: 店の一覧が原作のヘルプ（`?` で切り替える `STORE_SHOW_HELP`）を出しているか。
 * @details `ui-store.c` の `store_display_recalc()` はヘルプを出すとき一覧の箱を
 * `loc.page_rows = -5`、出さないとき `-2` にして `menu_layout()` へ渡す（`menu->boundary`
 * に残る）。`struct store_context` は `ui-store.c` の中だけの型で外から flags を読めないので、
 * この箱の値で見分ける。
 */
static int an_menu_store_help_value = 0;

/*!
 * @brief 店の画面（`use_store()`）が実際に開いている間だけ 1 になる旗（調-05・F-angband-13）。
 * @details 以前は「プレイヤーの足元が店のマス（`store_at()`）か」だけで店の中と数えていたので、
 * 店に入って出た直後（店の入口に立っている）は、持ち物の窓などの店と無関係なメニューまで店の画として
 * 扱われ、店のメニューの受け手が選択を横取りして返事（`request_id`）を返さず、画面が「応答待ち」のまま
 * 取消を受け付けなくなっていた。原作は店に入るとき `enter_store()`（`ui-store.c`）が
 * `EVENT_LEAVE_WORLD` を、店を出るとき `leave_store()` が `EVENT_ENTER_WORLD` を送る
 * （どちらもゲーム開始・終了のときにも送られるが、旗は `store_at()` と併せて見るので害はない）。
 * その 2 つの受け手を足して旗を立て下ろしする（原作のコードには触れない）。
 */
static int an_store_session_value = 0;

static void an_store_session_on_leave_world(game_event_type type, game_event_data *data, void *user)
{
    (void)type;
    (void)data;
    (void)user;
    an_store_session_value = 1;
}

static void an_store_session_on_enter_world(game_event_type type, game_event_data *data, void *user)
{
    (void)type;
    (void)data;
    (void)user;
    an_store_session_value = 0;
}

void an_store_session_install_handlers(void)
{
    an_store_session_value = 0;
    /* 付け直し（2 回目のゲーム）でも二重にならないよう、先に外す（無ければ何もしない）。 */
    event_remove_handler(EVENT_LEAVE_WORLD, an_store_session_on_leave_world, NULL);
    event_remove_handler(EVENT_ENTER_WORLD, an_store_session_on_enter_world, NULL);
    event_add_handler(EVENT_LEAVE_WORLD, an_store_session_on_leave_world, NULL);
    event_add_handler(EVENT_ENTER_WORLD, an_store_session_on_enter_world, NULL);
}

int an_store_session_active(void)
{
    return an_store_session_value;
}

/*!
 * @brief A1 の受け手。`ui-menu.c` の `display_menu_row()` が `sel`（選ぶ字）を
 * 決めた直後に呼ぶ（既定は同ファイル内の弱い空関数。ここが実体）。
 * @param pos 並びの中の位置（`menu->filter_list` を通す前）
 * @param oid `pos` を `filter_list` で引いた後のオブジェクト id
 * @param sel その行を選ぶ字（`get_tag()` / `menu->selections[pos]` の結果。無ければ 0）
 * @details 隠れた行（`MN_ROW_HIDDEN`）は `display_menu_row()` がここへ来る前に
 * 抜けている。
 *
 * 見直しの指摘 11: 以前はここで `available` を常に真にしていた。原作
 * `display_menu_row()`（`ui-menu.c`）は `menu->row_funcs->valid_row` が
 * NULL でなければ `valid_row(menu, oid)` を呼び、`MN_ROW_VALID` かどうかで
 * 表示の色（有効/無効）を決めている（`menu_row_style_for_validity()`）。
 * このフックは `display_row()` の直後——`display_menu_row()` は `oid` を
 * 既に持っているので、ここでも同じ `valid_row(menu, oid)` を呼べば、原作と
 * 同じ判定を使い回せる（`valid_row` が無ければ、原作の `is_valid_row()` と
 * 同じく「常に有効」に倒す）。`MN_ROW_HIDDEN` はここへ来る前に抜けているので
 * 実際に来るのは `MN_ROW_VALID`/`MN_ROW_INVALID` の 2 つだけ——
 * `MN_ROW_VALID`（値 1）以外なら `available=0` とする。
 */
void an_menu_row_drawn(struct menu *menu, int pos, int oid, char sel, bool cursor, int row, int col, int width)
{
    (void)pos;
    if (an_menu_row_count < AN_MENU_ROW_CAP) {
        an_menu_row_record *record = &an_menu_rows[an_menu_row_count++];
        record->row = row;
        record->col = col;
        record->width = width;
        record->cursor = cursor ? 1 : 0;
        record->sel = sel;
        if (menu && menu->row_funcs && menu->row_funcs->valid_row) {
            record->available = (menu->row_funcs->valid_row(menu, oid) == MN_ROW_VALID) ? 1 : 0;
        } else {
            record->available = 1;
        }
        /*
         * AH-11: ここは `row_funcs->display_row()`（`ui-menu.c:609`）の直後——
         * その行の内容はもう Term に描かれている。`sel` の有無に関わらず写し取って
         * おけば、`discard_stale_menu_if_needed()` が `sel==0` の行（カーソルだけで
         * 選ぶ一覧）でも「いまの Term と一致するか」で判じられる。
         */
        record->snapshot_len = 0;
        if (width > 0) {
            int want = width;
            if (want > (int)sizeof(record->snapshot) - 1) {
                want = (int)sizeof(record->snapshot) - 1;
            }
            const int n = an_term_row_cols(row, col, want, record->snapshot, (int)sizeof(record->snapshot));
            if (n > 0) {
                record->snapshot_len = n;
            }
        }
    }
    an_menu_hooks_seen++;
}

/*!
 * @brief A2 の受け手（開始）。世代を進め、前のメニューの行の記録を捨て、`menu->active`
 * （このメニューが実際に自分の物として描いている矩形）を取り直す。
 * @details AH-13: 知識画面（`ui-knowledge.c` の
 * `display_knowledge()`）は 1 回のループで非活性側のメニュー→活性側のメニューの
 * 順に `menu_refresh()`（＝ここ）を呼ぶ。以前はここで毎回 `menu->active` を
 * 無条件に置き換えていたため、→ / Enter で活性側へ移った直後は箱が右列
 * （`col=13`）だけになり、`box_left_for_row()`（`an_main.cpp`）が左列を
 * 切り落としていた。
 *
 * 直しは、**前回の `an_menu_begin()` からまだ 1 度も present（`an_present()`）
 * していない**（＝画面は 1 度も外へ出ていない＝同じフレームの中で 2 つの
 * メニューを続けて開いた）ときに限り、前回の箱と今回の箱を**行の範囲が
 * 重なり、桁の範囲が重ならない**（左右に並ぶ 2 列、という形だけ）場合だけ
 * 合わせて 1 つの箱にする——親メニュー＋同じ回に描かれた小さな浮動メニュー
 * のような「行も桁も重なる」組み合わせでは、この条件に当たらないので
 * 今までどおり置き換える。
 */
/*! 店の一覧を新 UI の店の画面へ運ぶため（`an_native_inventory.c` の `an_native_store_info()`）、
 * いま開いているメニューを覚える（AH-41）。古い記録を捨てるときは一緒に捨てる。 */
static struct menu *an_menu_current_value = NULL;

struct menu *an_menu_current(void)
{
    return an_menu_current_value;
}

/*!
 * @name 地図の右クリックが開く文脈メニュー（AH-58）
 * @details 右クリック（修飾なし）を Term へ積んだら `an_menu_expect_click_context()` で
 * 「次に始まる動的メニュー」を待つ。動的メニュー（`ui-menu.c` の `menu_dynamic_select()`。
 * `ui-context.c` の `context_menu_cave()`・`context_menu_player()` などが使う）が始まると
 * `an_menu_dynamic_begin()` が、待っていればそのメニュー（`struct menu` のポインタ）を文脈メニュー
 * として覚え、`menu_dynamic_select()` が終わる（`an_menu_dynamic_end()`）まで
 * `an_menu_is_click_context()` が真を返す。`context_menu_player()` の「その他」のように、
 * 文脈メニューを閉じてすぐ（別の入力を読まずに）続きの小メニューを開くものは、終わりでもう一度
 * 期待を立てて同じ扱いにする。期待は、別の入力を読んだとき（`an_menu_forget_click_context_expectation()`）
 * に捨てる——右クリックがメニューを開かなかったとき（原作の `mouse_movement` オプションが偽など）、
 * あとで開いた別のメニューを文脈メニューと取り違えないため。
 * @{
 */
static int an_click_context_expected = 0;
static struct menu *an_click_context_menu = NULL;

void an_menu_expect_click_context(void)
{
    an_click_context_expected = 1;
}

void an_menu_forget_click_context_expectation(void)
{
    an_click_context_expected = 0;
}

int an_menu_is_click_context(void)
{
    return an_click_context_menu != NULL;
}

void an_menu_dynamic_begin(struct menu *menu)
{
    if (an_click_context_expected && menu) {
        an_click_context_menu = menu;
        an_click_context_expected = 0;
    }
}

void an_menu_dynamic_end(struct menu *menu)
{
    if (menu && (menu == an_click_context_menu)) {
        an_click_context_menu = NULL;
        an_click_context_expected = 1; /* 続きの小メニューが同じクリックのものなら、それも文脈メニュー */
    }
}
/*! @} */

void an_menu_begin(struct menu *menu)
{
    an_menu_current_value = menu;
    int new_row = -1;
    int new_col = -1;
    int new_width = -1;
    int new_page_rows = -1;
    const char *new_cmd_keys = NULL;
    const char *new_switch_keys = NULL;

    if (menu) {
        new_row = menu->active.row;
        new_col = menu->active.col;
        new_width = menu->active.width;
        new_page_rows = menu->active.page_rows;
        new_cmd_keys = menu->cmd_keys;
        new_switch_keys = menu->switch_keys;
    }

    const int no_present_since_last_begin = (an_menu_begin_present_epoch >= 0)
        && (an_menu_begin_present_epoch == an_present_epoch_value);
    const int prev_valid = (an_menu_active_width > 0) && (an_menu_active_page_rows > 0);
    const int new_valid = (new_width > 0) && (new_page_rows > 0);
    int do_union = 0;

    if (no_present_since_last_begin && prev_valid && new_valid) {
        const int prev_top = an_menu_active_row;
        const int prev_bottom = an_menu_active_row + an_menu_active_page_rows;
        const int prev_left = an_menu_active_col;
        const int prev_right = an_menu_active_col + an_menu_active_width;
        const int new_top = new_row;
        const int new_bottom = new_row + new_page_rows;
        const int new_left = new_col;
        const int new_right = new_col + new_width;
        const int rows_overlap = (prev_top < new_bottom) && (new_top < prev_bottom);
        const int cols_overlap = (prev_left < new_right) && (new_left < prev_right);
        if (rows_overlap && !cols_overlap) {
            do_union = 1;
        }
    }

    if (do_union) {
        const int top = (an_menu_active_row < new_row) ? an_menu_active_row : new_row;
        const int left = (an_menu_active_col < new_col) ? an_menu_active_col : new_col;
        const int prev_bottom = an_menu_active_row + an_menu_active_page_rows;
        const int new_bottom = new_row + new_page_rows;
        const int bottom = (prev_bottom > new_bottom) ? prev_bottom : new_bottom;
        const int prev_right = an_menu_active_col + an_menu_active_width;
        const int new_right = new_col + new_width;
        const int right = (prev_right > new_right) ? prev_right : new_right;

        an_menu_active_row = top;
        an_menu_active_col = left;
        an_menu_active_width = right - left;
        an_menu_active_page_rows = bottom - top;
        /*
         * 見直しの指摘（AH-13）: 箱を合わせても `cmd_keys`/`switch_keys` は
         * **今回のメニューのものだけ**を使う——以前は今回が NULL（そのメニューに
         * 直接効くコマンド鍵が無い）だと前回のメニューの値を残していたため、
         * 前の画面の「売る」等のボタンが無関係な画面にまで出続けることがあった。
         * 継承しない（NULL なら NULL のまま）。
         */
        an_menu_cmd_keys_value = new_cmd_keys;
        an_menu_switch_keys_value = new_switch_keys;
    } else {
        an_menu_active_row = new_row;
        an_menu_active_col = new_col;
        an_menu_active_width = new_width;
        an_menu_active_page_rows = new_page_rows;
        an_menu_cmd_keys_value = new_cmd_keys;
        an_menu_switch_keys_value = new_switch_keys;
    }

    an_menu_epoch_value++;
    an_menu_row_count = 0;
    an_menu_hooks_seen++;
    an_menu_begin_present_epoch = an_present_epoch_value;

    an_menu_store_kind_value = 0;
    if (an_store_session_value && player && cave) {
        struct store *store = store_at(cave, player->grid);
        if (store) {
            an_menu_store_kind_value = (store->feat == FEAT_HOME) ? 1 : 2;
        }
    }
    an_menu_store_help_value = (an_menu_store_kind_value != 0) && menu
        && (menu->boundary.page_rows == -5);
}

const char *an_menu_cmd_keys(void)
{
    return an_menu_cmd_keys_value;
}

const char *an_menu_switch_keys(void)
{
    return an_menu_switch_keys_value;
}

int an_menu_store_kind(void)
{
    return an_menu_store_kind_value;
}

int an_menu_store_help_shown(void)
{
    return an_menu_store_help_value;
}

/*! A2 の受け手（終了）。行の記録はそのまま残す（読み出すのは次の frame の組み立て時）。 */
void an_menu_end(struct menu *menu)
{
    (void)menu;
    an_menu_hooks_seen++;
}

int an_menu_hook_calls(void)
{
    return an_menu_hooks_seen;
}

int an_menu_epoch(void)
{
    return an_menu_epoch_value;
}

int an_menu_choice_count(void)
{
    return an_menu_row_count;
}

int an_menu_choice_at(int index, an_menu_choice_row *out)
{
    if (!out || (index < 0) || (index >= an_menu_row_count)) {
        return 0;
    }
    out->row = an_menu_rows[index].row;
    out->col = an_menu_rows[index].col;
    out->width = an_menu_rows[index].width;
    out->cursor = an_menu_rows[index].cursor;
    out->sel = an_menu_rows[index].sel;
    out->available = an_menu_rows[index].available;
    out->snapshot_len = an_menu_rows[index].snapshot_len;
    if (out->snapshot_len > 0) {
        memcpy(out->snapshot, an_menu_rows[index].snapshot, (size_t)out->snapshot_len);
    }
    return 1;
}

int an_menu_active_region(int *row, int *col, int *width, int *page_rows)
{
    if ((an_menu_active_width <= 0) || (an_menu_active_page_rows <= 0)) {
        return 0;
    }
    if (row) *row = an_menu_active_row;
    if (col) *col = an_menu_active_col;
    if (width) *width = an_menu_active_width;
    if (page_rows) *page_rows = an_menu_active_page_rows;
    return 1;
}

/*! `an_shim.h` の註記のとおり。`an_menu_begin()` と同じ形で空にする
 * （世代は進めない——「新しいメニューが開いた」わけではないので）。 */
void an_menu_discard_stale(void)
{
    an_menu_row_count = 0;
    an_menu_active_row = -1;
    an_menu_active_col = -1;
    an_menu_active_width = -1;
    an_menu_active_page_rows = -1;
    /* 2026-09-28: コマンド鍵も捨てる。残すと能力値の決め方のメニューの '?' が「ヘルプ」のボタンとして
     * ポイント制の画面に残り、左の一覧がその 1 行になって上下が原作へ届かなかった（AH-39）。 */
    an_menu_cmd_keys_value = NULL;
    an_menu_switch_keys_value = NULL;
    an_menu_current_value = NULL;
}

/* ==================================================================== A7（設計 §4。AH-24） */

/*! AH-24: `an_textblock_shown()` が最後に記録した箱（`an_shim.h` の註記のとおり）。 */
static int an_textblock_row = -1;
static int an_textblock_col = -1;
static int an_textblock_width = -1;
static int an_textblock_rows_used = -1;
static char an_textblock_snapshot[32];
static int an_textblock_snapshot_len = 0;

void an_textblock_shown(int row, int col, int width, int rows_used)
{
    an_textblock_row = row;
    an_textblock_col = col;
    an_textblock_width = width;
    an_textblock_rows_used = rows_used;

    an_textblock_snapshot_len = 0;
    if (width > 0) {
        int want = width;
        if (want > (int)sizeof(an_textblock_snapshot) - 1) {
            want = (int)sizeof(an_textblock_snapshot) - 1;
        }
        const int n = an_term_row_cols(row, col, want, an_textblock_snapshot, (int)sizeof(an_textblock_snapshot));
        if (n > 0) {
            an_textblock_snapshot_len = n;
        }
    }
}

int an_textblock_active_region(int *row, int *col, int *width, int *rows_used)
{
    if ((an_textblock_width <= 0) || (an_textblock_rows_used <= 0)) {
        return 0;
    }
    if (an_textblock_snapshot_len <= 0) {
        return 0; /* 写しが無ければ「いまの Term とまだ一致するか」を判じられない。 */
    }

    /* 日本語（設計 §3.2）: 写しを取ったときと同じ桁（`min(width, 31)`）で読み直す。
     * 以前は写しの**バイト数**を桁数として渡していた（英語では同じ値）。 */
    char text[sizeof(an_textblock_snapshot)];
    int want = an_textblock_width;
    if (want > (int)sizeof(an_textblock_snapshot) - 1) {
        want = (int)sizeof(an_textblock_snapshot) - 1;
    }
    const int n = an_term_row_cols(an_textblock_row, an_textblock_col, want, text, (int)sizeof(text));
    if ((n != an_textblock_snapshot_len) || (memcmp(text, an_textblock_snapshot, (size_t)n) != 0)) {
        return 0; /* もう別の画面。前の記録は使わない。 */
    }

    if (row) *row = an_textblock_row;
    if (col) *col = an_textblock_col;
    if (width) *width = an_textblock_width;
    if (rows_used) *rows_used = an_textblock_rows_used;
    return 1;
}

/* ==================================================================== 起動 */

/*!
 * @brief `init_file_paths()` へ渡した後、`ANGBAND_DIR_USER` 以下を §2 の置き場
 * （`data_dir/angband/{user,save,panic,scores,archive}`）へ差し替える。
 * @details `init_file_paths()` は冒頭で全部 `string_free()` してから組み直すので、
 * **この関数はその後で呼ぶこと**（設計 §5.2 の手順どおり）。
 */
static int an_redirect_data_dirs(const char *data_dir)
{
    char path[1024];
    size_t base_len = strlen(data_dir);

    if ((base_len == 0) || (base_len + 32 >= sizeof(path))) {
        return 0;
    }

    /*
     * 置き場の下の 5 つのフォルダは**ここで先に作る**（原作の `create_needed_dirs()` は、無ければ `dir_create()` で作るが、
     * `dir_create()` はパスを 1 バイトずつ見て区切り文字（0x5C）で切る。Shift-JIS の 2 バイト目が 0x5C になる字
     * （表・ソ・能・十・予・申 など）を置き場のパスに含む遊ぶ人では、字の途中で切って 'Cannot create' の fatal で
     * 起動できなかった）。作ってあれば `dir_create()` は何もしない。
     */
    (void)portable_make_dir(data_dir);

#define AN_REDIRECT(VAR, NAME) \
    do { \
        snprintf(path, sizeof(path), "%s%s%s", data_dir, \
            (data_dir[base_len - 1] == '\\' || data_dir[base_len - 1] == '/') ? "" : PATH_SEP, (NAME)); \
        string_free(VAR); \
        VAR = string_make(path); \
        (void)portable_make_dir(path); \
    } while (0)

    AN_REDIRECT(ANGBAND_DIR_USER, "user");
    AN_REDIRECT(ANGBAND_DIR_SAVE, "save");
    AN_REDIRECT(ANGBAND_DIR_PANIC, "panic");
    AN_REDIRECT(ANGBAND_DIR_SCORES, "scores");
    AN_REDIRECT(ANGBAND_DIR_ARCHIVE, "archive");

#undef AN_REDIRECT

    return 1;
}

/*!
 * @brief 日本語のときだけ `ANGBAND_DIR_GAMEDATA` を `<lib>/../lib-ja/gamedata` へ向ける
 *
 *
 * @details `lib-ja/gamedata` は `tools/angband/an_build_ja_gamedata.py` が作る英語の写しで、
 * 説明の行（`desc:` ほか）だけが日本語。`name:` を含むほかの行は英語と 1 バイトも違わないので、
 * セーブは言語をまたいで開ける（§4「セーブの置き場は言語で分けない」）。
 * 置き場が無い（作成し忘れ・配布物に入れ忘れ）ときは英語の `lib/gamedata` のまま進め、
 * stderr に 1 行出す（制約 5「訳が無ければ英語をそのまま出す」）。英語の起動では何もしない
 * （制約 1）。`customize` `help` `screens` は英語の `lib` のまま（ヘルプは P5 で別に向ける）。
 */
static void an_redirect_gamedata_ja(const char *lib_path)
{
    char path[1024];
    size_t len;

    if (!an_lang_enabled()) {
        return;
    }
    len = strlen(lib_path);
    while (len > 0 && (lib_path[len - 1] == '\\' || lib_path[len - 1] == '/')) {
        len--;
    }
    /* `<lib>` の親（`angband/`）を探す。 */
    while (len > 0 && lib_path[len - 1] != '\\' && lib_path[len - 1] != '/') {
        len--;
    }
    if (len + 32 >= sizeof(path)) {
        return;
    }
    memcpy(path, lib_path, len);
    snprintf(path + len, sizeof(path) - len, "lib-ja%sgamedata", PATH_SEP);
    if (!portable_dir_exists(path)) {
        fprintf(stderr, "[angband] lib-ja/gamedata not found (%s); descriptions stay English\n", path);
        fflush(stderr);
        return;
    }
    string_free(ANGBAND_DIR_GAMEDATA);
    ANGBAND_DIR_GAMEDATA = string_make(path);
    fprintf(stderr, "[angband] gamedata = %s\n", path);
    fflush(stderr);
}

/*! 日本語のヘルプの置き場（`<lib>/../lib-ja/help`）。無い・英語のときは空。 */
static char an_help_ja_dir[1024];

/*!
 * @brief 日本語のときだけ `<lib>/../lib-ja/help` を覚える（#12・§18。P5）。
 *
 * @details `lib-ja/help` は `tools/angband/an_build_ja_help.py` が `angband/lang/ja/help` から作る
 * （78 桁で折り済み）。`ANGBAND_DIR_HELP` は差し替えない——訳の無いファイルは英語の
 * `lib/help` から開く（制約 5）。探すのは `ui-help.c` の `show_file()`（フック #12 → `an_help_path()`）。
 */
static void an_find_help_ja(const char *lib_path)
{
    size_t len;

    an_help_ja_dir[0] = '\0';
    if (!an_lang_enabled()) {
        return;
    }
    len = strlen(lib_path);
    while (len > 0 && (lib_path[len - 1] == '\\' || lib_path[len - 1] == '/')) {
        len--;
    }
    while (len > 0 && lib_path[len - 1] != '\\' && lib_path[len - 1] != '/') {
        len--;
    }
    if (len + 32 >= sizeof(an_help_ja_dir)) {
        return;
    }
    memcpy(an_help_ja_dir, lib_path, len);
    snprintf(an_help_ja_dir + len, sizeof(an_help_ja_dir) - len, "lib-ja%shelp", PATH_SEP);
    if (!portable_dir_exists(an_help_ja_dir)) {
        fprintf(stderr, "[angband] lib-ja/help not found (%s); help stays English\n", an_help_ja_dir);
        fflush(stderr);
        an_help_ja_dir[0] = '\0';
        return;
    }
    fprintf(stderr, "[angband] help (ja) = %s\n", an_help_ja_dir);
    fflush(stderr);
}

/*!
 * @brief `show_file()` が開くヘルプのパスを、日本語の訳があればそちらにする（フック #12）。
 * @return 訳のファイルが在って `buf` へ書いたら 1。0 なら原作どおり `ANGBAND_DIR_HELP` から開く。
 */
int an_help_path(char *buf, size_t cap, const char *name)
{
    if (!buf || !name || !an_help_ja_dir[0] || !an_lang_enabled()) {
        return 0;
    }
    path_build(buf, cap, an_help_ja_dir, name);
    if (!file_exists(buf)) {
        return 0;
    }
    return 1;
}

/*!
 * @brief `an_lib_root()` の実体（`an_shim.h`）。`an_bootstrap()` が求めた
 * `lib_path`（末尾に区切りつき）をそのまま持つ。
 */
static char s_lib_root[1024] = { 0 };

const char *an_lib_root(void)
{
    return s_lib_root;
}

int an_bootstrap(const char *lib_dir, const char *data_dir)
{
    char lib_path[1024];
    size_t len;

    if (!lib_dir || !lib_dir[0]) {
        strcpy(an_error_text, "lib directory was not given");
        return AN_ERR_ARG;
    }
    if (!data_dir || !data_dir[0]) {
        strcpy(an_error_text, "data directory was not given");
        return AN_ERR_ARG;
    }

    len = strlen(lib_dir);
    if (len + 32 >= sizeof(lib_path)) {
        strcpy(an_error_text, "lib directory path is too long");
        return AN_ERR_ARG;
    }
    strcpy(lib_path, lib_dir);
    if (!suffix(lib_path, PATH_SEP)) {
        strcat(lib_path, PATH_SEP);
    }
    strcpy(s_lib_root, lib_path); /* an_town_map.c 用（`lib_path` は関数末尾で消える）。 */

    if (!portable_dir_exists(lib_path)) {
        snprintf(an_error_text, sizeof(an_error_text), "lib directory not found: %.900s", lib_path);
        fprintf(stderr, "[angband] %s\n", an_error_text);
        fflush(stderr);
        return AN_ERR_LIB;
    }

    /* Term が要る。原作のモジュール初期化が進捗を Term へ書くことがある。 */
    {
        int cols = 0;
        int rows = 0;
        if (an_term_size(&cols, &rows) != AN_OK) {
            strcpy(an_error_text, "no term installed (call an_term_install first)");
            return AN_ERR_STATE;
        }
    }

    /* z-util.h:49-50 の 2 つのフックを差し込む。 */
    plog_aux = an_plog_aux;
    quit_aux = an_quit_aux;

    /*
     * UTF-8 の変換フック（設計 §6.1）。ゲームデータの非 ASCII な名前
     * （`Sméagol` ほか）は `init_angband()` の中で読まれるので、その前に差す。
     */
    an_install_text_hooks();

    /*
     * "win" だと customize/font-win.prf が読まれ、床が字コード 8 になる
     * （S1 の見直しで確認。設計 §6.1）。当方は独自の値でよい。
     */
    ANGBAND_SYS = "hd2d";
    use_graphics = 0;

    an_error_text[0] = '\0';
    an_abort_kind = 0;
    an_abort_armed = 1;

    if (setjmp(an_abort_buf) != 0) {
        an_abort_armed = 0;
        return an_abort_kind ? an_abort_kind : AN_ERR_QUIT;
    }

    /*
     * `config` / `lib` / `data` の 3 引数はどれも同じ `lib_path` を渡す
     * （`gamedata` / `customize` / `help` / `screens` はこれで揃って引ける。
     * 設計 §2）。`ANGBAND_DIR_USER` 系は直後に §2 の置き場へ差し替える。
     */
    init_file_paths(lib_path, lib_path, lib_path);
    an_redirect_gamedata_ja(lib_path);
    an_find_help_ja(lib_path);
    if (!an_redirect_data_dirs(data_dir)) {
        an_abort_armed = 0;
        strcpy(an_error_text, "data directory path is too long");
        return AN_ERR_ARG;
    }

    create_needed_dirs();

    /*
     * `main.c:521-524` の写し。`cmd_get_hook` はゲームループ（`ui-game.c:942`）が
     * 読む——立てないと `CTX_GAME` の命令要求で NULL 関数ポインタを呼ぶ。
     */
    cmd_get_hook = textui_get_cmd;

    /*
     * `init_display()` が `EVENT_ENTER_BIRTH` などの UI 側イベントハンドラを登録する
     * （`ui-display.c:2887-2898`。`ui_init_birthstate_handlers()` を含む）。
     * これを呼ばないと `setup_menus()` が走らず、birth のメニュー（`race_menu` ほか）の
     * `menu_data` が NULL のままで `menu_question()` が落ちる（S1 で実際に踏んだ穴）。
     * `textui_init()` は入力まわりの下ごしらえ（`ui-init.c:44`）。
     */
    init_display();
    (void)init_angband();
    /*
     * AH-02 の直し（クラッシュの修正）。`an_recent_message_count()`／
     * `an_get_recent_message()`（`an_map_shim.c`）はこれが立つまでコアの
     * `messages_num()` を呼ばない。`init_angband()` の中で `messages_module.init`
     * （`messages_init()`）が動くのはここまでに済んでいる。
     */
    an_mark_messages_ready();
    textui_init();

    /* `EVENT_MESSAGE` の受け手（設計 §6.2）。Term からではなくここで積む。 */
    an_install_message_hook();

    /*
     * AH-31: `EVENT_BOLT`/`EVENT_EXPLOSION`/`EVENT_MISSILE` の受け手をもう 1 組足す
     * （`an_fx.c`。`init_display()` が既に登録した原作の `display_bolt` ほかとは
     * 競合しない——`event_add_handler()` は同じ種類へ何本でも積める）。
     */
    an_fx_install_handlers();
    an_store_session_install_handlers(); /* 調-05: 店の画面が開いている間の旗 */

    /*
     * `get_check_hook` 等を包む（設計 §6.3）。`textui_init()` が既定の実装
     * （`textui_get_check` ほか）を差した直後でなければ、包む元が無い。
     */
    an_install_ui_hooks();

    /*
     * `get_aim_dir_hook` を新 UI の目標選択へ寄せる（設計 §8.5 T1）。同じく
     * `an_install_ui_hooks()` の直後——`textui_init()` が既定実装
     * （`textui_get_aim_dir`）を差した後でなければ包む元が無い。
     */
    an_install_targeting_hooks();

    an_abort_armed = 0;
    return AN_OK;
}

/*!
 * @name 自動で開き直す（`--resume`。言語切り替えの立て直し。 の修-60）
 *
 * @details 言語は `an_bootstrap()` より前に決まっていなければならないので、遊んでいる途中で
 * 替えるには**コアを終了させて起こし直す**しかない。起こし直したあとにセーブを選び直させるのは
 * 筋が悪いので、**終わるときに印を書き、起きるときに読む**（Sil-Q・Frox・幻想蛮怒と同じ作り）。
 *
 * 印は `<user>/.resume` の 1 行（セーブファイルの綴り）。**セーブの置き場には置かない**——
 * 原作の `got_savefile()` は点で始まるファイルも数えるので、`save/` に置くとセーブ選択の一覧に
 * 出てしまう。`user/` は一覧の対象外である。読んだら消す：残しておくと、次に `--resume` で
 * 起きたとき別の人物が勝手に開く。
 *
 * 開き方は `play_game(GAME_LOAD)`（`savefile` を据えて直に読む。原作の `main.c` が `-u` 付きで
 * 起きたときと同じ）。タイトル（`select_savefile()`）は出さない。
 * @{
 */

//! 印の綴り。
#define AN_RESUME_FILE ".resume"

//! `--resume` が渡されたか。`an_set_resume()` が立て、`an_run_game()` が使って倒す。
static int an_resume_armed = 0;

/*! @brief 印の道を組む。`ANGBAND_DIR_USER` が未設定なら 0 を返す。 */
static int an_resume_path(char *buf, size_t max)
{
    if (!ANGBAND_DIR_USER || !ANGBAND_DIR_USER[0]) {
        return 0;
    }
    path_build(buf, max, ANGBAND_DIR_USER, AN_RESUME_FILE);
    return 1;
}

/*! @brief 印を消す。無ければ何もしない。 */
static void an_resume_clear(void)
{
    char path[1024];

    if (an_resume_path(path, sizeof(path))) {
        (void)remove(path);
    }
}

/*!
 * @brief 印を書く（保存に成功した直後だけ）。
 * @details 書けなくても遊びは止めない——**次の立て直しでセーブ選択が出るだけ**である。
 * `savefile` は `<save>/<綴り>` の完全なパスなので、末尾（区切りの後ろ）だけを記す。
 */
static void an_resume_write(void)
{
    char path[1024];
    const char *leaf = savefile;
    const char *p;
    FILE *fp;

    for (p = savefile; *p; p++) {
        if ((*p == '\\') || (*p == '/')) {
            leaf = p + 1;
        }
    }
    if (!leaf[0] || !an_resume_path(path, sizeof(path))) {
        return;
    }
    fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "[angband] could not write the resume mark: %s\n", path);
        fflush(stderr);
        return;
    }
    fprintf(fp, "%s\n", leaf);
    fclose(fp);
    fprintf(stderr, "[angband] resume mark -> %s\n", leaf);
    fflush(stderr);
}

/*!
 * @brief 印を読んで消す。
 * @param name 綴りの受け皿
 * @param max 受け皿の大きさ
 * @return 1 = 読めて、その名前のセーブが実在する／0 = 印が無い・読めない・セーブが無い
 */
static int an_resume_take(char *name, size_t max)
{
    char path[1024];
    char save_path[1024];
    FILE *fp;
    size_t len;

    if (!name || (max == 0)) {
        return 0;
    }
    name[0] = '\0';
    if (!an_resume_path(path, sizeof(path))) {
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
    while ((len > 0) && ((name[len - 1] == '\n') || (name[len - 1] == '\r') || (name[len - 1] == ' '))) {
        name[--len] = '\0';
    }
    /* 空・点で始まる・道の区切りや `:` を含む綴りは取らない（セーブの置き場の直下の名前だけ）。 */
    if ((len == 0) || (name[0] == '.') || (strpbrk(name, "\\/:") != NULL)) {
        fprintf(stderr, "[angband] the resume mark is not a save file name: %s\n", name);
        fflush(stderr);
        return 0;
    }

    /* **実在を確かめる。**無い綴りで `GAME_LOAD` へ入ると新規作成が始まる。 */
    path_build(save_path, sizeof(save_path), ANGBAND_DIR_SAVE, name);
    fp = fopen(save_path, "rb");
    if (!fp) {
        fprintf(stderr, "[angband] resume mark names a save that is not there: %s\n", name);
        fflush(stderr);
        return 0;
    }
    fclose(fp);
    return 1;
}

void an_set_resume(int on)
{
    an_resume_armed = on ? 1 : 0;
}

//! `--resume`（`GAME_LOAD`）で入ったときだけ 1。`an_reinit_hook()` が名前を捨てるかの印。
static int an_reinit_clear_names = 0;

/*!
 * @brief `play_game()` が再初期化したあと（`reinit_hook`。死亡後の「新しいゲーム」）に呼ばれる。
 * 常に差す（`an_run_game()`）。
 * @details (1) **`character_generated` を偽に戻す**（調-04・F-angband-07）。原作は死んだ人物の
 * `character_generated` を真のまま `cleanup_angband()`→`init_angband()` を回し、新しい人物を作り始める
 * `start_game()` の中でようやく偽にする。その間（`init_angband()` が作り直した、まだ種族も無い空の
 * `player` のまま、`textui_init()`・セーブ選択の画面を出す途中）に Term が描かれて画面を組むと、
 * 当方のフレームの組み立て（`an_read_status_effects()` ほか。`character_generated && player` だけを
 * 見る）が空の `player` の `race` を読んで落ちていた（`player_flags()` の `p->race->flags`）。
 * (2) `--resume` で入ったときは、タイトル（`select_savefile()`）から入った場合に原作が行う
 * 「新しいゲームが既存のセーブを上書きしないよう `savefile` と `arg_name` を空にする」（`ui-game.c`）
 * 掃除が走らないので、ここで同じことをする。
 */
static void an_reinit_hook(void)
{
    character_generated = false;
    /*
     * (3) 原作の `cleanup_angband()` は `event_remove_all_handlers()` で全部の受け手を外す
     * （`init.c`）。`init_display()` が原作の受け手は付け直すが、当方が足した 3 組
     * （`EVENT_MESSAGE`・`EVENT_MISSILE` ほか・店の旗）は 2 回目のゲームでは外れたままになる
     * ——店の画が店と数えられず、メッセージも積まれない。ここ（`init_display()` の後）で付け直す。
     * 1 回目のゲームではこの関数は呼ばれず、`an_run_game()` 側の登録だけなので二重にならない。
     * 入力のフック（`get_check_hook` ほか）は別の仕組みで、`textui_init()` が `play_again` のとき
     * `textui_input_init()` を呼び直さない（`ui-init.c`）ので、包みは 2 回目のゲームでも残る。
     */
    an_perm_reset(); /* 常時の効果の表は、解放された古い項目を指したままにしない（読み直した項目で引き直す） */
    an_install_message_hook();
    an_fx_install_handlers();
    an_store_session_install_handlers();
    /* 毎ターンの画面の送り出し（`EVENT_REFRESH`）の受け手も同じ理由で外れる。コアの `refresh()` は
     * `ui_enter_world()` が町に入るときに差すので、先に付ければ呼ぶ順（コアが先・こちらが後）は変わらない。 */
    an_term_reinstall_refresh_handler();
    if (an_reinit_clear_names) {
        savefile[0] = '\0';
        arg_name[0] = '\0';
    }
}

/*!
 * @brief 印が在るならそれを開く準備をする。
 * @return 1 = 開く（`savefile` を据えた）／0 = ふつうにセーブ選択を出す
 * @details **一度きり**（`an_run_game()` から 1 回だけ呼ばれる）。印は `--resume` が無いときも
 * 消す（古い印を次の立て直しへ持ち越さない）。
 */
static int an_take_resume(void)
{
    char name[256];

    if (!an_resume_armed) {
        an_resume_clear();
        return 0;
    }
    an_resume_armed = 0;

    if (!an_resume_take(name, sizeof(name))) {
        fprintf(stderr, "[angband] --resume found nothing to open; showing the savefile menu\n");
        fflush(stderr);
        return 0;
    }
    path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, name);
    an_reinit_clear_names = 1;
    fprintf(stderr, "[angband] --resume opened %s\n", name);
    fflush(stderr);
    return 1;
}
/*! @} */

/*!
 * @name 誕生の最初の画面（種族）の取消でセーブ選択（タイトル）へ戻る（修-61。決めたこと 2026-10-12）
 *
 * @details 原作の誕生の流れ（`ui-birth.c` の `textui_do_birth()`）は、最初の画面（種族）で ESC を
 * 押しても同じ画面を描き直すだけで（`BIRTH_BACK` → `BIRTH_RESET` → `BIRTH_RACE_CHOICE`）、
 * セーブ選択へ戻る道を持たない。抜けられるのは `quit()` だけ（それはコアの終了になる）。
 * そこでアダプタが、その ESC を入力の段（`an_null_term.c` の `an_drain_keys_into_term()`）で拾い、
 * `play_game()` ごと `longjmp` で抜けて `play_game(GAME_SELECT)` をもう一度呼ぶ。
 *
 * 抜けたあとの掃除は最小限にする：`player_init()`（人物を初期の状態へ。`ht_birth` も 0 に戻るので、
 * 次の誕生が「前の人物を引き継ぐか」を訊かない）、`cmdq_flush()`、端末の消去、積み残しの入力の破棄。
 * `cleanup_angband()`→`init_angband()` の再初期化は**しない**（原作の `play_again` の経路。
 * `event_remove_all_handlers()` が当方の差し込みの受け手まで外すため。誕生の途中では地図もまだ無く、
 * 作り直す必要が無い）。誕生の画面の種族・職業のメニュー（`setup_menus()` が確保）は、
 * 次の誕生のときに確保し直すので少しだけ漏れる（`free_birth_menus()` を呼ぶ `EVENT_LEAVE_BIRTH` は
 * `savefile` の名前を決めてしまうので送らない）。
 * @{
 */

//! 取消で戻る先（`an_run_game()` の中）。
static jmp_buf an_cancel_buf;
static int an_cancel_armed = 0;

/*!
 * @brief いま誕生の最初の画面（種族のメニュー）が出ているか。
 * @details 種族のメニューの箱は `ui-birth.c` の `race_region`（桁 `RACE_COL`=2・行 `TABLE_ROW`=9・
 * 幅 17）。職業のメニューは桁 19 か、種族と並べて描いた最初の 1 フレームだけの幅 34（桁 2）、
 * 能力値の決め方は桁 36 なので、桁 2・幅 17 で種族だけが当たる。誕生の前（セーブ選択）は
 * `character_generated` が偽でも箱が違う。
 * 誕生の最初の画面では、起動時に積まれた 1 段（`ui-init.c` の `screen_save_depth++`）があるので、
 * 深さが 1 を超えるとき（右クリックの文脈メニューなど、さらに `screen_save()` した画面）は除く。
 */
static int an_at_first_birth_screen(void)
{
    int row = 0;
    int col = 0;
    int width = 0;
    int page_rows = 0;

    if (!an_cancel_armed || character_generated || (screen_save_depth > 1)) {
        return 0;
    }
    if (!an_menu_active_region(&row, &col, &width, &page_rows)) {
        return 0;
    }
    return ((row == 9) && (col == 2) && (width == 17)) ? 1 : 0;
}

void an_birth_cancel_if_first_screen(void)
{
    if (!an_at_first_birth_screen()) {
        return;
    }
    fprintf(stderr, "[angband] birth: ESC on the first screen; back to the savefile menu\n");
    fflush(stderr);
    an_cancel_armed = 0;
    longjmp(an_cancel_buf, 1);
}

/*! @brief 取消で抜けてきたあと、`play_game(GAME_SELECT)` を呼び直す前の掃除。 */
static void an_birth_cancel_cleanup(void)
{
    cmdq_flush();
    player_init(player);
    character_dungeon = false;
    msg_flag = false;
    an_menu_discard_stale();
    (void)Term_clear();
    (void)Term_flush();
    savefile[0] = '\0';
    arg_name[0] = '\0';
}
/*! @} */

int an_run_game(void)
{
    an_game_kind = 0;
    an_shutting_down = 0;

    if (setjmp(an_game_buf) != 0) {
        an_game_armed = 0;
        an_game_in_progress = 0;
        an_cancel_armed = 0;
        return an_game_kind ? an_game_kind : AN_OK;
    }

    an_game_armed = 1;
    an_game_in_progress = 1;

    /*
     * `select_savefile()`（`ui-game.c:770`）がタイトル代わりにセーブ選択を描き、
     * 死亡後の「もう 1 回」も `play_game()` の中で回る（設計 §5.3・§5.6）。
     * 戻ってくるのは `quit()` が `an_game_buf` へ longjmp したときだけ。
     *
     * 言語切り替えの立て直し（`--resume`。`an_set_resume()`）のときだけ、セーブ選択を出さずに
     * 直前に保存したセーブを開く。
     *
     * 誕生の最初の画面で取消されたら（`an_birth_cancel_if_first_screen()`）、ここへ `longjmp` で
     * 戻ってきて、セーブ選択からやり直す（修-61）。`mode` は `longjmp` をまたぐので static。
     */
    {
        static int mode;

        mode = an_take_resume() ? GAME_LOAD : GAME_SELECT;
        reinit_hook = an_reinit_hook;
        for (;;) {
            if (setjmp(an_cancel_buf) == 0) {
                an_cancel_armed = 1;
                play_game((enum game_mode_type)mode);
                break;
            }
            an_birth_cancel_cleanup();
            mode = GAME_SELECT;
        }
        an_cancel_armed = 0;
    }

    /* 戻らない想定だが、万一戻ったら正常終了として扱う（設計外の経路への備え）。 */
    an_game_armed = 0;
    an_game_in_progress = 0;
    return AN_OK;
}

/*!
 * @brief ホストからの終了要求。設計 §5.5（1.1 版で訂正）。
 * @param forced 0 = 穏やか（`quit_request`。拒める）／1 = 強制（EOF。拒めない）
 *
 * `main-win.c:4535-4546` は `close_game(true)` を呼ぶが、**`close_game()` は
 * `Term->mapped_flag` が真だと「Press Return」を出して `inkey()` で待つ**
 * （`ui-game.c:1145-1150`）ので、そのまま写すと `exit` が送られないままコアが
 * 固まる（S1 の見直しで確認）。ここでは `close_game()` を呼ばず、`save_game()`
 * だけで `quit(NULL)` へ進む。
 */
void an_shutdown_and_quit(int forced)
{
    if (an_shutting_down) {
        return; /* 再入よけ（save_game() の途中で Term_fresh が挟まりうる）。 */
    }

    if (inkey_flag) {
        /* コマンド待ち。穏やか・強制のどちらでも保存して終了させる。 */
        an_shutting_down = 1;
        fprintf(stderr, "[angband] shutdown requested: saving and quitting\n");
        fflush(stderr);
        msg_flag = false;
        /* 保存に成功し、遊べる人物が居るなら、起こし直しのときの続きの印を残す（`--resume`）。 */
        if (save_game_checked() && character_generated) {
            an_resume_write();
        }
        quit(NULL);
        /* 戻らない（an_quit_aux が an_game_buf へ longjmp する）。 */
    }

    if (!forced) {
        /*
         * 穏やかな要求（メニューや入力の途中）は拒む。**ホスト側が要求 1 回に
         * つき 1 回だけこの関数を呼ぶ**ので、ここでは旗を立てず、そのまま
         * 戻ってよい（設計 §5.5。以前は毎回この関数が呼ばれ、10ms ごとに
         * この plog が出続けていた——S1 の見直しで実測。stdin を閉じて 6 秒で
         * 694 行）。
         */
        plog("Please exit any open menus before closing the game.");
        /*
         * 見直しの指摘 12：`plog()` は stderr だけなので画面側に届かない。
         * `EVENT_MESSAGE` を経ずに同じ文面をメッセージキューへ直接積む。
         */
        an_push_synthetic_message("Please exit any open menus before closing the game.", 4 /* TERM_RED */);
        return;
    }

    /*
     * 強制（EOF）でコマンド待ちでもない。拒めないので、原作のパニックセーブ
     * （`savefile_get_panic_name()` の置き場）へ書いてから終了させる。
     * 次の起動で `start_game()` が「A panic save exists. Use it?」と尋ねる
     * （`ui-game.c:702`）ので、原作が用意した回復の経路に乗る。
     */
    an_shutting_down = 1;
    fprintf(stderr, "[angband] forced shutdown (ui gone): writing a panic save\n");
    fflush(stderr);
    if (character_generated) {
        char panic_path[1024];
        savefile_get_panic_name(panic_path, sizeof(panic_path), savefile);
        if (savefile_save(panic_path)) {
            fprintf(stderr, "[angband] panic save written: %s\n", panic_path);
        } else {
            fprintf(stderr, "[angband] panic save failed: %s\n", panic_path);
        }
        fflush(stderr);
    }
    quit(NULL);
    /* 戻らない（an_quit_aux が an_game_buf へ longjmp する）。 */
}

int an_awaiting_command(void)
{
    return (inkey_flag && (screen_save_depth == 0)) ? 1 : 0;
}

int an_menu_open(void)
{
    return ((screen_save_depth > 0) || !character_generated || play_again) ? 1 : 0;
}

int an_character_generated(void)
{
    return (character_generated && !play_again) ? 1 : 0; /* `play_again`：死亡後の再初期化の間は人物が居ない（調-04） */
}
