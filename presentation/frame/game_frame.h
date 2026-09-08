/*!
 * @file game_frame.h
 * @brief 1 フレーム分のプレゼンテーション入力（設計書 §4.6 / Phase4 Sub 拡張）
 *
 * 所属は presentation/frame 専属。ui は include するのみ（循環禁止）。
 */
#pragma once

#include "frame/combat_fx.h"
#include "frame/sound_event.h"
#include "frame/hud_snapshot.h"
#include "frame/map_cell_view.h"
#include "frame/message_event.h"
#include "frame/minimap_snapshot.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/*!
 * @brief サブパネルの枚数（`PanelId::Sub1`〜`Sub7`）。
 * @details **2026-08-19 に 5 → 7**（こう決めた——「パネルの分割数を指定できるように。
 * 下段を 2/3/4・右を 1/2/3」）。上限がちょうど 7 なのは偶然ではない——コアの
 * サブウインドウは `angband_terms[1..7]` の 7 枚で、これで**過不足なく重なる**
 * （`sdl_sub_window_terms.cpp` の `static_assert`）。5 枚だったころは窓 6・7 が
 * どこにも出ず、コアの `=`→`w` で印を立てても何も起きなかった。
 * @note 出る枚数は割り付け次第（既定は下段 3 ＋ 右 2 ＝ 5 枚で、従来と同じ絵）。
 * 出ていない枚の `kind` は -1（UI 既定）で、フラグも立てない。
 */
constexpr int kSubPanelCount = 7;

/*!
 * @brief SQ-2: コアが地図の上へ**一過性に重ね書きした**1 マス。
 *
 * @details 旧世代のコア（Sil-Q・幻想蛮怒）は、ダメージの数字・矢の飛跡・爆風・
 * 聞き耳の `*` を **`print_rel()` で主 Term の地図区画へ直に書いて**、少し待ってから
 * `lite_spot()` で消す。この層は `map_info()` を通らないので、マスの絵を
 * `map_info()` から組んでいる線には**1 つも届かない**。
 *
 * アダプタが「Term の地図区画」と「`cells` の見た目」を突き合わせ、**食い違ったマスだけ**を
 * ここへ積む。こうすると 8 系統がまとめて拾え、コアの木にフックを 1 つも刺さずに済む。
 *
 * @note **空なら codec が丸ごと省く。** 出さないコア（変愚・短愚蛮怒）のフレームは
 * 1 バイトも変わらない。
 */
struct MapOverlayCell {
    int16_t gx{};
    int16_t gy{};
    char ascii{}; //!< そのマスに重ねて出す 1 文字（ASCII）
    uint8_t color{ 1 }; //!< TERM_COLOR 相当（16 に畳んだ後）
};

/*!
 * @brief SQ-1: 敵の**警戒度**の段。
 * @details **コアに依らない粒度**にしてある（必守制約 1）。Sil-Q の
 * `get_alertness_text()` の境目そのままだが、名前は「眠り／気づいていない／気づいた」
 * という、どのローグライクにもある概念で付けた。
 * @note 出さないコアは `monster_alerts` を空にしておけばよい（既定）。
 */
enum class MonsterAlert : uint8_t {
    Unknown = 0, //!< 分からない（このマスの敵について何も言わない）
    Asleep = 1,
    Unwary = 2, //!< 起きているが、まだこちらに気づいていない
    Alert = 3,
};

/*!
 * @brief SQ-1: 見えている敵 1 体ぶんの警戒度。
 * @details **マスで指す**（個体番号ではなく）。`cells` と同じ座標系なので、画面側は
 * 実体を組むときに同じマスを引ける。空なら codec が丸ごと省く。
 */
struct MonsterAlertCell {
    int16_t gx{};
    int16_t gy{};
    uint8_t level{ static_cast<uint8_t>(MonsterAlert::Unknown) };
};

//! サブパネル 1 行（コアのサブウインドウ Term 由来）。
struct SubPanelLine {
    std::string text_utf8;
    uint8_t color{ 1 }; //!< TERM_COLOR 相当（行内で最初に見つかった文字の色）
};

/*!
 * @brief サブパネル 1 枚の中身（K-26）。
 * @details `kind >= 0` のとき、そのパネルは**コアのサブウインドウ**を映す
 * （`kind` は `SubWindowRedrawingFlag` の番号）。Bridge が専用の Term を 1 枚ずつ
 * `angband_terms[1..5]` に置き、コアの `fix_*` が描いた文字グリッドを読み取って積む。
 * `kind < 0` は「UI 既定」で、従来の作り込み表示（`sub2_lines` 等）をそのまま使う。
 * @note 文字グリッドなので**等幅フォントで描く**こと（Term ミラーと同じ理由。B-25）。
 */
struct SubPanelContent {
    int kind{ -1 };
    std::string title_utf8; //!< 種類名（見出し）。UI 既定のときは空
    std::vector<SubPanelLine> lines;
};

/*!
 * @brief K-32: Term の 1 行を「同じ色の連なり」に切った断片。
 * @details コアの最下行（ステータスバー）は**桁と色の両方に意味がある**
 * （空腹・朦朧などが色つきで左に、速度・学習・階層が右端の決まった桁に出る。
 * `main-window-row-column.h` の `COL_SPEED = -24` / `COL_STUDY = -13` / `COL_DEPTH = -8`）。
 * 1 行 1 色・1 本の文字列にすると、その両方が失われる。
 */
struct TermTextRun {
    int col{}; //!< Term 桁（0 起点）。描画側はこの桁に合わせて置く
    std::string text_utf8;
    uint8_t color{ 1 }; //!< TERM_COLOR 相当
};

/*!
 * @brief コアの色表（`angband_color_table` の 0〜15）を RGB で写したもの。
 * @details コアは**この表そのものを書き換える**道を 2 つ持っている。
 * `do_cmd_colors`（`&` の「(3) カラーの設定を変更する」。`cmd-io/cmd-dump.cpp:157-185`）と
 * ユーザ設定ファイルの `V:` 行（`io/interpret-pref-file.cpp:323`）で、どちらも
 * 書き換えたあと `term_xtra(TERM_XTRA_REACT)` で画面側に「引き直せ」と言う。
 *
 * **画面側が自分の写しをべた書きで持っていると、書き換えても 1 ドットも変わらない。**
 * 実際に長らくそうなっていた（`hd2d/render/term_colors.h` に 16 色の表があり、
 * `TERM_XTRA_REACT` は null term で no-op だった）。ここに載せて毎フレーム渡す。
 *
 * 48 バイトしかなく、しかも既定（＝コアの初期値）と同じ間は codec が丸ごと省くので、
 * 色をいじっていない通常のプレイでは 1 バイトも増えない。
 * @note 16 色で足りる。Bridge は色番号を `& 0x0F` で採る（上位ビットは JP の全角フラグ。
 * `presentation_bridge.cpp` の `kAfKanjic`）ので、16 以上の色は画面に出ようがない。
 */
struct TermPalette {
    //! 値はコアの既定（`src/term/gameterm.cpp:32` の `angband_color_table`）そのもの。
    std::array<std::array<uint8_t, 3>, 16> rgb{ {
        { { 0x00, 0x00, 0x00 } }, //!< 0 TERM_DARK
        { { 0xFF, 0xFF, 0xFF } }, //!< 1 TERM_WHITE
        { { 0x80, 0x80, 0x80 } }, //!< 2 TERM_SLATE
        { { 0xFF, 0x80, 0x00 } }, //!< 3 TERM_ORANGE
        { { 0xC0, 0x00, 0x00 } }, //!< 4 TERM_RED
        { { 0x00, 0x80, 0x40 } }, //!< 5 TERM_GREEN
        { { 0x00, 0x80, 0xFF } }, //!< 6 TERM_BLUE
        { { 0x80, 0x40, 0x00 } }, //!< 7 TERM_UMBER
        { { 0x40, 0x40, 0x40 } }, //!< 8 TERM_L_DARK
        { { 0xC0, 0xC0, 0xC0 } }, //!< 9 TERM_L_WHITE
        { { 0xFF, 0x00, 0xFF } }, //!< 10 TERM_VIOLET
        { { 0xFF, 0xFF, 0x00 } }, //!< 11 TERM_YELLOW
        { { 0xFF, 0x00, 0x00 } }, //!< 12 TERM_L_RED
        { { 0x00, 0xFF, 0x00 } }, //!< 13 TERM_L_GREEN
        { { 0x00, 0xFF, 0xFF } }, //!< 14 TERM_L_BLUE
        { { 0xC0, 0x80, 0x40 } }, //!< 15 TERM_L_UMBER
    } };

    bool operator==(const TermPalette &other) const { return this->rgb == other.rgb; }
    bool operator!=(const TermPalette &other) const { return !(*this == other); }
};

/*!
 * @brief Term の 1 行の中で「同じ色が続いている区間」（UTF-8 バイト位置）。
 * @details `TermMirrorLine::color` は 1 行 1 色なので、**1 行に何色も出る画面**が潰れる。
 * いちばん分かりやすいのがカラーの設定（`&` →(3)）の見本行で、コアは
 * `term_putstr(i * 4, 22, -1, i, format("%3d", i))`（`cmd-io/cmd-dump.cpp:112`）と
 * **数字 1 つずつ別の色**で描くのに、ミラーでは行頭の色（＝黒）に染まって
 * **見本がまるごと見えなかった**。
 * @note 位置は `MenuChoice` と同じく UTF-8 の**バイト位置**（Term 列ではない）。
 */
struct TermColorSpan {
    int begin{ 0 };
    int len{ 0 };
    uint8_t color{ 1 }; //!< TERM_COLOR 相当
};

//! MenuOverlay 用 Term ミラー1行（読取専用・Phase8）。
struct TermMirrorLine {
    std::string text_utf8;
    uint8_t color{1}; //!< 行の代表色。`color_spans` が空のときだけ使う
    int source_row{0}; //!< Term 論理行（カーソル照合用）

    /*!
     * @name K-23: この行の中で `TERM_YELLOW` で書かれた最初の連続部分（UTF-8 バイト位置）
     * @details birth（種族・職業・性格・魔法領域・性別・オートローラー）はコアが
     * **色だけ**で選択中の項目を示す（`c_put_str(TERM_YELLOW, ...)`。
     * `birth-select-race.cpp:76` ほか）。**印は色だけ**なので、`》` を探す
     * `fill_core_cursor` では見つからない。ここに位置を採っておき、
     * `fill_birth_cursor` が「いま選んでいる項目」の枠にする。
     * `len == 0` なら黄色は無い。
     * @note 色として描くぶんには `color_spans` があれば足りる。この欄が要るのは
     * **どれを選んでいるかという意味**を取り出すためで、描画のためではない。
     * @{
     */
    int highlight_begin{0};
    int highlight_len{0};
    /*! @} */

    /*!
     * @brief 行の中の色の切れ目。**空なら行全体が `color`。**
     * @details 1 行が 1 色で済む行（ほとんどの行）では空にしておく。Bridge が色の
     * 変わり目を見つけたときだけ、行全体を覆う区間の並びとして積む
     * （隙間は無い。先頭から末尾まで連続する）。
     * @note **末尾に置いてある。** 位置の並びで初期化している所（`hd2d_app.cpp` の
     * 合成画面）が幾つもあり、途中へ入れるとそれが全部ずれる。
     */
    std::vector<TermColorSpan> color_spans;
};

/*!
 * @brief Term ミラー上の「1 キーで決まる選択肢」1 件（K-16 / 設計書 §14）。
 * @details 建物・店・アイテム選択は**コアが 1 文字キーを待つ**だけで、カーソル操作の
 * 仕組みがコア側に無い。コアは非破壊（必守制約 1）なので、Bridge が Term ミラーから
 * 選択肢を読み取り、ui がカーソルを重ねて**決定でその 1 キーだけ** KeyQueue へ積む。
 * @note 位置はすべて `TermMirrorLine::text_utf8` の**バイト位置**（Term 列ではない）。
 * 全角は 1 セル 2 列だが UTF-8 では 3 バイトなので、列で持つと描画側でずれる。
 */
struct MenuChoice {
    int line_index{0}; //!< `menu_term_lines` の添字
    int span_begin{0}; //!< 選択肢の表示範囲（キー文字＋ラベル）の開始バイト
    int span_len{0}; //!< 同・バイト長
    int key_begin{0}; //!< 決定キーを表す文字そのものの開始バイト（`i/e)` の `i` / `e`）
    int key_len{0}; //!< 同・バイト長
    int key{0}; //!< KeyQueue へ積む 1 キー（ESC=0x1B / スペース=0x20）
};

/*!
 * @brief K-20: 1 行プロンプト（`y/n` など）の選択肢 1 件。
 */
struct PromptChoice {
    std::string label_utf8;
    int key{0}; //!< 決定したとき KeyQueue へ積む 1 キー
    //! 行の中でその文字（`[y/n]` の `y` / `n`）が置かれているバイト位置。枠を重ねる場所。
    int begin{0};
    int len{0};
};

/*!
 * @brief K-20: Term 行 0 の「はい／いいえ」プロンプト。
 * @details `input_check_strict()`（`src/core/asking-player.cpp:220`）は
 * `'y'` / `'n'` しか受け付けず、**`'\r'` は `bell()` で撥ねる**。しかも
 * `screen_save()` を通らないので icky にならず、MenuOverlay にも出ない。
 * つまり従来はキーボードの `y` を打つ以外に「はい」と答える方法が無かった。
 * ここを選択肢として持ち上げ、ui がカーソル付きの小窓を出して 1 キーだけ積む。
 * @note `choices` が空＝プロンプトは出ていない。
 */
struct PromptBar {
    std::string text_utf8; //!< プロンプト本文（`[y/n]` を含む行そのもの）
    std::vector<PromptChoice> choices;
    /*!
     * @brief 小窓に出す操作説明。空なら `y/n` 用の既定文言。
     * @details K-37 で `[S]弾, [A]矢 :`（矢弾の作成）のような**はい／いいえではない**
     * プロンプトも同じ器に載せたので、「B/ESC:いいえ」を出しっぱなしにできなくなった。
     */
    std::string footer_utf8;
    /*!
     * @brief 元の画面（Term ミラー）の中での行。`< 0` はミラーに出ていない画面。
     * @details 数値入力と同じで、出ている画面（店・建物）では**別窓を出さず
     * `[y/n]` の `y` / `n` にそのまま枠を重ねる**。`choices[i].begin/len` は
     * この行の `text_utf8` に対するバイト位置。
     */
    int line_index{-1};
};

/*!
 * @brief K-22: 数値入力（`input_quantity` / `input_numerics`）の状態。
 * @details どちらも `input_string(prompt, len, initial, numpad_cursor=false)` →
 * `askfor()`（`src/core/asking-player.cpp`）で、**Term 行 0 に
 * 「プロンプト＋編集中の文字列」**が出る。プロンプトの末尾は必ず `(最小-最大): ` の形
 * （`input_quantity`＝`いくつですか (1-%d): ` / `input_integer`＝`<文言>(%d-%d): `）。
 * ここから最小・最大・いまの値を読み、ui がカーソル（上下＝増減／左右＝桁）を重ねる。
 * @note `active` が偽なら数値入力は出ていない。
 */
struct NumericInput {
    bool active{false};
    std::string prompt_utf8; //!< `(最小-最大): ` までのプロンプト（編集中の値は含まない）
    int min{0};
    int max{0};
    int value{0}; //!< いま Term の編集バッファに入っている値
    int digits{1}; //!< `max` の桁数（＝桁カーソルの可動域）
    int text_len{0}; //!< いま編集バッファに出ている**文字数**（`digits` 未満なら 0 詰めが要る）

    /*!
     * @name 元の画面（Term ミラー）の中での値の位置
     * @details 店や建物のようにプロンプト行がミラーに出ている画面では、**別窓を出さずに
     * その場の数字へ桁カーソルを重ねる**（人間の指示「元のウインドウでカーソルによる
     * 数値入力にはできないか？」）。`line_index < 0` はミラーに出ていない画面
     * （ダンジョンで個数を聞かれる等。icky にならないので Term 行 0 はどこにも描かれない）で、
     * その場合だけ ui が小窓を出す。位置は `text_utf8` の**バイト位置**。
     * @{
     */
    int line_index{-1};
    int value_begin{0};
    int value_len{0};
    /*! @} */
};

/*!
 * @brief K-40: 転移（帰還・テレポート）の演出状態。
 * @details ui はコアに触れないので（必守制約 6）、「いま転移が近い／いま転移した」を
 * 知る手立てが GameFrame 以外に無い。Bridge がコアの帰還カウンタと**プレイヤ格子の跳び**
 * からこの 2 値だけを作り、見た目（青み・収束・破裂）は全部 ui 側で決める。
 * @note コアは読取のみ（必守制約 1）。カウンタを消費したり乱数を引いたりはしない。
 */
struct TeleportFx {
    /*!
     * @brief 溜めの進み具合 0..1。0 = 平常。
     * @details 帰還の巻物・現実変容のように**残りターンを持つ**転移だけがここを上げる。
     * 上限は 0.85 で、残りの 0.15 は発動の瞬間（`burst` の直前）のために空けてある。
     */
    float charge{ 0.f };
    /*!
     * @brief 転移が起きたフレームだけ真（立ち上がり 1 フレーム）。
     * @details 消費側は `GameFrame::frame_id` で二重取りを防ぐこと。`capture` は入力待ちの
     * 間も 10ms ごとに回るので、同じ縁を何度も拾うと破裂がその場で繰り返される。
     */
    bool burst{ false };
};

/*!
 * @brief フロアの素性（ボクセル HD2D 設計書 §12-1・§12-2。P3 で新設）。
 *
 * @details ボクセル HD2D は「同じ階へ戻ると同じ絵になる」ことを要求する（§9.3）。
 * そのために `seed = hash(絶対gx, 絶対gy, フロア識別子)` で見た目を引くので、
 * **フロアを一意に指す番号**が要る。
 *
 * `generated_turn` が要るのは、**現実変容などで作り直された同じ階を別物と見分ける**ため。
 * `dungeon_id` と `dun_level` だけだと、作り直された階に前の階の記憶が乗る。
 *
 * @note ここは**追加だけ**の変更なのでプロトコルの版は上げない（v1 §3.2）。
 * 既存の 旧2D画面 は未知キーとして黙って捨てる（v1 §2.3）。
 * @note フロアの**寸法**は足していない。`MinimapSnapshot::width/height` が既に運んでいる。
 */
struct FloorIdentity {
    //! `FloorType::dungeon_id`。地上は 0。
    int dungeon_id{ 0 };
    //! `FloorType::dun_level`。地上は 0。
    int dun_level{ 0 };
    //! `FloorType::generated_turn`。**この階が作られたときの turn**。
    uint64_t generated_turn{ 0 };
    /*!
     * @brief フロアの種別（§12-2）。意味づけの規則を選ぶのに使う。
     * @details 0 = 不明 / 1 = 地上（町・荒野）/ 2 = ダンジョン / 3 = クエスト階 / 4 = 闘技場。
     */
    int kind{ 0 };
    /*!
     * @brief 町の番号（§12-2「町 N 番」）。町の中でなければ 0。
     *
     * @details **P10 第 2 期（町の建物）で足した。**足す理由は 2 つある。
     *
     * 1. **町は訪れるたびに同じでなければならない。**ボクセル HD2D の見た目の種は
     *    `generated_turn` を混ぜている（`terrain_view.cpp` の `cell_seed`）が、
     *    町のフロアは出入りのたびに作り直されるので turn が変わる。そのままだと
     *    **町から出て戻るたびに建物の意匠が入れ替わる**。地上では turn を混ぜず、
     *    代わりにこの番号を混ぜる
     * 2. 町ごとに作りを変えられる（辺境の地と大都市で同じ意匠にしない）
     *
     * コアの `FloorType` からは取れないので `AngbandWorld::get_town_index()` から採る。
     * @note 追加だけの変更なのでプロトコルの版は上げない（v1 §3.2）。
     */
    int town_id{ 0 };
    /*!
     * @brief **いまいる場所の名前**（`src/floor/floor-util.cpp` の `map_name()` そのもの）。
     *
     * @details 「イークの洞窟」「辺境の地」「地上」。P10 のカットイン演出
     * （階が変わったときに地名と階層を出す。2026-08-11 に決めた）で要る。
     *
     * **ui 側で組み立てられない。**町の名前は `TownList`、ダンジョンの名前は
     * `DungeonList` にあり、クエスト階・闘技場・広域マップは別扱いになる——
     * その分岐はコアが `map_name()` に持っているので、**答えだけ**を運ぶ。
     * `status_col_lines`（Term の写し）から掬うこともできるが、桁の詰め方が変わると
     * 黙って壊れる（罠 36 と同じ形）。
     *
     * @note 追加だけの変更なのでプロトコルの版は上げない（v1 §3.2）。
     */
    std::string place_name_utf8;
    /*!
     * @brief **広域マップ（全体マップ）にいるか**（`AngbandWorld::is_wild_mode()`）。
     *
     * @details `kind` では見分けられない——広域マップも `Surface` で、`town_id` は
     * 荒野と同じ 0 になる。カットイン演出は「地上とダンジョンだけ・**全体マップには
     * 出さない**」（2026-08-11 に決めた）ので、この 1 ビットが要る。
     * @note 同上、版は上げない。
     */
    bool wild_mode{ false };
};

//! `FloorIdentity::kind` の値。
enum class FloorKind : int {
    Unknown = 0,
    Surface = 1,
    Dungeon = 2,
    Quest = 3,
    Arena = 4,
};

/*!
 * @brief 光の状態（ボクセル HD2D 設計書 §12-4・§12-5。P5 で新設）。
 *
 * @details 設計書は「**昼夜は単項目あたりの効果が最大**」と言っている（照明が全部変わるため）。
 * 地形の色でも実体の色でもなく**時刻そのもの**を渡すのは、方向光の向き・色・強さと
 * 環境光を UI 側で連続に作るため。「昼か夜か」の 2 値だと日の出・日没が階段状に飛ぶ。
 *
 * @note `day_minute` を分（整数）で持つのは JSON の往復で誤差を出さないため。
 * コアの `AngbandWorld::extract_date_time()` が (日, 時, 分) を返すので `時*60+分` にする。
 * @note ここは**追加だけ**の変更なのでプロトコルの版は上げない（v1 §3.2）。
 * 既存の 旧2D画面 は未知キーとして黙って捨てる（v1 §2.3）。
 */
struct LightingState {
    /*!
     * @brief 一日のうちの位置（分。0〜1439）。0 = 真夜中・720 = 正午。
     * @details コアの一日は `TURNS_PER_TICK * TOWN_DAWN` ターンで、`extract_date_time()` が
     * これを 24 時間へ割り付けている。太陽の高度と方位はここから引く。
     */
    int day_minute{ 720 };
    /*!
     * @brief コアの言う「日中」か（`AngbandWorld::is_daytime()`）。
     * @details `day_minute` から導けそうに見えるが**導けない**。コアの日中判定は
     * 「一日の前半かどうか」で、`extract_date_time()` の時刻とは 1/4 日ずれた基準を使う。
     * モンスターの湧きや町の明るさはこちらの真偽値で動いているので、そのまま渡す。
     */
    bool daytime{ true };
    /*!
     * @brief プレイヤの光源半径（マス。`PlayerType::cur_lite`）。0 なら灯り無し。
     * @details 松明の減衰をなめらかに描くのに要る。コアの `light_level` は
     * マスあたり 0/1/2 の 3 段しか無いので、これだけでは丸い減衰が作れない。
     */
    int light_radius{ 0 };
};

/*!
 * @brief **周囲の地形の内訳**
 *
 * ## なぜマスごとではなくまとめて送るのか
 *
 * 環境音は「いま周りに何が在るか」で層を重ねる。材料は `MapCellView` にも在りそうに
 * 見えるが、**`feature_flags`（uint16）は `0x8000` の 1 ビットしか空いていない**。
 * 草・土砂・沼・深い水・山で 5 つ要るので入らず、幅を広げると 17 要素固定タプル
 * （`frame_codec.cpp` の `enc_cell`）の形が変わる。
 *
 * そのうえ**マスごとに 1 要素足すのは高い**——見えているマスは 1,400 前後あるので、
 * 毎フレーム数 KB 増える。**内訳なら 12 バイトで済む。**
 *
 * ## 数えるのはコア側
 *
 * 地形の意味（草か土か沼か）を知っているのは `TerrainCharacteristics` を読める側だけで、
 * **画面側は地形テーブルを知らない**（設計 §4.1）。だから Bridge が数えて入れる。
 * `SoundEvent` の曲がり角を画面側で計算しないのと同じ理由である。
 *
 * @note **数えないコアが在ってよい。**`radius == 0` は「数えていない」の意味で、
 * 画面側は層を使わずベッド 1 本に落ちる（`has_sub_panel_kinds` と同じ流儀）。
 * @note 追加だけの変更なのでプロトコルの版は上げない（v1 §3.2）。
 */
struct SurroundingsView {
    //! @name 割合。**0..255 が 0..100%**（合計は 100% を超えうる——壁は床と重ならないが、水と草は同じマスに無い前提で数える）
    //! @{
    uint8_t grass{}; //!< 草・花・薮
    uint8_t tree{}; //!< 立ち木
    uint8_t dirt{}; //!< 土・砂
    uint8_t swamp{}; //!< 沼
    uint8_t water{}; //!< 浅い水
    uint8_t deep_water{}; //!< 深い水。**海鳴りの層はこれで決まる**
    uint8_t lava{}; //!< 溶岩（浅い・深いをまとめる）
    uint8_t rock{}; //!< 山・岩・瓦礫
    uint8_t glass{}; //!< ガラス
    uint8_t wall{}; //!< 壁。**閉塞の層はこれで決まる**
    //! @}
    //! 数えた半径（マス）。**0 なら「数えていない」**。
    uint8_t radius{};
    //! 数えたマスの数（既知のものだけ）。0 なら同上。
    uint16_t counted{};

    //! 数えた結果が入っているか。
    bool valid() const { return (this->radius > 0) && (this->counted > 0); }
};

struct GameFrame {
    uint64_t frame_id{};
    int cam_x{};
    int cam_y{};
    int view_w{};
    int view_h{};
    //! プレイヤのフロア格子座標（Phase5 P5-C / H2: cam ではなくこれを隣接判定に使う）。
    int player_gx{};
    int player_gy{};
    std::vector<MapCellView> cells;
    //! ミニマップ用のフロア全域。cells がビューポート限定なので別に持つ。
    MinimapSnapshot minimap;
    HudSnapshot hud;
    std::vector<MessageEvent> messages;
    /*!
     * @brief SQ-2: コアが地図へ**一過性に重ね書きした**マス（`MapOverlayCell` の註記）。
     * @details 出すのは Sil-Q だけ（`sq_menu.h` の兄弟、`sq_shim.h` の `sq_read_overlay`）。
     * **空なら codec が丸ごと省く**ので、他コアのフレームは 1 バイトも変わらない。
     * `combat_fx` と違って**汲んだら消える性質は無い**——「いま Term に載っているもの」を
     * 毎フレーム写すだけなので、消えるときは自然に空になる。
     */
    std::vector<MapOverlayCell> map_overlay;
    /*!
     * @brief SQ-1: 見えている敵の警戒度（`MonsterAlertCell` の註記）。
     * @details 画面側は実体の足元へ**段ごとに色の違うリング**を敷く
     * （2026-08-22 に決めた）。空なら codec が丸ごと省く。
     */
    std::vector<MonsterAlertCell> monster_alerts;
    /*!
     * @name SQ-2 の 8 系統目 — 照準のマス
     * @details `hilite_target` は Term の**カーソルを動かすだけ**なので
     * `map_overlay`（`print_rel` の差分）では 1 度も拾えない。位置を直に運ぶ。
     * **-1 が「出ていない」**（既定なので codec が省く）。
     * @{
     */
    int16_t target_gx{ -1 };
    int16_t target_gy{ -1 };
    /*! @} */
    /*!
     * @brief この 1 フレームで起きた戦闘の見せ場。
     * @details **溜めずに毎回入れ替わる。** コア側は `take_events` で汲み出して空にするので、
     * 同じ出来事が 2 度入ることはない。`capture` は入力待ちの間も 10ms ごとに回るため、
     * 「消さずに番号で照合する」作りにすると同じ縁を拾い続けて演出が止まらなくなる。
     */
    std::vector<CombatFxEvent> combat_fx;
    /*!
     * @brief この 1 フレームで鳴らすべき音。
     * @details **コアは鳴らさず、ここへ載せるだけ**にした（画面側が HRTF で定位して鳴らす）。
     * `combat_fx` と同じく**汲んだら消える**——`capture` は入力待ちの間も回るので、
     * 残す作りにすると同じ音を鳴らし続ける。
     * @note 載るのは画面側が `ui_state.audio.sound_events` を立てたときだけ。
     * 立てていなければコアが従来どおり自分で鳴らす（移行の途中でも二重に鳴らない）。
     */
    std::vector<SoundEvent> sounds;
    std::string controller_hint;
    /*!
     * @brief コアが自由文字入力（`askfor`）の中にいるか。
     * @details 名前入力・銘の刻印・検索語など。**ソフトキーボードしか無い環境
     * （Android）で、キーボードを自動で出す／しまう判断に使う。**
     * 画面の見た目から推測せず、コアの `text_input_state_hook` からそのまま採る
     * （`presentation::install_text_input_hook`）。
     */
    bool text_input_active{false};
    /*!
     * @brief **コアがいま「次の命令」を待っているか**（v1 の追補。2026-08-23）。
     *
     * @details こう決めた——「A ボタンに、決定と同じく既定でコマンドメニューを置きたい。
     * コアは改変しない。中間層で対応できないか」への答えがこの 1 ビットである。
     *
     * 変愚蛮怒は `\r` を受けると自分の通常メニューを開く（`command_menu` オプション。
     * `src/io/input-key-requester.cpp:117`）が、**Sil-Q にはその仕組みが無い**
     * （`silq/src/dungeon.c` の `process_command()` に `\r` の枝が無い）。
     * だから画面側が自前のコマンドメニューを開くしかないが、**いつ開いてよいか**は
     * 画面には分からない——`-more-` の待ちで開くと、メッセージが進まなくなる。
     *
     * その判別はコアだけが持っている。Sil-Q なら `inkey_flag`（`util.c:1826` の註記
     * 「TRUE なら通常の命令を待っている」）がそのもので、**アダプタが読むだけでよい**
     * ——`silq/src/` には 1 バイトも触らない（必守制約 2）。
     *
     * @note **送らないコアでは偽のまま**で、画面の動きは 1 ビットも変わらない
     * （変愚系はこれまでどおり `\r` を送り、コア自身のメニューが開く）。
     */
    bool awaiting_command{false};
    /*!
     * @brief **コアが @ から離れた所を見ている**（`cam_x` / `cam_y` を注視点にすること）。
     *
     * @details ふつう見下ろしのカメラは @ を追う。ところが Sil-Q の `L`
     * （地図を動かす。`do_cmd_locate`）のように、**@ を動かさずに見る場所だけ動かす**
     * 命令がある。カメラが @ を追ったままだと、送られてくるマスだけが移って
     * **絵は動かず、窓から外れたブロックが消える**（2026-08-26 に気づいた
     * 「動かない。左を入れると少し離れた場所のブロックが透過した」）。
     *
     * **`cam` と @ の差では見分けられない。** 階の端では追従を切ったコアが
     * ふつうに丸めるので、差はいつでも出る。**コアが「いま離れて見ている」と
     * 言うときだけ**真になる。
     *
     * @note **送らないコアでは偽のまま**で、カメラの動きは 1 ビットも変わらない。
     */
    bool camera_detached{false};
    bool menu_open{false};
    //! true = タイトル／新規作成／死亡プロンプトなど。本文は MainMap 全面。
    //! false かつ menu_open = ゲーム中メニュー → MenuOverlay 窓。
    bool pre_game_menu{false};

    /*!
     * @brief **重ねのメニューで、地図はまだ生きている**か。
     * @details `menu_open` は「Term ミラーを前に出す」だが、**地図を消さずに
     * 小さな箱を重ねるだけ**の画がある（Frox のコマンドメニュー。`util.c` の
     * `inkey_from_menu()` は `screen_save()` で icky を上げてから、地図の上へ
     * `+----` の箱を `put_str` するだけ）。**そこで写しを全面に出すと、写しに
     * 載っている地図が「入り込んだ」ように見える**（2026-09-01 に実機で見つけた）。
     *
     * 真のとき、アダプタは **`menu_term_lines` を箱の行だけに刈り込む**。
     * 画面側は `frame_shows_map()` を真とみなして**立体を描き続け**、
     * 箱だけをその上に重ねる。
     *
     * @note **送らないコアでは偽のまま**で、絵は 1 ビットも変わらない
     *       （持ち物・店のように地図を消してから描く画は、今までどおり全面の写し）。
     */
    bool menu_over_map{false};

    /*!
     * @brief K-47: **起動からオープニングを抜けるまで**（＝タイトル画を敷いてよい間）か。
     * @details `pre_game_menu` はタイトル・birth・死亡後をまとめて指すので、
     * 「タイトル画だけに背景画を出す」判断には粗すぎる（birth のロール結果の裏に
     * 玉座の絵が出てしまう）。ここはコアの状態から推測せず、
     * `presentation::set_title_screen()` の上げ下げをそのまま写す。
     * @note **起動直後から真**である。データ初期化（`init_angband`）はオープニング選択より
     * 前に走り、その間も画面には進捗が出ているので、そこも同じ絵の上に出す。
     * 降ろすのは `sdl_choose_new_or_load()` を抜けるとき。
     * @note ここの既定が偽なのは「合成フレームの既定はタイトルではない」という意味でしかない。
     * 本線では `capture` が毎フレーム `g_title_screen` を写すので、この既定は効かない。
     */
    bool title_screen{false};

    //! Sub2 装備要約（最大 8 行）。空なら Renderer が "(empty)" 相当を出す。
    std::vector<std::string> sub2_lines;
    //! Sub3 視界モンスター要約（最大 8 行）。
    std::vector<std::string> sub3_lines;
    //! Sub5 インベントリ要約（最大 12 行）。超過は末尾 …(+N)。
    std::vector<std::string> sub5_lines;

    /*!
     * @brief K-26: 各サブパネルに「コアのサブウインドウ」を割り当てた結果。
     * @details 添字 0..4 が `PanelId::Sub1`〜`Sub5`。`kind < 0`（既定）の枚は
     * 従来どおり `sub2_lines` 等・メッセージ・HUD を描く。
     */
    std::array<SubPanelContent, kSubPanelCount> sub_panels{};

    /*!
     * @brief K-28: MainMap の左に出すキャラクター状態（コアの左フレーム 13 桁の写し）。
     * @details コアは主画面の列 0〜12 に種族・称号・レベル・経験値・所持金・装備記号・
     * 能力値・AC・HP・SP・切り傷・朦朧・空腹・状態・思い出・日付・ダンジョン名を描く
     * （`src/window/main-window-row-column.h` / `print_frame_basic`・`print_frame_extra`）。
     * Bridge がその 13 桁をそのまま読んで積む（**作り直さない**。書式・色はコアのまま）。
     * 空なら描かない（オープニング・birth のようにキャラクターがまだ無い画面）。
     * @note 文字グリッドなので**等幅フォント**で描くこと。
     */
    std::vector<SubPanelLine> status_col_lines;

    /*!
     * @brief `status_col_lines` の桁数（コアが左フレームに取っている幅）。
     * @details **0 は「申告なし」**で、描画側は従来どおり 13 桁で幅を取る。
     * コアごとに違う数なので**画面側に持たせない**（必守制約 1）——
     * 変愚は 13（`main-window-row-column.h`）、**幻想蛮怒は 20**（`COL_MAP`。
     * `gensoband/src/defines.h:1489`）。申告が無いころの絵を変えないために
     * 既定は 0 のままにしてある（古いコアと噛み合う。v1 §3.2）。
     * @note 申告しないと 20 桁の列が 13 桁で切れる。「夢殿大祀廟の洞窟」が
     * 「夢殿大祀廟の洞」になる、という形で実際に出ていた。
     */
    int status_col_cols{ 0 };

    /*!
     * @brief `status_col_lines` を置く側。**0 = 左・1 = 右**。
     * @details 既定 0 なので、申告しない古いコアはこれまでどおり左に出る（v1 §3.2 の互換）。
     * 右を申告するのは FroxComposband だけ（あちらの `ui_char_info_rect()` は右端 12 桁）。
     * 機能メニューの「状態列の位置」が `自動` のときにこの値へ従い、`左 / 右` を
     * 選んだら人の選択が勝つ（`hd2d/ui/hd2d_settings.h` の `StatusColSide`）。
     * @note **digest には混ぜない**——起動から終了まで動かない値である
     */
    int status_col_side{ 0 };

    /*!
     * @brief K-32: コアの**最下行（ステータスバー）**そのまま（色つき断片・桁つき）。
     * @details 左から状態異常・空腹・休息などの記号（`print_status`）、
     * 右端に速度・学習・階層（`print_speed` / `print_study` / `print_depth`）。
     * ControllerBar の帯に、コアと同じ桁の並び・同じ色で描く。
     * 空ならその行には何も出ていない（オープニング・birth を含む）。
     */
    std::vector<TermTextRun> bottom_row_runs;
    //! `bottom_row_runs` の桁の総数（Term の幅）。描画側が帯幅へ割り付けるのに使う。
    int bottom_row_cols{ 0 };

    /*!
     * @brief K-33: 階層（`地上` / `NN 階`）。**MainMap の右下**に出す。
     * @details 人間の指示「メインパネルの右下にだせないか？」。コアは最下行の右端
     * （`COL_DEPTH = -8`）に書くが、横に長い画面ではそこが地図から遠く離れてしまう。
     * 中身は `print_depth` の文字そのまま、色も**階の雰囲気**（`DungeonFeeling`）のまま。
     * `text_utf8` が空なら出さない（オープニング・birth）。
     * @note 重複を避けるため、この 1 か所だけに出す
     * （状態列にも最下段の帯にも入れない。`bottom_row_runs` からは桁で除いてある）。
     */
    SubPanelLine depth;

    /*!
     * @brief コアの色表（`&` で変えられる 16 色）。描画側の色はすべてここから引く。
     * @details 毎フレーム載せる。既定と同じ間は codec が省くので実質ただ。
     */
    TermPalette term_palette;

    //! icky 中の Term 全文ミラー（menu_open 時のみ Bridge が充填）。
    std::vector<TermMirrorLine> menu_term_lines;
    /*!
     * @name 文字入力のキャレット位置。負値＝出さない。
     * @details **どちらも `menu_term_lines` の中での位置**である。
     * - `menu_term_curs_col`: その行の先頭からの Term 桁（`text_utf8` のバイト位置ではない）
     * - `menu_term_curs_row`: **`menu_term_lines` の添字**（Term の行番号ではない）
     *
     * `menu_term_curs_row` を Term の行番号のまま渡してはいけない。ミラーは空行を捨てて詰めるので
     * 添字と Term 行が一致せず、描画側（`draw_term_mirror`）は添字として使う。
     * 長らく Term 行のまま渡していて、**キャレットが別の行に出るか、丸ごと消えていた**
     * （行数より大きい値になると描画側の範囲検査で落ちる）。
     * @{
     */
    int menu_term_curs_col{-1};
    int menu_term_curs_row{-1};
    /*! @} */

    //! K-16: `menu_term_lines` から読み取った選択肢。空ならカーソル操作の対象外の画面。
    std::vector<MenuChoice> menu_choices;

    /*!
     * @name ページを繰るキー（2026-08-29 に決めた「ヘルプで左右でページ送りをしてほしい」）
     *
     * @details 0 なら「この画はページで出来ていない」。0 でなければ**画面側が左右を
     * このキーへ翻訳する**（上下は今までどおり項目のカーソル）。
     *
     * ヘルプのように**一覧と本文が同じ画で入れ替わる**画のためにある。本文には
     * 選択肢が 1 つも無いので、左右を「選択肢の送り」に使う道が無く、遊ぶ側からは
     * ページを繰る手がパッドに 1 つも無かった。
     *
     * **入れるのはアダプタである。**画面側はコアの字を読まない（読むと 4 本の
     * コアごとに文言の表を持つことになる）。入れていないコアでは 0 のままなので、
     * 左右は今までどおり選択肢を動かす。
     * @{
     */
    int menu_page_prev_key{ 0 };
    int menu_page_next_key{ 0 };
    /*! @} */

    /*!
     * @brief K-20: **コアが自前で持っているカーソル**の位置（`》` / `> ` の行）。
     * @details コマンドメニュー（`inkey_from_menu`）・呪文・持ち物・特殊能力・ペットなど、
     * `use_menu` が真のときコアは選択行の頭に `》`（EN は `"> "`）を書く
     * （`input-key-requester.cpp:137` / `display-inventory.cpp:92` /
     * `spell-info.cpp:290` / `mind-power-getter.cpp:269` / `cmd-pet.cpp:617` ほか）。
     * これを見つけて**同じ枠**を描けば、コアのカーソルも UI のカーソルも見た目が揃う。
     * @note `key_len` は 0（下線を引く「決定キー」がコア側の画面には無い）。
     * `line_index < 0` ＝ コアのカーソルは出ていない。
     */
    MenuChoice menu_core_cursor{ -1, 0, 0, 0, 0, 0 };

    /*!
     * @brief 画面に出ている `》` **すべて**（`menu_core_cursor` はその先頭）。
     * @details コマンドメニューでサブメニューへ入ると、コアは**親の行にも `》` を残す**
     * （どこから降りてきたかを示すため）。先頭 1 個だけ枠にすると、残りが素の `》` のまま
     * 描かれ「枠付きと `》` の 2 種類のカーソルが同時に出る」ことになる。
     * 見た目を枠へ統一するため、UI はこの配列**全部**に枠を描く。
     */
    std::vector<MenuChoice> menu_core_cursors;

    //! K-20: `y/n` プロンプト。`choices` が空なら出ていない。
    PromptBar prompt;

    //! K-22: 数値入力。`active` が偽なら出ていない。
    NumericInput numeric;

    //! K-40: 転移の演出状態（HD2D のパーティクルが読む）。
    TeleportFx teleport_fx;

    //! ボクセル HD2D P3: フロアの素性（種の安定と意味づけの規則選択に使う）。
    FloorIdentity floor;

    //! ボクセル HD2D P5: 光の状態（昼夜・時刻・プレイヤの光源半径）。
    LightingState lighting;

    //! 周囲の地形の内訳。
    SurroundingsView surroundings;
};
