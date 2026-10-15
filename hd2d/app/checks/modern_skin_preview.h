#pragma once
#include "app/app_support.h"
#include "app/checks/check_support.h"
#include "ui/modern_store_ui.h"
#include "ui/modern_skin.h"
#include "ui/feature_menu.h"
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace hd2d {
// Render-only: no core process, settings load/save, game input, or save data.
inline int render_modern_skin_preview(const AppOptions &options,const char *directory) {
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1360;capture.window_h=900;
    Window window;std::string err;
    if(!create_window(capture,window,err)){std::fprintf(stderr,"%s\n",err.c_str());SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;
    if(!paint.init(err)||!text.init(18,err)){std::fprintf(stderr,"%s\n",err.c_str());SDL_Quit();return 1;}
    int width=0,height=0;SDL_GL_GetDrawableSize(window.window,&width,&height);
    UiLayout layout;layout.screen_w=width;layout.screen_h=height;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();layout.content={0,0,width,height};layout.scene=layout.content;
    std::filesystem::create_directories(directory);
    int failures=0;
    auto start=[&](){
        gl::glViewport(0,0,width,height);gl::glClearColor(.018f,.025f,.035f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(width,height);text.begin(width,height);
        if(std::getenv("HD2D_FROSTED_PREVIEW")) {
            for(int y=0;y<height;y+=52)for(int x=0;x<width;x+=88) {
                const float variation=((x/88+y/52)%3)*.012f;
                paint.rect({x+2,y+2,84,48},{.13f+variation,.17f+variation,.23f+variation,1});
            }
            paint.rect({width/2-35,120,70,height-240},{.12f,.30f,.43f,1});
            paint.rect({width/4,180,24,100},{.70f,.32f,.08f,1});
            paint.flush();paint.begin(width,height);
        }
    };
    auto save=[&](const std::string &name){paint.flush();text.flush();const auto error=gl::drain_gl_errors();if(!error.empty()){std::fprintf(stderr,"%s: %s\n",name.c_str(),error.c_str());++failures;}if(!save_framebuffer_bmp((std::filesystem::path(directory)/(name+".bmp")).string(),width,height))++failures;};
    for(int theme=0;theme<3;++theme) {
        const std::string prefix=theme==0?"A":(theme==1?"B":"C");
        GameFrame frame;frame.menu_open=true;frame.modern.version=3;frame.modern.kind="store";frame.modern.stage="browse";frame.modern.revision=1;frame.hud.gold=2840;
        const char *names[]={"クローク [1,+0]","軟革よろい [4,+2]","金属製スモール・シールド [5,+0]","硬革ブーツ [3,+0]","革グローブ [1,+1]","鉄ヘルメット [5,+0]","鋲付軟革よろい [5,+0]","金属製ラージ・シールド [8,+1]"};
        for(int i=0;i<8;++i) {
            ModernRow row;row.id="preview-"+std::to_string(i);row.label=names[i];row.count=1;row.price=120+i*160;row.weight="1.0 kg";row.slot=i;
            row.effects={"[1,+0]：基本防御力 1 ／ 防御力補正 +0"};
            row.description="装飾と文字の読みやすさを確認するための表示例です。\n\n選択中の品物の説明がここに表示されます。\n一覧と説明欄は同じ位置に固定し、長い説明はスクロールします。";
            row.available=i!=5;row.reason=i==5?"所持金が足りません。":"";
            row.actions={{"buy","買う",row.reason,row.available},{"preview","選択肢の表示例","",true}};frame.modern.rows.push_back(row);
        }
        ModernStoreUi store;store.sync(frame,true);start();store.draw(paint,text,layout,frame,theme);save(prefix+"-store");
        presentation::InputEventsMessage ignored;store.handle(CursorNav::Confirm,ignored);start();store.draw(paint,text,layout,frame,theme);save(prefix+"-popup");
        Hd2dSettings settings;settings.modern_ui=true;settings.modern_theme=theme;FeatureMenu menu;
        std::vector<PadCommand> commands;
        const char *labels[]={"歩く","走る","休む","待機する","調べる","探す","扉を開ける","扉を閉める","階段を上がる","階段を下りる"};
        for(int i=0;i<10;++i){PadCommand c;c.id=i+1;c.group_utf8="行動";c.label_utf8=labels[i];c.keys={97+i};commands.push_back(c);}
        for(int i=0;i<2;++i){PadCommand c;c.id=20+i;c.group_utf8=i==0?"持ち物":"情報";c.label_utf8=i==0?"持ち物を見る":"能力を見る";c.keys={105+i};commands.push_back(c);}
        menu.set_pad_state(commands,true,"preview");menu.open_commands(settings);start();menu.draw(paint,text,layout,settings);save(prefix+"-commands");
        menu.open_features(settings);start();menu.draw(paint,text,layout,settings);save(prefix+"-features");
        store.shutdown();
    }
    // The captured background must really affect the glass, rather than being a painted imitation.
    start();paint.rect({0,0,200,200},{.8f,.05f,.05f,1});paint.rect({200,0,200,200},{.05f,.8f,.05f,1});paint.flush();paint.begin(width,height);
    paint.surface({0,0,400,200},modern_skin(0).background,UiMaterial::Glass);paint.flush();
    const auto pixels=read_framebuffer(width,height);int left[3],right[3];pixel_at(pixels,width,height,100,100,left);pixel_at(pixels,width,height,300,100,right);
    if(!(left[0]>right[0]+12&&right[1]>left[1]+12))++failures;
    if(!gl::drain_gl_errors().empty())++failures;
    std::ofstream report(std::filesystem::path(directory)/"result.json");
    report << "{\"images\":12,\"render_errors\":" << failures << ",\"backdrop_check\":true,\"game_core_started\":false}\n";
    report.close();
    paint.shutdown();text.shutdown();SDL_Quit();
    std::fprintf(stderr,"Menu skin preview: 12 images, %d errors. No game core started.\n",failures);
    return failures?1:0;
}
}
