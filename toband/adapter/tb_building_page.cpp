/*!
 * @file tb_building_page.cpp
 * @brief 建物の結果のページ（`tb_building_page.h`）。
 *
 * ## 見分け（Term ミラーの形。日本語でも英語でも同じ形）
 * どれも行 23 に建物の画面の `ESC) 建物を出る`（英語は `ESC) Exit building`）が残っている（アイテム情報の枠だけは左の 13 桁だけ残るので、
 * 頭の `ESC) 建物` まで）。さらに次の形:
 *
 * | ページ | 原作 | 見分け |
 * | --- | --- | --- |
 * | 賞金首リスト | `bldg.c` `shoukinkubi()` | 行 0 が `何かキーを押してください`（英語 `Hit any key.`）で、行 6 が `現在の賞金首`（`Wanted monsters`） |
 * | 武器の比較 | `compare_weapons()` | 行 0 が `現在の技量から判断すると…`（`Based on your current abilities…`） |
 * | 防御について調べる | `eval_ac()` | 行 0 が `現在のあなたの装備からすると…`（`Defense abilities from your current Armor Class…`） |
 * | 勇者の殿堂 | `scores.c` `show_highclass()` | 行 0 が `何かキーを押すとゲームに戻ります`（`Hit any key to continue`） |
 * | アイテムの情報 | `object1.c` `screen_object()` | 桁 13 以降に `[何かキーを押すとゲームに戻ります]`（`[Press any key to continue]`）だけの行がある |
 *
 * ## 窓の作り
 * 共用の窓は、この画面を「建物の選択肢」（行 19〜23 の一覧）として組む。結果のページでは一覧が効かない（どのキーも
 * 「任意のキー」になる）ので、`capture()` の前に選択肢を空にして、`capture()` に「決定だけの詳細画面」を作らせる。
 * 後で題と本文だけ置き換える（建物の主の名前・一覧・所持金は載せない）。入力は共用の `input()` がそのまま翻訳する
 * （決定は `continue_key`＝Enter、取消は ESC、文字キーはそのまま。どれも原作の `inkey()` が受ける）。
 */
#include "tb_building_page.h"

#include "frame/message_more.h"
#include "tb_term_util.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace tb {

namespace {

using namespace term;

enum class Page {
    None,
    Wanted,     //!< 賞金首リストの頁送り
    Compare,    //!< 武器の比較
    ArmorEval,  //!< 防御について調べる
    HallOfFame, //!< 勇者の殿堂
    ItemInfo,   //!< アイテム調査・鑑定の情報
};

//! 直近の `prepare_building_page()` の判定。`apply_building_page_window()` が読む。
Page g_page = Page::None;

constexpr int kFooterFirstRow = 19; //!< 建物の行動の一覧（原作の `19 + i/2`）
constexpr int kLastRow = 22;        //!< 結果を描く最後の行（行 23 は建物の `ESC) 建物を出る`）
constexpr int kInfoColumn = 13;     //!< `screen_object()` の枠の左端（原作は列 13 から消して 15 から描く）

using Lines = std::vector<std::string>;

/*! @brief 行 `row` の桁 `from` 以降の字を、空白をつめて返す（無ければ空）。 */
std::string row_text(const GameFrame &frame, int row, int from = 0)
{
    return presentation::modern_spaces(slice_columns(text_at_row(frame, row), from));
}

/*! @brief 建物の画面の足（`ESC) 建物を出る`）が下に残っているか。枠に切られて途中までのときもある。 */
bool has_building_footer(const GameFrame &frame)
{
    for (const auto &line : frame.menu_term_lines) {
        if (line.source_row < kFooterFirstRow) {
            continue;
        }
        const std::string text = trim(line.text_utf8);
        if (starts_with(text, "ESC)") && (contains(text, u8c(u8"建物")) || contains(text, "Exit") || contains(text, "building"))) {
            return true;
        }
    }
    return false;
}

bool is_info_any_key(const std::string &text)
{
    if (text == u8c(u8"[何かキーを押すとゲームに戻ります]") || text == "[Press any key to continue]") {
        return true;
    }
    // 英語層に出る字は、原作の英語の枝（`[Press any key to continue]`）ではなく、日本語の枝を訳のカタログで引いた後の字
    // （`[Press any key to return to the game]`。実機で確かめた）。訳は変わりうるので、`[` ～ `any key` ～ `]` の形で見る。
    return text.size() > 9 && text.front() == '[' && text.back() == ']' && text.find("any key") != std::string::npos;
}

Page classify(const GameFrame &frame)
{
    // 窓がすでにある（店の窓など）・入力欄・数の入力・確認・`-続く-` の待ちの間は触らない
    if (!frame.menu_open || !frame.modern.stage.empty() || frame.text_input_active || frame.numeric.active
        || !frame.prompt.choices.empty() || presentation::message_more_pending(frame) || frame.menu_term_lines.empty()) {
        return Page::None;
    }
    if (!has_building_footer(frame)) {
        return Page::None;
    }
    const std::string row0 = trim(text_at_row(frame, 0));
    const std::string row6 = trim(text_at_row(frame, 6));
    if ((row0 == u8c(u8"何かキーを押してください") || row0 == "Hit any key.")
        && (row6 == u8c(u8"現在の賞金首") || row6 == "Wanted monsters")) {
        return Page::Wanted;
    }
    if (starts_with(row0, u8c(u8"現在の技量から判断すると")) || starts_with(row0, "Based on your current abilities")) {
        return Page::Compare;
    }
    if (starts_with(row0, u8c(u8"現在のあなたの装備からすると")) || starts_with(row0, "Defense abilities from your current Armor Class")) {
        return Page::ArmorEval;
    }
    if (row0 == u8c(u8"何かキーを押すとゲームに戻ります") || row0 == "Hit any key to continue") {
        return Page::HallOfFame;
    }
    for (const auto &line : frame.menu_term_lines) {
        if (line.source_row >= 1 && is_info_any_key(trim(slice_columns(line.text_utf8, kInfoColumn)))) {
            return Page::ItemInfo;
        }
    }
    return Page::None;
}

// ---------------------------------------------------------------------------------------------------------------------
// 本文の部品
// ---------------------------------------------------------------------------------------------------------------------
//! `見出し: 値` に割る。値が空のとき（見出しだけの行）・割れないときは false。
bool split_label(const std::string &line, std::string &label, std::string &value)
{
    size_t at = line.find(':');
    size_t width = 1;
    const size_t full = line.find(u8c(u8"："));
    if (full != std::string::npos && (at == std::string::npos || full < at)) {
        at = full;
        width = std::string(u8c(u8"：")).size();
    }
    if (at == std::string::npos || at == 0) {
        return false;
    }
    label = trim(line.substr(0, at));
    value = trim(line.substr(at + width));
    return !label.empty() && !value.empty() && label.size() <= 40;
}

/*! @brief 行の並びを、`見出し: 値`（fields）と文（paragraph）のまとまりにして `out` へ足す。先頭のまとまりに `title` を付ける。 */
void add_flow(std::vector<ModernBlock> &out, const Lines &lines, const std::string &title = std::string())
{
    int current = -1;
    bool titled = title.empty();
    for (const std::string &line : lines) {
        if (line.empty()) {
            continue;
        }
        std::string label, value;
        const bool field = split_label(line, label, value);
        const char *kind = field ? "fields" : "paragraph";
        if (current < 0 || out[static_cast<size_t>(current)].kind != kind) {
            ModernBlock block;
            block.kind = kind;
            if (!titled) {
                block.title = title;
                titled = true;
            }
            out.push_back(std::move(block));
            current = static_cast<int>(out.size()) - 1;
        }
        ModernBlock &block = out[static_cast<size_t>(current)];
        block.cells.push_back(field ? ModernCell{ label, value, false, "", "" } : ModernCell{ "", line, false, "", "" });
    }
}

//! 行 `from`〜`to` の字（桁 `column` 以降。空の行は除く）。
Lines rows_of(const GameFrame &frame, int from, int to, int column = 0)
{
    Lines lines;
    for (int row = from; row <= to; ++row) {
        const std::string text = row_text(frame, row, column);
        if (!text.empty()) {
            lines.push_back(text);
        }
    }
    return lines;
}

//! 注釈の段落: 空の行で区切り、行を継ぐ（日本語は詰め、英語の語の間は空白 1 つ）。
void add_notes(std::vector<ModernBlock> &out, const GameFrame &frame, int from, int to)
{
    ModernBlock block;
    block.kind = "paragraph";
    std::string paragraph;
    const auto flush = [&]() {
        if (!paragraph.empty()) {
            block.cells.push_back({ "", paragraph, false, "", "" });
            paragraph.clear();
        }
    };
    for (int row = from; row <= to; ++row) {
        const std::string text = row_text(frame, row);
        if (text.empty()) {
            flush();
            continue;
        }
        if (!paragraph.empty() && static_cast<unsigned char>(paragraph.back()) < 128 && static_cast<unsigned char>(text.front()) < 128) {
            paragraph += ' ';
        }
        paragraph += text;
    }
    flush();
    if (!block.cells.empty()) {
        out.push_back(std::move(block));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ページごとの本文
// ---------------------------------------------------------------------------------------------------------------------
/*! @brief 防御について調べる（`eval_ac()`）。行 0 に案内、行 2・3 に AC と軽減率、行 5〜7 に敵のレベルごとの回避率・期待値、行 9〜 に説明。 */
std::vector<ModernBlock> compose_armor(const GameFrame &frame, bool ja)
{
    std::vector<ModernBlock> out;
    const std::string intro = row_text(frame, 0);
    if (!intro.empty()) {
        ModernBlock block;
        block.kind = "paragraph";
        block.cells.push_back({ "", intro, false, "", "" });
        out.push_back(std::move(block));
    }
    add_flow(out, rows_of(frame, 2, 3));
    // 行 5〜7: `敵のレベル      :   0   10 … 100`。値は 11 個ずつ。表（敵のレベル・回避率・ダメージ期待値）にする。
    std::string labels[3];
    std::vector<std::string> values[3];
    bool table = true;
    for (int i = 0; i < 3; ++i) {
        const std::string text = text_at_row(frame, 5 + i);
        const size_t colon = text.find(':');
        if (colon == std::string::npos) {
            table = false;
            break;
        }
        labels[i] = trim(text.substr(0, colon));
        values[i] = tokens_of(text.substr(colon + 1));
        if (labels[i].empty() || values[i].empty() || values[i].size() != values[0].size()) {
            table = false;
            break;
        }
    }
    if (table) {
        ModernBlock head;
        head.kind = "table_header";
        for (int i = 0; i < 3; ++i) {
            head.cells.push_back({ "", labels[i], false, "", "" });
        }
        out.push_back(std::move(head));
        for (size_t c = 0; c < values[0].size(); ++c) {
            ModernBlock row;
            row.kind = "table_row";
            for (int i = 0; i < 3; ++i) {
                row.cells.push_back({ "", values[i][c], false, "", "" }); // 見出しは表の頭にあるので、行には付けない
            }
            out.push_back(std::move(row));
        }
    } else {
        add_flow(out, rows_of(frame, 5, 7));
    }
    add_notes(out, frame, 9, kLastRow);
    (void)ja;
    return out;
}

/*! @brief 武器の比較（`compare_weapons()`）。左の欄（桁 0〜39）と右の欄（桁 40〜）に 1 本ずつ。注釈は桁 0 から始まる 1 行（`(` で始まる）。 */
std::vector<ModernBlock> compose_compare(const GameFrame &frame, bool ja)
{
    const auto tr = [&](const char *jp, const char *en) { return std::string(ja ? jp : en); };
    std::vector<ModernBlock> out;
    const std::string intro = row_text(frame, 0);
    if (!intro.empty()) {
        ModernBlock block;
        block.kind = "paragraph";
        block.cells.push_back({ "", intro, false, "", "" });
        out.push_back(std::move(block));
    }
    Lines left, right, notes;
    for (int row = 1; row <= kLastRow; ++row) {
        const std::string whole = text_at_row(frame, row);
        if (trim(whole).empty()) {
            continue;
        }
        if (!whole.empty() && whole.front() == '(') {
            notes.push_back(presentation::modern_spaces(whole)); // 全幅の注釈
            continue;
        }
        const std::string l = presentation::modern_spaces(slice_columns(whole, 0, 40));
        const std::string r = presentation::modern_spaces(slice_columns(whole, 40));
        if (!l.empty()) {
            left.push_back(l);
        }
        if (!r.empty()) {
            right.push_back(r);
        }
    }
    // 1 行目は武器の名前。残りは行のまま並べる。
    // 名前を見出し（block.title）にしない: 詳細の頁は見出しが 2 つ以上あると左の一覧が見出しの行になり、決定が頁の先頭へ移るだけで
    // 原作へ届かなくなる（`hd2d/ui/modern_store_ui.cpp` の `navigation_page_`）。名前は `見出し: 値` の行にする。
    int index = 0;
    for (const Lines *column : { &left, &right }) {
        ++index;
        if (column->empty()) {
            continue;
        }
        ModernBlock name;
        name.kind = "fields";
        const std::string label = index == 1 ? tr(u8c(u8"第一の武器"), "First weapon") : tr(u8c(u8"第二の武器"), "Second weapon");
        name.cells.push_back({ label, column->front(), false, "", "" });
        out.push_back(std::move(name));
        if (column->size() > 1) {
            ModernBlock lines;
            lines.kind = "paragraph";
            for (size_t i = 1; i < column->size(); ++i) {
                lines.cells.push_back({ "", (*column)[i], false, "", "" });
            }
            out.push_back(std::move(lines));
        }
    }
    if (!notes.empty()) {
        ModernBlock block;
        block.kind = "paragraph";
        for (const auto &note : notes) {
            block.cells.push_back({ "", note, false, "", "" });
        }
        out.push_back(std::move(block));
    }
    return out;
}

/*! @brief 賞金首リストの 1 頁（`shoukinkubi()`）。行 4 に案内、行 6 に見出し、行 7〜16 に `名前 ---- $金額`（倒したものは `済`）。 */
std::vector<ModernBlock> compose_wanted(const GameFrame &frame, bool ja)
{
    std::vector<ModernBlock> out;
    const std::string offer = row_text(frame, 4);
    if (!offer.empty()) {
        ModernBlock block;
        block.kind = "paragraph";
        block.cells.push_back({ "", offer, false, "", "" });
        out.push_back(std::move(block));
    }
    const std::string heading = row_text(frame, 6);
    std::vector<std::pair<std::string, std::string>> entries;
    bool parsed = true;
    for (int row = 7; row <= 16; ++row) {
        const std::string text = text_at_row(frame, row);
        if (trim(text).empty()) {
            continue;
        }
        const size_t dash = text.find("---");
        if (dash == std::string::npos) {
            parsed = false;
            break;
        }
        std::string prize = trim(text.substr(dash));
        const size_t first = prize.find_first_not_of('-');
        prize = first == std::string::npos ? std::string() : trim(prize.substr(first));
        entries.emplace_back(presentation::modern_spaces(text.substr(0, dash)), prize);
    }
    if (!parsed || entries.empty()) {
        add_flow(out, rows_of(frame, 5, 16));
        return out;
    }
    ModernBlock head;
    head.kind = "table_header";
    head.title = heading;
    head.cells.push_back({ "", ja ? u8c(u8"賞金首") : "Target", false, "", "" });
    head.cells.push_back({ "", ja ? u8c(u8"賞金") : "Prize", false, "", "" });
    out.push_back(std::move(head));
    for (const auto &entry : entries) {
        ModernBlock row;
        row.kind = "table_row";
        row.cells.push_back({ "", entry.first, false, "", "" });
        row.cells.push_back({ "", entry.second, false, "", "" });
        out.push_back(std::move(row));
    }
    return out;
}

} // namespace

bool prepare_building_page(GameFrame &frame)
{
    g_page = classify(frame);
    if (g_page == Page::None) {
        return false;
    }
    if (g_page != Page::ItemInfo) {
        // 押しても効かない建物の行動の一覧。残すと左に「建物を出る」だけが残り、選ぶと任意のキーとして ESC が届く。
        frame.menu_choices.clear();
    }
    return true;
}

bool apply_building_page_window(GameFrame &frame)
{
    const Page page = g_page;
    g_page = Page::None;
    ModernWindow &w = frame.modern;
    // `prepare_building_page()` が真だったあと、`capture()` が「決定だけの詳細画面」を作ったときだけ組み替える。
    if (page == Page::None || w.stage != "detail" || w.kind != "dialog" || w.message_only) {
        return false;
    }
    const bool ja = w.language == "ja";
    const auto tr = [&](const char *jp, const char *en) { return std::string(ja ? jp : en); };
    std::string title;
    std::vector<ModernBlock> content;
    switch (page) {
    case Page::Wanted:
        title = tr(u8c(u8"賞金首リスト"), "Wanted unique list");
        content = compose_wanted(frame, ja);
        break;
    case Page::Compare:
        title = tr(u8c(u8"武器を比較する"), "Compare weapons");
        content = compose_compare(frame, ja);
        break;
    case Page::ArmorEval:
        title = tr(u8c(u8"防御について調べる"), "Evaluate AC");
        content = compose_armor(frame, ja);
        break;
    case Page::HallOfFame:
        title = tr(u8c(u8"勇者の殿堂"), "Palace of heroes");
        add_flow(content, rows_of(frame, 3, 18));
        break;
    case Page::ItemInfo: {
        // 枠は列 13 から。左（列 0〜12）には建物の主の名前・行動の一覧の頭が残っているので載せない。
        title = trim(text_at_row(frame, 0));
        Lines lines;
        for (int row = 1; row <= kLastRow + 1; ++row) {
            const std::string text = row_text(frame, row, kInfoColumn);
            if (!text.empty() && !is_info_any_key(text)) {
                lines.push_back(text);
            }
        }
        add_flow(content, lines);
        break;
    }
    case Page::None:
        return false;
    }
    if (content.empty()) {
        return false;
    }
    if (title.empty()) {
        title = w.title;
    }
    w.title = title;
    w.subtitle.clear();
    w.venue_header.clear();
    w.content_type = "document";
    w.content = std::move(content);
    w.rows.clear();
    w.actions.clear();
    w.cursor = -1;
    w.cursor_target.clear();
    w.input.clear();
    w.prev_key = w.next_key = 0;
    w.previous = w.next = false;
    w.body.clear();
    for (const auto &block : w.content) {
        if (!block.title.empty()) {
            w.body += block.title + "\n";
        }
        for (const auto &cell : block.cells) {
            w.body += (cell.label.empty() ? std::string() : cell.label + ": ") + cell.text + '\n';
        }
        w.body += '\n';
    }
    return true;
}

int building_page_selftest()
{
    int failed = 0;
    const auto check = [&](bool ok, const char *what, const std::string &detail = std::string()) {
        if (ok) {
            return;
        }
        std::fprintf(stderr, "[toband] building page selftest FAIL: %s%s%s\n", what, detail.empty() ? "" : " -- ", detail.c_str());
        ++failed;
    };
    //! ミラーは行ごとに 1 つずつ並ぶので、足りない行は空の行で埋める。
    const auto add_line = [](GameFrame &frame, int row, const std::string &text) {
        while (static_cast<int>(frame.menu_term_lines.size()) <= row) {
            TermMirrorLine blank;
            blank.source_row = static_cast<int>(frame.menu_term_lines.size());
            frame.menu_term_lines.push_back(std::move(blank));
        }
        frame.menu_term_lines[static_cast<size_t>(row)].text_utf8 = text;
    };
    const auto building_frame = [&](bool ja) {
        GameFrame frame;
        frame.menu_open = true;
        frame.modern.language = ja ? "ja" : "en";
        add_line(frame, 2, ja ? u8c(u8" 水庵 (人間)                              武器匠") : " Suiyan (Human)                           Weaponmaster");
        add_line(frame, 19, ja ? u8c(u8" c) 武器を比較する ($1000)          a) 防御について調べる ($1000)") : " c) Compare weapons (1000gp)       a) Evaluate AC (1000gp)");
        add_line(frame, 23, ja ? u8c(u8" ESC) 建物を出る                                     手持ちのお金:       3100") : " ESC) Exit building                                  Gold Remaining:       3100");
        return frame;
    };
    //! 左の欄を 40 桁にそろえて右の欄をつなぐ（原作は列 2 と列 40 に 1 本ずつ描く）。
    const auto two_columns = [](const std::string &left, const std::string &right) {
        std::string text = left;
        int width = term::column_of_byte(text, text.size());
        while (width < 40) {
            text += ' ';
            ++width;
        }
        return text + right;
    };
    //! 建物の画面の一覧を消して、行 `row` に字を置く（原作の `clear_bldg(0, 22)` と同じ）。
    const auto clear_list = [&](GameFrame &frame) {
        for (int row = 0; row <= 22; ++row) {
            add_line(frame, row, "");
        }
    };

    // 防御について調べる（実機で読んだ形）
    {
        GameFrame frame = building_frame(true);
        clear_list(frame);
        add_line(frame, 0, u8c(u8"現在のあなたの装備からすると、あなたの防御力はこれくらいです:"));
        add_line(frame, 2, u8c(u8"あなたの現在のAC:  15"));
        add_line(frame, 3, u8c(u8"ダメージ軽減率  :   6%"));
        add_line(frame, 5, u8c(u8"敵のレベル      :   0   10   20   30   40   50   60   70   80   90  100"));
        add_line(frame, 6, u8c(u8"回避率          :  21%  16%  13%  11%  10%  10%   9%   9%   8%   8%   8%"));
        add_line(frame, 7, u8c(u8"ダメージ期待値  :  74   78   81   83   84   84   85   85   86   86   86"));
        add_line(frame, 9, u8c(u8"    ダメージ軽減率とは、敵の攻撃が当たった時そのダメージを"));
        add_line(frame, 10, u8c(u8"    何パーセント軽減するかを示します。"));
        add_line(frame, 12, u8c(u8"    敵のレベルとは、その敵が通常何階に現れるかを示します。"));
        add_line(frame, 23, u8c(u8" ESC) 建物を出る                                     手持ちのお金:       3100"));
        // 一覧の選択肢（`ESC` だけが残る）も共用の判定は積む。見分けは Term の形だけで行う。
        MenuChoice esc;
        esc.key = 27;
        esc.line_index = 23;
        frame.menu_choices.push_back(esc);
        check(prepare_building_page(frame), "eval_ac page is recognised");
        check(frame.menu_choices.empty(), "the building choices are cleared so capture() makes a detail window");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        frame.modern.continue_key = 13;
        check(apply_building_page_window(frame), "eval_ac window is composed");
        const ModernWindow &w = frame.modern;
        check(w.title == u8c(u8"防御について調べる") && w.stage == "detail" && w.content_type == "document", "eval_ac: title and window kind");
        int table_rows = 0;
        bool first_ok = false, last_ok = false;
        for (const auto &block : w.content) {
            if (block.kind != "table_row") {
                continue;
            }
            ++table_rows;
            if (table_rows == 1) {
                first_ok = block.cells.size() == 3 && block.cells[0].text == "0" && block.cells[1].text == "21%" && block.cells[2].text == "74";
            }
            last_ok = block.cells.size() == 3 && block.cells[0].text == "100" && block.cells[1].text == "8%" && block.cells[2].text == "86";
        }
        check(table_rows == 11 && first_ok && last_ok, "eval_ac: 11 rows of (level, dodge rate, expected damage)");
        bool leftover = false;
        for (const auto &block : w.content) {
            for (const auto &cell : block.cells) {
                if (cell.text.find(u8c(u8"武器匠")) != std::string::npos || cell.text.find(u8c(u8"建物を出る")) != std::string::npos) {
                    leftover = true;
                }
            }
        }
        check(!leftover, "eval_ac: nothing of the building list is carried over");
        bool notes = false;
        for (const auto &block : w.content) {
            if (block.kind == "paragraph") {
                for (const auto &cell : block.cells) {
                    if (cell.text == std::string(u8c(u8"ダメージ軽減率とは、敵の攻撃が当たった時そのダメージを何パーセント軽減するかを示します。"))) {
                        notes = true;
                    }
                }
            }
        }
        check(notes, "eval_ac: the memo lines are joined into a paragraph");
    }
    // 武器の比較
    {
        GameFrame frame = building_frame(true);
        clear_list(frame);
        add_line(frame, 0, u8c(u8"現在の技量から判断すると、あなたの武器は以下のような威力を発揮します:"));
        add_line(frame, 2, two_columns(u8c(u8"  3振の ミニマム・ダガー"), u8c(u8"  ロング・ソード")));
        add_line(frame, 3, two_columns(u8c(u8"  攻撃回数: 4"), u8c(u8"  攻撃回数: 2")));
        add_line(frame, 4, two_columns(u8c(u8"  命中率:  0  50 100 150 200 (敵のAC)"), u8c(u8"  命中率:  0  50 100 150 200 (敵のAC)")));
        add_line(frame, 5, two_columns(u8c(u8"          95  74  47  20   5 (%)"), u8c(u8"          90  70  40  20   5 (%)")));
        add_line(frame, 6, u8c(u8"(一番高いダメージが適用されます。複数の倍打効果は足し算されません。)"));
        add_line(frame, 7, two_columns(u8c(u8"  可能なダメージ:"), u8c(u8"  可能なダメージ:")));
        add_line(frame, 8, two_columns(u8c(u8"   攻撃一回につき 17-19"), u8c(u8"   攻撃一回につき 25-30")));
        add_line(frame, 23, u8c(u8" ESC) 建物を出る                                     手持ちのお金:       3100"));
        check(prepare_building_page(frame), "compare page is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "compare window is composed");
        const ModernWindow &w = frame.modern;
        int weapon_blocks = 0;
        bool names = false;
        for (const auto &block : w.content) {
            check(block.title.empty(), "compare: no block has a title (two headings would turn the left list into section rows)");
            if (block.kind == "fields" && block.cells.size() == 1) {
                ++weapon_blocks;
                names = names || block.cells[0].text == u8c(u8"3振の ミニマム・ダガー") || block.cells[0].text == u8c(u8"ロング・ソード");
            }
        }
        check(weapon_blocks == 2 && names && w.title == u8c(u8"武器を比較する"), "compare: one name row per weapon, titled with the action name");
        bool right_damage = false, note = false;
        for (const auto &block : w.content) {
            for (const auto &cell : block.cells) {
                right_damage = right_damage || cell.text == std::string(u8c(u8"攻撃一回につき 25-30"));
                note = note || cell.text.rfind(u8c(u8"(一番高いダメージ"), 0) == 0;
            }
        }
        check(right_damage && note, "compare: the right column and the full-width note are both read");
    }
    // 賞金首リスト（1 頁目。行 0 に任意のキーの案内）
    {
        GameFrame frame = building_frame(true);
        add_line(frame, 0, u8c(u8"何かキーを押してください"));
        add_line(frame, 4, u8c(u8"          死体か骨を持ち帰ると賞金を差し上げます。"));
        add_line(frame, 6, u8c(u8"          現在の賞金首"));
        add_line(frame, 7, u8c(u8"          青炎の『ガルバ』                         ---- $5700"));
        add_line(frame, 8, u8c(u8"          岩トロル『ビル』                         ---- 済"));
        add_line(frame, 9, u8c(u8"          王女『ベルサリア・オヴェリス・ヴァレリア』 ---$30000"));
        check(prepare_building_page(frame), "wanted page is recognised even though the building commands are still drawn");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "wanted window is composed");
        std::vector<std::string> prizes;
        for (const auto &block : frame.modern.content) {
            if (block.kind == "table_row" && block.cells.size() == 2) {
                prizes.push_back(block.cells[0].text + "|" + block.cells[1].text);
            }
        }
        check(prizes.size() == 3 && prizes[0] == std::string(u8c(u8"青炎の『ガルバ』")) + "|$5700"
                && prizes[1] == std::string(u8c(u8"岩トロル『ビル』")) + "|" + u8c(u8"済")
                && prizes[2] == std::string(u8c(u8"王女『ベルサリア・オヴェリス・ヴァレリア』")) + "|$30000",
            "wanted: names and prizes are split at the dashes (also a long name that touches them, and a finished one)");
        check(frame.modern.title == u8c(u8"賞金首リスト"), "wanted: title");
    }
    // 賞金首リストの最後の頁（行 0 の案内が無い。建物の行動が効く）には触らない
    {
        GameFrame frame = building_frame(true);
        add_line(frame, 6, u8c(u8"          現在の賞金首"));
        check(!prepare_building_page(frame), "the last wanted page (no any-key prompt) is left as the building window");
    }
    // 勇者の殿堂
    {
        GameFrame frame = building_frame(true);
        add_line(frame, 0, u8c(u8"何かキーを押すとゲームに戻ります"));
        add_line(frame, 8, u8c(u8"あなた) 人間のtbmbase (レベル  1)"));
        check(prepare_building_page(frame), "hall of fame page is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "hall of fame window is composed");
        bool shown = false, header_leaked = false;
        for (const auto &block : frame.modern.content) {
            for (const auto &cell : block.cells) {
                shown = shown || cell.text == std::string(u8c(u8"あなた) 人間のtbmbase (レベル 1)"));
                header_leaked = header_leaked || cell.text.find(u8c(u8"武器匠")) != std::string::npos;
            }
        }
        check(shown && !header_leaked, "hall of fame: the player's line is shown and the building header (row 2) is not");
    }
    // アイテムの情報の枠（建物の画面の上に描かれ、左と下に一覧の一部が残る）
    {
        GameFrame frame = building_frame(true);
        add_line(frame, 0, u8c(u8"3振の ミニマム・ダガー (1d3) (+0,+0)"));
        add_line(frame, 1, u8c(u8"                アイテムの能力:"));
        add_line(frame, 2, u8c(u8" アスティヌス  それは敵に投げて大きなダメージを与えることができる。"));
        add_line(frame, 3, u8c(u8"               武器タイプ: 小剣・突剣"));
        add_line(frame, 4, u8c(u8"               [何かキーを押すとゲームに戻ります]"));
        add_line(frame, 19, u8c(u8" a) アイテム"));
        add_line(frame, 23, u8c(u8" ESC) 建物を"));
        check(prepare_building_page(frame), "item info frame over the building screen is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "item info window is composed");
        bool owner = false, description = false, footer = false, field = false;
        for (const auto &block : frame.modern.content) {
            for (const auto &cell : block.cells) {
                owner = owner || cell.text.find(u8c(u8"アスティヌス")) != std::string::npos;
                footer = footer || cell.text.find("ESC)") != std::string::npos || cell.text.find("a)") != std::string::npos
                    || cell.text.find(u8c(u8"何かキー")) != std::string::npos;
                description = description || cell.text == std::string(u8c(u8"それは敵に投げて大きなダメージを与えることができる。"));
                field = field || (cell.label == u8c(u8"武器タイプ") && cell.text == u8c(u8"小剣・突剣"));
            }
        }
        std::string dump;
        for (const auto &block : frame.modern.content) {
            for (const auto &cell : block.cells) {
                dump += "[" + cell.label + "|" + cell.text + "]";
            }
        }
        check(!owner && !footer && description && field && frame.modern.title == u8c(u8"3振の ミニマム・ダガー (1d3) (+0,+0)"),
            "item info: the owner's name, the building list and the any-key line are not carried over", dump);
    }
    // 英語
    {
        GameFrame frame = building_frame(false);
        clear_list(frame);
        add_line(frame, 0, "Defense abilities from your current Armor Class are evaluated below.");
        add_line(frame, 2, "Your current AC :  15");
        add_line(frame, 3, "Protection rate :   6%");
        add_line(frame, 5, "Level of Monster:   0   10   20");
        add_line(frame, 6, "Dodge Rate      :  21%  16%  13%");
        add_line(frame, 7, "Average Damage  :  74   78   81");
        add_line(frame, 23, " ESC) Exit building                                  Gold Remaining:       3100");
        check(prepare_building_page(frame), "eval_ac (english) is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame) && frame.modern.title == "Evaluate AC", "eval_ac (english) title");
        int rows = 0;
        for (const auto &block : frame.modern.content) {
            rows += block.kind == "table_row" ? 1 : 0;
        }
        check(rows == 3, "eval_ac (english): 3 table rows");
    }
    // 武器の比較（英語。字は原作の `#else` の枝と、実機の Term で読んだ形）
    {
        GameFrame frame = building_frame(false);
        clear_list(frame);
        add_line(frame, 0, "Based on your current abilities, here is what your weapons will do");
        add_line(frame, 2, two_columns("  3 Minimum Daggers", "  Long Sword"));
        add_line(frame, 3, two_columns("  Number of Blows: 4", "  Number of Blows: 2"));
        add_line(frame, 4, two_columns("  To Hit:  0  50 100 150 200 (AC)", "  To Hit:  0  50 100 150 200 (AC)"));
        add_line(frame, 5, two_columns("          95  74  47  20   5 (%)", "          90  70  40  20   5 (%)"));
        add_line(frame, 6, "(Only highest damage applies per monster. Special damage not cumulative.)");
        add_line(frame, 7, two_columns("  Possible Damage:", "  Possible Damage:"));
        add_line(frame, 8, two_columns("   17-19 per blow", "   25-30 per blow"));
        add_line(frame, 23, " ESC) Exit building                                  Gold Remaining:       3100");
        check(prepare_building_page(frame), "compare page (english) is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "compare window (english) is composed");
        int weapon_blocks = 0;
        bool names = false, note = false, right_damage = false;
        for (const auto &block : frame.modern.content) {
            if (block.kind == "fields" && block.cells.size() == 1) {
                ++weapon_blocks;
                names = names || (block.cells[0].label == "First weapon" && block.cells[0].text == "3 Minimum Daggers")
                    || (block.cells[0].label == "Second weapon" && block.cells[0].text == "Long Sword");
            }
            for (const auto &cell : block.cells) {
                note = note || cell.text.rfind("(Only highest damage", 0) == 0;
                right_damage = right_damage || cell.text == "25-30 per blow";
            }
        }
        check(weapon_blocks == 2 && names && note && right_damage && frame.modern.title == "Compare weapons",
            "compare (english): one name row per weapon, the right column and the note are read, titled with the action name");
    }
    // 賞金首リスト（英語。`Hit any key.` ＋ 行 6 の `Wanted monsters`）
    {
        GameFrame frame = building_frame(false);
        add_line(frame, 0, "Hit any key.");
        add_line(frame, 4, "          Bring back a corpse or bones and we will give you a prize.");
        add_line(frame, 6, "          Wanted monsters");
        add_line(frame, 7, "          Garuba the Blue Flame                    ---- $5700");
        add_line(frame, 8, "          Bill the Rock Troll                      ---- done");
        check(prepare_building_page(frame), "wanted page (english) is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "wanted window (english) is composed");
        std::vector<std::string> prizes;
        std::string head;
        for (const auto &block : frame.modern.content) {
            if (block.kind == "table_row" && block.cells.size() == 2) {
                prizes.push_back(block.cells[0].text + "|" + block.cells[1].text);
            }
            if (block.kind == "table_header" && block.cells.size() == 2) {
                head = block.cells[0].text + "|" + block.cells[1].text;
            }
        }
        check(prizes.size() == 2 && prizes[0] == "Garuba the Blue Flame|$5700" && prizes[1] == "Bill the Rock Troll|done" && head == "Target|Prize"
                && frame.modern.title == "Wanted unique list",
            "wanted (english): names and prizes are split at the dashes, the heading is English");
    }
    // 勇者の殿堂（英語）
    {
        GameFrame frame = building_frame(false);
        add_line(frame, 0, "Hit any key to continue");
        add_line(frame, 8, "You) Human tbmbase (Level  1)");
        check(prepare_building_page(frame), "hall of fame page (english) is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "hall of fame window (english) is composed");
        bool shown = false, header_leaked = false;
        for (const auto &block : frame.modern.content) {
            for (const auto &cell : block.cells) {
                shown = shown || cell.text == "You) Human tbmbase (Level 1)";
                header_leaked = header_leaked || cell.text.find("Weaponmaster") != std::string::npos;
            }
        }
        check(shown && !header_leaked && frame.modern.title == "Palace of heroes", "hall of fame (english): the player's line is shown, the building header is not, titled in English");
    }
    // アイテムの情報の枠（英語）
    {
        GameFrame frame = building_frame(false);
        add_line(frame, 0, "3 Minimum Daggers (1d3) (+0,+0)");
        add_line(frame, 1, "                Item Attributes:");
        add_line(frame, 2, " Astinus  It can be thrown at your enemies to do great damage.");
        add_line(frame, 3, "               Weapon type: small sword/rapier");
        add_line(frame, 4, "               [Press any key to return to the game]");
        add_line(frame, 19, " a) Research");
        add_line(frame, 23, " ESC) Exit buil");
        check(prepare_building_page(frame), "item info frame (english) over the building screen is recognised");
        frame.modern.stage = "detail";
        frame.modern.kind = "dialog";
        check(apply_building_page_window(frame), "item info window (english) is composed");
        bool owner = false, footer = false, field = false;
        for (const auto &block : frame.modern.content) {
            for (const auto &cell : block.cells) {
                owner = owner || cell.text.find("Astinus") != std::string::npos;
                footer = footer || cell.text.find("ESC)") != std::string::npos || cell.text.find("a)") != std::string::npos
                    || cell.text.find("Press any key") != std::string::npos;
                field = field || (cell.label == "Weapon type" && cell.text == "small sword/rapier");
            }
        }
        check(!owner && !footer && field && frame.modern.title == "3 Minimum Daggers (1d3) (+0,+0)",
            "item info (english): the owner's name, the building list and the any-key line are not carried over");
    }
    // 別の画面には触らない
    {
        GameFrame frame = building_frame(true);
        check(!prepare_building_page(frame), "the ordinary building screen is left alone");
        GameFrame typing = building_frame(true);
        clear_list(typing);
        add_line(typing, 0, u8c(u8"現在のあなたの装備からすると、あなたの防御力はこれくらいです:"));
        add_line(typing, 23, u8c(u8" ESC) 建物を出る                                     手持ちのお金:       3100"));
        typing.text_input_active = true;
        check(!prepare_building_page(typing), "a text input in the building is left alone");
        GameFrame more = building_frame(true);
        clear_list(more);
        add_line(more, 0, u8c(u8"現在のあなたの装備からすると、あなたの防御力はこれくらいです: -続く-"));
        add_line(more, 23, u8c(u8" ESC) 建物を出る                                     手持ちのお金:       3100"));
        check(!prepare_building_page(more), "a -more- wait is left alone", "row0=" + text_at_row(more, 0));
        GameFrame no_footer = building_frame(true);
        add_line(no_footer, 0, u8c(u8"何かキーを押すとゲームに戻ります"));
        add_line(no_footer, 23, "");
        check(!prepare_building_page(no_footer), "without the building footer the screen is not a building page");
    }
    g_page = Page::None;
    return failed;
}

} // namespace tb
