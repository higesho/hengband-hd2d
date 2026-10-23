/*!
 * @file dcss_stat.h
 * @brief DCSS のステータス欄（原作 `output.cc` の `print_stats` ほかの再現）。
 *
 * ## 何のためのものか
 * 原作のタイル版は、ステータス欄を**文字の格子**として `cgotoxy` と `cprintf` で書く（`output.cc`）。
 * ウェブタイル版はその字を送らず、数値だけを `player` メッセージで送る。ここは `DcssUiModel` の数値から、
 * 原作と同じ並び・色・桁で格子（`DcssTextGrid`）を組む。段 U1 の設計の材料 の U1（案 Y）。
 *
 * ## 原作のどこを写したか（`crawl-ref/source/output.cc`）
 * - 通常の配置: `_redraw_title`・`_print_stats_hp`・`_print_stats_mp`・`_print_stats_ac`・`_print_stats_ev`・`_print_stat`・
 *   `_print_stats_doom`・`_print_stats_contam`・`print_stats` の XL の行・`print_stats_level`・`draw_border`・
 *   `_print_stats_noise`・`update_turn_count`・`_print_stats_wp`・`_print_stats_qv`・`_print_status_lights`・`colour_bar`
 * - 小さい配置（窓幅 1490 ドット未満）: `_cgotoxy_touchui`・`_cprintf_touchui` が座標で行き先を替える仕組みを、
 *   行き先の座標を直接書く形に直した（縦の体力・魔力の棒 `vdraw` を含む）
 * - 日本語の見出しは日本語版の辞書（`dat/database/ja/jtrans/output.txt`）の訳。幅は英語の見出しの幅に詰める（`_hud_caption`）
 *
 * ## 原作と違う所（確かめた事実・推測）
 * - 「時間: x.x (y.y)」の括弧の中（`elapsed_time_at_last_input` との差）は JSON に無いので、指令待ちに入った時刻から求める（推測）。
 * - 種族と職業の略称（小さい配置の 2 行目。例 `HuFi`）は JSON に無いので、コアが別に送る `dcss_ui` の値を使う。無ければ種族の英名の先頭 2 字。
 * - 騒音の「消音」・体力の増強中（`_boosted_hp`）・小さい配置の騒音の細部は未対応（印を出さないだけ）。
 */
#pragma once

#include "dcss/dcss_cells.h"
#include "dcss/dcss_model.h"

#include <string>

namespace hd2d::dcss {

//! 体力・魔力・騒音の棒（原作 `colour_bar`）。直前に描いた長さを覚えて、変わった分を色分けする。
struct DcssColourBar {
    int m_default{ kLightGreen };
    int m_change_pos{ kGreen };
    int m_change_neg{ kRed };
    int m_empty{ kDarkGrey };
    int horiz_bar_width{ -1 };
    int old_disp{ -1 };
    int request_redraw_after{ 0 };

    void reset()
    {
        this->old_disp = -1;
        this->request_redraw_after = 0;
    }
    //! 横の棒（通常の配置）。座標は 1 始まり。
    void draw(DcssTextGrid &grid, int cols, int ox, int oy, int val, int max_val, int sub_val, int turn);
    //! 縦の棒（小さい配置）。`ox` は左端の桁、`oy` は棒の上端の行、下端は `obase` 行。
    void vdraw(DcssTextGrid &grid, int ox, int oy, int obase, int val, int max_val, int turn);
};

/*!
 * @brief ステータス欄の組み立て役。棒の状態を持つ。
 * @details 原作は値が変わった行だけを書き直す（棒は書き直す瞬間の前後で色が変わる）。ここも同じく、
 * 組み直しは「`player` が変わった」または「棒が 4 手番後に戻る時刻が来た」ときだけ呼ぶこと
 * （毎フレーム呼ぶと、変化の色がすぐ消える）。
 */
class DcssStatPanel {
public:
    //! 格子を組み直す。`cols`・`rows` は格子の桁数と行数。`small_layout` は小さい配置。
    void build(const DcssUiModel &model, int cols, int rows, bool small_layout, bool japanese, DcssTextGrid &grid);
    //! 次に組み直すべき手番（棒の戻り）。無ければ 0。
    int redraw_after_turn() const;
    void reset_bars();
    //! 種族と職業の略称（コアが送る。小さい配置の 2 行目）。
    void set_character_abbrev(std::string abbrev) { this->abbrev_ = std::move(abbrev); }

private:
    void build_normal(const DcssUiModel &model, int cols, int rows, bool ja, DcssTextGrid &grid);
    void build_small(const DcssUiModel &model, int cols, int rows, bool ja, DcssTextGrid &grid);

    DcssColourBar hp_bar_{ kLightGreen, kGreen, kRed, kDarkGrey };
    DcssColourBar mp_bar_{ kBlue, kBlue, kLightBlue, kDarkGrey };
    DcssColourBar noise_bar_{ kWhite, kLightGrey, kLightGrey, kDarkGrey };
    std::string abbrev_;
};

//! 状態の灯・武器の名前などの色（`DcssInvItem::colour` が -1 のときの既定は LIGHTGREY）。
int dcss_item_colour(int colour);

} // namespace hd2d::dcss
