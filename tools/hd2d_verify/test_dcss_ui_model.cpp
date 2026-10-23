// CPU-only test of the DCSS screen parts that need no GL: `hd2d/dcss/dcss_cells.*`, `dcss_model.*`, `dcss_message.*`, `dcss_stat.*`
// .
//
// Build and run (the bundled LLVM-MinGW; SDL and GL are not needed):
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I hd2d -I third_party/cpp/include ^
//     tools/hd2d_verify/test_dcss_ui_model.cpp hd2d/dcss/dcss_cells.cpp hd2d/dcss/dcss_model.cpp hd2d/dcss/dcss_message.cpp ^
//     hd2d/dcss/dcss_stat.cpp -o test_dcss_ui_model.exe
//   test_dcss_ui_model.exe
//
// Where the expected values come from (not from the code under test):
//   - the line breaks of the message area: the 1280x720 screenshot of the original tile version (`run1_d.png`): the sentence
//     "あなたは他の者が失敗したところで勝利できるだろうか？ ゾットのオーブを見つけられるだろうか？" breaks after "見つけら" in a
//     message area of 78 columns (1 column is the prefix glyph), which is what `wordwrap_line` gives for 77 cells;
//   - the status rows: the 1920x994 screenshot (`r1920_game.png`) and the source `output.cc` (`print_stats`, `draw_border`):
//     captions in brown, the value of the health at column 9, the bars at column 19 for 25 cells (43 - 18), the stat values at column 25;
//   - the colour table and the bar colours: `tilefont.cc` and `output.cc` (`colour_bar`).
#include "dcss/dcss_cells.h"
#include "dcss/dcss_message.h"
#include "dcss/dcss_model.h"
#include "dcss/dcss_stat.h"

#include <iostream>
#include <string>
#include <vector>

using namespace hd2d::dcss;

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

std::string line_text(const DcssStyledLine &line)
{
    std::string s;
    for (const DcssStyledChar &c : line) {
        s += dcss_encode_utf8(c.ch);
    }
    return s;
}

std::string row_text(const DcssTextGrid &g, int row)
{
    std::string s;
    for (int x = 0; x < g.cols; ++x) {
        const char32_t c = g.at(x, row);
        if (c == kWideFiller) {
            continue;
        }
        s += dcss_encode_utf8(c);
    }
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    return s;
}

void test_cells()
{
    check_eq(dcss_cell_width(U'a'), 1, "半角は 1");
    check_eq(dcss_cell_width(U'あ'), 2, "全角は 2");
    check_eq(dcss_cell_width(U'『'), 2, "『 は 2");
    check_eq(dcss_cell_width(U'…'), 1, "… は 1（原作の wcwidth は半角）");
    check_eq(dcss_text_width("体力: 18"), 5 + 1 + 2, "表示幅");
    check_eq(dcss_chop_string("セットなし", 9, false), std::string("セットな"), "全角を半分だけ残さない（小さい配置の矢筒）");
    check_eq(dcss_chop_string("ロングソード (+0)", 6, false), std::string("ロング"), "武器名を 6 マスに切る");
    check_eq(dcss_chop_string("AC:", 5), std::string("AC:  "), "足りなければ空白で埋める");

    // 色の表（tilefont.cc）
    check_eq(static_cast<int>(dcss_term_rgb(kBrown).r), 165, "BROWN の赤");
    check_eq(static_cast<int>(dcss_term_rgb(kLightGreen).g), 255, "LIGHTGREEN の緑");
    check_eq(dcss_colour_from_name("lightgray", -1), static_cast<int>(kLightGrey), "綴りの別名");

    // 色つきの書式
    const DcssStyledLine tagged = dcss_parse_tagged("<yellow>ようこそ<lightgrey>、<white>A</white>B");
    check_eq(line_text(tagged), std::string("ようこそ、AB"), "タグは字に残らない");
    check_eq(static_cast<int>(tagged[0].colour), static_cast<int>(kYellow), "開きの色");
    check_eq(static_cast<int>(tagged[5].colour), static_cast<int>(kWhite), "入れ子の色");
    check_eq(static_cast<int>(tagged[6].colour), static_cast<int>(kLightGrey), "閉じで 1 つ戻る");
    check_eq(line_text(dcss_parse_tagged("a<<b")), std::string("a<b"), "<< は <");

    // 折り返し（実機の絵: 1280x720 のメッセージ欄は 78 桁。先頭の 1 桁は印）
    const DcssStyledLine sentence = dcss_parse_tagged("あなたは他の者が失敗したところで勝利できるだろうか？ ゾットのオーブを見つけられるだろうか？");
    const std::vector<DcssStyledLine> wrapped = dcss_wrap_line(sentence, 77);
    check_eq(wrapped.size(), static_cast<std::size_t>(2), "2 行に折り返す");
    if (wrapped.size() == 2) {
        check_eq(line_text(wrapped[0]), std::string("あなたは他の者が失敗したところで勝利できるだろうか？ ゾットのオーブを見つけら"), "1 行目（実機の絵と同じ位置）");
        check_eq(line_text(wrapped[1]), std::string("れるだろうか？"), "2 行目");
    }
    // 禁則: 行頭に「。」を置かない（手前の字ごと次の行へ送る）。幅 10 マスの 1 行に「あいうえお。」（11 マス）
    const std::vector<DcssStyledLine> kinsoku = dcss_wrap_line(dcss_parse_tagged("あいうえお。"), 10);
    check_eq(kinsoku.size(), static_cast<std::size_t>(2), "禁則で 2 行");
    if (kinsoku.size() == 2) {
        check_eq(line_text(kinsoku[0]), std::string("あいうえ"), "「。」を行頭に置かない");
        check_eq(line_text(kinsoku[1]), std::string("お。"), "禁則の 2 行目");
    }
    // 空白で切れるなら空白で切る（英文）
    const std::vector<DcssStyledLine> english = dcss_wrap_line(dcss_parse_tagged("You hit the goblin. You miss the jackal."), 20);
    check_eq(english.size(), static_cast<std::size_t>(2), "英文は 2 行");
    if (english.size() == 2) {
        check_eq(line_text(english[0]), std::string("You hit the goblin."), "空白の所で切る");
        check_eq(line_text(english[1]), std::string("You miss the jackal."), "行の頭の空白は捨てる");
    }

    // 文字格子: 全角は 2 マスを使い、2 マス目に詰め物を置く
    DcssTextGrid g;
    g.resize(10, 2);
    g.goto_xy(1, 1);
    g.putstr("体力:18");
    check_eq(g.where_x(), 1 + 5 + 2, "全角 2 字 + ':' + 2 桁 = 7 マス進む");
    check(g.at(0, 0) == U'体' && g.at(1, 0) == kWideFiller, "全角の次は詰め物");
    g.goto_xy(2, 1); // 全角の 2 マス目から上書きすると 1 マス目は空白
    g.putstr("x");
    check(g.at(0, 0) == U' ', "全角の半分を上書きしたら残りは空白");
}

const char *kFirstPlayer = R"JSON({"msg":"player","name":"m1920","title":"盾を帯びし者","wizard":0,"explore":0,"species":"Human","god":"","penance":0,"piety_rank":0,"ostracism_pips":0,"form":0,"hp":18,"hp_max":18,"real_hp_max":18,"mp":1,"mp_max":1,"dd_real_mp_max":0,"poison_survival":18,"ac":6,"ev":7,"sh":4,"ac_mod":0,"ev_mod":0,"sh_mod":0,"str":16,"int":8,"dex":12,"doom":0,"contam":0,"xl":1,"progress":0,"gold":0,"noise":-1,"adjusted_noise":0,"time":0,"turn":0,"place":"Dungeon","depth":1,"pos":{"x":0,"y":0},"status":[],"inv":{"0":{"base_type":0,"quantity":1,"letter":97,"name":"ロングソード (+0)","col":10},"1":{"base_type":2,"quantity":1,"letter":98,"name":"スケイルメイル (+0)","col":10}},"quiver_item":-1,"quiver_desc":"<darkgrey>セットなし<lightgrey>","weapon_index":0,"offhand_index":-1,"offhand_weapon":0,"unarmed_attack":"何も装備していない","unarmed_attack_colour":7})JSON";

void test_model()
{
    DcssUiModel m;
    check(!m.player().seen, "最初は入っていない");
    check(m.ingest_line(kFirstPlayer), "player を受ける");
    check(m.player().seen, "名前が入ればゲームに入っている");
    check_eq(m.player().hp, 18, "体力");
    check_eq(m.player().intel, 8, "知力（int）");
    check_eq(m.player().inv.at(0).name, std::string("ロングソード (+0)"), "持ち物の名前");
    // 差分: 一部の欄だけの player は残りを保つ
    check(m.ingest_line(R"({"msg":"player","time":210,"turn":21,"pos":{"x":3,"y":11}})"), "差分の player");
    check_eq(m.player().time, 210, "差分で時間が替わる");
    check_eq(m.player().hp_max, 18, "差分で触れない欄は残る");
    check_eq(m.player_x(), 3, "位置");

    // msgs
    check(m.ingest_line(R"({"msg":"msgs","messages":[{"text":"<yellow>A<lightgrey>","turn":0,"channel":0},{"text":"B","turn":1,"channel":0}]})"), "msgs");
    check_eq(m.messages().size(), static_cast<std::size_t>(2), "2 件");
    check(m.ingest_line(R"({"msg":"msgs","rollback":1,"more":true})"), "取り消しと more");
    check_eq(m.messages().size(), static_cast<std::size_t>(1), "後ろから 1 件取り消す");
    check(m.more(), "more の旗");

    // map: x だけ省ける・y が替わると x も書く・階が替わると空
    check(m.ingest_line(R"({"msg":"map","clear":true,"vgrdc":{"x":0,"y":0},"cells":[{"x":-1,"y":0,"mf":1},{"mf":2},{"x":-1,"y":1,"mf":2},{"mf":1}]})"), "map");
    check_eq(m.map_feature_at(-1, 0), 1, "(-1,0) は床");
    check_eq(m.map_feature_at(0, 0), 2, "x を省いた次のマスは +1");
    check_eq(m.map_feature_at(-1, 1), 2, "行が替わると x・y を書く");
    check_eq(m.map_feature_at(0, 1), 1, "その次");
    const DcssUiModel::MapBounds b = m.map_bounds();
    check(b.valid && b.min_x == -1 && b.max_x == 0 && b.min_y == 0 && b.max_y == 1, "外接の矩形");
    // ミニマップの自分の印と除外（`mf` には入らない。原作 `update_minimap` が足す）
    check(m.ingest_line(R"({"msg":"map","cells":[{"x":3,"y":11,"mf":1},{"x":4,"y":11,"mf":1,"t":{"bg":8388608}},{"mf":1,"t":{"bg":4194304}},{"mf":2,"t":{"bg":8388608}}]})"), "除外つきの map");
    check_eq(m.map_feature_at(3, 11), static_cast<int>(kMfPlayer), "自分のマスは MF_PLAYER（player.pos）");
    check_eq(m.map_feature_at(4, 11), static_cast<int>(kMfExclRoot), "除外の中心（床）");
    check_eq(m.map_feature_at(5, 11), static_cast<int>(kMfExcl), "除外（床）");
    check_eq(m.map_feature_at(6, 11), static_cast<int>(kMfWall), "壁は除外の印が付いても壁のまま");
    check(m.ingest_line(R"({"msg":"map","clear":true,"cells":[]})"), "階が替わる");
    check(!m.map_bounds().valid, "空になる");

    // options
    check(m.ingest_line(R"({"msg":"options","options":{"hp_colour":[{"value":80,"colour":"lightred"}],"tile_floor_col":{"r":1,"g":2,"b":3}}})"), "options");
    check_eq(m.options().hp_colour.size(), static_cast<std::size_t>(1), "体力の色の段");
    check_eq(m.options().hp_colour[0].second, static_cast<int>(kLightRed), "段の色");
    check_eq(static_cast<int>(m.options().map_colour[kMfFloor].b), 3, "ミニマップの床の色");

    // 日本語の名前の表
    check_eq(dcss_species_name_ja("Human"), std::string("人間"), "種族");
    check_eq(dcss_species_name_ja("Hill Orc"), std::string("丘オーク"), "種族（2 語）");
    check_eq(dcss_branch_name_ja("Dungeon"), std::string("迷宮"), "場所");
    check_eq(dcss_branch_name_ja("an Ice Cave"), std::string(dcss_branch_name_ja("Ice Cave")), "冠詞つきの場所");
    check_eq(dcss_god_name_ja("Trog"), std::string("トログ"), "神");
    check(dcss_god_name_ja("Jiyva Xyzzy").find("Xyzzy") != std::string::npos, "ジーヴァの 2 つ目の名前は残す");
    check_eq(dcss_species_name_ja("Nobody"), std::string("Nobody"), "表に無ければ英語のまま");
}

void test_message_window()
{
    DcssUiModel m;
    DcssMessageWindow w;
    w.resize(30, 4); // 30 桁・4 行（最終行は `--more--` のために空けるので、置けるのは 3 行）
    m.ingest_line(R"({"msg":"input_mode","mode":0})");
    m.ingest_line(kFirstPlayer);
    m.ingest_line(R"({"msg":"msgs","messages":[{"text":"<yellow>one<lightgrey>","turn":0,"channel":0}]})");
    w.sync(m);
    DcssTextGrid g;
    w.build(m, g);
    check_eq(row_text(g, 0), std::string(" one"), "先頭の 1 桁は印の字（空白）");
    check_eq(static_cast<int>(g.attr_at(1, 0) & 0x0F), static_cast<int>(kYellow), "色");
    // 指令待ちに入ると最後の行の先頭に `_`
    m.ingest_line(R"({"msg":"input_mode","mode":1})");
    w.sync(m);
    w.build(m, g);
    check(g.at(0, 0) == U'_', "指令待ちで最後の行の先頭が _");
    // 3 行を超えるときは、既読（入力のあと）の行から押し出す
    m.ingest_line(R"({"msg":"input_mode","mode":0})");
    w.sync(m);
    m.ingest_line(R"({"msg":"msgs","messages":[{"text":"two","turn":1,"channel":0},{"text":"three","turn":1,"channel":0},{"text":"four","turn":1,"channel":0}]})");
    w.sync(m);
    w.build(m, g);
    check_eq(row_text(g, 0), std::string(" two"), "既読の one は押し出される");
    check_eq(row_text(g, 2), std::string(" four"), "新しい 3 行が残る");
    // 長いメッセージは折り返す。最終行は空いている
    m.ingest_line(R"({"msg":"msgs","messages":[{"text":"x","turn":1,"channel":0}]})");
    w.sync(m);
    w.build(m, g);
    check_eq(row_text(g, 3), std::string(""), "最終行は使わない");
    // `-more-`
    m.ingest_line(R"({"msg":"msgs","more":true})");
    w.build(m, g);
    check_eq(row_text(g, 3), std::string(" --more--"), "最終行の 2 桁目から --more--");
    check_eq(static_cast<int>(g.attr_at(1, 3) & 0x0F), static_cast<int>(kCyan), "--more-- は PROMPT チャンネルの色（シアン）");
    // 取り消し
    m.ingest_line(R"({"msg":"msgs","rollback":1,"more":false})");
    w.sync(m);
    w.build(m, g);
    bool has_x = false;
    for (int r = 0; r < g.rows; ++r) {
        has_x = has_x || (row_text(g, r) == " x");
    }
    check(!has_x, "取り消したメッセージは消える");
}

void test_stat_normal()
{
    DcssUiModel m;
    m.ingest_line(kFirstPlayer);
    DcssStatPanel panel;
    DcssTextGrid g;
    panel.build(m, 43, 13, false, true, g);

    check_eq(row_text(g, 0), std::string("盾を帯びし者『m1920』"), "1 行目（称号『名前』）");
    check_eq(static_cast<int>(g.attr_at(0, 0) & 0x0F), static_cast<int>(kYellow), "1 行目は黄色");
    check_eq(row_text(g, 1), std::string("人間"), "2 行目（種族）");
    check_eq(row_text(g, 2), std::string("体力:   18/18"), "体力の行（値は 9 桁目）");
    check_eq(static_cast<int>(g.attr_at(0, 2) & 0x0F), static_cast<int>(kBrown), "見出しは茶色");
    check_eq(static_cast<int>(g.attr_at(8, 2) & 0x0F), static_cast<int>(kLightGrey), "体力 100% の値は灰色");
    check_eq(row_text(g, 3), std::string("魔力:   1/1"), "魔力の行");
    check_eq(row_text(g, 4), std::string("AC:    6          腕力: 16"), "AC と 腕力");
    check_eq(row_text(g, 5), std::string("回避:  7          知力: 8"), "回避 と 知力");
    check_eq(row_text(g, 6), std::string("盾:    4          器用: 12"), "盾 と 器用");
    check_eq(row_text(g, 7), std::string("Lv:  1 次Lv:  0%  場所:  迷宮:1"), "XL・次のレベル と 場所");
    check_eq(row_text(g, 8), std::string("騒音:") + std::string(13, ' ') + "時間: 0.0 (0.0)", "騒音 と 時間");
    check_eq(row_text(g, 9), std::string("a) ロングソード (+0)"), "武器");
    check_eq(static_cast<int>(g.attr_at(3, 9) & 0x0F), static_cast<int>(kLightGreen), "武器の名前の色はアイテムの色");
    check_eq(row_text(g, 10), std::string("セットなし"), "矢筒");

    // 棒: 19 桁目から 43 - 18 = 25 マス。体力は LIGHTGREEN、魔力は BLUE、騒音（0）は DARKGREY の空
    for (int x = 18; x < 43; ++x) {
        if ((g.attr_at(x, 2) >> 4) != kLightGreen) {
            check(false, "体力の棒は 25 マス全部 LIGHTGREEN");
            break;
        }
    }
    check_eq(static_cast<int>(g.attr_at(17, 2) >> 4), 0, "棒の手前は背景なし");
    check_eq(static_cast<int>(g.attr_at(18, 3) >> 4), static_cast<int>(kBlue), "魔力の棒は BLUE");
    check_eq(static_cast<int>(g.attr_at(7, 8) >> 4), static_cast<int>(kDarkGrey), "騒音の棒（0）は DARKGREY の空（8 桁目から 9 マス）");
    check_eq(static_cast<int>(g.attr_at(15, 8) >> 4), static_cast<int>(kDarkGrey), "騒音の棒の最後（9 マス目）");
    check_eq(static_cast<int>(g.attr_at(16, 8) >> 4), 0, "騒音の棒は 9 マスで終わる");
}

void test_stat_changes()
{
    DcssUiModel m;
    m.ingest_line(kFirstPlayer);
    DcssStatPanel panel;
    DcssTextGrid g;
    panel.build(m, 43, 13, false, true, g);
    // 体力が 18 → 9 に減ると、棒の減った分（後ろ半分）が RED（変化）で出て、4 手番後に戻す印が付く
    m.ingest_line(R"({"msg":"player","hp":9,"turn":1})");
    panel.build(m, 43, 13, false, true, g);
    check_eq(static_cast<int>(g.attr_at(18, 2) >> 4), static_cast<int>(kLightGreen), "残った前半は LIGHTGREEN");
    check_eq(static_cast<int>(g.attr_at(42, 2) >> 4), static_cast<int>(kRed), "減った分は RED");
    check_eq(panel.redraw_after_turn(), 1 + 4, "4 手番後に組み直す");
    // 体力 50% は黄色（hp_colour の既定は 70 → yellow・40 → red。50 は 70 以下なので黄）
    check_eq(static_cast<int>(g.attr_at(8, 2) & 0x0F), static_cast<int>(kYellow), "体力 50% の値は黄");
    // 組み直すと変化の色が消える
    m.ingest_line(R"({"msg":"player","turn":6})");
    panel.build(m, 43, 13, false, true, g);
    check_eq(static_cast<int>(g.attr_at(42, 2) >> 4), static_cast<int>(kDarkGrey), "4 手番後は空の色");
}

void test_stat_small()
{
    DcssUiModel m;
    m.ingest_line(kFirstPlayer);
    DcssStatPanel panel;
    panel.set_character_abbrev("HuFi");
    DcssTextGrid g;
    // 小さい配置: 9 桁 × 45 行（実機の 1280x720 の絵と同じ。体力の数字は一番下の行）
    panel.build(m, 9, 45, true, true, g);
    check_eq(row_text(g, 0), std::string("m1920"), "名前だけ");
    check_eq(row_text(g, 1), std::string("HuFi"), "種族と職業の略称");
    check_eq(row_text(g, 5), std::string("AC EV SH"), "AC・EV・SH の見出し（2 字）");
    check_eq(row_text(g, 6), std::string(" 6  7  4"), "値は右寄せ 2 桁");
    check_eq(row_text(g, 7), std::string("St In De"), "能力値の見出し");
    check_eq(row_text(g, 8), std::string("16  8 12"), "能力値");
    check_eq(row_text(g, 9), std::string("XL Next"), "XL の見出し");
    check_eq(row_text(g, 10), std::string(" 1  0%"), "XL と 次のレベル");
    check_eq(row_text(g, 12), std::string("Noise"), "騒音の見出し");
    check_eq(row_text(g, 14), std::string("Place"), "場所の見出し");
    check_eq(row_text(g, 15), std::string("迷宮:1"), "場所");
    check_eq(row_text(g, 16), std::string("Time"), "時間の見出し");
    check_eq(row_text(g, 17), std::string("0.0"), "時間");
    check_eq(row_text(g, 19), std::string("a) ロング"), "武器（6 マスに切る）");
    check_eq(row_text(g, 20), std::string("セットなし").substr(0, 12), "矢筒（9 マスに切る → 全角 4 字）");
    check_eq(row_text(g, 44), std::string("  18   1"), "体力・魔力の数字は一番下の行（右寄せ 3 桁を 2 桁目・6 桁目から）");
    // 縦の棒: 体力は 2〜4 桁目、魔力は 6〜8 桁目。下から 18 行（27 行目〜44 行目。原作の `vdraw` は一番下の行を 2 回塗る癖がある）
    check_eq(static_cast<int>(g.attr_at(1, 43) >> 4), static_cast<int>(kLightGreen), "体力の縦の棒（下端）");
    check_eq(static_cast<int>(g.attr_at(4, 43) >> 4), 0, "棒の間（5 桁目）は背景なし");
    check_eq(static_cast<int>(g.attr_at(5, 43) >> 4), static_cast<int>(kBlue), "魔力の縦の棒は 6 桁目から");
    check_eq(static_cast<int>(g.attr_at(1, 26) >> 4), static_cast<int>(kLightGreen), "体力の縦の棒（上端は 27 行目。実機の絵で上端 y=416 = 26 行 × 16）");
    check_eq(static_cast<int>(g.attr_at(1, 25) >> 4), 0, "上端の上は空き");
}

} // namespace

int main()
{
    test_cells();
    test_model();
    test_message_window();
    test_stat_normal();
    test_stat_changes();
    test_stat_small();
    std::cout << g_checks << " checks, " << g_failed << " failed" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
