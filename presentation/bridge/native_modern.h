#pragma once
#include "frame/game_frame.h"
#include "frame/modern_window_codec.h"
#include "frame/message_more.h"
#include "bridge/modern_content.h"
#include "bridge/input_event_adapter.h"
#include <algorithm>

namespace presentation {
struct NativeItemRow {ModernRow item;int source_row;int selected{-1},sources{0};};
// Adapts the core's existing choices and input state into common UI components.
// No commands are inferred from item names or invented by the renderer.
// Bootstrap screens expose their native selection with a leading "> ".
// Preserve keyless rows (save deletion/confirmation) through native cursor input.
inline std::string native_menu_compact(std::string text) {
    text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}),text.end());
    for(char &c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return text;
}
inline bool native_terminal_navigation(GameFrame &frame) {
    if(!frame.menu_open||frame.menu_core_cursor.line_index>=0||frame.numeric.active||frame.text_input_active||!frame.prompt.choices.empty())return false;
    const int selected_line=frame.menu_term_curs_row,selected_column=frame.menu_term_curs_col;
    if(selected_line>=0&&selected_line<int(frame.menu_term_lines.size())&&!frame.menu_term_lines.empty()) {
        const auto prompt=native_menu_compact(frame.menu_term_lines.front().text_utf8);
        const int source_row=frame.menu_term_lines[selected_line].source_row;
        const bool poker_prompt=prompt.find("keepwhichcards")!=std::string::npos||prompt.find("staywitch?")!=std::string::npos||prompt.find(reinterpret_cast<const char*>(u8"残すカードを決めて"))!=std::string::npos;
        const bool poker_cell=(source_row==14&&selected_column>=5&&selected_column<=69&&(selected_column-5)%16==0)||(source_row==16&&selected_column==38);
        if(poker_prompt&&poker_cell){MenuChoice c;c.line_index=selected_line;c.key=13;frame.menu_core_cursor=c;frame.menu_core_cursors.assign(1,c);frame.menu_choices.clear();return true;}
    }
    if(std::any_of(frame.menu_choices.begin(),frame.menu_choices.end(),[](const auto &c){return c.key!=27&&c.key!=32;}))return false;
    const int row=frame.menu_term_curs_row;
    if(row<0||row>=int(frame.menu_term_lines.size())||frame.menu_term_curs_col<0)return false;
    const auto utf8=[](const char8_t *s){return reinterpret_cast<const char*>(s);};
    std::string heading,footer;bool knowledge=false;
    for(const auto &line:frame.menu_term_lines) {
        const auto text=native_menu_compact(line.text_utf8);
        if(heading.empty()&&!text.empty())heading=text;
        if(line.source_row<=3&&(text.starts_with("knowledge-")||text.starts_with(utf8(u8"知識-"))))knowledge=true;
        if(text.find("esc")!=std::string::npos&&(text.find("<dir>")!=std::string::npos||text.find(utf8(u8"<方向>"))!=std::string::npos||text.find(utf8(u8"<向き>"))!=std::string::npos))footer=text;
    }
    auto selected=frame.menu_term_lines[row].text_utf8;
    while(!selected.empty() && selected.back()==' ')selected.pop_back();
    const auto identifier=selected.rfind('(');
    const bool option_identifier=identifier!=std::string::npos&&selected.ends_with(')')&&identifier+2<selected.size()
        &&std::all_of(selected.begin()+identifier+1,selected.end()-1,[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});
    const bool option_page=heading.find("y/n")!=std::string::npos&&heading.find("esc")!=std::string::npos&&option_identifier;
    const bool window_grid=!footer.empty()&&((heading.find("window")!=std::string::npos&&heading.find("flag")!=std::string::npos)
        ||(heading.find(utf8(u8"ウィンドウ"))!=std::string::npos&&(heading.find(utf8(u8"フラグ"))!=std::string::npos||heading.find(utf8(u8"割り当て"))!=std::string::npos)));
    const bool knowledge_selection=knowledge&&!footer.empty()&&!selected.empty()&&native_menu_compact(selected)!=footer;
    if(!option_page&&!window_grid&&!knowledge_selection)return false;
    MenuChoice cursor;cursor.line_index=row;cursor.span_begin=0;cursor.span_len=0;
    cursor.key=window_grid?'t':(knowledge?(footer.find("'?'" )!=std::string::npos?'?':'r'):13);
    frame.menu_core_cursor=cursor;frame.menu_core_cursors.assign(1,cursor);frame.menu_choices.clear();
    // ページ送りの既定は斜めの方向キー（`9`＝前・`3`＝次。変愚・短愚・幻想蛮怒・Frox・Sil-Q の `browser_cursor` が受ける）。
    // コアのアダプタが先に申告していれば（Angband は空白だけ）そちらを優先する。
    if(knowledge&&!frame.menu_page_prev_key&&!frame.menu_page_next_key){frame.menu_page_prev_key='9';frame.menu_page_next_key='3';}
    return true;
}

inline void native_pregame_selection(GameFrame &frame) {
    if(!frame.pre_game_menu || frame.text_input_active || frame.numeric.active || !frame.prompt.choices.empty())return;
    const bool declared=std::any_of(frame.menu_choices.begin(),frame.menu_choices.end(),[](const auto &c){return c.selected;});
    if(declared)return;
    bool load=false,confirm=false,title=false;int selected=-1;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto text=modern_trim(frame.menu_term_lines[i].text_utf8);
        if(text=="=== Load Game ==="||text=="=== Delete Save ===")load=true;
        if(text.starts_with("This will delete:"))confirm=true;
        if(text.starts_with("> "))selected=int(i);
        if(text.find("N) New Game")!=std::string::npos)title=true;
    }
    if(selected<0||(!load&&!title))return;
    if(load&&!confirm){frame.menu_page_prev_key='7';frame.menu_page_next_key='9';}
    std::vector<MenuChoice> choices;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto &line=frame.menu_term_lines[i].text_utf8;
        auto start=line.find_first_not_of(' ');if(start==std::string::npos)continue;
        const bool picked=line.compare(start,2,"> ")==0;
        if(picked)start=line.find_first_not_of(' ',start+2);
        if(start==std::string::npos)continue;
        const auto text=line.substr(start);
        const bool letter=text.size()>2&&text[1]==')';
        const bool keyless=text.starts_with("* Delete a save file *")||text.starts_with("No, keep it")||text.starts_with("Yes, delete it")||text.starts_with(") ");
        if((title&&(!letter||(text[0]!='N'&&text[0]!='L'&&text[0]!='Q')))||(!title&&!letter&&!keyless))continue;
        MenuChoice choice;choice.line_index=int(i);choice.span_begin=int(start);choice.span_len=int(text.size());
        choice.key_begin=int(start);choice.key_len=letter?1:0;choice.key=title?int(text[0]):0;choice.label_utf8=(!title&&!confirm&&text.starts_with(") "))?modern_trim(text.substr(2)):modern_choice_label(text);choice.selected=picked;
        if(picked){frame.menu_core_cursor=choice;frame.menu_core_cursors.assign(1,choice);}
        choices.push_back(std::move(choice));
    }
    if(!choices.empty())frame.menu_choices=std::move(choices);
}
inline void native_cursor_selection(const GameFrame &frame,ModernWindow &window) {
    const auto &cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    window.cursor=cursor.line_index;window.continue_key=cursor.key?cursor.key:13;
    window.cursor_span_begin=-1;window.cursor_span_len=0;
    if(cursor.line_index<0||cursor.line_index>=int(frame.menu_term_lines.size())||cursor.span_begin<0||cursor.span_len<=0)return;
    const auto &line=frame.menu_term_lines[cursor.line_index].text_utf8;
    if(size_t(cursor.span_begin)>line.size()||size_t(cursor.span_len)>line.size()-size_t(cursor.span_begin))return;
    const auto boundary=[&](size_t at){return at==line.size()||(static_cast<unsigned char>(line[at])&0xc0)!=0x80;};
    if(!boundary(cursor.span_begin)||!boundary(size_t(cursor.span_begin)+cursor.span_len))return;
    window.cursor_span_begin=cursor.span_begin;window.cursor_span_len=cursor.span_len;
}
inline std::string native_readable_body(const GameFrame &frame,ModernWindow &window) {
    if(frame.menu_term_lines.empty() || frame.pre_game_menu)return window.body;
    const auto &heading=frame.menu_term_lines.front().text_utf8;
    const bool inventory=heading.find(reinterpret_cast<const char*>(u8"持ち物"))!=std::string::npos
        ||heading.find(reinterpret_cast<const char*>(u8"荷物袋"))!=std::string::npos
        ||heading.find(reinterpret_cast<const char*>(u8"装備"))!=std::string::npos
        ||heading.find("Inventory")!=std::string::npos||heading.find("Equipment")!=std::string::npos||heading.find("(Inven)")!=std::string::npos;
    const auto trim=[](std::string text){while(!text.empty()&&text.back()==' ')text.pop_back();return text;};
    if(inventory) {
        std::string body=heading+"\n";int rows=0;
        for(size_t i=1;i<frame.menu_term_lines.size();++i){
            const auto &text=frame.menu_term_lines[i].text_utf8;
            for(size_t p=0;p+2<text.size();++p)if(((text[p]>='a'&&text[p]<='z')||(text[p]>='A'&&text[p]<='Z'))&&text[p+1]==')'&&text[p+2]==' '){
                std::string row=text.substr(p);size_t end=std::string::npos;
                for(const char *unit:{" kg"," lbs"," lb"}){auto at=row.find(unit);if(at!=std::string::npos)end=std::min(end,at+std::char_traits<char>::length(unit));}
                if(end!=std::string::npos)row.resize(end);
                body+=trim(row)+"\n";++rows;break;
            }
        }
        if(rows)return body;
    }
    if(window.stage=="choices"&&!frame.pre_game_menu){
        std::string body=heading+"\n";int previous=-1;
        for(const auto &choice:frame.menu_choices){
            if(choice.line_index<0||choice.line_index>=int(frame.menu_term_lines.size())||choice.line_index==previous)continue;
            previous=choice.line_index;const auto &line=frame.menu_term_lines[choice.line_index].text_utf8;
            body+=trim(line.substr(std::clamp(choice.span_begin,0,int(line.size()))))+"\n";
        }
        return body;
    }
    if(window.stage=="cursor"&&frame.menu_core_cursor.span_begin>0){
        std::string body=heading+"\n";int column=frame.menu_core_cursor.span_begin;int painted=1;const int selected=window.cursor;
        for(size_t i=1;i<frame.menu_term_lines.size();++i){
            const auto &line=frame.menu_term_lines[i].text_utf8;if(int(line.size())<=column)continue;
            std::string text=line.substr(column);int content=0,map=0;bool japanese=false;
            for(unsigned char c:text)if(c!=' '){++content;if(c>=128)japanese=true;else if(std::string("#.%~^:+-|'!_/\\<>@&").find(c)!=std::string::npos)++map;}
            if(content&&(int(i)==selected||japanese||map*2<content)){
                text=trim(text);
                if(int(i)==selected){
                    window.cursor=painted;
                    const int begin=std::max(0,window.cursor_span_begin-column);
                    const int end=std::clamp(window.cursor_span_begin+window.cursor_span_len-column,0,int(text.size()));
                    if(window.cursor_span_begin>=0&&end>begin){window.cursor_span_begin=begin;window.cursor_span_len=end-begin;}
                    else {window.cursor_span_begin=-1;window.cursor_span_len=0;}
                }
                body+=text+"\n";++painted;
            }
        }
        return body;
    }
    return window.body;
}
inline void native_menu_key_aliases(ModernWindow &window,const GameFrame &frame) {
    if(window.stage!="choices")return;
    const bool title=frame.pre_game_menu&&window.rows.size()==3&&window.rows[0].key=='N'&&window.rows[1].key=='L'&&window.rows[2].key=='Q';
    bool options=false;
    for(const auto &line:frame.menu_term_lines){
        const auto heading=native_menu_compact(line.text_utf8);
        if(line.source_row<=2&&(heading=="gameoptions"||heading=="tinyangbandoptions"||heading=="froxcomposbandoptions"||heading==reinterpret_cast<const char*>(u8"[オプションの設定]")||heading==reinterpret_cast<const char*>(u8"froxcomposbandの設定")))options=true;
    }
    if(!title&&!options)return;
    // These native title/options switches explicitly accept both cases. Other
    // menus retain distinct a/A commands; only advertised aliases are accepted.
    for(auto &row:window.rows)if(row.key>='A'&&row.key<='Z')row.actions.push_back({"choice",row.label,row.reason,row.available,row.key+'a'-'A'});
}
// These editors use their own input loop. Their active header and caret
// identify editing even when the ordinary get_string hook is not active.
inline void native_editor_input(GameFrame &frame) {
    if(frame.numeric.active||!frame.prompt.choices.empty()||frame.menu_term_curs_row<0||frame.menu_term_curs_row>=int(frame.menu_term_lines.size()))return;
    if(frame.menu_term_lines[frame.menu_term_curs_row].source_row<1)return;
    for(const auto &line:frame.menu_term_lines)if(line.source_row==0&&line.text_utf8.find("^Q")!=std::string::npos&&line.text_utf8.find("^W")!=std::string::npos&&line.text_utf8.find("ESC")!=std::string::npos){frame.text_input_active=true;frame.menu_choices.clear();return;}
}
inline bool native_boxed_editor_choices(GameFrame &frame,const std::string &profile) {
    if((profile!="legacy"&&profile!="frox")||frame.numeric.active||!frame.prompt.choices.empty())return false;
    std::vector<MenuChoice> choices;int borders=0;bool inside=false;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto text=modern_trim(frame.menu_term_lines[i].text_utf8);if(text.empty())continue;
        if(text.size()>4&&text.front()=='+'&&text.back()=='+'&&text.find_first_not_of("+-")==std::string::npos){++borders;inside=borders==1;continue;}
        if(!inside||text.front()!='|'||text.back()!='|')return false;
        auto value=modern_trim(text.substr(1,text.size()-2));
        if(value.size()<4||value[0]<'a'||value[0]>'z'||value[1]!=')'||value[2]!=' ')return false;
        MenuChoice choice;choice.key=value[0];choice.line_index=int(i);choice.span_begin=0;choice.span_len=int(frame.menu_term_lines[i].text_utf8.size());
        value=modern_trim(value.substr(3));const auto gap=value.find("  ");
        if(gap!=std::string::npos){const auto hint=modern_trim(value.substr(gap));value=modern_trim(value.substr(0,gap));if(hint.size()==2&&hint[0]=='^'&&hint[1]>='A'&&hint[1]<='Z')choice.description_utf8="Ctrl+"+hint.substr(1);else if(!hint.empty())value+=" "+hint;}
        choice.label_utf8=value;choices.push_back(std::move(choice));
    }
    if(borders!=2||choices.empty())return false;
    frame.text_input_active=false;frame.menu_core_cursor={};frame.menu_core_cursors.clear();frame.menu_choices=std::move(choices);return true;
}

class NativeModernMenu {
public:
    void set_enabled(bool value) { enabled_ = value; if (!value) { current_ = {}; signature_.clear(); text_scope_revision_=detail_scope_revision_=0;text_scope_id_.clear();detail_scope_id_.clear();detail_scope_body_.clear(); } }
    int last_request() const { return last_request_; }
    const std::string &result() const { return result_; }
    void adopt_request(int id,const std::string &result) { if(id>last_request_) {last_request_=id;result_=result;} }
    void set_title(std::string title){title_=std::move(title);}
    void set_language(std::string language){language_=language=="ja"?"ja":"en";}
    void set_document_profile(std::string profile){document_profile_=std::move(profile);}
    bool enabled() const { return enabled_; }
    bool active() const { return enabled_ && !current_.stage.empty(); }
    void capture(GameFrame &frame,const std::vector<ModernAction> &declared_actions={},const std::vector<NativeItemRow> &items={}) {
        if (!enabled_ || !frame.modern.stage.empty() || !frame.command_menu_keys.empty()) { current_ = {}; return; }
        native_editor_input(frame);
        const bool boxed_editor=native_boxed_editor_choices(frame,document_profile_);
        native_terminal_navigation(frame);
        native_pregame_selection(frame);
        if(frame.prompt.lightweight){current_={};return;}
        ModernWindow window;window.language=language_;
        //! 見る・狙うの最中にコアの画面（モンスターの思い出）が重なっている間は、アダプタが見るの帯を送らない
        //! （`look_screen_cover.h`）ので、問いは立たず、重なった画面が本文の窓になる。
        const bool modern_prompt=!frame.prompt.lightweight&&!frame.prompt.choices.empty();
        /*
         * AH-35。以前はこの分岐へ `message_more_pending`
         * だけで（`frame.menu_open` が偽のままでも）入れていた。5 コアとも `-more-` の
         * 合図（`frame.prompt.text_utf8` が `-more-` で終わる。`fill_row0_prompt()`/
         * `fill_prompts()` 系が立てる）自体は `menu_open` に関わらず立つ——「別の
         * キーでやり過ごしてから次のコマンドを送りたい」呼び手（`tools/angband/
         * ah15_floor_list_continue_smoke.py` の `send_and_settle()` など）はこれを
         * そのまま読むので、その合図自体は消さない。ここで直すのは**モーダルの
         * 組み方**——店・知識などの実在のメニュー（`menu_open` 真）の中で出る
         * `-more-` は、その開いているメニューへ重ねて見せたい（変愚・幻想蛮怒・
         * Sil-Q は実際にこの形でしか出さない）ので `window.kind="dialog"` の
         * ままでよいが、**通常のダンジョン画面（`menu_open` 偽）で `-more-` だけが
         * 立っている**ときにまで同じ全画面のモーダルを組むと、実在するメニューが
         * 1 つも無いのに全画面表示になる——Angband の生年直後の最初のメッセージが
         * これで全画面化していた。**FroxComposband は通常の
         * ダンジョン画面でも `-more-` を出す**（`fc_menu.cpp::fill_row0_prompt()`
         * の `fc_awaiting_command()==0` の枝は `menu_open` を見ていない）ので、
         * この分岐は Angband だけの事情ではない。`menu_open` 偽のときは
         * `message_more_pending()` だけでは入り口にしない——メッセージは
         * サブパネル（`fill_messages()`。`menu_open` に関わらず毎回埋まる）と
         * プロンプト帯（`draw_prompt_bar()`。`frame.prompt.footer_utf8` に
         * 「続ける」の案内を乗せられる）にだけ出す。
         */
        if (frame.menu_open || frame.numeric.active || frame.text_input_active || modern_prompt) {
            window.version = 5;
            window.kind = "dialog";
            window.title = title_;
            // AH-12。`frame.menu_box_header_utf8` はメニューの箱の外の見出し・説明
            // （店の名前と所持金・呪文の説明）——Angband のアダプタだけが積む
            // （既定は空文字列）ので、他のコアはこの行を通っても
            // `window.subtitle` は今までどおり空のままになる。
            if (!frame.menu_box_header_utf8.empty()) window.subtitle = frame.menu_box_header_utf8;
            window.prev_key = frame.menu_page_prev_key;
            window.next_key = frame.menu_page_next_key;
            window.input = modern_prompt_text(frame);
            for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
                const auto &line = frame.menu_term_lines[i];
                const bool is_input = (frame.numeric.active && int(i) == frame.numeric.line_index)
                    || (!frame.prompt.choices.empty() && int(i) == frame.prompt.line_index)
                    || (frame.text_input_active && !message_more_pending(frame) && int(i) == frame.menu_term_curs_row);
                if (!is_input) window.body += line.text_utf8;
                window.body += '\n';
            }
            if (window.input.empty() && frame.text_input_active && frame.menu_term_curs_row >= 0
                && frame.menu_term_curs_row < int(frame.menu_term_lines.size()))
                window.input = frame.menu_term_lines[frame.menu_term_curs_row].text_utf8;
            if (frame.numeric.active) window.stage = "number";
            else if (modern_prompt) window.stage = "confirm";
            else if (message_more_pending(frame)) { window.stage = "detail"; window.continue_key = ' '; }
            else if (frame.text_input_active && !modern_is_editor_menu(frame)) {window.stage = "text";window.text_confirm_action=true;}
            else if (frame.menu_choices.empty() && (frame.menu_core_cursor.line_index >= 0 || !frame.menu_core_cursors.empty())) {
                window.stage = "cursor"; native_cursor_selection(frame,window);
            } else if (!frame.menu_choices.empty()) {
                window.stage = "choices";
                for (const auto &choice : frame.menu_choices) {
                    if ((choice.line_index < 0 || choice.line_index >= int(frame.menu_term_lines.size())) && choice.label_utf8.empty()) continue;
                    const std::string text=(choice.line_index>=0&&choice.line_index<int(frame.menu_term_lines.size()))?frame.menu_term_lines[choice.line_index].text_utf8:choice.label_utf8;
                    int start = std::clamp(choice.span_begin, 0, int(text.size()));
                    int size = std::clamp(choice.span_len, 0, int(text.size()) - start);
                    ModernRow row; row.key = choice.key; row.hotkey = choice.key;
                    row.label = choice.label_utf8.empty()?(size ? text.substr(start, size) : text):choice.label_utf8;
                    row.id = "choice:" + std::to_string(window.rows.size()) + ":" + std::to_string(choice.line_index) + ":" + std::to_string(choice.span_begin) + ":" + std::to_string(choice.key);
                    unsigned long long identity=1469598103934665603ULL;for(unsigned char c:row.label){identity^=c;identity*=1099511628211ULL;}row.id+=":"+std::to_string(identity);
                    row.price_known = false;row.available=choice.available;row.reason=choice.reason_utf8;row.description=choice.description_utf8;
                    if(choice.selected || (choice.line_index>=0 && choice.line_index==frame.menu_core_cursor.line_index && (frame.menu_core_cursor.span_len<=0 || choice.span_begin==frame.menu_core_cursor.span_begin)))window.cursor=int(window.rows.size());
                    row.actions.push_back({"choice", row.label, row.reason, row.available, choice.key});
                    window.rows.push_back(std::move(row));
                }
            } else { window.stage = "detail"; window.continue_key = 13; }
        }
        if(window.stage=="text" && window.body.find("^Q")!=std::string::npos && window.body.find("^W")!=std::string::npos) {window.prev_key=15;window.next_key=12;}
        if(window.stage=="detail" && !frame.text_input_active && frame.menu_term_curs_row>=0 && frame.menu_term_curs_row<int(frame.menu_term_lines.size())) {
            const auto line=native_menu_compact(frame.menu_term_lines[frame.menu_term_curs_row].text_utf8);
            const auto utf8=[](const char8_t *s){return reinterpret_cast<const char*>(s);};
            window.capture_keys=line=="trigger:"||line=="keypress:"||line==utf8(u8"トリガーキー:")||line==utf8(u8"押すキー:")||line==utf8(u8"引き金:");
        }
        // AH-16 (second half): Angband's keymap create/query/remove screen sets
        // frame.capture_keys (an_main.cpp's fill_prompts()) instead of relying on the
        // "trigger:"/"keypress:" cursor-row text above (it has no such row -- see
        // game_frame.h's capture_keys comment). Reuse the same shared
        // ModernWindow::capture_keys contract rather than adding a second one; this
        // runs after the block above so it can only turn the flag on, never overwrite
        // it back to false.
        if(frame.capture_keys && window.stage=="detail") window.capture_keys=true;
        if(frame.pre_game_menu&&window.stage=="choices")for(const auto &row:window.rows) {
            if(row.key=='['&&(row.label=="Previous page"||row.label==reinterpret_cast<const char*>(u8"前のページ")))window.prev_key='[';
            if(row.key==']'&&(row.label=="Next page"||row.label==reinterpret_cast<const char*>(u8"次のページ")))window.next_key=']';
        }
        native_menu_key_aliases(window,frame);
        interpret_modern_content(frame,window,document_profile_);
        // A few building services paint their filtered inventory directly and
        // expose only a native cursor (no MenuChoice records and, depending on
        // the subwindow update order, no live item references).  Keep those
        // visible inventory rows selectable in the modern left pane.  The
        // native cursor remains authoritative: navigation sends 8/2 and the
        // selected row sends Enter.
        const std::string terminal_prompt=window.input.empty()?window.body:window.input;
        const bool terminal_item_prompt=(terminal_prompt.find(reinterpret_cast<const char*>(u8"どのアイテム"))!=std::string::npos)
            ||(terminal_prompt.find("Which item")!=std::string::npos)||(terminal_prompt.find("which item")!=std::string::npos);
        if(items.empty()&&!frame.pre_game_menu&&window.stage=="cursor") {
            if(terminal_item_prompt) {
                std::vector<ModernRow> terminal_items;
                const auto &native_cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
                const int selected_line=native_cursor.line_index>=0&&native_cursor.line_index<int(frame.menu_term_lines.size())
                    ?frame.menu_term_lines[native_cursor.line_index].source_row:-1;
                int selected=-1;
                for(const auto &line:frame.menu_term_lines) {
                    if(line.source_row<=0||line.source_row==frame.menu_term_lines.front().source_row)continue;
                    std::string text=modern_trim(line.text_utf8);
                    const std::string arrow=reinterpret_cast<const char*>(u8"》");
                    if(text.starts_with(arrow))text=modern_trim(text.substr(arrow.size()));
                    if(text.empty())continue;
                    ModernRow row;row.slot=line.source_row;row.selection=0;row.key=13;row.hotkey=0;row.price_known=false;
                    if(text.size()>1&&text[1]==' '&&!std::isalnum(static_cast<unsigned char>(text[0]))) {
                        row.glyph=text.substr(0,1);text=modern_trim(text.substr(2));
                    }
                    const auto kg=text.rfind(" kg"),lb=text.rfind(" lb");
                    const auto unit=kg!=std::string::npos?kg:lb;
                    if(unit!=std::string::npos) {
                        const auto split=text.rfind("  ",unit);
                        if(split!=std::string::npos){row.weight=modern_trim(text.substr(split));text=modern_trim(text.substr(0,split));}
                    }
                    row.label=text;row.available=!row.label.empty();
                    unsigned long long identity=1469598103934665603ULL;for(unsigned char c:row.label){identity^=c;identity*=1099511628211ULL;}
                    row.id="terminal-item:"+std::to_string(line.source_row)+":"+std::to_string(identity);
                    row.actions.push_back({"choice",row.label,"",row.available,13});
                    if(line.source_row==selected_line)selected=int(terminal_items.size());
                    terminal_items.push_back(std::move(row));
                }
                if(!terminal_items.empty()) {
                    window.rows=std::move(terminal_items);window.content.clear();window.body.clear();window.stage="choices";window.content_type="items";
                    window.title=language_=="ja"?reinterpret_cast<const char*>(u8"アイテムの選択"):"Select an item";
                    window.cursor=selected>=0?selected:0;window.cursor_target=window.rows[window.cursor].id;
                }
            }
        }
        // Race and class choices are already explicit selections.  The native
        // cores may insert a redundant yes/no screen; acknowledge its
        // affirmative choice immediately so the modern flow proceeds to the
        // next creation step.
        if(frame.pre_game_menu&&window.stage=="confirm") {
            const bool after_race=current_.title=="Choose a race"||current_.title==reinterpret_cast<const char*>(u8"種族の選択");
            const bool after_class=current_.title=="Choose a class"||current_.title==reinterpret_cast<const char*>(u8"職業の選択");
            const bool awaiting_automatic_answer=current_.stage=="confirm"&&current_.auto_select.starts_with("@answer:");
            if(after_race||after_class||awaiting_automatic_answer)for(size_t i=0;i<frame.prompt.choices.size();++i) {
                const auto &choice=frame.prompt.choices[i];
                if(choice.key=='y'||choice.key=='Y'||choice.label_utf8=="Yes"||choice.label_utf8==reinterpret_cast<const char*>(u8"はい")) {
                    window.auto_select="@answer:"+std::to_string(i);break;
                }
            }
        }
        if(!items.empty()&&!frame.pre_game_menu&&(window.stage=="choices"||window.stage=="cursor"||window.stage=="detail")
            &&!window.message_only&&(!modern_document_info(frame).active||terminal_item_prompt)&&window.content_type!="browser") {
            const auto &native_cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
            const int selected_line=native_cursor.line_index>=0&&native_cursor.line_index<int(frame.menu_term_lines.size())
                ?frame.menu_term_lines[native_cursor.line_index].source_row:-1;
            const bool cursor_driven=window.stage=="cursor"||std::any_of(items.begin(),items.end(),[](const auto &r){return r.selected>=0;});
            window.rows.clear();window.content.clear();window.body.clear();window.cursor=-1;window.cursor_target.clear();window.stage="choices";window.content_type="items";
            window.title=language_=="ja"?reinterpret_cast<const char*>(u8"アイテムの選択"):"Select an item";
            window.input=modern_prompt_text(frame);
            for(const auto &entry:items) {
                auto row=entry.item;
                if(cursor_driven){row.key=13;row.hotkey=0;}
                if(entry.selected==1||(cursor_driven&&entry.selected<0&&entry.source_row==selected_line))window.cursor=int(window.rows.size());
                row.actions={{"choice",row.label,"",row.available,row.key}};
                window.rows.push_back(std::move(row));
            }
            const int sources=items.front().sources&7;
            if(sources&&(sources&(sources-1))) {
                if(cursor_driven){modern_add_action(window,'4',language_=="ja"?reinterpret_cast<const char*>(u8"前のアイテム一覧"):"Previous item list");modern_add_action(window,'6',language_=="ja"?reinterpret_cast<const char*>(u8"次のアイテム一覧"):"Next item list");}
                else {
                    if((sources&3)==3)modern_add_action(window,'/',language_=="ja"?reinterpret_cast<const char*>(u8"持ち物・装備を切替"):"Switch inventory / equipment");
                    if(sources&4)modern_add_action(window,'-',language_=="ja"?reinterpret_cast<const char*>(u8"床のアイテム"):"Floor items");
                }
            }
            for(const auto &choice:frame.menu_choices) {
                if(document_profile_=="frox"&&(choice.key=='3'||choice.key=='9')&&window.content_type=="items")continue;
                // Source-list navigation was exposed above with a complete,
                // localized label.  Do not add the native prompt fragment as
                // a second action (for example the stray "or 装備品").
                if((sources&&(sources&(sources-1)))
                    &&((cursor_driven&&(choice.key=='4'||choice.key=='6'))
                        ||(!cursor_driven&&(choice.key=='/'||choice.key=='-'))))continue;
                if(!choice.key||std::any_of(items.begin(),items.end(),[&](const auto &r){return r.item.key==choice.key;}))continue;
                const auto label=choice.label_utf8.empty()&&choice.line_index>=0&&choice.line_index<int(frame.menu_term_lines.size())
                    ?frame.menu_term_lines[choice.line_index].text_utf8.substr(std::max(0,choice.span_begin),std::max(0,choice.span_len)):choice.label_utf8;
                modern_add_action(window,choice.key,modern_choice_label(label));
            }
        }
        if(boxed_editor) {
            window.content_type="editor_menu";window.title=language_=="ja"?reinterpret_cast<const char*>(u8"自動拾いの編集メニュー"):"Auto-pick editing menu";
            window.body.clear();window.content.clear();window.actions.clear();window.cursor=-1;window.cursor_target.clear();
            for(auto &row:window.rows)if(row.description.size()==6&&row.description.starts_with("Ctrl+"))row.actions.push_back({"choice",row.label,"",true,row.description.back()&31});
        }
        if(window.content_type=="browser") {
            if(current_.content_type!="browser"||current_.title!=window.title){browser_group_focus_=true;browser_group_id_.clear();}
            const bool has_items=std::any_of(window.rows.begin(),window.rows.end(),[](const auto &r){return r.selection==1;});
            if(has_items) {
                const auto at=std::find_if(window.rows.begin(),window.rows.end(),[&](const auto &r){return r.id==window.cursor_target;});
                if(at!=window.rows.end())browser_group_focus_=at->selection==0;
                browser_group_id_.clear();
            } else {
                std::string group;
                for(const auto &r:window.rows)if(r.selection==0&&r.slot>=0&&r.slot<int(frame.menu_term_lines.size())) {
                    const auto &line=frame.menu_term_lines[r.slot];const auto start=line.text_utf8.find_first_not_of(' ');int color=line.color;
                    for(const auto &span:line.color_spans)if(int(start)>=span.begin&&int(start)<span.begin+span.len){color=span.color;break;}
                    if((color&15)==14){group=r.id;break;}
                }
                if(group.empty())for(const auto &r:window.rows)if(r.selection==0){group=r.id;break;}
                if(!browser_group_id_.empty()&&group!=browser_group_id_)browser_group_focus_=true;
                browser_group_id_=group;window.cursor_target=browser_group_focus_?group:std::string();
                for(auto &block:window.content)for(auto &cell:block.cells)cell.selected=!window.cursor_target.empty()&&cell.target==window.cursor_target;
                window.actions.clear();window.continue_key='4';
                ModernBlock empty;empty.kind="paragraph";empty.cells.push_back({"",language_=="ja"?reinterpret_cast<const char*>(u8"この分類に該当する項目はありません。"):"There are no entries in this group.",false});window.content.push_back(std::move(empty));
                window.body+=(window.body.empty()?"":"\n")+window.content.back().cells.front().text;
            }
        }
        if(window.stage=="choices"||window.stage=="cursor"||(window.stage=="text"&&frame.player_name_input_active))for(const auto &action:declared_actions){if(window.content_type=="spells"&&action.key=='*')continue;modern_add_action(window,action.key,action.label);}
        modern_retain_building_result(window,current_);
        if(window.stage==current_.stage&&window.title==current_.title
            &&std::any_of(window.rows.begin(),window.rows.end(),[&](const auto &row){return row.id==current_.preview_target;}))window.preview_target=current_.preview_target;
        auto signature_window=window;signature_window.preview_target.clear();signature_window.can_preview=false;
        const auto next = encode_modern_window(signature_window).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
            + frame.prompt.text_utf8 + ":" + std::to_string(frame.menu_term_curs_col)
            + ":" + std::to_string(frame.numeric.min) + ":" + std::to_string(frame.numeric.max) + ":" + std::to_string(frame.numeric.value);
        // A core may drain several received events before processing its first key.
        // Only one action may consume a captured state. Publish a fresh revision
        // even after a native no-op so the next deliberate action remains usable.
        const bool refreshed_after_action=consumed_;
        if (next != signature_ || consumed_) { signature_ = next; ++revision_; consumed_ = false; }
        const std::string text_scope=window.title+"\n"+window.input_label+"\n"+window.content_type+"\n"+window.text_edit_mode;
        if(window.stage=="text") {
            if(current_.stage!="text"||text_scope_id_!=text_scope)text_scope_revision_=revision_;
            text_scope_id_=text_scope;
        } else {text_scope_revision_=0;text_scope_id_.clear();}
        std::string detail_scope;
        if(window.stage=="detail") {
            detail_scope=window.kind+"\n"+window.stage+"\n"+window.content_type+"\n"+window.title+"\n"+window.subtitle
                +"\n"+std::to_string(window.continue_key)+":"+std::to_string(window.page)+":"+std::to_string(window.page_size)
                +":"+std::to_string(window.previous)+":"+std::to_string(window.next)+":"+std::to_string(window.prev_key)+":"+std::to_string(window.next_key);
            for(const auto &action:window.actions)detail_scope+="\nA:"+action.id+":"+std::to_string(action.key)+":"+std::to_string(action.enabled);
            for(const auto &row:window.rows) {
                detail_scope+="\nR:"+row.id+":"+std::to_string(row.key)+":"+std::to_string(row.available);
                for(const auto &action:row.actions)detail_scope+=":A:"+action.id+":"+std::to_string(action.key)+":"+std::to_string(action.enabled);
            }
            auto previous_body=detail_scope_body_;
            while(!previous_body.empty()&&(previous_body.back()==' '||previous_body.back()=='\t'||previous_body.back()=='\r'||previous_body.back()=='\n'))previous_body.pop_back();
            const bool same_growing_detail=!refreshed_after_action&&current_.stage=="detail"&&detail_scope_id_==detail_scope
                &&window.body.starts_with(previous_body);
            if(!same_growing_detail)detail_scope_revision_=revision_;
            detail_scope_id_=detail_scope;detail_scope_body_=window.body;
        } else {detail_scope_revision_=0;detail_scope_id_.clear();detail_scope_body_.clear();}
        window.revision = revision_; window.request_id = last_request_; window.result = result_;
        current_ = window; numeric_ = frame.numeric; prompt_ = frame.prompt;
        frame.modern = std::move(window);
    }
    bool input(const InputEventWire &event, std::vector<int> &keys) {
        if (event.e != "ui_action" || !active()) return false;
        if (event.request_id <= last_request_) return true;
        last_request_ = event.request_id; result_ = "rejected";
        // Text echo may arrive between typing and pressing Enter/Escape.
        // These actions still refer to the same live field; a new field or a
        // subsequent confirmation has a separate scope and remains protected.
        const bool live_text_end=current_.stage=="text"&&(event.action=="text_confirm"||event.action=="cancel")
            &&text_scope_revision_>0&&event.ui_revision>=text_scope_revision_&&event.ui_revision<=revision_;
        // Some native narrative screens paint one more line every frame. A
        // Continue pressed while that same document is growing still names the
        // same operation. A body replacement, structural change, or accepted
        // action starts a fresh scope, so delayed input cannot cross screens.
        const bool live_detail_continue=current_.stage=="detail"&&event.action=="continue"
            &&detail_scope_revision_>0&&event.ui_revision>=detail_scope_revision_&&event.ui_revision<=revision_;
        if ((event.ui_revision != revision_&&!live_text_end&&!live_detail_continue) || consumed_) return true;
        if(current_.content_type=="quick_help"&&(event.action=="cancel"||(event.action=="window_action"&&event.target=="quick-close"))) {
            for(int i=0;i<3-std::clamp(current_.page,0,2);++i)keys.push_back(27);
            result_="accepted";consumed_=true;return true;
        }
        const bool empty_browser=current_.content_type=="browser"&&std::none_of(current_.rows.begin(),current_.rows.end(),[](const auto &r){return r.selection==1;});
        if(empty_browser&&event.action=="native_key"&&std::string_view("rRvVcCpP?").find(char(event.number))!=std::string_view::npos)return true;
        auto row = std::find_if(current_.rows.begin(), current_.rows.end(), [&](const auto &r) { return r.id == event.target; });
        int key = 0;
        if(event.action=="preview"&&(current_.stage=="choices"||current_.stage=="cursor")&&row!=current_.rows.end()) {
            current_.preview_target=row->id;result_="accepted";return true;
        }
        if (event.action == "cancel") key = 27;
        else if(event.action=="focus"&&current_.stage=="cursor"&&row!=current_.rows.end()) {
            if(current_.cursor_target==event.target){result_="accepted";return true;}
            key=modern_focus_key(current_,event.target);
        }
        else if(event.action=="window_action") {
            for(const auto &a:current_.actions)if(a.id==event.target&&a.enabled)key=a.key;
        }
        else if(event.action=="row_action"&&row!=current_.rows.end()&&row->available) {
            if(current_.stage=="cursor"&&row->id!=current_.cursor_target)return true;
            for(const auto &a:row->actions)if(a.enabled&&a.key==event.number)key=a.key;
        }
        else if (current_.stage == "text" && event.action == "text_confirm") key=13;
        else if ((current_.stage == "choices"||current_.stage=="cursor") && event.action == "choice" && row != current_.rows.end() && row->available)
            key = row->key ? row->key : (int(row-current_.rows.begin())==current_.cursor?13:0);
        else if ((current_.stage == "cursor" || (current_.stage=="choices"&&current_.cursor>=0)) && event.action == "navigate"
            && (event.number == 2 || event.number == 4 || event.number == 6 || event.number == 8)) key = '0' + static_cast<int>(event.number);
        else if ((current_.stage == "detail" || current_.stage == "cursor") && event.action == "continue") key = current_.continue_key;
        else if (event.action == "document_page") key = event.number ? current_.next_key : current_.prev_key;
        else if (current_.stage == "confirm" && event.action == "answer" && event.number >= 0 && event.number < int(prompt_.choices.size())) key = prompt_.choices[event.number].key;
        else if (current_.stage == "number" && event.action == "number" && event.number >= numeric_.min && event.number <= numeric_.max) {
            InputEventsMessage message; InputEventWire number; number.e = "set_number"; number.number = event.number; number.digits = 1;
            message.events.push_back(number); append_input_event_keys(message, keys); key = 13;
        } else if (event.action == "native_key" && event.number > 0 && event.number < 128
            && current_.content_type != "settings_list_rogue") {
            // Rogue の設定一覧は上下・左右・取消だけで操作する。文字キーを原作へ
            // 渡すと、原作が値の位置へ "(Yes or No)" などを書いて一覧が崩れる。
            const bool listed = std::any_of(current_.rows.begin(), current_.rows.end(), [&](const auto &r) { return r.key == event.number && r.available; });
            if (listed || current_.stage == "detail" || current_.stage == "cursor") key = static_cast<int>(event.number);
        }
        if (key) {
            if(current_.content_type=="browser") {
                if(key=='4'||key=='1'||key=='7')browser_group_focus_=true;
                if(key=='6')browser_group_focus_=false;
            }
            keys.push_back(key); result_ = "accepted"; consumed_ = true;
        }
        return true;
    }
    bool rogue_settings_active() const { return current_.content_type=="settings_list_rogue"; }
private:
    bool enabled_{false}, consumed_{false},browser_group_focus_{true};
    std::string browser_group_id_;
    int revision_{1000000}, last_request_{0},text_scope_revision_{0},detail_scope_revision_{0};
    std::string text_scope_id_,detail_scope_id_,detail_scope_body_;
    std::string signature_, result_,title_,language_{"en"},document_profile_{"generic"};
    ModernWindow current_;
    NumericInput numeric_;
    PromptBar prompt_;
};
}
