/*
 * terrain - ein gebackenes Szeneriebuendel (.fgb aus ../bake/btgbake.py)
 *           laden und zeichnen.
 *
 * Das Buendel kommt erdfest: die Eckpunkte stehen relativ zum Mittelpunkt der
 * Kachel im erdfesten System.  Beim Laden werden sie einmal in das oertliche
 * System gedreht (Ost, Nord, oben, in Metern) - danach ist die Kamera eine
 * gewoehnliche Angelegenheit und der Shader muss nichts davon wissen.
 */
#ifndef TERRAIN_H
#define TERRAIN_H

#include <GLES2/gl2.h>

#define TERRAIN_GROUPS 64

/* Alle Kacheln werden in dasselbe oertliche System gedreht - das der ersten.
   Dadurch faellt die Erde am Rand richtig weg (ueber 16 km sind das 20 m). */
struct terrain_frame {
    double origin[3];
    double east[3], north[3], up[3];
    int set;
};

struct terrain {
    GLuint vbo, ibo, prog, tex;
    GLint u_mvp, u_light, u_col, u_texmap;
    float texmap[3];            /* Ursprung x, y und 1/Spanne fuer die Abbildung */
    int wide;                       /* 32-Bit-Indizes */
    int nvertices, nindices, ngroups;
    unsigned int vbytes;
    struct {
        int start, count;
        float col[3];
        char name[32];
        GLuint tex;                 /* nur bei Modellen: eigene Textur je Gruppe */
    } group[TERRAIN_GROUPS];
    int is_model;                   /* Mittelpunkt (0,0,0): ein Flugzeug, kein Stueck Erde */
    double center[3];
    float local_center[3];      /* Mittelpunkt im System der ersten Kachel */
    float radius;
    float centre_height;        /* Gelaendehoehe in der Mitte der Kachel */
    char name[32];
};

int terrain_load(struct terrain *t, const char *path, struct terrain_frame *frame);
void terrain_draw(const struct terrain *t, const float mvp[16], const float light[3]);
/* Nimmt der Kachel ihr gebackenes Bild - dann wird wieder nach Material
   gezeichnet (Flugplaetze, wo die Bahn scharf sein soll). */
void terrain_drop_texture(struct terrain *t);
/* Ein Ort auf der Erde im oertlichen System der geladenen Kacheln. */
void terrain_frame_local(const struct terrain_frame *f, double lat_deg, double lon_deg,
                         double elev_m, float out[3]);

void mat_model(float out[16], const float pos[3], float yaw_deg, float pitch_deg, float roll_deg);

/* Sichtbar heisst: naeher als die Sichtweite und nicht hinter der Kamera. */
int terrain_visible(const struct terrain *t, const float eye[3], const float fwd[3],
                    float view_range_m);

/* Kleine Matrixhilfen - mehr braucht ein Bild aus einer Kanzel nicht. */
void mat_perspective(float out[16], float fovy_deg, float aspect, float znear, float zfar);
void mat_look(float out[16], const float eye[3], float yaw_deg, float pitch_deg, float roll_deg);
void mat_mul(float out[16], const float a[16], const float b[16]);

#endif
