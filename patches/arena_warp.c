#include "patches.h"
#include "misc_funcs.h"

#include <ultra64.h>
#include "types.h"
#include "variables.h"

// Native bridge import (registered in main.cpp). Returns 1 in battle mode.
DECLARE_FUNC(s32, arena_bridge_is_battle);

#define ARENA_WARP_MAP 15  /* MAP_NITROS_1 boss arena (flat/open); was 2 MAP_BATTLE_ROOM */

// A1.1b-ii: func_80081C50 seeds the next-level var (D_8016E432) and the spawn
// coords from gCurrentLevel, just before the loader (func_80081D78) reads
// them. In battle mode, redirect gCurrentLevel to the arena shell first so
// the whole level (geometry, skybox, spawn) resolves to it. Original body
// copied verbatim from code/71AA0.c func_80081C50 after the override.
RECOMP_PATCH void func_80081C50(void) {
    if (arena_bridge_is_battle()) {
        gCurrentLevel = ARENA_WARP_MAP;
        recomp_printf("[arena_warp] -> map %d\n", gCurrentLevel);
    }
    D_8016E430 = 0;
    D_8016E432 = (s16) gCurrentLevel;
    D_8016E434 = (s16) gCurrentLevel;
    D_8016E438 = (f32) *D_80108238[gCurrentLevel]->unk0;
    D_8016E43C = (f32) *(D_80108238[gCurrentLevel]->unk0 + 0x1);
    D_8016E440 = (f32) *(D_80108238[gCurrentLevel]->unk0 + 0x2);
    D_8016E444 = (f32) *(D_80108238[gCurrentLevel]->unk0 + 0x3);
}
