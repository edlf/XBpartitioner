#ifndef SELECTION_SLIDE_H_
#define SELECTION_SLIDE_H_

#include <xtl.h>

// Eases a selection bar towards its target; time based (last_tick: 0 at first)
void SlideTowards(FLOAT &position, FLOAT target, DWORD &last_tick);

#endif // SELECTION_SLIDE_H_
