/*!
 * @file ui_layout.cpp
 * @brief `ui_layout.h` の実装。
 */
#include "ui/ui_layout.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace hd2d {

namespace {

/*!
 * @name 比率
 * @details 旧 2D UI の割り付けの定数と**同じ数**である（コードは引かない。必守制約 6）。
 * 既存 UI で利用者が使い込んだ配分なので、`Split` を選んだときに勝手に変えない。
 * @{
 */
constexpr double kRightColRatio = 0.33;
constexpr double kRightColRatioMin = 0.31;
constexpr double kRightColRatioMax = 0.35;
constexpr double kBottomRowRatio = 1.0 / 3.0;
constexpr double kRightSplitYRatio = 0.46;
/*! @} */

/*!
 * @name 状態列の桁数
 * @details **コアが申告する**（`GameFrame::status_col_cols`）。変愚は 13、幻想蛮怒は
 * 20（`COL_MAP`）。申告が無い（0）ときは 13 に落ちるので、古いコアでも絵は変わらない。
 *
 * 自由関数の APIにしてあるのは、`build_layout()` が static で呼び出し元が 20 か所以上
 * あるためである（`ui_paint.h` の `set_panel_transparent()` と同じ形）。
 * **検査は誰も `set_status_col_cols()` を呼ばない**ので、`--ui-check` の期待値は動かない。
 * @{
 */
constexpr int kStatusColRefCells = 13; //!< 申告が無いときの桁数（＝上限の比率を決めたときの桁数）
int g_status_col_cols = 0;
/*!
 * @brief 状態列を置く側（**0 = 左・1 = 右**。既定 0）。
 * @details 解決は `hd2d_app.cpp`（設定が自動ならコアの申告に従う）。ここは置くだけ。
 * 基準は （決めたこと F2）。
 */
int g_status_col_side = 0;
/*! @} */

/*!
 * @brief 帯を `count` 枚へ割り付ける（1/20 単位 → 画素）。
 *
 * @param total_px 割る帯の長さ（下段なら幅、右列なら高さ）。
 * @param units 前から `count - 1` 枚ぶんの 1/20 単位。**最後の 1 枚は持たない。**
 * @param units_min 1 枚が潰れない下限。
 * @param out_px `count` 枚ぶんの画素（`total_px` の完全な分割になる）。
 *
 * @details **余りは最後の枚が吸う**（合計が必ず `total_px` に一致し、1px の隙間や
 * 重なりができない）。境界の画素は累計から出す——1 枚ずつ丸めて足すと、
 * 端が 1px ずれる形が残る。
 *
 * @note 下段（幅）と右列（高さ）で**同じ関数を通す**。別々に書くと、片方だけ
 * 「合計が合わない」を直した状態が必ずできる。
 */
void split_units(int total_px, const int *units, int count, int units_min, int *out_px)
{
    int used_px = 0;
    int acc = 0;
    for (int i = 0; i + 1 < count; ++i) {
        //! 後ろの枚が潰れないところで止める（残り枚数 × 下限を必ず残す）。
        const int room = kSubBottomUnits - acc - (units_min * (count - 1 - i));
        const int a = std::clamp(units[i], units_min, std::max(units_min, room));
        acc += a;
        const int edge = static_cast<int>(static_cast<long long>(total_px) * acc / kSubBottomUnits);
        out_px[i] = edge - used_px;
        used_px = edge;
    }
    out_px[count - 1] = total_px - used_px;
}

//! 下段を `split` のとおりに置く。**枠は `sub[0..3]` で場所固定**（枚数で詰めない）。
void place_bottom_subs(UiLayout &layout, int x0, int block_w, int y, int h, const SubSplit &split)
{
    const int n = split.bottom();
    int px[kSubBottomMax]{};
    split_units(block_w, split.bottom_w20, n, kSubBottomW20Min, px);
    int x = x0;
    for (int i = 0; i < n; ++i) {
        layout.sub[static_cast<std::size_t>(kSubBottomSlot + i)] = { x, y, px[i], h };
        x += px[i];
    }
}

//! 右列を `split` のとおりに置く。**枠は `sub[4..6]`**。
void place_right_subs(UiLayout &layout, int x, int col_w, int y0, int col_h, const SubSplit &split)
{
    const int m = split.right();
    int px[kSubRightMax]{};
    split_units(col_h, split.right_h20, m, kSubRightH20Min, px);
    int y = y0;
    for (int i = 0; i < m; ++i) {
        layout.sub[static_cast<std::size_t>(kSubRightSlot + i)] = { x, y, col_w, px[i] };
        y += px[i];
    }
}

/*!
 * @name コアの Term のマス目（`src/term/gameterm.h` の `TERM_DEFAULT_COLS/ROWS`）
 *
 * @details 無頭のコア（`HengbandCore.exe`）は**この大きさで固定**なので、
 * ゲーム中のメニュー・店・建物の写しが 80×24 を超えることは無い。
 * つまり「入るか入らないか」は窓の大きさだけで決まり、フレームを見なくても分かる。
 * **数を写しているのであってコードは引いていない**（必守制約 4・6）。
 * @{
 */
constexpr int kTermMirrorCols = 80;
/*!
 * ミラーの行数。**コアによって違う**——変愚は 24（`TERM_DEFAULT_ROWS`）、
 * 幻想蛮怒と Sil-Q は 27（それぞれのアダプタの `kTermRows`）。24 で組むと
 * **下 3 行が黙って切れる**。店のコマンド行（`ESC) 建物から出る` `p) 商品を買う` …）は
 * Term のいちばん下に出るので、切れると**買う手段が画面から消える**（実測して踏んだ）。
 * だから既定値は持つが、**届いた行数で上書きする**（`set_term_mirror_rows`）。
 * 桁は 80 のまま——v1 のミラーは 80 桁と決まっており、行末の空白は落として送られてくる
 * ので、届いた文字から実際の桁数は測れない。
 */
constexpr int kTermMirrorRows = 24;
int g_term_mirror_rows = kTermMirrorRows;
/*! @} */

/*!
 * @brief ゲーム中のメニュー（通常メニュー・店・建物）を写す小窓を置く。
 *
 * @details **地図の枠（`scene`）の中に収める**（2026-08-11 に決めた
 * 「通常メニューも枠が全画面近くまで担っていた。建物メニューも」）。
 * 従来は窓の 85% × 90% で、画面の作りに関係なく**窓**を基準にしていたため、
 * 1600×900 では 1360×810 ＝ ほぼ全画面の板になっていた。
 *
 * **中身はコアの文字グリッドなので、割り当ての一覧のようには縮められない。**
 * 80 桁が入る大きさを割ると店の品書きが桁で切れて読めなくなる。だから大きさは
 * **常に「要るぶん」**（80 桁 × コアの行数 ＋ 余白）で、枠に収まるかどうかで
 * 変えない。収まらないときは**足りないぶんだけ**地図へ被せ、画面の中身
 * （`content`）の内側へ寄せる。**黙って切らない**のが要点である。
 *
 * かつては収まらないと窓の 85%×90% へ逃げていたが、**8 ピクセル足りないだけで
 * 板が 2 倍以上に跳ねて左の状態欄まで覆う**という壊れ方をした（Sil-Q は 27 行で、
 * 1920×1080 の機体でちょうどそれを踏む）。逃げ道の粗さが症状そのものだった。
 *
 * @note `term_full`（タイトル・birth・死亡）はここでは触らない。
 * あちらの画面には地図が無いので、窓いっぱいで正しい。
 */
void place_term_overlay(UiLayout &layout)
{
    /*
     * 要る大きさ ＝ 80×（コアの行数）のマス ＋ `panel_body()` が内側へ取る余白。
     * **余白の式は `game_hud.cpp` にしかない**ので、そこから逆に測る
     * （ここで「たぶん 1 桁ぶん」と当てると、79 桁しか出ない小窓が静かにできる）。
     */
    const int pad_w = (layout.cell_w * 40) - panel_body(RectPx{ 0, 0, layout.cell_w * 40, 0 }, layout.cell_w).w;
    const int pad_h = pad_w; //!< `panel_body()` は上下左右とも同じ量を取る
    const int need_w = (kTermMirrorCols * layout.cell_w) + pad_w;
    const int need_h = (g_term_mirror_rows * layout.cell_h) + pad_h;

    /*
     * 置き場所は**地図の枠の中心**。地図が無い作りでは画面の中身そのものを使う。
     */
    const RectPx &box = layout.content;
    const RectPx &area = layout.scene.empty() ? box : layout.scene;

    /*
     * **収まらなくても「要る大きさ」で置く。**（2026-08-22。実機で踏んだ）
     *
     * 以前は収まらないと**窓の 85%×90%** へ逃げていた。ところが Sil-Q（27 行）は
     * 1920×1080 の機体で **8 ピクセル**だけ足りず、その 8px のために板が
     * 648×656 から 1632×972 へ跳ね、左の状態欄まで覆っていた。
     * 「読めない板を出すくらいなら地図に被せる」という判断そのものは正しいが、
     * **被せる量は足りないぶんだけでよい。**
     *
     * だから大きさは常に `need`。はみ出したぶんは `content` の中へ寄せるだけにする
     * （`Tall` で下がパッドに取られていても、板が画面の外へ潜らない）。
     */
    const int w = std::min(need_w, box.w);
    const int h = std::min(need_h, box.h);
    int x = area.x + ((area.w - w) / 2);
    int y = area.y + ((area.h - h) / 2);
    x = std::clamp(x, box.x, box.x + std::max(0, box.w - w));
    y = std::clamp(y, box.y, box.y + std::max(0, box.h - h));
    layout.term_overlay = RectPx{ x, y, w, h };

    /*
     * **画面そのものが足りないときだけ**報せる。ここまで来ると桁か行が切れる
     * ——店のコマンド行が消えるので、黙って切ってはいけない。
     */
    if ((w < need_w) || (h < need_h)) {
        static int said_w = -1;
        static int said_h = -1;
        if ((need_w != said_w) || (need_h != said_h)) {
            said_w = need_w;
            said_h = need_h;
            std::fprintf(stderr,
                "[hd2d] 小窓が画面に入りません（切れます）: 要る %dx%d"
                "（%d桁 x %d行 ＋ 余白 %dx%d・マス %dx%d）/ 中身 %dx%d\n",
                need_w, need_h, kTermMirrorCols, g_term_mirror_rows, pad_w, pad_h,
                layout.cell_w, layout.cell_h, box.w, box.h);
            std::fflush(stderr);
        }
    }
}

/*!
 * @brief 仕切りを掴む帯。**描く絵と当たり判定を同じ矩形から作る**（食い違わせない）。
 * @details 下段は隣り合う枠の境に**縦棒**、右列は**横棒**を置く。番号は場所で固定
 * （`kSubGripBottom` / `kSubGripRight`）なので、枚数を減らすと使わない帯が空になるだけ。
 */
void place_grips(UiLayout &layout)
{
    const int half_w = std::max(3, layout.cell_w / 2);
    const int half_h = std::max(3, layout.cell_h / 2);
    for (int i = 0; i + 1 < kSubBottomMax; ++i) {
        const RectPx &left = layout.sub[static_cast<std::size_t>(kSubBottomSlot + i)];
        const RectPx &right = layout.sub[static_cast<std::size_t>(kSubBottomSlot + i + 1)];
        if (left.empty() || right.empty()) {
            continue;
        }
        layout.grip[static_cast<std::size_t>(kSubGripBottom + i)]
            = { right.x - half_w, right.y, half_w * 2, right.h };
    }
    for (int i = 0; i + 1 < kSubRightMax; ++i) {
        const RectPx &upper = layout.sub[static_cast<std::size_t>(kSubRightSlot + i)];
        const RectPx &lower = layout.sub[static_cast<std::size_t>(kSubRightSlot + i + 1)];
        if (upper.empty() || lower.empty()) {
            continue;
        }
        layout.grip[static_cast<std::size_t>(kSubGripRight + i)]
            = { lower.x, lower.y - half_h, lower.w, half_h * 2 };
    }
}

/*!
 * @brief 状態列の幅（画素）。
 * @param cap_ratio 画面幅に対する上限の比率。**13 桁を前提に決めた値**なので、
 *   申告された桁数に比例させる（そうしないと 20 桁を申告しても上限が先に噛んで
 *   切れたまま——実測では 1600 幅で 13 桁ぶんの上限に既に当たっている）。
 * @details 桁数は**コアの申告**（`set_status_col_cols`）。申告が無ければ 13 桁。
 */
int status_col_width(int cell_w, int screen_w, double cap_ratio = 0.12)
{
    const int cols = (g_status_col_cols > 0) ? g_status_col_cols : kStatusColRefCells;
    const int by_columns = (cell_w * cols) + (cell_w * 2); // 桁 ＋ 左右の余白 1 桁ずつ
    const double ratio = cap_ratio * static_cast<double>(cols) / static_cast<double>(kStatusColRefCells);
    const int cap = static_cast<int>(std::lround(static_cast<double>(screen_w) * ratio));
    return std::clamp(std::min(by_columns, cap), 0, screen_w / 2);
}

int clamp_i(int v, int lo, int hi)
{
    return std::max(lo, std::min(v, hi));
}

//! 重なる作りでのサブパネルの置き場所（`Split` の枠と同じ配分にする）。
void place_overlaid_subs(UiLayout &layout, bool subs_open, int usable_h, const SubSplit &split)
{
    layout.sub_overlaid = true;
    if (!subs_open) {
        return; // 閉じているときは 1 枚も無い（矩形は空のまま）
    }
    const int w = layout.screen_w;
    int right_w = static_cast<int>(std::lround(static_cast<double>(w) * kRightColRatio));
    right_w = clamp_i(right_w,
        static_cast<int>(std::lround(static_cast<double>(w) * kRightColRatioMin)),
        static_cast<int>(std::lround(static_cast<double>(w) * kRightColRatioMax)));
    const int left_w = w - right_w;
    const int bottom_h = static_cast<int>(std::floor(static_cast<double>(usable_h) * kBottomRowRatio));
    const int top_h = usable_h - bottom_h;

    place_bottom_subs(layout, 0, left_w, top_h, bottom_h, split);
    place_right_subs(layout, left_w, right_w, 0, usable_h, split);
}

/*!
 * @brief ミニマップは**空いている所の右上**（2026-08-08 に決めた。旧 HD2D と同じ位置）。
 * @param free 他の板に取られていない矩形。**`scene` ではない**のは、重なる作りで
 * サブパネルを開いたときにその下敷きになるためである。「どうせ上に描くから」で重ねると、
 * 開いた瞬間にミニマップが消えたように見える。
 * @details 高さを幅の 2/3 にするのは**フロアが横長（198×66）だから**である。
 * 正方形にすると 2/3 が空く（旧 HD2D も同じ比で取っている）。
 */
void place_minimap(UiLayout &layout, const RectPx &free, MinimapCorner corner, int size_pct)
{
    if (free.empty()) {
        return;
    }
    /*
     * **縦横とも 2 倍**（2026-08-09 に決めた「ミニマップは縦横今の倍のサイズにして」）。
     * 予算そのものを広げるので、切り出せるマス数と 1 マスの画素の両方が増える
     * （`draw_minimap` の `kPixelsPerGrid` も一緒に上げてある）。
     * 上限は「空いている所の 2/3」で止める。ここを外すと狭い窓で 3D が隠れる。
     *
     * **縦持ちだけ、そこからさらに 2/3 にする**（2026-08-12 に決めた「ミニマップが
     * キャラに被るので、Android 版縦持ちのみ枠を今の 2/3 にしよう」＋実機の写真）。
     * 縦では地図の枠が短く狭いので、同じ式でも**枠の 6 割**を覆ってしまい、
     * 画面の真ん中に立っている自機に被る。
     *
     * **予算・下限・上限の 3 つとも掛ける**のが要点である。予算だけ縮めると
     * 下限（`cell_h * 16`）に引っかかって 15% しか小さくならない
     * （実機の値で計算すると 472px → 400px にしかならず、指示の 2/3 にならない）。
     * 3 つとも掛ければ、いま下限・上限・自然値のどれで決まっていても結果は必ず 2/3 になる。
     *
     * @note **Android ではなく「縦持ち」で分けている。**指示は「Android 版縦持ちのみ」だが、
     * 起きているのは縦長の枠という形の問題で、機種の問題ではない。ここを `__ANDROID__` で
     * 切ると、Windows の `--ui-check --windowed=720x1280` が**実機と違う絵**を出すことになり、
     * 縦の見え方を手元で確かめる道が無くなる。横持ちは Android でも Windows でも変わらない。
     */
    const bool portrait = layout.screen_h > layout.screen_w;
    const auto shrink = [portrait](int v) { return portrait ? ((v * 2) / 3) : v; };
    int side = clamp_i(shrink(std::min(free.w, free.h) * 2 / 3),
        shrink(layout.cell_h * 16), shrink(layout.cell_h * 36));
    /*
     * **表示サイズ**（2026-08-19 に決めた）。上で決めた「自然な大きさ」に対する割合で、
     * 100 が従来である（`size_pct == 100` のとき乗除で値が変わらないことが要点——
     * 検査の期待値が動かない）。上限は「空いている所」に収まる所で切る。
     */
    if (size_pct != 100) {
        const int wanted = (side * clamp_i(size_pct, 25, 400)) / 100;
        side = clamp_i(wanted, layout.cell_h * 6, std::min(free.w, free.h));
    }
    int height = (side * 2) / 3;
    const int pad = layout.cell_w;
    if (((side + (pad * 2)) > free.w) || ((height + (pad * 2)) > free.h)) {
        /*
         * 入らない。**黙って重ねない**のは従来どおりだが、**大きさを指定されている
         * ときだけは縮めて置く**——「大きくしたら消えた」は設定として使えない。
         */
        if (size_pct <= 100) {
            return;
        }
        side = std::max(layout.cell_h * 6, free.w - (pad * 2));
        height = std::min((side * 2) / 3, std::max(layout.cell_h * 4, free.h - (pad * 2)));
        if (((side + (pad * 2)) > free.w) || ((height + (pad * 2)) > free.h)) {
            return;
        }
    }
    /*
     * **置く所**（2026-08-19 に決めた「表示位置（右上、右下、左上、左下、画面中央）」）。
     * 従来は右上だけだったので、`TopRight` は 1 画素も動かない式のままにしてある。
     *
     * 隅は**この領域の隅**である（窓の隅ではない）。理由は `UiLayout::minimap_area` の注記。
     */
    layout.minimap_area = free;
    const int left = free.x + pad;
    const int right = free.x + free.w - side - pad;
    const int top = free.y + pad;
    const int bottom = free.y + free.h - height - pad;
    switch (corner) {
    case MinimapCorner::BottomRight:
        layout.minimap = { right, bottom, side, height };
        break;
    case MinimapCorner::TopLeft:
        layout.minimap = { left, top, side, height };
        break;
    case MinimapCorner::BottomLeft:
        layout.minimap = { left, bottom, side, height };
        break;
    case MinimapCorner::Centre: {
        /*
         * **メインマップのど真ん中**（2026-08-19 に決めた:「中央表示は Diablo や
         * シレンシリーズを想定している」→「メインマップのど真ん中だね」→
         * 「サブパネルには被らないように」）。
         *
         * これは隅に寄せる 4 つとは性格が違う——隅の 4 つが「常に出す小さな地図」なら、
         * 中央は**大きく重ねて読む地図**である。指示の 3 つを同時に立てるには:
         *
         * | 指示 | どう満たすか |
         * |---|---|
         * | メインマップのど真ん中 | 中心は `layout.scene` の中心。**動かさない** |
         * | サブパネルに被らない | `free`（板を避けた領域）から出ない |
         * | 大きく出したい | **中心を保ったまま入るところまで大きさを詰める** |
         *
         * 位置を寄せて収めると隅へ張り付く（一度そう書いた）。**動かすのは大きさのほう**
         * である——中心が動かないので「ど真ん中」は保たれ、板にも被らない。
         * サブパネルを閉じている常の形では詰める必要が無いので、指定した大きさで出る。
         */
        const RectPx &view = layout.scene.empty() ? free : layout.scene;
        const int mid_x = view.x + (view.w / 2);
        const int mid_y = view.y + (view.h / 2);
        /*
         * 中心を `mid` に置いたまま `free` に収まる最大の寸法。
         * **狭い側で決まる**（中心が領域の中心から外れているほど小さくなる）。
         */
        const int half_w = std::min(mid_x - free.x, (free.x + free.w) - mid_x);
        const int half_h = std::min(mid_y - free.y, (free.y + free.h) - mid_y);
        const int room_w = (half_w * 2) - (pad * 2);
        const int room_h = (half_h * 2) - (pad * 2);
        //! 縦横比（3:2）は保つ。片方だけ詰めるとマスが歪んで地図として読みにくい。
        int fit = std::min(side, room_w);
        fit = std::min(fit, (room_h * 3) / 2);
        if (fit < (layout.cell_h * 6)) {
            return; // 板を避けたうえで置けるほどの場所が無い（黙って重ねない）
        }
        const int fit_h = (fit * 2) / 3;
        layout.minimap = { mid_x - (fit / 2), mid_y - (fit_h / 2), fit, fit_h };
        break;
    }
    case MinimapCorner::TopRight:
    default:
        layout.minimap = { right, top, side, height };
        break;
    }
    /*
     * **画面中央は狙って置いたもの**なので、ゲージとの重なり回避（下の押し下げ）を掛けない。
     * 掛けると「中央」を選んだのに中央から動く。
     */
    if (corner == MinimapCorner::Centre) {
        return;
    }
    /*
     * **`Full` のゲージと重ならない所まで下げる。**ゲージは左上・ミニマップは右上なので
     * 横長の窓では当たらないが、**縦長の窓では左右の余裕が無く重なる**
     * （1080×1920 で 46×48 画素ぶん重なった。縦持ちを足したときに検査が捕まえた。2026-08-12）。
     * どちらも地図に重ねるのが仕様なので、板どうしの重なりの規則では防げない。
     * 下げきれないほど狭ければ**そのまま**にする（消すより重なるほうがまし）。
     */
    if (layout.hud.empty()) {
        return;
    }
    const bool hits = (layout.minimap.x < (layout.hud.x + layout.hud.w))
        && ((layout.minimap.x + layout.minimap.w) > layout.hud.x)
        && (layout.minimap.y < (layout.hud.y + layout.hud.h))
        && ((layout.minimap.y + layout.minimap.h) > layout.hud.y);
    if (!hits) {
        return;
    }
    const int pushed = layout.hud.y + layout.hud.h + pad;
    if ((pushed + height + pad) <= (free.y + free.h)) {
        layout.minimap.y = pushed;
    }
}

/*!
 * @brief プロンプトの 1 行。空いている所の**下端**へ。
 * @details ミニマップは右上へ移したので、幅を止める必要はもう無い（2026-08-08）。
 */
void place_prompt_bar(UiLayout &layout, const RectPx &free)
{
    if (free.empty()) {
        return;
    }
    const int pad = layout.cell_w;
    const int w = free.w - (pad * 2);
    const int y = free.y + free.h - (layout.cell_h * 2);
    if ((w <= (layout.cell_w * 8)) || (y <= free.y)) {
        return; // 置けないほど狭い
    }
    layout.prompt_bar = { free.x + pad, y, w, layout.cell_h };
}

} // namespace

int SubSplit::bottom() const
{
    return std::clamp(this->bottom_count, 2, kSubBottomMax);
}

int SubSplit::right() const
{
    return std::clamp(this->right_count, 1, kSubRightMax);
}

RectPx panel_body(const RectPx &r, int cell_w)
{
    const int pad = std::max(2, cell_w / 2);
    return RectPx{ r.x + pad, r.y + pad, r.w - (pad * 2), r.h - (pad * 2) };
}

void UiLayout::offset_rects(int dx, int dy)
{
    if ((dx == 0) && (dy == 0)) {
        return;
    }
    /*
     * **1 か所で全部ずらす。**
     *
     * @note **配列は 1 つずつ並べない。**前はサブパネルを `sub[0]`〜`sub[4]` と
     * 手で並べていた。枠が 5 枚から **7 枚**へ増えたとき（2026-08-19）ここを直し忘れ、
     * **右列の下 2 枚だけ切り欠きのぶんずれた**まま半年ちかく残った
     * （2026-08-26 に気づいた「Android だけど、右パネルの下二段が左にズレる」）。
     * 仕切りも 2 本しか並んでおらず、3〜5 本目は掴む場所がずれていた。
     *
     * **ずれるのは切り欠きのある機体だけ**（Windows は `dx` も `dy` も 0）なので、
     * 手元では 1 度も出ない。だから**数えて回す**——枠が増えても勝手に付いてくる。
     */
    const auto shift = [dx, dy](RectPx &r) {
        if (r.empty()) {
            return; // 空の矩形は空のまま（原点だけ動くと `empty()` の意味が濁る）
        }
        r.x += dx;
        r.y += dy;
    };
    RectPx *const singles[] = {
        &this->content, &this->scene, &this->status_col, &this->hud, &this->message_bar, &this->prompt_bar,
        &this->bottom_bar, &this->minimap, &this->minimap_area, &this->term_full, &this->term_overlay,
    };
    for (RectPx *const r : singles) {
        shift(*r);
    }
    for (RectPx &r : this->sub) {
        shift(r);
    }
    for (RectPx &r : this->grip) {
        shift(r);
    }
}

/*!
 * @brief 割り付けを **0 起点で**組む（`UiLayout::compute` の中身）。
 * @details 切り欠きを避けるためのずらしは**入れない**。
 * ここは `Split` の枝で早く `return` するので、**ずらしをこの中に書くと片方だけ通る**
 * （実際そうして Android で上の 96px が空いたまま絵が下へ食み出した。2026-08-12）。
 * ずらすのは呼び出し側の `compute()` 1 か所。
 */
static UiLayout build_layout(LayoutMode mode, int screen_w, int screen_h, int cell_w, int cell_h, bool subs_open,
    const SubSplit &split, bool reserve_pad, MinimapCorner minimap_corner, int minimap_size_pct)
{
    UiLayout layout;
    layout.mode = mode;
    layout.screen_w = std::max(1, screen_w);
    layout.screen_h = std::max(1, screen_h);
    layout.cell_w = std::max(1, cell_w);
    layout.cell_h = std::max(1, cell_h);

    const int w = layout.screen_w;
    const int h = layout.screen_h;
    const int ch = layout.cell_h;

    /*
     * ゲームの絵に使える高さ。**縦持ちの `Tall` でパッドを出しているときだけ**下を空ける
     * （`kTallContentRatio`。参考画像 `VirtualPad_V.jpg` の実測）。横持ちのパッドは
     * 画面いっぱいに広がるので、同じ場所取りをすると地図が理由なく縮む。
     */
    const bool reserve = reserve_pad && (mode == LayoutMode::Tall) && (h > w);
    const int content_h = reserve
        ? clamp_i(static_cast<int>(std::lround(static_cast<double>(h) * kTallContentRatio)), ch * 8, h)
        : h;
    layout.content = { 0, 0, w, content_h };

    /*
     * 最下段の帯。**文字のマスから出す**（既存 UI の 88px は 22pt 前提の数で、
     * この exe のフォントでは行が余る／切れる）。行の内訳は上から
     *   1 行目 … コアの最下行（ステータスバー）
     *   2 行目 … コアの操作の案内 ＋ 右端に UI が横取りしているキー
     *   3〜4 行目 … **コントローラーの全ボタンの割り当て**（2026-08-11 に決めた）
     * 旧 HD2D 版の帯（88px）も「コアの最下行 ＋ 割り当て 2 行」の同じ形だった。
     *
     * **置くのは `content` の下端**（画面の下端ではない）。パッドに場所を譲っているとき、
     * 画面の下端へ置くと帯だけがパッドの下へ潜る。
     */
    const int bar_h = std::min((ch * 4) + (ch / 2), content_h / 4);
    layout.bottom_bar = { 0, content_h - bar_h, w, bar_h };
    const int usable_h = content_h - bar_h;

    /*
     * 全面の写し（タイトル・birth・死亡）は作りに依らない（この画面には 3D が 1 つも無い）。
     * ゲーム中の小窓（`term_overlay`）は**地図の枠が決まってから**置く
     * （`place_term_overlay()`。作りごとの `return` の直前で呼んでいる）。
     */
    layout.term_full = { 0, 0, w, h };

    if (mode == LayoutMode::Split) {
        /* ---- HengbandUi と同じ画面分割（3D は MainMap 矩形の中） ---- */
        layout.sub_overlaid = false;
        int right_w = static_cast<int>(std::lround(static_cast<double>(w) * kRightColRatio));
        right_w = clamp_i(right_w,
            static_cast<int>(std::lround(static_cast<double>(w) * kRightColRatioMin)),
            static_cast<int>(std::lround(static_cast<double>(w) * kRightColRatioMax)));
        const int left_w = w - right_w;
        const int bottom_h = static_cast<int>(std::floor(static_cast<double>(usable_h) * kBottomRowRatio));
        const int main_h = usable_h - bottom_h;

        const int status_w = clamp_i(status_col_width(layout.cell_w, w), 0, left_w / 2);
        /*
         * **状態列の左右**。
         * 右置きは Frox の申告か人の選択で来る。右のときは左ブロックの右端
         * （＝右列の手前）に寄せ、地図をその左に置く。
         */
        if (g_status_col_side == 1) {
            layout.status_col = { left_w - status_w, 0, status_w, main_h };
            layout.scene = { 0, 0, left_w - status_w, main_h };
        } else {
            layout.status_col = { 0, 0, status_w, main_h };
            layout.scene = { status_w, 0, left_w - status_w, main_h };
        }

        place_bottom_subs(layout, 0, left_w, main_h, bottom_h, split);
        place_right_subs(layout, left_w, right_w, 0, usable_h, split);

        /*
         * **メッセージの帯は切らない。**Sub1 が受け持っている（`kind < 0` の既定の中身）ので、
         * 別に帯を切ると同じものが 2 か所に出る。矩形を空にしておけば描く側が Sub1 だけを使う。
         */
        place_minimap(layout, layout.scene, minimap_corner, minimap_size_pct);
        place_prompt_bar(layout, layout.scene);
        place_grips(layout);
        place_term_overlay(layout);
        return layout;
    }

    if (mode == LayoutMode::Tall) {
        /* ---- 縦持ち向け（右の 2 枚が無い画面分割） ---- */
        layout.sub_overlaid = false;
        /*
         * 上下の比も下段 3 枚の配分も **`Split` と同じ数**である（参考画像の実測でも
         * 地図 674 : 下段 335 ＝ 2:1、下段は 25:24:50 だった。偶然ではなく、
         * 利用者が横で使い込んだ配分をそのまま縦へ持ってきているということ）。
         */
        const int bottom_h = static_cast<int>(std::floor(static_cast<double>(usable_h) * kBottomRowRatio));
        const int main_h = usable_h - bottom_h;

        /*
         * 状態列は `Split` と同じ 13 桁（2026-08-12 に決めた「左に状態列を出す」）。
         * **上限だけ緩める**——縦持ちでは画面の 12% が 13 桁に足りず、数字が桁で切れる。
         * 桁から出した幅がそのまま通るようにして、上限は「地図を潰さない」ためだけに置く。
         */
        const int status_w = clamp_i(status_col_width(layout.cell_w, w, 0.28), 0, w / 3);
        //! 左右の振り分けは `Split` と同じ（§6.1）。
        if (g_status_col_side == 1) {
            layout.status_col = { w - status_w, 0, status_w, main_h };
            layout.scene = { 0, 0, w - status_w, main_h };
        } else {
            layout.status_col = { 0, 0, status_w, main_h };
            layout.scene = { status_w, 0, w - status_w, main_h };
        }

        /*
         * 下段は**画面の幅いっぱい**（`Split` は左ブロックの幅だった）。
         * **右列は置かない**（枠は空のまま）——`sub_overlaid == false` と合わせて
         * 「出さない」の意味になる。`Tall` は「右列が無い作り」そのものなので、
         * 右列の枚数の設定はここでは効かない（機能メニューの値の欄でそう言う）。
         */
        place_bottom_subs(layout, 0, w, main_h, bottom_h, split);

        //! メッセージの帯は切らない（`Split` と同じ理由——Sub1 が受け持っている）。
        place_minimap(layout, layout.scene, minimap_corner, minimap_size_pct);
        place_prompt_bar(layout, layout.scene);
        place_grips(layout);
        place_term_overlay(layout);
        return layout;
    }

    /* ---- 全画面 3D（Full / Hybrid） ---- */
    layout.scene = { 0, 0, w, h };
    place_overlaid_subs(layout, subs_open, usable_h, split);
    place_grips(layout);

    /*
     * 常時出るもの（メッセージ・状態列・ミニマップ）は、**開いた板の外側**に置く。
     * 「どうせ上から描くから」で重ねると、サブパネルを開いた瞬間に
     * メッセージとミニマップが下敷きへ消える。
     */
    const int content_bottom = subs_open ? layout.sub[kSubBottomSlot].y : usable_h;
    const int right_edge = subs_open ? layout.sub[kSubRightSlot].x : w;
    /*
     * **サブパネルを開いている間はメッセージの帯を出さない。**Sub1 が同じものを出すので、
     * 開いた瞬間に同じメッセージが 2 か所へ並ぶ（1 度そうなった絵を撮って気づいた）。
     */
    const int msg_h = subs_open ? 0 : std::min(ch * 3, std::max(ch, content_bottom / 3));
    if (msg_h > 0) {
        layout.message_bar = { layout.cell_w, content_bottom - msg_h, right_edge - (layout.cell_w * 2), msg_h };
    }

    int free_x = 0;
    if (mode == LayoutMode::Hybrid) {
        const int status_w = status_col_width(layout.cell_w, w);
        //! 左右の振り分け（§6.1）。右のときは右端（開いた右列の手前）に寄せる。
        if (g_status_col_side == 1) {
            layout.status_col = { right_edge - status_w, 0, status_w, content_bottom - msg_h };
        } else {
            layout.status_col = { 0, 0, status_w, content_bottom - msg_h };
            free_x = status_w;
        }
    } else {
        // Full: 状態列の代わりにゲージ。左上へ横に短く置く。
        layout.hud = { layout.cell_w, layout.cell_h / 2, std::min(w / 3, layout.cell_w * 34), ch * 3 };
    }
    /*
     * 右置きのときは自由領域（ミニマップ・プロンプトの置き場）を状態列の手前で
     * 止める——止めないと TopRight のミニマップが状態列へ重なる（§6.1）。
     */
    const int free_right = ((mode == LayoutMode::Hybrid) && (g_status_col_side == 1) && !layout.status_col.empty())
        ? layout.status_col.x
        : right_edge;
    const RectPx free{ free_x, 0, free_right - free_x, content_bottom - msg_h };
    place_minimap(layout, free, minimap_corner, minimap_size_pct);
    place_prompt_bar(layout, free);
    /*
     * **重なる作りでは `scene` が窓そのもの**なので、`place_term_overlay()` は
     * 「枠に収める」枝を通っても窓の真ん中に 80×24 の板を置くだけになる。
     * それでよい（従来の 85%×90% より小さく、中身は 1 桁も切れない）。
     */
    place_term_overlay(layout);
    return layout;
}

UiLayout UiLayout::compute(LayoutMode mode, int screen_w, int screen_h, int cell_w, int cell_h, bool subs_open,
    const SubSplit &split, int origin_x, int origin_y, bool reserve_pad, bool minimap_bottom,
    MinimapCorner minimap_corner, int minimap_size_pct)
{
    UiLayout layout = build_layout(mode, screen_w, screen_h, cell_w, cell_h, subs_open, split,
        reserve_pad, minimap_corner, minimap_size_pct);
    /*
     * ---- HP/MP の棒は**全部の作りで画面の左下**（2026-08-14 に決めた）----
     *
     * もとは `Full` の左上にだけ置いていた。**作りごとに置き場所を決めると、
     * 作りを切り替えるたびに目が探し直す**ことになるので、どの作りでも同じ所に出す。
     * 地図（`scene`）の中の左下に重ねる——`Split` の状態列にも数字はあるが、
     * 棒は「減ったことに一目で気づく」ためのものなので別物である。
     */
    {
        const int gauge_w = std::min(layout.scene.w - (layout.cell_w * 2), layout.cell_w * 30);
        const int gauge_h = layout.cell_h * 3;
        /*
         * **下端は最下段の帯より上**。`Full` / `Hybrid` では地図が窓いっぱいなので、
         * 地図の下端に置くと帯の下へ潜る（`--ui-check` が捕まえた）。
         * メッセージの帯とプロンプトも同じ理由で避ける——どちらも地図の下側に出る。
         */
        int bottom = layout.scene.y + layout.scene.h;
        if (!layout.bottom_bar.empty()) {
            bottom = std::min(bottom, layout.bottom_bar.y);
        }
        if (!layout.message_bar.empty()) {
            bottom = std::min(bottom, layout.message_bar.y);
        }
        if (!layout.prompt_bar.empty()) {
            bottom = std::min(bottom, layout.prompt_bar.y);
        }
        /*
         * 左端は**状態列の右**から（`Hybrid` は地図が窓いっぱいで、左に状態列が
         * 重なっている）。状態列にも数字は出るが、棒はその外に出す。
         */
        int left = layout.scene.x + layout.cell_w;
        /*
         * 避けるのは**状態列が左にあるとき**だけ（§6.1）。右置き（Frox）の列を
         * 同じ式で避けると `left` が地図の右端を越え、棒そのものが出なくなる。
         */
        if (!layout.status_col.empty() && (layout.status_col.x <= layout.scene.x)) {
            left = std::max(left, layout.status_col.x + layout.status_col.w + layout.cell_w);
        }
        const int width = std::min(gauge_w, (layout.scene.x + layout.scene.w) - left - layout.cell_w);
        const int y = bottom - gauge_h - (layout.cell_h / 2);
        if ((width > (layout.cell_w * 10)) && (y > (layout.scene.y + layout.cell_h))) {
            layout.hud = { left, y, width, gauge_h };
        }
    }
    /*
     * ---- VR ではミニマップを**右下**へ（2026-08-14 に決めた）----
     * 平らな画面では右上（3D の邪魔になりにくい）だが、VR の板は見上げる位置にあるので、
     * 上に置くと**顎を上げないと読めない**。下端は HP/MP の棒と同じ理由で下に寄せる。
     */
    if (minimap_bottom && !layout.minimap.empty()) {
        const int bottom = (layout.scene.y + layout.scene.h) - layout.cell_h;
        layout.minimap.y = std::max(layout.scene.y, bottom - layout.minimap.h);
    }
    layout.origin_x = origin_x;
    layout.origin_y = origin_y;
    /*
     * **ずらすのはここだけ。**`build_layout` は作りごとに `return` が分かれているので、
     * あちらへ書くと必ず片方を通らない道ができる（`Split` で実際に踏んだ）。
     */
    layout.offset_rects(origin_x, origin_y);
    return layout;
}

void set_term_mirror_rows(int rows)
{
    //! 上限は 255（`term` の `hgt` が byte）。下限は 24（それ未満のコアは無い）。
    g_term_mirror_rows = (rows >= kTermMirrorRows) ? ((rows <= 255) ? rows : 255) : kTermMirrorRows;
}

int term_mirror_rows()
{
    return g_term_mirror_rows;
}

void set_status_col_cols(int cols)
{
    g_status_col_cols = (cols > 0) ? cols : 0;
}

int status_col_cols()
{
    return g_status_col_cols;
}

void set_status_col_side(int side)
{
    g_status_col_side = (side == 1) ? 1 : 0;
}

int status_col_side()
{
    return g_status_col_side;
}

bool ui_break_enabled(const char *what)
{
    static const std::string spec = [] {
        const char *const value = std::getenv("HD2D_BREAK_UI");
        return std::string((value != nullptr) ? value : "");
    }();
    if (spec.empty() || (spec == "0")) {
        return false;
    }
    if ((spec == "1") || (spec == "all")) {
        return true;
    }
    return spec.find(what) != std::string::npos;
}

bool parse_layout_mode(const std::string &spec, LayoutMode &out)
{
    if ((spec == "full") || (spec == "3d")) {
        out = LayoutMode::Full;
        return true;
    }
    if ((spec == "hybrid") || (spec == "mix")) {
        out = LayoutMode::Hybrid;
        return true;
    }
    if ((spec == "split") || (spec == "panels") || (spec == "ui")) {
        out = LayoutMode::Split;
        return true;
    }
    //! 綴りは 3 つ受ける（cfg に書くのは `tall`。人が打つときの言い方に幅がある）。
    if ((spec == "tall") || (spec == "portrait") || (spec == "noright")) {
        out = LayoutMode::Tall;
        return true;
    }
    return false;
}

const char *layout_mode_name(LayoutMode mode)
{
    switch (mode) {
    case LayoutMode::Full:
        return "full";
    case LayoutMode::Split:
        return "split";
    case LayoutMode::Tall:
        return "tall";
    case LayoutMode::Hybrid:
    default:
        return "hybrid";
    }
}

} // namespace hd2d
