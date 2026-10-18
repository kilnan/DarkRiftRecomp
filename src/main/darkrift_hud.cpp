// Workaround for Dark Rift's missing 2D overlay (health bars, names, timer) in RT64.
//
// The game draws its 2D sprites as copy-mode texture rectangles in a separate display list
// (gfxBg in the decompilation) that the main list calls before the 3D scene. During fights
// RT64 produces no pixels for those rectangles (see the README). Triangles drawn in the same
// place do render, so this rewrites the list: every TEXRECT becomes two textured triangles
// with the same screen area and texture coordinates, drawn in 1-cycle mode through an
// orthographic 320x240 projection. Texture loads and every other command are copied as they
// are. This is a workaround for an RT64 issue, not N64 behavior, and should go away once
// the underlying problem is fixed.

#include <cstdint>

#include "darkrift_hud.h"

namespace {

// Scratch RDRAM above the original 8 MB, below the Turbo3D translator's area.
constexpr uint32_t list_start = 0x00BF0000;
constexpr uint32_t list_end = 0x00BF8000;
constexpr uint32_t vtx_start = 0x00BF8000;
constexpr uint32_t vtx_end = 0x00BFE000;
constexpr uint32_t ortho_mtx_addr = 0x00BFF000;
constexpr uint32_t ident_mtx_addr = 0x00BFF040;
constexpr uint32_t viewport_addr = 0x00BFF080;

constexpr uint32_t max_input_commands = 0x2000;

struct Writer {
    uint32_t* words;
    uint32_t cursor = list_start;
    uint32_t vtx_cursor = vtx_start;
    bool overflow = false;

    void emit(uint32_t w0, uint32_t w1) {
        if (cursor + 8 > list_end - 8) {
            overflow = true;
            return;
        }
        words[cursor / 4] = w0;
        words[cursor / 4 + 1] = w1;
        cursor += 8;
    }

    // F3D Vtx: x, y, z, flag, s, t (s10.5), rgba.
    uint32_t vertices(const int16_t (&v)[4][4]) {
        if (vtx_cursor + 64 > vtx_end) {
            overflow = true;
            return 0;
        }
        const uint32_t addr = vtx_cursor;
        for (int i = 0; i < 4; i++) {
            uint32_t* p = words + (addr + i * 16) / 4;
            p[0] = (uint32_t(uint16_t(v[i][0])) << 16) | uint16_t(v[i][1]);
            p[1] = 0;
            p[2] = (uint32_t(uint16_t(v[i][2])) << 16) | uint16_t(v[i][3]);
            p[3] = 0xFFFFFFFF;
        }
        vtx_cursor += 64;
        return addr;
    }
};

// Writes a libultra Mtx (s15.16 fixed point, integer parts then fractions).
void write_mtx(uint32_t* words, uint32_t addr, const double (&m)[4][4]) {
    int32_t fixed[16];
    for (int i = 0; i < 16; i++) {
        fixed[i] = int32_t(m[i / 4][i % 4] * 65536.0 + (m[i / 4][i % 4] >= 0 ? 0.5 : -0.5));
    }
    for (int k = 0; k < 8; k++) {
        const uint32_t a = uint32_t(fixed[2 * k]), b = uint32_t(fixed[2 * k + 1]);
        words[(addr + k * 4) / 4] = (a & 0xFFFF0000) | (b >> 16);
        words[(addr + 32 + k * 4) / 4] = (a << 16) | (b & 0xFFFF);
    }
}

void write_constants(uint32_t* words) {
    // Screen pixels -> clip space for a 320x240 viewport. W = 160 keeps the coefficients integral
    // except for the Y aspect factor.
    const double ortho[4][4] = {
        { 1, 0, 0, 0 },
        { 0, -4.0 / 3.0, 0, 0 },
        { 0, 0, 1, 0 },
        { -160, 160, 0, 160 },
    };
    const double ident[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
    write_mtx(words, ortho_mtx_addr, ortho);
    write_mtx(words, ident_mtx_addr, ident);
    // Vp: scale and translation in quarter pixels.
    words[viewport_addr / 4 + 0] = (640u << 16) | 480u;
    words[viewport_addr / 4 + 1] = 511u << 16;
    words[viewport_addr / 4 + 2] = (640u << 16) | 480u;
    words[viewport_addr / 4 + 3] = 511u << 16;
}

} // namespace

uint32_t darkrift::rewrite_2d_list(uint8_t* rdram, uint32_t list) {
    uint32_t* words = reinterpret_cast<uint32_t*>(rdram);
    list &= 0x3FFFF8;

    // Only rewrite lists that draw texture rectangles.
    bool has_texrect = false;
    for (uint32_t i = 0; i < max_input_commands; i++) {
        const uint32_t op = words[(list + i * 8) / 4] >> 24;
        if (op == 0xB8) {
            break;
        }
        has_texrect |= (op == 0xE4);
    }
    if (!has_texrect) {
        return 0;
    }

    write_constants(words);
    Writer out{ words };

    // Triangle setup: standard viewport, orthographic projection, no geometry modes, texturing on.
    out.emit(0x03800010, viewport_addr);  // G_MOVEMEM viewport
    out.emit(0x01030040, ortho_mtx_addr); // G_MTX projection | load
    out.emit(0x01020040, ident_mtx_addr); // G_MTX modelview | load
    out.emit(0xB6000000, 0xFFFFFFFF);     // G_CLEARGEOMETRYMODE all
    out.emit(0xBB000001, 0xFFFFFFFF);     // G_TEXTURE tile 0, on, scale 1
    out.emit(0xBA001402, 0x00000000);     // cycle type: 1 cycle
    out.emit(0xBA000C02, 0x00000000);     // texture filter: point
    out.emit(0xBA001301, 0x00080000);     // texture perspective on (W is 160, not 1)
    out.emit(0xFC121824, 0xFF33FFFF);     // combine: TEXEL0 color and alpha
    out.emit(0xB900031D, 0x0F0A4000);     // render mode: opaque surface
    out.emit(0xB9000002, 0x00000001);     // alpha compare: threshold (blend color alpha is set by the caller)

    for (uint32_t i = 0; i < max_input_commands && !out.overflow; i++) {
        const uint32_t a = list + i * 8;
        const uint32_t w0 = words[a / 4], w1 = words[a / 4 + 1];
        const uint32_t op = w0 >> 24;
        if (op == 0xB8) {
            break;
        }
        if (op != 0xE4) {
            out.emit(w0, w1);
            continue;
        }

        // TEXRECT, followed by RDPHALF commands carrying (s, t) and (dsdx, dtdy). The game sets
        // copy mode before this list, so the rectangle covers [ul, lr] inclusive and dsdx is 4x.
        const uint32_t st = words[(a + 8) / 4 + 1];
        const uint32_t steps = words[(a + 16) / 4 + 1];
        i += 2;
        const int xl = int((w1 >> 12) & 0xFFF) >> 2, yl = int(w1 & 0xFFF) >> 2;
        const int xh = int((w0 >> 12) & 0xFFF) >> 2, yh = int(w0 & 0xFFF) >> 2;
        const int width = xh - xl + 1, height = yh - yl + 1;
        const int s0 = int16_t(st >> 16), t0 = int16_t(st & 0xFFFF);
        const double s_per_px = int16_t(steps >> 16) / 4096.0, t_per_px = int16_t(steps & 0xFFFF) / 1024.0;
        const int s1 = s0 + int(width * s_per_px * 32.0 + 0.5);
        const int t1 = t0 + int(height * t_per_px * 32.0 + 0.5);
        const int16_t v[4][4] = {
            { int16_t(xl), int16_t(yl), int16_t(s0), int16_t(t0) },
            { int16_t(xl + width), int16_t(yl), int16_t(s1), int16_t(t0) },
            { int16_t(xl), int16_t(yl + height), int16_t(s0), int16_t(t1) },
            { int16_t(xl + width), int16_t(yl + height), int16_t(s1), int16_t(t1) },
        };
        const uint32_t vtx = out.vertices(v);
        if (vtx == 0) {
            break;
        }
        out.emit(0x04300040, vtx);                   // G_VTX 4 vertices at slot 0
        out.emit(0xBF000000, (0u << 16) | (10u << 8) | 20u); // G_TRI1 0 1 2
        out.emit(0xBF000000, (10u << 16) | (30u << 8) | 20u); // G_TRI1 1 3 2
    }

    // Leave the state as the caller's copy-mode setup had it.
    out.emit(0xBA001402, 0x00200000);  // cycle type: copy
    out.emit(0xBA000C02, 0x00002000);  // texture filter: bilinear
    out.emit(0xBA001301, 0x00000000);  // texture perspective off
    out.emit(0xFCFFFFFF, 0xFFFE793C);  // combine: as set by the copy-mode setup list
    out.emit(0xB900031D, 0x0F0A7008);  // render mode: as set by the copy-mode setup list
    out.emit(0xBB000000, 0xFFFFFFFF);  // G_TEXTURE off
    out.emit(0xB8000000, 0);
    return out.overflow ? 0 : list_start;
}
