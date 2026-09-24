/* Prueft das Geometriemodell ohne Geraet.
 *
 * Drei Fragen, die ein Flugmodell beantworten koennen muss:
 *   1. Gleitet es?  Ohne Schub soll es sinken und dabei Fahrt halten,
 *      nicht durchsacken und nicht beschleunigen.
 *   2. Hebt es ab?  Mit Vollgas soll es rollen, dann fliegen.
 *   3. Daempft sich das Rollen?  Ein fester Querruderausschlag muss zu
 *      einer *begrenzten* Rollrate fuehren -- genau das kann das
 *      Tabellenmodell nicht, weil dort der Knueppel die Rate vorgibt.
 *
 *   bladetest [datei.fdm]
 */
#include "blade.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static void kopf(const struct blade_aircraft *a) {
    printf("%s: %.0f kg, %d Flaechen, %d Beine, Traegheit %.0f/%.0f/%.0f\n",
           a->name, a->mass_kg, a->nsurf, a->ngear, a->ixx, a->iyy, a->izz);
}

static void gleiten(const struct blade_aircraft *a) {
    struct blade_state s;
    blade_init(&s, a);
    s.on_ground = 0; s.engine_on = 0;
    s.alt_m = 1000.0f; s.ground_m = 0.0f;
    s.u = 45.0f;
    printf("\nGleiten ohne Schub (Start 1000 m, 45 m/s):\n");
    for (int i = 0; i < 6000; ++i) {
        blade_step(&s, a, 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1);
        if (i % 1000 == 999)
            printf("  %2.0f s  %6.0f m  %5.1f m/s  Sinken %+5.1f m/s  "
                   "Anstellwinkel %+5.1f  Nicken %+5.1f  Rollen %+6.1f\n",
                   (i + 1) * 0.01, s.alt_m, s.v_ms, s.vs_ms,
                   s.alpha_deg, s.pitch_deg, s.roll_deg);
    }
    float gleitzahl = s.v_ms > 0.1f && s.vs_ms < -0.05f ? -s.v_ms / s.vs_ms : 0.0f;
    printf("  Gleitzahl daraus: %.1f\n", gleitzahl);
}

static void abheben(const struct blade_aircraft *a) {
    struct blade_state s;
    blade_init(&s, a);
    s.engine_on = 1;
    s.ground_m = 0.0f;
    blade_place_on_ground(&s, a);
    printf("\nStart mit Vollgas:\n");
    int ab = -1;
    for (int i = 0; i < 6000; ++i) {
        /* ab 25 m/s ziehen, wie es ein Mensch auch taete */
        float stick = s.v_ms > 25.0f ? 0.35f : 0.0f;
        blade_step(&s, a, 0.01f, 0.0f, stick, 0.0f, 1.0f, 0.0f, 0, 1);
        if (ab < 0 && !s.on_ground && s.alt_m > 1.0f) ab = i;
        if (i % 1000 == 999)
            printf("  %2.0f s  %5.1f m/s  %6.1f m  Nicken %+5.1f  %s\n",
                   (i + 1) * 0.01, s.v_ms, s.alt_m, s.pitch_deg,
                   s.on_ground ? "am Boden" : "in der Luft");
    }
    if (ab >= 0) printf("  abgehoben nach %.1f s\n", ab * 0.01);
    else printf("  NICHT abgehoben\n");
}

static void rollen(const struct blade_aircraft *a) {
    struct blade_state s;
    blade_init(&s, a);
    s.on_ground = 0; s.engine_on = 1;
    s.alt_m = 1000.0f; s.ground_m = 0.0f;
    s.u = 50.0f;
    printf("\nQuerruder halb rechts, 4 s (die Rollrate muss sich einpendeln):\n");
    for (int i = 0; i < 400; ++i) {
        blade_step(&s, a, 0.01f, 0.5f, 0.0f, 0.0f, 0.6f, 0.0f, 0, 1);
        if (i % 100 == 99)
            printf("  %.1f s  Rollen %+6.1f  Rollrate %+6.1f Grad/s\n",
                   (i + 1) * 0.01, s.roll_deg, s.p * 57.2958f);
    }
}

int main(int argc, char **argv) {
    struct blade_aircraft a;
    blade_default(&a);
    if (argc > 1 && !blade_load(&a, argv[1])) {
        printf("%s nicht ladbar (fehlt `geometrie`?)\n", argv[1]);
        return 1;
    }
    kopf(&a);
    gleiten(&a);
    abheben(&a);
    rollen(&a);
    return 0;
}
