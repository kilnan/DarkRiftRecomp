// Turbo3D -> Fast3D translation for Dark Rift.
//
// Dark Rift draws its fighters with SGI's Turbo3D microcode (gspDarkRift3D), which RT64
// does not implement. A Turbo3D task is not a display list: its data is an array of
// "objects" (gtGfx in libultra's gt.h, `Batch` in the decompilation), each pointing to an
// optional global state, an object state with its own transform, a vertex array and a
// triangle list. This file walks that array and writes an equivalent Fast3D display list
// into spare RDRAM, which RT64 then renders with the game's Fast3D microcode.
//
// Written from the public libultra gt.h layout and Nintendo's Turbo3D documentation.

#include <cstdint>
#include <cstdio>
#include <array>

#include "darkrift_turbo3d.h"

namespace {

// RDRAM as laid out by N64Recomp: 32-bit words in host order, so sub-word accesses are
// address-swizzled.
struct Rdram {
    uint8_t* base;

    uint32_t u32(uint32_t addr) const { return *reinterpret_cast<const uint32_t*>(base + addr); }
    uint16_t u16(uint32_t addr) const { return *reinterpret_cast<const uint16_t*>(base + (addr ^ 2)); }
    uint8_t u8(uint32_t addr) const { return base[addr ^ 3]; }
    void put_u32(uint32_t addr, uint32_t v) { *reinterpret_cast<uint32_t*>(base + addr) = v; }
};

// Scratch RDRAM for the generated display list and helpers. It lives in the extended RAM
// region the recompiled runtime provides above the original 8 MB.
constexpr uint32_t identity_mtx_addr = 0x00BFFF00;
constexpr uint32_t dl_start = 0x00C00000;
constexpr uint32_t dl_end = 0x00E00000;

// Fast3D (F3D) opcodes and constants used below.
constexpr uint32_t G_SPNOOP = 0x00;
constexpr uint32_t G_MTX = 0x01;
constexpr uint32_t G_MOVEMEM = 0x03;
constexpr uint32_t G_VTX = 0x04;
constexpr uint32_t G_TRI1 = 0xBF;
constexpr uint32_t G_TEXTURE = 0xBB;
constexpr uint32_t G_MOVEWORD = 0xBC;
constexpr uint32_t G_SETGEOMETRYMODE = 0xB7;
constexpr uint32_t G_CLEARGEOMETRYMODE = 0xB6;
constexpr uint32_t G_ENDDL = 0xB8;
constexpr uint32_t G_MTX_MODELVIEW = 0x00, G_MTX_PROJECTION = 0x01, G_MTX_LOAD = 0x02;
constexpr uint32_t G_MV_VIEWPORT = 0x80;
constexpr uint32_t G_MW_SEGMENT = 0x06, G_MW_PERSPNORM = 0x0E;
constexpr uint32_t G_SHADE = 0x00000004;

// Turbo3D (gt.h) layout.
constexpr uint32_t GT_TEXTURE = 0x00000002;
constexpr uint8_t GT_FLAG_NOMTX = 0x01, GT_FLAG_NO_XFM = 0x02, GT_FLAG_XFM_ONLY = 0x04;
constexpr uint32_t GLOB_PERSPNORM = 0, GLOB_OTHERMODE = 8, GLOB_SEGBASES = 16, GLOB_VIEWPORT = 80, GLOB_RDPCMDS = 96;
constexpr uint32_t STATE_RENDER = 0, STATE_TEXTURE = 4, STATE_VTXCOUNT = 8, STATE_VTXV0 = 9, STATE_TRICOUNT = 10,
                   STATE_FLAG = 11, STATE_RDPCMDS = 12, STATE_OTHERMODE = 16, STATE_TRANSFORM = 24;
constexpr uint32_t OBJ_SIZE = 16, VTX_SIZE = 16;
constexpr int VTX_CACHE = 64;

class Translator {
public:
    explicit Translator(uint8_t* rdram) : mem{ rdram } {}

    uint32_t run(uint32_t data_ptr, uint32_t data_size) {
        cursor = dl_start;
        write_identity();
        emit(cmd(G_MTX, (G_MTX_MODELVIEW | G_MTX_LOAD) << 16 | 64), identity_mtx_addr);

        const uint32_t count = data_size / OBJ_SIZE;
        for (uint32_t i = 0; i < count && !overflow; i++) {
            const uint32_t obj = phys(data_ptr) + i * OBJ_SIZE;
            const uint32_t gstate = mem.u32(obj + 0);
            const uint32_t state = mem.u32(obj + 4);
            if (state == 0) {
                break; // a NULL state ends object processing
            }
            if (gstate != 0) {
                global_state(resolve(gstate));
            }
            object(resolve(state), mem.u32(obj + 8), mem.u32(obj + 12));
        }

        emit(cmd(G_ENDDL, 0), 0);
        if (overflow) {
            fprintf(stderr, "[darkrift] Turbo3D translation overflowed its buffer, frame truncated\n");
        }
        return dl_start;
    }

private:
    Rdram mem;
    uint32_t cursor = 0;
    bool overflow = false;
    std::array<uint32_t, 16> segments{};
    // Per cache slot: source vertex address and the matrix it is transformed with.
    std::array<uint32_t, VTX_CACHE> slot_vtx{};
    std::array<uint32_t, VTX_CACHE> slot_mtx{};
    uint32_t current_mtx = 0;
    uint32_t loaded_mtx = 0;

    static uint32_t cmd(uint32_t op, uint32_t low) { return (op << 24) | (low & 0xFFFFFF); }
    static uint32_t phys(uint32_t addr) { return addr & 0x00FFFFFF; }

    // Turbo3D pointers are segment addresses; segment 0 is physical memory.
    uint32_t resolve(uint32_t addr) const {
        const uint32_t seg = (addr >> 24) & 0x0F;
        return phys(segments[seg] + (addr & 0x00FFFFFF));
    }

    void emit(uint32_t w0, uint32_t w1) {
        if (cursor + 16 > dl_end) { // keep room for the final G_ENDDL
            overflow = true;
            return;
        }
        mem.put_u32(cursor, w0);
        mem.put_u32(cursor + 4, w1);
        cursor += 8;
    }

    void write_identity() {
        static constexpr uint32_t ident[16] = { 0x00010000, 0, 0x00000001, 0, 0, 0x00010000, 0, 0x00000001, 0, 0, 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < 16; i++) {
            mem.put_u32(identity_mtx_addr + i * 4, ident[i]);
        }
    }

    // Copies a Turbo3D RDP command block, which ends with an all-zero word pair
    // (gDPEndDisplayList is a no-op in Turbo3D, not a G_ENDDL).
    void copy_rdp_block(uint32_t addr) {
        for (int n = 0; n < 4096 && !overflow; n++) {
            const uint32_t w0 = mem.u32(addr), w1 = mem.u32(addr + 4);
            if (w0 == 0 && w1 == 0) {
                return;
            }
            emit(w0, w1);
            addr += 8;
        }
    }

    void global_state(uint32_t g) {
        for (uint32_t seg = 1; seg < 16; seg++) {
            segments[seg] = mem.u32(g + GLOB_SEGBASES + seg * 4);
            emit(cmd(G_MOVEWORD, (seg * 4) << 8 | G_MW_SEGMENT), segments[seg]);
        }
        emit(cmd(G_MOVEWORD, G_MW_PERSPNORM), mem.u16(g + GLOB_PERSPNORM));
        emit(cmd(G_MOVEMEM, G_MV_VIEWPORT << 16 | 16), g + GLOB_VIEWPORT);
        emit(mem.u32(g + GLOB_OTHERMODE), mem.u32(g + GLOB_OTHERMODE + 4));
        if (const uint32_t rdp = mem.u32(g + GLOB_RDPCMDS)) {
            copy_rdp_block(resolve(rdp));
        }
    }

    void object(uint32_t s, uint32_t vtxp, uint32_t trip) {
        const uint8_t flag = mem.u8(s + STATE_FLAG);
        if (flag & (GT_FLAG_NO_XFM | GT_FLAG_XFM_ONLY)) {
            static bool warned = false;
            if (!warned) {
                fprintf(stderr, "[darkrift] Turbo3D object with flags 0x%02X not supported yet, skipped\n", flag);
                warned = true;
            }
            return;
        }

        // Render and texture state.
        const uint32_t render = mem.u32(s + STATE_RENDER);
        emit(cmd(G_CLEARGEOMETRYMODE, 0), 0xFFFFFFFF);
        emit(cmd(G_SETGEOMETRYMODE, 0), render | G_SHADE);
        const uint32_t tile = mem.u32(s + STATE_TEXTURE) & 7;
        emit(cmd(G_TEXTURE, (tile << 8) | ((render & GT_TEXTURE) ? 1 : 0)), 0xFFFFFFFF);
        emit(mem.u32(s + STATE_OTHERMODE), mem.u32(s + STATE_OTHERMODE + 4));
        if (const uint32_t rdp = mem.u32(s + STATE_RDPCMDS)) {
            copy_rdp_block(resolve(rdp));
        }

        if (!(flag & GT_FLAG_NOMTX)) {
            current_mtx = s + STATE_TRANSFORM;
        }

        // Load this object's vertices into the emulated 64-entry cache.
        const uint32_t vtx_count = mem.u8(s + STATE_VTXCOUNT);
        const uint32_t vtx_v0 = mem.u8(s + STATE_VTXV0);
        if (vtxp != 0) {
            const uint32_t src = resolve(vtxp);
            for (uint32_t i = 0; i < vtx_count && vtx_v0 + i < VTX_CACHE; i++) {
                slot_vtx[vtx_v0 + i] = src + i * VTX_SIZE;
                slot_mtx[vtx_v0 + i] = current_mtx;
            }
        }

        // Draw the triangles, loading each vertex with the matrix it was cached with.
        const uint32_t tri_count = mem.u8(s + STATE_TRICOUNT);
        if (trip == 0 || tri_count == 0) {
            return;
        }
        const uint32_t tris = resolve(trip);
        for (uint32_t t = 0; t < tri_count && !overflow; t++) {
            const uint32_t tri = tris + t * 4;
            const uint8_t idx[3] = { mem.u8(tri + 0), mem.u8(tri + 1), mem.u8(tri + 2) };
            const uint8_t flat = mem.u8(tri + 3);
            bool valid = true;
            for (int k = 0; k < 3; k++) {
                const uint8_t v = idx[k];
                if (v >= VTX_CACHE || slot_vtx[v] == 0 || slot_mtx[v] == 0) {
                    valid = false;
                    break;
                }
                if (slot_mtx[v] != loaded_mtx) {
                    emit(cmd(G_MTX, (G_MTX_PROJECTION | G_MTX_LOAD) << 16 | 64), slot_mtx[v]);
                    loaded_mtx = slot_mtx[v];
                }
                emit(cmd(G_VTX, (k << 16) | VTX_SIZE), slot_vtx[v]);
            }
            if (valid) {
                emit(cmd(G_TRI1, 0), (uint32_t(flat & 3) << 24) | (0 << 16) | (10 << 8) | 20);
            }
        }
    }
};

} // namespace

uint32_t darkrift::translate_turbo3d(uint8_t* rdram, uint32_t data_ptr, uint32_t data_size) {
    Translator translator{ rdram };
    return translator.run(data_ptr, data_size);
}
