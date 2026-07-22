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
void arena_bridge_tick_input(int sx, int sy, int buttons);  /* tick p0; buttons: b0 jump,b1 bomb,b2 set */
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
int   arena_spawn_gate(void);   /* A1.2d: 1 only after ~90 routine frames (heap-ready gate) */
int   arena_routine_seen(void); /* A1.2f: 1 once the battle render routine has run (mash-stop) */
int   arena_draw_gate(void);    /* A1.2e: 1 after ~30 routine frames (gDebugRoutine1 restore) */
void  arena_draw_gate_reset(void); /* call at every level-enter (window recurs per transition) */
void  arena_puppet_set_slot(int i, int slot);
int   arena_puppet_get_slot(int i);
float arena_puppet_wx(int i);
float arena_puppet_wy(int i);
float arena_puppet_wz(int i);
float arena_puppet_yaw(int i);
void  arena_dbg_u32(int tag, unsigned val);   /* A1.2b temp evidence logger */
/* A1.2c bombs: state getters + slot table + actor-slot check (patch-callable). */
int   arena_bomb_active(int i);
float arena_bomb_wx(int i);
float arena_bomb_wy(int i);
float arena_bomb_wz(int i);
void  arena_bomb_set_slot(int i, int slot);
int   arena_bomb_get_slot(int i);
int   arena_is_actor_slot(int slot);
/* A1.2c slice 2: blast effects (patch-callable). */
int   arena_spike_once(void);      /* 1 on first call only (ID-spike latch) */
int   arena_sweep_active(void);    /* 1 during the entry window (boss cleanup) */
int   arena_spike_next(void);      /* next spike step 0..9 once per Q edge, else -1 */
int   arena_blast_new(int i);      /* 1 once per blast birth; call all i every frame */
float arena_blast_wx(int i);
float arena_blast_wy(int i);
float arena_blast_wz(int i);
int   arena_blast_active(int i);   /* ttl != 0 */
float arena_blast_wr(int i);       /* world-units current blast radius */
void  arena_blastactor_set_slot(int i, int slot);   /* 4 pooled blast actors */
int   arena_blastactor_get_slot(int i);
#ifdef __cplusplus
}
#endif
#endif
