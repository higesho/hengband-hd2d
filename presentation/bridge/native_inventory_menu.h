#pragma once
#include "bridge/native_modern.h"
#include <chrono>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace presentation {
// The inventory is a view of owned objects. Actions still enter the original
// command and its selector, so inscription checks, quantities and aim are native.
class NativeInventoryMenu {
public:
    void configure(bool enabled,std::string language,std::string profile) {
        enabled_=enabled;language_=std::move(language);profile_=std::move(profile);
        if(!enabled_){active_=root_=pending_=bare_more_=false;current_={};}
    }
    template<class Owned,class Commands>
    void capture(GameFrame &frame,Owned owned,Commands commands) {
        if(active_&&frame.modern.request_id>request_){request_=frame.modern.request_id;result_=frame.modern.result;}
        /*
         * Remember a proven top-level command boundary while the inventory is
         * closed.  Some cores emit a short owner-less refresh between that
         * boundary and the frontend's inventory_open event; clearing availability
         * on that transient frame makes a valid open request race and get rejected.
         *
         * Once the inventory is active/pending, however, use the strict current
         * frame only.  That prevents the root list from reopening between an item
         * command and its original child selector (equip/takeoff/etc.).
         */
        const bool child_owner=frame.menu_open||frame.pre_game_menu||frame.text_input_active
            ||frame.numeric.active||!frame.prompt.choices.empty()||!frame.modern.stage.empty();
        const bool command_boundary=enabled_&&frame.awaiting_command&&!child_owner;
        if(!enabled_) available_=false;
        else if(pending_) {
            /*
             * An item command has entered the original game.  Do not reopen the
             * root list until the real command boundary is observed again.
             *
             * `command_boundary` can still read true on the very first capture()
             * after the item's key was accepted: the key is queued for the game
             * thread, but that thread may not have dequeued and processed it yet
             * (it is polled from getch(), not delivered synchronously), so
             * `frame.awaiting_command` still reflects the *previous* top-level
             * prompt. Reopening the root list right then discards `pending_`
             * before the key was ever seen by the child prompt it was meant for
             * (e.g. wield's own letter prompt), silently swallowing the action.
             * Require one confirmed `command_boundary==false` observation first,
             * proving the game actually left that prior prompt, before a
             * following `true` is trusted as "the item command finished".
             * (Seen on Linux/WSL scheduling for rogue2's equipment_audit wield
             * step; Windows' tighter poll loop rarely exposed the gap.)
             */
            if(!command_boundary) pending_confirmed_=true;
            /*
             * 子の問いを出さずに終わる命令もある（Rogue 3.6/5.4 の `T` 鎧を脱ぐは
             * 対象を聞かない）。そのときは `command_boundary==false` の画が 1 枚も
             * 来ないので、上の条件だけでは永久に確かめられず、一覧は active_ のまま
             * 見えなくなり、以後の inventory_open も全部弾かれていた
             * （気づいたこと 2026-09-25「鎧を外して持ち物を開き直すと何も出ず反応しない」）。
             * 命令を送った時点から持ち物の中身か最新のメッセージが変わっていれば、
             * 原作がその命令を読んで実行し終えた証拠なので、それでも確かめたことにする。
             */
            else if(!pending_confirmed_&&pending_snapshot_!=snapshot(frame,owned()))pending_confirmed_=true;
            /*
             * 中身もメッセージも変えずに終わる命令もある（原作が黙って何もしない、
             * 同じ文言をもう一度出すだけ、など）。そのときは上の 2 つのどちらでも
             * 確かめられず、一覧が二度と開かなくなる。上の競合（キーがまだゲームの
             * スレッドに読まれていない）は 1 回のポーリングの間の話なので、キーを
             * 送ってから十分に時間が経ち、なおコマンド待ちに居るなら、原作はもう
             * キーを読んで命令を終えている。
             */
            else if(!pending_confirmed_&&command_boundary&&pending_elapsed())pending_confirmed_=true;
            available_=command_boundary&&pending_confirmed_;
        } else if(active_) {
            // While the root inventory itself owns input, an owner-less refresh
            // must not silently revoke that ownership; otherwise the visible
            // list can no longer accept cancel/selection events.
            if(command_boundary) available_=true;
            else if(child_owner) {
                available_=false;
                /*
                 * 一覧から命令を 1 つも送っていないのに子の問い（設定の一覧・文字入力など）が
                 * 出たのは、一覧の外（コマンドメニュー・キーボード）から別の命令が入ったとき。
                 * 一覧はもう持ち主ではないので閉じる。閉じないと、その問いの文字入力の窓が
                 * 下の「子の問いを一覧の飾りへ着替えさせる」処理で持ち物の一覧に化け、
                 * 一覧の上に入力欄が出ていた（Rogue の設定の
                 * 好きな果物）。
                 */
                if(!command_sent_){active_=false;current_={};}
            }
        } else if(command_boundary) available_=true;
        else if(child_owner) available_=false;
        last_command_boundary_=command_boundary;
        const bool was_bare_more=bare_more_;
        root_=false;bare_more_=false;
        if(!active_) {
            if(frame.modern.stage.empty()&&request_>frame.modern.request_id){frame.modern.version=5;frame.modern.revision=std::max(frame.modern.revision,revision_);frame.modern.request_id=request_;frame.modern.result=result_;}
            return;
        }
        if(available_) {
            pending_=false;command_sent_=false;navigation_steps_=0;root_=true;current_=ModernWindow{};
            auto &w=current_;w.version=5;w.kind="inventory";w.stage="browse";w.content_type="items";
            w.title=tr(u8"持ち物一覧","Inventory");w.language=language_;w.page_size=64;
            w.rows=owned();current_rows_=w.rows;current_message_=message_key(frame);
            const auto table=commands();command_keys_.clear();
            for(auto &row:w.rows) {
                std::vector<ModernAction> actions;
                for(const auto &candidate:row.actions) {
                    const auto found=std::find_if(table.entries.begin(),table.entries.end(),[&](const auto &c){return c.command==candidate.key;});
                    if(found==table.entries.end())continue;
                    const auto keys=table.current_keymap=="rogue"?found->seq_rogue:found->seq_original;
                    const auto id="inventory:"+std::to_string(candidate.key);
                    actions.push_back({id,found->label_utf8,keys.empty()?tr(u8"このキー配列では実行できません。","Unavailable in this keymap."):candidate.reason,
                        !keys.empty()&&candidate.enabled,keys.size()==1?keys.front():0});
                    command_keys_[id]=keys;
                }
                std::reverse(actions.begin(),actions.end());
                actions.push_back({"back",tr(u8"戻る","Back"),"",true,27});row.actions=std::move(actions);row.tab=0;
            }
            if(!frame.messages.empty())w.input=frame.messages.back().text_utf8;
            const auto inventory_signature=encode_modern_window(w).dump();
            if(inventory_signature!=signature_||consumed_){signature_=inventory_signature;++revision_;consumed_=false;}
            w.revision=revision_;w.request_id=request_;w.result=result_;
            frame.modern=w;frame.menu_open=true;return;
        }
        auto &w=frame.modern;
        if(pending_&&profile_=="angband"&&w.stage=="choices")angband_select(frame,w);
        else if(pending_&&w.content_type=="items"&&w.stage=="choices") {
            const auto wanted=std::find_if(w.rows.begin(),w.rows.end(),[&](const auto &r){return r.slot==slot_&&r.selection==location_&&r.label==label_&&r.count==count_;});
            if(wanted!=w.rows.end())w.auto_select=wanted->id;
            else if(navigation_steps_<32) {
                // These are native item-source/page controls, never a game action.
                const bool other_source=!w.rows.empty()&&w.rows.front().selection!=location_;
                navigation_key_=0;
                if(profile_=="frox")navigation_key_=other_source?(location_==1?5:(location_==3?17:9)):'3';
                else if(other_source)navigation_key_=w.cursor>=0?'6':'/';
                if(navigation_key_) {
                    w.actions.push_back({"inventory-source",tr(u8"対象の一覧へ移動","Find selected item"),"",true,navigation_key_});
                    w.auto_select="inventory-source";
                }
            } else pending_=false;
        }
        /*
         * AH-35 以来、遊んでいる最中の素の `-more-`
         * （実在するメニューを開いていないとき）は `native_modern.h::capture()` が
         * 全画面のダイアログへ昇格させなくなった——`w.stage` は空のままになる。
         * ここでアイテムの命令（読む・使う等）を送った直後にその `-more-` が来ると、
         * 以前は「昇格したダイアログの `message_only`」をこの一覧の飾りへ着替えさせて
         * いたが、その昇格自体が起きなくなったので `w.stage` が空のまま何もせず抜けて
         * いた——一覧が消え、命令を確認し終えた次のフレームで一覧が新規に開き直す
         * 「消えて開き直る」明滅になっていた（レビュー担当の指摘 2026-09-25）。
         * **一覧はすでに開いていた窓**なので、これは AH-35 が防ぎたい「開いていない所
         * からの全画面化」には当たらない——同じ一覧の飾りのまま、中身だけメッセージへ
         * 差し替える。
         */
        /*
         * Rogue 系（RogueClone2・3.6・5.4）の［続く］は行 0 のミラーにも帯にも載らず、
         * 最新のメッセージの末尾に付くだけ（`rogue2/adapter/r2_frame.cpp` の
         * `inline_continue`、`rogue_legacy/adapter/rl_frame.cpp` の `continuation_visible()`）。
         * そのコアではメッセージの印も見る。他のコアの履歴には印が残らないので広げない。
         */
        const bool rogue_more=profile_=="rogue"&&!frame.awaiting_command&&frame.prompt.text_utf8.empty()
            &&!frame.messages.empty()&&message_more_suffix(frame.messages.back().text_utf8);
        const bool bare_more_pending=w.stage.empty()&&(message_more_pending(frame)||rogue_more)
            &&!frame.numeric.active&&frame.prompt.choices.empty();
        if(w.stage=="number"||w.stage=="text"||w.stage=="confirm"||w.message_only||bare_more_pending) {
            // Keep the native revision and input contract of this child prompt.
            w.kind="inventory";w.title=tr(u8"持ち物一覧","Inventory");w.rows=owned();
            for(auto &row:w.rows)row.tab=0;
            w.content_type="items";w.venue_header.clear();
            if(w.message_only){w.stage="message";w.input=w.body;}
            else if(bare_more_pending) {
                w.stage="message";w.continue_key=' ';w.input=modern_message(frame);
                /*
                 * 変愚・短愚蛮怒では、メニューの外の `-続く-` に原作側の窓が 1 枚も無い
                 * （`frame.modern` は version 0 のまま）。版番号が 0 だと送られず、
                 * revision が 0 だと画面側の返事を照合できないので、この一覧の番号を使う。
                 * 同じ `-続く-` の間は同じ番号にして、送った continue が古くならないようにする。
                 */
                if(!w.version)w.version=5;
                if(!w.revision){if(!was_bare_more)++revision_;w.revision=revision_;}
            }
        }
        /*
         * 命令を送ってから原作がコマンド待ちへ戻るまでの間（子の問いも `-続く-` も無い画）は、
         * 原作の側に窓が無い（`w.stage` が空）。そのまま送ると画面側は一覧を閉じて地図に
         * 切り替え、コマンド待ちに戻った所でまた一覧を開く——「壊す」の後に一覧が一度消えて
         * 開き直して見えていた。原作が別の画を出していない間は、
         * 同じ一覧を**入力を受けない窓**（`input_ready=false`）として出し続ける。
         * 照準（`targeting.active`）・方向などの問い（`prompt`）・原作の Term の画
         * （`menu_term_lines`）が出ている間は出さない——マップや原作の画を見せる所である。
         * メッセージの欄だけで問うコア（Rogue 系の投げる「方向は？」など。帯も照準も無い）も、
         * 命令の後に「？」で終わる新しいメッセージが出たら問いとみなして出さない
         * （出すと問いが一覧の下に隠れ、答えられないまま止まる）。
         */
        const bool asks_in_message=!frame.messages.empty()&&message_key(frame)!=current_message_
            &&message_asks(frame.messages.back().text_utf8);
        const bool busy=w.stage.empty()&&!bare_more_pending&&!asks_in_message&&!frame.targeting.active&&!frame.menu_open
            &&!frame.numeric.active&&!frame.text_input_active&&frame.prompt.choices.empty()
            &&frame.prompt.text_utf8.empty()&&frame.menu_term_lines.empty()&&!frame.pre_game_menu;
        if(busy) {
            // stage は "message"（一覧の下に最新のメッセージを出す形）。"browse" にすると、一覧を
            // 開いたと読む側（画面・検査）が入力を受けない窓へ命令を送ってしまう。
            ModernWindow hold;hold.version=5;hold.kind="inventory";hold.stage="message";hold.content_type="items";
            hold.title=tr(u8"持ち物一覧","Inventory");hold.language=language_;hold.page_size=64;hold.input_ready=false;
            hold.rows=owned();for(auto &row:hold.rows){row.actions.clear();row.tab=0;}
            if(!frame.messages.empty())hold.input=frame.messages.back().text_utf8;
            hold.revision=revision_;hold.request_id=w.request_id;hold.result=w.result;
            w=std::move(hold);frame.menu_open=true;
        }
        bare_more_=bare_more_pending;
        /*
         * Rogue 系の［続く］が原作側の窓（`message_only`）として来た画でも、画面側の
         * continue / cancel はここで受ける。後ろ（NativeModernMenu）へ流すと cancel は
         * ESC になり、Rogue の［続く］は ESC を読み捨てるので画が 1 枚も来ないまま
         * 止まっていた（3.6 の `T` の直後に敵の攻撃が重なったとき。2026-09-26）。
         */
        rogue_message_more_=profile_=="rogue"&&w.message_only&&w.stage=="message";
        last_native_revision_=w.revision;
        if(request_>w.request_id){w.request_id=request_;w.result=result_;}
    }
    bool input(const InputEventWire &event,std::vector<int> &keys) {
        if(!enabled_||event.e!="ui_action")return false;
        if(event.action=="inventory_open") {
            if(event.request_id<=request_)return true;
            // 上の capture() の時間の条件は、画が作り直されないと評価されない。コマンド待ちの
            // まま止まっているコアでは画が来ないこともあるので、開く要求の側でも同じ条件を見る。
            if(pending_&&!pending_confirmed_&&last_command_boundary_&&pending_elapsed()){pending_confirmed_=true;available_=true;}
            request_=event.request_id;result_=available_?"accepted":"rejected";
            /*
             * `consumed_` を立てるのは、閉じていた一覧をここで新たに開くときだけでよい
             * （クライアントへ確実に新しい revision を届けるため）。すでに active_ な
             * 一覧へ重ねて inventory_open が届くのは、原作側の command_boundary で
             * 一覧が自動的に再オープンした直後にクライアントが同じ要求を送った場合
             * （`capture()` のコメント参照）。その自動再オープンはすでに正しい
             * revision で送信済みなので、ここでまた consumed_=true にすると、
             * 直後にクライアントが「たった今受け取った revision」で送る次の行動
             * （装備の持ち替えなど）が、まだ再構築（consumed_ リセット）前の
             * ゲームスレッドに追いつかれて revision 不一致で弾かれる競合になる
             * （Linux 版 rogue2/protocol_audit.py の equipment_audit が
             * wield で再現。Windows は間に合っていたため見えなかった）。
             */
            if(available_){if(!active_)consumed_=true;active_=true;}return true;
        }
        if(!active_)return false;
        /*
         * 上の capture() が素の `-more-` を一覧の飾りの "message" に着替えさせた画では、
         * 原作の側に新 UI の窓が無い（`frame.modern.stage` は空）。だから画面側の
         * continue / cancel を後ろ（`modern_store_input()` などコアごとの受け手）へ
         * 流すと「窓が無い」として弾かれ、`-more-` を送れないまま一覧も開けなくなる
         * （気づいたこと 2026-09-25「変愚でアイテムを破壊したあと持ち物が参照できなく
         * なってフリーズした」）。この窓を作ったのはここなので、ここで答える。
         */
        if((bare_more_||rogue_message_more_)&&(event.action=="continue"||event.action=="cancel")) {
            if(event.request_id<=request_)return true;
            request_=event.request_id;result_="rejected";
            if(event.ui_revision==last_native_revision_) {
                // Rogue の［続く］は Space しか受けない（ESC は読み捨てられて止まったままになる）。
                keys.push_back(event.action=="cancel"&&profile_!="rogue"?27:' ');result_="accepted";bare_more_=false;rogue_message_more_=false;
                if(event.action=="cancel")pending_=false;
            }
            return true;
        }
        if(event.action=="cancel")pending_=false;
        if(!root_) {
            if(pending_&&event.action=="window_action"&&event.target=="inventory-source") {
                if(event.request_id<=request_)return true;
                request_=event.request_id;result_="rejected";
                if(event.ui_revision==last_native_revision_&&navigation_key_&&navigation_steps_<32){keys.push_back(navigation_key_);++navigation_steps_;result_="accepted";navigation_key_=0;}
                return true;
            }
            if(event.action=="choice"&&event.ui_revision==last_native_revision_)pending_=false;
            return false;
        }
        if(event.request_id<=request_)return true;
        request_=event.request_id;result_="rejected";
        if(event.ui_revision!=revision_||consumed_)return true;
        if(event.action=="cancel"){active_=false;result_="accepted";return true;}
        const auto row=std::find_if(current_.rows.begin(),current_.rows.end(),[&](const auto &r){return r.id==event.target;});
        if(row==current_.rows.end())return true;
        const auto action=std::find_if(row->actions.begin(),row->actions.end(),[&](const auto &a){return a.id==event.action&&a.enabled;});
        const auto found=command_keys_.find(event.action);
        if(action==row->actions.end()||found==command_keys_.end()||found->second.empty())return true;
        slot_=row->slot;location_=row->selection;label_=row->label;count_=row->count;
        pending_=true;command_sent_=true;pending_confirmed_=false;navigation_steps_=0;consumed_=true;root_=false;pending_since_=std::chrono::steady_clock::now();
        pending_snapshot_=snapshot_rows(current_rows_)+'\x1f'+current_message_;
        keys.insert(keys.end(),found->second.begin(),found->second.end());result_="accepted";
        return true;
    }
private:
    /*
     * Angband の `item_menu()`（原作 ui-object.c）は画面の字から選択肢を作るので、行に
     * slot/selection/count が無い。そのままでは対象の行が見つからず、原作の選択肢が
     * そのまま出て遊ぶ人が選び直していた（2026-09-26）。原作の見出し
     * 「(Inven: …」「(Equip: …」「(Quiver: …」（日本語は「(持ち物: …」ほか）で今の一覧を知り、その一覧での文字
     * （持ち物と装備は `all_letters_nohjkl[番号]`、矢筒は数字）と名前の両方が合う行
     * だけを対象にする。装備と持ち物に同じ名前（松明など）があっても取り違えない。
     */
    void angband_select(const GameFrame &frame,ModernWindow &w) {
        static constexpr std::string_view letters="abcdefgimnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::string source;
        for(const auto &line:frame.menu_term_lines) {
            // 日本語の札は変愚の同じ問いの綴り（Angband の messages.ja.txt）。英語の綴りも今までどおり見る。
            static const std::pair<const char*,const char*> heads[]={{"(Inven:","Inven"},{"(Equip:","Equip"},{"(Quiver:","Quiver"},{"(Floor:","Floor"},
                {reinterpret_cast<const char*>(u8"(持ち物:"),"Inven"},{reinterpret_cast<const char*>(u8"(装備品:"),"Equip"},{reinterpret_cast<const char*>(u8"(矢筒:"),"Quiver"},{reinterpret_cast<const char*>(u8"(床上:"),"Floor"}};
            for(const auto &[head,name]:heads)
                if(line.text_utf8.find(head)!=std::string::npos){source=name;break;}
            if(!source.empty())break;
        }
        if(source.empty()){if(navigation_steps_>=32)pending_=false;return;}
        const bool quiver=location_==0&&slot_>=1000;
        const std::string wanted=location_==1?"Equip":(quiver?"Quiver":"Inven");
        int key=0;
        if(quiver){if(slot_-1000<10)key='0'+slot_-1000;}
        else if(slot_>=0&&slot_<int(letters.size()))key=letters[size_t(slot_)];
        if(!key){pending_=false;return;}
        if(source==wanted) {
            auto names=[&](const ModernRow &r){
                const auto at=r.label.find(label_);
                return !label_.empty()&&at!=std::string::npos&&(at+label_.size()==r.label.size()||r.label[at+label_.size()]==' ');
            };
            const auto found=std::find_if(w.rows.begin(),w.rows.end(),[&](const auto &r){return r.key==key&&names(r);});
            const bool unique=found!=w.rows.end()&&std::count_if(w.rows.begin(),w.rows.end(),[&](const auto &r){return r.key==key;})==1;
            if(unique)w.auto_select=found->id;else pending_=false;   // 見つからなければ原作の選択肢をそのまま見せる
            return;
        }
        if(navigation_steps_>=32){pending_=false;return;}
        const int step=quiver?'|':'/';
        if(std::none_of(w.actions.begin(),w.actions.end(),[&](const auto &a){return a.key==step&&a.enabled;})){pending_=false;return;}
        navigation_key_=step;
        w.actions.push_back({"inventory-source",tr(u8"対象の一覧へ移動","Find selected item"),"",true,navigation_key_});
        w.auto_select="inventory-source";
    }
    std::string tr(const char8_t *ja,const char *en)const{return language_=="ja"?reinterpret_cast<const char*>(ja):en;}
    static std::string snapshot_rows(const std::vector<ModernRow> &rows) {
        std::string s;
        for(const auto &r:rows){s+=r.label;s+='\x1e';s+=std::to_string(r.count);s+=r.equipped?'E':'-';}
        return s;
    }
    // 最新のメッセージは本文だけでなく通し番号（`MessageEvent::seq`）も見る。同じ文言を
    // もう一度出しただけの命令（例: 満腹なのにまた食べようとした）でも変化として拾える。
    static std::string message_key(const GameFrame &frame) {
        return frame.messages.empty()?std::string{}:std::to_string(frame.messages.back().seq)+'\x1d'+frame.messages.back().text_utf8;
    }
    static bool message_asks(std::string_view text) {
        while(!text.empty()&&(text.back()==' '||text.back()=='\n'))text.remove_suffix(1);
        return text.ends_with("?")||text.ends_with(reinterpret_cast<const char*>(u8"？"));
    }
    static std::string snapshot(const GameFrame &frame,const std::vector<ModernRow> &rows) {
        return snapshot_rows(rows)+'\x1f'+message_key(frame);
    }
    bool pending_elapsed() const {return std::chrono::steady_clock::now()-pending_since_>std::chrono::milliseconds(1500);}
    bool enabled_{},available_{},active_{},root_{},pending_{},pending_confirmed_{},consumed_{},bare_more_{},rogue_message_more_{},last_command_boundary_{},command_sent_{};
    std::chrono::steady_clock::time_point pending_since_{};
    int revision_{4000000},request_{},last_native_revision_{},slot_{},location_{},count_{},navigation_key_{},navigation_steps_{};
    std::string language_{"en"},profile_,signature_,result_,label_,pending_snapshot_,current_message_;
    ModernWindow current_;
    std::vector<ModernRow> current_rows_;
    std::unordered_map<std::string,std::vector<int>> command_keys_;
};
}
