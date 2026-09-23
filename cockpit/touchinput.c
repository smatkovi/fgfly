#include "touchinput.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Harmattans Kernel-Header sind von 2.6.32 und kennen die Plaetze des
   B-Protokolls noch nicht; die Zahlen stehen seit jeher fest. */
#ifndef ABS_MT_POSITION_X
#define ABS_MT_POSITION_X 0x35
#endif
#ifndef ABS_MT_POSITION_Y
#define ABS_MT_POSITION_Y 0x36
#endif
#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif
#ifndef ABS_MT_TRACKING_ID
#define ABS_MT_TRACKING_ID 0x39
#endif
#ifndef SYN_MT_REPORT
#define SYN_MT_REPORT 2
#endif

#define SLOTS 10

static struct { int id, x, y; } slot[SLOTS];
static int cur_slot;
static int protocol_b;                  /* 1, sobald ABS_MT_SLOT auftaucht */
static int min_x, max_x, min_y, max_y;

/* Das A-Protokoll meldet je Beruehrung eine Gruppe, abgeschlossen mit
   SYN_MT_REPORT, und das ganze Bild mit SYN_REPORT.  Genau so lesen wir es. */
static int pend_x, pend_y, pend_have;
static struct { int x, y; } frame[SLOTS];
static int frame_n, frame_ready, frame_count;

int touch_open(void) {
    const char *why = NULL;
    /* COCKPIT_TOUCH zeigt auf ein bestimmtes Geraet - damit laesst sich die
       Bedienung ueber ssh pruefen: ein zweiter, virtueller Schirm aus
       ~/ps/meego-uitest/mtap.py statt der Finger. */
    const char *want = getenv("COCKPIT_TOUCH");
    for (int i = -1; i < 24; ++i) {
        char path[64];
        if (i < 0) {
            if (!want) continue;
            snprintf(path, sizeof(path), "%s", want);
        } else {
            if (want) break;            /* nur das gewuenschte Geraet */
            snprintf(path, sizeof(path), "/dev/input/event%d", i);
        }
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno == EACCES && !why) why = "keine Berechtigung, Gruppe input fehlt";
            continue;
        }
        char name[128] = "";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        struct input_absinfo abs;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs) == 0 && abs.maximum > abs.minimum) {
            min_x = abs.minimum;
            max_x = abs.maximum;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs) == 0) {
                min_y = abs.minimum;
                max_y = abs.maximum;
            }
            for (int s = 0; s < SLOTS; ++s) slot[s].id = -1;
            printf("Beruehrungsschirm: %s (%s), x %d..%d, y %d..%d\n",
                   name, path, min_x, max_x, min_y, max_y);
            return fd;
        }
        close(fd);
    }
    /* Warum nicht?  Fast immer, weil der Starter des Startbildschirms die
       Gruppe `input` nicht weitergibt - dann sagt das Oeffnen "Keine
       Berechtigung", und im Protokoll steht sonst nur ein Raetsel. */
    printf("kein Beruehrungsschirm gefunden (%s) - X-Zeiger wird benutzt\n",
           why ? why : "kein Geraet mit Mehrfingerachsen");
    return -1;
}

void touch_read(int fd, struct touch_state *st) {
    struct input_event ev[64];
    ssize_t got;
    while ((got = read(fd, ev, sizeof(ev))) > 0) {
        for (size_t i = 0; i < (size_t)got / sizeof(ev[0]); ++i) {
            struct input_event *e = &ev[i];
            if (e->type == EV_ABS) {
                switch (e->code) {
                case ABS_MT_SLOT:
                    protocol_b = 1;
                    cur_slot = e->value >= 0 && e->value < SLOTS ? e->value : 0;
                    break;
                case ABS_MT_TRACKING_ID:
                    if (protocol_b) slot[cur_slot].id = e->value;
                    break;
                case ABS_MT_POSITION_X:
                    if (protocol_b) slot[cur_slot].x = e->value;
                    else { pend_x = e->value; pend_have = 1; }
                    break;
                case ABS_MT_POSITION_Y:
                    if (protocol_b) slot[cur_slot].y = e->value;
                    else { pend_y = e->value; pend_have = 1; }
                    break;
                default:
                    break;
                }
            } else if (e->type == EV_SYN) {
                if (e->code == SYN_MT_REPORT) {
                    if (pend_have && frame_n < SLOTS) {
                        frame[frame_n].x = pend_x;
                        frame[frame_n].y = pend_y;
                        ++frame_n;
                    }
                    pend_have = 0;
                } else if (e->code == SYN_REPORT) {
                    if (!protocol_b) { frame_count = frame_n; frame_ready = 1; }
                    frame_n = 0;
                    pend_have = 0;
                }
            }
        }
    }

    int n = 0;
    if (protocol_b) {
        for (int s = 0; s < SLOTS && n < TOUCH_MAX; ++s) {
            if (slot[s].id < 0) continue;
            st->p[n].down = 1;
            st->p[n].x = (float)(slot[s].x - min_x) / (float)(max_x - min_x + 1);
            st->p[n].y = (float)(slot[s].y - min_y) / (float)(max_y - min_y + 1);
            ++n;
        }
    } else if (frame_ready) {
        for (int s = 0; s < frame_count && n < TOUCH_MAX; ++s) {
            st->p[n].down = 1;
            st->p[n].x = (float)(frame[s].x - min_x) / (float)(max_x - min_x + 1);
            st->p[n].y = (float)(frame[s].y - min_y) / (float)(max_y - min_y + 1);
            ++n;
        }
    } else {
        return;                          /* noch kein vollstaendiges Bild */
    }
    for (int i = n; i < TOUCH_MAX; ++i) st->p[i].down = 0;
    st->n = n;
}
