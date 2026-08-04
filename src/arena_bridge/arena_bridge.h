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
/* HUD (A1.2g): sim state the reused Hero HUD is driven from. */
int   arena_player_hp(int i);       /* 0..TUNE_START_HP */
int   arena_player_stocks(int i);   /* rounds won this match */
int   arena_match_phase(void);      /* PHASE_* */
void  arena_dbg_dump(int i, int objID, int actionState);   /* A1.2b diag: log one object-table row */
float arena_get_bomber_off_x(int i);   /* A1.2b: (sim_pos_i - sim_pos_0).x * scale */
float arena_get_bomber_off_z(int i);
float arena_get_bomber_yaw(int i);
/* A1.2b puppet actors: state + frozen-origin world placement (patch-callable). */
void  arena_puppet_capture(uint32_t bx, uint32_t by, uint32_t bz, int level);
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
/* A1.4 set-bomb animation (patch-callable). */
int   arena_set_new(int i);              /* 1 once per player-i set-bomb edge */
void  arena_dbg_anim(int idx, int frame, int state);  /* anim idx+frame+actionState */
/* Explosion visual evidence: one line per blast->actor drive frame (bounded by
 * TUNE_BLAST_TTL per blast). wr crosses as a float BIT PATTERN. */
void  arena_dbg_blast(int k, int slot, int wrbits);
/* A1.5 camera probe. tag: 0=at 1=eye 2=rot 3=up 4=misc(x=gCameraType as a plain
 * int, y=dist bits). Other tags carry three float BIT PATTERNS - the export ABI
 * takes no float arguments. Probe-mode gating and throttling are native-side, so
 * the patch can call this unconditionally and stay stateless. */
void  arena_dbg_cam(int tag, int xbits, int ybits, int zbits);
/* A1.2g floor guard: 1 = player is off-map, restore arena_floor_last_*. */
int   arena_floor_guard(int xbits, int ybits, int zbits);
float arena_floor_last_x(void);
float arena_floor_last_y(void);
float arena_floor_last_z(void);
/* A1.2g floor raster (probe mode 7): native owns the grid cursor, the patch
 * supplies the game's own ground-query answer for each point. */
int   arena_floor_raster_active(void);   /* 1 while a raster is in progress    */
int   arena_floor_raster_next(void);     /* 1 = a point is ready in px/py/pz   */
float arena_floor_raster_px(void);
float arena_floor_raster_py(void);
float arena_floor_raster_pz(void);
void  arena_floor_raster_report(int sel, int hbits, int type);  /* sel, height, surface type */
/* A1.5: arena centre in Hero world coords (same frozen-origin mapping as the
 * puppets), for gView.at. */
float arena_cam_at_x(void);
float arena_cam_at_y(void);
float arena_cam_at_z(void);
float arena_cam_dist(void);   /* ARENA_CAM_DIST, env-overridable for framing */
float arena_cam_zfar(void);   /* battle-mode far clip; the level's own is too near */
int   arena_cam_enabled(void);/* 0 when ARENA_CAM_OFF=1 - runtime A/B for the camera */
int   arena_set_hold(void);   /* 1 while the set pose should be re-asserted */
int   arena_set_anim_index(void);  /* ARENA_SET_ANIM; default 41 */
int   arena_kick_anim_index(void); /* ARENA_KICK_ANIM; default 32 */
/* The action pose to re-assert this frame, or -1. Covers set AND kick; the
 * patch needs no edge of its own, because a one-shot trigger only survives a
 * frame against the walker (§8.18). */
int   arena_pose_anim(void);
int   arena_cam_follow(void);      /* ARENA_CAM_FOLLOW=1: aim at the player */
float arena_cam_pitch_deg(void);   /* ARENA_CAM_PITCH; trig computed natively */
float arena_cam_yaw_deg(void);     /* ARENA_CAM_YAW; effective trig natively */
float arena_cam_yaw_eff_sin(void); /* sin(yaw + 90deg) */
float arena_cam_yaw_eff_cos(void); /* cos(yaw + 90deg) */
float arena_cam_pitch_sin(void);
float arena_cam_pitch_cos(void);
int   arena_anim_sweep_index(void);/* ARENA_ANIM_SWEEP=<ticks>; -1 = off */
/* Battle button ownership (2026-08-01): the input callback latches the real
 * mask and strips the sim's verbs (B/Z/R) from the game's copy at the POLL;
 * the patch reads the latch for the sim's jump/bomb/set. */
void  arena_latch_buttons(int held);
int   arena_latched_buttons(void);
int   arena_contain_player_state(int cur);  /* walker PUSH(42) -> last non-push state */
int   arena_contain_player_vely(int velybits, int correcting); /* + last non-push Vel.y */
int   arena_push_entry_on(void);  /* 1 iff ARENA_PUSH_ENTRY=1: vanilla push entry in battle (A/B) */
/* Single-player oracle (spec 2026-08-01): ARENA_ORACLE=1 boots VANILLA
 * campaign with per-frame probes; goldens are extracted from the log.
 * Native owns all gating/throttling and the shared frame counter n; the
 * patch calls unconditionally and stays stateless. Floats cross as BITS. */
int   arena_oracle_mode(void);    /* 1 iff ARENA_ORACLE=1 (cached) */
int   arena_oracle_seen(void);    /* 1 once the in-level routine has run (mash-stop) */
void  arena_oracle_phase(const char* name);  /* main.cpp phase markers -> log */
void  arena_verb_mark(const char* name);     /* battle verb markers -> [verb] */
void  arena_oracle_frame(int level, int playerValid, int floorYbits, int playerYbits);
void  arena_oracle_anim(int idx, int framebits, int state);
void  arena_oracle_obj(int slot_state, int xbits, int ybits, int zbits);
#ifdef __cplusplus
}
#endif
#endif
