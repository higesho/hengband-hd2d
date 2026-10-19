/*!
 * @file tb_birth_stats.cpp
 * @brief 誕生の能力値の割り振りの窓（`tb_birth_stats.h`）。
 *
 * ## 原作の画面（`birth.c` の `player_birth_aux_2()`）
 * 行 1 に見出し、行 3〜5 に性別・種族・クラス、行 7 に注意書き、行 11 に操作の案内（`コスト合計: nn/60  2/8/j/k で移動, …`）、
 * 行 13 に列の見出し、行 14〜19 に能力値 6 行（`名前  基本値  最小値  最大値  (コスト)  種族  =  合計値`）、
 * 行 21 に紙幣への変換（残りのポイントがあるときだけ）。原作のカーソルは今の能力値の行の基本値の右（`Term_gotoxy(POINT_COL + 14, POINT_ROW + stat)`）。
 * キーは `2/j` が次の行・`8/k` が前の行・`4/h` が減らす・`6/l` が増やす・`Enter` と `ESC` が決定・`S` がやり直し（行頭の選択へ戻る）・
 * `Q` が終了（仲間の誕生では効かない）。**コストの計算・上限・紙幣への変換は原作のまま**で、ここは画面の数値を読むだけ。
 *
 * ## 窓の作り
 * `NativeModernMenu::capture()` は、この画面を「決定だけの詳細画面（`detail`）」にする（左の一覧には `Enter) 次へ` しか出ない）。
 * 共用部分（`presentation/bridge/`）は変えずに、アダプタが `capture()` の後で窓を組み替える。
 * 窓の種類は Sil-Q の能力値の窓と同じ `sil_allocation`（行は − と ＋ の対。左右は値の増減、決定は `allocation:continue`）。
 * 画面側の動きはその窓の既存の実装に従う: 上下は原作のカーソルを動かす（`focus`）、左右は対の行を押す（`choice`）、決定は次へ。
 *
 * ## 入力の翻訳
 * 組み替えた窓は `NativeModernMenu` の `current_`（`detail`）と食い違うので、`ui_action` は `NativeModernMenu::input()`
 * では翻訳できない。ここで翻訳する。ただし**要求番号・版の管理は `NativeModernMenu` に任せる**: 同じ要求を「続ける」の
 * 操作に直して `input()` に通し、受理されたときだけ（版が合い、まだ使われていない）ここで決めたキーを返す。
 * 受理の記録（`result`・次の版の繰り上げ）が共用部分の他の窓と同じになる。
 */
#include "tb_birth_stats.h"

#include "tb_term_util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace tb {

namespace {

using term::u8c; //!< UTF-8 の文字列リテラルを `const char *` にする（呼ぶ側は `u8c(u8"…")` と `u8` を字面に書く。理由は `tb_term_util.h`）

constexpr int kRowCost = 11;      //!< 操作の案内の行（`POINT_ROW - 3`）
constexpr int kRowHeader = 13;    //!< 列の見出し（`POINT_ROW - 1`）
constexpr int kRowFirstStat = 14; //!< 能力値の最初の行（`POINT_ROW`）
constexpr int kStatCount = 6;     //!< `A_MAX`
constexpr int kRowNote = 21;      //!< 使われていないポイント（`POINT_ROW + A_MAX + 1`）

//! 能力値 1 行ぶん（原作が描いた字のまま）。
struct StatRow {
    int source_row{};
    std::string name, base, min, max, cost, race, total;
};

//! 窓の行のうち、操作の翻訳に要るものだけ。
struct Control {
    std::string id;
    int key{};
    int slot{};
    bool available{true};
};

//! 直近に組んだ窓。画面から来た操作の翻訳に使う（組み替えが偽の間は `active == false`）。
struct State {
    bool active{false};
    std::string cursor_target;
    std::vector<Control> controls;
};
State g_state;

std::string trim(const std::string &text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> tokens_of(const std::string &text)
{
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

const TermMirrorLine *line_at_row(const GameFrame &frame, int source_row)
{
    for (const auto &line : frame.menu_term_lines) {
        if (line.source_row == source_row) {
            return &line;
        }
    }
    return nullptr;
}

/*! @brief `腕力 :  15  15  18  (11)  +0  =  15` の 1 行を読む。形が違えば false。 */
bool parse_stat_row(const std::string &raw, StatRow &out)
{
    // `( 0)` のように括弧の中に空白が入るので、先に詰める。
    std::string text;
    for (size_t i = 0; i < raw.size(); ++i) {
        text += raw[i];
        if (raw[i] == '(') {
            while (i + 1 < raw.size() && raw[i + 1] == ' ') {
                ++i;
            }
        }
    }
    const auto tokens = tokens_of(text);
    size_t open = tokens.size();
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].size() >= 2 && tokens[i].front() == '(' && tokens[i].back() == ')') {
            open = i;
            break;
        }
    }
    // 名前（1 語以上）・基本値・最小値・最大値・(コスト)・種族の補正・=・合計値
    if (open < 4 || open + 3 >= tokens.size() || tokens[open + 2] != "=") {
        return false;
    }
    out.base = tokens[open - 3];
    out.min = tokens[open - 2];
    out.max = tokens[open - 1];
    out.cost = tokens[open].substr(1, tokens[open].size() - 2);
    out.race = tokens[open + 1];
    out.total = tokens[open + 3];
    std::string name;
    for (size_t i = 0; i + 3 < open; ++i) {
        if (tokens[i] == ":" || tokens[i] == u8c(u8"：")) {
            continue;
        }
        if (!name.empty()) {
            name += ' ';
        }
        name += tokens[i];
    }
    while (!name.empty() && name.back() == ':') {
        name.pop_back();
    }
    out.name = trim(name);
    return !out.name.empty();
}

bool to_int(const std::string &text, int &value)
{
    if (text.empty()) {
        return false;
    }
    char *end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

/*! @brief 操作の案内の行から、使った分と上限（`34/60`）を取り出す。 */
bool parse_cost(const std::string &text, int &used, int &limit)
{
    for (size_t slash = text.find('/'); slash != std::string::npos; slash = text.find('/', slash + 1)) {
        size_t b = slash, e = slash + 1;
        while (b > 0 && text[b - 1] >= '0' && text[b - 1] <= '9') {
            --b;
        }
        while (e < text.size() && text[e] >= '0' && text[e] <= '9') {
            ++e;
        }
        if (b < slash && e > slash + 1 && to_int(text.substr(b, slash - b), used) && to_int(text.substr(slash + 1, e - slash - 1), limit)) {
            return true;
        }
    }
    return false;
}

//! 見出しの語（`基本値` など）。括弧は外す。
std::string clean_label(std::string label)
{
    while (!label.empty() && (label.front() == '(' || label.front() == '[')) {
        label.erase(0, 1);
    }
    while (!label.empty() && (label.back() == ')' || label.back() == ']')) {
        label.pop_back();
    }
    return label;
}

//! 原作の画面の形か。能力値の行を読めたら true。
bool read_screen(const GameFrame &frame, std::vector<StatRow> &stats, std::vector<std::string> &labels, int &used, int &limit)
{
    const TermMirrorLine *guide = line_at_row(frame, kRowCost);
    if (!guide || guide->text_utf8.find("2/8/j/k") == std::string::npos || guide->text_utf8.find("4/6/h/l") == std::string::npos) {
        return false;
    }
    if (!parse_cost(guide->text_utf8, used, limit)) {
        return false;
    }
    stats.clear();
    for (int i = 0; i < kStatCount; ++i) {
        const TermMirrorLine *line = line_at_row(frame, kRowFirstStat + i);
        StatRow row;
        if (!line || !parse_stat_row(line->text_utf8, row)) {
            return false;
        }
        row.source_row = kRowFirstStat + i;
        stats.push_back(std::move(row));
    }
    labels.clear();
    if (const TermMirrorLine *header = line_at_row(frame, kRowHeader)) {
        for (const auto &token : tokens_of(header->text_utf8)) {
            labels.push_back(clean_label(token));
        }
    }
    return true;
}

} // namespace

bool apply_birth_stats_window(GameFrame &frame)
{
    g_state = State{};
    ModernWindow &w = frame.modern;
    // `capture()` が作る詳細画面（決定が Enter）にだけ手を入れる。窓が無い（新しい画面でない）・別の窓なら触らない。
    if (w.stage != "detail" || w.kind != "dialog" || w.continue_key != 13 || frame.text_input_active || frame.numeric.active
        || !frame.prompt.choices.empty()) {
        return false;
    }
    std::vector<StatRow> stats;
    std::vector<std::string> labels;
    int used = 0, limit = 0;
    if (!read_screen(frame, stats, labels, used, limit)) {
        return false;
    }
    const bool ja = w.language == "ja";
    const auto tr = [&](const char *jp, const char *en) { return std::string(ja ? jp : en); };

    // 原作のカーソルがある行（基本値の右）。ミラーの行の添字で来る。
    int selected_source = kRowFirstStat;
    if (frame.menu_term_curs_row >= 0 && frame.menu_term_curs_row < static_cast<int>(frame.menu_term_lines.size())) {
        const int row = frame.menu_term_lines[static_cast<size_t>(frame.menu_term_curs_row)].source_row;
        if (row >= kRowFirstStat && row < kRowFirstStat + kStatCount) {
            selected_source = row;
        }
    }

    w.stage = "cursor";
    w.content_type = "sil_allocation";
    w.title = tr(u8c(u8"能力値の設定"), "Attributes");
    w.subtitle = tr(u8c(u8"コスト合計"), "Total cost") + ": " + std::to_string(used) + "/" + std::to_string(limit);
    w.rows.clear();
    w.content.clear();
    w.actions.clear();
    w.cursor = -1;
    w.cursor_target.clear();
    w.continue_key = 13;
    w.input.clear();

    // 右の欄: 性別・種族・クラス（行 3〜5。`名前 : 値`）
    {
        ModernBlock identity;
        identity.kind = "fields";
        for (int row = 3; row <= 5; ++row) {
            const TermMirrorLine *line = line_at_row(frame, row);
            if (!line) {
                continue;
            }
            const size_t colon = line->text_utf8.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            const std::string label = trim(line->text_utf8.substr(0, colon)), value = trim(line->text_utf8.substr(colon + 1));
            if (!label.empty() && !value.empty()) {
                identity.cells.push_back({ label, value, false, "", "" });
            }
        }
        if (!identity.cells.empty()) {
            w.content.push_back(std::move(identity));
        }
    }
    // 右の欄: コスト合計と残り
    {
        ModernBlock pool;
        pool.kind = "fields";
        pool.cells.push_back({ tr(u8c(u8"コスト合計"), "Total cost"), std::to_string(used) + "/" + std::to_string(limit), false, "", "" });
        pool.cells.push_back({ tr(u8c(u8"残りポイント"), "Points left"), std::to_string(limit - used), false, "", "" });
        w.content.push_back(std::move(pool));
    }
    // 右の欄: 原作の表（基本値・最小値・最大値・コスト・種族・合計値）。
    // 窓の表は 1 行に 5 列までしか並ばない（それ以上は次の行へ折り返して列がずれる）ので、5 列にまとめる:
    // 能力 | 基本値 | 最小〜最大 | コスト | 合計値（種族の補正）。字はすべて原作が描いたもの。
    ModernBlock head;
    head.kind = "table_header";
    const std::vector<std::string> fallback = ja
        ? std::vector<std::string>{ u8c(u8"基本値"), u8c(u8"最小値"), u8c(u8"最大値"), u8c(u8"コスト"), u8c(u8"種族"), u8c(u8"合計値") }
        : std::vector<std::string>{ "Base", "Min", "Max", "Cost", "Race", "Total" };
    const auto label = [&](size_t i) { return labels.size() == 6 && !labels[i].empty() ? labels[i] : fallback[i]; };
    head.cells.push_back({ "", tr(u8c(u8"能力"), "Stat"), false, "", "" });
    head.cells.push_back({ "", label(0), false, "", "" });
    head.cells.push_back({ "", tr(u8c(u8"最小〜最大"), "Min-Max"), false, "", "" });
    head.cells.push_back({ "", label(3), false, "", "" });
    head.cells.push_back({ "", label(5) + "(" + label(4) + ")", false, "", "" });
    w.content.push_back(std::move(head));

    std::vector<Control> controls;
    for (const StatRow &stat : stats) {
        const bool active = stat.source_row == selected_source;
        int base = 0, min = 0, max = 0;
        const bool numbers = to_int(stat.base, base) && to_int(stat.min, min) && to_int(stat.max, max);
        const std::string first_id = "allocation:" + std::to_string(stat.source_row) + ":0";
        ModernBlock block;
        block.kind = "table_row";
        block.cells.push_back({ "", stat.name, false, "", "" });
        block.cells.push_back({ "", stat.base, false, "", "" });
        block.cells.push_back({ "", stat.min + (ja ? u8c(u8"〜") : "-") + stat.max, false, "", "" });
        block.cells.push_back({ "", stat.cost, false, "", "" });
        block.cells.push_back({ "", stat.total + " (" + stat.race + ")", false, "", "" });
        if (active) {
            block.cells.front().selected = true;
            block.cells.front().target = first_id;
            w.cursor_target = first_id;
        }
        w.content.push_back(std::move(block));
        for (int column = 0; column < 2; ++column) {
            const int key = column ? '6' : '4';
            ModernRow row;
            row.id = "allocation:" + std::to_string(stat.source_row) + ":" + std::to_string(column);
            row.label = stat.name + (column ? " +" : u8c(u8" −"));
            row.slot = stat.source_row;
            row.selection = 0;
            row.key = row.hotkey = key;
            row.price_known = false;
            // 下限・上限は原作が描いた最小値・最大値（最大値はいま残りのポイントで届く上限）。
            row.available = !numbers || (column ? base < max : base > min);
            if (!row.available) {
                row.reason = column ? tr(u8c(u8"これ以上は上げられません（上限か、ポイントが足りません）。"), "Cannot raise this stat further (limit or not enough points).")
                                    : tr(u8c(u8"これ以上は下げられません（最小値です）。"), "Cannot lower this stat further (minimum).");
            }
            row.description = stat.name + "\n\n"
                + (column ? tr(u8c(u8"この能力を 1 上げます（ポイントを使います）。"), "Raise this stat by one (spends points).")
                          : tr(u8c(u8"この能力を 1 下げます（ポイントが戻ります）。"), "Lower this stat by one (refunds points)."));
            row.actions = { { "choice", row.label, row.reason, row.available, key } };
            controls.push_back({ row.id, key, row.slot, row.available });
            w.rows.push_back(std::move(row));
        }
    }
    if (w.cursor_target.empty()) {
        w.cursor_target = w.rows.front().id;
    }
    // 右の欄: 原作の注意書き（行 7）と紙幣への変換（行 21）
    for (const int row : { 7, kRowNote }) {
        if (const TermMirrorLine *line = line_at_row(frame, row)) {
            const std::string text = trim(line->text_utf8);
            if (!text.empty()) {
                ModernBlock note;
                note.kind = "paragraph";
                note.cells.push_back({ "", text, false, "", "" });
                w.content.push_back(std::move(note));
            }
        }
    }
    ModernRow done;
    done.id = "allocation:continue";
    done.label = tr(u8c(u8"次に進む"), "Continue");
    done.slot = 1000000000;
    done.key = done.hotkey = 13;
    done.price_known = false;
    done.actions = { { "choice", done.label, "", true, 13 } };
    controls.push_back({ done.id, 13, done.slot, true });
    w.rows.push_back(std::move(done));

    w.body.clear();
    for (const auto &block : w.content) {
        if (!block.title.empty()) {
            w.body += block.title + "\n";
        }
        for (const auto &cell : block.cells) {
            if (!cell.label.empty()) {
                w.body += cell.label + ": ";
            }
            w.body += cell.text + '\n';
        }
        w.body += '\n';
    }
    w.cursor_span_begin = -1;
    w.cursor_span_len = 0;

    g_state.active = true;
    g_state.cursor_target = w.cursor_target;
    g_state.controls = std::move(controls);
    return true;
}

bool birth_stats_input(const presentation::InputEventWire &event, std::vector<int> &keys, presentation::NativeModernMenu &menu)
{
    if (!g_state.active || event.e != "ui_action") {
        return false;
    }
    const auto find = [&](const std::string &id) -> const Control * {
        for (const auto &control : g_state.controls) {
            if (control.id == id) {
                return &control;
            }
        }
        return nullptr;
    };
    int key = 0;
    if (event.action == "focus") {
        // 原作のカーソルを目的の能力値の行へ 1 行ずつ動かす（`2`＝次の行・`8`＝前の行）。
        const Control *wanted = find(event.target), *current = find(g_state.cursor_target);
        if (wanted && current && wanted->slot != current->slot) {
            key = wanted->slot > current->slot ? '2' : '8';
        }
    } else if (event.action == "choice") {
        // 対の行（− と ＋）は、原作のカーソルが着いた行に対して `4` か `6`。決定の行は `Enter`。
        const Control *control = find(event.target);
        if (control && control->available) {
            key = control->key;
        }
    } else if (event.action == "continue") {
        key = 13;
    } else if (event.action == "cancel") {
        // 戻る＝性別・種族・クラスの選択からやり直す（原作の `S`）。ESC は原作では「決定」なので、そのままは送らない。
        key = 'S';
    } else if (event.action == "navigate" && (event.number == 2 || event.number == 4 || event.number == 6 || event.number == 8)) {
        key = static_cast<int>('0' + event.number);
    }
    if (!key) {
        return false;
    }
    // 版・要求番号の管理は NativeModernMenu に任せる（同じ要求を「続ける」にして通す）。受理されたときだけキーを返す。
    presentation::InputEventWire ledger = event;
    ledger.action = "continue";
    ledger.target.clear();
    std::vector<int> accepted;
    menu.input(ledger, accepted);
    if (!accepted.empty()) {
        keys.push_back(key);
    }
    return true;
}

int birth_stats_selftest()
{
    int failed = 0;
    const auto check = [&](bool ok, const char *what) {
        if (ok) {
            return;
        }
        std::fprintf(stderr, "[toband] birth stats selftest FAIL: %s\n", what);
        ++failed;
    };
    //! ミラーは行ごとに 1 つずつ並ぶ（`menu_term_curs_row` はその添字）ので、足りない行は空の行で埋める。
    const auto add_line = [](GameFrame &frame, int row, const std::string &text) {
        while (static_cast<int>(frame.menu_term_lines.size()) <= row) {
            TermMirrorLine blank;
            blank.source_row = static_cast<int>(frame.menu_term_lines.size());
            frame.menu_term_lines.push_back(std::move(blank));
        }
        frame.menu_term_lines[static_cast<size_t>(row)].text_utf8 = text;
    };
    // 実機で読んだ原作の画面（男性・人間・ナイト。基本値は 腕力 15 ほか、コスト合計 34/60）。
    GameFrame frame;
    frame.pre_game_menu = true;
    frame.menu_open = true;
    add_line(frame, 1, u8c(u8"  キャラクタの能力値を設定してください。"));
    add_line(frame, 3, u8c(u8" 性別   : 男性"));
    add_line(frame, 4, u8c(u8" 種族   : 人間"));
    add_line(frame, 5, u8c(u8" クラス : ナイト"));
    add_line(frame, 11, u8c(u8"          コスト合計: 34/60  2/8/j/k で移動,  4/6/h/l で変更,  Enter で決定"));
    add_line(frame, 13, u8c(u8"                  基本値  最小値  最大値  (コスト)  種族     合計値"));
    const char *const stats[6][7] = {
        { u8c(u8"腕力"), "15", "15", "18", "11", "+0", "15" }, { u8c(u8"知能"), "10", "10", "17", " 0", "+0", "10" },
        { u8c(u8"賢さ"), "10", "10", "17", " 0", "+0", "10" }, { u8c(u8"器用"), "16", "16", "18", "16", "+0", "16" },
        { u8c(u8"耐久"), "14", "14", "18", " 7", "+0", "14" }, { u8c(u8"魅力"), "10", "10", "17", " 0", "+0", "10" } };
    for (int i = 0; i < 6; ++i) {
        const auto *v = stats[i];
        char buf[160];
        std::snprintf(buf, sizeof(buf), "          %s :      %s      %s      %s      (%s)    %s  =      %s", v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        add_line(frame, 14 + i, buf);
    }
    frame.menu_term_curs_row = 15; // 知能の行にカーソル
    frame.modern.version = 5;
    frame.modern.kind = "dialog";
    frame.modern.stage = "detail";
    frame.modern.continue_key = 13;
    frame.modern.language = "ja";
    // capture() を通さず、窓の版は 1000001 とする（入力の翻訳の確かめは NativeModernMenu に同じ窓を作らせて行う）。
    check(apply_birth_stats_window(frame), "the stat screen is recognised");
    const ModernWindow &w = frame.modern;
    check(w.stage == "cursor" && w.content_type == "sil_allocation", "stage is cursor and the window kind is sil_allocation");
    check(w.rows.size() == 13 && w.rows.back().id == "allocation:continue", "12 stat rows (minus/plus pairs) and the continue row");
    check(w.cursor_target == "allocation:15:0", "the native cursor row is the window cursor");
    check(!w.rows[0].available && w.rows[1].available, "a stat at its minimum cannot be lowered but can be raised");
    check(w.subtitle == std::string(u8c(u8"コスト合計")) + ": 34/60", "the total cost is read from the original line");
    bool table_ok = false;
    for (const auto &block : w.content) {
        if (block.kind == "table_row" && block.cells.size() == 5 && block.cells[0].text == u8c(u8"知能")) {
            table_ok = block.cells[1].text == "10" && block.cells[2].text == std::string("10") + u8c(u8"〜") + "17"
                && block.cells[3].text == "0" && block.cells[4].text == "10 (+0)";
        }
    }
    check(table_ok, "the table row for the selected stat is read from the original line (5 columns)");
    // 入力の翻訳（NativeModernMenu に「詳細画面」を 1 回 capture させ、その版で動かす）
    presentation::NativeModernMenu menu;
    menu.set_enabled(true);
    menu.set_language("ja");
    GameFrame live = frame;
    live.modern = ModernWindow{};
    menu.capture(live);
    check(apply_birth_stats_window(live), "the stat screen is recognised after capture()");
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
    check(birth_stats_input(event("focus", "allocation:17:0", 0, 1), keys, menu) && keys == std::vector<int>{ '2' },
        "focus on a lower row sends 2 (native cursor down)");
    keys.clear();
    check(!birth_stats_input(event("focus", "allocation:17:0", 0, 1), keys, menu) || keys.empty(), "a repeated request is not accepted twice");
    // 受理のあとは版が繰り上がる（次の capture で）。ここでは窓を作り直して続きを確かめる。
    GameFrame next = frame;
    next.modern = ModernWindow{};
    menu.capture(next);
    check(apply_birth_stats_window(next), "the stat screen is recognised again");
    live = next;
    keys.clear();
    check(birth_stats_input(event("choice", "allocation:15:1", 0, 2), keys, menu) && keys == std::vector<int>{ '6' }, "plus sends 6");
    GameFrame third = frame;
    third.modern = ModernWindow{};
    menu.capture(third);
    check(apply_birth_stats_window(third), "the stat screen is recognised a third time");
    live = third;
    keys.clear();
    check(!birth_stats_input(event("choice", "allocation:14:0", 0, 3), keys, menu) && keys.empty(), "a minus on a stat at its minimum sends nothing");
    keys.clear();
    check(birth_stats_input(event("choice", "allocation:continue", 0, 3), keys, menu) && keys == std::vector<int>{ 13 }, "continue sends Enter");
    // 別の画面の窓には触らない
    GameFrame other = frame;
    other.menu_term_lines[11].text_utf8.clear(); // 操作の案内の行が無ければ、能力値の割り振りの画面ではない
    other.modern = ModernWindow{};
    other.modern.kind = "dialog";
    other.modern.stage = "detail";
    other.modern.continue_key = 13;
    check(!apply_birth_stats_window(other), "a different screen is left alone");
    return failed;
}

} // namespace tb
