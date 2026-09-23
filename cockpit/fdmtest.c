/* Prueft das Flugmodell ohne Geraet: faellt ein Flugzeug ohne Schub? */
#include "fdm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    struct fdm_aircraft a;
    struct fdm_state s;
    fdm_default(&a);
    if (argc > 1 && !fdm_load(&a, argv[1])) { printf("%s nicht ladbar\n", argv[1]); return 1; }
    printf("%s: %.0f kg, %.1f m2, Schub %.0f N\n", a.name, a.mass_kg, a.wing_area_m2, a.thrust_max_n);
    fdm_init(&s, &a);
    s.engine_on = 0;
    s.on_ground = 0;
    s.alt_m = 600.0f;
    s.ground_m = 0.0f;
    s.v_ms = argc > 2 ? (float)atof(argv[2]) : 50.0f;
    s.pitch_deg = 0.0f;
    float t = 0.0f;
    for (int i = 0; i < 1200; ++i) {      /* 60 s bei 20 Hz */
        fdm_step(&s, &a, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1);
        t += 0.05f;
        if (i % 100 == 0)
            printf("%4.0f s  %6.1f m  %5.1f m/s (%5.1f kt)  Bahn %+5.1f Grad  "
                   "Steigen %+6.1f m/s  Anstellwinkel %+5.1f\n",
                   t, s.alt_m, s.v_ms, s.v_ms * 1.94384f, s.gamma_deg, s.vs_ms, s.alpha_deg);
    }
    return 0;
}
