/*!
 * @file fc_frame.cpp
 * @brief `fc_frame.h` の実装。
 *
 * ## 文字コード（M0）
 * Frox は純 ASCII なので**変換が無い**。行の組み立ては「セルのバイトを並べて
 * 制御文字だけ空白に倒す」で終わる。M1 で CP932 が入ったら、ここの
 * `row_to_utf8()` / `span_to_line()` / `row_to_runs()` に変換を 1 度だけ通す
 * （設計 §3.1・§12 の 2。**経路をここへ集めてあるのはそのため**）。
 *
 * ## 色 span
 * 1 行 1 色に落とすと 1 行に何色も出る画面が潰れる（変愚で実際に潰れた）。
 * ここでは属性の変わり目ごとに区間を切り、行全体を隙間なく覆う。
 */

#include "fc_frame.h"

#include "fc_text.h" //!< CP932 → UTF-8（設計 §3.1）

#include "fc_lang_c.h" //!< 自由入力の旗（追補 A1。フック #9）
#include "fc_manifest.h"
#include "fc_menu.h"
#include "fc_shim.h"
#include "fc_sub_panels.h"

#include "frame/cell_feature_bits.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fc {

namespace {

/* ============================================================ 画面側からの申告 */

//! 視界の既定。`ui_state` が来るまでのぶん（HD2D は必ず送ってくる）。
int g_view_w = 47;
int g_view_h = 22;
//! 視界の上限。桁違いの値が来ても走査量が爆発しないようにする。
constexpr int kMaxView = 200;
bool g_camera_follow_player = false;
const TileManifest *g_manifest = nullptr;
//! 画面側の `ui_state.cursor_mode`。**既定は真**（検査だけカーソルの無い世界にしない）。
bool g_cursor_mode = true;

//! 主 Term の最大寸法。`fc_term_row` の受け皿の大きさを決めるだけの上限。
constexpr int kMaxCols = 256;
//! 画面側へ運ぶメッセージの本数。変愚の bridge と同じ 8。
constexpr int kMessageLines = 8;
//! 1 本のメッセージの上限。
constexpr int kMessageBytes = 1024;

/*!
 * @name Frox の版面（`frox/src/xtra2.c` の `ui_*_rect()`。実読 2026-08-24）
 * @details
 * - `ui_char_info_rect()` = `rect(wid-12, 1, 12, hgt-1)` … **状態列は右端 12 桁**
 * - `ui_map_rect()` = `rect(0, 1, wid-13, hgt-2)` … 地図は左
 * - `ui_status_bar_rect()` = 最下行。**`prt_depth()` が行の頭に書き**、
 *   状態バーがその右（`_depth_width` から）に続く——幻想蛮怒とは左右が逆
 * @{
 */
constexpr int kStatusColCells = 12;
/*! @} */

/*!
 * @brief セル 1 個ぶんを空白とみなすか（地図のタイル印 `0x7F` が紛れ込む穴を塞ぐ）。
 * @note **0x80 以上は空白ではない**（設計 §3.2）。CP932 の 2 バイト文字のバイトなので、
 * ここで空白に倒すと名前がまるごと消える。`0x7F` だけを弾く。
 */
bool cell_is_blank(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u == ' ') || (u < 0x20) || (u == 0x7F);
}

/*!
 * @brief コアの字（CP932）→ 線の字（UTF-8）。
 * @details **純 ASCII なら 1 バイトも触らない**（設計 §1 制約 1）——`--lang` 無しの
 * 画を M0 と 1 バイトも変えないための決めである。変換できなければ**落とさずに
 * そのまま運ぶ**（消えるより化けたほうが調べやすい）。
 */
std::string to_utf8(const char *bytes, std::size_t len)
{
    if ((bytes == nullptr) || (len == 0)) {
        return std::string();
    }
    bool wide = false;
    for (std::size_t i = 0; i < len; ++i) {
        if (static_cast<unsigned char>(bytes[i]) >= 0x80) {
            wide = true;
            break;
        }
    }
    if (!wide) {
        return std::string(bytes, len);
    }
    std::string out = fc::sjis_to_utf8(bytes, len);
    if (out.empty()) {
        out.assign(bytes, len);
    }
    return out;
}

inline std::string to_utf8(const char *z) { return to_utf8(z, (z != nullptr) ? std::strlen(z) : 0); }

/*!
 * @brief セル 1 行（CP932 のバイト＋属性）→ 1 行の UTF-8 ＋色 span。
 * @param out_spans 色 span（**UTF-8 のバイト位置**。行全体を隙間なく覆う）
 *
 * @details 設計 §3.2 の 1 か所目。**色 span はバイト位置で持っている**ので、
 * 2 セルが 1 文字（さらに UTF-8 では 3 バイト）になると境界が動く。
 * 塊ごとに変換して、変換後の長さで span を積み直す。
 *
 * 英語のときは走る道が M0 と同じである——0x80 以上が 1 つも無ければ
 * `sjis_to_utf8()` を呼ばずに素のバイトを返す（制約 1）。
 */
std::string row_to_utf8(const char *cells, const unsigned char *attrs, int count,
    std::vector<TermColorSpan> &out_spans)
{
    out_spans.clear();

    /*
     * 末尾の空白は色ごと落とす（27 行 × 毎フレームの運び賃。v1 §10）。
     * 右から見ても 2 バイト文字を切らない: CP932 の後続バイトは 0x40〜0x7E と
     * 0x80〜0xFC で、**空白（0x20）にも制御文字にもならない**。
     */
    int end = count;
    while ((end > 0) && (cells[end - 1] == ' ')) {
        --end;
    }
    if (end <= 0) {
        return std::string();
    }

    std::string text;
    int x = 0;
    while (x < end) {
        const unsigned char attr = attrs[x];

        /* 同じ属性が続くあいだを 1 塊にする。2 バイト文字は 2 セルまとめて取る。 */
        std::string chunk;
        bool wide = false;
        while (x < end) {
            if (attrs[x] != attr) {
                break;
            }
            const auto lead = static_cast<unsigned char>(cells[x]);
            if (fc::is_sjis_lead(lead) && ((x + 1) < end)) {
                /*
                 * 2 セル 1 文字。**属性は先行バイト側を採る**——後続バイトの
                 * セルにも同じ属性が入っているのが普通だが、違っていても
                 * 文字を割ってはいけない。
                 */
                chunk.push_back(cells[x]);
                chunk.push_back(cells[x + 1]);
                x += 2;
                wide = true;
                continue;
            }
            if (fc::is_sjis_lead(lead)) {
                chunk.push_back(' '); // 区画の端で割れた。半端なバイトは運ばない
                x += 1;
                continue;
            }
            chunk.push_back(cell_is_blank(cells[x]) ? ' ' : static_cast<char>(lead));
            x += 1;
        }

        std::string piece = chunk;
        if (wide) {
            piece = fc::sjis_to_utf8(chunk);
            if (piece.empty() && !chunk.empty()) {
                /* 変換できない塊（壊れた符号）。**行を落とさず**空白で埋める。 */
                piece.assign(chunk.size(), ' ');
            }
        }

        TermColorSpan span;
        span.begin = static_cast<int>(text.size());
        span.len = static_cast<int>(piece.size());
        span.color = attr;
        if (span.len > 0) {
            out_spans.push_back(span);
        }
        text += piece;
    }

    return text;
}

/*!
 * @brief **地図を消さずに重ねただけのメニュー**なら、写しを箱の行だけへ刈り込む。
 * @return 刈り込んだ（＝重ねのメニューだった）か
 *
 * @details `util.c` の `inkey_from_menu()` は `screen_save()` で `character_icky` を
 * 上げてから、**地図の上へ `+----` の箱を `put_str` するだけ**である。写しをそのまま
 * 積むと 80×24 に地図が載ったまま画面へ出て、立体の代わりに字の地図が見える
 * （実機で見つけた（2026-09-01）「menu と、タイトル画面にコアのマップが入り込んでる」）。
 *
 * 箱の見つけ方は `fc_menu.cpp` の `fill_menu_choices()` と**同じ規則**にする
 * ——`+----` の行が 2 本以上あることだけを見る。ずらすと、箱の中の選択肢を拾う側と
 * 刈り込む側で食い違って、選べない項目が出る。
 *
 * @note **持ち物・店・一覧は刈り込まない。** あれらはコアが地図を消してから描くので、
 * 写しは全面のままでよい（箱の行が無いのでここは偽を返す）。
 */
//! 重ねのメニューの箱 1 つ（Term の行と桁）。
struct MenuBox {
    int top{ 0 };
    int bottom{ 0 };
    int left{ 0 };
    int right{ 0 };
};

/*!
 * @brief Term から `+----+` の箱を**全部**見つける。
 * @details 小メニューへ入ると箱が 2 つになり、**位置がずれる**（親は 13〜19 行、
 * 小メニューは 15〜21 行で右へも下へもはみ出す。2026-09-02 に実測）。
 * 1 つ目の箱の桁で切ると小メニューの右が落ち、隙間に地図が残る。
 *
 * **桁は生のセルから数える**——写しは UTF-8 で、CP932 でも 1 バイト＝1 桁だからここで採る。
 * 同じ左右を持つ行どうしを 1 つの箱にまとめる（上辺と下辺）。
 */
static bool find_menu_boxes(std::vector<MenuBox> &out)
{
    int cols = 0;
    int rows = 0;
    if (fc_term_size(&cols, &rows) != FC_OK) {
        return false;
    }
    char cells[kMaxCols];
    unsigned char attrs[kMaxCols];
    /*
     * **左の桁で束ねる。**`(左, 右)` の対で束ねると、下辺が別の箱に上書きされている
     * ときに別物と見なされ、箱が 1 行に潰れる（小メニューを開くと親の下辺が
     * 隠れる。2026-09-02 に実測して踏んだ）。
     */
    for (int y = 0; y < rows; ++y) {
        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int n = fc_term_row(y, cells, attrs, kMaxCols);
        if (n <= 0) {
            continue;
        }
        for (int x = 0; (x + 2) < n; ++x) {
            if ((cells[x] != '+') || (cells[x + 1] != '-') || (cells[x + 2] != '-')) {
                continue;
            }
            bool merged = false;
            for (auto &box : out) {
                if (box.left == x) {
                    box.bottom = y;
                    merged = true;
                    break;
                }
            }
            if (merged) {
                continue;
            }
            /*
             * **右端は上辺から採る。**下辺は別の箱に上書きされていることがあるが、
             * 上辺は必ず塞がれていない（後から開く箱は下へずれる）。
             */
            int right = -1;
            for (int rx = x + 1; rx < n; ++rx) {
                if (cells[rx] == '+') {
                    right = rx;
                    break;
                }
                if ((cells[rx] != '-') && (cells[rx] != ' ')) {
                    break; //!< 罫が途切れた。ここは箱の上辺ではない
                }
            }
            if (right > x) {
                out.push_back(MenuBox{ y, y, x, right });
            }
        }
    }
    return !out.empty();
}

//! CP932 の 2 バイト字の頭か。**桁を潰すときは 2 桁まとめて**潰す（半分だけ消すと化ける）。
static bool cp932_lead(unsigned char c)
{
    return ((c >= 0x81u) && (c <= 0x9Fu)) || ((c >= 0xE0u) && (c <= 0xFCu));
}

static bool crop_mirror_to_menu_box(GameFrame &frame)
{
    std::vector<MenuBox> boxes;
    if (!find_menu_boxes(boxes)) {
        return false;
    }
    //! 箱をまとめて囲む矩形。**箱が 2 つ以上でも右と下を落とさない。**
    int top = boxes.front().top;
    int bottom = boxes.front().bottom;
    int left = boxes.front().left;
    int right = boxes.front().right;
    for (const auto &box : boxes) {
        top = (std::min)(top, box.top);
        bottom = (std::max)(bottom, box.bottom);
        left = (std::min)(left, box.left);
        right = (std::max)(right, box.right);
    }
    if ((bottom <= top) || (right <= left)) {
        return false;
    }
    const int width = (right - left) + 1;

    char cells[kMaxCols];
    unsigned char attrs[kMaxCols];
    std::vector<TermMirrorLine> box_lines;
    box_lines.reserve(static_cast<std::size_t>((bottom - top) + 1));
    for (int y = top; y <= bottom; ++y) {
        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int got = fc_term_row(y, cells, attrs, kMaxCols);
        if (got <= left) {
            continue;
        }
        /*
         * **どの箱にも入らないマスは空白にする。** 箱がずれて重なると、
         * 囲む矩形の隅に地図が残る（`#~~.....` が出ていた）。
         * 2 バイト字は**2 桁まとめて**潰す——半分だけ消すと化ける。
         */
        for (int x = left; (x <= right) && (x < got); ++x) {
            bool inside = false;
            for (const auto &box : boxes) {
                if ((y >= box.top) && (y <= box.bottom) && (x >= box.left) && (x <= box.right)) {
                    inside = true;
                    break;
                }
            }
            if (inside) {
                if (cp932_lead(static_cast<unsigned char>(cells[x]))) {
                    ++x; //!< 頭が生きているなら尾も生かす
                }
                continue;
            }
            if (cp932_lead(static_cast<unsigned char>(cells[x])) && ((x + 1) < got)) {
                cells[x + 1] = ' ';
                attrs[x + 1] = 1;
            }
            cells[x] = ' ';
            attrs[x] = 1;
        }
        const int take = (std::min)(width, got - left);
        TermMirrorLine line;
        line.source_row = y;
        line.text_utf8 = row_to_utf8(cells + left, attrs + left, take, line.color_spans);
        line.color = line.color_spans.empty() ? 1 : line.color_spans.front().color;
        box_lines.push_back(std::move(line));
    }
    if (box_lines.size() < 2) {
        return false;
    }
    frame.menu_term_lines.swap(box_lines);
    /*
     * **カーソルも一緒にずらす。** `menu_term_curs_row` は Term の行番号ではなく
     * `menu_term_lines` の**添字**で、`fill_term_mirror()` は「捨てるようにしたら
     * 必ずここも直す」と書いてある——ここがその「必ず」である。
     * 囲む矩形の外に居たカーソルは**消す**（-1）。写しに無い所を指させない。
     */
    if (frame.menu_term_curs_row >= 0) {
        const int row = frame.menu_term_curs_row;
        const int col = frame.menu_term_curs_col;
        if ((row >= top) && (row <= bottom) && (col >= left) && (col <= right)) {
            frame.menu_term_curs_row = row - top;
            frame.menu_term_curs_col = col - left;
        } else {
            frame.menu_term_curs_col = -1;
            frame.menu_term_curs_row = -1;
        }
    }
    return true;
}

void fill_term_mirror(GameFrame &frame)
{
    int cols = 0;
    int rows = 0;
    if (fc_term_size(&cols, &rows) != FC_OK) {
        return;
    }
    cols = (std::min)(cols, kMaxCols - 1);

    char cells[kMaxCols];
    unsigned char attrs[kMaxCols];

    frame.menu_term_lines.clear();
    frame.menu_term_lines.reserve(static_cast<std::size_t>(rows));

    for (int y = 0; y < rows; ++y) {
        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int n = fc_term_row(y, cells, attrs, kMaxCols);
        if (n < 0) {
            continue;
        }

        TermMirrorLine line;
        line.source_row = y;
        line.text_utf8 = row_to_utf8(cells, attrs, n, line.color_spans);
        /* `color` は `color_spans` が空のときだけ使われる。 */
        line.color = line.color_spans.empty() ? 1 : line.color_spans.front().color;
        frame.menu_term_lines.push_back(std::move(line));
    }

    /*
     * カーソル。**`menu_term_curs_row` は `menu_term_lines` の添字**であって
     * Term の行番号ではない。ここは空行も捨てずに全行を積んでいるので一致する。
     * 捨てるようにしたら**必ずここも直す**。
     * @note 2026-09-01 に `crop_mirror_to_menu_box()` が捨てるようになったので、
     *       あちらで添字をずらしている。**刈り込みを増やすときは同じ手当てを忘れない。**
     */
    int cx = 0;
    int cy = 0;
    int visible = 0;
    if ((fc_term_cursor(&cx, &cy, &visible) == FC_OK) && visible && (cy >= 0)
        && (cy < static_cast<int>(frame.menu_term_lines.size()))) {
        frame.menu_term_curs_col = cx;
        frame.menu_term_curs_row = cy;
    } else {
        frame.menu_term_curs_col = -1;
        frame.menu_term_curs_row = -1;
    }
}

/* ============================================= 右の状態列と最下段（設計 §4.3） */

/*!
 * @name 直前に読めた値
 * @details `redraw_stuff()` は `character_icky` の間フレームを更新しない。
 * 店や一覧を開いている間の Term は本文で塗り潰されているので、
 * そのときは直前に読めた値を使い回す（幻想蛮怒と同じ手）。
 * @{
 */
std::vector<SubPanelLine> g_status_col_cache;
std::vector<TermTextRun> g_bottom_row_cache;
SubPanelLine g_depth_cache;
/*! @} */

/*!
 * @brief 桁 `[x0, x1)` を 1 行にする。
 * @param trim_left 行頭の空白も落とすか。**状態列では落とさない**——
 * 項目が決まった桁に並ぶので、左を詰めると位置の意味が崩れる。
 */
SubPanelLine span_to_line(const char *cells, const unsigned char *attrs, int n, int x0, int x1,
    bool trim_left)
{
    SubPanelLine out;
    const int begin = (std::max)(0, x0);
    int end = (std::min)(x1, n);

    while ((end > begin) && cell_is_blank(cells[end - 1])) {
        --end;
    }
    if (end <= begin) {
        return out;
    }

    std::string sys; // CP932 のまま組み、最後に 1 回だけ変換する（設計 §3.2）
    bool color_found = false;
    bool wide = false;
    for (int x = begin; x < end; ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        if (!color_found && !cell_is_blank(cells[x])) {
            out.color = attrs[x];
            color_found = true;
        }
        if (fc::is_sjis_lead(lead) && ((x + 1) < end)) {
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            wide = true;
            ++x;
            continue;
        }
        if (fc::is_sjis_lead(lead)) {
            sys.push_back(' '); // 区画の端で割れた。**半端なバイトは運ばない**
            continue;
        }
        sys.push_back(cell_is_blank(cells[x]) ? ' ' : cells[x]);
    }

    if (trim_left) {
        const std::size_t head = sys.find_first_not_of(' ');
        sys = (head == std::string::npos) ? std::string() : sys.substr(head);
        while (!sys.empty() && (sys.back() == ' ')) {
            sys.pop_back();
        }
    }

    if (!wide) {
        out.text_utf8 = std::move(sys); // 純 ASCII。**1 バイトも触らない**
        return out;
    }
    out.text_utf8 = fc::sjis_to_utf8(sys);
    if (out.text_utf8.empty() && !sys.empty()) {
        out.text_utf8.assign(sys.size(), ' ');
    }
    return out;
}

/*!
 * @brief 1 行を**同じ色が続く区間ごと**に切って桁とともに積む（最下段の状態バー）。
 * @param x0 ここから左は積まない（階層は右下へ出すので二重にしない——ただし
 *           **Frox の階層は行の頭**なので、こちらは「頭を除く」向きで使う）
 */
void row_to_runs(const char *cells, const unsigned char *attrs, int n, int x0, int x1,
    std::vector<TermTextRun> &out)
{
    out.clear();
    const int end = (std::min)(n, x1);

    std::string sys; // CP932 のまま溜め、run を閉じるときに 1 回だけ変換する
    bool wide = false;
    int run_col = 0;
    unsigned char run_color = 1;

    const auto flush = [&]() {
        while (!sys.empty() && (sys.back() == ' ')) {
            sys.pop_back();
        }
        if (sys.empty()) {
            wide = false;
            return;
        }
        TermTextRun run;
        run.col = run_col;
        run.color = run_color;
        if (wide) {
            run.text_utf8 = fc::sjis_to_utf8(sys);
            if (run.text_utf8.empty()) {
                run.text_utf8.assign(sys.size(), ' ');
            }
        } else {
            run.text_utf8 = sys; // 純 ASCII。**1 バイトも触らない**
        }
        out.push_back(std::move(run));
        sys.clear();
        wide = false;
    };

    for (int x = (std::max)(0, x0); x < end; ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        const unsigned char color = attrs[x];

        if (cell_is_blank(cells[x])) {
            /* 空白 2 桁以上で区切る（1 桁の空白は語の間なので run に含める）。 */
            if (!sys.empty() && ((x + 1) < end) && cell_is_blank(cells[x + 1])) {
                flush();
                continue;
            }
            if (!sys.empty()) {
                sys.push_back(' ');
            }
            continue;
        }

        if (sys.empty()) {
            run_col = x;
            run_color = color;
        } else if (color != run_color) {
            flush();
            run_col = x;
            run_color = color;
        }

        if (fc::is_sjis_lead(lead) && ((x + 1) < end)) {
            /* 2 セル 1 文字。**属性は先行バイト側**を採る（文字を割らない）。 */
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            wide = true;
            ++x;
            continue;
        }
        if (fc::is_sjis_lead(lead)) {
            break; // 区画の端で割れた。半端なバイトは運ばない
        }
        sys.push_back(cells[x]);
    }
    flush();
}

/*!
 * @brief 右の状態列・最下段・階層をまとめて写す。
 * @details 読む相手の判断（icky なら直前の値）が同じなので 1 か所に置く。
 *
 * **状態列は右端 12 桁**（`ui_char_info_rect()`）で、`status_col_side = 1` を
 * 申告する（設計 §4.3。**決めたこと F2 の土台**——画面はこの申告を「自動」の
 * 答えに使い、左右の選択は画面側の設定が勝つ）。
 *
 * **最下段は左右が幻想蛮怒と逆**——`prt_depth()` が行の頭に
 * `Angband: L5` の形で書き、状態バーがその右に続く。だから階層は
 * 「行 0 桁目から始まる最初の塊」を取り出し、残りを帯へ積む。
 */
void fill_status_col(GameFrame &frame)
{
    frame.status_col_lines.clear();
    //! 桁数と側は**コアが申告する**（画面側に数を持たせない。設計 §1 制約 3）。
    frame.status_col_cols = kStatusColCells;
    frame.status_col_side = 1; //!< 右（設計 §4.3。0 = 左が既定なので明示する）
    frame.bottom_row_runs.clear();
    frame.bottom_row_cols = 0;
    frame.depth = SubPanelLine{};

    const int screen = fc_screen_flags();
    if (((screen & FC_SCREEN_GENERATED) == 0) || ((screen & FC_SCREEN_DUNGEON) == 0)
        || ((screen & FC_SCREEN_DEAD) != 0)) {
        /* @ がまだ無い／もう無い。Term の右端は本文なので読まない。 */
        g_status_col_cache.clear();
        g_bottom_row_cache.clear();
        g_depth_cache = SubPanelLine{};
        return;
    }

    int cols = 0;
    int rows = 0;
    if (fc_term_size(&cols, &rows) != FC_OK) {
        return;
    }
    cols = (std::min)(cols, kMaxCols - 1);
    if ((cols <= kStatusColCells) || (rows <= 2)) {
        return;
    }
    frame.bottom_row_cols = cols;

    if ((screen & (FC_SCREEN_ICKY | FC_SCREEN_XTRA)) == 0) {
        char cells[kMaxCols];
        unsigned char attrs[kMaxCols];

        /* 状態列 = 右端 12 桁・行 1〜rows-2（行 0 はメッセージ・最下行は状態バー）。 */
        std::vector<SubPanelLine> lines;
        for (int y = 1; y <= (rows - 2); ++y) {
            std::memset(cells, ' ', sizeof(cells));
            std::memset(attrs, 1, sizeof(attrs));
            const int n = fc_term_row(y, cells, attrs, kMaxCols);
            if (n < 0) {
                continue;
            }
            lines.push_back(span_to_line(cells, attrs, n, cols - kStatusColCells, cols, false));
        }
        while (!lines.empty() && lines.back().text_utf8.empty()) {
            lines.pop_back();
        }
        frame.status_col_lines = std::move(lines);

        /* 最下行。頭の塊が階層（`prt_depth()`）、その右が状態バー。 */
        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int n = fc_term_row(rows - 1, cells, attrs, kMaxCols);
        if (n > 0) {
            std::vector<TermTextRun> runs;
            row_to_runs(cells, attrs, n, 0, cols, runs);
            if (!runs.empty() && (runs.front().col == 0)
                && (static_cast<int>(runs.front().text_utf8.size()) < (cols / 2))) {
                /*
                 * 頭の塊を階層として抜く。半分より長い塊は階層ではない場面の守り
                 * （実測 2026-08-24: 広域マップの案内行は色替わりで塊が切れるので、
                 * 実際に抜かれるのは見出しの `World Map:` だけ。案内の残りは
                 * ちゃんと帯へ流れる——FH-10 はこの確認で閉じた）。
                 */
                frame.depth.text_utf8 = runs.front().text_utf8;
                frame.depth.color = runs.front().color;
                /* 見出しの尻の `:` は落とす（`World Map:` -> `World Map`。
                 * `Angband: L5` のような中の `:` はそのまま）。 */
                if (!frame.depth.text_utf8.empty() && (frame.depth.text_utf8.back() == ':')) {
                    frame.depth.text_utf8.pop_back();
                }
                runs.erase(runs.begin());
            }
            frame.bottom_row_runs = std::move(runs);
        }

        g_status_col_cache = frame.status_col_lines;
        g_bottom_row_cache = frame.bottom_row_runs;
        g_depth_cache = frame.depth;
        return;
    }

    frame.status_col_lines = g_status_col_cache;
    frame.bottom_row_runs = g_bottom_row_cache;
    frame.depth = g_depth_cache;
}

void fill_hud(GameFrame &frame)
{
    fc_hud_data data{};
    if (fc_read_hud(&data) != FC_OK) {
        return;
    }

    frame.hud.name = to_utf8(data.name);
    frame.hud.hp = data.hp;
    frame.hud.hp_max = data.hp_max;
    frame.hud.sp = data.sp;
    frame.hud.sp_max = data.sp_max;
    frame.hud.gold = static_cast<int>(data.gold);
    frame.hud.level = data.level;
    frame.hud.depth = data.depth;

    /* `status_line` は変愚の `fill_hud` と**同じ形**にする（画面側が同じ読み方をする）。 */
    char status[128];
    std::snprintf(status, sizeof(status), "HP %d/%d  SP %d/%d  LEV %d",
        frame.hud.hp, frame.hud.hp_max, frame.hud.sp, frame.hud.sp_max, frame.hud.level);
    frame.hud.status_line = status;

    /* 現在地と階層は**コアの文言のまま**運ぶ（`prt_depth()` と同じ字面）。 */
    frame.hud.right_top_lines.clear();
    if (data.place[0] != '\0') {
        frame.hud.right_top_lines.push_back(to_utf8(data.place));
    }
    if (data.depth_text[0] != '\0') {
        frame.hud.right_top_lines.push_back(to_utf8(data.depth_text));
    }
}

void fill_messages(GameFrame &frame)
{
    const int num = fc_message_count();
    const int take = (std::min)(kMessageLines, (num > 0) ? num : 0);

    frame.messages.clear();
    frame.messages.reserve(static_cast<std::size_t>(take));

    char buf[kMessageBytes];
    for (int age = take - 1; age >= 0; --age) {
        const int n = fc_message_text(age, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            continue;
        }
        MessageEvent ev{};
        ev.seq = static_cast<uint32_t>(frame.messages.size());
        ev.color = 1;
        ev.text_utf8 = to_utf8(buf, static_cast<std::size_t>(n));
        frame.messages.push_back(std::move(ev));
    }
}

/*!
 * @brief コアの色表を `frame.term_palette` へ。
 * @details 読めなかったときは**既定のまま置く**（嘘の色で塗るより前と同じ絵のほうが害が小さい）。
 */
void fill_term_palette(GameFrame &frame)
{
    unsigned char rgb[16 * 3] = { 0 };
    if (fc_read_palette(rgb, static_cast<int>(sizeof(rgb))) < 0) {
        return;
    }
    for (std::size_t i = 0; i < frame.term_palette.rgb.size(); ++i) {
        frame.term_palette.rgb[i][0] = rgb[i * 3 + 0];
        frame.term_palette.rgb[i][1] = rgb[i * 3 + 1];
        frame.term_palette.rgb[i][2] = rgb[i * 3 + 2];
    }
}

/* ==================================================================== 地図 */

/*!
 * @brief `FC_FEAT_*`（C の境界）→ `CELL_FEAT_*`（画面側）。
 * @details **表をここ 1 か所に置く。** 値を揃えて素通しにすると、どちらかの並びが
 * 変わったときに黙って別の意味になる（壁が扉になる類の壊れ方）。
 */
uint16_t translate_feature_flags(unsigned short fc_bits)
{
    static const struct {
        unsigned short from;
        uint16_t to;
    } kTable[] = {
        { FC_FEAT_WALL, CELL_FEAT_WALL },
        { FC_FEAT_DOOR, CELL_FEAT_DOOR },
        { FC_FEAT_DOOR_OPEN, CELL_FEAT_DOOR_OPEN },
        { FC_FEAT_STAIRS, CELL_FEAT_STAIRS },
        { FC_FEAT_PERMANENT, CELL_FEAT_PERMANENT },
        { FC_FEAT_TREE, CELL_FEAT_TREE },
        { FC_FEAT_WATER, CELL_FEAT_WATER },
        { FC_FEAT_LAVA, CELL_FEAT_LAVA },
        { FC_FEAT_GLOW, CELL_FEAT_GLOW },
        { FC_FEAT_PLAYER, CELL_FEAT_PLAYER },
        { FC_FEAT_KNOWN, CELL_FEAT_KNOWN },
        { FC_FEAT_PASSABLE, CELL_FEAT_PASSABLE },
        { FC_FEAT_RUBBLE, CELL_FEAT_RUBBLE },
        { FC_FEAT_ROOM, CELL_FEAT_ROOM },
        { FC_FEAT_GLOWING, CELL_FEAT_GLOWING },
    };

    unsigned bits = 0u;
    for (const auto &row : kTable) {
        if ((fc_bits & row.from) != 0) {
            bits |= row.to;
        }
    }
    return static_cast<uint16_t>(bits);
}

//! `FC_MM_*` → `MinimapKind`。理由は `translate_feature_flags` と同じ。
uint8_t translate_minimap_kind(unsigned char fc_kind)
{
    switch (fc_kind) {
    case FC_MM_FLOOR:
        return static_cast<uint8_t>(MinimapKind::Floor);
    case FC_MM_WALL:
        return static_cast<uint8_t>(MinimapKind::Wall);
    case FC_MM_DOOR:
        return static_cast<uint8_t>(MinimapKind::Door);
    case FC_MM_STAIRS:
        return static_cast<uint8_t>(MinimapKind::Stairs);
    case FC_MM_ITEM:
        return static_cast<uint8_t>(MinimapKind::Item);
    case FC_MM_MONSTER:
        return static_cast<uint8_t>(MinimapKind::Monster);
    case FC_MM_PLAYER:
        return static_cast<uint8_t>(MinimapKind::Player);
    case FC_MM_MOUNTAIN:
        return static_cast<uint8_t>(MinimapKind::Mountain);
    case FC_MM_WATER:
        return static_cast<uint8_t>(MinimapKind::Water);
    case FC_MM_TREE:
        return static_cast<uint8_t>(MinimapKind::Tree);
    case FC_MM_GRASS:
        return static_cast<uint8_t>(MinimapKind::Grass);
    case FC_MM_LAVA:
        return static_cast<uint8_t>(MinimapKind::Lava);
    case FC_MM_SNOW:
        return static_cast<uint8_t>(MinimapKind::Snow);
    default:
        return static_cast<uint8_t>(MinimapKind::Unknown);
    }
}

/*!
 * @brief 実体 → 目録索引（M2 で @ ・敵・物まで引くようにした）。
 *
 * @details 見る順は**変愚と同じ**（`presentation/bridge/presentation_bridge.cpp` の
 * `resolve_tile_index`）: @ → 敵 → 未視認 → 床の物 → 地形。順を変えると、
 * 敵の上に載っている品の絵が敵より先に出るような食い違いが起きる。
 *
 * **目録に無いものは 0 を返す**（嘘の索引を返さない）。画面側は `ascii_fallback` から
 * **字の板**を立てる（設計 §6.2）ので、上流が実体を足しても消えはしない。
 */
uint16_t resolve_tile_index(const fc_map_cell &cell)
{
    if (g_manifest == nullptr) {
        return 0;
    }
    if ((cell.feature_flags & FC_FEAT_PLAYER) != 0) {
        /*
         * @ の絵は**種族で決まる**（職ではない）。変愚も `PlayerRaceType` で引いている。
         * 引けなければ 0＝字の板——`prace` は Frox のほうが多い（74 種）ので、
         * 目録に無い種族が出たらそこへ落ちる。
         */
        int pclass = 0;
        int prace = 0;
        if (fc_player_kind(&pclass, &prace) == FC_OK) {
            const uint16_t pt = g_manifest->lookup_entity('P', prace);
            if (pt != 0) {
                return pt;
            }
        }
        return 0;
    }
    if (cell.monster_id != 0) {
        const uint16_t mt = g_manifest->lookup_entity('R', cell.monster_id);
        if (mt != 0) {
            return mt;
        }
        return 0; // 目録に無い敵は字の板へ（**地形を返すと床で塗り潰される**）
    }
    if ((cell.feature_flags & FC_FEAT_KNOWN) == 0) {
        return 0; // 未視認。真っ暗（地形を漏らさない）
    }
    if ((cell.light_level >= 2) && (cell.object_id != 0)) {
        const uint16_t ot = g_manifest->lookup_entity('K', cell.object_id);
        if (ot != 0) {
            return ot;
        }
        return 0; // 目録に無い品も字の板へ
    }
    return g_manifest->lookup_terrain(cell.terrain_id);
}

void fill_map(GameFrame &frame)
{
    int floor_w = 0;
    int floor_h = 0;
    if (fc_floor_size(&floor_w, &floor_h) != FC_OK) {
        return; // 階がまだ無い（誕生画面・オープニング）
    }

    int px = 0;
    int py = 0;
    if (fc_player_pos(&px, &py) != FC_OK) {
        return;
    }
    frame.player_gx = px;
    frame.player_gy = py;

    const int view_w = (std::min)((std::max)(g_view_w, 1), kMaxView);
    const int view_h = (std::min)((std::max)(g_view_h, 1), kMaxView);
    frame.view_w = view_w;
    frame.view_h = view_h;

    int ox = px - (view_w / 2);
    int oy = py - (view_h / 2);
    /* 追従しない設定のときだけ階の端で丸める（変愚と同じ）。 */
    if (!g_camera_follow_player) {
        const int max_ox = (std::max)(0, floor_w - view_w);
        const int max_oy = (std::max)(0, floor_h - view_h);
        ox = (std::max)(0, (std::min)(ox, max_ox));
        oy = (std::max)(0, (std::min)(oy, max_oy));
    }
    frame.cam_x = ox + (view_w / 2);
    frame.cam_y = oy + (view_h / 2);

    std::vector<fc_map_cell> raw(static_cast<std::size_t>(view_w) * static_cast<std::size_t>(view_h));
    const int n = fc_read_map(ox, oy, view_w, view_h, raw.data(), static_cast<int>(raw.size()));
    if (n <= 0) {
        return;
    }

    frame.cells.clear();
    frame.cells.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const fc_map_cell &src = raw[static_cast<std::size_t>(i)];
        MapCellView cell{};
        cell.gx = src.gx;
        cell.gy = src.gy;
        /*
         * **番号を名寄せしてから送る**（「送る地形番号は変愚のもの」・
         * 設計 §4.2 / §5.1）。恒等の 165 種は素通しで、Frox 固有の 23 種だけ
         * 1522〜1544（専用のボクセル）へ写る。表に無い feat は素通し。
         */
        cell.terrain_id = (src.terrain_id != 0)
            ? (g_manifest ? g_manifest->voxel_terrain(src.terrain_id, src.terrain_id)
                          : src.terrain_id)
            : 0;
        cell.feature_flags = translate_feature_flags(src.feature_flags);
        cell.monster_id = src.monster_id;
        cell.monster_slot = src.monster_slot;
        cell.object_id = src.object_id;
        cell.light_level = src.light_level;
        cell.fg_color = src.fg_color;
        cell.bg_color = src.bg_color;
        cell.ascii_fallback = src.ascii;
        cell.tile_index = resolve_tile_index(src);
        /* `under_tile_index` は 0 固定・`graf_*` は -1 固定（設計 §4 の表）。 */
        frame.cells.push_back(cell);
    }

    /* ミニマップ（階の全域）。`cells` は視界ぶんしかないので別に運ぶ。 */
    std::vector<unsigned char> kinds(static_cast<std::size_t>(floor_w) * static_cast<std::size_t>(floor_h));
    const int written = fc_read_minimap(kinds.data(), static_cast<int>(kinds.size()));
    if (written == static_cast<int>(kinds.size())) {
        frame.minimap.width = floor_w;
        frame.minimap.height = floor_h;
        frame.minimap.player_gx = px;
        frame.minimap.player_gy = py;
        frame.minimap.kinds.resize(kinds.size());
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            frame.minimap.kinds[i] = translate_minimap_kind(kinds[i]);
        }
    }
}

/*!
 * @brief 階の素性と光（`FloorIdentity` / `LightingState`）。
 * @details HD2D は `floor` で見た目の種を安定させ（同じ階へ戻ると同じ絵）、
 * `lighting` で昼夜の方向光を作る。**空だと町の意匠が出入りのたびに入れ替わる。**
 */
void fill_floor_and_lighting(GameFrame &frame)
{
    fc_floor_info info{};
    if (fc_read_floor_info(&info) != FC_OK) {
        return;
    }

    frame.floor.dungeon_id = info.dungeon_id;
    frame.floor.dun_level = info.dun_level;
    frame.floor.kind = info.kind;
    frame.floor.town_id = info.town_id;
    frame.floor.wild_mode = (info.wild_mode != 0);
    /* `generated_turn` は 0 のまま（幻想蛮怒と同じ判断。「毎回違う絵」より害が小さい）。 */
    if (info.place_name[0] != '\0') {
        frame.floor.place_name_utf8 = to_utf8(info.place_name);
    }

    frame.lighting.day_minute = info.day_minute;
    frame.lighting.daytime = (info.daytime != 0);
    frame.lighting.light_radius = info.light_radius;
}

/* ======================================================= 戦闘の見せ場（FH-05） */

/*!
 * @brief 溜まっている見せ場を汲んで `frame.combat_fx` へ移す。
 * @details 先に HP の走査（被弾・命中・とどめ）を回してから汲む。
 * **汲んだら消える**（`fc_fx_take`。幻想蛮怒の `fill_combat_fx` と同じ形——
 * 消さずに番号で照合すると同じ縁を何度も拾って演出が止まらない）。
 * 弾道・爆発は null term が観測して積んである（`fc_null_term.c` の節註）。
 */
void fill_combat_fx(GameFrame &frame)
{
    fc_fx_scan_deltas();

    fc_fx_event raw[128];
    const int n = fc_fx_take(raw, 128);

    frame.combat_fx.clear();
    if (n <= 0) {
        return;
    }
    frame.combat_fx.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const fc_fx_event &e = raw[i];
        CombatFxEvent fx{};
        fx.kind = static_cast<CombatFxKind>(e.kind);
        fx.element = static_cast<CombatFxElement>(e.element);
        fx.y = static_cast<std::int16_t>(e.y);
        fx.x = static_cast<std::int16_t>(e.x);
        fx.src_y = static_cast<std::int16_t>(e.src_y);
        fx.src_x = static_cast<std::int16_t>(e.src_x);
        /* 強さは割合（かすり傷で画面を真っ赤にしないため）。分母 0 は 1 に倒す。 */
        const float den = (e.den > 0) ? static_cast<float>(e.den) : 1.f;
        float v = static_cast<float>(e.num) / den;
        v = (v < 0.f) ? 0.f : ((v > 1.f) ? 1.f : v);
        fx.intensity = v;
        frame.combat_fx.push_back(fx);
    }
}

/* ============================================ 周囲の地形の内訳（環境音の層） */

/*!
 * @brief 周囲の内訳を `frame.surroundings` へ写す。
 * @details 数えるのは C 側（`fc_read_surroundings`）——地形の意味を知っているのは
 * `f_info` を読めるあちらだけである。ここは写すだけ。
 *
 * **`radius` が 0 なら「数えていない」**（ゲーム前・階が無い）。画面側は
 * `valid()` が偽の内訳を層に使わず、土台 1 本だけで鳴らす
 * （`presentation/frame/game_frame.h` の `SurroundingsView::valid`）。
 */
void fill_surroundings(GameFrame &frame)
{
    frame.surroundings = SurroundingsView{};

    fc_surroundings s{};
    if (fc_read_surroundings(&s) != FC_OK) {
        return;
    }
    auto &out = frame.surroundings;
    out.grass = s.grass;
    out.tree = s.tree;
    out.dirt = s.dirt;
    out.swamp = s.swamp;
    out.water = s.water;
    out.deep_water = s.deep_water;
    out.lava = s.lava;
    out.rock = s.rock;
    out.glass = s.glass;
    out.wall = s.wall;
    out.radius = s.radius;
    out.counted = s.counted;
}

/* ==================================================================== 効果音 */

/*!
 * @brief 溜まっている音を汲んで `frame.sounds` へ移す。
 * @details **汲んだら消える**（`fc_sound_take`）。音は状態ではなく一度きりの出来事で、
 * 載ったその 1 枚を逃すと二度と来ない。
 *
 * `heard_*` と `path` は載せない——Frox は音の道のりを数えていないので 0 のままにする。
 * 画面側は 0 を「計算していない」と読んで実際のマスと直線距離へ落ちる
 * （`presentation/frame/sound_event.h` の約束）。**コア名の分岐は生えない。**
 */
void fill_sounds(GameFrame &frame)
{
    fc_sound_event raw[32];
    const int n = fc_sound_take(raw, 32);

    frame.sounds.clear();
    if (n <= 0) {
        return;
    }
    frame.sounds.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const fc_sound_event &e = raw[i];
        if (e.name[0] == '\0') {
            continue;
        }
        SoundEvent s{};
        s.name = e.name;
        s.y = static_cast<std::int16_t>(e.y);
        s.x = static_cast<std::int16_t>(e.x);
        frame.sounds.push_back(std::move(s));
    }
}

//! ゲーム前画面の選択肢（`set_pregame_choices`）。ゲームスレッドだけが触る。
std::vector<MenuChoice> g_pregame_choices;

} // namespace

void set_view_size(int w, int h)
{
    if (w > 0) {
        g_view_w = w;
    }
    if (h > 0) {
        g_view_h = h;
    }
}

void set_camera_follow_player(bool follow)
{
    g_camera_follow_player = follow;
}

void set_cursor_mode(bool enabled)
{
    g_cursor_mode = enabled;
}

void apply_cursor_mode()
{
    fc_apply_cursor_mode(g_cursor_mode ? 1 : 0);
}

void set_tile_manifest(const TileManifest *manifest)
{
    g_manifest = manifest;
}

void set_pregame_choices(const std::vector<MenuChoice> &choices)
{
    g_pregame_choices = choices;
}

GameFrame capture_frame(unsigned long long frame_id)
{
    GameFrame frame;
    frame.frame_id = frame_id;

    /*
     * メニューの判定（幻想蛮怒と同じ形）。
     *
     * - `menu_open` … 「Term ミラーを前に出す」。全画面の別画面（店・一覧）と、
     *   @ がまだ／もう無い場面。`character_icky` は**計数器**なので非 0 で見る。
     * - `pre_game_menu` … 「本文を全面に出す」。@ がまだ無い／死んだとき。
     *
     * @ が出来る前は `menu_open` を必ず立てる。ここを落とすと `init_angband` の
     * 進捗や `-more-` 待ちが**画面に 1 文字も出ないまま固まる**。
     */
    const int screen = fc_screen_flags();
    const bool generated = (screen & FC_SCREEN_GENERATED) != 0;
    const bool dead = (screen & FC_SCREEN_DEAD) != 0;
    /*
     * `fc_doc_ui_active()` は M0.5 で足した（FH-02。設計 §14.2(a)）。Frox の
     * 品選び・呪文選びほか 6 つの全画面 UI は icky を立てない（`Term_save()` 直叩き）
     * ので、これが無いと一覧が画面へ 1 行も出ない。
     */
    frame.menu_open = !generated || dead || ((screen & (FC_SCREEN_ICKY | FC_SCREEN_XTRA)) != 0)
        || ((screen & FC_SCREEN_DUNGEON) == 0) || (fc_doc_ui_active() != 0);
    frame.pre_game_menu = !generated || dead;
    /* `title_screen` は「タイトル画を敷いてよい間」＝ @ が出来るまで。 */
    frame.title_screen = !generated;

    /*
     * Term ミラーは **`menu_open` のときだけ**積む。毎フレーム積むと画面側の
     * `frame_shows_map()` が「いま地図ではない」と判断して**立体を 1 枚も描かない**
     * （幻想蛮怒の P3 で実際に踏んだ）。判定の後に積む。
     */
    if (frame.menu_open) {
        fill_term_mirror(frame);
        /*
         * **地図が生きている間の重ねのメニューだけ**刈り込む（2026-09-01）。
         * 題名・誕生・死亡（`pre_game_menu`）と、地図がまだ無い間は素通し。
         */
        frame.menu_over_map = generated && !dead && ((screen & FC_SCREEN_DUNGEON) != 0)
            && crop_mirror_to_menu_box(frame);
        /* ゲーム前画面（タイトル＋セーブ選択）の選択肢。カーソル層がこれで動く。 */
        frame.menu_choices = g_pregame_choices;
        /*
         * コアのカーソル（コマンドメニューの `"> "`）と選択肢の札（M0.5 FH-01。
         * `fc_menu.h`）。**コアがカーソルを持つ画面では札を作らない**——カーソルが
         * 二重になる。ゲーム前画面の選択肢が既に入っているときも触らない。
         */
        const bool core_has_cursor = fill_core_cursor(frame);
        if (!core_has_cursor && frame.menu_choices.empty()) {
            (void)fill_menu_choices(frame);
        }
    }
    /*
     * **`menu_open` に関わらず毎フレーム。**`[y/n]` と個数入力は `screen_save()` を
     * 通らない＝ icky にならないので、ミラーが開いていない地図の上にも出る。
     */
    fill_row0_prompt(frame);

    /* 色表と操作ヒント。どちらも毎フレーム積む（既定と同じ間は codec が省く）。 */
    fill_term_palette(frame);
    /* M0 は英語。**ASCII だけで組む**（プロトコルへ出すリテラルは ASCII。設計 §3.1）。 */
    frame.controller_hint = "Move: D-pad/L  Select/F10: menu  A: enter  B: back";

    fill_hud(frame);
    /*
     * 状態列・最下段・階層。**`menu_open` に関わらず毎回積む**——icky の
     * 1 フレームで空にすると、メニューを閉じた直後の 1 枚だけ列が消える。
     * 中身の判断は `fill_status_col` が持つ。
     */
    fill_status_col(frame);
    /* サブパネル 7 枚。状態列と同じ理由で毎回積む。 */
    fill_sub_panels(frame);
    fill_messages(frame);
    fill_map(frame);
    fill_floor_and_lighting(frame);
    /* 戦闘の見せ場（FH-05）。走査＋汲み取りを毎 capture で 1 回。 */
    fill_combat_fx(frame);
    /* 効果音。見せ場と同じく汲んだら消える。 */
    fill_sounds(frame);
    /* 周囲の地形の内訳。 */
    fill_surroundings(frame);
    /*
     * **自由入力の待ちか**（追補 A1。フック #9）。`util.c` の `_askfor_aux()` を
     * `fc_text_input_set()` で囲んである。画面側はこの旗が立っている間だけ
     * IME の窓を出し、非 ASCII のバイトを送ってくる。**旗を一度も立てないと
     * IME を切る枝が一度も成立しない**（Sil-Q の SH-07 と同じ穴）。
     */
    frame.text_input_active = (fc_text_input_active() != 0);

    return frame;
}

} // namespace fc
