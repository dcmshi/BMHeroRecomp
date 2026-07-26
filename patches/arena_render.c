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
/* A1.4: was 6, but the un-pile gives each bomb a DISTINCT model-pool slot (they
 * used to all collapse into one, masking the ceiling). 3 puppets + 4 = 7 actors,
 * under the ~8 ceiling; covers the 4-bomb spread. A 5th/6th live bomb won't render
 * until the boss's held slots are freed. */
#define BOMB_POOL 4
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

/* A1.4 set-bomb animation. On a sim set edge for player 0, play the game's own
 * set/drop-bomb pose on gPlayerObject: code_extra_0 anim 29, bank 1, table
 * D_80115808 — the EXACT trigger form proven by the fork's teleporter_obj.c
 * (same call, anim 7). D_80115808 resolves in patches (it's in data_dump.toml),
 * so no literal-address workaround (§8.2) is needed for the table. The read-back
 * getters func_8001B880/62C resolve as functions and read the live model-anim
 * record (unk14 index / unk24 frame @ +2/frame) for the auto-verify probe. Kick
 * has NO game anim (integration notes §8.5c) — the player keeps locomotion, no
 * trigger. */
extern s32 D_80115808[];                          /* code_extra_0 player anim table */
extern s32 func_8001B880(s32 objId, s32 part);    /* live anim index (unk14) */
extern f32 func_8001B62C(s32 objId, s32 part);    /* live anim frame counter (unk24) */
DECLARE_FUNC(s32,  arena_export_set_new, s32 i);           /* 1 once per player-i set edge */
DECLARE_FUNC(void, arena_export_dbg_anim, s32 idx, s32 frame);   /* burst-log anim idx+frame */

/* ---- A1.5 fixed arena camera ------------------------------------------- */
#include "arena_cam.h"                      /* pose constants; no game types    */
/* Native side owns the probe-mode gate AND the throttle, so this is called
 * unconditionally every frame and the patch stays stateless (a patch-local
 * counter aborts 0xC0000409). Floats cross as BIT PATTERNS - the export ABI
 * takes no float arguments (notes 8.2). */
DECLARE_FUNC(void, arena_export_dbg_cam, s32 tag, s32 x, s32 y, s32 z);
DECLARE_FUNC(f32,  arena_cam_at_x);         /* arena centre, Hero world coords  */
DECLARE_FUNC(f32,  arena_cam_at_y);
DECLARE_FUNC(f32,  arena_cam_at_z);

/* Bit-cast a float into an int arg for the export ABI. */
static s32 fbits(f32 v) { union { f32 f; s32 i; } u; u.f = v; return u.i; }

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

        /* A1.5 camera probe. Sampled at routine ENTRY, i.e. before this frame's
         * write but after the previous frame's - so once the override lands,
         * these values proving equal to what we wrote is the evidence that
         * nothing else stomps gView. Native side gates on ARENA_AUTO_BATTLE=6
         * and throttles to one sample per 30 frames. */
        arena_export_dbg_cam(0, fbits(gView.at.x),  fbits(gView.at.y),  fbits(gView.at.z));
        arena_export_dbg_cam(1, fbits(gView.eye.x), fbits(gView.eye.y), fbits(gView.eye.z));
        arena_export_dbg_cam(2, fbits(gView.rot.x), fbits(gView.rot.y), fbits(gView.rot.z));
        arena_export_dbg_cam(3, fbits(gView.up.x),  fbits(gView.up.y),  fbits(gView.up.z));
        arena_export_dbg_cam(4, (s32)gCameraType,   fbits(gView.dist),  0);
    }

    func_80024744();

    if (arena_bridge_is_battle() && gPlayerObject != NULL) {
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
        /* A1.4 co-drive FIX (2026-07-24): capture the world origin + sim ref ONCE,
         * early (after the ~30-frame draw-gate warmup — level stable, minimal
         * drift, vs the old 90-frame spawn-gate capture that drifted ~1400u), then
         * drive player 0 by the sim's ABSOLUTE mapped position (like the 3 puppets)
         * — NOT by adding a per-frame delta on top of the game walker's own
         * movement. The old delta model double-drove the player (game + sim) and
         * let the sim jam against its arena walls (dx->0) while the game coasted ->
         * mid-floor slowdowns (per-frame [mv] trace). Absolute drive makes the sim
         * the sole owner of X/Z; Y stays game-driven (grounding); camera follows. */
        if (!arena_export_puppet_ready() && arena_export_draw_gate()) {
            union { f32 f; u32 u; } cx, cy, cz;
            cx.f = gPlayerObject->Pos.x; cy.f = gPlayerObject->Pos.y; cz.f = gPlayerObject->Pos.z;
            arena_export_puppet_capture((s32)cx.u, (s32)cy.u, (s32)cz.u);
        }
        if (arena_export_puppet_ready()) {
            gPlayerObject->Pos.x = arena_export_puppet_wx(0);   /* absolute sim pos (no co-drive) */
            gPlayerObject->Pos.z = arena_export_puppet_wz(0);
        }
        /* A1.2e: Rot.y = 180 - sim_yaw, DERIVED (not guessed) from the game's
         * own movement math: game moves along (+sin th, +cos th) (2BF00.c:480),
         * the sim along (+sin yaw, -cos yaw) (arena_sim.c kick math): th=180-yaw
         * preserves movement DIRECTION. A1.3 facing (user feel-tests 2026-07-23):
         * Facing = the GAME'S OWN moveAngle, copied 1:1 (user's suggestion).
         * The game's player update (func_80024744, called above) computes
         * gPlayerObject->moveAngle authentically from the camera-rotated stick
         * every frame; the probe verified it's live and correct in the arena
         * (moveAngle = Math_Atan2f(Vel.x,Vel.z), e.g. 218.3 for Vel(-11.16,
         * -14.12)). Deriving facing from our sim yaw or from dx/dz fought the
         * gradual-turn lag and angle-convention mismatches (read 90deg then
         * 45deg off). Copying the game's own facing sidesteps all of it and IS
         * authentic by construction. We still drive POSITION from the sim
         * (dx/dz); only facing borrows the game's value. */
        gPlayerObject->Rot.y = gPlayerObject->moveAngle;

        /* A1.4: set-bomb animation for player 0. The sim tick above (line ~139)
         * latched a set edge if player 0 placed a bomb this frame (bomb
         * FREE->SETTLED). Overlay the game's own set/drop pose (anim 29) on
         * gPlayerObject once per event; the walker (func_80024744, above) returns
         * to locomotion on its next state change, so we don't re-trigger. Always
         * read-and-clear the edge (no accumulation); only trigger/read-back once
         * the player's model-anim record is bound (Unk140[0] >= 0 — a negative
         * slot would host-AV in func_8001C0EC / the getters during level-enter). */
        {
            s32 set_edge = arena_export_set_new(0);
            if (gPlayerObject->Unk140[0] >= 0) {
                if (set_edge)
                    func_8001C0EC(0, 0, 29, 1, (u32*)D_80115808);
                /* Auto-verify probe (temporary): burst-log the live anim index +
                 * frame so arena-soak.ps1 asserts idx->29 with the frame advancing. */
                arena_export_dbg_anim(func_8001B880(0, 0), (s32)func_8001B62C(0, 0));
            }
        }

        /* Spawn the 3 actors once. Freeze the world anchor (player's spawn Pos,
         * passed as u32 bits — the export ABI can't take float args) + sim ref,
         * then proper-spawn + anim-bind each into a free [14..77] slot.
         * A1.2d: gated behind ~90 warmup frames — the routine's FIRST call runs
         * synchronously inside level-enter (func_800824A8 -> func_80000964),
         * where the anim-instance path (func_8001C0EC -> func_800122F0) hangs
         * in malloc_game (symbolized stack, 2026-07-22). Spawn only once the
         * level loop is actually pumping. */
        if (arena_export_spawn_gate() && arena_export_puppet_get_slot(1) < 0) {
            /* origin/ref are captured early (above); this block only SPAWNS the
             * actors, still gated ~90 frames (heap not serviceable earlier, §8.5b).
             * Spawn-once latch is now "player-1 slot not yet assigned" (puppet_ready
             * no longer gates the spawn — it means "origin captured" now). */
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

            /* A1.4: the A1.2c "fallback blast" actors are DROPPED. They were the
             * root of the invisible-set-bomb bug: each was spawned then set
             * ACTION_NONE, which let func_80027464 REUSE the just-freed slot for
             * the next actor, so every blast+bomb actor collapsed into ONE
             * gObjects slot (evidence: [setdbg] slot=17 for all). The blast render
             * loop then set that shared slot ACTION_NONE whenever no blast was live
             * -> the bomb the bomb-loop had just shown was immediately hidden.
             * Removing the blast actors frees model-pool budget (§8 ceiling) AND
             * stops the hiding. Explosion visual deferred (revisit by reusing a
             * bomb's own actor as its blast on detonation, to stay under the pool
             * ceiling). arena_blastactor slots stay -1, so the blast render loop
             * below no-ops. */

            /* A1.4: spawn the bomb-actor pool with DISTINCT slots. Do NOT set
             * ACTION_NONE at spawn (that caused the pile-up above); the per-frame
             * loop below hides the inactive ones the same frame (no flicker). Pool
             * kept small (BOMB_POOL) to stay under the model-pool ceiling now that
             * the actors take distinct slots. */
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
                        if (slot >= 0)
                            func_8001ABF4(slot, 0, 0, D_801163DC_ADDR);   /* anim bind; left ACTION_IDLE */
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
                    /* Puppet facing is a yaw-derived placeholder. Player 0 copies
                     * the game's own moveAngle, but puppets aren't gPlayerObject
                     * (no game moveAngle) and are POSITIONED absolutely, so no
                     * per-frame delta is available. It's invisible anyway: puppets
                     * are rotationally-symmetric bomb-mesh placeholders (real
                     * bomber mesh deferred, A1.2d §8.5b). Revisit facing when the
                     * bomber mesh lands (add per-puppet dx/dz exports + moveAngle). */
                    f32 py = 90.0f - arena_export_puppet_yaw(i);
                    if (py >= 360.0f) py -= 360.0f;
                    if (py < 0.0f)    py += 360.0f;
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
    /* A1.2e stability history: NULL-parking routine1 through the load window
     * failed — the level-enter pump can run 90+ routine frames, so any counter
     * gate reopens IN-pump (dump 2026-07-22 16:37). The race is fixed at the
     * dispatcher instead: see the func_8001D9E4 patch below (direct dispatch,
     * no func_map lookup for the known hook). Assign immediately again. */
    gDebugRoutine1 = &func_800821E0;
    gDebugRoutine2 = &arena_render_routine;   /* was &func_80024744 */
    func_80081D78();
    func_80000964();
}

/* A1.2e stability note: the draw dispatcher (func_8001D9E4) is patched in
 * required_patches.c — its `gDebugRoutine1()` indirect call was changed there
 * to direct-dispatch our known hook (no func_map lookup — the lookup RACES
 * overlay load/unload; 8 symbolized dumps, same class as the §8.9 prints). */
