#include "bg.h"

namespace bg {

#if BG_HAS_240x135
extern const Set set_240x135;
#endif
#if BG_HAS_320x170
extern const Set set_320x170;
#endif
#if BG_HAS_320x240
extern const Set set_320x240;
#endif

const Set *find(int width, int height) {
    static const Set *const sets[] = {
#if BG_HAS_240x135
        &set_240x135,
#endif
#if BG_HAS_320x170
        &set_320x170,
#endif
#if BG_HAS_320x240
        &set_320x240,
#endif
        nullptr,
    };
    for (const Set *s : sets)
        if (s && s->width == width && s->height == height) return s;
    return nullptr;
}

}  // namespace bg
