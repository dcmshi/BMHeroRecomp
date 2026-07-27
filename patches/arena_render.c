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
DECLARE_FUNC(void, arena_export_puppet_capture, s32 bx, s32 by, s32 bz, s32 level);
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
DECLARE_FUNC(void, arena_export_dbg_anim, s32 idx, s32 frame, s32 state);  /* anim idx+frame+actionState */

/* ---- A1.5 fixed arena camera ------------------------------------------- */
#include "arena_cam.h"                      /* pose constants; no game types    */
/* Native side owns the probe-mode gate AND the throttle, so this is called
 * unconditionally every frame and the patch stays stateless (a patch-local
 * counter aborts 0xC0000409). Floats cross as BIT PATTERNS - the export ABI
 * takes no float arguments (notes 8.2). */
DECLARE_FUNC(void, arena_export_dbg_cam, s32 tag, s32 x, s32 y, s32 z);
DECLARE_FUNC(s32,  arena_export_floor_guard, s32 x, s32 y, s32 z);  /* A1.2g */
DECLARE_FUNC(f32,  arena_export_floor_last_x);
DECLARE_FUNC(f32,  arena_export_floor_last_y);
DECLARE_FUNC(f32,  arena_export_floor_last_z);
/* A1.2g floor raster (ARENA_AUTO_BATTLE=7). Native owns the grid cursor and the
 * accumulator; the patch just answers "is there floor at this point?" using the
 * game's own ground query. See the block at the end of arena_render_routine. */
DECLARE_FUNC(s32,  arena_export_floor_raster_active);
DECLARE_FUNC(s32,  arena_export_floor_raster_next);
DECLARE_FUNC(f32,  arena_export_floor_raster_px);
DECLARE_FUNC(f32,  arena_export_floor_raster_py);
DECLARE_FUNC(f32,  arena_export_floor_raster_pz);
DECLARE_FUNC(void, arena_export_floor_raster_report, s32 sel, s32 hbits, s32 type);
/* The game's ground query (decomp src/code/69AA0.c:205). Pure: it takes only a
 * position and refreshes the collision-result globals below - no caller context,
 * no object index. Every object's own ground handling calls it the same way. */
extern void func_80078168(f32 x, f32 y, f32 z);
/* Its results. Auto-named D_ data symbols are reached by ADDRESS LITERAL (§8.2)
 * rather than by extern: the literal form is proven, and a symbol that fails to
 * resolve through the patch reloc path corrupts silently. GQ_SEL selects which
 * of the two result slots holds the answer; the game's own "no floor here" test
 * is GQ_SEL==0 && GQ_H[0]==-30000.0f (69AA0.c:401). */
#define GQ_SEL  (*(volatile u8*)0x801776E0 & 1)
#define GQ_H    ((volatile f32*)0x80177760)
/* Surface TYPE (the object's unkAE). 69AA0.c:411 keys the hazard path off it:
 * types 0xF8 / 0xF7 / 0xF5 / 0xD9 raise the damage flag. Rastering this maps
 * the room's damage tiles exactly - the Nitros corners hurt and stun the
 * game-side player, and our spawns sit on them. */
#define GQ_TYPE ((volatile s32*)0x80177740)
/* D_8016E080: the hazard code the game derives from the surface under the
 * player (func_80086AD0, decomp 76640.c:714). 0 = none, 1 = the 0xF7 damage
 * tile our corners use. Auto-named data symbol -> address literal. */
#define gDebugHazardCode (*(volatile s8*)0x8016E080)
DECLARE_FUNC(f32,  arena_export_cam_dist);  /* ARENA_CAM_DIST, env-overridable  */
DECLARE_FUNC(f32,  arena_export_cam_zfar);  /* battle-mode far clip plane       */
DECLARE_FUNC(s32,  arena_export_cam_enabled);/* 0 = ARENA_CAM_OFF, runtime A/B  */
DECLARE_FUNC(s32,  arena_export_set_hold);  /* 1 while the set pose must hold   */
DECLARE_FUNC(s32,  arena_export_player_hp, s32 i);      /* A1.2g HUD: sim HP     */
DECLARE_FUNC(s32,  arena_export_player_stocks, s32 i);  /* rounds won            */
DECLARE_FUNC(s32,  arena_export_match_phase);           /* PHASE_*               */
/* ZFAR, consumed by guPerspective in the in-level draw (decomp 71AA0.c:610) and
 * set per-level from gLevelInfo[level]->unk2C (56800.c:372). Auto-named data
 * symbol -> address literal (section 8.2). The decomp declares it as a union
 * whose bytes[] alias is written by the per-object collision code, so write it
 * AFTER the update loop and before the draw. */
#define GAME_ZFAR (*(volatile f32*)0x801779C8)
DECLARE_FUNC(f32,  arena_export_cam_at_x);  /* arena centre, Hero world coords  */
DECLARE_FUNC(f32,  arena_export_cam_at_y);
DECLARE_FUNC(f32,  arena_export_cam_at_z);

/* Bit-cast a float into an int arg for the export ABI. */
static s32 fbits(f32 v) { union { f32 f; s32 i; } u; u.f = v; return u.i; }

/* Stamp the fixed arena pose onto gView. Writes ONLY at/rot/dist: func_8001994C
 * (decomp src/boot/17930.c:605) derives eye and up.y from exactly these, and the
 * probe confirmed its formula reproduces the live eye to 2dp.
 *
 * Called TWICE per frame, and both are load-bearing:
 *   - BEFORE func_80024744, because that routine rotates gActiveContStickX/Y in
 *     place by gView.rot.y — the stick must see our stable yaw, not the rail's.
 *   - AFTER it, because the game's own camera update runs INSIDE that call and
 *     reverts everything. Measured: we write rot=(60,0,0), and the next frame's
 *     entry reads back rot=(20,2,0). The post-write is what the draw actually
 *     sees. */
static void arena_cam_stamp(void) {
    f32 ax;
    if (!arena_export_cam_enabled()) return;   /* ARENA_CAM_OFF=1: runtime A/B */
    ax = arena_export_cam_at_x();
    f32 ay = arena_export_cam_at_y();
    f32 az = arena_export_cam_at_z();
    /* Distance comes from native so it can be swept with the ARENA_CAM_DIST env
     * var at runtime; the header constant is still the default and the shipped
     * value. Framing was a full patch rebuild per trial before this. */
    f32 dist = arena_export_cam_dist();

    gView.at.x  = ax;
    gView.at.y  = ay;
    gView.at.z  = az;
    gView.rot.x = ARENA_CAM_PITCH_DEG;   /* pitch; the game's own was 20deg     */
    gView.rot.y = ARENA_CAM_YAW_DEG;     /* the value that MUST stay fixed      */
    gView.rot.z = 0.0f;
    gView.dist  = dist;

    /* eye/up written EXPLICITLY. func_8001994C would derive them from the above
     * (decomp src/boot/17930.c:605), but it is gated on D_8016E134 == 0 and that
     * gate is evidently closed here: with only at/rot/dist written, the picture
     * was PIXEL-IDENTICAL across a 2x change of ARENA_CAM_DIST, and the logged
     * eye never moved off the rail camera's last value. So we finish the job
     * ourselves, using the game's own formula (guLookAt consumes eye/at/up).
     *
     * Same trig as the header's model, inlined here with no runtime calls:
     *   eye = at + dist * (cos(yaw+90)*cos(pitch), sin(pitch), sin(yaw+90)*cos(pitch))
     * At yaw 0 that is at + (0, dist*sin(pitch), dist*cos(pitch)) — +Z and above,
     * which puts the arena's long axis horizontal. */
    gView.eye.x = ax + dist * ARENA_CAM_COS_YAW_EFF * ARENA_CAM_COS_PITCH;
    gView.eye.y = ay + dist * ARENA_CAM_SIN_PITCH;
    gView.eye.z = az + dist * ARENA_CAM_SIN_YAW_EFF * ARENA_CAM_COS_PITCH;
    gView.up.x  = 0.0f;
    gView.up.y  = 1.0f;   /* pitch 60 is < 90, so the game's rule gives +1 */
    gView.up.z  = 0.0f;
}

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
    u16 held_buttons;
    /* A1.2g - SUPPRESS THE ROOM'S OWN DAMAGE. In battle the SIM owns every hit
     * and every hit point; the game object is a puppet, so any damage the room
     * lands on it is by definition wrong. It is also dangerous: the bypassed
     * death path crashes (§8.9), which makes a room hazard a route to a hard
     * crash.
     *
     * The Nitros floor has damage tiles at the four corners - surface type 0xF7,
     * located exactly with the probe-7 surface-type raster. The chain is
     * func_80086AD0 (reads the surface under gPlayerObject, sets D_8016E080 = 1
     * for 0xF7, decomp 76640.c:714) -> the case 5/6 block inside func_80024744,
     * which turns that into a damage request in D_80177648 (21E10.c:648) -> the
     * application, gated on exactly one flag: `if (!gDebugInvincibileFlag)`
     * (21E10.c:670).
     *
     * So ONE named flag suppresses the whole class - the tiles and anything else
     * the room might throw - instead of us patching each hazard path separately.
     * It is the game's own debug facility, and its only other uses are the debug
     * menu's display and toggle, so nothing else changes.
     *
     * Written EVERY frame and CLEARED outside battle, so leaving a match for the
     * campaign in the same process cannot leave the player invincible. */
    gDebugInvincibileFlag = arena_bridge_is_battle() ? 1 : 0;

    /* A1.2g HUD - REUSE Hero's own in-level HUD rather than building an overlay.
     * The art, layout and draw already exist and already look like the game; all
     * they lack is our numbers. Driven EVERY frame so it tracks the sim exactly,
     * including across a round restart.
     *
     *   gHealthCount <- sim HP. TUNE_START_HP is 4 to match gMaxHealth, which the
     *                   game hard-codes to 4 (76640.c func_80088134), so this is
     *                   1:1 - no scaling, no half-bars.
     *   gBombCount   <- 3, gFireCount <- 3. These count POWERUPS COLLECTED and
     *                   cap at 3 (21E10.c:366/374); the HUD draws count + 1, so
     *                   3 renders as "4" - the maximum, not an off-by-one.
     *                   Battle has no powerups; everyone is permanently fully
     *                   kitted, so showing max is honest rather than decorative.
     *   gGemCount    <- 0. Gems do not exist in battle.
     *   gScore       <- 0. The campaign score is meaningless here. Zeroing only
     *                   "censors" it to 00000 - actually hiding it, or
     *                   repurposing the field per player, means touching the HUD
     *                   draw and is a separate decision.
     *
     * Non-battle is left completely alone: these are the campaign's own counters
     * and writing them outside battle would corrupt a real playthrough. */
    if (arena_bridge_is_battle()) {
        s32 hp = arena_export_player_hp(0);
        if (hp < 0) hp = 0;
        if (hp > (s32)gMaxHealth) hp = (s32)gMaxHealth;
        gHealthCount = (s8)hp;
        gBombCount   = 3;
        gFireCount   = 3;
        gGemCount    = 0;
        gScore       = 0;
    }

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
        /* The game's OWN bomb pool is gObjects[2..5] (Get_InactiveObject,
         * 69AA0.c) - below the [14..77] range this sweep was built for, so it
         * was never suppressed. Belt and braces alongside the button mask above:
         * anything spawned before the mask takes effect gets cleaned up rather
         * than sitting on the floor as an inert bomb that never explodes. */
        for (k = 2; k < 6; k++)
            gObjects[k].actionState = ACTION_NONE;
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

        /* A1.5 FIXED CAMERA. Re-asserted EVERY frame (the established idiom —
         * the boss re-activates if its sweep stops, notes 8.13).
         *
         * ORDERING IS LOAD-BEARING: func_80024744 (called just below) rotates
         * gActiveContStickX/Y in place by gView.rot.y. Writing the camera AFTER
         * that call would rotate this frame's stick by the game's swinging yaw
         * while the picture used ours — they'd disagree by up to 120deg (the
         * measured swing), which is worse than the drift we're fixing.
         *
         * We write ONLY at / rot / dist. func_8001994C (decomp src/boot/17930.c:605,
         * recovered with tools/decomp-func.ps1) derives eye AND up.y from exactly
         * these every frame, gated on D_8016E134 == 0 — and the probe confirmed
         * its formula reproduces the live eye exactly. Writing eye ourselves
         * would just be recomputed away, and "nothing changed" is a far harder
         * symptom to read than a wrong pose. Let the game finish the job. */
        arena_cam_stamp();   /* pre-update: the stick rotation reads rot.y */
    }

    /* TAKE THE BUTTONS AWAY FROM THE GAME'S PLAYER (battle only).
     *
     * gPlayerObject is meant to be a PUPPET: we own its position, its facing and
     * its animations. But its own action logic still ran, and it still read the
     * controller - so every press was acted on TWICE. Three symptoms from the
     * 2026-07-27 feel test, one cause:
     *   - "throwing seems to throw two bombs, one explodes and one does not" -
     *     ours (sim, drawn from gObjects[14..77]) plus the GAME'S own, which
     *     Get_InactiveObject puts in gObjects[2..5] (decomp 69AA0.c). That range
     *     is BELOW our suppression sweep, so it was never touched; and it never
     *     detonates because its fuse logic depends on game state we bypass.
     *   - "placing bomb with Q plays the throw animation instead of the set
     *     animation" - the game's own throw animation, fighting our A1.4 pose.
     *   - throwing while the SIM player is dead - the game object has no idea.
     *
     * The STICK is deliberately left alone: func_80024744 turns it into
     * gPlayerObject->moveAngle, which we copy for facing. Only the buttons go.
     * Captured first, so the sim still sees the real presses below. */
    held_buttons = gActiveContButton;
    if (arena_bridge_is_battle()) gActiveContButton = 0;

    func_80024744();

    if (arena_bridge_is_battle() && gPlayerObject != NULL) {
        /* Post-update re-assert. The game's camera update runs inside
         * func_80024744 and reverts our pose (measured: wrote (60,0,0), read
         * back (20,2,0) next frame). This write is the one the draw sees. */
        arena_cam_stamp();
        arena_export_dbg_cam(5, fbits(gView.rot.x), fbits(gView.rot.y), fbits(gView.rot.z));
        arena_export_dbg_cam(6, fbits(gView.at.x),  fbits(gView.at.y),  fbits(gView.at.z));
        /* Player position in HERO coords. Sweeping all four directions (mode 6)
         * makes the min/max of these the real traversable extent, hence the true
         * floor centre — measured, not derived from the spawn anchor, which the
         * capture log shows varies between runs (origin.z was 0 in one boot and
         * 171 in the next). */
        arena_export_dbg_cam(7, fbits(gPlayerObject->Pos.x),
                                fbits(gPlayerObject->Pos.y),
                                fbits(gPlayerObject->Pos.z));
        /* A1.2g: player state, plus D_8016E080 - the HAZARD CODE the game derives
         * from the surface under the player (func_80086AD0; 1 = the 0xF7 damage
         * tile at our corners). Logging it is how the suppression is verified
         * rather than assumed: a non-zero code with no damage and no stun proves
         * the tile is being DETECTED and the damage SUPPRESSED, where "nothing
         * bad happened" alone would prove nothing. */
        arena_export_dbg_cam(8, (s32)gPlayerObject->actionState,
                                (s32)gPlayerObject->unkA6,
                                (s32)gDebugHazardCode);

        /* A1.5 FAR CLIP. Tag 9 logs the LEVEL's authored ZFAR, then we raise it.
         * Measured: MAP_NITROS_1 already authors 8000 and the floor's far corner
         * is only ~2400 away, so this is NOT the framing problem - see the note
         * on arena_cam_zfar. Kept as a guard for maps that author a short plane.
         * Written here, after the update loop, because the decomp declares this
         * as a union whose bytes[] alias the per-object collision code writes
         * (69AA0.c:393). */
        arena_export_dbg_cam(9, fbits(GAME_ZFAR), fbits(arena_export_cam_zfar()), 0);
        GAME_ZFAR = arena_export_cam_zfar();

        /* A1.2g FLOOR GUARD. The arena's floor polygon is SMALLER than the sim's
         * collidable bounds, so the sim can walk player 0 off the edge; the
         * ground query then finds nothing and parks Pos.y at 30000 (measured -
         * actionState stays 4 throughout, so this is NOT the death path).
         * Containment until the sim geometry is re-matched to the real floor
         * (section 8.5a, a sim change). Runs AFTER the position drive below has
         * had a frame to settle, i.e. it corrects the previous frame's overrun. */
        if (arena_export_floor_guard(fbits(gPlayerObject->Pos.x),
                                     fbits(gPlayerObject->Pos.y),
                                     fbits(gPlayerObject->Pos.z))) {
            gPlayerObject->Pos.x = arena_export_floor_last_x();
            gPlayerObject->Pos.y = arena_export_floor_last_y();
            gPlayerObject->Pos.z = arena_export_floor_last_z();
        }
    }

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
        /* NEGATED: the two sides disagree on which way is "up" on the Y axis.
         *
         * The recomp maps W to GameInput::Y_AXIS_POS and computes
         * cur_y += Y_AXIS_POS - Y_AXIS_NEG (recompinput profiles.cpp:397), so W
         * arrives POSITIVE. The sim wants forward NEGATIVE - its own probe
         * harness says so outright: "sy MUST be -31, not +31: iatan2(Q(0),
         * Q(-31)) resolves to 0x0000" (tune_probes.c) - because yaw 0 means -Z
         * and the sim moves along (sin yaw, -cos yaw).
         *
         * Without this, W drove the player toward +Z, which the fixed camera
         * (eye at +Z; RenderDoc-confirmed depth = 2486.6 - 0.5z, so +Z is nearer
         * the camera) renders as moving DOWN the screen. That is the "W and S
         * are reversed" from the 2026-07-27 feel test, reproduced and measured
         * with probe mode 8.
         *
         * The negation belongs HERE, in the adapter: the sim's convention is
         * canonical, documented, covered by tests and folded into the pinned
         * hash. Bending the sim to suit one front end would be backwards. */
        s32 sy = -(s32)(gActiveContStickY * (31.0f / 80.0f));
        if (sx >  31) sx =  31;
        if (sx < -31) sx = -31;
        if (sy >  31) sy =  31;
        if (sy < -31) sy = -31;
        /* held_buttons, not gActiveContButton: the live copy was zeroed above so
         * the game's own player could not act on it. */
        s32 jump = (held_buttons & CONT_A) ? 1 : 0;
        s32 bomb = (held_buttons & CONT_B) ? 1 : 0;
        s32 set  = (held_buttons & CONT_G) ? 1 : 0;   /* Z trigger / Q key */
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
            arena_export_puppet_capture((s32)cx.u, (s32)cy.u, (s32)cz.u, gCurrentLevel);
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
        /* Facing vs travel check (probe modes 6/8). We copy the GAME's moveAngle,
         * which the walker computes from the RAW stick - but the sim is now fed a
         * NEGATED stick Y, so the two can disagree about which way the player is
         * pointing. Log both and compare rather than assume. */
        /* A1.2g exit-trigger hunt: watch gCurrentLevel and the level-transition
         * request vars. A probe run that ends on the stage select changed one of
         * these; logging them says WHICH and WHEN. */
        arena_export_dbg_cam(11, gCurrentLevel, (s32)D_8016E432, (s32)D_8016E434);
        arena_export_dbg_cam(10, fbits(gPlayerObject->moveAngle),
                                 fbits(arena_export_player_yaw(0)),
                                 (s32)gPlayerObject->actionState);

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
                /* HOLD, not one-shot. Measured 2026-07-27 with the [animw]
                 * window: the walker (func_80024744, which runs BEFORE this
                 * block every frame) re-asserts its own anim unconditionally, so
                 * a single trigger is replaced on the very next frame - with the
                 * camera on AND off, standing still AND moving. Re-assert while
                 * the native hold window is open and the walker has taken it. */
                if (set_edge || (arena_export_set_hold() && func_8001B880(0, 0) != 29))
                    func_8001C0EC(0, 0, 29, 1, (u32*)D_80115808);
                /* Auto-verify probe (temporary): burst-log the live anim index +
                 * frame so arena-soak.ps1 asserts idx->29 with the frame advancing. */
                arena_export_dbg_anim(func_8001B880(0, 0), (s32)func_8001B62C(0, 0),
                                      (s32)gPlayerObject->actionState);
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

        /* A1.2g FLOOR RASTER (probe mode 7 only; native gates it). Walk a grid
         * over the arena asking the game's own ground query where the floor is,
         * so ONE run maps the whole floor. This replaces walking the player into
         * the edge, which can only ever find one boundary point and then stalls
         * (the guard parks the player right there).
         *
         * Runs LAST in the frame, after func_80024744 and every object update,
         * because func_80078168 overwrites the shared collision-result globals -
         * nothing in this frame reads them after this point.
         *
         * The per-frame budget is a literal, not a native counter, so the patch
         * stays stateless. 384/frame covers the default 81x81 survey in ~17
         * frames and a full 201x201 edge-refinement pass in ~105. */
        if (arena_export_floor_raster_active()) {
            s32 n;
            for (n = 0; n < 384; n++) {
                if (!arena_export_floor_raster_next()) break;
                func_80078168(arena_export_floor_raster_px(),
                              arena_export_floor_raster_py(),
                              arena_export_floor_raster_pz());
                {
                    s32 sel = GQ_SEL;
                    union { f32 f; s32 i; } h;
                    h.f = GQ_H[sel];
                    arena_export_floor_raster_report(sel, h.i, GQ_TYPE[sel]);
                }
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
