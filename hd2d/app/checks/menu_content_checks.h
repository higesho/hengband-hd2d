#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/modern_store_ui.h"
#include "ui/menu_scale.h"
#include "ui/menu_frame.h"
#include "frame/frame_codec.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
namespace hd2d {
inline int run_menu_content_checks(const AppOptions &options,const char *directory) {
    namespace fs=std::filesystem;using Json=nlohmann::json;
    const fs::path dir(directory);fs::create_directories(dir);int checks=0,failures=0,images=0;Json results=Json::array();
    Json fixtures;{std::ifstream stream(dir/"frames.json");stream>>fixtures;}
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1600;capture.window_h=900;
    Window window;std::string error;if(!create_window(capture,window,error)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;if(!paint.init(error)||!text.init(16,error)){SDL_Quit();return 1;}
    const auto overlap=[](const RectPx &a,const RectPx &b){return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;};
    for(const auto &size:std::initializer_list<std::pair<int,int>>{{1600,900},{800,600},{720,1080}}) {
        SDL_SetWindowSize(window.window,size.first,size.second);SDL_PumpEvents();
        int width=0,height=0;SDL_GL_GetDrawableSize(window.window,&width,&height);
        UiLayout layout;layout.screen_w=width;layout.screen_h=height;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();layout.content={0,0,width,height};layout.scene=layout.content;
        for(const auto &fixture:fixtures)for(int level:{0,2,4})for(int theme=0;theme<3;++theme) {
            if(fixture.value("width",width)!=width||fixture.value("height",height)!=height||fixture.value("font",level)!=level||fixture.value("theme",theme)!=theme)continue;
            const std::string name=fixture.at("name");Json result{{"name",name},{"width",width},{"height",height},{"font",level},{"theme",theme},{"errors",Json::array()},{"cells",Json::array()}};
            const auto check=[&](bool okay,const std::string &what){++checks;if(!okay){++failures;result["errors"].push_back(what);std::fprintf(stderr,"FAIL %s %dx%d font%d theme%d: %s\n",name.c_str(),width,height,level,theme,what.c_str());}};
            GameFrame frame;if(!presentation::decode_game_frame(fixture.at("frame").dump(),frame,error)){check(false,"frame decode: "+error);results.push_back(result);continue;}
            check(frame.modern.version>=5&&!frame.modern.content_type.empty(),"semantic content model exists");
            const float scale=menu_font_scale(level);ModernStoreUi menu;menu.sync(frame,true);menu.set_font_level(level);
            gl::glViewport(0,0,width,height);gl::glClearColor(.025f,.035f,.05f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(width,height);text.begin(width,height);
            menu.draw(paint,text,layout,frame,theme);paint.flush();text.flush();
            check(gl::drain_gl_errors().empty(),"no GL errors");
            text.set_layout_scale(scale);const auto logical=menu_logical_layout(layout,scale,&text);const auto box=fixed_menu_rect(logical);text.set_layout_scale(1.f);
            const auto cells=menu.painted_content();int selected=0;
            for(size_t i=0;i<cells.size();++i) {
                const auto &c=cells[i];const auto r=c.rect;
                check(r.w>0&&r.h>0&&r.x>=box.x&&r.y>=box.y&&r.x+r.w<=box.x+box.w&&r.y+r.h<=box.y+box.h,"cell stays inside the menu: "+c.text);
                for(size_t j=0;j<i;++j)check(!overlap(r,cells[j].rect),"cells do not overlap: "+c.text+" / "+cells[j].text);
                if(c.selected)++selected;
                result["cells"].push_back({{"text",c.text},{"target",c.target},{"selected",c.selected},{"x",r.x},{"y",r.y},{"w",r.w},{"h",r.h}});
            }
            if(frame.modern.stage=="cursor"&&!frame.modern.cursor_target.empty())check(selected==1,"one selected cell remains visible");
            if(frame.modern.stage=="choices"&&!frame.modern.rows.empty())check(selected==1,"one selected choice remains visible");
            if(frame.modern.stage=="text") {
                const auto input=menu.text_edit_rect(layout);const auto panel=menu_scaled_rect(box,scale);
                check(input.x>=panel.x&&input.y>=panel.y&&input.x+input.w<=panel.x+panel.w&&input.y+input.h<=panel.y+panel.h,"editable field stays inside the menu");
                for(const auto &c:cells)check(!overlap(menu_scaled_rect(c.rect,scale),input),"content does not overlap the editable field");
            }
            const auto filename=name+"-"+std::to_string(width)+"x"+std::to_string(height)+"-font"+std::to_string(level)+"-theme"+std::to_string(theme)+".bmp";
            check(save_framebuffer_bmp((dir/filename).string(),width,height),"screenshot saved");++images;result["image"]=filename;menu.shutdown();results.push_back(std::move(result));
        }
    }
    Json report{{"checks",checks},{"failures",failures},{"images",images},{"game_core_started",false},{"results",results}};
    std::ofstream(dir/"result.json")<<report.dump(2);std::fprintf(stderr,"content checks=%d failures=%d images=%d\n",checks,failures,images);
    paint.shutdown();text.shutdown();SDL_Quit();return failures?1:0;
}
}
