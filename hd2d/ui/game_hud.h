/*!
 * @file game_hud.h
 * @brief フレームの中身を画面へ置く（P8）— 状態列・サブパネル・最下段・
 *   メッセージ・ミニマップ・Term の写し。
 *
 * 基準は P8。**旧2D画面 と同じことができる**のが完了条件で、
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
#include "ui/modern_store_ui.h"
#include "ui/orb_hud.h"
#include "ui/targeting_ui.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
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
    /*!
     * @brief HP/MP の玉の 6 層とバフ・デバフのアイコンのシート。
     * @details **背面の絵（`title` / `panel_*`）とは独立に有効・無効になる**（呼び出し側が
     * 「背面の絵」の設定を切っても玉は出る）。読めなかった層があれば玉もアイコンも描かず、
     * 元の棒を描く。
     */
    OrbHudImages orb;

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
    ModernStoreUi modern;
    TargetingUi targeting;
    int modern_theme{0};
    int modern_font_level{0};
    mutable int native_scroll_row{0},native_scroll_col{0},native_focus{-2};
    mutable bool native_view_valid{false};
    mutable std::string native_signature;
    mutable GameFrame native_view;
    int window_theme{-1};
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
     * @name HP/MP の玉の動き
     * @details 設定（`Hd2dSettings` の `orb_*`）と時計は、描く側が持っていないので
     * 毎フレームここへ入れる（`minimap_*` と同じ流儀）。動きの状態 `orb_anim` は
     * 描画の外に持つ（描くたびに進めるので `mutable`）。
     * @{
     */
    OrbHudParams orb_params;
    //! 実時計（ms）。0 のままなら動きは止まる（検査が時刻を決めて渡す）。
    std::uint32_t orb_now_ms{ 0 };
    mutable OrbHudAnimation orb_anim;
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
 * @brief 下の帯の操作説明の行で、左のコントローラーの案内に渡せる幅（修-14）。
 * @param body_w 帯の中身の幅。
 * @param hint_w コントローラーの案内を 1 行に収めたときの幅（0 なら案内は無い）。
 * @param key_w UI のキーの案内の幅（右端に寄せる。0 なら無い）。呼ぶ側で `body_w` までに丸めておく。
 * @param gap 左右の案内の間に空ける幅。
 * @param min_room これより狭くしか渡せないなら案内を出さない（切れた断片を出さない）。
 * @return 左の案内を切る幅。0 なら出さない。**実際に描く幅 `min(hint_w, 戻り)` と `key_w` の合計は
 *   いつも `body_w` 以下**で、左右の矩形は交わらない。
 * @details 帯の高さを増やさずに重なりを避けるため、入らないときはキーの案内を右端に残して左を切る。
 */
int bottom_hint_room(int body_w, int hint_w, int key_w, int gap, int min_room);

/*!
 * @brief 問いの帯（`draw_prompt_bar()`）に出す 1 行。入りきらないとき、選んでいる選択肢が見える範囲だけに絞ったもの。
 * @details 帯は 1 行で折り返さず、幅を超えた字を切る。選択肢を横に並べた帯（Frox の `Travel: `・各コアの `Look: `）は
 * 幅を超えやすく、右の選択肢が見えないうえ、LB/RB で選んでも枠が見えない所へ行っていた（修-32 の担当の気づき）。
 */
struct PromptBandView {
    std::string text; //!< 帯へ描く字（入るときは元の行そのもの）
    int frame_x = -1; //!< 選んでいる選択肢の枠の左端（帯の左端からの画素）。-1 なら枠を出さない
    int frame_w = 0; //!< 枠の幅
    bool cut = false; //!< 絞ったか（入るときは偽。偽なら `text` は元の行と同じ）
};

/*!
 * @param line 帯の 1 行（`frame.prompt.text_utf8`。改行を含まないもの）。
 * @param ranges 選択肢の範囲（`PromptChoice` の `begin`・`len`。バイト）。**前から順に並び、重ならず、`len > 0`**
 *   であること。満たさないときは絞らない（`cut` が偽・枠は選択肢の位置のまま）。
 * @param selected 選んでいる選択肢の番号。
 * @param limit 字を置ける幅（画素）。
 * @param measure 字の幅を測る関数（画素）。
 * @details 入るなら元の行をそのまま返す（従来と同じ）。入らないときは、見出し（最初の選択肢より前）を残し、
 * 選んでいる選択肢が入るように前の選択肢を `... ` で省き、後ろの省いた選択肢も ` ...` で示す。
 * 切るのは選択肢の境目だけで、字の途中では切らない。
 */
PromptBandView fit_prompt_band(const std::string &line, const std::vector<std::pair<int, int>> &ranges, int selected,
    int limit, const std::function<int(const std::string &)> &measure);

/*!
 * @brief 1 フレームぶんの UI を積む。
 * @param paint 下敷き・枠・ゲージ。**呼び出し側が `text` より先に流すこと。**
 * @param text 文字。
 * @details Term の写し（タイトル・店・メニュー）が出ているときは、地図まわりの
 * パネルを描かない。**その画面には地図が無い**ので、古い中身が残って見えると嘘になる。
 */
void draw_game_ui(UiPaint &paint, TextOverlay &text, const UiLayout &layout,
    const GameFrame &frame, const HudState &state, const UiBackdrops *backdrops = nullptr,
    bool modern_window = true);

/*!
 * @brief 新 UI の窓の下にマップとサブパネルを描き続ける画か（タイトル・キャラクター作成以外）。
 */
bool modern_window_keeps_hud(const GameFrame &frame);

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
RectPx draw_ascii_map_panel(UiPaint &paint, TextOverlay &text, const RectPx &panel, const GameFrame &frame, TargetingUi *targeting=nullptr, double time=0.);

/*!
 * @brief Term の写しの上で画面座標 `(x, y)` に当たる選択肢の添字（無ければ -1）。
 * @details クリック／タップで選択肢を直接選ぶための逆引き（P5 その2・2026-08-19。
 * 「セーブデータ選択が文字指定でしか選択できない」に気づいた）。当たりの矩形は
 * 描画（`draw_term_mirror` の枠）と同じ計算なので、見えている枠とずれない。
 * `menu_choices` が立っている画面すべてで効く（幻想蛮怒のセーブ選択・変愚の店や建物）。
 */
int menu_choice_at(TextOverlay &text, const UiLayout &layout, const GameFrame &frame, int x, int y,int theme=-1,const HudState *state=nullptr);

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
