#include "patches.h"
#include "misc_funcs.h"

#include <ultra64.h>
#include "types.h"
#include "variables.h"

/* Import the EXPORT (syms.ld / REGISTER_FUNC) names, not the internal C++ names. */
DECLARE_FUNC(s32,  arena_bridge_is_battle);
DECLARE_FUNC(void, arena_export_tick_input, s32 sx, s32 sy, s32 buttons);  /* b0 jump,b1 bomb,b2 set */
DECLARE_FUNC(f32,  arena_export_player_x, s32 i);
DECLARE_FUNC(f32,  arena_export_player_y, s32 i);
DECLARE_FUNC(f32,  arena_export_player_z, s32 i);
DECLARE_FUNC(f32,  arena_export_player_yaw, s32 i);

/* A1.2b puppet exports: native holds the frozen world origin + slot table
 * (patches must be stateless); we read placement back each frame. */
DECLARE_FUNC(void, arena_export_puppet_capture, s32 bx, s32 by, s32 bz);
DECLARE_FUNC(s32,  arena_export_puppet_ready);
DECLARE_FUNC(s32,  arena_export_spawn_gate);   /* A1.2d: 1 only after ~90 frames (heap-ready) */
DECLARE_FUNC(s32,  arena_export_draw_gate);    /* A1.2e: 1 after ~30 frames (routine1 restore) */
DECLARE_FUNC(void, arena_export_draw_gate_reset);  /* A1.2e: reset at every level-enter */
DECLARE_FUNC(void, arena_export_puppet_set_slot, s32 i, s32 slot);
DECLARE_FUNC(s32,  arena_export_puppet_get_slot, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wx, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wy, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_wz, s32 i);
DECLARE_FUNC(f32,  arena_export_puppet_yaw, s32 i);

/* A1.2c bomb exports */
DECLARE_FUNC(s32,  arena_export_bomb_active, s32 i);
DECLARE_FUNC(f32,  arena_export_bomb_wx, s32 i);
DECLARE_FUNC(f32,  arena_export_bomb_wy, s32 i);
DECLARE_FUNC(f32,  arena_export_bomb_wz, s32 i);
DECLARE_FUNC(void, arena_export_bomb_set_slot, s32 i, s32 slot);
DECLARE_FUNC(s32,  arena_export_bomb_get_slot, s32 i);
DECLARE_FUNC(s32,  arena_export_is_actor_slot, s32 slot);

/* A1.2c slice 2: blast exports + the game effect spawner */
DECLARE_FUNC(s32,  arena_export_spike_once);
DECLARE_FUNC(s32,  arena_export_spike_next);
DECLARE_FUNC(s32,  arena_export_sweep_active);
DECLARE_FUNC(void, arena_export_dbg_u32, s32 tag, s32 val);   /* evidence logger */
DECLARE_FUNC(s32,  arena_export_blast_new, s32 i);
DECLARE_FUNC(f32,  arena_export_blast_wx, s32 i);
DECLARE_FUNC(f32,  arena_export_blast_wy, s32 i);
DECLARE_FUNC(f32,  arena_export_blast_wz, s32 i);
extern void func_80081468(s32 id, f32 x, f32 y, f32 z);   /* spawn effect by ID at pos */

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
/* Bomb-actor pool size. 6 = TUNE_MAX_LIVE_BOMBS (covers every bomb the sim can
 * currently make). Ceiling: 6 is stable, 8+ crashes at spawn — the suppressed
 * Nitros boss holds model/anim-pool slots that actionState=NONE doesn't free,
 * leaving little headroom. Raising the sim cap (A1.3) needs that freed first
 * (integration notes §8). */
#define BOMB_POOL 6
/* A1.2d real bomber. The bomber is ONE object, ONE part; the mesh is loaded by
 * the spawner from an ObjSpawnInfo, and the anim instance is bound with
 * func_8001C0EC (-> func_8001BE6C) + func_8001ABF4 — but the descriptor and
 * tables MUST come from the game's own registry, gObjInfo[objID] (named data
 * symbol, patch-visible), exactly as the game's generic binder func_8002BE04
 * (2BF00.c:227) does. Hardcoding the MENU bomber's table (D_80115F34) hung
 * forever: file 1's layout is context-dependent, and func_800122F0 parses
 * alloc sizes straight from whatever the offset points at (garbage -> endless
 * malloc_game walk; forensic tags 43-46, 2026-07-22). OBJ_MIR_BOMBER is the
 * training-room mirror bomberman — a real in-level NPC with the full bomber
 * model + anims. Its behaviour is NOT wanted: the puppet keeps the inert door
 * objID and only borrows the entry's mesh + anim tables. */
extern void func_8001C0EC(s32 objId, s32 part, s32 animIdx, s32 fileID, u32* animTable);

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
    /* A1.2e forensics: the game ROTATES the stick in place inside
     * func_80024744 (21E10.c:833, guRotateF(gView.rot.y) -> guMtxXFMF) for
     * gCameraType in {1,2,5,6,7,8} — capture the raw stick BEFORE that call
     * so the hold-L log can show raw vs. game-rotated (double-rotation
     * check). */
    f32 raw_sx = gActiveContStickX;
    f32 raw_sy = gActiveContStickY;

    /* A1.2e stability: restore the draw hook once the load window is over
     * (func_800824A8 leaves it NULL through the level-enter pump — see the
     * patch below). Idempotent write, battle-agnostic (campaign levels route
     * through this routine too and need their draw hook back). */
    if (arena_export_draw_gate())
        gDebugRoutine1 = &func_800821E0;

    /* Boss suppression: BEFORE the update loop runs any object's per-frame
     * behaviour, deactivate every gObjects[14..77] that isn't one of our actors
     * (the 3 player puppets or the 16 bomb actors — arena_is_actor_slot checks
     * both native slot tables). Silences the arena's boss (and its flaky
     * behaviour) while leaving the map's floor geometry (not in gObjects) intact. */
    if (arena_bridge_is_battle() && gPlayerObject != NULL) {
        /* EVERY FRAME: the boss re-activates after the entry window (confirmed
         * on screen when the sweep was gated to ~5s), so keep sweeping all
         * non-actor [14..77] objects. Game effects turned out NOT to live in
         * this range (their draw ran with the sweep off), so this is safe. */
        s32 k;
        for (k = 14; k < 78; k++) {
            if (!arena_export_is_actor_slot(k))
                gObjects[k].actionState = ACTION_NONE;
        }
    }

    func_80024744();

    if (arena_bridge_is_battle() && gPlayerObject != NULL) {
        /* A1.2e phase-1 (TEMPORARY, removed in cleanup): camera forensics.
         * Logged only while L (E key) is held so the human samples exactly
         * where it matters. Floats logged as bit patterns. tags:
         * 70 eye.x  71 eye.z  72 at.x  73 at.z  74 fwd.x  75 fwd.z (normd) */
        if (gActiveContButton & CONT_L) {
            union { f32 f; u32 u; } v;
            f32 cfx = gView.at.x - gView.eye.x;
            f32 cfz = gView.at.z - gView.eye.z;
            f32 cl2 = cfx * cfx + cfz * cfz;
            v.f = gView.eye.x; arena_export_dbg_u32(70, v.u);
            v.f = gView.eye.z; arena_export_dbg_u32(71, v.u);
            v.f = gView.at.x;  arena_export_dbg_u32(72, v.u);
            v.f = gView.at.z;  arena_export_dbg_u32(73, v.u);
            if (cl2 > 0.0001f) {
                f32 cinv = 1.0f / sqrtf(cl2);
                v.f = cfx * cinv; arena_export_dbg_u32(74, v.u);
                v.f = cfz * cinv; arena_export_dbg_u32(75, v.u);
            }
            /* Double-rotation check: 76 camera type, 77/78 raw stick (pre-
             * func_80024744), 79/80 current stick (post), 81 gView.rot.y. */
            arena_export_dbg_u32(76, (u32)gCameraType);
            v.f = raw_sx;             arena_export_dbg_u32(77, v.u);
            v.f = raw_sy;             arena_export_dbg_u32(78, v.u);
            v.f = gActiveContStickX;  arena_export_dbg_u32(79, v.u);
            v.f = gActiveContStickY;  arena_export_dbg_u32(80, v.u);
            v.f = gView.rot.y;        arena_export_dbg_u32(81, v.u);
        }

        /* A1.2e RESOLVED: the raw pass-through IS camera-relative here — the
         * game itself rotates gActiveContStickX/Y in place by gView.rot.y
         * inside func_80024744 (21E10.c:833) for gCameraType in {1,2,5,6,7,8},
         * and this arena is type 6 (probe 2026-07-22: raw (0,80) -> (43.5,68.0)
         * = exactly rot.y=34deg). An additional gView-based rotation here
         * DOUBLE-ROTATES (~2x35deg -> forward speed cut to ~1/3 — the "slow
         * up/down" report, quantified). The A1.2a "compressed" note predates
         * the Nitros warp (Battle Room may differ — re-check if the map ever
         * changes: log gCameraType, must be in the rotation set). */
        s32 sx = (s32)(gActiveContStickX * (31.0f / 80.0f));
        s32 sy = (s32)(gActiveContStickY * (31.0f / 80.0f));
        if (sx >  31) sx =  31;
        if (sx < -31) sx = -31;
        if (sy >  31) sy =  31;
        if (sy < -31) sy = -31;
        s32 jump = (gActiveContButton & CONT_A) ? 1 : 0;
        s32 bomb = (gActiveContButton & CONT_B) ? 1 : 0;
        s32 set  = (gActiveContButton & CONT_G) ? 1 : 0;   /* Z trigger / Q key */
        s32 buttons = jump | (bomb << 1) | (set << 2);
        arena_export_tick_input(sx, sy, buttons);
        gPlayerObject->Pos.x += arena_export_player_x(0);   /* getter returns dx */
        gPlayerObject->Pos.z += arena_export_player_z(0);   /* getter returns dz */
        /* A1.2e: sim yaw -> game Rot.y is a MIRROR (negation), not a half-turn:
         * "+0" read opposite on E/W runs, "+180" read opposite on N/S runs
         * (two user reports 2026-07-22 triangulate the sign flip; sim yaw 0 =
         * -Z from the kick math, game turns the other way). */
        {
            f32 fy = 360.0f - arena_export_player_yaw(0);
            if (fy >= 360.0f) fy -= 360.0f;
            gPlayerObject->Rot.y = fy;
        }

        /* Spawn the 3 actors once. Freeze the world anchor (player's spawn Pos,
         * passed as u32 bits — the export ABI can't take float args) + sim ref,
         * then proper-spawn + anim-bind each into a free [14..77] slot.
         * A1.2d: gated behind ~90 warmup frames — the routine's FIRST call runs
         * synchronously inside level-enter (func_800824A8 -> func_80000964),
         * where the anim-instance path (func_8001C0EC -> func_800122F0) hangs
         * in malloc_game (symbolized stack, 2026-07-22). Spawn only once the
         * level loop is actually pumping. */
        if (!arena_export_puppet_ready() && arena_export_spawn_gate()) {
            union { f32 f; u32 u; } cx, cy, cz;
            cx.f = gPlayerObject->Pos.x;
            cy.f = gPlayerObject->Pos.y;
            cz.f = gPlayerObject->Pos.z;
            arena_export_puppet_capture((s32)cx.u, (s32)cy.u, (s32)cz.u);

            s32 i;
            for (i = 1; i < 4; i++) {   /* players 1-3 */
                struct ObjSpawnInfo info;
                s32 bomber_id;   /* A1.2d: picked gObjInfo entry, -1 = none */
                info.unk0 = 0; info.unk2 = OBJ_TOBIRA1_O;
                /* default to the bomb placeholder; the i==1 branch below upgrades
                 * it to a bomber entry if a populated one exists */
                info.unk4 = 9; info.unk6 = 0;
                info.unk7 = 0; info.unk8 = 0; info.unk9 = 0; info.unkA = 0;
                if (i == 1) {   /* A1.2d spike: player 1 = real bomber. Candidate
                                 * gObjInfo entries scanned null-safe (many entries
                                 * have unk38 == NULL — deref of a null/non-KSEG0
                                 * pointer is a HOST access violation in recomp'd
                                 * code, crash 2026-07-22). unk2 stays the door
                                 * objID — the NPC behaviour is unwanted. */
                    s32 cand[4];
                    s32 k;
                    cand[0] = OBJ_MIR_BOMBER; cand[1] = OBJ_EVBOMBER;
                    cand[2] = OBJ_EVS_BOM;    cand[3] = OBJ_BOMBER7;
                    bomber_id = -1;
                    for (k = 0; k < 4; k++) {
                        u32 si = (u32) gObjInfo[cand[k]].unk38;
                        u32 ap = (u32) gObjInfo[cand[k]].animPtr;
                        arena_export_dbg_u32(50 + k, si);   /* spawn-info ptr */
                        arena_export_dbg_u32(60 + k, ap);   /* anim table ptr */
                        if (bomber_id < 0 && si != 0 && ap != 0)
                            bomber_id = cand[k];
                    }
                    arena_export_dbg_u32(43, (u32)bomber_id);   /* the pick (-1 = none) */
                    if (bomber_id >= 0) {
                        struct ObjSpawnInfo* bi = gObjInfo[bomber_id].unk38;
                        info.unk0 = bi->unk0; info.unk4 = bi->unk4; info.unk6 = bi->unk6;
                        info.unk7 = bi->unk7; info.unk8 = bi->unk8; info.unk9 = bi->unk9;
                        info.unkA = bi->unkA;
                        arena_export_dbg_u32(44, ((u32)bi->unk0 << 16) | (u16)bi->unk4); /* part|file */
                    }
                    /* Registry empty => puppet stays a bomb. A1.2d verdict: the
                     * bomber MESH is resident (file 1 cfg 0x13) but its ANIMS are
                     * not reachable in the arena — the menu stream table
                     * D_80115F34 is garbage in-level (hdr count 0x6080A, file 1
                     * byte-identical across NITROS_1/MIRROR_ROOM), gObjInfo is
                     * empty in every warpable arena, and the modelTag has no
                     * embedded anims (func_8001191C AVs on the null-source bind,
                     * dump 2026-07-22). Drawing the mesh without an anim instance
                     * white-screens (A1.2b). Future lead: player 0 ANIMATES here,
                     * so valid bomber anim data IS resident via the player path —
                     * trace gPlayerObject's anim-instance bind. */
                }
                {
                    s32 slot = func_80027464(1, &info,
                                             gPlayerObject->Pos.x,
                                             gPlayerObject->Pos.y,
                                             gPlayerObject->Pos.z, 0.0f);
                    arena_export_dbg_u32(40, (u32)slot);   /* per-puppet spawn slot */
                    if (slot >= 0) {
                        if (i == 1 && bomber_id >= 0) {
                            /* Bind model-anim + texanim exactly as the game's own
                             * generic binder does (func_8002BE04): per-objID gObjInfo
                             * tables, null-guarded. */
                            void* ap  = gObjInfo[bomber_id].animPtr;
                            void* ms  = gObjInfo[bomber_id].moveSpeed;
                            s32   prt = gObjInfo[bomber_id].unk38->unk0;
                            if (ap != NULL) {
                                func_8001C0EC(slot, prt, 0,
                                              gObjInfo[bomber_id].unk38->unk4, (u32*)ap);
                                arena_export_dbg_u32(41, 0);
                            }
                            if (ms != NULL) {
                                func_8001ABF4(slot, 0, prt, (struct UnkStruct8016C298_1*)ms);
                                arena_export_dbg_u32(42, 0);
                            }
                        } else {
                            func_8001ABF4(slot, 0, 0, D_801163DC_ADDR);   /* bind anim */
                        }
                    }
                    arena_export_puppet_set_slot(i, slot);
                }
            }

            /* A1.2c slice 2 fallback: 4 blast actors (bomb mesh, scaled by the
             * sim's blast radius each frame), start hidden. */
            {
                s32 bj;
                for (bj = 0; bj < 4; bj++) {
                    struct ObjSpawnInfo linfo;
                    linfo.unk0 = 0; linfo.unk2 = OBJ_TOBIRA1_O; linfo.unk4 = 9;
                    linfo.unk6 = 0; linfo.unk7 = 0; linfo.unk8 = 0; linfo.unk9 = 0; linfo.unkA = 0;
                    {
                        s32 slot = func_80027464(1, &linfo,
                                                 gPlayerObject->Pos.x,
                                                 gPlayerObject->Pos.y,
                                                 gPlayerObject->Pos.z, 0.0f);
                        if (slot >= 0) {
                            func_8001ABF4(slot, 0, 0, D_801163DC_ADDR);
                            gObjects[slot].actionState = ACTION_NONE;   /* start hidden */
                        }
                        arena_export_blastactor_set_slot(bj, slot);
                    }
                }
            }

            /* A1.2c: spawn a pool of 16 bomb actors (same recipe), start hidden;
             * the per-frame loop below shows/positions the live ones. */
            {
                s32 bi;
                for (bi = 0; bi < BOMB_POOL; bi++) {
                    struct ObjSpawnInfo binfo;
                    binfo.unk0 = 0; binfo.unk2 = OBJ_TOBIRA1_O; binfo.unk4 = 9;   /* bomb mesh */
                    binfo.unk6 = 0; binfo.unk7 = 0; binfo.unk8 = 0; binfo.unk9 = 0; binfo.unkA = 0;
                    {
                        s32 slot = func_80027464(1, &binfo,
                                                 gPlayerObject->Pos.x,
                                                 gPlayerObject->Pos.y,
                                                 gPlayerObject->Pos.z, 0.0f);
                        if (slot >= 0) {
                            func_8001ABF4(slot, 0, 0, D_801163DC_ADDR);
                            gObjects[slot].actionState = ACTION_NONE;   /* start hidden */
                        }
                        arena_export_bomb_set_slot(bi, slot);
                    }
                }
            }
        }

        /* Position the 3 actors from the sim each frame (frozen anchor -> no mirror). */
        {
            s32 i;
            for (i = 1; i < 4; i++) {
                s32 slot = arena_export_puppet_get_slot(i);
                if (slot >= 0) {
                    f32 py = 360.0f - arena_export_puppet_yaw(i);   /* same mirror as player 0 */
                    if (py >= 360.0f) py -= 360.0f;
                    gObjects[slot].Pos.x       = arena_export_puppet_wx(i);
                    gObjects[slot].Pos.y       = arena_export_puppet_wy(i);
                    gObjects[slot].Pos.z       = arena_export_puppet_wz(i);
                    gObjects[slot].Rot.y       = py;
                    gObjects[slot].actionState = ACTION_IDLE;
                }
            }
        }

        /* A1.2c: show + position each live sim bomb, hide the free ones. */
        {
            s32 bi;
            for (bi = 0; bi < 16; bi++) {
                s32 slot = arena_export_bomb_get_slot(bi);
                if (slot >= 0) {
                    if (arena_export_bomb_active(bi)) {
                        gObjects[slot].Pos.x       = arena_export_bomb_wx(bi);
                        gObjects[slot].Pos.y       = arena_export_bomb_wy(bi);
                        gObjects[slot].Pos.z       = arena_export_bomb_wz(bi);
                        gObjects[slot].actionState = ACTION_IDLE;   /* visible */
                    } else {
                        gObjects[slot].actionState = ACTION_NONE;   /* hidden */
                    }
                }
            }
        }

        /* A1.2c slice 2 (fallback visual): drive the 4 blast actors from the
         * sim's live blasts — position at each blast center, Scale grown with
         * the sim radius (also doubles as the generic-draw Scale test: if
         * Scale is ignored they still show as a normal-size 'pop'). The game
         * effect spawner (func_80081468) was abandoned: in this bypassed
         * arena its effects render invisible and several IDs crash the
         * effect-list draw (func_8001CDF4) — see integration notes. */
        {
            s32 aj = 0;
            s32 bi;
            for (bi = 0; bi < 16 && aj < 4; bi++) {
                if (arena_export_blast_active(bi)) {
                    s32 slot = arena_export_blastactor_get_slot(aj);
                    if (slot >= 0) {
                        f32 sc = arena_export_blast_wr(bi) / 15.0f;   /* mesh~15u base; TODO(feel) */
                        if (sc < 1.0f) sc = 1.0f;
                        gObjects[slot].Pos.x       = arena_export_blast_wx(bi);
                        gObjects[slot].Pos.y       = arena_export_blast_wy(bi);
                        gObjects[slot].Pos.z       = arena_export_blast_wz(bi);
                        gObjects[slot].Scale.x     = sc;
                        gObjects[slot].Scale.y     = sc;
                        gObjects[slot].Scale.z     = sc;
                        gObjects[slot].actionState = ACTION_IDLE;
                    }
                    aj++;
                }
            }
            for (; aj < 4; aj++) {
                s32 slot = arena_export_blastactor_get_slot(aj);
                if (slot >= 0) gObjects[slot].actionState = ACTION_NONE;
            }
        }
    }
}

/* Level-enter setup: original body, but route per-frame routine 2 through our
 * wrapper so the puppet write runs every frame in-level. */
RECOMP_PATCH void func_800824A8(void) {
    func_8001ECB8();
    /* A1.2e stability: do NOT set gDebugRoutine1 here. The runtime's func_map
     * mutates during overlay load/unload, and the draw dispatcher's INDIRECT
     * call (func_8001D9E4 -> gDebugRoutine1()) races it during the level-enter
     * pump below (func_80000964) — 7 symbolized dumps, stochastic, timing-
     * dependent (human-paced frontends hit it, the soak's mash rarely does).
     * The dispatcher tolerates NULL (17930.c:1561); arena_render_routine
     * restores routine1 once the load window is over (arena_draw_gate, ~30
     * routine frames). Same crash class as the §8.9 racing prints. */
    gDebugRoutine1 = NULL;
    arena_export_draw_gate_reset();   /* the load window recurs per transition */
    gDebugRoutine2 = &arena_render_routine;   /* was &func_80024744 */
    func_80081D78();
    func_80000964();
}
