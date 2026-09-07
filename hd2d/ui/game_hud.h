/*!
 * @file game_hud.h
 * @brief フレームの中身を画面へ置く（P8）— 状態列・サブパネル・最下段・
 *   メッセージ・ミニマップ・Term の写し。
 *
 * 基準は P8。**`HengbandUi.exe` と同じことができる**のが完了条件で、
 * 同じ**実装**にすることではない（設計書 必守制約 6「`ui/` は参照はしてよいが移植しない」）。
 *
 * ## 置き場所は `UiLayout` が持つ
 * ここは「渡された矩形に、渡されたフレームの中身を書く」だけにしてある。
 * 3 つの作り（`Full` / `Hybrid` / `Split`）で分岐するのは**下敷きの濃さ**と
 * **矩形が空かどうか**だけで、書き方は 1 通りしか無い。作りごとに別の描画経路を持つと
 * 必ず片方だけ直された状態になる（P6 の「2 本のシェーダ」と同じ話。§14-13）。
 *
 * ## 文字グリッドで来るもの
 * `status_col_lines` / `sub_panels[].lines` / `menu_term_lines` / `bottom_row_runs` は
 * **コアの Term の写し**である（桁に意味がある）。等幅で、桁をマスで置くこと。
 * 比例フォントで詰めると右端揃えの速度・階層がずれる。
 */
#pragma once

#include "frame/game_frame.h"
#include "ui/ui_cursor.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"

#include <string>
#include <vector>

namespace hd2d {

class TextOverlay;
class UiPaint;

/*!
 * @brief 背面の 1 枚絵（無くても遊べる）。
 * @details **絵があるときは下敷きを薄くする。**濃いままだと絵が見えず、
 * 「読み込めているのに出ていない」と誤診する。
 */
struct UiBackdrops {
    const UiImagePainter *painter{ nullptr };
    const UiImage *title{ nullptr };
    const UiImage *panel_bottom{ nullptr };
    const UiImage *panel_right{ nullptr };

    bool has_panels() const
    {
        return (this->painter != nullptr)
            && (((this->panel_bottom != nullptr) && this->panel_bottom->valid())
                || ((this->panel_right != nullptr) && this->panel_right->valid()));
    }
    bool has_title() const
    {
        return (this->painter != nullptr) && (this->title != nullptr) && this->title->valid();
    }
};

/*!
 * @brief UI 側だけが持つ状態（フレームには載らない）。
 * @details カーソルの位置は「いまどれを選んでいるか」という UI の状態であって
 * ゲームの状態ではない。コアは非破壊なので、ここで持つしかない。
 */
struct HudState {
    //! 重なる作り（`Full` / `Hybrid`）でサブパネルを開いているか。
    bool subs_open{ false };
    //! 選択肢・はい／いいえ・数値入力のカーソル。
    UiCursors cursors;
    /*!
     * @brief UI が自分で食うキーの案内（`F10:機能メニュー` など）。
     * @details **コアの `controller_hint` とは別に出す。**あちらはゲームの操作の案内で、
     * こちらは「この exe が横取りしているキー」である。出しておかないと、
     * 機能メニューがあることに誰も気づけない。
     */
    std::string ui_key_hint;
    /*!
     * @brief コントローラーの**全ボタンの割り当て**（`build_pad_bind_items`）。
     *
     * @details 2026-08-11 に決めた「旧 HD2D 版にあった画面下部の操作割り当て表示を
     * 実装して。すべてのコントローラーキーの割り当てを表示」。旧 2D UI の K-18 と
     * 同じ考え方で、**項目のまま**渡して折り返しは描画側でやる
     * （字送りを知っているのは `TextOverlay` だけなので、桁数で切ると必ず溢れるか余る）。
     */
    std::vector<std::string> controller_binds;
    /*!
     * @brief 一人称のとき、いま向いている方位（ラジアン。0 = 北・時計回り）。
     * @details 2026-08-11 に決めた「FPS モードの時のみ、ミニマップのキャラクターを
     * 輝点ではなく三角にし、向いている方向に三角のさきを向くように」。
     * **`first_person_facing` を見るのは `first_person` が真のときだけ。**
     * 見下ろしのときは向きという概念が無い（カメラは必ず北を向いている）ので、
     * 従来どおりの輝点で描く。
     */
    bool first_person{ false };
    float first_person_facing{ 0.f };
    /*!
     * @brief 見下ろしの視点回転の段。
     *
     * @details **ミニマップそのものは回さない**（地図の記憶は世界座標であり、回すと読めなくなる。
     * 定石どおり北上固定）。代わりに 0 以外のとき隅へ**北の印**を出して、
     * 3D の絵の中で北がどちらなのかを知らせる。
     * @note 0 のときは何も描かないので、回転を入れる前と 1 画素も変わらない。
     */
    int camera_turn{ 0 };
    /*!
     * @name ミニマップの見せ方（2026-08-19 に決めた）。`Hd2dSettings` の写し
     * @details 描く側は `Hd2dSettings` を持っていないので、毎フレームここへ入れる
     * （`first_person_facing` と同じ流儀）。**既定値は従来の見え方そのもの。**
     * @{
     */
    int minimap_cell_px{ 6 };
    int minimap_opacity_pct{ 100 };
    //! 真なら**地図ごと `camera_turn` 段回す**（画面の奥とミニマップの上を揃える）。
    bool minimap_relative{ false };
    MinimapCorner minimap_corner{ MinimapCorner::TopRight };
    /*! @} */
    /*!
     * @brief 掴んでいる仕切り（`kSubGripBottom`〜／`kSubGripRight`〜。-1 = 掴んでいない）。
     * @details 印を描くのは `UiLayout::grip` の矩形そのもの。**当たり判定と同じ矩形から描く**
     * ので、掴めるように見えて掴めない所ができない（必守制約 3 の親戚）。
     */
    int grabbed_grip{ -1 };
};

/*!
 * @brief 仕切りのうち、`x, y` が掴めるものの添字（掴めなければ -1）。
 * @details 位置を持っているのはレイアウトなので、判定もここに置く。
 * 呼び出し側が別に矩形を組み直すと、描いた印とずれる。
 */
int grip_at(const UiLayout &layout, int x, int y);

/*!
 * @brief 掴んだ仕切りを `x, y` へ動かす。
 * @param[in,out] split 枚数と仕切り。**動かすのは仕切りだけ**（枚数は触らない）。
 * @return 値が変わったら true。
 * @details 下段の仕切りは `x`、右列の仕切りは `y` を見る。**どちらを見るかは番号で決まる**
 * ので、呼ぶ側は両方渡してここに任せる（呼ぶ側で分けると必ず片方を書き忘れる）。
 */
bool drag_grip(const UiLayout &layout, int grip, int x, int y, SubSplit &split);

/*!
 * @brief 1 フレームぶんの UI を積む。
 * @param paint 下敷き・枠・ゲージ。**呼び出し側が `text` より先に流すこと。**
 * @param text 文字。
 * @details Term の写し（タイトル・店・メニュー）が出ているときは、地図まわりの
 * パネルを描かない。**その画面には地図が無い**ので、古い中身が残って見えると嘘になる。
 */
void draw_game_ui(UiPaint &paint, TextOverlay &text, const UiLayout &layout,
    const GameFrame &frame, const HudState &state, const UiBackdrops *backdrops = nullptr);

/*!
 * @brief 画面に地図（3D）が出ているか。
 * @details 偽なら Term の写しが全面を占めている。カメラも `ui_state` も、この間は
 * 意味を持たない（`hd2d_app.cpp` が 3D のパスを丸ごと飛ばす判断にも使う）。
 */
bool frame_shows_map(const GameFrame &frame);

/*!
 * @brief **メインパネルへ 2D のアスキー地図を描く**。
 *
 * @param panel 描く矩形（`UiLayout::scene`）。空なら何もしない。
 * @return 実際に字を置いた矩形（空なら描いていない）。
 *
 * @details 2026-08-19 に決めた（・改訂）「あくまでもメインパネルに 2D でアスキー表示するのみで」。
 * 初版の設計（Term 80×24 を丸写しして画面全部を置き換える）は
 * **コントローラー操作などの UI 側の追加要素が動かない**ため取り下げられた。
 * ここが描くのは**メインパネルの中身だけ**で、状態列・サブパネル・最下段・ミニマップ・
 * カーソル層・バーチャルパッドは何も変わらない。
 *
 * ## 中心はコアの窓の中心（`cam_x/cam_y`）
 * プロトコルの可視窓は注視点を中心にした対称な矩形で、`cam` は**窓の左上ではなく中心**である
 * （`presentation_bridge.cpp` の `ox = px - view_w/2`）。ここを左上と取り違えると
 * 自機が隅に出て地図の半分が枠の外へ行く（`draw_frame_as_text` が P0 で踏んだ穴）。
 *
 * @note **未踏破のマスは描かない**（`CELL_FEAT_KNOWN`）。暗いのではなく「無い」ので、
 * 下敷きの黒がそのまま出る。
 */
RectPx draw_ascii_map_panel(UiPaint &paint, TextOverlay &text, const RectPx &panel, const GameFrame &frame);

/*!
 * @brief Term の写しの上で画面座標 `(x, y)` に当たる選択肢の添字（無ければ -1）。
 * @details クリック／タップで選択肢を直接選ぶための逆引き（P5 その2・2026-08-19。
 * 「セーブデータ選択が文字指定でしか選択できない」に気づいた）。当たりの矩形は
 * 描画（`draw_term_mirror` の枠）と同じ計算なので、見えている枠とずれない。
 * `menu_choices` が立っている画面すべてで効く（幻想蛮怒のセーブ選択・変愚の店や建物）。
 */
int menu_choice_at(TextOverlay &text, const UiLayout &layout, const GameFrame &frame, int x, int y);

/*!
 * @brief 直近のメッセージを**折り返して**下詰めで積む（新しいものが下）。
 * @param max_width 1 行に使える画素（0 以下なら折らない）。
 * @param rows 使える行数。溢れたら**古いほうから**落とす。
 * @details メッセージの帯（`Full` / `Hybrid`）とサブパネルの Sub1（`Split` / `Tall`）で
 * 同じものを出すので 1 か所に置いてある。**入らない字を捨てない**のが要点で、
 * 捨てていた頃は長いメッセージが途中で切れて読めなかった（2026-08-21 に気づいた
 * 「メッセージがパネル全体に表示せず半分くらいで切れる」）。
 * @note 公開しているのは `--ui-check` から突くためである（描くのは `game_hud.cpp` だけ）。
 */
std::vector<SubPanelLine> recent_message_lines(TextOverlay &text, const std::vector<MessageEvent> &messages,
    int max_width, int rows);

/*!
 * @brief 下の帯にコアの行そのものを出すか。
 * @details 出るのは 2 種類ある。`[y/n]` のような**選ぶ行**と、銘の刻印・検索語のように
 * **いま打っている行**（`銘: !k`。選択肢は 1 つも無い）である。
 * **選択肢の有無を条件にしてはいけない**——打っている行が出なくなり、
 * 「打っても画面が何も変わらない」に戻る（2026-08-15 に決めた）。
 *
 * `line_index >= 0` は「その行が Term の写しにも出ている」という意味なので出さない。
 * 出すと同じ行が 2 か所に並ぶ（名前入力・生い立ち・自動拾いのエディタが全部これ）。
 * @note 判定を `--ui-check` から見るために公開している。
 */
bool prompt_bar_shows_text(const GameFrame &frame);

} // namespace hd2d
