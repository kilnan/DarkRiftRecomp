#ifndef DARKRIFT_TURBO3D_H
#define DARKRIFT_TURBO3D_H

#include <cstdint>

namespace darkrift {
    // Translates a Turbo3D task (array of gtGfx objects) into a Fast3D display list in
    // spare RDRAM and returns its physical address.
    uint32_t translate_turbo3d(uint8_t* rdram, uint32_t data_ptr, uint32_t data_size);
}

#endif
