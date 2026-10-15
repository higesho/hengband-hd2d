#pragma once
#include "app/app_support.h"
#include "ui/modern_store_ui.h"
#include "ui/text_wrap.h"
#include "bridge/modern_artifact_lore.generated.h"
#include "i18n/lang.h"
#include <filesystem>
#include <fstream>
#include <sstream>
namespace hd2d {
inline int render_english_details_preview(const AppOptions &options,const char *directory) {
    std::filesystem::create_directories(directory);int checks=0,failures=0,images=0;
    const auto check=[&](bool okay){++checks;if(!okay)++failures;};
    const std::string previous(i18n::current());check(i18n::set_language("en"));
    // Check the compiled locale table used by the bridge, including all 11 supplements.
    for(const auto &entry:presentation::modern_artifact_texts) {
        check(entry.en_name&&*entry.en_name);
        if(*entry.ja_lore)check(entry.en_lore&&*entry.en_lore);
        check(presentation::modern_artifact_text(entry.id)==&entry);
    }
    check(presentation::modern_artifact_text(-1)==nullptr);
    const auto fit=[](std::string_view s,int width){size_t n=std::min(s.size(),static_cast<size_t>(width));while(n>0&&n<s.size()&&(static_cast<unsigned char>(s[n])&0xc0)==0x80)--n;return n;};
    const auto words=wrap_ui_text("resistance to disenchantment",14,fit);
    check(words==std::vector<std::string>({"resistance to","disenchantment"}));
    check(wrap_ui_text("one\n\ntwo",8,fit)==std::vector<std::string>({"one","","two"}));
    const std::string japanese="説明文の折り返しも維持します。";
    const auto jp=wrap_ui_text(japanese,10,fit);std::string joined;for(const auto &line:jp)joined+=line;check(joined==japanese);
    check(wrap_ui_text("abcdefghijklmnopqrstuvwxyz",8,fit).size()==4);
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1360;capture.window_h=900;
    Window window;std::string err;if(!create_window(capture,window,err)){SDL_Quit();return 1;}
    UiPaint paint;TextOverlay text;if(!paint.init(err)||!text.init(18,err)){SDL_Quit();return 1;}
    int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);
    UiLayout layout;layout.screen_w=w;layout.screen_h=h;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();layout.content={0,0,w,h};layout.scene=layout.content;
    const auto normalize=[](const std::string &s){std::istringstream in(s);std::string word,out;while(in>>word){if(!out.empty())out+=' ';out+=word;}return out;};
    const auto *lore=presentation::modern_artifact_text(208);
    const std::string paragraph=lore->en_lore;
    for(int width:{160,280,400}) {
        const auto lines=wrap_ui_text(paragraph,width,[&](std::string_view s,int limit){return text.fit_bytes(s,limit);});
        std::string rebuilt;for(const auto &line:lines){check(text.measure(line)<=width);rebuilt+=line+' ';}
        check(normalize(rebuilt)==normalize(paragraph));
    }
    auto start=[&](){gl::glViewport(0,0,w,h);gl::glClearColor(.018f,.025f,.035f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(w,h);text.begin(w,h);};
    auto save=[&](const std::string &name){paint.flush();text.flush();check(gl::drain_gl_errors().empty());check(save_framebuffer_bmp((std::filesystem::path(directory)/(name+".bmp")).string(),w,h));++images;};
    for(int theme=0;theme<3;++theme) {
        const std::string prefix=theme==0?"A":(theme==1?"B":"C");
        GameFrame frame;frame.menu_open=true;frame.modern.version=3;frame.modern.kind="store";frame.modern.stage="browse";frame.modern.revision=1;frame.hud.gold=2840;
        ModernRow row;row.id="english-sample";row.label="Aegis Fang";row.count=1;row.price_known=false;row.weight="15.0 lb";row.description=paragraph;
        row.actions={{"buy","Buy","",true}};frame.modern.rows.push_back(row);
        ModernStoreUi ui;ui.sync(frame,true);start();ui.draw(paint,text,layout,frame,theme);save(prefix+"-english");
        frame.modern.rows[0].label="Layout sample: an unusually long item name with several enchantments and a long player inscription";
        frame.modern.rows[0].description=paragraph+"\n\n"+presentation::modern_artifact_text(245)->en_lore+"\n\nEND OF DESCRIPTION";
        frame.modern.revision=2;ui.sync(frame,true);start();ui.draw(paint,text,layout,frame,theme);save(prefix+"-long-name");
        presentation::InputEventsMessage ignored;ui.toggle_details();for(int i=0;i<200;++i)ui.handle(CursorNav::Down,ignored);
        start();ui.draw(paint,text,layout,frame,theme);save(prefix+"-description-end");check(ignored.events.empty());
        auto narrow=layout;narrow.content={0,0,860,700};ModernStoreUi narrow_ui;narrow_ui.sync(frame,true);
        start();narrow_ui.draw(paint,text,narrow,frame,theme);save(prefix+"-narrow");
        ui.shutdown();narrow_ui.shutdown();
    }
    std::ofstream report(std::filesystem::path(directory)/"result.json");report<<"{\"images\":"<<images<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"game_core_started\":false}\n";report.close();
    paint.shutdown();text.shutdown();SDL_Quit();i18n::set_language(previous);return failures?1:0;
}
}
