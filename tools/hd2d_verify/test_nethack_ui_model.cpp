// CPU-only test of the NetHack screen parts that need no GL: `hd2d/nethack/nh_model.*`, `nh_msgwin.*`, `nh_statwin.*`
// .
//
// Build and run (the bundled LLVM-MinGW; SDL and GL are not needed):
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I hd2d -I third_party/cpp/include ^
//     tools/hd2d_verify/test_nethack_ui_model.cpp hd2d/nethack/nh_model.cpp hd2d/nethack/nh_msgwin.cpp hd2d/nethack/nh_statwin.cpp ^
//     hd2d/dcss/dcss_cells.cpp -o test_nethack_ui_model.exe
//   test_nethack_ui_model.exe
//
// Where the expected values come from (not from the code under test):
//   - the message window rules: `win/win32/mhmsgwnd.c` of the original GUI (MSNH_MSG_PUTSTR, MSNH_MSG_CLEAR_WINDOW, `can_append_text`,
//     `begin_nohistory_line` / `end_nohistory_line`): the first line of a turn never joins the line before it; a later line with the same
//     attribute joins with two spaces when it still fits; lines added after `clear` are "this turn"; a no-history line borrows the last line and
//     gives it back;
//   - the status window: `win/win32/mhstatus.h` (`fieldorder1`, `fieldorder2`) and the default formats of `src/botl.c` (`initblstats`);
//   - the wire format: 
#include "nethack/nh_model.h"
#include "nethack/nh_msgwin.h"
#include "nethack/nh_statwin.h"

#include <iostream>
#include <string>
#include <vector>

using namespace hd2d::nethack;

namespace {

int g_checks = 0;
int g_failed = 0;

void check(bool ok, const std::string &what)
{
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::cerr << "FAILED: " << what << std::endl;
    }
}

template <typename A, typename B>
void check_eq(const A &actual, const B &expected, const std::string &what)
{
    ++g_checks;
    if (!(actual == expected)) {
        ++g_failed;
        std::cerr << "FAILED: " << what << ": expected [" << expected << "], got [" << actual << "]" << std::endl;
    }
}

std::string join_rows(const std::vector<NhMsgWindow::Row> &rows)
{
    std::string out;
    for (const NhMsgWindow::Row &r : rows) {
        out += (out.empty() ? "" : "|") + r.text;
    }
    return out;
}

std::string join_runs(const std::vector<NhStatRun> &runs)
{
    std::string out;
    for (const NhStatRun &r : runs) {
        out += (r.space_before && !out.empty() ? " " : "") + r.text;
    }
    return out;
}

void test_message_window()
{
    NhMsgWindow w;
    w.set_columns(60);
    w.put(0, "You see here a dagger.", false);
    check_eq(w.lines().size(), static_cast<std::size_t>(1), "msg: first line");
    check_eq(w.lines_last_turn(), 1, "msg: first line is this turn");
    // 同じ属性で、まだ 1 行に収まる → 半角 2 つで続ける
    w.put(0, "It is sharp.", false);
    check_eq(w.lines().size(), static_cast<std::size_t>(1), "msg: joined");
    check_eq(w.lines().back().text, "You see here a dagger.  It is sharp.", "msg: joined text");
    check_eq(w.lines_last_turn(), 1, "msg: joined line does not count twice");
    // 収まらない（36 + 2 + 20 + 8 (--More--) = 66 桁が 60 桁を超える）→ 新しい行
    w.put(0, "The goblin hits you!", false);
    check_eq(w.lines().size(), static_cast<std::size_t>(2), "msg: too long to join");
    check_eq(w.lines_last_turn(), 2, "msg: 2 lines this turn");
    // 属性が違う → 新しい行
    w.put(kAtrBold, "Bold.", false);
    check_eq(w.lines().size(), static_cast<std::size_t>(3), "msg: other attr starts a line");
    check_eq(w.lines().back().attr, kAtrBold, "msg: attr kept");
    // clear: 行は残り、今のターンの印だけ消える。新しいターンの最初の行は続けない
    w.clear();
    check_eq(w.lines_last_turn(), 0, "msg: clear resets this turn");
    check_eq(w.lines().size(), static_cast<std::size_t>(3), "msg: clear keeps lines");
    w.put(kAtrBold, "Again.", false);
    check_eq(w.lines().size(), static_cast<std::size_t>(4), "msg: first line after clear never joins");
    {
        const std::vector<NhMsgWindow::Row> rows = w.rows(3);
        check_eq(rows.size(), static_cast<std::size_t>(3), "msg: rows count");
        check(!rows[0].current && !rows[1].current && rows[2].current, "msg: only the line after clear is current");
        check_eq(rows[2].text, "Again.", "msg: last row text");
    }
    // --More-- は最終行の末尾に付く
    {
        const std::vector<NhMsgWindow::Row> rows = w.rows(2, "--More--");
        check_eq(rows.back().text, "Again.--More--", "msg: more token on the last row");
        check_eq(rows.front().text, "Bold.", "msg: row before");
    }
    // 履歴に残さない行は最終行を借りる
    w.clear();
    w.put(0, "Pick a position.", false);
    w.put(0, "a doorway", true);
    check(w.nohistory_active(), "msg: nohistory active");
    check_eq(w.rows(1).back().text, "a doorway", "msg: nohistory shown on the last row");
    w.put(0, "an open door", true);
    check_eq(w.rows(1).back().text, "an open door", "msg: nohistory replaced");
    check_eq(w.lines().size(), static_cast<std::size_t>(5), "msg: nohistory did not add lines");
    w.put(0, "Done.", false);
    check(!w.nohistory_active(), "msg: nohistory ended by a normal line");
    check_eq(join_rows(w.rows(1)), "Pick a position.  Done.", "msg: borrowed line restored and joined");
    // ATR_NOHISTORY を attr に含めても同じ
    w.clear();
    w.put(kAtrNoHistory, "tmp", false);
    check(w.nohistory_active(), "msg: ATR_NOHISTORY bit");
    w.clear();
    check(!w.nohistory_active(), "msg: clear ends nohistory");
}

void test_message_wrap()
{
    NhMsgWindow w;
    w.set_columns(10);
    w.put(0, "abcdefghij klmnopq", false);
    const std::vector<NhMsgWindow::Row> rows = w.rows(4);
    check_eq(join_rows(rows), "abcdefghij|klmnopq", "wrap: break at the space");
    check(rows[0].current && rows[1].current, "wrap: both rows are this turn");
    // 見える行数より長いときは下の行を残す
    const std::vector<NhMsgWindow::Row> one = w.rows(1);
    check_eq(one.size(), static_cast<std::size_t>(1), "wrap: one row");
    check_eq(one[0].text, "klmnopq", "wrap: bottom row kept");
    // 全角の幅は 2: 5 字で 10 桁
    NhMsgWindow j;
    j.set_columns(10);
    j.put(0, "あいうえおかきくけこ", false);
    const std::vector<NhMsgWindow::Row> jr = j.rows(4);
    check_eq(jr.size(), static_cast<std::size_t>(2), "wrap: 10 wide chars in 10 columns -> 2 rows");
    check_eq(jr[0].text + jr[1].text, "あいうえおかきくけこ", "wrap: nothing lost");
    // 空の窓
    NhMsgWindow e;
    check(e.rows(4).empty(), "wrap: empty window has no rows");
    check(e.rows(0).empty(), "wrap: zero rows");
}

void test_model_windows_and_messages()
{
    NhModel m;
    check(!m.ingest("not json"), "model: garbage is dropped");
    check(!m.ingest("{\"t\":\"frame\"}"), "model: other messages are not ours");
    check(!m.ingest("{\"t\":\"nethack_ui\"}"), "model: no kind");
    check(!m.ingest("{\"t\":\"nethack_ui\",\"k\":\"unknown_kind\"}"), "model: unknown kind is dropped");
    check(!m.ingest("{\"t\":\"nethack_ui\",\"k\":5}"), "model: kind of the wrong type");
    check_eq(m.version(), static_cast<std::uint64_t>(0), "model: nothing changed yet");

    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"create\",\"id\":1,\"type\":\"message\"}"), "model: create message");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"create\",\"id\":2,\"type\":\"status\"}"), "model: create status");
    check(!m.map_created(), "model: no map yet");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"create\",\"id\":3,\"type\":\"map\"}"), "model: create map");
    check(m.map_created(), "model: map created = in game");
    check_eq(m.window_of_type("message"), 1, "model: message window id");
    check_eq(m.window_of_type("status"), 2, "model: status window id");
    check_eq(m.window_of_type("map"), 3, "model: map window id");
    check_eq(m.window_of_type("menu"), -1, "model: no menu window");
    check(m.window(3) != nullptr && !m.window(3)->displayed, "model: map not displayed yet");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"display\",\"id\":3,\"blocking\":false}"), "model: display map");
    check(m.window(3)->displayed, "model: map displayed");

    // メッセージ
    m.msgwin().set_columns(90);
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"clear\",\"id\":1}"), "model: clear message window");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"putstr\",\"id\":1,\"attr\":0,\"text\":\"Hello Agent, welcome to NetHack!\"}"), "model: putstr");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"putstr\",\"id\":1,\"attr\":0,\"text\":\"You are a neutral male human Wizard.\"}"), "model: putstr 2");
    check_eq(m.msgwin().lines().size(), static_cast<std::size_t>(1), "model: the two lines joined (90 columns)");
    check_eq(m.msgwin().lines().back().text, "Hello Agent, welcome to NetHack!  You are a neutral male human Wizard.", "model: joined text");
    // raw_print（id = -1）もメッセージの窓へ
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"putstr\",\"id\":-1,\"attr\":1,\"text\":\"raw line\"}"), "model: raw_print");
    check_eq(m.msgwin().lines().back().text, "raw line", "model: raw_print goes to the message window");
    check_eq(m.msgwin().lines().back().attr, 1, "model: raw_print attr");
    // nohistory の欄
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"putstr\",\"id\":1,\"attr\":0,\"text\":\"auto\",\"nohistory\":true}"), "model: nohistory putstr");
    check(m.msgwin().nohistory_active(), "model: nohistory flag");
    // 知らない窓への putstr は捨てる（メッセージの窓には混ぜない）
    const std::size_t before = m.msgwin().lines().size();
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"putstr\",\"id\":99,\"attr\":0,\"text\":\"lost\"}"), "model: putstr to an unknown window is understood");
    check_eq(m.msgwin().lines().size(), before, "model: ... but not shown");
    // メニューの窓の putstr は窓の行に溜まる
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"create\",\"id\":5,\"type\":\"text\"}"), "model: create text");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"putstr\",\"id\":5,\"attr\":0,\"text\":\"line 1\"}"), "model: text putstr");
    check_eq(m.window(5)->lines.size(), static_cast<std::size_t>(1), "model: text window collects lines");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"clear\",\"id\":5}"), "model: clear text window");
    check(m.window(5)->lines.empty(), "model: cleared");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"destroy\",\"id\":5}"), "model: destroy text");
    check(m.window(5) == nullptr, "model: destroyed");
}

void test_model_wait_and_misc()
{
    NhModel m;
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"command\",\"r\":0}"), "wait: command");
    check_eq(m.wait().reason, "command", "wait: reason");
    check(!m.more_pending(), "wait: not more");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"more\",\"r\":7}"), "wait: more");
    check(m.more_pending(), "wait: more pending");
    check_eq(m.wait().r, 7, "wait: request number");
    // wait 以外の知らせが来たら待ちは終わっている
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"putstr\",\"id\":-1,\"attr\":0,\"text\":\"x\"}"), "wait: a later putstr");
    check(!m.more_pending(), "wait: a later message ends the wait");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"more\",\"r\":8}"), "wait: more again");
    m.clear_wait();
    check(m.wait().reason.empty(), "wait: cleared by the screen when it answers");

    // 要求の番号
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"menu\",\"r\":12,\"id\":4,\"prompt\":\"Pick\",\"how\":1,\"items\":[]}"), "request: menu");
    check_eq(m.last_request_kind(), "menu", "request: kind");
    check_eq(m.last_request_r(), 12, "request: number");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"ask\",\"r\":13,\"kind\":\"yn\",\"query\":\"Really?\",\"choices\":\"ynq\",\"def\":\"n\"}"), "request: ask");
    check_eq(m.last_request_kind(), "ask:yn", "request: ask kind");
    check_eq(m.last_request_r(), 13, "request: ask number");

    // カーソルと cliparound
    check(!m.cursor_known(), "curs: unknown at first");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"curs\",\"id\":3,\"x\":10,\"y\":5}"), "curs: ingest");
    check(m.cursor_known() && m.cursor_x() == 10 && m.cursor_y() == 5, "curs: values");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"cliparound\",\"x\":40,\"y\":11}"), "cliparound: ingest");
    check(m.clip_known() && m.clip_x() == 40 && m.clip_y() == 11, "cliparound: values");

    // 設定
    check(!m.pref_bool("perm_invent"), "pref: default off (D15)");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"pref\",\"name\":\"perm_invent\",\"value\":true}"), "pref: bool true");
    check(m.pref_bool("perm_invent"), "pref: perm_invent on");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"pref\",\"name\":\"perm_invent\",\"value\":0}"), "pref: number 0");
    check(!m.pref_bool("perm_invent"), "pref: perm_invent off again");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"pref\",\"name\":\"font\",\"value\":\"Arial\"}"), "pref: string");
    check_eq(m.pref_string("font"), "Arial", "pref: string value");

    // 終了
    check(!m.exited(), "exit: not yet");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"bell\"}"), "bell");
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"exit\",\"text\":\"Be seeing you...\"}"), "exit");
    check(m.exited(), "exit: flag");
    check_eq(m.exit_text(), "Be seeing you...", "exit: text");
}

void test_status()
{
    NhModel m;
    // 値だけが届いたとき（原作の既定の書式を当てる）
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"status\",\"fields\":["
                   "{\"f\":0,\"text\":\"Agent the Evoker\"},"
                   "{\"f\":1,\"text\":\"10\"},{\"f\":2,\"text\":\"12\"},{\"f\":3,\"text\":\"14\"},"
                   "{\"f\":4,\"text\":\"18\"},{\"f\":5,\"text\":\"16\"},{\"f\":6,\"text\":\"9\"},"
                   "{\"f\":7,\"text\":\"Neutral\"},"
                   "{\"f\":20,\"text\":\"Dlvl:1\"},{\"f\":10,\"text\":\"$:0\"},"
                   "{\"f\":18,\"text\":\"12\",\"color\":10,\"pct\":80},{\"f\":19,\"text\":\"15\"},"
                   "{\"f\":11,\"text\":\"7\"},{\"f\":12,\"text\":\"7\"},"
                   "{\"f\":14,\"text\":\"9\"},{\"f\":13,\"text\":\"1\"},{\"f\":21,\"text\":\"0\"},"
                   "{\"f\":16,\"text\":\"1\"},{\"f\":17,\"text\":\"Hungry\",\"color\":11,\"attr\":2}],"
                   "\"cond\":[{\"name\":\"Stun\",\"color\":1,\"attr\":64},{\"name\":\"Conf\",\"color\":3}],\"flush\":true}"),
          "status: ingest");
    check(m.status_seen(), "status: seen");
    NhStatusView v = nh_build_status(m);
    check_eq(join_runs(v.rows[0]), "Agent the Evoker St:10 Dx:12 Co:14 In:18 Wi:16 Ch:9 Neutral", "status: row 1 order and default labels");
    check_eq(join_runs(v.rows[1]), "Dlvl:1 $:0 HP:12(15) Pw:7(7) AC:9 Xp:1/0 T:1 Hungry Stun Conf", "status: row 2 order, glued (...) and /..., conditions last");
    check_eq(v.bar_pct, 80, "status: bar from pct");
    check_eq(v.bar_color, 10, "status: bar color from HP color");
    // 項目の色と属性
    const std::vector<NhStatRun> &r2 = v.rows[1];
    bool hungry_ok = false;
    bool stun_ok = false;
    for (const NhStatRun &r : r2) {
        if (r.text == "Hungry") hungry_ok = (r.color == 11) && (r.attr == kHlBold);
        if (r.text == "Stun") stun_ok = (r.color == 1) && (r.attr == kHlInverse) && (r.field == kBlCondition);
    }
    check(hungry_ok, "status: field color and attr");
    check(stun_ok, "status: condition color and attr");
    // glue: (15) は HP の直後、space_before が偽
    {
        bool glued = false;
        for (std::size_t i = 1; i < r2.size(); ++i) {
            if (r2[i].text == "(15)") glued = !r2[i].space_before && (r2[i - 1].text == "HP:12");
        }
        check(glued, "status: (max) glued to HP");
    }
    // 次の更新: HP が下がった（割合が無いときは HP ÷ 最大）。状態異常が消える
    check(m.ingest("{\"t\":\"nethack_ui\",\"k\":\"status\",\"fields\":[{\"f\":18,\"text\":\"3\"}],\"cond\":[]}"), "status: update");
    v = nh_build_status(m);
    check_eq(join_runs(v.rows[1]), "Dlvl:1 $:0 HP:3(15) Pw:7(7) AC:9 Xp:1/0 T:1 Hungry", "status: HP updated, conditions cleared");
    check_eq(v.bar_pct, 20, "status: bar computed from HP / max");
    check_eq(v.bar_color, 2, "status: no color -> green bar");
    // コアが整形済みの字を送るときはそのまま
    NhModel f;
    check(f.ingest("{\"t\":\"nethack_ui\",\"k\":\"status\",\"fields\":[{\"f\":1,\"text\":\"St:18/01\"},{\"f\":18,\"text\":\"HP:5\"},{\"f\":19,\"text\":\"(30)\"}]}"), "status: formatted ingest");
    v = nh_build_status(f);
    check_eq(join_runs(v.rows[0]), "St:18/01", "status: formatted text kept");
    check_eq(join_runs(v.rows[1]), "HP:5(30)", "status: formatted HP kept and glued");
    check_eq(v.bar_pct, 16, "status: bar from formatted values");
    // 書式の欄 fmt
    NhModel g;
    check(g.ingest("{\"t\":\"nethack_ui\",\"k\":\"status\",\"fields\":[{\"f\":1,\"text\":\"18\",\"fmt\":\" 筋力:%s\"}]}"), "status: fmt ingest");
    check_eq(join_runs(nh_build_status(g).rows[0]), "筋力:18", "status: fmt applied, leading space dropped");
    // reset
    check(g.ingest("{\"t\":\"nethack_ui\",\"k\":\"status\",\"reset\":true,\"fields\":[{\"f\":0,\"text\":\"X\"}]}"), "status: reset");
    check_eq(join_runs(nh_build_status(g).rows[0]), "X", "status: reset clears the old fields");
    // 範囲外の項目・色
    check(g.ingest("{\"t\":\"nethack_ui\",\"k\":\"status\",\"fields\":[{\"f\":99,\"text\":\"Z\"},{\"f\":0,\"text\":\"Y\",\"color\":-1}]}"), "status: out of range field");
    const NhStatusView bad = nh_build_status(g);
    check_eq(join_runs(bad.rows[0]), "Y", "status: out of range field dropped");
    check_eq(bad.rows[0][0].color, kNoColor, "status: negative color means none");
    // 空の状態
    check(nh_build_status(NhModel{}).rows[0].empty() && nh_build_status(NhModel{}).rows[1].empty(), "status: empty model");
    check_eq(nh_build_status(NhModel{}).bar_pct, 0, "status: no bar without HP");
}

} // namespace

void test_gold_display()
{
    // 所持金は、金貨の glyph の符号（`\\GXXXXNNNN`）に `:` と金額が続いて届く。原作の GUI 版（`mswproc.c` の `BL_GOLD`）は `$:金額` と出す。
    NhStatusField gold;
    gold.present = true;
    gold.text = "\\G1A910F2E:25";
    check_eq(nh_status_display_text(kBlGold, gold), std::string("$:25"), "gold: 符号を取り除いて `$:金額`");
    NhStatusField plain;
    plain.present = true;
    plain.text = "7";
    check_eq(nh_status_display_text(kBlGold, plain), std::string("$:7"), "gold: 金額だけが届いたときは既定の書式");
    NhStatusField fmt;
    fmt.present = true;
    fmt.text = "9";
    fmt.fmt = "Au:%s";
    check_eq(nh_status_display_text(kBlGold, fmt), std::string("Au:9"), "gold: 書式が届けばそれに当てる");
}

int main()
{
    test_message_window();
    test_message_wrap();
    test_model_windows_and_messages();
    test_model_wait_and_misc();
    test_status();
    test_gold_display();
    std::cout << "test_nethack_ui_model: " << g_checks << " checks, " << g_failed << " failed" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
