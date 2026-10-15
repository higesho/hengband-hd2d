#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/game_hud.h"
#include "ui/menu_scale.h"
#include "ui/menu_frame.h"
#include "frame/cell_feature_bits.h"
#include "bridge/native_modern.h"
#include <filesystem>
#include <fstream>
namespace hd2d {
inline int run_panel_font_sync_checks(const AppOptions &options,const char *directory) {
    std::filesystem::create_directories(directory);int checks=0,failures=0;std::vector<std::string> failed;
    const auto check=[&](bool okay,const char *label){++checks;if(!okay){++failures;failed.emplace_back(label);}};
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1600;capture.window_h=900;
    Window window;std::string error;if(!create_window(capture,window,error)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;if(!paint.init(error)||!text.init(16,error)){SDL_Quit();return 1;}
    int width=0,height=0;SDL_GL_GetDrawableSize(window.window,&width,&height);
    GameFrame frame;frame.hud.hp=120;frame.hud.hp_max=150;frame.hud.sp=20;frame.hud.sp_max=50;frame.hud.gold=1000;frame.hud.name="表示例";
    for(const auto *line:{"人間","戦士","レベル 10","経験 1000","腕力 18/50","知能 12","HP 120/150"})frame.status_col_lines.push_back({line,1});
    for(int panel=0;panel<kUiSubPanels;++panel){auto &content=frame.sub_panels[panel];content.kind=1;content.title_utf8=panel==0?"メッセージ":(panel==1?"装備品":"パネル "+std::to_string(panel+1));for(int row=0;row<30;++row)content.lines.push_back({"項目 "+std::to_string(row+1)+"  ABC 123",1});}
    for(int y=-30;y<=30;++y)for(int x=-80;x<=80;++x){frame.cells.emplace_back();auto &cell=frame.cells.back();cell.gx=x;cell.gy=y;cell.feature_flags=CELL_FEAT_KNOWN;cell.ascii_fallback=(x==0&&y==0)?'@':((x%12==0||y%9==0)?'#':'.');cell.fg_color=(x==0&&y==0)?11:1;}
    int previous_status=0,previous_cols=100000,previous_document_rows=100000,images=0;
    for(int level=0;level<5;++level) {
        Hd2dSettings settings;settings.modern_ui=true;settings.modern_font_level=level;const float scale=menu_font_scale(level);
        const int cell_w=int(text.cell_w()*scale+.5f),cell_h=int(text.cell_h()*scale+.5f);
        const auto layout=UiLayout::compute(LayoutMode::Split,width,height,cell_w,cell_h,true);
        check(layout.status_col.w>=previous_status,"status column expands with shared font");previous_status=layout.status_col.w;
        const auto wire=build_ui_state(ViewWindow{},layout,settings,false);
        check(wire.main_term_rows>=24&&wire.main_term_rows<=previous_document_rows,"native help rows follow the current menu font and viewport");previous_document_rows=wire.main_term_rows;
        text.set_layout_scale(scale);const auto logical=panel_logical_layout(layout,scale,text);
        check(text.raster_font_px()==int(16*scale+.5f),"panel font matches menu font pixels");
        for(int i=0;i<kUiSubPanels;++i)if(!layout.sub[i].empty()&&wire.sub_panel_cells[i].cols>=8) {
            const auto body=panel_body(logical.sub[i],logical.cell_w);
            check(std::abs(wire.sub_panel_cells[i].cols-body.w/logical.cell_w)<=1,"core column request matches scaled panel rendering");
            check(std::abs(wire.sub_panel_cells[i].rows-(body.h/logical.cell_h-1))<=1,"core row request matches scaled panel rendering");
        }
        text.set_layout_scale(1.f);
        TextOverlay map_text;check(map_text.init(int(16*scale+.5f),error),"ASCII map font uses shared level");
        const int cols=layout.scene.w/map_text.cell_w();check(cols<=previous_cols,"main map shows fewer cells for larger font");previous_cols=cols;
        gl::glViewport(0,0,width,height);gl::glClearColor(.025f,.035f,.05f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(width,height);text.begin(width,height);map_text.begin(width,height);
        const auto map_rect=draw_ascii_map_panel(paint,map_text,layout.scene,frame);
        check(map_rect.x>=layout.scene.x&&map_rect.y>=layout.scene.y&&map_rect.x+map_rect.w<=layout.scene.x+layout.scene.w&&map_rect.y+map_rect.h<=layout.scene.y+layout.scene.h,"scaled ASCII map stays inside main panel");
        HudState state;state.window_theme=0;state.modern_font_level=level;draw_game_ui(paint,text,layout,frame,state,nullptr);
        paint.flush();map_text.flush();text.flush();check(gl::drain_gl_errors().empty(),"panel draw has no GL errors");
        check(save_framebuffer_bmp((std::filesystem::path(directory)/("panels-"+std::to_string(level+1)+".bmp")).string(),width,height),"panel image saved");++images;
        GameFrame menu;menu.menu_open=true;TermMirrorLine line;line.text_utf8="a) メニュー項目";line.color=1;menu.menu_term_lines.push_back(line);menu.menu_choices={{0,0,static_cast<int>(line.text_utf8.size()),0,1,'a'}};
        presentation::NativeModernMenu adapter;adapter.set_enabled(true);adapter.set_language("ja");adapter.capture(menu);
        HudState menu_state;menu_state.window_theme=0;menu_state.modern_font_level=level;menu_state.cursors.sync(menu);menu_state.modern.sync(menu,true);menu_state.modern.set_font_level(level);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(width,height);text.begin(width,height);draw_game_ui(paint,text,layout,menu,menu_state,nullptr);paint.flush();text.flush();
        const auto &painted=menu_state.modern.painted_content();const auto item=std::find_if(painted.begin(),painted.end(),[](const auto &cell){return cell.text.find(reinterpret_cast<const char*>(u8"メニュー項目"))!=std::string::npos&&!cell.target.empty();});
        check(item!=painted.end(),"enlarged panel layout does not blank interpreted menus");
        presentation::InputEventsMessage picked;
        if(item!=painted.end()){
            const auto physical=menu_scaled_rect(item->rect,scale);menu_state.modern.click(layout,physical.x+physical.w/2,physical.y+physical.h/2,picked);menu_state.modern.handle(CursorNav::Confirm,picked);
        }
        check(item!=painted.end()&&picked.events.size()==1&&picked.events[0].target==item->target,"interpreted menu click remains aligned with enlarged panel layout");
        check(save_framebuffer_bmp((std::filesystem::path(directory)/("menu-"+std::to_string(level+1)+".bmp")).string(),width,height),"menu alignment image saved");++images;
        map_text.shutdown();
    }
    {
        Hd2dSettings variable;variable.modern_ui=true;variable.modern_font_level=0;
        const auto small=UiLayout::compute(LayoutMode::Full,800,480,text.cell_w(),text.cell_h(),false);
        const auto large=UiLayout::compute(LayoutMode::Full,1600,1000,text.cell_w(),text.cell_h(),false);
        check(build_ui_state(ViewWindow{},large,variable,false).main_term_rows>build_ui_state(ViewWindow{},small,variable,false).main_term_rows,"native help rows increase when the window provides more document height");
    }
    paint.shutdown();text.shutdown();SDL_Quit();
    std::ofstream report(std::filesystem::path(directory)/"result.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"images\":"<<images<<",\"game_core_started\":false,\"failed\":[";
    for(size_t i=0;i<failed.size();++i)report<<(i?",":"")<<'"'<<failed[i]<<'"';report<<"]}\n";return failures?1:0;
}
}
