#define SDL_MAIN_HANDLED
#include "app/test_keyboard.h"
#include <iostream>
#include <cassert>
int main(){
 SDL_Init(0);std::vector<SDL_Event> events;std::string error;int count=0;
 assert(hd2d::compile_test_key(R"({"mouse_button":"left","x":800,"y":152})",events,error));
 assert(events.size()==2&&events[0].type==SDL_MOUSEBUTTONDOWN&&events[1].type==SDL_MOUSEBUTTONUP&&events[0].button.x==800&&events[0].button.y==152&&events[0].button.button==SDL_BUTTON_LEFT);++count;
 for(const auto *b:{"right","middle"}){assert(hd2d::compile_test_key(std::string("{\"mouse_button\":\"")+b+"\",\"x\":0,\"y\":32767}",events,error));assert(events.size()==2);++count;}
 for(const auto *bad:{R"({"mouse_button":"left","x":-1,"y":0})",R"({"mouse_button":"left","x":32768,"y":0})",R"({"mouse_button":"left","x":1.5,"y":0})",R"({"mouse_button":"left","x":0})",R"({"mouse_button":"left","x":0,"y":0,"key":"Return"})",R"({"mouse_button":"invalid","x":0,"y":0})",R"({"key":"Return","x":0})",R"({"probe":true,"x":0})",R"({"mouse_button":"left","x":true,"y":0})"}){assert(!hd2d::compile_test_key(bad,events,error));assert(events.empty());++count;}
 assert(hd2d::compile_test_key(R"({"key":"F2"})",events,error)&&events[0].key.keysym.sym==SDLK_F2);++count;
 assert(hd2d::compile_test_key(R"({"key":"x","ctrl":true})",events,error)&&events[0].key.keysym.mod==KMOD_LCTRL);++count;
 assert(hd2d::compile_test_key(R"({"touch":true,"x":0.25,"y":0.75})",events,error)&&events.size()==2&&events[0].type==SDL_FINGERDOWN&&events[1].type==SDL_FINGERUP&&events[0].tfinger.y==.75f);++count;
 assert(hd2d::compile_test_key(R"({"touch":true,"x":0.5,"y":0.5,"phase":"down","finger":3})",events,error)&&events.size()==1&&events[0].type==SDL_FINGERDOWN&&events[0].tfinger.fingerId==3&&events[0].tfinger.pressure==1);++count;
 assert(hd2d::compile_test_key(R"({"touch":true,"x":0.5,"y":0.5,"phase":"motion"})",events,error)&&events.size()==1&&events[0].type==SDL_FINGERMOTION&&events[0].tfinger.fingerId==1);++count;
 assert(hd2d::compile_test_key(R"({"id":"t","touch":true,"x":0.5,"y":0.5,"phase":"up","finger":2})",events,error)&&events.size()==1&&events[0].type==SDL_FINGERUP&&events[0].tfinger.fingerId==2&&events[0].tfinger.pressure==0);++count;
 for(const auto *bad:{R"({"touch":true,"x":0.5,"y":0.5,"phase":"long"})",R"({"touch":true,"x":0.5,"y":0.5,"phase":1})",R"({"touch":true,"x":0.5,"y":0.5,"finger":1.5})",R"({"touch":true,"x":0.5,"y":0.5,"finger":-1})",R"({"phase":"down","x":0.5,"y":0.5})",R"({"touch":true,"x":0.5,"y":0.5,"phase":"down","key":"Return"})"}){assert(!hd2d::compile_test_key(bad,events,error)&&events.empty());++count;}
 assert(hd2d::compile_test_key(R"({"pad_button":"leftshoulder","pressed":false})",events,error)&&events.size()==1&&events[0].type==SDL_CONTROLLERBUTTONUP&&events[0].cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER);++count;
 for(const auto *bad:{R"({"touch":true,"x":1.1,"y":0})",R"({"touch":true,"x":true,"y":0})",R"({"touch":false,"x":0,"y":0})",R"({"pad_button":"invalid","pressed":true})",R"({"pad_button":"a","pressed":true,"key":"Return"})",R"({"pad_button":"a"})"}){assert(!hd2d::compile_test_key(bad,events,error)&&events.empty());++count;}
 std::cout<<"PASS "<<count<<" mouse and existing keyboard input checks\n";SDL_Quit();
}
