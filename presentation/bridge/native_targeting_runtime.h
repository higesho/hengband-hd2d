#pragma once
#include "bridge/native_targeting.h"
#include <functional>
#include <string>
struct GameFrame;

namespace presentation {
NativeTargeting &native_targeting();
void configure_native_targeting(bool enabled,const std::string &language,
    std::function<std::string(const char *)> convert={});
void apply_native_targeting_frame(GameFrame &frame);
void apply_native_targeting_menu_frame(GameFrame &frame);
void prepare_native_targeting_menu_frame(GameFrame &frame);
}
