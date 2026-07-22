#include "arena_bridge.h"

#include <cstdio>
#include <cstdint>

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
    float g_scale = 120.0f;    /* Hero units per sim unit */
    float g_scale_z = 120.0f;  /* symmetric with X; forward/back feel is a
                                * known camera-relative item for the feel pass */
    float g_render_dx = 0.0f;  /* last tick's displacement, scaled */
    float g_render_dz = 0.0f;
    float g_render_yaw = 0.0f;

    /* A1.2b puppet actors (players 1-3). State lives here because patches must
     * be stateless. The world anchor is FROZEN at spawn (not the live player
     * object) so actors follow the sim, not the player's own movement (which
     * would mirror). g_ref_s* is player 0's sim pos at capture. */
    bool  g_puppets_ready = false;
    float g_origin_x = 0.0f, g_origin_y = 0.0f, g_origin_z = 0.0f;  /* frozen world anchor */
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
extern "C" void arena_bridge_tick_input(int sx, int sy, int buttons) {
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

    /* A1.2b diag: are the sim's players 1-3 holding their corners, or does the
     * convergence come from the game's object update? Log all 4 once a second. */
    static uint32_t n = 0;
    if ((++n % 60u) == 0u && g_log) {
        std::fprintf(g_log,
            "[simpos] t%u p0(%.2f,%.2f) p1(%.2f,%.2f) p2(%.2f,%.2f) p3(%.2f,%.2f)\n",
            g_state.tick,
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
extern "C" void arena_puppet_capture(uint32_t bx, uint32_t by, uint32_t bz) {
    if (g_puppets_ready) return;
    union { uint32_t u; float f; } ux, uy, uz;
    ux.u = bx; uy.u = by; uz.u = bz;
    g_origin_x = ux.f; g_origin_y = uy.f; g_origin_z = uz.f;
    g_ref_sx = qf(g_state.players[0].pos.x);
    g_ref_sy = qf(g_state.players[0].pos.y);   /* A1.2c: bomb arc height reference */
    g_ref_sz = qf(g_state.players[0].pos.z);
    g_puppets_ready = true;
    if (g_log) {
        std::fprintf(g_log, "[capture] origin(%.2f,%.2f,%.2f) ref_s(%.2f,%.2f)\n",
                     g_origin_x, g_origin_y, g_origin_z, g_ref_sx, g_ref_sz);
        std::fflush(g_log);
    }
}
extern "C" int  arena_puppet_ready(void)              { return g_puppets_ready ? 1 : 0; }
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
    if (i < 0 || i >= ARENA_MAX_BOMBS) return g_origin_y;
    return g_origin_y + (qf(g_state.bombs[i].pos.y) - g_ref_sy) * g_scale;   /* arc height */
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
    if (i < 0 || i >= ARENA_MAX_BLASTS) return g_origin_y;
    return g_origin_y + (qf(g_state.blasts[i].center.y) - g_ref_sy) * g_scale;
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
