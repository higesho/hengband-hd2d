#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/game_hud.h"
#include "ui/modern_skin.h"
#include "ui/feature_menu.h"
#include <filesystem>
#include <fstream>

namespace hd2d {
inline int render_panel_skin_preview(const AppOptions &options,const char *directory) {
    std::filesystem::create_directories(directory);int failures=0,checks=0;
    auto check=[&](bool okay){++checks;if(!okay){++failures;std::fprintf(stderr,"panel skin check %d failed\n",checks);}};
    // New installations use modern UI theme A : default is theme 0, not -1.
    Hd2dSettings settings;check(settings.modern_ui&&settings.window_theme()==0);
    const auto file=(std::filesystem::path(directory)/"settings-check.cfg").string();
    for(int i=0;i<4;++i) {
        settings.modern_ui=i<3;settings.modern_theme=i<3?i:0;
        check(save_settings(file,settings));Hd2dSettings loaded;check(load_settings(file,loaded));
        check(loaded.window_theme()==(i<3?i:-1));
    }
    {std::ofstream legacy(file);legacy<<"ui_mode=modern\nui_theme=parchment\nmain_panel_theme=dark\nsub_panel_theme=japanese,dark,parchment\n";}
    Hd2dSettings legacy;check(load_settings(file,legacy));check(legacy.window_theme()==1);check(save_settings(file,legacy));
    {std::ifstream saved(file);std::string content((std::istreambuf_iterator<char>(saved)),std::istreambuf_iterator<char>());check(content.find("main_panel_theme=")==std::string::npos&&content.find("sub_panel_theme=")==std::string::npos);}
    const bool ja=i18n::current()=="ja";FeatureMenu settings_menu;settings_menu.open_features(settings);
    check(settings_menu.focus_item(ja?"ウィンドウのデザイン":"Window design"));
    settings.modern_ui=false;
    for(int i=0;i<4;++i){settings_menu.handle(MenuNav::Right,settings);check(settings.window_theme()==(i<3?i:-1));}
    settings_menu.handle(MenuNav::Left,settings);check(settings.window_theme()==2);
    check(!settings_menu.focus_item(ja?"メイン画面のデザイン":"Main screen design"));
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1360;capture.window_h=900;
    Window window;std::string err;if(!create_window(capture,window,err)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;if(!paint.init(err)||!text.init(18,err)){SDL_Quit();return 1;}
    int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);
    GameFrame frame=make_synthetic_frame(80,60,40,30);fill_synthetic_ui(frame);frame.menu_open=false;frame.pre_game_menu=false;frame.menu_term_lines.clear();frame.prompt={};
    for(auto &cell:frame.cells) {
        const int dx=cell.gx-frame.player_gx,dy=cell.gy-frame.player_gy;
        const bool room=std::abs(dx)<20&&std::abs(dy)<10;
        const bool edge=std::abs(dx)==20||std::abs(dy)==10;
        cell.ascii_fallback=room?'.':(edge?'#':' ');cell.fg_color=room?2:7;
        if(dx==0&&dy==0){cell.ascii_fallback='@';cell.fg_color=1;}
        if(dx==7&&dy==2){cell.ascii_fallback='o';cell.fg_color=13;}
        if(dx==-8&&dy==-3){cell.ascii_fallback='!';cell.fg_color=12;}
    }
    for(int i=0;i<kUiSubPanels;++i){frame.sub_panels[i].kind=0;frame.sub_panels[i].title_utf8="Panel "+std::to_string(i+1);frame.sub_panels[i].lines={{"表示内容は同じまま、デザインだけ変更",1},{"HP 120 / 160",13},{"状態：通常",11},{"クローク [1,+0]",9},{"メッセージと色の表示例",14}};}
    SubSplit split;split.bottom_count=4;split.right_count=3;
    const auto layout=UiLayout::compute(LayoutMode::Split,w,h,text.cell_w(),text.cell_h(),true,split);
    auto start=[&](){gl::glViewport(0,0,w,h);gl::glClearColor(.018f,.025f,.035f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(w,h);text.begin(w,h);};
    auto save=[&](const std::string &name){paint.flush();text.flush();check(gl::drain_gl_errors().empty());check(save_framebuffer_bmp((std::filesystem::path(directory)/(name+".bmp")).string(),w,h));};
    for(int mode=0;mode<4;++mode) {
        HudState state;state.subs_open=true;state.window_theme=mode<3?mode:-1;
        start();draw_ascii_map_panel(paint,text,layout.scene,frame);paint.flush();text.flush();
        const auto before=read_framebuffer(w,h);paint.begin(w,h);text.begin(w,h);
        draw_game_ui(paint,text,layout,frame,state);save(mode==0?"A-panels":(mode==1?"B-panels":(mode==2?"C-panels":"original-panels")));
        const auto after=read_framebuffer(w,h);
        // The bundled CJK font can make the minimap taller than the Windows
        // font. Exclude that intentional overlay and its floor label.
        const int sample_y=std::max(layout.scene.y+layout.scene.h/2+32,
            layout.minimap.y+layout.minimap.h+2*text.cell_h());
        const RectPx centre{layout.scene.x+layout.scene.w/2-48,sample_y,96,48};
        const unsigned char black[3]={0,0,0};check(count_painted_pixels(before,w,h,centre,black)>20);
        check(count_changed_pixels(before,after,w,h,centre)==0);
    }
    start();UiLayout menu_layout=layout;menu_layout.content={0,0,w,h};settings_menu.draw(paint,text,menu_layout,settings);save("unified-settings");
    // Transparent-panel policy must leave the centre untouched, including parchment.
    start();paint.rect({0,0,w,h},{.17f,.23f,.31f,1});paint.flush();const auto before=read_framebuffer(w,h);
    paint.begin(w,h);set_panel_transparent(true);paint_modern_window(paint,{20,20,240,180},1,true,!panel_transparent());paint.flush();set_panel_transparent(false);
    const auto after=read_framebuffer(w,h);int a[3],b[3];pixel_at(before,w,h,120,100,a);pixel_at(after,w,h,120,100,b);check(a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]);
    std::ofstream result(std::filesystem::path(directory)/"result.json");result<<"{\"images\":5,\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"game_core_started\":false}\n";result.close();
    paint.shutdown();text.shutdown();SDL_Quit();return failures?1:0;
}
}
