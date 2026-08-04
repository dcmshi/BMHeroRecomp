#ifndef VERB_SCRIPT_H
#define VERB_SCRIPT_H
#include <stdint.h>

/* One choreography row. Times are SIM TICKS from the script origin; the
 * input callback runs at 2 polls per tick, and verb_apply is the ONLY place
 * that conversion exists (the 2:1 clock has produced three separate bugs
 * when hand-converted). dur == 0 rows are pure markers. */
typedef struct {
    const char* name;    /* marker fired at row start; NULL = unnamed */
    uint32_t    start;   /* ticks */
    uint32_t    dur;     /* ticks; 0 = marker only */
    uint16_t    buttons; /* N64 mask OR'd in while active */
    int8_t      stick_y; /* -1 / 0 / +1 full deflection. THE SIGNS READ
                          * INVERTED: -1 drives +Z, which is where a set bomb
                          * sits. Flipping one to "correct" a direction stops
                          * the goldens reproducing - an uninverted walkoff
                          * kicked the set bomb 20 frames before kickrun. */
} VerbRow;

#define VERB_A 0x8000
#define VERB_B 0x4000
#define VERB_R 0x0010

/* The VANILLA single-player script (ARENA_ORACLE=1). Ticks = the historical
 * poll windows / 2, verbatim - the goldens byte-identity check depends on
 * exact reproduction. Overlapping rows are intended (stick during a hold). */
static const VerbRow kOracleScript[] = {
    { "walk",      150,  60, 0,      -1 },
    { "stand",     210,   0, 0,       0 },
    { "dropB",     240,   2, VERB_B,  0 },
    { "holdB",     450,  30, VERB_B,  0 },
    { "releaseB",  480,   0, 0,       0 },
    { "setR",      630,   2, VERB_R,  0 },
    { "walkoff",   660,  20, 0,      +1 },
    { "kickrun",   680,  30, 0,      -1 },
    { "jumpA",     870,   3, VERB_A,  0 },
    { "airsetR",   876,   2, VERB_R,  0 },
    { "carryB",   1050,  45, VERB_B,  0 },   /* B held through carrywalk */
    { "carrywalk",1065,  30, 0,      -1 },
    { "carryrel", 1095,   0, 0,       0 },
    { "holdlong", 1200, 240, VERB_B,  0 },   /* B held through windupwalk */
    { "windupwalk",1380, 60, 0,      -1 },
    { "spreadrel",1440,   0, 0,       0 },
    /* Step clear of the spread fan, then WAIT: the fan is 4 bombs = the game's
     * WHOLE pool [2..5], and a set attempted before they fuse out (~106f, the
     * vanilla set-bomb fuse) SILENTLY spawns nothing (Get_InactiveObject) -
     * which is why setR2 sits 200 ticks later, not 20. */
    { NULL,       1450,  30, 0,      +1 },
    { "setR2",    1650,   2, VERB_R,  0 },
    { "jumpon",   1670,   3, VERB_A,  0 },
    { "carryjump",1840,  32, VERB_B,  0 },   /* B held to the midair release */
    { "jumpB",    1850,   3, VERB_A,  0 },
    { "relairB",  1872,   0, 0,       0 },
    { "DONE",     2000,   0, 0,       0 },
};

/* The BATTLE mode-13 probe. Shared names (carryB, carrywalk, carryrel,
 * holdlong, windupwalk, carryjump, jumpB, relairB, setR2) are PREFIX-SHAPED
 * copies of the vanilla verbs: identical buttons/stick from the verb's start,
 * so the differ's min-window truncation makes them comparable. Windows that
 * cannot match (fuse 150 vs 106, the spread arming at 120 ticks) either stop
 * early (holdlong = 110t, under the spread) or use battle-only names
 * (holdrel, jumpon). Everything starts after the 180-tick countdown.
 *
 * EVERY GROUNDED RELEASE MUST COME FROM A STANDSTILL. Measured 2026-08-02: a
 * release on the same tick the stick let go threw the bomb 5 ticks before it
 * impact-detonated ON the still-running player ([throw] t415 -> [blastvis]
 * t420 -> [hitpose] t427), versus a 12-tick flight that lands clear when the
 * walk stopped 20+ ticks earlier. The tumble is not just noise: the gate's
 * mode-13 soak treats [hitpose] as the run-complete signal, so a mid-script
 * one KILLS the boot before setR2/jumpon ever run. Hence the ~22-tick stand
 * between windupwalk and holdrel. */
static const VerbRow kBattleScript[] = {
    { "carryB",    125,  15, VERB_B,  0 },
    { "carrywalk", 140,  60, 0,      -1 },
    { NULL,        140,  85, VERB_B,  0 },   /* B continues to the release */
    { "carryrel",  225,   0, 0,       0 },   /* 25t after the walk: flies clear */
    { "holdlong",  250, 110, VERB_B,  0 },   /* stops before the 120t spread */
    { "windupwalk",318,  20, 0,      -1 },   /* charged (hold+68) and moving */
    { "holdrel",   360,   0, 0,       0 },   /* battle-only: 22t after the walk */
    { "carryjump", 385,  32, VERB_B,  0 },
    { "jumpB",     395,   3, VERB_A,  0 },
    { "relairB",   417,   0, 0,       0 },
    { "setR2",     455,   2, VERB_R,  0 },
    { "jumpon",    480,   3, VERB_A,  0 },
    /* Battle-only window CLOSER (task #30): jumpon's jump+landing+idle are
     * comparable against vanilla, but the mode-13 fuse (150 vs 106) pushes
     * the hit reaction ~44t later BY DESIGN - no clip work can reconcile
     * that run. This marker ends jumpon's compared window after the landing
     * settles and before either side's blast; the hit clip stays covered by
     * the bespoke [hitpose] check. 535 = jumpon + arc (~32t) + squat (6) +
     * an idle tail with margin. */
    { "postjump",  535,   0, 0,       0 },
};

/* poll is 1-based and runs at 2x tick rate. Named rows mark once, on their
 * first active poll (or their start poll for dur==0 markers). */
static inline void verb_apply(const VerbRow* rows, int n_rows, uint32_t poll,
                              uint16_t* buttons, float* stick_y,
                              void (*mark)(const char*)) {
    int i;
    for (i = 0; i < n_rows; i++) {
        uint32_t p0 = rows[i].start * 2u;
        uint32_t p1 = (rows[i].start + rows[i].dur) * 2u;
        if (rows[i].name && poll == p0 && mark) mark(rows[i].name);
        if (poll >= p0 && poll < p1) {
            *buttons |= rows[i].buttons;
            if (rows[i].stick_y > 0) *stick_y =  1.0f;
            if (rows[i].stick_y < 0) *stick_y = -1.0f;
        }
    }
}
#endif
