#ifndef ARENA_BRIDGE_H
#define ARENA_BRIDGE_H
#include <stdint.h>
/* Fork-native glue between the recomp host and the pure arena sim
 * (lib/bmhero-arena). A1.1a: tick a silent passenger ArenaState once per VI
 * and log proof-of-life. No game state read/written; no rendering. */
#ifdef __cplusplus
extern "C" {
#endif
void arena_bridge_tick(void);   /* call once per VI/frame */
void arena_bridge_set_battle_mode(int on);   /* menu sets this; tick reads it */
int  arena_bridge_battle_active(void);        /* plain accessor for the export shim */
void arena_bridge_tick_input(int sx, int sy, int jump, int bomb);  /* tick player 0 */
float arena_get_player_x(int i);
float arena_get_player_y(int i);
float arena_get_player_z(int i);
float arena_get_player_yaw_deg(int i);
void  arena_dbg_dump(int i, int objID, int actionState);   /* A1.2b diag: log one object-table row */
float arena_get_bomber_off_x(int i);   /* A1.2b: (sim_pos_i - sim_pos_0).x * scale */
float arena_get_bomber_off_z(int i);
float arena_get_bomber_yaw(int i);
/* A1.2b puppet actors: state + frozen-origin world placement (patch-callable). */
void  arena_puppet_capture(uint32_t bx, uint32_t by, uint32_t bz);
int   arena_puppet_ready(void);
void  arena_puppet_set_slot(int i, int slot);
int   arena_puppet_get_slot(int i);
float arena_puppet_wx(int i);
float arena_puppet_wy(int i);
float arena_puppet_wz(int i);
float arena_puppet_yaw(int i);
void  arena_dbg_u32(int tag, unsigned val);   /* A1.2b temp evidence logger */
#ifdef __cplusplus
}
#endif
#endif
