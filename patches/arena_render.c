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
 * drive our sim from the controller and puppet the player's horizontal
 * position from it. We override only X/Z (from ArenaState, offset by the
 * player's current position so movement is relative and stays in-room) and
 * leave Y to the game so it keeps the bomberman grounded. */
/* A1.2b spike diagnostic (temporary): once, ~1.5s into battle, dump the object
 * table so we can see which slots are free (actionState==ACTION_NONE) and which
 * bomber/enemy models are already resident (drawable) in the Battle Room. */
static int  g_dbg_frames = 0;
static int  g_dbg_dumped = 0;
static void arena_dbg_dump_objects(void) {
    int i;
    recomp_printf("[arena_dbg] gPlayerObject slot=%d\n",
                  (int)(gPlayerObject - gObjects));
    for (i = 0; i < 16; i++) {
        recomp_printf("[arena_dbg] obj[%2d] objID=%d actionState=%d\n",
                      i, (int)gObjects[i].objID, (int)gObjects[i].actionState);
    }
}

void arena_render_routine(void) {
    func_80024744();
    if (arena_bridge_is_battle() && !g_dbg_dumped) {
        if (++g_dbg_frames >= 90) { arena_dbg_dump_objects(); g_dbg_dumped = 1; }
    }
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
        /* Move the player by our sim's per-frame velocity: add the change in
         * sim position (this frame vs last) to the player's live position, so
         * it moves per our physics without teleporting it out of the room. */
        gPlayerObject->Pos.x += arena_export_player_x(0);   /* getter returns dx */
        gPlayerObject->Pos.z += arena_export_player_z(0);   /* getter returns dz */
        gPlayerObject->Rot.y  = arena_export_player_yaw(0);
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
