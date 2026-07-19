#include "arena_bridge.h"

#include <cstdio>

extern "C" {
#include "arena/arena_sim.h"
}

namespace {
    bool       g_inited = false;
    ArenaState g_state;
    uint32_t   g_calls = 0;
}

extern "C" void arena_bridge_tick(void) {
    if (!g_inited) {
        arena_init(&g_state, 0, 4, 0xB0BB1E5u);  /* 4 idle players; round won't end */
        g_inited = true;
        std::printf("[arena] bridge init: state %zu bytes\n", sizeof(ArenaState));
    }
    /* neutral inputs: silent passenger this milestone */
    const ArenaInput neutral[ARENA_MAX_PLAYERS] = {0, 0, 0, 0};
    arena_tick(&g_state, neutral);
    if ((++g_calls % 60u) == 0u) {
        std::printf("[arena] tick %u hash %08x\n",
                    g_state.tick, arena_hash(&g_state));
        std::fflush(stdout);
    }
}
