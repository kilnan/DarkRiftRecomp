// Dark Rift specific definitions that the shared recomp frontend expects.
#include "zelda_debug.h"
#include "recomp.h"

namespace zelda64 {
    // Dark Rift has no scenes to warp to, but the shared debug menu indexes the
    // first entry unconditionally, so provide a single placeholder.
    std::vector<AreaWarps> game_warps{
        { "Dark Rift", { { 0, "None", { "None" } } } },
    };
}

// __osGetCause reads the COP0 Cause register. The game only calls it from its
// debug/exception printing routines, so report "no cause".
extern "C" void __osGetCause_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}
