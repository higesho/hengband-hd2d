#pragma once
#include "app/app_support.h"
#include "ui/modern_store_ui.h"
#include "ui/menu_scale.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace hd2d {
/*!
 * @brief 文脈メニューのポップアップの検査（AH-58。`ModernStoreUi::context_popup_active()`）。窓と GL は要るがコアは起こさない。
 *
 * @details 地図のクリック・長押しが開く文脈メニューは、コアが `menu_over_map`（地図の上に重なる小さな箱）と言い、
 * 画面がアンカー（押した点）を持っているとき、窓いっぱいの窓ではなく押した点の近くのポップアップで出す。確かめること:
 *   - アンカーが無い・`menu_over_map` が偽・選択肢の段でない、のどれかならポップアップにならない（ほかのコアの窓は今までどおり）
 *   - アンカーの右下へ出し、地図の矩形の端で切れるなら反対側へ寄せる（4 つの端・角・狭い地図）
 *   - 文字の大きさ（0〜4）を変えても、アンカーの近くの地図の矩形の中に収まる
 *   - 行のクリックは `ui_action` の `choice`、窓の外のクリックは `cancel`、使えない行のクリックは何も送らない
 *   - 行が多いときは地図の高さに収まり、カーソルの行はいつも見えている
 * 画は `directory` に BMP で残す（目で見て確かめる用）。実行: `HD2D_CONTEXT_POPUP_CHECK=<フォルダ> HengbandHd2d.exe --ui-check`
 */
inline int run_context_popup_check(const AppOptions &options,const char *directory) {
    std::filesystem::create_directories(directory);int checks=0,failures=0,images=0;
    const auto check=[&](bool okay,const std::string &what){++checks;if(!okay){++failures;std::fprintf(stderr,"[context-popup-check] FAIL: %s\n",what.c_str());}};
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1600;capture.window_h=900;
    Window window;std::string err;if(!create_window(capture,window,err)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;if(!paint.init(err)||!text.init(18,err)){SDL_Quit();return 1;}
    int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);
    UiLayout layout;layout.screen_w=w;layout.screen_h=h;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();
    layout.content={0,0,w,h};layout.scene={100,0,w-300,h*62/100};
    const RectPx scene=layout.scene;
    auto start=[&](){gl::glViewport(0,0,w,h);gl::glClearColor(.02f,.03f,.045f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(w,h);text.begin(w,h);};
    auto finish=[&](const std::string &name){paint.flush();text.flush();check(gl::drain_gl_errors().empty(),name+": draw without GL errors");check(save_framebuffer_bmp((std::filesystem::path(directory)/(name+".bmp")).string(),w,h),name+": image saved");++images;};
    const char *labels[]={"Look At","Use Item On","Pathfind To","Walk Towards","Run Towards","Throw To"};
    const auto make_frame=[&](int rows,bool over_map){
        GameFrame f;f.menu_open=true;f.menu_over_map=over_map;f.cells.emplace_back();
        f.modern.version=5;f.modern.kind="dialog";f.modern.stage="choices";f.modern.title="Angband";f.modern.revision=1;f.modern.cursor=-1;
        for(int i=0;i<rows;++i){
            ModernRow row;row.id="choice:"+std::to_string(i);row.label=i<6?labels[i]:("Extra command "+std::to_string(i));
            row.key=row.hotkey="lU,;.v"[i%6];f.modern.rows.push_back(row);
        }
        TermMirrorLine line;line.source_row=0;line.text_utf8="(Enter to select command, ESC to cancel) You see an open floor:";f.menu_term_lines.push_back(line);
        return f;
    };
    const auto inside=[&](const RectPx &box){return box.x>=scene.x+6&&box.y>=scene.y+6&&box.x+box.w<=scene.x+scene.w-6&&box.y+box.h<=scene.y+scene.h-6;};
    const auto draw=[&](ModernStoreUi &ui,const GameFrame &f,int theme,const std::string &name){start();ui.draw(paint,text,layout,f,theme);finish(name);};

    // ポップアップにならない場合（ほかのコアの窓は今までどおり）
    {
        auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);
        check(!ui.context_popup_active(),"no anchor: not a popup");
        draw(ui,f,0,"a-no-anchor-big-window");
        check(ui.context_popup_box().empty(),"no anchor: no popup box");
        ui.set_context_anchor(true,700,300);check(ui.context_popup_active(),"anchor + menu_over_map: a popup");
        auto plain=make_frame(6,false);ModernStoreUi big;big.sync(plain,true);big.set_context_anchor(true,700,300);
        check(!big.context_popup_active(),"menu_over_map false: not a popup even with an anchor");
        auto confirm=make_frame(6,true);confirm.modern.stage="confirm";ModernStoreUi other;other.sync(confirm,true);other.set_context_anchor(true,700,300);
        check(!other.context_popup_active(),"not a choice window: not a popup");
        ui.set_context_anchor(false,0,0);check(!ui.context_popup_active(),"anchor cleared: not a popup");
    }

    // 置き場所: 右下が基本で、地図の矩形の端で切れるなら反対側へ寄せる
    struct Place{const char *name;int ax,ay;bool flip_x,flip_y;};
    const Place places[]={
        {"middle",scene.x+scene.w/3,scene.y+scene.h/4,false,false},
        {"right-edge",scene.x+scene.w-20,scene.y+scene.h/4,true,false},
        {"bottom-edge",scene.x+scene.w/3,scene.y+scene.h-20,false,true},
        {"bottom-right",scene.x+scene.w-20,scene.y+scene.h-20,true,true},
        {"top-left",scene.x+2,scene.y+2,false,false},
    };
    for(int theme=0;theme<3;++theme)for(const auto &place:places) {
        auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,place.ax,place.ay);
        draw(ui,f,theme,std::string("b-")+place.name+"-theme"+std::to_string(theme));
        const RectPx box=ui.context_popup_box();
        check(ui.context_popup_active()&&!box.empty(),std::string(place.name)+": popup drawn");
        check(inside(box),std::string(place.name)+": inside the map rectangle");
        if(theme==0) {
            if(!place.flip_x&&std::string(place.name)=="middle")check(box.x==place.ax+14&&box.y==place.ay+14,"middle: right and below the click");
            if(place.flip_x)check(box.x+box.w<place.ax,std::string(place.name)+": moved to the left of the click");
            if(place.flip_y)check(box.y+box.h<place.ay,std::string(place.name)+": moved above the click");
            check(box.w<700&&box.h<scene.h,std::string(place.name)+": a small window, not the full menu");
        }
    }

    // 文字の大きさを変えても、アンカーの近くの地図の矩形の中に収まる（大きい字は窓が大きくなるので、背の高い地図で見る）
    for(int level=0;level<=4;++level) {
        auto tall=layout;tall.scene={100,0,w-300,h-40};
        const RectPx area=tall.scene;
        auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);ui.set_font_level(level);
        const int ax=area.x+area.w/3,ay=area.y+area.h/8;ui.set_context_anchor(true,ax,ay);
        start();ui.draw(paint,text,tall,f,0);finish("c-font-level-"+std::to_string(level));
        const RectPx box=ui.context_popup_box();const float scale=menu_font_scale(level);
        check(ui.context_popup_active()&&box.x>=area.x+6&&box.y>=area.y+6&&box.x+box.w<=area.x+area.w-6&&box.y+box.h<=area.y+area.h-6,"font level "+std::to_string(level)+": inside the map rectangle");
        check(std::abs(box.x-(ax+static_cast<int>(14*scale)))<=3&&std::abs(box.y-(ay+static_cast<int>(14*scale)))<=3,"font level "+std::to_string(level)+": placed at the click (scaled offset): box "+std::to_string(box.x)+","+std::to_string(box.y)+" "+std::to_string(box.w)+"x"+std::to_string(box.h)+" anchor "+std::to_string(ax)+","+std::to_string(ay)+" scale "+std::to_string(scale));
    }

    // 狭い地図（ポップアップより狭い）: 窓の外へ出ない
    {
        auto narrow=layout;narrow.scene={50,50,260,220};
        auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,250,200);
        start();ui.draw(paint,text,narrow,f,0);finish("d-narrow-map");
        const RectPx box=ui.context_popup_box();
        check(box.x>=0&&box.y>=0&&box.x+box.w<=w&&box.y+box.h<=h,"narrow map: still inside the window");
    }

    // クリック: 行は choice、窓の外は cancel、使えない行は何も送らない
    {
        const int ax=scene.x+scene.w/3,ay=scene.y+scene.h/4;
        const auto row_center=[&](ModernStoreUi &ui,const std::string &id,int &x,int &y){
            for(const auto &cell:ui.painted_content())if(cell.target==id){const RectPx r=menu_scaled_rect(cell.rect,1.f);x=r.x+r.w/2;y=r.y+r.h/2;return true;}
            return false;
        };
        for(int pick=0;pick<6;++pick) {
            auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,ax,ay);
            start();ui.draw(paint,text,layout,f,0);paint.flush();text.flush();
            int x=0,y=0;const bool found=row_center(ui,"choice:"+std::to_string(pick),x,y);
            check(found,"row "+std::to_string(pick)+" is drawn and traced");
            presentation::InputEventsMessage out;ui.click(layout,x,y,out);
            check(out.events.size()==1&&out.events[0].e=="ui_action"&&out.events[0].action=="choice"&&out.events[0].target=="choice:"+std::to_string(pick),
                "clicking row "+std::to_string(pick)+" sends its choice at once");
        }
        {
            auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,ax,ay);
            start();ui.draw(paint,text,layout,f,0);paint.flush();text.flush();
            presentation::InputEventsMessage out;ui.click(layout,ax-80,ay-80,out);
            check(out.events.size()==1&&out.events[0].action=="cancel","a click outside the popup cancels the menu");
        }
        {
            auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,ax,ay);
            start();ui.draw(paint,text,layout,f,0);paint.flush();text.flush();
            const RectPx box=ui.context_popup_box();presentation::InputEventsMessage out;
            ui.click(layout,box.x+box.w/2,box.y+6,out);
            check(out.events.empty(),"a click on the popup's heading sends nothing");
        }
        {
            auto f=make_frame(6,true);f.modern.rows[2].available=false;f.modern.rows[2].reason="Not now";
            ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,ax,ay);
            start();ui.draw(paint,text,layout,f,0);paint.flush();text.flush();
            int x=0,y=0;check(row_center(ui,"choice:2",x,y),"an unavailable row is drawn");
            presentation::InputEventsMessage out;ui.click(layout,x,y,out);
            check(out.events.empty(),"clicking an unavailable row sends nothing");
        }
        {
            // キーボード: 下・下・決定 = 3 行目（窓いっぱいの窓と同じ操作）
            auto f=make_frame(6,true);ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,ax,ay);
            presentation::InputEventsMessage out;ui.handle(CursorNav::Down,out);ui.handle(CursorNav::Down,out);ui.handle(CursorNav::Confirm,out);
            check(out.events.size()==1&&out.events[0].action=="choice"&&out.events[0].target=="choice:2","keyboard Down, Down, Confirm chooses the third row");
            presentation::InputEventsMessage back;ModernStoreUi cancelled;cancelled.sync(f,true);cancelled.set_context_anchor(true,ax,ay);cancelled.cancel(back);
            check(back.events.size()==1&&back.events[0].action=="cancel","Cancel key still cancels");
        }
    }

    // 行が多い: 地図の高さに収まり、カーソルの行はいつも見えている
    {
        auto f=make_frame(40,true);ModernStoreUi ui;ui.sync(f,true);ui.set_context_anchor(true,scene.x+scene.w/3,scene.y+40);
        presentation::InputEventsMessage out;
        for(int step=0;step<39;++step)ui.handle(CursorNav::Down,out);
        draw(ui,f,0,"e-many-rows");
        const RectPx box=ui.context_popup_box();
        check(inside(box),"many rows: inside the map rectangle");
        bool cursor_visible=false;size_t drawn=0;
        for(const auto &cell:ui.painted_content()){++drawn;cursor_visible=cursor_visible||cell.selected;}
        check(cursor_visible&&drawn<40&&drawn>=2,"many rows: fewer rows than the menu has, and the cursor row is among them");
    }

    std::ofstream report(std::filesystem::path(directory)/"result.json");report<<"{\"images\":"<<images<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";report.close();
    std::fprintf(stderr,"[context-popup-check] %d checks, %d failures, %d images in %s\n",checks,failures,images,directory);
    paint.shutdown();text.shutdown();SDL_Quit();return failures?1:0;
}
}
