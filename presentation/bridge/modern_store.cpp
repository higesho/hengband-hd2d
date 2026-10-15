#include "bridge/native_modern.h"
#include "bridge/native_targeting_runtime.h"
#include "bridge/native_item_display_impl.h"
#include "bridge/native_inventory_menu.h"
#include "frame/pad_command_table.h"
#include "object-hook/hook-expendable.h"
#include "bridge/modern_store.h"
#include "bridge/hengband_source_version.h"
#if HB_HENGBAND_SOURCE_3025
#include "bridge/store_screen_snapshot.h"
#include "store/store-owners.h"
#include "store/store-util.h"
#include "world/world.h"
#endif
#include "bridge/modern_item_details.generated.h"
#include "bridge/modern_artifact_lore.generated.h"
#include "frame/modern_window_codec.h"
#include "frame/message_more.h"
#include "bridge/input_event_adapter.h"
#include "bridge/official_tile_table.h"
#include "bridge/unaware_item_lookup.h"
#include "flavor/flavor-describer.h"
#include "locale/character-encoding.h"
#include "locale/japanese.h"
#include "store/store.h"
#include "store/pricing.h"
#include "store/store-owners.h"
#include "player/race-info-table.h"
#include "store/service-checker.h"
#include "object/object-info.h"
#include "object/item-use-flags.h"
#include "object/item-tester-hooker.h"
#include "object-hook/hook-armor.h"
#include "object-hook/hook-perception.h"
#include "io/input-key-requester.h"
#include "object-enchant/tr-types.h"
#include "inventory/inventory-slot-types.h"
#include "sv-definition/sv-lite-types.h"
#include "inventory/inventory-util.h"
#include "inventory/player-inventory.h"
#include "view/display-symbol.h"
#include "game-option/input-options.h"
#include "market/building-actions-table.h"
#include "system/player-type-definition.h"
#include "system/floor/floor-info.h"
#include "system/grid-type-definition.h"
#include "system/terrain/terrain-definition.h"
#include "system/baseitem/baseitem-definition.h"
#include "system/artifact/artifact-definition.h"
#include "system/building-type-definition.h"
#include "system/enums/terrain/terrain-characteristics.h"
#include "market/building-service.h"
#include "util/enum-converter.h"
#include <algorithm>
#include <cstdio>
#include <set>

namespace presentation {
namespace {
NativeModernMenu native_menu;
NativeInventoryMenu inventory_menu;
bool enabled=false,consumed=false;
int revision=1,last_request=0;
std::string signature,result,pending_action,context_kind,context_title,modern_language{"en"};
int context_feat=-1;
int pending_building_action=BACT_NOTHING;
bool target_selected=false;
ModernWindow current;
NumericInput numeric;
PromptBar prompt;
std::vector<MenuChoice> choices;
std::string utf8(const std::string &s) {auto v=sys_to_utf8(s);return v?*v:s;}
bool has_key(const GameFrame &f,int k) {return std::any_of(f.menu_choices.begin(),f.menu_choices.end(),[k](const auto &c){return c.key==k;});}
std::string identity(const std::string &source,int index,const std::string &label,int count,int price) {
    unsigned long long h=1469598103934665603ULL;
    for(unsigned char c:label) {h^=c;h*=1099511628211ULL;}
    return source+":"+std::to_string(index)+":"+std::to_string(h)+":"+std::to_string(count)+":"+std::to_string(price);
}
void item_details(ModernRow &row,const ItemEntity &item) {
    if(item.is_aware()||item.is_known()||item.has_identification_flag(IdentificationFlag::STORE)) {
        row.tile=lookup_object_tile(static_cast<uint16_t>(item.bi_id));
    } else if(item.get_baseitem_record().is_apparent()) {
        // 正体の分からないアイテム: 分類ごとの代表の板。
        if(const auto category=presentation::unaware_item_category(item))
            row.tile=lookup_object_tile(presentation::unaware_item_representative_bi_id(*category));
    }
    if(item.is_known()) {
        if(item.is_protector() || item.ac!=0 || item.to_a!=0) {
            char b[200];std::snprintf(b,sizeof(b),_("[%d,%+d] : 基本防御力 %d / 防御力補正 %+d","[%d,%+d] : base AC %d / AC bonus %+d"),item.ac,item.to_a,item.ac,item.to_a);
            row.effects.push_back(utf8(b));
        }
        if(item.bi_key.tval()==ItemKindType::BOW) {
            const int power=item.get_arrow_magnification()+(item.get_flags_known().has(TR_XTRA_MIGHT)?1:0);
            row.effects.push_back(utf8(_("射撃倍率（判明分）: x", "Known shooting multiplier: x"))+std::to_string(power));
            const auto ammo=item.get_arrow_kind();
            if(ammo==ItemKindType::SHOT)row.effects.push_back(utf8(_("使用する弾: スリングの弾", "Ammunition: sling shots")));
            else if(ammo==ItemKindType::ARROW)row.effects.push_back(utf8(_("使用する弾: 矢", "Ammunition: arrows")));
            else if(ammo==ItemKindType::BOLT)row.effects.push_back(utf8(_("使用する弾: クロスボウの矢", "Ammunition: crossbow bolts")));
        }
        if(item.is_weapon_ammo() && item.damage_dice.is_valid()) {
            const auto &dice=item.damage_dice;
            row.effects.push_back(utf8(_("基礎ダメージ: ","Base damage: "))+dice.to_string()
                +" ("+std::to_string(dice.num)+"-"+std::to_string(dice.maxroll())+")");
            row.effects.push_back(utf8(_("攻撃ダイスの値です。能力値・技能・敵の防御などによる補正前の値です。", "Weapon dice before adjustments for attributes, skill and enemy defenses.")));
        }
        if(item.is_weapon_armour_ammo() || item.to_h || item.to_d) {
            char b[200];std::snprintf(b,sizeof(b),_("(%+d,%+d) : 命中補正 %+d / ダメージ補正 %+d","(%+d,%+d) : hit %+d / damage %+d"),item.to_h,item.to_d,item.to_h,item.to_d);
            row.effects.push_back(utf8(b));
        }
    }
    if(item.is_known()) {
        const auto flags=item.get_flags_known();
        const std::pair<tr_type,const char*> modifiers[]={
            {TR_STR,_("腕力","Strength")},{TR_INT,_("知能","Intelligence")},{TR_WIS,_("賢さ","Wisdom")},
            {TR_DEX,_("器用さ","Dexterity")},{TR_CON,_("耐久力","Constitution")},{TR_CHR,_("魅力","Charisma")},
            {TR_TUNNEL,_("{+掘} 掘削能力","{+Dg} Digging")},{TR_SPEED,_("スピード","Speed")},
            {TR_STEALTH,_("隠密能力","Stealth")},{TR_SEARCH,_("探索能力","Searching")},
            {TR_INFRA,_("赤外線視力","Infravision")},{TR_BLOWS,_("攻撃回数","Attack count")}};
        for(const auto &[flag,label]:modifiers)if(flags.has(flag))row.effects.push_back(utf8(label)+": "+(item.pval>=0?"+":"")+std::to_string(item.pval));
    }
    const auto *localized=item.is_fixed_artifact()?modern_artifact_text(static_cast<int>(item.fa_id)):nullptr;
    const char *lore=localized?(i18n::is_japanese()?localized->ja_lore:localized->en_lore):nullptr;
    for (const auto &line : modern_known_item_details(item,lore)) {
        if (!row.description.empty()) row.description += "\n";
        row.description += utf8(line);
    }
    if (!item.is_fully_known()) {
        if (!row.description.empty()) row.description += "\n";
        row.description += utf8(_("まだ分かっていない性質があるかもしれません。", "Some properties may still be unknown."));
    }

}
ModernRow make_item(PlayerType *player,const ItemEntity &source,int slot,int tab,bool shop_stock=false) {
    auto displayed=source.clone();
    // STORE names disclose basic stats, even if KNOWN is not set on the stock.
    // Give the read-only display copy the same knowledge; never set FULL_KNOWN.
    if(shop_stock && source.has_identification_flag(IdentificationFlag::STORE))
        displayed.set_identification_flag(IdentificationFlag::KNOWN);
    const auto &item=displayed;
    ModernRow row;row.slot=slot;row.tab=tab;row.count=item.number;row.equipped=tab==1 && slot>=INVEN_MAIN_HAND;
    const auto symbol=item.get_symbol();
    row.glyph=std::string(1,symbol.character>=32&&symbol.character<127?symbol.character:'?');row.glyph_color=symbol.color&15;
    item_details(row,item);row.label=utf8(describe_flavor(player,item,0));
    if(item.is_known()&&item.is_fixed_artifact()) {
        if(const auto *localized=modern_artifact_text(static_cast<int>(item.fa_id))) {
            const auto old_name=utf8(item.get_fixed_artifact().name);
            const auto at=old_name.empty()?std::string::npos:row.label.find(old_name);
            if(at!=std::string::npos)row.label.replace(at,old_name.size(),i18n::is_japanese()?localized->ja_name:localized->en_name);
        }
    }
    char b[64];std::snprintf(b,sizeof(b),"%d.%d %s",_(lb_to_kg_integer(item.weight),item.weight/10),_(lb_to_kg_fraction(item.weight),item.weight%10),_("kg","lb"));row.weight=b;
    return row;
}
std::vector<ModernRow> owned_items(PlayerType *player) {
    std::vector<ModernRow> rows;if(!player)return rows;
    const AllMatchItemTester all;
    const auto pack_labels=prepare_label_string(player,USE_INVEN,all),equip_labels=prepare_label_string(player,USE_EQUIP,all);
    for(int slot=0;slot<INVEN_TOTAL;++slot) {
        if(!player->inventory[slot]||!player->inventory[slot]->is_valid())continue;
        const auto &item=*player->inventory[slot];auto row=make_item(player,item,slot,1);
        row.selection=row.equipped?1:0;row.price_known=false;
        const auto &labels=row.equipped?equip_labels:pack_labels;const int index=slot-(row.equipped?INVEN_MAIN_HAND:0);
        if(index>=0&&index<int(labels.size()))row.hotkey=row.key=labels[index]==' '?0:labels[index];
        row.id=identity("item:"+std::to_string(row.selection),slot,row.label,row.count,0);
        const auto action=[&](int command){row.actions.push_back({std::to_string(command),"","",true,command});};
        /*
         * 命令は原作の選択肢に載るものだけを出す。載らない行に出すと、NativeInventoryMenu が
         * 対象の一覧へ移ろうとして '6' を 32 回送った末に諦め、原作の選択肢が素のまま残る
         * （2026-09-25。破壊 `k` は持ち物と床だけ＝`cmd-destroy.cpp` の USE_INVEN | USE_FLOOR、
         * 燃料補給 `F` は装備中の光源に合う持ち物だけ＝`cmd-refill.cpp`）。
         */
        action('I');action('{');if(item.inscription&&!item.inscription->empty())action('}');action('d');if(!row.equipped)action('k');
        if(row.equipped)action('t');
        else {action('v');if(item_tester_hook_wear(player,&item))action('w');}
        if(item_tester_hook_eatable(player,&item))action('E');
        if(item_tester_hook_quaff(player,&item))action('q');
        if(item.is_readable())action('r');
        if(row.equipped&&item.is_activatable())action('A');
        if(item.is_ammo())action('f');
        switch(item.bi_key.tval()) {
        case ItemKindType::STAFF:action('u');break;
        case ItemKindType::WAND:action('a');break;
        case ItemKindType::ROD:action('z');break;
        case ItemKindType::LITE:case ItemKindType::FLASK: {
            const auto &light=player->inventory[INVEN_LITE];
            const bool lantern=light&&light->is_valid()&&light->bi_key==BaseitemKey(ItemKindType::LITE,SV_LITE_LANTERN);
            const bool torch=light&&light->is_valid()&&light->bi_key==BaseitemKey(ItemKindType::LITE,SV_LITE_TORCH);
            if(!row.equipped&&((lantern&&item.can_refill_lantern())||(torch&&item.can_refill_torch())))action('F');
            break;
        }
        default:break;
        }
        if(static_cast<int>(item.bi_key.tval())>=90){action('b');action('m');action('G');}
        rows.push_back(std::move(row));
    }
    return rows;
}
std::vector<ModernRow> building_identify_owned_items(PlayerType *player,bool full_identify) {
    std::vector<ModernRow> rows;if(!player)return rows;
    const auto predicate=full_identify?object_is_not_fully_identified:object_is_not_identified;
    const FuncItemTester unidentified(predicate);
    const bool filter_unidentified=can_get_item(player,unidentified);
    int source_row[2]={1,1};
    for(int slot=0;slot<INVEN_TOTAL;++slot) {
        if(!player->inventory[slot]||!player->inventory[slot]->is_valid())continue;
        const auto &item=*player->inventory[slot];
        if(filter_unidentified&&!predicate(&item))continue;
        const int location=slot>=INVEN_MAIN_HAND?1:0;
        auto row=make_item(player,item,slot,location);row.price_known=false;
        row.id=identity("building-item:"+std::to_string(location),slot,row.label,row.count,0);
        row.selection=location;row.slot=source_row[location]++;row.key=13;row.hotkey=0;
        row.actions={{"choice",row.label,"",true,13}};rows.push_back(std::move(row));
    }
    return rows;
}
PadCommandsMessage inventory_commands() {
    PadCommandsMessage table;
    for(const auto &entry:pad_command_table()) {
        PadCommandWireEntry command;command.command=entry.command;command.label_utf8=entry.label_utf8;
        const auto keys=pad_command_key_sequence(entry.command);command.seq_original.assign(keys.begin(),keys.end());
        table.entries.push_back(std::move(command));
    }
    return table;
}
void map_item_choices(ModernWindow &w,const GameFrame &f,PlayerType *player,StoreSaleType kind) {
    if(w.stage!="own_choose")return;
    if(!f.menu_core_cursors.empty())w.cursor=f.menu_core_cursor.line_index;
    w.can_toggle=command_wrk==USE_INVEN||command_wrk==USE_EQUIP||command_wrk==USE_FLOOR;
    // The first item selection of these commands uses exactly these testers and
    // slot order in show_inventory/show_equipment. Labels may be truncated or identical.
    const FuncItemTester selling(store_will_buy,player,kind);
    const FuncItemTester wearing(item_tester_hook_wear,player);
    const AllMatchItemTester all;
    const ItemTester &tester=pending_action=="equip"?static_cast<const ItemTester&>(wearing):
        (pending_action=="takeoff"?static_cast<const ItemTester&>(all):static_cast<const ItemTester&>(selling));
    int source_row=1;
    for(auto &row:w.rows) {
        if(row.tab!=1||(command_wrk!=USE_EQUIP&&command_wrk!=USE_INVEN))continue;
        if((command_wrk==USE_EQUIP)!=row.equipped||row.slot==INVEN_PACK)continue;
        if(!tester.okay(player->inventory[row.slot].get()))continue;
        const int expected_row=source_row++;
        for(size_t n=0;n<f.menu_term_lines.size();++n) {
            if(f.menu_term_lines[n].source_row!=expected_row)continue;
            row.selection=static_cast<int>(n);
            for(const auto &choice:f.menu_choices)if(choice.line_index==row.selection)row.key=choice.key;
            break;
        }
    }
}

}
bool modern_store_enabled() {return enabled;}
void set_modern_store_language(const std::string &language){modern_language=language=="ja"?"ja":"en";native_menu.set_language(modern_language);native_menu.set_document_profile("heng");inventory_menu.configure(enabled,modern_language,"heng");}
void set_modern_store_enabled(bool value) {native_menu.set_enabled(value);enabled=value;inventory_menu.configure(enabled,modern_language,"heng");if(!enabled){current={};signature.clear();consumed=false;pending_action.clear();pending_building_action=BACT_NOTHING;context_kind.clear();context_feat=-1;}}
namespace {
bool item_window(const std::string &kind) {return kind=="store"||kind=="home"||kind=="museum";}
bool modal(const GameFrame &f) {return f.numeric.active||f.text_input_active||!f.prompt.choices.empty();}
std::string modal_stage(const GameFrame &f) {
    return f.numeric.active?"number":(!f.prompt.choices.empty()?"confirm":(f.text_input_active?"text":""));
}
std::string frame_body(const GameFrame &f) {
    std::string body;
    for(size_t i=0;i<f.menu_term_lines.size();++i) {
        const auto &line=f.menu_term_lines[i];
        // Keep the row indices, but move active input and response prompts below the body.
        const bool input=(f.numeric.active&&static_cast<int>(i)==f.numeric.line_index)
            ||(!f.prompt.choices.empty()&&static_cast<int>(i)==f.prompt.line_index)
            ||(f.text_input_active&&!message_more_pending(f)&&static_cast<int>(i)==f.menu_term_curs_row);
        if(!input)body+=line.text_utf8;
        body+="\n";
    }
    return body;
}
void dialog_choice(ModernWindow &w,const std::string &label,int key) {
    ModernRow row;row.slot=static_cast<int>(w.rows.size());row.key=key;row.label=label;
    row.id=identity("dialog",row.slot,label,0,key);row.price_known=false;
    row.actions.push_back({"choice",label,"",true});w.rows.push_back(std::move(row));
}
void capture_dialog(ModernWindow &w,const GameFrame &f) {
    w.kind="dialog";w.title=context_title;w.body=frame_body(f);w.stage=modal_stage(f);
    w.cursor=f.menu_core_cursor.line_index;
    w.input=f.prompt.text_utf8;
    if(w.input.empty()&&!f.menu_term_lines.empty())w.input=f.menu_term_lines.front().text_utf8;
    w.prev_key=f.menu_page_prev_key;w.next_key=f.menu_page_next_key;
    if(message_more_pending(f)&&!f.numeric.active&&f.prompt.choices.empty()){w.stage="detail";w.continue_key=' ';return;}
    if(!w.stage.empty())return;
    const auto contains=[&](const char *s){return w.body.find(s)!=std::string::npos;};
    // These commands are taken from market/building-monster.cpp, poker.cpp,
    // building-craft-weapon.cpp and play-gamble.cpp. Do not infer arbitrary keys.
    if(message_more_pending(f)) {
        w.stage="detail";w.continue_key=' ';
    } else if(contains("モンスターの文字を入力")||contains("Enter character to be identified")) {
        dialog_choice(w,utf8(_("全モンスター","All monsters")),1);
        dialog_choice(w,utf8(_("ユニーク・モンスター","Unique monsters")),21);
        dialog_choice(w,utf8(_("ユニーク以外","Non-unique monsters")),14);
        dialog_choice(w,utf8(_("名前で調べる","Search by name")),13);
        for(int key=33;key<=126;++key)dialog_choice(w,utf8(_("記号: ","Symbol: "))+static_cast<char>(key),key);
        w.stage="choices";
    } else if(contains("残すカードを決めて")||contains("Keep which cards")) {
        w.stage="cursor";w.continue_key=13;w.cursor=f.menu_term_curs_row;
    } else if(contains("もう一度(Y/N)")||contains("Again(Y/N)")) {
        w.input=utf8(_("もう一度遊びますか？","Play again?"));
        dialog_choice(w,utf8(_("いいえ","No")),'n');dialog_choice(w,utf8(_("はい","Yes")),'y');w.stage="choices";
    } else if(contains("比較対象: 's'で変更")||contains("'s' Select secondary weapon")) {
        dialog_choice(w,utf8(_("比較する武器を変更","Change comparison weapon")),'s');
        dialog_choice(w,utf8(_("比較を終える","Finish comparison")),27);w.stage="choices";
    } else if(contains("'r'思い出")||contains("(r)ecall, ESC, space to continue")) {
        dialog_choice(w,utf8(_("思い出を表示／閉じる","Show / hide recall")),'r');
        dialog_choice(w,utf8(_("次のモンスター","Next monster")),' ');
        dialog_choice(w,utf8(_("前のモンスター","Previous monster")),'-');w.stage="choices";
    } else if(!f.menu_core_cursors.empty()||f.menu_core_cursor.line_index>=0) {
        w.stage="cursor";w.cursor=f.menu_core_cursor.line_index;w.continue_key=f.menu_core_cursor.key?f.menu_core_cursor.key:13;
    } else if(!f.menu_choices.empty()) {
        w.stage="choices";
        for(const auto &c:f.menu_choices) {
            if(c.line_index<0||c.line_index>=static_cast<int>(f.menu_term_lines.size())||!c.key)continue;
            const auto &line=f.menu_term_lines[c.line_index].text_utf8;
            const int start=std::clamp(c.span_begin,0,static_cast<int>(line.size()));
            const int length=std::clamp(c.span_len,0,static_cast<int>(line.size())-start);
            dialog_choice(w,length?line.substr(start,length):line,c.key);
        }
    } else {w.stage="detail";w.continue_key=13;}
    if(w.stage=="cursor")native_cursor_selection(f,w);
}
}
void capture_modern_store(GameFrame &f,PlayerType *player) {
    if(!enabled)return;
    prepare_native_targeting_menu_frame(f);
    native_terminal_navigation(f);
    ModernWindow w;w.language=modern_language;w.version=5;
    if(player&&player->current_floor_ptr&&!f.pre_game_menu&&(f.menu_open||modal(f))) {
        const auto &grid=player->current_floor_ptr->get_grid(player->get_position());const auto &terrain=grid.get_terrain();
#if HB_HENGBAND_SOURCE_3025
        // 変愚 3.0.2.5: 店の大域変数の代わりに、原作が店の一覧を描いた瞬間の写しを読む（store_screen_snapshot.h）
        const auto kind=terrain.store_sale_type;
        // 原作が店の一覧を描いた瞬間の写し（狙いの上書きが display_store_inventory() の頭に差す。store_screen_snapshot.h）。
        // ゲームの画面へ戻った（店を出た）フレームで捨てる。
        auto &snap=store_screen_snapshot;
        if(AngbandWorld::get_instance().character_icky_depth==0)snap=StoreScreenSnapshot{};
        const Store *const store_ptr=snap.store;
        const bool at_store=store_ptr&&kind!=StoreSaleType::NONE&&store_ptr->get_sale_type()==kind;
        const bool home=kind==StoreSaleType::HOME,museum=kind==StoreSaleType::MUSEUM;
        const bool browse=at_store&&has_key(f,'x')&&(home?(has_key(f,'g')&&has_key(f,'d')):(museum?(has_key(f,'d')&&has_key(f,'r')):(has_key(f,'p')&&has_key(f,'s'))));
        const bool more=message_more_pending(f);
        const bool stock_pending=!target_selected&&(pending_action=="buy"||pending_action=="take"||pending_action=="remove");
        const bool own_pending=!target_selected&&(pending_action=="sell"||pending_action=="deposit"||pending_action=="donate"||pending_action=="equip"||pending_action=="takeoff");
        bool choose=stock_pending&&!f.menu_choices.empty();
        for(const auto &c:f.menu_choices)if(choose) {
            if(c.line_index<0||c.line_index>=static_cast<int>(f.menu_term_lines.size())) {choose=false;break;}
            const int source=f.menu_term_lines[c.line_index].source_row;
            if(source<6||source>=6+snap.page_size||c.key<'A'||c.key>'z'){choose=false;break;}
        }
        const bool own_selection=own_pending&&(!f.menu_choices.empty()||f.menu_core_cursor.line_index>=0||modal(f));
        if(at_store&&(browse||choose||own_selection||(context_feat==grid.feat&&item_window(context_kind)&&(modal(f)||more)))) {
            w.kind=home?"home":(museum?"museum":"store");w.title=utf8(terrain.name.data());
            w.venue_header=w.title;
            if(!home&&!museum){const auto &owner=store_ptr->get_owner();w.venue_header+=" ("+std::to_string(owner.max_cost)+")\n"+utf8(owner.owner_name)+" ("+utf8(race_info[enum2i(owner.owner_race)].title.data())+")";}
            context_kind=w.kind;context_title=w.title;context_feat=grid.feat;
            w.page=snap.page_top;w.page_size=std::max(1,snap.page_size);w.stage=modal_stage(f);
            if(more)w.stage="message";
            if(w.stage.empty())w.stage=browse?"browse":(choose?"choose":"own_choose");
            for(int i=0;i<std::min<int>(store_ptr->stock_num,4000);++i) {
                if(i>=static_cast<int>(store_ptr->stock.size())||!store_ptr->stock[i])break;
                const auto &item=*store_ptr->stock[i];auto row=make_item(player,item,i,0,!home&&!museum);
                row.price_known=!home&&!museum;
                if(row.price_known)row.price=price_item(player,item.calc_price(),*store_ptr,StoreTradeType::PLAYER_BUYS);
                row.available=!row.price_known||row.price<=player->au;
                if(!row.available)row.reason=utf8(_("所持金が足りません。","Not enough gold."));
                const int local=i-snap.page_top;row.key=local>=0&&local<snap.page_size?(local<26?'a'+local:'A'+local-26):0;
                const int tag=i%w.page_size;row.hotkey=tag<26?'a'+tag:(tag<52?'A'+tag-26:0);
                row.id=identity(context_kind+"stock",i,row.label,row.count,row.price);
                row.actions.push_back({home?"take":(museum?"remove":"buy"),utf8(home?_("取る","Take"):(museum?_("展示をやめる","Remove exhibit"):_("買う","Buy"))),row.reason,row.available});
                if(museum)row.description+="\n"+utf8(_("展示をやめた品物は戻りません。原作の確認後に取り除かれます。","Removed exhibits cannot be recovered. Removal requires the game's confirmation."));
                w.rows.push_back(std::move(row));
            }
            const AllMatchItemTester every_item;
            const auto inven_tags=prepare_label_string(player,USE_INVEN,every_item);
            const auto equip_tags=prepare_label_string(player,USE_EQUIP,every_item);
            for(int i=0;i<INVEN_TOTAL;++i) {
                if(!player->inventory[i]||!player->inventory[i]->is_valid())continue;
                const auto &item=*player->inventory[i];auto row=make_item(player,item,i,1);
                row.available=store_will_buy(player,&item,kind);
                if(!row.available)row.reason=utf8(_("この店では買い取れません。","This store does not buy this item."));
                const bool known_curse=row.equipped&&item.is_cursed()&&(item.is_known()||item.has_identification_flag(IdentificationFlag::SENSE));
                if(known_curse){row.available=false;row.reason=utf8(_("呪われていて外せません。","Cursed equipment cannot be removed."));}
                row.price_known=!home&&!museum&&item.is_known();
                if(row.price_known)row.price=std::max(0,std::min(price_item(player,item.calc_price(),*store_ptr,StoreTradeType::PLAYER_SELLS),static_cast<int>(store_ptr->get_owner().max_cost)));
                const auto &tags=row.equipped?equip_tags:inven_tags;const int tag=i-(row.equipped?INVEN_MAIN_HAND:0);
                if(tag>=0&&tag<static_cast<int>(tags.size())&&tags[tag]!=' ')row.hotkey=tags[tag];
                row.id=identity("own",i,row.label,row.count,row.price);
                row.actions.push_back({home?"deposit":(museum?"donate":"sell"),utf8(home?_("置く","Deposit"):(museum?_("寄贈する","Donate"):_("売る","Sell"))),row.reason,row.available});
                if(museum)row.description+="\n"+utf8(_("寄贈した品物は取り戻せません。原作の確認後に寄贈します。","Donations cannot be taken back. Donation requires the game's confirmation."));
                if(row.equipped)row.actions.push_back({"takeoff",utf8(_("外す","Take off")),known_curse?utf8(_("呪われていて外せません。","Cursed equipment cannot be removed.")):"",!known_curse});
                else if(item.is_equipment())row.actions.push_back({"equip",utf8(_("装備する","Equip")),"",item_tester_hook_wear(player,&item)});
                w.rows.push_back(std::move(row));
            }
            map_item_choices(w,f,player,kind);
            if(browse&&!modal(f))pending_action.clear();
        } else if(grid.has(TerrainCharacteristics::BLDG)) {
#else
        const auto kind=terrain.store_sale_type;
        const bool at_store=st_ptr&&ot_ptr&&grid.feat==cur_store_feat&&kind!=StoreSaleType::NONE;
        const bool home=kind==StoreSaleType::HOME,museum=kind==StoreSaleType::MUSEUM;
        const bool browse=at_store&&has_key(f,'x')&&(home?(has_key(f,'g')&&has_key(f,'d')):(museum?(has_key(f,'d')&&has_key(f,'r')):(has_key(f,'p')&&has_key(f,'s'))));
        const bool more=message_more_pending(f);
        const bool stock_pending=!target_selected&&(pending_action=="buy"||pending_action=="take"||pending_action=="remove");
        const bool own_pending=!target_selected&&(pending_action=="sell"||pending_action=="deposit"||pending_action=="donate"||pending_action=="equip"||pending_action=="takeoff");
        bool choose=stock_pending&&!f.menu_choices.empty();
        for(const auto &c:f.menu_choices)if(choose) {
            if(c.line_index<0||c.line_index>=static_cast<int>(f.menu_term_lines.size())) {choose=false;break;}
            const int source=f.menu_term_lines[c.line_index].source_row;
            if(source<6||source>=6+store_bottom||c.key<'A'||c.key>'z'){choose=false;break;}
        }
        const bool own_selection=own_pending&&(!f.menu_choices.empty()||f.menu_core_cursor.line_index>=0||modal(f));
        if(at_store&&(browse||choose||own_selection||(context_feat==grid.feat&&item_window(context_kind)&&(modal(f)||more)))) {
            w.kind=home?"home":(museum?"museum":"store");w.title=utf8(terrain.name.data());
            w.venue_header=w.title;
            if(!home&&!museum)w.venue_header+=" ("+std::to_string(ot_ptr->max_cost)+")\n"+utf8(ot_ptr->owner_name)+" ("+utf8(race_info[enum2i(ot_ptr->owner_race)].title.data())+")";
            context_kind=w.kind;context_title=w.title;context_feat=grid.feat;
            w.page=store_top;w.page_size=std::max(1,store_bottom);w.stage=modal_stage(f);
            if(more)w.stage="message";
            if(w.stage.empty())w.stage=browse?"browse":(choose?"choose":"own_choose");
            for(int i=0;i<std::min<int>(st_ptr->stock_num,4000);++i) {
                if(i>=static_cast<int>(st_ptr->stock.size())||!st_ptr->stock[i])break;
                const auto &item=*st_ptr->stock[i];auto row=make_item(player,item,i,0,!home&&!museum);
                row.price_known=!home&&!museum;
                if(row.price_known)row.price=price_item(player,item.calc_price(),ot_ptr->inflate,false,kind);
                row.available=!row.price_known||row.price<=player->au;
                if(!row.available)row.reason=utf8(_("所持金が足りません。","Not enough gold."));
                const int local=i-store_top;row.key=local>=0&&local<store_bottom?(local<26?'a'+local:'A'+local-26):0;
                const int tag=i%w.page_size;row.hotkey=tag<26?'a'+tag:(tag<52?'A'+tag-26:0);
                row.id=identity(context_kind+"stock",i,row.label,row.count,row.price);
                row.actions.push_back({home?"take":(museum?"remove":"buy"),utf8(home?_("取る","Take"):(museum?_("展示をやめる","Remove exhibit"):_("買う","Buy"))),row.reason,row.available});
                if(museum)row.description+="\n"+utf8(_("展示をやめた品物は戻りません。原作の確認後に取り除かれます。","Removed exhibits cannot be recovered. Removal requires the game's confirmation."));
                w.rows.push_back(std::move(row));
            }
            const AllMatchItemTester every_item;
            const auto inven_tags=prepare_label_string(player,USE_INVEN,every_item);
            const auto equip_tags=prepare_label_string(player,USE_EQUIP,every_item);
            for(int i=0;i<INVEN_TOTAL;++i) {
                if(!player->inventory[i]||!player->inventory[i]->is_valid())continue;
                const auto &item=*player->inventory[i];auto row=make_item(player,item,i,1);
                row.available=store_will_buy(player,&item,kind);
                if(!row.available)row.reason=utf8(_("この店では買い取れません。","This store does not buy this item."));
                const bool known_curse=row.equipped&&item.is_cursed()&&(item.is_known()||item.has_identification_flag(IdentificationFlag::SENSE));
                if(known_curse){row.available=false;row.reason=utf8(_("呪われていて外せません。","Cursed equipment cannot be removed."));}
                row.price_known=!home&&!museum&&item.is_known();
                if(row.price_known)row.price=std::max(0,std::min(price_item(player,item.calc_price(),ot_ptr->inflate,true,kind),static_cast<int>(ot_ptr->max_cost)));
                const auto &tags=row.equipped?equip_tags:inven_tags;const int tag=i-(row.equipped?INVEN_MAIN_HAND:0);
                if(tag>=0&&tag<static_cast<int>(tags.size())&&tags[tag]!=' ')row.hotkey=tags[tag];
                row.id=identity("own",i,row.label,row.count,row.price);
                row.actions.push_back({home?"deposit":(museum?"donate":"sell"),utf8(home?_("置く","Deposit"):(museum?_("寄贈する","Donate"):_("売る","Sell"))),row.reason,row.available});
                if(museum)row.description+="\n"+utf8(_("寄贈した品物は取り戻せません。原作の確認後に寄贈します。","Donations cannot be taken back. Donation requires the game's confirmation."));
                if(row.equipped)row.actions.push_back({"takeoff",utf8(_("外す","Take off")),known_curse?utf8(_("呪われていて外せません。","Cursed equipment cannot be removed.")):"",!known_curse});
                else if(item.is_equipment())row.actions.push_back({"equip",utf8(_("装備する","Equip")),"",item_tester_hook_wear(player,&item)});
                w.rows.push_back(std::move(row));
            }
            map_item_choices(w,f,player,kind);
            if(browse&&!modal(f))pending_action.clear();
        } else if(grid.has(TerrainCharacteristics::BLDG)) {
#endif
            const int index=enum2i(terrain.building_type);
            if(index>=0&&index<MAX_BUILDINGS) {
                const auto &b=buildings[index];bool service=false;
                for(const auto &c:f.menu_choices)if(c.line_index>=0&&c.line_index<static_cast<int>(f.menu_term_lines.size())) {
                    const int y=f.menu_term_lines[c.line_index].source_row;
                    if(y>=19&&y<23)for(char key:b.letters)if(key&&c.key==key)service=true;
                }
                if(service&&!message_more_pending(f)) {
                    context_kind="building";context_title=utf8(b.name);context_feat=grid.feat;
                    w.kind="building";w.title=context_title;w.stage=modal_stage(f);if(w.stage.empty())w.stage="services";
                    w.venue_header=w.title+"\n"+utf8(b.owner_name)+" ("+utf8(b.owner_race)+")";
                    const bool owner=is_owner(player,b),member=is_member(player,b);
                    for(int i=0;i<8;++i)if(b.letters[i]) {
                        ModernRow row;row.slot=i;row.selection=b.actions[i];row.key=b.letters[i];row.label=utf8(b.act_names[i]);row.price=owner?b.member_costs[i]:b.other_costs[i];
                        row.available=b.action_restr[i]==0||(b.action_restr[i]==1?member:owner);
                        if(!row.available)row.reason=utf8(_("利用資格がありません。","Membership is required."));
                        else if(row.price>player->au){row.available=false;row.reason=utf8(_("所持金が足りません。","Not enough gold."));}
                        row.id=identity("building"+std::to_string(index),i,row.label,0,row.price);
                        row.actions.push_back({"service",row.label,row.reason,row.available});w.rows.push_back(std::move(row));
                    }
                    for(const auto &line:f.menu_term_lines)if(line.source_row>2&&line.source_row<19)w.body+=line.text_utf8+"\n";
                    if(!modal(f))pending_action.clear();
                }
            }
        }
        if(w.stage.empty()&&more&&context_kind=="building"&&context_feat==grid.feat&&current.kind=="building") {
            const bool continuing=current.stage=="message";
            w=current;
            // Transport identity is assigned after the content signature is
            // computed.  Carrying the previous revision into that signature
            // makes an otherwise unchanged -more- screen revise itself every
            // frame, so every user acknowledgement is stale by arrival.
            w.revision=0;w.request_id=0;w.result.clear();
            w.stage="message";w.continue_key=' ';
            if(!continuing){w.body.clear();w.content.clear();}
        }
        if(w.stage.empty()&&!context_kind.empty()&&context_feat==grid.feat)capture_dialog(w,f);
        if(!w.stage.empty()) {
            if(f.text_input_active&&f.menu_term_curs_row>=0&&f.menu_term_curs_row<static_cast<int>(f.menu_term_lines.size()))
                w.input=f.menu_term_lines[f.menu_term_curs_row].text_utf8;
            if(w.input.empty())w.input=f.prompt.text_utf8;
            if(w.input.empty()&&!f.menu_term_lines.empty())w.input=f.menu_term_lines.front().text_utf8;
        }
    }
    interpret_modern_content(f,w,"heng");
    // Building services such as the village elder's Identify command use the
    // original cursor-only item picker.  That picker is captured here before
    // NativeModernMenu gets a chance to replace the custom building dialog,
    // so expose its live inventory as modern rows at this boundary.
    if(player&&context_kind=="building"&&w.stage=="cursor") {
        const std::string item_prompt=w.input.empty()?w.body:w.input;
        // get_item_floor() owns an item-display scope for the whole native
        // selector.  That state is a stronger signal than the rendered prompt:
        // some building services (notably the village elder's Identify) show
        // only the source selector such as "or / 装備品" and never contain the
        // literal "どのアイテム" text.  Relying on that wording therefore left
        // the raw source-selector fragments in the modern menu instead of the
        // inventory rows.
        const bool native_item_selector=!native_item_sources.empty();
        const bool selecting_item=native_item_selector
            ||item_prompt.find(utf8(_("どのアイテム","Which item")))!=std::string::npos
            ||item_prompt.find("Which item")!=std::string::npos||item_prompt.find("which item")!=std::string::npos;
        if(selecting_item) {
            std::vector<ModernRow> item_rows;
            const auto &native_cursor=f.menu_core_cursors.empty()?f.menu_core_cursor:f.menu_core_cursors.back();
            const int selected_line=native_cursor.line_index>=0&&native_cursor.line_index<int(f.menu_term_lines.size())
                ?f.menu_term_lines[native_cursor.line_index].source_row:-1;
            int selected=-1;
            const auto is_identify_text=[](const std::string &text){
                return text.find(utf8(_("鑑定","Identify")))!=std::string::npos
                    ||text.find("Identify")!=std::string::npos||text.find("identify")!=std::string::npos;
            };
            const bool identify_prompt=is_identify_text(item_prompt)||std::any_of(f.menu_term_lines.begin(),f.menu_term_lines.end(),[&](const auto &line){return is_identify_text(line.text_utf8);});
            const bool identify_action=pending_building_action==BACT_IDENT_ONE||pending_building_action==BACT_RESEARCH_ITEM||identify_prompt;
            const bool combined_identify=identify_action&&native_item_selector&&command_wrk!=USE_FLOOR;
            const auto is_full_identify_text=[](const std::string &text){return text.find("*Identify*")!=std::string::npos||text.find(utf8(_("*鑑定*","*Identify*")))!=std::string::npos;};
            const bool full_identify=pending_building_action==BACT_RESEARCH_ITEM||is_full_identify_text(item_prompt)
                ||std::any_of(f.menu_term_lines.begin(),f.menu_term_lines.end(),[&](const auto &line){return is_full_identify_text(line.text_utf8);});
            if(combined_identify)item_rows=building_identify_owned_items(player,full_identify);
            for(const auto &ref:native_display_items) {
                if(!ref.object)continue;
                const auto &item=*static_cast<const ItemEntity *>(ref.object);if(!item.is_valid())continue;
                auto row=make_item(player,item,ref.slot,ref.location==1?1:0);row.key=13;row.hotkey=0;row.price_known=false;
                const auto row_id=identity("building-item:"+std::to_string(ref.location),ref.slot,row.label,row.count,0);
                if(combined_identify) {
                    if(ref.selected==1||(ref.selected<0&&ref.source_row==selected_line)) {
                        const auto at=std::find_if(item_rows.begin(),item_rows.end(),[&](const auto &candidate){return candidate.id==row_id;});
                        if(at!=item_rows.end())selected=int(at-item_rows.begin());
                    }
                    continue;
                }
                row.slot=ref.source_row;row.selection=ref.location;row.id=row_id;
                row.actions={{"choice",row.label,"",true,13}};
                if(ref.selected==1||(ref.selected<0&&ref.source_row==selected_line))selected=int(item_rows.size());
                item_rows.push_back(std::move(row));
            }
            // If a late subwindow refresh has released the object references,
            // retain the exact rows the original picker painted.  Their
            // leading symbol is the native item icon (for example ? or =).
            if(item_rows.empty())for(const auto &line:f.menu_term_lines) {
                if(line.source_row<=0)continue;
                std::string text=modern_trim(line.text_utf8);const std::string arrow=reinterpret_cast<const char*>(u8"》");
                if(text.starts_with(arrow))text=modern_trim(text.substr(arrow.size()));if(text.empty())continue;
                ModernRow row;row.slot=line.source_row;row.selection=0;row.key=13;row.hotkey=0;row.price_known=false;row.available=true;
                if(text.size()>1&&text[1]==' '&&!std::isalnum(static_cast<unsigned char>(text[0]))) {row.glyph=text.substr(0,1);text=modern_trim(text.substr(2));}
                const auto kg=text.rfind(" kg"),lb=text.rfind(" lb"),unit=kg!=std::string::npos?kg:lb;
                if(unit!=std::string::npos){const auto split=text.rfind("  ",unit);if(split!=std::string::npos){row.weight=modern_trim(text.substr(split));text=modern_trim(text.substr(0,split));}}
                row.label=text;row.id=identity("building-terminal-item",line.source_row,row.label,1,0);row.actions={{"choice",row.label,"",true,13}};
                if(line.source_row==selected_line)selected=int(item_rows.size());item_rows.push_back(std::move(row));
            }
            if(!item_rows.empty()) {
                w.rows=std::move(item_rows);w.body.clear();w.content.clear();w.content_type="items";
                w.actions.clear();
                w.title=utf8(combined_identify?_("持ち物・装備","Inventory / equipment")
                                               :(command_wrk==USE_FLOOR?_("床のアイテム","Floor items"):_("持ち物一覧","Inventory")));
                w.cursor=selected>=0?selected:0;w.cursor_target=w.rows[w.cursor].id;
                const int sources=native_item_sources.empty()?0:native_item_sources.back();
                if(identify_action&&(sources&USE_FLOOR)) {
                    if(command_wrk==USE_FLOOR)modern_add_action(w,(sources&USE_INVEN)?'6':'4',utf8(_("持ち物・装備","Inventory / equipment")));
                    else modern_add_action(w,command_wrk==USE_INVEN?'4':'6',utf8(_("床のアイテム","Floor items")));
                }
            }
        }
    }
    for(auto &row:w.rows) {
        if(!row.hotkey&&!item_window(w.kind))row.hotkey=row.key;
        for(auto &action:row.actions) {
            if(action.id=="buy")action.key='p';else if(action.id=="sell")action.key='s';
            else if(action.id=="take")action.key='g';else if(action.id=="remove")action.key='r';
            else if(action.id=="deposit"||action.id=="donate")action.key='d';
            else if(action.id=="equip")action.key='w';else if(action.id=="takeoff")action.key=rogue_like_commands?'T':'t';
            else if(action.id=="service"||action.id=="choice")action.key=row.key;
        }
    }
    native_menu_key_aliases(w,f);
    if(w.stage.empty()){pending_action.clear();context_kind.clear();context_title.clear();context_feat=-1;}
    std::string next=encode_modern_window(w).dump(-1,' ',false,nlohmann::json::error_handler_t::replace)+f.prompt.text_utf8+std::to_string(f.menu_term_curs_row)+":"+std::to_string(f.menu_term_curs_col)
        +(f.messages.empty()?"":f.messages.back().text_utf8)+std::to_string(f.numeric.active)+":"+std::to_string(f.numeric.min)+":"+std::to_string(f.numeric.max)+":"+std::to_string(f.numeric.value);
    if(next!=signature||consumed){signature=std::move(next);++revision;consumed=false;}
    w.revision=revision;w.request_id=last_request;w.result=result;
    w.text_confirm_action=w.stage=="text";
    current=w;numeric=f.numeric;prompt=f.prompt;choices=f.menu_choices;f.modern=std::move(w);
    std::vector<NativeItemRow> items;
    if(player&&f.modern.stage.empty())for(const auto &ref:native_display_items) {
        if(!ref.object){ModernRow row;row.id="slot:"+std::to_string(ref.location)+":"+std::to_string(ref.slot);row.label=utf8(ref.empty_label);row.key=row.hotkey=ref.key;row.slot=ref.slot;row.selection=ref.location;row.price_known=false;
            items.push_back({std::move(row),ref.source_row,ref.selected,native_item_sources.empty()?0:native_item_sources.back()});continue;}
        const auto &item=*static_cast<const ItemEntity *>(ref.object);if(!item.is_valid())continue;
        auto row=make_item(player,item,ref.slot,ref.location==1?1:0);row.key=row.hotkey=ref.key;row.price_known=false;row.selection=ref.location;
        row.id=identity("item:"+std::to_string(ref.location),ref.slot,row.label,row.count,0);
        hb_target_item_menu_row(row.id.c_str(),ref.object);
        items.push_back({std::move(row),ref.source_row,ref.selected,native_item_sources.empty()?0:native_item_sources.back()});
    }
    native_menu.capture(f,{},items);
    apply_native_targeting_menu_frame(f);
    inventory_menu.capture(f,[&]{return owned_items(player);},inventory_commands);
}
void modern_store_reject_queued_request(int id) {
    native_menu.adopt_request(id,"rejected");
    if(id>last_request){last_request=id;result="rejected";}
}
bool modern_store_input(const InputEventWire &e,std::vector<int> &keys) {
    if(inventory_menu.input(e,keys))return true;
    if(native_menu.input(e,keys))return true;
    if(e.e!="ui_action")return false;
    if(e.request_id<=last_request)return true;
    last_request=e.request_id;result="rejected";
    if(!enabled || (consumed&&e.action!="cancel") || e.ui_revision!=revision || current.stage.empty())return true;
    const auto row=std::find_if(current.rows.begin(),current.rows.end(),[&](const auto &r){return r.id==e.target;});
    int key=0;
    if(e.action=="cancel") {key=27;pending_action.clear();pending_building_action=BACT_NOTHING;}
    else if(e.action=="focus"&&current.stage=="cursor"&&row!=current.rows.end()) {
        if(current.cursor_target==e.target){result="accepted";return true;}
        const auto cursor=std::find_if(current.rows.begin(),current.rows.end(),[&](const auto &candidate){return candidate.id==current.cursor_target;});
        const bool combined_building_items=current.kind=="building"&&current.content_type=="items"&&current.title==utf8(_("持ち物・装備","Inventory / equipment"));
        if(combined_building_items&&cursor!=current.rows.end()&&row->selection!=cursor->selection)
            key=row->selection>cursor->selection?'6':'4';
        else key=modern_focus_key(current,e.target);
    }
    else if(e.action=="choice"&&current.stage=="cursor"&&row!=current.rows.end()&&row->available)key=row->key;
    else if(e.action=="window_action") {for(const auto &a:current.actions)if(a.id==e.target&&a.enabled)key=a.key;}
    else if(e.action=="native_key"&&e.number>0&&e.number<128) {
        const bool listed=std::any_of(choices.begin(),choices.end(),[&](const auto &c){return c.key==e.number;});
        if((current.kind=="dialog"&&(current.stage=="cursor"||current.stage=="detail"))||(listed&&current.stage!="text"&&current.stage!="number"&&current.stage!="confirm")) {
            key=e.number;
            if(current.stage=="browse"&&item_window(current.kind)) {
                if(key=='p'||key=='g')pending_action=current.kind=="home"?"take":"buy";
                else if(key=='s'||key=='d')pending_action=current.kind=="home"?"deposit":(current.kind=="museum"?"donate":"sell");
                else if(key=='r'&&current.kind=="museum")pending_action="remove";
                else if(key=='w')pending_action="equip";else if(key==(rogue_like_commands?'T':'t'))pending_action="takeoff";
                target_selected=false;
            }
        }
    }
    else if((current.stage=="browse"||current.stage=="services") && row!=current.rows.end()) {
        if(e.action=="page_for" && row->tab==0 && item_window(current.kind)) {
            if(row->slot<current.page)key='-';else if(row->slot>=current.page+current.page_size)key=' ';
        } else {
            const auto action=std::find_if(row->actions.begin(),row->actions.end(),[&](const auto &a){return a.id==e.action&&a.enabled;});
            if(action!=row->actions.end()) {
                if((e.action=="buy"||e.action=="take"||e.action=="remove")&&row->slot>=current.page&&row->slot<current.page+current.page_size)
                    key=e.action=="buy"?'p':(e.action=="take"?'g':'r');
                if(e.action=="sell")key='s';if(e.action=="deposit"||e.action=="donate")key='d';if(e.action=="equip")key='w';if(e.action=="takeoff")key=rogue_like_commands?'T':'t';
                if(e.action=="service"&&std::any_of(choices.begin(),choices.end(),[&](const auto &c){return c.key==row->key;}))key=row->key;
                if(key){pending_action=e.action;if(e.action=="service")pending_building_action=row->selection;target_selected=false;}
            }
        }
    } else if(current.stage=="choose" && e.action=="select" && row!=current.rows.end() && row->tab==0) {
        if(std::any_of(choices.begin(),choices.end(),[&](const auto &c){return c.key==row->key;})){key=row->key;target_selected=true;}
    } else if(current.stage=="own_choose" && e.action=="select" && row!=current.rows.end() && row->tab==1) {
        if(row->selection>=0 && current.cursor>=0){key=row->selection==current.cursor?13:(row->selection>current.cursor?'2':'8');if(key==13)target_selected=true;}
        else if(row->selection>=0 && row->key && std::any_of(choices.begin(),choices.end(),[&](const auto &c){return c.key==row->key&&c.line_index==row->selection;})){key=row->key;target_selected=true;}
        else if(current.can_toggle) {
            if(!use_menu)key=command_see?'/':'*';
            else if(row->selection<0)key=row->equipped?(command_wrk==USE_FLOOR?'4':'6'):(command_wrk==USE_EQUIP?'4':'6');
        }
    } else if(current.kind=="dialog"&&current.stage=="choices"&&e.action=="choice"&&row!=current.rows.end()) {
        key=row->key;
    } else if(current.kind=="dialog"&&current.stage=="cursor"&&e.action=="navigate") {
        if(e.number==2||e.number==4||e.number==6||e.number==8)key=e.number+'0';
    } else if(current.kind=="dialog"&&(current.stage=="cursor"||current.stage=="detail")&&e.action=="continue") {
        key=current.continue_key;
    } else if(current.kind=="dialog"&&e.action=="document_page") {
        key=e.number==0?current.prev_key:current.next_key;
    } else if(current.stage=="message"&&e.action=="continue") {key=current.continue_key;
    } else if(current.stage=="text"&&e.action=="text_confirm") {key=13;
    } else if(current.stage=="number"&&e.action=="number"&&numeric.active&&e.number>=numeric.min&&e.number<=numeric.max) {
        InputEventsMessage m;InputEventWire n;n.e="set_number";n.number=e.number;n.digits=1;m.events.push_back(n);append_input_event_keys(m,keys);key=13;
    } else if(current.stage=="confirm"&&e.action=="answer"&&static_cast<size_t>(e.number)<prompt.choices.size())key=prompt.choices[static_cast<size_t>(e.number)].key;
    if(key) {keys.push_back(key);result="accepted";consumed=e.action!="service"&&e.action!="navigate"&&e.action!="choice"&&e.action!="continue"&&e.action!="document_page"&&e.action!="native_key";}
    return true;
}
}
