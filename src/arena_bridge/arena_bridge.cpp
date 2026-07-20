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

    /* Coord mapping: ArenaState Q20.12 -> Hero Battle-Room float coords.
     * TODO(feel): calibrated on-screen (A1.2a Task 3). Seed: arena is +/-6
     * sim units; ~40 Hero units per sim unit, room centered near origin. */
    float g_scale    = 40.0f;
    float g_origin_x = 0.0f;
    float g_origin_y = 0.0f;      /* ground height in Hero coords */
    float g_origin_z = 0.0f;

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
    ArenaInput in[ARENA_MAX_PLAYERS] = {0, 0, 0, 0};
    in[0] = arena_input_pack(sx, sy, jump, bomb, 0);
    arena_tick(&g_state, in);
}

extern "C" float arena_get_player_x(int i) {
    return qf(g_state.players[i].pos.x) * g_scale + g_origin_x;
}
extern "C" float arena_get_player_y(int i) {
    return qf(g_state.players[i].pos.y) * g_scale + g_origin_y;
}
extern "C" float arena_get_player_z(int i) {
    return qf(g_state.players[i].pos.z) * g_scale + g_origin_z;
}
extern "C" float arena_get_player_yaw_deg(int i) {
    /* binary angle (65536 = full turn) -> degrees */
    return (float)g_state.players[i].yaw * (360.0f / 65536.0f);
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
