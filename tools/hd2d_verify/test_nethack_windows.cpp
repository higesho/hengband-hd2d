// NetHack の専用の画面の窓の部品のうち、GL を使わない部分の単体検査（の N2・N3）:
//   hd2d/nethack/nh_menu.*（メニューの状態と操作）・nh_select.*（キャラクタ作成の選び）・nh_input.*（yn・文字入力・拡張コマンド）・
//   nh_tiles.*（タイルの一覧）と、nh_model.* の要求・持ち物の常設の窓の取り込み。
//
// 組み立てと実行（同梱の LLVM-MinGW。SDL と GL は要らない）:
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I hd2d -I third_party/cpp/include ^
//     tools/hd2d_verify/test_nethack_windows.cpp hd2d/nethack/nh_menu.cpp hd2d/nethack/nh_select.cpp hd2d/nethack/nh_input.cpp ^
//     hd2d/nethack/nh_tiles.cpp hd2d/nethack/nh_model.cpp hd2d/nethack/nh_msgwin.cpp hd2d/dcss/dcss_cells.cpp -o test_nethack_windows.exe
//   test_nethack_windows.exe
//
// 期待値の出どころ（試す対象のコードではなく原作）:
//   - メニューの操作: `win/win32/mhmenu.c` の `onListChar`（字で選ぶ・グループの字・個数・ページ送り・全選択・検索・スペース）と `SelectMenuItem`。
//   - キャラクタ作成: `win/win32/mhdlg.c` の `plselAdjustSelections`（職業を替えると、種族・性別・属性を合う物へ直す）。
//   - yn: `win/win32/mswproc.c` の `mswin_yn_function`（Esc は q・n・既定の順、スペースと Enter は既定、選べない字は捨てる）。
//   - 通信の形: `CORE_PROTOCOL_V1.md` §8.7。
#include "nethack/nh_input.h"
#include "nethack/nh_menu.h"
#include "nethack/nh_model.h"
#include "nethack/nh_select.h"
#include "nethack/nh_tiles.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>

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

json item(int i, const std::string &ch, const std::string &text, int clr = 8, const std::string &gch = std::string())
{
    json j = json::object();
    j["i"] = i;
    if (!ch.empty()) {
        j["ch"] = ch;
    }
    if (!gch.empty()) {
        j["gch"] = gch;
    }
    j["clr"] = clr;
    j["glyph"] = -1;
    j["text"] = text;
    return j;
}

json heading(const std::string &text)
{
    json j = json::object();
    j["i"] = 0;
    j["attr"] = 7;
    j["clr"] = -1;
    j["text"] = text;
    return j;
}

std::string menu_payload(int r, int how, const std::string &prompt, const std::vector<json> &items)
{
    json m = json::object();
    m["t"] = "nethack_ui";
    m["k"] = "menu";
    m["r"] = r;
    m["id"] = 4;
    m["prompt"] = prompt;
    m["how"] = how;
    m["items"] = json::array();
    for (const json &e : items) {
        m["items"].push_back(e);
    }
    return m.dump();
}

//! 見出し 2 つと、選べる項目 5 つ（a〜e）。
NhMenu sample_menu(int how, int page_rows = 4)
{
    NhMenu menu;
    const bool ok = menu.load(menu_payload(7, how, "何を選びますか?",
        { heading("武器"), item(1, "a", "+0 戦斧"), item(2, "b", "+0 小剣"), heading("防具"), item(3, "c", "鉄環の鎧"), item(4, "d", "食料", 8, "$"),
            item(5, "e", "食料の缶詰", 8, "$") }));
    check(ok, "menu load");
    menu.set_page_rows(page_rows);
    return menu;
}

json reply_of(const std::string &s)
{
    return json::parse(s);
}

void test_menu_basic()
{
    NhMenu menu = sample_menu(2);
    check_eq(menu.size(), 7, "menu: 行の数（見出しを含む）");
    check_eq(menu.how(), 2, "menu: how");
    check_eq(menu.request(), 7, "menu: 要求の番号");
    check_eq(menu.prompt(), std::string("何を選びますか?"), "menu: 見出しの字");
    check(!menu.items()[0].selectable(), "menu: 見出しは選べない");
    check(menu.items()[1].selectable(), "menu: 項目は選べる");
    check_eq(menu.cursor(), 1, "menu: 焦点は最初の選べる行");

    check(menu.on_char('a') == NhMenuResult::None, "menu(ANY): 字は反転するだけで決定しない");
    check_eq(menu.count_of(1), -1, "menu(ANY): a が選ばれた（全部）");
    check(menu.on_char('a') == NhMenuResult::None, "menu(ANY): もう一度で外れる");
    check_eq(menu.count_of(1), 0, "menu(ANY): a が外れた");
    (void)menu.on_char('b');
    (void)menu.on_char('d');
    check(menu.on_key(NhMenuKey::Enter) == NhMenuResult::Submit, "menu(ANY): Enter で決定");
    const json r = reply_of(menu.submit_json());
    check_eq(r["t"].get<std::string>(), std::string("nethack_reply"), "menu: 返事の t");
    check_eq(r["r"].get<int>(), 7, "menu: 返事の r");
    check_eq(r["a"].get<std::string>(), std::string("menu"), "menu: 返事の a");
    check_eq(static_cast<int>(r["sel"].size()), 2, "menu: 選んだ数");
    check_eq(r["sel"][0]["i"].get<int>(), 2, "menu: 1 つ目は b（i=2）");
    check_eq(r["sel"][0]["n"].get<int>(), -1, "menu: 個数の指定なしは -1");
    check_eq(r["sel"][1]["i"].get<int>(), 4, "menu: 2 つ目は d（i=4）");

    NhMenu cancelled = sample_menu(2);
    check(cancelled.on_key(NhMenuKey::Escape) == NhMenuResult::Cancel, "menu: Esc で取り消し");
    check_eq(reply_of(cancelled.cancel_json())["a"].get<std::string>(), std::string("cancel"), "menu: 取り消しの返事");

    NhMenu none = sample_menu(2);
    check(none.on_key(NhMenuKey::Enter) == NhMenuResult::Submit, "menu: 何も選ばず Enter");
    check_eq(static_cast<int>(reply_of(none.submit_json())["sel"].size()), 0, "menu: 何も選ばなければ sel は空");
}

void test_menu_pick_one_and_none()
{
    NhMenu one = sample_menu(1);
    check(one.on_char('c') == NhMenuResult::Submit, "menu(ONE): 字を押すとその場で決定");
    json r = reply_of(one.submit_json());
    check_eq(static_cast<int>(r["sel"].size()), 1, "menu(ONE): 1 つだけ");
    check_eq(r["sel"][0]["i"].get<int>(), 3, "menu(ONE): c は i=3");

    NhMenu moved = sample_menu(1);
    check(moved.on_key(NhMenuKey::Down) == NhMenuResult::None, "menu(ONE): 矢印は決定しない");
    check_eq(moved.cursor(), 2, "menu(ONE): 焦点が次の項目へ");
    check_eq(moved.count_of(2), -1, "menu(ONE): 焦点の項目が選ばれる");
    check_eq(moved.count_of(1), 0, "menu(ONE): 前の項目の選びは外れる");
    check(moved.on_key(NhMenuKey::Down) == NhMenuResult::None, "menu(ONE): 見出しを飛ばして進む");
    check_eq(moved.cursor(), 4, "menu(ONE): 見出しの行（3）は飛ばして 4 へ");
    check(moved.on_key(NhMenuKey::Enter) == NhMenuResult::Submit, "menu(ONE): Enter で決定");
    check_eq(reply_of(moved.submit_json())["sel"][0]["i"].get<int>(), 3, "menu(ONE): 焦点の項目（c）で決定");

    NhMenu enter_first = sample_menu(1);
    check(enter_first.on_key(NhMenuKey::Enter) == NhMenuResult::Submit, "menu(ONE): 動かさずに Enter");
    check_eq(reply_of(enter_first.submit_json())["sel"][0]["i"].get<int>(), 1, "menu(ONE): 最初の項目で決定");

    NhMenu view = sample_menu(0);
    check(view.on_char('a') == NhMenuResult::None, "menu(NONE): 字は何も選ばない");
    check_eq(view.selected_total(), 0, "menu(NONE): 選べない");
    check(view.on_key(NhMenuKey::Enter) == NhMenuResult::Submit, "menu(NONE): Enter で閉じる");
    check_eq(reply_of(view.submit_json())["a"].get<std::string>(), std::string("dismiss"), "menu(NONE): 閉じたら dismiss");
    check_eq(reply_of(view.cancel_json())["a"].get<std::string>(), std::string("dismiss"), "menu(NONE): Esc も dismiss");
}

void test_menu_group_count_paging()
{
    NhMenu menu = sample_menu(2);
    check(menu.on_char('$') == NhMenuResult::None, "menu: グループの字");
    check_eq(menu.count_of(5), -1, "menu: $ で d が選ばれる（行の位置は 5）");
    check_eq(menu.count_of(6), -1, "menu: $ で e が選ばれる（行の位置は 6）");
    check_eq(menu.count_of(1), 0, "menu: 別のグループは選ばれない");
    (void)menu.on_char('$');
    check_eq(menu.selected_total(), 0, "menu: もう一度で外れる");

    NhMenu one = sample_menu(1);
    check(one.on_char('$') == NhMenuResult::Submit, "menu(ONE): グループの字は先頭の項目で決定");
    check_eq(reply_of(one.submit_json())["sel"][0]["i"].get<int>(), 4, "menu(ONE): グループの先頭は d");

    // 個数（焦点の項目に数字で指定。先頭の 0 は無視。Esc は個数を消すだけ）
    NhMenu count = sample_menu(2);
    (void)count.on_char('0');
    check(!count.counting(), "menu: 先頭の 0 は無視");
    (void)count.on_char('1');
    (void)count.on_char('2');
    check(count.counting(), "menu: 個数の入力中");
    check_eq(count.count_of(1), 12, "menu: 焦点の項目に 12 個");
    check(count.on_key(NhMenuKey::Enter) == NhMenuResult::Submit, "menu: 個数つきで決定");
    check_eq(reply_of(count.submit_json())["sel"][0]["n"].get<int>(), 12, "menu: 返事の個数");
    NhMenu esc = sample_menu(2);
    (void)esc.on_char('3');
    check(esc.on_key(NhMenuKey::Escape) == NhMenuResult::None, "menu: 個数の入力中の Esc は消すだけ");
    check_eq(esc.count_of(1), 0, "menu: 個数が消えた");
    check(esc.on_key(NhMenuKey::Escape) == NhMenuResult::Cancel, "menu: 次の Esc で取り消し");

    // 全選択・全解除・反転（ANY だけ）
    NhMenu all = sample_menu(2);
    (void)all.on_char('.');
    check_eq(all.selected_total(), 5, "menu: . で全選択");
    (void)all.on_char('-');
    check_eq(all.selected_total(), 0, "menu: - で全解除");
    (void)all.on_char('a');
    (void)all.on_char('@');
    check_eq(all.selected_total(), 4, "menu: @ で反転（a は外れ、ほかが選ばれる）");
    NhMenu all_one = sample_menu(1);
    (void)all_one.on_char('.');
    check_eq(all_one.selected_total(), 0, "menu(ONE): . は効かない");

    // ページ送り: 見える行 4 行・全 7 行
    NhMenu page = sample_menu(2, 4);
    check_eq(page.top(), 0, "menu: 先頭");
    check(!page.bottom_visible(), "menu: 最後の行はまだ見えない");
    check(page.on_key(NhMenuKey::Space) == NhMenuResult::None, "menu: スペースは次のページ");
    check_eq(page.top(), 3, "menu: 次のページ（最後の 4 行）");
    check(page.bottom_visible(), "menu: 最後のページ");
    check(page.on_key(NhMenuKey::Space) == NhMenuResult::Submit, "menu: 最後のページのスペースは決定（NetHack モード）");
    NhMenu keys = sample_menu(2, 3);
    (void)keys.on_char('>');
    check_eq(keys.top(), 3, "menu: > で次のページ");
    (void)keys.on_char('<');
    check_eq(keys.top(), 0, "menu: < で前のページ");
    (void)keys.on_char('|');
    check_eq(keys.cursor(), 6, "menu: | で最後の項目へ（行の位置は 6）");
    (void)keys.on_char('^');
    check_eq(keys.cursor(), 1, "menu: ^ で最初の項目へ");
}

void test_menu_search_and_click()
{
    NhMenu menu = sample_menu(2);
    (void)menu.on_char(':');
    check(menu.searching(), "menu: : で検索の入力中");
    menu.on_search_text("食料");
    check_eq(menu.search_text(), std::string("食料"), "menu: 検索文字列");
    check(menu.on_key(NhMenuKey::Enter) == NhMenuResult::None, "menu: 検索を実行");
    check(!menu.searching(), "menu: 検索は終わる");
    check_eq(menu.count_of(5), -1, "menu: 食料を含む d が選ばれる");
    check_eq(menu.count_of(6), -1, "menu: 食料を含む e も選ばれる");
    check_eq(menu.count_of(1), 0, "menu: 含まない項目は選ばれない");
    (void)menu.on_char(':');
    check(menu.on_key(NhMenuKey::Escape) == NhMenuResult::None, "menu: 検索の Esc は検索だけやめる");
    check(!menu.searching(), "menu: 検索をやめた");

    NhMenu click = sample_menu(2);
    check(click.click_row(2, false) == NhMenuResult::None, "menu(ANY): クリックは反転");
    check_eq(click.count_of(2), -1, "menu(ANY): クリックで選ばれる");
    check(click.click_row(3, false) == NhMenuResult::None, "menu: 見出しはクリックしても何も起きない");
    NhMenu one = sample_menu(1);
    check(one.click_row(2, false) == NhMenuResult::None, "menu(ONE): クリックは選ぶだけ");
    check(one.click_row(2, true) == NhMenuResult::Submit, "menu(ONE): ダブルクリックで決定");

    // 選ばれた状態で始まる項目
    json pre = item(1, "a", "最初から選ばれている");
    pre["sel"] = true;
    NhMenu preselected;
    check(preselected.load(menu_payload(1, 2, "", { pre, item(2, "b", "選ばれていない") })), "menu: sel つきの読み込み");
    check_eq(preselected.count_of(0), -1, "menu: sel の項目は選ばれて始まる");

    NhMenu bad;
    check(!bad.load("{\"k\":\"text\"}"), "menu: menu でない知らせは読まない");
    check(!bad.load("not json"), "menu: 壊れた JSON は読まない");
}

json role_payload(const json &pick)
{
    json m = json::object();
    m["t"] = "nethack_ui";
    m["k"] = "select";
    m["r"] = 3;
    m["roles"] = json::array({ { { "i", 0 }, { "name", "考古学者" } }, { { "i", 1 }, { "name", "野蛮人" } }, { { "i", 2 }, { "name", "騎士" } },
        { { "i", 3 }, { "name", "ワルキューレ" }, { "name_f", "ワルキューレ女" } } });
    m["races"] = json::array({ { { "i", 0 }, { "name", "人間" } }, { { "i", 1 }, { "name", "エルフ" } }, { { "i", 2 }, { "name", "ドワーフ" } },
        { { "i", 3 }, { "name", "オーク" } } });
    m["genders"] = json::array({ { { "i", 0 }, { "name", "男性" } }, { { "i", 1 }, { "name", "女性" } } });
    m["aligns"] = json::array({ { { "i", 0 }, { "name", "秩序" } }, { { "i", 1 }, { "name", "中立" } }, { { "i", 2 }, { "name", "混沌" } } });
    // 役割, 種族, 性別, 属性
    json ok = json::array();
    const auto add = [&ok](int role, int race, int gender, int align) { ok.push_back(json::array({ role, race, gender, align })); };
    for (const int g : { 0, 1 }) {
        add(0, 0, g, 0); // 考古学者 人間 秩序・中立
        add(0, 0, g, 1);
        add(0, 2, g, 0); // 考古学者 ドワーフ 秩序
        add(1, 0, g, 1); // 野蛮人 人間 中立・混沌
        add(1, 0, g, 2);
        add(1, 3, g, 2); // 野蛮人 オーク 混沌
        add(2, 0, g, 0); // 騎士 人間 秩序
    }
    add(3, 0, 1, 0); // ワルキューレは女性だけ（人間 秩序・中立、ドワーフ 秩序）
    add(3, 0, 1, 1);
    add(3, 2, 1, 0);
    m["ok"] = ok;
    m["name"] = "テスター";
    m["pick"] = pick;
    return m;
}

NhSelect sample_select(const json &pick = json::object({ { "role", -1 }, { "race", -1 }, { "gender", -1 }, { "align", -1 } }))
{
    NhSelect sel;
    check(sel.load(role_payload(pick).dump()), "select load");
    return sel;
}

void test_select()
{
    NhSelect sel = sample_select();
    check_eq(sel.request(), 3, "select: 要求の番号");
    check_eq(sel.name(), std::string("テスター"), "select: 名前");
    check_eq(static_cast<int>(sel.roles().size()), 4, "select: 職業の数");
    check_eq(sel.role(), 0, "select: 初めは先頭の組（考古学者）");
    check_eq(sel.race(), 0, "select: 初めの種族は人間");
    check_eq(sel.gender(), 0, "select: 初めの性別は男性");
    check_eq(sel.align(), 0, "select: 初めの属性は秩序");

    // 今の職業に合わない種族（エルフ）は選べない。ドワーフは合う。
    check(!sel.race_ok(1), "select: 考古学者にエルフは合わない（赤字）");
    check(sel.race_ok(2), "select: 考古学者にドワーフは合う");
    check(!sel.choose_race(1), "select: 合わない種族は選べない");
    check_eq(sel.race(), 0, "select: 合わない種族を選ぼうとしても変わらない");
    check(!sel.align_ok(2), "select: 考古学者の人間に混沌は合わない（灰色）");
    check(!sel.choose_align(2), "select: 合わない属性は選べない");

    // 職業を替えると、合う種族・性別・属性へ直す。
    check(sel.choose_role(2), "select: 騎士を選ぶ");
    check_eq(sel.race(), 0, "select: 騎士でも人間のまま（合うので変えない）");
    check_eq(sel.align(), 0, "select: 騎士は秩序");
    check(!sel.align_ok(1), "select: 騎士に中立は合わない");
    check(sel.choose_role(1), "select: 野蛮人を選ぶ");
    check_eq(sel.align(), 1, "select: 秩序は野蛮人に合わないので、先頭の合う属性（中立）へ");
    check(sel.choose_race(3), "select: 野蛮人にオークは合う");
    check_eq(sel.align(), 2, "select: オークを選ぶと属性が混沌へ直る");
    check(sel.choose_role(3), "select: ワルキューレを選ぶ");
    check_eq(sel.gender(), 1, "select: ワルキューレは女性だけ（性別が女性へ直る）");
    check_eq(sel.race(), 0, "select: 種族はオークが合わないので、先頭の合う種族（人間）へ");
    check(!sel.gender_ok(0), "select: ワルキューレに男性は合わない（灰色）");
    check_eq(sel.role_label(sel.roles()[3]), std::string("ワルキューレ女"), "select: 女性のときは女性の職業名");
    check_eq(sel.role_label(sel.roles()[1]), std::string("野蛮人"), "select: 女性の名前が無ければそのまま");

    // 返事
    const json play = reply_of(sel.play_json());
    check_eq(play["a"].get<std::string>(), std::string("select"), "select: 開始の返事");
    check_eq(play["role"].get<int>(), 3, "select: 返事の職業");
    check_eq(play["gender"].get<int>(), 1, "select: 返事の性別");
    check_eq(play["r"].get<int>(), 3, "select: 返事の r");
    check_eq(reply_of(sel.random_json())["a"].get<std::string>(), std::string("random"), "select: ランダムの返事");
    check_eq(reply_of(sel.quit_json())["a"].get<std::string>(), std::string("quit"), "select: 終了の返事");

    // 焦点とキーボード
    NhSelect kb = sample_select();
    check(kb.focus() == NhSelFocus::Roles, "select: 焦点は職業の一覧から");
    kb.move_choice(1);
    check_eq(kb.role(), 1, "select: 下で次の職業");
    kb.move_focus(1);
    check(kb.focus() == NhSelFocus::Races, "select: Tab で種族へ");
    kb.move_choice(1); // 野蛮人の人間 → 次の合う種族（オーク）。エルフ・ドワーフは飛ばす
    check_eq(kb.race(), 3, "select: 合わない種族は飛ばして次の合う種族へ");
    kb.move_focus(-2);
    check(kb.focus() == NhSelFocus::Quit, "select: Shift+Tab で後ろへ（最後のボタンへ回る）");
    kb.move_focus(1);
    check(kb.focus() == NhSelFocus::Roles, "select: 回って職業へ戻る");

    // pick（先に決まっている選び）
    NhSelect picked = sample_select(json::object({ { "role", 3 }, { "race", -1 }, { "gender", -1 }, { "align", -1 } }));
    check_eq(picked.role(), 3, "select: pick の職業");
    check_eq(picked.gender(), 1, "select: pick と矛盾しない先頭の組（女性）");
    NhSelect picked2 = sample_select(json::object({ { "role", 1 }, { "race", 3 }, { "gender", 1 }, { "align", -1 } }));
    check_eq(picked2.align(), 2, "select: pick の残りは合う属性で埋める");

    // pick が全部決まっていて有効な組なら、窓を出さずに始められる（原作の GUI 版 `plselRandomize`）
    check(!picked.pick_complete(), "select: 職業だけ決まっていても窓は出す");
    check(!sample_select().pick_complete(), "select: 何も決まっていなければ窓を出す");
    NhSelect full = sample_select(json::object({ { "role", 3 }, { "race", 0 }, { "gender", 1 }, { "align", 0 } }));
    check(full.pick_complete(), "select: 4 つとも決まっていて有効な組");
    check_eq(full.role(), 3, "select: 全部決まった選びの職業");
    NhSelect invalid = sample_select(json::object({ { "role", 3 }, { "race", 0 }, { "gender", 0 }, { "align", 0 } }));
    check(!invalid.pick_complete(), "select: 4 つとも決まっていても、ワルキューレの男性は無効（窓を出す）");

    // 表が無いときは、すべて有効
    NhSelect open;
    json m = role_payload(json::object());
    m.erase("ok");
    check(open.load(m.dump()), "select: ok が無くても読む");
    check(open.race_ok(1), "select: 表が無ければエルフも合う");
    check(open.choose_role(3), "select: 表が無ければどの職業も選べる");

    NhSelect bad;
    check(!bad.load("{\"k\":\"menu\"}"), "select: select でない知らせは読まない");
}

void test_input()
{
    NhTextField f;
    f.set_max_bytes(10);
    check_eq(f.insert("abc"), 3, "field: 字を足す");
    check_eq(f.insert("日本"), 6, "field: 全角は 3 バイトずつ");
    check_eq(f.text(), std::string("abc日本"), "field: 中身");
    check_eq(f.insert("語"), 0, "field: 上限を超える字は足さない（文字の切れ目で捨てる）");
    check_eq(f.insert("x"), 1, "field: 上限までは足せる");
    check_eq(f.insert("y"), 0, "field: 上限");
    check(f.backspace(), "field: BS");
    check(f.backspace(), "field: BS");
    check_eq(f.text(), std::string("abc日"), "field: 全角は 1 字ずつ戻す");
    check(f.backspace(), "field: BS で全角が消える");
    check_eq(f.text(), std::string("abc"), "field: 全角を 1 字で消す");
    f.clear();
    check(!f.backspace(), "field: 空の BS は何もしない");
    f.insert("a\tb\x01");
    check_eq(f.text(), std::string("ab"), "field: 制御文字は捨てる");
    f.set_preedit("へんかん");
    check_eq(f.preedit(), std::string("へんかん"), "field: 変換中の字");
    f.clear();
    check(f.preedit().empty(), "field: 消すと変換中の字も消える");

    char result = 0;
    check(nh_resolve_yn('y', "yn", 'n', result) && (result == 'y'), "yn: 選べる字");
    check(nh_resolve_yn('Y', "yn", 'n', result) && (result == 'y'), "yn: 大文字は小文字として扱う");
    check(!nh_resolve_yn('x', "yn", 'n', result), "yn: 選べない字は捨てる");
    check(nh_resolve_yn(0x1B, "ynq", 'n', result) && (result == 'q'), "yn: Esc は q（あれば）");
    check(nh_resolve_yn(0x1B, "yn", 'y', result) && (result == 'n'), "yn: q が無ければ Esc は n");
    check(nh_resolve_yn(0x1B, "ab", 'a', result) && (result == 'a'), "yn: どちらも無ければ Esc は既定");
    check(nh_resolve_yn(' ', "yn", 'n', result) && (result == 'n'), "yn: スペースは既定");
    check(nh_resolve_yn('\r', "yn", 'y', result) && (result == 'y'), "yn: Enter は既定");
    check(nh_resolve_yn('h', "", 0, result) && (result == 'h'), "yn: 選べる字が無い問い（方向）は何でも受ける");
    check(nh_resolve_yn('a', "$a-f", 0, result) && (result == 'a'), "yn: 字の範囲の問いの文字");
    check_eq(nh_yn_prompt_text("Really quit?", "yn", 'n'), std::string("Really quit? [yn] (n)"), "yn: 問いの表示");
    check_eq(nh_yn_prompt_text("In what direction?", "", 0), std::string("In what direction?"), "yn: 選べる字が無ければ問いのまま");
    check_eq(nh_yn_prompt_text("Save?", "yn", 0), std::string("Save? [yn]"), "yn: 既定が無ければ括弧を出さない");

    const std::vector<std::string> cmds = { "adjust", "annotate", "apply", "pray", "ride", "rub" };
    const std::vector<int> a = nh_ext_matches(cmds, "a");
    check_eq(static_cast<int>(a.size()), 3, "ext: a で始まる名前");
    check_eq(static_cast<int>(nh_ext_matches(cmds, "").size()), 6, "ext: 何も打たなければ全部");
    check_eq(static_cast<int>(nh_ext_matches(cmds, "PR").size()), 1, "ext: 大文字小文字は同じ");
    check(nh_ext_matches(cmds, "zz").empty(), "ext: 合う名前が無い");
    check_eq(nh_ext_common_prefix(cmds, a), std::string("a"), "ext: adjust・annotate・apply の共通の前方は a");
    check_eq(nh_ext_common_prefix(cmds, nh_ext_matches(cmds, "an")), std::string("annotate"), "ext: 1 つなら名前そのもの");
    check_eq(nh_ext_common_prefix(cmds, nh_ext_matches(cmds, "r")), std::string("r"), "ext: ride と rub の共通の前方は r");
    const std::vector<std::string> jp = { "祈る", "乗る", "擦る" };
    check_eq(static_cast<int>(nh_ext_matches(jp, "祈").size()), 1, "ext: 日本語の名前の前方一致");
    check_eq(nh_ext_common_prefix(jp, nh_ext_matches(jp, "")), std::string(), "ext: 共通の前方が無ければ空");
    check_eq(static_cast<int>(nh_utf8_prefix_bytes("日本語", 2)), 6, "utf8: 2 字ぶんは 6 バイト");
}

void test_tiles()
{
    json j = json::object();
    j["tile_size"] = 16;
    j["columns"] = 40;
    j["rows"] = 58;
    j["tile_count"] = 100;
    j["glyph2tile"] = json::array({ 0, 2, 4, -1, 99, 100 });
    NhTileTable table;
    std::string err;
    check(nh_tiles_parse(j.dump(), table, err), "tiles: 読める");
    check(table.valid(), "tiles: 表がある");
    check_eq(table.tile_for_glyph(1), 2, "tiles: glyph から絵の番号");
    check_eq(table.tile_for_glyph(3), -1, "tiles: 表の値が負なら絵なし");
    check_eq(table.tile_for_glyph(5), -1, "tiles: 絵の数を超える番号は絵なし");
    check_eq(table.tile_for_glyph(99), -1, "tiles: 表の外の glyph は絵なし");
    check_eq(table.tile_for_glyph(-1), -1, "tiles: 負の glyph は絵なし");
    int x = -1;
    int y = -1;
    check(table.tile_origin(41, x, y), "tiles: 絵の位置");
    check_eq(x, 16, "tiles: 41 番は 2 列目");
    check_eq(y, 16, "tiles: 41 番は 2 行目");
    check(!table.tile_origin(100, x, y), "tiles: 範囲外の番号は位置が無い");
    float u0 = 0.f;
    float v0 = 0.f;
    float u1 = 0.f;
    float v1 = 0.f;
    check(nh_tile_uv(table, 41, 640, 928, u0, v0, u1, v1), "tiles: UV");
    check(u0 > 16.f / 640.f && u0 < 17.f / 640.f, "tiles: 左端は絵の左端のすぐ内側");
    check(u1 < 32.f / 640.f && u1 > 31.f / 640.f, "tiles: 右端は絵の右端のすぐ内側");
    check(v0 > 16.f / 928.f && v1 < 32.f / 928.f, "tiles: 縦も同じ");
    check(!nh_tile_uv(table, 41, 16, 16, u0, v0, u1, v1), "tiles: 画像より外の絵は貼れない");
    check(!nh_tiles_parse("{}", table, err), "tiles: glyph2tile が無ければ読めない");
    check(!nh_tiles_parse("oops", table, err), "tiles: 壊れた JSON は読めない");
}

void test_model_requests()
{
    NhModel model;
    check(model.request() == nullptr, "model: 初めは要求が無い");
    check(model.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"create\",\"id\":9,\"type\":\"invent\"}"), "model: 持ち物の窓の作成");
    check_eq(model.invent_window(), 9, "model: 持ち物の窓の番号");
    check(model.perm_invent(), "model: 持ち物の窓が作られたら常設の窓を出す");
    check(model.invent_payload().empty(), "model: 中身はまだ無い");
    const std::string inv = menu_payload(0, 0, "", { item(1, "a", "戦斧") });
    check(model.ingest(inv), "model: 持ち物の常設の窓の中身（r なし）");
    check(model.request() == nullptr, "model: r の無いメニューは要求にしない（返事は要らない）");
    check_eq(model.invent_payload(), inv, "model: 常設の窓の中身を覚える");
    check_eq(static_cast<int>(model.invent_version()), 1, "model: 中身が届くたびに版が増える");

    const std::string menu = menu_payload(5, 1, "どれ?", { item(1, "a", "x") });
    check(model.ingest(menu), "model: メニューの要求");
    check(model.request() != nullptr, "model: 要求がある");
    check_eq(model.request()->kind, std::string("menu"), "model: 要求の種類");
    check_eq(model.request()->r, 5, "model: 要求の番号");
    check_eq(model.request()->payload, menu, "model: 届いた字そのもの");
    const std::uint64_t serial = model.request()->serial;
    model.clear_request();
    check(model.request() == nullptr, "model: 返事を送ったら要求は閉じる");
    check(model.ingest("{\"t\":\"nethack_ui\",\"k\":\"ask\",\"r\":6,\"kind\":\"yn\",\"query\":\"Sure?\",\"choices\":\"yn\"}"), "model: yn の要求");
    check(model.request()->serial > serial, "model: 新しい要求は版が進む");
    check_eq(model.request()->kind, std::string("ask"), "model: 問いの種類");
    check_eq(model.last_request_kind(), std::string("ask:yn"), "model: 従来の last_request_kind も保つ");
    check(model.ingest("{\"t\":\"nethack_ui\",\"k\":\"win\",\"op\":\"destroy\",\"id\":9}"), "model: 持ち物の窓の破棄");
    check(!model.perm_invent(), "model: 窓が無く pref も無ければ出さない");
    check(model.ingest("{\"t\":\"nethack_ui\",\"k\":\"pref\",\"name\":\"perm_invent\",\"value\":\"1\"}"), "model: pref perm_invent");
    check(model.perm_invent(), "model: pref perm_invent が 1 なら出す");
    check(model.invent_payload().empty(), "model: 破棄したら中身も消える");
    check_eq(model.invent_window(), -1, "model: 窓の番号も消える");
    check(model.ingest("{\"t\":\"nethack_ui\",\"k\":\"exit\"}"), "model: 終わり");
    check(model.request() == nullptr, "model: 終わったら要求は閉じる");
}

} // namespace

int main()
{
    test_menu_basic();
    test_menu_pick_one_and_none();
    test_menu_group_count_paging();
    test_menu_search_and_click();
    test_select();
    test_input();
    test_tiles();
    test_model_requests();
    std::cout << "checks: " << g_checks << ", failed: " << g_failed << std::endl;
    return (g_failed == 0) ? 0 : 1;
}
