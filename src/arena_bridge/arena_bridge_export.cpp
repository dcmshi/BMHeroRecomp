// Recomp-ABI shim so MIPS patches can call into the native bridge.
#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "arena_bridge.h"

extern "C" void arena_bridge_is_battle(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return(ctx, arena_bridge_battle_active());
}

extern "C" void arena_export_tick_input(uint8_t* rdram, recomp_context* ctx) {
    int sx   = _arg<0, int>(rdram, ctx);
    int sy   = _arg<1, int>(rdram, ctx);
    int jump = _arg<2, int>(rdram, ctx);
    int bomb = _arg<3, int>(rdram, ctx);
    arena_bridge_tick_input(sx, sy, jump, bomb);
}
extern "C" void arena_export_player_x(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_get_player_x(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_player_y(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_get_player_y(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_player_z(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_get_player_z(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_player_yaw(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_get_player_yaw_deg(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_dbg_dump(uint8_t* rdram, recomp_context* ctx) {
    arena_dbg_dump(_arg<0, int>(rdram, ctx), _arg<1, int>(rdram, ctx), _arg<2, int>(rdram, ctx));
}
