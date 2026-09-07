/*!
 * @file fc_shim.h
 * @brief FroxComposband コアと C++ アダプタの**境界**。純 C ABI・自己完結。
 *
 * 基準は （アダプタの構造 ── C の皮と C++ の本体）。
 *
 * ## この 1 枚が境界である理由
 * Frox のヘッダ（`angband.h` / `externs.h` ほか）は 1990 年代世代の C で、
 * C++ へ安全に include できる保証がない。そこで
 * **Frox のヘッダを見てよいのは `fc_*.c`（C の TU）だけ**と決め、
 * C++ 側（`fc_main.cpp` 以降）はこのヘッダしか見ない。
 *
 * したがってここに Frox の型・マクロを漏らしてはいけない。
 * `byte` も `cptr` も `term` も出さず、素の C の型だけで喋る。
 *
 * ## 文字コード
 * **M0 を出入りする文字列はすべて ASCII** である（`frox/src` も `frox/lib/edit` も
 * 非 ASCII バイト 0。`frox/UPSTREAM.md`）。M1 でコアの中が CP932 になるので、
 * そのときは `fc_text.h` を足して**境界の出口で変換する**（設計 §3.1）。
 * **この境界では一切変換しない**という約束は M1 でも変えない。
 *
 * ## P5 の時点で在るもの・無いもの
 * ここに在るのは Term の設営と読み口・起動列・ゲーム状態の読み口まで。
 * サブパネル（`fc_sub_terms.c`）と pad 表（`fc_pad_commands.cpp`）は **P6 で足す**。
 */

#ifndef FROX_ADAPTER_FC_SHIM_H
#define FROX_ADAPTER_FC_SHIM_H

#include <stddef.h> /* size_t（`fc_resume_take()`） */

#ifdef __cplusplus
extern "C" {
#endif

/* 返り値。0 が成功、負が失敗。 */
#define FC_OK 0
#define FC_ERR_ARG (-1) /* 引数が無い・長すぎる */
#define FC_ERR_LIB (-2) /* lib ディレクトリが見つからない */
#define FC_ERR_TERM (-3) /* Term が立てられない */
#define FC_ERR_QUIT (-4) /* コアが quit() を踏んだ */
#define FC_ERR_CORE (-5) /* コアが core() を踏んだ */
#define FC_ERR_STATE (-6) /* 手順違い（Term 無しで起動列を呼んだ等） */

/*!
 * @brief コアの名。プロトコル v1 の握手で使う `core_name`。
 * @return "frox"（静的な文字列。解放しない）
 */
const char *fc_core_name(void);

/*!
 * @brief コアの版。`frox/src/defines.h:19-22` の `VER_MAJOR` / `VER_MINOR` /
 *        `VER_PATCH` / `VER_EXTRA` から組む。
 * @return "7.3.pipari.2" のような文字列（静的な文字列。解放しない）
 *
 * @note **`VER_PATCH` は数ではなく文字列**である（`"pipari"`）。先方の
 *       `src/Makefile.src` の `VERSION` と同じ並びになる。
 *       先方の版が上がったら**ここは何もしなくていい**。写しを取り込み直せば追随する。
 */
const char *fc_core_version(void);

/* ================================================================== Term の設営 */

/*!
 * @brief ヘッドレスの Term を 1 枚立てて活性にする。
 * @param cols 桁（M0 は 80）
 * @param rows 行（**24 未満は不可**──note() が 23 行目に書く）
 * @return FC_OK / FC_ERR_ARG / FC_ERR_STATE / FC_ERR_TERM
 *
 * **`fc_bootstrap()` より先に呼ぶこと。** `init_angband()` は note() /
 * `Term_putstr()` で進捗を書くので、Term が 1 枚も無いと落ちる。
 */
int fc_term_install(int cols, int rows);

/*! @brief 立てた Term を畳む。 */
void fc_term_remove(void);

/*! @brief 立っている Term の大きさ。立っていなければ `FC_ERR_STATE`。 */
int fc_term_size(int *cols, int *rows);

/*!
 * @brief Term の 1 行を読む（画のミラー）。
 * @param y 行
 * @param text 文字の受け皿（NUL 終端で返す）
 * @param attr 色の受け皿（0..15。不要なら NULL）
 * @param cap `text` の容量
 * @return 書いた文字数、または負の失敗
 */
int fc_term_row(int y, char *text, unsigned char *attr, int cap);

/*! @brief カーソルの位置と可視。`visible` は「画面外でなく、かつ可視」のとき 1。 */
int fc_term_cursor(int *x, int *y, int *visible);

/*! @brief 画を消す（`Term_clear()`）。 */
void fc_term_clear(void);

/*! @brief 画に 1 行書く（`Term_putstr()`）。タイトル画面をこちらで描くのに使う（§3.3）。 */
void fc_term_putstr(int x, int y, int attr, const char *text);

/*! @brief 画を確定して 1 枚送る（`Term_fresh()` → `TERM_XTRA_FRESH` → `present`）。 */
void fc_term_present(void);

/*!
 * @brief 変化検出用の安い digest。
 *
 * @details 実装は `fc_null_term.c` に置いてある（Term の画がそこの静的変数だから）。
 * 混ぜるものは「画面に出るものが変われば必ず動く」材料に限る。
 *
 * @note **`status_col_side` は混ぜない**（設計 §4.3）。起動から終了まで動かない値で、
 * 混ぜても意味が無い。「画面が読む旗は digest に混ぜる」の罠には
 *       当たらない——あれは「立った瞬間の 1 枚」を要求する旗の話である。
 */
unsigned long long fc_change_digest(void);

/* ================================================================== HUD の読み口 */

/*!
 * @brief HUD の材料（設計 §4）。
 * @details 「無い」ときは 0 と空文字列。誕生画面のように `p_ptr` がまだ
 * 埋まっていない場面でも呼んでよい（`in_game` が 0 で返る）。
 */
typedef struct fc_hud_data {
    char name[64]; /*!< プレイヤ名（`player_name`） */
    char place[96]; /*!< 現在地名（町名 or ダンジョン名） */
    char depth_text[32]; /*!< "Surface" "L12" 等（`prt_depth` と同じ文言） */
    int hp;
    int hp_max;
    int sp;
    int sp_max;
    int level;
    int depth; /*!< `dun_level` */
    long gold; /*!< `p_ptr->au` */
    int in_game; /*!< `character_generated`。0 のあいだ上の数値は意味を持たない */
} fc_hud_data;

/*!
 * @brief HUD の材料を読む。
 * @return FC_OK（失敗しない。まだ何も無ければ 0 詰めで返る）
 */
int fc_read_hud(fc_hud_data *out);

/*! @brief 溜まっているメッセージの本数（`msg_count()`。`frox/src/message.c:93`）。 */
int fc_message_count(void);

/*!
 * @brief 古さ `age` のメッセージ（0 が最新）。
 * @return 書いた文字数（無ければ 0）
 * @note 中身は `msg_get_plain_text()` で取る——Frox のメッセージは色つきの
 *       ドキュメント片なので、**装飾を落とした素の字**を貰う口があちらに在る。
 */
int fc_message_text(int age, char *out, int cap);

/*! @brief `character_generated`。 */
int fc_character_generated(void);

/*! @brief キー配列が rogue 式か（`rogue_like_commands`）。pad 表が見る。 */
int fc_rogue_like_commands(void);

/* -------------------------------------------------------------- 画面の状態の旗 */

#define FC_SCREEN_GENERATED 0x0001 /*!< `character_generated`。@ が出来ている */
#define FC_SCREEN_ICKY 0x0002 /*!< `character_icky != 0`。全画面の別画面（店・一覧） */
#define FC_SCREEN_XTRA 0x0004 /*!< `character_xtra != 0`。再計算中で画面を触らない間 */
#define FC_SCREEN_DEAD 0x0008 /*!< `p_ptr->is_dead` */
#define FC_SCREEN_DUNGEON 0x0010 /*!< `character_dungeon`。`cave[][]` に階が載っている */
#define FC_SCREEN_PLAYING 0x0020 /*!< `p_ptr->playing` */

/*! @brief 上の旗の束。 */
int fc_screen_flags(void);

/* ------------------------------------------ メッセージ行の置き場（FH-13） */

/*!
 * @brief **いまの**メッセージ行の矩形（`msg_line_rect()`）。
 *
 * @details **場所は固定ではない。** 通常は `ui_msg_rect()` ＝
 * `rect(0, 0, MIN(72, wid-13), 10)` だが、**店・我が家・建物・クエストの UI は
 * `msg_line_init(ui_shop_msg_rect())` で `rect(0, 0, 80, 3)` へ移し替える**
 * （`shop.c:1524` ほか 4 か所。出る時に `ui_msg_rect()` へ戻す）。
 *
 * `[y/n]` の読み取りはこの矩形で切ってから末尾一致を取る（`fc_menu.cpp`）。
 * 決め打ちの 67 桁で切っていたため、**店の 68 桁目以降に出た `[y/n]` を
 * 取りこぼしていた**（FH-13。実機で見つけた（2026-08-24）「y/n の選択が
 * カーソルでできない」）。
 *
 * @param[out] x,y 左上（桁・行）。@param[out] w 桁数。
 * @param[out] h **いま使っている行数**（`doc_line_count`。0 もありうる）
 * @return `FC_OK`、または `init_angband()` 前なら `FC_ERR_STATE`
 * @note `init_angband()` の途中でも `Term_fresh()` は起きる（進捗の note）ので、
 *       **`msg_on_startup()`（`init2.c:1960`）より前に呼ぶと落ちる**。
 *       `fc_mark_msg_ready()` を通った後だけ真を返す。
 */
int fc_msg_rect(int *x, int *y, int *w, int *h);

/*! @brief `init_angband()` が済んだ合図（`fc_bootstrap.c` が 1 度だけ呼ぶ）。 */
void fc_mark_msg_ready(void);

/*!
 * @brief **検査専用**: メッセージ行を店の矩形（`rect(0,0,80,3)`）へ移す／戻す。
 * @param on 非 0 で店の矩形、0 で通常（`ui_msg_rect()`）
 *
 * @details 店・我が家・建物・クエストは `msg_line_init()` で箱を移す。
 * **そこへはボットが届かない**（店は町の区画をまたげない）ので、
 * `--selftest` が同じ呼び出しで矩形だけ再現し、`[y/n]` と個数の読み取りが
 * **68 桁目以降でも当たること**を機械で押さえる（FH-13 / FH-15）。
 * @note 遊んでいる最中には呼ばない（`fc_main.cpp` の `run_selftest` だけ）。
 */
void fc_msg_rect_use_shop_for_test(int on);

/* ------------------------------------------------ doc UI の旗（M0.5。設計 §14.2(a)） */

/*!
 * @brief `Term_save()` を直に呼ぶ全画面 UI（doc UI）が開いているか。
 *
 * @details Frox は icky を立てない全画面 UI を 6 つ持つ（品選び・呪文選び・
 * skillmaster・鍛冶・世界地図の表示・wiz_obj。設計 §14.1 の事実 1）。
 * `character_icky` に映らないので、この旗が無いと一覧が画面へ 1 行も出ない
 * （FH-02 の正体）。
 *
 * 立てるのは `fc_ui_term_save()`。**降ろすのは `Term_load()` ではなく
 * 「`inkey_flag` が TRUE になった」を見たとき**——鍛冶は小画面から戻るたび
 * load する（save 1 / load 10）ので、load で降ろすと開いている最中に閉じる。
 * `inkey_flag` はコマンド待ちでだけ TRUE（`util.c:3731`）なので、そこまで
 * 戻った＝ doc UI はもう畳まれている。
 *
 * @note 降ろす判定はこの関数の**中**にある（呼ぶだけで降りる）。呼び手は
 * `fc_change_digest()`（毎周回）と `fc_frame.cpp` の `menu_open`。
 * **digest に混ぜてある**——降りる瞬間は Term が 1 セルも変わらないので、
 * 混ぜないとミラーが開いたまま残る。
 */
int fc_doc_ui_active(void);

/*!
 * @brief **品選びの窓（`obj_prompt`）が開いているか**（フック #37。2026-08-28）。
 *
 * @details `fc_doc_ui_active()` とは別物である。あちらは「全画面 UI がどれか開いた」の
 * 掛け金で、**店の中では店を出るまで降りない**（`inkey_flag` を見て降ろすため）。
 * こちらは `obj_prompt()` の入りと片づけで上げ下げするので、
 * **いま品を訊かれているか**を 1 ビットで答えられる。
 *
 * 読むのは `fc_menu.cpp` の `fill_menu_choices()`。開いている間は
 * **店の命令列を札にせず、窓の中の品だけを札にする**（FH-14 の裏返し）。
 */
int fc_obj_prompt_active(void);

/*!
 * @name Term_save / Term_load の横取り
 * @details vcxproj の**ファイル単位の define**（`z-doc.c` の `/FI` と同じ流儀）で、
 * 上の 6 TU にだけ `Term_save=fc_ui_term_save;Term_load=fc_ui_term_load` を差す。
 * z-term.h の宣言ごと改名されるので原型も自動で合う。`frox/src` には触らない
 * （設計 §1 制約 1）。`py_birth.c` は**入れない**——誕生は `!generated` で
 * 既にミラーが開く。実体は `fc_shim.c`（本物の Term_save / Term_load へ回す）。
 * @{
 */
int fc_ui_term_save(void);
int fc_ui_term_load(void);
/*! @} */

/* ==================================================================== 地図の読み口 */

/*
 * 地形の意味の旗。**Frox は変愚の直系**なので `FF_*` がそのまま同じ番号で在り、
 * 幻想蛮怒の `gb_feature_bits()` と 1 行も違わない読み方ができる（検討 §2）。
 */
#define FC_FEAT_WALL 0x0001u /*!< `FF_WALL`（26） */
#define FC_FEAT_DOOR 0x0002u /*!< `FF_DOOR`（18） */
#define FC_FEAT_DOOR_OPEN 0x0004u /*!< `FF_DOOR` かつ `FF_CLOSE`（9）＝「閉じられる」＝いま開いている */
#define FC_FEAT_STAIRS 0x0008u /*!< `FF_STAIRS`（20）/ `FF_ENTRANCE`（107）/ `FF_QUEST_ENTER`（98） */
#define FC_FEAT_PERMANENT 0x0010u /*!< `FF_PERMANENT`（27） */
#define FC_FEAT_TREE 0x0020u /*!< `FF_TREE`（83） */
#define FC_FEAT_WATER 0x0040u /*!< `FF_WATER`（39） */
#define FC_FEAT_LAVA 0x0080u /*!< `FF_LAVA`（40） */
#define FC_FEAT_GLOW 0x0100u /*!< `FF_GLOW`（37） */
#define FC_FEAT_PLAYER 0x0200u /*!< @ の居るマス */
#define FC_FEAT_KNOWN 0x0400u /*!< 地形が見えている（`map_info` と同じ判定） */
#define FC_FEAT_PASSABLE 0x0800u /*!< `FF_MOVE`（2）かつ壁・扉・木・岩でない */
#define FC_FEAT_RUBBLE 0x1000u /*!< `FF_HURT_ROCK`（44）かつ壁でも扉でもない（＝瓦礫） */
#define FC_FEAT_ROOM 0x2000u /*!< `CAVE_ROOM` */
#define FC_FEAT_GLOWING 0x4000u /*!< `CAVE_GLOW` かつ `CAVE_MNDK` でない */

/*!
 * @brief 1 マスぶんの材料。
 * @note `terrain_id` は**先方の feat 番号のまま**である。変愚の番号への名寄せは
 *       `fc_frame.cpp` が `frox/tilework/terrain_map.csv` を引いて行う
 */
typedef struct fc_map_cell {
    short gx;
    short gy;
    unsigned short terrain_id; /*!< 先方の feat（未知のマスは 0） */
    unsigned short monster_id; /*!< 見えている敵の `ap_r_idx`（居なければ 0） */
    unsigned short monster_slot; /*!< `cave[][].m_idx`（居なければ 0） */
    unsigned short object_id; /*!< 照らされた床の物の `k_idx`（無ければ 0） */
    unsigned short feature_flags; /*!< 上の `FC_FEAT_*` */
    unsigned char fg_color; /*!< `map_info()` の `a & 0x0F` */
    unsigned char bg_color; /*!< `map_info()` の `ta & 0x0F` */
    char ascii; /*!< `map_info()` の `c`。**字の板の出どころ**（設計 §6.2） */
    unsigned char light_level; /*!< 0 = 闇 / 1 = 視界内 / 2 = 照らされている */
} fc_map_cell;

/*! @brief いまの階の大きさ（`cur_wid` / `cur_hgt`）。 */
int fc_floor_size(int *width, int *height);

/*! @brief @ の位置（`px` / `py`）。 */
int fc_player_pos(int *x, int *y);

/*! @brief @ の職と種族（`p_ptr->pclass` / `p_ptr->prace`）。 */
int fc_player_kind(int *pclass, int *prace);

/*!
 * @brief 矩形ぶんのマスを読む。
 * @param cap `out` の要素数（**`w * h` 以上が要る**）
 * @return 書いた要素数、または負の失敗
 */
int fc_read_map(int x0, int y0, int w, int h, fc_map_cell *out, int cap);

/* ---------------------------------------------------------------- ミニマップ */

#define FC_MM_UNKNOWN 0
#define FC_MM_FLOOR 1
#define FC_MM_WALL 2
#define FC_MM_DOOR 3
#define FC_MM_STAIRS 4
#define FC_MM_ITEM 5
#define FC_MM_MONSTER 6
#define FC_MM_PLAYER 7
#define FC_MM_MOUNTAIN 8 /*!< `FF_MOUNTAIN`（102）。壁だが山 */
/*
 * 9〜13 は歩ける地形の細別（FH-09。`MinimapKind` の 9〜13 と同じ番号）。
 * Frox の町と荒野は草・木・水が大半で、全部 `FLOOR` に畳むと一色塗りになる。
 * **歩けるかどうかの意味は `FLOOR` と同じ**（画面側の経路判定もそう扱う）。
 */
#define FC_MM_WATER 9 /*!< `FF_WATER`（39）。深浅は分けない */
#define FC_MM_TREE 10 /*!< `FF_TREE`（83） */
#define FC_MM_GRASS 11 /*!< `feat_grass` / `feat_flower`（旗が無いので番号で見る） */
#define FC_MM_LAVA 12 /*!< `FF_LAVA`（40） */
#define FC_MM_SNOW 13 /*!< `FF_SNOW`（50）。雪原と Snow castle の床 */

/*!
 * @brief 階全体を 1 バイト 1 マスで読む。
 * @param cap `cur_wid * cur_hgt` 以上
 * @return 書いたバイト数、または負の失敗
 */
int fc_read_minimap(unsigned char *out, int cap);

/* ------------------------------------------------------------------ 階の素性 */

typedef struct fc_floor_info {
    int kind; /*!< 0 = 不明 / 1 = 地上 / 2 = ダンジョン / 3 = クエスト階 / 4 = 闘技場 */
    int dungeon_id; /*!< `dungeon_type` */
    int dun_level;
    int town_id; /*!< `p_ptr->town_num` */
    int wild_mode; /*!< 広域マップを見ている */
    int day_minute; /*!< 0..1439 */
    int daytime; /*!< `is_daytime()` */
    int light_radius; /*!< `p_ptr->cur_lite` */
    char place_name[96];
} fc_floor_info;

/*! @brief 階の素性を読む。 */
int fc_read_floor_info(fc_floor_info *out);

/* ============================================ 周囲の地形の内訳（環境音の層） */

/*!
 * @brief 周囲の地形の内訳（`presentation/frame/game_frame.h` の `SurroundingsView`）。
 * @details 環境音は「いま周りに何が在るか」で層を重ねる
 * **数えるのはコア側の仕事**である——
 * 地形の意味（草か土か沼か）を知っているのは `f_info` を読める側だけで、
 * 画面側は地形テーブルを持たない。
 *
 * 割合は 0..255 が 0..100%。**合計は 100% を超えうる**——壁とガラスは材料とは
 * 別勘定で、ガラスの壁は壁でもありガラスでもあるからである。
 */
typedef struct fc_surroundings {
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
} fc_surroundings;

/*!
 * @brief 周囲を数える。@return FC_OK（失敗しない。数えられなければ 0 詰め）
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
int fc_read_surroundings(fc_surroundings *out);

/* ============================================================ 戦闘の見せ場（FH-05） */

/*
 * `frox/src` には 1 バイトも触らない（設計 §1 制約 1）ので、幻想蛮怒の
 * `gb_fx_*`（コアへ 3 か所差した）と同じ道は取れない。代わりに
 * **コアが自分で出した事実だけ**を 3 つの口から集める:
 *
 *   1. **弾道と爆発** … コアは弾道を「1 セル描く → cursor 合わせ → fresh →
 *      `TERM_XTRA_DELAY`」の署名で Term に描く（`spells1.c:2903-2906`。爆発は
 *      環ごとに複数セル → fresh → DELAY）。null term がこの署名を観測し、
 *      描かれたセル（記号 `*|/-\` だけ）を拾う。**コアが描いた絵そのもの**である。
 *   2. **被弾** … `p_ptr->chp` の減りをフレーム間で見る（`fc_fx_scan_deltas`）。
 *   3. **命中ととどめ** … `m_list[]` の HP の減りと、`sound(SOUND_KILL)` ＋
 *      スロット消滅の突き合わせ。SOUND は `use_sound` を立てて `Term_xtra` で
 * 受けるだけで、**音は今までどおり 1 つも鳴らさない**（
 *      「音の設計は保留」はそのまま）。
 *
 * kind / element の番号は `presentation/frame/combat_fx.h` の
 * `CombatFxKind` / `CombatFxElement` と同じ。束ね方（描画色 → element）は
 * **色が往復でほぼ保たれる**ように選ぶ（element は画面側で色にしか使われない）。
 */

#define FC_FX_KIND_HIT_PLAYER 0
#define FC_FX_KIND_HIT_MONSTER 1
#define FC_FX_KIND_BOLT 2

typedef struct fc_fx_event {
    unsigned char kind; /*!< FC_FX_KIND_* */
    unsigned char element; /*!< `CombatFxElement` と同じ番号 */
    short y; /*!< 起きたマス（Bolt は着弾側） */
    short x;
    short src_y; /*!< Bolt の出所。ほかは y/x と同じ */
    short src_x;
    short num; /*!< 強さの分子（受けたダメージなど） */
    short den; /*!< 強さの分母（最大 HP など。0 は 1 と読む） */
} fc_fx_event;

/*! @brief 溜まっている見せ場を汲んで消す。@return 書いた個数 */
int fc_fx_take(fc_fx_event *out, int cap);

/*! @brief これまでに積んだ**累計**。digest に混ぜる（積んだ瞬間のフレームを強制する）。 */
unsigned long fc_fx_total(void);

/*! @brief HP の減り（被弾・命中・とどめ）を 1 回ぶん走査して積む。capture ごとに呼ぶ。 */
void fc_fx_scan_deltas(void);

/*!
 * @name null term から差す口（実装は `fc_shim.c`）
 * @details 座標は**世界のマス**（呼び手が `ui_pt_to_cave_pt()` で直してから渡す）。
 * @{
 */
/*! @brief 弾道・爆発のセル 1 つ。`ch` は描かれた記号、`attr` は描かれた色。 */
void fc_fx_push_spark(int gy, int gx, int ch, int attr);
/*! @brief `sound(SOUND_KILL)` を 1 回聞いた（とどめの突き合わせに使う）。 */
void fc_fx_note_kill_sound(void);
/*! @} */

/* ==================================================================== 効果音 */

/*
 * **コアは鳴らさない。**「どのマスで何の音が要る」という出来事だけをフレームへ載せ、
 * 鳴らすのは画面側（`hd2d/audio/`）である。理由は
 * `presentation/frame/sound_event.h`・——位置で聞き分けるには
 * 聞き手の位置と向き、および HRTF の効くミキサが要り、コアはどちらも持たないため。
 *
 * 建付けは上の見せ場（`fc_fx_*`）と同じ環である。積むのは `fc_null_term.c` の
 * `TERM_XTRA_SOUND`、汲むのは `fc_frame.cpp`。
 *
 * **音は名前で運ぶ**（番号ではない）。番号は変種ごとに別の音を指すので、番号で運ぶと
 * 画面側に変種ごとの対応表が要る。名前なら表は 1 つで済む。綴りは Frox の
 * `angband_sound_name[]`（`variable.c`）そのままで、変愚の目録
 * （`assets/audio/sfx.jsonc`）に 45 名がそのまま当たる。
 */

/*! @brief 音 1 つ。`name` は `angband_sound_name[]` の綴り（NUL 終端）。 */
typedef struct fc_sound_event {
    char name[16]; /*!< 音の名前。空なら捨ててよい */
    short y; /*!< 鳴ったマス（いまは常に @ のマス＝頭で鳴る） */
    short x;
} fc_sound_event;

/*!
 * @brief 画面側が音の出来事を欲しがっているか（`ui_state.sound_events`）を覚える。
 * @details 偽の間は 1 つも積まない。**`use_sound` は落とさないこと**——コアの
 * `sound()` はあれが偽だと `Term_xtra` すら呼ばず、書き留める機会ごと消える。
 */
void fc_sound_set_wanted(int wanted);

/*! @brief `TERM_XTRA_SOUND` を 1 回聞いた。`v` は `SOUND_*` の番号。 */
void fc_sound_push(int v);

/*! @brief 溜まっている音を汲んで消す。@return 書いた個数 */
int fc_sound_take(fc_sound_event *out, int cap);

/*!
 * @brief これまでに積んだ**累計**。digest に混ぜる。
 * @details 音は一度きりの出来事で、鳴った瞬間の Term は 1 セルも動かないことがある
 * （`sound()` の後に画が変わらない場面）。混ぜないと**その 1 枚が線に乗らず**、
 * 音が次の別の変化まで遅れる。
 */
unsigned long fc_sound_total(void);

/* ================================================================ セーブの口 */

/*! @brief セーブの置き場（`ANGBAND_DIR_SAVE`）。 */
const char *fc_save_dir(void);

/*! @brief 遊ぶ枠を決める（`savefile` / `savefile_base` を直に埋める。設計 §3.3）。 */
int fc_set_savefile(const char *slot);

/*! @brief 枠の指定を消す。 */
void fc_clear_savefile(void);

/*! @brief いまの枠の名（`savefile_base`）。 */
const char *fc_savefile_base(void);

/*! @brief いまの枠の道（`savefile`）。 */
const char *fc_savefile_path(void);

/*!
 * @name 言語切り替えの立て直し（追補 A2。`--resume`。実装は `fc_bootstrap.c`）
 *
 * @details 印は `<save>/.resume` の 1 行（セーブ枠の綴り）。書くのは
 * `fc_shutdown_and_quit()` が**強制保存に成功したとき**だけで、読むのは
 * `--resume` を渡された起動列（`fc_main.cpp`）である。
 * 詳しい理由は `fc_bootstrap.c` の同名の節。
 * @{
 */

/*!
 * @brief 印を読んで**消す**（一度きり）。
 * @param name 綴りの受け皿
 * @param max 受け皿の大きさ
 * @return 1 = 読めて、その綴りのセーブが実在する／0 = 印が無い・壊れている・セーブが無い
 */
int fc_resume_take(char *name, size_t max);

/*!
 * @brief 印を消す。
 * @details `--resume` **無し**で起きたときに呼ぶ——持ち越すと、次の立て直しで
 * 意図しない人物が開く。
 */
void fc_resume_clear(void);
/*! @} */

/* ============================================================ 持ち物・装備・矢筒 */

/*
 * **ここが Frox で唯一の書き直しである**（設計 §3.4。決めたこと F3）。
 * Frox は変愚の `inventory[INVEN_RARM]` 方式を捨てて `pack.c` / `equip.c` /
 * `quiver.c` の 3 つの器に分けた。**矢筒は幻想蛮怒に無い概念**なので、
 * 落とさないように種を 1 つ足してある（設計 §1 制約 7）。
 */
#define FC_ITEMS_INVENTORY 0 /*!< 持ち物（`pack_obj(1 .. pack_max())`） */
#define FC_ITEMS_EQUIPMENT 1 /*!< 装備（`equip_obj(1 .. equip_max())`） */
#define FC_ITEMS_QUIVER 2 /*!< 矢筒（`quiver_obj(1 .. quiver_max())`）。**Frox 固有** */

/*!
 * @brief 持ち物・装備・矢筒の `index` 番目（0 起点・埋まっている枠だけ数える）。
 * @return 書いた文字数（そこまで無ければ 0）、または負の失敗
 */
int fc_read_item_line(int what, int index, char *out, int cap);

/*! @brief 視認している敵の `index` 番目の名前。見えていない敵は数えない。 */
int fc_read_visible_monster(int index, char *out, int cap);

/* ================================================================== 色の申告 */

/*!
 * @brief コアの 16 色を RGB で読む（`angband_color_table`）。
 * @param cap `rgb` のバイト数（**16 * 3 以上**）
 * @return 書いたバイト数、または負の失敗
 *
 * @note Frox の色は `TERM_DARK`〜`TERM_L_UMBER` の **16 色**で、当方の
 *       `TermPalette` と同じ数である（設計 §4.1）。畳む作業は無い。
 */
int fc_read_palette(unsigned char *rgb, int cap);

/*!
 * @brief カーソル選択（`ui_state.cursor_mode`）をコアへ当てる。
 * @details 真なら `command_menu` / `use_menu` を立てる（Enter でコマンドメニュー・
 * 以降の選択はコアのカーソルで）。偽なら触らない——従来どおり文字キー主体。
 * 幻想蛮怒の `gb_apply_cursor_mode()` と同じ。
 */
void fc_apply_cursor_mode(int enable);

/* ============================================================ ホストのフック */

/*!
 * @brief null term から C++ 側を呼び返す唯一の道。
 * @details 差さっていない間（`--selftest`）は逃げ道に落ちる（`fc_null_term.c`）。
 */
typedef struct fc_host_hooks {
    /*! 画を 1 枚送る。`TERM_XTRA_FRESH` と待ちの空回りから呼ばれる。 */
    void (*present)(void);
    /*! 次のキー（無ければ 0 以下）。**末尾へ積む**ので 1 回に何個返してもよい。 */
    int (*next_key)(void);
    /*! 溜まっている入力を捨てる（`TERM_XTRA_FLUSH`）。 */
    void (*drop_keys)(void);
    /*! 少し寝る。 */
    void (*sleep_ms)(int ms);
    /*! 畳めと言われているか。真なら `fc_shutdown_and_quit()` へ落ちる。 */
    int (*shutdown_requested)(void);
} fc_host_hooks;

/*! @brief フックを差す（NULL で外す）。 */
void fc_set_host_hooks(const fc_host_hooks *hooks);

/* ================================================================== 起動と進行 */

/*!
 * @brief `init_file_paths()` → `init_angband()` まで。
 * @param lib_dir `frox/lib` の道
 * @param lang M0 は常に英語。**引数は受けるが中身は M1 で入る**（設計 §3.6・§12 の 3）
 * @return FC_OK / FC_ERR_*
 *
 * **`fc_term_install()` より後に呼ぶこと。**
 */
int fc_bootstrap(const char *lib_dir, const char *lang);

/*!
 * @brief `play_game()` を回す。**戻ってくるのは遊び終えたとき**（設計 §3.2）。
 * @param new_game 真なら新規、偽なら再開
 * @return FC_OK（正常終了）/ FC_ERR_CORE
 */
int fc_run_game(int new_game);

/*! @brief 直近の失敗の説明（静的な文字列）。 */
const char *fc_last_error(void);

/*!
 * @brief 強制保存して畳む。**戻らない**。
 * @details `main-win.c:3816`（`WM_QUERYENDSESSION`）の写し。閉じる合図
 * （`WM_CLOSE`）のほうを採らないのは、あちらが `inkey()` の待ちを踏むからである。
 */
void fc_shutdown_and_quit(void);

/*!
 * @brief 実体の数を読む（`--selftest` の材料）。
 * @details `max_r_idx` / `max_k_idx` / `max_f_idx` / `max_a_idx` / `max_e_idx` /
 *          `max_d_idx` の順で 6 つ書く。**地形の名寄せ表の全数照合は P7 で足す**
 *          （設計 §4.2・§9 の 4。表そのものがまだ無い）。
 * @param out 6 要素以上の受け皿
 * @param cap `out` の要素数
 * @return 書いた要素数、または負の失敗
 */
int fc_read_limits(int *out, int cap);

/* ================================================================== サブパネル */

/*
 * 画面のサブパネルは 7 枚（`presentation` の `kSubPanelCount`）。コアの
 * サブウィンドウ（`window_flag[1..7]` × `window_stuff()`）をそこへ写す。
 * Term の世話は `fc_sub_terms.c`、種類の申告と文字の積みは `fc_sub_panels.cpp`。
 *
 * **`frox/src` は 1 バイトも触れない**（設計 §1 制約 1）ので、幻想蛮怒が
 * `cmd4.c` に 1 行入れて作った「世代計数器」は使えない。Sil-Q と同じく
 * **画面の cfg を持ち主と決めて希望を押し続ける**
 */
#define FC_SUB_PANELS 7

/*!
 * @brief 矢筒の種（**Frox 固有**。設計 §3.4 / §4.4）。
 * @details `window_flag` のビットではない——Frox のサブウィンドウに矢筒は無い。
 * だがコアには `quiver.c` という**幻想蛮怒に無い器**が在り、落とすと制約 7 に触る。
 * そこで**アダプタが自分で描く種**として 1 つ足す。番号は `window_flag` の 32 ビットと
 * 当たらない 1000 番台にしてある。
 */
#define FC_SUB_KIND_QUIVER 1000

/*! @brief サブパネル用の Term を 7 枚立てる（`fc_bootstrap()` の**後**）。 */
int fc_sub_terms_install(void);

/*!
 * @brief 種類の名前（`window_flag_desc[flag]`）。描画関数の無い種類は 0 を返す。
 * @return 書いた文字数（無ければ 0）
 */
int fc_window_flag_name(int flag, char *out, int cap);

/*! @brief 画面が望む桁・行を覚える（0 は「申告なし」）。 */
void fc_sub_term_set_cells(int panel, int cols, int rows);

/*! @brief 覚えた大きさを Term へ当てる。変えたら 1。 */
int fc_sub_term_apply_cells(int panel);

/*! @brief その枚の Term の大きさ。 */
int fc_sub_term_size(int panel, int *cols, int *rows);

/*! @brief その枚の Term の 1 行（`fc_term_row` と同じ流儀）。 */
int fc_sub_term_row(int panel, int y, char *text, unsigned char *attr, int cap);

/*! @brief いまその枚に立っている種類（-1 = 何も立っていない）。 */
int fc_sub_current_kind(int panel);

/*! @brief 描画関数のある種類だけを通す濾し器（無ければ -1）。 */
int fc_sub_sanitize_kind(int flag);

/*!
 * @brief 本線 Term（`window_flag[0]`）の種類を落とす。落としたら 1。
 * @details 0 番は地図そのもの。ここに印が立っていると `fix_inven()` ほかが
 * **持ち物一覧を地図の上に描く**。セーブに入っていた値で立ちうる。
 */
int fc_sub_clear_main_flags(void);

/*! @brief その枚の種類を差し替える。変えたら 1。 */
int fc_sub_set_kind(int panel, int flag);

/*! @brief 「描き直せ」の印を立てる（`window_stuff()` は呼ばない）。 */
void fc_sub_request_redraw(void);

/* ================================================================ pad の表（v1 §8） */

/*!
 * @brief 画面のパッドへ載せるコマンド 1 つぶん。
 * @details 名札はコアの語（M0 は英語）。**画面側に表を持たせない**（設計 §1 制約 3）。
 */
typedef struct fc_pad_command {
    int command; /*!< Angband のコマンド文字。**永続化の鍵** */
    unsigned char key_normal; /*!< 元の（Angband 式）配列で押すキー。0 = 出せない */
    unsigned char key_rogue; /*!< rogue 式配列で押すキー。0 = 出せない */
    char group[32]; /*!< 分類の名札（`menu_info[0]` の見出し） */
    char label[64]; /*!< 画面に出す名札（`menu_info` の項目そのまま） */
} fc_pad_command;

/*!
 * @brief pad の表を読む。
 * @return 書いた件数、または負の失敗
 */
int fc_read_pad_commands(fc_pad_command *out, int cap);

/*!
 * @brief コマンド → その配列で押すべきキー（`keymap_act` の逆引き）。
 * @param rogue 真なら rogue 式配列
 * @return 押すキー。**0 は「そのコマンドは出せない」**
 */
unsigned char fc_pad_key_for_command(int rogue, int command);

/*! @brief 表の版。名札が変わる（職・広域マップ）と動く。切替の見張りに使う。 */
int fc_pad_table_stamp(void);

/* ============================================================ 地形の全数照合（P7） */

/*!
 * @brief その feat が `f_info` に定義されているか（`--selftest` の全数照合）。
 * @return 1 = 定義あり / 0 = 空き枠 / 負 = 範囲外
 * @details `max_f_idx`（255）は器の大きさで、実際に定義されているのは 188 種である。
 *          空き枠に名前は入っていないので、そこで見分ける。
 */
int fc_terrain_defined(int feat);

/*!
 * @brief その `r_idx` が `r_info` に定義されているか（M2 の目録の全数照合）。
 * @return 1 = 定義あり / 0 = 空き枠 / 負 = 範囲外
 * @details 実体の id は**疎**である（`max_r_idx` は器の大きさで、定義は 1,394 種）。
 *          空き枠に名前は入っていないので、地形と同じやり方で見分ける。
 */
int fc_monster_defined(int r_idx);

/*!
 * @brief その `k_idx` が `k_info` に定義されているか（同上）。
 * @return 1 = 定義あり / 0 = 空き枠 / 負 = 範囲外
 */
int fc_object_defined(int k_idx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FROX_ADAPTER_FC_SHIM_H */
