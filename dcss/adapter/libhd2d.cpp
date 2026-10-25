// DCSS コア: 端末の層（libconsole.h）の差し替え実装。
//
// 原作は、どの端末の実装を組むかを Makefile で選ぶ（Unix は libunix.cc＝curses、
// Windows は libw32c.cc＝コンソール）。ウェブタイル版（USE_TILE_WEB）の送受信は
// libunix.cc の中にだけあり、Windows の libw32c.cc には無い。このファイルは、
// libunix.cc の「-headless」の分岐（curses を使わず、tileweb.cc の tiles.* だけを呼ぶ）
// と同じ振る舞いを、curses 抜きで実装し直したもの。原作のファイルは書き換えない。
//
// 画面の中身（地図・メニュー・メッセージ）は tileweb.cc が JSON で送る。この層は
//   - キー入力: tiles.await_input() / tiles.try_await_input()（受け口は dcss_posix_shim.cpp）
//   - 文字の描画: 何も描かず、tiles.* に文字・色・カーソルの動きだけを伝える
// の 2 つだけをする。端末の大きさは固定（80×24）。
#include "AppHdr.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "cio.h"
#include "cloud.h"
#include "colour.h"
#include "env.h"
#include "libutil.h"
#include "state.h"
#include "tiles-build-specific.h"
#include "jobs.h"
#include "player.h"
#include "species.h"
#include "unicode.h"
#include "ui.h"
#include "view.h"
#include "viewgeom.h"
#include "version.h"

#include "dcss_posix_shim.h"
#include "dcss_tabs_data.h" // 画面の部品（dcss-ui）のタブの欄の中身と操作（段 U2）
#include "dcss_pad_data.h" // 画面の部品（dcss-ui）のゲームパッドの割り当てへ送る、割り当てられるコマンドの表（段 5。dcss_tabs_data.h の後）
#include "dcss_keymap_data.h" // 画面の部品（dcss-ui）の操作の割り当てが読み書きする、キーボードの割り当て＝原作のキーマップ（D33。dcss_pad_data.h の後）
#include "dcss_ui_mouse.h" // 画面の部品（dcss-ui）の地図のマウス操作（足元のクリック・ツールチップ。段 U3・U4）
#include "dcss_ui_startup.h" // 画面の部品（dcss-ui）の開始メニューの中身（モードの一覧・セーブデータと人形。段 U3）

// libw32c.h / libunix.h が宣言している物のうち、他の原作ファイルから参照される物。
void fakecursorxy(int x, int y);
bool in_headless_mode();
void enter_headless_mode();
void w32_cancel_waiting_for_input();

#define HD2D_LINES 24
#define HD2D_COLS 80

// 内部の座標は 1 始まり（原作の習慣）。
static int s_x = 1;
static int s_y = 1;
static bool s_cursor_enabled = true;
static int s_tiles_pending_key = 0;

// 原作の「ヘッドレス」ではない。ヘッドレス（-headless）にすると原作は
// 「-test などの引数が無ければ起動しない」と判断する（initfile.cc）ので、常に偽を返す。
bool in_headless_mode()
{
    return false;
}

void enter_headless_mode()
{
}

// libutil.cc の console_handler（ウィンドウを閉じたとき）が呼ぶ。入力待ちを解く物は
// 受け口の側（標準入力の EOF で Esc が返る）にあるので、ここでは何もしない。
void w32_cancel_waiting_for_input()
{
}

static void on_input_closed()
{
    // 標準入力が閉じた＝画面側が居なくなった。原作の SIGHUP と同じ扱いにして、
    // ゲームが保存して終わる道に乗せる（getch_ck() が seen_hups を見て Esc を返す）。
    crawl_state.seen_hups++;
}

void console_startup()
{
    dcss_shim_set_eof_callback(on_input_closed);
    tiles.resize();
}

void console_shutdown()
{
}

int get_number_of_lines()
{
    return HD2D_LINES;
}

int get_number_of_cols()
{
    return HD2D_COLS;
}

int num_to_lines(int num)
{
    return num;
}

void clrscr_sys()
{
    s_x = 1;
    s_y = 1;
}

void clear_to_end_of_line()
{
    tiles.clear_to_end_of_line();
}

void gotoxy_sys(int x, int y)
{
    s_x = x;
    s_y = y;
}

void textcolour(int col)
{
    tiles.textcolour(col);
}

void textbackground(int col)
{
    tiles.textbackground(col);
}

COLOURS default_hover_colour()
{
    return BLUE;
}

lib_display_info::lib_display_info()
    : type(CRAWL_BUILD_NAME),
      term("hd2d"),
      fg_colors(16),
      bg_colors(16)
{
}

void putwch(char32_t chr)
{
    // 折り返しとカーソルの動きを真似る（libunix.cc の -headless と同じ）。
    const wchar_t c = (wchar_t)chr;
    s_x += c ? wcwidth(chr) : 0;
    if (s_x >= HD2D_COLS && s_y >= HD2D_LINES)
    {
        s_x = HD2D_COLS;
        s_y = HD2D_LINES;
    }
    else if (s_x > HD2D_COLS)
    {
        s_y++;
        s_x = s_x - HD2D_COLS;
    }

    char32_t buf[2];
    buf[0] = chr;
    buf[1] = 0;
    tiles.put_ucs_string(buf);
}

void cprintf(const char *format, ...)
{
    char buffer[2048];

    va_list argp;
    va_start(argp, format);
    vsnprintf(buffer, sizeof(buffer), format, argp);
    va_end(argp);

    char32_t c;
    char *bp = buffer;
    while (int s = utf8towc(&c, bp))
    {
        bp += s;
        putwch(c);
    }
}

void puttext(int x1, int y1, const crawl_view_buffer &vbuf)
{
    const screen_cell_t *cell = vbuf;
    const coord_def size = vbuf.size();
    for (int y = 0; y < size.y; ++y)
    {
        cgotoxy(x1, y1 + y);
        for (int x = 0; x < size.x; ++x)
        {
            put_colour_ch(cell->colour, cell->glyph);
            cell++;
        }
    }
}

void update_screen()
{
    tiles.set_need_redraw();
}

void set_cursor_enabled(bool enabled)
{
    s_cursor_enabled = enabled;
    tiles.set_text_cursor(enabled);
}

bool is_cursor_enabled()
{
    return s_cursor_enabled;
}

void set_mouse_enabled(bool)
{
}

void fakecursorxy(int x, int y)
{
    gotoxy_sys(x, y);
    set_cursor_region(GOTO_CRT);
}

int wherex()
{
    return s_x;
}

int wherey()
{
    return s_y;
}

void delay(unsigned int ms)
{
    if (crawl_state.disables[DIS_DELAY])
        return;

    tiles.redraw();
    if (ms)
    {
        tiles.send_message("{\"msg\":\"delay\",\"t\":%d}", ms);
        tiles.flush_messages();
        // 原作の手元のタイル版と同じに、ここまでの絵を画面へ送ってから ms だけ待つ。送らないと、旅・自動探索の
        // 途中の歩みが画面に出ず、最後の位置へ飛んだように見えた。
        dcss_shim_game_delay(ms);
    }
}

// MSVC の wint_t は 16 ビット（unsigned short）。原作の特殊キー（矢印などの `CK_*`、命令を符号にしたもの = 負の数）はウェブタイル版の
// `await_input` が wint_t で返すので 16 ビットに切られ、正の数に化ける（Linux は 32 ビットなので起きない）。負の数の範囲（-3000〜-1）は
// 0xF448〜0xFFFF に写る（字として使われない私用領域の端と特殊字）ので、そこは負の数へ戻す。画面のタブの欄の操作の合図（kActionKey）は字として扱う。
// ゲームパッドの枠のキー（`CK_PAD_BASE`〜`CK_PAD_LAST`。-5000〜-5063 = 0xEC39〜0xEC78）と「無視するキー」（`CK_NO_KEY`。-9989 = 0xD8FB）も、
// 画面が割り当ての無いボタン・コマンドを受けない場面のボタンの押しとして送るので負の数へ戻す（段 5）。マウスの `CK_MOUSE_*` は従来どおり戻さない。
static int widen_key(wint_t c)
{
#ifdef _WIN32
    if (c >= 0xF448 && c <= 0xFFFF && static_cast<int>(c) != dcss_tabs_data::kActionKey)
        return static_cast<int>(static_cast<short>(c));
    if ((c >= static_cast<wint_t>(CK_PAD_LAST) && c <= static_cast<wint_t>(CK_PAD_BASE)) || c == static_cast<wint_t>(CK_NO_KEY))
        return static_cast<int>(static_cast<short>(c));
#endif
    return static_cast<int>(c);
}

static int tiles_getchk()
{
    tiles.redraw();

    if (s_tiles_pending_key)
    {
        wint_t c = s_tiles_pending_key;
        s_tiles_pending_key = 0;
        return widen_key(c);
    }

    if (crawl_state.seen_hups)
        return ESCAPE;

    wint_t c = tiles.await_input([]() { return false; });
    if (c != 0)
        return widen_key(c);

    return ESCAPE;
}

int getch_ck()
{
    for (;;)
    {
        const int c = tiles_getchk();
        // 画面のタブの欄（持ち物・呪文など）への操作。原作のタイル版は区画の handle_mouse をこの入力待ちの中で呼び、
        // 実行したら CK_MOUSE_CMD を返す。同じ道を通す（場面に合わない操作は捨てて、入力待ちを続ける）。
        if (c == dcss_tabs_data::kActionKey)
        {
            if (dcss_tabs_data::run_pending_action())
                return CK_MOUSE_CMD;
            continue;
        }
        if ((c == CK_MOUSE_MOVE || c == CK_MOUSE_CLICK) && !crawl_state.mouse_enabled)
            continue;
        // 原作のタイル版の `getch_ck` と同じく、無視するキー（画面がパッドの押しを「何もしない」代わりに送る。`kbhit` を真にして自動探索などを止める）は読み捨てる。
        if (c == CK_NO_KEY)
            continue;
        return c;
    }
}

bool kbhit()
{
    if (s_tiles_pending_key)
        return true;
    if (crawl_state.seen_hups)
        return false;
    wint_t c = tiles.try_await_input();
    if (c != 0)
    {
        s_tiles_pending_key = c;
        return true;
    }
    return false;
}

// 画面の部品（dcss-ui）へ渡す、種族と職業の略称。原作の小さい配置のステータス欄 2 行目（`output.cc` の `_nowrap_eol_cprintf_touchui`）が
// `species::get_abbrev` と `get_job_abbrev` をつなげて出す字と同じ。ウェブタイル版の JSON には無い。
std::string dcss_adapter_char_abbrev()
{
    if (you.species == SP_UNKNOWN || you.char_class == JOB_UNKNOWN)
        return std::string();
    return std::string(species::get_abbrev(you.species)) + get_job_abbrev(you.char_class);
}

// 画面の部品（dcss-ui）のタブの欄の中身（`{"t":"dcss_ui","k":"tabs",…}` の JSON）。キャラクターがまだ決まっていなければ空。ゲームのスレッドで呼ぶ。
std::string dcss_adapter_tabs_json()
{
    return dcss_tabs_data::build_json();
}

// 画面のタブの欄への操作（`{"t":"dcss_action",…}` の JSON）を控える。受信スレッドから呼んでよい。実行は入力待ちの getch_ck の中。
void dcss_adapter_queue_tab_action(const std::string &json)
{
    dcss_tabs_data::queue_action(json);
}

// 控えた操作を入力待ちへ知らせるキーの番号（dcss_main.cpp が `{"msg":"key","keycode":N}` で送る）。
int dcss_adapter_tab_action_key()
{
    return dcss_tabs_data::kActionKey;
}

// 画面の部品（dcss-ui）の地図のマウス操作（足元のクリック・ツールチップ）。recvfrom が `hd2d_` で始まる行を渡してくる（dcss_posix_shim.cpp）。
std::string dcss_adapter_handle_ui_line(const std::string &line, std::string &reply)
{
    // 名前の問いの Enter: 同じ名前のセーブが在るか（上書きの確認は原作の窓の中で待つだけでウェブへ出ないので、画面が出すために読む）。
    if (line.find("\"hd2d_name_check\"") != std::string::npos)
    {
        reply = dcss_ui_startup::name_check_reply(line);
        return std::string();
    }
    // 操作の割り当て（機能メニュー）のキーボードの割り当て＝原作のキーマップ（macro.txt の K:）の読み書き（D33）。
    if (line.find("\"hd2d_keymap\"") != std::string::npos)
    {
        reply = dcss_keymap_data::handle(line);
        return std::string();
    }
    return dcss_ui_mouse::handle_line(line, reply);
}

// 画面の部品（dcss-ui）のゲームパッドの割り当てへ送る、割り当てられるコマンドの表（`{"t":"dcss_ui","k":"pad",…}` の JSON。dcss_pad_data.h）。
// ゲームが始まる前はキーの割り当てが整っていないので空。ゲームのスレッドで呼ぶ。
std::string dcss_adapter_pad_json()
{
    if (!crawl_state.game_started)
        return std::string();
    return dcss_pad_data::build_json();
}

// 画面の部品（dcss-ui）の開始メニューの中身（`{"t":"dcss_ui","k":"startup",…}` の JSON。dcss_ui_startup.h）。ゲームが始まっていれば空。ゲームのスレッドで呼ぶ。
std::string dcss_adapter_startup_json()
{
    return dcss_ui_startup::build_json();
}

// 画面の部品（dcss-ui）の立体の雲（段 3・D14）のための、そのマスの雲の見通し。原作の `is_opaque_cloud`（雲の種類ごとの表の `opaque`。煙・蒸気・墨など）を読む。
// ウェブタイル版の JSON には載らない。座標はプレイヤーのマスから見た相対（原作のウェブタイル版の座標は `m_origin` からの相対で、足元の `you.pos()` に足して使う）。-1 = 雲が無い（覚えが無い）・0 = 視界を遮らない・1 = 視界を遮る。ゲームのスレッドで呼ぶ。
int dcss_adapter_cloud_kind(int dx, int dy)
{
    const coord_def p = you.pos() + coord_def(dx, dy);
    if (!in_bounds(p))
        return -1;
    const cloud_type type = env.map_knowledge(p).cloud();
    if (type == CLOUD_NONE)
        return -1;
    return is_opaque_cloud(type) ? 1 : 0;
}

// 画面の部品（dcss-ui）の攻撃の演出（段 4）のための、いまプレイヤー自身の行動の最中か。原作の `you.turn_is_over` を読む（書かない）。
// 偽 = プレイヤーが撃った・投げた・唱えた最中（コマンドの処理の途中。行動を終えると真になる）、真 = 世界が反応している最中（怪物・味方の行動）。
// ウェブタイル版の JSON には弾の撃ち手が載らない。画面は、同じマスの並びが一度に届いたビームの弾を、撃ち手の側から順に飛ばすために使う。ゲームのスレッドで呼ぶ。
bool dcss_adapter_world_reacting()
{
    return you.turn_is_over;
}

int dcss_adapter_seen_hups()
{
    return crawl_state.seen_hups;
}

extern bool CrawlIsCrashing; // end.cc: `end(exit_code, …)` が、0 でない終了コードのとき立てる

bool dcss_adapter_exit_failed()
{
    return CrawlIsCrashing;
}

void dcss_adapter_test_mark_failed()
{
    CrawlIsCrashing = true;
}
