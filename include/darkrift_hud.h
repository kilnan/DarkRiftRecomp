#ifndef DARKRIFT_HUD_H
#define DARKRIFT_HUD_H

#include <cstdint>

namespace darkrift {
    // Writes a copy of a 2D sprite display list with its texture rectangles turned into
    // textured triangles (workaround for missing HUD rectangles in RT64). Returns the copy's
    // physical address, or 0 if the list has no texture rectangles.
    uint32_t rewrite_2d_list(uint8_t* rdram, uint32_t list);
}

#endif
