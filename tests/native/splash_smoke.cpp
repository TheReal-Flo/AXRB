#include "splash.h"
#include <cstdio>
int main() {
    using axrb::host::detail::load_splash_pixels;
    const auto rgba = load_splash_pixels(128, false, false);
    const auto bgra = load_splash_pixels(128, true, false);
    const auto linear = load_splash_pixels(128, false, true);
    unsigned colored = 0, darker = 0;
    for (size_t i = 0; i < rgba.size(); i += 4) {
        if (rgba[i] != bgra[i+2] || rgba[i+2] != bgra[i] || rgba[i+1] != bgra[i+1] || rgba[i+3] != 255) return 1;
        if (rgba[i] > 20 || rgba[i+1] > 20 || rgba[i+2] > 20) ++colored;
        for (int c = 0; c < 3; ++c) {
            if (linear[i+c] > rgba[i+c]) return 2;
            if (linear[i+c] < rgba[i+c]) ++darker;
        }
    }
    if (colored < 8000 || darker < 1000) return 3;
    std::puts("Embedded logo decoding, channel order and color conversion passed");
}
