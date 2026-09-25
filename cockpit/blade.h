/*
 * blade - das Flugzeug als Geometrie statt als Tabelle.
 *
 * Das Verfahren ist dasselbe, das X-Plane benutzt und das in FlightGear
 * YASim heisst: Die Flaechen werden in Stuecke geschnitten, jedes Stueck
 * bekommt seine **eigene** Anstroemung -- also Fluggeschwindigkeit plus das,
 * was die Drehung des Flugzeugs an dieser Stelle dazutut -- und daraus
 * seinen eigenen Anstellwinkel, Auftrieb und Widerstand.  Summiert werden
 * Kraft *und* Moment, und integriert wird mit sechs Freiheitsgraden.
 *
 * Warum das mehr ist als die Tabelle daneben (fdm.c): Dort gibt der
 * Knueppel Drehraten vor, das Flugzeug hat also keine Traegheit und keine
 * Daempfung; ein Flaechenstueck, das beim Rollen schneller angestroemt
 * wird, kommt gar nicht vor.  Hier faellt beides von selbst heraus: Rollen
 * daempft sich, weil der aufsteigende Fluegel weniger anstroemt, und die
 * Nase sackt in der Kurve, weil das Hoehenleitwerk weniger traegt.
 *
 * Die Zahlen kommen aus FlightGears YASim-Dateien (`<wing>`, `<hstab>`,
 * `<vstab>`, `<gear>`, `<propeller>`), umgesetzt von acftconv.py.  Dort
 * wurden sie bisher zu zwei Beiwerten zusammengerechnet -- genau dieser
 * Schritt faellt jetzt weg.
 *
 * Koordinaten im Rumpfsystem, in Metern: x nach vorn, y nach rechts,
 * z nach unten.  Alles in SI.
 */
#ifndef BLADE_H
#define BLADE_H

#define BLADE_MAX_SURF 10
#define BLADE_MAX_SEG  6        /* Stuecke je Flaechenhaelfte */
#define BLADE_MAX_GEAR 4

/* Eine tragende Flaeche: Fluegel, Hoehen- oder Seitenleitwerk.  `mirror`
   heisst, dass es sie auch spiegelbildlich links gibt -- YASim schreibt nur
   eine Haelfte hin. */
struct blade_surface {
    float x, y, z;              /* Wurzel, im Rumpfsystem */
    float chord;                /* Tiefe an der Wurzel */
    float length;               /* Laenge einer Haelfte, laengs der Flaeche */
    float taper;                /* Tiefe aussen geteilt durch innen */
    float incidence_deg;        /* Einstellwinkel an der Wurzel */
    float twist_deg;            /* Schraenkung: aussen minus innen */
    float dihedral_deg;         /* V-Stellung, positiv nach oben */
    float sweep_deg;            /* Pfeilung nach hinten */
    float camber;               /* Woelbung: hebt die Auftriebskurve */
    float stall_aoa_deg;        /* wo die Stroemung abreisst */
    float stall_width_deg;      /* wie weich der Abriss ist */
    float stall_peak;           /* Ueberhoehung kurz vor dem Abriss */
    float flap, aileron, elevator, rudder;  /* Anteil der Tiefe als Ruder */
    int mirror;                 /* 1 = es gibt sie auch links */
    int vertical;               /* 1 = steht senkrecht (Seitenleitwerk) */
};

/* Ein Fahrwerksbein als Kontaktpunkt: Feder und Daempfer, wie bei YASim. */
struct blade_gear {
    float x, y, z;              /* wo es sitzt, ausgefahren */
    float spring_n_m;           /* Federrate */
    float damp_ns_m;            /* Daempfung */
    float steer;                /* 1 = lenkbar (Bugrad) */
    float brake;                /* 1 = gebremst */
};

struct blade_aircraft {
    char name[32];
    float mass_kg;
    float ixx, iyy, izz;        /* Traegheitsmomente um die Rumpfachsen */
    float thrust_max_n;
    float rpm_idle, rpm_max;
    int jet;
    float power_w;              /* Wellenleistung, falls Propeller */
    float prop_r;               /* Propellerhalbmesser, m */
    float reise_ms;            /* Reisegeschwindigkeit, zum Trimmen */
    float cd_body;              /* Rumpfwiderstand, auf die Fluegelflaeche bezogen */
    float wing_area_m2;         /* nur fuer Anzeige und Rumpfwiderstand */
    float wing_span_m;
    int nsurf, ngear;
    struct blade_surface surf[BLADE_MAX_SURF];
    struct blade_gear gear[BLADE_MAX_GEAR];
};

/* Der Zustand: Lage, Drehraten, Geschwindigkeit im Rumpfsystem. */
struct blade_state {
    float u, v, w;              /* Geschwindigkeit im Rumpfsystem, m/s */
    float p, q, r;              /* Drehraten um x, y, z, rad/s */
    float roll_deg, pitch_deg, heading_deg;
    float alt_m, ground_m;
    float east_m, north_m;
    float rpm;
    int engine_on, on_ground;
    float alpha_deg, beta_deg;  /* Anstell- und Schiebewinkel, fuer die Anzeige */
    float vs_ms, v_ms;          /* Steigen und Fahrt, fuer die Instrumente */
    float load_g;               /* Lastvielfaches, fuer das Blickfeld */
    /* Nur zum Nachsehen: was das Fahrwerk im letzten Schritt getan hat. */
    float gear_pen, gear_force;
    float pen_je[BLADE_MAX_GEAR];
    int gear_touch;
};

int  blade_load(struct blade_aircraft *a, const char *path);   /* 1 = gelesen */
void blade_default(struct blade_aircraft *a);                  /* eingebaute C172 */
void blade_init(struct blade_state *s, const struct blade_aircraft *a);
/* Setzt das Flugzeug so hin, dass die Raeder den Boden beruehren.  `alt_m`
   ist die Hoehe des Schwerpunkts, nicht die der Raeder -- wer das
   verwechselt, startet mit dem Fahrwerk im Boden, und die Feder schiesst
   das Flugzeug in die Luft. */
void blade_place_on_ground(struct blade_state *s, const struct blade_aircraft *a);
void blade_step(struct blade_state *s, const struct blade_aircraft *a, float dt,
                float stick_roll, float stick_pitch, float rudder, float throttle,
                float flaps, int brake, int gear_down);

/* Luftdichte nach der Normatmosphaere -- die feste 1,225 war der groebste
   Fehler im alten Modell: In Reiseflughoehe trug der Fluegel wie am Boden. */
float blade_density(float alt_m);

#endif
