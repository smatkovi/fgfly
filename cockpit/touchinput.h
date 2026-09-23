/*
 * touchinput - der Bildschirm mit zwei Fingern.
 *
 * X gibt auf Harmattan nur einen Zeiger her; fuer Drehen mit einem und Zoomen
 * mit zwei Fingern braucht es den Beruehrungsschirm selbst.  Der Atmel mXT
 * meldet seine Beruehrungen als ABS_MT_*-Ereignisse mit Plaetzen, also lesen
 * wir /dev/input/event* direkt - ohne EVIOCGRAB, damit die Oberflaeche
 * daneben weiter funktioniert.
 */
#ifndef TOUCHINPUT_H
#define TOUCHINPUT_H

#define TOUCH_MAX 2

struct touch_point {
    int down;
    float x, y;            /* Bildschirmanteile 0..1 */
};

struct touch_state {
    struct touch_point p[TOUCH_MAX];
    int n;                 /* wieviele Finger gerade liegen */
};

/* Gibt den Dateizeiger auf den Beruehrungsschirm oder -1. */
int touch_open(void);

/* Holt alles, was anliegt, und traegt den Stand ein.  Blockiert nie. */
void touch_read(int fd, struct touch_state *st);

#endif
