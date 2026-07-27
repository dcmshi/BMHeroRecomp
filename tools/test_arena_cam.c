/* Host-side guards for the fixed-camera pose (A1.5). The patch itself runs as
 * MIPS inside the game and can't be unit-tested, but the pose CONSTANTS and math
 * are pure and portable - so the assumptions baked into them are machine-checked
 * here instead of living as comments.
 *
 * Build/run via tools\run-cam-tests.ps1 (also wired into build.ps1). */
#include <stdio.h>
#include <math.h>
#include "../patches/arena_cam.h"
/* The SIM's geometry, straight from the submodule. Including it here is the
 * point: it makes invariant 8.5a (the sim's collidable bounds must track the
 * rendered map) a compile-and-run check across the two repos instead of a note
 * that nobody re-reads. arena_geom.h is header-only, integer-only Q20.12. */
#include "../lib/bmhero-arena/src/arena/arena_geom.h"

static int failures = 0;
#define CHECK(c, ...) do { if(!(c)){ failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while(0)

#define PI 3.14159265358979323846
#define EPS 1e-5

int main(void) {
    /* ---- ASSUMPTION 1: the precomputed trig literals match their angles. ----
     * The patch must not call sinf/cosf (an emitted libcall links silently and
     * jumps to 0 - integration notes 8.11), so the trig is hardcoded. That makes
     * "changed the angle, forgot to update the literal" a silent, plausible bug
     * that would aim the camera somewhere arbitrary. This is the guard. */
    double rp = ARENA_CAM_PITCH_DEG * PI / 180.0;
    double ry = ARENA_CAM_YAW_DEG   * PI / 180.0;
    CHECK(fabs(ARENA_CAM_SIN_PITCH - sin(rp)) < EPS,
          "ARENA_CAM_SIN_PITCH %.7f != sin(%.1f deg) = %.7f - update the literal",
          ARENA_CAM_SIN_PITCH, ARENA_CAM_PITCH_DEG, sin(rp));
    CHECK(fabs(ARENA_CAM_COS_PITCH - cos(rp)) < EPS,
          "ARENA_CAM_COS_PITCH %.7f != cos(%.1f deg) = %.7f - update the literal",
          ARENA_CAM_COS_PITCH, ARENA_CAM_PITCH_DEG, cos(rp));
    CHECK(fabs(ARENA_CAM_SIN_YAW - sin(ry)) < EPS,
          "ARENA_CAM_SIN_YAW %.7f != sin(%.1f deg) = %.7f - update the literal",
          ARENA_CAM_SIN_YAW, ARENA_CAM_YAW_DEG, sin(ry));
    CHECK(fabs(ARENA_CAM_COS_YAW - cos(ry)) < EPS,
          "ARENA_CAM_COS_YAW %.7f != cos(%.1f deg) = %.7f - update the literal",
          ARENA_CAM_COS_YAW, ARENA_CAM_YAW_DEG, cos(ry));

    /* ---- ASSUMPTION 2: yaw 0 keeps the input mapping an identity. ----
     * The game rotates the stick in place by gView.rot.y (func_80024744,
     * camtype 6 - notes 8.11). Yaw 0 makes that rotation the identity, which is
     * the whole reason a held stick direction stops curving. A non-zero yaw
     * silently reintroduces the bug this slice exists to fix. */
    CHECK(ARENA_CAM_YAW_DEG == 0.0f,
          "ARENA_CAM_YAW_DEG is %.2f, not 0 - the game rotates the stick by "
          "rot.y, so a non-zero yaw reintroduces the curved-movement bug",
          ARENA_CAM_YAW_DEG);

    /* ---- ASSUMPTION 3: the pitch keeps foreshortening acceptable. ----
     * Screen travel for toward/away motion vs across motion is sin(pitch).
     * 60deg -> 0.87 (near-equal, the design choice); 45deg -> 0.71 (the artifact
     * we removed). This encodes the DECISION, so a future pitch change that
     * reintroduces bad foreshortening fails here instead of in a playtest. */
    CHECK(arena_cam_foreshorten() >= 0.85f,
          "foreshorten factor %.3f < 0.85 (pitch %.1f deg) - W/S will read "
          "noticeably slower than A/D again",
          (double)arena_cam_foreshorten(), ARENA_CAM_PITCH_DEG);

    /* ---- ASSUMPTION 4: our effective-yaw trig matches the GAME'S +90 offset. --
     * func_8001994C (decomp src/boot/17930.c:605) computes the eye from
     * view_rot_y = gView.rot.y + 90. Our COS/SIN_YAW_EFF literals model that. If
     * they drift from the game's formula, the camera ends up pointing somewhere
     * we never intended - and because the game recomputes eye every frame, the
     * mistake would look like "the override didn't take" rather than "the pose
     * is wrong", which is a much harder bug to read. */
    double reff = (ARENA_CAM_YAW_DEG + ARENA_CAM_YAW_OFFSET_DEG) * PI / 180.0;
    CHECK(fabs(ARENA_CAM_COS_YAW_EFF - cos(reff)) < EPS,
          "ARENA_CAM_COS_YAW_EFF %.7f != cos(yaw+%.0f) = %.7f",
          ARENA_CAM_COS_YAW_EFF, ARENA_CAM_YAW_OFFSET_DEG, cos(reff));
    CHECK(fabs(ARENA_CAM_SIN_YAW_EFF - sin(reff)) < EPS,
          "ARENA_CAM_SIN_YAW_EFF %.7f != sin(yaw+%.0f) = %.7f",
          ARENA_CAM_SIN_YAW_EFF, ARENA_CAM_YAW_OFFSET_DEG, sin(reff));

    /* The game nudges rot.x of exactly 90 or 270 by 1 degree (gimbal guard).
     * Sitting on one of those would silently shift our pitch. */
    CHECK(ARENA_CAM_PITCH_DEG != 90.0f && ARENA_CAM_PITCH_DEG != 270.0f,
          "pitch %.1f hits the game's gimbal guard (func_8001994C nudges 90/270 "
          "by 1 degree), so the real pitch would differ from the declared one",
          ARENA_CAM_PITCH_DEG);

    /* ---- ASSUMPTION 5: the resulting eye pose is geometrically sane. ---- */
    float ox, oy, oz;
    arena_cam_eye_offset(&ox, &oy, &oz);
    CHECK(oy > 0.0f, "camera must sit ABOVE the arena (oy=%.1f)", (double)oy);
    CHECK(fabs(ox) < EPS,
          "yaw 0 (effective 90) must give zero X offset (ox=%.4f) - otherwise "
          "the arena's long axis stops being horizontal on screen", (double)ox);
    CHECK(oz > 0.0f,
          "with the game's +90 offset, yaw 0 must put the eye at +Z (oz=%.1f)",
          (double)oz);
    double len = sqrt((double)ox*ox + (double)oy*oy + (double)oz*oz);
    CHECK(fabs(len - ARENA_CAM_DIST) < 1e-2,
          "eye offset length %.3f != ARENA_CAM_DIST %.3f", len, (double)ARENA_CAM_DIST);
    CHECK(ARENA_CAM_DIST > 0.0f, "ARENA_CAM_DIST must be positive");

    /* ---- ASSUMPTION 6: the SIM's arena matches the MEASURED floor (8.5a). ----
     * This is the invariant the A1.2g fall came from violating. The sim's bounds
     * and the rendered floor are edited in two different repos, so nothing but a
     * test keeps them together: change arena_geom.h without re-measuring, or
     * re-measure without updating the sim, and the player walks off the world
     * again (the game's ground query returns its no-floor sentinel and the
     * player hangs off-map). Tolerance is one raster step (10 Hero units) - the
     * resolution the floor was actually measured at, so we can't claim better. */
    const double sim_half_x = (double)arena_nitros_standin.half_x / 4096.0;
    const double sim_half_z = (double)arena_nitros_standin.half_z / 4096.0;
    const double want       = (double)ARENA_FLOOR_HALF / (double)ARENA_RENDER_SCALE;
    CHECK(fabs(sim_half_x * ARENA_RENDER_SCALE - ARENA_FLOOR_HALF) <= 10.0,
          "sim half_x %.4f u = %.1f Hero, but the measured floor half is %.1f - "
          "sim bounds and rendered map have drifted apart (8.5a)",
          sim_half_x, sim_half_x * ARENA_RENDER_SCALE, (double)ARENA_FLOOR_HALF);
    CHECK(fabs(sim_half_z * ARENA_RENDER_SCALE - ARENA_FLOOR_HALF) <= 10.0,
          "sim half_z %.4f u = %.1f Hero, but the measured floor half is %.1f - "
          "sim bounds and rendered map have drifted apart (8.5a)",
          sim_half_z, sim_half_z * ARENA_RENDER_SCALE, (double)ARENA_FLOOR_HALF);
    CHECK(fabs(sim_half_x - sim_half_z) < 1e-3,
          "the measured floor is SQUARE, so the sim's arena must be too "
          "(half_x %.4f vs half_z %.4f)", sim_half_x, sim_half_z);
    /* Every spawn must be inside the floor, or a player starts off the map. */
    for (int i = 0; i < 4; i++) {
        double sx = (double)arena_nitros_standin.spawns[i].x / 4096.0;
        double sz = (double)arena_nitros_standin.spawns[i].z / 4096.0;
        CHECK(fabs(sx) < want && fabs(sz) < want,
              "spawn %d at (%.3f,%.3f) is outside the measured floor (half %.4f u)",
              i, sx, sz, want);
    }
    /* The camera must actually be able to frame the floor it is aimed at. */
    CHECK(ARENA_CAM_DIST > 2.0f * ARENA_FLOOR_HALF * ARENA_CAM_SIN_PITCH * 0.75f,
          "ARENA_CAM_DIST %.0f is too short to frame a %.0f-deep floor at pitch "
          "%.0f", (double)ARENA_CAM_DIST, 2.0 * ARENA_FLOOR_HALF,
          (double)ARENA_CAM_PITCH_DEG);

    if (!failures) { printf("ALL CAMERA POSE TESTS PASSED\n"); return 0; }
    printf("%d FAILURE(S)\n", failures); return 1;
}
