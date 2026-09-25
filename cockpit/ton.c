/* Siehe ton.h: der Klang wird gerechnet, nicht abgespielt. */
#include "ton.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#define RATE 22050
#define BLOCK 512               /* 23 ms je Block - kurz genug zum Nachfuehren */

static pa_simple *pa;
static pthread_t faden;
static volatile int laeuft;

/* Was der Bildtakt setzt und der Klangfaden liest.  Kein Schloss: Es sind
   einzelne Fliesskommazahlen, und ein halb gelesener Wert klaenge fuer ein
   Dreiundzwanzigstel einer Sekunde minimal anders.  Dafuer ein Schloss zu
   nehmen hiesse, den Bildtakt auf den Klang warten zu lassen. */
static volatile float z_rpm, z_rpm_max = 2700.0f, z_throttle, z_v;
static volatile int z_ground, z_jet, z_engine;

void ton_zustand(float rpm, float rpm_max, float throttle, float v_ms,
                 int on_ground, int jet, int engine_on) {
    z_rpm = rpm;
    z_rpm_max = rpm_max > 100.0f ? rpm_max : 2700.0f;
    z_throttle = throttle;
    z_v = v_ms;
    z_ground = on_ground;
    z_jet = jet;
    z_engine = engine_on;
}

/* Ein billiges, gleichverteiltes Rauschen.  rand() waere langsamer und
   braeuchte ein Schloss. */
static unsigned int saat = 22222;
static float rauschen(void) {
    saat = saat * 1103515245u + 12345u;
    return (float)((saat >> 9) & 0x7fff) / 16384.0f - 1.0f;
}

static void *klangfaden(void *unbenutzt) {
    (void)unbenutzt;
    short puffer[BLOCK];
    /* Zustand der Klangerzeugung zwischen den Bloecken */
    float phase = 0.0f;          /* Propellerwelle */
    float phase2 = 0.0f;         /* zweite Harmonische */
    float tief = 0.0f;           /* Tiefpass fuer den Fahrtwind */
    float tief2 = 0.0f;          /* zweiter Tiefpass, fuer das Rollen */
    float turbine = 0.0f;        /* Tiefpass fuer die Turbine */
    float laut = 0.0f;           /* geglaettete Lautstaerke */

    while (laeuft) {
        float rpm = z_rpm, rpm_max = z_rpm_max, gas = z_throttle, v = z_v;
        int am_boden = z_ground, ist_jet = z_jet, motor = z_engine;

        /* Die Grundfrequenz des Propellers: ein Impuls je Blatt und
           Umdrehung.  Zwei Blaetter sind die uebliche Annahme. */
        float f_prop = rpm / 60.0f * 2.0f;
        if (f_prop < 1.0f) f_prop = 1.0f;
        float d_phase = f_prop / (float)RATE;
        float d_phase2 = d_phase * 2.0f;

        /* Lautstaerken.  Der Motor traegt mit der Drehzahl, der Fahrtwind
           mit dem Quadrat der Geschwindigkeit -- wie der Staudruck, der ihn
           macht. */
        float ziel = 0.0f;
        if (motor) ziel = 0.18f + 0.55f * (rpm / rpm_max);
        float wind = 0.30f * (v * v) / (80.0f * 80.0f);
        if (wind > 0.45f) wind = 0.45f;

        for (int i = 0; i < BLOCK; ++i) {
            laut += (ziel - laut) * 0.0008f;       /* weich nachziehen */

            float wert = 0.0f;
            if (ist_jet) {
                /* Turbine: gefiltertes Rauschen mit einem Pfeifen darueber.
                   Das Pfeifen liegt bei der Schaufelfrequenz, das Rauschen
                   ist der Strahl. */
                turbine += (rauschen() - turbine) * 0.08f;
                phase += d_phase * 6.0f;
                if (phase >= 1.0f) phase -= 1.0f;
                wert = turbine * laut * 0.9f
                     + sinf(phase * 6.2831853f) * laut * 0.25f;
            } else {
                /* Propeller: ein Saegezahn je Blattdurchgang, dazu die
                   zweite Harmonische -- zusammen klingt das nach Motor und
                   nicht nach Sinuston. */
                phase += d_phase;
                if (phase >= 1.0f) phase -= 1.0f;
                phase2 += d_phase2;
                if (phase2 >= 1.0f) phase2 -= 1.0f;
                float saege = phase * 2.0f - 1.0f;
                float saege2 = phase2 * 2.0f - 1.0f;
                wert = (saege * 0.6f + saege2 * 0.25f) * laut;
                /* Ein wenig Rauschen: ein Kolbenmotor ist kein Oszillator. */
                tief2 += (rauschen() - tief2) * 0.25f;
                wert += tief2 * laut * 0.18f;
            }

            /* Fahrtwind: Rauschen, zweimal tiefpassgefiltert, damit es
               rauscht und nicht zischt. */
            tief += (rauschen() - tief) * 0.10f;
            wert += tief * wind;

            /* Am Boden rumpelt es zusaetzlich, solange es rollt. */
            if (am_boden && v > 1.0f) {
                float rollen = v / 40.0f;
                if (rollen > 1.0f) rollen = 1.0f;
                wert += tief2 * 0.18f * rollen;
            }

            if (wert > 1.0f) wert = 1.0f;
            if (wert < -1.0f) wert = -1.0f;
            puffer[i] = (short)(wert * 12000.0f);
        }

        int fehler = 0;
        if (pa_simple_write(pa, puffer, sizeof(puffer), &fehler) < 0) {
            fprintf(stderr, "Ton: %s\n", pa_strerror(fehler));
            break;
        }
    }
    return NULL;
}

int ton_start(void) {
    const char *aus = getenv("COCKPIT_TON");
    if (aus && aus[0] == '0') return 0;

    static const pa_sample_spec spec = {
        .format = PA_SAMPLE_S16LE, .rate = RATE, .channels = 1
    };
    /* Kurze Puffer: Der Klang soll der Drehzahl folgen, nicht ihr
       hinterherlaufen.  Drei Bloecke sind 70 Millisekunden. */
    static const pa_buffer_attr attr = {
        .maxlength = (uint32_t)-1,
        .tlength = BLOCK * 2 * 3,
        .prebuf = (uint32_t)-1,
        .minreq = (uint32_t)-1,
        .fragsize = (uint32_t)-1
    };
    int fehler = 0;
    pa = pa_simple_new(NULL, "FG Fly", PA_STREAM_PLAYBACK, NULL,
                       "Flugzeug", &spec, NULL, &attr, &fehler);
    if (!pa) {
        fprintf(stderr, "Kein Ton: %s\n", pa_strerror(fehler));
        return 0;
    }
    laeuft = 1;
    if (pthread_create(&faden, NULL, klangfaden, NULL) != 0) {
        pa_simple_free(pa);
        pa = NULL;
        laeuft = 0;
        return 0;
    }
    printf("Ton: PulseAudio, %d Hz, Bloecke zu %d\n", RATE, BLOCK);
    return 1;
}

void ton_stop(void) {
    if (!laeuft) return;
    laeuft = 0;
    pthread_join(faden, NULL);
    if (pa) {
        pa_simple_free(pa);
        pa = NULL;
    }
}
