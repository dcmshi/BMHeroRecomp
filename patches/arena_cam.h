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
 * 35 = the user's pick from the 2026-08-01 A/B round ("closer lower angle
 * looks better" vs the 60-degree near-top-down); the old 60 rationale
 * (near-equal toward/away vs across reading) lost to feel. Env override:
 * ARENA_CAM_PITCH. */
#define ARENA_CAM_PITCH_DEG   35.0f

/* MUST stay 0. The game rotates the stick in place by gView.rot.y for
 * gCameraType in {1,2,5,6,7,8} (the arena is type 6, notes 8.11). Yaw 0 makes
 * that rotation the identity, so stick-up maps to a fixed world axis. (The old
 * "and it puts the arena's long axis horizontal" rationale no longer applies:
 * the measured floor is SQUARE — see ARENA_FLOOR_HALF below — so there is no
 * long axis. Yaw 0 still stands on the input-mapping argument alone.) */
#define ARENA_CAM_YAW_DEG      0.0f

/* ---- MEASURED map geometry (probe mode 7, 2026-07-26) ---------------------
 * The MAP_NITROS_1 floor, measured by asking the game's OWN ground query
 * (func_80078168) on a grid — not by walking a player, which only ever measures
 * how far the player could GO. Two independent passes (50-unit and 10-unit
 * grids; 6,561 and 40,401 samples) agree on the extent exactly:
 *
 *     a filled SQUARE, x in [-950,950], z in [-950,950], flat at y = 240,
 *     no holes, no pillars, no steps.
 *
 * These are the single source of truth for BOTH sides of invariant 8.5a:
 *   - the render anchor: arena_bridge.cpp maps the sim's arena CENTRE onto
 *     (ARENA_FLOOR_CX, ARENA_FLOOR_CZ), instead of anchoring on wherever the
 *     player happened to spawn;
 *   - the sim's collidable bounds: arena_geom.h half_x/half_z must equal
 *     ARENA_FLOOR_HALF / ARENA_RENDER_SCALE.
 * tools/test_arena_cam.c asserts those two agree — 8.5a as a test, not a note. */
#define ARENA_FLOOR_CX       0.0f
#define ARENA_FLOOR_CZ       0.0f
#define ARENA_FLOOR_Y      240.0f
#define ARENA_FLOOR_HALF   950.0f
#define ARENA_RENDER_SCALE 120.0f   /* Hero units per sim unit (arena_bridge.cpp) */

/* MEASURED HAZARD CORNERS (probe mode 7 surface-type raster, 2026-07-27).
 * The Nitros room puts damage tiles in four 250x250 corner blocks - surface
 * type 0xF7, which 69AA0.c:411 keys the damage flag off. They cost the
 * game-side player health and stun him briefly. The sim does not model them.
 *
 * A tile is hazardous where |x| >= this AND |z| >= this:
 *
 *     z= 950  ccccc.....bbbbb.........bbbbb.....ccccc
 *     z= 750  ccccc.....bbbbb.........bbbbb.....ccccc     c = 0xF7 hazard
 *     z= 700  bbbbb..........bbbbaaaaa..........aaaaa
 *
 * This matters because our sim spawns USED to sit at +-780 Hero - inside them.
 * test_arena_cam.c now asserts every spawn clears this region by a player
 * radius. Suppressing the tiles outright is the remaining A1.2g work. */
#define ARENA_HAZARD_MIN   750.0f

/* Precomputed trig - guarded by tools/test_arena_cam.c. */
#define ARENA_CAM_SIN_PITCH   0.8660254f   /* sin(60) */
#define ARENA_CAM_COS_PITCH   0.5f         /* cos(60) */
#define ARENA_CAM_SIN_YAW     0.0f         /* sin(0)  */
#define ARENA_CAM_COS_YAW     1.0f         /* cos(0)  */

/* Hero world units. The camera WORKS: at this distance the whole 1900x1900 floor
 * is framed and centred, with margin below the near edge. Verified against a
 * RenderDoc capture, not just a screenshot (see below). Override at runtime with
 * the ARENA_CAM_DIST env var (arena_bridge.cpp arena_cam_dist) - no rebuild.
 *
 * Chosen from a sweep measured with tools/shot_measure.py on 1600x900 frames:
 * at 2400 the floor's near edge runs off the bottom (bbox y ends at 899 of 899);
 * 2800 fits with margin (y[286..876]); 3200 wastes screen. Floor centroid sits
 * at x~815 of 1600 - i.e. centred, as intended.
 *
 * A CAUTION WORTH KEEPING. This value was previously "1800, best of a bad set",
 * and three separate root causes were hypothesised for why the arena wouldn't
 * frame - gView.at being overwritten, the level's far clip plane, level-chunk
 * view culling. All three were WRONG. The camera had been correct the entire
 * time; tools/capture-game.ps1 was silently capturing only the TOP-LEFT QUARTER
 * of the frame (a DPI bug - see the banner in that script), so the centred arena
 * appeared shoved into a corner. A RenderDoc capture of the same frame settled it
 * in one shot. Integration notes 8.17.
 *
 * 2026-08-01: 2800 -> 1600, the user's A/B pick ("really zoomed out, not a fan
 * of the top down view"; paired with pitch 60 -> 35). The 2800 whole-floor
 * framing analysis above still holds for INSPECTION shots - use
 * ARENA_CAM_DIST=2800 ARENA_CAM_PITCH=60 to reproduce it. At 1600/35 the
 * camera no longer frames the whole floor; play framing beats map framing. */
#define ARENA_CAM_DIST      1600.0f
/* The game's own rail camera aims at y=340 with origin_y=240, i.e. 100 above the
 * floor anchor (measured, ARENA_AUTO_BATTLE=6). Matching that keeps the horizon
 * where the room was authored for. */
#define ARENA_CAM_AT_Y_LIFT  100.0f

/* ---------------------------------------------------------------------------
 * WHO OWNS eye AND up.
 *
 * The DESIGN assumed the game derives them and we must not write them. That
 * turned out to be wrong IN THIS ARENA, and the patch now writes eye/up itself
 * (arena_render.c arena_cam_stamp) - the derivation below is gated on
 * D_8016E134 == 0 and that gate is closed here. The evidence: with only
 * at/rot/dist written, the picture was PIXEL-IDENTICAL across a 2x change of
 * ARENA_CAM_DIST and the logged eye never left the rail camera's last value.
 * The formula below is still exactly what we reproduce, so it stays documented
 * (and ASSUMPTION 4 in the host test still guards our trig against it).
 *
 * func_8001994C (decomp src/boot/17930.c:605, recovered via tools/decomp-func.ps1)
 * computes, from at + rot + dist:
 *
 *     view_rot_y = gView.rot.y + 90.0f;          <-- NOTE THE +90 OFFSET
 *     eye.x = dist * cos(view_rot_y) * cos(rot.x) + at.x;
 *     eye.y = dist * sin(rot.x)                  + at.y;
 *     eye.z = dist * sin(view_rot_y) * cos(rot.x) + at.z;
 *     up.y  = (rot.x >= 90 && rot.x < 270) ? -1 : +1;
 *
 * Consequences of the +90 offset, at our yaw of 0:
 *   effective yaw = 90  =>  cos = 0, sin = 1
 *   => eye.x = at.x            (no lateral offset)
 *   => eye.z = at.z + dist*cos(pitch)   (camera sits at +Z, looking back at -Z)
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
