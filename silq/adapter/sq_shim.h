/*!
 * @file sq_shim.h
 * @brief Sil-Q コアと C++ アダプタの**境界**。純 C ABI・自己完結。
 *
 * 基準は （アダプタの構造 ── C の皮と C++ の本体）。
 *
 * ## この 1 枚が境界である理由
 * Sil-Q のヘッダ（`angband.h` / `externs.h` ほか）は 2000 年代前半世代の C で、
 * C++ へ安全に include できる保証がない。そこで
 * **Sil-Q のヘッダを見てよいのは `sq_*.c`（C の TU）だけ**と決め、
 * C++ 側（`sq_main.cpp` 以降）はこのヘッダしか見ない。
 *
 * したがってここに Sil-Q の型・マクロを漏らしてはいけない。
 * `byte` も `cptr` も `term` も出さず、素の C の型だけで喋る。
 *
 * ## 文字コード
 * **Sil-Q は純 ASCII である**（`lib/edit/*.txt` に非 ASCII バイトが 1 つも無い）。
 * よってここを出入りする文字列は**そのまま UTF-8 として通る**——幻想蛮怒の
 * `gb_text`（SJIS ⇄ UTF-8）に当たるものは要らない（設計 §3.1）。
 * 逆に、画面から降りてくるキーの **0x80 以上は捨てる**（英語しか入力できない）。
 *
 * ## 段階
 * P1（足場）ではコア名・版・Term の設営・起動列だけを実装する。
 * ゲーム状態の読み出し（マップ・HUD・メッセージ）は P2 / P3 で足す。
 */

#ifndef SILQ_ADAPTER_SQ_SHIM_H
#define SILQ_ADAPTER_SQ_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* 返り値。0 が成功、負が失敗。幻想蛮怒の GB_* と同じ並びにしてある。 */
#define SQ_OK 0
#define SQ_ERR_ARG (-1) /* 引数が無い・長すぎる */
#define SQ_ERR_LIB (-2) /* lib ディレクトリが見つからない */
#define SQ_ERR_TERM (-3) /* Term が立てられない */
#define SQ_ERR_QUIT (-4) /* コアが quit() を踏んだ */
#define SQ_ERR_CORE (-5) /* コアが core() を踏んだ */
#define SQ_ERR_STATE (-6) /* 手順違い（Term 無しで起動列を呼んだ等） */

/*!
 * @brief コアの名。プロトコル v1 の握手で使う `core_name`（P2）。
 * @return "silq"（静的な文字列。解放しない）
 */
const char *sq_core_name(void);

/*!
 * @brief コアの版（`silq/src/defines.h` の `VERSION_NAME` と `VERSION_STRING`）。
 * @return "Sil-Q 1.5.1.0-beta2" のような文字列（静的。解放しない）
 *
 * 先方の版が上がったら**ここは何もしなくていい**。写しを取り込み直せば追随する。
 */
const char *sq_core_version(void);

/* ================================================================== Term の設営 */

/*!
 * @brief ヘッドレスの Term を 1 枚立てて活性にする。
 * @param cols 桁（M0 は 80。設計 §3.2）
 * @param rows 行（M0 は 27。**24 未満は不可**）
 * @return SQ_OK / SQ_ERR_ARG / SQ_ERR_STATE / SQ_ERR_TERM
 *
 * **`sq_bootstrap()` より先に呼ぶこと。** `init_angband()` は進捗を
 * `note()`（`init2.c:1587`）で **23 行目**に書くので、Term が 1 枚も無いと落ちるし、
 * 24 行未満でも書けない。さらに `play_game()` は
 * `Term->hgt < 24 || Term->wid < 80` で `quit("main window is too small")` を踏む
 * （`dungeon.c:2944`）。
 */
int sq_term_install(int cols, int rows);

/*! @brief Term を畳む。立てていなければ何もしない。 */
void sq_term_remove(void);

/*!
 * @brief 立っている Term の大きさを返す。
 * @return SQ_OK / SQ_ERR_STATE
 */
int sq_term_size(int *cols, int *rows);

/*!
 * @brief Term の 1 行を写す（画面ミラーの読み口）。
 * @param y 行
 * @param text 文字の受け皿。NUL 終端して返す
 * @param attr 色の受け皿。NULL 可。**16 に畳んだ後の色**（設計 §4.1）
 * @param cap text / attr の容量（バイト）
 * @return 写したセル数、または負の値（SQ_ERR_*）
 *
 * @note Sil-Q は `MAX_COLORS 32`（`z-term.h:254`）で、16 以上は `TERM_SHADE` を
 * 足した「暗い写し」である。当方の `TermPalette` は 16 固定なので**ここで畳む**。
 * 暗さの情報は `light_level` が別に運ぶので落ちない。
 */
int sq_term_row(int y, char *text, unsigned char *attr, int cap);

/*!
 * @brief Term のカーソル位置と可視状態。
 * @return SQ_OK / SQ_ERR_STATE
 * @details `visible` は「隠していない」かどうか（`scr->cu` の否定かつ `cv`）。
 */
int sq_term_cursor(int *x, int *y, int *visible);

/*!
 * @brief 「前と変わったか」を安く判るための digest。
 *
 * @details `capture` は待ちの間も 10ms ごとに回る。変化していないフレームを
 * 毎回組み直して比べるのは高くつくので、ここで安い値を作って門にする。
 * 材料は「画面に出るものが変われば必ず動くもの」だけ:
 * Term の画・カーソル・`turn`・メッセージ本数・@ の位置と HP・icky/generated。
 * FNV-1a（64bit）。衝突しても実害は「1 フレーム古い画が残る」程度で、
 * `turn` が混ざっているのでゲームが進んでいる間は必ず動く。
 */
unsigned long long sq_change_digest(void);

/* ============================================================ ゲーム状態の読み出し（P2） */

/*!
 * @brief HUD の材料（設計 §4 の `hud` 行）。文字列は **ASCII**。
 * @details 「無い」ときは 0 と空文字列。タイトル画面のように `p_ptr` がまだ
 * 埋まっていない場面でも呼んでよい（`in_game` が 0 で返る）。
 *
 * **Sil-Q に無いもの**（設計 §4 の表）:
 * - `gold` … 金銭という概念が無い（`player_type` に `au` が無い）。常に 0
 * - `level` … プレイヤレベルが無い（経験値を技能に振る）。常に 0
 */
typedef struct sq_hud_data {
    char name[64]; /*!< `op_ptr->full_name` */
    char depth_text[32]; /*!< "Surface" / "550 ft"（`prt_depth()` と同じ文言） */
    int hp;
    int hp_max;
    int sp; /*!< Sil-Q の SP は歌の「声」に当たる（`csp`） */
    int sp_max;
    int depth; /*!< `p_ptr->depth`（階。ft は ×50） */
    int exp; /*!< `p_ptr->new_exp`（未使用の経験値。技能に振る原資） */
    int in_game; /*!< `character_generated`。0 のあいだ上の数値は意味を持たない */
} sq_hud_data;

/*!
 * @brief HUD の材料を読む。
 * @return SQ_OK（失敗しない。まだ何も無ければ 0 詰めで返る）
 */
int sq_read_hud(sq_hud_data *out);

/*! @brief 溜まっているメッセージの本数（`message_num()`）。 */
int sq_message_count(void);

/*!
 * @brief メッセージ 1 本を写す（`message_str()`）。
 * @param age 0 が最新。`sq_message_count()` 以上は空文字列
 * @return 写したバイト数（NUL を含まない）。負は SQ_ERR_ARG
 */
int sq_message_text(int age, char *out, int cap);

/*! @brief ゲームが始まっているか（`character_generated`）。 */
int sq_character_generated(void);

/*!
 * @brief 品書き（`get_item()` の一覧）を**出だしから見えるようにする**。
 *
 * @details `auto_display_lists`（`defines.h:2563`）を立てる。**呼んでよいのは
 * `character_generated` が立った直後 1 回だけ**（`sq_main.cpp` の `host_present`）。
 * セーブから設定を読み終えた後に上書きする形になる。
 *
 * ## なぜ要るのか
 * 画面側の選択枠は **Term ミラーの行の上にしか描けない**（`game_hud.cpp` の
 * `span_rect`）。`get_item()`（`object1.c:2879`）と歌選び（`cmd4.c:573`）は
 * どちらもこの設定が偽だと `screen_save()` を通らず一覧も描かないので、
 * **行 0 の促しだけが出て、選ぶ手立てが 1 つも無い**画面になる。
 * キーボードなら `a` と打てるし `*` で一覧を出せるが、**コントローラーには
 * `*` を押す道が無い**（`sq_pad_commands.cpp` の表に無い）。
 *
 * ## `p_ptr->command_see` ではだめだった（2026-08-22 に実測）
 * 同じ効きの実行時の旗はセーブに載らないので、そちらを立てるほうが行儀がよい。
 * ところが `process_player()` が**手番の頭で毎回落とす**
 * （`dungeon.c:1765`「cancel lurking browse mode」）。立てた次の手番で消える。
 *
 * ## 副作用
 * この設定は**セーブに載る**（`save.c:696` / `load.c:654`）ので、一度遊ぶと
 * 人が切っていた設定が真で焼き直される。起動のたびにどのみち立て直すので
 * 実害は「上流の Sil-Q で開いたときも一覧が出る」だけである。
 * 遊んでいる途中に設定画面で切れば、**その回は切れたまま**になる（ここは 1 度しか呼ばない）。
 */
void sq_prime_item_list_view(void);

/*!
 * @name `sq_screen_flags()` のビット。
 * @details 画面側の `menu_open` / `pre_game_menu` を決めるための材料。
 * **判定そのものは C++ 側（`sq_frame.cpp`）が持つ**——ここは事実だけを渡す。
 * @{
 */
#define SQ_SCREEN_GENERATED 0x0001 /*!< `character_generated`。@ が出来ている */
#define SQ_SCREEN_ICKY 0x0002 /*!< `character_icky != 0`。全画面の別画面（一覧・鍛冶） */
#define SQ_SCREEN_XTRA 0x0004 /*!< `character_xtra != 0`。再計算中で画面を触らない間 */
#define SQ_SCREEN_DEAD 0x0008 /*!< `p_ptr->is_dead` */
#define SQ_SCREEN_DUNGEON 0x0010 /*!< `character_dungeon`。`cave_feat[][]` に階が載っている */
#define SQ_SCREEN_PLAYING 0x0020 /*!< `p_ptr->playing` */
/*! @} */

/*!
 * @brief いまの画面の状態（上のビットの論理和）。
 *
 * @details `character_icky` / `character_xtra` は **`s16b` の計数器**である
 * （`externs.h:78-79`。`screen_save()` が ++、`screen_load()` が --）。
 * `== TRUE` で比べてはいけない——非 0 かどうかだけを見る。
 */
int sq_screen_flags(void);

/* ======================================================== サブパネル（M0.5 ②） */

/*!
 * @name サブパネル用の Term
 * @details 実装は `sq_sub_terms.c`（C）と
 * `sq_sub_panels.cpp`（C++）に分かれている。**枚数は画面側の
 * `kSubPanelCount`（7）と揃える**——Sil-Q の `ANGBAND_TERM_MAX` は 8 なので
 * `angband_term[1..7]` にちょうど収まる。
 * @{
 */
#define SQ_SUB_PANELS 7

/*!
 * @brief 画素なしの Term を 7 枚立てて `angband_term[1..7]` に挿す。
 * @return SQ_OK / SQ_ERR_TERM
 * @details `sq_term_install()` の後・`sq_bootstrap()` の前に呼ぶ。
 * 何度呼んでも 1 度しか立たない。**活性は変えない**（本線 Term に戻して返る）。
 */
int sq_sub_terms_install(void);

/*!
 * @brief 種類の名前（`window_flag_desc[flag]` を `sq_tr()` に通したもの。CP932）。
 * @return 書いたバイト数。**描画関数の無い種類は 0**（一覧に載せない印）
 * @details `window_stuff()` が `fix_*` を呼ぶ 7 つだけを通す。表に名前が在ることと
 * 描く関数が在ることは別である（`sq_sub_terms.c` の `sq_flag_has_fix` の表）。
 */
int sq_window_flag_name(int flag, char *out, int cap);

/*! @brief 画面側が望む桁・行。0 は「申告なし」（v1 §7）。当てるのは `sq_sub_term_apply_cells`。 */
void sq_sub_term_set_cells(int panel, int cols, int rows);

/*! @brief 望みの大きさを当てる。@return 変わったら 1（＝いま入っている絵は古い） */
int sq_sub_term_apply_cells(int panel);

/*! @brief いまの大きさ。@return SQ_OK / SQ_ERR_STATE */
int sq_sub_term_size(int panel, int *cols, int *rows);

/*! @brief 1 行を写す。`attr` は **16 に畳んだ後**（`sq_term_row` と同じ）。 */
int sq_sub_term_row(int panel, int y, char *text, unsigned char *attr, int cap);

/*!
 * @brief その枚にいま立っている種類。-1 = 何も立っていない／描けない種類。
 * @details **これが実効値**（v1 §7）である。画面側はこれを見て機能メニューの
 * 表示を合わせる。
 */
int sq_sub_current_kind(int panel);

/*! @brief 描画関数の無い番号を -1 に落とす（画面から来た値を整える）。 */
int sq_sub_sanitize_kind(int flag);

/*!
 * @brief 本線 Term（0 番）に立っている種類を落とす。@return 落としたら 1
 * @details 立っていると持ち物一覧が地図の上に描かれる。立ちうるのは
 * **セーブに入っていた値**——本物の Sil-Q で遊んだセーブを開いたとき。
 */
int sq_sub_clear_main_flags(void);

/*! @brief その枚の種類を差し替える。@return 変わったら 1 */
int sq_sub_set_kind(int panel, int flag);

/*!
 * @brief 「描き直せ」の印を立てる（`p_ptr->window`）。
 * @details **`window_stuff()` は呼ばない**。呼ぶと品選びや鍛冶の最中に Term を掻き回す。
 */
void sq_sub_request_redraw(void);
/*! @} */

/* ================================================================ ホストのフック */

/*!
 * @brief null term（C 側）から C++ 側を呼び返す手段。
 *
 * @details 変愚の `presentation/term/sdl_null_term.cpp`（`on_fresh` / `on_event` /
 * `on_flush` / `on_delay`）と、幻想蛮怒の `gb_host_hooks` と同じ形。
 *
 * すべて**ゲームスレッドから**呼ばれる。コアの状態を触ってよいのはそこだけ
 * （`p_ptr` も `cave_feat` も `Term` も素の大域変数で、鍵が無い）。
 */
typedef struct sq_host_hooks {
    /*! 画面が更新された（`TERM_XTRA_FRESH` / 待ちの毎周回）。frame を組んで送る。 */
    void (*present)(void);
    /*!
     * 次のキーを 1 個取る。**待たない**。
     * @return 1〜127 のキー、または 0（いま無い）
     * @note 返すのは **ASCII** である。0x80 以上は C++ 側で捨ててから積むこと。
     */
    int (*next_key)(void);
    /*! 溜まっているキーを捨てる（`TERM_XTRA_FLUSH`）。 */
    void (*drop_keys)(void);
    /*! ミリ秒待つ（`TERM_XTRA_DELAY` と、キー待ちの空回り）。 */
    void (*sleep_ms)(int ms);
    /*! 終了要求（stdin EOF / `quit` メッセージ）が出ているか。0 か非 0。 */
    int (*shutdown_requested)(void);
} sq_host_hooks;

/*!
 * @brief フックを差す。NULL を渡すと外れる（`--selftest` と同じ「ESC を積むだけ」）。
 * @details 差してよいのは `sq_term_install()` の後・`sq_bootstrap()` の前。
 */
void sq_set_host_hooks(const sq_host_hooks *hooks);

/*!
 * @brief **溜まった押しを捨てて `ms` ミリ秒だけ落ち着かせる**（2026-08-23）。
 *
 * @details 捨てる → 待つ → もう一度捨てる。**画面とコアが別プロセス**なので、
 * 1 度きりだと「捨てた瞬間にまだ線の途中にいた押し」が後から届く。
 * 待っている間も画は送るので、画面は固まらない。
 *
 * 要る所は「**画面が切り替わった直後で、1 打が重い意味を持つ**」場面である。
 * いまの呼び手はタイトル（`sq_pick_game`）だけ。
 */
void sq_settle_input(int ms);

/* ==================================================================== 地図（P3） */

/*!
 * @name `sq_map_cell::feature_flags` のビット（設計 §4.2）。
 *
 * @details `presentation/frame/cell_feature_bits.h` の `CELL_FEAT_*` と**意味は同じ**だが、
 * ここは純 C の境界なので値を独立に定義する。翻訳表は `sq_frame.cpp` の
 * `translate_feature_flags()` 1 か所だけにある（**どちらかの並びが変わっても
 * そこを直せば済む**ようにするための独立である。値を揃えて素通しにしない）。
 *
 * Sil-Q に `FF_` フラグは無い。判定は **`FEAT_*` の番号の範囲**（`defines.h:940-1013`）と
 * **`CAVE_*` ビット**（`:1593-1607`）から立てる。
 * @{
 */
#define SQ_FEAT_WALL 0x0001u /*!< `cave_info & CAVE_WALL` */
#define SQ_FEAT_DOOR 0x0002u /*!< `FEAT_DOOR_HEAD`(0x20)〜`FEAT_DOOR_TAIL`(0x2F) */
#define SQ_FEAT_DOOR_OPEN 0x0004u /*!< `FEAT_OPEN`(0x04) / `FEAT_BROKEN`(0x05) */
#define SQ_FEAT_STAIRS 0x0008u /*!< `FEAT_LESS`(0x50)〜`FEAT_MORE_SHAFT`(0x53) */
#define SQ_FEAT_PERMANENT 0x0010u /*!< `FEAT_WALL_PERM`(0x3F) */
#define SQ_FEAT_RUBBLE 0x0020u /*!< `FEAT_RUBBLE`(0x31) */
#define SQ_FEAT_GLOW 0x0040u /*!< **立てない。**画面側では「光源のマス」の意味（§4.2） */
#define SQ_FEAT_PLAYER 0x0080u /*!< @ の居るマス */
#define SQ_FEAT_KNOWN 0x0100u /*!< `CAVE_MARK`（記憶）または `CAVE_SEEN`（いま見えている） */
#define SQ_FEAT_PASSABLE 0x0200u /*!< `!(CAVE_WALL)` かつ扉でない */
#define SQ_FEAT_ROOM 0x0400u /*!< `CAVE_ROOM` */
#define SQ_FEAT_GLOWING 0x0800u /*!< `CAVE_GLOW` かつ暗がりでない */
#define SQ_FEAT_CHASM 0x1000u /*!< `FEAT_CHASM`(0x02)。**Sil-Q 固有**（歩けるが落ちる） */
#define SQ_FEAT_FORGE 0x2000u /*!< `FEAT_FORGE_HEAD`(0x40)〜`TAIL`(0x4F)。**Sil-Q 固有**（鍛冶場） */
/*! @} */

/*!
 * @brief 地図 1 マス。**中身はすでに「見えているぶんだけ」**（未知は 0 詰め）。
 * @details `MapCellView`（`presentation/frame/map_cell_view.h`）の材料。
 * 見た目（`ascii` / `fg` / `bg`）は `map_info()` の返り値そのままで、
 * ID は `cave_feat` / `cave_m_idx` / `cave_o_idx` の直読み。
 * **視認していない敵・拾っていないアイテムは入れない**（画面側へ漏らさない）。
 */
typedef struct sq_map_cell {
    short gx;
    short gy;
    unsigned short terrain_id; /*!< `f_info[cave_feat].mimic` を解決した後の feat */
    unsigned short feature_flags; /*!< 上の `SQ_FEAT_*` */
    unsigned short monster_id; /*!< `m_ptr->r_idx`。**`ml` のときだけ**。0 = 居ない／見えない */
    unsigned short monster_slot; /*!< `m_idx`（個体の通し番号）。条件は `monster_id` と同じ */
    unsigned short object_id; /*!< 先頭の `marked` オブジェクトの `k_idx`。0 = 無い */
    unsigned char light_level; /*!< 0 = 記憶のみ / 1 = 視界内だが無灯 / 2 = いま照明あり */
    unsigned char fg_color; /*!< `map_info` の `a`（**16 に畳んだ後**。設計 §4.1） */
    unsigned char bg_color; /*!< `map_info` の `ta`（地形層。同じく畳んだ後） */
    char ascii; /*!< `map_info` の `c`（ASCII 1 バイト） */
} sq_map_cell;

/*!
 * @brief いまの階の大きさ（`p_ptr->cur_map_wid` / `cur_map_hgt`）。
 * @return SQ_OK / SQ_ERR_STATE（階がまだ無い）
 */
int sq_floor_size(int *width, int *height);

/*! @brief @ の位置（`p_ptr->px` / `py`）。@return SQ_OK / SQ_ERR_STATE */
int sq_player_pos(int *x, int *y);

/*!
 * @brief コアが「いま見ている区画」（`p_ptr->wy` / `wx` と 1 画面ぶんの大きさ）。
 *
 * @details ふつうは @ を追って動くので画面側は見なくてよいのだが、
 * **`L`（地図を動かす。`do_cmd_locate`）はこれだけを動かす**——@ は動かない。
 * 画面側は @ を中心に窓を切っているので、そのままでは**押しても何も起きない**
 * （2026-08-26 に気づいた「地図を動かすモードに入るもキー入力をしても画面変化なし」）。
 *
 * @param[out] x,y 区画の左上のマス。@param[out] w,h 区画の大きさ（マス）。
 * @return `SQ_OK` / `SQ_ERR_*`。どれも NULL 可。
 */
int sq_panel_view(int *x, int *y, int *w, int *h);

/*!
 * @brief `L`（地図を動かす）の最中か。**そのときだけ真。**
 * @details `do_cmd_locate()`（`silq/src/cmd3.c`）が自分で立てる。
 *
 * **「@ が区画の外に出たか」では足りない**（2026-08-26 に外した）。広い階では
 * 1 画面ぶん動かしても @ が区画の中に残るので、判定が立たず**絵が動かないまま**になる。
 * **命令そのものしか知らないこと**なので、命令に言わせる。
 */
int sq_locate_active(void);

/*!
 * @brief @ の種族（`p_ptr->prace`。`race.txt` の N 番号 0〜3）。
 * @details **@ の絵を引くのに要る**（目録の `P` は種族ごとに 1 枚）。家（`phouse`）は
 * 絵を持たないので出さない。
 * @return SQ_OK / SQ_ERR_STATE（まだ人物が居ない）
 */
int sq_player_race(int *race);

/*!
 * @brief 視界の矩形を読む。
 * @param x0,y0 左上（階の外にはみ出してよい。はみ出しは 0 詰めで返る）
 * @param w,h 大きさ
 * @param out `w * h` 個の受け皿（走査順は y 外側・x 内側）
 * @param cap `out` の要素数
 * @return 埋めたマス数、または SQ_ERR_*
 *
 * @note `map_info()` は**純粋関数ではない**（幻覚のときに乱数を引く）。
 * 変愚側の Bridge も同じものを毎フレーム呼んでいるので流儀は揃えたが、
 * 乱数が進むことは承知しておく。
 */
int sq_read_map(int x0, int y0, int w, int h, sq_map_cell *out, int cap);

/*!
 * @brief 一過性の重ね書き 1 マス。
 * @details `MapOverlayCell`（`presentation/frame/game_frame.h`）の材料。
 */
typedef struct sq_overlay_cell {
    short gx;
    short gy;
    unsigned char color; /*!< **16 に畳んだ後**（`sq_read_map` と同じ） */
    char ascii; /*!< そのマスに重ねて出ている 1 文字 */
} sq_overlay_cell;

/*!
 * @brief Term の地図区画と `cells` の見た目を突き合わせ、**食い違ったマスだけ**返す。
 *
 * @param cells `sq_read_map()` が返した並び（`gx`/`gy`/`ascii`/`fg_color` を使う）
 * @param count その要素数
 * @param out 受け皿
 * @param cap `out` の要素数
 * @return 積んだ数（0 以上）、または SQ_ERR_*
 *
 * @details `print_rel()`（`cave.c`）は `Term_queue_char(COL_MAP + kx, ROW_MAP + ky, ...)` で
 * **主 Term の地図区画へ直に書く**。当方はマスの絵を `map_info()` から作っているので、
 * ダメージの数字・矢の飛跡・爆風・聞き耳の `*`・照準カーソルが**1 つも届かない**。
 * ここで突き合わせれば **8 系統がまとめて**拾え、コアの木にフックを 1 つも刺さずに済む
 * （必守制約 2）。
 *
 * ## `map_info()` を呼び直さない
 * `map_info()` は**純粋関数ではない**（幻覚のときに乱数を引く）。呼び直すと 1 フレームに
 * 2 度引くことになるので、`sq_read_map()` が既に採った結果をそのまま材料にする。
 *
 * ## 罠 1 — 差分は**コアのパネル**を基準に採る
 * `print_rel()` は `p_ptr->wy`/`wx` ＋ `SCREEN_HGT`×`SCREEN_WID` の外を捨てる。
 * `cells` は呼び手が渡した任意の矩形なので、**パネルの外のマスは見ない**
 * （Term のその桁には状態列や帯が入っており、比べると全部が「食い違い」になる）。
 *
 * ## 罠 2 — 幻覚のあいだは**何も出さない**
 * `map_info()` は `p_ptr->image` のとき `image_random()` を引く（`cave.c:1264`）。
 * Term に描かれた字は**前に引いた乱数**なので、毎フレーム全面が食い違う。
 * 意味のある重ね書きと見分けられないので、その間は 0 件で返す。
 */
int sq_read_overlay(const sq_map_cell *cells, int count, sq_overlay_cell *out, int cap);

/*!
 * @name 敵の警戒度。
 *
 * @details **このゲームの隠密の中核**である。Sil-Q は `hilite_unwary`（既定 ON）で
 * 「まだこちらに気づいていない敵」の背景を暗くして見分けさせるが、その仕掛けは
 * **attr の背景セットの軸**（`attr = 前景(0..31) + 32 × 背景セット`）に乗っていて、
 * 当方の畳み口（`& 0x0F`）が丸ごと捨てていた——**1 匹もハイライトされていない**。
 *
 * 運ぶのは背景セットではなく**警戒度そのもの**にした（2026-08-22 に決めた
 * 「板の足元に円形の警戒度に応じた色を変えるリングを表示させる」）。背景セットは
 * 3 値しか無く、地の色を塗る以上のことができない。段を運べば画面側が自由に描ける。
 *
 * 段はコアに依らない粒度にする（必守制約 1）。Sil-Q の境目は
 * `get_alertness_text()`（`xtra1.c`）と**同じ 2 本**である。
 * @{
 */
#define SQ_ALERT_UNKNOWN 0 /*!< 分からない（出さない） */
#define SQ_ALERT_ASLEEP 1 /*!< `alertness < ALERTNESS_UNWARY`（-10）。眠っている */
#define SQ_ALERT_UNWARY 2 /*!< `< ALERTNESS_ALERT`（0）。気づいていない */
#define SQ_ALERT_ALERT 3 /*!< 気づいている */

/*! @brief 警戒度 1 件（見えている敵のマス）。 */
typedef struct sq_alert_cell {
    short gx;
    short gy;
    unsigned char level; /*!< 上の `SQ_ALERT_*` */
} sq_alert_cell;

/*!
 * @brief 見えている敵の警戒度を写す。
 * @return 積んだ数、または SQ_ERR_*
 * @details **`hilite_unwary` が偽なら 0 件で返す。**あれは「気づいていない敵を
 * 目立たせるか」という遊ぶ人の設定で、当方のリングは同じ意図の別の見せ方である。
 * コアの設定を無視して出すと、切ったつもりの人の画面に出続ける。
 */
int sq_read_alerts(sq_alert_cell *out, int cap);

/*!
 * @brief いま照準が乗っているマス。@return SQ_OK / SQ_ERR_STATE（出ていない）
 * @details **これは重ね書きではない。** `hilite_target`（既定 ON）は
 * `move_cursor_relative()` で **Term のカーソルを動かすだけ**で（`dungeon.c:1742`）、
 * `print_rel()` を通らないので `sq_read_overlay()` では 1 度も拾えない。
 * 位置を直に読んで別に運ぶ。`target_sighted()` が偽なら「出ていない」。
 */
int sq_read_target(int *x, int *y);
/*! @} */

/*!
 * @name ミニマップの種別コード。`MinimapKind`（`minimap_snapshot.h`）と同じ意味。
 * @details 値の独立の理由は `SQ_FEAT_*` と同じ。翻訳は `sq_frame.cpp`。
 * @{
 */
#define SQ_MM_UNKNOWN 0
#define SQ_MM_FLOOR 1
#define SQ_MM_WALL 2
#define SQ_MM_DOOR 3
#define SQ_MM_STAIRS 4
#define SQ_MM_ITEM 5
#define SQ_MM_MONSTER 6
#define SQ_MM_PLAYER 7
/*! @} */

/*!
 * @brief 階の全域をミニマップの種別コードで写す。
 * @param out `width * height` バイトの受け皿
 * @param cap `out` の容量
 * @return 書いたバイト数、または SQ_ERR_*
 * @details **見たマスだけ**を出す（未踏破を出すと未探索領域を画面側が暴露する）。
 */
int sq_read_minimap(unsigned char *out, int cap);

/*!
 * @brief 階の素性（`FloorIdentity` の材料）。文字列は ASCII。
 * @details Sil-Q には町も荒野もクエスト階も無い——**常にダンジョン**である。
 */
typedef struct sq_floor_info {
    int dun_level; /*!< `p_ptr->depth`（階。ft は ×50） */
    int kind; /*!< 常に 2（ダンジョン） */
    char place_name[64]; /*!< "Surface" / "550 ft" */
    int light_radius; /*!< `p_ptr->cur_light` */
} sq_floor_info;

/*! @brief 階の素性と光。@return SQ_OK（失敗しない。ゲーム前は 0 詰め） */
int sq_read_floor_info(sq_floor_info *out);

/* ============================================ 周囲の地形の内訳（環境音の層） */

/*!
 * @brief 周囲の地形の内訳（`presentation/frame/game_frame.h` の `SurroundingsView`）。
 * @details 環境音は「いま周りに何が在るか」で層を重ねる
 * **数えるのはコア側の仕事**である——
 * 地形の意味を知っているのは `cave_feat` を読める側だけで、画面側は地形テーブルを持たない。
 *
 * **Sil-Q で埋まるのは 2 つだけ**である。あのコアの地形は床・壁・瓦礫・扉・階段・
 * 深淵・鍛冶場しか無く、草も水も木も溶岩も 1 つも存在しない（`defines.h` の `FEAT_*`）。
 * 埋まらない欄は 0 のままで、画面側はその層を鳴らさない。
 *
 * | 欄 | Sil-Q での中身 |
 * |---|---|
 * | `wall` | 通れないマス（`cave_floor_bold` が偽）。**閉塞の層**——地下だけの遊びなので効く |
 * | `rock` | 瓦礫（`FEAT_RUBBLE`）。**岩の層** |
 *
 * 割合は 0..255 が 0..100%。瓦礫は壁でもあるので**両方に数える**（合計は 100% を超えうる）。
 */
typedef struct sq_surroundings {
    unsigned char rock; /*!< 瓦礫 */
    unsigned char wall; /*!< 通れないマス。**閉塞の層はこれで決まる** */
    unsigned char radius; /*!< 数えた半径。**0 なら「数えていない」** */
    unsigned short counted; /*!< 数えたマスの数（既知のものだけ） */
} sq_surroundings;

/*!
 * @brief 周囲を数える。@return SQ_OK（失敗しない。数えられなければ 0 詰め）
 * @details 変愚の `fill_surroundings()`（`presentation_bridge.cpp`）と**同じ数え方**に
 * してある。ずれると同じ場所で音が食い違う。気をつけたのは 2 つ:
 *
 * - **既知のマスだけ**（`CAVE_MARK | CAVE_SEEN`）。未探査を混ぜると、まだ見ていない
 *   広間の閉塞が鳴る
 * - **円で数える**（正方形だと角のマスが遠いのに同じ重みになる）
 *
 * 読むだけで、コアの状態は 1 つも変えない（設計 §1 制約 1）。
 */
int sq_read_surroundings(sq_surroundings *out);

/* ==================================================================== 効果音 */

/*
 * **コアは鳴らさない。**「どのマスで何の音が要る」という出来事だけをフレームへ載せ、
 * 鳴らすのは画面側（`hd2d/audio/`）である。理由は
 * `presentation/frame/sound_event.h`・——位置で聞き分けるには
 * 聞き手の位置と向き、および HRTF の効くミキサが要り、コアはどちらも持たないため。
 *
 * **Sil-Q の音は `message()` の型そのもの**である（`util.c:2960` が
 * `sound(message_type)` を呼ぶ）。だから直に `sound()` を書いてある 15 か所より
 * 広く鳴る——`MSG_STAIRS` `MSG_KILL` `MSG_HIT` `MSG_OPENDOOR` `MSG_HITWALL` など
 * 24 種が実際に出る。`MSG_GENERIC`（0）は綴りが空なので捨てる。
 *
 * 綴りは `angband_sound_name[]`（`variable.c`）そのままで、変愚の目録
 * （`assets/audio/sfx.jsonc`）に 22 名が当たる。残る 5 つは Angband 系の固有名で、
 * 目録の側に行を足して変愚の wav を割り当ててある。
 *
 * @note 見えていない敵の騒音は
 * ここには乗らない。あちらは の P4 で、音の道のりを
 * コアが計算して `path` / `heard_*` に載せる別の仕事である。
 */

/*! @brief 音 1 つ。`name` は `angband_sound_name[]` の綴り（NUL 終端）。 */
typedef struct sq_sound_event {
    char name[16]; /*!< 音の名前。空なら捨ててよい */
    short y; /*!< 鳴ったマス（いまは常に @ のマス＝頭で鳴る） */
    short x;
} sq_sound_event;

/*!
 * @brief 画面側が音の出来事を欲しがっているか（`ui_state.sound_events`）を覚える。
 * @details 偽の間は 1 つも積まない。**`use_sound` は落とさないこと**——コアの
 * `sound()` はあれが偽だと `Term_xtra` すら呼ばず、書き留める機会ごと消える
 * （`util.c:2098`）。
 */
void sq_sound_set_wanted(int wanted);

/*! @brief `TERM_XTRA_SOUND` を 1 回聞いた。`v` は `MSG_*` の番号。 */
void sq_sound_push(int v);

/*! @brief 溜まっている音を汲んで消す。@return 書いた個数 */
int sq_sound_take(sq_sound_event *out, int cap);

/*!
 * @brief これまでに積んだ**累計**。digest に混ぜる。
 * @details 音は一度きりの出来事で、鳴った瞬間の Term が 1 セルも動かない場面がある。
 * 混ぜないと**その 1 枚が線に乗らず**、音が次の別の変化まで遅れる
 */
unsigned long sq_sound_total(void);

/* ============================================================== 文字入力（W1） */

/*!
 * @brief 自由文字入力の待ちに出入りするときに立てる旗を上下する。
 *
 * @details 呼ぶのは **`silq/src` 側**（`util.c` の `askfor_aux()` と `askfor_name()`）。
 * あちらはこのヘッダを見ず、`extern` を 1 行書いて呼ぶ（幻想蛮怒と同じ作法。
 * `gensoband/src/autopick.c:3682`）—— 20 年ものの C にアダプタのヘッダを見せないため。
 *
 * これが無いと **IME が起動から終了まで有効のまま**になり、
 * 一度変換が始まると以後の打鍵が合成へ吸われる（`s`が届かない。SH-07）。
 * また画面側は非 ASCII をこの旗のときだけ送るので、日本語の名前も打てない（SH-31）。
 */
void sq_text_input_set(int active);

/*! @brief いま自由文字入力の待ちか（`frame.text_input_active` の材料）。 */
int sq_text_input_active(void);

/*!
 * @brief いま**次の命令**を待っているか（`frame.awaiting_command` の材料。2026-08-23）。
 *
 * @details 読むのはコアの `inkey_flag` **1 つだけ**である。あちらの註記
 * （`util.c:1826`）が「TRUE なら通常の命令を待っていると見なす」と言っており、
 * 立つのは `request_command()` が `inkey()` を呼ぶ直前（`util.c:4078`）、
 * 降りるのは `inkey()` が鍵を取って返る所（`util.c:2059`）。
 * **待っている最中はずっと立っている**ので、待ちの中から回る `sq_present()`
 * （`sq_null_term.c` の `sq_term_event`）で採れる。
 *
 * `-more-` や `y/n` の待ちでは**立たない**——そこが要るところである。
 * 立たない待ちで画面がコマンドメニューを開くと、メッセージが進まなくなる。
 *
 * **`silq/src/` には 1 バイトも足していない**（必守制約 2）。`inkey_flag` は
 * `externs.h` が公開している素の大域で、先方の `main-win.c` も同じように読んでいる。
 */
int sq_awaiting_command(void);

/*!
 * @brief **画面が読む旗が `sq_change_digest()` に混ざっているか**を確かめる（検査。2026-08-23）。
 *
 * @param report 落ちた旗の名前を書く先（成功なら触らない）。`NULL` 可
 * @param cap `report` の大きさ
 * @return 0 = 全部混ざっている／非 0 = 混ざっていない旗がある
 *
 * @details **同じ穴を 2 度踏んだので機械で押さえる。**
 * `askfor_aux()` も `request_command()` も「画を描き終えてから待ちに入る」ので、
 * 旗が立った瞬間は**マスが 1 つも変わっていない**。digest に混ぜ忘れると capture が
 * 省かれ、**旗が真のフレームが 1 枚も送られない**——画面側では
 * 「IME が切れない」（W1）／「A がコマンドメニューにならない」（2026-08-23）になる。
 * どちらも**絵は完全に正常に見える**ので、目では絶対に気づけない。
 *
 * やり方は「旗を立てて digest を採り、戻して採り、違うことを見る」。
 * 触った大域は**必ず元へ戻す**（検査のために遊びの状態を動かさない）。
 */
int sq_digest_flag_check(char *report, int cap);

/* ==================================================================== 起動と進行 */

/*!
 * @brief 起動列。`init_file_paths(lib_dir)` → `init_angband()` まで。
 * @param lib_dir Sil-Q の lib ディレクトリ（`<exe_dir>/silq/lib`）。
 *                末尾の区切りは有っても無くてもよい
 * @param lang `"ja"` なら**実体の定義を `<silq>/lib-ja/edit` から読む**。
 *             NULL / `"en"` / 作った `lib-ja/edit` が無いときは英語のまま
 * @return SQ_OK / SQ_ERR_*
 *
 * **`play_game()` は呼ばない。** ゲームを回すのは `sq_run_game()`。
 * 失敗したときの説明は `sq_last_error()` に入る。
 *
 * @note **言語は `init_angband()` より前に決まっていなければならない。**
 * ここで `edit` の置き場が決まり、そのまま `*.raw` に焼かれるためである。
 * 遊んでいる途中の切り替えは M1 ではやらない（設計 §10）。
 */
int sq_bootstrap(const char *lib_dir, const char *lang);

/*!
 * @brief 名前の組み立てを並べて出す（`--name-check`）。**診断の口**。
 * @details `sq_bootstrap()` の後に呼ぶ。遊びの筋には入らない。
 * 助数詞・銘の前置き／後置き・風味・アーティファクト・`monster_desc` の
 * 5 つの型を一度に見るためのもの。
 * 出力は stderr（v1 §1.1）。
 */
void sq_name_check(void);

/*!
 * @brief タイトル（`initial_menu`）とゲーム本体を回す。**戻ってくるのは終わったとき**。
 * @return SQ_OK（正常終了）/ SQ_ERR_QUIT / SQ_ERR_CORE
 *
 * @details `main.c:569-658` のループの写し（設計 §3.3）。**タイトルとセーブ選択を
 * 自作しない**のがここの肝である——Sil-Q は `initial_menu()`（`init2.c:1741`）で
 * 「a) Tutorial / b) New character / c) Open saved character / d) Quit」を
 * **自分で Term に描く**ので、ミラーにそのまま出るし、文字キー・矢印（8/2）・
 * Enter で選べる。幻想蛮怒で要った `title_and_pick_savefile()` は不要。
 */
int sq_run_game(void);

/*!
 * @brief 次に `sq_run_game()` を回すとき、**直前に保存した人物を自動で開く**（`--resume`）。
 * @param on 1 = 開く／0 = ふつうにタイトルを出す
 *
 * @details 遊んでいる途中の言語切り替えのための口。
 * 画面側が言語を替えると、**コアを保存して畳み、起こし直す**——言語は
 * `init_angband()` に入る前に決まっていなければならないので、それしか手が無い
 * （`sq_bootstrap()` の註）。起こし直したあとに人物を選び直させるのは筋が悪いので、
 * ここで自動的に開く。
 *
 * **どの人物かはコアが覚えている。** `sq_shutdown_and_quit()` が保存したときに
 * セーブの置き場へ**印**（`.resume`。点で始まるので一覧に出ない）を書き、
 * ここが 1 だとそれを読んで消す。だから画面側は綴りを知らなくてよく、
 * プロトコルに欄を足す必要も無い（必守制約 3・4）。
 *
 * **`sq_bootstrap()` より前に呼ぶこと。** 呼ばなければ（0 のままなら）起動列が
 * 古い印を消すので、印が残って別の人物が勝手に開くことはない。
 */
void sq_set_resume(int on);

/*!
 * @brief 直近の失敗の説明（コアの `quit()` / `core()` に渡された文字列）。
 * @return 静的な buffer。失敗していなければ空文字列
 */
const char *sq_last_error(void);

/*!
 * @brief 終了要求に応じて**保存してから畳む**。**戻らない**。
 *
 * @details `main-win.c:3439`（`WM_CLOSE`）の写し（設計 §3.2）:
 * `msg_flag = FALSE;` → `do_cmd_save_game();` → `quit(NULL);`。
 * 幻想蛮怒のような緊急セーブ経路（`WM_QUERYENDSESSION` の写し）は要らない——
 * Sil-Q のこの列は `inkey()` が待つ場所を通らない。ただし `do_cmd_save_game()` の
 * 中の `message_flush()` が `-more-` で待たないよう、**`msg_flag` を先に落とす**こと
 * （落とすのは向こうの `WM_CLOSE` も同じ）。
 *
 * 呼んでよいのは**ゲームスレッド**（＝`TERM_XTRA_EVENT` の中）だけ。
 */
void sq_shutdown_and_quit(void);

#ifdef __cplusplus
}
#endif

#endif /* SILQ_ADAPTER_SQ_SHIM_H */
