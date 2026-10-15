#pragma once
#include <unordered_map>
#include <vector>
namespace presentation {
struct TermCaptureState {bool full_screen{};std::vector<bool> saved;};
inline std::unordered_map<const void *,TermCaptureState> term_capture_states;
inline void term_capture_clear(const void *term){term_capture_states[term].full_screen=true;}
inline void term_capture_save(const void *term){auto &state=term_capture_states[term];state.saved.push_back(state.full_screen);state.full_screen=false;}
inline void term_capture_load(const void *term,bool all){
    auto &state=term_capture_states[term];if(state.saved.empty())return;
    if(all){state.full_screen=state.saved.front();state.saved.clear();}
    else {state.full_screen=state.saved.back();state.saved.pop_back();}
}
inline void term_capture_base(const void *term){term_capture_states.erase(term);}
inline bool term_capture_full_screen(const void *term){const auto found=term_capture_states.find(term);return found!=term_capture_states.end()&&found->second.full_screen;}
}
