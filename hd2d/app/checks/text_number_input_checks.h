#pragma once
/*!
 * @file text_number_input_checks.h
 * @brief 数の欄と文字入力の欄の検査（修-06・修-58）。窓も GL もコアも要らない。
 * @details `HD2D_TEXT_NUMBER_INPUT_CHECK=1 HengbandHd2d.exe --ui-check`。本物の `ModernStoreUi` に窓を直接渡して確かめる。
 *  - 修-06（数の欄）：初期値が範囲の外（Rogue 5.4 の繰り返し回数は初期値 0・最小 1）でも、決定は範囲に直した値を送る。
 *    数字を打ち足して最大を超えたら最大に張り付き（決定は最大を送る）、そのあとの Backspace は 1 字ずつ。
 *    最大に収まる打ち足し（123）は今までどおり。最小未満（0）の決定は何も送らない。
 *  - 修-58（文字入力の欄）：「基本」型（幻想蛮怒・Frox）で、まだ何も編集していない欄の最初の Backspace・Delete は既定の字を全部消す
 *    （末尾まで Ctrl-F、字数ぶんの Backspace）。2 回目以降は 1 字ずつ。字を打つ・端へ動かすと 1 字ずつに戻る。
 *    欄が新しく出ると、また全部消す。全角の字は 1 字と数える。「全部」型（変愚・短愚）・「追記」型（Sil-Q）は今までどおり 1 字ずつ。
 */
#include "ui/modern_store_ui.h"
#include <cstdio>
#include <string>
namespace hd2d {
namespace text_number_check {
inline GameFrame number_frame(int revision,int min,int max,int value) {
    GameFrame f;f.menu_open=true;f.cells.emplace_back();
    auto &m=f.modern;m.version=5;m.kind="dialog";m.stage="number";m.title="Quantity";m.revision=revision;m.language="en";
    f.numeric.active=true;f.numeric.min=min;f.numeric.max=max;f.numeric.value=value;f.numeric.prompt_utf8="Quantity: ";
    return f;
}
inline GameFrame text_frame(int revision,const std::string &mode,const std::string &input,const std::string &title="Name") {
    GameFrame f;f.menu_open=true;f.cells.emplace_back();
    auto &m=f.modern;m.version=5;m.kind="dialog";m.stage="text";m.title=title;m.input_label="Name";m.revision=revision;m.language="en";
    m.text_edit_mode=mode;m.input=input;m.input_caret=int(input.size());m.input_ready=true;
    return f;
}
//! 決定を押して、送られた数を返す（送られなければ -1）。
inline int confirmed_number(ModernStoreUi &ui) {
    presentation::InputEventsMessage out;ui.handle(CursorNav::Confirm,out);
    for(const auto &e:out.events)if(e.action=="number")return e.number;
    return -1;
}
struct KeyCount {int forward{0},backspace{0},del{0},other{0};};
//! たまっている入力を取り出して、キーの種類ごとに数える。
inline KeyCount keys_sent(ModernStoreUi &ui) {
    presentation::InputEventsMessage out;ui.advance(out);KeyCount k;
    for(const auto &e:out.events) {
        if(e.e=="key"&&e.ctrl&&e.chr=="f")++k.forward;
        else if(e.e=="key"&&e.name=="backspace")++k.backspace;
        else if(e.e=="key"&&e.name=="delete")++k.del;
        else ++k.other;
    }
    return k;
}
}
inline int run_text_number_input_checks() {
    using namespace text_number_check;
    int checks=0,failures=0;
    const auto check=[&](bool okay,const std::string &name){++checks;if(!okay){++failures;std::fprintf(stderr,"FAIL: %s\n",name.c_str());}};
    // ---- 修-06：数の欄 ----
    {
        ModernStoreUi ui;ui.sync(number_frame(1,1,255,0),true);
        const int n=confirmed_number(ui);
        check(n==1,"number: an initial value below the minimum is raised to it (got "+std::to_string(n)+")");
    }
    {
        ModernStoreUi ui;ui.sync(number_frame(1,0,9999,20000),true);
        const int n=confirmed_number(ui);
        check(n==9999,"number: an initial value above the maximum is lowered to it (got "+std::to_string(n)+")");
    }
    {
        ModernStoreUi ui;ui.sync(number_frame(1,1,255,1),true);
        ui.text("999");
        const int n=confirmed_number(ui);
        check(n==255,"number: typing 999 with maximum 255 sticks to 255 (got "+std::to_string(n)+")");
    }
    {
        ModernStoreUi ui;ui.sync(number_frame(1,0,9999,0),true);
        ui.text("1");ui.text("2");ui.text("3");ui.text("4");ui.text("5");
        const int n=confirmed_number(ui);
        check(n==9999,"number: typing 12345 digit by digit with maximum 9999 sticks to 9999 (got "+std::to_string(n)+")");
    }
    {
        ModernStoreUi ui;ui.sync(number_frame(1,1,255,1),true);
        ui.text("1");ui.text("2");ui.text("3");
        const int n=confirmed_number(ui);
        check(n==123,"number: typing within the maximum is unchanged (got "+std::to_string(n)+")");
    }
    {
        ModernStoreUi ui;ui.sync(number_frame(1,1,255,1),true);
        ui.text("999");ui.backspace();
        const int n=confirmed_number(ui);
        check(n==25,"number: Backspace after sticking to the maximum removes one digit (got "+std::to_string(n)+")");
    }
    {
        ModernStoreUi ui;ui.sync(number_frame(1,1,255,1),true);
        ui.text("0");
        const int n=confirmed_number(ui);
        check(n==-1,"number: a value below the minimum is still not sent (got "+std::to_string(n)+")");
    }
    // ---- 修-58：文字入力の欄 ----
    {
        ModernStoreUi ui;ui.sync(text_frame(1,"basic","Hello"),true);
        ui.backspace();auto k=keys_sent(ui);
        check(k.forward==5&&k.backspace==5&&k.other==0,"basic: the first Backspace clears the whole default (forward "+std::to_string(k.forward)+", backspace "+std::to_string(k.backspace)+")");
        ui.backspace();k=keys_sent(ui);
        check(k.forward==0&&k.backspace==1&&k.other==0,"basic: the second Backspace removes one character (backspace "+std::to_string(k.backspace)+")");
    }
    {
        ModernStoreUi ui;ui.sync(text_frame(1,"basic","Hello"),true);
        ui.edit_text_key(127);auto k=keys_sent(ui);
        check(k.forward==5&&k.backspace==5&&k.del==0,"basic: the first Delete also clears the whole default");
        ui.edit_text_key(127);k=keys_sent(ui);
        check(k.del==1&&k.forward==0&&k.backspace==0,"basic: the second Delete removes one character");
    }
    {
        ModernStoreUi ui;ui.sync(text_frame(1,"basic","\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86"),true);
        ui.backspace();const auto k=keys_sent(ui);
        check(k.forward==3&&k.backspace==3,"basic: a multi-byte default counts characters, not bytes (forward "+std::to_string(k.forward)+")");
    }
    {
        ModernStoreUi ui;ui.sync(text_frame(1,"basic","Hello"),true);
        ui.text("x");keys_sent(ui);
        ui.backspace();const auto k=keys_sent(ui);
        check(k.forward==0&&k.backspace==1,"basic: after typing, Backspace removes one character");
    }
    {
        ModernStoreUi ui;ui.sync(text_frame(1,"basic","Hello"),true);
        ui.edit_text_boundary(true);keys_sent(ui);
        ui.backspace();const auto k=keys_sent(ui);
        check(k.forward==0&&k.backspace==1,"basic: after moving to the end, Backspace removes one character");
    }
    {
        ModernStoreUi ui;ui.sync(text_frame(1,"basic","Hello"),true);
        ui.backspace();keys_sent(ui);
        ui.sync(text_frame(2,"basic","Hello"),true);ui.backspace();
        const auto same=keys_sent(ui);
        check(same.forward==0&&same.backspace==1,"basic: the same field refreshed stays edited");
        ui.sync(text_frame(3,"basic","Ab","Other name"),true);ui.backspace();
        const auto fresh=keys_sent(ui);
        check(fresh.forward==2&&fresh.backspace==2,"basic: a new field is fresh again (forward "+std::to_string(fresh.forward)+")");
    }
    for(const char *mode:{"full","append"}) {
        ModernStoreUi ui;ui.sync(text_frame(1,mode,"Hello"),true);
        ui.backspace();const auto k=keys_sent(ui);
        check(k.forward==0&&k.backspace==1&&k.other==0,std::string(mode)+": the first Backspace stays one character");
    }
    {
        ModernStoreUi ui;ui.sync(text_frame(1,"full","Hello"),true);
        ui.edit_text_key(127);const auto k=keys_sent(ui);
        check(k.del==1&&k.forward==0&&k.backspace==0,"full: the first Delete stays one character");
    }
    std::fprintf(stderr,"[text-number-input-check] %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
}
