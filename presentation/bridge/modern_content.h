#pragma once
#include "frame/game_frame.h"
#include "frame/message_more.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string_view>

namespace presentation {
// Terminal coordinates are consumed here. The resulting document contains
// logical fields, table cells and paragraphs; the renderer never reads Term.
inline std::string modern_trim(std::string text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
/*!
 * @brief 誕生（とタイトル）の画面か。死亡・引退のあとの画面（`game_over`）は含めない（修-10）。
 * @details `pre_game_menu` は「本文を全面に出す」判定でタイトル・誕生・死亡後をまとめて指す。
 * 題や操作の名前（「キャラクター作成」「作成をやめる」ほか）は誕生の画面のものなので、死亡後には付けない。
 */
inline bool modern_birth_screen(const GameFrame &frame) {
    return frame.pre_game_menu&&!frame.game_over;
}
/*!
 * @brief 文字入力の窓の題。問いの項目名に「の入力」を足す。
 * @details コアの問いが文になっているとき（「セーブファイル名を入力してください」）に足すと
 * 「セーブファイル名を入力してくださいの入力」になる（観-1）ので、文の語尾を外して項目名にする。
 */
inline std::string modern_input_title(std::string label,bool ja) {
    label=modern_trim(std::move(label));
    while(!label.empty()&&(label.back()==':'||label.back()==' '))label.pop_back();
    const std::string request=reinterpret_cast<const char*>(u8"を入力してください");
    if(label.size()>request.size()&&label.ends_with(request)){label.resize(label.size()-request.size());return label+(ja?reinterpret_cast<const char*>(u8"の入力"):" input");}
    if(label.find(reinterpret_cast<const char*>(u8"入力"))!=std::string::npos||label.starts_with("Enter ")||label.starts_with("Please "))return label;
    return label+(ja?reinterpret_cast<const char*>(u8"の入力"):" input");
}
inline std::string modern_spaces(std::string_view text) {
    std::string out;bool gap=false;
    for(unsigned char c:text) {
        if(c==' '||c=='\t'||c=='\r'||c=='\n'){gap=!out.empty();continue;}
        if(gap)out+=' ';gap=false;out+=char(c);
    }
    return out;
}
// A one-cell row near the top becomes the window heading.  Two kinds of row are never a heading:
// a row of map glyphs (a map or a status column that the mirror still holds; same glyph set as
// `native_readable_body`) and an item row of a list (`a) ...`).  Both would otherwise become the
// window title.  A row that has a word of three or more letters is prose, even when it also has
// glyph characters (`--- Title ---`).
inline bool modern_map_glyph_row(std::string_view text) {
    int content=0,glyph=0,letters=0;
    for(unsigned char c:text) {
        if(c==' '){letters=0;continue;}
        if(c>=128)return false;
        ++content;
        if(std::isalpha(c)){if(++letters>=3)return false;}else letters=0;
        if(std::string_view("#.%~^:+-|'!_/\\<>@&\x7f").find(char(c))!=std::string_view::npos)++glyph;
    }
    return content>0&&glyph*2>=content;
}
inline bool modern_item_row(std::string_view text) {
    return text.size()>=3&&std::isalnum(static_cast<unsigned char>(text[0]))&&text[1]==')'&&text[2]==' ';
}
inline bool modern_heading_candidate(std::string_view text) {
    return !modern_map_glyph_row(text)&&!modern_item_row(text);
}
// The three C cores use CP932 cell widths. In that encoding multiplication,
// Greek and Cyrillic characters occupy two cells; half-width kana occupy one.
// Hengband/Tangband use Unicode widths. Keep this policy inside interpretation.
inline thread_local bool modern_legacy_columns=false;
struct ModernColumnScope {
    bool previous;
    explicit ModernColumnScope(const std::string &profile):previous(modern_legacy_columns){modern_legacy_columns=profile=="legacy"||profile=="frox"||profile=="sil";}
    ~ModernColumnScope(){modern_legacy_columns=previous;}
};
inline size_t modern_column_byte(std::string_view text,int column) {
    int cells=0;size_t at=0;
    while(at<text.size()&&cells<column) {
        const unsigned char c=text[at];size_t n=c<0x80?1:((c&0xe0)==0xc0?2:((c&0xf0)==0xe0?3:4));
        if(at+n>text.size())break;
        unsigned cp=c&((1u<<(7-n))-1);if(n==1)cp=c;
        for(size_t i=1;i<n;++i)cp=(cp<<6)|(static_cast<unsigned char>(text[at+i])&63);
        const bool wide=modern_legacy_columns?(cp>=0x80&&cp!=0x80&&!(cp>=0xff61&&cp<=0xff9f)&&!(cp>=0xf8f0&&cp<=0xf8f3)):cp>=0x1100&&(cp<=0x115f||cp==0x2329||cp==0x232a||(cp>=0x2e80&&cp<=0xa4cf)||(cp>=0xac00&&cp<=0xd7a3)||(cp>=0xf900&&cp<=0xfaff)||(cp>=0xfe10&&cp<=0xfe6f)||(cp>=0xff01&&cp<=0xff60)||(cp>=0xffe0&&cp<=0xffe6)||cp>=0x1f300);
        cells+=wide?2:1;at+=n;
    }
    return at;
}
inline std::string modern_message(const GameFrame &frame) {
    std::string result;
    if(frame.prompt.choices.empty()&&message_more_suffix(frame.prompt.text_utf8))result=frame.prompt.text_utf8;
    else for(const auto &line:frame.menu_term_lines)if(line.source_row==0){result=line.text_utf8;break;}
    result=modern_trim(result);
    for(const auto *marker:{"-more-",reinterpret_cast<const char*>(u8"-続く-")})if(result.ends_with(marker))result.resize(result.size()-std::char_traits<char>::length(marker));
    if(modern_trim(result).empty()&&!frame.messages.empty())result=frame.messages.back().text_utf8;
    std::string joined;size_t first=0;
    while(first<result.size()) {
        const auto end=result.find('\n',first);const auto line=modern_trim(result.substr(first,end==std::string::npos?end:end-first));
        if(!line.empty()) {
            if(!joined.empty()&&static_cast<unsigned char>(joined.back())<128&&static_cast<unsigned char>(line.front())<128)joined+=' ';
            joined+=line;
        }
        if(end==std::string::npos)break;first=end+1;
    }
    return modern_spaces(joined);
}
inline std::string modern_choice_label(std::string text) {
    text=modern_trim(text);
    if(text.starts_with("> "))text=modern_trim(text.substr(2));
    if(text.size()>2&&text[1]==')')return modern_trim(text.substr(2));
    if(text.size()>3&&(text[0]=='('||text[0]=='[')&&((text[0]=='('&&text[2]==')')||(text[0]=='['&&text[2]==']'))&&std::isalnum(static_cast<unsigned char>(text[1])))return modern_trim(text.substr(3));
    if(text.size()>4&&text.starts_with("* ")&&text.ends_with(" *"))return modern_trim(text.substr(2,text.size()-4));
    if(text.size()>2&&text.front()=='['&&text.back()==']')return modern_trim(text.substr(1,text.size()-2));
    const std::string open=reinterpret_cast<const char*>(u8"［"),close=reinterpret_cast<const char*>(u8"］");
    if(text.starts_with(open)&&text.ends_with(close))return modern_trim(text.substr(open.size(),text.size()-open.size()-close.size()));
    // 原作は `ESC) 建物を出る` / `SPACE) 次ページ` のように大文字でも書く。画面側が足す札は `Esc)` / `Space)`
    // （hd2d/ui/menu_header.h）なので、大文字小文字を区別せず剥がさないと札が 2 重になる（修-16）。
    for(const char *prefix:{"Enter)","Return)","Esc)","Tab)","Space)"}) {
        const size_t n=std::char_traits<char>::length(prefix);
        if(text.size()>=n&&std::equal(prefix,prefix+n,text.begin(),[](char a,char b){return std::tolower(static_cast<unsigned char>(a))==std::tolower(static_cast<unsigned char>(b));}))return modern_trim(text.substr(n));
    }
    return text;
}
struct ModernSourceCell {std::string text;int begin{},end{};};
inline std::vector<ModernSourceCell> modern_split_cells(const std::string &line) {
    std::vector<ModernSourceCell> cells;
    size_t start=line.find_first_not_of(" \t");
    while(start!=std::string::npos&&start<line.size()) {
        size_t end=start;
        while(end<line.size()) {
            if(line[end]=='\t'||(line[end]==' '&&end+1<line.size()&&line[end+1]==' '))break;
            // Adjacent letter choices can share a column boundary without a gap.
            if(start+1<line.size()&&line[start+1]==')'&&end>start+2&&end+1<line.size()&&line[end+1]==')'&&std::isalpha(static_cast<unsigned char>(line[end])))break;
            ++end;
        }
        auto text=modern_trim(line.substr(start,end-start));
        if(!text.empty())cells.push_back({std::move(text),int(start),int(end)});
        start=line.find_first_not_of(" \t",end);
        if(start==end&&end<line.size()&&end+1<line.size()&&line[end+1]!=')')++start;
    }
    for(size_t i=0;i+1<cells.size();) {
        if(cells[i+1].text.starts_with(":")&&cells[i].text.find(':')==std::string::npos) {
            cells[i].text+=" "+cells[i+1].text;cells[i].end=cells[i+1].end;cells.erase(cells.begin()+i+1);
        } else ++i;
    }
    return cells;
}
inline bool modern_stat_header_cells(const std::string &line,std::vector<ModernSourceCell> &cells) {
    const char *names[]={"STR","INT","WIS","DEX","CON","CHR",reinterpret_cast<const char*>(u8"腕力"),reinterpret_cast<const char*>(u8"知能"),reinterpret_cast<const char*>(u8"賢さ"),reinterpret_cast<const char*>(u8"器用"),reinterpret_cast<const char*>(u8"耐久"),reinterpret_cast<const char*>(u8"魅力")};
    std::vector<ModernSourceCell> headers;
    for(const auto *name:names) {
        const auto at=line.find(name);if(at==std::string::npos)continue;
        const size_t n=std::char_traits<char>::length(name);
        if(static_cast<unsigned char>(name[0])<128&&((at>0&&std::isalpha(static_cast<unsigned char>(line[at-1])))||(at+n<line.size()&&std::isalpha(static_cast<unsigned char>(line[at+n])))))continue;
        headers.push_back({name,int(at),int(at+n)});
    }
    if(headers.size()<5) {
        headers.clear();
        const std::vector<std::vector<std::string>> groups={
            {"Disarming","Device","Save","Stealth"},{"Searching","Perception","Melee","Archery"},
            {reinterpret_cast<const char*>(u8"解除"),reinterpret_cast<const char*>(u8"魔道具"),reinterpret_cast<const char*>(u8"魔法防御"),reinterpret_cast<const char*>(u8"隠密")},
            {reinterpret_cast<const char*>(u8"探索"),reinterpret_cast<const char*>(u8"知覚"),reinterpret_cast<const char*>(u8"近接戦闘"),reinterpret_cast<const char*>(u8"射撃")}
        };
        for(const auto &group:groups) {
            std::vector<ModernSourceCell> found;
            for(const auto &name:group){auto at=line.find(name);if(at!=std::string::npos)found.push_back({name,int(at),int(at+name.size())});}
            if(found.size()==4){headers=std::move(found);break;}
        }
        if(headers.size()!=4)return false;
    }
    for(const auto *name:{"BHP",reinterpret_cast<const char*>(u8"生命"),reinterpret_cast<const char*>(u8"経験")}) {
        const auto at=line.find(name);if(at!=std::string::npos)headers.push_back({name,int(at),int(at+std::char_traits<char>::length(name))});
    }
    std::sort(headers.begin(),headers.end(),[](const auto &a,const auto &b){return a.begin<b.begin;});
    std::vector<ModernSourceCell> combined;
    for(const auto &cell:cells)if(cell.end<=headers.front().begin)combined.push_back(cell);
    combined.insert(combined.end(),headers.begin(),headers.end());cells=std::move(combined);return true;
}
inline bool modern_frame_rule(std::string_view text) {
    int symbols=0,other=0;
    for(unsigned char c:text)if(c!=' '&&c!='\t') {
        if(std::string_view("-=_|+~*#").find(c)!=std::string_view::npos)++symbols;else ++other;
    }
    return symbols>=3&&!other;
}
inline int modern_byte_column(const std::string &text,size_t at) {
    int column=0;while(size_t(column)<text.size()*2&&modern_column_byte(text,column)<at)++column;return column;
}
inline int modern_focus_key(const ModernWindow &window,const std::string &target) {
    const auto wanted=std::find_if(window.rows.begin(),window.rows.end(),[&](const auto &r){return r.id==target;});
    const auto current=std::find_if(window.rows.begin(),window.rows.end(),[&](const auto &r){return r.id==window.cursor_target;});
    if(wanted==window.rows.end()||wanted==current)return 0;
    if(current==window.rows.end())return window.content_type=="browser"&&wanted->selection==0?'4':0;
    if(window.content_type=="settings_list_rogue")return wanted>current?13:'-';
    // Choice grids are painted as one vertical list in the modern UI.  Move
    // through the native grid in its key order so crossing a source row does
    // not first jump to the same column and then walk back across the row.
    if(window.content_type=="choice_grid") {
        if(wanted->slot==current->slot)return wanted>current?'6':'4';
        if(wanted->selection==current->selection)return wanted->slot>current->slot?'2':'8';
        return wanted>current?'6':'4';
    }
    if(window.content_type=="browser"&&wanted->selection!=current->selection)return wanted->selection>current->selection?'6':'4';
    if(wanted->slot!=current->slot)return wanted->slot>current->slot?'2':'8';
    if(wanted->selection!=current->selection)return wanted->selection>current->selection?'6':'4';
    return 0;
}

inline bool modern_rogue_settings(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(profile!="rogue"||w.stage!="cursor")return false;
    const bool ja=w.language=="ja";
    std::vector<ModernRow> rows;
    std::vector<ModernBlock> content;
    std::string target;
    bool yes_no=false,true_false=false,has_choice=false;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto text=modern_trim(frame.menu_term_lines[i].text_utf8);
        if(text.empty())continue;
        const auto colon=text.rfind(':');
        if(colon==std::string::npos)continue;
        auto value=modern_trim(text.substr(colon+1));
        /*
         * 原作は想定外のキーを受けると値の後ろへ "(T or F)" / "(Yes or No)" を書き足す。
         * その行を値として読めないと、カーソル行が一覧から消えて別の画面に変わる。
         */
        if(const auto hint=value.find('(');hint!=std::string::npos&&hint>0&&value.ends_with(")"))value=modern_trim(value.substr(0,hint));
        const bool yes=value=="Yes",no=value=="No",
            truth=value=="True"||value==reinterpret_cast<const char*>(u8"有効"),
            falsity=value=="False"||value==reinterpret_cast<const char*>(u8"無効");
        // 5.4 の "inven" だけが 3 択（原作 options.c の inv_t_name[]）。rl_i18n.c の
        // 表示境界で日本語へ訳されるので、原文と訳文の両方で見分ける
        // （rl_i18n.c の "Overwrite"/"Slow"/"Clear" のエントリ）。
        const bool choice=value=="Overwrite"||value=="Slow"||value=="Clear"
            ||value==reinterpret_cast<const char*>(u8"上書き")||value==reinterpret_cast<const char*>(u8"1行ずつ")||value==reinterpret_cast<const char*>(u8"画面消去");
        auto label=modern_trim(text.substr(0,colon));
        // RogueClone2 はカーソル行の 1 桁目に '>' を書く。項目名には含めない。
        if(label.starts_with(">"))label=modern_trim(label.substr(1));
        if(label.empty())continue;
        const auto id_begin=label.rfind(" (\"");
        std::string identifier;
        if(id_begin!=std::string::npos&&label.ends_with("\")")) {
            identifier=label.substr(id_begin+3,label.size()-id_begin-5);
            label=modern_trim(label.substr(0,id_begin));
        }
        const bool is_bool=yes||no||truth||falsity;
        yes_no=yes_no||yes||no;true_false=true_false||truth||falsity;has_choice=has_choice||choice;
        ModernRow row;
        row.id="rogue-option:"+(identifier.empty()?std::to_string(frame.menu_term_lines[i].source_row):identifier);
        row.label=label;row.slot=int(i);row.selection=0;row.price_known=false;row.available=true;
        const bool active=int(i)==frame.menu_core_cursor.line_index;
        if(active)target=row.id;
        // 名前・好きな果物のような文字列の項目と、5.4 の inven（3 択）は原作の
        // 文言をそのまま出す。真偽の項目だけ「オン／オフ」に置き換える
        // （設計の判断、rogue_restore 第 2 回、項目 1・2）。
        const std::string shown_value=is_bool?((yes||truth)?(ja?reinterpret_cast<const char*>(u8"オン"):"On"):(ja?reinterpret_cast<const char*>(u8"オフ"):"Off")):value;
        ModernCell cell{row.label,shown_value,active};
        cell.target=row.id;
        ModernBlock block;block.kind="fields";block.cells.push_back(std::move(cell));content.push_back(std::move(block));
        rows.push_back(std::move(row));
    }
    if(rows.empty()||target.empty()||yes_no==true_false)return false;
    w.content_type="settings_list_rogue";w.stage="cursor";w.title=ja?reinterpret_cast<const char*>(u8"設定"):"Settings";
    w.body.clear();w.content=std::move(content);w.rows=std::move(rows);w.cursor_target=std::move(target);w.actions.clear();
    /*
     * Rogue の原作 option editor は y/n または t/f を受けると次の行へ自動で進む。
     * HD2D の設定一覧は「カーソルはその項目に置いたまま、決定で値だけを変える」が
     * 共通契約なので、アダプタ専用の '1' / '0'（真偽）・'4' / '6'（3 択の巡回）を使う。
     * 各 Rogue source patch はこれを「値を変更して同じ行に留まる」キーとしてだけ受理する。
     * これにより最終行（RogueClone2/5.4 の墓碑設定）を変更しても option() の末尾へ
     * 抜けて別の Space 待ち画面へ遷移しない。文字列の項目（名前・好きな果物）は
     * 決定キーで入力を始め、その先は既存の IME 入力
     * （GameFrame::text_input_active / ModernWindow の stage="text"）にそのまま任せる。
     */
    (void)yes_no;(void)true_false;
    /*
     * 決定キーは Ctrl-E（rogue-setting-toggle）を送る。真偽は反転、5.4 の inven は次の値、
     * 文字列の項目（名前・好きな果物）はここで初めて入力を始める。左右では値を変えない
     * （カーソルが来ただけで入力が始まり、確定で次の行の入力へ
     * 連続し、最後はスペース待ちで止まっていた）。on/off/prev/next は検査と互換のために残す。
     */
    w.actions.push_back({"rogue-setting-toggle",ja?reinterpret_cast<const char*>(u8"切り替え"):"Toggle","",true,5});
    w.actions.push_back({"rogue-setting-on",ja?reinterpret_cast<const char*>(u8"オン"):"On","",true,'1'});
    w.actions.push_back({"rogue-setting-off",ja?reinterpret_cast<const char*>(u8"オフ"):"Off","",true,'0'});
    if(has_choice) {
        w.actions.push_back({"rogue-setting-prev",ja?reinterpret_cast<const char*>(u8"前の値"):"Previous value","",true,'4'});
        w.actions.push_back({"rogue-setting-next",ja?reinterpret_cast<const char*>(u8"次の値"):"Next value","",true,'6'});
    }
    return true;
}
inline bool modern_source_choice(const ModernSourceCell &cell) {
    return cell.text.size()>2&&cell.text[1]==')'&&((cell.text[0]>='a'&&cell.text[0]<='z')||(cell.text[0]>='A'&&cell.text[0]<='Z')||cell.text[0]=='*');
}
inline bool modern_cursor_choice_grid(const GameFrame &frame) {
    const auto cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    if(cursor.line_index<0||cursor.line_index>=int(frame.menu_term_lines.size()))return false;
    const auto &line=frame.menu_term_lines[cursor.line_index].text_utf8;
    const int at=cursor.span_len>0?cursor.span_begin:int(modern_column_byte(line,std::max(0,frame.menu_term_curs_col)));
    for(const auto &cell:modern_split_cells(line))if(modern_source_choice(cell)&&at>=cell.begin-2&&at<cell.end)return true;
    return false;
}
inline void modern_grid_target(ModernWindow &window,ModernCell &cell,const ModernSourceCell &source,
    const std::string &line,int row,bool linear) {
    ModernRow target;target.key=target.hotkey=static_cast<unsigned char>(source.text[0]);target.label=cell.text;target.price_known=false;
    unsigned long long hash=1469598103934665603ULL;for(unsigned char c:cell.text){hash^=c;hash*=1099511628211ULL;}
    target.id="cursor:"+std::to_string(row)+":"+std::to_string(source.begin)+":"+std::to_string(hash);
    target.slot=linear?(target.key>='a'&&target.key<='z'?target.key-'a':(target.key>='A'&&target.key<='Z'?target.key-'A'+26:52)):row;
    target.selection=linear?0:modern_byte_column(line,source.begin);
    target.actions.push_back({"choice",cell.text,"",true,target.key});cell.target=target.id;
    if(cell.selected)window.cursor_target=target.id;
    window.rows.push_back(std::move(target));
}

inline bool interpret_modern_window_flags(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(w.stage!="cursor")return false;
    bool family=false;
    for(const auto &line:frame.menu_term_lines)if(line.source_row<=2) {
        const auto &s=line.text_utf8;
        if((s.find("Window")!=std::string::npos&&(s.find("flag")!=std::string::npos||s.find("Flag")!=std::string::npos))
            ||s.find(reinterpret_cast<const char*>(u8"ウィンドウ・フラグ"))!=std::string::npos
            ||s.find(reinterpret_cast<const char*>(u8"サブウィンドウの割り当て"))!=std::string::npos
            ||s.find(reinterpret_cast<const char*>(u8"ウィンドウ・フラグ"))!=std::string::npos)family=true;
    }
    if(!family)return false;
    std::vector<int> columns;int first=-1,last=-1;
    for(size_t row=0;row<frame.menu_term_lines.size();++row) {
        const auto &text=frame.menu_term_lines[row].text_utf8;std::vector<int> found;
        for(size_t at=0;at<text.size();++at)if((text[at]=='.'||text[at]=='X'||text[at]=='x')&&(at==0||text[at-1]==' ')
            &&(at+1==text.size()||text[at+1]==' '))found.push_back(modern_byte_column(text,at));
        if(found.size()>=6) {
            if(first<0)first=int(row);last=int(row);
            for(int column:found)if(std::find(columns.begin(),columns.end(),column)==columns.end())columns.push_back(column);
        }
    }
    if(columns.size()<6||columns.size()>16)return false;
    std::sort(columns.begin(),columns.end());
    const bool ja=w.language=="ja";w.content.clear();w.rows.clear();w.cursor_target.clear();w.content_type="settings_table";
    w.title=ja?reinterpret_cast<const char*>(u8"小窓の表示設定"):"Window contents";
    ModernBlock header;header.kind="table_header";header.cells.push_back({"",ja?reinterpret_cast<const char*>(u8"表示する内容"):"Content",false});
    for(size_t i=0;i<columns.size();++i)header.cells.push_back({"",i==0?(ja?reinterpret_cast<const char*>(u8"メイン"):"Main"):std::to_string(i),false});
    w.content.push_back(std::move(header));
    const auto &cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    int selected_col=0;
    for(size_t i=1;i<columns.size();++i)if(std::abs(columns[i]-frame.menu_term_curs_col)<std::abs(columns[selected_col]-frame.menu_term_curs_col))selected_col=int(i);
    for(int row=first;row<=last;++row) {
        const auto &text=frame.menu_term_lines[row].text_utf8;
        auto name=modern_trim(text.substr(0,modern_column_byte(text,columns.front())));
        if(name.empty()||modern_frame_rule(name))continue;
        ModernBlock line;line.kind="table_row";line.cells.push_back({"",modern_spaces(name),false});
        for(size_t c=0;c<columns.size();++c) {
            const auto at=modern_column_byte(text,columns[c]);const char flag=at<text.size()?text[at]:' ';
            const auto value=(flag=='X'||flag=='x')?(ja?reinterpret_cast<const char*>(u8"オン"):"On"):(flag=='.'?(ja?reinterpret_cast<const char*>(u8"オフ"):"Off"):reinterpret_cast<const char*>(u8"—"));
            ModernRow target;target.id="window:"+std::to_string(row)+":"+std::to_string(c);target.slot=row;target.selection=int(c);target.price_known=false;
            target.label=name+" / "+w.content.front().cells[c+1].text;target.available=flag=='.'||flag=='X'||flag=='x';
            if(!target.available)target.reason=ja?reinterpret_cast<const char*>(u8"この小窓には割り当てられません。"):"This content is unavailable in this window.";
            ModernCell cell{w.content.front().cells[c+1].text,value,row==cursor.line_index&&int(c)==selected_col};cell.target=target.id;
            if(cell.selected) {
                w.cursor_target=target.id;
                // Gensoband's t always assigns; its y/n are the actual on/off operations.
                if(profile=="legacy")w.continue_key=(flag=='X'||flag=='x')?'n':'y';
            }
            w.rows.push_back(std::move(target));line.cells.push_back(std::move(cell));
        }
        w.content.push_back(std::move(line));
    }
    return w.content.size()>1;
}
inline void interpret_modern_stat_columns(const std::string &source,const std::vector<ModernSourceCell> &cells,
    bool header,ModernBlock &block,std::vector<std::pair<int,std::string>> &columns,const std::string &language) {
    if(header) {
        columns.clear();bool started=false;
        for(size_t i=0;i<cells.size();++i) {
            const auto &cell=cells[i];
            if(cell.text=="STR"||cell.text==reinterpret_cast<const char*>(u8"腕力")||cell.text==reinterpret_cast<const char*>(u8"筋力")
                ||cell.text=="Disarming"||cell.text=="Searching"||cell.text==reinterpret_cast<const char*>(u8"解除")||cell.text==reinterpret_cast<const char*>(u8"探索"))started=true;
            if(started)columns.emplace_back(modern_byte_column(source,(cell.begin+cell.end)/2),cell.text);
        }
        const size_t prefix=block.cells.size()-columns.size();block.cells.resize(prefix);block.kind="fields";return;
    }
    if(columns.empty())return;
    std::vector<ModernCell> labels,values;
    const bool skills=columns.size()==4;
    for(size_t i=0;i<cells.size();++i) {
        auto value=block.cells[i];auto numeric=value.text;
        if(numeric.starts_with("== "))numeric=modern_trim(numeric.substr(3));
        const bool number=!numeric.empty()&&std::all_of(numeric.begin(),numeric.end(),[](unsigned char c){return std::isdigit(c)||c=='+'||c=='-'||c=='%'||c=='.'||c==':';});
        if(number||skills) {
            const int column=modern_byte_column(source,(cells[i].begin+cells[i].end)/2);
            const auto found=std::min_element(columns.begin(),columns.end(),[&](const auto &a,const auto &b){return std::abs(a.first-column)<std::abs(b.first-column);});
            if(found!=columns.end()&&std::abs(found->first-column)<(skills?8:6)){value.label=found->second;value.text=std::move(numeric);values.push_back(std::move(value));continue;}
        }
        labels.push_back(std::move(value));
    }
    if(values.size()<3)return;
    block.kind="stat_values";
    std::vector<ModernCell> retained;
    for(const auto &label:labels) {
        if(label.text.empty()){retained.push_back(label);continue;}
        if(!block.title.empty())block.title+=" / ";block.title+=label.label.empty()?label.text:(label.label+": "+label.text);
    }
    const bool total=std::any_of(cells.begin(),cells.end(),[](const auto &cell){return cell.text.starts_with("== ");});
    if(block.title.empty())block.title=language=="ja"?reinterpret_cast<const char*>(total?u8"合計":u8"能力補正"):(total?"Total":"Modifiers");
    retained.insert(retained.end(),values.begin(),values.end());block.cells=std::move(retained);
}

inline void modern_add_action(ModernWindow &w,int key,std::string label) {
    label=modern_trim(label);
    if(key<=0||key>127||key==13||key==27||label.empty())return;
    if(std::any_of(w.rows.begin(),w.rows.end(),[&](const auto &row){return row.key==key;}))return;
    if(std::any_of(w.actions.begin(),w.actions.end(),[&](const auto &action){return action.key==key;}))return;
    w.actions.push_back({"key:"+std::to_string(key),std::move(label),"",true,key});
}
inline void modern_frox_cornucopia_controls(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if(profile!="frox")return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    const auto add=[&](int key,const std::string &label) {
        if(key<=0||key>127||label.empty())return;
        if(std::any_of(w.actions.begin(),w.actions.end(),[&](const auto &a){return a.key==key;}))return;
        w.actions.push_back({"cornucopia:"+std::to_string(key),label,"",true,key});
    };

    if(w.stage=="number") {
        bool amount=false;
        for(const auto &line:frame.menu_term_lines) {
            const auto text=modern_trim(line.text_utf8);
            if(text.starts_with("Request:")||text.starts_with("Pay back:")||text.starts_with("Withdraw:")
                ||text.starts_with("Additional deposit:")||text.starts_with("New deposit:"))amount=true;
            if(text=="Press 'M' to extend your loan instead.")add('M',tr(u8"借入に切り替える","Switch to borrowing"));
            else if(text=="Press 'M' to repay your loan instead.")add('M',tr(u8"返済に切り替える","Switch to repayment"));
            else if(text=="Press 'M' to deposit money instead.")add('M',tr(u8"預金に切り替える","Switch to deposit"));
            else if(text=="Press 'M' to withdraw money instead.")add('M',tr(u8"引き出しに切り替える","Switch to withdrawal"));
        }
        if(amount)w.content_type="cornucopia_amount";
    }

    if(source.find("[Space] to unselect a plan, [Enter] to buy this policy")!=std::string::npos) {
        w.content_type="cornucopia_policy";
        add(' ',tr(u8"選択中のプランを外す","Unselect current plan"));
        add('8',tr(u8"前のプランへ移動","Move to previous plan"));
        add('2',tr(u8"次のプランへ移動","Move to next plan"));
        add('N',tr(u8"保険対象の数量を増やす","Increase insured quantity"));
        add('+',tr(u8"保険対象の数量を増やす","Increase insured quantity"));
        add('n',tr(u8"保険対象の数量を減らす","Decrease insured quantity"));
        add('-',tr(u8"保険対象の数量を減らす","Decrease insured quantity"));
        add(13,tr(u8"この保険内容を購入する","Buy this policy"));
    }
}
inline void modern_frox_autopick_search_controls(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(profile!="frox"||w.stage!="text")return;
    static constexpr std::string_view prompt="Search key(^I:inven ^L:destroyed): ";
    const TermMirrorLine *row=nullptr;
    for(const auto &line:frame.menu_term_lines) {
        if(line.source_row==0&&line.text_utf8.starts_with(prompt)){row=&line;break;}
    }
    if(!row)return;

    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    const size_t first=prompt.size();
    const size_t caret=std::min(row->text_utf8.size(),modern_column_byte(row->text_utf8,std::max(0,frame.menu_term_curs_col)));
    const auto tail=row->text_utf8.substr(std::min(first,row->text_utf8.size()));
    const auto nonblank=tail.find_last_not_of(" \t\r\n");
    const size_t visible=std::max(nonblank==std::string::npos?size_t(0):nonblank+1,caret>=first?caret-first:size_t(0));

    w.content_type="autopick_search";
    w.title=tr(u8"自動拾いルールの検索","Search auto-pick rules");
    w.input_label=tr(u8"検索文字列","Search text");
    w.input=tail.substr(0,std::min(visible,tail.size()));
    w.input_caret=int(std::min(w.input.size(),caret>=first?caret-first:size_t(0)));
    w.actions.clear();
    modern_add_action(w,9,tr(u8"所持品から検索対象を選ぶ","Choose search target from inventory"));
    modern_add_action(w,12,tr(u8"直前に破棄した品を検索対象にする","Use last destroyed item as search target"));
    modern_add_action(w,18,tr(u8"前方向ではなく逆方向へ検索する","Search backward"));
}
inline bool modern_control_line(const GameFrame &frame,const TermMirrorLine &line) {
    const auto &text=line.text_utf8;
    const bool escape=text.find("ESC")!=std::string::npos||text.find("Esc")!=std::string::npos||text.find("Escape")!=std::string::npos;
    const int last=frame.menu_term_lines.empty()?0:frame.menu_term_lines.back().source_row;
    const bool placement=line.source_row<=1||line.source_row>=last-4;
    const bool birth=modern_birth_screen(frame)&&((text.find("'S'")!=std::string::npos&&text.find("'Q'")!=std::string::npos)
        ||(text.find("'='")!=std::string::npos&&text.find('(')!=std::string::npos));
    const bool editor=text.find("^Q")!=std::string::npos&&text.find("^W")!=std::string::npos;
    return (escape&&placement)||birth||editor;
}
inline std::string modern_interpret_controls(const GameFrame &frame,const TermMirrorLine &source,ModernWindow &w) {
    std::string text=source.text_utf8;
    if(!modern_control_line(frame,source))return text;
    bool found=false;size_t first=std::string::npos;
    for(size_t at=0;at+2<text.size();++at) {
        if(text[at]!='\''||text[at+2]!='\''||static_cast<unsigned char>(text[at+1])>=128)continue;
        const int key=static_cast<unsigned char>(text[at+1]);size_t end=text.find_first_of(",;)",at+3);
        const auto another=text.find('\'',at+3);if(another!=std::string::npos)end=std::min(end,another);
        auto label=modern_trim(text.substr(at+3,end==std::string::npos?end:end-at-3));
        if(label.empty()||label.front()=='-'||label.front()=='=')continue;
        if(label.starts_with(reinterpret_cast<const char*>(u8"で")))label.erase(0,3);
        if(label.starts_with("to "))label.erase(0,3);
        modern_add_action(w,key,std::move(label));found=true;first=std::min(first,at);
    }
    // Editors and documented single-key command bands use "key: action".
    size_t start=0;
    while(start<text.size()) {
        const auto end=text.find_first_of(",;",start);const auto chunk=modern_trim(text.substr(start,end==std::string::npos?end:end-start));
        int key=0;size_t prefix=0;
        if(chunk.size()>3&&chunk[0]=='^'&&chunk[1]>='A'&&chunk[1]<='Z'&&chunk[2]==':'){key=chunk[1]-'A'+1;prefix=3;}
        else if(chunk.size()>2&&static_cast<unsigned char>(chunk[0])<128&&chunk[1]==':'){key=static_cast<unsigned char>(chunk[0]);prefix=2;}
        if(key){modern_add_action(w,key,modern_trim(chunk.substr(prefix)));found=true;first=std::min(first,start);}
        if(end==std::string::npos)break;start=end+1;
    }
    if(!found)return text;
    auto lead=modern_trim(text.substr(0,first));
    const auto paren=lead.rfind('(');if(paren!=std::string::npos)lead=modern_trim(lead.substr(0,paren));
    if(lead.starts_with('<')||lead=="("||lead=="["||lead=="Directions")lead.clear();
    // Text after ESC can contain real counts (Sil-Q's knowledge browser).
    const auto esc=text.find("ESC");
    if(esc!=std::string::npos) {
        const auto tail=modern_trim(text.substr(esc+3));
        if(tail.find(reinterpret_cast<const char*>(u8"知っている"))!=std::string::npos||tail.find("known")!=std::string::npos){if(!lead.empty())lead+=' ';lead+=tail;}
    }
    return lead;
}
inline bool modern_browser_content(const GameFrame &frame,ModernWindow &w) {
    if(w.stage!="detail"&&w.stage!="cursor")return false;
    std::string title,footer,count;int footer_row=-1;const TermMirrorLine *header=nullptr;
    for(const auto &line:frame.menu_term_lines) {
        const auto text=modern_trim(line.text_utf8);
        if(line.source_row<=3&&(text.starts_with("Knowledge -")||text.starts_with("Visuals -")||text.starts_with(reinterpret_cast<const char*>(u8"知識 -"))||text.starts_with(reinterpret_cast<const char*>(u8"表示 -"))))title=text;
        if(line.source_row==3)count=text;
        if(line.source_row==4)header=&line;
        if(text.find("ESC")!=std::string::npos&&(text.find("<dir>")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"<方向>"))!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"<向き>"))!=std::string::npos)){footer=text;footer_row=line.source_row;}
    }
    if(title.empty()||!header||footer.empty())return false;
    const bool ja=w.language=="ja";const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    const auto slice=[](const std::string &text,int from,int to){const auto a=modern_column_byte(text,from),b=modern_column_byte(text,to);return modern_trim(text.substr(a,b-a));};
    int separator=-1;for(const auto &line:frame.menu_term_lines)if(line.source_row==6){const auto at=line.text_utf8.find('|');if(at!=std::string::npos)separator=modern_byte_column(line.text_utf8,at);}
    int name_col=separator>=0?separator+2:0;
    if(separator<0)for(const auto &cell:modern_split_cells(header->text_utf8))if(cell.text=="Name"||cell.text==reinterpret_cast<const char*>(u8"名前"))name_col=modern_byte_column(header->text_utf8,cell.begin);
    const bool editing=footer.find("ENTER")!=std::string::npos||footer.find("Enter")!=std::string::npos;
    const bool recall=footer.find("'r'")!=std::string::npos||footer.find(" r ")!=std::string::npos;
    ModernWindow before_edit;if(editing)before_edit=w;
    w.content.clear();w.rows.clear();w.actions.clear();w.cursor_target.clear();w.stage="cursor";w.cursor=frame.menu_term_curs_row;w.title=title;w.subtitle=count;
    if(editing) {
        const auto hex=[](char c){return c>='0'&&c<='9'?c-'0':(c>='a'&&c<='f'?c-'a'+10:(c>='A'&&c<='F'?c-'A'+10:-1));};
        for(const auto &line:frame.menu_term_lines)if(line.source_row==6)for(size_t at=0;at+4<line.text_utf8.size();++at) {
            const auto &s=line.text_utf8;
            if(s[at+2]!='/'||hex(s[at])<0||hex(s[at+1])<0||hex(s[at+3])<0||hex(s[at+4])<0)continue;
            const int color=hex(s[at])*16+hex(s[at+1]),character=hex(s[at+3])*16+hex(s[at+4]);
            ModernBlock item;item.kind="fields";item.cells.push_back({tr(u8"対象","Target"),modern_trim(s.substr(modern_column_byte(s,name_col),at-modern_column_byte(s,name_col))),false});w.content.push_back(std::move(item));
            ModernBlock value;value.kind="fields";
            value.cells.push_back({tr(u8"文字","Character"),character==32?tr(u8"空白","Space"):(character>32&&character<127?std::string(1,char(character)):tr(u8"文字・タイル番号","Character / tile code")),false});
            value.cells.push_back({tr(u8"文字番号","Character code"),std::to_string(character)+" (0x"+s.substr(at+3,2)+")",false});
            value.cells.push_back({tr(u8"色番号","Color code"),std::to_string(color)+" (0x"+s.substr(at,2)+")",false});w.content.push_back(std::move(value));
            w.content_type="symbol_editor";w.title=tr(u8"文字と色の設定","Character and color");w.continue_key=13;w.prev_key=w.next_key=0;
            modern_add_action(w,'4',tr(u8"前の文字","Previous character"));modern_add_action(w,'6',tr(u8"次の文字","Next character"));modern_add_action(w,'8',tr(u8"前の色","Previous color"));modern_add_action(w,'2',tr(u8"次の色","Next color"));
            if(footer.find("'c'")!=std::string::npos)modern_add_action(w,'c',tr(u8"文字と色をコピー","Copy character and color"));
            if(footer.find("'p'")!=std::string::npos)modern_add_action(w,'p',tr(u8"文字と色を貼り付け","Paste character and color"));
            w.actions.push_back({"key:13",tr(u8"変更を確定する","Accept changes"),"",true,13});w.actions.push_back({"key:27",tr(u8"変更を取り消す","Cancel changes"),"",true,27});return true;
        }
        w=std::move(before_edit);return false;
    }
    struct Column {int start;std::string label;};std::vector<Column> columns;
    if(separator>=0)columns.push_back({0,tr(u8"分類","Group")});columns.push_back({name_col,tr(u8"名前","Name")});
    for(const auto &cell:modern_split_cells(header->text_utf8)) {
        const int col=modern_byte_column(header->text_utf8,cell.begin);
        if(col<=name_col)continue;
        if(cell.text=="Idx"||cell.text==reinterpret_cast<const char*>(u8"番号"))columns.push_back({col,tr(u8"番号","Index")});
        else if(cell.text=="Sym"||cell.text==reinterpret_cast<const char*>(u8"文字"))columns.push_back({col,tr(u8"文字","Character")});
        else if(cell.text==reinterpret_cast<const char*>(u8"記号"))columns.push_back({col,tr(u8"記号","Symbol")});
        else if(cell.text=="Kills"||cell.text==reinterpret_cast<const char*>(u8"殺害数"))columns.push_back({col,tr(u8"殺害数","Kills")});
    }
    std::sort(columns.begin(),columns.end(),[](const auto &a,const auto &b){return a.start<b.start;});
    ModernBlock headings;headings.kind="table_header";for(const auto &col:columns)headings.cells.push_back({"",col.label,false});w.content.push_back(std::move(headings));
    const int cursor_line=frame.menu_term_curs_row>=0?frame.menu_term_curs_row:frame.menu_core_cursor.line_index;
    const int active_column=separator>=0&&frame.menu_term_curs_col<name_col?0:1;
    for(size_t index=0;index<frame.menu_term_lines.size();++index) {
        const auto &line=frame.menu_term_lines[index];if(line.source_row<6||line.source_row>=footer_row)continue;
        ModernBlock block;block.kind="table_row";
        for(size_t c=0;c<columns.size();++c) {
            auto value=slice(line.text_utf8,columns[c].start,c+1<columns.size()?columns[c+1].start:256);
            while(!value.empty()&&(value.back()=='|'||value.back()==' '))value.pop_back();
            const bool group=separator>=0&&c==0,item=columns[c].start==name_col;
            ModernCell cell{columns[c].label,value,false};
            if((group||item)&&!value.empty()) {
                ModernRow row;row.id="browser:"+std::to_string(index)+":"+(group?"0":"1")+":"+value;row.slot=int(index);row.selection=group?0:1;row.label=value;row.price_known=false;
                cell.target=row.id;cell.selected=int(index)==cursor_line&&row.selection==active_column;
                if(cell.selected)w.cursor_target=row.id;
                if(item&&recall)row.actions.push_back({"help",tr(u8"詳細を見る","Inspect"),"",true,'r'});
                w.rows.push_back(std::move(row));
            }
            block.cells.push_back(std::move(cell));
        }
        if(std::any_of(block.cells.begin(),block.cells.end(),[](const auto &cell){return !cell.text.empty();}))w.content.push_back(std::move(block));
    }
    // Groups and items are independent lists. Their rows are not pairs.
    if(separator>=0) {
        ModernBlock groups;groups.kind="group_list";groups.title=tr(u8"分類","Groups");std::string current_group;
        w.content.front().cells.erase(w.content.front().cells.begin());
        for(size_t i=1;i<w.content.size();++i) {
            auto &block=w.content[i];if(block.cells.empty())continue;auto cell=std::move(block.cells.front());block.cells.erase(block.cells.begin());
            cell.label.clear();if(!cell.text.empty())groups.cells.push_back(std::move(cell));
        }
        for(const auto &line:frame.menu_term_lines)if(line.source_row>=6&&(line.color&15)==14) {
            current_group=slice(line.text_utf8,0,separator);if(!current_group.empty())break;
        }
        if(!current_group.empty())w.subtitle=(count.empty()?"":count+" / ")+current_group;
        w.content.erase(std::remove_if(w.content.begin()+1,w.content.end(),[](const auto &block){return std::all_of(block.cells.begin(),block.cells.end(),[](const auto &cell){return cell.text.empty();});}),w.content.end());
        w.content.insert(w.content.begin(),std::move(groups));
    }
    if(w.rows.empty()) {
        w.content.clear();ModernBlock empty;empty.kind="paragraph";empty.cells.push_back({"",tr(u8"該当する項目はありません。","There are no matching entries."),false});w.content.push_back(std::move(empty));
        w.content_type="empty_list";w.stage="detail";w.continue_key=27;w.prev_key=w.next_key=0;w.previous=w.next=false;return true;
    }
    w.content_type="browser";w.continue_key=active_column==0?'6':(recall?'r':(footer.find("'v'")!=std::string::npos?'v':13));
    /*
     * ページ送りは斜めの方向キー（9＝前のページ・3＝次のページ）。ブラウザの原作（`browser_cursor`）は方向キーの
     * 斜めで 1 画面ぶん動かす。変愚・短愚・幻想蛮怒・Frox は ` `（次）・`-`（前）も受けるが、Sil-Q は受けない
     * （`target_dir()` が ` ` を方向にしない。ページ送りは `9`/`3` だけ。`cmd4.c` の `browser_cursor`）。
     * `9`/`3` は 5 コアのどれも受けるので、これを既定にする（以前は ` `/`-` に上書きしていて、
     * `native_modern.h` の `native_terminal_navigation()` の `9`/`3` の宣言が効かず、Sil-Q ではページ送りが空振りしていた）。
     * コアのアダプタが別のキーを申告しているとき（`GameFrame::menu_page_*_key`）は、そちらを優先する。
     */
    const bool declared_pages=frame.menu_page_prev_key||frame.menu_page_next_key;
    w.prev_key=declared_pages?frame.menu_page_prev_key:'9';
    w.next_key=declared_pages?frame.menu_page_next_key:'3';
    w.previous=w.next=true;
    if(recall)modern_add_action(w,'r',tr(u8"詳細を見る","Inspect"));
    if(footer.find("'v'")!=std::string::npos)modern_add_action(w,'v',tr(u8"文字と色を変更する","Edit character and color"));
    if(footer.find("'c'")!=std::string::npos)modern_add_action(w,'c',tr(u8"文字と色をコピー","Copy character and color"));
    if(footer.find("'p'")!=std::string::npos)modern_add_action(w,'p',tr(u8"文字と色を貼り付け","Paste character and color"));
    return true;
}

inline std::vector<int> modern_numbers(const std::string &text) {
    std::vector<int> out;
    for(size_t at=0;at<text.size();) {
        if(!std::isdigit(static_cast<unsigned char>(text[at]))){++at;continue;}
        const size_t start=at;const bool hex=at+2<text.size()&&text[at]=='0'&&(text[at+1]=='x'||text[at+1]=='X');if(hex)at+=2;
        while(at<text.size()&&(hex?std::isxdigit(static_cast<unsigned char>(text[at])):std::isdigit(static_cast<unsigned char>(text[at]))))++at;
        try{out.push_back(std::stoi(text.substr(start,at-start),nullptr,hex?16:10));}catch(...){}
    }
    return out;
}
inline void modern_command_row(ModernWindow &w,const char *prefix,int key,std::string label) {
    ModernRow row;row.id=std::string(prefix)+std::to_string(key);row.label=std::move(label);row.key=row.hotkey=key;row.price_known=false;
    row.actions.push_back({"choice",row.label,"",true,key});w.rows.push_back(std::move(row));
}
inline bool modern_color_editor(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if(w.stage=="text"||w.stage=="number"||w.stage=="confirm")return false;
    const bool sil=profile=="sil";
    if(source.find("n/N/k/K/r/R/g/G/b/B")==std::string::npos&&!(sil&&(source.find("k,K")!=std::string::npos)))return false;
    std::string name;int index=-1;std::vector<int> rgb;
    for(const auto &line:frame.menu_term_lines) {
        auto text=modern_trim(line.text_utf8);
        if(sil)text=modern_trim(text.substr(0,modern_column_byte(text,40)));
        if(text.starts_with("Color =")||text.starts_with(reinterpret_cast<const char*>(u8"カラー ="))||text.starts_with(reinterpret_cast<const char*>(u8"色 ="))) {
            const auto numbers=modern_numbers(text);if(!numbers.empty())index=numbers.front();const auto equal=text.rfind('=');if(equal!=std::string::npos)name=modern_trim(text.substr(equal+1));
        }
        if(text.find("R,G,B")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"赤,緑,青"))!=std::string::npos)rgb=modern_numbers(text);
    }
    if(index<0||rgb.size()!=4)return false;
    const bool ja=w.language=="ja";const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    w.title=tr(u8"色の設定","Color settings");w.content_type="color_editor";w.stage="choices";w.cursor=-1;w.cursor_target.clear();w.rows.clear();w.actions.clear();w.content.clear();w.prev_key=w.next_key=0;
    ModernBlock info;info.kind="fields";info.cells={{tr(u8"色番号","Color index"),std::to_string(index),false},{tr(u8"名前","Name"),name,false}};w.content.push_back(std::move(info));
    char hex[8];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",rgb[1]&255,rgb[2]&255,rgb[3]&255);
    ModernBlock swatch;swatch.kind="swatch";swatch.cells.push_back({tr(u8"現在の色","Current color"),hex,false});w.content.push_back(std::move(swatch));
    ModernBlock values;values.kind="fields";const std::vector<std::string> labels={sil?tr(u8"明るさ","Extra value"):tr(u8"拡張値（K）","Extra value (K)"),tr(u8"赤（R）","Red (R)"),tr(u8"緑（G）","Green (G)"),tr(u8"青（B）","Blue (B)")};
    for(int i=0;i<4;++i)values.cells.push_back({labels[i],std::to_string(rgb[i]),false});w.content.push_back(std::move(values));
    const auto add=[&](int key,const std::string &label){modern_command_row(w,"color:",key,label);};
    if(sil){add('2',tr(u8"次の色を選ぶ","Select the next color"));add('8',tr(u8"前の色を選ぶ","Select the previous color"));add('6',tr(u8"右の濃淡を選ぶ","Select the shade to the right"));add('4',tr(u8"左の濃淡を選ぶ","Select the shade to the left"));}
    else {add('n',tr(u8"次の色を選ぶ","Select the next color"));add('N',tr(u8"前の色を選ぶ","Select the previous color"));}
    for(int i=0;i<4;++i){const char key=std::string_view("krgb")[i];add(key,ja?labels[i]+reinterpret_cast<const char*>(u8"を増やす"):"Increase "+labels[i]);add(key-'a'+'A',ja?labels[i]+reinterpret_cast<const char*>(u8"を減らす"):"Decrease "+labels[i]);}
    if(sil){add('c',tr(u8"別の色からコピーする","Copy another color"));add('v',tr(u8"値を入力して指定する","Enter exact values"));}
    return true;
}
inline bool modern_visual_number_editor(const GameFrame &frame,ModernWindow &w,const std::string &source) {
    if(w.stage=="text"||w.stage=="number"||w.stage=="confirm"||source.find("n/N/^N/a/A/^A/c/C/^C")==std::string::npos)return false;
    const bool ja=w.language=="ja";const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    std::string target,name;std::vector<ModernBlock> content;
    for(const auto &line:frame.menu_term_lines) {
        const auto text=modern_trim(line.text_utf8);
        if(line.source_row==17) {
            const auto comma=text.find(',');target=modern_trim(text.substr(0,comma));const auto equal=text.rfind('=');if(equal!=std::string::npos)name=modern_trim(text.substr(equal+1));
        }
        if(line.source_row==18&&!text.empty()){ModernBlock block;block.kind="paragraph";block.cells.push_back({"",modern_spaces(text),false});content.push_back(std::move(block));}
        if(line.source_row==19||line.source_row==20) {
            const auto equal=text.find('=');if(equal==std::string::npos)continue;const auto nums=modern_numbers(text.substr(equal+1));if(nums.size()<2)continue;
            ModernBlock block;block.kind="fields";block.title=line.source_row==19?tr(u8"初期値","Default values"):tr(u8"現在値","Current values");
            block.cells.push_back({tr(u8"色番号","Color code"),std::to_string(nums[0]),false});block.cells.push_back({tr(u8"文字番号","Character code"),std::to_string(nums[1]),false});
            block.cells.push_back({tr(u8"文字","Character"),nums[1]==32?tr(u8"空白","Space"):(nums[1]>32&&nums[1]<127?std::string(1,char(nums[1])):tr(u8"タイル・拡張文字","Tile / extended character")),false});content.push_back(std::move(block));
        }
    }
    if(target.empty()||content.size()<2)return false;
    w.stage="choices";w.cursor=-1;w.cursor_target.clear();w.rows.clear();w.actions.clear();w.content_type="visual_numbers";w.title=tr(u8"表示する文字と色の設定","Character and color settings");w.subtitle=target;
    w.content=std::move(content);ModernBlock item;item.kind="heading";item.cells.push_back({"",name,false});w.content.insert(w.content.begin(),std::move(item));
    const auto add=[&](int key,const std::string &label){modern_command_row(w,"visual:",key,label);};
    const std::vector<std::string> labels={tr(u8"対象","Target"),tr(u8"色","Color"),tr(u8"文字","Character")};
    for(int i=0;i<3;++i){const char key=std::string_view("nac")[i];add(key,ja?reinterpret_cast<const char*>(u8"次の")+labels[i]:"Next "+labels[i]);add(key-'a'+'A',ja?reinterpret_cast<const char*>(u8"前の")+labels[i]:"Previous "+labels[i]);add(key&31,ja?labels[i]+reinterpret_cast<const char*>(u8"の番号を入力"):"Enter "+labels[i]+" number");}
    if(source.find("l/L/^L")!=std::string::npos){add('l',tr(u8"次の照明条件","Next lighting condition"));add('L',tr(u8"前の照明条件","Previous lighting condition"));add(12,tr(u8"照明条件の番号を入力","Enter lighting condition number"));add('d',tr(u8"照明別の設定を初期化","Reset lighting variants"));for(int key:std::initializer_list<int>{'D',4})w.rows.back().actions.push_back({"choice",w.rows.back().label,"",true,key});}
    add('v',tr(u8"文字と色を選んで変更する","Select a character and color"));for(int key:std::initializer_list<int>{'V',22})w.rows.back().actions.push_back({"choice",w.rows.back().label,"",true,key});
    return true;
}
inline bool modern_boolean_settings(const GameFrame &frame,ModernWindow &w) {
    if(w.stage!="cursor"&&w.stage!="detail")return false;
    std::string header;for(const auto &line:frame.menu_term_lines)if(line.source_row<=1)header+=line.text_utf8;
    const bool readonly=header.find("Browse Only")!=std::string::npos||header.find("read-only")!=std::string::npos||header.find(reinterpret_cast<const char*>(u8"参照のみ"))!=std::string::npos||header.find(reinterpret_cast<const char*>(u8"閲覧のみ"))!=std::string::npos;
    if(!readonly&&(header.find("y/n")==std::string::npos||(header.find("ESC")==std::string::npos&&header.find("Esc")==std::string::npos)))return false;
    const bool ja=w.language=="ja";const bool birth_options=frame.pre_game_menu&&!readonly;
    std::vector<ModernBlock> blocks;std::vector<ModernRow> rows;std::string selected;int confirm=13,last_row=0;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto &source=frame.menu_term_lines[i];if(source.source_row<2)continue;
        const auto text=modern_trim(source.text_utf8);size_t end=text.size();std::string identifier;
        const auto id=text.rfind('(');
        if(id!=std::string::npos&&text.ends_with(')')) {
            auto name=text.substr(id+1,text.size()-id-2);
            if(!name.empty()&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789")==std::string::npos){identifier=std::move(name);end=id;}
        }
        const auto colon=text.rfind(':',end);if(colon==std::string::npos||colon>=end)continue;
        if(identifier.empty()&&text.substr(0,colon).find("  ")==std::string::npos)continue;
        const auto value=modern_trim(text.substr(colon+1,end-colon-1));if(value.empty())continue;
        if(identifier.empty()){unsigned long long hash=1469598103934665603ULL;for(unsigned char c:modern_spaces(text.substr(0,colon))){hash^=c;hash*=1099511628211ULL;}identifier="label-"+std::to_string(hash);}
        const bool yes=value=="yes"||value=="on"||value==reinterpret_cast<const char*>(u8"はい"),no=value=="no"||value=="off"||value==reinterpret_cast<const char*>(u8"いいえ");
        ModernRow row;row.id="option:"+identifier;row.label=modern_spaces(text.substr(0,colon));row.slot=int(i);row.selection=0;row.price_known=false;row.available=!readonly;
        const bool active=int(i)==(frame.menu_term_curs_row>=0?frame.menu_term_curs_row:frame.menu_core_cursor.line_index);
        ModernCell cell{row.label,yes?(ja?reinterpret_cast<const char*>(u8"オン"):"On"):(no?(ja?reinterpret_cast<const char*>(u8"オフ"):"Off"):value),active};cell.target=row.id;
        if(active){selected=row.id;confirm=yes?'n':'y';}last_row=std::max(last_row,source.source_row);
        ModernBlock block;block.kind="fields";block.cells.push_back(std::move(cell));blocks.push_back(std::move(block));rows.push_back(std::move(row));
    }
    if(rows.empty()||(!readonly&&selected.empty()))return false;
    // Explanations after the settings remain prose; the option identifiers and
    // the native control band are represented by the fields and actions.
    for(const auto &line:frame.menu_term_lines)if(line.source_row>last_row) {
        const auto text=modern_spaces(line.text_utf8);
        if(!text.empty()&&text.find("ESC")==std::string::npos&&text.find("Esc")==std::string::npos&&!modern_frame_rule(text)){ModernBlock b;b.kind="paragraph";b.cells.push_back({"",text,false});blocks.push_back(std::move(b));}
    }
    w.content_type=readonly?"settings_readonly":"settings_list";w.content=std::move(blocks);w.rows=std::move(rows);w.cursor_target=std::move(selected);w.continue_key=readonly?13:confirm;w.actions.clear();w.stage="cursor";
    if(readonly) {
        w.title=modern_spaces(header.substr(0,std::min(header.find('('),header.find(reinterpret_cast<const char*>(u8"（")))))+(ja?reinterpret_cast<const char*>(u8"（参照のみ）"):" (Read only)");
        w.actions.push_back({"key:13",ja?reinterpret_cast<const char*>(u8"次の項目へ"):"Next option","",true,13});
        if(header.find('?')!=std::string::npos)modern_add_action(w,'?',ja?reinterpret_cast<const char*>(u8"設定のヘルプ"):"Settings help");
    }
    else {
        const auto ascii=header.find('('),wide=header.find(reinterpret_cast<const char*>(u8"（"));w.title=modern_spaces(header.substr(0,std::min(ascii,wide)));
        modern_add_action(w,'y',ja?reinterpret_cast<const char*>(u8"オンにする・次の値"):"Set on / next value");modern_add_action(w,'n',ja?reinterpret_cast<const char*>(u8"オフにする・前の値"):"Set off / previous value");
        if(birth_options) {
            // The native birth-options loop finishes with Escape.  Expose that
            // transition as the final selectable row so Enter means exactly
            // what the label says; keep it out of native cursor focusing.
            ModernRow done;done.id="@action:accept-birth-options";done.label=ja?reinterpret_cast<const char*>(u8"この内容で決定"):"Confirm these options";
            done.slot=1000000000;done.key=done.hotkey=27;done.price_known=false;
            done.actions={{"choice",done.label,"",true,27}};w.rows.push_back(std::move(done));
            w.actions.push_back({"accept-birth-options",ja?reinterpret_cast<const char*>(u8"この内容で決定"):"Confirm these options","",true,27});
        } else w.actions.push_back({"key:13",ja?reinterpret_cast<const char*>(u8"次の項目へ"):"Next option","",true,13});
        if(header.find('?')!=std::string::npos)modern_add_action(w,'?',ja?reinterpret_cast<const char*>(u8"設定のヘルプ"):"Settings help");
        if(header.find("'F'")!=std::string::npos||header.find(" F ")!=std::string::npos)modern_add_action(w,'F',ja?reinterpret_cast<const char*>(u8"自動セーブの頻度を変える"):"Change autosave frequency");
    }
    return true;
}

inline bool modern_is_editor_menu(const GameFrame &frame) {
    if(!frame.text_input_active||frame.menu_choices.empty())return false;
    for(const auto &line:frame.menu_term_lines)if(line.source_row==0) {
        const auto text=modern_trim(line.text_utf8);
        return text.starts_with("(a-")&&(text.find("Command:")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"コマンド:"))!=std::string::npos);
    }
    return false;
}
inline void modern_editor_content(const GameFrame &frame,ModernWindow &w,const std::string &source) {
    const bool ja=w.language=="ja";const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    if(modern_is_editor_menu(frame)) {
        w.content_type="editor_menu";w.title=tr(u8"自動拾いの編集メニュー","Auto-pick editing menu");w.content.clear();w.actions.clear();for(auto &row:w.rows)row.description.clear();return;
    }
    if(w.stage!="text"||source.find("^Q")==std::string::npos||source.find("^W")==std::string::npos||frame.menu_term_curs_row<0)return;
    const auto &current=frame.menu_term_lines[frame.menu_term_curs_row];int current_line=current.source_row,last=frame.menu_term_lines.back().source_row-1;
    for(const auto &line:frame.menu_term_lines)if(line.source_row==0) {
        const auto start=line.text_utf8.rfind('(');if(start!=std::string::npos){const auto numbers=modern_numbers(line.text_utf8.substr(start));if(numbers.size()==2)current_line=numbers[1]+1;}
    }
    for(const auto &line:frame.menu_term_lines)if(line.source_row>10&&line.text_utf8.size()>40&&line.text_utf8.find_first_not_of('-')==std::string::npos)last=line.source_row;
    w.text_fullwidth_kana=false;w.text_edit_mode="editor";w.content_type="editor";w.title=tr(u8"自動拾いの設定","Auto-pick settings");w.input_label=tr(u8"編集中の行 ","Editing line ")+std::to_string(current_line);w.input=current.text_utf8;
    const int missing=std::clamp(frame.menu_term_curs_col-modern_byte_column(w.input,w.input.size()),0,512);w.input.append(size_t(missing),' ');
    const size_t caret=modern_column_byte(w.input,std::max(0,frame.menu_term_curs_col));
    while(w.input.size()>caret&&w.input.back()==' ')w.input.pop_back();w.input_caret=int(std::min(w.input.size(),caret));
    w.content.clear();w.actions.clear();w.rows.clear();w.prev_key=15;w.next_key=12;
    for(int y=1;y<last;++y) {
        const auto found=std::find_if(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[&](const auto &line){return line.source_row==y;});
        const int number=current_line+y-current.source_row;ModernBlock line;line.kind="code";
        line.cells.push_back({std::to_string(std::max(1,number)),found==frame.menu_term_lines.end()?"":found->text_utf8,y==current.source_row});w.content.push_back(std::move(line));
    }
    modern_add_action(w,17,tr(u8"保存せず閉じる","Close without saving"));modern_add_action(w,23,tr(u8"保存して閉じる","Save and close"));
    w.actions.push_back({"key:27",tr(u8"編集メニューを開く","Open editing menu"),"",true,27});
    modern_add_action(w,19,tr(u8"文字列を検索する","Search text"));modern_add_action(w,16,tr(u8"前の行へ移る","Previous line"));modern_add_action(w,14,tr(u8"次の行へ移る","Next line"));
}

inline bool modern_sil_settings(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if(profile!="sil"||(w.stage!="cursor"&&w.stage!="detail"))return false;
    const bool editable=source.find("direction keys to set")!=std::string::npos||source.find(reinterpret_cast<const char*>(u8"向きの鍵で選び"))!=std::string::npos;
    const bool readonly=source.find("press Return to go back")!=std::string::npos||source.find(reinterpret_cast<const char*>(u8"Return で戻る"))!=std::string::npos;
    if(!editable&&!readonly)return false;
    const bool ja=w.language=="ja";std::vector<ModernBlock> content;std::vector<ModernRow> rows;std::string target;int key=13;
    const int current=frame.menu_term_curs_row>=0?frame.menu_term_curs_row:frame.menu_core_cursor.line_index;
    for(size_t index=0;index<frame.menu_term_lines.size();++index) {
        const auto &line=frame.menu_term_lines[index];if(line.source_row<=1)continue;
        const auto text=modern_trim(line.text_utf8);const auto colon=text.rfind(':');
        if(colon!=std::string::npos) {
            const auto label=modern_trim(text.substr(0,colon)),value=modern_trim(text.substr(colon+1));
            const bool yes=value=="yes"||value==reinterpret_cast<const char*>(u8"入"),no=value=="no"||value==reinterpret_cast<const char*>(u8"切");
            const auto numeric=modern_numbers(value);
            if(!yes&&!no&&numeric.size()!=1)continue;
            ModernRow row;row.id="sil-option:"+std::to_string(line.source_row);row.label=label;row.slot=int(index);row.selection=0;row.price_known=false;row.available=!readonly;
            const bool selected=int(index)==current&&!readonly;
            ModernCell cell{label,yes?(ja?reinterpret_cast<const char*>(u8"オン"):"On"):(no?(ja?reinterpret_cast<const char*>(u8"オフ"):"Off"):value),selected};cell.target=row.id;
            if(selected){target=row.id;key=yes?'4':'6';}
            ModernBlock block;block.kind="fields";block.cells.push_back(std::move(cell));content.push_back(std::move(block));rows.push_back(std::move(row));
        } else if(!text.empty()&&text.find("Return")==std::string::npos) {
            ModernBlock block;block.kind="paragraph";block.cells.push_back({"",modern_spaces(text),false});content.push_back(std::move(block));
        }
    }
    if(rows.empty()||(!readonly&&target.empty()))return false;
    w.content_type=readonly?"settings_readonly":"settings_list";w.content=std::move(content);w.rows=std::move(rows);w.cursor_target=std::move(target);w.stage=readonly?"detail":"cursor";w.continue_key=key;w.actions.clear();
    if(!readonly){modern_add_action(w,'6',ja?reinterpret_cast<const char*>(u8"オンにする・値を上げる"):"Set on / increase");modern_add_action(w,'4',ja?reinterpret_cast<const char*>(u8"オフにする・値を下げる"):"Set off / decrease");w.actions.push_back({"key:13",ja?reinterpret_cast<const char*>(u8"設定を終えて戻る"):"Finish settings","",true,13});}
    return true;
}

inline void modern_known_window_actions(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(w.content_type!="settings_table")return;
    std::string header;for(const auto &line:frame.menu_term_lines)if(line.source_row<=1)header+=line.text_utf8;
    const bool ja=w.language=="ja";
    // Replace parsed command fragments with complete labels for this table.
    w.actions.clear();
    struct Action {char key;const char8_t *ja;const char *en;};
    for(const auto &a:std::initializer_list<Action>{{'t',u8"オン・オフを切り替える","Toggle"},{'y',u8"オンにする","Set on"},{'n',u8"オフにする","Set off"},{'s',u8"この小窓だけオンにする","Use only this window"}}) {
        const auto quoted=std::string("'")+a.key+"'",bare=std::string(" ")+a.key+",";
        const bool declared=profile=="heng"?(a.key=='t'||a.key=='s'):((profile=="legacy"||profile=="frox")?a.key!='s':false);
        if(declared||header.find(quoted)!=std::string::npos||header.find(bare)!=std::string::npos||header.find(std::string("<")+a.key+">")!=std::string::npos) {
            const auto label=a.key=='t'&&profile=="legacy"?(ja?reinterpret_cast<const char*>(u8"この小窓だけに割り当てる"):"Assign only to this window"):(ja?reinterpret_cast<const char*>(a.ja):a.en);
            modern_add_action(w,a.key,label);
        }
    }
    if(profile=="heng"||profile=="legacy"||profile=="frox")modern_add_action(w,'?',ja?reinterpret_cast<const char*>(u8"設定のヘルプ"):"Window settings help");
}

inline void modern_save_content(const GameFrame &frame,ModernWindow &w,const std::string &source) {
    if(!frame.pre_game_menu)return;
    const bool ja=w.language=="ja";
    const bool start=w.rows.size()==3&&w.rows[0].key=='N'&&w.rows[1].key=='L'&&w.rows[2].key=='Q';
    if(start) {
        w.content_type="start";
        if(ja){w.rows[0].label=reinterpret_cast<const char*>(u8"新しく始める");w.rows[1].label=reinterpret_cast<const char*>(u8"セーブデータを読み込む");w.rows[2].label=reinterpret_cast<const char*>(u8"終了する");}
        return;
    }
    const bool load=source.find("=== Load Game ===")!=std::string::npos;
    const bool deleting=source.find("=== Delete Save ===")!=std::string::npos;
    const bool named=w.title==reinterpret_cast<const char*>(u8"セーブデータを読み込む")||w.title==reinterpret_cast<const char*>(u8"削除するデータを選ぶ")
        ||w.title==reinterpret_cast<const char*>(u8"セーブデータの削除")||w.title=="Load Game"||w.title=="Choose the save to delete"||w.title=="Delete Save";
    if(!load&&!deleting&&!named)return;
    w.content_type="saves";
    if(ja&&load)w.title=reinterpret_cast<const char*>(u8"セーブデータを読み込む");
    if(ja&&deleting)w.title=reinterpret_cast<const char*>(u8"セーブデータの削除");
    for(auto &row:w.rows) {
        if(load&&row.label=="Delete a save file")row.label=ja?reinterpret_cast<const char*>(u8"セーブデータを削除する"):"Delete a save file";
        if(deleting&&source.find("This will delete:")!=std::string::npos) {
            if(row.label=="No, keep it")row.label=ja?reinterpret_cast<const char*>(u8"削除しない"):"No, keep it";
            if(row.label=="Yes, delete it")row.label=ja?reinterpret_cast<const char*>(u8"削除する"):"Yes, delete it";
        }
    }
    for(const auto &line:frame.menu_term_lines) {
        const auto text=modern_trim(line.text_utf8);
        if(text.starts_with("This will delete:")) {
            w.content.clear();ModernBlock item;item.kind="fields";
            item.cells.push_back({ja?reinterpret_cast<const char*>(u8"削除するデータ"):"Save to delete",modern_trim(text.substr(17)),false});w.content.push_back(std::move(item));
            ModernBlock note;note.kind="paragraph";note.cells.push_back({"",ja?reinterpret_cast<const char*>(u8"セーブと、そのデータに属する一時ファイルを削除します。削除すると元に戻せません。"):"The save and its own temporary files will be removed. This cannot be undone.",false});w.content.push_back(std::move(note));return;
        }
    }
    std::string message;
    if(source.find("No save files found")!=std::string::npos)message=ja?reinterpret_cast<const char*>(u8"セーブデータはありません。"):"There are no saved games.";
    if(source.find("The save has a lock file.")!=std::string::npos)message=ja?reinterpret_cast<const char*>(u8"このデータは使用中として記録されています。使用しているゲームを終了してから削除してください。"):"This save is marked as in use. Close the game using it before deleting it.";
    if(!message.empty()){w.content.clear();ModernBlock paragraph;paragraph.kind="paragraph";paragraph.cells.push_back({"",std::move(message),false});w.content.push_back(std::move(paragraph));}
}

struct ModernDocumentInfo {
    bool active{},frox{},menu{},search{},search_active{},large{};
    int header{-1},footer{-1},line{},total{},visible{23};
    std::string caption;
};
inline bool modern_document_entry(const GameFrame &frame,const MenuChoice &choice) {
    if(choice.line_index<0||choice.line_index>=int(frame.menu_term_lines.size())||!choice.key)return false;
    const auto text=modern_trim(frame.menu_term_lines[choice.line_index].text_utf8);const char key=char(choice.key);
    return text.starts_with(std::string(1,key)+")")||text.starts_with(std::string("(")+key+")")||text.starts_with(std::string("[")+key+"]");
}
inline ModernDocumentInfo modern_document_info(const GameFrame &frame) {
    ModernDocumentInfo info;const int last=frame.menu_term_lines.empty()?0:frame.menu_term_lines.back().source_row;
    info.visible=std::max(1,last-3);
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto &row=frame.menu_term_lines[i];const auto text=modern_trim(row.text_utf8);
        if(row.source_row<=1&&text.starts_with('[')&&text.ends_with(']')) {
            const auto slash=text.rfind('/');
            if(slash!=std::string::npos&&(text.find(',')!=std::string::npos||text.find("Line ")!=std::string::npos||text.find(".txt")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8" 行"))!=std::string::npos)) {
                size_t begin=slash,end=slash+1;
                while(begin>0&&std::isdigit(static_cast<unsigned char>(text[begin-1])))--begin;
                while(end<text.size()&&std::isdigit(static_cast<unsigned char>(text[end])))++end;
                if(begin<slash&&end>slash+1) {
                    try{info.line=std::stoi(text.substr(begin,slash-begin));info.total=std::stoi(text.substr(slash+1,end-slash-1));}catch(...){continue;}
                    info.active=true;info.header=int(i);info.caption=modern_trim(text.substr(1,begin-1));
                    const auto at=info.caption.rfind("Line");if(at!=std::string::npos)info.caption=modern_trim(info.caption.substr(0,at));
                    while(!info.caption.empty()&&(info.caption.back()==','||info.caption.back()==' '))info.caption.pop_back();
                }
            }
        }
        if(row.source_row>=last-2) {
            const bool escape=text.find("ESC")!=std::string::npos||text.find("Esc")!=std::string::npos;
            if(escape&&(text.find("Ctrl+F")!=std::string::npos||text.find("Ctrl-F")!=std::string::npos)&&text.find("F3")!=std::string::npos){info.active=true;info.frox=true;info.search=true;info.footer=int(i);}
            if(escape&&(text.find("Arrows/PgUp/PgDn")!=std::string::npos||text.find("to save to file")!=std::string::npos)){info.active=true;info.frox=true;info.footer=int(i);}
            if(escape&&(text.find("Right or Space for the next page")!=std::string::npos
                ||((text.find(reinterpret_cast<const char*>(u8"スペース"))!=std::string::npos||text.find("Space")!=std::string::npos)&&text.find(reinterpret_cast<const char*>(u8"上下"))!=std::string::npos))){info.active=true;info.large=true;info.footer=int(i);}
            if(text=="(press ESC to exit)"||text=="[Press a Number, or ESC to exit.]"||text==reinterpret_cast<const char*>(u8"（戻る ESC）")) {info.active=true;info.footer=int(i);}
            if(info.active&&escape&&info.footer<0)info.footer=int(i);
            if(row.source_row==last-1&&(text.find("Find:")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"検索:"))!=std::string::npos))info.search_active=true;
        }
    }
    if(info.active&&!frame.menu_choices.empty())info.menu=std::all_of(frame.menu_choices.begin(),frame.menu_choices.end(),[&](const auto &c){return modern_document_entry(frame,c);});
    info.large=info.large||info.total>info.visible;
    return info;
}
inline bool modern_document_heading(const TermMirrorLine &source) {
    const auto text=modern_trim(source.text_utf8);if(text.empty()||text.size()>160)return false;
    const auto start=source.text_utf8.find_first_not_of(' ');int color=source.color;
    for(const auto &span:source.color_spans)if(int(start)>=span.begin&&int(start)<span.begin+span.len){color=span.color;break;}
    if(color!=3&&color!=4&&color!=14)return false;
    for(const auto &span:source.color_spans)if(span.color!=color) {
        const auto part=source.text_utf8.substr(std::clamp(span.begin,0,int(source.text_utf8.size())),std::clamp(span.len,0,int(source.text_utf8.size())-std::clamp(span.begin,0,int(source.text_utf8.size()))));
        if(!modern_trim(part).empty())return false;
    }
    return true;
}
inline void modern_document_pairs(ModernBlock &block) {
    const auto value=[](const std::string &s){
        if(s.empty())return false;
        if(std::isdigit(static_cast<unsigned char>(s[0]))||s[0]=='+'||s[0]=='-'||s[0]=='('||s[0]=='[')return true;
        // Whole-cell values of a label/value sheet line (Angband's "Speed  Normal"
        // and "Max Depth  Town"; section 21). Exact match
        // only: a longer cell that merely contains these words is not a value.
        for(const char *word:{"Normal","Town",reinterpret_cast<const char*>(u8"通常"),reinterpret_cast<const char*>(u8"町")})if(s==word)return true;
        for(const char *word:{"Good","Poor","Fair","Bad","Excellent","Superb","Heroic","Legendary",reinterpret_cast<const char*>(u8"良い"),reinterpret_cast<const char*>(u8"普通"),reinterpret_cast<const char*>(u8"悪い"),reinterpret_cast<const char*>(u8"劣る"),reinterpret_cast<const char*>(u8"卓越"),reinterpret_cast<const char*>(u8"超越"),reinterpret_cast<const char*>(u8"英雄"),reinterpret_cast<const char*>(u8"伝説")})if(s.find(word)!=std::string::npos)return true;
        return false;
    };
    std::vector<ModernCell> pairs;
    for(size_t i=0;i<block.cells.size();++i) {
        auto cell=block.cells[i];
        if(i+1<block.cells.size()&&cell.target.empty()&&block.cells[i+1].target.empty()&&block.cells[i+1].label.empty()
            &&((!cell.label.empty()&&cell.text.empty())||(cell.label.empty()&&!value(cell.text)&&value(block.cells[i+1].text)))) {
            if(cell.label.empty())cell.label=cell.text;
            cell.text=block.cells[i+1].text;cell.selected=cell.selected||block.cells[i+1].selected;++i;
        }
        pairs.push_back(std::move(cell));
    }
    block.cells=std::move(pairs);
}
inline void modern_document_controls(const GameFrame &frame,const ModernDocumentInfo &doc,ModernWindow &w,const std::string &profile) {
    if(!doc.active)return;
    const bool ja=w.language=="ja",modal=w.stage=="text"||w.stage=="number"||w.stage=="confirm";
    w.content_type=modal?"document_form":(doc.menu?"document_menu":"document");w.subtitle=doc.caption;
    if(doc.total>0)w.subtitle+=(w.subtitle.empty()?"":" / ")+std::to_string(doc.line+1)+"-"+std::to_string(std::min(doc.total,doc.line+doc.visible))+" / "+std::to_string(doc.total)+(ja?reinterpret_cast<const char*>(u8" 行"):" lines");
    if(modal){w.prev_key=w.next_key=0;w.actions.clear();return;}
    w.prev_key=frame.menu_page_prev_key?frame.menu_page_prev_key:'-';w.next_key=frame.menu_page_next_key?frame.menu_page_next_key:' ';
    w.previous=w.next=doc.large;w.continue_key=doc.search_active?13:w.next_key;
    const auto add=[&](const char *id,int key,const char8_t *jp,const char *en){
        if(std::any_of(w.actions.begin(),w.actions.end(),[&](const auto &a){return a.id==id;}))return;
        w.actions.push_back({id,ja?reinterpret_cast<const char*>(jp):en,"",true,key});
    };
    add("doc-previous",w.prev_key,u8"前のページ","Previous page");add("doc-next",w.next_key,u8"次のページ","Next page");
    if(profile=="frox"||doc.frox) {
        if(doc.search){add("doc-search",'/',u8"検索する","Find");add("doc-find-next",6,u8"次の検索結果","Next match");add("doc-find-previous",'\\',u8"前の検索結果","Previous match");}
        add("doc-help",'?',u8"閲覧操作のヘルプ","Viewer help");add("doc-guide",'!',u8"遊び方の説明","Getting started");
        add("doc-export",'|',u8"ファイルに保存する","Save to a file");add("doc-next-section",'>',u8"次の見出し","Next heading");add("doc-previous-section",'<',u8"前の見出し","Previous heading");
    } else {
        add("doc-search",'/',u8"検索する","Find");add("doc-line",'#',u8"指定した行へ移動する","Go to line");
        if(profile=="sil") {add("doc-highlight",'&',u8"強調する文字を指定する","Highlight text");add("doc-case",'!',u8"大文字と小文字の区別を切り替える","Toggle case sensitivity");}
        else if(profile=="heng"||profile=="legacy") {add("doc-highlight",'=',u8"強調する文字を指定する","Highlight text");add("doc-file",'%',u8"別の文書を開く","Open another document");add("doc-export",'|',u8"ファイルに保存する","Save to a file");add("doc-help",'?',u8"閲覧操作のヘルプ","Viewer help");}
    }
    // Keep inline link markers in the paragraph. The link style is emitted by
    // z-doc.c, including references whose marker sits at the end of a line.
    if(profile=="frox"||doc.frox)for(const auto &line:frame.menu_term_lines) {
        for(size_t i=0;i+2<line.text_utf8.size();++i)if(line.text_utf8[i]=='['&&line.text_utf8[i+2]==']'&&line.text_utf8[i+1]>='a'&&line.text_utf8[i+1]<='z') {
            bool link=false;for(const auto &span:line.color_spans)if(span.color==13&&int(i)>=span.begin&&int(i+3)<=span.begin+span.len)link=true;
            if(!link)continue;const int key=line.text_utf8[i+1];const std::string id="doc-link:"+std::to_string(key);
            if(std::any_of(w.actions.begin(),w.actions.end(),[&](const auto &a){return a.id==id;}))continue;
            const std::string label=(ja?reinterpret_cast<const char*>(u8"参照 "):"Reference ")+std::string(1,char(key));
            w.actions.push_back({id,label,"",true,key});
            ModernBlock block;block.kind="links";ModernCell cell{"",label,false};cell.target=id;block.cells.push_back(std::move(cell));w.content.push_back(std::move(block));
        }
    }
    for(const auto &choice:frame.menu_choices)if(choice.line_index>=0&&choice.line_index<int(frame.menu_term_lines.size())&&choice.key) {
        const auto &line=frame.menu_term_lines[choice.line_index];
        if(line.source_row<frame.menu_term_lines.back().source_row-2)continue;
        auto label=choice.label_utf8;
        if(label.empty())label=line.text_utf8.substr(std::clamp(choice.span_begin,0,int(line.text_utf8.size())),std::clamp(choice.span_len,0,int(line.text_utf8.size())-std::clamp(choice.span_begin,0,int(line.text_utf8.size()))));
        modern_add_action(w,choice.key,modern_choice_label(label));
    }
}

inline void modern_frox_custom_document_controls(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(profile!="frox")return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    std::string source;
    for(const auto &line:frame.menu_term_lines){source+=line.text_utf8;source+='\n';}

    const bool character_sheet=source.find("[Character Sheet, Line ")!=std::string::npos
        ||source.find("Press c to save a snapshot, C to compare")!=std::string::npos
        ||source.find("Save to: c/0/Enter = default slot")!=std::string::npos;
    if(character_sheet) {
        const bool save_mode=source.find("Save to: c/0/Enter = default slot")!=std::string::npos;
        const bool compare_armed=source.find("Press C again to compare with the default snapshot")!=std::string::npos;
        if(save_mode) {
            modern_add_action(w,'c',tr(u8"既定スロットへ保存","Save to default snapshot slot"));
            modern_add_action(w,'0',tr(u8"既定スロットへ保存","Save to default snapshot slot"));
            modern_add_action(w,'7',tr(u8"追加スロット7へ保存","Save to extra slot 7"));
            modern_add_action(w,'8',tr(u8"追加スロット8へ保存","Save to extra slot 8"));
            modern_add_action(w,'9',tr(u8"追加スロット9へ保存","Save to extra slot 9"));
            if(std::none_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.key==13;}))
                w.actions.push_back({"charsheet-save-default",tr(u8"既定スロットへ保存","Save to default snapshot slot"),"",true,13});
            if(std::none_of(w.actions.begin(),w.actions.end(),[](const auto &a){return a.key==27;}))
                w.actions.push_back({"charsheet-save-cancel",tr(u8"保存を取り消す","Cancel snapshot save"),"",true,27});
        } else {
            modern_add_action(w,'c',tr(u8"現在の表示を保存","Save snapshot"));
            modern_add_action(w,'C',compare_armed?tr(u8"既定スロットと比較する","Compare with default snapshot"):tr(u8"保存内容と比較する","Compare snapshot"));

            // The core deliberately uses shifted number-key characters to
            // open saved comparison slots.  Expose those as named actions so
            // touch/controller users do not need to synthesize Shift+digit.
            modern_add_action(w,'=',tr(u8"既定スロットを表示","View default snapshot"));
            modern_add_action(w,'&',tr(u8"追加スロット7を表示","View snapshot slot 7"));
            modern_add_action(w,'*',tr(u8"追加スロット8を表示","View snapshot slot 8"));
            modern_add_action(w,'(',tr(u8"追加スロット9を表示","View snapshot slot 9"));
        }
    }

    const bool scores=source.find("High Score Listing")!=std::string::npos
        &&source.find("Press ^N to sort by Name")!=std::string::npos;
    if(scores) {
        modern_add_action(w,19,tr(u8"スコア順に並べる","Sort by score"));      // Ctrl-S
        modern_add_action(w,4,tr(u8"日付順に並べる","Sort by date"));         // Ctrl-D
        modern_add_action(w,18,tr(u8"種族順に並べる","Sort by race"));        // Ctrl-R
        modern_add_action(w,3,tr(u8"職業順に並べる","Sort by class"));        // Ctrl-C
        modern_add_action(w,14,tr(u8"名前順に並べる","Sort by name"));        // Ctrl-N
        modern_add_action(w,25,tr(u8"古い記録文書を整理する","Purge old score documents")); // Ctrl-Y
    }
}

inline void modern_building_heading(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(frame.pre_game_menu||w.stage!="choices"||(profile!="frox"&&profile!="legacy"))return;
    bool exit=false;
    for(const auto &line:frame.menu_term_lines)if(line.source_row==23)
        exit=line.text_utf8.find("Exit building")!=std::string::npos||line.text_utf8.find(reinterpret_cast<const char*>(u8"建物を出"))!=std::string::npos;
    if(!exit)return;
    const std::string reported_title=w.title;
    std::vector<std::string> parts;
    for(const auto &line:frame.menu_term_lines)if(line.source_row==(profile=="frox"?3:2)) {
        for(const auto &cell:modern_split_cells(modern_trim(line.text_utf8))) {
            const auto part=modern_trim(cell.text);parts.push_back(part);
            if(!w.venue_header.empty())w.venue_header+='\n';w.venue_header+=part;
        }
        for(auto &block:w.content)block.cells.erase(std::remove_if(block.cells.begin(),block.cells.end(),[&](const auto &cell){return cell.label.empty()&&std::find(parts.begin(),parts.end(),cell.text)!=parts.end();}),block.cells.end());
        w.content.erase(std::remove_if(w.content.begin(),w.content.end(),[](const auto &block){return block.cells.empty();}),w.content.end());
    }
    if(parts.empty())return;
    const std::string building_title=parts.back();
    const bool generic=reported_title=="Menu"||reported_title==reinterpret_cast<const char*>(u8"メニュー")||reported_title==building_title;
    if(!generic&&!reported_title.empty()) {
        ModernBlock result;result.kind="paragraph";result.cells.push_back({"",reported_title,false});
        w.content.insert(w.content.begin(),std::move(result));
    }
    w.title=building_title;
}

inline bool modern_building_result_block(const ModernBlock &block) {
    if(block.kind!="paragraph")return false;
    return std::any_of(block.cells.begin(),block.cells.end(),[](const auto &cell){
        return !cell.text.empty()&&cell.text.find(reinterpret_cast<const char*>(u8"何かキー"))==std::string::npos
            &&cell.text.find("Press any key")==std::string::npos;
    });
}

inline void modern_retain_building_result(ModernWindow &window,const ModernWindow &previous) {
    if(window.stage!="choices"||window.venue_header.empty()||window.venue_header!=previous.venue_header)return;
    if(std::any_of(window.content.begin(),window.content.end(),modern_building_result_block))return;
    std::vector<ModernBlock> retained;
    for(const auto &block:previous.content)if(modern_building_result_block(block))retained.push_back(block);
    window.content.insert(window.content.begin(),retained.begin(),retained.end());
}

inline void modern_birth_choice_compact(const GameFrame &frame,ModernWindow &w) {
    if(!frame.pre_game_menu||w.content_type!="choice_grid")return;
    const bool relevant=w.title=="Choose a race"||w.title=="Choose a class"||w.title=="Choose a personality"
        ||w.title==reinterpret_cast<const char*>(u8"種族の選択")||w.title==reinterpret_cast<const char*>(u8"職業の選択")
        ||w.title==reinterpret_cast<const char*>(u8"性格の選択");
    if(!relevant)return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    const std::vector<std::string> identity_labels={tr(u8"名前","Name"),tr(u8"性別","Sex"),tr(u8"種族","Race"),tr(u8"職業","Class")};
    const auto value_of=[&](const std::string &label){
        for(const auto &block:w.content)for(const auto &cell:block.cells)if(cell.label==label&&!cell.text.empty())return cell.text;
        return std::string{};
    };
    ModernBlock identity;identity.kind="paragraph";
    const auto field=[&](const std::string &label){const auto value=value_of(label);return value.empty()?std::string{}:label+": "+value;};
    const auto name=field(identity_labels[0]);if(!name.empty())identity.cells.push_back({"",name,false});
    std::string person;
    for(size_t i=1;i<identity_labels.size();++i){const auto entry=field(identity_labels[i]);if(entry.empty())continue;if(!person.empty())person+="    ";person+=entry;}
    if(!person.empty())identity.cells.push_back({"",person,false});

    std::string heading,modifiers,misc,note;
    for(const auto &block:w.content) {
        if(block.kind=="grid")continue;
        for(const auto &cell:block.cells) {
            if(cell.label.empty()&&(cell.text.find(reinterpret_cast<const char*>(u8"修正"))!=std::string::npos||cell.text.find("modifier")!=std::string::npos))heading+=cell.text;
            if(block.kind=="stat_values"&&!cell.label.empty()&&!cell.text.empty()
                &&std::find(identity_labels.begin(),identity_labels.end(),cell.label)==identity_labels.end()) {
                if(!modifiers.empty())modifiers+="    ";modifiers+=cell.label+" "+cell.text;
            }
            if(block.kind=="fields"&&cell.label.empty()&&(cell.text.starts_with("HD")||cell.text.find(reinterpret_cast<const char*>(u8"隠密"))!=std::string::npos||cell.text.find("Stealth")!=std::string::npos||cell.text.find(reinterpret_cast<const char*>(u8"赤外線"))!=std::string::npos||cell.text.find("Infra")!=std::string::npos)) {
                if(!misc.empty())misc+="    ";misc+=cell.text;
            }
            if(block.kind=="paragraph"&&(cell.text.starts_with(reinterpret_cast<const char*>(u8"注意"))||cell.text.starts_with("Note")))note=cell.text;
        }
    }
    ModernBlock adjustment;adjustment.kind="paragraph";adjustment.title=modern_spaces(heading);
    if(!modifiers.empty())adjustment.cells.push_back({"",modifiers,false});
    if(!misc.empty())adjustment.cells.push_back({"",misc,false});
    ModernBlock warning;warning.kind="paragraph";if(!note.empty())warning.cells.push_back({"",note,false});
    w.content.clear();
    if(!identity.cells.empty())w.content.push_back(std::move(identity));
    if(!adjustment.cells.empty())w.content.push_back(std::move(adjustment));
    if(!warning.cells.empty())w.content.push_back(std::move(warning));
}

inline void modern_heng_minimum_stats(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if((profile!="legacy"&&profile!="heng")||!frame.pre_game_menu)return;
    const bool screen=source.find(reinterpret_cast<const char*>(u8"最低限得たい能力値を設定"))!=std::string::npos
        ||source.find("Set minimum stats")!=std::string::npos;
    if(!screen)return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    const auto &native_cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    const int selected_source=native_cursor.line_index>=0&&native_cursor.line_index<int(frame.menu_term_lines.size())
        ?frame.menu_term_lines[native_cursor.line_index].source_row:-1;
    w.stage="cursor";w.content_type="stat_allocation";w.title=tr(u8"最低希望能力値の設定","Minimum desired attributes");
    w.rows.clear();w.content.clear();w.cursor=-1;w.cursor_target.clear();w.continue_key=13;
    ModernBlock header;header.kind="table_header";
    for(const auto &label:std::vector<std::string>{tr(u8"能力","Attribute"),tr(u8"基本値","Base"),tr(u8"種族","Race"),tr(u8"職業","Class"),tr(u8"性格","Personality"),tr(u8"合計値","Total"),tr(u8"最大値","Maximum")})header.cells.push_back({"",label,false});
    w.content.push_back(std::move(header));
    int stat=0;
    for(const auto &line:frame.menu_term_lines) {
        if(line.source_row<14||line.source_row>19)continue;
        std::vector<std::string> values;
        for(const auto &cell:modern_split_cells(line.text_utf8)) {
            auto value=modern_spaces(cell.text);
            while(!value.empty()&&(value.back()==':'||value.back()==' '))value.pop_back();
            if(!value.empty()&&value!="=")values.push_back(std::move(value));
        }
        if(values.size()<7)continue;
        const std::string name=values.front();
        ModernBlock row;row.kind="table_row";
        const std::vector<std::string> shown={name,values[1],values[2],values[3],values[4],values[5],values[6]};
        const std::string target="minimum-stat:"+std::to_string(stat)+":0";
        for(size_t i=0;i<shown.size();++i)row.cells.push_back({"",shown[i],selected_source==line.source_row&&i==0,target});
        w.content.push_back(std::move(row));
        for(int column=0;column<2;++column) {
            ModernRow choice;choice.id="minimum-stat:"+std::to_string(stat)+":"+std::to_string(column);
            choice.label=name+(column?" +":reinterpret_cast<const char*>(u8" −"));choice.slot=line.source_row;choice.selection=column;
            choice.key=choice.hotkey=column?'6':'4';choice.price_known=false;choice.actions={{"choice",choice.label,"",true,choice.key}};
            w.rows.push_back(std::move(choice));
        }
        if(selected_source==line.source_row)w.cursor_target=target;
        ++stat;
    }
    if(stat!=6)return;
    ModernRow done;done.id="minimum-stat:continue";done.label=tr(u8"この最低値で次に進む","Continue with these minimums");done.slot=21;
    done.key=done.hotkey=13;done.price_known=false;done.actions={{"choice",done.label,"",true,13}};w.rows.push_back(std::move(done));
    if(selected_source==21)w.cursor_target=w.rows.back().id;
    if(w.cursor_target.empty())w.cursor_target=w.rows.front().id;
    for(const auto &line:frame.menu_term_lines)if(line.source_row==23&&!modern_trim(line.text_utf8).empty()) {
        ModernBlock chance;chance.kind="paragraph";chance.cells.push_back({"",modern_spaces(line.text_utf8),false});w.content.push_back(std::move(chance));break;
    }
}

inline void modern_monster_name_prompt(const GameFrame &frame,ModernWindow &w,const std::string &source) {
    if(source.find("^M")==std::string::npos)return;
    const bool monster=source.find("monster")!=std::string::npos||source.find("Monster")!=std::string::npos
        ||source.find(reinterpret_cast<const char*>(u8"モンスター"))!=std::string::npos
        ||(source.find("identified")!=std::string::npos&&source.find("^A")!=std::string::npos&&source.find("^U")!=std::string::npos);
    if(!monster)return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    w.stage="choices";w.content_type="monster_query";w.rows.clear();w.actions.clear();w.cursor=0;w.cursor_target.clear();w.capture_keys=false;
    const auto add=[&](int key,const std::string &label){ModernRow row;row.id="monster-query:"+std::to_string(key);row.label=label;row.key=row.hotkey=key;row.price_known=false;row.actions={{"choice",label,"",true,key}};w.rows.push_back(std::move(row));};
    add(13,tr(u8"名前を入力して調べる","Search by monster name"));
    add(1,tr(u8"すべてのモンスター","All monsters"));
    add(21,tr(u8"ユニーク・モンスター","Unique monsters"));
    add(14,tr(u8"ユニーク以外のモンスター","Non-unique monsters"));
    w.content.clear();ModernBlock note;note.kind="paragraph";note.cells.push_back({"",tr(u8"名前で調べる場合は先頭の項目を選び、次の入力欄へ名前を入力します。記号キーを直接押す従来の検索も使えます。","Choose the first item to enter a monster name. Native symbol-key search remains available from the keyboard."),false});w.content.push_back(std::move(note));
    (void)frame;
}

inline void modern_monster_query_result_controls(const GameFrame &frame,ModernWindow &w,const std::string &source) {
    const bool recall_question=source.find("Recall details? (k/y/n)")!=std::string::npos
        ||(source.find("/y/n")!=std::string::npos&&source.find("k:")!=std::string::npos
            &&source.find(reinterpret_cast<const char*>(u8"思い出"))!=std::string::npos);
    const bool result_prompt=source.find("[(r)ecall, ESC]")!=std::string::npos
        ||(source.find("ESC]")!=std::string::npos&&source.find("'r'")!=std::string::npos
            &&source.find(reinterpret_cast<const char*>(u8"思い出"))!=std::string::npos);
    if(!recall_question&&!result_prompt)return;

    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    w.stage="choices";
    w.content_type="monster_query_results";
    w.rows.clear();
    w.actions.clear();
    w.cursor=0;
    w.cursor_target.clear();
    w.capture_keys=false;
    const auto add=[&](int key,const std::string &label){
        ModernRow row;
        row.id="monster-query-result:"+std::to_string(key);
        row.label=label;
        row.key=row.hotkey=key;
        row.price_known=false;
        row.actions={{"choice",label,"",true,key}};
        w.rows.push_back(std::move(row));
    };
    if(recall_question) {
        add('y',tr(u8"詳細を見る","Show details"));
        add('k',tr(u8"撃破数順で詳細を見る","Show details sorted by kills"));
        add('n',tr(u8"詳細を見ずに戻る","Return without details"));
    } else {
        add('r',tr(u8"詳細表示を切り替える","Toggle recall details"));
        add('-',tr(u8"前のモンスター","Previous monster"));
        add(' ',tr(u8"次のモンスター","Next monster"));
    }
    (void)frame;
}

inline void modern_frox_birth_order(ModernWindow &w,const std::string &profile) {
    // Frox does not expose a native cursor on every race/class subpage, so
    // those pages can be classified as a character form rather than a choice
    // grid.  The stable title is the reliable boundary for both root and
    // "Other" pages.
    if(profile!="frox")return;
    const bool selection=w.title=="Choose a race"||w.title=="Choose a class"
        ||w.title==reinterpret_cast<const char*>(u8"種族の選択")||w.title==reinterpret_cast<const char*>(u8"職業の選択");
    if(!selection)return;
    const auto order=[](int key){if(key>='a'&&key<='z')return key-'a';if(key>='A'&&key<='Z')return 26+key-'A';if(key=='*')return 52;return 1000+key;};
    std::stable_sort(w.rows.begin(),w.rows.end(),[&](const auto &a,const auto &b){return order(a.key)<order(b.key);});
}

inline void modern_frox_stats(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if(profile!="frox"||!frame.pre_game_menu||w.stage!="choices")return;
    if(source.find("Enter Your Starting Stats")==std::string::npos&&source.find(reinterpret_cast<const char*>(u8"初期能力値を決める"))==std::string::npos)return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    std::vector<ModernBlock> values;int last_stat=-1;
    const std::vector<std::string> columns={tr(u8"基本値","Base"),tr(u8"消費ポイント","Points spent"),tr(u8"補正","Modifier"),tr(u8"合計","Total")};
    for(size_t line=0;line<frame.menu_term_lines.size();++line) {
        const auto &text=frame.menu_term_lines[line].text_utf8;
        if(text.size()<5||text[1]!='/'||text[3]!=')'||text[2]!=text[0]-'a'+'A')continue;
        const char key=text[0];if(std::string_view("siwdcr").find(key)==std::string_view::npos)continue;
        const auto cells=modern_split_cells(text.substr(4));if(cells.size()<5)continue;
        const auto name=modern_trim(cells[0].text);ModernBlock block;block.kind="stat_values";block.title=name;
        std::string description=name+"\n";
        for(int c=0;c<4;++c){block.cells.push_back({columns[c],cells[c+1].text,false});description+=columns[c]+": "+cells[c+1].text+"\n";}
        description+="\n"+tr(u8"基本値は8から17の間で循環します。合計30ポイント以内で設定してください。","The base value cycles between 8 and 17. Spend at most 30 points in total.");
        for(auto &row:w.rows)if(row.key==key||row.key==key-'a'+'A') {
            const bool increase=row.key!=key;
            row.id="stat:"+std::string(1,char(row.key));
            row.label=name+(increase?" +":" -");
            row.description=(ja?name+(increase?reinterpret_cast<const char*>(u8"を上げる"):reinterpret_cast<const char*>(u8"を下げる")):(increase?"Increase ":"Decrease ")+name)+"\n\n"+description;
        }
        values.push_back(std::move(block));last_stat=int(line);
    }
    if(values.size()!=6)return;
    std::string spent;
    if(last_stat+1<int(frame.menu_term_lines.size()))spent=modern_trim(frame.menu_term_lines[last_stat+1].text_utf8);
    int score=0;bool score_known=false;
    try {size_t used=0;score=std::stoi(spent,&used);score_known=used==spent.size();}catch(...){}
    auto start=std::find_if(w.content.begin(),w.content.end(),[](const auto &block){return std::any_of(block.cells.begin(),block.cells.end(),[](const auto &cell){return cell.text=="Enter Your Starting Stats"||cell.text==reinterpret_cast<const char*>(u8"初期能力値を決める");});});
    if(start!=w.content.end())w.content.erase(start,w.content.end());
    w.content.insert(w.content.begin(),values.begin(),values.end());
    if(score_known) {
        ModernBlock total;total.kind="fields";total.cells.push_back({tr(u8"使用ポイント / 上限","Points spent / limit"),std::to_string(score)+" / 30",false});w.content.insert(w.content.begin(),std::move(total));
        for(auto &row:w.rows)if(row.key==13) {
            row.available=score<=30;row.reason=row.available?"":tr(u8"使用ポイントを30以下にしてください。","Reduce the total to 30 points or fewer.");
            for(auto &action:row.actions){action.enabled=row.available;action.reason=row.reason;}
        }
    }
    w.title=tr(u8"初期能力値の設定","Starting attributes");
    // Keep both controls for each attribute adjacent, before every other choice.
    const std::string order="sSiIwWdDcCrR";
    std::stable_sort(w.rows.begin(),w.rows.end(),[&](const auto &a,const auto &b){
        return order.find(char(a.key))<order.find(char(b.key));
    });
    const auto promote=[&](int key,size_t at){
        const auto pos=std::min(at,w.rows.size());
        auto found=std::find_if(w.rows.begin()+pos,w.rows.end(),[&](const auto &row){return row.key==key;});
        if(found!=w.rows.end())std::rotate(w.rows.begin()+pos,found,found+1);
    };
    promote(13,12);
    promote(27,13);
    w.content_type="stat_allocation";
    if(score_known)w.subtitle=tr(u8"使用ポイント / 上限: ","Points spent / limit: ")+std::to_string(score)+" / 30";
    modern_add_action(w,'!',tr(u8"ゲーム開始の案内","Getting started"));
}

inline void modern_frox_birth_summary(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if(profile!="frox"||!frame.pre_game_menu||w.stage!="choices")return;
    (void)source;
    const auto has_key=[&](int key){return std::any_of(w.rows.begin(),w.rows.end(),[&](const auto &row){return row.key==key;});};
    // The Japanese native sheet prints a space before its field colons
    // ("名前 :"), while the interpreted fields normalize that spelling.
    // Its command set is the stable semantic identity of this screen.
    if(!has_key('n')||!has_key('*')||!has_key('s')||!has_key('r')||!has_key('c')||!has_key(13)||!has_key(27))return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    // py_birth.c draws this menu in two columns.  The modern list is a single
    // reading order, so keep only the requested commands and place them in the
    // order in which the player makes the character-creation decisions.
    // `p` (personality) is drawn by py_birth.c except in beginner mode; a row the screen does not draw is skipped below.
    const std::vector<std::pair<int,std::string>> order={
        {'n',tr(u8"名前を変える","Change name")},{'*',tr(u8"名前をランダムに","Randomize name")},
        {'s',tr(u8"性別を変える","Change gender")},{'p',tr(u8"性格を変える","Change personality")},{'r',tr(u8"種族を変える","Change race")},
        {'c',tr(u8"職業を変える","Change class")},{'g',tr(u8"ゲーム速度を変える","Change game speed")},
        {'=',tr(u8"設定","Settings")},{'?',tr(u8"ヘルプ","Help")},
        {13,tr(u8"次の画面","Next screen")},{27,tr(u8"前の画面","Previous screen")}
    };
    std::vector<ModernRow> rows;rows.reserve(order.size());
    for(const auto &[key,label]:order) {
        auto found=std::find_if(w.rows.begin(),w.rows.end(),[&](const auto &row){return row.key==key;});
        if(found==w.rows.end())continue;
        auto row=*found;row.label=label;row.hotkey=key;
        for(auto &action:row.actions)if(action.id=="choice")action.label=label;
        rows.push_back(std::move(row));
    }
    if(rows.size()>=8) {
        // The original screen takes a bare Enter as "Next screen" (it has no cursor of its own), so the modern
        // cursor starts there each time the window appears (fix 29). The screen side does the placing.
        std::string next_id;for(const auto &row:rows)if(row.key==13){next_id=row.id;break;}
        w.rows=std::move(rows);w.cursor=-1;w.cursor_target.clear();w.initial_target=next_id;
    }
}

inline void modern_genso_bonus_allocation(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if(profile!="legacy"||!frame.pre_game_menu)return;
    const bool screen=source.find(reinterpret_cast<const char*>(u8"能力値にボーナスポイントを割り振って下さい"))!=std::string::npos
        ||source.find("Distribute the bonus points among your stats")!=std::string::npos;
    if(!screen)return;
    const auto before=w;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    const auto selected_line=[&](const TermMirrorLine &line){
        if((line.color&15)==11)return true;
        return std::any_of(line.color_spans.begin(),line.color_spans.end(),[](const auto &span){return (span.color&15)==11&&span.len>0;});
    };
    w.stage="cursor";w.content_type="stat_allocation";w.title=tr(u8"ボーナスポイントの割り振り","Allocate bonus points");
    w.rows.clear();w.content.clear();w.actions.clear();w.cursor=-1;w.cursor_target.clear();
    for(const auto &line:frame.menu_term_lines)if(line.source_row==22) {
        auto value=modern_spaces(line.text_utf8);
        auto colon=value.find(':');size_t separator=1;
        const auto full_colon=std::string(reinterpret_cast<const char*>(u8"："));
        if(colon==std::string::npos){colon=value.find(full_colon);separator=full_colon.size();}
        if(colon!=std::string::npos)value=modern_trim(value.substr(colon+separator));
        if(value.size()>1&&value.front()=='('&&value.back()==')')value=modern_trim(value.substr(1,value.size()-2));
        const auto left=std::string(reinterpret_cast<const char*>(u8"（")),right=std::string(reinterpret_cast<const char*>(u8"）"));
        if(value.starts_with(left)&&value.ends_with(right))value=modern_trim(value.substr(left.size(),value.size()-left.size()-right.size()));
        while(!value.empty()&&value.back()==')')value.pop_back();
        if(value.ends_with(right))value.resize(value.size()-right.size());
        if(!value.empty()){ModernBlock block;block.kind="paragraph";block.cells.push_back({"",tr(u8"残りボーナスポイント: ","Bonus points remaining: ")+value,false});w.content.push_back(std::move(block));}
    }
    ModernBlock header;header.kind="table_header";
    for(const auto &label:std::vector<std::string>{tr(u8"能力","Attribute"),tr(u8"基本値","Base"),tr(u8"種族 職業 性格","Race / class / personality"),tr(u8"ボーナス","Bonus"),tr(u8"合計値","Total")})header.cells.push_back({"",label,false});
    w.content.push_back(std::move(header));
    int stat=0;bool selected=false;
    for(size_t index=0;index<frame.menu_term_lines.size();++index) {
        const auto &line=frame.menu_term_lines[index];if(line.source_row<14||line.source_row>19)continue;
        const auto cells=modern_split_cells(line.text_utf8);if(cells.empty())continue;
        const std::string name=modern_spaces(cells.front().text);if(name.empty())continue;
        std::vector<std::string> values;for(const auto &cell:cells){auto value=modern_spaces(cell.text);if(!value.empty())values.push_back(std::move(value));}
        if(values.size()<9)continue;
        ModernBlock block;block.kind="table_row";
        const std::vector<std::string> shown={values[0],values[1],values[2]+" "+values[3]+" "+values[4],values[6],values[8]};
        for(const auto &value:shown)block.cells.push_back({"",value,false});
        const bool active=selected_line(line);const std::string target="genso-stat:"+std::to_string(stat)+":0";
        if(active&&!block.cells.empty()){block.cells.front().selected=true;block.cells.front().target=target;w.cursor_target=target;selected=true;}
        w.content.push_back(std::move(block));
        for(int column=0;column<2;++column) {
            const int key=column?'6':'4';ModernRow row;row.id="genso-stat:"+std::to_string(stat)+":"+std::to_string(column);
            row.label=name+(column?" +":reinterpret_cast<const char*>(u8" −"));row.slot=int(index);row.key=row.hotkey=key;row.price_known=false;
            row.actions={{"choice",row.label,"",true,key}};w.rows.push_back(std::move(row));
        }
        ++stat;
    }
    if(stat!=6){w=before;return;}
    ModernRow done;done.id="genso-stat:continue";done.label=tr(u8"この割り振りで次に進む","Continue with this allocation");done.slot=1000000000;
    done.key=done.hotkey=13;done.price_known=false;done.actions={{"choice",done.label,"",true,13}};
    for(const auto &line:frame.menu_term_lines)if(line.source_row==21&&selected_line(line)){w.cursor_target=done.id;selected=true;}
    w.rows.push_back(std::move(done));
    if(!selected)w.cursor_target=w.rows.front().id;
    if(source.find('?')!=std::string::npos)modern_add_action(w,'?',tr(u8"ボーナスポイントのヘルプ","Bonus point help"));
}

// Angband's point-based stat screen (ui-birth.c point_based_command()) paints
// the character sheet and the cost column into one term.  Read generically,
// the stat columns and the sheet fields share rows, so the native cursor
// (placed on the stat row) highlights "Race" and the values lose their
// columns .  Present it with the same paired
// -/+ controls as Sil-Q: each row sends the native '4'/'6' after the focus
// has moved the native cursor with '8'/'2' (slot = mirror line).  The stat
// roller (roller_command()) paints the same sheet without the cost column; its
// choices come from the adapter, so only the sheet is laid out again there.
inline void modern_angband_point_based(const GameFrame &frame,ModernWindow &w,const std::string &source) {
    if(!frame.pre_game_menu)return;
    const bool points=w.stage=="cursor"&&(source.find("[up/down to move, left/right to modify, 'r' to reset, 'Enter' to accept]")!=std::string::npos
        ||source.find(reinterpret_cast<const char*>(u8"[上下で移動、左右で増減、'r' でやり直す、'Enter' で決定]"))!=std::string::npos);
    const bool roller=w.stage=="choices"&&(source.find("['r' to reroll")!=std::string::npos
        ||source.find(reinterpret_cast<const char*>(u8"['r' で振り直す"))!=std::string::npos);
    if(!points&&!roller)return;
    const size_t cols=points?7:6;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    struct Token {size_t begin,end;std::string text;};
    const auto tokens=[](const std::string &line){
        std::vector<Token> out;size_t i=0;
        while(i<line.size()){while(i<line.size()&&line[i]==' ')++i;const size_t b=i;while(i<line.size()&&line[i]!=' ')++i;if(i>b)out.push_back({b,i,line.substr(b,i-b)});}
        return out;
    };
    const auto number=[](const std::string &text){return !text.empty()&&text.find_first_not_of("0123456789")==std::string::npos;};
    // A stat row ends with "<name>: base race class equip best [cost]".
    std::vector<int> stat_lines;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto t=tokens(frame.menu_term_lines[i].text_utf8);
        if(t.size()<cols)continue;
        const auto &name=t[t.size()-cols].text;
        if(name.size()>1&&name.back()==':'&&number(t[t.size()-cols+1].text)&&(!points||number(t.back().text)))stat_lines.push_back(int(i));
    }
    if(stat_lines.size()<3)return;
    for(size_t i=1;i<stat_lines.size();++i)if(stat_lines[i]!=stat_lines[i-1]+1)return;
    const int first=stat_lines.front(),last=stat_lines.back();
    if(first<1)return;
    const auto header=tokens(frame.menu_term_lines[first-1].text_utf8);
    if(header.size()<cols-1)return;
    std::string spent,total_label;
    if(points&&last+1<int(frame.menu_term_lines.size())) {
        const auto t=tokens(frame.menu_term_lines[last+1].text_utf8);
        if(t.size()>=2&&t.back().text.find('/')!=std::string::npos) {
            spent=t.back().text;
            const auto &line=frame.menu_term_lines[last+1].text_utf8;
            const size_t begin=t.back().begin;
            // The label is the run of tokens just before the value ("Total Cost:"),
            // separated from the sheet on its left by a wide gap.
            size_t k=t.size()-2;
            while(k>0&&t[k].begin-t[k-1].end<3)--k;
            total_label=modern_trim(line.substr(t[k].begin,begin-t[k].begin));
            if(!total_label.empty()&&total_label.back()==':')total_label.pop_back();
        }
    }
    const auto &cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    const auto before=w;
    w.content.clear();
    if(points) {
        w.content_type="sil_allocation";
        w.title=tr(u8"能力値の割り振り（ポイント制）","Point-based stats");
        w.rows.clear();w.cursor=-1;w.cursor_target.clear();
    } else w.title=tr(u8"能力値のロール","Stat roller");
    if(!spent.empty()) {
        const auto slash=spent.find('/');int used=-1,limit=-1;
        try{used=std::stoi(spent.substr(0,slash));limit=std::stoi(spent.substr(slash+1));}catch(...){}
        ModernBlock pool;pool.kind="fields";
        pool.cells.push_back({total_label.empty()?tr(u8"合計コスト","Total cost"):total_label,spent,false});
        if(used>=0&&limit>=used)pool.cells.push_back({tr(u8"残りポイント","Points left"),std::to_string(limit-used),false});
        w.content.push_back(std::move(pool));
        w.subtitle=(total_label.empty()?tr(u8"合計コスト","Total cost"):total_label)+": "+spent;
    }
    ModernBlock head;head.kind="table_header";
    const auto &h=header;const size_t n=h.size();
    // Header: base, race, class, equipment, best[, cost] (the last cols-1 tokens).
    const size_t o=n-(cols-1);
    std::vector<std::string> labels{tr(u8"能力","Stat"),h[o].text,h[o+1].text+" "+h[o+2].text+" "+h[o+3].text,h[o+4].text};
    if(points)labels.push_back(h[o+5].text);
    for(const auto &label:labels)head.cells.push_back({"",label,false});
    w.content.push_back(std::move(head));
    for(const int index:stat_lines) {
        const auto t=tokens(frame.menu_term_lines[index].text_utf8);const size_t b=t.size()-cols;
        std::string name=t[b].text;name.pop_back();
        const bool active=index==cursor.line_index;
        ModernBlock block;block.kind="table_row";
        std::vector<std::string> values{name,t[b+1].text,t[b+2].text+" "+t[b+3].text+" "+t[b+4].text,t[b+5].text};
        if(points)values.push_back(t[b+6].text);
        for(const auto &value:values)block.cells.push_back({"",value,false});
        if(!points){w.content.push_back(std::move(block));continue;}
        for(int column=0;column<2;++column) {
            const int key=column?'6':'4';ModernRow row;row.id="an-points:"+std::to_string(index)+":"+std::to_string(column);
            row.label=name+(column?" +":reinterpret_cast<const char*>(u8" −"));row.slot=index;row.selection=0;row.key=key;row.hotkey=0;row.price_known=false;
            row.description=name+"\n\n"+(column?tr(u8"この能力を 1 上げます（ポイントを使います）。","Raise this stat by one (spends points)."):tr(u8"この能力を 1 下げます（ポイントが戻ります）。","Lower this stat by one (refunds points)."));
            row.actions={{"choice",row.label,"",true,key}};
            if(active&&!column){w.cursor_target=row.id;block.cells.front().selected=true;block.cells.front().target=row.id;}
            w.rows.push_back(std::move(row));
        }
        w.content.push_back(std::move(block));
    }
    if(points&&w.cursor_target.empty()){w=before;return;}
    if(points) {
    ModernRow done;done.id="an-points:accept";done.label=tr(u8"この割り振りで決定","Accept these stats");done.slot=1000000000;
    done.key=13;done.hotkey=0;done.price_known=false;done.actions={{"choice",done.label,"",true,13}};
    w.rows.push_back(std::move(done));
    }
    // The rest of the character sheet keeps the native columns (fixed-pitch
    // lines, as for help tables): the left half of the stat rows, then the
    // lower sheet.  The history is prose; the footer is the controls above.
    const auto code_line=[&](std::string text){
        while(!text.empty()&&text.back()==' ')text.pop_back();
        if(modern_trim(text).empty())return;
        ModernBlock b;b.kind="code";b.cells.push_back({"",std::move(text),false});w.content.push_back(std::move(b));
    };
    for(int i=first-1;i<=std::min(last+1,int(frame.menu_term_lines.size())-1);++i) {
        const auto &line=frame.menu_term_lines[size_t(i)].text_utf8;const auto t=tokens(line);
        size_t cut=line.size();
        if(i==first-1&&t.size()>=cols-1)cut=t[t.size()-(cols-1)].begin;
        else if(i>=first&&i<=last&&t.size()>=cols)cut=t[t.size()-cols].begin;
        else if(i==last+1&&!spent.empty()&&!total_label.empty())cut=line.rfind(total_label);
        code_line(line.substr(0,std::min(cut,line.size())));
    }
    std::string history;
    for(size_t i=size_t(last)+2;i<frame.menu_term_lines.size();++i) {
        const auto &line=frame.menu_term_lines[i].text_utf8;const auto text=modern_trim(line);
        if(text.empty()||text.starts_with("["))continue;
        const bool prose=text.find(reinterpret_cast<const char*>(u8"。"))!=std::string::npos||text.find(". ")!=std::string::npos||(!history.empty()&&!line.starts_with("  "));
        if(prose){if(!history.empty()&&static_cast<unsigned char>(text.front())<128)history+=' ';history+=modern_spaces(text);}
        else code_line(line);
    }
    if(!history.empty()){ModernBlock b;b.kind="paragraph";b.title=tr(u8"生い立ち","History");b.cells.push_back({"",history,false});w.content.push_back(std::move(b));}
    if(!points)return;
    w.actions.clear();
    modern_add_action(w,'r',tr(u8"割り振りを初めに戻す","Reset the allocation"));
}

inline void modern_birth_start_menu(const GameFrame &frame,ModernWindow &w,const std::string &source) {
    if(!frame.pre_game_menu)return;
    const bool restart=std::any_of(w.actions.begin(),w.actions.end(),[](const auto &action){return action.key=='S';});
    const bool quit=std::any_of(w.actions.begin(),w.actions.end(),[](const auto &action){return action.key=='Q';});
    const bool final_prompt=source.find("Enter ゲーム開始")!=std::string::npos
        ||source.find("Enter to continue")!=std::string::npos
        ||source.find("Enter to start the game")!=std::string::npos
        // Gensoband's mixed CP932 path can damage the visible prompt before
        // semantic interpretation.  Its final sheet is still unambiguous: a
        // pre-game detail page with Enter plus native Restart and Quit.
        ||(w.stage=="detail"&&w.continue_key==13&&restart&&quit);
    if(!final_prompt)return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    w.stage="choices";w.content_type="character";w.rows.clear();w.actions.clear();w.cursor=0;w.cursor_target.clear();
    const auto add=[&](int key,const std::string &label){ModernRow row;row.id="birth-final:"+std::to_string(key);row.label=label;row.key=row.hotkey=key;row.price_known=false;row.actions={{"choice",row.label,"",true,key}};w.rows.push_back(std::move(row));};
    add(13,tr(u8"ゲームを開始する","Start the game"));
    if(source.find("'S'")!=std::string::npos||source.find("S'tart over")!=std::string::npos)add('S',tr(u8"キャラクター作成を最初からやり直す","Restart character creation from the beginning"));
    if(source.find("'Q'")!=std::string::npos||source.find("'Q'uit")!=std::string::npos)add('Q',tr(u8"ゲームを終了する","Exit the game"));
}

inline void modern_sil_allocation(const GameFrame &frame,ModernWindow &w,const std::string &source,const std::string &profile) {
    if(profile!="sil")return;
    const bool stats=source.find("Arrow keys allocate points to stats")!=std::string::npos||source.find(reinterpret_cast<const char*>(u8"矢印の鍵で能力値に点"))!=std::string::npos;
    const bool skills=source.find("Arrow keys allocate points to skills")!=std::string::npos||source.find(reinterpret_cast<const char*>(u8"矢印の鍵で技能に点"))!=std::string::npos;
    const bool allocation=w.stage=="cursor"&&(stats||skills);
    const bool sheet=std::any_of(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[](const auto &line){
        return line.source_row==2&&(line.text_utf8.find("Age")!=std::string::npos||line.text_utf8.find(reinterpret_cast<const char*>(u8"年齢"))!=std::string::npos)
            &&(line.text_utf8.find("Name")!=std::string::npos||line.text_utf8.find(reinterpret_cast<const char*>(u8"名前"))!=std::string::npos);
    });
    if(!sheet)return;
    const bool ja=w.language=="ja";
    const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    const auto part=[](const std::string &line,int first,int last){const auto begin=modern_column_byte(line,first),end=modern_column_byte(line,last);return modern_spaces(line.substr(begin,end-begin));};
    const auto field=[&](const std::string &text)->ModernCell {
        // Labels come from Sil-Q's character sheet; the complete value/formula
        // stays together even when the native display uses separate color runs.
        const std::vector<std::string> names={"Light Radius","Max Burden","Game Turn","Total Exp","Exp Pool","Min Depth","Name","Race","House","Age","Height","Weight","Burden","Depth","Health","Voice","Melee","Bows","Armor","Song","Strength","Dexterity","Constitution","Grace","STR","DEX","CON","GRA","Archery","Evasion","Stealth","Perception","Will","Smithing",
            tr(u8"光の半径",""),tr(u8"重さの上限",""),tr(u8"荷物の重さ",""),tr(u8"経過ターン",""),tr(u8"未使用経験",""),tr(u8"総経験",""),tr(u8"名前",""),tr(u8"種族",""),tr(u8"家",""),tr(u8"年齢",""),tr(u8"身長",""),tr(u8"体重",""),tr(u8"生命力",""),tr(u8"声",""),tr(u8"打撃",""),tr(u8"弓",""),tr(u8"防護",""),tr(u8"腕力",""),tr(u8"敏捷",""),tr(u8"耐久",""),tr(u8"恩寵",""),tr(u8"射撃",""),tr(u8"回避",""),tr(u8"隠密",""),tr(u8"知覚",""),tr(u8"意志",""),tr(u8"鍛冶",""),tr(u8"歌唱","")};
        for(const auto &name:names)if(!name.empty()&&text.starts_with(name)&&(text.size()==name.size()||text[name.size()]==' '))return {name,modern_trim(text.substr(name.size())),false};
        return {"",text,false};
    };
    const auto &cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    w.content.clear();w.cursor_target.clear();
    if(allocation){w.rows.clear();w.content_type="sil_allocation";w.title=stats?tr(u8"能力値の設定","Attributes"):tr(u8"技能の設定","Skills");}
    else if(w.stage=="text")w.content_type="form";
    else w.content_type="character";
    std::string history;
    for(size_t index=0;index<frame.menu_term_lines.size();++index) {
        const auto &line=frame.menu_term_lines[index];const auto &raw=line.text_utf8;const int y=line.source_row;
        if(y>=2&&y<=14) {
            ModernBlock block;block.kind="fields";
            for(const auto range:std::initializer_list<std::pair<int,int>>{{1,22},{22,41},{41,allocation?74:80}}) {
                if(w.stage=="text"&&int(index)==frame.menu_term_curs_row&&range.first==1)continue;
                auto value=field(part(raw,range.first,range.second));if(value.label.empty()&&value.text.empty())continue;
                block.cells.push_back(std::move(value));
            }
            if(allocation&&((stats&&y>=2&&y<=5)||(skills&&y>=7&&y<=14))) {
                const auto cost=part(raw,74,80);
                if(!block.cells.empty()&&!cost.empty()&&cost.find_first_not_of("0123456789, -")==std::string::npos) {
                    const auto name=block.cells.back().label;const bool selected=int(index)==cursor.line_index;
                    ModernCell value{tr(u8"消費ポイント","Points spent"),cost,selected};
                    for(int column=0;column<2;++column) {
                        const int key=column?'6':'4';ModernRow row;row.id="allocation:"+std::to_string(index)+":"+std::to_string(column);
                        row.label=name+(column?" +":reinterpret_cast<const char*>(u8" −"));row.slot=int(index);row.selection=0;row.key=row.hotkey=key;row.price_known=false;
                        row.actions={{"choice",row.label,"",true,key}};
                        if(selected&&!column){w.cursor_target=row.id;value.target=row.id;}
                        w.rows.push_back(std::move(row));
                    }
                    block.cells.push_back(std::move(value));
                }
            }
            if(!block.cells.empty())w.content.push_back(std::move(block));
        } else if(y>=16&&y<=20) {
            if(w.stage=="text"&&int(index)==frame.menu_term_curs_row)continue;
            const auto prose=modern_spaces(raw);if(!prose.empty()){if(!history.empty()&&static_cast<unsigned char>(prose.front())<128)history+=' ';history+=prose;}
        } else if(y<2&&!modern_trim(raw).empty()) {
            ModernBlock block;block.kind="paragraph";block.cells.push_back({"",modern_spaces(raw),false});w.content.push_back(std::move(block));
        }
    }
    if(!history.empty()){ModernBlock block;block.kind="paragraph";block.title=tr(u8"生い立ち","History");block.cells.push_back({"",history,false});w.content.push_back(std::move(block));}
    if(allocation&&skills) {
        const std::vector<std::string> skill_names={"Melee","Archery","Evasion","Stealth","Perception","Will","Smithing","Song",
            tr(u8"打撃",""),tr(u8"射撃",""),tr(u8"回避",""),tr(u8"隠密",""),tr(u8"知覚",""),tr(u8"意志",""),tr(u8"鍛冶",""),tr(u8"歌唱","")};
        const auto is_skill=[&](const std::string &label){return std::find(skill_names.begin(),skill_names.end(),label)!=skill_names.end();};
        ModernBlock pool;pool.kind="fields";pool.title=tr(u8"残りの経験値","Experience available");
        ModernBlock changing;changing.kind="fields";changing.title=tr(u8"技能値と消費経験値","Skill values and experience costs");
        std::vector<ModernBlock> fixed;
        for(auto &block:w.content) {
            if(block.kind!="fields"){fixed.push_back(std::move(block));continue;}
            ModernBlock rest;rest.kind=block.kind;rest.title=block.title;std::string current_skill;
            for(auto &cell:block.cells) {
                if(cell.label=="Exp Pool"||cell.label==tr(u8"未使用経験","")){pool.cells.push_back(std::move(cell));continue;}
                if(is_skill(cell.label)) {current_skill=cell.label;changing.cells.push_back(std::move(cell));continue;}
                if(cell.label=="Points spent"||cell.label==tr(u8"消費ポイント","")) {
                    if(!current_skill.empty())cell.label=current_skill+" "+tr(u8"消費経験値","Experience cost");
                    changing.cells.push_back(std::move(cell));continue;
                }
                rest.cells.push_back(std::move(cell));
            }
            if(!rest.cells.empty())fixed.push_back(std::move(rest));
        }
        w.content.clear();
        if(!pool.cells.empty())w.content.push_back(std::move(pool));
        if(!changing.cells.empty())w.content.push_back(std::move(changing));
        w.content.insert(w.content.end(),std::make_move_iterator(fixed.begin()),std::make_move_iterator(fixed.end()));
    }
    if(allocation) {
        const auto pick=[&](const std::string &label,bool last=false){
            std::string value;
            for(const auto &block:w.content)for(const auto &cell:block.cells)if(cell.label==label&&!cell.text.empty()) {
                if(!last)return cell.text;value=cell.text;
            }
            return value;
        };
        std::string remaining;
        const auto remaining_label=tr(u8"残りの点","Points remaining");
        for(const auto &line:frame.menu_term_lines) {
            const auto text=modern_spaces(line.text_utf8);const auto at=text.find(remaining_label);
            if(at==std::string::npos)continue;
            auto colon=text.find(':',at+remaining_label.size());
            if(colon!=std::string::npos)remaining=modern_trim(text.substr(colon+1));
            break;
        }
        const auto shown=[&](const std::string &label,bool last=false){
            auto value=pick(label,last);const auto marker=value.find(remaining_label);if(marker!=std::string::npos)value=modern_trim(value.substr(0,marker));
            return label+": "+value;
        };
        const auto pair=[&](const std::string &a,const std::string &b){return shown(a)+"    "+shown(b);};
        const auto triple=[&](const std::string &a,const std::string &b,const std::string &c){return shown(a)+"    "+shown(b)+"    "+shown(c,true);};
        const auto block=[&](std::initializer_list<std::string> lines,std::string title={}){
            ModernBlock result;result.kind="paragraph";result.title=std::move(title);
            for(const auto &line:lines)result.cells.push_back({"",line,false});return result;
        };
        const auto strength=tr(u8"腕力","Strength"),dexterity=tr(u8"敏捷","Dexterity"),constitution=tr(u8"耐久","Constitution"),grace=tr(u8"恩寵","Grace");
        const auto name=tr(u8"名前","Name"),age=tr(u8"年齢","Age"),race=tr(u8"種族","Race"),height=tr(u8"身長","Height"),house=tr(u8"家","House"),weight=tr(u8"体重","Weight");
        const auto burden=tr(u8"荷物の重さ","Burden"),max_burden=tr(u8"重さの上限","Max Burden"),health=tr(u8"生命力","Health"),armor=tr(u8"防護","Armor"),voice=tr(u8"声","Voice"),light=tr(u8"光の半径","Light Radius"),turn=tr(u8"経過ターン","Game Turn");
        const auto unused=tr(u8"未使用経験","Exp Pool"),total=tr(u8"総経験","Total Exp");
        const std::vector<std::string> skill_names={tr(u8"打撃","Melee"),tr(u8"射撃","Archery"),tr(u8"回避","Evasion"),tr(u8"隠密","Stealth"),tr(u8"知覚","Perception"),tr(u8"意志","Will"),tr(u8"鍛冶","Smithing"),tr(u8"歌唱","Song")};
        std::vector<ModernBlock> exact;
        if(stats) {
            exact.push_back(block({remaining_label+": "+remaining,shown(strength),shown(dexterity),shown(constitution),shown(grace)}));
            exact.push_back(block({pair(name,age),pair(race,height),pair(house,weight),shown(health),shown(armor),shown(voice),shown(light),shown(turn),pair(burden,max_burden)}));
            ModernBlock skills_block;skills_block.kind="paragraph";
            skills_block.cells.push_back({"",triple(unused,total,skill_names[0]),false});
            for(size_t i=1;i<skill_names.size();++i)skills_block.cells.push_back({"",shown(skill_names[i],true),false});
            exact.push_back(std::move(skills_block));
        } else {
            exact.push_back(block({pair(unused,total)}));
            ModernBlock changing;changing.kind="paragraph";changing.title=tr(u8"技能値と消費経験値","Skill values and experience costs");
            const auto cost=tr(u8"消費経験値","Experience cost");
            for(const auto &skill:skill_names)changing.cells.push_back({"",shown(skill,true)+"    "+cost+": "+pick(skill+" "+cost),false});
            exact.push_back(std::move(changing));
            exact.push_back(block({pair(name,age),pair(race,height),pair(house,weight),shown(burden),shown(health),shown(armor),shown(voice),shown(light)}));
            exact.push_back(block({shown(strength),shown(dexterity),shown(constitution),shown(grace),remaining_label+": "+remaining}));
        }
        w.content=std::move(exact);
        // Allocation is already fully represented by the paired rows and the
        // final Continue row.  A separate Start/F4 action drawer duplicates
        // unrelated native commands and obscures the requested status layout.
        w.actions.clear();
    }
    for(auto &row:w.rows) {
        const auto prefix=row.key==13?"Enter ":(row.key==32?"Space ":(row.key=='m'?"m ":""));
        if(*prefix&&row.label.starts_with(prefix))row.label=modern_trim(row.label.substr(std::char_traits<char>::length(prefix)));
        if(!allocation&&row.key=='m')row.label=tr(u8"名前を入力する","Enter a name");
    }
    if(allocation){
        ModernRow done;done.id="allocation:continue";done.label=tr(u8"次に進む","Continue");done.slot=1000000000;done.key=done.hotkey=13;done.price_known=false;
        done.actions={{"choice",done.label,"",true,13}};w.rows.push_back(std::move(done));
    }
    if(w.stage=="text"&&frame.menu_term_curs_row>=0&&frame.menu_term_lines[frame.menu_term_curs_row].source_row==2) {
        w.title=tr(u8"名前の入力","Enter a name");modern_add_action(w,9,tr(u8"名前を無作為に決める","Random name"));
    } else if(modern_birth_screen(frame)&&!allocation)modern_add_action(w,'q',tr(u8"作成をやめる","Stop character creation"));
    const bool waiting=source.find("Please wait...")!=std::string::npos
        ||source.find(reinterpret_cast<const char*>(u8"お待ちください……"))!=std::string::npos;
    if(waiting&&w.stage=="detail") {
        // Sil-Q repaints this transient character sheet before the introduction
        // starts. It has no native input prompt, so do not advertise Continue
        // during the few frames in which an input would immediately go stale.
        w.input_ready=false;w.continue_key=0;
    }
}

inline void modern_edit_content(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    w.input_caret=-1;if(w.stage!="text")return;
    w.text_fullwidth_kana=profile=="sil"||profile=="frox";
    w.text_edit_mode=profile=="sil"?"append":((profile=="legacy"||profile=="frox")?"basic":"full");
    const bool mirror=frame.menu_term_curs_row>=0&&frame.menu_term_curs_row<int(frame.menu_term_lines.size());
    auto line=mirror?frame.menu_term_lines[frame.menu_term_curs_row].text_utf8:frame.prompt.text_utf8;
    if(line.empty()&&!mirror)return;
    const int missing=std::clamp(frame.menu_term_curs_col-modern_byte_column(line,line.size()),0,512);line.append(size_t(missing),' ');
    const size_t caret=frame.menu_term_curs_col>=0?modern_column_byte(line,frame.menu_term_curs_col):line.size();
    size_t first=0,last=line.size();bool field=false;
    const int source_row=mirror?frame.menu_term_lines[frame.menu_term_curs_row].source_row:-1;
    if(profile=="sil"&&source_row==2&&(modern_trim(line).starts_with("Name ")||modern_trim(line).starts_with(reinterpret_cast<const char*>(u8"名前 ")))) {
        // The name may contain Japanese (decision 2026-10-12, fix 57): the core side accepts it, so the field is not ASCII-only.
        first=modern_column_byte(line,8);last=modern_column_byte(line,22);field=true;
        w.input_label=w.language=="ja"?reinterpret_cast<const char*>(u8"名前"):"Name";
    } else {
        bool history=false;
        if(frame.pre_game_menu&&(profile=="heng"||profile=="legacy")&&source_row>=12&&source_row<=15)for(const auto &row:frame.menu_term_lines) {
            const auto text=modern_spaces(row.text_utf8);if(text.find("Character Background - Edit Mode")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"生い立ち - 編集モード"))!=std::string::npos)history=true;
        }
        if(history){w.text_edit_mode="grid";first=modern_column_byte(line,10);field=true;w.input_label=(w.language=="ja"?reinterpret_cast<const char*>(u8"生い立ち "):"Background ")+std::to_string(source_row-11)+" / 4";}
        else {
            const auto colon=line.find(':');const bool drive=colon==1&&line.size()>2&&(line[2]=='\\'||line[2]=='/');
            if(colon!=std::string::npos&&colon<caret&&colon<128&&line.find("://")==std::string::npos&&!drive) {
                auto label=modern_trim(line.substr(0,colon));
                // RogueClone2 の設定一覧は今の行の頭に '>' の印を描く。印は見出しではない。
                if(label.size()>1&&label.front()=='>')label=modern_trim(label.substr(1));
                if(!label.empty()){first=colon+1;if(first<line.size()&&line[first]==' ')++first;w.input_label=label;field=true;}
            }
            // Only a separate panel beyond the caret is excluded; preserve
            // spaces typed into the field, including a trailing space at it.
            const auto gap=line.find("  ",std::max(first,caret));if(gap!=std::string::npos)last=gap;
        }
    }
    first=std::min(first,line.size());last=std::max(first,std::min(last,line.size()));
    const auto buffer=line.substr(first,last-first);
    const auto leading=buffer.find_first_not_of(" \t\r\n");// AH-62: Angband の誕生の名前の問いは、日本語では訳文が英語の文より短いのに、名前の欄は英語の文の幅（桁 55）から
    // 始まる。間の空きを欄の中身に数えない（数えると入力欄の頭に空白が並び、カーソルの位置も実際の文字列とずれる）。
    const bool name_pad=field&&frame.player_name_input_active&&profile!="sil";
    const size_t removed=(field&&!name_pad)?0:std::min(caret>=first?caret-first:0,leading==std::string::npos?buffer.size():leading);
    const auto nonblank=buffer.find_last_not_of(" \t\r\n");
    const size_t visible=std::max(nonblank==std::string::npos?0:nonblank+1,std::min(buffer.size(),caret>=first?caret-first:0));
    w.input=buffer.substr(removed,visible>=removed?visible-removed:0);w.input_caret=int(std::min(caret>=first+removed?caret-first-removed:0,w.input.size()));
    if(w.input_label.find(reinterpret_cast<const char*>(u8"名前"))!=std::string::npos)w.input_label=reinterpret_cast<const char*>(u8"名前");
}
inline bool modern_scalar_setting(const GameFrame &frame,ModernWindow &w) {
    if(frame.pre_game_menu||frame.text_input_active||frame.numeric.active||!frame.prompt.choices.empty())return false;
    std::string prompt,current;
    for(const auto &line:frame.menu_term_lines) {
        const auto text=modern_spaces(line.text_utf8);
        if(text.find("(0-9")!=std::string::npos&&(text.find("ESC")!=std::string::npos||text.find("Esc")!=std::string::npos))prompt=text;
        if(line.source_row>=21&&(text.starts_with("Current")||text.starts_with(reinterpret_cast<const char*>(u8"現在"))))current=text;
    }
    if(prompt.empty()||current.empty())return false;
    const bool ja=w.language=="ja";w.title=modern_trim(prompt.substr(0,prompt.find("(0-9")));
    w.kind="dialog";w.stage="choices";w.content_type="scalar_setting";w.content.clear();w.rows.clear();w.actions.clear();w.cursor=-1;w.cursor_target.clear();w.input.clear();w.prev_key=w.next_key=0;
    ModernBlock value;value.kind="fields";const auto colon=current.find(':');
    value.cells.push_back({colon==std::string::npos?(ja?reinterpret_cast<const char*>(u8"現在値"):"Current value"):modern_trim(current.substr(0,colon)),colon==std::string::npos?current:modern_trim(current.substr(colon+1)),false});w.content.push_back(std::move(value));
    const bool percent=current.find('%')!=std::string::npos;
    for(int i=0;i<=9;++i)modern_command_row(w,"setting:",'0'+i,std::to_string(percent?i*10:i)+(percent?"%":""));
    modern_command_row(w,"setting:",27,ja?reinterpret_cast<const char*>(u8"設定を終えて戻る"):"Finish settings");
    return true;
}
inline void modern_simple_form(const GameFrame &frame,ModernWindow &w,const std::string &source,bool document) {
    if(w.kind!="dialog"||frame.pre_game_menu||document||w.content_type=="editor")return;
    if(w.stage!="text"&&w.stage!="number"&&w.stage!="confirm"&&!w.capture_keys)return;
    const bool character=source.find(reinterpret_cast<const char*>(u8"年齢"))!=std::string::npos
        &&source.find(reinterpret_cast<const char*>(u8"種族"))!=std::string::npos
        ||source.find("Age")!=std::string::npos&&source.find("Race")!=std::string::npos;
    if(character&&w.stage!="confirm")return;
    const bool ja=w.language=="ja";std::string purpose;
    for(const auto &line:frame.menu_term_lines) {
        const auto text=modern_spaces(line.text_utf8);
        if(text.starts_with("Command:")||text.starts_with(reinterpret_cast<const char*>(u8"コマンド:"))||text.starts_with(reinterpret_cast<const char*>(u8"命令:")))purpose=modern_trim(text.substr(text.find(':')+1));
    }
    if(w.input_label=="Note")w.input_label=ja?reinterpret_cast<const char*>(u8"メモ"):"Note";
    const auto parent=modern_choice_label(w.title);
    w.content.clear();w.actions.clear();w.rows.clear();w.cursor=-1;w.cursor_target.clear();
    if(!purpose.empty()){w.title=purpose;if(parent!=purpose&&parent!="Menu"&&parent!=reinterpret_cast<const char*>(u8"メニュー"))w.subtitle=parent;}
    else if(!w.input_label.empty()){w.title=w.input_label;w.subtitle.clear();}
    if(w.stage=="number") {
        auto prompt=modern_spaces(frame.numeric.prompt_utf8);
        for(const char *prefix:{"Command:",reinterpret_cast<const char*>(u8"コマンド:")})if(prompt.starts_with(prefix))prompt=modern_trim(prompt.substr(std::char_traits<char>::length(prefix)));
        if(!prompt.empty()&&prompt.back()==':')prompt.pop_back();w.input_label=prompt;
        const auto bracket=prompt.find('(');if(bracket!=std::string::npos)w.title=modern_trim(prompt.substr(0,bracket));
    }
    if(w.stage=="confirm") {
        w.title=ja?reinterpret_cast<const char*>(u8"確認"):"Confirmation";
        if(!frame.hud.name.empty()&&(w.input.find(reinterpret_cast<const char*>(u8"冒険者"))!=std::string::npos||w.input.find("character")!=std::string::npos)) {
            ModernBlock name;name.kind="fields";name.cells.push_back({ja?reinterpret_cast<const char*>(u8"対象のキャラクター"):"Character",frame.hud.name,false});w.content.push_back(std::move(name));
        }
    }
    if(w.capture_keys) {
        w.content_type="key_capture";
        ModernBlock help;help.kind="paragraph";help.cells.push_back({"",ja?reinterpret_cast<const char*>(u8"設定・確認するキーを押してください。"):"Press the key you want to set or inspect.",false});w.content.push_back(std::move(help));
        for(size_t i=0;i+1<frame.menu_term_lines.size();++i) {
            const auto label=modern_spaces(frame.menu_term_lines[i].text_utf8);
            if(label.find("Action (if any)")!=std::string::npos||label.find(reinterpret_cast<const char*>(u8"現在のアクション"))!=std::string::npos||label.find(reinterpret_cast<const char*>(u8"いまの動き"))!=std::string::npos) {
                ModernBlock action;action.kind="fields";action.cells.push_back({ja?reinterpret_cast<const char*>(u8"現在の動作"):"Current action",modern_trim(frame.menu_term_lines[i+1].text_utf8),false});w.content.push_back(std::move(action));
            }
        }
    }
}
inline bool modern_map_content(const GameFrame &frame,ModernWindow &w) {
    if(frame.pre_game_menu||w.stage!="detail"||w.capture_keys)return false;
    bool border=false,prompt=false,map_filters=false;int map_rows=0;
    for(const auto &line:frame.menu_term_lines) {
        const auto text=modern_trim(line.text_utf8);
        if(text.size()>30&&text.starts_with("+---")&&text.back()=='+')border=true;
        if(text.find("Hit any key to continue")!=std::string::npos||text.find("Press any key")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"何かキーを押"))!=std::string::npos)prompt=true;
        if(text.find("M")!=std::string::npos&&text.find("N")!=std::string::npos
            &&text.find("K")!=std::string::npos&&text.find("D")!=std::string::npos
            &&(text.find("auto-picker")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"拾"))!=std::string::npos))map_filters=true;
        if(text.size()>20&&text.front()=='|') {
            int terrain=0;
            for(const char ch:text)if(std::string_view("#%.:+-~^<>@*").find(ch)!=std::string_view::npos)++terrain;
            if(terrain>=8)++map_rows;
        }
    }
    // Hengband/Tangband can clip both the right border and the continuation
    // prompt in the mirrored terminal.  At this point a detail dialog with
    // many terrain-heavy rows is already a sufficiently specific signature
    // for the explored-map screen; border/prompt text is
    // only supporting evidence and must not be mandatory.
    (void)border;(void)prompt;
    if(map_rows<8)return false;
    w.title=w.language=="ja"?reinterpret_cast<const char*>(u8"探索済みの地図"):"Explored map";w.subtitle.clear();w.content_type="map";w.content.clear();w.rows.clear();w.actions.clear();w.cursor=-1;w.cursor_target.clear();w.input.clear();w.continue_key=13;w.prev_key=w.next_key=0;
    if(map_filters) {
        const bool ja=w.language=="ja";
        modern_add_action(w,'M',ja?reinterpret_cast<const char*>(u8"拾う／確認して拾う対象を表示"):"Toggle auto-pick/query items");
        modern_add_action(w,'N',ja?reinterpret_cast<const char*>(u8"拾わない対象を表示"):"Toggle do-not-pick items");
        modern_add_action(w,'K',ja?reinterpret_cast<const char*>(u8"自動破棄対象を表示"):"Toggle auto-destroy items");
        modern_add_action(w,'D',ja?reinterpret_cast<const char*>(u8"拾う対象と拾わない対象をまとめて表示"):"Toggle combined pick/do-not-pick items");
    }
    // The Hengband-family direct M command temporarily leaves the normal floor
    // snapshot, so some frames do not carry MinimapSnapshot even though the
    // native terminal already contains the complete explored map. Preserve
    // those rows as a scrollable fixed-width fallback; the renderer uses the
    // graphical minimap whenever a snapshot is available.
    if(!frame.minimap.valid()) {
        ModernBlock ascii;ascii.kind="code";
        for(const auto &line:frame.menu_term_lines) {
            const auto text=modern_trim(line.text_utf8);
            if(text.size()>20&&(text.front()=='|'||text.starts_with("+---")))ascii.cells.push_back({"",text,false});
        }
        if(!ascii.cells.empty())w.content.push_back(std::move(ascii));
    }
    return true;
}

inline void modern_frox_explored_map_controls(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(profile!="frox"||w.content_type!="map")return;
    /* Frox cave.c::do_cmd_view_map() accepts M/N/K/D when Auto-pick rules are
     * available, but unlike the Hengband-family implementation it draws no
     * legend at all.  Its fallback branch explicitly paints "Hit any key to
     * continue"; absence of that line on the recognized explored-map screen
     * is therefore the stable signal that the filter loop owns input. */
    const bool continuation=std::any_of(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[](const auto &line){
        return line.text_utf8.find("Hit any key to continue")!=std::string::npos;
    });
    if(continuation)return;
    const bool ja=w.language=="ja";
    modern_add_action(w,'M',ja?reinterpret_cast<const char*>(u8"拾う／確認して拾う対象を表示"):"Toggle auto-pick/query items");
    modern_add_action(w,'N',ja?reinterpret_cast<const char*>(u8"拾わない対象を表示"):"Toggle do-not-pick items");
    modern_add_action(w,'K',ja?reinterpret_cast<const char*>(u8"自動破棄対象を表示"):"Toggle auto-destroy items");
    modern_add_action(w,'D',ja?reinterpret_cast<const char*>(u8"拾う対象と拾わない対象をまとめて表示"):"Toggle combined pick/do-not-pick items");
}

inline void modern_frox_world_map_controls(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(profile!="frox")return;
    const bool world_map=std::any_of(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[](const auto &line){
        const auto text=modern_spaces(line.text_utf8);
        // The Japanese layer paints the heading as "広域の地図 [": the next fragment overwrites its trailing colon.
        const bool heading=text.find("World Map:")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"広域の地図 ["))!=std::string::npos;
        return heading&&(text.find("Look around")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"で見回す"))!=std::string::npos);
    });
    if(!world_map)return;
    // Direction keys remain owned by the native world-map movement loop.
    // Only expose the non-directional auxiliary operation as a context action.
    modern_add_action(w,'*',w.language=="ja"?reinterpret_cast<const char*>(u8"周囲を見る"):"Look around");
}

inline bool modern_character_sheet_content(const GameFrame &frame,ModernWindow &w) {
    if(frame.pre_game_menu||w.kind!="dialog"||w.stage!="detail")return false;
    const auto has_action=[&](int key){return std::any_of(w.actions.begin(),w.actions.end(),[&](const auto &action){return action.key==key;});};
    const bool named_sheet=w.title.find("Character Sheet")!=std::string::npos
        ||w.title.find("Character sheet")!=std::string::npos
        ||w.title.find(reinterpret_cast<const char*>(u8"キャラクターシート"))!=std::string::npos;
    const bool native_footer=std::any_of(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[](const auto &line){
        const auto &text=line.text_utf8;
        return text.find("'c'")!=std::string::npos&&text.find("'f'")!=std::string::npos
            &&text.find("'h'")!=std::string::npos&&text.find("ESC")!=std::string::npos;
    });
    // Hengband-family character sheets advertise these three native footer
    // operations: change name, write to file, and change display mode.  That
    // combination is much more specific than the generic "Menu" title and
    // avoids treating ordinary reports as character sheets. Gensoband keeps
    // the same three commands in its footer even when its adapter does not
    // expose them as ModernAction entries, so accept that native signature too.
    if(!(has_action('c')&&has_action('f')&&has_action('h'))&&!native_footer&&!named_sheet)return false;
    int fields=0;
    for(const auto &block:w.content)if(block.kind=="fields")fields+=int(block.cells.size());
    if(fields<12)return false;
    w.content_type="character";
    w.title=w.language=="ja"?reinterpret_cast<const char*>(u8"キャラクター情報"):"Character information";
    return true;
}

inline bool modern_sil_quick_help(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(profile!="sil"||w.stage!="detail")return false;
    const auto row=[&](int y)->const TermMirrorLine*{for(const auto &line:frame.menu_term_lines)if(line.source_row==y)return &line;return nullptr;};
    const auto header=row(1),prompt=row(23),hint=row(24);if(!header||!prompt)return false;
    const bool manual=hint&&(hint->text_utf8.find("manual")!=std::string::npos||hint->text_utf8.find(reinterpret_cast<const char*>(u8"マニュアル"))!=std::string::npos);
    const auto h=modern_spaces(header->text_utf8),p=modern_spaces(prompt->text_utf8);
    const bool named=h.find("Movement etc")!=std::string::npos||h.find("Terrain")!=std::string::npos||h.find("Superfluous")!=std::string::npos;
    if(!manual&&!named)return false;
    if(p.find("press any key")==std::string::npos&&p.find(reinterpret_cast<const char*>(u8"何かキー"))==std::string::npos)return false;
    const bool ja=w.language=="ja";const auto tr=[&](const char8_t *jp,const char *en){return std::string(ja?reinterpret_cast<const char*>(jp):en);};
    int page=1;
    if(const auto r=row(3);r&&r->text_utf8.find("7 8 9")!=std::string::npos)page=0;
    if(const auto r=row(16);r&&r->text_utf8.find("y k u")!=std::string::npos)page=2;
    w.title=tr(u8"基本操作","Quick reference");w.subtitle=std::to_string(page+1)+" / 3";w.content_type="quick_help";w.page=page;w.content.clear();w.rows.clear();w.actions.clear();w.cursor=-1;w.cursor_target.clear();w.input.clear();
    w.continue_key=13;w.prev_key='8';w.next_key=page<2?13:0;w.previous=page>0;w.next=page<2;
    if(page>0)w.actions.push_back({"quick-previous",tr(u8"前のページ","Previous page"),"",true,'8'});
    w.actions.push_back({"quick-next",page<2?tr(u8"次のページ","Next page"):tr(u8"ヘルプを閉じる","Close help"),"",true,13});
    w.actions.push_back({"quick-close",tr(u8"ヘルプを閉じる","Close help"),"",true,27});
    if(manual)w.actions.push_back({"quick-manual",tr(u8"詳しいマニュアルを開く","Open the full manual"),"",true,'m'});
    const auto slice=[&](int y,int from,int to){const auto r=row(y);if(!r)return std::string();const auto a=modern_column_byte(r->text_utf8,from),b=modern_column_byte(r->text_utf8,to);return modern_trim(r->text_utf8.substr(a,b-a));};
    const auto heading=[&](std::string text){if(text.empty())return;ModernBlock b;b.kind="heading";b.cells.push_back({"",std::move(text),false});w.content.push_back(std::move(b));};
    const auto prose=[&](std::string text){if(text.empty()||text.find("press any key")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"何かキー"))!=std::string::npos)return;ModernBlock b;b.kind="paragraph";b.cells.push_back({"",modern_spaces(text),false});w.content.push_back(std::move(b));};
    const auto directions=[&](std::string keys){
        const char8_t *jp[]={u8"左上",u8"上",u8"右上",u8"左",u8"待つ",u8"右",u8"左下",u8"下",u8"右下"};
        const char *en[]={"Up-left","Up","Up-right","Left","Wait","Right","Down-left","Down","Down-right"};
        for(int y=0;y<3;++y){ModernBlock b;b.kind="grid";for(int x=0;x<3;++x){const int i=y*3+x;b.cells.push_back({std::string(1,keys[i]),tr(jp[i],en[i]),false});}w.content.push_back(std::move(b));}
    };
    const auto entries=[&](int from,int to,int first,int last,bool symbols){
        ModernBlock block;block.kind="fields";
        for(int y=first;y<=last;++y) {
            auto text=slice(y,from,to);if(text.empty()||text.find("press any key")!=std::string::npos||text.find(reinterpret_cast<const char*>(u8"何かキー"))!=std::string::npos)continue;
            const auto space=text.find(' ');if(space==std::string::npos){prose(text);continue;}
            const auto key=text.substr(0,space),meaning=modern_trim(text.substr(space+1));if(meaning.empty())continue;
            if(!symbols) {
                const auto name=key.size()==2&&key[0]=='^'?"Ctrl+"+std::string(1,char(std::toupper(static_cast<unsigned char>(key[1])))):key;
                block.cells.push_back({name,meaning,false});continue;
            }
            const auto r=row(y);const auto base=modern_column_byte(r->text_utf8,from);const auto at=r->text_utf8.find(key,base);
            // Multiple ward glyphs have different meanings by colour. Keep
            // each swatch with its symbol and description.
            for(size_t i=0;i<key.size();++i) {
                int color=r->color;for(const auto &span:r->color_spans)if(int(at+i)>=span.begin&&int(at+i)<span.begin+span.len){color=span.color;break;}
                const auto &rgb=frame.term_palette.rgb[color&15];char hex[8];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",rgb[0],rgb[1],rgb[2]);
                ModernCell cell{std::string(1,key[i]),meaning,false};cell.color=hex;block.cells.push_back(std::move(cell));
            }
        }
        if(!block.cells.empty())w.content.push_back(std::move(block));
    };
    if(page==0) {
        heading(slice(1,0,51));directions("789456123");
        std::string movement;for(int y=3;y<=5;++y){auto text=slice(y,11,51);if(!text.empty()){if(!movement.empty()&&!ja)movement+=' ';movement+=text;}}prose(movement);prose(slice(7,11,51));
        for(int y=9;y<=23;++y)prose(slice(y,0,51));
        heading(slice(1,51,80));entries(51,80,3,22,false);
    } else if(page==1) {
        for(const auto [from,to]:std::initializer_list<std::pair<int,int>>{{0,26},{26,51},{51,80}}){heading(slice(1,from,to));entries(from,to,3,22,from<51);}
    } else {
        heading(slice(1,0,34));entries(0,34,3,22,false);
        heading(slice(1,34,80));entries(34,80,3,12,false);
        heading(slice(14,34,80));directions("ykuhzlbjn");
        for(int y=16;y<=21;++y)prose(slice(y,43,80));
    }
    if(manual){ModernBlock b;b.kind="links";b.cells.push_back({"",tr(u8"詳しいマニュアルを開く","Open the full manual"),false,"quick-manual"});w.content.push_back(std::move(b));}
    return true;
}

inline bool modern_poker_content(const GameFrame &frame,ModernWindow &w) {
    const auto line=[&](int y)->const TermMirrorLine*{for(const auto &r:frame.menu_term_lines)if(r.source_row==y)return &r;return nullptr;};
    const auto top=line(0),border=line(5),state=line(14),done=line(16);
    if(!top||!border||!state||!done)return false;
    if(top->text_utf8.find("Keep which cards")==std::string::npos&&top->text_utf8.find(reinterpret_cast<const char*>(u8"残すカード"))==std::string::npos)return false;
    if(border->text_utf8.find("+------------+")==std::string::npos&&border->text_utf8.find(reinterpret_cast<const char*>(u8"┏━━━━━━┓"))==std::string::npos)return false;
    const auto slice=[](const std::string &s,int a,int b){const auto first=modern_column_byte(s,a),last=modern_column_byte(s,b);return s.substr(first,last-first);};
    int selected=5;
    if(frame.menu_term_curs_row>=0&&frame.menu_term_curs_row<int(frame.menu_term_lines.size())&&frame.menu_term_lines[frame.menu_term_curs_row].source_row==14)
        selected=std::clamp((frame.menu_term_curs_col-5)/16,0,4);
    w.content_type="poker";w.stage="cursor";w.title=w.language=="ja"?reinterpret_cast<const char*>(u8"ポーカー"):"Poker";
    w.rows.clear();w.content.clear();w.actions.clear();w.body.clear();w.subtitle.clear();w.input.clear();w.cursor=selected;w.continue_key=13;w.prev_key=w.next_key=0;
    ModernBlock cards;cards.kind="cards";
    for(int i=0;i<5;++i) {
        ModernRow row;row.id="poker:"+std::to_string(i);row.label=modern_trim(slice(state->text_utf8,5+16*i,11+16*i));row.key=13;row.slot=14;row.selection=5+16*i;
        ModernCell card;card.label=row.label;card.target=row.id;card.selected=i==selected;
        for(int y=6;y<=12;++y){if(y>6)card.text+='\n';if(const auto r=line(y))card.text+=slice(r->text_utf8,2+16*i,14+16*i);}
        if(const auto r=line(6)) {
            const int at=int(modern_column_byte(r->text_utf8,2+16*i));int color=r->color;
            for(const auto &span:r->color_spans)if(at>=span.begin&&at<span.begin+span.len){color=span.color;break;}
            const auto &rgb=frame.term_palette.rgb[color&15];char hex[8];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",rgb[0],rgb[1],rgb[2]);card.color=hex;
        }
        cards.cells.push_back(std::move(card));w.rows.push_back(std::move(row));
    }
    ModernRow finish;finish.id="poker:done";finish.label=modern_trim(done->text_utf8);finish.key=13;finish.slot=16;finish.selection=38;w.rows.push_back(std::move(finish));
    w.cursor_target=w.rows[selected].id;w.content.push_back(std::move(cards));
    for(const auto &r:frame.menu_term_lines)if(r.source_row<5||r.source_row>16) {
        auto text=modern_trim(r.text_utf8);if(text.empty())continue;
        ModernBlock b;b.kind="paragraph";b.cells.push_back({"",std::move(text),false});w.content.push_back(std::move(b));
    }
    ModernBlock current;current.kind="fields";current.cells.push_back({w.language=="ja"?reinterpret_cast<const char*>(u8"選択中"):"Selected",selected<5?std::to_string(selected+1)+": "+w.rows[selected].label:w.rows[selected].label,false});w.content.push_back(std::move(current));
    return true;
}
//! 空白で区切った語に分ける（全角の字の途中は割らない。区切りは半角の空白とタブだけ）。
inline std::vector<std::string> modern_words(const std::string &text) {
    std::vector<std::string> words;size_t at=0;
    while(at<text.size()) {
        while(at<text.size()&&(text[at]==' '||text[at]=='\t'))++at;
        const size_t begin=at;
        while(at<text.size()&&text[at]!=' '&&text[at]!='\t')++at;
        if(at>begin)words.push_back(text.substr(begin,at-begin));
    }
    return words;
}
inline bool modern_all_digits(const std::string &text) {
    return !text.empty()&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>='0'&&c<='9';});
}
/*!
 * @brief 呪文の表（覚える・唱える・調べるの呪文の選択）をカーソル式の一覧にする。
 * @details 変愚・短愚・幻想蛮怒・Frox は、本を選んだあとに「名前 [熟練度] Lv MP 失率% 効果」の表を端末へ描き、
 * 行を選ぶ問いの 1 行目（`(呪文:a-h, '*'で一覧, ESCで中断) どの呪文を学びますか?`）の下でカーソル（`》` か `>`）を動かす。
 * 以前はこの表を汎用の「fields」に割っていた。見出しと行で列の数が合わず、行ごとに列が折り返して崩れ、
 * 左の欄には「一覧」（`*` の操作）だけが残って、どの呪文を選ぶのか読めなかった。
 * 見出し（`Lv` と `MP`/`SP`/`Mana`/`マナ` を含む行）の下で、数値 3 つ（Lv・MP・失率%）を目印に名前と効果を切り分け、
 * 呪文 1 つを 1 行の選択肢にする。カーソルはコアのものが正で、移動は `modern_focus_key()` の上下、決定は Enter。
 * 左の欄の幅を超える文は切られるので、Lv・MP・失率・効果は右の欄の説明へ出す。
 */
inline bool modern_spell_list(const GameFrame &frame,ModernWindow &w) {
    if(w.stage!="cursor"||frame.pre_game_menu||frame.menu_term_lines.size()<3)return false;
    const auto &cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    const int lines=int(frame.menu_term_lines.size());
    if(cursor.line_index<2||cursor.line_index>=lines||cursor.span_begin<0)return false;
    // 左の欄（ステータス）の字が同じ行に載るコアがある。カーソルの桁から右だけを読む。
    const auto cut=[&](int index){
        const auto &text=frame.menu_term_lines[index].text_utf8;size_t at=size_t(cursor.span_begin);
        if(at>=text.size())return std::string();
        while(at<text.size()&&(static_cast<unsigned char>(text[at])&0xc0)==0x80)++at;
        return text.substr(at);
    };
    const auto is_marker=[](const std::string &word){return word==reinterpret_cast<const char*>(u8"》")||word==">"||word=="*";};
    // 見出し：カーソル行から上へ、Lv と MP の列名を持つ最初の行。
    int header=-1;std::vector<std::string> head;
    for(int i=cursor.line_index-1;i>=1&&header<0;--i) {
        auto words=modern_words(cut(i));
        const bool level=std::find(words.begin(),words.end(),"Lv")!=words.end();
        bool mana=false;
        for(const auto &word:words)if(word=="MP"||word=="SP"||word=="Mana"||word==reinterpret_cast<const char*>(u8"マナ"))mana=true;
        if(level&&mana){header=i;head=std::move(words);}
    }
    if(header<0||head.size()<4)return false;
    // 列の名前（見出しの語）。Lv の直前が名前の欄でなければ熟練度の欄、末尾の 3 語が MP・失率・効果。
    const auto level_at=size_t(std::find(head.begin(),head.end(),"Lv")-head.begin());
    const std::string grade_word=(level_at>=2&&head[level_at-1][0]!='(')?head[level_at-1]:std::string();
    const std::string mana_word=head[head.size()-3],fail_word=head[head.size()-2],info_word=head.back();
    struct Entry{int line{};std::string name,grade,level,mana,fail,info;};
    std::vector<Entry> entries;int selected=-1;
    for(int i=header+1;i<lines;++i) {
        auto words=modern_words(cut(i));
        if(!words.empty()&&is_marker(words.front()))words.erase(words.begin());
        int at=-1;
        for(size_t k=1;k+2<words.size();++k)if(modern_all_digits(words[k])&&modern_all_digits(words[k+1])&&words[k+2].size()>1&&words[k+2].back()=='%'
            &&modern_all_digits(words[k+2].substr(0,words[k+2].size()-1))){at=int(k);break;}
        if(at<1)break;
        Entry e;e.line=i;size_t name_end=size_t(at);
        if(name_end>=2&&words[name_end-1].size()>=2&&words[name_end-1].front()=='['&&words[name_end-1].back()==']') {
            e.grade=words[name_end-1].substr(1,words[name_end-1].size()-2);--name_end;
        }
        for(size_t k=0;k<name_end;++k){if(k)e.name+=' ';e.name+=words[k];}
        e.level=words[size_t(at)];e.mana=words[size_t(at)+1];e.fail=words[size_t(at)+2];
        for(size_t k=size_t(at)+3;k<words.size();++k){if(!e.info.empty())e.info+=' ';e.info+=words[k];}
        if(e.name.empty())break;
        if(i==cursor.line_index)selected=int(entries.size());
        entries.push_back(std::move(e));
    }
    if(entries.empty()||selected<0)return false;
    const bool ja=w.language=="ja";
    w.rows.clear();w.content.clear();w.body.clear();w.cursor_target.clear();
    for(const auto &e:entries) {
        ModernRow row;
        row.label=e.name;row.slot=frame.menu_term_lines[e.line].source_row>0?frame.menu_term_lines[e.line].source_row:e.line;row.selection=0;
        row.price_known=false;row.available=true;
        unsigned long long identity=1469598103934665603ULL;for(unsigned char c:e.name){identity^=c;identity*=1099511628211ULL;}
        row.id="spell:"+std::to_string(row.slot)+":"+std::to_string(identity);
        row.description="Lv "+e.level+"   "+mana_word+" "+e.mana+"   "+fail_word+" "+e.fail;
        if(!e.grade.empty())row.description+="\n"+(grade_word.empty()?std::string(ja?reinterpret_cast<const char*>(u8"熟練度"):"Proficiency"):grade_word)+" ["+e.grade+"]";
        if(!e.info.empty())row.description+="\n"+info_word+": "+e.info;
        row.actions.push_back({"choice",row.label,"",true,13});
        if(int(&e-entries.data())==selected)w.cursor_target=row.id;
        w.rows.push_back(std::move(row));
    }
    // 伝送の検証（`decode_modern_window`）は、カーソルの範囲を本文の行の中に置くことを求める。本文は空なので範囲は持たない。
    w.cursor=selected;w.content_type="spells";w.cursor_span_begin=-1;w.cursor_span_len=0;
    // 見出しは行 0 の問い。先頭の「(呪文:a-h, '*'で一覧, ESCで中断)」は操作の案内なので題から外す。
    auto heading=modern_trim(frame.menu_term_lines.front().text_utf8);
    if(heading.starts_with("(")) {
        const auto close=heading.find(") ");
        if(close!=std::string::npos&&close+2<heading.size())heading=modern_trim(heading.substr(close+2));
    }
    if(!heading.empty())w.title=heading;
    // `*`（一覧の出し入れ）は、一覧がもう出ているので操作から外す。
    w.actions.erase(std::remove_if(w.actions.begin(),w.actions.end(),[](const auto &a){return a.key=='*';}),w.actions.end());
    return true;
}
/*!
 * @brief メッセージ履歴（Ctrl+P）の見出し行か。
 * @details コアごと・言語ごとに文言が違う（変愚・短愚・幻想蛮怒 `以前のメッセージ 0-4 全部で(5)` / `Message Recall (0-4 of 5)`、
 * Sil-Q `前の文言（0-18 / 19 件）、0 件目から`、Angband `メッセージ履歴 (0-22 / 59)、位置 0` / `Message recall (…`）。
 * Frox（`[過去のメッセージ  404/436 行]`）は文書の枠（`modern_document_info`）で扱うのでここへは来ない。
 */
inline bool modern_history_header(std::string_view text) {
    for(const char *head:{"Message Recall (","Message recall (",reinterpret_cast<const char*>(u8"以前のメッセージ"),
        reinterpret_cast<const char*>(u8"前の文言（"),reinterpret_cast<const char*>(u8"メッセージ履歴 (")})if(text.starts_with(head))return true;
    return false;
}
/*!
 * @brief メッセージ履歴の窓を、見出し・本文・脚注に分けて組み直す。
 * @details 原作の画面は 1 行目が見出し（件数）、最後の行が脚注（`[ 'p' で更に古いもの, … ESC で中断 ]`）で、間が 1 行 1 メッセージ。
 * 汎用の組み立てだと見出しが窓の題に（Angband は「Angband」）、脚注が選択肢や本文（`[Press`）に割れていた（2026-10-10 の報告）。
 * ここで題を「メッセージ履歴」に固定し、見出しを副題に、脚注を捨てて**操作**（古い方・新しい方・検索）と**ページ送り**のキーにし、
 * 本文は 1 メッセージ 1 段落にする。Frox は文書の枠で組むので対象外。見出しと脚注の両方が揃った画面だけを組み直す
 * （検索の入力中などは脚注が無いので汎用のまま）。
 */
inline bool modern_message_history(const GameFrame &frame,ModernWindow &w,const std::string &profile) {
    if(profile=="frox"||w.kind!="dialog"||(w.stage!="detail"&&w.stage!="choices"&&w.stage!="cursor"))return false;
    const auto &lines=frame.menu_term_lines;
    int header=-1,footer=-1;
    for(size_t i=0;i<lines.size();++i) {
        const auto text=modern_trim(lines[i].text_utf8);
        if(text.empty())continue;
        if(modern_history_header(text))header=int(i);
        break;
    }
    if(header<0)return false;
    for(size_t i=lines.size();i-->size_t(header)+1;) {
        const auto text=modern_trim(lines[i].text_utf8);
        if(text.empty())continue;
        if(text.starts_with('[')&&text.ends_with(']')&&text.find("ESC")!=std::string::npos)footer=int(i);
        break;
    }
    if(footer<0)return false;
    const bool ja=w.language=="ja";
    const auto footer_text=modern_trim(lines[size_t(footer)].text_utf8);
    const auto has=[&](const char *key){return footer_text.find(key)!=std::string::npos;};
    // 古い方・新しい方のキー。Angband は脚注に書かず `menu_page_prev_key` / `menu_page_next_key` で申告する。
    const int older=frame.menu_page_prev_key?frame.menu_page_prev_key:(has("'p'")?'p':0);
    const int newer=frame.menu_page_next_key?frame.menu_page_next_key:(has("'n'")?'n':0);
    // 検索のキー。Angband は `=`、変愚系は `/`（英語の脚注は `...` と省いているが、`/` で検索できる）。
    const int find=has("'/'")?'/':(has("'='")?'=':(older?'/':0));
    w.title=ja?reinterpret_cast<const char*>(u8"メッセージ履歴"):"Message history";
    w.subtitle=modern_trim(lines[size_t(header)].text_utf8);
    w.rows.clear();w.cursor=-1;w.cursor_target.clear();w.stage="detail";
    w.content.clear();w.actions.clear();w.input.clear();
    for(int i=header+1;i<footer;++i) {
        const auto text=modern_trim(lines[size_t(i)].text_utf8);
        if(text.empty())continue;
        ModernBlock block;block.kind="paragraph";block.cells.push_back({"",modern_spaces(text),false});w.content.push_back(std::move(block));
    }
    if(older)modern_add_action(w,older,ja?reinterpret_cast<const char*>(u8"更に古いメッセージ"):"Older messages");
    if(newer)modern_add_action(w,newer,ja?reinterpret_cast<const char*>(u8"更に新しいメッセージ"):"Newer messages");
    if(find)modern_add_action(w,find,ja?reinterpret_cast<const char*>(u8"検索"):"Find");
    w.prev_key=older;w.next_key=newer;w.previous=older!=0;w.next=newer!=0;
    return true;
}
inline void interpret_modern_content(const GameFrame &frame,ModernWindow &w,const std::string &profile="generic") {
    const ModernColumnScope column_scope(profile);
    if(w.stage.empty())return;
    w.content.clear();w.actions.clear();w.cursor_target.clear();w.content_type="document";
    modern_edit_content(frame,w,profile);
    if(w.stage!="text"&&w.stage!="number"&&w.stage!="confirm")w.input.clear();
    if((w.message_only||message_more_pending(frame))&&!frame.numeric.active&&frame.prompt.choices.empty()) {
        if(w.kind!="dialog") {
            w.stage="message";w.continue_key=' ';w.input=modern_message(frame);
            w.content_type=w.kind=="building"?"services":"items";
            if(w.kind=="building") {
                // A facility result such as a rumour is content, not an input
                // prompt.  Keep consecutive -more- pages in the right pane;
                // the fixed footer then contains only the Continue control.
                const auto page=modern_trim(w.input);
                if(!page.empty()&&w.body.find(page)==std::string::npos) {
                    if(!w.body.empty()&&!w.body.ends_with('\n'))w.body+='\n';
                    w.body+=page;
                }
                w.input.clear();w.content.clear();
                ModernBlock block;block.kind="paragraph";block.cells.push_back({"",modern_spaces(w.body),false});w.content.push_back(std::move(block));
            }
            return;
        }
        w.message_only=true;w.content_type="message";w.stage="detail";w.continue_key=' ';
        w.body=modern_message(frame);w.input.clear();w.rows.clear();w.cursor=-1;
        ModernBlock block;block.kind="paragraph";block.cells.push_back({"",w.body,false});w.content.push_back(std::move(block));
        w.title=w.language=="ja"?reinterpret_cast<const char*>(u8"メッセージ"):"Message";
        if(profile=="frox") {
            modern_add_action(w,'n',w.language=="ja"?reinterpret_cast<const char*>(u8"このまとまりの残りを読み飛ばす"):"Skip the rest of this message block");
            w.actions.push_back({"key:27",w.language=="ja"?reinterpret_cast<const char*>(u8"残りをすべて読み飛ばす"):"Skip all remaining messages","",true,27});
            modern_add_action(w,'?',w.language=="ja"?reinterpret_cast<const char*>(u8"メッセージ操作のヘルプ"):"Message controls help");
        }
        return;
    }
    if(w.stage=="confirm") {
        int yes=-1,no=-1,cancel=-1;
        for(size_t i=0;i<frame.prompt.choices.size();++i){const int key=frame.prompt.choices[i].key;if(key=='y'||key=='Y'||key=='o'||key=='O')yes=int(i);if(key=='n'||key=='N'||key=='c'||key=='C')no=int(i);if(key==27)cancel=int(i);}
        w.default_answer=cancel>=0?cancel:0;
        if(frame.prompt.choices.size()==2&&yes>=0&&no>=0)w.default_answer=frame.prompt.text_utf8.find("[Y/n]")!=std::string::npos?yes:no;
    }
    if(w.stage=="confirm"&&frame.prompt.choices.size()==2) {
        auto input=modern_trim(w.input);
        if(input.size()>=5) {
            const size_t at=input.size()-5;
            if(input[at]=='['&&input[at+4]==']'&&(input[at+2]=='/'||input[at+2]==',')) {
                const auto a=input[at+1],b=input[at+3];
                if((a=='y'||a=='Y')&&(b=='n'||b=='N'))input=modern_trim(input.substr(0,at));
            }
        }
        w.input=input.empty()?(w.language=="ja"?reinterpret_cast<const char*>(u8"選択してください。"):"Choose an answer."):std::move(input);
    }
    // Item and service models already carry names, counts, prices and actions.
    if(w.kind!="dialog") {
        w.content_type=w.kind=="building"?"services":"items";
        if(w.kind=="building"&&!w.body.empty()) {
            ModernBlock block;block.kind="paragraph";block.cells.push_back({"",modern_spaces(w.body),false});w.content.push_back(std::move(block));
            w.body=modern_spaces(w.body);
        }
        return;
    }
    const auto document=modern_document_info(frame);
    if(!document.active&&modern_spell_list(frame,w))return;
    const bool document_modal=w.stage=="text"||w.stage=="number"||w.stage=="confirm";
    if(document.active&&!document_modal){w.stage=document.menu?"choices":"detail";if(!document.menu)w.rows.clear();w.cursor=-1;}
    const auto cursor=frame.menu_core_cursors.empty()?frame.menu_core_cursor:frame.menu_core_cursors.back();
    int focus_begin=cursor.span_begin,focus_end=cursor.span_begin+cursor.span_len;
    if(cursor.line_index>=0&&cursor.line_index<int(frame.menu_term_lines.size())&&cursor.span_len<=0) {
        const auto &line=frame.menu_term_lines[cursor.line_index].text_utf8;
        focus_begin=int(modern_column_byte(line,std::max(0,frame.menu_term_curs_col)));focus_end=focus_begin+1;
    }
    std::string original_body;
    for(const auto &line:frame.menu_term_lines)original_body+=line.text_utf8+"\n";
    const auto original_compact=modern_spaces(original_body);
    const bool smithing=profile=="sil"&&(w.title=="Smithing"||w.title==reinterpret_cast<const char*>(u8"鍛冶"));
    bool report_lines=false;
    for(const char *name:{"known artifacts","Known artifacts","unique monsters","Unique Monsters","Monsters Killed","Bounties",reinterpret_cast<const char*>(u8"伝説のアイテム"),reinterpret_cast<const char*>(u8"ユニーク・モンスター"),reinterpret_cast<const char*>(u8"倒した敵"),reinterpret_cast<const char*>(u8"賞金首"),reinterpret_cast<const char*>(u8"我が家のアイテム")})if(document.active&&document.caption.find(name)!=std::string::npos)report_lines=true;
    // Message history (Ctrl+P). Every source line is one message, so a line made only of symbols
    // ("====================" that every core adds at birth) is a message too, not a decorative rule.
    bool message_history=false;
    for(const char *name:{reinterpret_cast<const char*>(u8"前の文言（"),"Previous Messages",reinterpret_cast<const char*>(u8"以前のメッセージ"),
        reinterpret_cast<const char*>(u8"過去のメッセージ"),"Message Recall (","Message recall (",reinterpret_cast<const char*>(u8"メッセージ履歴 (")})if(original_body.find(name)!=std::string::npos)message_history=true;
    if(message_history)report_lines=true;
    for(auto &row:w.rows) {
        if(modern_spaces(row.description)==original_compact)row.description.clear();
        else if(!row.description.empty()) {
            std::string prose;size_t begin=0;bool gap=false;
            while(begin<row.description.size()) {
                const auto end=row.description.find('\n',begin);
                const auto line=modern_spaces(row.description.substr(begin,end==std::string::npos?end:end-begin));
                if(line.empty())gap=!prose.empty();
                else {if(!prose.empty())prose+=gap?"\n\n":((static_cast<unsigned char>(prose.back())<128&&static_cast<unsigned char>(line.front())<128)?" ":"");prose+=line;gap=false;}
                if(end==std::string::npos)break;begin=end+1;
            }
            row.description=std::move(prose);
        }
    }
    std::vector<std::string> table_headers;
    std::vector<std::pair<int,std::string>> stat_columns;
    const bool interactive=w.stage=="cursor";
    const bool choice_surface=interactive&&modern_cursor_choice_grid(frame);
    const bool linear_grid=choice_surface&&original_body.find(reinterpret_cast<const char*>(u8"あなたの登場作品"))!=std::string::npos;
    if(interactive)w.content_type="selection";
    else if(w.stage=="choices")w.content_type=modern_birth_screen(frame)?"character":"menu";
    else if(w.stage=="text"||w.stage=="number"||w.stage=="confirm")w.content_type="form";
    std::string paragraph;
    const auto flush=[&](){if(paragraph.empty())return;ModernBlock b;b.kind="paragraph";b.cells.push_back({"",std::move(paragraph),false});w.content.push_back(std::move(b));paragraph.clear();};
    bool found_heading=false;
    for(size_t i=0;i<frame.menu_term_lines.size();++i) {
        const auto &source=frame.menu_term_lines[i];std::string line=source.text_utf8;
        if(document.active&&(int(i)==document.header||int(i)==document.footer)){flush();continue;}
        if((frame.numeric.active&&int(i)==frame.numeric.line_index)||(!frame.prompt.choices.empty()&&int(i)==frame.prompt.line_index)
            ||(w.stage=="text"&&int(i)==frame.menu_term_curs_row)){flush();continue;}
        // Choice buttons own these fragments. Leave surrounding explanations.
        if((!document.active||(document.menu&&!document_modal))&&(w.stage=="choices"||w.stage=="text"||w.stage=="number"||w.stage=="confirm"))for(const auto &choice:frame.menu_choices)if(choice.line_index==int(i)&&choice.span_begin>=0&&choice.span_len>0&&choice.span_begin<int(line.size()))
            line.replace(choice.span_begin,std::min(choice.span_len,int(line.size())-choice.span_begin),std::min(choice.span_len,int(line.size())-choice.span_begin),' ');
        if(smithing&&w.stage=="choices")for(const auto &cell:modern_split_cells(line))if(modern_source_choice(cell))
            line.replace(size_t(cell.begin),size_t(cell.end-cell.begin),size_t(cell.end-cell.begin),' ');
        if(!document.active&&modern_control_line(frame,source)){auto interpreted_source=source;interpreted_source.text_utf8=line;line=modern_interpret_controls(frame,interpreted_source,w);}
        auto trimmed=modern_trim(line);
        const bool command_prompt=trimmed=="Command:"||trimmed==reinterpret_cast<const char*>(u8"コマンド:")||trimmed==reinterpret_cast<const char*>(u8"命令:");
        const bool page_marker=w.stage=="choices"&&(trimmed=="-more-"||trimmed==reinterpret_cast<const char*>(u8"-続く-"));
        if(!document.active&&(command_prompt||page_marker)){flush();continue;}
        std::string lower=trimmed;for(char &c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        if(lower=="[ press any key ]"||lower=="(press any key)"||lower=="[press any key]") {flush();continue;}
        if(trimmed.empty()||trimmed==">"||(!message_history&&modern_frame_rule(trimmed))){flush();continue;}
        if(frame.pre_game_menu&&(trimmed.starts_with("Up/Down or 8/2")||trimmed.starts_with("Pick the save to delete")||trimmed.starts_with("Arrows to choose,")
            ||trimmed.starts_with(reinterpret_cast<const char*>(u8"上下左右で選ぶ／")))){flush();continue;}
        if(trimmed.starts_with("=== ")&&trimmed.ends_with(" ===")) {trimmed=modern_trim(trimmed.substr(4,trimmed.size()-8));line=trimmed;}
        if(trimmed.front()=='|'&&trimmed.back()=='|'&&trimmed.size()>2){line.front()=' ';line.back()=' ';trimmed=modern_trim(line);}
        auto cells=modern_split_cells(line);
        const bool parsed_stat_header=modern_stat_header_cells(line,cells);
        if(cells.empty())continue;
        const bool selected=interactive&&int(i)==cursor.line_index;
        bool choice_grid=choice_surface&&std::any_of(cells.begin(),cells.end(),modern_source_choice);
        bool stats_header=parsed_stat_header;
        if(stats_header){flush();table_headers.clear();for(const auto &c:cells)table_headers.push_back(c.text);}
        // A column-aligned table inside a help text file (Angband's commands.txt:
        // "  a    Aim a wand                      A    Activate an object").  The
        // generic "fields" layout gives every line its own equal-width columns,
        // so the rows no longer line up and long cells wrap into the next row
        // .  The document font is
        // fixed-pitch (full width = two half-width cells), so keep the source
        // line as preformatted text instead.  Prose with two spaces after a
        // full stop is not a table: require an indented line or a gap of three
        // or more spaces.
        if(document.active&&document.total>0&&!document.menu&&cells.size()>=2&&!stats_header) {
            bool wide_gap=line.starts_with("  ");
            for(size_t c=1;c<cells.size()&&!wide_gap;++c)wide_gap=cells[c].begin-cells[c-1].end>=3;
            if(wide_gap) {
                flush();auto raw=line;while(!raw.empty()&&(raw.back()==' '||raw.back()=='	'))raw.pop_back();
                ModernBlock code;code.kind="code";code.cells.push_back({"",std::move(raw),false});w.content.push_back(std::move(code));
                continue;
            }
        }
        const auto field_colon=trimmed.find(':');
        const bool named_field=!document.active&&source.source_row<=7&&field_colon>0&&field_colon<32
            &&trimmed.find("://")==std::string::npos&&trimmed.find(reinterpret_cast<const char*>(u8"。"))==std::string::npos;
        const bool split=cells.size()>1||selected||choice_grid||named_field;
        if(split) {
            flush();ModernBlock block;block.kind=choice_grid?"grid":"fields";
            // The core's selected cell is mapped before stripping key labels.
            for(const auto &cell:cells) {
                ModernCell value;value.text=choice_grid&&modern_source_choice(cell)?modern_choice_label(cell.text):modern_spaces(cell.text);
                value.selected=selected&&(choice_grid?(focus_begin>=cell.begin-2&&focus_begin<cell.end):(cell.begin<focus_end&&cell.end>focus_begin));
                const auto colon=value.text.find(':');
                if(colon!=std::string::npos&&colon>0&&colon<48&&value.text.find("://")==std::string::npos
                    &&value.text.substr(0,colon).find_first_not_of("0123456789 +-")!=std::string::npos) {
                    value.label=modern_trim(value.text.substr(0,colon));value.text=modern_trim(value.text.substr(colon+1));
                }
                if(choice_grid&&modern_source_choice(cell))modern_grid_target(w,value,cell,line,int(i),linear_grid);
                block.cells.push_back(std::move(value));
            }
            if(selected&&std::none_of(block.cells.begin(),block.cells.end(),[](const auto &c){return c.selected;})) {
                // Some cores place their caret immediately before the option.
                auto nearest=std::find_if(cells.begin(),cells.end(),[&](const auto &c){return c.end>focus_begin;});
                block.cells[nearest==cells.end()?0:nearest-cells.begin()].selected=true;
            }
            if(stats_header)block.kind="table_header";
            else if(!table_headers.empty()&&block.cells.size()>=table_headers.size()-1)block.kind="table_row";
            else if(choice_grid)table_headers.clear();
            interpret_modern_stat_columns(line,cells,stats_header,block,stat_columns,w.language);
            if(block.kind=="fields")modern_document_pairs(block);
            if(!block.cells.empty())w.content.push_back(std::move(block));
        } else {
            const bool heading=document.active?modern_document_heading(source):(!found_heading&&source.source_row<=3&&trimmed.size()<100
                &&trimmed.find(reinterpret_cast<const char*>(u8"。"))==std::string::npos&&trimmed.find(". ")==std::string::npos&&modern_heading_candidate(trimmed));
            if(heading){flush();ModernBlock b;b.kind="heading";b.cells.push_back({"",modern_spaces(trimmed),false});w.content.push_back(std::move(b));found_heading=true;}
            else {
                if(!paragraph.empty()) {
                    const unsigned char last=paragraph.back(),first=trimmed.front();
                    if(last<128&&first<128)paragraph+=' ';
                }
                paragraph+=modern_spaces(trimmed);if(report_lines)flush();
            }
        }
    }
    flush();
    // Save names are literal data supplied by the bootstrap declaration.
    // A filename such as "a) Name" must not be mistaken for another key prefix.
    bool save_labels=false;
    if(frame.pre_game_menu)for(const auto &line:frame.menu_term_lines)if(line.source_row<=2) {
        const auto title=modern_trim(line.text_utf8);
        for(const char *known:{"=== Load Game ===","=== Delete Save ===","Load Game","Delete Save","Choose the save to delete","Saved Games",reinterpret_cast<const char*>(u8"セーブデータを読み込む"),reinterpret_cast<const char*>(u8"削除するデータを選ぶ"),reinterpret_cast<const char*>(u8"セーブデータの削除")})if(title==known)save_labels=true;
    }
    // Labels and prose are normalized once at the boundary, never in paint code.
    for(auto &row:w.rows){if(!save_labels)row.label=modern_spaces(modern_choice_label(row.label));for(auto &a:row.actions)if(a.id=="choice")a.label=row.label;}
    if(w.title.empty()) {
        for(const auto &block:w.content)if(block.kind=="heading"&&!block.cells.empty()){w.title=block.cells.front().text;break;}
        if(w.title.empty()) {
            if(frame.game_over)w.title=w.language=="ja"?reinterpret_cast<const char*>(u8"ゲームオーバー"):"Game over";
            else w.title=frame.pre_game_menu?"Character":"Menu";
        }
    }
    const bool japanese=w.language=="ja";
    const auto has_key=[&](int key){return std::any_of(w.rows.begin(),w.rows.end(),[&](const auto &r){return r.key==key;});};
    const bool character_form=w.stage=="choices"&&std::any_of(w.rows.begin(),w.rows.end(),[](const auto &r){return r.key=='n'&&(r.label=="Change Name"||r.label==reinterpret_cast<const char*>(u8"名前を変える"));});
    if(frame.game_over) {
        //! 死亡後の画面は誕生の題・操作の名前を付けない（修-10）。文字入力の窓（最期の言葉・ファイル名）だけは項目名を題にする。
        if(w.stage=="text"&&!w.input_label.empty())w.title=modern_input_title(w.input_label,japanese);
    } else if(frame.pre_game_menu) {
        if(w.title.starts_with("Name")||w.title.starts_with(reinterpret_cast<const char*>(u8"名前")))w.title=japanese?reinterpret_cast<const char*>(u8"キャラクター作成"):"Character creation";
        if(w.stage=="confirm")w.title=japanese?reinterpret_cast<const char*>(u8"選択内容の確認"):"Confirm selection";
        if(w.stage=="text"&&!w.input_label.empty())w.title=modern_input_title(w.input_label,japanese);
        else if(w.rows.size()==3&&has_key('N')&&has_key('L')&&has_key('Q'))w.content_type="start";
        else if(has_key('a')&&has_key('b')&&has_key('c')&&has_key('d')&&original_body.find("Sil ")!=std::string::npos){w.title="Sil-Q";w.content_type="start";}
        else if(character_form){w.title=japanese?reinterpret_cast<const char*>(u8"キャラクター作成"):"Character creation";w.content_type="character";}
        else {
            struct Heading {const char *source;const char8_t *ja;const char *en;};
            const Heading headings[]={
                {"Choose a sex",u8"性別の選択","Choose a sex"},{reinterpret_cast<const char*>(u8"性別を選"),u8"性別の選択","Choose a sex"},
                {"Choose a race",u8"種族の選択","Choose a race"},{reinterpret_cast<const char*>(u8"種族を選"),u8"種族の選択","Choose a race"},
                {"Choose a class",u8"職業の選択","Choose a class"},{reinterpret_cast<const char*>(u8"職業を選"),u8"職業の選択","Choose a class"},
                {"Choose a personality",u8"性格の選択","Choose a personality"},{reinterpret_cast<const char*>(u8"性格を選"),u8"性格の選択","Choose a personality"},
                {reinterpret_cast<const char*>(u8"遊び方を選"),u8"遊び方の選択","Game mode"},{"Choose a game mode",u8"遊び方の選択","Game mode"},
                {reinterpret_cast<const char*>(u8"あなたの登場作品"),u8"登場作品の選択","Choose a work"},
                {"Allocate stats",u8"能力値の設定","Attributes"},{reinterpret_cast<const char*>(u8"能力値に点を"),u8"能力値の設定","Attributes"}
            };
            for(const auto &h:headings)if(original_body.find(h.source)!=std::string::npos){w.title=japanese?reinterpret_cast<const char*>(h.ja):h.en;break;}
        }
        // Birth headers abbreviate these commands to a single vague word.
        // Keep the original keys, but state their complete result in the UI.
        for(auto &action:w.actions) {
            if(action.key=='S')action.label=japanese?reinterpret_cast<const char*>(u8"キャラクター作成を最初からやり直す"):"Restart character creation from the beginning";
            else if(action.key=='Q')action.label=japanese?reinterpret_cast<const char*>(u8"ゲームを終了する"):"Exit the game";
            else if(action.key=='?')action.label=japanese?reinterpret_cast<const char*>(u8"この画面のヘルプ"):"Help for this screen";
            else if(action.key=='=')action.label=japanese?reinterpret_cast<const char*>(u8"初期オプションを設定する"):"Set initial options";
        }
    }
    if(w.title=="Character"&&japanese)w.title=reinterpret_cast<const char*>(u8"キャラクター作成");
    if(w.title=="Menu"&&japanese)w.title=reinterpret_cast<const char*>(u8"メニュー");
    const bool shift_help=original_body.find("SHIFT+")!=std::string::npos;
    const bool character_help=frame.pre_game_menu&&character_form
        &&(original_body.find("uppercase letter")!=std::string::npos||original_body.find(reinterpret_cast<const char*>(u8"コマンドの大文字"))!=std::string::npos);
    if(w.stage=="choices"&&(shift_help||character_help))for(auto &row:w.rows) {
        if(row.key<'a'||row.key>'z'||(character_help&&std::string("ngrcpm").find(char(row.key))==std::string::npos)||has_key(row.key-'a'+'A'))continue;
        row.actions.push_back({"help",japanese?reinterpret_cast<const char*>(u8"項目のヘルプ"):"Help for this item","",true,row.key-'a'+'A'});
    }
    if(choice_surface)w.content_type="choice_grid";
    modern_birth_choice_compact(frame,w);
    (void)interpret_modern_window_flags(frame,w,profile);
    modern_known_window_actions(frame,w,profile);
    if(w.stage=="text"||w.stage=="number"||w.stage=="confirm")w.actions.clear();
    modern_frox_cornucopia_controls(frame,w,original_body,profile);
    modern_frox_autopick_search_controls(frame,w,profile);
    (void)modern_browser_content(frame,w);
    if(!modern_rogue_settings(frame,w,profile))(void)modern_boolean_settings(frame,w);
    (void)modern_sil_settings(frame,w,original_body,profile);
    (void)modern_color_editor(frame,w,original_body,profile);
    (void)modern_visual_number_editor(frame,w,original_body);
    modern_editor_content(frame,w,original_body);
    modern_heng_minimum_stats(frame,w,original_body,profile);
    modern_monster_name_prompt(frame,w,original_body);
    modern_monster_query_result_controls(frame,w,original_body);
    modern_genso_bonus_allocation(frame,w,original_body,profile);
    modern_sil_allocation(frame,w,original_body,profile);
    modern_angband_point_based(frame,w,original_body);
    modern_frox_stats(frame,w,original_body,profile);
    modern_frox_birth_order(w,profile);
    modern_frox_birth_summary(frame,w,original_body,profile);
    modern_birth_start_menu(frame,w,original_body);
    modern_building_heading(frame,w,profile);
    if(w.title.starts_with("=== ")&&w.title.ends_with(" ==="))w.title=modern_trim(w.title.substr(4,w.title.size()-8));
    if(!document.active)w.content.erase(std::remove_if(w.content.begin(),w.content.end(),[&](const auto &block){return block.kind=="heading"&&block.cells.size()==1&&block.cells.front().text==w.title;}),w.content.end());
    modern_save_content(frame,w,original_body);
    (void)modern_character_sheet_content(frame,w);
    if(report_lines&&w.content.empty()){ModernBlock empty;empty.kind="paragraph";empty.cells.push_back({"",w.language=="ja"?reinterpret_cast<const char*>(u8"該当する項目はありません。"):"There are no matching entries.",false});w.content.push_back(std::move(empty));}
    if(document.active){
        if(!document_modal&&!document.caption.empty())w.title=document.caption;
        modern_document_controls(frame,document,w,profile);
        modern_frox_custom_document_controls(frame,w,profile);
        // Frox names the character sheet in the document caption. That title
        // is only available after document controls have normalized the page,
        // so classify it again here as semantic character information.
        (void)modern_character_sheet_content(frame,w);
    }
    (void)modern_scalar_setting(frame,w);
    modern_simple_form(frame,w,original_body,document.active);
    if(modern_poker_content(frame,w))return;
    (void)modern_map_content(frame,w);
    modern_frox_explored_map_controls(frame,w,profile);
    modern_frox_world_map_controls(frame,w,profile);
    (void)modern_sil_quick_help(frame,w,profile);
    if(profile=="sil"&&!save_labels) {
        const auto label=[](std::string value){
            const std::pair<const char8_t*,const char8_t*> labels[]={
                {u8"巨手を組む",u8"マクロの設定"},{u8"色を決める",u8"色の設定"},
                {u8"好みの綴りを読み込む",u8"設定ファイルを読み込む"},{u8"巨手を綴りへ書き足す",u8"マクロを設定ファイルに書き出す"},
                {u8"巨手を調べる",u8"マクロを確認する"},{u8"巨手を作る",u8"マクロを作成する"},{u8"巨手を消す",u8"マクロを削除する"},
                {u8"鍵の割り当てを綴りへ書き足す",u8"キーの割り当てを設定ファイルに書き出す"},{u8"鍵の割り当てを調べる",u8"キーの割り当てを確認する"},
                {u8"鍵の割り当てを作る",u8"キーの割り当てを作成する"},{u8"鍵の割り当てを消す",u8"キーの割り当てを削除する"},
                {u8"新しい動きを打ち込む",u8"新しい動作を入力する"},{u8"色を書き出す",u8"色の設定を書き出す"},{u8"色を変える",u8"色を変更する"},
                {u8"命綴り",u8"設定ファイル"},{u8"綴り",u8"ファイル名"},{u8"綴りの名",u8"ファイル名"},
                {u8"n 覚書",u8"冒険の記録を見る"},{u8"c 名を変える",u8"名前を変更する"},{u8"s 綴りに保存",u8"ファイルに保存する"},
                {u8"a 能力取得",u8"能力を取得する"},{u8"i 技能を伸ばす",u8"技能を上げる"}
            };
            for(const auto &[old,now]:labels)if(value==reinterpret_cast<const char*>(old))return std::string(reinterpret_cast<const char*>(now));return value;
        };
        w.title=label(w.title);w.subtitle=label(w.subtitle);w.input_label=label(w.input_label);
        for(auto &row:w.rows){row.label=label(row.label);if(smithing&&row.label=="Arrows")row.label=reinterpret_cast<const char*>(u8"矢");}
    }
    for(auto &row:w.rows)for(auto &a:row.actions)if(a.id=="choice")a.label=row.label;
    (void)modern_message_history(frame,w,profile);
    w.body.clear();
    for(const auto &block:w.content) {
        if(!block.title.empty())w.body+=block.title+"\n";
        for(const auto &cell:block.cells){if(!cell.label.empty())w.body+=cell.label+": ";w.body+=cell.text;w.body+='\n';}
        w.body+='\n';
    }
    // Source-coordinate fields are obsolete in the semantic document.
    w.cursor_span_begin=-1;w.cursor_span_len=0;
}
}
