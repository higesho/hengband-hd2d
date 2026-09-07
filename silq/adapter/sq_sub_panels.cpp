/*!
 * @file sq_sub_panels.cpp
 * @brief `sq_sub_panels.h` の実装。
 *
 * ## 漢字（M1 と同じ扱い）
 * 旧 z-term に漢字属性は無く、2 バイト文字は**2 つのセルに割れて**入っている。
 * だから行は「セルのバイトをそのまま並べる」だけで正しい CP932 の並びになり、
 * それを 1 度だけ UTF-8 へ直す。1 セルずつ変換すると全角がすべて壊れる。
 *
 * 制御文字（`< 0x20` と `0x7F`）は空白に倒す。CP932 の 2 バイト目はどちらも取らない
 * （先行 `81-9F` / `E0-FC`、後続 `40-7E` と `80-FC`）ので、取り違えは起きない。
 */
#include "sq_sub_panels.h"

#include "sq_lang_c.h"
#include "sq_shim.h"
#include "sq_text.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sq {

namespace {

/*
 * サブ Term の枚数と `GameFrame::sub_panels` の枚数が食い違っていないか。
 *
 * **説明文は ASCII で書く。** `static_assert` の第 2 引数は「評価されない文字列」で、
 * Android 版は `tools/transcode_cp932_src.py` が日本語リテラルを 8 進の逃げ札へ翻すため、
 * ここに日本語を書くと組めなくなる（幻想蛮怒で 2026-08-21 に踏んだ）。
 */
static_assert(SQ_SUB_PANELS == kSubPanelCount, "SQ_SUB_PANELS must match GameFrame::sub_panels");

//! 1 行の受け皿（`SQ_SUB_MAX_COLS` は 255）。
constexpr int kRowBytes = 256;

//! 画面側が望む種類。-1 = UI 既定（＝コアには何も割り当てない）。
int g_want_kind[kSubPanelCount] = { -1, -1, -1, -1, -1, -1, -1 };

/*!
 * @brief `ui_state` で希望を**1 度でも受け取ったか**（2026-08-23）。
 * @details 受け取る前の `-1` は「UI 既定を望んでいる」ではなく「まだ知らない」である。
 * 区別しないと、握手からの数フレームでコア自身の既定を消してしまう。
 */
bool g_want_seen = false;

/*!
 * @name 最後にコアへ書いた希望
 * @details **希望が変わったときだけ書く**（`sq_sub_panels.h` の頭の註記）。
 * `kUnset` は「まだ 1 度も書いていない」。起動直後は必ず 1 度書く——画面側は
 * `hd2d.cfg` に覚えた割り当てを持っており、そちらが持ち主だからである。
 * @{
 */
constexpr int kUnset = -2; //!< -1 は「UI 既定」という**正しい値**なので使えない
int g_applied_kind[kSubPanelCount] = { kUnset, kUnset, kUnset, kUnset, kUnset, kUnset, kUnset };
/*! @} */

/*! 種類の名前（CP932 → UTF-8）。**使い回す**——毎フレーム 7 回変換するのは無駄。 */
const std::string &kind_label(int flag)
{
    static std::string cache[32];
    static bool filled[32] = {};
    static const std::string empty;

    if ((flag < 0) || (flag >= 32)) {
        return empty;
    }
    if (!filled[flag]) {
        char sys[128] = { 0 };
        const int n = sq_window_flag_name(flag, sys, static_cast<int>(sizeof(sys)));
        cache[flag] = (n > 0) ? sjis_to_utf8(sys, static_cast<std::size_t>(n)) : std::string();
        filled[flag] = true;
    }
    return cache[flag];
}

//! 制御文字を空白に倒す（`sq_frame.cpp` の `cell_is_blank` と同じ理由）。
char sane(char c)
{
    const auto u = static_cast<unsigned char>(c);
    if ((u < 0x20) || (u == 0x7F)) {
        return ' ';
    }
    return c;
}

//! 1 枚ぶんの文字を積む。末尾の空行は落とす（枠の下に空きが並ぶだけなので）。
void capture_lines(int panel, std::vector<SubPanelLine> &out)
{
    out.clear();

    int cols = 0;
    int rows = 0;
    if (sq_sub_term_size(panel, &cols, &rows) != SQ_OK) {
        return;
    }

    char text[kRowBytes];
    unsigned char attr[kRowBytes];
    for (int y = 0; y < rows; ++y) {
        const int n = sq_sub_term_row(panel, y, text, attr, kRowBytes);
        if (n < 0) {
            break;
        }

        std::string sys;
        sys.reserve(static_cast<std::size_t>(n));
        std::uint8_t color = 1;
        bool color_found = false;
        bool wide = false;
        for (int x = 0; x < n; ++x) {
            const char c = sane(text[x]);
            sys.push_back(c);
            if (static_cast<unsigned char>(c) >= 0x80u) {
                wide = true;
            }
            if (!color_found && (c != ' ')) {
                color = static_cast<std::uint8_t>(attr[x] & 0x0F);
                color_found = true;
            }
        }
        while (!sys.empty() && (sys.back() == ' ')) {
            sys.pop_back();
        }

        SubPanelLine line{};
        line.color = color;
        //! 純 ASCII の行は 1 バイトも触らない（設計 §1 制約 1）。
        line.text_utf8 = wide ? sjis_to_utf8(sys.data(), sys.size()) : sys;
        if (line.text_utf8.empty() && !sys.empty()) {
            line.text_utf8.assign(sys.size(), ' ');
        }
        out.push_back(std::move(line));
    }

    while (!out.empty() && out.back().text_utf8.empty()) {
        out.pop_back();
    }
}

} // namespace

presentation::SubPanelKindsMessage build_sub_panel_kinds()
{
    presentation::SubPanelKindsMessage message;

    /*
     * 先頭は必ず「UI 既定」（v1 §8.1）。名札はこちらで付ける——コアに
     * 「割り当てない」という種類は無いので、`window_flag_desc` からは出てこない。
     *
     * **`sq_tr()` を通す**（M1 追補 A4 と同じ道）。鍵は英語のままなので、
     * 日本語層が寝ていれば `UI default` がそのまま出る。
     */
    presentation::SubPanelKindWireEntry def;
    def.flag = -1;
    {
        //! 鍵は英語のまま。日本語層が寝ていれば `UI default` がそのまま出る。
        const char *const translated = sq_tr("UI default");
        bool wide = false;
        for (const char *p = translated; *p != 0; ++p) {
            if (static_cast<unsigned char>(*p) >= 0x80u) {
                wide = true;
                break;
            }
        }
        def.label_utf8 = wide ? sjis_to_utf8(translated, std::strlen(translated))
                              : std::string(translated);
    }
    message.entries.push_back(std::move(def));

    for (int flag = 0; flag < 32; ++flag) {
        const std::string &label = kind_label(flag);
        if (label.empty()) {
            continue; // 描画関数が無い（`window_stuff()` が呼ばない）か、予約領域
        }
        presentation::SubPanelKindWireEntry wire;
        wire.flag = flag;
        wire.label_utf8 = label;
        message.entries.push_back(std::move(wire));
    }
    return message;
}

void set_sub_panel_kind(int index, int flag)
{
    if ((index < 0) || (index >= kSubPanelCount)) {
        return;
    }
    g_want_kind[index] = flag;
    g_want_seen = true;
}

void reset_sub_panel_apply()
{
    for (int i = 0; i < kSubPanelCount; ++i) {
        g_applied_kind[i] = kUnset;
    }
}

void set_sub_panel_cells(int index, int cols, int rows)
{
    if ((index < 0) || (index >= kSubPanelCount)) {
        return;
    }
    sq_sub_term_set_cells(index, cols, rows);
}

void fill_sub_panels(GameFrame &frame)
{
    bool need_redraw = false;

    //! 本線 Term に種類を立てさせない（理屈は `sq_sub_clear_main_flags()` の註記）。
    if (sq_sub_clear_main_flags() != 0) {
        need_redraw = true;
    }

    for (int i = 0; i < kSubPanelCount; ++i) {
        SubPanelContent &content = frame.sub_panels[static_cast<std::size_t>(i)];

        /*
         * **希望を押し続ける**（2026-08-23。こう気づいた——「サブパネルのデフォルトが
         * 指示通りになってない」）。
         *
         * 前は「希望が変わったときだけ書く」だった。コア側で変えたぶん（`=`→窓）を
         * 踏み潰さないためだが、**それが利用者の設定を毎回食っていた**——コアは
         * 自分の都合で `window_flag[]` を丸ごと書き戻す場所を持っている
         * （`init2.c` の `init_other()` と `re_init_some_things()`、セーブの読み込み、
         * `toggle_inven_equip()`）。書き戻された値が実効値として画面へ流れ、
         * 画面はそれを「利用者がコア側で変えた」と読んで cfg へ焼く。
         * **どちらの向きの変更かを Sil-Q は名乗れない**（変愚の
         * `g_window_flags_generation` に当たるものが無い）ので、
         * **画面の cfg を持ち主と決める**——押し続ければ隙が無くなる。
         *
         * **失うものは無い。Sil-Q に窓割りを編集する画面は無い**（`cmd4.c:7216` の
         * 設定メニューに窓の頁が無い）。詳しくは `sq_sub_panels.h` の表。
         *
         * **希望を 1 度も受け取っていないうちは触らない**（`g_want_seen`）。
         * 握手の直後にコア自身の既定を消しても、誰の得にもならない。
         */
        const int want = sq_sub_sanitize_kind(g_want_kind[i]);
        if (g_want_seen && ((g_applied_kind[i] != want) || (sq_sub_current_kind(i) != want))) {
            g_applied_kind[i] = want;
            if (sq_sub_set_kind(i, want) != 0) {
                need_redraw = true;
            }
        }

        /*
         * **実効値はコアに立ったフラグ**である（v1 §7）。画面が望んだ番号をそのまま
         * 返さない——描画関数の無い番号やセーブに入っていた値は落ちるので、
         * 望みをそのまま返すと「選べたのに永久に空」になる。
         */
        const int kind = sq_sub_current_kind(i);
        if ((kind >= 0) && (sq_sub_term_apply_cells(i) != 0)) {
            need_redraw = true; // 桁数が変わった＝いま入っている絵は古い
        }

        content.kind = kind;
        if (kind < 0) {
            content.title_utf8.clear();
            content.lines.clear();
            continue;
        }
        content.title_utf8 = kind_label(kind);
        capture_lines(i, content.lines);
    }

    if (need_redraw) {
        sq_sub_request_redraw();
    }
}

} // namespace sq
