#pragma once
/*!
 * @file message_history_checks.h
 * @brief メッセージ履歴（Ctrl+P）の窓の検査。**窓も GL もコアも要らない。**
 * @details `HD2D_MESSAGE_HISTORY_CHECK=1 HengbandHd2d.exe --ui-check`。前半はコア側の組み立て（窓も GL もコアも要らない）、
 * 最後の「画面側の描き方」（修-18。`run_history_scroll_checks`）だけ窓と GL が要る（コアは起こさない）。
 * 各コアが実際に送った履歴の画面（日本語・英語。2026-10-10 に実機から採った行）を模したフレームを作り、本物の
 * `NativeModernMenu::capture()` に通して、窓の題・本文の段落の数・脚注の扱いを見る。
 *  - 題は「メッセージ履歴」（英語は「Message history」）。コアの名前（Angband）や見出しの件数にならない。
 *  - 本文は 1 メッセージ 1 段落。脚注（`[Press 'p' …]`・`[移動キーで移動、…]`）や見出しは本文に混ざらない。
 *  - 脚注は選択肢（左の行）に割れず、操作（古い方・新しい方・検索）とページ送りのキーになる。
 *  - Frox は文書の枠（`modern_document_info`）で組む。日本語でも 1 メッセージ 1 段落（以前は 1 つにつながっていた）。
 * 直す前の組み立て（見出しが題になる版）は、Angband の題が「Angband」・変愚と Sil-Q の英語に「[Press」が出るなどで落ちる。
 */
#include "bridge/native_modern.h"
#include "../../../frox/adapter/fc_history.h"
#include "app/app_support.h"
#include "ui/menu_scale.h"
#include "ui/modern_store_ui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace hd2d {
namespace message_history_check {
struct Sample {
    const char *name;
    const char *profile;     //!< `set_document_profile`（変愚・短愚 = heng、幻想蛮怒 = legacy、Sil-Q = sil、Angband = generic、Frox = frox）
    const char *language;
    const char *core_title;  //!< コアが `set_title` で渡す名前（Angband は "Angband"）
    std::vector<std::pair<int,std::string>> lines;
    int prev_key,next_key;   //!< コアが `menu_page_prev_key` / `menu_page_next_key` で申告する値（Angband だけ）
    bool with_choices;       //!< 脚注の語を選択肢にして渡すコア（Sil-Q・Angband は脚注が選択肢に割れていた）
    int paragraphs;          //!< 期待する本文の段落の数
    const char *title;       //!< 期待する題（nullptr なら見ない）
    std::vector<std::pair<int,std::string>> actions; //!< 期待する操作（キー・ラベル）。空なら見ない
};
inline std::vector<Sample> samples() {
    const auto ja=[](const char8_t *s){return std::string(reinterpret_cast<const char*>(s));};
    std::vector<Sample> out;
    // 変愚・短愚・幻想蛮怒（見出し・脚注とも同じ文言）
    out.push_back({"heng ja","heng","ja","Hengband",{
        {0,ja(u8"以前のメッセージ 0-5 全部で(6)")},{5,"===================="},{8,ja(u8"ターゲット解除。")},
        {9,ja(u8"＊＊＊　終末まで 残り 14日　＊＊＊")},{10,ja(u8"一つにつき $52です。")},
        {11,ja(u8"真鍮のランタン(7500ターンの寿命) {明}(i)を購入する。")},{12,ja(u8"買値 $52 で買いますか？[Y/n]")},
        {35,ja(u8"[ 'p' で更に古いもの, 'n' で更に新しいもの, '/' で検索, ESC で中断 ]")}},0,0,false,6,
        "メッセージ履歴",{{'p',"更に古いメッセージ"},{'n',"更に新しいメッセージ"},{'/',"検索"}}});
    out.push_back({"heng en","heng","en","Hengband",{
        {0,"Message Recall (0-4 of 5)"},{5,"===================="},{8,"You have no more Scrolls of Word of Recall."},
        {9,"You have 5 Flasks of Oil."},{10,"You see a Wooden Torch."},
        {35,"[Press 'p' for older, 'n' for newer, ..., or ESCAPE]"}},0,0,false,4,
        "Message history",{{'p',"Older messages"},{'n',"Newer messages"},{'/',"Find"}}});
    out.push_back({"legacy en (mixed language messages)","legacy","en","Gensoband",{
        {0,"Message Recall (0-31 of 316)"},{1,""},{2,ja(u8"白狼天狗に攻撃された。")},{3,"slashes you."},{4,"bites you."},
        {5,ja(u8"岩石が行く手をはばんでいる。 <x2>")},{6,"You resist the effects!"},
        {35,"[Press 'p' for older, 'n' for newer, ..., or ESCAPE]"}},0,0,false,5,
        "Message history",{}});
    // Sil-Q（脚注が選択肢に割れていた。見出しの次から 16 行目まで空）
    out.push_back({"sil ja","sil","ja","Sil-Q",{
        {0,ja(u8"前の文言（0-18 / 19 件）、0 件目から")},{17,"===================="},{20,ja(u8"陽の差す一角へ踏み込んだ。")},
        {21,ja(u8"陽だまりから踏み出した。")},{24,"There is a wall in the way! <5x>"},
        {35,ja(u8"['p' で古い方へ、'n' で新しい方へ、… ESCAPE で戻る]")}},0,0,true,4,
        "メッセージ履歴",{{'p',"更に古いメッセージ"},{'n',"更に新しいメッセージ"},{'/',"検索"}}});
    out.push_back({"sil en","sil","en","Sil-Q",{
        {0,"Message Recall (0-18 of 19), Offset 0"},{17,"===================="},{20,"You step into a patch of sunlight."},
        {21,"You step out of the sunlight."},{24,"There is a wall in the way! <5x>"},
        {35,"[Press 'p' for older, 'n' for newer, ..., or ESCAPE]"}},0,0,true,4,
        "Message history",{{'p',"Older messages"},{'n',"Newer messages"},{'/',"Find"}}});
    // Angband（題が「Angband」・見出しと脚注が選択肢に割れていた。古い方・新しい方のキーは申告）
    out.push_back({"angband ja","generic","ja","Angband",{
        {0,ja(u8"メッセージ履歴 (0-22 / 59)、位置 0")},{2,ja(u8"野犬はあなたに噛みついた。")},{3,ja(u8"野犬を攻撃した。")},
        {4,ja(u8"野犬を倒した。")},{5,ja(u8"壁が邪魔をしている！")},
        {26,ja(u8"[移動キーで移動、'=' 検索、ESC で戻る]")}},'p','n',true,4,
        "メッセージ履歴",{{'p',"更に古いメッセージ"},{'n',"更に新しいメッセージ"},{'=',"検索"}}});
    out.push_back({"angband en","generic","en","Angband",{
        {0,"Message recall (0-22 of 59), offset 0"},{2,ja(u8"野犬はあなたに噛みついた。")},{3,ja(u8"野犬を攻撃した。")},
        {4,"This seems a quiet, peaceful place."},
        {26,"[Movement keys to navigate, '=' to find, or ESCAPE to exit]"}},'p','n',true,3,
        "Message history",{{'p',"Older messages"},{'n',"Newer messages"},{'=',"Find"}}});
    // Frox（文書の枠。ターンの区切りの空行があり、メッセージが 1 行ずつ並ぶ）
    out.push_back({"frox ja","frox","ja","FroxComposband",{
        {0,ja(u8"[過去のメッセージ  404/436 行]")},{1,""},{2,ja(u8"洞窟蜘蛛の攻撃: それは噛んだ.")},{3,""},
        {4,ja(u8"洞窟蜘蛛を攻撃: 命中した。 洞窟蜘蛛を倒した。")},{5,ja(u8"石英の鉱脈が行く手を塞いでいる。")},
        {6,ja(u8"330 ゴールド相当の銅塊を集めた。")},{7,ja(u8"永久岩の壁が行く手を塞いでいる。 (x2)")},
        {35,ja(u8"[/ か Ctrl+F で検索、Enter/F3 で次、? でヘルプ、ESC で終了]")}},0,0,false,5,nullptr,{}});
    out.push_back({"frox en","frox","en","FroxComposband",{
        {0,"[Previous Messages, Line 407/439]"},{1,""},{2,ja(u8"洞窟蜘蛛の攻撃: それは噛んだ.")},{3,""},
        {4,ja(u8"洞窟蜘蛛を攻撃: 命中した。 洞窟蜘蛛を倒した。")},{5,ja(u8"石英の鉱脈が行く手を塞いでいる。")},
        {35,"[/ or Ctrl+F to search, Enter/F3 for next, ? for help, ESC to exit]"}},0,0,false,3,nullptr,{}});
    return out;
}
//! 脚注の語を選択肢に割る（Sil-Q・Angband のアダプタが以前していたこと）。`'p' …`・`'n' …` の区間を 1 つずつ。
inline void add_footer_choices(GameFrame &f,int line_index) {
    const std::string &text=f.menu_term_lines[size_t(line_index)].text_utf8;
    for(const char *key:{"'p'","'n'","'='"}) {
        const auto at=text.find(key);if(at==std::string::npos)continue;
        auto end=text.find_first_of(",]",at);if(end==std::string::npos)end=text.size();
        MenuChoice c;c.line_index=line_index;c.span_begin=int(at);c.span_len=int(end-at);c.key=key[1];f.menu_choices.push_back(c);
    }
}
//! 窓を描く側の検査（修-18）に使う、1 つの履歴の画面の作り方（コアごと・言語ごとの見出しと脚注）。
struct ScrollVariant {const char *name,*profile,*language,*core_title,*header_format,*footer;int prev_key,next_key;};
//! 履歴の窓の本文が窓に収まらないほど長いとき、描いた直後にどのメッセージが見えているか。
struct HistoryView {int lowest{0},highest{0},painted{0};bool highest_full{false};};
/*!
 * @brief 履歴の画面（原作の画面の写し）を本物の組み立て（`NativeModernMenu::capture()`）に通して、窓にしたフレームを作る。
 * @details 本文は古い順に `first`〜`first+count-1` 番のメッセージ（行の中に `#番号` を入れて、描いたあとに読み取る）。
 */
inline GameFrame history_window_frame(const ScrollVariant &v,int first,int count,int revision) {
    GameFrame f;f.menu_open=true;f.cells.emplace_back();
    const auto line=[&](int row,const std::string &text){TermMirrorLine l;l.source_row=row;l.text_utf8=text;f.menu_term_lines.push_back(std::move(l));};
    char header[160];std::snprintf(header,sizeof header,v.header_format,first+count-1,first+count-1);
    line(0,header);
    const bool ja=std::string(v.language)=="ja";
    for(int i=0;i<count;++i) {
        const int number=first+i;
        line(1+i,(ja?reinterpret_cast<const char*>(u8"野犬に噛みつかれた。 #"):"The wild dog bites you. #")+std::to_string(number));
    }
    line(1+count,v.footer);
    f.menu_term_curs_row=int(f.menu_term_lines.size())-1;f.menu_term_curs_col=40;
    f.menu_page_prev_key=v.prev_key;f.menu_page_next_key=v.next_key;
    presentation::NativeModernMenu menu;menu.set_enabled(true);menu.set_language(v.language);
    menu.set_document_profile(v.profile);menu.set_title(v.core_title);menu.capture(f);
    f.modern.revision=revision;
    return f;
}
/*!
 * @brief 履歴の窓の本文のうち、いま描かれているメッセージの範囲（`#番号` を読む）。
 * @details 窓から食み出して切れた行は、高さが足りない（`highest_full` は一番新しい行がまるごと見えているか）。
 */
inline HistoryView read_history_view(const ModernStoreUi &ui) {
    HistoryView out;int full_height=0;
    for(const auto &cell:ui.painted_content())if(cell.text.find('#')!=std::string::npos)full_height=std::max(full_height,cell.rect.h);
    for(const auto &cell:ui.painted_content()) {
        const auto at=cell.text.find('#');if(at==std::string::npos)continue;
        const int number=std::atoi(cell.text.c_str()+at+1);
        if(out.painted==0||number<out.lowest)out.lowest=number;
        if(out.painted==0||number>=out.highest){out.highest=number;out.highest_full=cell.rect.h>=full_height;}
        ++out.painted;
    }
    return out;
}
/*!
 * @brief 画面側の検査（修-18）：履歴の窓を開いたとき・頁を送ったとき、一番新しい行（本文の最後）が窓に見えている。
 * @details 原作の履歴は古い順で、新しいメッセージが画面の最後にある。窓が先頭から描くと、本文が窓より長いとき最新の行が下に切れていた。
 * 窓と GL は要るがコアは起こさない。変えていないこと：同じ内容の再送で遊ぶ人が上へ送った位置を戻さない・履歴でない長い窓は先頭から。
 */
template<class Check>
inline void run_history_scroll_checks(const AppOptions &options,Check &&check) {
    if(SDL_Init(SDL_INIT_VIDEO)!=0){check(false,"scroll: SDL_Init failed");return;}
    AppOptions capture=options;capture.gl_probe_only=true;capture.window_w=1600;capture.window_h=900;
    Window window;std::string error;
    if(!create_window(capture,window,error)){check(false,"scroll: could not create the check window: "+error);SDL_Quit();return;}
    UiPaint paint;TextOverlay text;
    if(!paint.init(error)||!text.init(18,error)){check(false,"scroll: could not start the painter: "+error);SDL_Quit();return;}
    int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);
    UiLayout layout;layout.screen_w=w;layout.screen_h=h;layout.cell_w=text.cell_w();layout.cell_h=text.cell_h();
    layout.content={0,0,w,h};layout.scene={0,0,w,h};
    const auto draw=[&](ModernStoreUi &ui,const GameFrame &f){
        gl::glViewport(0,0,w,h);gl::glClearColor(.02f,.03f,.045f,1);gl::glClear(gl::GL_COLOR_BUFFER_BIT);paint.begin(w,h);text.begin(w,h);
        ui.draw(paint,text,layout,f,0);paint.flush();text.flush();
        return read_history_view(ui);
    };
    const auto ja=[](const char8_t *s){return std::string(reinterpret_cast<const char*>(s));};
    const std::string header_ja_heng=ja(u8"以前のメッセージ 0-%d 全部で(%d)"),header_ja_sil=ja(u8"前の文言（0-%d / %d 件）、0 件目から"),
        header_ja_an=ja(u8"メッセージ履歴 (0-%d / %d)、位置 0"),footer_ja_heng=ja(u8"[ 'p' で更に古いもの, 'n' で更に新しいもの, '/' で検索, ESC で中断 ]"),
        footer_ja_sil=ja(u8"['p' で古い方へ、'n' で新しい方へ、… ESCAPE で戻る]"),footer_ja_an=ja(u8"[移動キーで移動、'=' 検索、ESC で戻る]");
    const std::vector<ScrollVariant> variants{
        {"heng ja","heng","ja","Hengband",header_ja_heng.c_str(),footer_ja_heng.c_str(),0,0},
        {"heng en","heng","en","Hengband","Message Recall (0-%d of %d)","[Press 'p' for older, 'n' for newer, ..., or ESCAPE]",0,0},
        {"sil ja","sil","ja","Sil-Q",header_ja_sil.c_str(),footer_ja_sil.c_str(),0,0},
        {"angband ja","generic","ja","Angband",header_ja_an.c_str(),footer_ja_an.c_str(),'p','n'},
        {"angband en","generic","en","Angband","Message recall (0-%d of %d), offset 0","[Movement keys to navigate, '=' to find, or ESCAPE to exit]",'p','n'},
    };
    constexpr int kMessages=40;
    for(const auto &v:variants) {
        const std::string name=std::string("scroll ")+v.name;
        // 開いたとき：窓に収まらないほど長いので、先頭の行は切れていて、最新の行がまるごと見える
        ModernStoreUi ui;auto page=history_window_frame(v,1,kMessages,1);
        check(page.modern.title==(std::string(v.language)=="ja"?ja(u8"メッセージ履歴"):std::string("Message history")),name+": the frame is the message history window (title '"+page.modern.title+"')");
        ui.sync(page,true);auto view=draw(ui,page);
        check(view.painted<kMessages,name+": the test window is too tall to prove anything (all "+std::to_string(view.painted)+" messages are painted)");
        check(view.painted>0&&view.highest==kMessages&&view.highest_full,name+": on open the newest line #"+std::to_string(kMessages)+" is fully visible (highest #"+std::to_string(view.highest)+", full="+std::to_string(view.highest_full)+")");
        // 頁を送った直後（別の範囲の画面が届く）：その頁の最新の行が見える
        auto older=history_window_frame(v,101,kMessages,2);
        ui.sync(older,true);view=draw(ui,older);
        check(view.highest==140&&view.highest_full,name+": right after a page turn the newest line #140 is fully visible (highest #"+std::to_string(view.highest)+")");
        // 同じ内容の再送（revision だけが進む）は、遊ぶ人が上へ送った位置を戻さない
        {
            const auto &cells=ui.painted_content();
            if(!cells.empty()) {
                const auto &r=cells.back().rect;presentation::InputEventsMessage out;
                for(int i=0;i<6;++i)ui.scroll_at(layout,r.x+r.w/2,r.y+r.h/2,-1,out);
            }
            const auto scrolled=draw(ui,older);
            check(scrolled.highest<140,name+": the test could not scroll the window up (highest #"+std::to_string(scrolled.highest)+")");
            auto refresh=history_window_frame(v,101,kMessages,3);ui.sync(refresh,true);
            const auto kept=draw(ui,refresh);
            check(kept.highest==scrolled.highest&&kept.lowest==scrolled.lowest,name+": a refresh with the same content keeps the player's scroll position (#"+std::to_string(kept.lowest)+"-"+std::to_string(kept.highest)+" vs #"+std::to_string(scrolled.lowest)+"-"+std::to_string(scrolled.highest)+")");
        }
        // 閉じてから同じ履歴を開き直しても、また最新の行から見せる
        {
            GameFrame closed;ui.sync(closed,true);draw(ui,closed);
            auto reopened=history_window_frame(v,101,kMessages,4);ui.sync(reopened,true);const auto again=draw(ui,reopened);
            check(again.highest==140&&again.highest_full,name+": reopening the same history shows the newest line again (highest #"+std::to_string(again.highest)+")");
        }
    }
    // 履歴でない長い窓（文書の窓）は、今までどおり先頭から見せる
    for(const char *language:{"ja","en"}) {
        GameFrame f;f.menu_open=true;f.cells.emplace_back();
        auto &m=f.modern;m.version=5;m.kind="dialog";m.stage="detail";m.title=std::string(language)=="ja"?ja(u8"キャラクターシート"):"Character sheet";m.revision=1;m.content_type="document";m.language=language;
        for(int i=1;i<=kMessages;++i){ModernBlock b;b.kind="paragraph";b.cells.push_back({"",std::string("Line #")+std::to_string(i),false});m.content.push_back(std::move(b));}
        ModernStoreUi ui;ui.sync(f,true);const auto view=draw(ui,f);
        check(view.lowest==1&&view.highest<kMessages,std::string("scroll other window ")+language+": a long document window other than the message history still starts at the top (#"+std::to_string(view.lowest)+"-#"+std::to_string(view.highest)+")");
    }
    paint.shutdown();text.shutdown();SDL_Quit();
}
inline int run_message_history_checks(const AppOptions &options) {
    int checks=0,failures=0;
    const auto check=[&](bool ok,const std::string &what){++checks;if(!ok){++failures;std::fprintf(stderr,"[message-history-check] FAIL: %s\n",what.c_str());}};
    for(const auto &s:samples()) {
        GameFrame f;f.menu_open=true;
        for(const auto &[row,text]:s.lines){TermMirrorLine l;l.source_row=row;l.text_utf8=text;f.menu_term_lines.push_back(std::move(l));}
        f.menu_term_curs_row=int(f.menu_term_lines.size())-1;f.menu_term_curs_col=40;
        f.menu_page_prev_key=s.prev_key;f.menu_page_next_key=s.next_key;
        if(s.with_choices)add_footer_choices(f,int(f.menu_term_lines.size())-1);
        presentation::NativeModernMenu menu;menu.set_enabled(true);menu.set_language(s.language);
        menu.set_document_profile(s.profile);menu.set_title(s.core_title);menu.capture(f);
        const auto &w=f.modern;const std::string name=s.name;
        check(w.kind=="dialog"&&w.stage=="detail"&&w.rows.empty(),name+": the history is one detail window with no choice rows (stage="+w.stage+", rows="+std::to_string(w.rows.size())+")");
        if(s.title)check(w.title==s.title,name+": the window title is the localized 'message history' (got '"+w.title+"')");
        int paragraphs=0;bool footer_leak=false,header_leak=false;
        for(const auto &b:w.content)if(b.kind=="paragraph")for(const auto &c:b.cells) {
            ++paragraphs;
            if(c.text.starts_with("[Press")||c.text.starts_with(reinterpret_cast<const char*>(u8"[移動キー"))||c.text.find("ESC")!=std::string::npos)footer_leak=true;
            if(c.text.find("Message Recall")!=std::string::npos||c.text.find("Message recall")!=std::string::npos
                ||c.text.find(reinterpret_cast<const char*>(u8"メッセージ履歴"))!=std::string::npos||c.text.find(reinterpret_cast<const char*>(u8"以前のメッセージ"))!=std::string::npos)header_leak=true;
        }
        check(paragraphs==s.paragraphs,name+": one paragraph per message (expected "+std::to_string(s.paragraphs)+", got "+std::to_string(paragraphs)+")");
        check(!footer_leak,name+": the footer hint is not in the body");
        check(!header_leak,name+": the header (counts) is not in the body");
        bool bad_label=false;
        for(const auto &a:w.actions)if(a.label.empty()||a.label.find("ESC")!=std::string::npos||a.label.find("for older")!=std::string::npos
            ||a.label.find("for newer")!=std::string::npos||a.label.find(reinterpret_cast<const char*>(u8"古い方へ"))!=std::string::npos||a.label.find(']')!=std::string::npos)bad_label=true;
        check(!bad_label,name+": no action is a fragment of the footer sentence");
        for(const auto &[key,label]:s.actions) {
            const bool found=std::any_of(w.actions.begin(),w.actions.end(),[&,key=key,label=label](const auto &a){return a.key==key&&a.label==label;});
            check(found,name+": has the action '"+std::string(1,char(key))+"' labelled '"+label+"'");
        }
        if(s.actions.size()>=2)check(w.prev_key==s.actions[0].first&&w.next_key==s.actions[1].first&&w.previous&&w.next,name+": the page keys are the older / newer keys");
    }
    // Frox: 原作が折り返した行を、記録（`msg_get()`）を物差しに 1 メッセージ 1 行へまとめる（`fc_history.h`）。
    {
        const auto ja=[](const char8_t *s){return std::string(reinterpret_cast<const char*>(s));};
        const auto frame_of=[&](const std::vector<std::string> &rows,const std::string &footer){
            std::vector<TermMirrorLine> lines;int row=0;
            for(const auto &text:rows){TermMirrorLine l;l.source_row=row++;l.text_utf8=text;lines.push_back(std::move(l));}
            TermMirrorLine l;l.source_row=35;l.text_utf8=footer;lines.push_back(std::move(l));
            return lines;
        };
        const auto texts=[&](const std::vector<TermMirrorLine> &lines){
            std::vector<std::string> out;for(size_t i=1;i+1<lines.size();++i)if(!fc::history_trim(lines[i].text_utf8).empty())out.push_back(fc::history_trim(lines[i].text_utf8));return out;};
        // 日本語: 折り返されるのは 3 行にまたがる長いメッセージだけ（日本語は字の途中で切れる）。ターンの区切りの空行が混ざる
        const std::vector<std::string> log_ja={ja(u8"洞窟蜘蛛の攻撃: それは噛んだ."),ja(u8"洞窟蜘蛛を攻撃: 命中した。 洞窟蜘蛛を倒した。"),ja(u8"石英の鉱脈が行く手を塞いでいる。"),
            ja(u8"前回の一時ダンジョンファイルを消した（異常終了の名残と思われる）。ふつうは何も起きない。おかしなところに気づいたら、帰還するか、別の街へ移動するか、世界地図へ出て 1 歩進んでから続けてほしい。 (x4)"),
            ja(u8"溶岩の鉱脈が行く手を塞いでいる。 (x4)")};
        auto lines=frame_of({ja(u8"[過去のメッセージ  404/436 行]"),"",ja(u8"洞窟蜘蛛の攻撃: それは噛んだ."),"",ja(u8"洞窟蜘蛛を攻撃: 命中した。 洞窟蜘蛛を倒した。"),ja(u8"石英の鉱脈が行く手を塞いでいる。"),"",
            ja(u8"前回の一時ダンジョンファイルを消した（異常終了の名残と思われる）。ふつ"),ja(u8"うは何も起きない。おかしなところに気づいたら、帰還するか、別の街へ移動"),
            ja(u8"するか、世界地図へ出て 1 歩進んでから続けてほしい。 (x4)"),"",ja(u8"溶岩の鉱脈が行く手を塞いでいる。 (x4)")},ja(u8"[/ か Ctrl+F で検索、ESC で終了]"));
        check(fc::join_history_lines(lines,log_ja),"frox history: the wrapped Japanese lines are joined");
        const auto joined=texts(lines);
        check(joined.size()==5,"frox history: five messages stay five lines (got "+std::to_string(joined.size())+")");
        check(joined.size()==5&&joined[3]==log_ja[3],"frox history: the three wrapped lines are one message with no space inserted inside Japanese text");
        // 英語: 単語の切れ目で折り返され、継ぎ目には空白を入れる。窓の最初の行は前のメッセージの続きでもよい
        const std::vector<std::string> log_en={"You have no more Scrolls of Word of Recall.",
            "Deleted temporary dungeon files (possibly from a crash). This usually causes no issues. If you notice anything odd, try recalling, traveling to a different town. (x2)","You hit it."};
        auto en=frame_of({"[Previous Messages, Line 407/439]","causes no issues. If you notice anything odd, try recalling, traveling","to a different town. (x2)","","You hit it."},"[ESC to exit]");
        check(fc::join_history_lines(en,log_en),"frox history: a window that starts in the middle of a message joins");
        const auto en_joined=texts(en);
        check(en_joined.size()==2&&en_joined[0]=="causes no issues. If you notice anything odd, try recalling, traveling to a different town. (x2)"&&en_joined[1]=="You hit it.",
            "frox history: English wrapped lines are joined with one space ("+std::to_string(en_joined.size())+" lines)");
        // 記録と字が合わなければ何もしない。履歴の画面でなければ何もしない
        auto bad=frame_of({"[Previous Messages, Line 1/2]","one","two"},"[ESC to exit]");
        check(!fc::join_history_lines(bad,{"something else entirely"})&&texts(bad).size()==2,"frox history: a log that does not match leaves the lines as they are");
        auto other=frame_of({"[Character Sheet, Line 1/50]","Name: x"},"[ESC to exit]");
        check(!fc::join_history_lines(other,{"Name: x"}),"frox history: another document is not touched");
        // 窓にすると、まとめた行は 1 メッセージ 1 段落
        GameFrame f;f.menu_open=true;f.menu_term_lines=lines;f.menu_term_curs_row=int(lines.size())-1;
        presentation::NativeModernMenu menu;menu.set_enabled(true);menu.set_language("ja");menu.set_document_profile("frox");menu.set_title("FroxComposband");menu.capture(f);
        int paragraphs=0;for(const auto &block:f.modern.content)if(block.kind=="paragraph")paragraphs+=int(block.cells.size());
        check(paragraphs==5,"frox history: the joined lines make five paragraphs in the window (got "+std::to_string(paragraphs)+")");
    }
    run_history_scroll_checks(options,check); // 修-18：窓を描く側（窓と GL が要る）
    std::fprintf(stderr,"[message-history-check] %d checks, %d failures\n",checks,failures);
    std::printf("%s\n",failures?"RESULT: FAIL":"RESULT: PASS");
    return failures?1:0;
}
}
}
