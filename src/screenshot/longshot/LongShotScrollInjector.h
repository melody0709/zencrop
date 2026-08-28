#pragma once

#include "LongShotTypes.h"

namespace longshot {

// Injects vertical or horizontal scrolling. The default
// Forward scrolls down for WHEEL and right for HWHEEL, so numeric signs are
// opposite across the two axes.
LONG ScrollDeltaForDirection(bool vertical, bool forward, int units);
void SimulateMouseScroll(bool vertical, bool forward, int units);

} // namespace longshot
