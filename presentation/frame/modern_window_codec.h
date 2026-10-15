#pragma once
#include "frame/modern_window.h"
#include <nlohmann/json.hpp>
#include <set>
inline nlohmann::json encode_modern_window(const ModernWindow &w) {
    nlohmann::json j{{"version",w.version},{"revision",w.revision},{"request_id",w.request_id},{"result",w.result},
        {"stage",w.stage},{"title",w.title},{"previous",w.previous},{"next",w.next},
        {"kind",w.kind},{"body",w.body},{"page",w.page},{"page_size",w.page_size},{"cursor",w.cursor},{"can_toggle",w.can_toggle},{"input",w.input},{"prev_key",w.prev_key},{"next_key",w.next_key},{"continue_key",w.continue_key}};
    j["subtitle"]=w.subtitle;j["cursor_target"]=w.cursor_target;j["language"]=w.language;j["content_type"]=w.content_type;j["input_label"]=w.input_label;j["input_caret"]=w.input_caret;
    j["can_preview"]=w.can_preview;j["preview_target"]=w.preview_target;
    if(!w.auto_select.empty())j["auto_select"]=w.auto_select;
    j["default_answer"]=w.default_answer;
    if(!w.venue_header.empty())j["venue_header"]=w.venue_header;
    j["text_edit_mode"]=w.text_edit_mode;j["text_fullwidth_kana"]=w.text_fullwidth_kana;
    j["input_ready"]=w.input_ready;j["message_only"]=w.message_only;j["text_ascii_only"]=w.text_ascii_only;
    j["content"]=nlohmann::json::array();
    for(const auto &block:w.content) {
        nlohmann::json cells=nlohmann::json::array();
        for(const auto &cell:block.cells)cells.push_back({{"label",cell.label},{"text",cell.text},{"selected",cell.selected},{"target",cell.target},{"color",cell.color}});
        j["content"].push_back({{"kind",block.kind},{"title",block.title},{"cells",cells}});
    }
    j["actions"]=nlohmann::json::array();
    for(const auto &a:w.actions)j["actions"].push_back({{"id",a.id},{"label",a.label},{"reason",a.reason},{"enabled",a.enabled},{"key",a.key}});
    j["rows"]=nlohmann::json::array();
    for(const auto &r:w.rows) {
        nlohmann::json a=nlohmann::json::array();
        for(const auto &v:r.actions)a.push_back({{"id",v.id},{"label",v.label},{"enabled",v.enabled},{"reason",v.reason},{"key",v.key}});
        j["rows"].push_back({{"id",r.id},{"label",r.label},{"weight",r.weight},{"count",r.count},{"price",r.price},{"key",r.key},
            {"tab",r.tab},{"slot",r.slot},{"tile",r.tile},{"selection",r.selection},{"available",r.available},
            {"price_known",r.price_known},{"equipped",r.equipped},{"reason",r.reason},{"description",r.description},{"effects",r.effects},{"actions",a},{"hotkey",r.hotkey},{"glyph",r.glyph},{"glyph_color",r.glyph_color}});
    }
    if(w.stage=="cursor" && w.cursor_span_begin>=0 && w.cursor_span_len>0) {j["cursor_span_begin"]=w.cursor_span_begin;j["cursor_span_len"]=w.cursor_span_len;}
    if(w.capture_keys)j["capture_keys"]=true;
    if(w.no_cancel)j["no_cancel"]=true;
    if(!w.initial_target.empty())j["initial_target"]=w.initial_target;
    if(w.text_confirm_action)j["text_confirm_action"]=true;
    return j;
}
inline bool decode_modern_window(const nlohmann::json &j,ModernWindow &out) {
    try {
        if(!j.is_object() || !j.at("version").is_number_integer() || (j.at("version")!=1 && j.at("version")!=2 && j.at("version")!=3 && j.at("version")!=4 && j.at("version")!=5)
            || !j.at("rows").is_array() || j.at("rows").size()>4096)return false;
        if(j.at("version")==5&&(!j.contains("content")||!j.contains("content_type")||!j.contains("actions")||!j.contains("language")||!j.contains("input_ready")||!j.contains("cursor_target")))return false;
        const auto str=[](const nlohmann::json &v,size_t limit=4096) {if(!v.is_string() || v.get_ref<const std::string&>().size()>limit)throw std::runtime_error("string");return v.get<std::string>();};
        const auto num=[](const nlohmann::json &v) {if(!v.is_number_integer() || v.get<long long>()<0 || v.get<long long>()>2000000000LL)throw std::runtime_error("integer");return v.get<int>();};
        ModernWindow w;w.version=num(j.at("version"));w.revision=num(j.at("revision"));w.request_id=num(j.at("request_id"));
        if(j.contains("venue_header"))w.venue_header=str(j["venue_header"],4096);
        w.result=str(j.at("result"));w.stage=str(j.at("stage"));w.title=str(j.at("title"));w.previous=j.at("previous").get<bool>();w.next=j.at("next").get<bool>();
        const std::set<std::string> stages{"","browse","choose","own_choose","number","confirm","detail","services","text","choices","cursor","finish","message"};
        if(!stages.count(w.stage))return false;
        if(w.version>=2) {
            w.kind=str(j.at("kind"));w.body=str(j.at("body"),32768);w.page=num(j.at("page"));w.page_size=num(j.at("page_size"));
            w.cursor=j.at("cursor").get<int>();w.can_toggle=j.at("can_toggle").get<bool>();
            if((w.kind!="store"&&w.kind!="building"&&w.kind!="home"&&w.kind!="museum"&&w.kind!="dialog"&&w.kind!="inventory") || w.page_size<1 || w.page_size>128 || w.cursor < -1 || w.cursor>4096)return false;
        }
        if(w.version>=3) {
            w.input=str(j.at("input"),32768);w.prev_key=num(j.at("prev_key"));w.next_key=num(j.at("next_key"));w.continue_key=num(j.at("continue_key"));
            if(w.prev_key>255||w.next_key>255||w.continue_key>255)return false;
        }
        if(j.contains("capture_keys")) {if(!j["capture_keys"].is_boolean())return false;w.capture_keys=j["capture_keys"].get<bool>();}
        if(j.contains("no_cancel")) {if(!j["no_cancel"].is_boolean())return false;w.no_cancel=j["no_cancel"].get<bool>();}
        if(j.contains("initial_target")) {if(!j["initial_target"].is_string())return false;w.initial_target=str(j["initial_target"]);}
        if(j.contains("text_confirm_action")) {if(!j["text_confirm_action"].is_boolean())return false;w.text_confirm_action=j["text_confirm_action"].get<bool>();}
        if(j.contains("cursor_span_begin") || j.contains("cursor_span_len")) {
            if(w.stage!="cursor" || w.cursor<0 || !j.contains("cursor_span_begin") || !j.contains("cursor_span_len"))return false;
            w.cursor_span_begin=num(j["cursor_span_begin"]);w.cursor_span_len=num(j["cursor_span_len"]);
            size_t row=0;for(int i=0;i<w.cursor;++i){const auto end=w.body.find('\n',row);if(end==std::string::npos)return false;row=end+1;}
            const auto end=w.body.find('\n',row);const size_t length=(end==std::string::npos?w.body.size():end)-row;
            if(w.cursor_span_len<=0 || size_t(w.cursor_span_begin)>length || size_t(w.cursor_span_len)>length-size_t(w.cursor_span_begin))return false;
            const auto boundary=[&](size_t at){return at==length || (static_cast<unsigned char>(w.body[row+at])&0xc0)!=0x80;};
            if(!boundary(w.cursor_span_begin)||!boundary(size_t(w.cursor_span_begin)+w.cursor_span_len))return false;
        }
        if(j.contains("content")) {
            if(!j["content"].is_array()||j["content"].size()>1024)return false;
            if(j.contains("subtitle"))w.subtitle=str(j["subtitle"]);
            if(j.contains("cursor_target"))w.cursor_target=str(j["cursor_target"]);
            if(j.contains("can_preview"))w.can_preview=j["can_preview"].get<bool>();
            if(j.contains("preview_target"))w.preview_target=str(j["preview_target"]);
            if(j.contains("auto_select"))w.auto_select=str(j["auto_select"]);
            if(j.contains("language")){w.language=str(j["language"],8);if(w.language!="ja"&&w.language!="en")return false;}
            w.content_type=str(j.at("content_type"),64);w.input_label=str(j.at("input_label"));
            if(!j.at("input_caret").is_number_integer())return false;
            w.input_caret=j.at("input_caret").get<int>();
            if(w.input_caret < -1 || w.input_caret>int(w.input.size()))return false;
            if(w.input_caret>=0&&w.input_caret<int(w.input.size())&&(static_cast<unsigned char>(w.input[w.input_caret])&0xc0)==0x80)return false;
            if(j.contains("default_answer")){if(!j["default_answer"].is_number_integer())return false;w.default_answer=j["default_answer"].get<int>();if(w.default_answer < -1||w.default_answer>=32)return false;}
            if(j.contains("text_fullwidth_kana"))w.text_fullwidth_kana=j["text_fullwidth_kana"].get<bool>();
            if(j.contains("text_edit_mode")){w.text_edit_mode=j["text_edit_mode"].get<std::string>();if(w.text_edit_mode!="full"&&w.text_edit_mode!="basic"&&w.text_edit_mode!="grid"&&w.text_edit_mode!="editor"&&w.text_edit_mode!="append")return false;}
            if(j.contains("input_ready"))w.input_ready=j["input_ready"].get<bool>();
            w.message_only=j.at("message_only").get<bool>();w.text_ascii_only=j.at("text_ascii_only").get<bool>();
            size_t bytes=0,cells=0;
            const std::set<std::string> kinds{"paragraph","heading","fields","grid","table_header","table_row","stat_values","links","code","swatch","group_list","cards"};
            for(const auto &b:j["content"]) {
                ModernBlock block;block.kind=str(b.at("kind"),32);if(b.contains("title"))block.title=str(b["title"]);
                if(!kinds.count(block.kind)||!b.at("cells").is_array()||b.at("cells").size()>128)return false;
                for(const auto &c:b.at("cells")) {
                    ModernCell cell;cell.label=str(c.at("label"));cell.text=str(c.at("text"),32768);cell.selected=c.at("selected").get<bool>();if(c.contains("target"))cell.target=str(c["target"]);
                    if(c.contains("color")){cell.color=str(c["color"],7);if(!cell.color.empty()&&(cell.color.size()!=7||cell.color[0]!='#'||cell.color.substr(1).find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos))return false;}
                    bytes+=cell.label.size()+cell.text.size()+cell.color.size();if(bytes>131072||++cells>8192)return false;
                    block.cells.push_back(std::move(cell));
                }
                w.content.push_back(std::move(block));
            }
        }
        if(j.contains("actions")) {
            if(!j["actions"].is_array()||j["actions"].size()>64)return false;
            std::set<std::string> commands;
            for(const auto &a:j["actions"]) {
                ModernAction action{str(a.at("id")),str(a.at("label")),str(a.at("reason")),a.at("enabled").get<bool>(),num(a.at("key"))};
                if(action.id.empty()||!commands.insert(action.id).second||action.key>127)return false;w.actions.push_back(std::move(action));
            }
        }
        std::set<std::string> ids;
        for(const auto &v:j.at("rows")) {
            ModernRow r;r.id=str(v.at("id"));r.label=str(v.at("label"));r.weight=str(v.at("weight"));r.count=num(v.at("count"));r.price=num(v.at("price"));r.key=num(v.at("key"));
            if(r.id.empty() || !ids.insert(r.id).second || r.key>255)return false;
            if(w.version>=2) {
                r.tab=num(v.at("tab"));r.slot=num(v.at("slot"));r.tile=num(v.at("tile"));r.selection=v.at("selection").get<int>();
                r.available=v.at("available").get<bool>();r.price_known=v.at("price_known").get<bool>();r.equipped=v.at("equipped").get<bool>();
                r.reason=str(v.at("reason"));r.description=str(v.at("description"),32768);
                if(r.tab>1 || r.tile>65535 || r.selection < -1 || r.selection>4096 || !v.at("effects").is_array() || v.at("effects").size()>256 || !v.at("actions").is_array() || v.at("actions").size()>32)return false;
                for(const auto &effect:v.at("effects"))r.effects.push_back(str(effect));
                for(const auto &a:v.at("actions"))r.actions.push_back({str(a.at("id")),str(a.at("label")),str(a.at("reason")),a.at("enabled").get<bool>()});
            }
            r.hotkey=r.key;
            if(w.version>=4) {
                r.hotkey=num(v.at("hotkey"));r.glyph=str(v.at("glyph"));r.glyph_color=num(v.at("glyph_color"));
                if(r.hotkey>127||r.glyph.size()>8||r.glyph_color>15)return false;
                for(size_t i=0;i<r.actions.size();++i){r.actions[i].key=num(v.at("actions")[i].at("key"));if(r.actions[i].key>127)return false;}
            }
            w.rows.push_back(std::move(r));
        }
        if(!w.cursor_target.empty()&&!ids.count(w.cursor_target))return false;
        if(!w.initial_target.empty()&&!ids.count(w.initial_target))w.initial_target.clear(); // only a hint: an unknown row is dropped, not rejected
        if(!w.preview_target.empty()&&!ids.count(w.preview_target))return false;
        bool auto_answer=false;
        if(w.stage=="confirm"&&w.auto_select.starts_with("@answer:")) {
            const auto value=w.auto_select.substr(8);
            auto_answer=!value.empty()&&value.size()<=2
                &&std::all_of(value.begin(),value.end(),[](unsigned char c){return c>='0'&&c<='9';})
                &&std::stoi(value)<32;
        }
        if(!w.auto_select.empty()&&!auto_answer&&!ids.count(w.auto_select)&&std::none_of(w.actions.begin(),w.actions.end(),[&](const auto &a){return a.id==w.auto_select&&a.enabled;}))return false;
        for(const auto &block:w.content)for(const auto &cell:block.cells)if(!cell.target.empty()&&!ids.count(cell.target)&&std::none_of(w.actions.begin(),w.actions.end(),[&](const auto &a){return a.id==cell.target;}))return false;
        out=std::move(w);return true;
    }catch(const std::exception&){return false;}
}
