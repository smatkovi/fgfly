#include "terrain.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* Modelle bringen ihre Texturkoordinate mit - anders als das Gelaende, wo sie
   aus dem Ort faellt.  Deshalb ein zweites, ebenso kleines Programm. */
static const char *MODEL_VERT =
    "uniform mat4 u_mvp;\n"
    "uniform lowp vec3 u_light;\n"
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_nrm;\n"
    "attribute vec2 a_uv;\n"
    "varying lowp float v_shade;\n"
    "varying mediump vec2 v_uv;\n"
    "void main() {\n"
    "  v_shade = 0.45 + 0.55 * max(dot(normalize(a_nrm), u_light), 0.0);\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char *MODEL_FRAG =
    "uniform lowp vec3 u_col;\n"
    "uniform sampler2D u_tex;\n"
    "varying lowp float v_shade;\n"
    "varying mediump vec2 v_uv;\n"
    "void main() { gl_FragColor = vec4(u_col * texture2D(u_tex, v_uv).rgb * v_shade, 1.0); }\n";

/* Die Texturkoordinate kommt aus dem Ort: das Bild liegt flach ueber der
   Kachel, also braucht kein Eckpunkt sie mitzuschleppen. */
static const char *TERRAIN_VERT =
    "uniform mat4 u_mvp;\n"
    "uniform lowp vec3 u_light;\n"
    "uniform vec3 u_texmap;\n"
    "uniform mediump float u_detail;\n"
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_nrm;\n"
    "varying lowp float v_shade;\n"
    "varying mediump vec2 v_uv;\n"
    "varying mediump vec2 v_grain;\n"
    "void main() {\n"
    "  v_shade = 0.45 + 0.55 * max(dot(normalize(a_nrm), u_light), 0.0);\n"
    "  v_uv = vec2((a_pos.x - u_texmap.x) * u_texmap.z,\n"
    "              1.0 - (a_pos.y - u_texmap.y) * u_texmap.z);\n"
    /* Der Ort geht bis 11 km; mediump traegt davon keine Nachkommastellen
       mehr.  Deshalb vorher auf einen kleinen Bereich zurueckfalten - zehn
       Wiederholungen je 600 m, was an der Naht glatt aufgeht. */
    "  v_grain = fract(a_pos.xy * u_detail) * 10.0;\n"
    "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

/* u_col ist weiss, wenn die Kachel ein Bild hat, und sonst die Materialfarbe -
   dann liegt eine weisse Textur von einem Punkt darunter.  So bleibt es ein
   Programm ohne Verzweigung im Fragment. */
static const char *TERRAIN_FRAG =
    "uniform lowp vec3 u_col;\n"
    "uniform sampler2D u_tex;\n"
    "varying lowp float v_shade;\n"
    "varying mediump vec2 v_uv;\n"
    "varying mediump vec2 v_grain;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(u_col * texture2D(u_tex, v_uv).rgb * v_shade, 1.0);\n"
    "}\n";

/* Bis eine gebackene Bildkachel darunterliegt, faerbt der Materialname.
   Die Namen sind FlightGears eigene (Materials/dds/global-summer.xml). */
static void material_colour(const char *name, float out[3]) {
    struct { const char *key; float r, g, b; } table[] = {
        /* Zuerst der Flugplatz, denn hier wird nach Teilzeichenfolge gesucht
           und der erste Treffer gilt.  FlightGears Belaege heissen pa_* fuer
           Asphalt, pc_* fuer Beton und lf_* fuer die aufgemalten Linien; die
           Werte sind die aus ../bake/materials.txt, durch 255 geteilt. */
        { "grass_rwy",       0.26f, 0.29f, 0.14f },
        { "dirt_rwy",        0.58f, 0.51f, 0.38f },
        { "lakebed_taxiway", 0.59f, 0.55f, 0.38f },
        { "_white",          0.66f, 0.66f, 0.66f },   /* Linien nach ihrer Farbe */
        { "_red",            0.71f, 0.29f, 0.29f },
        { "_blue",           0.29f, 0.40f, 0.71f },
        { "_green",          0.29f, 0.60f, 0.35f },
        { "_orange",         0.58f, 0.32f, 0.11f },
        { "_yellow",         0.72f, 0.66f, 0.24f },
        { "lf_",             0.72f, 0.66f, 0.24f },
        { "pa_",             0.29f, 0.27f, 0.26f },   /* Asphalt 73/70/67 */
        { "pc_",             0.61f, 0.61f, 0.60f },   /* Beton 155/156/153 */
        { "Asphalt",         0.29f, 0.27f, 0.26f },
        { "Freeway",    0.32f, 0.32f, 0.34f },
        { "Road",       0.38f, 0.37f, 0.36f },
        { "Railroad",   0.42f, 0.38f, 0.34f },
        { "Water",      0.18f, 0.34f, 0.52f },
        { "Lake",       0.18f, 0.34f, 0.52f },
        { "Stream",     0.20f, 0.38f, 0.55f },
        { "Canal",      0.20f, 0.38f, 0.55f },
        { "Urban",      0.45f, 0.42f, 0.40f },
        { "Town",       0.50f, 0.46f, 0.42f },
        { "Industrial", 0.44f, 0.42f, 0.44f },
        { "Forest",     0.18f, 0.34f, 0.16f },
        { "Crop",       0.52f, 0.52f, 0.28f },
        { "Grass",      0.38f, 0.52f, 0.26f },
        { "Vineyard",   0.40f, 0.46f, 0.24f },
        { "Mining",     0.48f, 0.44f, 0.36f },
        { "Airport",    0.35f, 0.35f, 0.33f },
    };
    out[0] = 0.34f; out[1] = 0.46f; out[2] = 0.24f;      /* sonst Wiese */
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (strstr(name, table[i].key)) {
            out[0] = table[i].r; out[1] = table[i].g; out[2] = table[i].b;
            return;
        }
}

/* Das Bild zur Kachel: <name>.tex neben <name>.fgb.  Die grobe Fassung
   heisst <name>.lod.fgb und teilt sich das Bild mit der feinen. */
static void load_texture_file(struct terrain *t, const char *tex_path, const double off[3]);

static void load_texture(struct terrain *t, const char *path, const double off[3]) {
    char tex_path[1024];
    const char *dot = strrchr(path, '.');
    int stem = dot ? (int)(dot - path) : (int)strlen(path);
    snprintf(tex_path, sizeof(tex_path), "%.*s.tex", stem, path);
    if (access(tex_path, R_OK) != 0 && stem > 4 && !strncmp(path + stem - 4, ".lod", 4)) {
        /* keine eigene Fassung - dann die der feinen Kachel */
        snprintf(tex_path, sizeof(tex_path), "%.*s.tex", stem - 4, path);
    }
    load_texture_file(t, tex_path, off);
}

/* Ein Bild aus einer .tex-Datei: RGB565 mit kleinem Kopf, dazu die Abbildung
   vom Ort auf die Texturkoordinate (bei Modellen ungenutzt). */
static void load_texture_file(struct terrain *t, const char *tex_path, const double off[3]) {
    FILE *f = fopen(tex_path, "rb");
    if (!f) return;
    char magic[4];
    uint32_t w = 0, h = 0, format = 0;
    float org[2] = {0.0f, 0.0f}, span = 1.0f;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "FGT1", 4) ||
        fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 || fread(&format, 4, 1, f) != 1 ||
        fread(org, 4, 2, f) != 2 || fread(&span, 4, 1, f) != 1) {
        fclose(f);
        return;
    }
    size_t bytes = (size_t)w * h * 2;
    unsigned char *px = malloc(bytes);
    if (!px || fread(px, 1, bytes, f) != bytes) { free(px); fclose(f); return; }
    fclose(f);

    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)w, (GLsizei)h, 0, GL_RGB,
                 GL_UNSIGNED_SHORT_5_6_5, px);
    /* **Nur Zweierpotenzen duerfen Stufen und GL_REPEAT haben.** Eine Textur
       mit krummen Kanten gilt in ES 2.0 sonst als unvollstaendig, und
       unvollstaendig heisst schwarz - so waren die Tragflaechen des A320
       (2133 x 2133 im Hangar) schwarz. */
    int pot = w && h && (w & (w - 1)) == 0 && (h & (h - 1)) == 0;
    if (pot) {
        /* Ohne Verkleinerungsstufen wird der Boden im flachen Blick zu Brei:
           ein Bildpunkt deckt dort Dutzende Bildpunkte der Textur ab. */
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        printf("  %ux%u ist keine Zweierpotenz - ohne Stufen, festgeklemmt\n", w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    free(px);

    /* Das Bild ist im System der Kachel gemalt, gezeichnet wird im System der
       ersten - der Versatz gehoert also dazu.  Die Drehung zwischen beiden
       (ueber 30 km etwa 0,3 Grad) wird vernachlaessigt; das ist gut ein
       Bildpunkt. */
    t->texmap[0] = (float)(org[0] + off[0]);
    t->texmap[1] = (float)(org[1] + off[1]);
    t->texmap[2] = 1.0f / span;
    printf("  Bild %ux%u, %.0f m je Punkt\n", w, h, span / (float)w);
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "Gelaende-Shader: %s\n", log);
        return 0;
    }
    return sh;
}

static void frame_init(struct terrain_frame *f, const double center[3]) {
    double len = sqrt(center[0] * center[0] + center[1] * center[1] + center[2] * center[2]);
    for (int i = 0; i < 3; ++i) {
        f->origin[i] = center[i];
        f->up[i] = center[i] / len;
    }
    f->east[0] = -f->up[1];
    f->east[1] = f->up[0];
    f->east[2] = 0.0;
    double e = sqrt(f->east[0] * f->east[0] + f->east[1] * f->east[1]);
    if (e < 1e-9) { f->east[0] = 1.0; f->east[1] = 0.0; e = 1.0; }
    f->east[0] /= e; f->east[1] /= e;
    f->north[0] = f->up[1] * f->east[2] - f->up[2] * f->east[1];
    f->north[1] = f->up[2] * f->east[0] - f->up[0] * f->east[2];
    f->north[2] = f->up[0] * f->east[1] - f->up[1] * f->east[0];
    f->set = 1;
}

/* Ein Bild je Texturgruppe: acbake.py legt sie als <stamm>.<textur>.tex ab. */
static void load_group_textures(struct terrain *t, const char *path) {
    const char *dot = strrchr(path, '.');
    int stem = dot ? (int)(dot - path) : (int)strlen(path);
    for (int g = 0; g < t->ngroups; ++g) {
        /* Seit die beweglichen Teile eigene Gruppen haben, kommt dieselbe
           Textur mehrfach vor - dann nur einmal laden, sonst liegen zwei
           Megabyte fuenfmal im Speicher. */
        int seen = -1;
        for (int k = 0; k < g && seen < 0; ++k)
            if (!strcmp(t->group[k].name, t->group[g].name)) seen = k;
        if (seen >= 0) { t->group[g].tex = t->group[seen].tex; continue; }
        char name[32];
        snprintf(name, sizeof(name), "%s", t->group[g].name);
        char *ext = strrchr(name, '.');
        if (ext) *ext = '\0';
        char tex_path[1024];
        snprintf(tex_path, sizeof(tex_path), "%.*s.%s.tex", stem, path, name);
        struct terrain tmp;
        memset(&tmp, 0, sizeof(tmp));
        double zero[3] = {0.0, 0.0, 0.0};
        load_texture_file(&tmp, tex_path, zero);
        t->group[g].tex = tmp.tex;
    }
}

int terrain_load(struct terrain *t, const char *path, struct terrain_frame *frame) {
    memset(t, 0, sizeof(*t));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    char magic[4];
    unsigned int flags = 0, nv = 0, ni = 0, ng = 0, namebytes = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "FGB1", 4)) { fclose(f); return 0; }
    if (fread(&flags, 4, 1, f) != 1) { fclose(f); return 0; }
    if (fread(t->center, 8, 3, f) != 3) { fclose(f); return 0; }
    if (fread(&t->radius, 4, 1, f) != 1) { fclose(f); return 0; }
    if (fread(&nv, 4, 1, f) != 1 || fread(&ni, 4, 1, f) != 1 || fread(&ng, 4, 1, f) != 1) {
        fclose(f); return 0;
    }
    if (fread(&namebytes, 4, 1, f) != 1) { fclose(f); return 0; }
    t->wide = (flags & 1) != 0;
    t->nvertices = (int)nv;
    t->nindices = (int)ni;
    t->ngroups = (int)(ng < TERRAIN_GROUPS ? ng : TERRAIN_GROUPS);

    char *names = malloc(namebytes + 1);
    if (!names || fread(names, 1, namebytes, f) != namebytes) { free(names); fclose(f); return 0; }
    names[namebytes] = '\0';
    const char *p = names;
    for (int i = 0; i < (int)ng; ++i) {
        unsigned int start = 0, count = 0;
        if (fread(&start, 4, 1, f) != 1 || fread(&count, 4, 1, f) != 1) { free(names); fclose(f); return 0; }
        if (i < t->ngroups) {
            t->group[i].start = (int)start;
            t->group[i].count = (int)count;
            snprintf(t->group[i].name, sizeof(t->group[i].name), "%s", p);
            material_colour(p, t->group[i].col);
        }
        p += strlen(p) + 1;
        if (p > names + namebytes) p = names + namebytes;
    }
    free(names);

    size_t vbytes = (size_t)nv * 24;
    unsigned char *raw = malloc(vbytes);
    if (!raw || fread(raw, 1, vbytes, f) != vbytes) { free(raw); fclose(f); return 0; }

    size_t ibytes = (size_t)ni * (t->wide ? 4 : 2);
    unsigned char *idx = malloc(ibytes);
    if (!idx || fread(idx, 1, ibytes, f) != ibytes) { free(raw); free(idx); fclose(f); return 0; }

    /* Anhang (Flagge 2): je Gruppe die Nummer ihrer Drehung, dann die
       Drehungen - Klappen und Ruder.  Wer den Anhang nicht kennt, hoert
       vorher auf, deshalb steht er ganz hinten. */
    for (int i = 0; i < TERRAIN_GROUPS; ++i) t->group[i].anim = -1;
    t->nanim = 0;
    if (flags & 2) {
        uint32_t na = 0;
        if (fread(&na, 4, 1, f) == 1 && na <= TERRAIN_GROUPS) {
            for (unsigned int i = 0; i < ng; ++i) {
                int32_t a = -1;
                if (fread(&a, 4, 1, f) != 1) break;
                if (i < (unsigned int)t->ngroups) t->group[i].anim = a;
            }
            for (uint32_t i = 0; i < na; ++i) {
                uint32_t kind = 0;
                float v[7];
                if (fread(&kind, 4, 1, f) != 1 || fread(v, 4, 7, f) != 7) break;
                t->anim[i].kind = (int)kind;
                for (int k = 0; k < 3; ++k) t->anim[i].p1[k] = v[k];
                for (int k = 0; k < 3; ++k) t->anim[i].p2[k] = v[3 + k];
                t->anim[i].factor = v[6];
                ++t->nanim;
            }
        }
    }
    fclose(f);

    t->is_model = (t->center[0] == 0.0 && t->center[1] == 0.0 && t->center[2] == 0.0);

    /* Erdfest -> oertlich, im System der ersten Kachel.  Die erste legt es
       fest; jede weitere kommt um den Abstand der Mittelpunkte versetzt. */
    if (!t->is_model && !frame->set) frame_init(frame, t->center);
    if (t->is_model && !frame->set) memset(frame, 0, sizeof(*frame));
    static const double ident_e[3] = {1, 0, 0}, ident_n[3] = {0, 1, 0}, ident_u[3] = {0, 0, 1};
    const double *east = t->is_model ? ident_e : frame->east;
    const double *north = t->is_model ? ident_n : frame->north;
    const double *up = t->is_model ? ident_u : frame->up;
    double d[3] = {
        t->center[0] - frame->origin[0],
        t->center[1] - frame->origin[1],
        t->center[2] - frame->origin[2]
    };
    double off[3] = {
        d[0] * east[0] + d[1] * east[1] + d[2] * east[2],
        d[0] * north[0] + d[1] * north[1] + d[2] * north[2],
        d[0] * up[0] + d[1] * up[1] + d[2] * up[2]
    };
    if (t->is_model) off[0] = off[1] = off[2] = 0.0;
    t->local_center[0] = (float)off[0];
    t->local_center[1] = (float)off[1];
    t->local_center[2] = (float)off[2];
    snprintf(t->name, sizeof(t->name), "%s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);

    /* Aufgeraeumtes Format fuer die GPU: drei Gleitkommazahlen Ort, drei Bytes
       Normale - 16 Bytes je Eckpunkt statt 24.  Modelle behalten die 24, denn
       ihre Texturkoordinate steht im Eckpunkt und nicht im Ort. */
    size_t vstride = t->is_model ? 24 : 16;
    unsigned char *gpu = malloc((size_t)nv * vstride);
    if (!gpu) { free(raw); free(idx); return 0; }
    for (unsigned int i = 0; i < nv; ++i) {
        const unsigned char *src = raw + (size_t)i * 24;
        float px, py, pz;
        memcpy(&px, src + 0, 4); memcpy(&py, src + 4, 4); memcpy(&pz, src + 8, 4);
        signed char nx = (signed char)src[12], ny = (signed char)src[13], nz = (signed char)src[14];
        float *out = (float *)(gpu + (size_t)i * vstride);
        if (t->is_model) memcpy(gpu + (size_t)i * vstride + 16, src + 16, 8);
        out[0] = (float)(px * east[0] + py * east[1] + pz * east[2] + off[0]);
        out[1] = (float)(px * north[0] + py * north[1] + pz * north[2] + off[1]);
        out[2] = (float)(px * up[0] + py * up[1] + pz * up[2] + off[2]);
        signed char *n = (signed char *)(gpu + (size_t)i * vstride + 12);
        n[0] = (signed char)(nx * east[0] + ny * east[1] + nz * east[2]);
        n[1] = (signed char)(nx * north[0] + ny * north[1] + nz * north[2]);
        n[2] = (signed char)(nx * up[0] + ny * up[1] + nz * up[2]);
    }
    free(raw);

    /* Der tiefste Punkt des Modells: dort stehen die Raeder.  Das Flugzeug
       wird um diesen Betrag angehoben gezeichnet, sonst steckt das Fahrwerk
       im Boden - beim A320 sind es 2,8 m, bei der c172 gut einer. */
    {
        float low = 1e30f;
        for (unsigned int i = 0; i < nv; ++i) {
            const float *p = (const float *)(gpu + (size_t)i * vstride);
            if (p[2] < low) low = p[2];
        }
        t->low = low < 1e29f ? low : 0.0f;
    }

    /* Wie hoch liegt der Boden in der Mitte?  Ohne das setzt ein Start "am
       Boden" die Kamera auf null - und die Kachel liegt hier 180 m hoeher,
       also sieht man von unten nur noch die Berge am Rand. */
    {
        /* Der hoechste Punkt im Umkreis von 200 m, nicht der mittlere: beim
           Mittelwert steht man im Gelaende statt darauf. */
        float top = -1e30f;
        for (unsigned int i = 0; i < nv; ++i) {
            const float *p = (const float *)(gpu + (size_t)i * vstride);
            float dx = p[0] - t->local_center[0], dy = p[1] - t->local_center[1];
            if (dx * dx + dy * dy < 200.0f * 200.0f && p[2] > top) top = p[2];
        }
        t->centre_height = top > -1e29f ? top : 0.0f;

        /* Dasselbe fuer die ganze Kachel, nur grob: ein Raster aus 64 x 64
           Zellen, in jeder der hoechste Eckpunkt.  Das sind bei einer Kachel
           von 12 km rund 190 m je Zelle und 16 KB Speicher - genug, damit das
           Flugzeug dem Gelaende folgt und an einem Berg aufsetzt. */
        if (!t->is_model && nv > 0) {
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            for (unsigned int i = 0; i < nv; ++i) {
                const float *p = (const float *)(gpu + (size_t)i * vstride);
                if (p[0] < x0) x0 = p[0];
                if (p[0] > x1) x1 = p[0];
                if (p[1] < y0) y0 = p[1];
                if (p[1] > y1) y1 = p[1];
            }
            int n = 64;
            float span = (x1 - x0) > (y1 - y0) ? (x1 - x0) : (y1 - y0);
            if (span < 1.0f) span = 1.0f;
            t->grid = malloc((size_t)n * n * sizeof(float));
            if (t->grid) {
                t->grid_n = n;
                t->grid_x0 = x0;
                t->grid_y0 = y0;
                t->grid_cell = span / n;
                for (int i = 0; i < n * n; ++i) t->grid[i] = -1e30f;
                for (unsigned int i = 0; i < nv; ++i) {
                    const float *p = (const float *)(gpu + (size_t)i * vstride);
                    int cx = (int)((p[0] - x0) / t->grid_cell);
                    int cy = (int)((p[1] - y0) / t->grid_cell);
                    if (cx < 0 || cy < 0 || cx >= n || cy >= n) continue;
                    if (p[2] > t->grid[cy * n + cx]) t->grid[cy * n + cx] = p[2];
                }
            }
        }
        float lo = 1e30f, hi = -1e30f;
        for (unsigned int i = 0; i < nv; ++i) {
            const float *p = (const float *)(gpu + (size_t)i * vstride);
            if (p[2] < lo) lo = p[2];
            if (p[2] > hi) hi = p[2];
        }
        if (getenv("TERRAIN_DEBUG"))
            printf("  Hoehen %.0f..%.0f m, Mitte %.0f m\n", lo, hi, t->centre_height);
    }

    glGenBuffers(1, &t->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * (GLsizeiptr)vstride, gpu, GL_STATIC_DRAW);
    t->vbytes = (unsigned int)((size_t)nv * vstride);
    free(gpu);

    /* Ein Index, der aus der Eckpunktliste zeigt, bringt den Treiber der SGX
       zum Abbruch ("Offset to VBO out of bounds") - also hier nachsehen. */
    {
        unsigned int worst = 0;
        if (t->wide) {
            const uint32_t *p = (const uint32_t *)idx;
            for (int i = 0; i < t->nindices; ++i) if (p[i] > worst) worst = p[i];
        } else {
            const uint16_t *p = (const uint16_t *)idx;
            for (int i = 0; i < t->nindices; ++i) if (p[i] > worst) worst = p[i];
        }
        if (worst >= nv) {
            fprintf(stderr, "%s: Index %u bei nur %u Eckpunkten - Buendel kaputt\n",
                    path, worst, nv);
            free(idx);
            return 0;
        }
    }

    glGenBuffers(1, &t->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ibytes, idx, GL_STATIC_DRAW);
    free(idx);

    /* Ein Programm fuer alle Kacheln - der Uebersetzer der SGX ist zu
       langsam, um dasselbe sechzehnmal zu tun. */
    static GLuint shared_prog;
    static GLint shared_mvp, shared_light, shared_col, shared_texmap;
    if (shared_prog) {
        t->prog = shared_prog;
        t->u_mvp = shared_mvp;
        t->u_light = shared_light;
        t->u_col = shared_col;
        t->u_texmap = shared_texmap;
        goto ready;
    }
    GLuint vs = compile_shader(GL_VERTEX_SHADER, TERRAIN_VERT);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, TERRAIN_FRAG);
    if (!vs || !fs) return 0;
    t->prog = glCreateProgram();
    glAttachShader(t->prog, vs);
    glAttachShader(t->prog, fs);
    glBindAttribLocation(t->prog, 0, "a_pos");
    glBindAttribLocation(t->prog, 1, "a_nrm");
    glLinkProgram(t->prog);
    GLint linked = GL_FALSE;
    glGetProgramiv(t->prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512] = {0};
        glGetProgramInfoLog(t->prog, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "Gelaende-Programm: %s\n", log);
        return 0;
    }
    t->u_mvp = glGetUniformLocation(t->prog, "u_mvp");
    t->u_light = glGetUniformLocation(t->prog, "u_light");
    t->u_col = glGetUniformLocation(t->prog, "u_col");
    t->u_texmap = glGetUniformLocation(t->prog, "u_texmap");
    glUseProgram(t->prog);
    glUniform1i(glGetUniformLocation(t->prog, "u_tex"), 0);
    glUniform1i(glGetUniformLocation(t->prog, "u_grain"), 1);
    /* Die Koernung wiederholt sich alle 24 m */
    glUniform1f(glGetUniformLocation(t->prog, "u_detail"), 1.0f / 600.0f);
    shared_prog = t->prog;
    shared_mvp = t->u_mvp;
    shared_light = t->u_light;
    shared_col = t->u_col;
    shared_texmap = t->u_texmap;
    printf("Gelaende-Shader: %d Bytes\n", (int)(strlen(TERRAIN_VERT) + strlen(TERRAIN_FRAG)));

ready:
    /* Die Puffer wieder loslassen.  Bleiben sie gebunden, deutet GL den
       Client-Zeiger der flachen Oberflaeche als Versatz in den Puffer - und
       der Treiber der SGX bricht mit "Offset to VBO out of bounds" ab. */
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (t->is_model) {
        static GLuint model_prog;
        if (!model_prog) {
            GLuint mv = compile_shader(GL_VERTEX_SHADER, MODEL_VERT);
            GLuint mf = compile_shader(GL_FRAGMENT_SHADER, MODEL_FRAG);
            model_prog = glCreateProgram();
            glAttachShader(model_prog, mv);
            glAttachShader(model_prog, mf);
            glBindAttribLocation(model_prog, 0, "a_pos");
            glBindAttribLocation(model_prog, 1, "a_nrm");
            glBindAttribLocation(model_prog, 2, "a_uv");
            glLinkProgram(model_prog);
            glUseProgram(model_prog);
            glUniform1i(glGetUniformLocation(model_prog, "u_tex"), 0);
            printf("Modell-Shader: %d Bytes\n", (int)(strlen(MODEL_VERT) + strlen(MODEL_FRAG)));
        }
        t->prog = model_prog;
        t->u_mvp = glGetUniformLocation(model_prog, "u_mvp");
        t->u_light = glGetUniformLocation(model_prog, "u_light");
        t->u_col = glGetUniformLocation(model_prog, "u_col");
        t->u_texmap = -1;
        load_group_textures(t, path);
    } else {
        load_texture(t, path, off);
    }

    printf("Kachel %-14s %7d Dreiecke, %2d Materialien, Ost %+7.1f km Nord %+7.1f km\n",
           t->name, t->nindices / 3, t->ngroups,
           t->local_center[0] / 1000.0f, t->local_center[1] / 1000.0f);
    return 1;
}

int terrain_visible(const struct terrain *t, const float eye[3], const float fwd[3],
                    float view_range_m) {
    float d[3] = { t->local_center[0] - eye[0], t->local_center[1] - eye[1],
                   t->local_center[2] - eye[2] };
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist - t->radius > view_range_m) return 0;              /* zu weit */
    if (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2] < -t->radius) return 0;  /* hinter uns */
    return 1;
}

/* Eine Koernung, die sich alle paar Meter wiederholt: das gebackene Bild hat
   18 m je Bildpunkt und ist aus der Naehe deshalb eine Flaeche.  Die Koernung
   gibt ihr wieder etwas zu sehen, ohne eine einzige Datei zu kosten - sie
   entsteht hier aus einer Zahlenfolge. */
static GLuint grain_texture(void) {
    static GLuint tex;
    if (tex) return tex;
    enum { N = 64 };
    unsigned char px[N * N];
    unsigned int seed = 12345u;
    for (int i = 0; i < N * N; ++i) {
        seed = seed * 1103515245u + 12345u;
        px[i] = (unsigned char)(40 + ((seed >> 16) % 216u));
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, N, N, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

/* Ein weisser Punkt fuer Kacheln ohne Bild - dann bleibt der Shader einer. */
static GLuint white_texture(void) {
    static GLuint tex;
    if (!tex) {
        unsigned short one = 0xFFFF;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &one);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    return tex;
}

/* Ein Flugplatz sieht nach Material besser aus als nach Bild: das gebackene
   Bild hat ueber fuenf Kilometer rund fuenf Meter je Bildpunkt, da ist die Bahn
   neun Punkte breit und die Markierung weg, waehrend die Materialgruppen die
   Kanten scharf haben.  Also der Kachel das Bild wieder wegnehmen - dann
   zeichnet terrain_draw Gruppe fuer Gruppe. */
int terrain_height_at(const struct terrain *t, float east, float north, float *height) {
    if (!t->grid || t->grid_n <= 0) return 0;
    int cx = (int)((east - t->grid_x0) / t->grid_cell);
    int cy = (int)((north - t->grid_y0) / t->grid_cell);
    if (cx < 0 || cy < 0 || cx >= t->grid_n || cy >= t->grid_n) return 0;
    /* Die eigene Zelle und ihre Nachbarn: eine leere Zelle (keine Eckpunkte
       darin) soll kein Loch im Boden sein. */
    float top = -1e30f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= t->grid_n || y >= t->grid_n) continue;
            float v = t->grid[y * t->grid_n + x];
            if (v > top) top = v;
        }
    }
    if (top < -1e29f) return 0;
    *height = top;
    return 1;
}

static float controls[4];

void terrain_set_controls(const float values[4]) {
    for (int i = 0; i < 4; ++i) controls[i] = values[i];
}

/* Drehung um eine Achse durch zwei Punkte, als Matrix fuer den Shader.
   Rodrigues, von Hand: erst an den Anfangspunkt, drehen, zurueck. */
static void axis_rotation(float out[16], const float p1[3], const float p2[3],
                          float angle_deg) {
    float ax = p2[0] - p1[0], ay = p2[1] - p1[1], az = p2[2] - p1[2];
    float len = sqrtf(ax * ax + ay * ay + az * az);
    if (len < 1e-6f) { memset(out, 0, 64); out[0] = out[5] = out[10] = out[15] = 1.0f; return; }
    ax /= len; ay /= len; az /= len;
    float a = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(a), s = sinf(a), t = 1.0f - c;
    float r[9] = {
        t * ax * ax + c,      t * ax * ay - s * az, t * ax * az + s * ay,
        t * ax * ay + s * az, t * ay * ay + c,      t * ay * az - s * ax,
        t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c
    };
    /* Spaltenweise, wie GL es erwartet, mit der Verschiebung um den Punkt. */
    out[0] = r[0]; out[1] = r[3]; out[2] = r[6];  out[3] = 0.0f;
    out[4] = r[1]; out[5] = r[4]; out[6] = r[7];  out[7] = 0.0f;
    out[8] = r[2]; out[9] = r[5]; out[10] = r[8]; out[11] = 0.0f;
    for (int i = 0; i < 3; ++i)
        out[12 + i] = p1[i] - (r[i * 3 + 0] * p1[0] + r[i * 3 + 1] * p1[1]
                               + r[i * 3 + 2] * p1[2]);
    out[15] = 1.0f;
}

void terrain_drop_texture(struct terrain *t) {
    if (t->tex) {
        glDeleteTextures(1, &t->tex);
        t->tex = 0;
    }
}

void terrain_draw(const struct terrain *t, const float mvp[16], const float light[3]) {
    if (!t->prog) return;
    glUseProgram(t->prog);
    glUniformMatrix4fv(t->u_mvp, 1, GL_FALSE, mvp);
    glUniform3fv(t->u_light, 1, light);
    if (t->u_texmap >= 0) glUniform3fv(t->u_texmap, 1, t->texmap);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t->tex ? t->tex : white_texture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, grain_texture());
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->ibo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    GLsizei stride = t->is_model ? 24 : 16;
    if (getenv("TERRAIN_DEBUG"))
        printf("  zeichne %-18s %u Bytes VBO, %d Eckpunkte, stride %d, %d Indizes\n",
               t->name, t->vbytes, t->nvertices, (int)stride, t->nindices);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)0);
    glVertexAttribPointer(1, 3, GL_BYTE, GL_TRUE, stride, (const void *)12);
    if (t->is_model) {
        static const float white[3] = { 1.0f, 1.0f, 1.0f };
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 24, (const void *)16);
        for (int i = 0; i < t->ngroups; ++i) {
            glBindTexture(GL_TEXTURE_2D, t->group[i].tex ? t->group[i].tex : white_texture());
            glUniform3fv(t->u_col, 1, t->group[i].tex ? white : t->group[i].col);
            /* Bewegliches Teil: um seine Achse drehen, so weit die Steuerung
               steht.  Der Rest des Flugzeugs bleibt, wie er ist. */
            int an = t->group[i].anim;
            if (an >= 0 && an < t->nanim) {
                float rot[16], m[16];
                axis_rotation(rot, t->anim[an].p1, t->anim[an].p2,
                              t->anim[an].factor * controls[t->anim[an].kind & 3]);
                mat_mul(m, mvp, rot);
                glUniformMatrix4fv(t->u_mvp, 1, GL_FALSE, m);
            } else {
                glUniformMatrix4fv(t->u_mvp, 1, GL_FALSE, mvp);
            }
            glDrawElements(GL_TRIANGLES, t->group[i].count,
                           t->wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                           (const void *)(size_t)(t->group[i].start * (t->wide ? 4 : 2)));
        }
        glDisableVertexAttribArray(2);
    } else if (t->tex) {
        /* Mit Bild braucht es die Aufteilung nach Material nicht mehr:
           ein Zeichenaufruf je Kachel statt siebzehn. */
        static const float white[3] = { 1.0f, 1.0f, 1.0f };
        glUniform3fv(t->u_col, 1, white);
        glDrawElements(GL_TRIANGLES, t->nindices,
                       t->wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const void *)0);
    } else {
        for (int i = 0; i < t->ngroups; ++i) {
            glUniform3fv(t->u_col, 1, t->group[i].col);
            glDrawElements(GL_TRIANGLES, t->group[i].count,
                           t->wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                           (const void *)(size_t)(t->group[i].start * (t->wide ? 4 : 2)));
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void terrain_frame_local(const struct terrain_frame *f, double lat_deg, double lon_deg,
                         double elev_m, float out[3]) {
    /* WGS84 nach erdfest, dann in das System der ersten Kachel drehen. */
    const double a = 6378137.0, e2 = 6.69437999014e-3;
    double lat = lat_deg * M_PI / 180.0, lon = lon_deg * M_PI / 180.0;
    double s = sin(lat), c = cos(lat);
    double nrad = a / sqrt(1.0 - e2 * s * s);
    double p[3] = {
        (nrad + elev_m) * c * cos(lon),
        (nrad + elev_m) * c * sin(lon),
        (nrad * (1.0 - e2) + elev_m) * s
    };
    double d[3] = { p[0] - f->origin[0], p[1] - f->origin[1], p[2] - f->origin[2] };
    out[0] = (float)(d[0] * f->east[0] + d[1] * f->east[1] + d[2] * f->east[2]);
    out[1] = (float)(d[0] * f->north[0] + d[1] * f->north[1] + d[2] * f->north[2]);
    out[2] = (float)(d[0] * f->up[0] + d[1] * f->up[1] + d[2] * f->up[2]);
}

void mat_perspective(float out[16], float fovy_deg, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fovy_deg * 0.5f * (float)M_PI / 180.0f);
    memset(out, 0, 16 * sizeof(float));
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (zfar + znear) / (znear - zfar);
    out[11] = -1.0f;
    out[14] = 2.0f * zfar * znear / (znear - zfar);
}

/* Blick aus der Kanzel: erst an den Ort, dann um Kurs, Laengs- und Querneigung.
   Die Achsen sind Ost, Nord, oben - also schaut die Nase bei Kurs 0 nach Nord. */
void mat_look(float out[16], const float eye[3], float yaw_deg, float pitch_deg, float roll_deg) {
    float cy = cosf(yaw_deg * (float)M_PI / 180.0f), sy = sinf(yaw_deg * (float)M_PI / 180.0f);
    float cp = cosf(pitch_deg * (float)M_PI / 180.0f), sp = sinf(pitch_deg * (float)M_PI / 180.0f);
    float cr = cosf(roll_deg * (float)M_PI / 180.0f), sr = sinf(roll_deg * (float)M_PI / 180.0f);

    float fwd[3] = { sy * cp, cy * cp, sp };
    float right0[3] = { cy, -sy, 0.0f };
    float up0[3] = {
        right0[1] * fwd[2] - right0[2] * fwd[1],
        right0[2] * fwd[0] - right0[0] * fwd[2],
        right0[0] * fwd[1] - right0[1] * fwd[0]
    };
    float right[3], up[3];
    for (int i = 0; i < 3; ++i) {
        right[i] = right0[i] * cr + up0[i] * sr;
        up[i] = up0[i] * cr - right0[i] * sr;
    }
    out[0] = right[0]; out[4] = right[1]; out[8]  = right[2];
    out[1] = up[0];    out[5] = up[1];    out[9]  = up[2];
    out[2] = -fwd[0];  out[6] = -fwd[1];  out[10] = -fwd[2];
    out[3] = out[7] = out[11] = 0.0f;
    out[12] = -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]);
    out[13] = -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]);
    out[14] = fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2];
    out[15] = 1.0f;
}

/* Ort und Lage des Flugzeugs als Matrix: erst rollen, dann nicken, dann auf
   den Kurs drehen, dann an den Ort. */
void mat_model(float out[16], const float pos[3], float yaw_deg, float pitch_deg, float roll_deg) {
    float cy = cosf(-yaw_deg * (float)M_PI / 180.0f), sy = sinf(-yaw_deg * (float)M_PI / 180.0f);
    float cp = cosf(pitch_deg * (float)M_PI / 180.0f), sp = sinf(pitch_deg * (float)M_PI / 180.0f);
    float cr = cosf(roll_deg * (float)M_PI / 180.0f), sr = sinf(roll_deg * (float)M_PI / 180.0f);

    float roll[9] = { cr, 0.0f, -sr,  0.0f, 1.0f, 0.0f,  sr, 0.0f, cr };       /* um Nord */
    float pitch[9] = { 1.0f, 0.0f, 0.0f,  0.0f, cp, sp,  0.0f, -sp, cp };      /* um Ost */
    float yaw[9] = { cy, sy, 0.0f,  -sy, cy, 0.0f,  0.0f, 0.0f, 1.0f };        /* um oben */

    float rp[9], m[9];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) sum += pitch[k * 3 + r] * roll[c * 3 + k];
            rp[c * 3 + r] = sum;
        }
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) sum += yaw[k * 3 + r] * rp[c * 3 + k];
            m[c * 3 + r] = sum;
        }
    memset(out, 0, 16 * sizeof(float));
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) out[c * 4 + r] = m[c * 3 + r];
    out[12] = pos[0];
    out[13] = pos[1];
    out[14] = pos[2];
    out[15] = 1.0f;
}

void mat_mul(float out[16], const float a[16], const float b[16]) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    memcpy(out, r, sizeof(r));
}
