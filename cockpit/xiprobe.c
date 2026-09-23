/*
 * xiprobe - was weiss X auf diesem Geraet ueber Finger?
 *
 * Der Simulator liest den Beruehrungsschirm bisher selbst aus
 * /dev/input/event*, und genau das darf er nicht, wenn er vom
 * Startbildschirm aus gestartet wird (die Gruppe `input` gibt der Starter
 * nicht weiter).  Andere Apps koennen es trotzdem - also fragen wir X, was
 * es anbietet: welche Fassung der Eingabeerweiterung, welche Geraete, und ob
 * ein Geraet Beruehrungen meldet (XITouchClass, ab XI 2.2) oder wenigstens
 * Mehrfinger-Achsen als Valuatoren.
 *
 * Braucht kein Fenster - nimmt also niemandem den Bildschirm weg.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XI2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Die Kopfdateien der Client-Schnittstelle fehlen im MADDE-Sysroot; die
   Strukturen stehen aber seit jeher fest (XInput2.h aus Xorg). */
/* XI2.h im Sysroot ist die Fassung 2.0 und kennt die Beruehrungsklasse noch
   nicht - die Zahl steht aber fest (XI 2.2). */
#ifndef XITouchClass
#define XITouchClass 8
#endif

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
    int type, sourceid, mode, num_touches;
} XITouchClassInfo;

typedef struct {
    int mask_len;
    unsigned char *mask;
} XIButtonState;

typedef struct { int type, sourceid, num_buttons; Atom *labels; XIButtonState state; } XIButtonClassInfo;

extern Status XIQueryVersion(Display *dpy, int *major, int *minor);
extern XIDeviceInfo *XIQueryDevice(Display *dpy, int deviceid, int *ndevices_return);
extern void XIFreeDeviceInfo(XIDeviceInfo *info);

static const char *use_name(int use) {
    switch (use) {
    case 0: return "?";
    case 1: return "Master-Zeiger";
    case 2: return "Master-Tastatur";
    case 3: return "Slave-Zeiger";
    case 4: return "Slave-Tastatur";
    case 5: return "unbenutzt";
    default: return "?";
    }
}

int main(void) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "kein Display\n"); return 1; }

    int opcode = 0, event = 0, error = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &opcode, &event, &error)) {
        printf("keine Eingabeerweiterung\n");
        return 2;
    }
    printf("XInputExtension: opcode %d\n", opcode);

    int want[][2] = { {2, 2}, {2, 1}, {2, 0} };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) {
        int major = want[i][0], minor = want[i][1];
        Status st = XIQueryVersion(dpy, &major, &minor);
        printf("  gewuenscht %d.%d -> Antwort %d.%d (%s)\n",
               want[i][0], want[i][1], major, minor,
               st == Success ? "Erfolg" : "abgelehnt");
        if (st == Success && (major > 2 || (major == 2 && minor >= 2))) break;
    }

    int n = 0;
    XIDeviceInfo *devs = XIQueryDevice(dpy, 0 /* XIAllDevices */, &n);
    printf("%d Geraete:\n", n);
    for (int i = 0; i < n; ++i) {
        printf("  [%d] %-28s %s, %d Klassen\n", devs[i].deviceid, devs[i].name,
               use_name(devs[i].use), devs[i].num_classes);
        for (int c = 0; c < devs[i].num_classes; ++c) {
            XIAnyClassInfo *any = devs[i].classes[c];
            if (any->type == XIValuatorClass) {
                XIValuatorClassInfo *v = (XIValuatorClassInfo *)any;
                char *label = v->label ? XGetAtomName(dpy, v->label) : NULL;
                printf("        Achse %d: %-24s %.0f .. %.0f\n", v->number,
                       label ? label : "(ohne Namen)", v->min, v->max);
                if (label) XFree(label);
            } else if (any->type == XITouchClass) {
                XITouchClassInfo *t = (XITouchClassInfo *)any;
                printf("        **Beruehrungen**: %d gleichzeitig, Art %d\n",
                       t->num_touches, t->mode);
            } else if (any->type == XIButtonClass) {
                XIButtonClassInfo *b = (XIButtonClassInfo *)any;
                printf("        %d Tasten\n", b->num_buttons);
            }
        }
    }
    XIFreeDeviceInfo(devs);
    XCloseDisplay(dpy);
    return 0;
}
