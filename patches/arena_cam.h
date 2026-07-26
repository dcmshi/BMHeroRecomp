/* Fixed arena camera pose (A1.5).
 *
 * Pure constants + math, NO game types - so this header compiles BOTH in the
 * MIPS patch and in the host test (tools/test_arena_cam.c).
 *
 * NEVER call sinf/cosf here. An emitted math libcall in patch code links
 * silently and jumps to 0 (integration notes 8.11). Pitch and yaw are
 * compile-time constants, so their trig is precomputed as literals below and
 * the host test asserts the literals still match the declared angles. */
#ifndef ARENA_CAM_H
#define ARENA_CAM_H

/* Pitch measured UP FROM THE GROUND PLANE: 90 = straight down, 0 = horizontal.
 * 60 chosen so toward/away motion reads at sin(60)=0.87 of across motion -
 * near-equal - while keeping enough depth to judge bomb arcs. See the spec. */
#define ARENA_CAM_PITCH_DEG   60.0f

/* MUST stay 0. The game rotates the stick in place by gView.rot.y for
 * gCameraType in {1,2,5,6,7,8} (the arena is type 6, notes 8.11). Yaw 0 makes
 * that rotation the identity, so stick-up maps to a fixed world axis. It also
 * puts the arena's long axis (half_x 7.9 vs half_z 3.87) horizontal on screen. */
#define ARENA_CAM_YAW_DEG      0.0f

/* Precomputed trig - guarded by tools/test_arena_cam.c. */
#define ARENA_CAM_SIN_PITCH   0.8660254f   /* sin(60) */
#define ARENA_CAM_COS_PITCH   0.5f         /* cos(60) */
#define ARENA_CAM_SIN_YAW     0.0f         /* sin(0)  */
#define ARENA_CAM_COS_YAW     1.0f         /* cos(0)  */

/* Hero world units. MEASURED, not guessed - we don't know the FOV, so the A1.5
 * probe (ARENA_AUTO_BATTLE=6) establishes the starting value and it is then
 * iterated by screenshot. The arena is 1896 x 928 Hero units
 * (2 * half_x/half_z * g_scale 120). */
#define ARENA_CAM_DIST      1600.0f
#define ARENA_CAM_AT_Y_LIFT   60.0f   /* aim slightly above the floor, not at it */

/* Which side of the arena the eye sits on. Resolved by the probe: guessing wrong
 * yields a MIRRORED view rather than an obvious failure. */
#define ARENA_CAM_Z_SIGN    (-1.0f)

/* Eye position relative to `at`, for the fixed pose. */
static inline void arena_cam_eye_offset(float* ox, float* oy, float* oz) {
    *ox = ARENA_CAM_DIST * ARENA_CAM_COS_PITCH * ARENA_CAM_SIN_YAW;
    *oy = ARENA_CAM_DIST * ARENA_CAM_SIN_PITCH;
    *oz = ARENA_CAM_DIST * ARENA_CAM_COS_PITCH * ARENA_CAM_COS_YAW * ARENA_CAM_Z_SIGN;
}

/* Screen travel for toward/away motion relative to across motion.
 * 1.0 = no foreshortening at all (straight down). */
static inline float arena_cam_foreshorten(void) { return ARENA_CAM_SIN_PITCH; }

#endif
