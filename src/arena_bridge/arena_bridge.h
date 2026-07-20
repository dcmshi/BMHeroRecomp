#ifndef ARENA_BRIDGE_H
#define ARENA_BRIDGE_H
/* Fork-native glue between the recomp host and the pure arena sim
 * (lib/bmhero-arena). A1.1a: tick a silent passenger ArenaState once per VI
 * and log proof-of-life. No game state read/written; no rendering. */
#ifdef __cplusplus
extern "C" {
#endif
void arena_bridge_tick(void);   /* call once per VI/frame */
void arena_bridge_set_battle_mode(int on);   /* menu sets this; tick reads it */
int  arena_bridge_battle_active(void);        /* plain accessor for the export shim */
#ifdef __cplusplus
}
#endif
#endif
