#include "../../host/src/menu_shortcut.h"
#include <cstdio>

int main() {
    using namespace axrb::protocol;
    axrb::host::MenuShortcut shortcut;
    ControllerInput hands[2]{};
    auto sample = [&](uint64_t time, uint32_t left, uint32_t right, bool rightActive = true) {
        hands[0] = {}; hands[1] = {};
        hands[0].active = 1; hands[1].active = rightActive;
        hands[0].buttons = left; hands[1].buttons = right;
        shortcut.apply(hands, time);
        return (hands[0].buttons & MenuClick) != 0;
    };
    auto require = [](bool okay) { if (!okay) std::fputs("Menu shortcut check failed\n", stderr); return okay; };
    if (!require(!sample(0, StickClick, 0))) return 1;
    if (!require(!sample(1000000000, StickClick, StickClick))) return 1;
    if (!require(!sample(1499999999, StickClick, StickClick))) return 1;
    if (!require(sample(1500000000, StickClick | PrimaryClick, StickClick))) return 1;
    if (!require(!(hands[0].buttons & StickClick) && !(hands[1].buttons & StickClick) &&
        (hands[0].buttons & PrimaryClick))) return 1;
    if (!require(sample(2000000000, StickClick, StickClick))) return 1;
    if (!require(!sample(2100000000, 0, StickClick))) return 1;
    if (!require(!sample(2200000000, StickClick, StickClick))) return 1;
    shortcut.reset(); // Focus loss must cancel a partially held chord.
    if (!require(!sample(3000000000, StickClick, StickClick))) return 1;
    if (!require(!sample(4000000000, StickClick, StickClick, false))) return 1;
    if (!require(!sample(5000000000, StickClick, StickClick))) return 1;
    if (!require(sample(5100000000, MenuClick, 0))) return 1; // Native menu preserved.
    return 0;
}
