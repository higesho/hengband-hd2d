#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/menu_scale.h"
#include "ui/native_menu_view.h"
#include "ui/menu_frame.h"
#include "ui/feature_menu.h"
#include "ui/modern_store_ui.h"
#include "ui/game_hud.h"
#include "ui/minimap_markers.h"
#include "assets/tile_catalog.h"
#include "world/slab_library.h"
#include "bridge/native_modern.h"
#include "frame/frame_codec.h"
#include "frame/frame_codec_check.h"
#include <filesystem>
#include <fstream>
namespace hd2d {
inline int run_menu_scale_detection_checks(const AppOptions &options,const char *directory) {
    std::filesystem::create_directories(directory);int checks=0,failures=0,images=0;std::vector<std::string> failed;
    const auto check=[&](bool okay,const char *name){++checks;if(std::getenv("HD2D_CHECK_TRACE"))std::fprintf(stderr,"[scale-check] %d %s\n",checks,name);if(!okay){++failures;failed.emplace_back(name);}};
    check(presentation::run_frame_codec_smoke()==0,"complete frame protocol smoke checks");
    const auto settings_path=(std::filesystem::path(directory)/"settings-fixture.cfg").string();
    for(int i=0;i<5;++i){Hd2dSettings saved;saved.modern_ui=true;saved.modern_font_level=i;Hd2dSettings loaded;
        check(save_settings(settings_path,saved)&&load_settings(settings_path,loaded)&&loaded.modern_font_level==i,"all five font levels survive settings roundtrip");}
    {std::ofstream file(settings_path);file<<"ui_mode=modern\nui_font_level=99\n";}
    Hd2dSettings bounds;check(load_settings(settings_path,bounds)&&bounds.modern_font_level==4,"font setting upper bound");
    {std::ofstream file(settings_path);file<<"ui_mode=modern\n";}
    Hd2dSettings legacy;check(load_settings(settings_path,legacy)&&legacy.modern_font_level==0,"old settings retain current minimum font");
    Hd2dSettings setting;setting.modern_ui=true;FeatureMenu selector;selector.open_features(setting);
    /* 機能メニューの項目の並びはキー列で数えず、名前で文字サイズの項目へ合わせる（`focus_item()`）。
     * 値の変更は左右キーが直接行う（決定は値を変えない。2026-09-22・`command_tabs_checks.h` と同じ）。 */
    const char *font_label=i18n::current()=="ja"?"文字サイズ":"Text size";
    {/* 機能タブの行から「画面」を名前で選び、決定で画面の節へ入る（入口から節へ入る実際の経路） */
        Hd2dSettings route_setting;route_setting.modern_ui=true;FeatureMenu route;route.open_features(route_setting);
        const auto labels=route.command_labels();const std::string screen_label=i18n::tr("hd2d.ui.feature-menu.screen");
        const auto at=std::find(labels.begin(),labels.end(),screen_label);
        if(at!=labels.end()){for(auto steps=at-labels.begin();steps>0;--steps)route.handle(MenuNav::Down,route_setting);route.handle(MenuNav::Confirm,route_setting);}
        check(at!=labels.end()&&route.is_open()&&!route.showing_command_tabs()&&route.tab_navigation_active(),"Features tab opens the Screen section by label");
    }
    const bool font_focused=selector.focus_item(font_label);
    if(font_focused)selector.handle(MenuNav::Right,setting);
    check(font_focused&&setting.modern_font_level==1,"font setting is reachable from Features Screen");
    for(int i=0;i<8;++i)selector.handle(MenuNav::Right,setting);check(setting.modern_font_level==4,"font selector stops at fifth level");
    for(int i=0;i<8;++i)selector.handle(MenuNav::Left,setting);check(setting.modern_font_level==0,"font selector never shrinks below current size");
    selector.handle(MenuNav::Confirm,setting);
    GameFrame sensed;sensed.minimap.width=4;sensed.minimap.height=2;sensed.minimap.kinds.assign(8,0);sensed.minimap.markers={0,1,2,3,4,0,0,0};
    GameFrame decoded;std::string error;
    check(presentation::decode_game_frame(presentation::encode_game_frame(sensed),decoded,error)&&decoded.minimap.markers==sensed.minimap.markers,"detection markers frame roundtrip");
    check(decoded.minimap.at(1,0)==MinimapKind::Unknown&&decoded.minimap.marker_at(1,0)==MinimapMarker::Treasure,"treasure detection does not disclose underlying terrain");
    auto empty=sensed;empty.minimap.markers.clear();check(presentation::decode_game_frame(presentation::encode_game_frame(empty),decoded,error)&&decoded.minimap.marker_at(1,0)==MinimapMarker::None,"old minimap without markers still loads");
    auto bad=sensed;bad.minimap.markers.pop_back();check(!presentation::decode_game_frame(presentation::encode_game_frame(bad),decoded,error),"incorrect marker array length is rejected");
    bad=sensed;bad.minimap.markers[0]=9;check(!presentation::decode_game_frame(presentation::encode_game_frame(bad),decoded,error),"unknown marker type is rejected");
    const auto gold=minimap_marker_color(MinimapMarker::Treasure),item=minimap_marker_color(MinimapMarker::Item),enemy=minimap_marker_color(MinimapMarker::Monster);
    check(gold.r>.9f&&gold.b>.6f&&gold.g<.5f,"treasure color is pink");check(item.b>.9f&&item.g>.7f&&item.r<.6f,"item color is light blue");check(enemy.r>.9f&&enemy.g<.3f&&enemy.b<.3f,"enemy color is red");
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1600;capture.window_h=900;
    Window window;if(!create_window(capture,window,error)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;UiImagePainter painter;VoxelRenderer icons;SlabLibrary slabs;
    slabs.set_dir(options.voxel_dir+"/slab");
    if(!paint.init(error)||!text.init(16,error)||!painter.init(error)||!icons.init(error)){SDL_Quit();return 1;}
    int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);UiLayout layout;layout.screen_w=w;layout.screen_h=h;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();layout.content={0,0,w,h};layout.scene=layout.content;
    const auto start=[&](){gl::glViewport(0,0,w,h);gl::glClearColor(.025f,.035f,.05f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(w,h);text.begin(w,h);painter.begin(w,h);};
    presentation::AssetManifestMessage manifest;manifest.root=std::filesystem::current_path().string();manifest.assets={{1427,"K",60,"tilework/sfc/K60.png"}};TileCatalog catalog;catalog.apply(manifest);
    const auto finish=[&](const std::string &name,ModernStoreUi *menu=nullptr){paint.flush();if(menu)menu->draw_voxels(icons,slabs,catalog,layout,&text);text.flush();check(gl::drain_gl_errors().empty(),"scaled draw has no GL errors");check(save_framebuffer_bmp((std::filesystem::path(directory)/(name+".bmp")).string(),w,h),"scaled screenshot saved");++images;};
    GameFrame stock;stock.menu_open=true;stock.modern.version=4;stock.modern.kind="store";stock.modern.stage="browse";stock.modern.revision=1;stock.modern.page_size=12;stock.hud.gold=20000;
    for(int i=0;i<40;++i){ModernRow row;row.id="item-"+std::to_string(i);row.slot=i;row.label="武器の表示例 "+std::to_string(i+1);row.weight="3.0 kg";row.price=1200+i;row.count=1;row.tile=1427;row.hotkey='a'+i%12;row.description="拡大された説明文です。名前・重さ・単価・アイコン・行の高さを同じ倍率で表示します。長い説明は折り返しとスクロールで読むことができます。";row.effects={"基礎ダメージ: 2d6 (2-12)","命中補正 +0 / ダメージ補正 +0"};row.actions={{"buy","買う","",true,'p'}};stock.modern.rows.push_back(row);}
    std::vector<PadCommand> commands;
    const std::vector<std::string> groups={"魔法/特殊能力","行動","道具(使用)","道具(その他)","装備","扉/箱","情報","設定","その他"};
    for(int g=0;g<9;++g)for(int i=0;i<12;++i)commands.push_back({100+g*12+i,'a'+i,groups[g],"コマンドの表示例 "+std::to_string(i+1),{'a'+i}});
    for(int level=0;level<5;++level) {
        const float scale=menu_font_scale(level);text.set_layout_scale(scale);
        const auto logical=menu_logical_layout(layout,scale,&text);const auto box=fixed_menu_rect(logical),physical=menu_scaled_rect(box,scale);
        check(text.raster_font_px()==int(16*scale+.5f),"font is rasterized at the selected pixel size");
        check(physical.x>=0&&physical.y>=0&&physical.x+physical.w<=w&&physical.y+physical.h<=h,"scaled panel stays inside viewport");
        const auto range=menu_header_visible_range(text,box,groups,7);check(range.first<=7&&7<range.first+range.second,"scaled tabs keep selected category visible");
        text.set_layout_scale(1.f);
        ModernStoreUi pointer;pointer.sync(stock,true);pointer.set_font_level(level);presentation::InputEventsMessage out;
        const int px=int((box.x+100)*scale),py=int((box.y+88)*scale);
        pointer.click(layout,px,py,out);pointer.handle(CursorNav::Confirm,out);pointer.handle(CursorNav::Confirm,out);
        check(out.events.size()==1&&out.events[0].target=="item-0","scaled pointer selects the exact item");pointer.shutdown();
        GameFrame editing=stock;editing.modern.stage="text";editing.text_input_active=true;ModernStoreUi editor;editor.sync(editing,true);editor.set_font_level(level);const auto ime=editor.text_edit_rect(layout);
        check(ime.w>0&&ime.h>=38&&ime.x>=physical.x&&ime.x+ime.w<=physical.x+physical.w+1,"scaled IME field matches panel bounds");editor.shutdown();
        for(int theme=0;theme<3;++theme) {
            const std::string prefix=std::string(theme==0?"A":theme==1?"B":"C")+"-"+std::to_string(level+1);
            ModernStoreUi menu;menu.sync(stock,true);menu.set_font_level(level);start();menu.draw(paint,text,layout,stock,theme);finish(prefix+"-store",&menu);
            setting.modern_theme=theme;setting.modern_font_level=level;FeatureMenu tabs;tabs.set_pad_state(commands,true,"check");tabs.open_commands(setting);
            start();tabs.draw(paint,text,layout,setting);finish(prefix+"-commands");
            FeatureMenu options_menu;options_menu.open_features(setting);options_menu.handle(MenuNav::Down,setting);options_menu.handle(MenuNav::Confirm,setting);options_menu.handle(MenuNav::Down,setting);
            start();options_menu.draw(paint,text,layout,setting);finish(prefix+"-settings");
            menu.shutdown();
        }
        const std::vector<std::string> cases={"Frox shop","Frox building","Sil-Q title","Sil-Q after death","Gensoband commands","Tangband / Hengband menu"};
        for(size_t which=0;which<cases.size();++which) {
            GameFrame plain;plain.menu_open=true;plain.pre_game_menu=which==2||which==3;plain.title_screen=which==2;
            const std::vector<std::string> content={cases[which],"a) 日本語の項目 / Item","b) 次の項目 / Next"};
            for(size_t row=0;row<content.size();++row){TermMirrorLine line;line.source_row=static_cast<int>(row);line.text_utf8=content[row];line.color=1;plain.menu_term_lines.push_back(line);}
            plain.menu_choices={{1,0,static_cast<int>(content[1].size()),0,1,'a'},{2,0,static_cast<int>(content[2].size()),0,1,'b'}};
            plain.menu_term_lines[1].color_spans={{0,3,11},{3,static_cast<int>(content[1].size())-3,1}};
            text.set_layout_scale(scale);
            const auto whole=native_menu_view(plain,text,0,0,10,800);
            check(whole.menu_term_lines[1].text_utf8==content[1],"zero horizontal scroll preserves Japanese and English text");
            check(whole.menu_choices[0].span_len==static_cast<int>(content[1].size())&&whole.menu_choices[0].key=='a',"zero scroll preserves full choice span and key");
            check(whole.menu_term_lines[1].color_spans[1].len>0,"zero scroll preserves colored text spans");
            const auto shifted=native_menu_view(plain,text,0,2,10,800);
            check(shifted.menu_term_lines[1].text_utf8!=content[1]&&!shifted.menu_term_lines[1].text_utf8.empty(),"positive horizontal scroll skips only a prefix");
            const auto returned=native_menu_view(plain,text,0,0,10,800);
            check(returned.menu_term_lines[1].text_utf8==content[1],"returning horizontal scroll to zero restores full text");
            check(native_menu_view(plain,text,0,-1,10,800).menu_term_lines[1].text_utf8==content[1],"negative horizontal offset does not erase text");
            check(plain.menu_term_lines[1].text_utf8==content[1],"viewport never mutates original frame text");
            text.set_layout_scale(1.f);
            presentation::NativeModernMenu adapter;adapter.set_enabled(true);adapter.set_language("ja");adapter.capture(plain);
            HudState plain_state;plain_state.window_theme=plain_state.modern_theme=static_cast<int>(which%3);plain_state.modern_font_level=level;plain_state.cursors.sync(plain);
            plain_state.modern.sync(plain,true);plain_state.modern.set_font_level(level);
            start();draw_game_ui(paint,text,layout,plain,plain_state,nullptr);finish("zero-"+std::to_string(which)+"-"+std::to_string(level+1));
            const auto &painted=plain_state.modern.painted_content();
            const auto item=std::find_if(painted.begin(),painted.end(),[](const auto &cell){return cell.text.find("日本語の項目 / Item")!=std::string::npos&&!cell.target.empty();});
            check(item!=painted.end(),"real modern HUD preserves Japanese and English choice text");
            presentation::InputEventsMessage picked;
            if(item!=painted.end()) {
                plain_state.modern.handle(CursorNav::Down,picked);picked.events.clear();
                const auto physical=menu_scaled_rect(item->rect,scale);
                plain_state.modern.click(layout,physical.x+physical.w/2,physical.y+physical.h/2,picked);
                plain_state.modern.handle(CursorNav::Confirm,picked);
            }
            check(item!=painted.end()&&picked.events.size()==1&&picked.events[0].target==item->target,"interpreted native choice remains clickable at every size");
        }
        GameFrame native;native.menu_open=true;
        for(int i=0;i<40;++i){TermMirrorLine line;line.source_row=i;line.text_utf8=i==39?std::string(180,' ')+"a) Target":"Native menu row "+std::to_string(i);line.color=1;native.menu_term_lines.push_back(line);}
        native.menu_choices={{39,180,9,180,1,'a'}};
        presentation::NativeModernMenu adapter;adapter.set_enabled(true);adapter.set_language("en");adapter.capture(native);
        HudState state;state.window_theme=0;state.modern_font_level=level;state.cursors.sync(native);state.modern.sync(native,true);state.modern.set_font_level(level);
        start();draw_game_ui(paint,text,layout,native,state,nullptr);finish("native-"+std::to_string(level+1));
        const auto &painted=state.modern.painted_content();const auto item=std::find_if(painted.begin(),painted.end(),[](const auto &cell){return cell.text.find("Target")!=std::string::npos&&!cell.target.empty();});
        check(item!=painted.end(),"native item far to the right stays visible after interpretation");
        presentation::InputEventsMessage picked;
        if(item!=painted.end()){const auto physical=menu_scaled_rect(item->rect,scale);state.modern.click(layout,physical.x+physical.w/2,physical.y+physical.h/2,picked);state.modern.handle(CursorNav::Confirm,picked);}
        check(item!=painted.end()&&picked.events.size()==1&&picked.events[0].target==item->target,"scaled interpreted click preserves native choice identity");
    }
    auto compact=layout;compact.screen_w=1280;compact.screen_h=720;compact.content={0,0,1280,720};compact.scene=compact.content;
    setting.modern_font_level=4;setting.modern_theme=0;
    ModernStoreUi small;small.sync(stock,true);small.set_font_level(4);
    presentation::InputEventsMessage scrolled;
    for(int i=0;i<30;++i)small.handle(CursorNav::Down,scrolled);
    start();small.draw(paint,text,compact,stock,0);paint.flush();small.draw_voxels(icons,slabs,catalog,compact,&text);finish("compact-5-store");
    small.handle(CursorNav::Confirm,scrolled);small.handle(CursorNav::Confirm,scrolled);
    check(scrolled.events.size()==1&&scrolled.events[0].target=="item-30","large-font scroll preserves selected item identity");small.shutdown();
    FeatureMenu compact_commands;compact_commands.set_pad_state(commands,true,"check");compact_commands.open_commands(setting);
    start();compact_commands.draw(paint,text,compact,setting);finish("compact-5-commands");
    FeatureMenu compact_settings;compact_settings.open_features(setting);compact_settings.handle(MenuNav::Down,setting);compact_settings.handle(MenuNav::Confirm,setting);compact_settings.handle(MenuNav::Down,setting);
    start();compact_settings.draw(paint,text,compact,setting);finish("compact-5-settings");
    GameFrame map;map.minimap.width=16;map.minimap.height=10;map.minimap.kinds.assign(160,0);map.minimap.markers.assign(160,0);map.minimap.player_gx=8;map.minimap.player_gy=5;map.minimap.kinds[88]=7;
    map.minimap.markers[33]=1;map.minimap.markers[54]=2;map.minimap.markers[105]=3;map.minimap.markers[125]=4;
    auto map_layout=layout;map_layout.minimap={160,120,520,340};HudState map_state;map_state.minimap_cell_px=24;
    start();draw_game_ui(paint,text,map_layout,map,map_state,nullptr);text.draw(200,400,"財宝: ピンク   アイテム: 水色   敵: 赤   罠: 黄",{},1200);finish("detection-map");
    slabs.shutdown(&icons);icons.shutdown();paint.shutdown();text.shutdown();painter.shutdown();SDL_Quit();
    std::ofstream report(std::filesystem::path(directory)/"result.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"images\":"<<images<<",\"game_core_started\":false,\"failed\":[";
    for(size_t i=0;i<failed.size();++i)report<<(i?",":"")<<'"'<<failed[i]<<'"';report<<"]}\n";
    return failures?1:0;
}
}
