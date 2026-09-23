/*
 * btgbake - dasselbe wie btgbake.py, nur so sparsam, dass es auf dem Geraet
 *           selbst laufen kann.
 *
 * Die Python-Fassung braucht fuer eine dichte Kachel 1,29 GB Spitzenspeicher;
 * die N950 hat 1 GB im ganzen.  Hier sind es die Daten selbst plus eine
 * Streuwerttabelle fuer die Eckpunkt-Tripel.
 *
 *   btgbake <kachel.btg.gz> <ziel.fgb>
 *
 * Die Ausgabe ist Byte fuer Byte dieselbe wie die der Python-Fassung - das
 * ist die Probe, ob die Umsetzung stimmt.
 */
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BOUNDING_SPHERE 0
#define VERTEX_LIST     1
#define NORMAL_LIST     2
#define TEXCOORD_LIST   3
#define COLOR_LIST      4
#define TRIANGLE_FACES  10
#define TRIANGLE_STRIPS 11
#define TRIANGLE_FANS   12

#define PROP_MATERIAL    0
#define PROP_INDEX_TYPES 1

#define IDX_VERTICES  1
#define IDX_NORMALS   2
#define IDX_COLORS    4
#define IDX_TEXCOORDS 8

#define MAX_GROUPS 128

struct reader {
    const unsigned char *data;
    size_t size, pos;
    int overrun;
};

static unsigned char take_u8(struct reader *r) {
    if (r->pos + 1 > r->size) { r->overrun = 1; return 0; }
    return r->data[r->pos++];
}

static uint16_t take_u16(struct reader *r) {
    if (r->pos + 2 > r->size) { r->overrun = 1; return 0; }
    uint16_t v;
    memcpy(&v, r->data + r->pos, 2);
    r->pos += 2;
    return v;
}

static uint32_t take_u32(struct reader *r) {
    if (r->pos + 4 > r->size) { r->overrun = 1; return 0; }
    uint32_t v;
    memcpy(&v, r->data + r->pos, 4);
    r->pos += 4;
    return v;
}

static const unsigned char *take_bytes(struct reader *r, size_t n) {
    if (r->pos + n > r->size) { r->overrun = 1; return NULL; }
    const unsigned char *p = r->data + r->pos;
    r->pos += n;
    return p;
}

/* --- die gelesene Kachel ------------------------------------------------- */

struct corner { uint32_t v, n, t; };

struct group {
    char material[64];
};

struct tile {
    int version, index_size;
    double center[3];
    float radius;
    float *vertex;        /* je drei */
    unsigned char *normal;/* je drei */
    float *texcoord;      /* je zwei */
    uint32_t nvertex, nnormal, ntexcoord;
    struct corner *corner;
    unsigned char *corner_group;          /* zu welchem Material die Ecke gehoert */
    uint32_t ncorner, corner_cap;
    struct group group[MAX_GROUPS];
    int ngroups;
};

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "kein Speicher fuer %lu Bytes\n", (unsigned long)n); exit(3); }
    return q;
}

/* Die Ecke merkt sich ihr Material: dasselbe Material kann in mehreren
   Objekten vorkommen, und im Buendel gehoeren seine Dreiecke zusammen. */
static void push_corner(struct tile *t, struct corner c, int group) {
    if (t->ncorner == t->corner_cap) {
        t->corner_cap = t->corner_cap ? t->corner_cap * 2 : 65536;
        t->corner = xrealloc(t->corner, t->corner_cap * sizeof(*t->corner));
        t->corner_group = xrealloc(t->corner_group, t->corner_cap);
    }
    t->corner_group[t->ncorner] = (unsigned char)group;
    t->corner[t->ncorner++] = c;
}

/* Ein Element zu Dreiecken aufloesen - Flaechen, Streifen und Faecher. */
static void element_triangles(struct tile *t, int obj_type, int index_types,
                              const unsigned char *raw, size_t nbytes, int index_size,
                              int group) {
    int per_vertex = !!(index_types & IDX_VERTICES) + !!(index_types & IDX_NORMALS) +
                     !!(index_types & IDX_COLORS) + !!(index_types & IDX_TEXCOORDS);
    if (per_vertex == 0) return;
    size_t count = nbytes / ((size_t)index_size * per_vertex);
    if (count < 3) return;

    struct corner *corner = malloc(count * sizeof(*corner));
    if (!corner) return;
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *p = raw + i * (size_t)index_size * per_vertex;
        uint32_t slot[4];
        for (int k = 0; k < per_vertex; ++k) {
            if (index_size == 4) { uint32_t v; memcpy(&v, p + 4 * k, 4); slot[k] = v; }
            else { uint16_t v; memcpy(&v, p + 2 * k, 2); slot[k] = v; }
        }
        int k = 0;
        corner[i].v = (index_types & IDX_VERTICES) ? slot[k++] : 0;
        corner[i].n = (index_types & IDX_NORMALS) ? slot[k++] : 0xFFFFFFFFu;
        if (index_types & IDX_COLORS) ++k;
        corner[i].t = (index_types & IDX_TEXCOORDS) ? slot[k++] : 0xFFFFFFFFu;
    }

    if (obj_type == TRIANGLE_FACES) {
        for (size_t i = 0; i + 2 < count; i += 3) {
            push_corner(t, corner[i], group); push_corner(t, corner[i + 1], group);
            push_corner(t, corner[i + 2], group);
        }
    } else if (obj_type == TRIANGLE_FANS) {
        for (size_t i = 1; i + 1 < count; ++i) {
            push_corner(t, corner[0], group); push_corner(t, corner[i], group);
            push_corner(t, corner[i + 1], group);
        }
    } else {                                  /* Streifen: jedes zweite umgekehrt */
        for (size_t i = 0; i + 2 < count; ++i) {
            if (i % 2 == 0) { push_corner(t, corner[i], group); push_corner(t, corner[i + 1], group); }
            else { push_corner(t, corner[i + 1], group); push_corner(t, corner[i], group); }
            push_corner(t, corner[i + 2], group);
        }
    }
    free(corner);
}

static int parse(struct tile *t, const unsigned char *data, size_t size, int forced_index_size);

/* Zeigt ein Index aus der Eckpunktliste heraus, war die Breite falsch. */
static int indices_out_of_range(const struct tile *t) {
    for (uint32_t i = 0; i < t->ncorner; ++i)
        if (t->corner[i].v >= t->nvertex) return 1;
    return 0;
}

static void tile_free(struct tile *t) {
    free(t->vertex); free(t->normal); free(t->texcoord); free(t->corner);
    free(t->corner_group);
    memset(t, 0, sizeof(*t));
}

static int parse(struct tile *t, const unsigned char *data, size_t size, int forced_index_size) {
    memset(t, 0, sizeof(*t));
    struct reader r = { data, size, 0, 0 };

    uint32_t header = take_u32(&r);
    take_u32(&r);                              /* Erzeugungszeit */
    if ((header >> 16) != 0x5347) { fprintf(stderr, "keine .btg-Datei\n"); return 0; }
    t->version = header & 0xFFFF;
    int wide = t->version >= 10;
    t->index_size = forced_index_size ? forced_index_size : (wide ? 4 : 2);

    uint32_t nobjects = wide ? take_u32(&r) : take_u16(&r);
    for (uint32_t o = 0; o < nobjects && !r.overrun; ++o) {
        int obj_type = (signed char)take_u8(&r);
        uint32_t nprops = wide ? take_u32(&r) : take_u16(&r);
        uint32_t nelements = wide ? take_u32(&r) : take_u16(&r);

        char material[64] = "";
        int index_types = IDX_VERTICES;
        for (uint32_t p = 0; p < nprops && !r.overrun; ++p) {
            int prop_type = (signed char)take_u8(&r);
            uint32_t nbytes = take_u32(&r);
            const unsigned char *raw = take_bytes(&r, nbytes);
            if (!raw) break;
            if (prop_type == PROP_MATERIAL) {
                size_t n = nbytes < sizeof(material) - 1 ? nbytes : sizeof(material) - 1;
                memcpy(material, raw, n);
                material[n] = '\0';
                for (size_t i = 0; i < n; ++i) if (material[i] == '\0') break;
            } else if (prop_type == PROP_INDEX_TYPES && nbytes > 0) {
                index_types = raw[0];
            }
        }

        int group_index = -1;
        if (obj_type == TRIANGLE_FACES || obj_type == TRIANGLE_STRIPS || obj_type == TRIANGLE_FANS) {
            for (int g = 0; g < t->ngroups; ++g)
                if (!strcmp(t->group[g].material, material)) { group_index = g; break; }
            if (group_index < 0 && t->ngroups < MAX_GROUPS) {
                group_index = t->ngroups++;
                snprintf(t->group[group_index].material, sizeof(t->group[group_index].material),
                         "%s", material);
            }
        }

        for (uint32_t e = 0; e < nelements && !r.overrun; ++e) {
            uint32_t nbytes = take_u32(&r);
            const unsigned char *raw = take_bytes(&r, nbytes);
            if (!raw) break;
            switch (obj_type) {
            case BOUNDING_SPHERE:
                if (nbytes >= 28) {
                    memcpy(t->center, raw, 24);
                    memcpy(&t->radius, raw + 24, 4);
                }
                break;
            case VERTEX_LIST:
                t->nvertex = nbytes / 12;
                t->vertex = xrealloc(t->vertex, nbytes ? nbytes : 1);
                memcpy(t->vertex, raw, nbytes);
                break;
            case NORMAL_LIST:
                t->nnormal = nbytes / 3;
                t->normal = xrealloc(t->normal, nbytes ? nbytes : 1);
                memcpy(t->normal, raw, nbytes);
                break;
            case TEXCOORD_LIST:
                t->ntexcoord = nbytes / 8;
                t->texcoord = xrealloc(t->texcoord, nbytes ? nbytes : 1);
                memcpy(t->texcoord, raw, nbytes);
                break;
            case COLOR_LIST:
                break;
            case TRIANGLE_FACES:
            case TRIANGLE_STRIPS:
            case TRIANGLE_FANS:
                element_triangles(t, obj_type, index_types, raw, nbytes, t->index_size,
                                  group_index);
                break;
            default:
                break;
            }
        }
    }
    if (r.overrun) fprintf(stderr, "Warnung: Datei endet frueher als der Kopf sagt\n");
    return 1;
}

/* --- backen -------------------------------------------------------------- */

struct slot { uint32_t v, n, t, index; };

struct baker {
    struct slot *slot;
    uint32_t mask;
    unsigned char *vertex;          /* je 24 Bytes, wie im Buendel */
    uint32_t nvertex, vertex_cap;
    uint32_t *index;
    uint32_t nindex, index_cap;
};

static uint32_t mix(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u;
    h ^= b + 0x85EBCA6Bu + (h << 6) + (h >> 2);
    h ^= c + 0xC2B2AE35u + (h << 6) + (h >> 2);
    h ^= h >> 15;
    return h;
}

static void baker_init(struct baker *b, uint32_t expect) {
    uint32_t cap = 1024;
    while (cap < expect * 2) cap <<= 1;
    b->slot = xrealloc(NULL, (size_t)cap * sizeof(*b->slot));
    memset(b->slot, 0xFF, (size_t)cap * sizeof(*b->slot));   /* 0xFFFFFFFF = leer */
    b->mask = cap - 1;
    b->vertex = NULL; b->nvertex = 0; b->vertex_cap = 0;
    b->index = NULL; b->nindex = 0; b->index_cap = 0;
}

static void emit_vertex(struct baker *b, const struct tile *t, struct corner c) {
    if (b->nvertex == b->vertex_cap) {
        b->vertex_cap = b->vertex_cap ? b->vertex_cap * 2 : 65536;
        b->vertex = xrealloc(b->vertex, (size_t)b->vertex_cap * 24);
    }
    unsigned char *out = b->vertex + (size_t)b->nvertex * 24;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    if (c.v < t->nvertex) memcpy(pos, t->vertex + (size_t)c.v * 3, 12);
    memcpy(out, pos, 12);

    double nx = 0.0, ny = 1.0, nz = 0.0;
    if (c.n < t->nnormal) {
        const unsigned char *n = t->normal + (size_t)c.n * 3;
        /* wie die Python-Fassung: das abgeschnittene halbe Byte zurueckgeben */
        nx = (n[0] + 0.5) / 127.5 - 1.0;
        ny = (n[1] + 0.5) / 127.5 - 1.0;
        nz = (n[2] + 0.5) / 127.5 - 1.0;
    }
    signed char q[3];
    double comp[3] = { nx, ny, nz };
    for (int i = 0; i < 3; ++i) {
        double v = floor(comp[i] * 127.0 + 0.5);
        if (v > 127.0) v = 127.0;
        if (v < -127.0) v = -127.0;
        q[i] = (signed char)v;
    }
    out[12] = (unsigned char)q[0];
    out[13] = (unsigned char)q[1];
    out[14] = (unsigned char)q[2];
    out[15] = 0;

    float uv[2] = {0.0f, 0.0f};
    if (c.t < t->ntexcoord) memcpy(uv, t->texcoord + (size_t)c.t * 2, 8);
    memcpy(out + 16, uv, 8);
    ++b->nvertex;
}

static uint32_t intern(struct baker *b, const struct tile *t, struct corner c) {
    uint32_t h = mix(c.v, c.n, c.t) & b->mask;
    for (;;) {
        struct slot *s = &b->slot[h];
        if (s->index == 0xFFFFFFFFu) {
            s->v = c.v; s->n = c.n; s->t = c.t; s->index = b->nvertex;
            emit_vertex(b, t, c);
            return s->index;
        }
        if (s->v == c.v && s->n == c.n && s->t == c.t) return s->index;
        h = (h + 1) & b->mask;
    }
}

static void push_index(struct baker *b, uint32_t i) {
    if (b->nindex == b->index_cap) {
        b->index_cap = b->index_cap ? b->index_cap * 2 : 65536;
        b->index = xrealloc(b->index, (size_t)b->index_cap * sizeof(uint32_t));
    }
    b->index[b->nindex++] = i;
}

static int write_bundle(const char *path, const struct tile *t, const struct baker *b,
                        const uint32_t *start, const uint32_t *count) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 0; }
    uint32_t wide = b->nvertex > 65535 ? 1 : 0;
    fwrite("FGB1", 1, 4, f);
    fwrite(&wide, 4, 1, f);
    fwrite(t->center, 8, 3, f);
    fwrite(&t->radius, 4, 1, f);
    uint32_t nv = b->nvertex, ni = b->nindex, ng = (uint32_t)t->ngroups;
    fwrite(&nv, 4, 1, f);
    fwrite(&ni, 4, 1, f);
    fwrite(&ng, 4, 1, f);

    uint32_t namebytes = 0;
    for (int g = 0; g < t->ngroups; ++g) namebytes += (uint32_t)strlen(t->group[g].material) + 1;
    fwrite(&namebytes, 4, 1, f);
    for (int g = 0; g < t->ngroups; ++g)
        fwrite(t->group[g].material, 1, strlen(t->group[g].material) + 1, f);
    for (int g = 0; g < t->ngroups; ++g) {
        fwrite(&start[g], 4, 1, f);
        fwrite(&count[g], 4, 1, f);
    }
    fwrite(b->vertex, 24, b->nvertex, f);
    if (wide) {
        fwrite(b->index, 4, b->nindex, f);
    } else {
        uint16_t *small = xrealloc(NULL, (size_t)b->nindex * 2);
        for (uint32_t i = 0; i < b->nindex; ++i) small[i] = (uint16_t)b->index[i];
        fwrite(small, 2, b->nindex, f);
        free(small);
    }
    fclose(f);
    return 1;
}

/* --- ausduennen ---------------------------------------------------------
 *
 * Eckpunkte auf ein Gitter zusammenziehen: alles, was in dieselbe Zelle
 * faellt, wird ein Eckpunkt (der Mittelwert), und Dreiecke, die dabei zwei
 * gleiche Ecken bekommen, fallen weg.  Das ist grob, aber es ist genau das,
 * was aus der Ferne niemand sieht - und es braucht einen Durchgang und eine
 * Streuwerttabelle, keine Kantenbewertung.
 */
struct cell {
    int32_t gx, gy, gz;
    uint32_t index;
};

struct cluster {
    struct cell *cell;
    uint32_t mask;
    float *sum;          /* je Zelle: x,y,z,nx,ny,nz,u,v */
    uint32_t *count;
    uint32_t n, cap;
};

static uint32_t cell_hash(int32_t x, int32_t y, int32_t z) {
    uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u ^ (uint32_t)z * 0xCB1AB31Fu;
    h ^= h >> 15;
    return h;
}

static uint32_t cluster_intern(struct cluster *c, int32_t gx, int32_t gy, int32_t gz,
                               const float *vertex) {
    uint32_t h = cell_hash(gx, gy, gz) & c->mask;
    for (;;) {
        struct cell *slot = &c->cell[h];
        if (slot->index == 0xFFFFFFFFu) {
            if (c->n == c->cap) {
                c->cap *= 2;
                c->sum = xrealloc(c->sum, (size_t)c->cap * 8 * sizeof(float));
                c->count = xrealloc(c->count, (size_t)c->cap * sizeof(uint32_t));
            }
            slot->gx = gx; slot->gy = gy; slot->gz = gz; slot->index = c->n;
            memset(c->sum + (size_t)c->n * 8, 0, 8 * sizeof(float));
            c->count[c->n] = 0;
            ++c->n;
        }
        if (slot->gx == gx && slot->gy == gy && slot->gz == gz) {
            float *acc = c->sum + (size_t)slot->index * 8;
            for (int i = 0; i < 8; ++i) acc[i] += vertex[i];
            c->count[slot->index]++;
            return slot->index;
        }
        h = (h + 1) & c->mask;
    }
}

static int write_lod(const char *path, const struct tile *t, const struct baker *b,
                     const uint32_t *start, const uint32_t *count, float cell_m) {
    struct cluster c;
    uint32_t cap = 1024;
    while (cap < b->nvertex * 2) cap <<= 1;
    c.cell = xrealloc(NULL, (size_t)cap * sizeof(struct cell));
    memset(c.cell, 0xFF, (size_t)cap * sizeof(struct cell));
    c.mask = cap - 1;
    c.cap = 4096; c.n = 0;
    c.sum = xrealloc(NULL, (size_t)c.cap * 8 * sizeof(float));
    c.count = xrealloc(NULL, (size_t)c.cap * sizeof(uint32_t));

    uint32_t *remap = xrealloc(NULL, (size_t)b->nvertex * sizeof(uint32_t));
    for (uint32_t i = 0; i < b->nvertex; ++i) {
        const unsigned char *v = b->vertex + (size_t)i * 24;
        float pos[3], uv[2];
        memcpy(pos, v, 12);
        memcpy(uv, v + 16, 8);
        float f[8] = {
            pos[0], pos[1], pos[2],
            (float)(signed char)v[12] / 127.0f,
            (float)(signed char)v[13] / 127.0f,
            (float)(signed char)v[14] / 127.0f,
            uv[0], uv[1]
        };
        remap[i] = cluster_intern(&c, (int32_t)floorf(pos[0] / cell_m),
                                  (int32_t)floorf(pos[1] / cell_m),
                                  (int32_t)floorf(pos[2] / cell_m), f);
    }

    /* Mittelwerte bilden und als Buendel-Eckpunkte schreiben */
    unsigned char *vertex = xrealloc(NULL, (size_t)c.n * 24);
    for (uint32_t i = 0; i < c.n; ++i) {
        const float *acc = c.sum + (size_t)i * 8;
        float inv = 1.0f / (float)c.count[i];
        float pos[3] = { acc[0] * inv, acc[1] * inv, acc[2] * inv };
        float nx = acc[3], ny = acc[4], nz = acc[5];
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; } else { nx = 0; ny = 0; nz = 1; }
        float uv[2] = { acc[6] * inv, acc[7] * inv };
        unsigned char *out = vertex + (size_t)i * 24;
        memcpy(out, pos, 12);
        out[12] = (unsigned char)(signed char)lrintf(nx * 127.0f);
        out[13] = (unsigned char)(signed char)lrintf(ny * 127.0f);
        out[14] = (unsigned char)(signed char)lrintf(nz * 127.0f);
        out[15] = 0;
        memcpy(out + 16, uv, 8);
    }

    /* Dreiecke uebernehmen, entartete weglassen */
    uint32_t *index = xrealloc(NULL, (size_t)b->nindex * sizeof(uint32_t));
    uint32_t lod_start[MAX_GROUPS], lod_count[MAX_GROUPS], n = 0;
    for (int g = 0; g < t->ngroups; ++g) {
        lod_start[g] = n;
        for (uint32_t i = start[g]; i + 2 < start[g] + count[g]; i += 3) {
            uint32_t a = remap[b->index[i]], bb = remap[b->index[i + 1]], cc = remap[b->index[i + 2]];
            if (a == bb || bb == cc || a == cc) continue;
            index[n++] = a; index[n++] = bb; index[n++] = cc;
        }
        lod_count[g] = n - lod_start[g];
    }

    struct baker lod = { NULL, 0, vertex, c.n, c.n, index, n, n };
    int ok = write_bundle(path, t, &lod, lod_start, lod_count);
    printf("  ausgeduennt    %u Eckpunkte, %u Dreiecke (%.0f %% weg, Zelle %.0f m)\n",
           c.n, n / 3, 100.0 * (1.0 - (double)n / (double)b->nindex), cell_m);
    free(c.cell); free(c.sum); free(c.count); free(remap); free(vertex); free(index);
    return ok;
}

/* --- die Kachel einmal von oben malen ------------------------------------
 *
 * Ein Bild je Kachel, Farbe je Material: damit braucht die Ferne keine
 * Dreiecke mehr, um auszusehen wie etwas, und aus siebzehn Zeichenaufrufen
 * wird einer.  Die Farben kommen aus matcolors.py - Mittelwert der Textur,
 * die FlightGear fuer das Material benutzt.
 */
struct palette {
    char name[48];
    unsigned char rgb[3];
};

static struct palette *load_palette(const char *path, int *n_out) {
    FILE *f = fopen(path, "r");
    if (!f) { *n_out = 0; return NULL; }
    struct palette *p = NULL;
    int n = 0, cap = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char name[48];
        int r, g, b;
        if (sscanf(line, "%47s %d %d %d", name, &r, &g, &b) != 4) continue;
        if (n == cap) { cap = cap ? cap * 2 : 64; p = xrealloc(p, (size_t)cap * sizeof(*p)); }
        snprintf(p[n].name, sizeof(p[n].name), "%s", name);
        p[n].rgb[0] = (unsigned char)r;
        p[n].rgb[1] = (unsigned char)g;
        p[n].rgb[2] = (unsigned char)b;
        ++n;
    }
    fclose(f);
    *n_out = n;
    return p;
}

static void palette_lookup(const struct palette *p, int n, const char *name, unsigned char out[3]) {
    out[0] = 90; out[1] = 110; out[2] = 70;            /* sonst Wiese */
    for (int i = 0; i < n; ++i)
        if (!strcmp(p[i].name, name)) { memcpy(out, p[i].rgb, 3); return; }
    /* Die Belaege eines Flugplatzes heissen in FlightGear pa_* (Asphalt),
       pc_* (Beton) und lf_* (aufgemalte Linie), und es sind Dutzende - jede
       Ziffer der Bahnkennung ein eigenes Material.  Steht der Name nicht in
       der Tafel, entscheidet die Familie; sonst wuerde ausgerechnet die Bahn
       als Wiese gebacken. */
    if (!strncmp(name, "pa_", 3)) { out[0] = 73;  out[1] = 70;  out[2] = 67; }
    else if (!strncmp(name, "pc_", 3)) { out[0] = 155; out[1] = 156; out[2] = 153; }
    else if (!strncmp(name, "lf_", 3)) { out[0] = 184; out[1] = 168; out[2] = 61; }
}

static uint16_t rgb565(const unsigned char c[3]) {
    return (uint16_t)(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3));
}

/* Ein Dreieck fuellen - Kantenfunktionen, ganzzahlig genug fuer 512 Punkte. */
/* Was ist ein Band?  Strassen, Bahnen, Baeche und Ortsgrenzen sind als
   schmale Streifen in die Triangulierung geschnitten - die sollen scharf
   bleiben.  Die Landbedeckung darunter darf ineinander laufen. */
static int is_band(const char *name) {
    static const char *const band[] = {
        "Road", "Freeway", "Railroad", "Stream", "Canal", "River", "Watercourse",
        "Airport", "pa_", "pc_", "lf_", "rwy", "taxiway", "Asphalt"
    };
    for (size_t i = 0; i < sizeof(band) / sizeof(band[0]); ++i)
        if (strstr(name, band[i])) return 1;
    return 0;
}

/* Die Uebergaenge weich machen - das ist der eine Kniff, mit dem X-Planes
   Boden besser aussieht als unserer: dort blendet eine zweite Textur als
   Rampe zwischen zwei Gelaendearten (`#if BORDER` in terrain.glsl).  Wir
   haben nur ein gebackenes Bild, also verwischen wir die Grenzen darin - aber
   **nur die der Landbedeckung**, bevor Strassen und Baeche darueberkommen.
   Ein gewichteter 3x3-Kasten, zweimal: das reicht bei 36 m je Bildpunkt fuer
   einen Saum von gut hundert Metern. */
static void soften(uint16_t *img, int size, int passes) {
    uint16_t *tmp = xrealloc(NULL, (size_t)size * size * 2);
    for (int pass = 0; pass < passes; ++pass) {
        memcpy(tmp, img, (size_t)size * size * 2);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int r = 0, g = 0, b = 0, n = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= size) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= size) continue;
                        int w = (dx == 0 && dy == 0) ? 4 : 1;
                        uint16_t v = tmp[yy * size + xx];
                        r += ((v >> 11) & 0x1F) * w;
                        g += ((v >> 5) & 0x3F) * w;
                        b += (v & 0x1F) * w;
                        n += w;
                    }
                }
                img[y * size + x] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
            }
        }
    }
    free(tmp);
}

static void fill_triangle(uint16_t *img, int size, const float *x, const float *y, uint16_t colour) {
    float minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
    for (int i = 1; i < 3; ++i) {
        if (x[i] < minx) minx = x[i];
        if (x[i] > maxx) maxx = x[i];
        if (y[i] < miny) miny = y[i];
        if (y[i] > maxy) maxy = y[i];
    }
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x1 < 0 || y1 < 0 || x0 >= size || y0 >= size) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > size - 1) x1 = size - 1;
    if (y1 > size - 1) y1 = size - 1;

    float area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
    if (fabsf(area) < 1e-6f) {
        /* entartet, aber sichtbar: als Strich zeichnen, damit Strassen
           und Baeche nicht verschwinden */
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;
            int steps = (int)(fabsf(x[j] - x[i]) + fabsf(y[j] - y[i])) + 1;
            for (int s = 0; s <= steps; ++s) {
                int px = (int)(x[i] + (x[j] - x[i]) * s / steps);
                int py = (int)(y[i] + (y[j] - y[i]) * s / steps);
                if (px >= 0 && py >= 0 && px < size && py < size) img[py * size + px] = colour;
            }
        }
        return;
    }
    float inv = 1.0f / area;
    for (int py = y0; py <= y1; ++py) {
        float fy = py + 0.5f;
        for (int px = x0; px <= x1; ++px) {
            float fx = px + 0.5f;
            float w0 = ((x[1] - fx) * (y[2] - fy) - (y[1] - fy) * (x[2] - fx)) * inv;
            float w1 = ((x[2] - fx) * (y[0] - fy) - (y[2] - fy) * (x[0] - fx)) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            img[py * size + px] = colour;
        }
    }
}

static int write_texture(const char *path, const struct tile *t, const struct baker *b,
                         const uint32_t *start, const uint32_t *count,
                         const struct palette *pal, int npal, int size) {
    /* oertliches System der Kachel - dieselbe Rechnung wie im Renderer */
    double cx = t->center[0], cy = t->center[1], cz = t->center[2];
    double len = sqrt(cx * cx + cy * cy + cz * cz);
    double up[3] = { cx / len, cy / len, cz / len };
    double east[3] = { -up[1], up[0], 0.0 };
    double e = sqrt(east[0] * east[0] + east[1] * east[1]);
    if (e < 1e-9) { east[0] = 1.0; east[1] = 0.0; e = 1.0; }
    east[0] /= e; east[1] /= e;
    double north[3] = {
        up[1] * east[2] - up[2] * east[1],
        up[2] * east[0] - up[0] * east[2],
        up[0] * east[1] - up[1] * east[0]
    };

    float *lx = xrealloc(NULL, (size_t)b->nvertex * sizeof(float));
    float *ly = xrealloc(NULL, (size_t)b->nvertex * sizeof(float));
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    for (uint32_t i = 0; i < b->nvertex; ++i) {
        float p[3];
        memcpy(p, b->vertex + (size_t)i * 24, 12);
        lx[i] = (float)(p[0] * east[0] + p[1] * east[1] + p[2] * east[2]);
        ly[i] = (float)(p[0] * north[0] + p[1] * north[1] + p[2] * north[2]);
        if (lx[i] < minx) minx = lx[i];
        if (lx[i] > maxx) maxx = lx[i];
        if (ly[i] < miny) miny = ly[i];
        if (ly[i] > maxy) maxy = ly[i];
    }
    float span = (maxx - minx) > (maxy - miny) ? (maxx - minx) : (maxy - miny);
    if (span < 1.0f) span = 1.0f;

    uint16_t *img = xrealloc(NULL, (size_t)size * size * 2);
    unsigned char grass[3] = { 90, 110, 70 };
    uint16_t fill = rgb565(grass);
    for (int i = 0; i < size * size; ++i) img[i] = fill;

    /* Von hinten nach vorn: die Landbedeckung steht in der Datei hinter den
       Baendern, also muss sie zuerst - dann liegen Strassen obenauf.  Und
       dazwischen werden die Grenzen der Landbedeckung weich gemacht: ein Feld
       hoert nicht an einer geraden Linie auf, ein Fluss schon. */
    for (int runde = 0; runde < 2; ++runde) {
        for (int g = t->ngroups - 1; g >= 0; --g) {
            if (is_band(t->group[g].material) != runde) continue;
            unsigned char rgb[3];
            palette_lookup(pal, npal, t->group[g].material, rgb);
            uint16_t colour = rgb565(rgb);
            for (uint32_t i = start[g]; i + 2 < start[g] + count[g]; i += 3) {
                float px[3], py[3];
                for (int k = 0; k < 3; ++k) {
                    uint32_t v = b->index[i + k];
                    px[k] = (lx[v] - minx) / span * (size - 1);
                    py[k] = (size - 1) - (ly[v] - miny) / span * (size - 1);
                }
                fill_triangle(img, size, px, py, colour);
            }
        }
        if (runde == 0) soften(img, size, 2);
    }

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); free(img); free(lx); free(ly); return 0; }
    uint32_t w = (uint32_t)size, h = (uint32_t)size, format = 0;   /* 0 = RGB565 */
    fwrite("FGT1", 1, 4, f);
    fwrite(&w, 4, 1, f);
    fwrite(&h, 4, 1, f);
    fwrite(&format, 4, 1, f);
    float org[2] = { minx, miny }, scale = span;
    fwrite(org, 4, 2, f);
    fwrite(&scale, 4, 1, f);
    fwrite(img, 2, (size_t)size * size, f);
    fclose(f);
    printf("  Bild           %d x %d, %.0f m je Punkt, %.0f KB\n",
           size, size, span / size, (double)(size * size * 2) / 1024.0);
    free(img); free(lx); free(ly);
    return 1;
}

static unsigned char *read_gz(const char *path, size_t *size_out) {
    gzFile gz = gzopen(path, "rb");
    if (!gz) { perror(path); return NULL; }
    size_t cap = 1 << 20, size = 0;
    unsigned char *buf = xrealloc(NULL, cap);
    for (;;) {
        if (size == cap) { cap *= 2; buf = xrealloc(buf, cap); }
        int got = gzread(gz, buf + size, (unsigned)(cap - size));
        if (got <= 0) break;
        size += (size_t)got;
    }
    gzclose(gz);
    *size_out = size;
    return buf;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    double t_start = now_s();
    if (argc < 3) {
        fprintf(stderr, "Aufruf: btgbake <kachel.btg.gz> <ziel.fgb> [zellgroesse_m]"
                        " [farbtafel.txt] [bildgroesse]\n");
        return 2;
    }
    size_t size = 0;
    unsigned char *data = read_gz(argv[1], &size);
    if (!data) return 3;

    struct tile tile;
    if (!parse(&tile, data, size, 0)) { free(data); return 4; }
    if (indices_out_of_range(&tile)) {
        int other = tile.index_size == 4 ? 2 : 4;
        fprintf(stderr, "Indizes passen nicht, noch einmal mit %d Bit\n", other * 8);
        tile_free(&tile);
        if (!parse(&tile, data, size, other)) { free(data); return 4; }
    }
    free(data);

    struct baker baker;
    baker_init(&baker, tile.ncorner ? tile.ncorner : 1024);

    uint32_t start[MAX_GROUPS], count[MAX_GROUPS];
    for (int g = 0; g < tile.ngroups; ++g) {
        start[g] = baker.nindex;
        for (uint32_t i = 0; i < tile.ncorner; ++i)
            if (tile.corner_group[i] == g)
                push_index(&baker, intern(&baker, &tile, tile.corner[i]));
        count[g] = baker.nindex - start[g];
    }

    if (!write_bundle(argv[2], &tile, &baker, start, count)) return 5;

    /* Bild der Kachel, wenn eine Farbtafel mitgegeben wurde */
    if (argc > 4) {
        int npal = 0;
        struct palette *pal = load_palette(argv[4], &npal);
        int size = argc > 5 ? atoi(argv[5]) : 512;
        if (size < 64) size = 64;
        if (npal == 0) fprintf(stderr, "Farbtafel %s leer oder fehlt\n", argv[4]);
        char tex_path[1024];
        const char *dot = strrchr(argv[2], '.');
        int stem = dot ? (int)(dot - argv[2]) : (int)strlen(argv[2]);
        snprintf(tex_path, sizeof(tex_path), "%.*s.tex", stem, argv[2]);
        write_texture(tex_path, &tile, &baker, start, count, pal, npal, size);
        /* Die grobe Fassung bekommt ein eigenes, kleineres Bild: sie wird nur
           aus der Ferne gezeichnet, und 8 MB je Kachel haette das Geraet nicht. */
        if (size > 512) {
            snprintf(tex_path, sizeof(tex_path), "%.*s.lod.tex", stem, argv[2]);
            write_texture(tex_path, &tile, &baker, start, count, pal, npal, 512);
        }
        free(pal);
    }

    /* Zweite, grobe Fassung fuer die Ferne: <name>.lod.fgb */
    if (argc > 3) {
        float cell = (float)atof(argv[3]);
        if (cell > 0.0f) {
            char lod_path[1024];
            const char *dot = strrchr(argv[2], '.');
            int stem = dot ? (int)(dot - argv[2]) : (int)strlen(argv[2]);
            snprintf(lod_path, sizeof(lod_path), "%.*s.lod%s", stem, argv[2], dot ? dot : ".fgb");
            write_lod(lod_path, &tile, &baker, start, count, cell);
        }
    }

    double elapsed = now_s() - t_start;
    printf("%s, Version %d, %.0f ms\n", argv[1], tile.version, elapsed * 1000.0);
    printf("  gelesen        %u Eckpunkte, %u Normalen, %u Texturkoordinaten\n",
           tile.nvertex, tile.nnormal, tile.ntexcoord);
    printf("  Gruppen        %d Materialien, %u Dreiecke\n", tile.ngroups, tile.ncorner / 3);
    printf("  gebacken       %u Eckpunkte, %u Indizes, %d Zeichenaufrufe\n",
           baker.nvertex, baker.nindex, tile.ngroups);
    /* Der Spitzenspeicher ist hier der eigentliche Punkt - deshalb steht er
       in der Ausgabe und nicht in einem Messwerkzeug daneben. */
    FILE *st = fopen("/proc/self/status", "r");
    if (st) {
        char line[128];
        while (fgets(line, sizeof(line), st))
            if (!strncmp(line, "VmHWM:", 6)) { printf("  Speicher      %s", line + 6); break; }
        fclose(st);
    }
    free(baker.slot); free(baker.vertex); free(baker.index);
    tile_free(&tile);
    return 0;
}
