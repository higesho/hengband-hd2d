/*!
 * @file nh_windows.h
 * @brief NetHack の専用の画面の窓の部品（メニュー・テキスト・問い・文字入力・拡張コマンド・キャラクタ作成・墓標・持ち物の常設の窓）。
 *
 * ## 何のためのものか
 * （N2・N3）。コアから届く要求（`menu`・`text`・`ask`・`select`・`rip`。要求の番号 `r` つき）を取り込み、
 * 原作の Windows の GUI 版（NetHackW）の窓の中身と並び・操作で描き、返事（`nethack_reply`）を作る。枠と地の色は HD2D の作り
 * （暗い下敷きと細い枠。D14）。窓が開いている間のキーはここが受け、コアへは返事だけを送る（`windows_capture_keys`）。
 *
 * ## 窓の出し方（原作の GUI 版と同じ並び）
 * - メニュー・テキスト・持ち物: 地図の右寄せ（`NhLayout::menu`。地図の幅の 1/4〜2/3、地図と同じ高さ）。
 * - yn・文字入力（`getlin`）: メッセージの窓の最終行に太字で出す（ゲームに入る前はメッセージの窓が無いので、中央の小さな窓で代える）。
 * - 名前・拡張コマンド・キャラクタ作成・墓標: 中央の窓（暗幕つき）。
 * - メニューの行の頭に、原作の 16×16 のタイルの絵（`glyph` を `tiles.json` の `glyph2tile` で引く。D16）。絵が読めなければ字だけ。
 *
 * ## 描きかたの約束（`NethackUi` が呼ぶ）
 * `draw` と `draw_invent` は四角（`UiPaint`）だけを積み、絵は `flush_tiles` まで貯める。呼ぶ側は `paint.flush()` のあとに `flush_tiles`、
 * そのあと字を流す（絵が四角の上、字が絵の上になる）。
 */
#pragma once

#include "app/native_core_ui.h" //!< NativePad（パッドの操作）
#include "nethack/nh_input.h"
#include "nethack/nh_layout.h"
#include "nethack/nh_menu.h"
#include "nethack/nh_model.h"
#include "nethack/nh_select.h"
#include "nethack/nh_tiles.h"
#include "render/text_overlay.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d::nethack {

//! 描く側が渡す環境。
struct NhDrawEnv {
    UiPaint &paint;
    TextOverlay &text;
    int screen_w{ 0 };
    int screen_h{ 0 };
    RectPx area;     //!< 使える範囲（下の案内の帯を引いたあと）。中央の窓の置き場
    NhRect menu;     //!< 右寄せの窓の矩形（`NhLayout::menu`）
    NhRect invent;   //!< 持ち物の常設の窓の矩形
    int cell_w{ 8 };
    int line_h{ 18 };
    int pad_x{ 6 };
    bool in_game{ false };
    bool japanese{ true };
};

//! 窓の部品が窓を閉じたときに残す結果。
struct NhOutcome {
    std::vector<std::string> replies; //!< `nethack_reply` の JSON の字（`type` は `nethack_reply`）
    std::string echo;                 //!< メッセージの窓へ残す 1 行（原作は答えた問いを履歴に積む）。無ければ空
    int echo_attr{ 1 };
};

//! 文字列を `width` 画素で折り返す（空白があればそこで切る。全角はどこでも切れる）。
std::vector<std::string> nh_wrap_to_width(TextOverlay &text, const std::string &line, int width);

class NhWindows {
public:
    enum class Kind { None, Menu, Text, Yn, Line, Name, ExtCmd, Select, Rip };

    void set_language(bool ja) { this->ja_ = ja; }
    //! 絵の一覧の置き場（`tiles.png`・`tiles.json` があるフォルダ）。変わったときだけ読み直す。読めなければ字だけで出す。
    void set_tile_dir(const std::string &dir);
    //! 受けた要求を取り込む。新しい要求（番号か届いた順が前と違う）だけ窓を作り直す。
    void sync(const NhModel &model);
    //! 窓が開いていて、キーを窓が受けるか。
    bool active() const { return this->kind_ != Kind::None; }
    Kind kind() const { return this->kind_; }
    //! 文字入力の窓が出ているか（ソフトキーボードと IME の欄の位置を出す）。
    bool wants_text_input() const;
    //! IME の欄の位置（画面の座標。描いたあとに有効）。
    bool text_input_rect(SDL_Rect &rect) const;

    //! SDL の出来事を 1 つ窓へ。食ったら真。返事ができたら `take_outcome` で取り出せる。
    bool on_event(const SDL_Event &event, bool reserved_key);
    //! 返事と、メッセージの窓へ残す行を取り出す（空なら何も無い）。
    NhOutcome take_outcome();
    //! 窓の操作の案内（下の帯の右側へ足す）。
    std::string key_hint(bool ja) const;

    /*!
     * @brief 右寄せの窓（メニュー・テキスト）が希望する幅（画素）。0 は希望なし。
     * @details 配置（`nh_compute_layout` の `menu_content_w`）へ渡す。中身の幅 + 余白。窓が開いていないときは、持ち物の常設の窓
     * （`perm_invent` が真のとき）の中身の幅。
     */
    int desired_width(TextOverlay &text, int cell_w, int line_h, int pad_x, bool perm_invent);

    //! 中央の窓・右寄せの窓を描く（四角と字を積む）。ゲームに入る前は、使える範囲いっぱいを黒で塗る。
    void draw(NhDrawEnv &env);
    //! 持ち物の常設の窓（`perm_invent`）を描く。
    void draw_invent(NhDrawEnv &env, const NhModel &model);
    //! ゲームの中の問い（yn・文字入力）が、メッセージの窓の最終行に出す字。出さないとき偽。
    bool message_row(std::string &text, bool &input_mode) const;
    //! メッセージの窓の最終行に出した入力のキャレットの位置を覚える（IME の欄・点滅の位置）。
    void set_message_caret(int x, int y, int h);
    //! 貯めた絵（メニューの行のタイル）を描く。`paint.flush()` のあとに呼ぶ。
    void flush_tiles(const UiImagePainter &images);
    //! glyph の絵を `dst` に貼るよう積む（コマンドの欄）。絵の一覧が読めない・絵が無いときは偽（何も積まない）。
    bool queue_glyph_tile(const RectPx &dst, int glyph);
    /*!
     * @brief はい／いいえの窓で選べる字（コマンドの欄に答えのボタンとして並べる。タッチだけの端末のため）。窓が違えば空。
     * @details `choices` があればその字（Esc より後の隠れた字は除く）。無ければ問いの文の最後の `[...]`（`[fg or ?*]`・`[a-e]` など）から拾う。
     */
    std::vector<int> yn_answer_keys() const;
    //! はい／いいえの窓に字で答える（キーを押したのと同じ。選べない字は捨てる）。
    void answer_yn(int key) { this->send_yn(key); }
    /*!
     * @brief パッドの操作。メニューは、十字の上下で行・一番下の行からさらに下で下のボタン（OK・キャンセル）へ移り、左右でボタンを替える。
     * A は行なら印を付ける（クリックと同じ）・ボタンならそれを押す。B は取り消し。ほかの窓は矢印・Enter・Esc のキーとして扱う。
     */
    bool on_pad(NativePad pad);
    //! メニューの下のボタンのどれにパッドのカーソルがあるか（-1 = 行の一覧・0 = OK・1 = キャンセル）。
    int menu_button_focus() const { return this->menu_btn_focus_; }

    //! 検査用。
    const NhMenu &menu() const { return this->menu_; }
    const NhSelect &select() const { return this->select_; }
    const NhTextField &field() const { return this->field_; }
    const std::string &prompt_text() const { return this->query_; }
    bool tiles_ready() const { return this->tiles_ready_; }

    void shutdown();

private:
    struct Hit {
        enum Type { MenuRow, Role, Race, Align, Gender, Play, Random, Quit, Ok, Cancel, Close, ExtRow, MenuScroll };
        RectPx rect;
        Type type{ MenuRow };
        int a{ 0 };
    };
    struct TileDraw {
        RectPx dst;
        int tile{ -1 };
    };
    struct TextLine {
        std::string text;
        int attr{ 0 };
    };

    void open_request(const NhModel::Request &request);
    void reply(const std::string &json, const std::string &echo = std::string(), int echo_attr = 1);
    void close();
    bool load_tiles();

    //! 要求の種類ごとの出来事。
    void on_text(const std::string &utf8);
    void on_keydown(const SDL_KeyboardEvent &key);
    bool on_mouse(const SDL_Event &event);
    void finish_menu(NhMenuResult result);
    void submit_text_field();
    void cancel_request();
    void send_yn(int key);
    void text_scroll(int rows);

    // ---- 描画（nh_windows_draw.cpp）----
    void draw_scrim(NhDrawEnv &env);
    RectPx centered(const NhDrawEnv &env, int w, int h) const;
    void draw_frame(NhDrawEnv &env, const RectPx &r, const std::string &title);
    void draw_button(NhDrawEnv &env, const RectPx &r, const std::string &label, bool focus, Hit::Type type, int a = 0);
    void draw_menu(NhDrawEnv &env);
    void draw_menu_rows(NhDrawEnv &env, NhMenu &menu, const RectPx &list, bool interactive, bool with_checks, bool collect_hits);
    void draw_text_window(NhDrawEnv &env);
    void draw_yn_dialog(NhDrawEnv &env);
    void draw_input_dialog(NhDrawEnv &env);
    void draw_ext_cmd(NhDrawEnv &env);
    void draw_select(NhDrawEnv &env);
    void draw_rip(NhDrawEnv &env);
    int draw_field(NhDrawEnv &env, const RectPx &box, bool caret, const std::string &prefix = std::string());
    int tile_px(int line_h) const;
    void wrap_text_lines(TextOverlay &text, int inner_w);
    bool tab_columns(TextOverlay &text, const NhMenu &menu, std::vector<int> &widths) const;
    int menu_content_width(TextOverlay &text, const NhMenu &menu, int cell_w, int line_h, bool with_checks) const;

    bool ja_{ true };
    Kind kind_{ Kind::None };
    int request_{ 0 };
    std::uint64_t seen_serial_{ 0 };

    NhMenu menu_;
    NhSelect select_;
    NhTextField field_;
    std::string query_;
    std::string choices_;
    char def_{ 0 };
    std::vector<std::string> ext_cmds_;
    int ext_pick_{ -1 };
    int ext_top_{ 0 };
    int menu_btn_focus_{ -1 }; //!< メニューの下のボタンのパッドのカーソル（-1 = 行の一覧）
    // テキストの窓・墓標
    std::vector<TextLine> text_lines_;
    std::vector<TextLine> text_wrapped_;
    int text_wrapped_w_{ -1 };
    int text_top_{ 0 };
    int text_page_{ 10 };
    struct Rip {
        std::string name;
        std::string killer;
        long gold{ 0 };
        int year{ 0 };
    } rip_;

    // 絵の一覧
    std::string tile_dir_;
    std::string tile_dir_loaded_;
    bool tiles_ready_{ false };
    UiImage tile_image_;
    NhTileTable tile_table_;

    // 描いたときに決まる物
    std::vector<Hit> hits_;
    std::vector<TileDraw> pending_tiles_;
    int menu_desired_w_{ 0 };
    std::uint64_t desired_serial_{ 0 };
    int desired_font_px_{ 0 };
    SDL_Rect caret_rect_{ 0, 0, 0, 0 };
    bool caret_valid_{ false };
    bool swallow_space_{ false };
    NhOutcome outcome_;

    // 持ち物の常設の窓
    NhMenu invent_;
    std::uint64_t invent_serial_{ 0 };
    std::uint64_t invent_desired_serial_{ 0 };
    int invent_desired_w_{ 0 };
};

} // namespace hd2d::nethack
