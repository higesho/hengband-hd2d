#include "ui/modern_store_ui.h"
#include "app/ui_probe.h"
#include "frame/message_more.h"
#include "ui/menu_scale.h"
#include "ui/menu_frame.h"
#include "ui/modern_skin.h"
#include "ui/modern_map.h"
#include "ui/text_wrap.h"
#include "ui/ui_paint.h"
#include "render/text_overlay.h"
#include "render/term_colors.h"
#include "assets/tile_catalog.h"
#include "world/slab_library.h"
#include "i18n/lang.h"
#include <cstring>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
namespace hd2d {
namespace {
const char *tr(const char *ja,const char *en){return i18n::current()=="ja"?ja:en;}
void draw_edit_field(UiPaint &p,TextOverlay &t,const RectPx &box,const std::string &line,int column,TextColor fg,PaintColor accent) {
    p.frame(box,accent,2);
    const int caret=column<0?0:t.measure(line.substr(0,std::min<size_t>(column,line.size())));
    const int offset=std::max(0,caret-box.w+32);
    const auto skip=offset?t.fit_bytes(line,offset):0;
    const int skipped=t.measure(line.substr(0,skip));
    t.draw(box.x+10,box.y+10,line.substr(skip),fg,std::max(1,box.w-20));
    if(column>=0)p.rect({box.x+10+std::clamp(caret-skipped,0,std::max(0,box.w-22)),box.y+8,2,t.cell_h()+3},accent);
}
int value(const std::string &s){if(s.empty()||s.size()>9)return -1;long long n=0;for(char c:s){if(c<'0'||c>'9')return -1;n=n*10+c-'0';}return n>INT_MAX?-1:static_cast<int>(n);}
std::vector<std::string> wrap(TextOverlay &t,const std::string &s,int width) {
    return wrap_ui_text(s,width,[&](std::string_view part,int room){return t.fit_bytes(part,room);});
}
int loop_index(int current,int delta,int count) {
    if(count<=0)return 0;
    current=std::clamp(current,0,count-1);
    if(!delta)return current;
    const int next=current+delta;
    return next<0?count-1:(next>=count?0:next);
}
bool paired_allocation(const ModernWindow &window) {
    return window.content_type=="stat_allocation"||window.content_type=="minimum_stat_allocation"||window.content_type=="sil_allocation";
}
int paired_allocation_count(const ModernWindow &window) {
    if(window.content_type=="sil_allocation")return std::max(0,int(window.rows.size())-1);
    if(window.content_type=="stat_allocation")return std::min(12,int(window.rows.size()));
    return 0;
}
//! Sil-Q の能力値・技能の窓だけ（修-59）。Angband の点割り振り（行は `an-points:*`）も同じ `sil_allocation` の種類を使うので、
//! Sil-Q だけが置く「次に進む」の行（`allocation:continue`）があるかで見分ける。Angband の決定・左右は今までどおり。
bool sil_allocation_window(const ModernWindow &window) {
    return window.content_type=="sil_allocation"&&std::any_of(window.rows.begin(),window.rows.end(),[](const auto &r){return r.id=="allocation:continue";});
}
int paired_vertical_index(const ModernWindow &window,int cursor,int step,int &column) {
    const int total=int(window.rows.size()),paired=paired_allocation_count(window);
    if(total<=0||!step)return std::clamp(cursor,0,std::max(0,total-1));
    cursor=std::clamp(cursor,0,total-1);column=std::clamp(column,0,1);
    if(cursor<paired) {
        column=cursor%2;const int next=cursor+step*2;
        if(next>=paired)return paired<total?paired:column;
        if(next<0)return paired<total?total-1:std::max(0,paired-2)+column;
        return next;
    }
    if(step>0)return cursor+1<total?cursor+1:std::min(column,std::max(0,paired-1));
    return cursor>paired?cursor-1:std::max(0,paired-2)+std::min(column,1);
}
}
std::vector<const ModernRow*> ModernStoreUi::rows() const {
    std::vector<const ModernRow*> out;for(const auto &r:window_.rows)if(!item_window()||r.tab==tabs_.index())out.push_back(&r);return out;
}
const ModernRow *ModernStoreUi::selected() const {const auto r=rows();return cursor_>=0&&cursor_<static_cast<int>(r.size())?r[cursor_]:nullptr;}
/*!
 * @brief メッセージ履歴（Ctrl+P）の窓か。
 * @details コアが題を「メッセージ履歴」／「Message history」に固定し、本文の段だけの窓（stage=detail）にして送る
 * （`presentation/bridge/modern_content.h` の `modern_message_history`。変愚・短愚・幻想蛮怒・Sil-Q・Angband）。Frox は文書の枠で別の題。
 */
static bool message_history_window(const ModernWindow &w) {
    return w.kind=="dialog"&&w.stage=="detail"&&(w.title=="メッセージ履歴"||w.title=="Message history");
}
void ModernStoreUi::sync(const GameFrame &f,bool requested) {
    /*
     * 持ち物一覧から命令を送った後、原作が「？」で終わるメッセージだけで問う画（窓も帯も
     * 無い）が来ることがある。RogueClone2 は「落とす物は？」をメッセージに出した直後に
     * アイテムの選択肢を出す（選択肢は auto_select で隠れる）ので、その 1 枚でマップへ
     * 切り替わって見えていた。一方 Rogue 3.6 の投げる「方向は？」はそのまま答えを待つ。
     * 2 つはフレームの上では区別できないので、画面側で 0.3 秒だけ一覧を出し続け、その間に
     * 次の画が来なければマップ（とメッセージの問い）を見せる（2026-09-26）。
     */
    {
        const auto now=std::chrono::steady_clock::now();
        const bool bare=f.modern.stage.empty()&&!f.menu_open&&!f.targeting.active&&f.menu_term_lines.empty()
            &&!f.numeric.active&&!f.text_input_active&&f.prompt.choices.empty()&&f.prompt.text_utf8.empty()&&!f.awaiting_command;
        const bool after_command=now-inventory_command_at_<std::chrono::seconds(5);
        if(bare&&after_command&&enabled_&&window_.kind=="inventory"&&!window_.stage.empty()&&!held_) {
            held_=true;hold_until_=now+std::chrono::milliseconds(300);window_.input_ready=false;return;
        }
        if(bare&&held_&&now<hold_until_)return;
        held_=false;
    }
    /* Look owns the D-pad in the core.  Its auxiliary one-key actions are
     * deliberately rendered by the lightweight PromptBar, where LB/RB moves
     * only the auxiliary selection.  Showing the full Modern dialog here
     * would intercept Up/Down/Left/Right before UiCursors can let them pass
     * through to the native Look cursor. */
    // The monster recall (`r`) opened from the look prompt is a core screen laid over it.  While it is shown the core does
    // not send the look prompt (the adapter sees the game's screen_save depth), so it arrives as an ordinary dialog.
    const bool look_context_prompt=f.prompt.footer_utf8.rfind("LB/RB:",0)==0;
    const bool lightweight_prompt=f.prompt.lightweight&&f.modern.stage.empty();
    if(requested)enabled_=!look_context_prompt&&!lightweight_prompt;else if(!f.menu_open&&!f.numeric.active&&f.prompt.choices.empty())enabled_=false;
    const bool was_ready=window_.input_ready;
    scope_changed_=scope_changed_||window_.kind!=f.modern.kind||window_.stage!=f.modern.stage
        ||window_.title!=f.modern.title||window_.content_type!=f.modern.content_type;
    const std::string old=window_.stage,oldkind=window_.kind,old_numeric_prompt=numeric_.prompt_utf8,old_cursor_target=window_.cursor_target;
    /*
     * 持ち物一覧で命令を選ぶと、原作の子の選択肢（stage=choices）が開き、コアの
     * NativeInventoryMenu が対象の行を auto_select で示す。advance() はそれを直ちに
     * 決定するが、その間の窓を描くと「一覧の上からカーソルが対象まで動き直してから
     * 結果が出る」ように見えていた（変愚の「壊す」）。
     * 確定し終えるまで（auto_select が付いているか、決定を送った同じ revision の窓が
     * まだ来ている間）は、直前の一覧の見た目をそのまま描き続ける。
     */
    {
        const bool automatic=f.modern.stage=="choices"&&!f.modern.auto_select.empty();
        const bool same=auto_backdrop_&&f.modern.stage=="choices"&&f.modern.revision==auto_revision_;
        if(automatic||same) {
            if(!auto_backdrop_&&window_.kind=="inventory"&&!window_.stage.empty()) {
                auto_backdrop_=std::make_shared<ModernStoreUi>(*this);auto_backdrop_->auto_backdrop_.reset();
            }
            if(automatic)auto_revision_=f.modern.revision;
        } else auto_backdrop_.reset();
    }
    if(oldkind=="dialog"&&old=="choices"&&cursor_>=0&&cursor_<int(window_.rows.size())) {
        const auto &row=window_.rows[cursor_];
        choice_focus_[oldkind+"|"+window_.title+"|"+window_.venue_header]={row.key,row.label};
    }
    const bool result_context=f.modern.kind=="store"||f.modern.kind=="home"||f.modern.kind=="museum"||f.modern.kind=="building"||f.modern.kind=="inventory";
    const bool fully_left=f.modern.stage.empty()&&!f.menu_open&&old.empty();
    std::vector<std::string> frame_messages;frame_messages.reserve(f.messages.size());
    for(const auto &message:f.messages)frame_messages.push_back(message.text_utf8);
    if(fully_left){message_context_.clear();recent_messages_.clear();message_signature_.clear();previous_frame_messages_.clear();}
    else if(result_context&&(message_context_.empty()||message_context_!=f.modern.kind)) {
        message_context_=f.modern.kind;recent_messages_.clear();
        message_sequence_=f.messages.empty()?0:f.messages.back().seq;
        previous_frame_messages_=frame_messages;
    }
    if(!message_context_.empty()&&frame_messages!=previous_frame_messages_) {
        const size_t overlap=message_suffix_prefix_overlap(previous_frame_messages_,frame_messages);
        std::vector<std::string> additions;
        if(overlap>0||previous_frame_messages_.empty())additions.assign(frame_messages.begin()+static_cast<std::ptrdiff_t>(overlap),frame_messages.end());
        else for(const auto &message:f.messages)if(message.seq>message_sequence_)additions.push_back(message.text_utf8);
        if(additions.empty()&&!frame_messages.empty())additions.push_back(frame_messages.back());
        // Store and facility footers show the result of the most recent native
        // operation.  Keeping older messages here made users acknowledge stale
        // text before the active quantity/confirmation prompt became visible.
        if(!additions.empty())recent_messages_.assign(1,std::move(additions.back()));
        previous_frame_messages_=frame_messages;
        if(!f.messages.empty())message_sequence_=std::max(message_sequence_,static_cast<std::uint64_t>(f.messages.back().seq));
        if(!additions.empty())message_scroll_=0;
    }
    if(item_window())tabs_.remember(cursor_,scroll_);
    bool question_changed=window_.input!=f.modern.input||prompt_.choices.size()!=f.prompt.choices.size();
    for(size_t i=0;!question_changed&&i<prompt_.choices.size();++i)question_changed=prompt_.choices[i].key!=f.prompt.choices[i].key||prompt_.choices[i].label_utf8!=f.prompt.choices[i].label_utf8;
    if(oldkind=="dialog"&&old=="choices"&&!dialog_signature_.empty()) {
        if(choice_positions_.size()>128)choice_positions_.clear();
        choice_positions_[dialog_signature_]={cursor_,scroll_};
    }
    if(window_.stage!=f.modern.stage||window_.kind!=f.modern.kind||window_.title!=f.modern.title||f.modern.actions.empty())commands_open_=false;
    const bool keep_choices=oldkind=="dialog"&&f.modern.kind=="dialog"&&(old=="choices"||retained_choices_)
        &&window_.content_type!="start"&&!window_.rows.empty()
        &&(f.modern.stage=="number"||f.modern.stage=="text"||f.modern.stage=="confirm"||f.modern.message_only);
    const auto backdrop=keep_choices?window_:ModernWindow{};
    const int backdrop_cursor=cursor_,backdrop_scroll=scroll_;
    window_=f.modern;numeric_=f.numeric;prompt_=f.prompt;retained_choices_=keep_choices;local_placeholder_=false;
    menu_over_map_=f.menu_over_map;menu_popup_=f.menu_over_map&&f.menu_popup;
    context_heading_=(f.menu_over_map&&!f.menu_term_lines.empty())?f.menu_term_lines.front().text_utf8:std::string();
    if(keep_choices){window_.rows=backdrop.rows;window_.content=backdrop.content;window_.title=backdrop.title;window_.content_type=backdrop.content_type;}
    // 文字入力の窓は、背景の選択肢の窓（職業の選択など）の題ではなく、コアが送る入力欄の題（「名前の入力」など）を出す（判-12）。
    // 数の入力・確認の窓は、これまでどおり背景の題のまま。
    if(keep_choices&&f.modern.stage=="text"&&!f.modern.title.empty())window_.title=f.modern.title;
    navigation_page_=window_.kind=="dialog"&&window_.content_type!="map"&&window_.content_type!="editor"&&window_.content_type!="character"&&!window_.capture_keys&&!window_.message_only
        &&window_.stage!="choices"&&window_.stage!="cursor"&&!keep_choices;
    if(navigation_page_) {
        window_.rows.clear();
        for(const auto &a:window_.actions)if(a.enabled){ModernRow row;row.id="@action:"+a.id;row.label=a.label;row.available=a.enabled;row.price_known=false;window_.rows.push_back(std::move(row));}
        for(size_t i=0;i<window_.content.size();++i) {
            const auto &block=window_.content[i];std::string heading=block.title;
            if(heading.empty()&&block.kind=="heading"&&!block.cells.empty())heading=block.cells.front().text;
            if(!heading.empty()){ModernRow row;row.id="@section:"+std::to_string(i);row.label=heading;row.price_known=false;window_.rows.push_back(std::move(row));}
        }
        // 見出しが 1 つだけの詳細の頁（Angband の死亡後の「何かキーを押すと詳しい情報を見る:」など）では、その見出しは
        // 移動先にならない（頁に 1 つしかない）。見出しの行にすると決定が見出しへ移るだけで、継続のキーが送られず先へ進めない。
        // 操作の行がなく継続のキーがある頁は、見出しの行を捨てて「次へ」の行にする。見出しが 2 つ以上ある頁は今までどおり。
        if(window_.stage=="detail"&&window_.continue_key>0&&window_.actions.empty()&&window_.rows.size()==1&&window_.rows.front().id.starts_with("@section:"))window_.rows.clear();
        if(window_.rows.empty()){
            ModernRow row;
            if(window_.stage=="detail"&&window_.continue_key>0&&window_.actions.empty()) {
                row.id="@continue";row.label=tr("次へ","Continue");row.key=row.hotkey=window_.continue_key;
            } else {row.id="@section:0";row.label=window_.title;}
            row.price_known=false;window_.rows.push_back(std::move(row));
        }
    }
    if(f.pre_game_menu&&window_.kind=="dialog"&&window_.stage=="cursor"&&window_.content_type=="selection"
        &&window_.rows.empty()&&window_.continue_key>0
        &&std::any_of(window_.actions.begin(),window_.actions.end(),[](const auto &action){return action.key=='S';})
        &&std::any_of(window_.actions.begin(),window_.actions.end(),[](const auto &action){return action.key=='Q';})) {
        ModernRow row;row.id="@continue";row.label=tr("決定して次へ","Accept and continue");
        row.key=row.hotkey=window_.continue_key;row.price_known=false;window_.rows.push_back(std::move(row));
    }
    /*
     * 案 A。原作がスクロールで出す一覧（知識のブラウザ・種族の特殊能力の
     * メニューなど）は、原作が Term に描いた行だけが窓の行になる。一覧の端でさらに上下を押しても、
     * 画面の中で回り込むと原作が後から描く項目に届かない。そこで端では**原作へ上下を送り**、続きがあれば
     * 原作にスクロールさせる。送っても行とカーソルが変わらなければ、そこが原作の一覧の本当の端
     * （`confirmed_*`）。本当の端でだけ操作の行（`@action:`）を出し、先頭・末尾では回り込む。
     * 原作が端で回り込む一覧は、送った上下で一覧が変わるので、回り込みも原作に任せる。
     * 設定の一覧（`settings_list`）は原作の表から全件を作ってあるので、この仕組みを通さない。
     */
    {
        const auto &type=window_.content_type;
        scroll_list_=window_.kind=="dialog"&&!navigation_page_&&!window_.capture_keys&&!f.pre_game_menu&&!keep_choices&&!window_.rows.empty()
            &&(type=="browser"||type=="menu"||type=="items"||type=="spells"||type=="selection")
            &&(window_.stage=="cursor"||(window_.stage=="choices"&&window_.cursor>=0))&&!paired_allocation(window_);
        if(scroll_list_) {
            native_rows_=int(window_.rows.size());primary_selection_=-1;
            native_sig_=window_.title+"|"+window_.stage+"\n";
            for(const auto &row:window_.rows){native_sig_+=row.id;native_sig_+='\n';primary_selection_=std::max(primary_selection_,row.selection);}
            if(native_sig_!=confirmed_sig_){confirmed_sig_=native_sig_;confirmed_top_.clear();confirmed_bottom_.clear();}
            const auto confirm=[&](std::vector<int> &set,int selection){if(std::find(set.begin(),set.end(),selection)==set.end())set.push_back(selection);};

            if(probe_.serial&&window_.request_id==probe_.serial) {
                //! 送った上下のあとのフレーム。行もカーソルも変わらなければ、そこが本当の端。
                if(window_.result!="rejected"&&native_sig_==probe_.sig&&cursor_token()==probe_.cursor) {
                    confirm(probe_.step>0?confirmed_bottom_:confirmed_top_,probe_.selection);
                    deferred_step_=probe_.step;deferred_sig_=native_sig_;
                }
                probe_={};
            } else if(probe_.serial&&window_.request_id>probe_.serial)probe_={};
            if(wrap_.active&&wrap_.serial&&window_.request_id==wrap_.serial) {
                wrap_.serial=0;
                if(window_.result=="rejected")wrap_={};
                else if(native_sig_==wrap_.sig&&cursor_token()==wrap_.cursor) {
                    if(wrap_.phase<2)++wrap_.phase;
                    else {
                        confirm(wrap_.toward>0?confirmed_bottom_:confirmed_top_,native_cursor_selection());
                        place_cursor_=wrap_.toward>0?-2:-1;wrap_={};
                    }
                }
            }
        } else {native_rows_=0;primary_selection_=0;native_sig_.clear();confirmed_sig_.clear();confirmed_top_.clear();confirmed_bottom_.clear();probe_={};wrap_={};deferred_step_=0;place_cursor_=-1;}
    }
    const bool settings_list=window_.content_type=="settings_list"||window_.content_type=="settings_list_rogue";
    //! 知識のブラウザの操作の行は、原作の一覧の本当の末尾が見えたときだけ出す（スクロールで続きがある間は出さない）。
    const bool actions_hidden=scroll_list_&&window_.content_type=="browser"
        &&std::find(confirmed_bottom_.begin(),confirmed_bottom_.end(),primary_selection_)==confirmed_bottom_.end();
    if(window_.kind=="dialog"&&window_.stage=="cursor"&&!window_.capture_keys&&!settings_list&&!paired_allocation(window_)&&!actions_hidden)for(const auto &action:window_.actions) {
        const auto id="@action:"+action.id;
        if(std::any_of(window_.rows.begin(),window_.rows.end(),[&](const auto &row){return row.id==id;}))continue;
        ModernRow row;row.id=id;row.label=action.label;row.price_known=false;row.available=action.enabled;row.reason=action.reason;window_.rows.push_back(std::move(row));
    }
    birth_layout_=f.pre_game_menu&&window_.content_type!="start";
    const int initial_answer=window_.default_answer<0?0:std::clamp(window_.default_answer,0,std::max(0,int(prompt_.choices.size())-1));
    command_cursor_=std::clamp(command_cursor_,0,std::max(0,int(window_.actions.size())-1));
    // 持ち物一覧の命令を原作が実行している間の窓（NativeInventoryMenu の busy）は、同じ一覧を
    // 見せ続けるだけなので「切り替えを待っています」を出さない（一覧の下の行が明滅する）。
    if(!window_.input_ready&&window_.kind=="inventory"){buffered_.events.clear();}
    else if(!window_.input_ready){buffered_.events.clear();notice_=tr("画面の切り替えを待っています…","Waiting for the screen update...");}
    else if(!was_ready)notice_.clear();
    if(window_.stage.empty()||oldkind!=window_.kind||(old=="text"&&window_.stage!="text"))buffered_.events.clear();
    text_line_=window_.stage=="text"?window_.input:(window_.input.empty()?f.prompt.text_utf8:window_.input);
    if(item_window()) {
        std::vector<MenuTab> defs{{"stock",window_.kind=="home"?tr("家のアイテム","Home items"):(window_.kind=="museum"?tr("展示品","Exhibits"):tr("店の商品","Store items")),{}},{"own",tr("持ち物・装備","Inventory / equipment"),{}}};
        if(window_.kind=="inventory"){defs.resize(1);defs[0].label=tr("持ち物一覧","Inventory");}
        for(const auto &r:window_.rows)defs[std::clamp(r.tab,0,1)].items.push_back(r.id);
        tabs_.reset(std::move(defs));
        if(old.empty()&&window_.stage=="browse"){tabs_.clear_positions();tabs_.select(0);}
        if(window_.stage=="own_choose")tabs_.select(1);else if(window_.stage=="choose")tabs_.select(0);
        cursor_=tabs_.cursor();scroll_=tabs_.scroll();
    } else {cursor_=std::clamp(cursor_,0,std::max(0,static_cast<int>(window_.rows.size())-1));if(oldkind!=window_.kind||old.empty()){cursor_=0;scroll_=0;}}
    if(window_.kind=="dialog") {
        std::string signature=window_.stage+"|"+window_.title;
        for(const auto &r:window_.rows)signature+="|"+r.id+(window_.stage=="cursor"?"":":"+r.label);
        if(window_.rows.empty()) {
            const auto first=window_.body.find_first_not_of(" \r\n\t");
            if(first!=std::string::npos)signature+="|"+window_.body.substr(first,window_.body.find('\n',first)-first);
            if(window_.stage=="confirm"||window_.stage=="text")signature+="|"+window_.input;
        }
        if(signature!=dialog_signature_){
            requested_preview_target_.clear();preview_request_=0;
            const bool keep_focus=oldkind==window_.kind&&old==window_.stage&&!focus_target_.empty()
                &&std::any_of(window_.rows.begin(),window_.rows.end(),[&](const auto &row){return row.id==focus_target_;});
            cursor_=0;scroll_=0;document_scroll_=0;document_x_=0;details_focus_=false;answer_=initial_answer;
            if(!keep_focus)focus_target_.clear();
            if(window_.stage=="choices")if(const auto found=choice_positions_.find(signature);found!=choice_positions_.end()) {
                cursor_=std::clamp(found->second.first,0,std::max(0,int(window_.rows.size())-1));scroll_=found->second.second;
            } else if(const auto remembered=choice_focus_.find(window_.kind+"|"+window_.title+"|"+window_.venue_header);remembered!=choice_focus_.end()) {
                const auto at=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &row){
                    return row.key==remembered->second.first&&row.label==remembered->second.second;
                });
                if(at!=window_.rows.end())cursor_=int(at-window_.rows.begin());
            }
            //! スクロールで行が入れ替わっても、カーソルを先頭へ戻さず原作のカーソルの行に合わせる（案 A）。
            if(scroll_list_&&window_.stage=="cursor"&&focus_target_.empty()) {
                const auto at=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &row){return row.id==window_.cursor_target;});
                if(at!=window_.rows.end())cursor_=int(at-window_.rows.begin());
            }
            //! 原作にカーソルが無い選択肢の窓（Frox のキャラ作成の本体）は、コアが指す行（「次の画面」）に最初のカーソルを置く。
            //! 素の Enter が原作どおり次の画面へ進む（修-29）。ほかの窓は `initial_target` が空なので今までどおり。
            if(window_.stage=="choices"&&window_.cursor<0&&!window_.initial_target.empty()) {
                const auto at=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &row){return row.id==window_.initial_target;});
                if(at!=window_.rows.end())cursor_=int(at-window_.rows.begin());
            }
            press_after_focus_.clear();
        }
        if(place_cursor_==-2&&scroll_list_) {
            //! 末尾の操作の行（無ければ名前の列の最後の行）。
            cursor_=std::max(0,int(window_.rows.size())-1);
            if(int(window_.rows.size())==native_rows_&&window_.content_type=="browser")
                for(int i=int(window_.rows.size())-1;i>=0;--i)if(window_.rows[i].selection==primary_selection_){cursor_=i;break;}
        }
        place_cursor_=-1;
        dialog_signature_=std::move(signature);
        if(window_.stage=="choices"&&window_.cursor>=0) {
            const bool native_reached=focus_target_.empty()||(window_.cursor<int(window_.rows.size())&&window_.rows[window_.cursor].id==focus_target_);
            if(native_reached){if(cursor_!=window_.cursor)document_scroll_=0;cursor_=window_.cursor;}
        }
        if(window_.stage=="cursor"&&(old_cursor_target!=window_.cursor_target||old!=window_.stage)) {
            const bool requested_reached=focus_target_.empty()||window_.cursor_target==focus_target_;
            if(requested_reached)if(const auto at=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &row){return row.id==window_.cursor_target;});at!=window_.rows.end()) {
                cursor_=int(at-window_.rows.begin());
                if(paired_allocation(window_)&&selection_column_==1&&cursor_+1<paired_allocation_count(window_))++cursor_;
            }
        }
        if(window_.stage!="choices"&&window_.stage!="cursor"){focus_target_.clear();press_after_focus_.clear();confirm_after_focus_=false;}
        if(oldkind!="dialog"){document_scroll_=0;document_x_=0;}
    }
    // メッセージ履歴（Ctrl+P）の窓は、原作と同じく古い順で、最新の行が本文の最後にある。窓は本文を先頭から描くので、本文が窓より長いと
    // 最新の行が下に切れて見えなかった（修-18）。新しく出たとき・頁を送って中身（見出しの範囲・本文）が変わったときは、次に描くときに末尾まで送る。
    // 同じ内容の再送（revision だけが進む）では、遊ぶ人が送った位置を動かさない。コアの窓の形式は変えない（題はコアが固定して送る）。
    if(message_history_window(window_)) {
        std::string signature=window_.subtitle+"|"+std::to_string(window_.content.size());
        if(!window_.content.empty()&&!window_.content.back().cells.empty())signature+="|"+window_.content.front().cells.front().text+"|"+window_.content.back().cells.back().text;
        if(signature!=history_signature_){history_signature_=std::move(signature);scroll_to_end_=true;}
    } else {history_signature_.clear();scroll_to_end_=false;}
    if(raw_pending_ && window_.revision!=sent_revision_) {raw_pending_=false;pending_=false;}
    if(preview_request_&&window_.request_id==preview_request_&&window_.result=="rejected"){requested_preview_target_.clear();preview_request_=0;}
    if(pending_&&!raw_pending_&&window_.request_id==serial_) {
        if(window_.result=="rejected") {pending_=false;workflow_.clear();focus_target_.clear();notice_=sent_action_=="native_key"?tr("この画面では使えないキーです。","That key is not available here."):tr("状態が変わりました。対象を選び直してください。","The state changed. Select the target again.");}
        else if(window_.stage!=sent_stage_||oldkind!=window_.kind||sent_action_=="service"
            ||sent_action_=="cancel"||(sent_action_=="answer"&&question_changed)||sent_action_=="text_confirm"||sent_action_=="row_action"||sent_action_=="window_action"||sent_action_=="focus"||sent_action_=="navigate"||sent_action_=="choice"||sent_action_=="continue"||sent_action_=="document_page"||sent_action_=="native_key"
            ||((sent_action_.rfind("command:",0)==0||sent_action_.rfind("inventory:",0)==0||sent_action_=="inventory_open")&&window_.result=="accepted")
            ||(sent_action_=="number"&&old_numeric_prompt!=numeric_.prompt_utf8)
            ||((sent_action_=="page_for"||sent_action_=="select")&&window_.revision!=sent_revision_)) {
            pending_=false;if(window_.stage=="browse"||window_.stage=="services")notice_.clear();
        }
    }
    if(old!=window_.stage || oldkind!=window_.kind || old_numeric_prompt!=numeric_.prompt_utf8) {operations_=false;details_focus_=false;answer_=initial_answer;if(window_.stage=="number"){number_=std::to_string(std::clamp(numeric_.value,numeric_.min,std::max(numeric_.min,numeric_.max)));replace_number_=true;}}
    //! 文字入力の欄が新しく出たら「まだ何も編集していない」に戻す（修-58）。窓の題と欄の見出しが変わったときも新しい欄として扱う。
    if(window_.stage!="text")text_field_scope_.clear();
    else if(const std::string scope=window_.title+"|"+window_.input_label+"|"+window_.kind;scope!=text_field_scope_){text_field_scope_=scope;text_fresh_=true;}
    if(question_changed&&window_.stage=="confirm")answer_=initial_answer;
    if(!window_.stage.empty()&&window_.content_type=="settings_list_rogue"&&!parent_categories_.empty())parent_tabs_seen_=true;
    if(!window_.stage.empty()&&window_.content_type!="settings_list_rogue"&&!parent_tabs_seen_&&parent_tab_request_<0) {
        parent_categories_.clear();parent_category_=-1;parent_header_first_=0;
    }
    if(window_.stage.empty()) {
        cancel_after_navigation_=false;pending_=false;operations_=false;workflow_.clear();raw_pending_=false;
        if(parent_tabs_seen_&&parent_tab_request_<0) {
            parent_categories_.clear();parent_category_=-1;parent_header_first_=0;parent_tabs_seen_=false;
        }
    }
    if(keep_choices){cursor_=backdrop_cursor;scroll_=backdrop_scroll;}
    if(old!=window_.stage)message_scroll_=0;
    if(operations_ && (!selected()||selected()->id!=target_))operations_=false;
    //! 預かったタブ移動。受けられるようになった所で 1 回だけ回す。1.5 秒より古いものは捨てる。
    if(deferred_tab_!=0) {
        if(std::chrono::steady_clock::now()-deferred_tab_at_>std::chrono::milliseconds(1500))deferred_tab_=0;
        else if(active()&&!pending_&&!operations_&&window_.input_ready
            &&((parent_tab_navigation_active()&&window_.stage=="cursor")||(item_window()&&window_.stage=="browse"))) {
            const int d=deferred_tab_;deferred_tab_=0;tab(d);
        }
    }
}
void ModernStoreUi::send(const std::string &action,presentation::InputEventsMessage &out,int n) {
    if(pending_||!window_.input_ready)return;
    if(window_.version<5&&(action=="window_action"||action=="row_action"||(document_mode()&&(action=="continue"||action=="document_page")))) {
        int key=action=="continue"?window_.continue_key:(action=="document_page"?(n?window_.next_key:window_.prev_key):n);if(action=="window_action")for(const auto &a:window_.actions)if(a.id==target_&&a.enabled)key=a.key;
        if(key<=0||key>127)return;
        if(&out!=&buffered_){out.events.insert(out.events.end(),buffered_.events.begin(),buffered_.events.end());buffered_.events.clear();}
        presentation::InputEventWire event;
        if(key==27)event.e="cancel";else if(key==13)event.e="confirm";
        else {event.e="key";if(key<27){event.ctrl=true;event.chr=std::string(1,char('a'+key-1));}else event.chr=std::string(1,char(key));}
        out.events.push_back(std::move(event));return;
    }
    if(action=="window_action"&&window_.stage=="text" && &out!=&buffered_){out.events.insert(out.events.end(),buffered_.events.begin(),buffered_.events.end());buffered_.events.clear();}
    presentation::InputEventWire e;e.e="ui_action";e.ui_revision=window_.revision;e.request_id=++serial_;e.action=action;e.target=target_;e.number=n;out.events.push_back(e);
    pending_=true;sent_stage_=window_.stage;sent_action_=action;sent_revision_=window_.revision;sent_at_=std::chrono::steady_clock::now();notice_.clear();
    if(action.rfind("inventory:",0)==0)inventory_command_at_=sent_at_;
}
//! Sil-Q の能力値・技能の窓（`sil_allocation`）で、`row`（偶数が「−」・奇数が「＋」）の値を変える。
//! 「−」と「＋」は原作では 1 つのカーソル位置（項目の行）を共有するので、原作のカーソルが項目へ着いてから 4/6 を送る（`advance`）。
void ModernStoreUi::press_allocation(int row,presentation::InputEventsMessage &out) {
    const int paired=paired_allocation_count(window_);
    if(row<0||row>=paired||row>=int(window_.rows.size()))return;
    const int first=row-row%2;
    cursor_=row;selection_column_=row%2;
    focus_target_=window_.rows[first].id;focus_steps_=0;press_after_focus_=window_.rows[row].id;
    advance(out);
}
void ModernStoreUi::advance(presentation::InputEventsMessage &out) {
    if(&out!=&buffered_){out.events.insert(out.events.end(),buffered_.events.begin(),buffered_.events.end());buffered_.events.clear();}
    if(inventory_open_queued_&&!pending_){inventory_open_queued_=false;open_inventory(out,inventory_categories_,inventory_category_);return;}
    if(!active()||!window_.input_ready)return;
    //! 預かった決定は、同じ窓・同じ目標の行のまま 3 秒以内に歩き終えたときだけ送る（古い操作が別の窓に効かないように）。
    if(confirm_after_focus_&&(window_.stage!="cursor"||window_scope()!=confirm_scope_
        ||std::chrono::steady_clock::now()-confirm_after_focus_at_>std::chrono::seconds(3)))confirm_after_focus_=false;
    if(pending_) {if(std::chrono::steady_clock::now()-sent_at_>std::chrono::seconds(10))notice_=tr("応答待ちです。操作を自動再送しません。","Waiting for the core. No automatic retry.");return;}
    if(cancel_after_navigation_){cancel_after_navigation_=false;cancel(out);return;}
    if(commands_open_)return;
    //! 回り込み：本当の端へ着くまで、原作のページ送り（なければ 1 行ずつ）を送り続ける。
    if(wrap_.active) {
        if(!scroll_list_){wrap_={};return;}
        if(++wrap_.count>600){wrap_={};notice_=tr("一覧の端まで進めませんでした。","Could not reach the end of the list.");return;}
        const int page_key=wrap_.toward<0?window_.prev_key:window_.next_key;
        const int column=native_cursor_selection();
        //! 分類の列などにカーソルがあるときは、まず主の列（名前の列）へ移す（ページ送りは列ごとに効く）。
        if(wrap_.phase==0&&(column==primary_selection_||wrap_.count>4))wrap_.phase=1;
        if(wrap_.phase==1&&!page_key)wrap_.phase=2;
        wrap_.sig=native_sig_;wrap_.cursor=cursor_token();
        const int before=serial_;
        if(wrap_.phase==0)send("navigate",out,primary_selection_>column?6:4);
        else if(wrap_.phase==1)send("document_page",out,wrap_.toward<0?0:1);
        else send("navigate",out,wrap_.toward<0?8:2);
        if(serial_==before)wrap_={};else wrap_.serial=serial_;
        return;
    }
    //! 端を確かめたあと、押されていた上下をもう一度処理する（操作の行へ進む・回り込む・別の列の行へ進む）。
    if(deferred_step_) {
        const int step=deferred_step_;deferred_step_=0;
        if(scroll_list_&&deferred_sig_==native_sig_)handle(step>0?CursorNav::Down:CursorNav::Up,out);
        return;
    }
    if(window_.stage=="choices"&&!window_.auto_select.empty()) {
        const auto row=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &r){return r.id==window_.auto_select;});
        target_=window_.auto_select;
        if(row!=window_.rows.end()) {
            /*
             * 行のキーが英字なら、原作の選択肢はその字でそのアイテムを直接選ぶ
             * （コアの choice はカーソルの位置に関わらず row->key を送る）。途中の
             * 行を 1 つずつ動かす必要は無い。キーが無い・Enter などカーソルの位置で
             * 決まる行だけ、従来どおりカーソルを合わせてから決定する。
             */
            const bool direct=(row->key>='a'&&row->key<='z')||(row->key>='A'&&row->key<='Z');
            if(!direct&&window_.cursor>=0&&window_.cursor!=int(row-window_.rows.begin()))send("navigate",out,int(row-window_.rows.begin())>window_.cursor?2:8);
            else send("choice",out);
        } else if(std::any_of(window_.actions.begin(),window_.actions.end(),[&](const auto &a){return a.id==target_&&a.enabled;}))send("window_action",out);
        return;
    }
    if(window_.stage=="confirm"&&window_.auto_select.starts_with("@answer:")) {
        const int choice=value(window_.auto_select.substr(8));
        if(choice>=0&&choice<int(prompt_.choices.size())){answer_=choice;send("answer",out,choice);}
        return;
    }
    if(window_.stage=="cursor"&&!focus_target_.empty()) {
        if(window_.cursor_target==focus_target_) {
            focus_target_.clear();focus_steps_=0;
            //! Sil-Q の能力値・技能の窓：原作のカーソルが項目へ着いたので、押された ± のキーを送る。
            if(!press_after_focus_.empty()){target_=press_after_focus_;press_after_focus_.clear();send("choice",out);return;}
        }
        else {
            const auto wanted=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &row){return row.id==focus_target_;});
            if(wanted==window_.rows.end()||++focus_steps_>512){focus_target_.clear();press_after_focus_.clear();confirm_after_focus_=false;notice_=tr("項目が変わりました。選び直してください。","The item changed. Select it again.");return;}
            target_=focus_target_;send("focus",out);return;
        }
    }
    //! 歩いている間に押された決定（調-08）。原作のカーソルが預かったときの目標の行に着いているときだけ送る。
    if(confirm_after_focus_&&focus_target_.empty()) {
        confirm_after_focus_=false;
        if(window_.stage=="cursor"&&window_.cursor_target==confirm_target_&&window_scope()==confirm_scope_)handle(CursorNav::Confirm,out);
        return;
    }
    if(window_.stage=="finish"){workflow_.clear();send("finish",out);return;}
    if(window_.stage=="choices"&&window_.cursor>=0&&!focus_target_.empty()) {
        const auto row=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &r){return r.id==focus_target_;});
        if(row==window_.rows.end()||int(row-window_.rows.begin())==window_.cursor)focus_target_.clear();
        else {send("navigate",out,int(row-window_.rows.begin())>window_.cursor?2:8);return;}
    }
    if(window_.can_preview&&window_.stage=="choices") {
        if(const auto row=selected();row&&row->id!=requested_preview_target_) {
            presentation::InputEventWire event;event.e="ui_action";event.action="preview";event.target=row->id;
            event.ui_revision=window_.revision;event.request_id=++serial_;preview_request_=event.request_id;requested_preview_target_=row->id;
            out.events.push_back(std::move(event));
        }
    }
    if(workflow_.empty())return;
    if(window_.kind=="dialog")return;
    if(window_.stage=="browse"&&workflow_started_){workflow_.clear();return;}
    target_=workflow_target_;
    const auto item=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &r){return r.id==target_;});
    if(item==window_.rows.end() || workflow_steps_>512) {workflow_.clear();notice_=tr("対象を確認できません。取消して選び直してください。","Cannot confirm the target. Cancel and select again.");return;}
    if(window_.stage=="browse") {
        if(workflow_started_) {workflow_.clear();return;}
        ++workflow_steps_;
        if(item->tab==0&&(item->slot<window_.page || item->slot>=window_.page+window_.page_size)) {send("page_for",out);return;}
        workflow_started_=true;send(workflow_,out);return;
    }
    if(window_.stage=="choose"||window_.stage=="own_choose") {++workflow_steps_;send("select",out);return;}
    workflow_.clear();
}
std::string ModernStoreUi::cursor_token() const {
    return window_.stage=="choices"?std::to_string(window_.cursor):window_.cursor_target;
}
int ModernStoreUi::native_cursor_selection() const {
    const int at=window_.stage=="choices"?window_.cursor
        :int(std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &r){return r.id==window_.cursor_target;})-window_.rows.begin());
    return at>=0&&at<int(window_.rows.size())?window_.rows[at].selection:0;
}
/*
 * 知識のブラウザの上下。窓の左の一覧は分類と名前が行の順に交互に並ぶが、原作の上下は**いまカーソルがある列の中**で動く
 * （左右で列を替える）。一覧の並びのまま 1 つ進めると、名前の列を歩いている途中で隣の分類へ飛んで分類を替えてしまい、
 * 1 つの分類の名前を下まで見られなかった。同じ列（`selection`）の次の行へ進み、無ければ動かない
 * （端の処理は `scroll_edge()` が先に済ませている）。操作の行（`@action:`）の上からは、名前の列の最後の行へ戻る。
 */
int ModernStoreUi::browser_column_step(int step) const {
    const int total=int(window_.rows.size()),n=std::min(native_rows_,total);
    if(cursor_<0||cursor_>=total)return 0;
    if(cursor_>=n) {
        if(step>0)return std::min(cursor_+1,total-1);
        if(cursor_>n)return cursor_-1;
        for(int i=n-1;i>=0;--i)if(window_.rows[i].selection==primary_selection_)return i;
        return n-1;
    }
    const int selection=window_.rows[cursor_].selection;
    for(int i=cursor_+step;i>=0&&i<n;i+=step)if(window_.rows[i].selection==selection)return i;
    //! 分類の列の端では、行の順の次の行（名前の列）へ進む。名前の列の端は `scroll_edge()` が先に処理している。
    if(selection!=primary_selection_&&cursor_+step>=0&&cursor_+step<n)return cursor_+step;
    return cursor_;
}
bool ModernStoreUi::native_cursor_at(int index) const {
    if(index<0||index>=int(window_.rows.size()))return false;
    return window_.stage=="choices"?window_.cursor==index:(!window_.cursor_target.empty()&&window_.cursor_target==window_.rows[index].id);
}
void ModernStoreUi::begin_probe(int step,int selection,presentation::InputEventsMessage &out) {
    probe_={};probe_.step=step;probe_.selection=selection;probe_.sig=native_sig_;probe_.cursor=cursor_token();
    const int before=serial_;
    send("navigate",out,step>0?2:8);
    if(serial_!=before)probe_.serial=serial_;else probe_={};
}
void ModernStoreUi::begin_wrap(int toward) {
    wrap_={};wrap_.active=true;wrap_.toward=toward;
}
/*
 * 一覧の端での上下（案 A）。次の順で決める。
 *  1. 原作の行ではない行（操作の行）の上: 末尾の行の下だけ、先頭へ回り込む。それ以外は従来どおり。
 *  2. 原作のカーソルがある行で、同じ列の続きの行が窓に無い: まだ確かめていなければ**原作へ上下を 1 回送る**
 *     （`begin_probe`）。送ったあとのフレームで行もカーソルも変わらなければ、そこが原作の一覧の本当の端
 *     （`sync()` が `confirmed_*` へ記録し、`advance()` が同じ上下をもう 1 回処理する）。変わっていれば原作が
 *     スクロール（またはページ送り・回り込み）したので、何もしない。
 *  3. 本当の端を確かめ済み: 別の列の行・操作の行があれば従来どおりそこへ。無ければ反対の端へ回り込む
 *     （原作のページ送り・上下を、変わらなくなるまで送る。`advance()` の wrap_）。
 * 知識のブラウザの「列」は行の `selection`（0＝分類・1＝名前）。続きの有無は同じ列の中で見る。
 */
bool ModernStoreUi::scroll_edge(int step,presentation::InputEventsMessage &out) {
    if(!scroll_list_||!step||!focus_target_.empty())return false;
    const int total=int(window_.rows.size()),n=std::min(native_rows_,total);
    if(n<=0||cursor_<0||cursor_>=total)return false;
    if(cursor_>=n) {
        if(step>0&&cursor_==total-1){begin_wrap(-1);advance(out);return true;}
        return false;
    }
    if(!native_cursor_at(cursor_))return false;
    const int selection=window_.rows[cursor_].selection;
    for(int i=cursor_+step;i>=0&&i<n;i+=step)if(window_.rows[i].selection==selection)return false;
    const auto &confirmed=step>0?confirmed_bottom_:confirmed_top_;
    if(std::find(confirmed.begin(),confirmed.end(),selection)==confirmed.end()){begin_probe(step,selection,out);return true;}
    if(window_.content_type=="browser") {
        //! 分類の列の端は、回り込みも操作の行も出さない。名前の列の行が続くなら、そちらへ進む（`browser_column_step()`）。
        if(selection!=primary_selection_)return false;
    } else if(cursor_+step>=0&&cursor_+step<n)return false;
    if(step>0) {
        if(n<total){cursor_=n;detail_scroll_=0;document_scroll_=0;return true;}
        begin_wrap(-1);advance(out);return true;
    }
    if(std::find(confirmed_bottom_.begin(),confirmed_bottom_.end(),primary_selection_)!=confirmed_bottom_.end()) {
        //! 末尾が窓に見えている。操作の行があればそこへ、無ければ原作のカーソルを末尾の行まで進める（窓の中だけの回り込み）。
        if(n<total){cursor_=total-1;return true;}
        focus_target_=window_.rows[total-1].id;focus_steps_=0;advance(out);return true;
    }
    begin_wrap(+1);advance(out);return true;
}
void ModernStoreUi::tab(int d) {
    /*
     * 受けられない間（直前の操作の応答待ち・画面の切り替え待ち・「設定」の画面がまだ
     * 届いていない）に押されたタブ移動は、捨てずに預けて sync() で回す
     * （2026-09-23 気づいたこと「右 Shift が右タブに遷移できないことがある」）。
     * 持ち物の操作メニューを開いている間（operations_）は、従来どおり移らない。
     */
    const bool tab_owner=(active()&&(item_window()||parent_tab_navigation_active()))||awaiting_parent_child();
    const bool ready=active()&&!pending_&&window_.input_ready
        &&(parent_tab_navigation_active()?window_.stage=="cursor":window_.stage=="browse");
    if(tab_owner&&!ready&&!operations_) {deferred_tab_=d;deferred_tab_at_=std::chrono::steady_clock::now();return;}
    if(!active()||pending_||operations_)return;
    if(parent_tab_navigation_active()) {
        if(window_.stage!="cursor"||!window_.input_ready||parent_categories_.empty())return;
        const int count=int(parent_categories_.size());
        const int target=(parent_category_+d+count)%count;
        if(target==parent_category_)return;
        parent_tab_request_=target;
        /*
         * Do not reopen the parent FeatureMenu yet.  The Rogue option editor
         * still owns native input until this Cancel has crossed the bridge and
         * the Modern child actually disappears.  take_parent_tab_request()
         * releases the request only after that close is observable.
         */
        send("cancel",buffered_);return;
    }
    if(!item_window()||window_.stage!="browse")return;
    if(window_.kind=="inventory") {
        if(inventory_categories_.empty())return;
        inventory_tab_request_=(inventory_category_+d+int(inventory_categories_.size()))%int(inventory_categories_.size());
        send("cancel",buffered_);return;
    }
    tabs_.remember(cursor_,scroll_);tabs_.step(d);cursor_=tabs_.cursor();scroll_=tabs_.scroll();detail_scroll_=0;notice_.clear();details_focus_=false;
}
void ModernStoreUi::open_inventory(presentation::InputEventsMessage &out,std::vector<std::string> categories,int index) {
    inventory_categories_=std::move(categories);inventory_category_=index;inventory_tab_request_=-1;
    if(pending_){inventory_open_queued_=true;return;}
    window_={};window_.version=5;window_.revision=1;window_.kind="inventory";window_.stage="browse";window_.title=tr("持ち物一覧","Inventory");local_placeholder_=true;
    enabled_=true;operations_=false;details_focus_=false;workflow_.clear();send("inventory_open",out);
}
void ModernStoreUi::set_parent_command_tabs(std::vector<std::string> categories,int index) {
    parent_categories_=std::move(categories);
    parent_category_=parent_categories_.empty()?-1:std::clamp(index,0,int(parent_categories_.size())-1);
    parent_tab_request_=-1;parent_header_first_=0;parent_tabs_seen_=false;
}
int ModernStoreUi::take_parent_tab_request() {
    if(parent_tab_request_<0||active()||pending_)return -1;
    const int index=parent_tab_request_;
    parent_tab_request_=-1;parent_categories_.clear();parent_category_=-1;parent_header_first_=0;parent_tabs_seen_=false;
    return index;
}
void ModernStoreUi::page(int direction,presentation::InputEventsMessage &out) {
    if(!active()||!window_.input_ready||pending_||operations_)return;
    const int step=(direction<0?-1:1)*std::max(1,window_.page_size);
    if(commands_open_){command_cursor_=std::clamp(command_cursor_+step,0,std::max(0,int(window_.actions.size())-1));return;}
    if(window_.stage=="text"){if(direction<0?window_.prev_key:window_.next_key)send("document_page",out,direction<0?0:1);return;}
    if(details_focus_&&window_.kind!="dialog") {detail_scroll_=std::max(0,detail_scroll_+step);return;}
    if(details_focus_||window_.stage=="detail") {
        if(!details_focus_&&(direction<0?window_.prev_key:window_.next_key)&&(window_.version<5||!document_mode()||(direction<0?window_.previous:window_.next)))send("document_page",out,direction<0?0:1);
        else document_scroll_=std::max(0,document_scroll_+step);
        return;
    }
    if(window_.stage=="cursor"||(window_.stage=="choices"&&(direction<0?window_.prev_key:window_.next_key))) {if(direction<0?window_.prev_key:window_.next_key)send("document_page",out,direction<0?0:1);return;}
    if(window_.stage!="choices"&&window_.stage!="browse"&&window_.stage!="services")return;
    const auto list=rows();if(list.empty())return;
    const int wanted=std::clamp(cursor_+step,0,int(list.size())-1);
    if(window_.stage=="choices"&&window_.cursor>=0){focus_target_=list[wanted]->id;advance(out);}
    else {cursor_=wanted;detail_scroll_=0;tabs_.remember(cursor_,scroll_);}
}
void ModernStoreUi::toggle_commands(){
    if(!active()||pending_||!window_.input_ready||window_.actions.empty()||parent_tab_navigation_active()
        ||window_.content_type=="settings_list_rogue")return;
    commands_open_=!commands_open_;command_cursor_=command_scroll_=0;details_focus_=false;focus_target_.clear();focus_steps_=0;
}
void ModernStoreUi::focus_details(bool on){
    if(!on){details_focus_=false;return;}
    if(window_.content_type=="map")return;
    if(active()&&!commands_open_&&!operations_&&!form_active()&&window_.input_ready&&!pending_&&(window_.stage=="browse"||window_.stage=="services"||window_.kind=="dialog"))details_focus_=true;
}
void ModernStoreUi::toggle_details(){focus_details(!details_focus_);}
bool ModernStoreUi::search_document(bool previous,presentation::InputEventsMessage &out){
    if(!active()||pending_||!window_.input_ready||!document_mode())return false;
    for(const auto &a:window_.actions)if(a.id==(previous?"doc-find-previous":"doc-find-next")){target_=a.id;send("window_action",out);return true;}
    for(const auto &a:window_.actions)if(a.id=="doc-search"){target_=a.id;send("window_action",out);return true;}
    return false;
}
bool ModernStoreUi::show_help(presentation::InputEventsMessage &out){
    if(!active()||!window_.input_ready||pending_||commands_open_||window_.kind!="dialog")return false;
    if(window_.stage=="choices")if(const auto row=selected())for(const auto &a:row->actions)if(a.id=="help"&&a.enabled){target_=row->id;send("row_action",out,a.key);return true;}
    if(window_.stage=="cursor")for(const auto &row:window_.rows)if(row.id==window_.cursor_target)for(const auto &action:row.actions)if(action.id=="help"&&action.enabled){target_=row.id;send("row_action",out,action.key);return true;}
    for(const auto &a:window_.actions)if(a.key=='?'&&a.enabled){target_=a.id;send("window_action",out);return true;}
    if(window_.stage!="choices")return false;
    for(const auto &row:window_.rows)if(row.key=='?'&&row.available){target_=row.id;send("choice",out);return true;}
    return false;
}
//! 原作のカーソルを目標の行へ動かしている途中か（`focus` を送った後の返事待ち、または返事が来て次の 1 手を送る前）。
//! Sil-Q の能力値の窓の `press_after_focus_` を使う動き（± のキーを送る前の移動）は数えない。
//! 原作のカーソルがもう目標の行に着いているとき（返事が届いて `advance()` が `focus_target_` を片づける前）は、歩いている間ではない。
//! 数えると、その間に押した左右（Sil-Q の能力値の窓の「＋」）が捨てられる（修-59 の戻り）。
bool ModernStoreUi::walking_focus() const {
    return active()&&window_.kind=="dialog"&&window_.stage=="cursor"&&!window_.rows.empty()&&!focus_target_.empty()
        &&window_.cursor_target!=focus_target_
        &&press_after_focus_.empty()&&!raw_pending_&&(!pending_||sent_action_=="focus");
}
bool ModernStoreUi::handle(CursorNav nav,presentation::InputEventsMessage &out) {
    if(!active())return false;
    if(!window_.input_ready||window_.stage=="finish"||nav==CursorNav::None)return true;
    /*
     * 原作のカーソルが目標の行へ歩いている間（調-08。F-gensoband-02）。返事待ちで入力を捨てると、幻想蛮怒の登場作品の一覧のように
     * 1 つ動かすのに約 0.5 秒かかる窓で、続けて押した上下と決定が約 0.7 秒取りこぼされる。
     * 上下は目標の行を付け替えるだけ（コアへは送らない。歩きは advance() が続ける）。決定は、歩き終えるまで預ける。
     */
    if(walking_focus()&&!commands_open_&&!operations_&&!details_focus_&&!wrap_.active) {
        if(nav==CursorNav::Confirm) {
            if(!confirm_after_focus_){confirm_after_focus_=true;confirm_after_focus_at_=std::chrono::steady_clock::now();confirm_target_=focus_target_;confirm_scope_=window_scope();}
            return true;
        }
        if(confirm_after_focus_||(nav!=CursorNav::Up&&nav!=CursorNav::Down)||window_.content_type=="poker"||paired_allocation(window_))return true;
    } else if(pending_)return true;
    const int step=nav==CursorNav::Up?-1:(nav==CursorNav::Down?1:0);
    if(wrap_.active)return true;
    if(commands_open_) {
        command_cursor_=loop_index(command_cursor_,step,int(window_.actions.size()));
        if(nav==CursorNav::Confirm&&!window_.actions.empty()) {
            const auto &action=window_.actions[command_cursor_];
            if(!action.enabled){notice_=action.reason;return true;}
            target_=action.id;commands_open_=false;send("window_action",out);
        }
        return true;
    }
    if(operations_) {
        const auto item=selected();if(!item){operations_=false;return true;}
        operation_=loop_index(operation_,step,static_cast<int>(item->actions.size()));
        if(nav==CursorNav::Confirm&&!item->actions.empty()) {
            const auto &a=item->actions[operation_];if(!a.enabled){notice_=a.reason;return true;}
            if(a.id=="back"){operations_=false;return true;}
            if(window_.kind=="inventory"){operations_=false;send(a.id,out);return true;}
            if(window_.kind=="dialog"){operations_=false;send("row_action",out,a.key);return true;}
            operations_=false;workflow_=a.id;workflow_target_=target_;workflow_started_=false;workflow_steps_=0;advance(out);
        }return true;
    }
    if(window_.stage=="text") {
        out.events.insert(out.events.end(),buffered_.events.begin(),buffered_.events.end());buffered_.events.clear();
        presentation::InputEventWire e;
        if(nav==CursorNav::Confirm){
            if(window_.text_confirm_action){send("text_confirm",out);return true;}
            e.e="confirm";pending_=true;raw_pending_=true;sent_revision_=window_.revision;sent_at_=std::chrono::steady_clock::now();
        }
        else if(nav==CursorNav::Left||nav==CursorNav::Right||nav==CursorNav::Up||nav==CursorNav::Down){
            if(window_.text_edit_mode=="append"){notice_=tr("この欄は末尾から入力し、Backspaceで削除します。","Type at the end and use Backspace to erase.");return true;}
            if((nav==CursorNav::Up||nav==CursorNav::Down)&&window_.text_edit_mode=="basic")return true;
            text_fresh_=false;e.e="key";e.ctrl=true;e.chr=nav==CursorNav::Left?"b":(nav==CursorNav::Right?"f":(nav==CursorNav::Up?"p":"n"));}
        if(!e.e.empty())out.events.push_back(e);return true;
    }
    if(window_.stage=="number") {
        if(step){const int n=value(number_);number_=std::to_string(std::clamp((n<0?numeric_.min:n)-step,numeric_.min,numeric_.max));replace_number_=true;}
        if(nav==CursorNav::Confirm){int n=value(number_);if(n<numeric_.min||n>numeric_.max)notice_=tr("範囲内の数量を入力してください。","Enter a valid quantity.");else send("number",out,n);}return true;
    }
    if(window_.stage=="confirm") {
        const int delta=step?step:(nav==CursorNav::Left?-1:(nav==CursorNav::Right?1:0));
        answer_=loop_index(answer_,delta,static_cast<int>(prompt_.choices.size()));
        if(nav==CursorNav::Confirm&&!prompt_.choices.empty())send("answer",out,answer_);return true;
    }
    if(nav==CursorNav::Confirm&&message_scroll_<message_max_scroll_) {
        message_scroll_=std::min(message_max_scroll_,message_scroll_+4);return true;
    }
    if(window_.stage=="message"||window_.message_only) {
        const bool direction=nav==CursorNav::Up||nav==CursorNav::Down||nav==CursorNav::Left||nav==CursorNav::Right;
        const auto jp_marker=std::string(reinterpret_cast<const char*>(u8"［続く］"));
        const auto ascii_marker=std::string(reinterpret_cast<const char*>(u8"[続く]"));
        const bool marker_visible=window_.body.find(jp_marker)!=std::string::npos
            ||window_.body.find(ascii_marker)!=std::string::npos
            ||window_.input.find(jp_marker)!=std::string::npos
            ||window_.input.find(ascii_marker)!=std::string::npos;
        // 方向入力を Continue に変えるのは、画面上に実際の［続く］が
        // 表示されている時だけ。"More" や「スペースで続ける」は対象外。
        if(window_.content_type=="message"&&window_.continue_key==' '&&direction&&marker_visible) {
            send("continue",out);return true;
        }
        if(window_.message_only&&!retained_choices_)document_scroll_=std::max(0,document_scroll_+step);
        else message_scroll_=std::clamp(message_scroll_+step,0,message_max_scroll_);
        if(nav==CursorNav::Confirm)send("continue",out);return true;
    }
    if(details_focus_) {
        auto &scroll=window_.kind=="dialog"?document_scroll_:detail_scroll_;
        scroll=std::max(0,scroll+step);
        if(nav==CursorNav::Confirm)details_focus_=false;
        return true;
    }
    if((nav==CursorNav::Left||nav==CursorNav::Right)&&sil_allocation_window(window_)&&window_.stage=="cursor"&&!window_.capture_keys) {
        //! Sil-Q の能力値・技能の窓：左右は項目の値を減らす・増やす（原作の 4・6）。決定は「次に進む」に空けてある（修-59）。
        if(cursor_>=0&&cursor_<paired_allocation_count(window_))press_allocation(cursor_-cursor_%2+(nav==CursorNav::Right?1:0),out);
        return true;
    }
    if((nav==CursorNav::Left||nav==CursorNav::Right)&&!window_.capture_keys
        &&(window_.stage=="choices"||window_.stage=="cursor")) {
        const int columns=paired_allocation(window_)?2:(window_.content_type=="poker"?5:1);
        const int count=paired_allocation(window_)?paired_allocation_count(window_):(window_.content_type=="poker"?5:0);
        if(cursor_<count&&columns>1) {
            const int column=cursor_%columns,delta=nav==CursorNav::Left?-1:1;
            if(column+delta>=0&&column+delta<columns) {
                cursor_+=delta;selection_column_=cursor_%columns;
                if(window_.stage=="cursor"&&!paired_allocation(window_)){focus_target_=window_.rows[cursor_].id;focus_steps_=0;advance(out);}
                return true;
            }
        }
        // Paired -/+ controls own both horizontal directions.  At the left or
        // right edge, keep focus on the control instead of falling through to
        // the information pane.
        if(paired_allocation(window_))return true;
    }
    // Left/right are reserved for value/grid selection.  Details focus is
    // moved explicitly by Tab or L2/R2 in the input layer.
    if(window_.kind=="dialog") {
        if(navigation_page_) {
            cursor_=loop_index(cursor_,step,int(window_.rows.size()));
            if(nav==CursorNav::Confirm)if(const auto row=selected()) {
                if(row->id.starts_with("@action:")){target_=row->id.substr(8);send("window_action",out);}
                else if(row->id=="@continue")send("continue",out);
                else if(row->id.starts_with("@section:")){requested_section_=std::atoi(row->id.c_str()+9);details_focus_=true;}
            }
            return true;
        }
        if(window_.stage=="cursor") {
            if(!window_.rows.empty()&&!window_.capture_keys) {
                if(step&&scroll_edge(step,out))return true;
                int next=cursor_+step;
                if(step&&paired_allocation(window_))next=paired_vertical_index(window_,cursor_,step,selection_column_);
                else if(step&&scroll_list_&&window_.content_type=="browser")next=browser_column_step(step);
                else if(step&&window_.content_type!="poker")next=loop_index(cursor_,step,int(window_.rows.size()));
                if(step&&window_.content_type=="poker") {
                    if(cursor_<5){selection_column_=cursor_;next=step>0?5:cursor_;}
                    else next=step<0?std::clamp(selection_column_,0,4):cursor_;
                }
                if(step&&window_.content_type=="poker"&&cursor_==5&&step>0)next=std::clamp(selection_column_,0,4);
                if(step&&window_.content_type=="poker"&&cursor_<5&&step<0)next=5;
                cursor_=loop_index(next,0,int(window_.rows.size()));
                if(step){
                    if(paired_allocation(window_)) {
                        const int paired=paired_allocation_count(window_);
                        // The Gensoband bonus screen includes Continue in its
                        // native cursor. Sil-Q does not: its Continue row is a
                        // modern control that sends Enter from the current
                        // native skill. Keep that distinction or Sil-Q loops
                        // forever trying to focus a cursor position it lacks.
                        if(cursor_<paired||window_.content_type=="stat_allocation") {
                            const int target_index=cursor_<paired?cursor_-(cursor_%2):cursor_;
                            focus_target_=window_.rows[target_index].id;focus_steps_=0;advance(out);
                        }
                    } else if(window_.rows[cursor_].id!="@continue"&&!window_.rows[cursor_].id.starts_with("@action:")&&window_.rows[cursor_].key!='*') {
                        focus_target_=window_.rows[cursor_].id;focus_steps_=0;advance(out);
                    }
                    return true;
                }
                const bool settings_list=window_.content_type=="settings_list"||window_.content_type=="settings_list_rogue";
                const auto has_key=[&](int k){const std::string id="key:"+std::to_string(k);return std::any_of(window_.actions.begin(),window_.actions.end(),[&](const auto &a){return a.id==id;});};
                if(settings_list&&(nav==CursorNav::Left||nav==CursorNav::Right)) {
                    const auto &row=window_.rows[cursor_];
                    if(!row.id.starts_with("@action:")) {
                        // Rogue の設定は左右で値を変えない。決定で切り替える。
                        if(window_.content_type=="settings_list_rogue")return true;
                        // 値を変えるキーはコアで違う（変愚系・Frox・Angband は y/n、Sil-Q は 6/4）。窓が申告する動作にある組を使う（修-07）。
                        // Sil-Q に y/n を送ると拒否されていた（F-silq-04）。どちらの組も無ければ従来の y/n。
                        const int more_key=has_key('y')?'y':(has_key('6')?'6':'y'),less_key=has_key('n')?'n':(has_key('4')?'4':'n');
                        target_="key:"+std::to_string(nav==CursorNav::Right?more_key:less_key);
                        send("window_action",out);
                        return true;
                    }
                }
                if(nav==CursorNav::Confirm) {
                    if(sil_allocation_window(window_)) {
                        //! Sil-Q の能力値・技能の窓：決定はどの行でも「次に進む」。点を増やさない（修-59。BUG-183 の決め）。
                        const auto done=std::find_if(window_.rows.begin(),window_.rows.end(),[](const auto &r){return r.id=="allocation:continue";});
                        if(done!=window_.rows.end()){target_=done->id;press_after_focus_.clear();send("choice",out);}
                        return true;
                    }
                    const auto &row=window_.rows[cursor_];target_=row.id;
                    if(row.id.starts_with("@action:")){target_=row.id.substr(8);send("window_action",out);return true;}
                    if(window_.content_type=="settings_list_rogue") {
                        // 真偽は反転、3 択は次の値、文字列の項目はここで入力を始める。
                        const auto action=std::find_if(window_.actions.begin(),window_.actions.end(),[](const auto &a){return a.id=="rogue-setting-toggle"&&a.enabled;});
                        if(action!=window_.actions.end()){target_=action->id;send("window_action",out);}
                        return true;
                    }
                    if(settings_list) {
                        // Sil-Q の設定の一覧（動作の表が 6/4）：決定は値を 1 つ進める（判-14）。窓が申告する「いまの値の次のキー」
                        // （`continue_key`。切の項目は 6＝入れる、入の項目は 4＝切る、数の項目は 6＝上げる）を、左右と同じ window_action で送る。
                        // 変愚系・Frox・Angband（動作の表が y/n）の決定は今までどおり何も送らない（Sil-Q だけの変更）。
                        if(has_key('6')&&!has_key('y')&&has_key(window_.continue_key)){target_="key:"+std::to_string(window_.continue_key);send("window_action",out);}
                        return true;
                    }
                    if(row.key=='*'){send("choice",out);return true;}
                    if(paired_allocation(window_)){send("choice",out);return true;}
                    if(std::any_of(row.actions.begin(),row.actions.end(),[](const auto &a){return a.id=="decrease"||a.id=="increase";})){operations_=true;operation_=0;}
                    else send("continue",out);
                    return true;
                }
            }
            if(nav==CursorNav::Confirm)send("continue",out);
            else {
                const ModernPaintedCell *from=nullptr,*best=nullptr;
                if(window_.content_type=="choice_grid")for(const auto &cell:content_navigation_)if(cell.target==window_.cursor_target){from=&cell;break;}
                long long score=LLONG_MAX;
                if(from)for(const auto &cell:content_navigation_) {
                    if(cell.target.empty()||cell.target==from->target)continue;
                    const int dx=(cell.rect.x*2+cell.rect.w)-(from->rect.x*2+from->rect.w),dy=(cell.rect.y*2+cell.rect.h)-(from->rect.y*2+from->rect.h);
                    const bool horizontal=nav==CursorNav::Left||nav==CursorNav::Right;
                    const int primary=horizontal?dx:dy,secondary=horizontal?dy:dx;
                    const int sign=nav==CursorNav::Up||nav==CursorNav::Left?-1:1;
                    if(primary*sign<=0||(horizontal&&std::abs(secondary)>from->rect.h))continue;
                    const long long cost=static_cast<long long>(std::abs(primary))*8+std::abs(secondary)*3;
                    if(cost<score){score=cost;best=&cell;}
                }
                if(best){focus_target_=best->target;focus_steps_=0;advance(out);}
                else send("navigate",out,nav==CursorNav::Up?8:(nav==CursorNav::Down?2:(nav==CursorNav::Left?4:6)));
            }
        } else if(window_.stage=="choices") {
            if(step&&window_.cursor>=0){
                if(scroll_edge(step,out))return true;
                const int wanted=loop_index(window_.cursor,step,int(window_.rows.size()));focus_target_=window_.rows[wanted].id;focus_steps_=0;advance(out);return true;
            }
            const int before=cursor_;
            int next=cursor_+step;
            if(step&&paired_allocation(window_))next=paired_vertical_index(window_,cursor_,step,selection_column_);
            else if(step)next=loop_index(cursor_,step,int(window_.rows.size()));
            cursor_=loop_index(next,0,static_cast<int>(window_.rows.size()));
            if(before!=cursor_)document_scroll_=0;
            if(nav==CursorNav::Confirm&&!window_.rows.empty()){const auto &row=window_.rows[cursor_];if(!row.available){notice_=row.reason;return true;}target_=row.id;send("choice",out);}
        } else if(window_.stage=="detail") {
            if(document_mode()&&((step>0&&document_scroll_>=document_max_scroll_&&window_.next)||(step<0&&document_scroll_==0&&window_.previous))){send("document_page",out,step>0?1:0);return true;}
            document_scroll_=std::max(0,document_scroll_+step);
            if(nav==CursorNav::Confirm)send("continue",out);
            if(nav==CursorNav::Left||nav==CursorNav::Right) {
                const bool next=nav==CursorNav::Right;
                if(next?window_.next_key:window_.prev_key)send("document_page",out,next?1:0);
                else document_x_=std::max(0,document_x_+(next?32:-32));
            }
        }
        return true;
    }
    const auto r=rows();
    const int old=cursor_;cursor_=loop_index(cursor_,step,static_cast<int>(r.size()));
    if(cursor_!=old){detail_scroll_=0;notice_.clear();}tabs_.remember(cursor_,scroll_);
    if(nav==CursorNav::Confirm&&!r.empty()) {
        const auto &item=*r[cursor_];target_=item.id;
        if(window_.kind=="building") {if(item.available)send("service",out);else notice_=item.reason;}
        else if(window_.stage=="browse") {
            if(item.id.rfind("store:command:",0)==0&&item.actions.size()==1) {
                if(item.actions.front().enabled)send(item.actions.front().id,out);else notice_=item.actions.front().reason;
            } else {operations_=true;operation_=0;}
        }
    }return true;
}
void ModernStoreUi::cancel(presentation::InputEventsMessage &out) {
    if(!active()||!window_.input_ready)return;
    wrap_={};deferred_step_=0;confirm_after_focus_=false;
    if(pending_){if(sent_action_=="navigate"||sent_action_=="focus"){focus_target_.clear();cancel_after_navigation_=true;}return;}
    if(commands_open_){commands_open_=false;return;}
    if(details_focus_){details_focus_=false;return;}if(operations_){operations_=false;return;}
    workflow_.clear();focus_target_.clear();press_after_focus_.clear();
    if(text_mode())buffered_.events.clear();
    if(text_mode()&&!window_.text_confirm_action){buffered_.events.clear();presentation::InputEventWire e;e.e="cancel";out.events.push_back(e);raw_pending_=true;pending_=true;sent_revision_=window_.revision;sent_at_=std::chrono::steady_clock::now();return;}
    send("cancel",out);
}
std::vector<std::string> ModernStoreUi::tab_labels() const {
    if(window_.kind=="inventory")return inventory_categories_.empty()?std::vector<std::string>{tr("持ち物一覧","Inventory")}:inventory_categories_;
    return {window_.kind=="home"?tr("家のアイテム","Home items"):(window_.kind=="museum"?tr("展示品","Exhibits"):tr("店の商品","Store items")),tr("持ち物・装備","Inventory / equipment")};
}
int ModernStoreUi::keyboard_key(const ModernRow &row) const {
    if(window_.kind=="inventory"){const auto item=selected();return item&&item->selection==row.selection?row.hotkey:0;}
    if(!item_window())return row.hotkey;
    if(window_.stage=="choose")return row.tab==0?row.key:0;
    if(window_.stage=="own_choose")return row.tab==1&&row.selection>=0?row.hotkey:0;
    const auto item=selected();if(!item||row.tab!=item->tab)return 0;
    if(row.tab==0&&row.slot/std::max(1,window_.page_size)!=item->slot/std::max(1,window_.page_size))return 0;
    if(row.tab==1&&row.equipped!=item->equipped)return 0;
    return row.hotkey;
}
bool ModernStoreUi::handle_native_key(int key,presentation::InputEventsMessage &out) {
    if(!active())return false;if(!window_.input_ready||pending_)return true;
    if(commands_open_) {
        for(const auto &a:window_.actions)if(a.key==key&&a.enabled){target_=a.id;commands_open_=false;send("window_action",out);return true;}
        return true;
    }
    if(text_mode()||window_.stage=="number")return false;
    for(const auto &a:window_.actions)if(a.key==key&&a.enabled){target_=a.id;send("window_action",out);return true;}
    if(window_.stage=="confirm") {
        for(size_t i=0;i<prompt_.choices.size();++i)if(prompt_.choices[i].key==key
            ||((key=='Y'||key=='y')&&(prompt_.choices[i].key=='Y'||prompt_.choices[i].key=='y'))
            ||((key=='N'||key=='n')&&(prompt_.choices[i].key=='N'||prompt_.choices[i].key=='n'))) {
            answer_=static_cast<int>(i);send("answer",out,answer_);return true;
        }
        return true;
    }
    if(operations_) {
        const auto item=selected();if(!item)return true;
        for(size_t i=0;i<item->actions.size();++i)if(item->actions[i].key==key) {
            operation_=static_cast<int>(i);return handle(CursorNav::Confirm,out);
        }
        return true;
    }
    const auto list=rows();
    if(key=='/'&&item_window()&&(tabs_.index()==1||window_.kind=="inventory")&&window_.stage=="browse") {
        const auto item=selected();
        for(size_t i=0;i<list.size();++i)if(item&&list[i]->equipped!=item->equipped){cursor_=static_cast<int>(i);detail_scroll_=0;tabs_.remember(cursor_,scroll_);return true;}
        return true;
    }
    if(window_.kind=="dialog"&&window_.stage=="choices")for(const auto *row:list)for(const auto &a:row->actions)if(a.id=="help"&&a.enabled&&a.key==key){target_=row->id;send("row_action",out,key);return true;}
    int match=-1;
    for(size_t i=0;i<list.size();++i)if(key && (keyboard_key(*list[i])==key || (window_.kind=="dialog"&&window_.stage=="choices"&&std::any_of(list[i]->actions.begin(),list[i]->actions.end(),[&](const auto &a){return a.id=="choice"&&a.enabled&&a.key==key;})))) {
        if(match>=0){notice_=tr("同じ記号が複数あります。矢印で対象を選んでください。","This key is ambiguous. Select the target with the arrow keys.");return true;}
        match=static_cast<int>(i);
    }
    if(match>=0) {
        cursor_=match;target_=list[match]->id;detail_scroll_=0;details_focus_=false;tabs_.remember(cursor_,scroll_);
        if(window_.stage=="cursor"){focus_target_.clear();send("choice",out);return true;}
        if(window_.stage=="choose"||window_.stage=="own_choose") {
            workflow_="select-only";workflow_target_=target_;workflow_started_=true;workflow_steps_=0;advance(out);return true;
        }
        return handle(CursorNav::Confirm,out);
    }
    if(key>0&&key<128&&(window_.kind=="dialog"||window_.stage=="browse"||window_.stage=="services"))send("native_key",out,key);
    return true;
}
void ModernStoreUi::text(const std::string &s) {
    if(!active()||!window_.input_ready||pending_)return;
    if(commands_open_){if(s.size()==1)handle_native_key(static_cast<unsigned char>(s[0]),buffered_);return;}
    if(text_mode()){
        if(window_.text_ascii_only&&std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32||c>126;})){
            notice_=tr("この欄は半角の英数字・記号で入力してください。","Use ASCII letters, numbers and punctuation in this field.");return;
        }
        if(window_.text_fullwidth_kana)for(size_t i=0;i+2<s.size();++i) {
            const auto a=static_cast<unsigned char>(s[i]),b=static_cast<unsigned char>(s[i+1]),c=static_cast<unsigned char>(s[i+2]);
            if(a==0xef&&((b==0xbd&&c>=0xa1&&c<=0xbf)||(b==0xbe&&c>=0x80&&c<=0x9f))){notice_=tr("この欄では全角カナを使ってください。","Use full-width kana in this field.");return;}
        }
        text_fresh_=false;presentation::InputEventWire e;e.e="text";e.text=s;buffered_.events.push_back(e);return;
    }
    if(window_.stage!="number"){if(s.size()==1&&static_cast<unsigned char>(s[0])<128)handle_native_key(static_cast<unsigned char>(s[0]),buffered_);return;}for(char c:s)if(c<'0'||c>'9')return;
    if(replace_number_){number_.clear();replace_number_=false;}if(number_.size()+s.size()<=9)number_+=s;
    //! 打ち足した結果が最大を超えたら最大に張り付ける（修-06。最小は途中の桁で下回るのがふつうなので、決定のときの範囲の確認に任せる）。
    if(const int n=value(number_);n>=0&&numeric_.max>=numeric_.min&&n>numeric_.max)number_=std::to_string(numeric_.max);
}
void ModernStoreUi::backspace() {
    if(!active()||!window_.input_ready||pending_)return;
    if(text_mode()){
        if(clear_fresh_text())return;
        presentation::InputEventWire e;e.e="key";e.name="backspace";buffered_.events.push_back(e);
    }
    else if(window_.stage=="number"){if(!number_.empty())number_.pop_back();replace_number_=false;}
}
void ModernStoreUi::edit_text_key(int key) {
    if(!active()||!window_.input_ready||pending_)return;
    if(window_.stage=="number"&&key==1){replace_number_=true;return;}
    if(!text_mode())return;
    if(window_.text_edit_mode=="append"){notice_=tr("この欄は末尾から入力し、Backspaceで削除します。","Type at the end and use Backspace to erase.");return;}
    if(key==127&&clear_fresh_text())return;
    text_fresh_=false;
    presentation::InputEventWire e;e.e="key";
    if(key==127)e.name="delete";
    else if(key>=1&&key<=26){e.ctrl=true;e.chr=std::string(1,static_cast<char>('a'+key-1));}
    else return;
    buffered_.events.push_back(e);
}
void ModernStoreUi::edit_text_boundary(bool end) {
    if(!active()||!window_.input_ready||pending_||!text_mode())return;
    if(window_.text_edit_mode=="full"||window_.text_edit_mode=="editor"){edit_text_key(end?5:1);return;}
    if(window_.text_edit_mode=="append"){notice_=tr("この欄は末尾から入力し、Backspaceで削除します。","Type at the end and use Backspace to erase.");return;}
    int count=128; // The basic native fields are bounded to a terminal line.
    if(window_.text_edit_mode=="grid") {
        const auto caret=size_t(std::clamp(window_.input_caret,0,int(window_.input.size())));count=0;
        for(size_t i=end?caret:0;i<(end?window_.input.size():caret);++i)if((static_cast<unsigned char>(window_.input[i])&0xc0)!=0x80)++count;
    }
    text_fresh_=false;
    presentation::InputEventWire e;e.e="key";e.ctrl=true;e.chr=end?"f":"b";
    for(int i=0;i<count;++i)buffered_.events.push_back(e);
}
/*!
 * 「基本」型の入力欄（幻想蛮怒・Frox の `askfor`）で、まだ何も編集していない欄の最初の Backspace・Delete は
 * 既定の字を全部消す（修-58。判-9 (2)。画面側の操作性の改善で、コアには普通のキーだけを送る）。
 * 末尾まで Ctrl-F で動いてから字数ぶんの Backspace を送る。2 回目以降は 1 字ずつのまま。消したら真を返す。
 */
bool ModernStoreUi::clear_fresh_text() {
    if(!text_fresh_)return false;
    text_fresh_=false;
    if(window_.text_edit_mode!="basic")return false;
    int count=0;for(const unsigned char c:window_.input)if((c&0xc0)!=0x80)++count;
    presentation::InputEventWire forward;forward.e="key";forward.ctrl=true;forward.chr="f";
    presentation::InputEventWire back;back.e="key";back.name="backspace";
    for(int i=0;i<count;++i)buffered_.events.push_back(forward);
    for(int i=0;i<count;++i)buffered_.events.push_back(back);
    return true;
}
RectPx ModernStoreUi::text_edit_rect(const UiLayout &l) const {
    const auto scale=menu_font_scale(font_level_);
    return menu_scaled_rect(text_edit_rect_logical(menu_logical_layout(l,scale)),scale);
}
RectPx ModernStoreUi::text_edit_rect_logical(const UiLayout &l) const {
    auto r=input_rect(l);r.h=38;return r;
}
RectPx ModernStoreUi::panel(const UiLayout &l) const{return l.preview_menu.empty()?fixed_menu_rect(l):l.preview_menu;}
bool ModernStoreUi::context_popup_active() const {
    return (context_anchor_valid_||menu_popup_)&&menu_over_map_&&active()&&window_.kind=="dialog"&&window_.stage=="choices"
        &&!window_.rows.empty()&&!commands_open_&&!operations_&&!navigation_page_&&!form_active();
}
RectPx ModernStoreUi::context_popup_box() const {
    return context_popup_active()?menu_scaled_rect(context_rect_,menu_font_scale(font_level_)):RectPx{};
}
/*
 * 地図のクリック・長押しが開いた文脈メニュー（AH-58）。コアが `menu_over_map` と言う選択肢の窓は、窓いっぱいの
 * 窓ではなく、押した点の近くの小さな窓で出す（原作も地図の上へ小さな箱を重ねる）。地図（立体）は後ろに残る。
 * 見出し（「〜が見える:」）は先頭の「(操作の案内)」を外して上に、行はその下に並べる。右下へ出し、画面の端で
 * 切れるなら反対側へ寄せる（地図の矩形の中に収める）。行のクリックはすぐ実行、窓の外のクリックは取消。
 */
void ModernStoreUi::draw_context_popup(UiPaint &p,TextOverlay &t,const UiLayout &l,int theme) const {
    const auto skin=modern_skin(theme);
    const int pad=10,row_height=std::max(30,t.cell_h()+10),line_height=t.cell_h()+2;
    std::string heading=context_heading_;
    if(!heading.empty()&&heading.front()=='(') {
        const auto close=heading.find(')');
        if(close!=std::string::npos)heading=heading.substr(close+1);
    }
    const auto first=heading.find_first_not_of(" \t");
    heading=first==std::string::npos?std::string():heading.substr(first);
    const RectPx region=l.scene.empty()?l.content:l.scene;
    int widest=0;
    for(const auto &row:window_.rows)widest=std::max(widest,t.measure(menu_label_with_key(row.hotkey,row.label)));
    const int limit=std::max(140,region.w-16);
    const int width=std::clamp(widest+2*pad+16,std::min(300,limit),std::min(520,limit));
    const auto wrapped=heading.empty()?std::vector<std::string>{}:wrap(t,heading,width-2*pad);
    const int heading_lines=std::min(3,static_cast<int>(wrapped.size()));
    const int heading_height=heading_lines>0?heading_lines*line_height+8:0;
    const int count=static_cast<int>(window_.rows.size());
    const int max_height=std::max(row_height*2+2*pad+heading_height,region.h-16);
    const int visible=std::clamp((max_height-2*pad-heading_height)/row_height,1,count);
    const int height=2*pad+heading_height+visible*row_height;
    int x=context_anchor_lx_+14,y=context_anchor_ly_+14;
    if(menu_popup_) {
        // クリックした点が無い問い（`menu_popup`）。古いクリックの点は使わず、地図の下寄りの中央へ出す。
        x=region.x+(region.w-width)/2;y=region.y+region.h-height-region.h/8;
    }
    if(x+width>region.x+region.w-6)x=context_anchor_lx_-14-width;
    if(y+height>region.y+region.h-6)y=context_anchor_ly_-14-height;
    x=std::clamp(x,region.x+6,std::max(region.x+6,region.x+region.w-width-6));
    y=std::clamp(y,region.y+6,std::max(region.y+6,region.y+region.h-height-6));
    const RectPx box{x,y,width,height};
    context_rect_=box;
    paint_modern_window(p,box,theme,true);
    int at=box.y+pad;
    for(int i=0;i<heading_lines;++i){t.draw(box.x+pad,at,wrapped[i],skin.muted,box.w-2*pad);at+=line_height;}
    if(heading_lines>0){paint_modern_rule(p,{box.x+pad,at+1,box.w-2*pad,1},theme);at+=8;}
    scroll_=std::clamp(scroll_,0,std::max(0,count-visible));
    if(cursor_<scroll_)scroll_=cursor_;
    if(cursor_>=scroll_+visible)scroll_=cursor_-visible+1;
    for(int i=0;i<visible;++i) {
        const int index=scroll_+i;
        if(index>=count)break;
        const auto &row=window_.rows[index];
        const RectPx cell{box.x+6,at+i*row_height,box.w-12,row_height-2};
        if(index==cursor_)paint_modern_selection(p,cell,theme);
        const auto ink=!row.available?skin.muted:(index==cursor_?skin.selected:skin.text);
        t.draw(cell.x+8,cell.y+(cell.h-t.cell_h())/2,menu_label_with_key(row.hotkey,row.label),ink,cell.w-16);
        content_trace_.push_back({cell,row.label,index==cursor_,row.id});
    }
    if(scroll_>0)t.draw(box.x+box.w-16,box.y+2,"▲",skin.muted,14);
    if(scroll_+visible<count)t.draw(box.x+box.w-16,box.y+box.h-18,"▼",skin.muted,14);
}
void ModernStoreUi::click_context_popup(int x,int y,presentation::InputEventsMessage &out) {
    if(!context_rect_.contains(x,y)){cancel(out);return;}
    for(const auto &cell:content_trace_) {
        if(cell.target.empty()||!cell.rect.contains(x,y))continue;
        const auto row=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &r){return r.id==cell.target;});
        if(row==window_.rows.end())return;
        if(!row->available){notice_=row->reason;return;}
        cursor_=static_cast<int>(row-window_.rows.begin());target_=row->id;send("choice",out);return;
    }
}
RectPx ModernStoreUi::list_rect(const UiLayout &l) const {
    const auto p=panel(l);const bool compact=p.h<360;
    const int share=!window_.venue_header.empty()&&p.w<760?50:(item_window()?66:55);
    const int top=compact?64:76;
    const int bottom=input_rect(l).y-message_height_-(compact?4:12);
    return {p.x+16,p.y+top,std::max(1,(p.w-48)*share/100),std::max(36,bottom-(p.y+top))};
}
RectPx ModernStoreUi::description_rect(const UiLayout &l) const {auto p=panel(l),r=list_rect(l);return {r.x+r.w+16,r.y,std::max(1,p.x+p.w-16-(r.x+r.w+16)),r.h};}
int ModernStoreUi::dialog_answer_count(const UiLayout &l)const {
    const auto box=panel(l);const int header=window_.subtitle.empty()?54:76;
    return std::max(1,std::min({3,int(prompt_.choices.size()),std::max(1,(box.h-header-148)/36)}));
}
int ModernStoreUi::dialog_controls_top(const UiLayout &l)const {
    const auto box=panel(l);
    const int footer=box.h<360?34:44;
    return box.y+box.h-footer-38;
}
RectPx ModernStoreUi::input_rect(const UiLayout &l) const {
    auto p=panel(l);const int width=p.w-40;
    const int footer=p.h<360?34:44;
    return {p.x+20,p.y+p.h-footer-38,width,38};
}
RectPx ModernStoreUi::popup_rect(const UiLayout &l) const {
    auto r=window_.kind=="dialog"?choice_list_rect(l):list_rect(l);const auto item=selected();const int count=item?static_cast<int>(item->actions.size()):0;
    int total=12;for(int i=0;i<count;++i)total+=i<int(operation_heights_.size())?operation_heights_[i]:36;
    const int h=std::min(r.h,std::max(48,total));const int w=std::min(r.w,std::max(80,popup_width_));
    const int row_height=window_.kind=="dialog"?choice_row_height(l):item_row_height(l);
    return {r.x+r.w-w,std::clamp(r.y+(cursor_-scroll_)*row_height,r.y,std::max(r.y,r.y+r.h-h)),w,h};
}
void ModernStoreUi::prepare_popup(TextOverlay &text,const UiLayout &layout) const {
    operation_heights_.clear();popup_width_=80;const auto item=selected();if(!operations_||!item)return;
    const auto list=window_.kind=="dialog"?choice_list_rect(layout):list_rect(layout);
    for(const auto &a:item->actions)popup_width_=std::max(popup_width_,text.measure(menu_key_prefix(a.key)+a.label)+28);
    popup_width_=std::min(popup_width_,list.w);
    for(const auto &a:item->actions)operation_heights_.push_back(std::max(36,int(wrap(text,menu_key_prefix(a.key)+a.label,std::max(1,popup_width_-24)).size())*(text.cell_h()+2)+12));
    operation_scroll_=std::clamp(operation_scroll_,0,std::max(0,int(operation_heights_.size())-1));
    if(operation_<operation_scroll_)operation_scroll_=operation_;
    for(;;) {
        int height=12;for(int i=operation_scroll_;i<=operation_&&i<int(operation_heights_.size());++i)height+=operation_heights_[i];
        if(height<=list.h||operation_scroll_>=operation_)break;
        ++operation_scroll_;
    }
}
void ModernStoreUi::click_operation(const UiLayout &layout,int x,int y) {
    const auto box=popup_rect(layout);if(!box.contains(x,y))return;
    int top=box.y+6;const auto item=selected();if(!item)return;
    for(int i=operation_scroll_;i<int(item->actions.size());++i) {
        const int height=i<int(operation_heights_.size())?operation_heights_[i]:36;
        if(y>=top&&y<top+height){operation_=i;return;}top+=height;
    }
}
void ModernStoreUi::draw_operations(UiPaint &paint,TextOverlay &text,const UiLayout &layout,int theme) const {
    const auto item=selected();if(!operations_||!item)return;
    const auto box=popup_rect(layout);const auto skin=modern_skin(theme);paint_modern_window(paint,box,theme,true);
    int y=box.y+6;
    for(int i=operation_scroll_;i<int(item->actions.size());++i) {
        const int height=i<int(operation_heights_.size())?operation_heights_[i]:36;if(y+height>box.y+box.h)break;
        const auto &action=item->actions[i];const auto ink=action.enabled?(i==operation_?skin.selected:skin.text):skin.muted;
        if(i==operation_)paint_modern_selection(paint,{box.x+4,y,box.w-8,height-4},theme);
        const auto lines=wrap(text,menu_key_prefix(action.key)+action.label,std::max(1,box.w-24));
        for(size_t n=0;n<lines.size();++n)text.draw(box.x+12,y+6+int(n)*(text.cell_h()+2),lines[n],ink,box.w-24);
        content_trace_.push_back({{box.x+4,y,box.w-8,height-4},action.label,i==operation_,"operation:"+action.id});y+=height;
    }
    if(operation_scroll_>0)text.draw(box.x+box.w-16,box.y+2,"▲",skin.muted,14);
    if(!item->actions.empty()&&operation_+1<int(item->actions.size())&&y>=box.y+box.h-40)text.draw(box.x+box.w-16,box.y+box.h-18,"▼",skin.muted,14);
}
void ModernStoreUi::click(const UiLayout &l,int x,int y,presentation::InputEventsMessage &out) {
    const auto scale=menu_font_scale(font_level_);click_logical(menu_logical_layout(l,scale),int(x/scale),int(y/scale),out);
}
void ModernStoreUi::scroll_at(const UiLayout &layout,int x,int y,int direction,presentation::InputEventsMessage &out,bool horizontal) {
    if(!active()||pending_||!window_.input_ready)return;
    const auto scale=menu_font_scale(font_level_);const auto l=menu_logical_layout(layout,scale);
    x=int(x/scale);y=int(y/scale);
    if(context_popup_active()){if(context_rect_.contains(x,y))handle(direction<0?CursorNav::Up:CursorNav::Down,out);return;}
    if(message_rect_.contains(x,y)){message_scroll_=std::clamp(message_scroll_+direction,0,message_max_scroll_);return;}
    if(operations_||commands_open_)return;
    auto list=window_.kind=="dialog"?choice_list_rect(l):list_rect(l);
    const auto box=panel(l);RectPx info{list.x+list.w+16,list.y,box.x+box.w-16-(list.x+list.w+16),list.h};
    if(info.contains(x,y)) {
        if(!form_active())details_focus_=true;
        if(horizontal&&window_.kind=="dialog")scroll_horizontal(direction);
        else {auto &s=window_.kind=="dialog"?document_scroll_:detail_scroll_;s=std::max(0,s+direction);}
    }
    else if(list.contains(x,y)&&window_.content_type=="editor"){if(horizontal)group_x_=std::max(0,group_x_+direction*24);else group_scroll_=std::clamp(group_scroll_+direction,0,group_max_scroll_);}
    else if(list.contains(x,y)&&!form_active()&&!horizontal){details_focus_=false;handle(direction<0?CursorNav::Up:CursorNav::Down,out);}
}
// dialog の確認（stage=confirm）の答えのボタン i の矩形。描く側（draw_dialog）と押す側（click_logical）が同じ式を使う。
static RectPx dialog_answer_rect(const RectPx &box,int controls,int index,int count) {
    const int width=std::max(1,(box.w-32)/std::max(1,count));
    return {box.x+16+index*width,controls,width-4,32};
}
void ModernStoreUi::click_logical(const UiLayout &l,int x,int y,presentation::InputEventsMessage &out) {
    if(!active()||!window_.input_ready||pending_)return;
    if(context_popup_active()){click_context_popup(x,y,out);return;}
    auto p=panel(l),r=list_rect(l),desc=description_rect(l);
    if(!p.contains(x,y))return;
    if(window_.kind=="dialog") {
        if(parent_tab_navigation_active()&&y<p.y+60) {
            if(header_layout_.previous.contains(x,y)){tab(-1);return;}
            if(header_layout_.next.contains(x,y)){tab(1);return;}
            for(size_t i=0;i<header_layout_.tabs.size();++i)if(header_layout_.tabs[i].contains(x,y)) {
                const int at=parent_header_first_+int(i);
                if(at!=parent_category_)tab(at-parent_category_);return;
            }
            return;
        }
        if(operations_) {
            if(y>p.y+p.h-44){if(x>p.x+p.w/2)cancel(out);else handle(CursorNav::Confirm,out);return;}
            click_operation(l,x,y);
            return;
        }
        if(!commands_open_&&(document_mode()||window_.content_type=="document_menu"))for(const auto &cell:content_trace_)if(!cell.target.empty()&&cell.rect.contains(x,y)) {
            const auto action=std::find_if(window_.actions.begin(),window_.actions.end(),[&](const auto &a){return a.id==cell.target&&a.enabled;});
            if(action!=window_.actions.end()){target_=action->id;send("window_action",out);return;}
        }
        if(commands_open_) {
            if(y>p.y+p.h-44){if(x>p.x+p.w/2)cancel(out);else handle(CursorNav::Confirm,out);return;}
            const RectPx list{p.x+16,p.y+54,p.w-32,std::max(36,p.h-112)};
            if(list.contains(x,y))command_cursor_=std::clamp(command_scroll_+(y-list.y-6)/40,0,std::max(0,int(window_.actions.size())-1));return;
        }
        if(window_.content_type!="settings_list_rogue"&&!parent_tab_navigation_active()&&!window_.actions.empty()&&commands_button(l).contains(x,y)){toggle_commands();return;}
        if(y>p.y+p.h-44){if(x>p.x+p.w/2&&!footer_without_cancel())cancel(out);else handle(CursorNav::Confirm,out);return;}
        const int bottom=dialog_controls_top(l);
        if(window_.content_type=="poker") {
            for(const auto &cell:content_navigation_)if(cell.rect.contains(x,y)&&!cell.target.empty()){focus_target_=cell.target;focus_steps_=0;details_focus_=false;advance(out);return;}
            const auto list=choice_list_rect(l);if(x>list.x+list.w)details_focus_=true;return;
        }
        if(window_.stage=="confirm") {
            // 確認の答えは、選択肢の窓を経由しない確認（navigation_page_ が真になる変愚・短愚の階段、Angband の引退など）でも押せる。
            // 先に下の選択肢・頁送りの枝へ入ると form_active() の return に阻まれて、答えのボタンに届かない。
            const int count=static_cast<int>(prompt_.choices.size());
            for(int i=0;i<count;++i)if(dialog_answer_rect(p,bottom,i,count).contains(x,y)){answer_=i;break;}
            return;
        }
        if(window_.stage=="choices"||window_.stage=="cursor"||navigation_page_) {
            if(form_active()) {
                // AH-62: 文字入力の窓（誕生の名前）でも、左の一覧の「操作」の行（ランダムな名前）はタップで押せる。
                if(window_.stage=="text"&&!pending_&&window_.input_ready) {
                    const auto list=choice_list_rect(l);
                    if(list.contains(x,y)&&!window_.rows.empty()) {
                        const int at=std::clamp(scroll_+(y-list.y-6)/choice_row_height(l),0,static_cast<int>(window_.rows.size())-1);
                        if(window_.rows[at].id.starts_with("@action:")&&window_.rows[at].available){target_=window_.rows[at].id.substr(8);send("window_action",out);}
                    }
                }
                return;
            }
            const auto list=choice_list_rect(l);
            if(list.contains(x,y)&&!window_.rows.empty()) {
                int selected=scroll_+(y-list.y-6)/choice_row_height(l);
                if(paired_allocation(window_)){
                    const int paired=paired_allocation_count(window_),visual_rows=paired/2;
                    selected=selected<visual_rows
                        ?selected*2+(x>=list.x+list.w/2?1:0)
                        :paired+(selected-visual_rows);
                }
                selected=std::clamp(selected,0,static_cast<int>(window_.rows.size())-1);
                if(paired_allocation(window_)&&window_.stage=="cursor") {
                    // Same as the arrow keys: the -/+ pair shares one native
                    // cursor position (the "-" row), and the core's Continue row
                    // is focused only for the Gensoband bonus screen.  Focusing
                    // the "+" row or Sil-Q/Angband's Continue row directly asks
                    // the core for a cursor it never reports.
                    const int paired=paired_allocation_count(window_);
                    cursor_=selected;if(selected<paired)selection_column_=selected%2;
                    //! Sil-Q は決定が「次に進む」なので、「−」「＋」の行は押した時点で値を変える（修-59）。
                    if(sil_allocation_window(window_)&&selected<paired){press_allocation(selected,out);details_focus_=false;return;}
                    if(selected<paired||window_.content_type=="stat_allocation") {
                        focus_target_=window_.rows[selected<paired?selected-selected%2:selected].id;focus_steps_=0;advance(out);
                    }
                    details_focus_=false;return;
                }
                if((window_.stage=="cursor"&&!window_.rows[selected].id.starts_with("@action:")&&window_.rows[selected].key!='*')||(!navigation_page_&&window_.stage=="choices"&&window_.cursor>=0)){focus_target_=window_.rows[selected].id;advance(out);}else{cursor_=selected;document_scroll_=0;}
                details_focus_=false;
            }else if(y>=list.y&&y<list.y+list.h)details_focus_=true;
            return;
        }
        if(y<bottom-56){if(!form_active()&&window_.stage!="cursor")details_focus_=true;return;}
        return;
    }
    if(y<p.y+60&&item_window()) {
        if(header_layout_.previous.contains(x,y)){tab(-1);return;}
        if(header_layout_.next.contains(x,y)){tab(1);return;}
        for(size_t i=0;i<header_layout_.tabs.size();++i)if(header_layout_.tabs[i].contains(x,y)) {
            const int current=window_.kind=="inventory"?inventory_category_:tabs_.index();
            const int at=int(i)+(window_.kind=="inventory"?inventory_header_first_:0);
            if(at!=current)tab(at-current);return;
        }
        return;
    }
    if(y>p.y+p.h-44){if(x>p.x+p.w/2&&!footer_without_cancel())cancel(out);else handle(CursorNav::Confirm,out);return;}
    if(operations_){click_operation(l,x,y);return;}
    if(window_.stage=="confirm"){auto box=input_rect(l);const int width=std::max(1,std::min(160,box.w/std::max(1,int(prompt_.choices.size()))));if(box.contains(x,y))answer_=std::clamp((x-box.x)/width,0,std::max(0,static_cast<int>(prompt_.choices.size())-1));return;}
    if(form_active())return;
    if(desc.contains(x,y)){details_focus_=true;return;}
    if(r.contains(x,y)&&(window_.stage=="browse"||window_.stage=="services")) {
        details_focus_=false;
        auto items=rows();cursor_=std::clamp(scroll_+(y-r.y)/item_row_height(l),0,std::max(0,static_cast<int>(items.size())-1));detail_scroll_=0;
        if(!items.empty()&&items[cursor_]->id.rfind("store:command:",0)==0)handle(CursorNav::Confirm,out);
    }
}
void ModernStoreUi::draw(UiPaint &p,TextOverlay &t,const UiLayout &l,const GameFrame &f,int theme) const {
    if(opening_placeholder()){content_trace_.clear();content_navigation_.clear();detail_image_rect_={};item_icons_.clear();return;} // 中身が届くまでは前の画（機能メニュー・マップ）を残す
    if(auto_backdrop_){auto_backdrop_->draw(p,t,l,f,theme);content_trace_=auto_backdrop_->content_trace_;return;}
    const UiProbeLayer probe_layer("window");
    MenuScaleScope scope(p,t,menu_font_scale(font_level_));const auto logical=menu_logical_layout(l,scope.scale,&t);
    context_anchor_lx_=int(context_anchor_x_/scope.scale);context_anchor_ly_=int(context_anchor_y_/scope.scale);
    draw_logical(p,t,logical,f,theme);
    if(ui_probe().enabled())probe_record(logical,scope.scale,f);
    // Opt-in trace for end-to-end input/geometry checks. It contains the
    // interpreted UI state and the exact rectangles used by this renderer.
    if(const char *path=std::getenv("HD2D_MENU_TRACE"))if(*path) {
        const auto rect=[](RectPx r){return nlohmann::json{{"x",r.x},{"y",r.y},{"w",r.w},{"h",r.h}};};
        nlohmann::json trace{{"active",active()},{"status",status()},{"kind",window_.kind},{"stage",window_.stage},{"type",window_.content_type},
            {"title",window_.title},{"revision",window_.revision},{"ready",window_.input_ready},{"pending",pending_},{"commands",commands_open_},
            {"cursor",cursor_},{"answer",answer_},{"command_cursor",command_cursor_},{"scroll",document_scroll_},{"max_scroll",document_max_scroll_},{"operations",operations_},
            {"details",details_focus_},{"font",font_level_},{"theme",theme},{"notice",notice_},{"popup",context_popup_active()},{"panel",rect(menu_scaled_rect(context_popup_active()?context_rect_:panel(logical),scope.scale))},
            {"input_rect",rect(menu_scaled_rect(text_edit_rect_logical(logical),scope.scale))},{"commands_button",rect(menu_scaled_rect(commands_button(logical),scope.scale))}};
        trace["targets"]=nlohmann::json::array();
        for(const auto &cell:content_trace_)trace["targets"].push_back({{"rect",rect(menu_scaled_rect(cell.rect,scope.scale))},{"text",cell.text},{"selected",cell.selected},{"target",cell.target}});
        const auto serialized=trace.dump();if(serialized!=trace_signature_) {std::ofstream output(path,std::ios::binary);output<<serialized;output.flush();if(output.good())trace_signature_=serialized;}
    }
}
void ModernStoreUi::probe_record(const UiLayout &l,float scale,const GameFrame &f) const {
    if(!active())return;
    auto &probe=ui_probe();
    const auto px=[scale](const RectPx &r){const auto s=menu_scaled_rect(r,scale);return ProbeRect{s.x,s.y,s.w,s.h};};
    const bool usable=window_.input_ready&&!pending_;
    ProbeWindow w;
    w.kind=window_.kind;w.title=window_.title;w.stage=commands_open_?"commands":window_.stage;w.type=window_.content_type;
    const auto items=rows();
    int index=0;
    for(const auto *row:items) {
        ProbeItem item;item.id=row->id;item.label=row->label;item.enabled=row->available;item.selected=index==cursor_;
        if(window_.content_type=="spells") {
            // 呪文の Lv・MP・失率は、説明の 1 行目に 3 つ空白で区切って並ぶ（"Lv 1   MP 2   失率 8%"）。
            const auto line=row->description.substr(0,row->description.find('\n'));
            size_t at=0;
            while(at<line.size()) {
                const auto end=line.find("   ",at);const auto part=line.substr(at,end==std::string::npos?end:end-at);
                if(!part.empty())item.cols.push_back(part);
                if(end==std::string::npos)break;
                at=end+3;
            }
        } else if(item_window()&&row->count>0) {
            // 店・持ち物の行：個数・重さ・値段（値段が未確定なら空）。
            item.cols={std::to_string(row->count),row->weight,row->price_known?std::to_string(row->price):std::string()};
        }
        w.items.push_back(std::move(item));++index;
    }
    w.selected=cursor_>=0&&cursor_<static_cast<int>(items.size())?cursor_:-1;
    w.prompt=bottom_message();w.revision=window_.revision;
    {const auto r=px(panel(l));w.has_rect=true;w.rect=r;}
    if(!notice_.empty())probe.notice(notice_);
    if(window_.stage=="number") {
        w.has_numeric=true;w.numeric_value=value(number_);w.numeric_min=numeric_.min;w.numeric_max=numeric_.max;
    }
    if(item_window()&&f.hud.gold>=0)w.gold=f.hud.gold;
    probe.window(std::move(w));
    // 行・操作・答え：描いたときの矩形（`content_trace_`）。クリックの判定が同じ矩形を使う。
    for(const auto &cell:content_trace_) {
        if(cell.target.empty()||cell.text.empty())continue;
        const char *kind="cell";
        bool enabled=usable;
        if(cell.target.rfind("answer:",0)==0)kind="answer";
        else if(cell.target.rfind("operation:",0)==0)kind="operation";
        else if(cell.target.rfind("store:command:",0)==0)kind="store-command";
        else {
            const auto row=std::find_if(window_.rows.begin(),window_.rows.end(),[&](const auto &r){return r.id==cell.target;});
            if(row!=window_.rows.end()){kind="row";enabled=enabled&&row->available;}
        }
        probe.button(cell.text,px(cell.rect),enabled,cell.selected,kind,false,cell.target);
    }
    if(context_popup_active())return; // 小さな文脈メニューは、行の外を押すと取消になるだけ（下の足の欄は無い）
    const auto p=panel(l);
    // 足の欄：左半分が決定・右半分が取消（`click_logical`。`y>p.y+p.h-44`）。
    // 取消が効かない窓（`no_cancel`）は、決定だけを全幅で出す（取消の部品を載せない）。押す側は `click_logical` が同じ判定を使う。
    if(footer_without_cancel())probe.button(tr("決定","Confirm"),px({p.x,p.y+p.h-43,p.w,43}),usable,false,"footer",false,"confirm");
    else {
        probe.button(tr("決定","Confirm"),px({p.x,p.y+p.h-43,p.w/2,43}),usable,false,"footer",false,"confirm");
        probe.button(tr("取消","Cancel"),px({p.x+p.w/2+1,p.y+p.h-43,p.w-p.w/2-1,43}),usable,false,"footer",false,"cancel");
    }
    if(window_.kind=="dialog"&&!commands_open_&&!operations_&&window_.content_type!="settings_list_rogue"
        &&!parent_tab_navigation_active()&&!window_.actions.empty())
        probe.button(tr("操作 (Start/F4)","Actions (Start/F4)"),px(commands_button(l)),usable,false,"actions",false,"actions");
}
void ModernStoreUi::draw_logical(UiPaint &p,TextOverlay &t,const UiLayout &l,const GameFrame &f,int theme) const {
    content_trace_.clear();content_navigation_.clear();detail_image_rect_={};item_icons_.clear();context_rect_={};
    const auto bounds=panel(l);const bool compact=bounds.h<360;
    message_height_=4*(t.cell_h()+2);
    prepare_popup(t,l);
    if(!active())return;painted_theme_=theme;venue_header_height_=0;if(context_popup_active()){draw_context_popup(p,t,l,theme);return;}if(window_.kind=="dialog"){draw_dialog(p,t,l,f,theme);return;}const auto box=panel(l),list=list_rect(l);auto desc=description_rect(l);const bool light=theme==1;
    const auto skin=modern_skin(theme);const auto edge=skin.border,accent=skin.selection;
    const auto fg=skin.text,dim=skin.muted;
    paint_modern_window(p,box,theme);
    auto text=[&](int x,int y,const std::string &s,int width,TextColor color){t.draw(x,y,s,color,std::max(1,width));};
    if(item_window()) {
        auto labels=tab_labels();int active_tab=tabs_.index();
        if(window_.kind=="inventory") {
            const auto [first,count]=menu_header_visible_range(t,box,labels,inventory_category_,inventory_header_first_);
            inventory_header_first_=first;labels=std::vector<std::string>(labels.begin()+first,labels.begin()+first+count);active_tab=inventory_category_-first;
        }
        // 命令の実行中に出し続ける一覧（NativeInventoryMenu の busy。入力を受けない）は、
        // 見た目を一覧のまま変えない（見出し・記号キーの案内が 1 フレームだけ変わって明滅する）。
        const bool inventory_hold=window_.kind=="inventory"&&!window_.input_ready;
        header_layout_=draw_menu_header(p,t,box,box.y+12,labels,active_tab,theme,(window_.stage=="browse"||inventory_hold)&&!pending_&&!operations_);
    } else text(box.x+16,box.y+18,window_.title,box.w-32,fg);
    const auto items=rows();
    const int row_height=item_row_height(l);const bool narrow_items=item_window()&&row_height>36;
    const std::string weight_heading=tr("重さ/個","Weight/ea");
    const std::string price_heading=tabs_.index()==1?tr("売却単価","Sale/ea"):tr("単価","Price/ea");
    const auto row_price=[&](const ModernRow &row) -> std::string {
        if(window_.kind=="home"||window_.kind=="museum"||window_.kind=="inventory")return std::string("—");
        if(row.tab==1 && !row.available)return tr("対象外","N/A");
        return row.price_known?std::to_string(row.price):tr("未確定","Unknown");
    };
    int weight_width=t.measure(weight_heading),price_width=t.measure(price_heading);
    for(const auto *row:items){weight_width=std::max(weight_width,t.measure(row->weight));price_width=std::max(price_width,t.measure(row_price(*row)));}
    weight_width=std::min(weight_width+8,std::max(1,list.w/5));
    price_width=std::min(price_width+8,std::max(1,list.w/5));
    if(window_.kind=="inventory")price_width=0;
    const int price_x=list.x+list.w-16-price_width,weight_x=price_x-16-weight_width;
    const auto right_text=[&](int x,int y,int width,const std::string &value,TextColor ink) {
        if(width<=0)return;
        std::string shown=value;
        if(t.measure(shown)>width) {
            const int dots=t.measure("...");
            shown=width>=dots?shown.substr(0,t.fit_bytes(shown,width-dots))+"...":"";
        }
        if(!shown.empty())text(x+width-t.measure(shown),y,shown,width,ink);
    };
    const int columns_y=box.y+(compact?44:50);
    if(f.hud.gold>=0)text(box.x+16,columns_y,tr("所持金: ","Gold: ")+std::to_string(f.hud.gold),narrow_items?box.w-32:(item_window()?std::max(1,weight_x-list.x-16):box.w-32),fg);
    if(item_window()&&!narrow_items) {
        right_text(weight_x,columns_y,weight_width,weight_heading,dim);
        right_text(price_x,columns_y,price_width,price_heading,dim);
    }
    paint_modern_inset(p,desc,theme);
    paint_modern_rule(p,{list.x,list.y-5,list.w,1},theme);
    paint_modern_rule(p,{list.x,list.y+list.h+4,list.w,1},theme);
    const int visible=std::max(1,list.h/row_height);scroll_=std::clamp(scroll_,0,std::max(0,static_cast<int>(items.size())-visible));
    if(cursor_<scroll_)scroll_=cursor_;if(cursor_>=scroll_+visible)scroll_=cursor_-visible+1;
    if(item_window())paint_modern_row_rules(p,list,row_height,theme);
    // The list frame is structural and must not disappear while a number,
    // text, or confirmation editor is active.
    p.frame(list,details_focus_?skin.border:skin.trim,2);
    if(!form_active()&&!operations_&&details_focus_)p.frame(desc,skin.trim,2);
    const auto popup=popup_rect(l);
    for(int i=scroll_;i<std::min<int>(items.size(),scroll_+visible);++i) {
        const auto &row=*items[i];const int y=list.y+(i-scroll_)*row_height;const bool picked=i==cursor_;
        if(row.id.rfind("store:command:",0)==0) {
            RectPx button{list.x+4,y+2,list.w-12,30};
            if(picked)paint_modern_selection(p,button,theme);else paint_modern_inset(p,button,theme);
            text(button.x+8,button.y+6,row.label,button.w-16,row.available?(picked?skin.selected:fg):dim);
            content_trace_.push_back({button,row.label,picked,row.id});continue;
        }
        if(item_window() && tabs_.index()==1 && i>0 && row.equipped!=items[i-1]->equipped)
            p.rect({list.x,y-3,list.w,3},skin.border);
        if(picked)paint_modern_selection(p,{list.x,y,list.w-8,row_height-4},theme);
        content_trace_.push_back({{list.x,y,list.w-8,row_height-4},row.label,picked,row.id});
        int width=list.w-24;if(operations_ && y<popup.y+popup.h && y+row_height-4>popup.y)width=std::max(0,popup.x-list.x-12);
        const auto ink=row.available?(picked?skin.selected:fg):dim;
        const std::string prefix=menu_key_prefix(keyboard_key(row));
        const bool has_prefix=menu_has_key_prefix(row.label,keyboard_key(row));
        if(width>0&&!has_prefix)text(list.x+8,y+8,prefix,std::min(width,40),ink);
        const int name_offset=item_window()?(row.count?78:8):(has_prefix?8:48);
        if(item_window()&&row.count>0&&width>=74) {
            const auto icon=row_icon_rect(l,i);
            p.rect(icon,theme==1?PaintColor{.96f,.92f,.82f,1}:PaintColor{.22f,.29f,.36f,.85f});
            p.frame(icon,{edge.r,edge.g,edge.b,.55f},1);
            item_icon(p,icon,row);
        }
        const int name_right=item_window()&&!narrow_items?std::min(weight_x-16,list.x+width+8):list.x+width+8;
        if(name_right>list.x+name_offset) {
            const auto label=(row.equipped?tr("[装備中] ","[Equipped] "):"")+row.label;
            if(narrow_items){const auto lines=wrap(t,label,name_right-list.x-name_offset);for(size_t n=0;n<std::min<size_t>(2,lines.size());++n)text(list.x+name_offset,y+5+int(n)*(t.cell_h()+2),lines[n],name_right-list.x-name_offset,ink);}
            else text(list.x+name_offset,y+8,label,name_right-list.x-name_offset,ink);
        }
        if(narrow_items&&row.count>0&&width>0) {
            const int half=std::max(1,width/2);text(list.x+8,y+row_height-23,row.weight,half,ink);
            if(window_.kind!="inventory"&&window_.kind!="home"&&window_.kind!="museum")right_text(list.x+8+half,y+row_height-23,half,tr("単価 ","Price ")+row_price(row),ink);
        } else if(item_window()&&row.count>0) {
            const auto uncovered=[&](int x,int w){return !operations_ || y+32<=popup.y || y>=popup.y+popup.h || x+w<=popup.x || x>=popup.x+popup.w;};
            if(uncovered(weight_x,weight_width))right_text(weight_x,y+8,weight_width,row.weight.empty()?"—":row.weight,ink);
            if(uncovered(price_x,price_width))right_text(price_x,y+8,price_width,row_price(row),ink);
        }
    }
    if(static_cast<int>(items.size())>visible) {
        const int h=std::max(12,list.h*visible/static_cast<int>(items.size()));const int y=list.y+(list.h-h)*scroll_/std::max(1,static_cast<int>(items.size())-visible);
        p.rect({list.x+list.w-5,y,4,h},edge);
    }
    if(items.empty())text(list.x+8,list.y+12,tr("ここにはアイテムがありません。","There are no items here."),list.w-20,dim);
    desc=draw_venue_header(p,t,desc,theme);
    const auto item=selected();
    if(item&&item_window()&&item->count==0) {
        text(desc.x+12,desc.y+12,item->label,desc.w-24,fg);
        // A command row may carry the core's own explanation (Angband's store
        // '?' help paragraph, AH-42).  Rows without one draw exactly as before.
        if(!item->description.empty()) {
            const auto lines=wrap(t,item->description,std::max(1,desc.w-24));
            int y=desc.y+12+t.cell_h()+10;
            for(const auto &line:lines){if(y+t.cell_h()>desc.y+desc.h-4)break;text(desc.x+12,y,line,desc.w-24,fg);content_trace_.push_back({{desc.x+12,y,desc.w-24,t.cell_h()},line,false,""});y+=t.cell_h()+4;}
        }
    } else if(item) {
        const bool short_pane=desc.h<88;
        const int icon_size=!item_window()?0:(short_pane?24:((desc.w<220||desc.h<160)?32:56));
        const int text_top=short_pane?8:icon_size+16;
        const int info_x=desc.x+(short_pane?icon_size+18:10),info_width=desc.w-(short_pane?icon_size+28:20);
        if(icon_size) {
            p.frame({desc.x+8,desc.y+8,icon_size,icon_size},edge,1);
            content_trace_.push_back({{desc.x+8,desc.y+8,icon_size,icon_size},"item image",false,""});
            item_icon(p,{desc.x+8,desc.y+8,icon_size,icon_size},*item);
        }
        int y=desc.y+10;
        std::string info=(window_.kind=="building"?tr("基本料金: ","Base fee: "):(item->tab==0?tr("購入単価: ","Unit price: "):tr("売却見積: ","Sale estimate: ")))
            +(item->price_known?std::to_string(item->price):tr("未確定","Unknown"))+"\n";
        if(window_.kind=="home"||window_.kind=="museum"||window_.kind=="inventory")info.clear();
        if(!item->weight.empty())info+=tr("重さ（1個）: ","Weight (each): ")+item->weight+" / "+std::to_string(item->count)+tr("個",item->count==1?" item":" items")+"\n";
        for(const auto &e:item->effects)info+=e+"\n";
        info+="\n"+item->description;
        if(window_.kind=="building") {
            info+=window_.body;
            // A facility action that only reports information (rumours,
            // rewards, unavailable destinations, and similar results) has no
            // input to collect.  Keep that result in the information pane;
            // the fixed footer is reserved for an active prompt or a
            // continued message that the player must acknowledge.
            if(window_.stage=="services"&&!recent_messages_.empty()) {
                if(!info.empty()&&info.back()!='\n')info+='\n';
                info+='\n';info+=recent_messages_.back();
            }
        }
        const auto lines=wrap(t,info,std::max(1,info_width));const int n=std::max(1,(desc.h-text_top-8)/(t.cell_h()+4));const int start=std::clamp(detail_scroll_,0,std::max(0,static_cast<int>(lines.size())-n));detail_scroll_=start;
        y=desc.y+text_top;for(int i=start;i<std::min<int>(lines.size(),start+n);++i){text(info_x,y,lines[i],info_width,fg);content_trace_.push_back({{info_x,y,info_width,t.cell_h()},lines[i],false,""});y+=t.cell_h()+4;}
        if(static_cast<int>(lines.size())>n) {
            const int track=std::max(1,desc.h-text_top-4),thumb=std::max(10,track*n/static_cast<int>(lines.size()));
            const int sy=desc.y+text_top+(track-thumb)*start/std::max(1,static_cast<int>(lines.size())-n);
            p.rect({desc.x+desc.w-5,sy,3,thumb},skin.trim);
        }
        if(details_focus_)p.frame(desc,skin.trim,2);
    }
    draw_operations(p,t,l,theme);
    const auto input=input_rect(l);paint_modern_rule(p,{box.x+16,input.y-message_height_-4,box.w-32,1},theme);
    draw_message(p,t,{input.x,input.y-message_height_,input.w,message_height_},theme);
    if(window_.stage=="number"){const int width=std::min(200,input.w);p.frame({input.x,input.y,width,32},accent,2);text(input.x+10,input.y+8,number_,width-20,fg);}
    if(window_.stage=="text")draw_edit_field(p,t,text_edit_rect_logical(l),text_line_,window_.input_caret,fg,skin.trim);
    if(window_.stage=="confirm")for(size_t i=0;i<prompt_.choices.size();++i) {
        const int width=std::max(1,std::min(160,input.w/std::max(1,int(prompt_.choices.size()))));
        RectPx a{input.x+static_cast<int>(i)*width,input.y,width-8,32};if(static_cast<int>(i)==answer_)paint_modern_selection(p,a,theme);
        // 画面の問い合わせの部品（probe_record が content_trace_ から作る）にも載せる。押す側（click_logical の store の確認）と同じ位置。
        content_trace_.push_back({a,prompt_.choices[i].label_utf8,static_cast<int>(i)==answer_,"answer:"+std::to_string(i)});
        text(a.x+8,a.y+8,menu_key_prefix(prompt_.choices[i].key)+prompt_.choices[i].label_utf8,a.w-16,static_cast<int>(i)==answer_?skin.selected:fg);
    }
    const bool inventory_hold=window_.kind=="inventory"&&!window_.input_ready;
    if(window_.stage=="browse"||inventory_hold)text(box.x+20,box.y+box.h-54,tabs_.index()==1
        ?tr("記号キー: 選択   /: 持ち物と装備の記号を切替","Letter keys: select   /: switch inventory/equipment keys")
        :tr("記号キー: 選択（カーソルのある範囲）","Letter keys: select in the cursor's range"),window_.venue_header.empty()?box.w-40:input.w,dim);
    text(box.x+20,box.y+box.h-30,pending_?tr("応答待ち","Waiting"):(details_focus_?tr("上下: 説明スクロール   Tab・L2: 一覧へ","Up/Down: scroll details   Tab/L2: list"):
        window_.stage=="message"&&!inventory_hold?tr("上下: メッセージ   決定: 続ける","Up/Down: message   Confirm: continue"):tr("決定: 選択 / 実行   取消: 戻る   左右: 選択   Tab・R2: 説明","Confirm: select / run   Cancel: back   Left/Right: choose   Tab/R2: details")),box.w-40,fg);
}
std::string ModernStoreUi::bottom_message() const {
    if(pending_)return tr("処理を待っています…","Waiting for the core...");
    if(!notice_.empty())return notice_;
    if(window_.stage=="message")return window_.input;
    if(window_.message_only)return retained_choices_?window_.body:std::string{};
    if(window_.stage=="number")return window_.input_label.empty()?numeric_.prompt_utf8:window_.input_label;
    if(window_.stage=="confirm")return window_.input.empty()?prompt_.text_utf8:window_.input;
    if(window_.stage=="text")return window_.input_label.empty()?tr("文字を入力してください。","Enter text."):window_.input_label;
    if(const auto item=selected()) {
        if(operations_&&!item->actions.empty()&&!item->actions[operation_].enabled)return item->actions[operation_].reason;
        if(!item->available)return item->reason;
    }
    if(!recent_messages_.empty()&&(window_.stage=="browse"||window_.stage=="services")&&window_.kind!="building") {
        std::string messages;for(const auto &line:recent_messages_){if(!messages.empty())messages+='\n';messages+=line;}return messages;
    }
    if(window_.kind=="inventory"&&window_.stage=="browse")return {};
    return window_.input;
}
void ModernStoreUi::draw_message(UiPaint &paint,TextOverlay &text,RectPx area,int theme) const {
    message_rect_=area;const int unit=text.cell_h()+2;
    const auto message=bottom_message();
    if(message!=message_signature_){message_signature_=message;message_scroll_=0;}
    if(area.h<text.cell_h()||message.empty()){message_max_scroll_=0;return;}
    const auto lines=wrap(text,message,std::max(1,area.w-8));const int count=std::max(1,area.h/unit);
    message_max_scroll_=std::max(0,int(lines.size())-count);message_scroll_=std::clamp(message_scroll_,0,message_max_scroll_);
    const auto skin=modern_skin(theme);
    for(int i=message_scroll_;i<std::min<int>(lines.size(),message_scroll_+count);++i) {
        const RectPx r{area.x,area.y+(i-message_scroll_)*unit,area.w-8,text.cell_h()};
        text.draw(r.x,r.y,lines[i],skin.text,r.w);content_trace_.push_back({r,lines[i],false,""});
    }
    if(message_max_scroll_){const int thumb=std::max(8,area.h*count/int(lines.size()));paint.rect({area.x+area.w-3,area.y+(area.h-thumb)*message_scroll_/message_max_scroll_,3,thumb},skin.trim);}
}
void ModernStoreUi::draw_content(UiPaint &paint,TextOverlay &text,RectPx area,const std::vector<ModernBlock> &content,int theme,bool follow) const {
    const auto skin=modern_skin(theme);
    if(content.size()>1&&content.front().kind=="table_header"&&std::all_of(content.begin()+1,content.end(),[](const auto &b){return b.kind=="table_row";})) {
        if(requested_section_>=0){document_scroll_=0;requested_section_=-1;}
        const int count=int(content.front().cells.size());if(count<2)return;
        const int unit=text.cell_h()+6,header_h=unit+14,room=std::max(1,area.w-24);
        std::vector<int> widths(count,48);int label_width=144;
        for(const auto &row:content)if(!row.cells.empty())label_width=std::max(label_width,text.measure(row.cells.front().text)+24);
        widths[0]=std::min(label_width,std::max(144,room*(window_.content_type=="browser"?68:38)/100));
        for(int c=1;c<count;++c)for(const auto &row:content)if(c<int(row.cells.size()))widths[c]=std::max(widths[c],text.measure(row.cells[c].text)+20);
        int minimum=0;for(int w:widths)minimum+=w;
        if(minimum<room){const int extra=(room-minimum)/(count-1);for(int c=1;c<count;++c)widths[c]+=extra;}
        int total_width=0;for(int w:widths)total_width+=w;
        std::vector<int> ys(content.size(),0),heights(content.size(),unit+14);
        int y=0;int focus_row=-1,focus_col=-1;std::string focus;
        for(size_t r=1;r<content.size();++r) {
            ys[r]=y;
            if(!content[r].cells.empty())heights[r]=std::max(unit+14,int(wrap(text,content[r].cells[0].text,widths[0]-20).size())*unit+14);
            y+=heights[r];
            for(size_t c=0;c<content[r].cells.size();++c)if(content[r].cells[c].selected){focus_row=int(r);focus_col=int(c);focus=std::to_string(r)+":"+std::to_string(c);}
        }
        const int visible=std::max(unit,area.h-header_h-12),maximum=(std::max(0,y-visible)+unit-1)/unit;
        document_scroll_=std::clamp(document_scroll_,0,maximum);
        document_x_=std::clamp(document_x_,0,std::max(0,total_width-room));
        if(follow&&focus_row>=0&&focus!=document_focus_) {
            const int top=document_scroll_*unit;
            if(ys[focus_row]<top)document_scroll_=ys[focus_row]/unit;
            if(ys[focus_row]+heights[focus_row]>top+visible)document_scroll_=std::min(maximum,(ys[focus_row]+heights[focus_row]-visible+unit-1)/unit);
            int x=0;for(int c=0;c<focus_col;++c)x+=widths[c];
            if(x<document_x_)document_x_=x;
            if(x+widths[focus_col]>document_x_+room)document_x_=x+widths[focus_col]-room;
        }
        document_max_scroll_=maximum;
        document_focus_=std::move(focus);
        const auto draw_row=[&](size_t r,int at,bool header) {
            int x=area.x+12-document_x_;const int top=area.y+(header?4:header_h+6),bottom=area.y+area.h-6;
            for(int c=0;c<std::min<int>(count,content[r].cells.size());++c) {
                const auto &value=content[r].cells[c];RectPx box{x,at,widths[c]-2,header?header_h-2:heights[r]-2};x+=widths[c];
                if(!value.target.empty())content_navigation_.push_back({box,value.label,value.selected,value.target});
                const int left=std::max(box.x,area.x+6),right=std::min(box.x+box.w,area.x+area.w-7);
                const int first=std::max(box.y,top),last=std::min(box.y+box.h,bottom);
                if(right<=left||last<=first)continue;
                RectPx clip{left,first,right-left,last-first};
                if(value.selected)paint_modern_selection(paint,clip,theme);
                else if(header)paint_modern_inset(paint,clip,theme);
                const auto ink=value.selected?skin.selected:(header?skin.muted:skin.text);
                const auto lines=wrap(text,value.text,std::max(1,box.w-16));int py=box.y+7;
                for(const auto &line:lines) {
                    if(py>=top+2&&py+text.cell_h()<=bottom) {
                        const int px=c==0?box.x+8:box.x+std::max(8,(box.w-text.measure(line))/2);
                        const auto skip=px<area.x+6?text.fit_bytes(line,area.x+6-px):0;
                        const int shown_x=px+text.measure(line.substr(0,skip));
                        if(shown_x>=area.x+5&&shown_x<right-3)text.draw(shown_x,py,line.substr(skip),ink,right-3-shown_x);
                    }
                    py+=unit;
                }
                content_trace_.push_back({clip,value.label.empty()?value.text:value.label+": "+value.text,value.selected,value.target});
            }
        };
        for(size_t r=1;r<content.size();++r)draw_row(r,area.y+header_h+6+ys[r]-document_scroll_*unit,false);
        draw_row(0,area.y+4,true);
        if(maximum>0){const int thumb=std::max(12,visible*visible/std::max(visible,y));paint.rect({area.x+area.w-4,area.y+header_h+6+(visible-thumb)*document_scroll_/maximum,3,thumb},skin.trim);}
        return;
    }

    const int unit=text.cell_h()+4,room=std::max(1,area.w-24);
    struct Cell {RectPx box;std::string label,target;std::vector<std::string> lines;bool selected{},follow{},heading{},inset{},code{},swatch{},colored_label{},horizontal{};int gutter{};PaintColor color{};};
    std::vector<Cell> layout;int y=8,section_y=-1,horizontal_width=room;std::string selection;RectPx selected_box;
    for(size_t b=0;b<content.size();++b) {
        if(int(b)==requested_section_)section_y=y;
        const auto &block=content[b];if(block.cells.empty())continue;
        if(block.kind=="stat_values") {
            size_t end=b+1;
            for(;end<content.size()&&content[end].kind=="stat_values"&&content[end].cells.size()==block.cells.size();++end) {
                bool same=true;for(size_t c=0;c<block.cells.size();++c)if(content[end].cells[c].label!=block.cells[c].label)same=false;
                if(!same)break;
            }
            if(end>b+1) {
                const int count=int(block.cells.size())+1;
                std::vector<int> widths(count,48);widths[0]=64;
                for(size_t r=b;r<end;++r) {
                    widths[0]=std::max(widths[0],std::min(room/3,text.measure(content[r].title)+16));
                    for(int c=1;c<count;++c)widths[c]=std::max(widths[c],text.measure(content[r].cells[c-1].text)+16);
                }
                int total=0;for(int width:widths)total+=width;
                if(total<room){const int extra=(room-total)/count;for(auto &width:widths)width+=extra;total+=extra*count;}
                horizontal_width=std::max(horizontal_width,total);
                const auto append_row=[&](const std::vector<std::string> &values,bool header) {
                    std::vector<Cell> cells;int height=unit+6,x=12;
                    for(int c=0;c<count;++c) {
                        Cell cell;cell.box={x,y,widths[c],0};cell.horizontal=true;cell.heading=header;
                        cell.lines=wrap(text,values[c],std::max(1,widths[c]-16));height=std::max(height,int(cell.lines.size())*unit+6);
                        cells.push_back(std::move(cell));x+=widths[c];
                    }
                    for(auto &cell:cells){cell.box.h=height;layout.push_back(std::move(cell));}y+=height+2;
                };
                std::vector<std::string> headers(1,"");for(const auto &cell:block.cells)headers.push_back(cell.label);append_row(headers,true);
                for(size_t r=b;r<end;++r){if(int(r)==requested_section_)section_y=y;std::vector<std::string> values(1,content[r].title);for(const auto &cell:content[r].cells)values.push_back(cell.text);append_row(values,false);}
                y+=4;b=end-1;continue;
            }
        }
        const bool prose=block.kind=="paragraph"||block.kind=="heading"||block.kind=="code"||block.kind=="swatch"||block.kind=="group_list";
        if(!block.title.empty()) {
            Cell title;title.box={12,y,room,unit+4};title.heading=true;title.lines={block.title};layout.push_back(std::move(title));y+=unit+6;
        }
        const bool table=block.kind=="table_header"||block.kind=="table_row"||block.kind=="stat_values";
        const bool fields=block.kind=="fields"||block.kind=="stat_values";
        int ideal=table?112:(block.kind=="grid"?184:236);
        if(fields){ideal=1;for(const auto &c:block.cells)ideal=std::max(ideal,text.measure(c.label+(c.label.empty()?"":": ")+c.text)+20);}
        // Hengband/Tangband's minimum-stat screen has seven fixed columns plus
        // a trailing probability paragraph. Because the paragraph follows the
        // table, this generic path lays the table out. Keep all seven cells on
        // the same visual row instead of wrapping the last "Maximum" cell.
        const bool fixed_stat_columns=window_.content_type=="stat_allocation"&&table&&block.cells.size()==7;
        const int columns=prose?1:(fixed_stat_columns?7:std::max(1,std::min<int>(block.cells.size(),room/ideal)));
        const int width=std::max(1,(room-(columns-1)*8)/columns);
        for(size_t first=0;first<block.cells.size();first+=columns) {
            std::vector<Cell> row;int height=unit+6;
            for(size_t c=first;c<std::min(block.cells.size(),first+columns);++c) {
                const auto &value=block.cells[c];Cell cell;cell.box={12+int(c-first)*(width+8),y,width,0};
                cell.label=value.label;cell.target=value.target;cell.code=block.kind=="code";cell.swatch=block.kind=="swatch";
                cell.gutter=cell.code?std::min(width/3,text.measure(cell.label)+18):0;
                if(cell.code) {
                    std::string_view rest=value.text;
                    while(!rest.empty()){size_t n=std::min(rest.size(),text.fit_bytes(rest,std::max(1,width-16-cell.gutter)));if(!n){n=1;while(n<rest.size()&&(static_cast<unsigned char>(rest[n])&0xc0)==0x80)++n;}cell.lines.emplace_back(rest.substr(0,n));rest.remove_prefix(n);}
                } else if(fields){cell.lines=wrap(text,value.label+(value.label.empty()?"":": ")+value.text,std::max(1,width-16));cell.label.clear();}
                else cell.lines=wrap(text,value.text,std::max(1,width-16));
                if(cell.swatch&&value.text.size()==7&&value.text.front()=='#')try{const auto color=std::stoul(value.text.substr(1),nullptr,16);cell.color={float((color>>16)&255)/255.f,float((color>>8)&255)/255.f,float(color&255)/255.f,1.f};}catch(...){}
                if(!value.color.empty())try{const auto color=std::stoul(value.color.substr(1),nullptr,16);cell.color={float((color>>16)&255)/255.f,float((color>>8)&255)/255.f,float(color&255)/255.f,1.f};cell.colored_label=true;}catch(...){}
                if(cell.lines.empty())cell.lines.push_back("");
                cell.selected=value.selected;cell.heading=block.kind=="heading"||block.kind=="table_header";
                cell.follow=!follow_target_.empty()&&value.target==follow_target_;
                cell.inset=false;
                height=std::max(height,6+int(cell.lines.size())*unit+(cell.label.empty()||cell.code?0:unit)+(cell.swatch?42:0));
                row.push_back(std::move(cell));
            }
            for(size_t i=0;i<row.size();++i) {
                auto &cell=row[i];cell.box.h=height;
                if(cell.selected||cell.follow){selected_box=cell.box;selection=std::to_string(b)+":"+std::to_string(first+i)+":"+cell.label;for(const auto &line:cell.lines)selection+=line;}
                layout.push_back(std::move(cell));
            }
            y+=height+2;
        }
        if(b+1<content.size()) {
            const auto &next=content[b+1];
            const bool section=!block.title.empty()||!next.title.empty()||block.kind!=next.kind||block.kind=="paragraph";
            if(section)y+=4;
        }
    }
    const int total=std::max(0,y+8-area.h),maximum=(total+unit-1)/unit;
    document_scroll_=std::clamp(document_scroll_,0,maximum);
    document_x_=std::clamp(document_x_,0,std::max(0,horizontal_width-room));
    if(section_y>=0){document_scroll_=std::clamp(section_y/unit,0,maximum);requested_section_=-1;}
    if(scroll_to_end_){document_scroll_=maximum;scroll_to_end_=false;} // メッセージ履歴：最新の行（本文の最後）まで送る（修-18）
    if(follow&&!selection.empty()&&selection!=document_focus_) {
        const int top=document_scroll_*unit;
        if(selected_box.y<top+8)document_scroll_=std::max(0,(selected_box.y-8)/unit);
        if(selected_box.y+selected_box.h>top+area.h-8)document_scroll_=std::min(maximum,(selected_box.y+selected_box.h-area.h+unit+7)/unit);
    }
    document_focus_=std::move(selection);
    document_max_scroll_=maximum;
    const int offset=document_scroll_*unit;
    for(const auto &cell:layout) {
        RectPx box{area.x+cell.box.x-(cell.horizontal?document_x_:0),area.y+cell.box.y-offset,cell.box.w,cell.box.h};
        if(!cell.target.empty()&&!cell.follow)content_navigation_.push_back({box,cell.label,cell.selected,cell.target});
        if(box.y+box.h<=area.y+4||box.y>=area.y+area.h-4)continue;
        const int left=std::max(box.x,area.x+4),right=std::min(box.x+box.w,area.x+area.w-4);
        if(right<=left)continue;
        RectPx clipped{left,std::max(box.y,area.y+4),right-left,std::min(box.y+box.h,area.y+area.h-4)-std::max(box.y,area.y+4)};
        std::string cell_text=cell.label;for(const auto &line:cell.lines){if(!cell_text.empty())cell_text+=' ';cell_text+=line;}
        content_trace_.push_back({clipped,std::move(cell_text),cell.selected,cell.target});
        if(cell.selected)paint_modern_selection(paint,clipped,theme);
        else if(cell.inset)paint_modern_inset(paint,clipped,theme);
        const auto ink=cell.selected?skin.selected:skin.text;int at=box.y+3;
        if(!cell.label.empty()) {
            if(at>=area.y+6&&at+text.cell_h()<=area.y+area.h-6) {
                auto label_ink=cell.selected?skin.selected:skin.muted;
                if(cell.colored_label){
                    const float light=.2126f*cell.color.r+.7152f*cell.color.g+.0722f*cell.color.b;
                    paint.rect({box.x+6,at-2,std::min(box.w-12,text.measure(cell.label)+8),text.cell_h()+4},light<.2f?PaintColor{.88f,.9f,.92f,1}:PaintColor{.025f,.04f,.065f,1});
                    label_ink={cell.color.r,cell.color.g,cell.color.b,1};
                }
                text.draw(box.x+8,at,cell.label,label_ink,box.w-16);
            }
            if(!cell.code)at+=unit;
        }
        for(const auto &line:cell.lines) {
            if(at>=area.y+6&&at+text.cell_h()<=area.y+area.h-6) {
                int x=box.x+8+cell.gutter;size_t skip=0;
                if(cell.horizontal)while(skip<line.size()&&x<area.x+6) {
                    size_t next=skip+1;while(next<line.size()&&(static_cast<unsigned char>(line[next])&0xc0)==0x80)++next;
                    x+=text.measure(line.substr(skip,next-skip));skip=next;
                }
                if(x>=area.x+4&&x<right-4)text.draw(x,at,line.substr(skip),ink,right-4-x);
            }
            at+=unit;
        }
        if(cell.swatch&&at>=area.y+6&&at+32<=area.y+area.h-6){paint.rect({box.x+8,at,box.w-16,32},cell.color);paint.frame({box.x+8,at,box.w-16,32},skin.border,1);}
        if(cell.heading&&clipped.h==box.h)paint_modern_rule(paint,{clipped.x+4,box.y+box.h-3,std::max(1,clipped.w-8),1},theme);
    }
    if(maximum>0) {
        const int thumb=std::max(10,area.h*area.h/std::max(area.h,y));
        paint.rect({area.x+area.w-5,area.y+(area.h-thumb)*document_scroll_/maximum,3,thumb},skin.trim);
    }
    if(horizontal_width>room){const int thumb=std::max(12,room*room/horizontal_width);paint.rect({area.x+12+(room-thumb)*document_x_/(horizontal_width-room),area.y+area.h-3,thumb,2},skin.trim);}
}

RectPx ModernStoreUi::commands_button(const UiLayout &l) const {
    const auto box=panel(l);const int width=std::min(196,std::max(96,box.w/3));return {box.x+box.w-width-16,box.y+10,width,32};
}
void ModernStoreUi::draw_commands(UiPaint &paint,TextOverlay &text,const UiLayout &l,int theme) const {
    const auto box=panel(l);const auto skin=modern_skin(theme);paint_modern_window(paint,box,theme);
    text.draw(box.x+20,box.y+18,window_.title+tr(" / 操作"," / Actions"),skin.text,box.w-40);
    const RectPx list{box.x+16,box.y+54,box.w-32,std::max(36,box.h-112)};
    paint_modern_inset(paint,list,theme);const int visible=std::max(1,(list.h-12)/40),count=int(window_.actions.size());
    command_scroll_=std::clamp(command_scroll_,0,std::max(0,count-visible));
    if(command_cursor_<command_scroll_)command_scroll_=command_cursor_;
    if(command_cursor_>=command_scroll_+visible)command_scroll_=command_cursor_-visible+1;
    for(int i=command_scroll_;i<std::min(count,command_scroll_+visible);++i) {
        const auto &action=window_.actions[i];RectPx row{list.x+6,list.y+6+(i-command_scroll_)*40,list.w-12,36};
        content_trace_.push_back({row,action.label,i==command_cursor_,action.id});
        if(i==command_cursor_)paint_modern_selection(paint,row,theme);
        text.draw(row.x+10,row.y+9,menu_label_with_key(action.key,action.label),!action.enabled?skin.muted:(i==command_cursor_?skin.selected:skin.text),row.w-20);
    }
    text.draw(box.x+20,box.y+box.h-32,tr("決定: 実行   取消: 元の画面に戻る","Confirm: run   Cancel: back to the screen"),skin.text,box.w-40);
}

void ModernStoreUi::draw_poker(UiPaint &p,TextOverlay &t,const UiLayout &l,int theme) const {
    const auto box=panel(l),left=choice_list_rect(l);const auto skin=modern_skin(theme);
    const RectPx right{left.x+left.w+16,left.y,box.x+box.w-16-left.x-left.w-16,left.h};
    paint_modern_window(p,box,theme);paint_modern_inset(p,left,theme);paint_modern_inset(p,right,theme);
    p.frame(details_focus_?right:left,skin.trim,2);t.draw(box.x+20,box.y+18,window_.title,skin.text,box.w-40);
    if(window_.content.empty()||window_.rows.size()!=6)return;
    const auto &cards=window_.content.front().cells;
    const int unit=t.cell_h()+2,card_w=14*t.cell_w()+12,gap=12,card_h=7*unit+18;
    const int total=int(cards.size())*(card_w+gap)-gap,visible=std::max(1,left.w-16);
    const int vertical_max=std::max(0,card_h+2*unit+48-left.h);
    group_scroll_=cursor_==5?vertical_max:0;
    if(cursor_<5) {
        const int x=cursor_*(card_w+gap);
        if(x<group_x_)group_x_=x;
        if(x+card_w>group_x_+visible)group_x_=x+card_w-visible;
    }
    group_x_=std::clamp(group_x_,0,std::max(0,total-visible));
    const auto clipped=[&](RectPx r){const int x=std::max(r.x,left.x+4),y=std::max(r.y,left.y+4),end=std::min(r.x+r.w,left.x+left.w-4),bottom=std::min(r.y+r.h,left.y+left.h-4);return RectPx{x,y,std::max(0,end-x),std::max(0,bottom-y)};};
    for(size_t i=0;i<cards.size();++i) {
        const auto &card=cards[i];const RectPx bounds{left.x+8+int(i)*(card_w+gap)-group_x_,left.y+8-group_scroll_,card_w,card_h+unit+8};
        const auto hit=clipped(bounds);if(hit.w<=0||hit.h<=0)continue;
        content_navigation_.push_back({hit,card.label,int(i)==cursor_,card.target});content_trace_.push_back(content_navigation_.back());
        if(int(i)==cursor_)paint_modern_selection(p,hit,theme);
        const auto face=clipped({bounds.x+2,bounds.y+2,card_w-4,card_h-4});
        p.rect(face,{.035f,.04f,.055f,1});p.frame(face,int(i)==cursor_?skin.trim:skin.border,2);
        TextColor ink{.9f,.9f,.9f,1};
        if(card.color.size()==7)try{const auto color=std::stoul(card.color.substr(1),nullptr,16);ink={float((color>>16)&255)/255.f,float((color>>8)&255)/255.f,float(color&255)/255.f,1};}catch(...){}
        size_t at=0;int row=0;
        while(at<=card.text.size()) {
            const auto end=card.text.find('\n',at);const auto line=card.text.substr(at,end==std::string::npos?end:end-at);
            int x=bounds.x+8;const int y=bounds.y+8+row*unit;
            const auto skip=x<left.x+6?t.fit_bytes(line,left.x+6-x):0;x+=t.measure(line.substr(0,skip));
            if(x>=left.x+4&&x<left.x+left.w-4&&y>=left.y+4&&y+t.cell_h()<=left.y+left.h-4)t.draw(x,y,line.substr(skip),ink,std::min(bounds.x+card_w-6,left.x+left.w-6)-x);
            if(end==std::string::npos)break;at=end+1;++row;
        }
        const int y=bounds.y+card_h+2;if(bounds.x>=left.x+4&&bounds.x<left.x+left.w-4&&y+t.cell_h()<=left.y+left.h-4)
            t.draw(bounds.x+4,y,std::to_string(i+1)+": "+card.label,int(i)==cursor_?skin.selected:skin.text,std::min(card_w-8,left.x+left.w-8-bounds.x));
    }
    const RectPx done{left.x+8,left.y+card_h+unit+32-group_scroll_,std::max(1,left.w-16),unit+12};
    if(done.y+done.h<=left.y+left.h-4){if(cursor_==5)paint_modern_selection(p,done,theme);t.draw(done.x+8,done.y+5,window_.rows.back().label,cursor_==5?skin.selected:skin.text,done.w-16);content_navigation_.push_back({done,window_.rows.back().label,cursor_==5,window_.rows.back().id});content_trace_.push_back(content_navigation_.back());}
    if(total>visible){const int thumb=std::max(12,visible*visible/total);p.rect({left.x+8+(visible-thumb)*group_x_/(total-visible),left.y+left.h-4,thumb,2},skin.trim);}
    if(vertical_max){const int thumb=std::max(12,left.h*left.h/(left.h+vertical_max));p.rect({left.x+left.w-4,left.y+(left.h-thumb)*group_scroll_/vertical_max,2,thumb},skin.trim);}
    draw_content(p,t,right,std::vector<ModernBlock>(window_.content.begin()+1,window_.content.end()),theme,false);
    t.draw(box.x+20,box.y+box.h-30,tr("上下: 選択   左右: 一覧・情報   決定: 実行","Up/Down: select   Left/Right: cards / information   Confirm: select"),skin.text,box.w-40);
}
void ModernStoreUi::draw_dialog(UiPaint &p,TextOverlay &t,const UiLayout &l,const GameFrame &f,int theme) const {
    if(commands_open_){draw_commands(p,t,l,theme);return;}
    if(window_.content_type=="poker"){draw_poker(p,t,l,theme);return;}
    if(window_.stage=="choices"||(!window_.capture_keys&&window_.content_type!="map"&&!form_active()&&((window_.stage=="cursor"&&!window_.rows.empty())||navigation_page_))){draw_choices(p,t,l,theme);return;}
    const auto box=panel(l);const bool light=theme==1;
    const auto skin=modern_skin(theme);const auto edge=skin.border,accent=skin.selection;
    const auto fg=skin.text;
    const bool backdrop=retained_choices_||(navigation_page_&&window_.content_type!="map");
    if(backdrop)draw_choices(p,t,l,theme,true);else paint_modern_window(p,box,theme);
    t.draw(box.x+20,box.y+18,window_.title,fg,box.w-(window_.actions.empty()?40:commands_button(l).w+40));
    if(!window_.actions.empty()){const auto button=commands_button(l);paint_modern_inset(p,button,theme);t.draw(button.x+8,button.y+8,tr("操作 (Start/F4)","Actions (Start/F4)"),fg,button.w-16);}
    if(!window_.subtitle.empty())t.draw(box.x+20,box.y+40,window_.subtitle,skin.muted,box.w-40);
    const bool form=form_active();
    const int content_top=box.y+(window_.subtitle.empty()?54:76);
    const int content_bottom=dialog_controls_top(l)-message_height_-8;
    const RectPx body{box.x+16,content_top,box.w-32,std::max(24,content_bottom-content_top)};
    if(!backdrop){paint_modern_inset(p,body,theme);if(details_focus_)p.frame(body,skin.trim,2);}
    if(backdrop) {
        // Live input remains owned by the child prompt. Only its list backdrop is retained.
    } else if(window_.content_type=="browser"&&!window_.content.empty()&&window_.content.front().kind=="group_list") {
        const int group_width=std::clamp(body.w*30/100,130,320);
        const RectPx groups{body.x,body.y,group_width,body.h},items{body.x+group_width+12,body.y,std::max(1,body.w-group_width-12),body.h};
        const int item_scroll=document_scroll_,item_x=document_x_,item_max=document_max_scroll_;const auto item_focus=document_focus_;
        document_scroll_=group_scroll_;document_x_=group_x_;document_max_scroll_=group_max_scroll_;document_focus_=group_focus_;
        paint_modern_inset(p,groups,theme);draw_content(p,t,groups,{window_.content.front()},theme,!details_focus_);
        group_scroll_=document_scroll_;group_x_=document_x_;group_max_scroll_=document_max_scroll_;group_focus_=document_focus_;
        document_scroll_=item_scroll;document_x_=item_x;document_max_scroll_=item_max;document_focus_=item_focus;
        paint_modern_inset(p,items,theme);draw_content(p,t,items,std::vector<ModernBlock>(window_.content.begin()+1,window_.content.end()),theme,!details_focus_);
    } else if(window_.content_type=="editor") {
        const RectPx document{body.x,body.y,(body.w-16)*55/100,body.h};
        const RectPx information{document.x+document.w+16,body.y,body.w-document.w-16,body.h};
        const int saved_scroll=document_scroll_,saved_x=document_x_,saved_max=document_max_scroll_;const auto saved_focus=document_focus_;
        document_scroll_=group_scroll_;document_x_=group_x_;document_max_scroll_=group_max_scroll_;document_focus_=group_focus_;
        paint_modern_inset(p,document,theme);draw_content(p,t,document,window_.content,theme,true);
        group_scroll_=document_scroll_;group_x_=document_x_;group_max_scroll_=document_max_scroll_;group_focus_=document_focus_;
        document_scroll_=saved_scroll;document_x_=saved_x;document_max_scroll_=saved_max;document_focus_=saved_focus;
        ModernBlock current;current.kind="paragraph";current.title=window_.input_label;current.cells.push_back({"",window_.input,false});
        ModernBlock actions;actions.kind="fields";for(const auto &a:window_.actions)actions.cells.push_back({menu_key_prefix(a.key),a.label,false});
        paint_modern_inset(p,information,theme);draw_content(p,t,information,{current,actions},theme,false);
    } else if(window_.content_type=="map") {
        if(f.minimap.valid()) {
            const RectPx map_area{body.x,body.y,body.w*65/100,body.h},information{body.x+body.w*65/100+12,body.y,body.w-body.w*65/100-12,body.h};
            const auto grid=draw_modern_map(p,t,map_area,f.minimap,theme,i18n::current()=="ja");
            content_trace_.push_back({grid,tr("探索済みの地図","Explored map"),false,""});
            ModernBlock depth;depth.kind="fields";depth.cells.push_back({tr("階層","Depth"),std::to_string(f.hud.depth),false});
            draw_content(p,t,information,{depth},theme,false);
        } else {
            draw_content(p,t,body,window_.content,theme,!details_focus_);
        }
    } else draw_content(p,t,body,window_.content,theme,(window_.stage=="cursor"||window_.content_type=="editor")&&!details_focus_);
    const int controls=dialog_controls_top(l);
    draw_message(p,t,{box.x+20,controls-message_height_,box.w-40,message_height_},theme);
    if(window_.stage=="choices"||window_.stage=="confirm") {
        const bool confirm=window_.stage=="confirm";const int count=confirm?static_cast<int>(prompt_.choices.size()):static_cast<int>(window_.rows.size());
        const int selected=confirm?answer_:cursor_;
        paint_modern_rule(p,{box.x+16,controls-5,box.w-32,1},theme);
        for(int i=0;i<count;++i) {
            const RectPx a=dialog_answer_rect(box,controls,i,count);
            content_trace_.push_back({a,confirm?prompt_.choices[i].label_utf8:window_.rows[i].label,i==selected,"answer:"+std::to_string(i)});if(i==selected)paint_modern_selection(p,a,theme);
            const int key=confirm?prompt_.choices[i].key:window_.rows[i].hotkey;
            auto label=confirm?prompt_.choices[i].label_utf8:window_.rows[i].label;const auto prefix=menu_key_prefix(key);
            label=menu_label_with_key(key,label);
            t.draw(a.x+8,controls+8,label,i==selected?skin.selected:fg,a.w+4-16);
        }
    } else if(window_.stage=="number") {
        p.frame({box.x+20,controls,box.w-40,38},accent,2);t.draw(box.x+30,controls+10,number_,fg,box.w-60);
    } else if(window_.stage=="text") {
        draw_edit_field(p,t,text_edit_rect_logical(l),text_line_,window_.input_caret,fg,skin.trim);
    } else if(window_.content_type!="map") {
        t.draw(box.x+20,controls+10,window_.stage=="cursor"?tr("方向: 移動   決定: 選択","Directions: move   Confirm: select"):tr("上下: スクロール   決定: 続ける","Up/Down: scroll   Confirm: continue"),fg,box.w-40);
    }
    std::string guide=window_.content_type=="map"?tr("決定・取消: 戻る","Confirm / Cancel: back"):details_focus_?tr("方向: 本文スクロール   Tab・L2: 操作へ","Directions: scroll content   Tab/L2: controls"):tr("決定: 実行   取消: 戻る   Tab・R2: 本文","Confirm: run   Cancel: back   Tab/R2: content");
    // 取消が効かない窓（`no_cancel`）は、ここでも「取消: 戻る」を出さない（選ぶ画面の案内と同じ。Umoria の名前の入力）。
    if(window_.no_cancel&&window_.content_type!="map"){const char *cut=tr("   取消: 戻る","   Cancel: back");const auto at=guide.find(cut);if(at!=std::string::npos)guide.erase(at,std::strlen(cut));}
    t.draw(box.x+20,box.y+box.h-30,guide,fg,box.w-40);
}

RectPx ModernStoreUi::choice_list_rect(const UiLayout &layout) const {
    auto box=panel(layout);const int extra=window_.subtitle.empty()?0:(box.h<360?10:22),gap=box.h<360?4:8;
    const int top=box.y+54+extra,bottom=input_rect(layout).y-message_height_-gap;
    const int share=paired_allocation(window_)?46:(birth_layout_?34:55);
    return {box.x+16,top,(box.w-48)*share/100,std::max(36,bottom-top)};
}
RectPx ModernStoreUi::draw_venue_header(UiPaint &paint,TextOverlay &text,RectPx area,int theme) const {
    venue_header_height_=0;if(window_.venue_header.empty())return area;
    const auto skin=modern_skin(theme);const auto lines=wrap(text,window_.venue_header,std::max(1,area.w-24));
    const int unit=text.cell_h()+4;
    venue_header_height_=int(lines.size())*unit+20;
    int y=area.y+8;
    for(const auto &line:lines) {
        text.draw(area.x+12,y,line,skin.text,area.w-24);
        content_trace_.push_back({{area.x+12,y,area.w-24,text.cell_h()},line,false,""});y+=unit;
    }
    paint_modern_rule(paint,{area.x+8,area.y+venue_header_height_-6,area.w-16,1},theme);
    area.y+=venue_header_height_;area.h=std::max(0,area.h-venue_header_height_);return area;
}
void ModernStoreUi::draw_choices(UiPaint &paint,TextOverlay &text,const UiLayout &layout,int theme,bool backdrop) const {
    auto box=panel(layout),list=choice_list_rect(layout);const auto skin=modern_skin(theme);
    RectPx details{list.x+list.w+16,list.y,box.x+box.w-16-(list.x+list.w+16),list.h};
    paint_modern_window(paint,box,theme);paint_modern_inset(paint,list,theme);paint_modern_inset(paint,details,theme);
    details=draw_venue_header(paint,text,details,theme);
    const bool parent_tabs=parent_tab_navigation_active();
    if(parent_tabs) {
        auto labels=parent_categories_;
        const auto [first,count]=menu_header_visible_range(text,box,labels,parent_category_,parent_header_first_);
        parent_header_first_=first;
        labels=std::vector<std::string>(labels.begin()+first,labels.begin()+first+count);
        header_layout_=draw_menu_header(paint,text,box,box.y+12,labels,parent_category_-first,theme,!pending_&&!operations_);
    } else {
        const bool show_action_button=!window_.actions.empty()&&window_.content_type!="settings_list_rogue";
        text.draw(box.x+20,box.y+18,window_.title.empty()?tr("メニュー","Menu"):window_.title,skin.text,box.w-(show_action_button?commands_button(layout).w+40:40));
        if(show_action_button){const auto button=commands_button(layout);paint_modern_inset(paint,button,theme);text.draw(button.x+8,button.y+8,tr("操作 (Start/F4)","Actions (Start/F4)"),skin.text,button.w-16);}
    }
    if(!window_.subtitle.empty())text.draw(box.x+20,box.y+40,window_.subtitle,skin.muted,box.w-40);
    const bool paired=paired_allocation(window_);
    const bool item_list=window_.content_type=="items";
    const int row_height=choice_row_height(layout);
    const int visible=std::max(1,(list.h-12)/row_height),count=static_cast<int>(window_.rows.size());
    const int paired_count=paired?paired_allocation_count(window_):0;
    const auto visual_row=[&](int i){return paired?(i<paired_count?i/2:paired_count/2+(i-paired_count)):i;};
    const int visual_count=paired?paired_count/2+(count-paired_count):count,visual_cursor=visual_row(cursor_);
    scroll_=std::clamp(scroll_,0,std::max(0,visual_count-visible));if(visual_cursor<scroll_)scroll_=visual_cursor;if(visual_cursor>=scroll_+visible)scroll_=visual_cursor-visible+1;
    if(window_.content_type=="items"||window_.content_type=="start")paint_modern_row_rules(paint,list,row_height,theme);
    if(!backdrop&&window_.content_type!="start")paint.frame(details_focus_?details:list,skin.trim,2);
    for(int i=0;i<count;++i){
        const int line=visual_row(i);if(line<scroll_||line>=scroll_+visible)continue;
        const auto &row=window_.rows[i];int y=list.y+6+(line-scroll_)*row_height;
        const bool half=paired&&i<paired_count;
        const int x=list.x+(half?(i%2)*(list.w/2):0),width=half?list.w/2:list.w;
        content_trace_.push_back({{x+4,y-2,width-8,row_height-2},row.label,i==cursor_,row.id});
        if(i==cursor_)paint_modern_selection(paint,{x+4,y-2,width-8,row_height-2},theme);
        auto color=!row.available?skin.muted:(i==cursor_?skin.selected:skin.text);
        if(item_list&&row.count>0) {
            const auto icon=row_icon_rect(layout,i);paint.frame(icon,skin.border,1);
            item_icon(paint,icon,row);
            // 記号キーの接頭辞（`a) `・`^A) `）は 32px の欄に収まる 2 字までのキーだけ。決定キー（Enter）などの長い名前は欄に入らず
            // `Ente` と切れて各行の頭に出る（村長の鑑定の窓。観-6(b)）ので、描かない。
            if(menu_key_label(row.hotkey).size()<=2)text.draw(x+8,y+4,menu_key_prefix(row.hotkey),color,32);
            if(row_height>36) {
                const auto lines=wrap(text,row.label,std::max(1,width-92));for(size_t n=0;n<std::min<size_t>(2,lines.size());++n)text.draw(x+80,y+4+int(n)*(text.cell_h()+2),lines[n],color,std::max(1,width-92));
                text.draw(x+8,y+row_height-24,row.weight,color,width-24);
            } else {
                const int weight_width=std::min(std::max(48,text.measure(row.weight)+8),width/4);
                text.draw(x+80,y+4,row.label,color,std::max(1,width-96-weight_width));
                text.draw(x+width-weight_width-8,y+4,row.weight,color,weight_width);
            }
        } else text.draw(x+10,y+(window_.content_type=="start"?7:4),menu_label_with_key(row.hotkey,row.label),color,width-20);
    }
    if(visual_count>visible){int thumb=std::max(14,list.h*visible/visual_count);int pos=(list.h-thumb)*scroll_/std::max(1,visual_count-visible);paint.rect({list.x+list.w-5,list.y+pos,3,thumb},skin.trim);}
    const ModernRow *chosen=cursor_>=0&&cursor_<count?&window_.rows[cursor_]:nullptr;
    if(chosen&&item_list&&chosen->count==0) {
        if(!chosen->description.empty()){ModernBlock block;block.kind="paragraph";block.cells.push_back({"",chosen->description,false});draw_content(paint,text,details,{block},theme,false);}
    } else if(chosen&&item_list) {
        detail_image_rect_={details.x+12,details.y+10,40,40};paint.frame(detail_image_rect_,skin.border,1);
        item_icon(paint,detail_image_rect_,*chosen);
        std::vector<ModernBlock> content;
        ModernBlock values;values.kind="fields";values.cells={{tr("数量","Count"),std::to_string(chosen->count),false},{tr("重さ（1個）","Weight (each)"),chosen->weight,false}};content.push_back(std::move(values));
        for(const auto &effect:chosen->effects){ModernBlock b;b.kind="paragraph";b.cells.push_back({"",effect,false});content.push_back(std::move(b));}
        ModernBlock description;description.kind="paragraph";description.cells.push_back({"",chosen->description,false});content.push_back(std::move(description));
        if(!chosen->available&&!chosen->reason.empty()){ModernBlock b;b.kind="paragraph";b.cells.push_back({"",chosen->reason,false});content.push_back(std::move(b));}
        details.y+=56;details.h=std::max(1,details.h-56);draw_content(paint,text,details,content,theme,false);
    } else if(chosen&&!paired_allocation(window_)&&(!chosen->description.empty()||(!chosen->available&&!chosen->reason.empty()))) {
        ModernBlock block;block.kind="paragraph";block.cells.push_back({"",!chosen->available&&!chosen->reason.empty()?chosen->reason:chosen->description,false});
        draw_content(paint,text,details,{block},theme,false);
    } else {
        auto content=window_.content;
        // 設定の一覧: 右の欄（原作の画面を並べた本文）は、左の一覧で選んでいる項目が見える所まで追いかける（強調はしない。
        // 左の一覧の選択が正で、ここは位置を合わせるだけ。遊ぶ人が Tab で右の欄へ移っている間は動かさない）。
        // 項目が多く、右の欄に収まらない一覧（Frox のオプションなど）で、選んだ項目の値が見えなくなるのを防ぐ。
        const bool follow_settings=window_.content_type=="settings_list"&&window_.stage=="cursor"&&!window_.cursor_target.empty()&&!details_focus_;
        if(window_.stage=="cursor")for(auto &block:content)for(auto &cell:block.cells){cell.selected=false;if(!follow_settings||cell.target!=window_.cursor_target)cell.target.clear();}
        follow_target_=follow_settings?window_.cursor_target:std::string();
        draw_content(paint,text,details,content,theme,follow_settings);
        follow_target_.clear();
    }
    if(details_focus_)paint.frame(details,skin.trim,2);
    draw_operations(paint,text,layout,theme);
    if(backdrop)return;
    if(message_height_)draw_message(paint,text,{box.x+20,input_rect(layout).y-message_height_,box.w-40,message_height_},theme);
    const bool help=chosen&&std::any_of(chosen->actions.begin(),chosen->actions.end(),[](const auto &a){return a.id=="help"&&a.enabled;});
    const char *guide=help?tr("決定: 選択   取消: 戻る   X/F3: ヘルプ   Tab・R2: 詳細","Confirm: select   Cancel: back   X/F3: help   Tab/R2: details"):tr("決定: 実行   取消: 戻る   Tab・R2: 詳細","Confirm: run   Cancel: back   Tab/R2: details");
    if(window_.content_type=="settings_list"||window_.content_type=="settings_list_rogue")guide=chosen&&chosen->id=="@action:accept-birth-options"
        ?tr("決定: この内容で次へ進む   上下: 項目を選ぶ   取消: 戻る","Confirm: continue with these options   Up/Down: select   Cancel: back")
        :tr("左右: 値を変更   上下: 項目を選ぶ   取消: 戻る","Left/Right: change value   Up/Down: select   Cancel: back");
    if(sil_allocation_window(window_))guide=tr("左右: 減らす・増やす   上下: 項目を選ぶ   決定: 次に進む   取消: 戻る","Left/Right: decrease/increase   Up/Down: select   Confirm: continue   Cancel: back");
    if(window_.content_type=="settings_list_rogue"&&!parent_tabs)guide=tr("決定: 切り替え・入力   上下: 項目を選ぶ   取消: 戻る","Confirm: toggle/edit   Up/Down: select   Cancel: back");
    if(window_.content_type=="settings_list_rogue"&&parent_tabs)guide=tr("決定: 切り替え・入力   上下: 項目を選ぶ   Shift・L1/R1: タブ   取消: 戻る","Confirm: toggle/edit   Up/Down: select   Shift/L1/R1: tabs   Cancel: back");
    /*
     * タイトル（`content_type=="start"`）の取消は、コアを終わらせて画面ごと終わる（タイトルへは戻らない）。
     * だから案内は「戻る」ではなく「終了」と書く。「読み込む」「削除」の窓（`start` ではない）は、タイトルへ戻るので「戻る」のまま。
     */
    if(window_.content_type=="start")guide=tr("決定: 実行   取消: 終了   Tab・R2: 詳細","Confirm: run   Cancel: quit   Tab/R2: details");
    /*
     * 取消が効かない窓（`no_cancel`。Umoria のキャラ作成）では「取消: 戻る」を出さない
     * （決めたこと 2026-10-03「戻れないなら画面下部に戻るという言葉を出さないようにしよう」）。
     * タイトルの種類で `no_cancel` の窓（Umoria のセーブを読み込む途中のスプラッシュ。取消でも先へ進むだけ）は「取消: 終了」も出さない。
     */
    std::string shown=notice_.empty()?std::string(guide):notice_;
    if(window_.no_cancel&&notice_.empty())for(const char *cut:{tr("   取消: 戻る","   Cancel: back"),tr("   取消: 終了","   Cancel: quit")}){const auto at=shown.find(cut);if(at!=std::string::npos)shown.erase(at,std::strlen(cut));}
    text.draw(box.x+20,box.y+box.h-34,shown,skin.text,box.w-40);
}

RectPx ModernStoreUi::row_icon_rect(const UiLayout &l,int row) const {
    const bool dialog=window_.kind=="dialog";const auto r=dialog?choice_list_rect(l):list_rect(l);
    return {r.x+44,r.y+(row-scroll_)*(dialog?choice_row_height(l):item_row_height(l))+(dialog?6:2),28,28};
}
void ModernStoreUi::item_icon(UiPaint &paint,const RectPx &box,const ModernRow &row) const {
    item_icons_.push_back({menu_scaled_rect(box,paint.layout_scale()),row.tile,row.glyph,static_cast<std::uint8_t>(row.glyph_color),row.available});
}
void ModernStoreUi::draw_voxels(VoxelRenderer &renderer,SlabLibrary &slabs,const TileCatalog &catalog,const UiLayout &l,TextOverlay *text) {
#ifdef HD2D_DISABLE_ITEM_VOXEL_RENDER
    (void)renderer;(void)slabs;(void)catalog;(void)l;(void)text;
    return;
#else
    if(auto_backdrop_){auto_backdrop_->draw_voxels(renderer,slabs,catalog,l,text);return;}
    if(!active()||item_icons_.empty()||commands_open_)return;
    (void)l;
    using namespace gl;
    GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    // This is the final UI pass. Clear depth only; the painted menu is retained.
    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
    renderer.begin_motion({},0);renderer.begin_cutaways(nullptr,0);renderer.set_cutaway_scale(0);
    renderer.set_look({});renderer.set_wear({});renderer.set_leaf({});renderer.set_shadow_scale(0);
    for(const auto &icon:item_icons_) {
        const auto entry=catalog.find(static_cast<std::uint16_t>(icon.tile));
        const int id=entry?slabs.acquire(static_cast<std::uint16_t>(icon.tile),entry->path,renderer):-1;
        if(id<0) {
            if(text&&!icon.glyph.empty()){
                std::string mark=icon.glyph;
                if(mark=="?")mark=reinterpret_cast<const char*>(u8"▤");
                else if(mark=="="||mark=="\"")mark=reinterpret_cast<const char*>(u8"○");
                else if(mark=="_"||mark=="-")mark=reinterpret_cast<const char*>(u8"✦");
                else if(mark==","||mark=="~")mark=reinterpret_cast<const char*>(u8"◆");
                const auto native=term_color_to_rgb(icon.color);const int brightness=int(native.r)+int(native.g)+int(native.b);
                const auto fallback=painted_theme_==1?TextColor{.12f,.16f,.22f,1.f}:TextColor{.9f,.92f,.96f,1.f};
                const auto ink=brightness<150?fallback:TextColor{native.r/255.f,native.g/255.f,native.b/255.f,1.f};
                const int width=text->measure(mark),x=icon.rect.x+std::max(2,(icon.rect.w-width)/2);
                text->draw(x,icon.rect.y+std::max(2,(icon.rect.h-text->cell_h())/2),mark,ink,icon.rect.w-4);
            }
            continue;
        }
        const auto [low,high]=slabs.bounds(id);const Vec3 centre=(low+high)*.5f;
        const float half=std::max({high.x-low.x,high.y-low.y,high.z-low.z,.1f})*.58f;
        const auto view=look_at(centre+Vec3{.65f,4.f,.32f},centre,{0,0,1});
        const auto projection=ortho(-half,half,-half,half,.1f,10.f);
        const auto &r=icon.rect;if(r.empty())continue;
        glViewport(viewport[0]+r.x,viewport[1]+viewport[3]-r.y-r.h,r.w,r.h);
        renderer.begin(projection*view,SceneLighting{},Mat4::identity(),0,0);
        InstanceData instance;instance.r=instance.g=instance.b=icon.available?1.f:.55f;
        renderer.draw_instanced(slabs.gpu(id),&instance,1);
    }
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
#endif
}
void ModernStoreUi::shutdown(){item_icons_.clear();}
std::string ModernStoreUi::status() const{return window_.kind+":"+(commands_open_?"commands":window_.stage)+" tab="+std::to_string(tabs_.index())+" row="+std::to_string(cursor_);}
}
