#pragma once
#include "frame/targeting.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace presentation {
inline TargetingPoint targeting_direction_delta(int direction) {
    constexpr TargetingPoint directions[]={{0,0},{-1,1},{0,1},{1,1},{-1,0},{0,0},{1,0},{-1,-1},{0,-1},{1,-1}};
    return direction>=0&&direction<=9?directions[direction]:directions[0];
}
inline int targeting_nearest_direction(TargetingPoint from,TargetingPoint to) {
    const int dx=to.x-from.x,dy=to.y-from.y;if(!dx&&!dy)return 5;
    constexpr double eighth_turn=0.78539816339744830962;
    constexpr int directions[]={6,9,8,7,4,1,2,3};
    const int sector=(int(std::llround(std::atan2(-double(dy),double(dx))/eighth_turn))+8)%8;
    return directions[sector];
}
struct NativeTargetPriority {
    TargetingCandidate candidate;
    int distance{},ticks_until_opportunity{},processing_order{};
    bool eligible{};
};
inline std::vector<TargetingCandidate> targeting_order_candidates(std::vector<NativeTargetPriority> entries) {
    entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto &e){return !e.eligible;}),entries.end());
    std::stable_sort(entries.begin(),entries.end(),[](const auto &a,const auto &b){
        if(a.distance!=b.distance)return a.distance<b.distance;
        if(a.ticks_until_opportunity!=b.ticks_until_opportunity)return a.ticks_until_opportunity<b.ticks_until_opportunity;
        return a.processing_order<b.processing_order;
    });
    std::vector<TargetingCandidate> out;out.reserve(entries.size());for(auto &e:entries)out.push_back(std::move(e.candidate));return out;
}
}
