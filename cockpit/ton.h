/*
 * ton - der Klang des Flugzeugs, gerechnet statt abgespielt.
 *
 * Warum kein Codec und keine Klangdatei: Ein Motor klingt nicht bei jeder
 * Drehzahl gleich, und eine Aufnahme, die man schneller abspielt, klingt
 * nach schnellerer Aufnahme und nicht nach hoeherer Drehzahl.  Ein
 * Propeller ist ein Impuls je Blatt und Umdrehung -- das sind bei 2400
 * Umdrehungen und zwei Blaettern 80 Impulse in der Sekunde, und die lassen
 * sich ausrechnen.  Dazu Rauschen fuer den Fahrtwind und das Rollen.
 *
 * Ausgegeben wird ueber PulseAudio (`libpulse-simple`), denn das laeuft auf
 * dem Geraet ohnehin und mischt in die Audio-Hardware des N9; direkt auf
 * ALSA zu gehen hiesse, ihm das Geraet wegzunehmen.
 *
 * Gerechnet wird in einem eigenen Faden: Der Klang darf nicht stocken,
 * wenn ein Bild laenger braucht, und pa_simple_write wartet, bis der Puffer
 * Platz hat -- im Bildtakt waere das eine Bremse.
 *
 *   COCKPIT_TON=0    kein Ton
 */
#ifndef TON_H
#define TON_H

int  ton_start(void);          /* 1 = laeuft */
void ton_stop(void);

/* Was gerade zu hoeren sein soll.  Wird aus dem Bildtakt gesetzt, der
   Klangfaden liest es beim naechsten Block. */
void ton_zustand(float rpm, float rpm_max, float throttle, float v_ms,
                 int on_ground, int jet, int engine_on);

#endif
