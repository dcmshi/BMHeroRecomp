// Recomp-ABI shim so MIPS patches can call into the native bridge.
#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "arena_bridge.h"

extern "C" void arena_bridge_is_battle(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return(ctx, arena_bridge_battle_active());
}
