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

    void proof(const char* fmt, unsigned a, unsigned b) {
        std::printf(fmt, a, b);
        std::fflush(stdout);
        if (g_log) { std::fprintf(g_log, fmt, a, b); std::fflush(g_log); }
    }
}

extern "C" void arena_bridge_tick(void) {
    if (!g_inited) {
        arena_init(&g_state, 0, 4, 0xB0BB1E5u);  /* 4 idle players; round won't end */
        g_inited = true;
        g_log = std::fopen("arena_bridge.log", "w");
        proof("[arena] bridge init: state %u bytes (tick %u)\n",
              (unsigned)sizeof(ArenaState), 0u);
    }
    /* neutral inputs: silent passenger this milestone */
    const ArenaInput neutral[ARENA_MAX_PLAYERS] = {0, 0, 0, 0};
    arena_tick(&g_state, neutral);
    if ((++g_calls % 60u) == 0u) {
        proof(g_battle_mode ? "[arena] BATTLE MODE tick %u hash %08x\n"
                            : "[arena] tick %u hash %08x\n",
              g_state.tick, arena_hash(&g_state));
    }
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
