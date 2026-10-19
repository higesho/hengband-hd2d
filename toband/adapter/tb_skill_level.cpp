/*!
 * @file tb_skill_level.cpp
 * @brief 「現在のスキル」の窓（`tb_skill_level.h`）。
 *
 * ## 原作の画面（`cmd4.c` の `do_cmd_skill_level()`）
 * 行 2 に見出し、行 4 に列の見出し（`グループ`・`スキル`・`レベル`）、行 5 に `=` の罫線、行 6 以降に 2 列の一覧。
 * 左の列は分類（`武器`・`その他`・`魔法`。いまの分類は `TERM_L_BLUE` の明るい青）、`|` の罫線をはさんで、
 * 右の列は、いまの分類の技能の名前（列 `|` + 2）とレベル（列 43。武器・その他は `[初心者]` などの段階、魔法は数）。
 * 原作のカーソルは、`4`/`6` で選んだ列に応じて、分類の行の頭（`Term_gotoxy(0, 6 + grp_cur - grp_top)`）か、
 * 技能の行の名前の頭（`Term_gotoxy(max + 3, 6 + skill_cur - skill_top)`）に置かれる。
 * キーは `browser_cursor()`: `4`/`6` が列を替え、`2`/`8` がその列の中を動かし、斜めの `3`/`9` が 1 画面ぶん動かす。ESC で閉じる。
 *
 * ## 窓の作り
 * 共用部分の知識の一覧（`presentation::modern_browser_content()`）と同じ形の窓（`browser`）にする:
 * 左の一覧に分類（`selection` 0）と技能（`selection` 1）の行、右の欄に分類の一覧と技能の表。
 * 画面側の矢印・パッド・クリックは、この窓の既存の動き（`focus`・`navigate`）に従う。
 * 数値・名前はすべて原作が画面に描いた字を読むだけで、ここでは決めない。
 *
 * ## 入力の翻訳
 * 組み替えた窓は `NativeModernMenu` の `current_`（`detail`）と食い違うので、`ui_action` は `NativeModernMenu::input()`
 * では翻訳できない。ここで翻訳する。要求番号・版の管理は `NativeModernMenu` に任せ、同じ要求を「続ける」に直して
 * `input()` に通し、受理されたときだけここで決めたキーを返す（能力値の窓 `tb_birth_stats.cpp` と同じ作り）。
 */
#include "tb_skill_level.h"

#include "tb_term_util.h"

#include <cstdio>
#include <string>
#include <vector>

namespace tb {

namespace {

using namespace term;

constexpr int kRowTitle = 2;  //!< `現在のスキル`
constexpr int kRowHeader = 4; //!< `グループ スキル … レベル`
constexpr int kRowRule = 5;   //!< `====`
constexpr int kRowFirst = 6;  //!< 一覧の最初の行（原作の `6 + i`）

//! 直近に組んだ窓（画面から来た操作の翻訳に使う）。組み替えが偽の間は `active == false`。
struct State {
    bool active{false};
    ModernWindow window;
};
State g_state;

bool is_title(const std::string &title)
{
    return title == u8c(u8"現在のスキル") || title == "Current Skill";
}

bool has_header_words(const std::string &header)
{
    return (contains(header, u8c(u8"グループ")) && contains(header, u8c(u8"スキル")) && contains(header, u8c(u8"レベル")))
        || (contains(header, "Group") && contains(header, "Skill") && contains(header, "Level"));
}

} // namespace

bool apply_skill_level_window(GameFrame &frame)
{
    g_state = State{};
    ModernWindow &w = frame.modern;
    // `capture()` が作る詳細画面（決定が Enter）にだけ手を入れる。窓が無い（新しい画面でない）・別の窓なら触らない。
    if (w.stage != "detail" || w.kind != "dialog" || w.continue_key != 13 || frame.text_input_active || frame.numeric.active
        || !frame.prompt.choices.empty() || !frame.menu_choices.empty()) {
        return false;
    }
    const std::string title = trim(text_at_row(frame, kRowTitle));
    const std::string header = text_at_row(frame, kRowHeader);
    const std::string rule = trim(text_at_row(frame, kRowRule));
    if (!is_title(title) || !has_header_words(header) || !starts_with(rule, "====")) {
        return false;
    }
    // 罫線 `|` の桁（分類の列の右）。技能の名前の桁は罫線の 2 つ右（原作の `max + 3`）。
    int separator = -1;
    if (const TermMirrorLine *first = line_at_row(frame, kRowFirst)) {
        const size_t at = first->text_utf8.find('|');
        if (at != std::string::npos) {
            separator = column_of_byte(first->text_utf8, at);
        }
    }
    const bool ja = w.language == "ja";
    const size_t level_at = header.find(ja ? u8c(u8"レベル") : "Level");
    if (separator < 0 || level_at == std::string::npos) {
        return false;
    }
    const int name_col = separator + 2;
    const int level_col = column_of_byte(header, level_at);
    if (level_col <= name_col) {
        return false;
    }
    const auto tr = [&](const char *jp, const char *en) { return std::string(ja ? jp : en); };
    std::string skill_label = trim(slice_columns(header, name_col, level_col));
    std::string level_label = trim(slice_columns(header, level_col));
    if (skill_label.empty()) {
        skill_label = tr(u8c(u8"スキル"), "Skill");
    }
    if (level_label.empty()) {
        level_label = tr(u8c(u8"レベル"), "Level");
    }

    // 原作のカーソル（ミラーの行の添字と桁）。分類の列にあるか、技能の列にあるか。
    const int cursor_line = frame.menu_term_curs_row;
    const int active_column = (cursor_line >= 0 && frame.menu_term_curs_col >= name_col) ? 1 : 0;

    ModernBlock groups, head;
    groups.kind = "group_list";
    groups.title = tr(u8c(u8"分類"), "Groups");
    head.kind = "table_header";
    head.cells.push_back({ "", skill_label, false, "", "" });
    head.cells.push_back({ "", level_label, false, "", "" });
    std::vector<ModernBlock> skill_rows;
    std::vector<ModernRow> rows;
    std::string current_group, cursor_target, first_group_id;
    for (size_t index = 0; index < frame.menu_term_lines.size(); ++index) {
        const TermMirrorLine &line = frame.menu_term_lines[index];
        if (line.source_row < kRowFirst) {
            continue;
        }
        const std::string group = trim(slice_columns(line.text_utf8, 0, separator));
        const std::string name = trim(slice_columns(line.text_utf8, name_col, level_col));
        const std::string level = trim(slice_columns(line.text_utf8, level_col));
        const bool selected_row = static_cast<int>(index) == cursor_line;
        if (!group.empty()) {
            ModernRow row;
            row.id = "browser:" + std::to_string(index) + ":0:" + group;
            row.slot = static_cast<int>(index);
            row.selection = 0;
            row.label = group;
            row.price_known = false;
            const bool selected = selected_row && active_column == 0;
            if (selected) {
                cursor_target = row.id;
            }
            if (first_group_id.empty()) {
                first_group_id = row.id;
            }
            if (current_group.empty() && first_char_color(line) == 14) {
                current_group = group; // いまの分類は明るい青（TERM_L_BLUE）
            }
            groups.cells.push_back({ "", group, selected, row.id, "" });
            rows.push_back(std::move(row));
        }
        if (!name.empty()) {
            ModernRow row;
            row.id = "browser:" + std::to_string(index) + ":1:" + name;
            row.slot = static_cast<int>(index);
            row.selection = 1;
            row.label = name;
            row.price_known = false;
            const bool selected = selected_row && active_column == 1;
            if (selected) {
                cursor_target = row.id;
            }
            ModernBlock block;
            block.kind = "table_row";
            // 列の見出しは表の頭（table_header）にあるので、行の字には付けない（付けると 1 行ごとに見出しが重なる）
            block.cells.push_back({ "", name, selected, row.id, "" });
            block.cells.push_back({ "", level, false, "", "" });
            skill_rows.push_back(std::move(block));
            rows.push_back(std::move(row));
        }
    }
    if (groups.cells.empty()) {
        return false; // 読めなかった。窓には触らない
    }
    if (cursor_target.empty()) {
        cursor_target = first_group_id; // 技能の列にカーソルがあっても、その行に技能が無いとき（空の行）は分類へ
    }

    w.stage = "cursor";
    w.content_type = "browser";
    w.title = title;
    w.subtitle = current_group;
    w.content.clear();
    w.content.push_back(std::move(groups));
    w.content.push_back(std::move(head));
    for (auto &block : skill_rows) {
        w.content.push_back(std::move(block));
    }
    w.rows = std::move(rows);
    w.actions.clear();
    w.cursor = cursor_line;
    w.cursor_target = cursor_target;
    // 左の列にいるときの決定は、技能の列へ移る（`6`）。技能の列では原作は決定を受けないので Enter のまま（無害）。
    w.continue_key = active_column == 0 ? '6' : 13;
    // ページ送りは斜めの方向キー（共用の知識の一覧と同じ。`browser_cursor()` が 1 画面ぶん動かす）。
    w.prev_key = '9';
    w.next_key = '3';
    w.previous = w.next = true;
    w.input.clear();
    w.capture_keys = false;
    w.cursor_span_begin = -1;
    w.cursor_span_len = 0;
    w.body.clear();
    for (const auto &block : w.content) {
        for (const auto &cell : block.cells) {
            w.body += cell.text + '\n';
        }
    }

    g_state.active = true;
    g_state.window = w;
    return true;
}

bool skill_level_input(const presentation::InputEventWire &event, std::vector<int> &keys, presentation::NativeModernMenu &menu)
{
    if (!g_state.active || event.e != "ui_action") {
        return false;
    }
    const ModernWindow &w = g_state.window;
    int key = 0;
    bool rejected_unreachable = false;
    if (event.action == "focus") {
        // 原作のカーソルを目的の行へ動かす（列が違えば `4`/`6`、同じ列なら `2`/`8` を 1 回）。共用の `modern_focus_key()` と同じ規則。
        if (event.target != w.cursor_target) {
            key = presentation::modern_focus_key(w, event.target);
            rejected_unreachable = (key == 0);
        }
    } else if (event.action == "navigate" && (event.number == 2 || event.number == 4 || event.number == 6 || event.number == 8)) {
        key = static_cast<int>('0' + event.number);
    } else if (event.action == "continue") {
        key = w.continue_key;
    } else if (event.action == "document_page") {
        key = event.number ? w.next_key : w.prev_key;
    } else {
        return false; // `cancel`（ESC）・`native_key`（文字キー）は共用の入力に任せる
    }
    if (rejected_unreachable) {
        // 動かせない目標: 共用の窓に元の要求を渡し、「拒否」として記録させる（`focus` は `detail` の窓では何も起こさない）。
        std::vector<int> ignored;
        menu.input(event, ignored);
        return true;
    }
    // 版・要求番号の管理は NativeModernMenu に任せる（同じ要求を「続ける」にして通す）。受理されたときだけキーを返す。
    presentation::InputEventWire ledger = event;
    ledger.action = "continue";
    ledger.target.clear();
    std::vector<int> accepted;
    menu.input(ledger, accepted);
    if (!accepted.empty() && key) {
        keys.push_back(key);
    }
    return true;
}

int skill_level_selftest()
{
    int failed = 0;
    const auto check = [&](bool ok, const char *what, const std::string &detail = std::string()) {
        if (ok) {
            return;
        }
        std::fprintf(stderr, "[toband] skill level selftest FAIL: %s%s%s\n", what, detail.empty() ? "" : " -- ", detail.c_str());
        ++failed;
    };
    //! ミラーは行ごとに 1 つずつ並ぶ（`menu_term_curs_row` はその添字）ので、足りない行は空の行で埋める。
    const auto add_line = [](GameFrame &frame, int row, const std::string &text, int color = 1) {
        while (static_cast<int>(frame.menu_term_lines.size()) <= row) {
            TermMirrorLine blank;
            blank.source_row = static_cast<int>(frame.menu_term_lines.size());
            frame.menu_term_lines.push_back(std::move(blank));
        }
        frame.menu_term_lines[static_cast<size_t>(row)].text_utf8 = text;
        frame.menu_term_lines[static_cast<size_t>(row)].color = static_cast<uint8_t>(color);
    };
    //! 実機で読んだ原作の画面（日本語。分類は武器・その他・魔法。いまの分類は武器）。
    const auto make_frame = [&](bool ja, int cursor_row, int cursor_col) {
        GameFrame frame;
        frame.pre_game_menu = false;
        frame.menu_open = true;
        if (ja) {
            add_line(frame, 2, u8c(u8"現在のスキル "));
            add_line(frame, 4, u8c(u8"グループ スキル                            レベル"));
            add_line(frame, 5, std::string(59, '='));
            add_line(frame, 6, u8c(u8"武器   | 小剣・突剣                        [初心者]"), 14);
            add_line(frame, 7, u8c(u8"その他 | カタナ                            [熟練者]"));
            add_line(frame, 8, u8c(u8"魔法   | 剣                                [初心者]"));
            add_line(frame, 9, u8c(u8"       | 大剣                              [初心者]"));
        } else {
            add_line(frame, 2, "Current Skill ");
            add_line(frame, 4, "Group Skill                               Level");
            add_line(frame, 5, std::string(59, '='));
            add_line(frame, 6, "Weapon | Small sword                      [Unskilled]", 14);
            add_line(frame, 7, "Misc   | Katana                           [Expert]");
            add_line(frame, 8, "Magic  | Sword                            [Unskilled]");
            add_line(frame, 9, "       | Great sword                      [Unskilled]");
        }
        // 一覧の終わりまで罫線 `|` が続く（原作は `browser_rows` 行ぶん描く）
        for (int row = 10; row < 12; ++row) {
            add_line(frame, row, "       |");
        }
        frame.menu_term_curs_row = cursor_row;
        frame.menu_term_curs_col = cursor_col;
        frame.modern.version = 5;
        frame.modern.kind = "dialog";
        frame.modern.stage = "detail";
        frame.modern.continue_key = 13;
        frame.modern.language = ja ? "ja" : "en";
        return frame;
    };

    // 分類の列（桁 0）で、いまの分類は 2 行目（その他）の位置にカーソルがある場合
    GameFrame frame = make_frame(true, 7, 0);
    check(apply_skill_level_window(frame), "the skill screen is recognised (ja)");
    const ModernWindow &w = frame.modern;
    check(w.stage == "cursor" && w.content_type == "browser", "stage is cursor and the window kind is browser");
    check(w.title == u8c(u8"現在のスキル"), "the title is read from the original line");
    check(w.subtitle == u8c(u8"武器"), "the subtitle is the current group (the bright blue one)");
    int groups = 0, skills = 0;
    for (const auto &row : w.rows) {
        (row.selection == 0 ? groups : skills) += 1;
    }
    check(groups == 3 && skills == 4, "3 group rows and 4 skill rows");
    check(w.cursor_target == "browser:7:0:" + std::string(u8c(u8"その他")), "the native cursor row (group column) is the window cursor", w.cursor_target);
    check(w.continue_key == '6', "confirm on the group column moves to the skill column (6)");
    bool table_ok = false;
    for (const auto &block : w.content) {
        if (block.kind == "table_row" && block.cells.size() == 2 && block.cells[0].text == u8c(u8"カタナ")) {
            table_ok = block.cells[1].text == u8c(u8"[熟練者]");
        }
    }
    std::string dump;
    for (const auto &block : w.content) {
        if (block.kind == "table_row") {
            dump += "[";
            for (const auto &cell : block.cells) {
                dump += cell.text + "|";
            }
            dump += "]";
        }
    }
    check(table_ok, "a skill row (name, level) is read from the original line", dump);
    check(!w.content.empty() && w.content.front().kind == "group_list" && w.content.front().cells.size() == 3, "the group list is the first block");

    // 技能の列（桁 9 以降）にカーソルがある場合
    GameFrame in_skills = make_frame(true, 8, 9);
    check(apply_skill_level_window(in_skills), "the skill screen is recognised with the cursor in the skill column");
    check(in_skills.modern.cursor_target == "browser:8:1:" + std::string(u8c(u8"剣")), "the cursor row (skill column) is the window cursor");
    check(in_skills.modern.continue_key == 13, "confirm on the skill column keeps Enter");

    // 英語
    GameFrame en = make_frame(false, 6, 0);
    check(apply_skill_level_window(en), "the skill screen is recognised (en)");
    check(en.modern.subtitle == "Weapon" && en.modern.cursor_target == "browser:6:0:Weapon", "english group and cursor");
    bool en_table = false;
    for (const auto &block : en.modern.content) {
        if (block.kind == "table_row" && block.cells.size() == 2 && block.cells[0].text == "Katana") {
            en_table = block.cells[1].text == "[Expert]";
        }
    }
    check(en_table, "english skill row");

    // 別の画面には触らない
    GameFrame other = make_frame(true, 7, 0);
    other.menu_term_lines[2].text_utf8 = u8c(u8"現在の知識");
    check(!apply_skill_level_window(other), "a different screen is left alone");
    GameFrame choices = make_frame(true, 7, 0);
    choices.modern.stage = "choices";
    check(!apply_skill_level_window(choices), "a choices window is left alone");

    // 入力の翻訳（NativeModernMenu に「詳細画面」を 1 回 capture させ、その版で動かす）
    presentation::NativeModernMenu menu;
    menu.set_enabled(true);
    menu.set_language("ja");
    GameFrame live = make_frame(true, 6, 0);
    live.modern = ModernWindow{};
    menu.capture(live);
    check(apply_skill_level_window(live), "the skill screen is recognised after capture()");
    const auto event = [&](const char *action, const std::string &target, int number, int request) {
        presentation::InputEventWire e;
        e.e = "ui_action";
        e.action = action;
        e.target = target;
        e.number = number;
        e.ui_revision = live.modern.revision;
        e.request_id = request;
        return e;
    };
    std::vector<int> keys;
    check(skill_level_input(event("focus", "browser:7:0:" + std::string(u8c(u8"その他")), 0, 1), keys, menu) && keys == std::vector<int>{ '2' },
        "focus on the next group sends 2 (native cursor down in the group column)",
        (keys.empty() ? std::string("(no key)") : std::to_string(keys.front())) + " target=" + live.modern.cursor_target);
    keys.clear();
    check(skill_level_input(event("focus", "browser:7:0:" + std::string(u8c(u8"その他")), 0, 1), keys, menu) && keys.empty(),
        "a repeated request is not accepted twice");
    // 受理のあとは版が繰り上がる（次の capture で）。窓を作り直して続きを確かめる。
    const auto rebuild = [&](GameFrame source) {
        source.modern = ModernWindow{};
        menu.capture(source);
        live = source;
        return apply_skill_level_window(live);
    };
    check(rebuild(make_frame(true, 6, 0)), "the screen is recognised again");
    keys.clear();
    check(skill_level_input(event("focus", "browser:8:1:" + std::string(u8c(u8"剣")), 0, 2), keys, menu) && keys == std::vector<int>{ '6' },
        "focus on a skill row from the group column sends 6 (move to the skill column)");
    check(rebuild(make_frame(true, 6, 9)), "the screen is recognised in the skill column");
    keys.clear();
    check(skill_level_input(event("focus", "browser:8:1:" + std::string(u8c(u8"剣")), 0, 3), keys, menu) && keys == std::vector<int>{ '2' },
        "focus on a lower skill row in the skill column sends 2");
    check(rebuild(make_frame(true, 6, 9)), "the screen is recognised once more");
    keys.clear();
    check(skill_level_input(event("focus", "browser:6:0:" + std::string(u8c(u8"武器")), 0, 4), keys, menu) && keys == std::vector<int>{ '4' },
        "focus on a group row from the skill column sends 4 (back to the group column)");
    check(rebuild(make_frame(true, 6, 0)), "the screen is recognised for navigate");
    keys.clear();
    check(skill_level_input(event("navigate", "", 6, 5), keys, menu) && keys == std::vector<int>{ '6' }, "navigate right sends 6");
    check(rebuild(make_frame(true, 6, 0)), "the screen is recognised for page keys");
    keys.clear();
    check(skill_level_input(event("document_page", "", 1, 6), keys, menu) && keys == std::vector<int>{ '3' }, "next page sends 3");
    check(rebuild(make_frame(true, 6, 0)), "the screen is recognised for continue");
    keys.clear();
    check(skill_level_input(event("continue", "", 0, 7), keys, menu) && keys == std::vector<int>{ '6' }, "continue on the group column sends 6");
    // 共用の入力に任せる操作
    keys.clear();
    check(!skill_level_input(event("cancel", "", 0, 8), keys, menu) && keys.empty(), "cancel is left to the shared input (ESC)");
    check(!skill_level_input(event("native_key", "", '2', 9), keys, menu) && keys.empty(), "a raw key is left to the shared input");
    return failed;
}

} // namespace tb
