/*!
 * @file dcss_mouse.h
 * @brief DCSS の地図のマウス操作（左右のクリック・マスに載せたときのツールチップとマスの説明）。段 U3・U4。
 *
 * ## 原作の動き（`tilereg-dgn.cc` の `DungeonRegion::handle_mouse`）
 * 命令を待っているとき（`input_mode` = 1）:
 * - 足元の左クリック … アイテムがあれば拾う・階段なら上り下り・どれでもなければ 1 ターン待つ。右クリック … キャラクターの概要。
 *   これは `hd2d_click_self`（コアの `dcss_ui_mouse.h` が処理する）で送る
 * - ほかのマスの左クリック … 旅・移動・怪物へ近づく（攻撃）。右クリック … そのマスの説明（原作の窓）。
 *   ウェブタイル版が持つ `click_cell`（`{"msg":"click_cell","x":…,"y":…,"button":1|3}`）をそのまま送る
 * 狙い・地図の画面（`input_mode` = 2〜4）の左クリック … 決定（`click_cell`）。動き … 狙いのカーソル（`target_cursor`）。
 * `-more-`・問い・はい/いいえ（`input_mode` = 5〜8）では地図のクリックに反応しない。
 *
 * ## ツールチップ（原作 `tile_tooltip_ms` = 500 ミリ秒・`FTFontWrapper::render_tooltip`）
 * マウスを同じマスに 500 ミリ秒置くと、そのマスの左右クリックの説明を、マウスの (-15, +20) に枠つきで出す
 * （外枠 (125,98,60)・内側 (4,2,4)・外の余白 7・窓の端から 5 離す）。本文はコアが組む（`hd2d_hover` の返事）。
 * マスの説明の 1 行（地形・雲の名前）は、マウスを載せるとすぐ、そのマスの上に白い字で出す。
 *
 * 描画にも通信にも触れない部分（`on_event`・`ingest_reply`）と、描く部分（`draw`）を 1 つの型に置く。
 */
#pragma once

#include "render/text_overlay.h"
#include "ui/ui_paint.h"

#include <cstdint>
#include <string>
#include <vector>

union SDL_Event;

namespace hd2d::dcss {

class DcssMapMouse {
public:
    /*!
     * @brief SDL の出来事を 1 つ見る（動き・押下）。
     * @param on_map マウスの位置が地図の矩形の中の見えているマスか
     * @param dx,dy プレイヤーのマスから見たマスの相対（`on_map` が真のとき）
     * @param player_x,player_y プレイヤーのマス（コアの座標。`player.pos`）
     * @param input_mode 原作の `input_mode`（0〜8）
     * @param mods 押している修飾キー（1 = Shift・2 = Ctrl）
     * @return 食ったか（地図のクリックは食う。動きは食わない）
     */
    bool on_event(const SDL_Event &event, bool on_map, int dx, int dy, int player_x, int player_y, int input_mode, int mods);

    //! コアからの返事（`{"t":"dcss_ui","k":"hover",…}`）を取り込む。
    void ingest_reply(const std::string &payload);

    //! コアへ送る行（原作の制御メッセージ。届いた順）を持っていく。
    std::vector<std::string> take_outgoing();

    //! マウスが地図を離れた・窓が開いた・入力が変わったときに表示を消す。
    void clear();

    /*!
     * @brief ツールチップとマスの説明の 1 行を描く。
     * @param tip_font ツールチップの字（原作 `tile_font_tip`）
     * @param tag_font マスの説明の字（原作 `tile_font_lbl`）
     * @param area ツールチップを収める矩形（窓全体。端から 5 画素内側）
     */
    void draw(UiPaint &paint, TextOverlay &tip_font, TextOverlay &tag_font, const RectPx &area, int screen_w, int screen_h, int ui_scale = 1);

    //! ツールチップを出すか。Android・Quest は偽（原作 `initfile.cc`: Android は `tile_tooltip_ms` = 0）。マスの説明の 1 行は残る。
    void set_tooltip_enabled(bool on) { this->tooltip_enabled_ = on; }

    bool tooltip_showing() const { return this->tip_shown_; }
    const std::string &tooltip_text() const { return this->tip_text_; }
    const std::string &tag_text() const { return this->tag_text_; }
    //! 検査用: 最後に送った問い合わせの数。
    int queries_sent() const { return this->queries_sent_; }

private:
    void request_hover(int dx, int dy);

    std::vector<std::string> outgoing_;
    int mouse_x_{ -1 };
    int mouse_y_{ -1 };
    bool on_map_{ false };
    int cell_dx_{ 0 };
    int cell_dy_{ 0 };
    bool cell_valid_{ false };
    std::uint32_t moved_at_{ 0 }; //!< 最後にマウスが動いた（SDL_GetTicks）
    std::string tip_text_;
    std::string tag_text_;
    bool tip_have_{ false }; //!< 今のマスの本文が届いている
    bool tip_shown_{ false };
    bool tooltip_enabled_{ true };
    int queries_sent_{ 0 };
    int last_target_x_{ INT32_MIN };
    int last_target_y_{ INT32_MIN };
};

} // namespace hd2d::dcss
