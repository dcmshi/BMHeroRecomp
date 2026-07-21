#include "arena_bridge.h"

#include <cstdio>

extern "C" {
#include "arena/arena_sim.h"
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
    /* non-battle proof-of-life: neutral inputs */
    const ArenaInput neutral[ARENA_MAX_PLAYERS] = {0, 0, 0, 0};
    arena_tick(&g_state, neutral);
    if ((++g_calls % 60u) == 0u)
        proof("[arena] tick %u hash %08x\n", g_state.tick, arena_hash(&g_state));
}

extern "C" void arena_bridge_tick_input(int sx, int sy, int jump, int bomb) {
    ensure_init();
    Vec3q before = g_state.players[0].pos;
    ArenaInput in[ARENA_MAX_PLAYERS] = {0, 0, 0, 0};
    in[0] = arena_input_pack(sx, sy, jump, bomb, 0);
    arena_tick(&g_state, in);
    Vec3q after = g_state.players[0].pos;
    g_render_dx  = qf(after.x - before.x) * g_scale;
    g_render_dz  = qf(after.z - before.z) * g_scale_z;
    g_render_yaw = (float)g_state.players[0].yaw * (360.0f / 65536.0f);
}

/* getters return the last tick's scaled displacement (dx/dz) and abs yaw;
 * i is ignored for A1.2a (only player 0 is driven). */
extern "C" float arena_get_player_x(int i)       { (void)i; return g_render_dx; }
extern "C" float arena_get_player_y(int i)       { (void)i; return 0.0f; }  /* Y left to game */
extern "C" float arena_get_player_z(int i)       { (void)i; return g_render_dz; }
extern "C" float arena_get_player_yaw_deg(int i) { (void)i; return g_render_yaw; }

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
