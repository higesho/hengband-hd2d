/*!
 * @file gb_frame.cpp
 * @brief `gb_frame.h` の実装。
 *
 * ## 漢字の扱い（設計 §3.1・V1）
 * 旧 z-term に漢字属性は**無い**（`z-term.h` に KANJI 系のフラグが無い）。
 * 2 バイト文字は**2 つのセルに割れて**入っており、`Term_putstr` が
 * `iskanji()` を見ながら 1 バイトずつ置いている。だからミラーを組む側が
 * **CP932 の先行バイトで走査して 2 セルを 1 文字に結合してから** UTF-8 化する。
 * 結合しないで 1 セルずつ変換すると、全角がすべて `??` になる。
 *
 * ## 色 span（設計 §4 の term_mirror 行）
 * 1 行 1 色に落とすと**1 行に何色も出る画面が潰れる**（変愚で実際に潰れた:
 * 色見本の行が行頭の色に染まって見えなくなった。`game_frame.h:97-105`）。
 * ここでは属性の変わり目ごとに区間を切り、行全体を隙間なく覆う。
 */

#include "gb_frame.h"

#include "gb_lang_c.h"
#include "gb_manifest.h"
#include "gb_shim.h"
#include "gb_sub_panels.h"
#include "gb_menu.h"
#include "gb_text.h"

#include "frame/cell_feature_bits.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gb {

namespace {

/* ============================================================ 画面側からの申告 */

//! 視界の既定。`ui_state` が来るまでのぶん（HD2D は必ず送ってくる）。
int g_view_w = 47;
int g_view_h = 22;
//! 視界の上限。桁違いの値が来ても走査量が爆発しないようにする。
constexpr int kMaxView = 200;
bool g_camera_follow_player = false;
const TileManifest *g_manifest = nullptr;

//! 主 Term の最大寸法。`gb_term_row` の受け皿の大きさを決めるだけの上限。
constexpr int kMaxCols = 256;
//! 画面側へ運ぶメッセージの本数。変愚の `presentation_bridge.cpp:95` と同じ 8。
constexpr int kMessageLines = 8;
//! 1 本のメッセージの上限（コアの `MESSAGE_BUF` はもっと長いが、運ぶのはこれで足りる）。
constexpr int kMessageBytes = 1024;

/*!
 * @brief セル 1 行（SJIS バイト＋属性）→ UTF-8 の 1 行＋色 span。
 * @param cells 文字のバイト列
 * @param attrs 属性
 * @param count セル数
 * @param out_spans 色 span（UTF-8 のバイト位置。行全体を隙間なく覆う）
 * @return UTF-8 の行
 */
std::string row_to_utf8(const char *cells, const unsigned char *attrs, int count,
    std::vector<TermColorSpan> &out_spans)
{
    out_spans.clear();

    /*
     * 末尾の空白は落とす。**色ごと落とす**ので span も短くなる。
     * 落とさないと、どの行も 80 桁ぶんの空白と span を運ぶことになり、
     * 27 行 × 毎フレームで効いてくる（v1 §10 の「小さく保つ」）。
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

        /* 同じ属性が続くあいだを 1 塊にする。漢字は 2 セルまとめて取る。 */
        std::string chunk;
        while (x < end) {
            if (attrs[x] != attr) {
                break;
            }
            const auto lead = static_cast<unsigned char>(cells[x]);
            if (is_sjis_lead(lead) && ((x + 1) < end)) {
                /*
                 * 2 セル 1 文字。**属性は先行バイト側を採る**——後続バイトの
                 * セルにも同じ属性が入っているのが普通だが、違っていても
                 * 文字を割ってはいけない。
                 */
                chunk.push_back(cells[x]);
                chunk.push_back(cells[x + 1]);
                x += 2;
                continue;
            }
            chunk.push_back(cells[x]);
            x += 1;
        }

        std::string utf8 = sjis_to_utf8(chunk);
        if (utf8.empty() && !chunk.empty()) {
            /* 変換できない塊（切れた漢字など）。**行を落とさず**空白で埋める。 */
            utf8.assign(chunk.size(), ' ');
        }

        TermColorSpan span;
        span.begin = static_cast<int>(text.size());
        span.len = static_cast<int>(utf8.size());
        span.color = attr;
        if (span.len > 0) {
            out_spans.push_back(span);
        }
        text += utf8;
    }

    return text;
}

/*!
 * @brief **地図を消さずに重ねただけのメニュー**なら、写しを箱の行だけへ刈り込む。
 * @return 刈り込んだ（＝重ねのメニューだった）か
 *
 * @details `util.c:4561` の `inkey_from_menu()` は `screen_save()` で `character_icky` を
 * 上げてから、**地図の上へ `+----` の箱を `put_str` するだけ**である。写しをそのまま
 * 積むと 80×24 に地図が載ったまま画面へ出て、立体の代わりに字の地図が見える
 * （実機で見つけた（2026-09-01）「幻想のコマンドメニューにもコアのマップが移りこんでる」。
 * Frox で先に直したのと同じ穴。`frox/adapter/fc_frame.cpp` と同じ手当てである）。
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
    if (gb_term_size(&cols, &rows) != GB_OK) {
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
        const int n = gb_term_row(y, cells, attrs, kMaxCols);
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
        const int got = gb_term_row(y, cells, attrs, kMaxCols);
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
    if (gb_term_size(&cols, &rows) != GB_OK) {
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
        const int n = gb_term_row(y, cells, attrs, kMaxCols);
        if (n < 0) {
            continue;
        }

        TermMirrorLine line;
        line.source_row = y;
        line.text_utf8 = row_to_utf8(cells, attrs, n, line.color_spans);
        /* `color` は `color_spans` が空のときだけ使われる（`game_frame.h:115`）。 */
        line.color = line.color_spans.empty() ? 1 : line.color_spans.front().color;
        frame.menu_term_lines.push_back(std::move(line));
    }

    /*
     * カーソル。**`menu_term_curs_row` は `menu_term_lines` の添字**であって
     * Term の行番号ではない（`game_frame.h:470-479`）。ここは空行も捨てずに
     * 全行を積んでいるので、両者は一致する。捨てるようにしたら**必ずここも直す**。
     * @note 2026-09-01 に `crop_mirror_to_menu_box()` が捨てるようになったので、
     *       あちらで添字をずらしている。**刈り込みを増やすときは同じ手当てを忘れない。**
     */
    int cx = 0;
    int cy = 0;
    int visible = 0;
    if ((gb_term_cursor(&cx, &cy, &visible) == GB_OK) && visible && (cy >= 0)
        && (cy < static_cast<int>(frame.menu_term_lines.size()))) {
        frame.menu_term_curs_col = cx;
        frame.menu_term_curs_row = cy;
    } else {
        frame.menu_term_curs_col = -1;
        frame.menu_term_curs_row = -1;
    }
}

/* ================================ 左の状態列と最下段（P4。K-28 / K-32 / K-33） */

/*!
 * @name コアの左フレームを MainMap の左へ写す
 * @details 旧コアは主 Term の**左 20 桁**に難易度・点数・種族・称号・レベル・経験値・
 * 所持金・装備記号・能力値・AC・HP・SP・騎乗・状態・切り傷・朦朧・空腹・日付・
 * ダンジョン名を描き続けている（`xtra1.c` の `prt_frame_basic` / `prt_frame_extra`）。
 * **表示を組み直さず、その区画をそのまま読む**（設計 §8 の P4 行）。
 *
 * 桁の根拠は `defines.h:1489` の **`COL_MAP = 20`**。地図は列 20 から描かれる
 * （`cave.c:1780` の `Term_erase(COL_MAP, y, wid)` と `:2203` の `Term_gotoxy(COL_MAP, y)`）
 * ので、列 0〜19 は左フレームの専有である。**変愚の 13 ではない**——
 * あちらは `main-window-row-column.h` が 13 桁で、幻想蛮怒は 20 桁に広げてある
 * （`defines.h:1488` に 12 だった頃の行がコメントで残っている）。
 *
 * 行の範囲は 1〜`rows-2`。行 0 はメッセージ行、最下行はステータスバーで横いっぱい
 * （`ROW_STATBAR = -1`）。左フレームの最下段は `ROW_DUNGEON = 25` で、
 * 27 行の Term ではちょうど `rows-2` に当たる。
 * @{
 */
//! 左フレームの桁数（`COL_MAP`。**変愚の 13 ではない**）。
constexpr int kStatusColCells = 20;
//! 階層が書かれている桁数（`COL_DEPTH = -8`。`prt_depth` の `%7s` ＋ 余白 1 桁）。
constexpr int kDepthCells = 8;

/*!
 * @name 直前に読めた値
 * @details `redraw_stuff()` は `character_icky` の間**左フレームを更新しない**
 * （`xtra1.c:12325`）。店や一覧を開いている間の Term は本文で塗り潰されているので、
 * そのときは直前に読めた値を使い回す（変愚 `presentation_bridge.cpp:737-742` と同じ手）。
 * @{
 */
std::vector<SubPanelLine> g_status_col_cache;
std::vector<TermTextRun> g_bottom_row_cache;
SubPanelLine g_depth_cache;
/*! @} */

/*!
 * @brief セル 1 個ぶんを空白とみなすか。
 * @details 空白・未書き込み（`\0`）のほかに**制御文字**（`< 0x20` と `0x7F`）も空白にする。
 *
 * これは **地図が左フレームへはみ出してくる**ため（実測 2026-08-17 / P4）。
 * ゲーム中の Term を 1 桁ずつ見ると、桁 17〜19 に `0x7F`（色 TERM_SLATE）が並び、
 * そのまま桁 20 以降の地図へ続いている。**読み込み中の画面には無く、地図が出てから
 * 現れる**ので出どころは地図側である。変愚の bridge も同じ形の穴を
 * `is_graphic_tile_attr()` で塞いでいる（`presentation_bridge.cpp:775`
 * 「地図のタイルが入り込む桁は空白にする」）。あちらは絵タイルなので属性で分かるが、
 * こちらは ASCII なので**文字**で見分ける——左フレームの項目はすべて印字文字なので、
 * 制御文字を落として困るものは無い。
 * @note SJIS の 2 バイト目は `0x40`〜`0x7E` と `0x80`〜`0xFC` で、**`0x7F` を含まない**。
 * だからこの判定で漢字の後続バイトを空白に取り違えることはない。
 */
bool cell_is_blank(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u == ' ') || (u < 0x20) || (u == 0x7F);
}

/*!
 * @brief 桁 `[x0, x1)` を UTF-8 の 1 行にする（漢字は 2 セル結合。`row_to_utf8` と同じ規則）。
 * @param trim_left 行頭の空白も落とすか。**左フレームでは落とさない**——
 * 1 行に複数の項目が決まった桁で並ぶ（`ROW_HUNGRY` 23 桁 0 ／ `COL_STATE` 4 ／
 * `COL_MOVESTATE` 9）ので、左を詰めると空腹が無いときに状態が空腹の位置へ滑る。
 * 階層（`%7s` の右詰め）だけは両端を落とす。
 * @return 行の色は**最初の非空白**セルの色（`SubPanelLine` は 1 行 1 色）。
 */
SubPanelLine span_to_line(const char *cells, const unsigned char *attrs, int n, int x0, int x1,
    bool trim_left)
{
    SubPanelLine out;
    const int begin = (std::max)(0, x0);
    int end = (std::min)(x1, n);

    /* 末尾の空白は落とす（運ぶ量を減らす。`row_to_utf8` と同じ判断）。 */
    while ((end > begin) && cell_is_blank(cells[end - 1])) {
        --end;
    }
    if (end <= begin) {
        return out;
    }

    std::string sys;
    bool color_found = false;
    for (int x = begin; x < end; ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        if (!color_found && !cell_is_blank(cells[x])) {
            out.color = attrs[x];
            color_found = true;
        }
        if (is_sjis_lead(lead) && ((x + 1) < end)) {
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            ++x;
            continue;
        }
        if (is_sjis_lead(lead)) {
            /* 区画の端で漢字が割れた。**半端なバイトは運ばない**（化けさせない）。 */
            sys.push_back(' ');
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

    out.text_utf8 = sjis_to_utf8(sys);
    if (out.text_utf8.empty() && !sys.empty()) {
        out.text_utf8.assign(sys.size(), ' ');
    }
    return out;
}

/*!
 * @brief 最下行（ステータスバー）を**同じ色が続く区間ごと**に切って桁とともに積む（K-32）。
 * @details この行は桁と色の両方に意味がある——左に状態異常・空腹・休息（`prt_status`。
 * `COL_STATBAR = 0`〜`MAX_COL_STATBAR = -26`）、右端の決まった桁に速度・学習・階層
 * （`COL_SPEED = -24` / `COL_STUDY = -13` / `COL_DEPTH = -8`）。1 行 1 色に潰すと両方落ちる。
 * @param depth_begin ここから右は積まない（階層は K-33 で右下へ出すので二重にしない）
 */
void row_to_runs(const char *cells, const unsigned char *attrs, int n, int depth_begin,
    std::vector<TermTextRun> &out)
{
    out.clear();
    const int end = (std::min)(n, depth_begin);

    std::string sys;
    int run_col = 0;
    unsigned char run_color = 1;

    const auto flush = [&]() {
        while (!sys.empty() && (sys.back() == ' ')) {
            sys.pop_back();
        }
        if (sys.empty()) {
            return;
        }
        TermTextRun run;
        run.col = run_col;
        run.color = run_color;
        run.text_utf8 = sjis_to_utf8(sys);
        if (run.text_utf8.empty()) {
            run.text_utf8.assign(sys.size(), ' ');
        }
        out.push_back(std::move(run));
        sys.clear();
    };

    for (int x = 0; x < end; ++x) {
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

        if (is_sjis_lead(lead) && ((x + 1) < end)) {
            /* 2 セル 1 文字。**属性は先行バイト側**を採る（文字を割らない）。 */
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            ++x;
            continue;
        }
        if (is_sjis_lead(lead)) {
            break; // 区画の端で割れた漢字。半端なバイトは運ばない
        }
        sys.push_back(cells[x]);
    }
    flush();
}

/*!
 * @brief 左の状態列（K-28）・最下段（K-32）・階層（K-33）をまとめて写す。
 * @details 読む相手の判断（icky なら直前の値）が同じなので 1 か所に置く。
 */
void fill_status_col(GameFrame &frame)
{
    frame.status_col_lines.clear();
    //! 桁数は**コアが申告する**（画面側に数を持たせない。`game_frame.h` の `status_col_cols`）。
    //! 変愚の 13 ではなく `COL_MAP` の 20 である。申告しないと地名が桁で切れる。
    frame.status_col_cols = kStatusColCells;
    frame.bottom_row_runs.clear();
    frame.bottom_row_cols = 0;
    frame.depth = SubPanelLine{};

    const int screen = gb_screen_flags();
    if (((screen & GB_SCREEN_GENERATED) == 0) || ((screen & GB_SCREEN_DUNGEON) == 0)
        || ((screen & GB_SCREEN_DEAD) != 0)) {
        /*
         * @ がまだ無い（オープニング・誕生）／もう無い（死亡）。Term の左端は本文なので
         * 読まない。死亡を混ぜてあるのは、墓碑の画を状態列として拾わないため
         * （画面側は `pre_game_menu` で全面ミラーに切り替わるので出はしないが、
         * **古い値を持ち越さない**ほうが後で読む人に嘘をつかない）。
         */
        g_status_col_cache.clear();
        g_bottom_row_cache.clear();
        g_depth_cache = SubPanelLine{};
        return;
    }

    int cols = 0;
    int rows = 0;
    if (gb_term_size(&cols, &rows) != GB_OK) {
        return;
    }
    cols = (std::min)(cols, kMaxCols - 1);
    if ((cols <= kDepthCells) || (rows <= 2)) {
        return;
    }
    frame.bottom_row_cols = cols;

    /*
     * icky の間はコアが左フレームを更新していない（`xtra1.c:12325`）ので、
     * **その画を読まずに**直前の値を使う。ゲーム中のフレームでは毎回読み直す。
     */
    if ((screen & (GB_SCREEN_ICKY | GB_SCREEN_XTRA)) == 0) {
        char cells[kMaxCols];
        unsigned char attrs[kMaxCols];

        std::vector<SubPanelLine> lines;
        for (int y = 1; y <= (rows - 2); ++y) {
            std::memset(cells, ' ', sizeof(cells));
            std::memset(attrs, 1, sizeof(attrs));
            const int n = gb_term_row(y, cells, attrs, kMaxCols);
            if (n < 0) {
                continue;
            }
            lines.push_back(span_to_line(cells, attrs, n, 0, kStatusColCells, false));
        }
        while (!lines.empty() && lines.back().text_utf8.empty()) {
            lines.pop_back();
        }
        frame.status_col_lines = std::move(lines);

        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int n = gb_term_row(rows - 1, cells, attrs, kMaxCols);
        if (n > 0) {
            row_to_runs(cells, attrs, n, cols - kDepthCells, frame.bottom_row_runs);
            frame.depth = span_to_line(cells, attrs, n, cols - kDepthCells, cols, true);
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
/*! @} */

void fill_hud(GameFrame &frame)
{
    gb_hud_data data{};
    if (gb_read_hud(&data) != GB_OK) {
        return;
    }

    frame.hud.name = sjis_to_utf8(data.name, std::strlen(data.name));
    frame.hud.hp = data.hp;
    frame.hud.hp_max = data.hp_max;
    frame.hud.sp = data.sp;
    frame.hud.sp_max = data.sp_max;
    frame.hud.gold = static_cast<int>(data.gold);
    frame.hud.level = data.level;
    frame.hud.depth = data.depth;

    /*
     * `status_line` は変愚の `fill_hud`（`presentation_bridge.cpp:122-125`）と
     * **同じ形**にする。画面側が同じ読み方をするので、形を割らない。
     * **ASCII だけで組む**（設計 §3.1: プロトコルへ出すリテラルは ASCII）。
     */
    char status[128];
    std::snprintf(status, sizeof(status), "HP %d/%d  SP %d/%d  LEV %d",
        frame.hud.hp, frame.hud.hp_max, frame.hud.sp, frame.hud.sp_max, frame.hud.level);
    frame.hud.status_line = status;

    /*
     * 現在地と階層は**コアの文言のまま**運ぶ（`prt_depth()` と同じ字面）。
     * ここで組み直すと画面側と食い違うので、日本語は SJIS → UTF-8 の変換だけ。
     */
    frame.hud.right_top_lines.clear();
    if (data.place[0] != '\0') {
        frame.hud.right_top_lines.push_back(sjis_to_utf8(data.place, std::strlen(data.place)));
    }
    if (data.depth_text[0] != '\0') {
        frame.hud.right_top_lines.push_back(sjis_to_utf8(data.depth_text, std::strlen(data.depth_text)));
    }
}

void fill_messages(GameFrame &frame)
{
    const int num = gb_message_count();
    const int take = (std::min)(kMessageLines, (num > 0) ? num : 0);

    frame.messages.clear();
    frame.messages.reserve(static_cast<std::size_t>(take));

    char buf[kMessageBytes];
    for (int age = take - 1; age >= 0; --age) {
        const int n = gb_message_text(age, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            continue;
        }
        MessageEvent ev{};
        ev.seq = static_cast<uint32_t>(frame.messages.size());
        ev.color = 1;
        /*
         * **メッセージは Term を通らない。** ここはコアのメッセージリングを直に読む道で、
         * `Term_addstr()` のフック #1 は 1 度も通らない（設計 §5 #4 の
         * 「Term 経由で足りる」はメッセージには当たらなかった。E3 の通しで判った）。
         * だから引くのはここである——`msg_print()` の素の文言はこれで英語になる。
         *
         * `msg_format()` の文言は既に組み上がっているので、ここでは引けない
         * （書式はフック #2、`%s` の中身は #3 が先に引いている）。
         * 引けなければ日本語のまま返り、`--report-untranslated` が拾う（制約 4）。
         */
        const char *text = gb_tr(buf);
        ev.text_utf8 = sjis_to_utf8(text, std::strlen(text));
        if (ev.text_utf8.empty()) {
            continue;
        }
        frame.messages.push_back(std::move(ev));
    }
}

/* ================================================ 色表とカーソル（M1 / S6・S2） */

//! 画面側の `ui_state.cursor_mode`。**既定は真**——コアを起こさない検査は握手を
//! 通らないので、既定を偽にすると検査だけカーソルの無い世界になる（設計 §6.3 と同じ理由）。
bool g_cursor_mode = true;

/*!
 * @brief コアの色表を `frame.term_palette` へ（S6）。
 * @details 変愚の `fill_term_palette()`（`presentation_bridge.cpp:1018`）と同じ形。
 * 読めなかったときは**既定のまま置く**（`TermPalette` の初期値＝変愚の色）。
 * 嘘の色で塗るより、前と同じ絵のほうが害が小さい。
 */
void fill_term_palette(GameFrame &frame)
{
    unsigned char rgb[16 * 3] = { 0 };
    if (gb_read_palette(rgb, static_cast<int>(sizeof(rgb))) < 0) {
        return;
    }
    for (std::size_t i = 0; i < frame.term_palette.rgb.size(); ++i) {
        frame.term_palette.rgb[i][0] = rgb[i * 3 + 0];
        frame.term_palette.rgb[i][1] = rgb[i * 3 + 1];
        frame.term_palette.rgb[i][2] = rgb[i * 3 + 2];
    }
}

/* ==================================================================== 地図（P3） */

/*!
 * @brief `GB_FEAT_*`（C の境界）→ `CELL_FEAT_*`（画面側）。
 * @details **表をここ 1 か所に置く。** 値を揃えて素通しにすると、どちらかの並びが
 * 変わったときに黙って別の意味になる（壁が扉になる類の壊れ方で、絵を見るまで分からない）。
 */
uint16_t translate_feature_flags(unsigned short gb_bits)
{
    static const struct {
        unsigned short from;
        uint16_t to;
    } kTable[] = {
        { GB_FEAT_WALL, CELL_FEAT_WALL },
        { GB_FEAT_DOOR, CELL_FEAT_DOOR },
        { GB_FEAT_DOOR_OPEN, CELL_FEAT_DOOR_OPEN },
        { GB_FEAT_STAIRS, CELL_FEAT_STAIRS },
        { GB_FEAT_PERMANENT, CELL_FEAT_PERMANENT },
        { GB_FEAT_TREE, CELL_FEAT_TREE },
        { GB_FEAT_WATER, CELL_FEAT_WATER },
        { GB_FEAT_LAVA, CELL_FEAT_LAVA },
        { GB_FEAT_GLOW, CELL_FEAT_GLOW },
        { GB_FEAT_PLAYER, CELL_FEAT_PLAYER },
        { GB_FEAT_KNOWN, CELL_FEAT_KNOWN },
        { GB_FEAT_PASSABLE, CELL_FEAT_PASSABLE },
        { GB_FEAT_RUBBLE, CELL_FEAT_RUBBLE },
        { GB_FEAT_ROOM, CELL_FEAT_ROOM },
        { GB_FEAT_GLOWING, CELL_FEAT_GLOWING },
    };

    unsigned bits = 0u;
    for (const auto &row : kTable) {
        if ((gb_bits & row.from) != 0) {
            bits |= row.to;
        }
    }
    return static_cast<uint16_t>(bits);
}

//! `GB_MM_*` → `MinimapKind`。理由は `translate_feature_flags` と同じ。
uint8_t translate_minimap_kind(unsigned char gb_kind)
{
    switch (gb_kind) {
    case GB_MM_FLOOR:
        return static_cast<uint8_t>(MinimapKind::Floor);
    case GB_MM_WALL:
        return static_cast<uint8_t>(MinimapKind::Wall);
    case GB_MM_DOOR:
        return static_cast<uint8_t>(MinimapKind::Door);
    case GB_MM_STAIRS:
        return static_cast<uint8_t>(MinimapKind::Stairs);
    case GB_MM_ITEM:
        return static_cast<uint8_t>(MinimapKind::Item);
    case GB_MM_MONSTER:
        return static_cast<uint8_t>(MinimapKind::Monster);
    case GB_MM_PLAYER:
        return static_cast<uint8_t>(MinimapKind::Player);
    case GB_MM_MOUNTAIN:
        return static_cast<uint8_t>(MinimapKind::Mountain);
    default:
        return static_cast<uint8_t>(MinimapKind::Unknown);
    }
}

/*!
 * @brief @ の絵の索引。**クラス → 種族 → `@` の板**の 3 段。
 * @details 幻想蛮怒は**クラスが人物**なので、クラスを先に見る。一般クラス（戦士・メイジ…）は
 * クラスの絵を持たないので種族へ落ちる。種族の絵も無ければ受け皿の `@` の板に落ちて、
 * **絵を 1 枚も入れていない状態が従来どおりになる**（M0 と同じ見え方）。
 * 種族の id を `1000 +` でずらすのは、クラスと種族が同じ `P` の番号空間を分け合うためである。
 *
 * **受け皿は `P,0` ではなく `P,9999`。**`0` は戦士のクラス番号なので、そこに `@` を置くと
 * 戦士だけクラスの絵を持てなくなる（`build_mapping.py` の P 行の注記と対になっている。
 * **片方だけ変えると @ が地形に化ける**——受け皿が引けないと `resolve_tile_index` が
 * そのまま床の索引を返すためで、実測で 1 度踏んだ）。
 * @note 誕生前（`gb_player_kind` が `GB_ERR_STATE`）も受け皿を返す。
 */
uint16_t player_tile_index()
{
    constexpr uint16_t fallback_id = 9999; // `@` の板。build_mapping.py と対

    int pclass = -1;
    int prace = -1;
    if (gb_player_kind(&pclass, &prace) == GB_OK) {
        if (pclass >= 0) {
            if (const uint16_t by_class = g_manifest->lookup('P', static_cast<uint16_t>(pclass))) {
                return by_class;
            }
        }
        if (prace >= 0) {
            if (const uint16_t by_race = g_manifest->lookup('P', static_cast<uint16_t>(1000 + prace))) {
                return by_race;
            }
        }
    }
    return g_manifest->lookup('P', fallback_id);
}

/*!
 * @brief **幻想蛮怒の地形 id を変愚の番号へ名寄せする**。
 *
 * ## なぜ要るのか
 * 立体（3D）の細別は `PrefabLibrary::rule_for_terrain(terrain_id)` が
 * `assets/voxel/terrain_prefabs.jsonc` を**番号で**引いて決める。番号は 226 まで
 * 幻想蛮怒と変愚で揃っている（タグまで一致）が、**227〜240 に幻想蛮怒だけの地形が
 * 割り込んでいる**ので、そこから先は同じ番号が別の地形を指す。
 * 直し方は 2 通りある:
 *
 * 1. **番号がずれただけの地形は写す。**幻想蛮怒の 241〜248（寒気帯・電撃帯・酸の沼・
 *    毒の沼）は変愚にも同じタグの地形が 227〜234 に在る。番号を写せば
 *    **今日ただの床に見えている毒の沼と酸の沼が 3D の沼になる**
 * 2. **幻想蛮怒だけの地形が変愚の規則を横取りするのを止める。**231〜238 は変愚側の
 *    同じ番号に**規則の行が在る**ので、建物や扉が沼や水没岩に化けている
 *    （8 件ある）。予約帯へ逃がして
 *    **役割の既定へ落とす**——「何も出ない」ほうが「別の地形が出る」より正しい
 *
 * ## ここでやる理由
 * **UI にコア固有の知識を持ち込まない**（`GENSOBAND_CORE_DESIGN` §1-1）。番号の食い違いは
 * 幻想蛮怒コアの事実なので、名寄せもコア側（このアダプタ）が持つ。**先方の `src/` は触らない。**
 *
 * ## 表を機械で作らない理由（設計 §2.4 の訂正。2026-08-20 実測）
 * 設計書は「`mapping.csv` の F 行から機械的に作る」と書いていたが、**それでは壊れる**。
 * mapping.csv は 2D の**絵**の名寄せなので、絵を使い回している行が同じ番号へ倒れている
 * （`F,129〜159 → F128` ＝ `BUILDING_1`〜`BUILDING_31` が全部 `BUILDING_0` の絵、
 * `F,160〜166 → F1` ＝ 部屋生成の雛形が床の絵）。これを立体の名寄せに使うと、
 * **町じゅうの建物が 1 種類に潰れて看板と門が壊れる**（設計 V3 が案じていたとおり）。
 * だから名寄せは**タグの一致**から作った下の表で行う。
 *
 * @param gb_id 幻想蛮怒の feat 番号（`gb_shim.c` がそのまま送ってくる）
 * @return UI へ送る番号。**0〜226 と 249〜252 は素通し**（恒等写像）
 */
uint16_t align_terrain_id(uint16_t gb_id)
{
    /*
     * **予約帯**（幻想蛮怒だけの地形の逃がし先）。変愚の規則表の最大 id は 238 なので、
     * 1000 番台は永久に空である。逃がしたマスは役割の既定（床・壁）で描かれる。
     * @note この帯は**仮の宿**である。 の
     *   「幻想蛮怒の地形表を UI に持たせる」が入ったら、この写しごと消してよい。
     */
    constexpr uint16_t kReserved = 1000;

    struct Row {
        uint16_t from;
        uint16_t to;
    };
    /*
     * **タグの一致から作った表**（`gensoband/lib/edit/f_info.txt` の `N:` と
     * `lib/edit/TerrainDefinitions.jsonc` の `key` を突き合わせた結果。2026-08-20 実測）。
     */
    static constexpr Row kRows[] = {
        /* ---- ① 番号がずれただけ（タグが一致する）。写す ---- */
        { 241, 227 }, //!< HEAVY_COLD_ZONE
        { 242, 228 }, //!< COLD_ZONE
        { 243, 229 }, //!< HEAVY_ELECTRICAL_ZONE
        { 244, 230 }, //!< ELECTRICAL_ZONE
        { 245, 231 }, //!< DEEP_ACID_PUDDLE …… ここから 4 つが**今日ただの床**
        { 246, 232 }, //!< SHALLOW_ACID_PUDDLE
        { 247, 233 }, //!< DEEP_POISONOUS_PUDDLE
        { 248, 234 }, //!< SHALLOW_POISONOUS_PUDDLE
        /*
         * ---- ② 幻想蛮怒だけの地形が変愚の規則を横取りしている 8 件。予約帯へ逃がす ----
         * **逃がすのはこの 8 つだけでよい**——227〜230・239・240・249〜252 も幻想蛮怒
         * だけの地形だが、変愚側の同じ番号に**規則の行が無い**ので既に役割へ落ちている
         * （`terrain_prefabs.jsonc` の実測: 227〜252 で行が在るのは 231〜238 だけ）。
         * とくに 240（`ICE_WALL`）は `town_styles.jsonc` が**番号のまま名指して**
         * 氷塊に差し替えているので、動かしてはいけない。
         * @note `terrain_prefabs.jsonc` に 227〜252 の行が増えたら**この表を見直すこと**。
         */
        { 231, kReserved + 231 }, //!< BUILDING_EX2 ← 深い酸の沼
        { 232, kReserved + 232 }, //!< BUILDING_EX3 ← 浅い酸の沼
        { 233, kReserved + 233 }, //!< BUILDING_EX4 ← 深い毒の沼
        { 234, kReserved + 234 }, //!< TEWI_PIT ← 浅い毒の沼
        { 235, kReserved + 235 }, //!< HANAKOSAN_DOOR ← 深い水の上の岩石
        { 236, kReserved + 236 }, //!< ELDER_SIGN ← 深い水の上の溶岩の鉱脈
        { 237, kReserved + 237 }, //!< PORTAL ← 深い水の上の石英の鉱脈
        { 238, kReserved + 238 }, //!< TRAP_BEAM ← 深い水の上の花崗岩の壁
    };
    for (const Row &row : kRows) {
        if (row.from == gb_id) {
            return row.to;
        }
    }
    return gb_id;
}

/*!
 * @brief 実体 → 目録索引（設計 §5.1）。優先順は変愚の `resolve_tile_index` と同じ。
 * @details @ ＞ 見えている敵 ＞（未視認なら 0）＞ 照らされた床の物 ＞ 地形。
 * R/K/F は mapping.csv が全数を持っているので、**未登録で 0 に落ちるのは
 * 未視認のマスだけ**である（ASCII 板が受け皿になっている。§5.2-3）。
 */
uint16_t resolve_tile_index(const gb_map_cell &cell)
{
    if (g_manifest == nullptr) {
        return 0;
    }
    if ((cell.feature_flags & GB_FEAT_PLAYER) != 0) {
        if (const uint16_t pt = player_tile_index()) {
            return pt;
        }
    }
    if (cell.monster_id != 0) {
        if (const uint16_t mt = g_manifest->lookup('R', cell.monster_id)) {
            return mt;
        }
    }
    if ((cell.feature_flags & GB_FEAT_KNOWN) == 0) {
        return 0; // 未視認。真っ暗（地形を漏らさない）
    }
    if ((cell.light_level >= 2) && (cell.object_id != 0)) {
        if (const uint16_t ot = g_manifest->lookup('K', cell.object_id)) {
            return ot;
        }
    }
    return g_manifest->lookup('F', cell.terrain_id);
}

void fill_map(GameFrame &frame)
{
    int floor_w = 0;
    int floor_h = 0;
    if (gb_floor_size(&floor_w, &floor_h) != GB_OK) {
        return; // 階がまだ無い（誕生画面・オープニング）
    }

    int px = 0;
    int py = 0;
    if (gb_player_pos(&px, &py) != GB_OK) {
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
    /*
     * 追従しない設定のときだけ階の端で丸める（変愚の `Bridge::capture` と同じ）。
     * HD2D は 1 点透視で可視帯が奥へ深く、丸めると @ が画面内を泳ぐので追従が既定。
     * はみ出したマスは `gb_read_map` が空白で返すので走査も描画も壊れない。
     */
    if (!g_camera_follow_player) {
        const int max_ox = (std::max)(0, floor_w - view_w);
        const int max_oy = (std::max)(0, floor_h - view_h);
        ox = (std::max)(0, (std::min)(ox, max_ox));
        oy = (std::max)(0, (std::min)(oy, max_oy));
    }
    frame.cam_x = ox + (view_w / 2);
    frame.cam_y = oy + (view_h / 2);

    std::vector<gb_map_cell> raw(static_cast<std::size_t>(view_w) * static_cast<std::size_t>(view_h));
    const int n = gb_read_map(ox, oy, view_w, view_h, raw.data(), static_cast<int>(raw.size()));
    if (n <= 0) {
        return;
    }

    frame.cells.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const gb_map_cell &src = raw[static_cast<std::size_t>(i)];
        MapCellView cell{};
        cell.gx = src.gx;
        cell.gy = src.gy;
        //! **番号を名寄せしてから送る**（`align_terrain_id`。設計 §2.4）。
        //! 2D の板は `resolve_tile_index` が**生の番号**で引くので、そちらは動かない。
        cell.terrain_id = align_terrain_id(src.terrain_id);
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
    const int written = gb_read_minimap(kinds.data(), static_cast<int>(kinds.size()));
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
    gb_floor_info info{};
    if (gb_read_floor_info(&info) != GB_OK) {
        return;
    }

    frame.floor.dungeon_id = info.dungeon_id;
    frame.floor.dun_level = info.dun_level;
    frame.floor.kind = info.kind;
    frame.floor.town_id = info.town_id;
    frame.floor.wild_mode = (info.wild_mode != 0);
    /*
     * `generated_turn` は **0 のまま**にする。変愚ではこれで「作り直された同じ階」を
     * 見分けるが、幻想蛮怒の階に「作られた turn」を持つ欄が無い。0 に倒すと
     * 「同じ dungeon_id・同じ階は毎回同じ絵」になる——現実変容の後も前の見た目が
     * 残るという穴はあるが、**毎回違う絵になるより害が小さい**（設計 §10 の後段）。
     */
    if (info.place_name[0] != '\0') {
        frame.floor.place_name_utf8 = sjis_to_utf8(info.place_name, std::strlen(info.place_name));
    }

    frame.lighting.day_minute = info.day_minute;
    frame.lighting.daytime = (info.daytime != 0);
    frame.lighting.light_radius = info.light_radius;
}

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
    gb_apply_cursor_mode(g_cursor_mode ? 1 : 0);
}

void set_tile_manifest(const TileManifest *manifest)
{
    g_manifest = manifest;
}

//! ゲーム前画面の選択肢（`set_pregame_choices`）。ゲームスレッドだけが触る。
std::vector<MenuChoice> g_pregame_choices;

void set_pregame_choices(const std::vector<MenuChoice> &choices)
{
    g_pregame_choices = choices;
}

/* ============================================ 戦闘の見せ場（M1 / S10・R4） */

/*!
 * @brief 溜まっている見せ場を汲んで `frame.combat_fx` へ移す。
 * @details **汲んだら消す**（`gb_fx_take`）。消さずに番号で照合する作りにすると、
 * 同じ縁を何度も拾って演出が止まらない（変愚の `TeleportFx::burst` と同じ罠）。
 */
void fill_combat_fx(GameFrame &frame)
{
    gb_fx_event raw[GB_FX_MAX];
    const int n = gb_fx_take(raw, GB_FX_MAX);

    frame.combat_fx.clear();
    if (n <= 0) {
        return;
    }
    frame.combat_fx.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const gb_fx_event &e = raw[i];
        CombatFxEvent fx{};
        fx.kind = static_cast<CombatFxKind>(e.kind);
        //! 束にするのは C 側（`GF_*` を読めるのはあちら）。番号は `CombatFxElement` と同じ。
        fx.element = static_cast<CombatFxElement>(gb_fx_element_of(e.typ));
        fx.y = static_cast<std::int16_t>(e.y);
        fx.x = static_cast<std::int16_t>(e.x);
        fx.src_y = static_cast<std::int16_t>(e.src_y);
        fx.src_x = static_cast<std::int16_t>(e.src_x);
        /*
         * 強さは**割合**（かすり傷で画面を真っ赤にしないため）。
         * 分母 0 はコア側で潰してあるが、ここでも 1 に倒しておく。
         */
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
 * @details 数えるのは C 側（`gb_read_surroundings`）——地形の意味を知っているのは
 * `f_info` を読めるあちらだけである。ここは写すだけ。
 *
 * **`radius` が 0 なら「数えていない」**（ゲーム前・階が無い）。画面側は
 * `valid()` が偽の内訳を層に使わず、土台 1 本だけで鳴らす
 * （`presentation/frame/game_frame.h` の `SurroundingsView::valid`）。
 */
void fill_surroundings(GameFrame &frame)
{
    frame.surroundings = SurroundingsView{};

    gb_surroundings s{};
    if (gb_read_surroundings(&s) != GB_OK) {
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
 * @details **汲んだら消す**（`gb_sound_take`）。音は状態ではなく一度きりの出来事で、
 * 載ったその 1 枚を逃すと二度と来ない。
 *
 * **画面側が鳴らすときしか溜まらない。**そうでないときは `gb_audio.c` が
 * winmm で鳴らしていて、ここは常に 0 件で通る（§2.2）。
 *
 * `heard_*` と `path` は載せない——音の道のりを数えていないので 0 のままにする。
 * 画面側は 0 を「計算していない」と読んで実際のマスと直線距離へ落ちる
 * （`presentation/frame/sound_event.h` の約束）。**コア名の分岐は生えない。**
 */
void fill_sounds(GameFrame &frame)
{
    gb_sound_event raw[32];
    const int n = gb_sound_take(raw, 32);

    frame.sounds.clear();
    if (n <= 0) {
        return;
    }
    frame.sounds.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const gb_sound_event &e = raw[i];
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

GameFrame capture_frame(unsigned long long frame_id)
{
    GameFrame frame;
    frame.frame_id = frame_id;

    /*
     * メニューの判定（P3。P2 は真をべた書きしていた）。
     *
     * - `menu_open` … 「Term ミラーを前に出す」。全画面の別画面（店・建物・一覧）と、
     *   @ がまだ／もう無い場面。`character_icky` は**計数器**なので非 0 で見る
     *   （`gb_screen_flags` の注記）。`character_xtra`（再計算中）も混ぜるのは
     *   `main-win.c:2805` が「いま普通の地図ではない」の判定にそう使っているから。
     * - `pre_game_menu` … 「本文を全面に出す」。@ がまだ無い／死んだとき
     *   （`game_frame.h:397-399`）。変愚の `frame.pre_game_menu = !character_generated || is_dead`
     *   と同じ形にしてある。
     *
     * @ が出来る前は `menu_open` を必ず立てる。ここを落とすと `init_angband` の
     * 進捗や `-more-` 待ちが**画面に 1 文字も出ないまま固まる**（変愚が Android で
     * 実際に踏んだ穴。`presentation_bridge.cpp:3151-3160`）。
     */
    const int screen = gb_screen_flags();
    const bool generated = (screen & GB_SCREEN_GENERATED) != 0;
    const bool dead = (screen & GB_SCREEN_DEAD) != 0;
    frame.menu_open = !generated || dead || ((screen & (GB_SCREEN_ICKY | GB_SCREEN_XTRA)) != 0)
        || ((screen & GB_SCREEN_DUNGEON) == 0);
    frame.pre_game_menu = !generated || dead;
    /*
     * `title_screen`（K-47）は「タイトル画を敷いてよい間」。幻想蛮怒には
     * オープニングの宣言口が無いので、**@ が出来るまで**をそれとみなす。
     */
    frame.title_screen = !generated;

    /*
     * Term ミラーは **`menu_open` のときだけ**積む（`game_frame.h:468`
     * 「icky 中の Term 全文ミラー（menu_open 時のみ Bridge が充填）」）。
     *
     * **毎フレーム積んではいけない。**画面側の `frame_shows_map()`
     * （`hd2d/ui/game_hud.cpp:639`）は `menu_term_lines.empty()` だけを見ており、
     * 積んであると「いま地図ではない」と判断して**立体を 1 枚も描かず**
     * Term の写しを窓に出す。P3 の実機で実際にそうなった——HUD もミニマップも
     * 正しいのに地図だけが出ず、原因がコア側にあるように見えた。
     * 中身が空でもキーが立てば同じなので、**判定の後に積む**形にしてある。
     */
    if (frame.menu_open) {
        fill_term_mirror(frame);
        /*
         * **地図が生きている間の重ねのメニューだけ**刈り込む（2026-09-01）。
         * 題名・誕生・死亡と、地図がまだ無い間は素通し。
         */
        frame.menu_over_map = generated && !dead && ((screen & GB_SCREEN_DUNGEON) != 0)
            && crop_mirror_to_menu_box(frame);
        /*
         * ゲーム前画面（タイトル＋セーブ選択。P5）の選択肢。アダプタが自分で
         * 描いた画面の分だけを宣言で載せる（Term からの自動抽出は持たない）。
         * 画面側のカーソル層（矢印・パッド・クリック）がこれで動く。
         */
        frame.menu_choices = g_pregame_choices;
    }

    /*
     * 色表（S6）と操作ヒント（S8）。どちらも 1 フレームぶんの費用がほぼ無い
     * （色は 48 バイトで、既定と同じ間は codec が省く）。**毎フレーム積む**——
     * `&` と pref の `V:` で遊んでいる最中に変わる。
     *
     * ヒントの文言は変愚（`presentation_bridge.cpp:3155`）と同じにする。**幻想蛮怒だけ
     * 別の言い回しにしない**——同じ画面の同じ場所に出る同じ意味の文字だから。
     * この TU の日本語リテラルは CP932 で焼かれる（設計 §3.1）ので `sjis_to_utf8` を通す。
     */
    fill_term_palette(frame);
    {
        static const char kHint[] = "移動:十字/L  Select/F10:機能  A:決定  B:戻る";
        //! **英語ならカタログで引く**（フック #33）。この行は画面の下端に出るだけで
        //! Term を通らない。訳は変愚の英語（`presentation_bridge.cpp`）と同じ字にする。
        const char *hint = gb_tr(kHint); //GB: english layer #33
        frame.controller_hint = sjis_to_utf8(hint, std::strlen(hint));
    }

    fill_hud(frame);
    /*
     * 左の状態列・最下段・階層（P4）。**`menu_open` に関わらず毎回積む。**
     * 画面側は地図を出しているフレームでしか使わない（`game_hud.cpp:746` は
     * `frame_shows_map()` の後ろ）が、icky の 1 フレームで空にすると
     * メニューを閉じた直後の 1 枚だけ列が消える。中身の判断は `fill_status_col` が持つ。
     */
    fill_status_col(frame);
    /*
     * サブパネル 7 枚（S1）。**`menu_open` に関わらず毎回積む**——状態列と同じで、
     * メニューの 1 フレームで空にすると閉じた直後の 1 枚だけ枠が消える。
     * 中身はコアが `handle_stuff()` で描いたものをそのまま読む。
     */
    fill_sub_panels(frame);
    /*
     * 文字入力（S5・R2）。コアが**自由文字入力の待ち**に入っている間だけ立つ
     * ——`askfor_aux()` と、それを通らない自前ループ 3 つ（自動拾いエディタ本体・
     * その検索文字列・生い立ちの編集）を `gb_text_input_set` で囲んである。
     * **これが無いと日本語が 1 文字も打てない**
     * ——画面側は非 ASCII をこの旗のときだけ送る（`hd2d_app.cpp:11086`）。
     */
    frame.text_input_active = (gb_text_input_active() != 0);
    /*
     * カーソルとプロンプト（S2 後半・S4）。
     *
     * `fill_core_cursor()` はミラーの `》` を探す。ミラーが空（＝ゲーム中）なら
     * 何も見つからないので、ゲーム前画面の宣言（`g_pregame_choices`）とは
     * ぶつからない——あちらは `menu_choices`、こちらは `menu_core_cursors` で器も別。
     *
     * `fill_row0_prompt()` は **`menu_open` に関わらず**呼ぶ。`[y/n]` と数値入力は
     * icky にならないので、ゲーム中の地図の上に行 0 だけで出てくる。
     */
    const bool core_has_cursor = fill_core_cursor(frame);
    /*
     * 店・建物・品物選びの選択肢（R3）。**コアがカーソルを持っている画面では作らない**
     * ——二重のカーソルになる。ゲーム前画面（`g_pregame_choices`）が既に入っているときも
     * 触らない（`menu_choices` が空かどうかで見る）。
     */
    if (!core_has_cursor && frame.menu_choices.empty()) {
        fill_store_menu_choices(frame);
    }
    fill_row0_prompt(frame);
    /*
     * 戦闘の見せ場（S10 / R4）。**汲んだら消える**ので、capture を省いたフレームがあっても
     * 縁は落ちない（次の capture でまとめて出る）。溜まっているうちは`gb_change_digest()` が
     * 動くようにしてある（`gb_null_term.c`）ので、画が変わらなくても門を越える。
     */
    fill_combat_fx(frame);
    /* 効果音。見せ場と同じく汲んだら消える。 */
    fill_sounds(frame);
    /* 周囲の地形の内訳。 */
    fill_surroundings(frame);
    fill_messages(frame);
    fill_map(frame);
    fill_floor_and_lighting(frame);

    return frame;
}

} // namespace gb
