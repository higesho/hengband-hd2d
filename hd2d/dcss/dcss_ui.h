/*!
 * @file dcss_ui.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）の画面の部品。握手でコアが `dcss-ui` を名乗ったときだけ使う。
 *
 * ## 何のためのものか
 * 決めたこと D10: 画面は原作のタイル版の UI を再現し、マップの表示だけを立体にする。HD2D の左の状態の欄・サブパネル・玉・
 * 状態のアイコンは出さず、代わりにこの部品が原作と同じ区画（ステータス欄・メッセージ欄・ミニマップ・タブの欄）を描く。
 * 設計の材料は （§1 原作の区画・§5 立体のマップとのつなぎ・§6 部品の形）。
 *
 * ## 段 U1 で入っているもの
 * - 受信（`webtiles` の行 → `DcssUiModel`）・配置（`DcssLayout::compute`。窓の幅 1490 ドット未満は小さい配置）
 * - ステータス欄（`DcssStatPanel`）・メッセージ欄（`DcssMessageWindow`）・ミニマップ・タブの欄の枠と見出しの絵
 * - 字は BIZ UDゴシック（Windows は OS のもの、無ければ同梱の OFL 版）。大きさは窓の大きさで原作の表から引く
 *
 * ## 入っていないもの（次の段）
 * タブの中身（持ち物・呪文など）・窓の部品（メニュー・説明）・ツールチップ・マウス・パッド。
 *
 * ## 共通部分との境
 * `hd2d_app.cpp` が申告 `dcss-ui` の真偽 1 つで、(1) 受信の行をここへ渡す、(2) 配置の結果（3D の矩形・下の帯）を差し替える、
 * (3) HD2D の部品の代わりにここを描く、の 3 つだけを呼ぶ。ほかのコアはここを 1 行も通らない。
 */
#pragma once

#include "dcss/dcss_cells.h"
#include "dcss/dcss_layout.h"
#include "dcss/dcss_clouds.h"
#include "dcss/dcss_fx.h"
#include "dcss/dcss_marks.h"
#include "dcss/dcss_message.h"
#include "dcss/dcss_mouse.h"
#include "dcss/dcss_model.h"
#include "dcss/dcss_pad.h"
#include "dcss/dcss_pad_screen.h"
#include "dcss/dcss_stat.h"
#include "dcss/dcss_tabs.h"
#include "dcss/dcss_touch.h"
#include "dcss/dcss_windows.h"
#include "render/text_overlay.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"

#include <memory>
#include <string>
#include <vector>

namespace hd2d::dcss {

class DcssUi {
public:
    DcssUi();
    ~DcssUi();
    DcssUi(const DcssUi &) = delete;
    DcssUi &operator=(const DcssUi &) = delete;

    //! 表示の言語（`ja` / `en`）。ステータス欄の見出しと名前の訳に効く。
    void set_language(const std::string &lang);

    /*!
     * @brief Android・Quest の配置にする（原作の Android 版と同じ字の大きさの表と窓の拡大。`dcss_android_profile`）。
     * @param display_w,display_h 端末の画面（窓）の実寸。短い辺から拡大の倍率 `game_scale`（= 短い辺 / 1080 + 1）と字の大きさの行を決める。
     * @details 呼ばなければ従来どおり（Windows・Linux。倍率 1・小さい配置は窓の幅 1490 ドット未満のとき）。呼ぶと、配置は
     * 倍率で割った論理の窓で計算し、描くときに座標系ごと倍率倍に拡大する（`DcssUi::draw` ほか。ほかのコアは通らない）。
     * 原作は Android の実機では字が大きく窓が狭い（論理の幅は 1280 以下）ので、いつも小さい配置になる。
     */
    void use_android_profile(int display_w, int display_h);
    //! 窓の拡大の倍率（Windows・Linux は 1）。
    int ui_scale() const { return this->ui_scale_; }

    /*!
     * @brief コアから届いた 1 通（`{"t":"webtiles","line":"…"}` か `{"t":"dcss_ui",…}`）を取り込む。
     */
    void ingest_payload(const std::string &payload);

    /*!
     * @brief 窓の大きさから配置を決める（毎フレーム呼んでよい。変わらなければ何もしない）。
     * @param origin_x,origin_y 使える範囲の左上（窓の座標）
     * @param usable_w,usable_h 使える範囲の大きさ
     * @param reserved_bottom 下の案内の帯の高さ。配置は残りの高さで計算する（`DcssLayout::compute` の窓の高さ）
     * @details 字の大きさが変わるときは字を開き直す（GL が要る）。失敗したら `err` に理由を入れて偽。
     */
    bool prepare(int origin_x, int origin_y, int usable_w, int usable_h, int reserved_bottom, std::string &err);
    bool layout_ok() const { return this->layout_.ok; }
    //! 配置を組み直すたびに増える（記録を出すかの判定に使う）。
    std::uint64_t layout_serial() const { return this->layout_serial_; }
    const DcssLayout &layout() const { return this->layout_; }

    //! 立体の地図を描く矩形（窓の座標）。配置が成立していなければ空。
    RectPx scene_rect() const;
    //! 小さい配置か。
    bool small_layout() const { return this->layout_.small_layout; }
    //! 窓の大きさ（使える範囲）。
    int usable_w() const { return this->usable_w_; }
    int usable_h() const { return this->usable_h_; }

    //! ゲームに入っているか（`player` を受けた）。入っていない間は区画を描かない。
    bool in_game() const { return this->model_.player().seen; }

    /*!
     * @brief 区画を描く。`paint` は下敷き・四角、字は自分の字で積む（`flush_text` で流す）。
     * @param images タブの見出しの絵を貼るのに使う
     * @param screen_w,screen_h 窓の実寸
     */
    void draw(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h);
    //! 自分の字を流す（`paint.flush()` の後に呼ぶ）。
    void flush_text();
    void shutdown();

    const DcssUiModel &model() const { return this->model_; }

    //! 窓の部品（メニュー・説明・作成の格子ほか。段 U3。`dcss_windows.h`）。窓か文字入力があるとキーを窓が受ける。
    bool windows_capture_keys() const { return this->windows_.captures_keys(); }
    //! SDL の出来事 1 つを窓の部品へ見せる。窓が食ったら真。`reserved_key` は HD2D が自分で使うキー（機能メニュー・Alt+Enter）。
    bool on_window_event(const SDL_Event &event, bool reserved_key);
    //! 窓の部品がコアへ送る行（原作の制御メッセージ）を持っていく。
    std::vector<std::string> take_window_outgoing() { return this->windows_.take_outgoing(); }
    //! 窓を描く（ゲームに入る前も含む）。下の層の四角・字・絵を流したあとで呼ぶ。
    void draw_windows(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h);
    const DcssWindows &windows() const { return this->windows_; }

    /*!
     * ゲームパッド（段 5。`dcss_pad.h`・`dcss_pad_screen.h`）。割り当て・疑似カーソル・下の操作の案内の中身・割り当ての画面。
     * `scene()` はいまのパッドの押しの行き先（コマンド待ち・窓・狙い・その他）。
     */
    DcssPad &pad() { return this->pad_; }
    const DcssPad &pad() const { return this->pad_; }
    DcssPadScreen &pad_screen() { return this->pad_screen_; }
    DcssPadScene scene() const { return DcssPad::scene_for(this->model_, this->windows_.captures_keys()); }
    //! 疑似カーソルを描く（`draw_windows` の最後に呼ばれる。右スティックが最後に動かした方のときだけ）。
    void draw_pad_cursor(UiPaint &paint);

    /*!
     * 地図のマウス操作（段 U3・U4。左右のクリック・ツールチップ。`dcss_mouse.h`）。窓が開いている間・ゲームに入る前は何もしない。
     * `on_map` はマウスが地図の見えているマスの上か、`dx`・`dy` はプレイヤーのマスから見た相対。食ったら真（地図のクリック）。
     */
    bool on_map_event(const SDL_Event &event, bool on_map, int dx, int dy);
    //! 地図のマウス操作がコアへ送る行（原作の制御メッセージ）を持っていく。
    std::vector<std::string> take_map_outgoing() { return this->map_mouse_.take_outgoing(); }
    const DcssMapMouse &map_mouse() const { return this->map_mouse_; }

    /*!
     * 地図の上の印（段 3。態度・強さの足元の輪と立体の雲。`dcss_marks.h`・`dcss_clouds.h`）。状態のアイコンと悪魔の等級の数字はコアが怪物の板に焼き込む。
     * `ring_tints()` は `EntityTracker::build` に渡す半透明の色の輪、`append_cloud_voxels()` は雲のボクセルを攻撃の視覚効果のボクセルへ足す。
     */
    const std::vector<RingTint> &ring_tints() const { return this->marks_.ring_tints(); }
    std::size_t append_cloud_voxels(std::vector<FxVoxel> &out, float seconds) const
    {
        return this->clouds_.append_voxels(out, this->marks_.clouds(), seconds);
    }
    const DcssMarks &marks() const { return this->marks_; }
    const DcssClouds &clouds() const { return this->clouds_; }
    /*!
     * 攻撃の演出（段 4。ビームの弾・矢・投げた物・爆発・閃光。`dcss_fx.h`）。原作のタイルの絵をボクセルにして攻撃の視覚効果のボクセルと点光源へ足す。
     * `right`・`up` は画面の右と上（カメラ）。足したボクセルの数を返す。
     */
    std::size_t append_fx(FxFrame &out, const Vec3 &right, const Vec3 &up)
    {
        DcssFxView view;
        view.right = right;
        view.up = up;
        return this->fx_.append(out, DcssFx::seconds_now(), view);
    }
    DcssFx &fx() { return this->fx_; }
    const DcssFx &fx() const { return this->fx_; }

    /*!
     * タブの欄（持ち物・呪文ほか。段 U2）へのマウス。区画の中の押下は消費したとして真。コアへ送る要求は `take_tab_outgoing` で持っていく。
     * タブが食わなかった押しは、ステータス欄（原作 `StatRegion::handle_mouse`）とメッセージ欄（`MessageRegion::handle_mouse`）の左クリックとして見る。
     */
    bool on_tab_mouse(const DcssTabMouse &ev);
    std::vector<DcssTabOutgoing> take_tab_outgoing();
    const DcssTabs &tabs() const { return this->tabs_; }

    /*!
     * タッチ（段 7。`dcss_touch.h`・「段 7 のタッチ」）。指の出来事（`SDL_FINGER*`）を原作の Android 版と同じ決まりで
     * マウスの出来事（`which` = `kTouchMouseId`）に直して `out` へ追記する。呼ぶ側が `SDL_PushEvent` で積み直し、窓・タブの欄・地図の既存のマウスの経路へ流す。
     * @param ignore_down 真のとき、この指の押下はなかったことにする（仮想パッドのボタンの近くを触った指）
     */
    void on_finger(const SDL_Event &event, int screen_w, int screen_h, std::uint32_t now, bool ignore_down, std::vector<SDL_Event> &out)
    {
        this->touch_.on_finger(event, screen_w, screen_h, now, ignore_down, out);
    }
    //! 戻るキー（Android の `SDLK_AC_BACK`）は Esc（原作 `CK_ESCAPE`）。窓の部品の送り先へ送る。出来事が戻るキーでなければ何もせず偽。
    bool on_back_key(const SDL_Event &event);
    //! ソフトキーボードを出したいか（名前の問いが出ている間。コアの 1 行入力ではないので、フレームの `text_input_active` には載らない）。
    bool wants_soft_keyboard() const { return this->windows_.name_prompt(); }
    //! 画面のキーボードの出し入れの合図（システム命令の欄・ゲームメニューの項目）が届いていれば真にして消す。
    bool take_keyboard_toggle();
    //! 小さい配置のタブの見出しの絵を出しているか（Android は既定で偽。ステータス欄を押すと切り替わる）。
    bool tab_icons_shown() const { return this->tab_icons_shown_; }

    //! 検査用: 最後に組んだ格子の字（行ごと。ステータス欄・メッセージ欄）。
    std::vector<std::string> stat_lines() const;
    std::vector<std::string> message_lines() const;
    //! 検査用: 配置と字の大きさの 1 行の説明。
    std::string describe() const;

private:
    struct FontSlot {
        TextOverlay text;
        int px{ 0 };
        bool open{ false };
    };

    bool open_font(FontSlot &slot, int px, std::string &err);
    void rebuild_grids();
    void draw_grid(FontSlot &font, UiPaint &paint, int x, int y, const DcssTextGrid &grid, int char_w, int char_h);
    void draw_minimap(UiPaint &paint);
    void draw_text_input_echo(UiPaint &paint, int screen_w, int screen_h);
    int glyph_y_shift() const;
    //! SDL の出来事の座標（窓の座標）を論理の座標に直した写しを返す。倍率 1 なら同じ物。
    SDL_Event to_logical(const SDL_Event &event, float scale) const;
    //! 窓の部品の層を描く倍率（画素 / 層の論理の単位）。捲れない窓が収まらないときは、`ui_scale_` より小さくして層を大きく取る（`fit_k_`）。
    float window_scale() const { return this->ui_scale_ / (this->windows_.has_windows() ? this->fit_k_ : 1.f); }
    void update_window_fit();
    void draw_windows_body(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h);

    DcssUiModel model_;
    DcssStatPanel stat_panel_;
    DcssMessageWindow msgwin_;
    DcssTextGrid stat_grid_;
    DcssTextGrid msg_grid_;
    DcssLayout layout_;
    DcssLayoutInput last_input_;
    bool have_layout_input_{ false };
    std::uint64_t layout_serial_{ 0 };
    bool japanese_{ true };

    int origin_x_{ 0 };
    int origin_y_{ 0 };
    int usable_w_{ 0 };
    int usable_h_{ 0 };
    int reserved_bottom_{ 0 };
    //! 窓の拡大（Android・Quest）。`origin_*`・`usable_*`・`reserved_bottom_`・`layout_` は **倍率で割った論理の座標**で持つ。窓の座標が欲しいときは `ui_scale_` を掛ける。
    bool android_profile_{ false };
    int display_w_{ 0 };
    int display_h_{ 0 };
    int ui_scale_{ 1 };
    float fit_k_{ 1.f }; //!< 窓の部品の層を、論理の窓の何倍の大きさで組むか（1 = そのまま。案内の帯の上に収まらない窓を縮めて描くため）
    DcssAndroidProfile android_{};

    FontSlot stat_font_;
    FontSlot msg_font_;
    FontSlot lbl_font_; //!< タブの欄の説明の 1 行（原作 `tile_font_lbl`）
    DcssWindows windows_; //!< 窓の部品（段 U3）
    DcssPad pad_; //!< ゲームパッド（段 5）
    DcssPadScreen pad_screen_{ pad_, windows_ }; //!< 割り当ての画面（窓の部品へ積む）
    DcssMapMouse map_mouse_; //!< 地図のマウス操作とツールチップ（段 U3・U4）
    FontSlot tip_font_; //!< ツールチップの字（原作 `tile_font_tip`）
    DcssTabs tabs_;
    DcssTouch touch_; //!< 指 → マウスの状態機械（段 7）
    bool tab_icons_shown_{ true }; //!< 小さい配置のタブの見出しの絵（Android は `use_android_profile` で偽にする）
    bool keyboard_toggle_{ false };
    std::vector<DcssTabOutgoing> region_out_; //!< ステータス欄・メッセージ欄の左クリックがコアへ送る要求
    DcssMarks marks_; //!< 地図の上の印（段 3）
    DcssClouds clouds_; //!< 立体の雲の模様（段 3）
    DcssFx fx_; //!< 攻撃の演出の絵（段 4）
    bool atlas_tried_{ false };
    std::string last_tabs_described_;
    DcssScreenProfile profile_;
    std::string font_path_;
    int font_index_{ 0 };

    std::uint64_t player_version_seen_{ ~0ull };
    std::uint64_t msg_version_seen_{ ~0ull };
    bool stat_dirty_{ true };
    bool small_seen_{ false };

};

} // namespace hd2d::dcss
