/*
 * sgxprobe - compile a corpus of GLSL ES shaders on the device and report what
 *            the driver makes of them, together with every implementation limit
 *            a port would run into.
 *
 * Built for the Nokia N9/N950 (PowerVR SGX530, OpenGL ES 2.0) and, with the
 * same source, for the development machine, so that a failure on the device can
 * be told apart from a broken shader.
 *
 *   sgxprobe <directory>
 *
 * The directory holds <name>.vert and <name>.frag files; files sharing a name
 * are additionally linked into a program, which is where varying and uniform
 * budgets are actually enforced.  The exit code is the number of failures.
 */
#ifdef SGXPROBE_MIN_HEADERS
#include "compat/gles_min.h"
#else
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#endif

/* The PowerVR SGX530 offers no ES2 config that can draw into a pbuffer - its
 * pbuffer configs are ES1 and OpenVG only.  Where that is so, the probe needs
 * a native drawable, and an X pixmap is the one that costs nothing and shows
 * nothing.  Declared here rather than pulled in from Xlib.h, which the phone
 * this is written on does not have; only these four calls are used. */
#ifdef SGXPROBE_X11
typedef void XDisplay;
XDisplay *XOpenDisplay(const char *);
int XDefaultScreen(XDisplay *);
int XDefaultDepth(XDisplay *, int);
unsigned long XRootWindow(XDisplay *, int);
unsigned long XCreatePixmap(XDisplay *, unsigned long, unsigned, unsigned, unsigned);
int XSync(XDisplay *, int);
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SHADERS 512
#define MAX_NAME 256

static char *slurp(const char *path, long *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return NULL; }
    buf[len] = '\0';
    fclose(f);
    if (len_out) *len_out = len;
    return buf;
}

static void print_int(const char *name, GLenum e) {
    GLint v = -1;
    glGetIntegerv(e, &v);
    printf("  %-34s %d\n", name, v);
}

static void print_precision(const char *label, GLenum shader, GLenum type) {
    GLint range[2] = {0, 0}, precision = 0;
    glGetShaderPrecisionFormat(shader, type, range, &precision);
    printf("  %-34s range +-2^%d/%d, %d bits\n", label, range[0], range[1], precision);
}

/* The info log is the whole point of this program: print it verbatim. */
static void dump_log(const char *what, const char *name, GLuint obj, int is_program) {
    GLint len = 0;
    if (is_program) glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
    else glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) return;
    char *log = malloc((size_t)len + 1);
    if (!log) return;
    if (is_program) glGetProgramInfoLog(obj, len, NULL, log);
    else glGetShaderInfoLog(obj, len, NULL, log);
    log[len] = '\0';
    printf("    --- %s log for %s ---\n", what, name);
    for (char *line = strtok(log, "\n"); line; line = strtok(NULL, "\n")) printf("    | %s\n", line);
    free(log);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    /* The SGX's shader compiler takes minutes over a large shader; line
     * buffering makes the progress visible over a pipe instead of arriving
     * all at once at the end. */
    setvbuf(stdout, NULL, _IOLBF, 0);

#ifdef SGXPROBE_X11
    XDisplay *xdpy = XOpenDisplay(NULL);
    if (!xdpy) { fprintf(stderr, "cannot open the X display (set DISPLAY=:0)\n"); return 100; }
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)xdpy);
#else
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "no EGL display (on X11 set DISPLAY=:0)\n"); return 100; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed: %#x (on X11 set DISPLAY=:0)\n", eglGetError());
        return 100;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    /* Which configs exist at all - printed, because a driver that offers no
     * ES2 pbuffer is itself a finding, and the list says what it does offer. */
    EGLConfig all[128];
    EGLint nall = 0;
    eglGetConfigs(dpy, all, 128, &nall);
    printf("EGL configs: %d\n", nall);
    EGLConfig cfg = NULL, pixmap_cfg = NULL;
    int printed = 0;
    for (EGLint i = 0; i < nall; ++i) {
        EGLint renderable = 0, surface = 0, r = 0, g = 0, b = 0, a = 0, d = 0, st = 0;
        eglGetConfigAttrib(dpy, all[i], EGL_RENDERABLE_TYPE, &renderable);
        eglGetConfigAttrib(dpy, all[i], EGL_SURFACE_TYPE, &surface);
        eglGetConfigAttrib(dpy, all[i], EGL_RED_SIZE, &r);
        eglGetConfigAttrib(dpy, all[i], EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(dpy, all[i], EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(dpy, all[i], EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(dpy, all[i], EGL_DEPTH_SIZE, &d);
        eglGetConfigAttrib(dpy, all[i], EGL_STENCIL_SIZE, &st);
        int es2 = (renderable & EGL_OPENGL_ES2_BIT) != 0;
        int pbuffer = (surface & EGL_PBUFFER_BIT) != 0;
        if (printed < 24) {
            printf("  cfg%-3d rgba %d%d%d%d depth %2d stencil %d  renderable %#x%s  surface %#x%s\n",
                   i, r, g, b, a, d, st, renderable, es2 ? " (ES2)" : "",
                   surface, pbuffer ? " (pbuffer)" : "");
            ++printed;
        }
        if (!cfg && es2 && pbuffer) cfg = all[i];
        if (!pixmap_cfg && es2 && (surface & EGL_PIXMAP_BIT)) pixmap_cfg = all[i];
    }

    const EGLint pb_attr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface surf = EGL_NO_SURFACE;
    if (cfg) {
        surf = eglCreatePbufferSurface(dpy, cfg, pb_attr);
        printf("\nsurface: pbuffer\n");
    }
#ifdef SGXPROBE_X11
    else if (pixmap_cfg) {
        /* The pixmap's depth has to match what the config draws: the buffer
         * size is that depth for the packed formats this driver offers. */
        EGLint depth = 0;
        eglGetConfigAttrib(dpy, pixmap_cfg, EGL_BUFFER_SIZE, &depth);
        int screen = XDefaultScreen(xdpy);
        if (depth != XDefaultDepth(xdpy, screen)) {
            printf("\nnote: config buffer size %d, X default depth %d\n",
                   depth, XDefaultDepth(xdpy, screen));
        }
        unsigned long pixmap = XCreatePixmap(xdpy, XRootWindow(xdpy, screen), 16, 16,
                                             (unsigned)depth);
        XSync(xdpy, 0);
        cfg = pixmap_cfg;
        surf = eglCreatePixmapSurface(dpy, cfg, (EGLNativePixmapType)pixmap, NULL);
        printf("\nsurface: X pixmap, depth %d (no ES2 config here can do pbuffer)\n", depth);
    }
#endif
    if (!cfg) {
        fprintf(stderr, "\nno ES2-renderable config with a usable surface type\n");
        return 100;
    }
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "pbuffer or context failed: %#x\n", eglGetError());
        return 100;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "eglMakeCurrent failed: %#x\n", eglGetError());
        return 100;
    }

    printf("EGL      %d.%d  %s\n", major, minor, eglQueryString(dpy, EGL_VERSION));
    printf("VENDOR   %s\n", glGetString(GL_VENDOR));
    printf("RENDERER %s\n", glGetString(GL_RENDERER));
    printf("VERSION  %s\n", glGetString(GL_VERSION));
    printf("GLSL     %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("EXT      %s\n\n", glGetString(GL_EXTENSIONS));

    printf("limits\n");
    print_int("MAX_VERTEX_ATTRIBS", GL_MAX_VERTEX_ATTRIBS);
    print_int("MAX_VERTEX_UNIFORM_VECTORS", GL_MAX_VERTEX_UNIFORM_VECTORS);
    print_int("MAX_FRAGMENT_UNIFORM_VECTORS", GL_MAX_FRAGMENT_UNIFORM_VECTORS);
    print_int("MAX_VARYING_VECTORS", GL_MAX_VARYING_VECTORS);
    print_int("MAX_TEXTURE_IMAGE_UNITS", GL_MAX_TEXTURE_IMAGE_UNITS);
    print_int("MAX_VERTEX_TEXTURE_IMAGE_UNITS", GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS);
    print_int("MAX_COMBINED_TEXTURE_IMAGE_UNITS", GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
    print_int("MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE);
    print_int("MAX_CUBE_MAP_TEXTURE_SIZE", GL_MAX_CUBE_MAP_TEXTURE_SIZE);
    print_int("MAX_RENDERBUFFER_SIZE", GL_MAX_RENDERBUFFER_SIZE);
    GLboolean compiler = GL_FALSE;
    glGetBooleanv(GL_SHADER_COMPILER, &compiler);
    printf("  %-34s %s\n", "SHADER_COMPILER", compiler ? "yes" : "no");
    print_precision("fragment highp float", GL_FRAGMENT_SHADER, GL_HIGH_FLOAT);
    print_precision("fragment mediump float", GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT);
    print_precision("vertex highp float", GL_VERTEX_SHADER, GL_HIGH_FLOAT);
    printf("\n");

    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "cannot open %s\n", dir); return 100; }

    static char names[MAX_SHADERS][MAX_NAME];
    static GLuint objs[MAX_SHADERS];
    static int kinds[MAX_SHADERS];          /* 0 = vertex, 1 = fragment */
    int n = 0, failed = 0, compiled = 0;

    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_SHADERS) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        int kind;
        if (!strcmp(dot, ".vert")) kind = 0;
        else if (!strcmp(dot, ".frag")) kind = 1;
        else continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        long len = 0;
        char *src = slurp(path, &len);
        if (!src) { printf("READFAIL %s\n", e->d_name); ++failed; continue; }

        GLuint sh = glCreateShader(kind ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        const char *p = src;
        glShaderSource(sh, 1, &p, NULL);
        glCompileShader(sh);
        GLint ok = GL_FALSE;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        printf("%-4s %-40s %ld bytes\n", ok ? "OK" : "FAIL", e->d_name, len);
        dump_log("compile", e->d_name, sh, 0);
        free(src);
        if (!ok) { ++failed; glDeleteShader(sh); continue; }
        ++compiled;
        snprintf(names[n], MAX_NAME, "%.*s", (int)(dot - e->d_name), e->d_name);
        objs[n] = sh;
        kinds[n] = kind;
        ++n;
    }
    closedir(d);

    printf("\nprograms\n");
    int linked = 0;
    for (int i = 0; i < n; ++i) {
        if (kinds[i] != 0) continue;
        for (int j = 0; j < n; ++j) {
            if (kinds[j] != 1 || strcmp(names[i], names[j])) continue;
            GLuint prog = glCreateProgram();
            glAttachShader(prog, objs[i]);
            glAttachShader(prog, objs[j]);
            glLinkProgram(prog);
            GLint ok = GL_FALSE;
            glGetProgramiv(prog, GL_LINK_STATUS, &ok);
            printf("%-4s %s\n", ok ? "LINK" : "FAIL", names[i]);
            dump_log("link", names[i], prog, 1);
            if (ok) ++linked; else ++failed;
            glDeleteProgram(prog);
        }
    }

    printf("\n%d compiled, %d programs linked, %d failures\n", compiled, linked, failed);
    return failed > 250 ? 250 : failed;
}
