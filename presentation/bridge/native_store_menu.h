#pragma once
#include "bridge/native_modern.h"
#include "bridge/native_store_data.h"
#include <cstdio>
#include <cstring>
#include <functional>

namespace presentation {
inline std::string native_store_id(int kind,const native_store_item &item,const std::string &label) {
    unsigned long long hash=1469598103934665603ULL;
    for(unsigned char c:label){hash^=c;hash*=1099511628211ULL;}
    return "store:"+std::to_string(kind)+":"+std::to_string(item.tab)+":"+std::to_string(item.slot)+":"+std::to_string(item.position)+":"+std::to_string(hash)+":"+std::to_string(item.count)+":"+std::to_string(item.price);
}
class NativeStoreMenu {
public:
    void set_enabled(bool enabled,bool japanese,const std::string &profile="generic") { enabled_=enabled;japanese_=japanese;direct_commands_=profile=="frox";show_command_hub_=profile!="genso";fallback_.set_language(japanese?"ja":"en");fallback_.set_document_profile(profile=="genso"?"legacy":profile);if(!enabled){current_={};pending_.clear();} }
    void capture(GameFrame &f,const native_store_info &info,std::vector<ModernRow> rows) {
        info_=info;choices_=f.menu_choices;numeric_=f.numeric;prompt_=f.prompt;lines_=f.menu_term_lines;native_cursor_=f.menu_core_cursor.line_index;native_cursor_col_=f.menu_core_cursor.span_begin;
        body_.clear();for(const auto &line:f.menu_term_lines)body_+=line.text_utf8+"\n";
        if(!enabled_||!info.active||(!f.menu_open&&!f.numeric.active&&!f.text_input_active&&f.prompt.choices.empty())) {
            /* 店を出たら窓の署名も空にする（修-17）。同じ店の同じ在庫へ入り直すと署名が前と一致し、版（revision_）も
             * 取消で立てた consumed_ も戻らず、入り直した窓の操作が全部「受け付け済み」として断られていた。
             * 版の番号そのものは戻さない（戻すと古い操作が通る）。空にすれば次の窓は必ず版が進み、consumed_ も下りる。 */
            current_={};pending_.clear();last_action_.clear();finishing_=false;signature_.clear();fallback_.set_enabled(false);return;
        }
        ModernWindow w;w.language=japanese_?"ja":"en";w.version=5;w.kind=info.kind==2?"home":(info.kind==3?"museum":"store");
        w.title=title_;w.rows=std::move(rows);w.page=info.top;w.page_size=info.page_size;
        w.venue_header=heading_.empty()?title_:heading_;
        w.prev_key=info.previous_key;w.next_key=info.next_key;w.input=modern_prompt_text(f);
        const bool browse=info.root_frame>=0?info.root_frame!=0:command_band(f,info);
        if(browse&&selected_){pending_.clear();selected_=false;source_sent_=false;consumed_=false;}
        bool use_fallback=false;
        if(f.numeric.active)w.stage="number";
        else if(!f.prompt.choices.empty())w.stage="confirm";
        else if(message_more_pending(f)){w.stage="message";w.continue_key=' ';}
        else if(f.text_input_active){w.stage="text";w.text_confirm_action=true;}
        else if(manual_&&!browse)use_fallback=true;
        else if(!pending_.empty()&&!selected_)w.stage=own_action(pending_)?"own_choose":"choose";
        else if(browse)w.stage="browse";
        else use_fallback=true;
        if(browse)manual_=false;
        if(last_action_=="remove"&&info.remove_key&&!f.numeric.active&&f.prompt.choices.empty()&&!f.text_input_active&&!message_more_pending(f)) {
            const bool target_gone=std::none_of(w.rows.begin(),w.rows.end(),[&](const auto &row){return row.id==action_target_;});
            if(target_gone){finishing_=true;use_fallback=false;w.stage="finish";}
            else if(w.stage=="browse")w.stage="choose";
        }
        if(use_fallback){
            fallback_.set_enabled(true);fallback_.adopt_request(last_request_,result_);fallback_.capture(f);
            current_=f.modern;return;
        }
        fallback_.set_enabled(false);
        if(w.stage=="browse"){
            ModernRow operations;operations.id="store:commands";operations.tab=0;operations.slot=w.page;
            operations.label=tr(reinterpret_cast<const char*>(u8"店の操作"),"Store commands");operations.price_known=false;
            for(const auto &c:f.menu_choices){
                std::string label=choice_text(f,c);
                if(!c.key||c.key==info.get_key||c.key==info.put_key||c.key=='b'||c.key=='g'||c.key=='p'||c.key=='s'||c.key=='d'||c.key=='x'||c.key==27)continue;
                if(direct_commands_&&(c.key=='R'||c.key=='H'||c.key=='U'||c.key=='B'))continue;
                // The modern list already scrolls over every stock item. Keep
                // native paging only for selecting a purchase on its core page.
                if(direct_commands_&&(c.key==info.previous_key||c.key==info.next_key||c.key=='q'||c.key=='Q'||c.key==info.remove_key))continue;
                if(label.empty()||(!is_command_choice(f,c)))continue;
                operations.actions.push_back({"command:"+std::to_string(c.key),label,"",true,c.key});
            }
            if(direct_commands_)for(const auto &action:operations.actions) {
                ModernRow command;command.id="store:"+action.id;command.label=action.label;command.price_known=false;
                command.actions.push_back(action);w.rows.push_back(std::move(command));
            }
            else if(show_command_hub_&&!operations.actions.empty())w.rows.push_back(std::move(operations));
            if(direct_commands_&&(w.kind=="home"||w.kind=="museum")
                &&std::none_of(operations.actions.begin(),operations.actions.end(),[](const auto &action){return action.key=='?';})) {
                // Frox Home/Museum writes "? for help" as ordinary prose,
                // not as a parsed command token.  Keep the native command
                // available on the Modern surface without changing the
                // underlying store/list navigation model.
                ModernRow help;help.id="store:command:63";help.tab=0;help.slot=w.page;help.price_known=false;
                help.label=tr(reinterpret_cast<const char*>(u8"ヘルプ"),"Help");
                help.actions.push_back({"command:63",help.label,"",true,'?'});w.rows.push_back(std::move(help));
            }
            if(direct_commands_&&info.buy_all_key) {
                ModernRow command;command.id="store:command:"+std::to_string(info.buy_all_key);command.tab=0;command.slot=w.page;command.price_known=false;
                command.label=tr(reinterpret_cast<const char*>(u8"全部買う（合計 "),"Buy everything (total ")+std::to_string(info.buy_all_total)+tr(reinterpret_cast<const char*>(u8"ゴールド）")," gold)");
                command.available=true;
                command.reason=f.hud.gold>=info.buy_all_total?"":tr(reinterpret_cast<const char*>(u8"所持金が足りないため、払える分だけ順に購入し、足りなくなった所で止まります。"),"Not enough gold for everything; items are bought in order until the gold runs out.");
                command.actions.push_back({"command:"+std::to_string(info.buy_all_key),command.label,command.reason,command.available,info.buy_all_key});w.rows.push_back(std::move(command));
            }
            if(direct_commands_&&info.show_hidden_key&&info.hidden_count>0) {
                ModernRow command;command.id="store:command:"+std::to_string(info.show_hidden_key);command.tab=0;command.slot=w.page;command.price_known=false;
                command.label=tr(reinterpret_cast<const char*>(u8"隠したアイテムを再表示（"),"Show hidden items (")+std::to_string(info.hidden_count)+")";
                command.actions.push_back({"command:"+std::to_string(info.show_hidden_key),command.label,"",true,info.show_hidden_key});w.rows.push_back(std::move(command));
            }
        }
        interpret_modern_content(f,w);
        auto sig=encode_modern_window(w).dump(-1,' ',false,nlohmann::json::error_handler_t::replace)+body_+f.prompt.text_utf8
            +std::to_string(f.numeric.value)+":"+std::to_string(f.numeric.min)+":"+std::to_string(f.numeric.max)+":"+std::to_string(f.menu_core_cursor.line_index);
        const bool published_after_action=direct_commands_&&consumed_;
        if(sig!=signature_||published_after_action){signature_=std::move(sig);++revision_;consumed_=false;if(published_after_action)stage_revision_=revision_;}
        {const std::string scope=w.stage+"\n"+f.prompt.text_utf8+"\n"+f.numeric.prompt_utf8+"\n"+title_;if(scope!=stage_scope_||stage_revision_>revision_){stage_scope_=scope;stage_revision_=revision_;}}
        w.revision=revision_;w.request_id=last_request_;w.result=result_;current_=w;f.modern=std::move(w);
    }
    void set_title(std::string title){title_=std::move(title);}
    void set_heading(std::string heading){heading_=std::move(heading);}
    void reject_queued_request(int id){if(id>last_request_){last_request_=id;result_="rejected";fallback_.adopt_request(id,result_);}}
    bool input(const InputEventWire &e,std::vector<int> &keys){
        if(e.e!="ui_action"||!enabled_||current_.stage.empty())return false;
        if(fallback_.active()){
            bool handled=fallback_.input(e,keys);last_request_=fallback_.last_request();result_=fallback_.result();
            if(e.action=="choice"&&!keys.empty())selected_=true;
            return handled;
        }
        if(e.request_id<=last_request_)return true;last_request_=e.request_id;result_="rejected";
        /* 同じ段・同じ問いの間（`stage_revision_` 以降）に画が描き直されて版が進んだだけなら、画面の操作は有効のまま。
         * 対象の行は id（名前・個数・値段で決まる）で、数量・答えはいまの問いの範囲で、いまの状態に照らして検める（下）。
         * キーそのもの（native_key）は、画が変わると別の意味になるので、版が一致するときだけ受ける。 */
        const bool live_stage_action=direct_commands_&&e.action!="native_key"
            &&e.ui_revision>=stage_revision_&&e.ui_revision<=revision_;
        if((e.ui_revision!=revision_&&!live_stage_action)||(consumed_&&e.action!="cancel"))return true;
        auto row=std::find_if(current_.rows.begin(),current_.rows.end(),[&](const auto &r){return r.id==e.target;});
        int key=0;
        if(e.action=="finish"&&finishing_){key=27;finishing_=false;last_action_.clear();pending_.clear();selected_=false;}
        else if(e.action=="cancel"){key=27;last_action_.clear();finishing_=false;pending_.clear();selected_=false;manual_=false;source_sent_=false;}
        else if(e.action=="native_key"&&e.number>0&&e.number<128){
            if(std::any_of(choices_.begin(),choices_.end(),[&](const auto &c){return c.key==e.number;})||e.number==current_.prev_key||e.number==current_.next_key||synonym_of_declared_command(e.number))key=static_cast<int>(e.number);
        }
        else if(current_.stage=="browse"&&row!=current_.rows.end()){
            if(e.action=="page_for"&&row->tab==0)key=row->slot<info_.top?info_.previous_key:info_.next_key;
            else if(row->available){
                if(e.action=="buy"||e.action=="take"||e.action=="remove")key=e.action=="remove"&&info_.remove_key?info_.remove_key:info_.get_key;
                else if(own_action(e.action))key=info_.put_key;
                else if(e.action.rfind("command:",0)==0){for(const auto &a:row->actions)if(a.id==e.action)key=a.key;}
                else {for(const auto &a:row->actions)if(a.id==e.action&&a.enabled)key=a.key;}
                if(key){last_action_=e.action;action_target_=row->id;pending_=e.action;selected_=e.action.rfind("command:",0)==0;source_sent_=false;manual_=false;}
            }
        }
        else if(current_.stage=="choose"&&e.action=="select"&&row!=current_.rows.end()&&row->tab==0){
            if(row->slot<info_.top||row->slot>=info_.top+info_.page_size)key=row->slot<info_.top?info_.previous_key:info_.next_key;
            else {key=row->key;selected_=key!=0;}
        }
        else if(current_.stage=="own_choose"&&e.action=="select"&&row!=current_.rows.end()&&row->tab==1){
            if(info_.selection_style==1&&row->selection>=0){
                int page=page_from_body();int desired=row->selection/std::max(1,info_.own_page_size);
                if(page!=desired)key=page<desired?'3':'9';
                else if(std::any_of(choices_.begin(),choices_.end(),[&](const auto &c){return c.key==row->key;})){key=row->key;selected_=true;}
            }else{
                std::vector<int> matches;
                for(const auto &c:choices_){std::string label=choice_text_from_body(c);if(!label.empty()&&labels_match(label,row->label))matches.push_back(c.key);}
                std::vector<int> cursor_rows;
                if(native_cursor_>=0)for(size_t i=0;i<lines_.size();++i){
                    const auto at=lines_[i].text_utf8.find(row->label);
                    if(at!=std::string::npos&&at>=static_cast<size_t>(native_cursor_col_))cursor_rows.push_back(static_cast<int>(i));
                }
                if(cursor_rows.size()==1){key=cursor_rows.front()==native_cursor_?13:(cursor_rows.front()>native_cursor_?'2':'8');selected_=key==13;}
                else if(matches.size()==1){key=matches.front();selected_=true;}
                else if(info_.selection_style==2&&!source_sent_){int location=row->slot/1000;key=location==2?5:(location==3?17:9);source_sent_=true;}
                else if(info_.selection_style==3&&!source_sent_){key='/';source_sent_=true;}
            }
            if(!key){manual_=true;result_="accepted";return true;}
        }
        else if(current_.stage=="message"&&e.action=="continue")key=current_.continue_key;
        else if(current_.stage=="text"&&e.action=="text_confirm")key=13;
        else if(current_.stage=="number"&&e.action=="number"&&e.number>=numeric_.min&&e.number<=numeric_.max){
            InputEventsMessage m;InputEventWire n;n.e="set_number";n.number=e.number;n.digits=1;m.events.push_back(n);append_input_event_keys(m,keys);key=13;
        }
        else if(current_.stage=="confirm"&&e.action=="answer"&&e.number>=0&&e.number<int(prompt_.choices.size()))key=prompt_.choices[e.number].key;
        if(key){keys.push_back(key);result_="accepted";consumed_=e.action!="page_for"&&e.action!="select"&&e.action!="native_key";}
        return true;
    }
    void remember_lines(const GameFrame &f){lines_=f.menu_term_lines;}
private:
    const char *tr(const char *ja,const char *en)const{return japanese_?ja:en;}
    static bool own_action(const std::string &a){return a=="sell"||a=="deposit"||a=="donate";}
    /* Frox（direct_commands_）の店・我が家の同義キー。原作の命令列 `[b/p/g] Buy  [s/d] Sell`（shop.c・home.c）は同じ命令を
     * 複数のキーで受けるが、アダプタが申告する選択肢は各札の先頭の 1 キー（b・s）だけなので、p・g・d が native_key として断られていた。
     * 申告された先頭のキーと同じ命令のキーなら受ける（他のコアの店は変えない）。 */
    bool synonym_of_declared_command(int key)const{
        if(!direct_commands_||key<=0)return false;
        if(key==info_.get_key||key==info_.put_key)return true;
        static const char *const groups[]={"bpg","sd","uU"};
        for(const char *g:groups){
            if(!std::strchr(g,key))continue;
            for(const char *m=g;*m;++m)if(std::any_of(choices_.begin(),choices_.end(),[&](const auto &c){return c.key==*m;}))return true;
        }
        return false;
    }
    static std::string choice_text(const GameFrame &f,const MenuChoice &c){
        if(c.line_index<0||c.line_index>=int(f.menu_term_lines.size()))return {};
        const auto &s=f.menu_term_lines[c.line_index].text_utf8;int start=std::clamp(c.span_begin,0,int(s.size()));int size=std::clamp(c.span_len,0,int(s.size())-start);return size?s.substr(start,size):s;
    }
    std::string choice_text_from_body(const MenuChoice &c)const{GameFrame f;f.menu_term_lines=lines_;return choice_text(f,c);}
    static bool labels_match(const std::string &a,const std::string &b){return !b.empty()&&a.find(b)!=std::string::npos;}
    static bool is_command_choice(const GameFrame &f,const MenuChoice &c){
        if(c.line_index<0||c.line_index>=int(f.menu_term_lines.size()))return false;
        const auto &s=f.menu_term_lines[c.line_index];
        return s.source_row>=int(f.menu_term_lines.size())-6||s.text_utf8.find(" to ")!=std::string::npos||s.text_utf8.find('[')!=std::string::npos;
    }
    static bool command_band(const GameFrame &f,const native_store_info &info){
        bool get=false,put=false,examine=false;
        for(const auto &c:f.menu_choices)if(is_command_choice(f,c)){get|=c.key==info.get_key;put|=c.key==info.put_key;examine|=c.key=='x';}
        return put&&(get||examine);
    }
    int page_from_body()const{
        int a=0,b=0;auto at=body_.find("(Page ");if(at!=std::string::npos&&std::sscanf(body_.c_str()+at,"(Page %d of %d)",&a,&b)==2)return std::max(0,a-1);
        for(size_t p=body_.find('(');p!=std::string::npos;p=body_.find('(',p+1))if(std::sscanf(body_.c_str()+p,"(%d / %d",&a,&b)==2)return std::max(0,a-1);
        return 0;
    }
    bool enabled_{false},japanese_{true},direct_commands_{false},show_command_hub_{true},selected_{false},consumed_{false},source_sent_{false},manual_{false},finishing_{false};
    int revision_{2000000},stage_revision_{0},last_request_{0},native_cursor_{-1},native_cursor_col_{0};
    std::string title_,heading_,signature_,stage_scope_,result_,pending_,body_,last_action_,action_target_;
    native_store_info info_{};ModernWindow current_;NumericInput numeric_;PromptBar prompt_;
    std::vector<MenuChoice> choices_;std::vector<TermMirrorLine> lines_;
    NativeModernMenu fallback_;
};
}
