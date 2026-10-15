// Observe logical screen ownership without editing the original terminal code.
// A physical redraw does not clear a logical screen and is deliberately untouched.
#define term_clear core_term_clear
#define term_save core_term_save
#define term_load core_term_load
#include "term/z-term.cpp"
#undef term_clear
#undef term_save
#undef term_load
#include "term/term_capture_state.h"

void term_clear(){core_term_clear();presentation::term_capture_clear(game_term);}
void term_save(){core_term_save();presentation::term_capture_save(game_term);}
void term_load(bool all){const bool saved=!game_term->mem_stack.empty();core_term_load(all);if(saved)presentation::term_capture_load(game_term,all);}
