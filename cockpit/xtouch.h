/*
 * xtouch - Beruehrungen ueber X (XInput 2.0), statt /dev/input direkt.
 *
 * Der Weg ueber das Geraet scheitert daran, dass eine vom Startbildschirm
 * gestartete App die Gruppe `input` nicht hat.  X hat sie, und Harmattan
 * reicht bis zu sechs Finger als Achsen durch.
 */
#ifndef XTOUCH_H
#define XTOUCH_H

#include <X11/Xlib.h>
#include "touchinput.h"

/* Sucht die Fingerachsen und meldet unsere Wuensche an; 0 = geht nicht. */
int xtouch_open(Display *dpy, Window win);

/* Gehoert dieses X-Ereignis uns? */
int xtouch_is_event(XEvent *ev);

/* Traegt den Stand der Finger ein. */
void xtouch_event(Display *dpy, XEvent *ev, struct touch_state *st);

#endif
