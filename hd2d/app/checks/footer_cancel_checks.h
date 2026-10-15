#pragma once
#include "app/app_support.h"
#include "app/ui_probe.h"
#include "ui/modern_store_ui.h"
#include "i18n/lang.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace hd2d {
/*!
 * @brief 足の欄の取消と、タイトルの案内の字の検査（修-11・修-19。`ModernStoreUi` の `footer_without_cancel()` と案内の字）。窓と GL は要るがコアは起こさない。
 *
 * @details 確かめること:
 *   - 取消が効かない窓（`no_cancel`。Umoria のキャラ作成）は、足の欄の取消の部品を問い合わせに載せず、決定を全幅にする。
 *     右半分を押しても取消を送らない。パッドの B・Esc（`cancel()`）は今までどおり取消を送る
 *   - `no_cancel` でない窓は今までどおり（決定・取消の 2 つ。右半分で取消）
 *   - `no_cancel` の窓の本文の段（`detail`。Umoria の名前の入力など）の案内も「取消: 戻る」を出さない
 *   - タイトル（`content_type=="start"`）の案内は日本語「取消: 終了」・英語 `Cancel: quit`。
 *     タイトルでない選択窓（読み込む・削除など）は「取消: 戻る」・`Cancel: back` のまま
 * 実行: `HD2D_FOOTER_CANCEL_CHECK=<フォルダ> HengbandHd2d.exe --ui-check`
 */
inline int run_footer_cancel_check(const AppOptions &options,const char *directory) {
    std::filesystem::create_directories(directory);int checks=0,failures=0;
    const auto check=[&](bool okay,const std::string &what){++checks;if(!okay){++failures;std::fprintf(stderr,"[footer-cancel-check] FAIL: %s\n",what.c_str());}};
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1600;capture.window_h=900;
    Window window;std::string err;if(!create_window(capture,window,err)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;if(!paint.init(err)||!text.init(18,err)){SDL_Quit();return 1;}
    int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);
    UiLayout layout;layout.screen_w=w;layout.screen_h=h;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();
    layout.content={0,0,w,h};layout.scene={0,0,w,h};
    const std::string previous(i18n::current());
    auto &probe=ui_probe();probe.enable(true);

    const auto make_frame=[&](const char *content_type,bool no_cancel,const char *stage="choices"){
        GameFrame f;f.menu_open=true;f.cells.emplace_back();
        f.modern.version=5;f.modern.kind="dialog";f.modern.stage=stage;f.modern.title="Test";f.modern.revision=1;f.modern.cursor=0;
        f.modern.content_type=content_type;f.modern.no_cancel=no_cancel;
        const char *labels[]={"New","Load","Quit"};const char keys[]={'N','L','Q'};
        for(int i=0;i<3;++i){ModernRow row;row.id="choice:"+std::to_string(i);row.label=labels[i];row.key=row.hotkey=keys[i];f.modern.rows.push_back(row);}
        return f;
    };
    //! 描いて確定し、足の欄の部品と案内の字を取り出す。
    struct Result {bool has_confirm{false},has_cancel{false};ProbeButton confirm,cancel;std::string json;};
    const auto draw=[&](ModernStoreUi &ui,const GameFrame &f){
        gl::glViewport(0,0,w,h);gl::glClearColor(.02f,.03f,.045f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(w,h);text.begin(w,h);
        ui.draw(paint,text,layout,f,0);paint.flush();text.flush();
        probe.frame_info("check",1,w,h,"game","menu");probe.commit("game");
        Result r;std::string e;UiProbe::TapQuery q;q.kind="footer";
        q.id="confirm";r.has_confirm=probe.find_button(q,r.confirm,e);
        q.id="cancel";r.has_cancel=probe.find_button(q,r.cancel,e);
        r.json=probe.snapshot_json("game");return r;
    };
    const auto has=[](const std::string &json,const char *needle){return json.find(needle)!=std::string::npos;};

    // 1. no_cancel でない窓：決定と取消の 2 つ。右半分の押下は取消
    int plain_confirm_w=0;
    {
        auto f=make_frame("",false);ModernStoreUi ui;ui.sync(f,true);const auto r=draw(ui,f);
        check(r.has_confirm&&r.has_cancel,"plain window: footer has Confirm and Cancel");
        plain_confirm_w=r.confirm.rect.w;
        if(r.has_cancel){
            presentation::InputEventsMessage out;ui.click(layout,r.cancel.rect.x+r.cancel.rect.w/2,r.cancel.rect.y+r.cancel.rect.h/2,out);
            check(out.events.size()==1&&out.events[0].action=="cancel","plain window: clicking the Cancel half cancels");
        }
    }
    // 2. no_cancel の窓：取消の部品が無く、決定が全幅。右半分の押下は取消を送らない。B・Esc は今までどおり
    {
        auto f=make_frame("",true);ModernStoreUi ui;ui.sync(f,true);const auto r=draw(ui,f);
        check(r.has_confirm,"no_cancel window: footer has Confirm");
        check(!r.has_cancel,"no_cancel window: footer has no Cancel button");
        check(r.has_confirm&&plain_confirm_w>0&&r.confirm.rect.w>=plain_confirm_w*2-2,"no_cancel window: Confirm spans the whole footer");
        if(r.has_confirm){
            presentation::InputEventsMessage out;ui.click(layout,r.confirm.rect.x+r.confirm.rect.w*3/4,r.confirm.rect.y+r.confirm.rect.h/2,out);
            bool cancelled=false;for(const auto &e:out.events)cancelled=cancelled||e.action=="cancel";
            check(!cancelled,"no_cancel window: clicking the right half does not cancel");
        }
        presentation::InputEventsMessage pad;ModernStoreUi other;other.sync(f,true);other.cancel(pad);
        check(pad.events.size()==1&&pad.events[0].action=="cancel","no_cancel window: the pad B / Esc path (cancel()) is unchanged");
    }
    // 3. 案内の字：タイトルは「取消: 終了」、そのほかは「取消: 戻る」（日本語・英語）
    for(const char *language:{"ja","en"}) {
        check(i18n::set_language(language),std::string("language ")+language+" selectable");
        const bool ja=std::string(language)=="ja";
        const char *quit=ja?"取消: 終了":"Cancel: quit",*back=ja?"取消: 戻る":"Cancel: back";
        {
            auto f=make_frame("start",false);ModernStoreUi ui;ui.sync(f,true);const auto r=draw(ui,f);
            check(has(r.json,quit),std::string("title (")+language+"): guide says quit");
            check(!has(r.json,back),std::string("title (")+language+"): guide does not say back");
        }
        {
            auto f=make_frame("",false);ModernStoreUi ui;ui.sync(f,true);const auto r=draw(ui,f);
            check(has(r.json,back),std::string("other choice window (")+language+"): guide still says back");
            check(!has(r.json,quit),std::string("other choice window (")+language+"): guide does not say quit");
        }
        {
            // 取消が効かない窓の本文の段：案内から「取消: 戻る」を外す。効く窓は今までどおり
            auto f=make_frame("",true,"detail");ModernStoreUi ui;ui.sync(f,true);const auto r=draw(ui,f);
            check(has(r.json,ja?"決定: 実行":"Confirm: run"),std::string("no_cancel detail window (")+language+"): guide is drawn");
            check(!has(r.json,back),std::string("no_cancel detail window (")+language+"): guide does not say back");
            auto g=make_frame("",false,"detail");ModernStoreUi other;other.sync(g,true);const auto q=draw(other,g);
            check(has(q.json,back),std::string("detail window (")+language+"): guide still says back");
        }
    }
    i18n::set_language(previous);
    std::ofstream report(std::filesystem::path(directory)/"result.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";report.close();
    std::fprintf(stderr,"[footer-cancel-check] %d checks, %d failures in %s\n",checks,failures,directory);
    paint.shutdown();text.shutdown();SDL_Quit();return failures?1:0;
}
}
