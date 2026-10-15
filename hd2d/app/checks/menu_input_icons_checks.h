#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/modern_store_ui.h"
#include "ui/feature_menu.h"
#include "assets/tile_catalog.h"
#include "world/slab_library.h"
#include "frame/modern_window_codec.h"
#include "frame/message_more.h"
#include "frame/frame_codec.h"
#include "ui/menu_chrome.h"
#include "ui/menu_scale.h"
#include "bridge/modern_content.h"
#include "ui/menu_frame.h"
#include "ui/game_hud.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
namespace hd2d {
inline int run_menu_input_icons_check(const AppOptions &options,const char *directory) {
    int checks=0,failures=0,images=0;std::filesystem::create_directories(directory);
    std::vector<std::string> failed;
    const auto check=[&](bool okay,const char *label){++checks;if(!okay){++failures;failed.emplace_back(label);}};
    check(menu_label_with_key('a',"(a) Item")=="(a) Item","parenthesized original symbol is not duplicated");
    check(menu_label_with_key('A',"[A] Item")=="[A] Item","bracketed original symbol is not duplicated");
    GameFrame ready;ready.awaiting_command=true;ready.command_menu_keys="xX";
    check(nothing_to_confirm(ready),"normal command wait: confirm (Enter/A) has nothing to do");
    for(int mode=0;mode<4;++mode) {
        auto child=ready;
        if(mode==0)child.menu_open=true;
        if(mode==1)child.numeric.active=true;
        if(mode==2)child.text_input_active=true;
        if(mode==3)child.prompt.choices={{"No",'n',0,1}};
        check(!nothing_to_confirm(child),"child input still has something for confirm (Enter/A) to answer");
    }
    GameFrame ready_copy;std::string codec_error;
    check(presentation::decode_game_frame(presentation::encode_game_frame(ready),ready_copy,codec_error)
        && ready_copy.command_menu_keys=="xX" && ready_copy.awaiting_command,"native menu keys frame roundtrip");
    GameFrame native;native.menu_open=true;native.menu_over_map=true;
    const std::vector<std::string> raw_lines={"+------------------------------+","| 》 設定       情報           |","|    プレイ記録(|)             |","+------------------------------+"};
    for(size_t i=0;i<raw_lines.size();++i){TermMirrorLine line;line.source_row=static_cast<int>(i)+1;line.text_utf8=raw_lines[i];line.color=1;native.menu_term_lines.push_back(line);}
    native.menu_core_cursor={1,2,9,0,0,0};native.menu_core_cursors={native.menu_core_cursor};
    const auto cleaned=modern_menu_text(native);
    check(cleaned[0].find_first_not_of(' ')==std::string::npos && cleaned[3].find_first_not_of(' ')==std::string::npos,"terminal rectangle borders are removed");
    check(cleaned[1].find('|')==std::string::npos && cleaned[1].find("》")==std::string::npos,"terminal sides and cursor mark are removed");
    check(cleaned[2].find("(|)")!=std::string::npos,"literal command key is preserved");
    for(size_t i=0;i<cleaned.size();++i)check(cleaned[i].size()==raw_lines[i].size()&&native.menu_term_lines[i].text_utf8==raw_lines[i],"native source and span byte offsets are unchanged");
    std::vector<std::string> missing={""};erase_old_menu_chrome(missing,{{0,20,1,0,0,0}});check(missing[0].empty(),"hidden input line does not throw while masking cursor");
    GameFrame page;page.menu_term_lines.resize(2);
    page.menu_term_lines[0].source_row=0;page.menu_term_lines[0].text_utf8="そのコマンドは店の中では使えません。";
    page.menu_term_lines[1].source_row=18;page.menu_term_lines[1].text_utf8="-続く-";
    check(!presentation::message_more_pending(page),"Japanese stock pagination is not a message wait");
    page.menu_term_lines[1].text_utf8="-more-";
    check(!presentation::message_more_pending(page),"English stock pagination is not a message wait");
    page.menu_term_lines[0].text_utf8="message -more-";
    check(presentation::message_more_pending(page),"English row zero message still waits");
    page.menu_term_lines[0].text_utf8="メッセージ -続く-";
    check(presentation::message_more_pending(page),"Japanese row zero message still waits");
    GameFrame f;f.menu_open=true;f.modern.version=4;f.modern.revision=1;f.modern.stage="browse";f.modern.page_size=2;f.hud.gold=2000;
    for(int i=0;i<4;++i){ModernRow row;row.id="stock-"+std::to_string(i);row.label=i%2?"Shadow Cloak":"Glaive";row.slot=i;row.hotkey='a'+i%2;row.key=i<2?row.hotkey:0;row.tile=i%2?1490:1427;row.glyph="?";row.weight="1.0 lb";row.count=1;row.price=100+i;row.description="Keyboard selection, list icons and item details share the same item identity.";row.actions={{"buy","Buy","",true,'p'}};f.modern.rows.push_back(row);}
    ModernWindow decoded;check(decode_modern_window(encode_modern_window(f.modern),decoded)&&decoded.rows[0].hotkey=='a'&&decoded.rows[0].actions[0].key=='p',"v4 keyboard roundtrip");
    auto invalid=encode_modern_window(f.modern);invalid["rows"][0]["hotkey"]=128;check(!decode_modern_window(invalid,decoded),"invalid hotkey rejected");
    ModernStoreUi keyboard;presentation::InputEventsMessage out;keyboard.sync(f,true);
    keyboard.text("b");keyboard.advance(out);check(out.events.empty(),"letter selection opens local popup only");
    keyboard.text("p");keyboard.advance(out);check(out.events.size()==1&&out.events.back().action=="buy"&&out.events.back().target=="stock-1","native popup key buys exact selected target");
    keyboard.text("p");keyboard.advance(out);check(out.events.size()==1,"pending key does not double submit");
    GameFrame selected=f;selected.modern.stage="choose";selected.modern.revision=2;selected.modern.request_id=1;selected.modern.result="accepted";keyboard.sync(selected,true);keyboard.advance(out);
    check(out.events.size()==2&&out.events.back().action=="select"&&out.events.back().target=="stock-1","keyboard shares guarded selection workflow");
    ModernStoreUi paged;paged.sync(f,true);paged.handle(CursorNav::Down,out);paged.handle(CursorNav::Down,out);out.events.clear();paged.text("a");paged.text("p");paged.advance(out);
    check(out.events.size()==1&&out.events[0].action=="page_for"&&out.events[0].target=="stock-2","repeated letters use cursor range and guarded page alignment");
    GameFrame own=f;own.modern.rows.clear();
    for(int i=0;i<2;++i){ModernRow row;row.id=i?"equipped":"carried";row.label="Same visible item";row.tab=1;row.slot=i?24:0;row.equipped=i!=0;row.hotkey='a';row.actions={{i?"takeoff":"equip",i?"Take off":"Equip","",true,i?'t':'w'}};own.modern.rows.push_back(row);}
    ModernStoreUi inventory;inventory.sync(own,true);inventory.tab(1);inventory.text("/");inventory.text("a");inventory.text("t");out.events.clear();inventory.advance(out);
    check(out.events.size()==1&&out.events[0].target=="equipped"&&out.events[0].action=="takeoff","inventory equipment key groups never collide");
    GameFrame answer=f;answer.modern.stage="confirm";answer.prompt.choices={{"Yes",'y',0,1},{"No",'n',2,1}};
    ModernStoreUi confirmation;confirmation.sync(answer,true);confirmation.text("N");out.events.clear();confirmation.advance(out);
    check(out.events.size()==1&&out.events[0].action=="answer"&&out.events[0].number==1,"native no confirmation key");
    GameFrame numeric=f;numeric.modern.stage="number";numeric.numeric.active=true;numeric.numeric.min=1;numeric.numeric.max=9;numeric.numeric.value=1;
    ModernStoreUi number;number.sync(numeric,true);number.text("2");number.text("a");out.events.clear();number.handle(CursorNav::Confirm,out);
    check(out.events.size()==1&&out.events[0].action=="number"&&out.events[0].number==2,"number input owns letters and digits");
    GameFrame editing=f;editing.modern.stage="text";editing.text_input_active=true;ModernStoreUi text_edit;text_edit.sync(editing,true);
    text_edit.text("abc");text_edit.edit_text_key(1);text_edit.text("x");out.events.clear();text_edit.advance(out);
    check(out.events.size()==3&&out.events[0].e=="text"&&out.events[1].e=="key"&&out.events[1].ctrl&&out.events[1].chr=="a"&&out.events[2].text=="x","text editing controls preserve input order");
    GameFrame choices=f;choices.modern.kind="dialog";choices.modern.stage="choices";choices.modern.rows.clear();
    for(int key:std::array<int,3>{'a','A',1}){ModernRow row;row.id="choice-"+std::to_string(key);row.label="Native option";row.key=row.hotkey=key;choices.modern.rows.push_back(row);}
    for(int key:std::array<int,3>{'a','A',1}){ModernStoreUi picker;picker.sync(choices,true);out.events.clear();picker.handle_native_key(key,out);check(out.events.size()==1&&out.events[0].action=="choice"&&out.events[0].target=="choice-"+std::to_string(key),"case and control key remain distinct");}
    Hd2dSettings settings;settings.modern_ui=true;FeatureMenu commands;
    std::vector<PadCommand> command_data{{11,'m',"Actions","Cast a spell",{'m'}},{12,3,"Actions","Character",{3}},{13,'?',"Actions","Magic help",{'?','m'}},{14,'M',"Items","Browse",{'M'}}};
    commands.set_pad_state(command_data,true,"preview");commands.open_commands(settings);
    check(commands.handle_native_key('m',settings)&&commands.take_command_request()==11,"command single key");
    check(commands.handle_native_key(3,settings)&&commands.take_command_request()==12,"command Ctrl key");
    commands.handle_native_key('?',settings);check(commands.take_command_request()==0,"command prefix waits");commands.handle_native_key('m',settings);check(commands.take_command_request()==13,"command sequence resolves");
    commands.handle_native_key('?',settings);commands.handle_native_key('x',settings);check(commands.take_command_request()==0,"invalid key sequence does not execute another command");
    /*
     * **F10 / Back は F1 / Start と同じ 1 つの窓を開く**。
     * 新 UI では `open_features()`（F10 / Back の入口）が `open_commands()`（F1 / Start
     * の入口）へそのまま委ねる——以前はここで最後の「機能」タブへ強制的に移して
     * いたが、それをやめたので、**前回のタブと行の位置がそのまま戻る**ことを確かめる
     * （持ち物一覧タブ・機能タブで閉じた場合も含む）。
     */
    {
        FeatureMenu reopen;
        std::vector<PadCommand> rows{{301,'a',"Actions","Do A",{'a'}},{302,'b',"Actions","Do B",{'b'}},
            {303,'c',"Items","Do C",{'c'}},{304,'d',"Items","Do D",{'d'}}};
        reopen.set_pad_state(rows,true,"preview");
        reopen.open_commands(settings); // F1 / Start 相当。既定は先頭のタブ（Actions）
        reopen.handle(MenuNav::NextTab,settings); // Items タブへ
        reopen.handle(MenuNav::Down,settings); // Items の 2 行目へ
        const int tab_before=reopen.command_tab_index();
        reopen.handle(MenuNav::Confirm,settings);
        const int row_id_before=reopen.take_command_request();
        check(tab_before==1&&row_id_before==304,"setup: navigated to a non-default tab and row");
        reopen.close();
        reopen.open_features(settings); // F10 / Back 相当
        check(reopen.command_tab_index()==tab_before,"F10/Back restores the last tab instead of forcing the features tab");
        reopen.handle(MenuNav::Confirm,settings);
        check(reopen.take_command_request()==row_id_before,"F10/Back restores the last row cursor within that tab");
        reopen.close();
        //! 前回が「持ち物一覧」タブでも同じに戻り、一覧を開く要求が生きていること。
        //! 並びは `refresh_command_tabs()` のとおり: 分類 → `@inventory` → `@ui-features`。
        //! 言語で綴りが変わる名札ではなく、この並びの決め打ちで引く。
        const auto labels=reopen.command_tab_labels();
        const int inventory_tab=static_cast<int>(labels.size())-2;
        reopen.open_commands(settings);
        while(reopen.command_tab_index()!=inventory_tab)reopen.handle(MenuNav::NextTab,settings);
        (void)reopen.take_inventory_request(); // 最初に着いたときの分は捨てる
        reopen.close();
        reopen.open_features(settings);
        check(reopen.command_tab_index()==inventory_tab,"F10/Back restores the inventory tab too");
        check(reopen.take_inventory_request(),"F10/Back re-raises the inventory-open request on the inventory tab");
        reopen.close();
        //! 前回が「機能」タブ（並びの最後）でも、強制ではなく復元として同じ所へ戻ること。
        const int features_tab=static_cast<int>(labels.size())-1;
        reopen.open_commands(settings);
        while(reopen.command_tab_index()!=features_tab)reopen.handle(MenuNav::NextTab,settings);
        reopen.close();
        reopen.open_features(settings);
        check(reopen.command_tab_index()==features_tab,"F10/Back still reaches the features tab when it was the last one open");
        reopen.close();
    }
    /*
     * キーボードの `i`・`e` は、メニューを 1 度も開く前でも持ち物の窓へ分類のタブを渡す（frox-5 の C13）。
     * 渡す元は `command_tab_labels()`。`set_pad_state()` を通らなくても `set_pad_commands()` で組めること。
     */
    {
        FeatureMenu fresh;
        check(fresh.command_tab_labels().empty(),"setup: a menu that was never opened has no command tabs");
        std::vector<PadCommand> rows{{301,'a',"Actions","Do A",{'a'}},{303,'c',"Items","Do C",{'c'}}};
        fresh.set_pad_commands(rows);
        const auto labels=fresh.command_tab_labels();
        check(labels.size()==4,"set_pad_commands() builds the categories, the inventory tab and the features tab");
        check(labels.size()==4&&labels[0]=="Actions"&&labels[1]=="Items","set_pad_commands() keeps the core's category order");
    }
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1360;capture.window_h=900;
    Window window;std::string err;if(!create_window(capture,window,err)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;UiImagePainter painter;VoxelRenderer icons;SlabLibrary slabs;
    slabs.set_dir(options.voxel_dir+"/slab");
    if(!paint.init(err)||!text.init(18,err)||!painter.init(err)||!icons.init(err)){SDL_Quit();return 1;}
    int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);UiLayout layout;layout.screen_w=w;layout.screen_h=h;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();layout.content={0,0,w,h};layout.scene=layout.content;
    for(int width:{480,720,1100}){const RectPx box{20,20,width,600};const auto head=menu_header_layout(text,box,32,{"Store items","Inventory / equipment"});check(head.previous.x==26&&head.next.x+head.next.w==box.x+box.w-6,"shoulder icons at window edges");check(head.tabs.front().x>head.previous.x+head.previous.w&&head.tabs.back().x+head.tabs.back().w<head.next.x,"tabs do not overlap icons");check(std::abs((head.tabs.front().x+head.tabs.back().x+head.tabs.back().w)-(2*box.x+box.w))<=2,"tab cluster is centred");}
    const std::vector<std::string> short_labels={"A","B","C","D","E","F","G","H","I","J"};
    const auto full_range=menu_header_visible_range(text,{0,0,1100,600},short_labels,8);
    check(full_range.first==0 && full_range.second==10,"all ten categories are shown when they fit");
    const std::vector<std::string> category_names={"魔法/特殊能力","行動","道具(使用)","道具(その他)","装備","扉/箱","情報","設定","その他","機能"};
    for(int width:{480,720,1100})for(int selected=0;selected<static_cast<int>(category_names.size());++selected) {
        const RectPx box{0,0,width,600};const auto range=menu_header_visible_range(text,box,category_names,selected);
        check(range.first<=selected && selected<range.first+range.second,"selected category remains visible at every width");
        int used=0;for(int i=range.first;i<range.first+range.second;++i)used+=text.measure(category_names[i])+28;
        const int room=width-2*(std::min(48,std::max(24,width/5))+16);
        bool extend_left=range.first>0 && used+text.measure(category_names[range.first-1])+28<=room;
        bool extend_right=range.first+range.second<static_cast<int>(category_names.size()) && used+text.measure(category_names[range.first+range.second])+28<=room;
        check(used<=room && !extend_left && !extend_right,"header fits maximum adjacent labels without shrinking");
    }
    check(text.measure(cleaned[1])==text.measure(raw_lines[1]),"Japanese cursor masking preserves displayed columns");
    const auto extreme=menu_header_visible_range(text,{0,0,160,200},{"An exceptionally long category"},0);
    check(extreme.first==0 && extreme.second==1,"one oversized category remains reachable");
    presentation::AssetManifestMessage manifest;manifest.root=std::filesystem::current_path().string();manifest.assets={{1427,"K",60,"tilework/sfc/K60.png"},{1490,"K",124,"tilework/sfc/K124.png"}};TileCatalog catalog;catalog.apply(manifest);
    check(slabs.acquire(1427,catalog.find(1427)->path,icons)>=0,"real item voxel board loads");
    check(slabs.acquire(1490,catalog.find(1490)->path,icons)>=0,"second real voxel board loads");
    auto start=[&](){gl::glViewport(0,0,w,h);gl::glClearColor(.025f,.035f,.05f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(w,h);text.begin(w,h);painter.begin(w,h);};
    auto finish=[&](const std::string &name,ModernStoreUi *menu){paint.flush();if(menu)menu->draw_voxels(icons,slabs,catalog,layout,&text);text.flush();check(gl::drain_gl_errors().empty(),"draw without GL errors");check(save_framebuffer_bmp((std::filesystem::path(directory)/(name+".bmp")).string(),w,h),"image saved");++images;};
    GameFrame visual=f;visual.modern.page_size=12;
    const std::array<std::pair<const char *,const char *>,3> fallback_items={{{"Scroll without a voxel","?"},{"Ring without a voxel","="},{"Staff without a voxel","_"}}};
    for(size_t i=0;i<fallback_items.size();++i){visual.modern.rows[i].label=fallback_items[i].first;visual.modern.rows[i].glyph=fallback_items[i].second;visual.modern.rows[i].tile=0;}
    visual.modern.rows.back().available=false;
    visual.modern.rows.back().reason="利用不可の表示例";
    visual.modern.rows.back().actions[0].enabled=false;
    for(size_t i=0;i<visual.modern.rows.size();++i){visual.modern.rows[i].hotkey=visual.modern.rows[i].key='a'+static_cast<int>(i);}
    for(int theme=0;theme<3;++theme) {
        const std::string prefix=theme==0?"A":(theme==1?"B":"C");settings.modern_theme=theme;
        ModernStoreUi menu;menu.sync(visual,true);start();menu.draw(paint,text,layout,visual,theme);finish(prefix+"-list",&menu);
        menu.text("b");start();menu.draw(paint,text,layout,visual,theme);finish(prefix+"-actions",&menu);
        commands.open_commands(settings);start();commands.draw(paint,text,layout,settings);finish(prefix+"-commands",nullptr);
        GameFrame carried=own;
        carried.modern.rows[0].label="持ち物の武器";carried.modern.rows[1].label="装備中の武器";
        carried.modern.rows[0].glyph=")";carried.modern.rows[1].glyph=")";
        ModernStoreUi grouped;grouped.sync(carried,true);grouped.tab(1);start();grouped.draw(paint,text,layout,carried,theme);finish(prefix+"-inventory",nullptr);grouped.shutdown();
        HudState native_state;native_state.window_theme=theme;native_state.cursors.sync(native);
        start();draw_game_ui(paint,text,layout,native,native_state,nullptr);finish(prefix+"-native-child",nullptr);
        auto choice_frame=native;choice_frame.menu_core_cursors.clear();choice_frame.menu_core_cursor.line_index=-1;
        choice_frame.menu_term_lines[1].text_utf8="| a) 入る                     |";
        choice_frame.menu_choices={{1,2,9,2,1,'a'}};
        native_state.cursors.sync(choice_frame);start();draw_game_ui(paint,text,layout,choice_frame,native_state,nullptr);finish(prefix+"-native-choice",nullptr);
        const auto body=panel_body(menu_text_area(layout,false,theme),layout.cell_w);
        check(menu_choice_at(text,layout,choice_frame,body.x+text.measure("| ")+2,body.y+layout.cell_h+2,theme)==0,"native choice click uses new frame geometry");
        std::vector<PadCommand> wide_commands;
        for(int i=0;i<9;++i)wide_commands.push_back({101+i,'a'+i,category_names[i],"実行する",{'a'+i}});
        FeatureMenu wide;wide.set_pad_state(wide_commands,true,"check");wide.open_commands(settings);
        start();wide.draw(paint,text,layout,settings);finish(prefix+"-all-categories",nullptr);
        auto labels=wide.command_tab_labels();auto box=fixed_menu_rect(layout);
        const auto range=menu_header_visible_range(text,box,labels,0);
        const std::vector<std::string> visible_labels(labels.begin()+range.first,labels.begin()+range.first+range.second);
        const auto hit_header=menu_header_layout(text,box,box.y+layout.cell_h-6,visible_labels);
        const int target=std::min(3,range.second-1);const auto hit=hit_header.tabs[target];
        check(wide.click_tab_header(hit.x+hit.w/2,hit.y+hit.h/2,settings),"fourth visible category accepts mouse selection");
        wide.handle(MenuNav::Confirm,settings);check(wide.take_command_request()==101+target,"expanded header click resolves the displayed category");
        menu.shutdown();
    }
    /*
     * 確認の「はい」「いいえ」を指で押せる（修-04・修-05。遊びのテスト 1 周目 F-hengband-05・F-tangband-05・F-angband-06・F-frox-104）。
     * dialog の確認（選択肢の窓を経由しないので navigation_page_ が真になる変愚・短愚の階段、Angband の引退と変身を戻す 3 択）と、
     * 店・持ち物の窓の確認の両方で、答えの矩形が画面の問い合わせの部品（painted_content）に載り、その中央を押すと選択が動き、
     * 決定でその答えが送られる。答えの矩形はほかの項目と重ならない。
     */
    {
        const auto verify=[&](bool okay,const std::string &what){check(okay,what.c_str());};
        const auto answer_cell=[](const ModernStoreUi &ui,int index)->const ModernPaintedCell *{
            for(const auto &cell:ui.painted_content())if(cell.target=="answer:"+std::to_string(index))return &cell;
            return nullptr;
        };
        const auto make_confirm=[&](const std::string &kind,int count){
            GameFrame c=f;c.menu_open=true;c.modern.kind=kind;c.modern.stage="confirm";c.modern.title="確認";c.modern.revision=7;
            c.prompt.text_utf8="Really? [y/n]";c.prompt.line_index=-1;c.prompt.choices.clear();
            const char *labels[]={"はい","いいえ","取消"};const char keys[]={'y','n','q'};
            for(int i=0;i<count;++i)c.prompt.choices.push_back({labels[i],keys[i],10+i*4,1});
            if(kind=="dialog"){c.modern.rows.clear();c.modern.content.clear();c.modern.content_type="";c.modern.input_ready=true;}
            return c;
        };
        for(const std::string kind:{std::string("dialog"),std::string("store")})for(int count:{2,3}) {
            const std::string name=kind+" confirm with "+std::to_string(count)+" answers";
            GameFrame c=make_confirm(kind,count);ModernStoreUi ui;ui.sync(c,true);
            start();ui.draw(paint,text,layout,c,0);paint.flush();text.flush();
            bool all_traced=true;for(int i=0;i<count;++i)all_traced=all_traced&&answer_cell(ui,i)!=nullptr;
            verify(all_traced,name+": every answer is in the painted parts (query_ui buttons)");
            if(!all_traced)continue;
            bool overlap=false;
            for(const auto &cell:ui.painted_content()){
                if(cell.target.rfind("answer:",0)==0)continue;
                for(int i=0;i<count;++i){const RectPx a=answer_cell(ui,i)->rect;
                    if(cell.rect.w>0&&cell.rect.h>0&&a.x<cell.rect.x+cell.rect.w&&cell.rect.x<a.x+a.w&&a.y<cell.rect.y+cell.rect.h&&cell.rect.y<a.y+a.h)overlap=true;}
            }
            verify(!overlap,name+": answer rectangles do not overlap other painted parts");
            for(int i=0;i<count;++i)for(int j=i+1;j<count;++j){const RectPx a=answer_cell(ui,i)->rect,b=answer_cell(ui,j)->rect;
                verify(!(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h),name+": answers "+std::to_string(i)+" and "+std::to_string(j)+" do not overlap");}
            for(const int pick:{count-1,0,1}) {
                // 決定を送ると応答待ちになって次の押下を受けないので、押下ごとに作り直す
                ModernStoreUi tapped;tapped.sync(c,true);
                start();tapped.draw(paint,text,layout,c,0);paint.flush();text.flush();
                const RectPx r=menu_scaled_rect(answer_cell(tapped,pick)->rect,1.f);
                presentation::InputEventsMessage tap;tapped.click(layout,r.x+r.w/2,r.y+r.h/2,tap);
                start();tapped.draw(paint,text,layout,c,0);paint.flush();text.flush();
                bool only_picked=true;for(int i=0;i<count;++i)only_picked=only_picked&&answer_cell(tapped,i)->selected==(i==pick);
                verify(tap.events.empty()&&only_picked,name+": tapping answer "+std::to_string(pick)+" moves the selection and sends nothing yet");
                presentation::InputEventsMessage decide;tapped.handle(CursorNav::Confirm,decide);
                verify(decide.events.size()==1&&decide.events[0].action=="answer"&&decide.events[0].number==pick,name+": Confirm after tapping answer "+std::to_string(pick)+" sends that answer");
                tapped.shutdown();
            }
        }
    }
    /*
     * 見出しが 1 つの詳細の頁は、決定で継続のキーを送る（修-08。Angband の死亡後の「詳しい情報を見る」）。見出しが 2 つ以上の頁は、
     * 決定が見出しへの移動のまま（文書の頁の目次）。窓の題の判定（修-09）は、地図の字だけの行・`a) ` の項目の行を見出しに採らない。
     */
    {
        const auto heading_page=[&](int headings){
            GameFrame c=f;c.menu_open=true;c.modern.version=5;c.modern.kind="dialog";c.modern.stage="detail";c.modern.content_type="document";c.modern.title="Info";
            c.modern.revision=11;c.modern.input_ready=true;c.modern.continue_key=13;c.modern.actions.clear();c.modern.rows.clear();c.modern.content.clear();
            for(int i=0;i<headings;++i){ModernBlock b;b.kind="heading";b.cells.push_back({"","Heading "+std::to_string(i),false});c.modern.content.push_back(b);}
            return c;
        };
        {
            GameFrame c=heading_page(1);ModernStoreUi ui;ui.sync(c,true);presentation::InputEventsMessage out;ui.handle(CursorNav::Confirm,out);
            check(out.events.size()==1&&out.events[0].action=="continue","detail page with one heading: Confirm sends Continue");
            ui.shutdown();
        }
        {
            GameFrame c=heading_page(2);ModernStoreUi ui;ui.sync(c,true);presentation::InputEventsMessage out;ui.handle(CursorNav::Confirm,out);
            check(out.events.empty(),"detail page with two headings: Confirm still moves to the heading and sends nothing");
            ui.shutdown();
        }
        check(presentation::modern_heading_candidate("Character Sheet"),"a plain title is a heading");
        check(presentation::modern_heading_candidate("--- Title ---"),"a title with dashes is a heading");
        check(presentation::modern_heading_candidate("== Birth =="),"an equals-sign title is a heading");
        check(!presentation::modern_heading_candidate("#.#..::~~.#%"),"a row of map glyphs is not a heading");
        check(!presentation::modern_heading_candidate("..#...@..#.."),"a map row with the player is not a heading");
        check(!presentation::modern_heading_candidate("a) Food Ration"),"an item row is not a heading");
        check(!presentation::modern_heading_candidate("b) Scroll of Light"),"another item row is not a heading");
        check(presentation::modern_heading_candidate(reinterpret_cast<const char *>(u8"何かキーを押すと詳しい情報を見る:")),"Japanese prose is a heading");
        GameFrame mirror;mirror.menu_open=true;
        const char *lines[]={"#.#..::~~.#%","Character Sheet","a) Food Ration"};
        for(int y=0;y<3;++y){TermMirrorLine l;l.source_row=y;l.text_utf8=lines[y];mirror.menu_term_lines.push_back(std::move(l));}
        ModernWindow w;w.version=5;w.kind="dialog";w.stage="detail";w.language="en";
        presentation::interpret_modern_content(mirror,w,"heng");
        check(w.title=="Character Sheet","the window title skips the map row and the item row");
    }
    std::ofstream report(std::filesystem::path(directory)/"result.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"images\":"<<images<<",\"game_core_started\":false,\"failed\":[";for(size_t i=0;i<failed.size();++i)report<<(i?",":"")<<'"'<<failed[i]<<'"';report<<"]}\n";report.close();
    keyboard.shutdown();paged.shutdown();inventory.shutdown();confirmation.shutdown();number.shutdown();slabs.shutdown(&icons);icons.shutdown();paint.shutdown();text.shutdown();painter.shutdown();SDL_Quit();return failures?1:0;
}
}
