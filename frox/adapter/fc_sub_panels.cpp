/*!
 * @file fc_sub_panels.cpp
 * @brief `fc_sub_panels.h` の実装。
 *
 * ## 文字コード
 * **M0 の Frox は純 ASCII** なので変換は無い。制御文字（`< 0x20` と `0x7F`）だけ
 * 空白に倒す（地図のタイル印が紛れ込む穴。`gb_frame.cpp` の `cell_is_blank` と同じ理由）。
 * M1 から CP932 が入るので、行の組み立てに `fc_text.h` の変換を 1 度だけ通す
 * （J3 で入れた。**PNG を目で見て見つけた**——ここを忘れると品名が □ になる。
 * 設計 §3.2 の表の 5 つ目である）。
 */
#include "fc_sub_panels.h"

#include "fc_text.h" //!< CP932 → UTF-8（設計 §3.1）

#include "fc_lang_c.h" //!< 名札の訳（M1 J4。fc_tr は CP932 で返す）

#include "fc_shim.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fc {

namespace {

/*
 * サブ Term の枚数と `GameFrame::sub_panels` の枚数が食い違っていないか。
 * 説明文は ASCII で書く（Android の変換ツリーが日本語リテラルを翻すため）。
 */
static_assert(FC_SUB_PANELS == kSubPanelCount, "FC_SUB_PANELS must match GameFrame::sub_panels");

//! 1 行の受け皿（`FC_SUB_MAX_COLS` は 255）。
constexpr int kRowBytes = 256;

//! 画面側が望む種類。-1 = UI 既定（＝コアには何も割り当てない）。
int g_want_kind[kSubPanelCount] = { -1, -1, -1, -1, -1, -1, -1 };

/*!
 * @brief `ui_state` で希望を**1 度でも受け取ったか**。
 * @details 受け取る前の `-1` は「UI 既定を望んでいる」ではなく「まだ知らない」である。
 * 区別しないと、握手からの数フレームでコア自身の既定を消してしまう。
 */
bool g_want_seen = false;

/*!
 * @name 最後にコアへ書いた希望
 * @details `kUnset` は「まだ 1 度も書いていない」。-1 は「UI 既定」という
 * **正しい値**なので使えない。
 * @{
 */
constexpr int kUnset = -2;
int g_applied_kind[kSubPanelCount] = { kUnset, kUnset, kUnset, kUnset, kUnset, kUnset, kUnset };
/*! @} */

/*! 種類の名前。**使い回す**——毎フレーム 7 回読むのは無駄。M0 は ASCII のまま。 */
const std::string &kind_label(int flag)
{
    static std::string cache[32];
    static bool filled[32] = {};
    static const std::string empty;

    if ((flag < 0) || (flag >= 32)) {
        return empty;
    }
    if (!filled[flag]) {
        char text[128] = { 0 };
        const int n = fc_window_flag_name(flag, text, static_cast<int>(sizeof(text)));
        cache[flag] = (n > 0) ? std::string(text, static_cast<std::size_t>(n)) : std::string();
        filled[flag] = true;
    }
    return cache[flag];
}

//! 制御文字を空白に倒す（`fc_frame.cpp` の `cell_is_blank` と同じ理由）。
char sane(char c)
{
    const auto u = static_cast<unsigned char>(c);
    if ((u < 0x20) || (u == 0x7F)) {
        return ' ';
    }
    return c;
}

/*!
 * @brief コアの字（CP932）→ 線の字（UTF-8）。`fc_frame.cpp` の `to_utf8` と同じ約束。
 * @details **純 ASCII なら 1 バイトも触らない**（英語のときの答えを M0 と変えない）。
 * 変換できなければ**落とさずにそのまま運ぶ**。
 */
std::string sub_to_utf8(const char *bytes, std::size_t len)
{
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

inline std::string sub_to_utf8(const std::string &s) { return sub_to_utf8(s.data(), s.size()); }

//! 1 枚ぶんの文字を積む。末尾の空行は落とす（枠の下に空きが並ぶだけなので）。
void capture_lines(int panel, std::vector<SubPanelLine> &out)
{
    out.clear();

    int cols = 0;
    int rows = 0;
    if (fc_sub_term_size(panel, &cols, &rows) != FC_OK) {
        return;
    }

    char text[kRowBytes];
    unsigned char attr[kRowBytes];
    for (int y = 0; y < rows; ++y) {
        const int n = fc_sub_term_row(panel, y, text, attr, kRowBytes);
        if (n < 0) {
            break;
        }

        SubPanelLine line;
        std::string body; // CP932 のまま組み、最後に 1 回だけ変換する（設計 §3.2）
        body.reserve(static_cast<std::size_t>(n));
        bool color_found = false;
        int end = n;
        while ((end > 0) && (sane(text[end - 1]) == ' ')) {
            --end;
        }
        for (int x = 0; x < end; ++x) {
            const auto lead = static_cast<unsigned char>(text[x]);
            if (fc::is_sjis_lead(lead) && ((x + 1) < end)) {
                body.push_back(text[x]);
                body.push_back(text[x + 1]);
                if (!color_found) {
                    line.color = attr[x];
                    color_found = true;
                }
                ++x;
                continue;
            }
            if (fc::is_sjis_lead(lead)) {
                body.push_back(' '); // 行の端で割れた。**半端なバイトは運ばない**
                continue;
            }
            const char c = sane(text[x]);
            body.push_back(c);
            if (!color_found && (c != ' ')) {
                line.color = attr[x];
                color_found = true;
            }
        }
        line.text_utf8 = sub_to_utf8(body);
        out.push_back(std::move(line));
    }

    while (!out.empty() && out.back().text_utf8.empty()) {
        out.pop_back();
    }
}

/*!
 * @brief 矢筒の枚を埋める（**コアの Term を使わない**。`fc_sub_panels.h` の頭）。
 * @details 中身は `fc_read_item_line(FC_ITEMS_QUIVER, ...)`。空なら「(empty)」を
 * 1 行だけ出す——空の枠と壊れた枠を画面で見分けられるように。
 */
void capture_quiver_lines(std::vector<SubPanelLine> &out)
{
    out.clear();

    char text[kRowBytes];
    for (int index = 0;; ++index) {
        const int n = fc_read_item_line(FC_ITEMS_QUIVER, index, text, kRowBytes);
        if (n <= 0) {
            break;
        }
        SubPanelLine line;
        line.color = 1;
        line.text_utf8 = sub_to_utf8(text, static_cast<std::size_t>(n));
        out.push_back(std::move(line));
        if (index >= 64) {
            break; // 矢筒の枠は 26 が上限のはず。桁違いなら打ち切る
        }
    }

    if (out.empty()) {
        SubPanelLine line;
        line.color = 8; // 灰
        line.text_utf8 = "(empty)";
        out.push_back(std::move(line));
    }
}

/*!
 * @brief **UI 既定パネル**の中身（`sub2_lines` / `sub3_lines` / `sub5_lines`）。
 * @details サブウィンドウを 1 枚も割り当てていないとき、画面はこの 3 つで
 * 装備・視界の敵・持ち物を出す（`hd2d/ui/game_hud.cpp` の `draw_default_sub_panel`）。
 * **埋めないと枠だけが並ぶ**（P8 の実機の絵で「装備」「持ち物」が空だった）。
 * 上限は幻想蛮怒と同じ（8 / 8 / 12）。M1 から CP932 が来るので変換を通す。
 */
void fill_default_panel_lines(GameFrame &frame)
{
    constexpr int kSub2Max = 8;
    constexpr int kSub3Max = 8;
    constexpr int kSub5Max = 12;
    char buf[512];

    frame.sub2_lines.clear();
    for (int i = 0; i < kSub2Max; ++i) {
        const int n = fc_read_item_line(FC_ITEMS_EQUIPMENT, i, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            break;
        }
        frame.sub2_lines.push_back(sub_to_utf8(buf, static_cast<std::size_t>(n)));
    }

    frame.sub3_lines.clear();
    for (int i = 0; i < kSub3Max; ++i) {
        const int n = fc_read_visible_monster(i, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            break;
        }
        frame.sub3_lines.push_back(sub_to_utf8(buf, static_cast<std::size_t>(n)));
    }

    frame.sub5_lines.clear();
    for (int i = 0; i < kSub5Max; ++i) {
        const int n = fc_read_item_line(FC_ITEMS_INVENTORY, i, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            break;
        }
        frame.sub5_lines.push_back(sub_to_utf8(buf, static_cast<std::size_t>(n)));
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
     * **名札は訳の層を通す**（M1 J4。設計 §4 追記 (d) の後で入れた）。
     * 画面は種類を `flag` の番号で突き合わせるので、名札は表示専用——
     * 訳しても結線は壊れない。`fc_tr` は CP932 で返すから UTF-8 へ直してから載せる。
     */
    presentation::SubPanelKindWireEntry def;
    def.flag = -1;
    def.label_utf8 = sub_to_utf8(fc_tr("UI default"));
    message.entries.push_back(std::move(def));

    for (int flag = 0; flag < 32; ++flag) {
        const std::string &label = kind_label(flag);
        if (label.empty()) {
            continue; // 描画関数が無い（`window_stuff()` が呼ばない）か、予約領域
        }
        presentation::SubPanelKindWireEntry wire;
        wire.flag = flag;
        wire.label_utf8 = sub_to_utf8(fc_tr(label.c_str()));
        message.entries.push_back(std::move(wire));
    }

    /*
     * **矢筒**（設計 §3.4 / §4.4。Frox 固有）。`window_flag` のビットではなく
     * アダプタが自分で描く種。番号は 32 ビットと当たらない 1000。
     */
    presentation::SubPanelKindWireEntry quiver;
    quiver.flag = FC_SUB_KIND_QUIVER;
    quiver.label_utf8 = sub_to_utf8(fc_tr("Quiver"));
    message.entries.push_back(std::move(quiver));

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
    fc_sub_term_set_cells(index, cols, rows);
}

void fill_sub_panels(GameFrame &frame)
{
    bool need_redraw = false;

    //! UI 既定パネルの中身（割り当ての無い枠が使う）。
    fill_default_panel_lines(frame);

    //! 本線 Term に種類を立てさせない（理屈は `fc_sub_clear_main_flags()` の註記）。
    if (fc_sub_clear_main_flags() != 0) {
        need_redraw = true;
    }

    for (int i = 0; i < kSubPanelCount; ++i) {
        SubPanelContent &content = frame.sub_panels[static_cast<std::size_t>(i)];

        /*
         * **矢筒はコアの Term を通らない**（`fc_sub_panels.h` の頭）。その枚の
         * `window_flag` は空（-1）に保ち、中身はこちらで積む。
         */
        const bool quiver = g_want_seen && (g_want_kind[i] == FC_SUB_KIND_QUIVER);

        /*
         * **希望を押し続ける**（Sil-Q と同じ判断。「サブパネルの持ち主は
         * 画面の cfg」）。コアは起動・セーブの読み込み・`toggle_inven_equip()` で
         * `window_flag[]` を丸ごと書き戻すが、どちらの向きの変更かを名乗れないので、
         * 画面の cfg を持ち主と決めて、実効値が希望と違えば毎フレーム書き直す。
         *
         * **希望を 1 度も受け取っていないうちは触らない**（`g_want_seen`）。
         * 握手の直後にコア自身の既定を消しても、誰の得にもならない。
         */
        const int want = quiver ? -1 : fc_sub_sanitize_kind(g_want_kind[i]);
        if (g_want_seen && ((g_applied_kind[i] != want) || (fc_sub_current_kind(i) != want))) {
            g_applied_kind[i] = want;
            if (fc_sub_set_kind(i, want) != 0) {
                need_redraw = true;
            }
        }

        if (quiver) {
            content.kind = FC_SUB_KIND_QUIVER;
            content.title_utf8 = sub_to_utf8(fc_tr("Quiver"));
            capture_quiver_lines(content.lines);
            continue;
        }

        /*
         * **実効値はコアに立ったフラグ**である（v1 §7）。画面が望んだ番号をそのまま
         * 返さない——描画関数の無い番号やセーブに入っていた値は落ちるので、
         * 望みをそのまま返すと「選べたのに永久に空」になる。
         */
        const int kind = fc_sub_current_kind(i);
        if ((kind >= 0) && (fc_sub_term_apply_cells(i) != 0)) {
            need_redraw = true; // 桁数が変わった＝いま入っている絵は古い
        }

        content.kind = kind;
        if (kind < 0) {
            content.title_utf8.clear();
            content.lines.clear();
            continue;
        }
        content.title_utf8 = sub_to_utf8(fc_tr(kind_label(kind).c_str()));
        capture_lines(i, content.lines);
    }

    if (need_redraw) {
        fc_sub_request_redraw();
    }
}

} // namespace fc
