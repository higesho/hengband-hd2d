/*!
 * @file an_map_shim.h
 * @brief マップ・HUD・メッセージを読む純 C ABI。`an_frame.cpp`（C++）はこれだけを見る。
 *
 * `an_shim.h` と同じ理由で分ける
 * ——`angband.h` を C++ 側へ漏らさない（設計 §3 の表）。
 */
#pragma once

#include "bridge/native_store_data.h" /* AH-04: native_store_item（持ち物一覧の 1 件） */

#ifdef __cplusplus
extern "C" {
#endif

/*! @name TF_* のうち画面へ渡す分だけを立てたビット（`an_map_shim.c` が変換する）。 */
/*! @{ */
#define AN_TF_WALL 0x0001
#define AN_TF_PERMANENT 0x0002
#define AN_TF_DOOR_ANY 0x0004
#define AN_TF_DOOR_CLOSED 0x0008
#define AN_TF_STAIR 0x0010
#define AN_TF_UPSTAIR 0x0020
#define AN_TF_DOWNSTAIR 0x0040
#define AN_TF_PASSABLE 0x0080
#define AN_TF_FIERY 0x0100
#define AN_TF_SHOP 0x0200
/*! 岩石（TF_ROCK）。壁でも扉でもない岩石は瓦礫（見直しの指摘 9）。 */
#define AN_TF_ROCK 0x0400
/*! 内部発光地形（TF_BRIGHT）。CELL_FEAT_GLOW の材料（見直しの指摘 9）。 */
#define AN_TF_BRIGHT 0x0800
/*! 地中に金を含む（TF_GOLD）。ミニマップの宝石印の材料。 */
#define AN_TF_GOLD 0x1000
/*! @} */

/*!
 * @brief ミニマップの上に重ねて出す印（`MinimapMarker` と同じ数え方）。
 */
enum an_minimap_marker {
    AN_MARKER_NONE = 0,
    AN_MARKER_TREASURE = 1,
    AN_MARKER_ITEM = 2,
    AN_MARKER_MONSTER = 3,
    AN_MARKER_TRAP = 4,
};

/*!
 * @brief 正体不明のとき分類ごとの代表で描く 8 分類。
 * @details tval の実数値は版によって違うので、分類の判定は Angband のヘッダを
 * 見られるこの C 側（`an_map_shim.c`）で行い、C++ 側（`an_frame.cpp`）には
 * この列挙だけを渡す。
 */
enum an_item_category {
    AN_ITEM_CATEGORY_NONE = 0,
    AN_ITEM_CATEGORY_POTION = 1,
    AN_ITEM_CATEGORY_SCROLL = 2,
    AN_ITEM_CATEGORY_RING = 3,
    AN_ITEM_CATEGORY_AMULET = 4,
    AN_ITEM_CATEGORY_WAND = 5,
    AN_ITEM_CATEGORY_STAFF = 6,
    AN_ITEM_CATEGORY_ROD = 7,
    AN_ITEM_CATEGORY_MUSHROOM = 8,
    /*!
     * 上の 8 分類に当たらないが、見た目だけ分かる（flavor を持つ）分類。
     * 見直しの指摘 10・ 末尾の注意どおり、推測で代表を決めず報告に書く。
     * 現在 Angband 4.2.6 は `object_kind::flavor` を持つ tval が上の 8 だけなので、この値は使わない。
     */
    AN_ITEM_CATEGORY_OTHER_FLAVORED = 9,
};

typedef struct an_cell_info {
    int known; /*!< `square_isknown()`。偽なら他のフィールドは意味を持たない */
    int in_view;
    int is_player;
    int lighting; /*!< `enum grid_light_level`（0=LOS,1=TORCH,2=LIT,3=DARK）。参考値。契約上の 0/1/2 は `light_level` を見ること */
    /*!
     * @brief 契約上の光の段（見直しの指摘 5）。
     * @details 0=記憶のみ（!in_view）/ 1=視界内だが無灯（in_view && LIGHTING_DARK）/
     * 2=いま照明あり（in_view かつ LIGHTING_DARK 以外）。
     * 手本は `presentation_bridge.cpp:3665` と `silq/adapter/sq_shim.h:370`。
     */
    int light_level;
    int room; /*!< `square_isroom()` */
    int glow; /*!< `square_isglow()` */
    unsigned int tf_bits; /*!< 上の `AN_TF_*` を OR したもの */
    char feat_name[64]; /*!< `f_info[f_idx].name`（英語。UTF-8 のまま） */
    int has_monster;
    char monster_name[64]; /*!< 種族名（`race->name`）。見えている敵だけ（`in_view` ではなく `ml`。見直しの指摘 8） */
    unsigned int monster_slot; /*!< `m_idx`（個体の通し番号）。条件は `has_monster` と同じ */
    int has_object;
    char object_name[64]; /*!< アイテムの種類名（`first_kind->name`）。既知のときだけ */
    int object_tval; /*!< `first_kind->tval`（見直しの指摘 1/10） */
    int object_aware; /*!< `first_kind->aware`。偽なら分類の代表で描く */
    int object_category; /*!< `an_item_category`の値。`object_tval` から `an_map_shim.c` が判定済み */
    /*!
     * @brief 幻覚中か（`g.hallucinate`。見直しの指摘 7）。
     * @details 真なら `has_monster`/`has_object` は実際の値を持たない。
     * 表示は `display_attr`/`display_char`（`hallucinatory_object`/`_monster` 済み）を使うこと。
     */
    int hallucinating;
    /*!
     * @name マスの上に実際に出す一番上の絵（見直しの指摘 1、3、7、9）。
     * @details `ui-map.c` の `grid_data_as_text()` をそのまま呼んだ結果（自分・敵・罠・アイテム・地形の
     * 優先順位、幻覚・未見の金・未見のアイテムを全て原作と同じように解決済み）。
     * `ascii_fallback`/`fg_color` の材料にそのまま使う。
     * @{
     */
    unsigned char display_attr;
    char display_char;
    /*! @} */
    /*!
     * @name 地形だけの絵（`grid_data_as_text()` の `tap`/`tcp`）。
     * @details `under_tile_index` の代わりには使わないが（地形のタイルは名前で引く）、
     * 将来の字フォールバック用に控えておく。
     * @{
     */
    unsigned char terrain_attr;
    char terrain_char;
    /*! @} */
    /*!
     * @brief **プレイヤの光で照らされた（暗黒側なら暗くされた）マスか**（1 = はい）。
     * @details `LightingState::player_lit`の材料。
     * 光（`AN_LIGHT_TORCH`〜`AN_LIGHT_OTHER`）は原作の `SQUARE_CLOSE_PLAYER`（`cave-view.c`。
     * 光源の半径の内側で、視認できるマス。`map_info()` の `LIGHTING_TORCH` は設定
     * `view_yellow_light` に左右されるので使わない）。暗黒側（`AN_LIGHT_DARK`）は、
     * UNLIGHT の人なら闇の探知範囲のうち暗いまま（`LIGHTING_DARK`。明るいマスは暗くならない）のマス、
     * 負の光（`cur_light < 0`）なら `SQUARE_VIEW` かつ半径 `-cur_light - 1` 以内のマス。
     * 光が無い（`an_player_light_kind()` が 0）ときは常に 0。
     */
    int player_lit;
} an_cell_info;

/*!
 * @name プレイヤの光源の種類（`presentation/frame/game_frame.h` の `LightingState::light_kind` と同じ値）
 * @{
 */
#define AN_LIGHT_NONE 0 /*!< 光が無い */
#define AN_LIGHT_TORCH 1 /*!< 木の松明（燃料あり） */
#define AN_LIGHT_LANTERN 2 /*!< ランタン（燃料あり） */
#define AN_LIGHT_ARTEFACT 3 /*!< アーティファクトの光・燃料の要らない光 */
#define AN_LIGHT_OTHER 4 /*!< 光源の枠以外の光・燃料切れの光源 */
#define AN_LIGHT_DARK 5 /*!< 暗黒側（UNLIGHT の人・負の光）。暗い範囲を新しい見た目で出す */
/*! @} */

/*!
 * @brief プレイヤの光源の種類（`AN_LIGHT_*`）。読むだけ。
 * @details 光源の枠（`"light"`）の物を見る。UNLIGHT の人（ネクロマンサー。`PF_UNLIGHT`）で
 * `cur_light <= 1` なら、原作が闇の探知範囲で見る（`cave-view.c` の `update_view_one()`）ので暗黒側。
 * `cur_light < 0` も暗黒側。燃料の切れた光源は光らないので、ほかの光が残っていれば `AN_LIGHT_OTHER`。
 */
int an_player_light_kind(void);

/*! ダンジョンが在るか（`character_dungeon`）。偽なら他の map 系関数を呼ばない。 */
int an_map_ready(void);

/*! プレイヤの居るマス（フロア座標）。 */
int an_map_player(int *gx, int *gy);

/*! フロアの寸法。 */
int an_map_dims(int *w, int *h);

/*! AH-33。`L`（`do_cmd_locate()`）の「行 0 が Map sector で始まる」問いが出ている間だけ真。 */
int an_locate_active(void);

/*! AH-33。`Term->offset_x`/`offset_y`（`L` が動かす、見ている区画の左上隅）。 */
int an_panel_offset(int *ox, int *oy);

/*!
 * @brief 1 マスぶんの情報を読む。
 * @return 1 = 読めた（`known` を見ること）／0 = 範囲外
 */
int an_map_cell(int gx, int gy, an_cell_info *out);

/*!
 * @brief 1 マスの「ミニマップ種別」（`MinimapKind` と同じ数え方）。
 * @details `hd2d/world/floor_meaning.cpp` の `rebuild_floor_meaning()` は
 * `GameFrame::minimap` だけを見て地形の役割（壁／床／通路）を決める（`GameFrame::cells`
 * は見ない）。**これが空のままだと `role_at()` が常に `Unknown` を返し、
 * 描くマスが 0 のまま止まる**（HD2D 実機での見直しで特定。
 * `hd2d/world/floor_meaning.cpp:97-100` の `!minimap.valid()` の早期 return）。
 * @return 0=未探知／1=床／2=壁／3=扉／4=階段／5=アイテム／6=モンスター／7=自分／8=山（町の花崗岩）
 */
int an_minimap_kind(int gx, int gy);

/*!
 * @brief 1 マスのミニマップ印（`MinimapMarker`。見直しの指摘 3）。
 * @return `an_minimap_marker` の値
 */
int an_map_marker(int gx, int gy);

typedef struct an_floor_info {
    int depth; /*!< `player->depth`。0 = 町 */
    int is_town; /*!< `depth == 0` */
    int daytime; /*!< `is_daytime()` */
    int day_minute; /*!< 0〜1439。0 = 真夜中 */
    int light_radius; /*!< `player->state.cur_light`（負なら暗黒側。半径は絶対値 - 1） */
    int light_kind; /*!< プレイヤの光源の種類（`AN_LIGHT_*`） */
    long long generated_turn; /*!< `cave->turn`（見直しの指摘 13） */
    int profile_id; /*!< 階の作り方の番号（101〜108）。町・不明は 0 */
} an_floor_info;

/*! @return 1 = 読めた（`an_map_ready()` が真）／0 = まだ */
int an_get_floor_info(an_floor_info *out);

/*!
 * @brief プレイヤの種族名（`player->race->name`）。P 行の引きに使う。
 * @return 1 = 読めた（`character_generated` が真）／0 = まだ
 */
int an_get_player_race_name(char *out, int cap);

/* ==================================================================== HUD */

/*! `character_generated` が真の間、コアの左 13 桁・最下段は Term から読める（設計 §6.2）。 */
#define AN_STATUS_COL_COLS 13

typedef struct an_hud_info {
    char name[64];
    int hp;
    int hp_max;
    int sp;
    int sp_max;
    int gold;
    int depth; /*!< 階（0 = 地上）。フィートではない */
    int level;
} an_hud_info;

/*! @return 1 = 読めた（`character_generated` が真）／0 = まだ人物が居ない */
int an_get_hud(an_hud_info *out);

/* ==================================================================== メッセージ */

/*!
 * @brief `EVENT_MESSAGE` の受け手を仕込む。1 度だけ呼ぶ（`an_bootstrap()` の中）。
 * @details Sil-Q や変愚と同じく、**Term から拾わず** `event_add_handler` で積む
 * （設計 §6.2 の「メッセージ」の行）。
 */
void an_install_message_hook(void);

/*! 常時の効果の表が引いた項目（`struct ui_entry`）の記録を捨てる。`cleanup_angband()` のあと（2 回目のゲーム）に呼ぶ。 */
void an_perm_reset(void);

/*!
 * @brief 溜まったメッセージを 1 件取り出す。
 * @param out 受け皿。`cap` バイト
 * @param color_out TERM_* の色（0〜15 に丸めたもの）
 * @return 1 = 取れた／0 = もう無い
 */
int an_take_message(char *out, int cap, int *color_out);

/*!
 * @brief `init_angband()` から戻った直後に一度だけ呼ぶ（`an_bootstrap.c`）。
 * @details `an_recent_message_count()`/`an_get_recent_message()` は、これが
 * 立つまではコアの `messages_num()` を呼ばない（`EVENT_ENTER_INIT` 直後などに
 * 呼ぶと落ちるため。実測でクラッシュした）。
 */
void an_mark_messages_ready(void);

/*!
 * @brief 直近のメッセージ数（コアの `messages_num()` そのもの）。
 * @details AH-01/AH-02 の直し。`an_take_message()` と違い履歴を消費しない。
 * 呼び出し側（`an_frame.cpp` の `fill_messages()`）は他の 5 本のコアと同じく、
 * 直近 8 行を毎フレーム丸ごと送る（`presentation_bridge.cpp` の `fill_messages()` と同じ形）。
 */
int an_recent_message_count(void);

/*!
 * @brief 直近のメッセージを 1 件、消費せずに読む。
 * @param age 0 が最新（`message_str()` と同じ約束）。
 * @param out 受け皿。`cap` バイト
 * @param color_out TERM_* の色（0〜15 に丸めたもの）
 * @return 1 = 読めた／0 = その `age` は無い
 */
int an_get_recent_message(int age, char *out, int cap, int *color_out);

/*!
 * @brief コア自体の事象ではなく、アダプタが作った文面を同じキューへ積む（見直しの指摘 12）。
 * @details `an_shutdown_and_quit()` の拒否理由は `plog()`（stderr だけ）でしか
 * 出ていなかったので、画面側に届かない。`EVENT_MESSAGE` を経ずに
 * 直接キューへ積む。
 */
void an_push_synthetic_message(const char *text, int color);

/*!
 * @brief これまでにキューへ積んだ総数（消費しても減らない）。
 * @details 変化検出用 digest（`an_change_digest()`）の材料。コアの `messages_num()`
 * は内部の `messages` がまだ作られていない間（`EVENT_ENTER_INIT` 直後等）に
 * 呼ぶと落ちる（実測）ので使わない。こちらはアダプタ自体のキューだけを数えるので安全。
 */
unsigned long long an_message_event_count(void);

/* ==================================================================== サブパネル（設計 §6.3） */

/*!
 * @brief サブパネルの要約行 1 本の入れ物の幅（バイト。UTF-8 でも英語のみの M0 なら足りる）。
 * @details 変愚の `GameFrame::sub2_lines`（装備）/`sub3_lines`（視界の敵）/`sub5_lines`
 * （持ち物）と同じ「文字列の要約」を Angband から作る（`presentation_bridge.cpp` の
 * `fill_sub2_equipment()`/`fill_sub3_monsters()`/`fill_sub5_inventory()` を手本にした）。
 * Angband 4.2.6 には `PW_INVEN`/`PW_EQUIP`/`PW_MONLIST` という実際のサブウインドウの
 * 描画関数（`ui-display.c`）があるが、それを流用すると `angband_term[]` を追加で
 * 何枚も起こして `window_flag[]` を切り替える大掛かりな仕組みが要る
 * （Sil-Q の `sq_sub_terms.c` 相当）。**この案件はまだ M0（サブウインドウの選択画面も
 * 無い英語専用の段）なので**、まずは画面側が「コアがサブウインドウを申告しない
 * （`kind < 0`）ときの既定表示」として既に持っている `sub2_lines`/`sub3_lines`/
 * `sub5_lines`（`hd2d/ui/game_hud.cpp` の `draw_default_sub_panel()`）を直接埋める
 * 形にした。メッセージ（`frame.messages`）とキャラクター情報（`frame.hud`）は
 * 元から埋まっているので、ここで足すのは装備・持ち物・視界の敵の 3 つだけでよい。
 */
#define AN_SUBLIST_LINE_CAP 256

/*!
 * @brief 装備の要約（`player->body.slots[]`）。
 * @param out `max_lines` 行 x `AN_SUBLIST_LINE_CAP` バイトの受け皿
 * @return 書いた行数
 */
int an_equipment_lines(char out[][AN_SUBLIST_LINE_CAP], int max_lines);

/*!
 * @brief 持ち物の要約（`player->gear` のうち装備されていないもの）。
 * @param skipped_out 入り切らなかった件数（NULL 可）
 * @return 書いた行数
 */
int an_inventory_lines(char out[][AN_SUBLIST_LINE_CAP], int max_lines, int *skipped_out);

/*!
 * @brief 視界の敵の要約（`monster_list_shared_instance()` の LOS 区分）。
 * @return 書いた行数
 */
int an_visible_monster_lines(char out[][AN_SUBLIST_LINE_CAP], int max_lines);

/*!
 * @brief AH-04: 種類 id → 名前・tval（タイルの名寄せ用。`an_frame.cpp` 側は
 * `angband.h` を見られないので、名前文字列だけをここで作って渡す）。
 * @param kind_id `native_store_item::kind_id`（= Angband の `object_kind::kidx`）
 * @param name_out 受け皿（`AN_SUBLIST_LINE_CAP` より小さくてよい）
 * @param tval_out 生の tval（`list-tvals.h` の値）。NULL 可
 * @return 引けたら真
 */
int an_kind_name_and_tval(int kind_id, char *name_out, int cap, int *tval_out);

/*!
 * @brief AH-04: 持ち物一覧（装備＋パック＋矢筒）を通し番号で 1 件ずつ読む。
 * @details 手本は `silq/adapter/sq_native_look.c` の `sq_native_owned_item()`。
 * 装備 → パック → 矢筒の順で、空きは飛ばして通し番号を振る
 * （`presentation::read_native_owned_items()` は `read()` が 0 を返すまで
 * `index` を 1 ずつ増やすだけなので、実物が尽きたところで止まればよい）。
 * @return 読めたら真、`index` が総数を超えたら偽
 */
int an_native_owned_item(int index, native_store_item *out);

/*! AH-41: 店の一覧（`NativeStoreMenu`）。店に居なければ 0。`an_native_inventory.c`。 */
int an_native_store_info(native_store_info *out);
/*! AH-41: 0.. は店の品、その後に持ち物（装備・パック・矢筒）。尽きたら 0。 */
int an_native_store_item(int index, native_store_item *out);


/* ============================================ 状態のアイコン（`GameFrame::status_effects`） */

/*! @brief 状態のアイコン 1 個ぶん。文字列は UTF-8。 */
typedef struct an_status_effect {
    char key[40]; /*!< `hd2d/ui/status_icon_table.inc` にある名前 */
    char label[96]; /*!< アイコンの下の名前。コアの言語（日本語層が起きていれば日本語） */
    char amount[16]; /*!< アイコンの上の効果量（`+10` など）。無ければ空 */
    int kind; /*!< 0 良い・1 悪い・2 待っている・3 特殊・4 常時の良い・5 常時の悪い（`StatusEffectKind`） */
} an_status_effect;

/*! @brief 1 回に返す最大数（一時的な状態 53 + 常時の効果の表） */
#define AN_STATUS_MAX 160

/*!
 * @brief いまの状態を並べて返す。並びは (1) 速さ (2) 一時的な状態（`player->timed[]` の番号順。原作の
 * `prt_tmd()` と同じ） (3) 帰還・降下の待ち (4) 常時の効果。
 * @return 件数。キャラクターがいなければ 0。
 * @details 読むだけでコアの状態は変えない。常時の効果は、原作のキャラクターシートの耐性の表（`ui_entry.txt` の
 * `CHAR_SCREEN1`）と**同じ道**（`compute_ui_entry_values_for_object()` / `_for_player()`）で、
 * **鑑定済みの旗・既知の属性だけ**を拾う（未鑑定の旗は出さない）。
 */
int an_read_status_effects(an_status_effect *out, int cap);

/*! @brief 状態の組が変わったら変わる値（`an_change_digest()` に混ぜる）。一時的な状態の有無・装備の既知の旗の数。 */
unsigned long an_status_stamp(void);

/*!
 * @brief 状態のアイコンの表の自己検査（`--selftest`。キャラクター不要だが `init_angband()` の後であること）。
 * @param err 最初に外れた 1 件の説明
 * @param checked 画面側の表と突き合わせた鍵の数（環境変数 `HENGBAND_STATUS_ICON_TABLE` に
 * `hd2d/ui/status_icon_table.inc` の絶対パスがあるときだけ数える）
 * @return 外れた件数
 * @details (1) 原作の `TMD_*` が全部、表の行か理由つきの除外に当たる (2) 常時の効果の表の行が全部、
 * キャラクターシートの表の項目（`ui_entry.txt`）に結び付く (3) 送る鍵が全部、画面側の表にある。
 */
int an_status_selftest(char *err, int cap, int *checked);

/*! @brief 一時的な状態の表の行数・常時の効果の表の行数（`--selftest` の報告用）。 */
int an_status_timed_count(void);
int an_status_perm_row_count(void);
#ifdef __cplusplus
} /* extern "C" */
#endif
