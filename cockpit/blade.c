/* Siehe blade.h: das Flugzeug als Geometrie, Kraefte je Flaechenstueck. */
#include "blade.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define G 9.80665f
#define DEG (float)(M_PI / 180.0)
#define RAD (float)(180.0 / M_PI)

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Normatmosphaere bis zur Tropopause, darueber gleiche Temperatur.  Mehr
   braucht ein Flugzeug nicht, das auf 11 km nichts zu suchen hat. */
float blade_density(float alt_m) {
    if (alt_m < 0.0f) alt_m = 0.0f;
    if (alt_m > 11000.0f) {
        /* oberhalb: Dichte faellt weiter, aber ohne Temperatursturz */
        float rho11 = 0.36392f;
        return rho11 * expf(-(alt_m - 11000.0f) / 6341.6f);
    }
    float t = 288.15f - 0.0065f * alt_m;
    return 1.225f * powf(t / 288.15f, 4.2559f);
}

/* Auftrieb und Widerstand eines Flaechenstuecks.
 *
 * Bis zum Abriss ist es die Traglinientheorie: der Anstieg je Radiant folgt
 * aus der Streckung, die Woelbung verschiebt die Kurve.  Danach wird
 * weichgeblendet auf die Platte -- `2 sin a cos a` --, denn eine
 * abgerissene Flaeche traegt wie ein Brett, nicht wie ein Profil.  Der
 * Uebergang ist keine Schoenheit, sondern der Grund, warum sich ein
 * ueberzogener Flieger anders anfuehlt als ein langsamer.
 */
static void airfoil(float alpha, float aspect, float camber,
                    float stall_aoa, float stall_width, float stall_peak,
                    float *cl, float *cd) {
    float slope = 2.0f * (float)M_PI * aspect / (aspect + 2.0f);   /* je Radiant */
    float lin = slope * (alpha + camber);
    float plate = 2.0f * sinf(alpha) * cosf(alpha);

    float a = fabsf(alpha);
    float blend = 0.0f;
    if (stall_width < 0.5f * DEG) stall_width = 0.5f * DEG;
    if (a > stall_aoa) blend = clampf((a - stall_aoa) / stall_width, 0.0f, 1.0f);
    /* Kurz vor dem Abriss traegt das Profil am meisten -- YASim nennt das
       `peak`.  Danach faellt es auf die Platte. */
    float peak = 1.0f + stall_peak * blend * (1.0f - blend) * 4.0f;
    *cl = lin * (1.0f - blend) * peak + plate * blend;

    /* Widerstand: Profil, induziert, und was die Platte quer dazu bremst. */
    float cdi = (*cl) * (*cl) / ((float)M_PI * aspect * 0.85f);
    float cd_plate = 2.0f * sinf(alpha) * sinf(alpha);
    *cd = 0.008f + cdi * (1.0f - blend) + cd_plate * blend;
}

void blade_place_on_ground(struct blade_state *s, const struct blade_aircraft *a) {
    float tief = 0.0f;
    for (int i = 0; i < a->ngear; ++i)
        if (a->gear[i].z > tief) tief = a->gear[i].z;
    /* Vier Zentimeter Einfederung: so viel traegt das Gewicht, und so
       steht es ruhig da, statt beim ersten Bild zu huepfen. */
    s->alt_m = s->ground_m + tief - 0.04f;
    s->on_ground = 1;
    s->u = s->v = s->w = 0.0f;
    s->p = s->q = s->r = 0.0f;
}

void blade_init(struct blade_state *s, const struct blade_aircraft *a) {
    memset(s, 0, sizeof(*s));
    s->engine_on = 0;
    blade_place_on_ground(s, a);
}

void blade_default(struct blade_aircraft *a) {
    memset(a, 0, sizeof(*a));
    snprintf(a->name, sizeof(a->name), "c172");
    a->mass_kg = 1043.0f;
    a->wing_area_m2 = 16.2f;
    a->wing_span_m = 11.0f;
    a->thrust_max_n = 2400.0f;
    a->rpm_idle = 700.0f;
    a->rpm_max = 2700.0f;
    a->cd_body = 0.035f;
    /* Traegheit aus Spannweite und Laenge geschaetzt, siehe unten. */
    a->ixx = a->mass_kg * 0.20f * 0.20f * a->wing_span_m * a->wing_span_m;
    a->iyy = a->mass_kg * 0.30f * 0.30f * 8.2f * 8.2f;
    a->izz = a->ixx + a->iyy;

    struct blade_surface *w = &a->surf[a->nsurf++];
    w->x = -0.3f; w->y = 0.6f; w->z = -1.0f;
    w->chord = 1.5f; w->length = 5.0f; w->taper = 1.0f;
    w->incidence_deg = 1.5f; w->twist_deg = -1.5f; w->dihedral_deg = 1.7f;
    w->camber = 0.06f;
    w->stall_aoa_deg = 16.0f; w->stall_width_deg = 4.0f; w->stall_peak = 1.6f;
    w->flap = 0.25f; w->aileron = 0.20f; w->mirror = 1;

    struct blade_surface *h = &a->surf[a->nsurf++];
    h->x = -4.6f; h->y = 0.2f; h->z = -0.6f;
    h->chord = 1.0f; h->length = 1.7f; h->taper = 0.9f;
    h->incidence_deg = -1.0f; h->camber = 0.0f;
    h->stall_aoa_deg = 18.0f; h->stall_width_deg = 6.0f; h->stall_peak = 1.2f;
    h->elevator = 0.45f; h->mirror = 1;

    struct blade_surface *v = &a->surf[a->nsurf++];
    v->x = -4.8f; v->y = 0.0f; v->z = -0.4f;
    v->chord = 1.3f; v->length = 1.4f; v->taper = 0.7f;
    v->stall_aoa_deg = 20.0f; v->stall_width_deg = 6.0f; v->stall_peak = 1.2f;
    v->rudder = 0.45f; v->vertical = 1;

    struct blade_gear *g;
    g = &a->gear[a->ngear++];
    g->x = 0.9f; g->y = 0.0f; g->z = 1.35f;
    g->spring_n_m = 60000.0f; g->damp_ns_m = 4000.0f; g->steer = 1.0f;
    g = &a->gear[a->ngear++];
    g->x = -0.5f; g->y = 1.4f; g->z = 1.25f;
    g->spring_n_m = 90000.0f; g->damp_ns_m = 6000.0f; g->brake = 1.0f;
    g = &a->gear[a->ngear++];
    g->x = -0.5f; g->y = -1.4f; g->z = 1.25f;
    g->spring_n_m = 90000.0f; g->damp_ns_m = 6000.0f; g->brake = 1.0f;
}

/* ------------------------------------------------------------------ */

struct kraft { float fx, fy, fz, mx, my, mz; };

static void add_force(struct kraft *k, float fx, float fy, float fz,
                      float rx, float ry, float rz) {
    k->fx += fx; k->fy += fy; k->fz += fz;
    /* Moment ist Hebelarm mal Kraft, im Rumpfsystem */
    k->mx += ry * fz - rz * fy;
    k->my += rz * fx - rx * fz;
    k->mz += rx * fy - ry * fx;
}

/* Wie stark ein Ruder wirkt: die Wurzel aus seinem Tiefenanteil ist die
   uebliche Naeherung -- ein Ruder ueber einem Viertel der Tiefe wirkt etwa
   halb so stark wie ein Anstellwinkel derselben Groesse.  Das Vorzeichen
   bleibt erhalten. */
static float wirk(float anteil) {
    if (anteil == 0.0f) return 0.0f;
    return anteil > 0.0f ? sqrtf(anteil) : -sqrtf(-anteil);
}

/* Ein Stueck einer Flaeche.  `side` ist +1 rechts, -1 links; `frac` liegt
   zwischen 0 (Wurzel) und 1 (Spitze). */
static void segment(struct kraft *k, const struct blade_surface *sf, int side,
                    float frac, float dfrac, float rho,
                    float u, float v, float w, float p, float q, float r,
                    float ail, float elev, float rud, float flap) {
    float chord = sf->chord * (1.0f + (sf->taper - 1.0f) * frac);
    float area = chord * sf->length * dfrac;
    if (area <= 0.0f) return;

    /* Wo das Stueck sitzt.  Die Flaeche laeuft von der Wurzel nach aussen,
       mit Pfeilung nach hinten und V-Stellung nach oben. */
    float span = sf->length * frac;
    float dih = sf->dihedral_deg * DEG, swp = sf->sweep_deg * DEG;
    /* Der Auftrieb greift am **Viertelpunkt** an, nicht an der Vorderkante.
       Das ist keine Feinheit: Bei der AG-14 sind das 40 cm, und sie
       entscheiden darueber, ob der Fluegel vor oder hinter dem Schwerpunkt
       zieht.  Der Umsetzer rechnete den Neutralpunkt richtig mit t/4, das
       Modell zog aber an der Nase -- beide waren sich uneinig, und das
       Flugzeug taumelte, obwohl die Rechnung "stabil" sagte. */
    float viertel = 0.25f * chord;
    float rx, ry, rz;
    if (sf->vertical) {
        rx = sf->x - viertel - span * sinf(swp);
        ry = sf->y;
        rz = sf->z - span * cosf(swp);        /* z zeigt nach unten */
    } else {
        rx = sf->x - viertel - span * sinf(swp);
        ry = sf->y * side + side * span * cosf(dih);
        rz = sf->z - span * sinf(dih);
    }

    /* Die Anstroemung an *dieser* Stelle: Fluggeschwindigkeit plus Drehung.
       Genau hier steckt der Unterschied zum Tabellenmodell -- der
       aufsteigende Fluegel sieht weniger Anstellwinkel und daempft das
       Rollen von selbst. */
    float lu = u + (q * rz - r * ry);
    float lv = v + (r * rx - p * rz);
    float lw = w + (p * ry - q * rx);

    /* Ruderausschlag aendert die Woelbung des Stuecks.  Ein Ruder, das ein
       Viertel der Tiefe einnimmt, wirkt etwa wie ein halb so grosser
       Anstellwinkel -- die Wurzel aus dem Tiefenanteil ist die uebliche
       Naeherung. */
    /* Die Anteile duerfen negativ sein: dann schlaegt das Ruder andersherum
       aus.  YASim schreibt das als `invert`, und bei einem Enten-Flugzeug
       ist es keine Feinheit, sondern der Unterschied zwischen Hochziehen
       und Druecken. */
    float defl = 0.0f;
    defl += flap * 30.0f * DEG * wirk(sf->flap);
    defl -= ail * side * 20.0f * DEG * wirk(sf->aileron);
    defl -= elev * 25.0f * DEG * wirk(sf->elevator);
    defl += rud * 25.0f * DEG * wirk(sf->rudder);

    float inc = (sf->incidence_deg + sf->twist_deg * frac) * DEG;
    float aspect = sf->length * 2.0f / (sf->chord * (1.0f + sf->taper) * 0.5f);
    if (aspect < 1.0f) aspect = 1.0f;

    float alpha, vmag2, lift_x, lift_y, lift_z, drag_x, drag_y, drag_z;
    if (sf->vertical) {
        /* Das Seitenleitwerk traegt zur Seite: sein Anstellwinkel ist der
           Schiebewinkel. */
        vmag2 = lu * lu + lv * lv;
        if (vmag2 < 0.01f) return;
        alpha = atan2f(lv, lu) + defl;
        float vm = sqrtf(vmag2);
        drag_x = -lu / vm; drag_y = -lv / vm; drag_z = 0.0f;
        /* Seitlicher Auftrieb: schiebt das Flugzeug nach rechts, traegt das
           Leitwerk nach links -- (lv, -lu) ist die Senkrechte dazu. */
        lift_x = lv / vm; lift_y = -lu / vm; lift_z = 0.0f;
    } else {
        vmag2 = lu * lu + lw * lw;
        if (vmag2 < 0.01f) return;
        alpha = atan2f(lw, lu) + inc + defl;
        float vm = sqrtf(vmag2);
        drag_x = -lu / vm; drag_y = 0.0f; drag_z = -lw / vm;
        /* Auftrieb steht senkrecht auf der Anstroemung, nach oben.  z zeigt
           nach unten, also ist (lw, -lu)/|v| die richtige Senkrechte: im
           Geradeausflug (0, -1), und das ist oben. */
        lift_x = lw / vm; lift_y = 0.0f; lift_z = -lu / vm;
    }

    float cl, cd;
    airfoil(alpha, aspect, sf->camber,
            sf->stall_aoa_deg * DEG, sf->stall_width_deg * DEG, sf->stall_peak,
            &cl, &cd);

    float qbar = 0.5f * rho * vmag2;
    float L = qbar * area * cl, D = qbar * area * cd;
    add_force(k, L * lift_x + D * drag_x,
                 L * lift_y + D * drag_y,
                 L * lift_z + D * drag_z, rx, ry, rz);
}

/* Alle Flaechen, Stueck fuer Stueck.  Steht fuer sich, weil das Trimmen
   dieselbe Rechnung braucht -- ein Trimmpunkt, der mit einer *anderen*
   Formel gesucht wird als der, mit der spaeter geflogen wird, ist keiner. */
static void flaechenkraefte(struct kraft *k, const struct blade_aircraft *a,
                            float rho, float u, float v, float w,
                            float p, float q, float r,
                            float ail, float elev, float rud, float flap) {
    for (int i = 0; i < a->nsurf; ++i) {
        const struct blade_surface *sf = &a->surf[i];
        int sides = sf->mirror ? 2 : 1;
        for (int sd = 0; sd < sides; ++sd) {
            int side = sd ? -1 : 1;
            if (sf->vertical) side = 1;
            for (int seg = 0; seg < BLADE_MAX_SEG; ++seg) {
                float frac = (seg + 0.5f) / BLADE_MAX_SEG;
                segment(k, sf, side, frac, 1.0f / BLADE_MAX_SEG, rho,
                        u, v, w, p, q, r, ail, elev, rud, flap);
            }
        }
    }
}

/* Das Flugzeug sich selbst trimmen lassen.
 *
 * Gesucht sind zwei Zahlen: der Anstellwinkel, bei dem der Auftrieb das
 * Gewicht traegt, und der Einstellwinkel des Leitwerks, bei dem sich die
 * Momente aufheben.  Beide haengen voneinander ab, also abwechselnd
 * nachziehen, bis sich nichts mehr aendert.
 *
 * Warum nicht beim Umsetzen, in Python?  Weil dort mit einer vereinfachten
 * Formel gerechnet wuerde -- ohne Schraenkung, ohne den Widerstand der
 * Flaechen ueber und unter dem Schwerpunkt.  Der Trimmpunkt kam dann auf
 * dem Papier heraus und im Flug nicht: die AG-14 hatte ueberall ein
 * nasenlastiges Moment und stuerzte, obwohl die Rechnung "stabil" sagte.
 */
static void blade_trim(struct blade_aircraft *a, float v_ms) {
    if (v_ms < 5.0f) v_ms = 45.0f;
    /* Welche Flaeche trimmt?  Die mit dem Hoehenruder -- das ist die Frage,
       die die Datei selbst beantwortet.  Nach Groesse zu gehen ging schief:
       Der Long-EZ hat drei waagrechte Flaechen, die groesste ist seine
       Strake, und der Trimmer verstellte daraufhin den Hauptfluegel. */
    int getrimmt = 0;
    float x_mittel = 0.0f;
    for (int i = 0; i < a->nsurf; ++i)
        if (!a->surf[i].vertical && a->surf[i].elevator != 0.0f) {
            ++getrimmt;
            x_mittel += a->surf[i].x;
        }
    if (!getrimmt) return;
    x_mittel /= getrimmt;
    /* Und in welche Richtung wirkt ein groesserer Einstellwinkel?  Hinter
       dem Schwerpunkt drueckt er die Nase, davor hebt er sie.  Ohne dieses
       Vorzeichen lief der Trimmer beim Long-EZ in die Begrenzung: Er nahm
       dem Vorfluegel genau den Auftrieb weg, den er zum Anheben gebraucht
       haette. */
    float richtung = x_mittel > 0.0f ? -1.0f : 1.0f;

    float rho = blade_density(0.0f);
    float gewicht = a->mass_kg * G;
    float alpha = 2.0f * DEG, i_leit = 0.0f;
    for (int runde = 0; runde < 60; ++runde) {
        float u = v_ms * cosf(alpha), w = v_ms * sinf(alpha);
        struct kraft k = {0, 0, 0, 0, 0, 0};
        flaechenkraefte(&k, a, rho, u, 0, w, 0, 0, 0, 0, 0, 0, 0);
        /* Auftrieb ist die Kraft nach oben, also gegen z. */
        float auftrieb = -(k.fz * cosf(alpha) - k.fx * sinf(alpha));
        float fehl_auftrieb = (auftrieb - gewicht) / gewicht;
        float fehl_moment = k.my / (gewicht * 2.0f);
        alpha -= fehl_auftrieb * 0.08f;
        i_leit += richtung * fehl_moment * 0.05f;
        alpha = clampf(alpha, -10.0f * DEG, 14.0f * DEG);
        i_leit = clampf(i_leit, -15.0f * DEG, 15.0f * DEG);
        for (int i = 0; i < a->nsurf; ++i)
            if (!a->surf[i].vertical && a->surf[i].elevator != 0.0f)
                a->surf[i].incidence_deg = i_leit * RAD;
        if (fabsf(fehl_auftrieb) < 0.002f && fabsf(fehl_moment) < 0.002f) break;
    }
}

/* Ein Schritt des Modells.  Nach aussen sichtbar ist blade_step darunter:
   Es zerlegt den Bildabstand in kleine Schritte. */
static void blade_step_one(struct blade_state *s, const struct blade_aircraft *a,
                           float dt, float stick_roll, float stick_pitch,
                           float rudder, float throttle, float flaps,
                           int brake, int gear_down) {
    if (dt <= 0.0f) return;

    if (!s->engine_on) throttle = 0.0f;
    float rpm_want = s->engine_on ? a->rpm_idle + throttle * (a->rpm_max - a->rpm_idle) : 0.0f;
    s->rpm += (rpm_want - s->rpm) * clampf(dt * 2.0f, 0.0f, 1.0f);

    float rho = blade_density(s->alt_m);
    struct kraft k = {0, 0, 0, 0, 0, 0};
    flaechenkraefte(&k, a, rho, s->u, s->v, s->w, s->p, s->q, s->r,
                    stick_roll, stick_pitch, rudder, flaps);

    /* Rumpf: nur Widerstand, aber der bremst die Schraeganstroemung mit. */
    {
        float vmag2 = s->u * s->u + s->v * s->v + s->w * s->w;
        if (vmag2 > 0.01f) {
            float vm = sqrtf(vmag2);
            float qbar = 0.5f * rho * vmag2;
            float D = qbar * a->wing_area_m2 * a->cd_body;
            float extra = gear_down ? 1.4f : 1.0f;
            D *= extra;
            add_force(&k, -D * s->u / vm, -D * s->v / vm, -D * s->w / vm, 0, 0, 0);
        }
    }

    /* Schub laengs, am Bug angreifend.
     *
     * Ein Propeller setzt Leistung um, nicht Kraft: `T = P / v`.  Im Stand
     * waere das unendlich, also begrenzt der Standschub aus der
     * Momententheorie, `T = (2 rho A P^2)^(1/3)`.  Das ist der Grund,
     * warum ein 115-PS-Flugzeug beim Anrollen mehr als das Dreifache
     * dessen zieht, was es bei Reisegeschwindigkeit zieht -- mit dem alten
     * linearen Abfall dauerte der Startlauf 38 Sekunden statt zehn.
     *
     * Eine Turbine liefert dagegen naeherungsweise festen Schub. */
    {
        float thrust;
        if (a->jet || a->power_w <= 0.0f) {
            thrust = throttle * a->thrust_max_n;
            if (!a->jet) thrust *= 1.0f - clampf(s->u / 90.0f, 0.0f, 0.7f);
        } else {
            float P = throttle * a->power_w;
            float rad = a->prop_r > 0.1f ? a->prop_r : 0.9f;
            float flaeche = (float)M_PI * rad * rad;
            float stand = cbrtf(2.0f * rho * flaeche * P * P);
            float schnell = P * 0.82f / (s->u > 3.0f ? s->u : 3.0f);
            thrust = schnell < stand ? schnell : stand;
        }
        add_force(&k, thrust, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f);
    }

    /* Schwerkraft: im Rumpfsystem haengt sie an der Lage. */
    float phi = s->roll_deg * DEG, theta = s->pitch_deg * DEG;
    float sp = sinf(phi), cp = cosf(phi), st = sinf(theta), ct = cosf(theta);
    float gx = -G * st, gy = G * ct * sp, gz = G * ct * cp;
    k.fx += a->mass_kg * gx;
    k.fy += a->mass_kg * gy;
    k.fz += a->mass_kg * gz;

    /* Wie schnell der Schwerpunkt gerade sinkt -- das braucht der Daempfer
       des Fahrwerks, und zwar in der Welt und nicht im Rumpfsystem. */
    float vd_now = -st * s->u + sp * ct * s->v + cp * ct * s->w;

    /* Fahrwerk als Kontaktpunkte: Feder gegen die Einfederung, Daempfer
       gegen die Geschwindigkeit, dazu Reibung laengs und quer.  Ohne die
       quere Reibung dreht das Flugzeug am Boden wie auf Eis. */
    int touching = 0;
    for (int i = 0; i < a->ngear && gear_down; ++i) {
        const struct blade_gear *g = &a->gear[i];
        /* Wo der Punkt in der Welt steht: nur die Hoehe zaehlt. */
        float wz = s->alt_m - (-st * g->x + ct * sp * g->y + ct * cp * g->z);
        float pen = s->ground_m - wz;
        if (pen <= 0.0f) continue;
        if (pen > 0.5f) pen = 0.5f;            /* durchgeschlagen ist durchgeschlagen */
        touching = 1;
        /* Feder gegen die Einfederung, Daempfer gegen das Sinken.  Der
           Daempfer wirkt nur beim Einfedern, sonst zoege er das Flugzeug
           beim Ausfedern an den Boden. */
        float dmp = vd_now > 0.0f ? g->damp_ns_m * vd_now : 0.0f;
        /* Der Daempfer darf die Sinkgeschwindigkeit in einem Zeitschritt
           hoechstens zur Haelfte wegnehmen.  Ohne diese Grenze schaukelt
           sich ein Aufsetzer auf: Die Daempfung eines Verkehrsflugzeugs
           liegt bei 50 kN je m/s, und bei 38 Millisekunden Bildabstand
           kehrt sie die Geschwindigkeit um, statt sie zu bremsen -- das
           Flugzeug wurde dabei bis an die Hoehengrenze geschossen. */
        float grenze = 0.5f * a->mass_kg * vd_now / dt;
        if (dmp > grenze) dmp = grenze;
        float fz = -(g->spring_n_m * pen + dmp);
        if (fz > 0.0f) fz = 0.0f;              /* der Boden zieht nicht */
        float mu_roll = (brake && g->brake > 0.0f) ? 0.4f : 0.02f;
        float mu_side = 0.6f;
        float fx = -mu_roll * (-fz) * (s->u > 0.1f ? 1.0f : (s->u < -0.1f ? -1.0f : 0.0f));
        float fy = -mu_side * (-fz) * clampf(s->v * 0.5f, -1.0f, 1.0f);
        if (g->steer > 0.0f) fy += rudder * (-fz) * 0.35f;
        add_force(&k, fx, fy, fz, g->x, g->y, g->z);
    }
    s->on_ground = touching;

    /* Beschleunigung im Rumpfsystem, mit den Scheinkraeften der Drehung. */
    float ax = k.fx / a->mass_kg - (s->q * s->w - s->r * s->v);
    float ay = k.fy / a->mass_kg - (s->r * s->u - s->p * s->w);
    float az = k.fz / a->mass_kg - (s->p * s->v - s->q * s->u);
    s->u += ax * dt; s->v += ay * dt; s->w += az * dt;

    /* Drehbeschleunigung: Moment minus Kreiselglied, durch Traegheit. */
    float dp = (k.mx - (a->izz - a->iyy) * s->q * s->r) / a->ixx;
    float dq = (k.my - (a->ixx - a->izz) * s->r * s->p) / a->iyy;
    float dr = (k.mz - (a->iyy - a->ixx) * s->p * s->q) / a->izz;
    s->p += dp * dt; s->q += dq * dt; s->r += dr * dt;
    /* Etwas Daempfung fuer das, was das Modell nicht kennt (Rumpf, Ruder
       im Windschatten).  Ohne sie schwingt das Seitenruder nach. */
    float damp = clampf(1.0f - dt * 0.6f, 0.0f, 1.0f);
    s->p *= damp; s->q *= damp; s->r *= damp;

    /* Lage: Eulerwinkel aus den Drehraten.  Senkrecht nach oben wird es
       singulaer -- dort wird der Nickwinkel begrenzt, ein Flugzeug in
       dieser Lage fliegt ohnehin nicht mehr. */
    if (ct < 0.05f && ct > -0.05f) ct = ct >= 0.0f ? 0.05f : -0.05f;
    float dphi = s->p + (s->q * sp + s->r * cp) * (st / ct);
    float dtheta = s->q * cp - s->r * sp;
    float dpsi = (s->q * sp + s->r * cp) / ct;
    s->roll_deg += dphi * RAD * dt;
    s->pitch_deg += dtheta * RAD * dt;
    s->heading_deg += dpsi * RAD * dt;
    if (s->pitch_deg > 87.0f) s->pitch_deg = 87.0f;
    if (s->pitch_deg < -87.0f) s->pitch_deg = -87.0f;
    /* Winkel mit fmodf normieren, nicht in einer Schleife: Wird ein Wert
       unendlich -- eine zu grosse Kraft, eine Traegheit nahe null --, dann
       laeuft `while (x >= 360) x -= 360` fuer immer.  Genau das ist
       passiert, und der Prueflauf blieb haengen statt abzustuerzen. */
    s->roll_deg = fmodf(s->roll_deg, 360.0f);
    if (s->roll_deg > 180.0f) s->roll_deg -= 360.0f;
    if (s->roll_deg < -180.0f) s->roll_deg += 360.0f;
    s->heading_deg = fmodf(s->heading_deg, 360.0f);
    if (s->heading_deg < 0.0f) s->heading_deg += 360.0f;

    /* Und wenn doch etwas entgleist ist: anhalten statt Unsinn rechnen.
       Ein Flugmodell, das NaN ausgibt, reisst den ganzen Renderer mit. */
    if (s->alt_m > s->ground_m + 30000.0f) s->alt_m = s->ground_m + 30000.0f;
    if (s->alt_m < s->ground_m - 1000.0f) s->alt_m = s->ground_m - 1000.0f;
    if (!isfinite(s->u) || !isfinite(s->w) || !isfinite(s->p) ||
        !isfinite(s->q) || !isfinite(s->alt_m) || !isfinite(s->roll_deg)) {
        s->u = s->v = s->w = 0.0f;
        s->p = s->q = s->r = 0.0f;
        s->roll_deg = s->pitch_deg = 0.0f;
        if (!isfinite(s->alt_m)) s->alt_m = s->ground_m;
        if (!isfinite(s->heading_deg)) s->heading_deg = 0.0f;
    }

    /* Vom Rumpf in die Welt: wohin es wirklich geht. */
    sp = sinf(s->roll_deg * DEG); cp = cosf(s->roll_deg * DEG);
    st = sinf(s->pitch_deg * DEG); ct = cosf(s->pitch_deg * DEG);
    float ss = sinf(s->heading_deg * DEG), cs = cosf(s->heading_deg * DEG);
    float vn = ct * cs * s->u + (sp * st * cs - cp * ss) * s->v + (cp * st * cs + sp * ss) * s->w;
    float ve = ct * ss * s->u + (sp * st * ss + cp * cs) * s->v + (cp * st * ss - sp * cs) * s->w;
    float vd = -st * s->u + sp * ct * s->v + cp * ct * s->w;
    s->north_m += vn * dt;
    s->east_m += ve * dt;
    s->alt_m -= vd * dt;
    /* Ohne Fahrwerk (eingefahren oder abgerissen) haelt nichts mehr - dann
       ist der Boden die Grenze, und der Aufprall kostet die Fahrt. */
    if (s->alt_m < s->ground_m) {
        s->alt_m = s->ground_m;
        if (s->w > 0.0f) s->w = 0.0f;
        s->on_ground = 1;
    }

    s->v_ms = sqrtf(s->u * s->u + s->v * s->v + s->w * s->w);
    s->vs_ms = -vd;
    s->alpha_deg = atan2f(s->w, s->u) * RAD;
    s->beta_deg = (s->v_ms > 1.0f) ? asinf(clampf(s->v / s->v_ms, -1.0f, 1.0f)) * RAD : 0.0f;
    s->load_g = (-k.fz / a->mass_kg) / G;
}

/* ------------------------------------------------------------------ *
 * Lesen.  Das Format ist zeilenweise und schlicht, damit acftconv.py es
 * schreiben kann, ohne eine Bibliothek zu brauchen -- und damit man es
 * von Hand nachbessern kann, wenn ein Flugzeug komisch fliegt.
 * ------------------------------------------------------------------ */
int blade_load(struct blade_aircraft *a, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int seen_geom = 0;
    memset(a, 0, sizeof(*a));
    a->rpm_idle = 700.0f; a->rpm_max = 2700.0f;
    a->cd_body = 0.035f;
    snprintf(a->name, sizeof(a->name), "?");
    while (fgets(line, sizeof(line), f)) {
        char key[32];
        if (sscanf(line, "%31s", key) != 1) continue;
        if (key[0] == '#') continue;
        const char *rest = line + strlen(key);
        if (!strcmp(key, "geometrie")) seen_geom = 1;
        else if (!strcmp(key, "name")) sscanf(rest, "%31s", a->name);
        else if (!strcmp(key, "mass_kg")) sscanf(rest, "%f", &a->mass_kg);
        else if (!strcmp(key, "inertia")) sscanf(rest, "%f %f %f", &a->ixx, &a->iyy, &a->izz);
        else if (!strcmp(key, "wing_area_m2")) sscanf(rest, "%f", &a->wing_area_m2);
        else if (!strcmp(key, "wing_span_m")) sscanf(rest, "%f", &a->wing_span_m);
        else if (!strcmp(key, "thrust_max_n")) sscanf(rest, "%f", &a->thrust_max_n);
        else if (!strcmp(key, "rpm_idle")) sscanf(rest, "%f", &a->rpm_idle);
        else if (!strcmp(key, "rpm_max")) sscanf(rest, "%f", &a->rpm_max);
        else if (!strcmp(key, "jet")) sscanf(rest, "%d", &a->jet);
        else if (!strcmp(key, "cd_body")) sscanf(rest, "%f", &a->cd_body);
        else if (!strcmp(key, "leistung_w")) sscanf(rest, "%f", &a->power_w);
        else if (!strcmp(key, "propeller_r")) sscanf(rest, "%f", &a->prop_r);
        else if (!strcmp(key, "reise_ms")) sscanf(rest, "%f", &a->reise_ms);
        else if (!strcmp(key, "flaeche") && a->nsurf < BLADE_MAX_SURF) {
            struct blade_surface *s = &a->surf[a->nsurf];
            int n = sscanf(rest, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d",
                           &s->x, &s->y, &s->z, &s->chord, &s->length, &s->taper,
                           &s->incidence_deg, &s->twist_deg, &s->dihedral_deg,
                           &s->sweep_deg, &s->camber,
                           &s->stall_aoa_deg, &s->stall_width_deg, &s->stall_peak,
                           &s->flap, &s->aileron, &s->elevator, &s->rudder,
                           &s->mirror, &s->vertical);
            if (n >= 18) ++a->nsurf;
        }
        else if (!strcmp(key, "bein") && a->ngear < BLADE_MAX_GEAR) {
            struct blade_gear *g = &a->gear[a->ngear];
            int n = sscanf(rest, "%f %f %f %f %f %f %f",
                           &g->x, &g->y, &g->z, &g->spring_n_m, &g->damp_ns_m,
                           &g->steer, &g->brake);
            if (n >= 5) ++a->ngear;
        }
    }
    fclose(f);
    if (!seen_geom || a->nsurf == 0 || a->mass_kg <= 0.0f) return 0;

    /* Traegheit, falls die Datei keine nennt.  YASim rechnet sie aus den
       Einzelmassen; die haben wir nicht, also aus Spannweite und Laenge
       geschaetzt -- Traegheitsradius etwa ein Fuenftel der Spannweite ums
       Rollen, knapp ein Drittel der Laenge ums Nicken.  Lieber grob
       geschaetzt als gar keine Traegheit. */
    if (a->ixx <= 0.0f) {
        float b = a->wing_span_m > 0.0f ? a->wing_span_m : 10.0f;
        float len = b * 0.8f;
        for (int i = 0; i < a->nsurf; ++i) {
            float hinten = -a->surf[i].x + a->surf[i].length;
            if (hinten > len) len = hinten;
        }
        a->ixx = a->mass_kg * (0.20f * b) * (0.20f * b);
        a->iyy = a->mass_kg * (0.30f * len) * (0.30f * len);
        a->izz = a->ixx + a->iyy;
    }
    blade_trim(a, a->reise_ms);
    return 1;
}


/* Warum in Teilschritten gerechnet wird.
 *
 * Die Fahrwerksfedern sind steif: 79 kN je Meter an jedem Hauptbein, dazu
 * die Traegheit ums Rollen.  Daraus wird eine Schwingung mit rund 13 rad/s,
 * also einer Viertelsekunde Umlauf.  Mit dem Bildabstand von 40 ms als
 * Zeitschritt waechst diese Schwingung bei jedem Schritt ein Stueck --
 * explizit gerechnet ist sie nicht stabil, gleichgueltig wie gut das
 * Fahrwerk gedaempft ist.  Auf dem Geraet sah man es: Das Flugzeug stand
 * mit angezogener Bremse und abgestelltem Motor auf der Bahn, begann zu
 * schaukeln und wurde nach fuenf Sekunden fortgeschleudert.
 *
 * Fuenf Millisekunden sind kurz genug (13 rad/s * 0,005 = 0,065) und
 * kosten wenig: Die Flaechenrechnung ist ein paar Dutzend Sinus, kein
 * Dreiecksnetz.
 */
void blade_step(struct blade_state *s, const struct blade_aircraft *a, float dt,
                float stick_roll, float stick_pitch, float rudder, float throttle,
                float flaps, int brake, int gear_down) {
    if (dt <= 0.0f) return;
    if (dt > 0.2f) dt = 0.2f;              /* nach einer Ladepause nicht aufholen */
    const float klein = 0.005f;
    int n = (int)(dt / klein) + 1;
    if (n > 40) n = 40;
    float teil = dt / n;
    for (int i = 0; i < n; ++i)
        blade_step_one(s, a, teil, stick_roll, stick_pitch, rudder, throttle,
                       flaps, brake, gear_down);
}
