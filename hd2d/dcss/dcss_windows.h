/*!
 * @file dcss_windows.h
 * @brief DCSS の窓の部品（メニュー・説明・文字画面・キャラクター作成の格子ほか）の描画と操作。段 U3。
 *
 * ## 何のためのものか
 * 決めたこと D10・D12: 画面は原作のタイル版の UI を再現し、操作は全部原作どおりにする。
 * ほかのコアの「新しい窓（modern_ui）」は使わず、コアが `webtiles` で送る窓の JSON（`DcssWindowStack`）から、
 * 原作の手元のタイル版の見た目（`ui::Popup` の暗幕・金茶の枠・メニューの項目の絵と強調）で自分で描く。
 * 操作は、ウェブタイル版のブラウザ側（`menu.js`・`ui-layouts.js`）がブラウザの中で受け持っていた分
 * （強調の移動・頁送り・捲り）をここで行い、コアへは原作の制御メッセージ（`key`・`menu_hover`・`menu_scroll` ほか）を送る。
 *
 * ## 呼び出し
 * `DcssUi`（`dcss_ui.h`）が持つ。窓が開いている間（`active()`）はキー入力を全部ここが受け、窓の外へは流さない
 * （窓の中のキーが HD2D の別の操作に化けないため）。コアへ送る行は `take_outgoing()` で取り出す
 * （`{"msg":"key","keycode":13}` のような原作の制御メッセージの JSON）。
 */
#pragma once

#include "dcss/dcss_cells.h"
#include "dcss/dcss_window.h"
#include "dcss/dcss_window_layout.h"
#include "render/text_overlay.h"
#include "ui/ui_image.h"
#include "ui/ui_paint.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

union SDL_Event;

namespace hd2d::dcss {

//! タイルの画像ページと切り出しの表（`assets/ui/dcss/atlas/`）。窓の中の絵（項目・説明の絵）を描く。
class DcssWindowAtlas {
public:
    ~DcssWindowAtlas();
    //! 表と画像ページを読む（1 回だけ。失敗したら偽のままで、窓は絵なしで出る）。
    bool load(std::string &err);
    bool ready() const { return this->ready_; }
    bool has(int tile) const;
    /*!
     * @brief タイル 1 枚を (x, y) に描く（原作 `TileBuffer::add`）。
     * @param scale 拡大率（整数。説明の窓は 2）
     * @param ymax 0 より大きければ、絵の下をここで切る（`menu` の `tiles[].ymax`）
     */
    void draw(const UiImagePainter &images, int tile, int x, int y, int scale = 1, int ymax = 0) const;
    //! タイルの大きさ（原作 `tile_info` の `width`・`height`）。
    bool size(int tile, int &w, int &h) const;
    void shutdown();

private:
    struct Info {
        char page{ 0 };
        int w{ 0 };
        int h{ 0 };
        int ox{ 0 };
        int oy{ 0 };
        int sx{ 0 };
        int sy{ 0 };
        int ex{ 0 };
        int ey{ 0 };
    };
    static int page_index(char page);

    bool ready_{ false };
    bool tried_{ false };
    std::vector<Info> info_; //!< タイル番号で引く（page == 0 は無し）
    std::vector<UiImage> pages_;
};

class DcssWindows {
public:
    DcssWindows();
    ~DcssWindows();
    DcssWindows(const DcssWindows &) = delete;
    DcssWindows &operator=(const DcssWindows &) = delete;

    //! 表示の言語（`ja` / `en`）。メニューの「先頭」「末尾」などの字に効く。
    void set_language(const std::string &lang)
    {
        this->japanese_ = (lang != "en");
        this->stack_.set_keyboard_item(this->android_extras_, this->japanese_ ? "画面上のキーボードを表示/非表示にする" : "Toggle on-screen keyboard");
    }
    /*!
     * Android・Quest 用の足し物（ゲームメニューの「画面上のキーボードを表示/非表示にする」。`DcssWindowStack::set_keyboard_item`）を使うか。
     * 選ばれたら `handler` を呼び（画面のキーボードの出し入れ）、ゲームメニューを閉じる（Esc を送る）。
     */
    void set_android_extras(bool on, std::function<void()> handler)
    {
        this->android_extras_ = on;
        this->keyboard_handler_ = std::move(handler);
        this->set_language(this->japanese_ ? "ja" : "en");
    }
    //! ゲームに入る前か（毎回伝える）。名前の問い（`DcssWindowStack::name_prompt`）の検出に使う。
    void set_pregame(bool pregame) { this->stack_.set_pregame(pregame); }
    //! 名前の問いが出ているか。出ている間は、窓が無くても画面が描く（`draw`）。
    bool name_prompt() const { return this->stack_.name_prompt(); }
    //! Esc（原作の Android 版の戻るキー `SDLK_AC_BACK` = `CK_ESCAPE`）を窓の部品の送り先へ送る。
    void send_escape() { this->send_key(27); }
    //! Enter（`-more-` を進める。原作の「`-more-` の間はどこを押しても進む」`MOUSE_MODE_MORE` の左クリック）を送る。
    void send_enter() { this->send_key(13); }
    //! 任意のキーをコアへ送る（文字入力が開いていれば、打った字の覚えも更新する。メッセージ欄の問いのマウス操作が使う）。
    void send_keycode(int keycode) { this->send_key(keycode); }

    //! コアから届いた `webtiles` の 1 行を取り込む。
    bool ingest_line(const std::string &line);
    //! アダプタが送る開始メニューの材料（`{"t":"dcss_ui","k":"startup",…}` の JSON 全体）を取り込む。
    bool ingest_startup(const nlohmann::json &message) { return dcss_parse_startup(message, this->startup_); }
    DcssWindowStack &stack() { return this->stack_; }
    const DcssWindowStack &stack() const { return this->stack_; }

    /*!
     * @brief 窓の部品の層の矩形と字の大きさを知らせる（毎フレーム呼んでよい。GL が要る）。
     * @param area 窓の部品の層（ウィンドウ全体の使える範囲）
     * @param crt_px 窓の字の高さ（`tile_font_crt_*` の既定の表）
     * @param small_layout 小さい配置か
     * @return 字を開けたか。失敗したら `err` に理由
     */
    bool configure(const RectPx &area, int crt_px, bool small_layout, std::string &err);

    //! 起動の読み込み画面（原作 `loading-screen.cc` のタイトルの絵）を出す間か。窓が 1 枚も開いていない最初の間だけ真（コアが原作のデータを読んでいる間）。
    bool title_pending() const { return !this->title_done_ && this->stack_.empty(); }
    //! 起動の読み込み画面を描く（`title_pending()` が真のとき。絵は `assets/ui/dcss/title/*.png` から 1 枚を無作為に選ぶ。無ければ何も出さず終わる）。
    void draw_title(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h);

    //! 窓か文字入力があって、キーを窓が受けるか。
    bool captures_keys() const { return !this->stack_.empty() || this->stack_.text_input().active; }
    bool has_windows() const { return !this->stack_.empty(); }

    //! 窓を描く（下から順に）。呼ぶ前に、下の層の四角・字・絵は流してあること。
    void draw(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h);
    //! 窓の拡大の倍率（Android・Quest。1 = 拡大しない）。字を拡大して描き、切り取り（`glScissor`）の矩形にも掛ける。座標は論理の座標のまま。
    void set_ui_scale(float scale) { this->ui_scale_ = (scale > 0.f) ? scale : 1.f; }
    //! 窓の部品の層の矩形（`configure` で渡した物）。
    const RectPx &area() const { return this->area_; }
    /*!
     * 捲れない窓（作成の格子・文字画面・開始メニュー）が開いているとき、それが収まるのに要る層の高さ（論理）。無ければ 0。
     * 呼ぶ側（`DcssUi`）は、層の高さがこれに届くように窓全体を縮めて描く（案内の帯の上に収める。 D28）。
     */
    int fit_need_h() const;

    /*!
     * @brief SDL の出来事を 1 つ受ける。
     * @param reserved_key HD2D が自分で使うキー（機能メニュー・Alt+Enter）。窓が開いていても食わない
     * @return 窓が食ったか（真なら呼び出し側はほかへ流さない）
     */
    bool on_event(const SDL_Event &event, bool reserved_key);

    //! コアへ送る行（原作の制御メッセージ。届いた順）を取り出す。
    std::vector<std::string> take_outgoing();

    /*!
     * 画面の側で積んだ窓（題の `tag` が `hd2d_pad` で始まるメニュー。ゲームパッドの割り当ての画面。`dcss_pad_screen.h`）がいちばん上のとき、
     * 窓の操作がコアへ送るはずだった行（`key`・`menu_hover` ほか）を、コアへは送らずこの受け手へ渡す。
     */
    void set_local_handler(std::function<void(const std::string &)> handler) { this->local_handler_ = std::move(handler); }

    void shutdown();

    //! 検査用: 窓の種類・題・大きさの 1 行の説明。
    std::string describe() const;

private:
    struct View;
    struct ScrollContent;

    // --- 描画の道具 ---
    bool ensure_font(std::string &err);
    //! 窓の枠の余白（`dcss_popup_place` の `pad`）。
    int fit_pad() const;
    //! 論理の座標を窓の拡大の倍率で画素にする（切り取りの矩形用）。
    int scaled(int v) const { return static_cast<int>(std::lround(static_cast<float>(v) * this->ui_scale_)); }
    int draw_styled(int x, int y, const DcssStyledLine &line, int max_w = 0);
    void draw_lines(int x, int y, const std::vector<DcssStyledLine> &lines);
    void begin_pass(UiPaint &paint, int screen_w, int screen_h);
    void end_pass(UiPaint &paint);
    void draw_scrim_and_frame(UiPaint &paint, const DcssPopupGeometry &g);
    void draw_menu(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images);
    void draw_crt(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images);
    //! 開始メニューを原作のタイル版の形で描く（段 U3）。材料が無い・文字画面が開始メニューでなければ描かず偽を返す。
    bool draw_startup(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images);
    void draw_ui(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images);
    void draw_scroll_text(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images, const ScrollContent &content);
    void draw_newgame_choice(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images);
    //! 名前の問い（原作 `newgame.cc` の `_choose_name`）を描く。窓は無い（原作はこの問いの窓をウェブタイル版へ送らない）ので、画面が組む。
    void draw_name_prompt(UiPaint &paint, int screen_w, int screen_h);
    bool on_name_mouse(const SDL_Event &event);
    void draw_scroller_bar(UiPaint &paint, const RectPx &region, int total, int scroll);

    // --- 入力の道具 ---
    void send_line(const std::string &line);
    void send_key(int keycode);
    void send_text(const std::string &text);
    void send_hover(int index, bool mouse);
    void send_scroll_if_changed(DcssWindow &w);
    bool on_key_down(const SDL_Event &event, bool reserved_key);
    bool on_text(const SDL_Event &event);
    bool on_mouse(const SDL_Event &event);

    // メニューの操作（ウェブ版 `menu.js` の移植）
    void menu_set_hovered(DcssWindow &w, int index, bool snap, bool from_mouse);
    void menu_cycle_hover(DcssWindow &w, bool reverse);
    int menu_next_hoverable(const DcssWindow &w, bool reverse, int starting_point, bool start_at_starting_point) const;
    void menu_paging(DcssWindow &w, bool up);
    void menu_scroll_to_item(DcssWindow &w, int index);
    void menu_scroll_bottom_to_item(DcssWindow &w, int index);
    void menu_line_step(DcssWindow &w, int dir);
    void menu_snap_in_page(DcssWindow &w, int index);
    void menu_update_visible(DcssWindow &w, const View &v);
    bool menu_handle_key(DcssWindow &w, const SDL_Event &event);
    bool menu_handle_char(DcssWindow &w, int ch);
    void menu_click_item(DcssWindow &w, int index, bool left);
    View *view_of(const DcssWindow &w);
    void scroll_by(DcssWindow &w, int delta);

    DcssWindowStack stack_;
    DcssWindowAtlas atlas_;
    bool japanese_{ true };
    RectPx area_{};
    int crt_px_{ 15 };
    bool small_layout_{ false };
    TextOverlay font_;
    int font_px_{ 0 };
    bool font_open_{ false };
    bool font_failed_{ false };
    std::string font_path_;
    DcssTextMeasure measure_;
    int screen_w_{ 0 };
    int screen_h_{ 0 };
    float ui_scale_{ 1.f };
    struct NameHit {
        RectPx rect;
        int key{ 0 };
    };
    std::vector<NameHit> name_hits_; //!< 名前の問いのボタン（Esc・*・Enter）の押せる所
    bool android_extras_{ false };
    std::function<void()> keyboard_handler_;
    bool atlas_tried_{ false };

    DcssStartupData startup_; //!< 開始メニューの材料（アダプタが送る）
    UiImage title_image_; //!< 起動の読み込み画面の絵
    bool title_loaded_{ false };
    bool title_done_{ false }; //!< 読み込み画面を出し終えた（窓が開いた・絵が無い）
    std::map<int, std::unique_ptr<View>> views_; //!< 窓の通し番号（`generation`）で引く、直前に描いた配置
    std::vector<std::string> outgoing_;
    std::function<void(const std::string &)> local_handler_; //!< 画面の側で積んだ窓の操作の受け手（`set_local_handler`）

    int mouse_x_{ -1 };
    int mouse_y_{ -1 };
    std::uint64_t seen_version_{ ~0ull };
};

} // namespace hd2d::dcss
