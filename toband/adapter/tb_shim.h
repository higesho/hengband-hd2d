/*!
 * @file tb_shim.h
 * @brief TOband コアと C++ アダプタの**境界**。純 C ABI・自己完結。
 *
 * （アダプタ）。C の皮と C++ の本体の分け方は （幻想蛮怒）。
 *
 * ## この 1 枚が境界である理由
 * TOband のヘッダ（`angband.h` / `externs.h` ほか）は 2013 年世代の C で、
 * C++ へ安全に include できる保証がない。そこで
 * **TOband のヘッダを見てよいのは `tb_*.c`（C の TU）だけ**と決め、
 * C++ 側（`tb_main.cpp` 以降）はこのヘッダしか見ない。
 *
 * したがってここにTOband の型・マクロを漏らしてはいけない。
 * `byte` も `cptr` も `term` も出さず、素の C の型だけで喋る。
 *
 * ## 文字コード
 * **ここを出入りする文字列はすべてコア内部コード（SJIS）のバイト列**である。
 * UTF-8 への変換は C++ 側（`tb_text.h`）の仕事で、この境界では一切変換しない
 * （設計 §3.1「フレームへ出す文字列は境界の出口で SJIS → UTF-8」）。
 *
 * ## P2（プロトコル本体）で足したもの
 * - 画面の読み口の残り（カーソル・HUD・メッセージ）
 * - **ホストのフック**（`tb_host_hooks`）。null term から C++ 側を呼び返す唯一の道
 * - `tb_run_game()`。`play_game()` を回す。`tb_bootstrap()` とは**別の跳び先**を使う
 *   （正常終了 = `quit()` と、起動失敗 = `quit()` を取り違えないため。設計 V4）
 */

#ifndef TOBAND_ADAPTER_TB_SHIM_H
#define TOBAND_ADAPTER_TB_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* 返り値。0 が成功、負が失敗。 */
#define TB_OK 0
#define TB_ERR_ARG (-1) /* 引数が無い・長すぎる */
#define TB_ERR_LIB (-2) /* lib ディレクトリが見つからない */
#define TB_ERR_TERM (-3) /* Term が立てられない */
#define TB_ERR_QUIT (-4) /* コアが quit() を踏んだ */
#define TB_ERR_CORE (-5) /* コアが core() を踏んだ */
#define TB_ERR_STATE (-6) /* 手順違い（Term 無しで起動列を呼んだ等） */
#define TB_BIRTH_CANCEL 1 /* `tb_run_game()` の戻り値：誕生の最初の画面（性別）で取消された。タイトルへ戻る（修-61） */

/*!
 * @brief コアの名。プロトコル v1 の握手で使う `core_name`（P2）。
 * @return "toband"（静的な文字列。解放しない）
 */
const char *tb_core_name(void);

/*!
 * @brief コアの版。`toband/src/defines.h` の `H_VER_MAJOR/MINOR/PATCH` から組む。
 * @return "2.1.6" のような文字列（静的な文字列。解放しない）
 *
 * 先方の版が上がったら**ここは何もしなくていい**。写しを取り込み直せば追随する。
 */
const char *tb_core_version(void);

/* ================================================================== Term の設営 */

/*!
 * @brief ヘッドレスの Term を 1 枚立てて活性にする。
 * @param cols 桁（M0 は 80。設計 §3.2）
 * @param rows 行（M0 は 27。**24 未満は不可**──note() が 23 行目に書く）
 * @return TB_OK / TB_ERR_ARG / TB_ERR_STATE
 *
 * **`init_angband()` より先に呼ぶこと。** あちらは note() / Term_putstr() で
 * 進捗を画面に書くので、Term が 1 枚も無いと落ちる。
 */
int tb_term_install(int cols, int rows);

/*! @brief Term を畳む。立てていなければ何もしない。 */
void tb_term_remove(void);

/*!
 * @brief 立っている Term の大きさを返す。
 * @return TB_OK / TB_ERR_STATE
 */
int tb_term_size(int *cols, int *rows);

/*! @brief 主 Term の行数を新UIの文書表示領域へ合わせる。 */
int tb_term_resize_rows(int rows);

/*!
 * @brief Term の 1 行を写す（画面ミラーの読み口。P2 の `term_mirror` の芯）。
 * @param y 行
 * @param text 文字の受け皿。NUL 終端して返す（SJIS のバイト列のまま。変換は境界の外）
 * @param attr 色の受け皿。NULL 可
 * @param cap text / attr の容量（バイト）
 * @return 写したセル数、または負の値（TB_ERR_*）
 *
 * 旧 z-term は漢字属性を持たない（z-term.h に KANJI 系なし）。**2 バイト文字は
 * 2 セルに分かれて入っている**ので、UTF-8 化する側が SJIS 先行バイトで
 * 走査して結合する（設計 §3.1）。ここでは結合しない。
 */
int tb_term_row(int y, char *text, unsigned char *attr, int cap);

/*!
 * @brief Term のカーソル位置と可視状態。
 * @return TB_OK / TB_ERR_STATE
 * @details `visible` は「隠していない」かどうか（`scr->cu` の否定）。
 * 文字入力のキャレットを画面側へ出すのに使う（`menu_term_curs_col/row`）。
 */
int tb_term_cursor(int *x, int *y, int *visible);

/* ============================================================ ゲーム状態の読み口 */

/*!
 * @brief HUD の材料（設計 §4 の `hud` 行）。文字列は**すべて SJIS**。
 * @details 「無い」ときは 0 と空文字列。誕生画面のように `p_ptr` がまだ
 * 埋まっていない場面でも呼んでよい（`in_game` が 0 で返る）。
 */
typedef struct tb_hud_data {
    char name[64]; /*!< プレイヤ名（`player_name`） */
    char place[96]; /*!< 現在地名（町名 or ダンジョン名） */
    char depth_text[32]; /*!< 「地上」「12 階」等（`prt_depth` と同じ文言） */
    int hp;
    int hp_max;
    int sp;
    int sp_max;
    int level;
    int depth; /*!< `dun_level` */
    long gold; /*!< `p_ptr->au` */
    int in_game; /*!< `character_generated`。0 のあいだ上の数値は意味を持たない */
} tb_hud_data;

/*!
 * @brief HUD の材料を読む。
 * @return TB_OK（失敗しない。まだ何も無ければ 0 詰めで返る）
 */
int tb_read_hud(tb_hud_data *out);

/*! @brief 溜まっているメッセージの本数（`message_num()`。util.c:2721）。 */
int tb_message_count(void);

/*!
 * @brief メッセージ 1 本を写す（`message_str()`。util.c:2745）。
 * @param age 0 が最新。`tb_message_count()` 以上は空文字列
 * @return 写したバイト数（NUL を含まない）。負は TB_ERR_ARG
 */
int tb_message_text(int age, char *out, int cap);

/*! @brief ゲームが始まっているか（`character_generated`）。守るものの有無の判定に使う。 */
int tb_character_generated(void);

/* ================================================================ 画面の状態（P3） */

/*!
 * @name `tb_screen_flags()` のビット。
 * @details 画面側の `menu_open` / `pre_game_menu` を決めるための材料。
 * **判定そのものは C++ 側（`tb_frame.cpp`）が持つ**——ここは事実だけを渡す。
 * @{
 */
#define TB_SCREEN_GENERATED 0x0001 /*!< `character_generated`。@ が出来ている */
#define TB_SCREEN_ICKY 0x0002 /*!< `character_icky != 0`。全画面の別画面（店・建物・一覧） */
#define TB_SCREEN_XTRA 0x0004 /*!< `character_xtra != 0`。再計算中で画面を触らない間 */
#define TB_SCREEN_DEAD 0x0008 /*!< `p_ptr->is_dead` */
#define TB_SCREEN_DUNGEON 0x0010 /*!< `character_dungeon`。`cave[][]` に階が載っている */
#define TB_SCREEN_PLAYING 0x0020 /*!< `p_ptr->playing` */
/*! @} */

/*!
 * @brief いまの画面の状態（上のビットの論理和）。
 *
 * @details `character_icky` は **`bool` の顔をした計数器**である（`util.c:3291` の
 * `screen_save()` が `character_icky++`、`screen_load()` が `--`）。だから
 * `== TRUE` で比べてはいけない——先方のコード自身が `cmd7.c:5080` で
 * 「screen_save() は character_icky++ なので TRUE/FALSE 比較では効かない」と
 * 注記している。ここでは非 0 かどうかだけを見る。
 */
int tb_screen_flags(void);

/*!
 * @brief 画面の退避の深さ（`character_icky`。`screen_save()` が増やし `screen_load()` が減らす計数器）。
 * @details 見る・狙うの最中に `r`（モンスターの思い出）が画面を重ねている間を、見るの帯を作る側が見分けるために使う
 * （`presentation/bridge/look_screen_cover.h`）。
 */
int tb_screen_depth(void);

/*!
 * @brief 「前と変わったか」を安く判るための digest（設計の P2 申し送り 6）。
 *
 * @details `capture_frame()` は待ちの間も 10ms ごとに回る。地図が入ると 1 回の
 * capture が 1,000 マス超になり、**変化していないフレームを毎回組み直して
 * 文字列で比べる**のは高くつく。ここで安い値を作り、変わっていなければ
 * capture ごと省く。
 *
 * 材料は「画面に出るものが変われば必ず動くもの」だけ:
 * Term の画（コアが地図も含めて自分で描いた結果）・カーソル・`turn`・
 * メッセージ本数・@ の位置と HP・icky/generated。
 * @note 衝突しても実害は「1 フレーム古い画が残る」程度だが、`turn` を混ぜてあるので
 * ゲームが進んでいる間は必ず動く。
 */
unsigned long long tb_change_digest(void);

/*!
 * @brief キー配列がローグライクか（`rogue_like_commands`）。
 * @details `pad_commands.current_keymap`（v1 §8）に載せる。ゲーム中に切り替わるので、
 * capture のたびに見て、変わっていたら表を送り直す。
 */
int tb_rogue_like_commands(void);

/* ==================================================================== 地図（P3） */

/*!
 * @name `tb_map_cell::feature_flags` のビット（設計 §4.2）。
 *
 * @details `presentation/frame/cell_feature_bits.h` の `CELL_FEAT_*` と**意味は同じ**だが、
 * ここは純 C の境界なので値を独立に定義する。翻訳表は `tb_frame.cpp` の
 * `translate_feature_flags()` 1 か所だけにある（**どちらかの並びが変わっても
 * そこを直せば済む**ようにするための独立である。値を揃えて素通しにしない）。
 * @{
 */
#define TB_FEAT_WALL 0x0001u /*!< `FF_WALL`（26） */
#define TB_FEAT_DOOR 0x0002u /*!< `FF_DOOR`（18） */
#define TB_FEAT_DOOR_OPEN 0x0004u /*!< `FF_DOOR` かつ `FF_CLOSE`（9）＝「閉じられる」＝いま開いている */
#define TB_FEAT_STAIRS 0x0008u /*!< `FF_STAIRS`（20）/ `FF_ENTRANCE`（107）/ `FF_QUEST_ENTER`（98） */
#define TB_FEAT_PERMANENT 0x0010u /*!< `FF_PERMANENT`（27） */
#define TB_FEAT_TREE 0x0020u /*!< `FF_TREE`（83） */
#define TB_FEAT_WATER 0x0040u /*!< `FF_WATER`（39） */
#define TB_FEAT_LAVA 0x0080u /*!< `FF_LAVA`（40） */
#define TB_FEAT_GLOW 0x0100u /*!< `FF_GLOW`（37） */
#define TB_FEAT_PLAYER 0x0200u /*!< @ の居るマス */
#define TB_FEAT_KNOWN 0x0400u /*!< 地形が見えている（`map_info` と同じ判定） */
#define TB_FEAT_PASSABLE 0x0800u /*!< `FF_MOVE`（2）かつ壁・扉・木・岩でない */
#define TB_FEAT_RUBBLE 0x1000u /*!< `FF_HURT_ROCK`（44）かつ壁でも扉でもない（＝瓦礫） */
#define TB_FEAT_ROOM 0x2000u /*!< `CAVE_ROOM` */
#define TB_FEAT_GLOWING 0x4000u /*!< `CAVE_GLOW` かつ `CAVE_MNDK` でない */
#define TB_FEAT_SHAFT 0x8000u /* FF_SHAFT (101): moves two levels */
/*! @} */

/*!
 * @brief 地図 1 マス。**中身はすでに「見えているぶんだけ」**（未知は 0 詰め）。
 * @details `MapCellView`（`presentation/frame/map_cell_view.h`）の材料。
 * 見た目（`ascii` / `fg` / `bg`）は `map_info()` の返り値そのままで、
 * ID は `cave[y][x]` の直読み。**視認していない敵・拾っていないアイテムは
 * 入れない**（画面側へ漏らさない。設計 §4）。
 */
typedef struct tb_map_cell {
    short gx;
    short gy;
    unsigned short terrain_id; /*!< `get_feat_mimic()`（ミミック解決後の feat） */
    unsigned short feature_flags; /*!< 上の `TB_FEAT_*` */
    unsigned short monster_id; /*!< `m_ptr->ap_r_idx`。**`ml` のときだけ**。0 = 居ない／見えない */
    unsigned short monster_slot; /*!< `m_idx`（個体の通し番号）。条件は `monster_id` と同じ */
    unsigned short object_id; /*!< 先頭の `OM_FOUND` オブジェクトの `k_idx`。0 = 無い */
    /*!
     * @brief 正体の分からないアイテムの分類（1..8）。0 = 対象外（識別済み・8 分類の外）。
     * @details。番号は
     * `presentation::UnawareItemCategory` と同じ並び（1=薬 2=巻物 3=指輪 4=アミュレット
     * 5=ワンド 6=杖 7=ロッド 8=キノコ）。プロトコル v1 の外形は変えない
     * （コア内部の `tb_shim.c → tb_frame.cpp` の受け渡しにだけ足した値）。
     */
    unsigned char object_unaware_category;
    unsigned char light_level; /*!< 0 = 記憶のみ / 1 = 視界内だが無灯 / 2 = いま照明あり */
    /*!
     * プレイヤの光源で照らされているマス（`CAVE_LITE`）なら 1。明るい部屋（`CAVE_GLOW`）や
     * モンスターの光（`CAVE_MNLT`）は含めない。`tb_frame.cpp` が `lighting.player_lit` のビット列にする
     *
     */
    unsigned char lit_by_player;
    /*!
     * @brief エレメントスコープ（`p_ptr->action == ACTION_ELEMSCOPE`）の最中に、このマスへ地面の四角を重ねるときの色。
     * @details 0 = 重ねない（スコープ中でない／見えも覚えてもいないマス）、1〜16 = `TERM_*` の色 + 1。
     * 色は**原作の `map_info()` が返した `a`**（スコープ中は `map_elem_attr()` の結果に差し替わる）をそのまま読む。
     */
    unsigned char elem_tint;
    unsigned char fg_color; /*!< `map_info` の `a & 0x0F` */
    unsigned char bg_color; /*!< `map_info` の `ta & 0x0F`（地形層） */
    char ascii; /*!< `map_info` の `c`（SJIS のバイト 1 つ。全角の片割れも来うる） */
} tb_map_cell;

/*!
 * @brief いまの階の大きさ（`cur_hgt` / `cur_wid`）。
 * @return TB_OK / TB_ERR_STATE（階がまだ無い）
 */
int tb_floor_size(int *width, int *height);

/*! @brief @ の位置（`py` / `px`）。@return TB_OK / TB_ERR_STATE */
int tb_player_pos(int *x, int *y);

/*!
 * @brief 位置の確認（`L`。原作の `do_cmd_locate()`）の最中か（修-64）。
 * @return 最中なら 1（`*cx`/`*cy` に見ている区画の中心のマス）。そうでなければ 0。
 * @details 行 0 の「Map sector」（日本語は「マップ位置」）で見分ける。原作は変えない。
 */
int tb_locate_panel_center(int *cx, int *cy);

/*!
 * @name カーソルのマスのポップアップ（狙う・調べるとき。決めたこと 2026-10-18）
 * @details 原作の `target_set_aux()`（`xtra2.c`）が、そのマスを調べる**最初の段**に出すものと同じ中身を、色つきの字で渡す（狙う `*` 用）。
 *   - 幻覚中 … 「何か奇妙な物」　・　見えているモンスターがいるマス … 「[あと何回]ターゲット:名前(レベル???, 無傷, 属性の名前)」（属性の名前はその属性の色）
 *   - 自分のおとり … 「ダミー人形」　・　床の物 … 1 つなら物の名前、山なら「N個のアイテム」
 *   - それ以外（地形の段） … `(火+02,水+00,地-04,風+08,攻+40,防+15)`（いちばん強い属性の項目だけその属性の色）
 *   どれも操作の案内（`[r思 …]` など）は付けない。
 * 原作の `prt_grid_elem()` は Term へ描くだけで呼べないので、公開の部品から同じ字を組む。字は SJIS（英語層が生きているときは英語の綴り）。
 * 色は `TERM_*`（0〜15）。見る `l` は段が空白で切り替わるので、これではなく原作が行 0 に出す字そのもの（`tb_frame.cpp`）を使う。
 * @{
 */
typedef void (*tb_info_span_fn)(void *user, int color, const char *sjis_text);
typedef void (*tb_info_newline_fn)(void *user);
void tb_elem_info(int y, int x, tb_info_span_fn span, tb_info_newline_fn newline, void *user);
/*!
 * @brief 主 Term のカーソルが指している地図のマス（見る `l` のカーソル。原作は `move_cursor_relative()` で Term のカーソルを動かす）。
 * @return 地図の区画の中なら 1（`*gx`/`*gy` に階の座標）。そうでなければ 0。
 */
int tb_cursor_cave_cell(int *gx, int *gy);
/*! @} */

/*!
 * @brief @ の**クラスと種族**（`p_ptr->pclass` / `p_ptr->prace`）。
 * @return TB_OK / TB_ERR_STATE（誕生前）
 * @details プレイヤの絵をクラス別にするために要る（ （幻想蛮怒）` §5）。
 * 階が無くても答えられる（`character_dungeon` を見ない）ので、誕生直後でも引ける。
 */
int tb_player_kind(int *pclass, int *prace);

/*!
 * @brief @ の**性別**（`p_ptr->psex`。原作の `SEX_FEMALE`=0・`SEX_MALE`=1）。
 * @return TB_OK / TB_ERR_STATE（誕生前）
 * @details プレイヤの絵を職業×性別で引くために要る（M1。新しい絵は職業ごとに男女を描き分けている）。
 */
int tb_player_sex(int *psex);

/*! @brief Character-creation race score multiplier (percent), or -1. */
int tb_birth_race_score_mult(int race_index);
const char *tb_birth_race_title(int race_index);
int tb_birth_race_score_mult_named(const char *title);

/*!
 * @brief 視界の矩形を読む。
 * @param x0,y0 左上（階の外にはみ出してよい。はみ出しは 0 詰めで返る）
 * @param w,h 大きさ
 * @param out `w * h` 個の受け皿（走査順は y 外側・x 内側）
 * @param cap `out` の要素数
 * @return 埋めたマス数、または TB_ERR_*
 *
 * @note `map_info()` は**純粋関数ではない**。幻覚（`p_ptr->image`）と多色モンスターで
 * `randint` を引く（`cave.c` の `image_monster` / `ATTR_MULTI`）。変愚側の Bridge も
 * 同じものを毎フレーム呼んでいるので流儀は揃えたが、乱数が進むことは承知しておく。
 */
int tb_read_map(int x0, int y0, int w, int h, tb_map_cell *out, int cap);

/*!
 * @brief ルーンウェポンの絵の番号の下駄。`TB_ART_RUNEWEAPON_BASE + n`（n = 1〜3、原作の `MAX_RUNEWEAPON`）。
 * @details 原作の番号（`max_r_idx + n - 1`）は r_info の件数に付いて動くので、絵の対応表（`mapping.csv` の R 行）には
 * 載せず、この固定の番号で引く。`tools/toband/build_mapping.py` の `RUNEWEAPON_ART_BASE` と対。
 * 武器の種類が分かるときは下の `TB_ART_RUNEWEAPON_KIND_BASE` の絵を先に使い、これは**知らない種類のときの受け皿**になる。
 */
#define TB_ART_RUNEWEAPON_BASE 10000

/*!
 * @brief ルーンウェポンの武器の種類ごとの絵の番号の下駄。`TB_ART_RUNEWEAPON_KIND_BASE + k`（k = 0〜9）。
 * @details 原作の `create_runeweapon`（`monster2.c`）が作る武器 10 種。k は
 * 魔剣 0・魔爪 1（`TV_SWORD`）／魔弓 2・魔銃 3（`TV_BOW`）／魔槌 4・魔鞭 5・魔杖 6・魔扇 7（`TV_HAFTED`）／魔槍 8・魔斧 9（`TV_POLEARM`）。
 * `tools/toband/build_mapping.py` の `RUNEWEAPON_KIND_BASE` と対。
 */
#define TB_ART_RUNEWEAPON_KIND_BASE 10100
#define TB_ART_RUNEWEAPON_KIND_COUNT 10

/*!
 * @brief アストラルモードの 1 枠目（祖先の体）の絵の番号の下駄。`TB_ART_PLAYER_RACE_BASE + 種族`。
 * @details 祖先の体はプレイヤーと同じ人型なので、**その種族のプレイヤーの絵**を引く。
 * `resolve_tile_index`（`tb_frame.cpp`）がこの帯の番号を見たら、モンスターの表（`R`）ではなく
 * プレイヤーの表（`P`、`1000 + 種族`）を引く。種族の絵が無ければ `@` の板（`P,9999`）になる。
 */
#define TB_ART_PLAYER_RACE_BASE 11000

/*!
 * @brief モンスターの種族番号から、**絵を引くための番号**を返す。
 * @details 多くのモンスターは番号そのままを返す。次の 2 つだけ別の番号を返す。
 *   - ランダムユニーク（`MON_RANDOM_UNIQUE_1〜3` = 989〜991）: 生成のたびに記号・種族の旗・レベルが変わる枠なので、
 *     いま生成されている中身に**いちばん近い既存のモンスター**の番号を返す（規則は `tb_shim.c` の `tb_random_unique_art_id`）。
 *   - ルーンウェポン（`monster_is_runeweapon`）: 武器の種類ごとの絵 `TB_ART_RUNEWEAPON_KIND_BASE + k`（10 種）。
 *     アストラルモードの 1 枠目（祖先の体）だけは `TB_ART_PLAYER_RACE_BASE + 種族`（プレイヤーの絵を引く）。
 *     知らない種類のときは `TB_ART_RUNEWEAPON_BASE + n`。
 * 画面へ送る `monster_id` そのものは変えない（個体の識別・音・図鑑は原作の番号のまま）。絵を決める
 * `resolve_tile_index`（`tb_frame.cpp`）だけがこの値を使う。
 * @note 環境変数 `TB_ART_TRACE` が空でなければ、ランダムユニークの選び結果と、ルーンウェポンの枠ごとの選び結果（`[toband-art] runeweapon ...`）を stderr へ出す（検査用）。
 */
int tb_monster_art_id(int r_idx);

/*!
 * @brief `tb_monster_art_id` の絵が引けなかったときの、**次に引く番号**を返す。
 * @details ルーンウェポン（祖先の体を除く）なら `TB_ART_RUNEWEAPON_BASE + n`（従来の絵 TRW1〜3）、それ以外は `r_idx` そのもの。
 */
int tb_monster_art_fallback_id(int r_idx);

/*!
 * @brief ルーンウェポンの武器（`tval`・`sval`）から、種類の番号 k（0〜9）を返す。知らない種類は -1。
 * @details 表は `TB_ART_RUNEWEAPON_KIND_BASE` の注を参照。
 */
int tb_runeweapon_kind(int tval, int sval);

/*!
 * @brief ルーンウェポンの枠 `n`（1〜3）の絵の番号を決める（ゲーム状態を読まない純粋な関数。自己検査と `tb_monster_art_id` が使う）。
 * @param astral アストラルモードか
 * @param race 枠の祖先の種族（`runeweapon_list[n].race`）
 * @return アストラルモードの 1 枠目なら `TB_ART_PLAYER_RACE_BASE + race`、武器の種類が分かれば `TB_ART_RUNEWEAPON_KIND_BASE + k`、
 *         知らない種類なら `TB_ART_RUNEWEAPON_BASE + n`
 */
int tb_runeweapon_art_for(int n, int tval, int sval, int astral, int race);

/*!
 * @brief ルーンウェポンの絵の番号の自己検査（`--selftest`）。武器 10 種の全数・知らない種類・祖先の体を当てる。
 * @param err 最初に落ちた件の説明を書く（空きは `cap`）
 * @param checked 当てた件数を書く（NULL 可）
 * @return 落ちた件数
 */
int tb_runeweapon_art_selftest(char *err, int cap, int *checked);

/*!
 * @name ミニマップの種別コード。`MinimapKind`（`minimap_snapshot.h`）と同じ意味。
 * @details 値の独立の理由は `TB_FEAT_*` と同じ。翻訳は `tb_frame.cpp`。
 * @{
 */
#define TB_MM_UNKNOWN 0
#define TB_MM_FLOOR 1
#define TB_MM_WALL 2
#define TB_MM_DOOR 3
#define TB_MM_STAIRS 4
#define TB_MM_ITEM 5
#define TB_MM_MONSTER 6
#define TB_MM_PLAYER 7
#define TB_MM_MOUNTAIN 8 /*!< `FF_MOUNTAIN`（102）。壁だが山（町の読み方が使う） */
/*! @} */

/*!
 * @brief 階の全域をミニマップの種別コードで写す。
 * @param out `width * height` バイトの受け皿
 * @param cap `out` の容量
 * @return 書いたバイト数、または TB_ERR_*
 * @details **見たマスだけ**を出す（未踏破を出すと未探索領域を画面側が暴露する）。
 */
int tb_read_minimap(unsigned char *out, int cap);

/*!
 * @brief 階の素性（`FloorIdentity` の材料）。文字列は SJIS。
 */
typedef struct tb_floor_info {
    int dungeon_id; /*!< `dungeon_type`。地上は 0 */
    int dun_level;
    int kind; /*!< 0 不明 / 1 地上 / 2 ダンジョン / 3 クエスト階 / 4 闘技場 */
    int town_id; /*!< `p_ptr->town_num`。町の中でなければ 0 */
    int wild_mode; /*!< `p_ptr->wild_mode`（広域マップ） */
    char place_name[96]; /*!< 現在地名（`tb_read_hud` の `place` と同じ） */
    int day_minute; /*!< 0〜1439。`extract_day_hour_min()` の 時*60+分 */
    int daytime; /*!< `is_daytime()` */
    int light_radius; /*!< `p_ptr->cur_lite` */
    /*!
     * 光源の種類（`LightingState::light_kind`）。0 = 光が無い、1 = 松明、2 = ランタン（魔法のランタン・行灯を含む）、
     * 3 = アーティファクトの光源、4 = それ以外（光源の枠が空・燃料切れでも残る、種族・クラス・装備の光）。
     */
    int light_kind;
} tb_floor_info;

/*! @brief 階の素性と光。@return TB_OK（失敗しない。ゲーム前は 0 詰め） */
int tb_read_floor_info(tb_floor_info *out);

/*!
 * @brief 天候（原作の `weather[WEATHER_RAIN]`・`weather[WEATHER_WIND]`・`weather[WEATHER_TEMP]` の生の値）。
 * @param out 3 つの値（雨・風・気温。0〜15。気温は大きいほど寒い）。範囲外は 0〜15 に丸める
 * @return 1 = 読めた（キャラクター作成済み）／0 = まだ無い（`out` は 0 詰め。画面へ申告しない）
 * @details 原作の変数を**読むだけ**。段への丸め（`weather_level`）は画面側がやる。
 */
int tb_read_weather(int out[3]);

/*!
 * @brief 光源の種類の判定（`tb_floor_info::light_kind`）を、作った物で全数照合する。
 * @param err 最初に外れた組み合わせの名前を入れる（失敗が無ければ空）
 * @param cap `err` の容量
 * @return 外れた組み合わせの数（0 なら全部合っている）
 * @details キャラクターは要らない。`--selftest` から呼ぶ（`lib` が読めていれば足りる）。
 */
int tb_light_selftest(char *err, int cap);

/* ============================================ 状態のアイコン（`GameFrame::status_effects`） */

/*! @brief 状態のアイコン 1 個ぶん。`key` は `hd2d/ui/status_icon_table.inc` にある名前。 */
typedef struct tb_status_effect {
    char key[40];
    char label[64]; /*!< アイコンの下の名前。CP932（英語なら ASCII） */
    char amount[16]; /*!< アイコンの上の効果量（`+10` など）。無ければ空 */
    int kind; /*!< 0 良い・1 悪い・2 待っている・3 特殊・4 常時の良い・5 常時の悪い（`StatusEffectKind`） */
} tb_status_effect;

/*! 1 回に返す最大数（状態の旗 62 + 常時の効果 81 + 速さ・空腹。余裕を見て 320）。 */
#define TB_STATUS_MAX 320

/*!
 * @brief いまの状態を並べて返す（速さ → 状態の旗（切り傷・朦朧を含む） → 空腹 → 常時の効果）。
 * @return 件数。キャラクターがいなければ 0
 * @details 一時的な状態の旗は `prt_status()` が最後に組んだもの（`tools/targeting/overlay.py` の 1 行が
 * `tb_status_record()` へ渡す）。速さ・空腹は原作が画面へ出した字を読む。常時の効果は鑑定済みの旗だけ。
 */
int tb_read_status_effects(tb_status_effect *out, int cap);

/*! @brief `prt_status()` から呼ばれる記録の関数（overlay.py が足す 1 行の呼び先）。`bar < 0` は組み直しの始まり。 */
void tb_status_record(int bar, const char *label, int attr);

/*! @brief 旗の組が変わったら変わる値（`tb_change_digest` に混ぜる）。 */
unsigned long tb_status_stamp(void);

/*! @brief 状態のアイコンの表の自己検査（キャラクター不要）。@return 外れた件数 */
int tb_status_selftest(char *err, int cap, int *checked);

/*! @brief 表を読む関数（検査用）。`BAR_*` の数・鍵・種類。 */
int tb_status_bar_table_count(void);
const char *tb_status_bar_table_key(int bar, int *kind);
/*! @brief 常時の効果の表を読む関数（検査用）。 */
int tb_status_perm_row_count(void);
const char *tb_status_perm_row_key(int row, int *kind, const char **imm_key);

/* ============================================ 周囲の地形の内訳（環境音の層） */

/*!
 * @brief 周囲の地形の内訳（`presentation/frame/game_frame.h` の `SurroundingsView`）。
 * @details 環境音は「いま周りに何が在るか」で層を重ねる
 *。**数えるのはコア側の仕事**である——
 * 地形の意味（草か土か沼か）を知っているのは `f_info` を読める側だけで、
 * 画面側は地形テーブルを持たない。
 *
 * 割合は 0..255 が 0..100%。**合計は 100% を超えうる**——壁とガラスは材料とは
 * 別勘定で、ガラスの壁は壁でもありガラスでもあるからである。
 */
typedef struct tb_surroundings {
    unsigned char grass; /*!< 草・薮・花 */
    unsigned char tree; /*!< 立ち木 */
    unsigned char dirt; /*!< 土・砂 */
    unsigned char swamp; /*!< 沼 */
    unsigned char water; /*!< 浅い水 */
    unsigned char deep_water; /*!< 深い水。**海鳴りの層はこれで決まる** */
    unsigned char lava; /*!< 溶岩（浅い・深いをまとめる） */
    unsigned char rock; /*!< 山・瓦礫 */
    unsigned char glass; /*!< ガラス */
    unsigned char wall; /*!< 壁。**閉塞の層はこれで決まる** */
    unsigned char radius; /*!< 数えた半径。**0 なら「数えていない」** */
    unsigned short counted; /*!< 数えたマスの数（既知のものだけ） */
} tb_surroundings;

/*!
 * @brief 周囲を数える。@return TB_OK（失敗しない。数えられなければ 0 詰め）
 * @details 変愚の `fill_surroundings()`（`presentation_bridge.cpp`）と**同じ数え方**に
 * してある。ずれると同じ場所で音が食い違う。気をつけたのは 4 つ:
 *
 * - **既知のマスだけ**。未探査を混ぜると**まだ見ていない海の音が鳴る**
 * - **円で数える**（正方形だと角のマスが遠いのに同じ重みになる）
 * - **沼を水より先に見る**。沼は水の旗も持っているので、順を逆にすると沼が消える
 * - **山は壁でもある**。壁として数えたうえで岩としても数える
 *
 * 読むだけで、コアの状態は 1 つも変えない（設計 §1 制約 1）。
 */
int tb_read_surroundings(tb_surroundings *out);

/* ============================================================== セーブの口（P3） */

/*!
 * @brief セーブの置き場（`ANGBAND_DIR_SAVE`）。`tb_bootstrap()` の後に呼ぶこと。
 * @return 静的な文字列（SJIS）。まだ決まっていなければ空
 */
const char *tb_save_dir(void);

/*!
 * @brief 使うセーブ枠を決める（設計 §3.2 版 1.1 の「固定スロット」）。
 * @param slot 枠の名（ディレクトリを含まない。`player` 等）
 * @return TB_OK / TB_ERR_ARG / TB_ERR_STATE（`init_file_paths` 前）
 *
 * @details `savefile` と `savefile_base` の両方を埋める。`process_player_name()`
 * （`files.c:10184`）は**両方が埋まっていて `sf` が偽のときだけ**訊かずに通るので、
 * ロード経路（`play_game(FALSE)`）では聞かれない。
 * **新規（`play_game(TRUE)`）では先方が必ず名前を訊く**（`mod140316` の改造。
 * `birth.c:7818` が `process_player_name(creating_savefile=TRUE)` を呼ぶ）ので、
 * ここで決めた枠は新規作成では使われない。P3 で照合した事実。
 */
int tb_set_savefile(const char *slot);

/*!
 * @brief セーブ枠の指定を外す（**新規作成のときは必ずこちら**）。
 *
 * @details `play_game()` は `new_game` に関わらず先頭で `load_player()` を呼び、
 * 失敗すると `quit("セーブファイルが壊れています")` を踏む（`dungeon.c:10271`）。
 * `savefile` が空のときだけ `load_player()` は `save.c:2265` の
 * `if (!savefile[0]) return (TRUE);` で素通りする。つまり
 * **「無い枠の名を据えたまま新規で始める」ことはできない**。
 * `main-win.c` の [ファイル]→[新規]（:4374）も `savefile` を空のまま呼んでいる。
 * P3 で実際に踏んで確かめた（据えたまま `play_game(TRUE)` を呼ぶと即 quit する）。
 */
void tb_clear_savefile(void);

/*!
 * @brief いま使っているセーブ枠の名（`savefile_base`）。
 * @return 静的な文字列（SJIS）。決まっていなければ空
 * @details 新規作成では**プレイヤが誕生画面で付けた名**がここに入る。
 * 次回の起動で続きから遊べるように、呼び出し側がこれを覚えておく。
 */
const char *tb_savefile_base(void);

/*!
 * @brief いま使っているセーブの道（`savefile`。ディレクトリ込み）。
 * @return 静的な文字列（SJIS）。決まっていなければ空
 * @details 呼び出し側が**在るか無いかを見て `play_game()` の引数を決める**のに使う。
 * `play_game(FALSE)` を無い道に対して呼ぶと、Windows では `load_player()` が
 * `fd_open` で落ちて `quit("セーブファイルが壊れています")` になる（P3 で照合した事実）。
 */
const char *tb_savefile_path(void);

/* ======================================================= ゲーム前画面の描き口（P5） */

/*!
 * @brief Term を消す（`Term_clear()`）。タイトル＋セーブ選択画面（P5）用。
 * @details 呼んでよいのは `tb_bootstrap()` の後・`tb_run_game()` の前だけ。
 * ゲーム中の Term はコアが持ち主で、横から描くと壊れる。
 */
void tb_term_clear(void);

/*!
 * @brief Term へ 1 行書く（`Term_putstr()`）。
 * @param attr コアの色番号（0 黒 / 1 白 / 2 灰 / 4 赤 / 11 黄 / 14 水色 …）
 * @param sjis 文字列（**SJIS**。UTF-8 は境界の手前で変換すること）
 */
void tb_term_putstr(int x, int y, int attr, const char *sjis);

/*!
 * @brief 描いた画を確定して frame に乗せる（`Term_fresh()`）。
 * @details 締めの `TERM_XTRA_FRESH` が null term 経由でフックの `present` を呼ぶ。
 */
void tb_term_present(void);

/* ================================== UI 既定パネルの中身（M1 / S1 の対） */

/*!
 * @name `tb_read_item_line()` の種別
 * @{
 */
#define TB_ITEMS_INVENTORY 0 /*!< 持ち物（`inventory[0 .. INVEN_PACK-1]`） */
#define TB_ITEMS_EQUIPMENT 1 /*!< 装備（`inventory[INVEN_RARM .. INVEN_TOTAL-1]`） */
/*! @} */

/*!
 * @brief 持ち物／装備の `index` 番目を 1 行の文字列にする（**SJIS**）。
 * @param what `TB_ITEMS_*`
 * @param index 0 起点。**空の枠は飛ばした後の順番**
 * @return 書いたバイト数。0 なら「そこには何も無い」（＝一覧の終わり）
 *
 * @details 画面側の**UI 既定パネル**（`sub2_lines` / `sub5_lines`）の材料。
 * サブウィンドウ（S1）を割り当てていないときに出るのがこちらで、**空だと
 * 枠だけが並ぶ**。変愚は `fill_sub2_equipment()` / `fill_sub5_inventory()` で
 * 同じものを埋めている（`presentation_bridge.cpp:163` / `:229`）。
 * 書式もあちらに合わせる——装備は 2 文字の枠名、持ち物は `a)` の添字。
 */
int tb_read_item_line(int what, int index, char *out, int cap);

/*!
 * @brief 視界にいるモンスターの `index` 番目の名前（**SJIS**）。
 * @return 書いたバイト数。0 なら「そこには何も居ない」
 * @details `sub3_lines` の材料。**視認しているものだけ**（`m_ptr->ml`）——
 * 見えていない敵を名前で漏らさない（設計 §4 の地図と同じ約束）。
 */
int tb_read_visible_monster(int index, char *out, int cap);

/* ================================================== 文字入力（M1 / S5） */

/*!
 * @brief 「いま自由文字入力の待ちだ」を立てる／下ろす。**コアが呼ぶ。**
 * @param active 1 = 待ちに入った / 0 = 抜けた
 *
 * @details 呼び元は `toband/src/util.c` の `askfor_aux()`（`inkey_special()` の
 * 前後 2 行。`//GB:` 印つき。設計書 §7 の許可列挙）。**先方の木への手入れはここだけ**である。
 *
 * ## なぜ手入れが要ったか
 * 画面側は**非 ASCII を `frame.text_input_active` のときだけ送る**
 * （`hd2d/app/hd2d_app.cpp:11086`）。コマンドを待っている所へ 2 バイト文字を流すと、
 * コアはそれを 2 つのコマンドとして読むからである（`iskanji` の判定は文字入力の中にしかない）。
 * この旗が立たない限り**日本語は 1 文字も打てず、IME の窓も出ない**。
 *
 * コアの側に「いま自由入力中」を表す印は無い。`inkey_flag` は「コマンド待ち」の否定でしかなく、
 * `y/n` やアイテム選択の待ちでも下りている——そこで日本語を通すと 2 コマンドに化ける。
 * `character_icky` も同じで、全画面かどうかしか言わない。だから**待ちそのものを囲む**
 * 2 行を入れた。旗が立っているのは「その待ちがキーを待っている間」ちょうどである。
 *
 * @note 囲んだ待ちは 4 つ（R2。`GENSOBAND_SYSTEM_ACCEPT.md（幻想蛮怒）` §3.5）:
 * `util.c` の `askfor_aux()`、`autopick.c` の `do_cmd_edit_autopick()` 本体と
 * `get_string_for_search()`、`birth.c` の `edit_history()`。
 * **マクロ編集は要らない**——文字入力はすべて `askfor()` 経由である。
 */
void tb_text_input_set(int active);

/*! @brief いま自由文字入力の待ちか（`frame.text_input_active` の材料）。 */
int tb_text_input_active(void);

/* ================================================== 音（M1 / S10・a） */

/*!
 * @name 効果音と BGM
 *
 * **何を鳴らすかを決めるのはコア**である（`util.c` の `play_music()` /
 * `select_floor_music()` / `sound()`）。ここは `Term_xtra` で届いた分類と番号を
 * 設定ファイルの行に引き当てて鳴らすだけ。実体は `tb_audio.c`。
 *
 * 素材は**幻想蛮怒には付いてこない**（`lib/xtra/{music,sound}` に在るのは cfg だけ）。
 * 作者の指示は「本家の music フォルダをそのままコピーしてくればよい」で
 * （`change2.txt` v1.1.58）、その本家の素材は CC0 / CC BY である
 * （`lib/xtra/music/readme.txt`。OpenGameArt 由来）。
 * @{
 */

//! 設定ファイルを読む。**`init_angband()` の後**（`ANGBAND_DIR_XTRA_*` が決まってから）。
void tb_audio_init(void);

//! コアが音の縁を投げてくるようにする（`use_sound` / `use_music`）。**起動時に 1 回。**
void tb_audio_enable_core(void);

//! 画面側の設定（`ui_state.audio`）を受ける。切ったら鳴っている曲も止める。
void tb_audio_set_enabled(int sound_on, int music_on);

/*!
 * @brief 音量を受ける（`ui_state.audio` の添字。**0 ＝ 100% … 9 ＝ 10%**）。効果音と BGM の両方に効く。
 * @details 当て方は 2 つで別である。**BGM は MCI に音量の引数がある**ので、開いた口へ
 * `MCI_DGV_SETAUDIO_VOLUME` を送るだけ。**効果音は `PlaySound` に引数が無い**ので、
 * 渡す前に **PCM の振幅を書き換える**（`tb_scale_wav`。縮め方は変愚の
 * `modulate_amplitude` と同じ計算なので、同じ段なら同じ大きさで鳴る）。
 * @note 最大の段のときは wav を 1 バイトも触らず、素のファイルを `SND_FILENAME` で渡す。
 */
void tb_audio_set_volume(int sound_index, int music_index);

//! 効果音を 1 つ鳴らす（`TERM_XTRA_SOUND` の値）。@return 0 = 鳴らした
int tb_audio_play_sound(int v);

//! BGM を替える（`TERM_XTRA_MUSIC_*` と番号）。**同じ曲なら何もしない。**@return 0 = 替えた
int tb_audio_play_music(int n, int v);

//! 鳴っている曲を止める。
void tb_audio_stop_music(void);

/*!
 * @brief 曲の終わりを取りに行く（＝ループさせる）。**周回ごとに呼ぶこと。**
 * @details MCI は終わりをメッセージで知らせてくるが、コアは窓を持たないので
 * 誰も回していない。ここを呼ばないと**曲が 1 回鳴って止まる**。
 */
void tb_audio_pump(void);

/*!
 * @brief そのモンスター専用の曲があるか（`find_mon_music_priority` の中身）。
 * @return `MON_MUSIC_PRIOR_NONE` / `_LOW` / `_MED` / `_HIGH`
 */
int tb_audio_mon_music_priority(int r_idx);

//! 読み込めた項目の数（検査用）。@return 1 = 読み込み済み
int tb_audio_entry_counts(int *sound, int *music);
/*! @} */

/* ============================================ 戦闘の見せ場（M1 / S10・R4） */

/*!
 * @name 弾幕・被弾・命中の記録
 *
 * `GameFrame::combat_fx` の材料。**溜めるのはコア、汲むのは capture**（変愚の
 * `CombatFeedback`（`src/core/combat-feedback.h`）と同じ形）。汲んだら消す
 * ——`serial` で照合する作りにすると同じ縁を何度も拾って点滅し続ける。
 *
 * ## 属性は先に宣言する
 * `take_hit()` の呼び出しは幻想蛮怒でも 200 か所を超えるので引数は増やせない。
 * 代わりに「次に起きる被弾はこの属性」を先に置いてもらう（`tb_fx_set_pending`）。
 * 置く場所は 2 つで全部覆える:
 * - 遠隔・呪文・ブレス … `project_p()`（`typ` を既に受け取っている）
 * - 近接打撃 … `make_attack_normal()`（`mbe_info[効果].explode_type`）
 *
 * 宣言が無ければ物理に落ちる。罠・落下・飢餓はそれでよい。
 * @{
 */

//! 溜めておける数。あふれたら**古いほうを捨てる**（新しい出来事のほうが見たい）。
#define TB_FX_MAX 32

//! 1 件。`typ` は幻想蛮怒の `GF_*`（`defines.h:4145` 以降）。束ねるのは C++ 側。
typedef struct tb_fx_event {
    int kind; /*!< 0 = 被弾 / 1 = 命中 / 2 = 飛道（`CombatFxKind` と同じ番号） */
    int typ; /*!< `GF_*`。-1 は「宣言なし＝物理」 */
    int y, x; /*!< 起きたマス（飛道は着弾側） */
    int src_y, src_x; /*!< 飛道の撃ったマス。ほかは y/x と同じ */
    int num, den; /*!< 強さ ＝ num / den（割合。0 除算は C++ 側で見る） */
} tb_fx_event;

//! 次に起きる被弾の属性を宣言する。`tb_fx_player_hit()` が消費して消す。
void tb_fx_set_pending(int typ);

//! ＠が受けた。`damage` と `mhp` から強さを出す。`melee` は近接なら 1。
void tb_fx_player_hit(int damage, int mhp, int melee);

//! モンスターに当てた。`max_hp` はその個体の最大 HP。
void tb_fx_monster_hit(int y, int x, int dam, int max_hp);

//! 弾・矢・ブレスが飛んだ。**1 発につき 1 件**（1 コマずつは積まない）。
void tb_fx_bolt(int src_y, int src_x, int y, int x, int typ);

/*!
 * @brief `GF_*` を画面側の束（`CombatFxElement`）の番号へ落とす。
 * @return 0=物理 1=火 2=冷 3=電 4=酸 5=毒 6=闇 7=光 8=混沌 9=精神
 * @details 束は 10 種、`GF_*` は 170 種以上ある。**見て分かる粒度**へ落とすのが
 * こちらの仕事で、色を決めるのは画面側である（`presentation/frame/combat_fx.h`）。
 * ここに置いてあるのは、番号の正が `defines.h`（C のマクロ）だからである
 * ——C++ 側へ写すと「写しが古い」事故が起きる。
 *
 * 幻想蛮怒だけの属性の落とし先（`change.txt`「新属性」）:
 *
 * | 幻想蛮怒 | 束 | なぜ |
 * |---|---|---|
 * | 水（`GF_WATER` / `GF_WATER_FLOW` / `GF_HOLY_WATER`） | 冷 | 冷たい系の青。専用の束は無い |
 * | 破邪（`GF_HOLY_FIRE`） | 光 | 閃光と同じ白。炎ではない |
 * | 時空（`GF_TIME` / `GF_NEXUS`） | 混沌 | 変愚も因果混乱を混沌に入れている |
 * | 狂気（`GF_CONFUSION`） | 混沌 | 精神は `GF_PSY_SPEAR` のほう |
 * | 核熱（`GF_NUKE`） | 毒 | 変愚と同じ（核熱は毒の系譜） |
 * | 気（`GF_FORCE` / `GF_MANA`） | 物理 | 無属性。既定の落とし先 |
 *
 * **弾幕は無属性のボルト**（`change.txt`「敵のレベルの無属性弾」）なので
 * `GF_MISSILE` を通って物理に落ちる。
 */
int tb_fx_element_of(int typ);

//! 溜まっている件数（digest 用。**画が変わらない出来事を門で落とさない**ため）。
int tb_fx_pending_count(void);

/*!
 * @brief 溜まっているぶんを持ち出して空にする。@return 書いた件数
 * @param out 受け皿。@param cap その大きさ
 */
int tb_fx_take(tb_fx_event *out, int cap);
/*! @} */

/*!
 * @name 魔法・投射物の効果の入口（実装は tb_frame.cpp。`tb_shim.c` の `tb_fx_note_*` が呼ぶ）
 * @details 共通の記録の置き場（`presentation/bridge/named_attack_fx.h`）へ積む。`attr` は原作の属性のコア名（`GF_` を除いた小文字。
 * 見た目の名前への変換は C++ 側）。整数の並びは `presentation/bridge/named_attack_fx_glue.h` の冒頭。
 * @{
 */
void tb_named_fx_projection(int origin, int shape, const char *attr, int rad, int src_y, int src_x, int impact_y, int impact_x,
    int flags, const int *path, int path_n, const int *area, int area_n);
void tb_named_fx_missile_step(int first, int kind, int item_id, const char *brand, int src_y, int src_x, int y, int x);
void tb_named_fx_missile_hit(void);
void tb_named_fx_missile_returns(void);
/*! @} */

/* ==================================================================== 効果音 */

/*
 * **鳴らす担当は 2 通りある**。
 *
 * | 画面側の装置 | 誰が鳴らすか |
 * |---|---|
 * | 開けている（`ui_state.sound_events` が真） | **画面側**。ここは名前とマスを積むだけ |
 * | 開いていない（音の無い機・Android の空実装） | **コア**。従来どおり `tb_audio.c` が winmm で鳴らす |
 *
 * 切り替えは自動で、設定の項目は増やさない。画面側でしか位置の聞き分け（HRTF）が
 * できないので、開いているなら必ず向こうへ渡す。
 *
 * **曲はこの分岐に入らない。**`TERM_XTRA_MUSIC_*` は幻想蛮怒が持っている枝で、
 * 画面側にそれを受ける器が無い（`SoundEvent` は効果音の形である）。曲は今までどおり
 * `tb_audio_play_music()` が鳴らす。
 *
 * **音は名前で運ぶ**（番号ではない）。番号は変種ごとに別の音を指すので、番号で運ぶと
 * 画面側に変種ごとの対応表が要る。名前なら表は 1 つで済む。綴りは幻想蛮怒の
 * `angband_sound_name[]`（`variable.c`）そのままで、変愚の目録
 * （`assets/audio/sfx.jsonc`）に 45 名がそのまま当たる。
 */

/*! @brief 音 1 つ。`name` は `angband_sound_name[]` の綴り（NUL 終端）。 */
typedef struct tb_sound_event {
    char name[16]; /*!< 音の名前。空なら捨ててよい */
    short y; /*!< 鳴ったマス（いまは常に @ のマス＝頭で鳴る） */
    short x;
} tb_sound_event;

/*!
 * @brief 画面側が音の出来事を欲しがっているか（`ui_state.sound_events`）を覚える。
 * @details 偽の間は 1 つも積まず、`tb_audio_play_sound()` が今までどおり鳴らす。
 * **`use_sound` は落とさないこと**——コアの `sound()` はあれが偽だと `Term_xtra`
 * すら呼ばず、書き留める機会ごと消える（`util.c:1728`）。
 */
void tb_sound_set_wanted(int wanted);

/*! @brief 画面側が鳴らす側か。`tb_null_term.c` が枝を選ぶのに読む。 */
int tb_sound_wanted(void);

/*! @brief `TERM_XTRA_SOUND` を 1 回聞いた。`v` は `SOUND_*` の番号。 */
void tb_sound_push(int v);

/*! @brief 溜まっている音を汲んで消す。@return 書いた個数 */
int tb_sound_take(tb_sound_event *out, int cap);

/*!
 * @brief これまでに積んだ**累計**。digest に混ぜる。
 * @details 音は一度きりの出来事で、鳴った瞬間の Term が 1 セルも動かない場面がある。
 * 混ぜないと**その 1 枚が線に乗らず**、音が次の別の変化まで遅れる
 *
 */
unsigned long tb_sound_total(void);

/* ============================================== サブウィンドウ（M1 / S1） */

/*!
 * @brief `=`→`w` の画面で割り当てが変わった回数（R1）。
 * @details **増えたときだけ**「遊ぶ側が変えた」と見てよい。`window_flag[]` そのものは
 * `toggle_inven_equip()`（床の品物を選ぶたび）とセーブの読み込みも書くので、
 * 差分で見ると歩くだけで振動する。数を進めるのは `cmd4.c` の `//GB:` 1 行だけ。
 */
unsigned int tb_window_flags_generation(void);

//! `cmd4.c` の `do_cmd_options_win()` から呼ばれる（`//GB:`）。**他から呼ばない。**
void tb_window_flags_bumped(void);

/*!
 * @brief 立てるサブ Term の枚数。**画面側の `kSubPanelCount` と同じ 7** でなければ
 *        ならない（`presentation/frame/game_frame.h`）。C++ 側で突き合わせている。
 * @details 幻想蛮怒の `angband_term[]` は 8 枚で、0 番は本線が使う。ちょうど収まる。
 */
#define TB_SUB_PANELS 7

/*!
 * @brief 画素を出さないサブ Term を 7 枚立て、`angband_term[1..7]` に挿す。
 * @return TB_OK / TB_ERR_TERM
 * @details 呼んでよいのは `tb_bootstrap()` の**後**（`angband_term[]` の並びが
 * 決まってから）。立てるだけで `window_flag` は触らない。
 */
int tb_sub_terms_install(void);

/*!
 * @brief 画面側が望む桁・行を覚える（`ui_state.sub_panel_cells[i]`。v1 §7）。
 * @details **0 は「申告なし」**でいまの大きさを保つ。実際の付け替えは
 * `tb_sub_term_apply_cells()`（別の関数にしてあるのは、Term の活性を
 * 掻き回すのを capture の中の 1 か所に閉じ込めるため）。
 */
void tb_sub_term_set_cells(int panel, int cols, int rows);

/*! @brief 覚えた大きさを Term へ当てる。@return 1 = 変えた（＝中身は古い） */
int tb_sub_term_apply_cells(int panel);

/*! @brief いまの桁・行。@return TB_OK / TB_ERR_STATE */
int tb_sub_term_size(int panel, int *cols, int *rows);

/*!
 * @brief サブ Term の 1 行を写す（`tb_term_row` と同じ約束。SJIS のバイト列のまま）。
 * @return 写したセル数、または負の値
 */
int tb_sub_term_row(int panel, int y, char *text, unsigned char *attr, int cap);

/*!
 * @brief その枠に立っている種類（`window_flag` のビット番号）。無ければ -1。
 * @details 2 つ以上立っていたら小さい番号を返す。**決めておく**——決めずに
 * 「不定」にすると、同じ操作で違う板が出る形が残る。
 */
int tb_sub_current_kind(int panel);

/*!
 * @brief その番号を**そのまま枠に立てられるか**で整える。立てられないなら -1。
 * @details `tb_sub_set_kind()` は描画関数の無い番号（記念撮影・ボーグ）を落とす。
 * コア側で変えられた値を抱え込むとき、整えずに抱えると**永久に手綱が返らない**
 * ――画面へ返るのは落とされた後の -1 なので、画面がそれを送り返してきても
 * 抱えている値（落とされる前の番号）と一致しない。
 */
int tb_sub_sanitize_kind(int flag);

/*!
 * @brief 本線 Term（`angband_term[0]`）の `window_flag` を落とす。@return 1 = 落とした
 * @details `fix_inven()` ほかは `j = 0..7` を走査する（`xtra1.c:2523`）ので、
 * `=`→`w` の画面で 0 番の列に印を付けると**持ち物一覧が地図の上に描かれる**。
 * この作りでは 0 番は地図そのものなので、選ばせずに落とす。
 */
int tb_sub_clear_main_flags(void);

/*!
 * @brief その枠の `window_flag` を `flag` **1 つだけ**にする。@return 1 = 変えた
 * @param flag `window_flag_desc` のビット番号。**-1 と、描画関数の無い番号は「何も無し」**
 * @details `toggle_inven_equip()` が変えたぶんは次のフレームで戻る。**`=`→`w` で
 * 遊ぶ側が変えたぶんだけは戻さない**（R1。`tb_window_flags_generation` で見分ける）。
 */
int tb_sub_set_kind(int panel, int flag);

/*!
 * @brief 「サブウィンドウを描き直せ」の印を立てる（`p_ptr->window`）。
 * @details 種類や桁数を変えた直後に呼ぶ。**描くのはコア**（`handle_stuff()`）で、
 * ここから `window_stuff()` を呼びに行かない。
 */
void tb_sub_request_redraw(void);

/*!
 * @brief 種類の名前（`window_flag_desc`。SJIS）。**描画関数のある種類だけ**。
 * @return 書いたバイト数。0 なら「その番号は選ばせない」（記念撮影・ボーグ・予約）
 * @details 画面側の `sub_panel_kinds`（v1 §8.1）の材料。**一覧を画面に持たせない**
 * （コアが種類を増やしたら黙って追従する）。
 */
int tb_window_flag_name(int flag, char *out, int cap);

/* ================================================== コマンドの表（M1 / S3） */

/*!
 * @brief パッドに並べるコマンド 1 件（`menu_info` の 1 項目）。文字列は **SJIS**。
 * @details 変愚の `PadCommandEntry`（`presentation/frame/pad_command_table.h`）に当たる。
 */
typedef struct tb_pad_command {
    unsigned char command; /*!< オリジナル配列の Angband コマンド（`menu_info[][].cmd`）。**永続化の鍵** */
    unsigned char key_original; /*!< オリジナル配列で押すキー。**0 = そのコマンドは出せない** */
    unsigned char key_rogue; /*!< ローグライク配列で押すキー。同上 */
    char group[64]; /*!< 分類名（`menu_info[0][g].name`） */
    char label[64]; /*!< 項目名（`menu_info[sub][k].name`） */
} tb_pad_command;

/*!
 * @brief コアのコマンドメニュー（`menu_info[10][10]`。`util.c:4210`）を平坦化して読む。
 * @param out 受け皿
 * @param cap `out` の要素数
 * @return 書いた件数、または `TB_ERR_ARG`
 *
 * @details 変愚の `register_pad_command_table()`（`presentation/bridge/pad_command_table.cpp`）
 * と**同じ形**。`menu_info[0]` が分類（`fin` が偽で `cmd` が小メニュー番号）、
 * `menu_info[sub]` の `fin` が真の項目だけがコマンドである。
 *
 * ## 名前は `special_menu_info` を通す
 * 職と広域マップで名札が変わる（`util.c:4491`。超能力者なら「超能力/特殊能力」、
 * 広域マップに居るなら「通常マップ(>)」）。`inkey_from_menu()` が画面へ出すときの
 * 差し替えをそのまま写す——**画面に出ている名前とパッドの名札を食い違わせない**。
 *
 * ## キーは逆引きする（変愚 §2.1 の罠と同じ）
 * `cmd` は**押されたキーではなくキーマップ変換後のコマンド**である
 * （`util.c:4766` が `keymap_act[mode][cmd]` を見ている）。そのまま積むと
 * ローグライク配列で別のコマンドになる。だから `keymap_act[mode][0..255]` を走査して
 * 「動作がそのコマンド 1 文字ちょうど」のキーを探す。見つからず、しかも `cmd` 自身が
 * キーマップ済みなら **0（出せない）**を返す——黙って誤ったキーを積むより無反応が安全。
 */
int tb_read_pad_commands(tb_pad_command *out, int cap);

/*!
 * @brief 1 つのコマンドを押すためのキーを、いまのキーマップから逆引きする。
 * @param rogue 0 = オリジナル配列 / 非 0 = ローグライク配列
 * @param command Angband のコマンド文字（`^G` なら 7）
 * @return 押すキー。**0 = その配列では出せない**
 *
 * @details `tb_read_pad_commands()` が内側で使っているのと同じ逆引きを、
 * **`menu_info` に載っていない命令**のために外へ出したものである
 * （`tb_pad_commands.cpp` の `kExtras`。 （幻想蛮怒）` GU-01・GU-02）。
 * 逆引きの規則も同じ——「動作がそのコマンド 1 文字ちょうど」のキーを探し、
 * 見つからず、しかも `command` 自身がキーマップ済みなら **0** を返す。
 */
unsigned char tb_pad_key_for_command(int rogue, int command);

/*!
 * @brief 上の表の中身が変わったかを安く見るための印。
 * @details 名札は職・広域マップ・階の種類で変わり、キーは配列で変わる。
 * 毎フレーム 63 件を組み直して比べるのは高いので、材料だけを混ぜた値を返す。
 * **変わったら送り直す**（v1 §8 の pad_commands は再送してよい）。
 */
int tb_pad_table_stamp(void);

/* ============================================ 色表とカーソル（M1 / S6・S2） */

/*!
 * @brief コアの色表（`angband_color_table`。`variable.c:605`）を 16 色ぶん写す。
 * @param rgb `16 * 3` バイトの受け皿（R,G,B の順）
 * @param cap `rgb` の容量
 * @return 書いたバイト数（48）、または `TB_ERR_ARG`
 *
 * @details 画面側の `TermPalette`（`presentation/frame/game_frame.h:81`）の材料。
 * **既定でも変愚と違う**——幻想蛮怒の `TERM_BLUE` は `00,00,FF`、変愚は `00,80,FF`。
 * さらに遊ぶ側が `&`（カラーの設定）と pref の `V:` 行で変えられるので、
 * **毎フレーム読んで送る**。48 バイトしかなく、既定と同じ間は codec が丸ごと省く
 * （`game_frame.h:75-79`）ので、色をいじらない限り 1 バイトも増えない。
 */
int tb_read_palette(unsigned char *rgb, int cap);

/*!
 * @brief コアのカーソル選択（`use_menu`）を立てる。
 *        変愚の `apply_cursor_mode()`（`presentation_bridge.cpp:2649`）と同じ形。
 * @param enable 画面側の `ui_state.cursor_mode`。**偽なら何もしない**（コアに任せる）
 *
 * @details `use_menu` が立つと、魔法・持ち物・店の選択がコアの `》` カーソルで動く
 * （幻想蛮怒の `src/*.c` に 143 か所）。印は `util.c:4587` の `》`（CP932 `81 74`）で
 * **変愚と同じ字**なので、画面側の拾い方もそのまま使える。
 *
 * **毎回呼ぶこと。** `request_command()` は入口で `use_menu = FALSE` に戻す
 * （`util.c:4727`）ので、1 回立てて済ませると次のコマンドで消える。変愚も同じ理由で
 * capture のたびに立て直している。**digest の門より手前で呼ぶ**——画が変わらず
 * capture を省いたフレームでも、コアはその間にコマンドを 1 つ受け取る。
 *
 * @note 誕生前（`character_generated` が偽）は触らない。あそこはコアの別メニューで、
 * `use_menu` を立てると誕生画面の操作が変わる。
 * @note `command_menu`（Enter でコマンドメニュー）も一緒に立てる。幻想蛮怒では
 * 既定が真（`tables.c:9253`）なので普通は何も変わらないが、遊ぶ側が切っていると
 * パッドの決定ボタンが行き場を失うため、カーソル操作の間は立て直す。
 */
void tb_apply_cursor_mode(int enable);

/* ================================================================ ホストのフック */

/*!
 * @brief null term（C 側）から C++ 側を呼び返す手段。**P2 の芯**。
 *
 * @details `platform/windows/core_main.cpp` の `UiSeam` に当たるもの。
 * 変愚では `presentation/term/sdl_null_term.cpp` が同じ形を持っている
 * （`on_fresh` / `on_event` / `on_flush` / `on_delay`）。名前も役割も揃えてある。
 *
 * すべて**ゲームスレッドから**呼ばれる。コアの状態を触ってよいのはそこだけ
 * （`p_ptr` も `cave` も `Term` も素の大域変数で、鍵が無い）。
 */
typedef struct tb_host_hooks {
    /*! 画面が更新された（`TERM_XTRA_FRESH` / 待ちの毎周回）。frame を組んで送る。 */
    void (*present)(void);
    /*!
     * 次のキーを 1 個取る。**待たない**。
     * @return 1〜255 のキー、または 0（いま無い）
     * @note 返すのは**コア内部コード（SJIS）のバイト**である。UTF-8 → CP932 の
     *   変換は C++ 側（`tb_text`）で済ませてから積むこと（設計 §3.1・V5）。
     */
    int (*next_key)(void);
    /*! 溜まっているキーを捨てる（`TERM_XTRA_FLUSH`）。 */
    void (*drop_keys)(void);
    /*! ミリ秒待つ（`TERM_XTRA_DELAY` と、キー待ちの空回り）。 */
    void (*sleep_ms)(int ms);
    /*! 終了要求（`quit_request` / stdin EOF）が出ているか。0 か非 0。 */
    int (*shutdown_requested)(void);
} tb_host_hooks;

/*!
 * @brief フックを差す。NULL を渡すと外れる（P1 と同じ「ESC を積むだけ」に戻る）。
 * @details 差してよいのは `tb_term_install()` の後・`tb_bootstrap()` の前。
 */
void tb_set_host_hooks(const tb_host_hooks *hooks);

/* ==================================================================== 起動と進行 */

/*!
 * @brief 起動列。`init_file_paths(lib_dir)` → `init_angband()` まで。
 * @param lib_dir 幻想蛮怒の lib ディレクトリ（`<exe_dir>/toband/lib`）。
 *                末尾の区切りは有っても無くてもよい
 * @return TB_OK / TB_ERR_*
 *
 * **`play_game()` は呼ばない。** ゲームを回すのは `tb_run_game()`。
 * 失敗したときの説明は `tb_last_error()` に入る。
 */
int tb_bootstrap(const char *lib_dir);

/*!
 * @brief `play_game()` を回す。**戻ってくるのは終わったとき**。
 * @param new_game 真なら新規作成（`main-win.c` の [ファイル]→[新規] と同じ）
 * @return TB_OK（正常終了）/ TB_ERR_QUIT / TB_ERR_CORE
 *
 * @details `main-win.c:4374-4377`（`IDM_FILE_NEW`）の写し:
 * `game_in_progress = TRUE; Term_flush(); play_game(new_game); quit(NULL);`
 *
 * **`tb_bootstrap()` とは別の跳び先を使う**（設計 V4）。あちらは「起動に失敗した」
 * を意味する `quit()` を捕まえるためのもので、こちらの `quit()` は
 * 「遊び終えた」＝正常終了である。同じ跳び先を使い回すと、正常終了が
 * `TB_ERR_QUIT` に化けて `exit` メッセージの `code` が嘘になる。
 */
int tb_run_game(int new_game);

/*!
 * @brief 誕生の最初の画面（性別の選択）で取消されたとき、`play_game()` を抜けてタイトルへ戻る。
 * @return 戻らない（`tb_run_game()` が `TB_BIRTH_CANCEL` を返す）。跳び先が無いときだけ 0 で戻る（`--selftest` など）
 *
 * @details 原作の誕生の最初の画面（`birth.c` の性別）は ESC を捨てて待ち続け、戻る道が無い（`S`＝やり直し・`Q`＝終了だけ）。
 * 設計は「最初の画面で取消すとタイトルへ戻る」（決めたこと 2026-10-12・計画の判-2）なので、アダプタが
 * ESC を拾って進行の段を抜け、呼び出し側（`tb_main.cpp`）がタイトルとセーブ選択を出し直す。コアのプロセスは終わらない。
 * ゲームのルール・原作のコードは変えない。性別の画面かどうかは Term の行 7（`tb_null_term.c` の `tb_birth_first_screen`）で見分ける。
 */
int tb_cancel_birth_to_title(void);

/*!
 * @brief 直近の失敗の説明（コアの `quit()` / `core()` に渡された文字列）。
 * @return 静的な buffer。失敗していなければ空文字列
 *
 * 中身は**コア内部の文字コード（SJIS）のまま**である。stderr へそのまま流す。
 */
const char *tb_last_error(void);

/*!
 * @brief 終了要求に応じて**強制保存してから畳む**。**戻らない**。
 *
 * @details `main-win.c:5512`（`WM_QUERYENDSESSION`）の写し（設計 V4）。
 * ウィンドウを閉じたときの `WM_CLOSE`（:5476）は `Term_key_push(SPECIAL_KEY_QUIT)` を
 * 積むだけで、実際の保存は `close_game()` まで下りてから行われ、
 * **その途中で `inkey()` が待つ**（files.c:12042）。見る相手のいない場面で
 * 待たせるわけにいかないので、待たない側＝`WM_QUERYENDSESSION` の列を採る。
 *
 * 呼んでよいのは**ゲームスレッド**（＝`TERM_XTRA_EVENT` の中）だけ。
 * 別スレッドから呼ぶと `save_player()` が生きているセーブを掴んだまま
 * `p_ptr` / `cave` を読むことになる。
 */
void tb_shutdown_and_quit(void);

int tb_awaiting_command(void);
/*!
 * @brief 検査用：環境変数 `HD2D_TEST_MONSTER_ACT` の命令ファイルがあれば読んで、敵に魔法・直接攻撃をさせる。
 *        命令は複数行（上から順に全部実行。fail のあとの行は skip）。敵を指定のマスへ動かす `place`、
 *        敵の番を止める `hold`、敵の一覧を出す `list` もある。
 * @return 待っているコアを起こすキー（死亡・階の移動のとき ESC）。要らなければ 0
 * @details 命令待ちのときだけ働く。環境変数が無ければ何もしない。命令・条件は `tb_shim.c` の同名の節に書いた。
 */
int tb_test_monster_act_poll(void);


#ifdef __cplusplus
}
#endif

#endif /* TOBAND_ADAPTER_TB_SHIM_H */
