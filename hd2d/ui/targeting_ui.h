#pragma once
#include "frame/targeting.h"
#include "frame/protocol_messages.h"
#include "ui/ui_layout.h"
#include "render/targeting_visual.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

namespace hd2d {
class UiPaint;
class TextOverlay;
struct TargetingButton {RectPx rect;std::string action;int value{};bool enabled{};};
class TargetingUi {
public:
    bool active()const{return state_.active;}
    bool visible()const{return state_.active||state_.preview_only;}
    bool pending()const{return pending_;}
    const TargetingState &state()const{return state_;}
    const std::vector<TargetingButton> &buttons()const{return buttons_;}
    void set_font_level(int level){font_level_=std::clamp(level,0,4);}
    void sync(const TargetingState &state) {
        const bool new_scope=state.active&&(!state_.active||state.scope!=state_.scope);
        camera_dirty_|=new_scope||(state.active&&(state.selected!=state_.selected||state.selection_sequence!=state_.selection_sequence));
        state_=state;
        if(new_scope){queue_.clear();pending_=false;notice_.clear();serial_=std::max(serial_,state.request_id);extras_open_=false;}
        if(state.extras.empty())extras_open_=false;
        if(!state.active){queue_.clear();pending_=false;notice_.clear();camera_dirty_=false;buttons_.clear();extras_open_=false;return;}
        if(pending_&&state.request_id==serial_) {
            pending_=false;
            if(state.result=="rejected"){queue_.clear();notice_=state.reason;}
        }
    }
    void advance(presentation::InputEventsMessage &out) {
        if(!active()||pending_)return;
        while(!queue_.empty()) {
            auto intent=std::move(queue_.front());queue_.pop_front();
            if(intent.action=="aim_confirm"&&!state_.can_confirm){notice_=state_.reason;continue;}
            if(state_.mode=="fixed"&&intent.action!="aim_confirm"&&intent.action!="aim_cancel")continue;
            presentation::InputEventWire e;e.e="ui_action";e.action=intent.action;e.target=intent.target;e.number=intent.value;
            e.ui_revision=state_.revision;e.request_id=++serial_;out.events.push_back(e);
            pending_=true;sent_action_=intent.action;notice_.clear();return;
        }
    }
    void select(TargetingPoint point,presentation::InputEventsMessage &out) {
        if(!active()||state_.mode=="fixed"||point.x<0||point.y<0||point.x>=state_.width||point.y>=state_.height)return;
        if(!queue_.empty()&&queue_.back().action=="aim_select")queue_.pop_back();
        enqueue({"aim_select",std::to_string(point.x)+","+std::to_string(point.y),0},out);
    }
    void move(int direction,presentation::InputEventsMessage &out) {
        if(direction<1||direction>9||direction==5)return;
        if(extras_open_){if(direction==8||direction==2)step_extras(direction==8?-1:1);return;}
        enqueue({"aim_move","",direction},out);
    }
    // The extra operations the core offers (Angband '<' '>' 'x' 'p' 'm' 'g' 'k'): a list opened with
    // Start / F4 / the "Actions" button, chosen with the cursor and the decision key (AH-54).
    bool extras_open()const{return extras_open_;}
    bool has_extras()const{return active()&&!state_.extras.empty();}
    void toggle_extras() {
        if(!has_extras()||pending_)return;
        extras_open_=!extras_open_;extras_cursor_=std::clamp(extras_cursor_,0,int(state_.extras.size())-1);
    }
    void step_extras(int step) {
        if(!extras_open_||state_.extras.empty())return;
        const int count=int(state_.extras.size());extras_cursor_=((extras_cursor_+step)%count+count)%count;
    }
    // A key typed straight in (the original key of the operation). False when it is not one of the extras.
    bool extra_key(int key,presentation::InputEventsMessage &out) {
        if(!active())return false;
        const auto found=std::find_if(state_.extras.begin(),state_.extras.end(),[&](const auto &e){return e.key==key;});
        if(found==state_.extras.end())return false;
        if(!found->enabled){notice_=found->label;return true;}
        extras_open_=false;enqueue({"aim_extra","",key},out);return true;
    }
    void decide_extra(presentation::InputEventsMessage &out) {
        if(!extras_open_||extras_cursor_<0||extras_cursor_>=int(state_.extras.size()))return;
        const auto &e=state_.extras[extras_cursor_];
        if(!e.enabled)return;
        extra_key(e.key,out);
    }
    void cycle(int direction,presentation::InputEventsMessage &out) {
        if(state_.candidates.empty())return;enqueue({"aim_cycle","",direction<0?0:1},out);
    }
    void confirm(presentation::InputEventsMessage &out) {
        // Confirmation must use an acknowledged preview, not a queued cursor
        // position. A repeat key while waiting cannot become a second attack.
        if(extras_open_){decide_extra(out);return;}
        if(pending_||!queue_.empty())return;
        // Mode "look" (AH-47): the decision key opens the detail of what the
        // cursor is on.  A look never places a target, so there is nothing to
        // confirm; the cancel key ends it.  (Only the Angband core uses "look".)
        if(state_.mode=="look"){if(!state_.target_name.empty())enqueue({"aim_recall","",0},out);return;}
        enqueue({"aim_confirm","",0},out);
    }
    // "detail" (design doc section 8.5.1): mode "look" only. Stays open --
    // the core answers in the message panel, not by switching screens.
    void recall(presentation::InputEventsMessage &out) {
        if(state_.mode!="look")return;enqueue({"aim_recall","",0},out);
    }
    void cancel(presentation::InputEventsMessage &out) {
        if(extras_open_){extras_open_=false;return;}
        if(pending_&&(sent_action_=="aim_confirm"||sent_action_=="aim_cancel"))return;
        queue_.clear();enqueue({"aim_cancel","",0},out);
    }
    bool take_camera_target(TargetingPoint &point,bool &creature) {
        if(!active()||!camera_dirty_)return false;camera_dirty_=false;
        if(state_.selected==state_.source)return false;
        point=state_.selected;creature=state_.selected_id!=0;return true;
    }
    void aim_camera(float eye_x,float eye_y,float eye_z,float dt,bool vertical,float &yaw,float &facing,float &pitch) {
        TargetingPoint point;bool creature=false;
        if(take_camera_target(point,creature)) {
            const float dx=point.x+.5f-eye_x,dy=point.y+.5f-eye_y;
            camera_yaw_=std::atan2(dx,-dy);
            camera_pitch_=std::clamp(std::atan2(eye_z-(creature?.55f:.05f),std::hypot(dx,dy))*57.2957795f,-60.f,60.f);
            camera_turning_=true;
        }
        if(!active()){camera_turning_=false;return;}
        if(!camera_turning_)return;
        const float amount=1.f-std::exp(-18.f*std::max(0.f,dt));
        const float difference=std::remainder(camera_yaw_-yaw,6.283185307f);
        yaw+=difference*amount;facing=yaw;
        if(vertical)pitch+=(camera_pitch_-pitch)*amount;
        if(std::abs(difference)<.002f&&(!vertical||std::abs(camera_pitch_-pitch)<.1f)) {
            yaw=camera_yaw_;facing=yaw;if(vertical)pitch=camera_pitch_;camera_turning_=false;
        }
    }
    bool click_toolbar(int x,int y,presentation::InputEventsMessage &out) {
        if(!active()||!toolbar_.contains(x,y))return false;
        for(const auto &b:buttons_)if(b.rect.contains(x,y)&&b.enabled) {
            if(b.action=="aim_confirm")confirm(out);else if(b.action=="aim_cancel")cancel(out);
            else if(b.action=="aim_recall")recall(out);
            else if(b.action=="aim_extras")toggle_extras();
            else if(b.action=="aim_extra")extra_key(b.value,out);
            else cycle(b.value?1:-1,out);
            break;
        }
        return true;
    }
    void draw(UiPaint &,TextOverlay &,const UiLayout &,int theme)const;
    void draw_map(UiPaint &,const TargetingMapProjection &,double time)const;
    TargetingMapProjection map_projection;
private:
    struct Intent {std::string action,target;int value;};
    void enqueue(Intent intent,presentation::InputEventsMessage &out) {
        if(!active()||queue_.size()>=64)return;
        if(pending_&&(sent_action_=="aim_confirm"||sent_action_=="aim_cancel"))return;
        queue_.push_back(std::move(intent));advance(out);
    }
    TargetingState state_;
    std::deque<Intent> queue_;
    bool pending_{},camera_dirty_{};
    bool camera_turning_{};
    float camera_yaw_{},camera_pitch_{};
    int serial_{},font_level_{},extras_cursor_{};
    bool extras_open_{};
    std::string sent_action_,notice_;
    mutable RectPx toolbar_;
    mutable std::vector<TargetingButton> buttons_;
};
}
