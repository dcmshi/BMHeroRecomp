/* Host-side guards for the fixed-camera pose (A1.5). The patch itself runs as
 * MIPS inside the game and can't be unit-tested, but the pose CONSTANTS and math
 * are pure and portable - so the assumptions baked into them are machine-checked
 * here instead of living as comments.
 *
 * Build/run via tools\run-cam-tests.ps1 (also wired into build.ps1). */
#include <stdio.h>
#include <math.h>
#include "../patches/arena_cam.h"

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

    if (!failures) { printf("ALL CAMERA POSE TESTS PASSED\n"); return 0; }
    printf("%d FAILURE(S)\n", failures); return 1;
}
