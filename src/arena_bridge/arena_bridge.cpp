#include "arena_bridge.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>   /* getenv/atoi - A1.5 camera probe gate */
#include <cstring>   /* memcpy - float bit patterns across the export ABI */
#include <cmath>     /* sin/cos for the runtime camera pitch (native only!) */

#include "../../patches/arena_cam.h"   /* A1.5 pose constants (ARENA_CAM_AT_Y_LIFT) */

extern "C" {
#include "arena/arena_sim.h"
#include "arena/arena_tuning.h"   /* TUNE_BLAST_* for the blast-actor visuals */
}

namespace {
    bool       g_inited = false;
    ArenaState g_state;
    uint32_t   g_calls = 0;
    std::FILE* g_log = nullptr;   /* persistent proof evidence, agent-readable */
    bool       g_battle_mode = false;

    /* Render mapping (A1.2a): the sim's per-frame displacement, scaled to
     * Hero units. The render patch ADDS this delta to the player's live
     * position each frame (never teleports), so it moves per our physics
     * while the game keeps it grounded and the camera follows smoothly.
     * TODO(feel): scale tuned on-screen. */
    /* ONE source of truth for the scale: arena_cam.h, which is also what the sim's
     * arena_geom.h half-extents are derived from and what test_arena_cam.c checks
     * them against. A local literal here could drift from the sim silently. */
    float g_scale   = ARENA_RENDER_SCALE;   /* Hero units per sim unit */
    float g_scale_z = ARENA_RENDER_SCALE;   /* square arena -> same on both axes */

/* The bomb mesh's origin is its CENTER; the game's own bomb rests with it 30
 * world units above the floor (69AA0.c:359). Applied to bomb AND blast wy so
 * settled bombs sit ON the floor and the explosion spawns at bomb-centre
 * height, exactly like the game's own detonation. */
#define BOMB_MESH_REST_LIFT 30.0f
    float g_render_dx = 0.0f;  /* last tick's displacement, scaled */
    float g_render_dz = 0.0f;
    float g_render_yaw = 0.0f;

    /* A1.2b puppet actors (players 1-3). State lives here because patches must
     * be stateless. The world anchor is FROZEN at spawn (not the live player
     * object) so actors follow the sim, not the player's own movement (which
     * would mirror). g_ref_s* is player 0's sim pos at capture. */
    bool  g_puppets_ready = false;
    float g_origin_x = 0.0f, g_origin_y = 0.0f, g_origin_z = 0.0f;  /* frozen world anchor */
    int   g_level_at_capture = -1;   /* gCurrentLevel when the anchor was taken */
    float g_ref_sx = 0.0f, g_ref_sy = 0.0f, g_ref_sz = 0.0f;         /* frozen sim ref (p0) */
    int   g_puppet_slot[ARENA_MAX_PLAYERS] = { -1, -1, -1, -1 };
    /* A1.2c: 16 bomb actors, 1:1 with g_state.bombs[0..15]. */
    int   g_bomb_slot[ARENA_MAX_BOMBS] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };
    /* A1.2c slice 2: blast liveness last frame (edge detect) + spike latch. */
    bool  g_blast_prev[ARENA_MAX_BLASTS] = {};
    bool  g_spike_done = false;
    /* Spike v3: one effect per Q press (edge tracked here from tick buttons). */
    bool  g_set_prev = false;
    bool  g_spike_pending = false;
    int   g_spike_idx = 0;
    /* A1.2c slice 2 fallback: 4 pooled blast actors (bomb mesh, scaled). */
    int   g_blast_slot[4] = { -1, -1, -1, -1 };
    /* A1.4 set-anim edge: a bomb going FREE->SETTLED in one tick (owner==i) is a
     * SET event (a set places directly to SETTLED at the player's feet, sim
     * arena_sim.c:240; thrown bombs pass through AIRBORNE first, so they don't
     * match). Prev-state tracked here (patches are stateless) mirroring
     * g_blast_prev; the edge is latched per player in tick_input and read-and-
     * cleared by arena_set_new so it fires once per event. Kick has NO game anim
     * (integration notes §8.5c) so only the set edge is exposed. Pure read of sim
     * state — no gameplay change, pinned hash 5f500fcb holds. */
    uint8_t g_bomb_prev_state[ARENA_MAX_BOMBS] = {};
    int     g_set_edge[ARENA_MAX_PLAYERS] = { 0, 0, 0, 0 };

    /* Frames since player 0's last set edge, or -1 when no window is open. Drives
     * the [animw] diagnostic window in arena_dbg_anim. Latched in tick_input
     * because arena_set_new() consumes the edge before the render patch could
     * pass it back to us. */
    int g_anim_since_set = -1;

    /* ACTION POSE window. One mechanism for both set and kick: an action latches
     * the index to play and a frame budget; the patch triggers it once and the
     * func_8001C0EC walker gate (§8.23) lets the clip play untouched for the
     * window's duration. */
    int g_pose_anim   = -1;
    int g_pose_frames = 0;

/* Window length in frames. Default 10 = clip 29's exact length (measured
 * 2026-07-30: the frame counter wraps 18 -> 0 at +2/frame), so the game's own
 * drop clip plays EXACTLY ONCE - the real game's drop is one snappy play-
 * through, and the old 24-frame window looped it 2.4x ("doesn't move as fast
 * as I remember", feel test 2026-07-30). ARENA_POSE_FRAMES overrides when
 * experimenting with longer clips (41 needs ~24). */
static int arena_pose_frames(void) {
    static const int n = []() {
        const char* v = std::getenv("ARENA_POSE_FRAMES");
        if (v) { int k = std::atoi(v); if (k > 0 && k <= 120) return k; }
        return 10;
    }();
    return n;
}

/* KICK window length, separate from the set window: the oracle measured the
 * kick clip (33) at 18 frames vs the set clip (31) at 10 — one shared length
 * cut the kick off 10/18 through and the walker stomped the rest (feel round
 * 4: "the kick animation doesn't look correct"). Default = the golden
 * kick_anim_frames; ARENA_KICK_POSE_FRAMES overrides for experiments. */
static int arena_kick_pose_frames(void) {
    static const int n = []() {
        const char* v = std::getenv("ARENA_KICK_POSE_FRAMES");
        if (v) { int k = std::atoi(v); if (k > 0 && k <= 120) return k; }
        return 18;
    }();
    return n;
}

    float qf(int32_t q) { return (float)q / 4096.0f; }  /* Q20.12 -> float */

    void ensure_init() {
        if (!g_inited) {
            arena_init(&g_state, 0, 4, 0xB0BB1E5u);  /* 4 players; round won't end */
            g_inited = true;
            g_log = std::fopen("arena_bridge.log", "w");
        }
    }

    void proof(const char* fmt, unsigned a, unsigned b) {
        std::printf(fmt, a, b);
        std::fflush(stdout);
        if (g_log) { std::fprintf(g_log, fmt, a, b); std::fflush(g_log); }
    }
}

extern "C" void arena_bridge_tick(void) {
    ensure_init();
    if (g_battle_mode) return;   /* battle: the render patch drives the tick */
    /* non-battle proof-of-life: real neutral (raw 0 != neutral, see tick_input) */
    ArenaInput n = arena_input_pack(0, 0, 0, 0, 0);
    const ArenaInput neutral[ARENA_MAX_PLAYERS] = { n, n, n, n };
    arena_tick(&g_state, neutral);
    if ((++g_calls % 60u) == 0u)
        proof("[arena] tick %u hash %08x\n", g_state.tick, arena_hash(&g_state));
}

/* buttons packs jump|bomb|set into one arg (the export ABI only passes 4 args,
 * so we can't take them separately): bit0 jump, bit1 bomb, bit2 set/kick. */
/* A1.2f soak: the render routine's first call here = the battle level is
 * running — the signal the frontend mash keys off (stopping at puppet_ready
 * instead let the mash's START presses PAUSE the game in-level, which halts
 * the update dispatcher and freezes the spawn warmup: a pause/unpause
 * livelock, seen on screen 2026-07-22). */
static bool g_routine_seen = false;
extern "C" int arena_routine_seen(void) { return g_routine_seen ? 1 : 0; }

/* A1.2e stability: gate for restoring gDebugRoutine1 after the level-enter
 * load window. The runtime's func_map mutates during overlay load/unload and
 * indirect-call lookups on the game thread RACE it (same mechanism as the
 * §8.9 print crashes; 7 symbolized dumps all name the draw dispatcher's
 * gDebugRoutine1() call). The patch keeps routine1 NULL through the window
 * (dispatcher tolerates NULL) and restores it once this opens (~0.5s of
 * routine invocations). Process-lifetime, battle-agnostic. */
static int g_draw_warmup = 0;
extern "C" int arena_draw_gate(void) {
    if (g_draw_warmup < 30) { g_draw_warmup++; return 0; }
    return 1;
}
/* Reset at EVERY level-enter (func_800824A8 patch): the load window recurs on
 * every level transition — a process-lifetime gate left the hook restored
 * instantly on RE-entry and the race fired again (probe crash 2026-07-22,
 * player ran into the boss room's level-exit trigger). */
extern "C" void arena_draw_gate_reset(void) { g_draw_warmup = 0; }

/* BATTLE BUTTON OWNERSHIP (2026-08-01): the input callback latches the real
 * button mask here, then strips the sim's verbs (B/Z/R) from what the game
 * receives - at the POLL, before any game code can copy or edge-detect them.
 * The render patch builds the sim's jump/bomb/set from this latch. */
static int g_latched_buttons = 0;
extern "C" void arena_latch_buttons(int held) { g_latched_buttons = held; }
extern "C" int  arena_latched_buttons(void)   { return g_latched_buttons; }

extern "C" void arena_bridge_tick_input(int sx, int sy, int buttons) {
    g_routine_seen = true;
    ensure_init();
    Vec3q before = g_state.players[0].pos;
    /* Neutral is arena_input_pack(0,...) = 0x820, NOT raw 0 (raw 0 decodes to a
     * full -32,-32 stick). Idle players 1-3 must get real neutral or they run. */
    ArenaInput neutral = arena_input_pack(0, 0, 0, 0, 0);
    ArenaInput in[ARENA_MAX_PLAYERS] = { neutral, neutral, neutral, neutral };
    in[0] = arena_input_pack(sx, sy, buttons & 1, (buttons >> 1) & 1, (buttons >> 2) & 1);
    arena_tick(&g_state, in);
    Vec3q after = g_state.players[0].pos;
    g_render_dx  = qf(after.x - before.x) * g_scale;
    g_render_dz  = qf(after.z - before.z) * g_scale_z;
    g_render_yaw = (float)g_state.players[0].yaw * (360.0f / 65536.0f);

    /* A1.4: latch a set edge for each player whose bomb went FREE->SETTLED this
     * tick (the set-placement transition). arena_set_new(i) reads-and-clears. */
    for (int b = 0; b < ARENA_MAX_BOMBS; b++) {
        uint8_t now = g_state.bombs[b].state;
        /* v15 throw evidence: log the HELD->AIRBORNE edge tick-stamped, so the
         * throw probe can prove impact detonation by latency ([blastvis] tick
         * minus [throw] tick << TUNE_FUSE_TICKS). */
        if (g_bomb_prev_state[b] == BSTATE_HELD && now == BSTATE_AIRBORNE) {
            if (g_log) {
                std::fprintf(g_log, "[throw] t%u bomb=%d owner=%d\n",
                             g_state.tick, b, (int)g_state.bombs[b].owner);
                std::fflush(g_log);
            }
        }
        if (g_bomb_prev_state[b] == BSTATE_FREE && now == BSTATE_SETTLED) {
            int o = g_state.bombs[b].owner;
            if (o >= 0 && o < ARENA_MAX_PLAYERS) g_set_edge[o] = 1;
            if (o == 0) {
                g_anim_since_set = 0;           /* open the [animw] window */
                /* STANDING-ONLY pose (2026-07-31): a pose played while the body
                 * keeps gliding at run speed reads broken no matter which clip
                 * it is — and the classic games play no set pose while running
                 * (locomotion continues). Play the drop clip only when the
                 * player is actually standing; ARENA_POSE_MOVING=1 restores
                 * always-pose for A/B. */
                static const bool pose_moving = []() {
                    const char* v = std::getenv("ARENA_POSE_MOVING");
                    return v && v[0] == '1';
                }();
                float vx = qf(g_state.players[0].vel.x);
                float vz = qf(g_state.players[0].vel.z);
                bool standing = (vx * vx + vz * vz) < 0.0025f; /* < 0.05 su/t */
                if (standing || pose_moving) {
                    g_pose_anim   = arena_set_anim_index();
                    g_pose_frames = arena_pose_frames();
                }
            }
            if (o == 0 && g_log) {   /* [setdbg]: diagnose set-bomb placement + render slot */
                std::fprintf(g_log, "[setdbg] t%u bi=%d live=%d simY=%.3f wy=%.2f originY=%.2f slot=%d\n",
                    g_state.tick, b, g_state.players[0].live_bombs,
                    qf(g_state.bombs[b].pos.y), arena_bomb_wy(b), g_origin_y, g_bomb_slot[b]);
                std::fflush(g_log);
            }
        }
        /* KICK edge: SETTLED -> SLIDING. The sim stamps the kicker into `bounced`
         * as idx+1 (arena_sim.c, walk-in kick), so the actor is known without
         * any new sim state. Pure read - no gameplay change, hash untouched.
         *
         * §8.5c called a kick animation non-existent ("Bomberman Hero offense is
         * grab->throw"), and that was wrong: the 2026-07-27 animation contact
         * sheet shows clear kick poses at indices 32/33. */
        if (g_bomb_prev_state[b] == BSTATE_SETTLED && now == BSTATE_SLIDING) {
            int kicker = (int)g_state.bombs[b].bounced - 1;
            if (kicker == 0) {
                g_pose_anim   = arena_kick_anim_index();
                g_pose_frames = arena_kick_pose_frames();
                if (g_log) {
                    std::fprintf(g_log, "[kick] pose idx=%d bomb=%d\n", g_pose_anim, b);
                    std::fflush(g_log);
                }
            }
        }
        g_bomb_prev_state[b] = now;
    }

    /* A1.2b diag: are the sim's players 1-3 holding their corners, or does the
     * convergence come from the game's object update? Log all 4 once a second. */
    static uint32_t n = 0;
    if ((++n % 60u) == 0u && g_log) {
        std::fprintf(g_log,
            /* phase/shrink/alive/state make a "the player got stuck" report
             * self-diagnosing: PHASE_ROUND_END freezes movement outright, sudden
             * death creeps the walls in, and a dead player simply stops. Without
             * these the log shows a position that stops changing and nothing
             * about why. */
            "[simpos] t%u ph=%d shr=%d alive=%d st0=%d p0(%.2f,%.2f) p1(%.2f,%.2f) "
            "p2(%.2f,%.2f) p3(%.2f,%.2f)\n",
            g_state.tick,
            (int)g_state.phase, (int)g_state.shrink_step,
            (int)((g_state.players[0].state != PSTATE_DEAD) +
                  (g_state.players[1].state != PSTATE_DEAD) +
                  (g_state.players[2].state != PSTATE_DEAD) +
                  (g_state.players[3].state != PSTATE_DEAD)),
            (int)g_state.players[0].state,
            qf(g_state.players[0].pos.x), qf(g_state.players[0].pos.z),
            qf(g_state.players[1].pos.x), qf(g_state.players[1].pos.z),
            qf(g_state.players[2].pos.x), qf(g_state.players[2].pos.z),
            qf(g_state.players[3].pos.x), qf(g_state.players[3].pos.z));
        /* A1.2c telemetry: live-bomb count (confirms throw/set produce bombs). */
        int nb = 0;
        for (int bi = 0; bi < ARENA_MAX_BOMBS; bi++)
            if (g_state.bombs[bi].state != BSTATE_FREE) nb++;
        std::fprintf(g_log, "[bombs] t%u live=%d p0.held=%d\n",
                     g_state.tick, nb, g_state.players[0].held_bomb);
        std::fflush(g_log);
    }
    /* Early-frame input forensics: any nonzero buttons in the first ~3s means
     * gActiveContButton carries stale/garbage bits during the fade. */
    if (g_state.tick <= 180 && buttons != 0 && g_log) {
        std::fprintf(g_log, "[earlybtn] t%u buttons=0x%x\n", g_state.tick, buttons);
        std::fflush(g_log);
    }
    /* Spike v3: arm one step per set/kick PRESS edge (tick-guarded). */
    {
        bool set_now = ((buttons >> 2) & 1) != 0;
        if (set_now && !g_set_prev && g_state.tick > 120) g_spike_pending = true;
        g_set_prev = set_now;
    }
}

/* getters return the last tick's scaled displacement (dx/dz) and abs yaw;
 * i is ignored for A1.2a (only player 0 is driven). */
extern "C" float arena_get_player_x(int i)       { (void)i; return g_render_dx; }
extern "C" float arena_get_player_y(int i)       { (void)i; return 0.0f; }  /* Y left to game */
extern "C" float arena_get_player_z(int i)       { (void)i; return g_render_dz; }
extern "C" float arena_get_player_yaw_deg(int i) { (void)i; return g_render_yaw; }

/* Per-index actor placement (A1.2b): the scaled XZ offset of player i from
 * player 0's sim position, plus yaw. The patch anchors these to the live player
 * object so the other actors sit at their sim positions (self-correcting, no
 * absolute origin). */
extern "C" float arena_get_bomber_off_x(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return 0.0f;
    return qf(g_state.players[i].pos.x - g_state.players[0].pos.x) * g_scale;
}
extern "C" float arena_get_bomber_off_z(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return 0.0f;
    return qf(g_state.players[i].pos.z - g_state.players[0].pos.z) * g_scale_z;
}
extern "C" float arena_get_bomber_yaw(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return 0.0f;
    return (float)g_state.players[i].yaw * (360.0f / 65536.0f);
}

/* A1.2b diagnostic: the patch reads gObjects[i] and calls this per slot; we log
 * to arena_bridge.log (persistent, agent-readable) so we can see free slots +
 * resident models in the Battle Room without recomp_printf. */
extern "C" void arena_dbg_dump(int i, int objID, int actionState) {
    static int done = 0;                 /* native holds the "log once" state */
    if (done) return;
    std::printf("[arena_dbg] obj[%d] objID=%d actionState=%d\n", i, objID, actionState);
    std::fflush(stdout);
    if (g_log) {
        std::fprintf(g_log, "[arena_dbg] obj[%d] objID=%d actionState=%d\n", i, objID, actionState);
        std::fflush(g_log);
    }
    if (i >= 15) done = 1;               /* one full 16-row snapshot captured */
}

extern "C" int arena_bridge_battle_active(void) {
    return g_battle_mode ? 1 : 0;
}

extern "C" void arena_bridge_set_battle_mode(int on) {
    g_battle_mode = (on != 0);
    std::printf("[arena] battle mode -> %s\n", g_battle_mode ? "ON" : "OFF");
    std::fflush(stdout);
    if (g_log) { std::fprintf(g_log, "[arena] battle mode -> %s\n",
                              g_battle_mode ? "ON" : "OFF"); std::fflush(g_log); }
}

/* A1.2b: freeze the world anchor (the player's spawn-frame Pos, passed as u32
 * bit patterns to dodge float-arg ABI) and the sim reference (player 0's sim
 * pos now). Idempotent — only the first call takes effect. */
extern "C" void arena_puppet_capture(uint32_t bx, uint32_t by, uint32_t bz, int level) {
    if (g_puppets_ready) return;
    union { uint32_t u; float f; } ux, uy, uz;
    ux.u = bx; uy.u = by; uz.u = bz;

    /* A1.2g ANCHOR FIX (2026-07-26). The frame is now pinned to the MEASURED
     * floor, not to wherever the player happened to be standing:
     *
     *     hero = FLOOR_CENTRE + sim * scale        (ref_s = the sim's centre, 0)
     *
     * The old form anchored the player's spawn Pos to the sim's SPAWN, which put
     * the sim's arena CENTRE at Hero (906,422) instead of (0,0) - measured, from
     * the same run that produced the floor map: origin(0,240,0), ref_s(-7.55,
     * -3.52). The sim's x range then mapped to Hero [-42,1854] against a floor of
     * [-950,950], so more than half the arena hung over the edge. THAT is the
     * A1.2g "fall": the player was driven off the floor polygon and the ground
     * query returned its no-floor sentinel. It also explains why the coarse walk
     * probe only ever saw x in [0..353], and why raising ARENA_CAM_DIST pushed
     * the arena off-centre instead of just framing more of it.
     *
     * Anchoring on measured constants also removes the per-run drift the capture
     * had by construction (origin.z logged as 0 one boot and 171 the next) - the
     * floor does not move between boots, but the player's spawn did.
     *
     * The captured Pos is still logged, as a CHECK: if it stops sitting on the
     * measured floor centre, either the map changed or the warp landed somewhere
     * else, and that should be visible rather than silently absorbed. */
    g_origin_x = ARENA_FLOOR_CX;
    g_origin_y = ARENA_FLOOR_Y;
    g_origin_z = ARENA_FLOOR_CZ;
    g_ref_sx = 0.0f;   /* sim arena centre <-> measured floor centre */
    g_ref_sy = 0.0f;   /* sim ground (y=0)  <-> measured floor height */
    g_ref_sz = 0.0f;
    g_level_at_capture = level;
    g_puppets_ready = true;
    if (g_log) {
        /* gCurrentLevel is logged because a floor measurement that doesn't record
         * WHICH MAP it measured is a trap: the render routine runs in every level,
         * so [capture] fires wherever the draw gate happens to open — including
         * the stage-select map if the arena warp hasn't landed yet. Anything
         * derived from these coordinates is only valid for level ARENA_WARP_MAP. */
        std::fprintf(g_log,
            "[capture] level=%d anchor origin(%.2f,%.2f,%.2f) ref_s(0,0,0) scale=%.1f | "
            "player was at (%.2f,%.2f,%.2f) d=(%.2f,%.2f)\n",
            level, g_origin_x, g_origin_y, g_origin_z, g_scale,
            ux.f, uy.f, uz.f, ux.f - ARENA_FLOOR_CX, uz.f - ARENA_FLOOR_CZ);
        std::fflush(g_log);
    }
}
extern "C" int  arena_puppet_ready(void)              { return g_puppets_ready ? 1 : 0; }

/* A1.2d: spawn warmup gate. The render routine's FIRST invocation happens
 * synchronously inside the level-enter function (func_800824A8 ->
 * func_80000964 -> gDebugRoutine2), before the game heap is serviceable —
 * calling the anim-instance path there hangs in malloc_game (symbolized
 * stack, 2026-07-22). Gate the one-shot spawn block on ~1.5s of routine
 * invocations so it runs only once the level loop is actually pumping.
 * Process-lifetime latch, same as the rest of the puppet state. */
static int g_spawn_warmup = 0;
extern "C" int arena_spawn_gate(void) {
    if (g_spawn_warmup < 90) { g_spawn_warmup++; return 0; }
    return 1;
}
extern "C" void arena_puppet_set_slot(int i, int slot){ if (i >= 0 && i < ARENA_MAX_PLAYERS) g_puppet_slot[i] = slot; }
extern "C" int  arena_puppet_get_slot(int i)          { return (i >= 0 && i < ARENA_MAX_PLAYERS) ? g_puppet_slot[i] : -1; }

/* World placement of puppet i, anchored to the FROZEN origin. */
extern "C" float arena_puppet_wx(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return g_origin_x;
    return g_origin_x + (qf(g_state.players[i].pos.x) - g_ref_sx) * g_scale;
}
extern "C" float arena_puppet_wy(int i) { (void)i; return g_origin_y; }
extern "C" float arena_puppet_wz(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return g_origin_z;
    return g_origin_z + (qf(g_state.players[i].pos.z) - g_ref_sz) * g_scale_z;
}
extern "C" float arena_puppet_yaw(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return 0.0f;
    return (float)g_state.players[i].yaw * (360.0f / 65536.0f);
}

/* A1.2b debug: log a tagged u32 (e.g. a gFileArray ptr / a free slot) to the
 * persistent, agent-readable arena_bridge.log. Temporary evidence-gathering. */
extern "C" void arena_dbg_u32(int tag, unsigned val) {
    std::printf("[dbg] tag=%d val=0x%08x (%u)\n", tag, val, val);
    std::fflush(stdout);
    if (g_log) { std::fprintf(g_log, "[dbg] tag=%d val=0x%08x (%u)\n", tag, val, val); std::fflush(g_log); }
}

/* A1.2c bombs: read g_state.bombs[i], mapped through the SAME frozen frame as
 * the puppets (bombs live in the same sim space). Y is mapped too so the throw
 * arc shows (unlike players, whose Y is left to the game). */
extern "C" int arena_bomb_active(int i) {
    if (i < 0 || i >= ARENA_MAX_BOMBS) return 0;
    return g_state.bombs[i].state != BSTATE_FREE ? 1 : 0;
}
extern "C" float arena_bomb_wx(int i) {
    if (i < 0 || i >= ARENA_MAX_BOMBS) return g_origin_x;
    return g_origin_x + (qf(g_state.bombs[i].pos.x) - g_ref_sx) * g_scale;
}
extern "C" float arena_bomb_wy(int i) {
    if (i < 0 || i >= ARENA_MAX_BOMBS) return g_origin_y + BOMB_MESH_REST_LIFT;
    float w = g_origin_y + BOMB_MESH_REST_LIFT
              + (qf(g_state.bombs[i].pos.y) - g_ref_sy) * g_scale;   /* arc height */
    /* A1.2e: floor-clamp. ref_sy is sampled at capture; any positive ref puts
     * ground bombs (sim y=0) BELOW the game floor — set bombs were invisible
     * while thrown bombs (arcing above) showed (user log 2026-07-22: Q presses
     * reached the sim, live=1..3, nothing on screen). origin_y = the grounded
     * player height at capture = the floor (raster-confirmed 2026-07-30:
     * ground h=[240..240] with origin_y 240).
     *
     * BOMB_MESH_REST_LIFT: the bomb mesh's origin is its CENTER, and the game's
     * own bomb rests at floor + 30 (69AA0.c:359, `sp44 = D_80177760[1] + 30.0f`).
     * Without the lift our settled bombs rendered half-sunk (feel test
     * 2026-07-30: "the bomb appears slightly inside the floor"). */
    if (w < g_origin_y + BOMB_MESH_REST_LIFT) w = g_origin_y + BOMB_MESH_REST_LIFT;
    return w;
}
extern "C" float arena_bomb_wz(int i) {
    if (i < 0 || i >= ARENA_MAX_BOMBS) return g_origin_z;
    return g_origin_z + (qf(g_state.bombs[i].pos.z) - g_ref_sz) * g_scale_z;
}
extern "C" void arena_bomb_set_slot(int i, int slot) { if (i >= 0 && i < ARENA_MAX_BOMBS) g_bomb_slot[i] = slot; }
extern "C" int  arena_bomb_get_slot(int i)           { return (i >= 0 && i < ARENA_MAX_BOMBS) ? g_bomb_slot[i] : -1; }

/* 1 if `slot` is one of our actors (player puppet or bomb) — for the boss sweep. */
extern "C" int arena_is_actor_slot(int slot) {
    for (int i = 1; i < ARENA_MAX_PLAYERS; i++) if (g_puppet_slot[i] == slot) return 1;
    for (int i = 0; i < ARENA_MAX_BOMBS;   i++) if (g_bomb_slot[i]   == slot) return 1;
    for (int i = 0; i < 4;                 i++) if (g_blast_slot[i]  == slot) return 1;
    return 0;
}

/* A1.2c slice 2: blasts. Edge-detect per index (the patch calls blast_new for
 * ALL 16 indices every frame, so prev-tracking inside the getter is sound). */
extern "C" int arena_spike_once(void) {
    /* Guard: ignore (WITHOUT latching) during the first ~2s in-level —
     * gActiveContButton may carry garbage/stale bits during the load fade,
     * which must not auto-fire the spike. */
    if (g_state.tick <= 120) return 0;
    if (g_spike_done) return 0;
    g_spike_done = true;
    return 1;
}
/* Sweep gate: 1 during the entry window only. The boss is deactivated in the
 * first frames and stays down; an every-frame sweep also kills any effect
 * objects the game spawns into [14..77] (invisible effects + delayed crash). */
extern "C" int arena_sweep_active(void) {
    return g_state.tick < 300 ? 1 : 0;   /* ~5s entry window */
}

/* Spike v3: returns the next spike step index (0..9) exactly once per armed
 * Q-press edge, else -1. The patch maps the index to an effect ID. */
extern "C" int arena_spike_next(void) {
    if (!g_spike_pending) return -1;
    g_spike_pending = false;
    if (g_spike_idx >= 10) return -1;
    return g_spike_idx++;
}
extern "C" int arena_blast_new(int i) {
    if (i < 0 || i >= ARENA_MAX_BLASTS) return 0;
    bool alive = g_state.blasts[i].ttl != 0;
    bool was   = g_blast_prev[i];
    g_blast_prev[i] = alive;
    return (alive && !was) ? 1 : 0;
}
extern "C" float arena_blast_wx(int i) {
    if (i < 0 || i >= ARENA_MAX_BLASTS) return g_origin_x;
    return g_origin_x + (qf(g_state.blasts[i].center.x) - g_ref_sx) * g_scale;
}
extern "C" float arena_blast_wy(int i) {
    if (i < 0 || i >= ARENA_MAX_BLASTS) return g_origin_y + BOMB_MESH_REST_LIFT;
    float w = g_origin_y + BOMB_MESH_REST_LIFT
              + (qf(g_state.blasts[i].center.y) - g_ref_sy) * g_scale;
    if (w < g_origin_y + BOMB_MESH_REST_LIFT) w = g_origin_y + BOMB_MESH_REST_LIFT;
    return w;   /* explosion at bomb-CENTER height, like the game's own spawn */
}
extern "C" float arena_blast_wz(int i) {
    if (i < 0 || i >= ARENA_MAX_BLASTS) return g_origin_z;
    return g_origin_z + (qf(g_state.blasts[i].center.z) - g_ref_sz) * g_scale_z;
}
extern "C" int arena_blast_active(int i) {
    if (i < 0 || i >= ARENA_MAX_BLASTS) return 0;
    return g_state.blasts[i].ttl != 0 ? 1 : 0;
}
/* World-units blast radius right now (grows over TUNE_BLAST_GROW_TICKS). */
extern "C" float arena_blast_wr(int i) {
    if (i < 0 || i >= ARENA_MAX_BLASTS) return 0.0f;
    uint16_t rt = g_state.blasts[i].radius_t;
    if (rt > TUNE_BLAST_GROW_TICKS) rt = TUNE_BLAST_GROW_TICKS;
    return qf(TUNE_BLAST_RADIUS) * ((float)rt / (float)TUNE_BLAST_GROW_TICKS) * g_scale;
}
extern "C" void arena_blastactor_set_slot(int i, int slot) { if (i >= 0 && i < 4) g_blast_slot[i] = slot; }
extern "C" int  arena_blastactor_get_slot(int i)           { return (i >= 0 && i < 4) ? g_blast_slot[i] : -1; }

/* A1.4: 1 exactly once per player-i set-bomb event (FREE->SETTLED edge latched
 * in tick_input). Read-and-clear so it fires once. Kick has no game anim. */
extern "C" int arena_set_new(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return 0;
    int e = g_set_edge[i];
    g_set_edge[i] = 0;
    return e;
}
/* A1.4 auto-verify: burst-log the player's live anim (index+frame) for a few
 * frames on each index CHANGE, so arena-soak.ps1 can assert idx->29 (the set
 * pose) with the frame counter advancing, without flooding every frame.
 * Temporary probe evidence, mirrors arena_dbg_u32. */
extern "C" void arena_dbg_anim(int idx, int frame, int state) {
    static int last  = -1;
    static int burst = 0;
    if (idx != last) { last = idx; burst = 8; }

    /* Two logs, deliberately.
     *
     * [anim] is the ORIGINAL burst - 8 frames from an index change - and the
     * A1.4 gate keys off it. Keep its format byte-identical so the gate keeps
     * meaning the same thing.
     *
     * [animw] is a WINDOW: every frame for 40 frames after a set edge, with the
     * player's actionState. The burst alone cannot diagnose this regression,
     * because the whole difference between PASS and FAIL is the pose surviving
     * 3 frames instead of 2 - the burst shows you the symptom and nothing about
     * the surrounding state. */
    if (burst > 0) {
        burst--;
        std::printf("[anim] idx=%d frame=%d\n", idx, frame);
        std::fflush(stdout);
        if (g_log) { std::fprintf(g_log, "[anim] idx=%d frame=%d\n", idx, frame); std::fflush(g_log); }
    }

    if (g_pose_frames > 0) g_pose_frames--;

    if (g_anim_since_set >= 0 && g_anim_since_set < 40) {
        if (g_log) {
            std::fprintf(g_log, "[animw] +%02d idx=%d frame=%d state=%d\n",
                         g_anim_since_set, idx, frame, state);
            std::fflush(g_log);
        }
        g_anim_since_set++;
    }
}

/* Explosion visual evidence: the patch reports every blast->actor drive frame
 * ([blastvis]), so the soak can assert both that a detonation DREW something
 * and that its radius GREW (-Rising on the wr integer part). Bounded output:
 * TUNE_BLAST_TTL (20) lines per blast, every write flushed (crash-safe). The
 * radius crosses as a float BIT PATTERN - the export ABI takes no float args. */
extern "C" void arena_dbg_blast(int k, int slot, int wrbits) {
    union { int i; float f; } wr; wr.i = wrbits;
    if (g_log) {
        /* tick appended LAST so existing gate regexes keep matching */
        std::fprintf(g_log, "[blastvis] k=%d slot=%d wr=%.1f t%u\n",
                     k, slot, wr.f, g_state.tick);
        std::fflush(g_log);
    }
}

/* ---- Single-player oracle (spec 2026-08-01) ---------------------------- */
/* One monotonic counter n stamps every oracle line: it ticks once per game
 * frame (arena_oracle_frame runs FIRST in the patch's oracle block), so all
 * timing goldens live in one clock regardless of which channel logged them. */
static bool     g_oracle_seen = false;
static unsigned g_oracle_n    = 0;

extern "C" int arena_oracle_mode(void) {
    static const bool on = []() {
        const char* v = std::getenv("ARENA_ORACLE");
        return v && v[0] == '1'; }();
    return on ? 1 : 0;
}
extern "C" int arena_oracle_seen(void) { return g_oracle_seen ? 1 : 0; }

extern "C" void arena_oracle_phase(const char* name) {
    if (!arena_oracle_mode()) return;
    ensure_init();
    if (g_log) { std::fprintf(g_log, "[oracle] phase=%s n=%u\n", name, g_oracle_n);
                 std::fflush(g_log); }
}

/* Heartbeat: in-level signal (mash-stop) + floor/player Y from the game's own
 * ground query. Logged 1-in-30 (readable log); n ticks EVERY call. */
extern "C" void arena_oracle_frame(int level, int playerValid, int floorYbits, int playerYbits) {
    if (!arena_oracle_mode()) return;
    ensure_init();
    g_oracle_seen = true;
    g_oracle_n++;
    union { int i; float f; } fy, py; fy.i = floorYbits; py.i = playerYbits;
    if (g_log && (g_oracle_n % 30u) == 1u) {
        std::fprintf(g_log, "[oracle] frame n=%u level=%d player=%d floorY=%.2f playerY=%.2f\n",
                     g_oracle_n, level, playerValid, fy.f, py.f);
        std::fflush(g_log);
    }
}

/* Player anim, every frame while in-level (the parser needs the frame ramp to
 * measure clip length). frame is f32 bits (func_8001B62C returns f32). */
extern "C" void arena_oracle_anim(int idx, int framebits, int state) {
    if (!arena_oracle_mode()) return;
    union { int i; float f; } fr; fr.i = framebits;
    if (g_log) {
        std::fprintf(g_log, "[oracle-anim] n=%u idx=%d frame=%.1f state=%d\n",
                     g_oracle_n, idx, fr.f, state);
        std::fflush(g_log);
    }
}

/* Game object watch. slot_state = (slot << 16) | (actionState & 0xFFFF).
 * Slots 2..5 = the game's bomb pool -> [oracle-bomb]; 6..13 = the explosion
 * pool (func_8007E76C spawns there) -> [oracle-blast]. The patch only reports
 * ACTIVE objects, so lines are sparse. */
extern "C" void arena_oracle_obj(int slot_state, int xbits, int ybits, int zbits) {
    if (!arena_oracle_mode()) return;
    int slot = (slot_state >> 16) & 0xFFFF, st = slot_state & 0xFFFF;
    union { int i; float f; } x, y, z; x.i = xbits; y.i = ybits; z.i = zbits;
    if (g_log) {
        std::fprintf(g_log, "[oracle-%s] n=%u slot=%d state=%d pos=(%.1f,%.1f,%.1f)\n",
                     slot >= 6 ? "blast" : "bomb", g_oracle_n, slot, st, x.f, y.f, z.f);
        std::fflush(g_log);
    }
}

/* A1.5: arena centre in Hero world coords, for gView.at.
 *
 * Deliberately reuses the SAME frozen-origin mapping as the puppets: if the
 * origin capture is ever wrong, the camera is wrong in the same direction as the
 * actors, which keeps the picture self-consistent and makes the error obvious
 * instead of confusing. Sim (0,0) is the arena centre. */
extern "C" float arena_cam_at_dx(void);
extern "C" float arena_cam_at_dz(void);
extern "C" int   arena_cam_follow(void);
extern "C" float arena_cam_at_x(void) {
    if (arena_cam_follow()) return arena_puppet_wx(0);   /* debug: track the player */
    return g_origin_x + (0.0f - g_ref_sx) * g_scale   + arena_cam_at_dx();
}
extern "C" float arena_cam_at_z(void) {
    if (arena_cam_follow()) return arena_puppet_wz(0);
    return g_origin_z + (0.0f - g_ref_sz) * g_scale_z + arena_cam_at_dz();
}
extern "C" float arena_cam_at_y(void) { return g_origin_y + ARENA_CAM_AT_Y_LIFT; }

/* Calibration offsets for the camera target (ARENA_CAM_AT_DX / _DZ, default 0).
 * Shifting `at` by a KNOWN world amount and measuring how far the picture moves
 * is the only way to check our world->screen model without guessing at pixels:
 * if the image shifts by the predicted amount in the predicted direction, the
 * model is right and any remaining confusion is about what we're looking at. */
namespace {
    float cam_env(const char* name) {
        const char* v = std::getenv(name);
        return v ? (float)std::atof(v) : 0.0f;
    }
}
extern "C" float arena_cam_at_dx(void) { static const float v = cam_env("ARENA_CAM_AT_DX"); return v; }
extern "C" float arena_cam_at_dz(void) { static const float v = cam_env("ARENA_CAM_AT_DZ"); return v; }

/* Camera distance, overridable at runtime with ARENA_CAM_DIST. arena_cam.h says
 * this value "is iterated by screenshot" — but it lives in a PATCH header, so
 * every trial was a full patch rebuild. Reading it here makes framing a
 * relaunch-and-look loop instead, and it doubles as the cheapest possible test
 * of whether our gView write drives the picture at all: if the render doesn't
 * move when this changes, something downstream is ignoring the pose. The
 * compile-time constant remains the default and the shipped value. */
/* Far clip plane for battle mode, overridable with ARENA_CAM_ZFAR.
 *
 * ZFAR is D_801779C8.raw (the debug overlay literally prints "ZFAR"), set per
 * level from gLevelInfo[level]->unk2C (decomp src/code/56800.c:372) and consumed
 * by guPerspective in the in-level draw (71AA0.c:610).
 *
 * IT IS NOT THE FRAMING PROBLEM. The hypothesis was that the level's authored
 * plane, sized for the map's own low/close rail camera, was clipping the arena's
 * far edge under our much higher camera. MEASURED (probe tag 9, 2026-07-27):
 * MAP_NITROS_1 already authors ZFAR = 8000, and the floor's far corner sits only
 * ~2400 away at ARENA_CAM_DIST 1800. Overriding it changed nothing.
 *
 * Kept as a cheap guard for maps that DO author a short plane, and because the
 * probe tag that logs the level's value is worth having. 8000 matches the game's
 * own usage elsewhere (4DFF0.c: near 10, far 8000), so the near/far ratio stays
 * within precedent for the N64's 16-bit depth. */
/* Runtime A/B switch for the fixed camera (ARENA_CAM_OFF=1 disables the stamp).
 * The A1.4 anim regression is isolated by toggling this, and rebuilding the patch
 * for each side of an A/B is both slow and risky - a failed rebuild silently
 * leaves the previous exe and the probe then measures the wrong binary. */
/* 1 while the set pose should be HELD (a window after player 0's set edge).
 * The one-shot trigger cannot survive: the game walker re-asserts its own anim
 * EVERY frame, so the pose is replaced on the very next frame - measured, with
 * the camera both on and off, standing still and moving. Holding means
 * re-triggering whenever the walker has taken the anim away. */
/* Which animation index the set-bomb pose plays. ARENA_SET_ANIM overrides.
 *
 * Default 29 is the decomp's set/drop pose (code_extra_0 state 0x0E ->
 * PLAYER_ACTION_DROP_BOMB_0), but that binding is recorded at MEDIUM confidence -
 * read from machine-C, never visually confirmed - and feel testing reports it
 * looks like a THROW. Made tunable so the right index can be found by eye in a
 * few relaunches instead of by re-reading machine-C. The RE lists the throws as
 * {34, 36, 38, 39, 42, 47}, impacts as {43, 44, 47, 48, 49}, warp 7, idle 0 and
 * locomotion 1-8, so the set pose is most likely a near neighbour of 29. */
/* DEBUG follow camera (ARENA_CAM_FOLLOW=1). The shipped camera is fixed on the
 * arena centre, which is right for play but useless for inspecting an animation:
 * at the framing distance the bomber is a couple of dozen pixels. With this on,
 * `at` tracks player 0 so a close ARENA_CAM_DIST stays on him.
 *
 * Yaw is untouched, so the input mapping is unaffected - only the target moves.
 * Inspection only; the fixed target is what ships. */
/* Camera PITCH, overridable with ARENA_CAM_PITCH (degrees above the ground
 * plane; 90 = straight down). Default is the shipped ARENA_CAM_PITCH_DEG.
 *
 * The trig is computed HERE, natively, which is the whole reason this can be a
 * runtime knob at all: the patch must never call sinf/cosf, because an emitted
 * math libcall links silently and jumps to 0 (notes 8.11). That is why the
 * shipped pose keeps precomputed literals. Native C++ has no such restriction.
 *
 * Added for animation inspection - the play pitch of 60 is nearly top-down and
 * poses are hard to read - but it is equally the knob for future framing work.
 * Clamped away from the gimbal values the game nudges (90 / 270). */
extern "C" float arena_cam_pitch_deg(void) {
    static const float d = []() {
        const char* v = std::getenv("ARENA_CAM_PITCH");
        if (v) {
            float f = (float)std::atof(v);
            if (f > 1.0f && f < 89.0f) return f;
        }
        return (float)ARENA_CAM_PITCH_DEG;
    }();
    return d;
}
extern "C" float arena_cam_pitch_sin(void) {
    static const float v = std::sin(arena_cam_pitch_deg() * 3.14159265358979f / 180.0f);
    return v;
}
extern "C" float arena_cam_pitch_cos(void) {
    static const float v = std::cos(arena_cam_pitch_deg() * 3.14159265358979f / 180.0f);
    return v;
}

/* Camera YAW, overridable with ARENA_CAM_YAW (degrees; 0 = the shipped view
 * from +Z). Exists for inspection shots — the 2026-07-30 pose review needed a
 * 3/4 FRONT angle ("from the back it was a little hard to tell"). Same
 * native-trig pattern as pitch: the patch must never emit a sinf/cosf libcall
 * (notes 8.11), so the effective (yaw+90°) sin/cos cross the ABI as floats.
 * Default reproduces ARENA_CAM_YAW_DEG / *_YAW_EFF exactly — play unchanged. */
extern "C" float arena_cam_yaw_deg(void) {
    static const float d = []() {
        const char* v = std::getenv("ARENA_CAM_YAW");
        if (v) {
            float f = (float)std::atof(v);
            if (f >= -360.0f && f <= 360.0f) return f;
        }
        return (float)ARENA_CAM_YAW_DEG;
    }();
    return d;
}
extern "C" float arena_cam_yaw_eff_sin(void) {
    static const float v = std::sin((arena_cam_yaw_deg() + ARENA_CAM_YAW_OFFSET_DEG)
                                    * 3.14159265358979f / 180.0f);
    return v;
}
extern "C" float arena_cam_yaw_eff_cos(void) {
    static const float v = std::cos((arena_cam_yaw_deg() + ARENA_CAM_YAW_OFFSET_DEG)
                                    * 3.14159265358979f / 180.0f);
    return v;
}

extern "C" int arena_cam_follow(void) {
    static const int on = []() {
        const char* v = std::getenv("ARENA_CAM_FOLLOW");
        return (v && v[0] == '1') ? 1 : 0;
    }();
    return on;
}

/* ANIM SWEEP (ARENA_ANIM_SWEEP=<ticks per index>, 0 = off). Cycles the player
 * through every animation index, holding each for N ticks and logging it, so the
 * right one can be identified in a single run instead of one relaunch per guess.
 *
 * Exists because the decomp's set/drop binding (state 0x0E -> anim 29) is
 * MEDIUM confidence and both 29 and 30 read as throws in play. Watching the
 * whole set is a faster and more honest answer than more machine-C reading. */
extern "C" int arena_anim_sweep_index(void) {
    static const int hold = []() {
        const char* v = std::getenv("ARENA_ANIM_SWEEP");
        if (v) { int n = std::atoi(v); if (n > 0 && n <= 600) return n; }
        return 0;
    }();
    if (hold == 0) return -1;
    static int frames = 0, idx = 0, last = -1;
    if (++frames >= hold) { frames = 0; if (++idx > 52) idx = 0; }
    if (idx != last) {
        last = idx;
        if (g_log) {
            std::fprintf(g_log, "[animsweep] now playing idx=%d\n", idx);
            std::fflush(g_log);
        }
        std::printf("[animsweep] now playing idx=%d\n", idx);
        std::fflush(stdout);
    }
    return idx;
}

/* Set pose index; ARENA_SET_ANIM overrides (-1 = no pose, keep locomotion).
 * DEFAULT 31 = what the GAME itself plays on an R-set, measured by the
 * single-player oracle (tools/oracle/goldens.json: set_anim_idx=31, 10 frames;
 * spec 2026-08-01). It was 29, read statically out of the drop handler
 * (func_80282E5C_code_extra_0) — the oracle shows 29 is the THROW clip: it is
 * what a tap-B and a B-release both play (throw_anim_idx=29), while a stationary
 * set plays 31. Runtime composition beats a static decomp read.
 * 29/41/42 remain reachable by env. */
extern "C" int arena_set_anim_index(void) {
    static const int idx = []() {
        const char* v = std::getenv("ARENA_SET_ANIM");
        if (v) { int n = std::atoi(v); if (n >= -1 && n < 64) return n; }
        return 31;
    }();
    return idx;
}

/* The pose the patch should be holding, or -1. Ticked down in arena_dbg_anim,
 * which the patch calls once per frame. */
extern "C" int arena_pose_anim(void) {
    return (g_pose_frames > 0) ? g_pose_anim : -1;
}

/* Kick pose index; ARENA_KICK_ANIM overrides (-1 = NO pose, keep locomotion).
 * DEFAULT 33 = what the GAME plays when Hero runs into a set bomb, measured by
 * the single-player oracle (goldens kick_anim_idx=33, 18 frames; the clip starts
 * ~8 frames BEFORE the bomb starts sliding). -1 was the old default, on §8.5c's
 * static reading that the walk-in kick has no animation at all (zero anim calls
 * in 69AA0.c) — that reading was wrong about the composed runtime: the walker
 * plays 33 on contact. -1 stays reachable by env. */
extern "C" int arena_kick_anim_index(void) {
    static const int idx = []() {
        const char* v = std::getenv("ARENA_KICK_ANIM");
        if (v) { int n = std::atoi(v); if (n >= -1 && n < 64) return n; }
        return 33;
    }();
    return idx;
}

extern "C" int arena_set_hold(void) {
    return (g_anim_since_set >= 0 && g_anim_since_set < 24) ? 1 : 0;
}

/* ---- A1.2g HUD ----------------------------------------------------------
 * The reused Hero HUD reads these. Kept as plain accessors so the patch does the
 * driving and the sim stays the single source of truth. */
extern "C" int arena_player_hp(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return 0;
    return g_state.players[i].hp;
}
extern "C" int arena_player_stocks(int i) {
    if (i < 0 || i >= ARENA_MAX_PLAYERS) return 0;
    return g_state.players[i].stocks_won;
}
extern "C" int arena_match_phase(void) { return g_state.phase; }

extern "C" int arena_cam_enabled(void) {
    static const int on = []() {
        const char* v = std::getenv("ARENA_CAM_OFF");
        return (v && v[0] == '1') ? 0 : 1;
    }();
    return on;
}

extern "C" float arena_cam_zfar(void) {
    static const float f = []() {
        const char* v = std::getenv("ARENA_CAM_ZFAR");
        if (v) { float x = (float)std::atof(v); if (x > 0.0f) return x; }
        return 8000.0f;
    }();
    return f;
}

extern "C" float arena_cam_dist(void) {
    static const float d = []() {
        const char* v = std::getenv("ARENA_CAM_DIST");
        if (v) { float f = (float)std::atof(v); if (f > 0.0f) return f; }
        return (float)ARENA_CAM_DIST;
    }();
    return d;
}

/* ---- A1.2g floor guard ---------------------------------------------------
 * The arena's floor polygon is SMALLER than the sim's collidable bounds, so the
 * sim can walk player 0 off the edge. When that happens the game's ground query
 * finds nothing and parks Pos.y at 30000 - measured, with actionState staying 4
 * throughout, so this is NOT the death path and NOT a crash; the player just
 * hangs off-map and X/Z freeze.
 *
 * Until the sim geometry is re-matched to the real floor (section 8.5a - the
 * proper fix, and a sim change), this guard keeps the player on the floor: the
 * bridge remembers the last position with a valid ground height, and the patch
 * restores it whenever the sentinel appears. State lives here because patches
 * must be stateless.
 *
 * It is a CONTAINMENT measure, not a correctness fix: the sim still believes the
 * player is out there, so sim and render disagree while the guard is holding. It
 * stops the visible fall and lets a sweep keep mapping the floor. */
namespace {
    bool  g_floor_ok = false;
    float g_floor_x = 0.0f, g_floor_y = 0.0f, g_floor_z = 0.0f;
}
extern "C" int arena_floor_guard(int xbits, int ybits, int zbits) {
    float x, y, z;
    std::memcpy(&x, &xbits, sizeof x);
    std::memcpy(&y, &ybits, sizeof y);
    std::memcpy(&z, &zbits, sizeof z);
    if (y < 10000.0f) {                 /* valid ground height -> remember it */
        g_floor_ok = true;
        g_floor_x = x; g_floor_y = y; g_floor_z = z;
        return 0;                       /* nothing to correct */
    }
    /* Since the 2026-07-26 anchor fix the sim's arena maps exactly onto the
     * measured floor, so this must NEVER fire. Say so loudly the first time it
     * does: a silent crutch would hide a real regression in the very invariant
     * (8.5a) this whole slice exists to establish. Kept as containment so a
     * regression is a log line rather than a player falling out of the world. */
    if (g_floor_ok) {
        static bool warned = false;
        if (!warned && g_log) {
            warned = true;
            std::fprintf(g_log,
                "[floor] *** GUARD FIRED at (%.1f,%.1f) - the sim drove the player "
                "OFF the measured floor (half %.0f). Sim bounds and rendered map "
                "have drifted apart again (8.5a).\n",
                g_floor_x, g_floor_z, (double)ARENA_FLOOR_HALF);
            std::fflush(g_log);
        }
    }
    return g_floor_ok ? 1 : 0;          /* 1 = off-map, restore last good */
}
extern "C" float arena_floor_last_x(void) { return g_floor_x; }
extern "C" float arena_floor_last_y(void) { return g_floor_y; }
extern "C" float arena_floor_last_z(void) { return g_floor_z; }

/* ---- A1.2g floor RASTER (ARENA_AUTO_BATTLE=7) ----------------------------
 * The keystone measurement for section 8.5a (the sim's collidable bounds must
 * track the RENDERED map). We never had a map of the real floor - only the
 * sliver a walking player happened to cover, because a walk stalls at the first
 * edge it finds and the guard then parks it there.
 *
 * So stop walking and ask the geometry directly. func_80078168(x,y,z) is the
 * game's OWN ground query (decomp src/code/69AA0.c:205 - a pure position-driven
 * chain, no caller context). After it returns:
 *     i = D_801776E0 & 1;   h = D_80177760[i];
 * and the game's own test for "no floor here" is  i == 0 && h == -30000.0f
 * (69AA0.c:401 - the branch that leaves the object's ground height unset). We
 * copy that rule verbatim rather than inventing a threshold, and h is the
 * ground height everywhere else.
 *
 * One run maps the whole floor, and because it needs no player movement there
 * is nothing for an edge to stall.
 *
 * Coordinates are ABSOLUTE Hero world units. The box is centred on the captured
 * spawn origin only because that is a point known to be ON the floor; the
 * reported extent is absolute, so it stays comparable across runs even though
 * the capture origin itself drifts (observed: origin.z 0 one boot, 171 the
 * next). A hit on the box border means the box was too small - flagged, not
 * silently truncated. */
namespace {
    /* Grid is env-tunable so a coarse survey and a fine edge-refinement pass are
     * the same probe, no rebuild between them:
     *     ARENA_RASTER_N     samples per axis (odd -> the centre is sampled)
     *     ARENA_RASTER_STEP  Hero units between samples
     * Defaults survey +-2000 at 50u. FR_MAX caps the static map. */
    constexpr int FR_MAX  = 201;
    int   FR_N    = 81;
    float FR_STEP = 50.0f;

    bool  fr_checked = false, fr_armed = false;
    bool  fr_started = false, fr_done = false;
    int   fr_cursor = 0;                 /* linear index; ONLY arena_floor_raster_report advances it */
    float fr_x0 = 0.0f, fr_z0 = 0.0f, fr_y = 0.0f;   /* box corner + probe height */
    float fr_px = 0.0f, fr_pz = 0.0f;                /* the point the patch must query now */
    unsigned char fr_hit[FR_MAX * FR_MAX];
    unsigned char fr_type[FR_MAX * FR_MAX];   /* surface type per cell (unkAE) */
    float fr_minx = 0, fr_maxx = 0, fr_minz = 0, fr_maxz = 0, fr_minh = 0, fr_maxh = 0;
    long  fr_hits = 0;

    void fr_finish() {
        fr_done = true;
        if (!g_log) return;
        if (fr_hits == 0) {
            std::fprintf(g_log, "[raster] NO FLOOR FOUND in %dx%d box around origin "
                                "(%.1f,%.1f) step %.0f - probe height or box is wrong\n",
                         FR_N, FR_N, (double)(fr_x0 + FR_STEP * (FR_N / 2)),
                         (double)(fr_z0 + FR_STEP * (FR_N / 2)), (double)FR_STEP);
            std::fflush(g_log);
            return;
        }
        /* A hit on the border means the floor continues past the box. */
        bool edge = false;
        for (int i = 0; i < FR_N; i++) {
            if (fr_hit[i] || fr_hit[(FR_N - 1) * FR_N + i] ||
                fr_hit[i * FR_N] || fr_hit[i * FR_N + FR_N - 1]) edge = true;
        }
        std::fprintf(g_log,
            "[raster] DONE  level=%d samples=%d hits=%ld step=%.0f\n"
            "[raster] x=[%.1f..%.1f] z=[%.1f..%.1f]\n"
            "[raster] centre=(%.1f,%.1f) span=(%.1f x %.1f) half=(%.1f x %.1f)\n"
            "[raster] ground h=[%.1f..%.1f]%s\n",
            g_level_at_capture, FR_N * FR_N, fr_hits, (double)FR_STEP,
            (double)fr_minx, (double)fr_maxx, (double)fr_minz, (double)fr_maxz,
            (double)((fr_minx + fr_maxx) * 0.5f), (double)((fr_minz + fr_maxz) * 0.5f),
            (double)(fr_maxx - fr_minx), (double)(fr_maxz - fr_minz),
            (double)((fr_maxx - fr_minx) * 0.5f), (double)((fr_maxz - fr_minz) * 0.5f),
            (double)fr_minh, (double)fr_maxh,
            edge ? "   *** EDGE-SATURATED: floor continues past the box, widen FR_N ***" : "");
        /* ASCII map: one row per z, x increasing left to right. Reading the SHAPE
         * matters as much as the extent - a bounding box is only the right model
         * if the floor is actually a filled rectangle. */
        std::fprintf(g_log, "[raster] map (rows = z ascending, cols = x ascending, '#' = floor)\n");
        for (int iz = 0; iz < FR_N; iz++) {
            char row[FR_MAX + 1];   /* FR_N is runtime now - no VLA */
            for (int ix = 0; ix < FR_N; ix++) row[ix] = fr_hit[iz * FR_N + ix] ? '#' : '.';
            row[FR_N] = '\0';
            std::fprintf(g_log, "[raster] z=%8.1f %s\n", (double)(fr_z0 + FR_STEP * iz), row);
        }

        /* SURFACE TYPES. 69AA0.c:411 keys the hazard path off the surface type
         * (the object's unkAE): 0xF8 / 0xF7 / 0xF5 / 0xD9 raise the damage flag,
         * 0xFF is plain floor. The Nitros corners damage and stun the game-side
         * player and our spawns sit on them, so map WHERE each type is - knowing
         * only that hazards exist somewhere is not actionable. */
        {
            int seen[256] = { 0 };
            for (int i = 0; i < FR_N * FR_N; i++)
                if (fr_hit[i]) seen[fr_type[i]]++;
            std::fprintf(g_log, "[raster] surface types present:\n");
            char sym[256];
            const char* pool = "abcdefghijklmnopqrstuvwxyz";
            int next = 0;
            for (int t = 0; t < 256; t++) {
                sym[t] = '?';
                if (!seen[t]) continue;
                if (t == 0xFF)      sym[t] = '.';        /* plain floor */
                else if (next < 26) sym[t] = pool[next++];
                const char* note = "";
                if (t == 0xF8 || t == 0xF7 || t == 0xF5 || t == 0xD9)
                    note = "   <== HAZARD (69AA0.c:411 raises the damage flag)";
                std::fprintf(g_log, "[raster]   type 0x%02X '%c' cells=%d%s\n",
                             t, sym[t], seen[t], note);
            }
            std::fprintf(g_log, "[raster] type map (same grid; '.' = plain floor)\n");
            for (int iz = 0; iz < FR_N; iz++) {
                char row[FR_MAX + 1];
                for (int ix = 0; ix < FR_N; ix++) {
                    int i = iz * FR_N + ix;
                    row[ix] = fr_hit[i] ? sym[fr_type[i]] : ' ';
                }
                row[FR_N] = '\0';
                std::fprintf(g_log, "[raster] z=%8.1f %s\n",
                             (double)(fr_z0 + FR_STEP * iz), row);
            }
        }
        std::fflush(g_log);
    }
}

extern "C" int arena_floor_raster_active(void) {
    if (!fr_checked) {
        const char* m = std::getenv("ARENA_AUTO_BATTLE");
        fr_armed = (m != nullptr && std::atoi(m) == 7);
        fr_checked = true;
    }
    if (!fr_armed || fr_done) return 0;
    if (!g_puppets_ready) return 0;      /* need the origin, i.e. a settled level */
    if (!fr_started) {
        if (const char* n = std::getenv("ARENA_RASTER_N")) {
            int v = std::atoi(n);
            if (v >= 3 && v <= FR_MAX) FR_N = (v % 2) ? v : v - 1;   /* keep it odd */
        }
        if (const char* s = std::getenv("ARENA_RASTER_STEP")) {
            float v = (float)std::atof(s);
            if (v > 0.0f) FR_STEP = v;
        }
        fr_x0 = g_origin_x - FR_STEP * (FR_N / 2);
        fr_z0 = g_origin_z - FR_STEP * (FR_N / 2);
        fr_y  = g_origin_y;              /* a height known to be on the floor */
        std::memset(fr_hit, 0, sizeof fr_hit);
        std::memset(fr_type, 0xFF, sizeof fr_type);   /* 0xFF = plain floor */
        fr_started = true;
        if (g_log) {
            std::fprintf(g_log, "[raster] START level=%d centre=(%.1f,%.1f) y=%.1f "
                                "%dx%d step=%.0f\n",
                         g_level_at_capture, (double)g_origin_x, (double)g_origin_z,
                         (double)fr_y, FR_N, FR_N, (double)FR_STEP);
            std::fflush(g_log);
        }
    }
    return 1;
}

extern "C" int arena_floor_raster_next(void) {
    if (!fr_started || fr_done) return 0;
    if (fr_cursor >= FR_N * FR_N) { fr_finish(); return 0; }
    fr_px = fr_x0 + FR_STEP * (fr_cursor % FR_N);
    fr_pz = fr_z0 + FR_STEP * (fr_cursor / FR_N);
    return 1;
}
extern "C" float arena_floor_raster_px(void) { return fr_px; }
extern "C" float arena_floor_raster_pz(void) { return fr_pz; }
extern "C" float arena_floor_raster_py(void) { return fr_y; }

/* sel = D_801776E0 & 1, hbits = D_80177760[sel] as a bit pattern (no float args
 * over the export ABI). Advancing the cursor HERE, not in _next, keeps the point
 * and the answer in lockstep no matter how the patch batches its loop. */
extern "C" void arena_floor_raster_report(int sel, int hbits, int type) {
    if (!fr_started || fr_done || fr_cursor >= FR_N * FR_N) return;
    float h;
    std::memcpy(&h, &hbits, sizeof h);
    if (!(sel == 0 && h == -30000.0f)) {          /* the game's own "no floor" rule */
        fr_hit[fr_cursor]  = 1;
        fr_type[fr_cursor] = (unsigned char)(type & 0xFF);
        if (fr_hits == 0) {
            fr_minx = fr_maxx = fr_px; fr_minz = fr_maxz = fr_pz; fr_minh = fr_maxh = h;
        }
        if (fr_px < fr_minx) fr_minx = fr_px;
        if (fr_px > fr_maxx) fr_maxx = fr_px;
        if (fr_pz < fr_minz) fr_minz = fr_pz;
        if (fr_pz > fr_maxz) fr_maxz = fr_pz;
        if (h < fr_minh) fr_minh = h;
        if (h > fr_maxh) fr_maxh = h;
        fr_hits++;
    }
    fr_cursor++;
}

/* A1.5 camera probe. The patch calls this every frame for 5 tags; the GATE and
 * THROTTLE live here, not in the patch, because patches must stay stateless (a
 * patch-local counter aborts 0xC0000409). Same division of labour as
 * arena_dbg_anim's burst log above.
 *
 * Floats arrive as BIT PATTERNS - the export ABI takes no float arguments. */
extern "C" void arena_dbg_cam(int tag, int xbits, int ybits, int zbits) {
    static const char* mode  = std::getenv("ARENA_AUTO_BATTLE");
    /* mode 8 (direction probe) reuses this logger for the facing check. */
    static const bool  airset = (mode != nullptr &&
                                 (std::atoi(mode) == 12 || std::atoi(mode) == 11));
    static const bool  armed = (mode != nullptr &&
                                (std::atoi(mode) == 6 || std::atoi(mode) == 8 ||
                                 std::atoi(mode) == 9)) || airset;
    static const bool  turnprobe = (mode != nullptr && std::atoi(mode) == 9);
    if (!armed) return;

    /* Air-set probe (mode 12): who owns player Y when a mid-air set fires?
     * gameY = the walker's own Pos.y (tag 7, stashed); simY = our sim player 0.
     * Every other frame, BEFORE the throttle - the question is a runaway ramp
     * vs a normal arc, and a 1-in-30 sample can miss the whole jump. 30000 is
     * the game's "no ground here" sentinel (the floor guard's park value). */
    if (airset) {
        static float as_gameY = 0;
        if (tag == 7) std::memcpy(&as_gameY, &ybits, sizeof as_gameY);
        if (tag == 8) {
            static int an = 0;
            if ((++an % 2) == 0 && g_log) {
                std::fprintf(g_log, "[airset] f%04d gameY=%.1f simY=%.3f state=%d pair=%d pressed=0x%04x\n",
                             an, (double)as_gameY,
                             (double)qf(g_state.players[0].pos.y), xbits,
                             ybits, (unsigned)zbits & 0xFFFFu);
                std::fflush(g_log);
            }
        }
        return;   /* mode 12 wants only this channel; skip the mode-6 loggers */
    }
    if (tag < 0 || tag > 11) return;

    /* A1.2g exit-trigger hunt: gCurrentLevel + the next-level request vars.
     * BEFORE the throttle - a level transition is a single-frame event and a
     * one-in-30 sample would miss it. Logged only when the values CHANGE, so the
     * moment of a transition stands out instead of drowning in per-frame noise. */
    if (tag == 11) {
        static int last_lv = -12345, last_a = -12345, last_b = -12345;
        if (xbits != last_lv || ybits != last_a || zbits != last_b) {
            last_lv = xbits; last_a = ybits; last_b = zbits;
            std::printf("[level] gCurrentLevel=%d next=(%d,%d)\n",
                        xbits, ybits, zbits);
            std::fflush(stdout);
            if (g_log) {
                std::fprintf(g_log, "[level] gCurrentLevel=%d next=(%d,%d)\n",
                             xbits, ybits, zbits);
                std::fflush(g_log);
            }
        }
        return;
    }

    /* A1.2g floor-extent tracker. Runs EVERY frame, BEFORE the log throttle -
     * a 30-frame sample is far too coarse to locate a floor edge.
     *
     * Y == 30000 is the game's "no ground here" sentinel (observed; actionState
     * stays 4 throughout, so it is NOT a death or fall state - the player simply
     * walked off the floor polygon). A sample with a normal Y is therefore ON
     * the floor, and the running min/max of those gives the REAL floor extent in
     * Hero coords: both the geometry fix (section 8.5a - sim bounds must track
     * the rendered map) and the true centre the fixed camera should aim at. */
    if (tag == 7) {
        float px, py, pz;
        std::memcpy(&px, &xbits, sizeof px);
        std::memcpy(&py, &ybits, sizeof py);
        std::memcpy(&pz, &zbits, sizeof pz);
        static bool  seen = false;
        static float minx = 0, maxx = 0, minz = 0, maxz = 0;
        if (py < 10000.0f) {                       /* on the floor */
            if (!seen) { minx = maxx = px; minz = maxz = pz; seen = true; }
            if (px < minx) minx = px;
            if (px > maxx) maxx = px;
            if (pz < minz) minz = pz;
            if (pz > maxz) maxz = pz;
            static int n = 0;
            if ((++n % 30) == 0 && g_log) {
                std::fprintf(g_log,
                    "[floor] x=[%.1f..%.1f] z=[%.1f..%.1f] centre=(%.1f,%.1f) span=(%.1f x %.1f)\n",
                    (double)minx, (double)maxx, (double)minz, (double)maxz,
                    (double)((minx + maxx) * 0.5f), (double)((minz + maxz) * 0.5f),
                    (double)(maxx - minx), (double)(maxz - minz));
                std::fflush(g_log);
            }
        }
    }

    /* Turn probe (mode 9) needs tag 10 EVERY frame, not one sample per 30:
     * the question is whether the game's own walker SNAPS its facing on a
     * reversal or sweeps it at a bounded rate, and a 30-frame sample cannot
     * tell a snap from a sweep. Logged before the throttle for that reason. */
    if (turnprobe && tag == 10) {
        float ma, sy;
        std::memcpy(&ma, &xbits, sizeof ma);
        std::memcpy(&sy, &ybits, sizeof sy);
        static int tf = 0;
        if (g_log && tf < 600) {
            std::fprintf(g_log, "[turn] f%03d moveAngle=%.1f simYaw=%.1f state=%d\n",
                         tf, (double)ma, (double)sy, zbits);
            std::fflush(g_log);
        }
        tf++;
        return;
    }

    static int frames = 0;
    if (tag == 0) frames++;              /* tag 0 arrives once per frame */
    if ((frames % 30) != 0) return;      /* one sample per ~half second */

    float x, y, z;
    std::memcpy(&x, &xbits, sizeof x);
    std::memcpy(&y, &ybits, sizeof y);
    std::memcpy(&z, &zbits, sizeof z);

    char line[160];
    if (tag == 8) {
        /* A1.2g: player actionState + objID as plain ints, to catch the state
         * transition at the moment the player leaves the floor (ppos Y -> 30000). */
        /* hazard = D_8016E080, the code the game derives from the surface under
         * the player (1 = the 0xF7 damage tile at our corners). Non-zero WITH no
         * damage and no stun is the proof that suppression works; "nothing bad
         * happened" on its own proves nothing. */
        std::snprintf(line, sizeof line, "[cam] state=%d unkA6=%d hazard=%d\n",
                      xbits, ybits, zbits);
    } else if (tag == 4) {
        std::snprintf(line, sizeof line, "[cam] type=%d dist=%.1f\n", xbits, (double)y);
    } else if (tag == 9) {
        /* x = the LEVEL's authored ZFAR, sampled BEFORE our write; y = ours. */
        std::snprintf(line, sizeof line, "[cam] zfar level=%.1f -> ours=%.1f\n",
                      (double)x, (double)y);
    } else if (tag == 10) {
        /* Facing check. x = the game's moveAngle (what we copy into Rot.y),
         * y = the SIM's yaw in degrees (what actually determines travel).
         * For W the sim yaw is 0 (-Z), and the game's convention is
         * direction = (sin th, cos th), so facing -Z needs moveAngle 180.
         * Anything else means the model points somewhere other than where it
         * is actually going - which matters now that the bridge negates the
         * stick's Y for the sim but the game computes moveAngle from the raw
         * stick. */
        /* state included so a long idle ON the spawn pad shows whether the room's
         * damage tiles are firing under us - A1.2g put those tiles at the
         * corners, which is exactly where our spawns are. */
        std::snprintf(line, sizeof line,
                      "[cam] face moveAngle=%.1f simYaw=%.1f state=%d\n",
                      (double)x, (double)y, zbits);
    } else {
        /* tags 5/6 are the SAME fields sampled immediately AFTER our write, so a
         * post-vs-entry comparison shows whether anything stomps gView between
         * frames. Distinguishes "our write never ran" from "it ran and was
         * overwritten" - two very different bugs that look identical at entry. */
        static const char* names[] = { "at", "eye", "rot", "up", "", "wrote_rot", "wrote_at", "ppos", "", "zfar" };
        std::snprintf(line, sizeof line, "[cam] %s=(%.2f,%.2f,%.2f)\n",
                      names[tag], (double)x, (double)y, (double)z);
    }
    std::printf("%s", line);
    std::fflush(stdout);
    if (g_log) { std::fprintf(g_log, "%s", line); std::fflush(g_log); }
}
