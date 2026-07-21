// Recomp-ABI shim so MIPS patches can call into the native bridge.
#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "arena_bridge.h"
#include <cstring>
#include <cstdint>

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

/* A1.2b spawn spike: clone gObjects[0] (the player, the only resident bomber
 * model) into free slot 2, exactly once per launch. Done natively via a raw
 * byte copy in rdram (both regions are 4-aligned, so byte order is preserved)
 * to avoid a patch-side 336-byte struct copy, which at -O0 would emit an
 * unresolved memcpy. gObjects @ 0x80154150; stride 336 confirmed by the dump. */
extern "C" void arena_export_spawn_clone_once(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    static bool done = false;
    if (done) return;
    done = true;
    const uint32_t OBJ_BASE = 0x00154150u;   /* 0x80154150 - 0x80000000 */
    const uint32_t STRIDE   = 336u;          /* sizeof(ObjectStruct) */
    std::memcpy(rdram + OBJ_BASE + 2u * STRIDE, rdram + OBJ_BASE, STRIDE);
}

extern "C" void arena_export_bomber_off_x(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_get_bomber_off_x(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_bomber_off_z(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_get_bomber_off_z(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_bomber_yaw(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_get_bomber_yaw(_arg<0, int>(rdram, ctx)));
}

/* A1.2b placeholder actors: clone the door (slot 14, a simple static resident
 * object) into free slots 2,3,4 for players 1-3, exactly once. Raw byte copy in
 * rdram (4-aligned regions -> byte order preserved). gObjects @ 0x80154150,
 * stride 336 confirmed by the dump; door confirmed resident at slot 14. */
extern "C" void arena_export_spawn_placeholders_once(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    static bool done = false;
    if (done) return;
    done = true;
    const uint32_t BASE = 0x00154150u, STRIDE = 336u;
    const uint32_t SRC  = BASE + 15u * STRIDE;   /* plate at slot 15 (inert prop) */
    for (uint32_t s = 2u; s <= 4u; ++s)
        std::memcpy(rdram + BASE + s * STRIDE, rdram + SRC, STRIDE);
}

/* A1.2b puppet exports. Floats cross the ABI as u32 bit patterns (args) or via
 * _return (returns); no float _arg is used (unsupported for arbitrary slots). */
extern "C" void arena_export_puppet_capture(uint8_t* rdram, recomp_context* ctx) {
    uint32_t bx = _arg<0, uint32_t>(rdram, ctx);
    uint32_t by = _arg<1, uint32_t>(rdram, ctx);
    uint32_t bz = _arg<2, uint32_t>(rdram, ctx);
    arena_puppet_capture(bx, by, bz);
}
extern "C" void arena_export_puppet_ready(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; _return(ctx, arena_puppet_ready());
}
extern "C" void arena_export_puppet_set_slot(uint8_t* rdram, recomp_context* ctx) {
    arena_puppet_set_slot(_arg<0, int>(rdram, ctx), _arg<1, int>(rdram, ctx));
}
extern "C" void arena_export_puppet_get_slot(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_puppet_get_slot(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_puppet_wx(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_puppet_wx(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_puppet_wy(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_puppet_wy(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_puppet_wz(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_puppet_wz(_arg<0, int>(rdram, ctx)));
}
extern "C" void arena_export_puppet_yaw(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, arena_puppet_yaw(_arg<0, int>(rdram, ctx)));
}
