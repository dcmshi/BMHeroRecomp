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

/* A1.2b puppet exports: native holds the frozen world origin + slot table
 * (patches must be stateless); we read placement back each frame. */
DECLARE_FUNC(void, arena_export_puppet_capture, s32 bx, s32 by, s32 bz);
DECLARE_FUNC(s32,  arena_export_puppet_ready);
DECLARE_FUNC(void, arena_export_puppet_set_slot, s32 i, s32 slot);
DECLARE_FUNC(s32,  arena_export_puppet_get_slot, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wx, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wy, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wz, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_yaw, s32 i);

/* Game proper-spawn: scans gObjects[14..77], loads mesh from gFileArray[info->unk4]. */
extern s32 func_80027464(s32 count, struct ObjSpawnInfo* info, f32 x, f32 y, f32 z, f32 rotY);
/* Animation bind (Unk148 instance) — the piece the general spawn omits for
 * animated models; without it the draw aborts. func_8001ABF4 is a function
 * (resolves in patches); its anim-config arg is an auto-named DATA symbol
 * (D_xxxx) that does NOT resolve via the patch reloc path, so we pass its
 * address as a literal (the recomp translates game addresses on deref). */
struct UnkStruct8016C298_1;
extern void func_8001ABF4(s32 arg0, s32 arg1, s32 arg2, struct UnkStruct8016C298_1* arg3);
#define D_801163DC_ADDR ((struct UnkStruct8016C298_1*)0x801163DC)  /* bomb anim config */
/* Follow-up lead (real bomber mesh, integration notes §8): the player-bomber is
 * gFileArray[1] (func_8001BD44 cfg 0x13); D_80101E8C @ 0x80101E8C is A bomber
 * anim config, but it's a multi-part SKELETAL model — a single bind draws a
 * malformed model (white-screen RSP abort). Needs the real multi-part load
 * (4DFF0.c) + per-part skeletal binds + an idle pose. Deferred. */

extern void func_80024744(void);            /* original per-frame routine 2 (update) */
extern void func_800821E0(void);            /* original per-frame routine 1 (draw)   */
extern void func_8001ECB8(void);
extern void func_80081D78(void);
extern void func_80000964(void);
extern void (*gDebugRoutine1)(void);
extern void (*gDebugRoutine2)(void);

/* Per-frame in-level update wrapper (routed in via func_800824A8 below). In
 * battle mode it drives our sim from the controller and puppets all 4 players:
 *  - player 0 = the campaign player object, moved by the sim's per-frame
 *    displacement (X/Z; Y left to the game so it stays grounded, camera follows);
 *  - players 1-3 = 3 extra actors spawned once into gObjects[14..77] via the
 *    game's own func_80027464 + func_8001ABF4 (anim bind), then positioned each
 *    frame from the sim against a FROZEN world origin (no mirror).
 * Runs on a flat arena (warped to MAP_NITROS_1) — the Battle Room's pits aborted
 * per-object collision on off-platform actors. The pre-update sweep below
 * suppresses that arena's boss. Full RE: integration notes §8. Actors are bomb
 * placeholders; real bomber models are a follow-up (skeletal, see above). */
void arena_render_routine(void) {
    /* Boss suppression: BEFORE the update loop runs any object's per-frame
     * behaviour, deactivate every gObjects[14..77] that isn't one of our 3
     * puppets. Silences the arena's boss (and its flaky behaviour) while leaving
     * the map's floor geometry (not in gObjects) intact. */
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

        /* Spawn the 3 actors once. Freeze the world anchor (player's spawn Pos,
         * passed as u32 bits — the export ABI can't take float args) + sim ref,
         * then proper-spawn + anim-bind each into a free [14..77] slot. */
        if (!arena_export_puppet_ready()) {
            union { f32 f; u32 u; } cx, cy, cz;
            cx.f = gPlayerObject->Pos.x;
            cy.f = gPlayerObject->Pos.y;
            cz.f = gPlayerObject->Pos.z;
            arena_export_puppet_capture((s32)cx.u, (s32)cy.u, (s32)cz.u);

            s32 i;
            for (i = 1; i < 4; i++) {   /* players 1-3 */
                struct ObjSpawnInfo info;
                info.unk0 = 0; info.unk2 = OBJ_TOBIRA1_O; info.unk4 = 9;   /* bomb placeholder mesh */
                info.unk6 = 0; info.unk7 = 0; info.unk8 = 0; info.unk9 = 0; info.unkA = 0;
                {
                    s32 slot = func_80027464(1, &info,
                                             gPlayerObject->Pos.x,
                                             gPlayerObject->Pos.y,
                                             gPlayerObject->Pos.z, 0.0f);
                    if (slot >= 0) func_8001ABF4(slot, 0, 0, D_801163DC_ADDR);   /* bind anim */
                    arena_export_puppet_set_slot(i, slot);
                }
            }
        }

        /* Position the 3 actors from the sim each frame (frozen anchor -> no mirror). */
        {
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
