#include "fdm.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define RHO 1.225f          /* Luftdichte auf Meereshoehe */
#define G   9.80665f

/* Eingebaute Cessna 172 - dieselben Zahlen, die acftconv.py aus c172p.xml
   holt, damit das Programm auch ohne Flugzeugdatei etwas Vernuenftiges tut. */
static void set_table(struct fdm_table *t, const float *pairs, int n) {
    t->n = n > FDM_TABLE_MAX ? FDM_TABLE_MAX : n;
    for (int i = 0; i < t->n; ++i) {
        t->x[i] = pairs[2 * i];
        t->y[i] = pairs[2 * i + 1];
    }
}

void fdm_default(struct fdm_aircraft *a) {
    static const float cl[] = {-5.16f, -0.22f, 0.0f, 0.25f, 5.16f, 0.73f, 8.02f, 1.02f,
                               12.03f, 1.25f, 14.90f, 1.44f, 16.04f, 1.47f, 17.19f, 1.43f,
                               20.63f, 1.15f, 30.0f, 1.47f};
    static const float cd[] = {-5.0f, 0.0041f, 0.0f, 0.0052f, 5.0f, 0.0442f, 8.0f, 0.0860f,
                               12.0f, 0.1298f, 14.0f, 0.1565f, 20.0f, 0.2537f, 30.0f, 0.45f};
    static const float clf[] = {0.0f, 0.0f, 10.0f, 0.20f, 20.0f, 0.30f, 30.0f, 0.35f};
    static const float cdf[] = {0.0f, 0.0f, 10.0f, 0.007f, 20.0f, 0.012f, 30.0f, 0.018f};

    memset(a, 0, sizeof(*a));
    snprintf(a->name, sizeof(a->name), "c172 (eingebaut)");
    a->mass_kg = 747.0f;
    a->wing_area_m2 = 16.165f;
    a->wing_span_m = 10.912f;
    a->thrust_max_n = 2400.0f;
    a->rpm_idle = 700.0f;
    a->rpm_max = 2700.0f;
    a->cd0 = 0.027f;
    a->cd_gear = 0.0f;          /* die c172 hat ihr Fahrwerk immer draussen */
    set_table(&a->cl_alpha, cl, sizeof(cl) / (2 * sizeof(float)));
    set_table(&a->cd_alpha, cd, sizeof(cd) / (2 * sizeof(float)));
    set_table(&a->cl_flap, clf, 4);
    set_table(&a->cd_flap, cdf, 4);
}

static int read_table_line(struct fdm_table *t, const char *rest) {
    int n = 0;
    int used = 0;
    if (sscanf(rest, "%d%n", &n, &used) != 1) return 0;
    rest += used;
    if (n > FDM_TABLE_MAX) n = FDM_TABLE_MAX;
    for (int i = 0; i < n; ++i) {
        float x, y;
        if (sscanf(rest, "%f %f%n", &x, &y, &used) != 2) { t->n = i; return i > 0; }
        t->x[i] = x;
        t->y[i] = y;
        rest += used;
    }
    t->n = n;
    return 1;
}

/* Das Dateiformat ist absichtlich Text: ein Schluessel je Zeile, Tabellen als
   Anzahl und dann Paare.  Ein Flugzeug sind rund 1000 Bytes. */
int fdm_load(struct fdm_aircraft *a, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fdm_default(a);
    snprintf(a->name, sizeof(a->name), "%s", "(unbenannt)");
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[32];
        int used = 0;
        if (sscanf(line, "%31s%n", key, &used) != 1) continue;
        const char *rest = line + used;
        if (!strcmp(key, "name")) sscanf(rest, " %31[^\n]", a->name);
        else if (!strcmp(key, "mass_kg")) a->mass_kg = (float)atof(rest);
        else if (!strcmp(key, "wing_area_m2")) a->wing_area_m2 = (float)atof(rest);
        else if (!strcmp(key, "wing_span_m")) a->wing_span_m = (float)atof(rest);
        else if (!strcmp(key, "thrust_max_n")) a->thrust_max_n = (float)atof(rest);
        else if (!strcmp(key, "rpm_idle")) a->rpm_idle = (float)atof(rest);
        else if (!strcmp(key, "rpm_max")) a->rpm_max = (float)atof(rest);
        else if (!strcmp(key, "cd0")) a->cd0 = (float)atof(rest);
        else if (!strcmp(key, "cd_gear")) a->cd_gear = (float)atof(rest);
        else if (!strcmp(key, "cl_alpha")) read_table_line(&a->cl_alpha, rest);
        else if (!strcmp(key, "cd_alpha")) read_table_line(&a->cd_alpha, rest);
        else if (!strcmp(key, "cl_flap")) read_table_line(&a->cl_flap, rest);
        else if (!strcmp(key, "cd_flap")) read_table_line(&a->cd_flap, rest);
    }
    fclose(f);
    return 1;
}

float fdm_lookup(const struct fdm_table *t, float x) {
    if (t->n <= 0) return 0.0f;
    if (x <= t->x[0]) return t->y[0];
    if (x >= t->x[t->n - 1]) return t->y[t->n - 1];
    for (int i = 1; i < t->n; ++i) {
        if (x <= t->x[i]) {
            float span = t->x[i] - t->x[i - 1];
            float f = span > 0.0f ? (x - t->x[i - 1]) / span : 0.0f;
            return t->y[i - 1] + f * (t->y[i] - t->y[i - 1]);
        }
    }
    return t->y[t->n - 1];
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void fdm_init(struct fdm_state *s, const struct fdm_aircraft *a) {
    (void)a;
    s->alt_m = 0.0f;
    s->ground_m = 0.0f;
    s->v_ms = 0.0f;
    s->gamma_deg = 0.0f;
    s->pitch_deg = 0.0f;
    s->roll_deg = 0.0f;
    s->heading_deg = 0.0f;
    s->alpha_deg = 0.0f;
    s->rpm = 0.0f;
    s->vs_ms = 0.0f;
    s->north_m = 0.0f;
    s->east_m = 0.0f;
    s->on_ground = 1;
}

void fdm_step(struct fdm_state *s, const struct fdm_aircraft *a, float dt,
              float stick_roll, float stick_pitch, float rudder, float throttle,
              float flaps, int brake, int gear_down) {
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;               /* ein Hacker im Bildtakt soll nicht sprengen */

    /* Drehzahl folgt dem Hebel traege - steht der Motor, faellt sie auf null
       und der Hebel bewirkt nichts. */
    if (!s->engine_on) throttle = 0.0f;
    float rpm_want = s->engine_on ? a->rpm_idle + throttle * (a->rpm_max - a->rpm_idle)
                                  : 0.0f;
    s->rpm += (rpm_want - s->rpm) * clampf(dt * 2.0f, 0.0f, 1.0f);

    /* Steuerung: der Knueppel gibt Raten, die Ruder wirken erst mit Fahrt.
     * Unter Rollgeschwindigkeit haengt das Ruder schlaff - deshalb der Faktor. */
    float authority = clampf(s->v_ms / 25.0f, 0.0f, 1.0f);
    s->roll_deg += stick_roll * 70.0f * authority * dt;
    s->pitch_deg += stick_pitch * 25.0f * authority * dt;
    s->roll_deg = clampf(s->roll_deg, -75.0f, 75.0f);
    s->pitch_deg = clampf(s->pitch_deg, -30.0f, 25.0f);
    if (s->on_ground) {
        s->roll_deg *= 1.0f - clampf(dt * 4.0f, 0.0f, 1.0f);     /* Fahrwerk haelt gerade */
        if (s->pitch_deg < 0.0f) s->pitch_deg = 0.0f;
    }

    /* Auftrieb und Widerstand, beide aus den Kurven des Flugzeugs.  Der
       Abriss steht in der Tabelle selbst - ueber etwa 16 Grad faellt sie. */
    float alpha = s->pitch_deg - s->gamma_deg;
    s->alpha_deg = alpha;
    float flap_deg = flaps * 30.0f;
    float cl = fdm_lookup(&a->cl_alpha, alpha) + fdm_lookup(&a->cl_flap, flap_deg);
    float cd = a->cd0 + fdm_lookup(&a->cd_alpha, alpha) + fdm_lookup(&a->cd_flap, flap_deg);
    if (gear_down) cd += a->cd_gear;
    float q = 0.5f * RHO * s->v_ms * s->v_ms;
    float lift = q * a->wing_area_m2 * cl;
    float drag = q * a->wing_area_m2 * cd;
    float thrust = throttle * a->thrust_max_n * (1.0f - clampf(s->v_ms / 90.0f, 0.0f, 0.7f));

    float gamma = s->gamma_deg * (float)M_PI / 180.0f;
    float roll = s->roll_deg * (float)M_PI / 180.0f;
    float weight = a->mass_kg * G;

    /* Laengs: Schub gegen Widerstand und Hangabtrieb */
    float accel = (thrust - drag - weight * sinf(gamma)) / a->mass_kg;
    if (s->on_ground) {
        float roll_friction = brake ? 0.35f : 0.02f;
        accel -= roll_friction * G * (s->v_ms > 0.1f ? 1.0f : 0.0f);
    }
    s->v_ms += accel * dt;
    if (s->v_ms < 0.0f) s->v_ms = 0.0f;

    /* Quer: der senkrechte Anteil des Auftriebs gegen das Gewicht kruemmt die
     * Bahn, der waagrechte dreht den Kurs. */
    if (s->on_ground) {
        if (lift > weight && s->v_ms > 5.0f) {
            s->on_ground = 0;                            /* abgehoben */
        } else {
            s->gamma_deg = 0.0f;
            s->vs_ms = 0.0f;
            s->alt_m = s->ground_m;
    s->ground_m = 0.0f;
        }
        if (s->v_ms > 0.5f)          /* das Bugrad lenkt, und zwar mit dem Ruder */
            s->heading_deg += rudder * 12.0f * dt * (s->v_ms < 15.0f ? 1.0f : 15.0f / s->v_ms);
    }
    if (!s->on_ground && s->v_ms > 1.0f) {
        float dgamma = (lift * cosf(roll) - weight * cosf(gamma)) / (a->mass_kg * s->v_ms);
        s->gamma_deg += dgamma * 180.0f / (float)M_PI * dt;
        s->gamma_deg = clampf(s->gamma_deg, -30.0f, 30.0f);
        s->heading_deg += (G * tanf(roll) / s->v_ms) * 180.0f / (float)M_PI * dt;
        /* Das Seitenruder giert; viel ist es nicht, aber es haelt die Nase. */
        s->heading_deg += rudder * 12.0f * authority * dt;
        s->vs_ms = s->v_ms * sinf(s->gamma_deg * (float)M_PI / 180.0f);
        s->alt_m += s->vs_ms * dt;
        if (s->alt_m <= s->ground_m) {                   /* aufgesetzt */
            s->alt_m = s->ground_m;
    s->ground_m = 0.0f;
            s->vs_ms = 0.0f;
            s->gamma_deg = 0.0f;
            s->on_ground = 1;
        }
    }
    /* Ort mitfuehren, damit die Kachel unter einem wegzieht */
    float hdg = s->heading_deg * (float)M_PI / 180.0f;
    float ground_speed = s->v_ms * cosf(s->gamma_deg * (float)M_PI / 180.0f);
    s->north_m += ground_speed * cosf(hdg) * dt;
    s->east_m += ground_speed * sinf(hdg) * dt;

    while (s->heading_deg < 0.0f) s->heading_deg += 360.0f;
    while (s->heading_deg >= 360.0f) s->heading_deg -= 360.0f;
}
