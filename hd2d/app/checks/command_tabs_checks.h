#pragma once
#include "ui/feature_menu.h"
#include <cstdio>
namespace hd2d {
inline int run_command_tabs_checks()
{
    int passed=0,failed=0;
    auto check=[&](bool value,const char *label){if(value)++passed;else {++failed;std::fprintf(stderr,"FAIL: %s\n",label);}};
    MenuTabs tabs;
    tabs.reset({{"a","A",{"1","2"}},{"","Other",{"3"}}});
    check(tabs.index()==0,"first tab on first load");tabs.remember(1,7);tabs.step(1);
    check(tabs.index()==1&&tabs.cursor()==0,"next tab starts at first row");tabs.step(-1);
    check(tabs.cursor()==1&&tabs.scroll()==7,"per-tab position");
    tabs.reset({{"a","A",{"2","1"}},{"","Other",{"3"}}});
    check(tabs.cursor()==0,"item identity survives reordering");
    tabs.reset({{"a","A",{"1"}},{"","Other",{"3"}}});check(tabs.cursor()==0,"removed item clamps");
    tabs.reset({});tabs.step(1);check(tabs.tabs().empty()&&tabs.cursor()==0,"empty tabs");
    std::vector<PadCommand> commands;
    for(int i=1;i<=5;++i) {
        PadCommand c;c.id=i;c.group_utf8=i<=2?"Actions":(i<=4?"Items":"Info");
        c.label_utf8="Command "+std::to_string(i);if(i<5)c.keys={96+i};
        if(i==2)c.subgroup_utf8="Subgroup";
        commands.push_back(c);
    }
    Hd2dSettings settings;settings.modern_ui=true;
    FeatureMenu menu;menu.set_pad_state(commands,true,"test");menu.open_commands(settings);
    check(menu.showing_command_tabs()&&menu.command_tab_labels().size()==5,"modern command tabs include inventory");
    check(menu.command_labels().size()==2,"subgroups flattened within tab");
    menu.handle(MenuNav::Down,settings);menu.handle(MenuNav::Confirm,settings);
    check(menu.take_command_request()==2&&menu.take_command_request()==0,"confirm requests command exactly once");
    menu.handle(MenuNav::NextTab,settings);check(menu.command_tab_index()==1,"RB changes category");
    menu.handle(MenuNav::Down,settings);menu.handle(MenuNav::PreviousTab,settings);menu.handle(MenuNav::Confirm,settings);
    check(menu.take_command_request()==2,"return restores command");
    menu.handle(MenuNav::NextTab,settings);menu.close();menu.open_commands(settings);menu.handle(MenuNav::Confirm,settings);
    check(menu.command_tab_index()==1&&menu.take_command_request()==4,"reopen restores tab and command");
    menu.handle(MenuNav::Categories,settings);check(menu.command_labels().size()==5,"category chooser");
    menu.handle(MenuNav::Down,settings);menu.handle(MenuNav::Confirm,settings);
    check(menu.command_tab_index()==2&&menu.take_command_request()==0,"category choice does not run command");
    menu.handle(MenuNav::Confirm,settings);check(menu.take_command_request()==0,"unavailable command cannot run");
    menu.handle(MenuNav::Categories,settings);menu.handle(MenuNav::Cancel,settings);check(menu.is_open(),"cancel chooser keeps menu open");
    menu.handle(MenuNav::Cancel,settings);check(!menu.is_open(),"cancel direct menu closes it");
    settings.modern_ui=false;menu.open_commands(settings);check(!menu.showing_command_tabs(),"original mode retains hierarchy");
    settings.modern_ui=true;FeatureMenu empty;empty.open_commands(settings);empty.handle(MenuNav::Confirm,settings);
    check(empty.take_command_request()==0,"empty command menu cannot execute");empty.handle(MenuNav::Cancel,settings);check(!empty.is_open(),"empty menu can close");
    FeatureMenu zero;PadCommand first=commands.front();first.id=0;
    zero.set_pad_state({first},true,"test");zero.open_commands(settings);zero.handle(MenuNav::Confirm,settings);
    int id=-1;check(zero.take_pending_command_request(id)&&id==0,"zero command ID is explicit");
    check(!zero.take_pending_command_request(id),"zero command is consumed once");
    {
        PadCommand action;action.id=1;action.command='s';action.group_utf8="行動";action.label_utf8="探索";action.keys={'s'};
        PadCommand info;info.id=2;info.command='?';info.group_utf8="情報";info.label_utf8="ヘルプ";info.keys={'?'};
        PadCommand option;option.id=23;option.command='o';option.group_utf8="設定";option.label_utf8="Rogueの設定";option.keys={'o'};
        FeatureMenu rogue;rogue.set_pad_state({action,info,option},true,"rogue");rogue.open_commands(settings);
        const auto labels=rogue.command_tab_labels();
        const auto it=std::find(labels.begin(),labels.end(),std::string("設定"));
        check(it!=labels.end(),"Rogue settings appear as a first-level Settings tab");
        if(it!=labels.end()) {
            rogue.open_command_tab(static_cast<int>(std::distance(labels.begin(),it)),settings);
            int command=-1;
            check(rogue.take_pending_command_request(command)&&command==23,"opening the Rogue Settings tab dispatches native options directly");
            check(!rogue.take_pending_command_request(command),"direct Settings-tab dispatch is consumed once");
            // 設定画面を「戻る」で閉じたあと、メニューを開き直しただけで o を送らない。
            rogue.close();rogue.open_features(settings);
            check(!rogue.take_pending_command_request(command),"reopening the feature menu after Rogue Settings does not dispatch options again");
            check(!rogue.take_inventory_request(),"reopening the feature menu does not open the inventory tab");
            rogue.close();rogue.open_commands(settings);
            check(!rogue.take_pending_command_request(command)&&rogue.command_tab_labels()[rogue.command_tab_index()]!="設定",
                "reopening the command menu after Rogue Settings starts from a normal tab");
            /*
             * 見出しを押して先頭のタブから最後のタブへ移っても、途中の「設定」（o を送る）と
             * 「持ち物一覧」（一覧を開く）の処理は走らない（2026-10-07。以前は 1 つずつ移っていた）。
             */
            rogue.jump_to_command_tab(0);(void)rogue.take_pending_command_request(command);(void)rogue.take_inventory_request();
            const int last=static_cast<int>(rogue.command_tab_labels().size())-1;
            rogue.jump_to_command_tab(last);
            check(rogue.command_tab_index()==last,"a header click lands on the clicked tab");
            check(!rogue.take_pending_command_request(command),"a header click does not dispatch Settings on the way");
            check(!rogue.take_inventory_request(),"a header click does not open the inventory on the way");
        }
    }
    FeatureMenu appearance;appearance.open_features(settings);
    const char *font_label=i18n::current()=="ja"?"文字サイズ":"Text size";
    check(appearance.focus_item(font_label),"modern text-size setting can be focused directly");
    settings.modern_font_level=1;
    appearance.handle(MenuNav::Left,settings);
    check(settings.modern_font_level==0,"modern setting changes immediately with Left");
    appearance.handle(MenuNav::Confirm,settings);
    check(settings.modern_font_level==0,"Confirm does not enter or change a second edit mode");
    appearance.handle(MenuNav::Right,settings);
    check(settings.modern_font_level==1,"modern setting changes immediately with Right");
    presentation::InputEventsMessage input;
    check(!emit_core_command(0,{first},input)&&input.events.empty(),"unbound zero is unchanged");
    check(emit_core_command(0,{first},input,true)&&input.events.size()==1,"explicit menu zero can execute");
    std::fprintf(stderr,"Command tabs: %d passed, %d failed\n",passed,failed);return failed?1:0;
}
}
