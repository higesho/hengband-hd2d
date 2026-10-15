#pragma once
/*!
 * @file birth_cursor_checks.h
 * @brief キャラクター作成の窓の「最初のカーソルの位置」と「決定・左右の意味」の検査（修-29・修-59）。窓も GL もコアも要らない。
 * @details `HD2D_BIRTH_CURSOR_CHECK=1 HengbandHd2d.exe --ui-check`。本物の `ModernStoreUi` に窓を直接渡して確かめる。
 *  - 修-29（Frox のキャラ作成の本体）：コアが `initial_target`（「次の画面」の行）を送ると、窓が新しく出たときの最初のカーソルがそこに置かれ、
 *    素の決定がその行を実行する。同じ窓のまま更新されてもカーソルは戻らない。別の窓を挟んで戻ると、また「次の画面」に置かれる。
 *    `initial_target` が空の窓は今までどおり先頭。JSON の往復で落ちない・知らない行を指していれば捨てる・古い JSON でも読める。
 *  - 修-59（Sil-Q の能力値・技能の窓 `sil_allocation`）：決定はどの行にいても「次に進む」を送り、点を増やさない。左右は「−」「＋」の行のキー
 *    （4・6）を送る。原作のカーソルが項目へ着いていなければ、先に項目へ動かしてから送る。
 *    ほかの窓の種類（`stat_allocation`）の決定は今までどおり選んだ行を実行する。
 *  - 修-07（設定の一覧 `settings_list`）：左右は窓が申告する動作の表にある「値を変えるキー」の組を送る（Sil-Q は 6/4、変愚系・Frox・Angband は y/n、
 *    どちらも無ければ y/n）。Rogue 系（`settings_list_rogue`）は今までどおり何も送らない。
 *  - 判-14（Sil-Q の設定の一覧の決定）：動作の表が 6/4 の一覧だけ、決定が窓の申告する `continue_key`（切の項目は 6、入の項目は 4）を送り値を 1 つ進める。
 *    動作の表が y/n の一覧（変愚系・Frox・Angband）の決定は今までどおり何も送らない。
 *  - 判-12（文字入力の窓の題）：選択肢の窓（職業の選択など）の上に文字入力の窓が重なるとき、題はコアが送る入力欄の題になる。
 *    数の入力の窓は背景の題のまま。
 */
#include "ui/modern_store_ui.h"
#include "frame/modern_window_codec.h"
#include <cstdio>
#include <string>
#include <vector>
namespace hd2d {
namespace birth_cursor_check {
inline ModernRow row(const std::string &id,const std::string &label,int key) {
    ModernRow r;r.id=id;r.label=label;r.key=r.hotkey=key;r.price_known=false;
    r.actions={{"choice",label,"",true,key}};return r;
}
//! Frox のキャラ作成の本体を模した窓（原作にカーソルが無い選択肢。`cursor=-1`）。
inline GameFrame hub_frame(int revision,const std::string &initial,const std::string &title="Character") {
    GameFrame f;f.menu_open=true;f.cells.emplace_back();
    auto &m=f.modern;m.version=5;m.kind="dialog";m.stage="choices";m.title=title;m.revision=revision;m.cursor=-1;m.language="en";
    const char *labels[]={"Change name","Randomize name","Change gender","Change race","Change class","Change speed","Settings","Help","Next screen","Previous screen"};
    const int keys[]={'n','*','s','r','c','g','=','?',13,27};
    for(int i=0;i<10;++i)m.rows.push_back(row("choice:"+std::to_string(i),labels[i],keys[i]));
    m.initial_target=initial;return f;
}
//! Sil-Q の能力値の窓を模した窓（項目 2 つの「−」「＋」と最後の「次に進む」）。原作のカーソルは `cursor_target`（「−」の行）にある。
inline GameFrame allocation_frame(int revision,const std::string &native_cursor,const char *content_type="sil_allocation",int request_id=0) {
    GameFrame f;f.menu_open=true;f.cells.emplace_back();
    auto &m=f.modern;m.version=5;m.kind="dialog";m.stage="cursor";m.title="Attributes";m.revision=revision;m.content_type=content_type;m.language="en";
    m.request_id=request_id;m.result=request_id?"accepted":"";
    for(int stat=0;stat<2;++stat) {
        const std::string name=stat?"Dexterity":"Strength";
        m.rows.push_back(row("allocation:"+std::to_string(stat)+":0",name+" -",'4'));
        m.rows.push_back(row("allocation:"+std::to_string(stat)+":1",name+" +",'6'));
    }
    m.rows.push_back(row("allocation:continue","Continue",13));
    m.cursor_target=native_cursor;return f;
}
inline const presentation::InputEventWire *last_event(const presentation::InputEventsMessage &out) {
    return out.events.empty()?nullptr:&out.events.back();
}
}
inline int run_birth_cursor_checks() {
    using namespace birth_cursor_check;
    int checks=0,failures=0;
    const auto check=[&](bool okay,const std::string &name){++checks;if(!okay){++failures;std::fprintf(stderr,"FAIL: %s\n",name.c_str());}};
    // ---- 修-29：最初のカーソルの位置 ----
    {
        ModernStoreUi ui;ui.sync(hub_frame(1,"choice:8"),true);
        check(ui.selected_row_id()=="choice:8","hub: the first cursor is on \"Next screen\" (got "+ui.selected_row_id()+")");
        presentation::InputEventsMessage out;ui.handle(CursorNav::Confirm,out);
        const auto *e=last_event(out);
        check(e&&e->action=="choice"&&e->target=="choice:8","hub: a bare Confirm runs \"Next screen\"");
    }
    {
        ModernStoreUi ui;ui.sync(hub_frame(1,"choice:8"),true);
        presentation::InputEventsMessage out;ui.handle(CursorNav::Up,out);
        check(ui.selected_row_id()=="choice:7","hub: Up moves from \"Next screen\" to \"Help\" (got "+ui.selected_row_id()+")");
        ui.sync(hub_frame(2,"choice:8"),true);
        check(ui.selected_row_id()=="choice:7","hub: the same window refreshed keeps the player's position (got "+ui.selected_row_id()+")");
        // 別の窓を挟んで戻ると、また「次の画面」に置く（種族を選んで戻ったあとも素の決定で次へ進める）
        GameFrame race=hub_frame(3,"","Race");race.modern.rows.resize(4);
        ui.sync(race,true);
        check(ui.selected_row_id()=="choice:0","other choices window without initial_target: starts at the first row (got "+ui.selected_row_id()+")");
        ui.sync(hub_frame(4,"choice:8"),true);
        check(ui.selected_row_id()=="choice:8","hub shown again: the cursor is on \"Next screen\" again (got "+ui.selected_row_id()+")");
    }
    {
        ModernStoreUi ui;ui.sync(hub_frame(1,""),true);
        check(ui.selected_row_id()=="choice:0","no initial_target: the cursor starts at the first row as before (got "+ui.selected_row_id()+")");
        ModernStoreUi ui2;ui2.sync(hub_frame(1,"choice:99"),true);
        check(ui2.selected_row_id()=="choice:0","unknown initial_target: ignored, first row (got "+ui2.selected_row_id()+")");
    }
    // ---- JSON の往復 ----
    {
        const auto j=encode_modern_window(hub_frame(1,"choice:8").modern);
        ModernWindow back;check(decode_modern_window(j,back)&&back.initial_target=="choice:8","codec: initial_target survives a round trip");
        auto plain=encode_modern_window(hub_frame(1,"").modern);
        check(!plain.contains("initial_target"),"codec: an empty initial_target is not written");
        ModernWindow old;check(decode_modern_window(plain,old)&&old.initial_target.empty(),"codec: JSON without initial_target (older core) still decodes");
        auto bad=j;bad["initial_target"]="choice:99";
        ModernWindow dropped;check(decode_modern_window(bad,dropped)&&dropped.initial_target.empty(),"codec: an unknown initial_target is dropped, the window is kept");
    }
    // ---- 修-59：Sil-Q の能力値・技能の窓 ----
    {
        ModernStoreUi ui;ui.sync(allocation_frame(1,"allocation:0:0"),true);
        presentation::InputEventsMessage out;ui.handle(CursorNav::Confirm,out);
        const auto *e=last_event(out);
        check(e&&out.events.size()==1&&e->action=="choice"&&e->target=="allocation:continue","sil_allocation: Confirm on a stat row goes to \"Continue\", it does not add a point");
    }
    {
        ModernStoreUi ui;ui.sync(allocation_frame(1,"allocation:0:0"),true);
        presentation::InputEventsMessage out;ui.handle(CursorNav::Right,out);
        const auto *e=last_event(out);
        check(e&&out.events.size()==1&&e->action=="choice"&&e->target=="allocation:0:1","sil_allocation: Right sends the \"+\" key of the stat the original cursor is on");
        ui.sync(allocation_frame(2,"allocation:0:0","sil_allocation",e?e->request_id:0),true);
        presentation::InputEventsMessage out2;ui.handle(CursorNav::Left,out2);
        const auto *e2=last_event(out2);
        check(e2&&out2.events.size()==1&&e2->action=="choice"&&e2->target=="allocation:0:0","sil_allocation: Left sends the \"-\" key");
    }
    {
        // 画面の選択が 2 番目の項目にあるのに、原作のカーソルがまだ最初の項目にあるとき：先に動かしてから「＋」を送る
        ModernStoreUi ui;ui.sync(allocation_frame(1,"allocation:0:0"),true);
        presentation::InputEventsMessage down;ui.handle(CursorNav::Down,down);
        const auto *d=last_event(down);
        check(d&&d->action=="focus"&&d->target=="allocation:1:0","sil_allocation: Down asks the original to move to the next stat");
        ui.sync(allocation_frame(2,"allocation:1:0","sil_allocation",d?d->request_id:0),true);
        presentation::InputEventsMessage right;ui.handle(CursorNav::Right,right);
        const auto *r=last_event(right);
        check(r&&right.events.size()==1&&r->action=="choice"&&r->target=="allocation:1:1","sil_allocation: Right on the second stat sends its \"+\" key");
    }
    {
        // 他の窓の種類（幻想蛮怒・変愚の能力値の窓 `stat_allocation`）は決定が選んだ行のキーのまま
        ModernStoreUi ui;ui.sync(allocation_frame(1,"allocation:0:0","stat_allocation"),true);
        presentation::InputEventsMessage out;ui.handle(CursorNav::Confirm,out);
        const auto *e=last_event(out);
        check(e&&e->action=="choice"&&e->target=="allocation:0:0","stat_allocation: Confirm still runs the selected row (unchanged)");
    }
    {
        // Angband の点割り振りも同じ `sil_allocation` の種類を使う（行は `an-points:*`・「次に進む」の行が無い）。決定は今までどおり選んだ行
        GameFrame f=allocation_frame(1,"an-points:0:0");
        f.modern.rows.pop_back();
        for(auto &r:f.modern.rows)r.id="an-points:"+r.id.substr(11);
        f.modern.cursor_target="an-points:0:0";
        ModernStoreUi ui;ui.sync(f,true);
        presentation::InputEventsMessage out;ui.handle(CursorNav::Confirm,out);
        const auto *e=last_event(out);
        check(e&&e->action=="choice"&&e->target=="an-points:0:0","Angband point-based window (sil_allocation without a Continue row): Confirm is unchanged");
    }
    // ---- 修-07：設定の一覧の左右は、窓が申告する「値を変えるキー」の組を送る（Sil-Q は 6/4、そのほかは y/n） ----
    {
        const auto settings_frame=[](int up,int down,bool rogue=false,int continue_key=0) {
            GameFrame f;f.menu_open=true;f.cells.emplace_back();
            auto &m=f.modern;m.version=5;m.kind="dialog";m.stage="cursor";m.title="Options";m.revision=1;m.language="en";
            m.content_type=rogue?"settings_list_rogue":"settings_list";
            for(int i=0;i<2;++i) {
                ModernRow r;r.id="opt:"+std::to_string(i);r.label="Option "+std::to_string(i);r.price_known=false;m.rows.push_back(std::move(r));
            }
            m.cursor_target="opt:0";m.continue_key=continue_key;
            if(up)m.actions.push_back({"key:"+std::to_string(up),"Set on / increase","",true,up});
            if(down)m.actions.push_back({"key:"+std::to_string(down),"Set off / decrease","",true,down});
            return f;
        };
        const auto press=[&](const GameFrame &f,CursorNav nav)->std::string {
            ModernStoreUi ui;ui.sync(f,true);
            presentation::InputEventsMessage out;ui.handle(nav,out);
            const auto *e=last_event(out);
            return (e&&e->action=="window_action")?e->target:std::string("(no window_action)");
        };
        const auto expect_target=[&](const GameFrame &f,CursorNav nav,const std::string &want,const std::string &name) {
            const auto got=press(f,nav);
            check(got==want,name+" (want "+want+", got "+got+")");
        };
        expect_target(settings_frame('6','4'),CursorNav::Right,"key:54","settings_list (Sil-Q actions 6/4): Right sends 6");
        expect_target(settings_frame('6','4'),CursorNav::Left,"key:52","settings_list (Sil-Q actions 6/4): Left sends 4");
        expect_target(settings_frame('y','n'),CursorNav::Right,"key:121","settings_list (actions y/n): Right sends y");
        expect_target(settings_frame('y','n'),CursorNav::Left,"key:110","settings_list (actions y/n): Left sends n");
        expect_target(settings_frame(0,0),CursorNav::Right,"key:121","settings_list (no key actions): Right falls back to y");
        expect_target(settings_frame(0,0),CursorNav::Left,"key:110","settings_list (no key actions): Left falls back to n");
        expect_target(settings_frame('6','4',true),CursorNav::Right,"(no window_action)","settings_list_rogue: Right still sends nothing (unchanged)");
        // ---- 判-14：Sil-Q の一覧（6/4）の決定は continue_key を送る。y/n の一覧と Rogue 系は何も送らない ----
        expect_target(settings_frame('6','4',false,'6'),CursorNav::Confirm,"key:54","settings_list (Sil-Q 6/4, option is off): Confirm sends 6 (turn on / increase)");
        expect_target(settings_frame('6','4',false,'4'),CursorNav::Confirm,"key:52","settings_list (Sil-Q 6/4, option is on): Confirm sends 4 (turn off)");
        expect_target(settings_frame('y','n',false,'y'),CursorNav::Confirm,"(no window_action)","settings_list (actions y/n): Confirm still sends nothing (unchanged)");
        expect_target(settings_frame('6','4',true,'6'),CursorNav::Confirm,"(no window_action)","settings_list_rogue: Confirm here still sends nothing (the toggle is its own action)");
    }
    // ---- 判-12：選択肢の窓の上に文字入力の窓が重なるとき、題は入力欄の題 ----
    {
        const auto input_frame=[](int revision,const char *stage,const std::string &title) {
            GameFrame f;f.menu_open=true;f.cells.emplace_back();
            auto &m=f.modern;m.version=5;m.kind="dialog";m.stage=stage;m.title=title;m.revision=revision;m.language="en";
            if(std::string(stage)=="text"){m.input_label="Name";m.input="Gate";}
            return f;
        };
        ModernStoreUi ui;ui.sync(hub_frame(1,"","Class"),true);
        check(ui.window_title()=="Class","title: the choices window shows its own title (got "+ui.window_title()+")");
        ui.sync(input_frame(2,"text","Enter a name"),true);
        check(ui.window_title()=="Enter a name","title: the text input over the choices shows the input title, not the background one (got "+ui.window_title()+")");
        ModernStoreUi ui2;ui2.sync(hub_frame(1,"","Class"),true);
        ui2.sync(input_frame(2,"number","Quantity"),true);
        check(ui2.window_title()=="Class","title: the number input still keeps the background title (unchanged) (got "+ui2.window_title()+")");
        ModernStoreUi ui3;ui3.sync(hub_frame(1,"","Class"),true);
        ui3.sync(input_frame(2,"text",""),true);
        check(ui3.window_title()=="Class","title: a text input without its own title keeps the background title (got "+ui3.window_title()+")");
    }
    std::fprintf(stderr,"[birth-cursor-check] %d checks, %d failures\n",checks,failures);
    std::printf("%s\n",failures?"RESULT: FAIL":"RESULT: PASS");
    return failures?1:0;
}
}
