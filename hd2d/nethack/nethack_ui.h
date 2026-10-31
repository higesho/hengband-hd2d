/*!
 * @file nethack_ui.h
 * @brief NetHack の専用の画面の部品。握手でコアが `nethack-ui` を名乗ったときだけ使う。
 *
 * ## 何のためのものか
 * （D10〜D17）。画面は原作の Windows の GUI 版（NetHackW）の区画を再現し、地図の区画だけを立体にする。
 * HD2D の左の状態の欄・サブパネル・玉・ミニマップ・新しい画面の窓・コマンドメニューは出さず、代わりにこの部品が
 * 上のメッセージの窓・下のステータスの窓を描く。下の操作の案内の帯と機能メニューは HD2D のまま（D13）。
 * 窓の枠と地の色は HD2D の作り（暗い下敷きと細い枠。D14）。
 *
 * ## 段 N1 で入っているもの
 * - 受信（`nethack_ui` → `NhModel`）・配置（`nh_compute_layout`）。ゲームに入る前（キャラクタ作成まで）は従来の配置のまま。
 * - メッセージの窓（今のターンの行と前の行で色を変える・属性・`--More--`・見える行数と桁数をコアへ知らせる）。
 * - ステータスの窓（2 行・項目の色と属性・題の欄の体力バー）。
 * - 地図の区画 = `scene_rect()`（立体の地図が入る）。
 * - `--More--` の応答（スペース・Enter・Esc を `keys` で送る）。
 *
 * ## 段 N2・N3 で入っているもの（窓の部品。`nh_windows.h`）
 * - メニュー・テキスト・問い（yn）・文字入力（名前・`getlin`・拡張コマンド）・キャラクタ作成・墓標・持ち物の常設の窓。
 * - メニューの行の頭に原作のタイルの絵（コアが `pref nethack_tiles` で知らせたフォルダの `tiles.png`・`tiles.json`）。
 * - 窓が開いている間のキーは窓が受け、コアへは返事（`nethack_reply`）だけを送る。
 *
 * ## 段 N4 で入っているもの（マウス。`nh_mouse.h`）
 * - 地図のクリック: コアが命令か位置の選択を待っている間の左クリック・右クリックを、座標と `mod`（1 左・2 右）にして `pos` で返す
 *   （歩く・走る・調べるはコアが決める。原作の `nh_poskey`）。
 * - 位置の選択（`;`・`_`・魔法の対象）: コアの `curs` に従う地図のカーソルの印を、立体の地図の上に重ねる（`map_cursor_cell`）。
 * - マウスを載せたマスの説明: コアに聞いて（`nethack_query` → `hover`）マウスの横に小さく出す。
 *
 * ## 入っていないもの（次の段）
 * パッド・Android のタッチ（N5）。
 */
#pragma once

#include "app/native_core_ui.h"
#include "nethack/nh_layout.h"
#include "nethack/nh_model.h"
#include "nethack/nh_mouse.h"
#include "nethack/nh_windows.h"
#include "render/text_overlay.h"

#include <string>
#include <vector>

namespace hd2d::nethack {

class NethackUi final : public NativeCoreUi {
public:
    NethackUi();
    ~NethackUi() override;

    const char *feature_name() const override { return "nethack-ui"; }
    void set_language(const std::string &lang) override;
    void ingest_payload(const std::string &payload) override;
    bool prepare(int origin_x, int origin_y, int usable_w, int usable_h, int reserved_bottom, std::string &err) override;
    bool layout_ok() const override { return this->layout_.ok; }
    std::uint64_t layout_serial() const override { return this->layout_serial_; }
    bool in_game() const override { return this->model_.map_created() && !this->model_.exited(); }
    RectPx scene_rect() const override;
    std::string describe() const override;
    void draw(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h) override;
    void flush_text() override;
    void draw_windows(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h) override;
    bool windows_capture_keys() const override { return this->model_.more_pending() || this->windows_.active(); }
    bool on_window_event(const SDL_Event &event, bool reserved_key) override;
    bool on_pad(NativePad pad) override;
    bool on_map_event(const SDL_Event &event, bool on_map, int cell_x, int cell_y) override;
    bool map_cursor_cell(int &cell_x, int &cell_y) const override;
    void draw_map_cursor(UiPaint &paint, const RectPx &cell) override;
    std::vector<NativeUiOutgoing> take_outgoing() override;
    std::string key_hint(bool ja) const override;
    //! 画面のキーボード（Android）。文字を打つ窓だけ。はい／いいえの窓の答えはコマンドの欄にボタンで並べる（遊ぶ人「バーチャルキーボードは無し」）
    bool wants_soft_keyboard() const override { return this->windows_.wants_text_input(); }
    void shutdown() override;

    const NhModel &model() const { return this->model_; }
    const NhLayout &layout() const { return this->layout_; }
    const NhWindows &windows() const { return this->windows_; }
    //! 検査用: いま見えているメッセージの窓の行（折り返し済み。`--More--` を含む）。
    std::vector<std::string> message_lines() const;
    //! 検査用: いま見えているステータスの窓の行。
    std::vector<std::string> status_lines() const;

private:
    bool open_font(int px, std::string &err);
    //! 原作の色の番号 → 字の色（暗い下地で読める値）。
    static TextColor text_color_of(int nhcolor, float alpha = 1.f);
    static PaintColor paint_color_of(int nhcolor, float alpha = 1.f);
    //! 属性つきで 1 つの run を描く。戻り値は進んだ幅（画素）。
    int draw_run(int x, int y, const std::string &text, const TextColor &color, int attr, int max_width);
    void draw_message_window(UiPaint &paint);
    void draw_status_window(UiPaint &paint);
    //! `msgwin`（見える行数と桁数）をコアへ送る要求を積む（変わったときだけ）。
    void report_message_window_size();
    void send_keys(int key);
    NhMapMouseContext mouse_context() const;
    //! マウスを載せたマスの説明の吹き出し。
    void draw_hover_tooltip(UiPaint &paint, int screen_w, int screen_h);
    /*
     * コマンドの欄（`cmdbar`）。タッチだけの端末でもコマンドを打てるよう、コアが送った一覧を絵のボタンで並べる。
     * 左クリック（タップ）でコマンドを送る（コマンドを待っている間だけ）。右クリック（長押し）・マウスを載せると名前の吹き出しを出す。
     */
    void draw_cmdbar(UiPaint &paint);
    void draw_cmdbar_tip(UiPaint &paint);
    //! はい／いいえの窓の答えのボタン（コマンドの欄の場所に重ねる）。
    void draw_yn_buttons(UiPaint &paint);
    bool yn_mode() const { return (this->windows_.kind() == NhWindows::Kind::Yn) && !this->layout_.cmdbar.empty(); }
    bool on_cmdbar_event(const SDL_Event &event);
    int cmdbar_index_at(int x, int y) const;
    RectPx cmdbar_cell_rect(int index) const;
    void send_cmdbar(int index);
    //! 窓が作った返事を `out_` へ積み、要求を閉じる。答えた問いはメッセージの窓へ残す。
    void collect_window_outcome();
    void sync_windows();

    NhModel model_;
    NhWindows windows_;
    NhLayout layout_;
    NhLayoutInput last_input_;
    NhMapMouse map_mouse_;
    std::uint64_t seen_hover_serial_{ 0 };
    bool have_layout_input_{ false };
    std::uint64_t layout_serial_{ 0 };
    bool japanese_{ true };

    int origin_x_{ 0 };
    int origin_y_{ 0 };
    int usable_w_{ 0 };
    int usable_h_{ 0 };
    int reserved_bottom_{ 0 };
    int line_h_{ 18 };
    int cell_w_{ 8 };
    int pad_x_{ 6 };
    int msg_cols_{ 0 };

    TextOverlay text_;
    int font_px_{ 0 };
    bool font_open_{ false };
    std::string font_path_;

    int sent_rows_{ -1 };
    int sent_cols_{ -1 };
    bool swallow_space_{ false };
    int cmdbar_cell_{ 0 }; //!< コマンドの欄のボタン 1 つの大きさ（画素）
    int cmdbar_hover_{ -1 }; //!< マウスを載せているボタン（-1 = 無し）
    int cmdbar_tip_{ -1 }; //!< 名前の吹き出しを出しているボタン（押した・長押しした物）
    std::uint32_t cmdbar_tip_until_{ 0 };
    std::vector<NativeUiOutgoing> out_;
    SDL_Rect last_ime_rect_{ 0, 0, 0, 0 };
};

} // namespace hd2d::nethack
