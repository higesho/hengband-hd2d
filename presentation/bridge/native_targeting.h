#pragma once
#include "frame/targeting.h"
#include "frame/protocol_messages.h"
#include <algorithm>
#include <charconv>
#include <deque>
#include <limits>
#include <mutex>

namespace presentation {
// The wake byte is consumed only by the core's targeting input loop. It is
// never an in-game command and is not sent by the UI as a raw key sequence.
constexpr int native_targeting_wake=29;
struct NativeTargetingInput {
    // Recall ("detail"): mode "look" only (design doc section 8.5.1). Does not
    // move the cursor or end the session -- the core answers in frame.messages
    // and the look screen stays open.
    // Extra: one of TargetingState::extras (value = its key). Angband only ('<' '>' 'x' 'p' 'm' 'g' 'k').
    enum Kind { Select, Move, Cycle, Confirm, Cancel, Recall, Extra } kind{Select};
    TargetingPoint point;
    int value{},request_id{};
};
/*!
 * AH-27（Angband の課題管理表。ただしこのクラス自身は
 * 5 コア共用）。ここまでは`enabled_`/`state_`/`pending_`/`consumed_` 等の内部状態に
 * 何の排他制御も無かった——`input()` は「呼び手が自分の入力ロックの下で呼ぶ」
 * 前提（元のコメント「Called by the core's existing input dispatcher, under
 * its input lock.」）だったが、これは**受信スレッド側の規律**でしかなく、
 * `begin()`/`take()`/`publish()`/`end()`/`enabled()`/`active()`/`state()` を
 * 呼ぶ**ゲームスレッド側**（`hb_target_choose()`。`native_targeting_runtime.cpp`。
 * 原作の `get_aim_dir` フックの中からネストして動く、受信スレッドとは別の
 * 実行文脈）は元から何もロックしていなかった。5 コアとも `receive_loop()` を
 * 別スレッドで起こす同じ作り（`std::thread(receive_loop).detach()`）なので、
 * この欠落は Angband だけでなく共用コード側の穴だった——1 か所（このクラス）
 * に内部の `std::mutex` を足して直すのが、5 個のアダプタへ同じロックの規律を
 * 別々に書き足すより確実（書き忘れが起きない）。
 *
 * 全部の公開関数を同じ `mutex_` で包む。`state()` は参照ではなく値で返す
 * ——参照のままだと、呼び手が中身を読んでいる間にロックが外れて別の
 * スレッドが書き換えられる（コピー自体は保護されても、コピーした先の
 * 読み出しは保護できない）。`TargetingState` はハンドルを持たない普通の
 * 値型（`vector`/`string` のみ）なのでコピーの費用は軽い。
 *
 * 単一スレッドしか使わないコード（`--selftest` 等）には副作用が無い
 * （`std::mutex` は未競合なら実質タダ）。
 */
class NativeTargeting {
public:
    NativeTargeting()=default;
    // `mutex_` makes the copy/move members implicitly deleted, which some
    // compilers (TangbandCore's /WX) turn into a hard error over just the
    // *warning* about that (C4625/C5026/...). The single instance
    // (`native_targeting()`) is a process-lifetime singleton that is never
    // copied or moved, so delete these explicitly instead of relying on the
    // implicit deletion.
    NativeTargeting(const NativeTargeting &)=delete;
    NativeTargeting &operator=(const NativeTargeting &)=delete;
    NativeTargeting(NativeTargeting &&)=delete;
    NativeTargeting &operator=(NativeTargeting &&)=delete;
    void set_enabled(bool enabled){
        std::lock_guard<std::mutex> lock(mutex_);
        if(enabled_!=enabled)disabled_wake_sent_=false;enabled_=enabled;
    }
    bool take_disabled_wake(){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!state_.active||enabled_||disabled_wake_sent_)return false;disabled_wake_sent_=true;return true;
    }
    bool enabled()const{std::lock_guard<std::mutex> lock(mutex_);return enabled_;}
    bool active()const{std::lock_guard<std::mutex> lock(mutex_);return state_.active;}
    TargetingState state()const{std::lock_guard<std::mutex> lock(mutex_);return state_;}
    void begin(TargetingState state) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();consumed_=false;
        state.active=true;state.scope=++scope_;state.revision=++revision_;
        state.request_id=last_request_;state_=std::move(state);
    }
    void publish(TargetingState state,bool selection_changed) {
        std::lock_guard<std::mutex> lock(mutex_);
        consumed_=false;
        state.active=true;state.scope=state_.scope;state.revision=++revision_;
        state.request_id=last_request_;state.result=state_.result;
        state.selection_sequence=state_.selection_sequence+(selection_changed?1:0);
        state_=std::move(state);
    }
    void end(){std::lock_guard<std::mutex> lock(mutex_);state_={};pending_.clear();consumed_=true;}
    bool take(NativeTargetingInput &out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(pending_.empty())return false;out=pending_.front();pending_.pop_front();return true;
    }
    // Called by the core's existing input dispatcher, under its input lock
    // (that outer lock still matters -- it keeps "translate a key" and
    // "enqueue it" atomic on the receive side -- but it no longer has to be
    // the *same* lock as the one protecting this object's own fields; see
    // the AH-27 note above `mutex_`).
    // Prefix recognition also consumes late packets after the request ended.
    bool input(const InputEventWire &e,std::vector<int> &keys) {
        if(e.e!="ui_action"||!e.action.starts_with("aim_"))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!state_.active||e.request_id<=last_request_)return true;
        last_request_=e.request_id;state_.request_id=last_request_;state_.result="rejected";
        if(consumed_)return true;
        if(e.ui_revision!=state_.revision||!pending_.empty()){keys.push_back(native_targeting_wake);return true;}
        NativeTargetingInput input;input.request_id=e.request_id;
        if(e.action=="aim_select") {
            const auto comma=e.target.find(',');if(comma==std::string::npos)return wake(keys);
            const auto parse=[](std::string_view s,int &out){const auto r=std::from_chars(s.data(),s.data()+s.size(),out);return r.ec==std::errc()&&r.ptr==s.data()+s.size();};
            if(!parse(std::string_view(e.target).substr(0,comma),input.point.x)||!parse(std::string_view(e.target).substr(comma+1),input.point.y))return wake(keys);
            if(input.point.x<0||input.point.y<0||input.point.x>=state_.width||input.point.y>=state_.height||state_.mode=="fixed")return wake(keys);
            input.kind=NativeTargetingInput::Select;
        } else if(e.action=="aim_move") {
            if(e.number<1||e.number>9||e.number==5||state_.mode=="fixed")return wake(keys);
            input.kind=NativeTargetingInput::Move;input.value=int(e.number);
        } else if(e.action=="aim_cycle") {
            if((e.number!=0&&e.number!=1)||state_.candidates.empty()||state_.mode=="fixed")return wake(keys);
            input.kind=NativeTargetingInput::Cycle;input.value=int(e.number);
        } else if(e.action=="aim_confirm") {
            if(!state_.can_confirm)return wake(keys);
            input.kind=NativeTargetingInput::Confirm;consumed_=true;
        } else if(e.action=="aim_cancel") {
            input.kind=NativeTargetingInput::Cancel;consumed_=true;
        } else if(e.action=="aim_recall") {
            if(state_.mode!="look")return wake(keys);
            input.kind=NativeTargetingInput::Recall;
        } else if(e.action=="aim_extra") {
            // The key must be one the core offered (and enabled) in the frame the UI acted on.
            const auto offered=std::find_if(state_.extras.begin(),state_.extras.end(),[&](const auto &x){return x.key==e.number&&x.enabled;});
            if(offered==state_.extras.end())return wake(keys);
            input.kind=NativeTargetingInput::Extra;input.value=int(e.number);
        } else return wake(keys);
        pending_.push_back(input);state_.result="accepted";return wake(keys);
    }
private:
    static bool wake(std::vector<int> &keys){keys.push_back(native_targeting_wake);return true;}
    mutable std::mutex mutex_;
    bool enabled_{},consumed_{},disabled_wake_sent_{};
    int scope_{},revision_{4000000},last_request_{};
    TargetingState state_;
    std::deque<NativeTargetingInput> pending_;
};
}
