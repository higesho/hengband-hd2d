/*!
 * @file an_shim.h
 * @brief Angband 4.2 コア（`AngbandCore.exe`）の純 C ABI。C++ 側（`an_main.cpp`）はこれだけを見る。
 *
 * 手本は `silq/adapter/sq_shim.h`。
 *
 * ## なぜ分けるか
 * `an_bootstrap.c` / `an_null_term.c` はコアと同じ C の翻訳単位（`angband.h` を include し、
 * `setjmp`/`longjmp` でコアの `quit()` を受ける）。`an_main.cpp` は C++ で、
 * `presentation/` や `std::string` を使う。両者の境目をこの 1 枚の C ABI に閉じる
 * ——`angband.h` を C++ 側へ漏らさない（Sil-Q と同じ規約。設計 §3 の表）。
 *
 * ## この ABI が持つもの
 * 握手・キー・Term の写し・終了（S1）に加え、A1/A2（`an_menu_row_drawn` ほか）の
 * 記録（見直しの指摘 11: 以前は「ここでは記録するだけ——選択肢の構造化は S3」と
 * 書いてあったが、S3 のいま `an_main.cpp` 側で実際に `menu_choices` へ構造化して
 * いるので、この ABI が生の記録を渡す役目であることに変わりはない）。マップ・
 * 実体・HUD（`GameFrame` の中身）は `an_frame.cpp` が `an_map_shim.h` を通して担当する。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*! @name 戻り値 */
/*! @{ */
#define AN_OK 0
#define AN_ERR_ARG 1
#define AN_ERR_LIB 2
#define AN_ERR_STATE 3
#define AN_ERR_TERM 4
#define AN_ERR_QUIT 5 /*!< 起動中に quit() を踏んだ（失敗） */
#define AN_ERR_CORE 6 /*!< core() を踏んだ（続行不能） */
/*! @} */

/* ==================================================================== Term（an_null_term.c） */

/*!
 * @brief ヘッドレスの主 Term を 1 枚立てる。設計 §6.1 の 80x27 固定。
 * @return AN_OK、または AN_ERR_ARG（寸法）/ AN_ERR_STATE（二重）/ AN_ERR_TERM（term_init 失敗）
 */
int an_term_install(int cols, int rows);
void an_term_remove(void);
void an_term_reinstall_refresh_handler(void); /* 2 回目のゲームで EVENT_REFRESH の受け手を付け直す */
int an_term_size(int *cols, int *rows);

/*!
 * @brief `y` 行目を UTF-8 へ変換して `out` へ書く（末尾 NUL 込み。末尾の空白は詰める）。
 * @param out 受け皿。`cap` バイト
 * @return 書いた**バイト数**（UTF-8。`an_null_term.c` の `an_term_row_range_impl()` の
 * 戻り値をそのまま返す）。負なら AN_ERR_*
 * @details 見直しの指摘 11: 以前の註記「wchar_t 単位」は誤り——実体
 * （`an_term_row_range_impl()`）は `an_append_utf8()` が積んだ**バイト数**（`pos`）を
 * そのまま返しており、桁数（wchar_t の個数）ではない。サロゲート対は 1 符号位置へ
 * 組んでから UTF-8 へ直すので、全角 1 字が複数バイトになりうる（日本語の扱いは
 * ）。
 */
int an_term_row(int y, char *out, int cap);

/*!
 * @brief `an_term_row()` と同じだが、桁 `[col_start, col_start+col_count)` だけを渡す。
 * @details 左の状態列（`COL_MAP` 桁）や最下段の帯を Term から切り出すのに使う
 * （設計 §6.2）。末尾の空白は詰めない——桁の位置を保つ側の都合を Term 側で
 * 決め打ちしないため（呼び手が要れば詰める）。
 */
int an_term_row_cols(int y, int col_start, int col_count, char *out, int cap);

/*!
 * @brief 行 `y` の桁 `[col_start, col_end)` が `an_term_row_cols()` の字で何バイトになるか。
 * @details 日本語。Term の**桁**を `text_utf8` の
 * **バイト位置**へ直すのに使う（全角は 3 バイト・2 桁、埋め字は 0 バイト・1 桁）。
 * 英語（ASCII）では `col_end - col_start` と同じ値。
 */
int an_term_cols_to_bytes(int y, int col_start, int col_end);

/*!
 * @brief `y` 行目の色（0〜15 に丸めたもの）を `out` へ書く。
 * @details 16〜27（`z-color.h` の拡張色）は `an_null_term.c` の 12 行対応表
 * （設計 §4-1 の案 a。RGB 距離が最も近い 0〜15 を機械的に選んだ）で丸める。
 */
int an_term_colors(int y, unsigned char *out, int cap);

int an_term_cursor(int *x, int *y, int *visible);

/*!
 * @brief 色番号（0〜27）を0〜15へ丸める（`an_null_term.c` の対応表）。
 * @details メッセージの色（`an_map_shim.c`）や実体の色（`an_frame.cpp`）など、
 * Term 以外の経路で色番号を渡す先すべてがこれを通す——`& 0x0F` で切ると
 * 16〜27（拡張色）が誤った色へ化ける（見直しの指摘 13）。
 */
unsigned char an_round_color(int a);

/*!
 * @brief 変化検出用の安い digest（手本は `silq/adapter/sq_null_term.c` の同名関数）。
 * @details `an_term_event()` は入力待ちの間も `AN_IDLE_SLICE_MS` ごとに `present()` を
 * 呼ぶ。フレームを毎回組んで JSON へ直列化してから前回と比べるのは高くつく
 * （見直しの指摘 6。実測 CPU 約28%）。ここで安く「何か変わったか」を先に見て、
 * 変わっていなければ `build_frame()` 自体を丸ごと省く。
 * 画面に出るものが変われば必ず動く材料（Term の画・`turn`・メッセージ数・
 * 人物の有無・階の有無・命令待ち・メニューの開閉）を混ぜる。
 */
unsigned long long an_change_digest(void);

/*!
 * @brief `text_mbcs_hook` ほか 4 つの UTF-8 変換フックを差す（設計 §6.1。`an_text_hooks.c`）。
 * @details `an_bootstrap()` の中、`init_angband()` より前に呼ぶこと
 * ——ゲームデータの非 ASCII な名前（`Sméagol` ほか）はそこで読まれる。
 */
void an_install_text_hooks(void);

/* ==================================================================== ホストのフック */

typedef struct an_host_hooks {
    void (*present)(void); /*!< 画が確定した（TERM_XTRA_FRESH）。frame の送り出しどころ */
    int (*next_key)(void); /*!< 次のキー（0 = 無い）。ASCII の 1 バイトを int で返す */
    void (*drop_keys)(void); /*!< 受信キューを捨てる（TERM_XTRA_FLUSH） */
    void (*sleep_ms)(int ms);
    /*!
     * @brief 終了要求の種類。**要求 1 回につき 1 回だけ真を返す**（消費したら旗を下ろす）。
     * @return 0 = 無い／1 = 穏やかな要求（`quit_request`。`inkey_flag` が偽なら拒んでよい）／
     *   2 = 強制（stdin の EOF。プロトコル v1 §9.2。拒めない）
     * @details 1 を毎回返し続けると `an_shutdown_and_quit()` が同じ理由を 10ms ごとに
     *   plog し続ける（S1 の見直しで実測。stdin を閉じて 6 秒で 694 行）。ホスト側
     *   （`an_main.cpp`）が `quit_request` の旗を読み出した時点で下ろすこと。
     *   2（EOF）はコアが確実に終わるので、下ろさなくても実害は無い。
     */
    int (*shutdown_requested)(void);
} an_host_hooks;

void an_set_host_hooks(const an_host_hooks *hooks);

/*!
 * @name 地図のクリック（AH-57。プロトコルの `input_event` の `e == "map_click"`）
 * @details 画面が送る `map_click`（ダンジョンのマスの座標・ボタン・修飾キー）を、`next_key()` が返す
 * **キーの列の中へ 1 個の整数として混ぜる**ための符号。キー（1〜255）と順序を保つため、
 * 別の待ち行列は作らない。`AN_KEY_MAP_CLICK`（ビット 30）が立っていればクリックで、
 * 下位ビットに次の欄を詰める（`an_null_term.c` の `an_map_click_apply()` が解く）。
 *
 * | ビット | 中身 |
 * |---|---|
 * | 0〜7 | マスの x（0〜255。**画面の座標ではなくダンジョンのマス**） |
 * | 8〜15 | マスの y |
 * | 16〜17 | ボタン（1 = 左、2 = 右。原作の `mouseclick.button` と同じ番号） |
 * | 18〜20 | 修飾キー（`AN_MAP_CLICK_SHIFT` / `_CTRL` / `_ALT` の和） |
 * @{
 */
#define AN_KEY_MAP_CLICK 0x40000000
#define AN_MAP_CLICK_SHIFT 1
#define AN_MAP_CLICK_CTRL 2
#define AN_MAP_CLICK_ALT 4
#define AN_MAP_CLICK_PACK(grid_x, grid_y, button, mods) \
    (AN_KEY_MAP_CLICK | ((mods) << 18) | ((button) << 16) | ((grid_y) << 8) | (grid_x))
/*! @} */

/* ==================================================================== 起動と進行（an_bootstrap.c） */

/*!
 * @brief `init_file_paths()` → `create_needed_dirs()` → `init_angband()`。
 * @param lib_dir 実行時データの置き場（`<exe_dir>/angband/lib`。設計 §2）
 * @param data_dir セーブ・遊ぶ人設定の置き場（`portable::data_dir/angband`）
 * @return AN_OK、または AN_ERR_*（`an_last_error()` に理由）
 */
int an_bootstrap(const char *lib_dir, const char *data_dir);

/*!
 * @brief 次に `an_run_game()` を回すとき、**直前に保存した人物を自動で開く**（`--resume`）。
 * @param on 1 = 開く／0 = 開かない（既定）
 * @details 画面は言語を替えるとき、このコアを `quit_request` で終わらせて `--resume` 付きで起こし直す
 * （`hd2d_app.cpp` の (d2)）。`an_shutdown_and_quit()` が保存に成功したときだけ
 * `<user>/.resume` に印を書き、`an_run_game()` が印を読んで消してから `play_game(GAME_LOAD)` で
 * 開く（セーブ選択を出さない）。印が無い・セーブが無いときは、ふつうにセーブ選択を出す。
 * 立て直しでない起動では、残っている印を消す（持ち越さない）。`an_run_game()` より前に呼ぶこと。
 */
void an_set_resume(int on);

/*!
 * @brief 誕生の最初の画面（種族）で ESC を読んだとき、セーブ選択（タイトル）へ戻る（修-61）。
 * @details 入力の段（`an_null_term.c`）が ESC を Term へ積む前に呼ぶ。最初の画面でなければ何もせず
 * 戻る。最初の画面なら `play_game()` を `longjmp` で抜け、`an_run_game()` が `play_game(GAME_SELECT)`
 * を呼び直す（戻らない）。原作の誕生の流れは最初の画面の取消で同じ画面を描き直すだけで、
 * タイトルへ戻る道を持たない。
 */
void an_birth_cancel_if_first_screen(void);

/*!
 * @brief `play_game(GAME_SELECT)` を回す（`an_set_resume(1)` で印が読めたときは `GAME_LOAD`）。
 * **戻るのは遊び終えたとき**（設計 §5.3）。
 * @return AN_OK（正常終了）/ AN_ERR_CORE（`core()` を踏んだ）
 */
int an_run_game(void);

/*!
 * @brief ホストからの終了要求（画面の× ／ `quit_request` ／ stdin の EOF）。
 * @param forced 0 = 穏やかな要求（`inkey_flag` が偽なら拒める）／
 *   1 = 強制（EOF。拒めない。偽なら原作のパニックセーブへ保存する）
 * @details 設計 §5.5。`main-win.c:4535-4546` は `close_game(true)` を呼ぶが、
 * **`close_game()` は `Term->mapped_flag` が真だと「Press Return」を出して
 * `inkey()` で待つ**ので、そのまま写すとコアが終わらない（S1 の見直しで確認）。
 * ここでは `save_game()` だけを呼び、`close_game()` は呼ばない。
 * `inkey_flag` が真なら保存して終了させる——**戻らない**。偽なら
 * `forced` に応じて拒む（穏やか）か、パニックセーブへ保存して終了させる（強制）。
 */
void an_shutdown_and_quit(int forced);

const char *an_last_error(void);
const char *an_core_version(void); /*!< "4.2.6" */

/* ==================================================================== 画面が読む状態 */

/*! `inkey_flag` が真かつ `screen_save_depth == 0`（設計 §6.2）。 */
int an_awaiting_command(void);
/*! AH-23 の検査専用（`an_main.cpp` の実体を参照）。通常運用では誰も呼ばない。 */
unsigned long long an_debug_current_frame_id(void);
/*! `screen_save_depth > 0` または `character_generated` が偽（設計 §6.2）。 */
int an_menu_open(void);
int an_character_generated(void);

/*!
 * @brief A1/A2（`ui-menu.c` の差し込み）が実際に呼ばれた回数。
 * @details S1/S2 では選択肢の構造は使わない。**patch が当たって実行されていることの
 * 証拠**として `--selftest` が読む。
 */
int an_menu_hook_calls(void);

/*!
 * @brief 直近の `an_menu_begin()` で刻んだ世代番号。
 * @details S3 で選択肢（`menu_choices`）を構造化するとき、行の記録にこの値を刻んでおき、
 * 読み出す時点の世代と比べて食い違えば「別のメニューの、もう閉じた記録」として捨てる
 * ——`an_menu_end()` の後に `screen_save_depth` が変わらないまま次のメニューが
 * すぐ開く場面があり、`screen_save_depth` だけでは区別が付かないため
 * （設計 §4 の見直し。この関数自体は S1 では読まれない）。
 */
int an_menu_epoch(void);

/*!
 * @brief 直近のメニュー 1 枚ぶんの行の記録（設計 §6.3「選択肢」）。
 * @details `an_menu_begin()` のたびに空になる。`row`/`col`/`width` は Term の桁・行
 * （`menu_term_lines` の `source_row` と突き合わせて `line_index` を引く。設計 §6.3 の
 * 「`line_index` は `menu_term_lines` の添字（空行を詰めているので Term 行と一致しない）」）。
 * `sel` は `get_tag()` / `selections[pos]` の字（0 なら「鍵が無い。カーソルで選ぶしかない」）。
 */
typedef struct an_menu_choice_row {
    int row;
    int col;
    int width;
    int cursor; /*!< そのメニューでいま選ばれている行なら 1 */
    char sel;
    int available; /*!< 見直しの指摘 11。原作 `valid_row()` が MN_ROW_VALID を返したら 1
                     * （`valid_row` を持たないメニューは常に 1）。0 なら選べない行 */
    /*!
     * AH-11: `an_menu_row_drawn` の時点
     * （`row_funcs->display_row()` の直後）で、その行の内容を `snapshot_len`
     * バイトだけ写し取ったもの。`sel` の無い行（カーソルだけで選ぶ一覧。知識
     * 画面など）は `discard_stale_menu_if_needed()`（`an_main.cpp`）が
     * 「選ぶ字で判じられない」ために常に「残す」側へ倒れていた——この写しで
     * 実際の Term の中身と突き合わせれば `sel==0` の行でも判じられる。
     * `snapshot_len<=0` なら写せなかった（列が無い等）。
     */
    char snapshot[32];
    int snapshot_len;
} an_menu_choice_row;

int an_menu_choice_count(void);
int an_menu_choice_at(int index, an_menu_choice_row *out);

/*!
 * @brief 直近の `an_menu_begin()` が記録した、いま開いているメニューの
 * **実際の枠**（`struct menu` の `active`。`row`/`col`/`width`/`page_rows`）。
 * @details 見直しで見つけた 2 つの不具合の直し（設計 §6.3。どちらも「詳細欄に
 * 元のTerm行の残りが混ざる」）:
 * - 持ち物一覧などの**行**の箱の外（地図・状態列の残り）が詳細欄に流れ込む。
 * - 店の「Command for X」のような**小さな浮動メニュー**は、`region_erase_bordered()`
 *   で自分の枠 + 1 桁だけを消してから描く——枠の**右**（もっと横に長い、前の
 *   店の一覧の値段など）は消えずに残ったまま。行だけでなく桁の右端も知らないと
 *   混入を防げない。
 *
 * `menu->active` は `region_calculate()` を経ているので、`struct menu` を
 * ゼロ初期化のまま使う全面表示のメニュー（`menu_layout()` を呼ばない場合）でも
 * 画面いっぱいの矩形へ解決される——「切らない」と「切る」を同じ経路で扱える
 * （呼び手が別に「箱が無い」場合分けをしなくてよい）。
 * @return 1（枠が有効。`width`/`page_rows` とも正）、または 0（メニューが無い・
 *   `an_menu_begin()` が一度も呼ばれていない）
 */
int an_menu_active_region(int *row, int *col, int *width, int *page_rows);

/*!
 * @brief A1/A2 の記録（`an_menu_choice_count()`/`an_menu_choice_at()`/
 * `an_menu_active_region()` が読むもの）を空にする。
 * @details 「応答待ちです」で止まる不具合の見直し（2 回目の再発）で見つけた
 * 根本原因の直し。A1/A2 は `struct menu` を使う画面（`ui-menu.c` の
 * `display_menu_row()`/`menu_refresh()`）でしか呼ばれない——原作の birth の
 * 能力値割り振り画面（`ui-birth.c` の `point_based_command()`。`struct menu` を
 * 使わず `Term_gotoxy()` + 生の `inkey_ex()` で直接描く）のような画面へ移ると、
 * `an_menu_begin()` が一度も呼ばれないまま**前の画面の記録が残り続ける**。
 * `an_main.cpp` 側（`an_menu_row_drawn()`/`an_menu_active_region()` の読み出し
 * 元）が「いま Term に出ている文字と、記録した選ぶ字が 1 つも一致しない」と
 * 判じたときにこれを呼び、次のフレームから記録を「無い」ものとして扱えるようにする。
 */
void an_menu_discard_stale(void);

/*!
 * @brief 直近の `an_menu_begin()` が積んだ `menu->cmd_keys`（`ui-menu.c` の
 * `menu_select()` が一覧の行より先に振り分ける、一覧の行を経由しないコマンド鍵。
 * 店の `d`/`g`/`?`/`=` ほか）。
 * @details 一覧 0 件の店（我が家の直し。決めたこと）を新 UI から操作可能に
 * するための足がかり。`struct menu` を使わない画面、または `an_menu_begin()` が
 * 一度も呼ばれていなければ `NULL`。文字列は静的（`store_menu_set_selections()`
 * が代入する文字列リテラル）なので、返った先で作り直す必要はない。
 * @return `cmd_keys` へのポインタ、または `NULL`。
 */
const char *an_menu_cmd_keys(void);

/*!
 * @brief AH-09/AH-06: いま開いているメニューの `menu->switch_keys`。
 * @details `cmd_keys` と同じ経路（`menu_select()`）で振り分けられる、行に紐付かない
 * 追加キーのもう 1 系統——`item_menu()`（`ui-object.c`）の `m->switch_keys = "/|-";`
 * （持ち物・装備・矢筒・床のあいだを切り替える）がこちら側にしか無い。`cmd_keys`
 * と同じく静的文字列リテラルなのでポインタだけ持ち回る。`struct menu` を使わない
 * 画面、または `an_menu_begin()` が一度も呼ばれていなければ `NULL`。
 * @return `switch_keys` へのポインタ、または `NULL`。
 */
const char *an_menu_switch_keys(void);

/*!
 * @brief 直近の `an_menu_begin()` の時点で、プレイヤーが店のマスに立っているか。
 * @return 0（店ではない）／1（我が家）／2（我が家以外の店）。
 */
int an_menu_store_kind(void);

/*!
 * @brief 店の画面（原作の `use_store()`）が実際に開いているか（1/0。調-05）。`an_bootstrap.c`。
 * @details 足元が店のマスというだけでは真にならない（店に入って出た直後は入口に立っている）。
 */
int an_store_session_active(void);

/*!
 * @brief AH-42: 店の一覧が原作のヘルプ（`?`）を出しているか（1/0）。`an_bootstrap.c`。
 */
int an_menu_store_help_shown(void);

/*!
 * @name AH-58: 地図の右クリックが開く文脈メニュー（`an_bootstrap.c`）
 * @details 右クリック（修飾なし）の `Term_mousepress()` を積んだら `an_menu_expect_click_context()`、
 * **次に始まる動的メニュー**（`ui-menu.c` の `menu_dynamic_select()`。そこへ差し込んだ
 * `an_menu_dynamic_begin()`/`an_menu_dynamic_end()`——宣言は `tools/angband/prepare_sources.py` の
 * 差し込みが `ui-menu.c` へ書く——が呼ばれる）を文脈メニュー
 * （`ui-context.c` の `context_menu_cave()`・`context_menu_player()`）として覚える。
 * `an_menu_is_click_context()` は、いま開いている動的メニューがその 1 枚のとき真（閉じると偽に戻る。
 * 閉じてすぐ続きの小メニューを開くもの——自分のマスのメニューの「その他」——は同じ扱いを続ける）。
 * 別の入力を読んだら `an_menu_forget_click_context_expectation()` で期待を捨てる（右クリックが
 * メニューを開かなかったとき、あとで開いた別のメニューを取り違えない）。
 * `an_main.cpp` はこれで `frame.menu_over_map` を立てる（地図の上に重なる小さな箱。立体を残す）。
 * @{
 */
void an_menu_expect_click_context(void);
void an_menu_forget_click_context_expectation(void);
int an_menu_is_click_context(void);
/*! @} */

/*!
 * @brief AH-13: `an_present()`（`an_null_term.c`）が呼ばれるたびに 1 進む目印。
 * @details 知識画面（`ui-knowledge.c` の `display_knowledge()`）は非活性側→
 * 活性側の順で同じループの中で 2 回 `menu_refresh()`（＝`an_menu_begin()`）を
 * 呼ぶ——その間に画面は 1 度も外へ出て（present されて）いない。`an_menu_begin()`
 * はこれを使って「前の枠を置き換えるべきか、まだ見せていない枠と合わせて
 * 1 つの箱にすべきか」を判じる（下の註記）。
 */
void an_menu_note_present(void);

/* ==================================================================== 実行時データの置き場（an_bootstrap.c） */

/*!
 * @brief AH-24: `an_textblock_shown()`（`ui-output.c` の `textui_textblock_show()`
 * が各ページの描画直後に呼ぶ、 節 4 A7）の受け手。
 * @param row 箱の先頭行（見出しがあれば見出しの行を含む）
 * @param col 箱の左端の桁
 * @param width 箱の横幅
 * @param rows_used 見出し（あれば 1 行）＋そのページの本文行＋フッタ 2 行の合計
 * @details 弱い既定は空（実体は `angband/adapter/an_bootstrap.c`）。この行の
 * 内容を写し取っておき、`an_textblock_active_region()` が「いまの Term と
 * まだ一致するか」を確かめる材料にする——一致しなくなったら（別の画面へ
 * 移った）この記録はもう使わない。
 */
void an_textblock_shown(int row, int col, int width, int rows_used);

/*!
 * @brief AH-24: `an_textblock_shown()` が記録した箱を読み出す。
 * @details `an_main.cpp` の `menu_active_box()` は、`an_menu_active_region()`
 * （A1/A2。`struct menu` を使う画面）が無効なときだけこちらを試す——
 * `textui_textblock_show()` は `struct menu` を使わない「生テキストの画面」
 * （`V`・アイテムの説明・知識画面・モンスターの図鑑・死亡画面・装備の比較・
 * 店の説明ほか）の唯一の実体なので、A1/A2 はここで一度も呼ばれない。
 * 記録した先頭行の写しがいまの Term の同じ位置とまだ一致するときだけ真を
 * 返す——一致しなくなっていれば（画面が別の物へ変わった）0 を返し、
 * 呼び手はこの記録を使わない（次の画面へ残骸が漏れるのを防ぐ）。
 * @return 1（箱が有効で、いまの Term ともまだ一致する）／0（無い、または
 *   もう一致しない）
 */
int an_textblock_active_region(int *row, int *col, int *width, int *rows_used);

/* ==================================================================== 戦闘の見せ場（AH-31。`an_fx.c`） */

/*!
 * @brief `an_fx.c` の環（`an_fx_ring[]`）の大きさ。`an_frame.cpp`（`fill_combat_fx()`）
 * が `an_fx_take()` へ渡す受け皿はこれと同じ大きさにすること——見直しの指摘。
 * 以前は受け皿が 64 で環（128）より小さく、1 フレームの間に 64 件を超えて
 * 溜まると `an_fx_take()` が古い側だけを渡して**新しい見せ場のほうを**
 * 取りこぼしていた（`an_fx_take()` は汲んだら環を空にする契約なので、
 * 溢れた分は復元できない）。
 */
#define AN_FX_MAX 128

/*!
 * @brief `CombatFxEvent`（`presentation/frame/combat_fx.h`）の C ABI 版。
 * @details `an_frame.cpp`（C++）は `angband.h` を見ないので、`kind`/`element` は
 * その enum と同じ並びの生の整数で運ぶ（0=HitPlayer/1=HitMonster/2=Bolt。
 * 属性は 0=Physical〜9=Psy。手本は `frox/adapter/fc_shim.h` の `fc_fx_event`）。
 */
typedef struct an_fx_event {
    int kind;
    int element;
    int y;
    int x;
    int src_y;
    int src_x;
    float intensity;
} an_fx_event;

/*!
 * @brief 溜まっている見せ場を汲んで `out` へ写す（汲んだら空になる。`an_fx.c`）。
 * @return 書いた件数
 */
int an_fx_take(an_fx_event *out, int cap);

/*!
 * @brief `EVENT_BOLT`/`EVENT_EXPLOSION`/`EVENT_MISSILE` の受け手を登録する。
 * @details `an_bootstrap()` が `init_angband()` の後に 1 度だけ呼ぶ
 * （`game_event_dispatch` は `event_add_handler` された全員を呼ぶので、
 * `init_display()` が既に登録した原作の `display_bolt` ほかとは競合しない
 * ——あちらはヘッドレス Term の中身を描くだけで、こちらは別に見せ場を汲む）。
 */
void an_fx_install_handlers(void);

/* ==================================================================== 魔法・投射物の効果（projections / missiles） */

/*!
 * @brief 範囲・経路の 1 マス。`presentation/frame/attack_fx.h` の `FxCell` と同じ内容（`dist` は中心からの距離）。
 */
typedef struct an_fx_cell {
    int y;
    int x;
    int dist;
} an_fx_cell;

/*!
 * @brief 投射 1 回を共通の記録の置き場（`presentation/bridge/named_attack_fx.h`）へ積む。実体は `an_frame.cpp`。
 * @details `origin`/`shape`/`flags` は `ProjectionFx` と同じ並びの整数（`an_fx.c` の `AN_FX_*`）。`attr` は
 * 画面側の登録表の名前（空なら汎用の見た目）。`an_fx.c` の `an_fx_note_projection()`（原作の `project()` に足した
 * 観測の呼び出し）が呼ぶ。
 */
void an_named_fx_projection(int origin, int shape, const char *attr, int rad, int src_y, int src_x, int impact_y,
    int impact_x, int flags, const an_fx_cell *path, int path_n, const an_fx_cell *area, int area_n);

/*!
 * @brief 射撃・投擲の 1 歩（原作の `EVENT_MISSILE` 1 回）。`first` が真なら新しい 1 発の始まり。
 * @details `kind` は `MissileKind`（0=矢 1=クロスボウの矢 2=スリングの弾 3=投擲）。`object_name`/`object_tval` は
 * 物の基本アイテム（`first_kind->name`/`tval`。地図のセルと同じ引き方で番号に直す）。`unaware` が真（名前を知らない
 * 薬・巻物など）なら番号は引かない。`brand` は**識別済みの**属性の名前（無ければ空）。
 */
void an_named_fx_missile_step(int first, int kind, const char *object_name, int object_tval, int unaware,
    const char *brand, int src_y, int src_x, int y, int x);

/*! @brief 開いている 1 発が相手に当たった（`player-attack.c` の `ranged_helper()` の観測の呼び出し）。 */
void an_named_fx_missile_hit(void);

/*!
 * @brief `an_bootstrap()` に渡された `lib_dir`（末尾に区切りを添えたもの。
 * 設計 §2 の `<exe_dir>/angband/lib`）。`an_town_map.c` が地図のファイルを
 * `ANGBAND_DIR_GAMEDATA` からの相対ではなく**ここ**から組むために持つ
 * ——`gamedata`（`copy_runtime_data()` が `prepare_sources.py` のたびに
 * `shutil.rmtree` してから原作の中身で埋め直す）と違い、`lib/hd2d/` は
 * 触られない。
 * @return `an_bootstrap()` が一度も呼ばれていなければ空文字列。
 */
const char *an_lib_root(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
