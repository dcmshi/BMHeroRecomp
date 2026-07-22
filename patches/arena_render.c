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

/* A1.2b puppet exports (Task 1) */
DECLARE_FUNC(void, arena_export_puppet_capture, s32 bx, s32 by, s32 bz);
DECLARE_FUNC(s32,  arena_export_puppet_ready);
DECLARE_FUNC(void, arena_export_puppet_set_slot, s32 i, s32 slot);
DECLARE_FUNC(s32,  arena_export_puppet_get_slot, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wx, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wy, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wz, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_yaw, s32 i);
DECLARE_FUNC(void, arena_export_dbg_u32, s32 tag, s32 val);   /* temp evidence logger */

/* Game proper-spawn: scans gObjects[14..77], loads mesh from gFileArray[info->unk4]. */
extern s32 func_80027464(s32 count, struct ObjSpawnInfo* info, f32 x, f32 y, f32 z, f32 rotY);
/* Animation bind (Unk148 instance) — the piece the general spawn omits for
 * animated models. func_8001ABF4 is a function (resolves in patches); the
 * bomb's animation config table D_801163DC is an auto-named DATA symbol that
 * does NOT resolve via the patch reloc path, so we pass its address as a
 * literal (the recomp translates game addresses on deref inside the callee). */
struct UnkStruct8016C298_1;
extern void func_8001ABF4(s32 arg0, s32 arg1, s32 arg2, struct UnkStruct8016C298_1* arg3);
#define D_801163DC_ADDR ((struct UnkStruct8016C298_1*)0x801163DC)

/* Flip to 1 to re-enable the placeholder spawn experiment (see below). */
#define ARENA_SPAWN_TEST 1
/* BISECT: 0 = spawn 3 but park them (no per-frame positioning); 1 = position. */
#define ARENA_POSITION 1

extern void func_80024744(void);            /* original per-frame routine 2 (update) */
extern void func_800821E0(void);            /* original per-frame routine 1 (draw)   */
extern void func_8001ECB8(void);
extern void func_80081D78(void);
extern void func_80000964(void);
extern void (*gDebugRoutine1)(void);
extern void (*gDebugRoutine2)(void);

/* Per-frame in-level update wrapper: run the original routine, then in battle
 * mode drive our sim from the controller and puppet player 0 (the campaign
 * player object) by the sim's per-frame displacement (X/Z; Y left to the game).
 *
 * A1.2b: also spawns 3 extra actors (players 1-3) into gObjects[14..77] via the
 * game's own func_80027464, binds each one's animation with func_8001ABF4 (the
 * piece the general spawn omits for animated models — without it the draw
 * aborts), and positions them from the sim each frame against a frozen world
 * origin (no mirror). WORKS on a flat arena (warped to MAP_NITROS_1); the Battle
 * Room's pits aborted per-object collision on off-platform actors. Detail:
 * docs/bmhero-recomp-integration-notes.md §8. TODO: suppress the Nitros boss;
 * swap the bomb placeholder mesh (gFileArray[9]) for the bomber (gFileArray[1]);
 * inert objID (door behaviour still runs). */
void arena_render_routine(void) {
#if ARENA_SPAWN_TEST
    /* Boss suppression: BEFORE the update loop (func_80024744) runs any object's
     * per-frame behaviour, deactivate every gObjects[14..77] that isn't one of
     * our 3 puppets. In a Nitros boss arena this silences the boss (and its
     * flaky per-frame behaviour) while leaving the flat floor geometry intact. */
    if (arena_bridge_is_battle() && gPlayerObject != NULL) {
        s32 s1 = arena_export_puppet_get_slot(1);
        s32 s2 = arena_export_puppet_get_slot(2);
        s32 s3 = arena_export_puppet_get_slot(3);
        s32 k;
        for (k = 14; k < 78; k++) {
            if (k != s1 && k != s2 && k != s3)
                gObjects[k].actionState = ACTION_NONE;
        }
    }
#endif
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
        gPlayerObject->Pos.x += arena_export_player_x(0);   /* getter returns dx */
        gPlayerObject->Pos.z += arena_export_player_z(0);   /* getter returns dz */
        gPlayerObject->Rot.y  = arena_export_player_yaw(0);

        /* A1.2b DEBUG (Phase-1 evidence): log the mesh file pointers + first free
         * slot on the first in-level frame; spawn DISABLED to confirm the crash
         * lives in the spawn path and to capture the log before any crash. */
        if (!arena_export_puppet_ready()) {
            union { f32 f; u32 u; } cx, cy, cz;
            cx.f = gPlayerObject->Pos.x;
            cy.f = gPlayerObject->Pos.y;
            cz.f = gPlayerObject->Pos.z;
            arena_export_puppet_capture((s32)cx.u, (s32)cy.u, (s32)cz.u);

#if ARENA_SPAWN_TEST
            /* Spawn 3 placeholder actors (players 1-3), once, anchored at the
             * player's spawn Pos. func_80027464 loads the (bomb) model; the added
             * func_8001ABF4 binds its animation instance (the piece the general
             * spawn omits for animated models — without it the draw aborts). */
            {
                s32 i;
                for (i = 1; i < 4; i++) {   /* players 1-3 */
                    struct ObjSpawnInfo info;
                    info.unk0 = 0; info.unk2 = OBJ_TOBIRA1_O; info.unk4 = 9;
                    info.unk6 = 0; info.unk7 = 0; info.unk8 = 0; info.unk9 = 0; info.unkA = 0;
                    {
                        s32 slot = func_80027464(1, &info,
                                                 gPlayerObject->Pos.x + 60.0f,   /* just beside the player */
                                                 gPlayerObject->Pos.y,
                                                 gPlayerObject->Pos.z, 0.0f);
                        arena_export_dbg_u32(210 + i, slot);
                        if (slot >= 0) func_8001ABF4(slot, 0, 0, D_801163DC_ADDR);
                        arena_export_puppet_set_slot(i, slot);
                    }
                }
                arena_export_dbg_u32(220, 0);
            }
#else
            arena_export_puppet_set_slot(1, -1);
#endif
        }

#if ARENA_SPAWN_TEST && ARENA_POSITION
        /* Position the 3 actors from the sim each frame (frozen anchor -> no
         * mirror), runs after the spawn-once block above. */
        if (arena_export_puppet_ready()) {
            s32 i;
            for (i = 1; i < 4; i++) {
                s32 slot = arena_export_puppet_get_slot(i);
                if (slot >= 0) {
                    gObjects[slot].Pos.x       = arena_export_puppet_wx(i);
                    gObjects[slot].Pos.y       = arena_export_puppet_wy(i);
                    gObjects[slot].Pos.z       = arena_export_puppet_wz(i);
                    gObjects[slot].Rot.y       = arena_export_puppet_yaw(i);
                    gObjects[slot].actionState = ACTION_IDLE;
                }
            }
        }
#endif
    }
}

/* Level-enter setup: original body, but route per-frame routine 2 through our
 * wrapper so the puppet write runs every frame in-level. */
RECOMP_PATCH void func_800824A8(void) {
    func_8001ECB8();
    gDebugRoutine1 = &func_800821E0;
    gDebugRoutine2 = &arena_render_routine;   /* was &func_80024744 */
    func_80081D78();
    func_80000964();
}
