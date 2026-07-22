#include "patches.h"
#include "misc_funcs.h"

#include <ultra64.h>
#include "types.h"
#include "variables.h"

// Native bridge import (registered in main.cpp). Returns 1 in battle mode.
DECLARE_FUNC(s32, arena_bridge_is_battle);

#define ARENA_WARP_MAP 15  /* MAP_NITROS_1 boss arena (flat/open).
                            * History: 2 MAP_BATTLE_ROOM (pits aborted actor
                            * collision); 71 MAP_MIRROR_ROOM tried for A1.2d
                            * (bomber anim data) — registry empty there too AND
                            * direct-warp hits an unregistered function pointer
                            * in func_8001D9E4 at level-enter (get_function ->
                            * exit; dump 2026-07-22). Avoid 71. */

// A1.1b-ii: func_80081C50 seeds the next-level var (D_8016E432) and the spawn
// coords from gCurrentLevel, just before the loader (func_80081D78) reads
// them. In battle mode, redirect gCurrentLevel to the arena shell first so
// the whole level (geometry, skybox, spawn) resolves to it. Original body
// copied verbatim from code/71AA0.c func_80081C50 after the override.
RECOMP_PATCH void func_80081C50(void) {
    if (arena_bridge_is_battle()) {
        gCurrentLevel = ARENA_WARP_MAP;
        /* NOTE (2026-07-21): tried neutering gLevelInfo[level]->unk24/unk28
         * (the loader's spawn hooks) to kill the boss before init — but those
         * hooks also do draw/level setup: the arena then white-screens
         * deterministically. Reverted. The per-frame sweep handles the boss
         * once in-level. */
        /* recomp_printf here was the THIRD confirmed site of the load-window
         * print crash (_Printf -> get_function -> exit; symbolized dump
         * 2026-07-22) — the very "stochastic load crash" the note above called
         * an open item. This function IS the level-load prep; never print here.
         * (Sites 1+2: required_patches.c load_from_rom_to_addr,
         * 3d_object_hook.c func_800608B8.) */
        /* recomp_printf("[arena_warp] -> map %d\n", gCurrentLevel); */
    }
    D_8016E430 = 0;
    D_8016E432 = (s16) gCurrentLevel;
    D_8016E434 = (s16) gCurrentLevel;
    D_8016E438 = (f32) *D_80108238[gCurrentLevel]->unk0;
    D_8016E43C = (f32) *(D_80108238[gCurrentLevel]->unk0 + 0x1);
    D_8016E440 = (f32) *(D_80108238[gCurrentLevel]->unk0 + 0x2);
    D_8016E444 = (f32) *(D_80108238[gCurrentLevel]->unk0 + 0x3);
}
