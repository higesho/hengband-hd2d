// CPU-only tests. Link the real controller and codec; rendering is not exercised here.
#include "ui/modern_store_ui.h"
#include "assets/tile_catalog.h"
#include "ui/ui_paint.h"
#include "render/text_overlay.h"
#include "render/voxel_renderer.h"
#include "world/slab_library.h"
#include "i18n/lang.h"
#include "frame/modern_window_codec.h"
#include "frame/protocol_messages.h"
#include "bridge/native_store_menu.h"
#include "bridge/native_inventory_menu.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
namespace hd2d {
void UiPaint::panel(const RectPx &,const PanelStyle &){}
void UiPaint::frame(const RectPx &,const PaintColor &,int){}
void UiPaint::rect(const RectPx &,const PaintColor &){}
void UiPaint::surface(const RectPx &,const PaintColor &,UiMaterial){}
void UiPaint::rounded_frame(const RectPx &,const PaintColor &,int,int){}
void UiPaint::triangle(float,float,float,float,float,float,const PaintColor &){}
int TextOverlay::draw(int,int,std::string_view,const TextColor &,int){return 0;}
int TextOverlay::measure(std::string_view s){return static_cast<int>(s.size());}
std::size_t TextOverlay::fit_bytes(std::string_view s,int){return s.size();}
void TextOverlay::set_layout_scale(float){}
void VoxelRenderer::begin(const Mat4 &,const SceneLighting &,const Mat4 &,gl::GLuint,int){}
void VoxelRenderer::begin_motion(const WindParams &,float){}
void VoxelRenderer::set_look(const LookParams &){}
void VoxelRenderer::set_wear(const WearParams &){}
void VoxelRenderer::set_leaf(const LeafParams &){}
void VoxelRenderer::begin_cutaways(const Cutaway *,std::size_t){}
void VoxelRenderer::set_cutaway_scale(float){}
void VoxelRenderer::set_shadow_scale(float){}
void VoxelRenderer::draw_instanced(GpuPrefab &,const InstanceData *,std::size_t,const Mat4 *,std::size_t){}
int SlabLibrary::acquire(std::uint16_t,const std::string &,VoxelRenderer &){return -1;}
void UiImage::unload(){}
bool UiImage::load(const std::string &,std::string &){return false;}
void UiImagePainter::draw_cover(const UiImage &,const RectPx &,float) const {}
const TileEntry *TileCatalog::find(std::uint16_t) const {return nullptr;}
namespace i18n {std::string_view current(){return "en";}}
}
namespace hd2d::gl {
PFN_glGetIntegerv glGetIntegerv=nullptr;
PFN_glViewport glViewport=nullptr;
PFN_glClear glClear=nullptr;
PFN_glDisable glDisable=nullptr;
PFN_glDepthMask glDepthMask=nullptr;
}
int main() {
    int checks=0;
    auto check=[&](bool v,const char *s){if(!v){std::cerr<<"FAILED: "<<s<<std::endl;std::exit(1);}++checks;};
    check(hd2d::message_suffix_prefix_overlap({"old","meal","full"},{"meal","full","awake"})==2,
        "sliding native message windows identify only the newly appended result");
    GameFrame f;f.menu_open=true;f.modern.version=2;f.modern.revision=3;f.modern.stage="browse";
    f.modern.rows={{"item-a","A","1 kg",10,2,'a'},{"item-b","B","2 kg",5,9,'b'}};
    for(auto &row:f.modern.rows)row.actions.push_back({"buy","Buy","",true});
    ModernWindow decoded;
    check(decode_modern_window(encode_modern_window(f.modern),decoded),"roundtrip");
    f.modern.venue_header="General Store (10000)\nOwner (Human)";
    check(decode_modern_window(encode_modern_window(f.modern),decoded)&&decoded.venue_header==f.modern.venue_header,"venue header roundtrip");
    auto invalid_header=encode_modern_window(f.modern);invalid_header["venue_header"]=42;
    check(!decode_modern_window(invalid_header,decoded),"non-string venue header rejected");
    invalid_header["venue_header"]=std::string(4097,'x');
    check(!decode_modern_window(invalid_header,decoded),"oversized venue header rejected");
    auto bad=encode_modern_window(f.modern);bad["rows"][1]["id"]="item-a";
    check(!decode_modern_window(bad,decoded),"duplicate ids rejected");
    bad=encode_modern_window(f.modern);bad["revision"]=-1;
    check(!decode_modern_window(bad,decoded),"negative revision rejected");
    bad=encode_modern_window(f.modern);bad["stage"]="unknown";
    check(!decode_modern_window(bad,decoded),"unknown stage rejected");
    bad=encode_modern_window(f.modern);bad["rows"][0]["label"]=std::string(4097,'x');
    check(!decode_modern_window(bad,decoded),"oversized label rejected");
    using hd2d::CursorNav;hd2d::ModernStoreUi ui;presentation::InputEventsMessage out;
    ui.sync(f,false);check(!ui.active(),"original mode unchanged");ui.sync(f,true);check(ui.active(),"modern mode active");
    ui.handle(CursorNav::Down,out);ui.handle(CursorNav::Confirm,out);
    check(out.events.empty(),"row confirmation only opens operations");
    ui.cancel(out);check(out.events.empty(),"local cancel does not send escape");
    ui.handle(CursorNav::Confirm,out);ui.handle(CursorNav::Confirm,out);
    check(out.events.size()==1&&out.events[0].action=="buy"&&out.events[0].target=="item-b","target then buy");
    ui.handle(CursorNav::Confirm,out);ui.cancel(out);
    check(out.events.size()==1,"pending input cannot double submit or cancel purchase");
    f.modern.stage="choose";f.modern.revision=4;f.modern.request_id=1;f.modern.result="accepted";ui.sync(f,true);ui.advance(out);
    check(out.events.size()==2&&out.events.back().action=="select"&&out.events.back().target=="item-b","select only after choose response");
    ui.advance(out);check(out.events.size()==2,"select is sent once");
    f.modern.stage="number";f.modern.revision=5;f.modern.request_id=2;f.numeric.active=true;f.numeric.min=1;f.numeric.max=5;f.numeric.value=1;
    ui.sync(f,true);ui.text("999");ui.handle(CursorNav::Confirm,out);check(out.events.size()==2,"out of range stays local");
    ui.backspace();ui.backspace();ui.backspace();ui.text("3");ui.handle(CursorNav::Confirm,out);
    check(out.events.size()==3&&out.events.back().action=="number"&&out.events.back().number==3,"number commits one request");
    f.modern.revision=6;f.modern.request_id=3;f.numeric.value=3;
    ui.sync(f,true);ui.handle(CursorNav::Confirm,out);check(out.events.size()==3,"numeric echo is not transaction completion");
    f.modern.stage="confirm";f.modern.revision=6;f.modern.request_id=3;f.numeric.active=false;
    f.prompt.choices={{"Yes",'Y',0,1},{"No",'n',2,1}};ui.sync(f,false);check(ui.active(),"mode change deferred during confirmation");
    ui.handle(CursorNav::Right,out);ui.handle(CursorNav::Confirm,out);
    check(out.events.size()==4&&out.events.back().action=="answer"&&out.events.back().number==1,"answer selection");
    presentation::InputEventsMessage wire;std::string error;
    check(presentation::decode_input_events(presentation::encode_input_events(out),wire,error)&&wire.events.size()==4,"action codec roundtrip");
    check(wire.events.back().ui_revision==6&&wire.events.back().request_id==4,"codec keeps guard identity");
    auto oversized=nlohmann::json::parse(presentation::encode_input_events(out));
    oversized["events"]=nlohmann::json::array({oversized["events"][0]});
    oversized["events"][0]["request_id"]=4294967297ULL;
    check(presentation::decode_input_events(oversized.dump(),wire,error)&&wire.events.empty(),"request identity cannot wrap");
    f.modern.stage="";f.menu_open=false;f.prompt={};ui.sync(f,false);check(!ui.active()&&!ui.enabled(),"mode changes at safe boundary");
    // The target vanishes while the core is changing to its selection screen.
    hd2d::ModernStoreUi changed;f.menu_open=true;f.modern.stage="browse";f.modern.revision=10;f.modern.request_id=0;f.modern.result="";
    out.events.clear();changed.sync(f,true);changed.handle(CursorNav::Confirm,out);changed.handle(CursorNav::Confirm,out);
    f.modern.rows.erase(f.modern.rows.begin());f.modern.stage="choose";f.modern.revision=11;f.modern.request_id=1;f.modern.result="accepted";
    changed.sync(f,true);changed.advance(out);check(out.events.size()==1,"vanished target is never selected");
    // Added for the home/museum and child-dialog implementation; run with the next manual verification phase.
    for(const auto &kind: {std::string("home"),std::string("museum")}) {
        GameFrame home;home.menu_open=true;home.modern.version=3;home.modern.kind=kind;home.modern.stage="browse";home.modern.revision=100;
        ModernRow stock;stock.id="stock";stock.label="Stored item";stock.actions.push_back({kind=="home"?"take":"remove","Action","",true});
        home.modern.rows.push_back(stock);
        check(decode_modern_window(encode_modern_window(home.modern),decoded),"home/museum v3 roundtrip");
        hd2d::ModernStoreUi storage;presentation::InputEventsMessage events;storage.sync(home,true);
        storage.handle(CursorNav::Confirm,events);check(events.events.empty(),"storage opens local actions");
        storage.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action==(kind=="home"?"take":"remove"),"storage action preserves meaning");
        home.modern.kind="dialog";home.modern.stage="choices";home.modern.rows.clear();
        ModernRow replacement;replacement.id="replacement";replacement.label="Replacement slot";replacement.key='b';home.modern.rows.push_back(replacement);
        home.modern.revision=101;home.modern.request_id=1;home.modern.result="accepted";
        storage.sync(home,true);storage.advance(events);
        check(events.events.size()==1,"second selection is not auto-confirmed with first target");
        storage.handle(CursorNav::Confirm,events);
        check(events.events.size()==2&&events.events.back().action=="choice"&&events.events.back().target=="replacement","child choice requires user confirmation");
    }
    GameFrame dialog;dialog.menu_open=true;dialog.modern.version=3;dialog.modern.kind="dialog";dialog.modern.stage="cursor";dialog.modern.revision=200;dialog.modern.continue_key=13;
    hd2d::ModernStoreUi child;presentation::InputEventsMessage child_events;child.sync(dialog,true);
    child.handle(CursorNav::None,child_events);check(child_events.events.empty(),"unmapped key does not move child cursor");
    child.handle(CursorNav::Right,child_events);
    check(child_events.events.size()==1&&child_events.events[0].action=="navigate"&&child_events.events[0].number==6,"special cursor without list rows keeps native horizontal navigation");
    {GameFrame map;map.menu_open=true;map.modern.version=5;map.modern.kind="dialog";map.modern.stage="detail";map.modern.content_type="map";map.modern.continue_key=13;map.modern.revision=201;
     hd2d::ModernStoreUi screen;presentation::InputEventsMessage events;screen.sync(map,true);screen.handle(CursorNav::Confirm,events);
     check(events.events.size()==1&&events.events[0].action=="continue","map confirmation returns through the native close operation");}
    bad=encode_modern_window(dialog.modern);bad["continue_key"]=256;
    check(!decode_modern_window(bad,decoded),"out of range child key rejected");
    {
        GameFrame roll;roll.menu_open=true;roll.pre_game_menu=true;roll.modern.version=5;roll.modern.kind="dialog";roll.modern.stage="cursor";
        roll.modern.content_type="selection";roll.modern.continue_key=13;roll.modern.actions={{"key:83","Start over","",true,'S'},{"key:81","Quit","",true,'Q'}};
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(roll,true);picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action=="continue","character roll screen puts native Enter before Start over and Quit");
    }
    {
        presentation::NativeStoreMenu adapter;adapter.set_enabled(true,false,"generic");
        GameFrame store;store.menu_open=true;
        TermMirrorLine line;line.text_utf8="[Space] Next page";store.menu_term_lines.push_back(line);
        MenuChoice choice;choice.key=32;choice.line_index=0;choice.span_len=16;store.menu_choices.push_back(choice);
        native_store_info info{};info.active=1;info.kind=1;info.root_frame=1;info.page_size=12;info.next_key=32;
        adapter.capture(store,info,{});
        presentation::InputEventWire request;request.e="ui_action";request.action="command:32";request.target="store:commands";
        request.ui_revision=store.modern.revision;request.request_id=1;std::vector<int> keys;
        adapter.input(request,keys);check(keys==std::vector<int>{32},"adapter accepts first page command");
        adapter.capture(store,info,{});request.request_id=2;keys.clear();adapter.input(request,keys);
        check(keys==std::vector<int>{32},"adapter accepts fresh page command after unchanged root screen");
        keys.clear();adapter.input(request,keys);check(keys.empty(),"adapter ignores duplicate page request");
        adapter.set_enabled(true,false,"frox");adapter.capture(store,info,{});
        check(store.modern.rows.empty(),"Frox omits redundant native page controls");
        store.menu_choices[0].key=63;store.menu_term_lines[0].text_utf8="[?] Help";store.menu_choices[0].span_len=8;
        adapter.capture(store,info,{});
        check(store.modern.rows.size()==1&&store.modern.rows[0].id=="store:command:63","Frox exposes remaining commands without a submenu");
        ModernRow product;product.id="product";product.label="Food";product.count=4;
        store.prompt.text_utf8="Purchase completed.\nYour pack has been updated. -more-";
        adapter.capture(store,info,{product});
        check(store.modern.kind=="store"&&store.modern.stage=="message"&&store.modern.rows.size()==1,"store notification retains the item list");
        check(store.modern.input.find("Your pack has been updated.")!=std::string::npos,"store notification retains all message lines");
        check(decode_modern_window(encode_modern_window(store.modern),decoded),"store message state roundtrip");
        request.request_id=3;request.action="continue";request.target="";request.ui_revision=store.modern.revision;keys.clear();adapter.input(request,keys);
        check(keys==std::vector<int>{32},"store notification continues through the native acknowledgement");
    }
    for(bool changed:{false,true}) {
        GameFrame shop;shop.menu_open=true;shop.modern.version=5;shop.modern.stage="browse";shop.modern.revision=500;
        ModernRow commands;commands.id="store:commands";commands.label="Commands";
        commands.actions.push_back({"command:32","Next page","",true,32});shop.modern.rows.push_back(commands);
        hd2d::ModernStoreUi ui;presentation::InputEventsMessage ev;ui.sync(shop,true);
        ui.handle(CursorNav::Confirm,ev);ui.handle(CursorNav::Confirm,ev);
        check(ev.events.size()==1&&ev.events.back().action=="command:32","store page command sent");
        shop.modern.request_id=ev.events.back().request_id;shop.modern.result="accepted";
        if(changed)++shop.modern.revision;
        ui.sync(shop,true);ui.cancel(ev);
        check(ev.events.size()==2&&ev.events.back().action=="cancel","same-stage store acknowledgement clears wait even on single page");
    }
    {
        GameFrame stats;stats.menu_open=true;stats.modern.version=5;stats.modern.kind="dialog";stats.modern.stage="choices";
        stats.modern.content_type="stat_allocation";stats.modern.subtitle="Points spent / limit: 30 / 30";
        for(char key:std::string("sSiIwWdDcCrRn?")){ModernRow r;r.id=std::string(1,key);r.label=r.id;r.key=key;stats.modern.rows.push_back(r);}
        hd2d::ModernStoreUi ui;presentation::InputEventsMessage ev;ui.sync(stats,true);
        ui.handle(CursorNav::Right,ev);ui.handle(CursorNav::Confirm,ev);
        check(ev.events.size()==1&&ev.events.back().target=="S","right first moves from minus to the plus column");
        stats.modern.request_id=ev.events.back().request_id;stats.modern.result="accepted";ui.sync(stats,true);
        ev.events.clear();ui.handle(CursorNav::Right,ev);ui.handle(CursorNav::Confirm,ev);
        check(ev.events.size()==1&&ev.events.back().target=="S","right at the plus edge stays in the left control list");
        stats.modern.request_id=ev.events.back().request_id;ui.sync(stats,true);ev.events.clear();
        ui.handle(CursorNav::Down,ev);ui.handle(CursorNav::Confirm,ev);
        check(ev.events.back().target=="I","vertical movement retains the selected plus column");
        stats.modern.request_id=ev.events.back().request_id;ui.sync(stats,true);
        for(int i=0;i<5;++i)ui.handle(CursorNav::Down,ev);
        ui.handle(CursorNav::Confirm,ev);
        check(ev.events.back().target=="n","other commands follow all six attributes");
    }
    for(const std::string profile:{"frox","legacy"}) {
        GameFrame building;ModernWindow w;w.stage="choices";
        TermMirrorLine heading;heading.source_row=profile=="frox"?3:2;heading.text_utf8="Owner (Human)           Inn";building.menu_term_lines.push_back(heading);
        TermMirrorLine option;option.source_row=19;option.text_utf8="a) Rest";building.menu_term_lines.push_back(option);
        TermMirrorLine footer;footer.source_row=23;footer.text_utf8="ESC) Exit building";building.menu_term_lines.push_back(footer);
        MenuChoice c;c.key='a';c.line_index=1;building.menu_choices.push_back(c);
        presentation::modern_building_heading(building,w,profile);
        check(w.venue_header=="Owner (Human)\nInn","native building owner and type become fixed heading");
        w.venue_header.clear();building.menu_term_lines.back().text_utf8="ESC) Exit";
        presentation::modern_building_heading(building,w,profile);
        check(w.venue_header.empty(),"unrelated menus do not acquire a building header");
    }
    {
        ModernWindow previous;previous.stage="choices";previous.venue_header="Owner\nInn";ModernBlock result;result.kind="paragraph";result.cells.push_back({"","A useful rumour",false});previous.content.push_back(result);
        ModernWindow current;current.stage="choices";current.venue_header=previous.venue_header;ModernBlock gold;gold.kind="fields";gold.cells.push_back({"Gold","99",false});current.content.push_back(gold);
        presentation::modern_retain_building_result(current,previous);
        check(current.content.size()==2&&current.content.front().cells.front().text=="A useful rumour","facility result remains in the right pane when the native redraw contains only gold");
    }
    {
        GameFrame menu;menu.menu_open=true;menu.modern.version=5;menu.modern.revision=9;menu.modern.kind="dialog";menu.modern.stage="choices";menu.modern.title="Spells";menu.modern.can_preview=true;
        menu.modern.rows={{"a","Pulse A","",0,0,'a'},{"b","Pulse B","",0,0,'b'}};
        hd2d::ModernStoreUi preview_ui;presentation::InputEventsMessage events;preview_ui.sync(menu,true);preview_ui.advance(events);
        check(events.events.size()==1&&events.events[0].action=="preview"&&events.events[0].target=="a","opening a preview-capable menu reports its local focus without committing");
        preview_ui.advance(events);check(events.events.size()==1,"unchanged local preview focus is not repeatedly sent");
        preview_ui.handle(CursorNav::Down,events);preview_ui.advance(events);preview_ui.handle(CursorNav::Confirm,events);
        check(events.events.size()==3&&events.events[1].action=="preview"&&events.events[1].target=="b"
            &&events.events[2].action=="choice"&&events.events[2].target=="b","read-only preview requests do not swallow navigation or confirmation");
        menu.modern.preview_target="b";
        check(decode_modern_window(encode_modern_window(menu.modern),decoded)&&decoded.can_preview&&decoded.preview_target=="b","menu preview focus roundtrips separately from the native cursor");
        menu.modern.preview_target="unknown";
        check(!decode_modern_window(encode_modern_window(menu.modern),decoded),"preview cannot name a row outside the captured menu");
    }
    {
        presentation::NativeModernMenu native;native.set_enabled(true);
        GameFrame base;base.menu_open=true;
        for(const char *line:{"Spells","a) Pulse A","b) Pulse B"}){TermMirrorLine row;row.text_utf8=line;row.source_row=int(base.menu_term_lines.size());base.menu_term_lines.push_back(row);}
        for(int i=1;i<=2;++i){MenuChoice choice;choice.key='a'+i-1;choice.line_index=i;choice.label_utf8=i==1?"Pulse A":"Pulse B";base.menu_choices.push_back(choice);}
        auto frame=base;native.capture(frame);const int revision=frame.modern.revision;const auto id=frame.modern.rows[1].id;
        presentation::InputEventWire request;request.e="ui_action";request.action="preview";request.target=id;request.ui_revision=revision;request.request_id=1;
        std::vector<int> keys;native.input(request,keys);
        check(keys.empty()&&native.result()=="accepted","native preview input produces no gameplay key");
        frame=base;native.capture(frame);
        check(frame.modern.revision==revision&&frame.modern.preview_target==id,"read-only focus does not invalidate a concurrent ordinary confirmation");
        request.action="choice";request.request_id=2;native.input(request,keys);
        check(keys==std::vector<int>{'b'},"ordinary confirmation still invokes its native key exactly once after a preview");
    }
    {
        presentation::NativeModernMenu native;native.set_enabled(true);native.set_title("Character creation");
        GameFrame growing;growing.menu_open=true;TermMirrorLine first;first.source_row=5;first.text_utf8="Into the darkness";growing.menu_term_lines.push_back(first);
        native.capture(growing);const int pressed_revision=growing.modern.revision;
        growing.modern={};growing.menu_term_lines.front().text_utf8="Into the darkness, they descended";native.capture(growing);
        check(growing.modern.revision>pressed_revision&&growing.modern.stage=="detail","a growing native narrative publishes its newly painted text");
        presentation::InputEventWire request;request.e="ui_action";request.action="continue";request.ui_revision=pressed_revision;request.request_id=1;
        std::vector<int> keys;native.input(request,keys);
        check(keys==std::vector<int>{13}&&native.result()=="accepted","Continue remains valid while the same native narrative grows");
        growing.modern={};native.capture(growing);keys.clear();request.request_id=2;native.input(request,keys);
        check(keys.empty()&&native.result()=="rejected","an accepted narrative action closes the stale Continue scope");
        growing.modern={};growing.menu_term_lines.front().text_utf8="A different document";native.capture(growing);
        keys.clear();request.request_id=3;request.ui_revision=pressed_revision;native.input(request,keys);
        check(keys.empty()&&native.result()=="rejected","replacing a detail body rejects input from the prior document");
    }
    {
        GameFrame narrative;narrative.menu_open=true;narrative.modern.version=5;narrative.modern.kind="dialog";narrative.modern.stage="detail";
        narrative.modern.content_type="document";narrative.modern.title="Character creation";narrative.modern.continue_key=13;narrative.modern.revision=41;
        ModernBlock prose;prose.kind="paragraph";prose.cells.push_back({"","Into the darkness",false});narrative.modern.content.push_back(std::move(prose));
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(narrative,true);picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action=="continue","a narrative document with no command list exposes Continue directly");
    }
    {
        GameFrame message;message.modern.version=5;message.modern.kind="dialog";message.modern.stage="detail";
        message.modern.content_type="message";message.modern.title="Message";message.modern.continue_key=' ';message.modern.message_only=true;message.modern.revision=42;
        message.modern.body=reinterpret_cast<const char*>(u8"敵を倒した。［続く］");
        for(const auto nav:{CursorNav::Up,CursorNav::Down,CursorNav::Left,CursorNav::Right}) {
            hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(message,true);picker.handle(nav,events);
            check(events.events.size()==1&&events.events[0].action=="continue","direction input advances a visible combat/message Continue prompt");
        }
        message.modern.body=reinterpret_cast<const char*>(u8"スペースで続ける");
        message.modern.continue_key=' ';message.modern.revision=43;
        hd2d::ModernStoreUi ordinary;presentation::InputEventsMessage ordinary_events;ordinary.sync(message,true);ordinary.handle(CursorNav::Left,ordinary_events);
        check(ordinary_events.events.empty(),"direction input is not converted to Continue when [続く] is not actually displayed");
    }
    {
        GameFrame poker;poker.menu_open=true;poker.menu_term_curs_row=16;poker.menu_term_curs_col=38;
        for(int y=0;y<17;++y){TermMirrorLine r;r.source_row=y;r.text_utf8=std::string(80,' ');poker.menu_term_lines.push_back(std::move(r));}
        poker.menu_term_lines[0].text_utf8="Keep which cards (direction keys move; space selects)?";
        for(int i=0;i<5;++i){poker.menu_term_lines[5].text_utf8.replace(i*16,16," +------------+ ");poker.menu_term_lines[6].text_utf8.replace(i*16+2,12,"A           ");poker.menu_term_lines[14].text_utf8.replace(i*16+5,6," Stay ");}
        poker.menu_term_lines[16].text_utf8.replace(38,4,"Sure");
        ModernWindow w;w.version=5;w.kind="dialog";w.stage="cursor";w.page_size=12;
        presentation::interpret_modern_content(poker,w,"heng");
        check(w.content_type=="poker"&&w.rows.size()==6&&w.content.front().cells.size()==5&&std::count(w.content.front().cells[0].text.begin(),w.content.front().cells[0].text.end(),'\n')==6,"poker retains five cards and their seven original rows");
        check(decode_modern_window(encode_modern_window(w),decoded)&&presentation::modern_focus_key(w,"poker:0")=='8'&&w.cursor_target=="poker:done","poker geometry and native selection survive transport");
    }
    {
        GameFrame options;options.menu_open=true;options.pre_game_menu=true;options.menu_term_curs_row=1;
        for(const auto &source:std::initializer_list<std::pair<int,const char *>>{{0,"Initial options (Return:next, y/n:change, ESC:done, ?:help)"},{2,"Smart monsters : yes (smart_learn)"},{3,"Preserve artifacts : no (preserve_mode)"}}) {
            TermMirrorLine line;line.source_row=source.first;line.text_utf8=source.second;options.menu_term_lines.push_back(std::move(line));
        }
        options.menu_core_cursor.line_index=1;
        ModernWindow w;w.version=5;w.kind="dialog";w.stage="cursor";
        presentation::interpret_modern_content(options,w,"heng");
        check(w.content_type=="settings_list"&&w.rows.size()==3&&w.rows.back().id=="@action:accept-birth-options"
            &&w.rows.back().key==27&&w.rows.back().label=="Confirm these options","birth options expose native Escape as a final explicit confirmation row");
        check(decode_modern_window(encode_modern_window(w),decoded),"synthetic birth-options confirmation survives the production decoder limits");
        check(std::none_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.key==13;})
            &&std::any_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.id=="accept-birth-options"&&a.key==27;}),"birth options remove the misleading next-option command");
        GameFrame presented=options;presented.modern=w;hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(presented,true);
        check(picker.take_scope_changed()&&!picker.take_scope_changed(),"a new semantic menu scope is reported once");
        picker.handle(CursorNav::Up,events);
        picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action=="window_action"&&events.events[0].target=="accept-birth-options","the final birth-options row confirms through the native Escape transition");
        options.pre_game_menu=false;w={};w.version=5;w.kind="dialog";w.stage="cursor";
        presentation::interpret_modern_content(options,w,"heng");
        check(w.content_type=="settings_list"&&w.rows.size()==2&&std::any_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.key==13;}),"ordinary settings retain native next-option control");
    }
    {
        GameFrame options;options.menu_open=true;options.menu_term_curs_row=0;
        for(const auto *text:{"Terse output (\"terse\"): True","Follow turnings (\"passgo\"): False"}) {
            TermMirrorLine line;line.source_row=int(options.menu_term_lines.size());line.text_utf8=text;options.menu_term_lines.push_back(std::move(line));
        }
        options.menu_core_cursor.line_index=0;options.menu_core_cursor.key=13;options.menu_core_cursors={options.menu_core_cursor};
        ModernWindow w;w.version=5;w.kind="dialog";w.stage="cursor";w.language="ja";
        presentation::interpret_modern_content(options,w,"rogue");
        check(w.content_type=="settings_list_rogue"&&w.title==reinterpret_cast<const char*>(u8"設定")&&w.rows.size()==2&&w.rows[0].id=="rogue-option:terse"
            &&w.rows[1].id=="rogue-option:passgo","Rogue boolean options become one cursor-driven settings list");
        check(std::any_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.id=="rogue-setting-on"&&a.key=='1';})
            &&std::any_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.id=="rogue-setting-off"&&a.key=='0';}),"Rogue True/False settings expose stay-on-row HD2D toggle actions");
        GameFrame presented=options;presented.modern=w;hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(presented,true);
        check(std::any_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.id=="rogue-setting-toggle"&&a.key==5;}),
            "Rogue settings expose the confirm toggle action (Ctrl-E)");
        picker.handle(CursorNav::Right,events);picker.handle(CursorNav::Left,events);
        check(events.events.empty(),"Rogue settings do not change values with Left/Right");
        picker.toggle_commands();
        check(picker.status().find(":commands")==std::string::npos,
            "Rogue settings do not expose their internal ON/OFF actions as a second command screen");
        picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action=="window_action"&&events.events[0].target=="rogue-setting-toggle",
            "Rogue settings toggle with Confirm using the stay-on-row action");
    }
    {
        GameFrame options;options.menu_open=true;options.menu_term_curs_row=0;
        for(const auto *text:{"Ask whether quit (\"askquit\"): Yes","Follow passages (\"passgo\"): No"}) {
            TermMirrorLine line;line.source_row=int(options.menu_term_lines.size());line.text_utf8=text;options.menu_term_lines.push_back(std::move(line));
        }
        options.menu_core_cursor.line_index=0;options.menu_core_cursor.key=13;options.menu_core_cursors={options.menu_core_cursor};
        ModernWindow w;w.version=5;w.kind="dialog";w.stage="cursor";w.language="ja";
        presentation::interpret_modern_content(options,w,"rogue");
        check(w.content_type=="settings_list_rogue"&&std::any_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.id=="rogue-setting-on"&&a.key=='1';})
            &&std::any_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.id=="rogue-setting-off"&&a.key=='0';}),"RogueClone2 Yes/No settings expose stay-on-row HD2D toggle actions");
        GameFrame back_presented=options;back_presented.modern=w;hd2d::ModernStoreUi back_picker;presentation::InputEventsMessage back_events;back_picker.sync(back_presented,true);back_picker.cancel(back_events);
        check(back_events.events.size()==1&&back_events.events[0].action=="cancel",
            "RogueClone2 settings expose Cancel as a native back request");
        GameFrame presented=options;presented.modern=w;hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;
        picker.sync(presented,true);picker.handle(CursorNav::Left,events);
        check(events.events.empty(),"RogueClone2 settings do not change values with Left");
        picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action=="window_action"&&events.events[0].target=="rogue-setting-toggle",
            "RogueClone2 settings toggle with Confirm using the stay-on-row action");
        hd2d::ModernStoreUi tabs_picker;
        tabs_picker.set_parent_command_tabs({"move","settings","features"},1);tabs_picker.sync(presented,true);
        check(tabs_picker.parent_tab_navigation_active(),"Rogue settings retain their parent command-tab context");
        tabs_picker.tab(1);
        check(tabs_picker.take_parent_tab_request()<0,"Rogue parent tab does not reopen before the native settings editor closes");
        GameFrame closed;closed.modern={};tabs_picker.sync(closed,true);
        check(tabs_picker.take_parent_tab_request()==2,"Rogue parent tab reopens only after the child settings editor has closed");

        // 2026-09-23「右 Shift が右タブに遷移できないことがある」。
        // (1) 「設定」タブを選んだ直後、原作の設定画面がまだ届いていない間に押した。
        hd2d::ModernStoreUi early;presentation::InputEventsMessage early_events;
        early.set_parent_command_tabs({"move","settings","features"},1);
        check(early.awaiting_parent_child(),"Rogue settings tab waits for the native editor before it is shown");
        early.tab(1);early.advance(early_events);
        check(early_events.events.empty(),"a tab step before the settings arrive is held, not sent");
        early.sync(presented,true);early.advance(early_events);
        check(early_events.events.size()==1&&early_events.events[0].action=="cancel",
            "a tab step pressed before the settings arrived is applied once they arrive");
        // (2) 値を変えた応答を待っている間に押した。
        hd2d::ModernStoreUi busy;presentation::InputEventsMessage busy_events;
        busy.set_parent_command_tabs({"move","settings","features"},1);busy.sync(presented,true);
        busy.handle(CursorNav::Confirm,busy_events);
        check(busy_events.events.size()==1&&busy_events.events[0].action=="window_action","settings toggle is in flight");
        const int in_flight=busy_events.events[0].request_id;busy_events.events.clear();
        busy.tab(1);busy.advance(busy_events);
        check(busy_events.events.empty(),"a tab step during a pending toggle is held");
        GameFrame answered=presented;answered.modern.revision=presented.modern.revision+1;
        answered.modern.request_id=in_flight;answered.modern.result="accepted";
        busy.sync(answered,true);busy.advance(busy_events);
        check(busy_events.events.size()==1&&busy_events.events[0].action=="cancel",
            "a tab step pressed during a pending toggle is applied after the answer");
    }
    {
        // RogueClone2 のカーソル行の '>' と、想定外のキーで原作が書き足すヒント。
        GameFrame options;options.menu_open=true;options.menu_term_curs_row=0;
        for(const auto *text:{">Ask whether quit (\"askquit\"): Yes  (Yes or No)","Follow passages (\"passgo\"): No"}) {
            TermMirrorLine line;line.source_row=int(options.menu_term_lines.size());line.text_utf8=text;options.menu_term_lines.push_back(std::move(line));
        }
        options.menu_core_cursor.line_index=0;options.menu_core_cursor.key=13;options.menu_core_cursors={options.menu_core_cursor};
        ModernWindow w;w.version=5;w.kind="dialog";w.stage="cursor";w.language="ja";
        presentation::interpret_modern_content(options,w,"rogue");
        check(w.content_type=="settings_list_rogue"&&w.rows.size()==2&&w.cursor_target=="rogue-option:askquit",
            "Rogue settings keep the cursor row when the native editor appends an input hint");
        check(w.rows[0].label=="Ask whether quit","RogueClone2 cursor marker is not part of the option label");
        GameFrame hinted;hinted.menu_open=true;hinted.menu_term_curs_row=0;
        for(const auto *text:{"Terse output: True      (T or F)","Jump: False"}) {
            TermMirrorLine line;line.source_row=int(hinted.menu_term_lines.size());line.text_utf8=text;hinted.menu_term_lines.push_back(std::move(line));
        }
        hinted.menu_core_cursor.line_index=0;hinted.menu_core_cursor.key=13;hinted.menu_core_cursors={hinted.menu_core_cursor};
        ModernWindow hw;hw.version=5;hw.kind="dialog";hw.stage="cursor";hw.language="ja";
        presentation::interpret_modern_content(hinted,hw,"rogue");
        check(hw.content_type=="settings_list_rogue"&&hw.rows.size()==2&&hw.cursor_target=="rogue-option:0",
            "Rogue 3.6 settings keep the cursor row when get_bool appends (T or F)");

        presentation::NativeModernMenu native;native.set_enabled(true);native.set_language("ja");native.set_document_profile("rogue");
        GameFrame live=options;native.capture(live);
        check(live.modern.content_type=="settings_list_rogue","native Rogue settings frame is recognised");
        presentation::InputEventWire key;key.e="ui_action";key.action="native_key";key.number='y';key.request_id=1;key.ui_revision=live.modern.revision;
        std::vector<int> keys;native.input(key,keys);
        check(keys.empty(),"Rogue settings do not forward letter keys that make the native editor overwrite the value");
    }
    {
        GameFrame passive;passive.menu_open=true;passive.modern.version=5;passive.modern.kind="dialog";
        passive.modern.stage="detail";passive.modern.continue_key=' ';passive.modern.revision=45;
        hd2d::ModernStoreUi detail;presentation::InputEventsMessage events;detail.sync(passive,true);detail.cancel(events);
        check(events.events.size()==1&&events.events[0].action=="cancel",
            "passive Rogue detail Cancel reaches the adapter so it can satisfy Space-only waits");
    }
    {
        GameFrame frox;frox.pre_game_menu=true;ModernWindow w;w.language="ja";w.stage="choices";
        const std::vector<int> native_order={'n','*','s','?','r',9,'c','g','=',13,27};
        for(int key:native_order){ModernRow row;row.id="frox:"+std::to_string(key);row.key=row.hotkey=key;row.label="old";row.actions={{"choice","old","",true,key}};w.rows.push_back(std::move(row));}
        const std::string sheet=reinterpret_cast<const char*>(u8"名前: PLAYER\n種族: 人間\n職業: 戦士\n");
        presentation::modern_frox_birth_summary(frox,w,sheet,"frox");
        const std::vector<int> expected={'n','*','s','r','c','g','=', '?',13,27};
        std::vector<int> actual;for(const auto &row:w.rows)actual.push_back(row.key);
        check(actual==expected&&w.rows[5].label==reinterpret_cast<const char*>(u8"ゲーム速度を変える")
            &&w.rows[6].label==reinterpret_cast<const char*>(u8"設定"),"Frox creation summary uses the requested single-column command order");
    }
    {
        presentation::NativeModernMenu native;native.set_enabled(true);native.set_language("ja");
        GameFrame select;select.menu_open=true;select.pre_game_menu=true;
        TermMirrorLine title;title.source_row=0;title.text_utf8=reinterpret_cast<const char*>(u8"種族を選ぶ");select.menu_term_lines.push_back(title);
        MenuChoice race;race.key='a';race.line_index=0;race.label_utf8=reinterpret_cast<const char*>(u8"人間");select.menu_choices.push_back(race);
        native.capture(select);
        GameFrame confirm;confirm.menu_open=true;confirm.pre_game_menu=true;confirm.prompt.text_utf8=reinterpret_cast<const char*>(u8"よろしいですか？");
        confirm.prompt.choices={{reinterpret_cast<const char*>(u8"はい"),'y',0,1},{reinterpret_cast<const char*>(u8"いいえ"),'n',2,1}};
        native.capture(confirm);
        check(confirm.modern.stage=="confirm"&&confirm.modern.auto_select=="@answer:0","race confirmation is marked for immediate affirmative input");
        check(decode_modern_window(encode_modern_window(confirm.modern),decoded)&&decoded.auto_select=="@answer:0","automatic confirmation survives protocol validation");
        confirm.modern={};native.capture(confirm);
        check(confirm.modern.auto_select=="@answer:0","automatic confirmation remains present on repeated native frames until consumed");
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(confirm,true);picker.advance(events);
        check(events.events.size()==1&&events.events[0].action=="answer"&&events.events[0].number==0,"race confirmation advances without a visible user step");
    }
    {
        presentation::NativeModernMenu native;native.set_enabled(true);native.set_language("ja");
        GameFrame pick;pick.menu_open=true;TermMirrorLine prompt;prompt.source_row=0;prompt.text_utf8=reinterpret_cast<const char*>(u8"4/6: 持ち物 or 装備品");pick.menu_term_lines.push_back(prompt);
        for(int key:{'4','6'}){MenuChoice choice;choice.key=key;choice.line_index=0;choice.label_utf8=key=='4'?"or "+std::string(reinterpret_cast<const char*>(u8"装備品")):reinterpret_cast<const char*>(u8"持ち物");pick.menu_choices.push_back(choice);}
        ModernRow item;item.id="item";item.label=reinterpret_cast<const char*>(u8"鑑定する品");item.key=13;
        presentation::NativeItemRow wrapped{item,1,1,3};native.capture(pick,{}, {wrapped});
        check(pick.modern.actions.size()==2&&std::none_of(pick.modern.actions.begin(),pick.modern.actions.end(),[](const auto &action){return action.label.find("or ")!=std::string::npos;}),"item source navigation suppresses duplicate native prompt fragments");
    }
    {
        presentation::NativeModernMenu native;native.set_enabled(true);native.set_language("ja");
        GameFrame pick;pick.menu_open=true;pick.menu_core_cursor={1,11,40};
        for(const auto &source:std::initializer_list<std::pair<int,const char *>>{{0,reinterpret_cast<const char*>(u8"(持ち物: ESC) どのアイテムを鑑定しますか?")},{1,reinterpret_cast<const char*>(u8"           》 ? 2巻の未鑑定の巻物                     0.5 kg")},{2,reinterpret_cast<const char*>(u8"              = 未鑑定の指輪                         0.2 kg")}}) {
            TermMirrorLine line;line.source_row=source.first;line.text_utf8=source.second;pick.menu_term_lines.push_back(std::move(line));
        }
        native.capture(pick);
        check(pick.modern.content_type=="items"&&pick.modern.rows.size()==2&&pick.modern.cursor==0
            &&pick.modern.rows[0].glyph=="?"&&pick.modern.rows[1].glyph=="=","cursor-only building inventory becomes selectable icon rows");
        presentation::InputEventWire move;move.e="ui_action";move.action="navigate";move.number=2;move.request_id=1;move.ui_revision=pick.modern.revision;
        std::vector<int> keys;native.input(move,keys);check(keys.size()==1&&keys[0]=='2',"cursor-only building inventory follows native selection movement");
    }
    {
        GameFrame list;list.menu_open=true;list.modern.version=5;list.modern.kind="dialog";list.modern.stage="choices";list.modern.title="First";
        for(int i=0;i<3;++i){ModernRow row;row.id="row-"+std::to_string(i);row.label=row.id;row.key='a'+i;list.modern.rows.push_back(row);}
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(list,true);picker.take_scope_changed();
        picker.handle(CursorNav::Up,events);picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].target=="row-2","ordinary choice lists wrap from the first row to the last");
        list.modern.request_id=events.events[0].request_id;list.modern.result="accepted";picker.sync(list,true);events.events.clear();
        list.modern.title="Second";picker.sync(list,true);check(picker.take_scope_changed(),"a title transition clears held navigation for the next birth page");
    }
    {
        GameFrame sil;sil.menu_open=true;sil.modern.version=5;sil.modern.kind="dialog";sil.modern.stage="cursor";sil.modern.content_type="sil_allocation";
        for(int i=0;i<8;++i){ModernRow row;row.id="allocation:"+std::to_string(i);row.label=row.id;row.key=i%2?6:4;sil.modern.rows.push_back(row);}
        ModernRow done;done.id="allocation:continue";done.label="Continue";done.key=13;sil.modern.rows.push_back(done);sil.modern.cursor_target="allocation:0";
        sil.modern.actions.push_back({"key:113","Stop character creation","",true,'q'});
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(sil,true);picker.handle(CursorNav::Up,events);
        check(events.events.empty(),"Sil Continue is local because the native allocation cursor has no Continue position");
        picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action=="choice"&&events.events[0].target=="allocation:continue","Sil allocation keeps global actions out of the paired list and confirms Continue directly");
    }
    {
        GameFrame sheet;sheet.menu_open=true;sheet.pre_game_menu=true;sheet.modern.version=5;sheet.modern.kind="dialog";sheet.modern.stage="detail";sheet.modern.content_type="character";sheet.modern.continue_key=13;
        sheet.modern.actions.push_back({"key:113","Stop character creation","",true,'q'});ModernBlock history;history.kind="paragraph";history.cells.push_back({"","Character history",false});sheet.modern.content.push_back(history);
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(sheet,true);picker.handle(CursorNav::Confirm,events);
        check(events.events.size()==1&&events.events[0].action=="continue","Sil character review confirms forward instead of selecting the global quit action");
    }
    {
        GameFrame store;store.menu_open=true;store.modern.version=5;store.modern.kind="store";store.modern.stage="browse";store.modern.page_size=12;
        for(int i=0;i<3;++i){ModernRow row;row.id="stock:"+std::to_string(i);row.label=row.id;row.tab=0;row.slot=i;row.available=true;row.actions={{"buy","Buy","",true,'p'}};store.modern.rows.push_back(std::move(row));}
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(store,true);picker.handle(CursorNav::Down,events);picker.handle(CursorNav::Down,events);
        GameFrame outside;picker.sync(outside,false);picker.sync(store,true);picker.handle(CursorNav::Confirm,events);picker.handle(CursorNav::Confirm,events);
        check(!events.events.empty()&&events.events.back().target=="stock:0","re-entering a store resets its cursor to the first row");
    }
    {
        GameFrame wait;wait.menu_open=true;wait.pre_game_menu=true;
        for(int y=0;y<=2;++y){TermMirrorLine line;line.source_row=y;line.text_utf8=y==0?"Please wait...":(y==2?" Name Audit                    Age  20":"");wait.menu_term_lines.push_back(std::move(line));}
        ModernWindow w;w.version=5;w.kind="dialog";w.stage="detail";w.continue_key=13;w.language="en";
        presentation::interpret_modern_content(wait,w,"sil");
        check(w.content_type=="character"&&!w.input_ready&&w.continue_key==0,"Sil's transient post-name sheet waits for the introduction without exposing Continue");
    }
    {
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"en","heng");GameFrame frame;frame.awaiting_command=true;
        inventory.capture(frame,[]{ModernRow row;row.id="owned";row.label="Owned";for(int command:{10,20,30})row.actions.push_back({"candidate","","",true,command});return std::vector<ModernRow>{row};},[]{
            presentation::PadCommandsMessage table;for(int command:{10,20,30}){presentation::PadCommandWireEntry entry;entry.command=command;entry.label_utf8="Action "+std::to_string(command);entry.seq_original={command};table.entries.push_back(entry);}return table;});
        presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;std::vector<int> keys;check(inventory.input(open,keys),"inventory root accepts its explicit open action");
        inventory.capture(frame,[]{ModernRow row;row.id="owned";row.label="Owned";for(int command:{10,20,30})row.actions.push_back({"candidate","","",true,command});return std::vector<ModernRow>{row};},[]{
            presentation::PadCommandsMessage table;for(int command:{10,20,30}){presentation::PadCommandWireEntry entry;entry.command=command;entry.label_utf8="Action "+std::to_string(command);entry.seq_original={command};table.entries.push_back(entry);}return table;});
        const auto &actions=frame.modern.rows.front().actions;
        check(actions.size()==4&&actions[0].id=="inventory:30"&&actions[1].id=="inventory:20"&&actions[2].id=="inventory:10"&&actions[3].id=="back","inventory actions are completely reversed before the final Back entry");
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(frame,true);picker.handle(CursorNav::Confirm,events);picker.handle(CursorNav::Up,events);picker.handle(CursorNav::Confirm,events);
        check(events.events.empty(),"inventory action popup wraps upward from the first action to local Back");
    }
    {
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"en","rogue");
        const auto owned=[] { ModernRow row;row.id="owned";row.label="Owned";return std::vector<ModernRow>{row}; };
        const auto commands=[] { return presentation::PadCommandsMessage{}; };
        GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
        GameFrame transient;inventory.capture(transient,owned,commands);
        presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;std::vector<int> keys;
        check(inventory.input(open,keys),"inventory open survives an owner-less refresh after a proven command boundary");
        inventory.capture(transient,owned,commands);
        check(transient.modern.kind=="inventory"&&transient.modern.stage=="browse","visible inventory keeps ownership across an owner-less refresh");
        presentation::InputEventWire cancel;cancel.e="ui_action";cancel.action="cancel";cancel.request_id=2;cancel.ui_revision=transient.modern.revision;
        check(inventory.input(cancel,keys),"inventory cancel remains accepted after an owner-less refresh");
        GameFrame child;child.menu_open=true;inventory.capture(child,owned,commands);
        presentation::InputEventWire reopen;reopen.e="ui_action";reopen.action="inventory_open";reopen.request_id=3;
        check(inventory.input(reopen,keys),"inventory open request is handled while a native child owns input");
        GameFrame rejected;inventory.capture(rejected,owned,commands);
        check(rejected.modern.result=="rejected"&&rejected.modern.stage.empty(),"native child ownership rejects root inventory reopening");
    }
    {
        // 2026-09-26: the list was open, then a command from outside the list (the
        // command menu's Settings) started a native text prompt.  The list is no longer
        // the owner: the text window must stay a dialog and the list must not reopen.
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"ja","rogue");
        const auto owned=[]{ModernRow row;row.id="owned";row.label="Owned";row.actions.push_back({"candidate","","",true,10});return std::vector<ModernRow>{row};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=10;entry.label_utf8="Call";entry.seq_original={10};table.entries.push_back(entry);return table;};
        GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
        std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
        GameFrame root=ready;inventory.capture(root,owned,commands);
        check(root.modern.kind=="inventory"&&root.modern.stage=="browse","inventory root is open before the outside command");
        auto text_frame=[]{GameFrame f;f.menu_open=true;f.text_input_active=true;f.modern.version=5;f.modern.kind="dialog";f.modern.stage="text";f.modern.revision=1000009;f.modern.input="slime-mold";return f;};
        GameFrame text=text_frame();inventory.capture(text,owned,commands);
        check(text.modern.kind=="dialog"&&text.modern.stage=="text"&&text.modern.input=="slime-mold","a text prompt started outside the list is not dressed as the inventory");
        GameFrame after=ready;inventory.capture(after,owned,commands);
        check(after.modern.stage.empty(),"the abandoned list does not reopen at the next command boundary");
        // An item command's own text prompt is still shown inside the list.
        presentation::InputEventWire reopen;reopen.e="ui_action";reopen.action="inventory_open";reopen.request_id=2;inventory.input(reopen,keys);
        GameFrame root2=ready;inventory.capture(root2,owned,commands);
        presentation::InputEventWire act;act.e="ui_action";act.action="inventory:10";act.target="owned";act.request_id=3;act.ui_revision=root2.modern.revision;
        check(inventory.input(act,keys),"inventory sends the item command");
        GameFrame own=text_frame();inventory.capture(own,owned,commands);
        check(own.modern.kind=="inventory"&&own.modern.stage=="text","an item command's own text prompt keeps the inventory frame");
    }
    {
        // 2026-09-25: an item command that ends on a bare map `-more-` (Hengband's
        // `-続く-` with auto_more off).  The core has no modern window at all
        // (version 0), so the inventory must own the prompt and answer Continue itself.
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"ja","heng");
        const auto owned=[]{ModernRow row;row.id="owned";row.label="Owned";row.count=2;row.actions.push_back({"candidate","","",true,10});return std::vector<ModernRow>{row};};
        const auto one_left=[]{ModernRow row;row.id="owned";row.label="Owned";row.count=1;row.actions.push_back({"candidate","","",true,10});return std::vector<ModernRow>{row};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=10;entry.label_utf8="Destroy";entry.seq_original={10};table.entries.push_back(entry);return table;};
        GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
        std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
        GameFrame root=ready;inventory.capture(root,owned,commands);
        presentation::InputEventWire act;act.e="ui_action";act.action="inventory:10";act.target="owned";act.request_id=2;act.ui_revision=root.modern.revision;
        check(inventory.input(act,keys)&&keys==std::vector<int>{10},"inventory sends the item command");
        GameFrame more;more.prompt.text_utf8=reinterpret_cast<const char*>(u8"Owned を壊した。-続く-");more.prompt.line_index=-1;
        inventory.capture(more,one_left,commands);
        check(more.modern.version==5&&more.modern.stage=="message"&&more.modern.kind=="inventory"&&more.modern.revision>0,"a bare map -more- after an item command stays in the inventory as a message");
        keys.clear();presentation::InputEventWire cont;cont.e="ui_action";cont.action="continue";cont.request_id=3;cont.ui_revision=more.modern.revision;
        check(inventory.input(cont,keys)&&keys==std::vector<int>{' '},"the inventory answers Continue on a -more- that has no native window");
        GameFrame after=more;inventory.capture(after,one_left,commands);
        check(after.modern.request_id==3&&after.modern.result=="accepted","the Continue reply is echoed");
        GameFrame back;back.awaiting_command=true;inventory.capture(back,one_left,commands);
        check(back.modern.kind=="inventory"&&back.modern.stage=="browse"&&back.modern.rows.front().count==1,"the list reopens after the -more- with the destroyed count");
    }
    {
        // An item command that finishes without a child prompt and without changing
        // the inventory or the message must not keep the list closed forever.
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"en","heng");
        const auto owned=[]{ModernRow row;row.id="owned";row.label="Owned";row.actions.push_back({"candidate","","",true,10});return std::vector<ModernRow>{row};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=10;entry.label_utf8="Noop";entry.seq_original={10};table.entries.push_back(entry);return table;};
        GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
        std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
        GameFrame root=ready;inventory.capture(root,owned,commands);
        presentation::InputEventWire act;act.e="ui_action";act.action="inventory:10";act.target="owned";act.request_id=2;act.ui_revision=root.modern.revision;inventory.input(act,keys);
        GameFrame stale=ready;inventory.capture(stale,owned,commands);
        check(!(stale.modern.stage=="browse"&&stale.modern.input_ready),"a stale command-boundary frame right after the command does not reopen the list");
        check(stale.modern.kind=="inventory"&&stale.modern.stage=="message"&&!stale.modern.input_ready&&stale.menu_open&&stale.modern.rows.size()==1
            &&stale.modern.rows.front().actions.empty(),"while the command runs the list stays on screen and accepts no input");
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        presentation::InputEventWire reopen;reopen.e="ui_action";reopen.action="inventory_open";reopen.request_id=3;inventory.input(reopen,keys);
        GameFrame later=ready;inventory.capture(later,owned,commands);
        check(later.modern.result=="accepted"&&later.modern.stage=="browse","a silent command stops blocking the list once the core has sat at the command prompt");
    }
    {
        // 2026-09-26: after an item command the list must not close to the map
        // between the child prompts and the return to the command prompt, but it
        // must step aside for aiming, a raw prompt (direction) and a core screen.
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"ja","heng");
        const auto owned=[]{ModernRow row;row.id="owned";row.label="Owned";row.count=3;row.actions.push_back({"candidate","","",true,10});return std::vector<ModernRow>{row};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=10;entry.label_utf8="Aim";entry.seq_original={10};table.entries.push_back(entry);return table;};
        GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
        std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
        GameFrame root=ready;inventory.capture(root,owned,commands);
        presentation::InputEventWire act;act.e="ui_action";act.action="inventory:10";act.target="owned";act.request_id=2;act.ui_revision=root.modern.revision;inventory.input(act,keys);
        GameFrame running;inventory.capture(running,owned,commands);
        check(running.modern.kind=="inventory"&&!running.modern.input_ready&&running.menu_open,"a running item command keeps the list instead of the map");
        GameFrame aiming;aiming.targeting.active=true;inventory.capture(aiming,owned,commands);
        check(aiming.modern.stage.empty()&&!aiming.menu_open,"aiming shows the map (the list steps aside)");
        GameFrame direction;direction.prompt.text_utf8="Direction?";inventory.capture(direction,owned,commands);
        check(direction.modern.stage.empty(),"a raw direction prompt shows the map");
        GameFrame screen;screen.menu_term_lines.push_back({});inventory.capture(screen,owned,commands);
        check(screen.modern.kind!="inventory","a core screen is not covered by the list");
        presentation::InputEventWire stray;stray.e="ui_action";stray.action="inventory:10";stray.target="owned";stray.request_id=3;stray.ui_revision=running.modern.revision;keys.clear();
        GameFrame again;inventory.capture(again,owned,commands);
        check(!inventory.input(stray,keys)&&keys.empty(),"the held list sends no item command");
    }
    {
        // A repeated identical message still proves the command ran (message seq).
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"en","heng");
        const auto owned=[]{ModernRow row;row.id="owned";row.label="Owned";row.actions.push_back({"candidate","","",true,10});return std::vector<ModernRow>{row};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=10;entry.label_utf8="Eat";entry.seq_original={10};table.entries.push_back(entry);return table;};
        GameFrame ready;ready.awaiting_command=true;ready.messages.push_back({7,1,"You are full."});inventory.capture(ready,owned,commands);
        std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
        GameFrame root=ready;inventory.capture(root,owned,commands);
        presentation::InputEventWire act;act.e="ui_action";act.action="inventory:10";act.target="owned";act.request_id=2;act.ui_revision=root.modern.revision;inventory.input(act,keys);
        GameFrame again=ready;again.messages.back().seq=8;inventory.capture(again,owned,commands);
        check(again.modern.kind=="inventory"&&again.modern.stage=="browse"&&again.modern.input_ready,"the same message text with a new sequence number reopens the list");
    }
    {
        // Rogue's ［続く］ is only a suffix of the latest message and accepts Space only.
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"ja","rogue");
        const auto owned=[]{ModernRow row;row.id="owned";row.label="Owned";row.equipped=true;row.actions.push_back({"candidate","","",true,84});return std::vector<ModernRow>{row};};
        const auto removed=[]{ModernRow row;row.id="owned";row.label="Owned";row.actions.push_back({"candidate","","",true,84});return std::vector<ModernRow>{row};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=84;entry.label_utf8="Take off";entry.seq_original={84};table.entries.push_back(entry);return table;};
        GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
        std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
        GameFrame root=ready;inventory.capture(root,owned,commands);
        presentation::InputEventWire act;act.e="ui_action";act.action="inventory:84";act.target="owned";act.request_id=2;act.ui_revision=root.modern.revision;inventory.input(act,keys);
        GameFrame more;more.messages.push_back({3,1,reinterpret_cast<const char*>(u8"Owned を脱いだ。［続く］")});inventory.capture(more,removed,commands);
        check(more.modern.stage=="message"&&more.modern.version==5,"Rogue ［続く］ after an item command stays in the inventory");
        keys.clear();presentation::InputEventWire cancel;cancel.e="ui_action";cancel.action="cancel";cancel.request_id=3;cancel.ui_revision=more.modern.revision;
        check(inventory.input(cancel,keys)&&keys==std::vector<int>{' '},"Rogue ［続く］ is dismissed with Space even from Cancel");
        // 原作側の窓（message_only）として来た［続く］でも、一覧が受けて Space にする（2026-09-26）。
        GameFrame boxed;boxed.menu_open=true;boxed.modern.version=5;boxed.modern.kind="dialog";boxed.modern.stage="detail";
        boxed.modern.message_only=true;boxed.modern.revision=1000001;boxed.modern.body="Owned を脱いだ。";
        inventory.capture(boxed,removed,commands);
        check(boxed.modern.stage=="message"&&boxed.modern.kind=="inventory","a native Rogue message window stays inside the inventory");
        keys.clear();presentation::InputEventWire stale=cancel;stale.request_id=4;stale.ui_revision=boxed.modern.revision-1;
        check(inventory.input(stale,keys)&&keys.empty(),"a stale Cancel on the Rogue ［続く］ is answered here, not forwarded as ESC");
        GameFrame answer=boxed;inventory.capture(answer,removed,commands);
        check(answer.modern.request_id==4&&answer.modern.result=="rejected","the rejected Cancel is reported back with its request id");
        keys.clear();presentation::InputEventWire fresh=cancel;fresh.request_id=5;fresh.ui_revision=answer.modern.revision;
        check(inventory.input(fresh,keys)&&keys==std::vector<int>{' '},"Cancel on a native Rogue ［続く］ window sends Space");
    }
    {
        ModernWindow grid;grid.content_type="choice_grid";
        ModernRow a;a.id="e";a.slot=7;a.selection=80;grid.rows.push_back(a);ModernRow b;b.id="f";b.slot=8;b.selection=1;grid.rows.push_back(b);
        grid.cursor_target="e";
        check(presentation::modern_focus_key(grid,"f")=='6',"a vertical modern list crosses a native grid row in one forward step");
        grid.cursor_target="f";check(presentation::modern_focus_key(grid,"e")=='4',"choice-grid reverse movement follows native key order");
        ModernRow c;c.id="g";c.slot=9;c.selection=1;grid.rows.push_back(c);grid.cursor_target="f";
        check(presentation::modern_focus_key(grid,"g")=='2',"a native one-column choice grid keeps vertical movement");
    }
    {
        GameFrame frame;frame.menu_open=true;frame.pre_game_menu=true;frame.menu_core_cursor.line_index=0;
        for(int i=0;i<6;++i){TermMirrorLine line;line.source_row=14+i;line.text_utf8="           Stat"+std::to_string(i)+" :       10    +0   +1   +0  =       11      18";frame.menu_term_lines.push_back(line);}
        TermMirrorLine chance;chance.source_row=23;chance.text_utf8="Prob: ~ 1/100";frame.menu_term_lines.push_back(chance);
        ModernWindow window;window.language="en";window.stage="cursor";
        presentation::modern_heng_minimum_stats(frame,window,"Set minimum stats for picking up your character.","legacy");
        check(window.content_type=="stat_allocation"&&window.rows.size()==13&&window.rows.back().key==13,"minimum desired stats expose six -/+ pairs and Continue");
        check(window.content.size()==8&&window.content[1].cells.size()==7,"minimum desired stats retain a compact seven-column native breakdown");
    }
    {
        GameFrame frame;ModernWindow window;window.language="en";window.stage="cursor";
        presentation::modern_monster_name_prompt(frame,window,"Enter character to be identified(^A:All,^U:Uniqs,^N:Non uniqs,^M:Name):");
        check(window.content_type=="monster_query"&&window.rows.size()==4&&window.rows.front().key==13,"monster research exposes name entry as the first explicit choice");
    }
    {
        ModernWindow window;window.content_type="character";window.title="Choose a race";
        for(char key:std::string("cba")){ModernRow row;row.key=key;row.label=std::string(1,key);window.rows.push_back(row);}
        presentation::modern_frox_birth_order(window,"frox");
        check(window.rows[0].key=='a'&&window.rows[1].key=='b'&&window.rows[2].key=='c',"Frox race and class choices sort by their native a-key order");
    }
    {
        GameFrame first;first.menu_open=true;first.modern.version=5;first.modern.kind="dialog";first.modern.stage="choices";first.modern.title="Inn";
        for(int i=0;i<3;++i){ModernRow row;row.id="old-"+std::to_string(i);row.label="row-"+std::to_string(i);row.key='a'+i;first.modern.rows.push_back(row);}
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(first,true);picker.handle(CursorNav::Down,events);
        GameFrame changed=first;for(int i=0;i<3;++i)changed.modern.rows[i].id="new-"+std::to_string(i);++changed.modern.revision;picker.sync(changed,true);picker.handle(CursorNav::Confirm,events);
        check(!events.events.empty()&&events.events.back().target=="new-1","facility choice focus survives a result-driven row identity refresh");
    }
    {
        hd2d::UiLayout layout;layout.preview_menu={60,80,1480,740};GameFrame frame;frame.menu_open=true;frame.modern.version=5;frame.modern.kind="store";frame.modern.stage="browse";
        hd2d::ModernStoreUi picker;picker.sync(frame,true);const auto baseline=picker.input_rect(layout);
        for(const std::string stage:{"number","confirm","text","choices"}){frame.modern.stage=stage;++frame.modern.revision;picker.sync(frame,true);const auto current=picker.input_rect(layout);check(current.y==baseline.y&&current.h==baseline.h,"the lower control frame is fixed across modal states");}
    }
    {
        presentation::NativeStoreMenu adapter;GameFrame store;store.menu_open=true;TermMirrorLine help;help.text_utf8="[?] Help";store.menu_term_lines.push_back(help);MenuChoice choice;choice.key='?';choice.line_index=0;choice.span_len=8;store.menu_choices.push_back(choice);
        native_store_info info{};info.active=1;info.kind=1;info.root_frame=1;info.page_size=12;
        adapter.set_enabled(true,false,"genso");adapter.capture(store,info,{});check(store.modern.rows.empty(),"Gensoband omits the redundant Store commands row");
        info.buy_all_key='B';info.buy_all_total=50;info.show_hidden_key='U';info.hidden_count=2;store.hud.gold=40;
        adapter.set_enabled(true,true,"frox");adapter.capture(store,info,{});
        const auto buy=std::find_if(store.modern.rows.begin(),store.modern.rows.end(),[](const auto &row){return row.id=="store:command:66";});
        const auto show=std::find_if(store.modern.rows.begin(),store.modern.rows.end(),[](const auto &row){return row.id=="store:command:85";});
        check(buy!=store.modern.rows.end()&&buy->available&&!buy->reason.empty()&&show!=store.modern.rows.end()&&show->label.find(reinterpret_cast<const char*>(u8"隠したアイテムを再表示"))!=std::string::npos,"Frox exposes Buy all (buys what the gold covers, with a note when short) and meaningful Show hidden commands");
    }
    {
        // 2026-09-26: 持ち物一覧で命令を選んだ後の原作の選択肢は、カーソルを 1 行ずつ
        // 動かさずに確定し、確定し終えるまで直前の一覧を描き続ける（変愚の「壊す」）。
        GameFrame inv;inv.menu_open=true;inv.modern.version=5;inv.modern.kind="inventory";inv.modern.stage="browse";inv.modern.revision=4000001;
        for(int i=0;i<5;++i){ModernRow row;row.id="owned-"+std::to_string(i);row.label="Item "+std::to_string(i);inv.modern.rows.push_back(row);}
        hd2d::ModernStoreUi picker;presentation::InputEventsMessage events;picker.sync(inv,true);
        GameFrame pick;pick.menu_open=true;pick.modern.version=5;pick.modern.kind="dialog";pick.modern.stage="choices";pick.modern.content_type="items";pick.modern.revision=1000010;pick.modern.cursor=0;
        for(int i=0;i<5;++i){ModernRow row;row.id="item:0:"+std::to_string(i);row.label="Item "+std::to_string(i);row.key='a'+i;row.available=true;pick.modern.rows.push_back(row);}
        pick.modern.auto_select="item:0:4";
        picker.sync(pick,true);
        check(picker.showing_auto_backdrop(),"an auto-selected native item selector is not painted over the inventory");
        picker.advance(events);
        check(events.events.size()==1&&events.events[0].action=="choice"&&events.events[0].target=="item:0:4","a lettered item row is chosen at once without walking the native cursor");
        GameFrame same=pick;same.modern.auto_select.clear();same.modern.request_id=events.events[0].request_id;same.modern.result="accepted";picker.sync(same,true);
        check(picker.showing_auto_backdrop(),"the same selector frame after the choice still shows the inventory");
        GameFrame confirm;confirm.menu_open=true;confirm.modern.version=5;confirm.modern.kind="inventory";confirm.modern.stage="confirm";confirm.modern.revision=1000011;confirm.modern.rows=inv.modern.rows;
        picker.sync(confirm,true);
        check(!picker.showing_auto_backdrop(),"the next native question replaces the retained inventory");
        // Enter で決まる行（キーがカーソルの位置に依る）は、従来どおりカーソルを合わせる。
        hd2d::ModernStoreUi cursor;presentation::InputEventsMessage moved;cursor.sync(inv,true);
        GameFrame enter=pick;for(auto &row:enter.modern.rows)row.key=13;enter.modern.revision=1000020;cursor.sync(enter,true);cursor.advance(moved);
        check(moved.events.size()==1&&moved.events[0].action=="navigate"&&moved.events[0].number==2,"an Enter-selected row still moves the native cursor first");
        check(cursor.showing_auto_backdrop(),"cursor movement toward an Enter-selected row is not painted");
        // 持ち物一覧から来ていない選択肢（施設など）は隠さない。
        hd2d::ModernStoreUi other;GameFrame dialog=pick;other.sync(dialog,true);
        check(!other.showing_auto_backdrop(),"a selector that was not opened from the inventory is painted normally");
    }
    {
        // Angband の選択肢は行に身元が無い。見出しの一覧名・文字・名前が揃う行だけを選ぶ。
        presentation::NativeInventoryMenu inventory;inventory.configure(true,"en","angband");
        const auto owned=[]{
            ModernRow torch;torch.id="eq";torch.label="a Wooden Torch (5000 turns)";torch.slot=5;torch.selection=1;torch.equipped=true;torch.count=1;torch.actions.push_back({"candidate","","",true,100});
            ModernRow spare=torch;spare.id="pk";spare.slot=5;spare.selection=0;spare.equipped=false;
            return std::vector<ModernRow>{torch,spare};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=100;entry.label_utf8="Drop";entry.seq_original={100};table.entries.push_back(entry);return table;};
        GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
        std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
        GameFrame root=ready;inventory.capture(root,owned,commands);
        presentation::InputEventWire act;act.e="ui_action";act.action="inventory:100";act.target="eq";act.request_id=2;act.ui_revision=root.modern.revision;inventory.input(act,keys);
        const auto selector=[](const char *header,std::vector<std::pair<int,std::string>> rows){
            GameFrame f;f.menu_open=true;f.modern.version=5;f.modern.kind="dialog";f.modern.stage="choices";f.modern.revision=2000;
            TermMirrorLine h;h.text_utf8=header;f.menu_term_lines.push_back(h);
            for(size_t i=0;i<rows.size();++i){ModernRow r;r.id="choice:"+std::to_string(i);r.key=rows[i].first;r.label=rows[i].second;f.modern.rows.push_back(r);}
            f.modern.actions.push_back({"key:47","/","",true,'/'});return f;};
        GameFrame inven=selector("Drop which item? (Inven: a-f, / for Equip, ESC)",{{'a',"2 Rations of Food 1.6 lb"},{'f',"a Wooden Torch (5000 turns) 2.2 lb"}});
        inventory.capture(inven,owned,commands);
        check(inven.modern.auto_select=="inventory-source","Angband never takes the pack torch for the equipped torch; it switches to Equip");
        GameFrame equip=selector("Drop which item? (Equip: a-f, / for Inven, ESC)",{{'a',"Wielding a Dagger 1.2 lb"},{'f',"Light source a Wooden Torch (5000 turns) 2.2 lb"}});
        equip.modern.revision=2001;inventory.capture(equip,owned,commands);
        check(equip.modern.auto_select=="choice:1","Angband selects the equipped torch by list, letter and name");
    }
    {
        // 日本語の Angband は見出しの札が変愚の綴り（持ち物: 装備品: 矢筒: 床上:）。英語と同じく一覧を見分ける。
        presentation::NativeInventoryMenu inventory;
        const auto owned=[]{
            ModernRow torch;torch.id="eq";torch.label=reinterpret_cast<const char*>(u8"木の松明 (5000 ターン)");torch.slot=5;torch.selection=1;torch.equipped=true;torch.count=1;torch.actions.push_back({"candidate","","",true,100});
            ModernRow spare=torch;spare.id="pk";spare.slot=5;spare.selection=0;spare.equipped=false;
            ModernRow arrow;arrow.id="qv";arrow.label=reinterpret_cast<const char*>(u8"矢 (40)");arrow.slot=1000;arrow.selection=0;arrow.count=40;arrow.actions.push_back({"candidate","","",true,100});
            return std::vector<ModernRow>{torch,spare,arrow};};
        const auto commands=[]{presentation::PadCommandsMessage table;presentation::PadCommandWireEntry entry;entry.command=100;entry.label_utf8="Drop";entry.seq_original={100};table.entries.push_back(entry);return table;};
        const auto selector=[](const char8_t *header,std::vector<std::pair<int,std::string>> rows,int revision,int step){
            GameFrame f;f.menu_open=true;f.modern.version=5;f.modern.kind="dialog";f.modern.stage="choices";f.modern.revision=revision;
            TermMirrorLine h;h.text_utf8=reinterpret_cast<const char*>(header);f.menu_term_lines.push_back(h);
            for(size_t i=0;i<rows.size();++i){ModernRow r;r.id="choice:"+std::to_string(i);r.key=rows[i].first;r.label=rows[i].second;f.modern.rows.push_back(r);}
            f.modern.actions.push_back({"key:"+std::to_string(step),std::string(1,char(step)),"",true,step});return f;};
        const auto start=[&](const char *target){
            inventory=presentation::NativeInventoryMenu{};inventory.configure(true,"ja","angband");
            GameFrame ready;ready.awaiting_command=true;inventory.capture(ready,owned,commands);
            std::vector<int> keys;presentation::InputEventWire open;open.e="ui_action";open.action="inventory_open";open.request_id=1;inventory.input(open,keys);
            GameFrame root=ready;inventory.capture(root,owned,commands);
            presentation::InputEventWire act;act.e="ui_action";act.action="inventory:100";act.target=target;act.request_id=2;act.ui_revision=root.modern.revision;inventory.input(act,keys);};
        start("eq");
        GameFrame inven=selector(u8"どれを落としますか? (持ち物: a-f, / で装備, ESC)",{{'a',reinterpret_cast<const char*>(u8"食料 2 個 1.6 lb")},{'f',reinterpret_cast<const char*>(u8"木の松明 (5000 ターン) 2.2 lb")}},3000,'/');
        inventory.capture(inven,owned,commands);
        check(inven.modern.auto_select=="inventory-source","Japanese Angband: the pack torch is not taken for the equipped one; it switches to Equip");
        GameFrame equip=selector(u8"どれを落としますか? (装備品: a-f, / で持ち物, ESC)",{{'a',reinterpret_cast<const char*>(u8"武器 ダガー 1.2 lb")},{'f',reinterpret_cast<const char*>(u8"光源 木の松明 (5000 ターン) 2.2 lb")}},3001,'/');
        inventory.capture(equip,owned,commands);
        check(equip.modern.auto_select=="choice:1","Japanese Angband selects the equipped torch by the 装備品: header, letter and name");
        start("qv");
        GameFrame pack=selector(u8"どれを落としますか? (持ち物: a-f, | で矢筒, ESC)",{{'a',reinterpret_cast<const char*>(u8"食料 2 個 1.6 lb")}},3002,'|');
        inventory.capture(pack,owned,commands);
        check(pack.modern.auto_select=="inventory-source","Japanese Angband moves from 持ち物: to the quiver with |");
        GameFrame quiver=selector(u8"どれを落としますか? (矢筒: 0-0, / で持ち物, ESC)",{{'0',reinterpret_cast<const char*>(u8"矢 (40)")}},3003,'/');
        inventory.capture(quiver,owned,commands);
        check(quiver.modern.auto_select=="choice:0","Japanese Angband selects the arrows by the 矢筒: header");
        // 札が見分けられない綴り（わざと壊す）では何も選ばない。
        start("eq");
        GameFrame broken=selector(u8"どれを落としますか? (もちもの: a-f, / で装備, ESC)",{{'f',reinterpret_cast<const char*>(u8"木の松明 (5000 ターン) 2.2 lb")}},3004,'/');
        inventory.capture(broken,owned,commands);
        check(broken.modern.auto_select.empty(),"an unknown header spelling selects nothing");
    }
    {
        // Tabs into the inventory: the locally-built window has no rows until the core answers.
        // It must not be drawn (an empty window flashed between tabs; user report 2026-09-26).
        hd2d::ModernStoreUi inv;presentation::InputEventsMessage out;GameFrame none;inv.sync(none,true);
        inv.open_inventory(out,{"A","@inventory"},1);
        check(inv.active()&&inv.opening_placeholder(),"the inventory tab is a hidden placeholder until the core answers");
        check(!out.events.empty()&&out.events.back().action=="inventory_open","the inventory tab asks the core for the list");
        GameFrame reply;reply.menu_open=true;reply.modern.version=5;reply.modern.kind="inventory";reply.modern.stage="browse";
        reply.modern.revision=2;reply.modern.request_id=out.events.back().request_id;reply.modern.result="accepted";
        reply.modern.rows={{"item-a","A","1 kg",1,1,'a'}};inv.sync(reply,true);
        check(inv.active()&&!inv.opening_placeholder(),"the core's list replaces the placeholder and is drawn");
        check(!inv.leaving_parent_tab(),"no parent tab request without the Rogue settings list");
    }
    std::cout<<"PASS "<<checks<<" modern UI checks\n";
}
