// NetHack の専用の画面の、地図の上のマウスの判断の単体検査（の N4）。
// SDL も GL も使わない `hd2d/nethack/nh_mouse.*` と、`nh_model.*` の待ち・位置の選択・説明の取り込みを見る。
//
// 組み立てと実行（同梱の LLVM-MinGW。SDL と GL は要らない）:
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I hd2d -I third_party/cpp/include ^
//     tools/hd2d_verify/test_nethack_mouse.cpp hd2d/nethack/nh_mouse.cpp hd2d/nethack/nh_model.cpp hd2d/nethack/nh_msgwin.cpp ^
//     hd2d/dcss/dcss_cells.cpp -o test_nethack_mouse.exe
//   test_nethack_mouse.exe
//
// 期待値の出どころ（試す対象のコードではなく原作とプロトコルの文書）:
//   - クリックの返事: 原作の GUI 版 `win/win32/mswproc.c` の `mswin_nh_poskey`（x・y はマス番号、`mod` は CLICK_1 = 1・CLICK_2 = 2）と
//     `win/win32/mhmap.c`（左ボタン = CLICK_1、右ボタンとダブルクリック = CLICK_2）。
//   - マスの範囲: 原作の `isok(x, y)`（x は 1〜COLNO-1 = 79、y は 0〜ROWNO-1 = 20）。
//   - 返事・問い合わせの形:。
#include "nethack/nh_model.h"
#include "nethack/nh_mouse.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace hd2d::nethack;
using nlohmann::json;

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

NhMapMouseContext ctx_command()
{
    NhMapMouseContext c;
    c.map_click_wait = true;
    return c;
}

void test_cell_range()
{
    check(nh_map_cell_ok(1, 0), "isok: 左上の角");
    check(nh_map_cell_ok(79, 20), "isok: 右下の角");
    check(!nh_map_cell_ok(0, 5), "isok: x = 0 は原作が使わない列");
    check(!nh_map_cell_ok(80, 5), "isok: x = 80 は外");
    check(!nh_map_cell_ok(10, -1), "isok: y = -1 は外");
    check(!nh_map_cell_ok(10, 21), "isok: y = 21 は外");
}

void test_json_shapes()
{
    const json reply = json::parse(nh_pos_reply_json(12, 7, 2));
    check_eq(reply.value("t", std::string()), std::string("nethack_reply"), "返事: t");
    check_eq(reply.value("a", std::string()), std::string("pos"), "返事: a");
    check_eq(reply.value("x", -1), 12, "返事: x");
    check_eq(reply.value("y", -1), 7, "返事: y");
    check_eq(reply.value("mod", -1), 2, "返事: mod");
    check_eq(reply.value("r", -1), 0, "返事: r は 0（番号なし）");
    const json query = json::parse(nh_hover_query_json(3, 9));
    check_eq(query.value("t", std::string()), std::string("nethack_query"), "問い合わせ: t");
    check_eq(query.value("q", std::string()), std::string("hover"), "問い合わせ: q");
    check_eq(query.value("x", -1), 3, "問い合わせ: x");
    check_eq(query.value("y", -1), 9, "問い合わせ: y");
}

void test_press()
{
    NhMapMouse m;
    // 命令の待ちの左クリック → CLICK_1
    NhPressResult r = m.on_press(ctx_command(), true, 40, 10, 1, 1);
    check(r.consumed, "押下: 地図の上は食う");
    check_eq(json::parse(r.reply).value("mod", 0), 1, "押下: 左 = CLICK_1");
    check_eq(json::parse(r.reply).value("x", 0), 40, "押下: x");
    check_eq(json::parse(r.reply).value("y", 0), 10, "押下: y");
    // 右クリック → CLICK_2
    r = m.on_press(ctx_command(), true, 5, 3, 2, 1);
    check_eq(json::parse(r.reply).value("mod", 0), 2, "押下: 右 = CLICK_2");
    // 連続の 2 回目の左クリック → CLICK_2（原作の WM_LBUTTONDBLCLK）
    r = m.on_press(ctx_command(), true, 5, 3, 1, 2);
    check_eq(json::parse(r.reply).value("mod", 0), 2, "押下: ダブルクリック = CLICK_2");
    // 地図の外は食わない・返事も無い
    r = m.on_press(ctx_command(), false, 5, 3, 1, 1);
    check(!r.consumed && r.reply.empty(), "押下: 地図の外は何もしない");
    // 中ボタンなどは食わない
    r = m.on_press(ctx_command(), true, 5, 3, 0, 1);
    check(!r.consumed && r.reply.empty(), "押下: 左右以外のボタンは食わない");
    // 範囲外のマス: 食うが返事は無い
    r = m.on_press(ctx_command(), true, 0, 3, 1, 1);
    check(r.consumed && r.reply.empty(), "押下: 範囲外のマスは食って返さない");
    r = m.on_press(ctx_command(), true, 40, 21, 1, 1);
    check(r.consumed && r.reply.empty(), "押下: y が範囲外");
    // 待っていない場面（コアが歩いている途中・問い・窓）: 食うが返事は無い
    NhMapMouseContext idle;
    r = m.on_press(idle, true, 40, 10, 1, 1);
    check(r.consumed && r.reply.empty(), "押下: コアが待っていないときは返さない");
    NhMapMouseContext window = ctx_command();
    window.windows_active = true;
    r = m.on_press(window, true, 40, 10, 1, 1);
    check(r.consumed && r.reply.empty(), "押下: 窓が開いているあいだは返さない");
    NhMapMouseContext more = ctx_command();
    more.more_pending = true;
    r = m.on_press(more, true, 40, 10, 1, 1);
    check(r.consumed && r.reply.empty(), "押下: --More-- のあいだは返さない");
}

void test_hover()
{
    NhMapMouse m;
    const NhMapMouseContext ctx = ctx_command();
    // 地図のマスの上に来たら、そのマスを 1 度だけ聞く
    std::string q = m.on_motion(ctx, true, 10, 5, 300, 200, 1);
    check_eq(json::parse(q).value("x", -1), 10, "ホバー: x を聞く");
    check_eq(json::parse(q).value("y", -1), 5, "ホバー: y を聞く");
    q = m.on_motion(ctx, true, 10, 5, 305, 203, 1);
    check(q.empty(), "ホバー: 同じマスの中の動きでは聞き直さない");
    std::string text;
    int px = 0;
    int py = 0;
    check(!m.tooltip(ctx, text, px, py), "ホバー: 答えが来るまでは出さない");
    m.on_answer(10, 5, "doorway");
    check(m.tooltip(ctx, text, px, py), "ホバー: 答えが来たら出す");
    check_eq(text, std::string("doorway"), "ホバー: 字");
    check_eq(px, 305, "ホバー: 出す位置 x はマウス");
    check_eq(py, 203, "ホバー: 出す位置 y はマウス");
    // 別のマスの答えは捨てる
    m.on_answer(11, 5, "wall");
    check(m.tooltip(ctx, text, px, py) && text == "doorway", "ホバー: 別のマスの答えは取らない");
    // 別のマスへ動いたら聞き直し、古い答えは出さない
    q = m.on_motion(ctx, true, 11, 5, 330, 205, 1);
    check_eq(json::parse(q).value("x", -1), 11, "ホバー: 別のマスは聞き直す");
    check(!m.tooltip(ctx, text, px, py), "ホバー: 別のマスの古い答えは出さない");
    m.on_answer(11, 5, "wall");
    check(m.tooltip(ctx, text, px, py) && text == "wall", "ホバー: 新しい答え");
    // 同じマスでも、コアの待ちが新しくなったら（歩いた・敵が動いた）聞き直す。答えが来るまで古い字は残す（ちらつかせない）
    q = m.on_tick(ctx, 1);
    check(q.empty(), "ホバー: 待ちが同じなら聞き直さない");
    q = m.on_tick(ctx, 2);
    check_eq(json::parse(q).value("x", -1), 11, "ホバー: 待ちが新しくなったら同じマスを聞き直す");
    check(m.tooltip(ctx, text, px, py), "ホバー: 聞き直しの間も出し続ける");
    q = m.on_tick(ctx, 2);
    check(q.empty(), "ホバー: 同じ待ちで二重に聞かない");
    // コアが待っていない間は聞かず、出さない
    NhMapMouseContext idle;
    q = m.on_tick(idle, 3);
    check(q.empty(), "ホバー: コアが待っていないときは聞かない");
    check(!m.tooltip(idle, text, px, py), "ホバー: コアが待っていないときは出さない");
    // 待ちが戻ったら（serial が変わっている）聞き直す
    q = m.on_tick(ctx, 3);
    check(!q.empty(), "ホバー: 待ちが戻ったら聞き直す");
    // 地図の外へ出たら消える
    q = m.on_motion(ctx, false, 0, 0, 5, 5, 3);
    check(q.empty() && !m.tooltip(ctx, text, px, py), "ホバー: 地図の外では出さない");
    int hx = 0;
    int hy = 0;
    check(!m.hovered_cell(hx, hy), "ホバー: 地図の外ではマスなし");
    // 範囲外のマスは聞かない
    q = m.on_motion(ctx, true, 0, 5, 5, 5, 3);
    check(q.empty(), "ホバー: 範囲外のマスは聞かない");
    // 窓が開いているあいだは聞かない
    NhMapMouseContext window = ctx;
    window.windows_active = true;
    q = m.on_motion(window, true, 20, 5, 5, 5, 3);
    check(q.empty(), "ホバー: 窓が開いているあいだは聞かない");
    // クリックで返したあとは、次の待ちで聞き直す
    q = m.on_motion(ctx, true, 20, 5, 5, 5, 4);
    check(!q.empty(), "ホバー: 窓が閉じたら聞く");
    m.on_answer(20, 5, "floor of a room");
    NhPressResult r = m.on_press(ctx, true, 20, 5, 1, 1);
    check(!r.reply.empty(), "クリック: 返事");
    check(!m.tooltip(ctx, text, px, py), "クリック: 返したあとは説明を消す");
}

void test_model_wait_pos_hover()
{
    NhModel model;
    check(!model.map_click_wait() && !model.pos_active(), "model: 最初は待っていない");
    check_eq(model.wait_serial(), static_cast<std::uint64_t>(0), "model: wait_serial の初期値");
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"create\",\"id\":3,\"type\":\"map\"}");
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"create\",\"id\":1,\"type\":\"message\"}");
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"command\"}");
    check(model.map_click_wait() && !model.pos_wait() && !model.pos_active(), "model: command の待ちはクリックを受ける");
    check_eq(model.wait_serial(), static_cast<std::uint64_t>(1), "model: wait_serial が増える");
    // 説明の答えは待ちを終わらせない
    check(model.ingest("{\"t\":\"nethack_ui\",\"k\":\"hover\",\"x\":10,\"y\":5,\"text\":\"doorway\"}"), "model: hover を取り込む");
    check(model.map_click_wait(), "model: hover は待ちを終わらせない");
    check_eq(model.hover_text(), std::string("doorway"), "model: hover の字");
    check_eq(model.hover_x(), 10, "model: hover の x");
    check_eq(model.hover_y(), 5, "model: hover の y");
    check_eq(model.hover_serial(), static_cast<std::uint64_t>(1), "model: hover_serial");
    // 位置の選択
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"pos\"}");
    check(model.map_click_wait() && model.pos_wait() && model.pos_active(), "model: pos の待ち");
    check_eq(model.wait_serial(), static_cast<std::uint64_t>(2), "model: wait_serial 2");
    // 地図のカーソルだけを覚える（メッセージの窓の curs は無視）
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"curs\",\"id\":3,\"x\":20,\"y\":8}");
    check(model.cursor_known() && model.cursor_x() == 20 && model.cursor_y() == 8, "model: 地図の curs を覚える");
    // curs・putstr で wait は消えるが、位置の選択の途中の印は残る（カーソルの印を消さない）
    check(!model.map_click_wait(), "model: curs で待ちの表示は消える");
    check(model.pos_active(), "model: 位置の選択の印は残る");
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"curs\",\"id\":1,\"x\":2,\"y\":3}");
    check(model.cursor_x() == 20 && model.cursor_y() == 8, "model: メッセージの窓の curs は地図のカーソルにしない");
    // 選んだあと
    model.clear_pos_active();
    check(!model.pos_active(), "model: クリックで選んだら位置の選択は終わる");
    // command の待ちに戻れば下りる
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"pos\"}");
    check(model.pos_active(), "model: 再び位置の選択");
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"command\"}");
    check(!model.pos_active(), "model: 命令の待ちに戻れば位置の選択は終わる");
    // 窓が開けば下りる
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"pos\"}");
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"menu\",\"r\":5,\"id\":4,\"how\":1,\"items\":[]}");
    check(!model.pos_active(), "model: メニューが開けば位置の選択は終わる");
    // 持ち物の常設の窓の更新（r = 0）は関係しない
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"wait\",\"reason\":\"pos\"}");
    (void)model.ingest("{\"t\":\"nethack_ui\",\"k\":\"menu\",\"r\":0,\"id\":6,\"how\":0,\"items\":[]}");
    check(model.pos_active(), "model: 常設の持ち物の窓の更新では下りない");
    // 地図の窓がまだ無い間の curs は区別できないので覚える
    NhModel early;
    (void)early.ingest("{\"t\":\"nethack_ui\",\"k\":\"curs\",\"id\":9,\"x\":4,\"y\":4}");
    check(early.cursor_known() && early.cursor_x() == 4, "model: 窓が不明なら curs を覚える");
}

void test_hints()
{
    check_eq(nh_map_key_hint(true, "").empty(), true, "帯: 待っていなければ空");
    check_eq(nh_map_key_hint(true, "key").empty(), true, "帯: 1 キーの待ちは空");
    check(nh_map_key_hint(true, "command").find("左クリック") != std::string::npos, "帯: 命令の待ち（日本語）はクリックを案内する");
    check(nh_map_key_hint(false, "command").find("Left click") != std::string::npos, "帯: 命令の待ち（英語）");
    check(nh_map_key_hint(true, "pos").find("クリック") != std::string::npos, "帯: 位置の選択（日本語）");
    check(nh_map_key_hint(false, "pos").find("Click") != std::string::npos, "帯: 位置の選択（英語）");
    // HD2D の共通のパッドの割り当ての文言は入れない
    for (const char *reason : { "command", "pos" }) {
        for (const bool ja : { true, false }) {
            const std::string hint = nh_map_key_hint(ja, reason);
            check(hint.find("Start") == std::string::npos && hint.find("コマンドメニュー") == std::string::npos
                    && hint.find("一人称") == std::string::npos && hint.find("Command menu") == std::string::npos,
                std::string("帯: パッドの割り当ての文言を含まない ") + reason);
            check(hint.size() >= 2 && hint.substr(hint.size() - 2) == "  ", std::string("帯: 末尾に空白 2 つ ") + reason);
        }
    }
}

} // namespace

int main()
{
    test_cell_range();
    test_json_shapes();
    test_press();
    test_hover();
    test_model_wait_pos_hover();
    test_hints();
    std::cout << "test_nethack_mouse: " << g_checks << " checks, " << g_failed << " failed" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
