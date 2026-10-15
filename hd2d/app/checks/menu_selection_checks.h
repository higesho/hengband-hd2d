#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/modern_store_ui.h"
#include "ui/menu_cursor_span.h"
#include "ui/menu_scale.h"
#include "ui/menu_frame.h"
#include "ui/menu_chrome.h"
#include "frame/frame_codec.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
namespace hd2d {
inline int run_menu_selection_checks(const AppOptions &options,const char *directory) {
    namespace fs=std::filesystem;using Json=nlohmann::json;
    const fs::path dir(directory);fs::create_directories(dir);int checks=0,failures=0,images=0;Json results=Json::array();
    const auto check=[&](bool okay,const char *name){++checks;if(!okay){++failures;std::fprintf(stderr,"FAIL: %s\n",name);}};
    Json fixtures;{std::ifstream stream(dir/"frames.json");stream>>fixtures;}
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1600;capture.window_h=900;
    Window window;std::string error;if(!create_window(capture,window,error)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;if(!paint.init(error)||!text.init(16,error)){SDL_Quit();return 1;}
    int width=0,height=0;SDL_GL_GetDrawableSize(window.window,&width,&height);
    UiLayout layout;layout.screen_w=width;layout.screen_h=height;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();layout.content={0,0,width,height};layout.scene=layout.content;
    for(const auto &fixture:fixtures) {
        const std::string name=fixture.at("name");GameFrame frame;
        if(!presentation::decode_game_frame(fixture.at("frame").dump(),frame,error)){check(false,"captured frame decodes");continue;}
        check(frame.modern.stage=="cursor"||frame.modern.stage=="choices","captured menu has a selection");
        for(int level=0;level<5;++level)for(int theme=0;theme<3;++theme) {
            const float scale=menu_font_scale(level);ModernStoreUi menu;menu.sync(frame,true);menu.set_font_level(level);
            gl::glViewport(0,0,width,height);gl::glClearColor(.025f,.035f,.05f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(width,height);text.begin(width,height);
            menu.draw(paint,text,layout,frame,theme);paint.flush();text.flush();
            check(gl::drain_gl_errors().empty(),"selection draw has no GL errors");
            const std::string filename=name+"-font"+std::to_string(level)+"-theme"+std::to_string(theme)+".bmp";
            check(save_framebuffer_bmp((dir/filename).string(),width,height),"selection screenshot saved");++images;
            Json result{{"name",name},{"font",level},{"theme",theme},{"image",filename}};
            if(frame.modern.stage=="cursor"&&frame.modern.cursor_span_begin>=0) {
                text.set_layout_scale(scale);const auto logical=menu_logical_layout(layout,scale,&text);const auto box=fixed_menu_rect(logical);
                std::vector<std::string> lines;size_t at=0;while(at<frame.modern.body.size()){const auto end=frame.modern.body.find('\n',at);lines.push_back(frame.modern.body.substr(at,end==std::string::npos?end:end-at));if(end==std::string::npos)break;at=end+1;}
                const auto &w=frame.modern;check(w.cursor>=0&&w.cursor<int(lines.size()),"selected display row exists");
                if(w.cursor>=0&&w.cursor<int(lines.size())) {
                    auto line=lines[w.cursor];const auto selected=line.substr(w.cursor_span_begin,w.cursor_span_len);
                    check(selected==fixture.at("expected_selection").get<std::string>(),"selected label exactly matches original captured item");
                    const auto clip=RectPx{box.x+20,0,box.w-40,text.cell_h()+3};const int origin=box.x+24;
                    const auto r=menu_cursor_span_rect(text,line,clip,origin,w.cursor_span_begin,w.cursor_span_len);
                    check(r.w>0&&r.w<clip.w,"only the selected item has a highlight");
                    check(r.x>=clip.x&&r.x+r.w<=clip.x+clip.w,"selection is clipped inside its panel");
                    check(menu_cursor_span_rect(text,line,clip,origin-10000,w.cursor_span_begin,w.cursor_span_len).empty(),"offscreen selection is not pinned to the edge");
                    result["selected"]=selected;result["width"]=r.w;result["panel_width"]=clip.w;
                }
                text.set_layout_scale(1.f);
            }
            menu.shutdown();results.push_back(result);
        }
    }
    const std::string multi="a) left    b) "+std::string(reinterpret_cast<const char*>(u8"日本語"))+"    c) right";
    check(menu_cursor_span_rect(text,multi,{0,0,1600,20},0,15,1).empty(),"UTF-8 continuation offset is not rendered");
    check(menu_cursor_span_rect(text,multi,{0,0,1600,20},0,1,500).empty(),"out-of-range span is not rendered");
    check(menu_cursor_span_rect(text,"     ",{0,0,1600,20},0,0,5).empty(),"blank padding is not highlighted");
    Json report{{"checks",checks},{"failures",failures},{"images",images},{"game_core_started",false},{"results",results}};
    std::ofstream(dir/"result.json")<<report.dump(2);std::fprintf(stderr,"selection checks=%d failures=%d images=%d\n",checks,failures,images);
    paint.shutdown();text.shutdown();SDL_Quit();return failures?1:0;
}
}
