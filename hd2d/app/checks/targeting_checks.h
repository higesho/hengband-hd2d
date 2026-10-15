#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/game_hud.h"
#include "render/targeting_renderer.h"
#include "frame/cell_feature_bits.h"
#include <filesystem>
#include <fstream>
namespace hd2d {
inline int run_targeting_checks(const AppOptions &options,const char *directory) {
    std::filesystem::create_directories(directory);int checks=0,failures=0,images=0;
    const auto check=[&](bool okay,const char *label){++checks;if(!okay){++failures;std::fprintf(stderr,"targeting check: %s\n",label);}};
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1280;capture.window_h=800;
    Window window;std::string error;if(!create_window(capture,window,error)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;TargetingRenderer renderer;
    if(!paint.init(error)||!text.init(24,error)||!renderer.init(error)){std::fprintf(stderr,"%s\n",error.c_str());SDL_Quit();return 1;}
    int width,height;SDL_GL_GetDrawableSize(window.window,&width,&height);
    GameFrame frame;frame.cam_x=10;frame.cam_y=10;
    for(int y=0;y<25;++y)for(int x=0;x<40;++x) {
        frame.cells.emplace_back();auto &cell=frame.cells.back();cell.gx=static_cast<int16_t>(x);cell.gy=static_cast<int16_t>(y);
        cell.feature_flags=CELL_FEAT_KNOWN;cell.ascii_fallback=(x==4&&y==10)?'@':(y==10&&(x==8||x==13)?'o':'.');cell.fg_color=1;
    }
    auto &s=frame.targeting;s.active=true;s.scope=1;s.revision=1;s.width=40;s.height=25;s.kind="attack";s.mode="point";
    s.source={4,10};s.selected={13,10};s.selected_id=1;s.can_confirm=true;
    s.markers={{1,{13,10},true,true,false,true},{2,{8,10},true,false,true,true},{0,{10,13},false,false,false,false}};
    TargetingPath path;for(int x=4;x<=8;++x)path.points.push_back({x,10});s.paths.push_back(path);
    path.points.clear();path.guide=true;for(int x=8;x<=13;++x)path.points.push_back({x,10});s.paths.push_back(path);
    for(int y=8;y<=12;++y)for(int x=6;x<=10;++x)s.area.push_back({{x,y},x==10});
    TargetingUi ui;const RectPx panel{17,29,width-34,height-58};
    for(const auto *kind:{"attack","support","neutral"})for(int tick=0;tick<2;++tick) {
        s.kind=kind;ui.sync(s);gl::glViewport(0,0,width,height);gl::glClearColor(.02f,.02f,.03f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);
        paint.begin(width,height);text.begin(width,height);
        const auto area=draw_ascii_map_panel(paint,text,panel,frame,&ui,tick*.05);
        paint.flush();text.flush();check(gl::drain_gl_errors().empty(),"ASCII draw GL errors");
        TargetingPoint point;const auto &map=ui.map_projection;
        check(map.pick(map.x+(13-map.cam_x+map.cols/2)*map.cell_w+map.cell_w/2,map.y+(10-map.cam_y+map.rows/2)*map.cell_h+map.cell_h/2,point)&&point==s.selected,"actual font projection returns selected cell");
        check(area.w>0,"ASCII map drawn");
        check(save_framebuffer_bmp((std::filesystem::path(directory)/(std::string("ascii-")+kind+"-"+std::to_string(tick)+".bmp")).string(),width,height),"ASCII capture");++images;
    }
    Camera camera;camera.target={9,10,0};camera.distance=11;camera.pitch=.78f;camera.viewport_w=width;camera.viewport_h=height;
    const std::vector<TargetingEntity> bodies={{1,13,10,13.5f,10.5f,0,1,1.5f},{2,8,10,8.5f,10.5f,0,1,1.2f}};
    s.kind="attack";
    for(int tick=0;tick<2;++tick) {
        gl::glClearColor(.07f,.08f,.1f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT|gl::GL_DEPTH_BUFFER_BIT);
        const auto visual=build_targeting_visual(s,camera.eye(),bodies,tick*.05);
        renderer.draw(camera.view_projection(),visual.area,true);renderer.draw(camera.view_projection(),visual.dots,true);renderer.draw(camera.view_projection(),visual.reticles,false);
        check(gl::drain_gl_errors().empty(),"3D shader and draw GL errors");
        check(save_framebuffer_bmp((std::filesystem::path(directory)/("world-"+std::to_string(tick)+".bmp")).string(),width,height),"3D capture");++images;
    }
    renderer.shutdown();paint.shutdown();text.shutdown();SDL_Quit();
    std::ofstream report(std::filesystem::path(directory)/"result.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"images\":"<<images<<",\"game_core_started\":false}\n";
    return failures?1:0;
}
}
