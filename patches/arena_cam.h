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

/* ---------------------------------------------------------------------------
 * THE GAME DERIVES eye AND up ITSELF - we must NOT write them.
 *
 * func_8001994C (decomp src/boot/17930.c:605, recovered via tools/decomp-func.ps1)
 * recomputes them every frame from at + rot + dist, gated on D_8016E134 == 0:
 *
 *     view_rot_y = gView.rot.y + 90.0f;          <-- NOTE THE +90 OFFSET
 *     eye.x = dist * cos(view_rot_y) * cos(rot.x) + at.x;
 *     eye.y = dist * sin(rot.x)                  + at.y;
 *     eye.z = dist * sin(view_rot_y) * cos(rot.x) + at.z;
 *     up.y  = (rot.x >= 90 && rot.x < 270) ? -1 : +1;
 *
 * So the patch writes ONLY at / rot.x / rot.y / dist and lets the game finish
 * the job - self-consistent with its own conventions, and nothing to fight.
 *
 * Consequences of the +90 offset, at our yaw of 0:
 *   effective yaw = 90  =>  cos = 0, sin = 1
 *   => eye.x = at.x            (no lateral offset)
 *   => eye.z = at.z + dist*cos(pitch)   (camera sits at +Z, looking back at -Z)
 * which puts the arena's LONG axis (X) horizontal on screen, as intended.
 * There is no Z_SIGN constant to guess - the game's formula settles it.
 *
 * The game also guards the gimbal (rot.x of exactly 90 or 270 is nudged by 1),
 * so our 60 is safely away from that.
 * ------------------------------------------------------------------------- */

/* The game's +90 yaw offset, and the trig of the EFFECTIVE yaw (yaw + 90).
 * At yaw 0: effective 90 -> cos 0, sin 1. Guarded by the host test. */
#define ARENA_CAM_YAW_OFFSET_DEG   90.0f
#define ARENA_CAM_COS_YAW_EFF       0.0f   /* cos(0 + 90) */
#define ARENA_CAM_SIN_YAW_EFF       1.0f   /* sin(0 + 90) */

/* A MODEL of where the game will place the eye given what we write. The patch
 * does not use this - the game computes the real thing. The host test uses it to
 * check our framing reasoning against the game's actual formula. */
static inline void arena_cam_eye_offset(float* ox, float* oy, float* oz) {
    *ox = ARENA_CAM_DIST * ARENA_CAM_COS_YAW_EFF * ARENA_CAM_COS_PITCH;
    *oy = ARENA_CAM_DIST * ARENA_CAM_SIN_PITCH;
    *oz = ARENA_CAM_DIST * ARENA_CAM_SIN_YAW_EFF * ARENA_CAM_COS_PITCH;
}

/* Screen travel for toward/away motion relative to across motion.
 * 1.0 = no foreshortening at all (straight down). */
static inline float arena_cam_foreshorten(void) { return ARENA_CAM_SIN_PITCH; }

#endif
