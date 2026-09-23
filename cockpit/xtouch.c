/*
 * xtouch - die Finger von X holen statt aus /dev/input.
 *
 * Harmattans X reicht **sechs** Beruehrungen als Achsen der Eingabe-
 * erweiterung durch: je Finger fuenf Stueck (Position X, Position Y, Touch
 * Major, Touch Minor, Tracking ID) am Master-Zeiger, insgesamt dreissig.
 * So kommen die anderen Apps an zwei Finger, ohne `/dev/input/event*` zu
 * oeffnen - und das ist der Punkt: vom Startbildschirm gestartet hat eine App
 * die Gruppe `input` nicht und darf das Geraet gar nicht aufmachen.
 *
 * Gelesen wird XI 2.0 (mehr gibt der Server nicht her): Tastendruck, Loslassen
 * und Bewegung des Zeigers, und in jedem Ereignis stehen die Achsen, die sich
 * geaendert haben.  Die uebrigen merken wir uns.
 */
#include "xtouch.h"

#include <X11/Xatom.h>
#include <X11/extensions/XI2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XIValuatorClass
#define XIValuatorClass 2
#endif

/* Die Client-Strukturen fehlen im MADDE-Sysroot (nur die Protokollkoepfe sind
   da); sie stehen seit Xorg 1.7 unveraendert in XInput2.h. */
typedef struct { int type; int sourceid; } XIAnyClassInfo;

typedef struct {
    int deviceid;
    char *name;
    int use;
    int attachment;
    Bool enabled;
    int num_classes;
    XIAnyClassInfo **classes;
} XIDeviceInfo;

typedef struct {
    int type, sourceid, number;
    Atom label;
    double min, max, value;
    int resolution;
    int mode;
} XIValuatorClassInfo;

typedef struct {
    int deviceid;
    int mask_len;
    unsigned char *mask;
} XIEventMask;

typedef struct { int mask_len; unsigned char *mask; } XIButtonState;
typedef struct { int mask_len; unsigned char *mask; double *values; } XIValuatorState;
typedef struct { int base, latched, locked, effective; } XIModifierState;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    int extension;
    int evtype;
    Time time;
    int deviceid;
    int sourceid;
    int detail;
    Window root, event, child;
    double root_x, root_y, event_x, event_y;
    int flags;
    XIButtonState buttons;
    XIValuatorState valuators;
    XIModifierState mods;
    XIModifierState group;
} XIDeviceEvent;

extern Status XIQueryVersion(Display *dpy, int *major, int *minor);
extern XIDeviceInfo *XIQueryDevice(Display *dpy, int deviceid, int *ndevices);
extern void XIFreeDeviceInfo(XIDeviceInfo *info);
extern Status XISelectEvents(Display *dpy, Window win, XIEventMask *masks, int nmasks);

#define MAX_AXES 64

static int xi_opcode = -1;
static int n_contacts;                  /* wieviele Finger der Server anbietet */
static struct {
    int ix, iy, iid;                    /* Achsennummern dieses Fingers */
} contact[TOUCH_MAX];
static double axis_min[MAX_AXES], axis_max[MAX_AXES];
static double axis_val[MAX_AXES];
static int axis_known[MAX_AXES];
static int pointer_down;

static void set_bit(unsigned char *mask, int bit) { mask[bit >> 3] |= 1 << (bit & 7); }
static int get_bit(const unsigned char *mask, int len, int bit) {
    return (bit >> 3) < len && (mask[bit >> 3] & (1 << (bit & 7)));
}

int xtouch_open(Display *dpy, Window win) {
    int event = 0, error = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) {
        xi_opcode = -1;
        return 0;
    }
    int major = 2, minor = 0;
    if (XIQueryVersion(dpy, &major, &minor) != Success || major < 2) {
        xi_opcode = -1;
        return 0;
    }

    /* Welche Achse gehoert zu welchem Finger?  Die Namen sagen es, und sie
       wiederholen sich: Position X, Position Y, Touch Major, Touch Minor,
       Tracking ID - und dann von vorne fuer den naechsten Finger. */
    int ndev = 0;
    XIDeviceInfo *devs = XIQueryDevice(dpy, 0 /* XIAllDevices */, &ndev);
    n_contacts = 0;
    int master = -1;
    for (int i = 0; i < ndev && n_contacts == 0; ++i) {
        if (devs[i].use != 1 /* Master-Zeiger */) continue;
        int seen_x = -1, seen_y = -1;
        for (int c = 0; c < devs[i].num_classes; ++c) {
            XIAnyClassInfo *any = devs[i].classes[c];
            if (any->type != XIValuatorClass) continue;
            XIValuatorClassInfo *v = (XIValuatorClassInfo *)any;
            if (v->number < 0 || v->number >= MAX_AXES) continue;
            axis_min[v->number] = v->min;
            axis_max[v->number] = v->max;
            char *label = v->label ? XGetAtomName(dpy, v->label) : NULL;
            if (!label) continue;
            if (!strcmp(label, "Abs MT Position X")) seen_x = v->number;
            else if (!strcmp(label, "Abs MT Position Y")) seen_y = v->number;
            else if (!strcmp(label, "Abs MT Tracking ID") && seen_x >= 0 && seen_y >= 0
                     && n_contacts < TOUCH_MAX) {
                contact[n_contacts].ix = seen_x;
                contact[n_contacts].iy = seen_y;
                contact[n_contacts].iid = v->number;
                ++n_contacts;
                seen_x = seen_y = -1;
            }
            XFree(label);
        }
        master = devs[i].deviceid;
    }
    XIFreeDeviceInfo(devs);

    if (master < 0) { xi_opcode = -1; return 0; }

    unsigned char mask[4] = { 0, 0, 0, 0 };
    set_bit(mask, XI_ButtonPress);
    set_bit(mask, XI_ButtonRelease);
    set_bit(mask, XI_Motion);
    XIEventMask em;
    em.deviceid = master;
    em.mask_len = sizeof(mask);
    em.mask = mask;
    XISelectEvents(dpy, win, &em, 1);
    XFlush(dpy);

    for (int i = 0; i < MAX_AXES; ++i) axis_known[i] = 0;
    printf("Beruehrungen von X: %d Finger, Geraet %d, XI %d.%d\n",
           n_contacts, master, major, minor);
    return 1;
}

int xtouch_is_event(XEvent *ev) {
    return xi_opcode >= 0 && ev->type == GenericEvent && ev->xcookie.extension == xi_opcode;
}

void xtouch_event(Display *dpy, XEvent *ev, struct touch_state *st) {
    XGenericEventCookie *cookie = &ev->xcookie;
    if (!XGetEventData(dpy, cookie)) return;
    XIDeviceEvent *de = (XIDeviceEvent *)cookie->data;

    if (de->evtype == XI_ButtonPress) {
        pointer_down = 1;
        /* Neue Geste, neue Finger: was von der letzten noch in den Achsen
           steht, gilt nicht mehr - sonst liegt schon beim Aufsetzen ein
           zweiter Finger da, den niemand angefasst hat. */
        for (int c = 0; c < n_contacts; ++c) {
            axis_known[contact[c].ix] = 0;
            axis_known[contact[c].iy] = 0;
            axis_known[contact[c].iid] = 0;
        }
    } else if (de->evtype == XI_ButtonRelease) pointer_down = 0;

    /* Nur die geaenderten Achsen stehen im Ereignis - der Rest gilt weiter. */
    const double *values = de->valuators.values;
    for (int i = 0; i < de->valuators.mask_len * 8 && i < MAX_AXES; ++i) {
        if (!get_bit(de->valuators.mask, de->valuators.mask_len, i)) continue;
        axis_val[i] = *values++;
        axis_known[i] = 1;
    }

    int n = 0;
    if (pointer_down) {
        for (int c = 0; c < n_contacts && n < TOUCH_MAX; ++c) {
            int ix = contact[c].ix, iy = contact[c].iy, iid = contact[c].iid;
            /* Ein Finger liegt, solange seine Kennung nicht negativ ist. */
            if (!axis_known[ix] || !axis_known[iy]) continue;
            if (axis_known[iid] && axis_val[iid] < 0.0) continue;
            /* Der erste Finger zaehlt auch ohne Kennung (eine Maus hat
               keine); jeder weitere nur mit - sonst zaehlen alte Werte mit. */
            if (c > 0 && !axis_known[iid]) continue;
            double rx = axis_max[ix] - axis_min[ix];
            double ry = axis_max[iy] - axis_min[iy];
            st->p[n].down = 1;
            st->p[n].x = rx > 0 ? (float)((axis_val[ix] - axis_min[ix]) / rx) : 0.0f;
            st->p[n].y = ry > 0 ? (float)((axis_val[iy] - axis_min[iy]) / ry) : 0.0f;
            ++n;
        }
        if (n == 0) {                   /* Zeiger ohne Fingerachsen (Maus) */
            st->p[0].down = 1;
            st->p[0].x = (float)de->event_x / (float)DisplayWidth(dpy, DefaultScreen(dpy));
            st->p[0].y = (float)de->event_y / (float)DisplayHeight(dpy, DefaultScreen(dpy));
            n = 1;
        }
    } else {
        /* Losgelassen: alle Kennungen vergessen, sonst klebt ein Finger. */
        for (int c = 0; c < n_contacts; ++c) axis_known[contact[c].iid] = 0;
    }
    for (int i = n; i < TOUCH_MAX; ++i) st->p[i].down = 0;
    st->n = n;
    XFreeEventData(dpy, cookie);
}
