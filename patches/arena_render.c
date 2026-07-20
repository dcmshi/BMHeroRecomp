#include "patches.h"
#include "misc_funcs.h"

#include <ultra64.h>
#include "types.h"
#include "variables.h"

/* Import the EXPORT (syms.ld / REGISTER_FUNC) names, not the internal C++ names. */
DECLARE_FUNC(s32,  arena_bridge_is_battle);
DECLARE_FUNC(void, arena_export_tick_input, s32 sx, s32 sy, s32 jump, s32 bomb);
DECLARE_FUNC(f32,  arena_export_player_x, s32 i);
DECLARE_FUNC(f32,  arena_export_player_y, s32 i);
DECLARE_FUNC(f32,  arena_export_player_z, s32 i);
DECLARE_FUNC(f32,  arena_export_player_yaw, s32 i);

extern void func_80024744(void);            /* original per-frame routine 2 */
extern void func_800821E0(void);            /* per-frame routine 1 (draw) */
extern void func_8001ECB8(void);
extern void func_80081D78(void);
extern void func_80000964(void);
extern void (*gDebugRoutine1)(void);
extern void (*gDebugRoutine2)(void);

/* Per-frame in-level: run the original routine-2 work, then in battle mode
 * drive our sim from the controller and puppet gPlayerObject from it. */
void arena_render_routine(void) {
    func_80024744();
    if (arena_bridge_is_battle() && gPlayerObject != NULL) {
        /* N64 stick (~+/-80) -> sim stick (+/-31); sim stick up = -Z */
        s32 sx = (s32)(gActiveContStickX * (31.0f / 80.0f));
        s32 sy = (s32)(gActiveContStickY * (31.0f / 80.0f));
        if (sx >  31) sx =  31;
        if (sx < -31) sx = -31;
        if (sy >  31) sy =  31;
        if (sy < -31) sy = -31;
        s32 jump = (gActiveContButton & CONT_A) ? 1 : 0;
        s32 bomb = (gActiveContButton & CONT_B) ? 1 : 0;
        arena_export_tick_input(sx, sy, jump, bomb);
        gPlayerObject->Pos.x = arena_export_player_x(0);
        gPlayerObject->Pos.y = arena_export_player_y(0);
        gPlayerObject->Pos.z = arena_export_player_z(0);
        gPlayerObject->Rot.y = arena_export_player_yaw(0);
    }
}

/* Level-enter setup: original body, but route per-frame routine 2 through
 * our wrapper so the puppet write runs every frame in-level. */
RECOMP_PATCH void func_800824A8(void) {
    func_8001ECB8();
    gDebugRoutine1 = &func_800821E0;
    gDebugRoutine2 = &arena_render_routine;   /* was &func_80024744 */
    func_80081D78();
    func_80000964();
}
