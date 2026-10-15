// Observe the core's other whole-screen erase operation. Partial erases keep
// the current ownership; they do not turn a floating menu into a full screen.
#define clear_from core_clear_from
#include "term/screen-processor.cpp"
#undef clear_from
#include "term/term_capture_state.h"
void clear_from(int row){core_clear_from(row);if(row==0)presentation::term_capture_clear(game_term);}
