/*
 * fdm - ein kleines Flugmodell: so viel Physik, dass Neigen wirklich fliegt.
 *
 * Die Kennwerte kommen aus FlightGears eigenen JSBSim-Daten, umgesetzt von
 * `acftconv.py` in eine Datei von rund einem Kilobyte: Masse, Fluegelflaeche,
 * die Auftriebs- und Widerstandskurve ueber dem Anstellwinkel, der Beitrag
 * der Klappen, Leistung und Drehzahlbereich.  Damit fliegt jedes Flugzeug des
 * Hangars halbwegs wie es soll, ohne dass ein einziges Modell mitkommt.
 *
 * Alles in SI; was die Instrumente zeigen, rechnet der Anzeigeteil um.
 */
#ifndef FDM_H
#define FDM_H

#define FDM_TABLE_MAX 40

/* Stuetzstellen einer JSBSim-Tabelle, linear dazwischen, ausserhalb flach. */
struct fdm_table {
    int n;
    float x[FDM_TABLE_MAX];
    float y[FDM_TABLE_MAX];
};

struct fdm_aircraft {
    char name[32];
    float mass_kg;
    float wing_area_m2;
    float wing_span_m;
    float thrust_max_n;
    float rpm_idle, rpm_max;
    float cd0;
    float cd_gear;          /* was das ausgefahrene Fahrwerk kostet */
    struct fdm_table cl_alpha;   /* Anstellwinkel in Grad -> Auftriebsbeiwert */
    struct fdm_table cd_alpha;
    struct fdm_table cl_flap;    /* Klappenstellung in Grad -> Zuschlag */
    struct fdm_table cd_flap;
};

struct fdm_state {
    float alt_m;            /* ueber dem Datum der Kachel, nicht ueber Grund */
    float ground_m;         /* wo der Boden liegt */
    float v_ms;             /* Fahrt durch die Luft */
    float gamma_deg;        /* Bahnneigung */
    float pitch_deg, roll_deg, heading_deg;
    float alpha_deg;
    float rpm;
    int engine_on;              /* 0 = abgestellt: keine Drehzahl, kein Schub */
    float vs_ms;            /* Steigen, aus v und gamma */
    float north_m, east_m;  /* Ort in der Kachel, oertlich gerechnet */
    int on_ground;
};

void fdm_default(struct fdm_aircraft *a);              /* eingebaute Cessna 172 */
int fdm_load(struct fdm_aircraft *a, const char *path); /* 1 = gelesen */
float fdm_lookup(const struct fdm_table *t, float x);

void fdm_init(struct fdm_state *s, const struct fdm_aircraft *a);

/* stick_roll und stick_pitch sind -1..1, throttle und flaps 0..1. */
void fdm_step(struct fdm_state *s, const struct fdm_aircraft *a, float dt,
              float stick_roll, float stick_pitch, float rudder, float throttle,
              float flaps, int brake, int gear_down);

#endif
