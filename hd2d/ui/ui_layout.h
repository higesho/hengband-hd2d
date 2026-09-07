/*!
 * @file ui_layout.h
 * @brief 画面の作り（P8）— どこに 3D を描き、どこに何を置くか。
 *
 * 段取りは P8。
 *
 * ## 4 つある理由
 * **2026-08-08 に決めたは「全画面 3D と HengbandUi、折衷を 3 種切り替えられるように」。**
 * どれかを既定に決めて他を捨てるのではなく、**全部成立させる**のがこの段の要件である。
 * 4 つ目の `Tall` は**縦持ちの受け皿**（2026-08-12 に決めた「右サブパネル無し・
 * メインマップと下サブパネル 3 つの構成を追加。Android 縦持ちはこれをデフォルトに」）。
 *
 * | 作り | 3D | 常時出るもの | サブパネル 5 枚 |
 * |---|---|---|---|
 * | `Full` | 全画面 | HUD のゲージ・メッセージ・最下行 | 開いたときだけ重なる |
 * | `Hybrid` | 全画面 | キャラクター状態の列・メッセージ・最下行 | 開いたときだけ重なる |
 * | `Split` | MainMap 矩形の中 | 状態列・サブ 5 枚・最下行（全部同時） | **常に**枠の中 |
 * | `Tall` | MainMap 矩形の中 | 状態列・サブ **3 枚**・最下行 | **下段 3 枚だけ**（右の 2 枚は空） |
 *
 * ## ここが持つ約束
 * - **`scene` はカメラの矩形そのもの。**`Camera::viewport_w/h` は `scene.w/h` であり、
 *   `Camera::project()` が返す画素は**この矩形の左上を原点とする**。窓の座標へ戻すには
 *   `scene.x/y` を足す。逆写像（マウス）は引く。ここを取り違えると
 *   **描いた絵と当たり判定が食い違う**（必守制約 3）
 * - 比率は旧 2D UI の割り付けの定数と**同じ数**にしてある。コードは引かない
 *   （必守制約 6）。同じ画面を相手にしているので数が揃うだけである
 * - 帯の高さは**文字のマスから導く**（px 決め打ちにしない）。既存 UI は 22pt 前提の 88px 固定で、
 *   この exe はフォントの大きさが違う
 */
#pragma once

#include <string>

namespace hd2d {

//! 画面の作り。**どれも使う**（どれかが「正しい」わけではない）。
/*!
 * @brief ミニマップを置く所（2026-08-19 に決めた「表示位置（右上、右下、左上、左下、画面中央）」）。
 *
 * @details 置く先は **3D の枠（`UiLayout::scene`）の中**である。ミニマップは地図の上に
 * 重ねるものなので、窓の隅ではなく 3D の隅に寄る（`Split` で下段にパネルがあるとき、
 * 窓の右下はサブパネルの中になってしまう）。
 * @note 値は cfg に書くので**増やすときは末尾へ**。
 */
enum class MinimapCorner : int {
    TopRight = 0,
    BottomRight,
    TopLeft,
    BottomLeft,
    Centre,
    Count,
};

enum class LayoutMode {
    Full, //!< 全画面 3D ＋ ゲージだけ
    Hybrid, //!< 全画面 3D ＋ 状態列とメッセージ（既定）
    Split, //!< HengbandUi と同じ画面分割
    /*!
     * @brief 右の 2 枚を落とした画面分割 ＝ **縦持ち向け**（2026-08-12 に決めた）。
     * @details 状態列 ＋ 地図 ＋ 下段 3 枚。`Split` との違いは**右の列が無い**ことだけで、
     * 下段 3 枚の幅の配分（5/5/10）も上下の比（2:1）も `Split` と同じ数を使う。
     * **横持ちで選んでもよい**（縦でしか成り立たない作りではない）。ただし
     * バーチャルパッドのための場所取り（`kTallContentRatio`）は縦のときだけ効く。
     */
    Tall,
};

struct RectPx {
    int x{};
    int y{};
    int w{};
    int h{};

    bool empty() const { return (this->w <= 0) || (this->h <= 0); }
    bool contains(int px, int py) const
    {
        return (px >= this->x) && (px < (this->x + this->w)) && (py >= this->y) && (py < (this->y + this->h));
    }
    //! 四方を `n` 画素だけ内側へ縮める（枠の内側を取る）。
    RectPx inset(int n) const { return RectPx{ this->x + n, this->y + n, this->w - (n * 2), this->h - (n * 2) }; }
};

/*!
 * @brief パネル 1 枚の中身を書ける枠内（枠と余白を除いた所）。
 *
 * @details **余白の取り方はここ 1 か所**である。描く側（`game_hud.cpp`）だけでなく、
 * 「そのパネルに何桁入るか」から**大きさを決める側**（`place_term_overlay()`）と
 * **確かめる側**（`--ui-check`）も同じ式を見る必要がある。それぞれで持つと、
 * 「80 桁入るはずの小窓で 79 桁しか出ない」が静かに起きる。
 */
RectPx panel_body(const RectPx &r, int cell_w);

/*!
 * @brief サブパネルの枚数（プロトコルの `kSubPanelCount` と同じ 7）。
 * @details **2026-08-19 に 5 → 7**（「パネルの分割数を指定できるように」と決めた）。
 * 7 なのは**コアのサブウインドウがちょうど 7 枚**だから（`angband_terms[1..7]`）。
 * @note **枠は 7 つあるが、出るのは割り付けが使う枚だけ**である。
 * 下段は `sub[0..3]`・右列は `sub[4..6]` と**場所で固定**してあり、枚数を増減しても
 * 既に割り当てた中身が別の枠へずれない（下段を 3→4 にすると `sub[3]` が増えるだけ）。
 */
constexpr int kUiSubPanels = 7;

/*!
 * @name サブパネルの割り付け（2026-08-19 に決めた）
 * @details 下段 2〜4 枚・右列 1〜3 枚。**既定は下段 4・右列 3**
 * （2026-08-23 に決めた「サブパネルは画像の状態をデフォルトにして」。前は下段 3・右 2）。
 * 中身の既定（`hd2d_settings.h` の `sub_panel_kind`）と**対応させてある**ので、
 * 片方だけ変えないこと——枠と本文の長さが噛み合わなくなる。
 * 枠の番号は場所で決まる: `sub[0..3]` が下段（左から）、`sub[4..6]` が右列（上から）。
 * @{
 */
constexpr int kSubBottomMax = 4;
constexpr int kSubRightMax = 3;
constexpr int kSubBottomSlot = 0; //!< 下段の先頭の枠
constexpr int kSubRightSlot = kSubBottomMax; //!< 右列の先頭の枠（= 4）
//! 既定の枚数（**下段 4・右列 3**。2026-08-23 に決めた）。
constexpr int kSubBottomCountDefault = 4;
constexpr int kSubRightCountDefault = 3;
/*! @} */

/*!
 * @name 仕切りの位置（1/20 単位）
 * @details **最後の 1 枚は引き算で出す**（合計が必ずブロックの大きさになり、1px の隙間や
 * 重なりを作らない）ので、持つ数は「枚数 − 1」である。
 *
 * 既定は決めたこと（**2026-08-25**）の画のとおり:
 * **下段 4 / 4 / 5 /（残り 7）**、**右列 6 / 6 /（残り 8）**。
 * 下段は左の 3 枚（メッセージ・見えている敵・戦いの目）が 1 行の短い中身で、
 * **いちばん右の敵の地形の記憶だけが長文**なのでそこを広く取る。右列は
 * 冒険者・装備を詰めて、**持ち物にいちばん高さを渡す**割りである。
 *
 * 前の既定は下段 5 / 5 / 5（4 等分）・右列 8 / 6（2026-08-23 に決めた）で、
 * その前は下段 5 / 5 / 10（3 枚。左＝メッセージ・中央＝思い出・右＝装備）、
 * 右列 10 / 10（2 枚）だった。右列の 10 は `kRightSplitYRatio = 0.46` を
 * **使える高さに対する 1/20 単位へ移した**ものである。
 * @{
 */
constexpr int kSubBottomUnits = 20;
constexpr int kSubRightUnits = 20;
constexpr int kSubBottomW20Default[kSubBottomMax - 1] = { 4, 4, 5 };
constexpr int kSubRightH20Default[kSubRightMax - 1] = { 6, 6 };
//! 掴んで動かせる範囲（1 枚が潰れないように）。
constexpr int kSubBottomW20Min = 2;
//! 右列は**行数で潰れる**ので下段より広く取る（3/20 ＝ 使える高さの 15%）。
constexpr int kSubRightH20Min = 3;
/*! @} */

/*!
 * @name 仕切りを掴む帯の番号
 * @details 下段の仕切りが `grip[0..2]`、右列の仕切りが `grip[3..4]`。
 * **枚数を減らしても番号は動かない**（使わない仕切りの矩形が空になるだけ）ので、
 * 掴んでいる最中に枚数が変わっても掴む先が入れ替わらない。
 * @{
 */
constexpr int kSubGripBottom = 0;
constexpr int kSubGripRight = kSubBottomMax - 1; //!< = 3
constexpr int kSubGripCount = (kSubBottomMax - 1) + (kSubRightMax - 1); //!< = 5
/*! @} */

/*!
 * @brief サブパネルの割り付け（枚数と仕切り）。
 * @details 既定のまま作れば**従来と同じ絵**になる。`UiLayout::compute()` へ丸ごと渡す
 * ——引数を 2 本 4 本と増やしていくと、呼ぶ側が位置を数え間違える。
 */
struct SubSplit {
    int bottom_count{ kSubBottomCountDefault };
    int right_count{ kSubRightCountDefault };
    int bottom_w20[kSubBottomMax - 1]{ kSubBottomW20Default[0], kSubBottomW20Default[1],
        kSubBottomW20Default[2] };
    int right_h20[kSubRightMax - 1]{ kSubRightH20Default[0], kSubRightH20Default[1] };

    //! 実際に出る下段の枚数（範囲外の値が cfg から来ても潰れない）。
    int bottom() const;
    //! 実際に出る右列の枚数。**`Tall` では 0 枚**だが、それを決めるのは割り付けの側。
    int right() const;
};

/*!
 * @brief `Tall` で**ゲームの絵が使う高さ**（画面の高さに対する比）。残りはバーチャルパッドの場所。
 *
 * @details 参考にした画像 `VirtualPad_V.jpg`（1080×1920）の実測。
 * 地図が 0〜674、下段 3 枚が 674〜1009、その下 1010〜1920 が白＝パッドの領域だった。
 * 1009.5 / 1920 ≒ **0.526**。
 *
 * **効くのは「`Tall` かつ縦向きかつパッドを出している」ときだけ。**
 * 横向きのパッドは画面いっぱいに広がる（下半分に固まっていない）ので、
 * 同じ場所取りをすると地図が理由なく縮む。パッドを切れば全高を使う。
 */
constexpr double kTallContentRatio = 0.526;

struct UiLayout {
    LayoutMode mode{ LayoutMode::Hybrid };
    /*!
     * @name 使える画面（**窓そのものとは限らない**）
     * @details Android は切り欠き（ノッチ・パンチ穴）を避けた範囲だけを使う
     * （`platform/android/android_safe_area.h`。テーマでは避けられない）。
     * `screen_w/h` は**使える大きさ**、`origin_x/y` は窓の中でのその左上である。
     *
     * **中の矩形（`scene` ほか）は窓の座標**（原点を足した後の値）で入っている。
     * マウスと指の座標も窓の座標なので、当たり判定はそのまま噛み合う。
     * 逆に「画面の真ん中」を自分で出す側（`FeatureMenu::panel_rect`）は
     * **原点を足す**こと——足さないと切り欠きのぶんだけ左上へずれる。
     * @{
     */
    int screen_w{};
    int screen_h{};
    int origin_x{};
    int origin_y{};
    /*! @} */
    //! 等幅のマス目（`TextOverlay::cell_w()/cell_h()`）。行数・桁数はここから割る。
    int cell_w{ 8 };
    int cell_h{ 16 };

    /*!
     * @brief ゲームの絵に使える範囲。**ふつうは使える画面そのもの。**
     * @details `Tall` で縦持ち・パッド表示のときだけ、下がバーチャルパッドのぶん空く
     * （`kTallContentRatio`）。**割り付けはこの中で組む**ので、下に何を足すときも
     * 「画面の高さ」ではなくこちらから採ること——画面の高さから採ると、
     * 足した物だけがパッドの下へ潜る。
     */
    RectPx content{};

    /*!
     * @brief 3D を描く矩形。**カメラの原点はここの左上。**
     * @details `Full` / `Hybrid` では窓いっぱい、`Split` / `Tall` では MainMap の矩形。
     */
    RectPx scene{};

    //! キャラクター状態の列（`Hybrid` / `Split`）。`Full` では空。
    RectPx status_col{};
    /*!
     * @brief HP/MP の棒。**全部の作りで地図の左下**（2026-08-14 に決めた）。
     * @details もとは `Full` の左上だけだった。作りごとに置き場所が変わると、
     * 切り替えるたびに目が探し直すことになる。状態列にも数字は出るが、
     * 棒は「減ったことに一目で気づく」ための別物である。
     */
    RectPx hud{};
    /*!
     * @brief 直近のメッセージ。**Sub1 が出ているときは空**。
     * @details **メッセージの家は 1 つ。**Sub1 の既定の中身がメッセージなので
     * （既存 UI と同じ割り当て）、Sub1 が画面に出ている間にこの帯も出すと同じものが
     * 2 か所に並ぶ。`Split` と、重なる作りで**サブパネルを開いている**間は空になる。
     */
    RectPx message_bar{};
    /*!
     * @brief プロンプト（`[y/n]` など）1 行。**3 つの作りとも必ず出る場所。**
     * @details メッセージと違って**答えないと先へ進まない**ので、サブパネルの開閉で
     * 出たり消えたりしてはいけない。地図の上（空いている所の下端）に重ねる。
     */
    RectPx prompt_bar{};
    //! コアの最下行（ステータスバー）＋操作ヒント。
    RectPx bottom_bar{};
    //! ミニマップ。既定は `scene` の右上（置く所は `MinimapCorner` で選べる）。
    RectPx minimap{};
    /*!
     * @brief ミニマップを**置いてよい領域**（`minimap` はこの中に必ず収まる）。
     *
     * @details 重なる作り（`Full` / `Hybrid`）では、常時出るもの（メッセージの帯・状態列）と
     * **開いたサブパネルの外側**だけが使える。だから「画面の隅」ではなく
     * 「この領域の隅」に置く——そうしないと、サブパネルを開いた瞬間に
     * ミニマップがパネルの下へ消える。
     *
     * ここを公開しているのは**検査のため**である。置いた基準を持たずに
     * 「窓の右か」で見る検査を書いたら、サブパネルを開いた形で軒並み落ちた
     * （＝検査の基準が間違っていた。2026-08-19）。描いた所と検査する所は同じ矩形から出す。
     */
    RectPx minimap_area{};

    /*!
     * @brief サブパネルの枠（添字 0..3 が下段の左から、4..6 が右列の上から）。
     * @details **出ない枚は空**（矩形が空）。`Tall` では右列がまるごと空、
     * 下段を 3 枚にしていれば `sub[3]` が空、右を 2 枚にしていれば `sub[6]` が空になる。
     * 描く側も数える側も「空でない枠だけ」を見ること。
     */
    RectPx sub[kUiSubPanels]{};
    /*!
     * @brief サブパネルが 3D の上に**重なる**か（`Full` / `Hybrid` で真）。
     * @details 重なる作りでは下敷きを半透明にし、閉じているときは 1 枚も描かない。
     */
    bool sub_overlaid{ true };

    /*!
     * @brief コアの Term を写す矩形（タイトル・birth・死亡など**全面**のとき）。
     * @details 3 つの作りとも**窓いっぱい**にする。この画面には 3D が 1 つも無いので、
     * 作りによって出る場所が変わる意味が無い。
     */
    RectPx term_full{};
    //! ゲーム中メニュー（店・持ち物など）を写す小窓。`term_full` の 85% × 90%。
    RectPx term_overlay{};

    /*!
     * @brief 仕切りを掴む帯。**下段が `grip[0..2]`（縦棒）、右列が `grip[3..4]`（横棒）**。
     * @details **描く絵と当たり判定を同じ矩形から作る**ので、掴めるように見えて掴めない所が
     * できない（必守制約 3 の親戚）。その仕切りが無い作り・枚数のときは空。
     * @note 番号は**場所で固定**（`kSubGripBottom` / `kSubGripRight`）。枚数で詰めると、
     * 掴んでいる最中に枚数が変わったとき掴む先が入れ替わる。
     */
    RectPx grip[kSubGripCount]{};

    /*!
     * @brief 画面の実寸と文字のマスから全部の矩形を出す。
     * @param screen_w,screen_h **使える大きさ**（切り欠きを避けた後。窓の実寸とは限らない）。
     * @param subs_open サブパネルを開いているか（重なる作りでのみ効く）。
     * @param split サブパネルの枚数と仕切り（`SubSplit`）。**利用者が機能メニューで選んだ
     *   枚数と、境界を掴んで動かした結果**がここへ来る。既定は下段 3・右 2 ＝ 従来の絵。
     * @param origin_x,origin_y 使える範囲の左上（窓の座標）。既定の 0,0 は「窓いっぱい」。
     *   **中の割り付けは今までどおり 0 起点で組み、最後にまとめてずらす**
     *   ——比率の計算に原点が混ざると、切り欠きのある端末だけ配分が変わる。
     * @param reserve_pad バーチャルパッドを出しているか。**`Tall` かつ縦向きのときだけ**
     *   下に場所を空ける（`kTallContentRatio`）。他の作りでは何も変わらない。
     * @param minimap_corner ミニマップを置く所（機能メニュー ＞ ミニマップ）。
     * @param minimap_size_pct ミニマップの大きさ（%）。100 が従来。
     * @note **既定のまま呼べば従来と 1 画素も変わらない。**検査（`--ui-check` など）は
     *   既定で呼ぶので、設定を足したことで検査の期待値が動かない。
     */
    static UiLayout compute(LayoutMode mode, int screen_w, int screen_h, int cell_w, int cell_h, bool subs_open,
        const SubSplit &split = SubSplit{},
        int origin_x = 0, int origin_y = 0, bool reserve_pad = false, bool minimap_bottom = false,
        MinimapCorner minimap_corner = MinimapCorner::TopRight, int minimap_size_pct = 100);

    //! 中の矩形をまとめてずらす（`compute` の最後が使う）。
    void offset_rects(int dx, int dy);
};

/*!
 * @brief コアが送ってきた Term ミラーの行数を覚える（`GameFrame::menu_term_lines`）。
 * @param rows 届いた行数。**24 未満は 24 に丸める**（それより小さいコアは無い）
 * @details **画面側にコアごとの数を持たせない**ための手段（必守制約 1）。
 * 変愚は 24 行（`src/term/gameterm.h` の `TERM_DEFAULT_ROWS`）、幻想蛮怒と Sil-Q は 27 行。
 * 24 で小窓を組むと**下 3 行が切れ**、店のコマンド行が画面から消える
 * （あれは Term のいちばん下に出る）。
 *
 * `set_status_col_cols()` と同じ理由で自由関数にしてある。
 * **フレームを受け取る道からだけ呼ぶこと。**
 */
void set_term_mirror_rows(int rows);

//! いま覚えている行数（既定 24）。
int term_mirror_rows();

/*!
 * @brief コアが申告した状態列の桁数を覚える（`GameFrame::status_col_cols`）。
 * @param cols 桁数。**0 以下は「申告なし」**で、従来どおり 13 桁になる
 * @details **画面側にコアごとの数を持たせない**ための手段（必守制約 1）。
 * 変愚は 13（`main-window-row-column.h`）、幻想蛮怒は 20（`COL_MAP`）。
 * 20 桁のコアで申告を落とすと、地名「夢殿大祀廟の洞窟」が「夢殿大祀廟の洞」になる。
 *
 * `UiLayout::compute()` の引数にしていないのは、呼び出し元が 20 か所以上あり、
 * そのほとんどが検査だからである（`ui_paint.h` の `set_panel_transparent()` と同じ形）。
 * **フレームを受け取る道からだけ呼ぶこと。**
 */
void set_status_col_cols(int cols);

//! いま覚えている桁数（0 は申告なし）。
int status_col_cols();

/*!
 * @brief 状態列を置く側を覚える（**0 = 左・1 = 右**）。
 * @details 値は**画面側で解決済み**のもの——`hd2d_app.cpp` が設定
 * （`Hd2dSettings::StatusColSide`。自動ならコアの申告 `GameFrame::status_col_side`）
 * を 0/1 に畳んでから渡す。ここに `Auto` は来ない。
 * `set_status_col_cols()` と同じ理由で自由関数にしてある
 * （`compute()` の呼び出し元は 20 か所以上あり、ほとんどが検査）。
 * **検査は誰も呼ばない**ので、`--ui-check` の期待値（左置き）は動かない。
 * 基準は （決めたこと F2。**5 本すべてのコアで効く**）。
 */
void set_status_col_side(int side);

//! いま覚えている側（既定 0 = 左）。
int status_col_side();

/*!
 * @brief `HD2D_BREAK_UI` で**わざと壊す**対象か（検査の検査）。
 * @param what `"panels"`（描かない）/ `"cursor"`（カーソルを動かさない）。
 * @details P6 の `HD2D_BREAK_SHADOW_MOTION`・P7 の `HD2D_BREAK_POST` と同じ考え方。
 * 綴りは `panels` / `cursor` / `all`（`1` は `all` と同じ）。**読むのはここ 1 か所**
 * （2 か所で読むと片方だけ直された状態に必ずなる）。
 */
bool ui_break_enabled(const char *what);

//! `--layout=full|hybrid|split` の綴りから読む。読めたら true。
bool parse_layout_mode(const std::string &spec, LayoutMode &out);
//! 画面と記録に出す名前。
const char *layout_mode_name(LayoutMode mode);

} // namespace hd2d
