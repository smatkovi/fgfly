/*
 * cockpit - die Bedienung eines kleinen Flugsimulators fuer N9/N950.
 *
 * Nachgebaut ist, was X-Plane 9 auf derselben SGX 530 benutzt: neigen steuert
 * Rollen und Nicken, links ein senkrechter Schubhebel, rechts die Klappen,
 * unten die Bremse, oben links der Ansichtsknopf.  Dazu ein Horizont, damit
 * die Lage sichtbar ist.
 *
 * Alles zeichnet **ein** Programm aus zwei Shadern von zusammen rund 200
 * Bytes: die Geometrie entsteht je Bild auf der CPU (einige hundert Eckpunkte)
 * und geht in einem einzigen glDrawArrays an die GPU.  Das ist die Antwort auf
 * die Frage, ob es auch im Kilobyte-Bereich geht - hier sind es 0,2 KB.
 *
 *   cockpit [sekunden]
 *
 * Umgebung:
 *   COCKPIT_SHOT=<datei>   legt ein Bild als PPM ab (glReadPixels)
 *   COCKPIT_SHOT_FRAME=N   welches Bild (Vorgabe 60)
 *   COCKPIT_AXES=rx,ry     welche Beschleunigungsachsen Rollen und Nicken
 *                          geben, mit Vorzeichen (Vorgabe 0,1 = x und y)
 *   COCKPIT_NOSENSOR=1     Sensor nicht lesen (fuer einen Lauf ohne Bewegung)
 *   COCKPIT_EDGE=px        wie breit der Rand ist, der dem Randwisch des
 *                          Fenstermanagers gehoert (Vorgabe 12, 0 = aus)
 *   COCKPIT_ANGLE=grad     _MEEGOTOUCH_ORIENTATION_ANGLE (Vorgabe 0, quer)
 *   COCKPIT_TOUCH=<geraet> diesen Beruehrungsschirm lesen statt zu suchen
 *   COCKPIT_ICAO=LOWW      um welchen Platz die Kacheln geladen werden
 */
#include "fdm.h"
#include "terrain.h"
#include "touchinput.h"
#include "xtouch.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>

#define ACCEL_PATH "/sys/devices/platform/lis3lv02d/position"
#define MAX_VERTS 24576

static const char *VERT =
    "attribute vec2 a_pos;\n"
    "attribute vec4 a_col;\n"
    "varying lowp vec4 v_col;\n"
    "void main() { v_col = a_col; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static const char *FRAG =
    "varying lowp vec4 v_col;\n"
    "void main() { gl_FragColor = v_col; }\n";

struct vertex { GLfloat x, y, r, g, b, a; };
static struct vertex verts[MAX_VERTS];
static int nverts;
static float aspect = 854.0f / 480.0f;

/* Zustand, den die Bedienung setzt */
static float throttle = 0.0f, flaps = 0.0f;
static int brake = 1, view_mode = 0, gear_down = 1;
static float rudder;                    /* -1 links, +1 rechts, federt zurueck */
static int quit_now;
static float tilt_roll, tilt_pitch;          /* was der Sensor meldet */
static struct terrain model;
static int have_model;

/* --- Startbildschirm ---------------------------------------------------- */
#define MAX_ENTRIES 12
struct entry { char name[28]; char path[256]; };
static struct entry aircraft[MAX_ENTRIES], scenery[MAX_ENTRIES];
static int n_aircraft, n_scenery, sel_aircraft, sel_scenery;
/* Der Startbildschirm ist eine kleine Folge von Seiten: Flugzeug und Art des
   Platzes, dann zwei Buchstaben der Kennung, dann die Liste, dann die Bahn. */
enum { S_MAIN, S_LETTER, S_COUNTRY, S_LIST, S_ACFT, S_RWY, S_FETCH };
static int screen_start = 1;            /* 1 = waehlen, 0 = fliegen */
static int page = S_MAIN;
static int start_airborne;

struct airport {
    char icao[5];
    char name[20];
    char country[24];           /* deutsch, Grossbuchstaben, _ statt Leerzeichen */
    double lat, lon;
    int elev_ft, cat, length;
    int rwy[8], nrwy;
};
static struct airport apt_page[7];
static int apt_page_n, apt_total, apt_offset, apt_sel, apt_cat, apt_rwy;
static char apt_prefix[3] = "";
/* Zwei Wege zum Platz: ueber die Kennung (zwei Buchstaben) oder ueber das
   Land.  Das Land ist der bequemere - man weiss, wo man fliegen will, aber
   nicht, dass Wien LOWW heisst. */
static int by_country = 1;
static char apt_country[24] = "";
static char cland[64][24];              /* Laender mit diesem Anfangsbuchstaben */
static int n_cland, cland_offset;
static char cland_initial = 'A';
static struct airport apt_chosen;
static int have_chosen;
static char fetch_note[64] = "";
static int browse_mode;                 /* 0 = Flugplaetze, 1 = Hangar */

struct catalog_entry { char id[28]; char name[28]; int mb; char url[160]; };
static struct catalog_entry cat_page[7];
static int cat_page_n, cat_total, cat_offset, cat_sel;
static struct catalog_entry cat_chosen;
static struct fdm_state fdm;
static struct fdm_aircraft aircraft_data;
#define MAX_TILES 24
static struct terrain land[MAX_TILES];
static struct terrain_frame land_frame;
static int nland;
static int coarse_of[MAX_TILES];        /* grobe Fassung je Kachel, -1 = keine */
static int is_coarse[MAX_TILES];        /* diese Kachel ist selbst eine grobe */
static int have_terrain;
static int tiles_drawn, triangles_drawn, coarse_drawn, nland_total;
/* Aussenansicht: mit einem Finger drehen, mit zweien heran und weg. */
static float orbit_az, orbit_el = 14.0f, chase_m = 22.0f;
static const struct fdm_aircraft *acft = &aircraft_data;

static void push(float x, float y, const float c[4]) {
    if (nverts >= MAX_VERTS) return;
    struct vertex *v = &verts[nverts++];
    v->x = x; v->y = y; v->r = c[0]; v->g = c[1]; v->b = c[2]; v->a = c[3];
}

/* Rechteck in Bildschirmanteilen (0..1, links oben = 0,0) */
/* Die Bedienelemente sind nicht zu sehen, solange niemand sie anfasst - und
   dann durchscheinend, wie bei X-Plane.  `ui_alpha` faellt nach dem letzten
   Finger von selbst wieder auf null. */
static float ui_alpha = 0.0f;

static void rect(float x, float y, float w, float h, const float c[4]);

static void rect_ui(float x, float y, float w, float h, const float c[4]) {
    if (ui_alpha <= 0.002f) return;
    const float faded[4] = { c[0], c[1], c[2], c[3] * ui_alpha };
    rect(x, y, w, h, faded);
}

static void rect(float x, float y, float w, float h, const float c[4]) {
    float x0 = x * 2.0f - 1.0f, x1 = (x + w) * 2.0f - 1.0f;
    float y0 = 1.0f - y * 2.0f, y1 = 1.0f - (y + h) * 2.0f;
    push(x0, y0, c); push(x1, y0, c); push(x1, y1, c);
    push(x0, y0, c); push(x1, y1, c); push(x0, y1, c);
}

/* Runde Instrumente.  Der Halbmesser zaehlt in Hoehenanteilen; in der Breite
   wird er durch das Seitenverhaeltnis geteilt, sonst werden aus Kreisen
   Ellipsen. */
static void disc(float cx, float cy, float r, int segs, const float c[4]) {
    float px = 2.0f * cx - 1.0f, py = 1.0f - 2.0f * cy;
    float rx = 2.0f * r / aspect, ry = 2.0f * r;
    for (int i = 0; i < segs; ++i) {
        float a0 = 2.0f * (float)M_PI * i / segs, a1 = 2.0f * (float)M_PI * (i + 1) / segs;
        push(px, py, c);
        push(px + rx * sinf(a0), py + ry * cosf(a0), c);
        push(px + rx * sinf(a1), py + ry * cosf(a1), c);
    }
}

/* Ein Strich vom Halbmesser r0 bis r1 unter dem Winkel ang (0 = oben, im
   Uhrzeigersinn) - Zeiger und Teilstriche sind dasselbe, nur andere Masse. */
static void spoke(float cx, float cy, float r0, float r1, float ang, float w,
                  const float c[4]) {
    float px = 2.0f * cx - 1.0f, py = 1.0f - 2.0f * cy;
    float a = ang * (float)M_PI / 180.0f;
    float sx = sinf(a) * 2.0f / aspect, sy = cosf(a) * 2.0f;
    float nx = cosf(a) * 2.0f / aspect * w, ny = -sinf(a) * 2.0f * w;
    float x0 = px + sx * r0, y0 = py + sy * r0;
    float x1 = px + sx * r1, y1 = py + sy * r1;
    push(x0 - nx, y0 - ny, c); push(x1 - nx, y1 - ny, c); push(x1 + nx, y1 + ny, c);
    push(x0 - nx, y0 - ny, c); push(x1 + nx, y1 + ny, c); push(x0 + nx, y0 + ny, c);
}

/* Viereck in gleichmassigen Einheiten (y wie NDC, x davon unabhaengig),
   gedreht um roll und um pitch verschoben - so bleibt der Horizont bei
   854 x 480 rund und kippt nicht elliptisch. */
static void horizon_quad(float u0, float v0, float u1, float v1,
                         float roll, float shift, const float c[4]) {
    float s = sinf(roll), co = cosf(roll);
    float ux[4] = {u0, u1, u1, u0}, uy[4] = {v0, v0, v1, v1};
    float px[4], py[4];
    for (int i = 0; i < 4; ++i) {
        float x = ux[i], y = uy[i] + shift;
        px[i] = (x * co - y * s) / aspect;
        py[i] = x * s + y * co;
    }
    push(px[0], py[0], c); push(px[1], py[1], c); push(px[2], py[2], c);
    push(px[0], py[0], c); push(px[2], py[2], c); push(px[3], py[3], c);
}

static int read_accel(float *roll, float *pitch) {
    FILE *f = fopen(ACCEL_PATH, "r");
    if (!f) return 0;
    int a[3] = {0, 0, 0};
    int got = fscanf(f, "(%d,%d,%d)", &a[0], &a[1], &a[2]);
    fclose(f);
    if (got != 3) return 0;

    int ri = 0, pi = 1;
    const char *axes = getenv("COCKPIT_AXES");
    if (axes) sscanf(axes, "%d,%d", &ri, &pi);
    float rv = a[ri < 0 ? -ri : ri] * (ri < 0 ? -1.0f : 1.0f);
    float pv = a[pi < 0 ? -pi : pi] * (pi < 0 ? -1.0f : 1.0f);
    *roll = rv / 1000.0f * 90.0f;
    *pitch = pv / 1000.0f * 90.0f;
    if (*roll > 90.0f) *roll = 90.0f;
    if (*roll < -90.0f) *roll = -90.0f;
    if (*pitch > 90.0f) *pitch = 90.0f;
    if (*pitch < -90.0f) *pitch = -90.0f;
    return 1;
}

/* Sieben-Segment-Ziffern: eine Ziffer sind hoechstens sieben Rechtecke, also
   42 Eckpunkte.  Mehr braucht eine Zahl ueber dem Horizont nicht. */
static void digit(float x, float y, float w, float h, int value, const float c[4]) {
    static const unsigned char seg[10] = {
        /* a b c d e f g */
        0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B
    };
    if (value < 0 || value > 9) return;
    unsigned char s = seg[value];
    float t = h * 0.14f;                 /* Strichstaerke */
    float mid = y + h * 0.5f - t * 0.5f;
    if (s & 0x40) rect(x + t, y, w - 2 * t, t, c);                        /* a oben */
    if (s & 0x20) rect(x + w - t, y + t * 0.5f, t, h * 0.5f - t, c);      /* b rechts oben */
    if (s & 0x10) rect(x + w - t, mid + t * 0.5f, t, h * 0.5f - t, c);    /* c rechts unten */
    if (s & 0x08) rect(x + t, y + h - t, w - 2 * t, t, c);                /* d unten */
    if (s & 0x04) rect(x, mid + t * 0.5f, t, h * 0.5f - t, c);            /* e links unten */
    if (s & 0x02) rect(x, y + t * 0.5f, t, h * 0.5f - t, c);              /* f links oben */
    if (s & 0x01) rect(x + t, mid, w - 2 * t, t, c);                      /* g Mitte */
}

static void number(float x, float y, float w, float h, int value, int digits,
                   const float c[4]) {
    for (int i = digits - 1; i >= 0; --i) {
        digit(x + i * w * 1.30f, y, w, h, value % 10, c);
        value /= 10;
    }
}

/* Eine Schrift von fuenf mal sieben Punkten, als Rechtecke gezeichnet - je
   Zeile eines, nicht je Punkt.  Mehr braucht ein Startbildschirm nicht, und
   eine Datei kostet es auch nicht. */
static const char FONT_ORDER[] = " -./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const unsigned char FONT[sizeof(FONT_ORDER) - 1][7] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* Leerzeichen */
    { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },   /* - */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C },   /* . */
    { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },   /* / */
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   /* 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   /* 2 */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },   /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   /* 9 */
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   /* A */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },   /* B */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },   /* C */
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },   /* D */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },   /* E */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },   /* F */
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F },   /* G */
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   /* H */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F },   /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },   /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },   /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },   /* L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },   /* M */
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },   /* N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   /* O */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },   /* P */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },   /* Q */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },   /* R */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },   /* S */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },   /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },   /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },   /* W */
    { 0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11 },   /* X */
    { 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04 },   /* Y */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },   /* Z */
};

static void glyph(float x, float y, float w, float h, int index, const float c[4]) {
    float px = w / 5.0f, py = h / 7.0f;
    for (int row = 0; row < 7; ++row) {
        unsigned char bits = FONT[index][row];
        int col = 0;
        while (col < 5) {
            if (!(bits & (0x10 >> col))) { ++col; continue; }
            int start = col;
            while (col < 5 && (bits & (0x10 >> col))) ++col;
            rect(x + start * px, y + row * py, (col - start) * px, py, c);
        }
    }
}

static float text(float x, float y, float h, const char *str, const float c[4]);

static float text_ui(float x, float y, float h, const char *str, const float c[4]) {
    if (ui_alpha <= 0.002f) return x;
    const float faded[4] = { c[0], c[1], c[2], c[3] * ui_alpha };
    return text(x, y, h, str, faded);
}

/* Schreibt in Grossbuchstaben; was die Schrift nicht kennt, bleibt leer. */
static float text(float x, float y, float h, const char *str, const float c[4]) {
    float w = h * 5.0f / 7.0f / aspect;
    for (const char *p = str; *p; ++p) {
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        const char *at = strchr(FONT_ORDER, ch);
        if (at) glyph(x, y, w, h, (int)(at - FONT_ORDER), c);
        x += w * 1.25f;
    }
    return x;
}

/* Ein Zifferblatt: Scheibe, Teilstriche, Beschriftung, Zeiger.  Die
   Beschriftung sitzt auf demselben Kreis wie die Striche, nur weiter innen,
   und wird an ihrer halben Breite ausgerichtet - die Schrift kann nicht
   zentrieren, also rechnen wir es aus. */
static void gauge(float cx, float cy, float r, float value, float vmax,
                  int ticks, const float needle_col[4],
                  const char *const *labels, int nlabels, float a0, float a1) {
    static const float face[4] = {0.06f, 0.06f, 0.07f, 1.0f};
    static const float mark[4] = {0.75f, 0.75f, 0.78f, 1.0f};
    static const float text_col[4] = {0.82f, 0.84f, 0.88f, 1.0f};
    disc(cx, cy, r, 28, face);
    for (int i = 0; i <= ticks; ++i) {
        float ang = a0 + (a1 - a0) * i / ticks;
        spoke(cx, cy, r * 0.80f, r * 0.96f, ang, 0.006f, mark);
    }
    /* Die Beschriftung laeuft nur bis 145 Grad, nicht bis 160: unten treffen
       sich sonst die erste und die letzte Zahl. */
    float th = r * 0.22f;
    for (int i = 0; i < nlabels; ++i) {
        float inner0 = a0 + (a1 - a0) * 0.045f, inner1 = a1 - (a1 - a0) * 0.045f;
        float ang = (inner0 + (inner1 - inner0) * i / (nlabels - 1)) * (float)M_PI / 180.0f;
        float lx = cx + sinf(ang) * r * 0.56f / aspect;
        float ly = cy - cosf(ang) * r * 0.56f;
        float w = th * 5.0f / 7.0f / aspect * 1.25f * (float)strlen(labels[i]);
        text(lx - w * 0.5f, ly - th * 0.5f, th, labels[i], text_col);
    }
    float frac = value / vmax;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    spoke(cx, cy, -r * 0.12f, r * 0.78f, a0 + (a1 - a0) * frac, 0.010f, needle_col);
}

/* Eine Sprosse der Leiter im Blickfeld: zwei Striche links und rechts der
   Mitte, in der Lage des Flugzeugs mitgedreht.  Ein halber Bildschirm sind
   45 Grad - dieselbe Rechnung wie beim Horizont. */
static void hud_rung(float angle_deg, float roll, float shift, float half,
                     const float col[4]) {
    float y = angle_deg / 45.0f;
    horizon_quad(-half, y - 0.004f, -0.12f, y + 0.004f, roll, shift, col);
    horizon_quad(0.12f, y - 0.004f, half, y + 0.004f, roll, shift, col);
}

static void build_frame(void) {
    static const float sky[4]    = {0.35f, 0.55f, 0.85f, 1.0f};
    static const float ground[4] = {0.38f, 0.29f, 0.17f, 1.0f};
    static const float line[4]   = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float track[4]  = {0.10f, 0.10f, 0.12f, 1.0f};
    static const float knob[4]   = {0.85f, 0.85f, 0.88f, 1.0f};
    static const float lit[4]    = {0.90f, 0.25f, 0.15f, 1.0f};
    static const float dim[4]    = {0.25f, 0.25f, 0.28f, 1.0f};
    static const float white[4]  = {0.95f, 0.95f, 0.95f, 1.0f};
    static const float amber[4]  = {0.95f, 0.70f, 0.15f, 1.0f};
    static const float green[4]  = {0.45f, 0.85f, 0.45f, 1.0f};
    static const float black[4]  = {0.05f, 0.06f, 0.07f, 1.0f};

    nverts = 0;
    float roll = fdm.roll_deg * (float)M_PI / 180.0f;
    /* Nase hoch schiebt den Horizont nach unten, nicht nach oben. */
    float shift = -fdm.pitch_deg / 45.0f;     /* 45 Grad = ein halbes Bild */

    if (!have_terrain) {            /* ohne Gelaende malt der Horizont die Welt */
        horizon_quad(-3.0f, 0.0f, 3.0f, 3.0f, roll, shift, sky);
        horizon_quad(-3.0f, -3.0f, 3.0f, 0.0f, roll, shift, ground);
        horizon_quad(-3.0f, -0.006f, 3.0f, 0.006f, roll, shift, line);
    }

    /* Kompassrichtung als Zahl oben in der Mitte, dreistellig wie im Funk */
    {
        int hdg = (int)(fdm.heading_deg + 0.5f) % 360;
        rect(0.435f, 0.015f, 0.13f, 0.095f, track);
        number(0.452f, 0.030f, 0.028f, 0.065f, hdg, 3, white);
    }

    /* Das feste Flugzeugzeichen in der Mitte - der Fluglageanzeiger */
    if (view_mode != 2) {
        rect(0.44f, 0.345f, 0.04f, 0.010f, amber);
        rect(0.52f, 0.345f, 0.04f, 0.010f, amber);
        rect(0.495f, 0.335f, 0.010f, 0.030f, amber);
    }

    /* Im Verfolger steht das Flugzeug in der Mitte des Bildes - die Kamera
       sieht ihm ja nach.  Ein paar Rechtecke reichen, es von hinten zu
       erkennen. */
    if (view_mode == 2 && !have_model) {
        rect(0.475f, 0.44f, 0.05f, 0.11f, dim);          /* Rumpf */
        rect(0.40f, 0.475f, 0.20f, 0.022f, dim);         /* Flaechen */
        rect(0.455f, 0.425f, 0.09f, 0.018f, dim);        /* Hoehenleitwerk */
        rect(0.495f, 0.405f, 0.010f, 0.045f, dim);       /* Seitenleitwerk */
    }

    /* Die gruene Anzeige im Blickfeld - wie die HUD-Sicht von X-Plane:
       Leiter fuer die Lage, Fahrt links, Hoehe rechts, sonst nichts.  Sie
       sitzt auf der Sicht nach vorn, also in Ansicht 1. */
    if (view_mode == 1) {
        static const float hud[4] = {0.25f, 1.00f, 0.35f, 1.0f};
        horizon_quad(-0.75f, -0.004f, 0.75f, 0.004f, roll, shift, hud);
        for (int a = 5; a <= 20; a += 5) {
            hud_rung((float)a, roll, shift, a % 10 ? 0.28f : 0.40f, hud);
            hud_rung((float)-a, roll, shift, a % 10 ? 0.28f : 0.40f, hud);
        }
        /* Fahrt links, Hoehe rechts, Steigen darunter - in Ziffern, wie im
           Blickfeld ueblich. */
        number(0.045f, 0.46f, 0.030f, 0.070f, (int)(fdm.v_ms * 1.94384f + 0.5f), 3, hud);
        number(0.80f, 0.46f, 0.030f, 0.070f,
               (int)(fdm.alt_m * 3.28084f + 0.5f) % 100000, 5, hud);
        int vs = (int)(fdm.vs_ms * 196.85f);
        if (vs < 0) vs = -vs;
        number(0.82f, 0.56f, 0.022f, 0.050f, vs, 4, hud);
        rect(0.80f, 0.575f, 0.012f, 0.008f, hud);        /* Vorzeichen: Strich */
        if (fdm.vs_ms > 0.1f) rect(0.804f, 0.567f, 0.004f, 0.024f, hud);
    }

    /* Vier runde Instrumente: Fahrt, Hoehe, Variometer, Drehzahl.
       Nur in der Kanzelsicht - die freie Sicht und der Verfolger zeigen
       Gelaende, nicht Zifferblaetter. */
    float ias_kt = fdm.v_ms * 1.94384f;
    float alt_ft = fdm.alt_m * 3.28084f;
    float vs_fpm = fdm.vs_ms * 196.85f;
    if (view_mode == 0) {
    /* Der Halbmesser zaehlt in Hoehenanteilen, in der Breite wird er durch
       das Seitenverhaeltnis geteilt: 0,13 sind 0,073 breit, also braucht es
       mindestens 0,155 Abstand, damit sich die Zifferblaetter nicht ueberdecken. */
    static const char *const ias_labels[] = {"0", "40", "80", "120", "160"};
    static const char *const alt_labels[] = {"0", "2", "4", "6", "8", "10"};
    static const char *const vs_labels[] = {"-2", "-1", "0", "1", "2"};
    static const char *const rpm_labels[] = {"0", "10", "20", "30"};
    gauge(0.25f, 0.755f, 0.125f, ias_kt, 160.0f, 8, white, ias_labels, 5, -160.0f, 160.0f);
    gauge(0.405f, 0.755f, 0.125f, fmodf(alt_ft, 1000.0f), 1000.0f, 10, white, alt_labels, 6,
          -160.0f, 160.0f);
    /* Das Variometer hat die Null auf neun Uhr, Steigen darueber, Sinken
       darunter - so steht es in jedem Flugzeug. */
    gauge(0.56f, 0.755f, 0.125f, vs_fpm + 2000.0f, 4000.0f, 8, white, vs_labels, 5,
          -250.0f, 70.0f);
    gauge(0.715f, 0.755f, 0.125f, fdm.rpm, 3000.0f, 6, green, rpm_labels, 4, -160.0f, 160.0f);

    /* Die Hoehe als Zahl - ein Zeiger allein ist auf 100 Fuss genau abzulesen,
       aber nicht auf 1000.  Oben links, wo nichts anderes steht. */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)(alt_ft + 0.5f));
        text(0.035f, 0.035f, 0.055f, buf, white);
        snprintf(buf, sizeof(buf), "%d", (int)(ias_kt + 0.5f));
        text(0.035f, 0.105f, 0.045f, buf, amber);
    }
    }

    /* Alles von hier an ist Bedienung: unsichtbar, bis ein Finger kommt, und
       dann durchscheinend - so macht es X-Plane, und so bleibt die Sicht
       frei.  `rect_ui` und `text_ui` blenden mit `ui_alpha`. */

    /* Schubhebel links, Klappen rechts - wie bei X-Plane */
    rect_ui(0.04f, 0.15f, 0.06f, 0.70f, track);
    rect_ui(0.035f, 0.15f + (1.0f - throttle) * 0.66f, 0.07f, 0.04f, knob);
    rect_ui(0.90f, 0.15f, 0.06f, 0.70f, track);
    rect_ui(0.895f, 0.15f + (1.0f - flaps) * 0.66f, 0.07f, 0.04f, knob);

    /* Fahrwerk links unter dem Schubhebel - wie bei X-Plane; gruen heisst
       draussen und verriegelt.  Bremse rechts, Ansicht oben links. */
    rect_ui(0.03f, 0.87f, 0.10f, 0.09f, gear_down ? green : dim);
    rect_ui(0.755f, 0.885f, 0.11f, 0.09f, brake ? lit : dim);
    rect_ui(0.14f, 0.03f, 0.13f, 0.08f,
            view_mode == 0 ? dim : (view_mode == 1 ? knob : green));

    /* Der Anlasser.  Steht der Motor, leuchtet er gruen und heisst START;
       laeuft er, ist er dunkel und heisst STOP. */
    rect_ui(0.30f, 0.03f, 0.13f, 0.08f, fdm.engine_on ? dim : green);
    text_ui(0.315f, 0.048f, 0.048f, fdm.engine_on ? "STOP" : "START",
            fdm.engine_on ? white : black);

    /* Seitenruder: waagrecht unten, federt in die Mitte zurueck.  Am Boden
       lenkt es das Bugrad, in der Luft giert es. */
    rect_ui(0.22f, 0.925f, 0.40f, 0.045f, track);
    rect_ui(0.22f + (rudder + 1.0f) * 0.5f * 0.36f, 0.918f, 0.04f, 0.06f, knob);

    /* Und ein Weg hinaus - ohne Fenstermanager gibt es sonst keinen. */
    rect_ui(0.915f, 0.02f, 0.07f, 0.085f, lit);
    text_ui(0.935f, 0.035f, 0.055f, "X", white);
}

static GLuint compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "shader: %s\n", log);
        exit(2);
    }
    return sh;
}

/* 30 Grad Neigung sind voller Ausschlag, die ersten vier bleiben tot -
   sonst zittert die Lage im Rauschen des Sensors. */
static float stick_from_tilt(float tilt_deg) {
    float dead = 4.0f;
    if (fabsf(tilt_deg) < dead) return 0.0f;
    float v = (tilt_deg - (tilt_deg > 0.0f ? dead : -dead)) / 26.0f;
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

/* Was auf dem Geraet liegt: Flugzeuge sind *.fdm, Szenerien Verzeichnisse
   mit mindestens einem .fgb darin. */
static int has_bundle(const char *dir) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;
    while (d && (e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && !strcmp(dot, ".fgb")) { found = 1; break; }
    }
    if (d) closedir(d);
    return found;
}

static void add_entry(struct entry *list, int *n, const char *name, const char *path) {
    if (*n >= MAX_ENTRIES) return;
    snprintf(list[*n].name, sizeof(list[*n].name), "%s", name);
    snprintf(list[*n].path, sizeof(list[*n].path), "%s", path);
    ++*n;
}

static void scan_data(const char *root) {
    DIR *d = opendir(root);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
        const char *dot = strrchr(e->d_name, '.');
        struct stat st;
        if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (has_bundle(path)) add_entry(scenery, &n_scenery, e->d_name, path);
        } else if (dot && !strcmp(dot, ".fdm")) {
            char name[28];
            snprintf(name, sizeof(name), "%.*s", (int)(dot - e->d_name), e->d_name);
            add_entry(aircraft, &n_aircraft, name, path);
        }
    }
    if (d) closedir(d);
}

/* Der Mittelpunkt einer Kachel steht in den ersten 32 Bytes des Buendels -
   den liest man, ohne die Kachel zu laden. */
static int bundle_center(const char *path, double c[3]) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4];
    unsigned int flags;
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "FGB1", 4) &&
             fread(&flags, 4, 1, f) == 1 && fread(c, 8, 3, f) == 3;
    fclose(f);
    return ok;
}

/* Ein Verzeichnis voller Kacheln: **nicht** alle laden.  Bei 79 Kacheln waeren
   das 1,8 Millionen Dreiecke und 160 MB Bilder, und das Laden allein dauert
   eine Viertelminute.  Also erst die Mittelpunkte lesen, die Kachel in der
   Mitte des Gebiets zum Ausgangspunkt machen und von dort die naechsten
   nehmen. */
/* Eine Kachel mit vier Grossbuchstaben als Namen ist ein Flugplatz -
   TerraSync legt ihn als eigene Kachel neben die Gelaendekacheln. */
static int is_airport_tile(const char *tile_name, char icao_out[8]) {
    char icao[16];
    snprintf(icao, sizeof(icao), "%s", tile_name);
    char *dot = strchr(icao, '.');
    if (dot) *dot = '\0';
    if (strlen(icao) != 4) return 0;
    for (int i = 0; i < 4; ++i)
        if (icao[i] < 'A' || icao[i] > 'Z') return 0;
    if (icao_out) snprintf(icao_out, 8, "%s", icao);
    return 1;
}

/* `want` ist die Kennung des Platzes, um den herum geladen werden soll -
   sonst wird der Flugplatz genommen, der der Mitte des Gebiets am naechsten
   liegt.  Ohne das laedt die Auswahl im Menue die Kacheln um irgendeinen
   anderen Platz, und man steht an den Koordinaten seines eigenen, aber ohne
   Boden darunter. */
static void load_terrain_dir(const char *tpath, const char *want) {
    struct cand { char path[288]; double c[3]; double dist; };
    static struct cand cand[256];
    int n = 0;

    DIR *d = opendir(tpath);
    struct dirent *e;
    while (d && (e = readdir(d)) && n < 256) {
        size_t len = strlen(e->d_name);
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcmp(dot, ".fgb")) continue;
        if (len > 8 && !strcmp(e->d_name + len - 8, ".lod.fgb")) continue;
        snprintf(cand[n].path, sizeof(cand[n].path), "%s/%s", tpath, e->d_name);
        if (bundle_center(cand[n].path, cand[n].c)) ++n;
    }
    if (d) closedir(d);
    if (n == 0) return;

    double mid[3] = {0, 0, 0};
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k) mid[k] += cand[i].c[k] / n;

    /* Am liebsten faengt man auf einem Flugplatz an.  Die heissen in
       TerraSync nach ihrer Kennung - vier Grossbuchstaben - und tragen Bahn
       und Vorfeld; alles andere sind Zahlen.  Gibt es keinen, nehmen wir die
       Kachel in der Mitte des Gebiets. */
    int best = -1;
    double best_d = 1e30;
    if (want && *want) {                /* der gewaehlte Platz, wenn er daliegt */
        char wanted[16];
        snprintf(wanted, sizeof(wanted), "%s.fgb", want);
        for (int i = 0; i < n && best < 0; ++i) {
            const char *name = strrchr(cand[i].path, '/');
            name = name ? name + 1 : cand[i].path;
            if (!strcmp(name, wanted)) best = i;
        }
    }
    for (int pass = 0; pass < 2 && best < 0; ++pass) {
        for (int i = 0; i < n; ++i) {
            const char *name = strrchr(cand[i].path, '/');
            name = name ? name + 1 : cand[i].path;
            int is_airport = 1;
            for (int k = 0; k < 4; ++k)
                if (name[k] < 'A' || name[k] > 'Z') is_airport = 0;
            if (name[4] != '.') is_airport = 0;
            if (pass == 0 && !is_airport) continue;
            double dx = cand[i].c[0] - mid[0], dy = cand[i].c[1] - mid[1];
            double dz = cand[i].c[2] - mid[2];
            double dist = dx * dx + dy * dy + dz * dz;
            if (dist < best_d) { best_d = dist; best = i; }
        }
    }
    if (best < 0) best = 0;
    for (int i = 0; i < n; ++i) {
        double dx = cand[i].c[0] - cand[best].c[0];
        double dy = cand[i].c[1] - cand[best].c[1];
        double dz = cand[i].c[2] - cand[best].c[2];
        cand[i].dist = dx * dx + dy * dy + dz * dz;
    }
    for (int i = 1; i < n; ++i) {            /* klein genug fuer Einfuegen */
        struct cand tmp = cand[i];
        int j = i - 1;
        while (j >= 0 && cand[j].dist > tmp.dist) { cand[j + 1] = cand[j]; --j; }
        cand[j + 1] = tmp;
    }

    /* Neun feine plus neun grobe passen in MAX_TILES und sind auf dem Geraet
       in ein paar Sekunden geladen. */
    int limit = 9;
    if (limit > n) limit = n;
    for (int i = 0; i < limit && nland + 2 <= MAX_TILES; ++i) {
        if (!terrain_load(&land[nland], cand[i].path, &land_frame)) continue;
        /* Die Flugplatzkachel bekommt kein Bild: sie ist klein (LOWW sind
           21 636 Dreiecke in 31 Gruppen), und nur nach Material gezeichnet
           ist der Asphalt der Bahn Asphalt und nicht der Mittelwert aus
           fuenf Metern Wiese ringsum. */
        if (is_airport_tile(land[nland].name, NULL)) terrain_drop_texture(&land[nland]);
        coarse_of[nland] = -1;
        int base = nland++;
        /* und die grobe Fassung dazu, wenn es sie gibt */
        char lod[300];
        int stem = (int)strlen(cand[i].path) - 4;
        snprintf(lod, sizeof(lod), "%.*s.lod.fgb", stem, cand[i].path);
        if (nland < MAX_TILES && terrain_load(&land[nland], lod, &land_frame)) {
            if (is_airport_tile(land[nland].name, NULL)) terrain_drop_texture(&land[nland]);
            coarse_of[base] = nland;
            coarse_of[nland] = -1;
            is_coarse[nland] = 1;
            ++nland;
        }
    }
    printf("Gelaende: %d von %d Kacheln geladen, Ausgangspunkt %s\n",
           limit, n, strrchr(cand[0].path, '/') + 1);
}

/* --- Flugplatzliste ------------------------------------------------------
 *
 * 23 529 Plaetze in 1,3 MB: zu viel, um sie im Speicher zu halten, aber wenig
 * genug, um die Datei fuer jede Seite einmal zu lesen.  Gefiltert wird nach
 * Art (Gross, Klein, Platz) und nach den ersten Buchstaben der Kennung - zwei
 * Buchstaben genuegen, um von 23 000 auf ein paar hundert zu kommen.
 */
static int apt_has_country;             /* hat die Datei die Laenderspalte? */

static const char *airports_path(void) {
    static const char *paths[] = { "/opt/fgfly/share/airports.txt", "airports.txt",
                                   "/home/user/airports.txt" };
    static const char *found;
    if (!found) {
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            FILE *f = fopen(paths[i], "r");
            if (!f) continue;
            /* Die Laenderspalte kam spaeter dazu, und an der Zeile selbst ist
               sie nicht zu erkennen - auch die Namen stehen in
               Grossbuchstaben.  Also sagt es der Kopf der Datei. */
            char line[224];
            while (fgets(line, sizeof(line), f) && line[0] == '#')
                if (strstr(line, "Bahnrichtungen Land")) apt_has_country = 1;
            fclose(f);
            found = paths[i];
            break;
        }
    }
    return found;
}

static int parse_airport(const char *line, struct airport *a) {
    char rwys[64] = "", country[24] = "", name[40] = "";
    int got = sscanf(line, "%4s %lf %lf %d %d %d %63s %23s %39[^\n]",
                     a->icao, &a->lat, &a->lon, &a->elev_ft, &a->cat, &a->length,
                     rwys, country, name);
    if (got < 7) return 0;
    if (got >= 8 && apt_has_country) {
        snprintf(a->country, sizeof(a->country), "%s", country);
        snprintf(a->name, sizeof(a->name), "%s", got >= 9 ? name : "");
    } else {                            /* alte Datei: dort steht der Name, und
                                           das erste Wort davon steckt schon in
                                           `country` - wieder zusammensetzen */
        a->country[0] = 0;
        snprintf(a->name, sizeof(a->name), "%s%s%s", got >= 8 ? country : "",
                 got >= 9 ? " " : "", got >= 9 ? name : "");
    }
    a->nrwy = 0;
    for (char *p = rwys; *p && a->nrwy < 8; ) {
        a->rwy[a->nrwy++] = atoi(p);
        char *comma = strchr(p, ',');
        if (!comma) break;
        p = comma + 1;
    }
    return 1;
}

/* Fuellt apt_page mit der Seite ab apt_offset und zaehlt alle Treffer. */
static void apt_scan(void) {
    apt_page_n = 0;
    apt_total = 0;
    const char *path = airports_path();
    if (!path) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t plen = strlen(apt_prefix);
    char line[224];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        struct airport a;
        if (!parse_airport(line, &a)) continue;
        if (a.cat != apt_cat) continue;
        if (plen && strncmp(a.icao, apt_prefix, plen)) continue;
        if (apt_country[0] && strcmp(a.country, apt_country)) continue;
        if (apt_total >= apt_offset && apt_page_n < 7) apt_page[apt_page_n++] = a;
        ++apt_total;
    }
    fclose(f);
}

/* Die Laender mit diesem Anfangsbuchstaben, die ueberhaupt Plaetze dieser Art
   haben - einmal durch die Datei, dieselbe Runde wie apt_scan. */
static void cland_scan(void) {
    n_cland = 0;
    const char *path = airports_path();
    if (!path) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[224];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        struct airport a;
        if (!parse_airport(line, &a)) continue;
        if (a.cat != apt_cat || a.country[0] != cland_initial) continue;
        int seen = 0;
        for (int i = 0; i < n_cland && !seen; ++i) seen = !strcmp(cland[i], a.country);
        if (!seen && n_cland < (int)(sizeof(cland) / sizeof(cland[0])))
            snprintf(cland[n_cland++], sizeof(cland[0]), "%s", a.country);
    }
    fclose(f);
    for (int i = 1; i < n_cland; ++i) {          /* klein genug fuer Einfuegen */
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%s", cland[i]);
        int j = i - 1;
        while (j >= 0 && strcmp(cland[j], tmp) > 0) {
            snprintf(cland[j + 1], sizeof(cland[0]), "%s", cland[j]);
            --j;
        }
        snprintf(cland[j + 1], sizeof(cland[0]), "%s", tmp);
    }
}

/* Unterstriche sind im Land nur die Leerzeichen der Datei. */
static const char *pretty(const char *word) {
    static char buf[24];
    snprintf(buf, sizeof(buf), "%s", word);
    for (char *p = buf; *p; ++p) if (*p == '_') *p = ' ';
    return buf;
}

/* --- Hangar --------------------------------------------------------------
 *
 * 648 Flugzeuge in 66 KB, aus FlightGears Katalog.  Gefiltert wird nach dem
 * ersten Buchstaben der Kennung; geladen wird erst, wenn eines gewaehlt ist.
 */
static const char *catalog_path(void) {
    static const char *paths[] = { "/opt/fgfly/share/aircraft.txt", "aircraft.txt",
                                   "/home/user/aircraft.txt" };
    static const char *found;
    if (!found)
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            FILE *f = fopen(paths[i], "r");
            if (f) { fclose(f); found = paths[i]; break; }
        }
    return found;
}

static void cat_scan(void) {
    cat_page_n = 0;
    cat_total = 0;
    const char *path = catalog_path();
    if (!path) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t plen = strlen(apt_prefix);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        struct catalog_entry e;
        if (sscanf(line, "%27s %d %27s %159s", e.id, &e.mb, e.name, e.url) != 4) continue;
        if (plen) {
            char head[4] = { (char)toupper((unsigned char)e.id[0]), 0, 0, 0 };
            if (head[0] != apt_prefix[0]) continue;
        }
        for (char *q = e.name; *q; ++q) if (*q == '_') *q = ' ';
        if (cat_total >= cat_offset && cat_page_n < 7) cat_page[cat_page_n++] = e;
        ++cat_total;
    }
    fclose(f);
}

static int aircraft_installed(const char *id) {
    for (int i = 0; i < n_aircraft; ++i)
        if (!strcasecmp(aircraft[i].name, id)) { sel_aircraft = i; return 1; }
    return 0;
}

/* Steht die Kachel, auf der wir anfangen, fuer einen Flugplatz, dann fangen
   wir auch auf seiner Bahn an: Ort und Richtung stehen in airports.txt, das
   aus FlightGears apt.dat kommt (23 529 Plaetze, 854 KB). */
static int start_on_runway(const char *tile_name) {
    char icao[8];
    if (!is_airport_tile(tile_name, icao)) return 0;

    const char *path = airports_path();     /* liest auch den Kopf der Datei */
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return 0;

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        struct airport a;
        if (!parse_airport(line, &a)) continue;
        if (strcmp(a.icao, icao)) continue;
        /* Die Bahnrichtungen stehen in der siebenten Spalte, nicht in der
           fuenften - dort steht die Art des Platzes.  Vorher zeigte das
           Flugzeug deshalb nach 1 Grad statt die Bahn entlang. */
        double lat = a.lat, lon = a.lon;
        int elev_ft = a.elev_ft, length = a.length;
        int hdg = a.nrwy > 0 ? a.rwy[0] : 0;
        float local[3];
        terrain_frame_local(&land_frame, lat, lon, elev_ft * 0.3048, local);
        fdm.east_m = local[0];
        fdm.north_m = local[1];
        fdm.ground_m = local[2];
        fdm.alt_m = local[2];
        fdm.heading_deg = (float)hdg;
        printf("Start auf %s, Bahn %02d, %.0f m lang\n", a.icao, hdg / 10, (double)length);
        found = 1;
        break;
    }
    fclose(f);
    return found;
}

/* Liegt fuer diese Kennung schon eine gebackene Kachel da? */
static int scenery_has(const char *icao) {
    for (int i = 0; i < n_scenery; ++i) {
        char path[320];
        snprintf(path, sizeof(path), "%s/%s.fgb", scenery[i].path, icao);
        if (access(path, R_OK) == 0) { sel_scenery = i; return 1; }
    }
    return 0;
}

/* Szenerie holen und backen, waehrend die Anzeige weiterlaeuft: das Skript
   laeuft als eigener Vorgang und schreibt seinen Stand in eine Datei. */
#define FETCH_NOTE "/home/user/MyDocs/fgfly-fetch.txt"
static pid_t fetch_pid;

static void begin_fetch(const struct airport *a) {
    unlink(FETCH_NOTE);
    snprintf(fetch_note, sizeof(fetch_note), "LADE SZENERIE");
    char lat[24], lon[24];
    snprintf(lat, sizeof(lat), "%.5f", a->lat);
    snprintf(lon, sizeof(lon), "%.5f", a->lon);
    fetch_pid = fork();
    if (fetch_pid == 0) {
        setpgid(0, 0);                  /* eigene Gruppe: ABBRECHEN trifft alle */
        execl("/bin/sh", "sh", "/opt/fgfly/bin/fgfly-fetch.sh", a->icao, lat, lon, (char *)NULL);
        execl("/bin/sh", "sh", "./fgfly-fetch.sh", a->icao, lat, lon, (char *)NULL);
        _exit(127);
    }
    page = S_FETCH;
}

static void begin_aircraft_fetch(const struct catalog_entry *e) {
    unlink(FETCH_NOTE);
    snprintf(fetch_note, sizeof(fetch_note), "LADE %s", e->id);
    snprintf(apt_chosen.icao, sizeof(apt_chosen.icao), "%s", "");
    fetch_pid = fork();
    if (fetch_pid == 0) {
        setpgid(0, 0);
        execl("/bin/sh", "sh", "/opt/fgfly/bin/fgfly-aircraft.sh", e->id, e->url, (char *)NULL);
        execl("/bin/sh", "sh", "./fgfly-aircraft.sh", e->id, e->url, (char *)NULL);
        _exit(127);
    }
    page = S_FETCH;
}

/* Was der Startbildschirm ausgewaehlt hat, wird jetzt wirklich geladen. */
static void load_world(void) {
    if (n_aircraft > 0) {
        if (fdm_load(&aircraft_data, aircraft[sel_aircraft].path))
            printf("Flugzeug: %s\n", aircraft_data.name);
        char mpath[288];
        int stem = (int)strlen(aircraft[sel_aircraft].path) - 4;
        snprintf(mpath, sizeof(mpath), "%.*s.model.fgb", stem, aircraft[sel_aircraft].path);
        struct terrain_frame none;
        memset(&none, 0, sizeof(none));
        none.set = 1;
        have_model = terrain_load(&model, mpath, &none);
    }
    if (n_scenery > 0 && nland == 0)
        load_terrain_dir(scenery[sel_scenery].path, have_chosen ? apt_chosen.icao : NULL);
    have_terrain = nland > 0;
    fdm_init(&fdm, acft);
    /* Der Boden liegt dort, wo die Kachel ihn hat - bei LOWW rund 180 m.
       Ohne das startet man unter der Landschaft. */
    float ground = nland > 0 ? land[0].centre_height : 0.0f;
    fdm.ground_m = ground;
    fdm.alt_m = ground;
    /* Auf einem Flugplatz beginnt man auf der Bahn, nicht in der Wiese.  Wurde
       einer ausgewaehlt, gilt dessen Bahn; sonst die der Ausgangskachel. */
    int on_runway = 0;
    if (have_chosen) {
        float local[3];
        terrain_frame_local(&land_frame, apt_chosen.lat, apt_chosen.lon,
                            apt_chosen.elev_ft * 0.3048, local);
        fdm.east_m = local[0];
        fdm.north_m = local[1];
        fdm.ground_m = local[2];
        fdm.alt_m = local[2];
        fdm.heading_deg = (float)apt_chosen.rwy[apt_rwy < apt_chosen.nrwy ? apt_rwy : 0];
        printf("Start auf %s, Bahn %02d\n", apt_chosen.icao,
               (int)fdm.heading_deg / 10);
        on_runway = 1;
    } else {
        on_runway = nland > 0 ? start_on_runway(land[0].name) : 0;
    }
    if (on_runway) ground = fdm.ground_m;
    if (start_airborne) {
        fdm.alt_m = ground + 600.0f;
        fdm.v_ms = 50.0f;
        fdm.on_ground = 0;
        brake = 0;
        throttle = 0.75f;               /* der Motor laeuft, sonst faellt man */
        fdm.engine_on = 1;
        fdm.rpm = acft->rpm_idle + 0.75f * (acft->rpm_max - acft->rpm_idle);
    } else {
        throttle = 0.0f;
        brake = 1;
        /* Am Boden faengt man mit stehendem Motor an und drueckt START -
           so ist auch zu sehen, dass er laeuft. */
        fdm.engine_on = 0;
        fdm.rpm = 0.0f;
    }
}

/* --- Startbildschirm ------------------------------------------------------ */

static const float C_BACK[4] = {0.08f, 0.10f, 0.14f, 1.0f};
static const float C_TEXT[4] = {0.95f, 0.95f, 0.95f, 1.0f};
static const float C_GREY[4] = {0.55f, 0.57f, 0.60f, 1.0f};
static const float C_MARK[4] = {0.20f, 0.35f, 0.55f, 1.0f};
static const float C_GO[4]   = {0.30f, 0.65f, 0.35f, 1.0f};

static const char *CAT_NAME[3] = { "GROSSFLUGHAFEN", "KLEINFLUGHAFEN", "FLUGPLATZ" };

static void start_flight(void);
static int scenery_has(const char *icao);
static void begin_fetch(const struct airport *a);
static void begin_aircraft_fetch(const struct catalog_entry *e);

static void build_main(void) {
    text(0.06f, 0.04f, 0.075f, "FLUGZEUG", C_GREY);
    for (int i = 0; i < n_aircraft && i < 4; ++i) {
        float y = 0.16f + i * 0.095f;
        if (i == sel_aircraft) rect(0.04f, y - 0.012f, 0.42f, 0.085f, C_MARK);
        text(0.06f, y, 0.058f, aircraft[i].name, i == sel_aircraft ? C_TEXT : C_GREY);
    }
    if (n_aircraft == 0) text(0.06f, 0.16f, 0.058f, "KEINE .FDM", C_GREY);
    rect(0.04f, 0.555f, 0.42f, 0.085f, C_MARK);
    text(0.06f, 0.567f, 0.052f, "AUS DEM HANGAR", C_TEXT);

    text(0.52f, 0.04f, 0.075f, "WOHIN", C_GREY);
    for (int i = 0; i < 3; ++i) {
        float y = 0.16f + i * 0.095f;
        rect(0.50f, y - 0.012f, 0.46f, 0.085f, C_MARK);
        text(0.52f, y, 0.052f, CAT_NAME[i], C_TEXT);
    }
    if (n_scenery > 0) {
        rect(0.50f, 0.445f, 0.46f, 0.085f, C_MARK);
        text(0.52f, 0.457f, 0.052f, "SCHON GEBACKEN", C_TEXT);
    }

    rect(0.04f, 0.845f, 0.40f, 0.10f, C_MARK);
    text(0.07f, 0.868f, 0.055f, start_airborne ? "IN DER LUFT" : "AM BODEN", C_TEXT);
}

static void build_letter(void) {
    char title[64];
    if (browse_mode)
        snprintf(title, sizeof(title), "HANGAR %s", apt_prefix);
    else if (by_country)
        snprintf(title, sizeof(title), "%s - LAND", CAT_NAME[apt_cat]);
    else
        snprintf(title, sizeof(title), "%s - KENNUNG %s", CAT_NAME[apt_cat], apt_prefix);
    text(0.04f, 0.04f, 0.06f, title, C_GREY);
    /* Der Umschalter: nach dem Anfangsbuchstaben des Landes oder nach dem der
       Kennung.  Im Hangar gibt es nur Kennungen. */
    if (!browse_mode) {
        rect(0.62f, 0.02f, 0.34f, 0.10f, C_MARK);
        text(0.645f, 0.042f, 0.052f, by_country ? "NACH LAND" : "NACH KENNUNG", C_TEXT);
    }
    for (int i = 0; i < 26; ++i) {
        int col = i % 7, row = i / 7;
        float x = 0.04f + col * 0.135f, y = 0.17f + row * 0.19f;
        char letter[2] = { (char)('A' + i), 0 };
        rect(x, y, 0.115f, 0.15f, C_MARK);
        text(x + 0.035f, y + 0.035f, 0.08f, letter, C_TEXT);
    }
    rect(0.72f, 0.74f, 0.24f, 0.15f, C_GREY);
    text(0.78f, 0.785f, 0.06f, "ZURUECK", C_BACK);
}

static void build_catalog(void) {
    char title[64];
    snprintf(title, sizeof(title), "HANGAR %s  %d-%d VON %d", apt_prefix,
             cat_total ? cat_offset + 1 : 0, cat_offset + cat_page_n, cat_total);
    text(0.04f, 0.03f, 0.055f, title, C_GREY);
    for (int i = 0; i < cat_page_n; ++i) {
        float y = 0.13f + i * 0.105f;
        int have = aircraft_installed(cat_page[i].id);
        rect(0.03f, y - 0.010f, 0.94f, 0.095f, have ? C_GO : C_MARK);
        text(0.05f, y + 0.006f, 0.050f, cat_page[i].name, C_TEXT);
        char mb[12];
        snprintf(mb, sizeof(mb), "%d MB", cat_page[i].mb);
        text(0.84f, y + 0.008f, 0.048f, mb, C_GREY);
    }
    rect(0.03f, 0.90f, 0.20f, 0.085f, C_GREY);
    text(0.07f, 0.918f, 0.055f, "ZURUECK", C_BACK);
    rect(0.40f, 0.90f, 0.13f, 0.085f, C_MARK);
    text(0.455f, 0.918f, 0.055f, "-", C_TEXT);
    rect(0.56f, 0.90f, 0.13f, 0.085f, C_MARK);
    text(0.615f, 0.918f, 0.055f, "/", C_TEXT);
}

static void build_country(void) {
    char title[64];
    int shown = n_cland - cland_offset;
    if (shown > 7) shown = 7;
    snprintf(title, sizeof(title), "%c  %d-%d VON %d", cland_initial,
             n_cland ? cland_offset + 1 : 0, cland_offset + shown, n_cland);
    text(0.04f, 0.03f, 0.055f, title, C_GREY);
    for (int i = 0; i < shown; ++i) {
        float y = 0.13f + i * 0.105f;
        rect(0.03f, y - 0.010f, 0.94f, 0.095f, C_MARK);
        text(0.05f, y, 0.058f, pretty(cland[cland_offset + i]), C_TEXT);
    }
    if (!n_cland) text(0.05f, 0.14f, 0.058f, "KEIN LAND MIT DIESEM BUCHSTABEN", C_GREY);
    rect(0.03f, 0.90f, 0.20f, 0.085f, C_GREY);
    text(0.07f, 0.918f, 0.055f, "ZURUECK", C_BACK);
    rect(0.40f, 0.90f, 0.13f, 0.085f, C_MARK);
    text(0.455f, 0.918f, 0.055f, "-", C_TEXT);
    rect(0.56f, 0.90f, 0.13f, 0.085f, C_MARK);
    text(0.615f, 0.918f, 0.055f, "/", C_TEXT);
}

/* Ein Tipp in der Liste hat bisher sofort geladen - und in einer Liste aus
   sieben Zeilen trifft man leicht die falsche.  Also erst fragen, und dabei
   sagen, wie gross das wird. */
static void build_acft(void) {
    int have = aircraft_installed(cat_chosen.id);
    text(0.04f, 0.06f, 0.070f, cat_chosen.name, C_TEXT);
    char sub[64];
    snprintf(sub, sizeof(sub), "KENNUNG %s", cat_chosen.id);
    text(0.04f, 0.20f, 0.050f, sub, C_GREY);
    if (have) {
        text(0.04f, 0.33f, 0.060f, "SCHON DA", C_GO);
    } else {
        snprintf(sub, sizeof(sub), "%d MB AUS DEM HANGAR", cat_chosen.mb);
        text(0.04f, 0.33f, 0.060f, sub, C_TEXT);
        text(0.04f, 0.45f, 0.044f, "BESSER UEBER WLAN - DAS PAKET WIRD", C_GREY);
        text(0.04f, 0.52f, 0.044f, "NACH DEM UMSETZEN WIEDER GELOESCHT", C_GREY);
    }
    rect(0.03f, 0.84f, 0.30f, 0.12f, C_GREY);
    text(0.07f, 0.875f, 0.058f, "ZURUECK", C_BACK);
    rect(0.52f, 0.84f, 0.44f, 0.12f, C_GO);
    text(0.60f, 0.875f, 0.058f, have ? "NEHMEN" : "LADEN", C_TEXT);
}

static void build_list(void) {
    if (browse_mode) { build_catalog(); return; }
    char title[64];
    snprintf(title, sizeof(title), "%s  %d-%d VON %d",
             apt_country[0] ? pretty(apt_country) : apt_prefix,
             apt_total ? apt_offset + 1 : 0, apt_offset + apt_page_n, apt_total);
    text(0.04f, 0.03f, 0.055f, title, C_GREY);
    for (int i = 0; i < apt_page_n; ++i) {
        float y = 0.13f + i * 0.105f;
        rect(0.03f, y - 0.010f, 0.94f, 0.095f, C_MARK);
        text(0.05f, y, 0.062f, apt_page[i].icao, C_TEXT);
        text(0.20f, y + 0.008f, 0.050f, apt_page[i].name, C_GREY);
        char len[16];
        snprintf(len, sizeof(len), "%d M", apt_page[i].length);
        text(0.83f, y + 0.008f, 0.050f, len, C_GREY);
    }
    rect(0.03f, 0.90f, 0.20f, 0.085f, C_GREY);
    text(0.07f, 0.918f, 0.055f, "ZURUECK", C_BACK);
    rect(0.40f, 0.90f, 0.13f, 0.085f, C_MARK);
    text(0.455f, 0.918f, 0.055f, "-", C_TEXT);
    rect(0.56f, 0.90f, 0.13f, 0.085f, C_MARK);
    text(0.615f, 0.918f, 0.055f, "/", C_TEXT);
}

static void build_rwy(void) {
    char title[64];
    snprintf(title, sizeof(title), "%s %s", apt_chosen.icao, apt_chosen.name);
    text(0.04f, 0.04f, 0.065f, title, C_TEXT);
    char sub[48];
    snprintf(sub, sizeof(sub), "%d M BAHN  %d FT", apt_chosen.length, apt_chosen.elev_ft);
    text(0.04f, 0.14f, 0.048f, sub, C_GREY);
    text(0.04f, 0.25f, 0.050f, "BAHN WAEHLEN", C_GREY);
    for (int i = 0; i < apt_chosen.nrwy; ++i) {
        int col = i % 4, row = i / 4;
        float x = 0.05f + col * 0.23f, y = 0.34f + row * 0.17f;
        char label[8];
        snprintf(label, sizeof(label), "%02d", apt_chosen.rwy[i] / 10);
        rect(x, y, 0.19f, 0.13f, i == apt_rwy ? C_GO : C_MARK);
        text(x + 0.05f, y + 0.03f, 0.075f, label, C_TEXT);
    }
    rect(0.03f, 0.86f, 0.22f, 0.10f, C_GREY);
    text(0.06f, 0.885f, 0.055f, "ZURUECK", C_BACK);
    rect(0.56f, 0.86f, 0.40f, 0.10f, C_GO);
    text(0.66f, 0.885f, 0.055f, "START", C_TEXT);
}

static void build_fetch(void) {
    text(0.06f, 0.28f, 0.075f, browse_mode ? cat_chosen.id : apt_chosen.icao, C_TEXT);
    text(0.06f, 0.42f, 0.050f, fetch_note[0] ? fetch_note : "LADE UND BACKE", C_GREY);
    text(0.06f, 0.54f, 0.042f, "DAS DAUERT EIN PAAR MINUTEN", C_GREY);
    /* Wer sich vertippt hat, soll nicht zusehen muessen, wie 258 MB
       durchlaufen. */
    rect(0.03f, 0.84f, 0.34f, 0.12f, C_GREY);
    text(0.07f, 0.875f, 0.058f, "ABBRECHEN", C_BACK);
}

/* Bricht den laufenden Holer ab - die ganze Prozessgruppe, sonst laedt das
   Python im Skript munter weiter.  Das Skript raeumt in seinem trap auf. */
static void cancel_fetch(void) {
    if (fetch_pid > 0) {
        kill(-fetch_pid, SIGTERM);
        kill(fetch_pid, SIGTERM);
        /* Nicht warten: das Skript raeumt in seinem trap ein ausgepacktes
           Paket weg, und ein `rm -rf` ueber hunderte Megabyte auf vfat
           dauert - so lange stuende das Bild still. */
        waitpid(fetch_pid, NULL, WNOHANG);
        fetch_pid = 0;
    }
    snprintf(fetch_note, sizeof(fetch_note), "%s", "");
    unlink(FETCH_NOTE);
}

static void build_start(void) {
    nverts = 0;
    rect(0.0f, 0.0f, 1.0f, 1.0f, C_BACK);
    switch (page) {
    case S_MAIN:   build_main();   break;
    case S_LETTER: build_letter(); break;
    case S_COUNTRY: build_country(); break;
    case S_ACFT:   build_acft();   break;
    case S_LIST:   build_list();   break;
    case S_RWY:    build_rwy();    break;
    case S_FETCH:  build_fetch();  break;
    default: break;
    }
}

static void touch_main(float x, float y) {
    if (y > 0.14f && y < 0.55f) {
        int row = (int)((y - 0.15f) / 0.095f);
        if (x < 0.48f) {
            if (row >= 0 && row < n_aircraft) sel_aircraft = row;
        } else if (row >= 0 && row < 3) {
            apt_cat = row;
            apt_prefix[0] = 0;
            apt_country[0] = 0;
            browse_mode = 0;
            page = S_LETTER;
        } else if (row == 3 && n_scenery > 0) {
            have_chosen = 0;
            start_flight();
        }
        return;
    }
    if (y > 0.54f && y < 0.65f && x < 0.48f) {     /* aus dem Hangar */
        browse_mode = 1;
        apt_prefix[0] = 0;
        page = S_LETTER;
        return;
    }
    if (y > 0.83f && x < 0.45f) start_airborne = !start_airborne;
}

static void touch_letter(float x, float y) {
    if (!browse_mode && y < 0.14f && x > 0.60f) {   /* Land oder Kennung */
        by_country = !by_country;
        apt_prefix[0] = 0;
        apt_country[0] = 0;
        return;
    }
    if (y > 0.72f && x > 0.70f) {
        size_t n = strlen(apt_prefix);
        if (n > 0) apt_prefix[n - 1] = 0;
        else page = S_MAIN;
        return;
    }
    int col = (int)((x - 0.04f) / 0.135f), row = (int)((y - 0.17f) / 0.19f);
    if (col < 0 || col > 6 || row < 0 || row > 3) return;
    int i = row * 7 + col;
    if (i < 0 || i > 25) return;
    if (!browse_mode && by_country) {   /* der Buchstabe ist der des Landes */
        cland_initial = (char)('A' + i);
        cland_offset = 0;
        apt_country[0] = 0;
        cland_scan();
        page = S_COUNTRY;
        return;
    }
    size_t n = strlen(apt_prefix);
    if (n < 2) {
        apt_prefix[n] = (char)('A' + i);
        apt_prefix[n + 1] = 0;
    }
    if (browse_mode) {                  /* ein Buchstabe genuegt bei 648 */
        cat_offset = 0;
        cat_scan();
        page = S_LIST;
    } else if (strlen(apt_prefix) == 2) {
        apt_offset = 0;
        apt_scan();
        page = S_LIST;
    }
}

/* Zwischen zwei Zeilen liegt ein schmaler toter Streifen: wer die Kante
   trifft, trifft lieber nichts als die Nachbarzeile. */
static int list_row(float y, int n)
{
    int row = (int)((y - 0.12f) / 0.105f);
    if (row < 0 || row >= n) return -1;
    if (y - (0.12f + row * 0.105f) > 0.085f) return -1;
    return row;
}

static void touch_catalog(float x, float y) {
    if (y > 0.89f) {
        if (x < 0.25f) { page = S_LETTER; return; }
        if (x > 0.38f && x < 0.55f && cat_offset >= 7) { cat_offset -= 7; cat_scan(); }
        else if (x > 0.55f && x < 0.70f && cat_offset + 7 < cat_total) { cat_offset += 7; cat_scan(); }
        return;
    }
    int row = list_row(y, cat_page_n);
    if (row < 0) return;
    cat_chosen = cat_page[row];
    page = S_ACFT;                      /* erst fragen, dann laden */
}

static void touch_acft(float x, float y) {
    if (y < 0.82f) return;
    if (x < 0.36f) { page = S_LIST; return; }
    if (x > 0.50f) {
        if (aircraft_installed(cat_chosen.id)) page = S_MAIN;
        else begin_aircraft_fetch(&cat_chosen);
    }
}

static void touch_country(float x, float y) {
    if (y > 0.88f) {
        if (x < 0.25f) { page = S_LETTER; return; }
        if (x > 0.38f && x < 0.55f && cland_offset >= 7) cland_offset -= 7;
        else if (x > 0.55f && x < 0.70f && cland_offset + 7 < n_cland) cland_offset += 7;
        return;
    }
    int n = n_cland - cland_offset;
    int row = list_row(y, n > 7 ? 7 : n);
    if (row < 0) return;
    snprintf(apt_country, sizeof(apt_country), "%s", cland[cland_offset + row]);
    apt_prefix[0] = 0;
    apt_offset = 0;
    apt_scan();
    page = S_LIST;
}

static void touch_list(float x, float y) {
    if (browse_mode) { touch_catalog(x, y); return; }
    if (y > 0.89f) {
        if (x < 0.25f) { page = apt_country[0] ? S_COUNTRY : S_LETTER; return; }
        if (x > 0.38f && x < 0.55f && apt_offset >= 7) { apt_offset -= 7; apt_scan(); }
        else if (x > 0.55f && x < 0.70f && apt_offset + 7 < apt_total) { apt_offset += 7; apt_scan(); }
        return;
    }
    int row = list_row(y, apt_page_n);
    if (row >= 0) {
        apt_chosen = apt_page[row];
        apt_rwy = 0;
        have_chosen = 1;
        page = S_RWY;
    }
}

static void touch_rwy(float x, float y) {
    if (y > 0.85f) {
        if (x < 0.27f) { page = S_LIST; return; }
        if (x > 0.55f) {
            if (scenery_has(apt_chosen.icao)) start_flight();
            else begin_fetch(&apt_chosen);
        }
        return;
    }
    int col = (int)((x - 0.05f) / 0.23f), row = (int)((y - 0.34f) / 0.17f);
    int i = row * 4 + col;
    if (col >= 0 && col < 4 && row >= 0 && i < apt_chosen.nrwy) apt_rwy = i;
}

static void touch_start(float x, float y) {
    switch (page) {
    case S_MAIN:   touch_main(x, y);   break;
    case S_LETTER: touch_letter(x, y); break;
    case S_COUNTRY: touch_country(x, y); break;
    case S_ACFT:   touch_acft(x, y);   break;
    case S_LIST:   touch_list(x, y);   break;
    case S_RWY:    touch_rwy(x, y);    break;
    case S_FETCH:
        if (y > 0.82f && x < 0.40f) {   /* ABBRECHEN */
            cancel_fetch();
            page = browse_mode ? S_LIST : S_RWY;
        }
        break;
    default: break;
    }
}

/* Erst laden, dann losfliegen. */
static void start_flight(void) {
    load_world();
    screen_start = 0;
}

/* `first` ist 1, wenn der Finger in diesem Bild aufgesetzt hat.  Die Schalter
   - Bremse, Fahrwerk, Ansicht, das Kreuz - duerfen nur dann umspringen: sonst
   kippen sie, solange der Finger liegt, in jedem Bild einmal um (bei 30 Bildern
   je Sekunde also dreissigmal), und was am Ende herauskommt, ist Zufall.  Das
   war es, was die Bremse nicht loesbar und die Ansicht nicht waehlbar machte.
   Die Schieber - Schub, Klappen, Seitenruder - folgen weiter jedem Bild. */
/* Liegt der Punkt auf einem Bedienteil?  Genau die Rechtecke, die auch
   gezeichnet werden.  Frueher stand dafuer in der Gestenentscheidung ein
   grober Rahmen (die aeusseren 17 Prozent ringsum), und alles darin galt als
   Bedienung - in der Verfolgersicht blieb zum Drehen nur die Bildmitte. */
static int on_widget(float x, float y) {
    if (y < 0.115f && x > 0.90f) return 1;                  /* das Kreuz */
    if (y < 0.13f && x > 0.12f && x < 0.29f) return 1;      /* Ansicht */
    if (y < 0.13f && x > 0.29f && x < 0.45f) return 1;      /* Anlasser */
    if (x < 0.16f && y > 0.13f && y < 0.87f) return 1;      /* Schubhebel */
    if (x > 0.84f && y > 0.13f && y < 0.87f) return 1;      /* Klappen */
    if (y > 0.85f && x < 0.15f) return 1;                   /* Fahrwerk */
    if (y > 0.87f && x > 0.74f && x < 0.88f) return 1;      /* Bremse */
    if (y > 0.90f && x > 0.20f && x < 0.64f) return 1;      /* Seitenruder */
    return 0;
}

static void touch(int px, int py, int width, int height, int first) {
    float x = (float)px / width, y = (float)py / height;
    if (y < 0.115f && x > 0.90f) { if (first) quit_now = 1; return; }
    if (y > 0.90f && x > 0.20f && x < 0.64f) {      /* Seitenruder */
        rudder = (x - 0.22f) / 0.36f * 2.0f - 1.0f;
        if (rudder < -1.0f) rudder = -1.0f;
        if (rudder > 1.0f) rudder = 1.0f;
        return;
    }
    if (y > 0.85f && x < 0.15f) { if (first) gear_down = !gear_down; return; }
    if (x < 0.16f) throttle = 1.0f - (y - 0.15f) / 0.70f;
    else if (x > 0.84f) flaps = 1.0f - (y - 0.15f) / 0.70f;
    else if (y > 0.87f && x > 0.74f && x < 0.88f) { if (first) brake = !brake; }
    else if (y < 0.13f && x > 0.12f && x < 0.29f) {
        if (first) view_mode = (view_mode + 1) % 3;
    }
    else if (y < 0.13f && x > 0.29f && x < 0.45f) {
        if (first) fdm.engine_on = !fdm.engine_on;
    }
    if (throttle < 0.0f) throttle = 0.0f;
    if (throttle > 1.0f) throttle = 1.0f;
    if (flaps < 0.0f) flaps = 0.0f;
    if (flaps > 1.0f) flaps = 1.0f;
}

static void write_ppm(const char *path, int w, int h) {
    unsigned char *px = malloc((size_t)w * h * 4);
    if (!px) return;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y)            /* GL zaehlt von unten */
            for (int x = 0; x < w; ++x) {
                unsigned char *p = px + ((size_t)y * w + x) * 4;
                fwrite(p, 1, 3, f);
            }
        fclose(f);
        printf("Bild: %s\n", path);
    }
    free(px);
}

static double now_s(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    double seconds = argc > 1 ? atof(argv[1]) : 0.0;

    Display *xdpy = XOpenDisplay(NULL);
    if (!xdpy) { fprintf(stderr, "kein X-Display (DISPLAY=:0 setzen)\n"); return 1; }
    int screen = DefaultScreen(xdpy);
    int width = DisplayWidth(xdpy, screen), height = DisplayHeight(xdpy, screen);
    aspect = (float)width / height;

    /* Erst die Konfiguration, dann das Fenster: die Fensterflaeche muss den
     * Sichttyp haben, den EGL der Konfiguration zuordnet, sonst weist
     * eglCreateWindowSurface sie ab. */
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)xdpy);
    if (!eglInitialize(dpy, NULL, NULL)) {
        fprintf(stderr, "eglInitialize: %#x\n", eglGetError());
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "keine ES2-Fensterkonfiguration: %#x\n", eglGetError());
        return 1;
    }
    EGLint native_id = 0;
    eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &native_id);

    XVisualInfo want, *vi = NULL;
    int nvi = 0;
    memset(&want, 0, sizeof(want));
    want.visualid = (VisualID)native_id;
    if (native_id) vi = XGetVisualInfo(xdpy, VisualIDMask, &want, &nvi);
    if (!vi || nvi < 1) {                       /* dann eben der Sichttyp der Wurzel */
        memset(&want, 0, sizeof(want));
        want.visualid = XVisualIDFromVisual(DefaultVisual(xdpy, screen));
        vi = XGetVisualInfo(xdpy, VisualIDMask, &want, &nvi);
    }
    if (!vi || nvi < 1) { fprintf(stderr, "kein passender X-Sichttyp\n"); return 1; }
    printf("Sichttyp %#lx, Tiefe %d\n", (unsigned long)vi->visualid, vi->depth);

    /* Kein override_redirect mehr: der Randwisch, die Aufgabenansicht und das
     * Schliessen von dort sind alles Sachen des Fenstermanagers, und der sieht
     * ein Fenster, das an ihm vorbei angelegt wird, ueberhaupt nicht.  Also ein
     * gewoehnliches Fenster, das sich nur ueber _NET_WM_STATE_FULLSCREEN den
     * ganzen Schirm nimmt - dann verhaelt sich die App wie die anderen. */
    XSetWindowAttributes attr;
    memset(&attr, 0, sizeof(attr));
    attr.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask
                    | StructureNotifyMask | VisibilityChangeMask | FocusChangeMask;
    attr.colormap = XCreateColormap(xdpy, RootWindow(xdpy, screen), vi->visual, AllocNone);
    attr.border_pixel = 0;
    Window win = XCreateWindow(xdpy, RootWindow(xdpy, screen), 0, 0, width, height, 0,
                               vi->depth, InputOutput, vi->visual,
                               CWEventMask | CWColormap | CWBorderPixel,
                               &attr);

    Atom a_wintype = XInternAtom(xdpy, "_NET_WM_WINDOW_TYPE", False);
    Atom a_normal  = XInternAtom(xdpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    Atom a_state   = XInternAtom(xdpy, "_NET_WM_STATE", False);
    Atom a_fullscr = XInternAtom(xdpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom a_protos  = XInternAtom(xdpy, "WM_PROTOCOLS", False);
    Atom a_delete  = XInternAtom(xdpy, "WM_DELETE_WINDOW", False);
    Atom a_netpid  = XInternAtom(xdpy, "_NET_WM_PID", False);
    Atom a_angle   = XInternAtom(xdpy, "_MEEGOTOUCH_ORIENTATION_ANGLE", False);
    /* Beide Eigenschaften muessen stehen, bevor das Fenster auftaucht: der
       Fenstermanager liest sie bei der Abbildungsanfrage, nicht spaeter. */
    XChangeProperty(xdpy, win, a_wintype, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&a_normal, 1);
    XChangeProperty(xdpy, win, a_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&a_fullscr, 1);
    /* Ohne WM_DELETE_WINDOW wird aus der Aufgabenansicht heraus die
       Verbindung abgeschnitten (XKillClient); damit meldet sich das
       Schliessen als gewoehnliches Ereignis und wir raeumen selbst auf. */
    Atom protocols[1] = { a_delete };
    XSetWMProtocols(xdpy, win, protocols, 1);
    /* _NET_WM_PING sagen wir bewusst nicht zu: der Compositor fragt dann in
       kurzen Abstaenden nach, und beim Laden einer Szenerie stecken wir lange
       genug in einem Stueck Arbeit, dass er uns fuer haengend hielte und das
       Abschiessen anboete.  Wir brauchen die Frage nicht. */
    long netpid = (long)getpid();
    XChangeProperty(xdpy, win, a_netpid, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&netpid, 1);
    /* Harmattan dreht die Oberflaeche selbst; wir liegen quer wie der
       Bildschirm selbst, also Winkel 0.  COCKPIT_ANGLE, falls doch. */
    long angle = getenv("COCKPIT_ANGLE") ? atol(getenv("COCKPIT_ANGLE")) : 0;
    XChangeProperty(xdpy, win, a_angle, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&angle, 1);
    char hostname[128] = "";
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        char *hp = hostname;
        XTextProperty tp;
        if (XStringListToTextProperty(&hp, 1, &tp)) {
            XSetWMClientMachine(xdpy, win, &tp);
            XFree(tp.value);
        }
    }
    /* Die .desktop-Datei nennt X-Maemo-Wm-Class=cockpit; ohne WM_CLASS am
       Fenster findet der Starter das Fenster nicht wieder und die
       Aufgabenansicht zeigt es ohne unser Zeichen. */
    XClassHint cls;
    cls.res_name = (char *)"cockpit";
    cls.res_class = (char *)"cockpit";
    XSetClassHint(xdpy, win, &cls);
    XStoreName(xdpy, win, "FG Fly");
    XSetIconName(xdpy, win, "FG Fly");
    XMapRaised(xdpy, win);
    XFlush(xdpy);

    /* Erst X fragen: von dort kommen die Finger auch dann, wenn die App vom
       Startbildschirm gestartet wurde und die Gruppe `input` nicht hat. */
    int use_xtouch = xtouch_open(xdpy, win);

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)win, NULL);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface: %#x\n", eglGetError());
        return 1;
    }
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext: %#x\n", eglGetError());
        return 1;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "eglMakeCurrent: %#x\n", eglGetError());
        return 1;
    }
    /* Ohne Bildsynchronisation misst man, was die Maschine kann; mit ihr,
       was man sieht.  COCKPIT_VSYNC=0 schaltet sie fuer Messungen ab. */
    eglSwapInterval(dpy, getenv("COCKPIT_VSYNC") ? atoi(getenv("COCKPIT_VSYNC")) : 1);
    printf("%s, %dx%d\n", glGetString(GL_RENDERER), width, height);
    printf("Shader: %d + %d = %d Bytes\n", (int)strlen(VERT), (int)strlen(FRAG),
           (int)(strlen(VERT) + strlen(FRAG)));

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, VERT));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, FRAG));
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_col");
    glLinkProgram(prog);
    GLint linked = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) { fprintf(stderr, "Programm bindet nicht\n"); return 2; }
    glUseProgram(prog);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glViewport(0, 0, width, height);

    int no2d = getenv("COCKPIT_NO2D") != NULL;
    float view_range = getenv("COCKPIT_RANGE") ? (float)atof(getenv("COCKPIT_RANGE")) : 25000.0f;
    float lod_switch = getenv("COCKPIT_LOD") ? (float)atof(getenv("COCKPIT_LOD")) : 9000.0f;
    if (getenv("COCKPIT_CHASE")) chase_m = (float)atof(getenv("COCKPIT_CHASE"));
    int tri_budget = getenv("COCKPIT_TRIS") ? atoi(getenv("COCKPIT_TRIS")) : 150000;
    float vp = getenv("COCKPIT_VIEWPORT") ? (float)atof(getenv("COCKPIT_VIEWPORT")) : 1.0f;
    if (vp <= 0.0f || vp > 1.0f) vp = 1.0f;
    glViewport(0, 0, (int)(width * vp), (int)(height * vp));
    const char *shot = getenv("COCKPIT_SHOT");
    long shot_frame = getenv("COCKPIT_SHOT_FRAME") ? atol(getenv("COCKPIT_SHOT_FRAME")) : 60;
    int no_sensor = getenv("COCKPIT_NOSENSOR") != NULL;
    int demo = getenv("COCKPIT_DEMO") != NULL;
    int dragging = 0;
    /* Das Geraet selbst ist nur noch der Rueckfall - aus einer Shell heraus
       (dort hat der Benutzer die Gruppe) oder mit COCKPIT_TOUCH zum Pruefen. */
    int touch_fd = (use_xtouch && !getenv("COCKPIT_TOUCH")) ? -1 : touch_open();
    /* COCKPIT_XTOUCH laesst zusaetzlich den X-Zeiger gelten - damit kann man
       die Oberflaeche ueber ssh mit tap.py bedienen. */
    int x_touch = getenv("COCKPIT_XTOUCH") != NULL;
    struct touch_state touch_now, touch_prev;
    memset(&touch_now, 0, sizeof(touch_now));
    memset(&touch_prev, 0, sizeof(touch_prev));
    int gesture = 0;                    /* 0 = nichts, 1 = Bedienung, 2 = drehen, 3 = zoomen,
                                           4 = Randwisch, gehoert dem Fenstermanager */
    int app_active = 1;                 /* 0, sobald der Randwisch uns wegwischt */
    int had_focus = 0;                  /* erst wenn wir den Fokus einmal hatten,
                                           heisst sein Verlust auch Hintergrund */
    /* Beim Pruefen ueber ssh liegt das Fenster hinter dem, was der Nutzer
       gerade offen hat - dann pausiert die App und man misst nichts. */
    int never_pause = getenv("COCKPIT_NOPAUSE") != NULL;
    double ui_touch_time = -100.0;      /* wann zuletzt ein Finger da war */
    /* Der Randwisch faengt am Bildschirmrand an.  Wir lesen den Schirm direkt
       und sehen denselben Finger; was am Rand aufsetzt, ruehren wir nicht an,
       sonst zieht das Wegwischen nebenher den Schubhebel.  COCKPIT_EDGE=0
       schaltet das ab. */
    int edge_px = getenv("COCKPIT_EDGE") ? atoi(getenv("COCKPIT_EDGE")) : 12;
    float pinch0 = 0.0f, chase0 = 0.0f;
    long frames = 0, frames_at_last = 0;
    double start = now_s(), last = start, t_prev = start;
    /* Was auf dem Geraet liegt, fuer den Startbildschirm. */
    const char *data_dir = getenv("COCKPIT_DATA") ? getenv("COCKPIT_DATA") : "/home/user";
    scan_data(data_dir);
    /* Die Daten liegen auf dem grossen Teil: /home hat 463 MB frei, MyDocs
       2 GB.  Also auch dort nachsehen. */
    char mydocs[288];
    snprintf(mydocs, sizeof(mydocs), "%s/MyDocs", data_dir);
    struct stat mst;
    if (!stat(mydocs, &mst) && S_ISDIR(mst.st_mode)) scan_data(mydocs);
    printf("Gefunden: %d Flugzeuge, %d Szenerien in %s\n", n_aircraft, n_scenery, data_dir);

    /* Flugzeug: die Datei aus acftconv.py, sonst die eingebaute Cessna. */
    fdm_default(&aircraft_data);
    const char *acft_path = getenv("COCKPIT_ACFT");
    if (acft_path) screen_start = 0;
    if (acft_path && fdm_load(&aircraft_data, acft_path))
        printf("Flugzeug: %s (%s)\n", aircraft_data.name, acft_path);
    else
        printf("Flugzeug: %s\n", aircraft_data.name);
    printf("  %.0f kg, %.1f m2, Schub %.0f N, %.0f-%.0f U/min, %d+%d Stuetzstellen\n",
           aircraft_data.mass_kg, aircraft_data.wing_area_m2, aircraft_data.thrust_max_n,
           aircraft_data.rpm_idle, aircraft_data.rpm_max, aircraft_data.cl_alpha.n, aircraft_data.cd_alpha.n);
    fdm_init(&fdm, acft);
    const char *tpath = getenv("COCKPIT_TERRAIN");
    if (tpath) {
        /* Derselbe Anfang wie aus dem Menue: Kacheln laden, das Gelaende
           einschalten und auf der Bahn stehen - sonst stand das Flugzeug auf
           Meereshoehe unter der Landschaft und es war ueberhaupt nichts zu
           sehen (have_terrain blieb 0). */
        load_terrain_dir(tpath, getenv("COCKPIT_ICAO"));
        have_terrain = nland > 0;
        screen_start = 0;
        if (nland > 0) {
            fdm.ground_m = land[0].centre_height;
            fdm.alt_m = fdm.ground_m;
            if (start_on_runway(land[0].name)) fdm.alt_m = fdm.ground_m;
        }
    }
    /* Ohne Hand am Geraet: die erste Auswahl nehmen und losfliegen. */
    if (getenv("COCKPIT_GROUND")) start_airborne = 0;
    if (getenv("COCKPIT_AUTOSTART")) { load_world(); screen_start = 0; }
    if (getenv("COCKPIT_ALT")) {
        fdm.alt_m = (float)atof(getenv("COCKPIT_ALT"));
        fdm.on_ground = 0;
    }
    if (getenv("COCKPIT_SPEED")) fdm.v_ms = (float)atof(getenv("COCKPIT_SPEED")) / 1.94384f;
    if (getenv("COCKPIT_FLAPS")) flaps = (float)atof(getenv("COCKPIT_FLAPS"));
    if (getenv("COCKPIT_HDG")) fdm.heading_deg = (float)atof(getenv("COCKPIT_HDG"));
    if (getenv("COCKPIT_VIEW")) view_mode = atoi(getenv("COCKPIT_VIEW")) % 3;
    const char *mpath = getenv("COCKPIT_MODEL");
    if (mpath) {
        struct terrain_frame none;
        memset(&none, 0, sizeof(none));
        none.set = 1;                       /* Modelle brauchen kein Ortssystem */
        have_model = terrain_load(&model, mpath, &none);
        if (have_model)
            printf("Flugzeugmodell: %d Dreiecke, %d Gruppen\n",
                   model.nindices / 3, model.ngroups);
    }

    for (;;) {
        if (touch_fd >= 0 || use_xtouch) {
            if (touch_fd >= 0) touch_read(touch_fd, &touch_now);
            /* bei X traegt xtouch_event() den Stand ein, gleich unten */
            /* Der Schirm meldet weiter, auch wenn wir weggewischt sind - die
               Finger auf dem Startbildschirm sind dann nicht unsere. */
            if (!app_active) memset(&touch_now, 0, sizeof(touch_now));
            if (touch_now.n == 2) {
                float dx = touch_now.p[0].x - touch_now.p[1].x;
                float dy = touch_now.p[0].y - touch_now.p[1].y;
                float d = sqrtf(dx * dx * aspect * aspect + dy * dy);
                /* Der zweite Finger kommt selten im selben Bild wie der
                   erste - und faellt zwischendurch auch mal fuer ein Bild
                   aus.  Der Abstand beim Anfang wird deshalb nur einmal
                   gemerkt, sonst faengt das Zoomen bei jedem Aussetzer von
                   vorne an und bewegt sich nie. */
                if (gesture != 3) { gesture = 3; pinch0 = d; chase0 = chase_m; }
                else if (d > 0.01f && pinch0 > 0.01f) {
                    chase_m = chase0 * pinch0 / d;      /* auseinander = naeher dran */
                    if (chase_m < 8.0f) chase_m = 8.0f;
                    if (chase_m > 400.0f) chase_m = 400.0f;
                }
            } else if (touch_now.n == 1) {
                float x = touch_now.p[0].x, y = touch_now.p[0].y;
                if (gesture == 0) {
                    /* Wo der Finger aufsetzt, entscheidet: am Rand wischt er
                       die App weg (das macht der Fenstermanager), auf einem
                       Bedienteil wird bedient, sonst wird gedreht.  Im Menue
                       ist alles Bedienung.  Aus dem Zoomen heraus wird nichts
                       neu entschieden - erst wenn alle Finger weg sind. */
                    float ex = edge_px / (float)width, ey = edge_px / (float)height;
                    int on_edge = edge_px > 0 && (x < ex || x > 1.0f - ex
                                                  || y < ey || y > 1.0f - ey);
                    if (on_edge) gesture = 4;
                    else gesture = (screen_start || on_widget(x, y) || view_mode != 2)
                                   ? 1 : 2;
                }
                if (gesture == 1) {
                    /* Im Menue bedient derselbe Finger das Menue, nicht die
                       Schubhebel - das fehlte und machte die Auswahl unbedienbar. */
                    if (screen_start) {
                        if (touch_prev.n == 0) touch_start(x, y);   /* nur beim Aufsetzen */
                    }
                    else touch((int)(x * width), (int)(y * height), width, height,
                               touch_prev.n == 0);
                } else if (gesture == 2 && touch_prev.n == 1) {
                    orbit_az -= (x - touch_prev.p[0].x) * 240.0f;
                    orbit_el += (y - touch_prev.p[0].y) * 160.0f;
                    if (orbit_el > 85.0f) orbit_el = 85.0f;
                    if (orbit_el < -85.0f) orbit_el = -85.0f;
                }
            } else {
                gesture = 0;
            }
            touch_prev = touch_now;
        }
        while (XPending(xdpy)) {
            XEvent ev;
            XNextEvent(xdpy, &ev);
            if (xtouch_is_event(&ev)) {
                xtouch_event(xdpy, &ev, &touch_now);
                continue;
            }
            if ((touch_fd >= 0 || use_xtouch) && !x_touch
                && (ev.type == ButtonPress || ev.type == ButtonRelease
                    || ev.type == MotionNotify))
                continue;               /* die Finger kommen vom Schirm, nicht vom Zeiger */
            if (ev.type == ButtonPress) {
                if (screen_start)
                    touch_start((float)ev.xbutton.x / width, (float)ev.xbutton.y / height);
                else { dragging = 1; touch(ev.xbutton.x, ev.xbutton.y, width, height, 1); }
            } else if (ev.type == ButtonRelease) dragging = 0;
            else if (ev.type == MotionNotify && dragging && !screen_start)
                touch(ev.xmotion.x, ev.xmotion.y, width, height, 0);
            else if (ev.type == KeyPress) goto done;
            else if (ev.type == ClientMessage) {
                /* Nur was ueber WM_PROTOCOLS kommt, ist eine Absprache mit dem
                   Fenstermanager; hier: das Schliessen. */
                if (ev.xclient.message_type == a_protos
                    && (Atom)ev.xclient.data.l[0] == a_delete) goto done;
            }
            else if (ev.type == ConfigureNotify) {
                if (ev.xconfigure.width > 0 && ev.xconfigure.height > 0
                    && (ev.xconfigure.width != width || ev.xconfigure.height != height)) {
                    width = ev.xconfigure.width;
                    height = ev.xconfigure.height;
                    aspect = (float)width / height;
                    glViewport(0, 0, (int)(width * vp), (int)(height * vp));
                }
            }
            /* Weggewischt heisst: ganz verdeckt oder gar nicht mehr
               abgebildet.  Dann rechnen und zeichnen wir nichts. */
            else if (ev.type == VisibilityNotify)
                app_active = ev.xvisibility.state != VisibilityFullyObscured;
            else if (ev.type == UnmapNotify) app_active = 0;
            else if (ev.type == MapNotify) app_active = 1;
            /* Auf der N950 gemessen: mcompositor schickt kein
               VisibilityNotify, wenn eine andere App in den Vordergrund
               kommt - die Bildrate lief im Hintergrund weiter.  Der Fokus
               sagt es.  Nur zaehlen, wenn wir ihn ueberhaupt je hatten,
               sonst hielte sich eine App an, die nie einen bekommt. */
            /* Nur ein richtiger Fokuswechsel zaehlt: waehrend eines Griffs
               (NotifyGrab/NotifyUngrab, etwa wenn der Compositor die Geste
               liest) wandert der Fokus kurz weg, ohne dass wir im Hintergrund
               waeren. */
            else if (ev.type == FocusIn && ev.xfocus.mode == NotifyNormal) {
                had_focus = 1;
                app_active = 1;
            }
            else if (ev.type == FocusOut && ev.xfocus.mode == NotifyNormal && had_focus)
                app_active = 0;
        }
        if (!app_active && never_pause) app_active = 1;
        if (!app_active) {
            gesture = 0;
            dragging = 0;
            usleep(100000);
            /* Die Pause ist kein Zeitschritt und keine Bildrate. */
            t_prev = now_s();
            last = t_prev;
            frames_at_last = frames;
            if (seconds > 0.0 && t_prev - start >= seconds) break;
            continue;
        }
        if (page == S_FETCH && frames % 30 == 0) {
            FILE *nf = fopen(FETCH_NOTE, "r");
            if (nf) {
                if (fgets(fetch_note, sizeof(fetch_note), nf)) {
                    char *nl = strchr(fetch_note, '\n');
                    if (nl) *nl = 0;
                }
                fclose(nf);
            }
            if (!strncmp(fetch_note, "FERTIG", 6)) {
                int status;
                waitpid(fetch_pid, &status, WNOHANG);
                fetch_pid = 0;
                n_aircraft = 0;
                n_scenery = 0;
                scan_data(getenv("COCKPIT_DATA") ? getenv("COCKPIT_DATA") : "/home/user");
                if (browse_mode) {
                    aircraft_installed(cat_chosen.id);
                    page = S_MAIN;
                } else if (scenery_has(apt_chosen.icao)) {
                    start_flight();
                } else {
                    snprintf(fetch_note, sizeof(fetch_note), "NICHTS GEFUNDEN");
                    page = S_RWY;
                }
            } else if (!strncmp(fetch_note, "FEHLER", 6) || !strncmp(fetch_note, "KEIN", 4)) {
                page = browse_mode ? S_LIST : S_RWY;
            }
        }
        if (!no_sensor && !screen_start) read_accel(&tilt_roll, &tilt_pitch);
        double t_now = now_s();
        float dt = (float)(t_now - t_prev);
        t_prev = t_now;

        /* Bedienelemente wie bei X-Plane: unsichtbar, bis jemand den Schirm
           anfasst, dann zweieinhalb Sekunden durchscheinend zu sehen und
           danach in einer Sekunde wieder weg.  Im Menue sind sie immer da. */
        if (touch_now.n > 0 || dragging) ui_touch_time = t_now;
        {
            double idle = t_now - ui_touch_time;
            float a = 0.0f;
            if (screen_start) a = 1.0f;
            else if (idle < 2.5) a = 0.60f;
            else if (idle < 3.5) a = 0.60f * (float)(3.5 - idle);
            ui_alpha = a;
        }
        /* Der Boden ist keine Ebene: unter dem Flugzeug nachsehen, wie hoch
           die Kachel dort liegt.  Ohne das steht er auf der Hoehe des
           Startplatzes, und man fliegt durch Berge hindurch. */
        if (have_terrain && !screen_start) {
            float ground = 0.0f;
            int found = 0;
            for (int i = 0; i < nland && !found; ++i) {
                if (is_coarse[i]) continue;
                found = terrain_height_at(&land[i], fdm.east_m, fdm.north_m, &ground);
            }
            if (found) {
                /* Weich nachziehen: eine Rasterzelle ist rund 190 m breit, und
                   der Sprung von einer zur naechsten soll das Flugzeug nicht
                   in die Luft werfen. */
                float step = dt * 4.0f;
                if (step > 1.0f) step = 1.0f;
                if (step < 0.0f) step = 0.0f;
                fdm.ground_m += (ground - fdm.ground_m) * step;
            }
        }
        /* Neigen ist der Knueppel: 30 Grad Neigung = voller Ausschlag,
           die ersten 4 Grad bleiben tot, sonst zittert es. */
        float sr = stick_from_tilt(tilt_roll);
        float sp = stick_from_tilt(tilt_pitch);
        if (demo) {                     /* ein Start ohne Hand am Geraet */
            double age = t_now - start;
            if (age > 1.0) { brake = 0; throttle = 1.0f; fdm.engine_on = 1; }
            sr = 0.0f;
            sp = (fdm.v_ms * 1.94384f > 55.0f && fdm.pitch_deg < 8.0f) ? 0.5f : 0.0f;
            if (!fdm.on_ground && fdm.alt_m > 300.0f) sp = -0.2f;
        }
        if (!screen_start)
            /* Was die Steuerung sagt, sollen auch die Klappen und Ruder am Modell
           zeigen - solange das Flugzeug sie mitbringt. */
        {
            float surfaces[4] = { flaps, sr, sp, rudder };
            terrain_set_controls(surfaces);
        }
        fdm_step(&fdm, acft, dt, sr, sp, rudder, throttle, flaps, brake, gear_down);
        /* Das Seitenruder federt zurueck, sobald der Finger weg ist. */
        if (touch_fd < 0 ? !dragging : touch_now.n == 0) {
            rudder -= rudder * (dt * 4.0f > 1.0f ? 1.0f : dt * 4.0f);
            if (fabsf(rudder) < 0.01f) rudder = 0.0f;
        }
        if (quit_now) break;

        if (screen_start) {
            build_start();
            glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
        build_frame();
        if (have_terrain) {
            glClearColor(0.35f, 0.55f, 0.85f, 1.0f);       /* Himmel */
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            float proj[16], view[16], mvp[16];
            float yaw0 = fdm.heading_deg * (float)M_PI / 180.0f;
            float pit0 = fdm.pitch_deg * (float)M_PI / 180.0f;
            float eye[3] = { fdm.east_m, fdm.north_m, fdm.alt_m + 2.0f };
            float cam_roll = fdm.roll_deg;
            float cam_yaw = fdm.heading_deg, cam_pitch = fdm.pitch_deg;
            if (view_mode == 2) {
                /* Verfolger: um das Flugzeug herum, nicht starr dahinter.
                   Ein Finger dreht, zwei ziehen heran - und mitgerollt wird
                   nicht, sonst kippt die Welt statt des Flugzeugs. */
                cam_yaw = fdm.heading_deg + orbit_az;
                cam_pitch = -orbit_el;
                float cy = cosf(cam_yaw * (float)M_PI / 180.0f);
                float sy2 = sinf(cam_yaw * (float)M_PI / 180.0f);
                float cp = cosf(cam_pitch * (float)M_PI / 180.0f);
                float sp = sinf(cam_pitch * (float)M_PI / 180.0f);
                eye[0] -= sy2 * cp * chase_m;
                eye[1] -= cy * cp * chase_m;
                eye[2] -= sp * chase_m;
                cam_roll = 0.0f;
            }
            static const float light[3] = { 0.40f, 0.25f, 0.88f };
            mat_perspective(proj, 55.0f, (float)width / height, 5.0f, 60000.0f);
            mat_look(view, eye, cam_yaw, cam_pitch, cam_roll);
            mat_mul(mvp, proj, view);
            /* Nur zeichnen, was naeher als die Sichtweite und nicht hinter
               uns ist - sonst laegen ueber eine Million Dreiecke an. */
            float yaw = fdm.heading_deg * (float)M_PI / 180.0f;
            float pit = fdm.pitch_deg * (float)M_PI / 180.0f;
            float fwd[3] = { sinf(yaw) * cosf(pit), cosf(yaw) * cosf(pit), sinf(pit) };
            tiles_drawn = 0;
            triangles_drawn = 0;
            coarse_drawn = 0;
            for (int i = 0; i < nland; ++i) {
                if (is_coarse[i]) continue;          /* nur ueber coarse_of erreichbar */
                if (!terrain_visible(&land[i], eye, fwd, view_range)) continue;
                const struct terrain *use = &land[i];
                float dx = land[i].local_center[0] - eye[0];
                float dy = land[i].local_center[1] - eye[1];
                float dist = sqrtf(dx * dx + dy * dy);
                /* Manche Kacheln sind auch aus der Naehe zu dicht - die
                   dichteste hier hat 378 000 Dreiecke, ein Drittel der Zeit
                   eines Bildes.  Ueber dem Budget nehmen wir auch nah die
                   grobe Fassung; sie traegt dasselbe Bild. */
                int too_dense = land[i].nindices / 3 > tri_budget;
                if (coarse_of[i] >= 0 && (dist > lod_switch || too_dense)) {
                    use = &land[coarse_of[i]];          /* aus der Ferne die grobe */
                    ++coarse_drawn;
                }
                terrain_draw(use, mvp, light);
                ++tiles_drawn;
                triangles_drawn += use->nindices / 3;
            }
            /* Das Flugzeug selbst - nur von aussen zu sehen. */
            if (have_model && view_mode == 2) {
                float m[16], mvp_model[16];
                /* Angehoben um den tiefsten Punkt des Modells: die Raeder
                   sollen den Boden beruehren, nicht der Bezugspunkt. */
                float pos[3] = { fdm.east_m, fdm.north_m, fdm.alt_m - model.low };
                mat_model(m, pos, fdm.heading_deg, fdm.pitch_deg, fdm.roll_deg);
                mat_mul(mvp_model, mvp, m);
                glDisable(GL_CULL_FACE);     /* die Modelle sind nicht durchweg richtig herum */
                terrain_draw(&model, mvp_model, light);
                glEnable(GL_CULL_FACE);
                triangles_drawn += model.nindices / 3;
            }
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        }
        if (!no2d) {
            /* Die flache Oberflaeche zeichnet aus eigenen Feldern, nicht aus
               einem Puffer - also sicherstellen, dass keiner gebunden ist. */
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glUseProgram(prog);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), &verts[0].x);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(struct vertex), &verts[0].r);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_TRIANGLES, 0, nverts);
            glDisable(GL_BLEND);
        }
        eglSwapBuffers(dpy, surf);
        ++frames;

        if (shot && frames == shot_frame) write_ppm(shot, width, height);
        double t = now_s();
        if (t - last >= 1.0) {
            /* Bilder seit der letzten Zeile, nicht der Schnitt seit dem Start -
               sonst zieht die Ladezeit die Zahl eine halbe Minute lang mit. */
            printf("%5.1f B/s %2d Kacheln (%d grob) %6d Dr.  Schub %.2f Klappen %.2f "
                   "Bremse %d Fahrwerk %-3s  "
                   "%5.1f kt  %6.0f ft  %+6.0f ft/min  %4.0f U/min  "
                   "Rollen %+5.1f Nicken %+5.1f %s\n",
                   (frames - frames_at_last) / (t - last), tiles_drawn, coarse_drawn,
                   triangles_drawn,
                   throttle, flaps, brake, gear_down ? "aus" : "ein",
                   fdm.v_ms * 1.94384f, fdm.alt_m * 3.28084f, fdm.vs_ms * 196.85f, fdm.rpm,
                   fdm.roll_deg, fdm.pitch_deg, fdm.on_ground ? "am Boden" : "in der Luft");
            /* COCKPIT_DEBUG sagt, was die Finger gerade anrichten - ohne das
               ist aus der Ferne nicht zu sehen, ob eine Geste ankommt. */
            if (getenv("COCKPIT_DEBUG"))
                printf("      Sicht %d  Geste %d  Finger %d  Kreisen %+6.1f/%+5.1f  "
                       "Abstand %.0f m\n",
                       view_mode, gesture, touch_now.n, orbit_az, orbit_el, chase_m);
            last = t;
            frames_at_last = frames;
        }
        if (seconds > 0.0 && t - start >= seconds) break;
    }
done:
    if (fetch_pid > 0) {                /* der Holer soll nicht weiterlaufen */
        kill(fetch_pid, SIGTERM);
        waitpid(fetch_pid, NULL, 0);
    }
    printf("%ld Bilder in %.1f s = %.1f Bilder/s\n", frames, now_s() - start,
           frames / (now_s() - start));
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    XDestroyWindow(xdpy, win);
    XCloseDisplay(xdpy);
    return 0;
}
