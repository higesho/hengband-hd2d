/*
 * Umoria の端末入出力。上流の `src/ui_io.cpp`（curses）を組まず、同じ関数をここで実装する
 * （「M0 の記録」の 32 個）。
 *
 * 字は 24×80 の格子へ書くだけで、画面側へ送るのは um_main.cpp の present である。
 * ゲームの振る舞い（-more- の待ち方、[y/n] の解釈、文字入力の規則）は上流に合わせる。
 */

#include "headers.h"
#include "um_host.h"
#include "um_frame.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

int eof_flag = 0;
bool panic_save = false;
bool umoria_command_input_active = false;

namespace {

using Grid = std::array<std::array<char, umoria::kTermCols>, umoria::kTermRows>;
/*
 * マスごとの「書いたかたまり」（run）の番号。1 回の `putString` などで
 * 書いた字は同じ番号を持つ。0 は空き。訳はこの単位で当てる（格子そのものは英語のまま。アダプタが字を読んで
 * 問いを見分けているため）。
 */
using RunGrid = std::array<std::array<std::uint32_t, umoria::kTermCols>, umoria::kTermRows>;

Grid g_screen;
Grid g_saved;
RunGrid g_run;
RunGrid g_saved_run;
std::uint32_t g_next_run = 0;
std::uint32_t g_current_run = 0;
int g_cur_row = 0;
int g_cur_col = 0;
bool g_text_input = false;
int g_saved_depth = 0;
/*
 * 退避の後に、地図の場所（1〜22 行目・状態の列より右）へ字の列を書いたか。持ち物の一覧などの画面は putString で書き、
 * 地図の変化（感知した物・怪物の移動）は panelPutTile で 1 マスずつ書くので、これで「画面が地図のままか」を見分ける。
 */
bool g_overlay_since_save = false;
int g_text_input_col = 0;
int g_text_input_row = 0;
umoria::Question g_question = umoria::Question::None;
umoria::HostHooks g_hooks;

void fill_blank(Grid &grid)
{
    for (auto &row : grid) row.fill(' ');
}

void fill_blank(RunGrid &grid)
{
    for (auto &row : grid) row.fill(0);
}

struct Init {
    Init()
    {
        fill_blank(g_screen);
        fill_blank(g_saved);
        fill_blank(g_run);
        fill_blank(g_saved_run);
    }
} g_init;

bool in_range(int row, int col)
{
    return row >= 0 && row < umoria::kTermRows && col >= 0 && col < umoria::kTermCols;
}

void set_cursor(int row, int col)
{
    g_cur_row = row < 0 ? 0 : (row >= umoria::kTermRows ? umoria::kTermRows - 1 : row);
    g_cur_col = col < 0 ? 0 : (col >= umoria::kTermCols ? umoria::kTermCols - 1 : col);
}

//! 字を 1 つ置いてカーソルを右へ進める（行末では止まる）。
void put_at_cursor(char ch)
{
    if (!in_range(g_cur_row, g_cur_col)) return;
    g_screen[g_cur_row][g_cur_col] = (ch == '\0') ? ' ' : ch;
    g_run[g_cur_row][g_cur_col] = g_current_run;
    if (g_cur_col < umoria::kTermCols - 1) ++g_cur_col;
}

void write_text(int row, int col, const char *text)
{
    g_current_run = ++g_next_run;
    set_cursor(row, col);
    for (const char *p = text; *p != '\0' && g_cur_col < umoria::kTermCols; ++p) {
        if (*p == '\n') break;
        const bool last = g_cur_col == umoria::kTermCols - 1;
        put_at_cursor(*p);
        if (last) break;
    }
}

void clear_line_from(int row, int col)
{
    if (row < 0 || row >= umoria::kTermRows) return;
    for (int c = col < 0 ? 0 : col; c < umoria::kTermCols; ++c) {
        g_screen[row][c] = ' ';
        g_run[row][c] = 0;
    }
}

void present()
{
    if (g_hooks.present != nullptr) g_hooks.present();
}

bool shutting_down()
{
    return g_hooks.shutdown_requested != nullptr && g_hooks.shutdown_requested();
}

//! 画面側からのキーを 1 つ待つ。画面側が居なくなったら -1。
int wait_key()
{
    present();
    for (;;) {
        if (shutting_down()) return -1;
        const int key = g_hooks.next_key != nullptr ? g_hooks.next_key() : 0;
        if (key > 0) return key;
        if (g_hooks.sleep_ms != nullptr) g_hooks.sleep_ms(5);
    }
}

/*
 * 画面側が居なくなったときの扱い。プロトコルの正本（CORE_PROTOCOL_V1.md §9.2）は
 * 「緊急セーブして終了」。上流が標準入力の EOF で取る道（ESC を返し続け、
 * 生成済みのキャラは保存して終わる）と同じ結果になるようにする。
 */
char on_input_closed()
{
    message_ready_to_print = false;
    ++eof_flag;
    if (!game.character_generated || game.character_saved) endGame();
    playerDisturb(1, 0);
    if (eof_flag > 100) {
        panic_save = true;
        (void) strcpy(game.character_died_from, "(end of input: panic saved)");
        if (!saveGame()) {
            (void) strcpy(game.character_died_from, "panic: unexpected eof");
            game.character_is_dead = true;
        }
        endGame();
    }
    return ESCAPE;
}

} // namespace

namespace umoria {

void set_host_hooks(const HostHooks &hooks) { g_hooks = hooks; }
char term_char(int row, int col) { return in_range(row, col) ? g_screen[row][col] : ' '; }
std::uint32_t term_run(int row, int col) { return in_range(row, col) ? g_run[row][col] : 0; }
int term_cursor_row() { return g_cur_row; }
int term_cursor_col() { return g_cur_col; }
bool term_text_input_active() { return g_text_input; }
int term_text_input_col() { return g_text_input_col; }
int term_text_input_row() { return g_text_input_row; }
bool term_screen_saved() { return g_saved_depth > 0; }
bool term_overlay_since_save() { return g_overlay_since_save; }
Question term_question() { return g_question; }

} // namespace umoria

bool terminalInitialize()
{
    fill_blank(g_screen);
    fill_blank(g_run);
    set_cursor(0, 0);
    return true;
}

void terminalRestore() { putQIO(); }

void terminalSaveScreen()
{
    g_saved = g_screen;
    g_saved_run = g_run;
    g_saved_depth = 1;
    g_overlay_since_save = false; // 原作は入れ子にしない（保存の受け皿が 1 枚だけ）
}

void terminalRestoreScreen()
{
    g_screen = g_saved;
    g_run = g_saved_run;
    g_saved_depth = 0;
    g_overlay_since_save = false;
}

ssize_t terminalBellSound()
{
    putQIO();
    return 0;
}

void putQIO()
{
    screen_has_changed = true;
    present();
}

void flushInputBuffer()
{
    if (eof_flag != 0) return;
    while (g_hooks.next_key != nullptr && g_hooks.next_key() > 0) {
    }
}

void clearScreen()
{
    if (message_ready_to_print) printMessage(CNIL);
    fill_blank(g_screen);
    fill_blank(g_run);
    set_cursor(0, 0);
    g_saved_depth = 0; // 全部消して描き直すなら、退避した画面へはもう戻らない
}

void clearToBottom(int row)
{
    for (int r = row < 0 ? 0 : row; r < umoria::kTermRows; ++r) clear_line_from(r, 0);
    set_cursor(row, 0);
}

void moveCursor(Coord_t coord) { set_cursor(coord.y, coord.x); }

void addChar(char ch, Coord_t coord)
{
    g_current_run = ++g_next_run;
    set_cursor(coord.y, coord.x);
    put_at_cursor(ch);
}

void putString(const char *out_str, Coord_t coord)
{
    // 状態の列（0〜12 桁）の中だけに書く字は地図の画面の書き直し。0 桁目から状態の列を越えて書く字（オプションの本文）は窓（2026-10-04）。
    if (g_saved_depth > 0 && coord.y >= 1 && coord.y <= umoria::kTermRows - 2 && out_str != nullptr && out_str[0] != '\0'
        && (coord.x >= 13 || coord.x + static_cast<int>(std::strlen(out_str)) > 13)) {
        g_overlay_since_save = true;
    }
    // 右端（79 桁目）は使わない。上流と同じく、はみ出す分は切る。
    const int col = coord.x > 79 ? 79 : coord.x;
    std::string text(out_str != nullptr ? out_str : "");
    if (static_cast<int>(text.size()) > 79 - col) text.resize(static_cast<std::size_t>(79 - col));
    write_text(coord.y, col, text.c_str());
}

//! 文字の説明（`/`）で記号を受けた直後か。次に 0 行目へ書かれる説明を、メッセージとして履歴に残す（修-43）。
static bool g_describe_tile_pending = false;

void putStringClearToEOL(const std::string &str, Coord_t coord)
{
    if (g_describe_tile_pending && coord.y == MSG_LINE && coord.x == 0) {
        /*
         * 原作 identifyGameObject は、記号の説明（「! - A potion.」）を 0 行目へ直に書くだけでメッセージにしない。
         * 画面側はメッセージを履歴（messages[]）から読み、0 行目は問いの間しか見ないので、説明がどこにも出なかった。
         * ほかのメッセージと同じ経路（printMessage。0 行目への書き込みと履歴への登録）で出す。
         */
        g_describe_tile_pending = false;
        printMessage(str.c_str());
        return;
    }
    if (coord.y == MSG_LINE && message_ready_to_print) printMessage(CNIL);
    clear_line_from(coord.y, coord.x);
    putString(str.c_str(), coord);
}

void eraseLine(Coord_t coord)
{
    if (coord.y == MSG_LINE && message_ready_to_print) printMessage(CNIL);
    clear_line_from(coord.y, coord.x);
    set_cursor(coord.y, coord.x);
}

void panelMoveCursor(Coord_t coord)
{
    set_cursor(coord.y - dg.panel.row_prt, coord.x - dg.panel.col_prt);
}

void panelPutTile(char ch, Coord_t coord)
{
    addChar(ch, Coord_t{coord.y - dg.panel.row_prt, coord.x - dg.panel.col_prt});
}

void messageLinePrintMessage(std::string message)
{
    const int row = g_cur_row;
    const int col = g_cur_col;
    clear_line_from(MSG_LINE, 0);
    if (message.size() > 79) message.resize(79);
    write_text(MSG_LINE, 0, message.c_str());
    set_cursor(row, col);
}

void messageLineClear()
{
    const int row = g_cur_row;
    const int col = g_cur_col;
    clear_line_from(MSG_LINE, 0);
    set_cursor(row, col);
}

void printMessage(const char *msg)
{
    bool combine = false;
    int old_len = 0;

    if (message_ready_to_print) {
        old_len = static_cast<int>(std::strlen(messages[last_message_id])) + 1;
        const int new_len = msg != nullptr ? static_cast<int>(std::strlen(msg)) : 0;
        // 前の行と合わせて 1 行に収まるなら続けて書く。収まらなければ -more- で待つ。
        if (msg != nullptr && new_len + old_len + 2 < 73) {
            combine = true;
        } else {
            putString(" -more-", Coord_t{MSG_LINE, old_len > 73 ? 73 : old_len});
            /*
             * 地図の画面での -more- は待たずに進める（気づいたこと 2026-10-03「戦闘メッセージがいちいち
             * ウインドウに出る」「移動しているとやたらとちらつく」）。メッセージはサブパネルと履歴に残るので
             * 読める。ゲームのルールは変わらず、キーの待ちが無くなるだけ。メニュー・店などの画面では従来どおり待つ。
             */
            if (!(game.character_generated && !game.character_is_dead && umoria::map_screen_active())) {
                char key;
                do {
                    key = getKeyInput();
                } while (key != ' ' && key != ESCAPE && key != '\n' && key != '\r');
            }
        }
    }

    if (!combine) clear_line_from(MSG_LINE, 0);

    if (msg == nullptr) {
        message_ready_to_print = false;
        return;
    }

    game.command_count = 0;
    message_ready_to_print = true;

    if (combine) {
        putString(msg, Coord_t{MSG_LINE, old_len + 2});
        std::strcat(messages[last_message_id], "  ");
        std::strcat(messages[last_message_id], msg);
        return;
    }
    messageLinePrintMessage(msg);
    if (++last_message_id >= MESSAGE_HISTORY_SIZE) last_message_id = 0;
    std::strncpy(messages[last_message_id], msg, MORIA_MESSAGE_SIZE);
    messages[last_message_id][MORIA_MESSAGE_SIZE - 1] = '\0';
}

void printMessageNoCommandInterrupt(const std::string &msg)
{
    const int count = game.command_count;
    printMessage(msg.c_str());
    game.command_count = count;
}

char getKeyInput()
{
    /*
     * コマンドを待ち始めた時点では、画面は必ず地図に戻っている。原作は持ち物などの画面から戻るとき、
     * 退避した画面を戻さずに地図を描き直すことがあり、「退避中」の印が残る。残ったままだと、敵の手番の
     * 戦闘メッセージ（コマンド待ちではない）で地図ではないと判定され、字の頁の窓が出た
     * （全画面の「Message」の窓）。ここで下ろす。
     */
    if (umoria_command_input_active) g_saved_depth = 0;
    putQIO();
    game.command_count = 0;
    for (;;) {
        const int key = wait_key();
        if (key < 0) return on_input_closed();
        // ^R（再描画）は上流どおり読み捨てる。画面は毎回まるごと送っているので何もしなくてよい。
        if (key != CTRL_KEY('R')) return static_cast<char>(key);
    }
}

bool getCommand(const std::string &prompt, char &command)
{
    if (!prompt.empty()) putStringClearToEOL(prompt, Coord_t{0, 0});
    g_question = umoria::Question::Command;
    command = getKeyInput();
    g_question = umoria::Question::None;
    messageLineClear();
    return command != ESCAPE;
}

bool getTileCharacter(const std::string &prompt, char &command)
{
    // 記号を 1 字待つ（文字の説明・抹殺の巻物）。印を変えて、画面側に記号の一覧の選択肢を出させる（パッドで答えられなかった。2026-10-04）。
    if (!prompt.empty()) putStringClearToEOL(prompt, Coord_t{0, 0});
    g_question = umoria::Question::Tile;
    command = getKeyInput();
    g_question = umoria::Question::None;
    messageLineClear();
    // 文字の説明の問い（identifyGameObject）だけ。抹殺の巻物の問い（同じ関数）は説明を書かない。
    g_describe_tile_pending = command != ESCAPE && prompt.rfind("Enter character to be identified", 0) == 0;
    return command != ESCAPE;
}

bool getMenuItemId(const std::string &prompt, char &command) { return getCommand(prompt, command); }

bool getStringInput(char *in_str, Coord_t coord, int slen)
{
    const int start = coord.x;
    const int last = (coord.x + slen - 1) > 79 ? 79 : (coord.x + slen - 1);
    for (int c = start; c <= last && c < umoria::kTermCols; ++c) g_screen[coord.y][c] = ' ';
    set_cursor(coord.y, coord.x);

    char *p = in_str;
    bool done = false;
    bool aborted = false;
    g_text_input = true;
    g_text_input_col = start;
    g_text_input_row = coord.y;
    while (!done && !aborted) {
        const int key = getKeyInput();
        if (key == ESCAPE) {
            aborted = true;
        } else if (key == CTRL_KEY('J') || key == CTRL_KEY('M')) {
            done = true;
        } else if (key == CTRL_KEY('D')) {
            /*
             * 画面側の数値入力（`set_number`）は「Ctrl-B を 10 回・Ctrl-D を 10 回・数字」で欄を打ち直す
             * （presentation/bridge/input_event_adapter.cpp）。原作の欄は末尾にしか書けないので、
             * Ctrl-D は欄を空にする、Ctrl-B は何もしない、と読む（M3。交渉の値をパッドで入れるため）。
             */
            while (coord.x > start) {
                --coord.x;
                putString(" ", coord);
            }
            moveCursor(coord);
            p = in_str;
            *p = '\0';
        } else if (key == CTRL_KEY('B')) {
            // 上の注記のとおり何もしない（鳴らさない）。
        } else if (key == DELETE || key == CTRL_KEY('H')) {
            if (coord.x > start) {
                --coord.x;
                putString(" ", coord);
                moveCursor(coord);
                *--p = '\0';
            }
        } else if (std::isprint(static_cast<unsigned char>(key)) != 0 && coord.x <= last) {
            addChar(static_cast<char>(key), coord);
            *p++ = static_cast<char>(key);
            ++coord.x;
        } else {
            terminalBellSound();
        }
    }
    g_text_input = false;
    if (aborted) return false;
    while (p > in_str && p[-1] == ' ') --p;
    *p = '\0';
    return true;
}

bool getInputConfirmation(const std::string &prompt)
{
    return getInputConfirmationWithAbort(0, prompt) == 1;
}

int getInputConfirmationWithAbort(int column, const std::string &prompt)
{
    putStringClearToEOL(prompt, Coord_t{0, column});
    if (g_cur_col > 73) set_cursor(0, 73);
    write_text(g_cur_row, g_cur_col, " [y/n]");

    char key = ' ';
    g_question = umoria::Question::Confirm;
    while (key == ' ') key = getKeyInput();
    g_question = umoria::Question::None;
    messageLineClear();

    if (key == 'N' || key == 'n') return 0;
    if (key == 'Y' || key == 'y') return 1;
    return -1;
}

void waitForContinueKey(int line_number)
{
    putStringClearToEOL("[ press any key to continue ]", Coord_t{line_number, 23});
    (void) getKeyInput();
    eraseLine(Coord_t{line_number, 0});
}

/*
 * 走る・休むの最中に「キーが押されたら止める」ための問い合わせ。上流と同じく、
 * キーがあればそれを読み捨てて真を返す。待つ時間は使わない（キューを見るだけ）。
 */
bool checkForNonBlockingKeyPress(int microseconds)
{
    (void) microseconds;
    present();
    if (shutting_down()) {
        ++eof_flag;
        return false;
    }
    return g_hooks.next_key != nullptr && g_hooks.next_key() > 0;
}

/*
 * 既定の名前。上流は OS のアカウント名を使うが、実名を墓標やスコアへ出さないため
 * 使わない（Rogue 系と同じ判断。「プレイヤー表示名」）。
 */
void getDefaultPlayerName(char *buffer) { (void) std::strcpy(buffer, "X"); }

bool checkFilePermissions() { return true; }

#ifndef _WIN32
// ui.h が fopen/open をこの名前へ差し替えている。'~' の展開は使わないので素通しにする。
#undef fopen
#undef open
FILE *tfopen(const char *file, const char *mode) { return fopen(file, mode); }
int topen(const char *file, int flags, int mode) { return open(file, flags, mode); }
bool tilde(const char *file, char *expanded)
{
    if (file == nullptr) return false;
    (void) std::strcpy(expanded, file);
    return true;
}
#endif
