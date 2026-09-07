/*!
 * @file gb_sub_panels.cpp
 * @brief `gb_sub_panels.h` の実装。
 *
 * ## 漢字（設計 §3.1・V1）
 * 旧 z-term に漢字属性は無く、2 バイト文字は**2 つのセルに割れて**入っている。
 * だから行は「セルのバイトをそのまま並べる」だけで正しい SJIS の並びになり、
 * それを 1 度だけ UTF-8 へ直す。1 セルずつ変換すると全角がすべて壊れる。
 *
 * 制御文字（`< 0x20` と `0x7F`）は空白に倒す。SJIS の 2 バイト目はどちらも取らない
 * （先行 `81-9F` / `E0-EF`、後続 `40-7E` と `80-FC`）ので、取り違えは起きない。
 */
#include "gb_sub_panels.h"

#include "gb_lang_c.h" //GB: english layer #33 - the panel labels never reach Term, so #1 cannot see them
#include "gb_shim.h"
#include "gb_text.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gb {

namespace {

/*
 * サブ Term の枚数と `GameFrame::sub_panels` の枚数が食い違っていないか。
 *
 * **説明文は ASCII で書く。** `static_assert` の第 2 引数は「評価されない文字列」で、
 * C++26 以降そこに逃げ札を書けない（clang は既にエラーにする）。Android 版は
 * `tools/transcode_cp932_src.py` が日本語リテラルを 8 進の逃げ札へ翻すので、
 * ここに日本語を書くと**組めなくなる**（2026-08-21 に踏んだ）。あの道具も
 * 同じ形を見つけたら止めるようにしてある。
 */
static_assert(GB_SUB_PANELS == kSubPanelCount,
    "GB_SUB_PANELS must match GameFrame::sub_panels");

//! 1 行の受け皿。`GB_SUB_MAX_COLS`（255）＋ NUL。
constexpr int kRowBytes = 256;

//! 画面側が望む種類。-1 = UI 既定（＝コアには何も割り当てない）。
int g_want_kind[kSubPanelCount] = { -1, -1, -1, -1, -1, -1, -1 };

/*!
 * @name コア側で変えられた種類を抱え込む（R1。双方向）
 *
 * `=`→`w` の画面で割り当てを変えても、これが無いと**次のフレームで黙って戻る**
 * ——画面側がフラグの持ち主で、`fill_sub_panels()` が毎回 `g_want_kind` を当て直すため。
 * 操作はできるのに結果が消えるので、遊ぶ人からは壊れて見える。
 *
 * ## 見分けにフラグの差分を使ってはいけない
 * `window_flag[]` を書くのは遊ぶ側だけではない:
 *
 * | 誰が | いつ |
 * | --- | --- |
 * | `toggle_inven_equip()`（`object1.c:4036`） | 床の品物を選ぶたび・店。**持ち物↔装備を全窓で入れ替える** |
 * | `load.c` の読み込み | セーブを開いたとき。配列を丸ごと書く |
 * | `do_cmd_options_win()`（`cmd4.c:2215`） | **`=`→`w`。これだけが遊ぶ側の意図** |
 *
 * だから見るのは `gb_window_flags_generation()`（`cmd4.c` の `//GB:` 1 行が進める数）。
 *
 * ## 拾ったあとは画面が追いつくまで抱えておく
 * 画面まで往復するのに数フレーム掛かる。一度きりで渡すと、その間に届く
 * `ui_state`（まだ古い値）で元へ戻る。**画面が同じ値を送り返してくるまで抱える。**
 * @{
 */
constexpr int kNoOverride = -2; //!< -1 は「UI 既定」という**正しい値**なので使えない

int g_core_override[kSubPanelCount] = { kNoOverride, kNoOverride, kNoOverride, kNoOverride,
    kNoOverride, kNoOverride, kNoOverride };
unsigned int g_seen_generation = 0; //!< 前に見た世代。**増えていたら `=` で変えられた**

/*! @brief 世代が進んでいたら、いまコアに立っている種類を抱え込む。 */
void take_core_side_changes()
{
    const unsigned int gen = gb_window_flags_generation();
    if (gen == g_seen_generation) {
        return;
    }
    g_seen_generation = gen;
    for (int i = 0; i < kSubPanelCount; ++i) {
        /*
         * **整えてから抱える。**`gb_sub_set_kind()` は描画関数の無い番号を落とすので、
         * 生のまま抱えると画面へ返るのは -1 になり、画面がそれを送り返してきても
         * 抱えている番号と一致せず**手綱が永久に返らない**。
         */
        g_core_override[i] = gb_sub_sanitize_kind(gb_sub_current_kind(i));
    }
}
/*! @} */

/*! 種類の名前（SJIS → UTF-8）。**使い回す**——毎フレーム 7 回変換するのは無駄。 */
const std::string &kind_label(int flag)
{
    static std::string cache[32];
    static bool filled[32] = {};
    static const std::string empty;

    if ((flag < 0) || (flag >= 32)) {
        return empty;
    }
    if (!filled[flag]) {
        char sjis[128] = { 0 };
        const int n = gb_window_flag_name(flag, sjis, static_cast<int>(sizeof(sjis)));
        /*
         * **英語ならカタログで引く**（フック #33。GR-02 / GR-03）。
         * この名札は副画面の種類の一覧（`sub_panel_kinds`）と見出し（`title_utf8`）に
         * そのまま載り、**Term を 1 度も通らない**ので #1 では拾えない。
         * 引けなければ原文が返る（`gb_lang_c.h` の約束）ので、日本語のときは 1 ビットも変わらない。
         */
        const char *label = (n > 0) ? gb_tr(sjis) : ""; //GB: english layer #33
        cache[flag] = (n > 0) ? sjis_to_utf8(label, std::strlen(label)) : std::string();
        filled[flag] = true;
    }
    return cache[flag];
}

//! 制御文字を空白に倒す（`gb_frame.cpp` の `cell_is_blank` と同じ理由）。
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
    if (gb_sub_term_size(panel, &cols, &rows) != GB_OK) {
        return;
    }

    char text[kRowBytes];
    unsigned char attr[kRowBytes];
    for (int y = 0; y < rows; ++y) {
        const int n = gb_sub_term_row(panel, y, text, attr, kRowBytes);
        if (n < 0) {
            break;
        }

        std::string sjis;
        sjis.reserve(static_cast<std::size_t>(n));
        std::uint8_t color = 1;
        bool color_found = false;
        for (int x = 0; x < n; ++x) {
            const char c = sane(text[x]);
            sjis.push_back(c);
            if (!color_found && (c != ' ')) {
                color = static_cast<std::uint8_t>(attr[x] & 0x0F);
                color_found = true;
            }
        }
        while (!sjis.empty() && (sjis.back() == ' ')) {
            sjis.pop_back();
        }

        SubPanelLine line{};
        line.color = color;
        line.text_utf8 = sjis_to_utf8(sjis.data(), sjis.size());
        out.push_back(std::move(line));
    }

    while (!out.empty() && out.back().text_utf8.empty()) {
        out.pop_back();
    }
}

/*!
 * @brief **UI 既定パネル**の中身（`sub2_lines` / `sub3_lines` / `sub5_lines`）。
 * @details サブウィンドウ（S1）を 1 枚も割り当てていないとき、画面はこの 3 つで
 * 装備・視界の敵・持ち物を出す（`hd2d/ui/game_hud.cpp` の `draw_default_sub_panel`）。
 * **埋めないと枠だけが並ぶ。**変愚は `presentation_bridge.cpp:163` 以降で同じことを
 * している。上限もあちらに合わせた（8 / 8 / 12）。
 */
void fill_default_panel_lines(GameFrame &frame)
{
    constexpr int kSub2Max = 8;
    constexpr int kSub3Max = 8;
    constexpr int kSub5Max = 12;
    char buf[512];

    frame.sub2_lines.clear();
    for (int i = 0; i < kSub2Max; ++i) {
        const int n = gb_read_item_line(GB_ITEMS_EQUIPMENT, i, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            break;
        }
        frame.sub2_lines.push_back(sjis_to_utf8(buf, static_cast<std::size_t>(n)));
    }

    frame.sub3_lines.clear();
    for (int i = 0; i < kSub3Max; ++i) {
        const int n = gb_read_visible_monster(i, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            break;
        }
        frame.sub3_lines.push_back(sjis_to_utf8(buf, static_cast<std::size_t>(n)));
    }

    frame.sub5_lines.clear();
    for (int i = 0; i < kSub5Max; ++i) {
        const int n = gb_read_item_line(GB_ITEMS_INVENTORY, i, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            break;
        }
        frame.sub5_lines.push_back(sjis_to_utf8(buf, static_cast<std::size_t>(n)));
    }
}

} // namespace

presentation::SubPanelKindsMessage build_sub_panel_kinds()
{
    presentation::SubPanelKindsMessage message;

    /*
     * 先頭は必ず「UI 既定」（v1 §8.1）。名札はこちらで付ける——コアに
     * 「割り当てない」という種類は無いので、`window_flag_desc` からは出てこない。
     */
    presentation::SubPanelKindWireEntry def;
    def.flag = -1;
    {
        static const char kLabel[] = "UI 既定";
        const char *label = gb_tr(kLabel); //GB: english layer #33
        def.label_utf8 = sjis_to_utf8(label, std::strlen(label));
    }
    message.entries.push_back(std::move(def));

    for (int flag = 0; flag < 32; ++flag) {
        const std::string &label = kind_label(flag);
        if (label.empty()) {
            continue; // 描画関数が無い（記念撮影・ボーグ）か、予約領域
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
}

void set_sub_panel_cells(int index, int cols, int rows)
{
    if ((index < 0) || (index >= kSubPanelCount)) {
        return;
    }
    gb_sub_term_set_cells(index, cols, rows);
}

void fill_sub_panels(GameFrame &frame)
{
    bool need_redraw = false;
    //! 割り当てが 1 枚も無いときに出る中身。**枠だけを並べない**ため必ず埋める。
    fill_default_panel_lines(frame);

    /*
     * 本線 Term（`angband_term[0]`）に種類を立てさせない。`fix_inven()` ほかは
     * `j = 0..7` を走査する（`xtra1.c:2523`）ので、0 番に印が立っていると
     * **持ち物一覧が地図の上に描かれる**。0 番は地図そのものである。
     *
     * `=`→`w` の画面からは立てられない（`cmd4.c:2345` の `if (x == 0) break;`）。
     * 立ちうるのは**セーブに入っていた値**——本物の Windows 版で遊んだセーブを
     * 開くと、あちらの窓割りがそのまま載ってくる。
     */
    if (gb_sub_clear_main_flags() != 0) {
        need_redraw = true;
    }
    //! コア側で変えられた種類（`=`→`w`）を抱え込む。**下の輪より先に見る。**
    take_core_side_changes();

    for (int i = 0; i < kSubPanelCount; ++i) {
        SubPanelContent &content = frame.sub_panels[static_cast<std::size_t>(i)];
        int want = g_want_kind[i];

        if (g_core_override[i] != kNoOverride) {
            if (want == g_core_override[i]) {
                g_core_override[i] = kNoOverride; //!< 画面が追いついた。手綱を返す
            } else {
                want = g_core_override[i]; //!< まだ。**画面の古い値で戻さない**
            }
        }

        if (gb_sub_set_kind(i, want) != 0) {
            need_redraw = true;
        }
        /*
         * **実効値はコアに立ったフラグ**である。画面が望んだ番号をそのまま返さない
         * ——描画関数の無い番号（記念撮影・ボーグ）や壊れた設定は `gb_sub_set_kind()` が
         * 落とすので、望みをそのまま返すと「選べたのに永久に空」になる。
         */
        const int kind = gb_sub_current_kind(i);
        if ((kind >= 0) && (gb_sub_term_apply_cells(i) != 0)) {
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
        gb_sub_request_redraw();
    }
}

} // namespace gb
