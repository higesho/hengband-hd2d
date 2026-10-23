/*!
 * @file dcss_window.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）の窓の部品の状態。原作のウェブタイル版が送る窓の JSON から作る。
 *
 * ## 何のためのものか
 * 決めたこと D10（画面は原作のタイル版の UI を再現する）と段 U3。
 * ウェブタイル版は、メニュー・説明・問い・キャラクター作成の格子などの窓を、次の JSON で送ってくる
 *。ここはその行を 1 行ずつ受けて、積まれている窓（`DcssWindow`）を覚える。
 * 描画にも通信にも触れない（単体で検査できる）。描くのは `dcss_window_draw.cpp`、入力は `dcss_window_input.cpp`。
 *
 * ## 受ける型
 * - `menu`（`type:"crt"` を含む）・`update_menu`・`update_menu_items`・`menu_scroll`・`close_menu`・`close_all_menus`・`title_prompt`
 * - `ui-push`・`ui-pop`・`ui-state`（15 種の窓。`type` で区別する）
 * - `txt`（80 桁の文字画面。`id: "menu_txt"`）・`init_input`・`close_input`（1 行の文字入力）
 *
 * 窓の積み方は、アダプタの `dcss/adapter/dcss_frame.cpp`（M0 の文字画面への起こし）と同じ規則。
 */
#pragma once

#include "dcss/dcss_cells.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hd2d::dcss {

//! 原作 `MenuFlag`（`menu.h`）。ウェブ版の `enums.js` の `menu_flag` と同じ値。
enum DcssMenuFlag : int {
    kMfNoSelect = 0x00001,
    kMfSingleSelect = 0x00002,
    kMfMultiSelect = 0x00004,
    kMfSelectQty = 0x00008,
    kMfAnyPrintable = 0x00010,
    kMfInitHover = 0x00040,
    kMfWrap = 0x00080,
    kMfAllowFormatting = 0x00200,
    kMfNoWrapRows = 0x00800,
    kMfStartAtEnd = 0x01000,
    kMfQuietSelect = 0x04000,
    kMfUseTwoColumns = 0x08000,
    kMfArrowsSelect = 0x40000,
    kMfGridLayout = 0x100000,
};

//! タイル 1 枚（`menu` の `tiles[]`・`ui-push` の `tile`）。`tex` は画像ページの番号（原作 `TEX_*`）。
struct DcssTileRef {
    int t{ 0 };
    int tex{ 0 };
    int ymax{ 0 };
};

//! メニューの項目 1 つ（`menu` の `items[]`）。
struct DcssMenuItem {
    std::string text; //!< 書式つき（`<yellow>…</yellow>`）。「ホットキー + 名前」が 1 本の字に入っている
    int quantity{ 0 };
    int colour{ kLightGrey };
    std::vector<int> hotkeys;
    int level{ 2 }; //!< 0・1 = 見出し・小見出し、2 = 項目（原作 `MEL_*`）
    std::vector<DcssTileRef> tiles;
    bool placeholder{ true }; //!< まだ中身が届いていない（`total_items` より先に出た枠）
};

//! `menu` 1 枚。
struct DcssMenuData {
    std::string tag;
    int flags{ 0 };
    std::string title; //!< 書式つき
    std::string more;
    std::string alt_more;
    std::vector<DcssMenuItem> items;
    int last_hovered{ -1 };
    int jump_to{ 0 };
    int total_items{ 0 };
    bool replace{ false };
};

/*!
 * @brief 積まれている窓 1 枚。
 * @details 種類は 3 つ: メニュー（`menu`）・全画面の文字画面（`menu` の `type:"crt"`。内容は `DcssWindowStack::crt_lines`）・
 * 構造を持つ窓（`ui-push`。`type` が名前、`data` が JSON 本体、`state` が `ui-state` で更新される値）。
 * 描画側が持つ操作の状態（捲り位置・窓の中の強調など）も同居させる（窓が消えれば一緒に消えるため）。
 */
struct DcssWindow {
    enum class Kind { Menu, Crt, Ui };
    Kind kind{ Kind::Menu };
    int generation{ 0 };
    bool centred{ false }; //!< 原作 `ui-centred`（開始前の窓は中央。ゲーム中は上寄せ）
    DcssMenuData menu;
    std::string type; //!< `ui-push` の `type`
    nlohmann::json data;
    nlohmann::json state;

    // --- 描画・入力が持つ操作の状態 ---
    int scroll_y{ 0 }; //!< 捲り位置（画素。メニューの本文・捲れる文書・説明の本文）
    int pane{ 0 }; //!< 窓の中の面（神の画面・怪物の説明の `!` で切り替わる）
    int hover_button{ -1 }; //!< 作成の格子の強調（マウスの下）
    int focus_hotkey{ 0 }; //!< 作成の格子のキーボードの強調（`outer_menu_focus`・`ui-state` の `button_focus`）
    int hover_action{ -1 }; //!< 説明の下の操作の文の強調
    int crt_pending_focus{ -1 }; //!< 文字画面（開始メニュー）で、マウスの下へ移すために選択を送って、まだコアの返事が来ていない項目の番号
    std::uint32_t crt_pending_at{ 0 }; //!< その送った時刻（ミリ秒）
    int crt_last_focus{ -2 }; //!< 開始メニューで、直前の描画の選択中の項目の番号（変わったら、セーブデータの欄を選択中の項目が見える位置へ捲る）
    int first_visible{ 0 }; //!< メニューの見えている最初・最後の項目（`menu_scroll` に送る）
    int last_visible{ 0 };
    int last_sent_first{ -2 };
    int last_sent_hover{ -2 };
    bool keep_scroll_bottom{ false };
    bool view_init{ false }; //!< 最初の描画で捲り位置・強調を整えたか
    std::string typed; //!< 窓の中の 1 行入力（`msgwin-get-line`）の打ち込み（画面が覚える。`init_input` が来る入力は `DcssTextInput` が持つ）
    std::uint64_t opened_at{ 0 };
};

//! 開始メニューのモード 1 つ（アダプタが送る `{"t":"dcss_ui","k":"startup"}` の `modes[]`。原作 `startup.cc` の `game_modes_menu_item`）。
struct DcssStartupMode {
    int id{ 0 };
    std::string label;
    std::string desc;
    int tile{ 0 };
};

//! 開始メニューのセーブデータ 1 つ（同 `saves[]`）。`doll` は `[タイル番号, 下を切る高さ]` を下の層から順に並べた物。
struct DcssStartupSave {
    std::string name;
    std::string label;
    bool wiz{ false };
    bool loadable{ true };
    std::vector<std::pair<int, int>> doll;
};

/*!
 * @brief 開始メニュー（`startup.cc` の `UIStartupMenu`）の材料。原作のタイル版と同じ形に描くために、アダプタがゲームの中から読んで送る（`dcss/adapter/dcss_ui_startup.h`）。
 * @details 選択中の項目・名前・説明の行・操作の案内は、これまでどおり文字画面（`txt`）と原作のキー処理が持つ。
 */
struct DcssStartupData {
    bool valid{ false };
    std::string p_name; //!< 「名前を入力:」
    std::string p_modes; //!< 「ゲームモード:」
    std::string p_saves; //!< 「セーブデータ:」
    std::string p_new_game; //!< 「新しくゲームを始める」
    std::vector<DcssStartupMode> modes;
    std::vector<DcssStartupSave> saves;
};

//! `{"t":"dcss_ui","k":"startup",…}` の JSON から読む。読めなければ偽。
bool dcss_parse_startup(const nlohmann::json &m, DcssStartupData &out);

//! 1 行の文字入力（`init_input`）。
struct DcssTextInput {
    bool active{ false };
    std::string prompt;
    std::string tag;
    std::string prefill;
    std::string typed; //!< 画面が覚えている打ち込み（`prefill` から編集した結果）
    bool replace_on_first_key{ false };
    int maxlen{ 0 };
};

class DcssWindowStack {
public:
    /*!
     * @brief `webtiles` の 1 行（原作の JSON の字。先頭 `*` はサーバ宛の制御なので捨てる）を取り込む。
     * @return 窓の状態が変わったか
     */
    bool ingest_line(const std::string &raw);

    //! 積まれている窓（下から上の順）。
    const std::vector<DcssWindow> &windows() const { return this->windows_; }
    std::vector<DcssWindow> &windows() { return this->windows_; }
    bool empty() const { return this->windows_.empty(); }
    //! いちばん上の窓。無ければ nullptr。
    DcssWindow *top() { return this->windows_.empty() ? nullptr : &this->windows_.back(); }
    const DcssWindow *top() const { return this->windows_.empty() ? nullptr : &this->windows_.back(); }
    //! いちばん上のメニュー（`menu` の更新の宛先）。
    DcssWindow *top_menu();
    DcssWindow *top_ui(const std::string &type);

    //! 全画面の文字画面の行（HTML のまま。0〜23 行）。
    const std::array<std::string, 24> &crt_lines() const { return this->crt_lines_; }

    const DcssTextInput &text_input() const { return this->input_; }
    /*!
     * 名前の問い（原作 `newgame.cc` の `_choose_name`。「あなたの名前は？」）。キャラクター作成の最後（武器を選んだあと）に、名前を空にして始めたときに出る。
     * ウェブタイル版の構造は、この問いの窓を送らない（`_choose_name` は `push_ui_layout` を呼ばない。ウェブサーバー版は `DGAMELAUNCH` でこの問い自体を飛ばす）。
     * 来るのは、`resumable_line_reader` の作成（`cio.h`）が出す `init_input`（`type` = `messages`）の直後の `close_input` だけ（この読み手は 1 字も読まずに戻る。
     * 字はあとから窓のキーの処理が `putkey` で入れる）。そこで、**ゲームに入る前**（`set_pregame(true)`）に `init_input` の直後に `close_input` が来たら、名前の問いが
     * 出ているとみなす。ゲームの画面が来る（`set_pregame(false)`）か、窓が積まれる（Esc で作成の窓へ戻る）と消える。
     * 出ている間は、打った字・Enter・Esc・`*` が窓の部品に届くよう、1 行入力（`text_input`）を立てる。
     */
    bool name_prompt() const { return this->name_prompt_; }
    //! 名前の問いの題に出す「種族と職業」（直前の作成の窓の題から取る。取れなければ空）。
    const std::string &name_prompt_job() const { return this->name_job_; }
    //! 「*」（ランダムな名前）を押した。名前はコアが決めるので、画面は字を知らない。
    bool name_prompt_random() const { return this->name_random_; }
    void set_name_prompt_random(bool v) { this->name_random_ = v; }
    //! ゲームに入る前か（画面の側が毎回伝える）。真から偽へ変わると名前の問いを閉じる。
    void set_pregame(bool pregame);
    DcssTextInput &text_input() { return this->input_; }

    //! 変わるたびに増える（描き直しの合図）。
    std::uint64_t version() const { return this->version_; }
    //! キャラクター作成の窓を 1 度でも見たか。
    bool creation_started() const { return this->creation_started_; }
    //! 死亡・勝利の後の画面（`game-over`）が積まれているか。
    bool game_over() const;

    //! 画面が窓の外から押されたとき（Esc を送ったとき）などのために、窓が閉じられた通し数。
    std::uint64_t close_serial() const { return this->close_serial_; }

    /*!
     * Android・Quest 用の足し物（原作の Android 版のゲームメニューが持つ項目）。ゲームメニュー（`tag` が `game_menu`）の「情報を調べる」の次に
     * 「画面上のキーボードを表示/非表示にする」（原作 `CMD_TOGGLE_KEYBOARD`。`main.cc` の `#ifdef __ANDROID__`）を足す。
     * 原作のコアは `__ANDROID__` を外して組んでいる（`dcss_android_core.cmake`）ので、メニューに無い。選ぶと、ホットキー `kToggleKeyboardKey` が
     * 窓の部品に届き、コアへは送らず画面の側で受ける（`DcssWindows::send_key`）。
     */
    void set_keyboard_item(bool on, const std::string &label) { this->keyboard_item_ = on; this->keyboard_label_ = label; }

private:
    void on_menu(const nlohmann::json &m);
    void on_update_menu(const nlohmann::json &m);
    void on_update_menu_items(const nlohmann::json &m);
    void on_menu_scroll(const nlohmann::json &m);
    void on_ui_push(const nlohmann::json &m);
    void on_ui_state(const nlohmann::json &m);
    void on_txt(const nlohmann::json &m);
    void pop_menu_family();
    void pop_ui();

    std::vector<DcssWindow> windows_;
    std::array<std::string, 24> crt_lines_{};
    DcssTextInput input_;
    bool pregame_{ false };
    bool input_fresh_{ false }; //!< 直前の行が `init_input`（`type` = `messages`）だった
    bool name_prompt_{ false };
    bool name_random_{ false };
    std::string name_job_;
    std::string last_newgame_title_; //!< 直前の作成の窓の題（タグを除いた字）
    std::uint64_t version_{ 0 };
    int generation_{ 0 };
    bool creation_started_{ false };
    std::uint64_t close_serial_{ 0 };
    bool keyboard_item_{ false };
    std::string keyboard_label_;
};

//! ゲームメニューに足した「画面上のキーボードの切り替え」のホットキー（原作の値ではない。コアへは送らない）。
inline constexpr int kToggleKeyboardKey = -7012;

/*!
 * @brief 原作の書式つき文字列（`<yellow>…</yellow>`・`<w>`・`<h>`・`<<`）を色つきの字の列にする。
 * @details `dcss_parse_tagged` との違い: 原作の `formatted_string::get_colour` と同じく、`<h>` = 黄・`<w>` = 白を読み、
 * `<bg:…>` は読み飛ばし（背景色は使わない）、読めないタグは原作どおり字として明るい赤で残す。
 * `default_colour` は最初の字の色、`base_colour`（省略時は同じ）は閉じタグで積みが空になったときに戻る色
 * （原作は項目の色で始めても、閉じタグのあとは薄い灰色に戻る）。
 */
DcssStyledLine dcss_parse_window_text(std::string_view text, int default_colour = kLightGrey, int base_colour = -1);
//! 色のタグを外した字。
std::string dcss_strip_window_text(std::string_view text);
//! `\n` で行に分ける（色は引き継ぐ）。
std::vector<DcssStyledLine> dcss_split_styled_lines(const DcssStyledLine &line);

} // namespace hd2d::dcss
