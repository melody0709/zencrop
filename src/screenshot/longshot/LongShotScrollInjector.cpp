#include "LongShotScrollInjector.h"

namespace longshot {

LONG ScrollDeltaForDirection(bool vertical, bool forward, int units) {
    if (units <= 0) return 0;

    // Vertical and horizontal wheel directions are intentionally asymmetric:
    //   WHEEL:  forward ? -units : +units
    //   HWHEEL: forward ? +units : -units
    const bool positive = vertical ? !forward : forward;
    return positive ? static_cast<LONG>(units) : -static_cast<LONG>(units);
}

void SimulateMouseScroll(bool vertical, bool forward, int units) {
    const LONG delta = ScrollDeltaForDirection(vertical, forward, units);
    if (delta == 0) return;

    // Windows WHEEL_DELTA is 120 per notch; this injector passes raw tier units
    // as mouseData for SendInput (25/35/45/55/65).
    const DWORD flags = vertical ? MOUSEEVENTF_WHEEL : MOUSEEVENTF_HWHEEL;

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    input.mi.mouseData = static_cast<DWORD>(delta);
    SendInput(1, &input, sizeof(INPUT));
}

} // namespace longshot
