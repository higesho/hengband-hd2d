#pragma once
#include "frame/targeting.h"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

inline nlohmann::json encode_targeting(const TargetingState &s) {
    using J=nlohmann::json;
    const auto point=[](TargetingPoint p){return J::array({p.x,p.y});};
    J out{{"version",s.version},{"active",s.active},{"preview_only",s.preview_only},{"scope",s.scope},{"revision",s.revision},
        {"request_id",s.request_id},{"selection_sequence",s.selection_sequence},{"result",s.result},
        {"width",s.width},{"height",s.height},{"mode",s.mode},{"kind",s.kind},{"label",s.label},
        {"target_name",s.target_name},{"reason",s.reason},{"source",point(s.source)},
        {"selected",point(s.selected)},{"selected_id",s.selected_id},{"direction",s.direction},
        {"distance",s.distance},{"can_confirm",s.can_confirm},{"stochastic",s.stochastic},
        {"effect_known",s.effect_known}};
    out["candidates"]=J::array();out["paths"]=J::array();out["area"]=J::array();out["markers"]=J::array();
    // Always 6 elements now (design doc section 8.5.1: "add only"). kind/detail
    // default to "" for every core that never sets them, so the wire bytes for
    // an old candidate change only by two empty strings -- decode_targeting
    // below restores the exact legacy TargetingCandidate for those.
    for(const auto &c:s.candidates)out["candidates"].push_back(J::array({c.id,c.point.x,c.point.y,c.name,c.kind,c.detail}));
    for(const auto &p:s.paths){J cells=J::array();for(auto c:p.points)cells.push_back(point(c));out["paths"].push_back({{"possible",p.possible},{"guide",p.guide},{"points",std::move(cells)}});}
    for(const auto &c:s.area)out["area"].push_back(J::array({c.point.x,c.point.y,c.possible}));
    for(const auto &m:s.markers)out["markers"].push_back(J::array({m.id,m.point.x,m.point.y,m.creature,m.selected,m.affected,m.valid}));
    // Only when the core offers extras, so every other core's wire bytes stay as they were.
    if(!s.extras.empty()){out["extras"]=J::array();for(const auto &e:s.extras)out["extras"].push_back(J::array({e.key,e.label,e.enabled}));}
    // The cursor popup (TOband). One array per line, each span [colour, text]. Only when the core sends one.
    if(!s.info.empty()){
        out["info"]=J::array();
        for(const auto &line:s.info){J spans=J::array();for(const auto &sp:line.spans)spans.push_back(J::array({sp.color,sp.text}));out["info"].push_back(std::move(spans));}
    }
    return out;
}
inline bool decode_targeting(const nlohmann::json &j,TargetingState &out,std::string &error) {
    try {
        using J=nlohmann::json;
        const auto integer=[](const J &v,int min,int max){if(!v.is_number_integer())throw std::runtime_error("integer expected");const auto n=v.get<long long>();if(n<min||n>max)throw std::runtime_error("integer out of range");return int(n);};
        const auto boolean=[](const J &v){if(!v.is_boolean())throw std::runtime_error("boolean expected");return v.get<bool>();};
        const auto string=[](const J &v,size_t max){if(!v.is_string()||v.get_ref<const std::string&>().size()>max)throw std::runtime_error("invalid string");return v.get<std::string>();};
        const auto array=[](const J &v,size_t max){if(!v.is_array()||v.size()>max)throw std::runtime_error("invalid array");};
        TargetingState s;
        s.version=integer(j.at("version"),1,1);s.active=boolean(j.at("active"));
        if(j.contains("preview_only"))s.preview_only=boolean(j.at("preview_only"));
        if(s.active&&s.preview_only)throw std::runtime_error("preview cannot request targeting input");
        s.scope=integer(j.at("scope"),1,2000000000);s.revision=integer(j.at("revision"),1,2000000000);
        s.request_id=integer(j.at("request_id"),0,2000000000);s.selection_sequence=integer(j.at("selection_sequence"),0,2000000000);
        s.width=integer(j.at("width"),1,4096);s.height=integer(j.at("height"),1,4096);
        s.result=string(j.at("result"),16);if(s.result!=""&&s.result!="accepted"&&s.result!="rejected")throw std::runtime_error("invalid result");
        s.mode=string(j.at("mode"),16);if(s.mode!="point"&&s.mode!="direction8"&&s.mode!="creature"&&s.mode!="fixed"&&s.mode!="look")throw std::runtime_error("invalid targeting mode");
        s.kind=string(j.at("kind"),16);if(s.kind!="attack"&&s.kind!="support"&&s.kind!="neutral")throw std::runtime_error("invalid targeting kind");
        s.label=string(j.at("label"),1024);s.target_name=string(j.at("target_name"),1024);s.reason=string(j.at("reason"),4096);
        const auto point=[&](const J &v){array(v,2);if(v.size()!=2)throw std::runtime_error("invalid point");return TargetingPoint{integer(v[0],0,s.width-1),integer(v[1],0,s.height-1)};};
        // selected_id/marker id allow down to -2000000000, not just -1, since
        // mode "look" (section 8.5.1) hands out synthetic negative ids for
        // non-monster candidates (object/feature/trap) -- the old -1-only floor
        // matched only "id 0 free point, -1 player, positive monster" and threw
        // "integer out of range" on any of those (an_targeting.c's synthetic ids
        // start at -1001), which failed the whole frame decode silently and left
        // the client showing the previous frame forever.
        s.source=point(j.at("source"));s.selected=point(j.at("selected"));s.selected_id=integer(j.at("selected_id"),-2000000000,2000000000);
        s.direction=integer(j.at("direction"),0,9);s.distance=integer(j.at("distance"),0,8192);
        s.can_confirm=boolean(j.at("can_confirm"));s.stochastic=boolean(j.at("stochastic"));s.effect_known=boolean(j.at("effect_known"));
        array(j.at("candidates"),4096);std::set<int> ids;
        for(const auto &v:j["candidates"]){
            array(v,6);if(v.size()!=4&&v.size()!=6)throw std::runtime_error("invalid candidate");
            TargetingCandidate c;c.id=integer(v[0],-2000000000,2000000000);if(!c.id||!ids.insert(c.id).second)throw std::runtime_error("invalid candidate id");
            c.point=point(J::array({v[1],v[2]}));c.name=string(v[3],1024);
            if(v.size()==6){
                c.kind=string(v[4],16);
                if(!c.kind.empty()&&c.kind!="monster"&&c.kind!="object"&&c.kind!="feature"&&c.kind!="trap"&&c.kind!="player")throw std::runtime_error("invalid candidate kind");
                c.detail=string(v[5],2048);
            }
            s.candidates.push_back(std::move(c));
        }
        array(j.at("paths"),256);size_t cells=0;
        for(const auto &v:j["paths"]){TargetingPath p;p.possible=boolean(v.at("possible"));if(v.contains("guide"))p.guide=boolean(v.at("guide"));array(v.at("points"),4096);for(const auto &c:v["points"]){if(++cells>65536)throw std::runtime_error("too many path cells");p.points.push_back(point(c));}s.paths.push_back(std::move(p));}
        array(j.at("area"),65536);std::set<std::pair<int,int>> area;
        for(const auto &v:j["area"]){array(v,3);if(v.size()!=3)throw std::runtime_error("invalid area cell");TargetingAreaCell c{point(J::array({v[0],v[1]})),boolean(v[2])};if(!area.insert({c.point.x,c.point.y}).second)throw std::runtime_error("duplicate area cell");s.area.push_back(c);}
        array(j.at("markers"),4097);ids.clear();int selected=0;
        for(const auto &v:j["markers"]){array(v,7);if(v.size()!=7)throw std::runtime_error("invalid marker");TargetingMarker m;m.id=integer(v[0],-2000000000,2000000000);m.point=point(J::array({v[1],v[2]}));m.creature=boolean(v[3]);m.selected=boolean(v[4]);m.affected=boolean(v[5]);m.valid=boolean(v[6]);if(m.creature&&(!m.id||!ids.insert(m.id).second))throw std::runtime_error("invalid marker id");if(m.selected){++selected;if(m.point!=s.selected||m.id!=s.selected_id)throw std::runtime_error("selection mismatch");}s.markers.push_back(m);}
        // extras のキーは印字できる字（33〜126）か Ctrl＋英字（1〜26）。Angband のローグライクのキー配置の
        // 「無視する」は ^D（2026-10-07）。
        if(j.contains("extras")){
            array(j["extras"],32);
            for(const auto &v:j["extras"]){array(v,3);if(v.size()!=3)throw std::runtime_error("invalid extra");TargetingExtra e;e.key=integer(v[0],1,126);if(e.key>26&&e.key<33)throw std::runtime_error("invalid extra key");e.label=string(v[1],256);e.enabled=boolean(v[2]);s.extras.push_back(std::move(e));}
        }
        if(j.contains("info")){
            array(j["info"],8);
            for(const auto &line:j["info"]){
                array(line,16);TargetingInfoLine l;
                for(const auto &v:line){array(v,2);if(v.size()!=2)throw std::runtime_error("invalid info span");TargetingInfoSpan sp;sp.color=integer(v[0],0,15);sp.text=string(v[1],512);l.spans.push_back(std::move(sp));}
                s.info.push_back(std::move(l));
            }
        }
        if(s.active&&selected!=1)throw std::runtime_error("exactly one selected marker required");
        if(s.preview_only&&(selected||s.can_confirm))throw std::runtime_error("menu preview cannot select or confirm a target");
        out=std::move(s);return true;
    } catch(const std::exception &e){error=std::string("targeting: ")+e.what();return false;}
}
