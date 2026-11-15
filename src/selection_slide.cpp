#include "stdafx.h"
#include "selection_slide.h"
#include <math.h>

// Fraction of the remaining distance a selection bar moves per 1/60 s
static const FLOAT SELECTION_SLIDE_FACTOR = 0.35f;
static const FLOAT SLIDE_FRAME_MS = 1000.0f / 60.0f;
// After a pause (another screen) the bar continues instead of jumping
static const DWORD SLIDE_MAX_STEP_MS = 50;

void SlideTowards(FLOAT &position, FLOAT target, DWORD &last_tick)
{
    // By time: slow frames would otherwise make the bar crawl
    const DWORD now = GetTickCount();
    DWORD elapsed = (last_tick == 0) ? (DWORD)SLIDE_FRAME_MS : now - last_tick;
    last_tick = now;
    if (elapsed > SLIDE_MAX_STEP_MS)
    {
        elapsed = SLIDE_MAX_STEP_MS;
    }

    const FLOAT remaining = target - position;
    if (remaining > -0.5f && remaining < 0.5f)
    {
        position = target;
        return;
    }

    // (1 - factor) of the distance is left after each 1/60 s
    const FLOAT left = (FLOAT)pow(1.0 - SELECTION_SLIDE_FACTOR, (double)elapsed / SLIDE_FRAME_MS);
    position = target - remaining * left;
}
