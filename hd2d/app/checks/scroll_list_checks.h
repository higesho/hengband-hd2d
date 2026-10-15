#pragma once
/*!
 * @file scroll_list_checks.h
 * @brief 原作がスクロールで出す一覧の端の動き（案 A）の検査。**窓も GL もコアも要らない。**
 * @details `HD2D_SCROLL_LIST_CHECK=1 HengbandHd2d.exe --ui-check`。
 * 原作の一覧を模した「偽のコア」（知識のブラウザ型と、選択肢のメニュー型）を作り、本物の `NativeModernMenu::capture()` と
 * `ModernStoreUi`（`sync`・`handle`・`advance`）をつないで、上下キーだけで次を確かめる。
 *  - 原作が窓の高さまでしか描かない一覧でも、一覧の端でさらに下を押すと原作がスクロールして**最後の項目に届く**。
 *  - 原作の本当の末尾に着いたときだけ操作の行（`@action:`）が出る（スクロールで続きがある間は出ない）。
 *  - 本当の先頭・末尾で、さらに押すと反対の端へ回り込む（原作が回り込む一覧は原作に任せる）。
 * 直す前の画面（一覧の中だけで回り込む版）は、末尾へ届かず落ちる。
 */
#include "ui/modern_store_ui.h"
#include "bridge/native_modern.h"
#include "bridge/paged_list_keys.h"
#include <cstdio>
#include <string>
#include <vector>
namespace presentation {
//! 画面の実行体はコアの入力変換（`bridge/input_event_adapter.cpp`）をつないでいない。`NativeModernMenu::input()` の `set_number` だけが使うので、検査では何もしない。
void append_input_event_keys(const InputEventsMessage &,std::vector<int> &) {}
}
namespace hd2d {
namespace scroll_list_check {
inline std::string pad(std::string s,size_t n){if(s.size()<n)s.append(n-s.size(),' ');return s;}
//! 原作の一覧の模型。`browser` は変愚の知識のブラウザ（分類と名前の 2 列・窓が半ページずつスクロール・端で止まる）、`menu` は選択肢のメニュー。
struct Fake {
    bool browser{true};
    int total{40},visible{16},top{0},cur{0},column{0};   //!< column: 0＝分類（変愚は最初に分類へカーソルがある）・1＝名前
    bool wrap{false};   //!< menu: 端で回り込むか
    int keys_seen{0};
    void clamp_scroll() {
        if(cur<top)top=cur;
        while(cur>=top+visible)top=std::min(std::max(0,total-visible),top+std::max(1,visible/2));
        if(top<0)top=0;
    }
    void key(int k) {
        ++keys_seen;
        if(browser) {
            if(k=='4'||k=='6'){column=k=='6'?1:0;return;}
            if(column==0)return;
            if(k=='2')cur=std::min(total-1,cur+1);
            else if(k=='8')cur=std::max(0,cur-1);
            else if(k==' ')cur=std::min(total-1,cur+visible);
            else if(k=='-')cur=std::max(0,cur-visible);
            clamp_scroll();return;
        }
        if(k=='2'){cur=cur+1;if(cur>=total)cur=wrap?0:total-1;}
        else if(k=='8'){cur=cur-1;if(cur<0)cur=wrap?total-1:0;}
        if(cur<top)top=cur;
        if(cur>=top+visible)top=cur-visible+1;
        if(wrap&&cur==0)top=0;
    }
    GameFrame frame() const {
        GameFrame f;f.menu_open=true;
        const auto line=[&](int row,std::string text){TermMirrorLine l;l.source_row=row;l.text_utf8=std::move(text);f.menu_term_lines.push_back(std::move(l));};
        if(browser) {
            line(2,"Knowledge - Test");line(3,std::to_string(total)+" known");
            line(4,pad("Group",11)+pad("Name",22)+"Sym");line(5,std::string(70,'='));
            int cursor_line=0;
            for(int i=0;i<visible&&top+i<total;++i) {
                const int index=top+i;
                line(6+i,pad(i==0?"Group A":"",11)+"| "+pad("Item "+std::to_string(index),22)+"x");
                if(index==cur)cursor_line=int(f.menu_term_lines.size())-1;
            }
            line(23,"<dir>, 'r' to recall, ESC");
            f.menu_term_curs_row=cursor_line;f.menu_term_curs_col=column==1?13:0;
            return f;
        }
        line(0,"Pick one");
        for(int i=0;i<visible&&top+i<total;++i) {
            const int index=top+i;const std::string text="  Item "+std::to_string(index);
            line(1+i,text);
            MenuChoice c;c.line_index=i+1;c.span_begin=2;c.span_len=int(text.size())-2;c.key=0;c.selected=index==cur;c.label_utf8="Item "+std::to_string(index);
            f.menu_choices.push_back(c);
            if(index==cur){f.menu_core_cursor=c;f.menu_core_cursors.assign(1,c);}
        }
        return f;
    }
};
//! 画面では隣り合う行なのに、原作のカーソルが何手も歩かないと着かない窓の模型（幻想蛮怒の登場作品の一覧。調-08・F-gensoband-02）。
//! 画面の行の並びは `order`（原作の位置の番号 `slot`）、原作のカーソルは `slot` の昇順に 1 手ずつ動く（`modern_focus_key` の縦の動き）。
struct WalkRig {
    std::vector<int> order{0,3,1,4,2};
    int cur{0},revision{10},steps{0},continues{0},confirmed_slot{-1};
    std::string title{"Works"};
    ModernStoreUi ui;GameFrame frame;
    std::vector<presentation::InputEventWire> held;   //!< コアが受け取ってまだ返事をしていない操作
    WalkRig(){frame=build(0,0,"");ui.sync(frame,true);}
    GameFrame build(int request_id,int,const char *result) const {
        GameFrame f;f.menu_open=true;f.pre_game_menu=true;
        auto &m=f.modern;m.version=5;m.revision=revision;m.kind="dialog";m.stage="cursor";m.title=title;m.content_type="choice_grid";m.input_ready=true;
        m.request_id=request_id;m.result=result;
        for(size_t i=0;i<order.size();++i){ModernRow r;r.id="slot:"+std::to_string(order[i]);r.label="Work "+std::to_string(order[i]);r.slot=order[i];r.key=0;r.available=true;m.rows.push_back(r);}
        m.cursor_target="slot:"+std::to_string(cur);
        for(size_t i=0;i<order.size();++i)if(order[i]==cur)m.cursor=int(i);
        return f;
    }
    //! コアが受け取った操作をすべて処理して返事のフレームを作り、画面へ渡す。
    void core_reply() {
        for(const auto &e:held) {
            int request=e.request_id;const char *result="rejected";
            if(e.ui_revision==revision) {
                if(e.action=="focus") {
                    const int target=std::atoi(e.target.c_str()+5);
                    if(target!=cur){cur+=target>cur?1:-1;++steps;++revision;}
                    result="accepted";
                } else if(e.action=="continue") {++continues;confirmed_slot=cur;++revision;result="accepted";}
            }
            frame=build(request,0,result);
        }
        if(!held.empty()){held.clear();ui.sync(frame,true);}
    }
    //! 預けた決定のあとで窓が替わった（題が変わった）ことにする。
    void title_changed(){title="Other";frame=build(0,0,"accepted");ui.sync(frame,true);}
    void take(presentation::InputEventsMessage &out){for(auto &e:out.events)held.push_back(e);out.events.clear();}
    //! 返事待ちの途中で止める：操作を 1 回押し、コアに渡すだけで返事は返さない。
    void press(CursorNav nav){presentation::InputEventsMessage out;ui.handle(nav,out);take(out);}
    //! 描画 1 回ぶん：画面が次の操作を送り（advance）、コアが返事をし、返事が届いた回はすぐ次を送る（`hd2d_app.cpp` の late advance と同じ）。
    void tick(){
        presentation::InputEventsMessage out;ui.advance(out);take(out);
        const bool was=ui.pending();core_reply();
        if(was&&!ui.pending()){presentation::InputEventsMessage late;ui.advance(late);take(late);}
    }
    void settle(){for(int i=0;i<200;++i){tick();if(held.empty()&&!ui.pending())break;}}
};
//! 偽のコア・本物の `NativeModernMenu`・本物の `ModernStoreUi` をつないだ 1 組。
struct Rig {
    Fake fake;presentation::NativeModernMenu menu;ModernStoreUi ui;GameFrame frame;int requests{0};
    explicit Rig(Fake f):fake(std::move(f)){menu.set_enabled(true);menu.set_language("en");refresh();}
    void refresh(){frame=fake.frame();frame.modern={};menu.capture(frame);ui.sync(frame,true);}
    bool feed(presentation::InputEventsMessage &out) {
        bool any=false;
        for(const auto &event:out.events) {
            std::vector<int> keys;menu.input(event,keys);++requests;any=true;
            for(int k:keys)fake.key(k);
        }
        out.events.clear();
        if(any)refresh();
        return any;
    }
    void settle(){for(int i=0;i<6000;++i){presentation::InputEventsMessage out;ui.advance(out);if(!feed(out))break;}}
    void press(CursorNav nav,int times=1){for(int i=0;i<times;++i){presentation::InputEventsMessage out;ui.handle(nav,out);feed(out);settle();}}
    bool on_action() const {return ui.selected_row_id().rfind("@action:",0)==0;}
};
}

inline int run_scroll_list_checks() {
    using namespace scroll_list_check;
    int checks=0,failures=0;
    const auto check=[&](bool okay,const std::string &name){++checks;if(!okay){++failures;std::fprintf(stderr,"FAIL: %s\n",name.c_str());}};
    // ---- 知識のブラウザ型（分類 1 つ・名前 40 件・窓は 16 行） ----
    {
        Rig r(Fake{true,40,16});
        check(r.frame.modern.content_type=="browser"&&r.frame.modern.stage=="cursor","browser: the fake screen is read as a knowledge browser");
        const int native_rows=r.ui.row_count();
        check(native_rows==17,"browser: only the drawn rows (1 group + 16 names) are rows at first, no action rows yet (got "+std::to_string(native_rows)+")");
        r.press(CursorNav::Down);   // 分類が 1 つだけなので、その下は名前の列の先頭へ進む
        check(r.fake.cur==0&&r.fake.column==1,"browser: Down at the only group moves to the first name");
        r.press(CursorNav::Left);   // 左右で列を替える
        check(r.fake.column==0,"browser: Left moves from the name column back to the group column");
        r.press(CursorNav::Right);
        check(r.fake.cur==0&&r.fake.column==1,"browser: Right moves to the name column again");
        int pressed=0;
        while(r.fake.cur<39&&pressed<200){r.press(CursorNav::Down);++pressed;}
        check(r.fake.cur==39,"browser: pressing Down reaches the last name that the original draws only after scrolling (cur="+std::to_string(r.fake.cur)+")");
        check(r.fake.top>0,"browser: the original scrolled its window");
        check(!r.on_action()&&r.ui.row_count()==17,"browser: no action rows while the end is not confirmed");
        check(r.ui.selected_row_id().find("Item 39")!=std::string::npos,"browser: the cursor row is the last name (id "+r.ui.selected_row_id()+")");
        r.press(CursorNav::Down);   // 本当の末尾を確かめる → 操作の行へ
        check(r.on_action(),"browser: at the true end the next Down moves to an action row (id "+r.ui.selected_row_id()+")");
        check(r.ui.row_count()>17,"browser: action rows appear only at the true end");
        const int actions=r.ui.row_count()-17;
        r.press(CursorNav::Down,actions-1);
        check(r.on_action(),"browser: Down walks through the action rows");
        r.press(CursorNav::Down);   // 最後の操作の行の下 → 先頭へ回り込む
        check(r.fake.cur==0&&r.fake.top==0,"browser: Down at the last action row wraps to the true first name (cur="+std::to_string(r.fake.cur)+")");
        check(r.ui.row_count()==17,"browser: action rows are hidden again once the list is scrolled back");
        check(r.fake.keys_seen<400,"browser: no runaway key traffic (keys="+std::to_string(r.fake.keys_seen)+")");
        // 先頭の上 → 末尾へ回り込み
        r.press(CursorNav::Up);   // 名前の列の先頭の上 → 末尾の操作の行へ
        check(r.fake.cur==39,"browser: Up at the true first row wraps to the true end (cur="+std::to_string(r.fake.cur)+")");
        check(r.on_action(),"browser: after wrapping to the end the cursor is on the last action row");
    }
    // ---- 一覧が窓に収まるブラウザ（スクロールしない） ----
    {
        Rig r(Fake{true,5,16});
        r.press(CursorNav::Down,5);
        check(r.fake.cur==4&&r.fake.top==0&&!r.on_action(),"short browser: Down reaches the last name without scrolling");
        r.press(CursorNav::Down);
        check(r.on_action(),"short browser: one more Down confirms the end and moves to the action row");
        check(r.fake.keys_seen<20,"short browser: few keys sent (keys="+std::to_string(r.fake.keys_seen)+")");
    }
    // ---- 選択肢のメニュー型（30 件・窓は 10 行・端で止まる） ----
    {
        Fake f;f.browser=false;f.total=30;f.visible=10;f.wrap=false;
        Rig r(f);
        check(r.frame.modern.stage=="choices"&&r.frame.modern.cursor==0,"menu: the fake screen is a cursor-driven menu");
        r.press(CursorNav::Down,29);
        check(r.fake.cur==29,"menu: Down reaches the 30th item that the original draws only after scrolling (cur="+std::to_string(r.fake.cur)+")");
        check(r.fake.top>0,"menu: the original scrolled its window");
        r.press(CursorNav::Down);
        check(r.fake.cur==0&&r.fake.top==0,"menu: Down at the true end wraps to the true first item (cur="+std::to_string(r.fake.cur)+")");
        r.press(CursorNav::Up);
        check(r.fake.cur==29,"menu: Up at the true first item wraps to the true last item (cur="+std::to_string(r.fake.cur)+")");
    }
    // ---- 原作が端で回り込むメニュー ----
    {
        Fake f;f.browser=false;f.total=30;f.visible=10;f.wrap=true;
        Rig r(f);
        r.press(CursorNav::Down,29);
        check(r.fake.cur==29,"wrapping menu: Down reaches the last item");
        const int before=r.requests;
        r.press(CursorNav::Down);
        check(r.fake.cur==0,"wrapping menu: the original wraps by itself");
        check(r.requests-before==1,"wrapping menu: a single request is enough (requests="+std::to_string(r.requests-before)+")");
        r.press(CursorNav::Up);
        check(r.fake.cur==29,"wrapping menu: Up wraps to the last item");
    }
    // ---- 窓に収まる短いメニュー（従来の回り込みと同じ結果） ----
    {
        Fake f;f.browser=false;f.total=4;f.visible=10;f.wrap=false;
        Rig r(f);
        r.press(CursorNav::Down,4);
        check(r.fake.cur==0,"short menu: Down past the last item wraps to the first");
        r.press(CursorNav::Up);
        check(r.fake.cur==3,"short menu: Up past the first item wraps to the last (cur="+std::to_string(r.fake.cur)+" keys="+std::to_string(r.fake.keys_seen)+" row="+r.ui.selected_row_id()+")");
    }
    // ---- ページ式の一覧（原作が 1 ページぶんだけ描き、空白・`-` で送る）のページ送りのキーの申告 ----
    {
        const auto keys_for=[&](std::vector<std::string> lines,int &prev,int &next) {
            GameFrame f;f.menu_open=true;
            int row=0;for(auto &text:lines){TermMirrorLine l;l.source_row=row++;l.text_utf8=std::move(text);f.menu_term_lines.push_back(std::move(l));}
            presentation::fill_paged_list_keys(f);prev=f.menu_page_prev_key;next=f.menu_page_next_key;
        };
        int prev=0,next=0;
        keys_for({"Select a building:"," a) Inn"," b) Armory"," -- more (1/2) --"},prev,next);
        check(prev=='-'&&next==' ',"paged list: the candidate selector's '-- more (1/2) --' declares '-' (previous) and ' ' (next)");
        keys_for({"List of all essences you have. 2/3"},prev,next);
        check(prev=='-'&&next==' ',"paged list: the smith's essence list (English) declares both keys");
        keys_for({"現在所持しているエッセンス 1/2"},prev,next);
        check(prev=='-'&&next==' ',"paged list: the smith's essence list (Japanese) declares both keys");
        keys_for({"現在所持しているエッセンス 1/1"},prev,next);
        check(prev==0&&next==0,"paged list: a single page declares nothing");
        keys_for({"(SPACE=next, ESC=exit) Add which ability? 1/2"},prev,next);
        check(prev==0&&next==' ',"paged list: the smith's ability list has no previous page, only ' ' (next)");
        keys_for({"(SPACEで次ページ, ESCで中断) どの能力を付加しますか？ 1/2"},prev,next);
        check(prev==0&&next==' ',"paged list: the smith's ability list (Japanese) declares only ' '");
        keys_for({"(Powers a-c, *=List, /=Browse, ESC=exit) Use which power? "},prev,next);
        check(prev==0&&next==' ',"paged list: the racial power prompt (non-menu) declares only ' '");
        keys_for({"(特殊能力 a-c, '*'で一覧, '/'で閲覧, ESCで中断) どの能力を使いますか？"},prev,next);
        check(prev==0&&next==' ',"paged list: the racial power prompt (Japanese) declares only ' '");
        keys_for({"Inventory"," a) a Potion"," b) a Scroll"},prev,next);
        check(prev==0&&next==0,"paged list: an ordinary list declares nothing");
        // 申告したキーを、画面のページ送り（PageDown・RB）が原作へ送る
        GameFrame f;f.menu_open=true;
        const std::vector<std::string> lines={"Select a building:"," a) Inn"," b) Armory"," -- more (1/2) --"};
        for(int i=0;i<int(lines.size());++i){TermMirrorLine l;l.source_row=i;l.text_utf8=lines[i];f.menu_term_lines.push_back(std::move(l));}
        for(int i=1;i<=2;++i){MenuChoice c;c.line_index=i;c.span_begin=1;c.span_len=int(lines[i].size())-1;c.key=i==1?'a':'b';f.menu_choices.push_back(c);}
        presentation::fill_paged_list_keys(f);
        presentation::NativeModernMenu menu;menu.set_enabled(true);menu.set_language("en");menu.capture(f);
        ModernStoreUi ui;ui.sync(f,true);
        check(f.modern.stage=="choices"&&f.modern.next_key==' '&&f.modern.prev_key=='-',"paged list: the window carries the declared page keys");
        presentation::InputEventsMessage out;ui.page(1,out);
        std::vector<int> sent;for(const auto &e:out.events)menu.input(e,sent);
        check(sent.size()==1&&sent[0]==' ',"paged list: PageDown / RB sends the original's next-page key");
        presentation::InputEventsMessage back;ModernStoreUi ui2;ui2.sync(f,true);ui2.page(-1,back);
        check(back.events.size()==1&&back.events[0].action=="document_page"&&back.events[0].number==0,"paged list: PageUp / LB sends the previous-page request");
    }
    // ---- 画面では隣の行へ動かすのに、原作のカーソルが何手も歩く窓（調-08・F-gensoband-02） ----
    {
        // (1) 歩いている間（返事待ち）に押した上下を捨てない。目標の行を付け替えて、最後に押した行へ着く
        WalkRig w;
        w.press(CursorNav::Down);                    // 行 0（位置 0）→ 行 1（位置 3）。原作は 3 手歩く
        check(w.ui.pending()&&w.ui.selected_row_id()=="slot:3","walk: Down aims at the next displayed row and waits for the core");
        w.press(CursorNav::Down);                    // 返事待ちの間の 2 回目。以前は捨てられた
        check(w.ui.selected_row_id()=="slot:1","walk: Down pressed while the walk is pending moves the target (id "+w.ui.selected_row_id()+")");
        w.settle();
        check(w.cur==1,"walk: the original cursor ends on the last row pressed, not the first (slot "+std::to_string(w.cur)+")");
        // (2) 返事の合間（返事が届いて次の 1 手を送る前）に押した上下も同じ
        WalkRig b;
        b.press(CursorNav::Down);b.core_reply();     // 返事は届いたが、次の 1 手はまだ送っていない
        b.press(CursorNav::Down);
        b.settle();
        check(b.cur==1&&b.ui.selected_row_id()=="slot:1","walk: Down pressed between steps also moves the target (slot "+std::to_string(b.cur)+")");
        // (3) 歩いている間の決定は預かり、着いてから 1 回だけ送る。途中の行を決定しない
        WalkRig c;
        c.press(CursorNav::Down);
        c.press(CursorNav::Confirm);
        c.settle();
        check(c.continues==1&&c.confirmed_slot==3,"walk: Confirm pressed during the walk is sent once, on the target row (continues "+std::to_string(c.continues)+", slot "+std::to_string(c.confirmed_slot)+")");
        // (4) 決定を預けた後の上下は無視する（決定は預けた行に効く）
        WalkRig d;
        d.press(CursorNav::Down);d.press(CursorNav::Confirm);d.press(CursorNav::Down);
        d.settle();
        check(d.continues==1&&d.confirmed_slot==3,"walk: Down after a held Confirm does not move the confirmed row");
        // (5) 窓が変わったら、預けた決定は新しい窓へ効かない
        WalkRig e;
        e.press(CursorNav::Down);e.press(CursorNav::Confirm);
        e.core_reply();
        e.title_changed();
        e.settle();
        check(e.continues==0,"walk: a held Confirm is dropped when the window changes (continues "+std::to_string(e.continues)+")");
        // (6) 返事が届いた回にすぐ次の 1 手を送る：1 手ごとに描画 1 回ぶん。以前は 2 回ぶん
        WalkRig f;
        f.press(CursorNav::Down);
        int ticks=0;while((!f.held.empty()||f.ui.pending())&&ticks<50){f.tick();++ticks;}
        check(f.cur==3&&ticks<=4,"walk: one native step per tick (3 steps took "+std::to_string(ticks)+" ticks)");
    }
    std::fprintf(stderr,"[scroll-list-check] %d checks, %d failures\n",checks,failures);
    std::printf("%s\n",failures?"RESULT: FAIL":"RESULT: PASS");
    return failures?1:0;
}
}
